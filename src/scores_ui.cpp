#include "scores_ui.h"

#include "achievement.h"
#include "color.h"
#include "cursesdef.h"
#include "event_statistics.h"
#include "input.h"
#include "kill_tracker.h"
#include "lighting/rmlui_layer.h"
#include "output.h"
#include "path_info.h"
#include "point.h"
#include "rml_screen.h"
#include "rml_util.h"
#include "stats_tracker.h"
#include "translations.h"
#include "ui.h"
#include "ui_manager.h"

#include <RmlUi/Core.h>
#include <algorithm>
#include <cassert>
#include <iterator>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

static std::string get_achievements_text( const achievements_tracker& achievements )
{
    std::string os;
    std::vector<const achievement *> valid_achievements = achievements.valid_achievements();
    valid_achievements.erase(
        std::remove_if( valid_achievements.begin(), valid_achievements.end(),
    [&]( const achievement * a ) { return achievements.is_hidden( a ); } ),
    valid_achievements.end() );
    using sortable_achievement =
        std::tuple<achievement_completion, std::string, const achievement *>;
    std::vector<sortable_achievement> sortable_achievements;
    std::transform(
        valid_achievements.begin(), valid_achievements.end(),
    std::back_inserter( sortable_achievements ), [&]( const achievement * ach ) {
        achievement_completion comp = achievements.is_completed( ach->id );
        return std::make_tuple( comp, ach->name().translated(), ach );
    } );
    std::sort( sortable_achievements.begin(), sortable_achievements.end(), localized_compare );
    for( const sortable_achievement& ach : sortable_achievements ) {
        os += achievements.ui_text_for( std::get<const achievement*>( ach ) ) + "\n";
    }
    if( valid_achievements.empty() ) { os += _( "This game has no valid achievements.\n" ); }
    os += _( "Note that only achievements that existed when you started this game and still "
             "exist now will appear here." );
    return os;
}

static std::string get_scores_text( stats_tracker& stats )
{
    std::string os;
    std::vector<const score *> valid_scores = stats.valid_scores();
    for( const score * scr : valid_scores ) { os += scr->description( stats ) + "\n"; }
    if( valid_scores.empty() ) { os += _( "This game has no valid scores.\n" ); }
    os += _( "\nNote that only scores that existed when you started this game and still exist now "
             "will appear here." );
    return os;
}

namespace
{
// RmlUi render path for the scores screen (full UI→RmlUi migration, Tier 1
// screen #2). Tab bar mirrors the missions pattern; the body is a SINGLE
// scrolling text pane (the F.2 scrolling-text-view seed). Keyboard stays on
// input_context — LEFT/RIGHT switch tabs, UP/DOWN/PAGE_* scroll the #scores-body
// element via SetScrollTop (RmlUi handles the mouse wheel itself).
struct scores_rml_tab {
    Rml::String name;
    bool selected = false;
};
struct scores_rml_session {
    Rml::Vector<scores_rml_tab> tabs;
    Rml::String body_rml; // colour-tagged text; RmlUi wraps (data-rml)
    Rml::DataModelHandle handle;
    Rml::ElementDocument *doc = nullptr;
    Rml::Element *scroll = nullptr; // #scores-body, scrolled from C++
};

// Data-model for the tab-less kills screen (show_kills). One colour-tagged body
// string; uses the rml_doc harness rather than show_scores_ui's inline boilerplate.
struct scores_kills_session {
    Rml::String body_rml;
    Rml::DataModelHandle handle;
};

bool g_scores_types_registered = false;
bool g_scores_model_active = false;

void register_scores_rml_types( Rml::DataModelConstructor& c )
{
    if( g_scores_types_registered ) { return; }
    Rml::StructHandle<scores_rml_tab> th = c.RegisterStruct<scores_rml_tab>();
    th.RegisterMember( "name", &scores_rml_tab::name );
    th.RegisterMember( "selected", &scores_rml_tab::selected );
    c.RegisterArray<Rml::Vector<scores_rml_tab>>();
    g_scores_types_registered = true;
}
} // namespace

bool &scores_rmlui_enabled()
{
    // Default OFF — opt in via the F4 panel. See rml_screen.h.
    static bool enabled = true;
    return enabled;
}

void show_scores_ui(
    const achievements_tracker& achievements, stats_tracker& stats, const kill_tracker& kills )
{
    catacurses::window w;

    enum class tab_mode {
        achievements,
        scores,
        kills,
        num_tabs,
        first_tab = achievements,
    };

    tab_mode tab = static_cast<tab_mode>( 0 );
    input_context ctxt( "SCORES" );
    ctxt.register_cardinal();
    ctxt.register_action( "PAGE_UP" );
    ctxt.register_action( "PAGE_DOWN" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "PREV_TAB" );
    ctxt.register_action( "NEXT_TAB" );
    ctxt.register_action( "HELP_KEYBINDINGS" );

    catacurses::window w_view;
    scrolling_text_view view( w_view );
    bool new_tab = true;

    // ---- RmlUi render path ---------------------------------------------------
    // When active, the curses on_redraw is skipped and the model is synced in the
    // tab-change block below (NOT every 16ms tick — that would reset the scroll
    // offset). Keyboard scroll drives #scores-body directly via SetScrollTop.
    std::unique_ptr<scores_rml_session> rml;
    const auto scroll_rml = [&]( float frac ) {
        if( !rml || rml->scroll == nullptr ) { return; }
        Rml::Element* e = rml->scroll;
        const float ch = e->GetClientHeight();
        const float max_top = std::max( 0.0f, e->GetScrollHeight() - ch );
        const float t = std::clamp( e->GetScrollTop() + frac * ch, 0.0f, max_top );
        e->SetScrollTop( t );
    };

    ui_adaptor ui;
    const auto& init_windows = [&]( ui_adaptor & ui ) {
        w = new_centered_win( TERMY - 2, FULL_SCREEN_WIDTH );
        w_view =
            catacurses::newwin( getmaxy( w ) - 4, getmaxx( w ) - 1, point( getbegx( w ), getbegy( w ) + 3 ) );
        ui.position_from_window( w );
    };
    ui.on_screen_resize( init_windows );
    // initialize explicitly here since w_view is used before first redraw
    init_windows( ui );

    const std::vector<std::pair<tab_mode, std::string>> tabs = {
        {tab_mode::achievements, _( "ACHIEVEMENTS" )},
        {tab_mode::scores, _( "SCORES" )},
        {tab_mode::kills, _( "KILLS" )},
    };

    // Open the RmlUi document if the toggle is on and the layer is ready. On
    // failure (or toggle off) `rml` stays null and the curses path runs.
    if( scores_rmlui_enabled() && rmlui_layer::ready() && !g_scores_model_active ) {
        if( Rml::Context * ctx = rmlui_layer::context() ) {
            Rml::DataModelConstructor c = ctx->CreateDataModel( "scores" );
            if( c ) {
                rml = std::make_unique<scores_rml_session>();
                register_scores_rml_types( c );
                c.Bind( "tabs", &rml->tabs );
                c.Bind( "body_rml", &rml->body_rml );
                c.BindEventCallback(
                "on_tab", [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
                    int idx = -1;
                    if( !args.empty() ) { args[0].GetInto( idx ); }
                    if( idx >= 0 && idx < static_cast<int>( tab_mode::num_tabs ) ) {
                        tab = static_cast<tab_mode>( idx );
                        // Force the tab-change block to rebuild + reset scroll
                        // (mouse path; the keyboard path sets this directly).
                        new_tab = true;
                    }
                } );
                rml->handle = c.GetModelHandle();
                rml->doc = rmlui_layer::open_document( PATH_INFO::datadir() + "gui/scores.rml" );
                if( rml->doc == nullptr ) {
                    ctx->RemoveDataModel( "scores" );
                    rml.reset();
                } else {
                    rml->scroll = rml->doc->GetElementById( "scores-body" );
                    g_scores_model_active = true;
                    // Tick at 16ms so RmlUi hover/mouse-wheel stay live between keys.
                    ctxt.set_timeout( 16 );
                }
            }
        }
    }

    ui.on_redraw( [&]( const ui_adaptor & ) {
        // Tier-10 rip-out: the RmlUi document renders itself; the curses draw path is gone.
        // The model is synced in the tab-change block below.
    } );

    while( true ) {
        if( new_tab ) {
            std::string text;
            switch( tab ) {
                case tab_mode::achievements:
                    text = get_achievements_text( achievements );
                    break;
                case tab_mode::scores:
                    text = get_scores_text( stats );
                    break;
                case tab_mode::kills:
                    text = kills.get_kills_text();
                    break;
                case tab_mode::num_tabs:
                    assert( false );
                    break;
            }
            view.set_text( text );
            if( rml ) {
                rml->tabs.clear();
                for( const auto& t : tabs ) { rml->tabs.push_back( {t.second, t.first == tab} ); }
                rml->body_rml = cata_text_to_rml( text );
                rml->handle.DirtyVariable( "tabs" );
                rml->handle.DirtyVariable( "body_rml" );
                // New tab starts at the top, mirroring curses set_text.
                if( rml->scroll != nullptr ) { rml->scroll->SetScrollTop( 0.0f ); }
            }
        }

        ui_manager::redraw();
        const std::string action = ctxt.handle_input();
        new_tab = false;
        if( action == "RIGHT" || action == "NEXT_TAB" ) {
            tab = static_cast<tab_mode>( static_cast<int>( tab ) + 1 );
            if( tab >= tab_mode::num_tabs ) { tab = tab_mode::first_tab; }
            new_tab = true;
        } else if( action == "LEFT" || action == "PREV_TAB" ) {
            tab = static_cast<tab_mode>( static_cast<int>( tab ) - 1 );
            if( tab < tab_mode::first_tab ) {
                tab = static_cast<tab_mode>( static_cast<int>( tab_mode::num_tabs ) - 1 );
            }
            new_tab = true;
        } else if( action == "DOWN" ) {
            rml ? scroll_rml( 0.1f ) : view.scroll_down();
        } else if( action == "UP" ) {
            rml ? scroll_rml( -0.1f ) : view.scroll_up();
        } else if( action == "PAGE_DOWN" ) {
            rml ? scroll_rml( 0.9f ) : view.page_down();
        } else if( action == "PAGE_UP" ) {
            rml ? scroll_rml( -0.9f ) : view.page_up();
        } else if( action == "CONFIRM" || action == "QUIT" ) {
            break;
        }
    }

    // Tear down the RmlUi document + data model (no-op if the curses path ran).
    if( rml ) {
        if( rml->doc != nullptr ) { rmlui_layer::close_document( rml->doc ); }
        if( Rml::Context * ctx = rmlui_layer::context() ) { ctx->RemoveDataModel( "scores" ); }
        g_scores_model_active = false;
        rml.reset();
    }
}

void show_kills( kill_tracker& kills )
{
    catacurses::window w;

    input_context ctxt( "SCORES" );
    ctxt.register_cardinal();
    ctxt.register_action( "PAGE_UP" );
    ctxt.register_action( "PAGE_DOWN" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );

    catacurses::window w_view;
    scrolling_text_view view( w_view );

    // RmlUi render path: a single scrolling text pane (no tabs) — the show_kills
    // twin of show_scores_ui's body, on the rml_doc harness. kdata declared before
    // the doc so it outlives it; scroll element captured for keyboard SetScrollTop.
    std::unique_ptr<scores_kills_session> kdata;
    rml_doc kills_rml;
    Rml::Element* scroll_el = nullptr;

    ui_adaptor ui;
    const auto& init_windows = [&]( ui_adaptor & ui ) {
        w = new_centered_win( TERMY - 2, FULL_SCREEN_WIDTH );
        w_view =
            catacurses::newwin( getmaxy( w ) - 4, getmaxx( w ) - 1, point( getbegx( w ), getbegy( w ) + 3 ) );
        ui.position_from_window( w );
        view.set_text( kills.get_kills_text() );
    };
    ui.on_screen_resize( init_windows );
    // initialize explicitly here since w_view is used before first redraw
    init_windows( ui );

    kills_rml.open( scores_rmlui_enabled(), "scores_kills", ctxt, [&]( Rml::DataModelConstructor & c ) {
        kdata = std::make_unique<scores_kills_session>();
        kdata->body_rml = cata_text_to_rml( kills.get_kills_text() );
        c.Bind( "body_rml", &kdata->body_rml );
        kdata->handle = c.GetModelHandle();
    } );
    if( kills_rml ) { scroll_el = kills_rml.document()->GetElementById( "scores-kills-body" ); }

    // Scroll the RmlUi body element by a fraction of its viewport (cf. show_scores_ui).
    const auto scroll_rml = [&]( float frac ) {
        if( scroll_el == nullptr ) { return; }
        const float ch = scroll_el->GetClientHeight();
        const float max_top = std::max( 0.0f, scroll_el->GetScrollHeight() - ch );
        const float t = std::clamp( scroll_el->GetScrollTop() + frac * ch, 0.0f, max_top );
        scroll_el->SetScrollTop( t );
    };

    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( kills_rml ) { return; }
    } );

    while( true ) {
        ui_manager::redraw();
        const std::string action = ctxt.handle_input();
        if( action == "DOWN" ) {
            kills_rml ? scroll_rml( 0.1f ) : view.scroll_down();
        } else if( action == "UP" ) {
            kills_rml ? scroll_rml( -0.1f ) : view.scroll_up();
        } else if( action == "PAGE_DOWN" ) {
            kills_rml ? scroll_rml( 0.9f ) : view.page_down();
        } else if( action == "PAGE_UP" ) {
            kills_rml ? scroll_rml( -0.9f ) : view.page_up();
        } else if( action == "CONFIRM" || action == "QUIT" ) {
            break;
        }
    }
    kills_rml.close();
}
