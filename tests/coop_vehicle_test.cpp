/**
 * Vehicle sync integration tests.
 *
 * Exercises the vehicle_state packet relay from client → host through
 * coop_sim_transport, verifying that the server's vehicle_id_map_ /
 * vehicle_id_map_rev_ lookup and map::displace_vehicle() pipeline works
 * end-to-end.
 *
 * Tags: [coop][vehicle]
 */

#include "avatar.h"
#include "catch/catch_amalgamated.hpp"
#include "coop_client.h"
#include "coop_server.h"
#include "coop_session.h"
#include "coop_sim_transport.h"
#include "game.h"
#include "map.h"
#include "map_helpers.h"
#include "npc.h"
#include "state_helpers.h"
#include "type_id.h"
#include "veh_type.h"
#include "vehicle.h"

namespace {

/// RAII guard: sets coop_session::mode on construction, restores on destruction.
struct coop_mode_guard {
    coop_mode saved;
    explicit coop_mode_guard(coop_mode m): saved(coop_session::get().mode) {
        coop_session::get().mode = m;
    }
    ~coop_mode_guard() { coop_session::get().mode = saved; }
    coop_mode_guard(const coop_mode_guard&) = delete;
    auto operator=(const coop_mode_guard&) -> coop_mode_guard& = delete;
};

/// In-process co-op test harness (copied from coop_inproc_test.cpp).
struct inproc_harness {
    coop_server srv;
    coop_client cli;
    coop_sim_transport* srv_tx = nullptr;
    coop_sim_transport* cli_tx = nullptr;
    npc* proxy = nullptr;

    auto setup() -> void {
        clear_all_state();
        build_test_map(ter_id("t_grass"));

        auto& sess = coop_session::get();
        sess.mode = coop_mode::host;
        sess.partner_name = "TestClient";
        sess.dimension_id = g->get_current_dimension_id();

        auto* stx = new coop_sim_transport();
        auto* ctx = new coop_sim_transport();
        stx->wire_peer(ctx);
        ctx->wire_peer(stx);
        srv_tx = stx;
        cli_tx = ctx;

        srv.set_transport_for_test(std::unique_ptr<coop_transport>(stx));
        cli.set_transport_for_test(std::unique_ptr<coop_transport>(ctx));
        srv.set_running_for_test(true);
        srv.set_join_phase_for_test(client_join_phase::connected);

        REQUIRE(srv.send_world_seed("TestClient"));
        {
            coop_mode_guard mcli(coop_mode::client);
            REQUIRE(cli.receive_world_seed());
            REQUIRE(cli.send_join_info());
        }
        REQUIRE(srv.wait_for_join_info());

        const auto spawn_pos = srv.client_join_pos().value_or(g->u.abs_pos());
        proxy = srv.spawn_proxy_npc(spawn_pos, "TestClient");
        REQUIRE(proxy != nullptr);

        REQUIRE(srv.send_initial_sync());
        {
            coop_mode_guard mcli(coop_mode::client);
            cli.coop_world_tick();
        }
    }

    auto tick() -> void {
        {
            coop_mode_guard mcli(coop_mode::client);
            cli.coop_world_tick();
        }
        srv.process_incoming_for_test();
        srv.coop_world_tick();
        srv.flush_send_queue_for_test();
    }

    ~inproc_harness() {
        if (srv_tx) { srv_tx->close_abruptly(); }
        if (cli_tx) { cli_tx->close_abruptly(); }
        clear_vehicles();
        auto& sess = coop_session::get();
        sess.mode = coop_mode::none;
        sess.proxy_npc_id = character_id();
        sess.partner_name.clear();
    }
};

/// Build a vehicle_state JSON packet (type 42) for the given vid and abs position.
auto make_vehicle_state_json(uint32_t vid, int ax, int ay, int az) -> std::string {
    return R"({"t":42,"d":{"vid":)" + std::to_string(vid) + R"(,"ax":)" + std::to_string(ax)
         + R"(,"ay":)" + std::to_string(ay) + R"(,"az":)" + std::to_string(az)
         + R"(,"face_x":0,"face_y":1,"velocity":0}})";
}

} // namespace

// ---------------------------------------------------------------------------
// Vehicle sync tests
// ---------------------------------------------------------------------------

TEST_CASE("vehicle: vehicle_state packet relays to host", "[coop][vehicle]") {
    inproc_harness h;
    h.setup();

    // Spawn a bicycle at a known position.
    const tripoint_bub_ms spawn_bub{50, 50, 0};
    vehicle* veh = g->m.add_vehicle(vproto_id("bicycle"), spawn_bub, 0_degrees, 0, 0);
    REQUIRE(veh != nullptr);

    // Register it in the server's vehicle ID maps.
    const uint32_t vid = h.srv.register_vehicle_for_test(veh);
    CHECK(vid > 0);

    // Compute the vehicle's current abs position so we can send a delta.
    const tripoint_abs_ms old_abs = g->m.bub_to_abs(veh->bub_ms_location());

    // Target: move the vehicle 3 tiles east.
    const tripoint_abs_ms new_abs{old_abs.x() + 3, old_abs.y(), old_abs.z()};

    // Inject a vehicle_state packet into the server's transport inbox.
    h.cli_tx->send(make_vehicle_state_json(vid, new_abs.x(), new_abs.y(), new_abs.z()));

    // Process incoming + server tick (no full world sim needed for vehicle relay).
    h.srv.process_incoming_for_test();
    h.srv.coop_world_tick();

    // Verify the vehicle moved to the target position.
    const tripoint_abs_ms actual_abs = g->m.bub_to_abs(veh->bub_ms_location());
    CHECK(actual_abs.x() == new_abs.x());
    CHECK(actual_abs.y() == new_abs.y());
    CHECK(actual_abs.z() == new_abs.z());
}

TEST_CASE("vehicle: vehicle_id_map persists across multiple updates", "[coop][vehicle]") {
    inproc_harness h;
    h.setup();

    const tripoint_bub_ms spawn_bub{50, 50, 0};
    vehicle* veh = g->m.add_vehicle(vproto_id("bicycle"), spawn_bub, 0_degrees, 0, 0);
    REQUIRE(veh != nullptr);

    const uint32_t vid = h.srv.register_vehicle_for_test(veh);

    const tripoint_abs_ms base_abs = g->m.bub_to_abs(veh->bub_ms_location());

    // Send 3 sequential updates, each moving the vehicle 1 tile further east.
    for (int i = 1; i <= 3; ++i) {
        const tripoint_abs_ms target{base_abs.x() + i, base_abs.y(), base_abs.z()};
        h.cli_tx->send(make_vehicle_state_json(vid, target.x(), target.y(), target.z()));
        h.srv.process_incoming_for_test();
        h.srv.coop_world_tick();

        const tripoint_abs_ms actual = g->m.bub_to_abs(veh->bub_ms_location());
        CHECK(actual.x() == target.x());
        CHECK(actual.y() == target.y());
    }
}

TEST_CASE("vehicle: unknown vid is silently ignored", "[coop][vehicle]") {
    inproc_harness h;
    h.setup();

    // Send a vehicle_state packet with a vid that is NOT registered.
    // This must not crash or assert — the server silently ignores unknown vids.
    h.cli_tx->send(make_vehicle_state_json(9999, 60, 60, 0));

    // Tick should complete without crash.
    h.srv.process_incoming_for_test();
    h.srv.coop_world_tick();
    h.srv.process_incoming_for_test();
    h.srv.coop_world_tick();

    // If we got here, the unknown vid was handled gracefully.
    SUCCEED();
}
