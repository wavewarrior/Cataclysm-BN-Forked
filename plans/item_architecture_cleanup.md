# Item Subsystem Architectural Cleanup

**STATUS: COMPLETE** — 2026-07-03. Binary links cleanly on macOS (`osx-arm-slim` preset).

| Phase | Status | Notes |
|-------|--------|-------|
| 1 — Extract standalone types from item.h | ✅ Done | `iteminfo.h`, `item_reload_option.h`, `light_emission.h`, `item_cable.h`, `item_functions.h` created; `overmapbuffer.h` removed |
| 2 — Migrate methods to correct TUs | ✅ Done | 2a–2k committed; `rad_badge_color` restored to `item_info.cpp` per 2l; dead code removed (2m) |
| 3 — item.cpp code quality | ✅ Done | `copy_fields_from` extracted (3c), `has_item_with_id` range-fixed (3d), 5 stale includes removed (3b), anon namespaces confirmed (3a) |
| 4 — AGENTS.md style modernization | ⏭ Deferred | User marked obsolete. Scope: trailing returns, auto, std::ranges across 16 TUs |
| 5 — item_tags encapsulation | ✅ Done | `item_tags` private; all 5 external callers migrated to `set/unset_flag` |
| 6 — Decompose item_info.cpp | ✅ Done | `iteminfo_gun.cpp`, `iteminfo_food.cpp`, `iteminfo_bionic.cpp`, `iteminfo_combat.cpp` created; armor methods in `iteminfo_armor.cpp`; 3085→1462 lines. ≤800 target unmet: `final_info` (435 lines) cannot be split per plan; `basic_info`+9 small methods remain as residual |


## Context

The `item.cpp` P6 decomposition (−89%) was a mechanical file-split with no API or structural changes. It exposed three categories of architectural debt now addressed here:

1. **`item.h` include bloat**: ~370 lines of standalone types (cable state machine, display structs, reload option class) that have no membership in `item`, and a suspicious `#include "overmapbuffer.h"` forced in by the cable struct.
2. **Methods in wrong TUs**: resist methods in `item_combat.cpp` not `item_armor.cpp`; `item_ownership.cpp` is a second dump file covering 6 unrelated domains; `item_misc.cpp` contains fire/gun/event/armor methods; `item_fire.cpp` contains `typeId()`; `item_tool.cpp` contains `iteminfo` constructors.
3. **Pervasive style violations**: All 16 split TUs use old-style prefixed return types, typed locals, manual loops, `std::pair` returns, and no options structs — all prohibited by AGENTS.md.
4. **`item_info.cpp` is a new 3,085-line god-file** requiring the same decomposition treatment item.cpp received.

End state: leaner header with `overmapbuffer.h` removed from item.h's transitive chain; every method in its logical TU; `item_info.cpp` decomposed; `item_tags` private; all split TUs AGENTS.md-compliant.

---

## Approach

Six phases. Phases 1, 2, 4, and 6 are mutually independent and can run in parallel. Phase 3 depends on Phase 1 (reduced include noise). Phase 5 depends on Phases 1 and 3.

---

### Phase 1 — Extract standalone types from item.h

**1a. `struct iteminfo` + operators → `src/iteminfo.h`** (new file, ~115 lines)

`struct iteminfo` (item.h lines 137–198) with nested `enum flags` and two inline `operator|` functions (~lines 194–203) is a display-data struct. No dependency on `item` members.

- Create `src/iteminfo.h`: `#pragma once`, `#include <string>`, `#include <type_traits>`, paste struct + operators verbatim.
- In item.h: delete those lines, insert `#include "iteminfo.h"`.
- No callers need updating — all get `iteminfo` transitively through item.h.

**1b. `class item_reload_option` → `src/item_reload_option.h`** (new file, ~34 lines)

`item_reload_option` (item.h lines 205–235); implementation is in `src/item_tool.cpp`. Needs forward declarations `class player; class item;` and `#include <climits>` for `INT_MAX`.

- Create `src/item_reload_option.h`: `#pragma once`, `#include <climits>`, `class player; class item;`, paste class.
- In item.h: delete lines 205–235, insert `#include "item_reload_option.h"`.
- Add `#include "item_reload_option.h"` to direct callers confirmed by grep: `activity_actor.cpp`, `avatar_action.cpp`, `character_functions.h`, `character_functions.cpp`, `item_tool.cpp`, `iuse_actor.cpp`, `npcmove.cpp`, `ranged.cpp`, `vehicle_use.cpp`.
- In `character_functions.h`: replace `class item_reload_option;` forward decl with `#include "item_reload_option.h"`.

**1c. `struct light_emission` + `extern nolight` → `src/light_emission.h`** (new file, ~8 lines)

`struct light_emission` (item.h lines 95–100) and `extern light_emission nolight` — POD for lightmap. `nolight` defined in `item.cpp` line 131.

- Create `src/light_emission.h`: `#pragma once`, paste struct + extern.
- In item.h: delete those lines, add `#include "light_emission.h"`.
- Move `light_emission nolight = {0, 0, 0}` from `item.cpp` to new `src/light_emission.cpp` with `#include "light_emission.h"`. CMakeLists uses GLOB — no edit needed.
- Add `#include "light_emission.h"` to direct users: `savegame_json.cpp` (`nolight.luminance`), `item_fire.cpp`, `item_type.cpp`, `item_factory.cpp`. Verify: `grep -rl "nolight\b\|light_emission" src/ | grep -v item\.h`.
- **Contingency**: if separate `.cpp` causes linker issues, use `inline light_emission nolight = {0, 0, 0};` in the header instead.

**1d. Cable types → `src/item_cable.h`** (new file, ~170 lines; eliminates `overmapbuffer.h` from item.h)

`cable_connection_data` (item.h lines 2659–2804) is 145 lines of cable-state machine. It forces `#include "overmapbuffer.h"` into item.h. The `cable_state` enum (lines 103–110) and the `p1_name`/`p2_name`/`source_p1_name`/`source_p2_name` constants + `tripoint_abs_ms_min` constant (lines 111–115) are all cable-specific.

- Create `src/item_cable.h`:
  ```cpp
  #pragma once
  #include "item.h"
  #include "coordinates.h"
  #include <optional>
  #include <string>
  // cable_state enum (verbatim)
  // p1_name, p2_name, source_p1_name, source_p2_name as inline const std::string (see 1e)
  // tripoint_abs_ms_min as inline constexpr (check constructor availability)
  // struct cable_connection_data (verbatim, lines 2659–2804)
  ```
- In item.h: delete lines 103–115, delete lines 2659–2804, remove `#include "overmapbuffer.h"` (line 27).
- Add `#include "item_cable.h"` to confirmed callers: `bionics.cpp`, `item_misc.cpp`, `item_process.cpp`, `iuse.cpp`, `vehicle_part.cpp`. Verify: `grep -rl "cable_connection_data\|cable_state\|p1_name\|p2_name\b" src/ | grep -v item\.h`.
- **`TINT_*_VAR_NAME` constants** (lines 117–123): separately grep callers: `grep -rn "TINT_COLOR_VAR_NAME" src/`. If all callers already include item.h, move these string constants to the anonymous namespace of `item_display.cpp` or `item_properties.cpp` (wherever display code lives). Remove from item.h.

**1e. `static const std::string` → `inline const std::string` in new headers**

String constants currently using `static const std::string` create one copy per TU in any header. When moved to `item_cable.h` or remaining in item.h, change to `inline const std::string` (C++17 inline variable — single definition regardless of include count). Apply to all string constants extracted to headers.

**1f. `kill_tracker.h` → forward declaration only**

item.h includes `kill_tracker.h` for `std::unique_ptr<kill_tracker> kills`. `unique_ptr<T>` only needs T complete at destructor definition — in item.cpp.

- In item.h: replace `#include "kill_tracker.h"` with `class kill_tracker;` in the forward declarations block.
- In item.cpp: add `#include "kill_tracker.h"`.

**1g. `charge_removal_blacklist` / `to_cbc_migration` → callers only**

Declared in item.h lines 2646–2657, implemented entirely in `savegame_json.cpp`. Only callers: `init.cpp` and `savegame_json.cpp`. Remove from item.h; add declarations to `init.cpp`'s relevant section directly.

**1h. Free comparator declarations → `item_functions.h`**

`bool item_compare_by_charges(...)` / `bool item_ptr_compare_by_charges(...)` declared at item.h lines 2634–2635; implementations in `item.cpp` lines 1247–1261; only caller: `crafting.cpp` (uses `item_ptr_compare_by_charges`).

- Add declarations to `src/item_functions.h` alongside the existing `item_funcs` namespace.
- Move implementations from `item.cpp` to `src/item_functions.cpp`.
- Remove from item.h.
- Add `#include "item_functions.h"` to `crafting.cpp` if not already present.

**1i. Verify: overmapbuffer.h gone**

```sh
echo '#include "item.h"' | g++ -std=c++23 -I src -M - 2>/dev/null | tr ' ' '\n' | grep overmapbuffer
# Expected: no output
```

---

### Phase 2 — Migrate methods to their correct TUs

All moves are `.cpp`-only: no header changes, no signature changes, no behavioral changes. Use compiler-guided extraction (build `cataclysm-bn-tiles-common` after each move to surface missing file-local helpers). Commit each logical group atomically.

**2a. Resist methods + dispatcher: `item_combat.cpp` and `item_type.cpp` → `item_armor.cpp`**

From `item_combat.cpp` (confirmed lines):

| Method | Lines |
|---|---|
| `template<ResistGetter> static int phys_resist(...)` (file-local) + `#pragma optimize` pair | 555–605, 630–632 |
| `item::bash_resist(bool)` | 622–624 |
| `item::cut_resist(bool)` | 626–628 |
| `item::stab_resist(bool)` | 634–637 |
| `item::bullet_resist(bool)` | 639–642 |
| `item::acid_resist(bool, int)` | 644–677 |
| `item::fire_resist(bool, int)` | 679–711 |

From `item_type.cpp`:

| Method | Lines |
|---|---|
| `item::damage_resist(damage_type, bool)` | 280–305 |

Move all eight as a block to `item_armor.cpp`. `item_armor.cpp` already has `material.h`, `clothing_mod.h`, `damage.h`. The `damage_resist` dispatcher calls the resist methods — keeping them together eliminates cross-file coupling (`item_info.cpp::armor_protection_info` currently creates `item_info.cpp → item_combat.cpp` coupling through these calls).

**2b. Armor predicates: `item.cpp` → `item_armor.cpp`**

| Method | item.cpp lines |
|---|---|
| `item::covers(const bodypart_id&)` | 822–825 |
| `item::get_covered_body_parts()` | 827–830 |
| `item::get_covered_body_parts(side)` | 832–876 |
| `item::is_sided()` | 878–882 |
| `item::get_side()` | 884–889 |
| `item::set_side(side)` | 891–904 |
| `item::swap_side()` | 906–909 |
| `item::is_worn_only_with(const item&)` | 911–915 |
| `item::get_sizing(const Character&)` | 1150–1206 |

Move as block. Needed includes: `"bodypart.h"`, `"enums.h"` (`side`), `"itype.h"` (`islot_armor`) — all likely present in item_armor.cpp already.

**2c. Clothing mod methods: `item_misc.cpp` → `item_armor.cpp`**

| Method | item_misc.cpp lines |
|---|---|
| `item::has_clothing_mod()` | 622–628 |
| `get_clothing_mod_val_key(...)` (anon ns helper) | 630–644 |
| `item::get_clothing_mod_val(clothing_mod_type)` | 646–648 |
| `item::update_clothing_mod_val()` | 650–659 |

Move together (the anon-ns helper must move with its sole caller).

**2d. `item_ownership.cpp` domain cleanup**

`item_ownership.cpp` is a dump file with 6 unrelated domains. Move each group to its correct TU:

| Methods | To |
|---|---|
| `item::get_mod_locations()` / `get_free_mod_locations()` | `item_gun.cpp` |
| `item::brewing_time()` / `brewing_results()` | `item_food.cpp` |
| `item::can_revive()` / `ready_to_revive(const tripoint_bub_ms&)` | `item_process.cpp` |
| `item::spill_contents(Character&)` / `spill_contents(const tripoint_bub_ms&)` | `item_container.cpp` |
| `item::get_chapters()` / `get_remaining_chapters(...)` / `mark_chapter_as_read(...)` / `get_available_recipes(...)` | `item_misc.cpp` |
| `item::get_random_material()` / `get_base_material()` / `operator<(const item&)` | `item_type.cpp` |

What remains in `item_ownership.cpp` after these moves: `is_owned_by`, `is_old_owner`, `get_owner_name`, `set_owner`, `get_owner`, `get_old_owner`, `validate_ownership`, `can_contain` (×2), `get_contained`, `get_enchantments`, `bonus_from_enchantments`, `bonus_from_enchantments_wielded`, `get_relic_recharge_scheme`, `count_by_charges`, `count`, `craft_has_charges`, `is_money`, `is_money`. This is still a mixed file but smaller; refinement can continue in a follow-up.

**2e. Fire-domain methods: `item_misc.cpp` → `item_fire.cpp`**

| Methods | item_misc.cpp lines |
|---|---|
| `item::will_explode_in_fire()` | 118–130 |
| `item::detonate(detached_ptr<item>&&, const tripoint_bub_ms&, std::vector<...>&)` | 132–165 |

Move to `item_fire.cpp`. Needed includes already present in item_fire.cpp.

**2f. Gun-specific method: `item_misc.cpp` → `item_gun.cpp`**

`item::get_gun_ups_drain()` (item_misc.cpp lines 427–441) — gun-specific UPS discharge query.

**2g. Event method: `item_misc.cpp` → `item_events.cpp`**

`item::on_drop(const tripoint_bub_ms&)` (line 470–472) and `item::on_drop(const tripoint_bub_ms&, map&)` (lines 474–490).

**2h. Ownership event: `item_events.cpp` → `item_ownership.cpp`**

`item::handle_pickup_ownership(Character&)` (item_events.cpp lines 313–357) — faction ownership and NPC witness logic.

**2i. `typeId()`: `item_fire.cpp` → `item_type.cpp`**

`item::typeId()` (item_fire.cpp lines 233–235) is a fundamental identity accessor. Move to `item_type.cpp`.

**2j. `iteminfo` constructors: `item_tool.cpp` → `item_info.cpp`**

`iteminfo::iteminfo(...)` constructors (item_tool.cpp lines 616–632). `iteminfo` is being extracted to `iteminfo.h` in Phase 1a; its constructors should live in `item_info.cpp` (or a new `iteminfo.cpp` if `item_info.cpp` is being decomposed in Phase 6 — put them in the residual `item_info.cpp`).

**2k. Category accessors: `item_tool.cpp` → `item_type.cpp`**

`item::get_category_id()` / `item::get_category()` (item_tool.cpp lines 597–614) — general classification, not tool/reload logic.

**2l. `rad_badge_color()` declaration + implementation cleanup**

Implementation in `item.cpp` lines 108–126; callers: `item_info.cpp` and `suffer.cpp`. Move implementation to `item_info.cpp`. Create `src/item_info.h` with declaration `std::string rad_badge_color(int rad);`. Replace item.h line 93 with `#include "item_info.h"`. Add `#include "item_info.h"` to `suffer.cpp`.

**2m. Dead code removal from `item.cpp`**

Lines 96–106: `class npc_class; using npc_class_id = ...;` — vestigial, zero uses in remaining item.cpp functions. Delete.

Lines 89–93: Five `static const itype_id itype_cig_*` constants — grep each in item.cpp body only; if zero uses, delete.

---

### Phase 3 — item.cpp code quality

**3a. Anon namespace for file-local symbols**

Change `static` to anonymous namespace wrapping for:
- Lines 135–139: `static const itype *nullitem()`
- Lines 272–282: `static const item *get_most_rotten_component(const item&)`
- The five `itype_id` constants at lines 89–93 (if not deleted by 2m)

**3b. Stale include removal**

After Phases 1 and 2, audit these `item.cpp` includes — remove one at a time and rebuild: `<iomanip>`, `<locale>`, `<tuple>`, `"effect.h"`, `"npc.h"`, `"player.h"`, `"avatar.h"`, `"inventory.h"`, `"projectile.h"`, `"skill.h"`, `"character_functions.h"`, `"string_formatter.h"`, `"string_id_utils.h"`, `"cloning_utils.h"`.

**3c. Copy/assignment helper**

Extract `void item::copy_fields_from(const item &src)` (private, declared in item.h private section, defined in item.cpp). The ~40-field copy list duplicated verbatim in the copy ctor (lines 325–378) and `operator=` (lines 380–436) both delegate to it. No behavioral change.

**3d. `has_item_with_id` range fix**

item.cpp line 1138: replace typed `std::vector<item *> item_contents` + manual loop with:
```cpp
auto item_contents = contents.all_items_top();
return std::ranges::any_of( item_contents, [&]( const item *itm ) {
    return itm->typeId() == itype;
} );
```

---

### Phase 4 — AGENTS.md style modernization across split TUs

Apply to all 16 `item_*.cpp` files. Order by size (smallest first, build after each file): `item_fire.cpp` → `item_container.cpp` → `item_flags.cpp` → `item_events.cpp` → `item_properties.cpp` → `item_display.cpp` → `item_armor.cpp` → `item_misc.cpp` → `item_ownership.cpp` → `item_food.cpp` → `item_tool.cpp` → `item_type.cpp` → `item_gun.cpp` → `item_combat.cpp` → `item_process.cpp` → `item_info.cpp`.

**4a. Trailing return types** — `ReturnType fn(params)` → `auto fn(params) -> ReturnType` for every named function and method. Not lambdas. Verify each file with: `grep -c "^    [a-zA-Z_].* item::" src/item_FILENAME.cpp`.

**4b. `auto` variable declarations** — `SomeType var = expr` → `auto var = expr` when type is unambiguously deducible. Skip when the declared type is narrower than the deduced type (intentional narrowing).

**4c. `std::ranges` adoption**:
- `std::find_if(b, e, pred)` → `std::ranges::find_if(v, pred)`
- `std::any_of(b, e, pred)` → `std::ranges::any_of(v, pred)`
- `std::sort(b, e, cmp)` → `std::ranges::sort(v, cmp)`
- `std::accumulate(b, e, init, fn)` → `std::ranges::fold_left(v, init, fn)` (C++23)
- Manual erase-while-iterate → `std::erase_if(container, pred)`
- Manual `push_back` loop → filter/transform + `std::ranges::to<std::vector>()`
- Add `namespace ranges = std::ranges;` or `using namespace std::views;` function-local when used 3+ times
- Prefer method/function references: `transform(&Type::method)` over `[](auto& x){ return x.method(); }`

**4d. Options structs for 4+ parameter functions** — any non-trivial function called from ≥2 sites. Create struct in anonymous namespace of that file; use `{ .field = value }` at call sites. Key targets confirmed by audit:
- `process_internal(detached_ptr<item>&&, player*, tripoint_bub_ms&, bool, bool, temperature_flag, const weather_manager&)` — 7 params → options struct
- `process(…, temperature_flag, const weather_manager&)` — 6 params
- `process_rot(detached_ptr<item>&&, bool, tripoint_bub_ms&, player*, temperature_flag, const weather_manager&)` — 6 params
- `phys_resist(const item&, damage_type, clothing_mod_type, ResistGetter, bool)` — 5 params (after moving to item_armor.cpp)
- `use_charges(detached_ptr<item>&&, const itype_id&, int&, std::vector<...>&, const tripoint_bub_ms&, std::function<bool(const item&)>&)` — 6 params

**4e. Named structs for `std::pair`/`std::tuple` returns**:
- `item_combat.cpp`: `calc_effective_damage` lambda returns `std::make_pair(subtotal_moves, subtotal_damage)` — create `struct melee_summary { double moves; double damage; };` in anonymous namespace
- `item_ownership.cpp`: variables typed as `std::pair<bodypart_str_id, int>` in `on_wear` — create `struct body_part_mod { bodypart_str_id bp; int count; };`

**4f. Structured bindings for pair iteration** — replace `for(const std::pair<K, V> &e : map)` with `for(const auto &[key, val] : map)` throughout.

**4g. Cross-file duplicated static IDs → shared header**

These static ID constants are duplicated across TUs (confirmed by audit):

| Constant | Duplicate files |
|---|---|
| `static const itype_id itype_UPS("UPS")` | `item_process.cpp`, `item_tool.cpp` |
| `static const itype_id itype_bio_armor("bio_armor")` | `item_process.cpp`, `item_tool.cpp` |
| `static const skill_id skill_survival("survival")` | `item_display.cpp`, `item_info.cpp` |
| `static const skill_id skill_throw("throw")` | `item_gun.cpp`, `item_info.cpp` |
| `static const itype_id itype_barrel_small("barrel_small")` | `item_display.cpp`, `item_properties.cpp` |
| `static const trait_id trait_WOOLALLERGY("WOOLALLERGY")` | `item_display.cpp`, `item_info.cpp` |

Deduplicate by moving each to a single canonical TU (the one that uses it more, or the domain owner) and removing from the other. Do not create a shared header for static IDs — static IDs with internal linkage belong in exactly one TU.

**4h. Manual DT scan loop → shared helper**

The pattern `for(int idx = DT_NULL+1; idx != NUM_DT; ++idx)` appears in `item_gun.cpp::melee_skill()` and `item_type.cpp::is_melee()`. Extract a shared helper to `src/damage.h` (if a `damage_types()` range view doesn't already exist there) or to `item_functions.h`:
```cpp
namespace item_funcs {
// Returns a range of all valid damage_type values excluding DT_NULL
auto all_damage_types() -> std::span<const damage_type>;
}
```

---

### Phase 5 — `item_tags` encapsulation

**Prerequisite**: Phases 1 and 3 complete.

`item_tags` has `// TODO: Move to private ASAP`. All `item_factory.cpp` `item_tags` accesses are on `itype` objects (verified by audit: `finalize_pre`, `finalize_post`, `set_allergy_flags`, `npc_implied_flags` all take `itype &`) — `itype` has its own `item_tags` field separate from `item::item_tags`. Zero scope in item_factory.cpp. No new API is needed: existing `set_flag()` covers all runtime-`item` callers.

**Migration by caller pattern** (all three patterns use existing `set_flag()` — no new methods required):

| Pattern | Sites | Fix |
|---|---|---|
| `.item_tags.insert(flag_id(...))` | `inventory.cpp` ×3 (confirmed `item` via `spawn_temporary`), `vehicle_use.cpp` ×1 (confirmed `item &` from `spawn_temporary`), `artifact.cpp` ×2 (verify `def` is `item` not `itype` before editing) | `.set_flag(...)` |
| `.item_tags` direct access | `savegame_json.cpp` — grep `\.item_tags` in this file and confirm type before editing | `.set_flag(...)` or `get_flags()` as appropriate |

Move `item_tags` from `public:` to `private:`. Build — compiler surfaces any remaining direct access.

---

### Phase 6 — Decompose `item_info.cpp`

`item_info.cpp` is 3,085 lines — the same problem as item.cpp before P6. A partial extraction already produced `iteminfo_armor.cpp` (412 lines) but left the largest methods behind.

Use the same compiler-guided approach from the `cpp-godfile-decompose` skill: `.cpp`-only splits, each new file `#include "item.h"`, CMakeLists auto-picks up new files.

**Target splits in extraction order (largest-first approach):**

| New file | Methods to move | item_info.cpp lines | ~Size |
|---|---|---|---|
| `iteminfo_gun.cpp` (new) | `gun_info`, `gunmod_info`, `ammo_info`, `magazine_info` | 852–1340 | ~490 |
| `iteminfo_armor.cpp` (extend) | Add `armor_protection_info`, `armor_fit_info`, `animal_armor_info` | 1342–1600 | +260 |
| `iteminfo_food.cpp` (new) | `food_info`, `med_info` | 427–716 | ~290 |
| `iteminfo_bionic.cpp` (new) | `bionic_info` | ~2154–2285 | ~130 |
| `iteminfo_combat.cpp` (new) | `combat_info`, `damage_statblock_info` | ~2286–2540 | ~255 |

**Residual `item_info.cpp`** (~700–800 lines): `basic_info`, `final_info` (mark `// TODO: decompose` — it has the scoped `const iteminfo_query *parts = &parts_ref;` hack), `container_info`, `battery_info`, `tool_info`, `component_info`, `repair_info`, `disassembly_info`, `qualities_info`, `book_info`, `contents_info`, `info()` overloads, `info_string()` overloads.

**Procedure for each extraction:**
1. Create new `src/iteminfo_*.cpp` with `#include "item.h"` and target methods.
2. Build `cataclysm-bn-tiles-common` (no link). Compiler surfaces undeclared file-local helpers in new TU.
3. Copy needed file-local statics from `item_info.cpp` to new TU. Key helpers: `get_ranged_pierce()`, `get_ranged_armor_mult()`, `get_base_env_resist()` — check which functions each method actually calls.
4. Build with link (`cataclysm-bn-tiles`). Zero undefined references expected.
5. Remove moved code + sole-user helpers from `item_info.cpp`. Rebuild — no unused-static warnings.
6. Run `./out/build/linux-full/tests/cata_test-tiles "[item]"` — zero new failures.
7. Commit atomically.

**Extract `iteminfo_gun.cpp` first** (largest, ~490 lines; `gun_info` at 252 lines is the biggest method). Likely needed includes for `iteminfo_gun.cpp`: `"dispersion.h"`, `"gun_mode.h"`, `"ranged.h"`, `"iteminfo_format_utils.h"`.

---

## Critical files & anchors

| File | Symbol / region | Reason |
|---|---|---|
| `src/item.h:27` | `#include "overmapbuffer.h"` | Remove after Phase 1d — this is the key build-speed win |
| `src/item.h:95–123` | `cable_state` enum + string constants | Phase 1d extraction start |
| `src/item.h:137–235` | `struct iteminfo` + `class item_reload_option` | Phase 1a + 1b |
| `src/item.h:2659–2804` | `cable_connection_data` struct | Phase 1d — 145 lines driving overmapbuffer.h |
| `src/item_combat.cpp:555–711` | `phys_resist` + 6 resist methods | Phase 2a — move to item_armor.cpp |
| `src/item_type.cpp:280–305` | `damage_resist(damage_type, bool)` | Phase 2a — move with resist methods |
| `src/item_info.cpp:852–1340` | `gun_info` + `gunmod_info` + `ammo_info` | Phase 6 first extraction (~490 lines) |

---

## Verification

### After every change: build + targeted tests
```sh
cmake --build --preset linux-full --target cataclysm-bn-tiles cata_test-tiles 2>&1 | tail -20
./out/build/linux-full/tests/cata_test-tiles "[item]" --reporter compact
```

### Phase 1i: overmapbuffer.h removed
```sh
echo '#include "item.h"' | g++ -std=c++23 -I src -M - 2>/dev/null | tr ' ' '\n' | grep overmapbuffer
# Expected: no output
```

### Phase 2a: resist methods in correct file
```sh
grep -c "bash_resist\|cut_resist\|acid_resist\|fire_resist\|phys_resist" src/item_combat.cpp
# Expected: 0
grep -c "item::bash_resist\|item::cut_resist\|item::damage_resist" src/item_armor.cpp
# Expected: > 0
```

### Phase 5: item_tags private
```sh
grep -rn "\.item_tags\b" src/ | grep -v "^src/item\b"
# Expected: 0 (all migrated to accessors)
```

### Phase 6: item_info.cpp reduced
```sh
wc -l src/item_info.cpp
# Target: ≤ 800 lines
```

### Behavioral regression across all domains
```sh
./out/build/linux-full/tests/cata_test-tiles "[item],[armor],[melee],[food],[gun]" --reporter compact
# 5 pre-existing probabilistic failures in [melee] are known; 0 new failures expected
```

---

## Assumptions & contingencies

- **Phase 1c `nolight` ODR**: if the separate `light_emission.cpp` causes ODR violations, switch to `inline light_emission nolight = {0, 0, 0};` in the header.
- **Phase 1d `item_cable.h` includes `item.h`**: correct — item.h stops including item_cable.h entirely. No circular dep. Callers that need cable types include item_cable.h directly.
- **Phase 1e inline const strings**: `inline const std::string` requires C++17. The project already uses C++23 features — this is safe.
- **Phase 2a MSVC pragma**: The `#pragma optimize("", off/on)` pair at item_combat.cpp lines 576–579 and 630–632 wraps `phys_resist` for a known MSVC compiler bug. Move the pragma pair with `phys_resist` to item_armor.cpp.
- **Phase 2b file-local helpers in item.cpp**: if any armor-domain method in item.cpp calls `nullitem()` or other file-local helpers, the compiler surfaces it after the move. `nullitem()` stays in item.cpp; add `static const itype *nullitem()` as a forward declaration where needed or use the public `is_null()` equivalent.
- **Phase 6 `final_info` god-function** (~339 lines, item_info.cpp ~2639): contains a scoped `const iteminfo_query *parts = &parts_ref;` pointer alias hack marked `// TODO: Remove`. Do not split this function during Phase 6 — leave in residual `item_info.cpp` with the existing TODO.
- **Phase 6 `get_comestible_fun()` in item_flags.cpp**: the audit found this belongs in `item_food.cpp`. Move it as part of Phase 2 (alongside `brewing_time`/`brewing_results` moves) — add it to step 2d food group.
- **Ordering**: Phase 1 (sub-steps 1a→1h in order, rebuild after each) → then Phase 2 (steps 2a→2m can be grouped by destination file, each atomic commit), Phase 4, and Phase 6 in parallel → Phase 3 after Phase 1 → Phase 5 last.
