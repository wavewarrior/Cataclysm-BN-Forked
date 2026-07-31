#include "catch/catch_amalgamated.hpp"
#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "avatar.h"
#include "cata_utility.h"
#include "coordinates.h"
#include "damage.h"
#include "debug.h"
#include "enums.h"
#include "game.h"
#include "item.h"
#include "json.h"
#include "map.h"
#include "map_helpers.h"
#include "mapbuffer.h"
#include "mongroup.h"
#include "monster.h"
#include "overmapbuffer.h"
#include "state_helpers.h"
#include "type_id.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vehicle_wait.h"
#include "vpart_position.h"
#include "veh_type.h"

namespace
{

const auto horde_spawn_test_group = mongroup_id( "GROUP_ZOMBIE" );
const auto horde_spawn_test_monster = mtype_id( "mon_zombie" );

struct horde_vehicle_spawn_options {
    bool owned = false;
    bool tracked = false;
};

struct horde_vehicle_spawn_fixture {
    std::set<tripoint_abs_ms> vehicle_points;
    mongroup *horde = nullptr;
};

auto point_has_monster( const tripoint_abs_ms &p ) -> bool
{
    const auto &here = get_map();
    return g->critter_at<monster>( here.abs_to_bub( p ) ) != nullptr;
}

auto vehicle_points_contain_monster( const std::set<tripoint_abs_ms> &vehicle_points ) -> bool
{
    return std::ranges::any_of( vehicle_points, point_has_monster );
}

auto make_horde_vehicle_spawn_fixture(
    const horde_vehicle_spawn_options &options ) -> horde_vehicle_spawn_fixture
{
    clear_all_state();
    ACTIVE_OVERMAP_BUFFER.clear();

    auto &here = get_map();
    auto &you = get_avatar();
    const auto target_submap = tripoint_bub_sm( here.getmapsize() / 2, here.getmapsize() / 2, 0 );
    const auto target_submap_abs = here.bub_to_abs( target_submap );
    const auto target_submap_origin = project_to<coords::ms>( target_submap );
    const auto target_submap_end = target_submap_origin + tripoint( SEEX - 1, SEEY - 1, 0 );
    const auto vehicle_origin = target_submap_origin + tripoint( SEEX / 2, SEEY / 2, 0 );

    you.setpos( vehicle_origin + tripoint( 0, 0, -2 ) );
    const auto veh = here.add_vehicle( vproto_id( "car" ), vehicle_origin, 0_degrees, 0, 0 );
    REQUIRE( veh != nullptr );

    auto group = mongroup( horde_spawn_test_group, target_submap_abs, 1, 0 );
    group.horde = true;
    group.interest = 10;
    group.monsters.emplace_back( horde_spawn_test_monster );

    ACTIVE_OVERMAP_BUFFER.get( project_remain<coords::om>( target_submap_abs ).quotient );
    auto target_groups = ACTIVE_OVERMAP_BUFFER.groups_at( target_submap_abs );
    std::ranges::for_each( target_groups, []( mongroup * const target_group ) {
        target_group->clear();
    } );
    ACTIVE_OVERMAP_BUFFER.discard_monster_map( target_submap_abs );

    const auto horde = ACTIVE_OVERMAP_BUFFER.create_horde( group );
    REQUIRE( horde != nullptr );

    if( options.owned ) {
    veh->set_owner( you );
    }
    if( options.tracked ) {
    veh->toggle_tracking();
    }

    const auto vehicle_points = veh->get_points( true );
    const auto horde_spawn_blocking_terrain = ter_id( "t_wall" );
    std::ranges::for_each( here.points_in_rectangle( target_submap_origin, target_submap_end ),
    [&]( const auto & p ) {
        if( !vehicle_points.contains( here.bub_to_abs( p ) ) ) {
            here.ter_set( p, horde_spawn_blocking_terrain );
        }
    } );
    here.invalidate_map_cache( target_submap.z() );
    here.build_map_cache( target_submap.z(), true );

    return horde_vehicle_spawn_fixture{ .vehicle_points = vehicle_points, .horde = horde };
}

auto vehicle_with_legacy_pivot_json() -> std::string
{
    return R"json(
           {
           "type": "none",
           "posx": 5,
           "posy": 6,
           "om_id": 0,
           "faceDir": 180,
           "moveDir": 180,
           "turn_dir": 180,
           "velocity": 0,
           "falling": false,
           "floating": false,
           "flying": false,
           "cruise_velocity": 0,
           "vertical_velocity": 0,
           "name": "legacy pivot test vehicle",
           "owner": "",
           "old_owner": "",
           "parts": [
           {
           "id": "frame_horizontal",
           "mount_dx": 0,
           "mount_dy": 0,
           "open": false,
           "direction": 0,
           "blood": 0,
           "proxy_part_id": "null",
           "proxy_sym": 0,
           "enabled": false,
           "flags": 0,
           "passenger_id": -1,
           "crew_id": -1,
           "items": [],
           "ammo_pref": "null",
           "part_color": [ 0, 0, 0, 0 ]
       }
           ],
           "pivot": [ -1, 0 ],
           "zones": []
       }
           )json";
}

auto vehicle_with_invalid_part_and_legacy_pivot_json() -> std::string
{
    return R"json(
           {
           "type": "none",
           "posx": 5,
           "posy": 6,
           "om_id": 0,
           "faceDir": 180,
           "moveDir": 180,
           "turn_dir": 180,
           "velocity": 0,
           "falling": false,
           "floating": false,
           "flying": false,
           "cruise_velocity": 0,
           "vertical_velocity": 0,
           "name": "legacy pivot invalid part test vehicle",
           "owner": "",
           "old_owner": "",
           "parts": [
           {
           "id": "missing_saved_vehicle_part",
           "mount_dx": 99,
           "mount_dy": 99,
           "open": false,
           "direction": 0,
           "blood": 0,
           "proxy_part_id": "null",
           "proxy_sym": 0,
           "enabled": false,
           "flags": 0,
           "passenger_id": -1,
           "crew_id": -1,
           "items": [],
           "ammo_pref": "null",
           "part_color": [ 0, 0, 0, 0 ]
       },
           {
           "id": "frame_horizontal",
           "mount_dx": 0,
           "mount_dy": 0,
           "open": false,
           "direction": 0,
           "blood": 0,
           "proxy_part_id": "null",
           "proxy_sym": 0,
           "enabled": false,
           "flags": 0,
           "passenger_id": -1,
           "crew_id": -1,
           "items": [],
           "ammo_pref": "null",
           "part_color": [ 0, 0, 0, 0 ]
       }
           ],
           "pivot": [ -1, 0 ],
           "zones": []
       }
           )json";
}

} // namespace

TEST_CASE( "vehicle deserialize accepts legacy two coordinate pivot", "[vehicle][save]" )
{
    auto json = std::istringstream( vehicle_with_legacy_pivot_json() );
    auto jsin = JsonIn( json );
    auto veh = vehicle();

    REQUIRE( jsin.read( veh, true ) );
    CHECK( veh.mount_to_abs( tripoint_mnt_veh( -1, 0, 0 ) ) == tripoint_abs_ms( 5, 6, 0 ) );
    CHECK( veh.mount_to_abs( tripoint_mnt_veh( 0, 0, 0 ) ) == tripoint_abs_ms( 4, 6, 0 ) );
}

TEST_CASE( "vehicle deserialize keeps valid saved parts after an invalid part", "[vehicle][save]" )
{
    auto json = std::istringstream( vehicle_with_invalid_part_and_legacy_pivot_json() );
    auto jsin = JsonIn( json );
    auto veh = vehicle();
    auto loaded = false;

    const auto debug_msg = capture_debugmsg_during( [&]() {
        loaded = jsin.read( veh, true );
    } );

    REQUIRE( loaded );
    CHECK( debug_msg.find( "Skipping invalid saved vehicle part" ) != std::string::npos );
    CHECK( debug_msg.find( "missing_saved_vehicle_part" ) != std::string::npos );
    CHECK( veh.part_count() == 1 );
    CHECK( veh.mount_to_abs( tripoint_mnt_veh( 0, 0, 0 ) ) == tripoint_abs_ms( 4, 6, 0 ) );
}

TEST_CASE( "detaching_vehicle_unboards_passengers" )
{
    clear_all_state();
    const tripoint_bub_ms test_origin( 60, 60, 0 );
    const auto vehicle_origin = test_origin;
    avatar &player_character = get_avatar();
    map &here = get_map();
    vehicle *veh_ptr = here.add_vehicle( vproto_id( "bicycle" ), vehicle_origin, -90_degrees, 0, 0 );
    here.board_vehicle( test_origin, &player_character );
    REQUIRE( player_character.in_vehicle );
    here.detach_vehicle( veh_ptr );
    REQUIRE( !player_character.in_vehicle );
}

TEST_CASE( "destroy_grabbed_vehicle_section" )
{
    clear_all_state();
    GIVEN( "A vehicle grabbed by the player" ) {
        map &here = get_map();
        const tripoint_bub_ms test_origin( 60, 60, 0 );
        avatar &player_character = get_avatar();
        player_character.setpos( test_origin );
        const auto vehicle_origin = test_origin + tripoint_south_east;
        vehicle *veh_ptr = here.add_vehicle( vproto_id( "bicycle" ), vehicle_origin, -90_degrees, 0, 0 );
        REQUIRE( veh_ptr != nullptr );
        tripoint_bub_ms grab_point = test_origin + tripoint_rel_ms::east();
        player_character.grab( OBJECT_VEHICLE, tripoint_rel_ms::east() );
        REQUIRE( player_character.get_grab_type() != OBJECT_NONE );
        REQUIRE( player_character.grab_point == tripoint_rel_ms::east() );
        WHEN( "The vehicle section grabbed by the player is destroyed" ) {
            here.destroy( grab_point );
            REQUIRE( veh_ptr->get_parts_at( grab_point, "", part_status_flag::available ).empty() );
            THEN( "The player's grab is released" ) {
                CHECK( player_character.get_grab_type() == OBJECT_NONE );
                CHECK( player_character.grab_point == tripoint_rel_ms::zero() );
            }
        }
    }
}

TEST_CASE( "taking_control_of_vehicle_without_engine", "[vehicle]" )
{
    clear_all_state();
    const auto origin = tripoint_bub_ms( 60, 60, 0 );
    auto &player_character = get_avatar();
    player_character.setpos( origin );

    auto *veh_ptr = get_map().add_vehicle( vproto_id( "shopping_cart" ), origin, 0_degrees, 0, 0 );
    REQUIRE( veh_ptr != nullptr );
    REQUIRE_FALSE( player_character.controlling_vehicle );
    REQUIRE_FALSE( veh_ptr->engine_on );

    veh_ptr->start_engines( true );

    CHECK( player_character.controlling_vehicle );
    CHECK_FALSE( veh_ptr->engine_on );
    CHECK( !player_character.activity );
}

TEST_CASE( "moving_flying_vehicle_can_use_wait_menu", "[vehicle][wait]" )
{
    clear_all_state();
    const auto origin = tripoint_bub_ms( 60, 60, 0 );

    auto *veh_ptr = get_map().add_vehicle( vproto_id( "plane_small" ), origin, 0_degrees, 0, 0 );
    REQUIRE( veh_ptr != nullptr );

    veh_ptr->velocity = 100;
    CHECK( vehicle_wait::is_wait_blocked_by_movement( *veh_ptr ) );
    CHECK_FALSE( vehicle_wait::should_offer_flying_wait_durations( *veh_ptr ) );

    veh_ptr->set_flying( true );
    CHECK_FALSE( vehicle_wait::is_wait_blocked_by_movement( *veh_ptr ) );
    CHECK( vehicle_wait::should_offer_flying_wait_durations( *veh_ptr ) );
}

TEST_CASE( "horde_spawns_skip_owned_vehicle_tiles", "[horde][vehicle][monster]" )
{
    const auto cleanup = on_out_of_scope( [] {
        clear_all_state();
        ACTIVE_OVERMAP_BUFFER.clear();
    } );

    SECTION( "unowned and untracked vehicle tiles remain valid horde spawn locations" ) {
        const auto fixture = make_horde_vehicle_spawn_fixture( horde_vehicle_spawn_options{} );

        get_map().spawn_monsters( true );

        CHECK( vehicle_points_contain_monster( fixture.vehicle_points ) );
        CHECK( fixture.horde->empty() );
    }

    SECTION( "tracked but unowned vehicle tiles remain valid horde spawn locations" ) {
        const auto fixture = make_horde_vehicle_spawn_fixture( horde_vehicle_spawn_options{ .tracked = true } );

        get_map().spawn_monsters( true );

        CHECK( vehicle_points_contain_monster( fixture.vehicle_points ) );
        CHECK( fixture.horde->empty() );
    }

    SECTION( "owned but untracked vehicle tiles are excluded from horde spawn locations" ) {
        const auto fixture = make_horde_vehicle_spawn_fixture( horde_vehicle_spawn_options{ .owned = true } );

        get_map().spawn_monsters( true );

        CHECK_FALSE( vehicle_points_contain_monster( fixture.vehicle_points ) );
        CHECK_FALSE( fixture.horde->empty() );
    }

    SECTION( "owned and tracked vehicle tiles are excluded from horde spawn locations" ) {
        const auto fixture = make_horde_vehicle_spawn_fixture( horde_vehicle_spawn_options{ .owned = true,
                             .tracked = true } );

        get_map().spawn_monsters( true );

        CHECK_FALSE( vehicle_points_contain_monster( fixture.vehicle_points ) );
        CHECK_FALSE( fixture.horde->empty() );
    }
}

TEST_CASE( "add_item_to_broken_vehicle_part" )
{
    clear_all_state();
    const tripoint_bub_ms test_origin( 60, 60, 0 );
    const auto vehicle_origin = test_origin;
    vehicle *veh_ptr = get_map().add_vehicle( vproto_id( "bicycle" ), vehicle_origin, 0_degrees, 0, 0 );
    REQUIRE( veh_ptr != nullptr );

    const tripoint_bub_ms pos = vehicle_origin + tripoint_rel_ms::west();
    auto cargo_parts = veh_ptr->get_parts_at( pos, "CARGO", part_status_flag::any );
    REQUIRE( !cargo_parts.empty( ) );
    vehicle_part *cargo_part = cargo_parts.front();
    REQUIRE( cargo_part != nullptr );
    //Must not be broken yet
    REQUIRE( !cargo_part->is_broken() );
    //For some reason (0 - cargo_part->hp()) is just not enough to destroy a part
    REQUIRE( veh_ptr->mod_hp( *cargo_part, -( 1 + cargo_part->hp() ), DT_BASH ) );
    //Now it must be broken
    REQUIRE( cargo_part->is_broken() );
    //Now part is really broken, adding an item should fail
    detached_ptr<item> itm2 = item::spawn( "jeans" );
    itm2 = veh_ptr->add_item( *cargo_part, std::move( itm2 ) );
    CHECK( itm2 );
}

TEST_CASE( "damage_vehicle_oob" )
{
    clear_all_state();
    const tripoint_bub_ms test_origin( 60, 60, 0 );
    g->place_player( test_origin );
    const tripoint_bub_ms vehicle_origin( SEEX, 0, 0 );
    vehicle *veh_ptr = get_map().add_vehicle( vproto_id( "bicycle" ), vehicle_origin, 0_degrees, 0, 0 );
    REQUIRE( veh_ptr != nullptr );

    //Put an item in the vehicle
    const tripoint_bub_ms cargo_pos = vehicle_origin + tripoint_rel_ms::west();
    auto cargo_parts = veh_ptr->get_parts_at( cargo_pos, "CARGO", part_status_flag::any );
    REQUIRE( !cargo_parts.empty( ) );
    vehicle_part *cargo_part = cargo_parts.front();
    REQUIRE( cargo_part != nullptr );
    REQUIRE( !veh_ptr->add_item( *cargo_part, item::spawn( "jeans" ) ) );

    //Shift the vehicle half off the map
    g->place_player( test_origin + tripoint_east * SEEX );

    //Check the vehicle is still there.
    optional_vpart_position part_pos = get_map().veh_at( tripoint_bub_ms::zero() );
    REQUIRE( part_pos );

    // TODO: vehicle is at origin so tripoint_west == bubble pos; use parts_at_relative( point(-1,0), true ) directly
    auto parts = veh_ptr->parts_at_relative( veh_ptr->bubble_to_mount( tripoint_bub_ms(
                     tripoint_west ) ), true );
    REQUIRE( !parts.empty( ) );
    for( int part : parts ) {
        //We aren't actually smashing each chosen part in turn here
        //it's picking a random one each time, hence why we smash them all
        veh_ptr->damage( part, 10000 );
    }
}

static void check_wreckage( int zlevel )
{
    const tripoint_bub_ms test_origin( 60, 60, zlevel );
    const auto vehicle_origin = test_origin;

    vehicle *veh_ptr = get_map().add_vehicle( vproto_id( "bicycle" ), vehicle_origin, 0_degrees, 0, 0 );
    REQUIRE( veh_ptr != nullptr );

    vehicle *veh_ptr2 = get_map().add_vehicle( vproto_id( "car" ), vehicle_origin + tripoint_north_west,
                        0_degrees, 0, 0 );
    REQUIRE( veh_ptr2 != nullptr );

    INFO( veh_ptr2->name );
    CHECK( veh_ptr2->name == "Wreckage" );
}

TEST_CASE( "overlapping_vehicles_make_wreck" )
{
    clear_all_state();
    check_wreckage( 0 );
    check_wreckage( OVERMAP_HEIGHT );
    check_wreckage( -OVERMAP_DEPTH );
}

static void test_coord_translate( units::angle dir, const tripoint_mnt_veh &pivot,
                                  const tripoint_mnt_veh &p,
                                  tripoint_rel_ms &q )
{
    tileray tdir( dir );
    tdir.advance( p.x() - pivot.x() );
    q.x() = tdir.dx() + tdir.ortho_dx( p.y() - pivot.y() );
    q.y() = tdir.dy() + tdir.ortho_dy( p.y() - pivot.y() );
}

TEST_CASE( "check_vehicle_rotation_against_old", "[.]" )
{
    clear_all_state();
    const tripoint_bub_ms test_origin( 60, 60, 0 );
    const auto vehicle_origin = test_origin;
    vehicle *veh_ptr = get_map().add_vehicle( vproto_id( "bicycle" ), vehicle_origin, 0_degrees, 0, 0 );
    const tripoint_mnt_veh pivot;

    for( int dir = 0; dir < 24; dir++ ) {
        for( int x = -5; x <= 5; x++ ) {
            for( int y = -5; y <= 5; y++ ) {
                tripoint_mnt_veh p = {x, y, 0};
                point_rel_ms oldRes;
                veh_ptr->coord_translate( 15_degrees * dir, pivot, p, oldRes );

                tripoint_rel_ms newRes;
                test_coord_translate( 15_degrees * dir, pivot, p, newRes );

                CHECK( oldRes.x() == newRes.x() );
                CHECK( oldRes.y() == newRes.y() );

            }
        }
    }
}

TEST_CASE( "vehicle_rotation_reverse" )
{
    clear_all_state();
    const tripoint_bub_ms test_origin( 60, 60, 0 );
    const auto vehicle_origin = test_origin;
    vehicle *veh_ptr = get_map().add_vehicle( vproto_id( "bicycle" ), vehicle_origin, 0_degrees, 0, 0 );
    const tripoint_mnt_veh pivot;

    for( int dir = 0; dir < 24; dir++ ) {
        for( int x = -5; x <= 5; x++ ) {
            for( int y = -5; y <= 5; y++ ) {
                tripoint_mnt_veh p = {x, y, 0};
                point_rel_ms result;
                veh_ptr->coord_translate( 15_degrees * dir, pivot, p, result );

                tripoint_mnt_veh reversed;
                veh_ptr->coord_translate_reverse( 15_degrees * dir, pivot, tripoint_rel_ms( result, 0 ), reversed );

                CHECK( reversed.x() == p.x() );
                CHECK( reversed.y() == p.y() );

            }
        }
    }
}

TEST_CASE( "broken_door_and_lock_can_be_removed", "[vehicle]" )
{
    clear_all_state();
    const auto origin = tripoint_bub_ms( 60, 60, 0 );
    auto *veh_ptr = get_map().add_vehicle( vproto_id( "cross_split_test" ), origin, 0_degrees, 0, 0 );
    REQUIRE( veh_ptr != nullptr );

    const auto door_mount = tripoint_mnt_veh( 1, 0, 0 );
    const auto door_idx = veh_ptr->part_with_feature( door_mount, "OPENABLE", true );
    const auto lock_idx = veh_ptr->part_with_feature( door_mount, "DOOR_LOCKING", true );
    REQUIRE( door_idx >= 0 );
    REQUIRE( lock_idx >= 0 );

    auto &door_part = veh_ptr->part( door_idx );
    auto &lock_part = veh_ptr->part( lock_idx );
    // DOORS CAN SPAWN OPEN GUYS
    if( door_part.open ) {
        door_part.open = false;
    }
    REQUIRE_FALSE( door_part.open );

    REQUIRE( veh_ptr->mod_hp( door_part, -( door_part.hp() + 1 ), DT_BASH ) );
    REQUIRE( veh_ptr->mod_hp( lock_part, -( lock_part.hp() + 1 ), DT_BASH ) );
    REQUIRE( door_part.is_broken() );
    REQUIRE( lock_part.is_broken() );

    auto door_reason = std::string{};
    auto lock_reason = std::string{};
    CHECK( veh_ptr->can_unmount( door_idx, door_reason ) );
    CHECK( veh_ptr->can_unmount( lock_idx, lock_reason ) );
}

#include "physics/physics_world.h"
#include "physics/terrain_body.h"

// Box2D position authority is revoked when a vehicle's home submap leaves the
// simulated set while staying resident, and re-granted when it returns.  The
// discrimination that matters is that a vehicle which opted out *itself* must
// never be re-granted: the flag alone cannot distinguish the two cases, so the
// physics world records which revocations were its own.
TEST_CASE( "box2d_position_authority_survives_resident_submap_unload", "[vehicle][box2d]" )
{
    clear_all_state();
    auto &here = get_map();
    auto *pw = here.get_physics_world();
    REQUIRE( pw != nullptr );

    build_test_map( ter_id( "t_pavement" ) );
    auto *veh = here.add_vehicle( vproto_id( "car_test" ), tripoint_bub_ms( 60, 60, 0 ),
                                  0_degrees, 100, 0 );
    REQUIRE( veh != nullptr );
    REQUIRE( veh->box2d_position_authority );

    const auto home = veh->abs_sm_pos;

    SECTION( "revoked while resident, then re-granted on return" ) {
        pw->on_submap_unloaded( home, /*submap_still_resident=*/true );
        CHECK_FALSE( veh->box2d_position_authority );

        // Move the tile anchor while the tile-step mover owns the vehicle, so a
        // stale physics_pos would be detectable after the re-grant.
        REQUIRE( here.displace_vehicle( *veh, tripoint_rel_ms( 2, 0, 0 ) ) );
        const auto anchor = veh->bub_ms_location();

        pw->on_submap_loaded( here, veh->abs_sm_pos );
        CHECK( veh->box2d_position_authority );
        // physics_pos must be reseated to the anchor, or the first readback would
        // walk the vehicle back to where it sat when authority was revoked.
        CHECK( veh->physics_pos.x == Catch::Approx( static_cast<float>( anchor.x() ) ) );
        CHECK( veh->physics_pos.y == Catch::Approx( static_cast<float>( anchor.y() ) ) );
    }

    SECTION( "a vehicle that opted out itself is never re-granted" ) {
        veh->box2d_position_authority = false;
        pw->on_submap_unloaded( home, /*submap_still_resident=*/true );
        pw->on_submap_loaded( here, home );
        CHECK_FALSE( veh->box2d_position_authority );
    }
}

// A vehicle under Box2D position authority skips part_collision() via
// act_on_map()'s early return, so the per-tile readback walk in map::vehmove()
// routes each step through move_vehicle() to get collision consequences back.
// This asserts the end result: drive into a bashable wall, the wall gives way.
TEST_CASE( "box2d_authority_vehicle_bashes_terrain", "[vehicle][box2d]" )
{
    clear_all_state();
    auto &here = get_map();
    build_test_map( ter_id( "t_pavement" ) );

    auto *veh = here.add_vehicle( vproto_id( "car_test" ), tripoint_bub_ms( 60, 60, 0 ),
                                  0_degrees, 100, 0 );
    REQUIRE( veh != nullptr );
    REQUIRE( veh->box2d_position_authority );

    veh->tags.insert( "IN_CONTROL_OVERRIDE" );
    veh->engine_on = true;
    veh->velocity = 2000;          // 20 m/s, well past any bash threshold
    veh->cruise_velocity = 2000;

    // Put the obstacle along the vehicle's actual heading rather than at a
    // hard-coded tile: face_vec() decides which way 0_degrees points, and guessing
    // wrong just drives away from the wall and proves nothing.
    const auto fv = veh->face_vec();
    const auto start = veh->bub_ms_location();
    const auto obstacle = tripoint_bub_ms(
                              start.x() + static_cast<int>( std::lround( fv.x * 6 ) ),
                              start.y() + static_cast<int>( std::lround( fv.y * 6 ) ), 0 );
    here.ter_set( obstacle, ter_id( "t_wall_wood" ) );
    here.build_map_cache( 0, true );
    REQUIRE( here.is_bashable_ter_furn( obstacle, false ) );
    const auto before = here.ter( obstacle );

    // Build colliders for the obstacle's submap only.  Doing the whole map creates
    // ~176k bodies, including every non-pavement tile out to the map edge, and the
    // vehicle is then wedged before it travels anywhere (measured: velocity
    // collapsing 2000 -> 317 on the first turn).
    auto *pw = here.get_physics_world();
    REQUIRE( pw != nullptr );
    const auto colliders_before = pw->terrain_body_count();
    pw->on_submap_loaded( here, project_to<coords::sm>( here.bub_to_abs( obstacle ) ) );
    REQUIRE( pw->terrain_body_count() > colliders_before );

    for( int turn = 0; turn < 5 && here.ter( obstacle ) == before; ++turn ) {
        here.vehmove();
    }

    CHECK( here.ter( obstacle ) != before );
}

// Terrain colliders are what stop a vehicle driving through a wall, and several
// code paths gate their creation on the player's z-level — so a silent zero here
// is the difference between walls existing and not.  Two invariants:
//
//  - on_submap_loaded actually builds bodies for a submap containing obstacles,
//    and is idempotent: it is now called from more than one place
//    (game::place_player_overmap's destination-z rebuild as well as
//    on_zlevel_changed), and terrain_bodies_[key] is a plain assignment, so a
//    repeat call used to drop live b2BodyIds without destroying them.
//  - on_zlevel_changed moves colliders to the new level.  This is the machinery
//    the place_player_overmap fix relies on, so it needs its own coverage.
TEST_CASE( "box2d_terrain_colliders_build_and_rebuild", "[vehicle][box2d]" )
{
    clear_all_state();
    auto &here = get_map();
    auto *pw = here.get_physics_world();
    REQUIRE( pw != nullptr );

    build_test_map( ter_id( "t_pavement" ) );

    // PhysicsWorld is constructed once per binary and never reset between
    // TEST_CASEs, so this cannot assume an empty world: earlier cases leave
    // colliders behind.  Everything below is measured as a delta.

    const auto wall_z0 = tripoint_bub_ms( 60, 60, 0 );
    here.ter_set( wall_z0, ter_id( "t_wall_wood" ) );
    REQUIRE_FALSE( here.passable( wall_z0 ) );

    const auto sm_z0 = project_to<coords::sm>( here.bub_to_abs( wall_z0 ) );

    // Clear this submap first so the assertion measures a real build rather than a
    // replacement.  Another TEST_CASE may already have registered it — the bash spec
    // does — and on_submap_loaded is idempotent, so loading an already-loaded submap
    // leaves the global count unchanged and the check would fail for the wrong reason.
    pw->on_submap_unloaded( sm_z0, /*submap_still_resident=*/false );
    const auto cleared = pw->terrain_body_count();

    pw->on_submap_loaded( here, sm_z0 );
    const auto after_first = pw->terrain_body_count();
    CHECK( after_first > cleared );

    // Idempotent: a repeat call must replace, not stack.  Asserted against the
    // Box2D world's own body count, NOT the registry: terrain_bodies_[key] is an
    // assignment, so without the guard the old b2BodyIds are dropped from the
    // registry while still living in the world — the registry count is unchanged
    // and would report success.  (Verified: with the guard removed, the registry
    // assertion still passed and only this one fails.)
    const auto world_after_first = pw->world_body_count();
    pw->on_submap_loaded( here, sm_z0 );
    CHECK( pw->terrain_body_count() == after_first );
    CHECK( pw->world_body_count() == world_after_first );

    SECTION( "on_zlevel_changed moves colliders to the new level" ) {
        const auto wall_z1 = tripoint_bub_ms( 60, 60, 1 );
        here.ter_set( wall_z1, ter_id( "t_wall_wood" ) );
        REQUIRE_FALSE( here.passable( wall_z1 ) );

        pw->on_zlevel_changed( here, 0, 1 );
        // z=0's bodies are gone and z=1 now has its own, so colliders still exist.
        // Zero would mean a z-change leaves the player on a level with no
        // collision at all.
        CHECK( pw->terrain_body_count() > 0 );
    }
}

// Terrain colliders must not survive a world teardown.
//
// PhysicsWorld is constructed once, in the map constructor (map.cpp:256), and map is
// a pimpl<map> built once in game::game() — so it lives for the whole process.
// terrain_bodies_ is keyed by tripoint_abs_sm and nothing frees it except the
// destructor, which never runs mid-session.
//
// Returning to the main menu and loading a different world calls MAPBUFFER.clear()
// (main_menu.cpp:1054/1237/1247/1289, game_setup.cpp:725/742) without touching the
// map, so colliders built from world A would stay registered at absolute submap
// coordinates that in world B hold entirely different terrain: invisible walls where
// A had walls, and no collider where B has one.
//
// This covers the reset itself.  The wiring into mapbuffer::clear() is NOT tested:
// MAPBUFFER.clear() frees every submap while map::grid still points at them, so a
// test that calls it leaves the map holding dangling pointers and cannot recover —
// clear_all_state() crashes afterwards.  The game only clears the buffer with no
// world loaded.  The hook is three lines at the single choke point.
TEST_CASE( "box2d_world_teardown_drops_all_terrain_colliders", "[vehicle][box2d]" )
{
    clear_all_state();
    auto &here = get_map();
    auto *pw = here.get_physics_world();
    REQUIRE( pw != nullptr );

    const auto obstacle = tripoint_bub_ms( 60, 60, 0 );
    here.ter_set( obstacle, ter_id( "t_wall_wood" ) );
    REQUIRE( here.impassable_ter_furn( obstacle ) );

    const auto sm = project_to<coords::sm>( here.bub_to_abs( obstacle ) );
    pw->on_submap_unloaded( sm, /*submap_still_resident=*/false );
    pw->on_submap_loaded( here, sm );
    REQUIRE( pw->terrain_body_count() > 0 );

    pw->clear_world_bodies();
    CHECK( pw->terrain_body_count() == 0 );

    // Idempotent, and a rebuild after teardown still works — a stale registry would
    // otherwise make the next world's submaps look already-loaded.
    pw->clear_world_bodies();
    CHECK( pw->terrain_body_count() == 0 );

    pw->on_submap_loaded( here, sm );
    CHECK( pw->terrain_body_count() > 0 );
}

// Ramp z-transition for a vehicle that KEEPS Box2D position authority.
//
// vehicle_ramp_test.cpp sets box2d_position_authority = false for every vehicle it
// builds (lines 109 and 159), so its 83 failing assertions all exercise the legacy
// tile-step path.  Under BOX2D=ON that is not the path players drive on: real
// vehicles keep authority and move via the readback walk in map::vehmove(), which
// steps move_vehicle() one tile at a time.  Nothing covered the ramp on that path.
//
// Terrain layout mirrors vehicle_ramp_test's `up` case: the west half (x < 60) sits
// at z=1, the east half (x >= 62) at z=0, joined by ramp tiles at x=60/61.  The
// vehicle starts east at z=0 heading west and must end up at z=1.
TEST_CASE( "box2d_authority_vehicle_climbs_ramp", "[vehicle][box2d][ramp]" )
{
    clear_all_state();
    clear_vehicles();
    auto &here = get_map();

    constexpr int transit_x = 60;
    constexpr int highx = transit_x;      // 60
    constexpr int lowx = transit_x + 1;   // 61

    for( int y = 0; y < SEEY * MAPSIZE; y++ ) {
        for( int x = 0; x < transit_x; x++ ) {
            here.ter_set( tripoint_bub_ms( x, y, -1 ), ter_id( "t_rock" ) );
            here.ter_set( tripoint_bub_ms( x, y, 0 ), ter_id( "t_rock" ) );
            here.ter_set( tripoint_bub_ms( x, y, 1 ), ter_id( "t_pavement" ) );
        }
        here.ter_set( tripoint_bub_ms( lowx, y, 0 ), ter_id( "t_ramp_up_low" ) );
        here.ter_set( tripoint_bub_ms( highx, y, 0 ), ter_id( "t_ramp_up_high" ) );
        here.ter_set( tripoint_bub_ms( lowx, y, 1 ), ter_id( "t_ramp_down_low" ) );
        here.ter_set( tripoint_bub_ms( highx, y, 1 ), ter_id( "t_ramp_down_high" ) );
        for( int x = transit_x + 2; x < SEEX * MAPSIZE; x++ ) {
            here.ter_set( tripoint_bub_ms( x, y, 1 ), ter_id( "t_open_air" ) );
            here.ter_set( tripoint_bub_ms( x, y, 0 ), ter_id( "t_pavement" ) );
            here.ter_set( tripoint_bub_ms( x, y, -1 ), ter_id( "t_rock" ) );
        }
    }
    for( const auto z : std::array{ -1, 0, 1 } ) {
        here.invalidate_map_cache( z );
        here.build_map_cache( z, true );
    }

    // Heading west: 0 degrees is +x (east) and rotation is clockwise, so 180 is -x.
    auto *veh_ptr = here.add_vehicle( vproto_id( "car_test" ), tripoint_bub_ms( 75, 60, 0 ),
                                      180_degrees, 100, 0 );
    REQUIRE( veh_ptr != nullptr );
    vehicle &veh = *veh_ptr;
    REQUIRE( veh.box2d_position_authority );

    veh.tags.insert( "IN_CONTROL_OVERRIDE" );
    veh.engine_on = true;
    veh.velocity = 800;
    veh.cruise_velocity = 800;

    int reached_z = veh.bub_ms_location().z();
    int min_velocity = veh.velocity;
    for( int turn = 0; turn < 30 && reached_z == 0; ++turn ) {
        here.vehmove();
        if( veh.skidding ) { break; }
        reached_z = veh.bub_ms_location().z();
        min_velocity = std::min( min_velocity, veh.velocity );
    }

    CAPTURE( veh.bub_ms_location().to_string() );
    CAPTURE( reached_z );
    CAPTURE( min_velocity );
    CAPTURE( veh.skidding );

    // The vehicle must actually climb, and must not be brought to a dead stop doing
    // it: a halt on a ramp is the player-visible symptom.
    CHECK( reached_z == 1 );
    CHECK( min_velocity > 0 );
}

// map::load() re-anchors the whole reality bubble.  It must not leave the previous
// bubble's terrain colliders behind.
//
// load() drops every grid pointer with a bare
// `std::fill( grid.begin(), grid.end(), nullptr )` (map.cpp:1212) and never calls
// on_submap_unloaded, then loadn() builds fresh bodies for the new bubble.  Terrain
// bodies are keyed by absolute submap but *positioned* in bubble coordinates, and a
// distant load carries no shift delta, so on_map_shifted never runs either.  Without
// an explicit teardown the old bodies both accumulate and sit at stale positions —
// invisible walls at wherever those tiles used to appear on screen.
//
// Travelling away and back is the tightest statement of the invariant: the collider
// count for a given bubble must be a function of that bubble, not of how many times
// the player has visited it.
//
// PENDING SPEC — tagged [!shouldfail] because the harness cannot yet build terrain
// colliders at all, so the invariant is currently unmeasurable rather than violated.
// on_submap_loaded() only fires from map::loadn() for submaps on the player's
// z-level, on_tile_changed() only refreshes submaps that are ALREADY registered, and
// a fresh test world is open field where no tile earns a collider.  count_home comes
// out 0 and the equality below then holds trivially, which is why the REQUIRE guard
// is there — an earlier assertion in this file passed for exactly that reason while
// measuring nothing.
//
// The defect it targets is real and read-verified: map::load() fills the grid with
// nullptr (map.cpp:1212 before the fix) and never calls on_submap_unloaded.  The fix
// is committed; this spec flips green once the harness can produce colliders, at
// which point drop the tag.
TEST_CASE( "box2d_map_load_does_not_accumulate_colliders", "[!shouldfail][vehicle][box2d]" )
{
    clear_all_state();
    auto &here = get_map();
    auto *pw = here.get_physics_world();
    REQUIRE( pw != nullptr );

    const auto home = here.get_abs_sub();
    // Far enough that no submap of one bubble is a submap of the other.
    const auto away = home + tripoint_rel_sm( 3 * MAPSIZE, 3 * MAPSIZE, 0 );
    REQUIRE( away.z() == home.z() );

    // A fresh test world is open field, and open ground correctly gets no collider,
    // so the bubble needs real obstacles or there is nothing to count.  These live in
    // MAPBUFFER-resident submaps, so they survive the travel and are still there when
    // the bubble is re-anchored back onto them.
    for( int i = 0; i < 12; i++ ) {
        here.ter_set( tripoint_bub_ms( 20 + i * 4, 60, 0 ), ter_id( "t_wall_wood" ) );
    }

    here.load( home, true );
    const auto count_home = pw->terrain_body_count();

    here.load( away, true );
    const auto count_away = pw->terrain_body_count();

    here.load( home, true );
    const auto count_home_again = pw->terrain_body_count();

    CAPTURE( count_home );
    CAPTURE( count_away );
    CAPTURE( count_home_again );

    // Guard against a vacuous pass: if no colliders are ever built the equality below
    // holds trivially.  This is exactly how an earlier assertion in this file managed
    // to measure nothing at all.
    REQUIRE( count_home > 0 );

    // Revisiting must reproduce the original count, not add to it.
    CHECK( count_home_again == count_home );
}
