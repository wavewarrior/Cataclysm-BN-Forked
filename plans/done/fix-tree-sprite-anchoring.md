# Fix tree sprite anchoring and canopy overpaint (stoneshard trees)

## Problem

In the MSX++UnDeadPeopleEdition theme, trees (mostly the promoted Stoneshard trees) render **2x too large vs the theme's scale, clipped, and sprawled into right/below tiles**: the trunk is buried in the ground of the tiles below and the canopy is chopped at the tile edges.

## Root cause (verified)

`tile_config.json` tile entries inherit their draw offset from the **file section** they sit in (`load_tilejson_from_file`, `src/cata_tiles_tileset.cpp:1849` - `curr_tile.offset = sprite_offset`), while their `fg`/`bg` indices are **global atlas indices** that can resolve into any sheet. Twelve entries in the `large_terrain.png` section (64x80 cells, offset -16/-48) reference **128x160** cells:

- `t_tree`, `t_tree_willow`, `t_tree_birch`, `t_tree_dead` + each `_season_winter` variant -> `stoneshard_trees.png` (128x160, correct anchor -48/-128): sprites 23880-23898
- `t_shrub`, `t_underbrush` + winter variants -> `stoneshard_foliage.png` (128x160): sprites 19752-19781

Result: 128x160 art drawn with the 64x80 offset -> shifted 1.5 tiles right and 2.5 tiles low. All three symptoms come from this one mismatch. Same bug class found elsewhere: 8 `vac_oven_small*` item entries (64x80 art, 0/0 offset), `candlestick`/`press` (32x32 bg shadow drawn with -16/-48, floats 48px above the floor), and a dead duplicate `manhole_cover` entry (the later `wide.png` definition wins - no visual bug, config clutter).

A second, independent defect: the engine draws terrain left-to-right, top-to-bottom (`src/cata_tiles.cpp:796-810` row/col loop; per-row terrain then base layers at 1282-1310), so a wide canopy's overhang into not-yet-drawn tiles is **overpainted** by adjacent ground/furniture. A correctly anchored 4x5-tile canopy would lose its whole bottom-right quadrant.

## Confirmed decisions (user)

1. Trees display at **native 128x160** (4x5-tile canopy, full pixel fidelity), centered on their own tile, base on the tile's bottom edge - **plus** an engine deferred-canopy pass so the overhang is not overpainted.
2. **Fix all** cross-sheet mismatches (trees/foliage + the other sprites).

## Implementation

### 0. Plan files (AGENTS.md mandate)

Write this plan to `plans/fix-tree-sprite-anchoring.md` and `local://fix-tree-sprite-anchoring.md` as the first action.

### 1. Loader: per-layer sprite offsets (code)

**`src/cata_tiles.h`** - `tile_type` (line 66), after `offset_retracted` (line 86):

    std::optional<point> fg_offset; ///< per-entry override for the fg layer; nullopt = use offset
    std::optional<point> bg_offset; ///< per-entry override for the bg layer; nullopt = use offset

`std::optional` needs no maintenance at the ~5 `tile_type` construction sites (nullopt means fall back to `offset`).

**`src/cata_tiles_tileset.cpp`** - in `load_tilejson_from_file` (after line 1850, the `curr_tile.pixelscale` assignment):

    if( entry.has_int( "fg_offset_x" ) || entry.has_int( "fg_offset_y" ) ) {
        curr_tile.fg_offset = point( entry.get_int( "fg_offset_x", sprite_offset.x ),
                                     entry.get_int( "fg_offset_y", sprite_offset.y ) );
    }
    if( entry.has_int( "bg_offset_x" ) || entry.has_int( "bg_offset_y" ) ) {
        curr_tile.bg_offset = point( entry.get_int( "bg_offset_x", sprite_offset.x ),
                                     entry.get_int( "bg_offset_y", sprite_offset.y ) );
    }

**`src/cata_tiles.cpp`** - `draw_sprite_at` (line 2266): each call is one layer, so replace the single `tile_offset` usage (lines 2360-2369) with a per-layer value:

    const point layer_offset =
        ( is_fg ? tile.fg_offset : tile.bg_offset ).value_or( tile.offset );

Use `layer_offset` in the destination math (2384-2394) and in the `get_or_default(...)` call (2368). Per-layer overrides do not participate in the retract interpolation (document with a comment; trees/items never use retract).

### 2. Theme config: anchor the 12 tree/foliage entries

In `gfx/MSX++UnDeadPeopleEdition/tile_config.json`, `large_terrain.png` section, add to each of the 12 entries: "fg_offset_x": -48, "fg_offset_y": -128.

Ids: t_tree, t_tree_season_winter, t_tree_birch, t_tree_birch_season_winter, t_tree_dead, t_tree_dead_season_winter, t_tree_willow, t_tree_willow_season_winter, t_shrub, t_shrub_season_winter, t_underbrush, t_underbrush_season_winter. Their bg (64x80 dirt 18113/18123) keeps the section offset -16/-48 - the established ground-bleed look of the theme's 64x80 trees. Keep "rotates": false everywhere. Update the section // comment to document the per-layer offsets.

### 3. Repack stoneshard_foliage.png (centered + bottom-aligned)

The trees PNG was already repacked centered+bottom-aligned (uncommitted, Aug 21) - keep it; re-verify residuals (cx_off <= 0.5px, base_gap = 0) before committing. The foliage PNG is **not** repacked: cells are top-left-pasted (bboxes start at x 0-34, y 0-35; bases at y 124-159), so under -48/-128 the bushes would float. Repack with the same transform as the trees: for each of the 32 cells (128x160 grid, 1024x640 - keep grid and total size so **no global index shifts**): clear cell, alpha-bbox the art, paste at x=(128-w)/2, y=160-h. Full-bleed cells (bbox = whole cell) are unchanged. One-off Python/PIL script, run, discard; verify: 32 non-empty cells, base_gap = 0 for all, cx_off <= 0.5px (full-bleed cells exempt from centering), sheet still 1024x640.

### 4. Other cross-sheet sprites (config-only, same mechanism)

In the same tile_config.json:

- 8 entries in the normal_items.png section - vac_oven_small, vac_oven_small_done, vac_oven_small_full, vac_oven_small_on, overlay_wielded_vac_oven_small{,_done,_full,_on} - add "fg_offset_x": -16, "fg_offset_y": -48 (their 64x80 art in large_stuff.png).
- candlestick, press (in the large_stuff.png section) - add "bg_offset_x": 0, "bg_offset_y": 0 (their bg 9172 is a 32x32 shadow that currently floats 48px above the floor).
- Delete the dead duplicate manhole_cover entry from the normal_items.png section (the wide.png section definition at -16/-16 is the effective one; create_tile_type overwrites, verified at src/cata_tiles.cpp:437).
- Do **not** touch the t_stoneshard_tree_* / t_stoneshard_shrub_* entries (unused by any terrain, but valid and in the right section).

After edits run style-json-parallel and re-run the audit script to zero mismatches.

### 5. Engine: deferred terrain-canopy pass

Goal: terrain fg sprites whose destination overhangs their own tile square are redrawn once, after all pass-1 rows for that z, so the overhang lands on top of adjacent ground/furniture but **under** entities (pass 2/3) and before the splatmap boundary count.

**New record type** - src/cata_tiles_internal.h:

    struct canopy_defer_record {
        const tile_type* tile = nullptr;
        point_bub_ms screen_pos{};
        unsigned int loc_rand = 0;
        int rota = 0;
        tint_config fg_tint;
        lit_level ll = lit_level::LIT;
        bool apply_visual_effects = false;
        int overlay_count = 0;
        int height_3d = 0;
        int retract = 0;
        float sway = 0.0f;
        float face_amt = 0.0f;
        // snapshot of per-tile GPU state set by draw_from_id_string (lines 2060-2134)
        sprite_light_mode light_mode = sprite_light_mode::unlit;
        float light_mul = 0.0f;
        int frontier_mask = -1;
        float outline = 0.0f;
        sprite_xform xform{};
    };

**New members** - src/cata_tiles.h near active_anim_xform_ (line 1514):

    std::vector<canopy_defer_record> canopy_defers_;
    bool canopy_capture_ = false;
    bool canopy_replay_ = false;
    TILE_CATEGORY canopy_capture_category_ = C_NONE;

**Capture** - in draw_from_id_string (line 2137, before the draw_tile_at call) set canopy_capture_category_ = tile.category. In draw_sprite_at, after destination incl. the anim xform (line ~2418) and before the render lambda, capture when:

    canopy_capture_ && !canopy_replay_ && is_fg && overlay_count == 0
    && canopy_capture_category_ == C_TERRAIN

and the rect overhangs the tile square in any direction:

    destination.x < p.x() || destination.y < p.y()
    || destination.x + destination.w > p.x() + tile_width
    || destination.y + destination.h > p.y() + tile_height

Record all draw_sprite_at params (so the replay call is byte-identical) plus the state snapshot {gpu_light_mode, gpu_light_mul, low_frontier_mask_, vision_overlay_outline_, active_anim_xform_}.
Extract the overhang test into a small pure helper auto overhangs_tile( const SDL_Rect& dst, int tile_x, int tile_y, int tw, int th ) -> bool; for unit testing.

**Replay** - in draw() (src/cata_tiles.cpp), per z iteration: enable canopy_capture_ = true; and canopy_defers_.clear(); before the row while-loop (~1282); after the row loop and **before** the splatmap-cut record_splat_frame (1331):

    if( !canopy_defers_.empty() ) {
        canopy_capture_ = false;
        canopy_replay_ = true;
        for( const auto& d : canopy_defers_ ) {
            int h3d = d.height_3d;
            gpu_light_mode = d.light_mode;
            gpu_light_r = gpu_light_g = gpu_light_b = 1.0f;
            gpu_light_mul = d.light_mul;
            low_frontier_mask_ = d.frontier_mask;
            vision_overlay_outline_ = d.outline;
            active_anim_xform_ = d.xform;
            draw_sprite_at( *d.tile, d.screen_pos, d.loc_rand, /*is_fg:*/ true, d.rota, d.fg_tint,
                            d.ll, d.apply_visual_effects, d.overlay_count, &h3d, d.retract,
                            TILESET_NO_WARP, d.sway, d.face_amt );
        }
        canopy_replay_ = false;
        canopy_defers_.clear();
        canopy_capture_ = true;
    }

Disable capture after the z loop (the later memorized-tile path at 1605 draws after everything and needs no deferral).

Why this is safe (each verified against the current code):

- Queue order = draw order (no sorting in the sprite batcher); replay enqueues after pass-1 sprites, before entities -> canopy over adjacent tiles, entities over canopy. Same z-ordering convention as vanilla CBN.
- Splatmap boundary (tile_sprite_count() at 1334) is captured **after** the replay, so the decal seam stays between terrain and entities.
- No double occluder capture: occluder_capture_ is set by a guard scoped to each draw_terrain call, out of scope at replay time.
- No double memorize/anim-mark: those live in draw_terrain/draw_from_id_string, not draw_sprite_at.
- No double z-overlay: capture requires overlay_count == 0.
- Deterministic replay: same loc_rand/rota means same sprite pick; sway/face are passed explicitly; idle-anim frame is stable within the frame; active_warp_hash is unchanged between pass 1 and replay.
- Terrain-only gate (C_TERRAIN) keeps item/furniture z-ordering exactly as today.
- Cost: one extra enqueue per overhanging terrain fg (trees/big shrubs only; dozens per frame vs the 262144 instance cap).

### 6. Tests

1. **tests/tileset_config_audit_test.cpp** (new, tag [tileset]) - renderer-free regression guard:
   - Parse gfx/MSX++UnDeadPeopleEdition/tile_config.json (CWD-relative, SKIP guard if absent - same pattern as tests/ui_tween_test.cpp:171).
   - Recompute each file's cell range from the PNG header (width/height divided by section sprite_width/sprite_height).
   - Assert: every fg/bg index is in-range; for every tile entry, each referenced sprite's owning section has the **same cell size and offset as the entry's effective per-layer offset** (section offset, or the fg_offset_x/y / bg_offset_x/y overrides). This is exactly the invariant that was broken; it fails on the pre-fix tree entries and passes after.
   - Spot-asserts: t_tree effective fg offset == (-48,-128); t_shrub bg offset == (-16,-48); vac_oven_small fg offset == (-16,-48).
2. **Unit test** for the overhangs_tile helper (pure function): inside/touching/outside the tile square, wide sprite, negative offsets. Tag [tileset].
3. Optional, if the software-renderer route works headless (test_main already does SDL_Init(VIDEO)): a loader test that seeds TILESETS (src/options.h:361) with a temp dir (tiny PNG + config using the new keys) and asserts the parsed tile_type::fg_offset/bg_offset. SKIP guard if renderer creation fails; otherwise the in-game check below covers the wiring.

### 7. Build, verify, commit

- Format: format target for C++, style-json-parallel for JSON.
- Build (background job, 1200s+, osx-arm-slim per AGENTS rules): targets cataclysm-bn-tiles and cata_test-tiles.
- Run ./cata_test-tiles "[tileset]" then the broader ~[.] suite from the repo root (binary is at the repo root on this preset - check mtime first per AGENTS).
- In-game visual pass (user or display session): spawn in a forest, verify the acceptance list below.
- Atomic commits (file-scoped so the unrelated uncommitted src/compute/gpu_lm.cpp and src/map_cache.cpp edits stay out):
  1. feat(tileset): per-layer sprite offsets for tileset entries
  2. fix(tileset): anchor stoneshard trees and foliage on their own tiles (foliage PNG repack + config offsets + the already-uncommitted trees repack/headers)
  3. fix(tileset): anchor cross-sheet item and furniture sprites
  4. feat(render): deferred canopy pass for overhanging terrain sprites
  5. test(tileset): guard against cross-sheet sprite offset mismatches

## Acceptance criteria

1. t_tree + variants: canopy centered on the tree tile, trunk base on the tile's bottom edge, full 4x5 canopy visible - no bottom truncation, no right-side overpaint, no floating.
2. t_shrub / t_underbrush: bushes sit on the ground, centered.
3. vac_oven_small* grounded and centered; candlestick/press shadow on the floor; manhole unchanged.
4. Audit script + [tileset] tests report zero mismatches; full test suite green; cataclysm-bn-tiles builds.
5. Trees adjacent to fences/cars/trees: canopy overhangs on top of them; player standing beside a tree remains in front of the overhang.

## Edge cases & failure modes

- Entries without the new keys are byte-identical in behavior (optional fallback) - no regression for other themes or sheets.
- Replayed fg uses the same loc_rand, so the same sprite variant, including seasonal looks_like resolution (the recorded tile_type pointer is the already-resolved variant).
- Multitile terrain (e.g. t_ice) and rotated tiles replay with recorded rota/loc_rand - identical pick.
- static_z_effect overlays and MEMORIZED dimming replay correctly via the state snapshot (light_mul/frontier/outline).
- Foliage repack must keep 1024x640 / 32 cells (index stability); verify sheet dims before committing.
- If headless renderer creation fails, the optional loader test skips; wiring is covered by the in-game check.

## Assumptions / out of scope

- Theme tile size stays 32x32 (tile_info); offsets are tileset-px and scale at render time (existing behavior).
- The uncommitted trees-PNG repack + (-48,-128) headers in the working tree are the intended base state; they get committed in step 2's commit.
- Uncommitted src/compute/gpu_lm.cpp (main0) and src/map_cache.cpp (seen-cache flags) edits are from another session - left untouched.
- No art redraw, no downscaling; the 2px-bleed dirt ground under trees keeps the theme's existing look.
