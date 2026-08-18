#include "inventory_ui.h"

#include "avatar.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "character.h"
#include "debug.h"
#include "detached_ptr.h"
#include "flag.h"
#include "game.h"
#include "game_inventory.h"
#include "ime.h"
#include "inventory.h"
#include "item.h"
#include "item_category.h"
#include "item_search.h"
#include "item_stack.h"
#include "itype.h"
#include "line.h"
#include "map.h"
#include "map_selector.h"
#include "options.h"
#include "output.h"
#include "player.h"
#include "point.h"
#include "rml_callback.h"
#include "rml_screen.h"
#include "rml_util.h"
#include "sdl_wrappers.h"
#include "string_formatter.h"
#include "string_id.h"
#include "string_input_popup.h"
#include "translations.h"
#include "type_id.h"
#include "ui_manager.h"
#include "units_utility.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vehicle_selector.h"
#include "visitable.h"
#include "vpart_position.h"

#include <RmlUi/Core.h>
#include <algorithm>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

// ── RmlUi inventory path (Tier 3 framework migration, slice 1) ────────────────
namespace
{
struct inv_rml_row_model {
    Rml::String text_rml; // finished markup (invlet + cell text); category = header
    bool is_category = false;
    bool selected = false;
    // Mouse interactivity: every visible column's rows are flattened into one
    // vector (this codebase avoids nested data-for — see inventory.rml) — so
    // col_idx records which visible column a row came from, and col_first
    // flags the first row pulled from each column (for the CSS divider that
    // keeps columns visually distinct despite the flat list).
    int col_idx = 0;
    bool col_first = false;
};

bool g_inv_rml_types_registered = false;

void register_inv_rml_types( Rml::DataModelConstructor& c )
{
    if( g_inv_rml_types_registered ) { return; }
    Rml::StructHandle<inv_rml_row_model> rh = c.RegisterStruct<inv_rml_row_model>();
    rh.RegisterMember( "text_rml", &inv_rml_row_model::text_rml );
    rh.RegisterMember( "is_category", &inv_rml_row_model::is_category );
    rh.RegisterMember( "selected", &inv_rml_row_model::selected );
    rh.RegisterMember( "col_idx", &inv_rml_row_model::col_idx );
    rh.RegisterMember( "col_first", &inv_rml_row_model::col_first );
    c.RegisterArray<Rml::Vector<inv_rml_row_model>>();
    g_inv_rml_types_registered = true;
}
} // namespace

// pImpl (forward-declared in inventory_ui.h) — keeps RmlUi types out of the header.
struct inventory_rml_state {
    rml_doc rml;
    Rml::String title_rml;
    Rml::String hint_rml;
    Rml::String footer_rml;
    Rml::String filter_rml; // "[F] Filter: <text>" indicator (slice 6)
    // Every visible column's rows, flattened into one vector (mouse
    // interactivity: a single flat data-for, no nested data-for/.inv-col).
    Rml::Vector<inv_rml_row_model> rows;
    // Parallel to `rows`: row_refs[i] = {index into inventory_selector's full
    // columns vector, index into that column's entries vector} for rows[i].
    // Not bound to RmlUi — pure C++ bookkeeping for rml_on_select()/rml_on_hover().
    std::vector<std::pair<size_t, size_t>> row_refs;
    // Stats header lines (weight/volume), reusing the row model for its text_rml.
    Rml::Vector<inv_rml_row_model> stats;
    Rml::DataModelHandle handle;
};

bool &inventory_rmlui_enabled()
{
    // Default OFF — opt in via the F4 panel. See rml_screen.h.
    static bool enabled = true;
    return enabled;
}

/** The maximum distance from the screen edge, to snap a window to it */
static const size_t max_win_snap_distance = 4;
/** The minimal gap between two cells */
static const int min_cell_gap = 2;
/** The gap between two cells when screen space is limited*/
static const int normal_cell_gap = 4;
/** The minimal gap between the first cell and denial */
static const int min_denial_gap = 2;
/** The minimal gap between two columns */
static const int min_column_gap = 2;
/** The gap between two columns when there's enough space, but they are not centered */
static const int normal_column_gap = 8;
/**
 * The minimal occupancy ratio to align columns to the center
 * @see inventory_selector::get_columens_occupancy_ratio()
 */
static const double min_ratio_to_center = 0.85;

/** These categories should keep their original order and can't be re-sorted by inventory presets */
static const std::set<std::string> ordered_categories = {{"ITEMS_WORN"}};

constexpr int max_chosen_count = std::numeric_limits<int>::max();

struct navigation_mode_data {
    navigation_mode next_mode;
    translation name;
    nc_color color;
};

struct inventory_input {
    std::string action;
    int ch;
    inventory_entry *entry;
};

bool inventory_entry::operator==( const inventory_entry& other ) const
{
    return get_category_ptr() == other.get_category_ptr() && locations == other.locations;
}

class selection_column_preset: public inventory_selector_preset
{
    public:
        selection_column_preset() = default;

        std::string get_caption( const inventory_entry& entry ) const override {
            std::string res;
            const size_t available_count = entry.get_available_count();
            const item* item = entry.any_item();

            if( entry.chosen_count > 0 && entry.chosen_count < available_count ) {
                //~ %1$d: chosen count, %2$d: available count
                res +=
                    string_format( pgettext( "count", "%1$d of %2$d" ), entry.chosen_count, available_count )
                    + " ";
            } else if( available_count != 1 ) {
                res += string_format( "%d ", available_count );
            }
            if( item->is_money() ) {
                assert( available_count == entry.get_stack_size() );
                if( entry.chosen_count > 0 && entry.chosen_count < available_count ) {
                    res += item->display_money(
                               available_count, entry.get_total_charges(), entry.get_selected_charges() );
                } else {
                    res += item->display_money( available_count, entry.get_total_charges() );
                }
            } else {
                res += item->display_name( available_count );
            }
            return res;
        }

        nc_color get_color( const inventory_entry& entry ) const override {
            if( entry.is_item() ) {
            if( get_player_character().is_wielding( *entry.any_item() ) ) {
                    return c_light_blue;
                } else if( g->u.is_worn( *entry.any_item() ) ) {
                    return c_cyan;
                }
            }
            return inventory_selector_preset::get_color( entry );
        }
};

static const selection_column_preset selection_preset{};

std::string pickup_inventory_preset::get_denial( const item* loc ) const
{
    if( !p.has_item( *loc ) ) {
    if( loc->made_of( LIQUID ) ) {
            return _( "Can't pick up spilt liquids" );
        }
        // else if( !p.can_pick_volume( *loc ) && p.is_armed() ) {
        //     return _( "Too big to pick up" );
        // } else if( !p.can_pick_weight( *loc, !get_option<bool>( "DANGEROUS_PICKUPS" ) ) ) {
        //     return _( "Too heavy to pick up" );
        // }
    }

    return std::string();
}

int inventory_entry::get_total_charges() const
{
    int result = 0;
    for( const item * location : locations ) { result += location->charges; }
    return result;
}

int inventory_entry::get_selected_charges() const
{
    assert( chosen_count <= locations.size() );
    int result = 0;
    for( size_t i = 0; i < chosen_count; ++i ) {
        const item* location = locations[i];
        result += location->charges;
    }
    return result;
}

size_t inventory_entry::get_available_count() const
{
    if( locations.size() == 1 ) {
        return any_item()->count();
    } else {
        return locations.size();
    }
}

int inventory_entry::get_invlet() const
{
    if( custom_invlet != INT_MIN ) { return custom_invlet; }
    if( !is_item() ) { return '\0'; }
    return any_item()->invlet;
}

nc_color inventory_entry::get_invlet_color() const
{
    if( !is_selectable() ) {
    return c_dark_gray;
} else if( g->u.inv_assigned_invlet().contains( get_invlet() ) ) {
        return c_yellow;
    } else {
        return c_white;
    }
}

void inventory_entry::update_cache() { cached_name = any_item()->tname( 1 ); }

const item_category *inventory_entry::get_category_ptr() const
{
    if( custom_category != nullptr ) { return custom_category; }
    if( !is_item() ) { return nullptr; }
    return &any_item()->get_category();
}

bool inventory_column::activatable() const
{
    return std::ranges::any_of( entries, []( const inventory_entry & e ) { return e.is_selectable(); } );
}

inventory_entry *inventory_column::find_by_invlet( int invlet ) const
{
for( const auto& elem : entries ) {
    if( elem.is_item() && elem.get_invlet() == invlet ) {
            return const_cast<inventory_entry *>( &elem );
        }
    }
    return nullptr;
}

size_t inventory_column::get_width() const { return std::max( get_cells_width(), reserved_width ); }

size_t inventory_column::get_height() const { return std::min( entries.size(), height ); }

inventory_selector_preset::inventory_selector_preset()
{
    append_cell( std::function<std::string( const inventory_entry & )>(
    [this]( const inventory_entry & entry ) { return get_caption( entry ); } ) );
}

bool inventory_selector_preset::sort_compare(
    const inventory_entry& lhs, const inventory_entry& rhs ) const
{
    // Place items with an assigned inventory letter first, since the player cared enough to assign
    // them
    const bool left_fav = g->u.inv_assigned_invlet().contains( lhs.any_item()->invlet );
    const bool right_fav = g->u.inv_assigned_invlet().contains( rhs.any_item()->invlet );
    if( left_fav == right_fav ) {
        return lhs.cached_name.compare( rhs.cached_name ) < 0; // Simple alphabetic order
    } else if( left_fav ) {
        return true;
    }
    return false;
}

nc_color inventory_selector_preset::get_color( const inventory_entry& entry ) const
{
    return entry.is_item() ? entry.any_item()->color_in_inventory() : c_magenta;
}

std::function<bool( const inventory_entry & )> inventory_selector_preset::get_filter(
    const std::string& filter ) const
{
    auto item_filter = basic_item_filter( filter );

    return [item_filter]( const inventory_entry & e ) { return item_filter( *e.any_item() ); };
}

std::string inventory_selector_preset::get_caption( const inventory_entry& entry ) const
{
    const size_t count = entry.get_stack_size();
    std::string disp_name;
    if( entry.any_item()->is_money() ) {
        disp_name = entry.any_item()->display_money( count, entry.get_total_charges() );
    } else {
        disp_name = entry.any_item()->display_name( count );
    }

    return ( count > 1 ) ? string_format( "%d %s", count, disp_name ) : disp_name;
}

std::string inventory_selector_preset::get_denial( const inventory_entry& entry ) const
{
    return entry.is_item() ? get_denial( entry.any_item() ) : std::string();
}

std::string inventory_selector_preset::get_cell_text(
    const inventory_entry& entry, size_t cell_index ) const
{
    if( cell_index >= cells.size() ) {
        debugmsg( "Invalid cell index %d.", cell_index );
        return "it's a bug!";
    }
    if( !entry ) {
        return std::string();
    } else if( entry.is_item() ) {
        return cells[cell_index].get_text( entry );
    } else if( cell_index != 0 ) {
        return replace_colors( cells[cell_index].title );
    } else {
        return entry.get_category_ptr()->name();
    }
}

bool inventory_selector_preset::is_stub_cell(
    const inventory_entry& entry, size_t cell_index ) const
{
    if( !entry.is_item() ) { return false; }
const std::string& text = get_cell_text( entry, cell_index );
return text.empty() || text == cells[cell_index].stub;
}

void inventory_selector_preset::append_cell(
    const std::function<std::string( const item* )> &func, const std::string& title,
    const std::string& stub )
{
    // Don't capture by reference here. The func should be able to die earlier than the object
    // itself
    append_cell(
    std::function<std::string( const inventory_entry & )>( [func]( const inventory_entry & entry ) {
        return func( entry.any_item() );
    } ),
    title, stub );
}

void inventory_selector_preset::append_cell(
    const std::function<std::string( const inventory_entry & )> &func, const std::string& title,
    const std::string& stub )
{
    const auto iter = std::find_if( cells.begin(), cells.end(), [&title]( const cell_t &cell ) {
        return cell.title == title;
    } );
    if( iter != cells.end() ) {
        debugmsg( "Tried to append a duplicate cell \"%s\": ignored.", title.c_str() );
        return;
    }
    cells.emplace_back( func, title, stub );
}

std::string inventory_selector_preset::cell_t::get_text( const inventory_entry& entry ) const
{
    return replace_colors( func( entry ) );
}

void inventory_column::select( size_t new_index, scroll_direction dir )
{
    if( new_index < entries.size() ) {
        if( !entries[new_index].is_selectable() ) {
            new_index = next_selectable_index( new_index, dir );
        }

        selected_index = new_index;
        page_offset =
            ( new_index == static_cast<size_t>( -1 ) )
            ? 0
            : selected_index - selected_index % entries_per_page;
    }
}

size_t inventory_column::next_selectable_index( size_t index, scroll_direction dir ) const
{
    if( entries.empty() ) { return index; }
// limit index to the space of the size of entries
index = index % entries.size();
size_t new_index = index;
do {
    // 'new_index' incremented by 'dir' using division remainder (number of entries) to loop
    // over the entries. Negative step '-k' (backwards) is equivalent to '-k + N' (forward),
    // where:
    //     N = entries.size()  - number of elements,
    //     k = |step|          - absolute step (k <= N).
    new_index = ( new_index + static_cast<int>( dir ) + entries.size() ) % entries.size();
    } while( new_index != index && !entries[new_index].is_selectable() );

    if( !entries[new_index].is_selectable() ) { return static_cast<size_t>( -1 ); }

    return new_index;
}

void inventory_column::move_selection( scroll_direction dir )
{
    size_t index = selected_index;

    do {
        index = next_selectable_index( index, dir );
    } while( index != selected_index && is_selected_by_category( entries[index] ) );

    select( index, dir );
}

void inventory_column::move_selection_page( scroll_direction dir )
{
    size_t index = selected_index;

    do {
        const size_t next_index = next_selectable_index( index, dir );
        const bool flipped =
            next_index == selected_index
            || ( next_index > selected_index ) != ( static_cast<int>( dir ) > 0 );

        if( flipped && page_of( next_index ) == page_index() ) {
            break; // If flipped and still on the same page - no need to flip
        }

        index = next_index;
    } while( page_of( next_selectable_index( index, dir ) ) == page_index() );

    select( index, dir );
}

size_t inventory_column::get_entry_cell_width( size_t index, size_t cell_index ) const
{
    size_t res = utf8_width( get_entry_cell_cache( index ).text[cell_index], true );

    if( cell_index == 0 ) { res += get_entry_indent( entries[index] ); }

    return res;
}

size_t inventory_column::get_entry_cell_width(
    const inventory_entry& entry, size_t cell_index ) const
{
    size_t res = utf8_width( preset.get_cell_text( entry, cell_index ), true );

    if( cell_index == 0 ) { res += get_entry_indent( entry ); }

    return res;
}

size_t inventory_column::get_cells_width() const
{
    return std::accumulate(
           cells.begin(), cells.end(), static_cast<size_t>( 0 ),
    []( size_t lhs, const cell_t &cell ) { return lhs + cell.current_width; } );
}

void inventory_column::set_filter( const std::string& filter )
{
    entries_cell_cache.clear();
    paging_is_valid = false;
    prepare_paging( filter );
}

inventory_column::entry_cell_cache_t inventory_column::make_entry_cell_cache(
    const inventory_entry& entry ) const
{
    entry_cell_cache_t result;

    result.assigned = true;
    result.color = preset.get_color( entry );
    result.denial = preset.get_denial( entry );
    result.text.resize( preset.get_cells_count() );

    for( size_t i = 0, n = preset.get_cells_count(); i < n; ++i ) {
        result.text[i] = preset.get_cell_text( entry, i );
    }

    return result;
}

const inventory_column::entry_cell_cache_t &inventory_column::get_entry_cell_cache(
    size_t index ) const
{
    assert( index < entries.size() );

    if( entries_cell_cache.size() < entries.size() ) { entries_cell_cache.resize( entries.size() ); }

    if( !entries_cell_cache[index].assigned ) {
    entries_cell_cache[index] = make_entry_cell_cache( entries[index] );
    }

    return entries_cell_cache[index];
}

void inventory_column::refresh_entry_cell_caches()
{
    entries_cell_cache.clear();
    for( size_t i = 0; i < entries.size(); ++i ) {
        entries_cell_cache.push_back( make_entry_cell_cache( entries[i] ) );
    }
}

void inventory_column::set_width(
    const size_t new_width, const std::vector<inventory_column *> &all_columns )
{
    reset_width( all_columns );
    int width_gap = get_width() - new_width;
    // Now adjust the width if we must
    while( width_gap != 0 ) {
        const int step = width_gap > 0 ? -1 : 1;
        // Should return true when lhs < rhs
        const auto cmp_for_expansion = []( const cell_t &lhs, const cell_t &rhs ) {
            return lhs.visible() && lhs.gap() < rhs.gap();
        };
        // Should return true when lhs < rhs
        const auto cmp_for_shrinking = []( const cell_t &lhs, const cell_t &rhs ) {
            if( !lhs.visible() ) { return false; }
            if( rhs.gap() <= min_cell_gap ) {
                return lhs.current_width < rhs.current_width;
            } else {
                return lhs.gap() < rhs.gap();
            }
        };

        const auto& cell =
            step > 0 ? std::min_element( cells.begin(), cells.end(), cmp_for_expansion )
            : std::max_element( cells.begin(), cells.end(), cmp_for_shrinking );

        if( cell == cells.end() || !cell->visible() ) {
            break; // This is highly unlikely to happen, but just in case
        }

        cell->current_width += step;
        width_gap += step;
    }
    reserved_width = new_width;
}

void inventory_column::set_height( size_t new_height )
{
    if( height != new_height ) {
        if( new_height <= 1 ) {
            debugmsg( "Unable to assign height <= 1 (was %zd).", new_height );
            return;
        }
        height = new_height;
        entries_per_page = new_height;
        paging_is_valid = false;
    }
}

void inventory_column::expand_to_fit( const inventory_entry& entry )
{
    if( !entry ) { return; }

    // Don't use cell cache here since the entry may not yet be placed into the vector of entries.
    const std::string denial = preset.get_denial( entry );

    for( size_t i = 0, num = denial.empty() ? cells.size() : 1; i < num; ++i ) {
        auto& cell = cells[i];

        cell.real_width = std::max( cell.real_width, get_entry_cell_width( entry, i ) );

        // Don't reveal the cell for headers and stubs
        if( cell.visible() || ( entry.is_item() && !preset.is_stub_cell( entry, i ) ) ) {
            const size_t cell_gap = i > 0 ? normal_cell_gap : 0;
            cell.current_width = std::max( cell.current_width, cell_gap + cell.real_width );
        }
    }

    if( !denial.empty() ) {
        reserved_width = std::
                         max( get_entry_cell_width( entry, 0 ) + min_denial_gap + utf8_width( denial, true ),
                              reserved_width );
    }
}

void inventory_column::reset_width( const std::vector<inventory_column*> & )
{
    for( auto& elem : cells ) { elem = cell_t(); }
    reserved_width = 0;
    for( auto& elem : entries ) { expand_to_fit( elem ); }
}

size_t inventory_column::page_of( size_t index ) const
{
    assert( entries_per_page ); // To appease static analysis
    // NOLINTNEXTLINE(clang-analyzer-core.DivideZero)
    return index / entries_per_page;
}

size_t inventory_column::page_of( const inventory_entry& entry ) const
{
    return page_of(
           std::distance( entries.begin(), std::find( entries.begin(), entries.end(), entry ) ) );
}
bool inventory_column::has_available_choices() const
{
    if( !allows_selecting() || !activatable() ) { return false; }
for( size_t i = 0; i < entries.size(); ++i ) {
    if( entries[i].is_item() && get_entry_cell_cache( i ).denial.empty() ) { return true; }
    }
    return false;
}

bool inventory_column::is_selected( const inventory_entry& entry ) const
{
    return entry == get_selected() || ( multiselect && is_selected_by_category( entry ) );
}

bool inventory_column::is_selected_by_category( const inventory_entry& entry ) const
{
    return entry.is_item() && mode == navigation_mode::CATEGORY
           && entry.get_category_ptr() == get_selected().get_category_ptr()
           && page_of( entry ) == page_index();
}

const inventory_entry &inventory_column::get_selected() const
{
    if( selected_index >= entries.size() || !entries[selected_index].is_item() ) {
        // clang complains if we use the default constructor here
        static const inventory_entry dummy( nullptr );
        return dummy;
    }
    return entries[selected_index];
}

std::vector<inventory_entry *> inventory_column::get_all_selected() const
{
    const auto filter_to_selected = [&]( const inventory_entry & entry ) {
        return is_selected( entry );
    };
    return get_entries( filter_to_selected );
}

std::vector<inventory_entry *> inventory_column::get_entries(
    const std::function<bool( const inventory_entry& entry )> &filter_func ) const
{
    std::vector<inventory_entry *> res;

    for( const auto& elem : entries ) {
        if( filter_func( elem ) ) { res.push_back( const_cast<inventory_entry*>( &elem ) ); }
    }

    return res;
}

std::vector<inventory_entry *> inventory_column::get_all_entries(
    const std::function<bool( const inventory_entry& entry )> &filter_func ) const
{
    std::vector<inventory_entry *> res;

    for( const auto& elem : entries ) {
        if( filter_func( elem ) ) { res.push_back( const_cast<inventory_entry*>( &elem ) ); }
    }

    for( const auto& elem : entries_hidden ) {
        if( filter_func( elem ) ) { res.push_back( const_cast<inventory_entry*>( &elem ) ); }
    }

    return res;
}

std::vector<inventory_entry *> inventory_column::get_all_entries() const
{
    auto func = []( const inventory_entry & entry ) { return true; };
    return get_all_entries( func );
}

void inventory_column::set_stack_favorite( const item* location, bool favorite )
{
    const item* selected_item = location;
    std::list<item *> to_favorite;

    if( location->where() == item_location_type::character ) {
        int position = g->u.get_item_position( selected_item );

        if( position < 0 ) {
            g->u.i_at( position ).set_favorite( !selected_item->is_favorite ); // worn/wielded
        } else {
            g->u.inv_set_stack_favorite( position, !selected_item->is_favorite ); // in inventory
        }
    } else if( location->where() == item_location_type::map ) {
        auto items = g->m.i_at( location->position() );

        for( auto& item : items ) {
            if( item->stacks_with( *selected_item ) ) { to_favorite.push_back( item ); }
        }
        for( auto& item : to_favorite ) { item->set_favorite( favorite ); }
    } else if( location->where() == item_location_type::vehicle ) {
        const std::optional<vpart_reference> vp =
            g->m.veh_at( location->position() ).part_with_feature( "CARGO", true );
        assert( vp );

        auto items = vp->vehicle().get_items( vp->part_index() );

        for( auto& item : items ) {
            if( item->stacks_with( *selected_item ) ) { to_favorite.push_back( item ); }
        }
        for( auto * item : to_favorite ) { item->set_favorite( favorite ); }
    }
}

void inventory_column::on_input( const inventory_input& input )
{
    if( empty() || !active ) {
        return; // ignore
    }

    if( input.action == "DOWN" ) {
        move_selection( scroll_direction::FORWARD );
    } else if( input.action == "UP" ) {
        move_selection( scroll_direction::BACKWARD );
    } else if( input.action == "PAGE_DOWN" ) {
        move_selection_page( scroll_direction::FORWARD );
    } else if( input.action == "PAGE_UP" ) {
        move_selection_page( scroll_direction::BACKWARD );
    } else if( input.action == "HOME" ) {
        select( 0, scroll_direction::FORWARD );
    } else if( input.action == "EXAMINE" ) {
        const auto& highlighed = get_selected().any_item();

        std::vector<iteminfo> this_item = highlighed->info();
        item_info_data dummy( highlighed->display_name(), {}, this_item, {} );
        dummy.handle_scrolling = true;
        rml_examine_item( dummy );

        // recalc = true;
        // keepline = true;
    } else if( input.action == "END" ) {
        select( entries.size() - 1, scroll_direction::BACKWARD );
    } else if( input.action == "TOGGLE_FAVORITE" ) {
        auto selected_entries = get_all_selected();
        for( auto& entry_ptr : selected_entries ) {
            const item* loc = entry_ptr->any_item();
            set_stack_favorite( loc, !loc->is_favorite );
        }
        refresh_entry_cell_caches();
    }
}

void inventory_column::add_entry( const inventory_entry& entry )
{
    if( std::find( entries.begin(), entries.end(), entry ) != entries.end() ) {
        debugmsg( "Tried to add a duplicate entry." );
        return;
    }
    const auto iter =
    std::find_if( entries.rbegin(), entries.rend(), [&entry]( const inventory_entry & cur ) {
        const item_category* cur_cat = cur.get_category_ptr();
        const item_category* new_cat = entry.get_category_ptr();

        return cur_cat == new_cat
               || ( cur_cat != nullptr && new_cat != nullptr
                    && ( *cur_cat == *new_cat || *cur_cat < *new_cat ) );
    } );
    entries.insert( iter.base(), entry );
    entries_cell_cache.clear();
    expand_to_fit( entry );
    paging_is_valid = false;
}

void inventory_column::move_entries_to( inventory_column& dest )
{
    for( const auto& elem : entries ) {
        if( elem.is_item() &&
            // this column already has this entry, no need to try to add it again
            std::find( dest.entries.begin(), dest.entries.end(), elem ) == dest.entries.end() ) {
            dest.add_entry( elem );
        }
    }
    dest.prepare_paging();
    clear();
}

void inventory_column::prepare_paging( const std::string& filter )
{
    if( paging_is_valid ) { return; }

    entries_cell_cache.clear();

    const auto filter_fn = filter_from_string <
    inventory_entry > ( filter, [this]( const std::string & filter ) {
        return preset.get_filter( filter );
    } );

    // First, remove all non-items
    for( size_t i = 0; i < entries_hidden.size(); ++i ) { entries.push_back( entries_hidden[i] ); }

    entries_hidden.clear();
    for( size_t i = 0; i < entries.size(); ++i ) {
        if( entries[i].is_item() && !filter_fn( entries[i] ) ) {
            entries_hidden.push_back( entries[i] );
        }
    }

    const auto new_end =
    std::remove_if( entries.begin(), entries.end(), [&filter_fn]( const inventory_entry & entry ) {
        return !entry.is_item() || !filter_fn( entry );
    } );
    entries.erase( new_end, entries.end() );
    // Then sort them with respect to categories
    auto sort_function = [this]( const inventory_entry & lhs, const inventory_entry & rhs ) {
        if( *lhs.get_category_ptr() != *rhs.get_category_ptr() ) {
            return *lhs.get_category_ptr() < *rhs.get_category_ptr();
        } else {
            return preset.sort_compare( lhs, rhs );
        }
    };
    std::sort( entries.begin(), entries.end(), sort_function );

    // Recover categories
    const item_category* current_category = nullptr;
    for( auto iter = entries.begin(); iter != entries.end(); ++iter ) {
        if( iter->get_category_ptr() == current_category ) { continue; }
        current_category = iter->get_category_ptr();
        iter = entries.insert( iter, inventory_entry( current_category ) );
        expand_to_fit( *iter );
    }
    // Determine the new height.
    entries_per_page = height;
    if( entries.size() > entries_per_page ) {
        entries_per_page -= 1; // Make room for the page number.
        for( size_t i = entries_per_page - 1; i < entries.size(); i += entries_per_page ) {
            auto iter = std::next( entries.begin(), i );
            if( iter->is_category() ) {
                // The last item on the page must not be a category.
                entries.insert( iter, inventory_entry() );
            } else {
                // The first item on the next page must be a category.
                iter = std::next( iter );
                if( iter != entries.end() && iter->is_item() ) {
                    entries.insert( iter, inventory_entry( iter->get_category_ptr() ) );
                }
            }
        }
    }
    entries_cell_cache.clear();
    paging_is_valid = true;
    // Select the uppermost possible entry
    select( selected_index, selected_index ? scroll_direction::BACKWARD : scroll_direction::FORWARD );
}

void inventory_column::clear()
{
    entries.clear();
    entries_cell_cache.clear();
    paging_is_valid = false;
}

bool inventory_column::select( const item* loc )
{
    for( size_t index = 0; index < entries.size(); ++index ) {
        if( entries[index].is_selectable() && entries[index].any_item() == loc ) {
            select( index, scroll_direction::FORWARD );
            return true;
        }
    }
    return false;
}

size_t inventory_column::get_entry_indent( const inventory_entry& entry ) const
{
    if( !entry.is_item() ) { return 0; }

size_t res = 2;
if( get_option<bool>( "ITEM_SYMBOLS" ) ) { res += 2; }
if( allows_selecting() && activatable() && multiselect ) { res += 2; }
return res;
}

int inventory_column::reassign_custom_invlets( const player& p, int min_invlet, int max_invlet )
{
    int cur_invlet = min_invlet;
    for( auto& elem : entries ) {
        // Only items on map/in vehicles: those that the player does not possess.
        if( elem.is_selectable() && !p.has_item( *elem.any_item() ) ) {
            elem.custom_invlet = cur_invlet <= max_invlet ? cur_invlet++ : '\0';
        }
    }
    return cur_invlet;
}

size_t inventory_column::visible_cells() const
{
    return std::count_if( cells.begin(), cells.end(), []( const cell_t &elem ) {
        return elem.visible();
    } );
}

selection_column::selection_column( const std::string& id, const std::string& name )
    : inventory_column( selection_preset ),
      selected_cat( id, no_translation( name ), 0 ) {}

selection_column::~selection_column() = default;

std::vector<inv_rml_row> inventory_column::rml_rows() const
{
    // Mirrors draw() but emits data rows; reuses the per-entry cell cache. All
    // entries (RmlUi scrolls — no page windowing). Colours baked via the cache;
    // the selected row + category styling are handled by CSS classes.
    std::vector<inv_rml_row> out;
    if( !visible() ) { return out; }
    for( size_t i = 0; i < entries.size(); ++i ) {
        const inventory_entry& entry = entries[i];
        const entry_cell_cache_t &cache = get_entry_cell_cache( i );
        if( entry.is_category() ) {
            inv_rml_row r;
            r.is_category = true;
            r.entry_index = i;
            r.text = cata_text_to_rml(
                         colorize( cache.text.empty() ? std::string() : cache.text[0], cache.color ) );
            out.push_back( r );
            continue;
        }
        if( !entry.is_item() ) {
            continue; // blank spacer row
        }
        inv_rml_row r;
        r.entry_index = i;
        r.selected = active && is_selected( entry );
        const bool denied = !cache.denial.empty();
        std::string markup;
        if( entry.get_invlet() != '\0' ) {
            markup += colorize(
                          std::string( 1, static_cast<char>( entry.get_invlet() ) ), entry.get_invlet_color() );
        }
        markup += " ";
        // Item symbol glyph (identifying content — matches draw(): item color).
        if( entry.any_item() ) {
            markup += colorize( entry.any_item()->symbol(), entry.any_item()->color() ) + " ";
        }
        // Multiselect mark (matches draw(): '-' none / '+' all / '#' partial), only
        // for multiselect columns. The selection column has multiselect=false so it
        // shows no mark — same as curses.
        if( allows_selecting() && activatable() && multiselect ) {
            if( entry.chosen_count == 0 ) {
                markup += colorize( "-", c_dark_gray );
            } else if( entry.chosen_count >= entry.get_available_count() ) {
                markup += colorize( "+", c_light_green );
            } else {
                markup += colorize( "#", c_light_green );
            }
            markup += " ";
        }
        // When denied, curses shows only the first cell to make room for the red
        // denial reason (count = 1); match that so the layout can't collide.
        std::string body;
        const size_t cell_count = denied ? 1 : cache.text.size();
        for( size_t ci = 0; ci < cell_count && ci < cache.text.size(); ++ci ) {
            if( cache.text[ci].empty() ) { continue; }
            if( !body.empty() ) { body += "  "; }
            body += cache.text[ci];
        }
        markup += colorize( body, denied ? c_dark_gray : cache.color );
        if( denied ) { markup += "  " + colorize( cache.denial, c_red ); }
        r.text = cata_text_to_rml( markup );
        out.push_back( r );
    }
    return out;
}

void selection_column::reset_width( const std::vector<inventory_column*> &all_columns )
{
    inventory_column::reset_width( all_columns );

    const auto always_yes = []( const inventory_entry & ) { return true; };

    for( const inventory_column * const col : all_columns ) {
        if( col && !dynamic_cast<const selection_column * >( col ) ) {
            for( const inventory_entry * const ent : col->get_entries( always_yes ) ) {
                if( ent ) { expand_to_fit( *ent ); }
            }
        }
    }
}

void selection_column::prepare_paging( const std::string& filter )
{
    inventory_column::prepare_paging( filter );

    if( entries.empty() ) { // Category must always persist
        entries.emplace_back( &*selected_cat );
        expand_to_fit( entries.back() );
    }

    if( !last_changed.is_null() ) {
        const auto iter = std::find( entries.begin(), entries.end(), last_changed );
        if( iter != entries.end() ) {
            select( std::distance( entries.begin(), iter ), scroll_direction::FORWARD );
        }
        last_changed = inventory_entry();
    }
}

void selection_column::on_change( const inventory_entry& entry )
{
    inventory_entry my_entry( entry, &*selected_cat );

    auto iter = std::find( entries.begin(), entries.end(), my_entry );

    if( iter == entries.end() ) {
        if( my_entry.chosen_count == 0 ) {
            return; // Not interested.
        }
        add_entry( my_entry );
        last_changed = my_entry;
    } else if( iter->chosen_count != my_entry.chosen_count ) {
        if( my_entry.chosen_count > 0 ) {
            iter->chosen_count = my_entry.chosen_count;
            expand_to_fit( my_entry );
        } else {
            iter = entries.erase( iter );
        }
        paging_is_valid = false;

        if( iter != entries.end() ) { last_changed = *iter; }
    }
}

// TODO: Move it into some 'item_stack' class.
static std::vector<std::list<item *>> restack_items(
    const item_stack::const_iterator& from, const item_stack::const_iterator& to,
    bool check_components = false )
{
    std::vector<std::list<item *>> res;

    for( auto it = from; it != to; ++it ) {
        auto match = std::
        find_if( res.begin(), res.end(), [&it, check_components]( const std::list<item*> &e ) {
            return ( *it )->display_stacked_with( *const_cast<item*>( e.back() ), check_components );
        } );

        if( match != res.end() ) {
            match->push_back( const_cast<item*>( *it ) );
        } else {
            res.emplace_back( 1, const_cast<item*>( *it ) );
        }
    }

    return res;
}

static std::vector<std::list<item *>> restack_items(
    const std::vector<item *>::const_iterator& from, const std::vector<item *>::const_iterator& to,
    bool check_components = false )
{
    std::vector<std::list<item *>> res;

    for( auto it = from; it != to; ++it ) {
        auto match = std::
        find_if( res.begin(), res.end(), [&it, check_components]( const std::list<item*> &e ) {
            return ( *it )->display_stacked_with( *const_cast<item*>( e.back() ), check_components );
        } );

        if( match != res.end() ) {
            match->push_back( const_cast<item*>( *it ) );
        } else {
            res.emplace_back( 1, const_cast<item*>( *it ) );
        }
    }

    return res;
}

const item_category *inventory_selector::naturalize_category(
    const item_category& category, const tripoint_bub_ms& pos )
{
    const auto find_cat_by_id = [this]( const item_category_id & id ) {
        const auto iter =
        std::find_if( categories.begin(), categories.end(), [&id]( const item_category & cat ) {
            return cat.get_id() == id;
        } );
        return iter != categories.end() ? &*iter : nullptr;
    };

    const int dist = rl_dist( u.bub_pos(), pos );

    if( dist != 0 ) {
        const std::string suffix = direction_suffix( u.bub_pos().raw(), pos.raw() );
        const item_category_id id = item_category_id(
                                        string_format( "%s_%s", category.get_id().c_str(), suffix.c_str() ) );

        const auto existing = find_cat_by_id( id );
        if( existing != nullptr ) { return existing; }

        const std::string name = string_format( "%s %s", category.name(), suffix.c_str() );
        const int sort_rank = category.sort_rank() + dist;
        const item_category new_category( id, no_translation( name ), sort_rank );

        categories.push_back( new_category );
    } else {
        const item_category* const existing = find_cat_by_id( category.get_id() );
        if( existing != nullptr ) { return existing; }

        categories.push_back( category );
    }

    return &categories.back();
}

void inventory_selector::add_entry(
    inventory_column& target_column, std::vector<item *>&& locations,
    const item_category* custom_category )
{
    if( !preset.is_shown( locations.front() ) ) { return; }

    is_empty = false;
    inventory_entry entry( locations, custom_category, preset.get_denial( locations.front() ).empty() );

    target_column.add_entry( entry );

    shared_ptr_fast<ui_adaptor> current_ui = ui.lock();
    if( current_ui ) { current_ui->mark_resize(); }
}

void inventory_selector::add_item(
    inventory_column& target_column, item* location, const item_category* custom_category )
{
    add_entry( target_column, std::vector<item*>( 1, location ), custom_category );
}

void inventory_selector::add_items(
    inventory_column& target_column, const std::function<item*( item* )> &locator,
    const std::vector<std::list<item *>> &stacks, const item_category* custom_category )
{
    const item_category* nat_category = nullptr;

    for( const auto& elem : stacks ) {
        std::vector<item *> locations;
        std::transform( elem.begin(), elem.end(), std::back_inserter( locations ), locator );
        item* const& loc = locations.front();

        if( custom_category == nullptr ) {
            nat_category = &loc->get_category();
        } else if( nat_category == nullptr && preset.is_shown( loc ) ) {
            nat_category = naturalize_category( *custom_category, loc->position() );
        }

        add_entry( target_column, std::move( locations ), nat_category );
    }
}

void inventory_selector::remove_item( item* location )
{
    for( auto col_ptr : columns ) {
        std::vector<inventory_entry *> entries = col_ptr->get_all_entries();
        for( auto entry_ptr : entries ) {
            if( entry_ptr->is_item() ) {
                auto iter =
                    std::remove( entry_ptr->locations.begin(), entry_ptr->locations.end(), location );
                entry_ptr->locations.erase( iter, entry_ptr->locations.end() );
            }
        }

        auto remove_func = []( const inventory_entry & e ) { return e.locations.empty(); };

        auto iter = std::remove_if( col_ptr->entries.begin(), col_ptr->entries.end(), remove_func );
        col_ptr->entries.erase( iter, col_ptr->entries.end() );

        auto iter_hidden = std::
                           remove_if( col_ptr->entries_hidden.begin(), col_ptr->entries_hidden.end(), remove_func );
        col_ptr->entries_hidden.erase( iter_hidden, col_ptr->entries_hidden.end() );

        col_ptr->paging_is_valid = false;
        col_ptr->prepare_paging();
    }
}

void inventory_selector::add_character_items( Character& character )
{
    character.visit_items( [this, &character]( item * it ) {
        if( character.is_wielding( *it ) ) {
            add_item( own_gear_column, it, &item_category_id( "WEAPON_HELD" ).obj() );
        } else if( character.is_worn( *it ) ) {
            add_item( own_gear_column, it, &item_category_id( "ITEMS_WORN" ).obj() );
        }
        return VisitResponse::NEXT;
    } );
    // Visitable interface does not support stacks so it has to be here
    for( const auto& elem : character.inv_const_slice() ) {
        add_items(
        own_inv_column, []( item * it ) { return it; },
        restack_items( ( *elem ).begin(), ( *elem ).end(), preset.get_checking_components() ) );
    }
}

void inventory_selector::add_map_items( const tripoint_bub_ms& target )
{
    if( g->m.accessible_items( target ) ) {
        const auto items = g->m.i_at( target );
        const std::string name = to_upper_case( g->m.name( target ) );
        const item_category map_cat( name, no_translation( name ), 100 );

        add_items(
        map_column, []( item * it ) { return it; },
        restack_items( items.begin(), items.end(), preset.get_checking_components() ), &map_cat );
    }
}

void inventory_selector::add_vehicle_items( const tripoint_bub_ms& target )
{
    const std::optional<vpart_reference> vp = g->m.veh_at( target ).part_with_feature( "CARGO", true );
    if( !vp ) { return; }
    vehicle* const veh = &vp->vehicle();
    const int part = vp->part_index();
    vehicle_stack items = veh->get_items( part );
    const std::string name = to_upper_case( remove_color_tags( veh->part( part ).name() ) );
    const item_category vehicle_cat( name, no_translation( name ), 200 );

    const auto check_components = this->preset.get_checking_components();

    add_items(
    map_column, []( item * it ) { return it; },
    restack_items( items.begin(), items.end(), check_components ), &vehicle_cat );
}

void inventory_selector::add_nearby_items( int radius )
{
    if( radius >= 0 ) {
        for( const tripoint_bub_ms& pos : closest_points_first( u.bub_pos(), radius ) ) {
            // can not reach this -> can not access its contents
            if( u.bub_pos() != pos
                && !g->m.clear_path( u.bub_pos(), pos, rl_dist( u.bub_pos(), pos ), 1, 100 ) ) {
                continue;
            }
            add_map_items( pos );
            add_vehicle_items( pos );
        }
    }
}

void inventory_selector::add_bionics_items( Character& character )
{
    for( bionic bio : character.get_bionic_collection() ) {
        const itype_id fake = bio.info().fake_item;
        if( bio.info().has_flag( flag_BIONIC_TOOLS ) && !fake.is_null() && fake.str() != "" ) {
            item* fakeitem = g->add_fake_item( item::spawn( fake ) );
            add_entry( own_gear_column, std::vector<item*>( 1, fakeitem ),
                       &item_category_id( "BIONICS" ).obj() );
        }
    }
}

void inventory_selector::clear_items()
{
    is_empty = true;
    for( auto& column : columns ) { column->clear(); }
    own_inv_column.clear();
    own_gear_column.clear();
    map_column.clear();
}

bool inventory_selector::select( const item* loc )
{
    bool res = false;

    for( size_t i = 0; i < columns.size(); ++i ) {
        auto elem = columns[i];
        if( elem->visible() && elem->select( loc ) ) {
            if( !res && elem->activatable() ) {
                set_active_column( i );
                res = true;
            }
        }
    }

    return res;
}

inventory_entry *inventory_selector::find_entry_by_invlet( int invlet ) const
{
for( const auto elem : columns ) {
    const auto res = elem->find_by_invlet( invlet );
        if( res != nullptr ) { return res; }
    }
    return nullptr;
}

// FIXME: if columns are merged due to low screen width, they will not be splitted
// once screen width becomes enough for the columns.
void inventory_selector::rearrange_columns( size_t client_width )
{
    while( is_overflown( client_width ) ) {
        if( !own_gear_column.empty() ) {
            own_gear_column.move_entries_to( own_inv_column );
        } else if( !map_column.empty() ) {
            map_column.move_entries_to( own_inv_column );
        } else {
            break; // There's nothing we can do about it.
        }
    }
}

void inventory_selector::prepare_layout( size_t client_width, size_t client_height )
{
    // This block adds categories and should go before any width evaluations
    for( auto& elem : columns ) {
        elem->set_height( client_height );
        elem->reset_width( columns );
        elem->prepare_paging( filter );
    }
    // Handle screen overflow
    rearrange_columns( client_width );
    // If we have a single column and it occupies more than a half of
    // the available with -> expand it
    auto visible_columns = get_visible_columns();
    if( visible_columns.size() == 1 && are_columns_centered( client_width ) ) {
        visible_columns.front()->set_width( client_width, columns );
    }

    int custom_invlet = '0';
    for( auto& elem : columns ) {
        elem->prepare_paging();
        custom_invlet = elem->reassign_custom_invlets( u, custom_invlet, '9' );
    }

    refresh_active_column();
}

void inventory_selector::prepare_layout()
{
    const auto snap = []( size_t cur_dim, size_t max_dim ) {
        return cur_dim + 2 * max_win_snap_distance >= max_dim ? max_dim : cur_dim;
    };

    const int nc_width = 2 * ( 1 + border );
    const int nc_height = get_header_height() + 1 + 2 * border;

    prepare_layout( TERMX - nc_width, TERMY - nc_height );

    const int win_width = snap( get_layout_width() + nc_width, TERMX );
    const int win_height =
        snap( std::max<int>( get_layout_height() + nc_height, FULL_SCREEN_HEIGHT ), TERMY );

    prepare_layout( win_width - nc_width, win_height - nc_height );

    resize_window( win_width, win_height );
}

shared_ptr_fast<ui_adaptor> inventory_selector::create_or_get_ui_adaptor()
{
    shared_ptr_fast<ui_adaptor> current_ui = ui.lock();
    if( !current_ui ) {
        ui = current_ui = make_shared_fast<ui_adaptor>();
        current_ui->on_screen_resize( [this]( ui_adaptor & ) { prepare_layout(); } );
        current_ui->mark_resize();

        current_ui->on_redraw( [this]( const ui_adaptor & ) { refresh_window(); } );

        // Open the RmlUi doc once, for a subclass that opted in (toggle read here).
        if( uses_rml() ) { rml_open(); }
    }
    return current_ui;
}

size_t inventory_selector::get_layout_width() const
{
    const size_t min_hud_width = std::max( get_header_min_width(), get_footer_min_width() );
    const auto visible_columns = get_visible_columns();
    const size_t gaps =
        visible_columns.size() > 1 ? normal_column_gap * ( visible_columns.size() - 1 ) : 0;

    return std::max( get_columns_width( visible_columns ) + gaps, min_hud_width );
}

size_t inventory_selector::get_layout_height() const
{
    const auto visible_columns = get_visible_columns();
    // Find and return the highest column's height.
    const auto iter = std::max_element(
                          visible_columns.begin(), visible_columns.end(),
    []( const inventory_column * lhs, const inventory_column * rhs ) {
        return lhs->get_height() < rhs->get_height();
    } );

    return iter != visible_columns.end() ? ( *iter )->get_height() : 1;
}

size_t inventory_selector::get_header_height() const
{
    return display_stats || !hint.empty() ? 3 : 1;
}

size_t inventory_selector::get_header_min_width() const
{
    const size_t titles_width = std::max( utf8_width( title, true ), utf8_width( hint, true ) );
    if( !display_stats ) { return titles_width; }

    size_t stats_width = 0;
    for( const std::string& elem : get_stats() ) {
        stats_width = std::max( static_cast<size_t>( utf8_width( elem, true ) ), stats_width );
    }

    return titles_width + stats_width + ( stats_width != 0 ? 3 : 0 );
}

size_t inventory_selector::get_footer_min_width() const
{
    size_t result = 0;
    navigation_mode m = mode;

    do {
        result =
            std::max( static_cast<size_t>( utf8_width( get_footer( m ).first, true ) ) + 2 + 4, result );
        m = get_navigation_data( m ).next_mode;
    } while( m != mode );

    return result;
}

inventory_selector::stat display_stat(
    const std::string& caption, int cur_value, int max_value,
    const std::function<std::string( int )> &disp_func )
{
    const nc_color color = cur_value > max_value ? c_red : c_light_gray;
    return {{
            caption, colorize( disp_func( cur_value ), color ), "/",
            colorize( disp_func( max_value ), c_light_gray )
        }};
}

inventory_selector::stats inventory_selector::get_weight_and_volume_stats(
    units::mass weight_carried, units::mass weight_capacity, const units::volume& volume_carried,
    const units::volume& volume_capacity )
{
    return {
        {
            display_stat(
                string_format( _( "Weight (%s):" ), weight_units() ), to_gram( weight_carried ),
                to_gram( weight_capacity ),
                []( int w )
            {
                return string_format( "%.1f", round_up( convert_weight( units::from_gram( w ) ), 1 ) );
            } ),
            display_stat( string_format( _( "Volume (%s):" ), volume_units_abbr() ),
                          units::to_milliliter( volume_carried ), units::to_milliliter( volume_capacity ),
            []( int v ) { return format_volume( units::from_milliliter( v ) ); } )
        }};
}

inventory_selector::stats inventory_selector::get_raw_stats() const
{
    return get_weight_and_volume_stats(
           u.weight_carried(), u.weight_capacity(), u.volume_carried(), u.volume_capacity() );
}

std::vector<std::string> inventory_selector::get_stats() const
{
    // Stats consist of arrays of cells.
    const size_t num_stats = 2;
    const std::array<stat, num_stats> stats = get_raw_stats();
    // Streams for every stat.
    std::array<std::string, num_stats> lines;
    std::array<size_t, num_stats> widths;
    // Add first cells and spaces after them.
    for( size_t i = 0; i < stats.size(); ++i ) {
        lines[i] += string_format( "%s", stats[i][0] ) + " ";
    }
    // Now add the rest of the cells and align them to the right.
    for( size_t j = 1; j < stats.front().size(); ++j ) {
        // Calculate actual cell width for each stat.
        std::transform( stats.begin(), stats.end(), widths.begin(), [j]( const stat & elem ) {
            return utf8_width( elem[j], true );
        } );
        // Determine the max width.
        const size_t max_w = *std::max_element( widths.begin(), widths.end() );
        // Align all stats in this cell with spaces.
        for( size_t i = 0; i < stats.size(); ++i ) {
            if( max_w > widths[i] ) { lines[i] += std::string( max_w - widths[i], ' ' ); }
            lines[i] += string_format( "%s", stats[i][j] );
        }
    }
    // Construct the final result.
    return std::vector<std::string>( lines.begin(), lines.end() );
}

void inventory_selector::resize_window( int width, int height )
{
    w_inv = catacurses::newwin( height, width, point( ( TERMX - width ) / 2, ( TERMY - height ) / 2 ) );
    if( spopup ) { spopup->window( w_inv, point( 4, getmaxy( w_inv ) - 1 ), ( getmaxx( w_inv ) / 2 ) - 4 ); }
    shared_ptr_fast<ui_adaptor> current_ui = ui.lock();
    if( current_ui ) { current_ui->position_from_window( w_inv ); }
}

void inventory_selector::refresh_window() const
{
    // RmlUi path (only for a selector subclass that opted in via uses_rml() AND
    // whose doc was opened in create_or_get_ui_adaptor) — sync the model, skip
    // curses. Other subclasses / toggle-off fall through to the curses render.
    if( uses_rml() && rml_state_ ) {
    rml_sync();
        return;
    }
}

void inventory_selector::rml_open()
{
    if( rml_state_ ) { return; }
    rml_state_ = std::make_unique<inventory_rml_state>();
    inventory_rml_state* st = rml_state_.get();
    st->rml.open( inventory_rmlui_enabled(), "inventory", ctxt,
    [this, st]( Rml::DataModelConstructor & c ) {
        register_inv_rml_types( c );
        c.Bind( "title_rml", &st->title_rml );
        c.Bind( "hint_rml", &st->hint_rml );
        c.Bind( "footer_rml", &st->footer_rml );
        c.Bind( "filter_rml", &st->filter_rml );
        c.Bind( "rows", &st->rows );
        c.Bind( "stats", &st->stats );
        c.BindEventCallback( "on_select", rml_idx_callback( [this]( int idx ) { rml_on_select( idx ); } ) );
        c.BindEventCallback( "on_hover", rml_idx_callback( [this]( int idx ) { rml_on_hover( idx ); } ) );
        st->handle = c.GetModelHandle();
    } );
}

void inventory_selector::rml_sync() const
{
    inventory_rml_state* st = rml_state_.get();
    if( !st ) { return; }
    st->title_rml = cata_text_to_rml( colorize( title, c_white ) );
    st->hint_rml = cata_text_to_rml( colorize( hint, c_dark_gray ) );
    const std::pair<std::string, nc_color> f = get_footer( mode );
    st->footer_rml = cata_text_to_rml( colorize( f.first, f.second ) );
    // Filter indicator (mirrors draw_footer): the filter key + current filter text.
    // ASCII bracket/line decoration is dropped (semantic rewrite, like prior slices).
    if( has_available_choices() || !filter.empty() ) {
        const std::string label = string_format(
                                      filter.empty() ? _( "[%s] Filter" ) : _( "[%s] Filter: " ), ctxt.get_desc( "INVENTORY_"
                                          "FILTER" ) );
        st->filter_rml = cata_text_to_rml(
                             colorize( label, c_light_gray ) + colorize( filter, c_white ) );
    } else {
        st->filter_rml = Rml::String();
    }
    // Flatten every visible column's rows into one vector (mouse interactivity:
    // this codebase avoids nested data-for — see data/gui/inventory.rml — so a
    // single flat data-for iterates `rows`). row_refs maps a flat row back to
    // its owning column (full index, for set_active_column()/get_column()) and
    // its entry index within that column, for rml_on_select()/rml_on_hover().
    // The category/selected styling rides CSS classes; only the active column
    // reports selected=true (cursor highlight).
    st->rows.clear();
    st->row_refs.clear();
    const std::vector<inventory_column *> visible_columns = get_visible_columns();
    const std::vector<inventory_column *> &all_columns = get_all_columns();
    for( size_t col_i = 0; col_i < visible_columns.size(); ++col_i ) {
        const inventory_column *col = visible_columns[col_i];
        const size_t full_col_idx = static_cast<size_t>(
                                        std::ranges::find( all_columns, col ) - all_columns.begin() );
        bool first_row = true;
        for( const inv_rml_row& r : col->rml_rows() ) {
            inv_rml_row_model m;
            m.text_rml = r.text;
            m.is_category = r.is_category;
            m.selected = r.selected;
            m.col_idx = static_cast<int>( col_i );
            m.col_first = first_row && col_i > 0;
            first_row = false;
            st->rows.push_back( std::move( m ) );
            st->row_refs.emplace_back( full_col_idx, r.entry_index );
        }
    }
    // Stats header (weight/volume), right-aligned — mirrors draw_header's
    // display_stats branch. Each get_stats() line already carries per-segment
    // colour tags; wrap in the c_dark_gray base like the curses right_print.
    st->stats.clear();
    if( display_stats ) {
        for( const std::string& elem : get_stats() ) {
            inv_rml_row_model m;
            m.text_rml = cata_text_to_rml( colorize( elem, c_dark_gray ) );
            st->stats.push_back( m );
        }
    }
    st->handle.DirtyVariable( "title_rml" );
    st->handle.DirtyVariable( "hint_rml" );
    st->handle.DirtyVariable( "footer_rml" );
    st->handle.DirtyVariable( "filter_rml" );
    st->handle.DirtyVariable( "rows" );
    st->handle.DirtyVariable( "stats" );
}

void inventory_selector::rml_on_select( int flat_idx )
{
    inventory_rml_state* st = rml_state_.get();
    if( !st || flat_idx < 0 || static_cast<size_t>( flat_idx ) >= st->row_refs.size() ) {
        return;
    }
    const std::vector<inventory_column *> &all_columns = get_all_columns();
    // Resolve a flat row index to its entry, or nullptr if stale/out of range
    // (columns can be rebuilt by filtering between rml_sync() calls).
    const auto resolve = [&]( size_t idx ) -> inventory_entry* {
        if( idx >= st->row_refs.size() ) { return nullptr; }
        const auto &ref = st->row_refs[idx];
        if( ref.first >= all_columns.size() ) { return nullptr; }
        inventory_column &col = *all_columns[ref.first];
        if( ref.second >= col.entries.size() ) { return nullptr; }
        return &col.entries[ref.second];
    };

    inventory_entry *clicked = resolve( static_cast<size_t>( flat_idx ) );
    if( clicked == nullptr || clicked->is_category() || !clicked->is_selectable() ) {
        return;
    }

    const auto [col_idx, entry_idx] = st->row_refs[static_cast<size_t>( flat_idx )];
    set_active_column( col_idx );
    all_columns[col_idx]->select( entry_idx, scroll_direction::FORWARD );

    if( dynamic_cast<inventory_multiselector *>( this ) != nullptr ) {
        const bool shift_held = ( SDL_GetModState() & SDL_KMOD_SHIFT ) != 0;
        if( shift_held && last_clicked_row_idx_ >= 0 ) {
            const int lo = std::min( last_clicked_row_idx_, flat_idx );
            const int hi = std::max( last_clicked_row_idx_, flat_idx );
            for( int i = lo; i <= hi; ++i ) {
                inventory_entry *e = resolve( static_cast<size_t>( i ) );
                if( e != nullptr && !e->is_category() && e->is_selectable() ) {
                    rml_toggle_mark( *e );
                }
            }
        } else {
            rml_toggle_mark( *clicked );
        }
        last_clicked_row_idx_ = flat_idx;
    } else {
        rml_confirm_pending_ = true;
    }
}

void inventory_selector::rml_on_hover( int flat_idx )
{
    inventory_rml_state* st = rml_state_.get();
    if( !st || flat_idx < 0 || static_cast<size_t>( flat_idx ) >= st->row_refs.size() ) {
        return;
    }
    const std::vector<inventory_column *> &all_columns = get_all_columns();
    const auto [col_idx, entry_idx] = st->row_refs[static_cast<size_t>( flat_idx )];
    if( col_idx >= all_columns.size() ) { return; }
    inventory_column &col = *all_columns[col_idx];
    if( entry_idx >= col.entries.size() || col.entries[entry_idx].is_category() ) {
        return;
    }
    set_active_column( col_idx );
    col.select( entry_idx, scroll_direction::FORWARD );
}

void inventory_selector::set_filter()
{
    spopup = std::make_unique<string_input_popup>();
    spopup->max_length( 256 ).identifier( "inventory" ).text( filter );

    shared_ptr_fast<ui_adaptor> current_ui = ui.lock();
    if( current_ui ) { current_ui->mark_resize(); }

    ime_sentry sentry;

    do {
        ui_manager::redraw();
        spopup->query_string( /*loop=*/false );
    } while( !spopup->confirmed() && !spopup->canceled() );

    if( spopup->confirmed() ) {
        filter = spopup->text();
        for( const auto elem : columns ) { elem->set_filter( filter ); }
        if( current_ui ) { current_ui->mark_resize(); }
    }

    spopup.reset();
}

void inventory_selector::set_filter( const std::string& str )
{
    prepare_layout();
    filter = str;
    for( const auto elem : columns ) { elem->set_filter( filter ); }
    shared_ptr_fast<ui_adaptor> current_ui = ui.lock();
    if( current_ui ) { current_ui->mark_resize(); }
}

std::string inventory_selector::get_filter() const { return filter; }

bool inventory_selector::wield( inventory_entry& entry )
{
    if( !entry.is_item() ) { return false; }

    item* item = entry.any_item();
    bool wield_result = u.can_wield( *item ).success();
    if( wield_result ) {
        remove_item( item );
        u.wield( *item );
    } else {
        popup_getkey( u.can_wield( *item ).c_str() );
    }

    return wield_result;
}

bool inventory_selector::wear( inventory_entry& entry )
{
    if( !entry.is_item() ) { return false; }

    item* item = entry.any_item();
    bool wear_result = u.can_wear( *item ).success();
    if( wear_result ) {
        remove_item( item );
        auto to_move = item->detach();
        u.wear_item( std::move( to_move ) );
    } else {
        popup_getkey( u.can_wear( *item ).c_str() );
    }

    return wear_result;
}

std::pair<std::string, nc_color> inventory_selector::get_footer( navigation_mode m ) const
{
    if( has_available_choices() ) {
    return std::
           make_pair( get_navigation_data( m ).name.translated(), get_navigation_data( m ).color );
    }
    return std::make_pair( _( "There are no available choices" ), i_red );
}

inventory_selector::inventory_selector( player& u, const inventory_selector_preset& preset )
    : u( u ),
      preset( preset ),
      ctxt( "INVENTORY" ),
      own_gear_column( preset ),
      active_column_index( 0 ),
      mode( navigation_mode::ITEM ),
      own_inv_column( preset ),
      map_column( preset )
{
    ctxt.register_action( "DOWN", to_translation( "Next item" ) );
    ctxt.register_action( "UP", to_translation( "Previous item" ) );
    ctxt.register_action( "PAGE_DOWN", to_translation( "Page down" ) );
    ctxt.register_action( "PAGE_UP", to_translation( "Page up" ) );
    ctxt.register_action( "RIGHT", to_translation( "Next column" ) );
    ctxt.register_action( "LEFT", to_translation( "Previous column" ) );
    ctxt.register_action( "CONFIRM", to_translation( "Confirm your selection" ) );
    ctxt.register_action( "QUIT", to_translation( "Cancel" ) );
    ctxt.register_action( "CATEGORY_SELECTION", to_translation( "Switch category selection mode" ) );
    ctxt.register_action( "TOGGLE_FAVORITE", to_translation( "Toggle favorite" ) );
    ctxt.register_action( "HOME", to_translation( "Home" ) );
    ctxt.register_action( "END", to_translation( "End" ) );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "INVENTORY_FILTER" );
    ctxt.register_action( "EXAMINE" );
    ctxt.register_action( "WIELD" );
    ctxt.register_action( "WEAR" );
    ctxt.register_action( "ANY_INPUT" ); // For invlets

    append_column( own_inv_column );
    append_column( map_column );
    append_column( own_gear_column );
}

inventory_selector::~inventory_selector() = default;

bool inventory_selector::empty() const { return is_empty; }

bool inventory_selector::has_available_choices() const
{
    return std::any_of( columns.begin(), columns.end(), []( const inventory_column * element ) {
        return element->has_available_choices();
    } );
}

inventory_input inventory_selector::get_input()
{
    inventory_input res;

    res.action = ctxt.handle_input();
    res.ch = ctxt.get_raw_input().get_first_input();
    res.entry = find_entry_by_invlet( res.ch );

    if( res.entry != nullptr && !res.entry->is_selectable() ) { res.entry = nullptr; }

    return res;
}

void inventory_selector::on_input( const inventory_input& input )
{
    if( input.action == "CATEGORY_SELECTION" ) {
        toggle_navigation_mode();
    } else if( input.action == "LEFT" ) {
        toggle_active_column( scroll_direction::BACKWARD );
    } else if( input.action == "RIGHT" ) {
        toggle_active_column( scroll_direction::FORWARD );
    } else if( input.action == "INVENTORY_FILTER" ) {
        set_filter();
    } else if( input.action == "TOGGLE_FAVORITE" ) {
        if( has_available_choices() ) {
            for( inventory_column * elem : columns ) { elem->on_input( input ); }
        }
        refresh_active_column(); // Columns can react to actions by losing their activation capacity
        prepare_layout();
    } else if( input.action == "WIELD" ) {
        auto& entry = const_cast<inventory_entry &>( get_selected() );
        wield( entry );
    } else if( input.action == "WEAR" ) {
        auto& entry = const_cast<inventory_entry &>( get_selected() );
        wear( entry );
    } else {
        if( has_available_choices() ) {
            for( inventory_column * elem : columns ) { elem->on_input( input ); }
        }
        refresh_active_column(); // Columns can react to actions by losing their activation capacity
    }
}

void inventory_selector::on_change( const inventory_entry& entry )
{
    for( auto& elem : columns ) { elem->on_change( entry ); }
    refresh_active_column(); // Columns can react to changes by losing their activation capacity
}

std::vector<inventory_column *> inventory_selector::get_visible_columns() const
{
    std::vector<inventory_column *> res( columns.size() );
    const auto iter =
        std::copy_if( columns.begin(), columns.end(), res.begin(), []( const inventory_column * e ) {
        return e->visible();
    } );
    res.resize( std::distance( res.begin(), iter ) );
    return res;
}

inventory_column &inventory_selector::get_column( size_t index ) const
{
    if( index >= columns.size() ) {
        static inventory_column dummy( preset );
        return dummy;
    }
    return *columns[index];
}

void inventory_selector::set_active_column( size_t index )
{
    if( index < columns.size() && index != active_column_index && get_column( index ).activatable() ) {
        get_active_column().on_deactivate();
        active_column_index = index;
        get_active_column().on_activate();
    }
}

size_t inventory_selector::get_columns_width( const std::vector<inventory_column*> &columns ) const
{
    return std::accumulate(
           columns.begin(), columns.end(), static_cast<size_t>( 0 ),
    []( const size_t &lhs, const inventory_column * column ) {
        return lhs + column->get_width();
    } );
}

double inventory_selector::get_columns_occupancy_ratio( size_t client_width ) const
{
    const auto visible_columns = get_visible_columns();
    const int free_width =
        client_width - get_columns_width( visible_columns )
        - min_column_gap * std::max( static_cast<int>( visible_columns.size() ) - 1, 0 );
    return 1.0 - static_cast<double>( free_width ) / client_width;
}

bool inventory_selector::are_columns_centered( size_t client_width ) const
{
    return get_columns_occupancy_ratio( client_width ) >= min_ratio_to_center;
}

bool inventory_selector::is_overflown( size_t client_width ) const
{
    return get_columns_occupancy_ratio( client_width ) > 1.0;
}

void inventory_selector::toggle_active_column( scroll_direction dir )
{
    if( columns.empty() ) { return; }

    size_t index = active_column_index;

    do {
        switch( dir ) {
            case scroll_direction::FORWARD:
                index = index + 1 < columns.size() ? index + 1 : 0;
                break;
            case scroll_direction::BACKWARD:
                index = index > 0 ? index - 1 : columns.size() - 1;
                break;
        }
    } while( index != active_column_index && !get_column( index ).activatable() );

    set_active_column( index );
}

void inventory_selector::toggle_navigation_mode()
{
    mode = get_navigation_data( mode ).next_mode;
    for( auto& elem : columns ) { elem->on_mode_change( mode ); }
}

void inventory_selector::append_column( inventory_column& column )
{
    column.on_mode_change( mode );

    if( columns.empty() ) { column.on_activate(); }

    columns.push_back( &column );
}

const navigation_mode_data &inventory_selector::get_navigation_data( navigation_mode m ) const
{
    static const std::map<navigation_mode, navigation_mode_data> mode_data = {
        {navigation_mode::ITEM, {navigation_mode::CATEGORY, translation(), c_light_gray}},
        {
            navigation_mode::CATEGORY,
            {navigation_mode::ITEM, to_translation( "Category selection mode" ), h_white}
        }
    };

    return mode_data.at( m );
}

std::string inventory_selector::action_bound_to_key( char key ) const
{
for( const std::string& action_descriptor : ctxt.get_registered_actions_copy() ) {
    for( char bound_key : ctxt.keys_bound_to( action_descriptor ) ) {
            if( key == bound_key ) { return action_descriptor; }
        }
    }
    return std::string();
}

std::vector<char> inventory_selector::all_bound_keys() const
{
    std::vector<char> retv;
    for( const std::string& action_descriptor : ctxt.get_registered_actions_copy() ) {
        std::vector<char> to_add = ctxt.keys_bound_to( action_descriptor );
        retv.insert( retv.end(), to_add.begin(), to_add.end() );
    }
    return retv;
}

bool inventory_pick_selector::uses_rml() const
{
    // Tier 3 slice 1: the single-select picker is the first selector lit for RmlUi.
    return inventory_rmlui_enabled();
}

bool inventory_drop_selector::uses_rml() const
{
    // Tier 3 slice 3: the multiselect mechanism (marks + selection column +
    // query_count) is proven through the drop selector. Shares the global toggle.
    return inventory_rmlui_enabled();
}

bool inventory_compare_selector::uses_rml() const
{
    // Tier 3 slice 4: two-selection compare. Same multiselect render as slice 3.
    return inventory_rmlui_enabled();
}

bool inventory_iuse_selector::uses_rml() const
{
    // Tier 3 slice 5: same multiselect render; custom stats via get_raw_stats.
    return inventory_rmlui_enabled();
}

bool inventory_pickup_selector::uses_rml() const
{
    // Tier 3 slice 5: same multiselect render as slice 3.
    return inventory_rmlui_enabled();
}

item *inventory_pick_selector::execute()
{
    shared_ptr_fast<ui_adaptor> ui = create_or_get_ui_adaptor();
    while( true ) {
        ui_manager::redraw();

        // A mouse click (rml_on_select(), fired during the redraw() above)
        // can't itself return out of this loop, so it leaves this pending
        // flag for us to consume — mirrors the CONFIRM branch below.
        if( rml_confirm_pending_ ) {
            rml_confirm_pending_ = false;
            const inventory_entry &selected = get_active_column().get_selected();
            if( selected ) { return selected.any_item(); }
        }

        const inventory_input input = get_input();

        if( input.entry != nullptr ) {
            if( select( input.entry->any_item() ) ) { ui_manager::redraw(); }
            return input.entry->any_item();
        } else if( input.action == "QUIT" ) {
            return nullptr;
        } else if( input.action == "CONFIRM" ) {
            const inventory_entry& selected = get_active_column().get_selected();
            if( selected ) { return selected.any_item(); }
        } else if( handle_action( input.action ) ) {
            return nullptr;
        } else {
            on_input( input );
        }
    }
}

inventory_multiselector::inventory_multiselector(
    player& p, const inventory_selector_preset& preset, const std::string& selection_column_title )
    : inventory_selector( p, preset ),
      selection_col( new selection_column( "SELECTION_COLUMN", selection_column_title ) )
{
    ctxt.register_action( "RIGHT", to_translation( "Mark/unmark selected item" ) );
    ctxt.register_action( "DROP_NON_FAVORITE", to_translation( "Mark/unmark non-favorite items" ) );

    for( auto& elem : get_all_columns() ) { elem->set_multiselect( true ); }
    append_column( *selection_col );
}

void inventory_multiselector::rearrange_columns( size_t client_width )
{
    selection_col->set_visibility( true );
    inventory_selector::rearrange_columns( client_width );
    selection_col->set_visibility( !is_overflown( client_width ) );
}

size_t inventory_multiselector::query_count( size_t count = 0 )
{
    std::string count_str;
    if( count == 0 ) {
        count_str = "";
    } else {
        count_str = std::to_string( count );
    }

    spopup = std::make_unique<string_input_popup>();
    spopup->max_length( 256 ).identifier( "inventory" ).text( count_str );

    shared_ptr_fast<ui_adaptor> current_ui = ui.lock();
    if( current_ui ) { current_ui->mark_resize(); }

    do {
        ui_manager::redraw();
        spopup->query_string( /*loop=*/false );
    } while( !spopup->confirmed() && !spopup->canceled() );

    if( spopup->confirmed() ) {
        count_str = spopup->text();
        try {
            count = std::stoull( count_str );
        } catch( const std::exception& e ) { count = 0; }

        if( current_ui ) { current_ui->mark_resize(); }
    }

    spopup.reset();
    return count;
}

void inventory_multiselector::set_chosen_count( inventory_entry& entry, size_t count )
{
    if( entry.is_item() ) {
        entry.chosen_count = std::min( count, entry.get_available_count() );
        on_change( entry );
    }
}

void inventory_multiselector::rml_toggle_mark( inventory_entry &entry )
{
    // Mirrors the per-entry invlet toggle in execute(): unmarked -> fully
    // marked, marked -> unmarked. set_chosen_count() is virtual, so this
    // reaches the owning subclass's own bookkeeping (dropping / to_use maps).
    set_chosen_count( entry, entry.chosen_count == 0 ? static_cast<size_t>( max_chosen_count ) : size_t{ 0 } );
}

[[clang::optnone]]
std::vector<inventory_entry *> inventory_multiselector::get_selection_column_items() const
{
    auto func = []( const inventory_entry & e ) { return e.is_item(); };
    return selection_col->get_entries( func );
}

inventory_compare_selector::inventory_compare_selector( player& p )
    : inventory_multiselector( p, default_preset, _( "ITEMS TO COMPARE" ) ) {}

std::pair<const item *, const item *> inventory_compare_selector::execute()
{
    shared_ptr_fast<ui_adaptor> ui = create_or_get_ui_adaptor();
    while( true ) {
        ui_manager::redraw();

        const inventory_input input = get_input();

        inventory_entry* just_selected = nullptr;

        if( input.entry != nullptr ) {
            select( input.entry->any_item() );
            toggle_entry( input.entry );
            just_selected = input.entry;
        } else if( input.action == "RIGHT" ) {
            const auto selection( get_active_column().get_all_selected() );

            for( auto& elem : selection ) {
                if( elem->chosen_count == 0 || selection.size() == 1 ) {
                    toggle_entry( elem );
                    just_selected = elem;
                    if( compared.size() == 2 ) { break; }
                }
            }
        } else if( input.action == "CONFIRM" ) {
            popup_getkey( _( "You need two items for comparison.  Use %s to select them." ),
                          ctxt.get_desc( "RIGHT" ) );
        } else if( input.action == "QUIT" ) {
            return std::make_pair( nullptr, nullptr );
        } else {
            on_input( input );
        }

        if( compared.size() == 2 ) {
            const auto res = std::make_pair( compared[0], compared[1] );
            // Clear second selected entry to prevent comparison reopening too
            // soon
            if( just_selected ) { toggle_entry( just_selected ); }
            return res;
        }
    }
}

void inventory_compare_selector::toggle_entry( inventory_entry* entry )
{
    const item* it = &*entry->any_item();
    const auto iter = std::find( compared.begin(), compared.end(), it );

    entry->chosen_count = iter == compared.end() ? 1 : 0;

    if( entry->chosen_count != 0 ) {
        compared.push_back( it );
    } else {
        compared.erase( iter );
    }

    on_change( *entry );
}

void inventory_compare_selector::rml_toggle_mark( inventory_entry &entry )
{
    toggle_entry( &entry );
}

inventory_iuse_selector::inventory_iuse_selector(
    player& p, const std::string& selector_title, const inventory_selector_preset& preset,
    const GetStats& get_st )
    : inventory_multiselector( p, preset, selector_title ),
      get_stats( get_st ) {}

std::vector<iuse_location> inventory_iuse_selector::execute()
{
    shared_ptr_fast<ui_adaptor> ui = create_or_get_ui_adaptor();

    int count = 0;
    while( true ) {
        ui_manager::redraw();

        const inventory_input input = get_input();

        if( input.ch >= '0' && input.ch <= '9' ) {
            count = std::min( count, INT_MAX / 10 - 10 );
            count *= 10;
            count += input.ch - '0';
        } else if( input.entry != nullptr ) {
            select( input.entry->any_item() );
            if( count == 0 && input.entry->chosen_count == 0 ) { count = max_chosen_count; }
            set_chosen_count( *input.entry, count );
            count = 0;
        } else if( input.action == "RIGHT" ) {
            const auto selected( get_active_column().get_all_selected() );

            if( count == 0 ) {
                const bool clear =
                std::none_of( selected.begin(), selected.end(), []( const inventory_entry * elem ) {
                    return elem->chosen_count > 0;
                } );

                if( clear ) { count = max_chosen_count; }
            }

            for( const auto& elem : selected ) { set_chosen_count( *elem, count ); }
            count = 0;
        } else if( input.action == "CONFIRM" ) {
            if( to_use.empty() ) {
                popup_getkey( _( "No items were selected.  Use %s to select them." ),
                              ctxt.get_desc( "RIGHT" ) );
                continue;
            }
            break;
        } else if( input.action == "QUIT" ) {
            return std::vector<iuse_location>();
        } else {
            on_input( input );
            count = 0;
        }
    }

    std::vector<iuse_location> ret;
    for( const auto& entry : to_use ) {
        for( const iuse_location& loc : entry.second ) { ret.push_back( loc ); }
    }
    return ret;
}

void inventory_iuse_selector::set_chosen_count( inventory_entry& entry, size_t count )
{
    const item* it = &*entry.any_item();

    if( count == 0 ) {
        entry.chosen_count = 0;
        const auto iter = to_use.find( it );
        if( iter != to_use.end() ) { to_use.erase( iter ); }
    } else {
        entry.chosen_count = std::min(
                                 std::min( count, static_cast<size_t>( max_chosen_count ) ), entry.get_available_count() );
        to_use[it].clear();
        if( entry.locations.size() == 1 ) {
            to_use[it].emplace_back( *entry.locations[0], static_cast<int>( entry.chosen_count ) );
        } else {
            for( size_t i = 0; i < entry.chosen_count; i++ ) {
                to_use[it].emplace_back( *entry.locations[i], 1 );
            }
        }
    }

    on_change( entry );
}

inventory_selector::stats inventory_iuse_selector::get_raw_stats() const
{
    if( get_stats ) {
    std::map<const item *, int> tmp;
    for( const auto& elem : to_use ) {
            int num = 0;
            for( const iuse_location& loc : elem.second ) { num += static_cast<int>( loc.count ); }
            tmp.insert( std::make_pair( elem.first, num ) );
        }
        return get_stats( tmp );
    }
    return stats{{stat{{"", "", "", ""}}, stat{{"", "", "", ""}}}};
}

inventory_drop_selector::inventory_drop_selector( player& p,
        const inventory_selector_preset& preset )
    : inventory_multiselector( p, preset, _( "ITEMS TO DROP" ) ) {}

void inventory_drop_selector::process_selected(
    int &count, const std::vector<inventory_entry *> &selected )
{
    if( count == 0 ) {
        const bool clear =
        std::none_of( selected.begin(), selected.end(), []( const inventory_entry * elem ) {
            return elem->chosen_count > 0;
        } );

        if( clear ) { count = max_chosen_count; }
    }

    for( const auto& elem : selected ) { set_chosen_count( *elem, count ); }
    count = 0;
}

drop_locations inventory_drop_selector::execute()
{
    shared_ptr_fast<ui_adaptor> ui = create_or_get_ui_adaptor();
    this->keep_open = false;

    // if we favorited an item, we exited this function and entered it again
    // the selected items are in "dropping", so we fetch and display them in the UI
    std::vector<std::pair<inventory_entry *, int>> selected_entries;
    const auto always_yes = []( const inventory_entry & ) { return true; };

    for( const std::pair<const item * const, int> drop_pair : dropping ) {
        for( auto& col : get_all_columns() ) {
            for( const auto& entry : col->get_entries( always_yes ) ) {
                if( entry->any_item() == drop_pair.first ) {
                    selected_entries.emplace_back( entry, drop_pair.second );
                }
            }
        }
    }

    // empty the dropping variable, in case of 2 stacks of items being merged on unfavorite/favorite
    dropping.clear();
    for( auto selected_entry : selected_entries ) {
        set_chosen_count( *selected_entry.first, selected_entry.second );
    }

    // main multidrop selection loop
    int count = 0;
    while( true ) {
        ui_manager::redraw();

        const inventory_input input = get_input();

        if( input.ch >= '0' && input.ch <= '9' ) {
            const auto selected( get_active_column().get_all_selected() );

            count = query_count( input.ch - '0' );
            count = std::min( count, max_chosen_count );
            for( const auto& elem : selected ) { set_chosen_count( *elem, count ); }

            count = 0;
        } else if( input.entry != nullptr ) {
            select( input.entry->any_item() );
            if( count == 0 && input.entry->chosen_count == 0 ) { count = max_chosen_count; }
            set_chosen_count( *input.entry, count );
            count = 0;
        } else if( input.action == "DROP_NON_FAVORITE" ) {
            const auto filter_to_nonfavorite_and_nonworn = []( const inventory_entry & entry ) {
                return entry.is_item() && !entry.any_item()->is_favorite
                       && !g->u.is_worn( *entry.any_item() );
            };

            const auto selected( get_active_column().get_entries( filter_to_nonfavorite_and_nonworn ) );
            process_selected( count, selected );
        } else if( input.action == "RIGHT" ) {
            const auto selected( get_active_column().get_all_selected() );

            // No amount entered, select all
            if( count == 0 ) {
                count = max_chosen_count;

                // Any non favorite item to select?
                const bool select_nonfav =
                std::any_of( selected.begin(), selected.end(), []( const inventory_entry * elem ) {
                    return ( !elem->any_item()->is_favorite ) && elem->chosen_count == 0;
                } );

                // Otherwise, any favorite item to select?
                const bool select_fav =
                    !select_nonfav
                    && std::
                any_of( selected.begin(), selected.end(), []( const inventory_entry * elem ) {
                    return elem->any_item()->is_favorite && elem->chosen_count == 0;
                } );

                for( const auto& elem : selected ) {
                    const bool is_favorite = elem->any_item()->is_favorite;
                    if( ( select_nonfav && !is_favorite ) || ( select_fav && is_favorite ) ) {
                        set_chosen_count( *elem, count );
                    } else if( !select_nonfav && !select_fav ) {
                        // Every element is selected, unselect all
                        set_chosen_count( *elem, 0 );
                    }
                }
                // Select the entered amount
            } else {
                for( const auto& elem : selected ) { set_chosen_count( *elem, count ); }
            }

            count = 0;
        } else if( input.action == "CONFIRM" ) {
            if( dropping.empty() ) {
                popup_getkey( _( "No items were selected.  Use %s to select them." ),
                              ctxt.get_desc( "RIGHT" ) );
                continue;
            }
            break;
        } else if( input.action == "QUIT" ) {
            return drop_locations();
        } else {
            on_input( input );
            count = 0;
        }
    }

    drop_locations dropped_pos_and_qty;

    for( const std::pair<item *, int> drop_pair : dropping ) {
        // Note: drop_location here contains location of first item in stack,
        // and amount of items to be dropped from the stack.
        dropped_pos_and_qty.emplace_back( *drop_pair.first, drop_pair.second );
    }

    return dropped_pos_and_qty;
}

void inventory_drop_selector::set_chosen_count( inventory_entry& entry, size_t count )
{
    item* it = entry.item_stack_on_character();

    if( count == 0 ) {
        entry.chosen_count = 0;
        const auto iter = dropping.find( it );
        if( iter != dropping.end() ) { dropping.erase( iter ); }
    } else {
        entry.chosen_count = std::min(
                                 std::min( count, static_cast<size_t>( max_chosen_count ) ), entry.get_available_count() );
        dropping[it] = entry.chosen_count;
    }

    on_change( entry );
}

inventory_selector::stats inventory_drop_selector::get_raw_stats() const
{
    return get_weight_and_volume_stats(
           u.weight_carried_reduced_by( dropping ), u.weight_capacity(),
    u.volume_carried_reduced_by( dropping ), u.volume_capacity_reduced_by( 0_ml, dropping ) );
}

inventory_pickup_selector::inventory_pickup_selector(
    player& p, const inventory_selector_preset& preset )
    : inventory_multiselector( p, preset, "ITEMS TO PICKUP" ) {}

std::vector<pickup::pick_drop_selection> inventory_pickup_selector::execute()
{
    shared_ptr_fast<ui_adaptor> ui = create_or_get_ui_adaptor();

    while( true ) {
        ui_manager::redraw();

        const inventory_input input = get_input();

        if( input.ch >= '0' && input.ch <= '9' ) {
            const auto selected( get_active_column().get_all_selected() );

            size_t count = query_count( input.ch - '0' );
            for( const auto& elem : selected ) { set_chosen_count( *elem, count ); }
        } else if( input.entry != nullptr ) {
            select( input.entry->any_item() );
            if( input.entry->chosen_count == 0 ) {
                set_chosen_count( *input.entry, max_chosen_count );
            }
        } else if( input.action == "RIGHT" ) {
            const auto selected( get_active_column().get_all_selected() );

            size_t count = max_chosen_count;

            // Any non favorite item to select?
            const bool select_nonfav =
            std::any_of( selected.begin(), selected.end(), []( const inventory_entry * elem ) {
                return ( !elem->any_item()->is_favorite ) && elem->chosen_count == 0;
            } );

            // Otherwise, any favorite item to select?
            const bool select_fav =
                !select_nonfav
            && std::any_of( selected.begin(), selected.end(), []( const inventory_entry * elem ) {
                return elem->any_item()->is_favorite && elem->chosen_count == 0;
            } );

            for( const auto& elem : selected ) {
                const bool is_favorite = elem->any_item()->is_favorite;
                if( ( select_nonfav && !is_favorite ) || ( select_fav && is_favorite ) ) {
                    set_chosen_count( *elem, count );
                } else if( !select_nonfav && !select_fav ) {
                    // Every element is selected, unselect all
                    set_chosen_count( *elem, 0 );
                }
            }
        } else if( input.action == "CONFIRM" ) {
            std::vector<pickup::pick_drop_selection> result;
            std::vector<item *> locations;
            std::vector<int> counts;

            for( auto entry_ptr : get_selection_column_items() ) {
                int count = 0;
                int chosen_count = entry_ptr->chosen_count;
                for( size_t i = 0; i < entry_ptr->locations.size() && count < chosen_count &&
                     count < max_chosen_count; ++i ) {
                    item *item = entry_ptr->locations[i];
                    int needed_count = std::max( 0, chosen_count - count );
                    int to_add = std::min( needed_count, item->count() );
                    if( to_add > 0 ) {
                        locations.push_back( entry_ptr->locations[i] );
                        counts.push_back( to_add );
                    }
                }
            }

            result = pickup::optimize_pickup( locations, counts );
            if( result.empty() ) {
                popup_getkey( _( "No items were selected.  Use %s to select them." ),
                              ctxt.get_desc( "RIGHT" ) );
                continue;
            } else {
                return result;
            }
        } else if( input.action == "QUIT" ) {
            return std::vector<pickup::pick_drop_selection>();
        } else {
            on_input( input );
        }

        bool no_items = true;
        for( auto col_ptr : get_visible_columns() ) {
            if( col_ptr->allows_selecting() && !col_ptr->empty() ) {
                no_items = false;
                break;
            }
        }

        if( no_items ) { return std::vector<pickup::pick_drop_selection>(); }
    }

    return std::vector<pickup::pick_drop_selection>();
}

[[clang::optnone]]
inventory_selector::stats inventory_pickup_selector::get_raw_stats() const
{
    units::mass weight_carried = u.weight_carried();
    units::volume volume_carried = u.volume_carried();
    auto func = []( const inventory_entry & entry ) {
        return entry.is_item() && entry.chosen_count > 0;
    };
    std::vector<inventory_entry *> selected_items = selection_col->get_all_entries( func );

    // Add the weights and volumes of selected items
    // which might be picked up
    for( auto entry_ptr : selected_items ) {
        int count = 0;
        int chosen_count = entry_ptr->chosen_count;
        for( size_t i = 0; i < entry_ptr->locations.size() && count < chosen_count &&
             count < max_chosen_count; ++i ) {
            item *item = entry_ptr->locations[i];
            int needed_count = std::max( 0, chosen_count - count );
            int to_add = std::min( needed_count, item->count() );
            count += to_add;
            //WARNING: This specific order of operations and casts are needed
            //to ensure volume and weight are accurate, because of the fact the game
            //works in base volumes of 1ml and 1mg
            weight_carried += item->weight() * ( static_cast<double>( to_add ) / item->count() );
            volume_carried += item->volume() * ( static_cast<double>( to_add ) / item->count() );
        }
    }

    return get_weight_and_volume_stats(
               weight_carried, u.weight_capacity(), volume_carried, u.volume_capacity() );
}
