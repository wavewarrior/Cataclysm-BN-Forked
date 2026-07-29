#include "catch/catch_amalgamated.hpp"
#include <algorithm>
#include <map>
#include <ranges>
#include <utility>
#include <vector>

#include "ammo.h"
#include "avatar.h"
#include "calendar.h"
#include "creature_functions.h"
#include "coordinates.h"
#include "faction.h"
#include "game.h"
#include "item.h"
#include "itype.h"
#include "map.h"
#include "map_helpers.h"
#include "monster.h"
#include "npc.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "string_id.h"
#include "type_id.h"
#include "units.h"
#include "value_ptr.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vehicle_functions.h"
#include "vehicle_part.h"

static std::vector<const vpart_info *> turret_types()
{
    std::vector<const vpart_info *> res;

    for( const auto &e : vpart_info::all() ) {
        if( e.second.has_flag( "TURRET" ) ) {
            res.push_back( &e.second );
        }
    }

    return res;
}

static auto biggest_tank( const ammotype &ammo ) -> const vpart_info *
{
    std::vector<const vpart_info *> res;

    for( const auto &e : vpart_info::all() ) {
        const auto &vp = e.second;
        if( !item::spawn_temporary( vp.item )->is_watertight_container() ) {
            continue;
        }

        // vpart_info::fuel_type is the tank's *declared default* fuel, and every
        // generic tank in data/json/vehicleparts/tanks.json leaves it unset.  So
        // requiring `fuel->ammo->type == ammo` matched only tanks hard-wired to one
        // fuel and rejected every general-purpose container — for any ammo at all.
        // That is what made the water cannon and both flamethrowers look like they
        // had no tank available: a plain watertight tank is exactly what you would
        // load water or napalm into.
        //
        // Accept a tank when it either declares a matching fuel, or declares none
        // and is therefore general purpose.
        const itype *fuel = &*vp.fuel_type;
        const bool declares_match = fuel->ammo && fuel->ammo->type == ammo;
        const bool general_purpose = vp.fuel_type.is_null();
        if( declares_match || general_purpose ) {
            res.push_back( &vp );
        }
    }

    if( res.empty() ) { return nullptr; }

    return *std::ranges::max_element( res, {}, &vpart_info::size );
}

TEST_CASE( "vehicle_turret", "[vehicle][gun][magazine][.]" )
{
    clear_all_state();
    map &here = get_map();
    avatar &player_character = get_avatar();
    for( auto e : turret_types() ) {
        SECTION( e->name() ) {
            vehicle *veh = here.add_vehicle( vproto_id( "none" ), point_bub_ms( 65, 65 ), 270_degrees, 0, 0 );
            REQUIRE( veh );

            const int idx = veh->install_part( tripoint_mnt_veh::zero(), e->get_id(), true );
            REQUIRE( idx >= 0 );

            REQUIRE( veh->install_part( tripoint_mnt_veh::zero(), vpart_id( "storage_battery" ), true ) >= 0 );
            veh->charge_battery( 10000 );

            // Take the ammotype from the gun's accepted set, NOT by reinterpreting
            // ammo_default()'s string.  ammo_default() returns an itype_id — an ammo
            // *item* — while ammotype names an ammo *category*, and they are separate
            // id namespaces.  The old `ammotype( ...ammo_default().str() )` only
            // happened to work for guns whose default ammo item id coincides with an
            // ammotype name; for the rest it produced an invalid ammotype, so
            // ammo_set() silently failed and the turret was never ready.  Visible in
            // the log as "Tried to get invalid ammunition: gas_fungicidal" and
            // "Tried to set invalid ammo [100] for m249".
            const auto &accepted = veh->turret_query( veh->part( idx ) ).base().ammo_types();
            // A gun may accept several ammotypes and `accepted` is a std::set, so this
            // takes the lowest-sorted one.  That is fine because biggest_tank() now
            // accepts general-purpose tanks, so it resolves for any liquid ammo rather
            // than only for ammo with a purpose-built tank.
            const auto ammo = accepted.empty() ? ammotype::NULL_ID() : *accepted.begin();

            if( veh->part_flag( idx, "USE_TANKS" ) ) {
                auto *tank = biggest_tank( ammo );
                REQUIRE( tank );
                INFO( tank->get_id().str() );

                auto tank_idx = veh->install_part( tripoint_mnt_veh::zero(), tank->get_id(), true );
                REQUIRE( tank_idx >= 0 );
                REQUIRE( veh->part( tank_idx ).ammo_set( ammo->default_ammotype() ) );

            } else if( ammo ) {
                veh->part( idx ).ammo_set( ammo->default_ammotype() );
            }

            auto qry = veh->turret_query( veh->part( idx ) );
            REQUIRE( qry );

            REQUIRE( qry.query() == turret_data::status::ready );
            REQUIRE( qry.range() > 0 );

            player_character.setpos( veh->bub_part_location( idx ) );
            REQUIRE( qry.fire( player_character, player_character.abs_pos() + point_rel_ms( qry.range(),
                               0 ) ) > 0 );

            here.destroy_vehicle( veh );
        }
    }
}

TEST_CASE( "vehicle_turret_autoloader_integral_magazine", "[vehicle][gun][turret][autoload]" )
{
    clear_all_state();
    map &here = get_map();
    vehicle *veh = here.add_vehicle( vproto_id( "none" ), point_bub_ms( 65, 65 ), 270_degrees, 0, 0 );
    REQUIRE( veh );

    const auto turret_part_id = vpart_id( "mounted_rebar_rifle" );
    const auto autoloader_part_id = vpart_id( "turret_autoloader" );
    const auto cargo_part_id = vpart_id( "box" );
    const auto battery_part_id = vpart_id( "storage_battery" );

    const auto turret_index = veh->install_part( tripoint_mnt_veh::zero(), turret_part_id, true );
    REQUIRE( turret_index >= 0 );
    REQUIRE( veh->install_part( tripoint_mnt_veh::zero(), autoloader_part_id, true ) >= 0 );
    const auto cargo_index = veh->install_part( tripoint_mnt_veh::zero(), cargo_part_id, true );
    REQUIRE( cargo_index >= 0 );
    REQUIRE( veh->install_part( tripoint_mnt_veh::zero(), battery_part_id, true ) >= 0 );
    veh->charge_battery( 10000 );

    vehicle_part &turret_part = veh->part( turret_index );
    item &gun = turret_part.get_base();
    REQUIRE( gun.magazine_integral() );
    const auto ammo_capacity = gun.ammo_capacity();
    REQUIRE( ammo_capacity > 1 );

    const auto ammo_id = itype_id( "rebar_rail" );
    gun.ammo_set( ammo_id, 0 );
    const auto ammo_stack = ammo_capacity * 2;
    auto remaining = veh->add_item( cargo_index, item::spawn( ammo_id, calendar::turn, ammo_stack ) );
    REQUIRE( !remaining );

    auto current_ammo = gun.ammo_remaining();
    auto last_ammo = current_ammo;
    auto tries = 0;
    const auto max_tries = ammo_capacity * 2;
    while( current_ammo < ammo_capacity && tries < max_tries ) {
        calendar::turn += 1_minutes;
        vehicle_funcs::try_autoload_turret( *veh, turret_part );
        current_ammo = gun.ammo_remaining();
        if( current_ammo > last_ammo ) {
            last_ammo = current_ammo;
            tries = 0;
        } else {
            ++tries;
        }
    }
    REQUIRE( gun.ammo_remaining() == ammo_capacity );
}

TEST_CASE( "vehicle_turret_iff_protects_followers_in_line_of_fire", "[vehicle][turret][npc][iff]" )
{
    clear_all_state();
    build_test_map( ter_id( "t_dirt" ) );
    map &here = get_map();
    set_time( calendar::turn_zero + 12_hours );

    const auto shooter_pos = tripoint_bub_ms( 60, 60, 0 );
    avatar &shooter = get_avatar();
    shooter.setpos( shooter_pos );
    shooter.set_body();

    const auto follower_pos = shooter_pos + point( 3, 0 );
    npc &follower = spawn_npc( follower_pos.xy(), "thug" );
    follower.set_fac( faction_id( "your_followers" ) );
    follower.set_attitude( NPCATT_FOLLOW );
    REQUIRE( follower.is_player_ally() );
    REQUIRE( shooter.attitude_to( follower ) == Attitude::A_FRIENDLY );

    const auto hostile_pos = shooter_pos + point( 8, 0 );
    monster &hostile = spawn_test_monster( "mon_zombie_tough", hostile_pos );
    here.invalidate_map_cache( shooter_pos.z() );
    here.build_map_cache( shooter_pos.z(), true );
    REQUIRE( shooter.sees( hostile ) );

    const auto target = creature_functions::auto_find_hostile_target(
                            shooter, { .range = 20, .trail = false, .area = 0 } );
    REQUIRE_FALSE( target.has_value() );
    CHECK( target.error() == 1 );
}

TEST_CASE( "vehicle_turret_iff_allows_clear_shots", "[vehicle][turret][npc][iff]" )
{
    clear_all_state();
    build_test_map( ter_id( "t_dirt" ) );
    map &here = get_map();
    set_time( calendar::turn_zero + 12_hours );

    const auto shooter_pos = tripoint_bub_ms( 60, 60, 0 );
    avatar &shooter = get_avatar();
    shooter.setpos( shooter_pos );
    shooter.set_body();

    const auto follower_pos = shooter_pos + point( 0, 5 );
    npc &follower = spawn_npc( follower_pos.xy(), "thug" );
    follower.set_fac( faction_id( "your_followers" ) );
    follower.set_attitude( NPCATT_FOLLOW );
    REQUIRE( follower.is_player_ally() );

    const auto hostile_pos = shooter_pos + point( 8, 0 );
    monster &hostile = spawn_test_monster( "mon_zombie_tough", hostile_pos );
    here.invalidate_map_cache( shooter_pos.z() );
    here.build_map_cache( shooter_pos.z(), true );
    REQUIRE( shooter.sees( hostile ) );

    const auto target = creature_functions::auto_find_hostile_target(
                            shooter, { .range = 20, .trail = false, .area = 0 } );
    REQUIRE( target.has_value() );
    CHECK( &target->get() == &hostile );
}
