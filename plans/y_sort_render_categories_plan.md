# Restore vertical sprite slide via proper y-sorting (render categories + feet-anchor sort)

## Context

The sprite-animation system shipped with **vertical slide forced to zero**
(`creature.cpp`, `slide_offset_y = 0.f`). A vertical slide on a tall, bottom-anchored
sprite pushed it into the cell below, which then got painted over → "clipping into the
floor." The user wants vertical slide back and correctly diagnosed this as a
**draw-order / y-sorting** problem ("other things also suffer symptoms"). Extends
`plans/sprite_animation_plan.md`.

### Root cause (`cata_tiles::draw()`, `src/cata_tiles.cpp:3684-3728`)

Draw order is `for z (low→high) → per-row while (north→south) → terrain → base
(furniture/graffiti/trap) → zlevel bundle (field_or_item, vpart, critter_at)`. Each row
is drawn **completely** before the next.

Sprites are bottom-anchored and extend **upward** (`destination.y = p.y() + (tile_offset.y
− height_3d_val)·…`, `cata_tiles.cpp:4757`). Static tall sprites are safe (they grow into
already-drawn north rows). A creature's animation offset (`active_anim_xform_.off_y`,
applied at `cata_tiles.cpp:4769-4773`) is the **only** thing that makes a sprite reach
*downward* into the next (south) row — which is drawn **after**, so that row's terrain
paints over the sprite's feet. A north-bound slide's offset is intrinsically downward
(sprite travels from the southern origin up to the northern destination), so no
slide-only / draw-order-free fix exists.

Measured: only `gfx/HitButton_iso` (isometric) sets per-sprite `"height_3d"`. On the
standard top-down path `tile.height_3d == 0`, so terrain/furniture/items/vpart all paint
strictly **inside their own cell** — the only thing that ever crosses a cell boundary is
an animating creature.

## Industry-standard depth model (researched)

Every mainstream 2D engine uses the same two-part model — the "categories + granular
ordering" the user intuited:

1. **Coarse render-layer buckets** (fixed stacking, position-independent), bottom→top:
   `floor → ground features (furniture/items-on-floor) → DYNAMIC ENTITIES → overhead/canopy
   → UI`.
2. **Y-sort WITHIN the entity bucket by the sprite's FEET / base anchor (not center)**,
   per frame, back-to-front. Tall-sprite gotcha: sort by the feet. (Unity *Sort Point =
   Pivot*; Godot "offset sprite so position aligns with bottom"; GameMaker `depth = -y` —
   Stoneshard is GameMaker.) Multi-floor verticality = sort per-floor (≙ our z-levels).
   Only sort *dynamic* entities; static tiles keep fixed order.

Sources: [Unity 2D Renderer sorting](https://docs.unity3d.com/Manual/2d-renderer-sorting.html) ·
[Godot Node2D `y_sort_enabled`](https://docs.godotengine.org/en/stable/classes/class_node2d.html) ·
GameMaker `depth = -y` convention · RPG Maker/Tiled "above-player" overhead layer.

## Decisions (confirmed with user)

- **Sort key = feet anchor + animation offset** (base-y including the vertical slide/bob
  offset), not tile-row. Industry standard; correct entity-vs-entity compositing mid-slide.
- **Pragmatic scope:** build the buckets that change pixels now (ground-below + y-sorted
  entity bucket). **Reserve** an OVERHEAD/canopy category (documented, not implemented —
  `height_3d == 0` today makes it dead code).
- **Top-down only, unguarded** (no iso/`height_3d` fallback).

## Design

Map the existing monolithic passes onto explicit, ordered categories, drawn **per z**:

```
TERRAIN              (floor)                         — per-row, unchanged
MAP_FEATURES         (furniture, graffiti, trap)     — per-row, unchanged   [below entities]
── deferred, after all rows of this z ──
GROUND_ENTITIES      (field_or_item, vpart)          — row order            [below entities]
ENTITIES             (critter_at: creatures+player)  — Y-SORTED by feet     [the fix]
(OVERHEAD/canopy     — RESERVED, not built)
── after the whole z-loop ──
OVERLAYS             (zone_mark, zombie_revival)      — unchanged (3730-3733)
```

### Edit 1 — `src/cata_tiles.cpp`: relocate + categorize + feet-sort the entity bucket

Lift the `zlevel` bundle (`3707-3725`) **out of the per-row `while` loop** to run as a
deferred sweep **after** it, still inside `for z`. Restructure that deferred sweep into one
pass over the z-filtered `draw_points`, preserving the existing projected-vs-normal branch
(`3713-3723`) and per-layer `hide_unseen` gating verbatim:

1. **Inline, in row order** — draw `field_or_item` (gated) then `vpart` (ungated) for each
   tile. (Ground bucket, fully below entities.)
2. **Collect, don't draw** — for each tile, resolve the creature exactly as
   `draw_critter_at` does (`monster_override.find(p)` → else `g->critter_at({xy,z}, true)`,
   `cata_tiles.cpp:5764,5781`). If a real creature is present, compute its xform once
   (`compute_anim_xform`) to get `off_y`. Push a job:
   `{ tripoint{xy,z}, lit_level, &p.height_3d, invisible[5] (by value), z_drop=center.z()−z,
      creature*, xform, key }`.
   - **Feet sort key** = `player_to_screen(p.pos.xy()).y + tile_height  +  xform.off_y`
     (tile bottom = feet; `+off_y` shifts a downward-sliding sprite to sort south).
     `height_3d` is **excluded** (feet sit at the cell bottom). Override-path / no-creature
     jobs use `off_y = 0`.
3. **Sort** jobs with `std::stable_sort` by `key` ascending (stable → deterministic on
   ties, no flicker). `draw_points` is already ascending `screen_row` (proven:
   `for( int row = min_row; row < max_row; row++ )` at `3280` → `info.screen_row = row` →
   `push_back`, `3464-3465`), so collection order is already north→south; the sort only
   moves creatures whose `off_y` crosses a row.
4. **Draw** each job via `draw_critter_at`, back-to-front.

**Thread the resolution+xform once (do NOT double-call `compute_anim_xform` or
`critter_at`).** `compute_anim_xform → update_animation_state` mutates the hit ring queue
and seq-latch; it was built to run once per creature per frame, and calling it at both
collect and draw makes "twice, same `wall_now`, identical" a new unverified invariant —
plus it doubles the screen-wide `critter_at` lookup. Instead, thread the collected
`creature*` + `xform` into the draw via prefetch members:

- Add `const Creature *prefetch_critter_`, `sprite_xform prefetch_xform_`,
  `bool prefetch_valid_` to `cata_tiles`.
- Set them from the job immediately before each deferred `draw_critter_at` call.
- In `draw_critter_at`: when `prefetch_valid_`, **skip** the top wipe (`5756`), use
  `prefetch_critter_` instead of the `critter_at` lookup (`5781`), and set
  `active_anim_xform_ = prefetch_xform_` instead of `compute_anim_xform` (`5836`). Same in
  `draw_entity_with_overlays` (`6181`): use `prefetch_xform_` when valid. Clear
  `prefetch_valid_` after each draw.

**`height_3d` threading stays correct.** It is a per-tile by-ref accumulator (`p.height_3d`,
`tile_render_info`). Per tile the order stays terrain→base→ground→creature because *all*
terrain+base+ground for the z run before *any* creature draw; cross-tile order is
irrelevant (each tile owns its accumulator). Excluding `height_3d` from the sort key is
correct (feet, not head).

**Hoist** `const auto &ch = here.access_cache( z )` (currently per-row at `3707`) to once
per z in the relocated sweep.

### Edit 2 — `src/creature.cpp`: restore the vertical slide

In `update_animation_state` (`~189-213`), in the `if( s.move_slide )` branch replace
`s.slide_offset_y = 0.f;` with the symmetric form:

```cpp
s.slide_offset_y = -s.move_dir_y * remain;
```

Update the stale floor-clip comment (the y-sort fix makes downward offsets safe). Nothing
else changes — `compute_anim_xform` already folds `slide_offset_y * tile_height` into
`off_y` (`cata_tiles.cpp:3012-3040`).

## Optional follow-ups (NOT in v1 — same bug, user picks later)

The same draw-order constraint forced two other animations up-only; the y-sort fix makes
them safe to restore: **bob** (`-fabs(sin)` → full `sin()` dip-then-rise) and **idle
vertical** (`-|sway|·0.9` → symmetric). Also reserved: the **OVERHEAD/canopy** category
(needed only when a tileset sets `height_3d`).

## Verification

Build: `cmake --build out/build/osx-arm-slim --target cataclysm-bn-tiles`

1. **Reorder gate (terrain/ground only):** with nothing animating, **terrain + furniture +
   items + parked vehicle render pixel-identical before/after** (pure reorder, height_3d=0).
   Multi-z (stand at a ledge / look down a hole) — z-occlusion (floor above hides creature
   below) unchanged. FOV/unseen gating unchanged. *(Creatures are NOT in this gate — idle
   sway is continuous, so creature `off_y` is never zero and the sort key moves every
   frame even standing still.)*
2. **No creature z-flicker:** put **several creatures in a vertical column (adjacent rows,
   same screen x)** and watch them idle — their draw order must NOT flicker (idle wiggle
   ~1–2px can't flip creatures separated by ~`tile_height`; `stable_sort` keeps ties
   deterministic). This is the case a too-coarse key or unstable sort would expose.
3. **Slide (the fix):** move all 8 directions incl. diagonals — sprite slides smoothly into
   the destination, **no floor clip any direction** (north especially). Teleport → snaps
   (bob only), no slide.
4. **Symptoms:** tall monster moving vertically no longer clips; vertical attack-lunge no
   longer clips.
5. **Opt-out:** `SPRITE_ANIMATIONS` off → motion gone, rendering identical to gate (1).

## Risk

Touches the hottest in-game draw loop (z-levels, FOV, vehicle/item/creature layering) and
adds a small refactor to `draw_critter_at` / `draw_entity_with_overlays` (prefetch
threading). Mitigations: the terrain/ground path is a pure block relocation (gate 1 covers
it); the creature changes are isolated to the deferred pass + the prefetch branch. Preserve
`draw_critter_at`'s monster-override path, infrared/sees gating, ridden, facing, and
post-draw indicators (`5765-5882`) — the prefetch only short-circuits the creature lookup +
xform compute, nothing else. Safety valves: revert edit 1 (restores prior order), edit 2
(slide) independently revertible.

## Files

- `src/cata_tiles.cpp` — relocate/restructure the deferred entity pass in `draw()`
  (`~3684-3729`); prefetch branch in `draw_critter_at` (`~5752-5882`) and
  `draw_entity_with_overlays` (`~6181`).
- `src/cata_tiles.h` — `prefetch_critter_` / `prefetch_xform_` / `prefetch_valid_` members;
  the job struct for the collect/sort.
- `src/creature.cpp` — restore `slide_offset_y` in `update_animation_state` (`~209`).
