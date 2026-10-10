/**
 * Characterization of the turn-cycle copies, pinned through existing public seams.
 *
 * The turn plan (plans/turn-plan-module.md) will replace the three copies of the
 * per-turn simulation: game::post_action_world_step (host/solo),
 * game::coop_client_turn_step (client avatar-local half) and do_turn's inline sim
 * block. These tests record today's observable behaviour so the cutover can be
 * shown to be behaviour-preserving.
 *
 * do_turn() is not driven here: it runs the blocking input loop and has no
 * test seam. Its copy of the sim block is therefore not pinned by this file.
 *
 * Tags: [coop][parity]
 */

#include "calendar.h"
#include "catch/catch_amalgamated.hpp"
#include "coop_session.h"
#include "debug.h"
#include "game.h"
#include "map_helpers.h"
#include "state_helpers.h"

#include <cmath>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>

namespace {

/// RAII guard: sets coop_session::mode on construction, restores on destruction.
struct coop_mode_guard {
    coop_mode saved;
    explicit coop_mode_guard(coop_mode m): saved(coop_session::get().mode) {
        coop_session::get().mode = m;
    }
    ~coop_mode_guard() { coop_session::get().mode = saved; }
    coop_mode_guard(const coop_mode_guard&) = delete;
    auto operator=(const coop_mode_guard&) -> coop_mode_guard& = delete;
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
    auto operator=(const cerr_capture&) -> cerr_capture& = delete;
};

} // namespace

// Host/solo copy: one call advances the calendar by exactly one turn. This is the
// anchor that the cutover must keep: calendar advance happens once per call.
TEST_CASE("turn cycle: host step advances the calendar by one turn", "[coop][parity]") {
    setup_world();

    const time_point before = calendar::turn;
    g->post_action_world_step();
    CHECK(calendar::turn == before + 1_turns);
}

// Client copy: the avatar-local half never advances world time. The host reports
// advanced turns through apply_sync(), which owns the calendar, so the client step
// must leave calendar::turn untouched.
TEST_CASE("turn cycle: client step does not advance the calendar", "[coop][parity]") {
    setup_world();
    coop_mode_guard mcli(coop_mode::client);

    const time_point before = calendar::turn;
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

    std::string log;
    {
        cerr_capture capture;
        setDebugLogClasses(enum_bitset<DC>().set(DC::Main).set(DC::DebugMsg).set(DC::Game), true);
        for (int i = 0; i < 20; ++i) { g->post_action_world_step(); }
        setDebugLogClasses(enum_bitset<DC>().set(DC::Main).set(DC::DebugMsg), true);
        flush_debug_log();
        log = capture.text.str();
    }

    static const std::regex perf_re(
        R"(\[sim\]\[perf\] (\d+) turns avg: sim_total=([-+0-9.eE]+)ms )"
        R"(\(build_map_cache=([-+0-9.eE]+) monmove=([-+0-9.eE]+) world_tick=([-+0-9.eE]+)\))");

    std::smatch last;
    bool found = false;
    for (auto it = std::sregex_iterator(log.begin(), log.end(), perf_re);
         it != std::sregex_iterator(); ++it) {
        last = *it;
        found = true;
    }
    REQUIRE(found);
    CHECK(last[1].str() == "20");
    for (std::size_t field = 2; field <= 5; ++field) {
        const double value = std::stod(last[field].str());
        CHECK(std::isfinite(value));
        CHECK(value >= 0.0);
    }
}
