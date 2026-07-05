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
 *   2. make_player_move_cmd() (or the full factory, Phase 2+) produces a
 *      player_cmd_t from the action_id and current context.
 *   3. execute_player_cmd() applies the command to the game world.
 *
 * Co-op: coop_client serialises these; coop_server::execute_client_action()
 * mirrors them on the proxy NPC.  Phase 2+ will replace the current string
 * dispatch (queue_action("MOVE_N")) with typed commands.
 *
 * ── Phase roadmap ───────────────────────────────────────────────
 *   B3 Phase 1 (current): scaffold + move (lateral + vertical)
 *   B3 Phase 2+:          pause, open, close, smash, pickup,
 *                         fire, melee, throw, use, eat, reload,
 *                         wait, craft, …
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
    pickup, ///< ACTION_PICKUP / ACTION_PICKUP_ALL / ACTION_PICKUP_FEET (moves consumed; NPC impl
            ///< deferred)
    sleep,  ///< ACTION_SLEEP
    craft,  ///< ACTION_CRAFT / ACTION_LONGCRAFT / ACTION_RECRAFT

    // Phase 5+: smash (needs absolute target pos), fire (needs ctx_json + seq)
};

// ---------------------------------------------------------------------------
// Command descriptor
// ---------------------------------------------------------------------------

struct player_cmd_t {
    player_cmd_kind kind = player_cmd_kind::none;

    /// For kind == move: the authoritative movement delta (post iso_rotate
    /// resolution).  Zero for all other command kinds.
    tripoint_rel_ms delta;
};

// ---------------------------------------------------------------------------
// Factory and helpers
// ---------------------------------------------------------------------------

/// Build a player_cmd_t for a lateral movement action_id.
/// iso_rotate::yes applies the current tile_iso rotation (call from UI context).
/// iso_rotate::no uses raw cardinal/diagonal deltas (direction already resolved).
/// Returns player_cmd_kind::none for non-movement action_ids.
player_cmd_t make_player_move_cmd(action_id act, iso_rotate rot);

/// Map a move cmd's delta to its wire-protocol direction string ("MOVE_N" … "MOVE_NW").
/// Returns an empty string_view for non-move commands or unknown deltas.
/// Pure: no side effects; safe to call from unit tests.
std::string_view move_cmd_to_dir_string(const player_cmd_t& cmd);

/// Reverse of move_cmd_to_dir_string(): parse a wire-protocol direction string
/// ("MOVE_N", "MOVE_NE", …, "UP"/"DOWN" legacy aliases) back to a move player_cmd_t.
/// Returns player_cmd_kind::none for unknown strings.
/// Pure: no side effects; safe to call from unit tests.
player_cmd_t parse_move_cmd(std::string_view key);
