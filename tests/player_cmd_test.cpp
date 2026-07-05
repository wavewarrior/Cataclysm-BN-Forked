/**
 * B3 Phase 1 — player_cmd_t / make_player_move_cmd tests.
 *
 * Anchors the action_id → movement delta mapping so refactors cannot silently
 * break the direction resolution used by both handle_action() and the co-op
 * string-dispatch layer (execute_client_action).
 *
 * These tests do NOT require a full game world — make_player_move_cmd() is
 * pure (no global state, no g->) with iso_rotate::no.  The iso_rotate::yes
 * branch reads tile_iso which is set by render options; we verify the
 * non-iso path here and note the iso path is exercised at runtime.
 */

#include "action.h"
#include "catch/catch.hpp"
#include "coordinates.h"
#include "player_cmd.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

/// Convenience: build a move cmd without iso rotation.
auto move_cmd(action_id act) -> player_cmd_t { return make_player_move_cmd(act, iso_rotate::no); }

} // anonymous namespace

// ---------------------------------------------------------------------------
// 1. Non-movement actions → kind == none
// ---------------------------------------------------------------------------

TEST_CASE("make_player_move_cmd — non-movement action returns none", "[player_cmd][move]") {
    for (const auto act :
         {ACTION_NULL, ACTION_PAUSE, ACTION_INVENTORY, ACTION_FIRE, ACTION_PICKUP, ACTION_SLEEP}) {
        const auto cmd = move_cmd(act);
        CHECK(cmd.kind == player_cmd_kind::none);
        CHECK(cmd.delta == tripoint_rel_ms::zero());
    }
}

// ---------------------------------------------------------------------------
// 2. Cardinal and diagonal directions (non-iso)
// ---------------------------------------------------------------------------

TEST_CASE("make_player_move_cmd — cardinal N", "[player_cmd][move]") {
    const auto cmd = move_cmd(ACTION_MOVE_FORTH);
    CHECK(cmd.kind == player_cmd_kind::move);
    CHECK(cmd.delta == tripoint_rel_ms{0, -1, 0});
}

TEST_CASE("make_player_move_cmd — cardinal S", "[player_cmd][move]") {
    const auto cmd = move_cmd(ACTION_MOVE_BACK);
    CHECK(cmd.kind == player_cmd_kind::move);
    CHECK(cmd.delta == tripoint_rel_ms{0, 1, 0});
}

TEST_CASE("make_player_move_cmd — cardinal E", "[player_cmd][move]") {
    const auto cmd = move_cmd(ACTION_MOVE_RIGHT);
    CHECK(cmd.kind == player_cmd_kind::move);
    CHECK(cmd.delta == tripoint_rel_ms{1, 0, 0});
}

TEST_CASE("make_player_move_cmd — cardinal W", "[player_cmd][move]") {
    const auto cmd = move_cmd(ACTION_MOVE_LEFT);
    CHECK(cmd.kind == player_cmd_kind::move);
    CHECK(cmd.delta == tripoint_rel_ms{-1, 0, 0});
}

TEST_CASE("make_player_move_cmd — diagonal NE", "[player_cmd][move]") {
    const auto cmd = move_cmd(ACTION_MOVE_FORTH_RIGHT);
    CHECK(cmd.kind == player_cmd_kind::move);
    CHECK(cmd.delta == tripoint_rel_ms{1, -1, 0});
}

TEST_CASE("make_player_move_cmd — diagonal NW", "[player_cmd][move]") {
    const auto cmd = move_cmd(ACTION_MOVE_FORTH_LEFT);
    CHECK(cmd.kind == player_cmd_kind::move);
    CHECK(cmd.delta == tripoint_rel_ms{-1, -1, 0});
}

TEST_CASE("make_player_move_cmd — diagonal SE", "[player_cmd][move]") {
    const auto cmd = move_cmd(ACTION_MOVE_BACK_RIGHT);
    CHECK(cmd.kind == player_cmd_kind::move);
    CHECK(cmd.delta == tripoint_rel_ms{1, 1, 0});
}

TEST_CASE("make_player_move_cmd — diagonal SW", "[player_cmd][move]") {
    const auto cmd = move_cmd(ACTION_MOVE_BACK_LEFT);
    CHECK(cmd.kind == player_cmd_kind::move);
    CHECK(cmd.delta == tripoint_rel_ms{-1, 1, 0});
}

// ---------------------------------------------------------------------------
// 3. Struct invariants
// ---------------------------------------------------------------------------

TEST_CASE("player_cmd_t move delta is always z=0 for lateral actions", "[player_cmd][move]") {
    // All 8 lateral movement actions must produce z==0.
    for (const auto act :
         {ACTION_MOVE_FORTH, ACTION_MOVE_FORTH_RIGHT, ACTION_MOVE_RIGHT, ACTION_MOVE_BACK_RIGHT,
          ACTION_MOVE_BACK, ACTION_MOVE_BACK_LEFT, ACTION_MOVE_LEFT, ACTION_MOVE_FORTH_LEFT}) {
        const auto cmd = move_cmd(act);
        REQUIRE(cmd.kind == player_cmd_kind::move);
        CHECK(cmd.delta.z() == 0);
    }
}

TEST_CASE("player_cmd_t none has zero delta", "[player_cmd][move]") {
    const player_cmd_t cmd;
    CHECK(cmd.kind == player_cmd_kind::none);
    CHECK(cmd.delta == tripoint_rel_ms::zero());
}
