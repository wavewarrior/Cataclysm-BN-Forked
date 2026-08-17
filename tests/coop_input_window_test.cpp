#include "catch/catch_amalgamated.hpp"
#include "coop_input_window.h"
#include "coop_proto.h"

#include <cmath>
#include <deque>
#include <limits>
#include <string>

/// Convenience: an evictable action stamped at `t`.
static auto act(const char* name, double t) -> buffered_action {
    return {.action = name, .enqueued_ms = t, .evictable = true};
}

/// Convenience: a non-evictable (menu/info) action stamped at `t`.
static auto keep(const char* name, double t) -> buffered_action {
    return {.action = name, .enqueued_ms = t, .evictable = false};
}

// ── Window sizing ────────────────────────────────────────────────────────────
// The window must track the *slower* of host and client: an action is not stale
// until whoever has to resolve it has had time to.

TEST_CASE("coop_input_window_ms floors at the minimum", "[coop][inputwindow]") {
    CHECK(coop_input_window_ms(5.0, 5.0) == Catch::Approx(COOP_INPUT_WINDOW_MIN_MS));
    CHECK(coop_input_window_ms(0.0, 0.0) == Catch::Approx(COOP_INPUT_WINDOW_MIN_MS));
}

TEST_CASE("coop_input_window_ms is capped at the maximum", "[coop][inputwindow]") {
    CHECK(coop_input_window_ms(900.0, 40.0) == Catch::Approx(COOP_INPUT_WINDOW_MAX_MS));
}

TEST_CASE("coop_input_window_ms takes the slower side", "[coop][inputwindow]") {
    // Remote slower than local — the remote value wins.  This is the "longest taking
    // action of host and client" requirement.
    CHECK(coop_input_window_ms(40.0, 120.0) == Catch::Approx(120.0));
    // ...and symmetrically.
    CHECK(coop_input_window_ms(120.0, 40.0) == Catch::Approx(120.0));
}

TEST_CASE("coop_input_window_ms ignores non-finite estimates", "[coop][inputwindow]") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    // A poisoned estimate must not blow the window past its ceiling nor below its floor.
    CHECK(coop_input_window_ms(nan, 120.0) == Catch::Approx(120.0));
    CHECK(coop_input_window_ms(inf, 40.0) == Catch::Approx(40.0));
    CHECK(coop_input_window_ms(nan, nan) == Catch::Approx(COOP_INPUT_WINDOW_MIN_MS));
}

// ── Tick cost EWMA ───────────────────────────────────────────────────────────

TEST_CASE("coop_tick_cost_tracker seeds on the first sample", "[coop][inputwindow]") {
    coop_tick_cost_tracker t;
    CHECK(t.value() == Catch::Approx(0.0));
    t.sample(100.0);
    CHECK(t.value() == Catch::Approx(100.0));
}

TEST_CASE("coop_tick_cost_tracker smooths subsequent samples", "[coop][inputwindow]") {
    coop_tick_cost_tracker t;
    t.sample(100.0);
    t.sample(0.0);
    // alpha * 0 + ( 1 - alpha ) * 100 == 75 at alpha == 0.25
    CHECK(t.value() == Catch::Approx((1.0 - COOP_INPUT_EWMA_ALPHA) * 100.0));
    CHECK(t.value() == Catch::Approx(75.0));
}

TEST_CASE("coop_tick_cost_tracker rejects bad samples", "[coop][inputwindow]") {
    coop_tick_cost_tracker t;
    t.sample(100.0);
    t.sample(-1.0);
    CHECK(t.value() == Catch::Approx(100.0));
    t.sample(std::numeric_limits<double>::quiet_NaN());
    CHECK(t.value() == Catch::Approx(100.0));
    CHECK(std::isfinite(t.value()));
}

// ── Admission and the depth cap ──────────────────────────────────────────────

TEST_CASE("coop_admit_action enforces the depth cap keeping newest", "[coop][inputwindow]") {
    std::deque<buffered_action> q;
    for (int i = 0; i < 10; ++i) { coop_admit_action(q, act(std::to_string(i).c_str(), i)); }
    REQUIRE(q.size() == COOP_MAX_QUEUED_ACTIONS);
    // Survivors are the last COOP_MAX_QUEUED_ACTIONS admitted — newest intent wins.
    CHECK(q.back().action == "9");
    CHECK(q.front().action == "8");
}

TEST_CASE("coop_admit_action reports the number evicted", "[coop][inputwindow]") {
    std::deque<buffered_action> q;
    // Filling up to the cap evicts nothing.
    for (std::size_t i = 0; i < COOP_MAX_QUEUED_ACTIONS; ++i) {
        CHECK(coop_admit_action(q, act("fill", 0.0)) == 0);
    }
    // Every admission past the cap evicts exactly one.
    CHECK(coop_admit_action(q, act("over", 0.0)) == 1);
    CHECK(q.size() == COOP_MAX_QUEUED_ACTIONS);
}

TEST_CASE("coop_admit_action never evicts a non-evictable entry", "[coop][inputwindow]") {
    std::deque<buffered_action> q;
    coop_admit_action(q, keep("menu", 0.0));
    for (int i = 0; i < 10; ++i) { coop_admit_action(q, act(std::to_string(i).c_str(), i)); }
    REQUIRE(q.size() == COOP_MAX_QUEUED_ACTIONS);
    // The menu key pressed before the burst survives, and so does the newest movement.
    CHECK(q.front().action == "menu");
    CHECK(q.back().action == "9");

    // ...and it still survives the most aggressive expiry pass possible.
    coop_expire_stale_actions(q, 1000.0, 0.0);
    CHECK(q.front().action == "menu");
    CHECK(q.back().action == "9");
}

// ── Staleness expiry ─────────────────────────────────────────────────────────

TEST_CASE("coop_expire_stale_actions drops old input but keeps the newest", "[coop][inputwindow]") {
    std::deque<buffered_action> q{act("a", 0.0), act("b", 0.0), act("c", 100.0)};
    const std::size_t erased = coop_expire_stale_actions(q, 200.0, 50.0);
    CHECK(erased == 2);
    REQUIRE(q.size() == 1);
    // "c" is 100 ms old against a 50 ms window, but it is the most recent intent.
    CHECK(q.front().action == "c");
}

TEST_CASE("coop_expire_stale_actions never empties a single-entry queue", "[coop][inputwindow]") {
    std::deque<buffered_action> q{act("lone", 0.0)};
    // 10 s stale against a 16 ms window: a lone keypress during a very slow tick must
    // still execute.
    CHECK(coop_expire_stale_actions(q, 10000.0, COOP_INPUT_WINDOW_MIN_MS) == 0);
    REQUIRE(q.size() == 1);
    CHECK(q.front().action == "lone");
}

TEST_CASE("coop_expire_stale_actions keeps fresh input", "[coop][inputwindow]") {
    std::deque<buffered_action> q{act("a", 100.0), act("b", 110.0)};
    CHECK(coop_expire_stale_actions(q, 120.0, 50.0) == 0);
    CHECK(q.size() == 2);
}

TEST_CASE("coop_expire_stale_actions is a no-op on an empty queue", "[coop][inputwindow]") {
    std::deque<buffered_action> q;
    CHECK(coop_expire_stale_actions(q, 1000.0, 16.0) == 0);
    CHECK(q.empty());
}
