#include "catch/catch_amalgamated.hpp"
#include <string>
#include <vector>

#include "calendar.h"

#include "panels.h"
#include "type_id.h"
#include "widget.h"

// Sidebar widget-engine port. These tests prove, without a curses context or an
// avatar (real drawing is a later integration concern):
//   1. the widget generic_factory loads data/json/ui/sidebar.json,
//   2. the native-wrapper bridge binds JSON widgets to existing draw_* sidebar
//      functions (dispatch resolves) and carries metadata onto the window_panel,
//   3. flex heights (-1/-2) pass through unclamped,
//   4. the "custom" sidebar container reproduces the labels layout.

TEST_CASE( "native widget loads and binds to a draw target", "[widget][sidebar]" )
{
    const widget_id stats_id( "stats" );
    REQUIRE( stats_id.is_valid() );
    const widget &w = *stats_id;
    CHECK( w.style() == "native" );
    CHECK( w.native() == "draw_stat_wide" );
    CHECK( w.height() == 2 );

    // The bridge carries widget metadata onto the panel (Tier-10 curses rip-out:
    // name-only, no curses draw). The panel name is the widget id (stable,
    // untranslated save/load key).
    const window_panel wp = make_native_widget_panel( w, 38 );
    CHECK( wp.get_name() == "stats" );
    CHECK( wp.get_height() == 2 );
    CHECK( wp.get_width() == 38 );
    // Tier-10 curses rip-out: native panels are name-only now (rendered by the
    // RmlUi HUD via hud_producer); they carry no curses draw.
    CHECK_FALSE( static_cast<bool>( wp.draw ) );
}

TEST_CASE( "flex heights pass through unclamped", "[widget][sidebar]" )
{
    // Log = -2 (fill remaining), Map = -1 (minimap). window_panel relies on the
    // negative sentinels, so the bridge must NOT clamp them to >= 1.
    REQUIRE( widget_id( "log" ).is_valid() );
    REQUIRE( widget_id( "map" ).is_valid() );
    CHECK( widget_id( "log" )->height() == -2 );
    CHECK( widget_id( "map" )->height() == -1 );
}

TEST_CASE( "render-gated widget carries its show_if predicate", "[widget][sidebar]" )
{
    REQUIRE( widget_id( "mana" ).is_valid() );
    REQUIRE( widget_id( "vehicle" ).is_valid() );
    CHECK( widget_id( "mana" )->show_if() == "spell_panel" );
    CHECK( widget_id( "vehicle" )->show_if() == "veh_panel" );
    // An ungated widget has no predicate (always shown).
    CHECK( widget_id( "stats" )->show_if().empty() );
}

TEST_CASE( "sidebar container reproduces the labels layout", "[widget][sidebar]" )
{
    const widget_id sb_id( "custom" );
    REQUIRE( sb_id.is_valid() );
    const widget &s = *sb_id;
    CHECK( s.style() == "sidebar" );
    // 22 native (labels parity) + 4 value widgets + 4 body_graph widgets + coop_partner.
    REQUIRE( s._widgets.size() == 31 );
    CHECK( s._widgets.front() == widget_id( "hint" ) );
    CHECK( s._widgets.back() == widget_id( "coop_partner" ) );
}

TEST_CASE( "value widget parses var + icon and builds a panel", "[widget][sidebar]" )
{
    const widget_id pain_id( "val_pain" );
    REQUIRE( pain_id.is_valid() );
    const widget &w = *pain_id;
    CHECK( w.style() == "number" );
    CHECK( w.var() == widget_var::pain );
    CHECK( w.icon() == "heart" );

    // The value renderer builds a panel with a clean English save/gutter name
    // derived from the id ("val_pain" -> "Pain"), re-localized via _() at display.
    const window_panel wp = make_value_widget_panel( w, 44 );
    CHECK( wp.get_name() == "Pain" );
    CHECK( wp.get_height() == 1 );
    // hud_produce renders text for the RmlUi HUD (value/bodygraph widgets know
    // their own widget id; hud_producer's name-keyed table can't). No curses draw.
    CHECK_FALSE( static_cast<bool>( wp.draw ) );
    CHECK( static_cast<bool>( wp.hud_produce ) );

    // A var with no clean BN getter falls back to last (0); stamina has one.
    CHECK( widget_id( "val_stamina" )->var() == widget_var::stamina );
    CHECK( widget_id( "val_stamina" )->icon().empty() );
}

TEST_CASE( "body_graph widget parses dimension + builds a multi-row panel", "[widget][sidebar]" )
{
    const widget_id bg_id( "bodygraph" );
    REQUIRE( bg_id.is_valid() );
    const widget &w = *bg_id;
    CHECK( w.style() == "body_graph" );
    CHECK( w.var() == widget_var::body_graph );

    // The dimension variants parse their distinct vars.
    CHECK( widget_id( "bodygraph_temp" )->var() == widget_var::body_graph_temp );
    CHECK( widget_id( "bodygraph_encumb" )->var() == widget_var::body_graph_encumb );
    CHECK( widget_id( "bodygraph_status" )->var() == widget_var::body_graph_status );

    // The renderer builds a multi-row panel with a clean gutter name.
    const window_panel wp = make_bodygraph_widget_panel( w, 44 );
    CHECK( wp.get_name() == "Bodygraph" );
    CHECK( wp.get_height() == 3 );
    // Tier-10 curses rip-out: no curses draw; hud_produce renders the RmlUi rows.
    CHECK_FALSE( static_cast<bool>( wp.draw ) );
    CHECK( static_cast<bool>( wp.hud_produce ) );
}

TEST_CASE( "reload_widget_layouts registers the custom sidebar layout", "[widget][sidebar]" )
{
    // The integration that makes the sidebar selectable in-game: building the
    // panel_manager layout from the loaded sidebar widget. (In-game this is
    // called after world modfiles load; the test harness already has data.)
    panel_manager &pm = panel_manager::get_manager();
    pm.reload_widget_layouts();
    CHECK( pm.has_layout( "custom" ) );
}

TEST_CASE( "W_ALWAYS_DRAW maps to the window_panel force-draw flag", "[widget][sidebar]" )
{
    // The Map panel must redraw every frame (smooth GPU minimap) — the hardcoded
    // builder passes always_draw=true; the JSON path expresses it via W_ALWAYS_DRAW.
    REQUIRE( widget_id( "map" ).is_valid() );
    CHECK( make_native_widget_panel( *widget_id( "map" ), 44 ).always_draw );
    // A widget without the flag stays event-driven.
    REQUIRE( widget_id( "stats" ).is_valid() );
    CHECK_FALSE( make_native_widget_panel( *widget_id( "stats" ), 44 ).always_draw );
}

TEST_CASE( "widget-engine layouts reproduce the four built-ins", "[widget][sidebar]" )
{
    // Each we_* sidebar container mirrors initialize_default_*_panels: same panel
    // count and the same first/last panel, in builder order. (Native casing/heights
    // are covered by the per-widget defs; this guards the container transcription.)
    struct layout_case {
        std::string id;
        size_t count;
        std::string front;
        std::string back;
    };
    const std::vector<layout_case> cases = {
        { "we_classic",       20, "health_classic", "coop_partner" },
        { "we_compact",       18, "limbs_compact",  "coop_partner" },
        { "we_labels_narrow", 22, "hint",           "coop_partner" },
        { "we_labels",        23, "hint",           "coop_partner" },
    };
    for( const layout_case &c : cases ) {
        CAPTURE( c.id );
        const widget_id sb_id( c.id );
        REQUIRE( sb_id.is_valid() );
        const widget &s = *sb_id;
        CHECK( s.style() == "sidebar" );
        REQUIRE( s._widgets.size() == c.count );
        CHECK( s._widgets.front() == widget_id( c.front ) );
        CHECK( s._widgets.back() == widget_id( c.back ) );
    }

    // They register as selectable widget layouts (the built-in curses layouts were
    // removed in the Tier-10 rip-out).
    panel_manager &pm = panel_manager::get_manager();
    pm.reload_widget_layouts();
    CHECK( pm.has_layout( "we_classic" ) );
    CHECK( pm.has_layout( "we_labels" ) );
}


