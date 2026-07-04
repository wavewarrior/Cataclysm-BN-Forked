# Sub-Tile Camera Contract

## STATUS (reviewed 2026-06-27)

✅ **SHIPPED — ~100% implemented, contract honored by code. KEEP as reference spec.**
The one invariant holds: contract math lives at `cata_tiles.cpp:3209-3216` (plan said
`:3226-3228` — lines drifted, logic identical: floor center, push remainder to `op`, iso
excluded). Lighting reads the same `o`/`op` (`sdl_render_frame.cpp:208-209`, was `:404-407`).
`camera_2d.{h,cpp}`, `set_subtile_offset` (`cata_tiles.h:1328`), and the `draw_ter` wiring
(`game.cpp:4441-4447`) all match the integration table. Snap rules + zero-offset gate present.
Remaining: nothing in this slice. Deferred items (full view_offset migration, zoom, dead-zone
**game option**, minimap) stay deferred — accurate. Only stale data: a few line numbers above.

The lynchpin for "smooth beautiful lighting." This is the precise rule that lets
the camera scroll by fractional tiles while sprites and shadows stay locked
together. Get this wrong and shadows swim off geometry; get it right and the
whole effect is free.

## The one invariant

`cata_tiles::o` and `cata_tiles::op` are the **single shared source of truth**
for both consumers:

- **Sprites** — `player_to_screen(p) = (p - o) * tile_size + op`
  (`cata_tiles.cpp:7467`, non-iso branch).
- **Lighting** — `cam_off = op / tile_px - o`
  (`sdl_render_frame.cpp:404-407`, read via `get_tile_map_origin()` = `o` and
  `get_drawing_pixel_offset()` = `op`). The fragment shader does
  `map_pos = tile_tu - cam_off` and samples the SDF with **bilinear**
  interpolation (`sprite.frag.hlsl:158-174` `sdf_bilinear`).

Because both read the *same* `o` and `op`, any fractional offset injected into
that single pair moves sprites and shadows **together**. They physically cannot
desync — there is no second code path to keep in step.

`cam_off` is already `float`, and the SDF read is already bilinear. The pipeline
is already sub-tile capable. Today it never sees a fraction only because `o`/`op`
are computed from an integer center.

## The computation (the contract)

At the single site `cata_tiles.cpp:3226-3228`, given:

- `center` — the integer tile center (`g->ter_view_p`, unchanged; still drives
  z-loop, cursor, line-preview, footsteps — do **not** make it fractional).
- `(sub_x, sub_y)` — the fractional residual from `camera_2d`, range ~(-1, 1).
- `center_f = center + sub` — the conceptual fractional center.

Compute:

```
fx = floor(center_f.x)              // integer tile origin
fy = floor(center_f.y)
o  = (fx, fy) - (POSX, POSY)        // integer — tile loop + SDF region need this
op = dest - round(frac(center_f) * tile_size)   // frac = center_f - floor(center_f) ∈ [0,1)
```

Derivation: we want world coord `center_f` to land at the fixed viewport-center
pixel. A world tile `p` maps to `(p - center_f + (POSX,POSY)) * tile + dest`.
Forcing `o` integer (`= floor(center_f) - (POSX,POSY)`) and solving
`(p - o)*tile + op == (p - center_f + (POSX,POSY))*tile + dest` gives
`op = dest - frac(center_f) * tile`. ∎

`op` is integer pixels (rounded). That is fine and correct:
- Sprites are pixel-quantized anyway.
- Lighting reads the *same* rounded `op`, so it stays locked to sprites.
- Sub-pixel finer than 1/tile_px (~1/32 tile) is invisible.

## Invariants that must hold

1. **Zero offset ⇒ byte-identical to today.** When `sub == (0,0)` (smooth-follow
   disabled, snapped, or converged), `floor(center)=center`, `frac=0`, so
   `o = center - (POSX,POSY)`, `op = dest` — the exact current lines. This is the
   pixel-perfect gate: with follow speed 0, output must not change one pixel.

2. **`o` is always integer.** The tile-iteration loop, the SDF B1 occluder region
   (`frame_build.cpp:170-178`, origin from `o`), and visible-submap math all index
   on integer tiles. Never feed them the fraction.

3. **`center` (ter_view_p) stays integer.** Only `o`/`op` see the fraction.
   Everything that reads `ter_view_p` (cursor, footstep markers, draw_line,
   z-descent `for(z=center.z()...)`) keeps integer semantics.

4. **iso mode is excluded (for now).** `player_to_screen` iso branch projects onto
   a diamond grid (`tile_width/2`, `tile_width/4`); a cartesian `op` pixel shift
   does not equal the iso projection of a sub-tile world move, so smoothing would
   scroll diagonally-wrong. In iso, force `sub = (0,0)` and use the current path.
   Revisit with proper iso projection later.

## What does NOT rebuild on sub-tile scroll

Verified against current source:

- **SDF does not rebuild on camera pan.** It rebuilds only on
  `transparency_generation` change, z change, or map shift
  (`sdl_render_frame.cpp:153-158`). Sub-tile scroll just re-samples the existing
  SDF field at fractional `cam_off` → smooth, zero compute cost.
- The B1 region is derived from integer `o`; it only shifts when `o` crosses a
  tile boundary — same as today.

So smooth scroll is nearly free: no extra SDF/GI passes, just a fractional read.

## Snap rules (camera_2d)

Smoothing must **snap** (instant jump, `sub=0`) when the target moves far, or the
view would slide across the world:

- First frame / no prior center.
- `|target - smooth_center| > SNAP` (8 tiles) — teleport, z-change, modal
  save/restore of `view_offset`, debug possess.
- `looking == true` — look-around / aiming keep a crisp cursor-driven view.
- Follow speed ≤ 0 — disabled (pixel-perfect gate).

Within ~0.01 tile of target, snap-to-target to kill the infinite ease tail and
keep the steady state byte-identical.

## Integration points (this slice)

| Site | Change | Status |
|------|--------|--------|
| `src/camera_2d.{h,cpp}` | New. Owns smooth float center; `update(target, snap)`; `sub_x()/sub_y()`. | ✅ DONE (also gained look-ahead, dead-zone, shake) |
| `src/game.h` | `camera_2d main_camera_;` | ✅ DONE (`game.h:1152`) |
| `src/game.cpp` `draw_ter(center, looking, …)` | After `ter_view_p = center`: `main_camera_.update(center.xy().raw(), looking)`; `if(tilecontext) tilecontext->set_subtile_offset(sub_x, sub_y)`. | ✅ DONE (`game.cpp:4441-4447`) |
| `src/cata_tiles.h` | `float subtile_off_x_=0, subtile_off_y_=0;` + `set_subtile_offset()`. | ✅ DONE (`cata_tiles.h:1328,1342-1343`) |
| `src/cata_tiles.cpp:3226-3228` | Apply the contract (non-iso, non-zero only). | ✅ DONE (now `cata_tiles.cpp:3209-3216`) |

**Deferred (not this slice):** the full `view_offset` 17-site migration, zoom,
shake, dead-zone, minimap. `view_offset` remains the discrete target; the camera
only adds the smooth fractional residual on top. This delivers the visible
lighting win with a ~5-file blast radius instead of 12.
