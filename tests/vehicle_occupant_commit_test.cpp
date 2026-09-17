#include "avatar.h"
#include "calendar.h"
#include "catch/catch_amalgamated.hpp"
#include "coordinates.h"
#include "creature_tracker.h"
#include "game.h"
#include "map.h"
#include "map_helpers.h"
#include "monster.h"
#include "npc.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "type_id.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vpart_range.h"
#include "vpart_position.h"

#include <array>
#include <vector>

// Stage B gate: vehicle::commit_occupants() is the sole writer of boarded
// occupant position, called once per vehicle per turn from map::vehmove().
// These cases prove convergence against the plan's own gate criteria.

namespace
{


struct occupant_rig {
    vehicle *veh = nullptr;
    int front_seat = -1;
    int rear_seat = -1;
    int pet_seat = -1;
};

// Build a stationary "car" with three distinct boardable seats, spawned
// fueled (init_veh_fuel=1) so cruise control can sustain velocity across
// multiple turns -- matching tests/vehicle_ramp_test.cpp's convention.
occupant_rig make_three_seat_rig( const tripoint_bub_ms &origin )
{
    occupant_rig rig;
    rig.veh = get_map().add_vehicle( vproto_id( "car" ), origin, 0_degrees, 1, 0 );
    REQUIRE( rig.veh != nullptr );
    rig.veh->check_falling_or_floating();
    rig.veh->box2d_position_authority = false;
    rig.veh->tags.insert( "IN_CONTROL_OVERRIDE" );
    rig.veh->engine_on = true;

    std::vector<int> seats;
    for( const vpart_reference &vp : rig.veh->get_all_parts() ) {
        if( vp.part().is_seat() ) {
            seats.push_back( static_cast<int>( vp.part_index() ) );
        }
    }
    REQUIRE( seats.size() >= 3 );
    rig.front_seat = seats[0];
    rig.rear_seat = seats[1];
    rig.pet_seat = seats[2];
    return rig;
}

} // namespace

TEST_CASE( "commit_occupants_places_avatar_npc_and_pet_once_per_turn", "[vehicle][occupant]" )
{
    clear_all_state();
    build_test_map( ter_id( "t_pavement" ) );
    map &here = get_map();
    avatar &player_character = get_avatar();

    const tripoint_bub_ms origin( 60, 60, 0 );
    occupant_rig rig = make_three_seat_rig( origin );

    const tripoint_bub_ms front_pos = rig.veh->bub_part_location( rig.front_seat );
    player_character.setpos( front_pos );
    here.board_vehicle( front_pos, &player_character );
    REQUIRE( player_character.in_vehicle );

    const tripoint_bub_ms rear_pos = rig.veh->bub_part_location( rig.rear_seat );
    // Spawn away from the seat tile: place_npc()+load_npcs() auto-boards an
    // npc that spawns directly on a boardable vehicle part, which would then
    // make the explicit board_vehicle() below double-board the same seat.
    npc &test_npc = spawn_npc( origin + tripoint_rel_ms( 0, 5, 0 ), "test_talker" );
    test_npc.setpos( rear_pos );
    here.board_vehicle( rear_pos, &test_npc );
    REQUIRE( test_npc.in_vehicle );

    const tripoint_bub_ms pet_pos = rig.veh->bub_part_location( rig.pet_seat );
    monster &pet = spawn_test_monster( "mon_dog", pet_pos );
    pet.make_friendly();
    rig.veh->part( rig.pet_seat ).animal_ref = g->shared_from( pet );
    pet.boarded_vehicle = rig.veh->handle();
    pet.boarded_part = rig.pet_seat;
    REQUIRE( rig.veh->get_pet( rig.pet_seat ) == &pet );

    rig.veh->cruise_velocity = 179;
    rig.veh->velocity = 179;

    for( int turn = 0; turn < 10; ++turn ) {
        CAPTURE( turn );

        const unsigned pc_writes_before = player_character.position_writes;
        const unsigned npc_writes_before = test_npc.position_writes;
        const unsigned pet_writes_before = pet.position_writes;

        here.vehmove();
        REQUIRE( rig.veh->velocity > 0 );

        const tripoint_bub_ms new_front = rig.veh->bub_part_location( rig.front_seat );
        const tripoint_bub_ms new_rear = rig.veh->bub_part_location( rig.rear_seat );
        const tripoint_bub_ms new_pet = rig.veh->bub_part_location( rig.pet_seat );

        CHECK( player_character.bub_pos() == new_front );
        CHECK( test_npc.bub_pos() == new_rear );
        CHECK( pet.bub_pos() == new_pet );
        CHECK( g->critter_at( new_front ) == static_cast<Creature *>( &player_character ) );
        CHECK( g->critter_at( new_rear ) == static_cast<Creature *>( &test_npc ) );
        CHECK( g->critter_at( new_pet ) == static_cast<Creature *>( &pet ) );

        // Exactly one setpos() per occupant per turn the vehicle actually
        // moved -- not one per crossed tile (map::displace_vehicle's old
        // per-tile rider retry loop).
        CHECK( player_character.position_writes - pc_writes_before == 1 );
        CHECK( test_npc.position_writes - npc_writes_before == 1 );
        CHECK( pet.position_writes - pet_writes_before == 1 );
    }
}

TEST_CASE( "commit_occupants_spares_pet_from_collision_with_its_own_vehicle", "[vehicle][occupant][collision]" )
{
    clear_all_state();
    build_test_map( ter_id( "t_pavement" ) );
    map &here = get_map();

    const tripoint_bub_ms origin( 60, 60, 0 );
    occupant_rig rig = make_three_seat_rig( origin );

    const tripoint_bub_ms pet_pos = rig.veh->bub_part_location( rig.pet_seat );
    monster &pet = spawn_test_monster( "mon_dog", pet_pos );
    pet.make_friendly();
    rig.veh->part( rig.pet_seat ).animal_ref = g->shared_from( pet );
    pet.boarded_vehicle = rig.veh->handle();
    pet.boarded_part = rig.pet_seat;

    // An unrelated hostile monster stands well clear of the vehicle's own
    // footprint but directly in its path of travel (0_degrees faces +x, per
    // the convention tests/ranged_vehicle_recoil_test.cpp already relies on).
    const tripoint_bub_ms stranger_pos = origin + tripoint_rel_ms( 8, 0, 0 );
    monster &stranger = spawn_test_monster( "mon_zombie", stranger_pos );
    const int stranger_hp_before = stranger.get_hp();

    rig.veh->cruise_velocity = 1200;
    rig.veh->velocity = 1200;
    here.vehmove();

    // The pet, riding a boardable part of this same vehicle, must never be
    // treated as a collision target of its own vehicle.
    CHECK( pet.get_hp() == pet.get_hp_max() );

    // An unrelated monster in the vehicle's path is a real collision target
    // and is not spared by the pet-identity exclusion.
    CHECK( stranger.get_hp() <= stranger_hp_before );
}

namespace
{

// Minimal "drive up a ramp" terrain: the up=true case of
// tests/vehicle_ramp_test.cpp's set_ramp(), inlined without the up/down and
// use_ramp branches this test never exercises. x < transit_x is the elevated
// z=1 landing (rock below); x >= transit_x+2 is the flat z=0 approach (open
// air above); the two ramp tiles at transit_x/transit_x+1 bridge them.
void set_ramp_up( const int transit_x )
{
    map &here = get_map();
    build_test_map( ter_id( "t_pavement" ) );
    for( int y = 0; y < SEEY * MAPSIZE; y++ ) {
        for( int x = 0; x < transit_x; x++ ) {
            here.ter_set( tripoint_bub_ms( x, y, -1 ), ter_id( "t_rock" ) );
            here.ter_set( tripoint_bub_ms( x, y, 0 ), ter_id( "t_rock" ) );
            here.ter_set( tripoint_bub_ms( x, y, 1 ), ter_id( "t_pavement" ) );
            here.ter_set( tripoint_bub_ms( x, y, 2 ), ter_id( "t_open_air" ) );
            here.ter_set( tripoint_bub_ms( x, y, 3 ), ter_id( "t_open_air" ) );
        }
        here.ter_set( tripoint_bub_ms( transit_x + 1, y, 0 ), ter_id( "t_ramp_up_low" ) );
        here.ter_set( tripoint_bub_ms( transit_x, y, 0 ), ter_id( "t_ramp_up_high" ) );
        here.ter_set( tripoint_bub_ms( transit_x + 1, y, 1 ), ter_id( "t_ramp_down_low" ) );
        here.ter_set( tripoint_bub_ms( transit_x, y, 1 ), ter_id( "t_ramp_down_high" ) );
        for( int x = transit_x + 2; x < SEEX * MAPSIZE; x++ ) {
            here.ter_set( tripoint_bub_ms( x, y, 1 ), ter_id( "t_open_air" ) );
            here.ter_set( tripoint_bub_ms( x, y, 0 ), ter_id( "t_pavement" ) );
            here.ter_set( tripoint_bub_ms( x, y, -1 ), ter_id( "t_rock" ) );
        }
    }
    for( const auto z : std::array<int, 3> { -1, 0, 1 } ) {
        here.invalidate_map_cache( z );
        here.build_map_cache( z, true );
    }
}

} // namespace

TEST_CASE( "commit_occupants_follows_vehicle_across_a_ramp_z_transition", "[vehicle][occupant][ramp]" )
{
    clear_all_state();
    const int transit_x = 60;
    set_ramp_up( transit_x );
    map &here = get_map();

    const tripoint_bub_ms map_starting_point( transit_x + 4, 60, 0 );
    REQUIRE( here.ter( map_starting_point ) == ter_id( "t_pavement" ) );
    vehicle *veh_ptr = here.add_vehicle( vproto_id( "motorcycle" ), map_starting_point, 180_degrees, 1,
                                        0 );
    REQUIRE( veh_ptr != nullptr );
    vehicle &veh = *veh_ptr;
    veh.check_falling_or_floating();
    // Ramp tests require tile-step movement (move_vehicle), not Box2D physics --
    // same opt-out tests/vehicle_ramp_test.cpp uses, left alone until stage D.
    veh.box2d_position_authority = false;
    veh.tags.insert( "IN_CONTROL_OVERRIDE" );
    veh.engine_on = true;

    avatar &player_character = get_avatar();
    player_character.setpos( map_starting_point );
    here.board_vehicle( map_starting_point, &player_character );
    REQUIRE( player_character.in_vehicle );
    const int seat_part = player_character.boarded_part;
    REQUIRE( seat_part >= 0 );

    veh.cruise_velocity = 0;
    veh.velocity = 0;
    here.vehmove();
    veh.cruise_velocity = 179;
    veh.velocity = 179;
    REQUIRE( veh.safe_velocity() > 0 );

    int cycles = 0;
    while( veh.engine_on && veh.safe_velocity() > 0 && cycles < 10 ) {
        CAPTURE( cycles );
        const tripoint_bub_ms before_seat = veh.bub_part_location( seat_part );
        const tripoint_bub_ms before_player = player_character.bub_pos();
        here.vehmove();
        REQUIRE( !veh.skidding );
        const tripoint_bub_ms after_seat = veh.bub_part_location( seat_part );
        // The occupant's tile must track the vehicle's own seat exactly, on
        // every cycle including the one that crosses the ramp -- not just at
        // the end, and not via any setpos() outside commit_occupants (which
        // check_position_write_owner() would reject with a debugmsg).
        CAPTURE( before_seat );
        CAPTURE( after_seat );
        CAPTURE( before_player );
        CHECK( player_character.bub_pos() == after_seat );
        cycles++;
    }

    CHECK( veh.bub_ms_location().z() == 1 );
    CHECK( player_character.bub_pos().z() == 1 );
    CHECK( player_character.in_vehicle );
    CHECK( player_character.boarded_part == seat_part );
}

TEST_CASE( "thrown_occupant_leaves_vehicle_with_no_owning_part", "[vehicle][occupant][throw]" )
{
    clear_all_state();
    build_test_map( ter_id( "t_pavement" ) );
    map &here = get_map();

    const tripoint_bub_ms origin( 60, 60, 0 );
    occupant_rig rig = make_three_seat_rig( origin );

    get_avatar().setpos( origin );
    const tripoint_bub_ms rear_pos = rig.veh->bub_part_location( rig.rear_seat );
    npc &test_npc = spawn_npc( origin + tripoint_rel_ms( 0, 5, 0 ), "test_talker" );
    test_npc.setpos( rear_pos );
    here.board_vehicle( rear_pos, &test_npc );
    REQUIRE( test_npc.in_vehicle );
    REQUIRE( test_npc.boarded_part == rig.rear_seat );

    // "car"'s seats all ship with an installed seatbelt part, which
    // shake_vehicle() treats as unconditional throw protection regardless of
    // impact severity -- strip it so a hard-enough impact can actually throw
    // this rider, matching an unbelted seat in the field.
    const int seatbelt_part = rig.veh->part_with_feature( rig.rear_seat, VPFLAG_SEATBELT, true );
    REQUIRE( seatbelt_part >= 0 );
    // remove_part()'s return is shift_if_needed()'s origin-shift signal, not
    // a success flag -- verify removal by re-querying the feature instead.
    rig.veh->remove_part( seatbelt_part );
    REQUIRE( rig.veh->part_with_feature( rig.rear_seat, VPFLAG_SEATBELT, true ) == -1 );

    // Exercise the real production throw path (map::shake_vehicle(), called
    // from map::vehmove() after any collision with nonzero impulse) rather
    // than the unboard_vehicle() primitive it uses internally: stage a huge
    // velocity drop so throw_from_seat's roll succeeds regardless of RNG
    // (d_vel*80 must exceed str_cur*150+500 at the worst-case rng(80,120)
    // roll -- 3000 cm/s to 0 clears that with a wide margin for any str_cur).
    rig.veh->velocity = 0;
    here.shake_vehicle( *rig.veh, /*velocity_before=*/3000, 0_degrees );

    CHECK_FALSE( test_npc.in_vehicle );
    CHECK( test_npc.boarded_part == -1 );
    // "a real position": the position field is a live, self-consistent
    // report of where the game actually placed them -- not stale data tied
    // to a since-cleared seat. The seat's own passenger_flag must agree
    // that it is unowned; a fully enclosed sedan can box a thrown rider
    // back onto the same tile without that tile being a lingering vehicle
    // claim on them (checked separately via boarded_part above).
    CHECK_FALSE( rig.veh->part( rig.rear_seat ).has_flag( vehicle_part::passenger_flag ) );
}
