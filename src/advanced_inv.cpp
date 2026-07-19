
#include "advanced_inv.h"

#include "activity_actor.h"
#include "activity_actor_definitions.h"
#include "auto_pickup.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "character.h"
#include "color.h"
#include "debug.h"
#include "enums.h"
#include "examine_item_menu.h"
#include "game.h"
#include "game_constants.h"
#include "ime.h"
#include "input.h"
#include "inventory.h"
#include "item.h"
#include "item_category.h"
#include "item_contents.h"
#include "item_stack.h"
#include "map.h"
#include "map_selector.h"
#include "messages.h"
#include "options.h"
#include "output.h"
#include "panels.h"
#include "player.h"
#include "player_activity.h"
#include "point.h"
#include "ret_val.h"
#include "rml_screen.h"
#include "rml_util.h"
#include "string_formatter.h"
#include "string_input_popup.h"
#include "translations.h"
#include "type_id.h"
#include "ui.h"
#include "ui_manager.h"
#include "uistate.h"
#include "units.h"
#include "units_utility.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vehicle_selector.h"

#include <RmlUi/Core.h>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <list>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

static const activity_id ACT_ADV_INVENTORY( "ACT_ADV_INVENTORY" );
static const activity_id ACT_WEAR( "ACT_WEAR" );

static const trait_id trait_DEBUG_STORAGE( "DEBUG_STORAGE" );

void create_advanced_inv()
{
    advanced_inventory advinv;
    advinv.display();
}

enum aim_exit { exit_none = 0, exit_okay, exit_re_entry };

// *INDENT-OFF*
advanced_inventory::advanced_inventory()
    : inCategoryMode(false),
      recalc(true),
      src(left),
      dest(right),
      filter_edit(false)
      // panes don't need initialization, they are recalculated immediately
      ,
      squares(
          {{//               pos in window
            {AIM_INVENTORY, point(22, 2), tripoint_rel_ms::zero(), _("Inventory"), _("IN"), "I",
             "ITEMS_INVENTORY", AIM_INVENTORY},
            {AIM_SOUTHWEST, point(27, 3), tripoint_rel_ms::south_west(), _("South West"), _("SW"),
             "1", "ITEMS_SW", AIM_WEST},
            {AIM_SOUTH, point(30, 3), tripoint_rel_ms::south(), _("South"), _("S"), "2", "ITEMS_S",
             AIM_SOUTHWEST},
            {AIM_SOUTHEAST, point(33, 3), tripoint_rel_ms::south_east(), _("South East"), _("SE"),
             "3", "ITEMS_SE", AIM_SOUTH},
            {AIM_WEST, point(27, 2), tripoint_rel_ms::west(), _("West"), _("W"), "4", "ITEMS_W",
             AIM_NORTHWEST},
            {AIM_CENTER, point(30, 2), tripoint_rel_ms::zero(), _("Directly below you"), _("DN"),
             "5", "ITEMS_CE", AIM_CENTER},
            {AIM_EAST, point(33, 2), tripoint_rel_ms::east(), _("East"), _("E"), "6", "ITEMS_E",
             AIM_SOUTHEAST},
            {AIM_NORTHWEST, point(27, 1), tripoint_rel_ms::north_west(), _("North West"), _("NW"),
             "7", "ITEMS_NW", AIM_NORTH},
            {AIM_NORTH, point(30, 1), tripoint_rel_ms::north(), _("North"), _("N"), "8", "ITEMS_N",
             AIM_NORTHEAST},
            {AIM_NORTHEAST, point(33, 1), tripoint_rel_ms::north_east(), _("North East"), _("NE"),
             "9", "ITEMS_NE", AIM_EAST},
            {AIM_ABOVE, point(36, 1), tripoint_rel_ms::above(), _("Above"), _("UP"), "<",
             "ITEMS_UP", AIM_ABOVE},
            {AIM_BELOW, point(36, 2), tripoint_rel_ms::below(), _("Below"), _("DN"), ">",
             "ITEMS_DOWN", AIM_BELOW},
            {AIM_DRAGGED, point(22, 1), tripoint_rel_ms::zero(), _("Grabbed Vehicle"), _("GR"), "D",
             "ITEMS_DRAGGED_CONTAINER", AIM_DRAGGED},
            {AIM_ALL, point(19, 3), tripoint_rel_ms::zero(), _("Surrounding area"), _("AL"), "A",
             "ITEMS_AROUND", AIM_ALL},
            {AIM_CONTAINER, point(19, 1), tripoint_rel_ms::zero(), _("Container"), _("CN"), "C",
             "ITEMS_CONTAINER", AIM_CONTAINER},
            {AIM_WORN, point(22, 3), tripoint_rel_ms::zero(), _("Worn Items"), _("WR"), "W",
             "ITEMS_WORN", AIM_WORN}}}) {
    save_state = &uistate.transfer_save;
}
// *INDENT-ON*

advanced_inventory::~advanced_inventory()
{
    save_settings( false );
    if( save_state->exit_code != exit_re_entry ) { save_state->exit_code = exit_okay; }
    // Only refresh if we exited manually, otherwise we're going to be right back
    if( exit ) { g->u.check_item_encumbrance_flag(); }
}

void advanced_inventory::save_settings( bool only_panes )
{
    if( !only_panes ) { save_state->active_left = ( src == left ); }
    for( int i = 0; i < NUM_PANES; ++i ) { panes[i].save_settings(); }
}

void advanced_inventory::load_settings()
{
    aim_exit aim_code = static_cast<aim_exit>( save_state->exit_code );
    panes[left].load_settings( save_state->saved_area, squares, aim_code == exit_re_entry );
    panes[right].load_settings( save_state->saved_area_right, squares, aim_code == exit_re_entry );
    save_state->exit_code = exit_none;
}

std::string advanced_inventory::get_sortname( advanced_inv_sortby sortby )
{
    switch( sortby ) {
        case SORTBY_NONE:
            return _( "none" );
        case SORTBY_NAME:
            return _( "name" );
        case SORTBY_WEIGHT:
            return _( "weight" );
        case SORTBY_VOLUME:
            return _( "volume" );
        case SORTBY_CHARGES:
            return _( "charges" );
        case SORTBY_CATEGORY:
            return _( "category" );
        case SORTBY_DAMAGE:
            return _( "damage" );
        case SORTBY_AMMO:
            return _( "ammo/charge type" );
        case SORTBY_SPOILAGE:
            return _( "spoilage" );
        case SORTBY_PRICE:
            return _( "barter value" );
    }
    return "!BUG!";
}

bool advanced_inventory::get_square( const std::string& action, aim_location& ret )
{
    for( advanced_inv_area& s : squares ) {
        if( s.actionname == action ) {
            ret = screen_relative_location( s.id );
            return true;
        }
    }
    return false;
}

aim_location advanced_inventory::screen_relative_location( aim_location area )
{
    if( tile_iso ) {
        return squares[area].relative_location;
    } else {
        return area;
    }
}

inline std::string advanced_inventory::get_location_key( aim_location area )
{
    return squares[area].minimapname;
}

void advanced_inventory::init()
{
    for( auto& square : squares ) { square.init(); }

    panes[left].save_state = &save_state->pane;
    panes[right].save_state = &save_state->pane_right;

    load_settings();

    src = ( save_state->active_left ) ? left : right;
    dest = ( save_state->active_left ) ? right : left;
}

struct advanced_inv_sorter {
    advanced_inv_sortby sortby;
    advanced_inv_sorter( advanced_inv_sortby sort ) { sortby = sort; }
    bool operator()( const advanced_inv_listitem& d1, const advanced_inv_listitem& d2 ) {
        // Note: the item pointer can only be null on sort by category, otherwise it is always
        // valid.
        switch( sortby ) {
            case SORTBY_NONE:
                if( d1.idx != d2.idx ) { return d1.idx < d2.idx; }
                break;
            case SORTBY_NAME:
                // Fall through to code below the switch
                break;
            case SORTBY_WEIGHT:
                if( d1.weight != d2.weight ) { return d1.weight > d2.weight; }
                break;
            case SORTBY_VOLUME:
                if( d1.volume != d2.volume ) { return d1.volume > d2.volume; }
                break;
            case SORTBY_CHARGES:
                if( d1.items.front()->charges != d2.items.front()->charges ) {
                    return d1.items.front()->charges > d2.items.front()->charges;
                }
                break;
            case SORTBY_CATEGORY:
                assert( d1.cat != nullptr );
                assert( d2.cat != nullptr );
                if( d1.cat != d2.cat ) {
                    return *d1.cat < *d2.cat;
                } else if( d1.is_category_header() ) {
                    return true;
                } else if( d2.is_category_header() ) {
                    return false;
                }
                break;
            case SORTBY_DAMAGE:
                if( d1.items.front()->damage() != d2.items.front()->damage() ) {
                    return d1.items.front()->damage() < d2.items.front()->damage();
                }
                break;
            case SORTBY_AMMO: {
                const std::string a1 = d1.items.front()->ammo_sort_name();
                const std::string a2 = d2.items.front()->ammo_sort_name();
                // There are many items with "false" ammo types (e.g.
                // scrap metal has "components") that actually is not
                // used as ammo, so we consider them as non-ammo.
                const bool ammoish1 =
                    !a1.empty() && a1 != "components" && a1 != "none" && a1 != "NULL";
                const bool ammoish2 =
                    !a2.empty() && a2 != "components" && a2 != "none" && a2 != "NULL";
                if( ammoish1 != ammoish2 ) {
                    return ammoish1;
                } else if( ammoish1 && ammoish2 ) {
                    if( a1 == a2 ) {
                        // For items with the same ammo type, we sort:
                        // guns > tools > magazines > ammunition
                        if( d1.items.front()->is_gun() && !d2.items.front()->is_gun() ) {
                            return true;
                        }
                        if( !d1.items.front()->is_gun() && d2.items.front()->is_gun() ) {
                            return false;
                        }
                        if( d1.items.front()->is_tool() && !d2.items.front()->is_tool() ) {
                            return true;
                        }
                        if( !d1.items.front()->is_tool() && d2.items.front()->is_tool() ) {
                            return false;
                        }
                        if( d1.items.front()->is_magazine() && d2.items.front()->is_ammo() ) {
                            return true;
                        }
                        if( d2.items.front()->is_magazine() && d1.items.front()->is_ammo() ) {
                            return false;
                        }
                    }
                    return localized_compare( a1, a2 );
                }
            }
            break;
            case SORTBY_SPOILAGE:
                if( d1.items.front()->spoilage_sort_order()
                    != d2.items.front()->spoilage_sort_order() ) {
                    return d1.items.front()->spoilage_sort_order()
                           < d2.items.front()->spoilage_sort_order();
                }
                break;
            case SORTBY_PRICE:
                if( d1.items.front()->price( true ) != d2.items.front()->price( true ) ) {
                    return d1.items.front()->price( true ) > d2.items.front()->price( true );
                }
                break;
        }
        // secondary sort by name
        const std::string* n1;
        const std::string* n2;
        if( d1.name_without_prefix == d2.name_without_prefix ) {
            // if names without prefix equal, compare full name
            n1 = &d1.name;
            n2 = &d2.name;
        } else {
            // else compare name without prefix
            n1 = &d1.name_without_prefix;
            n2 = &d2.name_without_prefix;
        }
        return localized_compare( *n1, *n2 );
    }
};

std::string advanced_inventory::aim_area_grid_html(
    advanced_inventory_pane& pane, aim_location sel )
{
    const int area = pane.get_area();
    // One [key] cell, coloured by state — mirrors print_header (540-557).
    const auto cell = [&]( aim_location loc ) -> std::string {
        const aim_location data_location = screen_relative_location( loc );
        const char *bracket = squares[data_location].can_store_in_vehicle() ? "<>" : "[]";
        const bool in_vehicle =
        pane.in_vehicle() && squares[data_location].id == area && sel == area
            && area != AIM_ALL;
        const bool all_brackets =
        area == AIM_ALL && ( data_location >= AIM_SOUTHWEST && data_location <= AIM_NORTHEAST );
        nc_color bcolor = c_red;
        nc_color kcolor = c_red;
        if( squares[data_location].canputitems( pane.get_cur_item_ptr() ) )
        {
            bcolor =
            in_vehicle ? c_light_blue
            : ( area == data_location || all_brackets ? c_light_gray : c_dark_gray );
            kcolor =
            area == data_location
            ? c_white
            : ( sel == data_location ? c_light_gray : c_dark_gray );
        }
        const std::string key =
        in_vehicle && sel != AIM_DRAGGED ? std::string( "V" ) : get_location_key( loc );
        return "<span class=\"aim-gcell\">"
        + cata_text_to_rml(
            colorize( std::string( 1, bracket[0] ), bcolor ) + colorize( key, kcolor )
            + colorize( std::string( 1, bracket[1] ), bcolor ) )
        + "</span>";
    };
    const aim_location rows[3][3] = {
        {AIM_NORTHWEST, AIM_NORTH, AIM_NORTHEAST},
        {AIM_WEST, AIM_CENTER, AIM_EAST},
        {AIM_SOUTHWEST, AIM_SOUTH, AIM_SOUTHEAST}
    };
    std::string out = "<div class=\"aim-grid\">";
    for( const aim_location * r : {rows[0], rows[1], rows[2]} ) {
        out += "<div class=\"aim-grow\">";
        for( int i = 0; i < 3; i++ ) { out += cell( r[i] ); }
        out += "</div>";
    }
    out += "</div><div class=\"aim-grow aim-specials\">";
    for( const aim_location loc : {AIM_INVENTORY, AIM_WORN, AIM_ALL, AIM_DRAGGED, AIM_CONTAINER} ) {
        out += cell( loc );
    }
    out += "</div>";
    return out;
}

void advanced_inventory::recalc_pane( side p )
{
    auto& pane = panes[p];
    pane.recalc = false;
    pane.items.clear();
    // Add items from the source location or in case of all 9 surrounding squares,
    // add items from several locations.
    if( pane.get_area() == AIM_ALL ) {
        auto& alls = squares[AIM_ALL];
        auto& there = panes[-p + 1];
        auto& other = squares[there.get_area()];
        alls.volume = 0_ml;
        alls.weight = 0_gram;
        for( auto& s : squares ) {
            // All the surrounding squares, nothing else
            if( s.id < AIM_SOUTHWEST || s.id > AIM_NORTHEAST ) { continue; }

            // To allow the user to transfer all items from all surrounding squares to
            // a specific square, filter out items that are already on that square.
            // e.g. left pane AIM_ALL, right pane AIM_NORTH. The user holds the
            // enter key down in the left square and moves all items to the other side.
            const bool same = other.is_same( s );

            // Deal with squares with ground + vehicle storage
            // Also handle the case when the other tile covers vehicle
            // or the ground below the vehicle.
            if( s.can_store_in_vehicle() && !( same && there.in_vehicle() ) ) {
                bool do_vehicle = there.get_area() == s.id ? !there.in_vehicle() : true;
                pane.add_items_from_area( s, do_vehicle );
                alls.volume += s.volume;
                alls.weight += s.weight;
            }

            // Add map items
            if( !same || there.in_vehicle() ) {
                pane.add_items_from_area( s );
                alls.volume += s.volume;
                alls.weight += s.weight;
            }
        }
    } else {
        pane.add_items_from_area( squares[pane.get_area()] );
    }
    // Insert category headers (only expected when sorting by category)
    if( pane.sortby == SORTBY_CATEGORY ) {
        std::set<const item_category *> categories;
        for( auto& it : pane.items ) { categories.insert( it.cat ); }
        for( auto& cat : categories ) { pane.items.emplace_back( cat ); }
    }
    // Finally sort all items (category headers will now be moved to their proper position)
    std::ranges::stable_sort( pane.items, advanced_inv_sorter( pane.sortby ) );
    // itemsPerPage is 0 during processing
    if( itemsPerPage > 0 ) { pane.paginate( itemsPerPage ); }
}

// be explicit with the values
enum aim_entry { ENTRY_START = 0, ENTRY_VEHICLE = 1, ENTRY_MAP = 2, ENTRY_RESET = 3 };

bool advanced_inventory::move_all_items( bool nested_call )
{
    advanced_inventory_pane& spane = panes[src];
    advanced_inventory_pane& dpane = panes[dest];

    // AIM_ALL source area routine
    if( spane.get_area() == AIM_ALL ) {
        // move all to `AIM_WORN' doesn't make sense (see `MAX_WORN_PER_TYPE')
        if( dpane.get_area() == AIM_WORN ) {
            popup( _( "You look at the items, then your clothes, and scratch your head…" ) );
            return false;
        }
        // if the source pane (AIM_ALL) is empty, then show a message and leave
        if( !is_processing() && spane.items.empty() ) {
            popup( _( "There are no items to be moved!" ) );
            return false;
        }

        advanced_inv_area& sarea = squares[spane.get_area()];
        advanced_inv_area& darea = squares[dpane.get_area()];

        // Check first if the destination area still have enough room for moving all.
        if( !is_processing() && sarea.volume > darea.free_volume( dpane.in_vehicle() )
            && !query_yn( _( "There isn't enough room, do you really want to move all?" ) ) ) {
            return false;
        }

        // make sure that there are items to be moved
        bool done = false;
        // copy the current pane, to be restored after the move is queued
        advanced_inventory_pane shadow = panes[src];
        // here we recursively call this function with each area in order to
        // put all items in the proper destination area, with minimal fuss
        int &loc = save_state->aim_all_location;
        // re-entry nonsense
        int &entry = save_state->re_enter_move_all;
        // if we are just starting out, set entry to initial value
        switch( static_cast<aim_entry>( entry++ ) ) {
            case ENTRY_START:
                ++entry;
            /* fallthrough */
            case ENTRY_VEHICLE:
                if( squares[loc].can_store_in_vehicle() ) {
                    // either do the inverse of the pane (if it is the one we are transferring to),
                    // or just transfer the contents (if it is not the one we are transferring to)
                    spane.set_area(
                        squares[loc], dpane.get_area() == loc ? !dpane.in_vehicle() : true );
                    // add items, calculate weights and volumes... the fun stuff
                    recalc_pane( src );
                    // then move the items to the destination area
                    move_all_items( true );
                }
                break;
            case ENTRY_MAP:
                spane.set_area( squares[loc++], false );
                recalc_pane( src );
                move_all_items( true );
                break;
            case ENTRY_RESET:
                if( loc > AIM_AROUND_END ) {
                    loc = AIM_AROUND_BEGIN;
                    entry = ENTRY_START;
                    done = true;
                } else {
                    entry = ENTRY_VEHICLE;
                }
                break;
            default:
                debugmsg( "Invalid `aim_entry' [%d] reached!", entry - 1 );
                entry = ENTRY_START;
                loc = AIM_AROUND_BEGIN;
                return false;
        }
        // restore the pane to its former glory
        panes[src] = shadow;
        // make it auto loop back, if not already doing so
        if( !g->u.activity && ( !done || !get_option<bool>( "CLOSE_ADV_INV" ) ) ) { do_return_entry(); }
        return true;
    }

    // Check some preconditions to quickly leave the function.
    size_t liquid_items = 0;
    for( const advanced_inv_listitem& elem : spane.items ) {
        for( const item * elemit : elem.items ) {
            if( elemit->made_of( LIQUID ) ) { liquid_items++; }
        }
    }
    if( spane.items.empty() || liquid_items == spane.items.size() ) { return false; }
    bool restore_area = false;
    if( dpane.get_area() == AIM_ALL ) {
        auto loc = dpane.get_area();
        // ask where we want to store the item via the menu
        if( !query_destination( loc ) ) { return false; }
        restore_area = true;
    }
    if( !squares[dpane.get_area()].canputitems() ) {
        popup( _( "You can't put items there!" ) );
        return false;
    }
    advanced_inv_area& sarea = squares[spane.get_area()];
    advanced_inv_area& darea = squares[dpane.get_area()];

    // Make sure source and destination are different, otherwise items will disappear
    // Need to check actual position to account for dragged vehicles
    if( dpane.get_area() == AIM_DRAGGED && sarea.pos == darea.pos
        && spane.in_vehicle() == dpane.in_vehicle() ) {
        return false;
    } else if( spane.get_area() == dpane.get_area() && spane.in_vehicle() == dpane.in_vehicle() ) {
        return false;
    }

    if( nested_call || !get_option<bool>( "CLOSE_ADV_INV" ) ) {
        // Why is this here? It's because the activity backlog can act
        // like a stack instead of a single deferred activity in order to
        // accomplish some UI shenanigans. The inventory menu activity is
        // added, then an activity to drop is pushed on the stack, then
        // the drop activity is repeatedly popped and pushed on the stack
        // until all its items are processed. When the drop activity runs out,
        // the inventory menu activity is there waiting and seamlessly returns
        // the player to the menu. If the activity is interrupted instead of
        // completing, both activities are canceled.
        // Thanks to kevingranade for the explanation.
        do_return_entry();
    }

    map& here = get_map();
    if( spane.get_area() == AIM_INVENTORY || spane.get_area() == AIM_WORN ) {
        drop_locations dropped;
        // keep a list of favorites separated, only drop non-fav first if they exist
        drop_locations dropped_favorite;

        if( spane.get_area() == AIM_INVENTORY ) {
            for( size_t index = 0; index < g->u.inv_size(); ++index ) {
                const std::vector<item *> &stack = g->u.inv_const_stack( index );
                item* const& it = stack.front();

                if( !spane.is_filtered( *it ) ) {
                    int count;
                    if( it->count_by_charges() ) {
                        count = it->charges;
                    } else {
                        count = stack.size();
                    }
                    if( it->is_favorite ) {
                        dropped_favorite.emplace_back( *it, count );
                    } else {
                        dropped.emplace_back( *it, count );
                    }
                }
            }
        } else if( spane.get_area() == AIM_WORN ) {
            // do this in reverse, to account for vector item removal messing with future indices
            auto iter = g->u.worn.rbegin();
            for( size_t idx = 0; idx < g->u.worn.size(); ++idx, ++iter ) {
                item& it = **iter;

                if( !g->u.can_takeoff( it ).success() ) { continue; }

                if( !spane.is_filtered( it ) ) {
                    if( it.is_favorite ) {
                        dropped_favorite.emplace_back( it, it.count() );
                    } else {
                        dropped.emplace_back( it, it.count() );
                    }
                }
            }
        }
        if( dropped.empty() ) {
            if( !query_yn( _( "Really drop all your favorite items?" ) ) ) { return false; }
            dropped = dropped_favorite;
        }

        g->u.drop( dropped, g->u.bub_pos() + darea.off );
    } else {
        if( dpane.get_area() == AIM_WORN ) {
            // TODO: Start ACT_WEAR in this case
            debugmsg( "Wearing clothes using move all is not yet implemented" );
        } else {
            // Vehicle and map destinations are handled the same.
            // Check first if the destination area still have enough room for moving all.
            if( !is_processing() && sarea.volume > darea.free_volume( dpane.in_vehicle() )
                && !query_yn( _( "There isn't enough room, do you really want to move all?" ) ) ) {
                return false;
            }

            // Stash the destination
            const tripoint_rel_ms relative_destination = darea.off;

            // Find target items and quantities thereof for the new activity
            std::vector<item *> target_items;
            std::vector<int> quantities;

            item_stack::iterator stack_begin, stack_end;
            if( panes[src].in_vehicle() ) {
                vehicle_stack targets = sarea.veh->get_items( sarea.vstor );
                stack_begin = targets.begin();
                stack_end = targets.end();
            } else {
                map_stack targets = here.i_at( sarea.pos );
                stack_begin = targets.begin();
                stack_end = targets.end();
            }

            // If moving to vehicle, silently filter buckets
            // Moving them would cause tons of annoying prompts or spills
            const bool filter_buckets = dpane.in_vehicle();
            bool filtered_any_bucket = false;
            // Push item_locations and item counts for all items at placement
            for( item_stack::iterator it = stack_begin; it != stack_end; ++it ) {
                if( spane.is_filtered( **it ) ) { continue; }
                if( filter_buckets && ( *it )->is_bucket_nonempty() ) {
                    filtered_any_bucket = true;
                    continue;
                }
                if( spane.in_vehicle() ) {
                    target_items.emplace_back( *it );
                } else {
                    target_items.emplace_back( *it );
                }
                // quantity of 0 means move all
                quantities.push_back( 0 );
            }

            if( filtered_any_bucket ) {
                add_msg( m_info, _( "Skipping filled buckets to avoid spilling their contents." ) );
            }

            if( dpane.get_area() == AIM_INVENTORY ) {
                std::vector<pickup::pick_drop_selection> targets =
                    pickup::optimize_pickup( target_items, quantities );
                g->u.assign_activity(
                    std::make_unique<player_activity>( std::make_unique<pickup_activity_actor>(
                            targets,
                            panes[src].in_vehicle()
                            ? std::nullopt
                            : std::optional<tripoint_bub_ms>( g->u.bub_pos() ) ) ) );
            } else {
                g->u.assign_activity(
                    std::make_unique<player_activity>( std::make_unique<move_items_activity_actor>(
                            target_items, quantities, dpane.in_vehicle(), relative_destination ) ) );
            }
        }
    }
    // if dest was AIM_ALL then we used query_destination and should undo that
    if( restore_area ) { dpane.restore_area(); }
    return true;
}

bool advanced_inventory::show_sort_menu( advanced_inventory_pane& pane )
{
    uilist sm;
    sm.text = _( "Sort by…" );
    sm.addentry( SORTBY_NONE, true, 'u', _( "Unsorted (recently added first)" ) );
    sm.addentry( SORTBY_NAME, true, 'n', get_sortname( SORTBY_NAME ) );
    sm.addentry( SORTBY_WEIGHT, true, 'w', get_sortname( SORTBY_WEIGHT ) );
    sm.addentry( SORTBY_VOLUME, true, 'v', get_sortname( SORTBY_VOLUME ) );
    sm.addentry( SORTBY_CHARGES, true, 'x', get_sortname( SORTBY_CHARGES ) );
    sm.addentry( SORTBY_CATEGORY, true, 'c', get_sortname( SORTBY_CATEGORY ) );
    sm.addentry( SORTBY_DAMAGE, true, 'd', get_sortname( SORTBY_DAMAGE ) );
    sm.addentry( SORTBY_AMMO, true, 'a', get_sortname( SORTBY_AMMO ) );
    sm.addentry( SORTBY_SPOILAGE, true, 's', get_sortname( SORTBY_SPOILAGE ) );
    sm.addentry( SORTBY_PRICE, true, 'b', get_sortname( SORTBY_PRICE ) );
    // Pre-select current sort.
    sm.selected = pane.sortby - SORTBY_NONE;
    // Calculate key and window variables, generate window,
    // and loop until we get a valid answer.
    sm.query();
    if( sm.ret < SORTBY_NONE ) { return false; }
    pane.sortby = static_cast<advanced_inv_sortby>( sm.ret );
    return true;
}

input_context advanced_inventory::register_ctxt() const
{
    input_context ctxt( "ADVANCED_INVENTORY" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "UP" );
    ctxt.register_action( "DOWN" );
    ctxt.register_action( "LEFT" );
    ctxt.register_action( "RIGHT" );
    ctxt.register_action( "PAGE_DOWN" );
    ctxt.register_action( "PAGE_UP" );
    ctxt.register_action( "TOGGLE_TAB" );
    ctxt.register_action( "TOGGLE_VEH" );
    ctxt.register_action( "FILTER" );
    ctxt.register_action( "RESET_FILTER" );
    ctxt.register_action( "TOGGLE_FILTER_AUTORESET" );
    ctxt.register_action( "EXAMINE" );
    ctxt.register_action( "SORT" );
    ctxt.register_action( "TOGGLE_AUTO_PICKUP" );
    ctxt.register_action( "TOGGLE_FAVORITE" );
    ctxt.register_action( "MOVE_SINGLE_ITEM" );
    ctxt.register_action( "MOVE_VARIABLE_ITEM" );
    ctxt.register_action( "MOVE_ITEM_STACK" );
    ctxt.register_action( "MOVE_ALL_ITEMS" );
    ctxt.register_action( "CATEGORY_SELECTION" );
    ctxt.register_action( "ITEMS_NW" );
    ctxt.register_action( "ITEMS_N" );
    ctxt.register_action( "ITEMS_NE" );
    ctxt.register_action( "ITEMS_W" );
    ctxt.register_action( "ITEMS_CE" );
    ctxt.register_action( "ITEMS_E" );
    ctxt.register_action( "ITEMS_SW" );
    ctxt.register_action( "ITEMS_S" );
    ctxt.register_action( "ITEMS_SE" );
    ctxt.register_action( "ITEMS_UP" );
    ctxt.register_action( "ITEMS_DOWN" );
    ctxt.register_action( "ITEMS_INVENTORY" );
    ctxt.register_action( "ITEMS_WORN" );
    ctxt.register_action( "ITEMS_AROUND" );
    ctxt.register_action( "ITEMS_DRAGGED_CONTAINER" );
    ctxt.register_action( "ITEMS_CONTAINER" );

    ctxt.register_action( "ITEMS_DEFAULT" );
    ctxt.register_action( "SAVE_DEFAULT" );

    return ctxt;
}

void advanced_inventory::change_square(
    const aim_location changeSquare, advanced_inventory_pane& dpane,
    advanced_inventory_pane& spane )
{
    if( panes[left].get_area() == changeSquare || panes[right].get_area() == changeSquare ) {
        if( squares[changeSquare].can_store_in_vehicle() && changeSquare != AIM_DRAGGED ) {
            // only deal with spane, as you can't _directly_ change dpane
            if( dpane.get_area() == changeSquare ) {
                spane.set_area( squares[changeSquare], !dpane.in_vehicle() );
                spane.recalc = true;
            } else if( spane.get_area() == dpane.get_area() ) {
                // swap the `in_vehicle` element of each pane if "one in, one out"
                spane.set_area( squares[spane.get_area()], !spane.in_vehicle() );
                dpane.set_area( squares[dpane.get_area()], !dpane.in_vehicle() );
                recalc = true;
            }
        } else {
            swap_panes();
        }
        // we need to check the original area if we can place items in vehicle storage
    } else if( squares[changeSquare].canputitems( spane.get_cur_item_ptr() ) ) {
        bool in_vehicle_cargo = false;
        if( changeSquare == AIM_CONTAINER ) {
            squares[changeSquare].set_container( spane.get_cur_item_ptr() );
        } else if( spane.get_area() == AIM_CONTAINER ) {
            squares[changeSquare].set_container( nullptr );
            // auto select vehicle if items exist at said square, or both are empty
        } else if( squares[changeSquare].can_store_in_vehicle()
                   && spane.get_area() != changeSquare ) {
            if( changeSquare == AIM_DRAGGED ) {
                in_vehicle_cargo = true;
            } else {
                // check item stacks in vehicle and map at said square
                auto sq = squares[changeSquare];
                auto map_stack = get_map().i_at( sq.pos );
                auto veh_stack = sq.veh->get_items( sq.vstor );
                // auto switch to vehicle storage if vehicle items are there, or neither are there
                if( !veh_stack.empty() || map_stack.empty() ) { in_vehicle_cargo = true; }
            }
        }
        spane.set_area( squares[changeSquare], in_vehicle_cargo );
        spane.index = 0;
        spane.recalc = true;
        if( dpane.get_area() == AIM_ALL ) { dpane.recalc = true; }
    } else {
        popup( _( "You can't put items there!" ) );
    }
}

void advanced_inventory::start_activity(
    const aim_location destarea, advanced_inv_listitem* sitem, int &amount_to_move,
    const bool from_vehicle, const bool to_vehicle ) const
{

    const bool by_charges = sitem->items.front()->count_by_charges();

    if( destarea == AIM_WORN ) {
        std::vector<safe_reference<item>> wear_items;
        std::vector<int> wear_quantities;
        if( by_charges ) {
            wear_items.emplace_back( sitem->items.front() );
            wear_quantities.push_back( amount_to_move );
        } else {
            for( std::list<item * >::iterator it = sitem->items.begin();
                 amount_to_move > 0 && it != sitem->items.end(); ++it ) {
                wear_items.emplace_back( *it );
                wear_quantities.push_back( 0 );
                --amount_to_move;
            }
        }
        g->u.assign_activity( std::make_unique<player_activity>(
                                  std::make_unique <
                                  wear_activity_actor > ( std::move( wear_items ), std::move( wear_quantities ) ) ) );
    } else {
        // Find target items and quantities thereof for the new activity
        std::vector<item *> target_items;
        std::vector<int> quantities;
        if( by_charges ) {
            target_items.emplace_back( sitem->items.front() );
            quantities.push_back( amount_to_move );
        } else {
            for( std::list<item * >::iterator it = sitem->items.begin();
                 amount_to_move > 0 && it != sitem->items.end(); ++it ) {
                target_items.emplace_back( *it );
                quantities.push_back( 0 );
                --amount_to_move;
            }
        }

        if( destarea == AIM_INVENTORY ) {
            std::vector<pickup::pick_drop_selection> targets =
                pickup::optimize_pickup( target_items, quantities );
            g->u.assign_activity(
                std::make_unique<player_activity>( std::make_unique<pickup_activity_actor>(
                        targets,
                        from_vehicle ? std::nullopt : std::optional<tripoint_bub_ms>( g->u.bub_pos() ) ) ) );
        } else {
            // Stash the destination
            const tripoint_rel_ms relative_destination = squares[destarea].off;

            g->u.assign_activity(
                std::make_unique<player_activity>( std::make_unique<move_items_activity_actor>(
                        target_items, quantities, to_vehicle, relative_destination ) ) );
        }
    }
}

bool advanced_inventory::action_move_item(
    advanced_inv_listitem* sitem, advanced_inventory_pane& dpane,
    const advanced_inventory_pane& spane, const std::string& action )
{
    bool exit = false;
    if( sitem == nullptr || !sitem->is_item_entry() ) { return false; }
    aim_location destarea = dpane.get_area();
    aim_location srcarea = sitem->area;
    bool restore_area = destarea == AIM_ALL;
    if( !query_destination( destarea ) ) { return false; }
    // Not necessarily equivalent to spane.in_vehicle() if using AIM_ALL
    bool from_vehicle = sitem->from_vehicle;
    bool to_vehicle = dpane.in_vehicle();

    // AIM_ALL should disable same area check and handle it with proper filtering instead.
    // This is a workaround around the lack of vehicle location info in
    // either aim_location or advanced_inv_listitem.
    if( squares[srcarea].is_same( squares[destarea] ) && spane.get_area() != AIM_ALL
        && spane.in_vehicle() == dpane.in_vehicle() ) {
        popup( _( "Source area is the same as destination (%s)." ), squares[destarea].name );
        return false;
    }
    assert( !sitem->items.empty() );
    int amount_to_move = 0;
    if( !query_charges( destarea, *sitem, action, amount_to_move ) ) { return false; }
    // This makes sure that all item references in the advanced_inventory_pane::items vector
    // are recalculated, even when they might not have changed, but they could (e.g. items
    // taken from inventory, but unable to put into the cargo trunk go back into the inventory,
    // but are potentially at a different place).
    recalc = true;
    assert( amount_to_move > 0 );
    if( destarea == AIM_CONTAINER ) {
        if( !move_content( *sitem->items.front(), *squares[destarea].get_container( to_vehicle ) ) ) {
            return false;
        }
    } else if( srcarea == AIM_INVENTORY ) {

        // make sure advanced inventory is reopened after activity completion.
        do_return_entry();

        if( destarea == AIM_WORN ) {
            std::vector<safe_reference<item>> wear_items;
            std::vector<int> wear_quantities;
            wear_items.emplace_back( sitem->items.front() );
            wear_quantities.push_back( amount_to_move );
            g->u.assign_activity( std::make_unique<player_activity>(
                                      std::make_unique <
                                      wear_activity_actor > ( std::move( wear_items ), std::move( wear_quantities ) ) ) );
        } else {
            item* itm = &g->u.i_at( sitem->idx );

            drop_locations to_move = {drop_location( *itm, amount_to_move )};
            g->u.assign_activity( std::make_unique<player_activity>(
                                      std::make_unique <
                                      drop_activity_actor > ( g->u, to_move, !to_vehicle, squares[destarea].off ) ) );
        }
        // exit so that the activity can be carried out
        exit = true;

    } else if( srcarea == AIM_WORN ) {

        // make sure advanced inventory is reopened after activity completion.
        do_return_entry();

        // worn items are never stacked, so this should check out
        assert( sitem->items.size() == 1 );
        item* itm = sitem->items.front();
        ret_val<bool> takeoff_rv = g->u.can_takeoff( *itm );
        if( !takeoff_rv.success() ) {
            add_msg( m_info, "%s", takeoff_rv.c_str() );
        } else if( destarea == AIM_INVENTORY ) {
            g->u.takeoff( *itm );
        } else {
            drop_locations to_move = {drop_location( *itm, amount_to_move )};
            g->u.assign_activity( std::make_unique<player_activity>(
                                      std::make_unique <
                                      drop_activity_actor > ( g->u, to_move, !to_vehicle, squares[destarea].off ) ) );
        }
        // exit so that the activity can be carried out
        exit = true;

    } else {
        // from map/vehicle: start ACT_PICKUP or ACT_MOVE_ITEMS as necessary
        // Make sure advanced inventory is reopened after activity completion.
        do_return_entry();
        start_activity( destarea, sitem, amount_to_move, from_vehicle, to_vehicle );

        // exit so that the activity can be carried out
        exit = true;
    }

    // if dest was AIM_ALL then we used query_destination and should undo that
    if( restore_area ) { dpane.restore_area(); }
    return exit;
}

void advanced_inventory::action_examine(
    advanced_inv_listitem* sitem, advanced_inventory_pane& spane, input_context& ctxt )
{
    int ret = 0;
    const auto info_width = [this]() -> int { return w_width / 2; };
    const auto info_startx = [this]() -> int {
        return colstart + ( src == advanced_inventory::side::left ? w_width / 2 : 0 );
    };
    if( spane.get_area() == AIM_INVENTORY || spane.get_area() == AIM_WORN ) {
        int idx =
            spane.get_area() == AIM_INVENTORY
            ? sitem->idx
            : player::worn_position_to_index( sitem->idx );
        item* loc = &g->u.i_at( idx );
        // Setup a "return to AIM" activity. If examining the item creates a new activity
        // (e.g. reading, reloading, activating), the new activity will be put on top of
        // "return to AIM". Once the new activity is finished, "return to AIM" comes back
        // (automatically, see player activity handling) and it re-opens the AIM.
        // If examining the item did not create a new activity, we have to remove
        // "return to AIM".
        do_return_entry();
        assert( g->u.has_activity( ACT_ADV_INVENTORY ) );

        examine_item_menu::run(
            *loc, info_startx, info_width,
            src == advanced_inventory::side::left
            ? examine_item_menu::menu_pos_t::left
            : examine_item_menu::menu_pos_t::right );
        if( !g->u.has_activity( ACT_ADV_INVENTORY ) ) {
            exit = true;
        } else {
            g->u.cancel_activity();
        }
        // Might have changed a stack (activated an item, repaired an item, etc.)
        if( spane.get_area() == AIM_INVENTORY ) { g->u.inv_restack(); }
        recalc = true;
    } else {
        item& it = *sitem->items.front();
        std::vector<iteminfo> dummy;
        std::vector<iteminfo> item_info = it.info();

        item_info_data data( it.tname(), it.type_name(), item_info, dummy );
        data.handle_scrolling = true;

        rml_examine_item( data );
        if( ret == KEY_NPAGE || ret == KEY_DOWN ) {
            spane.scroll_by( +1 );
        } else if( ret == KEY_PPAGE || ret == KEY_UP ) {
            spane.scroll_by( -1 );
        }
    }
}

// ── RmlUi AIM path (Tier 3 sub-project, slice 1: dual-pane item lists) ──────────
namespace
{
// All-scalar model (six baked strings, two panes) — no struct/array registration.
struct aim_session {
    Rml::String left_title_rml, left_head_rml, left_rows_html;
    Rml::String right_title_rml, right_head_rml, right_rows_html;
    // Slice 2: per-pane area-selection grid + the top-bar clock/hints.
    Rml::String left_grid_rml, right_grid_rml;
    Rml::String clock_rml, hints_rml;
    // Slice 3: per-pane sort indicator + filter footer.
    Rml::String left_sort_rml, right_sort_rml;
    Rml::String left_filter_rml, right_filter_rml;
    // Gap-closing: recent message log (replaces the curses display_messages in the
    // head bar). The minimap is intentionally dropped in rml mode — pane→direction is
    // already conveyed by the slice-2 area-selection compass grid.
    Rml::String msglog_rml;
    Rml::DataModelHandle handle;
};

// One pane's rows as baked markup (column-header row + category/item rows). Mirrors
// print_items' per-item logic but emits cells (CSS flex columns, not absolute x).
// SLICE 1 defers: AIM_ALL src column, compact mode, autopickup marker.
std::string aim_pane_rows_html(
    const advanced_inventory_pane& pane, bool active,
    const std::array<advanced_inv_area, NUM_AIM_LOCATIONS> &squares )
{
    // AIM_ALL shows a per-item source-square column (matches print_items).
    const bool show_src = pane.get_area() == AIM_ALL;
    std::string out =
        "<div class=\"aim-colhead\">"
        "<span class=\"aim-c-name\">"
        + rml_escape( _( "Name (charges)" ) ) + "</span>";
    if( show_src ) { out += "<span class=\"aim-c-src\">" + rml_escape( _( "src" ) ) + "</span>"; }
    out +=
        "<span class=\"aim-c-amt\">" + rml_escape( _( "amt" ) )
        + "</span>"
      "<span class=\"aim-c-wt\">"
        + rml_escape( _( "weight" ) )
        + "</span>"
                     "<span class=\"aim-c-vol\">"
        + rml_escape( _( "vol" ) )
        + "</span>"
                     "</div>";
    const std::vector<advanced_inv_listitem> &items = pane.items;
    for( size_t i = 0; i < items.size(); i++ ) {
        const advanced_inv_listitem& sitem = items[i];
        if( sitem.is_category_header() ) {
            out += "<div class=\"aim-cat\">[" + rml_escape( sitem.name ) + "]</div>";
            continue;
        }
        if( !sitem.is_item_entry() ) { continue; }
        const item& it = *sitem.items.front();
        const bool selected = active && pane.index == static_cast<int>( i );
        const nc_color base = active ? it.color_in_inventory() : c_dark_gray;

        const bool stolen = !it.is_owned_by( g->u, true );
        const std::string stolen_pre = stolen ? "<color_light_red>!</color> " : std::string();
        std::string item_name;
        if( it.is_money() ) {
            unsigned int charges_total = 0;
            for( const item * m : sitem.items ) { charges_total += m->charges; }
            item_name = stolen_pre + it.display_money( sitem.items.size(), charges_total );
        } else {
            item_name = stolen_pre + it.display_name();
        }
        if( get_option<bool>( "ITEM_SYMBOLS" ) ) {
            item_name = string_format( "%s %s", it.symbol(), item_name );
        }

        std::string amt_cell;
        if( sitem.stacks > 1 ) {
            const int a = std::min( sitem.stacks, 9999 );
            amt_cell = colorize( string_format( "%d", a ), sitem.stacks > 9999 ? c_red : base );
        }

        double w = convert_weight( sitem.weight );
        nc_color wcol = w > 0 ? base : c_dark_gray;
        int prec;
        if( w >= 1000.0 ) {
            if( w >= 10000.0 ) {
                wcol = c_red;
                w = 9999.0;
            }
            prec = 0;
        } else if( w >= 100.0 ) {
            prec = 1;
        } else {
            prec = 2;
        }
        const std::string wt_cell = colorize( string_format( "%.*f", prec, w ), wcol );

        bool vtr = false;
        double vv = 0.0;
        const std::string vs = format_volume( sitem.volume, 5, &vtr, &vv );
        const nc_color vcol =
            ( vtr && vv > 0.0 ) ? c_red : ( sitem.volume.value() > 0 ? base : c_dark_gray );
        const std::string vol_cell = colorize( vs, vcol );

        const std::string cls = selected ? "aim-row selected" : "aim-row";
        std::string row =
            "<div class=\"" + cls
            + "\">"
          "<span class=\"aim-c-name\">"
            + cata_text_to_rml( colorize( item_name, base ) ) + "</span>";
        if( show_src ) {
            row += "<span class=\"aim-c-src\">"
                   + cata_text_to_rml( colorize( squares[sitem.area].shortname, base ) ) + "</span>";
        }
        row +=
            "<span class=\"aim-c-amt\">" + cata_text_to_rml( amt_cell )
            + "</span>"
          "<span class=\"aim-c-wt\">"
            + cata_text_to_rml( wt_cell )
            + "</span>"
        "<span class=\"aim-c-vol\">"
            + cata_text_to_rml( vol_cell )
            + "</span>"
        "</div>";
        out += row;
    }
    return out;
}

// One pane's weight/volume capacity head (mirrors print_items' formatted_head).
// squares is non-const because get_container() (AIM_CONTAINER) is non-const.
std::string aim_pane_head_html(
    const advanced_inventory_pane& pane, bool active,
    std::array<advanced_inv_area, NUM_AIM_LOCATIONS> &squares )
{
    const aim_location area = pane.get_area();
    const nc_color norm = active ? c_white : c_dark_gray;
    if( area == AIM_INVENTORY || area == AIM_WORN ) {
        const double wcar = convert_weight( g->u.weight_carried() );
        const double wcap = convert_weight( g->u.weight_capacity() );
        const std::string vcar = format_volume( g->u.volume_carried() );
        const std::string vcap = format_volume( g->u.volume_capacity() );
        const nc_color wc = g->u.weight_carried() > g->u.weight_capacity() ? c_red : c_light_green;
        const nc_color vc = g->u.volume_carried() > g->u.volume_capacity() ? c_red : c_light_green;
        return cata_text_to_rml(
                   colorize( string_format( "%.1f", wcar ), wc )
                   + colorize( string_format( "/%.1f %s  ", wcap, weight_units() ), c_light_gray )
                   + colorize( vcar, vc )
                   + colorize( string_format( "/%s %s", vcap, volume_units_abbr() ), c_light_gray ) );
    }
    std::string head;
    if( area == AIM_ALL ) {
        head = string_format(
                   "%3.1f %s  %s %s", convert_weight( squares[area].weight ), weight_units(),
                   format_volume( squares[area].volume ), volume_units_abbr() );
    } else {
        units::volume maxvolume = 0_ml;
        advanced_inv_area& s = squares[area];
        if( area == AIM_CONTAINER && s.get_container( pane.in_vehicle() ) != nullptr ) {
            maxvolume = s.get_container( pane.in_vehicle() )->get_container_capacity();
        } else if( pane.in_vehicle() ) {
            maxvolume = s.veh->max_volume( s.vstor );
        } else {
            maxvolume = get_map().max_volume( s.pos );
        }
        head = string_format(
                   "%3.1f %s  %s/%s %s", convert_weight( s.weight ), weight_units(), format_volume( s.volume ),
                   format_volume( maxvolume ), volume_units_abbr() );
    }
    return cata_text_to_rml( colorize( head, norm ) );
}
} // namespace

bool &advanced_inv_rmlui_enabled()
{
    // Default OFF — opt in via the F4 panel. See rml_screen.h.
    static bool enabled = true;
    return enabled;
}

void advanced_inventory::display()
{
    init();

    g->u.inv_restack();

    input_context ctxt{register_ctxt()};

    exit = false;
    recalc = true;

    std::unique_ptr<string_input_popup> spopup;
    std::unique_ptr<ui_adaptor> ui;

    // RmlUi dual-pane render (slice 1). All-scalar model; both panes' rows + heads
    // are baked into markup strings each sync. The active pane is `src`.
    std::unique_ptr<aim_session> rml_sess;
    rml_doc rml;
    const auto sync_rml = [&]() {
        if( !rml_sess ) { return; }
        advanced_inventory_pane& lp = panes[left];
        advanced_inventory_pane& rp = panes[right];
        const bool l_active = ( src == left );
        rml_sess->left_title_rml = cata_text_to_rml(
                                       colorize( squares[lp.get_area()].name, l_active ? c_white : c_dark_gray ) );
        rml_sess->right_title_rml = cata_text_to_rml(
                                        colorize( squares[rp.get_area()].name, !l_active ? c_white : c_dark_gray ) );
        rml_sess->left_head_rml = aim_pane_head_html( lp, l_active, squares );
        rml_sess->right_head_rml = aim_pane_head_html( rp, !l_active, squares );
        rml_sess->left_rows_html = aim_pane_rows_html( lp, l_active, squares );
        rml_sess->right_rows_html = aim_pane_rows_html( rp, !l_active, squares );
        // Per-pane area-selection grid (sel = current item's area, else pane area).
        const auto pane_sel = []( advanced_inventory_pane & p ) -> aim_location {
            const advanced_inv_listitem *cur = p.get_cur_item_ptr();
            return cur != nullptr ? cur->area : p.get_area();
        };
        rml_sess->left_grid_rml = aim_area_grid_html( lp, pane_sel( lp ) );
        rml_sess->right_grid_rml = aim_area_grid_html( rp, pane_sel( rp ) );
        // Top bar: clock (if the avatar has a watch) + keybinding/reset-filter hints.
        rml_sess->clock_rml =
            g->u.has_watch()
            ? cata_text_to_rml( colorize( to_string_time_of_day( calendar::turn ), c_white ) )
            : Rml::String();
        std::string hints =
            string_format(
                _( "< [<color_yellow>%s</color>] keybindings >" ),
                ctxt.get_desc(
                    "HELP_"
                    "KEYBINDIN"
                    "GS" ) )
            + "   ";
        hints +=
            get_option<bool>( "AIM_AUTORESET_FILTER" )
            ? _( "Reset Filter On Close "
             "[<color_light_green>ON</color>|<color_dark_gray>OFF</color>]" )
            : _( "Reset Filter On Close "
             "[<color_dark_gray>ON</color>|<color_light_green>OFF</color>]" );
        rml_sess->hints_rml = cata_text_to_rml( colorize( hints, c_white ) );
        // Recent message log (head bar — replaces the curses display_messages call;
        // head_height-2 == 3 lines in curses, a touch more here for the flex space).
        std::string mlog;
        for( const std::pair<std::string, std::string> &m : Messages::recent_messages( 4 ) ) {
            mlog +=
                "<div class=\"aim-msg\">"
                + cata_text_to_rml(
                    colorize( m.first + " ", c_dark_gray ) + colorize( m.second, c_light_gray ) )
                + "</div>";
        }
        rml_sess->msglog_rml = mlog;
        // Per-pane sort indicator + item count (mirrors redraw_pane's top line).
        const auto sort_str = [&]( advanced_inventory_pane & p ) -> Rml::String {
            std::string s = string_format(
                _( "< [%s] Sort: %s >" ), ctxt.get_desc( "SORT" ), get_sortname( p.sortby ) );
            const advanced_inv_area &sq = squares[p.get_area()];
            if( sq.max_size > 0 )
            {
                s += string_format( "  < %d/%d >", sq.get_item_count(), sq.max_size );
            }
            return cata_text_to_rml( colorize( s, c_light_gray ) );
        };
        // Per-pane filter footer (mirrors redraw_pane's bottom line); while editing
        // the active pane shows the in-progress query from the popup.
        const auto filter_str = [&]( advanced_inventory_pane & p, bool act ) -> Rml::String {
            const std::string fprefix = string_format(
                _( "[%s] Filter" ),
                ctxt.get_desc(
                    "FILTE"
                    "R" ) );
            if( filter_edit && act && spopup )
            {
                return cata_text_to_rml(
                    colorize( "< " + fprefix + ": ", c_light_gray )
                    + colorize( spopup->text() + "_", c_white ) );
            }
            if( !p.filter.empty() )
            {
                const std::string fsuffix = string_format(
                    _( "[%s] Reset" ),
                    ctxt.get_desc(
                        "RESET_"
                        "FILTER" ) );
                return cata_text_to_rml(
                    colorize( "< " + fprefix + ": ", c_light_gray ) + colorize( p.filter, c_white )
                    + colorize( " >  " + fsuffix, c_light_gray ) );
            }
            return cata_text_to_rml( colorize( "< " + fprefix + " >", c_light_gray ) );
        };
        rml_sess->left_sort_rml = sort_str( lp );
        rml_sess->right_sort_rml = sort_str( rp );
        rml_sess->left_filter_rml = filter_str( lp, l_active );
        rml_sess->right_filter_rml = filter_str( rp, !l_active );
        rml_sess->handle.DirtyVariable( "left_title_rml" );
        rml_sess->handle.DirtyVariable( "right_title_rml" );
        rml_sess->handle.DirtyVariable( "left_head_rml" );
        rml_sess->handle.DirtyVariable( "right_head_rml" );
        rml_sess->handle.DirtyVariable( "left_rows_html" );
        rml_sess->handle.DirtyVariable( "right_rows_html" );
        rml_sess->handle.DirtyVariable( "left_grid_rml" );
        rml_sess->handle.DirtyVariable( "right_grid_rml" );
        rml_sess->handle.DirtyVariable( "clock_rml" );
        rml_sess->handle.DirtyVariable( "hints_rml" );
        rml_sess->handle.DirtyVariable( "msglog_rml" );
        rml_sess->handle.DirtyVariable( "left_sort_rml" );
        rml_sess->handle.DirtyVariable( "right_sort_rml" );
        rml_sess->handle.DirtyVariable( "left_filter_rml" );
        rml_sess->handle.DirtyVariable( "right_filter_rml" );
    };

    if( !is_processing() ) {
        ui = std::make_unique<ui_adaptor>();
        ui->on_screen_resize( [&]( ui_adaptor & ui ) {
            constexpr int min_w_height = 10;
            const int min_w_width = FULL_SCREEN_WIDTH;
            const int max_w_width =
                get_option<bool>( "AIM_WIDTH" )
                ? TERMX
                : std::max( 120, TERMX
                            - 2
                            * ( panel_manager::get_manager().get_width_right()
                                + panel_manager::get_manager().get_width_left() ) );

            w_height = TERMY < min_w_height + head_height ? min_w_height : TERMY - head_height;
            w_width = TERMX < min_w_width ? min_w_width : TERMX > max_w_width ? max_w_width : TERMX;

            //(TERMY>w_height)?(TERMY-w_height)/2:0;
            headstart = 0;
            colstart = TERMX > w_width ? ( TERMX - w_width ) / 2 : 0;

            head = catacurses::
                   newwin( head_height, w_width - minimap_width, point( colstart, headstart ) );
            mm_border = catacurses::newwin(
                            minimap_height + 2, minimap_width + 2,
                            point( colstart + ( w_width - ( minimap_width + 2 ) ), headstart ) );
            minimap = catacurses::newwin(
                          minimap_height, minimap_width,
                          point( colstart + ( w_width - ( minimap_width + 1 ) ), headstart + 1 ) );
            panes[left].window =
                catacurses::newwin( w_height, w_width / 2, point( colstart, headstart + head_height ) );
            panes[right].window = catacurses::newwin(
                                      w_height, w_width / 2, point( colstart + w_width / 2, headstart + head_height ) );

            // 2 for the borders, 5 for the header stuff
            itemsPerPage = w_height - 2 - 5;

            if( filter_edit && spopup ) {
                spopup->window( panes[src].window, point( 4, w_height - 1 ), w_width / 2 - 4 );
            }

            ui.position( point( colstart, headstart ), point( w_width, head_height + w_height ) );
        } );
        ui->mark_resize();

        rml.open( advanced_inv_rmlui_enabled(), "advinv", ctxt, [&]( Rml::DataModelConstructor & c ) {
            rml_sess = std::make_unique<aim_session>();
            c.Bind( "left_title_rml", &rml_sess->left_title_rml );
            c.Bind( "right_title_rml", &rml_sess->right_title_rml );
            c.Bind( "left_head_rml", &rml_sess->left_head_rml );
            c.Bind( "right_head_rml", &rml_sess->right_head_rml );
            c.Bind( "left_rows_html", &rml_sess->left_rows_html );
            c.Bind( "right_rows_html", &rml_sess->right_rows_html );
            c.Bind( "left_grid_rml", &rml_sess->left_grid_rml );
            c.Bind( "right_grid_rml", &rml_sess->right_grid_rml );
            c.Bind( "clock_rml", &rml_sess->clock_rml );
            c.Bind( "hints_rml", &rml_sess->hints_rml );
            c.Bind( "msglog_rml", &rml_sess->msglog_rml );
            c.Bind( "left_sort_rml", &rml_sess->left_sort_rml );
            c.Bind( "right_sort_rml", &rml_sess->right_sort_rml );
            c.Bind( "left_filter_rml", &rml_sess->left_filter_rml );
            c.Bind( "right_filter_rml", &rml_sess->right_filter_rml );
            rml_sess->handle = c.GetModelHandle();
        } );

        ui->on_redraw( [&]( const ui_adaptor & ) {
            // RmlUi owns the AIM screen — the curses draw path is gone. The
            // per-pane prep the old redraw_pane did (recalc + fix_index) must run
            // here, or pane.items stays empty.
            if( rml ) {
                for( side p : {left, right} ) {
                    advanced_inventory_pane& pane = panes[p];
                    if( recalc || pane.recalc ) { recalc_pane( p ); }
                    pane.fix_index();
                }
                sync_rml();
                return;
            }
        } );
    }

    while( !exit ) {
        if( g->u.moves < 0 ) {
            do_return_entry();
            return;
        }
        dest = src == advanced_inventory::side::left
               ? advanced_inventory::side::right
               : advanced_inventory::side::left;

        if( ui ) {
            ui->invalidate_ui();
            if( recalc ) { g->invalidate_main_ui_adaptor(); }
            ui_manager::redraw_invalidated();
        }

        recalc = false;
        // source and destination pane
        advanced_inventory_pane& spane = panes[src];
        advanced_inventory_pane& dpane = panes[dest];
        // current item in source pane, might be null
        advanced_inv_listitem* sitem = spane.get_cur_item_ptr();
        aim_location changeSquare = NUM_AIM_LOCATIONS;

        const std::string action = is_processing() ? "MOVE_ALL_ITEMS" : ctxt.handle_input();
        // After a fiber yield the world may have ticked, freeing item* in pane lists.
        // Synchronously rebuild both panes and re-fetch sitem before any branch uses it.
        if( coop_fiber::active() ) {
            recalc_pane( src );
            recalc_pane( dest );
            panes[src].fix_index();
            panes[dest].fix_index();
            sitem = spane.get_cur_item_ptr();
        }
        if( action == "CATEGORY_SELECTION" ) {
            inCategoryMode = !inCategoryMode;
        } else if( action == "ITEMS_DEFAULT" ) {
            for( side cside : {left, right} ) {
                auto& pane = panes[cside];
                int i_location =
                    cside == left ? save_state->saved_area : save_state->saved_area_right;
                aim_location location = static_cast<aim_location>( i_location );
                if( pane.get_area() != location || location == AIM_ALL ) { pane.recalc = true; }
                pane.set_area( squares[location] );
            }
        } else if( action == "SAVE_DEFAULT" ) {
            save_state->saved_area = panes[left].get_area();
            save_state->saved_area_right = panes[right].get_area();
            popup( _( "Default layout was saved." ) );
        } else if( get_square( action, changeSquare ) ) {
            change_square( changeSquare, dpane, spane );
        } else if( action == "TOGGLE_FAVORITE" ) {
            if( sitem == nullptr || !sitem->is_item_entry() ) { continue; }
            for( auto * item : sitem->items ) { item->set_favorite( !item->is_favorite ); }
            // In case we've merged faved and unfaved items
            recalc = true;
        } else if( action == "MOVE_SINGLE_ITEM" || action == "MOVE_VARIABLE_ITEM"
                   || action == "MOVE_ITEM_STACK" ) {
            exit = action_move_item( sitem, dpane, spane, action );
        } else if( action == "MOVE_ALL_ITEMS" ) {
            exit = move_all_items();
            recalc = true;
        } else if( action == "SORT" ) {
            if( show_sort_menu( spane ) ) { recalc = true; }
        } else if( action == "FILTER" ) {
            std::string filter = spane.filter;
            filter_edit = true;
            if( ui ) {
                spopup = std::make_unique<string_input_popup>();
                spopup->max_length( 256 ).text( filter ).identifier( "adv_inv" );
                ui->mark_resize();
            }

            ime_sentry sentry;

            do {
                if( ui ) { ui_manager::redraw(); }
                std::string new_filter = spopup->query_string( false );
                if( spopup->canceled() ) {
                    // restore original filter
                    spane.set_filter( filter );
                } else {
                    spane.set_filter( new_filter );
                }
            } while( !spopup->canceled() && !spopup->confirmed() );
            filter_edit = false;
            spopup = nullptr;
        } else if( action == "RESET_FILTER" ) {
            spane.set_filter( "" );
        } else if( action == "TOGGLE_FILTER_AUTORESET" ) {
            get_options().get_option( "AIM_AUTORESET_FILTER" ).setNext();
            get_options().save();
        } else if( action == "TOGGLE_AUTO_PICKUP" ) {
            if( sitem == nullptr || !sitem->is_item_entry() ) { continue; }
            if( sitem->autopickup ) {
                get_auto_pickup().remove_rule( sitem->items.front() );
                sitem->autopickup = false;
            } else {
                get_auto_pickup().add_rule( sitem->items.front() );
                sitem->autopickup = true;
            }
            recalc = true;
        } else if( action == "EXAMINE" ) {
            if( sitem == nullptr || !sitem->is_item_entry() ) { continue; }
            action_examine( sitem, spane, ctxt );
        } else if( action == "QUIT" ) {
            exit = true;
            if( get_option<bool>( "AIM_AUTORESET_FILTER" ) ) {
                // reset both filters
                spane.set_filter( "" );
                dpane.set_filter( "" );
            }
        } else if( action == "PAGE_DOWN" ) {
            spane.scroll_by( +itemsPerPage );
        } else if( action == "PAGE_UP" ) {
            spane.scroll_by( -itemsPerPage );
        } else if( action == "DOWN" ) {
            if( inCategoryMode ) {
                spane.scroll_category( +1 );
            } else {
                spane.scroll_by( +1 );
            }
        } else if( action == "UP" ) {
            if( inCategoryMode ) {
                spane.scroll_category( -1 );
            } else {
                spane.scroll_by( -1 );
            }
        } else if( action == "LEFT" ) {
            src = left;
        } else if( action == "RIGHT" ) {
            src = right;
        } else if( action == "TOGGLE_TAB" ) {
            src = dest;
        } else if( action == "TOGGLE_VEH" ) {
            if( squares[spane.get_area()].can_store_in_vehicle() ) {
                // swap the panes if going vehicle will show the same tile
                if( spane.get_area() == dpane.get_area()
                    && spane.in_vehicle() != dpane.in_vehicle() ) {
                    swap_panes();
                    // disallow for dragged vehicles
                } else if( spane.get_area() != AIM_DRAGGED ) {
                    // Toggle between vehicle and ground
                    spane.set_area( squares[spane.get_area()], !spane.in_vehicle() );
                    spane.index = 0;
                    spane.recalc = true;
                    if( dpane.get_area() == AIM_ALL ) { dpane.recalc = true; }
                }
            } else {
                popup( _( "No vehicle storage space there!" ) );
            }
        }
    }
}

class query_destination_callback: public uilist_callback
{
    private:
        advanced_inventory &_adv_inv;

    public:
        query_destination_callback( advanced_inventory& adv_inv ): _adv_inv( adv_inv ) {}
        void refresh( uilist* menu ) override {}
        void draw_rml( uilist* menu, Rml::ElementDocument* doc ) override {
            Rml::Element* cb = doc->GetElementById( "callback" );
            if( !cb ) { return; }
            int sel = 0;
            if( menu->selected >= 0 && static_cast<size_t>( menu->selected ) < menu->entries.size() ) {
                sel = _adv_inv.screen_relative_location( static_cast<aim_location>( menu->selected + 1 ) );
            }
            std::string rml;
            for( int i = 1; i < 10; i++ ) {
                aim_location loc = _adv_inv.screen_relative_location( static_cast<aim_location>( i ) );
                std::string key = _adv_inv.get_location_key( loc );
                advanced_inv_area& square = _adv_inv.get_one_square( loc );
                bool in_vehicle = square.can_store_in_vehicle();
                const char *bracket = in_vehicle ? "<>" : "[]";
                bool canputitems = menu->entries[i - 1].enabled && square.canputitems();
                nc_color color = canputitems ? sel == loc ? h_white : c_light_gray : c_dark_gray;
                rml += colorize( std::string( 1, bracket[0] ), color );
                rml += colorize( key, color );
                rml += colorize( std::string( 1, bracket[1] ), color );
                if( i % 3 == 0 && i < 9 ) {
                    rml += "\n";
                } else if( i < 9 ) {
                    rml += "  ";
                }
            }
            cb->SetInnerRML( cata_text_to_rml( rml ) );
        }
};


bool advanced_inventory::query_destination( aim_location& def )
{
    if( def != AIM_ALL ) {
        if( squares[def].canputitems() ) { return true; }
        popup( _( "You can't put items there!" ) );
        return false;
    }

    uilist menu;
    menu.text = _( "Select destination" );
    /* free space for the squares */
    menu.pad_left_setup = 9;
    query_destination_callback cb( *this );
    menu.callback = &cb;
    menu.menu_style = "grid"; // RmlUi: compact side panel for the 3x3 square grid

    {
        std::vector<aim_location> ordered_locs;
        static_assert(
            AIM_NORTHEAST - AIM_SOUTHWEST == 8,
            "Expected 9 contiguous directions in the "
            "aim_location enum" );
        for( int i = AIM_SOUTHWEST; i <= AIM_NORTHEAST; i++ ) {
            ordered_locs.push_back( screen_relative_location( static_cast<aim_location>( i ) ) );
        }
        for( auto& ordered_loc : ordered_locs ) {
            auto& s = squares[ordered_loc];
            const int size = s.get_item_count();
            std::string prefix = string_format( "%2d/%d", size, MAX_ITEM_IN_SQUARE );
            if( size >= MAX_ITEM_IN_SQUARE ) { prefix += _( " (FULL)" ); }
            menu.addentry(
                ordered_loc, s.canputitems() && s.id != panes[src].get_area(),
                get_location_key( ordered_loc )[0],
                prefix + " " + s.name + " " + ( s.veh != nullptr ? s.veh->name : "" ) );
        }
    }
    // Selected keyed to uilist.entries, which starts at 0.
    menu.selected = save_state->last_popup_dest - AIM_SOUTHWEST;
    menu.query();
    if( menu.ret >= AIM_SOUTHWEST && menu.ret <= AIM_NORTHEAST ) {
        assert( squares[menu.ret].canputitems() );
        def = static_cast<aim_location>( menu.ret );
        // we have to set the destination pane so that move actions will target it
        // we can use restore_area later to undo this
        panes[dest].set_area( squares[def], true );
        save_state->last_popup_dest = menu.ret;
        return true;
    }
    return false;
}

bool advanced_inventory::move_content( item& src_container, item& dest_container )
{
    if( !src_container.is_container() ) {
        popup( _( "Source must be container." ) );
        return false;
    }
    if( src_container.is_container_empty() ) {
        popup( _( "Source container is empty." ) );
        return false;
    }

    item& src_contents = src_container.contents.front();

    if( !src_contents.made_of( LIQUID ) ) {
        popup( _( "You can unload only liquids into target container." ) );
        return false;
    }

    std::string err;
    // TODO: Allow buckets here, but require them to be on the ground or wielded
    const int amount = dest_container.get_remaining_capacity_for_liquid( src_contents, false, &err );
    if( !err.empty() ) {
        popup( err );
        return false;
    }
    if( src_container.is_non_resealable_container() ) {
        if( src_contents.charges > amount ) {
            popup( _( "You can't partially unload liquids from unsealable container." ) );
            return false;
        }
        src_container.on_contents_changed();
    }
    detached_ptr<item> moved = src_contents.split( amount );
    dest_container.fill_with( std::move( moved ), amount );

    uistate.adv_inv_container_content_type = dest_container.contents.front().typeId();

    return true;
}

bool advanced_inventory::query_charges(
    aim_location destarea, const advanced_inv_listitem& sitem, const std::string& action,
    int &amount )
{
    // should be a specific location instead
    assert( destarea != AIM_ALL );
    // valid item is obviously required
    assert( !sitem.items.empty() );
    const item& it = *sitem.items.front();
    advanced_inv_area& p = squares[destarea];
    const bool by_charges = it.count_by_charges();
    const units::volume free_volume = p.free_volume( panes[dest].in_vehicle() );
    // default to move all, unless if being equipped
    const int input_amount =
        by_charges ? it.charges
        : action == "MOVE_SINGLE_ITEM"
        ? 1
        : sitem.stacks;
    // there has to be something to begin with
    assert( input_amount > 0 );
    amount = input_amount;

    // Includes moving from/to inventory and around on the map.
    if( it.made_of( LIQUID ) ) {
        popup( _( "You can't pick up a liquid." ) );
        return false;
    }

    // Check volume, this should work the same for inventory, map and vehicles, but not for worn
    const int room_for = it.charges_per_volume( free_volume );
    if( amount > room_for && squares[destarea].id != AIM_WORN ) {
        if( room_for <= 0 ) {
            popup( _( "Destination area is full.  Remove some items first." ) );
            return false;
        }
        amount = std::min( room_for, amount );
    }
    // Map and vehicles have a maximal item count, check that. Inventory does not have this.
    if( destarea != AIM_INVENTORY && destarea != AIM_WORN && destarea != AIM_CONTAINER ) {
        const int cntmax = p.max_size - p.get_item_count();
        // For items counted by charges, adding it adds 0 items if something there stacks with
        // it.
        const bool adds0 =
            by_charges
            && std::any_of(
                panes[dest].items.begin(), panes[dest].items.end(),
        [&it]( const advanced_inv_listitem & li ) {
            return li.is_item_entry() && li.items.front()->stacks_with( it );
        } );
        if( cntmax <= 0 && !adds0 ) {
            popup( _( "Destination area has too many items.  Remove some first." ) );
            return false;
        }
        // Items by charge count as a single item, regardless of the charges. As long as the
        // destination can hold another item, one can move all charges.
        if( !by_charges ) { amount = std::min( cntmax, amount ); }
    }
    // Inventory has a weight capacity, map and vehicle don't have that
    if( destarea == AIM_INVENTORY || destarea == AIM_WORN ) {
        const units::mass unitweight = it.weight() / ( by_charges ? it.charges : 1 );
        const units::mass max_weight =
            g->u.has_trait( trait_DEBUG_STORAGE )
            ? units::mass_max
            : g->u.weight_capacity() * 4 - g->u.weight_carried();
        if( unitweight > 0_gram && unitweight * amount > max_weight ) {
            const int weightmax = max_weight / unitweight;
            if( weightmax <= 0 ) {
                popup( _( "This is too heavy!" ) );
                return false;
            }
            amount = std::min( weightmax, amount );
        }
    }
    // handle how many of armor type we can equip (max of 2 per type)
    if( destarea == AIM_WORN ) {
        const auto& id = sitem.items.front()->typeId();
        // how many slots are available for the item?
        const int slots_available = MAX_WORN_PER_TYPE - g->u.amount_worn( id );
        // base the amount to equip on amount of slots available
        amount = std::min( slots_available, input_amount );
    }
    // Now we have the final amount. Query if requested or limited room left.
    if( action == "MOVE_VARIABLE_ITEM" || amount < input_amount ) {
        const int count = by_charges ? it.charges : sitem.stacks;
        const char *msg = nullptr;
        std::string popupmsg;
        if( amount >= input_amount ) {
            msg = _( "How many do you want to move?  [Have %d] (0 to cancel)" );
            popupmsg = string_format( msg, count );
        } else {
            msg = _( "Destination can only hold %d!  Move how many?  [Have %d] (0 to cancel)" );
            popupmsg = string_format( msg, amount, count );
        }
        // At this point amount contains the maximal amount that the destination can hold.
        const int possible_max = std::min( input_amount, amount );
        if( amount <= 0 ) {
            popup( _( "The destination is already full!" ) );
        } else {
            amount = string_input_popup().title( popupmsg ).width( 20 ).only_digits( true ).query_int();
        }
        if( amount <= 0 ) { return false; }
        if( amount > possible_max ) { amount = possible_max; }
    }
    return true;
}

void advanced_inventory::swap_panes()
{
    // Switch left and right pane.
    std::swap( panes[left], panes[right] );
    // Switch save states
    std::swap( panes[left].save_state, panes[right].save_state );
    // Window pointer must be unchanged!
    std::swap( panes[left].window, panes[right].window );
    // Recalculation required for weight & volume
    recalc = true;
}

void advanced_inventory::do_return_entry()
{
    // only save pane settings
    save_settings( true );
    g->u.assign_activity( std::make_unique<player_activity>
                          ( std::make_unique<adv_inventory_activity_actor>() ) );
    g->u.activity->auto_resume = true;
    save_state->exit_code = exit_re_entry;
}

bool advanced_inventory::is_processing() const
{
    return save_state->re_enter_move_all != ENTRY_START;
}

void cancel_aim_processing() { uistate.transfer_save.re_enter_move_all = ENTRY_START; }
