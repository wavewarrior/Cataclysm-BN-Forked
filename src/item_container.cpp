// Item container/liquid: capacity queries, use_amount, allow_crafting_component, fill_with
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

// File-scope id constants (moved with container methods; internal linkage).
static const ammotype ammo_battery( "battery" );

namespace
{
// Intentional duplicate: fill_with() uses this in item_container.cpp while
// merge_charges() uses the copy in item.cpp. Callers split across TUs;
// pure computation (no state) so duplication is safe and correct.
time_duration weighted_averaged_rot( const item *a, const item *b )
{
    const int base_charges = a->charges + b->charges;

    return base_charges > 0
           ? ( a->get_rot() * a->charges + b->get_rot() * b->charges ) / base_charges
           : 0_seconds;
}
} // namespace

units::volume item::get_container_capacity() const
{
    if( !is_container() ) {
    return 0_ml;
}
return type->container->contains;
}

units::volume item::get_total_capacity() const
{
    units::volume result = get_storage() + get_container_capacity();

    // Consider various iuse_actors which add containing capability
    // Treating these two as special cases for now; if more appear in the
    // future then this probably warrants a new method on use_function to
    // access this information generically.
    if( is_bandolier() ) {
        result += dynamic_cast<const bandolier_actor *>
                  ( type->get_use( "bandolier" )->get_actor_ptr() )->max_stored_volume();
    }

    if( is_holster() ) {
        result += dynamic_cast<const holster_actor *>
                  ( type->get_use( "holster" )->get_actor_ptr() )->max_stored_volume();
    }

    return result;
}

int item::get_remaining_capacity_for_liquid( const item &liquid, bool allow_bucket,
        std::string *err ) const
{
    const auto error = [ &err ]( const std::string & message ) {
        if( err != nullptr ) {
            *err = message;
        }
        return 0;
    };

    int remaining_capacity = 0;

    // TODO: (sm) is_reloadable_with and this function call each other and can recurse for
    // watertight containers.
    if( !is_container() && is_reloadable_with( liquid.typeId() ) ) {
        if( ammo_remaining() != 0 && ammo_current() != liquid.typeId() ) {
            return error( string_format( _( "You can't mix loads in your %s." ), tname() ) );
        }
        remaining_capacity = ammo_capacity() - ammo_remaining();
    } else if( is_container() ) {
        if( !type->container->watertight && liquid.made_of( LIQUID ) ) {
            return error( string_format( _( "That %s isn't water-tight." ), tname() ) );
        } else if( !type->container->seals && ( !allow_bucket || !is_bucket() ) ) {
            return error( string_format( is_bucket() ?
                                         _( "That %s must be on the ground or held to hold contents!" )
                                         : _( "You can't seal that %s!" ), tname() ) );
        } else if( !contents.empty() && contents.front().typeId() != liquid.typeId() ) {
            return error( string_format( _( "You can't mix loads in your %s." ), tname() ) );
        }
        remaining_capacity = liquid.charges_per_volume( get_container_capacity() );
        if( !contents.empty() ) {
            remaining_capacity -= contents.front().charges;
        }
    } else {
        return error( string_format( _( "That %1$s won't hold %2$s." ), tname(),
                                     liquid.tname() ) );
    }

    if( remaining_capacity <= 0 ) {
        return error( string_format( _( "Your %1$s can't hold any more %2$s." ), tname(),
                                     liquid.tname() ) );
    }

    return remaining_capacity;
}

int item::get_remaining_capacity_for_liquid( const item &liquid, const Character &who,
        std::string *err ) const
{
    const bool allow_bucket = who.is_wielding( *this ) || !who.has_item( *this );
    int res = get_remaining_capacity_for_liquid( liquid, allow_bucket, err );

    if( res > 0 && !type->rigid && who.has_item( *this ) ) {
        const units::volume volume_to_expand = std::max( who.volume_capacity() - who.volume_carried(),
                                               0_ml );

        res = std::min( liquid.charges_per_volume( volume_to_expand ), res );

        if( res == 0 && err != nullptr ) {
            *err = string_format( _( "That %s doesn't have room to expand." ), tname() );
        }
    }

    return res;
}

int item::get_remaining_capacity_for_id( const itype_id &liquid, bool allow_buckets ) const
{
    int rem_cap = 0;
    itype obj = liquid.obj();
    if( !is_container() && is_reloadable_with( liquid ) ) {
        if( ammo_remaining() != 0 && ammo_current() != liquid ) {
            return 0;
        }
        rem_cap = ammo_capacity() - ammo_remaining();
    } else if( is_container() ) {
        if( !type->container->watertight ) {
            return 0;
        } else if( !type->container->seals && ( !allow_buckets || !is_bucket() ) ) {
            return 0;
        } else if( !contents.empty() && contents.front().typeId() != liquid ) {
            return 0;
        }
        rem_cap = obj.charges_per_volume( get_container_capacity() );
        if( !contents.empty() ) {
            rem_cap -= contents.front().charges;
        }
    }
    return rem_cap;
}

detached_ptr<item> item::use_amount( detached_ptr<item> &&self, const itype_id &it, int &quantity,
                                     std::vector<detached_ptr<item>> &used,
                                     const std::function<bool( const item & )> &filter )
{
    // Remember quantity so that we can unseal self
    int old_quantity = quantity;

    self->remove_items_with( [&]( detached_ptr<item> &&a ) {
        if( quantity > 0  && a->typeId() == it && filter( *a ) ) {
            used.push_back( std::move( a ) );
            quantity--;
            return VisitResponse::SKIP;
        }
        return VisitResponse::NEXT;
    } );

    if( quantity != old_quantity ) {
        self->on_contents_changed();
    }

    if( quantity > 0 && self->typeId() == it && filter( *self ) ) {
        used.push_back( std::move( self ) );
        quantity--;
        return detached_ptr<item>();
    }
    return std::move( self );
}


bool item::allow_crafting_component() const
{
    if( is_toolmod() && is_irremovable() ) {
    return false;
}

// vehicle batteries are implemented as magazines of charge
if( is_magazine() && ammo_types().contains( ammo_battery ) ) {
    return true;
}

// fixes #18886 - turret installation may require items with irremovable mods
if( is_gun() ) {
    bool valid = true;
    visit_items( [&]( const item * it ) {
        if( this == it ) {
                return VisitResponse::NEXT;
            }
            if( !( it->is_magazine() || ( it->is_gunmod() && it->is_irremovable() ) ) ) {
                valid = false;
                return VisitResponse::ABORT;
            }
            return VisitResponse::NEXT;
        } );
        return valid;
    }

    return contents.empty();
}

detached_ptr<item> item::fill_with( detached_ptr<item> &&liquid, int amount )
{
    if( amount == -1 ) {
        amount = INT_MAX;
    }
    amount = std::min( get_remaining_capacity_for_liquid( *liquid, true ),
                       std::min( amount, liquid->charges ) );
    if( amount <= 0 ) {
        return std::move( liquid );
    }

    if( !is_container() ) {
        if( !is_reloadable_with( liquid->typeId() ) ) {
            debugmsg( "Tried to fill %s which is not a container and can't be reloaded with %s.",
                      tname(), liquid->tname() );
            return std::move( liquid );
        }
        ammo_set( liquid->typeId(), ammo_remaining() + amount );
    } else if( is_food_container() ) {
        item &cts = contents.front();

        cts.set_rot( weighted_averaged_rot( &cts, &*liquid ) );
        cts.mod_charges( amount );
    } else if( !is_container_empty() ) {
        // if container already has liquid we need to set the amount
        item &cts = contents.front();
        cts.mod_charges( amount );
    } else {
        detached_ptr<item> liquid_copy = item::spawn( *liquid );
        liquid_copy->charges = amount;
        put_in( std::move( liquid_copy ) );
    }

    liquid->mod_charges( -amount );
    on_contents_changed();
    if( liquid->charges > 0 ) {
        return std::move( liquid );
    }
    return detached_ptr<item>();
}
