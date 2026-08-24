#include "cata_tiles_internal.h"
#include "catch/catch_amalgamated.hpp"

TEST_CASE("overhangs_tile detects canopy overhang", "[tileset][canopy]") {
    const int tw = 32;
    const int th = 32;
    const int tile_x = 100;
    const int tile_y = 200;

    // Exactly the tile square -> no overhang.
    CHECK_FALSE(cata_tiles_internal::overhangs_tile(
        SDL_Rect{tile_x, tile_y, tw, th}, tile_x, tile_y, tw, th));

    // One pixel past each edge -> overhang.
    CHECK(cata_tiles_internal::overhangs_tile(
        SDL_Rect{tile_x, tile_y, tw + 1, th}, tile_x, tile_y, tw, th));
    CHECK(cata_tiles_internal::overhangs_tile(
        SDL_Rect{tile_x, tile_y, tw, th + 1}, tile_x, tile_y, tw, th));
    CHECK(cata_tiles_internal::overhangs_tile(
        SDL_Rect{tile_x - 1, tile_y, tw + 1, th}, tile_x, tile_y, tw, th));
    CHECK(cata_tiles_internal::overhangs_tile(
        SDL_Rect{tile_x, tile_y - 1, tw, th + 1}, tile_x, tile_y, tw, th));

    // A 128x160 canopy (4x5 tiles at 32px) anchored at (-48, -128) overhangs left,
    // up and right; the tile square itself is only 1 of the 20 covered tiles.
    CHECK(cata_tiles_internal::overhangs_tile(
        SDL_Rect{tile_x - 48, tile_y - 128, 4 * tw, 5 * th}, tile_x, tile_y, tw, th));

    // A small sprite fully inside the square never overhangs.
    CHECK_FALSE(cata_tiles_internal::overhangs_tile(
        SDL_Rect{tile_x + 8, tile_y + 16, 16, 10}, tile_x, tile_y, tw, th));

    // Zero-sized (blank placeholder) sprite never overhangs.
    CHECK_FALSE(
        cata_tiles_internal::overhangs_tile(SDL_Rect{tile_x, tile_y, 0, 0}, tile_x, tile_y, tw, th));
}
