#include "catch/catch.hpp"

#include <string>

#include "panels.h"
#include "type_id.h"
#include "widget.h"

// Stage 3 of the sidebar widget-engine port. These tests prove two things
// without needing a curses context or an avatar (drawing for real is a Stage 6
// integration concern):
//   1. the widget generic_factory loads data/json/ui/sidebar.json, and
//   2. the native-wrapper bridge binds a JSON widget to an existing draw_*
//      sidebar function (dispatch resolves) and carries its metadata onto the
//      resulting window_panel with a non-null draw callback.

TEST_CASE( "native widget loads and binds to a draw target", "[widget][sidebar]" )
{
    const widget_id stats_id( "stats_native_test" );
    REQUIRE( stats_id.is_valid() );
    const widget &w = *stats_id;
    CHECK( w.style() == "native" );
    CHECK( w.native() == "draw_stats" );
    CHECK( w.height() == 1 );

    // Dispatch table resolves a known target and rejects an unknown one —
    // verifiable without invoking the draw (no curses/avatar needed).
    CHECK( native_draw_target_exists( "draw_stats" ) );
    CHECK_FALSE( native_draw_target_exists( "no_such_draw_fn" ) );

    // The bridge carries widget metadata onto the panel and binds a draw. The
    // panel name is the widget id (the stable, untranslated save/load key), not
    // the display label.
    const window_panel wp = make_native_widget_panel( w, 38 );
    CHECK( wp.get_name() == "stats_native_test" );
    CHECK( wp.get_height() == 1 );
    CHECK( wp.get_width() == 38 );
    CHECK( static_cast<bool>( wp.draw ) );
}

TEST_CASE( "sidebar container widget lists its children", "[widget][sidebar]" )
{
    const widget_id sb_id( "test_sidebar" );
    REQUIRE( sb_id.is_valid() );
    const widget &s = *sb_id;
    CHECK( s.style() == "sidebar" );
    REQUIRE( s._widgets.size() == 2 );
    CHECK( s._widgets[0] == widget_id( "stats_native_test" ) );
    CHECK( s._widgets[1] == widget_id( "messages_native_test" ) );
}
