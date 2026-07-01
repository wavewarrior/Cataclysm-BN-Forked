#ifdef COOP_ENABLED

#include "coop_menu.h"

#include "coop_client.h"
#include "coop_server.h"
#include "coop_session.h"
#include "debug.h"
#include "input.h"
#include "output.h"
#include "string_input_popup.h"
#include "translations.h"
#include "ui.h"
#include "worldfactory.h"

#include <SDL3_net/SDL_net.h>

namespace coop_menu {

auto run() -> void {
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
}

auto start_host() -> void {
    // Let user pick a world
    WORLDINFO *world = world_generator->pick_world( true );
    if( !world ) {
        return; // user cancelled
    }

    static coop_server srv;
    if( !srv.listen( 8080 ) ) {
        popup( _( "Failed to start server on port 8080." ) );
        return;
    }

    // Get local IP for display
    std::string display_ip = "?.?.?.?";
    {
        int n = 0;
        NET_Address **addrs = NET_GetLocalAddresses( &n );
        if( addrs && n > 0 ) {
            const char *str = NET_GetAddressString( addrs[0] );
            if( str ) {
                display_ip = str;
            }
            for( int i = 0; i < n; ++i ) {
                NET_UnrefAddress( addrs[i] );
            }
            SDL_free( addrs );
        }
    }

    // Show waiting message and poll for a client; Escape cancels.
    popup( string_format(
               _( "Waiting for client...\nShare IP: %s:8080\n\nPress Escape to cancel." ),
               display_ip ), PF_GET_KEY );
    // PF_GET_KEY already consumed one key; now spin until a client connects or user
    // re-presses Escape (each handle_input call has a 200 ms timeout).
    {
        input_context ctxt( "COOP_WAIT" );
        ctxt.register_action( "QUIT" );
        ctxt.register_action( "ANY_INPUT" );

        bool cancelled = false;
        while( !srv.try_accept() ) {
            const std::string action = ctxt.handle_input( 200 );
            if( action == "QUIT" ) {
                cancelled = true;
                break;
            }
        }

        if( cancelled ) {
            srv.shutdown();
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

    // Session established
    popup( _( "Client connected!\nWorld setup not yet implemented." ) );
}

auto start_join() -> void {
    std::string ip = string_input_popup()
                     .title( _( "Enter host IP address:" ) )
                     .width( 30 )
                     .query_string();

    if( ip.empty() ) {
        return; // cancelled
    }

    static coop_client cli;
    if( !cli.connect( ip, 8080 ) ) {
        popup( string_format( _( "Failed to connect to %s:8080." ), ip ) );
        return;
    }

    if( !cli.handshake() ) {
        popup( _( "Handshake failed." ) );
        cli.shutdown();
        return;
    }

    popup( _( "Connected! Game start not yet fully implemented." ) );
}

} // namespace coop_menu

#endif // COOP_ENABLED
