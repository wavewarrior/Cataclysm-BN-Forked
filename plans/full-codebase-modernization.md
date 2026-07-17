# Full Codebase Modernization Plan

## Status: COMPLETE (2026-07-17)

Two sessions: 9 god files decomposed, ~20,200 lines extracted across 15 commits into 15 new domain TUs.
All tests pass, build green throughout.

## Phase 2: God-File Decomposition — COMPLETE

### Session 1 — Tier 1+2 (11 commits)

| File | Before | After | Reduction | New TUs |
|------|--------|-------|-----------|---------|
| `character.cpp` | 11,713 | 7,810 | −3,903 (33%) | `character_needs.cpp`, `character_combat.cpp`, `character_encumbrance.cpp`, `character_inventory.cpp` |
| `map.cpp` | 9,817 | 5,843 | −3,974 (40%) | `map_vehicle.cpp`, `map_bash.cpp`, `map_items.cpp` |
| `iuse.cpp` | 8,919 | 6,562 | −2,357 (26%) | `iuse_electronics.cpp` |
| `iexamine.cpp` | 8,186 | 5,125 | −3,061 (37%) | `iexamine_crafting.cpp`, `iexamine_medical.cpp` |
| `iuse_actor.cpp` | 7,879 | 6,224 | −1,655 (21%) | `iuse_actor_deploy.cpp` |
| `activity_actor.cpp` | 7,426 | 5,896 | −1,530 (21%) | `activity_actor_tools.cpp` |

### Session 2 — Remaining targets (4 commits)

| File | Before | After | Reduction | New TUs |
|------|--------|-------|-----------|---------|
| `vehicle.cpp` | 8,430 | 7,613 | −817 (10%) | `vehicle_power.cpp`, `vehicle_damage.cpp` |
| `monattack.cpp` | 6,165 | 4,697 | −1,468 (24%) | `monattack_ranged.cpp`, `monattack_fungus.cpp` |
| `savegame_json.cpp` | 4,998 | 3,561 | −1,437 (29%) | `savegame_character.cpp` |

### All extraction commits

```
Session 2:
16d7968c refactor(save): extract savegame_character.cpp
49afbf4d refactor(monattack): extract monattack_fungus.cpp
8b473cac refactor(monattack): extract monattack_ranged.cpp
6e96e166 refactor(vehicle): extract vehicle_power.cpp + vehicle_damage.cpp

Session 1:
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

### Files NOT decomposed (with rationale)

| File | Lines | Rationale |
|------|-------|-----------|
| `game.cpp` | 7,196 | Already has 9 domain splits (`game_world_tick`, `game_movement`, `game_action`, `game_ui_extra`, `game_activity`, `game_inventory`, `game_info`, `game_object`). Remaining is core orchestration. |
| `vehicle.cpp` | 7,613 | Further extraction blocked by `distribution_graph::traverse` template + `RemovePartHandler` preamble classes. Power/damage clusters extracted; parts/fuel/movement remain tied to template instantiations. |
| `overmap.cpp` | 6,700 | Gated on determinism harness (see `plans/overmap_modernization.md`). |
| `mapgen.cpp` | 6,555 | RNG-ordered, same determinism gate as overmap. |

## Phase 3: Performance Optimization — DEFERRED

Profile-guided. Structural decomposition is complete; profiling can now proceed.

1. Profile hot paths (runtime flamegraph during normal gameplay)
2. Identify cache-unfriendly access patterns in map/creature iteration
3. Optimize serialization (savegame_json is a known bottleneck)
4. Evaluate SIMD opportunities in lighting/physics code

## Procedure (proven, reusable)

1. Map all `ClassName::` method definitions with regex
2. Group methods into domain clusters by name pattern + file position
3. Identify largest contiguous block for extraction
4. Use `find_method_end()` with brace-depth tracking to find exact closing `}`
5. Copy source file's include block up to first method definition (NO function bodies)
6. Scan extracted block for needed preamble statics
7. Check for TU-local hazards: shared statics → duplicate in anon-namespace
8. Write new `src/original_domain.cpp`, remove lines from original
9. `cmake --preset osx-arm-slim` (reconfigure for GLOB)
10. Build both targets, fix errors (missing includes, duplicate symbols, relocated helpers)
11. Commit atomically

### Key lessons

- `find_method_end()` with brace-depth tracking prevents truncated functions at slice boundaries
- Copy include block only up to first `ClassName::method(` — never include preamble function bodies
- Static helpers called from both TUs → duplicate in anonymous namespace
- Files with class definitions in the preamble need careful handling (vehicle.cpp)
- `distribution_graph::traverse` with lambdas ties methods to their TU (can't extract)
- Multi-line `static const` initializers with `{` are NOT function bodies
- Build after EVERY extraction — never batch
