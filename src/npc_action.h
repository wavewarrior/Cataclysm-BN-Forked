#pragma once

#include "coordinates.h"

#include <string>

class Creature;

/**
 * Describes the single action an NPC intends to take this tick.
 *
 * Produced by the decision logic in npc::move() and consumed by
 * npc::execute_action(), which performs all state mutations.
 *
 * Phase B2: npc_cmd_t carries the resolved target and destination so
 * execute_action() reads them directly instead of re-deriving them from
 * member state (current_target() / bub_pos() / good_escape_direction()).
 */

// ---------------------------------------------------------------------------
// Action kinds
// ---------------------------------------------------------------------------

enum npc_action : int {
    npc_undecided = 0,
    npc_pause,
    npc_reload,
    npc_sleep,
    npc_pickup,
    npc_heal,
    npc_use_painkiller,
    npc_drop_items,
    npc_flee,
    npc_melee,
    npc_shoot,
    npc_look_for_player,
    npc_heal_player,
    npc_follow_player,
    npc_follow_embarked,
    npc_talk_to_player,
    npc_mug_player,
    npc_goto_to_this_pos,
    npc_goto_destination,
    npc_avoid_friendly_fire,
    npc_escape_explosion,
    npc_noop,
    npc_reach_attack,
    npc_aim,
    npc_investigate_sound,
    npc_return_to_guard_pos,
    npc_player_activity,
    num_npc_actions
};

std::string npc_action_name(npc_action action);

// ---------------------------------------------------------------------------
// Command descriptor
// ---------------------------------------------------------------------------

struct npc_cmd_t {
    npc_action kind = npc_undecided;
    /// Resolved attack/heal/follow target. Null when the action does not
    /// involve a creature target (e.g. npc_pause, npc_flee, npc_reload).
    Creature* target = nullptr;
    /// Resolved movement destination. Set to good_escape_direction() for
    /// npc_flee, to target->bub_pos() for melee/ranged, bub_pos() otherwise.
    tripoint_bub_ms dest = {};
};
