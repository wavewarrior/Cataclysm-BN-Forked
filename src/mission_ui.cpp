#include "game.h" // IWYU pragma: associated

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "color.h"
#include "debug.h"
#include "input.h"
#include "mission.h"
#include "npc.h"
#include "output.h"
#include "string_formatter.h"
#include "string_utils.h"
#include "translations.h"
#include "ui.h"
#include "ui_manager.h"

#include <RmlUi/Core.h>
#include <memory>

#include "lighting/rmlui_layer.h"
#include "path_info.h"
#include "rml_screen.h"

namespace
{
// RmlUi render path for the missions screen (full UI→RmlUi migration, Tier 1).
// Keyboard stays on input_context; this only mirrors state into a data-model the
// missions.rml document renders. Mirrors the proven uilist session pattern.
struct missions_rml_tab {
    Rml::String name;
    bool selected = false;
};
struct missions_rml_row {
    Rml::String text_rml;
    bool selected = false;
};
struct missions_rml_session {
    Rml::Vector<missions_rml_tab> tabs;
    Rml::Vector<missions_rml_row> rows;
    bool has_rows = false;
    Rml::String empty_rml;   // "no missions" placeholder (data-rml)
    Rml::String detail_rml;  // right-hand detail pane (data-rml, RmlUi wraps)
    Rml::DataModelHandle handle;
    Rml::ElementDocument *doc = nullptr;
};

// Type registration is context-global and persists for the context's life, so
// once. Model NAME "missions" allows one missions screen at a time (it can't
// nest itself), matching the uilist single-instance guard pattern.
bool g_missions_types_registered = false;
bool g_missions_model_active = false;

void register_missions_rml_types( Rml::DataModelConstructor &c )
{
    if( g_missions_types_registered ) {
        return;
    }
    Rml::StructHandle<missions_rml_tab> th = c.RegisterStruct<missions_rml_tab>();
    th.RegisterMember( "name", &missions_rml_tab::name );
    th.RegisterMember( "selected", &missions_rml_tab::selected );
    c.RegisterArray<Rml::Vector<missions_rml_tab>>();
    Rml::StructHandle<missions_rml_row> rh = c.RegisterStruct<missions_rml_row>();
    rh.RegisterMember( "text_rml", &missions_rml_row::text_rml );
    rh.RegisterMember( "selected", &missions_rml_row::selected );
    c.RegisterArray<Rml::Vector<missions_rml_row>>();
    g_missions_types_registered = true;
}
} // namespace

bool &missions_rmlui_enabled()
{
    // Default OFF — opt in via the F4 panel. See rml_screen.h.
    static bool enabled = true;
    return enabled;
}

void game::list_missions()
{
    catacurses::window w_missions;

    enum class tab_mode : int {
        TAB_ACTIVE = 0,
        TAB_COMPLETED,
        TAB_FAILED,
        NUM_TABS,
        FIRST_TAB = 0,
        LAST_TAB = NUM_TABS - 1
    };
    tab_mode tab = tab_mode::FIRST_TAB;
    size_t selection = 0;
    int entries_per_page = 0;
    input_context ctxt( "MISSIONS" );
    ctxt.register_cardinal();
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );

    ui_adaptor ui;
    ui.on_screen_resize( [&]( ui_adaptor & ui ) {
        w_missions = new_centered_win( FULL_SCREEN_HEIGHT, FULL_SCREEN_WIDTH );

        // content ranges from y=3 to FULL_SCREEN_HEIGHT - 2
        entries_per_page = FULL_SCREEN_HEIGHT - 4;

        ui.position_from_window( w_missions );
    } );
    ui.mark_resize();

    std::vector<mission *> umissions;

    // ---- RmlUi render path ---------------------------------------------------
    // Mirrors the same state the curses on_redraw draws into a "missions" data
    // model. Keyboard navigation below is untouched; this only renders + lets
    // the layout engine wrap the detail pane (so NO fold_and_print port).
    std::unique_ptr<missions_rml_session> rml;

    const auto build_detail_rml = [&]( mission * miss ) -> std::string {
        const nc_color col = u.get_active_mission() == miss ? c_light_green : c_white;
        std::string for_npc;
        if( miss->get_npc_id().is_valid() )
        {
            npc *guy = g->find_npc( miss->get_npc_id() );
            if( guy ) {
                for_npc = string_format( _( " for %s" ), guy->disp_name() );
            }
        }
        std::string d = colorize( miss->name() + for_npc, col ) + "\n\n";
        if( !miss->get_description().empty() )
        {
            std::string desc = miss->get_description();
            for( const auto &reward : miss->get_likely_rewards() ) {
                const std::string token = "<reward_count:" + reward.second.str() + ">";
                desc = replace_all( desc, token, string_format( "%d", reward.first ) );
            }
            d += colorize( desc, c_white ) + "\n";
        }
        if( miss->has_deadline() )
        {
            const time_point deadline = miss->get_deadline();
            d += "\n" + colorize( string_format( _( "Deadline: %s" ), to_string( deadline ) ), c_white );
            if( tab != tab_mode::TAB_COMPLETED ) {
                const time_duration remaining = deadline - calendar::turn;
                std::string remaining_time;
                if( remaining <= 0_turns ) {
                    remaining_time = _( "None!" );
                } else if( u.has_watch() ) {
                    remaining_time = to_string( remaining );
                } else {
                    remaining_time = to_string_approx( remaining );
                }
                d += "\n" + colorize( string_format( _( "Time remaining: %s" ), remaining_time ), c_white );
            }
        }
        if( miss->has_target() )
        {
            const tripoint_abs_omt pos = u.abs_omt_pos();
            d += "\n" + colorize( string_format( _( "Target: %s   You: %s" ),
                                                 miss->get_target().to_string(), pos.to_string() ), c_white );
        }
        return d;
    };

    const auto sync_rml = [&]() {
        if( !rml ) {
            return;
        }
        rml->tabs.clear();
        rml->tabs.push_back( { _( "ACTIVE MISSIONS" ), tab == tab_mode::TAB_ACTIVE } );
        rml->tabs.push_back( { _( "COMPLETED MISSIONS" ), tab == tab_mode::TAB_COMPLETED } );
        rml->tabs.push_back( { _( "FAILED MISSIONS" ), tab == tab_mode::TAB_FAILED } );

        rml->rows.clear();
        for( size_t i = 0; i < umissions.size(); i++ ) {
            mission *miss = umissions[i];
            const nc_color col = u.get_active_mission() == miss ? c_light_green : c_white;
            missions_rml_row r;
            r.text_rml = cata_text_to_rml( colorize( miss->name(), col ) );
            r.selected = ( i == selection );
            rml->rows.push_back( r );
        }
        rml->has_rows = !umissions.empty();

        if( rml->has_rows && selection < umissions.size() ) {
            rml->detail_rml = cata_text_to_rml( build_detail_rml( umissions[selection] ) );
            rml->empty_rml = "";
        } else {
            rml->detail_rml = "";
            std::string nope;
            switch( tab ) {
                case tab_mode::TAB_COMPLETED:
                    nope = _( "You haven't completed any missions!" );
                    break;
                case tab_mode::TAB_FAILED:
                    nope = _( "You haven't failed any missions!" );
                    break;
                default:
                    nope = _( "You have no active missions!" );
                    break;
            }
            rml->empty_rml = cata_text_to_rml( colorize( nope, c_light_red ) );
        }

        rml->handle.DirtyVariable( "tabs" );
        rml->handle.DirtyVariable( "rows" );
        rml->handle.DirtyVariable( "has_rows" );
        rml->handle.DirtyVariable( "detail_rml" );
        rml->handle.DirtyVariable( "empty_rml" );
    };

    ui.on_redraw( [&]( const ui_adaptor & ) {
        // When the RmlUi path owns the screen, sync the model and skip all curses
        // drawing (leaving w_missions un-refreshed so nothing composites behind
        // the RmlUi overlay), mirroring the uilist dual-path.
        if( rml ) {
            sync_rml();
            return;
        }
    } );

    // Open the RmlUi document if the toggle is on and the layer is ready. On
    // failure (or toggle off) `rml` stays null and the curses path runs.
    if( missions_rmlui_enabled() && rmlui_layer::ready() && !g_missions_model_active ) {
        if( Rml::Context *ctx = rmlui_layer::context() ) {
            Rml::DataModelConstructor c = ctx->CreateDataModel( "missions" );
            if( c ) {
                rml = std::make_unique<missions_rml_session>();
                register_missions_rml_types( c );
                c.Bind( "tabs", &rml->tabs );
                c.Bind( "rows", &rml->rows );
                c.Bind( "has_rows", &rml->has_rows );
                c.Bind( "empty_rml", &rml->empty_rml );
                c.Bind( "detail_rml", &rml->detail_rml );
                c.BindEventCallback( "on_select",
                [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
                    int idx = -1;
                    if( !args.empty() ) {
                        args[0].GetInto( idx );
                    }
                    if( idx >= 0 && static_cast<size_t>( idx ) < umissions.size() ) {
                        selection = idx;
                    }
                } );
                c.BindEventCallback( "on_tab",
                [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
                    int idx = -1;
                    if( !args.empty() ) {
                        args[0].GetInto( idx );
                    }
                    if( idx >= 0 && idx < static_cast<int>( tab_mode::NUM_TABS ) ) {
                        tab = static_cast<tab_mode>( idx );
                        selection = 0;
                    }
                } );
                rml->handle = c.GetModelHandle();
                rml->doc = rmlui_layer::open_document( PATH_INFO::datadir() + "gui/missions.rml" );
                if( rml->doc == nullptr ) {
                    ctx->RemoveDataModel( "missions" );
                    rml.reset();
                } else {
                    g_missions_model_active = true;
                    // Tick at 16ms so RmlUi hover/mouse stay live between keys.
                    ctxt.set_timeout( 16 );
                }
            }
        }
    }

    while( true ) {
        umissions.clear();
        if( tab < tab_mode::FIRST_TAB || tab >= tab_mode::NUM_TABS ) {
            debugmsg( "The sanity check failed because tab=%d", static_cast<int>( tab ) );
            tab = tab_mode::FIRST_TAB;
        }
        switch( tab ) {
            case tab_mode::TAB_ACTIVE:
                umissions = u.get_active_missions();
                break;
            case tab_mode::TAB_COMPLETED:
                umissions = u.get_completed_missions();
                break;
            case tab_mode::TAB_FAILED:
                umissions = u.get_failed_missions();
                break;
            default:
                break;
        }
        if( ( !umissions.empty() && selection >= umissions.size() ) ||
            ( umissions.empty() && selection != 0 ) ) {
            debugmsg( "Sanity check failed: selection=%d, size=%d", static_cast<int>( selection ),
                      static_cast<int>( umissions.size() ) );
            selection = 0;
        }
        ui_manager::redraw();
        const std::string action = ctxt.handle_input();
        if( action == "RIGHT" ) {
            tab = static_cast<tab_mode>( static_cast<int>( tab ) + 1 );
            if( tab >= tab_mode::NUM_TABS ) {
                tab = tab_mode::FIRST_TAB;
            }
            selection = 0;
        } else if( action == "LEFT" ) {
            tab = static_cast<tab_mode>( static_cast<int>( tab ) - 1 );
            if( tab < tab_mode::FIRST_TAB ) {
                tab = tab_mode::LAST_TAB;
            }
            selection = 0;
        } else if( action == "DOWN" ) {
            selection++;
            if( selection >= umissions.size() ) {
                selection = 0;
            }
        } else if( action == "UP" ) {
            if( selection == 0 ) {
                selection = umissions.empty() ? 0 : umissions.size() - 1;
            } else {
                selection--;
            }
        } else if( action == "CONFIRM" ) {
            if( tab == tab_mode::TAB_ACTIVE && selection < umissions.size() ) {
                u.set_active_mission( *umissions[selection] );
            }
            break;
        } else if( action == "QUIT" ) {
            break;
        }
    }

    // Tear down the RmlUi document + data model (no-op if the curses path ran).
    if( rml ) {
        if( rml->doc != nullptr ) {
            rmlui_layer::close_document( rml->doc );
        }
        if( Rml::Context *ctx = rmlui_layer::context() ) {
            ctx->RemoveDataModel( "missions" );
        }
        g_missions_model_active = false;
        rml.reset();
    }
}
