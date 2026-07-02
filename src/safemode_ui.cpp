#include "safemode_ui.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "avatar.h"
#include "cata_utility.h"
#include "color.h"
#include "cursesdef.h"
#include "debug.h"
#include "filesystem.h"
#include "fstream_utils.h"
#include "game.h"
#include "input.h"
#include "json.h"
#include "monstergenerator.h"
#include "mtype.h"
#include "options.h"
#include "output.h"
#include "path_info.h"
#include "point.h"
#include "string_formatter.h"
#include "string_input_popup.h"
#include "string_utils.h"
#include "translations.h"
#include "ui_manager.h"
#include "world.h"

#include <RmlUi/Core.h>

#include "rml_screen.h"
#include "rml_util.h"
#include "lighting/rmlui_layer.h"

safemode &get_safemode()
{
    static safemode single_instance;
    return single_instance;
}

// ── RmlUi render path (full UI→RmlUi migration, Tier 2 screen #3) ─────────────
// 5th rml_doc consumer. Global/Character tabs (shared theme .tabs/.tab) over a
// 5-column rules table (#/Rule/Attitude/Dist/B-W/Cat). The cursor is 2D
// (line + column): the active CELL is highlighted, not the whole row. All editing
// (add/remove/copy/move/enable/disable/edit-via-popup) stays on input_context —
// the RmlUi job is rendering the table read-only + the cell highlight; rule text
// editing uses the already-migrated string_input_popup. Per-cell colour baked via
// cata_text_to_rml; the active cell is marked by sel_col (== column when the
// row is the cursor line, else -1) so RCSS can highlight exactly one cell.
namespace
{
struct safemode_rml_row {
    Rml::String num_rml;
    Rml::String rule_rml;
    Rml::String attitude_rml;
    Rml::String proximity_rml;
    Rml::String wblist_rml;
    Rml::String category_rml;
    bool selected = false;   // this row is the cursor line
    int sel_col = -1;        // active column on this row (-1 = none)
};
struct safemode_rml_session {
    Rml::Vector<safemode_rml_row> rows;
    bool global_tab = false;          // tab == GLOBAL_TAB
    Rml::String global_tab_label_rml;
    Rml::String character_tab_label_rml;
    Rml::String status_rml;           // "Safe Mode enabled: True/False" (safemode use only)
    Rml::String hints_rml;            // hotkey hints
    bool empty = false;
    Rml::String empty_rml;
    Rml::DataModelHandle handle;
};

bool g_safemode_types_registered = false;

void register_safemode_rml_types( Rml::DataModelConstructor &c )
{
    if( g_safemode_types_registered ) {
        return;
    }
    Rml::StructHandle<safemode_rml_row> rh = c.RegisterStruct<safemode_rml_row>();
    rh.RegisterMember( "num_rml", &safemode_rml_row::num_rml );
    rh.RegisterMember( "rule_rml", &safemode_rml_row::rule_rml );
    rh.RegisterMember( "attitude_rml", &safemode_rml_row::attitude_rml );
    rh.RegisterMember( "proximity_rml", &safemode_rml_row::proximity_rml );
    rh.RegisterMember( "wblist_rml", &safemode_rml_row::wblist_rml );
    rh.RegisterMember( "category_rml", &safemode_rml_row::category_rml );
    rh.RegisterMember( "selected", &safemode_rml_row::selected );
    rh.RegisterMember( "sel_col", &safemode_rml_row::sel_col );
    c.RegisterArray<Rml::Vector<safemode_rml_row>>();
    g_safemode_types_registered = true;
}

// test_pattern popup (safemode::test_pattern): a centered box over the still-open
// "safemode" rules screen listing the monster names a rule matches. Render-only —
// the keyboard owns up/down + quit; mouse click/hover moves the cursor. Mirrors the
// autopickup_test twin.
struct safemode_test_row {
    Rml::String num_rml;
    Rml::String name_rml;
    bool selected = false;
};
struct safemode_test_session {
    Rml::String title_rml;
    Rml::Vector<safemode_test_row> rows;
    Rml::DataModelHandle handle;
};

bool g_safemode_test_types_registered = false;

void register_safemode_test_rml_types( Rml::DataModelConstructor &c )
{
    if( g_safemode_test_types_registered ) {
        return;
    }
    Rml::StructHandle<safemode_test_row> rh = c.RegisterStruct<safemode_test_row>();
    rh.RegisterMember( "num_rml", &safemode_test_row::num_rml );
    rh.RegisterMember( "name_rml", &safemode_test_row::name_rml );
    rh.RegisterMember( "selected", &safemode_test_row::selected );
    c.RegisterArray<Rml::Vector<safemode_test_row>>();
    g_safemode_test_types_registered = true;
}
} // namespace

bool &safemode_rmlui_enabled()
{
    // Default OFF — opt in via the F4 panel. See rml_screen.h.
    static bool enabled = true;
    return enabled;
}

void safemode::show()
{
    show( _( " SAFE MODE MANAGER " ), true );
}

std::string safemode::npc_type_name()
{
    static std::string name = "human";
    return name;
}

void safemode::show( const std::string &custom_name_in, bool is_safemode_in )
{
    auto global_rules_old = global_rules;
    auto character_rules_old = character_rules;

    const int header_height = 4;
    int content_height = 0;

    enum Columns : int {
        COLUMN_RULE,
        COLUMN_ATTITUDE,
        COLUMN_PROXIMITY,
        COLUMN_WHITE_BLACKLIST,
        COLUMN_CATEGORY
    };

    std::map<int, int> column_pos;
    column_pos[COLUMN_RULE] = 4;
    column_pos[COLUMN_ATTITUDE] = column_pos[COLUMN_RULE] + 38;
    column_pos[COLUMN_PROXIMITY] = column_pos[COLUMN_ATTITUDE] + 10;
    column_pos[COLUMN_WHITE_BLACKLIST] = column_pos[COLUMN_PROXIMITY] + 6;
    column_pos[COLUMN_CATEGORY] = column_pos[COLUMN_WHITE_BLACKLIST] + 11;

    const int num_columns = column_pos.size();

    catacurses::window w_border;
    catacurses::window w_header;
    catacurses::window w;

    ui_adaptor ui;

    const auto init_windows = [&]( ui_adaptor & ui ) {
        content_height = FULL_SCREEN_HEIGHT - 2 - header_height;

        const point offset( TERMX > FULL_SCREEN_WIDTH ? ( TERMX - FULL_SCREEN_WIDTH ) / 2 : 0,
                            TERMY > FULL_SCREEN_HEIGHT ? ( TERMY - FULL_SCREEN_HEIGHT ) / 2 : 0 );

        w_border = catacurses::newwin( FULL_SCREEN_HEIGHT, FULL_SCREEN_WIDTH, offset );
        w_header = catacurses::newwin( header_height, FULL_SCREEN_WIDTH - 2,
                                       offset + point_south_east );
        w = catacurses::newwin( content_height, FULL_SCREEN_WIDTH - 2,
                                offset + point( 1, header_height + 1 ) );

        ui.position_from_window( w_border );
    };
    init_windows( ui );
    ui.on_screen_resize( init_windows );

    int tab = GLOBAL_TAB;
    int line = 0;
    int column = 0;
    int start_pos = 0;
    bool changes_made = false;
    input_context ctxt( "SAFEMODE" );
    ctxt.register_cardinal();
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "NEXT_TAB" );
    ctxt.register_action( "PREV_TAB" );
    ctxt.register_action( "ADD_DEFAULT_RULESET" );
    ctxt.register_action( "ADD_RULE" );
    ctxt.register_action( "REMOVE_RULE" );
    ctxt.register_action( "COPY_RULE" );
    ctxt.register_action( "ENABLE_RULE" );
    ctxt.register_action( "DISABLE_RULE" );
    ctxt.register_action( "MOVE_RULE_UP" );
    ctxt.register_action( "MOVE_RULE_DOWN" );
    ctxt.register_action( "TEST_RULE" );
    ctxt.register_action( "HELP_KEYBINDINGS" );

    if( is_safemode_in ) {
        ctxt.register_action( "SWITCH_SAFEMODE_OPTION" );
        ctxt.register_action( "SWAP_RULE_GLOBAL_CHAR" );
    }

    // ---- RmlUi render path (F.3 rml_doc harness) ----------------------------
    // rml_doc owns the open/guard/16ms-tick/close lifecycle; only the model + this
    // live sync stay here. sync_rml() runs in on_redraw, so the tabs, the table,
    // the cell highlight, and the status all track the 2D cursor (line/column).
    // All editing stays in the loop below (keyboard + string_input_popup); the
    // RmlUi doc is render-only. Model storage declared BEFORE rml so it outlives
    // the document.
    std::unique_ptr<safemode_rml_session> data;
    rml_doc rml;
    const auto sync_rml = [&]() {
        if( !rml ) {
            return;
        }
        auto &cur = ( tab == GLOBAL_TAB ) ? global_rules : character_rules;

        data->global_tab = ( tab == GLOBAL_TAB );
        data->global_tab_label_rml = cata_text_to_rml( _( "Global" ) );
        data->character_tab_label_rml = cata_text_to_rml( _( "Character" ) );

        // Hotkey hints (shortcut_text colourises the bracketed letter, matching
        // the legacy shortcut_print header).
        std::string hints;
        for( const std::string &hk : {
                 translate_marker( "<A>dd" ), translate_marker( "<R>emove" ), translate_marker( "<C>opy" ),
                 translate_marker( "<M>ove" ), translate_marker( "<E>nable" ), translate_marker( "<D>isable" ),
                 translate_marker( "<T>est" )
             } ) {
            hints += shortcut_text( c_light_green, _( hk ) ) + "  ";
        }
        hints += shortcut_text( c_light_green, _( "<+-> Move up/down" ) ) + "  ";
        hints += shortcut_text( c_light_green, _( "<Enter>-Edit" ) ) + "  ";
        hints += shortcut_text( c_light_green, _( "<Tab>-Switch Page" ) );
        data->hints_rml = cata_text_to_rml( hints );

        if( is_safemode_in ) {
            const bool on = get_option<bool>( "SAFEMODE" );
            data->status_rml = cata_text_to_rml( string_format( _( "Safe Mode enabled: %s  %s" ),
                                                 colorize( on ? _( "True" ) : _( "False" ), on ? c_light_green : c_light_red ),
                                                 shortcut_text( c_light_green, _( "<S>witch" ) ) ) );
        } else {
            data->status_rml.clear();
        }

        const bool char_no_name = ( tab == CHARACTER_TAB && g->u.name.empty() );
        data->empty = char_no_name || cur.empty();
        if( char_no_name ) {
            data->empty_rml = cata_text_to_rml(
                                  _( "Please load a character first to use this page!" ) );
        } else if( cur.empty() ) {
            data->empty_rml = cata_text_to_rml( string_format( "%s\n%s\n%s",
                                                _( "Safe Mode manager currently inactive." ),
                                                _( "Default rules are used.  Add a rule to activate." ),
                                                _( "Press ~ to add a default ruleset to get started." ) ) );
        }

        data->rows.clear();
        for( int i = 0; i < static_cast<int>( cur.size() ); ++i ) {
            const auto &rule = cur[i];
            const nc_color col = rule.active ? c_white : c_light_gray;
            safemode_rml_row r;
            r.num_rml = cata_text_to_rml( colorize( string_format( "%d", i + 1 ), col ) );
            r.rule_rml = cata_text_to_rml( colorize(
                                               rule.rule.empty() ? _( "<empty rule>" ) : rule.rule, col ) );
            r.attitude_rml = cata_text_to_rml( colorize(
                                                   rule.category == Categories::HOSTILE_SPOTTED
                                                   ? Creature::get_attitude_ui_data( rule.attitude ).first.translated() : "---", col ) );
            r.proximity_rml = cata_text_to_rml( colorize(
                                                    ( rule.category == Categories::SOUND || !rule.whitelist )
                                                    ? std::to_string( rule.proximity ) : "---", col ) );
            r.wblist_rml = cata_text_to_rml( colorize(
                                                 rule.whitelist ? _( "Whitelist" ) : _( "Blacklist" ), col ) );
            r.category_rml = cata_text_to_rml( colorize(
                                                   rule.category == Categories::SOUND ? _( "Sound" ) : _( "Hostile" ), col ) );
            r.selected = ( line == i );
            r.sel_col = ( line == i ) ? column : -1;
            data->rows.push_back( r );
        }

        data->handle.DirtyVariable( "rows" );
        data->handle.DirtyVariable( "global_tab" );
        data->handle.DirtyVariable( "global_tab_label_rml" );
        data->handle.DirtyVariable( "character_tab_label_rml" );
        data->handle.DirtyVariable( "status_rml" );
        data->handle.DirtyVariable( "hints_rml" );
        data->handle.DirtyVariable( "empty" );
        data->handle.DirtyVariable( "empty_rml" );
    };

    rml.open( safemode_rmlui_enabled(), "safemode", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
        data = std::make_unique<safemode_rml_session>();
        register_safemode_rml_types( c );
        c.Bind( "rows", &data->rows );
        c.Bind( "global_tab", &data->global_tab );
        c.Bind( "global_tab_label_rml", &data->global_tab_label_rml );
        c.Bind( "character_tab_label_rml", &data->character_tab_label_rml );
        c.Bind( "status_rml", &data->status_rml );
        c.Bind( "hints_rml", &data->hints_rml );
        c.Bind( "empty", &data->empty );
        c.Bind( "empty_rml", &data->empty_rml );
        // Click a tab to switch it (resets the cursor line); click/hover a row to
        // select it. Column navigation + all editing stay on the keyboard.
        c.BindEventCallback( "on_tab",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int idx = -1;
            if( !args.empty() ) {
                args[0].GetInto( idx );
            }
            const int want = ( idx == 0 ) ? GLOBAL_TAB : CHARACTER_TAB;
            if( want != tab ) {
                tab = want;
                line = 0;
            }
        } );
        c.BindEventCallback( "on_select",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int idx = -1;
            if( !args.empty() ) {
                args[0].GetInto( idx );
            }
            auto &curl = ( tab == GLOBAL_TAB ) ? global_rules : character_rules;
            if( idx >= 0 && idx < static_cast<int>( curl.size() ) ) {
                line = idx;
            }
        } );
        data->handle = c.GetModelHandle();
    } );

    ui.on_redraw( [&]( const ui_adaptor & ) {
        // RmlUi path owns the screen — sync the model and skip curses drawing.
        if( rml ) {
            sync_rml();
            return;
        }
    } );

    while( true ) {
        auto &current_tab = ( tab == GLOBAL_TAB ) ? global_rules : character_rules;

        ui_manager::redraw();

        const std::string action = ctxt.handle_input();

        if( action == "NEXT_TAB" ) {
            tab++;
            if( tab >= MAX_TAB ) {
                tab = 0;
            }
            line = 0;
        } else if( action == "PREV_TAB" ) {
            tab--;
            if( tab < 0 ) {
                tab = MAX_TAB - 1;
            }
            line = 0;
        } else if( action == "QUIT" ) {
            break;
        } else if( tab == CHARACTER_TAB && g->u.name.empty() ) {
            //Only allow loaded games to use the char sheet
        } else if( action == "DOWN" ) {
            line++;
            if( line >= static_cast<int>( current_tab.size() ) ) {
                line = 0;
            }
        } else if( action == "UP" ) {
            line--;
            if( line < 0 ) {
                line = current_tab.size() - 1;
            }
        } else if( action == "ADD_DEFAULT_RULESET" ) {
            changes_made = true;
            current_tab.emplace_back( "*", true, false, Attitude::A_HOSTILE,
                                      get_option<int>( "SAFEMODEPROXIMITY" )
                                      , HOSTILE_SPOTTED );
            current_tab.emplace_back( "*", true, true, Attitude::A_HOSTILE, 5, SOUND );
            line = current_tab.size() - 1;
        } else if( action == "ADD_RULE" ) {
            changes_made = true;
            current_tab.emplace_back( "", true, false, Attitude::A_HOSTILE,
                                      get_option<int>( "SAFEMODEPROXIMITY" ), HOSTILE_SPOTTED );
            line = current_tab.size() - 1;
        } else if( action == "REMOVE_RULE" && !current_tab.empty() ) {
            changes_made = true;
            current_tab.erase( current_tab.begin() + line );
            if( line > static_cast<int>( current_tab.size() ) - 1 ) {
                line--;
            }
            if( line < 0 ) {
                line = 0;
            }
        } else if( action == "COPY_RULE" && !current_tab.empty() ) {
            changes_made = true;
            current_tab.push_back( current_tab[line] );
            line = current_tab.size() - 1;
        } else if( action == "SWAP_RULE_GLOBAL_CHAR" && !current_tab.empty() ) {
            if( ( tab == GLOBAL_TAB && !g->u.name.empty() ) || tab == CHARACTER_TAB ) {
                changes_made = true;
                //copy over
                auto &temp_rules_from = ( tab == GLOBAL_TAB ) ? global_rules : character_rules;
                auto &temp_rules_to = ( tab == GLOBAL_TAB ) ? character_rules : global_rules;

                temp_rules_to.push_back( temp_rules_from[line] );

                //remove old
                temp_rules_from.erase( temp_rules_from.begin() + line );
                line = temp_rules_to.size() - 1;
                tab = ( tab == GLOBAL_TAB ) ? CHARACTER_TAB : GLOBAL_TAB;
            }
        } else if( action == "CONFIRM" && !current_tab.empty() ) {
            changes_made = true;
            if( column == COLUMN_RULE ) {
                ui_adaptor help_ui;

                // RmlUi backdrop: a passive static help doc stacked UNDER the
                // string_input "Safe Mode Rule:" popup, replacing the curses help
                // window. Category-specific (monster vs sound wildcards); no data
                // model — literal English in the .rml (same i18n gap as the column
                // heads). Closed after the rule entry below.
                Rml::ElementDocument *help_doc = nullptr;
                if( safemode_rmlui_enabled() && rmlui_layer::ready() ) {
                    const char *help_rml = nullptr;
                    switch( current_tab[line].category ) {
                        case Categories::HOSTILE_SPOTTED:
                            help_rml = "gui/safemode_help_monster.rml";
                            break;
                        case Categories::SOUND:
                            help_rml = "gui/safemode_help_sound.rml";
                            break;
                        default:
                            break;
                    }
                    if( help_rml ) {
                        help_doc = rmlui_layer::open_document( PATH_INFO::datadir() + help_rml, true );
                    }
                }

                help_ui.on_redraw( []( const ui_adaptor & ) {
                    // RmlUi help doc owns this; curses fallback removed (rip-out B).
                } );

                current_tab[line].rule = wildcard_trim_rule( string_input_popup()
                                         .title( _( "Safe Mode Rule:" ) )
                                         .width( 30 )
                                         .text( current_tab[line].rule )
                                         .query_string() );
                if( help_doc ) {
                    rmlui_layer::close_document( help_doc );
                }
            } else if( column == COLUMN_WHITE_BLACKLIST ) {
                current_tab[line].whitelist = !current_tab[line].whitelist;
            } else if( column == COLUMN_CATEGORY ) {
                if( current_tab[line].category == HOSTILE_SPOTTED ) {
                    current_tab[line].category = SOUND;
                } else if( current_tab[line].category == SOUND ) {
                    current_tab[line].category = HOSTILE_SPOTTED;
                }
            } else if( column == COLUMN_ATTITUDE ) {
                auto &attitude = current_tab[line].attitude;
                switch( attitude ) {
                    case Attitude::A_HOSTILE:
                        attitude = Attitude::A_NEUTRAL;
                        break;
                    case Attitude::A_NEUTRAL:
                        attitude = Attitude::A_FRIENDLY;
                        break;
                    case Attitude::A_FRIENDLY:
                        attitude = Attitude::A_ANY;
                        break;
                    case Attitude::A_ANY:
                        attitude = Attitude::A_HOSTILE;
                        break;
                    case Attitude::NUM_A:
                        attitude = Attitude::A_NEUTRAL;
                }
            } else if( column == COLUMN_PROXIMITY && ( current_tab[line].category == SOUND ||
                       !current_tab[line].whitelist ) ) {
                const auto text = string_input_popup()
                                  .title( _( "Proximity Distance (0=max view distance)" ) )
                                  .width( 4 )
                                  .text( std::to_string( current_tab[line].proximity ) )
                                  .description( _( "Option: " ) + std::to_string( get_option<int>( "SAFEMODEPROXIMITY" ) ) +
                                                " " + get_options().get_option( "SAFEMODEPROXIMITY" ).getDefaultText() )
                                  .max_length( 3 )
                                  .only_digits( true )
                                  .query_string();
                if( text.empty() ) {
                    current_tab[line].proximity = get_option<int>( "SAFEMODEPROXIMITY" );
                } else {
                    //Let the options class handle the validity of the new value
                    auto temp_option = get_options().get_option( "SAFEMODEPROXIMITY" );
                    temp_option.setValue( text );
                    current_tab[line].proximity = atoi( temp_option.getValue().c_str() );
                }
            }
        } else if( action == "ENABLE_RULE" && !current_tab.empty() ) {
            changes_made = true;
            current_tab[line].active = true;
        } else if( action == "DISABLE_RULE" && !current_tab.empty() ) {
            changes_made = true;
            current_tab[line].active = false;
        } else if( action == "LEFT" ) {
            column--;
            if( column < 0 ) {
                column = num_columns - 1;
            }
        } else if( action == "RIGHT" ) {
            column++;
            if( column >= num_columns ) {
                column = 0;
            }
        } else if( action == "MOVE_RULE_UP" && !current_tab.empty() ) {
            changes_made = true;
            if( line < static_cast<int>( current_tab.size() ) - 1 ) {
                std::swap( current_tab[line], current_tab[line + 1] );
                line++;
                column = 0;
            }
        } else if( action == "MOVE_RULE_DOWN" && !current_tab.empty() ) {
            changes_made = true;
            if( line > 0 ) {
                std::swap( current_tab[line],  current_tab[line - 1] );
                line--;
                column = 0;
            }
        } else if( action == "TEST_RULE" && !current_tab.empty() ) {
            test_pattern( tab, line );
        } else if( action == "SWITCH_SAFEMODE_OPTION" ) {
            get_options().get_option( "SAFEMODE" ).setNext();
            get_options().save();
        }
    }

    // Tear down the RmlUi document while the bound `data` is still alive. close()
    // is idempotent and a no-op when the curses path ran; safe before the early
    // no-changes return below.
    rml.close();

    if( !changes_made ) {
        return;
    }

    if( query_yn( _( "Save changes?" ) ) ) {
        if( is_safemode_in ) {
            save_global();
            if( !g->u.name.empty() ) {
                save_character();
            }
        } else {
            create_rules();
        }
    } else {
        global_rules = global_rules_old;
        character_rules = character_rules_old;
    }
}

void safemode::test_pattern( const int tab_in, const int row_in )
{
    std::vector<std::string> creature_list;

    auto &temp_rules = ( tab_in == GLOBAL_TAB ) ? global_rules : character_rules;

    if( temp_rules[row_in].rule.empty() ) {
        return;
    }

    if( g->u.name.empty() ) {
        popup( _( "No monsters loaded.  Please start a game first." ) );
        return;
    }

    //Loop through all monster mtypes
    for( const auto &mtype : MonsterGenerator::generator().get_all_mtypes() ) {
        std::string creature_name = mtype.nname();
        if( wildcard_match( creature_name, temp_rules[row_in].rule ) ) {
            creature_list.push_back( creature_name );
        }
    }

    int content_height = 0;
    int content_width = 0;

    catacurses::window w_test_rule_border;

    ui_adaptor ui;
    const auto init_windows = [&]( ui_adaptor & ui ) {
        const point offset( 15 + ( TERMX > FULL_SCREEN_WIDTH ? ( TERMX - FULL_SCREEN_WIDTH ) / 2 : 0 ),
                            5 + ( TERMY > FULL_SCREEN_HEIGHT ? ( TERMY - FULL_SCREEN_HEIGHT ) / 2 :
                                  0 ) );

        content_height = FULL_SCREEN_HEIGHT - 8;
        content_width = FULL_SCREEN_WIDTH - 30;

        w_test_rule_border = catacurses::newwin( content_height + 2, content_width,
                             offset );

        ui.position_from_window( w_test_rule_border );
    };
    init_windows( ui );
    ui.on_screen_resize( init_windows );

    int nmatch = creature_list.size();
    const std::string buf = string_format( vgettext( "%1$d monster matches: %2$s",
                                           "%1$d monsters match: %2$s",
                                           nmatch ), nmatch, temp_rules[row_in].rule.c_str() );

    int line = 0;

    input_context ctxt( "SAFEMODE_TEST" );
    ctxt.register_updown();
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );

    // RmlUi render path (mirrors autopickup_test). Render-only: the loop below owns
    // nav; the doc stacks over the still-open "safemode" rules screen. tdata is
    // declared BEFORE test_rml so it outlives the doc.
    std::unique_ptr<safemode_test_session> tdata;
    rml_doc test_rml;
    const auto sync_test_rml = [&]() {
        if( !test_rml ) {
            return;
        }
        tdata->title_rml = cata_text_to_rml( buf );
        tdata->rows.clear();
        for( int i = 0; i < static_cast<int>( creature_list.size() ); ++i ) {
            safemode_test_row r;
            r.num_rml = cata_text_to_rml( string_format( "%d", i + 1 ) );
            r.name_rml = cata_text_to_rml( creature_list[i] );
            r.selected = ( line == i );
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

    test_rml.open( safemode_rmlui_enabled(), "safemode_test", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
        tdata = std::make_unique<safemode_test_session>();
        register_safemode_test_rml_types( c );
        c.Bind( "title_rml", &tdata->title_rml );
        c.Bind( "rows", &tdata->rows );
        // Click/hover a row to move the cursor onto it; QUIT (keyboard) closes.
        c.BindEventCallback( "on_select",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int idx = -1;
            if( !args.empty() ) {
                args[0].GetInto( idx );
            }
            if( idx >= 0 && idx < static_cast<int>( creature_list.size() ) ) {
                line = idx;
            }
        } );
        tdata->handle = c.GetModelHandle();
    } );

    while( true ) {
        ui_manager::redraw();

        const std::string action = ctxt.handle_input();
        if( action == "DOWN" ) {
            line++;
            if( line >= static_cast<int>( creature_list.size() ) ) {
                line = 0;
            }
        } else if( action == "UP" ) {
            line--;
            if( line < 0 ) {
                line = creature_list.size() - 1;
            }
        } else if( action == "QUIT" ) {
            break;
        }
    }
}

void safemode::add_rule( const std::string &rule_in, const Attitude attitude_in,
                         const int proximity_in,
                         const rule_state state_in )
{
    character_rules.emplace_back( rule_in, true, ( state_in == RULE_WHITELISTED ),
                                  attitude_in, proximity_in, HOSTILE_SPOTTED );
    create_rules();

    if( !get_option<bool>( "SAFEMODE" ) &&
        query_yn( _( "Safe Mode is not enabled in the options.  Enable it now?" ) ) ) {
        get_options().get_option( "SAFEMODE" ).setNext();
        get_options().save();
    }
}

bool safemode::has_rule( const std::string &rule_in, const Attitude attitude_in )
{
    for( auto &elem : character_rules ) {
        if( rule_in.length() == elem.rule.length()
            && ci_find_substr( rule_in, elem.rule ) != -1
            && elem.attitude == attitude_in ) {
            return true;
        }
    }
    return false;
}

void safemode::remove_rule( const std::string &rule_in, const Attitude attitude_in )
{
    for( auto it = character_rules.begin();
         it != character_rules.end(); ++it ) {
        if( rule_in.length() == it->rule.length()
            && ci_find_substr( rule_in, it->rule ) != -1
            && it->attitude == attitude_in ) {
            character_rules.erase( it );
            create_rules();
            break;
        }
    }
}

bool safemode::empty() const
{
    return global_rules.empty() && character_rules.empty();
}

void safemode::create_rules()
{
    safemode_rules_hostile.clear();
    safemode_rules_sound.clear();
    //process include/exclude in order of rules, global first, then character specific
    add_rules( global_rules );
    add_rules( character_rules );
}

void safemode::add_rules( const std::vector<rules_class> &rules_in )
{
    //if a specific monster is being added, all the rules need to be checked now
    //may have some performance issues since exclusion needs to check all monsters also
    for( auto &rule : rules_in ) {
        switch( rule.category ) {
            case HOSTILE_SPOTTED:
                if( !rule.whitelist ) {
                    //Check include patterns against all monster mtypes
                    for( const auto &mtype : MonsterGenerator::generator().get_all_mtypes() ) {
                        set_rule( rule, mtype.nname(), RULE_BLACKLISTED );
                    }
                } else {
                    //exclude monsters from the existing mapping
                    for( const auto &safemode_rule : safemode_rules_hostile ) {
                        set_rule( rule, safemode_rule.first, RULE_WHITELISTED );
                    }
                }
                break;
            case SOUND:
                set_rule( rule, rule.rule, rule.whitelist ? RULE_WHITELISTED : RULE_BLACKLISTED );
                break;
            default:
                break;
        }
    }
}

void safemode::set_rule( const rules_class &rule_in, const std::string &name_in, rule_state rs_in )
{
    static std::vector<Attitude> attitude_any = { {Attitude::A_HOSTILE, Attitude::A_NEUTRAL, Attitude::A_FRIENDLY} };
    switch( rule_in.category ) {
        case HOSTILE_SPOTTED:
            if( !rule_in.rule.empty() && rule_in.active && wildcard_match( name_in, rule_in.rule ) ) {
                if( rule_in.attitude == Attitude::A_ANY ) {
                    for( auto &att : attitude_any ) {
                        safemode_rules_hostile[name_in][att] = rule_state_class( rs_in, rule_in.proximity,
                                                               HOSTILE_SPOTTED );
                    }
                } else {
                    safemode_rules_hostile[name_in][rule_in.attitude] = rule_state_class( rs_in, rule_in.proximity,
                        HOSTILE_SPOTTED );
                }
            }
            break;
        case SOUND:
            safemode_rules_sound.push_back( rule_in );
            break;
        default:
            break;
    }
}

rule_state safemode::check_monster( const std::string &creature_name_in,
                                    const Attitude attitude_in,
                                    const int proximity_in ) const
{
    const auto iter = safemode_rules_hostile.find( creature_name_in );
    if( iter != safemode_rules_hostile.end() ) {
        const auto &tmp = ( iter->second )[static_cast<int>( attitude_in )];
        if( tmp.state == RULE_BLACKLISTED ) {
            if( tmp.proximity == 0 || proximity_in <= tmp.proximity ) {
                return RULE_BLACKLISTED;
            }

        } else if( tmp.state == RULE_WHITELISTED ) {
            return RULE_WHITELISTED;
        }
    }

    return RULE_NONE;
}

bool safemode::is_sound_safe( const std::string &sound_name_in,
                              const int proximity_in ) const
{
    bool sound_safe = false;
    for( const rules_class &rule : safemode_rules_sound ) {
        if( wildcard_match( sound_name_in, rule.rule ) &&
            proximity_in >= rule.proximity ) {
            if( rule.whitelist ) {
                sound_safe = true;
            } else {
                return false;
            }
        }
    }
    return sound_safe;
}

void safemode::clear_character_rules()
{
    character_rules.clear();
}

bool safemode::save_character()
{
    return save( true );
}

bool safemode::save_global()
{
    return save( false );
}

bool safemode::save( const bool is_character_in )
{
    is_character = is_character_in;
    auto serializer = [&]( std::ostream & fout ) {
        JsonOut jout( fout, true );
        serialize( jout );

        if( !is_character ) {
            create_rules();
        }
    };

    if( is_character ) {
        world *world = g->get_active_world();
        if( !world->player_file_exist( ".sav" ) ) {
            return true; //Character not saved yet.
        }

        return world->write_to_player_file( ".sfm.json", serializer, _( "safemode configuration" ) );
    } else {
        return write_to_file( PATH_INFO::safemode(), serializer, _( "safemode configuration" ) );
    }
}

void safemode::load_character()
{
    load( true );
}

void safemode::load_global()
{
    load( false );
}

void safemode::load( const bool is_character_in )
{
    is_character = is_character_in;
    auto loader = [&]( JsonIn & jsin ) {
        try {
            deserialize( jsin );
        } catch( const JsonError &e ) {
            debugmsg( "Error loading safemode settings: %s", e.c_str() );
        }
    };

    std::ifstream fin;
    if( is_character ) {
        g->get_active_world()->read_from_player_file_json( ".sfm.json", loader, true );
    } else {
        read_from_file_json( PATH_INFO::safemode(), loader, true );
    }

    create_rules();
}

void safemode::serialize( JsonOut &json ) const
{
    json.start_array();

    auto &temp_rules = ( is_character ) ? character_rules : global_rules;
    for( auto &elem : temp_rules ) {
    json.start_object();

        json.member( "rule", elem.rule );
        json.member( "active", elem.active );
        json.member( "whitelist", elem.whitelist );
        json.member( "attitude", elem.attitude );
        json.member( "proximity", elem.proximity );
        json.member( "category", elem.category );

        json.end_object();
    }

    json.end_array();
}

void safemode::deserialize( JsonIn &jsin )
{
    auto &temp_rules = ( is_character ) ? character_rules : global_rules;
    temp_rules.clear();

    jsin.start_array();
    while( !jsin.end_array() ) {
        JsonObject jo = jsin.get_object();

        const std::string rule = jo.get_string( "rule" );
        const bool active = jo.get_bool( "active" );
        const bool whitelist = jo.get_bool( "whitelist" );
        const Attitude attitude = static_cast<Attitude>( jo.get_int( "attitude" ) );
        const int proximity = jo.get_int( "proximity" );
        const Categories cat = jo.has_member( "category" ) ? static_cast<Categories>
                               ( jo.get_int( "category" ) ) : HOSTILE_SPOTTED;

        temp_rules.emplace_back( rule, active, whitelist, attitude, proximity, cat
                               );
    }
}
