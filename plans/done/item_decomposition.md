## STATUS — COMPLETE (2026-07-02)

**item.cpp: 11,688 → 1,262 lines (−89%).** Phase 6 partial: `item.cpp` include pruning done
(42 unused headers removed, commit `8a46ef4291`). `item.h` include cleanup is NOT tractable
as a follow-on to body extraction: item.h includes are driven by the class *declaration*
(member variable types, method signatures, base classes) — which is unchanged. Pruning item.h
would require forward-declaration refactoring (different scope, higher risk per AGENTS.md
"don't modify headers with >10 usages"). Declare DONE. Move item.h forward-decl work to a
separate plan if ever needed for build-speed gains.

| Commit | File | Lines moved | item.cpp before → after |
|--------|------|------------|--------------------------|
| `4752d73269` | `item_info.cpp` | 2,950 | 11,688 → 8,738 |
| `931f8fa625` | `item_combat.cpp` | 668 | 8,738 → 8,069 |
| `3d5b7ea113` | `item_food.cpp` | 413 | 8,069 → 7,656 |
| `c2a54aca1d` | `item_gun.cpp` | 856 | 7,656 → 6,800 |
| `9f8d8e9e5c` | `item_process.cpp` | 881 | 6,800 → 5,919 |
| `1760aec44f` | `item_armor.cpp` + `item_type.cpp` (atomic) | 1,137 | 5,919 → 4,782 |
| `64e964c6f7` | `item_events.cpp` | 302 | 4,782 → 4,478 |
| `fa066e8db6` | `item_display.cpp` | 604 | 4,478 → 3,869 |
| `3428bf8089` | `item_fire.cpp` | 169 | 3,869 → 3,700 |
| `c98c846588` | `item_container.cpp` | 220 | 3,700 → 3,480 |
| `e5c1b54f04` | `item_properties.cpp` | 280 | 3,480 → 3,200 |
| `5311540105` | `item_flags.cpp` | 234 | 3,200 → 2,966 |
| `86576efb02` | `item_tool.cpp` | 500 | 2,966 → 2,466 |
| `04c8be2d26` | `item_misc.cpp` | 765 | 2,466 → 1,701 |
| `ebfaf55744` | `item_ownership.cpp` | 397 | 1,701 → 1,304 |
| `8a46ef4291` | include pruning (42 removed) | — | 1,304 → **1,262** |

`item_info.cpp` holds `item::basic_info`..`info_string` + 4 info helpers.
`item_combat.cpp` holds: DPS calc (`hits_by_accuracy`, `effective_dps`, `dps`, `average_dps`);
melee methods (`attack_cost`, `stamina_cost`, `damage_melee`×2, `get_attacks`, 12 bonus get/set,
`base_damage_*`, `reach_range`, `can_shatter`); physical resist (`phys_resist<>` + `bash/cut/stab/bullet/acid/fire_resist`).
`item_food.cpp` holds: `item_internal` namespace (goes-bad cache; sole users moved); `goes_bad()` cluster
(`goes_bad`, `goes_bad_after_opening`, `is_in_preserving_container`, `mark_rot_checked_now`,
`get_shelf_life`, `get_relative_rot`, `set_relative_rot`, `set_rot`, `spoilage_sort_order`, `calc_rot`,
`minimum_freshness_duration`, `mod_last_rot_check`); rot-apply cluster (`process_rot`×2, `update_rot_from_location`, `update_rot`).
All three verified: build+link green, `item::` symbol set byte-identical (463), `[item]` tests
pass (140,636 assertions / 47 cases). `[melee]` has 5 pre-existing seed-0 probabilistic
failures confirmed identical before and after the combat cut.

**NOTE — resist method standing debt:** `bash/cut/acid/fire_resist` are protection-flavored and
belong conceptually in `item_armor.cpp`, which was created in commit `1760aec44f`. They were NOT
migrated — they remain in `item_combat.cpp`. This is standing technical debt; migration is
mechanical (move + update statics) if ever pursued.

**CORRECTIONS to the plan below (verified against tree 2026-07-02):**
- **No CMakeLists edits needed.** `src/CMakeLists.txt` uses `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`
  — new `src/item_*.cpp` auto-compile. Ignore the `CMakeLists.txt` rows in the Files table.
- **`item_use.cpp` does NOT exist.** Phase 4 must CREATE it, not "merge with existing".
- **Gating risk the plan omits:** file-scope `static`/anon-namespace helpers + `static const` id
  constants (internal linkage) referenced by a moved method. Use compiler-guided extraction — build
  the object lib `cataclysm-bn-tiles-common` (no link) to surface undeclared-id (new TU) and
  unused-const (old TU) precisely. Add needed id decls to the new TU; remove only sole-user-moved
  ones from item.cpp (leave PRE-EXISTING dead decls like `itype_cig_*`/`joint_roach` — not our mess).
  Full procedure captured in managed skill `cpp-godfile-decompose`.

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
