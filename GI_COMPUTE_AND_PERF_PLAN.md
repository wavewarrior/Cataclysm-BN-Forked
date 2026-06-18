# Plan: D3D12-robust GI (GPU compute) + do_turn / lighting perf

## ★ Update 2026-06-18 — findings, staged sequence, Mac-side gate

**Goal (refined).** Three things at once: (1) GI that creates + renders on D3D12
with no pipeline roulette; (2) the sun/sky **quality upgrade** — the sun is the
dominant light (full-moon = sun with different params), so unlike for point
lights, RC's angular/cascade machinery genuinely earns its keep on the
directional+sky-dome case; (3) Mac/Win **structural parity** so we stop fixing
bugs back-and-forth across machines.

**Findings (this session, via the new gate below):**
- **DXIL compiles for ALL 18 lighting shaders on the Mac, including
  rc.frag/rc_bounce.** So the Win11 failure is **NOT DXC codegen** — the bytecode
  is valid. It is **`SDL_CreateGPUGraphicsPipeline` root-signature construction**
  from reflection. This reframes the rc.frag diagnostic stub's "the dynamic
  `[loop]` is the trigger" conclusion — the *same* dynamic emitter loop is in
  sprite.frag (sprite.frag.hlsl:409-420).
- **Likely real trigger = a fragment storage buffer with NO leading sampler.**
  sprite.frag (works) = `smp=1 storage[tex=2 buf=5]` (sampler at t0, storage
  after — SDL's documented sampled→storage-texture→storage-buffer t-order).
  rc.frag / rc_bounce / vol.frag = `smp=0`, storage at t0 → suspected E_INVALIDARG.
  **UNCONFIRMED on Win11** — the fork test decides.
- Vertex-stage storage buffers with no sampler (sprite.vert/shadow.vert) are
  fine — the issue is fragment-specific.

**Fork test (owed, Win11 — decides cheap-fix vs compute-mandatory):**
1. *Does the game render a LIT scene on D3D12 right now* (a lamp/fire lighting
   tiles)? **YES** → sprite.frag's sampler-led layout creates fine → the bug is
   the no-sampler layout → cheap fix = add a leading sampler to the sampler-less
   frag passes; compute still wanted for sun/sky quality but not urgent. **NO** →
   sprite.frag itself won't create on D3D12 → dynamic fragment loops are dead on
   this toolchain → compute is mandatory for ALL lighting.
2. *Run the A0 compute spike* (built: `src/lighting/compute_spike.cpp`) → GO/NO-GO.

**Staged sequence (do NOT land port + hierarchy as one diff on the backend you
can't see):**
- **Stage 0 — gates.** Fork test + A0 spike on Win11. Mac-side reflection gate
  green-able (below).
- **Stage 1 — PORT current RC math to compute; reach D3D12 == Metal on what
  exists today.** New `gi_compute_pass` + `gi.comp.hlsl` (Part A1-A3 below).
  Delete `radiance_cascade_pass` + `rc.frag` + `rc_bounce.frag` → this also clears
  their gate warnings. Pure robustness/parity move, small algorithmic risk.
  **LOCK PARITY HERE.**
- **Stage 2 — add the directional cascade hierarchy for sun/sky** (the quality
  upgrade) on the proven-parity base. Angular bins live in compute (the construct
  the fragment stage chokes on; compute is its natural home). If it breaks, you
  know it's the new math, not the port.

**Mac-side parity gate (BUILT this session): `tools/shader_check/shader_reflect_check`.**
Compiles every lighting HLSL to SPIRV + DXIL via the same shadercross the game
uses, reflects, lints — no GPU device, runs on macOS. ERROR on SPIRV/DXIL/reflect
failure; WARN (→ error under `--strict` / `-DSHADER_CHECK_STRICT=ON`) on a
fragment storage-buffer-with-0-samplers. Run:
`cmake --build out/build/osx-arm-slim --target shader_reflect_check && out/build/osx-arm-slim/tools/shader_check/shader_reflect_check`.
Moves the reflection-class bugs (the back-and-forth) onto the Mac. `gi.comp.hlsl`
gets checked too and dodges the fragment sampler rule entirely.

**Parity discipline (compute ≠ parity by itself).** Compute narrows the
divergence surface (no vertex pairing → dodges shadercross #169 signature-strip;
distinct reflection model → dodges the sampler-order root-sig) but the Mac/Win
divergence is shadercross MSL-vs-DXIL codegen and exists in compute too (#157:
Vulkan defaults storage buffers readonly, D3D12 readwrite → mark `readonly`
explicitly; the spike does). Real parity = the reflection gate + structural-
parity-not-pixel-parity expectations + pinned shadercross version.

**vol.frag (NOT in this GI migration — stays a fragment pass).** Sampler-less by
design. If the fork test confirms the no-sampler hypothesis, rebind sampler-first:
add `Texture2D Dummy : register(t0,space2)` + `SamplerState : register(s0,space2)`,
bump `SdfBuf`→t1 / `SkyVisBuf`→t2, and bind a sampler + dummy texture in
`volumetric_pass::record` (storage `first_slot` stays 0 — the sampler bind is a
separate slot counter). The `--strict` gate tracks this until done.

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

## Part A — GI: GPU compute gather → storage buffer

### A0. Compute infrastructure + spike gate (go/no-go)
- **Add a COMPUTE compile path** to `shader_compiler.{h,cpp}`: `SDL_SHADERCROSS_SHADERSTAGE_COMPUTE` via `SDL_ShaderCross_CompileComputePipelineFromHLSL` → `SDL_CreateGPUComputePipeline`. Compute declares its own resource model in `SDL_GPUComputePipelineCreateInfo` (`num_readonly_storage_buffers/textures`, `num_readwrite_storage_buffers/textures`, `num_uniform_buffers`, `threadcount_x/y/z`) — different from the graphics reflection.
- **SPIKE**: a minimal compute shader — dynamic `[loop]` over a `StructuredBuffer<float>` (readonly) writing one `RWStructuredBuffer<float>`. Build + run on **Win11/D3D12**. Read `debug.log`:
  - **Creates (no `E_INVALIDARG`/`0x80070057`)** → GO: the fragment-stage bug does not apply to compute; proceed A1+.
  - **Fails** → NO-GO: fall back to the **CPU-GI contingency** (appendix below). This is the explicit decision gate before investing in the real shader.
- This plumbing is required for the real rework regardless, so the spike is not throwaway.

### A1. `gi_compute_pass` (replaces `radiance_cascade_pass`)
- New `src/lighting/gi_compute_pass.{h,cpp}` + `data/shaders/lighting/src/gi.comp.hlsl`. Port the RC gather/bounce math from `rc.frag`/`rc_bounce.frag` into one compute shader:
  - **Readonly storage**: Emitters SB, SDF SB (reuse the existing buffers, same data the fragment passes consumed).
  - **Readwrite storage**: `gi_storage_` — tile-resolution RGB radiance over the **camera region + shadow margin** (low-freq; no supersampling, no transpose).
  - One thread per tile: gather emitters with SDF-occluded falloff (reuse `trace_shadow`/`sdf_bilinear` logic), optional bounce iterations. Dispatch `ceil(W/8)×ceil(H/8)` groups.
- Compute→graphics dependency: dispatch (write `gi_storage_`) precedes the sprite pass (read) on the same CB. This is the **standard compute→graphics barrier** SDL_GPU is designed to insert — verify it holds on D3D12 during early bring-up (lower-risk than the fragment color-target→storage-read edge it replaces).

### A2. Swap the sprite.frag consumer (`sprite_batcher.cpp` embedded HLSL + `bind_lighting_resources`)
- Remove `Texture2D<float4> IndirectTex` (storage texture) + its bind. Add `StructuredBuffer<float4> GiBuf` at the next fragment storage-buffer slot.
- **Renumber `register(tN, space2)` decls and the C++ `SDL_BindGPUFragmentStorageBuffers` slot indices in lockstep** (binding-order mismatch is a documented D3D12 crash).
- GI read: `dyn += gi_strength * gi_bilinear(world_pos)`, x-major `arr[x*H+y]` + `p-0.5` centre (mirror `sdf_bilinear`). `gi_strength` knob (F4 Alt+F8/F9) unchanged.
- `ShadowMask` becomes the only storage texture → the all-or-none 2-slot hazard is gone.

### A3. Wire + delete
- `render_state.{cpp,h}`: replace `rc_` with `gi_compute_pass`; own `gi_storage_`/`xfer_gi_` (or in `sdf_pass`/`emitter_collector`, uploaded in the same copy pass, `cycle=false`).
- `sdl_render_frame.cpp`: replace `rc().record()` + flush-and-gather with the compute dispatch, under the existing per-tile/structure gate; retain buffer on skip frames.
- **Delete**: `radiance_cascade_pass.{h,cpp}`, `rc.frag.hlsl`, `rc_bounce.frag.hlsl`, `cascade_tex_`/`radiance_field_tex_`, RC readback oracle. Keep `gi_strength` + the debug-mode slot.

### GI verification
- Metal build (`cmake --build out/build/osx-arm-slim --target cataclysm-bn-tiles`): one off-center light + `gi_strength` up → colored fill into shadow / around corners, wall-occluded, tracks a moving light (dev cursor-light tool).
- **D3D12 is the gate**: Win11 build+run → no pipeline-creation failure, no device-removed, GI visible + plausible. This is what the fragment RC never achieved.

---

## Part B — Perf: structure_rebuild hitch + measured do_turn

### B0. Capture sim numbers FIRST
Run, **hold movement ≥20 turns**, read `[sim][perf] … sim_total= (build_map_cache= monmove= world_tick=)` from `debug.log`. Don't pre-commit to a sim target before this. (`[sim][perf] build_map_cache` (map.cpp) is **distinct** from `[lighting][perf] structure_rebuild` (SDF DT) — both ~10ms, do not conflate.)

### B1. structure_rebuild region-limit (high-confidence, shares files/region with A1)
- The DT recomputes over the **full 180×180 bubble × 16 SS ≈ 520k cells ≈ 10ms**, but only on-screen + shadow-reach is sampled.
- **Limit the DT + SS transparency replication to camera rect + ~8-tile margin** in `frame_build.cpp`/`sdf_pass.cpp` → ~180×180 → ~80×60 ≈ 4× less → ~10ms → ~2–3ms. Preserves SS=4 (user-confirmed quality). Keep CPU↔shader stride (`x*map_h+y`) consistent with new dims.
- The GI compute field (A1) uses the same camera region — single source of region bounds.
- Fallback lever: `SDF_SUPERSAMPLE` 4→2 (last resort, quality tradeoff).

### B2. Attack the dominant sim span (after B0)
- **build_map_cache** (map.cpp ~9778, already parallel-phased): finer dirty-gating of transparency/lightmap sub-caches (rebuild only on changed inputs). Correctness-sensitive; gate on measured share.
- **monmove**: already LOD + `monperf` sleep-skip — inspect sight-cache clearing / tier thresholds.
- **world_tick**: field decay over loaded submaps (`do_emits` already 10s-gated) — check iteration scope.

### Perf verification
Re-run, hold ≥20 turns, compare `[sim][perf]` + `structure_rebuild` + `[render][perf] frame_period`. Target: structure_rebuild < 3ms, max-frame down from 76ms, fps up from ~32.

---

## Critical files

| File | Change |
|---|---|
| `src/lighting/shader_compiler.{h,cpp}` | A0 add COMPUTE compile path |
| `src/lighting/gi_compute_pass.{h,cpp}` *(new)* + `data/shaders/lighting/src/gi.comp.hlsl` *(new)* | A1 compute gather → `gi_storage_` |
| `src/lighting/sprite_batcher.cpp` | A2 HLSL: drop `IndirectTex`, add `GiBuf`; renumber registers + bind slots together |
| `src/lighting/render_state.{cpp,h}`, `sdf_pass.{cpp,h}`, `emitter_collector.{cpp,h}` | A3 own/upload `gi_storage_`; remove `rc_` |
| `src/sdl_render_frame.cpp` | A3 dispatch under gate; remove RC record; B1 region bounds |
| `src/lighting/frame_build.{cpp,h}` | B1 region-limit DT + SS replication |
| `radiance_cascade_pass.{h,cpp}`, `rc.frag.hlsl`, `rc_bounce.frag.hlsl` | A3 **delete** |
| `src/game.cpp` (~1818–2180), `src/map.cpp` (~9778) | B2 only if B0 points here |

## Appendix — CPU-GI contingency (only if A0 spike fails on D3D12)
Compute single-bounce on CPU in `frame_build.cpp` (splat emitter snapshot into per-tile RGB, SDF-occluded falloff + few wall-gated diffusion iters, camera region) → upload `gi_storage_` exactly as A2/A3 consume it. Robust but non-standard and adds main-thread cost (the Part-B tension). Same consumer swap, so only A0/A1 differ.

## Gotchas (module CLAUDE.md)
- Compute resource model ≠ graphics: declare readonly/readwrite storage counts + `threadcount` in `SDL_GPUComputePipelineCreateInfo`; bind via `SDL_BindGPUComputeStorageBuffers`.
- Fragment storage buffers are `register(tN, space2)`; HLSL decls + C++ bind indices move in lockstep.
- Storage buffers = verbatim CPU array, **x-major `arr[x*H+y]`, no transpose**.
- Each new `src/lighting/*.cpp` needs `#define dbg(x) DebugLogFL((x),DC::SDL)`.
- Never leave a declared SRV slot unbound on D3D12 → command-list corruption.
- Build: `cmake --build out/build/osx-arm-slim --target cataclysm-bn-tiles`; log at `~/Library/Application Support/Cataclysm-BN/config/debug.log` (`dbg(DL::Info)`; `DL::Debug` filtered). Win11/D3D12 is the correctness gate for Part A.
