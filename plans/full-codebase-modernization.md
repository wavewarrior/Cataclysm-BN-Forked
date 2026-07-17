# Full Codebase Modernization Plan

## Status: IN PROGRESS (2026-07-17)

Session 1 complete: Tier 1 decomposition done (3 files, 10,234 lines extracted, 8 commits).
Tier 2 remaining (5 files, ~39k lines).

## Phase 1: Mechanical Modernization — DROPPED

Analysis showed most targets are in vendored code (Lua/sol2) or string literals.
Not worth the risk/effort ratio. Skipped.

## Phase 2: God-File Decomposition — IN PROGRESS

Follows proven `cpp-godfile-decompose` skill (item.cpp 11,688→920, 16 commits).
Each file: map methods → extract domain TUs → build → commit → repeat.

### Tier 1 — COMPLETE (2026-07-17)

| File | Before | After | Reduction | New TUs created |
|------|--------|-------|-----------|-----------------|
| `character.cpp` | 11,713 | 7,810 | −3,903 (33%) | `character_needs.cpp` (1,769), `character_combat.cpp` (905), `character_encumbrance.cpp` (453), `character_inventory.cpp` (1,323) |
| `map.cpp` | 9,817 | 5,843 | −3,974 (40%) | `map_vehicle.cpp` (1,603), `map_bash.cpp` (1,769), `map_items.cpp` (1,164) |
| `iuse.cpp` | 8,919 | 6,562 | −2,357 (26%) | `iuse_electronics.cpp` (2,823) |

Commits (8 atomic, each build-green):
- `a7e3ea2` refactor(character): extract character_needs.cpp
- `fde59f6` refactor(character): extract character_combat.cpp
- `434d49b` refactor(character): extract character_encumbrance.cpp
- `c5228d1` refactor(character): extract character_inventory.cpp
- `77442cc` refactor(map): extract map_vehicle.cpp
- `e50b1b9` refactor(map): extract map_bash.cpp
- `795a698` refactor(map): extract map_items.cpp
- `8079c05` refactor(iuse): extract iuse_electronics.cpp

Verified: `[character]` (4/4), `[item]` (47/47), `[encumbrance]` (7/7) all pass.
`[melee]` has 5 pre-existing stochastic failures (Z-score assertions, not caused by decomposition).

### Tier 2 — NOT STARTED

| File | Lines | Domain split targets |
|------|-------|---------------------|
| `vehicle.cpp` | 8,430 | parts/construction, movement/steering, fuel/engine, turrets, damage/collision, electrical |
| `iexamine.cpp` | 8,186 | furniture/appliances, terrain/nature, containers, electronics, farming, crafting-stations |
| `iuse_actor.cpp` | 7,879 | actors by category (repair, transform, deploy, inscribe, consume, medical, holster, gunmod) |
| `activity_actor.cpp` | 7,426 | by activity type (craft, move, dig, fish, forage, lockpick, mining, medical, aim/fire) |
| `game.cpp` | 7,196 | input/handling, movement/player, combat, npc-interaction, environmental, save/load, debug |

### Tier 3 — DEFERRED (existing plans, additional gates)

| File | Lines | Plan |
|------|-------|------|
| `overmap.cpp` | 6,700 | `plans/overmap_modernization.md` — gated on Phase 0 determinism harness |
| `mapgen.cpp` | 6,555 | RNG-ordered, needs determinism guard like overmap |
| `savegame_json.cpp` | ~4,989 | `plans/savegame_decomposition.md` — pure mechanical split by subsystem |

## Phase 3: Performance Optimization — DEFERRED

Profile-guided. Cannot proceed until structural changes land.

1. Profile hot paths (runtime flamegraph during normal gameplay)
2. Identify cache-unfriendly access patterns in map/creature iteration
3. Optimize serialization (savegame_json is a known bottleneck)
4. Evaluate SIMD opportunities in lighting/physics code

## Procedure (proven, reusable)

1. Map all `ClassName::` method definitions with regex
2. Group methods into domain clusters by name pattern + file position
3. Identify largest contiguous block for extraction
4. Copy the source file's include block verbatim (strip non-static function definitions to avoid duplicate symbols)
5. Scan extracted block for needed preamble statics (file-scope `static const` IDs)
6. Check for TU-local hazards: anonymous namespace helpers, file-scope statics, pragma pairs
7. Write new `src/original_domain.cpp`, remove lines from original
8. `cmake --preset osx-arm-slim` (reconfigure to pick up new file via GLOB)
9. Build both targets with long timeout (1200s+)
10. Fix: missing includes, duplicate symbols, helpers that need duplication or relocation
11. Commit atomically: `refactor(module): extract module_domain.cpp`

Key lessons from session 1:
- Copy the full include block, then strip only function definitions — never guess includes
- Static helpers called from both TUs → duplicate in anonymous namespace (per skill Risk #2)
- Multi-line `static const` initializers with `{` are NOT function bodies — don't strip them
- Always check extraction boundaries for methods at the edge of the slice range
