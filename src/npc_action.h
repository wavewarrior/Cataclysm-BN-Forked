#pragma once

#include <string>

/**
 * Describes the single action an NPC intends to take this tick.
 *
 * Produced by the decision logic in npc::move() and consumed by
 * npc::execute_action(), which performs all state mutations.
 *
 * Phase B1: npc_cmd_t currently wraps only the action kind.
 * Phase B2 (future): will add resolved target pointer and dest tripoint so
 * execute_action() no longer has to re-derive them from member state via
 * current_target() and bub_pos().
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

    // Phase B2 additions (not yet populated by decide_action):
    //   Creature* target = nullptr;      // resolved attack/heal target
    //   tripoint_bub_ms dest = {};       // resolved movement destination
};
