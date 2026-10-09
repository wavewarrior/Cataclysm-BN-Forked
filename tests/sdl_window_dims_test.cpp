#include "catch/catch_amalgamated.hpp"
#include "cursesdef.h"
#include "game.h"
#include "point.h"
#include "sdltiles.h"

// The position-and-size overload describes an imaginary window, passed down as an empty one. A
// windowless game (the test runner, a driver Episode) has no terrain window either, so an empty
// window used to compare equal to `g->w_terrain`, take the tileset branch and dereference the
// absent tile context: opening the overmap crashed the game.
TEST_CASE("imaginary window dimensions do not take the terrain window's tileset", "[ui]") {
    REQUIRE(!g->w_terrain);

    const auto dim = get_window_dimensions(point(2, 3), point(10, 4));

    CHECK(dim.window_pos_cell == point(2, 3));
    CHECK(dim.window_size_cell == point(10, 4));
    CHECK(dim.window_size_pixel == point(10 * dim.scaled_font_size.x, 4 * dim.scaled_font_size.y));
}
