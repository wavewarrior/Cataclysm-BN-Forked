#include "item.h"

#include "action_time_scale.h"
#include "active_tile_data_def.h"
#include "ammo.h"
#include "bodypart.h"
#include "cached_item_options.h"
#include "cata_utility.h"
#include "catalua_icallback_actor.h"
#include "character.h"
#include "character_id.h"
#include "character_stat.h"
#include "color.h"
#include "craft_command.h"
#include "damage.h"
#include "debug.h"
#include "drop_token.h"
#include "enums.h"
#include "flag.h"
#include "game.h"
#include "game_constants.h"
#include "gun_mode.h"
#include "int_id.h"
#include "item_factory.h"
#include "item_group.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "kill_tracker.h"
#include "locations.h"
#include "map.h"
#include "material.h"
#include "messages.h"
#include "mtype.h"
#include "pimpl.h"
#include "point.h"
#include "recipe.h"
#include "relic.h"
#include "rng.h"
#include "rot.h"
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
#include "vpart_position.h"
#include "weather.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>

namespace
{

// Returns the default item type, used for the null item (default constructed),
// the returned pointer is always valid, it's never cleared by the @ref Item_factory.
static const itype *nullitem()
{
    static itype nullitem_m;
    return &nullitem_m;
}

} // namespace
item &null_item_reference()
{
    static item result{};
    // reset it, in case a previous caller has changed it
    result = item();
    return result;
}


const int item::INFINITE_CHARGES = INT_MAX;

item::item()
    : contents( this ),
      components( new component_item_location( this ) ),
      bday( calendar::start_of_cataclysm )
{
    type = nullitem();
    charges = 0;
}

item::item( const itype* type, time_point turn, int qty )
    : type( type ),
      contents( this ),
      components( new component_item_location( this ) ),
      bday( turn )
{
    item_vars_ = type->item_vars;
    corpse = has_flag( flag_CORPSE ) ? &mtype_id::NULL_ID().obj() : nullptr;
    item_counter = type->countdown_interval;

    if( qty >= 0 ) {
        charges = qty;
    } else {
        if( type->tool && type->tool->rand_charges.size() > 1 ) {
            const int charge_roll = rng( 1, type->tool->rand_charges.size() - 1 );
            charges =
                rng( type->tool->rand_charges[charge_roll - 1],
                     type->tool->rand_charges[charge_roll] );
        } else {
            charges = type->charges_default();
        }
    }

    if( has_flag( flag_NANOFAB_TEMPLATE ) ) {
        // Define all nanofab subgroups from nanofab_recipes.json
        auto all_groups = item_controller->get_all_group_names();

        // Prepare a vector to hold nanofab groups dynamically
        std::vector<item_group_id> nanofab_groups;

        // Populate it dynamically (this is probably pretty performance intensive, but allows for
        // modded templates)
        for( const auto& group : all_groups ) {
            const std::string& name = group.str();
            if( name.starts_with( "nanofab_template_" ) ) { nanofab_groups.push_back( group ); }
        }

        // Pick one subgroup randomly
        const item_group_id& chosen_group = random_entry( nanofab_groups );

        // Store which subgroup we picked
        set_var( "NANOFAB_GROUP_ID", chosen_group.str() );

        // Gather all possible items from this subgroup
        std::set<const itype *> all_items = item_group::every_possible_item_from( chosen_group );
        std::vector<const itype *> all_items_vec( all_items.begin(), all_items.end() );

        // Legacy compatibility: store the first item ID as fallback
        if( !all_items_vec.empty() ) {
            set_var( "NANOFAB_ITEM_ID", all_items_vec.front()->get_id().str() );
        }
    }


    if( type->gun ) {
        for( const itype_id& mod : type->gun->built_in_mods ) {
            detached_ptr<item> it = item::spawn( mod, turn, qty );
            it->set_flag( flag_IRREMOVABLE );
            put_in( std::move( it ) );
        }
        for( const itype_id& mod : type->gun->default_mods ) { put_in( item::spawn( mod, turn, qty ) ); }

    } else if( type->magazine ) {
        if( type->magazine->count > 0 ) {
            put_in(
                item::spawn( type->magazine->default_ammo, calendar::turn, type->magazine->count ) );
        }

    } else if( goes_bad() ) {
        active = true;
        last_rot_check = bday;

    } else if( type->tool ) {
        if( ammo_remaining() && !ammo_types().empty() ) {
            ammo_set( ammo_default(), ammo_remaining() );
        }
    }

    if( ( type->gun || type->tool ) && !magazine_integral() ) { set_var( "magazine_converted", 1 ); }

    if( !type->snippet_category.empty() ) {
        snip_id = SNIPPET.random_id_from_category( type->snippet_category );
    }

    // item always has any relic properties from itype.
    if( type->relic_data ) { relic_data = type->relic_data; }

    for( const auto& func : type->use_methods | std::views::values ) {
        const auto actor = func.get_actor_ptr();
        if( actor != nullptr ) { actor->on_spawned( *this ); }
    }
}

item::item( const itype_id& id, time_point turn, int qty ): item( & * id, turn, qty ) {}

item::item( const itype* type, time_point turn, default_charges_tag )
    : item( type, turn, type->charges_default() ) {}

item::item( const itype_id& id, time_point turn, default_charges_tag tag ): item( & * id, turn,
            tag ) {}

item::item( const itype* type, time_point turn, solitary_tag )
    : item( type, turn, type->count_by_charges() ? 1 : -1 ) {}

item::item( const itype_id& id, time_point turn, solitary_tag tag ): item( & * id, turn, tag ) {}

namespace
{

static const item *get_most_rotten_component( const item& craft )
{
    const item* most_rotten = nullptr;
    for( const item * const& it : craft.get_components() ) {
        if( it->goes_bad() ) {
            if( !most_rotten || it->get_relative_rot() > most_rotten->get_relative_rot() ) {
                most_rotten = it;
            }
        }
    }
    return most_rotten;
}

} // namespace
item::item(
    const recipe* rec, int qty, std::vector<detached_ptr<item>>&& items,
    std::vector<item_comp>&& selections )
    : item( "craft", calendar::turn, qty )
{
    craft_data_ = cata::make_value<craft_data>();
    craft_data_->making = rec;
    for( detached_ptr<item> &it : items ) { components.push_back( std::move( it ) ); }
    craft_data_->comps_used = std::move( selections );

    if( is_food() ) {
        activate();
        last_rot_check = bday;
        if( goes_bad() ) {
            const item* most_rotten = get_most_rotten_component( *this );
            if( most_rotten ) { set_relative_rot( most_rotten->get_relative_rot() ); }
        }
    }

    for( item * const& component : components ) {
        for( const flag_id& f : component->get_flags() ) {
            if( f->craft_inherit() ) { set_flag( f ); }
        }
        for( const flag_id& f : component->type->get_flags() ) {
            if( f->craft_inherit() ) { set_flag( f ); }
        }
    }
    // this extra section is so that in-progress crafts will correctly display expected flags.
    for( const flag_id& flag : rec->flags_to_delete ) { unset_flag( flag ); }
}

item::item( const item& source )
    : game_object<item>( source ),
      contents( this ),
      components( new component_item_location( this ) )
{
    copy_fields_from( source );

    for( item * const& it : source.contents.all_items_top() ) {
        contents.insert_item( item::spawn( *it ) );
    }

    for( item * const& it : source.components ) { components.push_back( item::spawn( *it ) ); }

    for( const auto& func : type->use_methods | std::views::values ) {
        const auto actor = func.get_actor_ptr();
        if( actor != nullptr ) { actor->on_spawned( *this ); }
    }
}
item &item::operator=( const item& source )
{
    copy_fields_from( source );

    contents.clear_items();

    for( item * const& it : source.contents.all_items_top() ) {
        contents.insert_item( item::spawn( *it ) );
    }

    components.clear();

    for( item * const& it : source.components ) { components.push_back( item::spawn( *it ) ); }

    for( const auto& func : type->use_methods | std::views::values ) {
        const auto actor = func.get_actor_ptr();
        if( actor != nullptr ) { actor->on_spawned( *this ); }
    }

    return *this;
}

void item::on_destroy()
{
    // These are getting left out until it can be deferred better
    // components.on_destroy();
    // contents.on_destroy();
}


item::~item() = default;
void item::copy_fields_from( const item& source )
{
    type = source.type;
    faults = source.faults;
    item_tags = source.item_tags;
    curammo = source.curammo;
    item_vars_ = source.item_vars_;
    corpse = source.corpse;
    corpse_name = source.corpse_name;
    techniques = source.techniques;
    craft_data_ = source.craft_data_;
    relic_data = source.relic_data;
    charges = source.charges;
    energy = source.energy;
    recipe_charges = source.recipe_charges;
    burnt = source.burnt;
    poison = source.poison;
    frequency = source.frequency;
    snip_id = source.snip_id;
    irradiation = source.irradiation;
    item_counter = source.item_counter;
    mission_id = source.mission_id;
    player_id = source.player_id;
    encumbrance_update_ = source.encumbrance_update_;
    rot = source.rot;
    last_rot_check = source.last_rot_check;
    bday = source.bday;
    owner = source.owner;
    old_owner = source.old_owner;
    damage_ = source.damage_;
    light = source.light;
    invlet = source.invlet;
    active = source.active;
    activated_by = source.activated_by;
    is_favorite = source.is_favorite;
}


detached_ptr<item> item::make_corpse(
    const mtype_id& mt, time_point turn, const std::string& name, const int upgrade_time )
{
    if( !mt.is_valid() ) { debugmsg( "tried to make a corpse with an invalid mtype id" ); }

    std::string corpse_type = mt == mtype_id::NULL_ID() ? "corpse_generic_human" : "corpse";

    detached_ptr<item> result = item::spawn( corpse_type, turn );

    result->corpse = &mt.obj();

    if( result->corpse->has_flag( MF_REVIVES ) ) {
        if( one_in( 20 ) ) { result->set_flag( flag_REVIVE_SPECIAL ); }
        result->set_var( "upgrade_time", std::to_string( upgrade_time ) );
    }

    // This is unconditional because the const itemructor above sets result.name to
    // "human corpse".
    result->corpse_name = name;

    return result;
}

void item::convert( const itype_id& new_type )
{
    type = &*new_type;
    relic_data = type->relic_data;
}

void item::deactivate()
{
    if( !is_active() ) {
        return; // no-op
    }

    active = false;
    if( is_tool() ) { type->tool->turns_active = 0; }

    // Is not placed in the world, so either a template of some kind or a temporary item.
    if( !has_position() ) { return; }
    switch( where() ) {
        case item_location_type::map:
            get_map().make_inactive( *this );
            break;
        case item_location_type::vehicle:
            get_map().veh_at( tripoint_bub_ms( position() ) )->vehicle().make_inactive( *this );
            break;
        default:
            break;
    }
}

void item::activate()
{
    if( is_active() ) {
        return; // no-op
    }

    if( type->countdown_interval > 0 ) { set_counter( type->countdown_interval ); }

    active = true;

    // Is not placed in the world, so either a template of some kind or a temporary item.
    if( !has_position() ) { return; }
    switch( where() ) {
        case item_location_type::map:
            get_map().make_active( *this );
            break;
        case item_location_type::vehicle:
            get_map().veh_at( tripoint_bub_ms( position() ) )->vehicle().make_active( *this );
            break;
        default:
            break;
    }
}

bool item::revert( const Character* ch, bool alert )
{
    const auto& tooldata = type->tool;
    // Can't be reverted, prevents destruction of irrevertable items.
    if( !tooldata->revert_to.has_value() ) { return false; }
    if( ch && alert && !tooldata->revert_msg.empty() ) {
        ch->add_msg_if_player( m_info, _( tooldata->revert_msg ), tname() );
    }
    convert( *tooldata->revert_to );
    return true;
}

units::energy item::mod_energy( const units::energy& qty )
{
    if( !is_battery() ) {
        debugmsg( "Tried to set energy of non-battery item" );
        return 0_J;
    }

    units::energy val = energy_remaining() + qty;
    if( val < 0_J ) {
        return val;
    } else if( val > type->battery->max_capacity ) {
        energy = type->battery->max_capacity;
    } else {
        energy = val;
    }
    return 0_J;
}

void item::ammo_set( const itype_id& ammo, int qty )
{
    if( qty < 0 ) {
        // completely fill an integral or existing magazine
        if( magazine_integral() || magazine_current() ) {
            qty = ammo_capacity();

            // else try to add a magazine using default ammo count property if set
        } else if( !magazine_default().is_null() ) {
            item mag( magazine_default() );
            if( mag.type->magazine->count > 0 ) {
                qty = mag.type->magazine->count;
            } else {
                qty = mag.ammo_capacity();
            }
        }
    }

    if( qty <= 0 ) {
        ammo_unset();
        return;
    }

    // handle reloadable tools and guns with no specific ammo type as special case
    if( ( ammo.is_null() && ammo_types().empty() ) || is_money() ) {
        if( ( is_tool() || is_gun() ) && magazine_integral() ) {
            curammo = nullptr;
            charges = std::min( qty, ammo_capacity() );
        }
        return;
    }

    // check ammo is valid for the item
    const itype* atype = &*ammo;
    if( !atype->ammo || !ammo_types().contains( atype->ammo->type ) ) {
        debugmsg( "Tried to set invalid ammo %s[%d] for %s", atype->get_id(), qty, typeId() );
        return;
    }

    if( is_magazine() ) {
        ammo_unset();
        detached_ptr<item> set_ammo =
            item::spawn( ammo, calendar::turn, std::min( qty, ammo_capacity() ) );
        if( has_flag( flag_NO_UNLOAD ) ) {
            set_ammo->set_flag( flag_NO_DROP );
            set_ammo->set_flag( flag_IRREMOVABLE );
        }
        put_in( std::move( set_ammo ) );

    } else if( magazine_integral() ) {
        curammo = atype;
        charges = std::min( qty, ammo_capacity() );

    } else {
        if( !magazine_current() ) {
            itype_id mag = magazine_default();
            if( !mag->magazine ) {
                debugmsg( "Tried to set ammo %s[%d] without suitable magazine for %s",
                          atype->get_id(), qty, typeId() );
                return;
            }

            // if default magazine too small fetch instead closest available match
            if( mag->magazine->capacity < qty ) {
                // as above call to magazine_default successful can infer minimum one option exists
                auto iter = type->magazines.find( atype->ammo->type );
                if( iter == type->magazines.end() ) {
                    debugmsg( "%s doesn't have a magazine for %s", typeId(), ammo );
                    return;
                }
                std::vector<itype_id> opts( iter->second.begin(), iter->second.end() );
                std::ranges::sort( opts, []( const itype_id & lhs, const itype_id & rhs ) {
                    return lhs->magazine->capacity < rhs->magazine->capacity;
                } );
                mag = opts.back();
                for( const itype_id& e : opts ) {
                    if( e->magazine->capacity >= qty ) {
                        mag = e;
                        break;
                    }
                }
            }
            put_in( item::spawn( mag ) );
        }
        magazine_current()->ammo_set( ammo, qty );
    }
}

void item::ammo_unset()
{
    if( !is_tool() && !is_gun() && !is_magazine() ) {
        // do nothing
    } else if( is_magazine() ) {
        contents.clear_items();
    } else if( magazine_integral() ) {
        curammo = nullptr;
        charges = 0;
    } else if( magazine_current() ) {
        magazine_current()->ammo_unset();
    }
}

int item::damage() const { return damage_; }

int item::damage_level( int max ) const
{
    if( damage_ == 0 || max <= 0 ) {
        return 0;
    } else if( max_damage() <= 1 ) {
        return damage_ > 0 ? max : damage_;
    } else if( damage_ < 0 ) {
        return -( ( max - 1 ) * ( -damage_ - 1 ) / ( max_damage() - 1 ) + 1 );
    } else {
        return ( max - 1 ) * ( damage_ - 1 ) / ( max_damage() - 1 ) + 1;
    }
}

void item::set_damage( int qty )
{
    on_damage( qty - damage_, DT_TRUE );
    damage_ = std::max( std::min( qty, max_damage() ), min_damage() );
}

detached_ptr<item> item::split( int qty )
{
    const bool split_from_preserving_container = goes_bad() && is_in_preserving_container();
    if( split_from_preserving_container ) { mark_rot_checked_now(); }
    if( qty <= 0 || !count_by_charges() || qty >= charges ) { return detach(); }
    detached_ptr<item> res = item::spawn( *this );
    res->charges = qty;
    charges -= qty;
    if( split_from_preserving_container ) { res->mark_rot_checked_now(); }
    return res;
}

detached_ptr<item> item::unsafe_split( int qty )
{
    if( !count_by_charges() ) {
        debugmsg( "Attempted to unsafe_split a non-count by charges item." );
        return detached_ptr<item>();
    }
    if( qty == 0 || qty >= charges ) { qty = charges; }
    detached_ptr<item> res = item::spawn( *this );
    res->charges = qty;
    charges -= qty;
    return res;
}

void item::unsafe_rejoin( item& old )
{
    if( old.charges != 0 ) { return; }

    merge_charges( old.detach(), true );
}

bool item::attempt_detach( std::function < detached_ptr<item>( detached_ptr<item> && ) > cb )
{
    if( is_null() ) { return false; }
    if( goes_bad() && is_in_preserving_container() ) { mark_rot_checked_now(); }
    if( count_by_charges() ) { return attempt_split( 0, cb ); }
    return game_object<item>::attempt_detach( cb );
}

bool item::attempt_split(
    int qty, const std::function < detached_ptr<item>( detached_ptr<item> && ) > & cb )
{
    const bool split_from_preserving_container = goes_bad() && is_in_preserving_container();
    if( split_from_preserving_container ) { mark_rot_checked_now(); }
    const bool split_needs_rot_actualization =
        goes_bad() && has_position() && !split_from_preserving_container;
    const auto split_pos = split_needs_rot_actualization ? position() : tripoint_bub_ms::zero();
    const auto vehicle_loc = dynamic_cast<vehicle_item_location *>( loc );
    const auto split_temperature =
        !split_needs_rot_actualization ? temperature_flag::TEMP_NORMAL
        : vehicle_loc != nullptr
        ? vehicle_loc->storage_temperature()
        : rot::temperature_flag_for_location( get_map(), *this );
    detached_ptr<item> det = unsafe_split( qty );
    if( det && split_from_preserving_container ) { det->mark_rot_checked_now(); }
    if( det && split_needs_rot_actualization ) {
        det = actualize_rot( std::move( det ), split_pos, split_temperature, get_weather() );
    }
    if( !det ) {
        if( charges == 0 && has_position() ) { detach().release(); }
        return false;
    }
    item& after_split = *det;
    int starting_charges = after_split.charges;
    det = cb( std::move( det ) );
    bool ret = true;
    bool changed = false;
    if( det ) {
        if( det->type->get_id() != type->get_id() ) {
            debugmsg( "attempt_split returned the wrong item type" );
        } else {
            changed |= det->charges != starting_charges;
            // Copy any changed properties from the new item, except the charges
            int old_charges = charges;
            *this = *det;
            charges = old_charges;
            merge_charges( std::move( det ), true );
        }
        ret = false;
    } else {
        changed = true;
    }
    if( changed ) {
        contents_item_location* contents_loc = dynamic_cast<contents_item_location *>( &*loc );
        if( contents_loc ) { contents_loc->on_changed( this ); }
    }
    after_split.unsafe_rejoin( *this );
    return ret;
}

bool item::is_null() const
{
    // Actually, type should never by null at all.
    return ( type == nullptr || type == nullitem() || typeId().is_null() );
}

bool item::is_unarmed_weapon() const { return has_flag( flag_UNARMED_WEAPON ) || is_null(); }


detached_ptr<item> item::in_its_container( detached_ptr<item>&& self )
{
    return item::
           in_container( self->type->default_container.value_or( itype_id::NULL_ID() ), std::move( self ) );
}

detached_ptr<item> item::in_container( const itype_id& cont, detached_ptr<item>&& self )
{
    if( !cont.is_null() ) {
        detached_ptr<item> ret = item::spawn( cont, self->birthday() );
        ret->invlet = self->invlet;
        item& obj = *self;
        ret->put_in( std::move( self ) );

        if( obj.made_of( LIQUID ) && ret->is_container() ) {
            // Note: we can't use any of the normal container functions as they check the
            // container being suitable (seals, watertight etc.)
            ret->contents.back().charges = obj.charges_per_volume( ret->get_container_capacity() );
        }
        return ret;
    } else {
        return std::move( self );
    }
}

int item::charges_per_volume( const units::volume& vol ) const
{
    if( count_by_charges() ) {
    if( type->volume == 0_ml ) {
            debugmsg( "Item '%s' with zero volume", tname() );
            return INFINITE_CHARGES;
        }
        // Type cast to prevent integer overflow with large volume containers like the cargo
        // dimension
        return vol * static_cast<int64_t>( type->stack_size ) / type->volume;
    } else {
        units::volume my_volume = volume();
        if( my_volume == 0_ml ) {
            debugmsg( "Item '%s' with zero volume", tname() );
            return INFINITE_CHARGES;
        }
        return vol / my_volume;
    }
}

bool item::display_stacked_with( const item& rhs, bool check_components ) const
{
    return !count_by_charges() && stacks_with( rhs, check_components );
}

bool item::stacks_with( const item& rhs, bool check_components, bool skip_type_check ) const
{
    if( !skip_type_check && type != rhs.type ) { return false; }
    if( is_relic() && rhs.is_relic() && !( *relic_data == *rhs.relic_data ) ) { return false; }
    if( is_money() && charges != 0 && rhs.charges != 0 ) {
        // Dealing with nonempty cash cards
        return true;
    }
    // This function is also used to test whether items counted by charges should be merged, for
    // that check the, the charges must be ignored. In all other cases (tools/guns), the charges are
    // important.
    if( !count_by_charges() && charges != rhs.charges ) { return false; }
    if( is_favorite != rhs.is_favorite ) { return false; }

    if( is_corpse() || rhs.is_corpse() ) {
        return this->is_corpse() && rhs.is_corpse() && ( *this->get_mtype() == *rhs.get_mtype() );
    }

    if( damage_ != rhs.damage_ ) { return false; }
    if( burnt != rhs.burnt ) { return false; }
    if( is_active() != rhs.is_active() ) { return false; }
    if( item_tags != rhs.item_tags ) { return false; }
    if( faults != rhs.faults ) { return false; }
    if( techniques != rhs.techniques ) { return false; }
    if( item_vars_ != rhs.item_vars_ ) { return false; }

    if( craft_data_ || rhs.craft_data_ ) {
        // In-progress crafts are always distinct items. Easier to handle for the player,
        // and there shouldn't be that many items of this type around anyway.
        return false;
    }
    if( check_components || is_comestible() || is_craft() ) {
        // Only check if at least one item isn't using the default recipe or is comestible
        if( !components.empty() || !rhs.components.empty() ) {
            if( get_uncraft_components() != rhs.get_uncraft_components() ) { return false; }
        }
    }
    if( contents.num_item_stacks() != rhs.contents.num_item_stacks() ) { return false; }

    if( ammo_current() != rhs.ammo_current() ) { return false; }

    if( goes_bad() && rhs.goes_bad() ) {
        // Stack items that fall into the same "bucket" of freshness.
        // Distant buckets are larger than near ones.

        switch( merge_comestible_mode ) {
            case merge_comestible_t::merge_legacy: {
                std::pair<int, clipped_unit> my_clipped_time_to_rot = clipped_time(
                        get_shelf_life() - rot );
                std::pair<int, clipped_unit> other_clipped_time_to_rot = clipped_time(
                        rhs.get_shelf_life() - rhs.rot );
                if( my_clipped_time_to_rot != other_clipped_time_to_rot ) { return false; }
            }
            break;
            case merge_comestible_t::merge_liquid: {
                if( !made_of( LIQUID ) || !rhs.made_of( LIQUID ) ) { return false; }
            }
            [[fallthrough]];
            default:
                return std::abs( get_relative_rot() - rhs.get_relative_rot() )
                       <= similarity_threshold;
        }

        if( rotten() != rhs.rotten() ) {
            // just to be safe that rotten and unrotten food is *never* stacked.
            return false;
        }
    }

    return contents.stacks_with( rhs.contents );
}

namespace
{

time_duration weighted_averaged_rot( const item* a, const item* b )
{
    const int base_charges = a->charges + b->charges;

    return base_charges > 0
           ? ( a->get_rot() * a->charges + b->get_rot() * b->charges ) / base_charges
           : 0_seconds;
}

} // namespace

bool item::merge_charges( detached_ptr<item>&& rhs, bool force )
{
    if( this == &*rhs ) {
        debugmsg( "Attempted to merge %s with itself.", debug_name() );
        return false;
    }
    if( !count_by_charges() || ( !stacks_with( *rhs ) && !force ) ) { return false; }
    item& obj = *rhs;
    safe_reference<item>::merge( this, &*rhs );
    detached_ptr<item> del = std::move( rhs );

    const auto new_rot = weighted_averaged_rot( this, &obj );

    // Prevent overflow when either item has "near infinite" charges.
    if( charges >= INFINITE_CHARGES / 2 || obj.charges >= INFINITE_CHARGES / 2 ) {
        charges = INFINITE_CHARGES;
        return true;
    }
    // We'll just hope that the item counter represents the same thing for both items
    if( item_counter > 0 || obj.item_counter > 0 ) {
        item_counter =
            ( static_cast<double>( item_counter ) * charges
              + static_cast<double>( obj.item_counter ) * obj.charges )
            / ( charges + obj.charges );
    }
    charges += obj.charges;

    rot = new_rot;
    set_age( std::max( age(), obj.age() ) );

    return true;
}

void item::put_in( detached_ptr<item>&& payload )
{
    if( !payload || payload->typeId() == itype_id::NULL_ID() ) {
        debugmsg( "Tried to insert non-item into %s", debug_name() );
        return;
    }
    if( &*payload == this ) {
        debugmsg( "Tried to put %s inside itself", debug_name().c_str() );
        return;
    }
    contents.insert_item( std::move( payload ) );
}

void item::add_item_with_id( const itype_id& itype, int count )
{
    detached_ptr<item> new_item = item::spawn( itype, calendar::turn, count );
    contents.insert_item( std::move( new_item ) );
}

bool item::has_item_with_id( const itype_id& itype ) const
{
    auto item_contents = contents.all_items_top();
    return std::ranges::any_of( item_contents, [&]( const item * itm ) {
        return itm->typeId() == itype;
    } );
}


detached_ptr<item> item::actualize_rot(
    detached_ptr<item>&& self, const tripoint_bub_ms& pnt, temperature_flag temperature,
    const weather_manager& weather )
{
    // Guard against null or invalid items that can survive save/load cycles
    // during dimension transitions (e.g. zombie items from deferred arena cleanup).
    if( !self || !self->type || self->type == nullitem() ) {
        if( self ) {
            debugmsg( "actualize_rot: skipping item with %s type at %s",
                      self->type ? "null-type" : "null", pnt.to_string() );
        }
        return std::move( self );
    }
    if( self->goes_bad() ) {
        return process_rot( std::move( self ), false, pnt, nullptr, temperature, weather );
    } else if( self->type->container && self->type->container->preserves ) {
        // Containers like tin cans preserves all items inside, they do not rot at all.
        return std::move( self );
    } else if( self->type->container && self->type->container->seals ) {
        // Items inside rot but do not vanish as the container seals them in.
        self->contents.remove_top_items_with(
        [&pnt, &temperature, &weather]( detached_ptr<item>&& it ) {
            if( !it || !it->type || it->type == nullitem() ) { return std::move( it ); }
            if( it->goes_bad() ) {
                it = process_rot( std::move( it ), true, pnt, nullptr, temperature, weather );
            }
            return std::move( it );
        } );
        return std::move( self );
    } else {
        // Check and remove rotten contents, but always keep the container.
        self->contents.remove_top_items_with(
        [&pnt, &temperature, &weather]( detached_ptr<item>&& it ) {
            return actualize_rot( std::move( it ), pnt, temperature, weather );
        } );
        return std::move( self );
    }
}

bool item_ptr_compare_by_charges( const item* left, const item* right )
{
    if( left->contents.empty() ) {
        return false;
    } else if( right->contents.empty() ) {
        return true;
    } else {
        return right->contents.front().charges < left->contents.front().charges;
    }
}

bool item_compare_by_charges( const item& left, const item& right )
{
    return item_ptr_compare_by_charges( &left, &right );
}

bool item::is_stackable() const
{
    return type->is_stackable();
}

// Get the hearing protection provided by this item.
// Returns advanced (active) hearing protection if true.
// Advanced hearing protection does not make it harder for the character to hear other sounds.
int item::get_hearing_protection( bool advanced ) const
{
    if( this->is_armor() ) {
    const islot_armor* armor = find_armor_data();
        if( armor == nullptr ) {
            return 0;
        }
        return ( advanced ) ? armor->adv_hearing_protection : armor->hearing_protection;
    } else {
        return 0;
    }
}

bool item::can_put_in_bandolier( const item& obj, bool ) const
{
    if( !type->can_use( "bandolier" ) ) {
    return false; // item is not a holster
}

const auto* ptr = dynamic_cast<const bandolier_actor *>
                  ( type->get_use( "bandolier" )->get_actor_ptr() );
if( !ptr->can_store( *this, obj ) ) {
    return false; // item is not a suitable holster for obj
}

return true;
}
