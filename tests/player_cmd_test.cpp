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
#include "catch/catch_amalgamated.hpp"
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

// ---------------------------------------------------------------------------
// Phase 3 — move_cmd_to_dir_string
// ---------------------------------------------------------------------------

TEST_CASE("move_cmd_to_dir_string — all 8 directions", "[player_cmd][dir_string]") {
    using P = point_rel_ms;
    struct Case {
        action_id act;
        std::string_view expected;
    };
    for (const auto& [act, expected] : std::initializer_list<Case>{
             {ACTION_MOVE_FORTH, "MOVE_N"},
             {ACTION_MOVE_FORTH_RIGHT, "MOVE_NE"},
             {ACTION_MOVE_RIGHT, "MOVE_E"},
             {ACTION_MOVE_BACK_RIGHT, "MOVE_SE"},
             {ACTION_MOVE_BACK, "MOVE_S"},
             {ACTION_MOVE_BACK_LEFT, "MOVE_SW"},
             {ACTION_MOVE_LEFT, "MOVE_W"},
             {ACTION_MOVE_FORTH_LEFT, "MOVE_NW"},
         }) {
        const auto cmd = make_player_move_cmd(act, iso_rotate::no);
        REQUIRE(cmd.kind == player_cmd_kind::move);
        CHECK(move_cmd_to_dir_string(cmd) == expected);
    }
}

TEST_CASE("move_cmd_to_dir_string — non-move cmd returns empty", "[player_cmd][dir_string]") {
    const player_cmd_t cmd; // kind == none
    CHECK(move_cmd_to_dir_string(cmd).empty());
}

// ---------------------------------------------------------------------------
// Phase 4 — parse_move_cmd (inverse of move_cmd_to_dir_string)
// ---------------------------------------------------------------------------

TEST_CASE("parse_move_cmd — all 8 directions", "[player_cmd][parse]") {
    using P = tripoint_rel_ms;
    struct Case {
        std::string_view key;
        P expected_delta;
    };
    for (const auto& [key, expected] : std::initializer_list<Case>{
             {"MOVE_N", P{0, -1, 0}},
             {"MOVE_NE", P{1, -1, 0}},
             {"MOVE_E", P{1, 0, 0}},
             {"MOVE_SE", P{1, 1, 0}},
             {"MOVE_S", P{0, 1, 0}},
             {"MOVE_SW", P{-1, 1, 0}},
             {"MOVE_W", P{-1, 0, 0}},
             {"MOVE_NW", P{-1, -1, 0}},
         }) {
        const auto cmd = parse_move_cmd(key);
        REQUIRE(cmd.kind == player_cmd_kind::move);
        CHECK(cmd.delta == expected);
    }
}

TEST_CASE("parse_move_cmd — legacy aliases UP/DOWN/LEFT/RIGHT", "[player_cmd][parse]") {
    CHECK(parse_move_cmd("UP").delta == tripoint_rel_ms{0, -1, 0});
    CHECK(parse_move_cmd("DOWN").delta == tripoint_rel_ms{0, 1, 0});
    CHECK(parse_move_cmd("LEFT").delta == tripoint_rel_ms{-1, 0, 0});
    CHECK(parse_move_cmd("RIGHT").delta == tripoint_rel_ms{1, 0, 0});
}

TEST_CASE("parse_move_cmd — unknown string returns none", "[player_cmd][parse]") {
    CHECK(parse_move_cmd("PAUSE").kind == player_cmd_kind::none);
    CHECK(parse_move_cmd("FIRE").kind == player_cmd_kind::none);
    CHECK(parse_move_cmd("").kind == player_cmd_kind::none);
}

TEST_CASE(
    "round-trip: make_player_move_cmd -> dir_string -> parse_move_cmd",
    "[player_cmd]["
    "parse]") {
    // Every lateral action_id should survive the full round-trip.
    for (const auto act :
         {ACTION_MOVE_FORTH, ACTION_MOVE_FORTH_RIGHT, ACTION_MOVE_RIGHT, ACTION_MOVE_BACK_RIGHT,
          ACTION_MOVE_BACK, ACTION_MOVE_BACK_LEFT, ACTION_MOVE_LEFT, ACTION_MOVE_FORTH_LEFT}) {
        const auto original = make_player_move_cmd(act, iso_rotate::no);
        const auto dir = move_cmd_to_dir_string(original);
        const auto parsed = parse_move_cmd(dir);
        REQUIRE(parsed.kind == player_cmd_kind::move);
        CHECK(parsed.delta == original.delta);
    }
}

// ---------------------------------------------------------------------------
// B3 Phase 5 — make_player_smash_cmd
// ---------------------------------------------------------------------------

TEST_CASE("make_player_smash_cmd: kind == smash", "[player_cmd]") {
    const tripoint_abs_ms target{100, 200, -1};
    const auto cmd = make_player_smash_cmd(target);
    CHECK(cmd.kind == player_cmd_kind::smash);
}

TEST_CASE("make_player_smash_cmd: target_abs preserved", "[player_cmd]") {
    const tripoint_abs_ms target{42, 99, 1};
    const auto cmd = make_player_smash_cmd(target);
    CHECK(cmd.target_abs == target);
}

TEST_CASE("make_player_smash_cmd: delta is zero", "[player_cmd]") {
    // target_abs and delta are separate fields; smash must not corrupt move delta.
    const auto cmd = make_player_smash_cmd(tripoint_abs_ms{1, 2, 0});
    CHECK(cmd.delta == tripoint_rel_ms{0, 0, 0});
}

TEST_CASE("move_cmd_to_dir_string: smash returns empty (not a move)", "[player_cmd]") {
    const auto cmd = make_player_smash_cmd(tripoint_abs_ms{0, 0, 0});
    CHECK(move_cmd_to_dir_string(cmd).empty());
}

// ---------------------------------------------------------------------------
// B3 Phase 6 — make_player_fire_cmd
// ---------------------------------------------------------------------------

TEST_CASE("make_player_fire_cmd: kind == fire", "[player_cmd]") {
    const auto cmd = make_player_fire_cmd(tripoint_abs_ms{10, 20, 0});
    CHECK(cmd.kind == player_cmd_kind::fire);
}

TEST_CASE("make_player_fire_cmd: target_abs preserved", "[player_cmd]") {
    const tripoint_abs_ms target{-5, 300, 2};
    const auto cmd = make_player_fire_cmd(target);
    CHECK(cmd.target_abs == target);
}

TEST_CASE("make_player_fire_cmd: delta is zero", "[player_cmd]") {
    const auto cmd = make_player_fire_cmd(tripoint_abs_ms{1, 2, 0});
    CHECK(cmd.delta == tripoint_rel_ms{0, 0, 0});
}

TEST_CASE("smash and fire commands are distinct kinds", "[player_cmd]") {
    const tripoint_abs_ms t{10, 10, 0};
    CHECK(make_player_smash_cmd(t).kind != make_player_fire_cmd(t).kind);
}

// ---------------------------------------------------------------------------
// B3 Phase 7 — make_player_eat_cmd / make_player_reload_cmd
// ---------------------------------------------------------------------------

TEST_CASE("make_player_eat_cmd: kind == eat", "[player_cmd]") {
    CHECK(make_player_eat_cmd().kind == player_cmd_kind::eat);
}

TEST_CASE("make_player_eat_cmd: delta and target_abs are zero", "[player_cmd]") {
    const auto cmd = make_player_eat_cmd();
    CHECK(cmd.delta == tripoint_rel_ms{0, 0, 0});
    CHECK(cmd.target_abs == tripoint_abs_ms{0, 0, 0});
}

TEST_CASE("make_player_reload_cmd: kind == reload", "[player_cmd]") {
    CHECK(make_player_reload_cmd().kind == player_cmd_kind::reload);
}

TEST_CASE("eat and reload are distinct kinds", "[player_cmd]") {
    CHECK(make_player_eat_cmd().kind != make_player_reload_cmd().kind);
}

// ---------------------------------------------------------------------------
// B3 Phase 8 — make_player_use_cmd
// ---------------------------------------------------------------------------

TEST_CASE("make_player_use_cmd: kind == use", "[player_cmd]") {
    CHECK(make_player_use_cmd().kind == player_cmd_kind::use);
}

TEST_CASE("make_player_use_cmd: delta and target_abs are zero", "[player_cmd]") {
    const auto cmd = make_player_use_cmd();
    CHECK(cmd.delta == tripoint_rel_ms{0, 0, 0});
    CHECK(cmd.target_abs == tripoint_abs_ms{0, 0, 0});
}

TEST_CASE("use, eat, reload are all distinct kinds", "[player_cmd]") {
    CHECK(make_player_use_cmd().kind != make_player_eat_cmd().kind);
    CHECK(make_player_use_cmd().kind != make_player_reload_cmd().kind);
}

// ---------------------------------------------------------------------------
// B3 Phase 9 — make_player_melee_cmd
// ---------------------------------------------------------------------------

TEST_CASE("make_player_melee_cmd: kind == melee", "[player_cmd]") {
    const auto cmd = make_player_melee_cmd(tripoint_abs_ms{5, 10, 0});
    CHECK(cmd.kind == player_cmd_kind::melee);
}

TEST_CASE("make_player_melee_cmd: target_abs preserved", "[player_cmd]") {
    const tripoint_abs_ms target{-3, 7, 1};
    CHECK(make_player_melee_cmd(target).target_abs == target);
}

TEST_CASE("make_player_melee_cmd: delta is zero", "[player_cmd]") {
    CHECK(make_player_melee_cmd(tripoint_abs_ms{1, 2, 0}).delta == tripoint_rel_ms{0, 0, 0});
}

TEST_CASE("melee is distinct from smash and fire", "[player_cmd]") {
    const tripoint_abs_ms t{0, 0, 0};
    CHECK(make_player_melee_cmd(t).kind != make_player_smash_cmd(t).kind);
    CHECK(make_player_melee_cmd(t).kind != make_player_fire_cmd(t).kind);
}
