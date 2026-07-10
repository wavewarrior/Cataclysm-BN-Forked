/**
 * B4 Phase 1 — resolve_aim_line() tests.
 *
 * Key invariant: resolve_aim_line() is deterministic.  The same (source, target)
 * pair must produce byte-identical output on every call, on every machine,
 * regardless of RNG state.  This is what makes it safe for co-op client-side
 * visual prediction without diverging from the server's trajectory.
 *
 * These tests exercise the geometric properties; they do NOT test the RNG-dependent
 * resolve_hit() stage (Phase 2) or emit_visuals() (Phase 3).
 */

#include "catch/catch_amalgamated.hpp"
#include "fire_cmd.h"
#include "line.h"
#include "map_helpers.h"
#include "rng.h"
#include "state_helpers.h"
#include "type_id.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

/// Set up a minimal open terrain world for each test.
void setup_world() {
    clear_all_state();
    build_test_map(ter_id("t_grass"));
    put_player_underground();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// 1. Determinism — same inputs, same output every time regardless of RNG state
// ---------------------------------------------------------------------------

TEST_CASE(
    "resolve_aim_line — identical result on repeated calls (determinism)",
    "[fire_cmd][aim_"
    "line]") {
    setup_world();

    const tripoint_bub_ms src{35, 35, 0};
    const tripoint_bub_ms dst{45, 38, 0};

    const auto traj_a = resolve_aim_line(src, dst);
    const auto traj_b = resolve_aim_line(src, dst);
    const auto traj_c = resolve_aim_line(src, dst);

    REQUIRE(!traj_a.empty());
    CHECK(traj_a == traj_b);
    CHECK(traj_b == traj_c);
}

TEST_CASE("resolve_aim_line — deterministic on cardinal N", "[fire_cmd][aim_line]") {
    setup_world();

    const tripoint_bub_ms src{35, 35, 0};
    const tripoint_bub_ms dst{35, 25, 0}; // 10 tiles north

    const auto traj1 = resolve_aim_line(src, dst);
    const auto traj2 = resolve_aim_line(src, dst);

    REQUIRE(!traj1.empty());
    CHECK(traj1 == traj2);
    // Source is excluded (Bresenham emits first step, not origin).
    // First tile is adjacent to src; last tile is dst.
    CHECK(traj1.front() != src);
    CHECK(traj1.back() == dst);
}

// ---------------------------------------------------------------------------
// 2. Geometric properties — path connects source to target
// ---------------------------------------------------------------------------

TEST_CASE("resolve_aim_line — path starts at source and ends at target", "[fire_cmd][aim_line]") {
    setup_world();

    const tripoint_bub_ms src{30, 30, 0};
    const tripoint_bub_ms dst{45, 42, 0};

    const auto traj = resolve_aim_line(src, dst);

    REQUIRE(!traj.empty());
    // Source excluded: front() is the first step away from source.
    CHECK(traj.front() != src);
    CHECK(traj.back() == dst);
}

TEST_CASE("resolve_aim_line — path length is approximately the range", "[fire_cmd][aim_line]") {
    setup_world();

    const tripoint_bub_ms src{35, 35, 0};
    const tripoint_bub_ms dst{35, 25, 0}; // exactly 10 tiles north

    const auto traj = resolve_aim_line(src, dst);

    // Source excluded: path length == range (10 steps to cover 10 tiles).
    REQUIRE(!traj.empty());
    const int range = rl_dist(src, dst);
    CHECK(static_cast<int>(traj.size()) == range);
}

TEST_CASE("resolve_aim_line — consecutive tiles are adjacent", "[fire_cmd][aim_line]") {
    setup_world();

    const tripoint_bub_ms src{35, 35, 0};
    const tripoint_bub_ms dst{48, 40, 0};

    const auto traj = resolve_aim_line(src, dst);
    REQUIRE(traj.size() >= 2);

    for (size_t i = 1; i < traj.size(); ++i) {
        const auto delta = (traj[i] - traj[i - 1]).raw();
        CHECK(std::abs(delta.x) <= 1);
        CHECK(std::abs(delta.y) <= 1);
        CHECK(delta.z == 0);
    }
}

// ---------------------------------------------------------------------------
// 3. No RNG — verify result is stable across multiple independent calls
//    (if RNG were involved, repeated calls with intervening rng() draws would
//    produce different results — a full RNG audit requires reading source, but
//    behavioral consistency is the observable contract)
// ---------------------------------------------------------------------------

TEST_CASE(
    "resolve_aim_line — result stable despite interleaved rng draws",
    "[fire_cmd][aim_"
    "line]") {
    setup_world();

    const tripoint_bub_ms src{35, 35, 0};
    const tripoint_bub_ms dst{50, 42, 0};

    const auto baseline = resolve_aim_line(src, dst);
    REQUIRE(!baseline.empty());

    // Draw RNG between calls — must not change the result.
    for (int i = 0; i < 100; ++i) { rng(0, 1000); }

    const auto after_rng = resolve_aim_line(src, dst);
    CHECK(after_rng == baseline);
}
