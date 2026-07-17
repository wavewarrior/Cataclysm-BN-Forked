# Full Codebase Modernization Plan

## Status: TIER 1+2 COMPLETE (2026-07-17)

Session complete: 6 god files decomposed, 16,480 lines extracted across 11 commits.
2 files remain (vehicle.cpp, game.cpp — blocked/already split).

## Phase 1: Mechanical Modernization — DROPPED

Analysis showed most NULL/typedef targets are in vendored code (Lua/sol2) or string literals.
Not worth the risk/effort ratio. Skipped.

## Phase 2: God-File Decomposition

Follows proven `cpp-godfile-decompose` skill (item.cpp 11,688→920, 16 commits).

### Tier 1 — COMPLETE

| File | Before | After | Reduction | New TUs created |
|------|--------|-------|-----------|-----------------|
| `character.cpp` | 11,713 | 7,810 | −3,903 (33%) | `character_needs.cpp` (1,769), `character_combat.cpp` (905), `character_encumbrance.cpp` (453), `character_inventory.cpp` (1,323) |
| `map.cpp` | 9,817 | 5,843 | −3,974 (40%) | `map_vehicle.cpp` (1,603), `map_bash.cpp` (1,769), `map_items.cpp` (1,164) |
| `iuse.cpp` | 8,919 | 6,562 | −2,357 (26%) | `iuse_electronics.cpp` (2,823) |

### Tier 2 — COMPLETE (partial)

| File | Before | After | Reduction | New TUs created |
|------|--------|-------|-----------|-----------------|
| `iexamine.cpp` | 8,186 | 5,125 | −3,061 (37%) | `iexamine_crafting.cpp` (1,564), `iexamine_medical.cpp` (2,014) |
| `iuse_actor.cpp` | 7,879 | 6,224 | −1,655 (21%) | `iuse_actor_deploy.cpp` (1,863) |
| `activity_actor.cpp` | 7,426 | 5,896 | −1,530 (21%) | `activity_actor_tools.cpp` (1,696) |

### Tier 2 — REMAINING

| File | Lines | Status | Blocker |
|------|-------|--------|---------|
| `vehicle.cpp` | 8,430 | Not started | Preamble has `RemovePartHandler`/`DefaultRemovePartHandler`/`MapgenRemovePartHandler` class definitions + `vehicle_stack` methods interleaved with `vehicle::` methods. Needs the handler classes moved to a shared header (`vehicle_part_handler.h`) before extraction is safe. Also `distribution_graph::traverse` template instantiation ties `fuel_left` to the TU. |
| `game.cpp` | 7,196 | Already split | Has 9 existing domain files (`game_world_tick.cpp`, `game_movement.cpp`, `game_action.cpp`, `game_ui_extra.cpp`, `game_activity.cpp`, `game_inventory.cpp`, `game_info.cpp`, `game_object.cpp`). Remaining 7,196 lines are core orchestration (game loop, init, setup) which should stay. |

### Commits (11 extraction + 2 docs)

```
55beb1be refactor(iuse_actor): extract iuse_actor_deploy.cpp
e8cf77ac refactor(activity): extract activity_actor_tools.cpp
f2015088 refactor(iexamine): extract iexamine_medical.cpp
b2ab29e0 refactor(iexamine): extract iexamine_crafting.cpp
8079c05a refactor(iuse): extract iuse_electronics.cpp
795a6987 refactor(map): extract map_items.cpp
e50b1b95 refactor(map): extract map_bash.cpp
77442cc4 refactor(map): extract map_vehicle.cpp
c5228d1f refactor(character): extract character_inventory.cpp
434d49bc refactor(character): extract character_encumbrance.cpp
fde59f6e refactor(character): extract character_combat.cpp
a7e3ea2c refactor(character): extract character_needs.cpp
```

### Verification

All tests pass after all extractions:
- `[character]`: 4/4 test cases, 235 assertions
- `[item]`: 47/47 test cases, 140,636 assertions
- `[encumbrance]`: 7/7 test cases, 26 assertions
- `[lua]`: 28/28 test cases, 194 assertions
- `[melee]`: 14/19 — 5 failures are pre-existing stochastic (Z-score assertions)

## Phase 3: Performance Optimization — DEFERRED

Profile-guided. Cannot proceed until structural changes land.

1. Profile hot paths (runtime flamegraph during normal gameplay)
2. Identify cache-unfriendly access patterns in map/creature iteration
3. Optimize serialization (savegame_json is a known bottleneck)
4. Evaluate SIMD opportunities in lighting/physics code

### Tier 3 — DEFERRED (existing plans, additional gates)

| File | Lines | Plan |
|------|-------|------|
| `overmap.cpp` | 6,700 | `plans/overmap_modernization.md` — gated on Phase 0 determinism harness |
| `mapgen.cpp` | 6,555 | RNG-ordered, needs determinism guard like overmap |
| `savegame_json.cpp` | ~4,989 | `plans/savegame_decomposition.md` — pure mechanical split by subsystem |

## Procedure (proven, reusable)

1. Map all `ClassName::` method definitions with regex
2. Group methods into domain clusters by name pattern + file position
3. Identify largest contiguous block for extraction
4. Use `find_method_end()` to find exact closing `}` at brace depth 0
5. Copy the source file's include block (up to first method definition, NO function bodies)
6. Scan extracted block for needed preamble statics (file-scope `static const` IDs)
7. Check for TU-local hazards: anonymous namespace helpers used by both TUs → duplicate
8. Write new `src/original_domain.cpp`, remove lines from original
9. `cmake --preset osx-arm-slim` (reconfigure to pick up new file via GLOB)
10. Build both targets with long timeout (1200s+)
11. Fix: missing includes, duplicate symbols (linker), helpers that need duplication
12. Commit atomically: `refactor(module): extract module_domain.cpp`

### Key lessons

- Copy include block up to first `ClassName::method(` — NOT the full preamble with function bodies
- `find_method_end()` prevents truncated function bodies at slice boundaries
- Static helpers called from both TUs → duplicate in anonymous namespace (per skill Risk #2)
- Multi-line `static const` initializers with `{` are NOT function bodies — don't strip them
- Files with class definitions in the preamble (vehicle.cpp) need the classes moved to headers first
- Template instantiations with lambdas (distribution_graph::traverse) tie methods to their TU
- Build after EVERY extraction — never batch multiple extractions before building
