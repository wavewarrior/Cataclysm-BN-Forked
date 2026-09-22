#include "catch/catch_amalgamated.hpp"
#include "lighting/tile_occlusion.h"
#include "lightmap.h"

// Consumer-visible contract for lighting::classify_tile_occlusion — see
// src/lighting/tile_occlusion.h for the rules. Two of these cases are the
// historical regressions the single-source classifier exists to prevent:
// windows landing on the SKY_WALL_H threshold (interior blackout), and the
// coverage-vs-transparency split (a window blocking light despite
// transmitting it).

TEST_CASE("classify_tile_occlusion open air transmits and has no height", "[lighting]")
{
    const lighting::tile_occlusion_query q{
        .transparency = LIGHT_TRANSPARENCY_OPEN_AIR,
        .coverage = 0,
    };
    const lighting::tile_occlusion out = lighting::classify_tile_occlusion(q);
    CHECK_FALSE(out.blocks_light);
    CHECK(out.height == 0.0f);
}

TEST_CASE("classify_tile_occlusion solid wall blocks at full coverage height", "[lighting]")
{
    const lighting::tile_occlusion_query q{
        .transparency = LIGHT_TRANSPARENCY_SOLID,
        .coverage = 100,
    };
    const lighting::tile_occlusion out = lighting::classify_tile_occlusion(q);
    CHECK(out.blocks_light);
    CHECK(out.height == Catch::Approx(1.0f));
}

TEST_CASE(
    "classify_tile_occlusion window transmits despite high coverage — daylight still "
    "reaches interiors",
    "[lighting]")
{
    // Regression: coverage 60 lands exactly on sky_sun.comp's SKY_WALL_H = 0.60
    // blocking threshold. A window (transparent, coverage 60) must never be
    // treated as a solid wall — the whole point of deriving "blocks light"
    // from the transparency cache instead of coverage.
    const lighting::tile_occlusion_query q{
        .transparency = LIGHT_TRANSPARENCY_OPEN_AIR,
        .coverage = 60,
    };
    const lighting::tile_occlusion out = lighting::classify_tile_occlusion(q);
    CHECK_FALSE(out.blocks_light);
    CHECK(out.height == 0.0f);
}

TEST_CASE("classify_tile_occlusion tree seeds the SDF but has zero march height",
          "[lighting]")
{
    // Phase 2.3: tree sun shadows come exclusively from the screen-space
    // silhouette mask, so a tree must not carry OccBuf height (which would make
    // the sun march and the sky-dome walk shadow it a second time). It stays
    // opaque — blocks_light seeds the SDF for point-light shadows / AO / GI.
    const lighting::tile_occlusion_query tree{
        .transparency = LIGHT_TRANSPARENCY_SOLID,
        .coverage = 80,
        .is_tree = true,
    };
    const lighting::tile_occlusion_query wall{
        .transparency = LIGHT_TRANSPARENCY_SOLID,
        .coverage = 80,
    };
    const lighting::tile_occlusion tree_out = lighting::classify_tile_occlusion(tree);
    const lighting::tile_occlusion wall_out = lighting::classify_tile_occlusion(wall);
    CHECK(tree_out.blocks_light);
    CHECK(tree_out.height == 0.0f);
    CHECK(wall_out.height > 0.0f);
}

TEST_CASE("classify_tile_occlusion vehicle obstacle over open air still blocks", "[lighting]")
{
    const lighting::tile_occlusion_query q{
        .transparency = LIGHT_TRANSPARENCY_OPEN_AIR,
        .coverage = 0,
        .is_vehicle_obstacle = true,
    };
    const lighting::tile_occlusion out = lighting::classify_tile_occlusion(q);
    CHECK(out.blocks_light);
    CHECK(out.height >= 1.0f);
}

TEST_CASE("classify_tile_occlusion invalid terrain during world load never blocks",
          "[lighting]")
{
    const lighting::tile_occlusion_query q{
        .transparency = LIGHT_TRANSPARENCY_SOLID,
        .coverage = 100,
        .terrain_valid = false,
    };
    const lighting::tile_occlusion out = lighting::classify_tile_occlusion(q);
    CHECK_FALSE(out.blocks_light);
    CHECK(out.height == 0.0f);
}

TEST_CASE("classify_tile_occlusion carries roof and open-sky flags through unchanged",
          "[lighting]")
{
    const lighting::tile_occlusion_query q{
        .transparency = LIGHT_TRANSPARENCY_OPEN_AIR,
        .floor_above = true,
        .outside = true,
    };
    const lighting::tile_occlusion out = lighting::classify_tile_occlusion(q);
    CHECK(out.roofed);
    CHECK(out.open_sky);
}
