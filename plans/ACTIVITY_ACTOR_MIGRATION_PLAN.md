## STATUS (reviewed 2026-07-01)
**~55% DONE — KEEP (in progress).** Actor count: **43 registered** (up from 21 baseline). Waves 1-5 COMPLETE. Legacy: **~26 unique** `do_turn` ids, **~30 unique** `finish` ids remaining. 3 double-registered (`ACT_READ`, `ACT_TRY_SLEEP`, `ACT_WAIT_STAMINA`).

**Completed:**
- Wave 1 (terrain): 11 actors, legacy handlers deleted
- Wave 2 (butchery): `butchery_activity_actor` handles 8 ids
- Wave 3 (item-action): 7 actors present, `repair_activity_hack` still present (blocked by ACT_TRAIN_SKILL)
- Wave 4 (consume/menu): 5 activities migrated (`consume_menu_activity_actor`, `firstaid_activity_actor`)
- Wave 5 (wood): 3 activities migrated (`wood_chop_activity_actor`)

**Remaining:** ~44 activities across Waves 6a-9

# Rework Plan — Finish the Activity → `activity_actor` Migration

## Context

A system-review of the gameplay/engine core ranked the **half-finished activity-system migration** as the best-ROI rework.

**The problem.** Two activity dispatch systems run side by side:

```cpp
// src/player_activity.cpp
if( actor ) { actor->do_turn( *this, p ); }
else        { type->call_do_turn( this, &p ); }   // legacy function-pointer map
```

**Definition of done.** `do_turn_functions` / `finish_functions` emptied and deleted; `call_do_turn` / `call_finish` removed; the `if(actor) else legacy` branches deleted; `repair_activity_hack` gone (#1612 closed); `activity_type::load` validation updated to require an actor.

## Current state

- **43** registered in `deserialize_functions` (`src/activity_actor.cpp:4857`)
- **~26** unique ids in `do_turn_functions` (`src/activity_handlers.cpp:231-266`)
- **~30** unique ids in `finish_functions` (`src/activity_handlers.cpp:269-303`)
- **3** double-registered (actor + legacy both present): `ACT_READ`, `ACT_TRY_SLEEP`, `ACT_WAIT_STAMINA`
- **1** duplicate key in `do_turn_functions`: `ACT_TIDY_UP` appears twice

## Wave status

| Wave | Status | Details |
|------|--------|---------|
| ✅ 1 — Terrain/timer | DONE | 11 actors present, legacy handlers deleted. |
| ✅ 2 — Butchery | DONE | `butchery_activity_actor` handles 8 ids. |
| 🟡 3 — Item-action | ACTORS DONE, HACK NOT REMOVED | 7 actors present; `repair_activity_hack` still in `activity_handlers.cpp` — blocked by `ACT_TRAIN_SKILL` lacking an actor. |
| ✅ 4 — Consume/menu | DONE | 5 activities migrated: `consume_menu_activity_actor` (4 ids) + `firstaid_activity_actor`. |
| ✅ 5 — Wood | DONE | 3 activities migrated: `wood_chop_activity_actor`. |
| 6a — Simple | NOT STARTED | GAME, GENERIC_GAME, MEDITATE, SHAVE, HAIRCUT, SOCIALIZE, ATM, VIBE, WAIT, WAIT_WEATHER, WAIT_NPC, FIND_MOUNT, SHEAR, PLAY_WITH_PET, TRAIN_PET |
| 6b — Medium | NOT STARTED | READ, TRY_SLEEP, WAIT_STAMINA, TRAVELLING, CRACKING, START_FIRE, FISH, MILK, MAKE_ZLAVE, TREE_COMMUNION, TRAIN, PULP, ADV_INVENTORY |
| 6c — Complex | NOT STARTED | TRAIN_SKILL, OPERATION, ROBOT_CONTROL, MIND_SPLICER, STUDY_SPELL, SPELLCASTING |
| 7 — Vehicle | NOT STARTED | VEHICLE, VEHICLE_REPAIR, VEHICLE_DECONSTRUCTION, START_ENGINES, HOTWIRE_CAR |
| 8 — Multi/zone/NPC | NOT STARTED | MULTIPLE_*, FETCH_REQUIRED, MOVE_LOOT, TIDY_UP |
| 9 — Rip-out | NOT STARTED | Legacy map deletion, dual-path branch removal |

## The migration recipe

For each legacy activity:

1. **Add a subclass** in `src/activity_actor_definitions.h` implementing the pure virtuals
2. **Port the body** from the matching handler into the actor; bag fields become typed members
3. **Port resume semantics** — implement `can_resume_with_internal` if needed
4. **Find & convert ALL state touchpoints** — every `assign_activity(ACT_*)` call site
5. **Register** the actor in `deserialize_functions`
6. **Delete** the legacy entries + free functions — **only after grep-zero gate**
7. **Verify** — build green, tests pass

## Wave 6a — Simple activities

**Activities:** GAME, GENERIC_GAME, MEDITATE, SHAVE, HAIRCUT, SOCIALIZE, ATM, VIBE, WAIT, WAIT_WEATHER, WAIT_NPC, FIND_MOUNT, SHEAR, PLAY_WITH_PET, TRAIN_PET

These are trivial activities with no bag fields or simple finish logic. Can be combined into a few actors:
- `game_activity_actor` — GAME, GENERIC_GAME (same do_turn)
- `morale_activity_actor` — MEDITATE, SHAVE, HAIRCUT (finish gives morale boost)
- `wait_activity_actor` — WAIT, WAIT_WEATHER, WAIT_NPC (no-op do_turn, message finish)
- `pet_activity_actor` — PLAY_WITH_PET, TRAIN_PET, SHEAR (monster reference)
- Individual actors for: SOCIALIZE, ATM, VIBE, FIND_MOUNT

## Wave 6b — Medium activities

**Activities:** READ, TRY_SLEEP, WAIT_STAMINA, TRAVELLING, CRACKING, START_FIRE, FISH, MILK, MAKE_ZLAVE, TREE_COMMUNION, TRAIN, PULP, ADV_INVENTORY

These have some bag fields and moderate logic:
- `read_activity_actor` — READ (targets, values, str_values)
- `try_sleep_activity_actor` — TRY_SLEEP (cancel hook removes sleep_query)
- `wait_stamina_activity_actor` — WAIT_STAMINA (values, cancel hook unwinds auto_resume)
- `cracking_activity_actor` — CRACKING (placement)
- `start_fire_activity_actor` — START_FIRE (tools_, placement, index)
- `fish_activity_actor` — FISH (tools_, placement, multi-fish coupling)
- `milk_activity_actor` — MILK (monsters)
- `make_zlave_activity_actor` — MAKE_ZLAVE (str_values, values)
- `tree_communion_activity_actor` — TREE_COMMUNION (values, overmap BFS)
- `train_activity_actor` — TRAIN (name, values)
- `pulp_activity_actor` — PULP (placement, stamina)
- `adv_inventory_activity_actor` — ADV_INVENTORY (trivial)
- `travelling_activity_actor` — TRAVELLING (special do_turn in player_activity.cpp)

## Wave 6c — Complex activities

**Activities:** TRAIN_SKILL, OPERATION, ROBOT_CONTROL, MIND_SPLICER, STUDY_SPELL, SPELLCASTING

- `train_skill_activity_actor` — TRAIN_SKILL (uses repair_activity_hack, blocks #1612)
- `operation_activity_actor` — OPERATION (many bag fields, NPC coupling)
- `robot_control_activity_actor` — ROBOT_CONTROL (monsters, reprogramming)
- `mind_splicer_activity_actor` — MIND_SPLICER (targets)
- `study_spell_activity_actor` — STUDY_SPELL (many bag fields, magic coupling)
- `spellcasting_activity_actor` — SPELLCASTING (many bag fields, magic coupling)

## Wave 7 — Vehicle activities

**Activities:** VEHICLE, VEHICLE_REPAIR, VEHICLE_DECONSTRUCTION, START_ENGINES, HOTWIRE_CAR

- `vehicle_activity_actor` — VEHICLE (very high complexity, many external readers)
- `vehicle_repair_activity_actor` — VEHICLE_REPAIR (zone coupling)
- `vehicle_deconstruction_activity_actor` — VEHICLE_DECONSTRUCTION (zone coupling)
- `start_engines_activity_actor` — START_ENGINES (placement, values)
- `hotwire_car_activity_actor` — HOTWIRE_CAR (values)

## Wave 8 — Multi/zone/NPC activities

**Activities:** MULTIPLE_FISH, MULTIPLE_CONSTRUCTION, MULTIPLE_MINE, MULTIPLE_BUTCHER, MULTIPLE_FARM, FETCH_REQUIRED, MOVE_LOOT, TIDY_UP, MULTIPLE_CHOP_TREES, MULTIPLE_CHOP_PLANKS

These are zone-manager and NPC-AI coupled multi-activities. Highest regression risk.

## Wave 9 — Rip-out

1. Delete `do_turn_functions` / `finish_functions` and `activity_type::call_do_turn` / `call_finish`
2. Delete the `if(actor) else …` branches in `player_activity.cpp`
3. Update `activity_type::load` validation to require an actor
4. Remove the `repair_activity_hack` namespace and its `#include`
5. Keep the `ACT_MIGRATION_CANCEL` load-time fallback as a safety net

## Critical files

- `src/activity_actor_definitions.h` — new actor declarations
- `src/activity_actor.cpp` — actor bodies + `deserialize_functions` registration
- `src/activity_handlers.cpp` / `.h` — source of legacy bodies to port; shrinks to empty
- `src/player_activity.cpp` / `.h` — dispatch branches removed in wave 9
- `src/activity_type.cpp` — validation update in wave 9
- `src/savegame_json.cpp` — confirm each actor round-trips

## Verification (per wave)

1. **Build green** — compile `cataclysm-bn-tiles`
2. **Grep-zero gate** — `rg "assign_activity\(\s*ACT_FOO|player_activity\(\s*ACT_FOO" src` must return zero
3. **In-game eyeball** each migrated activity
4. **Save/load round-trip mid-activity**
5. **Regression tests** — run `tests/player_activities_test.cpp`

## Resolved decision — Lua compatibility: (A) Best-effort

Lua mods can call `assign_activity(activity_id, int, int, int, string)`. Once an id is migrated, that path produces an actor-less activity. Best-effort / break it — no shim built.

## Rough effort

- Wave 6a: 1-2 days
- Wave 6b: 2-3 days
- Wave 6c: 3-4 days
- Wave 7: 3-4 days
- Wave 8: 1-2 weeks
- Wave 9: 1-2 days
- Total: ~4-6 weeks
