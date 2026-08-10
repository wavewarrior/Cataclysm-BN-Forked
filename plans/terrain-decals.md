# Terrain Decals — Large Multi-Tile Overlay System

## Goal

Break up visual monotony in large clusters of the same terrain type (e.g., 3×3 grass) by overlaying large organic-shaped decal sprites (17 grass variants, ~2×2 to ~4×5 tiles each) on top of terrain sprites but beneath entities. Inspired by Stoneshard's terrain decals.

## Design Decisions (Confirmed)

| Decision | Choice | Reasoning |
|----------|--------|-----------|
| Atlas strategy | (A) New dedicated atlas texture | Clean separation from blood/gibs splatmap; different blend mode, resolution, and purpose |
| Placement density | (A) Per-terrain-type in JSON | Granular control; `t_grass` ≠ `t_grass_long` |
| Invalidation | (A) Invalidate entire submap on terrain change | Simple, correct; submap is 12×12 = 144 tiles, cheap to recompute |
| Seasonal variants | (A) Single grass group for V1 | Ship the mechanic, iterate later |

## Architecture

### Draw Order (Extended)

```
Pass W-a:  Terrain sprites → Furniture → Graffiti → Traps
           ↓
           Splatmap composite (blood/gibs — existing, unchanged)
           ↓
NEW:       Terrain Decals (tile_batcher, alpha blend, scissored to map viewport)
           ↓
Pass W-b:  Fields → Items → Vehicle Parts → Creatures → Zone marks
```

Terrain decals are drawn as regular sprites through the existing `tile_batcher`,
in their own pass between the splatmap composite and the entity half. They
participate in the full lighting pipeline (`is_lit = true`) so they respond to
ambient, emitters, sun, and GI.

> **Deviation from the original design.** The plan first placed decals *before*
> the splatmap composite. They ended up *after* it: decals are a terrain-detail
> layer, and blood/gibs should stain over them rather than be hidden beneath
> them. Entities still occlude decals, which is what matters.

### Component Overview

```
src/
├── terrain_decals.h              ← New: API header
├── terrain_decals.cpp            ← New: atlas load, placement, render
│
├── mapdata.cpp                   ← Modified: parse "decals" from terrain JSON
├── mapdata.h                     ← Modified: ter_t gains decal config
│
├── submap.h                      ← Modified: invalidate decal cache on ter_set
├── submap.cpp                    ← Modified: call terrain_decals::invalidate()
│
├── lighting/render_state.h       ← Modified: expose terrain_decals accessor
├── lighting/render_state.cpp     ← Modified: init/shutdown terrain_decals
│
├── sdl_render_frame.cpp          ← Modified: render decals between W-a and splatmap
│
└── CMakeLists.txt                ← Modified: add new sources

gfx/
└── terrain_decals/
    ├── manifest.json             ← New: atlas config
    └── grass_01.png ... grass_17.png  ← New: decal sprites

data/json/furniture_and_terrain/
    └── terrain-flora.json        ← Modified: add "decals" to grass terrain types
```

## Detailed Design

### 1. Atlas Loading (`terrain_decals.h/cpp`)

New `terrain_decals` class in `src/terrain_decals.h`, following the splatmap_pass pattern but simpler — no per-submap textures, just a sprite atlas + placement cache.

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "coordinates.h"

struct SDL_GPUTexture;

/// One placed decal, in submap-local tile coordinates.
struct terrain_decal_instance {
    float tx = 0.f, ty = 0.f;   // centre in submap-local tile coords (0..12)
    float rotation = 0.f;        // radians
    int variant = 0;             // atlas variant index (0..N-1)
};

/// Per-terrain-type decal configuration (parsed from JSON).
struct terrain_decal_config {
    std::string group;           // atlas group name (e.g. "grass")
    float density = 0.12f;       // probability per eligible tile (0..1)
    int min_spacing = 2;         // minimum tiles between decal centres
};

namespace terrain_decals
{
/// Load the decal atlas from gfx/terrain_decals/manifest.json.
/// Returns true on success. Called from render_state::init().
auto init( SDL_GPUTexture **out_atlas, int *out_variants ) -> bool;

/// Shut down GPU resources. Called from render_state::shutdown().
auto shutdown( SDL_GPUTexture *atlas ) -> void;

/// Whether the atlas loaded successfully.
auto ready() -> bool;

/// Number of variants in the active group.
auto variant_count() const noexcept -> int;

/// Atlas texture handle (for sprite_batcher binding).
auto atlas() const noexcept -> SDL_GPUTexture *;

/// Compute decal placements for a submap based on current terrain layout.
/// Deterministic from absolute submap coordinates. Result cached per key.
/// `ter_at` callback returns ter_id for submap-local tile (x,y).
auto compute_placements( std::uint64_t key,
                         const std::function<ter_id(int,int)> &ter_at ) -> const std::vector<terrain_decal_instance> &;

/// Invalidate cached placements for a submap (called on terrain change).
auto invalidate( std::uint64_t key ) -> void;

/// Clear all caches (e.g. on dimension change).
auto clear() -> void;

} // namespace terrain_decals
```

Atlas loading mirrors `splatmap_pass::load_atlas()`:
- Reads `gfx/terrain_decals/manifest.json` for `cell_size` and group file lists
- Packs sprites into a single RGBA8 GPU texture using nearest-neighbor scaling
- Unlike splatmap's fixed 8×8 grid, this atlas is a simple strip or grid sized to fit the variants
- Each variant retains its natural aspect ratio; UVs are computed from packed positions

**Atlas format** — flexible packing (not fixed cells, since decals vary in size):
```json
{
  "groups": {
    "grass": [
      "grass_01.png", "grass_02.png", ..., "grass_17.png"
    ]
  }
}
```

Each loaded sprite stores: `{ u, v, uw, vh, width_px, height_px }` for UV + size lookup.

### 2. Terrain JSON Configuration

Extend terrain definitions with optional `"decals"` property:

```json
{
  "type": "terrain",
  "id": "t_grass",
  "decals": {
    "group": "grass",
    "density": 0.12,
    "min_spacing": 2
  },
  "...": "..."
}
```

Parse in `mapdata.cpp` / `ter_t`:
- Add `std::optional<terrain_decal_config> decal_config` to `ter_t`
- Parse in `ter_t::load()` or wherever terrain JSON members are read
- Missing `"decals"` → no decals for this terrain (opt-in)

**Density tuning guidelines:**
- `t_grass`: 0.12 (one decal per ~8 tiles)
- `t_grass_long`: 0.08 (longer grass, fewer decals needed)
- `t_grass_tall`: 0.06 (tall grass already visually busy)
- `t_moss`: 0.10 (small moss patches benefit from detail)
- `t_dirt`, `t_sand`: 0.0 (no decals unless we add dirt/sand decal groups)

### 3. Placement Algorithm

Deterministic, coordinate-seeded, per-submap. Same world always looks the same.

```
For each submap (12×12 tiles):
  1. Seed RNG from absolute submap coordinates (same pattern as splatmap)
  2. Shuffle tile order deterministically
  3. For each tile in shuffled order:
     a. Look up terrain type's decal_config
     b. Roll density: if random() > density, skip
     c. Check min_spacing: if any existing decal within min_spacing tiles, skip
     d. Place decal:
        - Centre: tile + random offset (-0.3..+0.3 tiles each axis)
        - Rotation: random 0..2π
        - Variant: random from group's variant pool
        - Add to placement list
  4. Cache result keyed by submap key
```

Key properties:
- **Deterministic**: Same absolute coords → same seed → same placements
- **Save/load stable**: World state determines terrain → terrain determines decals
- **Co-op safe**: Uses file-local `std::mt19937`, not game's `rng()`
- **O(N²) but tiny**: 144 tiles × max ~20 decals per submap = negligible

### 4. Invalidation

Call `terrain_decals::invalidate(submap_key)` from:
- `submap::set_ter()` — single tile terrain change
- `submap::set_all_ter()` — bulk terrain change
- `map::translate()` — terrain type translation
- `map::make_rubble()` — bash results

Pattern (in `submap.cpp`):
```cpp
void submap::set_ter( const point_sm_ms& p, ter_id terr )
{
    // ... existing code ...
    
    // Invalidate decal cache for this submap
    terrain_decals::invalidate( splatmap::key_of( pos ) );
}
```

Include `terrain_decals.h` in `submap.cpp` (not `.h` to avoid circular deps).

### 5. Rendering Integration

#### `sdl_render_frame.cpp` — Insert Between Pass W-a and Splatmap

Current flow (around line 960-970):
```cpp
// Pass W-a: terrain half
rs.flush_tile_sprites( rs.tile_batcher(), ..., 0, cut );
rs.tile_batcher().end_pass();

// Splatmap composite
rs.splatmap().composite( ... );

// Pass W-b: entity half
rs.flush_tile_sprites( rs.tile_batcher(), ..., cut, queued );
```

New flow:
```cpp
// Pass W-a: terrain half
rs.flush_tile_sprites( rs.tile_batcher(), ..., 0, cut );
rs.tile_batcher().end_pass();

// NEW: Terrain decals (over terrain, under splatmap)
render_terrain_decals( ctx, rs, proj_w, proj_h );

// Splatmap composite (unchanged)
rs.splatmap().composite( ... );

// Pass W-b: entity half (unchanged)
rs.flush_tile_sprites( rs.tile_batcher(), ..., cut, queued );
```

#### `render_terrain_decals()` Function

```cpp
static auto render_terrain_decals( render_context &ctx,
                                   lighting::render_state &rs,
                                   std::uint32_t proj_w, std::uint32_t proj_h ) -> void
{
    if( !terrain_decals::ready() || !splatmap::active() ) {
        return;
    }

    // Collect visible submaps' decal placements
    // For each visible submap quad in rs.splat_quads():
    //   - Compute placements if not cached
    //   - Convert each placement to a sprite_instance
    //   - Queue via rs.queue_tile_sprite(atlas, instance)
    
    // Sprite instances use:
    //   - dst_*: projected pixel coords (via player_to_screen)
    //   - src_*: UV from atlas variant
    //   - tint_: white (1,1,1,1) — full color from sprite
    //   - light_mode: gpu_lit (1.0f) — participates in lighting
    //   - rotation: from placement
    
    // Flush decals through tile_batcher in a brief pass
    // (LOADOP_LOAD to preserve terrain underneath)
}
```

The decals use the `tile_batcher` with `gpu_lit` light mode so they:
- Respond to ambient, emitters, sun, GI
- Are affected by the same tone grading and vision effects
- Blend naturally with the lit terrain beneath

### 6. Render State Integration

Add to `render_state`:
```cpp
// terrain_decals.h
#include "terrain_decals.h"

// In render_state class:
terrain_decals::manager& decals() noexcept { return decals_; }

// Member:
terrain_decals::manager decals_;
```

Init in `render_state::init()`:
```cpp
if( !decals_.init() ) {
    dbg() << "terrain_decals: atlas load failed (cosmetic, continuing)";
}
```

Shutdown in `render_state::shutdown()`:
```cpp
decals_.shutdown();
```

### 7. Asset Pipeline

#### `gfx/terrain_decals/manifest.json`
```json
{
  "tile_size": 32,
  "groups": {
    "grass": [
      "grass_01.png", "grass_02.png", "grass_03.png", "grass_04.png",
      "grass_05.png", "grass_06.png", "grass_07.png", "grass_08.png",
      "grass_09.png", "grass_10.png", "grass_11.png", "grass_12.png",
      "grass_13.png", "grass_14.png", "grass_15.png", "grass_16.png",
      "grass_17.png"
    ]
  }
}
```

`tile_size` is the tile size the art was drawn against, and it is what makes the
sprites resolution- and zoom-independent: a 64 px sprite at `"tile_size": 32`
covers 2×2 tiles, and keeps covering 2×2 tiles at any zoom or display scale.

#### Sprite Requirements
- PNG with alpha channel (transparent background)
- Natural size (no fixed cell size — each sprite keeps its dimensions)
- Organic edges with soft alpha falloff
- Dark/neutral background pixels fully transparent (alpha = 0)
- Sized as a whole multiple of `tile_size` where practical, so the footprint
  lands on tile boundaries

Place 17 PNG files in `gfx/terrain_decals/`. Sprites are packed left-to-right into
a single-row power-of-two atlas at load; a missing or unreadable file is skipped
with a warning, and an empty group disables the feature rather than failing.

### 8. CMakeLists.txt

Add to `src/CMakeLists.txt` source list:
```cmake
terrain_decals.cpp
```

## Implementation Status — BUILT

| Phase | Status | Notes |
|-------|--------|-------|
| 1. Core infrastructure | Done | `terrain_decals.h/.cpp`, manifest, auto-globbed by CMake |
| 2. Terrain JSON | Done | `decal_config` on `ter_t`; 4 terrains configured |
| 3. Placement & invalidation | Done | Deterministic per-submap cache; `invalidate()` on `set_ter` |
| 4. Rendering | Done | Own pass after splatmap composite, scissored, visibility-gated |
| 5. Density tuning | Done | 0.02–0.045 by terrain, `min_spacing` 3–4 |
| 5. Dev toggle | **Not done** | Deferred; `ready()` already gates on atlas presence |
| 5. Co-op desync test | **Not done** | Placement uses a file-local `mt19937`, never `rng()`, so by construction it cannot perturb the shared stream — unverified at runtime |

### Assets: real Stoneshard art (16 sprites)

`gfx/terrain_decals/grass_01..16.png` are Stoneshard's `s_maingrassprite_01..016`,
copied verbatim (no re-encode) from the user's extraction. `"tile_size": 64` is the
size the art was drawn against, which puts the footprints at:

| | footprint @ ts=64 |
|---|---|
| Largest (`grass_01`, 205×197) | 3.20 × 3.08 tiles |
| Median | 1.53 × 1.05 tiles |
| Smallest (`grass_10`, 60×34) | 0.94 × 0.53 tiles |

The largest sprite covering ~3×3 tiles is what makes this land on the original
brief — one decal breaks up a 3×3 grass cluster — while the smaller tufts fill in
without repeating.

All 16 pack into a single 2048×256 atlas row (2 MB VRAM), confirmed at load:
`terrain_decals: atlas loaded (16 variants, 2048x256)`.

To swap in different art: overwrite the PNGs (keep the filenames, or edit the
manifest list) and set `"tile_size"` to whatever that art was drawn against. The
loader skips unreadable files with a warning and disables the feature on an empty
group rather than failing.

#### Rotation must stay small for directional art

Placement originally applied `uniform(0, 2π)` rotation, which was invisible with
radially-symmetric placeholder blobs and *wrong* the moment real art landed:
foliage is drawn with a definite "up" — blades rise, the base sits on the ground —
so a freely rotated tuft reads as upside-down grass. Rotation is now a ±0.12 rad
(~±7°) lean, which breaks repetition without fighting the art.

### Two bugs found only by running the game

Both were invisible to the compiler and to code reading; each needed a pixel
measurement to find and to prove fixed.

**1. Decals painted over the sidebar and the letterbox.** A decal is a multi-tile
sprite that can overhang the map viewport, so the pass needs a scissor. The first
scissor attempt made it *worse*: `cata_tiles` records the viewport in **logical
projection points**, but `SDL_SetGPUScissor` takes **target texture pixels**. On a
HiDPI display those differ by the backing scale, so the rect landed at ~1/2.8
size in the top-left — simultaneously clipping the real map and letting decals
leak left. Fixed by scaling the rect by `wt->width() / proj_w`.

**2. Decals floated on unexplored black space.** `draw_points` spans the whole
viewport, not just visible tiles — visibility is carried per-tile in
`invisible[0]`, with terrain falling back to memory (or to nothing). Gating decal
placement on `!invisible[0]` fixed it:

| Region | greenish px before | after | pixel delta |
|---|---|---|---|
| Unexplored (left of lit area) | 6240 (3.90%) | 398 (0.25%) | 12612 changed, max 104 |
| Visible map | 126586 (70.33%) | 126586 (70.33%) | **0 changed, max 0** |

94% of the bleed removed with the visible map left byte-identical.

#### Confirmed again against the real art

The measurement above used the high-contrast placeholders. The real art is subtle
enough that a colour threshold cannot discriminate it from grass terrain, so the
final check was a true A/B: same save, same frame, atlas present vs renamed away
(no rebuild — `ready()` gates on the atlas).

| Region | changed px | max delta |
|---|---|---|
| Map interior | 7.40% | 394 |
| **Strictly left of map viewport** | **0.00%** | **0** |
| Below map (message log) | 0.16% | 179 |
| Right of map (sidebar) | 0.07% | 161 |

19 distinct decal patches covering 7.8% of the visible map, and the region left of
the viewport is byte-identical — the scissor and the visibility gate both hold.
The handful of residual pixels below/right of the map are scattered singletons
across a 225×150 area (HUD frame animation), not the contiguous ~1000 px blob a
leaked decal would produce.

### Sizing is tile-relative, not pixel-absolute

`decal_variant_info` stores footprint in **tiles** (`w_tiles`/`h_tiles` =
`sprite_px / manifest tile_size`), and `cata_tiles` multiplies by the live
`tile_width`. Storing raw pixels would have pinned decal size to the display
scale and ignored zoom.

At default zoom this is provably a no-op — `tile_width == tile_size == 32`, and
the before/after frames are byte-identical (blob count, area, and green-pixel
count all ratio exactly 1.000). Zoom is where it matters:

| Metric | zoom 1 | zoom 2 | scale |
|---|---|---|---|
| Tile grid period (autocorrelation) | 23.0 px | 45.5 px | 1.978× |
| Largest decal (equiv. diameter) | 56.1 px | 109.0 px | 1.942× |
| **Footprint in tiles** | **2.44** | **2.39** | **2% drift** |

Measured on the largest unclipped blob — the only comparable sample, since a
different *set* of decals is on screen at each zoom, which makes mean/percentile
comparisons across the two frames invalid.

### Architecture change: per-frame draw list

The plan had `sdl_render_frame` walk `splat_quads` and read the placement cache
directly. As built, `cata_tiles::record_splat_frame` produces a
`std::vector<terrain_decal_draw>` of already-resolved screen rects, and the render
pass just iterates it. This was forced by the visibility bug — only `cata_tiles`
knows `invisible[0]` — and it pays off twice more: positions now go through the
same `player_to_screen` projection as the tile sprites (so they cannot drift from
terrain), and the size scaling has access to the live `tile_width`.
`manager::placements_for()` existed only for the old path and was removed.

## Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Decal sprites too large for atlas | VRAM pressure | 17 sprites × ~128×160px ≈ 500KB uncompressed; negligible |
| Placement looks unnatural | Visual quality | Tune density/min_spacing per terrain; add rejection for tiles adjacent to walls |
| Performance on dense submaps | Frame time | Max ~20 decals/submap × ~10 visible submaps = ~200 sprites; batcher handles thousands |
| Save/load instability | Wrong visuals | Deterministic seeding from absolute coords; no game RNG involved |
| Co-op desync | Different worlds | File-local mt19937 seeded from coords; same as splatmap pattern |

## Deferred (V2)

- Autumn/winter decal groups with weather-driven switching
- Dirt/sand decal groups for non-grass terrain
- Height-aware placement (decals avoid stairs, ramps)
- Player proximity culling (don't compute placements for distant submaps)
- Mod support: custom decal groups via mod `gfx/` directories

## Notes

- **No per-tile storage**: Placements are recomputed from terrain layout + seed, cached per submap. Zero save format changes.
- **No gameplay effect**: Purely cosmetic. Decals do not affect pathfinding, movement, collision, or visibility.
- **Follows existing patterns**: Atlas loading mirrors `splatmap_pass`; placement seeding mirrors `splatmap_stamps`; render integration mirrors splatmap composite insertion.