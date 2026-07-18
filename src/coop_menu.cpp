#ifdef COOP_ENABLED

#include "coop_menu.h"

#include "avatar.h"
#include "coop_client.h"
#include "coop_server.h"
#include "coop_session.h"
#include "debug.h"
#include "game.h"
#include "input.h"
#include "output.h"
#include "popup.h"
#include "string_input_popup.h"
#include "translations.h"
#include "ui.h"
#include "ui_manager.h"
#include "worldfactory.h"
#include "options.h"
#include "overmapbuffer_registry.h" // g_active_dimension_id

#include <SDL3_net/SDL_net.h>

namespace coop_menu
{

auto run() -> bool
{
    uilist menu;
    menu.title = _( "CO-OP" );
    menu.addentry( 0, true, 'h', _( "Host a game" ) );
    menu.addentry( 1, true, 'j', _( "Join a game" ) );
    menu.addentry( 2, true, 'q', _( "Back" ) );
    menu.query();

    switch( menu.ret ) {
        case 0:
            start_host();
            break;
        case 1:
            start_join();
            break;
        default:
            break;
    }
    return coop_session::get().is_coop();
}

auto start_host() -> void
{
    // Let user pick a world
    WORLDINFO* world = world_generator->pick_world( true );
    if( !world ) {
        return; // user cancelled
    }

    world_generator->set_active_world( world );
    try {
        g->setup();
    } catch( const std::exception& err ) {
        popup( string_format( _( "World setup failed: %s" ), err.what() ) );
        return;
    }

    // Load the most recent save in the chosen world.
    if( world->world_saves.empty() ) {
        popup( _( "No saves found in this world. Start a single-player game first." ) );
        return;
    }
    if( !g->load( world->world_saves.front() ) ) {
        popup( _( "Failed to load save." ) );
        return;
    }

    const int coop_port = get_option<int>( "COOP_PORT" );
    static coop_server srv;
    if( !srv.listen( coop_port ) ) {
        popup( string_format( _( "Failed to start server on port %d." ), coop_port ) );
        return;
    }

    // Get local LAN IP for display.
    // Preference order: IPv4 private (192.168/10/172.16) > other non-loopback > fallback.
    // Skip IPv4 loopback (127.), IPv6 loopback (::1), IPv6 link-local (fe80:).
    std::string display_ip = "?.?.?.?";
    {
        int n = 0;
        NET_Address **addrs = NET_GetLocalAddresses( &n );
        if( addrs && n > 0 ) {
            std::string any_non_loopback;
            std::string fallback;
            auto is_loopback = []( const std::string & a ) {
                return a.rfind( "127.", 0 ) == 0   // IPv4 loopback
                       || a == "::1"                   // IPv6 loopback
                       || a.rfind( "fe80:", 0 ) == 0;  // IPv6 link-local
            };
            auto is_ipv4_private = []( const std::string & a ) {
                return a.rfind( "192.168.", 0 ) == 0
                       || a.rfind( "10.", 0 ) == 0
                       || a.rfind( "172.", 0 ) == 0;   // good enough approximation
            };
            for( int i = 0; i < n; ++i ) {
                const char *str = NET_GetAddressString( addrs[i] );
                if( !str ) { continue; }
                std::string a( str );
                if( is_loopback( a ) ) {
                    if( fallback.empty() ) { fallback = a; }
                } else if( is_ipv4_private( a ) ) {
                    display_ip = a;   // best candidate — stop searching
                    break;
                } else {
                    if( any_non_loopback.empty() ) { any_non_loopback = a; }
                }
            }
            if( display_ip == "?.?.?.?" ) {
                display_ip = any_non_loopback.empty() ? fallback : any_non_loopback;
            }
            for( int i = 0; i < n; ++i ) { NET_UnrefAddress( addrs[i] ); }
            SDL_free( addrs );
        }
    }
    // "Waiting for client" — static_popup renders via RmlUI query_popup.
    // Root cause of empty popup: static_popup ctor calls rml_sync() when text=""
    // (construction), then wait_message() sets text but only calls invalidate_ui()
    // (marks resize).  rml_sync() re-runs only from the on_redraw callback, which
    // fires on ui_manager::redraw().  handle_input() alone does NOT drive redraw.
    // Fix: call ui_manager::redraw() each iteration so message_rml stays current.
    {
        const std::string wait_msg = string_format(
                                         _( "Waiting for client...\nShare IP: %s:%d\n\nPress Escape to cancel." ),
                                         display_ip, coop_port );
        static_popup wait_popup;
        wait_popup.wait_message( "%s", wait_msg );

        input_context ctxt( "COOP_WAIT" );
        ctxt.register_action( "QUIT" );

        bool cancelled = false;
        while( !srv.try_accept() ) {
            ui_manager::redraw();                          // syncs message_rml → visible text
            if( ctxt.handle_input( 200 ) == "QUIT" ) {
                cancelled = true;
                break;
            }
        }

        if( cancelled ) {
            srv.shutdown();
            g->coop_server_ = nullptr;
            return;
        }
    }
    // try_accept() already stored the client; wait_for_client() returns immediately.
    if( !srv.wait_for_client() ) {
        popup( _( "No client connected." ) );
        srv.shutdown();
        return;
    }

    if( !srv.handshake() ) {
        popup( _( "Handshake failed." ) );
        srv.shutdown();
        return;
    }

    // Session established — register on g
    g->coop_server_ = &srv;
    coop_session::get().dimension_id = g_active_dimension_id;

    if( !srv.send_world_seed( g->u.get_name() ) ) {
        popup( _( "Failed to send world seed." ) );
        srv.shutdown();
        g->coop_server_ = nullptr;
        return;
    }
    // C6: wait up to 3 s for the client's join_info (blocking recv on main thread,
    // same pattern as handshake/send_world_seed — receiver thread NOT yet running).
    srv.wait_for_join_info( 3000 );
    const tripoint_abs_ms proxy_spawn = srv.client_join_pos().value_or( g->u.abs_pos() );
    srv.spawn_proxy_npc( proxy_spawn, "Partner" );
    if( !srv.send_initial_sync() ) {
        popup( _( "Failed to send initial sync." ) );
        srv.shutdown();
        g->coop_server_ = nullptr;
        return;
    }
    // Start receiver thread last — after all pre-game setup is complete.
    srv.start_receiver_thread();
}

auto start_join() -> void
{
    std::string ip =
        string_input_popup().title( _( "Enter host IP (or IP:port):" ) ).width( 30 ).query_string();

    if( ip.empty() ) {
    return; // cancelled
}

int port = get_option<int>( "COOP_PORT" );
// Parse "ip:port" syntax — last colon wins (supports bare IPv4 and bracketed IPv6).
if( const auto colon = ip.rfind( ':' ); colon != std::string::npos ) {
    const std::string port_str = ip.substr( colon + 1 );
        try {
            port = std::stoi( port_str );
        } catch( ... ) {
            popup( string_format( _( "Invalid port: %s" ), port_str ) );
            return;
        }
        if( port < 1 || port > 65535 ) {
            popup( string_format( _( "Port out of range (1-65535): %d" ), port ) );
            return;
        }
        ip = ip.substr( 0, colon );
    }

    static coop_client cli;
    if( !cli.connect( ip, port ) ) {
    popup( string_format( _( "Failed to connect to %s:%d." ), ip, port ) );
        return;
    }

    if( !cli.handshake() ) {
    popup( _( "Handshake failed." ) );
        cli.shutdown();
        return;
    }

    if( !cli.receive_world_seed() ) {
    popup( _( "Failed to receive world seed." ) );
        cli.shutdown();
        return;
    }

    // C6: send join_info BEFORE g->setup() so it arrives within the host's 3-second window.
    // g->u.abs_pos() is valid pre-setup; game::setup() does not reset the avatar position.
    // g->setup() can take several seconds on modded worlds — sending after it would always
    // race-lose against wait_for_join_info(3000).
    if( !cli.send_join_info() ) {
    DebugLog( DL::Info, DC::Main ) << "[coop] send_join_info failed — host uses spawn fallback";
    }

    try {
        g->setup();
    } catch( const std::exception& err ) {
        popup( string_format( _( "Client setup failed: %s" ), err.what() ) );
        cli.shutdown();
        return;
    }

    cli.apply_world_seed_to_avatar();

    // Session established — register on g
    g->coop_client_ = &cli;
    coop_session::get().dimension_id = g_active_dimension_id;
}

} // namespace coop_menu

auto show_coop_popup( const std::string& message ) -> bool
{
    return query_popup()
    .context( "COOP_POPUP" )
    .message( "%s", message )
    .option( "CONFIRM" )
    .option( "QUIT" )
    .allow_cancel( true )
    .query()
    .action == "CONFIRM";
}

#endif // COOP_ENABLED
