## STATUS (reviewed 2026-09-14)
**~44% DONE, self-inconsistent original targets corrected.** `src/savegame_json.cpp` is now 2,771 lines (down from 4,989) via two genuine extraction commits (`16d7968c7b` "extract savegame_character.cpp", `44424f05d5` "extract vehicle + monster serialization", both 2026-07-17) that moved avatar/npc/vehicle/monster out under different filenames than originally proposed. Two of the plan's originally-named targets — overmap and achievement — were misattributed from the start and never belonged in this plan: `overmap::serialize`/`serialize_view` has always lived in `src/savegame.cpp` (present since 2013, predates this plan) and achievement serialization (`achievement_state::serialize`, `achievements_tracker::serialize`) has always lived in `src/achievement.cpp` (present since 2020, predates this plan) — neither was ever resident in `savegame_json.cpp`. See corrected tables below.

# Save/Load Decomposition — Plan

## Context

`savegame_json.cpp` (2771 lines, down from the 4989 this plan was written against) is a monolithic file where every subsystem's serialization lives. As new features are added, the file grows linearly with no decomposition boundary. Every save-related change requires editing this single file, increasing merge conflicts and review overhead.

The serialization pattern itself is reasonable (JSON visitor with `JsonOut`/`JsonIn`), but the co-location of all serialization in one file is an organizational debt.

## Current state (2026-09-14)

| Original proposed target | Actual status |
|---|---|
| `src/avatar/save.cpp` | DONE — landed as `src/savegame_character.cpp` (`avatar::serialize`/`deserialize`, lines 811/908) |
| `src/npcs/save.cpp` | DONE — landed as `src/savegame_character.cpp` (`npc::serialize`, line 1567; same file as avatar, not split further) |
| `src/vehicle/save.cpp` | DONE — landed as `src/savegame_json_vehicle.cpp` (680 lines) |
| *(not originally named)* | DONE — monster serialization landed as `src/savegame_json_monster.cpp` (433 lines), an adjacent win not in the original table |
| `src/overmap/save.cpp` | N/A — `overmap::serialize`/`serialize_view` has always lived in `src/savegame.cpp:958/821` (predates this plan); never resident in `savegame_json.cpp` |
| `src/achievement/save.cpp` | N/A — `achievement_state::serialize`/`achievements_tracker::serialize` has always lived in `src/achievement.cpp` (predates this plan); never resident in `savegame_json.cpp` |
| `src/map/save.cpp` | **NOT DONE — remaining scope.** `submap::store`/`load` (`savegame_json.cpp:2029-2291`, ~260 lines) plus `mm_submap`/`mm_region`/`map_memory::load_legacy` (map-memory) are still resident |
| `src/items/save.cpp` | **NOT DONE — remaining scope.** `item::io`/`serialize`/`deserialize`, `item_contents`, `item::craft_data`, `damage_instance` (de)serialization, `pickup::act_item`, `iuse_location`, and the item `removal_list`/deferred-split helpers are still resident |
| `src/faction/save.cpp` | **NOT DONE — remaining scope.** `faction::serialize`/`deserialize` (`:1343-1394`) is still resident; fold `mission::serialize`/`deserialize` (`:1220-1341`, the plan's separate `src/mission/save.cpp` target) into this file too — it's small enough to share |

**Additional resident subsystems not named in the original table at all** — Phase 1's audit must not stop at the rows above:
- `player_activity`, `progress_counter`, `simple_task`, `requirement_data`, `SkillLevel`, `character_id`, `char_trait_data`, `consumption_history_t`/`consumption_event`, `location_inventory` helpers, `time_point`/`time_duration` — small gameplay primitives, candidates for a shared `savegame_misc.cpp` or their owning subsystem's file
- `Creature::store`/`load`, `player_morale` — character-adjacent; candidates to join `savegame_character.cpp`
- `dimension_info`/`pocket_dimension_data`/`dimension_bounds` — pocket dimension system
- `point`/`tripoint`, `addiction`, `recipe_subset`, `kill_tracker`, `cata_variant`, `event_multiset`, `stats_tracker` — small primitives/trackers
- `advanced_inv_pane_save_state`/`advanced_inv_save_state`, `wisheffect_state`, `debug_menu_state`, `uistatedata` — persisted UI state, arguably not gameplay serialization at all

Rebaseline the extraction target: adopt the project's established flat-file naming convention actually used by the landed split (`savegame_<subsystem>.cpp` at `src/` root, not a nested `<subsystem>/save.cpp` directory — no nested-directory form exists anywhere in the codebase). Rebaseline the "target ≤500 lines" acceptance goal (Phase 3) against this 2,771-line starting point, not the original 4,989.

## Approach

**Mechanical split** — move each subsystem's `serialize()`/`deserialize()` implementation bodies to the subsystem's own directory. The game still calls serialization through the same entry points (`save_game()`, `load_game()`).

No behavioral changes. No API changes. No header changes.

## Phases

### Phase 1 — Audit and plan

Map every function in `savegame_json.cpp` to a target file. Produces a split map for review.

Preliminary split targets:

| New file | Content from savegame_json.cpp | Status |
|----------|-------------------------------|--------|
| `src/savegame_character.cpp` | Avatar + NPC serialization | DONE |
| `src/savegame_json_vehicle.cpp` | Vehicle serialization | DONE |
| `src/savegame_json_monster.cpp` | Monster serialization | DONE |
| `src/savegame_map.cpp` | `submap`/`mm_submap`/`mm_region`/map-memory serialization | remaining |
| `src/savegame_item.cpp` | Item, item-contents, craft-data, damage-instance, pickup/iuse-location serialization | remaining |
| `src/savegame_faction.cpp` | Faction + mission serialization | remaining |
| `src/savegame_misc.cpp` (or fold into owning subsystem) | Remaining small primitives/trackers listed in "Current state" above | remaining |

Overmap and achievement are removed from this table — see "Current state" above for why they were never in scope.

### Phase 2 — Move implementation bodies

For each subsystem:
1. Create the target `.cpp` file with `#include` of the relevant headers
2. Move the `save()`/`load()` overload implementations
3. Verify build green
4. Continue to next subsystem

Each subsystem move is a separate commit for bisect safety.

### Phase 3 — Reduce savegame_json.cpp to orchestration

After all moves: `savegame_json.cpp` retains only:
- `save_game()` / `load_game()` entry points
- Per-system save order and dispatch
- Shared helpers (`open_file`, `write_common_data`, version checks)
- The `ACT_MIGRATION_CANCEL` block (save-compat logic tied to dispatch, not a subsystem)

Target: `savegame_json.cpp` < 500 lines.

### Phase 4 — C++23 modernization (opportunistic)

While touching each subsystem's save file:
- Use `std::expected` for deserialization error reporting (`JsonIn` errors → `std::expected<T, std::string>` instead of `debugmsg`)
- Trailing return types
- `auto` for local variables
- `ranges::*` for container serialization where applicable

Note: `std::expected` changes are higher risk (error propagation changes control flow) — mark as optional per-file.

## Verification (per phase)

- Build green. Save/load round-trip: save in any state → `save_game()` → `load_game()` → compare key state fields, they match.
- `wc -l src/savegame_json.cpp` drops per phase.
- No new includes in headers (all new includes are in .cpp files).

## Files

| File | Phase |
|------|-------|
| `src/savegame_json.cpp` | 1 (source), 3 (shrinks) |
| `src/savegame_character.cpp` (avatar + NPC) | already landed |
| `src/savegame_json_vehicle.cpp` | already landed |
| `src/savegame_json_monster.cpp` | already landed |
| `src/savegame_map.cpp` (new) | 2 |
| `src/savegame_item.cpp` (new) | 2 |
| `src/savegame_faction.cpp` (faction + mission, new) | 2 |
| `src/savegame_misc.cpp` (new, or fold into owning subsystem) | 2 |
| `src/CMakeLists.txt` | 2 |

## Effort: 3-4 days (revised — 4 of the original 9 targets already landed)
- Phase 1: 1 day (audit) — already substantially done by this housekeeping pass's research; formalize into a split map
- Phase 2: 1–2 days (4 remaining subsystems × half-day to a day each)
- Phase 3: 1 day (orchestration cleanup)
- Phase 4: 1–2 days (optional, opportunistic)
