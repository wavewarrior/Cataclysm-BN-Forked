# Camera System Modernization Plan

## Context

The current "camera" is ad-hoc math distributed across `avatar::view_offset`, `game::driving_view_offset`, `cata_tiles::o`/`op`, and inline `player_to_screen()` conversions. The lighting/render pipeline is surprisingly modern (GPU compute GI, SDF shadows, instance-batched sprites, bloom, tonemap), but the camera/viewport layer is fundamentally 1990s tile-scrolling. This plan modernizes the camera in 6 phases.

### Design decisions

| Question | Decision |
|----------|----------|
| File location | `plans/camera_modernization.md` |
| Zoom / Smooth follow default | **On by default**, tunable via F4 dev panel debug params |
| Minimap camera | **Replace old pixel minimap entirely**; separate look/scale from main camera |
| Dead zone radius | **Configurable** — game option + F4 override |

---

## Phase 1: `camera_2d` class (pure refactor, no behavior change)

**Goal**: single source of truth for viewport state.

### 1.1 Define `camera_2d`

New files `src/camera_2d.h` and `src/camera_2d.cpp`:

```cpp
class camera_2d {
    point_bub_ms center_;
    point viewport_px_;      // pixel offset of draw area (sidebar width)
    int tile_width_ = 32;
    int tile_height_ = 32;
    float zoom_ = 1.0f;      // Phase 2.5
    float rotation_ = 0.0f;  // reserved for future

    // Phase 2 fields
    float smooth_follow_speed_ = 8.0f;
    float look_ahead_tiles_ = 3.0f;
    int dead_zone_radius_ = 2;
    float shake_intensity_ = 0.0f;
    float shake_decay_ = 0.9f;

public:
    void set_center( point_bub_ms p );
    void set_viewport_px( point p );
    void set_viewport_size( int w, int h );
    void update( point_bub_ms target, tripoint_rel_ms velocity, float dt );

    [[nodiscard]] auto world_to_screen( tripoint_bub_ms ) const -> point;
    [[nodiscard]] auto screen_to_world( point ) const -> tripoint_bub_ms;
    [[nodiscard]] auto visible_tile_rect() const -> half_open_rectangle<point>;
    [[nodiscard]] auto get_view_matrix() const -> std::array<float, 6>;
    [[nodiscard]] auto get_tile_origin() const -> point;   // replaces cata_tiles::o
    [[nodiscard]] auto get_pixel_offset() const -> point;  // replaces cata_tiles::op

    // Phase 2 API
    void shake( float intensity, float duration );
    void nudge( tripoint_rel_ms delta );
    void reset_follow();
};
```

### 1.2 Merge existing offsets

- Move `avatar::view_offset` → `camera_2d` managed via `nudge()` / `reset_follow()`
- Move `game::driving_view_offset` → `camera_2d::update()` handles driving as velocity input
- `game::calc_driving_offset()` → `camera_2d` internal smoothing

### 1.3 Replace inline math

| Location | Replace with |
|----------|--------------|
| `cata_tiles::draw()` — `o = center.xy() - point(POSX,POSY)` | `get_tile_origin()` |
| `cata_tiles::draw()` — `op` setup from dest rect | `set_viewport_px()` |
| `player_to_screen()` | `camera_2d::world_to_screen()` |
| `assemble_light_inputs()` — `cam_off` formula | `get_view_matrix()` components |
| All ad-hoc `(mx - o.x) * tile_width + op.x` patterns | `world_to_screen()` |

### 1.4 Ownership

`game` owns `camera_2d main_camera_`. `cata_tiles::draw()` receives a `camera_2d const &` parameter. The lighting pipeline gets the view matrix from `main_camera_.get_view_matrix()`.

### 1.5 Verification

- Build: `cmake --build build --target cataclysm-bn-tiles`
- Visual: pixel-perfect screenshots match before/after
- Tests: existing vision/FOV tests pass unchanged

---

## Phase 2: Smart follow & UX camera features

**Goal**: features players notice immediately. All on by default, tweakable in F4 dev panel.

### 2.1 Smooth lerp follow

```cpp
void camera_2d::update( point_bub_ms target, tripoint_rel_ms velocity, float dt ) {
    // Dead zone check (Phase 2.3)
    if( dead_zone_radius_ > 0 && ...within dead zone... )
        return;

    // Smooth lerp
    float t = 1.0f - std::exp( -smooth_follow_speed_ * dt );
    center_ = point_bub_ms(
        lerp( center_.x(), target.x(), t ),
        lerp( center_.y(), target.y(), t )
    );

    // Look-ahead (Phase 2.2)
    if( look_ahead_tiles_ > 0.0f && !velocity.raw().is_zero() ) {
        auto dir = normalize( velocity.raw() );
        center_ += point_bub_ms( dir.x * look_ahead_tiles_, dir.y * look_ahead_tiles_ );
    }

    // Shake decay (Phase 2.4)
    shake_intensity_ *= shake_decay_;
}
```

### 2.2 Look-ahead

Offset the center toward the player's movement direction by `look_ahead_tiles_ * normalize(velocity)`. Diminishes smoothly when stationary. F4 tweak: `camera_look_ahead`.

### 2.3 Dead zone

If the player is within `dead_zone_radius_` tiles of the current center, skip `update()` entirely. This avoids unnecessary SDF/vis rebuilds when the player makes small micro-adjustments. F4 tweak: `camera_dead_zone`. Also exposed as a game option `CAMERA_DEAD_ZONE`.

### 2.4 Camera shake

```cpp
void camera_2d::shake( float intensity, float duration ) {
    shake_intensity_ = intensity;
    shake_decay_ = std::pow( 0.001f, 1.0f / ( duration * 60.0f ) );
}

// Applied in world_to_screen:
auto offset = random::offset() * shake_intensity_ * tile_size;
return screen + offset;
```

F4 toggle: `camera_shake` (allows disabling if motion-sick).

### 2.5 Zoom

- Mouse wheel → `zoom_ *= 1.1f` or `zoom_ /= 1.1f`
- Clamp to `[0.25f, 4.0f]`
- `world_to_screen` scales by `zoom_`
- SDF rebuild triggered on zoom change (shadow resolution changes)
- Keybinding to reset zoom to 1.0
- F4 slider: `camera_zoom`

### 2.6 World bounds clamping

Prevent the camera from showing void beyond map edges:

```cpp
int half_viewport_x = (viewport_width_ / tile_width_ / zoom_) / 2;
center_.x() = clamp( center_.x(), world_min_x + half_viewport_x, world_max_x - half_viewport_x );
```

### 2.7 F4 debug panel integration

All camera knobs exposed via `debug_params` struct alongside existing lighting knobs (`dither_amt`, `gi_strength`, etc.):

| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `camera_smooth_speed` | float | 8.0 | Follow lerp speed (tiles/sec) |
| `camera_look_ahead` | float | 3.0 | Look-ahead tiles |
| `camera_dead_zone` | int | 2 | Dead zone radius |
| `camera_shake` | float | 1.0 | Shake intensity multiplier (0 = off) |
| `camera_zoom` | float | 1.0 | Current zoom level |

### 2.8 Verification

- Walk around with default settings → smooth panning, look-ahead, no snap
- Trigger explosion → shake decays over time
- Mouse wheel → zoom in/out, shadows adapt
- F4 panel → each knob changes behavior in real-time
- Disable smooth follow → old behavior restored

---

## Phase 3: Multi-viewport & GPU minimap

**Goal**: replace old pixel minimap (2i-B-7b) with a `camera_2d`-driven minimap.

### 3.1 `cata_tiles::draw()` takes a camera reference

```cpp
void cata_tiles::draw( camera_2d const &cam, SDL_Rect const &viewport_rect, ... );
```

No more implicit `o`/`op` — all coordinate math goes through `cam`.

### 3.2 Minimap camera

```cpp
camera_2d minimap_camera_;
minimap_camera_.set_center( main_camera_.get_center() );
minimap_camera_.set_zoom( 0.05f );   // overview zoom
minimap_camera_.set_viewport_px( { sidebar_x, minimap_y } );
minimap_camera_.set_viewport_size( { 200, 200 } );
```

Minimap camera uses a **different tile rendering path**: instead of per-tile drawing at small zoom (which would be unreadable noise), it renders at the **overmap tile (OMT) level** — equivalent to `draw_from_id_string()` with `C_OVERMAP_TERRAIN`. This means the minimap shows an overmap-style overview centered on the player's current submap position, not a scaled-down main view.

F4 toggle: `minimap_mode` — switches between:
- `overmap` (existing strategic overmap view)
- `camera` (new camera-driven, centered on player's submap position)

### 3.3 Draw order in `game::draw()`

```cpp
// Main viewport
draw( main_camera_, w_terrain_rect );

// Minimap (if enabled)
if( get_option<bool>( "PIXEL_MINIMAP" ) ) {
    draw( get_minimap_camera(), w_minimap_rect );
}
```

Both write to the same `world_target` GPU texture at different screen positions via the tile batcher.

### 3.4 Delete old pixel minimap

Remove `src/pixel_minimap.cpp` and `src/pixel_minimap.h` entirely. The `SDL_Renderer`-based `RenderCopy` path is gone. This completes 2i-B-7b.

### 3.5 Verification

- Minimap renders via GPU path (visible in overlay/debug panel)
- Minimap shows overmap-style tiles, not scaled-down main tiles
- No more `SDL_Renderer` calls during gameplay
- FPS improves on builds that previously hit the SDL_Renderer path

---

## Phase 4: Dirty-tile & retained-mode rendering

**Goal**: skip re-enqueuing unchanged tiles. Largest perf win.

### 4.1 Tile generation counter

Add `uint32_t generation` to `submap` (or `level_cache`). Incremented when:
- Terrain/furniture/trap changes within that submap
- Transparency changes (fire, smoke, fields)
- Vehicle structure changes

### 4.2 Change set tracking

`cata_tiles` maintains a `std::unordered_map<submap_id, uint32_t> last_drawn_generation_` per frame.

Before `cata_tiles::draw()`:
1. Determine which submaps intersect `cam.visible_tile_rect()`
2. Compare each submap's current generation against `last_drawn_generation_`
3. Only re-enqueue tiles from submaps where generation changed
4. Tiles from unchanged submaps: reuse their existing `sprite_instance` data

### 4.3 GPU accumulation texture

`world_target` currently does `LOADOP_CLEAR` each frame. Switch to `LOADOP_LOAD` when dirty-tile is active:

- On frames with zero generation changes: skip tile pass entirely, just re-blit `world_target`
- On frames with partial changes: only re-draw the changed screen tiles into `world_target`
- Full redraw forced when: camera pans, zoom changes, SDF rebuild triggers

### 4.4 Verification

- Large static base: CPU render_time drops to near-zero after initial draw
- Dynamic tiles (fire, moving creatures, player movement) still update each frame
- No flicker or stale tile artifacts (`world_target` persistence is correct)
- F4 panel shows: `dirty_stats: submaps_checked=X, changed=Y, skipped=Z`

---

## Phase 5: GPU JFA SDF (Jump Flood Algorithm)

**Goal**: better shadows (Euclidean distance), no CPU cost. Already planned in `src/lighting/CLAUDE.md`.

### 5.1 Implement JFA compute shader

New file `src/lighting/jfa_sdf.comp.hlsl`:

1. **Seed pass**: write 0 for obstructed tiles, INF for unobstructed
2. **Flood pass**: `log2(max_dim)` iterations, each stepping `2^(k-1)` pixels, propagating nearest seed
3. **Output**: Euclidean distance field (float32), tile-resolution

Input: obstruction mask from `transparency_cache` (or submap obstruction data). Output: `SdfBuf` (same storage buffer as current).

### 5.2 Remove CPU SDF path

Delete `sdf_pass.cpp:compute_sdf_cpu()`, B1 region optimization, Chebyshev BFS code.

`build_and_submit_lighting()` dispatches the JFA compute shader instead.

### 5.3 Verification

- Shadows are round (no diamond faceting from Chebyshev)
- CPU `render_body` avg drops by 1-3ms (no more CPU SDF)
- F4 SDF overlay shows Euclidean (not Chebyshev) distances

---

## Phase 6: 3D FOV occlusion culling

**Goal**: don't brute-force every z-level through solid floors.

### 6.1 Per-submap floor height map

For each submap, precompute `std::array<std::array<int, SEEY>, SEEX> lowest_open_z` — the lowest z-level (relative to submap base) where `TFLAG_NO_FLOOR` is set or there's open air above.

Updated when terrain changes in the submap (piggyback on the generation counter from Phase 4).

### 6.2 Culling in the z-loop

In `cata_tiles::draw()` z-level iteration (around line 3500 of `cata_tiles.cpp`):

```cpp
int sx = temp_x / SEEX;
int sy = temp_y / SEEY;
int lowest_air = submap_lowest_open_z( sx, sy, temp_x % SEEX, temp_y % SEEY );
if( candidate_z < lowest_air ) break;  // solid floor below this column
```

When `fov_3d_occlusion` is off, this check is skipped (current behavior preserved).

### 6.3 Verification

- `fov_3d_z_range = 10` in a dense building: identical visual output
- Per-frame cost for 3D viewports drops proportionally to floors skipped
- Open areas (no floors above) still render all z-levels

---

## Delivery order

```
refactor/camera-2d-class         ── Phase 1 (mandatory base)
feat/camera-smart-follow          ── Phase 2 (high user impact)
feat/camera-minimap               ── Phase 3 (unblocks 2i-B-7b)
perf/camera-dirty-tile            ── Phase 4 (biggest perf win)
perf/gpu-jfa-sdf                  ── Phase 5 (better shadows)
perf/3d-fov-occlusion             ── Phase 6 (cheaper z-levels)
```

Phase 1 is mandatory before any other phase. Phases 2–6 can be done in any order after Phase 1.
