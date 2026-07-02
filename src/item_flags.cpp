// Item flag/property/quality/technique accessors
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

void item::unset_flags()
{
    item_tags.clear();
}

bool item::has_fault( const fault_id &fault ) const
{
    return faults.contains( fault );
}

bool item::has_own_flag( const flag_id &f ) const
{
    return item_tags.count( f );
}

bool item::has_flag( const flag_id &f ) const
{
    // Check if we have any gun/toolmods with the flag, and if we do
    // check if that flag should be inherited.
    // `json_flag::get` is pretty expensive so it's faster to do it
    // last as frequently there are no gun/toolmods with the flag f
    auto mods = is_gun() ? gunmods() : toolmods();

    const auto flag_in_mods = [&f]( const auto & mods ) -> bool {
        return std::any_of( mods.begin(), mods.end(), [&f]( const item * e )-> bool {
            return ( !e->is_gun() && e->has_flag( f ) );
        } );
    };

    if( f->inherit() && flag_in_mods( mods ) ) {
        return true;
    }

    // other item type flags
    if( type->has_flag( f ) ) {
        return true;
    }

    // now check for item specific flags
    return has_own_flag( f );
}

bool item::has_vitamin( const vitamin_id &v ) const
{
    if( !this->is_comestible() ) {
    return false;
}
// We need this function to get all vitamins including from inheritance.
// But we don't care about calories, so we can just pass a dummy.
npc dummy;
const nutrients food_item = dummy.compute_effective_nutrients( *this );
for( auto const& [vit_id, amount] : food_item.vitamins ) {
    if( vit_id == v ) {
            if( amount > 0 ) {
                return true;
            } else {
                break;
            }
        }
    }
    return false;
}

void item::set_flag( const flag_id &flag )
{
    if( flag.is_valid() ) {
        item_tags.insert( flag );
    } else {
        debugmsg( "Attempted to set invalid flag_id %s", flag.str() );
    }
}

void item::unset_flag( const flag_id &flag )
{
    item_tags.erase( flag );
}

void item::set_flag_recursive( const flag_id &flag )
{
    set_flag( flag );
    for( item * const &comp : components ) {
        comp->set_flag_recursive( flag );
    }
}

const item::FlagsSetType &item::get_flags() const
{
    return item_tags;
}

bool item::has_property( const std::string &prop ) const
{
    return type->properties.contains( prop );
}

std::string item::get_property_string( const std::string &prop, const std::string &def ) const
{
    const auto it = type->properties.find( prop );
    return it != type->properties.end() ? it->second : def;
}

int64_t item::get_property_int64_t( const std::string &prop, int64_t def ) const
{
    const auto it = type->properties.find( prop );
    if( it != type->properties.end() ) {
        char *e = nullptr;
        int64_t r = std::strtoll( it->second.c_str(), &e, 10 );
        if( !it->second.empty() && *e == '\0' ) {
            return r;
        }
        debugmsg( "invalid property '%s' for item '%s'", prop.c_str(), tname() );
    }
    return def;
}

int item::get_quality( const quality_id &id ) const
{
    int return_quality = INT_MIN;

    /**
     * EXCEPTION: Items with quality BOIL only count as such if they are empty,
     * excluding items of their ammo type if they are tools.
     */
    auto block_boil_filter = [this]( const item & itm ) {
        // We want to skip (do not block) only those : correct ammo, correct magazine, correct toolmod.Everything else should block.
        if( &itm == this ) {
            // Do not block if checking itself - we are checking only item contents not item itself.
            return false;
        } else if( itm.is_ammo() ) {
            return !ammo_types().contains( itm.ammo_type() );
        } else if( itm.is_magazine() ) {
            // we want to return "fine for boiling" if any of the ammo types match and "blocks boiling" if none match.
            for( const ammotype &at : ammo_types() ) {
                for( const ammotype &mag_at : itm.ammo_types() ) {
                    if( at == mag_at ) {
                        return false;
                    }
                }
            }
            return true;
        } else if( itm.is_toolmod() ) {
            return false;
        }
        return true;
    };
    // if it's has boil quality and it's empty, it's good to boil. If it's not empty and it's not a tool (it's probably a container), it's not good to boil. If it's a tool, it gets an extra chance: if it's only contents are mods or batteries, it's still good.
    // Also  we are using inverted filter, since we don't care about items that the filter likes, we only care if it find something it doesn't like.
    if( id == quality_id( "BOIL" ) && !contents.empty() &&
        ( !is_tool() || has_item_with( block_boil_filter ) ) ) {
        return INT_MIN;
    }

    for( const std::pair<const quality_id, int> &quality : type->qualities ) {
        if( quality.first == id ) {
            return_quality = quality.second;
        }
    }
    return_quality = std::max( return_quality, contents.best_quality( id ) );

    return return_quality;
}

std::map<quality_id, int> item::get_qualities() const
{
    std::map<quality_id, int> qualities;
    for( const auto &quality : type->qualities ) {
        qualities[quality.first] = get_quality( quality.first );
    }
    return qualities;
}

bool item::has_technique( const matec_id &tech ) const
{
    return type->techniques.contains( tech ) || techniques.contains( tech );
}

void item::add_technique( const matec_id &tech )
{
    techniques.insert( tech );
}

void item::remove_technique( const matec_id &tech )
{
    techniques.erase( tech );
}

std::vector<item *> item::toolmods()
{
    std::vector<item *> res;
    if( is_tool() ) {
        for( item *e : contents.all_items_top() ) {
            if( e->is_toolmod() ) {
                res.push_back( e );
            }
        }
    }
    return res;
}

std::vector<const item *> item::toolmods() const
{
    std::vector<const item *> res;
    if( is_tool() ) {
        for( const item *e : contents.all_items_top() ) {
            if( e->is_toolmod() ) {
                res.push_back( e );
            }
        }
    }
    return res;
}

std::set<matec_id> item::get_techniques() const
{
    std::set<matec_id> result = type->techniques;
    result.insert( techniques.begin(), techniques.end() );
    return result;
}

int item::get_comestible_fun() const
{
    if( !is_comestible() ) {
    return 0;
}
auto fun = get_comestible()->fun;
for( const flag_id &flag : item_tags ) {
    fun += flag->taste_mod();
    }
for( const flag_id &flag : type->get_flags() ) {
    fun += flag->taste_mod();
    }

    return static_cast<int>( get_var( "comestible_fun", static_cast<double>( fun ) ) );
}
