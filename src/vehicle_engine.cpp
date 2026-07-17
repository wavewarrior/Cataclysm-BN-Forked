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

static const itype_id fuel_type_animal( "animal" );
static const itype_id fuel_type_battery( "battery" );
static const itype_id fuel_type_muscle( "muscle" );
static const itype_id fuel_type_wind( "wind" );
static const fault_id fault_belt( "fault_engine_belt_drive" );
static const efftype_id effect_harnessed( "harnessed" );
static const std::string str_PERPETUAL( "PERPETUAL" );
const int bat_energy_j = 1000;

void vehicle::toggle_specific_engine( int e, bool on )
{
    // Special validation for muscle engines
    if( on && is_engine_type( e, fuel_type_muscle ) ) {
        std::string failure_reason;
        if( !can_enable_muscle_engine( e, failure_reason ) ) {
            add_msg( m_info, "%s", failure_reason.c_str() );
            return; // Don't enable the engine
        }
    }

    toggle_specific_part( engines[e], on );
}
void vehicle::toggle_specific_part( int p, bool on )
{
    parts[p].enabled = on;
}

bool vehicle::can_enable_muscle_engine( int e, std::string &failure_reason ) const
{
    const int part_idx = engines[e];
    const vpart_info &engine_info = part_info( part_idx );

    const player *passenger = get_passenger( part_idx );
    if( passenger != nullptr ) {
        if( engine_info.has_flag( "MUSCLE_LEGS" ) && passenger->get_working_leg_count() < 2 ) {
            failure_reason = string_format( _( "%s cannot operate the %s due to injured legs." ),
                                            passenger->name, engine_info.name() );
            return false;
        }
        if( engine_info.has_flag( "MUSCLE_ARMS" ) && passenger->get_working_arm_count() < 2 ) {
            failure_reason = string_format( _( "%s cannot operate the %s due to injured arms." ),
                                            passenger->name, engine_info.name() );
            return false;
        }
        return true;
    }

    failure_reason = string_format( _( "The %s cannot operate without an occupant." ),
                                    engine_info.name() );
    return false;
}

bool vehicle::has_muscle_engine_operator( int e ) const
{
    const int part_idx = engines[e];
    const vpart_info &engine_info = part_info( part_idx );

    const player *passenger = get_passenger( part_idx );
    if( passenger != nullptr ) {
        if( engine_info.has_flag( "MUSCLE_LEGS" ) && passenger->get_working_leg_count() >= 2 ) {
            return true;
        }
        if( engine_info.has_flag( "MUSCLE_ARMS" ) && passenger->get_working_arm_count() >= 2 ) {
            return true;
        }
    }
    return false;
}

void vehicle::validate_muscle_engines()
{
    for( size_t e = 0; e < engines.size(); e++ ) {
        if( is_engine_type( e, fuel_type_muscle ) && is_engine_on( e ) ) {
            if( !has_muscle_engine_operator( e ) ) {
                const int part_idx = engines[e];
                const vpart_info &engine_info = part_info( part_idx );
                parts[part_idx].enabled = false;
                add_msg( m_info, _( "The %s shuts down - it cannot operate without an occupant." ),
                         engine_info.name() );
            }
        }
    }
}
bool vehicle::is_engine_type_on( int e, const itype_id &ft ) const
{
    return is_engine_on( e ) && is_engine_type( e, ft );
}

bool vehicle::has_engine_type( const itype_id &ft, const bool enabled ) const
{
    for( size_t e = 0; e < engines.size(); ++e ) {
        if( is_engine_type( e, ft ) && ( !enabled || is_engine_on( e ) ) ) {
            return true;
        }
    }
    return false;
}
bool vehicle::has_engine_type_not( const itype_id &ft, const bool enabled ) const
{
    for( size_t e = 0; e < engines.size(); ++e ) {
        if( !is_engine_type( e, ft ) && ( !enabled || is_engine_on( e ) ) ) {
            return true;
        }
    }
    return false;
}

bool vehicle::has_engine_conflict( const vpart_info *possible_conflict,
                                   std::string &conflict_type ) const
{
    std::vector<std::string> new_excludes = possible_conflict->engine_excludes();
    // skip expensive string comparisons if there are no exclusions
    if( new_excludes.empty() ) {
        return false;
    }

    bool has_conflict = false;

    for( int engine : engines ) {
        std::vector<std::string> install_excludes = part_info( engine ).engine_excludes();
        std::vector<std::string> conflicts;
        std::set_intersection( new_excludes.begin(), new_excludes.end(), install_excludes.begin(),
                               install_excludes.end(), back_inserter( conflicts ) );
        if( !conflicts.empty() ) {
            has_conflict = true;
            conflict_type = conflicts.front();
            break;
        }
    }
    return has_conflict;
}

bool vehicle::is_engine_type( const int e, const itype_id  &ft ) const
{
    auto engine_fuel = parts[engines[e]].info().engine_fuel_opts();
    bool engine_has_fuel = std::find( engine_fuel.begin(), engine_fuel.end(), ft ) != engine_fuel.end();
    return parts[engines[e]].ammo_current().is_null() ? engine_has_fuel :
           parts[engines[e]].ammo_current() == ft;
}

bool vehicle::is_perpetual_type( const int e ) const
{
    const itype_id  &ft = part_info( engines[e] ).fuel_type;
    //TODO!: push up
    return item::spawn_temporary( ft )->has_flag( flag_PERPETUAL );
}

bool vehicle::is_engine_on( const int e ) const
{
    return parts[ engines[ e ] ].is_available() && is_part_on( engines[ e ] );
}

bool vehicle::is_part_on( const int p ) const
{
    const auto &pt = parts[p];
    return pt.enabled || ( pt.is_available() && pt.info().has_flag( str_PERPETUAL ) );
}

bool vehicle::is_alternator_on( const int a ) const
{
    auto &alt = parts[ alternators [ a ] ];
    if( alt.is_unavailable() ) {
        return false;
    }

    return std::ranges::any_of( engines, [this, &alt]( int idx ) {
        const auto &eng = parts [ idx ];
        //fuel_left checks that the engine can produce power to be absorbed
        return eng.is_available() && eng.enabled && fuel_left( eng.fuel_current() ) &&
               eng.mount == alt.mount && !eng.faults().contains( fault_belt );
    } );
}

bool vehicle::has_security_working() const
{
    bool found_security = false;
    if( fuel_left( fuel_type_battery ) > 0 ) {
        const auto [c, s] = get_controls_and_security();
        found_security = s >= 0;
    }
    return found_security;
}

void vehicle::backfire( const int e ) const
{
    const int power = part_vpower_w( engines[e], true );
    const auto pos = bub_part_location( engines[e] );
    sounds::sound( pos, 40 + power / 10000, sounds::sound_t::movement,
                   // single space after the exclaimation mark because it does not end the sentence
                   //~ backfire sound
                   string_format( _( "a loud BANG! from the %s" ), // NOLINT(cata-text-style)
                                  parts[ engines[ e ] ].name() ), true, "vehicle", "engine_backfire" );
}

const vpart_info &vehicle::part_info( int index, bool include_removed ) const
{
    if( index < static_cast<int>( parts.size() ) ) {
    if( !parts[index].removed || include_removed ) {
            return parts[index].info();
        }
    }
    return vpart_id::NULL_ID().obj();
}

// engines & alternators all have power.
// engines provide, whilst alternators consume.
int vehicle::part_vpower_w( const int index, const bool at_full_hp ) const
{
    const vehicle_part &vp = parts[ index ];
    int pwr = vp.info().power;
    if( part_flag( index, VPFLAG_ENGINE ) ) {
        if( pwr == 0 ) {
            pwr = vhp_to_watts( vp.base->engine_displacement() );
        }
        if( vp.info().fuel_type == fuel_type_animal ) {
            monster *mon = get_pet( index );
            if( mon != nullptr && mon->has_effect( effect_harnessed ) ) {
                // An animal that can carry twice as much weight, can pull a cart twice as hard.
                pwr = mon->get_speed() * mon->get_size() * 3
                      * ( mon->get_mountable_weight_ratio() * 5 );
            } else {
                pwr = 0;
            }
        }
        ///\EFFECT_STR increases power produced for MUSCLE_* vehicles
        pwr += ( g->u.str_cur - 8 ) * part_info( index ).engine_muscle_power_factor();
        /// wind-powered vehicles have differing power depending on wind direction
        if( vp.info().fuel_type == fuel_type_wind ) {
            const weather_manager &weather = get_weather();
            int windpower = weather.windspeed;
            // We're dead in the water.
            if( windpower < 1 ) {
                pwr = 0;
            } else {
                // For gameplay purposes, permit adjusting sails enough to sail upwind so long as it's blowing at all.
                rl_vec2d windvec;
                double raddir = ( ( weather.winddirection + 180 ) % 360 ) * ( M_PI / 180 );
                windvec = windvec.normalized();
                windvec.y = -std::cos( raddir );
                windvec.x = std::sin( raddir );
                rl_vec2d fv = face_vec();
                // We want 0.5 multiplier at 90 degrees instead of 0.0, so add 0.5.
                double dot = windvec.dot_product( fv ) + 0.5;
                // We don't want negatives or over 100% power, however.
                dot = std::min( 1.0, std::max( 0.0, dot ) );
                int windeffectint = static_cast<int>( ( windpower * dot ) * 200 );
                pwr = pwr + windeffectint;
            }
        }
    }

    if( pwr < 0 ) {
        return pwr; // Consumers always draw full power, even if broken
    }
    if( at_full_hp ) {
        return pwr; // Assume full hp
    }
    // Damaged engines give less power, but some engines handle it better
    double health = parts[index].health_percent();
    // dpf is 0 for engines that scale power linearly with damage and
    // provides a floor otherwise
    float dpf = part_info( index ).engine_damaged_power_factor();
    double effective_percent = dpf + ( ( 1 - dpf ) * health );
    return static_cast<int>( pwr * effective_percent );
}

// alternators, solar panels, reactors, and accessories all have epower.
// alternators, solar panels, and reactors provide, whilst accessories consume.
// for motor consumption see @ref vpart_info::energy_consumption instead
int vehicle::part_epower_w( const int index ) const
{
    int e = part_info( index ).epower;
    if( e < 0 ) {
        return e; // Consumers always draw full power, even if broken
    }
    return e * parts[ index ].health_percent();
}

int vehicle::power_to_energy_bat( const int power_w, const time_duration &d ) const
{
    // Integrate constant epower (watts) over time to get units of battery energy
    // Thousands of watts over millions of seconds can happen, so 32-bit int
    // insufficient.
    int64_t energy_j = power_w * to_seconds<int64_t>( d );
    int energy_bat = energy_j / bat_energy_j;
    int sign = power_w >= 0 ? 1 : -1;
    // energy_bat remainder results in chance at additional charge/discharge
    energy_bat += x_in_y( std::abs( energy_j % bat_energy_j ), bat_energy_j ) ? sign : 0;
    return energy_bat;
}

int vehicle::vhp_to_watts( const int power_vhp )
{
    // Convert vhp units (0.5 HP ) to watts
    // Used primarily for calculating battery charge/discharge
    // TODO: convert batteries to use energy units based on watts (watt-ticks?)
    constexpr int conversion_factor = 373; // 373 watts == 1 power_vhp == 0.5 HP
    return power_vhp * conversion_factor;
}

