#pragma once

#include "action.h"
#include "coordinates.h"

/**
 * Typed simulation commands produced by player input.
 *
 * Only move-consuming, world-mutating actions become commands.
 * UI-only actions (menus, option toggles, display helpers) are NOT commands
 * and stay as raw action_id dispatch inside handle_action().
 *
 * Usage flow (B3):
 *   1. Player presses a key → action_id is resolved by look_up_action().
 *   2. Factory function produces a player_cmd_t from action_id + context.
 *   3. execute_player_cmd() applies the command to the game world.
 *
 * Co-op: coop_server::execute_client_action() parses the wire packet into a
 * typed player_cmd_t, then delegates to execute_player_cmd() on the proxy.
 * Wire format (string key + ctx_json) is unchanged; typed commands give
 * testability and a clean path to a future typed wire format.
 *
 * ── Phase roadmap ───────────────────────────────────────────────
  *   Phase 1: scaffold + move (lateral)
  *   Phase 4: pause, pickup, sleep, craft (no-payload)
  *   Phase 5: smash (abs target pos)
  *   Phase 6: fire (target + seq)
  *   Phase 7: eat, reload (no-payload)
  *   Phase 8 (current): use/activate item (no-payload)
  *   Phase 9+: throw (needs field/explosion propagation), melee (needs target ID), …
 */
// ---------------------------------------------------------------------------
// Command kind
// ---------------------------------------------------------------------------

enum class player_cmd_kind : uint8_t {
    none = 0,

    /// Lateral player movement (N/S/E/W/NE/NW/SE/SW).
    /// delta carries the direction resolved from action_id + iso_rotate.
    move,

    // ── Phase 4: simple no-payload simulation commands ───────────────────
    pause,  ///< ACTION_PAUSE / ACTION_TIMEOUT / ACTION_WAIT
    pickup, ///< ACTION_PICKUP / ACTION_PICKUP_ALL / ACTION_PICKUP_FEET (moves consumed; NPC impl deferred)
    sleep,  ///< ACTION_SLEEP
    craft,  ///< ACTION_CRAFT / ACTION_LONGCRAFT / ACTION_RECRAFT

    // ── Phase 5: target-position commands ────────────────────────────────
    smash,  ///< bash terrain/creature at target_abs; ACTION_SMASH
    fire,   ///< fire weapon at target_abs using lag-comp seq; ACTION_FIRE / ACTION_FIRE_BURST
    // ── Phase 7: no-payload relay ─────────────────────────────────────────
    eat,    ///< eat/drink; ACTION_EAT — no payload
    reload, ///< reload weapon; ACTION_RELOAD_* — no payload

    // ── Phase 8: use/activate item ────────────────────────────────────────
    use,    ///< use/activate item; ACTION_USE — no payload

    // Phase 9+: melee reach attack (needs instrumenting autoattack — deferred)
};

// ---------------------------------------------------------------------------
// Command descriptor
// ---------------------------------------------------------------------------

struct player_cmd_t {
    player_cmd_kind kind = player_cmd_kind::none;

    /// For kind == move: the authoritative movement delta (post iso_rotate).
    /// Zero for all other command kinds.
    tripoint_rel_ms delta;

    /// For kind == smash: absolute world position of the bash target.
    /// Zero for all other command kinds.
    tripoint_abs_ms target_abs;
};

// ---------------------------------------------------------------------------
// Factory and helpers
// ---------------------------------------------------------------------------

/// Build a player_cmd_t for a lateral movement action_id.
player_cmd_t make_player_move_cmd(action_id act, iso_rotate rot);

/// Build a player_cmd_t for a smash action at abs_target.
/// Pure: no side effects; safe to call from UI and tests.
player_cmd_t make_player_smash_cmd(tripoint_abs_ms abs_target);

/// Build a player_cmd_t for a fire action at abs_target.
/// seq is passed separately to execute_player_cmd() for lag compensation.
/// Pure: no side effects; safe to call from UI and tests.
player_cmd_t make_player_fire_cmd(tripoint_abs_ms abs_target);

/// Build a player_cmd_t for eat/drink — no payload.
/// Pure: no side effects; safe to call from UI and tests.
player_cmd_t make_player_eat_cmd();

/// Build a player_cmd_t for a use/activate action — no payload.
/// Pure: no side effects; safe to call from UI and tests.
player_cmd_t make_player_use_cmd();

/// Build a player_cmd_t for a reload action — no payload.
/// Pure: no side effects; safe to call from UI and tests.
player_cmd_t make_player_reload_cmd();

/// Map a move cmd's delta to its wire-protocol direction string ("MOVE_N" … "MOVE_NW").
/// Returns an empty string_view for non-move commands or unknown deltas.
/// Pure: no side effects; safe to call from unit tests.
std::string_view move_cmd_to_dir_string(const player_cmd_t& cmd);

/// Reverse of move_cmd_to_dir_string(): parse a wire-protocol direction string
/// ("MOVE_N", "MOVE_NE", …, "UP"/"DOWN" legacy aliases) back to a move player_cmd_t.
/// Returns player_cmd_kind::none for unknown strings.
/// Pure: no side effects; safe to call from unit tests.
player_cmd_t parse_move_cmd(std::string_view key);
