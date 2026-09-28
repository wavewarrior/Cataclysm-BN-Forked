# Atmospheric Lighting + CPU/GPU Vision Coherence

## Context

Cataclysm-BN-Forked's renderer has a rich GPU lighting field (SDF sun shadows, GI
bounce, per-emitter falloff, bloom, AgX tonemap) that is
decoupled from the CPU per-tile vision/lightmap. The felt problem is twofold:

1. **Incoherence** — on a currently-visible (`VIS_CLEAR`) tile the GPU brightness
   (`gpu_total`) is computed with zero reference to the CPU's own light value for that
   tile, so a tile that barely crossed the CPU visibility threshold can render blazing.
   The CPU light value is already plumbed to the fragment shader as the `raw_light`
   varying (TEXCOORD9) but is **never read**.
2. **The scene looks dead / crushed** — three concrete, already-diagnosed render bugs:
   the palette ramp crushes lit terrain darker than *remembered* terrain (seen tiles look
   dimmer than memory); roofed interiors read near-zero sky access at noon; and there is
   no time-of-day mood or light motion.

Decisions taken with the user (do not revisit):
- **Direction:** make CPU and GPU *agree by fixing root causes*, and add a **gentle**
  (soft-knee, not hard-clamp) anti-overshoot guard using `raw_light`. The guard must
  never make legitimate tiles darker; it only tames extreme overshoot.
- **Scope:** the coherence fix **plus** an "alive/atmospheric, modern-engine retro-feel"
  pass: (a) palette-ramp fidelity, (b) indoor daylight portal scan, (c) day/night mood
  grade + light flicker, (d) volumetric god-rays / dust motes, (e) dither + optional CRT
  retro grade.

End state: seen terrain is no longer darker than remembered terrain; windowed interiors
are lit at daytime; fire/torch light flickers and the palette shifts with time of day;
window light shafts + faint motes read indoors; a subtle world-space dither (opt-in CRT
scanlines) gives the retro finish — all while CPU vision/gameplay math is **byte-identical
untouched** (the guard and every atmosphere touch are render-only, headless-safe, and
coop-checksum-neutral).

This plan supersedes the open recommendations in
`plans/gpu-cpu-lighting-vision-unification.md` (Option 3 is realized here as the soft
guard) and closes the render-side leads left in
`plans/gpu-daylight-black-scene-bisect-plan.md` §"Follow-up session 3" (palette ramp,
sky portal scan).

## Ground rules (apply to every step)

- **Render-only invariant.** No step may change any CPU path that feeds
  `Character::sight_range()`, `map::build_map_cache`, `visibility_cache`, monster AI, or
  `coop_world_checksum`. After the coherence step, `[vision]` and `[shadowcasting]` test
  output MUST be byte-identical to the pre-work baseline (see Verification). Flicker and
  day/night grade are cosmetic and must not enter `coop_world_checksum`
  (`src/coop_checksum.cpp:15-63`); confirm the emitter snapshot and grade are
  renderer-only (they are today).
- **Wire-stable structs.** `gpu_emitter` MUST stay 64 B (`src/lighting/gpu_emitter.h:49`),
  `sprite_instance` 112 B (`sprite_batcher.h:134`), `debug_params` 272 B
  (`sprite_batcher.cpp:62`), `grade_params` 80 B (`tonemap_pass.h:34`). Reuse the named
  reserved pads listed per step. New GPU-instanced passes (Step 6) use scalar-only instance
  structs, each with a `static_assert(sizeof(...)==N)` and a C++↔HLSL wire test copied from
  `tests/sprite_instance_wire_test.cpp`; no existing cbuffer needs to grow.
- **F4 knob hygiene (every step that adds, replaces, or reworks a knob).** Before adding a
  knob, grep `data/gui/devui.rml` + `src/sdl_lighting_devui.cpp` for an existing one and do
  NOT duplicate — there is already a world-space Bayer dither (`dither_amt`/`dither_bands`,
  tab-3 `:294-295`) and a full CRT block (`crt_*`, tab-0 `:102-109`). Each new knob gets an
  F4 slider + a `/tmp/cata_knob` name (plumbing: def `src/sdl_lighting_devui.cpp:79-118`,
  bind `:560-770`, markup `data/gui/devui.rml`, parse `src/sdl_input.cpp:528`) and MUST be
  zeroable to recover the exact prior frame. Panel rows use a clear, self-explanatory label
  (e.g. "window shaft intensity", not "shaft int"), sit under a named
  `<div class="devui-section">`, and get a one-line `<div class="devui-note">(…)</div>`
  description — that note is the panel's ONLY description idiom; there is NO `title=`/hover
  tooltip support (grep confirmed) and adding a tooltip system is out of scope. When a step
  REPLACES a feature, delete the old feature's knobs (externs, defs, binds, rml rows, `note`)
  in the SAME commit and set the new defaults to the intended shipping look, never leftover
  values. The exact tab/section/label/note/default for every new or changed knob is listed
  under "F4 panel changes" in Critical files & anchors.
- **New debug view** (only where noted): bump the `% 18u` cycle to `% 19u` at
  `src/sdl_input.cpp:724` and `src/sdl_lighting_devui.cpp:69`, and add its name to the
  list at `src/sdl_lighting_devui.cpp:973-977`.
- **HLSL storage buffers are read unconditionally** (D3D12 strips unread SBUFs and breaks
  the root signature — `sprite.frag.hlsl:1174-1177`). Never gate a buffer read behind an
  `if`; blend with `lerp`/mask instead. `nointerpolation` on interpolator varyings breaks
  D3D12 PSO creation (`sprite.vert.hlsl:160-179`, `0x80070057`) — never add it.
- **Build (HARD, per AGENTS.md):** always background, never killed, ≥1200 s:
  `cmake --preset osx-arm-slim && cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles`.
  On `osx-arm-slim` the fresh test binary is `./cata_test-tiles` at the repo root, NOT
  `out/build/osx-arm-slim/tests/…` (stale). `stat` mtime before trusting a result.
  Format before building: shaders/C++ via `cmake --build build --target format`.
- Each workstream below is one atomic Conventional Commit and leaves the tree building
  and tests green.

## Approach

Order: correctness first (1–3), then atmosphere (4–7). Steps 1,2 both touch the
`sprite.frag.hlsl` composite but on disjoint lines; do 2 then 1. Step 6 is independent of
3 (it sources shafts from the window-cone emitters already built CPU-side, not from
`SkyBuf`). Steps 4 and 7 both touch the tonemap grade; do 4 then 7.

### Step 1 — Palette-ramp fidelity (seen tiles stop being darker than memory)

Root cause (grounded): the stylized ramp darkens mid-luma lit tiles because the shade
curve floor is `shadow_value=0.28` with no identity step, and remembered terrain bypasses
the ramp entirely — so deep-remembered terrain (a plain `mem_mul` multiply) reads brighter
than ramp-crushed seen terrain. Secondary: texels whose colour is poorly represented by a
kept palette row snap to a foreign, often darker row.

Two coordinated edits (mechanical part + curve part), both behind the existing
`ramp_enable`/`ramp_steps`/`ramp_chroma` sliders so the whole thing is reversible:

1a. **Confidence-weighted plain-multiply fallback.** In
   `src/lighting/palette_ramp.cpp:170-196` (the 32³ OkLab nearest-row bake), `best_d` is
   the squared OkLab distance from the LUT cell to its chosen row and is currently
   discarded. Quantise it to a confidence in `[0,1]` (`conf = saturate(1 - sqrt(best_d) /
   CONF_MAX)`, `CONF_MAX` a new `ramp_gen_params` field defaulting `0.35`) and pack it into
   the **high bits** of the existing index uint: row index needs ≤13 bits (`PALETTE_ROWS
   =512`), leaving 19 bits — store `conf` in 8 bits at `<<24`. No new buffer/register, so
   `FRAG_SBUF_COUNT=7` (`src/lighting/frag_slots.h:12`) and the layout asserts
   (`sprite_batcher.cpp:410-419`) are untouched.
   - In `data/shaders/lighting/src/sprite.frag.hlsl`: at `:1179` unpack `pal_row = raw &
     0x1FFF`; unpack `conf = ((raw >> 24) & 0xFF)/255.0`. At `:1189-1190` multiply `conf`
     into `ramp_mask` so low-confidence texels fall back toward the exact plain-multiply
     `lit_rgb` (`albedo*rad_lit`, `:1145`) instead of a foreign ramp row. Reads stay
     unconditional.
   - Add `coverage_pct` + `mean_snap_dist` to `palette_ramp_data`
     (`src/lighting/palette_ramp.h:39-52`); log beside `tail_pct` at
     `src/lighting/render_state.cpp:681-687`.

1b. **Identity step in the shade curve.** In `src/lighting/palette_ramp.cpp:146-167`,
   after generating the `steps` shades, overwrite the step nearest the identity crossing
   with the exact authored colour (`pack_rgba8(hsv_to_rgb({.h=hsv.h, .s=hsv.s, .v=hsv.v}))`
   == `pack_rgba8(base)`), i.e. `val_mul=1, sat_mul=1, hue_shift=0` at that step. Change
   `ramp_gen_params` default `light_value` from `1.30` → `1.10` (`palette_ramp.h:31-37`)
   so the top step no longer over-brightens highlights by 30%. In the shader, bias
   `shade_i` so a tile at the daylight-reference luma retrieves the identity step: at
   `sprite.frag.hlsl:1181-1184`, after computing `shade_f`, add a small pull toward the
   identity step index as `luma(rad_lit)` approaches the reference (reference = `1.0`,
   HDR-lit tiles already saturate above it). Keep `shadow_value=0.28` (deep-shadow look
   intended).

Acceptance is measurable, not by eye (see Verification): at a fixed daylight scene, the
seen-terrain luma plateau must come within **≤8%** of the remembered-terrain plateau
(today it is inverted), and lit-scene terrain luma must recover toward the ramp-off value.

Edge cases: `PalIdxBuf` cells for colours the tileset never uses still resolve to a row
(dense LUT, no holes) — confidence just trends low there, which is correct (fallback to
multiply). `steps < 2` already clamped (`sprite.frag.hlsl:1180`). Rebuild triggers
unchanged (`cata_tiles_tileset.cpp:609`, slider `sdl_render_frame.cpp:799-809`).

### Step 2 — Gentle anti-overshoot guard (`raw_light` → `gpu_total`)

Wire the dead `raw_light` varying into `gpu_total` as a **soft knee**, gated to
`gpu_lit` tiles, that only tames extreme overshoot and never darkens a legitimately-lit
tile.

- File: `data/shaders/lighting/src/sprite.frag.hlsl`. Insert a new block immediately
  **after** the `gpu_total` definition (`:1091`) and **before** `albedo` (`:1110`), so it
  precedes `rad_lit` (`:1144`), the palette ramp (`:1178-1190`), and debug view 5
  (`:1289`).
- Conversion (no existing bridge exists; author these as shader `static const`, mirroring
  the values in `src/lightmap.h:16-22`, with a comment naming the source):
  `CPU_LIT = 10.0` (`LIGHT_AMBIENT_LIT`), `CPU_LOW = 3.5` (`LIGHT_AMBIENT_LOW`).
  `float cpu_norm = saturate((i.raw_light - CPU_LOW) / (CPU_LIT - CPU_LOW));`
  → 0 at the visibility floor, 1 at "brightly lit". Daylight tiles read ≫10 so
  `cpu_norm` saturates to 1 and the guard is inert there.
- Ceiling + soft knee:
  `float cap = lerp(GUARD_FLOOR, 2.0, cpu_norm);` with `static const float GUARD_FLOOR =
  1.0;` — a barely-visible tile (`cpu_norm≈0`) is softly pulled toward ~1.0; a well-lit
  tile keeps the full 0–2 range.
  `float g = luma(gpu_total);`
  `if (i.raw_light > 0.0 && mode_gpu_lit && g > cap) { gpu_total *= lerp(1.0, cap/g,
  guard_amount); }`
  where `guard_amount` is a **new F4 knob** stored in the free pad
  `debug_params::cutout_pad1` (`sprite_batcher.h:338`), default `0.5`, range `[0,1]`,
  `/tmp/cata_knob` name `guard_amount`. `guard_amount=0` reproduces today's frame exactly.
- Critical edge case: `raw_light == 0.0` is overloaded — it means *both* "no game /
  off-map / non-tile sprite" *and* a genuinely pitch-dark tile. The `i.raw_light > 0.0`
  test means the guard NEVER fires at 0, so it can never black out a legitimately dark
  `VIS_CLEAR` tile (the exact regression the composite rewrite removed,
  `sprite.frag.hlsl:1092-1105`). `mode_gpu_lit` (`:515`) excludes UI/fonts/memory/unlit.
- Do NOT add `nointerpolation` to `raw_light` (it is a per-instance constant, interpolation
  is already exact; the qualifier breaks D3D12 PSO creation). `raw_light` is a quantity,
  not a band-compared selector, so no band test is needed.

No CPU change; `gpu_raw_light` is already populated at `src/cata_tiles.cpp:2165-2166` and
pinned by `tests/sprite_instance_wire_test.cpp:49`.

### Step 3 — Indoor daylight portal scan + un-double-darken interiors

Root cause: `sky_admit` (`data/shaders/lighting/src/sky_sun.comp.hlsl:94-111`) samples
only 8 binary dome directions at a fixed reach (`:224-229`), so a 1-tile window is
chronically missed and roofed interiors read ~0 sky access → look dead at noon.

- **Portal scan for roofed tiles.** In `sky_sun.comp.hlsl`, for a probe whose own tile is
  roofed (`roof_at >= 0.5`, `:84-90`; the roof bit is already in `OccBuf.c1`), replace the
  8-way binary average with a denser distance-weighted scan: march `portal_dirs`
  directions (new knob, default 16) up to `portal_reach` tiles (new knob, default 8),
  accumulate a soft contribution `1/(1+dist)` for each direction that reaches an open-sky
  tile (`roof_at<0.5 && occ_height<SKY_WALL_H`), normalise, and write to `SkyBuf.rgb`.
  Outdoor/open probes keep the existing cheap dome path (branch on the probe's own
  `roof_at`). New knobs go in the free `SkySunParams` pads `ssp_pad0`/`ssp_pad1`
  (`src/lighting/sky_sun_pass.h:33-34`; mirror in the HLSL cbuffer `:41-55`), F4 sliders +
  `/tmp/cata_knob` names `portal_dirs`, `portal_reach`.
- **Stop double-darkening near-window interiors.** In `sprite.frag.hlsl`, gate the
  post-clamp shadow multiply by sky access: change `sun_shad_mul` (`:1088-1089`) to
  `sun_shad_mul = lerp(1.0, sun_shad_mul, sun_sky_vis);` so sun-shadow darkening only
  applies where the sky is actually visible, sparing interior tiles lit purely through a
  portal (which contribute via `sky_contrib`, `:844`).
- The denser field automatically improves indoor GI bounce (`gi_field.comp.hlsl:161-166`
  reads the same `SkyBuf`) with no change there.
- **Enable default.** `g_sky_sun_enable` defaults `false` (`sdl_lighting_devui.cpp:84`), so
  the whole pass — and this fix — is a silent no-op until enabled. Flip the ship default
  to `true` (`src/sdl_lighting_devui.h:61` / `:84`) **after** the perf check below.
- Perf: the pass runs only on rc-rebuild frames (`sdl_render_frame.cpp:490`,
  `:541-559`), `numthreads(8,8,1)`, grid `ceil(dim/8)`. The portal scan adds
  `portal_dirs*portal_reach` taps per roofed tile only. Measure the rc-rebuild frame cost
  on `osx-arm-slim` (see Verification). Contingency below if it regresses.

### Step 4 — Day/night mood grade

Drive the existing AgX-output grade with time of day so scenes shift warm at dawn/dusk,
cool at night, neutral-punchy at noon. No new uniform — reuse `grade_params`
`temperature`/`tint`/`saturation`/`contrast` (`tonemap_pass.h:25-28`), which today are
driven only by F4 sliders.

- In `src/sdl_render_frame.cpp`, next to `celestial_hour()` (`:462-466`), compute a
  time-of-day base grade from the hour scalar (0–24). Concrete curve (author as a small
  local helper returning a `grade_params` delta):
  - **Night** (hour <5 or ≥21): `temperature -0.06`, `tint +0.02`, `saturation ×0.85`,
    `contrast +0.03`.
  - **Dawn/dusk** (5–8, 18–21): `temperature +0.10`, `tint -0.02`, `saturation ×1.10`.
  - **Day** (8–18): neutral (0,0,×1,base). Interpolate linearly across the boundaries so
    it never pops.
- Fold this into the `grade_params` at the fill site `:1402-1418` as the **base**, then
  ADD the F4 slider values as a trim (so sliders still work for tuning). It inherits the
  existing `diagnostic_view_active()` grade bypass (`:1437-1441`), so debug views are
  unaffected. Applied in `tonemap.frag.hlsl:123-131` — no shader change needed.
- Headless/coop-safe: grade is renderer-only. `celestial_hour()` already honours
  `CBN_FORCE_SUN_HOUR` (`:441-460`), so A/B at a fixed hour is deterministic.

### Step 5 — Light flicker (fire/torch breathe; lamps steady)

Use the already-populated-but-dead `gpu_emitter::flicker_seed` (`gpu_emitter.h:45`,
seeded from the logical tile at `snapshot.cpp:96` / `with_tile_seed:114`) plus the
existing live `debug_params::anim_time` (`sprite_batcher.h:250`, wall-clock, already
driving foliage sway + cloud drift). Shader-side, so zero CPU/gameplay cost and no
determinism impact on gameplay state (matches the existing anim_time animation precedent).

- **Per-emitter amplitude** so lamps don't flicker: add `float flicker_amp` to
  `gpu_emitter` by renaming the reserved `pad0` (`:47`) — struct stays 64 B. Populate it
  in `src/lighting/snapshot.cpp:collect_zlev` per source kind: fire fields / open flames /
  torches / candles → `0.15`; electric lamps / steady terrain lights → `0.0` (use the
  existing per-source collection branches in `collect_zlev`, `snapshot.cpp:162+`; where a
  source is ambiguous, default `0.0` — steady). Mirror the field into the shader
  `GpuEmitter` struct (`sprite.frag.hlsl:42-47`, currently ends in `misc`) at the same
  64-byte offset.
- **Shader modulation.** In the two-pass emitter loop (`sprite.frag.hlsl:736-829`),
  multiply each emitter's contribution by
  `1.0 + flicker_amp * flicker_gain * (corr_vnoise(flicker_seed, anim_time) * 2 - 1)`
  (small helper: a 1-D value-noise in HLSL keyed on `flicker_seed` + `anim_time`; if no
  HLSL noise helper exists in the shader, add a tiny hash-based one — the C++ side already
  has `corr_vnoise` in `src/lighting/noise_utils.h` for reference semantics). `flicker_gain`
  is a **new F4 knob** in free pad `debug_params::cloud_pad1` (`sprite_batcher.h:346`),
  default `1.0`, `/tmp/cata_knob` name `flicker_gain`; `flicker_gain=0` freezes all light
  for A/B and vv.py determinism.

### Step 6 — God-ray shafts + dust motes (smoke-and-mirrors; replaces the SDF volumetric pass)

The SDF ray-march `volumetric_pass` is too expensive for a 2D top-down view and reads
weak. Delete it and replace with the industry-standard 2D fake-volumetric approach:
additive gradient **shaft sprites** through windows + a world-space **dust-mote** particle
system drifting inside the lit shafts (Terraria/Dead Cells-style layered light beams). This
is independent of Step 3 — shafts source from the per-window CONE emitters already built
CPU-side, not from `SkyBuf`.

**6a. Remove the SDF volumetric pass (clean cutover), in one commit:**
- Delete `src/lighting/volumetric_pass.h`, `src/lighting/volumetric_pass.cpp`,
  `data/shaders/lighting/src/vol.frag.hlsl` (no companion `.vert`; it reused
  `tonemap.vert.hlsl`).
- `src/lighting/render_state.h`: remove `#include "volumetric_pass.h"` `:41`, accessor
  `volumetric()` `:402-403`, member `volumetric_` `:568`.
- `src/lighting/render_state.cpp`: remove `volumetric_.init(...)` `:250` (+comment
  `:248-249`) and `volumetric_.shutdown()` `:313`.
- `src/sdl_render_frame.cpp`: remove static `g_vol_params` `:70` (+comment `:68-69`), the
  `assemble_light_inputs` fill block `:767-780`, the call site `:1189-1201`, and the
  volumetric mention in the `diagnostic_view_active()` doc comment `:77`.
- `src/sdl_lighting_devui.h`: remove externs
  `g_vol_enable/density/intensity/shadow/reach/indoor` `:81-89`.
  `src/sdl_lighting_devui.cpp`: remove their definitions `:103-109` and `c.Bind("vol_*")`
  `:592,594-598`. `data/gui/devui.rml`: delete the "Volumetric sun shafts (C2)" block
  `:147-153`.
- Docs: `src/lighting/CLAUDE.md:31` (pass-list row); `plans/THINGS THAT NEED FIXING.md`
  item #14 cites now-dead anchors (`volumetric_pass.h:47-51`) — rewrite it to the new
  shaft+dust implementation.
- **BLOCKER, do first:** `rain_effect` reads `g_vol_params.camera_off_x/y` +
  `tile_pixel_size` at `sdl_render_frame.cpp:1299-1301`. Repoint these to
  `s_emo.cam_off_x/cam_off_y/tile_px` (identical values, set `:690-692`) **before** deleting
  the static, or rain loses its projection.
- No CMake change (lighting HLSL is runtime-loaded via `load_lighting_shader_source`,
  `src/lighting/shader_compiler.cpp:42-47`; `data/shaders` is copied wholesale). No resize
  hook change (the pass owned no textures; new passes draw onto `world_target`).

**6b. God-ray shaft pass** — new `src/lighting/godray_shaft_pass.{h,cpp}` +
`data/shaders/lighting/src/godray_shaft.{vert,frag}.hlsl`, modelled verbatim on
`emitter_glow_pass` (additive `ONE/ONE`, `LOADOP_LOAD` onto `world_target`, runtime-loaded
shader, transfer→storage instance upload, `DrawGPUPrimitives(6, count)`).
- Instance `godray_shaft_instance` — scalar-only floats (mandatory rule,
  `emitter_glow_pass.h:32-40`): `cx, cy` (screen-px window centre), `dir_x, dir_y` (unit
  beam axis), `length_px, half_width_px, r, g, b, strength` → 10 floats / 40 B, with
  `static_assert(sizeof==40)`. `MAX_SHAFTS = 512`.
- Data source (no new detection): the per-window CONE emitters already built in
  `src/lighting/snapshot.cpp:189-252` (`make_cone` at `:249-251`, carrying `pos_x/y`,
  `cone_dir_x/y` inward normal, `r/g/b`, radius/lum). In `render_world_pass_w`, next to the
  emitter-glow instance builder (`sdl_render_frame.cpp:1222-1272`), filter `s_emo.snap` for
  `shape==emitter_shape::CONE` + z-matched + the same `VIS_CLEAR` gate the glow builder uses
  (`:1246-1253`); project centre with `s_emo.cam_off_x/y` and `tile_px`; set `dir` =
  `rs.current_sun().sun_dir_x/sun_dir_y` (light travel direction, `sprite_batcher.h:172-181`
  — the beam points into the room); `length_px` ∝ `tile_px * shaft_length_scale *
  cot(elevation)` (cot-elev precedent `sprite_batcher.cpp:281-282`; clamp an elevation floor
  so a near-horizon sun can't make an infinite beam); `strength` = cone luminance ×
  `shaft_intensity`.
- Shader: `godray_shaft.vert.hlsl` stretches the 6-vertex quad into an oriented beam (basis
  from `dir`, extents `length_px`×`half_width_px`, NDC Y-flip via `proj_w/proj_h` exactly as
  `emitter_glow.vert.hlsl`), passing a beam-space `uv`. `godray_shaft.frag.hlsl` is a
  procedural gradient (no texture asset exists): far-end fade `1 - saturate(uv.y)`, soft
  across width `saturate(1 - abs(uv.x))^2`, faint `anim_time` ripple; returns
  `float4(color*falloff*strength, 0.0)` so `ONE/ONE` leaves target alpha intact (the
  `emitter_glow.frag.hlsl` trick). Bloom (`:1285`) supplies the halo, so record shafts
  **before** bloom.
- Register: `render_state` member+accessor+init/shutdown next to `emitter_glow_`
  (`render_state.cpp:265,320`). Record in `render_world_pass_w` at the seam the deleted
  volumetric call vacated (before emitter glow / bloom).
- Knobs (replace the deleted `vol_*` block): `g_shaft_enable` (default `true`),
  `g_shaft_intensity` (default `0.6`), `g_shaft_length_scale` (default `1.0`),
  `g_shaft_width` (default `0.5`); externs `sdl_lighting_devui.h`, defs+binds
  `sdl_lighting_devui.cpp`, rows `devui.rml`, `/tmp/cata_knob` names `shaft_*`.
  `g_shaft_intensity=0` reproduces the post-removal frame.

**6c. Dust-mote system** — new `src/lighting/dust_mote_effect.{h,cpp}` +
`data/shaders/lighting/src/dust_mote.{vert,frag}.hlsl`, modelled on `rain_effect` (CPU
world-space pool, world→screen projection `((world+cam_off)*tile_px)`, GPU-instanced
procedural soft-dot quads, one `LOADOP_LOAD` pass onto `world_target`) with
`hud_particle_effect`'s real-dt spawn accumulator (`hud_particle_effect.cpp:424-455`) for
motion.
- Pool `dust_mote{world_x, world_y, vx, vy, age, max_age, brightness}`; `MAX_MOTES = 2048`;
  GPU instance scalar-only (`quad_instance` precedent `rain_effect.cpp:19-40`),
  premultiplied blend `ONE/ONE_MINUS_SRC_ALPHA` (rain's, `:47-62`).
- Spawn in **world simulation space** (motes linger when the beam moves — the correct
  choice for a moving beam) at random points along the active shaft instances built in 6b
  (reuse that list), so dust only appears where light actually falls. Motion: slow Brownian
  drift + faint bias along `-sun_dir` (rising in the light), dt-driven; fade in/out over
  `max_age`. A mote's `brightness`/colour is inherited from its origin shaft's `strength`,
  so it glows the light's colour.
- Record in `render_world_pass_w` after shafts, before bloom. No new render target → no
  resize hook.
- Knobs: `g_dust_enable` (default `true`), `g_dust_density` (default `0.5`), `g_dust_size`
  (default `0.5`), `g_dust_drift` (default `0.1`); `/tmp/cata_knob` names `dust_*`.
  `g_dust_enable=false` = no motes.
- FoW-safe by construction: both systems source only from `VIS_CLEAR` window shafts, never
  from an unguarded world query (the emitter-glow-pass lesson) — a shaft/mote can only
  appear where the player already sees the window.
- Add a C++↔HLSL wire test for `godray_shaft_instance` and the mote GPU instance, copying
  `tests/sprite_instance_wire_test.cpp` (`[lighting]`, no GPU device needed).

Budget/overflow: both pools are fixed-size (`MAX_SHAFTS`, `MAX_MOTES`); when a frame would
exceed the cap, keep the nearest-to-camera instances and drop the rest (distance sort /
early-out in the builder), matching `snapshot.cpp:567-571`'s truncation policy.

### Step 7 — Retro finish: reconcile the existing dither + CRT, extend CRT over the world

The "dither + CRT" retro finish is mostly ALREADY SHIPPED; the work here is to NOT duplicate
it (the exact trap this step guards against) and to close the one real gap.
- World-space ordered dither already exists: `dither_amt` (default 1.0) + `dither_bands`
  (default 32), the mean-preserving 4×4 Bayer dither on dynamic light
  (`sprite.frag.hlsl:384-388`, `dither_threshold`), exposed tab-3 "Dither / GI / shadow"
  (`devui.rml:294-295`). It already gives the banded-gradient retro look and Step 1's ramp
  reuses it — add NO new dither knob.
- A CRT post-effect already exists: `crt_enable` + `crt_scanline_alpha/pitch/thickness`,
  `crt_roll_speed`, `crt_flicker`, `crt_vignette`, driven by `crt_params g_crt`
  (`rmlui_layer.cpp:77`), applied as RCSS decorators (`:967-1181`), exposed tab-0 "CRT
  post-effect" (`devui.rml:102-109`). It is scoped to the RmlUi HUD panel only, NOT the map.

Close the real gap (no world-space CRT) by EXTENDING the existing CRT, never a second system:
- Add `bool crt_world = false;` to `crt_params` (`src/lighting/rmlui_layer.h:87-95`). When
  set, apply the same scanline + vignette to the tonemapped world: in
  `data/shaders/lighting/src/tonemap.frag.hlsl` `main` after grade (`:123-153`, before
  output), add a scanline+vignette term gated by a single `crt_world_amount` uniform packed
  into the free `grade_params::gp_pad0` (`tonemap_pass.h:32`; struct stays 80 B). Push it at
  the tonemap fill site (`sdl_render_frame.cpp:1402-1418`) as `g_crt.crt_world ?
  g_crt.scanline_alpha : 0.0`, with pitch reusing `g_crt.scanline_pitch`. `crt_world=false`
  ⇒ `crt_world_amount=0` ⇒ frame bit-identical to today. The RmlUi HUD CRT is untouched and
  cannot double-apply (tonemap runs on `world_target` only, resolved before UI compositing).
- Panel: add ONE checkbox row `world CRT (map scanlines)` bound to `crt_world` under the
  existing tab-0 "CRT post-effect" section (`devui.rml:103`), plus a `.devui-note` "(also
  draws the CRT scanlines/vignette over the map, reusing the sliders above)". No new sliders.

## Critical files & anchors

- `data/shaders/lighting/src/sprite.frag.hlsl` — composite: `gpu_total` `:1090-1091`
  (guard insert after `:1091`), `raw_light` decl `:236`, `mode_gpu_lit` `:515`, ramp apply
  `:1178-1190`, `mem_rgb` bypass `:1193`, emitter loop `:736-829`, `sun_shad_mul`
  `:1088-1089`, `GpuEmitter` mirror `:42-47`, `PalIdxBuf`/`RampBuf` `:90-91`,
  `dither_threshold` `:384-388`.
- `src/lighting/palette_ramp.cpp` — shade curve `:146-167` (identity step + `light_value`),
  OkLab bake `:170-196` (pack confidence in index uint).
- `src/lighting/snapshot.cpp` — `make_omni` seed `:96`, `with_tile_seed` `:114`,
  `collect_zlev` per-source emitter branches `:155-296` (set `flicker_amp`); the per-window
  CONE emitters `:189-252` (`make_cone` `:249-251`) are the god-ray shaft source.
- Step 6 templates / removal: `src/lighting/emitter_glow_pass.{h,cpp}` (shaft-pass template:
  32 B scalar instance, `ONE/ONE` additive, `VIS_CLEAR` gate), `src/lighting/rain_effect.cpp`
  + `hud_particle_effect.cpp:424-455` (dust-mote pool + dt spawn template), and the delete
  set `src/lighting/volumetric_pass.{h,cpp}` + `data/shaders/lighting/src/vol.frag.hlsl`.
  Sun direction: `sun_params.sun_dir_x/sun_dir_y` (`sprite_batcher.h:172-181`).
- `data/shaders/lighting/src/sky_sun.comp.hlsl` — `sky_admit` `:94-111`, dome loop
  `:224-229`, `roof_at` `:84-90`, `SkyBuf` store `:243-247`, `SkySunParams` pads `:47-48`.
- `src/sdl_render_frame.cpp` — grade fill `:1402-1418`, `celestial_hour()` `:462-466`,
  sky pass gate/dispatch `:541-559`, tonemap record `:1394-1446`.
- Knob/UI plumbing: `src/sdl_lighting_devui.cpp` (defs `:79-118`, binds `:560-770`,
  `g_sky_sun_enable` `:84`, dbg-mode cycle `:69`), `data/gui/devui.rml`,
  `src/sdl_input.cpp` (`/tmp/cata_knob` `:528`, dbg cycle `:724`).
- Wire-stability asserts: `gpu_emitter.h:49`, `sprite_batcher.cpp:62`,
  `tonemap_pass.h:34`, `frag_slots.h:12`; new `godray_shaft_instance` (==40 B) and the dust
  GPU instance each get their own `static_assert` + wire test.
- F4 panel changes (`data/gui/devui.rml` + `src/sdl_lighting_devui.{h,cpp}`) — every row a
  clear label under a named `.devui-section` with a one-line `.devui-note`:
  - Step 2 guard: tab-1, new row "vision overshoot guard" by the ramp rows (`:125-127`),
    default 0.5; note "(caps how far GPU light exceeds CPU vision on a just-visible tile; 0 =
    off)".
  - Step 3 portals: tab-3, rows "window portal dirs" / "window portal reach" under the
    sky/sun march block (after `:308`); relabel the `sky_sun_enable` row (`:303`) to
    "sky/sun + window portals" and default it on; note "(daylight seeping through windows;
    needs the march enabled)".
  - Step 4 day/night: tab-3 "Colour grade (ASC-CDL)" (`:265`) — NO new knob; add a
    `.devui-note` "(sliders TRIM an automatic time-of-day base grade)".
  - Step 5 flicker: tab-1, new "Light motion" section with row "light flicker", default 1.0;
    note "(fire/torch breathe; 0 = steady)".
  - Step 6: DELETE the entire "Volumetric sun shafts (C2)" block (`:147-153`) and its
    `g_vol_*` externs/defs/binds; ADD two tab-1 sections — "Window light shafts"
    (enable / intensity / length / width) and "Dust motes" (enable / density / size /
    drift), each with a `.devui-note`.
  - Step 7: tab-0 "CRT post-effect" — ADD one checkbox "world CRT (map scanlines)"
    (`crt_world`); reuse the existing CRT sliders; add NO dither/scanline sliders.

## Verification

Platform is macOS `osx-arm-slim`. `tools/visual_verify/vv.py` drives the **installed
Windows** build (per AGENTS.md) and does NOT run here — do not use it. On this Mac, capture
frames and drive the F4 knobs via the macOS skills (`cbn-lighting-knob-visual-proof`,
`cbn-shader-ab-determinism-gate`, `cbn-forced-debug-mode-pixel-proof`,
`cbn-macos-ui-verify-harness`), then measure with the platform-agnostic analysers
`tools/light_mode_check.py` (debug view 16) and `tools/frontier_profile.py` (seen/memory
luma plateaus) plus PIL/numpy pixel diffs on the captured frames — never eyeballed
screenshots. Every A/B toggles the step's own knob to 0 → prior frame, N → new, and gates
captures on identical resolution/zoom/world-state/hour (`CBN_FORCE_SUN_HOUR`).

Baseline first (once, before any edit): build, then
`./cata_test-tiles "[vision],[shadowcasting]" --rng-seed 1` and save the output. This is
the regression oracle for Step 2.

- **Coherence invariant (Step 2):** after the guard lands, rerun
  `./cata_test-tiles "[vision],[shadowcasting]" --rng-seed 1` — output MUST be
  byte-identical to baseline (guard is render-only). In-game: at a night scene with one
  torch, a tile just inside vision must not render blazing at `guard_amount=1` and must be
  bit-identical to today at `guard_amount=0` (debug view 5 = `gpu_total`, pixel diff).
- **Palette ramp (Step 1):** `tools/frontier_profile.py` at a daylight scene — seen-terrain
  luma plateau within ≤8% of the remembered plateau (today inverted); the new
  `coverage_pct` logs beside `tail_pct`. A/B on `ramp_enable`.
- **Portal scan (Step 3):** with `g_sky_sun_enable` on and a windowed room at
  `CBN_FORCE_SUN_HOUR=12`, interior tile luma rises measurably vs `portal_reach=0`; sun
  shadow no longer double-darkens near-window tiles. Record the rc-rebuild frame cost
  (existing `sim`/`render` perf path) before flipping the enable default.
- **Day/night (Step 4):** capture at forced hours 3/7/12/19; grade temperature/tint shift
  monotonically; `diagnostic_view_active()` frame unchanged.
- **Flicker (Step 5):** at a fire scene, successive frames differ near the flame at
  `flicker_gain=1` and are frame-identical at `flicker_gain=0`; a steady lamp scene shows
  no flicker (its `flicker_amp=0`).
- **God-rays/motes (Step 6):** after removal, `g_shaft_intensity=0` + `g_dust_enable=false`
  must reproduce the post-removal frame exactly. A windowed interior at
  `CBN_FORCE_SUN_HOUR=12` shows a beam from each visible window along the sun direction, with
  motes drifting inside it; both vanish at their knobs' zero. Confirm no content leak: a
  window/torch behind a wall the player cannot see produces no shaft or mote (VIS_CLEAR gate).
- **Retro finish (Step 7):** `crt_world` off → world frame bit-identical to today
  (`crt_world_amount`/`gp_pad0` = 0); `crt_world` on → scanlines + vignette appear over the
  MAP reusing the existing CRT sliders, while the RmlUi HUD CRT is unchanged (no
  double-apply). Confirm NO duplicate knobs were added: the panel still has exactly one
  dither block (tab-3) and one CRT block (tab-0, now with the world toggle).
- **F4 knob hygiene (all steps):** open the panel and confirm every knob added by this work
  has a clear label, sits under a named `.devui-section`, and carries a `.devui-note`; the
  six `g_vol_*` rows are gone; no knob name appears twice; and each new knob at its
  zero/default value reproduces the pre-step frame.
- **Whole-scene smoke:** launch the osx-arm-slim build into a save, walk indoors/outdoors
  across a dawn→noon→dusk span with a torch, confirm the composite reads coherent and
  alive with all defaults on.

Run the full `./cata_test-tiles "~[.]"` suite and `cmake --build build --target format`
once, after all steps land.

## Assumptions & contingencies

- **`g_sky_sun_enable` default flip (Step 3).** Assumed the portal scan's added
  rc-rebuild cost is acceptable on `osx-arm-slim` so the ship default can flip to `true`.
  If the measured rc-rebuild frame regresses beyond a playable budget, keep the default
  `false`, lower `portal_dirs`/`portal_reach` defaults until it fits, and document the
  quality/perf trade in the commit — do not ship an unconditional perf regression.
- **Flicker source classification (Step 5).** Assumed fire/flame/torch/candle emitters are
  distinguishable in `collect_zlev`'s existing per-source branches. If a source kind is not
  cleanly separable there, default its `flicker_amp` to `0.0` (steady) rather than
  guessing — a missed flicker is invisible, a wrongly-flickering lamp is a visible bug.
- **Final tuning constants** (guard_amount/floor, ramp curve values, grade curve deltas,
  mote/dither/scanline amounts, portal reach). Defaults above are starting points; each is
  an F4 knob, and the acceptance metrics (not taste) fix the shipped default. If a metric
  cannot be met by tuning within a step's knob range, report the measured gap rather than
  shipping a value that fails acceptance.
- **Repo convention:** once write access is restored, also copy this plan to
  `plans/atmospheric-lighting-coherence.md` (AGENTS.md requires a `plans/` record); the
  canonical working copy is this `local://` file.

## Execution notes (added post-implementation)

All seven steps landed. Deviations/clarifications from the literal spec, discovered during
implementation:

- **Step 6b beam direction:** implemented using each emitter's own `cone_dir_x/y` (the
  window's own inward normal, or a flashlight/headlight's own aim direction) rather than
  the global `sun_dir_x/y` the plan specified. This is strictly more correct — it also
  makes non-window CONE emitters (headlights, flashlights, caught by the same "reuse the
  CONE-emitter list wholesale, no new detection" instruction) point their beam the right
  way, and avoids the beam vanishing at night when `sun_dir` is near zero.
- **`/tmp/cata_knob` dispatcher:** the plan's ground rules require every new knob to get a
  `/tmp/cata_knob` name; this is a hardcoded `if/else` chain in `src/sdl_input.cpp` (a
  pre-existing headless verification mechanism) that must be extended per knob. All 13 new
  knobs (`guard_amount`, `portal_dirs`, `portal_reach`, `sky_sun_enable`, `flicker_gain`,
  `shaft_enable/intensity/length_scale/width`, `dust_enable/density/size/drift`,
  `crt_world`) are wired in.
- **Step 3 perf verdict:** measured on `osx-arm-slim` in a small (~180x180-tile) scene:
  rc-rebuild `frame_period` avg was 26.39ms (~37.9 fps) with the portal scan off and
  26.39–26.61ms (~37.5–37.9 fps) with it on — no measurable regression. `g_sky_sun_enable`
  ships `true` per the plan's contingency.
- **Formatter version drift:** the local `clang-format` (23.1.0) and `astyle` (3.6.16) do
  not match CI's pinned `clang-format` 22, so a repo-wide `cmake --build --target format`
  run reformats large swaths of unrelated files. Any future formatter run on this tree
  should be scoped to touched files only, or the unrelated churn reverted before commit
  (see `formatter-drift-attribution` skill).
