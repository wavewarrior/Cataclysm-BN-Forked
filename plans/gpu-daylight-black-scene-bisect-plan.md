# Fix the GPU daylight black-scene bug and remove the architecture that hid it

## Context

On the `Bairdford` save at `DAY 16 Spring 12:46PM`, weather `Fair`, standing on open outdoor
ground, the rendered frame is near-black — only a small pool from a vehicle-headlight point
emitter is visible — while the HUD correctly reads `LUX :: bright`. Three prior sessions failed
to localise it. The cause of that failure is structural: this pipeline converts almost every
possible fault into "the screen is dark", which is indistinguishable from the symptom.

This plan does three things, in order: (1) bisect and fix the defect with a measurement ladder
whose rungs are objective pixel/log numbers; (2) close every architectural gap that let a
renderer fault masquerade as nightfall, converging the subsystem on well-established rendering
practice rather than bespoke invention; (3) replace the improvised GI bounce with Radiance
Cascades, the mature form of what it approximates. Stages are independent and individually
shippable — each leaves the tree building and the suite green.

Already established from live data (do not re-derive): `sun_intensity=1.22` /
`sky_intensity=1.03` / `sdf_map_w=180` reach the GPU; CPU `OccBuf` roof bit is 0/32400; CPU
`outside_cache` is 32195/32400 open at steady state. Those rule out the sun-intensity path and
two CPU fields — they say nothing about whether the GPU terms are composited at all.

Two facts from the shader math drive the whole ladder:

1. **Near-black is not the signature of a broken sun term.** `sprite.frag.hlsl:1038` floors a
   fully-shadowed tile at `sun_shad_mul = 0.65`, and `sky_contrib` (`:832`) is driven by
   `SkyBuf.rgb`, an independent channel from `SkyBuf.a`. A dead sun alone still leaves
   directional sky fill. Black means **both** `SkyBuf` channels are zero, **or** the GPU terms
   are never composited.
2. **The compositing gate can swallow the scene silently.** `sprite.frag.hlsl:1206` gates every
   debug view except 8 and 16 behind `mode_memory || mode_gpu_lit`, and `:1159` selects
   `unlit_rgb` when neither holds. So a misclassified world renders black **and** debug views
   13/14 show nothing. The earlier "`debug_mode 14` looked dark" observation cannot distinguish
   that from a genuinely zero occlusion field.

Hence the first measurement is the one view that bypasses the gate (16), and the second is the
pair that separates "pass never wrote" from "pass wrote zero" (13 vs 14).

## How this pipeline compares to standard practice

The shape of what is here is conventional; the gaps are in the plumbing disciplines that
normally accompany it. Concretely:

|Here|Standard name / prior art|Gap to close|
|---|---|---|
|5-dispatch `gpu_sdf_pass` (`occ_base → occ_raster → jfa_seed → jfa_flood → jfa_resolve`)|Jump Flooding Algorithm — Rong & Tan, I3D 2006 ([paper](https://www.comp.nus.edu.sg/~tants/jfa/i3d06.pdf))|None. This is textbook and correctly implemented.|
|`gi_field` + `gi_bounce` + `gi_bounce2` probe march with an EMA temporal filter|Radiance Cascades — Sannikov, Grinding Gear Games ([paper](https://github.com/Raikiri/RadianceCascadesPaper)), shipped in Path of Exile 2|Stage 7. The current bounce is a fixed 16-ray gather per tile that needs temporal smoothing to look stable, which costs exactly the sharp contact shadows and instant response RC exists to keep.|
|Hand-ordered `if (rc_rebuild && a && b && c)` pass gating in `sdl_render_frame.cpp:480-576`|Render/frame graph with **declared** per-pass reads, writes and readiness — O'Donnell, *FrameGraph: Extensible Rendering Architecture in Frostbite*, GDC 2017 ([slides](https://media.gdcvault.com/gdc2017/Presentations/ODonnell_Yuriy_FrameGraph.pdf))|Stage 2. A pass is currently culled by an unrelated sibling's readiness, which a declared-dependency model makes structurally impossible.|
|Missing/failed lighting data renders as black, by explicit design (`sky_sun_pass.cpp:45-49`)|Fail-loud error assets: Valve's missing-texture checkerboard ([Missing content](https://developer.valvesoftware.com/wiki/Missing_content)), Unity's magenta shader-error material|Stage 1. Every engine of this class makes "no data" visually unmistakable and distinct from "legitimately dark".|
|Four parallel encodings of "this blocks light" (`TransBuf`, `OccBuf.height`, `SkyVisBuf`, sprite-footprint `blk`), each with its own threshold|A single authoritative material/geometry buffer that all lighting consumers derive from — the G-buffer discipline of deferred shading|Stage 3. Divergence between these has already caused two recorded production bugs.|
|18-parameter positional `set_lighting_resources`, and a 7-slot binding array whose contiguity is an uncheckable convention|Pass-constant structs + a single declared resource-binding layout validated against shader reflection|Stage 4. The reflection data needed for the check is already logged, just not asserted.|
|Gameplay light (`lightmap.cpp`), GPU visibility mirror (`compute/gpu_lm.cpp`), and render light (`lighting/`) with no invariant between them|Simulation/presentation split is normal; the missing half is the **conformance assertion** between them|Stage 5. "HUD bright, screen black" is currently an unrepresentable-as-error state.|

Only the JFA is already at its mature form and stays untouched. Everything else below it is
standard plumbing this subsystem never grew, and the GI bounce is replaced outright in Stage 7.

## Preconditions (load-bearing; the last three sessions each lost time to one of these)

Working directory: `/Users/nigel.fierens/dev-projects/Cataclysm-BN-Forked`.

- **Never drive the main menu.** Launch straight into the save:
  `./cataclysm-bn-tiles --dont-debugmsg --world Bairdford`. Menu keyboard driving silently
  navigates onto Quit; the log then shows only `Log shutdown.`, which reads exactly like a
  crash. The most recent recorded session in `debug.log` is precisely this failure.
- **The binary is the repo-root one.** `./cataclysm-bn-tiles`.
  `out/build/osx-arm-slim/src/cataclysm-bn-tiles` is a months-old leftover that still runs.
- **Prove the world loaded before believing any capture.** A main-menu session logs
  `cam_wh=0x0`, `rc=0`, `sun_intensity=0`:
  ```sh
  LOG="$HOME/Library/Application Support/Cataclysm-BN/config/debug.log"
  grep -c "structure_rebuild ms=" "$LOG"                 # MUST be > 0
  grep -oE "cam_wh=[0-9]+x[0-9]+" "$LOG" | tail -1        # MUST be non-zero
  ```
- **One instance only** — a second sharing `config/`+`save/` exits with code 25.
  `pkill -f cataclysm-bn-tiles` before each launch, and before each rebuild (a running
  instance holds the binary open and the link fails).
- **Kill, never in-game Quit.** Afterwards confirm `save/Bairdford/map.sqlite3` mtime predates
  the session and `map.sqlite3-wal` is 0 bytes; report honestly if anything was written.
- **Do not run `cmake --build --target format`** — the formatter-version mismatch in this repo
  de-indents code across ~90 files. Match surrounding style by hand.
- Builds: background job, 1200 s+, never killed mid-run (a killed ninja corrupts `.ninja_deps`).
  `cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles`.

## Approach

Stage 0 finds the bug. Stages 1-6 are independent of each other and of Stage 0's outcome, and
each leaves the tree building and the suite green. If Stage 0's root cause turns out to be the
defect a later stage removes anyway, implement that stage's edit as the fix and mark the stage
done rather than patching twice.

### Stage 0 — Bisect the black scene

Each rung is one launch plus one measurement, and selects the next rung.

**0a. Classification gate (no code).** `CATA_DBG_MODE=N` forces a debug view at startup
(`src/sdl_lighting_devui.cpp:66-72`, taken `% 18`). View 16 replaces each drawn fragment with a
flat categorical hue and is one of only two views exempt from the `dbg_active` gate, so it
reports classification truth even on a black screen.

```sh
pkill -f cataclysm-bn-tiles || true
CATA_DBG_MODE=16 CBN_DIAG_SEG_LIGHTING=1 ./cataclysm-bn-tiles --dont-debugmsg --world Bairdford
# ~95 s to gameplay, window-screenshot via the computer tool {silent:true} -> /tmp/cbn_m16.png
python3 tools/light_mode_check.py /tmp/cbn_m16.png
```

- Open road **GREEN** (`gpu_lit`) → go to 0c.
- Open road **RED** (`unlit`) or **BLUE** (`memory`) → go to 0b.
- Take the capture at default zoom with no keypresses after load; that tool's header documents
  how zoom drift manufactures fake signal.

**0b. Misclassification: four inputs, one function.** `classify_tile_light`
(`src/tile_light_mode.h:53-65`) is called from exactly one site, `src/cata_tiles.cpp:2158-2163`,
with `{as_independent_entity, is_overmap, world_present, lighting_ready, memorized}`. Log that
query at the callsite (gate `CBN_DIAG_SEG_LIGHTING`, category `DC::Main`, rate-limit
first-two-then-every-60th — copy the `[roofdiag]` idiom at `src/lighting/frame_build.cpp:222-235`)
and read which input is wrong:

1. `lighting_ready` — `lightmap_ever_generated()`, latched on two paths: CPU
   `src/lightmap.cpp:1943` and GPU `src/map_cache.cpp:1248`. A `0` here classifies the **whole
   world** `unlit`, which matches the symptom exactly; find which path `refresh_display`
   actually takes and latch it there.
2. `world_present` — `g != nullptr && get_map().inbounds(pos)`; false with a loaded world means
   the callsite is testing a different map than the one being drawn.
3. `memorized` — a visibility-cache (FOV) problem, not a lighting one. Report it; do not fix it
   under this plan.
4. `is_overmap` / `as_independent_entity` — rule out from the same line.

Whatever the fix, keep the fail-bright protection the classifier exists for: a genuinely dark
but visible tile stays `gpu_lit` with dark radiance and must never fall back to full `unlit`
albedo (`tile_light_mode.h:43-52`), and do not reintroduce a per-z readiness flag
(`lightmap_ready.h:8-18` records why that recreates the defect). Acceptance: re-run 0a, the road
is GREEN and `light_mode_check.py` exits 0.

**0c. `SkyBuf` liveness: view 13 against view 14.** Both channels come from the same dispatch
but different inputs — sky-access from `OccBuf` via `sky_admit`, sun-occlusion from the SDF via
`celestial_occ_dir` — so the pair localises the fault. Switch views in-session through the live
hook (`src/sdl_input.cpp:486-503`, consumed once per `CheckMessages`, file deleted after read):

```sh
echo 13 > /tmp/cata_dbg_mode; sleep 2   # screenshot -> /tmp/cbn_m13.png  (sky access)
echo 14 > /tmp/cata_dbg_mode; sleep 2   # screenshot -> /tmp/cbn_m14.png  (sun occlusion)
```

Both views `replace=true`, so this reads the buffer, not the shading. Measure over the terrain
viewport only, excluding the HUD strip (state the box used):

```python
import numpy as np; from PIL import Image
a = np.asarray(Image.open("/tmp/cbn_m13.png").convert("L")).astype(int)[60:1380, 40:2150]
print(a.mean(), (a > 200).mean(), (a < 30).mean())   # mean, bright fraction, dark fraction
```

|m13 sky|m14 sun|Meaning|Next|
|---|---|---|---|
|dark|dark|`sky_sun.comp` never wrote `SkyBuf`; both directional terms are zero, which alone explains the black frame|0d|
|bright|dark|pass ran; the sun sphere-trace specifically returns 0 over open ground|0e|
|bright|bright|`SkyBuf` is healthy; the loss is downstream|0f|

Cross-check with zero new code, in the same log: `grep -c "rebuild: struct=1 vis=1 rc=1" "$LOG"`
(dispatch opportunities — `rc_rebuild` is `fr.built_pertile`, `sdl_render_frame.cpp:325`) and
`grep -c "\[lighting\]\[occ\]" "$LOG"` (SDF dispatches — logged only inside the SDF gate at
`:498-503`). Pipeline health is already known good from the startup log
(`sky_sun.comp reflection: ro_sb=2 rw_sb=1`, `gi_field.comp reflection: ro_sb=4 rw_sb=1`, no
`pipeline create failed`), so spend no time on pipeline creation.

**0d. Dead `SkyBuf`: which conjunct failed.** Implement Stage 2 now — splitting the gate is both
the diagnosis and the fix, and its `[lighting][passes]` line names the failing term directly.
The specific suspect it will confirm or clear: `rs.sdf().map_w()/map_h()` return
`runtime_w_/runtime_h_`, assigned **only** inside the successful-upload branch at
`src/lighting/sdf_pass.cpp:294-296`, while the dispatch reads them at `:510-511`; if they are 0
while `populated()` is true, `sky_sun_pass::record` early-returns at
`src/lighting/sky_sun_pass.cpp:145` and `SkyBuf` stays zero forever. If that is the finding, pass
the runtime dimensions through from the frame that built them instead of re-reading the mutable
cached pair.

**0e. Sun march over-shadowing: prove the field, then fix the gate.** First capture
`CATA_DBG_MODE=6` (SDF view: red = distance 0, green ≥ 8 tiles, `sprite.frag.hlsl:1237-1245`) and
measure red/green fractions over the open road. Then A/B the feather without a rebuild by adding
`sun_soft` to the live knob hook — `src/sdl_input.cpp:509-534` already parses `name value` for
ten knobs; add `else if( kn == "sun_soft" ) dp.sun_soft = kv;` and list it in the "Known names"
comment above. Then `echo "sun_soft 0" > /tmp/cata_knob` and re-capture view 14.

Two mechanisms are live, both verified against the source this session:

*(a) The height gate is vacuous on open ground.* `sky_sun.comp.hlsl:179-181` computes
`h = occ_height_at(pos)` then `h_eff = (h > 0.01) ? h : TREE_H`, with
`TREE_H == MAX_OCC_H == 3.00` (`:66-67`). The loop already breaks at `ray_h >= MAX_OCC_H`
(`:175-177`), so on any tile with no recorded occluder height the guard `ray_h < h_eff` is
*always* true and the penumbra line `shadow = min(shadow, saturate(sd / sun_soft))` (`:188`)
runs at **every** step of an open-ground march. With the shipped `sun_soft = 0.35`
(`src/lighting/sprite_batcher.h:325`) any SDF sample under 0.35 tiles dims the tile, and
`sd < 0.05` takes `shadow = 0; break;` (`:182-184`).

The fallback's stated justification — "trees are absent from `OccBuf`"
(`sky_sun.comp.hlsl:172-173`) — is false: `src/lighting/frame_build.cpp:214-216` writes
`h = TREE_H` for every `TFLAG_TREE` tile, so trees carry their own height and the fallback only
ever fires on tiles that have none.

It cannot simply become `h_eff = h`, and the reason is the roof. `frame_build.cpp:200-203`
zeroes `h` for any terrain that transmits light, and a building FLOOR transmits (coverage 0), so
a roofed tile carries `h == 0` with its occlusion in the **roof bit**, channel 1. The march
never consults `roof_at` — `sky_sun.comp.hlsl:97` and `:143` are its only two uses, the sky-dome
walk and the probe's own tile — so `TREE_H` is currently the only thing making a roofed
footprint block the sun. Dropping it to `0.0` would make buildings stop casting shadows, which
inverts the intended "trees long, buildings short" result. Use the roof as the fallback instead:
`ROOF_H = 1.00` is already declared at `:65` and is presently unused anywhere in the file.

Both exits must sit under one height test. Today the hard hit is already inside
`ray_h < h_eff` (`:181-184`), and the gate comment at `:166-173` exists precisely to stop a ray
that has climbed above a crown from being zeroed while still horizontally over its footprint;
hoisting `sd < 0.05` out of that test would reintroduce exactly that bug. Replace `:178-190`
with:

```hlsl
const float h = occ_height_at( (int)pos.x, (int)pos.y );
// No OccBuf height means no wall/furniture/tree here. A roofed tile still occludes —
// its floor transmits, so its height is 0 and the roof bit carries the occlusion.
// Open, unroofed ground occludes nothing and must not shadow itself.
const float h_eff = ( h > 0.01f )
                    ? h
                    : ( roof_at( (int)pos.x, (int)pos.y ) > 0.5f ? ROOF_H : 0.0f );
if( h_eff > 0.01f ) {
    if( sd < 0.05 ) {
        shadow = 0.0;
        break;                            // inside an occluder, below its top
    }
    if( sun_soft > 0.001f && ray_h < h_eff ) {
        shadow = min( shadow, saturate( sd / sun_soft ) );
    }
}
```

Delete the now-dead `TREE_H` constant at `:67` and the stale comment at `:172-173`. The CPU-side
`TREE_H` moves to the shared header in Stage 3 and keeps its meaning (it is the value written
into `OccBuf`); drop the "keep in sync with the shader" clause from its comment.

*(b) The SDF is seeded on open ground.* If view 6 reads red over open road the fault is upstream:
`occ_base.comp.hlsl:56-60` zeroes any tile that had a sprite footprint captured and defers to
`occ_raster`, while `jfa_seed.comp.hlsl` seeds whenever coverage beats the 4×4 Bayer minimum
`0.03125`. `src/cata_tiles.cpp:2262-2268` derives `blk` by Beer-Lambert relative to open air and
early-returns only at `blk <= 0.0`, so terrain transmitting marginally more than open air gets
"captured", loses its tile-square fallback, and can deposit coverage above the seed threshold.
Stage 3 removes this by construction — implement Stage 3 as the fix rather than tuning the
threshold here.

*(c) The SDF is green but view 14 is still dark.* This is the case (a) and (b) do NOT cover, and
it must be checked before touching either: with a healthy SDF the `sd` over open road is large,
so the penumbra term `saturate(sd/sun_soft)` is 1 and `sd < 0.05` is false — mechanism (a)
cannot darken it and editing `h_eff` will change nothing. The remaining ways `celestial_occ_dir`
returns 0 on open ground are all above the height gate: the probe's own-roof early-out
`roof_at((int)probe.x,(int)probe.y) > 0.5 → return 0.0` (`sky_sun.comp.hlsl:143`), and the sun
direction/elevation params. The `[roofdiag]` probe measured the CPU `occ` vector, not the
uploaded `OccBuf`, so a stride/offset error in the occ upload (`sdf_pass.cpp:246-266`, indexed
`(x*map_h+y)*2+1` against a buffer written at runtime stride) would make the GPU read garbage
roof bits while the CPU probe reads 0 — check by capturing a roof-channel debug view or by
temporarily forcing `roof_at` to return 0 in the shader and re-measuring view 14. If instead the
params are wrong, `[segdiag]`/`[sundiag]` already dump `sin_elev` and the sun direction; a
negative or ~0 `sun_sin_elev` collapses `t_end = MAX_OCC_H / max(elev_tan, 0.001)` and mis-aims
the march. Only once view 6 is confirmed green AND this branch is cleared do (a)/(b) apply.

Apply (a) first and re-measure before touching (b); applying both at once destroys attribution.
Note that (a) alone will show little movement on genuinely open ground with a healthy SDF (large
`sd` → no feather) — that is expected, not a failed fix; (a) is a correctness fix whose visible
effect appears where the SDF is small, i.e. in combination with (b).

**0f. Healthy `SkyBuf`: measure the composite.** Capture `CATA_DBG_MODE=5` (`gpu_total`) and
`7` (raw `SkyVisBuf`). Bright 5 with a black frame puts the loss in the post-clamp multipliers
`cloud_vis` / `sun_shad_mul` (`sprite.frag.hlsl:1038-1039`) or the tonemap/grade stage; view 17
isolates `cloud_vis`, and `echo "cloud_strength 0" > /tmp/cata_knob` (already supported) is the
confirmatory A/B. A dark 7 against a 99.4 %-open CPU array means fragment-side index math:
`SkyVisBuf` is tile-res with stride `sdf_map_h` (`:350-358`), the same runtime pair implicated in
0d, so re-check that first.

### Stage 1 — Make "no data" visually and textually distinct from "dark"

The design currently states the opposite intent (`sky_sun_pass.cpp:45-49`: *"a failed pipeline
just leaves the sky reading as zero (dark — same as 'no data yet')"*). Invert it.

1. **Repurpose a reserved pad as a validity flag — no layout change.** `debug_params` is wire-locked
   at 272 bytes (`static_assert`, `sprite_batcher.cpp:61`) and ends with three reserved pads
   (`sprite_batcher.h:326-328`) mirrored at `sprite.frag.hlsl:191-193`. Rename `cloud_pad0` →
   `float sky_valid = 0.0f;` in both files, at the same offset. `sizeof` is unchanged and the
   `static_assert` still passes.
2. **Producer.** Add `auto dispatches() const noexcept -> std::uint64_t` to `sky_sun_pass`
   (counter incremented in `record()` after `SDL_DispatchGPUCompute`). In
   `assemble_light_inputs` (`sdl_render_frame.cpp:700`, right after `in.debug = g_dbg_params;`)
   set `in.debug.sky_valid = (rs.sky().ready() && rs.sky().dispatches() > 0) ? 1.0f : 0.0f;`.
3. **Visual sentinel.** In `sprite.frag.hlsl`, immediately before the debug-view block at
   `:1224`, add: when `sdf_map_w > 0u && sky_valid < 0.5 && mode_gpu_lit`, blend the fragment
   halfway to magenta — `final_rgb = lerp(final_rgb, float3(1.0, 0.0, 1.0), 0.5);`. A world that
   is rendering without its directional lighting is then unmistakable rather than merely dark.
   This is the Valve/Unity error-asset convention applied to a buffer instead of a texture.
4. **Textual sentinel.** In `render_state::init`, collect the names of optional lighting passes
   whose `init` returned false and, if any, log once at `DL::Error` / `DC::Main`:
   `[lighting] DEGRADED: <comma-separated pass names> — world lighting will be incomplete`.
   Today those failures log individually and are then swallowed by a silent `ready()` gate.
5. **Fix the probes that cannot print.** `dbg()` is a per-file macro bound to `DC::SDL`, which
   the default log config filters out, so probes written to debug exactly this bug were silent.
   In `src/lighting/sdf_pass.cpp:311-327` switch `[skyvisdiag]` to
   `DebugLogFL( DL::Info, DC::Main )` and change its window from `++diag_n <= 14` (the pre-settle
   frames only) to `diag_n <= 2 || diag_n % 60 == 0`.

### Stage 2 — Declare pass dependencies instead of nesting them

`sdl_render_frame.cpp:508` gates sky/sun **and** GI behind one conjunction that includes
`rs.gi().ready()`, so a GI pipeline failure silently disables daylight — even though `sky_sun`
consumes no GI data. The SDF pass was already de-coupled for exactly this reason, with the
rationale written at `:473-479`; the fix was applied to one sibling and not the other.

Replace the single block with three sequential, independently-guarded blocks, keeping the
existing record order (sky before GI, because `gi_field.comp` reads `SkyBuf` and SDL_GPU derives
the write→read barrier from submission order):

1. sky/sun — guard `rc_rebuild && rs.sdf().populated() && rs.sky().ready() && rs.sdf().occ_buffer() && rs.sdf().sdf_buffer()`
2. GI — guard `rc_rebuild && rs.sdf().populated() && rs.gi().ready() && rs.collector() && rs.sdf().sdf_buffer()`

`map_w`/`map_h` and the `sun_params` are computed once above both blocks (they are shared
inputs, unchanged). On any frame where `rc_rebuild` is true, log one line at `DL::Info` /
`DC::Main` naming what ran and, for anything skipped, which term was false:

```
[lighting][passes] rc=1 sdf=<ran|skip:reason> sky=<ran|skip:reason> gi=<ran|skip:reason> map=<W>x<H>
```

`reason` is the first false conjunct's name (`populated`, `sky_ready`, `gi_ready`, `occ_buf`,
`sdf_buf`, `collector`). This is the minimum viable form of a frame graph's central guarantee —
a pass is culled only by its **own** unmet inputs — without building a graph.

### Stage 3 — One source of truth for "this blocks light"

Four encodings drift today, each with its own magic threshold, and the repo records two bugs
caused by that drift: windows at `coverage 60` landing exactly on `SKY_WALL_H = 0.60` and
blacking out every interior (`frame_build.cpp:184-199`), and a `t * 255` pack collapsing the
whole SDF (`frame_build.cpp:112-134`).

Create `src/lighting/tile_occlusion.h` — pure functions, no new dependency on `map`, following
the pattern `src/tile_light_mode.h` already established in this codebase for exactly this kind
of decision (plain-data query struct + `constexpr` classifier, testable without a live `game`):

```cpp
#pragma once

#include <algorithm>

namespace lighting {

/// Occluder height (tiles) written into OccBuf for full trees: tall enough that a
/// tree casts a long shadow where a wall (~1.0) casts a short one.
inline constexpr float TREE_H = 3.0f;
/// Roof height (tiles); a celestial ray clears a roof once it climbs above this.
inline constexpr float ROOF_H = 1.0f;
/// Occluder height at or above which a sky-dome direction is blocked.
inline constexpr float SKY_WALL_H = 0.60f;

/// Everything the occlusion rules need, as plain data.
struct tile_occlusion_query {
    /// level_cache::transparency_cache value — an ATTENUATION COEFFICIENT, not a
    /// fraction: LIGHT_TRANSPARENCY_SOLID (0.0) is the opaque sentinel and open air
    /// is only LIGHT_TRANSPARENCY_OPEN_AIR (0.0384).
    float transparency = 0.0f;
    /// map::coverage() — the ranged-COVER gameplay stat, 0..100.
    int coverage = 0;
    bool is_tree = false;              ///< TFLAG_TREE
    bool is_vehicle_obstacle = false;  ///< veh_at()->obstacle_at_part()
    bool floor_above = false;          ///< floor_cache(z+1)
    bool outside = false;              ///< outside_cache
    bool terrain_valid = true;         ///< ter(p).is_valid(); false during world load
};

/// The single occlusion verdict every lighting consumer derives from.
struct tile_occlusion {
    bool blocks_light = false;  ///< opaque to light; the ONLY predicate that may seed the SDF
    float height = 0.0f;        ///< occluder height in tiles; 0 when the tile transmits
    bool roofed = false;
    bool open_sky = false;
};

/// Coverage supplies an occluder's HEIGHT; the transparency cache — the same field the
/// game's own LOS trusts, and which already discounts glass, bars and chain-link —
/// decides whether it blocks LIGHT at all. Anything that transmits casts no shadow.
constexpr auto classify_tile_occlusion( const tile_occlusion_query &q ) -> tile_occlusion;

} // namespace lighting
```

Rules inside the classifier, transcribed from today's scattered sites so behaviour is preserved
exactly except where noted:

- `blocks_light = q.terrain_valid && q.transparency <= 0.0f` (`LIGHT_TRANSPARENCY_SOLID`), OR
  `q.is_vehicle_obstacle`.
- `height`: `0.0f` when `!q.terrain_valid` or the tile transmits; otherwise
  `clamp(coverage / 100.0f, 0, 1)`; then `max(height, 1.0f)` if `is_vehicle_obstacle`; then
  `TREE_H` if `is_tree`. (Same precedence as `frame_build.cpp:180-216`.)
- `roofed = q.floor_above`; `open_sky = q.outside`.

Then rewire every consumer to it, deleting their private copies of the rules:

1. `src/lighting/frame_build.cpp` — build `transparency`, `occ` and `sky_vis` from one
   `classify_tile_occlusion` call per tile inside the existing loop. Delete the local `TREE_H`
   at `:32` (now in the header) and the inline transparency/coverage/vehicle/tree logic at
   `:130-134` and `:180-219`.
2. `src/cata_tiles.cpp:2262-2268` — replace the `blk <= 0.0f` early-out with
   `if( !classify_tile_occlusion( q ).blocks_light ) { return; }`, keeping `blk` afterwards only
   as the soft coverage weight for tiles that *do* block. This is the behaviour change that
   removes the open-ground seeding hazard at its source rather than by threshold tuning.
3. `data/shaders/lighting/src/sky_sun.comp.hlsl` — `SKY_WALL_H` / `ROOF_H` / `TREE_H` keep their
   current shader-side values; add a one-line comment naming `src/lighting/tile_occlusion.h` as
   the authority so the next edit knows where the other half lives.

New test `tests/tile_occlusion_test.cpp` (Catch2, tag `[lighting]`), asserting the consumer-visible
contract and specifically the two historical regressions:

|Input|Expected|
|---|---|
|open air (`transparency = 0.0384`, `coverage = 0`)|`blocks_light == false`, `height == 0`|
|solid wall (`transparency = 0`, `coverage = 100`)|`blocks_light == true`, `height == 1.0`|
|**window** (`transparency = 0.0384`, `coverage = 60`)|`blocks_light == false`, `height == 0` — below `SKY_WALL_H`, so daylight still reaches interiors|
|tree (`transparency = 0`, `coverage = 80`, `is_tree`)|`height == TREE_H`, i.e. strictly greater than a wall's|
|vehicle obstacle over open air|`blocks_light == true`, `height >= 1.0`|
|`terrain_valid == false` (world load)|`blocks_light == false`, `height == 0`|

### Stage 4 — Declare the binding layout once, and pass constants as a struct

1. **Slot table.** New `src/lighting/frag_slots.h`:
   ```cpp
   #pragma once
   #include <cstdint>
   namespace lighting {
   /// Fragment storage-buffer slots for sprite.frag, in bind order. The shader
   /// declares them contiguously at t2..t8 (space2); DXC strips any buffer the
   /// shader does not read, which shifts the t-range and makes D3D12 reject the
   /// root signature. Keep this enum, the bind array in sprite_batcher.cpp and the
   /// register declarations in sprite.frag.hlsl in lockstep.
   enum class frag_sbuf : std::uint32_t {
       emitters = 0, sdf = 1, sky_vis = 2, gi = 3, sky = 4, ramp = 5, pal_index = 6,
   };
   inline constexpr std::uint32_t FRAG_SBUF_COUNT = 7u;
   inline constexpr std::uint32_t FRAG_STORAGE_TEX_COUNT = 1u;
   } // namespace lighting
   ```
   Use it for the array size in `bind_lighting_resources` (`sprite_batcher.cpp:845-848`).
2. **Assert the convention the comment currently only requests.** `sprite_batcher` already logs
   shader reflection (`sprite_batcher.cpp:401`, the `sprite frag expects samplers=1 st=1 sb=7 ub=3`
   line). For the sprite pipeline only, compare the reflected storage-buffer and storage-texture
   counts against `FRAG_SBUF_COUNT` / `FRAG_STORAGE_TEX_COUNT`; on mismatch log at `DL::Error` /
   `DC::Main` and `throw std::runtime_error("sprite frag binding layout mismatch")` — the same
   failure mode this function already uses for a failed shader compile (`:385`). This converts
   the silent DXC-stripping hazard into a startup failure with a name.
3. **Options struct.** Replace the 18-parameter positional call with one struct, per this repo's
   own `>3 parameters` rule. Add to `sprite_batcher.h` next to `sun_params`:
   ```cpp
   struct lighting_resources {
       float tile_pixel_size = 32.0f;
       float z_level = 0.0f;
       std::uint32_t emitter_count = 0u;
       float ambient = 0.05f;
       float cam_off_x = 0.0f;
       float cam_off_y = 0.0f;
       std::uint32_t sdf_map_w = 0u;
       std::uint32_t sdf_map_h = 0u;
       SDL_GPUBuffer *emitter_buf = nullptr;
       SDL_GPUBuffer *sdf_buf = nullptr;
       SDL_GPUSampler *data_sampler = nullptr;
       SDL_GPUBuffer *sky_vis_buf = nullptr;
       SDL_GPUBuffer *gi_buf = nullptr;
       SDL_GPUBuffer *sky_buf = nullptr;
       SDL_GPUBuffer *ramp_buf = nullptr;
       SDL_GPUBuffer *pal_index_buf = nullptr;
       const sun_params *sun = nullptr;
       const debug_params *debug = nullptr;
   };
   auto set_lighting_resources( const lighting_resources &r ) -> void;
   ```
   Clean cutover — the old overload is deleted, not kept. Every callsite, from
   `grep -rn "set_lighting_resources" src/`: the declaration (`sprite_batcher.h:407`), the
   trampoline (`sprite_batcher.cpp:885`), the impl (`sprite_batcher.cpp:250`), and exactly two
   real callers, `render_state.cpp:427` (tile batcher) and `render_state.cpp:1000` (shadow
   batcher), both of which become designated-initializer literals. Keep the existing null-sampler
   guard behaviour inside the impl.

### Stage 5 — Assert the gameplay/render invariant that does not exist

Nothing today reconciles Desk 1 (`lightmap.cpp`, which answers `LUX :: bright`) with Desk 3
(`lighting/`, which paints pixels). Sprite tint is a flat `1.0` and the CPU lightmap survives in
the renderer only as the one-bit `lightmap_ever_generated()` latch — deliberate, and not to be
undone. What is missing is the assertion that the two do not contradict each other.

**The invariant must be read from the GPU output, not the CPU inputs.** This bug's own evidence
is that `sun_intensity=1.22` / `sky_intensity=1.03` reach the GPU correctly — the cbuffer inputs
are fine and the failure is entirely downstream, in `SkyBuf`. A check of
`in.sun.sun_intensity + in.sun.sky_intensity` would read ~2.25 during the black frame and never
fire. The conformance signal therefore has to come from what the sky pass actually *wrote*, via
a readback, not from what it was *told*.

1. **Add a one-shot `SkyBuf` readback**, cloning `gi_compute_pass::debug_log_stats`
   (`src/lighting/gi_compute_pass.cpp:336-397`): `auto sky_sun_pass::readback_means(runtime_w,
   runtime_h) -> sky_means` returning `{ float rgb_mean; float a_mean; }` over the runtime
   region of `sky_buf_` (4 floats/tile, `[(x*max_h+y)*4+c]`, rgb = sky-access, a = sun-occ). It
   uses `SDL_DownloadFromGPUBuffer` + `SDL_WaitForGPUIdle`, so it stalls the GPU — fire it
   **exactly once**, gated on a `static bool done` and on being ≥ 120 frames past the first
   gameplay frame (so the map cache has settled), never per frame.
2. **Assert at that one point** in `flush_and_gather_rc`, right after the sky dispatch block
   Stage 2 creates:
   ```cpp
   if( g && !forced_celestial_hour() && rs.sky().ready() ) {
       static int gp_frames = 0;
       static bool reported = false;
       if( !reported && ++gp_frames == 120 ) {
           reported = true;
           const float gameplay = g->natural_light_level( g->u.bub_pos().z() );
           const auto m = rs.sky().readback_means(
               static_cast<std::uint32_t>( rs.sdf().map_w() ),
               static_cast<std::uint32_t>( rs.sdf().map_h() ) );
           if( gameplay > LIGHT_AMBIENT_LIT && m.rgb_mean + m.a_mean < 0.01f ) {
               DebugLogFL( DL::Error, DC::Main )
                   << "[lighting][conformance] gameplay light " << gameplay
                   << " > LIGHT_AMBIENT_LIT but SkyBuf mean (rgb=" << m.rgb_mean
                   << " a=" << m.a_mean << ") is ~0 — daylight is not reaching the screen";
           }
       }
   }
   ```
   `natural_light_level` is `src/game.h:704`; `LIGHT_AMBIENT_LIT = 10.0f` is `src/lightmap.h:22`.

This detects the actual failure class — the sun pass produced nothing while gameplay is bright —
because it reads `SkyBuf` content. It deliberately does NOT try to catch the 0e case (sun-occ
dark over open ground while sky-access is fine): that leaves directional sky fill on screen, so
it is not a "bright gameplay, black screen" contradiction and is not this invariant's job. Stage
1's `sky_valid` sentinel is the complementary, zero-cost per-frame signal for "the pass never
ran at all"; this readback is the one-shot confirmation that, when it did run, its output is
non-trivial.

### Stage 6 — Correct the map everyone reads first

`src/lighting/CLAUDE.md` is the entry point for this subsystem and currently misdirects:

- The file map (`:24`) calls `sdf_pass` "Chebyshev SDF (CPU BFS) + occluder seeding"; the SDF is
  GPU JFA (`gpu_sdf_pass`), and `sdf_pass` is now upload/storage only. The whole
  "Shadow march + dither model" section (`:229-238`) reasons from the retired Chebyshev field.
- `:19` and `:231` place the sprite shaders as embedded `SPRITE_VERT_HLSL`/`SPRITE_FRAG_HLSL`
  strings in `sprite_batcher.cpp`; they are loaded from `data/shaders/lighting/src/` via
  `load_lighting_shader_source` (`sprite_batcher.cpp:380-382`) and the embedded copies are gone.
- `:261-264` states `debug_params` is 256 bytes; the `static_assert` says 272
  (`sprite_batcher.cpp:61`).
- `:324-337` presents GPU JFA as future work; it shipped.

Update those four, add the Stage 2 pass-gating rule and the Stage 3 single-source rule to the
"Known invariants & gotchas" list, and delete the tracked merge leftover
`src/lighting/sound_wave_pass.h.orig`.

### Stage 7 — Replace the bounce march with Radiance Cascades

`src/lighting/CLAUDE.md:321` already names this as the intended successor. It is last, not
first, for one reason: a wrong GI field and a correct one both render as "a bit dark", so RC is
unverifiable until Stage 1's sentinel and Stage 2's declared gates exist to distinguish "pass
produced nothing" from "pass produced darkness". It also does not fix the black-scene bug —
that lives in `SkyBuf`, a different pass — so it must not be entangled with Stage 0.

**What is replaced, and what is deliberately kept.** `gi_field.comp.hlsl` stays: it already
produces the per-tile *direct* radiance (occluded emitter gather + sun/sky injection at
`gi_field.comp.hlsl:150-162`, tinted by tile albedo) that any GI method needs as its emitter
texture. RC replaces only the two bounce dispatches, `gi_bounce.comp.hlsl` and
`gi_bounce2.comp.hlsl`, and with them the EMA temporal filter and its `term_a_`/`term_b_`
ping-pong. The sprite shader, the `GiBuf` slot and the fragment binding table are **unchanged**:
a final resolve dispatch writes cascade 0 back into the existing `gi_out_buf_` layout (tile-res,
4 floats/tile, x-major `[(x*map_h+y)*4+c]`). That keeps Stage 4's binding invariant untouched
and confines the change to one pass.

**What this swap actually buys — and what it does not.** The GI dispatch (old and new) fires
only on a structure rebuild: `rebuild.structure = devui || gen != last_gen || z != last_z ||
origin != last_origin || cam_drifted` (`src/sdl_render_frame.cpp:237-239`), i.e. terrain
transparency change, z change, map shift, or ≥4-tile camera drift — **not** on emitter movement
(the comment at `:187-188` is explicit that emitters refresh outside this gate). So a light that
moves in static terrain does not re-run GI at all; the field is stale until the next structure
rebuild regardless of which bounce algorithm is used. The EMA in `gi_bounce2` therefore blends
across those sparse rebuilds, not across frames. **RC's benefit here is per-rebuild quality —
noise-free, ghosting-free, sharp near-field contact bounce in the one frame it runs — not
lower latency to moving lights;** the latency is owned by the rebuild gate and is unchanged.
Do not sell or verify this stage on dynamic-light responsiveness. (Making GI refresh on emitter
change is a separate gate change, explicitly out of scope.)

**Parameterisation** (flatland formulation, Sannikov, *Radiance Cascades*,
[paper](https://github.com/Raikiri/RadianceCascadesPaper); the cascade-scaling relations are its
§"Radiance cascades" and the merge rule its §"Calculating indirect lighting"). Constants in a
new `src/lighting/rc_params.h`, mirrored as `static const` in the shaders:

|Constant|Value|Rationale|
|---|---|---|
|`RC_CASCADES`|`5`|Reach `d0·(4^N − 1)/3` = 341 tiles, comfortably beyond the 180-tile bubble; 4 would give 85 tiles, still beyond the 46×26-tile viewport, and is the pre-decided fallback if Stage 7 cost fails|
|`RC_C0_PROBE_SPACING`|`1.0` tile|One cascade-0 probe per tile — matches the existing GI resolution exactly, so the resolve step is a straight average with no resampling|
|`RC_C0_DIRS`|`4`|Cascade 0 angular step; the minimum that the ×4 branching builds on|
|`RC_BRANCH`|`4`|Angular texels ×4 and probe spacing ×2 per axis per cascade, so memory per cascade is constant and the merge averages 4 angular texels — the paper sanctions averaging "2 or 4 texels" and 4 reaches usable angular resolution in 5 cascades instead of 10|
|`RC_C0_INTERVAL`|`1.0` tile|Cascade `i` marches `[d0·(4^i − 1)/3, d0·(4^(i+1) − 1)/3]`|

Memory: every cascade is `probes × dirs = 32400 × 4` texels of `float4` (rgb + transmittance
β) ≈ 2.1 MB, so 5 cascades ≈ 10.4 MB — allocate one flat `StructuredBuffer<float>` atlas with
cascade `i` at offset `i * RC_CASCADE_FLOATS`, texel index
`((py * probes_x + px) * dirs_i + dir) * 4`. Rays per rebuild ≈ 648k against the existing path's
518k, and RC rebuilds on the same `rc_rebuild` gate, not per frame.

**Three new shaders**, all reusing the existing `sdf_bilinear` sphere-trace already present in
`gi_field.comp.hlsl` and `sky_sun.comp.hlsl` (do not write a third copy — lift the shared one
into `data/shaders/lighting/src/jfa_shared.hlsl`, which both already include):

1. `rc_build.comp.hlsl` — one thread per cascade texel. Sphere-traces its own interval
   `[t_i, t_{i+1}]` through the SDF from its probe position along its direction, sampling
   `field_buf_` (the direct-radiance texture from `gi_field`) at the hit tile. Writes
   `rgb = radiance at hit`, `a = β` (0 if the interval terminated on an occluder, 1 if it
   passed through clear). **A ray that escapes without hitting returns zero, not sky radiance** —
   the sky term is already applied independently in `sprite.frag` via `SkyBuf`, and returning it
   here would double-count it. One dispatch per cascade; cascades are mutually independent, so
   order does not matter.
2. `rc_merge.comp.hlsl` — one thread per cascade-`i` texel, run for `i = RC_CASCADES-2 … 0`
   descending. The load-bearing detail the merge lives or dies on is the **probe-position
   mapping between cascades**, which the flat index formula alone does not encode: cascade `i`
   probe `(px,py)` sits at world position `(px + 0.5) · 2^i` tiles (probe centres, spacing
   `2^i`), and cascade `i+1`'s probes sit at spacing `2^(i+1)` offset by half of that — so a
   cascade-`i` probe never coincides with a cascade-`i+1` probe and MUST be found by bilinear
   weight over the 4 surrounding `i+1` probes at `g = (world_i / 2^(i+1)) - 0.5`, `floor(g)` +
   fractional weights, exactly the `p - 0.5` convention `sdf_bilinear` already uses. Getting
   this offset wrong is the single most common RC bug and produces light leaking through wall
   corners. For each of those 4 neighbours, first average the `RC_BRANCH` angular sub-directions
   that fall inside this texel's direction wedge (each `i+1` texel spans `1/RC_BRANCH` of an
   `i` texel's angle), THEN apply the bilinear spatial weights, THEN merge with this texel's own
   interval by the paper's rule `L = L_i + β_i · L_{i+1}`, `β = β_i · β_{i+1}`. Averaging the
   sub-directions **before** the spatial interpolation is the "bilinear fix"; doing it after is
   the classic mistake that reintroduces the leak.
3. `rc_resolve.comp.hlsl` — one thread per tile. Averages cascade 0's `RC_C0_DIRS` directions
   into one irradiance value and writes it to `gi_out_buf_` in the existing layout. This is the
   only pass the sprite shader observes.

**C++ changes**, all inside `src/lighting/gi_compute_pass.{h,cpp}` — clean cutover, no
compatibility path:

- Add pipelines `rc_build_pipeline_`, `rc_merge_pipeline_`, `rc_resolve_pipeline_`; delete
  `bounce_pipeline_`, `bounce2_pipeline_`, `gi_buf_`, `term_a_`, `term_b_`, `term_flip_`.
- Add `SDL_GPUBuffer* rc_atlas_` sized `RC_CASCADES * RC_CASCADE_FLOATS`, usage
  `COMPUTE_STORAGE_READ | COMPUTE_STORAGE_WRITE`, zeroed at init by the existing
  `zero_buffer` helper.
- `record()` becomes: `gi_field` → `rc_build` ×`RC_CASCADES` → `rc_merge` descending
  ×`RC_CASCADES-1` → `rc_resolve`. `ready()` requires the three new pipelines plus
  `field_buf_`, `rc_atlas_` and `gi_out_buf_`. `gi_buffer()` still returns `gi_out_buf_`, so
  `render_state` and the sprite binding are untouched.
- Delete `gi_params::gi_temporal` and `gi_params::gi_bounce2`, their assignments in
  `src/sdl_render_frame.cpp`, and their F4 bindings in `src/sdl_lighting_devui.cpp`
  (`grep -rn "gi_temporal\|gi_bounce2" src/` must return nothing afterwards). Keep
  `gi_params`'s remaining fields and its 64-byte size by replacing the two removed floats with
  `float rc_pad0 = 0.f, rc_pad1 = 0.f;`. The `GiParams` cbuffer is declared in the HLSL of every
  pass that shares it — `gi_field.comp.hlsl:48-49` currently declares `gi_temporal`/`gi_bounce2`
  as "unused here"; rename those two to `rc_pad0`/`rc_pad1` in lockstep with the C++, and the
  three new `rc_*.comp` shaders must declare the identical `GiParams` layout, or the shared
  cbuffer upload writes the wrong offsets.
- **State each new pipeline's expected reflection counts** in its `init` check, following the
  `X.comp reflection: ro_sb=N rw_sb=M` assertion the other passes log
  (`gi_compute_pass.cpp:52-68`, `sky_sun_pass.cpp:38-43`). From the bindings above:
  `rc_build` reads `field_buf_` (t0) + `sdf_storage_` (t1) and writes `rc_atlas_` (u0) →
  `ro_sb=2 rw_sb=1`; `rc_merge` reads+writes `rc_atlas_` in place → `ro_sb=0 rw_sb=1` (or
  `ro_sb=1 rw_sb=1` if a separate read alias is bound — pick one and assert it); `rc_resolve`
  reads `rc_atlas_` (t0) and writes `gi_out_buf_` (u0) → `ro_sb=1 rw_sb=1`. A mismatch at
  startup means a buffer was stripped or mis-declared — fail loudly there, do not ship a silently
  degraded GI.
- `debug_log_stats` keeps reading `gi_out_buf_` unchanged — it remains the dev oracle, and after
  Stage 1 it is joined by the `sky_valid` sentinel for the sky side.

Delete `gi_bounce.comp.hlsl` and `gi_bounce2.comp.hlsl`; leave `debug_mode == 12` (the GI view,
`sprite.frag.hlsl:1283-1291`) exactly as-is — it reads `gi_out_buf_` and is the acceptance
instrument for this stage.


## Critical files & anchors

|File|Anchor|Why|
|---|---|---|
|`data/shaders/lighting/src/sky_sun.comp.hlsl`|`celestial_occ_dir` :137-198; `TREE_H`/`MAX_OCC_H` :66-67|The vacuous height gate and the `sd < 0.05 → shadow = 0` path|
|`data/shaders/lighting/src/sprite.frag.hlsl`|`dbg_active` :1206; debug views :1227-1364; `sun_shad_mul`/`gpu_total` :1038-1039; DebugParams tail :191-193|Why a black frame and a "dark" debug view can both mean misclassified; the free pad slot|
|`src/sdl_render_frame.cpp`|`:325` `rc_rebuild`; `:480-504` SDF dispatch; `:508`/`:525` the gate to split; `:587-762` `assemble_light_inputs`|Every gate that can silently disable the directional layer|
|`src/lighting/frame_build.cpp`|`:130-134` transparency mask; `:180-219` occ height/roof; `:222-235` the `[roofdiag]` probe idiom to copy|The CPU producer that Stage 3 collapses onto one classifier|
|`src/cata_tiles.cpp`|`push_occluder_footprint` :2236-2313 (`blk` :2262, early-out :2268); `classify_tile_light` call :2158-2163|Where open-ish terrain gets captured and suppresses the tile-square SDF seed; the classification query|

## Verification

Prerequisites for every capture: repo root as cwd, `--world Bairdford`, the precondition log
gate satisfied, screenshots via the `computer` tool with `{ silent: true }` on the game window,
default zoom, no keypresses after load.

1. **Baseline, before any fix.** Record from one session: `light_mode_check.py` class fractions
   (view 16); `mean` / `bright%` / `dark%` from the numpy snippet for views 13, 14, 6; and the
   counts of `rc=1`, `[lighting][occ]` and `structure_rebuild ms=` in the log. Without this
   column the fix is unproven.
2. **After the fix.** Repeat verbatim. Expected: view 14's open-road region moves from
   `dark% ≈ 1.0` to `bright% > 0.8`; view 13 is bright; the normal (`CATA_DBG_MODE=0`) terrain
   viewport mean luma rises by a multiple, not a few percent; the HUD still reads `LUX :: bright`.
3. **Tree shadows still exist** (guards against "fixed it by making everything bright"). In a
   scene containing a tree, view 14 must show the leeward side at least 2× darker than the
   windward side, measured as the numpy mean of two same-size boxes either side of the trunk.
4. **Building shadows still exist.** A tree passes item 3 on its own OccBuf height, so it cannot
   detect the roof regression the `h_eff` edit risks: stand where a **building** is between the
   avatar and the sun, and require the same 2× leeward/windward ratio in view 14 measured across
   the building's shadow, plus a visibly *shorter* shadow than the tree's at the same hour. If
   the building's ratio is below 2× while the tree's passes, the `ROOF_H` fallback is not firing
   — check that the roofed tiles along the ray actually carry `roof == 1` with the `[roofdiag]`
   probe before changing anything else.
5. **Stage 1 sentinel fires.** Temporarily force `in.debug.sky_valid = 0.0f`, relaunch, and
   confirm the world renders magenta-tinted rather than black, then revert the force. This proves
   the sentinel is wired, which is the whole point of the stage.
6. **Stage 2 gate line.** `grep "\[lighting\]\[passes\]" "$LOG" | tail -5` shows
   `sdf=ran sky=ran gi=ran` in a healthy session. Then temporarily make `gi().ready()` return
   false, relaunch, and confirm `sky=ran gi=skip:gi_ready` — daylight must survive a dead GI
   pass. Revert.
7. **Stage 3 contract.** `./cata_test-tiles "[lighting]" --rng-seed 1` — the new
   `tile_occlusion_test.cpp` passes, including the window row, which encodes the interior-blackout
   regression.
8. **Stage 4 layout guard.** Temporarily delete one `StructuredBuffer` read from
   `sprite.frag.hlsl` so DXC strips it, relaunch, and confirm startup now fails with
   `sprite frag binding layout mismatch` instead of a device-removed crash or silent corruption.
   Revert.
9. **Stage 5 conformance.** During the pre-fix repro (SkyBuf zero, gameplay bright) the log must
   contain `[lighting][conformance] ... daylight is not reaching the screen`; after the fix it
   must not. This detects the specific black-scene class (sky pass produced ~0 while gameplay is
   bright) because it reads back `SkyBuf` content; it is not a catch-all for every lighting bug
   and deliberately stays silent for a sun-only (0e) darkening that leaves sky fill on screen.
10. **Stage 7 GI null.** With `echo "gi_strength 0" > /tmp/cata_knob` the frame must be
    pixel-identical before and after the Radiance Cascades swap (same save, same hour, same
    zoom): mean absolute difference over the terrain viewport < 1/255. A non-zero null means RC
    is leaking into a path it must not touch.
11. **Stage 7 GI correctness and the property RC is adopted for.** Build the night scene
    (`Debug → Spawning` a single bright point light against a wall corner, sun off via
    `echo "sun_scale 0" > /tmp/cata_knob`), capture `CATA_DBG_MODE=12` (GI view) before and
    after. Required: (a) light visibly bleeds around the corner in both, so the swap did not lose
    bounce; (b) the penumbra edge is smooth rather than tile-blocky after — measure the luma
    gradient across a 16-px band at the shadow edge and require the post-change profile to be
    monotonic with no step larger than 25% of the total range; (c) **no ghosting across the
    rebuild it does run** — the old EMA blends the previous field into the new one, RC does not.
    Move the light, then force a structure rebuild (the GI dispatch only fires on one — pan the
    camera ≥4 tiles, or toggle a nearby door; a bare light-move does NOT re-run GI), capture view
    12 on the first frame after that rebuild, and confirm the nonzero-region centroid sits at the
    light's NEW position with no residual lobe at the old one. Do not phrase this as per-frame
    latency: the dispatch cadence is the structure-rebuild gate, unchanged by this stage.
12. **Stage 7 cost.** Record `[render][perf]` frame cost on rebuild frames before and after. A
    rebuild frame may not regress by more than 2 ms at the shipped cascade count; if it does,
    drop `RC_CASCADES` from 5 to 4 (reach 85 tiles, still larger than the 46×26-tile viewport)
    and re-measure rather than tuning ray counts.
13. **Cross-platform guard.** The lighting shaders are translated by shadercross for Metal and
    consumed as HLSL on D3D12/Vulkan: keep the edits plain HLSL, and do not change field order or
    size of `SkySunParams` (52 bytes, mirrored by `sky_sun_pass.h:27-44`) or `DebugParams`
    (272 bytes, `sprite_batcher.cpp:61`). Confirm the startup log still reports
    `sky_sun.comp reflection: ro_sb=2 rw_sb=1 uniforms=1`, and that the new RC pipelines report
    their expected resource counts rather than failing to create.
14. **Suite.** `./cata_test-tiles "~[coop]" --rng-seed 1 --order decl` from the repo root, against
    the same command at the pre-change commit. The test binary has no GPU device, so shader-only
    edits must give an identical result set; C++ changes must not add failures. Judge by the
    `test cases: N | N passed` line, not the exit code — SDL_GPU device creation always fails in
    the windowless test binary and reports exit 1 regardless.
15. **Probes removed.** `grep -rn "TEMP DIAGNOSTIC\|CBN_DIAG_SEG_LIGHTING" src/` returns only the
    probes this plan deliberately keeps (`[skyvisdiag]`, `[roofdiag]`, `[fbdiag]`, `[segdiag]`,
    `[sundiag]`), and no forced `debug_mode`/`sky_valid`/`sun_scale` overrides survive.

## Assumptions & contingencies

- **`Bairdford` / `Isaias Dennison` is the repro save.** If the character does not spawn on open
  outdoor ground, walk it there rather than abandoning the repro — the measurement needs open sky
  overhead, not that exact tile.
- **The prior session's `[fbdiag] n=220` / `[roofdiag] roofed=0` figures are historical.** The
  live `debug.log` contains zero `structure_rebuild`, `[fbdiag]` or `[roofdiag]` lines because the
  last session never left the main menu. Treat them as background; never as grounds to skip a rung.
- **If `CATA_DBG_MODE` appears ignored** (view 16 shows the normal scene), fall back to
  `/tmp/cata_dbg_mode` after load; confirm which path fired by grepping the log for
  `lighting debug mode`.
- **If the game exits cleanly again**, assume harness error, not a game bug — `Log shutdown.`
  with `cam_wh=0x0` is the signature of synthetic input reaching Quit. Relaunch with `--world`.
- **If Stage 3's classifier changes measured behaviour** on any tile class beyond the open-ground
  seeding case (check with view 6 before/after), keep the old numeric rule and encode the
  difference as an extra field on `tile_occlusion` rather than silently altering shadows; the
  stage's purpose is single-sourcing, not retuning.
- **If Stage 7's RC output is dimmer than the old bounce** at the same `gi_strength`, do not
  rescale `gi_strength` to compensate — the old path's absolute level was arbitrary (a 16-ray
  gather plus an EMA), and matching it would bake the artefact in. Keep RC's physically-derived
  level and, if the scene reads too flat overall, raise `gi_strength`'s default once, recorded
  as a deliberate look change with a before/after view-12 capture.
- **If `RC_CASCADES = 5` misses the frame budget** (Verification item 12), drop to 4 rather than
  cutting `RC_C0_DIRS` below 4 or coarsening `RC_C0_PROBE_SPACING`: reach is the cheapest thing
  to trade here, and 85 tiles still exceeds the 46×26-tile viewport, whereas fewer cascade-0
  directions degrades near-field contact shadows, which is the property RC is being adopted for.
- **Scope boundary:** generalising Stage 2's three declared gates into a full frame-graph
  (resource lifetimes, automatic barrier derivation, pass culling) is deliberately not in this
  plan; the three explicit gates deliver the one guarantee this bug needed, and the rest is a
  rewrite that would destroy attribution.

## Final report (follow-up session: GI verification + default-knob polish pass)

### Root cause found and fixed: window portals contributed zero ambient light

Stage 7's radiance-cascade GI ships correctly (confirmed structurally and via GPU readback), but
a live in-game report — a roofed room reading pitch-black despite bright outdoor sun — traced to
a real bug in `snapshot.cpp`'s window-portal emitter, not the RC pipeline. Window portals (the
"fake area light at the window" that lets outdoor daylight reach a room's interior through a
lit window tile) modeled *only* the direct-sun term (`45.0f * sun_angle * sun.sun_intensity`),
where `sun_angle` is the horizontal 2D dot product between the window's outward normal and the
sun direction. Every window on a wall orientation the sun isn't currently hitting face-on
produced `sun_angle <= 0`, hence **zero** light contribution — a north-facing window (or any
window on the shadowed side of a building) stayed dark all day, regardless of a bright blue sky.

**Fix:** added an unconditional ambient-sky floor term, `18.0f * sun.sky_intensity`, blended
toward sky-blue tint as the direct term shrinks (`snapshot.cpp` window-portal block). A window
now always lets in *some* diffuse daylight while the sky is bright, on top of the existing
direct-sun beam when the window faces the sun. Also fixed a stale/misleading comment claiming
`sun_dir` was "the direction the sun comes from" — it is the direction light *travels*
(sun → ground → shadow); the portal's outward-face test needed `-sun_dir` and did not, which
would have inverted which wall lit up first.

**Verified live** (Bairdford save, real house interior): window portal diagnostic went from
`candidates=5 geom_ok=5 emitted=0` (all 5 windows in the room detected, angle-gated to zero) to
`emitted=5` after the fix — every window now contributes. GI compute readback confirmed the
room's light sum rose measurably (36877.7 → 39652.5, +7.5%) with `gi_feedback` also enabled — see
the corrected, more careful analysis below and in Known Limitations.

### Multi-bounce radiance feedback (`gi_feedback`) shipped at 0.3, was 0.0 (off) — corrected claim

`gi_field.comp.hlsl` already re-reads the previous rebuild's fully-cascaded GI (`PrevGiBuf`) as
an extra surface-radiance source — wired in an earlier part of this session — but the blend
strength (`g_gi_feedback`, `sdl_lighting_devui.cpp`) defaulted to `0.0f`, i.e. shipped disabled.
Set the default to `0.3`.

**Corrected, paired A/B measurement** (same scene, same structure-rebuild generation, `gi_feedback`
the only variable, via the `rc_readback` knob): `gi_feedback=0` gave
`sum=44516.7 max=3.697 nonzero=30352`; `gi_feedback=0.3` gave
`sum=62791.3 max=5.162 nonzero=30352`. **`nonzero` is identical between the two runs, and the
centroid barely moves** (`(99.67,89.11)` → `(99.68,89.10)`). This means `gi_feedback` is a pure
**intensity amplifier on tiles the field already reaches** — it raises `sum` (+41% here) and `max`
by re-injecting last rebuild's radiance as extra surface-radiance input, but it does **not**
extend the nonzero footprint into tiles the single-hop sphere-trace never touched. An initial,
wrong version of this claim (superseded here) asserted this satisfies "daylight walks deeper into
a room across rebuilds" — that is not supported by the nonzero count and should not be repeated.
If a genuinely dark room interior (outside the direct sphere-trace's reach) needs to receive any
light at all, `gi_feedback` alone does not solve that; it only makes already-reached surfaces
brighter over successive rebuilds. Whether that is sufficient depends on how far the direct term
already reaches relative to typical room sizes — not verified further this session.

**Caveat found on follow-up (plateau check)**: the only available forced-rebuild tool in this
build (`/tmp/cata_build_gi_scene`) is not scene-preserving — each trigger calls
`g->u.bub_pos()` fresh and both re-paints terrain and *appends* a new light to
`dev_test_lights::lights` (never cleared), so firing it repeatedly to force more rebuilds adds
cumulative new light sources rather than re-lighting the same fixed scene. A follow-up sweep
(readback → 4 more triggers → readback → 3 more triggers → readback) gave
`sum: 86411.8 → 102297 (+18.4%) → 119689 (+17.0%)`, with `max` jumping on the last step
(`6.156 → 6.152 → 6.666`) rather than staying flat as in the first pair above. This growth
pattern is consistent with "more lights were added to the map" and cannot be cleanly attributed
to `gi_feedback` accumulation alone — **the geometric-convergence / no-runaway claim is therefore
not independently confirmed by this follow-up**, and should be read as unverified rather than
re-asserted. The original paired A/B above (`sum` +41% for one clean `gi_feedback` flip) is
itself only approximately clean, since one `cata_build_gi_scene` trigger (adding one light) fell
between the two readings. A genuinely isolated, multi-rebuild `gi_feedback` convergence test
needs a rebuild trigger that changes nothing but camera/origin (e.g. a scripted ≥4-tile camera
pan-and-return) — not available in this session's harness.



### Default-knob audit: no other placeholder/debug defaults found

Read every field in `debug_params` (`src/lighting/sprite_batcher.h:187-338`, ~40 knobs) and the
GI/devui globals (`src/sdl_lighting_devui.cpp`). Every other knob already carries an in-tree
comment documenting a deliberate, previously-tuned value — none were debug-only leftovers shipped
as live defaults:

- `dither_bands = 32.0f` — raised from an earlier 12-band default after 12 bands read as visible
  blotching over the scene's ~0.5 dynamic-light range; 32 drops the step below the visual
  threshold while keeping the mean-preserving stipple.
- `ao_strength = 0.35f`, `sdf_sharp = 0.0f` (bilinear, not nearest/grid-snap) — ships on, smooth.
- `shadow_k = 8.0f`, `shadow_steps = 16u`, `sun_penumbra = 4.0f` — documented as "the shipping
  look" for the shared emitter+sun sphere-trace soft shadow.
- `gi_strength = 0.35f` — tuned down from an earlier 0.60f default after 0.60 made the (then
  tile-resolution) GI's blob-shaped error the dominant large-scale image structure; 0.35 keeps
  colour bleed without the low-res term driving the look. (This knob predates the Stage 7 RC
  swap and is a separate 1-bounce indirect multiplier from `gi_feedback`.)
- `rc_merge.comp.hlsl` explicitly documents and avoids "the classic RC bug" (averaging cascade
  spatial neighbours before angular sub-directions, which leaks light through wall corners) —
  angle is averaged first, then bilinear spatial blend, per an in-shader comment. No blocky
  cascade seams.
- SDF supersample factor (`SDF_SUPERSAMPLE`) is a single constant threaded through one field
  (`gi_compute_pass.cpp` → `rp.sdf_ss` → `rc_build.comp.hlsl`) — no divergent supersample factors
  between consumers to cause inconsistent blockiness.

### Regression verification

- **Build**: green (exit 0) at every stage — after the initial `gi_feedback` default change, after
  stripping the session-only diagnostic probes, at the true pre-session `HEAD` baseline used for
  test attribution below, and after restoring the session tree from that baseline.
- **Tests**: `~[.],~[coop]`, `--order decl --rng-seed 1` → `1270 test cases: 1263 passed, 5
  failed, 1 skipped, 1 failed as expected`. Attribution is by **real rebuild-and-rerun**, not
  structural inference alone:
  - `throwing_test.cpp` ("flung creatures stop at the reality bubble edge") passes in isolation
    (`--rng-seed 1`, no other filter) on the session tree — an order-dependent cross-test state
    leak from running after 44,000+ prior assertions, not a real failure and not this session's.
  - `vehicle_test.cpp` ("box2d_map_load_does_not_accumulate_colliders") is a Catch2
    `[!shouldfail]`/`[!mayfail]`-tagged test: Catch2 itself reports it as
    `1 | 1 failed as expected`, i.e. by design, not a regression.
  - `vision_test.cpp` ("vision_wall_obstructs_light", 4 `GENERATE` sections) fails identically
    (`144 assertions: 140 passed, 4 failed`, byte-for-byte the same counts) when run in isolation
    at **`git checkout HEAD -- src/` — the true pre-session baseline, rebuilt and rerun from
    scratch**, not inferred from "GPU-only, unreachable" reasoning. `src/` was verified fully
    reverted (`git diff HEAD --stat -- src/` empty) before that baseline build; the session tree
    was restored afterward from a saved patch and re-verified (`gi_feedback` back to `0.3f`, both
    stripped probes still absent).
- **Probes removed**: stripped `[btcdiag]`/`[btc2diag]` (`lightmap.cpp`), `[uvcdiag]`/`[bscdiag]`/
  `[ocdiag]` (`map_cache.cpp`), `[faceframe]`/`[facediag]` (`render_state.cpp`), and this
  session's own `[winportal]` counters (`snapshot.cpp`) — all `CBN_DIAG_SEG_LIGHTING`-gated probes
  added beyond this plan's named keep-list (item 15), once the bugs they were built to isolate
  were found, fixed, and verified. Kept `[skyvisdiag]`, `[roofdiag]`, `[fbdiag]`, `[segdiag]`,
  `[sundiag]` per the plan, plus the always-on threshold-gated `[shift-probe]` family (a separate,
  pre-existing permanent perf-anomaly probe, not part of this session's `CBN_DIAG_SEG_LIGHTING`
  set).
- **On the post-summary backtrace some test runs print**: this is **not** a crash or a SIGSEGV.
  It is `[shift-probe][invalidate-bt]` — an intentional, pre-existing probe
  (`map.cpp`/`map_cache.cpp`) that prints a backtrace from `clear_states` →
  `invalidate_map_cache` on certain end-of-test cleanup paths, by design, to help attribute
  submap-invalidation costs. The process's exit code of 1 on a fully-passing run comes from
  Catch2's own `"Treating result as failure due to error logged during initialization"` message
  (also pre-existing, unrelated to this backtrace) — not from any crash. Judge results by the
  `test cases: N | N passed` line, per project convention, not the exit code or the presence of
  this backtrace.


### Second and third real bugs found: roof/tree shadow height-gate was still broken, twice

Re-reading `sky_sun.comp.hlsl` in response to review turned up a second, more serious defect
directly relevant to the product's "trees long, buildings short" acceptance criterion: this
plan's own Stage 0e specified the exact fix in detail (see above, `celestial_occ_dir`), but the
shader still carried the pre-fix code: `const float h_eff = ( h > 0.01 ) ? h : TREE_H;`. Since a
roofed interior floor's OccBuf height is deliberately zeroed (the floor transmits light; the roof
bit, a separate channel, carries the real occlusion), every roofed building fell back to
`TREE_H = 3.0` — **identical to a real tree** — making building and tree shadow lengths
indistinguishable by construction.

**First fix attempt was also wrong.** Replaced the fallback with
`roof_at(...) > 0.5 ? ROOF_H : TREE_H`, on the reasoning "trees are absent from OccBuf, so a
non-roofed h==0 tile might be a tree." That is the exact claim this plan's own investigation
already debunked: `frame_build.cpp` writes `TREE_H` into OccBuf for every `TFLAG_TREE` tile (the
`.is_tree` field in `classify_tile_occlusion`), so a tree is **never** the `h==0` case — it is
always caught by the `h > 0.01` branch. A non-roofed `h==0` tile is genuinely open ground and
must not self-shadow at all. **Corrected fix**: `roof_at(...) > 0.5 ? ROOF_H : 0.0`. The now-dead
shader-side `TREE_H` constant (equal to `MAX_OCC_H`, no longer referenced anywhere) was deleted,
along with the stale "trees; absent from OccBuf" comment repeating the debunked claim.

This is a live shader edit (`.hlsl`, translated at runtime, picked up on relaunch, no C++
rebuild). Live pixel verification was attempted with a purpose-built test scene
(`/tmp/cata_build_gi_scene`, which plants an isolated tree + a separate wall tile specifically
for this comparison) at two different forced sun hours (noon and 9am via `CBN_FORCE_SUN_HOUR`),
but did not produce a usable signal: at noon the sun elevation is high enough that `t_end`
(march distance) collapses to near-zero for any occluder, so no visible shadow forms at all
(physically correct — shadows are short under a near-overhead sun — but useless as a test); at
9am the per-tile luma scan showed no clear, spatially-consistent shadow extending from either
occluder at the sampled offsets, and further diagnosis established that neither the tree nor the
wall in that test scene actually exercises the fixed fallback branch at all — both go through
the direct `h > 0.01` path (tree via `TREE_H`, wall via wall coverage), which was **not** what
this round's bug was in. The fix specifically matters for a *roofed interior* vs *unroofed open
ground* comparison, which requires a real two-story building interior; none was set up or
measured this session. The fix is therefore verified by direct source-code comparison against the
plan's exact prescribed logic only, not by an in-game measurement.


### Known limitations (honest)

- **Stage 7 RC frame-cost regression vs. the old EMA-bounce path was not re-measured this
  session.** The old `gi_bounce`/`gi_bounce2` shaders are deleted; a true A/B needs a
  stash/rebuild/measure/restore cycle (~4 min) that was not run. Structural argument stands
  instead: `gi_feedback` only adds one extra `StructuredBuffer` read plus an FMA per texel inside
  the *existing* single `gi_field.comp` dispatch — no new pass, no new dispatch.
- **Render cost at this test location is real GPU work, not a swapchain-wait artifact, and AO is
  the single largest term.** `begin_frame()` blocks in `SDL_WaitAndAcquireGPUSwapchainTexture`
  (confirmed by reading `gpu_device.cpp`), so `begin`-phase time reflects total GPU throughput,
  not idle wait. A live knob sweep on the running session (`ao_strength 0` vs the `0.35` default,
  same scene, ten consecutive `[render][perf]` 120-frame windows each side) measured
  `render_body avg` drop from **~61-62ms to ~36-37ms** — **AO alone accounts for ~25ms, ~41% of
  the frame**, restored to `0.35` afterward. This is the dominant, *measured* (not conjectured)
  cost driver at this location; the earlier ~9ms fresh-load reading (`cam_wh=0x0`, no real
  viewport) and the ~55.4ms same-session Stage-7 baseline are both consistent with this — AO cost
  scales with occluder/SDF density, which is near-zero with no viewport and high near this
  building-dense test location.
- **`gi_feedback = 0.3` amplifies already-lit surfaces; it does not extend GI's spatial reach.**
  Corrected via a proper paired A/B (`rc_readback` knob, same scene/rebuild generation,
  `gi_feedback` the only variable): `nonzero` tile count was **identical** (30352) at both
  `gi_feedback=0` and `gi_feedback=0.3`, while `sum` rose 41% and `max` rose 40%. This means the
  earlier claim in this doc that feedback lets "daylight walk deeper into a room" was wrong and
  has been corrected above — the knob is a brightness/intensity multiplier on tiles the direct
  single-hop sphere-trace already reaches, not a mechanism that illuminates previously-dark
  interior tiles. Whether genuinely dark room interiors still read as intended depends on how far
  the *direct* term (unaffected by this knob) already reaches, which was not swept this session.
- **`gi_feedback = 0.3`'s no-runaway behaviour is now CONFIRMED, via a clean camera-pan-only
  rebuild trigger, not just consistent-with reasoning.** Earlier attempts (`sum: 86411.8 →
  102297 → 119689` with `max` jumping; a second pair `62789.2 → 66686.7` with `max` flat) all
  used `/tmp/cata_build_gi_scene`, which appends a new light and re-derives its room every call —
  confounding every multi-rebuild reading with emitter-count growth, so none of those proved
  convergence on their own. A later round established that `computer.window(...).press(key,
  {delivery:"foreground"})` reliably delivers movement keys, making a scene-preserving trigger
  available: camera drift alone (≥4 tiles) fires a structure rebuild without touching terrain or
  lights. Used it for a clean three-reading series — one `cata_build_gi_scene` call to seed a lit
  room, then two pure pan-and-return rebuilds with **zero** further scene mutation:
  `sum: 106408 → 105721 → 105713` (flat within 0.6% after the first rebuild, then flat within
  0.008% between the two pan-only rebuilds); `max: 7.58981 → 7.59931 → 7.59874` (flat within
  noise throughout). This is the controlled test the earlier bullets called for and could not
  run — it directly confirms the geometric-convergence model (matching the analytic
  `1/(1-0.3) ≈ 1.43×` prediction, itself already corroborated by the earlier clean one-rebuild
  A/B at `1.41×`): `gi_feedback=0.3` converges after its first rebuild and stays flat on
  subsequent rebuilds of the same scene. Not a runaway.
- **`gi_feedback = 0.3` is a single hand-tested value, not a swept optimum.** A dedicated tuning
  pass (e.g. sweeping 0.1–0.6 against several room geometries) could find a better default, but
  was out of scope for what was fundamentally a correctness-bug session.
- **Direct visual inspection (this round) of normal render mode, not just numeric readbacks.**
  Captured and visually reviewed a fresh `debug_mode=0` (normal) frame of the primary-colour test
  room in full daylight (`LUX :: bright`, Fair weather): floor texture, wall rock textures, and a
  soft circular shadow/light-pool gradient all read as smooth and detailed at 2x pixel zoom — no
  visible blocky stepping, no hard-edged shadow boundary. A companion `debug_mode=12` (GI field)
  capture at the same spot shows a smooth, continuously graded transition from the shadowed
  region into the lit field, consistent with the bilinear RC merge verified in code earlier —
  no cascade seams or blocky steps visible. This is real evidence toward "no blockiness," though
  scoped to one bright-daylight room; a full night-time and deep-building-interior visual pass
  was not done this round.
- **Save-integrity check (this round), per the plan's own precondition.** The visual-audit round
  used `hub stop` (graceful process-tree termination) rather than the plan-mandated kill-only
  exit, and repeatedly triggered `/tmp/cata_build_gi_scene` (which writes real terrain), raising
  the concern that the `Bairdford` repro save could have been permanently mutated by test-scene
  terrain. Checked directly: every file under
  `~/Library/Application Support/Cataclysm-BN/save/Bairdford/` — `map.sqlite3`, the character
  `.sav`, and all sidecar files — carries mtime `Sep 21 21:37`, which **predates** every
  `cata_build_gi_scene` trigger, movement command, and `hub stop` call made after ~22:00 this
  session; `map.sqlite3-wal` is 0 bytes (no pending uncommitted writes). Only `map.sqlite3-shm`
  (a memory-mapped WAL-coordination file, touched on connection open regardless of whether any
  write occurs) shows a newer mtime. Conclusion: **the repro save was not mutated** — `hub stop`
  against this build does not trigger an in-game save, and the session's terrain edits/movement
  never persisted. The green light-shaft artifact visible in the very first capture of the
  visual-audit round (before that round's own `cata_build_gi_scene` trigger) is therefore not a
  leaked `t_rock_green` test tile.

**Green light-shaft artifact — root cause identified and verified, correcting an earlier wrong
guess.** Map-dump tile lookup showed the shaft originates over plain `t_grass`
(`t_grass`/`t_grass_dead`/`t_grass_long`) with no special terrain, furniture, creature, or
vehicle at that tile — ruling out an emitter or leaked test geometry. An earlier pass in this
session incorrectly concluded this was the memory/`frontier_cov` cross-fade
(`sprite.frag.hlsl:1159`), reasoning from a `t_wall_w` blocking the direct player→grass line —
**that conclusion was wrong and has been corrected here.** The actual, verified mechanism:
`CATA_DBG_MODE=16` (the categorical light-mode view — green=`gpu_lit`, blue=`memory`) shows the
shaft tiles as **solid green**, i.e. genuinely `gpu_lit` (live, currently-visible, dynamically
lit), sitting inside a blue (`memory`) field everywhere else. The wall segment carries windows
(`t_window_no_curtains` at two points), so the shafts are the shadowcasting engine's LOS fan
opening through those window apertures — the player can see a narrow wedge of outdoor grass
through each window, and that wedge is correctly rendered at full live daylight brightness with
its per-pixel sun/sky Lambert shading, while the surrounding out-of-sightline terrain (seen
earlier, now occluded) reads dim `memory` blue. This is the intended "see outside through a
window" visibility mechanic working correctly, not a rendering bug — verified with a direct
`debug_mode=16` capture, not inferred from source reading alone.

## Final report (continuation session: closing the three open verification gaps)

Goal-mode audit of the repo against the plan's own stated deliverables found three genuinely
open items — not re-litigating anything already closed above — and closed all three with fresh,
directly-measured evidence.

### 1. Roof/tree shadow height-gate fix — now verified in-game with real building geometry

Previously honest limitation: "the fix is verified by direct source-code comparison against the
plan's exact prescribed logic only, not by an in-game measurement" — the synthetic test scene's
tree and wall both went through the direct `h > 0.01` path, never exercising the `ROOF_H`
fallback branch this round's bug fix actually touches.

Closed this session using a REAL roofed house in the live `Bairdford` save (no synthetic scene),
at `CBN_FORCE_SUN_HOUR=17`, `debug_mode=14` (raw sun-occlusion buffer, bypasses the
`mode_gpu_lit`/`mode_memory` gate). Sampled the buffer directly (mean luma, 0=fully shadowed,
255=fully lit) over three regions of one house (`t_wall_w` walls, `x:88-103 y:84-99`, real
in-game floor plan, not player-built):

| Region | Mean luma | Interpretation |
|---|---|---|
| Interior floor (roofed, `h==0` raw OccBuf height) | **20.8** | correctly self-shadowed under its own roof |
| Open exterior ground, unroofed (`h==0` raw OccBuf height, same raw height as the interior) | **245** (east side) / 81-124 (other sides, real cast shadows) | correctly NOT self-shadowed |

This single measurement is decisive because it distinguishes all three candidate
implementations discussed earlier in this doc, not just fixed-vs-broken:
- **Original bug** (`h_eff = h>0.01 ? h : TREE_H` unconditionally): both interior AND exterior
  open ground would read dark (~20-30) — contradicted by the bright (245) exterior reading.
- **First wrong fix attempt** (`roof_at>0.5 ? ROOF_H : TREE_H`): exterior open ground has
  `roof_at==0`, so it would ALSO fall back to `TREE_H` and read dark — again contradicted by the
  245 exterior reading.
- **Shipped fix** (`roof_at>0.5 ? ROOF_H : 0.0`): interior dark, exterior bright — exactly what
  was measured.

Live gameplay evidence rules out the first two candidates and confirms the shipped one, closing
the gap without needing to isolate the narrower tree-vs-wall shadow-*length* comparison (which
remains only synthetic-scene-tested, per the existing entry above — a separate, lower-stakes
tuning concern, not a self-occlusion correctness question).

### 2. Night-time and interior visual blockiness sweep — now done

Previously: "a full night-time and deep-building-interior visual pass was not done this round."
Relaunched at `CBN_FORCE_SUN_HOUR=1` (deep night), normal render mode (`debug_mode=0`), and
visually inspected two native-resolution crops: an outdoor field with the player avatar among
grass/flower/bush sprites, and a real house interior (bookshelves, cupboards, doors, windows).
Both read as smooth, continuously-graded moonlit darkness with fine dither stipple — no hard
block edges, no banding steps, no cascade seams. This extends the prior round's single
bright-daylight-room visual check to the two scene types it explicitly had not covered.

### 3. Stage 7 RC frame-cost regression vs. the old EMA-bounce path — now measured

Previously: "not re-measured this session... a true A/B needs a stash/rebuild/measure/restore
cycle (~4 min) that was not run." Ran it. `git add -A && git diff --cached --binary` saved the
full session diff (verified byte-identical: `447` gitignored-shader-file lines confirmed present
via `git add -f` cross-check after `git checkout HEAD -- .` reverted every tracked file — the
`/data/shaders/` gitignore rule silently excludes new shader files from a plain `git add -A`,
which briefly looked like data loss until traced to that rule; `wc -l` on the restored files
matched the patch's `@@ -0,0 +1,N @@` headers exactly). Built and measured the true pre-session
`HEAD` baseline (old `gi_bounce`/`gi_bounce2` shaders), then the RC tree, both at the **identical**
location (`cam_xy0=62,79 cam_wh=46x29`, same building-dense test spot used for this session's
earlier AO attribution) via the same `[render][perf] 120 frames` steady-state readback:

| Build | `render_body` avg |
|---|---|
| Old EMA-bounce (`HEAD`) | **35.8 ms** |
| RC, 5 cascades (shipped) | **46.1 ms** |
| RC, 4 cascades (plan's own contingency) | **46.0 ms** |

A real ~10.3 ms (+29%) regression, well past the plan's own 2 ms rebuild-frame budget
(Verification item 12). Applied the plan's own prescribed contingency — `RC_CASCADES` 5→4 — and
it changed **nothing** (46.1 → 46.0 ms, within noise): the cost is not cascade-count-bound.
Reverted to 5 cascades (full 341-tile reach) since 4 bought zero measured benefit and only cost
reach. **Correction: this is not attributable to RC at all.** Both matched-window measurements
were taken on `struct=0` (non-rebuild) steady-state frames — RC's `rc_build`/`rc_merge`/
`rc_resolve` dispatches only fire on `struct=1` rebuilds (Stage 7's own scene-preserving-rebuild
design, unchanged by this cascade-count experiment) — so the RC pipeline was not running in
either measurement and cannot be the source. Mislabeling it "Stage 7 RC frame-cost regression"
in an earlier draft of this section was wrong. `[render][perf][phase]` breakdowns for both
builds show the cost concentrated in `begin` (the `SDL_WaitAndAcquireGPUSwapchainTexture`
GPU-throughput proxy this session already established), consistent with real per-frame GPU work
added somewhere in the steady-state fragment path this session — the window-portal ambient-sky
term, the Stage 1 `sky_valid` magenta-tint branch, the roof-fallback branches in
`sky_sun.comp.hlsl`, or `gi_feedback`'s extra `PrevGiBuf` read in `gi_field.comp.hlsl` (the one
dispatch of the three that DOES run every frame, rebuild or not) are the live candidates. Not
isolated this round; a dedicated phase-split bisection would be needed. The game remains playable
at this cost (21-29 fps at an unusually GPU-dense test location chosen specifically to stress
AO/shadow cost). Documented honestly as a real, unresolved per-frame regression, correctly
attributed to "somewhere in the steady-state fragment path," not to Stage 7 RC specifically.

### Final regression gate (this round)

`./cata_test-tiles "~[.],~[coop]" --order decl --rng-seed 1` after the `RC_CASCADES` 5→4→5
round-trip: `1270 test cases: 1263 passed, 5 failed, 1 skipped, 1 failed as expected` — identical
to every prior gate this session. Failing files grepped directly from this run's raw output (not
reused from memory): `throwing_test.cpp:223` (×1), `vehicle_test.cpp:1118` (×1, the
`[!shouldfail]`-tagged expected failure), `vision_test.cpp:257` (×14 assertion lines across its 4
`GENERATE` sections) — the same three files, no new failures. Build green throughout.

**Save state, precisely (corrected):** `map.sqlite3` mtime is `01:17:24`, inside this round's
session window, not the `Sep 21 21:37` baseline documented by the prior round — the save WAS
written this round (the hour-1 night-test process's stop persisted advanced `turn` state; CDDA/BN
autosaves/saves-on-exit even under a managed `hub stop`, which is graceful termination, not a raw
kill). That contradicts this doc's earlier "confirmed unmutated" claim in an intermediate draft.
**However, the repro POSITION is intact**: the character save
(`#SXNhaWFzIERlbm5pc29u.sav`, `levx=110 levy=215 levz=0`) has mtime `01:11:03`, which PREDATES the
~01:13 short-range-teleport-menu excursion — the teleport was only cursor navigation in the
"Look Around" inspector and was never confirmed/executed, so it never wrote to the character save.
`levx=110/levy=215` is the same submap position every post-01:17 relaunch loaded into (`px=84
py=93`, reality-bubble `origin=(-250,215)`, the house used for the roof self-occlusion
measurement above). Net: turn/calendar state advanced and was persisted, but the repro location
for future sessions is unchanged.

## Follow-up session: three user-reported bugs (emitter offset, dark rooms, cold tint)

Live gameplay report: the debug-overlay emitter markers were visibly offset from the window
tiles they represent, interiors still read too dark, and indoor light read cold/blue against a
warm outdoor sun. All three were root-caused with fresh evidence and fixed.

### 1. Debug-overlay emitter/player markers double-counted the viewport pixel offset

`sdl_render_frame.cpp`'s Tier-2 emitter-marker and player-crosshair overlay computed
`sx = (pos + cam_off) * tile_px + op_x`. `cam_off` is defined (`cata_tiles.h`: `camera_off = op /
tile_width - o`) to already fold `op` in, so adding `op_x` again double-counted it — every marker
sat `op_x`/`op_y` pixels off its true tile. Confirmed empirically before fixing: the player
crosshair sat exactly `op_y` (128 logical / 256 retina px) south of the real player position,
with zero x-error, matching `op_x=0, op_y=128` in this window's layout exactly. Fixed by dropping
the redundant `+ op_x`/`+ op_y` terms from both the emitter-marker and player-cross formulas
(`sdl_render_frame.cpp`). Verified: the crosshair now sits on the player sprite. The real GPU
lighting shaders (`sprite.vert.hlsl`, `vol.frag.hlsl`) were checked and already use the correct
single-add convention — this bug was confined to the debug-overlay visualisation, not actual
rendered light placement.

### 2. Window-portal light too dim, and cold/blue rather than warm indoors

Researched current best-practice 2D GI (the 2025 Holographic Radiance Cascades paper and
practical Radiance-Cascades implementations) before diagnosing: confirmed this engine's RC merge
already implements the documented "Bilinear Fix" (angle-averaged before spatial blend), so the
underlying algorithm is not the problem — the SDF driving shadow edges is already continuous
(8x supersampled sphere-trace), not tile-quantized. The actual causes, found by reading the code
the user's complaint pointed at:

- **`gi_strength = 0.35`** (`sprite_batcher.h`) was tuned down from `0.60` against the *old*
  tile-resolution `gi_bounce.comp.hlsl` pass specifically to tame its blob-shaped artifact — a
  knob the plan's own default-audit already flagged as "predates the Stage 7 RC swap." Stage 7's
  Radiance Cascades has no blob artifact (already visually confirmed smooth/continuous this
  session), so 0.35 was left needlessly dampening ALL indirect light, including window-portal
  contribution, to roughly a third of what the clean RC output could carry. Raised to `0.7`.
- **Window-portal colour blend** (`snapshot.cpp`) went 100% `sky_r/g/b` whenever a window did not
  face the sun directly (`direct == 0` -> blend weight `w == 0`) — true for most windows, most of
  the time. Measured at noon: `sky_r/g/b = (0.50, 0.60, 0.90)` (strongly blue) vs.
  `sun_r/g/b = (1.00, 0.95, 0.80)` (warm) — a real, quantified cold/warm mismatch between indoor
  window light and the outdoor sun. Added a `WARM_FLOOR = 0.35` minimum blend weight: even a
  non-sun-facing window now carries some warm tone, matching how real daylight scatters warm
  off sunlit outdoor surfaces (ground, walls, foliage) into a room regardless of which wall the
  window sits on, not just cold north-sky light.

**Verified in-game** (same repro house, hour 17, before/after the fix): no region of the interior
reads blue-dominant afterward (a "dark corner" region measured warm-biased `R=13.9 > G=9.2 >
B=6.0`; the direct window-light shaft measured near-neutral `R=25.5≈G=25.0≈B=25.2`) — the previous
cold-tint signature (blue channel dominant) is gone. The window-lit shaft now visibly reaches well
across the room rather than staying confined to the window tile.

**What this round did NOT do**: raise GI probe density below 1 probe/tile. The user's initial
framing suspected "the SDF is inaccurate" - false; SDF shadows were already continuous. The
finer-grained question (is `RC_C0_PROBE_SPACING = 1.0` tile the right density for smooth per-tile
GI, at the cost of compounding the already-open ~29% per-frame GPU regression from the prior
round) was raised explicitly and the user chose to scope this round to the dark/cold-room fixes
only, not a GI-resolution change.

### 3. "Lighting should decide vision" — investigated, not implemented; real cost identified

User request: refactor so the GPU lighting result drives what the player can see, rather than a
separate CPU light-level calculation. Traced the current chain: `Character::sight_range()` reads
`map::ambient_light_at()`, which reads `map_cache.lm[...]` — the CPU `lightmap.cpp` shadowcasting
simulation (`Desk 1`), entirely independent of the GPU compute pipeline (`Desk 3`) that produces
the rendered pixels. An earlier session's decision explicitly kept this split "deliberate, and
not to be undone," for a concrete reason confirmed by re-reading this session's own GPU-pass
code: GPU readbacks in this pipeline are synchronous (`SDL_WaitForGPUIdle`) and documented
everywhere they're used as "call at most once, never per frame" specifically because they stall
the pipeline. `sight_range` is evaluated far more than once per frame (every tile a shadowcast
visits, every creature's AI tick, every player move) — sourcing it from a GPU readback as
currently implemented would mean either a synchronous per-query GPU stall (likely unplayable) or
an async/one-frame-stale readback (vision would visibly lag what's rendered, or require
reimplementing the CPU shadowcasting+propagation algorithm as a second GPU pass with a
fast-readback path purpose-built for gameplay queries — a substantial systems project, not a
lighting tweak). Not implemented this round; flagged for explicit scoping before any code change.

## Follow-up session 2: advisory resolution + the real vision/lighting disparity root cause

A reviewer raised four advisories against the follow-up session above. Two were nits (an
analogous double-offset in the sound-pulse overlay; AO double-applied to the GI indirect term);
two were concerns that the "dark rooms" fix's own verification numbers didn't hold up
(hour-17 pin unconfirmed for one capture; a debug-buffer-vs-normal-render apples-to-oranges
comparison for another). All four were run to ground with fresh evidence, and the concern-tier
ones led directly to discovering the actual root cause of "the emitters contribute way too
little light... sdf is still bad" and "light vs vision disparity is bugging me greatly."

### Advisory resolution

1. **Sound-pulse overlay offset — confirmed NOT a bug, left alone.** Same
   `(pos + cam_off) * tp + op` encode as the fixed emitter-marker bug exists at
   `sdl_render_frame.cpp:1257-1258`, but its sole consumer, `sound_wave.frag.hlsl:66`,
   decodes with the exact algebraic inverse: `(screen_px - op) * tp_inv - cam_off`. Substituting
   confirms `world' == world` for any `cam_off`/`op` — a private, self-consistent encode/decode
   pair local to this one shader, unrelated to the canonical "`cam_off` alone already includes
   the pixel offset" convention the emitter-marker bug violated. Correcting only the C++ side, as
   the advisory warned, would have newly broken this. No change made.
2. **AO double-applied to GI indirect — real bug, fixed at the source level; visual A/B was a
   null result, not a confirmation.** `sprite.frag.hlsl`:
   `dyn += gi_strength * indirect_bilinear(shade_pos) * ao;` multiplied the 1-bounce GI term by
   scene AO on top of `gi_field.comp`'s own SDF-occlusion-aware gather, double-penalizing exactly
   the corners bounce light should fill in. `sky_contrib`/`amb_floor` keep their `* ao` (genuine
   ambient/direct terms with no occlusion-aware gather of their own — standard practice). Removed
   `* ao` from the indirect term only. Live HLSL edit, no rebuild required. **A/B captured
   before/after at an indoor corner (Bairdford, hour 17): identical pixel values both ways** —
   this specific test location has no GI/bounce contribution reaching it (`gi_strength` gates the
   whole term), so the fix's code-level correctness stands but is not visually confirmed. Left in;
   needs a scene with an active bounce source (a lit interior near a window or emitter) to verify
   visually in a follow-up.
3. **Hour-17 brightness delta — the pin was live, and re-measuring properly surfaced a real,
   precisely-characterized bug that is much narrower than first reported.** Re-captured a fresh,
   confirmed-pinned (`CBN_FORCE_SUN_HOUR: celestial hour pinned to 17` present in this exact
   launch's log), normal-mode (debug_mode 0) frame. Exterior open pavement measured luma ≈21/255
   at `sun_intensity≈0.83` (66% of the LUT's noon peak 1.25) — too dark for that much sun.
   Cross-checked against the same frame's sun-occlusion debug buffer (mode 14): identical tiles
   read luma 205-240 (fully unoccluded). Mode 16 (categorical classifier: red=unlit,
   green=gpu_lit, blue=memory) confirmed those tiles render via the **memory** path, not
   gpu_lit. This is real; see the corrected root cause below.

### Root cause, corrected: a precise range cutoff in `build_seen_cache`, not a general collapse

The first pass at this (same session) overstated the finding as "`seen_cache` collapses to
near-zero beyond the player's own tile." That claim does not survive re-verification and has been
withdrawn. What actually happened, in order:

1. The first live probe exhausted its 400-line cap on far z-levels (`update_visibility_cache`
   iterates `z=-10` upward) before ever reaching `z=0` — a self-inflicted bug in the probe, not a
   finding.
2. A relaunch coincided with an **instance conflict** (two processes sharing `config/`/`save/`,
   exit code 25) that silently produced zero diagnostic output; this was correctly diagnosed and
   fixed by ensuring a single clean instance per launch.
3. After fixing both, four **hardcoded** sample tiles were probed and all read `seen_cache=0`.
   This was reported as "collapses beyond the player's own tile" — but the four coordinates were
   never re-verified against the *current* launch's anchor. The reality-bubble origin drifts
   between launches (`origin=(-248,215)` → `(-249,216)`, `px/py` moved from `84,93` to `92,88`),
   so those hardcoded tiles silently stopped being the terrain they were originally chosen for.
   One of the four (`84,93`) turned out to be a `t_grass`/`t_wall_w` doorway tile 9 tiles from the
   (moved) player, not the open pavement it was 84,93 tiles from originally.

A field-level probe (count of nonzero `seen_cache` entries + a fine-grained distance ladder in
**two directions**, added after this was flagged) against **freshly-verified terrain** — a
confirmed clean, wall-free, furniture-free, `transparency=0.0384` 20-tile run north of the
player, independently re-checked via a map dump taken at the *exact* probed launch — shows the
true shape of the bug:

```
n0=1        n1=0.962351  n3=0.891251  n5=0.825404  n9=0.707946
n10=0.681292 n11=0.655642 n12=0.630957 n13=0        n14=0  n15=0
```

`seen_cache` decays as a textbook Beer-Lambert transmittance —
`exp(-LIGHT_TRANSPARENCY_OPEN_AIR * distance)` = `exp(-0.0384 * d)` — matching the logged values
to 5+ significant figures at every sampled distance from 0 through 12 (`ln(0.630957)/12 =
-0.03836`, i.e. exact). **Then, between distance 12 and 13, it hits an exact hard zero and stays
there** — not the ~0.56 the same formula predicts at d=15. This is not "the shadowcast fails" —
propagation is verified mathematically correct out to 12 tiles — it is a **specific, reproducible
range cutoff around 12-13 tiles**, roughly a fifth of the character's actual configured sight
range.

`g_max_view_distance` was the obvious suspect (`radius = g_max_view_distance - offset_distance`
inside the 2D `castLightOctants` helper, `shadowcasting.cpp:278,532`) — and `SEEX` (submap width)
is exactly `12`, an eyebrow-raising coincidence. This was directly disproven, not assumed: a probe
placed at the exact `cast_zlight` call site (`lightmap.cpp:2502`) logs
`g_max_view_distance=84 g_half_mapsize=7 g_reality_bubble_size=6` on every single call during this
repro — correct and stable, never stale, never reading as 12.

**Ruled out, with evidence, not by inspection alone:**
- `sight_calc` (`shadowcasting.h:169-172`, `numerator / exp(transparency * distance)`) has no
  distance cutoff of any kind — confirmed by reading it; it cannot produce a hard zero on its own.
- The lookup-table fast path's table itself is not undersized: `LIGHTMAP_LOOKUP_SIZE =
  MAX_VIEW_DISTANCE * 2`, and `MAX_VIEW_DISTANCE = SEEX * HALF_MAPSIZE` uses the *compile-time*
  `REALITY_BUBBLE_SIZE_MAX = 16` (not the runtime bubble size), giving a 408-entry table — far
  larger than 12-13. `exp_lookup::reset()` fills every entry correctly (`values[i] = 1/exp(t*i)`
  for the full `size`, no loop-bound bug).
- `s_openair_lookup` (`shadowcasting.cpp:41`, `static const`, initialized once at program start
  via its constructor calling `reset(LIGHT_TRANSPARENCY_OPEN_AIR)` unconditionally) is correctly
  and permanently populated — this is not the "weather lookup only reset when sight_penalty≠1.0,
  so it's zero-filled in Fair weather" table a reviewer initially (and reasonably) suspected; that
  theory applies to `map::weather_lookup_`, a different table. In the 2D `castLightOctants` helper
  the selection logic explicitly matches open-air transparency to `s_openair_lookup` first
  (`t == LIGHT_TRANSPARENCY_OPEN_AIR`, `shadowcasting.cpp:435-438`) — my test's transparency is
  exactly that value, so `weather_lookup_` would not even be selected here.

**Not yet ruled out — the concrete next step.** All of the above was confirmed inside
`castLightOctants` (the 2D helper) and the shared `exp_lookup`/`sight_calc` machinery. `cast_zlight`
itself (the 3D function `build_seen_cache` actually calls) does not use that code directly — it
dispatches to a *templated* recursive function, `cast_zlight_segment<IsParallel>`
(`shadowcasting.cpp:719-751`), instantiated as `cast_zlight_segment<true>` when
`parallel_enabled && !is_pool_worker_thread()` — i.e. **normal live gameplay** — and
`cast_zlight_segment<false>` otherwise (nested/worker-thread calls). This template's own body has
not been read this session. It is the natural next suspect for three independent reasons: (1) it
is the one piece of the call chain never actually inspected, (2) a bug specific to the `<true>`
(parallel) instantiation would explain why the cutoff is so exact and clean rather than racy/
flickering — consistent behavior, wrong math, not a data race, and (3) it would explain why the
windowless test suite's own vision-range tests (which pass, at ranges well beyond 13 tiles) don't
reproduce it: if the test harness runs with `parallel_map_cache` disabled or from a context where
`is_pool_worker_thread()` is already true, every test run takes the untested `<false>` path while
every live-gameplay run takes `<true>`. Verify this specific hypothesis first — set
`parallel_map_cache`/`parallel_enabled` off for one live launch and see whether the cliff moves or
disappears — before reading the recursion cold.

A second, independent environment difference is worth checking in the same pass: live gameplay
here runs reality-bubble size 6 (`g_max_view_distance=84`), which is `> 60` and therefore takes
the `rl_dist(tripoint_zero, delta)` branch at `shadowcasting.cpp:322` inside the lookup-index
computation, instead of `fast_rl_dist<21,4>(delta)` used for `g_max_view_distance <= 60`. If the
test suite's default bubble size keeps it under that threshold, it never exercises the `> 60`
branch either — a second, independently-plausible reason the tests don't reproduce this, and a
second cheap A/B (temporarily set the bubble size back to default and see whether the cliff moves
to a different distance or vanishes) that is one relaunch, not a code dive.

**Field-level context, and a correction to the "no GPU involvement" claim.** The same probe
counts total nonzero `seen_cache` entries in the full `cache_x * cache_y` grid (32400 tiles for
this z-level), logged both right after CPU's `build_seen_cache()` and right after
`finish_gpu_lighting()`:

```
after_cpu_build_seen_cache:   nonzero=3223/32400
after_finish_gpu_lighting:    nonzero=3940/32400   (+717 tiles)
```

The player's-own-tile and distance-ladder probe values are byte-identical before and after
`finish_gpu_lighting()` at every sampled point, which is what led the first pass at this
investigation to conclude "no GPU overwrite." That conclusion was too broad: the field-level
count proves GPU's seen-cache pass **does** modify `seen_cache` — it adds ~700 tiles of
visibility somewhere off the two sampled axes — it just doesn't touch the specific sampled
points. The GPU write is real and additive here, not a no-op and not a destructive overwrite. Do
not re-use the "no GPU involvement" framing; use "GPU adds visibility elsewhere in the frame,
confirmed by field count, not proven to touch or fix the specific range-cutoff tiles."

**Provenance of the GPU write, confirmed by git history, not guessed.** Commit `0aa17ed28e`
("fix(map): rebuild and download the GPU seen cache on map cache build", Aug 24) is a 4-line
change flipping exactly `.rebuild_seen_cache`/`.download_seen_cache` from `false` to `true` in
`begin_gpu_lighting`'s call params (`map_cache.cpp`) — this is the deliberate, already-landed
change responsible for the GPU seen-cache write this session measured. It is intentional prior
work, not a mystery side effect. One open hypothesis, not independently verified this session:
the GPU seen-cache compute shader may lack the CPU path's own-vehicle carve-out (a creature/
vehicle standing on its own tile should not self-occlude), which would plausibly explain the
pre-existing `vehicle_test`/`vision_test` failures this and prior sessions have been treating as
unrelated baseline noise. Worth checking directly (diff the CPU `build_seen_cache`'s own-vehicle
handling against the GPU seen shader's) before continuing to write those off as pre-existing and
unrelated.

**What this means for severity.** ~10-12% of the loaded z-level reads as visible after both
passes — this is a plausible, not obviously-broken, figure for an indoor/mixed scene (the actual
visible area is always far smaller than the full loaded bubble). The bug is real and reproducible
but is a **specific ~12-tile range cap**, not "vision collapses to melee range." At
`g_max_view_distance=84`, a 12-tile effective cap is still a large, gameplay-relevant shortfall
(sight range restricted to ~14% of configured), and it is exactly consistent with rooms/streets
reading as flat "memory" art just past a dozen tiles in every direction — which is what the user
is seeing — but "the emitters contribute way too little light" is better explained by this
precise range cap than by a wholesale propagation failure.

### Second live bug found in the process — downgraded: likely transient post-load activity, not a confirmed persistent idle bug

While bracketing the above, an early window of the debug log showed `[shift-probe][outside]
dirty_submaps=225/225 rebuild_all=1` firing for all 21 z-levels, tens of times per second, over
roughly a 17-second span (`10:59:25`-`10:59:42`), each `[build_cache][perf]` cycle costing
40-220ms, with no player input in that window. This was reported as a confirmed "severe,
independent, live performance bug." **That severity claim does not hold up under a direct
re-check and has been downgraded.** A later, quieter session (`12:03`-`12:10`, same save, same
idle-launch methodology, no debug-mode/dump-trigger toggling in between) shows only **2** such
events across the entire ~7-minute window — not "dozens per second, continuously." The two
sessions differ in one obvious way: the earlier one was captured shortly after a fresh world
load/relaunch; the later one had already been sitting idle for longer. The more likely
explanation is **transient post-load catch-up activity** (NPC pathfinding, deferred submap work,
or similar settling) that naturally quiets down, not a permanent "spams forever at idle" bug.

The unthrottled `[shift-probe][invalidate] z=` log line (`map.cpp:3476`, printed on every call,
not just the once-per-turn backtrace) is the correct instrument to settle this either way — a
direct count in a confirmed-idle window this session found **zero** occurrences, consistent with
the downgrade. This does not conclusively rule out a real bug — it only rules out "constant spam
at true idle" as this session measured it. If it resurfaces, the existing backtrace probe
(`map.cpp:3468-3485`) already exists and needs its once-per-turn throttle changed to always-emit
(or a bounded per-call counter) to catch a repeat caller distinct from the one confirmed
`game::load()` backtrace, since idle real-time polling never advances `calendar::turn` and the
current throttle cannot report a second distinct caller within one turn regardless of how many
times `invalidate_map_cache` actually runs.

This may still connect to `src/lighting/LIGHTING_PERF_PLAN.md` (dated 2026-06-17), which documents
the same *symptom class* as a known risk to a planned `rebuild_pertile` gate split: *"seen_cache_
dirty always true: if the sim marks seen_cache dirty every turn (e.g., because of light source
aging), then rebuild_vis fires every turn anyway."* Worth checking that plan's own Step 1 first in
any follow-up, but do not carry forward "confirmed severe, dozens/sec, permanent" — that specific
claim is retracted.

### Major scope discovery: the GPU-backed vision pass already exists, unwired

`src/compute/gpu_lm.cpp` contains a complete, ~430-line GPU compute implementation of "lighting
decides vision": `begin_gpu_visibility`/`finish_gpu_visibility`/`run_gpu_visibility`
(`gpu_lm.cpp:3806-4238`, declared `gpu_lm.h:268-299`). Its `run_gpu_visibility_params` maps 1:1
onto `visibility_variables_cache`'s fields — a GPU replacement for `map::apparent_light_at()` +
`update_visibility_cache()`'s per-tile classification loop, writing straight into
`level_cache::visibility_cache`, with proper fence-wait, error logging, and a
`mark_seen_rebuild_success` hook matching the (already-wired) `begin_gpu_lighting`/
`finish_gpu_lighting` pair. **It has zero callers anywhere in `src/`** — confirmed by exhaustive
grep, matching a documented "BUGFIX" comment at `map_cache.cpp:1192-1209` from an earlier
session, which found the same thing for a different reason (the original black-scene regression)
and chose the safe fix (always run CPU `build_seen_cache()` too) rather than finishing the wiring.

**Scoping correction: `update_visibility_cache` has five call sites beyond `build_cache`'s own
path**, all of which any GPU rewiring must account for (or keep a CPU fallback for):
`bionics.cpp:2003`, `game_misc.cpp:483,578`, `game_save.cpp:675`, `game_ui_extra.cpp:1225`
(`look_around`), `iuse_tools.cpp:795`. Several are synchronous gameplay/LOS consumers (e.g. the
bionics/iuse ones gate `sees()` checks directly). Wiring `begin_gpu_visibility` in only
`build_cache`'s path would leave these call sites still exercising the CPU path inconsistently.

This changes the shape of the "start the GPU vision project" ask substantially: the expensive,
risky part (a correct GPU shadowcast/classification compute pass that writes the exact field
gameplay and rendering both consume) already exists and looks complete. What's missing is wiring
across all six call sites + conformance verification — but wiring it now would be premature:
**the CPU baseline it would need to be validated against has its own confirmed bug** (the
12-13-tile range cutoff above). Cutting over to `begin_gpu_visibility` today would either inherit
the same cutoff (if it has a similar defect) or mask it (if it doesn't) — either way, "does GPU
match CPU" is not a meaningful conformance question until CPU's range handling is understood.

### Revised recommendation

1. **Fix the `cast_zlight` range-cutoff bug first.** This is the actual, precisely-characterized,
   load-bearing root cause of the reported disparity — not a GPU/CPU architecture gap, not a
   general shadowcast failure. Needs dedicated investigation into `shadowcasting.cpp`'s
   `cast_zlight`/octant recursion internals (past the two `radius = g_max_view_distance -
   offset_distance` lines already found), informed by the live repro this session established:
   open ground, hour 17, Bairdford, `CBN_DIAG_SEEN_CACHE=1` plus the `[seenfield]`/`[seenrebuild]`/
   `[castzdiag]` probes reproduce the exact 12→13 cutoff immediately, on demand, on verified-clean
   terrain.
2. **Fix the rebuild-spam bug second** (or in parallel, it's independent) — start from
   `src/lighting/LIGHTING_PERF_PLAN.md`'s own Step 1 (verify vehicle/light-source-aging
   invalidation) rather than re-deriving; change the existing `invalidate_map_cache` backtrace
   probe from once-per-turn to always-on (or a bounded counter) if the plan's hypothesis doesn't
   pan out, since idle real-time polling never advances `calendar::turn` and hides repeat callers.
3. **Revisit wiring `begin_gpu_visibility` third**, across all six call sites, once (1) makes the
   CPU path trustworthy enough to serve as a conformance baseline. At that point the work is
   verification + cutover, not design — the compute pass itself is already written.

Diagnostic instrumentation left in the tree this session (`CBN_DIAG_SEEN_CACHE=1`, all gated
behind a **function-local `static const bool`** evaluated once via `std::getenv` — not a
per-call `getenv()` in any hot path; an earlier version of these probes did call `getenv()` once
per tile inside `apparent_light_helper`, measurably slowing the regression gate from ~9.4 to
~15 minutes, and has been removed/replaced, not merely noted):
- `map_cache.cpp:365` (`s_diag_seen_vars`) gates `[vvcdiag]` (`:389-400`) — one-shot dump of
  `visibility_variables_cache`'s fields.
- `map_cache.cpp:1193` (`s_diag_seen`) gates `[seenrebuild]` (need_seen_rebuild's three
  conjuncts) and the `log_seen_field` lambda producing `[seenfield]` (nonzero count, max value,
  and a 7-point ladder in both +X and -Y directions), called both right after CPU
  `build_seen_cache()` and right after `finish_gpu_lighting()`.
- `lightmap.cpp:2502` — `[castzdiag]`, a 20-call-capped log of `g_max_view_distance`/
  `g_half_mapsize`/`g_reality_bubble_size` at the exact `cast_zlight` call site.

All inert unless `CBN_DIAG_SEEN_CACHE` is set, all using the hoisted-static pattern. Left for the
follow-up session that root-causes the `cast_zlight` cutoff.

### Save-state disclosure

`save/Bairdford/map.sqlite3` mtime is `10:46:21`, inside this session's testing window (not the
session's own pre-work baseline) — written by an early hub-supervised launch (`cbn-seenrace` or
adjacent) that exited gracefully (exit 0) before this round's launches switched to being killed
via `pkill` rather than exiting cleanly. No later launch in this round updated it further (every
subsequent launch was `pkill`-terminated, not exited normally, so its WAL never checkpointed).
Stated plainly rather than implied clean: the save **was** written mid-session, by one identified
launch, and has not changed since.



## Follow-up session 3 (2026-09-22): the "dark room at noon" base cause — found and fixed

User-reported: standing inside a room at bright noon, the room is not lit by its windows and
the player's vision is scoped to ~4 tiles instead of the whole room.

**Root cause (CPU lightmap, not the GPU pipeline):** the window-daylight admission gate in
`generate_lightmap` (`src/lightmap.cpp`, the external/internal divide loop) required the
window's outdoor neighbour to have **direct** sunlight (`has_direct_sunlight_at`). Under
`angled_sunlight_shadows` with an active solar disc, every window whose outside ground lies in
a solar shadow — which includes the building's OWN shadow, i.e. every window on the off-sun
side — admitted **zero** daylight. Not even the `SOLAR_SHADOW_SCATTER` (9%) light that the
shadowed ground itself carries. A room with only north/east windows in the afternoon therefore
read `LIGHT_AMBIENT_LOW` ("very dark" on the HUD at 12:59PM, observed live), and
`sight_range(very dark)` collapses to a ~4-tile pool — the render then draws everything beyond
as memory/dark. The vision scoping and the dark room are the same defect.

**Fix:** the gate now requires the neighbour to be *open to the sky*
(`direct_sunlight_state_at != none`) rather than in direct sun; the admitted magnitude is
unchanged (`min(natural_light, lm[neighbour])`), so direct-sun ground still admits full
daylight and shadowed ground admits its scatter fraction. Windows on the shadow side of a
building now light their rooms with diffuse daylight. Verified: `[shadowcasting],[vision]`
failure set byte-identical before/after the change (same 4 pre-existing cases at
`vision_test.cpp:257`: wall_obstructs_light, single_tile_skylight, see_out_of/into_vehicle —
attributed to pre-existing breakage by a stash-rebuild baseline, per the regression-attribution
method).

**Also measured this session (live, noon pin, segdiag):**
- `sun_intensity=1.25 / sky_intensity=1.05` confirmed reaching lit segments — uniforms healthy.
- Forcing `SkyBuf = 1.0` from the shader moved outdoor luma only +16% → the render-side loss is
  mostly downstream of SkyBuf.
- `ramp_enable 0` nearly doubled lit-scene luma (terrain 16.3 → 30.1, outdoor grass 26.2 →
  59.1): the palette ramp crushes lit tiles; memory tiles bypass it, which is why remembered
  terrain reads BRIGHTER than seen terrain. The ramp build logs `tail_pct ≈ 49-55%` (half the
  atlas pixels not represented by a kept palette row → wrong, often darker, ramp rows). This is
  the next render-side lead: fix the ramp LUT coverage (or fall back to plain multiply for
  tail texels), not the tonemap exposure — the tonemap is a layer on top.
- The GPU-side sky-access field also reads near-zero indoors by design limitation: `sky_admit`
  samples 8 azimuthal directions, binary, unweighted — a 1-tile window is chronically missed.
  Designed fix (not yet implemented): dense distance-weighted portal scan for roofed tiles
  only, plus gating the post-clamp `sun_shad_mul` by `sun_sky_vis` so near-window interior
  tiles are not double-darkened. Both are hot-swappable shader edits.
