## STATUS (reviewed 2026-06-27)
**~15% DONE — KEEP (partial).** Only the `color` slice shipped: `src/cata_tiles_color.cpp` exists (13 `get_*_color` methods moved). NOT done: `cata_tiles_internal.h` (Phase 0), `cata_tiles_overrides.cpp` (Phase 1 dry-run), `_anim`, `_draw_layers`, `_lookup`, `_tileset`. `cata_tiles.cpp` still **7,647 LOC** (grew slightly; `tile_type_search`/`draw`/`refresh_anim_frame`/`draw_terrain` all still in the monolith). Plan is realistic and matches reality (the GLOB build-fact held; color split proves the multi-TU mechanism works). Worth finishing — keep. Note Phase 0 (internal.h) was *skipped*, suggesting the color slice had no cross-file symbols; later slices will hit the TU-locality issue the plan warns about.

# Decompose `src/cata_tiles.cpp` (7,443 LOC god-class)

## Context

`cata_tiles.cpp` is the largest remaining undecomposed rendering file (7,443 LOC) after the
`sdltiles.cpp` decomposition (which is now down to ~295 LOC). It is a single `cata_tiles`
class conflating 7+ concerns: tileset load/surface-FX, tile lookup, per-category draw,
sprite pipeline, color extraction, animation/overlay frames, and override maps. One giant
translation unit → slow to compile, hard to navigate, obscures the per-concern structure.

**Goal:** split the method *definitions* across concern-named `.cpp` files **without changing
behavior**. C++ permits one class's methods to be defined across many translation units, so
the `cata_tiles.h` class declaration stays the single source of truth. This is a strictly
mechanical move — methods keep `this->` access to all members, so no dependency threading,
no signature changes, no logic edits. The verification gate (build-green + in-game visual
identity) is only meaningful *because* nothing semantic changes.

This mirrors the spirit of the sdltiles decomposition (peel by concern, `sdl_<concern>.cpp`
naming, one commit per unit) but the mechanism differs: sdltiles was free functions lifted
out; cata_tiles is one class whose definitions get spread across sibling TUs.

**Decisions:** full 6-file split; new files flat in `src/` (matches the `sdl_<concern>.cpp`
precedent).

## Build-system facts (verified)

- **CMake** (`src/CMakeLists.txt:5`): `file(GLOB_RECURSE ... src/*.cpp CONFIGURE_DEPENDS)` →
  **any new `src/cata_tiles_*.cpp` is auto-compiled. No build-file edit.** This drives all
  platforms (Mac/Metal, Linux, and Windows via CMake-generated VS projects).
- **`msvc-full-features/*.vcxproj` is a stub** — it lists only `main.cpp`, `messages.cpp`,
  `stdafx.cpp`; `cata_tiles.cpp` is **absent** yet builds on Windows → that project is not
  the source-of-truth build. New sibling files inherit the same (CMake) path. No vcxproj edit.
- `USE_UNITY_BUILD` option exists (default OFF). Low risk: our split keeps all file-scope
  symbol names unique, so unity concatenation cannot collide. (Verify-on if unity is enabled.)

## Approach: same-class, multi-TU definition split

1. `cata_tiles.h` class declaration **unchanged** (only adds an `#include` if a relocated
   template needs it — none expected; see Phase 0).
2. One new internal header `src/cata_tiles_internal.h` holds the handful of TU-local symbols
   that are genuinely used across more than one target file.
3. Each new `.cpp` starts with `#include "cata_tiles.h"` + `#include "cata_tiles_internal.h"`
   (when needed) + a copied subset of the obvious project headers; let the compiler name any
   missing include rather than hand-minimizing the 91-include list.

### Scope guards (explicit NON-goals — keep this behavior-preserving)

- **Do NOT crack open the big methods.** `tile_type_search()` (~581 LOC) and `draw()`
  (~1082 LOC) move *wholesale* into their target files. Intra-method extraction is a
  separate, behavior-risky refactor — note as future follow-up, not part of this.
- **Do NOT convert the `static` color getters to free functions.** They are `this`-free and
  tempting, but conversion changes the header and all call sites. Relocate them as-is
  (`cata_tiles::` statics). Free-function conversion is an optional later pass.
- **Do NOT prune includes.** Copy a working subset; the compiler is the authority.

## TU-locality dependency map (the gating analysis)

Splitting a class across TUs link-fails if a method in file A references a TU-local symbol
(anon-namespace helper, file-scope `static`, or `.cpp`-defined template) that lives in
file B. Map of what crosses file boundaries:

**Genuinely cross-file → move to `cata_tiles_internal.h`** (as C++17 `inline` const vars /
`inline` free fn — no ODR issue):
- `empty_string` — used by 5+ methods across core/lookup/anim/layers (highest sharing)
- `itype_corpse` — `draw_field_or_item` (layers), `find_tile_looks_like` + `tile_type_search` (lookup)
- `multitile_keys` — `draw_from_id_string` (core) + `tile_type_search` (lookup)
- `TILE_CATEGORY_IDS` — `lr_generic` (tileset) + `tile_type_search` (lookup)
- `divide_round_down` — `find_tile_with_season` (lookup) + `tile_hit_xform` (anim)

**Single-cluster → co-locate with sole caller, no promotion:**
- Surface/blend helpers (`get_surface_hash`, `apply_blend_filter`,
  `apply_color_filter_blit_copy`, `is_contained`, `apply_uv_remap`, `get_pixel_function`,
  `ilerp`, `apply_surf_blend_effect`) → all `reinit()` → **tileset file**.
- UV-modifier helpers (`create_identity_uv_surface`, `chain_uv_modifier_at`,
  `foliage_sway_weight`, `group_applies_to_overlay`) → `draw_block` /
  `build_composite_uv_modifier` → **draw_layers file**.
- `msgtype_to_tilecolor`→`draw_sct_frame`, `draw_zone_overlay`→`draw_zones_frame`,
  `s_anim_tuning`→`refresh_anim_frame`+`compute_anim_xform` → all **anim file**.
- `effect_ridden`, `trait_INATTENTIVE`→`draw_critter_at`; `ITEM_HIGHLIGHT`→`draw_item_highlight`;
  `ZOMBIE_REVIVAL_INDICATOR`→`draw_zombie_revival_indicators`; `flag_TINT_*`→`draw_sprite_at`.

**Template members (must stay with their callers — same link hazard):**
- `find_tile_looks_like_by_string_id<T>` (5 explicit instantiations) ↔ `find_tile_looks_like`
  → keep both in **lookup file**.
- `lr_generic` + 3 `tile_loading_report` overloads ↔ `do_tile_loading_report`
  → keep all in **tileset file**.

`tileset` / `tileset_loader` classes live in `cata_tiles.h` (not TU-local) → no constraint.

## File boundaries (designed to minimize cross-cutting)

| New file | Methods (line ranges in current cata_tiles.cpp) | Co-located helpers/consts |
|---|---|---|
| `cata_tiles_tileset.cpp` | `on_options_changed`,`load_tileset`,`reinit`, `tileset::` impls (1699/1741/2526/2534), `do_tile_loading_report`+report templates+`lr_generic`, `build_renderer_list`,`build_display_list`, `init_light` | surface/blend anon helpers |
| `cata_tiles_lookup.cpp` | `tile_type_search`(1945), `find_tile_with_season`, `find_tile_looks_like`+template, `find_overlay_looks_like`, `get_terrain_orientation`,`get_rotation_and_subtile`,`get_connect_values`,`get_furn_connect_values`,`get_tile_values`,`get_tile_values_with_ter` | — (uses internal.h) |
| `cata_tiles_draw_layers.cpp` | `draw_terrain/furniture/trap/graffiti/field_or_item/vpart/critter_at/zone_mark/zombie_revival_indicators`(5196–5976), `draw_block`, `build_composite_uv_modifier`, `draw_entity_with_overlays`, `draw_item_highlight` | UV-modifier helpers; layer consts |
| ✅ DONE `cata_tiles_color.cpp` | all `get_*_color` (static + instance), color/tint logic | — |
| `cata_tiles_anim.cpp` | `refresh_anim_frame`,`compute_anim_xform`,`*_require(s)_animation`,`register_tile_hit`,`tile_hit_xform`, all `init_draw_*`/`void_*`/`draw_*_frame` (explosion/cone_aoe/bullet/hit/line/cursor/highlight/weather/sct/zones/footsteps), `init_explosion` etc. | `s_anim_tuning`,`msgtype_to_tilecolor`,`draw_zone_overlay` |
| `cata_tiles_overrides.cpp` | `init_draw_*_override`/`void_*_override`/`has_draw_override` (6570–6713) | — (helper-free) |
| **`cata_tiles.cpp`** (remains) | ctor/dtor, `draw()` main dispatch (3087–4169), `draw_sprite_at`/`draw_tile_at`/`draw_color_at`/`draw_from_id_string`/`draw_om_tile_recursively`, vision/memory (`apply_vision_effects`,`has_memory_at`,`get_*_memory_at`), `player_to_screen`,`get_window_tile_counts`,`set_draw_scale`,`display_character`,`draw_minimap`/`reset_minimap` | `flag_TINT_*` |

Result: core `cata_tiles.cpp` drops from 7,443 → ~1.8k LOC (the draw pipeline), with 6 concern files of ~600–1.1k LOC each.

## Phasing (one commit per file, each bisectable)

- **Phase 0 — internal header (de-risks everything after).** Create `cata_tiles_internal.h`
  with the 5 cross-file symbols as `inline` vars / `inline` fn; replace their file-scope
  defs in `cata_tiles.cpp` with the header include. Build-green + visual glance. Commit.
  *Once the cross-cutters live in a shared header, every later file move has zero
  TU-locality surprises.*
- **Phase 1 — dry-run slice:** extract `cata_tiles_overrides.cpp` (6570–6713, helper-free,
  tiny). Validates the build plumbing cheaply on the real build. Commit.
- **Phase 2…7 — one file per commit**, easiest→hardest: ✅ `color` (DONE) → `anim` → `draw_layers`
  → `lookup` → `tileset`. Each: cut definitions + co-located helpers + needed includes into
  the new file, build-green, in-game visual-identity glance, commit.
  > 2026-06-27: only `color` shipped (Phase 0 internal.h + Phase 1 overrides were skipped/not done).

## Verification

- **Per commit:** project's existing CMake build must be green (same build used for all prior
  lighting commits). A move-only change that compiles + links is almost certainly correct —
  the link step *proves* no TU-local symbol was orphaned.
- **In-game visual-identity glance** each commit: load a tiles save, confirm terrain/furniture/
  critters/items/vehicles/overlays/animations/weather render identically. Because this is
  pure relocation, any visual diff = a missed include or a dropped helper, not a logic bug.
- **Bisectable:** one file per commit → a regression points at exactly one move.
- **Windows/unity check (low priority, optional):** confirm a CMake configure picks the new
  files up on Windows and under `USE_UNITY_BUILD=ON` (expected fine; names stay unique).

## Out of scope (future follow-ups)

- Intra-method breakup of `tile_type_search` (581 LOC) and `draw()` (1082 LOC).
- Promoting `static` color getters to a free-function `color_provider` namespace.
- True subsystem extraction (separate classes with narrowed interfaces / testability).
- Include-list minimization across the new files.
