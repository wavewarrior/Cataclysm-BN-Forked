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

    // Get local LAN IP for display — skip loopback (127.x.x.x) so the partner
    // gets a routable address.  Fall back to first available if nothing else found.
    std::string display_ip = "?.?.?.?";
    {
        int n = 0;
        NET_Address** addrs = NET_GetLocalAddresses(&n);
        if (addrs && n > 0) {
            std::string fallback;
            for (int i = 0; i < n; ++i) {
                const char* str = NET_GetAddressString(addrs[i]);
                if (!str) { continue; }
                std::string a(str);
                if (a.rfind("127.", 0) == 0) {
                    // loopback — keep as last-resort fallback only
                    if (fallback.empty()) { fallback = a; }
                } else {
                    display_ip = a;    // first non-loopback wins
                    break;
                }
            }
            if (display_ip == "?.?.?.?" && !fallback.empty()) {
                display_ip = fallback;
            }
            for (int i = 0; i < n; ++i) { NET_UnrefAddress(addrs[i]); }
            SDL_free(addrs);
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
            _("Waiting for client...\nShare IP: %s:8080\n\nPress Escape to cancel."),
            display_ip);
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
