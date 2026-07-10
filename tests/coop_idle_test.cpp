// A5.4 — both_idle() / fast-forward verification
//
// Tests the real coop_server::both_idle() and coop_server::maybe_fast_forward()
// against live g->u state.  A coop_server object can be constructed without a
// network socket; both_idle() only reads g->u and client_is_idle_, so no
// actual co-op session is needed.
//
// Coverage:
//   1. both_idle() truth table — four combinations of host/client idle state
//   2. maybe_fast_forward() saturates g->main_loop_accum_ms_ to
//      COOP_FAST_FORWARD_ACCUM_MS when both idle, leaves it untouched otherwise
//   3. Constant consistency: COOP_FAST_FORWARD_ACCUM_MS / 1000.0 == COOP_MAX_CATCH_UP
//      (verifies the magic literal agrees with the cap constant)
//
// Gaps not covered here (need integration test / manual verification):
//   - Actual per-tick client_status packet parsing setting client_is_idle_
//   - Main-loop accumulator consumption (IDLE_TICK_INTERVAL_MS drain in main.cpp)
//   - apply_sync() process_turn() catch-up loop exercise

#ifdef COOP_ENABLED

#include "avatar.h"
#include "catch/catch_amalgamated.hpp"
#include "coop_proto.h"
#include "coop_server.h"
#include "game.h"
#include "map_helpers.h"
#include "state_helpers.h"
#include "type_id.h"

static const efftype_id effect_sleep("sleep");

namespace {

/// Scoped helper: puts g->u to sleep for the duration of the scope.
struct SleepingAvatar {
    SleepingAvatar() { g->u.add_effect(effect_sleep, 24_hours); }
    ~SleepingAvatar() { g->u.remove_effect(effect_sleep); }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// 1. both_idle() truth table
// ---------------------------------------------------------------------------

TEST_CASE("both_idle — host awake, client active → false", "[coop][idle]") {
    clear_all_state();
    build_test_map(ter_id("t_grass"));
    put_player_underground();

    REQUIRE_FALSE(g->u.in_sleep_state());

    coop_server server;
    server.set_client_idle_for_test(false);

    CHECK_FALSE(server.both_idle());
}

TEST_CASE("both_idle — host sleeping, client active → false", "[coop][idle]") {
    clear_all_state();
    build_test_map(ter_id("t_grass"));
    put_player_underground();

    SleepingAvatar _sleep;
    REQUIRE(g->u.in_sleep_state());

    coop_server server;
    server.set_client_idle_for_test(false);

    CHECK_FALSE(server.both_idle());
}

TEST_CASE("both_idle — host awake, client idle → false", "[coop][idle]") {
    clear_all_state();
    build_test_map(ter_id("t_grass"));
    put_player_underground();

    REQUIRE_FALSE(g->u.in_sleep_state());

    coop_server server;
    server.set_client_idle_for_test(true);

    CHECK_FALSE(server.both_idle());
}

TEST_CASE("both_idle — host sleeping, client idle → true", "[coop][idle]") {
    clear_all_state();
    build_test_map(ter_id("t_grass"));
    put_player_underground();

    SleepingAvatar _sleep;
    REQUIRE(g->u.in_sleep_state());

    coop_server server;
    server.set_client_idle_for_test(true);

    CHECK(server.both_idle());
}

// ---------------------------------------------------------------------------
// 2. maybe_fast_forward() accumulator saturation
// ---------------------------------------------------------------------------

TEST_CASE(
    "maybe_fast_forward — sets accum to COOP_FAST_FORWARD_ACCUM_MS when both idle",
    "[coop]["
    "idle]") {
    clear_all_state();
    build_test_map(ter_id("t_grass"));
    put_player_underground();

    SleepingAvatar _sleep;
    REQUIRE(g->u.in_sleep_state());

    coop_server server;
    server.set_client_idle_for_test(true);
    REQUIRE(server.both_idle());

    g->main_loop_accum_ms_ = 0.0;
    const auto triggered = server.maybe_fast_forward();

    CHECK(triggered);
    CHECK(g->main_loop_accum_ms_ == COOP_FAST_FORWARD_ACCUM_MS);
}

TEST_CASE("maybe_fast_forward — does not touch accum when client is active", "[coop][idle]") {
    clear_all_state();
    build_test_map(ter_id("t_grass"));
    put_player_underground();

    SleepingAvatar _sleep;
    REQUIRE(g->u.in_sleep_state());

    coop_server server;
    server.set_client_idle_for_test(false); // client active
    REQUIRE_FALSE(server.both_idle());

    g->main_loop_accum_ms_ = 42.0;
    const auto triggered = server.maybe_fast_forward();

    CHECK_FALSE(triggered);
    CHECK(g->main_loop_accum_ms_ == 42.0); // unchanged
}

// ---------------------------------------------------------------------------
// 3. Constant consistency
// ---------------------------------------------------------------------------

TEST_CASE("COOP_FAST_FORWARD_ACCUM_MS is derived from COOP_MAX_CATCH_UP * COOP_IDLE_TICK_MS", "[coo"
                                                                                              "p]["
                                                                                              "idle"
                                                                                              "]") {
    // COOP_FAST_FORWARD_ACCUM_MS is now defined as COOP_MAX_CATCH_UP * COOP_IDLE_TICK_MS
    // in coop_proto.h — this test is the runtime witness that the definition holds.
    // If COOP_IDLE_TICK_MS drifts from IDLE_TICK_INTERVAL_MS in main.cpp, the fast-forward
    // will fire at the wrong cadence; update COOP_IDLE_TICK_MS to match.
    CHECK(COOP_FAST_FORWARD_ACCUM_MS == static_cast<double>(COOP_MAX_CATCH_UP) * COOP_IDLE_TICK_MS);
}

#endif // COOP_ENABLED
