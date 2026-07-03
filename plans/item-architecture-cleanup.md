# Item Subsystem Architectural Cleanup

## Context

The `item.cpp` P6 decomposition (-89%) was a mechanical file-split with no API or structural changes. It exposed three categories of architectural debt now addressed here:

1. **`item.h` include bloat**: ~370 lines of standalone types (cable state machine, display structs, reload option class) that have no membership in `item`, and a suspicious `#include "overmapbuffer.h"` forced in by the cable struct.
2. **Methods in wrong TUs**: resist methods in `item_combat.cpp` not `item_armor.cpp`; `item_ownership.cpp` is a second dump file covering 6 unrelated domains; `item_misc.cpp` contains fire/gun/event/armor methods; `item_fire.cpp` contains `typeId()`; `item_tool.cpp` contains `iteminfo` constructors.
3. **Pervasive style violations**: All 16 split TUs use old-style prefixed return types, typed locals, manual loops, `std::pair` returns, and no options structs - all prohibited by AGENTS.md.
4. **`item_info.cpp` is a new 3,085-line god-file** requiring the same decomposition as item.cpp was: extract ~1500 lines of domain-specific iteminfo constructors into 4 TUs, leaving ~700-800 lines of core display logic.

## Approach

### Phase 1 - Extract standalone types from item.h

**Prerequisite**: None. These are header-only extractions.

Extract the 5 independent types + 1 removed type from item.h into dedicated headers:

| Step | Type | Lines in item.h | New header | Notes |
|---|---|---|---|---|
| 1a | `struct iteminfo` | 137–203 | `src/iteminfo.h` | ✓ DONE |
| 1b | `class item_reload_option` | 205–245 | `src/item_reload_option.h` | ✓ DONE |
| 1c | `struct light_emission` + `extern nolight` | 260–297 | `src/light_emission.h` | ✓ DONE |
| 1d | `enum cable_state`, cable helpers, `struct cable_connection_data` | 298–400 | `src/item_cable.h` | ✓ DONE (circular include fixed) |
| 1e | Static const strings (`p1_name`, etc. inline) | Inline | *(part of 1d)* | ✓ DONE |
| 1f | `class kill_tracker` include → forward decl | ~line 50 | item.h forward decl | ✓ DONE |
| 1g | `charge_removal_blacklist`, `to_cbc_migration` | Removed | — | ✓ DONE |
| 1h | Free comparator `operator|` → `item_functions.h` | 194–203 | `item_functions.h` | ✓ DONE |
| 1i | Verify transitive chain: no `overmapbuffer.h` | — | — | ✓ VERIFIED |

**Result after Phase 1**: 
- item.h: -370 lines, -overmapbuffer.h, no standalone types
- Callers include extracted headers directly
- Zero includes of overmapbuffer from item subsystem

---

### Phase 2 - Migrate methods to correct TUs

**Prerequisite**: Phase 1 complete.

Compiler-guided method migration: compile after each group to catch dangling references, then move them. 13 sub-steps grouped by destination TU.

| Group | Destination | Sub-steps | Source files | Notes |
|---|---|---|---|---|
| 1 | `item_armor.cpp` | 2a, 2b, 2c | item_combat, item_type, item.cpp, item_misc | Resist, armor predicates, clothing |
| 2 | `item_ownership.cpp` | 2d, 2h | item_ownership, item_events | Cleanup 6 unrelated domains, ownership events |
| 3 | `item_fire.cpp` | 2e, 2i | item_misc, item_fire | Fire methods, typeId relocation |
| 4 | `item_gun.cpp` | 2f | item_misc | Gun-specific method |
| 5 | `item_events.cpp` | 2g | item_misc | Event handler methods |
| 6 | `item_type.cpp` | 2k | item_tool | Category accessors |
| 7 | `item_info.cpp` | 2j | item_tool | iteminfo constructors |
| 8 | Misc cleanup | 2l, 2m | item_misc, item.cpp | Utility cleanup, dead code |

**Build verification**: Between each group (not between each step); stops on first error.

---

### Phase 3 - item.cpp code quality (after Phase 1 AND 2)

**Prerequisite**: Phases 1 and 2 complete.

Non-API structural cleanup in item.cpp after methods are moved:

| Step | Change | Impact |
|---|---|---|
| 3a | Move file-local symbols into anonymous namespace | Reduces symbol table bloat |
| 3b | Audit stale `#include` — remove what methods took | Reduced coupling (only after Phase 2) |
| 3c | Extract `copy_fields_from()` into named helper | Code clarity |
| 3d | `has_item_with_id()` → `std::ranges` instead of manual loop | AGENTS.md compliance |

---

### Phase 4 - AGENTS.md style modernization across 16 split TUs

**Prerequisite**: Phase 2 complete.

Blanket style update across all item_*.cpp TUs:

| Step | Change | Scope | Notes |
|---|---|---|---|
| 4a | Trailing return types: `auto foo() -> int;` | All function signatures | Replace `int foo()` |
| 4b | `auto` for typed locals: `auto x = value;` | All variables | Replace `int x = ...` |
| 4c | `std::ranges` + views for collection iteration | Loops over containers | No manual `++it` |
| 4d | Options structs for functions with >3 params | Function signatures | Designated init at call sites |
| 4e | Named structs for `std::pair`/`std::tuple` returns | Return types | Replaces `pair<int,str>` |
| 4f | Structured bindings where applicable | Local unpacking | `auto [a, b] = func();` |
| 4g | Deduplicate static IDs across TUs | Static const strings | One authority per ID |
| 4h | Manual DT scan loop → shared helper | `for(int idx = DT_NULL+1; idx != NUM_DT; ++idx)` | Extract `all_damage_types()` range |

**Implementation**: Single-pass per TU (smallest to largest).

---

### Phase 5 - `item_tags` encapsulation

**Prerequisite**: Phases 1 and 3 complete.

`item_tags` has `// TODO: Move to private ASAP`. All `item_factory.cpp` `item_tags` accesses are on `itype` objects (verified by audit: `finalize_pre`, `finalize_post`, `set_allergy_flags`, `npc_implied_flags` all take `itype &`) - `itype` has its own `item_tags` field separate from `item::item_tags`. Zero scope in item_factory.cpp. No new API is needed: existing `set_flag()` covers all runtime-`item` callers.

**Migration by caller pattern** (all three patterns use existing `set_flag()` - no new methods required):

| Pattern | Sites | Fix |
|---|---|---|
| `.item_tags.insert(flag_id(...))` | `inventory.cpp` ×3 (confirmed `item` via `spawn_temporary`), `vehicle_use.cpp` ×1 (confirmed `item &` from `spawn_tool`) | Replace with `set_flag(flag)` (takes flag_id directly) |
| `.item_tags.erase(flag_id(...))` | `vehicle_use.cpp` ×1 | Replace with call to new private method or refactor condition |
| `.item_tags` direct iteration | None found | N/A |

**Final state**: `item_tags` is `private` in `item` class; all mutations go through public `set_flag()` and `remove_flag()` accessors.

---

### Phase 6 - Decompose `item_info.cpp` god-file (after Phase 2)

**Prerequisite**: Phase 2 complete (especially step 2j: iteminfo constructors move to item_info.cpp).

`item_info.cpp` is 3,085 lines (second-largest file after item.cpp before decomposition). Extract ~1,500 lines of domain-specific `iteminfo` constructors into 4 focused TUs; leave ~800 lines of shared utilities in item_info.cpp.

| New file | Methods | Size (est.) | Source ranges |
|---|---|---|---|
| `iteminfo_gun.cpp` | gun_info, gunmod_info, ammo_info, magazine_info | ~490 lines | item_info.cpp lines ~888–1378 |
| `iteminfo_armor.cpp` | (extend existing) armor_info, clothing_info + additional armor analysis | ~260 lines | item_info.cpp lines ~1379–1638 |
| `iteminfo_food.cpp` | food_info, drink_info, nutrition_info, comestible_info, food_flavor | ~290 lines | item_info.cpp lines ~1639–1928 |
| `iteminfo_bionic.cpp` | bionic_info, power_info, fuel_info | ~130 lines | item_info.cpp lines ~1929–2058 |
| `iteminfo_combat.cpp` | combat_info, skill_info, dodge/damage/crit analysis | ~255 lines | item_info.cpp lines ~2059–2313 |
| `item_info.cpp` (residual) | Shared utilities, final_info (~339 lines), get_info_color, tag parsing, etc. | ~700–800 lines | — |

---

## Key Design Decisions

- **No new API methods**: Phases 2–6 are refactorings only; no new public methods added.
- **Same-class split OK, cross-class NOT OK**: All Phase 2 moves stay within `class item` (don't create free functions or new classes unless the plan explicitly names them). Use friend declarations or private accessors as needed to avoid breaking encapsulation.
- **Forward declarations over includes**: Headers use forward decls for types that don't need full definitions. Callers (#include the actual headers. item.h specifically does NOT #include item_cable.h; callers do.
- **Compiler-guided extraction** (Phases 2, 4): Compile after each group; don't guess line numbers.
- **Atomic commits**: One commit per major group or phase (not per-step). Phases 1, 2, 3, 4, 6, 5 each = 1 commit.
- **phase 6 `final_info` god-function** (~339 lines, item_info.cpp ~2639): contains a scoped `const iteminfo_query *parts = &parts_ref;` pointer alias hack marked `// TODO: Remove`. Do not split this function during Phase 6 - leave in residual `item_info.cpp` with the existing TODO.
- **Phase 6 `get_comestible_fun()` in item_flags.cpp**: the audit found this belongs in `item_food.cpp`. Move it as part of Phase 2 (alongside `brewing_time`/`brewing_results` moves) - add it to step 2d food group.
- **Ordering**: Phase 1 (sub-steps 1a→1h in order, rebuild after each) → then Phase 2 (steps 2a→2m can be grouped by destination file, each atomic commit), Phase 4, and Phase 6 in parallel → Phase 3 after Phase 1 → Phase 5 last.

---

## Execution Status

### Phase 1: COMPLETE ✓
- All 9 sub-steps done (1a-1i)
- item.h reduced by 370 lines
- Circular include in item_cable.h fixed (uses forward decl, not #include "item.h")
- Verified: overmapbuffer.h gone from transitive chain
- Fixed: kill_tracker.h include added to callers (item.cpp verified complete before Phase 2 started)

### Phase 2: IN PROGRESS
- Delegated to comprehensive compiler-guided extraction task
- 13 sub-steps (2a-2m) grouped by destination TU
- Building after each group to catch dangling references
- Running in background; paused manual edits to avoid race condition
- Will reconcile results when complete
- Phases 4 & 6 in parallel: After Phase 2
- Phase 5: After Phases 1, 3, 4

