// Item tool/reload/timer/charges: has_use, units_remaining, reload, use_charges,
// set_counter/advance_timer, set_snippet; item_reload_option and iteminfo helpers
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
#include "item_reload_option.h"
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

// File-scope id constants (moved with tool methods; internal linkage).
static const ammotype ammo_plutonium( "plutonium" );
static const itype_id itype_UPS( "UPS" );
static const itype_id itype_bio_armor( "bio_armor" );

bool item::has_use() const
{
    return type->has_use();
}

const use_function *item::get_use( const std::string &use_name ) const
{
    const use_function *fun = nullptr;
    visit_items(
    [&fun, &use_name]( const item * it ) {
        if( it == nullptr ) {
            return VisitResponse::SKIP;
        }
        fun = it->get_use_internal( use_name );
        if( fun != nullptr ) {
            return VisitResponse::ABORT;
        }
        return VisitResponse::NEXT;
    } );

    return fun;
}

const use_function *item::get_use_internal( const std::string &use_name ) const
{
    if( type != nullptr ) {
        return type->get_use( use_name );
    }
    return nullptr;
}

const item *item::get_usable_item( const std::string &use_name ) const
{
    const item *ret = nullptr;
    visit_items(
    [&ret, &use_name]( const item * it ) {
        if( it == nullptr ) {
            return VisitResponse::SKIP;
        }
        if( it->get_use_internal( use_name ) ) {
            ret = it;
            return VisitResponse::ABORT;
        }
        return VisitResponse::NEXT;
    } );

    return ret;
}

item *item::get_usable_item( const std::string &use_name )
{
    return const_cast<item *>( const_cast<const item *>( this )->get_usable_item( use_name ) );
}

int item::units_remaining( const Character &ch, int limit ) const
{
    if( count_by_charges() ) {
    return std::min( static_cast<int>( charges ), limit );
    }

    int res = ammo_remaining();
    if( res < limit && is_power_armor() ) {
    if( character_funcs::can_interface_armor( ch ) && has_flag( flag_USE_UPS ) ) {
            res += std::max( ch.charges_of( itype_UPS, limit - res ), ch.charges_of( itype_bio_armor,
                             limit - res ) );
        } else if( character_funcs::can_interface_armor( ch ) ) {
            res += ch.charges_of( itype_bio_armor, limit - res );
        } else {
            res += ch.charges_of( itype_UPS, limit - res );
        }
    } else if( res < limit && has_flag( flag_USE_UPS ) ) {
    res += ch.charges_of( itype_UPS, limit - res );
    }

    return std::min( res, limit );
}

bool item::units_sufficient( const Character &ch, int qty ) const
{
    if( qty < 0 ) {
    qty = count_by_charges() ? 1 : ammo_required();
    }

    return units_remaining( ch, qty ) == qty;
}

item_reload_option::item_reload_option( const item_reload_option & ) = default;

item_reload_option &item_reload_option::operator=( const item_reload_option & ) = default;

item_reload_option::item_reload_option( const player *who, item *target, const item *parent,
                                        item &ammo ) :
    who( who ), target( target ), ammo( &ammo ), parent( parent )
{
    if( this->target->is_ammo_belt() ) {
        const auto &linkage = this->target->type->magazine->linkage ;
        if( linkage ) {
            max_qty = this->who->charges_of( *linkage );
        }
    }
    qty( max_qty );
}

int item_reload_option::moves() const
{
    int mv = ammo->obtain_cost( *who, qty() ) + who->item_reload_cost( *target, *ammo, qty() );
    if( parent != target ) {
        if( parent->is_gun() ) {
            mv += parent->get_reload_time();
        } else if( parent->is_tool() ) {
            mv += 100;
        }
    }
    return mv;
}

void item_reload_option::qty( int val )
{
    bool ammo_in_ammo_container = ammo->is_ammo_container();
    bool ammo_in_container = ammo->is_container();
    item &ammo_obj = ( ammo_in_ammo_container || ammo_in_container ) ?
                     ammo->contents.front() : *ammo;

    if( ammo_in_ammo_container && !ammo_obj.is_ammo() ) {
        debugmsg( "Invalid reload option: %s", ammo_obj.tname() );
        return;
    }

    // Checking ammo capacity implicitly limits guns with removable magazines to capacity 0.
    // This gets rounded up to 1 later.
    int remaining_capacity = 0;
    if( target->is_watertight_container() && ammo_obj.made_of( LIQUID ) ) {
        remaining_capacity = target->get_remaining_capacity_for_liquid( ammo_obj, true );
    } else if( target->is_container() && ammo_obj.is_comestible() ) {
        remaining_capacity = ammo_obj.charges_per_volume( target->get_container_capacity() );
        if( !target->is_container_empty() ) {
            remaining_capacity -= target->ammo_remaining();
        }
    } else {
        remaining_capacity = target->ammo_capacity() - target->ammo_remaining();
    }
    if( target->has_flag( flag_RELOAD_ONE ) && !ammo->has_flag( flag_SPEEDLOADER ) ) {
        remaining_capacity = 1;
    }
    if( ammo_obj.type->ammo ) {
        if( ammo_obj.ammo_type() == ammo_plutonium ) {
            remaining_capacity = remaining_capacity / PLUTONIUM_CHARGES +
                                 ( remaining_capacity % PLUTONIUM_CHARGES != 0 );
        }
    }

    bool ammo_by_charges = ammo_obj.is_ammo() || ammo_in_container || ammo->is_comestible();
    int available_ammo = ammo_by_charges ? ammo_obj.charges : ammo_obj.ammo_remaining();
    // constrain by available ammo, target capacity and other external factors (max_qty)
    // @ref max_qty is currently set when reloading ammo belts and limits to available linkages
    qty_ = std::min( { val, available_ammo, remaining_capacity, max_qty } );

    // always expect to reload at least one charge
    qty_ = std::max( qty_, 1 );

}

int item::casings_count() const
{
    int res = 0;

    const_cast<item *>( this )->casings_handle( [&res]( detached_ptr<item> &&it ) {
        ++res;
        return std::move( it );
    } );

    return res ;
}

void item::casings_handle( const std::function < detached_ptr<item>( detached_ptr<item> && ) >
                           &func )
{
    if( !is_gun() ) {
        return;
    }

    contents.casings_handle( func );
}

bool item::reload( Character &who, item &loc, int qty )
{
    if( qty <= 0 ) {
        debugmsg( "Tried to reload zero or less charges" );
        return false;
    }
    item *ammo = &loc;
    if( ammo->is_null() ) {
        debugmsg( "Tried to reload using non-existent ammo" );
        return false;
    }

    item *container = nullptr;
    if( ammo->is_ammo_container() || ammo->is_container() ) {
        container = ammo;
        ammo = &ammo->contents.front();
    }

    if( !is_reloadable_with( ammo->typeId() ) ) {
        return false;
    }

    // limit quantity of ammo loaded to remaining capacity
    int limit = 0;
    if( is_watertight_container() && ammo->made_of( LIQUID ) ) {
        limit = get_remaining_capacity_for_liquid( *ammo, true );
    } else if( is_container() && ammo->is_comestible() ) {
        limit = ammo->charges_per_volume( get_container_capacity() );
        if( !is_container_empty() ) {
            limit -= ammo_remaining();
        }
    } else {
        limit = ammo_capacity() - ammo_remaining();
    }

    if( ammo->ammo_type() == ammo_plutonium ) {
        limit = limit / PLUTONIUM_CHARGES + ( limit % PLUTONIUM_CHARGES != 0 );
    }

    qty = std::min( qty, limit );

    // Lua iranged can_reload callback: blocks reloading before ammo is consumed
    if( const auto *iranged_cb = type->iranged_callbacks ) {
        if( !iranged_cb->call_can_reload( who, *this ) ) {
            return false;
        }
    }

    casings_handle( [&who]( detached_ptr<item> &&e ) {
        return who.i_add_or_drop( std::move( e ) );
    } );

    if( is_magazine() ) {
        qty = std::min( qty, ammo->charges );

        if( is_ammo_belt() ) {
            const auto &linkage = type->magazine->linkage;
            if( linkage && !who.use_charges_if_avail( *linkage, qty ) ) {
                debugmsg( "insufficient linkages available when reloading ammo belt" );
            }
        }

        detached_ptr<item> to_reload = ammo->split( qty );
        bool merged = false;
        for( item *it : contents.all_items_top() ) {
            if( it->merge_charges( std::move( to_reload ) ) ) {
                merged = true;
                break;
            }
        }
        if( !merged ) {
            // NOLINTNEXTLINE(bugprone-use-after-move)
            put_in( std::move( to_reload ) );
        }
    } else if( is_container() ) {
        if( container ) {
            container->on_contents_changed();
        }
        item &cur = *this;
        ammo->attempt_split( 0, [&cur, qty]( detached_ptr<item> &&it ) {
            return cur.fill_with( std::move( it ), qty );
        } );
    } else if( !magazine_integral() ) {
        // if we already have a magazine loaded prompt to eject it
        if( magazine_current() ) {
            //~ %1$s: magazine name, %2$s: weapon name
            std::string prompt = string_format( pgettext( "magazine", "Eject %1$s from %2$s?" ),
                                                magazine_current()->tname(), tname() );

            if( !who.dispose_item( *magazine_current(), prompt ) ) {
                return false;
            }
        }

        put_in( ammo->detach() );
        return true;

    } else {
        if( ammo->has_flag( flag_SPEEDLOADER ) ) {
            curammo = ammo->contents.front().type;
            qty = std::min( qty, ammo->ammo_remaining() );
            ammo->ammo_consume( qty, tripoint_bub_ms::zero() );
            charges += qty;
        } else if( ammo->ammo_type() == ammo_plutonium ) {
            curammo = ammo->type;
            ammo->charges -= qty;

            // any excess is wasted rather than overfilling the item
            charges += qty * PLUTONIUM_CHARGES;
            charges = std::min( charges, ammo_capacity() );
        } else {
            curammo = ammo->type;
            qty = std::min( qty, ammo->charges );
            ammo->charges -= qty;
            charges += qty;
        }
        // we have transfered ammo from the container to the item
        // therefore, we erase the 0-charge item inside container
        // TODO: why don't we just remove 0-charge items?
        if( ammo->charges == 0 && !ammo->has_flag( flag_SPEEDLOADER ) ) {
            ammo->detach();
            if( container != nullptr ) {
                who.inv_restack();
            }
        }
    }

    if( type->iranged_callbacks ) {
        type->iranged_callbacks->call_on_reload( who, *this );
    }

    return true;
}



void item::set_counter( const int value )
{
    item_counter = value;
}

int item::get_counter() const
{
    return item_counter;
}

bool item::has_explicit_turn_timer() const
{
    return is_active() && item_counter > 0 && type->countdown_interval > 0;
}

bool item::has_countdown_timer_type() const
{
    return type->countdown_interval > 0;
}

void item::advance_timer( int n )
{
    if( !has_explicit_turn_timer() || n <= 0 ) {
        return;
    }
    // Decrement counter, clamping at 0.  The countdown_action is deliberately
    // NOT fired here — it will trigger on the next normal process_items() call
    // when the submap re-enters the reality bubble, avoiding side-effects in
    // out-of-bubble context (explosions, spawns, etc.).
    item_counter = std::max( 0, item_counter - n );
}

void item::set_charges( int value )
{
    if( value < 0 ) {
        debugmsg( "Tried to set a negative charges value %d.", value );
        return;
    }
    if( !ammo_types().empty() ) {
        debugmsg( "Tried to set charges on an item with ammo." );
        return;
    }
    charges = value;
}

detached_ptr<item> item::use_charges( detached_ptr<item> &&self, const itype_id &what, int &qty,
                                      std::vector<detached_ptr<item>> &used,
                                      const tripoint_bub_ms &pos, const std::function<bool( const item & )> &filter )
{


    auto handle_item = [&qty, &used, &pos, &what]( detached_ptr<item> &&e ) {
        if( e->is_tool() ) {
            if( e->typeId() == what || ( what == itype_UPS && e->has_flag( flag_IS_UPS ) ) ) {
                int ups_eff_mult = e->type->tool->ups_eff_mult;
                int n = std::min( e->ammo_remaining() * ups_eff_mult, qty );
                int rand_increase = x_in_y( n % ups_eff_mult, ups_eff_mult );
                int really_used = ( n / ups_eff_mult ) + rand_increase;
                qty -= n;

                if( n == e->ammo_remaining() ) {
                    used.push_back( item::spawn( *e ) );
                    e->ammo_consume( really_used, pos );
                } else {
                    detached_ptr<item> split = item::spawn( *e );
                    split->ammo_set( e->ammo_current(), really_used );
                    e->ammo_consume( really_used, pos );
                    used.push_back( std::move( split ) );
                }
            }
        } else if( e->count_by_charges() ) {
            if( e->typeId() == what ) {
                if( e->charges > qty ) {
                    e->charges -= qty;
                    detached_ptr<item> split = item::spawn( *e );
                    split->charges = qty;
                    used.push_back( std::move( split ) );
                    qty = 0;
                } else {
                    qty -= e->charges;
                    used.push_back( std::move( e ) );
                    return detached_ptr<item>();
                }
            }
        }
        return std::move( e );
    };

    item &obj = *self;

    if( qty > 0 && filter( *self ) && self->typeId() == what ) {
        self = handle_item( std::move( self ) );
    }

    obj.remove_items_with( [&qty, &filter, &handle_item]( detached_ptr<item> &&e ) {
        if( qty == 0 ) {
            // found sufficient charges
            return VisitResponse::ABORT;
        }
        if( !filter( *e ) ) {
            return VisitResponse::NEXT;
        }
        item &obj = *e;
        e = handle_item( std::move( e ) );
        if( obj.is_tool() || obj.count_by_charges() ) {
            return VisitResponse::SKIP;
        }
        return VisitResponse::NEXT;
    } );
    return std::move( self );
}

void item::set_snippet( const snippet_id &id )
{
    if( is_null() ) {
        return;
    }
    if( !id.is_valid() ) {
        debugmsg( "there's no snippet with id %s", id.str() );
        return;
    }
    snip_id = id;
}

