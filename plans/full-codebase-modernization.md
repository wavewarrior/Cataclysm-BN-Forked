# Full Codebase Modernization Plan

## Status: IN PROGRESS (2026-07-17)

## Scope

Comprehensive modernization of the Cataclysm-BN-Forked codebase:
1. Mechanical C++ modernization (NULL, typedef, printf)
2. God-file decomposition (10 files, 83k lines total)
3. Performance optimization (profile-guided)

## Phase 1: Mechanical Modernization

Safe, parallelizable AST-driven rewrites. Single commit per category.

| Target | Count | Replacement | Risk |
|--------|-------|-------------|------|
| `NULL` | 121 | `nullptr` | Minimal — semantic equivalent in C++ |
| `typedef` | 23 | `using` alias | Minimal — equivalent |
| raw printf-family | 9 | `DebugLog` or `string_format` | Low — case by case |

Excludes vendored code (src/lua/, src/sol/, src/fmtlib_*).

## Phase 2: God-File Decomposition

Follows proven `cpp-godfile-decompose` skill (item.cpp 11,688→920, 16 commits).
Each file: map methods → extract domain TUs → build → commit → repeat.

### Tier 1 (>9000 lines — highest impact)

| File | Lines | Domain split targets |
|------|-------|---------------------|
| `character.cpp` | 11,713 | biometrics, skills, effects, movement, crafting, combat, mutations, bionics |
| `map.cpp` | 9,817 | lighting, furn/terrain, items-on-map, construction, vehicle-interaction, streaming |
| `iuse.cpp` | 8,919 | consumables, tools, medical, weapons, electronics, explosives |

### Tier 2 (7000-9000 lines)

| File | Lines | Domain split targets |
|------|-------|---------------------|
| `vehicle.cpp` | 8,430 | parts, movement, fuel, turrets, controls, damage |
| `iexamine.cpp` | 8,186 | furniture, terrain, containers, electronics, farming |
| `iuse_actor.cpp` | 7,879 | actors by category (repair, transform, deploy, inscribe) |
| `activity_actor.cpp` | 7,426 | by activity type (craft, move, dig, fish, forage, lockpick) |
| `game.cpp` | 7,196 | input, movement, combat, npc-interaction, environmental |

### Tier 3 (existing plans, additional gates)

| File | Lines | Status |
|------|-------|--------|
| `overmap.cpp` | 6,700 | Existing plan — gated on Phase 0 determinism harness |
| `mapgen.cpp` | 6,555 | RNG-ordered, needs determinism guard like overmap |
| `savegame_json.cpp` | ~4,989 | Existing plan — pure mechanical split by subsystem |

## Phase 3: Performance Optimization

Profile-guided. Cannot proceed until structural changes land.

1. Profile hot paths (runtime flamegraph during normal gameplay)
2. Identify cache-unfriendly access patterns in map/creature iteration
3. Optimize serialization (savegame_json is a known bottleneck)
4. Evaluate SIMD opportunities in lighting/physics code

## Execution Strategy

- Mechanical modernizations: AST-driven batch edits, one commit
- God-file decompositions: parallel subagents in isolated worktrees
- Each decomposition: sequential extract→build→commit within its worktree
- Performance: deferred to after structural changes land

## Verification

- Build green after every commit
- Symbol diff (nm) verifies no methods lost or duplicated
- Domain-tagged test suites pass after each decomposition
- Full test suite after all changes merged
