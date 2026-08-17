// Unit tests for the per-sprite lighting-composite classifier.
//
// Tagged `[lighting]` only — deliberately NOT `[.gpu]`. `classify_tile_light`
// is a pure function over plain data, so this needs no GPU, no game state and
// no loaded map, and must run in the ordinary `cata_test-tiles` CI lane.

#include "catch/catch_amalgamated.hpp"
#include "tile_light_mode.h"

// The classifier must remain usable at compile time: the renderer wants to be
// able to fold constant queries, and a `constexpr` body is the only thing
// keeping it free of hidden game-state dependencies.
static_assert(
    classify_tile_light(tile_light_query{
        .world_present = true,
        .lighting_ready = true,
    }) == sprite_light_mode::gpu_lit,
    "classify_tile_light must be a constant expression");

TEST_CASE("classify_tile_light picks the sprite composite mode", "[lighting]") {
    SECTION("memorized world tile with lighting ready cross-fades to memory") {
        CHECK(
            classify_tile_light(tile_light_query{
                .world_present = true,
                .lighting_ready = true,
                .memorized = true,
            })
            == sprite_light_mode::memory);
    }

    SECTION("standalone entity preview is unlit even in a lit world") {
        CHECK(
            classify_tile_light(tile_light_query{
                .as_independent_entity = true,
                .world_present = true,
                .lighting_ready = true,
            })
            == sprite_light_mode::unlit);
    }

    SECTION("overmap sprites are unlit: no SDF, sun, sky or emitters") {
        CHECK(
            classify_tile_light(tile_light_query{
                .is_overmap = true,
                .world_present = true,
                .lighting_ready = true,
            })
            == sprite_light_mode::unlit);
    }

    SECTION("no world (no game, or outside the map bubble) is unlit") {
        CHECK(
            classify_tile_light(tile_light_query{
                .world_present = false,
                .lighting_ready = true,
            })
            == sprite_light_mode::unlit);
    }

    SECTION("visible, non-memorized world tile is gpu_lit") {
        CHECK(
            classify_tile_light(tile_light_query{
                .world_present = true,
                .lighting_ready = true,
                .memorized = false,
            })
            == sprite_light_mode::gpu_lit);
    }

    SECTION("a pitch-dark visible world tile is still gpu_lit") {
        // THE REGRESSION. The rule this replaces tested per-tile brightness
        // (`lm[idx].max() > 0.001`) as a stand-in for world readiness, so a
        // genuinely dark but visible tile fell through to full unlit albedo
        // and threw its GPU shadows away. Brightness is deliberately not a
        // field of `tile_light_query` at all: there is nothing to set here,
        // and this MUST NOT be `unlit`. Darkness is a dark radiance.
        CHECK(
            classify_tile_light(tile_light_query{
                .world_present = true,
                .lighting_ready = true,
                .memorized = false,
            })
            == sprite_light_mode::gpu_lit);
    }

    SECTION("world present but no lightmap yet is unlit") {
        CHECK(
            classify_tile_light(tile_light_query{
                .world_present = true,
                .lighting_ready = false,
                .memorized = false,
            })
            == sprite_light_mode::unlit);
    }

    SECTION("readiness dominates memory") {
        // A memory sprite drawn before any lightmap exists must not cross-fade
        // toward an unpopulated radiance buffer.
        CHECK(
            classify_tile_light(tile_light_query{
                .world_present = true,
                .lighting_ready = false,
                .memorized = true,
            })
            == sprite_light_mode::unlit);
    }

    SECTION("UI dominates memory") {
        CHECK(
            classify_tile_light(tile_light_query{
                .as_independent_entity = true,
                .world_present = true,
                .lighting_ready = true,
                .memorized = true,
            })
            == sprite_light_mode::unlit);
    }
}
