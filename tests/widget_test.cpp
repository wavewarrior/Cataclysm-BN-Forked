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

TEST_CASE( "moon_phase_display maps every phase to a name and icon", "[widget][sidebar]" )
{
    struct phase_case {
        moon_phase phase;
        std::string name;
        std::string icon;
    };
    // Icon ids are hardcoded here against gfx/widgets/icons.json's actual entries —
    // a renamed/missing icon id should fail this test, not just silently 404 at draw.
    const std::vector<phase_case> cases = {
        { MOON_NEW,              "New moon",        "moon_new" },
        { MOON_WAXING_CRESCENT,  "Waxing crescent",  "moon_waxing_crescent" },
        { MOON_HALF_MOON_WAXING, "First quarter",    "moon_first_quarter" },
        { MOON_WAXING_GIBBOUS,   "Waxing gibbous",   "moon_waxing_gibbous" },
        { MOON_FULL,             "Full moon",        "moon_full" },
        { MOON_WANING_GIBBOUS,   "Waning gibbous",   "moon_waning_gibbous" },
        { MOON_HALF_MOON_WANING, "Last quarter",     "moon_last_quarter" },
        { MOON_WANING_CRESCENT,  "Waning crescent",  "moon_waning_crescent" },
    };
    for( const phase_case &c : cases ) {
        CAPTURE( c.name );
        const moon_phase_info info = moon_phase_display( c.phase );
        CHECK( std::string( info.name ) == c.name );
        CHECK( std::string( info.icon ) == c.icon );
    }
}

TEST_CASE( "wind_arrow_icon buckets every angle into its 8-way sector", "[widget][sidebar]" )
{
    struct angle_case {
        int dirangle;
        std::string icon;
    };
    const std::vector<angle_case> cases = {
        { -1,  "wind" },       // out of range (low) -> no-direction fallback
        { 0,   "wind_n" },
        { 23,  "wind_n" },
        { 24,  "wind_ne" },
        { 68,  "wind_ne" },
        { 69,  "wind_e" },
        { 113, "wind_e" },
        { 114, "wind_se" },
        { 158, "wind_se" },
        { 159, "wind_s" },
        { 203, "wind_s" },
        { 204, "wind_sw" },
        { 248, "wind_sw" },
        { 249, "wind_w" },
        { 293, "wind_w" },
        { 294, "wind_nw" },
        { 338, "wind_nw" },
        { 339, "wind_n" },    // wraps back across N (> 338)
        { 359, "wind_n" },
        { 360, "wind" },      // out of range (high) -> no-direction fallback
        { 400, "wind" },
    };
    for( const angle_case &c : cases ) {
        CAPTURE( c.dirangle );
        CHECK( std::string( wind_arrow_icon( c.dirangle ) ) == c.icon );
    }
}

TEST_CASE( "every custom-layout widget has an RmlUi HUD producer", "[widget][sidebar]" )
{
    // The mechanical Tier-10 rip-out gate, at the panel level rather than the
    // string-report level of sidebar_hud_coverage_report(): every widget in the
    // "custom" (31/31) layout must either resolve through the name-keyed
    // g_hud_producers table, or carry its own hud_produce (value/bodygraph widgets).
    panel_manager &pm = panel_manager::get_manager();
    pm.reload_widget_layouts();
    REQUIRE( pm.has_layout( "custom" ) );

    const widget_id sb_id( "custom" );
    REQUIRE( sb_id.is_valid() );
    REQUIRE( sb_id->_widgets.size() == 31 );

    for( const window_panel &panel : pm.get_current_layout() ) {
        // Mirror sidebar_hud_coverage_report's own gate: a panel that's toggled off
        // or whose render() predicate is false (e.g. coop_partner without COOP_ENABLED)
        // never enters the coverage count.
        if( !panel.toggle || !panel.render() ) {
            continue;
        }
        CAPTURE( panel.get_name() );
        CHECK( ( sidebar_hud_has_producer( panel.get_name() ) ||
                 static_cast<bool>( panel.hud_produce ) ) );
    }
}
