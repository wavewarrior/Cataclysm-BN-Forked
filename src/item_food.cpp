// Item food/rot methods: spoilage queries, rot calculations, and rot processing
// — split out of item.cpp. .cpp-only, no API changes.

#include "item.h"

#include <algorithm>
#include <numeric>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_set>

#include "active_tile_data_def.h"
#include "ammo.h"
#include "ascii_art.h"
#include "avatar.h"
#include "bionics.h"
#include "bodypart.h"
#include "cached_item_options.h"
#include "catalua_icallback_actor.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "character.h"
#include "character_encumbrance.h"
#include "character_functions.h"
#include "character_id.h"
#include "character_martial_arts.h"
#include "character_stat.h"
#include "clothing_mod.h"
#include "clzones.h"
#include "color.h"
#include "craft_command.h"
#include "damage.h"
#include "debug.h"
#include "dispersion.h"
#include "drop_token.h"
#include "effect.h" // for weed_msg
#include "enums.h"
#include "explosion.h"
#include "faction.h"
#include "fault.h"
#include "field_type.h"
#include "fire.h"
#include "flag.h"
#include "game.h"
#include "game_constants.h"
#include "gun_mode.h"
#include "iexamine.h"
#include "int_id.h"
#include "inventory.h"
#include "item_category.h"
#include "item_factory.h"
#include "item_group.h"
#include "iteminfo_format_utils.h"
#include "iteminfo_query.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "line.h"
#include "locations.h"
#include "magic.h"
#include "magic_enchantment.h"
#include "map.h"
#include "martialarts.h"
#include "material.h"
#include "melee.h"
#include "messages.h"
#include "mod_manager.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "options.h"
#include "output.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "pimpl.h"
#include "player.h"
#include "player_activity.h"
#include "pldata.h"
#include "point.h"
#include "projectile.h"
#include "profile.h"
#include "ranged.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "relic.h"
#include "requirements.h"
#include "ret_val.h"
#include "rng.h"
#include "rot.h"
#include "scores_ui.h"
#include "cloning_utils.h"
#include "skill.h"
#include "stomach.h"
#include "string_formatter.h"
#include "string_id_utils.h"
#include "string_utils.h"
#include "text_snippets.h"
#include "translations.h"
#include "type_id.h"
#include "units.h"
#include "units_energy.h"
#include "units_utility.h"
#include "value_ptr.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vitamin.h"
#include "vpart_position.h"
#include "weather.h"
#include "weather_gen.h"
#include "wheel_dimensions.h"

// File-scope id constants (moved with food/rot methods; internal linkage).
static const item_category_id itemcat_drugs( "drugs" );
static const item_category_id itemcat_food( "food" );

namespace item_internal
{
bool goes_bad_temp_cache = false;
const item *goes_bad_temp_cache_for = nullptr;
inline bool goes_bad_cache_fetch()
{
    return goes_bad_temp_cache;
}
inline void goes_bad_cache_set( const item *i )
{
    goes_bad_temp_cache = i->goes_bad();
    goes_bad_temp_cache_for = i;
}
inline void goes_bad_cache_unset()
{
    goes_bad_temp_cache = false;
    goes_bad_temp_cache_for = nullptr;
}
inline bool goes_bad_cache_is_for( const item *i )
{
    return goes_bad_temp_cache_for == i;
}

struct scoped_goes_bad_cache {
    scoped_goes_bad_cache( item *i ) {
        goes_bad_cache_set( i );
    }
    ~scoped_goes_bad_cache() {
        goes_bad_cache_unset();
    }
};
} // namespace item_internal

bool item::goes_bad() const
{
    if( item_internal::goes_bad_cache_is_for( this ) ) {
    return item_internal::goes_bad_cache_fetch();
    }
    if( has_flag( flag_PROCESSING ) ) {
    return false;
}
if( is_corpse() ) {
    // Corpses rot only if they are made of rotting materials
    return made_of_any( materials::get_rotting() );
    }
    return is_food() && get_comestible()->spoils != 0_turns;
}

bool item::goes_bad_after_opening( bool strict ) const
{
    // check if this item is explicitly a canning-type item: eg, it preserves contents
    if( strict ) {
    if( type->container && type->container->preserves &&
            !contents.empty() && contents.front().goes_bad() ) {
            return true;
        } else {
            return false;
        }
    }

    return goes_bad() || ( type->container && type->container->preserves &&
                           !contents.empty() && contents.front().goes_bad() );
}

auto item::is_in_preserving_container() const -> bool
{
    for( const item *parent = parent_item(); parent != nullptr; parent = parent->parent_item() ) {
        if( parent->type && parent->type->container && parent->type->container->preserves ) {
            return true;
        }
    }
    return false;
}

auto item::mark_rot_checked_now() -> void
{
    last_rot_check = calendar::turn;
}

time_duration item::get_shelf_life() const
{
    if( goes_bad() ) {
    if( is_food() ) {
            return get_comestible()->spoils;
        } else if( is_corpse() ) {
            return 24_hours;
        }
    }
    return 0_turns;
}

double item::get_relative_rot() const
{
    if( goes_bad() ) {
    const_cast<item *>( this )->update_rot_from_location( temperature_flag::TEMP_NORMAL );
        return rot / get_shelf_life();
    }
    return 0;
}

void item::set_relative_rot( double val )
{
    if( goes_bad() ) {
        rot = get_shelf_life() * val;
        // calc_rot uses last_rot_check (when it's not turn_zero) instead of bday.
        // this makes sure the rotting starts from now, not from bday.
        // if this item is the result of smoking or milling don't do this, we want to start from bday.
        if( !has_flag( flag_PROCESSING_RESULT ) ) {
            last_rot_check = calendar::turn;
        }
    }
}

void item::set_rot( time_duration val )
{
    rot = val;
}

int item::spoilage_sort_order() const
{
    const item *subject;
    constexpr int bottom = std::numeric_limits<int>::max();

    if( type->container && !contents.empty() ) {
        if( type->container->preserves ) {
            return bottom - 3;
        }
        subject = &contents.front();
    } else {
        subject = this;
    }

    if( subject->goes_bad() ) {
        return to_turns<int>( subject->get_shelf_life() - subject->rot );
    }

    if( subject->get_comestible() ) {
        if( subject->get_category().get_id() == itemcat_food ) {
            return bottom - 3;
        } else if( subject->get_category().get_id() == itemcat_drugs ) {
            return bottom - 2;
        } else {
            return bottom - 1;
        }
    }
    return bottom;
}

namespace
{

/**
 * Hardcoded lookup table for food rots per hour calculation.
 *
 * IRL this tends to double every 10c a few degrees above freezing, but past a certain
 * point the rate decreases until even extremophiles find it too hot. Here we just stop
 * further acceleration at 40C.
 *
 * Original formula:
 * @see https://github.com/cataclysmbn/Cataclysm-BN/blob/033901af4b52ad0bfcfd6abfe06bca4e403d44b1/src/item.cpp#L5612-L5640
 */
constexpr auto rot_chart = std::array<int, 44> {
    0, 372, 744, 1118, 1219, 1273, 1388, 1514, 1651, 1800,
    1880, 2050, 2235, 2438, 2658, 2776, 3027, 3301, 3600, 3926,
    4100, 4471, 4875, 5317, 5798, 6054, 6602, 7200, 7852, 8562,
    8941, 9751, 10633, 11595, 12645, 13205, 14400, 15703, 17125, 18674,
    19501,
};

} // namespace

/**
 * Get the hourly rot for a given temperature from the precomputed table.
 * @see rot_chart
 */
auto get_hourly_rotpoints_at_temp( const units::temperature temp ) -> int
{
    /**
     * Precomputed rot lookup table.
     */
    if( temp < temperatures::freezing ) {
    return 0;
}
if( temp > 40_c ) {
    return 21240;
}
// HACK: due to frequent fahrenheit <-> celsius conversion, 18C is actually 17.777C
// remove rounding after most of temperatures passed around are in `units::temperature`
const float temp_c = static_cast<float>( units::to_millidegree_celsius( temp ) ) / 1000;
    return rot_chart[std::round( temp_c )];
}

auto item::calc_rot( time_point time, const units::temperature temp ) const -> time_duration
{
    // Avoid needlessly calculating already rotten things.  Corpses should
    // always rot away and food rots away at twice the shelf life.  If the food
    // is in a sealed container they won't rot away, this avoids needlessly
    // calculating their rot in that case.
    if( !is_corpse() && get_shelf_life() != 0_turns && rot / get_shelf_life() > 2.0 ) {
        return 0_seconds;
    }

    // rot modifier
    float factor = 1.0;
    if( is_corpse() && has_flag( flag_FIELD_DRESS ) ) {
        factor = 0.75;
    }

    time_duration added_rot = 0_seconds;
    // simulation of different age of food at the start of the game and good/bad storage
    // conditions by applying starting variation bonus/penalty of +/- 20% of base shelf-life
    // positive = food was produced some time before calendar::start and/or bad storage
    // negative = food was stored in good conditions before calendar::start
    if( last_rot_check <= calendar::start_of_cataclysm ) {
        time_duration spoil_variation = get_shelf_life() * 0.2f;
        added_rot += rng( -spoil_variation, spoil_variation );
    }
    time_duration time_delta = time - last_rot_check;
    added_rot += factor * time_delta / 1_hours * get_hourly_rotpoints_at_temp( temp ) * 1_turns;
    return added_rot;
}

namespace
{

auto temperature_flag_to_highest_temperature( temperature_flag temperature ) -> units::temperature
{
    switch( temperature ) {
    case temperature_flag::TEMP_NORMAL:
    case temperature_flag::TEMP_HEATER:
        return units::temperature_max;
    case temperature_flag::TEMP_FRIDGE:
        return temperatures::fridge;
    case temperature_flag::TEMP_FREEZER:
        return temperatures::freezer;
    case temperature_flag::TEMP_ROOT_CELLAR:
        return temperatures::root_cellar;
}

return units::temperature_max;
}

} // namespace


time_duration item::minimum_freshness_duration( temperature_flag temperature ) const
{
    if( is_in_preserving_container() ) {
    return calendar::INDEFINITELY_LONG_DURATION;
}
const units::temperature temp = temperature_flag_to_highest_temperature( temperature );
unsigned long long rot_per_hour = get_hourly_rotpoints_at_temp( temp );

if( rot_per_hour <= 0 || !type->comestible ) {
    return calendar::INDEFINITELY_LONG_DURATION;
}

time_duration remaining_rot = type->comestible->spoils - rot;
// Has to be in int64 or it will overflow for long lasting food
unsigned long long duration = to_turns<unsigned long long>( remaining_rot )
                              * to_turns<unsigned long long>( 1_hours )
                              / rot_per_hour;
if( duration > to_turns<unsigned long long>( calendar::INDEFINITELY_LONG_DURATION ) ) {
    return calendar::INDEFINITELY_LONG_DURATION;
}

return time_duration::from_turns( static_cast<int>( duration ) );
}

void item::mod_last_rot_check( time_duration processing_duration )
{
    if( !has_own_flag( flag_PROCESSING ) ) {
        debugmsg( "mod_last_rot_check called on non smoking item: %s", tname() );
        return;
    }

    // Apply no rot while smoking
    last_rot_check += processing_duration;
}

detached_ptr<item> item::process_rot( detached_ptr<item> &&self, const tripoint_bub_ms &pos )
{
    return process_rot( std::move( self ), false, pos, nullptr, temperature_flag::TEMP_NORMAL,
                        get_weather() );
}

static units::temperature clip_by_temperature_flag( units::temperature temperature,
        temperature_flag flag )
{
    switch( flag ) {
        case temperature_flag::TEMP_NORMAL:
            // Just use the temperature normally
            return temperature;
        case temperature_flag::TEMP_FRIDGE:
            return std::min( temperature, temperatures::fridge );
        case temperature_flag::TEMP_FREEZER:
            return std::min( temperature, temperatures::freezer );
        case temperature_flag::TEMP_HEATER:
            return std::max( temperature, temperatures::normal );
        case temperature_flag::TEMP_ROOT_CELLAR:
            return temperatures::root_cellar;
        default:
            debugmsg( "Temperature flag enum not valid: %d.  Using current temperature.",
                      static_cast<int>( flag ) );
            break;
    }
    return temperature;
}

void item::update_rot_from_location( const temperature_flag temperature )
{
    if( !goes_bad() || last_rot_check == calendar::turn ) {
        return;
    }
    if( is_in_preserving_container() ) {
        mark_rot_checked_now();
        return;
    }

    auto pos = tripoint_bub_ms::zero();
    auto flag = temperature;
    if( is_loaded() && has_position() ) {
        pos = position();
        flag = rot::temperature_flag_for_location( get_map(), *this );
    }
    update_rot( pos, flag, get_weather() );
}

void item::update_rot( const tripoint_bub_ms &pos, const temperature_flag flag,
                       const weather_manager &weather )
{
    const time_point now = calendar::turn;

    // if player debug menu'd the time backward it breaks stuff, just reset the
    // last_temp_check and last_rot_check in this case
    if( now - last_rot_check < 0_turns ) {
        last_rot_check = now;
        return;
    }

    // process rot at most once every 100_turns (10 min)
    // note we're also gated by item::processing_speed
    constexpr time_duration smallest_interval = 10_minutes;

    units::temperature temp = weather.get_temperature( bub_to_abs( pos ) );
    temp = clip_by_temperature_flag( temp, flag );

    time_point time = last_rot_check;
    item_internal::scoped_goes_bad_cache _cache( this );

    if( now - time > 1_hours ) {
        // This code is for items that were left out of reality bubble for long time

        const weather_generator &wgen = weather.get_cur_weather_gen();
        const unsigned int seed = g->get_seed();
        // It's a modifier, so we need to subtract 0_f
        units::temperature local_mod = units::from_fahrenheit( g->new_game
                                       ? 0
                                       : get_map().get_temperature( pos ) ) - 0_f;

        // Process the past of this item since the last time it was processed
        while( now - time > 1_hours ) {
            // Get the environment temperature
            time_duration time_delta = std::min( 1_hours, now - 1_hours - time );
            time += time_delta;

            //Use weather if above ground, use map temp if below
            units::temperature env_temperature_raw;
            if( pos.z() >= 0 ) {
                tripoint_abs_ms location = tripoint_abs_ms( get_map().bub_to_abs( pos ) );
                units::temperature weather_temperature = wgen.get_weather_temperature( location, time,
                    calendar::config, seed );
                env_temperature_raw = weather_temperature + local_mod;
            } else {
                env_temperature_raw = temperatures::annual_average + local_mod;
            }

            units::temperature env_temperature_clipped = clip_by_temperature_flag( env_temperature_raw, flag );

            // Calculate item rot
            rot += calc_rot( time, env_temperature_clipped );
            last_rot_check = time;
        }
    }

    // Remaining <1 h from above
    // and items that are held near the player
    if( now - time > smallest_interval ) {
        rot += calc_rot( now, temp );
        last_rot_check = now;
    }
}

detached_ptr<item>  item::process_rot( detached_ptr<item> &&self, const bool seals,
                                       const tripoint_bub_ms &pos,
                                       player *carrier, const temperature_flag flag,
                                       const weather_manager &weather )
{
    if( !self ) {
        return std::move( self );
    }
    if( self->is_in_preserving_container() ) {
        self->mark_rot_checked_now();
        return std::move( self );
    }

    self->update_rot( pos, flag, weather );

    if( self->has_rotten_away() && carrier == nullptr && !seals ) {
        return detached_ptr<item>();
    }
    return std::move( self );
}

time_duration item::brewing_time() const { return is_brewable() ? type->brewable->time : 0_turns; }

const std::vector<itype_id>& item::brewing_results() const {
    static const std::vector<itype_id> nulresult{};
    return is_brewable() ? type->brewable->results : nulresult;
}
