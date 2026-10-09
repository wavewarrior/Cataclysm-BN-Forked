#include "catch/catch_amalgamated.hpp"
#include "driver_window.h"

#include <string>

// The windowed driver boot is told its window as `WxH` and puts it in a corner of the usable
// screen. The window itself needs a display session, so only the arithmetic is tested here; the
// contract suites under tools/bnplay check the boot against a real window.

TEST_CASE("driver_window_size_is_read_as_width_x_height", "[driver]") {
    const auto size = parse_driver_window_size("1280x720");
    REQUIRE(size.has_value());
    CHECK(size->width == 1280);
    CHECK(size->height == 720);
}

TEST_CASE("driver_window_size_refuses_anything_but_two_positive_whole_numbers", "[driver]") {
    for (const std::string text :
         {"", "1280", "1280x", "x720", "0x720", "1280x0", "-1x720", "1280x-720", "12.5x720",
          "1280X720", "1280x720x3", " 1280x720", "1280x720 ", "axb", "99999999999x720"}) {
        CAPTURE(text);
        CHECK_FALSE(parse_driver_window_size(text).has_value());
    }
}

TEST_CASE("driver_window_sits_in_the_bottom_right_corner_of_the_usable_screen", "[driver]") {
    // The usable area starts below a 25 px menu bar and ends above the dock.
    const screen_area usable{0, 25, 1440, 840};
    const screen_position at = driver_window_corner(usable, {1280, 720});
    CHECK(at.x == 160);
    CHECK(at.y == 145);
}

TEST_CASE("driver_window_corner_follows_a_display_that_does_not_start_at_the_origin", "[driver]") {
    const screen_area second{-1920, 0, 1920, 1055};
    const screen_position at = driver_window_corner(second, {1024, 768});
    CHECK(at.x == -1024);
    CHECK(at.y == 287);
}

TEST_CASE("driver_window_larger_than_the_screen_is_pinned_to_its_top_left", "[driver]") {
    const screen_area usable{0, 25, 1440, 840};
    const screen_position at = driver_window_corner(usable, {2000, 1200});
    CHECK(at.x == 0);
    CHECK(at.y == 25);
}
