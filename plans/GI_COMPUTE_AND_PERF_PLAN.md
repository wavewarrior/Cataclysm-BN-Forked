# Plan: D3D12-robust GI (GPU compute) + do_turn / lighting perf

## STATUS (reviewed 2026-06-27)
Done ~90%. Status table below VERIFIED against code 2026-06-27 and is accurate: rc.frag/rc_bounce.frag + radiance_cascade_pass DELETED; gi_field.comp/gi_bounce.comp/sky_sun.comp + jfa_*.comp present; gpu_sdf_pass (JFA) wired in render_state; SunSdfBuf removed from sprite.frag, GiBuf t6/SkyBuf t7 renumber landed (Stage 2b); B0 [sim][perf] probe in game.cpp:2185. KEEP — live spec. Only 2 items open: P4 (D3D12 barrier verify — blocked on Win11 hw) + B2 (sim-span attack — blocked on B0 capture). No contradictions found.

## ★ Status (2026-06-25) — all structural work done; polish + perf remain

| Section | Status | Commits |
|---------|--------|---------|
| Stage 0 — gates | ✅ Done | `66c21e2f6b` (reflection gate), `0bc24c3570` (A0 spike) |
| Stage 1 — GPU compute GI | ✅ Done | `a5857d907f` |
| Stage 2a — Directional skylight | ✅ Done | `fdbb63d14e` |
| Stage 2b.1 — Unified coverage occluder | ✅ Done | `da07f28c8c` |
| Stage 2b.2 — Directional moonlight | ✅ Done | `77537fef63` |
| P1 — Delete dead sun_sdf | ✅ Done | `648ebd13ff` |
| P2 — Sun/sky → GI bounce | ✅ Done | `c8bdd1e1bd` |
| P3 — GPU JFA SDF | ✅ Done | `129f42add8`, `241b3ffa2d` |
| P4 — D3D12 compute barrier | ❌ Blocked | Needs Win11 hardware |
| P5a — Soft sun penumbra | ✅ Done | `fa905de580` |
| P5b — F4 tunable knobs (SKY_DIRS/SKY_REACH/SUN_STEPS/SUN_PENUMBRA) | ✅ Done | 2026-06-25 |
| P5c — Sky horizon→zenith gradient | ❌ Deferred | 2D top-down has no view horizon; negligible |
| P5d — night_floor retune | ❌ Deferred | Knob exists (0.02f default); needs in-game tuning |
| P6a — Weather-dim moonlight | ✅ Done | `6f109a0737` |
| P6b — Vehicle occluders | ✅ Done | `8587d55b8d` |
| B0 — Sim perf probes wired | ✅ Done | `0273a52ca4` |
| B1 — structure_rebuild region-limit | 🔄 Superseded | P3 JFA SDF replaces CPU DT entirely |
| B2 — Attack sim spans | ❌ Not started | Needs measurement data first |

**One remaining actionable item:** build_map_cache/monmove/world_tick perf (B2) —
capture sim numbers, find the dominant span, target it.

---

## Context

Two independent problems on the primary target (Win11 / D3D12):

**1. GI is fragile on D3D12 — but the technique is right; the *shader stage* is wrong.** Current GI is GPU **Radiance Cascades** (industry-standard 2D GI: Sannikov, shipped in Path of Exile 2), implemented as **fragment** passes (`rc.frag` → `radiance_field_tex_`, `rc_bounce.frag` → `cascade_tex_`) read by `sprite.frag` as a `Texture2D<float4>` storage-read texture. The fragility is **SDL_shadercross→DXIL mishandling fragment-stage dynamic StructuredBuffer loops on D3D12**, not the GPU:
- The committed `rc.frag` header documents: `float4` SB dynamic-index reads rejected → forced scalar `StructuredBuffer<float>`; dynamically-indexed local arrays fail pipeline creation. The working-tree `rc.frag` is a **diagnostic stub** that already concluded *"gather needs a loop-free / **compute rework** (or accept GI-off on D3D12)."*
- Upstream confirms fragment-stage bugs: StructuredBuffer→ByteAddressBuffer dynamic-index breakage (SDL #12200), fragment-storage-buffer slot-skip / sampler-offset crashes (shadercross #13018).
- Compounding: same-CB storage-texture write→read barrier, transposed `COLOR_TARGET|GRAPHICS_STORAGE_READ` target, all-or-none storage-texture binding.

**Decision: rewrite the RC gather as a GPU compute shader writing a storage buffer.** Compute is the industry-natural home for RC, uses a different binding/reflection path (`SDL_BindGPUComputeStorageBuffers`, RW storage buffers) that very likely dodges the fragment-stage bug, keeps GI **off the main thread**, and writing a storage buffer (not a transposed color-target) removes the texture write→read + all-or-none hazards. Gated by a go/no-go **spike** (A0): the engine has **no compute pipelines today** (shader_compiler only does VERTEX/FRAGMENT). If a minimal D3D12 compute pipeline with a dynamic SB loop fails to create, fall back to the documented CPU-GI contingency.

Pinned dep (corrected): SDL_shadercross `9a46164…` against SDL release-3.4.10 (CMakeLists.txt:553) — *not* the stale `6b06e55c` in old notes.

**2. Frames are ~32 fps (30ms) while `render_body` is ~1.1ms** — the loop is **main-thread-bound** (sim + lighting). The lighting **`structure_rebuild` ≈ 10ms** spike (max-frame 76ms) is the 16× **supersampled Euclidean DT over the full 180×180 reality bubble** (`grid=32400tiles x16`, `frame_build.cpp`) when only ~60×40 tiles are on screen. The `[sim][perf]` probe (game.cpp ~1818–2180, logs `sim_total/build_map_cache/monmove/world_tick` every 20 turns) is wired but **produced no output yet** — sim numbers uncaptured. (Moving GI to CPU would have fought this; GPU-compute GI keeps the main thread free — consistent with Part B.)

Outcome wanted: GI that creates + renders identically on D3D12 and Metal with no pipeline roulette, and frames that don't spike.

---

## Part A — GI: GPU compute gather → storage buffer  (✅ Done: `a5857d907f`)

### A0. Compute infrastructure + spike gate (go/no-go)  <!-- ✅ Done: `0bc24c3570` (spike removed — compute proven) -->
- **Add a COMPUTE compile path** to `shader_compiler.{h,cpp}`: `SDL_SHADERCROSS_SHADERSTAGE_COMPUTE` via `SDL_ShaderCross_CompileComputePipelineFromHLSL` → `SDL_CreateGPUComputePipeline`. Compute declares its own resource model in `SDL_GPUComputePipelineCreateInfo` (`num_readonly_storage_buffers/textures`, `num_readwrite_storage_buffers/textures`, `num_uniform_buffers`, `threadcount_x/y/z`) — different from the graphics reflection.
- **SPIKE**: a minimal compute shader — dynamic `[loop]` over a `StructuredBuffer<float>` (readonly) writing one `RWStructuredBuffer<float>`. Build + run on **Win11/D3D12**. Read `debug.log`:
  - **Creates (no `E_INVALIDARG`/`0x80070057`)** → GO: the fragment-stage bug does not apply to compute; proceed A1+.
  - **Fails** → NO-GO: fall back to the **CPU-GI contingency** (appendix below). This is the explicit decision gate before investing in the real shader.
- This plumbing is required for the real rework regardless, so the spike is not throwaway.

### A1. `gi_compute_pass` (replaces `radiance_cascade_pass`)  <!-- ✅ Done -->
- New `src/lighting/gi_compute_pass.{h,cpp}` + `data/shaders/lighting/src/gi.comp.hlsl`. Port the RC gather/bounce math from `rc.frag`/`rc_bounce.frag` into one compute shader:
  - **Readonly storage**: Emitters SB, SDF SB (reuse the existing buffers, same data the fragment passes consumed).
  - **Readwrite storage**: `gi_storage_` — tile-resolution RGB radiance over the **camera region + shadow margin** (low-freq; no supersampling, no transpose).
  - One thread per tile: gather emitters with SDF-occluded falloff (reuse `trace_shadow`/`sdf_bilinear` logic), optional bounce iterations. Dispatch `ceil(W/8)×ceil(H/8)` groups.
- Compute→graphics dependency: dispatch (write `gi_storage_`) precedes the sprite pass (read) on the same CB. This is the **standard compute→graphics barrier** SDL_GPU is designed to insert — verify it holds on D3D12 during early bring-up (lower-risk than the fragment color-target→storage-read edge it replaces).

### A2. Swap the sprite.frag consumer  <!-- ✅ Done -->
- Remove `Texture2D<float4> IndirectTex` (storage texture) + its bind. Add `StructuredBuffer<float4> GiBuf` at the next fragment storage-buffer slot.
- **Renumber `register(tN, space2)` decls and the C++ `SDL_BindGPUFragmentStorageBuffers` slot indices in lockstep** (binding-order mismatch is a documented D3D12 crash).
- GI read: `dyn += gi_strength * gi_bilinear(world_pos)`, x-major `arr[x*H+y]` + `p-0.5` centre (mirror `sdf_bilinear`). `gi_strength` knob (F4 Alt+F8/F9) unchanged.
- `ShadowMask` becomes the only storage texture → the all-or-none 2-slot hazard is gone.

### A3. Wire + delete  <!-- ✅ Done -->
- `render_state.{cpp,h}`: replace `rc_` with `gi_compute_pass`; own `gi_storage_`/`xfer_gi_` (or in `sdf_pass`/`emitter_collector`, uploaded in the same copy pass, `cycle=false`).
- `sdl_render_frame.cpp`: replace `rc().record()` + flush-and-gather with the compute dispatch, under the existing per-tile/structure gate; retain buffer on skip frames.
- **Delete**: `radiance_cascade_pass.{h,cpp}`, `rc.frag.hlsl`, `rc_bounce.frag.hlsl`, `cascade_tex_`/`radiance_field_tex_`, RC readback oracle. Keep `gi_strength` + the debug-mode slot.

### GI verification
- Metal build (`cmake --build out/build/osx-arm-slim --target cataclysm-bn-tiles`): one off-center light + `gi_strength` up → colored fill into shadow / around corners, wall-occluded, tracks a moving light (dev cursor-light tool).
- **D3D12 is the gate**: Win11 build+run → no pipeline-creation failure, no device-removed, GI visible + plausible. This is what the fragment RC never achieved.

---

## Stage 2 — sun/sky directional skylight (SPEC, 2026-06-18)  (✅ Done: `fdbb63d14e` `da07f28c8c` `77537fef63`)

**Goal.** Replace the flat sky ambient (`sky_color · sky_intensity · sky_vis`) and the
single directional sun ray with a per-tile **directional skylight integral** computed
in compute, so the sky behaves as an occluded dome (bright toward open sky, self-shaded
in alcoves/overhangs) and indoor daylight emerges from the *direction of openings* —
unifying indoor + outdoor in one model. Built on the proven Stage-1 compute base
(scalar `StructuredBuffer`s, `numthreads(8,8,1)`, readonly-declared inputs, region-
limited grid). Mirrors `gi_compute_pass` structurally.

**Why directional-portal before heightfield (grilled 2026-06-18).** The indoor/outdoor
"split" is a flattening artifact: the GPU lights a 2D z-slice; horizontal wall occlusion
is marchable (in-plane SDF) but vertical/roof occlusion was collapsed to the per-tile
`outside_cache` scalar (`sky_vis`). True unification = a thin-slab 3D SDF marched in 3D
for *both* emitters and sun — but that wants **GPU JFA SDF generation** (a 3D CPU DT
would re-create the hitch B1 just removed) and is therefore **deferred to the JFA
roadmap phase**, not Stage 2. A single 2D *scalar* SDF cannot serve both because the
occluder *set* differs (emitters: walls only — a lamp must light its roofed room; sun:
walls **and** roofs). So Stage 2 keeps the emitter SDF untouched and works the sun/sky
path only, in two sub-steps. Note: this does **not** add a structure — the sun already
owns a separate 2D field (`sun_sdf`, wall-only, trees-excluded); 2b *upgrades* that one.

### Stage 2a — 2D directional sky-portal march  (✅ Done: `fdbb63d14e`)

Per tile, march the existing wall-only `SunSdfBuf` in N hemisphere directions; weight
each direction by whether it **reaches an open-sky tile** before a wall stops it
(sampling `SkyVisBuf`). This computes directional skylight *and* does the window/opening
propagation itself — so the CPU window-bleed flood-fill is **deleted** (the Part-B CPU
win: full-bubble K=8 flood-fill + gaussian → off the main thread).

**A. `frame_build.cpp` — feed raw open-sky, delete the CPU bleed.**
- `SkyVisBuf` upload becomes **raw `outside_cache`** (1.0 open / 0.0 roofed), tile-res,
  x-major `[x*H+y]` — *no* bleed flood-fill, *no* gaussian. The directional march now
  owns indoor propagation. **Delete** the `skylight_bleed` flood-fill block
  (frame_build.cpp ~275–323) and the `vision_blur` sky_vis gaussian (~329–339). Keep
  the `skylight_bleed`/`vision_blur` knobs wired to *nothing* for one commit (avoid a
  cbuffer-layout churn), or repurpose `skylight_bleed`→`sky_dir_strength`. The raw
  `sky_vis` still drives the fragment's roofed-tile gate as today (the march output is
  *additional* directional shaping, multiplied in — see D).
- `sun_sdf` build (region_sdf, ~241) is **unchanged** in 2a (still the wall-only SS SDF).

**B. New `src/lighting/sky_sun_pass.{h,cpp}` + `data/shaders/lighting/src/sky_sun.comp.hlsl`.**
Single compute dispatch (one thread = one tile), mirroring `gi_field`'s scaffolding:
- **Inputs (readonly storage):** `t0 space0` SunSdfBuf (`StructuredBuffer<float>`,
  SS-grid `[x*(map_h*SDF_SS)+y]`, tile units); `t1 space0` SkyVisBuf
  (`StructuredBuffer<float>`, **tile-res** `[x*map_h+y]`, raw open-sky 0/1).
- **Output (readwrite storage):** `u0 space1` SkyBuf (`RWStructuredBuffer<float>`,
  4 floats/tile rgb+pad, tile-res, x-major `[(x*map_h+y)*4+c]`).
- **Uniform `b0 space2` SkySunParams** (new struct, ≤32 B): `map_w, map_h` (uint);
  `sun_dir_x, sun_dir_y, sun_sin_elev, sun_intensity`; `shadow_k` (float);
  `shadow_steps` (uint). Sky/sun *colours* are applied in the fragment (keep colour out
  of the buffer → buffer is pure occluded-luma RGB weight; fewer params, lets F4 colour
  knobs stay fragment-side). Pack to 32 B with a pad.
- **Algorithm per tile** (probe = tile centre +0.5):
  - *Sky dome:* loop `d` over `SKY_DIRS` (const 8u start) fixed 2D directions over the
    upper hemisphere (uniform azimuth). For each: `sky_march(probe, dir)` = step the
    SunSdf (reuse Stage-1 `trace_shadow`/`sdf_bilinear` helpers — copy verbatim from
    `gi_field.comp`) up to `SKY_REACH` (const ~10) tiles; if it hits a wall (`sd<0.05`)
    → contributes 0; else sample `SkyVisBuf` at the march endpoint — if open (>0.5)
    accumulate the direction's hemisphere weight. `sky_rgb += w_dir · reached_open`.
    Normalise by `SKY_DIRS`. This is the per-tile **directional sky-access** (an
    AO-like skylight integral). Store in `SkyBuf` rgb (white-weight; fragment multiplies
    `sky_color`).
  - *Sun term:* `sun_occ = trace_shadow(probe, toward_sun, SUN_REACH=8, shadow_k,
    shadow_steps, directional)` averaged over `SUN_PENUMBRA` (const 1→ later 3–4)
    angular offsets of `toward_sun` for a soft edge. Fold into a 4th channel? No — keep
    SkyBuf rgb = sky-access; **add a parallel `sun_buf_`** OR pack sun_occ into the pad
    lane `[...*4+3]` (cheaper: 1 buffer). **Decision: pack `sun_occ` into lane 3** of
    SkyBuf (rgb = sky-access, a = sun occlusion). One buffer, one bind.
- `numthreads(8,8,1)`; dispatch `ceil(W/8)×ceil(H/8)`. No samplers (reflect-gate clean).
- `sky_sun_pass`: `init`/`resize`/`shutdown`/`ready()`/`sky_buffer()` exactly like
  `gi_compute_pass` (copy the buffer-alloc + pipeline-compile scaffold). `sky_buf_`
  usage = `COMPUTE_STORAGE_WRITE | GRAPHICS_STORAGE_READ`. Zero on init. `record(cb,
  sun_sdf_buf, sky_vis_buf, W, H, params)`: one `BeginGPUComputePass(rw=sky_buf_,1)`,
  bind `ro[2]={sun_sdf_buf, sky_vis_buf}`, dispatch.

**C. `sdf_pass.cpp` — add `COMPUTE_STORAGE_READ` to the two inputs the pass binds.**
- `sun_sdf_storage_` create flags (sdf_pass.cpp:254) and `skyvis_storage_` (~263):
  add `| SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ` (exact precedent: `sdf_storage_`
  already has it, :242–243). Never leave a bound SRV slot unflagged on D3D12.

**D. `sprite.frag.hlsl` — consume SkyBuf (t8), replace flat sky + sun occlusion.**
- Add `StructuredBuffer<float> SkyBuf : register(t8, space2);` (storage buf **slot 6**,
  the new LAST). Renumber nothing else (append-only). Update the header slot comment.
- `sky_contrib`: was `sky_color·sky_intensity·sky_vis`. Becomes
  `sky_color·sky_intensity · sky_access(world_pos)` where `sky_access` bilinear-reads
  SkyBuf.rgb at tile-res (`p-0.5` centre, mirror `indirect_bilinear`). The raw `sky_vis`
  roofed gate still multiplies (roofed deep-interior → access≈0 anyway, but keep the
  gate so the transition matches the fragment's other sky terms).
- `sun_contrib`: keep the per-pixel `sun_lambert` + `wet_spec` + screen-space tree
  `mask_term` (fragment-side, need the per-pixel normal). **Replace** the inline
  `trace_shadow(...)` call (sprite.frag.hlsl:540) with `sun_occ = SkyBuf.a` (bilinear).
  i.e. occlusion moves to compute, shading stays fragment. Drop the now-unused fragment
  `trace_shadow` sun path if no other caller (emitter shadows still use it → keep the
  helper; just the *sun* call site changes).
- C++ side (`sprite_batcher.cpp` `bind_lighting_resources` / the lighting god-call):
  bind `sky_buffer()` at fragment storage **slot 6** for the tile batcher only
  (ui/shadow batchers pass null, like GiBuf). Add the param to the bind signature in
  lockstep with the HLSL register (binding-order mismatch = D3D12 crash).

**E. `render_state.{cpp,h}` + `sdl_render_frame.cpp` — own + dispatch.**
- `render_state`: own `sky_sun_pass sky_`; `init`/`resize` it alongside `gi_` (same
  `max_w/max_h`); expose `sky()`.
- `flush_and_gather_rc` (sdl_render_frame.cpp ~250): under the **same `rc_rebuild` gate**
  as the GI record, after the SkyVis/SunSdf upload, **before** the sprite pass, add:
  `rs.sky().record(ctx.cmd_buffer, rs.sdf().sun_sdf_buffer(), rs.sdf().sky_vis_buffer(),
  W, H, sky_params)`. SDL_GPU inserts the compute-write→graphics-read barrier on
  `sky_buf_` automatically (same as `gi_buf_`). `cycle=false` (retained on skip frames).

**F. Reflect-gate + knobs.**
- `sky_sun.comp` joins the gate: expect `ro_sb=2 rw_sb=1 8×8×1`, no samplers. sprite.frag
  becomes `tex=1 buf=7 ub=3` (was buf=6). Run
  `shader_reflect_check` before declaring done (Mac-side D3D12 gate).
- Tuning constants (`SKY_DIRS`, `SKY_REACH`, `SUN_PENUMBRA`) live as `static const` in
  the shader first; promote to F4 `debug_params` knobs once eyeballed (follow-on, mirror
  `gi_strength`). Add a debug-view mode (next free slot) showing raw `SkyBuf.rgb`
  sky-access, gi_strength-independent (mirror GI mode 12).

### Stage 2b — unified coverage-occluder field + 3D elevation march  (✅ Done: `da07f28c8c` `77537fef63`)

**Design pivoted after data audit** (grilled): the original hand-built heightfield and
the "feed `angled_sunlight_cache`" pivot were BOTH rejected. `angled_sunlight_cache`
(lightmap.cpp:368) is correct *z-aware* sun visibility but (a) floor-only → ignores
half-walls/furniture, (b) sun-only → no moon. Decision: **one unified per-tile occluder
field from `map::coverage(p)`** (the game's single 0–100 obstruction scalar — wall ~100,
half-wall ~50, fence/furniture low; map.cpp:7615) + a roof bit from `floor_cache(z+1)`,
**marched per light-direction in 3D (x,y,elev)** for sun + moon + sky. One source, one
march, "fewer places to look when it's wrong". Confirmed available: `map::coverage`,
`get_moon_phase` (calendar.h:626). Half-walls/furniture handled by coverage→height;
roofs by the roof bit; sun elevation makes dawn/dusk shadows lengthen and lets a HIGH
sun clear a half-wall a LOW sun shadows.

**Sub-steps (verify each; 2b.1 carries the renumber risk):**

**2b.1 — coverage field + 3D elevation SUN  (✅ Done: `da07f28c8c`)**
- **`frame_build.cpp`:** build per-tile `occ` field, region-limited (reuse the B1 cam
  rect): 2 floats/tile `[(x*map_h+y)*2 + c]` — c0 = `map::coverage(p)/100` scaled to
  tile-height units, c1 = `floor_cache(z+1)` roof bit (1=roofed). Thread it through
  `emitter_collector::submit` → `flush_to_render_cb` → `sdf_pass::upload` (mirror the
  `sun_sdf` param — add `occ` alongside; ~4 mechanical edits).
- **`sdf_pass`:** new `occ_storage_` buffer (tile-res, 2 floats/tile) + `occ_buffer()`
  getter + upload; `GRAPHICS|COMPUTE_STORAGE_READ` (fragment may read it later; compute
  marches it now).
- **`sky_sun.comp`:** REWRITE the march to read `OccBuf` (height+roof) instead of
  SunSdf+SkyVis. Sun occlusion = 3D march toward the light: at horizontal step `t`,
  ray height = `t · tan(elev)` (`elev` from `sun_sin_elev`); a cell blocks if
  `occ_height(cell) ≥ ray_height` (soft-min by the margin for penumbra) OR the cell is
  roofed while `ray_height < ROOF_H` (probe's own roof blocks at t≈0). Sky-access:
  hemisphere portal march on the same field (reach a non-roofed tile, coverage-weighted
  block). Output unchanged: `SkyBuf` rgb=sky-access, a=celestial-occ. Params struct
  gains nothing (reuse `sky_sun_params`: dir + sin_elev already there).
- **`sprite.frag`:** sun shadow `trace_shadow(...use_sun)` → `SkyBuf.a`. **REMOVE
  `SunSdfBuf` (t6) + `sdf_bilinear_sun`/`sdf_texel_sun` + the `use_sun` branch of
  `trace_shadow`**; renumber GiBuf t7→t6, SkyBuf t8→t7 (storage buffers contiguous t2..t7
  again, buf=6). ⚠ THIS IS THE RENUMBER — fragment storage-buffer slot/register + the
  C++ bind move in LOCKSTEP (the repeated D3D12 device-removal cause). Emitter
  `trace_shadow` call drops the `use_sun` arg.
- **`sprite_batcher.cpp`:** bind 6 storage buffers `{emitter,sdf,skyvis,vis,gi,sky}`
  (drop `lp_sun_sdf_buf` from the array + gate); GiBuf→slot4/t6, SkyBuf→slot5/t7.
- **`render_state`/`sdl_render_frame`:** pass `occ_buffer()` to `sky().record` instead of
  sun_sdf+sky_vis; the celestial direction/elev is the SUN today (`make_sun_params`).
- **frame_build:** the now-unused `sun_sdf` build + `sun_sdf_storage_` can be removed
  (single-source cleanup) — OR kept one commit to shrink the diff, removed in 2b.1b.
- **Reflect-gate:** `sky_sun.comp` still `ro≤2 rw=1`; sprite.frag back to `buf=6`.

**2b.2 — moon (param-swap on the proven 2b.1 march)  (✅ Done: `77537fef63`)**
- CPU: when `!m_solar.direct_active` (night), feed the **moon** as the celestial light:
  direction = 12h-shifted sun arc (moon ≈ opposite sun; reuse the elevation math),
  colour = cold blue-white, intensity = `get_moon_phase` illumination × small factor.
  "Full moon = sun with different params." Same compute march, same `SkyBuf.a`; only the
  fragment colour/intensity + the march direction differ. Smooth dawn/dusk handoff: pick
  the brighter of sun/moon (or crossfade).
- Re-tune night ambient floor against directional moonlight.

**Residual limit (accepted):** the low-wall-pit-under-high-sun is now handled (elevation
clears short coverage). Coverage conflates opacity+height (chain fence = low coverage =
little shadow, physically tall but light-transparent — fine for lighting). Vehicles: 
`map::coverage` is furn-then-ter; vehicle occluders may need `obstacle_coverage` later.

### Deferred (NOT Stage 2) — full 3D-SDF unification  (🧊 rides P3/JFA roadmap)
One thin-slab 3D SDF (current z + few above, region-limited) sphere-marched in 3D for
**both** emitters and sun = the genuinely-merged structure. Belongs to the **GPU JFA
SDF** roadmap phase (3D DT on CPU would undo B1). Tracked there, not here.

### Stage 2 critical files
| File | Change |
|---|---|
| `src/lighting/sky_sun_pass.{h,cpp}` *(new)* + `data/shaders/lighting/src/sky_sun.comp.hlsl` *(new)* | 2a directional sky-portal march → `sky_buf_` (rgb=sky-access, a=sun occ) |
| `src/lighting/frame_build.cpp` | 2a: SkyVis upload = raw `outside_cache`; **delete** bleed flood-fill + sky_vis gaussian. 2b: sun_sdf→heightfield |
| `src/lighting/sdf_pass.cpp` | 2a: add `COMPUTE_STORAGE_READ` to `skyvis_storage_`; 2b: same to `sun_sdf_storage_` |
| `data/shaders/lighting/src/sprite.frag.hlsl` | 2a: add `SkyBuf` t8 (slot 6); sky_contrib←SkyBuf.rgb, sun_occ←SkyBuf.a (drop inline sun `trace_shadow` call) |
| `src/lighting/sprite_batcher.cpp` | 2a: bind `sky_buffer()` at fragment storage slot 6 (tile batcher only); param in lockstep with HLSL register |
| `src/lighting/render_state.{cpp,h}` | 2a: own `sky_` pass; init/resize alongside `gi_`; `sky()` accessor |
| `src/sdl_render_frame.cpp` | 2a: `sky().record(...)` in `flush_and_gather_rc` under the `rc_rebuild` gate, before the sprite pass |

### Stage 2 verification (dual-backend — D3D12 is the gate)
- **Reflect-gate (Mac, pre-run):** `sky_sun.comp` `ro_sb=2 rw_sb=1`; sprite.frag `buf=7`.
- **Metal:** open-sky tiles full sky; alcove/overhang tiles visibly self-shaded; indoor
  tile near a window lit *from the window direction*; deep interior dark; sun shadow
  matches prior softness (then softer with `SUN_PENUMBRA>1`). Dev: sky-access debug mode.
- **D3D12 (Win11):** no pipeline-creation failure, no device-removed, identical
  structural result. Same gate the fragment RC never passed; compute binding model
  carries it (Stage 1 precedent).

---

## Post-Stage-2 improvement backlog (2026-06-18 — Stage 2a/2b shipped + eyeballed)

Stage 2a (directional sky-portal) + 2b.1 (unified coverage occluder, 3D-elevation
sun) + 2b.2 (directional moonlight) are committed (`fdbb63d`, `da07f28c8c`,
`77537fef63`) and Metal-eyeball-confirmed. Remaining work, ordered by leverage:

### P1 — cleanup: delete dead `sun_sdf` (✅ Done: `648ebd13ff`)
After 2b.1 the wall-only sun SDF is read by nothing. Still being **built** (a
second region-DT in `frame_build` `region_sdf(sun_sdf, …)`), **uploaded**
(`sdf_pass`: `sun_sdf_storage_` + `xfer_sun_sdf_` + the upload block), and the
`sun_sdf_buffer()` accessor + `emitter_collector` `pending_sun_sdf_`/`sun_sdf`
plumbing. Remove the whole chain → drops one CPU DT + one GPU upload + one buffer
per rebuild. Low risk (no consumer). Also long-dead: `sdf_tex_` / `sky_vis_tex_`
R8/R32F textures in `sdf_pass` (superseded by storage buffers; nothing samples them).

### P2 — biggest visual: sun/sky → GI bounce (✅ Done: `c8bdd1e1bd`)
`gi_field.comp` gathers **emitters only** → the sun (dominant light) casts flat
dark+ambient shadows with no bounced daylight and no indoor light-leak. Feed
sun/sky-lit surface radiance into the GI field input (per-tile: `sun_term ·
SkyBuf.a + sky_term · SkyBuf.rgb`, already computed) so `gi_bounce` propagates it.
Plan's own thesis: "the GI win is the surface-radiance-field bounce." Moderate
effort, large realism gain (soft daylight fill, colour bleed indoors).

### P3 — biggest perf + unlocks the merge: GPU JFA SDF (✅ Done: `129f42add8` `241b3ffa2d`)
CPU Euclidean DT is the ~10 ms `structure_rebuild` hitch (render-thread even
region-limited). Move SDF generation to a GPU **Jump-Flood** pass → round
Euclidean, off the main thread, AND unblocks the genuinely-merged **thin-slab 3D
SDF** (marched in 3D for emitters AND sun — the real indoor/outdoor unification,
deferred here only because a 3D *CPU* DT would undo B1). See "Deferred" below.

### P4 — robustness: verify the D3D12 compute barrier (❌ Blocked — needs Win11 hardware)
The same-CB compute-write→graphics-read barrier on `gi_buf_` / `sky_buf_` is
**never verified on Win11/D3D12**. All compute lighting rests on SDL_GPU inserting
it correctly. Confirm on the next Win11 pass (no device-removed, GI/sky visible).

### P5 — cheap quality polish  (✅ P5a penumbra `fa905de580`; ✅ P5b F4 knobs 2026-06-25; ❌ P5c deferred; ❌ P5d deferred)
- Soft sun penumbra: average `celestial_occ` over 3–4 angular offsets of `toward`
  (`SUN_PENUMBRA` const → loop) for softer shadow edges.
- Promote shader/CPU constants to F4 knobs: `SKY_DIRS`/`SKY_REACH`/`SUN_STEPS`,
  `MOON_MAX`, moon colour, penumbra width, `night_floor` (mirror `gi_strength`).
- Sky colour as a horizon→zenith gradient instead of one flat `sky_color`.
- `night_floor` retune against the new directional moonlight.

### P6 — correctness gaps  (✅ P6a `6f109a0737`; ✅ P6b `8587d55b8d`)
- Moon not weather-dimmed (clouds ignore moonlight) — fold a cloud factor into
  `make_celestial_params` moon intensity.
- Vehicles: `map::coverage` is furn-then-ter → tall vehicle occluders may want
  `obstacle_coverage` in the occ-field build.

### Deferred (own phase) — full 3D-SDF unification (🧊 rides P3/JFA)
One thin-slab 3D SDF (current z + few above, region-limited) sphere-marched in 3D
for **both** emitters and sun = the genuinely-merged structure. Belongs to the
GPU-JFA phase (a 3D CPU DT would undo B1).

---

## Part B — Perf: structure_rebuild hitch + measured do_turn  (🔄 P3 JFA SDF superseded B1; B0 probes wired; B2 not started)

### B0. Capture sim numbers FIRST  (✅ Done: `0273a52ca4` — probes wired, no captured results yet)
Run, **hold movement ≥20 turns**, read `[sim][perf] … sim_total= (build_map_cache= monmove= world_tick=)` from `debug.log`. Don't pre-commit to a sim target before this. (`[sim][perf] build_map_cache` (map.cpp) is **distinct** from `[lighting][perf] structure_rebuild` (SDF DT) — both ~10ms, do not conflate.)

### B1. structure_rebuild region-limit  (🔄 Superseded — P3 JFA SDF replaces the CPU DT; no separate action needed)
- The DT recomputes over the **full 180×180 bubble × 16 SS ≈ 520k cells ≈ 10ms**, but only on-screen + shadow-reach is sampled.
- **Limit the DT + SS transparency replication to camera rect + ~8-tile margin** in `frame_build.cpp`/`sdf_pass.cpp` → ~180×180 → ~80×60 ≈ 4× less → ~10ms → ~2–3ms. Preserves SS=4 (user-confirmed quality). Keep CPU↔shader stride (`x*map_h+y`) consistent with new dims.
- The GI compute field (A1) uses the same camera region — single source of region bounds.
- Fallback lever: `SDF_SUPERSAMPLE` 4→2 (last resort, quality tradeoff).

### B2. Attack the dominant sim span  (❌ Not started — needs measurement from B0 first)
- **build_map_cache** (map.cpp ~9778, already parallel-phased): finer dirty-gating of transparency/lightmap sub-caches (rebuild only on changed inputs). Correctness-sensitive; gate on measured share.
- **monmove**: already LOD + `monperf` sleep-skip — inspect sight-cache clearing / tier thresholds.
- **world_tick**: field decay over loaded submaps (`do_emits` already 10s-gated) — check iteration scope.

### Perf verification
Re-run, hold ≥20 turns, compare `[sim][perf]` + `structure_rebuild` + `[render][perf] frame_period`. Target: structure_rebuild < 3ms, max-frame down from 76ms, fps up from ~32.

---

## Critical files  (🔄 all Stage-1/2 structural files done; per-item remaining below)

| File | Change | Status |
|------|--------|--------|
| `src/lighting/shader_compiler.{h,cpp}` | A0 add COMPUTE compile path | ✅ Done |
| `src/lighting/gi_compute_pass.{h,cpp}` + `data/shaders/lighting/src/gi_field.comp.hlsl` + `gi_bounce.comp.hlsl` | A1/A3 compute gather → `gi_storage_` | ✅ Done |
| `data/shaders/lighting/src/sky_sun.comp.hlsl` | Stage 2a/2b directional sky/sun compute | ✅ Done |
| `src/lighting/sky_sun_pass.{h,cpp}` | Stage 2a/2b owns sky_buf_ | ✅ Done |
| `src/lighting/sprite_batcher.cpp` | A2 renumber storage slots; bind GiBuf + SkyBuf | ✅ Done |
| `src/lighting/render_state.{cpp,h}` | A3 own gi_/sky_; remove rc_ | ✅ Done |
| `src/sdl_render_frame.cpp` | A3 dispatch under gate; B1 region bounds | ✅ Done |
| `src/lighting/frame_build.{cpp,h}` | B1 region-limit DT (superseded by P3 JFA) | 🔄 JFA now handles |
| `src/lighting/sdf_pass.cpp` | Stage-2b occ_buffer + COMPUTE_STORAGE_READ flags | ✅ Done |
| `src/lighting/gpu_sdf_pass.{cpp,h}` | P3 JFA SDF pass | ✅ Done |
| `radiance_cascade_pass.{h,cpp}`, `rc.frag.hlsl`, `rc_bounce.frag.hlsl` | A3 **delete** | ✅ Done |
| `src/game.cpp` (~1818–2180), `src/map.cpp` (~9778) | B2 only if B0 points here | ❌ Waiting |

## Appendix — CPU-GI contingency  (🧊 Not needed — A0 spike confirmed compute works on D3D12)
Compute single-bounce on CPU in `frame_build.cpp` (splat emitter snapshot into per-tile RGB, SDF-occluded falloff + few wall-gated diffusion iters, camera region) → upload `gi_storage_` exactly as A2/A3 consume it. Robust but non-standard and adds main-thread cost (the Part-B tension). Same consumer swap, so only A0/A1 differ.

## Gotchas (module CLAUDE.md)
- Compute resource model ≠ graphics: declare readonly/readwrite storage counts + `threadcount` in `SDL_GPUComputePipelineCreateInfo`; bind via `SDL_BindGPUComputeStorageBuffers`.
- Fragment storage buffers are `register(tN, space2)`; HLSL decls + C++ bind indices move in lockstep.
- Storage buffers = verbatim CPU array, **x-major `arr[x*H+y]`, no transpose**.
- Each new `src/lighting/*.cpp` needs `#define dbg(x) DebugLogFL((x),DC::SDL)`.
- Never leave a declared SRV slot unbound on D3D12 → command-list corruption.
- Build: `cmake --build out/build/osx-arm-slim --target cataclysm-bn-tiles`; log at `~/Library/Application Support/Cataclysm-BN/config/debug.log` (`dbg(DL::Info)`; `DL::Debug` filtered). Win11/D3D12 is the correctness gate for Part A.
