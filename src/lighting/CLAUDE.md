# Lighting module — architecture reference

**Phase**: 2i-B (bridge removed, GPU-only rendering)
**Backend**: SDL_GPU / SPIRV-Cross on D3D12 (Win11), Vulkan, Metal

---

## File map

| File | Role |
|---|---|
| `gpu_device.cpp/h` | SDL_GPU device lifecycle; swapchain format query |
| `sprite_batcher.cpp/h` | Instance-batched draw engine; one pass per batcher |
| `render_state.cpp/h` | **Singleton** (`get_render_state()`). Owns device, batchers, queues |
| `gpu_atlas.cpp/h` | GPU mirror of SDL_Texture atlas pages |
| `font_engine.cpp/h` | SDL_Surface→GPU glyph upload; per-glyph texture cache |
| `gpu_geometry.cpp/h` | 1×1 white texture (shared by UI rects and solid fills) |
| `shader_compiler.cpp/h` | HLSL→SPIRV-Cross compile; embedded `SPRITE_VERT_HLSL` in `sprite_batcher.cpp` |
| `occluder_capture.cpp/h` | Per-frame sprite-alpha footprints feeding the SDF seed |
| `palette_ramp.cpp/h` | Procedural per-palette shade ramps + OkLab row lookup |

---

## Grid-decoupled lighting (2026-08-01)

The shading pipeline was already continuous; the GRID came from its **inputs**. Eight
changes decouple them. Every one is runtime-revertible from the F4 Effects tab, and
every gameplay consumer (`fine_detail_vision_mod`, `Character::sight_range`,
`Creature::sees`, `map::ambient_light_at`, `map::sees`) is untouched.

1. **Art-texel light quantisation.** `shade_pos` snaps `light_pos` to the tileset
   texel lattice and is the sample position for every lighting read. Light is
   constant across an art texel instead of gradient-shaded across it at zoom > 1:1.
   The Bayer dither is re-keyed to the same lattice, so its cell is 4 art texels wide
   at every zoom (it used to scale with the zoomed pixel size). Knob: `light_quant`.
2. **Sprite-alpha occluders.** `occluder_capture` records, per frame, the footprint
   of every terrain/furniture/vpart sprite (quad centre + size + rotation in TILE
   units, plus the atlas rect). `occ_base.comp` seeds the old tile-square only where
   nothing was captured (off-camera fallback — load-bearing); `occ_raster.comp`
   inverts each quad's transform per subcell and averages 4x4 alpha taps into a
   coverage field; `jfa_seed.comp` converts coverage to occupancy through a
   world-locked Bayer threshold, so partial occluders seed a stable fraction of
   subcells and dapple. The SDF now carries the artwork's real silhouette instead of
   one binary flag per tile replicated into 64 subcells. Knob: `occ_soft_gain`.
   - occ_raster is dispatched ONCE PER ATLAS PAGE (one compute dispatch reads one
     texture), so `gpu_sdf_pass` sorts the quads by page. Atlas pages therefore carry
     `COMPUTE_STORAGE_READ` as well as `SAMPLER`.
   - The seed now depends on WHICH TILES WERE DRAWN, so the structure-rebuild gate in
     `sdl_render_frame.cpp` also fires once the camera drifts `SDF_CAM_DRIFT_TILES`.
3. **Sub-tile emitters.** `make_omni`/`make_cone` take FLOAT positions. Creature-carried
   lights add `anim_state.slide_offset_*` so a torch follows the sliding sprite;
   `flicker_seed` is keyed to the LOGICAL tile (`with_tile_seed`) so the phase does not
   jump mid-step. Emitters inside their own opaque tile are pushed 0.45 tile onto the
   wall face (`face_offset`) so a wall lamp lights the corridor, not itself.
4. **Sub-tile vision carve.** `sprite.frag` marches the same SDF toward the player and
   multiplies the result in, so a wall corner cuts sight as a smooth curve instead of a
   tile staircase. It can only SUBTRACT inside a tile the CPU already granted, so it
   cannot reveal anything. The dead 4.5 MB `VisBuf` field it replaced (built, blurred
   and uploaded every player move for no reader) is deleted. Knobs: `vis_curve`,
   `vis_radius`.
5. **Bilateral GI upsample.** `indirect_bilinear` weights its four taps by SDF
   similarity so tile-res bounce does not cross a wall.
6. **Palette shade ramps.** `palette_ramp` histograms every tileset sheet, keeps the
   256 most frequent colours, generates 8 shades per row procedurally and bakes a 32^3
   OkLab nearest-row lookup. `sprite.frag` resolves the lit colour to a shade STEP in
   the texel's own ramp, so a red surface darkens toward dark red, not grey. Gated to
   lit world tiles (UI/fonts/memory keep their own colour) and to `ramp_enable`.
   - The shade step keys off `combined` (= `max(tint, gpu_total)`), NOT `gpu_total`:
     outdoors this engine lights tiles through the CPU lightmap tint, so keying off the
     GPU term alone crushes daylight to black.
   - `ramp_steps` is the BAKE REQUEST; `render_state::palette_steps()` is the
     authoritative row stride the shader indexes with. They must not diverge.
   - Ramp output is display-referred, so `tonemap.frag` lerps out AgX on
     `tm_ramp_enable` (which replaced `tm_pad` — both sides move together).
7. **Sub-tile vision FRONTIER.** The three remaining whole-tile vision treatments are
   feathered across the boundary tile instead of snapping on at its edge:
   * `lit_level::LOW` (the dim edge of sight) used to swap the sprite for a GREYSCALE
     atlas variant — a hard per-tile colour change, and the loudest artefact left. It
     now keeps the normal sprite; `sprite.frag` reproduces `color_pixel_grayscale`
     (`av = 85*(r+g+b)>>8`, `max(av*5>>3, 1)`) and ramps it in. The atlas is
     `R8G8B8A8_UNORM`, so the shader sees the very same encoded values the CPU filter
     did and a deep-LOW tile renders byte-identically (±1/255 truncation).
   * remembered tiles lerp BOTH `mem_dim` and the CPU tint passthrough, so the edge
     falls through to `gpu_total` and renders exactly as the visible neighbour does.
     Feathering the dim alone would ramp toward full UNLIT ALBEDO — a bright rim.
   * a never-seen tile's `lighting_*` overlay fades its alpha.
   `cata_tiles::frontier_mask` packs the 8 neighbours' "same side as me" bits and
   `sprite.frag` bilinearly interpolates the four corner means (marching squares).
   The remap MUST be `smoothstep(0.5, 1.0, cov)`: on a straight frontier both corners
   on the shared edge average to exactly 0.5, so any band centred on 0.5 leaves that
   edge at half-treatment against an untreated neighbour — a softer staircase, but a
   staircase. Anchoring at 0.5 keeps the whole feather INSIDE the treated tile.
   Knob: `vis_edge`. Debug view: `debug_mode == 15`.
   - The mask rides the NEGATIVE range of `sprite_instance.pad2` (the `outline` lane),
     which is otherwise only ever tested `> 0.5` for the hover silhouette — so this
     costs no new vertex attribute, cbuffer field or GPU buffer. Bit 8 selects
     desaturate vs hide/dim.
   - `draw_from_id_string` computes the shape (it has the map position) but
     `draw_tile_at` publishes the marker, because that is the single site that owns
     the decision to desaturate (goggles / underwater override it).

---

## Per-frame render pass (bridge-free single pass)

`refresh_display()` in `sdltiles.cpp`:

```
tile_batcher.begin_pass(cb, swapchain_tex, LOADOP_CLEAR=black)
  1. flush_tile_sprites(gpu_sampler)   ← terrain/mob/item/vehicle sprites
  2. flush_ui(gpu_sampler)             ← per-slice ordered: each ui_adaptor slice
                                          draws its rects (white) THEN its glyphs,
                                          slices in z-order, so overlapping windows
                                          occlude correctly; transient overlays last
tile_batcher.end_pass()
submit_frame()
```

UI flush is **per-adaptor-slice ordered**, not two-phase. `ui_manager` calls
`render_state::append_slice(rects, glyphs)` per adaptor in z-order (recording a
boundary in `ui_slice_spans_`); `flush_ui` replays each slice rects-then-glyphs so
a higher slice's opaque backgrounds hide lower slices' glyphs. The old all-rects-
then-all-glyphs flush submitted every glyph after every rect → overlapping windows
(e.g. targeting UI over the sidebar) mashed together. UI is alpha-blended, no depth
test → submission order is the only occlusion lever.

Bridge removed 2026-05-22. `gpu_sampler_` created eagerly in `render_state::init()`.

---

## Queue lifecycle

```
ui_adaptor::redraw_invalidated()        ← called on every UI redraw cycle
  → clear_frame_queues()                ← ALL 3 queues cleared at start
  → window callbacks run                ← populate ui_rect + font_glyph queues

cata_tiles::draw() (in-game map)        ← called as a window callback WITHIN redraw_invalidated()
  → clear_tile_queue()                  ← ONLY tile sprites; NOT ui/font (already cleared by outer cycle)
  → enqueue all terrain/mob/vehicle sprites

draw_om() (sdltiles.cpp, overmap)       ← also a window callback within redraw_invalidated()
  → clear_tile_queue()                  ← same: tile-only clear
  → enqueue overmap tile sprites
```

On **no-input frames** (between redraw cycles), `refresh_display()` re-drains the **same queues** from the previous cycle. This is intentional — prevents black flash when no redraw has run yet.

Do NOT clear queues inside a flush method.

**Known limitation**: Partial redraws (e.g. tooltip on mouse-move) trigger `redraw_invalidated()` → `clear_frame_queues()` → only tooltip repopulates → one-frame black sidebar. Root fix = GPU accumulation texture (future phase 2i-B-7g or similar).

---

## sprite_instance struct (96 bytes, wire-stable)

```
float dst_x, dst_y, dst_w, dst_h        // pixel-space destination quad
float src_u, src_v, src_uw, src_vh      // normalised UV (negative = flipped axis)
float tint_r, tint_g, tint_b, tint_a    // RGBA multiplier (1.0 = passthrough)
float rotation                          // radians, clockwise, around quad centre
float light_mul                         // <0 = memorized tile carrying -(dist in tiles)
float pad1                              // foliage sway weight (sprite.vert)
float pad2                              // >0.5 hover outline; <-0.5 encoded frontier mask
float extrude_px, extrude_dark, extrude_lean, extrude_pad  // height-depth pillar
float light_mode                        // sprite_light_mode: 0 unlit, 1 gpu_lit, 2 memory
float lm_pad0, lm_pad1, lm_pad2
```

Changing this struct requires updating `SpriteInstance` in BOTH
`data/shaders/lighting/src/sprite.vert.hlsl` AND `shadow.vert.hlsl` — the
silhouette-shadow batcher is a second `sprite_batcher` over the same instance
buffer, so a truncated declaration there indexes with the wrong stride.
`rain_droplet.vert.hlsl` / `splat_stamp.vert.hlsl` declare a look-alike struct
but read their own 64-byte `quad_instance` buffers and are unaffected.
`static_assert(sizeof(sprite_instance)==96)` enforces the contract.

---

## Rotation in vertex shader

Clockwise screen-space rotation (Y-down) around quad centre. `rotation` field is in radians (converted from `tile_sprite_options::rotation_degrees` by `enqueue_tile_sprite` in `cata_tiles.h`). Flip is UV-encoded (negative `src_uw`/`src_vh`), independent of rotation.

Formula: `x' = x*cos - y*sin`, `y' = x*sin + y*cos`

---

## sprite_batcher internals

- `MAX_INSTANCES = 65536`, `RING_SLOTS = 3` (storage-buffer ring)
- `cycle=true` on storage buffer upload (D3D12 frames-in-flight race fix)
- Blend: `SrcAlpha · src + (1−SrcAlpha) · dst`
- `set_texture()` closes current segment and starts a new one; same-texture calls extend current segment
- Null texture in a segment → D3D12 command list corruption → crash at next GPU call. Never pass null.

---

## Shadow march + dither model (2026-05-30)

Fragment shader (`SPRITE_FRAG_HLSL` in `sprite_batcher.cpp`):

- **Bilinear SDF sampling.** `sdf_bilinear()` 4-taps the tile-resolution SDF.
  The CPU SDF is a **Chebyshev** BFS (`sdf_pass.cpp:compute_sdf_cpu`) → integer
  per-tile jumps; raw nearest reads gave diamond-faceted sawtooth penumbrae.
  Bilinear smooths them. Cell-origin convention (`floor`/`frac`) — matches the
  old `(int)p.x`, so shadows stay glued to occluder edges. Residual faint
  squarish directionality is inherent to Chebyshev (Euclidean DT = future).
- **One shared `trace_shadow(origin, dir, dist, k, steps)`** for BOTH emitters
  and the sun (sun was a hardcoded k=4/16/8.0 copy). Sun passes `dist=8.0`
  (directional reach) + the `shadow_k`/`shadow_steps` knobs, keeps its
  `sky_vis>0.01` gate (can't reach roofed tiles).
- **Multi-band ordered (Bayer) dither**, world-locked. `dither_threshold()`
  keys a 4×4 Bayer matrix to **world ART TEXELS** (`shade_pos * texels_per_tile`,
  where `texels_per_tile` is the tileset's NATIVE tile width — 32 for MSX++, fed
  from `tileset::get_tile_width()`), so the pattern sticks to terrain (no shimmer
  on scroll) AND the 4×4 cell stays exactly 4 art texels wide at every zoom. It
  used to key to world *screen* pixels (`world_pos * tile_pixel_size`, the ZOOMED
  width), which made the dither cell grow and shrink with the zoom level.
  Quantises ONLY the dynamic light (emitter+sky+sun) into `dither_bands` levels;
  **ambient floor is added AFTER** (dithering the floor makes dark areas sparkle).
  Mean-preserving.
- **Art-texel light quantisation.** `shade_pos` snaps `light_pos` to the tileset
  texel lattice (`(floor(p * texels_per_tile) + 0.5) / texels_per_tile`) and is
  the sample position for EVERY lighting read (sky-vis, emitters, `trace_shadow`,
  sky/sun, GI, SDF debug views). Light is therefore constant across each art
  texel instead of gradient-shaded across it at zoom > 1:1. 1/32 tile is 4× finer
  than the 8-subcell SDF grid, so sub-tile shadow curvature survives. `light_quant`
  = 0 restores per-screen-pixel evaluation. `i.uv`, `i.world_pos` and the AO taps
  are deliberately NOT snapped.
- **Knobs** in `debug_params` (**208 bytes** as of 2026-08-01 — grew from 176 when
  the grid-decoupled lighting knobs were added; `static_assert` enforces 208):
  `dither_amt` (0=off), `dither_bands`, `light_quant`, `occ_soft_gain`,
  `self_eps_tall`, `ramp_enable`, `ramp_steps`, `ramp_chroma`.
  Widget: Shift+F8/F9 = strength, Ctrl+F8/F9 = bands.

## Colored indirect light / GI — now GPU COMPUTE (Stage 1, supersedes RC fragment)

GI is computed on the GPU by `gi_compute_pass` (Stage 1 of
`GI_COMPUTE_AND_PERF_PLAN.md`, 2026-06-18). It **replaced** the fragment
`radiance_cascade_pass` (+ `rc.frag`/`rc_bounce.frag`), which created on Metal
but failed `SDL_CreateGPUGraphicsPipeline` root-signature construction on D3D12
(a fragment storage buffer with no leading sampler). Compute uses a distinct
binding model that dodges it; the A0 spike confirmed compute creates+runs on
Win11/D3D12. The older CPU diffusion path was already deleted in Phase 4.

- **Two compute dispatches** (`gi_compute_pass.{h,cpp}` + `gi_field.comp.hlsl` +
  `gi_bounce.comp.hlsl`): pass 1 (field) — one thread/tile, gathers occluded
  emitter radiance (SDF sphere-march) → `field_buf_`; pass 2 (bounce) — one
  thread/tile, marches N=16 rays through the field (1/(1+t) falloff, stop at
  walls) → `gi_buf_`. `numthreads(8,8,1)`, dispatch ceil(W/8)×ceil(H/8). SDL_GPU
  inserts the compute→compute barrier on `field_buf_` and the
  compute-write→graphics-read barrier on `gi_buf_` automatically.
- **Buffers** are scalar `(RW)StructuredBuffer<float>`, 4 floats/tile (rgb+pad),
  tile-res, x-major `[(x*map_h+y)*4 + c]` — **no transpose** (unlike the old RC
  texture). Readonly inputs declared `StructuredBuffer` (not RW) so D3D12 doesn't
  default them read-write. The emitter + SDF buffers carry
  `COMPUTE_STORAGE_READ` (added to their create flags) so the field pass can bind
  them. Compute HLSL spaces: readonly storage `(tN,space0)`, RW `(uN,space1)`,
  uniform `(bN,space2)`.
- **Consumer** (`sprite.frag.hlsl`): `StructuredBuffer<float> GiBuf :
  register(t7, space2)` — the LAST fragment storage buffer (slot 5). Replaced the
  old `IndirectTex` storage texture; with GI off the storage textures,
  **`ShadowMask` is now the sole storage texture (t1)** and the 6 storage buffers
  are t2..t7 (Emitters/Sdf/SkyVis/Vis/SunSdf/Gi). `indirect_bilinear(p)` reads
  GiBuf scalar at tile-res with the `p-0.5` centre; `dyn += gi_strength *
  indirect_bilinear(light_pos) * ao`, gated `gi_strength>0.001 && sdf_map_w>0`.
  `render_state` feeds `gi().gi_buffer()` via the lighting god-call's `gi_buf`
  param (only the tile batcher; ui/shadow batchers pass null).
- **Driven** in `refresh_display` (`flush_and_gather_rc` in `sdl_render_frame.cpp`)
  after the emitter/SDF upload, before Pass W, under the `rc_rebuild`
  (=`fr.built_pertile`) dirty gate; `gi_buf_` retained on skip frames (RW bindings
  `cycle=false`). Knob: `debug_params.gi_strength` (Alt+F8/F9). Gather tuning
  (RC_DIRS/STEPS/STEP/START/WALL) are `gi_bounce.comp` constants.
- **Dev oracle**: F4 readback (`g_rc_readback`) → `gi().debug_log_stats()`
  synchronous GPU→CPU readback of `gi_buf_` (plain float32 now, no half decode),
  logging sum/max/nonzero/centroid to DC::Main.
- **vs old RC**: SDF read is now SS-correct (matches sprite.frag's `SDF_SS`, now 8)
  grid) where rc.frag mis-indexed the SS buffer as tile-res — so occlusion is
  more accurate, not bug-for-bug identical to the Metal RC.
- **Future (Stage 2)**: add the directional cascade hierarchy for sun/sky on this
  proven-parity compute base; promote gather constants to F4 knobs.

## Intended direction (grilled 2026-06-01 — see `LIGHTING_REWORK_PLAN.md`)

The CPU **Chebyshev BFS SDF** and CPU **per-tile diffusion GI** above are
**interim, not the design target** — they produce squarish penumbra + blocky
bounce. Approved retarget: **GPU JFA SDF** (Euclidean → round) and **Radiance
Cascades GI** (Bilinear-Fix), both as fragment ping-pong on a new **RGBA16F RT
backbone** (reuse `ui_composite_target` machinery + the stubbed
`sprite_batcher.color_target_format`). Sequencing: a pre-backbone cleanup commit
first — extract the `refresh_display` SDF/GI/vis block → `lighting/frame_build`,
add the dirty-gate (busted while F4 panel visible), and single-source the shaders
(externalize the live HLSL, delete the dead `data/shaders/lighting/src/*.hlsl`
copies). Then backbone + AgX tonemap (mandatory together) → JFA → RC → bloom/LUT.
Single-thread emitter collect is ratified (no dedicated thread). Full rationale +
roadmap in `LIGHTING_REWORK_PLAN.md`.

## Known invariants & gotchas

**1. No opaque fills before tile sprites.**
`geometry->rect` routes to `ui_rect_queue` which flushes **after** tile sprites. An opaque fill in `ui_rect_queue` will paint over terrain. Use LOADOP_CLEAR for full-screen black, not a queued rect.

**2. GPU atlas lookup (`find_gpu_texture_full`).** 
Returns `{nullptr,0,0}` if the SDL_Texture is not tracked as an atlas sheet. GPU miss → sprite invisible + D_WARNING log. Causes: sprite not packing through `copy_surface_to_dynamic_atlas`, or texture handle mismatch.

**3. `draw()` and `draw_om()` are window callbacks.**
Both run INSIDE a `redraw_invalidated()` cycle. `clear_frame_queues()` already ran before them. They call `clear_tile_queue()` only to reset tile sprites before re-enqueuing. Calling `clear_frame_queues()` here would wipe UI content the other window callbacks just populated.

**4. Loading image GPU path disabled.**
`loading_ui.cpp:409` has `if (false && cache->gpu_texture)`. The GPU path caused a D3D12 crash: `upload_surface_to_gpu_texture` submits on a separate CB; D3D12 may not complete the resource barrier to PIXEL_SHADER_RESOURCE before the render pass samples → command buffer corruption. Fix: upload on the render CB, or fence the separate CB before sampling.

**5. SDL_Renderer still alive.**
`copy_surface_to_dynamic_atlas` still creates SDL_Textures (used as lookup keys for `find_gpu_texture_full`). Cannot delete SDL_Renderer until atlas switches to a pure GPU key. Target: phase 2i-B-7f.

---

## What remains for 2i-B completion (phase 2i-B-7)

| Sub | State | Notes |
|---|---|---|
| 7b pixel_minimap | ⏳ NOT migrated, invisible | `render()` blits `main_tex` via `RenderCopy(renderer,…)` (pixel_minimap.cpp:324) to the no-op'd display_buffer target → invisible. The line-190 `queue_ui_rect` comment is stale (no such call); a May-22 GPU migration was reverted. Migrate or drop. |
| 7c loading image | 🟡 GPU path disabled | `if(false && …)` "DIAG: disabled to isolate crash" (loading_ui.cpp:398); legacy `RenderCopy` live but dead. Re-enable = upload on render CB / fence the copy CB. |
| 7d scissor/clip | ✅ done | `sprite_batcher::set_scissor` + `SDL_SetGPUScissor`. |
| 7e screenshot | ✅ done | `save_screenshot` GPU copy-pass readback (sdltiles.cpp:3920-3960); no `SDL_RenderReadPixels`. |
| 7f mechanical delete | ⏳ | `SDL_Renderer_Ptr renderer` + `SDL_CreateRenderer` still live (sdltiles.cpp:129,350,363); `set_displaybuffer_rendertarget()` is no-op'd `{}` but `display_buffer` refs remain (sdltiles.cpp:3781) — **not fully removed** (corrects the "7f Part A removed display_buffer" note below). Blocked on atlas pure-GPU key. |
| accumulation texture | future | GPU-side "previous frame" buffer to fix partial-redraw flicker |

---

## Recurring build friction & fixes (2026-05-22/23 session)

Every item below has bitten us and costs 1+ build cycles. Check here first.

### C++ API patterns that differ from what you'd expect

| Wrong | Right | Why |
|-------|-------|-----|
| `my_MAPSIZE * SEEX` (global) | `get_map().getmapsize() * SEEX` | `my_MAPSIZE` is a **member** of the `map` class, not a global. `SEEX`/`SEEY` are in `game_constants.h`. |
| `get_map()` in `render_state::init()` | hardcode `11 * SEEX` or use `get_option` | `g == nullptr` at WinCreate time — `get_map()` dereferences `g`. Use a safe default; lazy-init on first game load if exact size matters. |
| `ter_id.obj_ptr()` | `ter_id->light_emitted` | `int_id<ter_t>` has `operator->` returning `const ter_t*`. There is no `obj_ptr()` method. |
| `m.i_at(tripoint_bub_ms)` | `m.i_at(p.raw())` | `map::i_at` takes `point` or `tripoint`, not strong-type wrappers. Call `.raw()`. |
| `map::get_cache(z)` from outside map | `map::access_cache(z)` | `get_cache` is private; `access_cache` is the public const accessor. |
| `effect_onfire` (extern) | `static const efftype_id my_effect_onfire("onfire")` | It's a translation-unit static in `lightmap.cpp`. Define a local copy. |
| `std::ranges::for_each(field, lambda)` | plain range-based `for` | MSVC rejects `std::ranges::for_each` on `field` objects; use `for(const auto& [ftype, fentry] : field)`. |
| `get_option<int>("MAPSIZE")` | doesn't exist | The option is named differently (or doesn't exist). Use `get_map().getmapsize()`. |
| `display_buffer` (dead, not deleted) | `get_sdl_window_size()` | display_buffer rendertarget plumbing is no-op'd (`set_displaybuffer_rendertarget(){}`) but the symbol + refs remain (sdltiles.cpp:3781) — NOT deleted. Still: callers of `get_sdl_display_buffer_size()` → use `get_sdl_window_size()`. |

### `dbg` macro — lighting/ files must define it themselves

The `dbg(x)` macro (`#define dbg(x) DebugLogFL((x),DC::SDL)`) is NOT globally available. Every new `.cpp` in `src/lighting/` that wants to log must add:
```cpp
#define dbg(x) DebugLogFL((x),DC::SDL)
```
after its includes. Using `DebugLog(DL::Error)` directly fails because `DebugLog` takes TWO arguments (level + class). Always use `dbg(DL::Error) << "..."` inside lighting/ files.

### SDL_GPU HLSL register spaces (D3D12 / SPIRV-Cross)

| SDL_GPU API | HLSL register |
|---|---|
| `SDL_BindGPUVertexStorageBuffers(rp, N, ...)` | `register(tN, space0)` |
| `SDL_PushGPUVertexUniformData(cb, N, ...)` | `register(bN, space1)` |
| `SDL_BindGPUFragmentSamplers(rp, N, ...)` | `register(tN, space2)` + `register(sN, space2)` |
| `SDL_BindGPUFragmentStorageBuffers(rp, N, ...)` | `register(tN, space4)` |
| `SDL_PushGPUFragmentUniformData(cb, N, ...)` | `register(bN, space3)` |

### cata_tiles.h nested class trap

`cata_tiles` contains a nested struct/class with getters like `get_tile_width()`. Code added inside THAT nested class cannot access `cata_tiles` members like `point o` (declared in the outer class at line ~1243).

**Rule**: any new method that touches `cata_tiles` private members (`o`, `tile_width`, `screentile_width`, etc.) must go in the **outer** `cata_tiles` class body, using explicit `public:`/`private:` guards if the surrounding region is private:
```cpp
    public:
        point get_tile_map_origin() const { return o; }
    private:
```

### Forward declarations missing in lighting/ headers

Always forward-declare ALL SDL GPU types used in a lighting/ header. Common omissions:
- `struct SDL_GPUBuffer;` — needed when any field or return type is `SDL_GPUBuffer*`
- `struct SDL_GPUTransferBuffer;`
- `struct SDL_GPUCopyPass;`

`sdl_wrappers.h` is NOT included by lighting/ headers (by design — they're self-contained).

### SDF + SkyVis are FRAGMENT STORAGE BUFFERS, not sampler textures (Metal) — CRITICAL

**2026-05-30, build-verified.** SDL_shadercross @ 6b06e55c mis-binds sampler
textures on Metal: the upload reaches the GPU (readback confirms) but the
fragment shader's `Texture2D.Load`/`.Sample` returns **0 for every fragment**.
This silently zeroed the SDF shadow march (`s=0 → shadow=0`), which in-game
*killed all emitter light* (main-menu glow only worked because `sdf_map_w=0`
there skips the march). Mode 6 was uniform red, mode 7 uniform black.

Emitters moved off `Texture2D` → `StructuredBuffer` 2026-05-29; **SDF + SkyVis
followed 2026-05-30** (read `SdfBuf` / `SkyVisBuf`). Atlas (sampler slot 0) is
the ONLY sampler texture left; all per-tile lighting data is in storage buffers.
Confirmed: mode 6 shows a real red→green gradient, emitters light + shadow
in-game. (SkyVis migration restores sun/sky, which the broken `sky_vis=0` had
gated off entirely; sky_vis is sourced from `map::access_cache().outside_cache`,
1.0=open sky / 0.0=roofed.)

**Storage buffers hold the CPU array directly — no transpose.** Data is x-major
`arr[x*H+y]`; the buffer is a verbatim memcpy (SDF) or per-element float convert
(SkyVis), so the shader indexes `Buf[x*sdf_map_h + y]` (x=`(int)world.x`,
y=`(int)world.y`, both clamped). No row/col swap (that was only needed for the
row-major *texture* Load).

Fragment resource layout (space2), K=1 sampled texture: Atlas `t0` (sampler) |
Emitters `t1` (storage slot 0) | SdfBuf `t2` (storage slot 1) | SkyVisBuf `t3`
(storage slot 2). Storage slot N → register `t(K+N)`. All 3 storage buffers are
bound in ONE `SDL_BindGPUFragmentStorageBuffers(first_slot=0, …, 3)` call so a
later bind can't zero an earlier slot. **The `space4` row below is WRONG** —
that layout failed with E_INVALIDARG; working code uses space2.
`sdf_tex_`/`sky_vis_tex_` R8/R32F textures still exist in `sdf_pass` but are now
dead (no shader reads them) — removable in a later cleanup.

### cata_tiles coordinate system — CRITICAL for world_pos shader

`cata_tiles` has TWO distinct offset members:
- **`o`** (`point o`): leftmost/topmost visible tile's MAP INDEX in tile coordinates (e.g., 135 when player is at tile 150 with a 30-tile view). NOT in pixels.
- **`op`** (`point op`): pixel offset of the tile-drawing area from the window top-left (e.g., sidebar width in pixels). Set from `dest` rect at draw time.

Sprite screen position: `screen.x = (mx - o.x) * tile_width + op.x`

The correct world_pos → map-tile camera offset formula:
```
cam_off = op / tile_width - o
```
(solved from `world_pos = tile_tu - cam_off = mx + 0.5` for emitter matching)

The WRONG formula (early bug): `cam_off = o / tile_px + 0.5` — this was ~135 tiles off, making all emitters appear 135+ tiles away → zero attenuation → no visible lighting.

Getters: `get_tile_map_origin()` → `o`, `get_drawing_pixel_offset()` → `op`.

### Phase 5 CPU lightmap tint: guards required

The `gpu_light_r/g/b = lm[idx].max()` path in `draw_from_id_string` MUST have:
1. `g != nullptr` — `get_map()` is only safe when `g` exists  
2. `lum > 0.001f` — before `generate_lightmap` runs, `lm` is all zeros. Tiles reaching this code are guaranteed LIT by the draw loop, so `lum == 0` means lightmap not generated yet (main menu, loading). Fall back to white tint (1.0f).

Without guard 2, the main menu renders completely black.

### Phase 7 black screen — CONFIRMED FIX: use Texture2D for fragment emitter data

**Root cause** (confirmed by Win11 D3D12 debug.log):
```
lighting: render_state init failed: sprite_batcher pipeline:
  Could not create graphics pipeline state! The parameter is incorrect. (0x80070057)
```
`SDL_CreateGPUGraphicsPipeline` → `E_INVALIDARG`.

**Mechanism**: `init_render_state_on` (render_state.cpp:527) silently catches
the exception. `ready()` = `device_.ready()` only. `WinCreate` ignores the return
value. `gpu_sampler_` never set → all flush guards fail → LOADOP_CLEAR black only.

**Root cause**: SDL_shadercross @ 6b06e55c doesn't correctly reflect fragment
`StructuredBuffer` resources at HLSL `register(tN, space4)` for D3D12. The
root signature SDL_GPU generates has `num_storage_buffers=0` for fragment; the
DXIL shader has the SRV entries → descriptor layout mismatch → `E_INVALIDARG`.

**Fix applied**: moved emitter data and SDF to **Texture2D fragment samplers**
(`register(tN, space2)`, the same space as the Atlas). These are correctly
reflected by SDL_shadercross on all backends.

| Resource | Old (broken) | New (working) |
|----------|-------------|---------------|
| Emitters | `StructuredBuffer<GpuEmitter> : register(t0, space4)` | `Texture2D<float4> EmitterTex : register(t1, space2)` |
| SDF | `StructuredBuffer<float> SdfBuf : register(t1, space4)` | `Texture2D<float> SdfTex : register(t2, space2)` |

**EmitterTex layout**: 4×64 RGBA32F. Row = emitter index. Col 0 = (pos_x,pos_y,pos_z,radius). Col 1 = (r,g,b,falloff). Upload reuses `xfer_[write_slot_]` (same bytes as SSBO, since `sizeof(gpu_emitter)=64=4×float4=one texture row`).

**Rule**: Never use fragment `StructuredBuffer` (space4) with SDL_shadercross @ 6b06e55c on D3D12. Use Texture2D samplers (space2) for fragment-stage data access.

**Post-review fixes applied:**
- `sdf_map_h` added to `light_params` (replacing `lp_pad` — same size, `sizeof` unchanged). Passed from `rs.sdf().map_h()`. Shader now uses `sdf_map_h` for Y-coord clamping; square-map assumption eliminated.
- Null-sampler guard in `set_lighting_resources`: if textures non-null but `data_sampler` null, both textures cleared to null and `emitter_count`/`sdf_map_w/h` zeroed — prevents fragment shader looping over unbound sampler slot.
- Dead parameters `emitter_ssbo` and `sdf_buffer` removed from the entire delegation chain.
- `SDL_GPUTextureRegion` in emitter texture upload: all fields explicitly set (x,y,z,layer,mip_level).

**To restore proper StructuredBuffer support**: bump SDL_shadercross GIT_TAG in CMakeLists.txt to a commit that correctly reflects fragment storage buffers.

### MAX_INSTANCES must be large for 4K + minimap

At 4K with a large terminal sidebar + pixel_minimap (17K rects) + tile sprites, the old 65536 cap is hit constantly. Current value: **262144** (262144 × 64 bytes = 16 MB/ring slot). Do not reduce.

### Variable scope in upload functions

When adding a storage buffer upload alongside a texture upload, do NOT reference `byte_size` from the texture block — declare a fresh variable:
```cpp
const Uint32 sbuf_size = pixel_count * static_cast<Uint32>(sizeof(float));
```

---

## Build workflow

**Builds run fine on this Mac** (SDL_GPU/Metal, Ninja). Build + run locally:
```
cmake --build out/build/osx-arm-slim --target cataclysm-bn-tiles
```
Add `--clean-first` to force a full recompile if a binary seems stale. Sources
are `GLOB_RECURSE` with `CONFIGURE_DEPENDS`, so new files are picked up
automatically on the next build. The runtime log is at
`~/Library/Application Support/Cataclysm-BN/config/debug.log` (grep it directly;
note `DL::Debug` is filtered — use `dbg(DL::Info)` for diagnostics that must
appear). Win11 (MSVC/D3D12) is still the primary release target; cross-check
D3D12-specific behaviour there when relevant.

## Token-cost tips

- **Don't paste full game logs.** Tail ~200 lines; filter to `ERROR|WARN`.
- **Read files once then edit in sequence.**
- **Use `smart_outline` → `smart_unfold`** instead of reading large files whole.
- **This CLAUDE.md** exists so you don't have to re-discover the pipeline architecture every session.
- **CRITICAL**: read `project_rendering_pipeline.md` from the auto-memory directory at session start (path in main CLAUDE.md).
