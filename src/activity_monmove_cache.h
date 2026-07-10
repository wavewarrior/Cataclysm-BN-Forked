#pragma once

#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

#include "memory_fast.h"
#include "monster.h"

class npc;

/// Cached monmove inputs shared across one activity fixed-window batch.
/// Normal monmove rebuilds them every turn; activity skips reuse them.
struct activity_monmove_cache {
    bool valid = false;
    int monster_count = 0;
    int tier0_count = 0;
    int effective_budget = 0;
    std::vector<shared_ptr_fast<monster>> monster_refs;
    std::vector<monster *> mon_snap;
    std::vector<shared_ptr_fast<npc>> npc_refs;
    std::vector<npc *> npc_snap;
    std::vector<std::pair<monster *, int8_t>> real_lod;
    std::unordered_set<monster *> ai_paused;
    std::vector<monster *> plannable_candidates;
    std::vector<std::pair<int, monster *>> eligible_order;
    monster::faction_snap_t faction_snap;
    monster::hostile_fac_map_t hostile_fac_map;
};
