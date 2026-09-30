# Lighting pipeline: precision-driven simplifications (exact + measured)

## Context

Critical, industry-benchmarked pass over the Cataclysm-BN-Forked lighting pipeline, removing expensive math whose precision the pixel-art style throws away anyway. Scope: **tier A** (provably no visible change) and **tier B** (small, pixel-art-justified, gated by pixel-diff + frame-time). The architectural "light cache" rewrite is out of scope; step 1's per-term attribution is the evidence any future such proposal must be judged against, and is recorded, not acted on, here.

- macOS builds `SHADER_TARGETS=spirv;msl`, Windows `dxil;spirv;msl` (`CMakeLists.txt` WIN32 branch), and ships `dxcompiler.dll`/`dxil.dll` beside the exe (`src/CMakeLists.txt:254-277`). `src/lighting/CLAUDE.md:596-600` names Win11/D3D12 the primary release target, but **execution and acceptance gating run on macOS only** (no Windows build on this machine, user decision 2026-09-30): steps ship on macOS evidence, every accepted step is recorded `Windows-unverified`, and step 10 writes a self-contained deferred Windows checklist into the plans/ copy.
- The game polls HARDCODED absolute trigger literals: `/tmp/cata_knob`, `/tmp/cata_dump_trigger`, `/tmp/cata_frame_N.bmp`, `/tmp/cata_map_N.json` (`sdl_input.cpp:521-561`, `lighting/gpu_device.cpp:146-150`, `sdl_lighting_devui.cpp:1252`). The harness therefore writes/polls trigger files under `$TRIG` (`/tmp` macOS; `<cwd-drive>:\tmp` Windows, where the same literal resolves), never under `$SCRATCH` — see step 0.2. The env channels `CATA_FRAME_DUMP=<frame>:<path>` / `CATA_MAP_DUMP=<frame>:<path>` are path-agnostic but read once at first use (`static`, `gpu_device.cpp:152`), so they cannot serve mid-session dumps; the harness uses the file trigger.
- Frame dumps are hand-written 24-bit BMP copying swapchain bytes 0-2 verbatim (`gpu_device.cpp:218-253`): channel order depends on the swapchain format (BGRA vs RGBA swaps R/B), and vendors rasterize differently. **Pixel baselines are per-platform only; never diff a macOS BMP against a Windows BMP.**
- Present mode: `SDL_ClaimWindowForGPUDevice` (`gpu_device.cpp:59`) creates the swapchain VSYNC; `render_state.cpp:51` then requests MAILBOX via `SDL_SetGPUSwapchainParameters` (`gpu_device.cpp:74-75`) whose bool return is **ignored**, and the `vsync=off` log (l.80) reports the requested flag, not the effective mode. If MAILBOX is unsupported (possible on Metal) the game stays vsync-quantized and `frame_period` snaps to refresh multiples. Step 0.4a adds the gate; no substitute metric is ever used.
- The per-turn legacy lightmap kernels (`src/shaders/lm_*_compute.hlsl`, precompiled per target by the `CATA_HLSL_SOURCES` glob) are already tile-resolution: `lm_raytrace_compute.hlsl` is an integer tile DDA (l.154-157) with one `sqrt`, radius early-out (l.113), one 8×8 group per source. No precision to remove; they are outside this plan.

Cost picture currently held: one prior datapoint, rc-rebuild `frame_period` ≈ 26.4 ms (~38 fps) on a ~180×180 scene (`src/sdl_lighting_devui.cpp` comment). Step 1 replaces it with a per-term breakdown on macOS before any tier-B money is spent.

Not touched, with reasons:
- **fp16/min16float:** the marches are load-bound, not ALU-bound; Khronos' 16bit_arithmetic sample says uplift is "very hard to guarantee"; DXIL honors min16float but the SPIR-V→MSL path largely ignores it — asymmetric payoff. Revisit only if step 1 shows ALU saturation.
- **Hardware-filtered SDF texture:** shadercross mis-binds sampler textures on Metal (`sprite.frag.hlsl:22-28`), and converting renumbers every fragment storage slot on D3D12 (root-signature churn).
- **Bloom:** already dual-Kawase, and `g_bloom_enable`=false.

## Approach

### Platform matrix (macOS column drives every step; Windows column feeds the deferred checklist)

|Slot|macOS (Metal/M1)|Windows (NVIDIA/D3D12)|
|---|---|---|
|Configure|already configured|`cmake --preset windows-tiles-sounds-x64-msvc` (preset requires vcpkg at `C:/vcpkg`; if absent, stop and report — do not improvise another toolchain)|
|Build|`cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles`|`cmake --build --preset windows-msvc-relwithdebinfo --target cataclysm-bn-tiles cata_test-tiles`|
|Game binary|newest mtime of `./cataclysm-bn-tiles` vs `out/build/osx-arm-slim/src/cataclysm-bn-tiles` (record `ls -l` + `md5` of both before/after each A/B)|newest-mtime match of glob `out/build/windows-tiles-sounds-x64-msvc/**/cataclysm-bn-tiles.exe`|
|Test binary|newest mtime of `./cata_test-tiles` vs `out/build/osx-arm-slim/tests/cata_test-tiles`|newest-mtime match of glob `out/build/windows-tiles-sounds-x64-msvc/**/cata_test-tiles.exe`|
|User dir|`~/Library/Application Support/Cataclysm-BN/`|`%LOCALAPPDATA%\cataclysm-bn\` (`path_info.cpp:48-51`)|
|debug log|`$SCRATCH/userdir/config/debug.log` (harness runs with `--userdir`, `main.cpp:352`; `path_info.cpp:106,161`)|same layout|
|`$TRIG` (trigger files the GAME polls)|`/tmp`|`<cwd-drive>:\tmp` (Windows resolves the literal `/tmp/x` to `<cwd-drive>:\tmp\x`; harness creates it before launch)|
|`$SCRATCH` (logs, harness, copied userdir, moved BMPs)|`/tmp/cbn_ab`|`<cwd-drive>:\tmp\cbn_ab`|

Standing rules: builds in background only, never killed, ≥1800 s timeout, from repo root; after each build compare binary mtime against build start before trusting any result; shaders load at runtime from `data/shaders/lighting/src/` (shader-only edits need relaunch, but still build once per step so commits stay coherent); launch cwd = repo root (datadir is cwd-relative, `path_info.cpp:74-85`); one atomic Conventional commit per passing step, no body.

### Step 0 — Plan copy, new knobs, present-mode log, rebuild counter, harness

1. Copy this file verbatim to `plans/lighting-precision-simplification.md`.
2. **Diagnostic paths stay as-is.** No new header, no C++ path change.
   The four game-polled literals stay hardcoded POSIX strings: `/tmp/cata_knob` (`sdl_input.cpp:521/522/561`), `/tmp/cata_dump_trigger` (`gpu_device.cpp:146/149`), triggered dump output `"/tmp/cata_frame_" + n + ".bmp"` (`gpu_device.cpp:150`), `"/tmp/cata_map_" + n + ".json"` (`sdl_lighting_devui.cpp:1252`). No C++ change. Because the game polls those ABSOLUTE paths, the harness writes/polls trigger files in `$TRIG` (= `/tmp` macOS, `<cwd-drive>:\tmp` Windows, created before launch) — writing them under `$SCRATCH` would time out every handshake. `$SCRATCH` = `$TRIG/cbn_ab` holds logs, harness scripts, the copied userdir, and moved BMPs.
3. **Expose the missing knobs.** The chain at `sdl_input.cpp:528-559` lacks `shadow_steps`, `max_shadow_k`, `gi_bilat`, `light_eps` (confirmed by reading the block). Append in the same style:
   - `else if( kn == "shadow_steps" ) dp.shadow_steps = static_cast<std::uint32_t>( std::max( 1.0f, kv ) );`
   - `else if( kn == "max_shadow_k" ) dp.max_shadow_k = kv;`
   - `else if( kn == "gi_bilat" ) dp.gi_bilat = kv;`
   - `else if( kn == "light_eps" ) dp.light_eps = kv;`
   - `else if( kn == "force_rc_rebuild" ) g_force_rc_rebuild = kv > 0.5f;`
   Extend the `Known names:` comment above the block. Types: `max_shadow_k` is `float` in DebugParams (`sprite_batcher.h:257`, cast to uint in HLSL), `shadow_steps` is `uint32_t` (`sprite_batcher.h:199`).
4. `extern bool g_force_rc_rebuild;` in `src/sdl_lighting_devui.h` next to `g_sky_sun_enable`; `bool g_force_rc_rebuild = false;` in `sdl_lighting_devui.cpp`. In `src/sdl_render_frame.cpp` (~l.239) OR it into `rebuild.structure`. Protocol: the F4 panel forces rebuild every frame on BOTH `structure` and `vis` (`devui_visible()`, l.239/247), so the panel stays CLOSED for every measured run and knobs move only through the file.

4a. **Present-mode gate (measurement-only fallback).** In `gpu_device.cpp` capture the bool returned by `SDL_SetGPUSwapchainParameters` (l.74-75, currently discarded) and extend the existing l.80 log line with `present=%s ret=%d`. Production behaviour is unchanged (still just requests MAILBOX). Only when env `CATA_MEASURE_IMMEDIATE=1` is set (the harness always sets it) and the MAILBOX call returned false, retry once with `SDL_GPU_PRESENTMODE_IMMEDIATE` and log the retry. Gate at step 1: the log must show `present=mailbox` or `present=immediate` with `ret=1`. `present=vsync` or `ret=0` ⇒ that platform's frame-period gates are NOT measurable — halt and report; no substitute metric. If the immediate fallback was needed, say so in the final report (benchmark sessions run unlocked; production unaffected).
5. **Rebuild-rate counter** so rebuild frames and steady frames separate in the log. In `sdl_render_frame.cpp`: file-static `int s_rebuild_in_window = 0;` bumped in `build_lighting` when `rebuild.structure` is true; in the `frame_perf` dtor's `n >= 120` block (l.1921-1937) append `" rebuilds=" << s_rebuild_in_window << "/120"` to the `[render][perf]` line and zero it with the other accumulators.
6. **Harness** `$SCRATCH/cbn_ab.py` (throwaway, never committed):
   - Args `--binary PATH --world NAME --label L --phases JSON` (list of `{"name":…, "knobs":{k:v}}`), `--settle 10 --measure 45 --scratch DIR`.
   - Userdir isolation: copy the real userdir's `config/` plus the one world dir to `$SCRATCH/userdir`; in the copied `config/options.json` set `AUTOSAVE_MINUTES` to 0. Launch `[binary, "--userdir", "$SCRATCH/userdir", "--dont-debugmsg", "--world", NAME]`, cwd = repo root, env `CATA_MEASURE_IMMEDIATE=1` (step 0.4a) plus any phase-requested extras like `CATA_LM_PROF=1` (step 9b), stdout/stderr → `$SCRATCH/L.log`. The real save is never opened.
   - Ready: poll `$SCRATCH/userdir/config/debug.log` for `frame heartbeat: 1500` (every 100 frames, `gpu_device.cpp:142-143`); 300 s timeout → fail.
   - Knob write: `name value` → `$TRIG/cata_knob`; wait ≤5 s for disappearance; require log `knob <name> = <value>` without `(unknown)`.
   - Phase: apply knobs, sleep settle, mark log size, sleep measure. Parse new `[render][perf] … frame_period avg=Xms` lines → median/mean/n_windows plus mean `rebuilds=k/120`. n_windows < 3 → phase fails.
   - Dump: freeze animated terms for determinism — `flicker_gain 0`, `cloud_strength 0`, `dust_enable 0`, `shaft_enable 0` (read the exact defaults from `sprite_batcher.h` DebugParams and restore those afterwards). Wait 2 s, create empty `$TRIG/cata_dump_trigger`, wait ≤10 s for `$TRIG/cata_frame_<n>.bmp`, move to `$SCRATCH/L-<phase>.bmp`.
   - Exit: SIGTERM / `taskkill /PID`, `/F` after 5 s. NEVER quit via menu (writes the save). Post-check `$SCRATCH/userdir/save/<World>/map.sqlite3-wal` absent-or-empty; non-empty ⇒ log it (autosave fired; tolerable but must be visible).
   - `$SCRATCH/cbn_diff.py A B` → `mean_abs_luma` (Rec.601 over BGR) and `pct_changed` (% pixels max-channel |Δ|>2), PIL+numpy, identical on both OSes.
7. World: alphabetically first directory under `$SCRATCH/userdir/save/` containing a `.sav` (macOS: `Bairdford`). Identical world/window-size/tileset every run, both platforms.

Commit: `chore(lighting): shadow/GI knobs, rebuild counter, present-mode log`.

### Step 1 — Cost attribution and gate calibration (no optimisation code)

Run on macOS; Windows re-gating is deferred (step 10 checklist) and can only demote later, never promote.

1. **Null pair:** two launches labels `base1`,`base2`, single phase `base`{}. `null_pct`,`null_luma` = `cbn_diff base1-base base2-base`. `noise_ms` = |median₁−median₂|; if > 0.5 ms rerun both with `--measure 120` and adopt that.
2. **Term attribution** (one launch, label `terms`), phases in order:
   `base`{}, `los_off`{vis_curve:0}, `steps1`{shadow_steps:1}, `k1`{max_shadow_k:1}, `bilat_off`{gi_bilat:0}, `gi_off`{gi_strength:0}, `eps_high`{light_eps:0.05}, `restore`{}.
   `k1` is a no-op until step 4 removes the shader's 16-floor — expect `k1`≡`base` pre-step-4 and rerun this launch after step 4's commit (one extra session) to get the real K-trace cost. Δmedian per term = per-feature GPU cost breakdown; record in the plans/ copy and retain at step 10. **If `los_off` Δ < 1.0 ms, step 8 is cancelled as not-worth-it.**
3. **Rebuild cost** (launch label `rebuild`): `nofb`{force_rc_rebuild:1, gi_feedback:0}, `fb`{force_rc_rebuild:1, gi_feedback:0.3}, `off`{force_rc_rebuild:0}. `G_REBUILD` = median(fb) − median(nofb) — the 3-iteration GI multiplier (iterations = `feedback>0.001 ? 3 : 1`, READ at `gi_compute_pass.cpp:285`). Sanity: `rebuilds≈120/120` in forced phases, ≈0 in `off`. **If G_REBUILD < 2.0 ms, step 9 is cancelled.**
4. **Gates, pre-decided** (paste actuals into the plans/ copy):
   - Tier A (steps 2-4, 6): diff vs `base1` within `null_pct+0.10 pp` ∧ `null_luma+0.05`, ∧ median frame_period ≤ baseline + `noise_ms`.
   - Tier B (steps 5, 7, 8, 9): diff within `null_pct+2.0 pp` ∧ `null_luma+1.0`, ∧ median gain ≥ 0.5 ms (≥ 1.0 ms for step 8). macOS fail ⇒ `git checkout` that step's files immediately, record measured-and-rejected. macOS pass ⇒ accepted, recorded `Windows-unverified` in the plans/ copy; the deferred Windows checklist may later demote (single `git revert`, shared branch). No judgement calls at review time.

### Step 2 — A1: cull off-view emitters from sprite shading (exact)

Exactness: `point_light_atten` (`attenuation.hlsl:3-8`) returns exactly 0 at dist ≥ radius and `sprite.frag` skips `atten <= 0`, so an emitter whose radius-disc cannot reach any visible `shade_pos` contributes exactly nothing.

Consumer audit of the emitter buffer (grepped `emitter_buffer()`, `last_count()`, `Emitters[`):
|Consumer|Count it gets|Note|
|---|---|---|
|sprite.frag loop via `emitter_count` (`render_state.cpp:421-428`)|**view count**|the cull target|
|`gi_field.comp` via `rp.emitter_count` (`sdl_render_frame.cpp:653`)|FULL|GI probes off-view tiles; reads the reordered-but-complete buffer, so GI changes only where >RC_K emitters tie at a probe — tier-A gate covers it|
|HUD glow overlay, godray shafts, dust, volumetric|CPU snapshot `result.snapshot_copy` (`frame_build.cpp:315-320`)|untouched — partition runs AFTER this copy|
|`s_emo.last_n_emit_pushed` (`sdl_render_frame.cpp:877`), `n_emit=` dbg line|FULL|diagnostics only|
|UI batcher|`emitter_count=0` forced (`sprite_batcher.cpp:744`)|unaffected|

Edits:
1. `src/lighting/snapshot.h`/`.cpp`:
   ```cpp
   /// Bubble-local whole-tile rectangle.
   struct emitter_view_rect { int x0 = 0; int y0 = 0; int w = 0; int h = 0; };
   /// Slack: tall sprites shade from a base tile below/left of the visible rows, and the
   /// camera pixel-slide (get_drawing_pixel_offset, sdl_render_frame.cpp:731) shifts shade_pos
   /// sub-tile; 4 tiles dominates both.
   inline constexpr int EMITTER_VIEW_MARGIN_TILES = 4;
   /// Stable-partitions so every emitter whose radius disc reaches view grown by the margin
   /// comes first; returns that count. Degenerate rect → untouched, returns size().
   auto partition_emitters_by_view( std::vector<gpu_emitter> &emitters, const emitter_view_rect &view ) -> int;
   ```
   Impl: grow rect by margin as floats; `dx = std::max( { rx0 - e.pos_x, 0.0f, e.pos_x - rx1 } )`, same for `dy`; keep iff `dx*dx + dy*dy < e.radius*e.radius` (strict `<`: atten is exactly 0 at dist == radius). `std::ranges::stable_partition`; return distance of the kept prefix. Margin rationale: the cull rect is camera rect + 4 tiles, which dominates the sub-tile slide (<1 tile) and tall-sprite base-tile reach (sprites taller than ~4 tiles on screen are rare and their base tile leaving the margin by >4 tiles means the visible top is off-screen too; the tier-A pixel gate catches any pop).
2. `src/lighting/frame_build.cpp`: partition AFTER `result.snapshot_copy = snapshot` (l.315-320) and BEFORE `rs.collector()->submit(...)` (l.321): `const auto view_count = have_world ? partition_emitters_by_view( snapshot, { .x0 = cam_x0, .y0 = cam_y0, .w = cam_w, .h = cam_h } ) : static_cast<int>( snapshot.size() );` (`snapshot` is the moved-into-submit vector; cam_* are already parameters, l.30).
3. `src/lighting/emitter_collector.h`/`.cpp`: `submit(...)` gains trailing `int view_count = -1` stored in `pending_view_count_`; `flush_to_render_cb` next to `last_count_.store` adds `last_view_count_.store( pending_view_count_ < 0 ? count : std::min( pending_view_count_, count ) ); pending_view_count_ = -1;`; new `int last_view_count() const noexcept` documented "emitters that can light the camera rect; first N slots of emitter_buffer()".
4. `src/lighting/render_state.cpp:421`: `const Uint32 ne = collector_ ? static_cast<Uint32>( collector_->last_view_count() ) : 0u;`. GI (`sdl_render_frame.cpp:653`) and the dbg lines (867/877) keep `last_count()`.
5. New test `tests/lighting_emitter_cull_test.cpp` (tag `[lighting]`; `tests/CMakeLists.txt:5-6` globs `tests/*.cpp` — confirmed): degenerate rect returns size() order-unchanged; disc tangent to the grown rect (distance == radius exactly) is culled while radius+0.01f survives; kept emitters preserve relative order. Exactly-representable pos/radius values only.

Commit: `perf(lighting): cull off-view emitters from sprite shading`.

### Step 3 — A2: tile-centre SDF into GiBuf's pad channel (exact)

Removes 4 `sdf_bilinear` (16 loads) per GI-lit fragment. GiBuf's 4th float is written `0.0` by resolve and read nowhere (verified: `gi_field.comp` reads PrevGiBuf channels 0-2 only, `gi_compute_pass.cpp:442` sums rgb only, `sprite.frag` `indirect_texel` uses rgb).

1. `data/shaders/lighting/src/rc_resolve.comp.hlsl`: add `StructuredBuffer<float> SdfBuf : register(t1, space0);`; `#include "rc_shared.hlsl"` after the cbuffer (its contract names `SdfBuf/sdf_map_w/sdf_map_h/sdf_ss` all exist there — verified against `rc_shared.hlsl:10-16`); replace `GiOut[go + 3u] = 0.0;` with `GiOut[go + 3u] = sdf_bilinear( float2( tid.xy ) + 0.5 );`; header comment: `.a = SDF at tile centre (sprite.frag bilateral GI weight)`.
2. `src/lighting/gi_compute_pass.cpp` resolve bind (~l.392): `SDL_GPUBuffer* ro[2] = { rc_atlas_, sdf_buf };` count 2; reflection expectations comment `(expects ro_sb=2 rw_sb=1)`; the l.76 comment `RC resolve: 2 readonly (atlas, sdf)`.
3. `sprite.frag.hlsl` (~l.284-331): `indirect_texel` returns `float4`; `indirect_bilinear` uses `.rgb` for the value and `a.w/b.w/c.w/d.w` in place of the four `sdf_bilinear(float2(xN,yN)+0.5)` weight calls; `sd_c = sdf_bilinear(p)` stays live. Difference only at the bubble border where clamped-texel values replace extrapolated ones — off-screen.
4. Lockstep note: no `.msl` regeneration needed — lighting shaders compile at runtime; but if the tree's precompiled `.msl` set is ever rebuilt by CI it picks this up automatically (they are generated from the same HLSL).

Commit: `perf(lighting): precompute tile-centre SDF for bilateral GI`.

### Step 4 — A3: honest top-K selection + static-size tracking arrays (exact-ish, tier A gate)

`sprite.frag.hlsl:745-791` today: `k_max = max(max_shadow_k, MAX_SHADOW_K_DEFAULT)` (knob can never lower K), no clamp of `k_max` to the 64-entry arrays (values >64 = out-of-bounds), and replacement compares against `top_val[0]` instead of the tracked weakest. Additionally `uint top_idx[64]; float top_val[64];` (l.747-748) are 128 potentially-spilling registers per fragment; the real cap is 16.

Edit:
```hlsl
const int k_max = (max_shadow_k > 0.5) ? min((int)max_shadow_k, MAX_SHADOW_K_DEFAULT) : MAX_SHADOW_K_DEFAULT;
uint top_idx[MAX_SHADOW_K_DEFAULT];
float top_val[MAX_SHADOW_K_DEFAULT];
int top_n = 0;
int weakest = 0;
// fill:  if (top_n < k_max) { top_idx[top_n]=ei; top_val[top_n]=v; weakest = (top_n==0||v<top_val[weakest]) ? top_n : weakest; ++top_n; }
// replace: else if (v > top_val[weakest]) { top_idx[weakest]=ei; top_val[weakest]=v; weakest = argmin over top_val[0..top_n); }
```
where `v = atten * lambert` (same expression as today's l.781). `MAX_SHADOW_K_DEFAULT` stays 16 (l.248). Behaviour differs from today only where >16 emitters pass eps at a pixel (there the new code shadows the genuinely-strongest set — a bug fix) or where the knob was set below 16 (today silently ignored). Spill-count proof is impossible here (no Xcode GPU capture on this machine, no shadercross -Qspectre dump); the array shrink is justified by construction (dynamic-indexed local arrays of 64 are the classic DXC/Metal spill bait) and measured end-to-end by the frame-time gate.

Slider cap (user decision): `data/gui/devui.rml:333` `max="64"` → `max="16"` so the F4 panel cannot express a range the shader clamps away (bind at `sdl_lighting_devui.cpp:760` unchanged).

Commit: `fix(lighting): honor max_shadow_k below 16, track true K strongest, shrink top-K arrays, cap devui slider`.

### Step 5 — A4: RC cascades 5 → 4 (**tier B, reach trade-off**)

Not exact: cascade 4 carries 85-341-tile hits, so dropping it removes GI contributions from emitters/bright fields >85 tiles from a probe. `src/lighting/rc_params.h:29-34` already records 4-cascade (reach 85 tiles) as sanctioned for scenes smaller than the reach gap; the reality bubble is ~180 tiles and the viewport 46×26, so the loss is GI arriving through geometry farther than 85 tiles — implausible to matter in-town, measurable by the gate.

Set `RC_CASCADES = 4u` at all four lockstep sites (complete list, grepped): `src/lighting/rc_params.h:35`, `rc_build.comp.hlsl:33`, `rc_merge.comp.hlsl:37`, `rc_resolve.comp.hlsl:16`. The `uint4 geom[RC_CASCADES]` cbuffer shrinks with them; `static_assert(sizeof(rc_params_gpu) == 32 + RC_CASCADES*16)` in `rc_params.h` follows automatically and the atlas allocation derives from `rc_total_floats` (same constant). `grep -rn "RC_CASCADES = 5" src data/shaders` must return 0 after. Rewrite the `rc_params.h` doc comment to "4 cascades reach 85 tiles". Gate: tier-B diff + frame-time; if it fails, bisect by reverting only these four lines.

Commit: `perf(lighting): drop radiance cascade beyond the reality bubble`.

### Step 6 — Tier-A verification launch + B1: shadow K default 16 → 8

1. Build; launch label `tierA`, phases `base`{}, `k8`{max_shadow_k:8}, `k16`{max_shadow_k:16}.
2. Tier-A acceptance for steps 2-4 combined: `cbn_diff base1-base tierA-base` within tier-A thresholds; median(tierA.base) ≤ median(base1) + `noise_ms`; debug.log clean: no `frag binding layout mismatch`, no `pipeline create failed`, GI reflection lines show `ro_sb=2` for resolve.
3. B1 gate (tier B): diff `tierA-k8` vs `tierA-k16` (same session, null≈0) within tier-B thresholds, AND median(k16) − median(k8) ≥ 0.5 ms.
   - Pass ⇒ set `max_shadow_k = 8.0f` (`sprite_batcher.h:257`) and `MAX_SHADOW_K_DEFAULT = 8` (`sprite.frag.hlsl:248`, which also shrinks the step-4 arrays to 8 — update the "64 covers extreme horde density" comment to match). Commit `perf(lighting): shadow-trace 8 strongest emitters per pixel`.
   - Fail (including "scene has ≤8 in-range emitters, nothing to measure") ⇒ keep 16, record why.

### Step 7 — B3: nearest-texel SDF on far sphere-trace steps

The JFA SDF saturates at `SDF_FLOOD/SDF_SS` = 2 tiles (`jfa_shared.hlsl:5`, `jfa_resolve.comp.hlsl:36`), so far-field steps traverse a flat field; bilinear there costs 4 loads across 2 cache lines (x-major layout, row stride `map_h*8` floats) for information nearest-texel already carries within ±0.0884 tiles.

1. Every `shadow_trace.hlsl` includer defines `sdf_bilinear` today (`sprite.frag.hlsl:262`, `gi_field.comp.hlsl:86`; `rc_shared.hlsl:34` serves rc_build/merge/resolve). Add a sibling `sdf_nearest(float2 p)` next to each `sdf_bilinear` definition: `sdf_texel((int)floor(p.x*SDF_SS), (int)floor(p.y*SDF_SS))` (SS = `SDF_SS` int in sprite/gi_field, `(int)sdf_ss` in rc_shared). Update the contract comment `shadow_trace.hlsl:10-12` to require BOTH functions. `gi_field.comp.hlsl` includes `shadow_trace.hlsl` at l.102 but does NOT include rc_shared — it has its own copies; add `sdf_nearest` there too.
2. `shadow_trace.hlsl` main loop (l.94-127):
   - `static const float SDF_FAR_TILES = 1.0;` and `static const float SDF_NEAREST_SLACK = 0.0884f; // sqrt(2)/(2*8): max point-to-texel-centre error; SDF is 1-Lipschitz`.
   - Inside the loop: `const bool far = prev_sd > SDF_FAR_TILES && prev_sd < 1e9; const float sd = far ? sdf_nearest(q) : sdf_bilinear(q);`
   - Hit test `sd < 0.05` and the Aaltonen triangulation consume `sd` unchanged; only the advance gets conservative: `prev_step = max(far ? sd - SDF_NEAREST_SLACK : sd, 0.15);` (slack ≥ max bilinear-vs-nearest deviation keeps the march strictly conservative; the 0.15 floor already dominates near occluders).
   - The self-shadow escape loop (l.81-87) keeps `sdf_bilinear` (near-field by definition).
3. Gate: tier-B diff vs latest accepted BMP + ≥0.5 ms. Open terrain has mostly far steps, so expect the biggest win in towns' open streets; revert on fail.

Commit: `perf(lighting): nearest SDF samples on far sphere-trace steps`.

### Step 8 — B2: cached vision-carve LOS field (only if G_LOS ≥ 1.0 ms)

The eye is the player tile centre (`in.debug.player_x/y = bub_pos()+0.5`, `sdl_render_frame.cpp:821-822`, no slide), so the field changes exactly when `rebuild.structure`/`rebuild.vis` fire or the knobs change — gate recomputation on that, not per frame. Ground fragments read the cached bilinear sample; **tall fragments (`frag_is_tall_n`, `sprite.frag.hlsl:629`) keep the live march** (different self_eps, base-tile origin).

1. **New `data/shaders/lighting/src/los_field.comp.hlsl`:**
   - `StructuredBuffer<float> SdfBuf : register(t0, space0); RWStructuredBuffer<float> LosBuf : register(u0, space1);`
   - `cbuffer LosParams : register(b0, space2) { uint sdf_map_w; uint sdf_map_h; uint sdf_ss; uint shadow_steps; int x0; int y0; uint w; uint h; float eye_x; float eye_y; float shadow_k; float los_pad0; }` (48 bytes; `static_assert(sizeof(los_params)==48)` on the C++ mirror).
   - `#include "rc_shared.hlsl"` then `#include "shadow_trace.hlsl"` (contract satisfied by the cbuffer names + rc_shared's SdfBuf).
   - `[numthreads(8,8,1)]`: thread (0,0) writes header `LosBuf[0..7] = {1, x0, y0, w, h, 0,0,0}`; every thread with `tid.x < w*sdf_ss && tid.y < h*sdf_ss` computes `p = float2(x0,y0) + (float2(tid.xy)+0.5)/sdf_ss`, `ev = float2(eye_x,eye_y)-p`, `ed=length(ev)`, `los = ed < 0.5 ? 1.0 : soft_shadow_march(p, ev/ed, ed, shadow_k, (int)shadow_steps, 0.05, false)` — the exact ground-fragment args from `sprite.frag.hlsl:1311-1314` — stores at `LosBuf[8 + tid.x*(h*sdf_ss) + tid.y]`.
2. **New `src/lighting/los_field_pass.{h,cpp}`** — structural copy of `sky_sun_pass` (init/resize/shutdown/ready/record/create_buffer/zero_buffer, same `compile_compute_pipeline(..., "los_field.comp")` call shape, reflection log `(expects ro_sb=1 rw_sb=1)`). Buffer `max_w*max_h*64 + 8` floats, usage `COMPUTE_STORAGE_WRITE | GRAPHICS_STORAGE_READ` (mirror of `gi_out_buf_`, `gi_compute_pass.cpp:121`), zeroed at init ⇒ header valid=0 ⇒ fragment falls back to live march.
3. **Wiring:** member `los_field_pass los_` + accessor in `render_state.h/.cpp`; mirror every `sky_.` lifecycle call (init l.155, shutdown l.308, resize wherever `sky_.resize` appears — grep `sky_\.` in render_state.cpp) with `los_.` equivalents; `.los_buf = los_.los_buffer()` in the `set_lighting_resources` aggregate (`render_state.cpp:425-445`).
4. **Binding slot (DXC-strip-safe):** `FRAG_SBUF_COUNT = 8u` (`frag_slots.h:12`); `lighting_resources` gains `SDL_GPUBuffer* los_buf = nullptr;` (doc: fragment storage slot 7 → t9/space2); `sprite_batcher.cpp` impl gains `lp_los_buf`, extends the all-non-null guard (l.856) and the `sbufs[FRAG_SBUF_COUNT]` array (l.858-860); `sprite.frag.hlsl` declares `StructuredBuffer<float> LosBuf : register(t9, space2);` after l.91 **and reads it unconditionally at top of main** — `const float los_hdr_valid = LosBuf[0];` — so DXC cannot strip the slot and shift the D3D12 root signature (the exact failure `sprite_batcher.cpp:403-413` asserts on). The carve block then consults `los_hdr_valid` before the bilinear fetch.
5. **Carve edit (`sprite.frag.hlsl:1304-1322`):** `bool los_cached(float2 p, out float v)` returns false unless `los_hdr_valid > 0.5`, `g = (p - float2(hdr.x,hdr.y))*SDF_SS - 0.5` lies inside `[0, hdr.w*SDF_SS-1]×[0, hdr.h*SDF_SS-1]`; else bilinear over `LosBuf[8 + sx*(hdr.h*SDF_SS)+sy]`. Carve becomes `los = (ed<0.5) ? 1.0 : ((!frag_is_tall_n && los_cached(shade_pos, c)) ? c : soft_shadow_march(...existing...))`. Everything downstream (`pow`, `vis_radius`, `mem_dim`) untouched.
6. **Dispatch (`sdl_render_frame.cpp` `flush_and_gather_rc`, after the GI block):** file-static `struct los_key { int eye_x, eye_y, z, x0, y0, w, h; float shadow_k; std::uint32_t shadow_steps; auto operator<=>(const los_key&) const = default; // *NOPAD* };` + `std::optional<los_key> s_last_los_key;`. When `rs.los().ready() && rs.sdf().populated() && g`: region = camera rect grown 2 tiles clamped to `rs.sdf().map_w()/map_h()`; skip if empty; record iff `rc_rebuild || !s_last_los_key || *s_last_los_key != key`; store key. Params from `g->u.bub_pos()` (+0.5), `SDF_SUPERSAMPLE`, `g_dbg_params.shadow_k/shadow_steps`. Camera rect stashed by `build_lighting` into a file-static (same place step 2's rect comes from, `sdl_render_frame.cpp:303-327`).
7. Gate: tier-B diff + ≥1.0 ms. Revert all step-8 files (incl. new ones) on fail.

Commit: `perf(lighting): cache vision-carve line of sight per sub-cell`.

### Step 9 — B4: amortize GI feedback iterations across frames (only if G_REBUILD ≥ 2.0 ms)

`gi_compute_pass::record` runs the full field→build→merge→resolve chain `iterations = feedback>0.001 ? 3 : 1` times inside one rebuild (`gi_compute_pass.cpp:285-286`, READ). Spread iterations 2-3 onto the following frames:
- Extract the loop body into `private: auto record_iteration( const gi_iteration & ) -> bool;` (repo rule: >3 params ⇒ options struct) with `struct gi_buffers { SDL_GPUBuffer* emitter; SDL_GPUBuffer* sdf; SDL_GPUBuffer* sky; SDL_GPUBuffer* albedo; };` and `struct gi_iteration { SDL_GPUCommandBuffer *cmd; gi_buffers bufs; std::uint32_t w; std::uint32_t h; gi_params params; };` (returns false where the body early-`return`s).
- `record()` runs one iteration, then `pending_iters_ = (params.gi_feedback > 0.001f ? 2u : 0u); pending_ = {w, h, params};`.
- Public `record_pending(cb, bufs)`: while `pending_iters_ > 0 && ready()`, run one stored iteration, decrement.
- Caller `sdl_render_frame.cpp:686` adapts to `gi_buffers`; in `flush_and_gather_rc` when GI did NOT record this frame, call `rs.gi().record_pending(ctx.cmd_buffer, { rs.collector()->emitter_buffer(), rs.sdf().sdf_buffer(), rs.sky().sky_buffer(), rs.sdf().albedo_buffer() })`.
- Update the comment block l.275-285: the series converges over 3 consecutive frames instead of 3× in one.
Gate: `rebuild_fb`-equivalent median drops ≥1.5 ms vs step 1, tier-B diff of a steady-state dump ≥2 s after load. Revert on fail.

Commit: `perf(lighting): spread GI feedback iterations over frames`.

### Step 9b — Per-turn lightmap path, measurement only (nothing shipped)

Context (verified this session): the per-turn kernels `src/shaders/lm_*_compute.hlsl` are already tile-resolution integer DDAs with radius early-outs — no precision to strip. Their cost lever is dispatch count / dirty-region size (`src/compute/gpu_lm.cpp`: `collect_sources` l.2860, `pack_inputs` l.2902, raytrace pipeline l.233). This step only measures whether that path matters, to decide a possible follow-up plan.

1. Throwaway env-gated probe (never committed): in `gpu_lm.cpp`'s per-turn lighting entry, behind `getenv("CATA_LM_PROF")`, bracket source collection, packing, and the raytrace/color-raytrace dispatches with `std::chrono` timers; count dispatched groups (`ceil(cache/8)^2` × z levels × sources); log one `[lm-prof] collect=…ms pack=…ms trace=…ms groups=N sources=M` line per turn to debug.log.
2. Harness launch label `perturn`, phase `base`{}; walk the avatar ~30 tiles (arrow keys via the `computer` tool; if synthetic input is dead as in past sessions, measure stationary idle and label it such); harvest ≥50 `[lm-prof]` lines.
3. Record medians in the plans/ copy and retain in Hindsight. Decision rule: per-turn lighting ≥ 5 ms median ⇒ open a separate dispatch-scoping plan (dirty-level narrowing, source culling); < 5 ms ⇒ close the per-turn question; this plan stays per-frame-only.

### Step 10 — macOS final verification, memory, deferred Windows checklist

1. **macOS final launch** label `final`: phases `base`, `los_off`, `rebuild_fb`. Report medians vs step 1 baselines.
2. `cata_test-tiles "[lighting]" --rng-seed 1` and `cata_test-tiles "[.gpu]" --rng-seed 1` green on macOS (the [.gpu] golden-image suite is the pixel contract, `tests/lighting_gpu_test.cpp:1-40`).
3. Append a `## Deferred Windows verification` section to `plans/lighting-precision-simplification.md`, self-contained for whoever runs it later, listing per accepted step: commit sha, one-line change, gate numbers achieved on macOS. Procedure it must prescribe:
   - Configure/build per the Windows matrix rows; run `cata_test-tiles "[lighting]" --rng-seed 1` first.
   - Null pair: two harness launches `win-base1`/`win-base2` phase `base`{} at branch tip; that pair defines the Windows platform null (`win_null_pct`/`win_null_luma`/`win_noise_ms`). Debug log read from `$SCRATCH\userdir\config\debug.log` (harness `--userdir`), NOT `%LOCALAPPDATA%`.
   - Per-step re-gate: steps WITHOUT a runtime revert-switch (5: `RC_CASCADES` compile-time; 7: shader path; 8: cache presence; 9: schedule change) are A/B'd by **building the step's parent commit vs the step commit** and diffing `base` phases. Steps WITH knob pairs (2/4/6 via `max_shadow_k`, GI via `gi_strength`) may use the single-session knob pair instead.
   - Demote rule: any step whose gain sign flips, whose magnitude falls under half the macOS gain, or whose parent-vs-step diff exceeds `win_null+2.0pp/+1.0` gets `git revert <sha>` on the shared branch and the checklist row marked `rejected-on-Windows`.
4. Retain results in Hindsight: `POST http://127.0.0.1:8888/v1/default/banks/dsh-cbn/memories` `{"items":[{"content":"<per-term attribution table, accepted/rejected steps with medians, all marked Windows-unverified + deferred checklist location> | lighting-perf 2026-09-30","context":"lighting-precision-simplification"}]}`.

### Step 11 — GLOSSARY.md (docs, after the last accepted code step)

Create root `GLOSSARY.md` per the domain-modeling format (definitions only, `_Avoid_` synonym lists, no implementation detail). Context heading: "Rendering & lighting". Terms, each 1-2 sentences:
- **Reality bubble**: the world volume around the player held resident in caches and lit each frame. _Avoid_: view, bubble (bare)
- **Emitter**: a world-position light source gathered per frame into the emitter buffer. _Avoid_: light source, lamp
- **Shade position**: the world point a fragment's lighting is evaluated at (a tall sprite shades from its base tile, not its drawn tile). _Avoid_: sample point
- **Vision carve**: the per-pixel line-of-sight dimming sprite.frag applies against the eye. _Avoid_: shadow ray, LOS fade
- **Structure rebuild**: the frame a cached lighting field is recomputed because world structure changed; distinct from a refresh, which reuses the cache. _Avoid_: dirty/invalidate (as nouns)
Drafts — verify each against the code at the Critical-files anchors before writing; drop any term the code contradicts.

## Critical files & anchors

- `data/shaders/lighting/src/sprite.frag.hlsl`: l.249-331 SDF/GI helpers (step 3), l.741-854 emitter loop/top-K (steps 2/4/6), l.1304-1322 vision carve (step 8) — reread before editing, 1519-line file.
- `data/shaders/lighting/src/shadow_trace.hlsl` l.68-129: the shared march consumed by sprite.frag, gi_field, and (step 8) los_field — step 7 edits it.
- `src/lighting/gi_compute_pass.cpp` l.264-398: GI record loop + resolve bind (steps 3/9).
- `src/sdl_render_frame.cpp`: l.217-263 rebuild flags, l.303-327 camera rect, l.513-700 flush_and_gather_rc, l.1895-1940 frame_perf (steps 0/2/8/9).
- `src/lighting/sprite_batcher.cpp` l.400-413 + l.850-861: the FRAG_SBUF_COUNT assertion and the all-or-none bind guard step 8 must extend.

## Verification

- Per build: debug.log contains the GI reflection lines with expected counts (`resolve … ro_sb=2` after step 3), no `pipeline create failed`, no `frag binding layout mismatch`; binary mtime > build start.
- Step 2 unit test: `cata_test-tiles "[lighting]" --rng-seed 1` — three new cull cases pass.
- Tier A (steps 2-4, 6): BMP diff vs `base1` within `null+0.10pp/+0.05` AND frame-period median non-regressing beyond `noise_ms` — on macOS at step 6.
- Tier B (steps 5, 7, 8, 9): each step reports (Δmedian ms, mean_abs_luma, pct_changed) against its pre-decided gate; kept or reverted mechanically, no discretion.
- End-to-end: `final-base` vs `base1` median frame_period is the headline number; plus the step-1 term table showing where the remaining cost sits.

## Assumptions & contingencies

- **Idle does not free-run** (<3 `[render][perf]` windows in the measure window): press `x` (look-around, no turn cost) once via the `computer` tool after load and press Escape before the dump; if still starved, measure every phase with `force_rc_rebuild 1` (continuous frames guaranteed) and report that the steady-state numbers are rebuild-dominated.
- **First-alpha world missing/corrupt:** use the alphabetically-next save; the same world for every launch.
- **Windows acceptance is deferred by decision:** nothing in steps 0-9 may block on a Windows run; every accepted step ships marked `Windows-unverified` and the step-10 checklist is the only Windows obligation.
- **`noise_ms` > 1.0 ms even at 120 s windows:** tier-B gates compare 3 launches per variant by median-of-medians.
- **`AUTOSAVE_MINUTES` default 5** (`options_registration.cpp:326-329`): the harness copies the config and sets it to 0, so autosave cannot fire; the WAL post-check stays as the tripwire if the option name ever moves.
- **No Xcode GPU capture / RenderDoc automation in this environment:** cost attribution is knob-ablation frame-time only; spill-count claims are avoided (step 4 justifies its array shrink by construction + end-to-end timing).
