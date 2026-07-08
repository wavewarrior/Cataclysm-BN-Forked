#include "trade_win.h"

#include "avatar.h"
#include "catacharset.h"
#include "color.h"
#include "game.h"
#include "ime.h"
#include "input.h"
#include "item.h"
#include "item_category.h"
#include "item_contents.h"
#include "item_search.h"
#include "itype.h"
#include "npc.h"
#include "output.h"
#include "player.h"
#include "point.h"
#include "rml_screen.h"
#include "rml_util.h"
#include "string_formatter.h"
#include "string_input_popup.h"
#include "string_utils.h"
#include "translations.h"
#include "ui_manager.h"
#include "units_utility.h"

#include <RmlUi/Core.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <vector>

// Tier 5 (interaction dialogs): the NPC trade screen RmlUi render path. Render-
// only doc mirroring update_win — two item panes (theirs | yours) + head bar
// (title / credit-debt / category toggle / hints) + per-pane stats + info pane.
// Keyboard owns all of it (page-relative letter hotkeys preserved → the RmlUi
// path renders the SAME visible page as curses, not a native-scroll-all list).
bool &trade_rmlui_enabled()
{
    static bool enabled = true;
    return enabled;
}

namespace
{
// RmlUi data-model for the trade screen. Each pane is a Vector of baked rows
// (one colour-tagged monospace-aligned string per row + a selected flag for the
// cursor / category hilite). Distinct registered row type; single registration.
struct trade_row {
    Rml::String text_rml;
    bool selected = false;
    bool header = false;
};
struct trade_session {
    Rml::String title_rml;
    Rml::String cost_rml;
    Rml::String category_rml;
    Rml::String hints_rml;
    Rml::String them_name_rml;
    Rml::String you_name_rml;
    Rml::String them_stats_rml;
    Rml::String you_stats_rml;
    Rml::String them_header_rml;
    Rml::String you_header_rml;
    Rml::String them_foot_rml;
    Rml::String you_foot_rml;
    bool them_focus = true;
    bool you_focus = false;
    Rml::Vector<trade_row> them_rows;
    Rml::Vector<trade_row> you_rows;
    bool show_info = false;
    Rml::String info_rml;
    Rml::DataModelHandle handle;
};

bool g_trade_types_registered = false;

void register_trade_rml_types( Rml::DataModelConstructor& c )
{
    if( g_trade_types_registered ) { return; }
    Rml::StructHandle<trade_row> rh = c.RegisterStruct<trade_row>();
    rh.RegisterMember( "text_rml", &trade_row::text_rml );
    rh.RegisterMember( "selected", &trade_row::selected );
    rh.RegisterMember( "header", &trade_row::header );
    c.RegisterArray<Rml::Vector<trade_row>>();
    g_trade_types_registered = true;
}

// Separate data-model for the EXAMINE item-description popup (show_item_data);
// stacks over the still-open "trade" doc. One colour-tagged body string + the
// pane-side flag (mirrors the curses info_win = w_you / w_them choice).
struct trade_iteminfo_session {
    Rml::String info_rml;
    bool on_right = false;
    Rml::DataModelHandle handle;
};

constexpr auto trade_head_height = 4;
constexpr auto trade_info_height = 4;
constexpr auto trade_header_rows = 4;
constexpr auto trade_header_separator_rows = 0;
constexpr auto trade_total_header_rows = trade_header_rows + trade_header_separator_rows;

struct category_range {
    item_category_id id;
    size_t start = 0;
    size_t end = 0;
};

auto build_page_starts(
    const std::vector<item_pricing> &list, const std::vector<size_t> &filtered,
    size_t rows_per_page ) -> std::vector<size_t>
{
    auto starts = std::vector<size_t> {};
    if( rows_per_page <= 1 ) {
        starts = std::views::iota( size_t{0}, filtered.size() ) | std::ranges::to<std::vector>();
        if( starts.empty() ) { starts.push_back( 0 ); }
        return starts;
    }
    if( filtered.empty() ) {
        starts.push_back( 0 );
        return starts;
    }
    auto index = size_t{0};
    while( index < filtered.size() ) {
        starts.push_back( index );
        auto row = size_t{0};
        auto last_category = std::optional<item_category_id> {};
        while( index < filtered.size() ) {
            const auto& ip = list[filtered[index]];
            const auto category_id = ip.locs.front()->get_category().get_id();
            if( !last_category || *last_category != category_id ) {
                if( row + 2 > rows_per_page && row > 0 ) { break; }
                row += 1;
            }
            if( row + 1 > rows_per_page ) { break; }
            row += 1;
            last_category = category_id;
            index++;
            if( row >= rows_per_page ) { break; }
        }
    }
    return starts;
}

auto page_index_for_offset( const std::vector<size_t> &page_starts, size_t offset ) -> size_t
{
    if( page_starts.empty() ) { return 0; }
const auto it = std::ranges::upper_bound( page_starts, offset );
if( it == page_starts.begin() ) { return 0; }
return static_cast<size_t>( std::distance( page_starts.begin(), it ) - 1 );
}

auto build_category_ranges(
    const std::vector<item_pricing> &list, const std::vector<size_t> &filtered )
-> std::vector<category_range>
{
    auto ranges = std::vector<category_range> {};
    std::ranges::for_each( std::views::iota( size_t{0}, filtered.size() ), [&]( size_t idx ) {
        const auto list_index = filtered[idx];
        const auto& ip = list[list_index];
        const auto category_id = ip.locs.front()->get_category().get_id();
        if( ranges.empty() || ranges.back().id != category_id ) {
            if( !ranges.empty() ) { ranges.back().end = idx; }
            ranges.push_back( category_range{.id = category_id, .start = idx, .end = idx + 1} );
        } else {
            ranges.back().end = idx + 1;
        }
    } );
    return ranges;
}

auto register_trade_actions( input_context& ctxt, bool include_any_input ) -> void
{
    ctxt.register_action( "SWITCH_LISTS" );
    ctxt.register_action( "PAGE_UP" );
    ctxt.register_action( "PAGE_DOWN" );
    ctxt.register_action( "UP" );
    ctxt.register_action( "DOWN" );
    ctxt.register_action( "LEFT" );
    ctxt.register_action( "RIGHT" );
    ctxt.register_action( "FILTER" );
    ctxt.register_action( "RESET_FILTER" );
    ctxt.register_action( "CATEGORY_SELECTION" );
    ctxt.register_action( "EXAMINE" );
    ctxt.register_action( "AUTOBALANCE" );
    ctxt.register_action( "TOGGLE_ITEM_INFO" );
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    if( include_any_input ) { ctxt.register_action( "ANY_INPUT" ); }
}
} // namespace

trading_window::trading_window( npc_trading::trade_state& state ): state( state ) {}

trading_window::~trading_window() = default;

auto trading_window::setup_win( ui_adaptor& ui ) -> void
{
    const auto win_they_w = TERMX / 2;
    const auto info_height = show_item_info ? trade_info_height : 0;
    entries_per_page = std::
                       min( TERMY - trade_head_height - info_height - 3 - trade_total_header_rows,
                            2 + ( 'z' - 'a' ) + ( 'Z' - 'A' ) );
    w_head = catacurses::newwin( trade_head_height, TERMX, point_zero );
    const auto list_height = TERMY - trade_head_height - info_height;
    w_them = catacurses::newwin( list_height, win_they_w, point( 0, trade_head_height ) );
    w_you =
        catacurses::newwin( list_height, TERMX - win_they_w, point( win_they_w, trade_head_height ) );
    w_info = catacurses::newwin( info_height, TERMX, point( 0, trade_head_height + list_height ) );
    ui.position( point_zero, point( TERMX, TERMY ) );
}

auto trading_window::show_item_data( size_t index, bool target_is_theirs ) -> info_popup_result
{
    auto& target_list = target_is_theirs ? state.theirs : state.yours;
    if( index >= target_list.size() ) { return info_popup_result::none; }

    const auto& info_win = target_is_theirs ? w_you : w_them;
    auto ui = ui_adaptor{};
    auto w_popup = catacurses::window{};
    auto scroll_pos = size_t{0};
    ui.on_screen_resize( [&]( ui_adaptor & ui ) {
        const auto width = std::max( getmaxx( info_win ), 1 );
        const auto height = std::max( getmaxy( info_win ), 1 );
        const auto pos = point( getbegx( info_win ), getbegy( info_win ) );
        w_popup = catacurses::newwin( height, width, pos );
        ui.position_from_window( w_popup );
    } );
    ui.mark_resize();

    const auto& itm = *target_list[index].locs.front();
    const auto info_text = itm.info_string();

    // RmlUi render path (render-only; keyboard owns scroll + exit-to-adjacent
    // below). The doc stacks over the still-open "trade" doc, overlaying the
    // pane the curses w_popup covered. The body element scrolls natively; the
    // colour-tagged text is baked once (static for this popup instance), so no
    // per-frame sync is needed. info_data declared before rml so it outlives it.
    std::unique_ptr<trade_iteminfo_session> info_data;
    rml_doc info_rml;
    Rml::Element* scroll_el = nullptr;

    auto ctxt = input_context( "NPC_TRADE" );
    ctxt.register_action( "UP" );
    ctxt.register_action( "DOWN" );
    ctxt.register_action( "PAGE_UP" );
    ctxt.register_action( "PAGE_DOWN" );
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );

    info_rml.open( trade_rmlui_enabled(), "trade_iteminfo", ctxt, [&]( Rml::DataModelConstructor & c ) {
        info_data = std::make_unique<trade_iteminfo_session>();
        info_data->info_rml = cata_text_to_rml( info_text );
        info_data->on_right = target_is_theirs;
        c.Bind( "info_rml", &info_data->info_rml );
        c.Bind( "on_right", &info_data->on_right );
        info_data->handle = c.GetModelHandle();
    } );
    if( info_rml ) { scroll_el = info_rml.document()->GetElementById( "trade-iteminfo-body" ); }

    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( info_rml ) { return; }
    } );

    // Scroll the RmlUi body element by a fraction of its viewport (cf. help.cpp).
    const auto scroll_rml = [&]( float frac ) {
        if( scroll_el == nullptr ) { return; }
        const float ch = scroll_el->GetClientHeight();
        const float max_top = std::max( 0.0f, scroll_el->GetScrollHeight() - ch );
        const float t = std::clamp( scroll_el->GetScrollTop() + frac * ch, 0.0f, max_top );
        scroll_el->SetScrollTop( t );
    };

    auto result = info_popup_result::none;
    auto exit = false;
    while( !exit ) {
        ui_manager::redraw();
        auto action = ctxt.handle_input();
        if( action == "UP" ) {
            result = info_popup_result::move_up;
            exit = true;
        } else if( action == "DOWN" ) {
            result = info_popup_result::move_down;
            exit = true;
        } else if( action == "PAGE_UP" || action == "PAGE_DOWN" ) {
            if( info_rml ) {
                scroll_rml( action == "PAGE_UP" ? -0.9f : 0.9f );
            } else {
                const auto inner_h = std::max( getmaxy( w_popup ) - 2, 1 );
                const auto folded = foldstring( info_text, std::max( getmaxx( w_popup ) - 2, 1 ) );
                const auto max_scroll =
                    folded.size() > static_cast<size_t>( inner_h )
                    ? folded.size() - static_cast<size_t>( inner_h )
                    : 0;
                const auto page_rem = static_cast<size_t>( inner_h );
                if( action == "PAGE_UP" ) {
                    scroll_pos = scroll_pos > page_rem ? scroll_pos - page_rem : 0;
                } else {
                    scroll_pos = std::min( scroll_pos + page_rem, max_scroll );
                }
            }
        } else if( action == "CONFIRM" || action == "QUIT" ) {
            exit = true;
        }
    }
    info_rml.close();
    return result;
}

auto trading_window::build_filtered_indices(
    const std::vector<item_pricing> &list, const std::string& filter ) const -> std::vector<size_t>
{
    if( filter.empty() ) {
    return std::views::iota( size_t{0}, list.size() ) | std::ranges::to<std::vector>();
    }
    const auto filter_fn = item_filter_from_string( filter );
    return std::views::iota( size_t{0}, list.size() )
    | std::views::filter( [&]( size_t idx ) { return filter_fn( *list[idx].locs.front() ); } )
    | std::ranges::to<std::vector>();
}

auto trading_window::get_var_trade( const item& it, int total_count, int amount_hint ) -> int
{
    auto popup_input = string_input_popup{};
    auto how_many = total_count;
    const auto contained = it.is_container() && !it.contents.empty();

    const auto title =
        contained
        ? string_format( _( "Trade how many containers with %s [MAX: %d]: " ),
                         it.get_contained().type_name( how_many ), total_count )
        : string_format( _( "Trade how many %s [MAX: %d]: " ), it.type_name( how_many ), total_count );
    if( amount_hint > 0 ) {
        popup_input.description( string_format(
                                     _( "Hint: You can buy up to %d with your current balance." ),
                                     std::min( amount_hint, total_count ) ) );
    } else if( amount_hint < 0 ) {
        popup_input.description(
            string_format( _( "Hint: You'll need to offer %d to even out the deal." ), -amount_hint ) );
    }
    popup_input.title( title ).edit( how_many );
    if( popup_input.canceled() || how_many <= 0 ) { return -1; }
    return std::min( total_count, how_many );
}

auto trading_window::perform_trade( npc& np, const std::string& deal ) -> bool
{
    state.volume_left = np.volume_capacity() - np.volume_carried();
    state.weight_left = np.weight_capacity() - np.weight_carried();

    // Shopkeeps are happy to have large inventories.
    if( np.is_shopkeeper() ) {
        state.volume_left = 5000_liter;
        state.weight_left = 5000_kilogram;
    }

    auto ctxt = input_context( "NPC_TRADE" );
    register_trade_actions( ctxt, true );

    auto ui = ui_adaptor{};
    ui.on_screen_resize( [this]( ui_adaptor & ui ) { setup_win( ui ); } );
    ui.mark_resize();

    // RmlUi render path (render-only; keyboard owns nav/select/confirm below).
    // Renders the SAME visible page as curses (page-relative letter hotkeys),
    // not a native-scroll-all list, so the displayed hotkeys match the input loop.
    auto rml_data = std::make_unique<trade_session>();
    rml_doc rml;
    const auto sync_rml = [&]() {
        if( !rml_data->handle ) { return; }
        const auto trade_accept = npc_trading::npc_will_accept_trade( state, np );

        // ── Head bar ──────────────────────────────────────────────────────
        const std::string title_label =
            deal == _( "Pay:" ) ? _( "Paying" )
            : deal == _( "Reward" )
            ? _( "Accepting a reward from" )
            : _( "Trading with" );
        rml_data->title_rml = cata_text_to_rml(
                                  colorize( title_label, c_white ) + " " + colorize( np.disp_name(), c_light_green ) );

        std::string cost_str = _( "Exchange" );
        if( !np.will_exchange_items_freely() ) {
            cost_str = string_format(
                           state.your_balance >= 0 ? _( "Credit %s" ) : _( "Debt %s" ),
                           format_money( std::abs( state.your_balance ) ) );
        }
        rml_data->cost_rml = cata_text_to_rml( colorize( cost_str, trade_accept ? c_green : c_red ) );
        rml_data->category_rml = cata_text_to_rml(
                                     _( "Category select: " ) + colorize( _( "ON" ), category_mode ? c_light_green : c_dark_gray )
                                     + colorize( "|", c_white )
                                     + colorize( _( "OFF" ), category_mode ? c_dark_gray : c_light_green ) );
        {
            const auto hint = [&]( const std::string & act, const std::string & label ) -> std::string {
                return colorize( "[" + ctxt.get_desc( act, 1 ) + "]", c_yellow ) + " "
                + colorize( label, c_light_gray ) + "   ";
            };
            rml_data->hints_rml = cata_text_to_rml(
                                      hint( "EXAMINE", _( "examine" ) ) + hint( "SWITCH_LISTS", _( "switch panes" ) )
                                      + hint( "CONFIRM", _( "confirm trade" ) ) + hint( "AUTOBALANCE", _( "autobalance" ) )
                                      + hint( "FILTER", _( "filter" ) ) + hint( "CATEGORY_SELECTION", _( "category" ) ) );
        }

        them_filtered = build_filtered_indices( state.theirs, them_filter );
        you_filtered = build_filtered_indices( state.yours, you_filter );

        // Player free capacity (mirrors update_win) for the YOU pane stats.
        const auto sel_amount = []( const item_pricing & ip, bool is_theirs ) -> int {
            if( ip.charges > 0 ) { return is_theirs ? ip.u_charges : ip.npc_charges; }
        return is_theirs ? ip.u_has : ip.npc_has;
    };
    units::volume your_sel_vol = 0_ml, their_sel_vol = 0_ml;
    units::mass your_sel_wt = 0_gram, their_sel_wt = 0_gram;
    for( const item_pricing& ip : state.yours ) {
            const int a = sel_amount( ip, false );
            your_sel_vol += ip.vol * a;
            your_sel_wt += ip.weight * a;
        }
        for( const item_pricing& ip : state.theirs ) {
            const int a = sel_amount( ip, true );
            their_sel_vol += ip.vol * a;
            their_sel_wt += ip.weight * a;
        }
        const auto player_free_volume =
            g->u.volume_capacity() - g->u.volume_carried() + your_sel_vol - their_sel_vol;
        const auto player_free_weight =
            g->u.weight_capacity() - g->u.weight_carried() + your_sel_wt - their_sel_wt;

        const auto align_left = []( const std::string & t, int w ) -> std::string {
            return t + std::string( std::max<int>( w - utf8_width( t ), 0 ), ' ' );
        };
        const auto align_right = []( const std::string & t, int w ) -> std::string {
            return std::string( std::max<int>( w - utf8_width( t ), 0 ), ' ' ) + t;
        };

        // ── Per-pane fill ─────────────────────────────────────────────────
        const auto fill_pane = [&]( bool they ) {
            const auto& list = they ? state.theirs : state.yours;
            const auto& filtered = they ? them_filtered : you_filtered;
            const size_t offset = they ? them_off : you_off;
            const size_t cursor = they ? them_cursor : you_cursor;
            const player& person = they ? static_cast<player &>( np ) : static_cast<player &>( g->u );
            Rml::Vector<trade_row> &out = they ? rml_data->them_rows : rml_data->you_rows;
            Rml::String &name_out = they ? rml_data->them_name_rml : rml_data->you_name_rml;
            Rml::String &stats_out = they ? rml_data->them_stats_rml : rml_data->you_stats_rml;
            Rml::String &header_out = they ? rml_data->them_header_rml : rml_data->you_header_rml;
            Rml::String &foot_out = they ? rml_data->them_foot_rml : rml_data->you_foot_rml;
            out.clear();

            name_out = cata_text_to_rml(
                           colorize( _( "Inventory:" ), c_white ) + " "
                           + colorize( they ? np.name : _( "You" ), c_light_green ) );

            // Per-pane weight/volume used/max (skipped for shopkeepers' own pane).
            if( !they || !np.is_shopkeeper() ) {
                const auto free_vol = they ? state.volume_left : player_free_volume;
                const auto free_wt = they ? state.weight_left : player_free_weight;
                const auto max_vol = they ? np.volume_capacity() : g->u.volume_capacity();
                const auto max_wt = they ? np.weight_capacity() : g->u.weight_capacity();
                const auto used_vol = max_vol - free_vol;
                const auto used_wt = max_wt - free_wt;
                const auto wt_col = used_wt > max_wt ? c_light_red : c_light_green;
                const auto vol_col = used_vol > max_vol ? c_light_red : c_light_green;
                stats_out = cata_text_to_rml(
                                colorize( string_format( "%.2f", convert_weight( used_wt ) ), wt_col )
                                + string_format( _( "/%s %s  " ), string_format( "%.2f", convert_weight( max_wt ) ),
                                                 weight_units() )
                                + colorize( string_format( "%.2f", convert_volume( to_milliliter( used_vol ) ) ),
                                            vol_col )
                                + string_format(
                                    _( "/%s %s" ), string_format( "%.2f", convert_volume( to_milliliter( max_vol ) ) ),
                                    volume_units_abbr() ) );
            } else {
                stats_out.clear();
            }

            // Column widths over the visible window (monospace grid).
            const size_t end = std::min( filtered.size(), offset + entries_per_page );
            int name_w = utf8_width( _( "Name (charges)" ) );
            int qty_w = utf8_width( _( "amt" ) );
            int weight_w = utf8_width( _( "weight" ) );
            int vol_w = utf8_width( _( "vol" ) );
            int price_w = utf8_width( _( "unit price" ) );
            for( size_t i = offset; i < end; i++ ) {
                const item_pricing& ip = list[filtered[i]];
                const int amt = ip.charges > 0 ? ip.charges : std::max( ip.count, 1 );
                name_w = std::max( name_w, utf8_width( ip.locs.front()->display_name() ) );
                if( ( ip.charges > 0 ? ip.charges : ip.count ) > 1 ) {
                    qty_w = std::max(
                                qty_w,
                                utf8_width( string_format( "%d", ip.charges > 0 ? ip.charges : ip.count ) ) );
                }
                weight_w = std::max(
                               weight_w, utf8_width( string_format( "%.2f", convert_weight( ip.weight * amt ) ) ) );
                vol_w = std::max(
                            vol_w,
                            utf8_width( string_format( "%.2f", convert_volume( to_milliliter( ip.vol * amt ) ) ) ) );
                price_w = std::max( price_w, utf8_width( format_money( ip.price ) ) );
            }
            name_w = std::min( name_w, 40 );

            header_out = cata_text_to_rml( colorize(
                                               align_left( _( "Name (charges)" ), name_w + 4 ) + " " + align_left( _( "amt" ), qty_w )
                                               + " " + align_left( _( "weight" ), weight_w ) + " " + align_left( _( "vol" ), vol_w )
                                               + " " + align_left( _( "unit price" ), price_w ),
                                               c_light_gray ) );

            const bool is_focused = ( they && focus_them ) || ( !they && !focus_them );
            const auto category_ranges = build_category_ranges( list, filtered );
            std::optional<item_category_id> active_category;
            if( category_mode && is_focused && !category_ranges.empty() ) {
                const size_t cc = they ? them_category_cursor : you_category_cursor;
                if( cc < category_ranges.size() ) { active_category = category_ranges[cc].id; }
            }

            const std::string hotkeys = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
            auto last_category = std::optional<item_category_id> {};
            size_t row = 0;
            for( size_t i = offset; i < filtered.size() && row < entries_per_page; i++ ) {
                const item_pricing& ip = list[filtered[i]];
                const item* it = ip.locs.front();
                const auto category_id = it->get_category().get_id();
                if( !last_category || *last_category != category_id ) {
                    trade_row h;
                    h.header = true;
                    h.text_rml = cata_text_to_rml(
                                     colorize( to_upper_case( it->get_category().name() ), c_magenta ) );
                    out.push_back( h );
                    row++;
                    if( row >= entries_per_page ) { break; }
                }
                nc_color color = it == &person.primary_weapon() ? c_yellow : c_light_gray;
                std::string itname = it->display_name();
                if( np.will_exchange_items_freely()
                    && ip.locs.front()->where() != item_location_type::character ) {
                    itname += " (" + ip.locs.front()->describe_location( &g->u ) + ")";
                    color = c_light_blue;
                }
                if( ip.selected ) { color = c_white; }
                const bool is_cursor = is_focused && i == cursor;
                const bool is_cat_sel = active_category && *active_category == category_id;

                const int owner_sells = they ? ip.u_has : ip.npc_has;
                const int owner_sells_charge = they ? ip.u_charges : ip.npc_charges;
                const int total_amount = ip.charges > 0 ? ip.charges : std::max( ip.count, 1 );
                const int selected_amount = ip.charges > 0 ? owner_sells_charge : owner_sells;
                char selection_mark = '-';
                if( selected_amount >= total_amount && total_amount > 0 ) {
                    selection_mark = '+';
                } else if( selected_amount > 0 ) {
                    selection_mark = '#';
                }
                const size_t hotkey_index = i - offset;
                const char keychar = hotkey_index < hotkeys.size() ? hotkeys[hotkey_index] : ' ';

                const int available_amount = ip.charges > 0 ? ip.charges : ip.count;
                const std::string qty_str =
                    available_amount > 1 ? string_format( "%d", available_amount ) : std::string{};
                const std::string weight_str =
                    string_format( "%.2f", convert_weight( ip.weight * available_amount ) );
                const std::string vol_str =
                    string_format( "%.2f", convert_volume( to_milliliter( ip.vol * available_amount ) ) );

                std::string price_str = format_money( ip.price );
                nc_color price_color = c_light_gray;
                if( !np.will_exchange_items_freely() ) {
                    const auto base_price = it->price( true );
                    if( base_price > 0 ) {
                        const double ratio = static_cast<double>( ip.price ) / base_price;
                        if( ratio < 0.95 ) {
                            price_color = they ? c_light_green : c_light_red;
                        } else if( ratio > 1.05 ) {
                            price_color = they ? c_light_red : c_light_green;
                        }
                    }
                } else {
                    price_color = c_dark_gray;
                    price_str.clear();
                }

                const std::string namecell = align_left(
                                                 string_format( "%c %c %s", keychar, selection_mark,
                                                     trim_by_length( itname, name_w ).c_str() ),
                                                 name_w + 4 );
                trade_row r;
                r.selected = is_cursor || is_cat_sel;
                r.text_rml = cata_text_to_rml(
                                 colorize( namecell, color ) + " " + colorize( align_left( qty_str, qty_w ), color )
                                 + " " + colorize( align_left( weight_str, weight_w ), color ) + " "
                                 + colorize( align_left( vol_str, vol_w ), color ) + " "
                                 + colorize( align_right( price_str, price_w ), price_color ) );
                out.push_back( r );
                last_category = category_id;
                row++;
            }

            // Footer: filter indicator + page label.
            const auto& pane_filter = they ? them_filter : you_filter;
            const bool editing_here = filter_edit && ( filter_edit_theirs == they );
            std::string foot;
            if( editing_here || !pane_filter.empty() ) {
                const std::string& ftext =
                    editing_here && filter_popup ? filter_popup->text() : pane_filter;
                foot += colorize( _( "filter: " ) + ftext, editing_here ? c_white : c_magenta ) + "   ";
            }
            const auto page_starts = build_page_starts( list, filtered, entries_per_page );
            const auto total_pages = std::max( page_starts.size(), size_t{1} );
            const auto current_page = page_index_for_offset( page_starts, offset ) + 1;
            foot += colorize(
                        string_format(
                            _( "Page %d/%d" ), static_cast<int>( current_page ), static_cast<int>( total_pages ) ),
                        c_light_gray );
            foot_out = cata_text_to_rml( foot );
        };
        fill_pane( true );
        fill_pane( false );
        rml_data->them_focus = focus_them;
        rml_data->you_focus = !focus_them;

        // ── Info pane ─────────────────────────────────────────────────────
        rml_data->show_info = show_item_info;
        if( show_item_info ) {
            const auto& info_list = focus_them ? state.theirs : state.yours;
            const auto& info_filtered = focus_them ? them_filtered : you_filtered;
            const size_t info_cursor = focus_them ? them_cursor : you_cursor;
            if( !category_mode && !info_filtered.empty() && info_cursor < info_filtered.size() ) {
                const item& info_item = *info_list[info_filtered[info_cursor]].locs.front();
                rml_data->info_rml = cata_text_to_rml(
                                         colorize( info_item.type->description.translated(), c_light_gray ) );
            } else {
                rml_data->info_rml = cata_text_to_rml(
                                         colorize( _( "No item selected." ), c_dark_gray ) );
            }
        } else {
            rml_data->info_rml.clear();
        }

        rml_data->handle.DirtyAllVariables();
    };

    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( rml ) {
            sync_rml();
            return;
        }
    } );

    rml.open( trade_rmlui_enabled(), "trade", ctxt, [&]( Rml::DataModelConstructor & c ) {
        register_trade_rml_types( c );
        c.Bind( "title_rml", &rml_data->title_rml );
        c.Bind( "cost_rml", &rml_data->cost_rml );
        c.Bind( "category_rml", &rml_data->category_rml );
        c.Bind( "hints_rml", &rml_data->hints_rml );
        c.Bind( "them_name_rml", &rml_data->them_name_rml );
        c.Bind( "you_name_rml", &rml_data->you_name_rml );
        c.Bind( "them_stats_rml", &rml_data->them_stats_rml );
        c.Bind( "you_stats_rml", &rml_data->you_stats_rml );
        c.Bind( "them_header_rml", &rml_data->them_header_rml );
        c.Bind( "you_header_rml", &rml_data->you_header_rml );
        c.Bind( "them_foot_rml", &rml_data->them_foot_rml );
        c.Bind( "you_foot_rml", &rml_data->you_foot_rml );
        c.Bind( "them_focus", &rml_data->them_focus );
        c.Bind( "you_focus", &rml_data->you_focus );
        c.Bind( "them_rows", &rml_data->them_rows );
        c.Bind( "you_rows", &rml_data->you_rows );
        c.Bind( "show_info", &rml_data->show_info );
        c.Bind( "info_rml", &rml_data->info_rml );
        rml_data->handle = c.GetModelHandle();
    } );

    auto confirm = false;
    auto exit = false;
    auto pending_count = std::optional<int> {};
    category_mode = false;
    them_category_cursor = 0;
    you_category_cursor = 0;
    them_filtered = build_filtered_indices( state.theirs, them_filter );
    you_filtered = build_filtered_indices( state.yours, you_filter );
    ui_manager::redraw();

    struct clamp_cursor_options {
        const std::vector<item_pricing> &list;
        const std::vector<size_t> &filtered;
        size_t &cursor;
        size_t &offset;
    };
    const auto clamp_cursor_to_list = [&]( const clamp_cursor_options & opts ) -> void {
        if( opts.filtered.empty() )
    {
        opts.cursor = 0;
        opts.offset = 0;
        return;
    }
    opts.cursor = std::min( opts.cursor, opts.filtered.size() - 1 );
    if( entries_per_page == 0 )
    {
        opts.offset = 0;
        return;
    }
    const auto page_starts = build_page_starts( opts.list, opts.filtered, entries_per_page );
    const auto page_index = page_index_for_offset( page_starts, opts.cursor );
    opts.offset = page_starts[page_index];
};

const auto affects_npc_capacity = [&]( const item & it ) -> bool {
        return it.where() == item_location_type::character && &it != &np.primary_weapon();
    };
    const auto apply_trade_change = [&]( item_pricing & ip, int new_amount ) -> void {
        auto &owner_sells = focus_them ? ip.u_has : ip.npc_has;
        auto &owner_sells_charge = focus_them ? ip.u_charges : ip.npc_charges;
        const auto has_charges = ip.charges > 0;
        auto *current_amount = has_charges ? &owner_sells_charge :&owner_sells;
        const auto max_amount = has_charges ? ip.charges : std::max( ip.count, 1 );
        const auto clamped_amount = std::clamp( new_amount, 0, max_amount );
        if( clamped_amount == *current_amount ) { return; }
        const auto delta_amount = clamped_amount - *current_amount;
        *current_amount = clamped_amount;
        ip.selected = clamped_amount > 0;

        const auto signed_amount = focus_them ? delta_amount : -delta_amount;
        const auto delta_price = static_cast<int>( ip.price * signed_amount );
        if( !np.will_exchange_items_freely() ) { state.your_balance -= delta_price; }
        if( affects_npc_capacity( *ip.locs.front() ) )
        {
            state.volume_left += ip.vol * signed_amount;
            state.weight_left += ip.weight * signed_amount;
        }
    };
    const auto sync_category_cursor =
        [&]( const std::vector<item_pricing> &list, const std::vector<size_t> &filtered_indices,
             const std::vector<category_range> &category_ranges, size_t &category_cursor,
    size_t cursor ) -> void {
        if( category_ranges.empty() || filtered_indices.empty() ) { return; }
    const auto cursor_category =
    list[filtered_indices[cursor]].locs.front()->get_category().get_id();
    const auto match = std::ranges::find_if( category_ranges, [&]( const category_range & entry )
    {
        return entry.id == cursor_category;
    } );
    if( match != category_ranges.end() )
    {
        category_cursor = static_cast<size_t>( std::distance( category_ranges.begin(), match ) );
        }
    };
    const auto get_current_amount = [&]( const item_pricing & ip ) -> int {
        if( ip.charges > 0 ) { return focus_them ? ip.u_charges : ip.npc_charges; }
    return focus_them ? ip.u_has : ip.npc_has;
};
const auto get_max_amount = [&]( const item_pricing & ip ) -> int {
        return ip.charges > 0 ? ip.charges : std::max( ip.count, 1 );
    };
    struct balance_item_entry {
        size_t list_index = 0;
        int current_amount = 0;
        int max_amount = 0;
        int unit_balance_delta = 0;
    };
    struct balance_choice {
        int prev_balance = 0;
        int amount = 0;
    };
    using balance_map = std::unordered_map<int, balance_choice>;
    const auto calc_category_autobalance_plan =
        [&]( const std::vector<item_pricing> &list, const std::vector<size_t> &filtered_indices,
    const category_range & range ) -> std::unordered_map<size_t, int> {
        auto plan = std::unordered_map<size_t, int>{};
        auto entries = std::vector<balance_item_entry>{};
        std::ranges::for_each( std::views::iota( range.start, range.end ), [&]( size_t idx )
        {
            const auto list_index = filtered_indices[idx];
            const auto& ip = list[list_index];
            const auto current_amount = get_current_amount( ip );
            plan.emplace( list_index, current_amount );
            const auto unit_balance_delta = ( focus_them ? -1 : 1 ) * static_cast<int>( ip.price );
            const auto max_amount = get_max_amount( ip );
            if( unit_balance_delta == 0 || max_amount == 0 ) { return; }
            entries.push_back( balance_item_entry{
                .list_index = list_index,
                .current_amount = current_amount,
                .max_amount = max_amount,
                .unit_balance_delta = unit_balance_delta} );
        } );
        if( entries.empty() ) { return plan; }

        auto min_balance = state.your_balance;
        auto max_balance = state.your_balance;
        std::ranges::for_each( entries, [&]( const balance_item_entry & entry )
        {
            const auto delta_at_min = entry.unit_balance_delta * ( 0 - entry.current_amount );
            const auto delta_at_max =
                entry.unit_balance_delta * ( entry.max_amount - entry.current_amount );
            min_balance += std::min( delta_at_min, delta_at_max );
            max_balance += std::max( delta_at_min, delta_at_max );
        } );

        auto dp = balance_map{
            {state.your_balance, balance_choice{.prev_balance = state.your_balance, .amount = 0}}};
        auto decisions = std::vector<balance_map>{};
        decisions.reserve( entries.size() );

        std::ranges::for_each( entries, [&]( const balance_item_entry & entry )
        {
            auto next = balance_map{};
            auto item_decisions = balance_map{};
            std::ranges::for_each( dp, [&]( const auto & entry_pair ) {
                const auto prev_balance = entry_pair.first;
                const auto unit = entry.unit_balance_delta;
                const auto current_amount = entry.current_amount;
                const auto max_amount = entry.max_amount;
                const auto lower =
                    ( static_cast<double>( min_balance - prev_balance ) / static_cast<double>( unit ) )
                    + current_amount;
                const auto upper =
                    ( static_cast<double>( max_balance - prev_balance ) / static_cast<double>( unit ) )
                    + current_amount;
                const auto min_amount =
                    std::clamp( static_cast<int>( std::ceil( std::min( lower, upper ) ) ), 0, max_amount );
                const auto max_amount_bound =
                    std::clamp( static_cast<int>( std::floor( std::max( lower, upper ) ) ), 0, max_amount );
                if( min_amount > max_amount_bound ) { return; }
                std::ranges::
                for_each( std::views::iota( min_amount, max_amount_bound + 1 ), [&]( int amount ) {
                    const auto new_balance = prev_balance + unit * ( amount - current_amount );
                    const auto inserted =
                        next.emplace(
                            new_balance,
                            balance_choice{.prev_balance = prev_balance, .amount = amount} )
                        .second;
                    if( inserted ) {
                        item_decisions.emplace(
                            new_balance,
                            balance_choice{.prev_balance = prev_balance, .amount = amount} );
                    }
                } );
            } );
            dp = std::move( next );
            decisions.push_back( std::move( item_decisions ) );
        } );

        if( dp.empty() ) { return plan; }

        auto best_balance = std::optional<int>{};
        std::ranges::for_each( dp, [&]( const auto & entry_pair )
        {
            const auto balance = entry_pair.first;
            if( balance >= 0 ) {
                if( !best_balance || balance < *best_balance ) { best_balance = balance; }
            }
        } );
        if( !best_balance )
        {
            std::ranges::for_each( dp, [&]( const auto & entry_pair ) {
                const auto balance = entry_pair.first;
                if( !best_balance || balance > *best_balance ) { best_balance = balance; }
            } );
        }
        if( !best_balance ) { return plan; }

        auto balance = *best_balance;
        for( auto i = static_cast<int>( entries.size() ); i-- > 0; )
        {
            const auto index = static_cast<size_t>( i );
            const auto& entry = entries[index];
            auto& item_decisions = decisions[index];
            const auto found = item_decisions.find( balance );
            if( found == item_decisions.end() ) { break; }
            plan[entry.list_index] = found->second.amount;
            balance = found->second.prev_balance;
        }
        return plan;
    };
    const auto calc_autobalance_amount = [&]( const item_pricing & ip ) -> int {
        const auto unit_balance_delta = ( focus_them ? -1 : 1 ) * static_cast<int>( ip.price );
        if( unit_balance_delta == 0 ) { return get_current_amount( ip ); }
        const auto current_amount = get_current_amount( ip );
        const auto max_amount = get_max_amount( ip );
        const auto ideal =
        static_cast<double>( current_amount )
        - static_cast<double>( state.your_balance ) / static_cast<double>( unit_balance_delta );
        const auto clamp_amount = [&]( int amount ) -> int {
            return std::clamp( amount, 0, max_amount );
        };
        const auto candidates = std::vector<int>{
            clamp_amount( 0 ), clamp_amount( max_amount ),
            clamp_amount( static_cast<int>( std::floor( ideal ) ) ),
            clamp_amount( static_cast<int>( std::ceil( ideal ) ) )
        };
        const auto result_balance = [&]( int amount ) -> int {
            return state.your_balance + unit_balance_delta * ( amount - current_amount );
        };
        const auto non_debt_candidates =
        candidates | std::views::filter( [&]( int amount ) { return result_balance( amount ) >= 0; } )
        | std::ranges::to<std::vector>();
        if( !non_debt_candidates.empty() )
        {
            return *std::ranges::min_element( non_debt_candidates, [&]( int lhs, int rhs ) {
                const auto lhs_balance = result_balance( lhs );
                const auto rhs_balance = result_balance( rhs );
                const auto lhs_abs = std::abs( lhs_balance );
                const auto rhs_abs = std::abs( rhs_balance );
                if( lhs_abs != rhs_abs ) { return lhs_abs < rhs_abs; }
                return std::abs( lhs - current_amount ) < std::abs( rhs - current_amount );
            } );
        }
        return *std::ranges::max_element( candidates, [&]( int lhs, int rhs )
        {
            const auto lhs_balance = result_balance( lhs );
            const auto rhs_balance = result_balance( rhs );
            if( lhs_balance != rhs_balance ) { return lhs_balance < rhs_balance; }
            return std::abs( lhs - current_amount ) > std::abs( rhs - current_amount );
        } );
    };
    while( !exit ) {
        auto& target_list = focus_them ? state.theirs : state.yours;
        auto& filtered = focus_them ? them_filtered : you_filtered;
        auto& offset = focus_them ? them_off : you_off;
        auto& cursor = focus_them ? them_cursor : you_cursor;
        auto& category_cursor = focus_them ? them_category_cursor : you_category_cursor;
        const auto item_hotkeys = ctxt.get_available_single_char_hotkeys( "abcdefghijklmnopqrstuvwxy"
                                                                          "zABCDEFGHIJKLMNOPQRSTUVWX"
                                                                          "YZ" );
        clamp_cursor_to_list( clamp_cursor_options{
            .list = target_list, .filtered = filtered, .cursor = cursor, .offset = offset} );
        const auto category_ranges = build_category_ranges( target_list, filtered );
        const auto page_starts = build_page_starts( target_list, filtered, entries_per_page );
        if( category_cursor >= category_ranges.size() ) {
            category_cursor = category_ranges.empty() ? 0 : category_ranges.size() - 1;
        }
        ui_manager::redraw();

        const auto action = ctxt.handle_input();
        if( action == "SWITCH_LISTS" ) {
            focus_them = !focus_them;
            if( category_mode ) {
                auto& new_target_list = focus_them ? state.theirs : state.yours;
                auto& new_filtered = focus_them ? them_filtered : you_filtered;
                auto& new_offset = focus_them ? them_off : you_off;
                auto& new_cursor = focus_them ? them_cursor : you_cursor;
                auto& new_category_cursor = focus_them ? them_category_cursor : you_category_cursor;
                const auto new_category_ranges =
                    build_category_ranges( new_target_list, new_filtered );
                if( !new_category_ranges.empty() ) {
                    if( new_category_cursor >= new_category_ranges.size() ) {
                        new_category_cursor = new_category_ranges.size() - 1;
                    }
                    new_cursor = new_category_ranges[new_category_cursor].start;
                    clamp_cursor_to_list( clamp_cursor_options{
                        .list = new_target_list,
                        .filtered = new_filtered,
                        .cursor = new_cursor,
                        .offset = new_offset} );
                }
            }
        } else if( action == "UP" ) {
            if( category_mode ) {
                if( !category_ranges.empty() ) {
                    category_cursor =
                        category_cursor > 0 ? category_cursor - 1 : category_ranges.size() - 1;
                    cursor = category_ranges[category_cursor].start;
                }
            } else if( !filtered.empty() ) {
                cursor = cursor > 0 ? cursor - 1 : filtered.size() - 1;
            }
        } else if( action == "DOWN" ) {
            if( category_mode ) {
                if( !category_ranges.empty() ) {
                    category_cursor =
                        ( category_cursor + 1 ) < category_ranges.size() ? category_cursor + 1 : 0;
                    cursor = category_ranges[category_cursor].start;
                }
            } else if( !filtered.empty() ) {
                cursor = ( cursor + 1 ) < filtered.size() ? cursor + 1 : 0;
            }
        } else if( action == "RIGHT" || action == "LEFT" ) {
            if( category_mode ) {
                if( !category_ranges.empty() ) {
                    const auto& range = category_ranges[category_cursor];
                    const auto apply_amount = [&]( item_pricing & ip ) -> void {
                        if( action == "RIGHT" )
                        {
                            const auto max_amount =
                            ip.charges > 0 ? ip.charges : std::max( ip.count, 1 );
                            apply_trade_change( ip, max_amount );
                        } else
                        {
                            apply_trade_change( ip, 0 );
                        }
                    };
                    std::ranges::for_each( std::views::iota( range.start, range.end ), [&]( size_t idx ) {
                        apply_amount( target_list[filtered[idx]] );
                    } );
                }
            } else if( !filtered.empty() ) {
                auto& ip = target_list[filtered[cursor]];
                if( action == "RIGHT" ) {
                    const auto max_amount = ip.charges > 0 ? ip.charges : std::max( ip.count, 1 );
                    const auto requested_amount = pending_count.value_or( max_amount );
                    apply_trade_change( ip, requested_amount );
                } else {
                    apply_trade_change( ip, 0 );
                }
            }
            pending_count.reset();
        } else if( action == "AUTOBALANCE" ) {
            if( filtered.empty() ) { continue; }
            if( category_mode ) {
                if( category_ranges.empty() ) { continue; }
                const auto& range = category_ranges[category_cursor];
                const auto plan = calc_category_autobalance_plan( target_list, filtered, range );
                std::ranges::for_each( std::views::iota( range.start, range.end ), [&]( size_t idx ) {
                    const auto list_index = filtered[idx];
                    auto& ip = target_list[list_index];
                    const auto plan_it = plan.find( list_index );
                    if( plan_it != plan.end() ) { apply_trade_change( ip, plan_it->second ); }
                } );
            } else {
                auto& ip = target_list[filtered[cursor]];
                const auto best_amount = calc_autobalance_amount( ip );
                apply_trade_change( ip, best_amount );
            }
            pending_count.reset();
        } else if( action == "TOGGLE_ITEM_INFO" ) {
            show_item_info = !show_item_info;
            ui.mark_resize();
        } else if( action == "CATEGORY_SELECTION" ) {
            category_mode = !category_mode;
            if( category_mode && !category_ranges.empty() && !filtered.empty() ) {
                sync_category_cursor(
                    target_list, filtered, category_ranges, category_cursor, cursor );
                cursor = category_ranges[category_cursor].start;
                clamp_cursor_to_list( clamp_cursor_options{
                    .list = target_list, .filtered = filtered, .cursor = cursor, .offset = offset} );
            }
        } else if( action == "FILTER" ) {
            auto& active_filter = focus_them ? them_filter : you_filter;
            const auto original_filter = active_filter;
            filter_edit = true;
            filter_edit_theirs = focus_them;
            const auto& filter_win = focus_them ? w_them : w_you;
            filter_popup = std::make_unique<string_input_popup>();
            const auto filter_prefix = _( "< [" );
            const auto filter_middle = _( "] filter" );
            const auto filter_suffix = _( " >" );
            const auto filter_input_sep = _( ": " );
            const auto filter_input_x =
                1 + utf8_width( filter_prefix ) + 1 + utf8_width( filter_middle )
                + utf8_width( filter_input_sep );
            const auto filter_input_end =
                std::max( getmaxx( filter_win ) - 2 - utf8_width( filter_suffix ), filter_input_x );
            const auto filter_input_y = getmaxy( filter_win ) - 1;
            filter_popup->max_length( 256 )
            .text( active_filter )
            .identifier( "npc_trade" )
            .window( filter_win, point( filter_input_x, filter_input_y ), filter_input_end );
            auto sentry = ime_sentry{};
            do {
                ui_manager::redraw();
                filter_popup->query_string( false );
            } while( !filter_popup->canceled() && !filter_popup->confirmed() );
            const auto filter_confirmed = filter_popup->confirmed();
            const auto filter_text = std::string( filter_popup->text() );
            filter_edit = false;
            filter_popup = nullptr;
            if( filter_confirmed ) {
                active_filter = filter_text;
                auto& active_list = focus_them ? state.theirs : state.yours;
                auto& active_filtered = focus_them ? them_filtered : you_filtered;
                active_filtered = build_filtered_indices( active_list, active_filter );
                clamp_cursor_to_list( clamp_cursor_options{
                    .list = active_list,
                    .filtered = active_filtered,
                    .cursor = cursor,
                    .offset = offset} );
            } else {
                active_filter = original_filter;
            }
        } else if( action == "RESET_FILTER" ) {
            auto& active_filter = focus_them ? them_filter : you_filter;
            active_filter.clear();
            auto& active_list = focus_them ? state.theirs : state.yours;
            auto& active_filtered = focus_them ? them_filtered : you_filtered;
            active_filtered = build_filtered_indices( active_list, active_filter );
            clamp_cursor_to_list( clamp_cursor_options{
                .list = active_list,
                .filtered = active_filtered,
                .cursor = cursor,
                .offset = offset} );
        } else if( action == "PAGE_UP" ) {
            const auto page_index = page_index_for_offset( page_starts, offset );
            if( page_index > 0 ) {
                offset = page_starts[page_index - 1];
            } else {
                offset = page_starts.front();
            }
            if( !filtered.empty() ) {
                cursor = offset;
                if( category_mode && !category_ranges.empty() ) {
                    sync_category_cursor(
                        target_list, filtered, category_ranges, category_cursor, cursor );
                }
            }
        } else if( action == "PAGE_DOWN" ) {
            const auto page_index = page_index_for_offset( page_starts, offset );
            if( ( page_index + 1 ) < page_starts.size() ) {
                offset = page_starts[page_index + 1];
            } else {
                offset = page_starts.back();
            }
            if( !filtered.empty() ) {
                cursor = offset;
                if( category_mode && !category_ranges.empty() ) {
                    sync_category_cursor(
                        target_list, filtered, category_ranges, category_cursor, cursor );
                }
            }
        } else if( action == "EXAMINE" ) {
            if( category_mode ) { continue; }
            const auto result = show_item_data( filtered.empty() ? 0 : filtered[cursor], focus_them );
            if( !filtered.empty() ) {
                if( result == info_popup_result::move_up ) {
                    cursor = cursor > 0 ? cursor - 1 : filtered.size() - 1;
                } else if( result == info_popup_result::move_down ) {
                    cursor = ( cursor + 1 ) < filtered.size() ? cursor + 1 : 0;
                }
            }
        } else if( action == "CONFIRM" ) {
            if( !npc_trading::npc_will_accept_trade( state, np ) ) {

                if( np.max_credit_extended() == 0 ) {
                    popup( _( "You'll need to offer me more than that." ) );
                } else {
                    popup( _( "Sorry, I'm only willing to extend you %s in credit." ),
                           format_money( np.max_credit_extended() ) );
                }
            } else if( state.volume_left < 0_ml || state.weight_left < 0_gram ) {
                // Make sure NPC doesn't go over allowed volume
                popup( _( "%s can't carry all that." ), np.name );
            } else if( npc_trading::calc_npc_owes_you( state, np ) < state.your_balance ) {
                // NPC is happy with the trade, but isn't willing to remember the whole debt.
                const auto trade_ok =
                    query_yn( _( "I'm never going to be able to pay you back for all that.  The most "
                             "I'm willing to owe you is %s.\n\nContinue with trade?" ),
                              format_money( np.max_willing_to_owe() ) );

                if( trade_ok ) {
                    exit = true;
                    confirm = true;
                }
            } else {
                if( query_yn( _( "Looks like a deal!  Accept this trade?" ) ) ) {
                    exit = true;
                    confirm = true;
                }
            }
        } else if( action == "QUIT" ) {
            exit = true;
            confirm = false;
        } else if( action == "ANY_INPUT" ) {
            const auto evt = ctxt.get_raw_input();
            if( evt.type != input_event_t::keyboard || evt.sequence.empty() ) { continue; }
            auto ch = evt.get_first_input();
            if( ch >= '0' && ch <= '9' ) {
                const auto digit = static_cast<int>( ch - '0' );
                if( !pending_count ) { pending_count.emplace( 0 ); }
                *pending_count = *pending_count * 10 + digit;
                if( *pending_count <= 0 ) { pending_count.reset(); }
                continue;
            }
            const auto hotkey_pos = item_hotkeys.find( static_cast<char>( ch ) );
            if( hotkey_pos == std::string::npos ) { continue; }
            auto ch_index = static_cast<size_t>( hotkey_pos );
            ch_index += offset;
            if( ch_index < filtered.size() ) {
                cursor = ch_index;
                clamp_cursor_to_list( clamp_cursor_options{
                    .list = target_list, .filtered = filtered, .cursor = cursor, .offset = offset} );
                if( category_mode && !category_ranges.empty() ) {
                    const auto cursor_category =
                        target_list[filtered[cursor]].locs.front()->get_category().get_id();
                    const auto match =
                    std::ranges::find_if( category_ranges, [&]( const category_range & entry ) {
                        return entry.id == cursor_category;
                    } );
                    if( match != category_ranges.end() ) {
                        category_cursor = static_cast<size_t>(
                                              std::distance( category_ranges.begin(), match ) );
                    }
                }
                auto& ip = target_list[filtered[ch_index]];
                auto change_amount = 1;
                auto& owner_sells = focus_them ? ip.u_has : ip.npc_has;
                auto& owner_sells_charge = focus_them ? ip.u_charges : ip.npc_charges;

                const auto calc_amount_hint = [&]() -> int {
                    if( ip.price > 0 )
                {
                    if( focus_them && state.your_balance > 0 ) {
                            return state.your_balance / ip.price;
                        } else if( !focus_them && state.your_balance < 0 ) {
                            const auto amt = state.your_balance / ip.price;
                            const auto rem = ( std::fmod( state.your_balance, ip.price ) == 0 ) ? 0 : 1;
                            return amt - rem;
                        }
                    }
                    return 0;
                };

                if( ip.selected ) {
                    if( owner_sells_charge > 0 ) {
                        change_amount = owner_sells_charge;
                        owner_sells_charge = 0;
                    } else if( owner_sells > 0 ) {
                        change_amount = owner_sells;
                        owner_sells = 0;
                    }
                } else if( ip.charges > 0 ) {
                    const auto hint = calc_amount_hint();
                    change_amount = get_var_trade( *ip.locs.front(), ip.charges, hint );
                    if( change_amount < 1 ) { continue; }
                    owner_sells_charge = change_amount;
                } else {
                    if( ip.count > 1 ) {
                        const auto hint = calc_amount_hint();
                        change_amount = get_var_trade( *ip.locs.front(), ip.count, hint );
                        if( change_amount < 1 ) { continue; }
                    }
                    owner_sells = change_amount;
                }
                ip.selected = !ip.selected;
                if( ip.selected != focus_them ) { change_amount *= -1; }
                const auto delta_price = static_cast<int>( ip.price * change_amount );
                if( !np.will_exchange_items_freely() ) { state.your_balance -= delta_price; }
                if( affects_npc_capacity( *ip.locs.front() ) ) {
                    state.volume_left += ip.vol * change_amount;
                    state.weight_left += ip.weight * change_amount;
                }
            }
        }
    }

    return confirm;
}
