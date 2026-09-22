# GPU daylight rendering bug: noon/fair-weather scenes render near-black

## Symptom

User-reported live-gameplay bug, unrelated to the vehicle-continuous-movement program: on the
`Bairdford` save, standing on an open outdoor road at `DAY 16 Spring 12:46PM`, weather `Fair`,
HUD `LUX :: bright` — the actual rendered frame is almost entirely black. Only a small dim pool
around the player (from vehicle headlights, a point-emitter light source) is visible; the broad
outdoor daylight that should illuminate the whole scene never appears. Confirmed reproducible
across three separate live sessions loading the same save.

**Critical property: the mechanical/vision layer is correct.** `LUX :: bright` in the HUD proves
`game::natural_light_level()` / `sunlight(calendar::turn)` compute the right value — sight range,
NPC AI, and all CPU gameplay mechanics that depend on light level are unaffected. This is a
**rendering-only** bug in the GPU compute-lightmap pipeline.

## Investigation log (ordered, each step falsified or confirmed by live data)

1. **`[calendar]` unit tests** (`./cata_test-tiles "[calendar]"`) — 21/21 assertions pass. Ruled
   out: `sunlight()`/`current_daylight_level()` CPU math.
2. **`k_sun` 24h LUT** (`src/lighting/sprite_batcher.cpp:91-102`) — hour 12 row is the LUT's
   brightness peak (`si=1.25`, `elev=0.87`). Ruled out: the static day/night curve itself.
3. **`CBN_FORCE_SUN_HOUR` env override** (`sdl_render_frame.cpp:437-457`) — grepped every session's
   `debug.log`; never set. Ruled out: a stuck debug pin.
4. **The `vision_*` "accepted baseline" test failures** (`vision_wall_obstructs_light` etc.) —
   initially misread as related (both "dark", both "noon"). **Corrected**: `plans/merge-main-into-improvements.md`
   already established these are a CPU-shadowcasting-fallback-only artifact of the *test binary*
   having no GPU device, byte-identical against vanilla upstream `main`. The live game uses the
   real Metal GPU compute-lightmap pipeline, a completely different code path. Not the cause of
   this bug — do not re-chase this lead.
5. **Added periodic `[segdiag]` logging** (`sprite_batcher.cpp:743-758`, converted the existing
   one-shot `s_diag_first_lit` latch to fire every ~300 lit segments; gated on
   `CBN_DIAG_SEG_LIGHTING=1`). Live capture on the actual noon/Fair frame, well after world load
   settled:
   ```
   sun_intensity=1.22106 sky_intensity=1.03071 sdf_map_w=180 emitter_count=2 ambient=0.05
   ```
   **`sun_intensity`/`sky_intensity` reach the GPU correctly bright.** The upstream celestial-hour
   computation, weather multiplier, and cbuffer upload are all fine. `ambient=0.05` is a red
   herring — `sprite.frag.hlsl:996-999` documents that in-game the shader ignores the CPU
   `ambient` uniform entirely and computes its own day/night floor via
   `lerp(night_floor, day_floor, sun_intensity)`.
6. **Read `sprite.frag.hlsl`'s sun-term gate** (lines 920-960):
   ```hlsl
   const bool sun_applies = sun_intensity > 0.001 && sun_sky_vis > 0.05 && sdf_map_w > 0u;
   ...
   const float sun_shadow = sun_occl;   // SkyBuf.a, written by sky_sun.comp
   sun_contrib = float3(sun_r,sun_g,sun_b) * sun_intensity * sun_lambert * sun_shadow * sun_sky_vis * mask_term;
   ```
   All three gate inputs (`sun_intensity`, `sdf_map_w`) confirmed fine in step 5. The remaining
   unverified factors are `sun_sky_vis` (read from `SkyVisBuf`, CPU-built) and `sun_occl` (read
   from `SkyBuf.a`, GPU-computed by `sky_sun.comp`).
7. **Live visual confirmation via `debug_mode 14`** ("sun occ" view, `sprite.frag.hlsl:1303-1311`)
   using the scripted-verification hook (`/tmp/cata_dbg_mode`, consumed by
   `sdl_input.cpp:486-502`) — the entire open road rendered as uniform dark blobs, not bright
   white. Confirms `sun_occ` reads near-zero (shadowed) over open, unobstructed ground.
8. **Hypothesis A: `OccBuf`'s roof bit is a false positive.** `frame_build.cpp:217` sets the roof
   bit from `above->floor_cache[idx]` (CPU `level_cache::floor_cache` at z+1); `build_floor_cache`
   (`map_cache.cpp:653-701`) bulk-initializes the whole level to `true` and only clears tiles whose
   submap actually got visited/generated — tiles under an ungenerated z+1 submap (the common case
   above open outdoor ground) could retain a stale default `true`, misread by
   `sky_sun.comp.hlsl:143` (`if (roof_at(...) > 0.5) return 0.0;`) as "always roofed, everywhere".
   **Added `[roofdiag]` instrumentation** (`frame_build.cpp:222-235`, same env gate) to count how
   many of the 32400 tiles have the roof bit set. **Falsified**: `roofed=0` for all 32400 tiles,
   every sample, across the whole session.
9. **Hypothesis B: `SkyVisBuf`/`outside_cache` never populates.** The pre-existing `[fbdiag]` probe
   (`frame_build.cpp:263-291`, written by an earlier session for a related-sounding symptom) was
   present but silent — its `dbg(x)` macro at the top of the file (`#define dbg(x)
   DebugLogFL((x), DC::SDL)`) uses debug category `DC::SDL`, evidently filtered out by default
   logging config while `DC::Main` (used by `[segdiag]`) is not. **Switched the category to
   `DC::Main`** to unblock it. Live capture: at `n=1,2` (immediately post-load) `outside_true=0`
   (transient, before the first cache build completes); by `n=220` (~19s later, steady state)
   `outside_true=32195` / `sky_vis_nonzero=32195` out of 32400 — **99.4% of the map correctly reads
   as open sky.** **Falsified as the root cause of the persistent bug**: the scene was still
   rendering black well after this point (checked at the same wall-clock time as the `n=220`
   sample), even though the CPU-side sky-visibility input feeding `sun_sky_vis` is correct.

## RESOLVED: root cause found (follow-up session, `gpu-daylight-black-scene-bisect-plan.md`)

The GPU-SDF/SkyBuf hypothesis below (section "Where this leaves it") is **falsified**. The bug
is not in the GPU lighting pipeline at all. It is a CPU-side `visibility_cache` population defect
that starves `cata_tiles.cpp`'s per-tile renderer of `VIS_CLEAR` classifications, which forces the
`memory`-mode render path (`sprite_light_mode::memory`, desaturated remembered-terrain art) for
almost the entire viewport instead of the live GPU-lit path — independent of whether the GPU sun/
sky/GI passes are healthy.

### Method

Used debug view 16 (`tools/light_mode_check.py`, categorical: red=`unlit`, green=`gpu_lit`,
blue=`memory`) — the one measurement the original plan correctly identified as bypassing the
`mode_memory || mode_gpu_lit` debug-view gate that made every earlier `debug_mode 14` observation
ambiguous. Result on the `Bairdford` open-road scene: **`memory 85.04%`, `gpu_lit 0.69%`**,
reproduced identically across two independent fresh sessions. This alone proves the scene isn't
lit-but-dark (which would show `gpu_lit` green with low luma); it's rendering from map memory.

Traced via new temporary instrumentation (added, measured, then fully reverted — `git diff` for
`src/cata_tiles.cpp` is empty again):

1. `classify_tile_light` (`src/tile_light_mode.h`) itself is correct and reads its `ll` input
   faithfully — the bug is upstream, in what `ll` gets set to.
2. `cata_tiles::draw_terrain` (`src/cata_tiles_draw_layers.cpp:147-285`) is called exactly once
   per on-screen column (1334 for a 46×29 viewport, confirmed by an unconditional call counter),
   and takes ONE of two branches: the live path (`188-247`, draws from the real tile + memorizes
   it) when `invisible[0]` is false, or a memory-fallback path (`274-282`) that hardcodes
   `lit_level::MEMORIZED` when `invisible[0]` is true.
3. `invisible[0]` is computed once per column in `cata_tiles.cpp`'s main draw loop
   (`~1125-1249`). Two early advisories misread this branch structure in opposite directions;
   direct reading of `would_apply_vision_effects`/`apply_vision_effects`
   (`cata_tiles.cpp:2848-2850`, `2904-2906`) resolved it: `apply_vision_effects(pos, visibility)`
   returns **true** for anything OTHER than `VIS_CLEAR` (dark/hidden/boomered — needs a special
   overlay) and **false** for `VIS_CLEAR` (normal, take the live/queue-with-`ll` path directly,
   `~1241-1244`). So the memory-fallback branch is only reached for columns whose
   `visibility_type` is **not** `VIS_CLEAR`.
4. `visibility_type` comes from `map::get_visibility(ll, cache)` (`src/map.cpp:972-1003`):
   `VIS_CLEAR` only for `ll` ∈ {`LOW`, `LIT`, `BRIGHT`}; `ll` ∈ {`BLANK`, `MEMORIZED`} → `VIS_HIDDEN`.
5. `ll` itself is read at `cata_tiles.cpp:1138`:
   `ll = ch.inbounds({x,y}) ? ch.visibility_cache[ch.idx(x,y)] : lit_level::BLANK;`
6. **Direct measurement of `ll`'s distribution** across the 1334 on-screen columns on the very
   first evaluated z-level (`z == center.z()`, i.e. the player's own floor, open road, noon,
   `Fair` weather): **`bright=9, blank=1325`** — everything else (`dark`, `low`, `lit`,
   `bright_only`, `memorized`) zero. 99.3% of the visible screen's `visibility_cache` entries are
   the **never-computed default** `lit_level::BLANK`, not a genuine "dark" reading.
7. Ruled out an out-of-bounds `ch.inbounds()` read (the tempting cheap explanation): the level
   cache is sized `cache_x=180, cache_y=180` (`level_cache::cache_x/cache_y`, runtime-sized from
   the `REALITY_BUBBLE_SIZE` option — matches the GPU `sdf_map_w=180` seen throughout the earlier
   session), while the screen's queried range is only `max_vis_x=168` — comfortably in bounds.
   So the array read is in-range; the **value stored there is genuinely `BLANK`**.

### Conclusion

`level_cache::visibility_cache` — populated by `map_cache.cpp`'s `apparent_light_at()` sweep
(`map_cache.cpp:447-495`) — is not being computed for the vast majority of the on-screen area even
though the player's reported natural light level is `LUX :: bright` (which should give a sight/
light radius far larger than the ~23-tile half-width the 46-wide viewport needs). This is a
**CPU-side visibility-cache / FOV defect** (the `apparent_light_at` sweep's bounds or radius are
wrong, or it's silently not running for most of the area this session), sitting entirely upstream
of and unrelated to the GPU lighting pipeline (`SkyBuf`, `sky_sun.comp`, SDF/JFA, GI). It explains
every downstream symptom precisely: `sun_intensity`/`sky_intensity`/`outside_cache`/roof-bit are
all correct (confirmed in the original investigation) because those are different buffers; the
screen looks black because 99%+ of it is drawn from desaturated map memory, not because the sun/
sky shader terms are zero.

### Scope decision

Per `gpu-daylight-black-scene-bisect-plan.md` Stage 0b's own decision tree: a `memorized`
misclassification rooted in the visibility cache is explicitly **"a visibility-cache (FOV)
problem, not a lighting one... report it; do not fix it under this plan."** `apparent_light_at`/
`build_seen_cache`'s sweep bounds are a distinct, higher-blast-radius subsystem (touches
shadowcasting, monster detection ranges, NPC AI, and possibly save compatibility) that deserves
its own dedicated investigation and plan, not a rushed fix folded into a rendering-pipeline-hygiene
plan. **Filed as a new, separate, high-priority bug**: "CPU `visibility_cache` reads `BLANK` for
~99% of the on-screen viewport under `LUX :: bright`, forcing near-universal `memory`-mode
rendering" — the actual cause of the reported black-scene symptom.

Stages 1-7 of the bisect plan (fail-loud sentinel, declared pass gating, single-source occlusion,
binding-layout guard, gameplay/render conformance assertion, docs correction, Radiance Cascades)
remain independently valuable pipeline hygiene and proceed as planned — they hedge exactly this
failure mode (a non-lighting bug masquerading as a lighting one) for the *next* time something
renders black, even though they are not what fixed this particular incident.

---

## Original hypothesis space (superseded by the above; kept for the historical record)

### Where this leaves it (unresolved — root cause not yet found)

Both individually-plausible CPU-side inputs to the shader's sun-term gate
(`OccBuf` roof bit, `SkyVisBuf`/outside_cache) are confirmed correct once the map cache settles.
Yet the on-screen result is still black at that same point in time. The remaining suspect is
**the GPU-only side of the pipeline**, upstream of what `[roofdiag]`/`[fbdiag]` can see:

1. **The SDF/JFA compute chain** (`occ_base.comp` → `occ_raster.comp` → `jfa_seed`/`flood`/`resolve`,
   see `src/lighting/gpu_sdf_pass.cpp`) producing a corrupted or all-near-occluder distance field,
   which would make `sky_sun.comp.hlsl`'s `celestial_occ_dir` sphere-trace (lines 137-198) read
   "inside an occluder" (`sd < 0.05`) even over open ground — independent of the CPU `OccBuf`
   height/roof inputs being correct.
2. **A buffer-timing/staleness bug**: the CPU writes correct `occ`/`sky_vis` arrays for the current
   frame, but the GPU dispatch samples a stale or desynced buffer (double-buffering issue, or a
   dimension/stride mismatch between what was built this frame and what the compute pass actually
   binds).

**Superseded**: this whole branch was never reached — the fault was found and confirmed upstream
of it (CPU `visibility_cache`, see the RESOLVED section above) before any GPU-side probe was
needed.

## Instrumentation left in the tree (uncommitted, env-gated, zero cost when unset)

- `src/lighting/sprite_batcher.cpp:743-758` — `[segdiag]` converted from one-shot to periodic
  (every ~300 lit segments), same `CBN_DIAG_SEG_LIGHTING` gate.
- `src/lighting/frame_build.cpp:222-235` — new `[roofdiag]` probe (roof-bit true-count).
- `src/lighting/frame_build.cpp:283-290` — existing `[fbdiag]` probe, category switched from
  filtered `DC::SDL` to working `DC::Main`.
- All *additional* temporary probes from the follow-up session (`[lmdiag]`, `[lmcat]`, `[dfsdiag]`,
  `[mmbounds]`, `[mmcount]`, `[zldiag]`, `[zlcount]`, `[llcdiag]`) were reverted in full —
  `git diff src/cata_tiles.cpp` is empty. Only the two files below remain modified.

`git diff --stat` shows exactly `src/lighting/frame_build.cpp` and
`src/lighting/sprite_batcher.cpp`. Safe to keep for a follow-up session (env-gated, zero cost when
`CBN_DIAG_SEG_LIGHTING` is unset) or revert if a clean tree is preferred before other work resumes.

## Explicitly ruled out — do not re-investigate

- CPU `sunlight()`/`current_daylight_level()` math (calendar tests pass).
- The 24h sun LUT's noon values (`sprite_batcher.cpp` `k_sun` table).
- A stuck `CBN_FORCE_SUN_HOUR` debug pin.
- The `vision_*` CPU-shadowcasting test-suite failures — a pre-existing, documented, GPU-absent
  test-harness artifact, not connected to this live-game GPU bug.
- The hardcoded `in.ambient = 0.05f;` (`sdl_render_frame.cpp:599`) — a UI/main-menu-only fallback
  the in-game shader path doesn't consume (confirmed by the shader's own comment).
- `OccBuf`'s roof bit being a stale-default false positive (`[roofdiag]`: 0/32400 roofed).
- `SkyVisBuf`/CPU `outside_cache` never populating (`[fbdiag]`: 32195/32400 correctly open at
  steady state, yet the bug persisted at that same point in time).
- **The entire GPU lighting pipeline** (`SkyBuf`, `sky_sun.comp`, SDF/JFA, GI, binding layout) —
  never implicated; the fault is 100% upstream in the CPU `visibility_cache`. Do not re-open this
  as a lighting-shader bug.
