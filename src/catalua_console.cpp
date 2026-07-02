#include "catalua_console.h"

#include "catalua_impl.h"
#include "catalua_log.h"
#include "cursesdef.h"
#include "game.h"
#include "init.h"
#include "input.h"
#include "lighting/rmlui_layer.h"
#include "output.h"
#include "path_info.h"
#include "rml_screen.h"
#include "rml_util.h"
#include "string_editor_window.h"
#include "string_utils.h"
#include "ui_manager.h"
#include "uistate.h"

#include <RmlUi/Core.h>
#include <algorithm>

bool &lua_console_rmlui_enabled()
{
    // Default ON — opt in via the F4 panel. See rml_screen.h.
    static bool enabled = true;
    return enabled;
}

namespace cata
{

namespace
{

struct numbered_prompt_line {
    int number = 0;
    std::string text;
};

auto split_prompt_lines( const std::string& text ) -> std::vector<std::string>
{
    auto lines = std::vector<std::string>();
    auto start = std::string::size_type{0};
    while( start <= text.size() ) {
        const auto end = text.find( '\n', start );
        if( end == std::string::npos ) {
            lines.push_back( text.substr( start ) );
            break;
        }
        lines.push_back( text.substr( start, end - start ) );
        start = end + 1;
        if( start == text.size() ) {
            lines.emplace_back();
            break;
        }
    }
    if( lines.empty() ) { lines.emplace_back(); }
    return lines;
}

auto format_line_number( const int line_number, const int width ) -> std::string
{
    if( line_number <= 0 ) { return std::string( width, ' ' ); }
    auto rendered = std::to_string( line_number );
    const auto padding = width - static_cast<int>( rendered.size() );
    if( padding > 0 ) { rendered.insert( rendered.begin(), padding, ' ' ); }
    return rendered;
}

auto build_numbered_prompt_lines( const std::string& text, const int content_width )
-> std::vector<numbered_prompt_line>
{
    auto lines = std::vector<numbered_prompt_line>();
    const auto logical_lines = split_prompt_lines( text );
    auto line_number = 1;
    for( auto i = 0; i < static_cast<int>( logical_lines.size() ); ++i ) {
        auto folded = foldstring( logical_lines[i], content_width );
        if( folded.empty() ) { folded.emplace_back(); }
        for( auto j = 0; j < static_cast<int>( folded.size() ); ++j ) {
            lines.push_back( numbered_prompt_line{
                .number = j == 0 ? line_number : 0,
                .text = std::move( folded[j] ),
            } );
        }
        ++line_number;
    }
    return lines;
}

} // namespace


// ---- RmlUi session ---------------------------------------------------------

struct rml_log_entry {
    Rml::String text_rml;
    bool is_input = false;
    bool is_head = false;
};

struct rml_prompt_line {
    Rml::String num; // line-number label (or spaces for continuation)
    Rml::String text_rml;
};

struct lua_console_rml_session {
    Rml::Vector<rml_log_entry> log;
    Rml::Vector<rml_prompt_line> prompt;
    Rml::String hints_rml;
    Rml::String footer_rml;
    Rml::DataModelHandle handle;
    Rml::ElementDocument *doc = nullptr;
};

struct folded_log_msg {
    bool is_head = false;
    LuaLogLevel level;
    std::string text;
};

static std::vector<folded_log_msg> build_folded_log( int width )
{
    std::vector<folded_log_msg> ret;
    for( const lua_log_msg& msg : get_lua_log_instance().get_entries() ) {
        std::vector<std::string> lines = foldstring( msg.text, width );
        for( int i = static_cast<int>( lines.size() ) - 1; i >= 0; i-- ) {
            ret.push_back( folded_log_msg{i == 0, msg.level, std::move( lines[i] )} );
        }
    }
    return ret;
}

static nc_color get_log_level_color( LuaLogLevel level )
{
    switch( level ) {
        case LuaLogLevel::Input:
            return c_white;
        case LuaLogLevel::DebugMsg:
            return c_magenta;
        case LuaLogLevel::Error:
            return c_red;
        case LuaLogLevel::Warn:
            return c_yellow;
        case LuaLogLevel::Info:
            return c_light_gray;
        default:
            debugmsg( "Log level color not defined!" );
            return c_white;
    }
}

static std::vector<std::string> &get_input_history() { return uistate.gethistory( "LUA_CONSOLE" ); }

static int num_history_entries() { return static_cast<int>( get_input_history().size() ); }

static void add_to_input_history( const std::string& s )
{
    std::vector<std::string> &hist = get_input_history();
    for( auto it = hist.begin(); it != hist.end(); it++ ) {
        if( *it == s ) {
            // Refresh existing history entry
            std::string msg = std::move( *it );
            hist.erase( it );
            hist.push_back( std::move( msg ) );
            return;
        }
    }
    // Add new history entry
    hist.push_back( s );
}

void show_lua_console_impl()
{
    input_context ctxt( "LUA_CONSOLE" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "EDIT" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "LUA_RELOAD" );
    ctxt.register_action( "TOGGLE_EXPANDED" );
    ctxt.register_action( "HISTORY_UP" );
    ctxt.register_action( "HISTORY_DOWN" );
    ctxt.register_action( "SCROLL_UP" );
    ctxt.register_action( "SCROLL_DOWN" );
    ctxt.register_action( "SCROLL_TOP" );
    ctxt.register_action( "SCROLL_BOTTOM" );

    ui_adaptor ui;
    rml_doc rml;
    std::unique_ptr<lua_console_rml_session> rml_sess;

    constexpr auto CURRENT_INPUT = -1;
    constexpr auto scroll_speed = 5;
    constexpr auto collapsed_input_area_size = 5;

    point win_pos;
    point win_size;
    point log_pos;
    point log_size;
    point prompt_pos;
    point prompt_size;

    auto log_scroll_pos = 0;
    auto log_folded = std::vector<folded_log_msg>();

    auto use_expanded_view = false;
    auto input_area_size = collapsed_input_area_size;

    const auto get_input_area_size = [&]( const point & window_size, const bool expanded ) -> int {
        const auto max_input_size = std::max( collapsed_input_area_size, window_size.y - 8 );
        if( !expanded ) { return collapsed_input_area_size; }
        const auto target_size = std::max( collapsed_input_area_size, window_size.y / 2 );
        return std::min( max_input_size, target_size );
    };

    const auto create_string_editor = [&]() {
        // Offset by one for the scrollbar
        return catacurses::newwin( prompt_size.y, prompt_size.x + 1, prompt_pos - point_east );
    };

    catacurses::window w_console;
    catacurses::window w_log;
    catacurses::window w_prompt;

    std::string current_input;
    int history_cursor = CURRENT_INPUT;

    auto is_editing = false;

    ui.on_screen_resize( [&]( ui_adaptor & ui ) {
        win_size = point( TERMX, TERMY );
        win_pos = point( ( TERMX - win_size.x ) / 2, ( TERMY - win_size.y ) / 2 );
        input_area_size = get_input_area_size( win_size, use_expanded_view );
        prompt_size = point( win_size.x - 3, input_area_size );
        prompt_pos = win_pos + point( 2, win_size.y - input_area_size - 1 );
        log_pos = win_pos + point_south_east;
        log_size = win_size + point( -2, -7 - input_area_size );
        w_console = catacurses::newwin( win_size.y, win_size.x, win_pos );
        w_log = catacurses::newwin( log_size.y, log_size.x, log_pos );
        w_prompt = catacurses::newwin( prompt_size.y, prompt_size.x, prompt_pos );
        ui.position_from_window( w_console );
    } );
    ui.mark_resize();

    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( rml ) { return; }
    } );


    // ---- RmlUi path ---------------------------------------------------------
    // Build a sync_rml lambda that pushes current state into the data model.
    // Called after every log rebuild and after each action that changes visible state.
    const auto sync_rml_data = [&]() {
        if( !rml_sess ) { return; }
        lua_console_rml_session& s = *rml_sess;

        // Log entries (rml_sess->log is rebuilt from log_folded)
        // log_folded[0] = newest; render oldest-first for top-down display.
        s.log.clear();
        for( int i = static_cast<int>( log_folded.size() ) - 1; i >= 0; i-- ) {
            const folded_log_msg& m = log_folded[i];
            nc_color col = get_log_level_color( m.level );
            std::string text =
                m.level == LuaLogLevel::Input && m.is_head ? "> " + m.text
                : m.level == LuaLogLevel::Input
                ? "  " + m.text
                : m.text;
            s.log.push_back( rml_log_entry{
                cata_text_to_rml( colorize( text, col ) ),
                m.level == LuaLogLevel::Input,
                m.is_head,
            } );
        }

        // Prompt lines
        const auto prompt_text =
            history_cursor == CURRENT_INPUT ? current_input : get_input_history()[history_cursor];
        const auto prompt_lines = build_numbered_prompt_lines( prompt_text, 60 );
        s.prompt.clear();
        for( const auto& line : prompt_lines ) {
            std::string num_str = format_line_number( line.number, 3 );
            s.prompt.push_back( rml_prompt_line{
                rml_escape( num_str ),
                rml_escape( line.text ),
            } );
        }

        // Hints
        if( is_editing ) {
            s.hints_rml = rml_escape( _(
                                          "Ctrl+S: run   Esc: cancel   Arrows: navigate   Enter: new "
                                          "line" ) );
        } else {
            s.hints_rml = rml_escape( string_format(
                                          _( "Enter: edit   Up/Down: history   PgUp/PgDn: scroll log   %s: expanded input   "
                                             "Esc: quit" ),
                                          ctxt.get_desc( "TOGGLE_EXPANDED" ) ) );
        }

        // Footer
        s.footer_rml = rml_escape( _( "Lua console" ) );

        Rml::DataModelHandle h = s.handle;
        h.DirtyVariable( "log" );
        h.DirtyVariable( "prompt" );
        h.DirtyVariable( "hints_rml" );
        h.DirtyVariable( "footer_rml" );
    };

    // Open RmlUi document (no-op when toggle OFF or not ready).
    rml.open( lua_console_rmlui_enabled(), "lua_console", ctxt, [&]( Rml::DataModelConstructor & c ) {
        rml_sess = std::make_unique<lua_console_rml_session>();

        // Register struct types once per context.
        static bool types_registered = false;
        if( !types_registered ) {
            types_registered = true;
            auto lh = c.RegisterStruct<rml_log_entry>();
            lh.RegisterMember( "text_rml", &rml_log_entry::text_rml );
            lh.RegisterMember( "is_input", &rml_log_entry::is_input );
            lh.RegisterMember( "is_head", &rml_log_entry::is_head );
            c.RegisterArray<Rml::Vector<rml_log_entry>>();

            auto ph = c.RegisterStruct<rml_prompt_line>();
            ph.RegisterMember( "num", &rml_prompt_line::num );
            ph.RegisterMember( "text_rml", &rml_prompt_line::text_rml );
            c.RegisterArray<Rml::Vector<rml_prompt_line>>();
        }

        c.Bind( "log", &rml_sess->log );
        c.Bind( "prompt", &rml_sess->prompt );
        c.Bind( "hints_rml", &rml_sess->hints_rml );
        c.Bind( "footer_rml", &rml_sess->footer_rml );
        rml_sess->handle = c.GetModelHandle();
    } );

    // Prime the model on first open.
    if( rml ) { sync_rml_data(); }

    bool log_invalidated = true;
    while( true ) {
        if( log_invalidated ) {
            log_invalidated = false;
            log_scroll_pos = 0;
            log_folded = build_folded_log( 76 );
            if( rml ) { sync_rml_data(); }
        }
        ui_manager::redraw_invalidated();

        const std::string act = ctxt.handle_input();

        if( act == "QUIT" ) {
            // Close
            return;
        } else if( act == "HISTORY_UP" ) {
            int sz = num_history_entries();
            if( sz != 0 && history_cursor != 0 ) {
                if( history_cursor == CURRENT_INPUT ) {
                    history_cursor = sz - 1;
                } else {
                    history_cursor = std::max( 0, history_cursor - 1 );
                }
            }
            // Update input preview
            ui.invalidate_ui();
        } else if( act == "HISTORY_DOWN" ) {
            int sz = num_history_entries();
            if( sz != 0 && history_cursor != CURRENT_INPUT ) {
                if( history_cursor == sz - 1 ) {
                    history_cursor = CURRENT_INPUT;
                } else {
                    history_cursor = std::min( history_cursor + 1, sz - 1 );
                }
            }
            // Update input preview
            ui.invalidate_ui();
        } else if( act == "SCROLL_UP" || act == "SCROLL_TOP" ) {
            int limit = std::max( 0, static_cast<int>( log_folded.size() ) - log_size.y );
            if( act == "SCROLL_TOP" ) {
                log_scroll_pos = limit;
            } else {
                log_scroll_pos = std::min( limit, log_scroll_pos + scroll_speed );
            }
            ui.invalidate_ui();
        } else if( act == "SCROLL_DOWN" ) {
            log_scroll_pos = std::max( 0, log_scroll_pos - scroll_speed );
            ui.invalidate_ui();
        } else if( act == "SCROLL_BOTTOM" ) {
            log_scroll_pos = 0;
            ui.invalidate_ui();
        } else if( act == "EDIT" ) {
            // Edit
            is_editing = true;
            ui.invalidate_ui();
            string_editor_window ew(
                create_string_editor,
                history_cursor == CURRENT_INPUT
                ? current_input
                : get_input_history()[history_cursor],
            string_editor_window::string_editor_window_options{
                .show_line_numbers = true,
            } );
            std::pair<bool, std::string> res = ew.query_string();
            is_editing = false;
            history_cursor = CURRENT_INPUT;
            if( res.first ) {
                // Confirmed
                current_input.clear();
                add_to_input_history( res.second );
                log_invalidated = true;
                run_console_input( DynamicDataLoader::get_instance().lua->lua, res.second );
            } else {
                // Canceled, save input for later use
                current_input = res.second;
            }
        } else if( act == "LUA_RELOAD" ) {
            ui.invalidate_ui();
            log_invalidated = true;
            reload_lua_code();
        } else if( act == "TOGGLE_EXPANDED" ) {
            use_expanded_view = !use_expanded_view;
            ui.mark_resize();
            ui.invalidate_ui();
        }
    }
}

} // namespace cata
