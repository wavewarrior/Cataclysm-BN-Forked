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

int vehicle::fuel_left( const int p, bool recurse ) const
{
    return fuel_left( parts[ p ].fuel_current(), recurse );
}

int vehicle::engine_fuel_left( const int e, bool recurse ) const
{
    if( static_cast<size_t>( e ) < engines.size() ) {
    return fuel_left( parts[ engines[ e ] ].fuel_current(), recurse );
    }
    return 0;
}

int vehicle::fuel_capacity( const itype_id &ftype ) const
{
    return std::accumulate( parts.begin(), parts.end(), 0, [&ftype]( const int &lhs,
    const vehicle_part & rhs ) {
        return lhs + ( rhs.ammo_current() == ftype ? rhs.ammo_capacity() : 0 );
    } );
}

int vehicle::drain( const itype_id &ftype, int amount )
{
    if( ftype == fuel_type_battery ) {
        // Batteries get special handling to take advantage of jumper
        // cables -- discharge_battery knows how to recurse properly
        // (including taking cable power loss into account).
        int remnant = discharge_battery( amount, true );

        // discharge_battery returns amount of charges that were not
        // found anywhere in the power network, whereas this function
        // returns amount of charges consumed; simple subtraction.
        return amount - remnant;
    }

    int drained = 0;
    for( auto &p : parts ) {
        if( amount <= 0 ) {
            break;
        }
        if( p.ammo_current() == ftype ) {
            int qty = p.ammo_consume( amount, bub_part_location( p ) );
            drained += qty;
            amount -= qty;
        }
    }

    invalidate_mass();
    return drained;
}

int vehicle::drain( const int index, int amount )
{
    if( index < 0 || index >= static_cast<int>( parts.size() ) ) {
        debugmsg( "Tried to drain an invalid part index: %d", index );
        return 0;
    }
    vehicle_part &pt = parts[index];
    if( pt.ammo_current() == fuel_type_battery ) {
        return drain( fuel_type_battery, amount );
    }
    if( !pt.is_tank() || !pt.ammo_remaining() ) {
        debugmsg( "Tried to drain something without any liquid: %s amount: %d ammo: %d",
                  pt.name(), amount, pt.ammo_remaining() );
        return 0;
    }

    const int drained = pt.ammo_consume( amount, bub_part_location( pt ) );
    invalidate_mass();
    return drained;
}

int vehicle::basic_consumption( const itype_id &ftype ) const
{
    int fcon = 0;
    for( size_t e = 0; e < engines.size(); ++e ) {
        if( is_engine_type_on( e, ftype ) ) {
            if( parts[ engines[e] ].ammo_current() == fuel_type_battery &&
                part_epower_w( engines[e] ) >= 0 ) {
                // Electric engine - use epower instead
                fcon -= part_epower_w( engines[e] );

            } else if( !is_perpetual_type( e ) ) {
                fcon += part_vpower_w( engines[e] );
                if( parts[ e ].faults().contains( fault_filter_air ) ) {
                    fcon *= 2;
                }
            }
        }
    }
    return fcon;
}

int vehicle::consumption_per_hour( const itype_id &ftype, int fuel_rate_w ) const
{
    item &fuel = *item::spawn_temporary( ftype );
    if( fuel_rate_w == 0 || fuel.has_flag( flag_PERPETUAL ) || !engine_on ) {
        return 0;
    }

    float j_per_turn = fuel_rate_w;
    float j_per_second = j_per_turn / to_seconds<float>( time_duration::from_turns( 1 ) );
    float kj_per_hour = j_per_second * 3.6f;
    float kj_per_mL = fuel.fuel_energy();

    return kj_per_hour / kj_per_mL;
}

int vehicle::ideal_engine_power( bool safe ) const
{
    int pwr = 0;
    int cnt = 0;

    for( size_t e = 0; e < engines.size(); e++ ) {
        if( is_engine_on( e ) ) {
            int p = engines[e];
            int m2c = safe ? part_info( engines[e] ).engine_m2c() : 100;
            if( parts[ engines[e] ].faults().contains( fault_filter_fuel ) ) {
                m2c *= 0.6;
            }
            pwr += part_vpower_w( p ) * m2c / 100;
            cnt += static_cast<int>( !part_info( p ).has_flag( "E_NO_POWER_DECAY" ) );
        }
    }

    for( size_t a = 0; a < alternators.size(); a++ ) {
        int p = alternators[a];
        if( is_alternator_on( a ) ) {
            pwr += part_vpower_w( p ); // alternators have negative power
        }
    }

    pwr = std::max( 0, pwr );

    if( cnt > 1 ) {
        pwr = pwr * 4 / ( 4 + cnt - 1 );
    }
    return pwr;
}

int vehicle::total_power_w( const bool fueled, const bool safe ) const
{
    int pwr = 0;
    int cnt = 0;

    for( size_t e = 0; e < engines.size(); e++ ) {
        int p = engines[e];
        if( engine_on && is_engine_on( e ) && ( !fueled || engine_fuel_left( e ) ) ) {
            int m2c = safe ? part_info( engines[e] ).engine_m2c() : 100;
            if( parts[ engines[e] ].faults().contains( fault_filter_fuel ) ) {
                m2c *= 0.6;
            }
            pwr += part_vpower_w( p ) * m2c / 100;
            cnt += static_cast<int>( !part_info( p ).has_flag( "E_NO_POWER_DECAY" ) );
        }
    }

    for( size_t a = 0; a < alternators.size(); a++ ) {
        int p = alternators[a];
        if( is_alternator_on( a ) ) {
            pwr += part_vpower_w( p ); // alternators have negative power
        }
    }

    pwr = std::max( 0, pwr );

    if( cnt > 1 ) {
        pwr = pwr * 4 / ( 4 + cnt - 1 );
    }
    return pwr;
}

bool vehicle::is_moving() const
{
    return velocity != 0;
}

bool vehicle::can_use_rails() const
{
    // Do not allow vehicles without rail wheels or with mixed wheels
    return !rail_wheelcache.empty() && wheelcache.size() == rail_wheelcache.size();
}

int vehicle::ground_acceleration( const bool fueled, int at_vel_in_vmi, const bool ideal ) const
{
    if( !( engine_on || skidding ) ) {
    return 0;
}
int target_cmps = std::max( at_vel_in_vmi, std::max( 447,
                            max_velocity( fueled ) / 4 ) );
    double weight = to_kilogram( total_mass() );
    if( is_towing() ) {
    vehicle *other_veh = tow_data.get_towed();
        if( other_veh ) {
            weight = weight + to_kilogram( other_veh->total_mass() );
        }
    }
    int engine_power_ratio = ( ideal ? ideal_engine_power() : total_power_w( fueled ) ) / weight;
    int accel_at_vel = 100 * 100 * engine_power_ratio / target_cmps;
    add_msg( m_debug, "%s: accel at %d cm/s is %d cm/s", name, target_cmps,
             accel_at_vel );
    return accel_at_vel;
}

int vehicle::aircraft_acceleration( const bool fueled, int at_vel_in_vmi, const bool ideal ) const
{
    ( void )at_vel_in_vmi;
    if( !( engine_on || is_flying ) ) {
    return 0;
}
const double thrust = total_thrust( fueled, ideal );
if( thrust == 0 ) {
    return 0;
}
const int accel_at_vel = 100 * total_thrust( fueled, ideal ) / to_kilogram( total_mass() );
return accel_at_vel;
}

int vehicle::water_acceleration( const bool fueled, int at_vel_in_vmi, const bool ideal ) const
{
    if( !( engine_on || skidding ) ) {
    return 0;
}
int target_cmps = std::max( at_vel_in_vmi, std::max( 447,
                            max_water_velocity( fueled ) / 4 ) );
    double weight = to_kilogram( total_mass() );
    if( is_towing() ) {
    vehicle *other_veh = tow_data.get_towed();
        if( other_veh ) {
            weight = weight + to_kilogram( other_veh->total_mass() );
        }
    }
    int engine_power_ratio = ( ideal ? ideal_engine_power() : total_power_w( fueled ) ) / weight;
    int accel_at_vel = 100 * 100 * engine_power_ratio / target_cmps;
    add_msg( m_debug, "%s: water accel at %d cm/s is %d cm/s", name, target_cmps,
             accel_at_vel );
    return accel_at_vel;
}

// cubic equation solution
// don't use complex numbers unless necessary and it's usually not
// see https://math.vanderbilt.edu/schectex/courses/cubic/ for the gory details
static double simple_cubic_solution( double a, double b, double c, double d )
{
    double p = -b / ( 3 * a );
    double q = p * p * p + ( b * c - 3 * a * d ) / ( 6 * a * a );
    double r = c / ( 3 * a );
    double t = r - p * p;
    double tricky_bit = q * q + t * t * t;
    if( tricky_bit < 0 ) {
        double cr = 1.0 / 3.0; // approximate the cube root of a complex number
        std::complex<double> q_complex( q );
        std::complex<double> tricky_complex( std::sqrt( std::complex<double>( tricky_bit ) ) );
        std::complex<double> term1( std::pow( q_complex + tricky_complex, cr ) );
        std::complex<double> term2( std::pow( q_complex - tricky_complex, cr ) );
        std::complex<double> term_sum( term1 + term2 );

        if( imag( term_sum ) < 2 ) {
            return p + real( term_sum );
        } else {
            debugmsg( "cubic solution returned imaginary values" );
            return 0;
        }
    } else {
        double tricky_final = std::sqrt( tricky_bit );
        double term1_part = q + tricky_final;
        double term2_part = q - tricky_final;
        double term1 = std::cbrt( term1_part );
        double term2 = std::cbrt( term2_part );
        return p + term1 + term2;
    }
}

int vehicle::acceleration( const bool fueled, int at_vel_in_vmi ) const
{
    if( is_watercraft() ) {
    return water_acceleration( fueled, at_vel_in_vmi );
    } else if( is_aircraft() && is_flying ) {
    return aircraft_acceleration( fueled, at_vel_in_vmi );
    }
    return ground_acceleration( fueled, at_vel_in_vmi );
}

int vehicle::current_acceleration( const bool fueled ) const
{
    return acceleration( fueled, std::abs( velocity ) );
}

// Ugly physics below:
// maximum speed occurs when all available thrust is used to overcome air/rolling resistance
// sigma F = 0 as we were taught in Engineering Mechanics 301
// engine power is torque * rotation rate (in rads for simplicity)
// torque / wheel radius = drive force at where the wheel meets the road
// velocity is wheel radius * rotation rate (in rads for simplicity)
// air resistance is -1/2 * air density * drag coeff * cross area * v^2
//        and c_air_drag is -1/2 * air density * drag coeff * cross area
// rolling resistance is mass * GRAVITY_OF_EARTH * rolling coeff * 0.000225 * ( 33.3 + v )
//        and c_rolling_drag is mass * GRAVITY_OF_EARTH * rolling coeff * 0.000225
//        and rolling_constant_to_variable is 33.3
// or by formula:
// max velocity occurs when F_drag = F_wheel
// F_wheel = engine_power / rotation_rate / wheel_radius
// velocity = rotation_rate * wheel_radius
// F_wheel * velocity = engine_power * rotation_rate * wheel_radius / rotation_rate / wheel_radius
// F_wheel * velocity = engine_power
// F_wheel = engine_power / velocity
// F_drag = F_air_drag + F_rolling_drag
// F_air_drag = c_air_drag * velocity^2
// F_rolling_drag = c_rolling_drag * velocity + rolling_constant_to_variable * c_rolling_drag
// engine_power / v = c_air_drag * v^2 + c_rolling_drag * v + 33 * c_rolling_drag
// c_air_drag * v^3 + c_rolling_drag * v^2 + c_rolling_drag * 33.3 * v - engine power = 0
// solve for v with the simplified cubic equation solver
// got it? quiz on Wednesday.
int vehicle::max_ground_velocity( const bool fueled, const bool ideal ) const
{
    int total_engine_w = ( ideal ? ideal_engine_power() : total_power_w( fueled ) );
    double c_rolling_drag = coeff_rolling_drag();
    double max_in_mps = simple_cubic_solution( coeff_air_drag(), c_rolling_drag,
                        c_rolling_drag * vehicles::rolling_constant_to_variable,
                        -total_engine_w );
    add_msg( m_debug, "%s: power %d, c_air %3.2f, c_rolling %3.2f, max_in_mps %3.2f",
             name, total_engine_w, coeff_air_drag(), c_rolling_drag, max_in_mps );
    return mps_to_cmps( max_in_mps );
}

// the same physics as ground velocity, but there's no rolling resistance so the math is easy
// F_drag = F_water_drag + F_air_drag
// F_drag = c_water_drag * velocity^2 + c_air_drag * velocity^2
// F_drag = ( c_water_drag + c_air_drag ) * velocity^2
// F_prop = engine_power / velocity
// F_prop = F_drag
// engine_power / velocity = ( c_water_drag + c_air_drag ) * velocity^2
// engine_power = ( c_water_drag + c_air_drag ) * velocity^3
// velocity^3 = engine_power / ( c_water_drag + c_air_drag )
// velocity = cube root( engine_power / ( c_water_drag + c_air_drag ) )
int vehicle::max_water_velocity( const bool fueled, const bool ideal ) const
{
    int total_engine_w = ( ideal ? ideal_engine_power() : total_power_w( fueled ) );
    double total_drag = coeff_water_drag() + coeff_air_drag();
    double max_in_mps = std::cbrt( total_engine_w / total_drag );
    add_msg( m_debug, "%s: power %d, c_air %3.2f, c_water %3.2f, water max_in_mps %3.2f",
             name, total_engine_w, coeff_air_drag(), coeff_water_drag(), max_in_mps );
    return mps_to_cmps( max_in_mps );
}

int vehicle::max_air_velocity( const bool fueled, const bool ideal ) const
{
    const double max_air_mps = std::sqrt( total_thrust( fueled, false, ideal ) / coeff_air_drag() );
    // fly fast at your own risk
    return mps_to_cmps( max_air_mps );
}

int vehicle::max_velocity( const bool fueled, const bool ideal ) const
{
    if( is_flying && is_aircraft() ) {
    return max_air_velocity( fueled, ideal );
    } else if( is_watercraft() ) {
    return max_water_velocity( fueled, ideal );
    } else {
        return max_ground_velocity( fueled, ideal );
    }
}

int vehicle::max_reverse_velocity( const bool fueled, const bool ideal ) const
{
    int max_vel = max_velocity( fueled, ideal );
    if( has_engine_type( fuel_type_battery, true ) ) {
        // Electric motors can go in reverse as well as forward
        return -max_vel;
    } else {
        // All other motive powers do poorly in reverse
        return -max_vel / 4;
    }
}

// the same physics as max_ground_velocity, but with a smaller engine power
int vehicle::safe_ground_velocity( const bool fueled, const bool ideal ) const
{
    for( size_t e = 0; e < parts.size(); e++ ) {
        const vehicle_part &vp = parts[ e ];
        int animal_vel = 0;
        if( vp.info().fuel_type == fuel_type_animal && engines.size() != 1 ) {
            monster *mon = get_pet( e );
            if( mon != nullptr && mon->has_effect( effect_harnessed ) ) {
                int animal_vel_cur = mon->get_speed() * 12;
                if( animal_vel > 0 ) {
                    animal_vel = std::min( animal_vel, animal_vel_cur );
                } else {
                    animal_vel = animal_vel_cur;
                }
            }
        }
        // Cap safe speed at the point where the slowest animal found would start to damage their yoke
        // If damage or weight has pulled max speed lower than this, cap at that instead.
        if( animal_vel > 0 ) {
            return std::min( animal_vel, max_ground_velocity( fueled ) );
        }
    }
    int effective_engine_w = ( ideal ? ideal_engine_power( true ) : total_power_w( fueled, true ) );
    double c_rolling_drag = coeff_rolling_drag();
    double safe_in_mps = simple_cubic_solution( coeff_air_drag(), c_rolling_drag,
                         c_rolling_drag * vehicles::rolling_constant_to_variable,
                         -effective_engine_w );
    return mps_to_cmps( safe_in_mps );
}

// TODO: Consider some kind of dynamic pressure based safe velocity
// or something simpler maybe
int vehicle::safe_aircraft_velocity( const bool fueled, const bool ideal ) const
{
    const double max_air_mps = std::sqrt( total_thrust( fueled,
                                          true, ideal ) / coeff_air_drag() );
    return mps_to_cmps( max_air_mps );
}

// the same physics as max_water_velocity, but with a smaller engine power
int vehicle::safe_water_velocity( const bool fueled, const bool ideal ) const
{
    int total_engine_w = ( ideal ? ideal_engine_power( true ) : total_power_w( fueled, true ) );
    double total_drag = coeff_water_drag() + coeff_air_drag();
    double safe_in_mps = std::cbrt( total_engine_w / total_drag );
    return mps_to_cmps( safe_in_mps );
}

int vehicle::safe_velocity( const bool fueled ) const
{
    if( is_flying && is_aircraft() ) {
    return safe_aircraft_velocity( fueled );
    } else if( is_watercraft() ) {
    return safe_water_velocity( fueled );
    } else {
        return safe_ground_velocity( fueled );
    }
}

bool vehicle::do_environmental_effects()
{
    bool needed = false;
    // check for smoking parts
    for( const vpart_reference &vp : get_all_parts() ) {
        /* Only lower blood level if:
         * - The part is outside.
         * - The weather is any effect that would cause the player to be wet. */
        if( vp.part().blood > 0 && g->m.is_outside( vp.pos() ) ) {
            needed = true;
            if( get_weather().weather_id->rains &&
                get_weather().weather_id->precip != precip_class::very_light ) {
                vp.part().blood--;
            }
        }
    }
    return needed;
}

void vehicle::spew_field( double joules, int part, field_type_id type, int intensity )
{
    if( rng( 1, 10000 ) > joules ) {
        return;
    }
    auto p = parts[part].mount;
    intensity = std::max( joules / 10000, static_cast<double>( intensity ) );
    // Move back from engine/muffler until we find an open space
    while( relative_parts.contains( p ) ) {
        p.x() += ( velocity < 0 ? 1 : -1 );
    }
    auto q = coord_translate( p );
    const auto dest = bub_ms_location() + q;
    g->m.mod_field_intensity( dest, type, intensity );
}

/**
 * Generate noise or smoke from a vehicle with engines turned on
 * load = how hard the engines are working, from 0.0 until 1.0
 * time = how many seconds to generated smoke for
 */
void vehicle::noise_and_smoke( int load, time_duration time )
{
    static const std::array<std::pair<std::string, int>, 8> sounds = { {
            { translate_marker( "hmm" ), 0 }, { translate_marker( "hummm!" ), 15 },
            { translate_marker( "whirrr!" ), 30 }, { translate_marker( "vroom!" ), 60 },
            { translate_marker( "roarrr!" ), 100 }, { translate_marker( "ROARRR!" ), 140 },
            { translate_marker( "BRRROARRR!" ), 180 }, { translate_marker( "BRUMBRUMBRUMBRUM!" ), INT_MAX }
        }
    };
    const std::string heli_noise = translate_marker( "WUMPWUMPWUMP!" );
    double noise = 0.0;
    double mufflesmoke = 0.0;
    double muffle = 1.0;
    double m = 0.0;
    int exhaust_part = -1;
    for( const vpart_reference &vp : get_avail_parts( "MUFFLER" ) ) {
        m = 1.0 - ( 1.0 - vp.info().bonus / 100.0 ) * vp.part().health_percent();
        if( m < muffle ) {
            muffle = m;
            exhaust_part = static_cast<int>( vp.part_index() );
        }
    }

    bool bad_filter = false;
    bool combustion = false;

    for( size_t e = 0; e < engines.size(); e++ ) {
        int p = engines[e];
        if( is_engine_on( e ) &&  engine_fuel_left( e ) ) {
            // convert current engine load to units of watts/40K
            // then spew more smoke and make more noise as the engine load increases
            int part_watts = part_vpower_w( p, true );
            double max_stress = static_cast<double>( part_watts / 40000.0 );
            double cur_stress = load / 1000.0 * max_stress;
            // idle stress = 1.0 resulting in nominal working engine noise = engine_noise_factor()
            // and preventing noise = 0
            cur_stress = std::max( cur_stress, 1.0 );
            double part_noise = cur_stress * part_info( p ).engine_noise_factor();

            if( part_info( p ).has_flag( "E_COMBUSTION" ) ) {
                combustion = true;
                double health = parts[p].health_percent();
                if( parts[ p ].base->faults.contains( fault_filter_fuel ) ) {
                    health = 0.0;
                }
                if( health < part_info( p ).engine_backfire_threshold() && one_in( 50 + 150 * health ) ) {
                    backfire( e );
                }
                double j = cur_stress * to_turns<int>( time ) * muffle * 1000;

                if( parts[ p ].base->faults.contains( fault_filter_air ) ) {
                    bad_filter = true;
                    j *= j;
                }

                if( ( exhaust_part == -1 ) && engine_on ) {
                    spew_field( j, p, fd_smoke, bad_filter ? fd_smoke.obj().get_max_intensity() : 1 );
                } else {
                    mufflesmoke += j;
                }
                part_noise = ( part_noise + max_stress * 3 + 5 ) * muffle;
            }
            noise = std::max( noise, part_noise ); // Only the loudest engine counts.
        }
    }
    if( !combustion ) {
        return;
    }
    /// TODO: handle other engine types: muscle / animal / wind / coal / ...

    if( exhaust_part != -1 && engine_on ) {
        spew_field( mufflesmoke, exhaust_part, fd_smoke,
                    bad_filter ? fd_smoke.obj().get_max_intensity() : 1 );
    }
    if( is_flying && has_part( VPFLAG_ROTOR ) ) {
        noise *= 2;
    }
    // Cap engine noise to avoid deafening.
    noise = std::min( noise, 100.0 );
    // Even a vehicle with engines off will make noise traveling at high speeds
    noise = std::max( noise, std::fabs( velocity / 224.0 ) );
    int lvl = 0;
    if( one_in( 4 ) && rng( 0, 30 ) < noise ) {
        while( noise > sounds[lvl].second ) {
            lvl++;
        }
    }
    add_msg( m_debug, "VEH NOISE final: %d", static_cast<int>( noise ) );
    vehicle_noise = static_cast<unsigned char>( noise );
    // TODO: other noises for non-rotor aircraft?
    sounds::sound( bub_ms_location(), noise, sounds::sound_t::movement,
                   _( has_part( VPFLAG_ROTOR ) ? heli_noise : sounds[lvl].first ), true );
}

int vehicle::wheel_area() const
{
    int total_area = 0;
    for( const int &wheel_index : wheelcache ) {
        total_area += parts[ wheel_index ].wheel_area();
    }

    return total_area;
}

float vehicle::average_or_rating() const
{
    if( wheelcache.empty() ) {
    return 0.0f;
}
float total_rating = 0;
for( const int &wheel_index : wheelcache ) {
    total_rating += part_info( wheel_index ).wheel_or_rating();
    }
    return total_rating / wheelcache.size();
}

static double tile_to_width( int tiles )
{
    if( tiles < 1 ) {
        return 0.1;
    } else if( tiles < 6 ) {
        return 0.5 + 0.4 * tiles;
    } else {
        return 2.5 + 0.15 * ( tiles - 5 );
    }
}

static constexpr int minrow = -122;
static constexpr int maxrow = 122;
struct drag_column {
    int pro = minrow;
    int hboard = minrow;
    int fboard = minrow;
    int aisle = minrow;
    int seat = minrow;
    int exposed = minrow;
    int roof = minrow;
    int shield = minrow;
    int turret = minrow;
    int panel = minrow;
    int windmill = minrow;
    int sail = minrow;
    int rotor = minrow;
    int ballon = minrow;
    int last = maxrow;
};

double vehicle::coeff_air_drag() const
{
    if( !coeff_air_dirty ) {
    return coefficient_air_resistance;
}
constexpr double c_air_base = 0.25;
constexpr double c_air_mod = 0.1;
constexpr double base_height = 1.4;
constexpr double aisle_height = 0.6;
constexpr double fullboard_height = 0.5;
constexpr double roof_height = 0.1;
constexpr double windmill_height = 0.7;
constexpr double sail_height = 0.8;
constexpr double rotor_height = 0.6;

std::vector<int> structure_indices = all_parts_at_location( part_location_structure );
int width = mount_max.y() - mount_min.y() + 1;

// a mess of lambdas to make the next bit slightly easier to read
const auto d_exposed = [&]( const vehicle_part & p ) {
    // if it's not inside, it's a center location, and it doesn't need a roof, it's exposed
    if( p.info().location != part_location_center ) {
            return false;
        }
        return !( p.inside || p.info().has_flag( "EXTENDABLE" ) ||
                  p.info().has_flag( "NO_ROOF_NEEDED" ) ||
                  p.info().has_flag( "WINDSHIELD" ) ||
                  p.info().has_flag( "OPENABLE" ) );
    };

    const auto d_protrusion = [&]( std::vector<int> parts_at ) {
        if( parts_at.size() > 1 ) {
            return false;
        } else {
            return parts[ parts_at.front() ].info().has_flag( "PROTRUSION" );
        }
    };
    const auto d_check_min = [&]( int &value, const vehicle_part & p, bool test ) {
        value = std::min( value, test ? p.mount.x() - mount_min.x() : maxrow );
    };
    const auto d_check_max = [&]( int &value, const vehicle_part & p, bool test ) {
        value = std::max( value, test ? p.mount.x() - mount_min.x() : minrow );
    };

    // raycast down each column. the least drag vehicle has halfboard, windshield, seat with roof,
    // windshield, halfboard and is twice as long as it is wide.
    // find the first instance of each item and compare against the ideal configuration.
    std::vector<drag_column> drag( width );
for( int p : structure_indices ) {
    if( parts[ p ].removed ) {
            continue;
        }
        int col = parts[ p ].mount.y() - mount_min.y();
        std::vector<int> parts_at = parts_at_relative( parts[ p ].mount, true );
        d_check_min( drag[ col ].pro, parts[ p ], d_protrusion( parts_at ) );
        for( int pa_index : parts_at ) {
            const vehicle_part &pa = parts[ pa_index ];
            d_check_max( drag[ col ].hboard, pa, pa.info().has_flag( "HALF_BOARD" ) );
            d_check_max( drag[ col ].fboard, pa, pa.info().has_flag( "FULL_BOARD" ) );
            d_check_max( drag[ col ].aisle, pa, pa.info().has_flag( "AISLE" ) );
            d_check_max( drag[ col ].shield, pa, pa.info().has_flag( "WINDSHIELD" ) &&
                         pa.is_available() );
            d_check_max( drag[ col ].seat, pa, pa.info().has_flag( "SEAT" ) ||
                         pa.info().has_flag( "BED" ) );
            d_check_max( drag[ col ].turret, pa, pa.info().location == part_location_onroof &&
                         !pa.info().has_flag( "SOLAR_PANEL" ) && !pa.info().has_flag( "WING" ) );
            d_check_max( drag[ col ].roof, pa, pa.info().has_flag( "ROOF" ) );
            d_check_max( drag[ col ].panel, pa, pa.info().has_flag( "SOLAR_PANEL" ) );
            d_check_max( drag[ col ].windmill, pa, pa.info().has_flag( "WIND_TURBINE" ) );
            d_check_max( drag[ col ].rotor, pa, pa.info().has_flag( "ROTOR" ) );
            d_check_max( drag[ col ].sail, pa, pa.info().has_flag( "WIND_POWERED" ) );
            d_check_max( drag[ col ].exposed, pa, d_exposed( pa ) );
            d_check_min( drag[ col ].last, pa, pa.info().has_flag( "LOW_FINAL_AIR_DRAG" ) ||
                         pa.info().has_flag( "HALF_BOARD" ) );
        }
    }
    double height = 0;
    double c_air_drag = 0;
    // tally the results of each row and prorate them relative to vehicle width
for( drag_column &dc : drag ) {
    // even as m_debug you rarely want to see this
    // add_msg( m_debug, "veh %: pro %d, hboard %d, fboard %d, shield %d, seat %d, roof %d, aisle %d, turret %d, panel %d, exposed %d, last %d\n", name, dc.pro, dc.hboard, dc.fboard, dc.shield, dc.seat, dc.roof, dc.aisle, dc.turret, dc.panel, dc.exposed, dc.last );

    double c_air_drag_c = c_air_base;
    // rams in front of the vehicle mildly worsens air drag
    c_air_drag_c += ( dc.pro > dc.hboard ) ? c_air_mod : 0;
        // not having halfboards in front of any windshields or fullboards moderately worsens
        // air drag
        c_air_drag_c += ( std::max( std::max( dc.hboard, dc.fboard ),
                                    dc.shield ) != dc.hboard ) ? 2 * c_air_mod : 0;
        // not having windshields in front of seats severely worsens air drag
        c_air_drag_c += ( dc.shield < dc.seat ) ? 3 * c_air_mod : 0;
        // missing roofs and open doors severely worsen air drag
        c_air_drag_c += ( dc.exposed > minrow ) ? 3 * c_air_mod : 0;
        // being twice as long as wide mildly reduces air drag
        c_air_drag_c -= ( 2 * ( mount_max.x() - mount_min.x() ) > width ) ? c_air_mod : 0;
        // trunk doors and halfboards at the tail mildly reduce air drag
        c_air_drag_c -= ( dc.last == mount_min.x() ) ? c_air_mod : 0;
        // turrets severely worsen air drag
        c_air_drag_c += ( dc.turret > minrow ) ? 3 * c_air_mod : 0;
        // having a windmill is terrible for your drag
        c_air_drag_c += ( dc.windmill > minrow ) ? 5 * c_air_mod : 0;
        // rotors are not great for drag!
        c_air_drag_c += ( dc.rotor > minrow ) ? 6 * c_air_mod : 0;
        // having a sail is terrible for your drag
        c_air_drag_c += ( dc.sail > minrow ) ? 7 * c_air_mod : 0;
        c_air_drag += c_air_drag_c;
        // vehicles are 1.4m tall
        double c_height = base_height;
        // plus a bit for a roof
        c_height += ( dc.roof > minrow ) ? roof_height : 0;
        // plus a lot for an aisle
        c_height += ( dc.aisle > minrow ) ?  aisle_height : 0;
        // or fullboards
        c_height += ( dc.fboard > minrow ) ? fullboard_height : 0;
        // and a little for anything on the roof
        c_height += ( dc.turret > minrow ) ? 2 * roof_height : 0;
        // solar panels are better than turrets or floodlights, though
        c_height += ( dc.panel > minrow ) ? roof_height : 0;
        // windmills are tall, too
        c_height += ( dc.windmill > minrow ) ? windmill_height : 0;
        c_height += ( dc.rotor > minrow ) ? rotor_height : 0;
        // sails are tall, too
        c_height += ( dc.sail > minrow ) ? sail_height : 0;
        height += c_height;
    }
    constexpr double air_density = 1.29; // kg/m^3
    // prorate per row height and c_air_drag
    height /= width;
    c_air_drag /= width;
    double cross_area = height * tile_to_width( width );
    add_msg( m_debug, "%s: height %3.2fm, width %3.2fm (%d tiles), c_air %3.2f\n", name, height,
             tile_to_width( width ), width, c_air_drag );
    if( !balloons.empty() ) {
        c_air_drag += coeff_balloon_drag();
    }
    // F_air_drag = c_air_drag * cross_area * 1/2 * air_density * v^2
    // coeff_air_resistance = c_air_drag * cross_area * 1/2 * air_density
    coefficient_air_resistance = std::max( 0.1, c_air_drag * cross_area * 0.5 * air_density );
    coeff_air_dirty = false;
    return coefficient_air_resistance;
}

double vehicle::coeff_balloon_drag() const
{
    double volume = std::accumulate( balloons.begin(), balloons.end(), double{0.0},
    [&]( double acc, int balloon ) {
        const double height{ parts[ balloon ].info().balloon_height() };
        return acc + height;
    } );
    return std::pow( volume, 2 / 3 );
}

double vehicle::coeff_rolling_drag() const
{
    if( !coeff_rolling_dirty ) {
    return coefficient_rolling_resistance;
}
constexpr double wheel_ratio = 1.25;
constexpr double base_wheels = 4.0;
// SAE J2452 measurements are in F_rr = N * C_rr * 0.000225 * ( v + 33.33 )
// Don't ask me why, but it's the numbers we have. We want N * C_rr * 0.000225 here,
// and N is mass * accel from gravity (aka weight)
constexpr double sae_ratio = 0.000225;
constexpr double newton_ratio = GRAVITY_OF_EARTH * sae_ratio;
double wheel_factor = 0;
if( wheelcache.empty() ) {
        wheel_factor = 50;
    } else {
        // should really sum the each wheel's c_rolling_resistance * it's share of vehicle mass
        for( auto wheel : wheelcache ) {
            wheel_factor += parts[ wheel ].info().wheel_rolling_resistance();
        }
        // mildly increasing rolling resistance for vehicles with more than 4 wheels and mildly
        // decrease it for vehicles with less
        wheel_factor *= wheel_ratio /
                        ( base_wheels * wheel_ratio - base_wheels + wheelcache.size() );
    }
    coefficient_rolling_resistance = newton_ratio * wheel_factor * to_kilogram(
                                         total_mass() ) * get_lift_percent( true );
    coefficient_rolling_resistance = std::max( coefficient_rolling_resistance, 0.0 );
    coeff_rolling_dirty = false;
    return coefficient_rolling_resistance;
}

double vehicle::water_hull_height() const
{
    if( coeff_water_dirty ) {
    coeff_water_drag();
    }
    return hull_height;
}

double vehicle::water_draft() const
{
    if( coeff_water_dirty ) {
    coeff_water_drag();
    }
    return draft_m;
}

bool vehicle::can_float() const
{
    if( coeff_water_dirty ) {
    coeff_water_drag();
    }
    int float_force = max_buoyancy() + total_balloon_lift();
    return to_newton( total_mass() ) <= float_force;
}


double vehicle::total_rotor_area() const
{
    return std::accumulate( rotors.begin(), rotors.end(), 0.0,
    [&]( double acc, int rotor ) {
        const double radius{ parts[ rotor ].info().rotor_diameter() / 2.0 };
        return acc + M_PI * std::pow( radius, 2 );
    } );
}

double vehicle::total_propeller_area() const
{
    return std::accumulate( propellers.begin(), propellers.end(), 0.0,
    [&]( double acc, int propeller ) {
        const double radius{ parts[ propeller ].info().propeller_diameter() / 2.0 };
        return acc + M_PI * std::pow( radius, 2 );
    } );
}

// Balloons can lift ~1 kg per m^3
// 1 tile is 1 m^2, but with an undefined height
// So balloon height and balloon weight will be what changes
// Returns a value in newtons
double vehicle::total_balloon_lift() const
{
    return GRAVITY_OF_EARTH * std::accumulate( balloons.begin(), balloons.end(), double{0.0},
    [&]( double acc, int balloon ) {
        const double height{ parts[ balloon ].info().balloon_height() };
        return acc + height;
    } );
}

// Wing Lift
// Based on air being 1kg/m^3
double vehicle::total_wing_lift() const
{
    const double meterpersec = cmps_to_mps( velocity );
    const double meterpersecsquared = std::pow( meterpersec, 2 );
    return meterpersecsquared * std::accumulate( wings.begin(), wings.end(), double{0.0},
    [&]( double acc, int wing ) {
        const double liftcoff{ parts[ wing ].info().lift_coff() };
        // m^2 area is always 1
        return acc + ( 0.5 * liftcoff );
    } );
}

// constants were converted from imperial to SI goodness
// returns as newton
double vehicle::thrust_of_rotorcraft( const bool fuelled, const bool safe, const bool ideal ) const
{
    constexpr double coefficient = 0.8642;
    constexpr double exponentiation = -0.3107;

    const double rotor_area = total_rotor_area();
    if( rotor_area == 0 ) {
        return 0;
    }
    // take off 15 % due to the imaginary tail rotor power?
    const int engine_power = ( ideal ? ideal_engine_power( safe ) : total_power_w( fuelled, safe ) );
    if( engine_power <= 0 ) {
        return 0;
    }
    const double power_load = engine_power / rotor_area;
    const double lift_thrust = coefficient * engine_power * std::pow( power_load, exponentiation );
    add_msg( m_debug, "lift thrust(N) of %s: %f, rotor area (m^2): %f, engine power (w): %i",
             name, lift_thrust, rotor_area, engine_power );
    return lift_thrust;
}

// constants were converted from imperial to SI goodness
// returns as newton
double vehicle::foward_thrust_of_propellers( const bool fuelled, const bool safe,
        const bool ideal ) const
{
    constexpr double coefficient = 0.8642;
    constexpr double exponentiation = -0.3107;

    const double rotor_area = total_propeller_area();
    if( rotor_area == 0 ) {
        return 0;
    }
    // take off 15 % due to the imaginary tail rotor power?
    const int engine_power = ( ideal ? ideal_engine_power( safe ) : total_power_w( fuelled, safe ) );

    const double power_load = engine_power / rotor_area;
    const double foward_thrust = coefficient * engine_power * std::pow( power_load, exponentiation );
    add_msg( m_debug, "foward thrust(N) of %s: %f, propeller area (m^2): %f, engine power (w): %i",
             name, foward_thrust, rotor_area, engine_power );
    return foward_thrust;
}

// get sum of horizontal thrust from all lifting parts
double vehicle::total_thrust( const bool fuelled, const bool safe, const bool ideal ) const
{
    return thrust_of_rotorcraft( fuelled, safe, ideal ) + foward_thrust_of_propellers( fuelled, safe,
    ideal );
}

// get sum of lift from all lifting parts
double vehicle::total_lift( const bool fuelled, const bool safe, const bool ideal,
                            const bool unpowered, const bool idle ) const
{
    if( idle ) {
    return total_balloon_lift();
    }
    if( unpowered ) {
    return total_balloon_lift() + total_wing_lift();
    } else {
        return thrust_of_rotorcraft( fuelled, safe, ideal ) + total_balloon_lift() + total_wing_lift();
    }
}

int vehicle::get_takeoff_speed( std::string speed_type ) const
{
    const int needed_force = to_newton( total_mass() ) - thrust_of_rotorcraft( true, false, true ) -
                             total_balloon_lift();

    const double liftwithoutspeed = std::accumulate( wings.begin(), wings.end(), double{0.0},
    [&]( double acc, int wing ) {
        const double liftcoff{ parts[ wing ].info().lift_coff() };
        // m^2 area is always 1
        return acc + ( 0.5 * liftcoff );
    } );
    if( liftwithoutspeed < 1 ) {
        return 0;
    }
    const double needed_met_sec_squared = needed_force / liftwithoutspeed;
    const double needed_met_sec = std::sqrt( needed_met_sec_squared );
    const double needed_km_hour = needed_met_sec / 1000 * 3600;
    if( speed_type == "default" ) {
        speed_type = get_option<std::string>( "USE_METRIC_SPEEDS" );
    }
    if( speed_type == "km/h" ) {
        return ceil( needed_km_hour );
    } else if( speed_type == "mph" ) {
        return ceil( needed_km_hour / 1.609 );
    } else if( speed_type == "t/t" ) {
        return ceil( needed_km_hour / 1.609 / 4 );
    } else {
        return INT_MAX;
    }
}
// For some reason, just checking total lift > 0 doesn't seem to work if the vehicle hasn't been piloted before, which was impacting the design view. This fixes it, and can be used to check if the vehicle HAS lift, but not enough to fly by doing has_lift && !has_sufficient_lift
bool vehicle::has_lift() const
{
    return has_part( VPFLAG_ROTOR ) || has_part( VPFLAG_BALLOON ) || has_part( VPFLAG_WING );
}

bool vehicle::has_sufficient_lift( const bool unpowered, const bool idle ) const
{
    return total_lift( true, false, false, unpowered, idle ) > to_newton( total_mass() );
}

double vehicle::get_lift_percent( const bool unpowered ) const
{
    return std::max( 0.0, 1 - ( total_lift( true, false, false,
           unpowered ) / to_newton( total_mass() ) ) );
}

bool vehicle::is_rotorcraft() const
{
    return ( has_part( VPFLAG_ROTOR ) ) && has_sufficient_lift();
}
// requires vehicle to have sufficient rotor lift
bool vehicle::is_aircraft() const
{
    return ( has_part( VPFLAG_ROTOR ) || has_part( VPFLAG_WING ) || has_part( VPFLAG_BALLOON ) )
    && has_sufficient_lift();
}

int vehicle::get_z_change() const
{
    return requested_z_change;
}

bool vehicle::is_flying_in_air() const
{
    return is_flying;
}

void vehicle::set_flying( bool new_flying_value )
{
    is_flying = new_flying_value;
}

bool vehicle::is_watercraft() const
{
    return is_floating || ( in_water && wheelcache.empty() );
}

bool vehicle::is_in_water( bool deep_water ) const
{
    return deep_water ? is_floating : in_water;
}

static constexpr double water_density = 1000.0; // kg/m^3

double vehicle::coeff_water_drag() const
{
    if( !coeff_water_dirty ) {
    return coefficient_water_resistance;
}
std::vector<int> hull_indices = all_parts_at_location( part_location_under );
double hull_coverage;
if( hull_indices.empty() ) {
        hull_coverage = 0;
    } else {
        hull_coverage = std::clamp( static_cast<double>( floating.size() ) / hull_indices.size(), 0.0,
                                    1.0 );
    }

    std::set<int> occupied_y;
for( int idx : hull_indices ) {
    occupied_y.insert( parts[idx].mount.y() );
    }
    // Tile == 1m width
    // I have a feeling this and actual_area_m cancle out somewhere in there...
    double width_m = occupied_y.size();
    if( width_m == 0 ) {
    width_m = 1;
}

// Each piece of hull is 1m^2
// Thus area is the number of hull pieces
double actual_area_m = hull_indices.size();

// effective hull area is actual hull area * hull coverage
if( hull_coverage == 0 ) {
    hull_area = 0;
} else {
    hull_area = actual_area_m * std::max( 0.1, hull_coverage );
    }
    // Treat the hullform as a simple cuboid to calculate displaced depth of
    // water.
    // Apply Archimedes' principle (mass of water displaced is mass of vehicle).
    // area * depth = hull_volume = water_mass / water density
    // water_mass = vehicle_mass
    // area * depth = vehicle_mass / water_density
    // depth = vehicle_mass / water_density / area
    if( hull_area == 0 ) {
    draft_m = 1;
} else {
    draft_m = to_kilogram( total_mass() ) / water_density / hull_area * get_lift_percent( true );
        draft_m = std::max( draft_m, 0.0 );
    }
    // increase the streamlining as more of the boat is covered in boat boards
    double c_water_drag = 1.25 - hull_coverage;
    // hull height starts at 0.3m and goes up as you add more boat boards
    if( hull_coverage == 0 ) {
    hull_height = 0;
} else {
    hull_height = 0.3 + 0.5 * hull_coverage;
}
// F_water_drag = c_water_drag * cross_area * 1/2 * water_density * v^2
// coeff_water_resistance = c_water_drag * cross_area * 1/2 * water_density
coefficient_water_resistance = c_water_drag * width_m * draft_m * 0.5 * water_density;
coeff_water_dirty = false;
return coefficient_water_resistance;
}

double vehicle::max_buoyancy() const
{
    if( coeff_water_dirty ) {
    coeff_water_drag();
    }
    const double total_volume = hull_area * water_hull_height();
    return total_volume * water_density * GRAVITY_OF_EARTH;
}

float vehicle::k_traction( float wheel_traction_area ) const
{
    if( is_floating ) {
    return can_float() ? 1.0f : -1.0f;
    }
    if( is_flying ) {
    // Dont prematurely kill our flight, we'll fall soon enough
    return ( has_lift() ) ? 1.0f : -1.0f;
    }
    if( is_watercraft() && can_float() ) {
        return 1.0f;
    }

    const float fraction_without_traction = 1.0f - wheel_traction_area / wheel_area();
    if( fraction_without_traction == 0 ) {
    return 1.0f;
}
const float mass_penalty = fraction_without_traction * to_kilogram( total_mass() );
    float traction = std::min( 1.0f, wheel_traction_area / mass_penalty );
    add_msg( m_debug, "%s has traction %.2f", name, traction );

    // For now make it easy until it gets properly balanced: add a low cap of 0.1
    return std::max( 0.1f, traction );
}
