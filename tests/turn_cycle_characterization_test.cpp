/// Characterization of the turn-cycle copies, pinned through existing public seams.
///
/// The turn plan (plans/turn-plan-module.md) will replace the three copies of the
/// per-turn simulation: game::post_action_world_step (host/solo),
/// game::coop_client_turn_step (client avatar-local half) and do_turn's inline sim
/// block. These tests record today's observable behaviour so the cutover can be
/// shown to be behaviour-preserving.
///
/// do_turn() is not driven here: it runs the blocking input loop and has no
/// test seam. Its copy of the sim block is therefore not pinned by this file.
///
/// Documented gaps, deliberately not tested:
/// - ordering hazards: scent.set() before scent.update(), floor cache before
///   process_falling, pathfinding cleared last;
/// - the catch-up cap (coop_client.cpp:1028-1031), not driven through the sync path;
/// - host world_tick effects (fields, vehicles, process_falling), host kcal, thirst;
/// - vehicles on the client path.
///
/// Tags: [coop][parity][sim]

#include "avatar.h"
#include "calendar.h"
#include "catch/catch_amalgamated.hpp"
#include "coop_session.h"
#include "debug.h"
#include "game.h"
#include "map_helpers.h"
#include "monster.h"
#include "scent_map.h"
#include "state_helpers.h"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <ranges>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

/// RAII guard: sets coop_session::mode on construction, restores on destruction.
struct coop_mode_guard {
    coop_mode saved;
    explicit coop_mode_guard(coop_mode m): saved(coop_session::get().mode) {
        coop_session::get().mode = m;
    }
    ~coop_mode_guard() { coop_session::get().mode = saved; }
    coop_mode_guard(const coop_mode_guard&) = delete;
    auto operator=(const coop_mode_guard&) -> coop_mode_guard& = delete; // *NOPAD*
};

/// Fresh world with grass, as the in-process harness builds it.
auto setup_world() -> void {
    clear_all_state();
    build_test_map(ter_id("t_grass"));
}

/// Redirects std::cerr into a buffer for its lifetime. The test runner sends debug
/// output to std::cerr (test_main.cpp setupDebug(DebugOutput::std_err)), so this is
/// where [sim][perf] lines land during a test.
struct cerr_capture {
    std::ostringstream text;
    std::streambuf* saved;
    cerr_capture(): saved(std::cerr.rdbuf(text.rdbuf())) {}
    ~cerr_capture() { std::cerr.rdbuf(saved); }
    cerr_capture(const cerr_capture&) = delete;
    auto operator=(const cerr_capture&) -> cerr_capture& = delete; // *NOPAD*
};

/// Runs @p steps host turns with debug output captured and returns what was logged.
/// The [sim][perf] emitter logs under DC::Game, so that class is enabled for the
/// capture. debug.h has no getter for the live class mask, so the mask is restored to
/// the default that setupDebug() installs (debug.cpp), not to the exact prior value.
auto capture_host_turns_log(int steps) -> std::string {
    cerr_capture capture;
    setDebugLogClasses(enum_bitset<DC>().set(DC::Main).set(DC::DebugMsg).set(DC::Game), true);
    for ([[maybe_unused]] const auto step : std::views::iota(0, steps)) {
        g->post_action_world_step();
    }
    setDebugLogClasses(enum_bitset<DC>().set(DC::Main).set(DC::DebugMsg), true);
    flush_debug_log();
    return capture.text.str();
}

} // namespace

// Host/solo copy: one call advances the calendar by exactly one turn. This is the
// anchor that the cutover must keep: calendar advance happens once per call.
TEST_CASE("turn cycle: host step advances the calendar by one turn", "[coop][parity]") {
    setup_world();
    // The first step after a load or new game only clears new_game and skips the turn
    // advance (game_action.cpp, post_action_world_step). Clear it so this test measures
    // a normal step.
    g->new_game = false;

    const auto before = calendar::turn;
    g->post_action_world_step();
    CHECK(calendar::turn == before + 1_turns);
}

// Client copy: the avatar-local half never advances world time. The host reports
// advanced turns through apply_sync(), which owns the calendar, so the client step
// must leave calendar::turn untouched.
TEST_CASE("turn cycle: client step does not advance the calendar", "[coop][parity]") {
    setup_world();
    coop_mode_guard mcli(coop_mode::client);

    const auto before = calendar::turn;
    g->coop_client_turn_step();
    CHECK(calendar::turn == before);
}

// The [sim][perf] line is emitted once per 20 host turns. Twenty consecutive calls
// therefore produce exactly one line, whatever the counter was when they started.
// Its field set is consumed by tooling, so each field must still be present and
// hold a non-negative average.
TEST_CASE(
    "turn cycle: [sim][perf] line after 20 turns keeps its field set", "[coop][parity][sim]") {
    setup_world();
    // Clear new_game so the 20 steps are normal steps, as in the other host cases.
    g->new_game = false;

    const auto log = capture_host_turns_log(20);

    static const std::regex perf_re(
        R"(\[sim\]\[perf\] (\d+) turns avg: sim_total=([-+0-9.eE]+)ms )"
        R"(\(build_map_cache=([-+0-9.eE]+) monmove=([-+0-9.eE]+) world_tick=([-+0-9.eE]+)\))");

    const auto matches =
        std::ranges::
            subrange(std::sregex_iterator(log.begin(), log.end(), perf_re), std::sregex_iterator())
        | std::ranges::to<std::vector<std::smatch>>();
    REQUIRE(matches.size() == 1);
    const auto& line = matches.front();
    CHECK(line[1].str() == "20");
    for (const auto field : std::views::iota(std::size_t{2}, std::size_t{6})) {
        const auto value = std::stod(line[field].str());
        CHECK(std::isfinite(value));
        CHECK(value >= 0.0);
    }
}

// Host copy lays scent at the avatar tile: scent.set() runs before scent.update().
TEST_CASE("turn cycle: host step lays scent at the avatar tile", "[coop][parity]") {
    setup_world();
    g->new_game = false;
    g->u.scent = 500;
    const auto pos = g->u.bub_pos();
    g->scent.reset();
    REQUIRE(g->scent.get(pos) == 0);

    g->post_action_world_step();
    CHECK(g->scent.get(pos) > 0);
}

// The client copy lays scent at the avatar tile too, so scent is not a host-only effect.
TEST_CASE("turn cycle: client step lays scent at the avatar tile", "[coop][parity]") {
    setup_world();
    coop_mode_guard mcli(coop_mode::client);
    g->u.scent = 500;
    const auto pos = g->u.bub_pos();
    g->scent.reset();
    REQUIRE(g->scent.get(pos) == 0);

    g->coop_client_turn_step();
    CHECK(g->scent.get(pos) > 0);
}

// monmove() runs only in the host copy. A zombie four tiles east moves on a host step.
TEST_CASE("turn cycle: host step moves monsters", "[coop][parity]") {
    setup_world();
    g->new_game = false;
    const auto start = g->u.bub_pos() + tripoint_rel_ms{4, 0, 0};
    auto& zed = spawn_test_monster("mon_zombie", start);

    g->post_action_world_step();
    CHECK(zed.bub_pos() != start);
}

// The client copy does not call monmove(), so the same zombie stays where it was spawned.
TEST_CASE("turn cycle: client step leaves monsters where they are", "[coop][parity]") {
    setup_world();
    coop_mode_guard mcli(coop_mode::client);
    const auto start = g->u.bub_pos() + tripoint_rel_ms{4, 0, 0};
    auto& zed = spawn_test_monster("mon_zombie", start);

    g->coop_client_turn_step();
    CHECK(zed.bub_pos() == start);
}
