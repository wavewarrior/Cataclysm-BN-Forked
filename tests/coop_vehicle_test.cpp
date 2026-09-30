/**
 * Co-op vehicle tests.
 *
 * Covers the client -> host driving relays (VEH_CONTROL, VEH_DRIVE), the proxy
 * boarding that makes it a real driver, and the host's cruise thrust for a proxy
 * driver, plus the legacy vehicle_state relay still in place.
 *
 * Tags: [coop][vehicle]
 */
#include "avatar.h"
#include "catch/catch_amalgamated.hpp"
#include "character.h"
#include "coop_client.h"
#include "coop_server.h"
#include "coop_session.h"
#include "coop_sim_transport.h"
#include "game.h"
#include "json.h"
#include "map.h"
#include "map_helpers.h"
#include "npc.h"
#include "player_cmd.h"
#include "skill.h"
#include "state_helpers.h"
#include "type_id.h"
#include "units_utility.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vehicle_handle.h"
#include "vehicle_part.h"
#include "vpart_position.h"
#include "vpart_range.h"

#include <array>
#include <cmath>
#include <sstream>
#include <vector>

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
    tripoint_abs_ms u_start;

    auto setup() -> void {
        clear_all_state();
        build_test_map(ter_id("t_grass"));

        auto& sess = coop_session::get();
        sess.mode = coop_mode::host;
        sess.partner_name = "TestClient";
        sess.dimension_id = g->get_current_dimension_id().str();

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
        u_start = g->u.abs_pos();
    }

    auto tick() -> void {
        {
            coop_mode_guard mcli(coop_mode::client);
            cli.coop_world_tick();
        }
        restore_host_avatar();
        srv.process_incoming_for_test();
        srv.coop_world_tick();
        srv.flush_send_queue_for_test();
    }

    /// In-process artifact: host and client share one `g`, so the client's sync
    /// reconciliation teleports the HOST avatar onto the proxy's tile.  Put it back where
    /// it started, otherwise a car spawned next to the proxy collides with that body every
    /// server tick and a real host would never see that.
    auto restore_host_avatar() -> void {
        if (!g->u.in_vehicle && g->u.abs_pos() != u_start) { g->u.setpos(u_start); }
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

/// Spawn an undamaged, full-tank car_test clear of the proxy and the host avatar
/// (mounts span x -3..2, y -1..2), so neither creature body blocks it.
auto spawn_drivable_car(inproc_harness& h) -> vehicle* {
    const tripoint_bub_ms at =
        abs_to_map_local(g->m, h.proxy->abs_pos()) + tripoint_rel_ms(5, 5, 0);
    vehicle* veh = g->m.add_vehicle(vproto_id("car_test"), at, 0_degrees, 100, 0);
    REQUIRE(veh != nullptr);
    return veh;
}

/// Bubble position of the vehicle's first available CONTROLS part (zero when none).
auto controls_pos(vehicle& veh) -> tripoint_bub_ms {
    for (const vpart_reference& vp : veh.get_avail_parts("CONTROLS")) { return vp.pos(); }
    return tripoint_bub_ms::zero();
}

/// Finds a BOARDABLE part with a vehicle-free neighbour.  `tile_out` is the boardable
/// tile, `delta_out` the offset from it to that free neighbour.
auto boardable_with_free_neighbour(
    vehicle& veh, tripoint_bub_ms& tile_out, tripoint_rel_ms& delta_out) -> bool {
    static const std::array<tripoint_rel_ms, 8> offs = {
        {tripoint_rel_ms::north(), tripoint_rel_ms::south(), tripoint_rel_ms::east(),
         tripoint_rel_ms::west(), tripoint_rel_ms::north_east(), tripoint_rel_ms::north_west(),
         tripoint_rel_ms::south_east(), tripoint_rel_ms::south_west()}};
    for (const vpart_reference& vp : veh.get_avail_parts(VPFLAG_BOARDABLE)) {
        for (const tripoint_rel_ms& n : offs) {
            if (!g->m.veh_at(vp.pos() + n)) {
                tile_out = vp.pos();
                delta_out = n;
                return true;
            }
        }
    }
    return false;
}

/// Boards the proxy at the controls and hands it control through the VEH_CONTROL relay.
auto proxy_take_control(inproc_harness& h, vehicle& veh) -> void {
    const tripoint_bub_ms ctrl = controls_pos(veh);
    REQUIRE(ctrl != tripoint_bub_ms::zero());
    h.proxy->setpos(map_local_to_abs(g->m, ctrl));
    g->m.board_vehicle(ctrl, h.proxy);
    REQUIRE(h.proxy->in_vehicle);
    h.proxy->set_skill_level(skill_id("driving"), 10);
    h.cli.queue_action("VEH_CONTROL", R"({"on":true,"engine":true})");
    h.tick();
}

/// Build a vehicle_state JSON packet (type 42) for the given vid and abs position.
auto make_vehicle_state_json(uint32_t vid, int ax, int ay, int az) -> std::string {
    return R"({"t":42,"d":{"vid":)" + std::to_string(vid) + R"(,"ax":)" + std::to_string(ax)
         + R"(,"ay":)" + std::to_string(ay) + R"(,"az":)" + std::to_string(az)
         + R"(,"face_x":0,"face_y":1,"velocity":0}})";
}

} // namespace

// ---------------------------------------------------------------------------
// Step 5: client-driven vehicle relays (VEH_CONTROL / VEH_DRIVE / proxy boarding)
// ---------------------------------------------------------------------------

TEST_CASE("vehicle: client takes control via VEH_CONTROL relay", "[coop][vehicle]") {
    inproc_harness h;
    h.setup();
    vehicle* veh = spawn_drivable_car(h);

    proxy_take_control(h, *veh);

    CHECK(veh->player_in_control(*h.proxy));
    CHECK(h.proxy->controlling_vehicle);
    CHECK(veh->engine_on);
}

TEST_CASE("vehicle: client pre-steers via VEH_DRIVE relay", "[coop][vehicle]") {
    inproc_harness h;
    h.setup();
    vehicle* veh = spawn_drivable_car(h);

    proxy_take_control(h, *veh);
    h.cli.queue_action("VEH_DRIVE", R"({"x":1,"y":0,"z":0})");
    h.tick();

    CHECK(lround(units::to_degrees(normalize(veh->turn_dir))) == 15);
    CHECK(lround(units::to_degrees(normalize(veh->face.dir()))) == 0);
}

TEST_CASE("vehicle: proxy driver gets cruise control", "[coop][vehicle]") {
    inproc_harness h;
    h.setup();
    vehicle* veh = spawn_drivable_car(h);

    proxy_take_control(h, *veh);
    // Cruise is the only thrust path a proxy driver reaches: pldrive with cruise_on
    // delegates to cruise_thrust (which only sets cruise_velocity), and the velocity
    // itself comes from gain_moves()'s cruise thrust - dead without a driver.
    REQUIRE(veh->cruise_on);
    h.cli.queue_action("VEH_DRIVE", R"({"x":0,"y":-1,"z":0})");
    h.tick();
    CHECK(veh->cruise_velocity > 0);
    h.tick();
    h.tick();

    CHECK(veh->velocity > 0);
}

TEST_CASE("vehicle: proxy move command boards a vehicle", "[coop][vehicle]") {
    inproc_harness h;
    h.setup();
    vehicle* veh = spawn_drivable_car(h);

    tripoint_bub_ms seat;
    tripoint_rel_ms delta;
    REQUIRE(boardable_with_free_neighbour(*veh, seat, delta));
    h.proxy->setpos(map_local_to_abs(g->m, seat + delta));
    REQUIRE(!h.proxy->in_vehicle);

    player_cmd_t cmd;
    cmd.kind = player_cmd_kind::move;
    cmd.delta = tripoint_rel_ms(-delta.raw());
    h.srv.execute_player_cmd(h.proxy, cmd, 1);

    CHECK(h.proxy->in_vehicle);
    CHECK(h.proxy->bub_pos() == seat);
}


// ----------------------------------------------------------------------------------
// Legacy vehicle_state relay tests
// ----------------------------------------------------------------------------------
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
    const tripoint_abs_ms old_abs = bub_to_abs(veh->bub_ms_location());

    // Target: move the vehicle 3 tiles east.
    const tripoint_abs_ms new_abs{old_abs.x() + 3, old_abs.y(), old_abs.z()};

    // Inject a vehicle_state packet into the server's transport inbox.
    h.cli_tx->send(make_vehicle_state_json(vid, new_abs.x(), new_abs.y(), new_abs.z()));

    // Process incoming + server tick (no full world sim needed for vehicle relay).
    h.srv.process_incoming_for_test();
    h.srv.coop_world_tick();

    // Verify the vehicle moved to the target position.
    const tripoint_abs_ms actual_abs = bub_to_abs(veh->bub_ms_location());
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

    const tripoint_abs_ms base_abs = bub_to_abs(veh->bub_ms_location());

    // Send 3 sequential updates, each moving the vehicle 1 tile further east.
    for (int i = 1; i <= 3; ++i) {
        const tripoint_abs_ms target{base_abs.x() + i, base_abs.y(), base_abs.z()};
        h.cli_tx->send(make_vehicle_state_json(vid, target.x(), target.y(), target.z()));
        h.srv.process_incoming_for_test();
        h.srv.coop_world_tick();

        const tripoint_abs_ms actual = bub_to_abs(veh->bub_ms_location());
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
