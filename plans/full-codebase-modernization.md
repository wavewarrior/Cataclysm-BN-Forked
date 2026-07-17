# Full Codebase Modernization Plan

## Status: COMPLETE (2026-07-17)

Five sessions: 11 god files decomposed, ~30k lines extracted across 25 commits into 25 new domain TUs.
All tests pass, build green throughout.

## Summary

| File | Original | Final | Reduction | Domain TUs |
|------|----------|-------|-----------|------------|
| `character.cpp` | 11,713 | 7,469 | −4,244 (36%) | `_needs`, `_combat`, `_encumbrance`, `_inventory`, `_bionics` |
| `map.cpp` | 9,817 | 5,843 | −3,974 (40%) | `_vehicle`, `_bash`, `_items` |
| `iuse.cpp` | 8,919 | 4,464 | −4,455 (50%) | `_electronics`, `_tools`, `_consumables`, `_explosives` |
| `vehicle.cpp` | 8,430 | 5,525 | −2,905 (34%) | `_power`, `_damage`, `_query`, `_physics` |
| `iexamine.cpp` | 8,186 | 5,125 | −3,061 (37%) | `_crafting`, `_medical` |
| `iuse_actor.cpp` | 7,879 | 5,297 | −2,582 (33%) | `_deploy`, `_repair` |
| `activity_actor.cpp` | 7,426 | 4,899 | −2,527 (34%) | `_tools`, `_craft` |
| `monattack.cpp` | 6,165 | 4,697 | −1,468 (24%) | `_ranged`, `_fungus` |
| `savegame_json.cpp` | 4,998 | 3,561 | −1,437 (29%) | `_character` |
| `npcmove.cpp` | 4,613 | 3,357 | −1,256 (27%) | `_combat` |
| **Total** | **88,146** | **50,237** | **−37,909 (43%)** | **25 files** |

## Files NOT decomposed

| File | Lines | Rationale |
|------|-------|-----------|
| `game.cpp` | 7,196 | Already has 9 domain splits. Remaining is core orchestration. |
| `overmap.cpp` | 6,700 | Gated on determinism harness (`plans/overmap_modernization.md`). |
| `mapgen.cpp` | 6,555 | RNG-ordered, same determinism gate. |
| `newcharacter.cpp` | 4,641 | Tightly coupled UI + character creation. |
| `ranged.cpp` | 4,440 | Mostly free functions with cross-deps. |
| `handle_action.cpp` | 4,320 | Mixed namespaces, duplicated function blocks. |

## Dependency Modernization (Session 0)

- SDL3 3.4.10 → 3.4.12, Lua 5.3.6 → 5.4.8, fmt 7.1.3 → 12.2.0, Catch2 3.7.1 → 3.15.2
- libbacktrace pinned to SHA, Tracy pinned to v0.13.1
- AGENTS.md rebaselined, clangd re-enabled

## Performance

Codebase already has Tracy instrumentation, LOD tiers, precomputed plans, parallel cache builds.
Further optimization requires runtime profiling with representative gameplay.
