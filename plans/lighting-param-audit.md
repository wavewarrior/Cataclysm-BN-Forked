# Lighting parameter audit: what actually reaches the pixel

Static deduction from the shader/C++ source (no live measurement), answering: which of the
~96 lighting parameters (68 `debug_params` + 11 `sun_params` + 17 `gi_params` + SkySunParams +
shader constants) contribute visibly to the final render at shipped defaults, and which are
negligible or dead.

Method: every field was reference-counted across all five consumer shaders (declaration vs
body use), then each live consumer's gate and magnitude was read in context. The single most
important magnitude fact is structural, not per-knob:

## The 2.0 ceiling clamp is the master gate on daylight

`sprite.frag.hlsl:1044`: `gpu_total = min(ambient_v + dyn, 2.0) * cloud_vis * sun_shad_mul`.

At noon/fair the pre-clamp sum is ~2.25 (sun ≈ 1.22 × lambert ≈ 0.66-1.0, sky ≈ 1.03, plus
GI, plus floor). Open daylight tiles sit **at the ceiling**, so *any additive term's marginal
contribution is clipped to zero there*. This is not conjecture — the cloud block's own comment
(`sprite.frag.hlsl:912-921`) records a measured 2-3% visibility for a pre-clamp cloud multiply
at full strength, which is why `cloud_vis` and `sun_shad_mul` were moved **post-clamp**. Any
future "why does knob X do nothing outdoors" question should check this clamp first;
conversely, every additive knob's visible domain is interiors / dusk / night / shadow, where
the sum is below 2.0.

## Tier 0 — runs every frame, contributes zero pixels (the real finding)

**The Phase-2 silhouette sun-shadow mask subsystem is fully live as *work* and fully dead as
*output*.**

- Producer: `render_state::flush_shadow_casters` (`render_state.cpp:965+`), called
  unconditionally every frame (`sdl_render_frame.cpp:1033`). Opens a render pass on
  `shadow_mask_` (full-res RGBA16F, `render_state.cpp:226-231`), re-draws the TALL subset of
  every tile sprite with a sun-tracking vertex shear through a **second dedicated
  `sprite_batcher`** (`shadow.vert.hlsl` / `shadow.frag.hlsl`), "Always open the pass …
  (even with zero tall casters) so the mask is defined each frame".
- Consumer: one per-fragment unconditional texture load (`sprite.frag.hlsl:853`) feeding
  exactly one term: `mask_term = saturate(1.0 - sun_mask_cov * shadow_mask_str)`
  (`:949-950`).
- Shipped value: `shadow_mask_str = 0.0` (`sprite_batcher.h:241`) → `mask_term == 1.0`
  identically, for every fragment, every frame.

So the shipped cost is: one full render pass over all tall sprites + an RGBA16F full-res
target + a per-fragment texture load + the 32→48-byte shadow-shear growth of the vert
LightParams cbuffer — for a multiply by 1.0. The phase was abandoned mid-way:
`sprite.frag.hlsl:944-945` still says "trees still ALSO cast via the SDF … a temporary double
shadow until 2.3 drops trees from the sun SDF", and 2.3 never landed. Note the corollary:
**creature sun shadows do not render at all today** — creatures are deliberately excluded from
the SDF (`occluder_capture.h`), and the mask that was supposed to carry their shadows ships
multiplied by zero.

Decision needed (either is coherent, shipping the current halfway state is not):
1. **Delete the subsystem** (mask target, `shadow_batcher_`, `flush_shadow_casters`,
   `shadow.vert/frag.hlsl`, `mask_term`, `shadow_mask_str`, the `g_shadow_debug` blit) —
   zero visual regression by construction, removes a whole pass; revivable from git.
2. **Finish Phase 2.3** (ship `shadow_mask_str > 0`, drop trees from the sun SDF) — buys
   creature/tree silhouette sun shadows, the original goal.

## Tier 1 — dead fields (no shader body reads them anywhere)

Reference-count matrix over sprite.frag / sprite.vert / sky_sun / gi_field / occ_raster:

|Field|Default|Verdict|
|---|---|---|
|`mem_desat`|0.70|0 uses anywhere. Memory desaturation moved to the CPU tileset memory FX; the knob and its F4 slider are inert.|
|`grade_desat`|0.55|0 uses. Tone grade moved to the tonemap ASC-CDL stage (`sdl_lighting_devui.cpp:700-715`).|
|`grade_cool`|0.20|0 uses. Same.|
|`grade_bright`|0.80|0 uses. Same.|
|`sun_params.sp_pad`|—|Dead pad, self-documented.|
|`gi_params.rc_pad1`|—|Dead pad (retired `gi_bounce2`).|
|`cutout_pad1`, `cloud_pad1`|—|Intentional 16-byte alignment pads.|
|`SkySunParams.shadow_k` / `.shadow_steps`|8 / 16|Declared `(reserved)` in `sky_sun.comp.hlsl:47-48`, never read; forwarded per-frame (`sdl_render_frame.cpp:546-547`) for nothing.|
|`rc_params_gpu`: `sdf_map_w/h`, `sdf_ss`, `c0_interval`, `rc_pad0` in `rc_merge`/`rc_resolve`|—|Declared for cbuffer-layout parity, unread in those two shaders (they don't include `rc_shared.hlsl`). Parity is load-bearing; document, don't remove.|

Simplification: the four dead *tuning* fields (`mem_desat`, `grade_*`) can be renamed to pads
in both C++ and HLSL at unchanged offsets (the same wire-stable trick `sky_valid` used), and
their F4 bindings deleted. Four fewer knobs that lie about doing something.

## Tier 2 — exact no-ops at shipped defaults (off-by-default branches)

|Field|Default|Gate|Note|
|---|---|---|---|
|`shadow_mask_str`|0.0|`mask_term = 1 - cov*str`|Tier 0 above.|
|`sdf_sharp`|0.0|`if (sdf_sharp <= 0.001) return bil;`|Bilinear→nearest bias, "rarely needed … kept as a live tightness lever". Sits in `sdf_bilinear`, the hottest sampler (8 AO taps + GI + vis-carve + every march step per fragment). Candidate: hardwire bilinear, delete the knob.|
|`vis_radius`|0.0|`if (vis_radius > 0.001)`|Deliberately off: a radial dim would darken daylight. Fine as a debug lever.|
|`debug_mode` / `debug_opacity`|0 / 0.6|`debug_mode > 0`|Debug-only by design; `debug_opacity` consumed only by modes 1-5.|
|`emitter_scale` / `sun_scale` / `sky_scale`|1.0|multiplicative identity|Pure bisect levers (the knob hook uses them). 3 mults/fragment; keep — they are the instrument this codebase's whole verification method relies on.|
|`nrm_atlas_v`|0.0 (DATA)|`atlas_normal` identity at 0|Future feature (normal atlas pages); per-frame stomped by `assemble_light_inputs`, F4 slider is a dead control.|
|`spec_strength`|0.0 (DATA)|`> 0.001`|CPU-folds user knob × rain intensity; live only while raining. Legit.|

## Tier 3 — live, but visually negligible in their largest domain (ceiling-clipped)

|Field|Default|Where it visibly acts|Where it does NOT (and why)|
|---|---|---|---|
|`day_floor`|0.05|Interiors, dusk|Noon outdoors: sum already ≥ 2.0, the +0.05 is clipped. Keep (cheap; interiors need it).|
|`night_floor`|0.02|Night — **load-bearing**: it is the only light on unlit night tiles|—|
|`gi_strength` (outdoors)|0.7|Interiors, window daylight-bleed, night emitter bounce|Open noon ground: additive on a clipped sum. The knob's value IS the indoor/dusk look.|
|`dither_amt`/`dither_bands`|1.0 / 32|Sub-ceiling gradients (dawn, interiors, emitter falloff)|Saturated daylight: quantizing a clipped value is invisible. 32 bands ≈ 0.03 steps — deliberately at visual threshold.|
|`ao_strength`|0.35|Wall-adjacent tiles (pulls the sky term far enough to dip under the ceiling)|Open ground: all 8 taps saturate → `ao = 1` exactly, by construction. **Comment drift**: `sprite.frag.hlsl:977-978` claims "ao_strength=0 … the committed default"; the struct ships 0.35 "(ships ON)". Fix the comment.|

## Tier 4 — live and load-bearing (complete, for reference)

- **Scene definition**: `sun_params` (dir/elev/intensity/colour ×2) — the daylight itself.
  `sun_sin_elev` also sets the flat-normal Lambert (`flat_sun = s/√(1+s²)` ≈ 0.66 at 0.87).
- **March shape**: `shadow_k` (×4 `POINT_K_GAIN` for emitters), `shadow_steps` — every soft
  shadow. `sun_soft` — SDF penumbra width in sky_sun (the Stage-0e fix hangs off it).
- **Perf gates that are invisible *when correct*** (their job): `light_eps` (0 → shader
  default; culls sub-threshold emitters before the march), `max_shadow_k` (16; only the K
  strongest emitters get a shadow march — visible only under >16 overlapping emitters),
  `self_eps_tall` (0.55; tall-sprite self-shadow escape, prevents artifacts).
- **Normal cluster**: `nrm_amount` 0.9 enters both Lambert sites (emitter + sun);
  `nrm_relief` −2.0 / `nrm_elev` 0.3 shape it; `nrm_entity_amount` 0.3 (gentle tall-sprite
  bevel), `nrm_radial_amount` 0.4 (tall sprites only, ground excluded — the checkerboard
  lesson is written at `sprite.frag.hlsl:633-639`), `face_arc` 1.5 — **load-bearing**: without
  the face arc, walls read flat-black (alpha-bevel structurally cannot shade an opaque body,
  measured note at `:645-649`).
- **Vision/memory cluster**: `vis_curve` 1.0 gates AND exponentiates the sub-tile LOS carve —
  note its cost: a full `soft_shadow_march` toward the player *per fragment*, whose visible
  effect exists only near occlusion boundaries (open-LOS fragments compute `v=1` → multiply
  by 1). Correct but the priciest identity-multiply in the shader; a cheap early-out (skip
  the march when the tile's frontier coverage is saturated) is a possible perf follow-up.
  `mem_dim` 0.35 / `mem_radius` 30 (memory look), `vis_edge` 1.0 (frontier feather),
  `light_quant`/`texels_per_tile` (pixel-art light lattice — identity only at zoom where
  screen px == art texel).
- **Palette ramp**: `ramp_enable` 1, `ramp_chroma` 0.35 (`ramp_steps` is DATA, stomped to the
  built LUT's step count). The single biggest "pixel-art vs HD lighting" look component.
- **GI**: `gi_strength` 0.7, `gi_albedo` 0.6, `gi_feedback` 0.3 (all live; feedback is what
  carries daylight deeper indoors across rebuilds), `gi_bilat` 1 (SDF-aware upsample — off
  means bounce leaks through walls).
- **Clouds** (6 fields): post-clamp multiplier — up to 80% darkening under full cover at
  `cloud_strength` 0.8; scale/wind/threshold/softness shape one fbm. All live.
- **Canopy cutout**: `cutout_radius` 0.55 / `cutout_feather` 0.22 — product acceptance
  criterion (see product notes), keyed to flagged canopies only.
- **Foliage motion** (vertex): `sway_amp` 3px / `sway_freq` 1.2 / `ripple_k` 1.5 /
  `gust_amp` 0.4 / `gust_freq` 0.3 / `part_radius` 2.5 / `part_strength` 0.5 — all read in
  sprite.vert's foliage block, gated per-sprite by the sway flag.
- **Sky/sun compute quality** (rebuild-cadence cost only, not per-frame): `sky_dirs` 8,
  `sky_reach` 10, `sun_steps` 24, `sun_penumbra` 4 — all read in sky_sun.comp.
- **DATA fields** (not knobs; per-frame injected, F4 sliders on them are dead controls):
  `player_x/y`, `anim_time`, `texels_per_tile`, `ramp_steps`, `nrm_atlas_v`,
  `spec_strength`, `sky_valid`.

## Cross-struct inconsistencies found on the way

1. **The sun/sky colour is pushed three times with two different weather treatments**:
   raw into `gi_params` (`sdl_render_frame.cpp:611-618`), weather-multiplied into the
   sprite's `sun_params` (`:721-727`) and the volumetric pass (`:751-758`). Bounced daylight
   therefore ignores weather dimming — in overcast, GI bounce is computed from full-strength
   sun while direct light is dimmed. Small in magnitude (GI is one additive term, scaled
   0.7) but a genuine inconsistency; unify by feeding gi_params the weather-multiplied
   values unless the raw feed was a deliberate look choice (no comment claims it is).
2. **`shadow_k` means different things at its four consumers** — sprite multiplies by
   `POINT_K_GAIN = 4`, gi_field/vol use it raw; "same number" ≠ "same penumbra".
3. **`gi_params` header comment says 64 bytes; the struct is 68** (`gi_compute_pass.h:47`).
4. **AO default comment drift** (`sprite.frag.hlsl:977-978` vs `sprite_batcher.h:239`).
5. `SDF_SUPERSAMPLE = 8` exists in four places (`sdf_pass.h:24` authority, `jfa_shared.hlsl:4`,
   `rc_shared.hlsl`, plus the C++ mirror in rc fill) — all currently agree.

## Recommended simplification order (each independently shippable)

1. **Decide Tier 0** (silhouette mask): delete or finish. Deleting removes a per-frame
   render pass, a second sprite_batcher, two shaders, one RGBA16F target and one
   per-fragment texture load, with provably zero visual change (`mask_term ≡ 1`).
2. **Retire the four dead tuning fields** (`mem_desat`, `grade_*`) to named pads at
   unchanged offsets + delete their F4 bindings.
3. **Drop `SkySunParams.shadow_k/shadow_steps`** (rename to pads; stop forwarding).
4. **Hardwire `sdf_sharp`** out of `sdf_bilinear` (one branch out of the hottest sampler).
5. **Fix the two stale comments + the 68-byte claim**; document the rc_params parity rule.
6. **Unify the GI weather treatment** (or write down why raw is intended).

Not recommended to touch: the identity-scale bisect levers (`*_scale`), the perf gates
(`light_eps`/`max_shadow_k`/`self_eps_tall`), `day_floor`/`night_floor`, dither, or any
Tier 4 row — each is either the instrument this project's verification method depends on or
carries a documented look decision.
