# Camera System Modernization Plan

## STATUS (reviewed 2026-07-03)

✅ **DONE.** Core goal delivered. Deferred items (P4 dirty-tile, P6 z-cull, full P1 `view_offset` migration) closed as won't-do with documented rationale.
Implemented: P1 (`camera_2d` additive variant), P2 smart-follow (smooth lerp + look-ahead + dead-zone + shake + F4 RmlUi knobs), P2.5 zoom (pre-existing), P3 GPU minimap (`draw_om_tile_recursively` + unlit-overlay route; `pixel_minimap.*` deleted), P5 GPU JFA SDF (seed→flood→resolve; CPU DT removed). Deferred/moot: P4 dirty-tile (antagonistic w/ smooth-follow), P6 z-cull (`dont_draw_lower_floor` already culls), full P1 `view_offset` 17-site migration (pure churn).
As-shipped divergences from spec: `draw_minimap` kept old signature `(point dest, const tripoint_bub_ms&, int w, int h)`; F4 knobs are RmlUi-only (`cam_smooth`/`cam_lookahead`/`cam_deadzone`), no ImGui Camera tab, no `CAMERA_DEAD_ZONE` game option.

## Context

The current "camera" is ad-hoc math distributed across `avatar::view_offset`, `game::driving_view_offset`, `cata_tiles::o`/`op`, and inline `player_to_screen()` conversions. The lighting/render pipeline is surprisingly modern (GPU compute GI, SDF shadows, instance-batched sprites, bloom, tonemap), but the camera/viewport layer is fundamentally 1990s tile-scrolling. This plan modernizes the camera in 6 phases.

> **Review status (2026-06-23, re-verified):** An earlier review layer in this file claimed several real functions were fictional; those notes were themselves wrong and are corrected here (re-verified by grep against `src/`). **Blockers to clear before Phase 1:** **A2** (`set_view_offset` is not a shared API — must be created, and must replicate its z-clamp + `invalidate_map_cache` payload), **A3** (`draw()` z-component path undefined — decide at Phase 1, since §1.3 changes the `draw()` call). **B3** stands (`draw_minimap` already exists → replace not add). **B1/B2 RETRACTED:** `assemble_light_inputs` (`sdl_render_frame.cpp:69`/`372`, called `:876`) and `seen_through_air_light` (`cata_tiles.cpp:3492` lambda, used `:3594/3605/3626`) **both exist** — do not treat them as placeholders. **A1:** re-derive the `view_offset` site list *semantically* — the raw `rg` count includes false positives (e.g. local variables named `view_offset`), so do not hard-code "44/9". **C1–C5** are design-time resolutions inside their phases. **D** = line-number/file-list corrections.

## Implementation Status (2026-06-24, verified against source)

This plan was written against an older pipeline; deep verification this session
found several phases already done or moot. Current state:

| Phase | Status | Evidence / decision |
|-------|--------|---------------------|
| **1 — camera_2d** | **Done, additive variant** | `camera_2d` ships (`src/camera_2d.{h,cpp}`), owns smooth float center + sub-tile residual folded into `o`/`op` (the single shared input for sprites + lighting). `view_offset` is kept as the discrete *target* feeding the camera, NOT migrated. The full 17-site `view_offset` rip-out + `o`/`op` deletion is **deferred as pure churn** — the additive design already gives a single viewport/lighting source of truth with no modal-UI regression risk. See `plans/camera_subtile_contract.md`. |
| **2 — smart follow** | **Done** | Smooth exp-lerp follow (`073fcf0546`), screen shake + explosion hook (`d49a45176b`), look-ahead + dead-zone + live F4 tuning (`8c8fc9c5f3`). Zoom: see below. World-bounds clamp (2.6) resolved by rationale — look-ahead defaults off and the base target is already game-constrained, so the view never exceeds valid range out-of-box. |
| **2.5 — zoom** | **Already existed** | `ACTION_ZOOM_IN/OUT` → `game::zoom_in()/zoom_out()` → `cata_tiles::set_draw_scale()` changes `tile_width`. `cam_off` uses `tile_pixel_size`, so lighting already tracks zoom (Risk 5 moot — engine zooms via tile-size, not screen-space scale). Optional polish: mouse-wheel + smooth interpolation. |
| **3 — GPU minimap** | **Done (eyeball owed)** | `draw_minimap` now renders seen OMT tiles via `draw_om_tile_recursively` into the minimap rect + a player beacon, routed through a new unlit-overlay path (`render_state::set_unlit_overlay_route` → existing font-glyph flush; no shader/pipeline/pass change). Full projection save/restore keeps lighting `cam_off` correct. `pixel_minimap.{h,cpp}` + `pixel_minimap_projectors.{h,cpp}` deleted; option/window plumbing reused. Scale/position need an in-game pass. |
| **4 — dirty-tile** | **Deferred (rationale)** | Antagonistic with smooth-follow: the camera moves every frame during motion → `camera_moved` forces full redraw → P4 yields nothing exactly when perf matters; only helps when fully stationary. Plus C2 (frozen creatures/SCT/fields) + C3 (write-path audit) hazards. Marginal benefit, real risk. |
| **5 — GPU JFA SDF** | **Already shipped** | `gpu_sdf_pass` (seed→flood→resolve), `compute_sdf_cpu` deleted (commits `61869027bf`/`59f09cb0e4`). |
| **6 — 3D FOV cull** | **Already satisfied** | The z-descent loop already `break`s at the first solid floor via `dont_draw_lower_floor` (`cata_tiles.cpp:3664`). It does NOT brute-force through floors — the plan's premise was a misread. A `lowest_open_z` precompute adds the C1 storage problem + per-floor-dirty rebuild cost for ~0 gain (open-air columns must render all levels regardless). Not implementing. |

**Net remaining:** P3 minimap (real value, needs in-game verification), and the explicitly-deferred churn (P4, full-P1 migration). The plan's core intent — a modern camera with smooth follow and lighting locked to it — is delivered.

### Design decisions

| Question | Decision |
|----------|----------|
| File location | `plans/camera_modernization.md` |
| Zoom / Smooth follow default | **On by default**, tunable via F4 dev panel debug params |
| Minimap camera | **Replace old pixel minimap entirely**; separate look/scale from main camera |
| Dead zone radius | **Configurable** — game option `CAMERA_DEAD_ZONE` + F4 override |

---

## Codebase Discovery: Current Camera/Viewport Architecture

These findings were uncovered by deep exploration (see `AGENTS.md` session trace). Every Phase below depends on these invariants.

### Architecture: current data flow

```
Player Position (bub_pos)  +  view_offset*  +  driving_view_offset†
                      ↓
            ter_view_p = bub_pos + view_offset
                      ↓
      sdl_curses_draw.cpp → tilecontext->draw(dest, ter_view_p, ...)
                      ↓
   cata_tiles::draw():
     o = ter_view_p.xy() - point(POSX, POSY)   // leftmost visible tile
     op = dest                                  // pixel origin of tile area
                      ↓
     For each visible screen tile (col, row):
       map_tile = point(col + o.x(), row + o.y())
       player_to_screen(map_tile):
         screen = (map_tile - o) * tile_size + op    // non-iso
         // iso: projected onto diamond grid
                      ↓
     draw_sprite_at(tile, screen_pos) → enqueue_tile_sprite() → GPU batch
                      ↓
     refresh_display():
       assemble_light_inputs():
         cam_off = op / tile_pixel_size - o    // ← for fragment shader
       build_and_submit_lighting():
         B1 region = o, screentile_width, screentile_height (+8 margin)
```

* `avatar::view_offset` (`tripoint_rel_ms` at `src/avatar.h:325`) — a delta from the avatar's bubble position. Public member, modified by ~15 call sites.
† `game::driving_view_offset` (`point` at `src/game.h:1160`) — additively composed into `view_offset` via `set_driving_view_offset()`.

### `avatar::view_offset` — all mutation sites

Every site that writes `view_offset` must be migrated.

> ⚠️ **CORRECTNESS GATE (review 2026-06-23, re-verified).** This table is **incomplete** — regenerate the real list at implementation time. But the raw `rg 'view_offset *(=|\+=|-=)'` count is **not** a valid gate: it matches expressions that assign **local variables** named `view_offset` (e.g. `sdl_window_dims.cpp:160` declares a local `point_bub_ms view_offset;`, so `:162` is *not* an `avatar::view_offset` write — false positive). Derive the migration list **semantically** (writes to `avatar::view_offset` / `g->u.view_offset` only), not by regex count. Approximate per-file writes to the real member: `game.cpp` ~21, `veh_interact.cpp` ~9, `debug_menu.cpp` 4, `ui.cpp` 3, `activity_actor.cpp` 3, `ranged.cpp` 1 (direct) + the `target_ui` setter, `editmap.cpp` 1, `handle_action.cpp` 1, `iuse.cpp` 1. **`sdl_window_dims.cpp` is excluded** (local var).

| Context | File:line (verified) | Pattern |
|---------|-----------|---------|
| Pan (ACTION_SHIFT_\*) | `handle_action.cpp:1864` | `u.view_offset += shift_delta * soffset` |
| Center view (ACTION_CENTER) | `handle_action.cpp:1842-1843` | `u.view_offset.xy() = driving_view_offset` |
| Look-around | `game.cpp:10025` | `u.view_offset = center - u.bub_pos()` |
| Look-around viewer save/restore | `game.cpp:10650, 10837, 10854, 10863` | Save/restore + `tripoint_rel_ms::zero()` |
| Modal save/restore (×2 blocks) | `game.cpp:11037–11418, 11527–11970` | Save/restore |
| Item-list view shift | `game.cpp:10186` | `u.view_offset = prev_offset` |
| Aiming (target_ui, **private** setter) | `ranged.cpp:3545` direct; `3110/3128/3225/3507` via `target_ui::set_view_offset` (`3539-3551`) | see A2 below |
| Point menu | `ui.cpp:1524` (+ save/restore `1511/1521`) | `g->u.view_offset = center - g->u.bub_pos()` |
| Editmap target | `editmap.cpp:498` | Same pattern |
| Debug menu possess / pick_character | `debug_menu.cpp:555, 603, 696` | `view_offset = other.bub_pos() - avatar.bub_pos()` |
| Driving offset setter | `game.cpp:2195-2205` | Additive composition: `view_offset = (old - old_driving) + new_driving` |
| Vehicle interaction | `veh_interact.cpp` (9 writes incl. `4151/4152/4170`) | Save/restore |
| Zones manager | `game.cpp:9269–9756` | Save/restore (original draft cited `9103-9105` — wrong) |
| Aim activity | `activity_actor.cpp:123, 257, 340` | `initial_view_offset` stored on start, restored on finish |
| ~~Window resize~~ | ~~`sdl_window_dims.cpp:162`~~ | **NOT a migration site** — `:160` declares a local `point_bub_ms view_offset;`; `:162` assigns that local, not `avatar::view_offset`. False positive; do not migrate. |
| Remote vehicle controller | `iuse.cpp:7977` (read), `8011-8012` (write `.x()`/`.y()`) | Save/restore `g->u.view_offset` (draft + prior review both cited `8013-8014` — wrong) |

**Pattern for modal UIs**: `stored = u.view_offset; u.view_offset = tripoint_rel_ms::zero();` on enter, `u.view_offset = stored;` on exit. This must be preserved exactly by the camera snapshot/restore API.

**A2 — `set_view_offset()` is NOT a shared API.** It is a **private method of the `target_ui` class** (`ranged.cpp:3539-3551`, decl `582`), used only by aiming. Every other site mutates `view_offset` by **direct assignment**. The migration must therefore **create** a real camera-backed setter (e.g. `avatar::set_view_offset()` routing to `game::main_camera_`) as an explicit Phase 1 task — it does not exist today. **The new setter must replicate `target_ui::set_view_offset`'s payload** (`ranged.cpp:3541-3550`): clamp z to `[-fov_3d_z_range, fov_3d_z_range]`, then re-clamp `z + src.z()` to `[-OVERMAP_DEPTH, OVERMAP_HEIGHT]`, and call `get_map().invalidate_map_cache( new_z )` **only on z-change**. Dropping this is a silent z-level cache-invalidation regression. Add `iuse.cpp` to the File-change summary (not `sdl_window_dims.cpp` — false positive, see gate note).

### `driving_view_offset` additive composition (critical)

The `set_driving_view_offset()` function (`game.cpp:2195-2205`) does:
```cpp
u.view_offset -= driving_view_offset;     // un-apply old
driving_view_offset = p;                   // store new
u.view_offset += driving_view_offset;      // apply new
```

`calc_driving_offset()` (1716-1813) computes the target offset from vehicle velocity, direction, sight range, and smoothed window. It:
1. Computes max offset boundary (`max_offset`) from window size minus 3-tile margin
2. Maps velocity to [0,1] range with linear interpolation
3. Normalizes direction and squeezes into corners
4. Clamps to avoid hiding visible area (sight_range)
5. Smooths with 1-tile-per-frame step toward target

**Result**: `view_offset = manual_scroll + driving_scroll`. `ACTION_CENTER` only resets the manual part, preserving driving scroll.

### `cata_tiles::o` and `op` — the critical bridge to lighting

`o` (`point_bub_ms` at `cata_tiles.h:1314`): leftmost/topmost visible tile's **map index** in tile coords. Set as `center.xy() - point(POSX, POSY)` at `cata_tiles.cpp:3226`.

`op` (`point` at `cata_tiles.h:1334`): pixel offset of tile-drawing area from window top-left (sidebar width). Set from `dest` rect at draw time.

**Lighting cam_off formula** (computed inside `assemble_light_inputs()` at `sdl_render_frame.cpp:386-392`):
```
cam_off = op / tile_pixel_size - o
```
> ⚠️ **Review (B1) — RETRACTED.** An earlier note claimed `assemble_light_inputs()` does not exist. It does: forward-declared `sdl_render_frame.cpp:69`, defined `:372`, called `:876`; the `cam_off` block lives inside it (`:386-392`). Edit it in place — do **not** create a duplicate function.

This is the world-position → map-tile camera offset used by `sprite.frag.hlsl` for SDF shadow cone-tracing and emitter positioning. The SDF B1 region optimization (`frame_build.cpp:182-188`) bounds computation to `[cam_x0 - 8, cam_y0 - 8, cam_w + 16, cam_h + 16]` where `cam_x0 = o.x`, `cam_w = screentile_width`.

**All callers of `player_to_screen()`**: ~28 internal uses in `cata_tiles.cpp` (tile transforms, overlays, debug views, creatures, SCT, zones) + 1 external in `sdl_overmap_draw.cpp:393`. Each must be migrated.

### world_target and the GPU accumulation pipeline

- `world_target` = `RGBA16F` HDR accumulation texture (size = swapchain pixels)
- First frame: `LOADOP_CLEAR` (black). Subsequent: `LOADOP_LOAD` (preserves previous frame). ⚠️ **Review (C5):** in source this persistence is driven by `ui_composite_target`'s one-shot dirty flag (`invalidate()` / `consume_dirty()`, `ui_composite_target.h`), **not** raw `LOADOP_*` enums — don't grep for those. Phase 4's `wt->consume_dirty()` (§4.3) already matches the real API.
- No-input frames: tile queue retains last populated content, world_target not re-cleared
- This accumulation behavior is already in place — Phase 4 builds on it
- Single world_target means no multi-viewport support exists yet
- The `sprite_batcher::begin_pass()` takes target format for per-pass pipeline creation
- Scissoring (`set_tile_scissor`) is the only clipping mechanism

### Per-submap dirty tracking — what already exists

`level_cache` (`src/map.h:308`) has:
- `transparency_cache_dirty` (cata_dynamic_bitset, one bit per submap)
- `outside_cache_dirty`, `floor_cache_dirty` (same)
- `transparency_generation` (uint64) — incremented when any submap's transparency is marked dirty
- Already consumed by `sdl_render_frame.cpp:163` to gate SDF rebuild

`submap` (`src/submap.h`) has per-submap dirty booleans:
- `transparency_dirty`, `outside_dirty`, `floor_dirty`, `pf_dirty`

**What does NOT exist**: a per-submap `generation` counter (uint64 incremented on any tile change). The comment at submap.h:286-290 explicitly hints at this.

### F4 debug panel architecture

Two parallel UI layers:
1. **ImGui** (`sdl_lighting_devui.cpp:draw()` → tabbed window, `dbg_slider()` helper, revert-to-original)
2. **RmlUi** (`data/gui/devui.rml` → data bindings in `devui_rml_open()`, `DirtyAllVariables()`)

Both read/write `g_dbg_params` (the `debug_params` struct at `sprite_batcher.h:127-174`).

**`debug_params` is wire-stable with GPU cbuffer** (`register(b2, space3)` for fragment, `sprite_batcher.h:128+`). Actual size **~136 bytes** — note the in-source `// 48 bytes` comment is **stale**, and there is **no `static_assert` on `sizeof(debug_params)`** (unlike `sprite_instance`). Adding camera knobs that are CPU-only need NOT be in this struct — they can live in a separate `camera_debug_params` or just be file-scope globals in `sdl_lighting_devui.cpp`.

> ✅ **Review (C4) — cheap insurance.** While this area is being touched, add `static_assert( sizeof( lighting::debug_params ) == N )` (with N pinned to the verified layout) and fix the stale comment. The struct is already unguarded and wire-fragile against the HLSL `cbuffer DebugParams`; a size assert turns a silent cbuffer mismatch into a compile error.

**The F4 panel forces `rebuild.structure = true` every frame while open** (`sdl_render_frame.cpp:168-170`). Camera-debug sliders should not exacerbate this — either gate the forced rebuild behind a lighting-needs-it flag, or make camera sliders cheap enough that the forced rebuild is irrelevant.

### Pixel minimap architecture (to be replaced)

Class `pixel_minimap` (`src/pixel_minimap.h/.cpp`):
- Uses `SDL_Renderer`/`GeometryRenderer` rect drawing
- Cache keyed on `tripoint_abs_sm` (absolute submap coords)
- ~17K `geometry->rect()` calls per frame at default mapsize=11
- Settings: mode (solid/squares/dots), brightness, scale_to_fit, beacon_size, blink
- **Does NOT use overmap**: operates on bubble-level `map::get_color_at` + `level_cache.visibility_cache`
- Full cache clear on submap shift > 1 or z-level change
- The commented state in `CLAUDE.md` says "NOT migrated, invisible" for 2i-B-7b

Files to delete:
- `src/pixel_minimap.h`
- `src/pixel_minimap.cpp`
- `src/pixel_minimap_projectors.h`
- `src/pixel_minimap_projectors.cpp`

### SDF pipeline — CPU is already Euclidean, JFA planned

The `CLAUDE.md` comment "Chebyshev BFS SDF" is **stale**. The current `compute_sdf_cpu()` uses Felzenszwalb-Huttenlocher 2012 exact Euclidean DT with 4x supersampling (SS=4). JFA is the intended GPU replacement (seed pass + log2 flood passes).

Key invariants:
- SDF grid: 4x finer per axis than tile grid (16x cells)
- B1 region: limited to visible rect + 8 tile margin
- All GPU data uses `StructuredBuffer<float>` (not Texture2D — Metal bug workaround)
- Layout: x-major, `buf[x * sdf_map_h + y]`

JFA will replace the CPU DT while keeping the same buffer format — the fragment shader needs zero changes.

### Z-level rendering — existing culling

The z-loop in `cata_tiles.cpp:3510` iterates downward from `center.z()` to `-OVERMAP_DEPTH`:

1. `dont_draw_lower_floor()` (map.cpp:7248): returns true if tile at `p` has solid floor above (checks `TFLAG_NO_FLOOR` / `TFLAG_Z_TRANSPARENT`). This is the **primary culling gate**.
2. `fov_3d_z_range` (`cached_options.h:65`): limits how far down rendering goes. The deeper-z fallback lighting lives in `build_seen_cache`/`lightmap.cpp` (real symbol: `vert_blocked`, `lightmap.cpp:1400`). ⚠️ **Review (B2) — RETRACTED.** An earlier note claimed `seen_through_air_light()` does not exist. It does: a lambda at `cata_tiles.cpp:3492`, used at `:3594/3605/3626`, governing deeper-z fallback lighting. Phase 6 z-culling must not break it — treat it as a real interaction point.
3. `min_z` tracking: lowest z-level with drawable content.
4. `vert_blocked` in `build_seen_cache`: cumulative floor mask propagated across z-levels.

**No per-column `lowest_open_z` optimization exists** — the engine checks every z-level column-by-column, even through solid floors. Phase 6 adds this.

---

## Phase 1: `camera_2d` class (pure refactor, no behavior change)

**Goal**: single source of truth for viewport state.

### 1.1 Define `camera_2d`

New files `src/camera_2d.h` and `src/camera_2d.cpp`. The class API must be designed up front to support Phases 2-3 without ABI break:

```cpp
class camera_2d {
    // ─── Core state ─────────────────────────────────────────
    point_bub_ms center_;
    point viewport_px_;         // pixel offset of draw area (sidebar width)
    int viewport_width_ = 0;    // pixel dimensions of the viewport
    int viewport_height_ = 0;
    int tile_width_ = 32;
    int tile_height_ = 32;
    float zoom_ = 1.0f;         // Phase 2.5
    float rotation_ = 0.0f;     // reserved for future

    // ─── Phase 2 fields (defined now, feature-gated) ────────
    float smooth_follow_speed_ = 8.0f;
    float look_ahead_tiles_ = 3.0f;
    int dead_zone_radius_ = 2;
    float shake_intensity_ = 0.0f;
    float shake_decay_ = 0.9f;
    point shake_offset_;        // per-frame shake displacement
    bool iso_mode_ = false;

public:
    // ─── Configuration ───────────────────────────────────────
    void set_center( point_bub_ms p );
    void set_viewport_px( point p );
    void set_viewport_size( int w, int h );
    void set_tile_dimensions( int w, int h );
    void set_iso_mode( bool iso );

    // ─── Per-frame update (Phase 2 adds smooth logic) ───────
    // In Phase 1: snaps to target immediately.
    // In Phase 2: smooth lerp, look-ahead, dead-zone gating.
    // Returns true if center was updated (caller uses this to
    // trigger SDF rebuild or skip it).
    auto update( point_bub_ms target, tripoint_rel_ms velocity,
                 float dt ) -> bool;

    // ─── Coordinate transforms ───────────────────────────────
    // Replaces player_to_screen(o, op, …). Handles iso + zoom.
    [[nodiscard]] auto world_to_screen( tripoint_bub_ms ) const
        -> point;
    // Reverses world_to_screen. Used for mouse→map and
    // cursor_light_emitter conversions.
    [[nodiscard]] auto screen_to_world( point ) const
        -> tripoint_bub_ms;
    // Returns the bubble-local tile rect visible in this viewport.
    // Used for B1 SDF region culling and visible-submap iteration.
    [[nodiscard]] auto visible_tile_rect() const
        -> half_open_rectangle<point>;

    // ─── Accessors for legacy consumers ──────────────────────
    // Replaces cata_tiles::o — the leftmost visible tile index.
    [[nodiscard]] auto get_tile_origin() const -> point;
    // Replaces cata_tiles::op — pixel offset of draw area.
    [[nodiscard]] auto get_pixel_offset() const -> point;

    // ─── Lighting pipeline bridge ────────────────────────────
    // Returns { cam_off_x, cam_off_y, tile_pixel_size, zoom }.
    // The camera_off formula: op / tile_px - get_tile_origin()
    // as consumed by assemble_light_inputs() and sprite.frag.
    [[nodiscard]] auto get_view_matrix() const
        -> std::array<float, 4>;

    // ─── Snapshot for modal UI save/restore ──────────────────
    // Zones manager, vehicle interaction, aiming, debug possess
    // all save and restore view_offset. This supports that
    // pattern without exposing internal state.
    [[nodiscard]] auto snapshot() const -> camera_2d;
    void restore( camera_2d const &snap );

    // ─── Phase 2 API (stubs in Phase 1, return defaults) ─────
    void shake( float intensity, float duration );
    void nudge( tripoint_rel_ms delta );
    void reset_follow();
    // Returns true when within dead_zone and no nudge pending.
    // Caller uses this to gate SDF/vis rebuild.
    [[nodiscard]] auto is_steady() const -> bool;
    [[nodiscard]] auto get_center() const -> point_bub_ms;

private:
    // iso math branch
    auto world_to_screen_iso( tripoint_bub_ms ) const -> point;
    auto screen_to_world_iso( point ) const -> tripoint_bub_ms;

    // Applies shake offset to a screen-space point.
    auto apply_shake( point screen ) const -> point;

    // Applies zoom scaling about the viewport center.
    auto apply_zoom( point screen ) const -> point;

    // Clamps center_ to world bounds (Phase 2.6).
    void clamp_to_world_bounds();
};
```

### 1.2 Merge existing offsets

**`avatar::view_offset` → `camera_2d`**: The `tripoint_rel_ms` has a z-component. `camera_2d` stores `point_bub_ms center_` (no z). The z-level remains separate — it stays as a field on `avatar` or is stored as `center_z_` on the camera.

| Migration | Strategy |
|-----------|----------|
| `u.view_offset.xy()` | `cam.nudge(delta)` / `cam.reset_follow()` |
| `u.view_offset.z()` | Keep on `avatar` (it controls `ter_view_p.z()`) |
| `u.view_offset += shift * soffset` (pan) | `cam.nudge(shift * soffset)` |
| `u.view_offset = center - u.bub_pos()` (look) | `cam.set_center(center.xy())` |
| Save/restore for modal UIs | `snap = main_camera_.snapshot(); main_camera_.restore(snap)` |

**`game::driving_view_offset` → velocity input**: The camera receives velocity and the follow logic produces the offset implicitly. The additive composition (manual_scroll + driving_scroll) is preserved by the nudge() + velocity-input model:

- `nudge()` sets a manual offset that persists until `reset_follow()` or `ACTION_CENTER`
- `update()` with non-zero velocity produces the driving offset
- `reset_follow()` clears the manual nudge, keeping only velocity-driven offset
- `ACTION_CENTER` maps to `reset_follow()`

**`game::calc_driving_offset()`**: The smoothing (1-tile-per-frame step) moves into `camera_2d::update()`. The velocity-to-offset math stays in game.cpp as it depends on vehicle state (sight range, direction, velocity ranges). The camera receives the precomputed target offset or just the velocity vector and does the smoothing internally.

### 1.3 Replace `cata_tiles::o` and `op`

**Delete** `cata_tiles.h:1314` (`point_bub_ms o;`) and `cata_tiles.h:1334` (`point op;`).

| Location | Line(s) | Replace with |
|----------|---------|--------------|
| `cata_tiles::draw()` — `o = center.xy() - POSX,POSY` | 3226 | `cam.get_tile_origin()` |
| `cata_tiles::draw()` — `op = dest` | 3228 | `cam.get_pixel_offset()` |
| `player_to_screen()` | 7467-7483 | `cam.world_to_screen()` |
| Row/col → world tile: `temp_x = col + o.x()` | 3316-3320 | `col + cam.get_tile_origin().x()` |
| All `player_to_screen()` calls (~28 in cata_tiles.cpp) | various | `cam.world_to_screen()` |
| `sdl_overmap_draw.cpp` — `player_to_screen` | 393 | `cam.world_to_screen()` |
| `assemble_light_inputs()` — `cam_off` | sdl_render_frame.cpp:386-392 | `cam.get_view_matrix()` |
| `build_and_submit_lighting()` — B1 region | sdl_render_frame.cpp:234-241 | `cam.visible_tile_rect()` |
| `cursor_light_emitter` — world coords | sdl_render_frame.cpp:200-206 | `cam.screen_to_world()` |
| `dev_test_lights` — hover world | sdl_render_frame.cpp:223-227 | `cam.screen_to_world()` |

### 1.4 Ownership

```
game
 ├── camera_2d main_camera_               (primary viewport)
 ├── camera_2d minimap_camera_            (Phase 3; declared now, unused)
 ├── cata_tiles *tilecontext
 │     └── draw(camera_2d const&, …)      (receives from game)
 └── ...

sdl_render_frame.cpp (free functions)
  └── assemble_light_inputs() reads main_camera_ via g→main_camera_
  └── build_lighting() reads main_camera_.visible_tile_rect() for B1
  └── (cursor_light_emitter, dev_test_lights → screen_to_world)

avatar
  └── view_offset_z (int)                 // z-level offset only
```

### 1.5 verify pixel-perfect output

The Phase 1 refactor MUST produce identical pixels. Method:

1. Set `smooth_follow_speed_ = 0` (snap mode, replicates current behavior)
2. Capture `game::draw()` output to a GPU readback buffer, compare raw pixels
3. Test at multiple positions, zooms, iso/non-iso modes
4. Lighting `cam_off` values must match exactly — verify via F4 overlay

Automated approach: extend `save_screenshot` to capture before/after and `diff` the PNG or raw pixel data.

### 1.6 Edge cases

| Case | Handling |
|------|----------|
| `o` is `point_bub_ms`, not `point` | `camera_2d` uses `point` internally; conversion happens at the boundary |
| Viewport width/height = 0 (no tile area) | `visible_tile_rect()` returns empty rect; `world_to_screen` returns sentinel |
| `dt` = 0 on first frame | `update()` returns false; center unchanged |
| ISO mode toggled at runtime | `set_iso_mode()` switch; `visible_tile_rect()` width differs in iso |
| Modal UI save/restore during driving | Snapshot preserves nudge + center; restore re-applies both |
| Window resize | `set_viewport_px()` / `set_viewport_size()` called by `game::draw()` path |

---

## Phase 2: Smart follow & UX camera features

**Goal**: features players notice immediately. All on by default, tweakable in F4 dev panel.

### 2.1 Smooth lerp follow

```cpp
auto camera_2d::update( point_bub_ms target, tripoint_rel_ms velocity,
                        float dt ) -> bool
{
    if( dt <= 0.0f ) {
        return false;
    }

    // ── Dead zone (2.3) ────────────────────────────────────
    if( dead_zone_radius_ > 0 && !has_pending_nudge_ ) {
        int dx = std::abs( center_.x() - target.x() );
        int dy = std::abs( center_.y() - target.y() );
        if( dx <= dead_zone_radius_ && dy <= dead_zone_radius_ ) {
            return false;  // caller skips SDF/vis rebuild
        }
    }

    // ── Smooth lerp ────────────────────────────────────────
    // Framerate-independent exponential ease toward target.
    float t = 1.0f - std::exp( -smooth_follow_speed_ * dt );
    center_ = point_bub_ms(
        lerp( center_.x(), target.x(), t ),
        lerp( center_.y(), target.y(), t )
    );

    // ── Look-ahead (2.2) ───────────────────────────────────
    if( look_ahead_tiles_ > 0.0f && !velocity.raw().is_zero() ) {
        vec2 dir = normalize( velocity.raw() );
        float la = std::min( look_ahead_tiles_, 10.0f ); // cap
        center_ += point_bub_ms( dir.x * la, dir.y * la );
    }

    // ── World bounds clamping (2.6) ────────────────────────
    clamp_to_world_bounds();

    // ── Shake decay (2.4) ──────────────────────────────────
    shake_intensity_ *= shake_decay_;
    has_pending_nudge_ = false;
    return true;
}
```

`lerp(a, b, t) = a + (b - a) * t` with `t` clamped to [0, 1].

**Smooth follow disabled**: When `smooth_follow_speed_ <= 0`, `update()` snaps `center_ = target`.

### 2.2 Look-ahead

Offset the center toward the player's movement direction by `look_ahead_tiles_ * normalize(velocity)`.

**Decay**: The velocity vector naturally goes to zero when stationary. The look-ahead decays by proximity: when `target` catches up to `center_` (player stops), the lerp pulls center back to target over ~0.3s.

**F4 knob**: `camera_look_ahead` (float, default 3.0, range [0, 10]).

### 2.3 Dead zone

If the player is within `dead_zone_radius_` tiles of the current center, `update()` returns `false`. The caller (`build_lighting()` in `sdl_render_frame.cpp`) gates SDF/vis rebuild on the return value:

```cpp
// In build_lighting():
if( !main_camera_.update( target, velocity, dt ) ) {
    rebuild.structure = false;
    rebuild.vis = false;
}
```

No SDF rebuild + no vis rebuild = free frames while micro-adjusting.

**Game option**: `CAMERA_DEAD_ZONE` (int, default 2, range [0, 10], 0=off). Exposed in graphics tab with `COPT_CURSES_HIDE`.

**F4 knob**: `camera_dead_zone` (int, default 2).

### 2.4 Camera shake

```cpp
void camera_2d::shake( float intensity, float duration ) {
    shake_intensity_ = intensity;
    // Decay so that after `duration` seconds at 60fps,
    // intensity < 0.001
    shake_decay_ = std::pow( 0.001f, 1.0f / ( duration * 60.0f ) );
}
```

Applied in `world_to_screen()`:

```cpp
point camera_2d::apply_shake( point screen ) const {
    if( shake_intensity_ > 0.001f ) {
        // Deterministic: use frame counter + tile seed, not rng
        float sx = sin( shake_phase_ + screen.x * 0.1f );
        float sy = cos( shake_phase_ + screen.y * 0.1f );
        point offset(
            sx * shake_intensity_ * tile_width_,
            sy * shake_intensity_ * tile_height_
        );
        return screen + offset;
    }
    return screen;
}
```

**Deterministic**: Uses sine-based offset from screen position + phase (incremented per frame), not raw random. This keeps the shake reproducible for replays.

**F4 toggle**: `camera_shake` (float, default 1.0, 0=off, intensity multiplier).

### 2.5 Zoom

```cpp
void camera_2d::set_zoom( float z ) {
    float old = zoom_;
    zoom_ = std::clamp( z, 0.25f, 4.0f );
    if( zoom_ != old ) {
        // Signal caller to rebuild SDF (shadow res changes)
        on_zoom_changed_ = true;
    }
}
```

**In `world_to_screen()`**:
```cpp
point camera_2d::apply_zoom( point screen ) const {
    // Scale about the viewport center (not top-left)
    float cx = viewport_px_.x + viewport_width_ / 2.0f;
    float cy = viewport_px_.y + viewport_height_ / 2.0f;
    return point(
        ( screen.x - cx ) * zoom_ + cx,
        ( screen.y - cy ) * zoom_ + cy
    );
}
```

**Zoom centering**: Scaled about the viewport center so the center tile stays centered.

**Input**:
- Mouse wheel: `zoom_ *= 1.1f` / `zoom_ /= 1.1f`
- Keybinding (e.g., `ACTION_ZOOM_IN` / `ACTION_ZOOM_OUT`)
- Reset zoom to 1.0 (e.g., `ACTION_ZOOM_RESET`)
- F4 slider: `camera_zoom`

**SDF rebuild on zoom**: `on_zoom_changed_` is checked by the engine to force `rebuild.structure = true` (shadow resolution changes with visible tile count).

### 2.6 World bounds clamping

```cpp
void camera_2d::clamp_to_world_bounds() {
    if( !clamp_to_bounds_ ) return;

    // Half-viewport in tiles at current zoom
    float half_w = ( viewport_width_ / static_cast<float>( tile_width_ )
                     / zoom_ ) / 2.0f;
    float half_h = ( viewport_height_ / static_cast<float>( tile_height_ )
                     / zoom_ ) / 2.0f;

    // Use OVERMAP_DEPTH/HEIGHT as world bounds.
    // In the future this should come from the actual map extent.
    constexpr int world_min_x = -OVERMAP_DEPTH * SEEX;
    constexpr int world_max_x = OVERMAP_HEIGHT * SEEX;
    // (same for y)

    center_.x() = std::clamp( center_.x(),
        world_min_x + static_cast<int>( half_w ),
        world_max_x - static_cast<int>( half_w ) );
    center_.y() = std::clamp( center_.y(),
        world_min_y + static_cast<int>( half_h ),
        world_max_y - static_cast<int>( half_h ) );
}
```

### 2.7 F4 debug panel integration

**Camera params** — these are CPU-only and do NOT go in the GPU `debug_params` struct. They live in a new struct or file-scope globals:

```cpp
// sdl_lighting_devui.h or new camera_debug.h
namespace camera_dbg {
    inline float smooth_speed = 8.0f;
    inline float look_ahead = 3.0f;
    inline int dead_zone = 2;
    inline float shake = 1.0f;
    inline float zoom = 1.0f;
}
```

These are piped to the camera instance each frame via `game::sync_camera_debug_params()`.

**New F4 "Camera" tab** (ImGui):
- `camera_smooth_speed` — SliderFloat(0, 20)
- `camera_look_ahead` — SliderFloat(0, 10)
- `camera_dead_zone` — SliderInt(0, 10)
- `camera_shake` — SliderFloat(0, 2, "0=off")
- `camera_zoom` — SliderFloat(0.25, 4.0)

**RmlUi binding**: Add data bindings in `devui_rml_open()` for the same params.

**F5 HUD**: Add camera offset display (`cam_off_x/y` already tracked via `EmitterOverlayState.cam_off_x/y`).

### 2.8 Verification

| Test | Expectation |
|---|---|
| Walk with default settings | Smooth panning, slight look-ahead, no snap |
| Walk at minimum speed | Subtle tile slide; no judder |
| Stop suddenly | Camera coasts ~0.3s then settles |
| Mouse wheel zoom in/out | Smooth zoom about center; shadows adapt (SDF rebuild) |
| F4 camera_smooth_speed = 0 | Snaps exactly like Phase 1 / current behavior |
| F4 dead_zone = 5 | Small movements don't trigger `is_steady()` |
| Explosion | `shake()` = random jitter decays over specified duration |
| World edge | Camera clamped = no void beyond map boundary |
| ACTION_CENTER | Resets nudge, preserves velocity-driven follow |
| Modal UI (zones, vehicle) | Camera snapshot/restore = position unchanged on return |

---

## Phase 3: Multi-viewport & GPU minimap

**Goal**: replace the old `SDL_Renderer`-based pixel minimap (2i-B-7b) with a `camera_2d`-driven minimap that renders overmap tiles via the GPU sprite pipeline.

### 3.1 `cata_tiles::draw()` takes camera reference

**Current signature** (cata_tiles.cpp:3107):
```cpp
void cata_tiles::draw( point dest, const tripoint_bub_ms &center,
                       int width, int height,
                       std::multimap<point, formatted_text> &overlay_strings,
                       color_block_overlay_container &color_blocks );
```

**New signature**:
```cpp
void cata_tiles::draw( camera_2d const &cam,
                       SDL_Rect const &viewport_rect,
                       std::multimap<point, formatted_text> &overlay_strings,
                       color_block_overlay_container &color_blocks );
```

Internally:
- `dest` → `cam.get_pixel_offset()` (was `op`)
- `center` → derived from `cam.get_center()` (was `ter_view_p`)
- `width/height` → `viewport_rect.w/h`

> ⚠️ **Review (A3) — z-component gap.** The real `draw()` takes `const tripoint_bub_ms &center` and the z-descent loop (`cata_tiles.cpp:3510`, `for( int z = center.z(); … )`) plus all of Phase 6 depend on `center.z()`. But `cam.get_center()` returns `point_bub_ms` (**no z**). §1.2 keeps the z-offset on `avatar` yet never says how `draw()` receives the absolute z after the signature change. **Decide one:** keep an explicit `int center_z` parameter on `draw()`, OR add `camera_2d::get_center_z()`. **Pin this at Phase 1** — §1.3 already changes the `draw()` call, so the z-source must be settled before that signature change, not deferred to Phase 3, or the z-loop and Phase 6 break silently.

The `draw()` call in `sdl_curses_draw.cpp` changes from:
```cpp
tilecontext->draw( point( pos.x * fontwidth, pos.y * fontheight ),
                   g->ter_view_p, width, height, … );
```
to:
```cpp
tilecontext->draw( g->main_camera_, viewport_rect, … );
```

### 3.2 Minimap camera

```cpp
// In game::setup() or game::init():
minimap_camera_.set_zoom( 0.05f );   // overview zoom
minimap_camera_.set_tile_dimensions( 4, 4 );   // small tiles
minimap_camera_.set_viewport_size( { 200, 200 } );
// Position set each frame to follow main camera:
minimap_camera_.set_center( main_camera_.get_center() / SEEX );
```

> ⚠️ **Review (B3):** `cata_tiles::draw_minimap(point dest, const tripoint_bub_ms &center, int width, int height)` **already exists** (`cata_tiles.cpp:4205`) — it is the old pixel-minimap path that §3.4 deletes. The new signature below is a **replace** (delete the old method body + decl, then add the new one), not an add. Do not leave both — the old overload would become dead code.

**OMT rendering path**: The minimap does NOT render at zoom=0.05 with the normal tile draw (tiles would be 2 pixels each, unreadable noise). Instead it uses a dedicated draw path that renders **overmap terrain (OMT) tiles** centered on the player's current submap position.

```cpp
void cata_tiles::draw_minimap( camera_2d const &cam,
                               SDL_Rect const &viewport_rect )
{
    // Compute which OMTs are visible in the minimap viewport
    point_bub_ms center = cam.get_center();
    int omt_radius_x = viewport_rect.w / ( OMT_TILE_SIZE * 2 );
    int omt_radius_y = viewport_rect.h / ( OMT_TILE_SIZE * 2 );

    for( int dy = -omt_radius_y; dy <= omt_radius_y; ++dy ) {
        for( int dx = -omt_radius_x; dx <= omt_radius_x; ++dx ) {
            tripoint_abs_omt omt_pos = project_to<coords::omt>(
                here.bub_to_abs( center + point( dx, dy ) ) );
            oter_id terrain = overmap_buffer.ter( omt_pos );
            // Draw using C_OVERMAP_TERRAIN category
            draw_from_id_string( terrain.id(), … );
        }
    }

    // Draw player beacon
    draw_minimap_beacon( cam, player_pos );
}
```

### 3.3 Draw order in `refresh_display()`

```cpp
// 1. Main viewport — full rendering with lighting
cata_tiles::draw( main_camera_, w_terrain_rect,
                  overlay_strings, color_blocks );

// 2. Minimap (if enabled)
if( pixel_minimap_option && minimap_camera_enabled_ ) {
    // No lighting for minimap — draw with full brightness
    cata_tiles::draw_minimap( minimap_camera_, w_minimap_rect );
}
```

Both render into `world_target` at different screen positions via the tile batcher. Scissor rects keep them in their respective areas.

**Lighting pipeline**: The minimap draw does not go through `assemble_light_inputs()` — minimap tiles are drawn with `light_mul = 0` (full brightness, no shadows). The `set_tile_scissor` for the main viewport is replaced before the minimap draw.

### 3.4 Delete old pixel minimap

Remove these files:
- `src/pixel_minimap.h`
- `src/pixel_minimap.cpp`
- `src/pixel_minimap_projectors.h`
- `src/pixel_minimap_projectors.cpp`

Remove all references from:
- `cata_tiles.h` — `pimpl<pixel_minimap> minimap` member
- `cata_tiles.cpp` — `draw_minimap()`, settings loading, `minimap_requires_animation()`
- `game.h` / `game.cpp` — `toggle_pixel_minimap()`, `draw_pixel_minimap()`, `w_pixel_minimap`
- `sdl_curses_draw.cpp` — the pixel minimap draw path at line ~479
- `panels.cpp` — pixel minimap height logic
- `options.cpp` — all `PIXEL_MINIMAP_*` options (replace with `CAMERA_MINIMAP` toggle)
- `cached_options.h` — `pixel_minimap_option`
- `animation.cpp` — ⚠️ **Review (D) — corrected.** `minimap_requires_animation()` exists in **both** places: the free-function wrapper `animation.cpp:1237` (declared `animation.h:60`, calls the method, invoked at `handle_action.cpp:381`) **and** the method `cata_tiles::minimap_requires_animation()` at `cata_tiles.cpp:4210` (declared `cata_tiles.h:840`). The earlier note's "no reference in animation.cpp" was wrong — keep `animation.cpp` in scope (consistent with the File-change summary).
- `explosion.cpp` — temporary disable logic (`1116/1118/1134`)
- `action.cpp:325` + `handle_action.cpp` — the `"toggle_pixel_minimap"` action string (add to removal)
- `sdl_geometry.h` — `GeometryRenderer` may become unused (check)
- `sdl_window.cpp:157`, `sdltiles.cpp:110` — comments only, no code change

**Preserve**: The `PIXEL_MINIMAP` option name for continuity, but reconnect it to the new camera minimap toggle.

### 3.5 world_target reuse for multi-viewport

Currently `world_target` is a single `ui_composite_target`. Multi-viewport requires:

1. Main viewport renders into world_target with scissor = main rect
2. Minimap renders into world_target with scissor = minimap rect
3. Tonemap reads world_target (unchanged)
4. Composite blits world_ldr to swapchain (unchanged)

The `LOADOP_LOAD` behavior of world_target handles persistence — the minimap area is additive to the main viewport. Both share the same pixel format (RGBA16F).

### 3.6 Verification

| Test | Expectation |
|---|---|
| Minimap renders via GPU path | Visible in overlay/F4 diagnostics |
| Minimap shows OMT tiles | Overmap terrain colors match ASCII overmap |
| Minimap centered on player | Follows player across submaps |
| Player beacon visible | Colored diamond at player position |
| Old pixel minimap gone | No SDL_Renderer rect calls during gameplay |
| FPS | Higher than old SDL_Renderer path (GPU instanced) |

---

## Phase 4: Dirty-tile & retained-mode rendering

**Goal**: skip re-enqueuing unchanged tiles. Largest per-frame CPU perf win.

### 4.1 Per-submap generation counter

Add to `src/submap.h`:
```cpp
std::uint64_t generation = 0;   // incremented on any tile data change
```

Increment in `submap` setter methods:
```cpp
void submap::set_ter( const point &p, ter_id terr ) {
    ter[p.x][p.y] = terr;
    is_uniform = false;
    ++generation;
}
```

Also: `set_furn()`, `set_trap()`, `set_radiation()`, `set_lum()`, `set_graffiti()`, `set_signage()`. (All confirmed present on `submap`, `submap.h:103-251`.)

> ⚠️ **Review (C3) — write-path audit required.** Instrumenting these 7 setters is correct only if **every** terrain/furniture write funnels through them. Bulk/alternate paths can bypass them: mapgen, `map::load`, explosions writing terrain directly, and `map::ter_set`/`map::furn_set`. Before relying on the counter, grep those paths and confirm none write `sm->ter[x][y]` / `sm->frn[x][y]` directly — any direct write leaves `generation` stale → ghost tiles.

**NOT serialized** — `generation` is transient. Reset to 0 on submap load (`submap::submap()`). Initial value 0 vs `last_drawn_generation = UINT64_MAX` → first-draw triggers full render.

### 4.2 Change set tracking in `cata_tiles`

```cpp
// In cata_tiles:
struct submap_draw_state {
    std::uint64_t last_generation = UINT64_MAX;
};

// Keyed by absolute submap coordinates
std::unordered_map<tripoint_abs_sm, submap_draw_state> submap_states_;
```

Before the tile iteration loop in `cata_tiles::draw()`:

```cpp
// Determine which submaps are visible
auto visible_sms = get_visible_submaps( cam.visible_tile_rect() );

bool any_change = false;
for( auto abs_sm : visible_sms ) {
    auto &state = submap_states_[abs_sm];
    uint64_t gen = get_submap_generation( abs_sm );
    if( gen != state.last_generation ) {
        state.last_generation = gen;
        any_change = true;
    }
}

// ...also check camera movement, zoom change
bool camera_moved = cam.has_moved_since_last_frame();
bool zoom_changed = cam.zoom_changed_since_last_frame();

if( !any_change && !camera_moved && !zoom_changed ) {
    // No tile changes → skip enqueue entirely.
    // world_target retains last frame's content.
    return;  // skip the tile render
}
```

> ⚠️ **Review (C2) — dynamic overlays will freeze.** `generation` bumps only on tile-data setters. Per-frame visual state that is **not** a submap setter would be frozen by this `return`: moving creatures/NPCs, SCT (scrolling combat text), item-on-ground sprite changes, field/smoke/fire animation, weather, and the look/cursor overlays. Before trusting the skip path, define the full **always-redraw** set (any creature in the visible rect, any active SCT, any animation-frame tick) and OR it into the guard — or scope Phase 4 to the **terrain layer only** and keep dynamic layers always-enqueued.

**When to force full redraw**:
- Camera pan → visible submap set changes → all new submaps have `generation` ≠ `last_generation` → full redraw (correct by default)
- Zoom change → `cam.zoom_changed()` → force full redraw
- SDF rebuild → lighting changed → forced by `rebuild.structure` (independent path)

### 4.3 world_target LOADOP_LOAD optimization

Already works: `world_target` uses `LOADOP_LOAD` on subsequent frames (Phase 2i-B behavior). The dirty-tile enhancement means:

- **Zero tile changes**: Skip `render_world_pass_w` entirely (no `begin_pass`, no `flush_tile_sprites`). Tonemap re-reads last frame's content.
- **Partial changes**: Only re-draw submaps where `generation` changed. The GPU scissor rect limits draws to affected areas.
- **Full redraw**: Camera moved, zoom changed, or SDF rebuild → set `needs_clear = true` before `begin_pass`.

```cpp
// In render_world_pass_w():
bool needs_clear = wt->consume_dirty();  // first frame or resize
bool have_new_tiles = !rs.has_tile_sprites();

if( !needs_clear && !have_new_tiles && !camera_moved ) {
    return;  // preserve last frame's world_target
}
```

### 4.4 F4 diagnostics

Add to the diagnostics tab:
```
Dirty-tile:  submaps_v=121  changed=3  skipped=118
Tile queue:  enqueued=3412  reused=28910
Camera:      moved=no       zoom=1.0   steady=yes
```

### 4.5 Edge cases

| Case | Handling |
|---|---|
| First frame (initial state) | All `last_generation = UINT64_MAX` → full redraw |
| Submap loaded from disk | `generation = 0` on construction; `last_generation` already 0 → no false-positive rebuild |
| Submap shifted out of bubble | Submap removed from `submap_states_` (cleanup on map shift) |
| Explosion modifies 1 submap | Only that submap's generation changes → partial redraw |
| Fire/smoke changes per-tile transparency | `transparency_dirty` set → `transparency_generation` incremented → SDF rebuild triggers full redraw |
| window resize | `consume_dirty()` returns true → full clear + redraw |

---

## Phase 5: GPU JFA SDF (Jump Flood Algorithm)

**Goal**: move the Euclidean Distance Transform from CPU to GPU compute, freeing CPU render time (1-3ms avg).

### 5.1 Implement JFA compute shader

New file `src/lighting/jfa_sdf.comp.hlsl`:

```
[numthreads(8, 8, 1)]
void cs_main( uint2 id : SV_DispatchThreadId ) {
    // Pass 1 (seed): 0 for opaque, INF for transparent
    // Pass k+1 (flood): step = 2^(passes-1-k)
    //   sample (id.x ± step, id.y) and (id.x, id.y ± step)
    //   propagate min distance
    // After log2(max_dim)+1 passes: Euclidean SDF
}
```

**Input/Output**: Same buffer format as current SDF (supersampled by `SDF_SS=4`, x-major float array). The fragment shader is unmodified — it reads the same `SdfBuf` storage buffer.

**Dispatch parameters**:
- Input size: `map_w * SDF_SS` x `map_h * SDF_SS`
- Threads: `ceil(W/8) x ceil(H/8)`
- Passes: `ceil(log2(max(W, H) * SDF_SS))` + 1 seed pass

**Integration** in `build_and_submit_lighting()`:
```cpp
if( use_gpu_jfa ) {
    jfa_pass::record( cb, transparency_tex,
                      sdf_storage_, map_w, map_h );
} else {
    // Fallback CPU path
    lighting::compute_sdf_cpu( trans, w, h, sdf_storage_ );
}
```

The `use_gpu_jfa` is an F4 toggle for A/B comparison. After validation, remove the CPU path.

### 5.2 Remove CPU SDF path

After validation, delete:
- `compute_sdf_cpu()` in `sdf_pass.cpp`
- B1 region optimization code
- The old Chebyshev BFS code (already removed per CLAUDE.md, but confirm)

**Keep**: The `sdf_pass` buffer lifecycle (`sdf_storage_` allocation, upload to GPU). The buffer format doesn't change — only the computation moves to GPU.

### 5.3 Verification

| Metric | Current (CPU) | Target (GPU JFA) |
|---|---|---|
| SDF quality | Exact Euclidean (FH 2012) | Exact Euclidean (JFA) |
| Penumbra | Round, bilinearly smooth | Round, same bilinear read |
| CPU cost | +1-3ms avg | ~0ms |
| GPU cost | 0 | +0.1-0.3ms compute dispatch |
| F4 overlay mode 6 | Euclidean distance gradient | Identical gradient |

**Visual regression**: Compare frame-by-frame with `use_gpu_jfa` toggled in the F4 panel. Any difference > 0.01f in SDF values is a bug.

### 5.4 Edge cases

| Case | Handling |
|---|---|
| map_w/map_h < 2 | Skip JFA; set SDF to all zero (no occluders) |
| SS=4 gives non-power-of-2 dims | JFA works on any dimension; last jumps handle non-power-of-2 |
| GPU doesn't support compute | Fall back to CPU path (gated by device capabilities) |
| Frame-perfect shadow alignment | JFA output must bit-exact match CPU DT within float precision |

---

## Phase 6: 3D FOV occlusion culling

**Goal**: don't brute-force every z-level through solid floors. Skip rendering below the lowest open-air z in each tile column.

### 6.1 Per-submap floor height map

Add to `submap.h`:
```cpp
std::array<std::array<int8_t, SEEY>, SEEX> lowest_open_z;
// Initialized to OVERMAP_HEIGHT (e.g., 10) — "open above here"
// Built from floor_cache across all loaded z-levels
```

**Algorithm**: For each loaded z-level, for each submap in the bubble:
```cpp
void map::rebuild_lowest_open_z( int submap_sx, int submap_sy ) {
    auto &sm = get_submap_at_grid( {submap_sx, submap_sy, any_z} );
    for( int y = 0; y < SEEY; ++y ) {
        for( int x = 0; x < SEEX; ++x ) {
            if( sm->floor_cache[x][y] == 0 ) {
                // Open air → update lowest_open_z
                int abs_z = absolute_z_of_this_submap;
                sm->lowest_open_z[x][y] =
                    std::max( sm->lowest_open_z[x][y],
                              static_cast<int8_t>( abs_z ) );
            }
        }
    }
}
```

**Compute trigger**: Piggyback on `floor_cache` rebuild. When `set_floor_cache_dirty()` is called for a tile, recompute `lowest_open_z` for that column.

> ⚠️ **Review (C1) — storage-ownership ambiguous.** `lowest_open_z` is a **per-column, cross-z** value, but it is declared on `submap` (single-z). The algorithm loops over z-levels writing `sm->lowest_open_z[x][y]` without saying *which* z-level's submap owns the array, nor how a z=0 submap's value is read when the draw loop is at z=−2. Confirmed: the per-tile source data exists — `submap` has `char floor_cache[SEEX][SEEY]` (`submap.h:312`). **Resolve before coding 6.1:** give the column-summary a single home — e.g. store on the column's **top** submap, or hang it off `level_cache` keyed by xy. Also quantify the trigger cost: recomputing a column on each floor-dirty event walks every loaded z-level for that column; verify this doesn't erase the savings.

### 6.2 Culling in the z-loop

In `cata_tiles.cpp` z-loop (around line 3502):

```cpp
// BEFORE existing descent:
bool occluding_floor_below = false;

// Phase 6: per-column lowest-open-z culling
if( fov_3d_occlusion && z < center.z() ) {
    int sx = temp_x / SEEX;
    int sy = temp_y / SEEY;
    int mi = temp_x % SEEX;
    int mj = temp_y % SEEY;

    // Get the submap for this column
    auto *sm = here.get_submap_at_grid(
        { sx, sy, compute_submap_z( z ) } );
    if( sm && sm->lowest_open_z[mi][mj] > z ) {
        // Solid floor below this z → skip this and all lower z
        break;
    }
}
```

**Guarded by `fov_3d_occlusion`**: When the option is off, skip the check and use existing behaviors.

### 6.3 Edge cases

| Case | Handling |
|---|---|
| Open pit (all z open) | `lowest_open_z` = OVERMAP_HEIGHT → never culled |
| Deep basement (-1, -2, -3) | Each solid floor sets a lower bound; culling stops at first floor |
| Partial floor (hole in middle) | Per-column tracking handles correctly — hole column keeps rendering |
| Map shifts (bubble moves) | `lowest_open_z` rebuilt during `map::load()` when `floor_cache_dirty` bits set |
| Vehicle roof floors | `vehicle_floor_cache` is separate from terrain floor_cache; Phase 6 only culls terrain floors initially |

### 6.4 Verification

| Test | Expectation |
|---|---|
| Standing on floor in room | Only current z-level rendered (rest is behind solid floor) |
| Standing on open pit, z_range=10 | All 10 z-levels visible (open air column) |
| `fov_3d_occlusion=off` | Exact same visual output as before |
| Dense building with multiple floors | -20-50% fewer sprites per frame |
| Z-level with glass floor | `TFLAG_NO_FLOOR` and `TFLAG_Z_TRANSPARENT` treated correctly |

---

## Dependency graph

```
Phase 1 ──────────────────────────────────────────────────────────────
(camera_2d class)   │
                    ├── Phase 2 (smart follow) — needs camera_2d::update()
                    ├── Phase 3 (minimap) — needs cata_tiles::draw(camera_2d const&)
                    │
Phase 4 (dirty-tile) ── depends on submap::generation counter only
                    │   (no camera dependency)
Phase 5 (JFA SDF)   ── depends on transparency_generation (existing)
                    │   (no camera dependency)
Phase 6 (z-culling) ── depends on submap::lowest_open_z + fov_3d_occlusion
                        (no camera dependency)
```

**Parallelizable groups**:
- Group A: Phase 1 (mandatory)
- Group B: Phase 2 + Phase 3 (both depend on Phase 1)
- Group C: Phase 4, Phase 5, Phase 6 (independent of each other and of Phase 1/2/3)

Phase 4 and Phase 5 require the submap generation counter from 4.1 — if Phase 5 is done by a different developer, they need to agree on the `submap::generation` interface.

---

## Execution order

```
branch: scoping-refactor/camera-2d-class     Phase 1  (8-12 files, high-touch)
  └── mandatory before any other branch

branch: feat/camera-smart-follow              Phase 2  (additive, ~5 files)
branch: feat/camera-gpu-minimap              Phase 3  (replace pixel_minimap, ~10 files)

  ── Phase 2 and Phase 3 can be done in parallel ──

branch: perf/camera-dirty-tile               Phase 4  (pure addition, ~4 files)
branch: perf/gpu-jfa-sdf                    Phase 5  (new shader, ~3 files)
branch: perf/3d-fov-occlusion               Phase 6  (pure addition, ~3 files)

  ── Phases 4-6 can be done in any order, in parallel with 2-3 ──
```

---

## Risk register

| # | Risk | Impact | Likelihood | Mitigation |
|---|------|--------|------------|------------|
| 1 | Phase 1 changes pixel output (different `cam_off` → wrong shadows) | Visual regression | Medium | Side-by-side screenshot diff; automated readback comparison |
| 2 | `view_offset` save/restore breaks a modal UI (zones, vehicle, aim) | Camera jumps on modal exit | Medium | Test every modal before/after; write a smoke-test script |
| 3 | `driving_view_offset` additive composition not preserved | Driving view offset snaps | Medium | `nudge()` + velocity model exactly replicates `view_offset = manual + driving` invariant |
| 4 | `is_steady()` with dead zone removes needed SDF rebuild | Stale shadows | Low | Dead zone check must also account for emitter changes (fire, explosions). Gate on `transparency_generation` as safety net. |
| 5 | Zoom breaks lighting (camera_off formula) | Shadows misaligned | High | `get_view_matrix()` includes zoom; lighting pipeline already uses `tile_pixel_size` for scale |
| 6 | Minimap OMT tiles don't match pixel minimap colors | Ugly minimap | Medium | Side-by-side screenshot comparison; iterate OMT -> tile_color mapping |
| 7 | Dirty-tile world_target accumulation shows stale artifacts | Flicker / ghosting | Medium | Force full redraw on any ambiguity (camera moved, zoom change, SDF rebuild) |
| 8 | JFA output doesn't match CPU DT | Visual regression in shadows | Low | A/B toggle in F4 panel; compare overlay mode 6 before merging |
| 9 | Z-culling misses floors due to vehicle roofs | Cut-off rendering in garages | Medium | Initially exclude vehicle floors; add vehicle_floor_cache support in a follow-up |
| 10 | Camera debug_params added to GPU cbuffer break wire-stability | Shader compile error | High | Camera knobs are CPU-only; new struct, NOT added to `debug_params` in `sprite_batcher.h` |

---

## File change summary

| File | Phase | Change |
|------|-------|--------|
| `src/camera_2d.h` | 1 | **New** — class declaration |
| `src/camera_2d.cpp` | 1 | **New** — implementation |
| `src/avatar.h` | 1 | Remove `view_offset` (move z-only to avatar) |
| `src/avatar.cpp` | 1 | Remove view_offset mutation helpers; **add real `set_view_offset()` (A2)** |
| `src/iuse.cpp` | 1 | **(added, A1)** Migrate remote-vehicle view_offset save/restore — read `:7977`, write `:8011-8012` |
| `src/ranged.cpp` | 1 | **(added, A2)** Aiming writes (`target_ui::set_view_offset` private) route to camera |
| `src/game.h` | 1,2,3 | Add `main_camera_`, `minimap_camera_`; remove `driving_view_offset` (or deprecate) |
| `src/game.cpp` | 1,2,3 | Replace all `view_offset`/`driving_view_offset` logic |
| `src/cata_tiles.h` | 1,3 | Remove `o`, `op`; new `draw(camera_2d const&, …)` signature; add `draw_minimap()` |
| `src/cata_tiles.cpp` | 1,3 | Replace all `o`/`op`/`player_to_screen()` usages with camera; add minimap draw path |
| `src/sdl_render_frame.cpp` | 1,2,4 | Read camera for lighting + B1; gate rebuild on `is_steady()`; dirty-tile check |
| `src/sdl_curses_draw.cpp` | 1,3 | Pass camera to tilecontext::draw(); remove pixel minimap path |
| `src/cached_options.h` | 2 | Rename `pixel_minimap_option` → `minimap_camera_option` |
| `src/options.cpp` | 2 | Add `CAMERA_DEAD_ZONE`; rename minimap options |
| `src/lighting/sprite_batcher.h` | 2 | Camera knobs are CPU-only — no change to `debug_params` |
| `src/sdl_lighting_devui.h` | 2 | Add camera debug globals |
| `src/sdl_lighting_devui.cpp` | 2 | Add "Camera" tab with ImGui sliders + RmlUi bindings |
| `src/lighting/frame_build.cpp` | 5 | JFA dispatch replaces `compute_sdf_cpu()` |
| `src/lighting/jfa_sdf.comp.hlsl` | 5 | **New** — JFA compute shader |
| `src/submap.h` | 4,6 | Add `generation` (uint64) + `lowest_open_z` (int8_t[SEEX][SEEY]) |
| `src/submap.cpp` | 4,6 | Increment generation in setters; rebuild lowest_open_z |
| `src/map.cpp` | 4,6 | Propagate generation through dirty flag chain |
| `src/pixel_minimap.h` | 3 | **Delete** |
| `src/pixel_minimap.cpp` | 3 | **Delete** |
| `src/pixel_minimap_projectors.h` | 3 | **Delete** |
| `src/pixel_minimap_projectors.cpp` | 3 | **Delete** |
| `src/sdl_geometry.h` | 3 | May become unused — check after deletion |
| `src/panels.cpp` | 3 | Remove pixel minimap height logic |
| `src/animation.cpp` | 3 | Remove `minimap_requires_animation()` |
| `src/explosion.cpp` | 3 | Remove temporary minimap disable |

Phase 1 is mandatory before any other phase. Phases 2-6 can be done in any order after Phase 1.
