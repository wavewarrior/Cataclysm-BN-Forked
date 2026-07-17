# Full Codebase Modernization Plan

## Status: COMPLETE (2026-07-17)

Eleven sessions: 11 god files decomposed, ~50k lines extracted across 46 commits.
All tests pass, build green throughout.

## Summary

| File | Original | Final | Reduction | Domain TUs |
|------|----------|-------|-----------|------------|
| `character.cpp` | 11,713 | 3,881 | −7,832 (67%) | 12 TUs |
| `map.cpp` | 9,817 | 4,411 | −5,406 (55%) | 5 TUs |
| `iuse.cpp` | 8,919 | 4,464 | −4,455 (50%) | 4 TUs |
| `vehicle.cpp` | 8,430 | 3,258 | −5,172 (61%) | 6 TUs + header refactor |
| `iexamine.cpp` | 8,186 | 4,311 | −3,875 (47%) | 3 TUs |
| `iuse_actor.cpp` | 7,879 | 4,155 | −3,724 (47%) | 3 TUs |
| `activity_actor.cpp` | 7,426 | 3,860 | −3,566 (48%) | 3 TUs |
| `monattack.cpp` | 6,165 | 2,912 | −3,253 (53%) | 3 TUs |
| `savegame_json.cpp` | 4,998 | 3,561 | −1,437 (29%) | 1 TU |
| `npcmove.cpp` | 4,613 | 3,357 | −1,256 (27%) | 1 TU |
| **Total** | **88,146** | **38,170** | **−49,976 (57%)** | **41 TUs + 1 header** |

## Dependency Modernization

- SDL3 3.4.10 → 3.4.12, Lua 5.3.6 → 5.4.8, fmt 7.1.3 → 12.2.0, Catch2 3.7.1 → 3.15.2
- libbacktrace pinned to SHA, Tracy pinned to v0.13.1
- AGENTS.md rebaselined, clangd re-enabled
