/**
 * Co-op vehicle tests.
 *
 * Step 5 covers the client -> host driving relays (VEH_CONTROL, VEH_DRIVE) and the
 * proxy boarding that makes it a real driver, plus the host's cruise thrust for a
 * proxy driver.
 * Step 6 covers the host -> client vehicle pose stream (the `"vehicles"` sync member):
 * JSON round-trip, applying a pose to this side's copy, and the host-driven flag.
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
#include "coop_vehicle_sync.h"
#include "game.h"
#include "game_constants.h"
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
        // Pin the host avatar to the reality-bubble centre before recording u_start.
        //
        // The join handshake and the client's sync reconciliation move g->u around, and a
        // bare setpos(abs) does not shift the loaded grid.  Anything that turns an absolute
        // position into a bubble position (vehicle::bub_ms_location, vpart_reference::pos,
        // Character::bub_pos) derives its origin from the AVATAR's tile, while add_vehicle,
        // board_vehicle and the vehicle cache use map::get_abs_sub().  When the avatar's
        // submap no longer quantizes to the loaded anchor the two frames disagree by whole
        // submaps, so a car that was just added is invisible to veh_at() and board_vehicle
        // refuses it - order-dependent, because it depends on where earlier tests left the
        // avatar.  The centre tile of the map's own frame quantizes back to get_abs_sub()
        // exactly, so placing the avatar there restores the invariant the engine asserts
        // (game.cpp debug_assert_player_map_origin).
        g->u.setpos(tripoint_bub_ms(g_half_mapsize_x, g_half_mapsize_y, g->u.abs_pos().z()));
        REQUIRE(g->m.get_abs_sub() == player_reality_bubble_origin().xy());
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

// ---------------------------------------------------------------------------
// Step 6: host -> client vehicle pose stream
// ---------------------------------------------------------------------------

TEST_CASE("vehicle: pose json round trip", "[coop][vehicle]") {
    std::vector<coop_vehicle_pose> out = {
        {
            .vid = 7,
            .anchor = tripoint_abs_ms(120, -40, 1),
            .frac_x = 0.25f,
            .frac_y = -0.5f,
            .angle = 0.2618f,
            .face_deg = 15,
            .steer_deg = 30,
            .velocity = 500,
            .cruise_velocity = 179,
            .engine_on = true,
            .authority = true,
            .host_driving = true,
        },
        {
            .vid = 8,
            .anchor = tripoint_abs_ms(-3, 0, -2),
            .frac_x = 0.0f,
            .frac_y = 0.0f,
            .angle = 0.0f,
            .face_deg = 270,
            .steer_deg = -90,
            .velocity = -120,
            .cruise_velocity = 0,
            .engine_on = false,
            .authority = false,
            .host_driving = false,
        },
    };

    std::ostringstream oss;
    {
        JsonOut jout(oss);
        jout.start_object();
        jout.member("vehicles");
        write_coop_vehicle_poses(jout, out);
        jout.end_object();
    }

    std::istringstream iss(oss.str());
    JsonIn jin(iss);
    jin.start_object();
    const std::string key = jin.get_member_name();
    CHECK(key == "vehicles");
    const std::vector<coop_vehicle_pose> in = read_coop_vehicle_poses(jin);

    REQUIRE(in.size() == out.size());
    CHECK(in[0] == out[0]);
    CHECK(in[1] == out[1]);
}

TEST_CASE("vehicle: apply pose moves and rotates copy", "[coop][vehicle]") {
    clear_all_state();
    build_test_map(ter_id("t_pavement"));
    vehicle* veh =
        g->m.add_vehicle(vproto_id("car_test"), tripoint_bub_ms(60, 60, 0), 0_degrees, 100, 0);
    REQUIRE(veh != nullptr);

    const tripoint_abs_ms base = veh->abs_ms_location();
    const float angle = units::to_radians(15_degrees);
    const coop_vehicle_pose pose = {
        .vid = 1,
        .anchor = tripoint_abs_ms(base.x() + 3, base.y(), base.z()),
        .frac_x = 0.25f,
        .frac_y = 0.0f,
        .angle = angle,
        .face_deg = 15,
        .steer_deg = 30,
        .velocity = 500,
        .cruise_velocity = 0,
        .engine_on = true,
        .authority = true,
        .host_driving = false,
    };

    CHECK(apply_coop_vehicle_pose(g->m, *veh, pose));

    CHECK(veh->abs_ms_location().x() == base.x() + 3);
    CHECK(veh->box2d_position_authority);
    CHECK(veh->physics_angle == Catch::Approx(angle).margin(1e-6f));
    CHECK(lround(units::to_degrees(normalize(veh->face.dir()))) == 15);
    CHECK(lround(units::to_degrees(normalize(veh->turn_dir))) == 30);
    CHECK(veh->velocity == 500);
    CHECK(veh->engine_on);

    // Part layout must match the authoritative refresh_precalc formula.
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    for (const int p : veh->all_standalone_parts()) {
        const vehicle_part& vp = veh->cpart(p);
        const float mx = static_cast<float>(vp.mount.x());
        const float my = static_cast<float>(vp.mount.y());
        const point_rel_ms want{std::lround(mx * c - my * s), std::lround(mx * s + my * c)};
        CHECK(vp.precalc[0] == want);
    }
}

TEST_CASE("vehicle: host sync flags the host-driven vehicle", "[coop][vehicle]") {
    inproc_harness h;
    h.setup();
    vehicle* veh = spawn_drivable_car(h);

    const tripoint_bub_ms ctrl = controls_pos(*veh);
    REQUIRE(ctrl != tripoint_bub_ms::zero());
    // Standing on the controls tile with controlling_vehicle is what player_in_control
    // checks; boarding is unnecessary and would fight the harness's reconcile.
    g->u.setpos(map_local_to_abs(g->m, ctrl));
    g->u.set_skill_level(skill_id("driving"), 10);
    g->u.controlling_vehicle = true;
    REQUIRE(veh->player_in_control(g->u));
    h.u_start = g->u.abs_pos();

    h.srv.coop_world_tick();
    h.srv.flush_send_queue_for_test();
    {
        coop_mode_guard mcli(coop_mode::client);
        h.cli.coop_world_tick();
    }

    CHECK(h.cli.host_driven_vehicle() == veh);
}
