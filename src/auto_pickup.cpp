#include "auto_pickup.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>

#include "avatar.h"
#include "color.h"
#include "cursesdef.h"
#include "debug.h"
#include "filesystem.h"
#include "fstream_utils.h"
#include "game.h"
#include "input.h"
#include "item.h"
#include "item_factory.h"
#include "itype.h"
#include "json.h"
#include "material.h"
#include "options.h"
#include "output.h"
#include "path_info.h"
#include "point.h"
#include "string_formatter.h"
#include "string_input_popup.h"
#include "string_utils.h"
#include "translations.h"
#include "type_id.h"
#include "ui_manager.h"
#include "world.h"

#include <RmlUi/Core.h>

#include "rml_screen.h"
#include "rml_util.h"

// ── RmlUi render path (full UI→RmlUi migration, Tier 2 screen #4) ─────────────
// 6th rml_doc consumer; the near-free twin of safemode (same tabbed rules-table
// shape). Dynamic tab count (Global/Character) rendered as a bound Vector over
// the shared theme .tabs/.tab; a 2-column table (Rule / I/E) with a 2D cursor
// (iLine + iColumn ∈ {1,2}) where the active CELL is highlighted via row.sel_col.
// All editing stays on input_context + the migrated string_input_popup; the doc
// is render-only. Per-row colour baked via cata_text_to_rml (bActive → grey).
namespace
{
struct autopickup_rml_tab {
    Rml::String name_rml;
    bool selected = false;
};
struct autopickup_rml_row {
    Rml::String num_rml;
    Rml::String rule_rml;
    Rml::String ie_rml;
    bool selected = false;   // iLine == i
    int sel_col = -1;        // active column (1 = rule, 2 = I/E), else -1
};
struct autopickup_rml_session {
    Rml::Vector<autopickup_rml_tab> tabs;
    Rml::Vector<autopickup_rml_row> rows;
    Rml::String status_rml;   // "Auto pickup enabled: True/False  <S>witch"
    Rml::String hints_rml;
    Rml::DataModelHandle handle;
};

bool g_autopickup_types_registered = false;

void register_autopickup_rml_types( Rml::DataModelConstructor &c )
{
    if( g_autopickup_types_registered ) {
        return;
    }
    Rml::StructHandle<autopickup_rml_tab> th = c.RegisterStruct<autopickup_rml_tab>();
    th.RegisterMember( "name_rml", &autopickup_rml_tab::name_rml );
    th.RegisterMember( "selected", &autopickup_rml_tab::selected );
    c.RegisterArray<Rml::Vector<autopickup_rml_tab>>();
    Rml::StructHandle<autopickup_rml_row> rh = c.RegisterStruct<autopickup_rml_row>();
    rh.RegisterMember( "num_rml", &autopickup_rml_row::num_rml );
    rh.RegisterMember( "rule_rml", &autopickup_rml_row::rule_rml );
    rh.RegisterMember( "ie_rml", &autopickup_rml_row::ie_rml );
    rh.RegisterMember( "selected", &autopickup_rml_row::selected );
    rh.RegisterMember( "sel_col", &autopickup_rml_row::sel_col );
    c.RegisterArray<Rml::Vector<autopickup_rml_row>>();
    g_autopickup_types_registered = true;
}
} // namespace

bool &autopickup_rmlui_enabled()
{
    // Default OFF — opt in via the F4 panel. See rml_screen.h.
    static bool enabled = true;
    return enabled;
}

using namespace auto_pickup;

static bool check_special_rule( const std::vector<material_id> &materials,
                                const std::string &rule );

auto_pickup::player_settings &get_auto_pickup()
{
    static auto_pickup::player_settings single_instance;
    return single_instance;
}

void user_interface::show()
{
    if( tabs.empty() ) {
        return;
    }

    const int iHeaderHeight = 4;
    int iContentHeight = 0;
    const int iTotalCols = 2;

    catacurses::window w_border;
    catacurses::window w_header;
    catacurses::window w;

    ui_adaptor ui;

    const auto init_windows = [&]( ui_adaptor & ui ) {
        iContentHeight = FULL_SCREEN_HEIGHT - 2 - iHeaderHeight;
        const point iOffset( TERMX > FULL_SCREEN_WIDTH ? ( TERMX - FULL_SCREEN_WIDTH ) / 2 : 0,
                             TERMY > FULL_SCREEN_HEIGHT ? ( TERMY - FULL_SCREEN_HEIGHT ) / 2 : 0 );

        w_border = catacurses::newwin( FULL_SCREEN_HEIGHT, FULL_SCREEN_WIDTH,
                                       iOffset );
        w_header = catacurses::newwin( iHeaderHeight, FULL_SCREEN_WIDTH - 2,
                                       iOffset + point_south_east );
        w = catacurses::newwin( iContentHeight, FULL_SCREEN_WIDTH - 2,
                                iOffset + point( 1, iHeaderHeight + 1 ) );

        ui.position_from_window( w_border );
    };
    init_windows( ui );
    ui.on_screen_resize( init_windows );

    size_t iTab = 0;
    int iLine = 0;
    int iColumn = 1;
    int iStartPos = 0;

    // ---- RmlUi render path (F.3 rml_doc harness) ----------------------------
    // Declarations + sync live here (on_redraw is registered just below); the
    // open() call is AFTER the input_context is built further down (open needs the
    // ctxt for the 16ms tick). sync_rml() does NOT need ctxt (hints use
    // shortcut_text), so it is safe to define before ctxt exists. The doc is
    // render-only: all editing stays in the loop. Model storage declared BEFORE
    // rml so it outlives the document.
    std::unique_ptr<autopickup_rml_session> data;
    rml_doc rml;
    const auto sync_rml = [&]() {
        if( !rml ) {
            return;
        }
        data->tabs.clear();
        for( size_t i = 0; i < tabs.size(); ++i ) {
            autopickup_rml_tab t;
            t.name_rml = cata_text_to_rml( tabs[i].title );
            t.selected = ( iTab == i );
            data->tabs.push_back( t );
        }

        std::string hints;
        hints += shortcut_text( c_light_green, _( "<A>dd" ) ) + "  ";
        hints += shortcut_text( c_light_green, _( "<R>emove" ) ) + "  ";
        hints += shortcut_text( c_light_green, _( "<C>opy" ) ) + "  ";
        hints += shortcut_text( c_light_green, _( "<M>ove" ) ) + "  ";
        hints += shortcut_text( c_light_green, _( "<E>nable" ) ) + "  ";
        hints += shortcut_text( c_light_green, _( "<D>isable" ) ) + "  ";
        if( !g->u.name.empty() ) {
            hints += shortcut_text( c_light_green, _( "<T>est" ) ) + "  ";
        }
        hints += shortcut_text( c_light_green, _( "<+-> Move up/down" ) ) + "  ";
        hints += shortcut_text( c_light_green, _( "<Enter>-Edit" ) ) + "  ";
        hints += shortcut_text( c_light_green, _( "<Tab>-Switch Page" ) );
        data->hints_rml = cata_text_to_rml( hints );

        if( is_autopickup ) {
            const bool on = get_option<bool>( "AUTO_PICKUP" );
            data->status_rml = cata_text_to_rml( string_format( _( "Auto pickup enabled: %s  %s" ),
                                                 colorize( on ? _( "True" ) : _( "False" ), on ? c_light_green : c_light_red ),
                                                 shortcut_text( c_light_green, _( "<S>witch" ) ) ) );
        } else {
            data->status_rml.clear();
        }

        const rule_list &cur = tabs[iTab].new_rules;
        data->rows.clear();
        for( int i = 0; i < static_cast<int>( cur.size() ); ++i ) {
            const nc_color col = cur[i].bActive ? c_white : c_light_gray;
            autopickup_rml_row r;
            r.num_rml = cata_text_to_rml( colorize( string_format( "%d", i + 1 ), col ) );
            r.rule_rml = cata_text_to_rml( colorize(
                                               cur[i].sRule.empty() ? _( "<empty rule>" ) : cur[i].sRule, col ) );
            r.ie_rml = cata_text_to_rml( colorize(
                                             cur[i].bExclude ? _( "Exclude" ) : _( "Include" ), col ) );
            r.selected = ( iLine == i );
            r.sel_col = ( iLine == i ) ? iColumn : -1;
            data->rows.push_back( r );
        }

        data->handle.DirtyVariable( "tabs" );
        data->handle.DirtyVariable( "rows" );
        data->handle.DirtyVariable( "status_rml" );
        data->handle.DirtyVariable( "hints_rml" );
    };

    ui.on_redraw( [&]( const ui_adaptor & ) {
        // RmlUi path owns the screen — sync the model and skip curses drawing.
        if( rml ) {
            sync_rml();
            return;
        }
        // Redraw the border
        draw_border( w_border, BORDER_COLOR, title );
        // |-
        mvwputch( w_border, point( 0, 3 ), c_light_gray, LINE_XXXO );
        // -|
        mvwputch( w_border, point( 79, 3 ), c_light_gray, LINE_XOXX );
        // _|_
        mvwputch( w_border, point( 5, FULL_SCREEN_HEIGHT - 1 ), c_light_gray, LINE_XXOX );
        mvwputch( w_border, point( 51, FULL_SCREEN_HEIGHT - 1 ), c_light_gray, LINE_XXOX );
        mvwputch( w_border, point( 61, FULL_SCREEN_HEIGHT - 1 ), c_light_gray, LINE_XXOX );
        wnoutrefresh( w_border );

        // Redraw the header
        int tmpx = 0;
        tmpx += shortcut_print( w_header, point( tmpx, 0 ), c_white, c_light_green, _( "<A>dd" ) ) + 2;
        tmpx += shortcut_print( w_header, point( tmpx, 0 ), c_white, c_light_green, _( "<R>emove" ) ) + 2;
        tmpx += shortcut_print( w_header, point( tmpx, 0 ), c_white, c_light_green, _( "<C>opy" ) ) + 2;
        tmpx += shortcut_print( w_header, point( tmpx, 0 ), c_white, c_light_green, _( "<M>ove" ) ) + 2;
        tmpx += shortcut_print( w_header, point( tmpx, 0 ), c_white, c_light_green, _( "<E>nable" ) ) + 2;
        tmpx += shortcut_print( w_header, point( tmpx, 0 ), c_white, c_light_green, _( "<D>isable" ) ) + 2;
        if( !g->u.name.empty() ) {
            shortcut_print( w_header, point( tmpx, 0 ), c_white, c_light_green, _( "<T>est" ) );
        }
        tmpx = 0;
        tmpx += shortcut_print( w_header, point( tmpx, 1 ), c_white, c_light_green,
                                _( "<+-> Move up/down" ) ) + 2;
        tmpx += shortcut_print( w_header, point( tmpx, 1 ), c_white, c_light_green,
                                _( "<Enter>-Edit" ) ) + 2;
        shortcut_print( w_header, point( tmpx, 1 ), c_white, c_light_green, _( "<Tab>-Switch Page" ) );

        for( int i = 0; i < 78; i++ ) {
            if( i == 4 || i == 50 || i == 60 ) {
                mvwputch( w_header, point( i, 2 ), c_light_gray, LINE_OXXX );
                mvwputch( w_header, point( i, 3 ), c_light_gray, LINE_XOXO );
            } else {
                // Draw line under header
                mvwputch( w_header, point( i, 2 ), c_light_gray, LINE_OXOX );
            }
        }
        mvwprintz( w_header, point( 1, 3 ), c_white, "#" );
        mvwprintz( w_header, point( 8, 3 ), c_white, _( "Rules" ) );
        mvwprintz( w_header, point( 52, 3 ), c_white, _( "I/E" ) );

        rule_list &cur_rules = tabs[iTab].new_rules;
        int locx = 17;
        for( size_t i = 0; i < tabs.size(); i++ ) {
            const auto color = iTab == i ? hilite( c_white ) : c_white;
            locx += shortcut_print( w_header, point( locx, 2 ), c_white, color, tabs[i].title ) + 1;
        }

        locx = 55;
        mvwprintz( w_header, point( locx, 0 ), c_white, _( "Auto pickup enabled:" ) );
        locx += shortcut_print( w_header, point( locx, 1 ),
                                get_option<bool>( "AUTO_PICKUP" ) ? c_light_green : c_light_red, c_white,
                                get_option<bool>( "AUTO_PICKUP" ) ? _( "True" ) : _( "False" ) );
        locx += shortcut_print( w_header, point( locx, 1 ), c_white, c_light_green, "  " );
        locx += shortcut_print( w_header, point( locx, 1 ), c_white, c_light_green, _( "<S>witch" ) );
        shortcut_print( w_header, point( locx, 1 ), c_white, c_light_green, "  " );

        wnoutrefresh( w_header );

        // Clear the lines
        for( int i = 0; i < iContentHeight; i++ ) {
            for( int j = 0; j < 79; j++ ) {
                if( j == 4 || j == 50 || j == 60 ) {
                    mvwputch( w, point( j, i ), c_light_gray, LINE_XOXO );
                } else {
                    mvwputch( w, point( j, i ), c_black, ' ' );
                }
            }
        }

        draw_scrollbar( w_border, iLine, iContentHeight, cur_rules.size(), point( 0, 5 ) );
        wnoutrefresh( w_border );

        calcStartPos( iStartPos, iLine, iContentHeight, cur_rules.size() );

        // display auto pickup
        for( int i = iStartPos; i < static_cast<int>( cur_rules.size() ); i++ ) {
            if( i >= iStartPos &&
                i < iStartPos + ( iContentHeight > static_cast<int>( cur_rules.size() ) ?
                                  static_cast<int>( cur_rules.size() ) : iContentHeight ) ) {
                nc_color cLineColor = cur_rules[i].bActive ? c_white : c_light_gray;

                mvwprintz( w, point( 1, i - iStartPos ), cLineColor, "%d", i + 1 );
                mvwprintz( w, point( 5, i - iStartPos ), cLineColor, "" );

                if( iLine == i ) {
                    wprintz( w, c_yellow, ">> " );
                } else {
                    wprintz( w, c_yellow, "   " );
                }

                wprintz( w, iLine == i && iColumn == 1 ? hilite( cLineColor ) : cLineColor, "%s",
                         cur_rules[i].sRule.empty() ? _( "<empty rule>" ) : cur_rules[i].sRule );

                mvwprintz( w, point( 52, i - iStartPos ), iLine == i && iColumn == 2 ?
                           hilite( cLineColor ) : cLineColor, "%s",
                           cur_rules[i].bExclude ? _( "Exclude" ) :  _( "Include" ) );
            }
        }

        wnoutrefresh( w );
    } );

    bStuffChanged = false;
    input_context ctxt( "AUTO_PICKUP" );
    ctxt.register_cardinal();
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "QUIT" );
    if( tabs.size() > 1 ) {
        ctxt.register_action( "NEXT_TAB" );
        ctxt.register_action( "PREV_TAB" );
    }
    ctxt.register_action( "ADD_RULE" );
    ctxt.register_action( "REMOVE_RULE" );
    ctxt.register_action( "COPY_RULE" );
    ctxt.register_action( "ENABLE_RULE" );
    ctxt.register_action( "DISABLE_RULE" );
    ctxt.register_action( "MOVE_RULE_UP" );
    ctxt.register_action( "MOVE_RULE_DOWN" );
    ctxt.register_action( "TEST_RULE" );
    ctxt.register_action( "HELP_KEYBINDINGS" );

    const bool allow_swapping = tabs.size() == 2;
    if( allow_swapping ) {
        ctxt.register_action( "SWAP_RULE_GLOBAL_CHAR" );
    }

    if( is_autopickup ) {
        ctxt.register_action( "SWITCH_AUTO_PICKUP_OPTION" );
    }

    rml.open( autopickup_rmlui_enabled(), "autopickup", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
        data = std::make_unique<autopickup_rml_session>();
        register_autopickup_rml_types( c );
        c.Bind( "tabs", &data->tabs );
        c.Bind( "rows", &data->rows );
        c.Bind( "status_rml", &data->status_rml );
        c.Bind( "hints_rml", &data->hints_rml );
        // Click a tab to switch it (resets the cursor line); click/hover a row to
        // select it. Column navigation + all editing stay on the keyboard.
        c.BindEventCallback( "on_tab",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int idx = -1;
            if( !args.empty() ) {
                args[0].GetInto( idx );
            }
            if( idx >= 0 && idx < static_cast<int>( tabs.size() ) ) {
                iTab = idx;
                iLine = 0;
            }
        } );
        c.BindEventCallback( "on_select",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int idx = -1;
            if( !args.empty() ) {
                args[0].GetInto( idx );
            }
            if( idx >= 0 && idx < static_cast<int>( tabs[iTab].new_rules.size() ) ) {
                iLine = idx;
            }
        } );
        data->handle = c.GetModelHandle();
    } );

    while( true ) {
        rule_list &cur_rules = tabs[iTab].new_rules;

        const bool currentPageNonEmpty = !cur_rules.empty();

        ui_manager::redraw();

        const std::string action = ctxt.handle_input();

        if( action == "NEXT_TAB" ) {
            iTab++;
            if( iTab >= tabs.size() ) {
                iTab = 0;
            }
            iLine = 0;
        } else if( action == "PREV_TAB" ) {
            if( iTab > 0 ) {
                iTab--;
            } else {
                iTab = tabs.size() - 1;
            }
            iLine = 0;
        } else if( action == "QUIT" ) {
            break;
        } else if( action == "DOWN" ) {
            iLine++;
            iColumn = 1;
            if( iLine >= static_cast<int>( cur_rules.size() ) ) {
                iLine = 0;
            }
        } else if( action == "UP" ) {
            iLine--;
            iColumn = 1;
            if( iLine < 0 ) {
                iLine = cur_rules.size() - 1;
            }
        } else if( action == "REMOVE_RULE" && currentPageNonEmpty ) {
            bStuffChanged = true;
            cur_rules.erase( cur_rules.begin() + iLine );
            if( iLine > static_cast<int>( cur_rules.size() ) - 1 ) {
                iLine--;
            }
            if( iLine < 0 ) {
                iLine = 0;
            }
        } else if( action == "COPY_RULE" && currentPageNonEmpty ) {
            bStuffChanged = true;
            cur_rules.push_back( cur_rules[iLine] );
            iLine = cur_rules.size() - 1;
        } else if( allow_swapping && action == "SWAP_RULE_GLOBAL_CHAR" && currentPageNonEmpty ) {
            const size_t other_iTab = ( iTab + 1 ) % 2;
            rule_list &other_rules = tabs[other_iTab].new_rules;
            bStuffChanged = true;
            //copy over
            other_rules.push_back( cur_rules[iLine] );
            //remove old
            cur_rules.erase( cur_rules.begin() + iLine );
            iTab = other_iTab;
            iLine = other_rules.size() - 1;
        } else if( action == "ADD_RULE" || ( action == "CONFIRM" && currentPageNonEmpty ) ) {
            const int old_iLine = iLine;
            if( action == "ADD_RULE" ) {
                cur_rules.push_back( rule( "", true, false ) );
                iLine = cur_rules.size() - 1;
            }
            ui_manager::redraw();

            if( iColumn == 1 || action == "ADD_RULE" ) {
                ui_adaptor help_ui;
                catacurses::window w_help;
                const auto init_help_window = [&]( ui_adaptor & help_ui ) {
                    const point iOffset( TERMX > FULL_SCREEN_WIDTH ? ( TERMX - FULL_SCREEN_WIDTH ) / 2 : 0,
                                         TERMY > FULL_SCREEN_HEIGHT ? ( TERMY - FULL_SCREEN_HEIGHT ) / 2 : 0 );
                    w_help = catacurses::newwin( FULL_SCREEN_HEIGHT * ( 2.0 / 3 ) + 2,
                                                 FULL_SCREEN_WIDTH * 3 / 4,
                                                 iOffset + point( 19 / 2, 7 + FULL_SCREEN_HEIGHT / 2 / 2 ) );
                    help_ui.position_from_window( w_help );
                };
                init_help_window( help_ui );
                help_ui.on_screen_resize( init_help_window );

                help_ui.on_redraw( [&]( const ui_adaptor & ) {
                    // NOLINTNEXTLINE(cata-use-named-point-constants)
                    fold_and_print( w_help, point( 1, 1 ), 999, c_white,
                                    _(
                                        "* is used as a Wildcard.  A few Examples:\n"
                                        "\n"
                                        "wooden arrow    matches the itemname exactly\n"
                                        "wooden ar*      matches items beginning with wood ar\n"
                                        "*rrow           matches items ending with rrow\n"
                                        "*avy fle*fi*arrow     multiple * are allowed\n"
                                        "heAVY*woOD*arrOW      case insensitive search\n"
                                        "\n"
                                        "Pickup using:\n"
                                        "c:food          matches item in category food\n"
                                        "m:kevlar        matches items made of Kevlar\n"
                                        "M:copper        matches items made purely of copper\n"
                                        "q:drilling      matches items with drilling qualites\n"
                                        "k:fabrication   matches books that teach fabrication\n"
                                        "b:c:food;*meat* matches items that are foods and *meat*"

                                    )
                                  );

                    draw_border( w_help );
                    wnoutrefresh( w_help );
                } );
                const std::string r = string_input_popup()
                                      .title( _( "Pickup Rule:" ) )
                                      .width( 30 )
                                      .text( cur_rules[iLine].sRule )
                                      .query_string();
                // If r is empty, then either (1) The player ESC'ed from the window (changed their mind), or
                // (2) Explicitly entered an empty rule- which isn't allowed since "*" should be used
                // to include/exclude everything
                if( !r.empty() ) {
                    cur_rules[iLine].set_rule_string( r );
                    bStuffChanged = true;
                } else if( action == "ADD_RULE" ) {
                    cur_rules.pop_back();
                    iLine = old_iLine;
                }
            } else if( iColumn == 2 ) {
                bStuffChanged = true;
                cur_rules[iLine].bExclude = !cur_rules[iLine].bExclude;
            }
        } else if( action == "ENABLE_RULE" && currentPageNonEmpty ) {
            bStuffChanged = true;
            cur_rules[iLine].bActive = true;
        } else if( action == "DISABLE_RULE" && currentPageNonEmpty ) {
            bStuffChanged = true;
            cur_rules[iLine].bActive = false;
        } else if( action == "LEFT" ) {
            iColumn--;
            if( iColumn < 1 ) {
                iColumn = iTotalCols;
            }
        } else if( action == "RIGHT" ) {
            iColumn++;
            if( iColumn > iTotalCols ) {
                iColumn = 1;
            }
        } else if( action == "MOVE_RULE_UP" && currentPageNonEmpty ) {
            bStuffChanged = true;
            if( iLine < static_cast<int>( cur_rules.size() ) - 1 ) {
                std::swap( cur_rules[iLine], cur_rules[iLine + 1] );
                iLine++;
                iColumn = 1;
            }
        } else if( action == "MOVE_RULE_DOWN" && currentPageNonEmpty ) {
            bStuffChanged = true;
            if( iLine > 0 ) {
                std::swap( cur_rules[iLine], cur_rules[iLine - 1] );
                iLine--;
                iColumn = 1;
            }
        } else if( action == "TEST_RULE" && currentPageNonEmpty && !g->u.name.empty() ) {
            test_pattern( cur_rules[iLine] );
        } else if( action == "SWITCH_AUTO_PICKUP_OPTION" ) {
            // TODO: Now that NPCs use this function, it could be used for them too
            get_options().get_option( "AUTO_PICKUP" ).setNext();
            get_options().save();
        }
    }

    // Tear down the RmlUi document while the bound `data` is still alive. close()
    // is idempotent and a no-op when the curses path ran; safe before the early
    // returns below.
    rml.close();

    if( !bStuffChanged ) {
        return;
    }

    if( !query_yn( _( "Save changes?" ) ) ) {
        return;
    }

    for( tab &t : tabs ) {
        t.rules.get() = t.new_rules;
    }
}


void user_interface::test_pattern( const rule &rule ) const
{
    std::vector<std::string> vMatchingItems;

    if( rule.sRule.empty() ) {
        return;
    }

    //Loop through all itemfactory items
    //APU now ignores prefixes, bottled items and suffix combinations still not generated
    for( const itype *e : item_controller->all() ) {
        if( rule( *e ) ) {
            vMatchingItems.push_back( e->nname( 1 ) );
        }
    }

    int iStartPos = 0;
    int iContentHeight = 0;
    int iContentWidth = 0;

    catacurses::window w_test_rule_border;
    catacurses::window w_test_rule_content;

    ui_adaptor ui;

    const auto init_windows = [&]( ui_adaptor & ui ) {
        const point iOffset( 15 + ( TERMX > FULL_SCREEN_WIDTH ? ( TERMX - FULL_SCREEN_WIDTH ) / 2 : 0 ),
                             5 + ( TERMY > FULL_SCREEN_HEIGHT ? ( TERMY - FULL_SCREEN_HEIGHT ) / 2 :
                                   0 ) );
        iContentHeight = FULL_SCREEN_HEIGHT - 8;
        iContentWidth = FULL_SCREEN_WIDTH - 30;

        w_test_rule_border = catacurses::newwin( iContentHeight + 2, iContentWidth,
                             iOffset );
        w_test_rule_content = catacurses::newwin( iContentHeight,
                              iContentWidth - 2,
                              iOffset + point_south_east );

        ui.position_from_window( w_test_rule_border );
    };
    init_windows( ui );
    ui.on_screen_resize( init_windows );

    int nmatch = vMatchingItems.size();
    const std::string buf = string_format( vgettext( "%1$d item matches: %2$s",
                                           "%1$d items match: %2$s",
                                           nmatch ), nmatch, rule.sRule );

    int iLine = 0;

    input_context ctxt( "AUTO_PICKUP_TEST" );
    ctxt.register_updown();
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );

    ui.on_redraw( [&]( const ui_adaptor & ) {
        draw_border( w_test_rule_border, BORDER_COLOR, buf, hilite( c_white ) );
        center_print( w_test_rule_border, iContentHeight + 1, red_background( c_white ),
                      _( "Won't display content or suffix matches" ) );
        wnoutrefresh( w_test_rule_border );

        // Clear the lines
        for( int i = 0; i < iContentHeight; i++ ) {
            for( int j = 0; j < 79; j++ ) {
                mvwputch( w_test_rule_content, point( j, i ), c_black, ' ' );
            }
        }

        calcStartPos( iStartPos, iLine, iContentHeight, vMatchingItems.size() );

        // display auto pickup
        for( int i = iStartPos; i < static_cast<int>( vMatchingItems.size() ); i++ ) {
            if( i >= iStartPos &&
                i < iStartPos + ( iContentHeight > static_cast<int>( vMatchingItems.size() ) ?
                                  static_cast<int>( vMatchingItems.size() ) : iContentHeight ) ) {
                nc_color cLineColor = c_white;

                mvwprintz( w_test_rule_content, point( 0, i - iStartPos ), cLineColor, "%d", i + 1 );
                mvwprintz( w_test_rule_content, point( 4, i - iStartPos ), cLineColor, "" );

                if( iLine == i ) {
                    wprintz( w_test_rule_content, c_yellow, ">> " );
                } else {
                    wprintz( w_test_rule_content, c_yellow, "   " );
                }

                wprintz( w_test_rule_content, iLine == i ? hilite( cLineColor ) : cLineColor, vMatchingItems[i] );
            }
        }

        wnoutrefresh( w_test_rule_content );
    } );

    while( true ) {
        ui_manager::redraw();

        const std::string action = ctxt.handle_input();
        if( action == "DOWN" ) {
            iLine++;
            if( iLine >= static_cast<int>( vMatchingItems.size() ) ) {
                iLine = 0;
            }
        } else if( action == "UP" ) {
            iLine--;
            if( iLine < 0 ) {
                iLine = vMatchingItems.size() - 1;
            }
        } else if( action == "QUIT" ) {
            break;
        }
    }
}

void player_settings::show()
{
    user_interface ui;

    ui.title = _( " AUTO PICKUP MANAGER " );
    ui.tabs.emplace_back( _( "[<Global>]" ), global_rules );
    if( !g->u.name.empty() ) {
        ui.tabs.emplace_back( _( "[<Character>]" ), character_rules );
    }
    ui.is_autopickup = true;

    ui.show();

    if( !ui.bStuffChanged ) {
        return;
    }

    save_global();
    if( !g->u.name.empty() ) {
        save_character();
    }

    refresh_map_items( map_items );
}

bool player_settings::has_rule( const item *it )
{
    const std::string &name = it->tname( 1 );
    for( auto &elem : character_rules ) {
        if( name.length() == elem.sRule.length() && ci_find_substr( name, elem.sRule ) != -1 ) {
            return true;
        }
    }
    return false;
}

void player_settings::add_rule( const item *it )
{
    character_rules.push_back( rule( it->tname( 1, false ), true, false ) );
    invalidate();

    if( !get_option<bool>( "AUTO_PICKUP" ) &&
        query_yn( _( "Autopickup is not enabled in the options.  Enable it now?" ) ) ) {
        get_options().get_option( "AUTO_PICKUP" ).setNext();
        get_options().save();
    }
}

void player_settings::remove_rule( const item *it )
{
    const std::string sRule = it->tname( 1, false );
    for( rule_list::iterator candidate = character_rules.begin();
         candidate != character_rules.end(); ++candidate ) {
        if( sRule.length() == candidate->sRule.length() &&
            ci_find_substr( sRule, candidate->sRule ) != -1 ) {
            character_rules.erase( candidate );
            invalidate();
            break;
        }
    }
}

bool player_settings::empty() const
{
    return global_rules.empty() && character_rules.empty();
}

bool check_special_rule( const std::vector<material_id> &materials, const std::string &rule )
{
    char type = ' ';
    std::vector<std::string> filter;
    if( rule[1] == ':' ) {
        type = rule[0];
        filter = string_split( rule.substr( 2 ), ',' );
    }

    if( filter.empty() || materials.empty() ) {
        return false;
    }

    if( type == 'm' ) {
        return std::ranges::any_of( materials, [&filter]( const material_id & mat ) {
            return std::ranges::any_of( filter, [&mat]( const std::string & search ) {
                return lcmatch( mat->name(), search );
            } );
        } );

    } else if( type == 'M' ) {
        return std::ranges::all_of( materials, [&filter]( const material_id & mat ) {
            return std::ranges::any_of( filter, [&mat]( const std::string & search ) {
                return lcmatch( mat->name(), search );
            } );
        } );
    }

    return false;
}

void player_settings::refresh_map_items( item_search_cache &map_items ) const
{
    //process include/exclude in order of rules, global first, then character specific
    //if a specific item is being added, all the rules need to be checked now
    //may have some performance issues since exclusion needs to check all items also
    map_items.clear_items();
    map_items.apply_rules( global_rules );
    map_items.apply_rules( character_rules );

}

rule_state base_settings::check_item( const item &item )
{
    if( !cache_is_valid ) {
        refresh_map_items( map_items );
        cache_is_valid = true;
    }

    const auto iter = map_items.find( item.typeId() );
    if( iter != map_items.end() ) {
        return iter->second;
    }

    return RULE_NONE;
}

void base_settings::invalidate()
{
    cache_is_valid = false;
}

void player_settings::clear_character_rules()
{
    character_rules.clear();
    invalidate();
}

bool player_settings::save_character()
{
    return save( true );
}

bool player_settings::save_global()
{
    return save( false );
}

bool player_settings::save( const bool bCharacter )
{
    if( bCharacter ) {
        //Character not saved yet.
        if( !g->get_active_world()->player_file_exist( ".sav" ) ) {
            return true;
        }

        return g->get_active_world()->write_to_player_file( ".apu.json", [&]( std::ostream & fout ) {
            JsonOut jout( fout, true );
            ( bCharacter ? character_rules : global_rules ).serialize( jout );
        }, _( "autopickup configuration" ) );
    } else {
        return write_to_file( PATH_INFO::autopickup(), [&]( std::ostream & fout ) {
            JsonOut jout( fout, true );
            ( bCharacter ? character_rules : global_rules ).serialize( jout );
        }, _( "autopickup configuration" ) );
    }
}

void player_settings::load_character()
{
    load( true );

}

void player_settings::load_global()
{
    load( false );
}

void player_settings::load( const bool bCharacter )
{
    if( bCharacter ) {
        g->get_active_world()->read_from_player_file_json( ".apu.json", [&]( JsonIn & jsin ) {
            ( bCharacter ? character_rules : global_rules ).deserialize( jsin );
        }, true );
    } else {
        read_from_file_json( PATH_INFO::autopickup(), [&]( JsonIn & jsin ) {
            ( bCharacter ? character_rules : global_rules ).deserialize( jsin );
        }, true );
    }
    // Don't eagerly refresh here: load_global() runs during game::load_static_data,
    // before item types are frozen, so refresh_map_items() -> Item_factory::all()
    // would assert. The cache is rebuilt lazily on first check_item() in-game.
    invalidate();
}

void npc_settings::show( const std::string &name )
{
    user_interface ui;
    ui.title = string_format( _( "Pickup rules for %s" ), name );
    ui.tabs.emplace_back( name, rules );
    ui.show();
    // Don't need to save the rules here, it will be save along with the NPC object itself.
    if( !ui.bStuffChanged ) {
        return;
    }
    invalidate();
}

void npc_settings::serialize( JsonOut &jsout ) const
{
    rules.serialize( jsout );
}

void npc_settings::deserialize( JsonIn &jsin )
{
    rules.deserialize( jsin );
}

void npc_settings::refresh_map_items( item_search_cache &map_items ) const
{
    map_items.clear_items();
    map_items.apply_rules( rules );
}

bool npc_settings::empty() const
{
    return rules.empty();
}

void base_settings::recreate() const
{
    map_items.clear_items();
    refresh_map_items( map_items );
}

