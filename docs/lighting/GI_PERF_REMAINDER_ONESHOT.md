# GI Compute + Perf — Remainder Execution Plan (one-shot spec)

**Audience:** a fresh low-context (≈90k) agent that has NOT seen this codebase. This
doc is self-contained: it tells you every file, symbol, line anchor, gotcha,
build/verify command, and commit boundary needed to finish the remainder of
`GI_COMPUTE_AND_PERF_PLAN.md` (repo root) WITHOUT exploring first. Read the
"Orientation" block, then execute tasks top-to-bottom (they are ordered by
leverage + safety). Each task is independently committable.

Source of record for design rationale = `GI_COMPUTE_AND_PERF_PLAN.md`. This doc is
the _execution_ layer. Module architecture reference = `src/lighting/CLAUDE.md`
(read it if a step confuses you; the critical bits are inlined below).

---

## ⚠️ How to execute this as an agent (read FIRST)

You can do **edit → build → reflect-gate** fully autonomously. You **cannot** judge
visual results — you have no eyes on the rendered frame. So:

- A step that says **"eyeball"**, **"play N turns"**, **"run a world"**, or
  **"verify visually"** is a **HUMAN handoff**. Do the code + build + gate, then
  **STOP and tell the user exactly what to look at** (e.g. "build is green, gate
  green — please load a world, check indoor near-window tiles gain soft fill, and
  tell me to commit"). **Never self-certify a visual result and never fabricate
  "looks good."** Commit only after the human confirms (or after build+gate alone
  for non-visual changes like a pure cleanup).
- Each task is its own commit and you may STOP after any committed task. If you only
  have budget for one thing, do **Task 0**.
- Tasks are ordered by leverage. P3 (JFA) is large — treat it as multi-session, land
  its sub-commits one at a time.

## Orientation (read once, fully)

### What is already done (do NOT redo)

- **Stage 0/1** — compute infra + GI gather ported to compute. Committed.
- **Stage 2a** — directional sky-portal march. Committed.
- **Stage 2b.1** — unified coverage occluder field + 3D-elevation sun. Committed.
- **Stage 2b.2** — directional moonlight (sun/moon param-swap). Committed `77537fef63`.
- **P1** — dead `sun_sdf` chain deleted. Committed `648ebd13ff`.
- **B1** — SDF distance-transform region-limited to the camera rect ± 8-tile margin.
  Lives in `frame_build.cpp` (`region_sdf` lambda, `MARGIN=8`). Committed.
- **P2 (sun/sky → GI bounce)** — ✅ Committed `fa905de580` (already committed before
  this plan was started; code existed in HEAD).

### Build / verify / run commands (memorize)

```bash
# Build the game (Metal, this Mac). ~minutes. Add --clean-first if a binary seems stale.
cmake --build out/build/osx-arm-slim --target cataclysm-bn-tiles

# Mac-side shader reflection gate (NO GPU needed). Run before declaring ANY shader done.
cmake --build out/build/osx-arm-slim --target shader_reflect_check \
  && out/build/osx-arm-slim/tools/shader_check/shader_reflect_check

# Run the game (human-driven; for eyeball / B0 turn-capture). "Clara City" world exists.
out/build/osx-arm-slim/src/cataclysm-bn-tiles --world "Clara City"

# Runtime log (grep this for [perf]/ERROR/reflection lines):
~/Library/Application\ Support/Cataclysm-BN/config/debug.log
#   - dbg(DL::Info) and DebugLogFL(DL::Info, DC::Main) appear.
#   - DL::Debug is FILTERED OUT — never rely on it for verification.
```

**Commit style:** terse Conventional Commits with a detailed body. Every commit in
this repo ends with this exact trailer (include it verbatim, blank line before it):

```
Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
```

**Binary-staleness trap:** a build that prints no errors can still have skipped
relinking. After building, check the binary mtime is newer than your edit, and run
the binary under `out/build/osx-arm-slim/src/`, not any stale root copy.

### Hard gotchas (these have each cost ≥1 build cycle — obey blindly)

1. **Fragment storage-buffer registers + C++ bind slots move in LOCKSTEP.** A
   mismatch = D3D12 device-removed crash. If you add/remove/reorder a
   `register(tN, space2)` storage buffer in `sprite.frag.hlsl`, you MUST update the
   `SDL_BindGPUFragmentStorageBuffers` array order in `sprite_batcher.cpp` in the
   same commit. (None of the tasks below except the deferred 3D-SDF require this —
   they keep buffer layouts fixed. Flagged where relevant.)
2. **Storage buffers hold the CPU array verbatim, x-major `arr[x*H+y]`, NO transpose.**
   Tile-res GI/sky/occ use `[(x*map_h+y)*N + c]`. SDF uses the SS-finer grid
   `sdf[x*(map_h*SDF_SS)+y]`, distances already in tile units (÷SDF_SS at build).
3. **Every new `src/lighting/*.cpp` must define its own log macro** after includes:
   `#define dbg(x) DebugLogFL((x),DC::SDL)`. `DebugLog` directly takes TWO args; use
   `dbg(DL::Error) << "...";` or `DebugLogFL(DL::Info, DC::Main) << "...";`.
4. **Compute HLSL register spaces (SDL_GPU):** readonly storage `(tN, space0)`,
   read-write storage `(uN, space1)`, uniform `(bN, space2)`. Readonly inputs MUST be
   declared `StructuredBuffer` (NOT `RWStructuredBuffer`) — Vulkan defaults readonly,
   D3D12 defaults readwrite; mismatch breaks parity.
5. **Fragment HLSL register spaces:** sampler/sampled-tex/storage-tex/storage-buf all
   in `space2`; `SDL_PushGPUFragmentUniformData` cbuffers in `space3`. (See the
   sprite.frag layout in the Appendix.)
6. **Never leave a declared SRV slot unbound on D3D12** → command-list corruption.
   Compute passes bind ALL their readonly buffers in one
   `SDL_BindGPUComputeStorageBuffers(first_slot=0, …, count)` call.
7. **`git add` discipline: NEVER `git add -A` / `git add .`.** The working tree
   carries unrelated parallel work (RmlUi runic-border: `theme.rcss`,
   `rmlui_layer.cpp`, `rmlui_render_interface.cpp`, `rmlui_proc_texture.{cpp,h}`).
   Stage explicit file lists only. Each task below names its exact files.

### The compute-pass pattern (every GPU lighting pass follows it)

A pass = a class in `src/lighting/<name>_pass.{h,cpp}` that: compiles a compute
pipeline from `data/shaders/lighting/src/<name>.comp.hlsl` via
`compile_compute_pipeline()`, owns its output `SDL_GPUBuffer`(s), exposes
`init/resize/shutdown/ready()/<x>_buffer()` + a `record(cb, inputs…, w, h, params)`.
`record` pushes the uniform (`SDL_PushGPUComputeUniformData(cb, 0, &p, sizeof p)`),
begins a compute pass with the RW output binding, binds readonly inputs, dispatches
`ceil(W/8)×ceil(H/8)` for `numthreads(8,8,1)`, ends. Copy `gi_compute_pass.{h,cpp}`
or `sky_sun_pass.{h,cpp}` as a template — they are the canonical examples.

`render_state` (`src/lighting/render_state.{h,cpp}`) owns each pass instance,
`init`s/`resize`s it (sized for the max reality-bubble tile grid), and exposes an
accessor (e.g. `rs.gi()`, `rs.sky()`, `rs.sdf()`). Dispatches are issued from
`flush_and_gather_rc` in `src/sdl_render_frame.cpp`, under the `rc_rebuild` gate.

### Key files (the lighting GPU pipeline)

| File                                                                                                            | Role                                                                                                                                                    |
| --------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/sdl_render_frame.cpp`                                                                                      | Frame orchestrator. `build_lighting`, `flush_and_gather_rc` (compute dispatches), `assemble_light_inputs` (fragment uniforms), `make_celestial_params`. |
| `src/lighting/frame_build.cpp`                                                                                  | CPU per-tile build: transparency, SDF (region-limited DT), `occ` coverage field, sky_vis, vis. Submits to collector.                                    |
| `src/lighting/sdf_pass.{h,cpp}`                                                                                 | Owns + uploads SDF/skyvis/vis/occ storage buffers; `compute_sdf_cpu` (the Euclidean DT, JFA target).                                                    |
| `src/lighting/gi_compute_pass.{h,cpp}` + `data/shaders/lighting/src/gi_field.comp.hlsl` + `gi_bounce.comp.hlsl` | GI: field gather + ray-march bounce → `gi_buf_`. P2 daylight injection lives in `gi_field.comp`.                                                        |
| `src/lighting/sky_sun_pass.{h,cpp}` + `data/shaders/lighting/src/sky_sun.comp.hlsl`                             | Sky-access + sun/moon occlusion march over `occ` → `sky_buf_`.                                                                                          |
| `data/shaders/lighting/src/sprite.frag.hlsl`                                                                    | The lit fragment shader. Consumes all storage buffers; computes `sky_contrib`/`sun_contrib`/`emitter_light`/GI.                                         |
| `src/lighting/sprite_batcher.cpp`                                                                               | `bind_lighting_resources` — binds fragment storage buffers (lockstep with sprite.frag registers).                                                       |
| `src/lighting/shader_compiler.{h,cpp}`                                                                          | `compile_compute_pipeline`, `compile_graphics_shader`, `load_lighting_shader_source`.                                                                   |
| `tools/shader_check/`                                                                                           | `shader_reflect_check` Mac-side reflection gate.                                                                                                        |

---

## Task 0 — Commit P2 (sun/sky → GI bounce). ✅ DONE `fa905de580`

**State:** already committed before this plan was started; code existed in HEAD. No action needed.

---

## Task P5 — cheap quality polish

Four independent sub-tasks; each is its own commit. Order: P5a → P5b → P5d are
low-risk; P5c (sky gradient) is the only one touching a fragment cbuffer layout —
do it last and carefully, or skip.

### P5a — soft sun penumbra (compute-side; no struct change) ✅ DONE `fa905de580`

**Committed.** See commit for details. Implemented 4-tap averaged celestial march (~±5° spread).

### P5b — retune night ambient floor against directional moonlight ⏳ HUMAN TUNING NEEDED

This is a TUNING task, not a code-structure task. `night_floor` is a `DebugParams`
knob consumed in `sprite.frag.hlsl` (cbuffer `b2, space3`, line ~101). Stage 2b.2
added directional moonlight (`sun_contrib` carries the moon at night via
`make_celestial_params`), so the flat `night_floor` is now likely too high.
**Change:** run the game at night, open F4, move the **"night floor"** slider
(`src/sdl_lighting_devui.cpp:284`) until moonlit-open vs roofed-interior reads right,
then bake the chosen value as the default. **The default lives in**
`src/lighting/sprite_batcher.h:143` — `float night_floor = 0.02f;` in the C++
`debug_params` struct (NOT the `.cpp`). Edit that literal. Changing only a default
value does NOT change `sizeof(debug_params)`, so the `static_assert` stays happy.
**Commit** (`sprite_batcher.h` only):
`tune(lighting): P5b lower night_floor for directional moonlight`.

### P5d — promote march constants to F4 knobs (optional, do after P5a)

**Goal:** expose `SKY_DIRS`, `SKY_REACH`, `SUN_STEPS`, `SUN_PENUMBRA` (sky_sun.comp)
and the GI gather constants (`gi_bounce.comp`) as live F4 knobs, mirroring
`gi_strength`.
**Mechanism:** these are `static const` in the shaders. To make them runtime:
(1) add fields to the relevant params struct — `sky_sun_params` (`sky_sun_pass.h`,
currently 32 B / 8 fields; it has unused `shadow_k`/`shadow_steps`/`ss_pad` you can
repurpose first to avoid growing it) and its `SkySunParams` cbuffer
(`sky_sun.comp.hlsl`) — **field order + size must match exactly**; (2) feed them from
`flush_and_gather_rc` (`sdl_render_frame.cpp`, the `kp.…` assignments) out of
`g_dbg_params`; (3) add the F4 sliders. The F4 panel is `src/sdl_lighting_devui.cpp`
— copy the `dbg_slider("GI strength", &g_dbg_params.gi_strength, GI_MIN, GI_MAX);`
line (≈:263) as the template, and any new knob field goes in the `debug_params`
struct in `src/lighting/sprite_batcher.h` (where `gi_strength`/`night_floor` live).
**Keep struct sizes aligned** — reuse the reserved/pad lanes before adding new ones.
This is fiddly; if context is tight, do ONLY the penumbra width + SKY_REACH (highest
visual payoff) and leave the rest const. **Gate + build + eyeball + commit** per the loop.

### P5c — sky colour as horizon→zenith gradient (most invasive; OPTIONAL)

**File:** `sprite.frag.hlsl` `sky_contrib` (line 512) +
`SunParams` cbuffer (`b1, space3`, lines 80-84) + the C++ `sun_params` struct +
`make_sun_params` (`src/lighting/sprite_batcher.cpp:83`, decl `sprite_batcher.h:177`
— it builds `sky_r/g/b`).
**Now:** `sky_contrib = float3(sky_r,sky_g,sky_b) * sky_intensity * sky_dir.rgb;`
where `sky_dir.rgb` is scalar sky-access replicated to rgb.
**Change:** introduce a second sky colour (horizon) and lerp by sky-access:
`sky_col = lerp(sky_horizon, sky_zenith, sky_dir.r)`. That needs 3 more floats
(`sky_horizon_rgb`) in `SunParams` + the matching C++ `sun_params` fields, populated
in `make_sun_params`. **`SunParams` is currently 12 floats (48 B); adding 3 → 15
floats (60 B). The C++ `sun_params` struct and the HLSL cbuffer must grow together
and stay 16-byte-alignment-safe.** No fragment STORAGE-buffer renumber (this is a
uniform cbuffer, space3) — but a uniform-layout mismatch silently corrupts all sun/sky
values. HUMAN eyeball: noon sky tints toward zenith-blue in the open, warmer near
occlusion. **Skip this if you are low on budget** — it is pure aesthetics and the
highest-risk P5 item. **Commit** (3 files: `data/shaders/lighting/src/sprite.frag.hlsl`,
`src/lighting/sprite_batcher.cpp` [make_sun_params], `src/lighting/sprite_batcher.h`
[the `sun_params` struct]): `feat(lighting): P5c horizon→zenith sky gradient`.

---

## Task P6 — correctness gaps

### P6a — weather-dim the moon (clouds currently ignore moonlight) ✅ DONE `6f109a0737`

**Committed.** Added `weather_cloud_mult()` helper and applied to both `make_celestial_params` moon branch and `assemble_light_inputs` night path.

### P6b — vehicle occluders in the coverage field (accepted-minor) ✅ DONE `8587d55b8d`

**Committed.** Added `m.veh_at(tp)` check with `obstacle_at_part()` to raise coverage height for parked vehicles.

---

## Task B0 — capture sim numbers (do before B2; no code)

**HUMAN-DRIVEN** — the probe only logs after real in-game turns, which means someone
has to play. As the agent: build, then ask the user to run the game and walk ≥20
turns; once they confirm, you grep the log.
**Goal:** the `[sim][perf]` probe is wired (`src/game.cpp`, the log line is at ~2178)
and logs `sim_total / build_map_cache / monmove / world_tick` every 20 turns, but has
produced no output yet. Capture real numbers before optimizing anything in Part B.
**Steps:** build, have the user run a world and **hold a movement key for ≥20 in-game
turns**. Then read `debug.log`:

```bash
grep "\[sim\]\[perf\]" ~/Library/Application\ Support/Cataclysm-BN/config/debug.log | tail
grep "\[lighting\]\[perf\] structure_rebuild" ~/Library/Application\ Support/Cataclysm-BN/config/debug.log | tail
grep "\[render\]\[perf\]" ~/Library/Application\ Support/Cataclysm-BN/config/debug.log | tail
```

Note: `[sim][perf] build_map_cache` (map.cpp) is DISTINCT from
`[lighting][perf] structure_rebuild` (the SDF DT in frame_build) — both ~10ms
historically; do not conflate. Record `sim_total` and its breakdown. **No commit** —
this produces the data that decides B2's target. Write the captured numbers into
`GI_COMPUTE_AND_PERF_PLAN.md` Part B (replace "produced no output yet").

---

## Task B2 — attack the dominant sim span (gated on B0)

Only act on the span B0 shows dominant. Candidates (from the plan):

- **build_map_cache** (`src/map.cpp` ~9778, already parallel-phased): finer
  dirty-gating of the transparency/lightmap sub-caches (rebuild only changed inputs).
  Correctness-sensitive — gate on measured share, add a dirty flag per sub-cache.
- **monmove**: already LOD + sleep-skip; inspect sight-cache clearing / tier thresholds.
- **world_tick**: field decay over loaded submaps (`do_emits` already 10s-gated) —
  check iteration scope.
  **Process:** pick the top span, form a hypothesis, make the smallest change, re-run
  B0's capture, compare. **Commit per change** with the before/after numbers in the body.
  Target (plan): `structure_rebuild < 3ms`, max-frame down from 76ms, fps up from ~32.

---

## Task P3 — GPU JFA SDF (the big one; biggest perf + unlocks the 3D merge)

**Why:** the CPU Euclidean DT (`compute_sdf_cpu` in `sdf_pass.cpp`) is the
`structure_rebuild` hitch — even region-limited (B1) it runs on the render thread.
Moving SDF generation to a GPU **Jump-Flood** pass makes it ~free and off the main
thread, AND unblocks the deferred thin-slab 3D SDF (see Deferred). JFA is only a
faster way to COMPUTE the same Euclidean DT — the _consumer_ layout is unchanged.

**Invariant you must preserve:** the SDF the fragment + GI read is
`sdf_storage_` (created in `sdf_pass::init`, lines ~237-249): an SS-finer grid,
`map_w*map_h*SDF_SUPERSAMPLE²` floats, x-major
`sdf[(x*SS+sx)*(runtime_h*SS) + (y*SS+sy)]`, **distances in TILE units**
(`SDF_SUPERSAMPLE = 4`, `sdf_pass.h:25`; must equal `SDF_SS=4` in
`gi_field.comp` + `sprite.frag.hlsl:138`). If JFA writes this buffer with the same
layout + units, NO consumer changes — that is the whole point. Confirm the readers
(`sdf_bilinear` in both `gi_field.comp` and `sprite.frag.hlsl`) are untouched.

This is multi-session-sized. Land it as the sub-commits below; each builds + gates +
runs green before the next. Do NOT land it as one diff.

### P3.1 — feed transparency to compute (new readonly input)

- `sdf_pass.{h,cpp}`: add a tile-res, compute-readable transparency buffer
  `trans_storage_` (floats, `0.0=opaque .. 1.0=open`), `map_w*map_h` floats, usage
  `COMPUTE_STORAGE_READ` (+ `COMPUTE_STORAGE_WRITE` not needed). Add `xfer_trans_f_`
  transfer buffer, the getter `trans_buffer()`, alloc in `init`, release in
  `shutdown` (mirror `skyvis_storage_` exactly — it is the closest precedent: tile-res
  float buffer converted from the uint8 cache).
- `sdf_pass::upload`: convert the existing `transparency` uint8 vector → float
  `[i]/255.0` into `xfer_trans_f_` and upload to `trans_storage_` (copy the
  `skyvis_storage_` upload block, ~510-531). This buffer is tile-res; the seed shader
  replicates to SS internally.
- Build + run. No behavior change yet (buffer written, unread). Commit:
  `feat(lighting): P3.1 tile-res transparency compute buffer (JFA input)`.

### P3.2 — new `gpu_sdf_pass` with JFA shaders (writes a SCRATCH buffer first)

- New `src/lighting/gpu_sdf_pass.{h,cpp}` (copy `gi_compute_pass` scaffold). It owns
  TWO ping-pong seed buffers (`seed_a_`, `seed_b_`: SS-grid, 2 floats/subcell =
  nearest-seed subcell coord, x-major stride `map_h*SS`) and writes the final
  distance into a **scratch** `jfa_sdf_` buffer (SS-grid, 1 float/subcell, TILE units)
  — NOT `sdf_storage_` yet (so you can A/B against the CPU DT this commit).
- Three shaders in `data/shaders/lighting/src/`:
  - `jfa_seed.comp.hlsl`: one thread per SS-subcell. Read `trans_storage_` at the
    parent tile (subcell → tile = `/SS`). If the tile is opaque (`trans < 0.5`), seed
    = this subcell's coord; else seed = sentinel `(-1,-1)`. Write to `seed_a_`.
    Inputs: `TransBuf (t0,space0)`; output `SeedBuf (u0,space1)`; uniform `map_w,map_h`.
  - `jfa_flood.comp.hlsl`: one thread/subcell. For step `s` (uniform), look at the 8
    neighbours at offset `±s` (and self); keep the seed giving min squared distance to
    this subcell. Read `SeedIn (t0,space0)`, write `SeedOut (u0,space1)`, uniform adds
    `step`. Run it ping-pong for `s = SS*maxdim/2 … 1` (powers of two), swapping
    in/out each dispatch.
  - `jfa_resolve.comp.hlsl`: one thread/subcell. `dist_subcells = length(subcell -
    nearest_seed)`; write `dist_subcells / SS` (tile units) to the output float buffer.
    Read `SeedBuf (t0,space0)`, write `SdfOut (u0,space1)`.
- `gpu_sdf_pass::record(cb, trans_buf, runtime_w, runtime_h)`: dispatch seed → N flood
  passes → resolve. Each dispatch is its OWN `BeginGPUComputePass`/`EndGPUComputePass`
  (SDL_GPU inserts the write→read barrier between consecutive passes automatically).
  Grid = `ceil(SW/8)×ceil(SH/8)` where `SW=runtime_w*SS`, `SH=runtime_h*SS`.
  **The seed buffers are SWAPPED, not aliased** — a flood pass reads one (readonly t0)
  and writes the OTHER (readwrite u0); you cannot bind the same buffer both ways in
  one pass. Exact schedule (CPU-side):
  ```
  seed pass:   reads trans_buf, writes seed_a_           // grid SW×SH
  in = seed_a_, out = seed_b_
  for (int step = next_pow2(max(SW,SH)) / 2; step >= 1; step /= 2) {
      flood pass: ro=in (t0), rw=out (u0), uniform.step = step;  dispatch SW×SH
      swap(in, out);                                     // out becomes next input
  }
  resolve pass: reads `in` (the last-written seed buffer), writes jfa_sdf_ (or
                sdf_storage_ in P3.3); dist = length(subcell - seed) / SS
  ```
  `next_pow2(n)` = smallest power of two ≥ n. Push `step` (+ `map_w/map_h`) in the
  flood uniform. Track which physical buffer `in`/`out` point to so `resolve` reads
  the correct final one (after the loop, `in` holds the result).
- `render_state` owns `gpu_sdf_pass gsdf_`, `init/resize` at `max_w*SS, max_h*SS`,
  accessor `gsdf()`. Dispatch in `flush_and_gather_rc` BEFORE `sky()`/`gi()` (they
  march the SDF), still under `rc_rebuild`.
- **A/B verify (LOG-based — you can check this yourself, no human eyes needed):**
  copy `gi_compute_pass::debug_log_stats` into `gpu_sdf_pass` to read back `jfa_sdf_`,
  and log the value at the player's centre subcell to `DC::Main`; alongside it log the
  CPU `sdf_storage_` value at the same subcell (the CPU `sdf` vector is still built this
  commit — P3.3 deletes it). Run, grep `debug.log` — the two numbers should agree
  within ~0.5 tile (JFA is near-exact; small error falls inside the bilinear). If they
  diverge wildly, the flood schedule or seed coords are wrong. Reflect-gate all three
  shaders (expect `ro_sb=1 rw_sb=1`, no samplers). Commit:
  `feat(lighting): P3.2 GPU JFA SDF pass (scratch buffer, A/B vs CPU DT)`.

### P3.3 — switch the consumers to JFA, delete the CPU DT

- Make `gpu_sdf_pass::resolve` write `sdf_storage_` directly (pass the sdf_pass's
  buffer in, or have render_state point both at one buffer). The SS-grid layout +
  tile units already match — no fragment/GI shader change, no renumber.
- `frame_build.cpp`: stop building the `sdf` channel on CPU — delete the `region_sdf(
  sdf, …)` call (~232) and the `compute_sdf_cpu` usage; keep building `transparency`
  (now feeds `trans_storage_`), `occ`, `sky_vis`, `vis`. The `region_sdf` lambda +
  `trans_ss` staging become dead → remove (single-source cleanup). Keep B1's camera
  rect — JFA can also be region-limited later (P3.4), but first prove full-grid.
- `sdf_pass`: `compute_sdf_cpu` is now unused → delete the function + its declaration
  in `sdf_pass.h` (~138). Also the long-dead `sdf_tex_` (R32F) and `sky_vis_tex_`
  (R8) textures (nothing samples them — see comments at sdf_pass.cpp:157-159,422-424)
  can go in this cleanup: drop the create/upload/shutdown blocks + the
  `sdf_texture()`/`sky_vis_texture()` getters. Verify no caller (`grep -rn
  "sdf_texture\(\)\|sky_vis_texture\(\)" src`).
- **Verify dual-backend (Metal now; D3D12 = the gate, see P4):** shadows look like
  before but rounder/cleaner; `structure_rebuild` time drops sharply in the
  `[lighting][perf]` log (the DT is gone). Commit:
  `perf(lighting): P3.3 SDF from GPU JFA; delete CPU DT + dead SDF/skyvis textures`.

### P3.4 — region-limit JFA (optional perf)

Mirror B1: run JFA only over the camera SS-rect ± margin, scatter into the full
buffer with a large sentinel outside. Only if the full-grid JFA shows up in `[render][perf]`.

**P3 risk notes:** the flood step schedule must start ≥ the grid half-size or the SDF
is wrong far from occluders (low risk here — the bilinear/cone trace only reach a few
tiles, but get it right). Seed sentinel handling: a subcell with no seed yet must
compare as +∞ distance so a real seed always wins. Keep `numthreads(8,8,1)`.

---

## Task P4 — verify the D3D12 compute barrier (Win11) — ✅ DONE 2026-06-18

**Resolved.** The current build runs perfect on Win11/D3D12: GI + sky/sun compute
passes render correctly, no pipeline-create failure, no device-removed. So SDL_GPU's
auto-inserted same-command-buffer compute-write→graphics-read (and compute→compute)
barriers on `gi_buf_` / `sky_buf_` **work on D3D12** — the gate the fragment RC never
passed. Structural Mac/Win parity confirmed for everything through Stage 2 + P2.
**Carry-forward:** when P3 (JFA) lands new compute buffers, re-confirm the SAME way on
the next Win11 pass (seed/flood ping-pong adds compute→compute aliasing barriers that
have not yet been exercised). No code owed now.

---

## Deferred — full 3D-SDF unification (rides P3/JFA, own phase)

One thin-slab 3D SDF (current z + a few levels above, region-limited) sphere-marched
in 3D for **both** emitters and the sun = the genuinely-merged indoor/outdoor
structure (replaces the current split: emitter SDF wall-only + sun coverage-march).
Belongs to the GPU-JFA phase because a 3D _CPU_ DT would undo B1. Do NOT attempt
before P3.3 lands. Tracked here only as a pointer; it is a separate plan.

---

## Appendix

### sprite.frag.hlsl fragment resource layout (space2 storage; space3 uniforms)

```
t0 Atlas (sampled)          s0 AtlasSmp (sampler)
t1 ShadowMask  (storage TEXTURE — the SOLE storage texture)
t2 Emitters    (storage buf slot 0)   StructuredBuffer<GpuEmitter>
t3 SdfBuf      (slot 1)   StructuredBuffer<float>  SS grid, x-major x*(map_h*SDF_SS)+y, TILE units
t4 SkyVisBuf   (slot 2)   StructuredBuffer<float>  tile-res, raw 0/1 open-sky
t5 VisBuf      (slot 3)   StructuredBuffer<float>  SS grid, per-tile visibility
t6 GiBuf       (slot 4)   StructuredBuffer<float>  tile-res, [(x*map_h+y)*4+c] rgb+pad
t7 SkyBuf      (slot 5)   StructuredBuffer<float>  tile-res, [(x*map_h+y)*4+c] rgb=sky-access a=celestial-occ
b0(space3) LightParams   b1 SunParams(48 B,12 floats)   b2 DebugParams
```

`DebugParams`/`debug_params` is **136 B** — guarded by
`static_assert(sizeof(debug_params)==136, …)` at `src/lighting/sprite_batcher.cpp:62`.
If P5d adds a knob field to the `debug_params` struct (`sprite_batcher.h`), you MUST
update both that `static_assert` and the matching `DebugParams` cbuffer in
`sprite.frag.hlsl` (b2, space3) — and any new sky_sun knob lives in `sky_sun_params`
(`sky_sun_pass.h`), not here. Storage-buffer slots are bound in ONE
`SDL_BindGPUFragmentStorageBuffers(first_slot=0, …, 6)` in
`sprite_batcher.cpp::bind_lighting_resources` (the call is at `sprite_batcher.cpp:847`)
— bind-array order MUST match t2..t7.

### Compute shader I/O (current)

```
gi_field.comp   ro: Emitters(t0) SdfBuf(t1) SkyBuf(t2)   rw: FieldBuf(u0)   ub: GiParams(b0)
gi_bounce.comp  ro: FieldBuf(t0) SdfBuf(t1)              rw: GiBuf(u0)      ub: GiParams(b0)
sky_sun.comp    ro: OccBuf(t0)                           rw: SkyBuf(u0)     ub: SkySunParams(b0)
```

`OccBuf` = tile-res, 2 floats/tile `occ[(x*map_h+y)*2+c]`: c0 = occluder height
(coverage/100, tiles), c1 = roof bit. Built in `frame_build.cpp` ~246-272.

### Params structs (keep C++ struct == HLSL cbuffer, field order + size)

- `gi_params` (`gi_compute_pass.h`): emitter_count,map_w,map_h,current_z,shadow_k,
  shadow_steps,pad0,pad1, sun_rgb+sun_intensity, sky_rgb+sky_intensity.
- `sky_sun_params` (`sky_sun_pass.h`, 32 B): map_w,map_h,sun_dir_x,sun_dir_y,
  sun_sin_elev,shadow_k,shadow_steps,ss_pad. (`shadow_k`/`shadow_steps`/`ss_pad`
  currently reserved — repurpose these before growing the struct.)

### Debug views (F4 panel, `debug_mode`)

GI raw = mode 12; sky-access/sun-occ views exist (mode 13/14 per plan). Use them to
isolate a term when eyeballing.

### make_celestial_params (sdl_render_frame.cpp ~265)

Day = sun (`make_sun_params(hour)`); night = moon = sun params with 12h-shifted arc,
cold blue-white colour (0.55,0.65,0.95), intensity `illum*MOON_MAX(0.18)` where
`illum` from `get_moon_phase(when)`. Picks the brighter body for the directional term.
This is the single source of the celestial light feeding BOTH the sky/sun compute
pass and the GI daylight injection.
