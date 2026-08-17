#include "catch/catch_amalgamated.hpp"
#include "coordinates.h"
#include "game.h"
#include "map.h"
#include "map_helpers.h"
#include "monster.h"
#include "npc.h"
#include "state_helpers.h"
#include "type_id.h"
#include "veh_type.h"
#include "vehicle.h"

TEST_CASE("mps_cmps_round_trip_converges_to_zero", "[vehicle]") {
    constexpr auto max_iterations = 200;

    for (auto v = 1; v <= 50; ++v) {
        auto coll_velocity = v;
        auto iterations = 0;
        while (coll_velocity > 0 && iterations < max_iterations) {
            const auto vel_mps = cmps_to_mps(coll_velocity);
            const auto new_velocity = mps_to_cmps(vel_mps * 0.9);
            coll_velocity = (std::abs(new_velocity) >= std::abs(coll_velocity)) ? 0 : new_velocity;
            ++iterations;
        }
        CAPTURE(v);
        CHECK(coll_velocity == 0);
        CHECK(iterations < max_iterations);
    }
}

TEST_CASE("vehicle_collision_with_wall_terminates", "[vehicle]") {
    clear_all_state();
    auto& here = get_map();
    build_test_map(ter_id("t_pavement"));
    clear_vehicles();

    const auto veh_pos = tripoint_bub_ms(60, 60, 0);
    const auto wall_pos = tripoint_bub_ms(60, 59, 0);

    auto* veh_ptr = here.add_vehicle(vproto_id("bicycle_test"), veh_pos, 270_degrees, 0, 0);
    REQUIRE(veh_ptr != nullptr);

    REQUIRE(here.ter_set(wall_pos, ter_id("t_concrete_wall")));
    here.build_map_cache(0, true);

    CAPTURE(here.ter(wall_pos).id().str());
    CAPTURE(here.move_cost_ter_furn(wall_pos));
    REQUIRE(here.impassable_ter_furn(wall_pos));

    veh_ptr->velocity = 222;
    const auto probe = veh_ptr->part_collision(vehicle_part_collision_options{
        .part = 0,
        .pos = wall_pos,
        .just_detect = true,
    });
    REQUIRE(probe.type != veh_coll_nothing);

    veh_ptr->velocity = 222;
    const auto ret = veh_ptr->part_collision(vehicle_part_collision_options{
        .part = 0,
        .pos = wall_pos,
    });

    CHECK(ret.type != veh_coll_nothing);
    CHECK(std::abs(veh_ptr->velocity) < 222);
}

TEST_CASE("vehicle_collision_with_hallucination_terminates", "[vehicle]") {
    clear_all_state();
    auto& here = get_map();
    build_test_map(ter_id("t_pavement"));
    clear_vehicles();

    const auto veh_pos = tripoint_bub_ms(60, 60, 0);
    const auto hallucination_pos = tripoint_bub_ms(60, 59, 0);

    auto* veh_ptr = here.add_vehicle(vproto_id("bicycle_test"), veh_pos, 270_degrees, 0, 0);
    REQUIRE(veh_ptr != nullptr);

    auto& hallucination = spawn_test_monster("mon_chicken", hallucination_pos);
    hallucination.hallucination = true;
    REQUIRE(g->critter_at<monster>(hallucination_pos, true) == &hallucination);

    veh_ptr->velocity = 222;
    const auto ret = veh_ptr->part_collision(vehicle_part_collision_options{
        .part = 0,
        .pos = hallucination_pos,
    });

    CHECK(ret.type == veh_coll_body);
    CHECK(hallucination.is_dead());
    CHECK(veh_ptr->velocity == 222);
}

TEST_CASE("vehicle_collision_hits_occupant_with_stale_in_vehicle_flag", "[vehicle][collision]") {
    clear_all_state();
    auto& here = get_map();
    build_test_map(ter_id("t_pavement"));
    clear_vehicles();

    // Build a vehicle with two distinct boardable seats so the collision
    // target tile belongs to THIS vehicle (is_veh_collision must be false,
    // reaching the body-collision path this fix changes).
    const auto veh_pos = tripoint_bub_ms(60, 60, 0);
    auto* veh = here.add_vehicle(vproto_id("none"), veh_pos, 0_degrees, 0, 0);
    REQUIRE(veh != nullptr);
    REQUIRE(veh->install_part(tripoint_mnt_veh(0, 0, 0), vpart_id("frame_vertical")) >= 0);
    const int seat_a = veh->install_part(tripoint_mnt_veh(0, 0, 0), vpart_id("seat"));
    REQUIRE(seat_a >= 0);
    REQUIRE(veh->install_part(tripoint_mnt_veh(1, 0, 0), vpart_id("frame_vertical")) >= 0);
    const int seat_b = veh->install_part(tripoint_mnt_veh(1, 0, 0), vpart_id("seat"));
    REQUIRE(seat_b >= 0);
    here.add_vehicle_to_cache(veh);

    // Properly board an NPC on seat A.
    const auto seat_a_pos = veh->bub_part_location(seat_a);
    const string_id<npc_template> test_guy("test_talker");
    const character_id model_id = here.place_npc(tripoint_bub_ms(15, 15, 0).xy(), test_guy);
    g->load_npcs();
    npc* seated_rider = g->find_npc(model_id);
    REQUIRE(seated_rider != nullptr);
    seated_rider->setpos(seat_a_pos);
    here.board_vehicle(seat_a_pos, seated_rider);
    REQUIRE(seated_rider->in_vehicle);

    // Stand a second NPC on seat B with a stale in_vehicle flag: standing on
    // this vehicle's own boardable tile, flagged as riding, but never
    // registered as seat B's passenger (passenger_flag/passenger_id unset).
    // This mirrors the real desync class documented at
    // map_vehicle.cpp's "Part/passenger position mismatch" debug detector.
    const auto seat_b_pos = veh->bub_part_location(seat_b);
    const character_id stray_id = here.place_npc(tripoint_bub_ms(16, 15, 0).xy(), test_guy);
    g->load_npcs();
    npc* stray_rider = g->find_npc(stray_id);
    REQUIRE(stray_rider != nullptr);
    stray_rider->setpos(seat_b_pos);
    stray_rider->in_vehicle = true;
    REQUIRE(veh->get_passenger(seat_b) == nullptr);

    veh->velocity = 222;
    const auto ret = veh->part_collision(vehicle_part_collision_options{
        .part = seat_a,
        .pos = seat_b_pos,
    });

    CHECK(ret.type == veh_coll_body);
}

#include "physics/physics_world.h"
#include "physics/terrain_body.h"
#include "vehicle_part.h"

// Vehicle-vehicle collision under Box2D position authority.
//
// Box2D itself cannot deliver this: filter_bits.h gives every vehicle shape
// groupIndex = vehicle_group (-1), and a shared negative groupIndex means those
// shapes never collide, so no VV contact event is ever generated.  It reaches the
// game only because the readback walk in map::vehmove() routes each tile step
// through move_vehicle(), which calls vehicle_vehicle_collision().
TEST_CASE("box2d_authority_vehicle_hits_vehicle", "[vehicle][collision][box2d]") {
    clear_all_state();
    clear_vehicles();
    auto& here = get_map();
    build_test_map(ter_id("t_pavement"));

    auto* mover =
        here.add_vehicle(vproto_id("car_test"), tripoint_bub_ms(60, 60, 0), 0_degrees, 100, 0);
    REQUIRE(mover != nullptr);
    REQUIRE(mover->box2d_position_authority);

    mover->tags.insert("IN_CONTROL_OVERRIDE");
    mover->engine_on = true;
    mover->velocity = 2000;
    mover->cruise_velocity = 2000;

    // Park a target squarely in the mover's path, derived from its heading rather
    // than a guessed tile.
    const auto fv = mover->face_vec();
    const auto start = mover->bub_ms_location();
    const auto target_pos = tripoint_bub_ms(
        start.x() + static_cast<int>(std::lround(fv.x * 8)),
        start.y() + static_cast<int>(std::lround(fv.y * 8)), 0);
    auto* parked = here.add_vehicle(vproto_id("car_test"), target_pos, 0_degrees, 0, 0);
    REQUIRE(parked != nullptr);
    REQUIRE(parked->velocity == 0);

    const int mover_hp_before = mover->part(0).hp();
    const int mover_vel_before = mover->velocity;

    for (int turn = 0; turn < 8; ++turn) { here.vehmove(); }

    // Assert specific collision consequences, not merely "something changed":
    // a position difference alone would also be satisfied by the two vehicles
    // driving straight through each other, which is the failure being tested.
    //
    //  - the struck vehicle must be shoved (gains velocity), and/or
    //  - one of them must take part damage,
    // and the striking vehicle must have lost speed.
    const bool parked_shoved = parked->velocity != 0;
    const bool damage_dealt = mover->part(0).hp() < mover_hp_before;
    const bool mover_slowed = mover->velocity < mover_vel_before;
    CAPTURE(parked_shoved);
    CAPTURE(damage_dealt);
    CAPTURE(mover_slowed);
    CAPTURE(mover->velocity);
    CAPTURE(parked->velocity);
    CHECK((parked_shoved || damage_dealt));
    CHECK(mover_slowed);
}
