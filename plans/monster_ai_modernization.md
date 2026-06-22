# Monster AI Modernization — Plan

## Context

`monattack.cpp` (6165 lines) and `monmove.cpp` (2784 lines) are the oldest, most legacy-dense gameplay files in the project. Together they carry 60+ `TODO`/`HACK` markers, a dedicated `legacy_pathfinding.h/.cpp` subsystem still in primary use, and two explicitly named "HORRIBLE HACK" blocks (`monattack.cpp:5765-5847`). The `SIM_PERFORMANCE_PLAN` adds throttling around this code but does not touch structural problems.

This plan covers code quality and C++ modernization only. Performance gating (z-aware LOD, lifecycle stride, spatial hash) is handled by `SIM_PERFORMANCE_PLAN Part 1`.

## Current state

| Metric | monattack.cpp | monmove.cpp | legacy_pathfinding.h/.cpp |
|--------|--------------|-------------|--------------------------|
| Lines | 6165 | 2784 | 63+131 |
| TODO/HACK markers | 44 | 16 | — |
| `ranges::` | 0 | 0 | 0 |
| Trailing returns | few | few | 0 |
| Manual index loops | ~15 | ~8 | 0 |
| Raw `new` | 4 | 1 | 0 |

## Phases

### Phase 1 — Decompose monattack.cpp

Split by monster action domain:

| New file | Contents from monattack.cpp |
|----------|---------------------------|
| `monattack_melee.cpp` | `attack_melee()`, `attack_upper_cut()`, etc. |
| `monattack_ranged.cpp` | `attack_shoot()`, `attack_gun()`, `attack_throw()`, etc. |
| `monattack_spell.cpp` | `attack_spell()`, monster-specific spell actions |
| `monattack_special.cpp` | Named monster specials (grab, sting, leap, spit, etc.) |
| `monattack_effect.cpp` | Effect application helpers, field interactions, status infliction |

Each new file: `#include "monster.h"`, `namespace monster_attacks { }`. Original `monattack.cpp` retains only the dispatch entry point (`execute_attack()`) and includes.

### Phase 2 — Eliminate HORRIBLE HACK blocks

Two documented hack blocks at `monattack.cpp:5765-5847`:

1. **Inventory processing hack** — fake item access for monsters without inventory (`5765-5810`). Replace with `monster::has_item_by_flag()` / `monster::use_item_by_flag()` that check `monster::inventory` with a safe fallback for `monster::inventory == nullptr`.

2. **Ammo-as-timer hack** — fake ammo count used as ability cooldown (`5811-5847`). Replace with `monster::ability_cooldown` member + `monster::can_use_ability()`. Port existing `monster::ammo[charge_type]` readers to the new member.

Both: add `on_monster_attack` and `on_monster_take_damage` hooks as proper methods instead of inline hack logic.

### Phase 3 — Replace legacy_pathfinding

`legacy_pathfinding.h` defines `pf_special` as a C bitmask enum with manual `operator|`/`operator&` (`pf_water`, `pf_lava`, `pf_sharp`, etc.). Replace:

- `enum pf_special : int` → `enum class pf_special : uint32_t` with `bitflag<pf_special>` wrapper (`src/bitflags.h` exists — model on `trait_flag` or `monster_flag`).
- Move hardcoded path-avoid settings from C++ (where each monster type sets them inline) into `monstergenerator.cpp` JSON extraction (already partially exists at `monstergenerator.cpp:1121-1246`).
- Delete `legacy_pathfinding.h/.cpp` after the last caller is migrated (verify: `rg "legacy_pathfinding" src/` returns zero).

### Phase 4 — Convert AI dispatch to strategy pattern

Current: `monmove.cpp` has `method_of_attack()` (~200 lines) → `execute_action()` (~300-line switch on `npc_action` enum) chain.

Replace with:

```cpp
struct monster_goal {
    virtual ~monster_goal() = default;
    virtual auto utility( const monster &m, const Creature &target ) const -> float = 0;
    virtual auto execute( monster &m, const Creature &target ) -> bool = 0;
    virtual auto id() const -> std::string_view = 0;
};
```

Registration via `static const std::vector<std::unique_ptr<monster_goal>> goals = { ... }` prioritized by `utility()`. `method_of_attack()` becomes a scan for the highest-utility goal whose `execute()` succeeds.

### Phase 5 — C++23 modernization pass

- Replace remaining index loops with `ranges::*` / `std::views`.
- Raw `Creature *` params → `Creature &` where nullable not required; `std::optional<Creature &>` (via `std::reference_wrapper`) or `Creature *` with null checks clarified to nonnull annotations.
- Trailing return types everywhere in touched files.
- `auto` for local variables.
- Options structs for `method_of_attack()`-style multi-param functions.

## Verification (per phase)

- **Build green** — `cataclysm-bn-tiles` compiles clean.
- **HACK/TODO count:** `rg "HACK|TODO" src/monattack.cpp src/monmove.cpp` drops per phase.
- **Phase 2:** The crash-repro horde save no longer hits `debugmsg` about missing inventory.
- **Phase 3:** `rg "legacy_pathfinding" src/` returns zero.
- **Phase 5:** `rg "for\s*\(\s*(int|size_t|auto)\s+\w+\s*=\s*(0|start)" src/monattack.cpp src/monmove.cpp` drops below 5.
- **Regression:** standard horde behavior A/B compare against a save with 20+ zombies; same kill count, same path decisions.

## Files

| File | Phase |
|------|-------|
| `src/monattack.cpp` | 1 (source), 2 (hack blocks) |
| `src/monattack_*.cpp` (new, 5 files) | 1 (target) |
| `src/monmove.cpp` | 4 (dispatch), 5 (modernize) |
| `src/monster.h` | 2 (new members), 4 (goal types) |
| `src/monstergenerator.cpp` | 3 (JSON extraction) |
| `src/legacy_pathfinding.h` | 3 (deleted) |
| `src/legacy_pathfinding.cpp` | 3 (deleted) |
| `src/CMakeLists.txt` | 1 (new files), 3 (remove files) |

## Effort: 2–3 weeks total
- Phase 1: 3–4 days (mechanical split)
- Phase 2: 1–2 days (hack elimination)
- Phase 3: 2–3 days (pathfinding)
- Phase 4: 3–4 days (strategy)
- Phase 5: 1–2 days (C++23)
