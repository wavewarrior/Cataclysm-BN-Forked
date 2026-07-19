#include "activity_actor_definitions.h"
#include "coop_client.h"
#include "activity_handlers.h" // IWYU pragma: associated
#include "avatar.h"
#include "avatar_action.h"
#include "calendar.h"
#include "cata_algo.h"
#include "character.h"
#include "character_functions.h"
#include "clzones.h"
#include "construction.h"
#include "construction_group.h"
#include "construction_partial.h"
#include "coordinates.h"
#include "creature.h"
#include "debug.h"
#include "drop_token.h"
#include "enums.h"
#include "field.h"
#include "field_type.h"
#include "fire.h"
#include "flag.h"
#include "flat_set.h"
#include "game.h"
#include "game_constants.h"
#include "iexamine.h"
#include "int_id.h"
#include "inventory.h"
#include "item.h"
#include "itype.h"
#include "iuse.h"
#include "line.h"
#include "map.h"
#include "map_iterator.h"
#include "map_selector.h"
#include "map_utils.h"
#include "mapdata.h"
#include "messages.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "options.h"
#include "output.h"
#include "overmapbuffer.h"
#include "pickup.h"
#include "pickup_token.h"
#include "player.h"
#include "player_activity.h"
#include "point.h"
#include "requirements.h"
#include "ret_val.h"
#include "rng.h"
#include "skill.h"
#include "stomach.h"
#include "string_formatter.h"
#include "string_id.h"
#include "string_utils.h"
#include "translations.h"
#include "trap.h"
#include "units.h"
#include "value_ptr.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vehicle_selector.h"
#include "vpart_position.h"
#include "weather.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <list>
#include <memory>
#include <numeric>
#include <optional>
#include <queue>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <utility>
#include <vector>

namespace views = std::views;

static const activity_id ACT_FETCH_REQUIRED( "ACT_FETCH_REQUIRED" );
static const activity_id ACT_FISH( "ACT_FISH" );
static const activity_id ACT_MOVE_LOOT( "ACT_MOVE_LOOT" );
static const activity_id ACT_MULTIPLE_BUTCHER( "ACT_MULTIPLE_BUTCHER" );
static const activity_id ACT_MULTIPLE_CHOP_PLANKS( "ACT_MULTIPLE_CHOP_PLANKS" );
static const activity_id ACT_MULTIPLE_CHOP_TREES( "ACT_MULTIPLE_CHOP_TREES" );
static const activity_id ACT_MULTIPLE_CONSTRUCTION( "ACT_MULTIPLE_CONSTRUCTION" );
static const activity_id ACT_MULTIPLE_FARM( "ACT_MULTIPLE_FARM" );
static const activity_id ACT_MULTIPLE_FISH( "ACT_MULTIPLE_FISH" );
static const activity_id ACT_MULTIPLE_MINE( "ACT_MULTIPLE_MINE" );
static const activity_id ACT_TIDY_UP( "ACT_TIDY_UP" );
static const activity_id ACT_VEHICLE( "ACT_VEHICLE" );
static const activity_id ACT_VEHICLE_DECONSTRUCTION( "ACT_VEHICLE_DECONSTRUCTION" );
static const activity_id ACT_VEHICLE_REPAIR( "ACT_VEHICLE_REPAIR" );

static const efftype_id effect_ai_waiting( "ai_waiting" );
static const efftype_id effect_pet( "pet" );
static const efftype_id effect_nausea( "nausea" );

static const itype_id itype_battery( "battery" );
static const itype_id itype_log( "log" );
static const itype_id itype_soldering_iron( "soldering_iron" );
static const itype_id itype_welder( "welder" );

static const trap_str_id tr_firewood_source( "tr_firewood_source" );
static const trap_str_id tr_unfinished_construction( "tr_unfinished_construction" );

static const zone_type_id zone_type_source_firewood( "SOURCE_FIREWOOD" );

static const zone_type_id zone_type_CHOP_TREES( "CHOP_TREES" );
static const zone_type_id zone_type_CONSTRUCTION_BLUEPRINT( "CONSTRUCTION_BLUEPRINT" );
static const zone_type_id zone_type_FARM_PLOT( "FARM_PLOT" );
static const zone_type_id zone_type_FISHING_SPOT( "FISHING_SPOT" );
static const zone_type_id zone_type_LOOT_CORPSE( "LOOT_CORPSE" );
static const zone_type_id zone_type_LOOT_IGNORE( "LOOT_IGNORE" );
static const zone_type_id zone_type_LOOT_IGNORE_FAVORITES( "LOOT_IGNORE_FAVORITES" );
static const zone_type_id zone_type_MINING( "MINING" );
static const zone_type_id zone_type_LOOT_UNSORTED( "LOOT_UNSORTED" );
static const zone_type_id zone_type_LOOT_WOOD( "LOOT_WOOD" );
static const zone_type_id zone_type_VEHICLE_DECONSTRUCT( "VEHICLE_DECONSTRUCT" );
static const zone_type_id zone_type_VEHICLE_REPAIR( "VEHICLE_REPAIR" );

static const quality_id qual_AXE( "AXE" );
static const quality_id qual_BUTCHER( "BUTCHER" );
static const quality_id qual_DIG( "DIG" );
static const quality_id qual_FISHING( "FISHING" );
static const quality_id qual_SAW_M( "SAW_M" );
static const quality_id qual_SAW_W( "SAW_W" );
static const quality_id qual_WELD( "WELD" );

static const std::string flag_BUTCHER_EQ( "BUTCHER_EQ" );
static const std::string flag_FISHABLE( "FISHABLE" );
static const std::string flag_GROWTH_HARVEST( "GROWTH_HARVEST" );
static const std::string flag_PLANT( "PLANT" );
static const std::string flag_PLOWABLE( "PLOWABLE" );
static const std::string flag_TREE( "TREE" );

void cancel_aim_processing();
// Generic activity: maximum search distance for zones, constructions, etc.
const int ACTIVITY_SEARCH_DISTANCE = 60;

static bool same_type( const std::vector<item*> &items )
{
    return std::ranges::all_of( items, [&items]( const item * const & it ) {
        return it->type == ( *items.begin() )->type;
    } );
}

static bool same_type( const std::vector<detached_ptr<item>> &items )
{
    return std::ranges::all_of( items, [&items]( const detached_ptr<item> &it ) {
        return it->type == ( *items.begin() )->type;
    } );
}

static void put_into_vehicle(
    Character& c, item_drop_reason reason, std::vector<detached_ptr<item>> &items, vehicle& veh,
    int part )
{
    if( items.empty() ) { return; }

    const tripoint_bub_ms where = veh.bub_part_location( part );
    map& here = get_map();
    const std::string ter_name = here.name( where );
    int fallen_count = 0;
    bool into_vehicle = false;

    std::vector<item *> items_copy;
    items_copy.reserve( items.size() );
    for( detached_ptr<item> &it : items ) { items_copy.push_back( &*it ); }

    // can't use constant reference here because of the spill_contents()
    for( detached_ptr<item> &it : items ) {
        item& obj = *it;
        it = pickup::handle_spillable_contents( c, std::move( it ), here );
        if( !it ) { continue; }
        it = veh.add_item( part, std::move( it ) );
        if( !it ) {
            into_vehicle = true;
        } else {
            if( obj.count_by_charges() ) {
                // Maybe we can add a few charges in the trunk and the rest on the ground.
                it = veh.add_charges( part, std::move( it ) );
                into_vehicle = true;
            }
            if( it ) {
                fallen_count += it->count();
                here.add_item_or_charges( where, std::move( it ) );
            }
        }
        obj.handle_pickup_ownership( c );
    }

    const std::string part_name = veh.part_info( part ).name();

    if( same_type( items_copy ) ) {
        const item* it = items_copy.front();
        const int dropcount = items.size() * it->count();
        const std::string it_name = it->tname( dropcount );

        switch( reason ) {
            case item_drop_reason::deliberate:
                c.add_msg_player_or_npc(
                    vgettext( "You put your %1$s in the %2$s's %3$s.",
                              "You put your %1$s in the %2$s's %3$s.", dropcount ),
                    vgettext( "<npcname> puts their %1$s in the %2$s's %3$s.",
                              "<npcname> puts their %1$s in the %2$s's %3$s.", dropcount ),
                    it_name, veh.name, part_name );
                break;
            case item_drop_reason::too_large:
                c.add_msg_if_player(
                    vgettext( "There's no room in your inventory for the %s, so you drop it into "
                              "the %s's %s.",
                              "There's no room in your inventory for the %s, so you drop them into "
                              "the %s's %s.",
                              dropcount ),
                    it_name, veh.name, part_name );
                break;
            case item_drop_reason::too_heavy:
                c.add_msg_if_player(
                    vgettext( "The %s is too heavy to carry, so you drop it into the %s's %s.",
                              "The %s are too heavy to carry, so you drop them into the %s's %s.",
                              dropcount ),
                    it_name, veh.name, part_name );
                break;
            case item_drop_reason::tumbling:
                c.add_msg_if_player(
                    m_bad,
                    vgettext( "Your %s tumbles into the %s's %s.",
                              "Your %s tumble into the %s's %s.", dropcount ),
                    it_name, veh.name, part_name );
                break;
        }
    } else {
        switch( reason ) {
            case item_drop_reason::deliberate:
                c.add_msg_player_or_npc(
                    _( "You put several items in the %1$s's %2$s." ),
                    _( "<npcname> puts several items in the %1$s's %2$s." ), veh.name, part_name );
                break;
            case item_drop_reason::too_large:
            case item_drop_reason::too_heavy:
            case item_drop_reason::tumbling:
                c.add_msg_if_player(
                    m_bad, _( "Some items tumble into the %1$s's %2$s." ), veh.name, part_name );
                break;
        }
    }

    if( fallen_count > 0 ) {
        if( into_vehicle ) {
            c.add_msg_if_player(
                m_warning,
                vgettext( "The %s is full, so something fell to the %s.",
                          "The %s is full, so some items fell to the %s.", fallen_count ),
                part_name, ter_name );
        } else {
            c.add_msg_if_player(
                m_warning,
                vgettext( "The %s is full, so it fell to the %s.",
                          "The %s is full, so they fell to the %s.", fallen_count ),
                part_name, ter_name );
        }
    }
}

static void pass_to_ownership_handling( item& obj, Character& c ) { obj.handle_pickup_ownership( c ); }

static void stash_on_pet( std::vector<detached_ptr<item>> &items, monster& pet, Character& who )
{
    if( !pet.get_storage_item() ) {
        debugmsg( "Tried to stash items on a pet without a storage item" );
        return;
    }
    units::volume remaining_volume =
        pet.get_storage_item()->get_storage() - pet.get_carried_volume();
    units::mass remaining_weight = pet.weight_capacity() - pet.get_carried_weight();
    map& here = get_map();

    for( detached_ptr<item> &it : items ) {
        item& obj = *it;
        if( it->volume() > remaining_volume ) {
            add_msg( m_bad, _( "%1$s did not fit and fell to the %2$s." ), it->display_name(),
                     here.name( pet.bub_pos() ) );
            here.add_item_or_charges( pet.bub_pos(), std::move( it ) );
        } else if( it->weight() > remaining_weight ) {
            add_msg( m_bad, _( "%1$s is too heavy and fell to the %2$s." ), it->display_name(),
                     here.name( pet.bub_pos() ) );
            here.add_item_or_charges( pet.bub_pos(), std::move( it ) );
        } else {
            pet.add_item( std::move( it ) );
            remaining_volume -= obj.volume();
            remaining_weight -= obj.weight();
        }
        // TODO: if NPCs can have pets or move items onto pets
        pass_to_ownership_handling( obj, who );
    }
}

// Helper function to collect names of all favorited items (including contents)
static auto collect_favorited_item_names( const item& it ) -> std::vector<std::string>
{
    std::vector<std::string> favorited_names;
    if( it.is_favorite ) { favorited_names.push_back( it.display_name() ); }

    auto favorited_contents =
        it.contents.all_items_ptr()
    | std::views::filter( []( const item * i ) { return i->is_favorite; } )
    | std::views::transform( []( const item * i ) { return i->display_name(); } );

    std::ranges::copy( favorited_contents, std::back_inserter( favorited_names ) );
    return favorited_names;
}

void drop_on_map(
    Character& c, item_drop_reason reason, detached_ptr<item>&& it, const tripoint_bub_ms& where )
{
    std::vector<detached_ptr<item>> vec;
    vec.push_back( std::move( it ) );
    drop_on_map( c, reason, vec, where );
}

void drop_on_map(
    Character& c, item_drop_reason reason, std::vector<detached_ptr<item>> &items,
    const tripoint_bub_ms& where )
{
    if( items.empty() ) { return; }
    map& here = get_map();
    const std::string ter_name = here.name( where );
    const bool can_move_there = here.passable( where );

    if( same_type( items ) ) {
        detached_ptr<item> &it = items.front();
        const int dropcount = items.size() * it->count();
        const std::string it_name = it->tname( dropcount );

        switch( reason ) {
            case item_drop_reason::deliberate:
                if( can_move_there ) {
                    c.add_msg_player_or_npc(
                        vgettext( "You drop your %1$s on the %2$s.",
                                  "You drop your %1$s on the %2$s.", dropcount ),
                        vgettext( "<npcname> drops their %1$s on the %2$s.",
                                  "<npcname> drops their %1$s on the %2$s.", dropcount ),
                        it_name, ter_name );
                } else {
                    c.add_msg_player_or_npc(
                        vgettext( "You put your %1$s in the %2$s.", "You put your %1$s in the %2$s.",
                                  dropcount ),
                        vgettext( "<npcname> puts their %1$s in the %2$s.",
                                  "<npcname> puts their %1$s in the %2$s.", dropcount ),
                        it_name, ter_name );
                }
                break;
            case item_drop_reason::too_large:
                c.add_msg_if_player(
                    vgettext( "There's no room in your inventory for the %s, so you drop it.",
                              "There's no room in your inventory for the %s, so you drop them.",
                              dropcount ),
                    it_name );
                break;
            case item_drop_reason::too_heavy:
                c.add_msg_if_player(
                    vgettext( "The %s is too heavy to carry, so you drop it.",
                              "The %s is too heavy to carry, so you drop them.", dropcount ),
                    it_name );
                break;
            case item_drop_reason::tumbling:
                c.add_msg_if_player(
                    m_bad,
                    vgettext( "Your %1$s tumbles to the %2$s.", "Your %1$s tumble to the %2$s.",
                              dropcount ),
                    it_name, ter_name );
                break;
        }


        if( get_option<bool>( "AUTO_NOTES_DROPPED_FAVORITES" ) && it->is_favorite ) {
            const tripoint_abs_omt your_pos = c.abs_omt_pos();
            const std::string sprite_prefix = "SPRITE:" + it->typeId().str() + ";";
            if( !get_overmapbuffer( c.get_dimension() ).has_note( your_pos ) ) {
                get_overmapbuffer( c.get_dimension() )
                .add_note( your_pos, sprite_prefix + it->display_name() );
            } else {
                get_overmapbuffer( c.get_dimension() )
                .add_note( your_pos, get_overmapbuffer( c.get_dimension() ).note( your_pos ) + "; "
                           + it->display_name() );
            }
        }

    } else {
        switch( reason ) {
            case item_drop_reason::deliberate:
                if( can_move_there ) {
                    c.add_msg_player_or_npc(
                        _( "You drop several items on the %s." ),
                        _( "<npcname> drops several items on the %s." ), ter_name );
                } else {
                    c.add_msg_player_or_npc(
                        _( "You put several items in the %s." ),
                        _( "<npcname> puts several items in the %s." ), ter_name );
                }
                break;
            case item_drop_reason::too_large:
            case item_drop_reason::too_heavy:
            case item_drop_reason::tumbling:
                c.add_msg_if_player( m_bad, _( "Some items tumble to the %s." ), ter_name );
                break;
        }
    }
    if( get_option<bool>( "AUTO_NOTES_DROPPED_FAVORITES" ) ) {
        const tripoint_abs_omt your_pos = c.abs_omt_pos();

        auto all_favorited_names =
        items | cata::ranges::flat_map( []( const auto & it ) {
            return collect_favorited_item_names( *it );
        } )
        | std::ranges::to<std::vector>();

        if( !all_favorited_names.empty() ) {
            const auto note_text = join( all_favorited_names, "; " );

            if( !get_overmapbuffer( c.get_dimension() ).has_note( your_pos ) ) {
                get_overmapbuffer( c.get_dimension() ).add_note( your_pos, note_text );
            } else {
                get_overmapbuffer( c.get_dimension() )
                .add_note(
                    your_pos,
                    get_overmapbuffer( c.get_dimension() ).note( your_pos ) + "; " + note_text );
            }
        }
    }

    for( auto& it : items ) {
        item& obj = *it;
        here.add_item_or_charges( where, std::move( it ) );
        pass_to_ownership_handling( obj, c );
    }
}

void put_into_vehicle_or_drop( Character& c, item_drop_reason reason, detached_ptr<item>&& it )
{
    std::vector<detached_ptr<item>> vec;
    vec.push_back( std::move( it ) );
    return put_into_vehicle_or_drop( c, reason, vec, c.bub_pos() );
}

void put_into_vehicle_or_drop(
    Character& c, item_drop_reason reason, std::vector<detached_ptr<item>> &items )
{
    return put_into_vehicle_or_drop( c, reason, items, c.bub_pos() );
}

void put_into_vehicle_or_drop(
    Character& c, item_drop_reason reason, detached_ptr<item>&& it, const tripoint_bub_ms& where,
    bool force_ground )
{

    std::vector<detached_ptr<item>> vec;
    vec.push_back( std::move( it ) );
    put_into_vehicle_or_drop( c, reason, vec, where, force_ground );
}

void put_into_vehicle_or_drop(
    Character& c, item_drop_reason reason, std::vector<detached_ptr<item>> &items,
    const tripoint_bub_ms& where, bool force_ground )
{
    map& here = get_map();
    const std::optional<vpart_reference> vp = here.veh_at( where ).part_with_feature( "CARGO", false );
    if( vp && !force_ground ) {
        put_into_vehicle( c, reason, items, vp->vehicle(), vp->part_index() );
        return;
    }
    drop_on_map( c, reason, items, where );
}

static std::list<pickup::act_item> convert_to_items(
    Character& p, const drop_locations& drop, const std::function<bool( item& loc )> &filter )
{
    std::list<pickup::act_item> res;

    for( const drop_location& rec : drop ) {
        item* loc = &*rec.loc;
        const int count = rec.count;

        if( !filter( *loc ) ) {
            continue;
        } else if( !p.is_worn( *loc ) && !p.is_wielding( *loc ) ) {
            // Special case. After dropping the first few items, the remaining items are already
            // separated. That means: `drop` already contains references to each of the items in
            // `p.inv.const_stack`, and `count` will be 1 for each of them.
            // If we continued without this check, we iterate over `p.inv.const_stack` multiple
            // times, but each time stopping after visiting the first item. In the end, we would add
            // references to the same item (the first one in the stack) multiple times.
            if( count == 1 ) {
                res.emplace_back( *loc, 1, loc->obtain_cost( p, 1 ) );
                continue;
            }
            int obtained = 0;
            for( item * const& it : p.inv_const_stack( p.get_item_position( &*loc ) ) ) {
                if( obtained >= count ) { break; }
                const int qty =
                    it->count_by_charges() ? std::min<int>( it->charges, count - obtained ) : 1;
                obtained += qty;
                res.emplace_back( *it, qty, it->obtain_cost( p, qty ) );
            }
        } else {
            res.emplace_back( *loc, count, p.is_wielding( *loc ) ? 0 : loc->obtain_cost( p ) );
        }
    }

    return res;
}

namespace pickup
{

// Prepares items for dropping by reordering them so that the drop
// cost is minimal and "dependent" items get taken off first.
// Implements the "backpack" logic.
std::list<act_item> reorder_for_dropping( Character& p, const drop_locations& drop )
{
    std::list<act_item> res = convert_to_items( p, drop, [&p]( item & loc ) {
        return p.is_wielding( loc );
    } );
    std::list<act_item> inv = convert_to_items( p, drop, [&p]( item & loc ) {
        return !p.is_wielding( loc ) && !p.is_worn( loc );
    } );
    std::list<act_item> worn = convert_to_items( p, drop, [&p]( item & loc ) {
        return p.is_worn( loc );
    } );

    // Sort inventory items by volume in ascending order
    inv.sort( []( const act_item & first, const act_item & second ) {
        return first.loc->volume() < second.loc->volume();
    } );
    // Add missing dependent worn items (if any).
    for( const auto& wait : worn ) {
        for( item * dit : p.get_dependent_worn_items( *wait.loc ) ) {
            const auto iter = std::ranges::find_if( worn, [dit]( const act_item & ait ) {
                return &*ait.loc == dit;
            } );

            if( iter == worn.end() ) {
                // TODO: Use a calculated cost
                worn.emplace_front( *dit, dit->count(), dit->obtain_cost( p ) );
            }
        }
    }
    // Sort worn items by storage in descending order, but dependent items always go first.
    worn.sort( []( const act_item & first, const act_item & second ) {
        return first.loc->is_worn_only_with( *second.loc )
               || ( first.loc->get_storage() > second.loc->get_storage()
                    && !second.loc->is_worn_only_with( *first.loc ) );
    } );

    // Avoid tumbling to the ground. Unload cleanly.
    units::volume dropped_inv_contents =
    std::accumulate( inv.begin(), inv.end(), 0_ml, []( units::volume acc, const act_item & ait ) {
        return acc + ait.loc->volume();
    } );
    const units::volume dropped_worn_storage =
    std::accumulate( worn.begin(), worn.end(), 0_ml, []( units::volume acc, const act_item & ait ) {
        return acc + ait.loc->get_storage();
    } );
    std::set<int> inv_indices;
    std::ranges::
    transform( inv, std::inserter( inv_indices, inv_indices.begin() ), [&p]( const act_item & ait ) {
        return p.get_item_position( &*ait.loc );
    } );

    units::volume excessive_volume =
        p.volume_carried() - dropped_inv_contents
        - p.volume_capacity_reduced_by( dropped_worn_storage );
    if( excessive_volume > 0_ml ) {
        const_invslice old_inv = p.inv_const_slice();
        std::vector<item *> non_favorite_candidates;
        std::vector<item *> favorite_candidates;
        for( size_t i = 0; i < old_inv.size(); i++ ) {
            if( inv_indices.contains( i ) ) { continue; }
            const std::vector<item *> &inv_stack = *old_inv[i];
            std::ranges::copy_if(
                inv_stack, std::back_inserter( non_favorite_candidates ),
            []( const item * const it ) { return !it->is_favorite && it->volume() > 0_ml; } );
            std::ranges::copy_if(
                inv_stack, std::back_inserter( favorite_candidates ),
            []( const item * const it ) { return it->is_favorite && it->volume() > 0_ml; } );
        }

        auto drop_from_pool_until_fits = [&]( std::vector<item*> &pool ) {
            while( excessive_volume > 0_ml && !pool.empty() ) {
                const size_t chosen_index = rng( 0, pool.size() - 1 );
                item* const selected = pool[chosen_index];
                pool[chosen_index] = pool.back();
                pool.pop_back();

                // Note: zero cost, but won't be contained on drop.
                inv.emplace_back( *selected, selected->count(), 0 );
                excessive_volume -= selected->volume();
            }
        };

        drop_from_pool_until_fits( non_favorite_candidates );
        drop_from_pool_until_fits( favorite_candidates );

        // Need to re-sort
        inv.sort( []( const act_item & first, const act_item & second ) {
            return first.loc->volume() < second.loc->volume();
        } );
    }

    // Cumulatively decreases
    units::volume remaining_dropped_storage = dropped_worn_storage;

    while( !worn.empty() && !inv.empty() ) {
        units::volume front_storage = worn.front().loc->get_storage();
        // Does not fit
        // TODO: but maybe an item further down the line does
        if( remaining_dropped_storage < inv.front().loc->volume() ) { break; }

        res.push_back( worn.front() );
        worn.pop_front();
        remaining_dropped_storage -= front_storage;
        while( !inv.empty() ) {
            units::volume inventory_item_volume = inv.front().loc->volume();
            if( front_storage < inventory_item_volume ) { break; }
            front_storage -= inventory_item_volume;

            res.push_back( inv.front() );
            // Free of charge
            res.back().consumed_moves = 0;

            inv.pop_front();
        }
    }

    // Now insert everything that remains
    std::ranges::copy( inv, std::back_inserter( res ) );
    std::ranges::copy( worn, std::back_inserter( res ) );

    return res;
}

std::vector<detached_ptr<item>> obtain_and_tokenize_items( player& p, std::list<act_item> &items )
{
    std::vector<detached_ptr<item>> res;
    drop_token_provider& token_provider = drop_token::get_provider();
    item_drop_token last_token = token_provider.make_next( calendar::turn );
    if( items.empty() ) { return res; }
    units::volume last_storage_volume = items.front().loc->get_storage();
    while( !items.empty() && ( p.is_npc() || p.moves > 0 || items.front().consumed_moves == 0 ) ) {
        act_item& ait = items.front();

        p.mod_moves( -ait.consumed_moves );

        if( p.is_worn( *ait.loc ) ) {
            if( !p.takeoff( *ait.loc, &res ) ) {
                // Skip item if failed to take it off
                debugmsg( "Failed to obtain worn target item of ACT_DROP" );
                items.pop_front();
                continue;
            }
        } else if( ait.loc->count_by_charges() ) {
            res.push_back( p.reduce_charges( const_cast<item*>( &*ait.loc ), ait.count ) );
        } else {
            res.push_back( ait.loc->detach() );
        }

        // TODO: Get the item consistently instead of using back()
        item& current_drop = *res.back();

        // Hack: if it consumes zero moves, it must have been contained
        // TODO: Properly mark containment somehow
        *current_drop.drop_token = token_provider.make_next( calendar::turn );
        ;
        if( ait.consumed_moves == 0 && last_storage_volume >= current_drop.volume() ) {
            last_storage_volume -= current_drop.volume();
            current_drop.drop_token->parent_number = last_token.parent_number;
        } else {
            last_token = *current_drop.drop_token;
            last_storage_volume = current_drop.get_storage();
        }

        items.pop_front();
    }

    return res;
}

} // namespace pickup

// TODO: Display costs in the multidrop menu
static void debug_drop_list( const std::list<pickup::act_item> &list )
{
    if( !debug_mode ) { return; }

    std::string res( "Items ordered to drop:\n" );
    for( const auto& ait : list ) {
        res += string_format(
                   "Drop %d %s for %d moves\n", ait.count, ait.loc->display_name( ait.count ),
                   ait.consumed_moves );
    }
    popup( res, PF_GET_KEY );
}

static void debug_tokens( const std::vector<detached_ptr<item>> &items )
{
    if( !debug_mode ) { return; }

    std::stringstream ss;
    ss << "Item tokens:\n";
    for( const detached_ptr<item> &it : items ) {
        ss << it->display_name() << ": " << *it->drop_token << '\n';
    }
    popup( ss.str(), PF_GET_KEY );
}

static std::vector<detached_ptr<item>> obtain_activity_items(
    Character& who, std::list<pickup::act_item> &targets )
{
    debug_drop_list( targets );

    std::vector<detached_ptr<item>> res =
        pickup::obtain_and_tokenize_items( *who.as_player(), targets );

    debug_tokens( res );

    return res;
}

void drop_activity_actor::do_turn( player_activity &, Character& who )
{
    const auto pos = who.bub_pos() + relpos;

    std::vector<detached_ptr<item>> dropped = obtain_activity_items( who, items );

    // C2a (co-op client only) — serialize each item BEFORE put_into_vehicle_or_drop
    // moves ownership into the map.  Must happen before the move because:
    //   - detached_ptr ownership is consumed by put_into_vehicle_or_drop; no access after.
    //   - cbc stacks may merge into pre-existing pointers (no "new" pointer to diff against).
    //   - the most common drop target is empty ground (before-set diff yields nothing).
    // Vehicle drops are skipped: items in vehicle cargo are host-authoritative.
    // Spillover to adjacent tiles (rare) is a deferred edge case — target pos is used for all entries.
    if( g->coop_client_ && !dropped.empty() ) {
        map &here = get_map();
        const bool goes_to_vehicle =
            !force_ground && here.veh_at( pos ).part_with_feature( "CARGO", false ).has_value();
        if( !goes_to_vehicle ) {
            const tripoint_abs_ms abs = here.bub_to_abs( pos );
            std::ostringstream oss;
            JsonOut jout( oss );
            jout.start_object();
            jout.member( "items" );
            jout.start_array();
            for( const detached_ptr<item> &it : dropped ) {
                if( !it ) { continue; }
                jout.start_object();
                jout.member( "tx", abs.x() );
                jout.member( "ty", abs.y() );
                jout.member( "tz", abs.z() );
                std::ostringstream item_oss;
                JsonOut jitem( item_oss );
                it->serialize( jitem );
                jout.member( "data", item_oss.str() );
                jout.end_object();
            }
            jout.end_array();
            jout.end_object();
            g->coop_client_->queue_action( "DROP", oss.str() );
        }
    }
    put_into_vehicle_or_drop( who, item_drop_reason::deliberate, dropped, pos, force_ground );

    get_map().process_falling();

    if( items.empty() ) { who.cancel_activity(); }
}

void activity_on_turn_wear( player_activity& act, player& p )
{
    // ACT_WEAR has item_location targets, and int quantities
    while( p.moves > 0 && !act.targets.empty() && !act.values.empty() ) {
        safe_reference<item> target = std::move( act.targets.back() );
        int quantity = act.values.back();
        act.targets.pop_back();
        act.values.pop_back();

        if( !target ) {
            debugmsg( "Lost target item of ACT_WEAR" );
            continue;
        }
        ret_val<bool> ret = p.can_wear( *target );
        if( ret.success() && ret.value() ) {
            detached_ptr<item> newit = target->split( quantity );
            p.wear_item( std::move( newit ) );
        }
    }

    // If there are no items left we are done
    if( act.targets.empty() ) { p.cancel_activity(); }
}


void stash_activity_actor::do_turn( player_activity &, Character& who )
{
    const auto pos = who.bub_pos() + relpos;

    monster* pet = g->critter_at<monster>( pos );
    if( pet != nullptr && pet->has_effect( effect_pet ) ) {
        pet->add_effect( effect_ai_waiting, 2_turns );
        std::vector<detached_ptr<item>> stashed = obtain_activity_items( who, items );
        stash_on_pet( stashed, *pet, who );
        if( items.empty() ) {
            who.cancel_activity();
            pet->remove_effect( effect_ai_waiting );
        }
    } else {
        who.add_msg_if_player( _( "The pet has moved somewhere else." ) );
        who.cancel_activity();
    }
}

