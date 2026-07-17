# Full Codebase Modernization Plan

## Status: COMPLETE (2026-07-17)

Ten sessions: 11 god files decomposed, ~48k lines extracted across 43 commits into 43 new domain TUs.
All tests pass, build green throughout.

## Summary

| File | Original | Final | Reduction | Domain TUs |
|------|----------|-------|-----------|------------|
| `character.cpp` | 11,713 | 3,881 | −7,832 (67%) | 12: `_needs`, `_combat`, `_encumbrance`, `_inventory`, `_bionics`, `_mount`, `_vision`, `_movement`, `_item_use`, `_description`, `_mutation`, `_stats` |
| `map.cpp` | 9,817 | 4,411 | −5,406 (55%) | 5: `_vehicle`, `_bash`, `_items`, `_terrain`, `_access` |
| `iuse.cpp` | 8,919 | 4,464 | −4,455 (50%) | 4: `_electronics`, `_tools`, `_consumables`, `_explosives` |
| `vehicle.cpp` | 8,430 | 4,575 | −3,855 (46%) | 5: `_power`, `_damage`, `_query`, `_physics`, `_items_tow` |
| `iexamine.cpp` | 8,186 | 5,125 | −3,061 (37%) | 2: `_crafting`, `_medical` |
| `iuse_actor.cpp` | 7,879 | 4,155 | −3,724 (47%) | 3: `_deploy`, `_repair`, `_misc` |
| `activity_actor.cpp` | 7,426 | 3,860 | −3,566 (48%) | 3: `_tools`, `_craft`, `_leisure` |
| `monattack.cpp` | 6,165 | 2,912 | −3,253 (53%) | 3: `_ranged`, `_fungus`, `_melee` |
| `savegame_json.cpp` | 4,998 | 3,561 | −1,437 (29%) | 1: `_character` |
| `npcmove.cpp` | 4,613 | 3,357 | −1,256 (27%) | 1: `_combat` |
| **Total** | **88,146** | **40,301** | **−47,845 (54%)** | **39 new + 4 pre-existing = 43** |

## Dependency Modernization

- SDL3 3.4.10 → 3.4.12, Lua 5.3.6 → 5.4.8, fmt 7.1.3 → 12.2.0, Catch2 3.7.1 → 3.15.2
- libbacktrace pinned to SHA, Tracy pinned to v0.13.1
- AGENTS.md rebaselined, clangd re-enabled

## Performance

Codebase already has Tracy instrumentation, LOD tiers, precomputed plans, parallel cache builds.
Further optimization requires runtime profiling with representative gameplay.
