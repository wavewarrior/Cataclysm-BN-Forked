#include "character.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "activity_actor_definitions.h"
#include "activity_handlers.h"
#include "avatar.h"
#include "bionics.h"
#include "bodypart.h"
#include "catalua_icallback_actor.h"
#include "character_encumbrance.h"
#include "character_functions.h"
#include "clothing_utils.h"
#include "debug.h"
#include "flag.h"
#include "game.h"
#include "item.h"
#include "itype.h"
#include "map.h"
#include "map_selector.h"
#include "messages.h"
#include "monster.h"
#include "morale.h"
#include "mutation.h"
#include "npc.h"
#include "options.h"
#include "output.h"
#include "player.h"
#include "translations.h"
#include "type_id.h"
#include "units.h"
#include "vehicle.h"
#include "vehicle_selector.h"
#include "vpart_position.h"

static const trait_id trait_ANTENNAE( "ANTENNAE" );
static const trait_id trait_ANTLERS( "ANTLERS" );
static const trait_id trait_WOOLALLERGY( "WOOLALLERGY" );
static const bionic_id bio_storage( "bio_storage" );
static const trait_id trait_DEBUG_STORAGE( "DEBUG_STORAGE" );
static const trait_id trait_HORNS_POINTED( "HORNS_POINTED" );
static const trait_id trait_SHELL( "SHELL" );
static const trait_id trait_SHELL2( "SHELL2" );

detached_ptr<item> Character::wear_item(
    detached_ptr<item>&& wear, bool interactive,
    std::optional<location_vector<item>::iterator> position )
{
    if( !wear ) { return std::move( wear ); }
    item& to_wear = *wear;
    const auto ret = can_wear( to_wear );
    if( !ret.success() ) {
        if( interactive ) { add_msg_if_player( m_info, "%s", ret.c_str() ); }
        return std::move( wear );
    }

    // Lua iwearable can_wear callback
    if( const auto * iwear_cb = to_wear.type->iwearable_callbacks ) {
        if( !iwear_cb->call_can_wear( *this, to_wear ) ) { return std::move( wear ); }
    }

    const bool was_deaf = is_deaf();
    const bool supertinymouse = get_size() == creature_size::tiny;
    const bool size_matters = to_wear.get_sizing( *this ) != item::sizing::ignore;
    last_item = to_wear.typeId();


    location_vector<item>::iterator pos = position.value_or( position_to_wear_new_item( to_wear ) );
    worn.insert( std::move( pos ), std::move( wear ) );

    if( interactive ) {
        add_msg_player_or_npc(
            _( "You put on your %s." ), _( "<npcname> puts on their %s." ), to_wear.tname() );
        moves -= item_wear_cost( to_wear );

        for( const body_part bp : all_body_parts ) {
            if( to_wear.covers( convert_bp( bp ) ) && encumb( convert_bp( bp ) ) >= 40 ) {
                add_msg_if_player(
                    m_warning,
                    bp == bp_eyes
                    ? _( "Your %s are very encumbered!  %s" )
                    : _( "Your %s is very encumbered!  %s" ),
                    body_part_name( bp ), encumb_text( bp ) );
            }
        }
        if( !was_deaf && is_deaf() ) { add_msg_if_player( m_info, _( "You're deafened!" ) ); }
        if( size_matters && supertinymouse && !to_wear.has_flag( flag_UNDERSIZE )
            && !to_wear.has_flag( flag_resized_small ) ) {
            add_msg_if_player(
                m_warning,
                _( "This %s is too big to wear comfortably!  Maybe it could be refitted." ),
                to_wear.tname() );
        } else if( size_matters && !supertinymouse
                   && ( to_wear.has_flag( flag_UNDERSIZE ) || to_wear.has_flag( flag_resized_small ) ) ) {
            add_msg_if_player(
                m_warning,
                _( "This %s is too small to wear comfortably!  Maybe it could be refitted." ),
                to_wear.tname() );
        }
    } else {
        add_msg_if_npc( _( "<npcname> puts on their %s." ), to_wear.tname() );
    }

    to_wear.on_wear( *this );

    inv.update_invlet( to_wear );
    inv.update_invlet_cache_with_item( to_wear );

    recalc_sight_limits();
    reset_encumbrance();
    // wearing a splint can change perceived pain without directly modifying pain
    // update morale just in case
    morale->on_stat_change( "perceived_pain", get_perceived_pain() );

    return detached_ptr<item>();
}

void Character::add_worn( detached_ptr<item>&& wear )
{
    if( !wear ) { return; }
    item& to_wear = *wear;
    location_vector<item>::iterator pos = position_to_wear_new_item( to_wear );
    worn.insert( pos, std::move( wear ) );
    to_wear.on_wear( *this );
    inv.update_invlet( to_wear );
    inv.update_invlet_cache_with_item( to_wear );
    recalc_sight_limits();
    reset_encumbrance();
}

std::vector<item *> Character::nearby( const std::function<bool( item*, item* )> &func,
                                       int radius )
{
    std::vector<item *> res;

    visit_items( [&]( item * e, item * parent ) {
        if( func( e, parent ) ) { res.emplace_back( e ); }
        return VisitResponse::NEXT;
    } );

    for( auto& cur : map_selector( bub_pos(), radius ) ) {
        cur.visit_items( [&]( item * e, item * parent ) {
            if( func( e, parent ) ) { res.emplace_back( e ); }
            return VisitResponse::NEXT;
        } );
    }

    for( auto& cur : vehicle_selector( bub_pos(), radius ) ) {
        cur.visit_items( [&]( item * e, item * parent ) {
            if( func( e, parent ) ) { res.emplace_back( e ); }
            return VisitResponse::NEXT;
        } );
    }

    return res;
}

int Character::amount_worn( const itype_id& id ) const
{
    int amount = 0;
    for( auto& elem : worn ) {
        if( elem->typeId() == id ) { ++amount; }
    }
    return amount;
}
detached_ptr<item> Character::i_add_to_container( detached_ptr<item>&& it, const bool unloading )
{
    if( !it->is_ammo() || unloading ) { return std::move( it ); }

    const itype_id item_type = it->typeId();
    auto add_to_container = [&it]( item & container ) {
        auto& contained_ammo = container.contents.front();
        if( contained_ammo.charges < container.ammo_capacity() ) {
            const int diff = container.ammo_capacity() - contained_ammo.charges;
            //~ %1$s: item name, %2$s: container name
            add_msg( pgettext( "container", "You put the %1$s in your %2$s." ), it->tname(),
                     container.tname() );
            if( diff >= it->charges ) {
                contained_ammo.merge_charges( std::move( it ) );
            } else {
                it->charges -= diff;
                contained_ammo.charges = container.ammo_capacity();
            }
        }
    };

    visit_items( [&]( item * item ) {
        if( it && item->is_ammo_container() && item_type == item->contents.front().typeId() ) {
            add_to_container( *item );
            item->handle_pickup_ownership( *this );
        }
        return VisitResponse::NEXT;
    } );

    return std::move( it );
}

item &Character::i_add( detached_ptr<item>&& it, bool should_stack )
{
    itype_id item_type_id = it->typeId();
    last_item = item_type_id;

    if( it->is_food() || it->is_ammo() || it->is_gun() || it->is_armor() || it->is_book()
        || it->is_tool() || it->is_melee() || it->is_food_container() ) {
        inv.unsort();
    }

    // if there's a desired invlet for this item type, try to use it
    bool keep_invlet = false;
    const invlets_bitset cur_inv = allocated_invlets();
    for( auto iter : inv.assigned_invlet ) {
        if( iter.second == item_type_id && !cur_inv[iter.first] ) {
            it->invlet = iter.first;
            keep_invlet = true;
            break;
        }
    }

    item& item_in_inv = inv.add_item( std::move( it ), keep_invlet, true, should_stack );
    item_in_inv.on_pickup( *this );

    clear_npc_ai_info_cache( npc_ai_info::reloadables );
    clear_npc_ai_info_cache( npc_ai_info::reloadable_cbms );
    return item_in_inv;
}

void Character::remove_worn_items_with(
    const std::function < detached_ptr<item>( detached_ptr<item> && ) > & filter )
{
    worn.remove_with( [this, filter]( detached_ptr<item>&& it ) {
        item& obj = *it;
        it = filter( std::move( it ) );
        if( !it ) { obj.on_takeoff( *this ); }
        return std::move( it );
    } );
}

item *Character::invlet_to_item( const int linvlet )
{
    // Invlets may come from curses, which may also return any kind of key codes, those being
    // of type int and they can become valid, but different characters when casted to char.
    // Example: KEY_NPAGE (returned when the player presses the page-down key) is 0x152,
    // casted to char would yield 0x52, which happens to be 'R', a valid invlet.
    if( linvlet > std::numeric_limits<char>::max() || linvlet < std::numeric_limits<char>::min() ) {
        return nullptr;
    }
    const char invlet = static_cast<char>( linvlet );
    item* invlet_item = nullptr;
    visit_items( [&invlet, &invlet_item]( item * it ) {
        if( it->invlet == invlet ) {
            invlet_item = it;
            return VisitResponse::ABORT;
        }
        // Visit top-level items only as UIs don't support nested items.
        // Also, inventory restack logic depends on this.
        return VisitResponse::SKIP;
    } );
    return invlet_item;
}

// Negative positions indicate weapon/clothing, 0 & positive indicate inventory
const item &Character::i_at( int position ) const
{
    if( position == -1 ) { return primary_weapon(); }
    if( position < -1 ) {
        int worn_index = worn_position_to_index( position );
        if( static_cast<size_t>( worn_index ) < worn.size() ) {
            auto iter = worn.begin();
            std::advance( iter, worn_index );
            return **iter;
        }
    }

    return inv.find_item( position );
}

item &Character::i_at( int position )
{
    return const_cast<item &>( const_cast<const Character*>( this )->i_at( position ) );
}

int Character::get_item_position( const item* it ) const
{
    const item& weapon = primary_weapon();
    if( weapon.has_item( *it ) ) { return -1; }

    int p = 0;
    for( const auto& e : worn ) {
        if( e->has_item( *it ) ) { return worn_position_to_index( p ); }
        p++;
    }

    return inv.position_by_item( it );
}

const std::vector<item *> &Character::inv_const_stack( int position ) const
{
    return inv.const_stack( position );
}

const_invslice Character::inv_const_slice() const { return inv.const_slice(); }

size_t Character::inv_size() const { return inv.size(); }

void Character::inv_restack() { inv.restack( *dynamic_cast<player*>( this ) ); }

void Character::inv_assign_empty_invlet( item& it, bool force )
{
    inv.assign_empty_invlet( it, *this, force );
}

void Character::inv_reassign_item( item& it, char invlet, bool remove_old )
{
    inv.reassign_item( it, invlet, remove_old );
}

int Character::inv_invlet_to_position( char invlet ) const { return inv.invlet_to_position( invlet ); }

void Character::rust_iron_items() { inv.rust_iron_items(); }

void Character::inv_clear() { inv.clear(); }

void Character::dump_inv( std::vector<item*> &to ) { inv.dump( to ); }

int Character::inv_position_by_item( item* it ) const { return inv.position_by_item( it ); }

void Character::inv_update_invlet( item& it ) { inv.update_invlet( it ); }

void Character::inv_update_invlet_cache_with_item( item& it )
{
    inv.update_invlet_cache_with_item( it );
}

std::map<char, itype_id> &Character::inv_assigned_invlet() { return inv.assigned_invlet; }

int Character::inv_position_by_type( const itype_id& type ) const
{
    return inv.position_by_type( type );
}

item &Character::inv_find_item( int position ) { return inv.find_item( position ); }

const item &Character::inv_find_item( int position ) const { return inv.find_item( position ); }

void Character::inv_set_stack_favorite( int position, bool favorite )
{
    inv.set_stack_favorite( position, favorite );
}

units::volume Character::inv_volume() const { return inv.volume(); }

void Character::inv_unsort() { inv.unsort(); }

detached_ptr<item> Character::inv_remove_item( item* it ) { return inv.remove_item( it ); }

detached_ptr<item> Character::i_rem( int pos )
{
    if( pos == -1 ) {
        return remove_primary_weapon();
    } else if( pos < -1 && pos > worn_position_to_index( worn.size() ) ) {
        auto iter = worn.begin();
        std::advance( iter, worn_position_to_index( pos ) );
        item* tmp = *iter;
        tmp->on_takeoff( *this );
        detached_ptr<item> ret;
        worn.erase( iter, &ret );
        return ret;
    }
    return inv.remove_item( pos );
}

detached_ptr<item> Character::i_rem_keep_contents( const int idx )
{
    detached_ptr<item> ret = i_rem( idx );
    ret->spill_contents( bub_pos() );
    return ret;
}

detached_ptr<item> Character::i_add_or_drop( detached_ptr<item>&& it )
{
    if( it->made_of( LIQUID ) || !can_pick_weight( *it, !get_option<bool>( "DANGEROUS_PICKUPS" ) )
        || !can_pick_volume( *it ) ) {
        return get_map().add_item_or_charges( bub_pos(), std::move( it ) );
    } else {
        inv.assign_empty_invlet( *it, *this );
        i_add( std::move( it ) );
        return detached_ptr<item>();
    }
}

std::list<item *> Character::get_dependent_worn_items( const item& it ) const
{
    std::list<item *> dependent;
    // Adds dependent worn items recursively
    const std::function<void( const item& it )> add_dependent = [&]( const item & it ) {
        for( const item * const& wit : worn ) {
            if( wit == &it || !wit->is_worn_only_with( it ) ) { continue; }
            const auto iter = std::ranges::find_if( dependent, [&wit]( const item * dit ) {
                return wit == dit;
            } );
            if( iter == dependent.end() ) { // Not in the list yet
                add_dependent( *wit );
                dependent.push_back( const_cast<item*>( wit ) );
            }
        }
    };

    if( is_worn( it ) ) { add_dependent( it ); }

    return dependent;
}

void Character::drop( item& loc, const tripoint_bub_ms& where )
{
    if( is_wielding( loc ) ) {
        const auto ret = can_unwield( loc );

        if( !ret.success() ) {
            add_msg( m_info, "%s", ret.c_str() );
            return;
        }
    } else if( is_wearing( loc ) ) {
        const auto ret = as_player()->can_takeoff( loc );

        if( !ret.success() ) {
            add_msg( m_info, "%s", ret.c_str() );
            return;
        }
    }

    drop( {drop_location( loc, loc.count() )}, where );
}

void Character::drop( const drop_locations& what, const tripoint_bub_ms& target, bool stash )
{
    if( what.empty() ) { return; }

    if( rl_dist( bub_pos(), target ) > 1 || !( stash || get_map().can_put_items( target ) ) ) {
        add_msg_player_or_npc(
            m_info, _( "You can't place items here!" ), _( "<npcname> can't place items here!" ) );
        return;
    }

    if( stash ) {
        assign_activity( std::make_unique<player_activity>(
                             std::make_unique<stash_activity_actor>( *this, what, target - bub_pos() ) ) );
    } else {
        assign_activity( std::make_unique<player_activity>(
                             std::make_unique<drop_activity_actor>( *this, what, false, target - bub_pos() ) ) );
    }
}

invlets_bitset Character::allocated_invlets() const
{
    invlets_bitset invlets = inv.allocated_invlets();

    const item& weapon = primary_weapon();
    invlets.set( weapon.invlet );
    for( const auto& w : worn ) { invlets.set( w->invlet ); }

    invlets[0] = false;

    return invlets;
}
bool Character::has_active_item_with_action( const std::string& use ) const
{
    return has_item_with( [use]( const item & it ) { return it.get_use( use ) && it.is_active(); } );
}

bool Character::has_active_item( const itype_id& id ) const
{
    return has_item_with( [id]( const item & it ) { return it.is_active() && it.typeId() == id; } );
}


bool Character::has_mission_item( int mission_id ) const
{
    return mission_id != -1
    && has_item_with( [&mission_id]( const item & it ) { return it.mission_id == mission_id; } );
}

void Character::remove_mission_items( int mission_id )
{
    if( mission_id == -1 ) { return; }
    remove_items_with( has_mission_item_filter{mission_id} );
}

units::mass Character::weight_carried() const { return weight_carried_reduced_by( {} ); }

units::volume Character::volume_carried() const { return inv.volume(); }

int Character::best_nearby_lifting_assist() const { return best_nearby_lifting_assist( bub_pos() ); }

int Character::best_nearby_lifting_assist( const tripoint_bub_ms& world_pos ) const
{
    const quality_id LIFT( "LIFT" );
    int mech_lift = 0;
    if( is_mounted() ) {
    auto mons = mounted_creature.get();
        if( mons->has_flag( MF_RIDEABLE_MECH ) ) {
            mech_lift = mons->mech_str_addition() + 10;
        }
    }
    return std::max( {
        this->max_quality( LIFT ), mech_lift,
        map_selector( bub_pos(), PICKUP_RANGE, false ).max_quality( LIFT ),
        vehicle_selector( world_pos, PICKUP_RANGE, false ).max_quality( LIFT )} );
}

units::mass Character::weight_carried_reduced_by( const excluded_stacks& without ) const
{
    const std::map<const item *, int> empty;

    // Worn items
    units::mass ret = 0_gram;
    for( auto& i : worn ) {
        if( !without.contains( i ) ) { ret += i->weight(); }
    }

    // Items in inventory
    ret += inv.weight_without( without );

    // Wielded item
    units::mass weaponweight = 0_gram;
    int subtract_count = 0;
    item& weapon = primary_weapon();
    auto weapon_it = without.find( &weapon );
    if( weapon_it == without.end() ) {
        weaponweight = weapon.weight();
    } else {
        subtract_count = ( *weapon_it ).second;
        if( weapon.count_by_charges() ) {
            weapon.charges -= subtract_count;
            if( weapon.charges < 0 ) {
                debugmsg( "Trying to remove more charges than the wielded item has" );
                // Set subtract_count to the original value of weapon->charges, so that it's set
                // back correctly at the end
                subtract_count += weapon.charges;
                weapon.charges = 0;
            }
            weaponweight = weapon.weight();
        } else if( subtract_count > 1 ) {
            debugmsg( "Trying to remove more than one wielded item" );
        } else {
            subtract_count = 0;
        }
    }
    // Don't try to add weaponweight if it doesn't exist or is weightless
    if( weaponweight > 0_gram ) {
        // Exclude wielded item if using lifting tool
        if( weaponweight + ret > weight_capacity() ) {
            const float liftrequirement = std::ceil(
                                              units::to_gram<float>( weaponweight ) / units::to_gram<float>( TOOL_LIFT_FACTOR ) );
            if( g->new_game || best_nearby_lifting_assist() < liftrequirement ) {
                ret += weaponweight;
            }
        } else {
            ret += weaponweight;
        }
    }
    weapon.charges += subtract_count;
    return ret;
}

units::volume Character::volume_carried_reduced_by( const excluded_stacks &without ) const
{
    if( without.empty() ) {
    return inv.volume();
    } else {
        return inv.volume_without( without );
    }
}

units::mass Character::weight_capacity() const
{
    if( has_trait( trait_DEBUG_STORAGE ) ) {
    // Infinite enough
    return units::mass_max;
}
// Get base capacity from creature,
// then apply player-only mutation and trait effects.
units::mass ret = Creature::weight_capacity();
/** @EFFECT_STR increases carrying capacity */
ret += get_str() * 4_kilogram;
ret *= mutation_value( "weight_capacity_modifier" );

units::mass worn_weight_bonus = 0_gram;
for( const item * const &it : worn ) {
    ret *= it->get_weight_capacity_modifier();
        worn_weight_bonus += it->get_weight_capacity_bonus();
    }

    units::mass bio_weight_bonus = 0_gram;
for( const bionic &i : get_bionic_collection() ) {
    const bionic_id &bid = i.id;
    ret *= bid->weight_capacity_modifier;
    bio_weight_bonus +=  bid->weight_capacity_bonus;
}

ret += bio_weight_bonus + worn_weight_bonus;

if( has_artifact_with( AEP_CARRY_MORE ) ) {
    ret += 22500_gram;
}

if( ret < 0_gram ) {
    ret = 0_gram;
}
if( is_mounted() ) {
    auto *mons = mounted_creature.get();
        // the mech has an effective strength for other purposes, like hitting.
        // but for lifting, its effective strength is even higher, due to its sturdy construction,
        // leverage, and being built entirely for that purpose with hydraulics etc.
        ret = mons->mech_str_addition() == 0 ? ret : ( mons->mech_str_addition() + 10 ) * 4_kilogram;
    }
    return ret;
}

units::volume Character::volume_capacity() const { return volume_capacity_reduced_by( 0_ml ); }

units::volume Character::volume_capacity_reduced_by(
    const units::volume &mod, const excluded_stacks &without ) const
{
    if( has_trait( trait_DEBUG_STORAGE ) ) {
    return units::volume_max;
}

units::volume ret = -mod;
for( const auto &i : worn ) {
    if( !without.contains( i ) ) {
            ret += i->get_storage();
        }
    }
    if( has_bionic( bio_storage ) ) {
    ret += 2_liter;
}
if( has_trait( trait_SHELL ) ) {
    ret += 4_liter;
}
if( has_trait( trait_SHELL2 ) && !has_active_mutation( trait_SHELL2 ) ) {
    ret += 6_liter;
}

ret = ret * mutation_value( "packmule_modifier" );

return std::max( ret, 0_ml );
}

bool Character::can_pick_volume( const item& it ) const
{
    return inv.volume() + it.volume() <= volume_capacity();
}

bool Character::can_pick_volume( units::volume volume ) const
{
    // Might not be 100% true because some items restack to a very tiny bit less
    // but close enough not to matter
    return inv.volume() + volume <= volume_capacity();
}

bool Character::can_pick_weight( const item& it, bool safe ) const
{
    return can_pick_weight( it.weight(), safe );
}

bool Character::can_pick_weight( units::mass weight, bool safe ) const
{
    if( !safe ) {
    // Character can carry up to four times their maximum weight
    return ( weight_carried() + weight <= ( has_trait( trait_DEBUG_STORAGE ) ?
                                            units::mass_max : weight_capacity() * 4 ) );
    } else {
        return ( weight_carried() + weight <= weight_capacity() );
    }
}

bool Character::can_use( const item& it, const item* context ) const
{
    const auto& ctx = context ? *context : it;

    if( !meets_requirements( it, &ctx ) ) {
        const std::string unmet( enumerate_unmet_requirements( it, &ctx ) );

        if( &it == &ctx ) {
            //~ %1$s - list of unmet requirements, %2$s - item name.
            add_msg_player_or_npc(
                m_bad, _( "You need at least %1$s to use this %2$s." ),
                _( "<npcname> needs at least %1$s to use this %2$s." ), unmet, it.tname() );
        } else {
            //~ %1$s - list of unmet requirements, %2$s - item name, %3$s - indirect item name.
            add_msg_player_or_npc(
                m_bad, _( "You need at least %1$s to use this %2$s with your %3$s." ),
                _( "<npcname> needs at least %1$s to use this %2$s with their %3$s." ), unmet,
                it.tname(), ctx.tname() );
        }

        return false;
    }

    return true;
}

ret_val<bool> Character::can_wear( const item &it, bool with_equip_change ) const
{
    if( !it.is_armor() ) {
    return ret_val<bool>::make_failure( _( "Putting on a %s would be tricky." ), it.tname() );
    }

    if( has_trait( trait_WOOLALLERGY ) && ( it.made_of( material_id( "wool" ) ) ||
                                                it.has_own_flag( flag_wooled ) ) ) {
        return ret_val<bool>::make_failure( _( "Can't wear that, it's made of wool!" ) );
    }


    if( !it.has_flag( flag_SEMITANGIBLE ) ) {
    for( const trait_id &mut : get_mutations() ) {
            const auto &branch = mut.obj();
            if( branch.conflicts_with_item( it ) ) {
                return ret_val<bool>::make_failure( is_player() ?
                                                    _( "Your %s mutation prevents you from wearing your %s." ) :
                                                    _( "My %s mutation prevents me from wearing this %s." ), branch.name(),
                                                    it.type_name() );
            }
        }
        if( it.covers( bodypart_id( "head" ) ) && !it.has_flag( flag_SEMITANGIBLE )
            && !it.made_of( material_id( "wool" ) ) && !it.made_of( material_id( "cotton" ) )
            && !it.made_of( material_id( "nomex" ) ) && !it.made_of( material_id( "leather" ) )
            && ( has_trait( trait_HORNS_POINTED ) || has_trait( trait_ANTENNAE )
                 || has_trait( trait_ANTLERS ) ) ) {
            return ret_val<bool>::make_failure(
                       _( "Cannot wear a helmet over %s." ),
                       ( has_trait( trait_HORNS_POINTED )
                         ? _( "horns" )
                         : ( has_trait( trait_ANTENNAE ) ? _( "antennae" ) : _( "antlers" ) ) ) );
        }
    }

    if( it.has_flag( flag_SPLINT ) ) {
    bool need_splint = false;
    for( const bodypart_id &bp : get_all_body_parts() ) {
            if( !it.covers( bp ) ) {
                continue;
            }
            if( is_limb_broken( bp ) && !worn_with_flag( flag_SPLINT, bp ) ) {
                need_splint = true;
                break;
            }
        }
        if( !need_splint ) {
            return ret_val<bool>::make_failure(
                       is_player() ? _( "You don't have any broken limbs this could help." )
                       : _( "%s doesn't have any broken limbs this could help." ),
                       name );
        }
    }

    if( it.has_flag( flag_RESTRICT_HANDS ) && !has_two_arms() ) {
    return ret_val<bool>::make_failure( ( is_player() ? _( "You don't have enough arms to wear that." )
                                          : string_format( _( "%s doesn't have enough arms to wear that." ), name ) ) );
    }

    if( it.has_flag( flag_FLOTATION ) && get_map().has_flag( TFLAG_WATER_CUBE, bub_pos() ) ) {
        return ret_val<bool>::make_failure( (
                                                is_player()
                                                ? _( "You can't put on a flotation device below the surface." )
                                                : string_format( _( "%s can't put on a flotation device below the surface." ), name ) ) );
    }

    //Everything checked after here should be something that could be solved by changing equipment
    if( with_equip_change ) {
    return ret_val<bool>::make_success();
    }

    if( it.is_power_armor() ) {
    for( auto &elem : worn ) {
            if( elem->get_covered_body_parts().make_intersection( it.get_covered_body_parts() ).any() &&
                !elem->has_flag( flag_POWERARMOR_COMPATIBLE ) && !elem->is_power_armor() ) {
                return ret_val<bool>::make_failure( _( "Can't wear power armor over other gear!" ) );
            } else if( elem->has_flag( flag_POWERARMOR_EXO ) && it.has_flag( flag_POWERARMOR_EXO ) ) {
                return ret_val<bool>::make_failure( _( "Can't wear multiple exoskeletons!" ) );
            }
        }
        if( !it.has_flag( flag_POWERARMOR_EXO ) && !is_wearing_power_armor() ) {
            return ret_val<bool>::make_failure( _(
                                                    "You can only wear power armor components with "
                                                    "power armor!" ) );
        }
        if( it.has_flag( flag_POWERARMOR_EXTERNAL ) ) {
            for( auto& elem : worn ) {
                if( elem->has_flag( flag_POWERARMOR_EXO )
                    && elem->get_covered_body_parts()
                    .make_intersection( it.get_covered_body_parts() )
                    .any() ) {
                    return ret_val<bool>::make_failure( _(
                                                            "Can't wear externals over an "
                                                            "exoskeleton!" ) );
                } else if(
                    elem->has_flag( flag_POWERARMOR_EXTERNAL )
                    && elem->get_covered_body_parts()
                    .make_intersection( it.get_covered_body_parts() )
                    .any() ) {
                    return ret_val<bool>::make_failure( _( "Can't wear externals over one another!" ) );
                }
            }
        }
        if( it.has_flag( flag_POWERARMOR_MOD ) ) {
            int max_layer = 2;
            std::vector<std::pair<bodypart_str_id, int>> mod_parts;
            std::vector<std::pair<bodypart_str_id, bool>> attachments;
            bool lhs = false;
            bool rhs = false;
            const auto& all_bps = get_all_body_parts();
            for( const bodypart_id& bp : all_bps ) {
                if( it.get_covered_body_parts().test( bp.id() ) ) {
                    mod_parts.emplace_back( bp, 0 );
                    attachments.emplace_back( bp, false );
                }
            }
            for( auto& elem : worn ) {
                // To check if there's an external/exoskeleton for the mod to attach to.
                for( std::pair<bodypart_str_id, bool> &attachment : attachments ) {
                    if( elem->get_covered_body_parts().test( attachment.first )
                        && ( elem->has_flag( flag_POWERARMOR_EXO )
                             || elem->has_flag( flag_POWERARMOR_EXTERNAL ) ) ) {
                        if( elem->is_sided() && elem->get_side() == attachment.first->part_side ) {
                            attachment.second = true;
                        } else {
                            attachment.second = true;
                        }
                    }
                }
                // To check how many mods are on a given part.
                for( std::pair<bodypart_str_id, int> &mod_part : mod_parts ) {
                    if( elem->get_covered_body_parts().test( mod_part.first )
                        && elem->has_flag( flag_POWERARMOR_MOD ) ) {
                        if( elem->is_sided() && elem->get_side() == mod_part.first->part_side ) {
                            mod_part.second++;
                        } else {
                            mod_part.second++;
                        }
                    }
                }
            }
            for( std::pair<bodypart_str_id, bool> &attachment : attachments ) {
                if( !attachment.second ) {
                    return ret_val<bool>::make_failure( _( "Nothing to attach the mod to!" ) );
                }
            }
            for( std::pair<bodypart_str_id, int> &mod_part : mod_parts ) {
                if( mod_part.first == body_part_torso ) { max_layer = 3; }
                if( mod_part.second >= max_layer ) {
                    if( !it.is_sided() || mod_part.first->part_side == side::BOTH ) {
                        return ret_val<bool>::make_failure( _(
                                                                "Can't wear any more mods on that "
                                                                "body part!" ) );
                    } else {
                        if( mod_part.first->part_side == side::LEFT ) {
                            lhs = true;
                        } else {
                            rhs = true;
                        }
                        if( lhs && rhs ) {
                            return ret_val<bool>::make_failure( _( "No more space for that mod!" ) );
                        }
                    }
                }
            }
        }
    } else {
        // Only headgear can be worn with power armor, except other power armor components.
        // You can't wear headgear if power armor helmet is already sitting on your head.
        for( auto& elem : worn ) {
            if( !it.has_flag( flag_POWERARMOR_COMPATIBLE )
                && ( is_wearing_power_armor()
                     && elem->get_covered_body_parts()
                     .make_intersection( it.get_covered_body_parts() )
                     .any() ) ) {
                return ret_val <
                       bool >::make_failure( _( "Can't wear %s with power armor!" ), it.tname() );
            }
        }
    }

    // Check if we don't have both hands available before wearing a briefcase, shield, etc. Also
    // occurs if we're already wearing one.
    const item& weapon = primary_weapon();
    if( it.has_flag( flag_RESTRICT_HANDS )
        && ( worn_with_flag( flag_RESTRICT_HANDS ) || weapon.is_two_handed( *this ) ) ) {
        return ret_val<bool>::make_failure( (
                                                is_player() ? _( "You don't have a hand free to wear that." )
                                                : string_format( _( "%s doesn't have a hand free to wear that." ), name ) ) );
    }

for( auto &i : worn ) {
    if( i->has_flag( flag_ONLY_ONE ) && i->typeId() == it.typeId() ) {
            return ret_val<bool>::make_failure( _( "Can't wear more than one %s!" ), it.tname() );
        }
    }

for( auto &i : worn ) {
    if( i->has_flag( flag_EXOSUIT ) && it.has_flag( flag_EXOSUIT ) ) {
            return ret_val<bool>::make_failure( _( "Can't wear more than one exosuit!" ) );
        }
    }

    if( amount_worn( it.typeId() ) >= MAX_WORN_PER_TYPE ) {
        return ret_val<bool>::make_failure(
                   _( "Can't wear %i or more %s at once." ), MAX_WORN_PER_TYPE + 1,
                   it.tname( MAX_WORN_PER_TYPE + 1 ) );
    }

    if( ( ( it.covers( bodypart_id( "foot_l" ) ) && is_wearing_shoes( side::LEFT ) ) ||
              ( it.covers( bodypart_id( "foot_r" ) ) && is_wearing_shoes( side::RIGHT ) ) ) &&
            ( !it.has_flag( flag_OVERSIZE ) || !it.has_flag( flag_OUTER ) ) && !it.has_flag( flag_SKINTIGHT ) &&
            !it.has_flag( flag_BELTED ) && !it.has_flag( flag_PERSONAL ) && !it.has_flag( flag_AURA ) &&
            !it.has_flag( flag_SEMITANGIBLE ) ) {
        // Checks to see if the player is wearing shoes
        return ret_val<bool>::make_failure( (
                                                is_player() ? _( "You're already wearing footwear!" )
                                                : string_format( _( "%s is already wearing footwear!" ), name ) ) );
    }

    if( it.covers( bodypart_id( "head" ) ) &&
            !it.has_flag( flag_HELMET_COMPAT ) && !it.has_flag( flag_SKINTIGHT ) &&
            !it.has_flag( flag_PERSONAL ) && !it.is_power_armor() &&
            !it.has_flag( flag_AURA ) && !it.has_flag( flag_SEMITANGIBLE ) && !it.has_flag( flag_OVERSIZE ) &&
            is_wearing_helmet() ) {
        return ret_val<bool>::make_failure( wearing_something_on( bodypart_id( "head" ) ),
                                            ( is_player() ? _( "You can't wear that with other headgear!" )
                                              : string_format( _( "%s can't wear that with other headgear!" ), name ) ) );
    }

    if( it.covers( bodypart_id( "head" ) ) && !it.has_flag( flag_SEMITANGIBLE ) &&
            ( it.has_flag( flag_SKINTIGHT ) || it.has_flag( flag_HELMET_COMPAT ) ) &&
            ( head_cloth_encumbrance() + it.get_encumber( *this, bodypart_id( "head" ) ) > 40 ) ) {
        return ret_val<bool>::make_failure( ( is_player() ? _( "You can't wear that much on your head!" )
                                              : string_format( _( "%s can't wear that much on their head!" ), name ) ) );
    }

    return ret_val<bool>::make_success();
}

bool Character::wear_possessed(
    item& to_wear, bool interactive, std::optional<location_vector<item>::iterator> position )
{
    if( is_worn( to_wear ) ) {
        if( interactive ) {
            add_msg_player_or_npc(
                m_info, _( "You are already wearing that." ),
                _( "<npcname> is already wearing that." ) );
        }
        return false;
    }
    if( to_wear.is_null() ) {
        if( interactive ) {
            add_msg_player_or_npc(
                m_info, _( "You don't have that item." ), _( "<npcname> doesn't have that item." ) );
        }
        return false;
    }

    bool was_weapon;
    detached_ptr<item> det;
    if( &to_wear == &primary_weapon() ) {
        det = remove_primary_weapon();
        was_weapon = true;
    } else {
        det = inv.remove_item( &to_wear );
        inv.restack( *this->as_player() );
        was_weapon = false;
    }

    auto result = wear_item( std::move( det ), interactive, std::move( position ) );
    if( result ) {
        if( was_weapon ) {
            set_primary_weapon( std::move( result ) );
        } else {
            inv.add_item( std::move( result ), true );
        }
        return false;
    }

    return true;
}

ret_val<bool> Character::can_takeoff( const item& it, bool dropping ) const
{
    auto iter = std::ranges::find_if( worn, [&it]( item * wit ) { return &it == wit; } );

    if( iter == worn.end() ) {
        return ret_val<bool>::make_failure(
                   !is_npc() ? _( "You are not wearing that item." )
                   : _( "<npcname> is not wearing that item." ) );
    }

    if( dropping && !get_dependent_worn_items( it ).empty() ) {
        return ret_val<bool>::make_failure(
                   !is_npc()
                   ? _( "You can't take off power armor while wearing other power armor "
                        "components." )
                   : _( "<npcname> can't take off power armor while wearing other power armor "
                        "components." ) );
    }
    if( it.has_flag( flag_NO_TAKEOFF ) ) {
        return ret_val<bool>::make_failure(
                   !is_npc() ? _( "You can't take that item off." )
                   : _( "<npcname> can't take that item off." ) );
    }
    return ret_val<bool>::make_success();
}

bool Character::takeoff( item& it, std::vector<detached_ptr<item>> *res )
{
    const auto ret = can_takeoff( it, res == nullptr );
    if( !ret.success() ) {
        add_msg( m_info, "%s", ret.c_str() );
        return false;
    }

    // Lua iwearable can_takeoff callback
    if( const auto * iwear_cb = it.type->iwearable_callbacks ) {
        if( !iwear_cb->call_can_takeoff( *this, it ) ) { return false; }
    }

    auto iter = std::ranges::find_if( worn, [&it]( item * wit ) { return &it == wit; } );

    if( res == nullptr ) {
        if( volume_carried() + it.volume() > volume_capacity_reduced_by( it.get_storage() ) ) {
            if( is_npc()
                || query_yn( _( "No room in inventory for your %s.  Drop it?" ),
                             colorize( it.tname(), it.color_in_inventory() ) ) ) {
                drop( it, bub_pos() );
                return true; // the drop activity ends up taking off the item anyway so shouldn't
                // try to do it again here
            } else {
                return false;
            }
        }
        ( *iter )->on_takeoff( *this );
        detached_ptr<item> det;
        worn.erase( iter, &det );
        inv.add_item( std::move( det ), true );
    } else {
        ( *iter )->on_takeoff( *this );
        detached_ptr<item> det;
        worn.erase( iter, &det );
        res->push_back( std::move( det ) );
    }

    add_msg_player_or_npc(
        _( "You take off your %s." ), _( "<npcname> takes off their %s." ), it.tname() );

    // TODO: Make this variable
    mod_moves( -250 );

    recalc_sight_limits();
    reset_encumbrance();

    // removing a splint from a broken limb can change perceived pain without directly modifying
    // pain update morale just in case
    morale->on_stat_change( "perceived_pain", get_perceived_pain() );

    return true;
}

ret_val<bool> Character::can_wield( const item &it ) const
{
    if( it.made_of( LIQUID ) ) {
    return ret_val<bool>::make_failure( _( "Can't wield spilt liquids." ) );
    }

    if( get_working_arm_count() <= 0 ) {
    return ret_val<bool>::make_failure(
               _( "You need at least one arm to even consider wielding something." ) );
    }

    if( is_armed() && primary_weapon().has_flag( flag_NO_UNWIELD ) ) {
    return ret_val<bool>::make_failure( _( "The %s is preventing you from wielding the %s." ),
                                        character_funcs::fmt_wielded_weapon( *this ), it.tname() );
    }

    monster *mount = mounted_creature.get();
    if( it.is_two_handed( *this ) && ( !has_two_arms() || worn_with_flag( flag_RESTRICT_HANDS ) ) &&
            !( is_mounted() && mount->has_flag( MF_RIDEABLE_MECH ) &&
               mount->type->mech_weapon && it.typeId() == mount->type->mech_weapon ) ) {
        if( worn_with_flag( flag_RESTRICT_HANDS ) ) {
            return ret_val<bool>::make_failure(
                       _( "Something you are wearing hinders the use of both hands." ) );
        } else if( it.has_flag( flag_ALWAYS_TWOHAND ) ) {
            return ret_val<bool>::make_failure( _( "The %s can't be wielded with only one arm." ),
                                                it.tname() );
        } else {
            return ret_val<bool>::
                   make_failure( _( "You are too weak to wield %s with only one arm." ), it.tname() );
        }
    }

    return ret_val<bool>::make_success();
}

ret_val<bool> Character::can_unwield( const item &it ) const
{
    if( it.has_flag( flag_NO_UNWIELD ) ) {
    return ret_val<bool>::make_failure( _( "You cannot unwield your %s." ), it.tname() );
    }

    return ret_val<bool>::make_success();
}

bool Character::unwield()
{
    if( primary_weapon().is_null() ) { return true; }

    if( !can_unwield( primary_weapon() ).success() ) { return false; }

    // Lua iwieldable can_unwield callback
    if( const auto * iwield_cb = primary_weapon().type->iwieldable_callbacks ) {
        if( !iwield_cb->call_can_unwield( *this, primary_weapon() ) ) { return false; }
    }

    primary_weapon().on_unwield( *this );

    const std::string query = string_format( _( "Stop wielding %s?" ), primary_weapon().tname() );

    if( !dispose_item( primary_weapon(), query ) ) { return false; }

    inv.unsort();

    return true;
}

ret_val<bool> Character::can_swap( const item &it ) const
{
    if( it.has_flag( flag_POWERARMOR_MOD ) ) {
    int max_layer = 2;
    std::vector< std::pair< bodypart_str_id, int > > mod_parts;
    const auto &all_bps = get_all_body_parts();
        for( const bodypart_id &bp : all_bps ) {
            if( it.get_covered_body_parts().test( bp.id() ) && bp->part_side != side::BOTH ) {
                mod_parts.emplace_back( bp, 0 );
            }
        }
        for( auto& elem : worn ) {
            for( std::pair<bodypart_str_id, int> &mod_part : mod_parts ) {
                bodypart_str_id bpid = mod_part.first;
                if( elem->get_covered_body_parts().test( bpid->opposite_part )
                    && elem->has_flag( flag_POWERARMOR_MOD ) ) {
                    mod_part.second++;
                }
            }
        }
        for( std::pair<bodypart_str_id, int> &mod_part : mod_parts ) {
            if( mod_part.second >= max_layer ) {
                return ret_val<bool>::make_failure( _( "There is no space on the opposite side!" ) );
            }
        }
    }

    return ret_val<bool>::make_success();
}

// pretty much the same as inventory::remove_randomly_by_volume but I didn't see a point in
// adding it to the inventory class when it's only called here in Character::drop_invalid_inventory
std::vector<detached_ptr<item>> remove_randomly_by_weight( location_inventory &,
        const units::mass & );
std::vector<detached_ptr<item>> remove_randomly_by_weight( location_inventory &inv,
        const units::mass &weight )
{
    std::vector<item *> contents;
    std::vector<detached_ptr<item>> result;

    inv.dump( contents );

    // shuffle the vector
    std::shuffle( contents.begin(), contents.end(), rng_get_engine() );

    // iterate through until we have dropped enough items
    auto dropped_weight = 0_gram;
    for( auto& e : contents ) {
        if( dropped_weight >= weight ) { break; }
        dropped_weight += e->weight();
        result.push_back( e->detach() );
    }

    return result;
}

void Character::drop_invalid_inventory()
{
    bool dropped_liquid = false;

    const auto p = bub_pos();

    inv.remove_items_with( [&dropped_liquid, &p]( detached_ptr<item>&& it ) {
        if( it->made_of( LIQUID ) ) {
            dropped_liquid = true;
            get_map().add_item_or_charges( p, std::move( it ) );
        }
        return VisitResponse::SKIP;
    } );

    if( dropped_liquid ) {
        add_msg_if_player( m_bad, _( "Liquid from your inventory has leaked onto the ground." ) );
    }

    if( volume_carried() > volume_capacity() ) {
        auto items_to_drop = inv.remove_randomly_by_volume( volume_carried() - volume_capacity() );
        put_into_vehicle_or_drop( *this, item_drop_reason::tumbling, items_to_drop );
    }
    if( !is_npc() ) { return; }
    // Also drop excess weight IF an NPC
    auto wt_carried = weight_carried();
    auto wt_capacity = weight_capacity();
    if( wt_carried > wt_capacity ) {
        auto items_to_drop = remove_randomly_by_weight( inv, wt_carried - wt_capacity );
        put_into_vehicle_or_drop( *this, item_drop_reason::too_heavy, items_to_drop );
    }
}

bool Character::has_artifact_with( const art_effect_passive effect ) const
{
for( const item *weapon : wielded_items() ) {
    if( weapon->has_effect_when_wielded( effect ) ) {
            return true;
        }
    }
for( auto &i : worn ) {
    if( i->has_effect_when_worn( effect ) ) {
            return true;
        }
    }
    return has_item_with( [effect]( const item & it ) { return it.has_effect_when_carried( effect ); } );
}

bool Character::is_wielding( const item& target ) const { return &primary_weapon() == &target; }

bool Character::is_wearing( const item &itm ) const
{
for( auto &i : worn ) {
    if( i == &itm ) {
            return true;
        }
    }
    return false;
}

bool Character::is_wearing( const itype_id &it ) const
{
for( auto &i : worn ) {
    if( i->typeId() == it ) {
            return true;
        }
    }
    return false;
}

bool Character::is_wearing_on_bp( const itype_id &it, const bodypart_id &bp ) const
{
for( auto &i : worn ) {
    if( i->typeId() == it && i->covers( bp ) ) {
            return true;
        }
    }
    return false;
}

bool Character::worn_with_flag( const flag_id& flag, const bodypart_id& bp ) const
{
    return std::ranges::any_of( worn, [&flag, bp]( const item * const & it ) {
        return it->has_flag( flag ) && ( bp == bodypart_str_id::NULL_ID() || it->covers( bp ) );
    } );
}

const item *Character::item_worn_with_flag( const flag_id &flag, const bodypart_id &bp ) const
{
for( const item * const &it : worn ) {
    if( it->has_flag( flag ) && ( bp == bodypart_str_id::NULL_ID() ||
                                      it->covers( bp ) ) ) {
            return it;
        }
    }
    return nullptr;
}

bool Character::worn_with_id( const itype_id& item_id, const bodypart_id& bp ) const
{
    return std::ranges::any_of( worn, [&item_id, bp]( const item * const & it ) {
        return it->typeId() == item_id && ( bp == bodypart_str_id::NULL_ID() || it->covers( bp ) );
    } );
}

const item *Character::item_worn_with_id( const itype_id &item_id, const bodypart_id &bp ) const
{
for( const item * const &it : worn ) {
    if( it->typeId() == item_id && ( bp == bodypart_str_id::NULL_ID() ||
                                         it->covers( bp ) ) ) {
            return it;
        }
    }
    return nullptr;
}

bool Character::worn_with_quality( const quality_id& qual, const bodypart_id& bp ) const
{
    return std::ranges::any_of( worn, [&qual, bp]( const item * const & it ) {
        return it->get_quality( qual ) > 0 && ( bp == bodypart_str_id::NULL_ID() || it->covers( bp ) );
    } );
}

const item *Character::item_worn_with_quality( const quality_id &qual, const bodypart_id &bp ) const
{
for( const item * const &it : worn ) {
    if( it->get_quality( qual ) > 0 &&
            ( bp == bodypart_str_id::NULL_ID() || it->covers( bp ) ) ) {
            return it;
        }
    }
    return nullptr;
}

