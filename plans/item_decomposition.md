# Item Decomposition — Plan

## Context

`item.cpp` at **11,683 lines** is the single largest file in the project — nearly twice the size of the next largest (`vehicle.cpp`, 8404). It already has the best `std::ranges` adoption (26x) and reasonable use of modern patterns, but the file is unmanageable for navigation, review, and testing. No single person can hold 700+ functions in their head.

`item.h` (2794 lines) is also oversized but carries declarations for the entire class — its size is a symptom of the same problem.

## Current state

| Metric | item.cpp | item.h |
|--------|---------|--------|
| Lines | 11,683 | 2794 |
| Functions/methods | ~700 | — |
| `ranges::` | 26 | — |
| `detached_ptr` | 88 | 51 |
| `std::optional` | moderate | — |
| Manual loops | ~21 range-for | — |

## Approach

**Strictly .cpp-only split** — no header changes, no API changes, no behavioral changes. Each new file `#include "item.h"` and implements a subset of `item`'s methods. The original `item.cpp` retains only constructors, destructors, serialization, and light inspection methods, then includes a disclaimer comment.

## Phases

### Phase 1 — Audit and plan the split

Map every method in `item.cpp` to one of 8 domain categories. Output a category→method list for review before any code moves.

Categories (preliminary):
1. **Combat/Weapon** — damage, penetration, gun mechanics, throwing
2. **Crafting/Recipe** — component checks, uncraft, recipe data
3. **Use/Activation** — `process()`, `activate()`, `reload()`, `burn()`, countdown actions
4. **Food/Comestible** — nutrition, rot, calorie queries, cooking
5. **Books/Knowledge** — reading, skill gain, morale from reading
6. **Tools/Qualities** — tool quality queries, charges, ammo
7. **Apparel/Clothing** — coverage, warmth, encumbrance, fitting
8. **Containers/Storage** — capacity, sealing, liquid handling

### Phase 2 — Combat/Weapon → `item_combat.cpp`

Extract all methods returning combat-relevant properties:
- `item::damage_melee()`, `item::damage_cut()`, `item::gun_damage()`, `item::gun_range()`
- `item::armor_cut()`, `item::armor_balistic()`
- `item::throw_range()`, `item::throw_damage()`
- `item::weapon_category()`, `item::weapon_speed()`
- Any method whose primary caller is `melee.cpp` or `ranged.cpp`

### Phase 3 — Crafting/Recipe → `item_crafting.cpp`

- `item::is_components()`, `item::has_recipe_data()`
- `item::get_uncraft_components()`, `item::get_recipe_component()`
- `item::can_repair_with()`, `item::repair_level()`
- `item::is_tool()`, `item::is_ammo()` (also used by crafting)

### Phase 4 — Use/Activation → `item_use.cpp` (merge with existing)

`src/item_use.cpp` already exists at ~800 lines with some activation logic. Merge the remaining activation/process methods from `item.cpp`:
- `item::process()`, `item::activate()`, `item::reload()`, `item::burn()`, `item::tick()`
- `item::needs_processing()`, `item::process_artifact()`, `item::process_wet()`
- `item::is_going_to_become()`, `item::will_recharge()`

### Phase 5 — Remaining categories

- `item_food.cpp` — rot, nutrition, calories, vitamins, freshness
- `item_book.cpp` — reading time, skill gain, fun, chapters
- `item_tool.cpp` — charges, ammo capacity, qualities, uses
- `item_armor.cpp` — coverage, encumbrance, warmth, protection
- `item_container.cpp` — capacity, contents, sealing, liquids

### Phase 6 — Include cleanup

After all methods are extracted:
- Check if `item.h` still needs to include `bodypart.h`, `skill.h`, `material.h` etc. (likely some are only needed by individual .cpp files now)
- Reduce transitive includes from `item.h` to speed up builds
- `item.cpp` now keeps only: ctors, dtor, `serialize()/deserialize()`, `is_null()`, `type_name()`, `tname()`, `display_name()`, `info()`, and forwarding statements

## Verification (per phase)

- Build green. No header changes.
- `nm -C cataclysm-bn-tiles | c++filt | grep "item::"` shows same set of symbols before/after.
- `wc -l src/item.cpp` drops by ~1000–1500 per phase.
- All existing tests pass (no behavioral changes).

## Files

| File | Phase | Notes |
|------|-------|-------|
| `src/item.cpp` | 1–6 | Shrinks to ~1500 lines |
| `src/item.h` | 6 | Include cleanup only |
| `src/item_combat.cpp` (new) | 2 | |
| `src/item_crafting.cpp` (new) | 3 | |
| `src/item_use.cpp` (existing) | 4 | Expanded |
| `src/item_food.cpp` (new) | 5 | |
| `src/item_book.cpp` (new) | 5 | |
| `src/item_tool.cpp` (new) | 5 | |
| `src/item_armor.cpp` (new) | 5 | |
| `src/item_container.cpp` (new) | 5 | |
| `src/CMakeLists.txt` | 2–5 | |

## Effort: 1–2 weeks
- Phase 1: 1 day (audit)
- Phase 2–5: 1–3 days each (mechanical, each 300–500 lines moved)
- Phase 6: 1 day (include cleanup)
