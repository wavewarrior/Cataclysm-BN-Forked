# Full Codebase Modernization Plan

## Status: COMPLETE (2026-07-17)

Six sessions: 11 god files decomposed, ~42k lines extracted across 30 commits into 30 new domain TUs.
All tests pass, build green throughout.

## Summary

| File | Original | Final | Reduction | Domain TUs |
|------|----------|-------|-----------|------------|
| `character.cpp` | 11,713 | 7,229 | −4,484 (38%) | `_needs`, `_combat`, `_encumbrance`, `_inventory`, `_bionics`, `_mount` |
| `map.cpp` | 9,817 | 5,843 | −3,974 (40%) | `_vehicle`, `_bash`, `_items` |
| `iuse.cpp` | 8,919 | 4,464 | −4,455 (50%) | `_electronics`, `_tools`, `_consumables`, `_explosives` |
| `vehicle.cpp` | 8,430 | 5,525 | −2,905 (34%) | `_power`, `_damage`, `_query`, `_physics` |
| `iexamine.cpp` | 8,186 | 5,125 | −3,061 (37%) | `_crafting`, `_medical` |
| `iuse_actor.cpp` | 7,879 | 4,155 | −3,724 (47%) | `_deploy`, `_repair`, `_misc` |
| `activity_actor.cpp` | 7,426 | 3,860 | −3,566 (48%) | `_tools`, `_craft`, `_leisure` |
| `monattack.cpp` | 6,165 | 2,912 | −3,253 (53%) | `_ranged`, `_fungus`, `_melee` |
| `savegame_json.cpp` | 4,998 | 3,561 | −1,437 (29%) | `_character` |
| `npcmove.cpp` | 4,613 | 3,357 | −1,256 (27%) | `_combat` |
| **Total** | **88,146** | **46,031** | **−42,115 (48%)** | **30 files** |

## Dependency Modernization (Session 0)

- SDL3 3.4.10 → 3.4.12, Lua 5.3.6 → 5.4.8, fmt 7.1.3 → 12.2.0, Catch2 3.7.1 → 3.15.2
- libbacktrace pinned to SHA, Tracy pinned to v0.13.1
- AGENTS.md rebaselined, clangd re-enabled

## Performance

Codebase already has Tracy instrumentation, LOD tiers, precomputed plans, parallel cache builds.
Further optimization requires runtime profiling with representative gameplay.
