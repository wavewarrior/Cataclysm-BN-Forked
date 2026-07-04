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
#include "string_input_popup.h"
#include "translations.h"
#include "ui.h"
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

    // Show waiting message and poll for a client; Escape cancels.
    popup(string_format(
              _("Waiting for client...\nShare IP: %s:8080\n\nPress Escape to cancel."), display_ip),
          PF_GET_KEY);
    // PF_GET_KEY already consumed one key; now spin until a client connects or user
    // re-presses Escape (each handle_input call has a 200 ms timeout).
    {
        input_context ctxt("COOP_WAIT");
        ctxt.register_action("QUIT");
        ctxt.register_action("ANY_INPUT");

        bool cancelled = false;
        while (!srv.try_accept()) {
            const std::string action = ctxt.handle_input(200);
            if (action == "QUIT") {
                cancelled = true;
                break;
            }
        }

        if (cancelled) {
            srv.shutdown();
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
    srv.spawn_proxy_npc(g->u.abs_pos(), "Partner"); // spawn near host
    if (!srv.send_initial_sync()) {
        popup(_("Failed to send initial sync."));
        srv.shutdown();
        g->coop_server_ = nullptr;
        return;
    }
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
