#include "catch/catch_amalgamated.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "coordinates.h"
#include "enums.h"
#include "game.h"
#include "item.h"
#include "itype.h"
#include "line.h"
#include "map.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "string_formatter.h"
#include "test_statistics.h"
#include "type_id.h"
#include "units.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vpart_position.h"
#include "vpart_range.h"

using efficiency_stat = statistics<int>;

const efftype_id effect_blind( "blind" );

static void clear_game( const ter_id &terrain )
{
    // Set to turn 0 to prevent solars from producing power
    calendar::turn = calendar::turn_zero;
    clear_states( state::avatar | state::vehicle );

    // Move player somewhere safe
    REQUIRE_FALSE( g->u.in_vehicle );
    g->u.setpos( tripoint_bub_ms::zero() );
    // Blind the player to avoid needless drawing-related overhead
    g->u.add_effect( effect_blind, 365_days, bodypart_str_id::NULL_ID() );

    build_test_map( terrain );
}

// Returns how much fuel did it provide
// But contains only fuels actually used by engines
static std::map<itype_id, int> set_vehicle_fuel( vehicle &v, const float veh_fuel_mult )
{
    // First we need to find the fuels to set
    // That is, fuels actually used by some engine
    std::set<itype_id> actually_used;
    for( const vpart_reference vp : v.get_all_parts() ) {
        vehicle_part &pt = vp.part();
        if( pt.is_engine() ) {
            actually_used.insert( pt.info().fuel_type );
            pt.enabled = true;
        } else {
            // Disable all parts that use up power or electric cars become non-deterministic
            pt.enabled = false;
        }
    }

    // We ignore battery when setting fuel because it uses designated "tanks"
    actually_used.erase( itype_id( "battery" ) );

    // Currently only one liquid fuel supported
    REQUIRE( actually_used.size() <= 1 );
    itype_id liquid_fuel = itype_id::NULL_ID();
    for( const itype_id &ft : actually_used ) {
        if( ft->phase == LIQUID ) {
            liquid_fuel = ft;
            break;
        }
    }

    // Set fuel to a given percentage
    // Batteries are special cased because they aren't liquid fuel
    std::map<itype_id, int> ret;
    for( const vpart_reference vp : v.get_all_parts() ) {
        vehicle_part &pt = vp.part();

        if( pt.is_battery() ) {
            pt.ammo_set( itype_id( "battery" ), pt.ammo_capacity() * veh_fuel_mult );
            ret[ itype_id( "battery" ) ] += pt.ammo_capacity() * veh_fuel_mult;
        } else if( pt.is_tank() && !liquid_fuel.is_null() ) {
            float qty = pt.ammo_capacity() * veh_fuel_mult;
            qty *= std::max( liquid_fuel->stack_size, 1 );
            qty /= to_milliliter( units::legacy_volume_factor );
            pt.ammo_set( liquid_fuel, qty );
            ret[ liquid_fuel ] += qty;
        } else {
            pt.ammo_unset();
        }
    }

    // We re-add battery because we want it accounted for, just not in the section above
    actually_used.insert( itype_id( "battery" ) );
    for( auto iter = ret.begin(); iter != ret.end(); ) {
        if( iter->second <= 0 || !actually_used.contains( iter->first ) ) {
            iter = ret.erase( iter );
        } else {
            ++iter;
        }
    }
    return ret;
}

// Returns the lowest percentage of fuel left
// i.e. 1 means no fuel was used, 0 means at least one dry tank
static float fuel_percentage_left( vehicle &v, const std::map<itype_id, int> &started_with )
{
    std::map<itype_id, int> fuel_amount;
    std::set<itype_id> consumed_fuels;
    for( const vpart_reference vp : v.get_all_parts() ) {
        vehicle_part &pt = vp.part();

        if( ( pt.is_battery() || pt.is_reactor() || pt.is_tank() ) &&
            !pt.ammo_current().is_null() ) {
            fuel_amount[ pt.ammo_current() ] += pt.ammo_remaining();
        }

        if( pt.is_engine() && !pt.info().fuel_type.is_null() ) {
            consumed_fuels.insert( pt.info().fuel_type );
        }
    }

    float left = 1.0f;
    for( const auto &type : consumed_fuels ) {
        const auto iter = started_with.find( type );
        // Weird - we started without this fuel
        float fuel_amt_at_start = iter != started_with.end() ? iter->second : 0.0f;
        REQUIRE( fuel_amt_at_start != 0.0f );
        left = std::min( left, static_cast<float>( fuel_amount[type] ) / fuel_amt_at_start );
    }

    return left;
}

const float fuel_level = 0.1f;
const int cycle_limit = 100;

// Algorithm goes as follows:
// Clear map
// Spawn a vehicle
// Set its fuel up to some percentage - remember exact fuel counts that were set here
// Drive it for a while, always moving it back to start point every turn to avoid it going off the bubble
// When moving back, record the sum of the tiles moved so far
// Repeat that for a set number of turns or until all fuel is drained
// Compare saved percentage (set before) to current percentage
// Rescale the recorded number of tiles based on fuel percentage left
// (i.e. 0% fuel left means no scaling, 50% fuel left means double the effective distance)
// Return the rescaled number
static int test_efficiency( const vproto_id &veh_id, int &expected_mass,
                            const ter_id &terrain,
                            const int reset_velocity_turn, const int target_distance,
                            const bool smooth_stops = false, const bool test_mass = true,
                            const bool in_reverse = false )
{
    int min_dist = target_distance * 0.92;
    int max_dist = target_distance * 1.08;
    clear_game( terrain );

    const tripoint_bub_ms map_starting_point( 60, 60, 0 );
    map &here = get_map();
    vehicle *veh_ptr = here.add_vehicle( veh_id, map_starting_point, -90_degrees, 0, 0 );

    REQUIRE( veh_ptr != nullptr );
    if( veh_ptr == nullptr ) {
        return 0;
    }

    vehicle &veh = *veh_ptr;

    // Remove all items from cargo to normalize weight.
    for( const vpart_reference vp : veh.get_all_parts() ) {
        veh_ptr->get_items( vp.part_index() ).clear();
        vp.part().ammo_consume( vp.part().ammo_remaining(), vp.pos() );
    }
    for( const vpart_reference vp : veh.get_avail_parts( "OPENABLE" ) ) {
        veh.close( vp.part_index() );
    }

    veh.refresh_insides();

    if( test_mass ) {
        const int actual_mass = to_gram( veh.total_mass() );
        const int tolerance = expected_mass / 50; // 2% — cargo/fuel RNG causes small variance
        CHECK( std::abs( actual_mass - expected_mass ) <= tolerance );
    }
    expected_mass = to_gram( veh.total_mass() );
    veh.check_falling_or_floating();
    REQUIRE( !veh.is_in_water() );
    const auto &starting_fuel = set_vehicle_fuel( veh, fuel_level );
    // This is ugly, but improves accuracy: compare the result of fuel approx function
    // rather than the amount of fuel we actually requested
    const float starting_fuel_per = fuel_percentage_left( veh, starting_fuel );
    REQUIRE( std::abs( starting_fuel_per - 1.0f ) < 0.001f );

    const auto starting_point = veh.bub_ms_location();
    veh.tags.insert( "IN_CONTROL_OVERRIDE" );
    veh.engine_on = true;

    const int sign = in_reverse ? -1 : 1;
    const int target_velocity = sign * std::min( 2235,
                                veh.safe_ground_velocity( false ) );
    veh.cruise_velocity = target_velocity;
    // If we aren't testing repeated cold starts, start the vehicle at cruising velocity.
    // Otherwise changing the amount of fuel in the tank perturbs the test results.
    if( reset_velocity_turn == -1 ) {
        veh.velocity = target_velocity;
    }
    int reset_counter = 0;
    int tiles_travelled = 0;
    int cycles_left = cycle_limit;
    bool accelerating = true;
    CHECK( veh.safe_velocity() > 0 );
    while( veh.engine_on && veh.safe_velocity() > 0 && cycles_left > 0 ) {
        cycles_left--;
        here.vehmove();
        veh.idle( true );
        // If the vehicle starts skidding, the effects become random and test is RUINED
        REQUIRE( !veh.skidding );
        for( const tripoint_abs_ms &pos : veh.get_points() ) {
            REQUIRE( here.ter( here.abs_to_bub( pos ) ) );
        }
        // How much it moved
        tiles_travelled += square_dist( starting_point, veh.bub_ms_location() );
        // Bring it back to starting point to prevent it from leaving the map
        const tripoint_rel_ms displacement = starting_point - veh.bub_ms_location();
        here.displace_vehicle( veh, tripoint_rel_ms( displacement ) );
        if( reset_velocity_turn < 0 ) {
            continue;
        }

        reset_counter++;
        if( reset_counter > reset_velocity_turn ) {
            if( smooth_stops ) {
                accelerating = !accelerating;
                veh.cruise_velocity = accelerating ? target_velocity : 0;
            } else {
                veh.velocity = 0;
                veh.last_turn = 0_degrees;
                veh.of_turn_carry = 0;
            }
            reset_counter = 0;
        }
    }

    float fuel_left = fuel_percentage_left( veh, starting_fuel );
    REQUIRE( starting_fuel_per - fuel_left > 0.0001f );
    const float fuel_percentage_used = fuel_level * ( starting_fuel_per - fuel_left );
    int adjusted_tiles_travelled = tiles_travelled / fuel_percentage_used;
    if( target_distance >= 0 ) {
        INFO( veh.name );
        CHECK( adjusted_tiles_travelled >= min_dist * 0.95 );
        CHECK( adjusted_tiles_travelled <= max_dist * 1.05 );
    }

    return adjusted_tiles_travelled;
}

static efficiency_stat find_inner(
    const std::string &type, int &expected_mass, const std::string &terrain, const int delay,
    const bool smooth, const bool test_mass = false, const bool in_reverse = false )
{
    efficiency_stat efficiency;
    for( int i = 0; i < 10; i++ ) {
        efficiency.add( test_efficiency( vproto_id( type ), expected_mass, ter_id( terrain ),
                                         delay, -1, smooth, test_mass, in_reverse ) );
    }
    return efficiency;
}

static void print_stats( const efficiency_stat &st )
{
    if( st.min() == st.max() ) {
        cata_printf( "All results %d.\n", st.min() );
    } else {
        cata_printf( "Min %d, Max %d, Midpoint %f.\n", st.min(), st.max(),
                     ( st.min() + st.max() ) / 2.0 );
    }
}

static void print_efficiency(
    const std::string &type, int expected_mass, const std::string &terrain, const int delay,
    const bool smooth )
{
    cata_printf( "Testing %s on %s with %s: ",
                 type.c_str(), terrain.c_str(), ( delay < 0 ) ? "no resets" : "resets every 5 turns" );
    print_stats( find_inner( type, expected_mass, terrain, delay, smooth ) );
}

static void find_efficiency( const std::string &type )
{
    SECTION( "finding efficiency of " + type ) {
        print_efficiency( type, 0,  "t_pavement", -1, false );
        print_efficiency( type, 0, "t_dirt", -1, false );
        print_efficiency( type, 0, "t_pavement", 5, false );
        print_efficiency( type, 0, "t_dirt", 5, false );
    }
}

static int avg_from_stat( const efficiency_stat &st )
{
    const int ugly_integer = ( st.min() + st.max() ) / 2.0;
    // Round to 4 most significant places
    const int magnitude = std::max<int>( 0, std::floor( std::log10( ugly_integer ) ) );
    const int precision = std::max<int>( 1, std::round( std::pow( 10.0, magnitude - 3 ) ) );
    return ugly_integer - ugly_integer % precision;
}

static int print_test_strings( const std::string &type, bool in_reverse = false )
{
    int expected_mass = 0;
    int v1 = avg_from_stat( find_inner( type, expected_mass, "t_pavement", -1, false, false,
                                        in_reverse ) );
    int expm = expected_mass;
    int v2 = avg_from_stat( find_inner( type, expected_mass, "t_dirt", -1, false, false,
                                        in_reverse ) );
    int v3 = avg_from_stat( find_inner( type, expected_mass, "t_pavement", 5, false, false,
                                        in_reverse ) );
    int v4 = avg_from_stat( find_inner( type, expected_mass, "t_dirt", 5, false, false,
                                        in_reverse ) );

    cata_printf(
        "    test_vehicle( \"%s\", %d, %d, %d, %d, %d%s );\n",
        type, expm, v1, v2, v3, v4, in_reverse ? ", 0, 0, true" : ""
    );

    return v1;
}

static auto test_vehicle(
    const std::string &type, const int expected_mass,
    const int pavement_target, const int dirt_target,
    const int pavement_target_w_stops, const int dirt_target_w_stops,
    const int pavement_target_smooth_stops = 0, const int dirt_target_smooth_stops = 0,
    const bool in_reverse = false ) -> void
{
    const auto run_case = [&]( const char *label, const ter_id & terrain,
                               const int reset_velocity_turn, const int target_distance,
    const bool smooth_stops ) {
        CAPTURE( type );
        CAPTURE( label );
        auto current_expected_mass = expected_mass;
        test_efficiency( vproto_id( type ), current_expected_mass, terrain,
                         reset_velocity_turn, target_distance, smooth_stops, true, in_reverse );
    };

    run_case( "on pavement", ter_id( "t_pavement" ), -1, pavement_target, false );
    run_case( "on dirt", ter_id( "t_dirt" ), -1, dirt_target, false );
    run_case( "on pavement, full stop every 5 turns", ter_id( "t_pavement" ), 5,
              pavement_target_w_stops, false );
    run_case( "on dirt, full stop every 5 turns", ter_id( "t_dirt" ), 5,
              dirt_target_w_stops, false );

    if( pavement_target_smooth_stops > 0 ) {
        run_case( "on pavement, alternating 5 turns of acceleration and 5 turns of decceleration",
                  ter_id( "t_pavement" ), 5, pavement_target_smooth_stops, true );
    }
    if( dirt_target_smooth_stops > 0 ) {
        run_case( "on dirt, alternating 5 turns of acceleration and 5 turns of decceleration",
                  ter_id( "t_dirt" ), 5, dirt_target_smooth_stops, true );
    }
}

std::vector<std::string> vehs_to_test = {{
        "beetle_test",
        "car_test",
        "car_sports_test",
        "electric_car_test",
        "suv_test",
        "motorcycle_test",
        "quad_bike_test",
        "scooter_test",
        "superbike_test",
        "ambulance_test",
        "fire_engine_test",
        "fire_truck_test",
        "truck_swat_test",
        "tractor_plow_test",
        "apc_test",
        "humvee_test",
        "road_roller_test",
        "golf_cart_test"
    }
};

/** This isn't a test per se, it executes this code to
 * determine the current state of vehicle efficiency.
 **/
TEST_CASE( "vehicle_find_efficiency", "[.]" )
{
    clear_all_state();
    for( const std::string &veh : vehs_to_test ) {
        find_efficiency( veh );
    }
}

/** This is even less of a test. It generates C++ lines for the actual test below */
TEST_CASE( "make_vehicle_efficiency_case", "[.]" )
{
    clear_all_state();
    const float acceptable = 1.25;
    std::map<std::string, int> forward_distance;
    for( const std::string &veh : vehs_to_test ) {
        const int in_forward = print_test_strings( veh );
        forward_distance[ veh ] = in_forward;
    }
    printf( "\n    // in reverse\n" );
    for( const std::string &veh : vehs_to_test ) {
        const int in_reverse = print_test_strings( veh, true );
        CHECK( in_reverse < ( acceptable * forward_distance[ veh ] ) );
    }
}

// TODO:
// Amount of fuel needed to reach safe speed.
// Amount of cruising range for a fixed amount of fuel.
// Fix test for electric vehicles
TEST_CASE( "vehicle_efficiency", "[vehicle] [engine]" )
{
    clear_all_state();
    test_vehicle( "beetle_test", 713837, 98240, 84140, 84110, 67140 );
    test_vehicle( "car_test", 1020629, 76590, 49330, 41160, 18930 );
    test_vehicle( "car_sports_test", 1052895, 49800, 34920, 31530, 20470 );
    test_vehicle( "electric_car_test", 774098, 19940, 15750, 11500, 8528 );
    test_vehicle( "suv_test", 1232142, 131000, 66990, 65260, 23100 );
    test_vehicle( "motorcycle_test", 163085, 45400, 37330, 53160, 41540 );
    test_vehicle( "quad_bike_test", 264465, 28370, 28370, 36490, 36490 );
    test_vehicle( "scooter_test", 57587, 140500, 140500, 143500, 143500 );
    test_vehicle( "superbike_test", 244085, 32940, 18340, 35080, 18970 );
    test_vehicle( "ambulance_test", 1723249, 131700, 110500, 61010, 50360 );
    test_vehicle( "fire_engine_test", 2152677, 511400, 506700, 309700, 306700 );
    test_vehicle( "fire_truck_test", 6188273, 41820, 5386, 12020, 2509 );
    test_vehicle( "truck_swat_test", 5744708, 74520, 7811, 19230, 4562 );
    test_vehicle( "tractor_plow_test", 725658, 195300, 195300, 102100, 102100 );
    test_vehicle( "apc_test", 5774683, 235300, 235300, 68770, 68770 );
    test_vehicle( "humvee_test", 5348031, 65920, 51800, 16280, 11780 );
    test_vehicle( "road_roller_test", 8648054, 130900, 42370, 14030, 3914 );
    test_vehicle( "golf_cart_test", 319930, 14830, 8844, 17660, 9416 );

    // in reverse
    test_vehicle( "beetle_test", 713837, 48830, 48830, 35510, 33420, 0, 0, true );
    test_vehicle( "car_test", 1032474, 63800, 27770, 30940, 18370, 0, 0, true );
    test_vehicle( "car_sports_test", 1052382, 49800, 34920, 28320, 17090, 0, 0, true );
    test_vehicle( "electric_car_test", 774098, 19940, 15750, 11500, 8528, 0, 0, true );
    test_vehicle( "suv_test", 1220467, 94930, 47890, 45860, 22720, 0, 0, true );
    test_vehicle( "motorcycle_test", 163085, 16340, 16270, 13340, 12820, 0, 0, true );
    test_vehicle( "quad_bike_test", 264465, 16620, 16620, 12750, 12750, 0, 0, true );
    test_vehicle( "scooter_test", 57587, 53990, 53990, 41330, 41330, 0, 0, true );
    test_vehicle( "superbike_test", 244085, 11270, 8967, 11140, 6820, 0, 0, true );
    test_vehicle( "ambulance_test", 1723308, 47800, 47760, 30990, 29060, 0, 0, true );
    test_vehicle( "fire_engine_test", 2147131, 207700, 207700, 143000, 143000, 0, 0, true );
    test_vehicle( "fire_truck_test", 6188273, 45240, 12400, 11810, 2375, 0, 0, true );
    test_vehicle( "truck_swat_test", 5739946, 58740, 27570, 18030, 2064, 0, 0, true );
    test_vehicle( "tractor_plow_test", 725658, 54440, 54440, 41300, 41300, 0, 0, true );
    test_vehicle( "apc_test", 5770343, 148700, 148700, 66100, 66100, 0, 0, true );
    test_vehicle( "humvee_test", 5346997, 64420, 35140, 15600, 10980, 0, 0, true );
    test_vehicle( "road_roller_test", 8648054, 66480, 66790, 14080, 4151, 0, 0, true );
    test_vehicle( "golf_cart_test", 319630, 14830, 8387, 17660, 6124, 0, 0, true );
}
