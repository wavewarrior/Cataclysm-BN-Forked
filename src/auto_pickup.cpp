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
#include "item_search.h"

#include <RmlUi/Core.h>

#include "rml_screen.h"
#include "lighting/rmlui_layer.h"
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

// Nested test-rule popup (user_interface::test_pattern): a centered box listing
// the item names a rule matches, stacked over the still-open "autopickup" doc.
// Render-only — the keyboard owns up/down/quit; mouse click/hover moves the cursor.
struct autopickup_test_row {
    Rml::String num_rml;
    Rml::String name_rml;
    bool selected = false;
};
struct autopickup_test_session {
    Rml::String title_rml;
    Rml::Vector<autopickup_test_row> rows;
    Rml::DataModelHandle handle;
};

bool g_autopickup_test_types_registered = false;

void register_autopickup_test_rml_types( Rml::DataModelConstructor &c )
{
    if( g_autopickup_test_types_registered ) {
        return;
    }
    Rml::StructHandle<autopickup_test_row> rh = c.RegisterStruct<autopickup_test_row>();
    rh.RegisterMember( "num_rml", &autopickup_test_row::num_rml );
    rh.RegisterMember( "name_rml", &autopickup_test_row::name_rml );
    rh.RegisterMember( "selected", &autopickup_test_row::selected );
    c.RegisterArray<Rml::Vector<autopickup_test_row>>();
    g_autopickup_test_types_registered = true;
}
} // namespace

bool &autopickup_rmlui_enabled()
{
    // Default OFF — opt in via the F4 panel. See rml_screen.h.
    static bool enabled = true;
    return enabled;
}

using namespace auto_pickup;

bool auto_pickup::test_pattern_function( const itype &type, std::string filter )
{
    auto func = filter_from_string<itype>( filter, wildcard_itype_filter );
    return func( type );
}

bool auto_pickup::autopickup_item_function( const item &object, std::string filter )
{
    auto func = filter_from_string<item>( filter, wildcard_item_filter );
    return func( object );
}

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

                // RmlUi backdrop: a passive static help doc stacked under the
                // string_input popup, replacing the curses help window. No data
                // model — the text is literal English in the .rml (same i18n gap
                // as the column heads); closed after the rule entry below.
                Rml::ElementDocument *help_doc = nullptr;
                if( autopickup_rmlui_enabled() && rmlui_layer::ready() ) {
                    help_doc = rmlui_layer::open_document(
                                   PATH_INFO::datadir() + "gui/autopickup_help.rml", true );
                }

                help_ui.on_redraw( []( const ui_adaptor & ) {
                    // RmlUi help doc owns this; curses fallback removed (rip-out B).
                } );
                const std::string r = string_input_popup()
                                      .title( _( "Pickup Rule:" ) )
                                      .width( 30 )
                                      .text( cur_rules[iLine].sRule )
                                      .query_string();
                if( help_doc ) {
                    rmlui_layer::close_document( help_doc );
                }
                // If r is empty, then either (1) The player ESC'ed from the window (changed their mind), or
                // (2) Explicitly entered an empty rule- which isn't allowed since "*" should be used
                // to include/exclude everything
                if( !r.empty() ) {
                    cur_rules[iLine].sRule = wildcard_trim_rule( r );
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
            cur_rules[iLine].test_pattern();
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
    invalidate();
}

void rule::test_pattern() const
{
    std::vector<std::string> vMatchingItems;

    if( sRule.empty() ) {
        return;
    }

    //Loop through all itemfactory items
    //APU now ignores prefixes, bottled items and suffix combinations still not generated
    for( const itype *e : item_controller->all() ) {
        const std::string sItemName = e->nname( 1 );
        if( !test_pattern_function( *e, sRule ) && !wildcard_match( sItemName, sRule ) ) {
            continue;
        }

        vMatchingItems.push_back( sItemName );
    }

    int iContentHeight = 0;
    int iContentWidth = 0;

    catacurses::window w_test_rule_border;

    ui_adaptor ui;

    const auto init_windows = [&]( ui_adaptor & ui ) {
        const point iOffset( 15 + ( TERMX > FULL_SCREEN_WIDTH ? ( TERMX - FULL_SCREEN_WIDTH ) / 2 : 0 ),
                             5 + ( TERMY > FULL_SCREEN_HEIGHT ? ( TERMY - FULL_SCREEN_HEIGHT ) / 2 :
                                   0 ) );
        iContentHeight = FULL_SCREEN_HEIGHT - 8;
        iContentWidth = FULL_SCREEN_WIDTH - 30;

        w_test_rule_border = catacurses::newwin( iContentHeight + 2, iContentWidth,
                             iOffset );

        ui.position_from_window( w_test_rule_border );
    };
    init_windows( ui );
    ui.on_screen_resize( init_windows );

    int nmatch = vMatchingItems.size();
    const std::string buf = string_format( vgettext( "%1$d item matches: %2$s",
                                           "%1$d items match: %2$s",
                                           nmatch ), nmatch, sRule );

    int iLine = 0;

    input_context ctxt( "AUTO_PICKUP_TEST" );
    ctxt.register_updown();
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );

    // RmlUi render path. Render-only: the loop below owns nav; the doc stacks over
    // the still-open "autopickup" doc (centered box, rules screen behind), matching
    // the curses sub-window. Model storage declared BEFORE rml so it outlives the doc.
    std::unique_ptr<autopickup_test_session> tdata;
    rml_doc test_rml;
    const auto sync_test_rml = [&]() {
        if( !test_rml ) {
            return;
        }
        tdata->title_rml = cata_text_to_rml( buf );
        tdata->rows.clear();
        for( int i = 0; i < static_cast<int>( vMatchingItems.size() ); ++i ) {
            autopickup_test_row r;
            r.num_rml = cata_text_to_rml( string_format( "%d", i + 1 ) );
            r.name_rml = cata_text_to_rml( vMatchingItems[i] );
            r.selected = ( iLine == i );
            tdata->rows.push_back( r );
        }
        tdata->handle.DirtyVariable( "title_rml" );
        tdata->handle.DirtyVariable( "rows" );
    };

    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( test_rml ) {
            sync_test_rml();
        }
        // RmlUi owns the screen; curses fallback removed (rip-out B).
    } );

    test_rml.open( autopickup_rmlui_enabled(), "autopickup_test", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
        tdata = std::make_unique<autopickup_test_session>();
        register_autopickup_test_rml_types( c );
        c.Bind( "title_rml", &tdata->title_rml );
        c.Bind( "rows", &tdata->rows );
        // Click/hover a row to move the cursor onto it; QUIT (keyboard) closes.
        c.BindEventCallback( "on_select",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int idx = -1;
            if( !args.empty() ) {
                args[0].GetInto( idx );
            }
            if( idx >= 0 && idx < static_cast<int>( vMatchingItems.size() ) ) {
                iLine = idx;
            }
        } );
        tdata->handle = c.GetModelHandle();
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
    create_rule( it );

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

void npc_settings::create_rule( const std::string &to_match )
{
    rules.create_rule( map_items, to_match );
}

void rule_list::create_rule( cache &map_items, const std::string &to_match )
{
    for( const rule &elem : *this ) {
        if( !elem.bActive || !wildcard_match( to_match, elem.sRule ) ) {
            continue;
        }

        map_items[ to_match ] = elem.bExclude ? RULE_BLACKLISTED : RULE_WHITELISTED;
    }
}

void player_settings::create_rule( const item *it )
{
    // TODO: change it to be a reference
    global_rules.create_rule( map_items, *it );
    character_rules.create_rule( map_items, *it );
}

void rule_list::create_rule( cache &map_items, const item &it )
{
    const std::string to_match = it.tname( 1, false );

    for( const rule &elem : *this ) {
        if( !elem.bActive ) {
            continue;
        } else if( !autopickup_item_function( it, elem.sRule ) &&
                   !wildcard_match( to_match, elem.sRule ) ) {
            continue;
        }

        map_items[ to_match ] = elem.bExclude ? RULE_BLACKLISTED : RULE_WHITELISTED;
    }
}

void player_settings::refresh_map_items( cache &map_items ) const
{
    //process include/exclude in order of rules, global first, then character specific
    //if a specific item is being added, all the rules need to be checked now
    //may have some performance issues since exclusion needs to check all items also
    global_rules.refresh_map_items( map_items );
    character_rules.refresh_map_items( map_items );
}

void rule_list::refresh_map_items( cache &map_items ) const
{
for( const rule &elem : *this ) {
    if( elem.sRule.empty() || !elem.bActive ) {
            continue;
        }

        if( !elem.bExclude ) {
            //Check include patterns against all itemfactory items
            for( const itype *e : item_controller->all() ) {
                const std::string &cur_item = e->nname( 1 );

                if( !test_pattern_function( *e, elem.sRule ) && !wildcard_match( cur_item, elem.sRule ) ) {
                    continue;
                }

                map_items[ cur_item ] = RULE_WHITELISTED;
                map_items.temp_items[ cur_item ] = e;
            }
        } else {
            //only re-exclude items from the existing mapping for now
            //new exclusions will process during pickup attempts
            for( auto &map_item : map_items ) {
                if( !test_pattern_function( *( map_items.temp_items[ map_item.first ] ), elem.sRule ) &&
                    !wildcard_match( map_item.first, elem.sRule ) ) {
                    continue;
                }

                map_items[ map_item.first ] = RULE_BLACKLISTED;
            }
        }
    }
}

rule_state base_settings::check_item( const std::string &sItemName ) const
{
    if( !map_items.ready ) {
    recreate();
    }

    const auto iter = map_items.find( sItemName );
    if( iter != map_items.end() ) {
        return iter->second;
    }

    return RULE_NONE;
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

void rule::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "rule", sRule );
    jsout.member( "active", bActive );
    jsout.member( "exclude", bExclude );
    jsout.end_object();
}

void rule_list::serialize( JsonOut &jsout ) const
{
    jsout.start_array();
for( const rule &elem : *this ) {
    elem.serialize( jsout );
    }
    jsout.end_array();
}

void rule::deserialize( JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    sRule = jo.get_string( "rule" );
    bActive = jo.get_bool( "active" );
    bExclude = jo.get_bool( "exclude" );
}

void rule_list::deserialize( JsonIn &jsin )
{
    clear();

    jsin.start_array();
    while( !jsin.end_array() ) {
        rule tmp;
        tmp.deserialize( jsin );
        push_back( tmp );
    }
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

void npc_settings::refresh_map_items( cache &map_items ) const
{
    rules.refresh_map_items( map_items );
}

bool npc_settings::empty() const
{
    return rules.empty();
}

void base_settings::recreate() const
{
    map_items.clear();
    map_items.temp_items.clear();
    refresh_map_items( map_items );
    map_items.ready = true;
    map_items.temp_items.clear();
}

void base_settings::invalidate()
{
    map_items.ready = false;
}
