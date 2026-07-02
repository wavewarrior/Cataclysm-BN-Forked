#include "ui.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <iterator>
#include <memory>

// Third-party retained-mode UI. Included before debug.h so its headers can't
// collide with the DebugLog macro.
#include "avatar.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "debug.h"
#include "game.h"
#include "ime.h"
#include "input.h"
#include "lighting/rmlui_layer.h"
#include "output.h"
#include "path_info.h"
#include "player.h"
#include "string_input_popup.h"
#include "string_utils.h"
#include "ui_manager.h"

#include <RmlUi/Core.h>

catacurses::window new_centered_win( int nlines, int ncols )
{
    int height = std::min( nlines, TERMY );
    int width = std::min( ncols, TERMX );
    point pos( ( TERMX - width ) / 2, ( TERMY - height ) / 2 );
    return catacurses::newwin( height, width, pos );
}

// ---- RmlUi uilist render session -------------------------------------------
// One row in the bound visible window. `text` is plain (stripped of color tags)
// for dirty-comparison; `text_rml` carries <span style="color:…"> markup for
// data-rml rendering.
struct uilist_rml_row {
    Rml::String text;
    Rml::String text_rml;
    Rml::String hotkey;
    Rml::String col;
    bool selected = false;
    bool enabled = true;
};

// Holds the document + data-model handle + the storage the model is bound to.
// Defined here (before ~uilist) so uilist's unique_ptr<uilist_rml_session>
// member has a complete type for its deleter. Owned by uilist::rml_session.
class uilist_rml_session
{
    public:
        // Bound model storage (pointers handed to RmlUi must stay valid for the
        // session's whole life — they live here, not on a stack frame).
        Rml::String title;
        bool has_title = false;
        Rml::Vector<Rml::String> header;
        Rml::Vector<uilist_rml_row> rows;
        Rml::String desc;
        Rml::String desc_rml;
        bool has_desc = false;
        // Raw (still color-tagged) source the desc/desc_rml were built from, kept
        // so rml_sync only re-runs the expensive cata_text_to_rml when it changes.
        Rml::String desc_src;
        Rml::String filter;
        bool has_filter = false;
        bool filter_active = false;
        // Per-menu look variant (mirrors uilist::menu_style), applied as a class
        // on .uilist-panel to drive width/alignment + single-vs-two-column.
        Rml::String menu_style;
        // Whether the side region (desc + #callback) has content worth showing.
        // Gates the side div so empty stacked menus draw no stray divider; true
        // when there's a description OR the style is a panel style (info/grid).
        bool has_side = false;

        Rml::DataModelHandle handle;
        Rml::ElementDocument *doc = nullptr;
        // The window of fentries currently bound into `rows`: [win_top, win_top+win_len).
        int win_top = 0;
        int win_len = 0;
        // First fentry at the top of the viewport — the single source of truth for
        // scroll position (virtual scrolling). Keyboard nav updates it; mouse/idle
        // frames read it back from the list's scroll offset. See rml_sync.
        int scroll_row = 0;
};

bool &uilist_rmlui_enabled()
{
    // Default ON (Tier-10 rip-out track A): uilist is the most-proven Tier-0 screen
    // (eyeball-confirmed) and the only gameplay ImGui consumer; routing it through RmlUi
    // by default is the precondition for deleting the dormant ImGui layer. Falls back to
    // curses only when RmlUi isn't ready (early init). A/B via the F4 panel.
    static bool enabled = true;
    return enabled;
}

bool &query_popup_rmlui_enabled()
{
    // Default OFF — see ui.h. Opt in via the F4 panel.
    static bool enabled = true;
    return enabled;
}

bool &string_input_rmlui_enabled()
{
    // Default OFF — see ui.h. Opt in via the F4 panel.
    static bool enabled = true;
    return enabled;
}

// Rml colour/text helpers (rml_escape, nc_color_to_hex, cata_text_to_rml) were
// promoted to rml_util.{h,cpp} so every migrated screen + the world-text layer
// share one path. Included via ui.h.

// Row struct + array type registration is context-global and persists for the
// context's life, so it's done once. The model NAME "uilist" allows a single
// RmlUi uilist at a time; a nested one finds the name taken and falls back.
bool g_rml_uilist_types_registered = false;
bool g_rml_uilist_model_active = false;

void register_uilist_rml_types( Rml::DataModelConstructor& c )
{
    if( g_rml_uilist_types_registered ) { return; }
    Rml::StructHandle<uilist_rml_row> rh = c.RegisterStruct<uilist_rml_row>();
    rh.RegisterMember( "text", &uilist_rml_row::text );
    rh.RegisterMember( "text_rml", &uilist_rml_row::text_rml );
    rh.RegisterMember( "hotkey", &uilist_rml_row::hotkey );
    rh.RegisterMember( "col", &uilist_rml_row::col );
    rh.RegisterMember( "selected", &uilist_rml_row::selected );
    rh.RegisterMember( "enabled", &uilist_rml_row::enabled );
    c.RegisterArray<Rml::Vector<uilist_rml_row>>();
    // The header is a plain string array (data-for over `header`); its array
    // type needs registering too, even though String itself is a built-in.
    c.RegisterArray<Rml::Vector<Rml::String>>();
    g_rml_uilist_types_registered = true;
}
/**
 * \defgroup UI "The UI Menu."
 * @{
 */

uilist::size_scalar &uilist::size_scalar::operator=( auto_assign )
{
    fun = nullptr;
    return *this;
}

uilist::size_scalar &uilist::size_scalar::operator=( const int val )
{
    fun = [val]() -> int { return val; };
    return *this;
}

uilist::size_scalar &uilist::size_scalar::operator=( const std::function<int()> &fun )
{
    this->fun = fun;
    return *this;
}

uilist::pos_scalar &uilist::pos_scalar::operator=( auto_assign )
{
    fun = nullptr;
    return *this;
}

uilist::pos_scalar &uilist::pos_scalar::operator=( const int val )
{
    fun = [val]( int ) -> int { return val; };
    return *this;
}

uilist::pos_scalar &uilist::pos_scalar::operator=( const std::function<int( int )> &fun )
{
    this->fun = fun;
    return *this;
}

uilist::uilist() { init(); }

uilist::uilist( const std::string& hotkeys_override )
{
    init();
    if( !hotkeys_override.empty() ) { hotkeys = hotkeys_override; }
}

uilist::uilist( const std::string& msg, const std::vector<uilist_entry> &opts )
{
    init();
    text = msg;
    entries = opts;
    query();
}

uilist::uilist( const std::string& msg, const std::vector<std::string> &opts )
{
    init();
    text = msg;
    for( const std::string& opt : opts ) { entries.emplace_back( opt ); }
    query();
}

uilist::uilist( const std::string& msg, std::initializer_list<const char* const> opts )
{
    init();
    text = msg;
    for( const char * const opt : opts ) { entries.emplace_back( opt ); }
    query();
}

uilist::~uilist()
{
    shared_ptr_fast<ui_adaptor> current_ui = ui.lock();
    if( current_ui ) { current_ui->reset(); }
}

void uilist::color_error( const bool report )
{
    if( report ) {
        _color_error = report_color_error::yes;
    } else {
        _color_error = report_color_error::no;
    }
}

/*
 * Enables oneshot construction -> running -> exit
 */
uilist::operator int() const { return ret; }

/**
 * Sane defaults on initialization
 */
void uilist::init()
{
    if( test_mode ) {
        debugmsg( "uilist must not be used in test mode" );
        return;
    }
    w_x_setup = pos_scalar::auto_assign{};
    w_y_setup = pos_scalar::auto_assign{};
    w_width_setup = size_scalar::auto_assign{};
    w_height_setup = size_scalar::auto_assign{};
    w_x = 0;
    w_y = 0;
    w_width = 0;
    w_height = 0;
    ret = UILIST_WAIT_INPUT;
    text.clear();                // header text, after (maybe) folding, populates:
    textformatted.clear();       // folded to textwidth
    textwidth = MENU_AUTOASSIGN; // if unset, folds according to w_width
    title.clear(); // Makes use of the top border, no folding, sets min width if w_width is auto
    keypress = 0;  // last keypress from (int)getch()
    window = catacurses::window(); // our window
    keymap.clear();                // keymap[int] == index, for entries[index]
    selected = 0;                  // current highlight, for entries[index]
    entries.clear(); // uilist_entry(int returnval, bool enabled, int keycode, std::string text, ...
    // TODO: submenu stuff)
    started = false; // set to true when width and key calculations are done, and window is
    // generated.
    pad_left_setup = 0;
    pad_right_setup = 0;
    pad_left = 0;         // make a blank space to the left
    pad_right = 0;        // or right
    desc_enabled = false; // don't show option description by default
    desc_lines_hint = 6;  // default number of lines for description
    desc_lines = 6;
    footer_text.clear();          // takes precedence over per-entry descriptions.
    border_color = c_magenta;     // border color
    text_color = c_light_gray;    // text color
    title_color = c_green;        // title color
    hilight_color = h_white;      // highlight for up/down selection bar
    hotkey_color = c_light_green; // hotkey text to the right of menu entry's text
    disabled_color = c_dark_gray; // disabled menu entry
    allow_disabled = false;       // disallow selecting disabled options
    allow_anykey = false;         // do not return on unbound keys
    allow_cancel = true;          // allow canceling with "QUIT" action
    allow_additional = false;     // do not return on unhandled additional actions
    hilight_disabled = false;     // if false, hitting 'down' onto a disabled entry will advance
    // downward to the first enabled entry
    vshift = 0;                   // scrolling menu offset
    vmax = 0;                     // max entries area rows
    callback = nullptr;           // * uilist_callback
    filter.clear();               // filter string. If "", show everything
    fentries.clear(); // fentries is the actual display after filtering, and maps displayed entry
    // number to actual entry number
    fselected = 0;    // selected = fentries[fselected]
    filtering = true; // enable list display filtering via '/' or '.'
    filtering_igncase = true; // ignore case when filtering
    max_entry_len = 0;
    max_column_len = 0; // for calculating space for second column

    _color_error = report_color_error::yes;
    hotkeys = DEFAULT_HOTKEYS;
    input_category = "UILIST";
    additional_actions.clear();
}

input_context uilist::create_main_input_context() const
{
    input_context ctxt( input_category );
    ctxt.register_updown();
    ctxt.register_action( "PAGE_UP", to_translation( "Fast scroll up" ) );
    ctxt.register_action( "PAGE_DOWN", to_translation( "Fast scroll down" ) );
    ctxt.register_action( "HOME", to_translation( "Go to first entry" ) );
    ctxt.register_action( "END", to_translation( "Go to last entry" ) );
    ctxt.register_action( "SCROLL_UP" );
    ctxt.register_action( "SCROLL_DOWN" );
    if( allow_cancel ) { ctxt.register_action( "QUIT" ); }
    ctxt.register_action( "SELECT" );
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "FILTER" );
    ctxt.register_action( "MOUSE_MOVE" );
    ctxt.register_action( "ANY_INPUT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
for( const auto& additional_action : additional_actions ) {
    ctxt.register_action( additional_action.first, additional_action.second );
    }
    return ctxt;
}

input_context uilist::create_filter_input_context() const
{
    input_context ctxt( input_category );
    // string input popup actions
    ctxt.register_action( "TEXT.LEFT" );
    ctxt.register_action( "TEXT.RIGHT" );
    ctxt.register_action( "TEXT.QUIT" );
    ctxt.register_action( "TEXT.CONFIRM" );
    ctxt.register_action( "TEXT.CLEAR" );
    ctxt.register_action( "TEXT.BACKSPACE" );
    ctxt.register_action( "TEXT.HOME" );
    ctxt.register_action( "TEXT.END" );
    ctxt.register_action( "TEXT.DELETE" );
    ctxt.register_action( "TEXT.PASTE" );
    ctxt.register_action( "TEXT.INPUT_FROM_FILE" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "ANY_INPUT" );
    // uilist actions
    ctxt.register_updown();
    ctxt.register_action( "PAGE_UP", to_translation( "Fast scroll up" ) );
    ctxt.register_action( "PAGE_DOWN", to_translation( "Fast scroll down" ) );
    ctxt.register_action( "HOME", to_translation( "Go to first entry" ) );
    ctxt.register_action( "END", to_translation( "Go to last entry" ) );
    ctxt.register_action( "SCROLL_UP" );
    ctxt.register_action( "SCROLL_DOWN" );
    return ctxt;
}

void uilist::filterlist()
{
    bool filtering = ( this->filtering && !filter.empty() );

    // TODO: && is_all_lc( filter )
    bool ignore_case = filtering_igncase;
    fentries.clear();
    fselected = -1;

    int f = 0;
    int num_entries = entries.size();
    // check if string begin by " and finish by ". If that's the case, we only return a result if it
    // matches it exactly
    bool exact_match_only = !filter.empty() && filter.front() == '\"' && filter.back() == '\"';
    if( exact_match_only ) {
        filter.erase( std::remove( filter.begin(), filter.end(), '\"' ), filter.end() );
    }

    for( int i = 0; i < num_entries; i++ ) {
        if( filtering ) {
            if( exact_match_only ) {
                if( !( entries[i].txt == filter ) ) { continue; }
            } else if( ignore_case ) {
                if( !lcmatch( entries[i].txt, filter ) ) { continue; }
            } else if( entries[i].txt.find( filter ) == std::string::npos ) {
                continue;
            }
        }
        fentries.push_back( i );
        if( i == selected && ( hilight_disabled || entries[i].enabled ) ) {
            fselected = f;
        } else if( i > selected && fselected == -1 && ( hilight_disabled || entries[i].enabled ) ) {
            // Past the previously selected entry, which has been filtered out,
            // choose another nearby entry instead.
            fselected = f;
        }
        f++;
    }
    if( fselected == -1 ) {
        fselected = 0;
        vshift = 0;
        if( fentries.empty() ) {
            selected = -1;
        } else {
            selected = fentries[0];
        }
    } else if( fselected < static_cast<int>( fentries.size() ) ) {
        selected = fentries[fselected];
    } else {
        fselected = selected = -1;
    }
    // scroll to top of screen if all remaining entries fit the screen.
    if( static_cast<int>( fentries.size() ) <= vmax ) { vshift = 0; }
    if( callback != nullptr ) { callback->select( this ); }
}

void uilist::filterpredicate( const std::function<bool( int )> &predicate )
{
    fentries.clear();
    fselected = -1;

    int num_entries = entries.size();
    for( int i = 0; i < num_entries; i++ ) {
        if( predicate( i ) ) { fentries.push_back( i ); }
    }
    if( !fentries.empty() ) {
        selected = fentries[0];
        fselected = 0;
        vshift = 0;
    }
}

void uilist::clear_filter()
{
    filter.clear();
    filterlist();
}

void uilist::set_filter( const std::string& fstr )
{
    filter = fstr;
    filterlist();
}

void uilist::inputfilter()
{
    input_context ctxt = create_filter_input_context();
    filter_popup = std::make_unique<string_input_popup>();
    filter_popup->context( ctxt ).text( filter ).max_length( 256 ).window(
        window, point( 4, w_height - 1 ), w_width - 4 );
    ime_sentry sentry;
    if( rml_session ) { rml_session->filter_active = true; }
    do {
        ui_manager::redraw();
        filter = filter_popup->query_string( false );
        if( !filter_popup->canceled() ) {
            const std::string action = ctxt.input_to_action( ctxt.get_raw_input() );
            if( filter_popup->handled() || !scrollby( scroll_amount_from_action( action ) ) ) {
                filterlist();
            }
        }
    } while( !filter_popup->confirmed() && !filter_popup->canceled() );

    if( rml_session ) { rml_session->filter_active = false; }

    if( filter_popup->canceled() ) { filterlist(); }

    filter_popup.reset();
}

bool uilist::set_selected( int sel )
{
    if( sel < 0 || sel >= static_cast<int>( entries.size() ) ) {
        // Shortcut
        return false;
    }

    for( size_t i = 0; i < fentries.size(); i++ ) {
        if( fentries[i] == sel ) {
            selected = sel;
            fselected = static_cast<int>( i );
            return true;
        }
    }

    return false;
}

/**
 * Find the minimum width between max( min_width, 1 ) and
 * max( max_width, min_width, 1 ) to fold the string to no more than max_lines,
 * or no more than the minimum number of lines possible, assuming that
 * foldstring( width ).size() decreases monotonously with width.
 **/
static int find_minimum_fold_width(
    const std::string& str, int max_lines, int min_width, int max_width )
{
    if( str.empty() ) { return std::max( min_width, 1 ); }
    min_width = std::max( min_width, 1 );
    // max_width could be further limited by the string width, but utf8_width is
    // not handling linebreaks properly.

    if( min_width < max_width ) {
        // If with max_width the string still folds to more than max_lines, find the
        // minimum width that folds the string to such number of lines instead.
        max_lines = std::max<int>( max_lines, foldstring( str, max_width ).size() );
        while( min_width < max_width ) {
            int width = ( min_width + max_width ) / 2;
            // width may equal min_width, but will always be less than max_width.
            int lines = foldstring( str, width ).size();
            // If the current width folds the string to no more than max_lines
            if( lines <= max_lines ) {
                // The minimum width is between min_width and width.
                max_width = width;
            } else {
                // The minimum width is between width + 1 and max_width.
                min_width = width + 1;
            }
            // The new interval will always be smaller than the previous one,
            // so the loop is guaranteed to end.
        }
    }
    return min_width;
}

/**
 * Calculate sizes, populate arrays, initialize window
 */
void uilist::setup()
{
    bool w_auto = !w_width_setup.fun;

    // Space for a line between text and entries. Only needed if there is actually text.
    const int text_separator_line = text.empty() ? 0 : 1;
    if( w_auto ) {
        w_width = 4;
        if( !title.empty() ) { w_width = utf8_width( remove_color_tags( title ) ) + 5; }
    } else {
        w_width = w_width_setup.fun();
    }
    const int max_desc_width = w_auto ? TERMX - 4 : w_width - 4;

    bool h_auto = !w_height_setup.fun;
    if( h_auto ) {
        w_height = 4;
    } else {
        w_height = w_height_setup.fun();
    }

    max_entry_len = 0;
    max_column_len = 0;
    desc_lines = desc_lines_hint;
    std::vector<int> autoassign;
    pad_left = pad_left_setup.fun ? pad_left_setup.fun() : 0;
    pad_right = pad_right_setup.fun ? pad_right_setup.fun() : 0;
    int pad = pad_left + pad_right + 2;
    int descwidth_final = 0; // for description width guard
    for( size_t i = 0; i < entries.size(); i++ ) {
        int txtwidth = utf8_width( remove_color_tags( entries[i].txt ) );
        int ctxtwidth = utf8_width( remove_color_tags( entries[i].ctxt ) );
        if( txtwidth > max_entry_len ) { max_entry_len = txtwidth; }
        if( ctxtwidth > max_column_len ) { max_column_len = ctxtwidth; }
        int clen = ( ctxtwidth > 0 ) ? ctxtwidth + 2 : 0;
        if( entries[i].enabled ) {
            if( entries[i].hotkey > 0 ) {
                keymap[entries[i].hotkey] = i;
            } else if( entries[i].hotkey == -1 && i < 100 ) {
                autoassign.push_back( i );
            }
            if( entries[i].retval == -1 ) { entries[i].retval = i; }
            if( w_auto && w_width < txtwidth + pad + 4 + clen ) {
                w_width = txtwidth + pad + 4 + clen;
            }
        } else {
            if( w_auto && w_width < txtwidth + pad + 4 + clen ) {
                // TODO: or +5 if header
                w_width = txtwidth + pad + 4 + clen;
            }
        }
        if( desc_enabled ) {
            const int min_desc_width =
                std::min( max_desc_width, std::max( w_width, descwidth_final ) - 4 );
            int descwidth = find_minimum_fold_width(
                                footer_text.empty() ? entries[i].desc : footer_text, desc_lines, min_desc_width,
                                max_desc_width );
            descwidth += 4; // 2x border + 2x ' ' pad
            if( descwidth_final < descwidth ) { descwidth_final = descwidth; }
        }
        if( entries[i].text_color == c_red_red ) { entries[i].text_color = text_color; }
    }
    size_t next_free_hotkey = 0;
    for( auto it = autoassign.begin(); it != autoassign.end() && next_free_hotkey < hotkeys.size();
         ++it ) {
        while( next_free_hotkey < hotkeys.size() ) {
            const int setkey = hotkeys[next_free_hotkey];
            next_free_hotkey++;
            if( !keymap.contains( setkey ) ) {
                entries[*it].hotkey = setkey;
                keymap[setkey] = *it;
                break;
            }
        }
    }

    if( desc_enabled ) {
        if( descwidth_final > TERMX ) {
            desc_enabled = false; // give up
        } else if( descwidth_final > w_width ) {
            w_width = descwidth_final;
        }
    }

    if( !text.empty() ) {
        int twidth = utf8_width( remove_color_tags( text ) );
        bool formattxt = true;
        int realtextwidth = 0;
        if( textwidth == -1 ) {
            if( !w_auto ) {
                realtextwidth = w_width - 4;
            } else {
                realtextwidth = twidth;
                if( twidth + 4 > w_width ) {
                    if( realtextwidth + 4 > TERMX ) { realtextwidth = TERMX - 4; }
                    textformatted = foldstring( text, realtextwidth );
                    formattxt = false;
                    realtextwidth = 10;
                    for( auto& l : textformatted ) {
                        const int w = utf8_width( remove_color_tags( l ) );
                        if( w > realtextwidth ) { realtextwidth = w; }
                    }
                    if( realtextwidth + 4 > w_width ) { w_width = realtextwidth + 4; }
                }
            }
        } else if( textwidth != -1 ) {
            realtextwidth = textwidth;
            if( realtextwidth + 4 > w_width ) { w_width = realtextwidth + 4; }
        }
        if( formattxt ) { textformatted = foldstring( text, realtextwidth ); }
    }

    // shrink-to-fit
    if( desc_enabled ) {
        desc_lines = 0;
        for( const uilist_entry& ent : entries ) {
            // -2 for borders, -2 for padding
            desc_lines = std::max <
                         int > ( desc_lines,
                                 foldstring( footer_text.empty() ? ent.desc : footer_text, w_width - 4 ).size() );
        }
        if( desc_lines <= 0 ) { desc_enabled = false; }
    }

    if( w_auto && w_width > TERMX ) { w_width = TERMX; }

    vmax = entries.size();
    int additional_lines =
        2 + text_separator_line + // add two for top & bottom borders
        static_cast<int>( textformatted.size() );
    if( desc_enabled ) {
        additional_lines += desc_lines + 1; // add one for description separator line
    }

    if( h_auto ) { w_height = vmax + additional_lines; }

    if( w_height > TERMY ) { w_height = TERMY; }

    if( vmax + additional_lines > w_height ) { vmax = w_height - additional_lines; }

    if( !w_x_setup.fun ) {
        w_x = ( ( TERMX - w_width ) / 2 );
    } else {
        w_x = w_x_setup.fun( w_width );
    }
    if( !w_y_setup.fun ) {
        w_y = ( ( TERMY - w_height ) / 2 );
    } else {
        w_y = w_y_setup.fun( w_height );
    }

    window = catacurses::newwin( w_height, w_width, point( w_x, w_y ) );
    if( !window ) { abort(); }

    if( !started ) { filterlist(); }

    started = true;
}

void uilist::reposition( ui_adaptor& ui )
{
    setup();
    if( filter_popup ) { filter_popup->window( window, point( 4, w_height - 1 ), w_width - 4 ); }
    ui.position_from_window( window );
}

void uilist::apply_scrollbar()
{
    int sbside = ( pad_left <= 0 ? 0 : w_width - 1 );
    int estart = textformatted.size();
    if( estart > 0 ) {
        estart += 2;
    } else {
        estart = 1;
    }

    scrollbar()
    .offset_x( sbside )
    .offset_y( estart )
    .content_size( fentries.size() )
    .viewport_pos( vshift )
    .viewport_size( vmax )
    .border_color( border_color )
    .arrow_color( border_color )
    .slot_color( c_light_gray )
    .bar_color( c_cyan_cyan )
    .scroll_to_last( false )
    .apply( window );
}

/**
 * Generate and refresh output
 */
void uilist::show( ui_adaptor& ui ) {}

int uilist::scroll_amount_from_action( const std::string& action )
{
    if( action == "UP" ) {
        return -1;
    } else if( action == "PAGE_UP" ) {
        return ( -vmax + 1 );
    } else if( action == "SCROLL_UP" ) {
        return -3;
    } else if( action == "DOWN" ) {
        return 1;
    } else if( action == "PAGE_DOWN" ) {
        return vmax - 1;
    } else if( action == "SCROLL_DOWN" ) {
        return +3;
    } else {
        return 0;
    }
}

/**
 * check for valid scrolling keypress and handle. return false if invalid keypress
 */
bool uilist::scrollby( const int scrollby )
{
    if( scrollby == 0 ) { return false; }

    bool looparound = ( scrollby == -1 || scrollby == 1 );
    bool backwards = ( scrollby < 0 );

    fselected += scrollby;
    if( !looparound ) {
        if( backwards && fselected < 0 ) {
            fselected = 0;
        } else if( fselected >= static_cast<int>( fentries.size() ) ) {
            fselected = fentries.size() - 1;
        }
    }

    if( backwards ) {
        if( fselected < 0 ) { fselected = fentries.size() - 1; }
        for( size_t i = 0; i < fentries.size(); ++i ) {
            if( hilight_disabled || entries[fentries[fselected]].enabled ) { break; }
            --fselected;
            if( fselected < 0 ) { fselected = fentries.size() - 1; }
        }
    } else {
        if( fselected >= static_cast<int>( fentries.size() ) ) { fselected = 0; }
        for( size_t i = 0; i < fentries.size(); ++i ) {
            if( hilight_disabled || entries[fentries[fselected]].enabled ) { break; }
            ++fselected;
            if( fselected >= static_cast<int>( fentries.size() ) ) { fselected = 0; }
        }
    }
    if( static_cast<size_t>( fselected ) < fentries.size() ) {
        selected = fentries[fselected];
        if( callback != nullptr ) { callback->select( this ); }
    }
    imgui_scroll_to_selected = true;
    return true;
}

shared_ptr_fast<ui_adaptor> uilist::create_or_get_ui_adaptor()
{
    shared_ptr_fast<ui_adaptor> current_ui = ui.lock();
    if( !current_ui ) {
        ui = current_ui = make_shared_fast<ui_adaptor>();
        current_ui->on_redraw( [this]( ui_adaptor & ui ) {
            // Renderer priority: RmlUi (when this menu opened a document) > curses
            // show(). For the RmlUi path, push current state into the data-model
            // here — this runs on every redraw, including the ~60Hz ticks — and
            // let any callback touch the live document. setup() (→ vmax for
            // scrollby) still runs via reposition()/resize for all paths.
            if( rml_session ) {
                rml_sync();
                if( callback != nullptr && rml_session->doc != nullptr ) {
                    callback->draw_rml( this, rml_session->doc );
                }
            } else {
                show( ui );
            }
        } );
        current_ui->on_screen_resize( [this]( ui_adaptor & ui ) { reposition( ui ); } );
        current_ui->mark_resize();
    }
    return current_ui;
}

/**
 * Handle input and update display
 *
 */
void uilist::query( bool loop, int timeout )
{
    if( test_mode ) {
        debugmsg( "Tried to open UI in test mode" );
        ret = UILIST_ERROR;
        return;
    }
    keypress = 0;
    if( entries.empty() ) {
        ret = UILIST_ERROR;
        return;
    }
    ret = UILIST_WAIT_INPUT;

    input_context ctxt = create_main_input_context();

    shared_ptr_fast<ui_adaptor> ui = create_or_get_ui_adaptor();

    // Renderer priority: RmlUi first (when it can open a document), else curses.
    // rml_open() returns false (and is a no-op) when RmlUi isn't ready or the
    // model name is already taken by a nested menu.
    // Ensure fentries is populated before the RmlUi document opens, so the
    // data model starts with the correct row data instead of an empty list
    // that may not visually update on the first dirty-variable pass.
    if( !started ) { setup(); }
    const bool use_rmlui = rml_open();

    ui_manager::redraw();

    do {
        // When RmlUi is active, drive ~60 Hz frame ticks so mouse hover and
        // transition animations stay responsive.  The caller-requested timeout
        // is handled below: internal ticks loop; caller timeouts return.
        const int actual_timeout = ( use_rmlui && loop ) ? 16 : timeout;
        ret_act = ctxt.handle_input( actual_timeout );
        const auto event = ctxt.get_raw_input();
        keypress = event.get_first_input();
        const auto iter = keymap.find( keypress );

        if( scrollby( scroll_amount_from_action( ret_act ) ) ) {
            /* nothing */
        } else if( filtering && ret_act == "FILTER" ) {
            inputfilter();
        } else if( ret_act == "ANY_INPUT" && iter != keymap.end() ) {
            // only handle "ANY_INPUT" since "HELP_KEYBINDINGS" is already
            // handled by the input context and the caller might want to handle
            // its custom actions
            const auto it = std::find( fentries.begin(), fentries.end(), iter->second );
            if( it != fentries.end() ) {
                const bool enabled = entries[*it].enabled;
                if( enabled || allow_disabled || hilight_disabled ) {
                    // Change the selection to display correctly when this function
                    // is called again.
                    fselected = std::distance( fentries.begin(), it );
                    selected = *it;
                    if( enabled || allow_disabled ) { ret = entries[selected].retval; }
                    if( callback != nullptr ) { callback->select( this ); }
                }
            }
        } else if( !fentries.empty() && ret_act == "CONFIRM" ) {
            if( entries[selected].enabled ) {
                ret = entries[selected].retval; // valid
            } else if( allow_disabled ) {
                // disabled
                ret = entries[selected].retval;
            }
        } else if( allow_cancel && ret_act == "QUIT" ) {
            ret = UILIST_CANCEL;
        } else if( ret_act == "TIMEOUT" ) {
            if( use_rmlui && loop ) {
                // Internal frame tick — redraw and keep looping.
                // (Caller-requested timeout with loop==false falls through below.)
                ui_manager::redraw();
                continue;
            }
            ret = UILIST_TIMEOUT;
        } else if( ret_act == "MOUSE_MOVE" ) {
            // Mouse movement wakes the loop; redraw so RmlUi can update hover.
            ui_manager::redraw();
            continue;
        } else {
            // including HELP_KEYBINDINGS, in case the caller wants to refresh their contents
            bool unhandled = callback == nullptr || !callback->key( ctxt, event, selected, this );
            if( unhandled && allow_anykey ) {
                ret = UILIST_UNBOUND;
            } else if( unhandled && allow_additional ) {
                for( const auto& it : additional_actions ) {
                    if( it.first == ret_act ) {
                        ret = UILIST_ADDITIONAL;
                        break;
                    }
                }
            }
        }

        ui_manager::redraw();
    } while( loop && ret == UILIST_WAIT_INPUT );

    if( use_rmlui ) { rml_close(); }
}

bool uilist::rml_open()
{
    if( !uilist_rmlui_enabled() || !rmlui_layer::ready() ) { return false; }
    Rml::Context* ctx = rmlui_layer::context();
    if( ctx == nullptr || g_rml_uilist_model_active ) {
        return false; // not ready, or a nested uilist already owns the model name
    }
    Rml::DataModelConstructor c = ctx->CreateDataModel( "uilist" );
    if( !c ) { return false; }
    rml_session = std::make_unique<uilist_rml_session>();
    register_uilist_rml_types( c );

    c.Bind( "title", &rml_session->title );
    c.Bind( "has_title", &rml_session->has_title );
    c.Bind( "header", &rml_session->header );
    c.Bind( "rows", &rml_session->rows );
    c.Bind( "desc", &rml_session->desc );
    c.Bind( "desc_rml", &rml_session->desc_rml );
    c.Bind( "has_desc", &rml_session->has_desc );
    c.Bind( "filter", &rml_session->filter );
    c.Bind( "has_filter", &rml_session->has_filter );
    c.Bind( "filter_active", &rml_session->filter_active );
    c.Bind( "menu_style", &rml_session->menu_style );
    c.Bind( "has_side", &rml_session->has_side );
    c.BindEventCallback(
    "on_click", [this]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
        int idx = -1;
        if( !args.empty() ) { args[0].GetInto( idx ); }
        rml_on_click( idx );
    } );
    c.BindEventCallback(
    "on_hover", [this]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
        int idx = -1;
        if( !args.empty() ) { args[0].GetInto( idx ); }
        rml_on_hover( idx );
    } );
    rml_session->handle = c.GetModelHandle();

    rml_session->doc = rmlui_layer::open_document( PATH_INFO::datadir() + "gui/uilist.rml" );
    if( rml_session->doc == nullptr ) {
        ctx->RemoveDataModel( "uilist" );
        rml_session.reset();
        return false;
    }
    g_rml_uilist_model_active = true;
    // Open scrolled to the preselected entry: the first sync takes the keyboard
    // branch (scroll_row follows fselected) instead of the mouse branch (top 0).
    imgui_scroll_to_selected = true;
    rml_sync();
    // Force RmlUi to process dirty variables immediately so the document DOM
    // is built/updated before the first frame render.
    ctx->Update();
    return true;
}

void uilist::rml_sync()
{
    if( !rml_session ) { return; }
    uilist_rml_session& s = *rml_session;

    s.title = title;
    s.has_title = !title.empty();

    // Header is static for the menu's life; rebuild a candidate and only swap
    // (and dirty, below) when it actually differs, so the data-for DOM isn't
    // recreated every ~60Hz tick.
    Rml::Vector<Rml::String> new_header;
    new_header.reserve( textformatted.size() );
    for( const std::string& line : textformatted ) { new_header.push_back( remove_color_tags( line ) ); }
    const bool header_changed = new_header != s.header;
    if( header_changed ) { s.header = std::move( new_header ); }

    // desc_rml runs the expensive cata_text_to_rml; only rebuild when the source
    // text changes (same dirty-proxy discipline as rows below).
    std::string new_desc_src;
    if( desc_enabled && selected >= 0 && static_cast<size_t>( selected ) < entries.size() ) {
        new_desc_src = footer_text.empty() ? entries[selected].desc : footer_text;
    }
    const bool desc_changed = new_desc_src != s.desc_src;
    if( desc_changed ) {
        s.desc_src = new_desc_src;
        s.desc = remove_color_tags( new_desc_src );
        s.desc_rml = new_desc_src.empty() ? Rml::String() : cata_text_to_rml( new_desc_src );
        s.has_desc = !s.desc.empty();
    }

    s.filter = filter;
    s.has_filter = !filter.empty();

    // Look variant + side-region visibility. Both are static for the menu's
    // life, so they ride the unconditionally-dirtied scalars below. Panel
    // styles always show the side (their #callback is filled imperatively by
    // draw_rml, which the data model can't observe); other styles show it only
    // when there's a description.
    s.menu_style = menu_style;
    const bool panel_style = ( menu_style == "info" || menu_style == "grid" );
    s.has_side = s.has_desc || panel_style;

    // ── Virtual scrolling ────────────────────────────────────────────────────
    // Binding every row of a thousands-long menu (e.g. the wish item list) is too
    // slow: RmlUi lays out and re-submits each bound row every frame. Instead bind
    // only a window around the viewport and fake the rest of the scroll height
    // with two spacer divs, so the scrollbar stays proportional and fully
    // reachable — the standard list virtualization the OS toolkits all use.
    //
    // Row pitch is fixed by CSS so it's known on frame 0: rml_sync runs in
    // on_redraw, BEFORE RmlUi's layout pass, so a laid-out row can't be measured
    // this frame. ROW_H_DP MUST match .uilist-row (height 28dp + margin 2dp); dp
    // are scaled to layout px by the context density ratio.
    constexpr float ROW_H_DP = 30.0f;
    constexpr int BUFFER = 6; // off-screen rows kept bound on each side
    const float H = ROW_H_DP * rmlui_layer::density_ratio();
    const int n = static_cast<int>( fentries.size() );

    Rml::Element* list = s.doc != nullptr ? s.doc->GetElementById( "list" ) : nullptr;
    // Rows that fit the viewport (>0 once laid out; a sane default on frame 0).
    const float viewport = list != nullptr ? list->GetClientHeight() : 0.0f;
    const int vis = viewport > 1.0f ? std::max( 1, static_cast<int>( viewport / H ) ) : 20;
    const int len = std::min( n, vis + 2 * BUFFER );

    // scroll_row = the first fentry at the top of the viewport (the scroll
    // position). Keyboard nav keeps fselected visible with minimal scroll; the
    // mouse wheel / scrollbar drag move RmlUi's own offset, which we read back on
    // the next (non-keyboard) tick. We NEVER read GetScrollTop on the same frame
    // we SetScrollTop (the write may not lay out until Update) — both branches
    // drive the integer scroll_row, and only the keyboard branch writes the offset.
    int sr = s.scroll_row;
    const bool kb = imgui_scroll_to_selected;
    if( kb ) {
        if( fselected < sr ) {
            sr = fselected;
        } else if( fselected >= sr + vis ) {
            sr = fselected - vis + 1;
        }
    } else if( list != nullptr && H > 0.0f ) {
        sr = static_cast<int>( list->GetScrollTop() / H );
    }
    sr = std::max( 0, std::min( sr, std::max( 0, n - vis ) ) );
    s.scroll_row = sr;

    const int top = std::max( 0, std::min( sr - BUFFER, std::max( 0, n - len ) ) );
    // Re-dirty "rows" (and resize the spacers) when the window moves or resizes.
    const bool window_moved = top != s.win_top || static_cast<int>( s.rows.size() ) != len;
    s.win_top = top;
    s.win_len = len;

    // Resize in-place instead of clearing/rebuilding so RmlUi keeps the
    // same DOM elements (and their compiled geometry handles) across frames.
    // data-for recreates elements when it detects a different array, which
    // defeats geometry caching and keeps geometry perpetually deferred.
    //
    // rml_sync() runs on every redraw, including idle ~60Hz ticks. Dirtying
    // "rows" rebuilds the data-for DOM and recompiles its geometry, so only
    // dirty it when the visible window's contents actually change — otherwise
    // row geometry recompiles every frame and never leaves the deferred path.
    bool rows_changed = window_moved;
    s.rows.resize( len );
    for( int i = 0; i < len; ++i ) {
        const int ei = fentries[top + i];
        const uilist_entry& e = entries[ei];
        uilist_rml_row& r = s.rows[i];
        Rml::String text = remove_color_tags( e.txt );
        Rml::String hotkey;
        if( e.hotkey >= 33 && e.hotkey < 126 ) {
            hotkey = std::string( 1, static_cast<char>( e.hotkey ) );
        }
        Rml::String col = remove_color_tags( e.ctxt );
        const bool sel = ( ei == selected );
        if( r.text != text || r.hotkey != hotkey || r.col != col || r.enabled != e.enabled
            || r.selected != sel ) {
            // text_rml is the expensive conversion; only rebuild when the row
            // actually changed (text is the cheap dirty proxy — same source).
            r.text_rml = cata_text_to_rml( e.txt );
            r.text = std::move( text );
            r.hotkey = std::move( hotkey );
            r.col = std::move( col );
            r.enabled = e.enabled;
            r.selected = sel;
            rows_changed = true;
        }
    }

    Rml::DataModelHandle h = s.handle;
    h.DirtyVariable( "title" );
    h.DirtyVariable( "has_title" );
    if( header_changed ) { h.DirtyVariable( "header" ); }
    if( rows_changed ) { h.DirtyVariable( "rows" ); }
    if( desc_changed ) {
        h.DirtyVariable( "desc" );
        h.DirtyVariable( "desc_rml" );
        h.DirtyVariable( "has_desc" );
    }
    h.DirtyVariable( "filter" );
    h.DirtyVariable( "has_filter" );
    h.DirtyVariable( "filter_active" );
    h.DirtyVariable( "menu_style" );
    h.DirtyVariable( "has_side" );

    // Size the virtual-scroll spacers so total content height == n * H (keeps the
    // scrollbar proportional and the scroll offset stable across rebinds). Then,
    // only on keyboard frames, drive the actual scroll offset to scroll_row —
    // flushing layout first so SetScrollTop clamps against the full (post-spacer)
    // scroll height instead of the stale one (otherwise big jumps clamp short).
    if( list != nullptr && H > 0.0f ) {
        if( window_moved ) {
            const int below = std::max( 0, n - top - len );
            if( Rml::Element * sp = s.doc->GetElementById( "spacer_top" ) ) {
                sp->SetProperty( "height", std::to_string( static_cast<int>( top * H ) ) + "px" );
            }
            if( Rml::Element * sp = s.doc->GetElementById( "spacer_bottom" ) ) {
                sp->SetProperty( "height", std::to_string( static_cast<int>( below * H ) ) + "px" );
            }
        }
        if( kb ) {
            s.doc->UpdateDocument();
            list->SetScrollTop( static_cast<float>( sr ) * H );
        }
    }
    imgui_scroll_to_selected = false;
}

void uilist::rml_close()
{
    if( !rml_session ) { return; }
    if( rml_session->doc != nullptr ) { rmlui_layer::close_document( rml_session->doc ); }
    if( Rml::Context * ctx = rmlui_layer::context() ) { ctx->RemoveDataModel( "uilist" ); }
    g_rml_uilist_model_active = false;
    rml_session.reset();
}

void uilist::rml_on_click( int window_index )
{
    if( !rml_session || window_index < 0 ) { return; }
    const int fe = rml_session->win_top + window_index;
    if( fe < 0 || fe >= static_cast<int>( fentries.size() ) ) { return; }
    const int ei = fentries[fe];
    const uilist_entry& e = entries[ei];
    if( e.enabled || allow_disabled ) {
        fselected = fe;
        selected = ei;
        ret = e.retval; // confirm; query loop notices ret changed and exits
    }
}

void uilist::rml_on_hover( int window_index )
{
    if( !rml_session || window_index < 0 ) { return; }
    const int fe = rml_session->win_top + window_index;
    if( fe < 0 || fe >= static_cast<int>( fentries.size() ) ) { return; }
    const int ei = fentries[fe];
    if( ei == selected ) { return; }
    fselected = fe;
    selected = ei;
    if( callback != nullptr ) { callback->select( this ); }
}

///@}
/**
 * cleanup
 */
void uilist::reset()
{
    window = catacurses::window();
    init();
}

void uilist::addentry( const std::string& str ) { entries.emplace_back( str ); }

void uilist::addentry( int r, bool e, int k, const std::string& str )
{
    entries.emplace_back( r, e, k, str );
}

void uilist::addentry_desc( const std::string& str, const std::string& desc )
{
    entries.emplace_back( str, desc );
}

void uilist::addentry_desc( int r, bool e, int k, const std::string& str, const std::string& desc )
{
    entries.emplace_back( r, e, k, str, desc );
}

void uilist::addentry_col(
    int r, bool e, int k, const std::string& str, const std::string& column,
    const std::string& desc )
{
    entries.emplace_back( r, e, k, str, desc, column );
}

void uilist::settext( const std::string& str ) { text = str; }

struct pointmenu_cb::impl_t {
    const std::vector<tripoint_bub_ms> &points;
    int last;                  // to suppress redrawing
    tripoint_rel_ms last_view; // to reposition the view after selecting
    shared_ptr_fast<game::draw_callback_t> terrain_draw_cb;

    impl_t( const std::vector<tripoint_bub_ms> &pts );
    ~impl_t();

    void select( uilist* menu );
};

pointmenu_cb::impl_t::impl_t( const std::vector<tripoint_bub_ms> &pts ): points( pts )
{
    last = INT_MIN;
    last_view = g->u.view_offset;
    terrain_draw_cb = make_shared_fast<game::draw_callback_t>( [this]() {
        if( last >= 0 && static_cast<size_t>( last ) < points.size() ) {
            g->draw_trail_to_square( g->u.view_offset, true );
        }
    } );
    g->add_draw_callback( terrain_draw_cb );
}

pointmenu_cb::impl_t::~impl_t() { g->u.view_offset = last_view; }

void pointmenu_cb::impl_t::select( uilist* const menu )
{
    if( last == menu->selected ) { return; }
    last = menu->selected;
    if( menu->selected < 0 || menu->selected >= static_cast<int>( points.size() ) ) {
        g->u.view_offset = tripoint_rel_ms::zero();
    } else {
        const tripoint_bub_ms& center = points[menu->selected];
        g->u.view_offset = center - g->u.bub_pos();
        // TODO: Remove this line when it's safe
        g->u.view_offset.z() = 0;
    }
    g->invalidate_main_ui_adaptor();
}

pointmenu_cb::pointmenu_cb( const std::vector<tripoint_bub_ms> &pts ): impl( pts ) {}

pointmenu_cb::~pointmenu_cb() = default;

void pointmenu_cb::select( uilist* const menu ) { impl->select( menu ); }
