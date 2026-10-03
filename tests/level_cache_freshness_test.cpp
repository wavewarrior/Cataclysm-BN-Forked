#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#if defined(CATA_SDL)
#include "compute/gpu_lm.h"
#endif
#include "avatar.h"
#include "calendar.h"
#include "catch/catch_amalgamated.hpp"
#include "coordinates.h"
#include "game.h"
#include "game_constants.h"
#include "level_cache_freshness.h"
#include "lightmap.h"
#include "map.h"
#include "map_helpers.h"
#include "monster.h"
#include "options_helpers.h"
#include "state_helpers.h"
#include "type_id.h"
#include "veh_type.h"
#include "vehicle.h"
#include "weather.h"

// Stage-0 pinning tests for the Level cache freshness effort (spec #8, tickets #9/#10).
//
// Assert externally visible behaviour only: after a change to the world or the viewer,
// and the standard refresh, what do line-of-sight and lightmap queries answer?
//
// After a mutation under test the refresh is `refresh_view()`, which runs the production
// entry point `game::refresh_player_visibility_cache_if_needed` WITHOUT a leading
// invalidate, so a pin fails if the mutator stops raising the freshness state it relies
// on. `refresh_level_cache()` (invalidate first) is kept only for fixture setup and for
// the "full rebuild does reflect the edit" control, where masking is intended.
//
// These tests never assert which internal dirty bit was raised, nor the order of
// internal calls. Vocabulary: GLOSSARY.md; plan: plans/level-cache-freshness.md.

namespace {

constexpr tripoint_bub_ms player_home(60, 60, 0);

// A 3x3 solid block centred on `centre`: thick enough that any straight line from the
// west post to the east post crosses it, so the expected answer does not depend on
// Bresenham tie-breaking.
auto build_wall_block(const tripoint_bub_ms& centre) -> void {
    map& here = get_map();
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            here.ter_set(centre + tripoint(dx, dy, 0), ter_id("t_wall"));
        }
    }
}

// Open daylight ground, player placed, Level cache fresh.
auto set_up_open_daylight_map() -> void {
    clear_all_state();
    build_test_map(ter_id("t_dirt"));
    g->place_player(player_home);
    set_time(calendar::turn_zero + 12_hours);
    get_avatar().recalc_sight_limits();
    refresh_level_cache();
}

} // namespace

TEST_CASE("player move plus standard refresh updates line of sight", "[level_cache_freshness]") {
    set_up_open_daylight_map();
    auto& you = get_avatar();

    build_wall_block(tripoint_bub_ms(62, 60, 0));
    monster& z = spawn_test_monster("debug_mon", tripoint_bub_ms(65, 60, 0));
    refresh_level_cache();

    SECTION("the block hides the target") { CHECK_FALSE(you.sees(z)); }

    SECTION("walking around the block reveals the target") {
        g->place_player(tripoint_bub_ms(60, 66, 0));
        refresh_view();
        CHECK(you.sees(z));
    }

    SECTION("walking back behind the block hides it again") {
        g->place_player(tripoint_bub_ms(60, 66, 0));
        refresh_view();
        REQUIRE(you.sees(z));
        g->place_player(player_home);
        refresh_view();
        CHECK_FALSE(you.sees(z));
    }
}

TEST_CASE(
    "z-level change plus standard refresh updates visibility", "[level_cache_freshness][zlevel]") {
    clear_all_state();
    build_test_map(ter_id("t_dirt"));

    map& here = get_map();
    // A solid floor one level up, with the player standing on it: the tile directly
    // below is not visible. After the z-level change the player and that tile share a
    // level, with nothing in between, so the same query must answer visible.
    const tripoint_bub_ms player_above(60, 60, 1);
    const tripoint_bub_ms sample(60, 60, 0);
    const tripoint_bub_ms player_below(60, 63, 0);
    here.ter_set(player_above, ter_id("t_floor"));
    here.ter_set(sample, ter_id("t_floor"));

    g->place_player(player_above);
    set_time(calendar::turn_zero + 12_hours);
    get_avatar().recalc_sight_limits();
    refresh_level_cache({0, 1});

    SECTION("the solid floor hides the tile below") {
        CHECK_FALSE(here.pl_sees(sample, /*max_range=*/100));
    }

    SECTION("changing z-level and refreshing reveals it") {
        g->place_player(player_below);
        refresh_view();
        CHECK(here.pl_sees(sample, /*max_range=*/100));
    }
}

TEST_CASE("terrain edit plus standard refresh updates line of sight", "[level_cache_freshness]") {
    set_up_open_daylight_map();
    map& here = get_map();
    auto& you = get_avatar();

    monster& z = spawn_test_monster("debug_mon", tripoint_bub_ms(65, 60, 0));
    refresh_level_cache();
    REQUIRE(you.sees(z));

    SECTION("raising a wall block cuts the line") {
        build_wall_block(tripoint_bub_ms(62, 60, 0));
        refresh_view();
        CHECK_FALSE(you.sees(z));
    }

    SECTION("an open door keeps the line") {
        here.ter_set(tripoint_bub_ms(62, 60, 0), ter_id("t_door_o"));
        refresh_view();
        CHECK(you.sees(z));
    }

    SECTION("a closed door cuts the line") {
        here.ter_set(tripoint_bub_ms(62, 60, 0), ter_id("t_door_c"));
        refresh_view();
        CHECK_FALSE(you.sees(z));
    }
}

TEST_CASE("light change plus standard refresh updates the lightmap", "[level_cache_freshness]") {
    clear_all_state();
    build_test_map(ter_id("t_dirt"));

    map& here = get_map();
    // Roofed room at midnight: the sampled tile starts in the dark.
    for (int x = 56; x <= 64; x++) {
        for (int y = 56; y <= 64; y++) {
            here.ter_set(tripoint_bub_ms(x, y, 1), ter_id("t_flat_roof"));
        }
    }
    g->place_player(player_home);
    set_time(calendar::turn_zero);
    get_avatar().recalc_sight_limits();
    refresh_level_cache();

    const level_cache& cache = here.access_cache(player_home.z());
    const auto sample_idx = static_cast<size_t>(cache.idx(player_home.x() + 2, player_home.y()));
    CHECK(cache.lm[sample_idx] < LIGHT_AMBIENT_LIT);

    SECTION("an emissive tile lights the room after the refresh") {
        here.ter_set(player_home + tripoint_east, ter_id("t_utility_light"));
        refresh_view();
        CHECK(cache.lm[sample_idx] > LIGHT_AMBIENT_DIM);
    }
}

TEST_CASE(
    "vehicle move plus standard refresh updates line of sight",
    "[level_cache_freshness][vehicle]") {
    set_up_open_daylight_map();
    map& here = get_map();
    auto& you = get_avatar();

    monster& z = spawn_test_monster("debug_mon", tripoint_bub_ms(65, 60, 0));
    refresh_level_cache();
    REQUIRE(you.sees(z));

    // An opaque appliance board on a bare chassis, parked off the line. The first
    // committed move re-anchors the mount offsets relative to the reported vehicle
    // location, so the vehicle is nudged once and only afterwards is the blocking tile
    // read from the part position the engine reports.
    const tripoint_bub_ms blocked_tile(62, 60, 0);
    vehicle* veh = here.add_vehicle(vproto_id("none"), tripoint_bub_ms(63, 61, 0), 0_degrees, 0, 0);
    REQUIRE(veh != nullptr);
    REQUIRE(veh->install_part(tripoint_mnt_veh::zero(), vpart_id("frame_horizontal"), true) >= 0);
    const int board_index =
        veh->install_part(tripoint_mnt_veh::zero(), vpart_id("clothboard_horizontal"), true);
    REQUIRE(board_index >= 0);
    // Tile-stepped movement, not the Box2D readback path: the point here is the
    // cache notification a committed tile crossing produces.
    veh->box2d_position_authority = false;
    REQUIRE(here.displace_vehicle(*veh, tripoint_rel_ms(0, 1, 0)));
    refresh_view();
    const tripoint_bub_ms board_start = veh->bub_part_location(board_index);
    REQUIRE_FALSE(here.is_transparent(board_start));
    const tripoint drive_into_line = blocked_tile.raw() - board_start.raw();
    CAPTURE(board_start);
    CAPTURE(drive_into_line);
    // Parked one tile north of the blocking tile, so the drive is a straight cardinal
    // run down the player-target line.
    REQUIRE(drive_into_line == tripoint(0, -1, 0));
    REQUIRE(you.sees(z));
    here.take_vehicle_move_notifications();

    WHEN("the vehicle is driven into the line of sight") {
        REQUIRE(here.displace_vehicle(*veh, tripoint_rel_ms(drive_into_line)));
        THEN("the move really reached the cache bookkeeping") {
            CHECK(here.take_vehicle_move_notifications() > 0);
        }
        THEN("the standard refresh reports the blocked line") {
            refresh_view();
            CHECK_FALSE(you.sees(z));
        }
    }
}

// Issue #18 fixed this: the per-turn production refresh runs with
// `player_map_cache_current = true`, and a terrain edit that changes visibility INPUTS
// (transparency, seen) raises no visibility bit. The refresh entry point used to believe
// the caller's claim, skip the rebuild, and recompute the visibility cache from stale
// inputs, which reproduces the stale answer. It now verifies the claim against the
// module's own view of the inputs, so the edit is reflected.
TEST_CASE(
    "refresh reflects terrain edits that raise no visibility bit",
    "[level_cache_freshness][level_cache_stale_bug]") {
    set_up_open_daylight_map();
    auto& you = get_avatar();

    monster& z = spawn_test_monster("debug_mon", tripoint_bub_ms(65, 60, 0));
    refresh_level_cache();
    REQUIRE(you.sees(z));

    build_wall_block(tripoint_bub_ms(62, 60, 0));

    // game.cpp's per-turn call: same public entry point, told the map cache is
    // already current.
    g->refresh_player_visibility_cache_if_needed(
        /*player_map_cache_current=*/true,
        /*skip_lightmap=*/true);
    CHECK_FALSE(you.sees(z)); // issue #18: the edit is reflected, not stale

    SECTION("the full rebuild agrees") {
        refresh_level_cache();
        CHECK_FALSE(you.sees(z));
    }
}

// Issue #19: entity lights (a burning monster walking into view, a friend
// switching on a lamp) change the lightmap without raising any freshness bit,
// because the light-source signature was only sampled inside a build. A caller
// claiming the map cache is current got visibility from the stale lightmap.
// The refresh entry point now samples the signature itself whenever it is able
// to process the lightmap, so a light-only change forces the rebuilding repair.
TEST_CASE(
    "entity light plus standard refresh updates the lightmap",
    "[level_cache_freshness][level_cache_stale_bug]") {
    clear_all_state();
    build_test_map(ter_id("t_dirt"));
    map& here = get_map();
    // Roofed room at midnight: the sampled tile starts in the dark.
    for (int x = 56; x <= 64; x++) {
        for (int y = 56; y <= 64; y++) {
            here.ter_set(tripoint_bub_ms(x, y, 1), ter_id("t_flat_roof"));
        }
    }
    g->place_player(player_home);
    set_time(calendar::turn_zero);
    get_avatar().recalc_sight_limits();
    refresh_level_cache();

    const level_cache& cache = here.access_cache(player_home.z());
    const auto sample_idx = static_cast<size_t>(cache.idx(player_home.x() + 2, player_home.y()));
    CHECK(cache.lm[sample_idx] < LIGHT_AMBIENT_LIT);

    // A burning monster appears one tile beyond the sample point. Movement and
    // effects raise no freshness bit; only the light-source signature notices.
    // Fire light is intensity 8, which clears LIGHT_AMBIENT_DIM one tile away.
    monster& z = spawn_test_monster("mon_zombie", tripoint_bub_ms(62, 60, 0));
    z.add_effect(efftype_id("onfire"), 100_turns);

    SECTION("the caller-claims-current refresh propagates the light") {
        // The per-turn production shape: the caller asserts the map cache is
        // current. Nothing but the signature knows the monster brought light.
        g->refresh_player_visibility_cache_if_needed(
            /*player_map_cache_current=*/true,
            /*skip_lightmap=*/false);
        CHECK(cache.lm[sample_idx] > LIGHT_AMBIENT_DIM); // issue #19: not stale
    }

    SECTION("a repeat refresh with nothing changed costs no recomputation") {
        g->refresh_player_visibility_cache_if_needed(
            /*player_map_cache_current=*/true,
            /*skip_lightmap=*/false);
        here.take_visibility_cache_updates();
        g->refresh_player_visibility_cache_if_needed(
            /*player_map_cache_current=*/true,
            /*skip_lightmap=*/false);
        CHECK(here.take_visibility_cache_updates() == 0);
    }
}
// Measurement for user story 27: how many expensive visibility recomputations does one
// turn cost on current behaviour? `map::take_visibility_cache_updates()` counts
// `map::update_visibility_cache()` calls, each of which sweeps every loaded z-level.
TEST_CASE(
    "one turn costs a pinned number of visibility recomputations",
    "[level_cache_freshness][perf]") {
    const auto no_autosave = override_option("AUTOSAVE", "false");
    const auto no_activity_skip = override_option("ACTIVITY_SKIP_VISIBILITY", "false");
    set_up_open_daylight_map();

    // Hold the inputs still: no creatures, no activity, clear sky.
    get_weather().weather_id = weather_type_id("clear");
    clear_creatures();
    clear_npcs();

    get_map().take_visibility_cache_updates();
    CHECK_FALSE(g->do_turn());
    const unsigned updates = get_map().take_visibility_cache_updates();

    // Pinned value: measured on current behaviour (seed 1, tiles build). Stage 3
    // (view stale replacing the aggregate) is expected to lower this.
    CHECK(updates == 1);
}

// Issue #17: the view-stale condition replaces the never-cleared aggregate. It is
// false right after the standard refresh, true again on a move or a z-level change,
// and a refresh that finds it false costs no recomputation at all.
TEST_CASE(
    "view stale is false after a refresh and true again when the viewer moves",
    "[level_cache_freshness]") {
    set_up_open_daylight_map();
    map& here = get_map();
    auto& you = get_avatar();

    refresh_view();
    CHECK_FALSE(level_cache_freshness::visibility_stale(here, you.bub_pos()));

    SECTION("a move inside the bubble makes the view stale") {
        g->place_player(tripoint_bub_ms(61, 60, 0));
        CHECK(level_cache_freshness::visibility_stale(here, you.bub_pos()));
        refresh_view();
        CHECK_FALSE(level_cache_freshness::visibility_stale(here, you.bub_pos()));
    }

    SECTION("a z-level change makes the view stale") {
        g->place_player(tripoint_bub_ms(60, 60, 1));
        CHECK(level_cache_freshness::visibility_stale(here, you.bub_pos()));
        refresh_view();
        CHECK_FALSE(level_cache_freshness::visibility_stale(here, you.bub_pos()));
    }

    SECTION("a light-level report makes the view stale") {
        level_cache_freshness::report( here, level_cache_freshness::light_changed {
            .scope = level_cache_freshness::light_changed::lightmap_scope::none,
            .visibility = true } );
        CHECK(level_cache_freshness::visibility_stale(here, you.bub_pos()));
    }
}

// The within-turn saving: the refresh patterns that repeat within one turn (spell
// targeting, projectile animation, creature-hit messages, autodrive) each call the
// same entry point. With the aggregate they all rebuilt; with view stale only the
// first pays.
TEST_CASE(
    "repeated within-turn refreshes cost no extra recomputation",
    "[level_cache_freshness][perf]") {
    set_up_open_daylight_map();
    map& here = get_map();

    refresh_view();
    here.take_visibility_cache_updates();
    for (int i = 0; i < 4; i++) {
        refresh_view();
    }
    CHECK(here.take_visibility_cache_updates() == 0);

    SECTION("but a viewer move pays again") {
        g->place_player(tripoint_bub_ms(61, 60, 0));
        refresh_view();
        CHECK(here.take_visibility_cache_updates() == 1);
    }
}

// Geometry-only visibility is the documented answer while the view is stale.
TEST_CASE(
    "a stale view answers visibility from geometry alone",
    "[level_cache_freshness]") {
    set_up_open_daylight_map();
    map& here = get_map();
    auto& you = get_avatar();

    monster& z = spawn_test_monster("debug_mon", tripoint_bub_ms(65, 60, 0));
    refresh_view();
    REQUIRE(you.sees(z));

    // Stale the view without rebuilding, then drop a wall in the corridor. The
    // cached answer is unchanged: readers that need exactness refresh first.
    level_cache_freshness::report( here, level_cache_freshness::light_changed {
        .scope = level_cache_freshness::light_changed::lightmap_scope::none,
        .visibility = true } );
    build_wall_block(tripoint_bub_ms(62, 60, 0));
    CHECK(you.sees(z));

    refresh_view();
    CHECK_FALSE(you.sees(z));
}

// Issue #20: the event-based per-level residency generation. Every change kind the
// module exposes advances it for every level the kind affects, even when the content
// ends up identical — that is the difference from the content-gated outside generation
// and the point of the residency seam (spec #8, user stories 18/19). The transparency
// generation keeps its own meaning; the outside generation keeps its content gate.
namespace {

// Residency generations of the z-window the pins examine.
std::array<std::uint64_t, 3> residency_base(map& here) {
    return { here.access_cache(-1).residency_generation,
             here.access_cache(0).residency_generation,
             here.access_cache(1).residency_generation };
}

} // namespace

TEST_CASE(
    "every change kind advances the residency generation of the affected level",
    "[level_cache_freshness]") {
    set_up_open_daylight_map();
    map& here = get_map();

    using K = level_cache_freshness;

    SECTION("terrain changed advances the tile's level") {
        const auto base = residency_base(here);
        K::report(here, K::terrain_changed{ .at = player_home });
        CHECK(here.access_cache(0).residency_generation > base[1]);
    }

    SECTION("terrain changed with the weather scope advances every loaded level") {
        const auto base = residency_base(here);
        K::report(here, K::terrain_changed{
            .at = player_home,
            .transparency = true,
            .scope = K::terrain_changed::transparency_scope::all_levels });
        CHECK(here.access_cache(-1).residency_generation > base[0]);
        CHECK(here.access_cache(0).residency_generation > base[1]);
        CHECK(here.access_cache(1).residency_generation > base[2]);
    }

    SECTION("light changed advances the affected levels") {
        const auto base = residency_base(here);
        K::report(here, K::light_changed{});
        CHECK(here.access_cache(0).residency_generation > base[1]);
    }

    SECTION("vehicle moved advances its level and the level above") {
        const auto base = residency_base(here);
        K::report(here, K::vehicle_moved{
            .sm_min = tripoint_bub_sm(2, 2, 0),
            .sm_max = tripoint_bub_sm(3, 3, 0),
            .z = 0 });
        CHECK(here.access_cache(0).residency_generation > base[1]);
        CHECK(here.access_cache(1).residency_generation > base[2]);
    }

    SECTION("player moved advances the levels") {
        const auto base = residency_base(here);
        K::report(here, K::player_moved{});
        CHECK(here.access_cache(0).residency_generation > base[1]);
    }

    SECTION("z-level changed advances the levels") {
        const auto base = residency_base(here);
        K::report(here, K::z_level_changed{});
        CHECK(here.access_cache(0).residency_generation > base[1]);
    }

    SECTION("map shifted advances every loaded level") {
        const auto base = residency_base(here);
        K::report(here, K::map_shifted{ .shift = point_rel_sm(1, 0), .player_z = 0 });
        CHECK(here.access_cache(-1).residency_generation > base[0]);
        CHECK(here.access_cache(0).residency_generation > base[1]);
        CHECK(here.access_cache(1).residency_generation > base[2]);
    }

    SECTION("world replaced advances every loaded level") {
        const auto base = residency_base(here);
        const int ms = here.getmapsize();
        K::report(here, K::world_replaced{
            .first = tripoint_bub_sm(0, 0, 0),
            .last = tripoint_bub_sm(ms - 1, ms - 1, 0) });
        CHECK(here.access_cache(-1).residency_generation > base[0]);
        CHECK(here.access_cache(0).residency_generation > base[1]);
        CHECK(here.access_cache(1).residency_generation > base[2]);
    }

    SECTION("a production terrain edit advances the residency generation") {
        // The kind is what the mutators report, so a real edit must move the
        // generation the residency layer polls.
        const auto base = residency_base(here);
        here.ter_set(player_home, ter_id("t_grass"));
        CHECK(here.access_cache(0).residency_generation > base[1]);
    }
}

TEST_CASE(
    "the residency generation advances even when the content is unchanged",
    "[level_cache_freshness]") {
    set_up_open_daylight_map();
    map& here = get_map();

    // A reported event whose property diff is empty — the shape of a paint that
    // repaints identical terrain — raises no dirty bit, yet the event-based
    // residency generation must still advance (user story 19). This is exactly
    // what separates it from the content-gated outside generation below.
    const std::uint64_t before = here.access_cache(0).residency_generation;
    level_cache_freshness::report(here, level_cache_freshness::terrain_changed{
        .at = player_home,
        .support_above = false,
        .memory_seen = false });
    CHECK(here.access_cache(0).residency_generation > before);
}

TEST_CASE(
    "the outside generation still does not advance when content is unchanged",
    "[level_cache_freshness]") {
    set_up_open_daylight_map();
    map& here = get_map();
    level_cache& ch = here.access_cache(0);

    // The content gate is the outside generation's meaning and issue #20 keeps it:
    // recording the same checksum twice advances nothing, while a new one does.
    const std::uint64_t before = level_cache_freshness::outside_generation(ch);
    level_cache_freshness::record_outside_content(ch, 0xC0FFEE);
    const std::uint64_t after_first = level_cache_freshness::outside_generation(ch);
    CHECK(after_first > before);
    level_cache_freshness::record_outside_content(ch, 0xC0FFEE);
    CHECK(level_cache_freshness::outside_generation(ch) == after_first);
    level_cache_freshness::record_outside_content(ch, 0xBEEF);
    CHECK(level_cache_freshness::outside_generation(ch) > after_first);
}
// Issue #21: the GPU residency layer learns about CPU changes by polling the
// per-level residency generations, not by being called. The jump counter is the
// headless window into what the poll observed: a change kind that advances a
// generation must eventually advance the counter, and a quiet poll must not.
TEST_CASE(
    "the residency poll notices generation jumps headlessly",
    "[level_cache_freshness]") {
#if defined(CATA_SDL)
    set_up_open_daylight_map();
    map& here = get_map();
    const auto poll = [&here]() {
        cata_gpu::poll_lighting_residency( {
            .device = nullptr,
            .m = &here,
            .cache_x = here.access_cache( 0 ).cache_x,
            .cache_y = here.access_cache( 0 ).cache_y,
            .z_count = OVERMAP_LAYERS,
        } );
    };
    using K = level_cache_freshness;

    // Adopt the current stamps, then a single terrain edit advances the counter
    // (the kind touches its level and neighbours); a second poll with no new
    // change observes nothing.
    poll();
    const std::uint64_t before = cata_gpu::lighting_residency_jump_count();
    K::report( here, K::terrain_changed { .at = player_home } );
    poll();
    const std::uint64_t after_edit = cata_gpu::lighting_residency_jump_count();
    CHECK( after_edit > before );
    poll();
    CHECK( cata_gpu::lighting_residency_jump_count() == after_edit );

    // A committed vehicle move advances its level and the level above; the
    // poll notices the jumps.
    poll();
    const std::uint64_t veh_before = cata_gpu::lighting_residency_jump_count();
    K::report( here, K::vehicle_moved {
        .sm_min = tripoint_bub_sm( 1, 1, 0 ),
        .sm_max = tripoint_bub_sm( 2, 2, 0 ),
        .z = 0 } );
    poll();
    CHECK( cata_gpu::lighting_residency_jump_count() > veh_before );
#endif // CATA_SDL
}

// ADR-0002: the rebuild plan is the single carrier of the refresh decision. These
// pins hold the plan's contract: the escalation law (a skip policy may neither
// escalate nor consume the light-source signature), the level lists the build is
// licensed to rebuild, the view-stale answer, and the residency stamps the GPU
// consumer will diff once T7 replaces the poll.
TEST_CASE(
    "the rebuild plan defers the lightmap under the skip policy without escalating",
    "[level_cache_freshness]") {
    clear_all_state();
    build_test_map( ter_id( "t_dirt" ) );
    map &here = get_map();
    g->place_player( player_home );
    set_time( calendar::turn_zero );
    get_avatar().recalc_sight_limits();
    refresh_level_cache();

    using K = level_cache_freshness;
    const auto pose = K::pose_of_viewer( get_avatar(), 0 );

    SECTION( "a clean lightmap defers under both policies" ) {
        CHECK( K::plan_for( here, pose, K::lightmap_policy::normal ).lightmap ==
               K::lightmap_disposition::defer_without_escalation );
        CHECK( K::plan_for( here, pose, K::lightmap_policy::skip ).lightmap ==
               K::lightmap_disposition::defer_without_escalation );
    }

    SECTION( "a raised lightmap bit processes under normal and defers under skip" ) {
        K::report( here, K::light_changed {} );
        CHECK( K::plan_for( here, pose, K::lightmap_policy::normal ).lightmap ==
               K::lightmap_disposition::process );
        CHECK( K::plan_for( here, pose, K::lightmap_policy::skip ).lightmap ==
               K::lightmap_disposition::defer_without_escalation );
    }

    SECTION( "skip may not consume the signature an entity light changed" ) {
        // A burning monster changes the light-source signature and raises no bit
        // of its own (issue #19 shape). A skip plan must not sample the signature:
        // it cannot clear the bit a changed signature raises, so it must not raise
        // one either.
        monster &z = spawn_test_monster( "mon_zombie", tripoint_bub_ms( 62, 60, 0 ) );
        z.add_effect( efftype_id( "onfire" ), 100_turns );
        const auto skipped = K::plan_for( here, pose, K::lightmap_policy::skip );
        CHECK( skipped.lightmap == K::lightmap_disposition::defer_without_escalation );
        CHECK_FALSE( K::stale( here.access_cache( 0 ), level_cache_part::lightmap ) );
        // The signature stayed unconsumed: the next normal plan escalates.
        CHECK( K::plan_for( here, pose, K::lightmap_policy::normal ).lightmap ==
               K::lightmap_disposition::process );
        CHECK( K::stale( here.access_cache( 0 ), level_cache_part::lightmap ) );
    }
}

TEST_CASE(
    "the rebuild plan lists the levels the dirty state licenses for rebuild",
    "[level_cache_freshness]") {
    set_up_open_daylight_map();
    map &here = get_map();
    using K = level_cache_freshness;
    const auto pose = K::pose_of_viewer( get_avatar(), 0 );
        // Builder-owned vehicle flags survive `clear_all_state` and can carry over
        // from a passing vehicle test; silence them so the plan measures the fixture.
        for( int z = -OVERMAP_DEPTH; z <= OVERMAP_HEIGHT; ++z ) {
            level_cache &ch = here.access_cache( z );
            K::stamp_veh_range( ch, false );
            K::stamp_vehicle_floor( ch, false );
        }

    SECTION( "a fresh world yields an empty plan" ) {
        const auto plan = K::plan_for( here, pose, K::lightmap_policy::normal );
        CHECK_FALSE( plan.structure );
        CHECK( plan.structure_levels.empty() );
        CHECK( plan.floor_levels.empty() );
        CHECK( plan.transparency_levels.empty() );
        CHECK_FALSE( plan.visibility );
    }

    SECTION( "a terrain edit lists its level for every affected part" ) {
        // An opaque wall: the property diff raises transparency and seen; the
        // floor cache does not distinguish dirt from wall, so no floor raise.
        here.ter_set( player_home, ter_id( "t_wall" ) );
        const auto plan = K::plan_for( here, pose, K::lightmap_policy::normal );
        CHECK( plan.structure );
        CHECK( std::ranges::find( plan.transparency_levels, 0 ) !=
               plan.transparency_levels.end() );
        // The structure set is the union: every listed level appears in it.
        for( const auto *levels : { &plan.transparency_levels } ) {
            for( const int z : *levels ) {
                CHECK( std::ranges::find( plan.structure_levels, z ) !=
                       plan.structure_levels.end() );
            }
        }
        // Seen dirt from the edit rides through as the visibility decision.
        CHECK( plan.visibility );
    }

    SECTION( "a viewer move sets visibility without licensing any rebuild" ) {
        g->place_player( tripoint_bub_ms( 61, 60, 0 ) );
        const auto moved = K::pose_of_viewer( get_avatar(), 0 );
        const auto plan = K::plan_for( here, moved, K::lightmap_policy::normal );
        CHECK( plan.visibility );
        CHECK_FALSE( plan.structure );
        CHECK( plan.structure_levels.empty() );
    }

    SECTION( "the plan carries the current residency stamps and pose" ) {
        const auto plan = K::plan_for( here, pose, K::lightmap_policy::normal );
        for( int z = -OVERMAP_DEPTH; z <= OVERMAP_HEIGHT; ++z ) {
            CHECK( plan.residency.generation[static_cast<size_t>( z + OVERMAP_DEPTH )] ==
                   K::residency_generation( here.access_cache( z ) ) );
        }
        CHECK( plan.pose.viewer == pose.viewer );
        CHECK( plan.pose.camera == pose.camera );
        CHECK( plan.pose.bubble_origin == here.get_abs_sub() );
        // Two plans for the same pose compare equal; a camera drift does not.
        CHECK( K::plan_for( here, pose, K::lightmap_policy::normal ).pose == plan.pose );
        auto drifted = pose;
        drifted.camera = point_rel_ms( 1, 0 );
        CHECK( K::plan_for( here, drifted, K::lightmap_policy::normal ).pose != plan.pose );
    }
}
