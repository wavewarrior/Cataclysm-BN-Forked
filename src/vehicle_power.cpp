#include "vehicle.h"
#include "detached_ptr.h"
#include "units_mass.h"
#include "vehicle_part.h" // IWYU pragma: associated
#include "vpart_position.h" // IWYU pragma: associated
#include "vpart_range.h" // IWYU pragma: associated

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <list>
#include <memory>
#include <numeric>
#include <queue>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <ranges>

#include "active_tile_data_def.h"
#include "avatar.h"
#include "avatar_functions.h"
#include "bionics.h"
#include "cata_utility.h"
#include "character.h"
#include "clzones.h"
#include "coordinates.h"
#include "creature.h"
#include "cuboid_rectangle.h"
#include "debug.h"
#include "distribution_grid.h"
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "explosion.h"
#include "faction.h"
#include "field_type.h"
#include "flag.h"
#include "game.h"
#include "item.h"
#include "item_category.h"
#include "item_contents.h"
#include "item_group.h"
#include "itype.h"
#include "json.h"
#include "make_static.h"
#include "map.h"
#include "map_iterator.h"
#include "mapbuffer.h"
#include "mapdata.h"
#include "messages.h"
#include "monster.h"
#include "npc.h"
#include "options.h"
#include "output.h"
#include "overmapbuffer.h"
#include "player.h"
#include "player_activity.h"
#include "point_float.h"
#include "rng.h"
#include "sounds.h"
#include "string_formatter.h"
#include "string_id.h"
#include "string_input_popup.h"
#include "submap.h"
#include "translations.h"
#include "units_utility.h"
#include "veh_type.h"
#include "vehicle_palette.h"
#include "vehicle_functions.h"
#include "weather.h"
#include "ui.h"
/*
 * Speed up all those if ( blarg == "structure" ) statements that are used everywhere;
 *   assemble "structure" once here instead of repeatedly later.
 */
static const std::string part_location_structure( "structure" );
static const std::string part_location_under( "under" );
static const std::string part_location_center( "center" );
static const std::string part_location_onroof( "on_roof" );

static const itype_id fuel_type_animal( "animal" );
static const itype_id fuel_type_battery( "battery" );
static const itype_id fuel_type_muscle( "muscle" );
static const itype_id fuel_type_plutonium_cell( "plut_cell" );
static const itype_id fuel_type_wind( "wind" );

static const fault_id fault_belt( "fault_engine_belt_drive" );
static const fault_id fault_filter_air( "fault_engine_filter_air" );
static const fault_id fault_filter_fuel( "fault_engine_filter_fuel" );
static const fault_id fault_immobiliser( "fault_engine_immobiliser" );

static const activity_id ACT_VEHICLE( "ACT_VEHICLE" );

static const bionic_id bio_jointservo( "bio_jointservo" );

static const efftype_id effect_harnessed( "harnessed" );

static const itype_id itype_battery( "battery" );
static const itype_id itype_plut_cell( "plut_cell" );
static const itype_id itype_water( "water" );
static const itype_id itype_water_clean( "water_clean" );
static const itype_id itype_water_purifier( "water_purifier" );
static const vpart_id vp_door_lock( "door_lock" );

static const flag_id f_VEHICLE_UNLOCKED( "VEHICLE_UNLOCKED" );
static const flag_id f_VEHICLE_LOCKED( "VEHICLE_LOCKED" );
static const flag_id f_VEHICLE_NO_LOCKS( "VEHICLE_NO_LOCKS" );

static const flag_id f_VEHICLE_NO_HOTWIRE( "VEHICLE_NO_HOTWIRE" );
static const flag_id f_VEHICLE_HOTWIRE( "VEHICLE_HOTWIRE" );

static const std::string str_DOOR_LOCKING( "DOOR_LOCKING" );
static const std::string str_OPENCLOSE_INSIDE( "OPENCLOSE_INSIDE" );
static const std::string str_PERPETUAL( "PERPETUAL" );

static const std::vector<std::string> vs_NO_HOTWIRING = {
    "MUSCLE_LEGS",
    "MUSCLE_ARMS",
    "ANIMAL_CTRL",
};

static bool is_sm_tile_outside( const tripoint_abs_ms &pos );
static bool is_sm_tile_over_water( const tripoint_abs_ms &pos );

static const itype_id fuel_type_mana( "mana" );

// 1 kJ per battery charge
const int bat_energy_j = 1000;

// For reference what each function is supposed to do, see their implementation in
// @ref DefaultRemovePartHandler. Add compatible code for it into @ref MapgenRemovePartHandler,
// if needed.

static bool is_sm_tile_over_water( const tripoint_abs_ms &pos )
{
    const auto proj = project_remain<coords::sm>( pos );
    auto &mbuf = MAPBUFFER_REGISTRY.get( get_map().get_bound_dimension() );
    auto sm = mbuf.lookup_submap( proj.quotient_tripoint );
    if( sm == nullptr ) {
        return false;
    }
    return ( sm->get_ter( proj.remainder ).obj().has_flag( TFLAG_CURRENT ) ||
             sm->get_furn( proj.remainder ).obj().has_flag( TFLAG_CURRENT ) );
}

static bool is_sm_tile_outside( const tripoint_abs_ms &pos )
{
    const auto proj = project_remain<coords::sm>( pos );
    auto &m = get_map();
    auto &mbuf = MAPBUFFER_REGISTRY.get( m.get_bound_dimension() );
    auto sm = mbuf.lookup_submap( proj.quotient_tripoint );
    if( sm == nullptr ) {
        return false;
    }
    return m.is_outside( m.abs_to_bub( pos ) );
}

std::vector<vehicle_part *> vehicle::lights( bool active )
{
    std::vector<vehicle_part *> res;
    for( auto &e : parts ) {
        if( ( !active || e.enabled ) && e.is_available() && e.is_light() ) {
            res.push_back( &e );
        }
    }
    return res;
}

int vehicle::total_accessory_epower_w() const
{
    int epower = 0;
    for( const vpart_reference &vp : get_enabled_parts( VPFLAG_ENABLED_DRAINS_EPOWER ) ) {
        epower += vp.info().epower;
    }
    return epower;
}

int vehicle::total_alternator_epower_w() const
{
    int epower = 0;
    if( engine_on ) {
        // If the engine is on, the alternators are working.
        for( size_t p = 0; p < alternators.size(); ++p ) {
            if( is_alternator_on( p ) ) {
                epower += part_epower_w( alternators[p] );
            }
        }
    }
    return epower;
}

int vehicle::total_engine_epower_w() const
{
    int epower = 0;

    // Engines: can both produce (plasma) or consume (gas, diesel) epower.
    // Gas engines require epower to run for ignition system, ECU, etc.
    // Electric motor consumption not included, see @ref vpart_info::energy_consumption
    if( engine_on ) {
        for( size_t e = 0; e < engines.size(); ++e ) {
            if( is_engine_on( e ) ) {
                epower += part_epower_w( engines[e] );
            }
        }
    }

    return epower;
}

int vehicle::total_solar_epower_w() const
{
    int epower_w = 0;
    for( int part : solar_panels ) {
        if( parts[ part ].is_unavailable() ) {
            continue;
        }

        if( !is_sm_tile_outside( g->m.bub_to_abs( bub_part_location( part ) ) ) ) {
            continue;
        }

        epower_w += part_epower_w( part );
    }
    // Weather doesn't change much across the area of the vehicle, so just
    // sample it once.
    const weather_type_id &wtype = current_weather( abs_ms_location() );
    const float tick_sunlight = incident_sunlight( wtype, calendar::turn );
    double intensity = tick_sunlight / default_daylight_level();
    return epower_w * intensity;
}

int vehicle::total_wind_epower_w() const
{
    map &here = get_map();
    const oter_id &cur_om_ter = get_overmapbuffer( dimension_id_ ).ter( project_to<coords::omt>
                                ( abs_ms_location() ) );
    const weather_manager &weather = get_weather();
    int epower_w = 0;
    for( int part : wind_turbines ) {
        if( parts[ part ].is_unavailable() ) {
            continue;
        }

        if( !is_sm_tile_outside( here.bub_to_abs( bub_part_location( part ) ) ) ) {
            continue;
        }

        double windpower = get_local_windpower( weather.windspeed, cur_om_ter,
                                                here.bub_to_abs( bub_part_location( part ) ),
                                                weather.winddirection, false );
        if( windpower <= ( weather.windspeed / 10.0 ) ) {
            continue;
        }
        epower_w += part_epower_w( part ) * windpower;
    }
    return epower_w;
}

int vehicle::total_water_wheel_epower_w() const
{
    int epower_w = 0;
    for( int part : water_wheels ) {
        if( parts[ part ].is_unavailable() ) {
            continue;
        }

        if( !is_sm_tile_over_water( g->m.bub_to_abs( bub_part_location( part ) ) ) ) {
            continue;
        }

        epower_w += part_epower_w( part );
    }
    // TODO: river current intensity changes power - flat for now.
    return epower_w;
}

int vehicle::net_battery_charge_rate_w() const
{
    return total_engine_epower_w() + total_alternator_epower_w() + total_accessory_epower_w() +
    total_solar_epower_w() + total_wind_epower_w() + total_water_wheel_epower_w() +
    max_reactor_epower_w();
}

int vehicle::max_reactor_epower_w() const
{
    int epower_w = 0;
    for( int elem : reactors ) {
        epower_w += is_part_on( elem ) ? part_epower_w( elem ) : 0;
    }
    return epower_w;
}

void vehicle::update_alternator_load()
{
    // Update alternator load
    if( engine_on ) {
        int engine_vpower = 0;
        for( size_t e = 0; e < engines.size(); ++e ) {
            if( is_engine_on( e ) && parts[engines[e]].info().has_flag( "E_ALTERNATOR" ) ) {
                engine_vpower += part_vpower_w( engines[e] );
            }
        }
        int alternators_power = 0;
        for( size_t p = 0; p < alternators.size(); ++p ) {
            if( is_alternator_on( p ) ) {
                alternators_power += part_vpower_w( alternators[p] );
            }
        }
        alternator_load =
            engine_vpower
            ? 1000 * ( std::abs( alternators_power ) + std::abs( extra_drag ) ) / engine_vpower
            : 0;
    } else {
        alternator_load = 0;
    }
}

void vehicle::power_parts()
{
    update_alternator_load();
    // Things that drain energy: engines and accessories.
    int engine_epower = total_engine_epower_w();
    int epower = engine_epower + total_accessory_epower_w() + total_alternator_epower_w();

    int delta_energy_bat = power_to_energy_bat( epower, 1_turns );
    int storage_deficit_bat = std::max( 0, fuel_capacity( fuel_type_battery ) -
                                        fuel_left( fuel_type_battery ) - delta_energy_bat );
    // Reactors trigger only on demand. If we'd otherwise run out of power, see
    // if we can spin up the reactors.
    if( !reactors.empty() && storage_deficit_bat > 0 ) {
        // Still not enough surplus epower to fully charge battery
        // Produce additional epower from any reactors
        bool reactor_working = false;
        bool reactor_online = false;
        for( auto &elem : reactors ) {
            // Check whether the reactor is on. If not, move on.
            if( !is_part_on( elem ) ) {
                continue;
            }
            // Keep track whether or not the vehicle has any reactors activated
            reactor_online = true;
            // the amount of energy the reactor generates each turn
            const int gen_energy_bat = power_to_energy_bat( part_epower_w( elem ), 1_turns );
            if( parts[ elem ].is_unavailable() ) {
                continue;
            } else if( parts[ elem ].info().has_flag( str_PERPETUAL ) ) {
                reactor_working = true;
                delta_energy_bat += std::min( storage_deficit_bat, gen_energy_bat );
            } else if( parts[elem].ammo_remaining() > 0 ) {
                // Efficiency: one unit of fuel is this many units of battery
                // Note: One battery is 1 kJ
                const int efficiency = part_info( elem ).power;
                const int avail_fuel = parts[elem].ammo_remaining() * efficiency;
                const int elem_energy_bat = std::min( gen_energy_bat, avail_fuel );
                // Cap output at what we can achieve and utilize
                const int reactors_output_bat = std::min( elem_energy_bat, storage_deficit_bat );
                // Fuel consumed in actual units of the resource
                int fuel_consumed = reactors_output_bat / efficiency;
                // Remainder has a chance of resulting in more fuel consumption
                fuel_consumed += x_in_y( reactors_output_bat % efficiency, efficiency ) ? 1 : 0;
                parts[ elem ].ammo_consume( fuel_consumed, bub_part_location( elem ) );
                reactor_working = true;
                delta_energy_bat += reactors_output_bat;
            }
        }

        if( !reactor_working && reactor_online ) {
            // All reactors out of fuel or destroyed
            for( auto &elem : reactors ) {
                parts[ elem ].enabled = false;
            }
            if( player_in_control( g->u ) || g->u.sees( bub_ms_location() ) ) {
                add_msg( _( "The %s's reactor dies!" ), name );
            }
        }
    }

    int battery_deficit = 0;
    if( delta_energy_bat > 0 ) {
        // store epower surplus in battery
        charge_battery( delta_energy_bat );
    } else if( epower < 0 ) {
        // draw epower deficit from battery
        battery_deficit = discharge_battery( std::abs( delta_energy_bat ) );
    }

    if( battery_deficit != 0 ) {
        // Scoops need a special case since they consume power during actual use
        for( const vpart_reference &vp : get_enabled_parts( "SCOOP" ) ) {
            vp.part().enabled = false;
        }
        // Rechargers need special case since they consume power on demand
        for( const vpart_reference &vp : get_enabled_parts( "RECHARGE" ) ) {
            vp.part().enabled = false;
        }

        for( const vpart_reference &vp : get_enabled_parts( VPFLAG_ENABLED_DRAINS_EPOWER ) ) {
            vehicle_part &pt = vp.part();
            if( pt.info().epower < 0 ) {
                pt.enabled = false;
            }
        }

        is_alarm_on = false;
        camera_on = false;
        if( player_in_control( g->u ) || g->u.sees( bub_ms_location() ) ) {
            add_msg( _( "The %s's battery dies!" ), name );
        }
        if( engine_epower < 0 ) {
            // Not enough epower to run gas engine ignition system
            engine_on = false;
            if( ( player_in_control( g->u ) || g->u.sees( bub_ms_location() ) ) &&
                has_engine_type_not( fuel_type_muscle, true ) ) {
                add_msg( _( "The %s's engine dies!" ), name );
            }
        }
    }
}
