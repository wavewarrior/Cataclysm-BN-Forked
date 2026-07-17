# Full Codebase Modernization Plan

## Status: COMPLETE (2026-07-17)

Three sessions: 10 god files decomposed, ~22k lines extracted across 17 commits into 17 new domain TUs.
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

### Session 3 — Deeper extraction (2 commits)

| File | Before | After | Reduction | New TUs |
|------|--------|-------|-----------|---------|
| `character.cpp` | 7,810 | 7,469 | −341 (4%) | `character_bionics.cpp` |
| `npcmove.cpp` | 4,613 | 3,359 | −1,254 (27%) | `npcmove_combat.cpp` |

### All extraction commits (17)

```
Session 3:
596e8d48 refactor(npc): extract npcmove_combat.cpp
aa8d540d refactor(character): extract character_bionics.cpp

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

### Files NOT decomposed (with rationale)

| File | Lines | Rationale |
|------|-------|-----------|
| `game.cpp` | 7,196 | Already has 9 domain splits. Remaining is core orchestration. |
| `vehicle.cpp` | 7,613 | Further extraction blocked by `distribution_graph::traverse` template. Power/damage extracted. |
| `overmap.cpp` | 6,700 | Gated on determinism harness (see `plans/overmap_modernization.md`). |
| `mapgen.cpp` | 6,555 | RNG-ordered, same determinism gate as overmap. |
| `newcharacter.cpp` | 4,641 | Tightly coupled UI + character creation — doesn't decompose cleanly. |
| `ranged.cpp` | 4,440 | Mostly free functions with cross-deps — not class-method-based. |
| `handle_action.cpp` | 4,320 | Mixed namespaces, duplicated function blocks — needs different approach. |

## Phase 3: Performance Optimization — DEFERRED (needs runtime profiling)

The codebase already has:
- Tracy instrumentation throughout hot paths (`ZoneScoped`, `ZoneScopedN`)
- LOD tiers for monster AI (tier 2 macro-tick optimization)
- Precomputed monster plans (parallel planning pass)
- Parallel z-level cache builds in `build_map_cache`

Static analysis found no low-hanging fruit — the per-turn paths are well-optimized.
Meaningful further optimization requires runtime flamegraphs with representative gameplay.

## Dependency Modernization (Session 0)

Also completed in the initial session:
- SDL3 3.4.10 → 3.4.12
- Lua 5.3.6 → 5.4.8 (vendor swap, no compat layer)
- fmt 7.1.3 → 12.2.0 (flat re-vendor)
- Catch2 3.7.1 → 3.15.2
- libbacktrace pinned to SHA, Tracy pinned to v0.13.1
- AGENTS.md rebaselined with adoption-reality table
- clangd re-enabled for agent harness
