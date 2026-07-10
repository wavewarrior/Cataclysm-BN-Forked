#ifdef COOP_ENABLED
/**
 * C2b TERRAIN_CHANGE — host-side apply_terrain_change tests.
 *
 * Exercises coop_server::apply_terrain_change() directly (no live server,
 * no proxy NPC) against a real game map.
 *
 * Schema under test: {"tx":N,"ty":N,"tz":N,"ter":"ter_id","furn":"furn_id"}
 *
 * Key invariants:
 *   1. Terrain ID is applied to the map at the correct position.
 *   2. Furniture ID is applied to the map at the correct position.
 *   3. Only terrain changes when furn is empty-string.
 *   4. Out-of-bounds tile is skipped silently.
 *
 * Tags: [coop][terrain]
 */

#include "calendar.h"
#include "catch/catch_amalgamated.hpp"
#include "coop_server.h"
#include "coordinates.h"
#include "game.h"
#include "map.h"
#include "map_helpers.h"
#include "mapdata.h"
#include "state_helpers.h"
#include "type_id.h"

#include <sstream>
#include <string>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

static constexpr tripoint_bub_ms TILE{40, 40, 0};

void setup_world() {
    clear_all_state();
    build_test_map(ter_id("t_grass"));
    put_player_underground();
}

auto make_terrain_manifest(
    const tripoint_abs_ms& pos, const std::string& ter, const std::string& furn) -> std::string {
    std::ostringstream oss;
    oss << "{\"tx\":" << pos.x() << ",\"ty\":" << pos.y() << ",\"tz\":" << pos.z() << ",\"ter\":\""
        << ter << "\",\"furn\":\"" << furn << "\"}";
    return oss.str();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// 1. Terrain ID applied to map
// ---------------------------------------------------------------------------

TEST_CASE("apply_terrain_change — terrain set on map", "[coop][terrain]") {
    setup_world();

    const tripoint_abs_ms abs = get_map().bub_to_abs(TILE);
    REQUIRE(get_map().ter(TILE) == ter_str_id("t_grass"));

    coop_server::apply_terrain_change(make_terrain_manifest(abs, "t_floor", ""));

    CHECK(get_map().ter(TILE) == ter_str_id("t_floor"));
}

// ---------------------------------------------------------------------------
// 2. Furniture ID applied to map
// ---------------------------------------------------------------------------

TEST_CASE("apply_terrain_change — furniture set on map", "[coop][terrain]") {
    setup_world();

    const tripoint_abs_ms abs = get_map().bub_to_abs(TILE);
    REQUIRE(get_map().furn(TILE) == furn_str_id("f_null"));

    coop_server::apply_terrain_change(make_terrain_manifest(abs, "t_floor", "f_locker"));

    CHECK(get_map().ter(TILE) == ter_str_id("t_floor"));
    CHECK(get_map().furn(TILE) == furn_str_id("f_locker"));
}

// ---------------------------------------------------------------------------
// 3. Empty furn string leaves furniture unchanged
// ---------------------------------------------------------------------------

TEST_CASE("apply_terrain_change — empty furn leaves furniture unchanged", "[coop][terrain]") {
    setup_world();

    // Pre-place furniture
    get_map().furn_set(TILE, furn_str_id("f_locker"));
    REQUIRE(get_map().furn(TILE) == furn_str_id("f_locker"));

    const tripoint_abs_ms abs = get_map().bub_to_abs(TILE);
    // Only terrain changes; furn field is empty
    coop_server::apply_terrain_change(make_terrain_manifest(abs, "t_floor", ""));

    CHECK(get_map().ter(TILE) == ter_str_id("t_floor"));
    CHECK(get_map().furn(TILE) == furn_str_id("f_locker")); // unchanged
}

// ---------------------------------------------------------------------------
// 4. Out-of-bounds position silently skipped
// ---------------------------------------------------------------------------

TEST_CASE("apply_terrain_change — out-of-bounds skipped", "[coop][terrain]") {
    setup_world();

    const std::string manifest = "{\"tx\":999999,\"ty\":999999,\"tz\":0,\"ter\":\"t_floor\","
                                 "\"furn\":\"\"}";

    REQUIRE_NOTHROW(coop_server::apply_terrain_change(manifest));
}

// ---------------------------------------------------------------------------
// 5. Unknown terrain id is skipped — tile stays unchanged, no t_null corruption
// ---------------------------------------------------------------------------

TEST_CASE( "apply_terrain_change — unknown ter id skipped", "[coop][terrain]" ) {
    setup_world();

    const tripoint_abs_ms abs = get_map().bub_to_abs( TILE );
    const ter_id original_ter = get_map().ter( TILE );

    // "t_nonexistent" is not a valid terrain id; it must NOT become t_null.
    coop_server::apply_terrain_change(
        make_terrain_manifest( abs, "t_nonexistent", "" ) );

    // Tile must be unchanged.
    CHECK( get_map().ter( TILE ) == original_ter );
    // And must NOT be t_null (open air) which would be worse than a skip.
    CHECK( get_map().ter( TILE ) != ter_str_id( "t_null" ) );
}

#endif // COOP_ENABLED
