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

#include <SDL3_net/SDL_net.h>

namespace coop_menu {

auto run() -> bool {
    uilist menu;
    menu.title = _("CO-OP");
    menu.addentry(0, true, 'h', _("Host a game"));
    menu.addentry(1, true, 'j', _("Join a game"));
    menu.addentry(2, true, 'q', _("Back"));
    menu.query();

    switch (menu.ret) {
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

auto start_host() -> void {
    // Let user pick a world
    WORLDINFO* world = world_generator->pick_world(true);
    if (!world) {
        return; // user cancelled
    }

    world_generator->set_active_world(world);
    try {
        g->setup();
    } catch (const std::exception& err) {
        popup(string_format(_("World setup failed: %s"), err.what()));
        return;
    }

    // Load the most recent save in the chosen world.
    if (world->world_saves.empty()) {
        popup(_("No saves found in this world. Start a single-player game first."));
        return;
    }
    if (!g->load(world->world_saves.front())) {
        popup(_("Failed to load save."));
        return;
    }

    static coop_server srv;
    if (!srv.listen(8080)) {
        popup(_("Failed to start server on port 8080."));
        return;
    }

    // Get local IP for display
    std::string display_ip = "?.?.?.?";
    {
        int n = 0;
        NET_Address** addrs = NET_GetLocalAddresses(&n);
        if (addrs && n > 0) {
            const char* str = NET_GetAddressString(addrs[0]);
            if (str) { display_ip = str; }
            for (int i = 0; i < n; ++i) { NET_UnrefAddress(addrs[i]); }
            SDL_free(addrs);
        }
    }
    // Show "Waiting for client" using a plain ui_adaptor + catacurses window.
    // static_popup was replaced here because it relies on the query_popup RmlUI model
    // (rml_open / g_rml_query_popup_model_active); if anything in the loading chain
    // already holds that model, rml_open() fails silently and the popup renders empty
    // with no text — confirmed in practice.  A direct catacurses draw bypasses the
    // model system entirely and is always visible once the game is loaded.
    {
        const std::string wait_msg = string_format(
            _("Waiting for client...\nShare IP: %s:8080\n\nPress Escape to cancel."),
            display_ip);

        catacurses::window wait_win;
        ui_adaptor wait_ui;
        wait_ui.on_screen_resize( [&]( ui_adaptor& ui ) {
            const int w = std::min( 52, TERMX - 4 );
            const int h = 6;
            const int x = ( TERMX - w ) / 2;
            const int y = ( TERMY - h ) / 2;
            wait_win = catacurses::newwin( h, w, point( x, y ) );
            ui.position_from_window( wait_win );
        } );
        wait_ui.mark_resize();
        wait_ui.on_redraw( [&]( const ui_adaptor& ) {
            werase( wait_win );
            draw_border( wait_win );
            fold_and_print( wait_win, point( 1, 1 ), getmaxx( wait_win ) - 2,
                            c_white, wait_msg );
            wnoutrefresh( wait_win );
        } );

        input_context ctxt( "COOP_WAIT" );
        ctxt.register_action( "QUIT" );

        bool cancelled = false;
        while( !srv.try_accept() ) {
            // handle_input(200) provides 200ms pacing, Escape detection, and
            // drives ui_manager::redraw which repaints wait_win each iteration.
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
    if (!srv.wait_for_client()) {
        popup(_("No client connected."));
        srv.shutdown();
        return;
    }

    if (!srv.handshake()) {
        popup(_("Handshake failed."));
        srv.shutdown();
        return;
    }

    // Session established — register on g
    g->coop_server_ = &srv;

    if (!srv.send_world_seed(g->u.get_name())) {
        popup(_("Failed to send world seed."));
        srv.shutdown();
        g->coop_server_ = nullptr;
        return;
    }
    // C6: wait up to 3 s for the client's join_info (blocking recv on main thread,
    // same pattern as handshake/send_world_seed — receiver thread NOT yet running).
    srv.wait_for_join_info(3000);
    const tripoint_abs_ms proxy_spawn = srv.client_join_pos().value_or(g->u.abs_pos());
    srv.spawn_proxy_npc(proxy_spawn, "Partner");
    if (!srv.send_initial_sync()) {
        popup(_("Failed to send initial sync."));
        srv.shutdown();
        g->coop_server_ = nullptr;
        return;
    }
    // Start receiver thread last — after all pre-game setup is complete.
    srv.start_receiver_thread();
}

auto start_join() -> void {
    std::string ip =
        string_input_popup().title(_("Enter host IP address:")).width(30).query_string();

    if (ip.empty()) {
        return; // cancelled
    }

    static coop_client cli;
    if (!cli.connect(ip, 8080)) {
        popup(string_format(_("Failed to connect to %s:8080."), ip));
        return;
    }

    if (!cli.handshake()) {
        popup(_("Handshake failed."));
        cli.shutdown();
        return;
    }

    if (!cli.receive_world_seed()) {
        popup(_("Failed to receive world seed."));
        cli.shutdown();
        return;
    }

    // C6: send join_info BEFORE g->setup() so it arrives within the host's 3-second window.
    // g->u.abs_pos() is valid pre-setup; game::setup() does not reset the avatar position.
    // g->setup() can take several seconds on modded worlds — sending after it would always
    // race-lose against wait_for_join_info(3000).
    if (!cli.send_join_info()) {
        DebugLog(DL::Info, DC::Main) << "[coop] send_join_info failed — host uses spawn fallback";
    }

    try {
        g->setup();
    } catch (const std::exception& err) {
        popup(string_format(_("Client setup failed: %s"), err.what()));
        cli.shutdown();
        return;
    }

    cli.apply_world_seed_to_avatar();

    // Session established — register on g
    g->coop_client_ = &cli;
}

} // namespace coop_menu

#endif // COOP_ENABLED
