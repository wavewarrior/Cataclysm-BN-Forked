## STATUS (reviewed 2026-06-27)
**0% DONE — KEEP (not started, realistic).** `savegame_json.cpp` still **4,989 lines**; none of the proposed targets exist (`src/avatar/save.cpp`, `src/npcs/save.cpp`, `src/overmap/save.cpp`, `src/items/save.cpp`, `src/faction/save.cpp`, etc. — all absent). Plan is mechanical, low-risk, and matches current reality. Sound but aspirational until someone picks it up. Keep — it's a clean, accurate roadmap, not stale.

# Save/Load Decomposition — Plan

## Context

`savegame_json.cpp` (4989 lines) is a monolithic file where every subsystem's serialization lives. As new features are added, the file grows linearly with no decomposition boundary. Every save-related change requires editing this single file, increasing merge conflicts and review overhead.

The serialization pattern itself is reasonable (JSON visitor with `JsonOut`/`JsonIn`), but the co-location of all serialization in one file is an organizational debt.

## Current state

| Metric | Value |
|--------|-------|
| Lines | 4989 |
| Subsystems serialized | ~20 (avatar, NPCs, vehicles, items, map, overmap, factions, missions, etc.) |
| `ranges::` | 2 |
| `std::optional` | moderate |
| `std::expected` | 0 |
| Trailing returns | 15 |

## Approach

**Mechanical split** — move each subsystem's `serialize()`/`deserialize()` implementation bodies to the subsystem's own directory. The game still calls serialization through the same entry points (`save_game()`, `load_game()`).

No behavioral changes. No API changes. No header changes.

## Phases

### Phase 1 — Audit and plan

Map every function in `savegame_json.cpp` to a target file. Produces a split map for review.

Preliminary split targets:

| New file | Content from savegame_json.cpp |
|----------|-------------------------------|
| `src/avatar/save.cpp` | Avatar serialization |
| `src/npcs/save.cpp` | NPC serialization |
| `src/vehicle/save.cpp` | Vehicle serialization |
| `src/map/save.cpp` | Map/submap serialization |
| `src/overmap/save.cpp` | Overmap serialization |
| `src/items/save.cpp` | Item serialization |
| `src/faction/save.cpp` | Faction serialization |
| `src/mission/save.cpp` | Mission serialization |
| `src/achievement/save.cpp` | Achievement/statistics serialization |

### Phase 2 — Move implementation bodies

For each subsystem:
1. Create the target `.cpp` file with `#include` of the relevant headers
2. Move the `save()`/`load()` overload implementations
3. Verify build green
4. Continue to next subsystem

Each subsystem move is a separate commit for bisect safety.

### Phase 3 — Reduce savegame_json.cpp to orchestration

After all moves: `savegame_json.cpp` retains only:
- `save_game()` / `load_game()` entry points
- Per-system save order and dispatch
- Shared helpers (`open_file`, `write_common_data`, version checks)
- The `ACT_MIGRATION_CANCEL` block (save-compat logic tied to dispatch, not a subsystem)

Target: `savegame_json.cpp` < 500 lines.

### Phase 4 — C++23 modernization (opportunistic)

While touching each subsystem's save file:
- Use `std::expected` for deserialization error reporting (`JsonIn` errors → `std::expected<T, std::string>` instead of `debugmsg`)
- Trailing return types
- `auto` for local variables
- `ranges::*` for container serialization where applicable

Note: `std::expected` changes are higher risk (error propagation changes control flow) — mark as optional per-file.

## Verification (per phase)

- Build green. Save/load round-trip: save in any state → `save_game()` → `load_game()` → compare key state fields, they match.
- `wc -l src/savegame_json.cpp` drops per phase.
- No new includes in headers (all new includes are in .cpp files).

## Files

| File | Phase |
|------|-------|
| `src/savegame_json.cpp` | 1 (source), 3 (shrinks) |
| `src/avatar/save.cpp` (new) | 2 |
| `src/npcs/save.cpp` (new) | 2 |
| `src/vehicle/save.cpp` (new) | 2 |
| `src/map/save.cpp` (new) | 2 |
| `src/overmap/save.cpp` (new) | 2 |
| `src/items/save.cpp` (new) | 2 |
| `src/faction/save.cpp` (new) | 2 |
| `src/mission/save.cpp` (new) | 2 |
| `src/achievement/save.cpp` (new) | 2 |
| `src/CMakeLists.txt` | 2 |

## Effort: 1 week
- Phase 1: 1 day (audit)
- Phase 2: 2–3 days (9 subsystems × 2–3 hours each)
- Phase 3: 1 day (orchestration cleanup)
- Phase 4: 1–2 days (optional, opportunistic)
