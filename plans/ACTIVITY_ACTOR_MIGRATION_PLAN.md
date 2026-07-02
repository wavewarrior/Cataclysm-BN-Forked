# Activity → `activity_actor` Migration Plan

## STATUS (reviewed 2026-07-01)
**~65% DONE.** Actors: **58** registered (up from 21 baseline). Legacy: **~23** `do_turn`, **~27** `finish` ids remaining. 3 double-registered (`ACT_READ`, `ACT_TRY_SLEEP`, `ACT_WAIT_STAMINA`).

### Completed (Waves 1-6a: 45 activities migrated)
| Wave | Actors | Activities | Notes |
|------|--------|------------|-------|
| ✅ 1 | 11 | BURROW, PICKAXE, JACKHAMMER, CHURN, FILL_PIT, CLEAR_RUBBLE, HAND_CRANK, PRY_NAILS, FILL_LIQUID, PLANT_SEED, FERTILIZE_PLOT, FORAGE | Legacy deleted |
| ✅ 2 | 1 | BUTCHER, BUTCHER_FULL, FIELD_DRESS, SKIN, QUARTER, DISMEMBER, DISSECT, BLEED | `butchery_activity_actor` |
| 🟡 3 | 7 | REPAIR_ITEM, MEND_ITEM, TOOLMOD_ADD, GUNMOD_ADD, RELOAD, WEAR, ARMOR_LAYERS | `repair_activity_hack` still present — blocked by `ACT_TRAIN_SKILL` (#1612) |
| ✅ 4 | 2 | EAT_MENU, CONSUME_FOOD_MENU, CONSUME_DRINK_MENU, CONSUME_MEDS_MENU, FIRSTAID | `consume_menu_activity_actor` + `firstaid_activity_actor` |
| ✅ 5 | 1 | CHOP_TREE, CHOP_LOGS, CHOP_PLANKS | `wood_chop_activity_actor` |
| ✅ 6a | 11 | GAME, GENERIC_GAME, MEDITATE, SHAVE, HAIRCUT, SOCIALIZE, ATM, VIBE, WAIT, WAIT_WEATHER, WAIT_NPC, FIND_MOUNT, SHEAR, PLAY_WITH_PET, TRAIN_PET | 11 actors (game, morale, wait, social, atm, vibe, shear, play_with_pet, train_pet, find_mount) |

### Remaining (~31 activities)
| Wave | Status | Activities |
|------|--------|------------|
| 6b — Medium | NOT STARTED | READ, TRY_SLEEP, WAIT_STAMINA, TRAVELLING, CRACKING, START_FIRE, FISH, MILK, MAKE_ZLAVE, TREE_COMMUNION, TRAIN, PULP, ADV_INVENTORY |
| 6c — Complex | NOT STARTED | TRAIN_SKILL, OPERATION, ROBOT_CONTROL, MIND_SPLICER, STUDY_SPELL, SPELLCASTING |
| 7 — Vehicle | NOT STARTED | VEHICLE, VEHICLE_REPAIR, VEHICLE_DECONSTRUCTION, START_ENGINES, HOTWIRE_CAR |
| 8 — Multi/zone/NPC | NOT STARTED | MULTIPLE_*, FETCH_REQUIRED, MOVE_LOOT, TIDY_UP |
| 9 — Rip-out | NOT STARTED | Legacy map deletion, dual-path branch removal |

## Context

**Problem.** Two activity dispatch systems run side by side:
```cpp
// src/player_activity.cpp
if( actor ) { actor->do_turn( *this, p ); }
else        { type->call_do_turn( this, &p ); }   // legacy function-pointer map
```

**Definition of done.** `do_turn_functions` / `finish_functions` emptied and deleted; `call_do_turn` / `call_finish` removed; the `if(actor) else legacy` branches deleted; `repair_activity_hack` gone (#1612 closed); `activity_type::load` validation updated to require an actor.

## Migration recipe

1. **Add subclass** in `src/activity_actor_definitions.h` implementing pure virtuals
2. **Port body** from legacy handler; bag fields → typed members
3. **Port resume semantics** — `can_resume_with_internal` if needed
4. **Find & convert ALL call sites** — every `assign_activity(ACT_*)` construction
5. **Register** in `deserialize_functions`
6. **Delete** legacy entries + free functions — **only after grep-zero gate**
7. **Verify** — build green, grep-zero, tests pass

## Wave 6b — Medium activities

**Activities:** READ, TRY_SLEEP, WAIT_STAMINA, TRAVELLING, CRACKING, START_FIRE, FISH, MILK, MAKE_ZLAVE, TREE_COMMUNION, TRAIN, PULP, ADV_INVENTORY

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

## Wave 8 — Multi/zone/NPC activities

**Activities:** MULTIPLE_FISH, MULTIPLE_CONSTRUCTION, MULTIPLE_MINE, MULTIPLE_BUTCHER, MULTIPLE_FARM, FETCH_REQUIRED, MOVE_LOOT, TIDY_UP, MULTIPLE_CHOP_TREES, MULTIPLE_CHOP_PLANKS

## Wave 9 — Rip-out

1. Delete `do_turn_functions` / `finish_functions` and `activity_type::call_do_turn` / `call_finish`
2. Delete `if(actor) else …` branches in `player_activity.cpp`
3. Update `activity_type::load` validation to require an actor
4. Remove `repair_activity_hack` namespace
5. Keep `ACT_MIGRATION_CANCEL` load-time fallback as safety net

## Critical files

- `src/activity_actor_definitions.h` — new actor declarations
- `src/activity_actor.cpp` — actor bodies + `deserialize_functions` registration
- `src/activity_handlers.cpp` / `.h` — legacy bodies to port; shrinks to empty
- `src/player_activity.cpp` / `.h` — dispatch branches removed in wave 9
- `src/activity_type.cpp` — validation update in wave 9

## Verification (per wave)

1. **Build green** — compile `cataclysm-bn-tiles`
2. **Grep-zero gate** — `rg "assign_activity\(\s*ACT_FOO|player_activity\(\s*ACT_FOO" src` → zero
3. **Save/load round-trip mid-activity**
4. **Regression tests** — `tests/player_activities_test.cpp`

## Build notes (Windows MSVC)

- **spirv-cross runtime mismatch**: `CMAKE_PROJECT_SPIRV-CROSS_INCLUDE` → `CMakeModules/FixSpirvCrossRuntime.cmake`
- **/bigobj**: Added to `CATALUA_BINDINGS_COMPILE_OPTIONS` for MSVC
- **mods gitlink**: `rm mods && mkdir mods` before CMake configure
- **vcpkg**: `x64-windows-static-md` triplet; `libxmp-lite` stub at `C:/vcpkg/installed/x64-windows-static-md/share/libxmp-lite/`

## Commits
```
7493053ae8 chore: update activity migration plan — Wave 6a complete
16247448d1 refactor(activity): migrate simple activities to actor system (Wave 6a)
4a9743eedc build: fix Windows MSVC build for spirv-cross runtime and /bigobj
01749bc333 refactor(activity): migrate consume-menu and wood activities to actor system
```
