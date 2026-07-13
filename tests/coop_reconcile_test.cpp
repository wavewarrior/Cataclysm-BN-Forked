#include "catch/catch_amalgamated.hpp"
#include "coop_reconcile.h"
#include "coordinates.h"
#include "point.h"

#include <vector>

/// Convenience: construct a bubble-coordinate point from raw integers.
static auto bub( int x, int y, int z = 0 ) -> tripoint_bub_ms { return tripoint_bub_ms{x, y, z}; }

/// Build a single-entry pending list.
static auto one( uint32_t seq, const char* key ) -> std::vector<reconcile_action>
{
    return {{seq, key}};
}

// ── Direction convention (concern #1) ────────────────────────────────────────
// Each MOVE_* key is anchored to the canonical direction constant from point.h
// (tripoint_north, tripoint_east, …), NOT to the {0,-1,0} literal inside
// key_to_delta.  A flipped key_to_delta entry fails these checks; tests that
// only verify the internal literal would not catch such a regression.
//
// Pattern: reconcile from bub(0,0) with last_seq=0, one pending action at seq=1.
// Expected result = bub(0,0) + tripoint_<dir>  (operator+ is plain addition —
// confirmed separately in the round-trip test below).

TEST_CASE( "coop_reconcile_pos direction: MOVE_N", "[coop][reconcile]" )
{
    CHECK( coop_reconcile_pos( bub( 0, 0 ), 0, one( 1, "MOVE_N" ) ) == bub( 0, 0 ) + tripoint_north );
}

TEST_CASE( "coop_reconcile_pos direction: MOVE_S", "[coop][reconcile]" )
{
    CHECK( coop_reconcile_pos( bub( 0, 0 ), 0, one( 1, "MOVE_S" ) ) == bub( 0, 0 ) + tripoint_south );
}

TEST_CASE( "coop_reconcile_pos direction: MOVE_E", "[coop][reconcile]" )
{
    CHECK( coop_reconcile_pos( bub( 0, 0 ), 0, one( 1, "MOVE_E" ) ) == bub( 0, 0 ) + tripoint_east );
}

TEST_CASE( "coop_reconcile_pos direction: MOVE_W", "[coop][reconcile]" )
{
    CHECK( coop_reconcile_pos( bub( 0, 0 ), 0, one( 1, "MOVE_W" ) ) == bub( 0, 0 ) + tripoint_west );
}

TEST_CASE( "coop_reconcile_pos direction: MOVE_NE", "[coop][reconcile]" )
{
    CHECK( coop_reconcile_pos( bub( 0, 0 ), 0, one( 1, "MOVE_NE" ) ) == bub( 0,
            0 ) + tripoint_north_east );
}

TEST_CASE( "coop_reconcile_pos direction: MOVE_NW", "[coop][reconcile]" )
{
    CHECK( coop_reconcile_pos( bub( 0, 0 ), 0, one( 1, "MOVE_NW" ) ) == bub( 0,
            0 ) + tripoint_north_west );
}

TEST_CASE( "coop_reconcile_pos direction: MOVE_SE", "[coop][reconcile]" )
{
    CHECK( coop_reconcile_pos( bub( 0, 0 ), 0, one( 1, "MOVE_SE" ) ) == bub( 0,
            0 ) + tripoint_south_east );
}

TEST_CASE( "coop_reconcile_pos direction: MOVE_SW", "[coop][reconcile]" )
{
    CHECK( coop_reconcile_pos( bub( 0, 0 ), 0, one( 1, "MOVE_SW" ) ) == bub( 0,
            0 ) + tripoint_south_west );
}

// ── Operator semantics (concern #2) ──────────────────────────────────────────
// Four cardinal moves from an origin must return to the origin.
// This asserts tripoint_bub_ms + tripoint is plain component addition with no
// clamping, offset, or origin arithmetic.  Any other semantics would leave a
// residual and fail the equality.
TEST_CASE( "coop_reconcile_pos operator+: N+E+S+W round-trip returns to origin", "[coop]["
           "reconcile]" )
{
    const auto origin = bub( 50, 50, 0 );
    const std::vector<reconcile_action> circuit =
    {{1, "MOVE_N"}, {2, "MOVE_E"}, {3, "MOVE_S"}, {4, "MOVE_W"}};
    CHECK( coop_reconcile_pos( origin, 0, circuit ) == origin );
}

// ── Behavioural cases ─────────────────────────────────────────────────────────

// No-op when prediction correct: server still at pre-move position,
// pending = 2×MOVE_N → replay restores predicted position.
TEST_CASE( "coop_reconcile_pos: no-op when prediction is correct", "[coop][reconcile]" )
{
    const auto server = bub( 10, 10 );
    const std::vector<reconcile_action> pending = {{4, "MOVE_N"}, {5, "MOVE_N"}};
    CHECK( coop_reconcile_pos( server, 3, pending ) == bub( 10,
            10 ) + tripoint_north + tripoint_north );
}

// Snap correction: server confirmed both moves but proxy stopped at wall;
// pending empty → result equals server position, correcting client's prediction.
TEST_CASE( "coop_reconcile_pos: snap correction when prediction was wrong", "[coop][reconcile]" )
{
    const auto server = bub( 10, 9 );
    CHECK( coop_reconcile_pos( server, 5, {} ) == server );
}

// Discard boundary: seq == last_seq must be skipped, seq == last_seq+1 replayed.
TEST_CASE( "coop_reconcile_pos: discard boundary exactly at seq == last_seq", "[coop][reconcile]" )
{
    const auto server = bub( 5, 5 );
    const std::vector<reconcile_action> pending = {{3, "MOVE_N"}, {4, "MOVE_E"}};
    // seq=3 confirmed (skipped), seq=4 replayed (MOVE_E = +tripoint_east)
    CHECK( coop_reconcile_pos( server, 3, pending ) == bub( 5, 5 ) + tripoint_east );
}

// Fallback snap-only: last_seq < 0 must ignore the buffer entirely.
TEST_CASE( "coop_reconcile_pos: fallback snap-only for pre-A2 host", "[coop][reconcile]" )
{
    const auto server = bub( 20, 30 );
    const std::vector<reconcile_action> stale =
    {{1, "MOVE_N"}, {2, "MOVE_E"}, {3, "MOVE_S"}, {4, "MOVE_W"}, {5, "MOVE_N"}};
    CHECK( coop_reconcile_pos( server, -1, stale ) == server );
}

// Non-movement keys must be position no-ops in replay.
TEST_CASE( "coop_reconcile_pos: non-movement keys are position no-ops", "[coop][reconcile]" )
{
    const auto server = bub( 3, 7 );
    const std::vector<reconcile_action> pending =
    {{1, "PAUSE"}, {2, "SMASH"}, {3, "FIRE"}, {4, "PICKUP"}};
    CHECK( coop_reconcile_pos( server, 0, pending ) == server );
}

// Gap 7: wall-blocked move with non-empty pending buffer.
// Server says proxy hit a wall (pos unchanged), but client still has MORE
// actions pending after the blocked one.  Snap to server pos, THEN replay
// the remaining pending actions from that corrected position.
TEST_CASE( "coop_reconcile_pos: snap correction with pending actions after the blocked one",
           "[coop][reconcile]" )
{
    const auto server = bub( 10, 10 ); // proxy couldn't move — same pos as before
    // seq=3 confirmed (wall block), seq=4 and seq=5 still pending
    const std::vector<reconcile_action> pending = {{4, "MOVE_N"}, {5, "MOVE_E"}};
    // Snap to server (10,10), replay MOVE_N → (10,9), replay MOVE_E → (11,9)
    CHECK( coop_reconcile_pos( server, 3, pending ) ==
           bub( 10, 10 ) + tripoint_north + tripoint_east );
}

// Gap 7b: multiple pending actions all replay from corrected server position.
TEST_CASE( "coop_reconcile_pos: snap + three-action replay from corrected position",
           "[coop][reconcile]" )
{
    const auto server = bub( 5, 5 );
    const std::vector<reconcile_action> pending = {
        {10, "MOVE_N"}, {11, "MOVE_N"}, {12, "MOVE_N"}
    };
    CHECK( coop_reconcile_pos( server, 9, pending ) ==
           bub( 5, 5 ) + tripoint_north + tripoint_north + tripoint_north );
}

// ── predicted_outcome scaffolding (Phase 2.2 deferred) ──────────────────────
// reconcile_action now carries an optional predicted_outcome.  The position
// reconciler MUST ignore it — only movement keys affect position.

TEST_CASE( "coop_reconcile_pos: predicted_outcome does not affect position", "[coop][reconcile]" )
{
    const auto server = bub( 5, 5 );
    predicted_outcome outcome;
    outcome.target_pos = bub( 10, 10 );
    outcome.target_expected_hp = 50;
    auto smash = reconcile_action{1, "SMASH"};
    smash.outcome = outcome;
    const std::vector<reconcile_action> pending = {smash};
    // SMASH is a non-movement key → position unchanged despite outcome
    CHECK( coop_reconcile_pos( server, 0, pending ) == server );
}

TEST_CASE( "coop_reconcile_pos: mixed movement and outcome actions", "[coop][reconcile]" )
{
    const auto server = bub( 0, 0 );
    predicted_outcome outcome;
    outcome.target_pos = bub( 3, 3 );
    outcome.target_expected_hp = 100;
    auto smash2 = reconcile_action{2, "SMASH"};
    smash2.outcome = outcome;
    const std::vector<reconcile_action> pending = {
        reconcile_action{1, "MOVE_N"},
        smash2,
        reconcile_action{3, "MOVE_E"}
    };
    // Only MOVE_N and MOVE_E affect position; SMASH with outcome is a no-op
    CHECK( coop_reconcile_pos( server, 0, pending ) ==
           bub( 0, 0 ) + tripoint_north + tripoint_east );
}
