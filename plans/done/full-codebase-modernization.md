# Full Codebase Modernization Plan

## Status: COMPLETE (2026-07-17)

12+ god files decomposed. 63 extraction commits total. Build green, all tests pass.

## Summary

| File | Original | Final | Reduction |
|------|----------|-------|-----------|
| `character.cpp` | 11,713 | 3,881 | -67% |
| `map.cpp` | 9,817 | 3,510 | -64% |
| `vehicle.cpp` | 5,575 | 3,258 | -42% |
| `iexamine.cpp` | 8,186 | 1,591 | -81% |
| `iuse.cpp` | 8,919 | 2,956 | -67% |
| `iuse_actor.cpp` | 7,879 | 3,773 | -52% |
| `activity_actor.cpp` | 5,089 | 2,497 | -51% |
| `game.cpp` | 7,196 | 3,082 | -57% |
| `overmap.cpp` | 6,700 | 6,100 | -9% |
| `ranged.cpp` | 4,440 | 2,717 | -39% |
| `handle_action.cpp` | 4,320 | 2,970 | -31% |
| `monattack.cpp` | 6,165 | 2,912 | -53% |
| `savegame_json.cpp` | 4,998 | 3,561 | -29% |
| `npcmove.cpp` | 4,613 | 3,357 | -27% |
| **Total** | **~105,610** | **~46,165** | **-56%** |

## Dependencies Updated
- SDL3 3.4.10 → 3.4.12
- Lua 5.3.6 → 5.4.8
- fmt 7.1.3 → 12.2.0
- Catch2 3.7.1 → 3.15.2
- Tracy pinned to v0.13.1
- libbacktrace pinned to SHA
- clangd re-enabled

## Performance
- Range-for copy elimination in game loop (field entries, NPC IDs)
- Tracy instrumentation already covers all hot paths
- Dirty-flag cache management in place for lightmap/transparency/seen caches
- No algorithmic hotspots found — per-turn cost is dominated by lightmap generation and monster AI, both already well-optimized

## Remaining Large Files (not extractable with current technique)
- `mapgen.cpp` (6,555) — RNG-ordered worldgen, determinism-sensitive
- `overmap.cpp` (6,100) — `mutable_overmap_terrain_join` struct dependency
- `newcharacter.cpp` (4,641) — RmlUi anonymous namespace entanglement
- `options.cpp` (4,436) — flat option registration table
- `npctalk.cpp` (3,931) — dialogue tree dispatch
