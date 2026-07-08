#include "game.h" // IWYU pragma: associated

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "color.h"
#include "cursesdef.h"
#include "debug.h"
#include "diary.h"
#include "input.h"
#include "options.h"
#include "output.h"
#include "popup.h"
#include "string_editor_window.h"
#include "string_formatter.h"
#include "string_input_popup.h"
#include "translations.h"
#include "ui.h"
#include "ui_manager.h"
#include "wcwidth.h"

#include <RmlUi/Core.h>

#include "rml_screen.h"
#include "rml_util.h"

namespace
{
// maximum limit of the UIs width. After that, we center them, with an even space left and right
const int MAX_DAIRY_UI_WIDTH = 150;

// RmlUi render path for the diary (full UI→RmlUi migration, Tier 1 screen #6) via
// the F.3 rml_doc harness. FIRST multi-pane shape: three simultaneous scroll
// lists (pages / changes / page-text) plus a title bar and a bottom info pane.
// Only ONE pane is active at a time (LEFT/RIGHT cycle currwin); the selected-row
// highlight shows only in the active pane (the `*_active` bools drive a CSS
// class, see diary.rcss). Live-synced via sync_rml() in ui_diary's on_redraw, so
// the cursor + active-pane + info all track the loop state without per-branch
// DirtyVariable. Colour rides inside the bound strings via cata_text_to_rml.
struct diary_rml_line {
    Rml::String text_rml;
    bool selected = false;
};
struct diary_rml_session {
    Rml::String title_rml;   // "<owner>'s Diary"
    Rml::String desc_rml;    // keybinding hints
    Rml::Vector<diary_rml_line> pages;
    Rml::Vector<diary_rml_line> changes;
    Rml::Vector<diary_rml_line> text;
    Rml::String info_rml;    // description of the selected change
    bool pages_active = false;
    bool changes_active = false;
    bool text_active = false;
    Rml::DataModelHandle handle;
};

// Context-global type registration, guarded once (see uilist pattern).
bool g_diary_types_registered = false;

void register_diary_rml_types( Rml::DataModelConstructor &c )
{
    if( g_diary_types_registered ) {
        return;
    }
    Rml::StructHandle<diary_rml_line> lh = c.RegisterStruct<diary_rml_line>();
    lh.RegisterMember( "text_rml", &diary_rml_line::text_rml );
    lh.RegisterMember( "selected", &diary_rml_line::selected );
    c.RegisterArray<Rml::Vector<diary_rml_line>>();
    g_diary_types_registered = true;
}
} // namespace

bool &diary_rmlui_enabled()
{
    // Default OFF — opt in via the F4 panel. See rml_screen.h.
    static bool enabled = true;
    return enabled;
}

static std::pair<point, point> diary_window_position()
{
    return {
        point( TERMX / 4, TERMY / 4 ),
        point( TERMX / 2, TERMY / 2 )
    };
}

static int uis_padding()
{
    const int padding = TERMX - MAX_DAIRY_UI_WIDTH;
    return ( padding >= 0 ) ? padding / 2 : 0;
}

void diary::show_diary_ui( diary *c_diary )
{
    catacurses::window w_diary;
    catacurses::window w_pages; // pages window, left of diary
    catacurses::window w_text; // right part of diary
    catacurses::window w_changes; // left part of diary
    catacurses::window w_border; // borders of diary
    catacurses::window w_desc; // keybindings window up
    catacurses::window w_info; // bottom window

    enum class window_mode : int { PAGE_WIN = 0, CHANGE_WIN, TEXT_WIN, NUM_WIN, FIRST_WIN = 0, LAST_WIN = NUM_WIN - 1 };
    window_mode currwin = window_mode::PAGE_WIN;


    std::map<window_mode, int> selected = { {window_mode::PAGE_WIN, 0}, {window_mode::CHANGE_WIN, 0}, {window_mode::TEXT_WIN, 0} };


    input_context ctxt( "DIARY" );
    ctxt.register_cardinal();
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "NEW_PAGE" );
    ctxt.register_action( "DELETE PAGE" );
    ctxt.register_action( "EXPORT_DIARY" );
    ctxt.register_action( "HELP_KEYBINDINGS" );

    // ---- RmlUi render path (F.3 rml_doc harness) ----------------------------
    // rml_doc owns the open/guard/16ms-tick/close lifecycle; only the model + this
    // live sync stay here. sync_rml() runs in ui_diary's on_redraw (invalidated
    // every loop iteration), so the three lists, the active-pane focus, and the
    // info pane all track currwin/selected without per-branch DirtyVariable. The
    // model storage is declared BEFORE rml so it outlives the document (RmlUi
    // holds raw pointers into it until close).
    std::unique_ptr<diary_rml_session> data;
    rml_doc rml;
    const auto sync_rml = [&]() {
        if( !rml ) {
            return;
        }
        data->title_rml = cata_text_to_rml( string_format( _( "%s's Diary" ), c_diary->owner ) );
        data->desc_rml = cata_text_to_rml( string_format( _( "%s, %s, %s, %s" ),
                                           ctxt.get_desc( "NEW_PAGE", _( "New page" ), input_context::allow_all_keys ),
                                           ctxt.get_desc( "CONFIRM", _( "Edit text" ), input_context::allow_all_keys ),
                                           ctxt.get_desc( "DELETE PAGE", _( "Delete page" ), input_context::allow_all_keys ),
                                           ctxt.get_desc( "EXPORT_DIARY", _( "Export diary" ),
                                               input_context::allow_all_keys ) ) );

        // pages list (left)
        data->pages.clear();
        {
            const std::vector<std::string> pages_list = c_diary->get_pages_list();
            for( int i = 0; i < static_cast<int>( pages_list.size() ); ++i ) {
                diary_rml_line ln;
                ln.text_rml = cata_text_to_rml( pages_list[i] );
                ln.selected = ( i == selected[window_mode::PAGE_WIN] );
                data->pages.push_back( ln );
            }
        }

        // changes list (centre-left): head text followed by the change list
        data->changes.clear();
        {
            std::vector<std::string> change_lines = c_diary->get_head_text();
            const std::vector<std::string> change_list = c_diary->get_change_list();
            change_lines.insert( change_lines.end(), change_list.begin(), change_list.end() );
            for( int i = 0; i < static_cast<int>( change_lines.size() ); ++i ) {
                diary_rml_line ln;
                ln.text_rml = cata_text_to_rml( change_lines[i] );
                ln.selected = ( i == selected[window_mode::CHANGE_WIN] );
                data->changes.push_back( ln );
            }
        }

        // page text (centre-right): one row per line so the cursor can scroll it
        data->text.clear();
        {
            const std::vector<std::string> text_lines = foldstring( c_diary->get_page_text(), 1000000 );
            for( int i = 0; i < static_cast<int>( text_lines.size() ); ++i ) {
                diary_rml_line ln;
                ln.text_rml = cata_text_to_rml( text_lines[i] );
                ln.selected = ( i == selected[window_mode::TEXT_WIN] );
                data->text.push_back( ln );
            }
        }

        // bottom info pane: description of the selected change (legacy showed this
        // only while the change or text pane was focused)
        if( currwin == window_mode::CHANGE_WIN || currwin == window_mode::TEXT_WIN ) {
            data->info_rml = cata_text_to_rml( c_diary->get_desc_map()[selected[window_mode::CHANGE_WIN]] );
        } else {
            data->info_rml.clear();
        }

        data->pages_active = ( currwin == window_mode::PAGE_WIN );
        data->changes_active = ( currwin == window_mode::CHANGE_WIN );
        data->text_active = ( currwin == window_mode::TEXT_WIN );

        data->handle.DirtyVariable( "title_rml" );
        data->handle.DirtyVariable( "desc_rml" );
        data->handle.DirtyVariable( "pages" );
        data->handle.DirtyVariable( "changes" );
        data->handle.DirtyVariable( "text" );
        data->handle.DirtyVariable( "info_rml" );
        data->handle.DirtyVariable( "pages_active" );
        data->handle.DirtyVariable( "changes_active" );
        data->handle.DirtyVariable( "text_active" );
    };

    rml.open( diary_rmlui_enabled(), "diary", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
        data = std::make_unique<diary_rml_session>();
        register_diary_rml_types( c );
        c.Bind( "title_rml", &data->title_rml );
        c.Bind( "desc_rml", &data->desc_rml );
        c.Bind( "pages", &data->pages );
        c.Bind( "changes", &data->changes );
        c.Bind( "text", &data->text );
        c.Bind( "info_rml", &data->info_rml );
        c.Bind( "pages_active", &data->pages_active );
        c.Bind( "changes_active", &data->changes_active );
        c.Bind( "text_active", &data->text_active );
        // Clicking/hovering a pane focuses it and moves that pane's cursor; the
        // next 16ms loop tick rebuilds via sync_rml (and re-opens the page for
        // PAGE_WIN at the loop top). Keyboard navigation is unchanged.
        c.BindEventCallback( "on_pages",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int idx = -1;
            if( !args.empty() ) {
                args[0].GetInto( idx );
            }
            if( idx >= 0 ) {
                currwin = window_mode::PAGE_WIN;
                selected[window_mode::PAGE_WIN] = idx;
            }
        } );
        c.BindEventCallback( "on_changes",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int idx = -1;
            if( !args.empty() ) {
                args[0].GetInto( idx );
            }
            if( idx >= 0 ) {
                currwin = window_mode::CHANGE_WIN;
                selected[window_mode::CHANGE_WIN] = idx;
            }
        } );
        c.BindEventCallback( "on_text",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int idx = -1;
            if( !args.empty() ) {
                args[0].GetInto( idx );
            }
            if( idx >= 0 ) {
                currwin = window_mode::TEXT_WIN;
                selected[window_mode::TEXT_WIN] = idx;
            }
        } );
        data->handle = c.GetModelHandle();
    } );

    ui_adaptor ui_diary;
    ui_diary.on_screen_resize( [&]( ui_adaptor & ui ) {
        const std::pair<point, point> beg_and_max = diary_window_position();
        point beg = ( uis_padding() != 0 ) ? point( uis_padding() + MAX_DAIRY_UI_WIDTH / 4 - 3,
                    beg_and_max.first.y - 1 ) : beg_and_max.first + point( uis_padding() - 3, -1 );
        point max = point( TERMX - beg.x - 5,
                           beg_and_max.second.y ) - point( uis_padding(), 0 );
        const int midx = max.x / 2;

        w_changes = catacurses::newwin( max.y - 3, midx - 1, beg + point_south );
        w_text = catacurses::newwin( max.y - 3, max.x - midx - 1, beg + point( 2 + midx, 3 ) );
        w_border = catacurses::newwin( max.y + 5, max.x + 9, beg + point( -4, -2 ) );

        ui.position_from_window( w_border );
    } );
    ui_diary.mark_resize();
    ui_diary.on_redraw( [&]( const ui_adaptor & ) {
        // RmlUi path owns the screen — sync the model and skip curses drawing.
        // sync lives only here (ui_diary is invalidated every loop iteration);
        // the other three panes early-return so the doc renders once per frame.
        if( rml ) {
            sync_rml();
            return;
        }
    } );

    ui_adaptor ui_pages;
    ui_pages.on_screen_resize( [&]( ui_adaptor & ui ) {
        const std::pair<point, point> beg_and_max = diary_window_position();
        point beg = beg_and_max.first;
        point max = beg_and_max.second;

        w_pages = catacurses::newwin( max.y + 5,
                                      ( uis_padding() != 0 ) ? MAX_DAIRY_UI_WIDTH / 4 - 7 : beg.x - 7, point( uis_padding(),
                                          beg.y - 3 ) );

        ui.position_from_window( w_pages );
    } );
    ui_pages.mark_resize();
    ui_pages.on_redraw( []( const ui_adaptor & ) {
        // RmlUi owns the screen; curses fallback removed (rip-out B).
    } );

    ui_adaptor ui_desc;
    ui_desc.on_screen_resize( [&]( ui_adaptor & ui ) {
        const std::pair<point, point> beg_and_max = diary_window_position();
        point beg = beg_and_max.first;

        w_desc = catacurses::newwin( 3, TERMX - uis_padding() * 2, point( uis_padding(), beg.y - 6 ) );

        ui.position_from_window( w_desc );
    } );
    ui_desc.mark_resize();
    ui_desc.on_redraw( []( const ui_adaptor & ) {
        // RmlUi owns the screen; curses fallback removed (rip-out B).
    } );

    ui_adaptor ui_info;
    ui_info.on_screen_resize( [&]( ui_adaptor & ui ) {
        const std::pair<point, point> beg_and_max = diary_window_position();
        point beg = beg_and_max.first;
        point max = beg_and_max.second;

        w_info = catacurses::newwin( ( TERMY - beg.y - max.y - 2 ) > 7 ? 7 : TERMY - beg.y - max.y - 2,
                                     TERMX - uis_padding() * 2, point( uis_padding(), beg.y + max.y + 2 ) );

        ui.position_from_window( w_info );
    } );
    ui_info.mark_resize();
    ui_info.on_redraw( []( const ui_adaptor & ) {
        // RmlUi owns the screen; curses fallback removed (rip-out B).
    } );

    while( true ) {

        if( ( !c_diary->pages.empty() &&
              selected[window_mode::PAGE_WIN] >= static_cast<int>( c_diary->pages.size() ) ) ||
            ( c_diary->pages.empty() && selected[window_mode::PAGE_WIN] != 0 ) ) {
            selected[window_mode::PAGE_WIN] = 0;
        }
        selected[window_mode::PAGE_WIN] = c_diary->set_opened_page( selected[window_mode::PAGE_WIN] );
        ui_diary.invalidate_ui();
        ui_pages.invalidate_ui();
        ui_desc.invalidate_ui();
        ui_info.invalidate_ui();
        ui_manager::redraw_invalidated();
        const std::string action = ctxt.handle_input();
        if( action == "RIGHT" ) {
            currwin = static_cast<window_mode>( static_cast<int>( currwin ) + 1 );
            if( currwin >= window_mode::NUM_WIN ) {
                currwin = window_mode::FIRST_WIN;
            }
            selected[window_mode::TEXT_WIN] = 0;
        } else if( action == "LEFT" ) {
            currwin = static_cast<window_mode>( static_cast<int>( currwin ) - 1 );
            if( currwin < window_mode::FIRST_WIN ) {
                currwin = window_mode::LAST_WIN;
            }
            selected[window_mode::TEXT_WIN] = 0;
        } else if( action == "DOWN" ) {
            selected[currwin]++;
            if( selected[window_mode::PAGE_WIN] >= static_cast<int>( c_diary->pages.size() ) ) {
                selected[window_mode::PAGE_WIN] = 0;
            }
        } else if( action == "UP" ) {
            selected[currwin]--;
            if( selected[window_mode::PAGE_WIN] < 0 ) {
                selected[window_mode::PAGE_WIN] = c_diary->pages.empty() ? 0 : c_diary->pages.size() - 1;
            }

        } else if( action == "CONFIRM" ) {
            if( !c_diary->pages.empty() ) {
                c_diary->edit_page_ui( [&]() {
                    return w_text;
                } );
            }
        } else if( action == "NEW_PAGE" ) {
            c_diary->new_page();
            selected[window_mode::PAGE_WIN] = c_diary->pages.size() - 1;

        } else if( action == "DELETE PAGE" ) {
            if( !c_diary->pages.empty() ) {
                if( query_yn( _( "Delete this page from the diary?" ) ) ) {
                    c_diary->delete_page();
                    if( selected[window_mode::PAGE_WIN] >= static_cast<int>( c_diary->pages.size() ) ) {
                        selected[window_mode::PAGE_WIN] --;
                    }
                }
            }
        } else if( action == "EXPORT_DIARY" ) {
            if( query_yn( _( "Export the diary as .md?" ) ) ) {
                c_diary->export_to_md();
            }

        } else if( action == "QUIT" ) {
            break;
        }

    }

    // Tear down the RmlUi document while the bound `data` is still alive.
    // close() is idempotent and a no-op when the curses path ran.
    rml.close();
}

void diary::edit_page_ui( const std::function<catacurses::window()> &create_window )
{
    // modify the stored text, so the new text is displayed after exiting from
    // the editor window, and before confirming or canceling the y/n query.
    std::string &new_text = get_page_ptr()->m_text;
    const std::string old_text = new_text;

    string_editor_window ed( create_window, new_text );

    do {
        const std::pair<bool, std::string> result = ed.query_string();
        new_text = result.second;

        // confirmed or unchanged
        if( result.first || old_text == new_text ) {
            break;
        }

        const bool force_uc = get_option<bool>( "FORCE_CAPITAL_YN" );
        const auto &allow_key = force_uc ? input_context::disallow_lower_case
                                : input_context::allow_all_keys;
        const std::string action = query_popup()
                                   .context( "YESNOQUIT" )
                                   .message( "%s", _( "Save entry?" ) )
                                   .option( "YES", allow_key )
                                   .option( "NO", allow_key )
                                   .allow_cancel( true )
                                   .default_color( c_light_red )
                                   .query()
                                   .action;
        if( action == "YES" ) {
            break;
        } else if( action == "NO" ) {
            new_text = old_text;
            break;
        }
    } while( true );
}
