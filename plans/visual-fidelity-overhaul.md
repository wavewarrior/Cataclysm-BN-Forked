# Visual Fidelity Overhaul — Eye-Catching Pixel Art Lighting at 60+ FPS

## Status: IN PROGRESS (2026-07-17)

## Context

The lighting pipeline already has strong foundations: GPU SDF soft shadows (4x supersampled), GPU compute GI (radiance cascade heritage), directional sky/sun with 3D coverage-occluder march, volumetric lit fog/sun shafts, bloom, AgX tonemap, alpha-Sobel normals + Lambert, GPU rain particles, ordered Bayer dither, vision FOV with falloff, and height depth pillars (DitW-style extrude). This plan elevates the visual quality from "good lighting system" to "eye-catching modern pixel art" while maintaining 60+ fps.

### Reference Games for Visual Target
- **Graveyard Keeper** — warm GK-style color grading, soft bloom halos, sprite relief
- **Stoneshard** — atmospheric darkness, ambient particles, strong vision falloff
- **Path of Exile 2** — radiance cascades GI (we already have this), emissive bloom
- **Core Keeper** — dust motes, firefly particles, warm underground ambiance
- **Caves of Qud** — pixel-art atmosphere, subtle post-processing

### Existing Plans Absorbed
- GRAVEYARD_KEEPER_VISUAL_PLAN.md §2 (normals), §4 (vegetation), §5 (foliage parting), §6 (grading) — all unstarted, now superseded by this plan
- LIGHTING_PERF_PLAN.md (vis rebuild split) — already partially done, remaining items folded into Wave 4
- LIGHTING_OPTIMIZATION_PLAN.md P4-P6 — optional defense items, folded into Wave 4

---

## Architecture: Current Render Pipeline

```
build_lighting()          — CPU: SDF/vis/skyvis/occ buffer builds (gated on transparency_generation)
flush_and_gather_rc()     — GPU compute: JFA SDF → sky/sun pass → GI field+bounce
assemble_light_inputs()   — Fill sun_params, debug_params, volumetric params
flush_shadow_casters()    — Silhouette sun-shadow mask (shadow.vert/frag, MAX blend)
render_world_pass_w()     — Main sprite pass (sprite.frag) → volumetric → bloom → rain → sound
tonemap_pass_t()          — AgX tonemapping (tonemap.frag)
composite_swapchain_pass_b() — Final composite (UI overlay, HUD, swapchain present)
```

Shaders are **runtime-loaded** from `data/shaders/lighting/src/` via `load_lighting_shader_source` — HLSL-only edits need NO C++ rebuild (hot-reload). C++ changes (cbuffer layout, new passes) need a build.

---

## Wave 1 — Shader Polish (HLSL-only, hot-reloadable, no rebuild)

### §1 Alpha-Shape Normals (replace albedo-luma Sobel)

**Current:** `surface_normal()` in sprite.frag uses 4-tap Sobel of **albedo luminance** → noisy, catches sprite artwork hatching not actual shape. Tall sprites forced flat.

**Change:** Swap gradient source from `dot(rgb, luma)` → **alpha channel** at a **widened tap radius** (~2-3 texels). Alpha defines the silhouette, so the bevel follows the sprite's actual shape — rounded armor, angular weapons, organic creatures. Remove/relax the tall-sprite flat override so trees/walls/furniture/creatures get relief.

**File:** `data/shaders/lighting/src/sprite.frag.hlsl` (surface_normal function ~line 337-368)

### §2 Enable + Tune SDF Ambient Occlusion

**Current:** `ao_strength` defaults to `0.0f` (OFF). The SDF cavity darkening code exists but is dormant.

**Change:** Set default `ao_strength = 0.35f` in debug_params (sprite_batcher.h). Instant depth perception — wall corners, furniture bases, doorways all darken slightly. Tune to taste.

**Files:** `src/lighting/sprite_batcher.h` (debug_params default), `data/shaders/lighting/src/sprite.frag.hlsl` (verify AO application path)

### §3 Color Grading in Tonemap

**Current:** AgX only. `grade_desat/cool/bright` applied in sprite.frag as a crude wash (desaturate + teal shift + brightness). No real grading.

**Change:** Add ASC-CDL grading AFTER AgX in `tonemap.frag.hlsl`:
- `out = pow(saturate(in * slope + offset), power)` per-RGB
- Temperature/tint shift (warm↔cool)
- Saturation adjustment `lerp(luma, rgb, sat)`
- Pivoted contrast `(c - 0.5) * contrast + 0.5`

New cbuffer `GradeParams` at `b1, space3` in tonemap.frag:
```hlsl
cbuffer GradeParams : register(b1, space3) {
    float3 cdl_slope;    float grade_pad0;
    float3 cdl_offset;   float grade_pad1;
    float3 cdl_power;    float grade_pad2;
    float  temperature;  // -1 cold .. +1 warm
    float  tint;         // green/magenta
    float  saturation;   // 0=grey .. 1=full .. 2=over
    float  contrast;     // 0.5=flat .. 1=normal .. 2=punchy
};
```

**Files:** `data/shaders/lighting/src/tonemap.frag.hlsl`, `src/lighting/tonemap_pass.h`, `src/lighting/tonemap_pass.cpp`, `src/sdl_render_frame.cpp` (push the new cbuffer)

### §4 Upgrade Bloom — Dual-Filter Kawase

**Current:** Single-scale 9-tap Gaussian at half-res (4 passes). Produces a uniform soft glow but no long-range light bleed.

**Change:** Replace with **dual-filter kawase** bloom:
- **Downsample chain** (4-5 levels): each level 2x smaller, 4-tap kawase downfilter
- **Upsample chain** (same levels): 8-tap kawase upfilter + additive blend with previous level
- Result: multi-scale glow (sharp near, diffuse far) from the same or fewer GPU passes, wider/softer spread. Industry standard (COD, Fortnite, most modern engines).

**Files:** New shaders `bloom_kawase_down.frag.hlsl`, `bloom_kawase_up.frag.hlsl`. Modify `src/lighting/bloom_pass.h`, `src/lighting/bloom_pass.cpp` (chain of mip levels instead of ping-pong).

### §5 Post-Processing Suite (Vignette + Film Grain + Chromatic Aberration)

**Current:** No post-processing beyond bloom + tonemap.

**Change:** Add to `tonemap.frag.hlsl` AFTER the AgX + grade:
1. **Vignette** — radial darkening from screen edges (1 - smoothstep(0.4, 0.9, dist²)). Subtle. Focuses the eye.
2. **Film grain** — blue-noise-based luminance noise scaled by `grain_amount` (~0.03). Prevents banding in dark areas and adds texture.
3. **Chromatic aberration** — sample R/G/B at slightly different UV offsets from centre (`ca_amount` ~0.002). Subtle lens imperfection = cinematic.

All three controlled by uniform knobs (default ON at subtle values, F4 tuneable, set to 0 to disable).

**File:** `data/shaders/lighting/src/tonemap.frag.hlsl`

---

## Wave 2 — Vegetation Life (vertex shader + C++ cbuffer)

### §6 Grow debug_params for New Knobs

**Current:** 152 bytes (38 floats). Needs room for vegetation + post knobs.

**Change:** Add these fields (grow to 192 B = 48 floats, 16-B aligned):
- `ripple_k` (§7 intra-sprite column desync)
- `gust_amp`, `gust_freq` (§7 multi-octave envelope)
- `part_radius`, `part_strength` (§8 player foliage parting)
- `grain_amount`, `ca_amount`, `vignette_amount` (§5 post FX)
- `bloom_quality` (§4 mip count)

**Files:** `src/lighting/sprite_batcher.h` (struct + static_assert), `src/lighting/sprite_batcher.cpp` (static_assert value, push sites), `data/shaders/lighting/src/sprite.frag.hlsl` + `sprite.vert.hlsl` (cbuffer mirrors), `src/sdl_lighting_devui.cpp` (sliders)

### §7 Multi-Octave Wind + Ripple

**Current:** Single `sin(anim_time * sway_freq + ph)` — uniform metronome look.

**Change:**
1. **Intra-sprite ripple:** `ph += c.x * ripple_k` → per-column UV desync → shear wave, not rigid slide
2. **Multi-octave wind:** `wind = sin(t*f+ph) + 0.5*sin(t*f*2.3+ph*1.7)` × slow gust envelope `(0.6 + 0.4*sin(anim_time*gust_freq)) * gust_amp`
3. Optional slight vertical canopy bob

**File:** `data/shaders/lighting/src/sprite.vert.hlsl` (sway block ~lines 145-155)

### §8 Player Foliage Parting

**Current:** `player_x/y` in vert cbuffer but **UNUSED** by sway block.

**Change:** Inside `if(swayw > 0)`, push foliage away from player:
```hlsl
float2 d = base_tile - float2(player_x, player_y);
float dist = length(d);
if(dist < part_radius) {
    float k = (1.0 - dist/part_radius) * part_strength;
    c_uv.x += normalize(d).x * k * bend;
    c_uv.y += k * bend * 0.3;
}
```

**File:** `data/shaders/lighting/src/sprite.vert.hlsl`

---

## Wave 3 — Ambient Atmosphere (new GPU pipeline)

### §9 World-Space Ambient Particles

**Goal:** Subtle environmental particles that react to lighting — dust motes (indoor/dim), fireflies (outdoor night), embers (near fire). World-locked (not screen-space).

**Architecture:** Reuse the existing `hud_particle_effect` GPU pipeline pattern (hud_particle.vert/frag) but render into `world_target` (not HUD) with world coordinates. CPU spawns particles based on environment (inside=dust, outside+night=fireflies, fire_field=embers). Each particle: position, velocity, size, colour, lifetime. Vertex shader billboards, fragment shader soft-circle alpha.

**Files:** New `ambient_particle_pass.{h,cpp}`, new `ambient_particle.vert.hlsl` + `ambient_particle.frag.hlsl`. Wire into `render_world_pass_w` after rain, before sound pulses.

### §10 Heat Distortion Near Fire

**Goal:** Screen-space UV warp near fire emitters (the shimmering air effect above hot surfaces).

**Architecture:** Post-pass reads the emitter buffer, for each fire-type emitter applies a UV perturbation (time-varying sin/cos noise) to the world_target sample. Intensity falls off with distance. Runs AFTER the main sprite pass, BEFORE bloom.

**File:** New `heat_distort.frag.hlsl`, minor C++ in `sdl_render_frame.cpp` to wire the pass.

---

## Wave 4 — Performance (measure → optimize)

### §11 Profile + Baseline

Run Tracy profiler with all new effects enabled. Measure per-pass GPU timing:
- sprite pass (the N² emitter loop)
- bloom passes (new kawase chain vs old 9-tap)
- compute passes (JFA SDF, sky/sun, GI field+bounce)
- tonemap (added grading + post math)

Target: full frame < 16.6ms (60 fps) on Apple M1 Pro. Budget per pass.

### §12 Optimize Bottlenecks

Based on measurement:
- **If emitter loop dominates:** Implement tile-binned light culling (Forward+, LIGHTING_OPT P6)
- **If bloom dominates:** Reduce mip count or skip frames
- **If SDF dominates:** Tighter region-limit or frame-amortise the JFA
- **If tonemap dominates:** Move grade to compute shader at lower res

### §13 Verify 60+ FPS

Run the game with:
- Dense urban night scene (many emitters + fog + rain)
- Horde combat (10+ glowing enemies)
- Peaceful forest (many sway sprites + fireflies)

All must hit 60fps on M1 Pro (the dev target). Win11/D3D12 is the release target (cannot gate here; verify when hardware available).

---

## Order of Execution

```
Wave 1 (§1-§5): All HLSL-only except §3's cbuffer and §4's bloom rewrite. Do first — biggest visual bang.
  §1 + §2: Independent, parallel
  §3: After §1/§2 (needs build for cbuffer)
  §4: Independent (bloom rewrite)
  §5: After §3 (rides tonemap cbuffer)

Wave 2 (§6-§8): C++ cbuffer growth + vertex shader. Do after Wave 1 builds.
  §6: First (ABI change)
  §7 + §8: After §6, independent

Wave 3 (§9-§10): New GPU pipelines. Do after Wave 2.
  §9: Independent
  §10: Independent

Wave 4 (§11-§13): After all effects land. Measure then optimize.
```

## Verification Per Wave

- **Wave 1:** Build. Launch game. Visual comparison before/after. F4 knobs for tuning. Screenshot comparison.
- **Wave 2:** Build. Walk through forest. Vegetation sways realistically, parts for player.
- **Wave 3:** Build. Stand in dark room — dust motes. Stand near fire — embers + heat shimmer. Night outdoors — fireflies.
- **Wave 4:** Tracy capture. All target scenes at 60+ fps.
