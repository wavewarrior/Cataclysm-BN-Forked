# Lighting composition: move to the standard pipeline

## STATUS (implemented 2026-08-03)

All phases DONE, including the Phase 3 retune check. Commits:

| commit | what |
|---|---|
| `d438dc71e0` | Phases 0+1 — `tile_light_mode.h`, a global lightmap-ready latch, a required `mode` on `enqueue_tile_sprite`, `sprite_instance` 80 → 96 B. Also fixed `shadow.vert.hlsl`, which declared a truncated 64-byte `SpriteInstance` and so indexed the shared instance buffer with the wrong stride. |
| `4c90f96885` | Phase 2 — the composite. `max()` deleted, radiance selected by mode, palette-ramp and debug gates migrated, memory implemented as the `frontier_cov` cross-fade. |
| `368122c9f0` | Made debug view 16 genuinely categorical (alpha + post-effect gating); F7 now logs the mode it lands on. |
| `4df928b3ec` | `tools/light_mode_check.py`, `tools/frontier_profile.py`, three `vv` scenarios. |
| `bd861e4b0d`, `20a03a820a` | Two crashes that had to be fixed to have any regression gate at all. Neither is lighting — see Collateral. |

### One deviation from the spec, and the measurement that forced it

The plan assigned the main menu to `unlit`. That would have silently deleted the
decorative menu emitter's glow, which `max(tint, gpu_total)` had been carrying. A
capture settled it instead of an argument: away from the emitter the backdrop reads
`rgb[10.0, 10.0, 15.0]` (the blue base), while the top-left reads
`rgb[16.0, 15.2, 18.2]` with std 25.1 against 0.39 elsewhere — a warm, localised lift
matching the emitter's own `r=1 g=0.55 b=0.15`. So a lane was genuinely needed.

`flash` — the three floats Phase 1 had added as padding — carries `colour x strength`
with `max(colour) == 1`, composited as `radiance * (1 - strength) + flash`, i.e. a
hue-preserving `lerp(radiance, colour, strength)`. The melee hit-flash moved onto the
same lane. Both rejected alternatives are recorded in `sprite_batcher.h`: a purely
additive emissive clips to white in daylight and destroys the hue that says WHAT was
hit (additive gives `(1.0, 1.0, 1.0)` where the lerp gives `(1.0, 0.4, 0.4)`), while
folding the flash into `tint` makes it multiplicative, so it vanishes at night — the
only place the old `max()` flash was ever visible.

### Verified, and how

- **Classification** — `classify_tile_light` is pure, with a Catch2 table at 9/9,
  including the regression case: a visible PITCH-DARK tile must be `gpu_lit`.
- **The HLSL compiles** — it is compiled at RUNTIME from the install, so no C++ build
  proves it parses. `lightmode-menu.vv` launches the game to the main menu with zero
  shader failures in `debug.log`.
- **Nothing takes the fail-bright path** — debug view 16 reads `unlit 0.00%` in
  daylight AND at night (`gpu_lit` 78–89%, `memory` 1.5–4.3%), at both 1920x1080 and
  2560x1440. The floor that keeps bloom fringe out of the class counts could have
  turned a dim `unlit` tile into a false 0.00%, so the dim band is audited by hue:
  night reads `268269 px, unlit 0, gpu_lit 216319, memory 0` — no red-dominant pixel
  at ANY brightness. Night is the decisive case, since that is where the old
  `max(1.0, gpu_total)` would have rendered dark-but-visible tiles at full albedo.
- **Phase 1 IS inert — but only after a bug fix it exposed.** Running the gate I had
  skipped found that the wire commit `d438dc71e0` did not render AT ALL: `nointerpolation`
  on the `light_mode` varying kills D3D12 graphics-pipeline creation whenever the fragment
  shader does not consume the value (`0x80070057`), so the sprite batcher never
  initialised and the whole world was black behind a working RmlUi HUD. Measured against
  a pre-change build at the same resolution and tile pitch: **75.17%** of pixels differed
  as committed, **0.04%** with the qualifier removed — the documented cross-launch null.
  Phase 2 had accidentally repaired it by starting to read the value, so HEAD was correct
  but one edit away from a black screen. Fixed in `b93bd74575`. The whole change moves
  **2.41%** of daylight pixels against that 0.04% null.
- **The frontier cross-fade is NOT verified.** An earlier claim here that it "gains no
  rim" was WRONG and has been withdrawn. `frontier_profile.py` profiled along a screen
  axis, and the real boundary in both captures is HORIZONTAL (normal −89.7°), so the
  x-profile ran along the boundary and never crossed it; the plateaus previously quoted
  (14.5 / 88.3 and 29.6 / 19.7) came from a stray-pixel bug in the crossing search and
  were fiction. The tool now profiles along the measured local normal and is confined to
  the map viewport, and on that basis BOTH captures FAIL — a non-monotonic step and a
  large overshoot above the visible plateau (+63.4 daylight, +38.3 night). Two caveats it
  reports itself mean this is not yet a verdict on the shader: the far plateau sits within
  one blur radius of the viewport edge, and the frontier is one straight line spanning
  only ~12 tiles. What is needed is a scene with a large seen/remembered boundary well
  inside the frame; see "Still open".
- **Debug cycling is non-destructive** — the 1 → 0 → 1 triplet restores the scene
  inside the paired same-state null (restore 286 px vs null 364 px).
- **Phase 3 (retune): no retune needed.** The measurable risk was that genuinely dark
  visible tiles, no longer failing bright, would crush the scene. Measured at 01:00 in
  a real explored save: luma 27.96, std 20.34, range 0–220, histogram
  `[63.0, 33.5, 1.9, 1.2, 0.3, 0, 0, 0]` — dark, but structured and with real
  highlights, not crushed (a crushed frame would be ~99% in bin 0 with std ~2). The
  clock change is proven, not assumed: `day → night` moved 80.92% of pixels against a
  0.01% paired null. `night_floor` / `day_floor` / `mem_dim` therefore stay as shipped.
- **Suite** — 981 cases, 953 passed, 25 failed, 3 failed-as-expected. The 25 were
  attributed by stash+rebuild at clean HEAD, fail identically there, and are
  vehicle / ramp / furniture-grab / stomach / filesystem tests, none of which render.

### Still open

**The frontier cross-fade needs a proper test scene.** Everything else here is measured.
This one is not, and the tool now says so instead of quietly passing. The save used has a
single straight, viewport-edge-hugging frontier ~12 tiles long, which cannot support a
plateau estimate. Whoever picks this up should build a scene with a large remembered
region well inside the frame — walk a character through a multi-room building, then step
back so a whole room drops out of line of sight — and re-run
`tools/frontier_profile.py --viewport 0.25,0.12,0.55,0.80`. Until then, treat the memory
branch's mid-band behaviour as unverified. Both ENDPOINTS are sound by construction
(`frontier_cov` 0 gives the visible result exactly, 1 gives the remembered look exactly);
it is only the shape in between that is unmeasured.

### Collateral — two crashes fixed to get a regression gate at all

Neither is a lighting defect; both aborted the Catch2 suite, so without them this work
had no regression signal.

- `bd861e4b0d` — `map::update_visibility_cache` sized the `rl_dist` lookup table from
  `cache_x/cache_y`, which bounds `|x - player_x|` only while the player is INSIDE the
  bubble. After a reality-bubble resize the avatar can sit outside it, and `index_3d`
  bounds-checks nothing. Bit-reproducible at `--rng-seed=2699219069`; it still faulted
  with zero worker threads, which ruled out the concurrent-realloc theory.
- `20a03a820a` — the `none` vproto has an empty part list, so `on_vehicle_added` fired
  with no parts and handed Box2D a zero-vertex polygon, which `__debugbreak`s on MSVC.

## Premise

`sprite.frag.hlsl:782` composites two independent lighting solutions with a
per-channel maximum:

```hlsl
const float3 combined = max(mem_tint, gpu_total);
```

Nobody does this. The standard pipeline computes radiance **once**, multiplies it
onto albedo, and treats visibility as a separate **mask**:

```
final = albedo x tint(colour) x radiance(selected by mode)
```

Light accumulates additively into a high-precision buffer, then multiplies
albedo; fog-of-war / FOV is a multiplicative mask or its own target. `min`/`max`
appear only for visibility masking, never as the main composite. That holds
across deferred 3D and 2D lightmap pipelines alike - including Stoneshard, which
`cata_tiles.cpp:1941` explicitly names as the quality target.

### What the code actually does today

`cata_tiles.cpp:1932-1944` only ever emits two tint values, so the `max` is a
*selector*, not a blend:

| CPU lightmap at tile | tint | `max(tint, gpu_total)` | effect |
|---|---|---|---|
| `lum > 0.001` | 0.0 | `gpu_total` | GPU is the sole brightness source (intended) |
| `lum <= 0.001` | 1.0 | `1.0` | **full unlit albedo, GPU discarded** |
| UI / menu / overmap / memory | 1.0 | `1.0` | passthrough (intended) |

So on a lit tile the CPU lightmap contributes nothing to brightness - it is a
one-bit gate. Two defects follow:

1. **The fallback fails BRIGHT.** Its comment says the branch means "lightmap not
   generated yet", but the condition equally matches a genuinely pitch-dark tile.
   There the GPU result - including any shadow - is thrown away and the tile
   renders at full albedo. Shadows are structurally unrepresentable exactly where
   they should be deepest. The threshold is a hard binary at 0.001, so a tile
   crossing it pops rather than ramps.
2. **`tint` is overloaded.** One channel carries colour, the lighting-mode
   selector, an "is this a game tile" flag, and alpha. `sprite.frag.hlsl:524-525`
   asserts the tint is combined "ADDITIVELY ... not suppressed by max()" while
   line 782 uses `max()` - the same comment-versus-code drift that produced the
   shadow-march bug fixed in `bbfda9cd0f`.

## Evidence

- Tint rule: `src/cata_tiles.cpp:1932-1944`.
- Composite: `data/shaders/lighting/src/sprite.frag.hlsl:780-782`.
- Contradictory comment: `sprite.frag.hlsl:524-525`.
- Memory tiles also carry `tint = 1.0`, so they bypass GPU lighting entirely.
  Measured on captures from this session: a Lua-fabricated patch read mean
  saturation 0.074 / lit-fraction 0.977 / luma 40.4, versus 0.144-0.182 / 0.33-0.49
  / 15.7-20.9 for terrain the character had actually seen. That is the
  `mem_desat = 0.70` signature - the fabricated scene was almost entirely memory
  tiles.
- The GPU path IS live on real tiles: the shadow-march change moved **3.48%** of
  pixels against a **0.05%** cross-launch null in a real town at night.

## Constraints discovered (these shape the design)

- **`tint_*` cannot be re-meant.** It is a field of the shared `sprite_instance`
  and means three different things across four vertex shaders:
  `sprite.vert` = lighting selector, `splat_stamp.vert` = target channel mask
  (1,0,0 blood / 0,1,0 wet / 0,0,1 snow), `rain_droplet.vert` = per-instance
  colour, `shadow.vert` = colour. A new channel is required.
- **Blast radius is narrow anyway.** Only `sprite.vert.hlsl` declares the full
  80-byte `sprite_instance`; rain/splat/shadow declare a separate 64-byte
  `quad_instance` (`rain_effect.cpp:19`, `splatmap_pass.cpp:28`). Growing
  `sprite_instance` touches exactly one shader.
- **No free lane.** `sprite_instance` is 80 bytes (`sprite_batcher.h:78`);
  `pad1` = foliage sway, `pad2` = outline. Both in use.
- `src/lighting/CLAUDE.md:165` claims `sizeof(sprite_instance)==64`. Stale - fix
  while here.
- **`enqueue_tile_sprite` (`cata_tiles.h:236`) has ~15 defaulted positional
  parameters.** A defaulted new parameter would let every missed call site
  silently inherit a default and fail as a wrong-looking pixel - the exact
  failure mode this codebase cannot currently detect (see Verification). The mode
  must be REQUIRED so the compiler enumerates the work.

## The six `tint` consumers in sprite.frag

Every one is a migration item. Item 4 is the sharp edge.

| # | Line | Use | Disposition |
|---|---|---|---|
| 1 | 407 | outline early-return colour | keep (real colour) |
| 2 | 780 | memory passthrough `mem_tint` | replaced by `memory` mode |
| 3 | 782 | the `max()` composite | deleted |
| 4 | 815-818 | palette-ramp gate `step(tint_sum, 0.01)` | **must move in the same commit** |
| 5 | 829, 877 | debug-mode gate `dbg_tint_sum < 0.01` | becomes `mode == gpu_lit` |
| 6 | 1006 | final alpha `i.tint.a` | keep (legitimate) |

Item 4: `tint_sum < 0.01` is currently a proxy for "this is a lit world tile".
The moment tint stops being 1.0 on UI and memory sprites, that test becomes
**always true** and the palette ramp starts eating HUD glyphs. It cannot lag
behind the composite change.

## Target design

```cpp
enum class sprite_light_mode : int { unlit = 0, gpu_lit = 1, memory = 2 };
```

| mode | composite | used by |
|---|---|---|
| `unlit` | `albedo x tint` | UI, fonts, overmap, main menu, rain, overlays, world-not-ready |
| `gpu_lit` | `albedo x tint x gpu_total` | world tiles the player can see |
| `memory` | `lerp(gpu_lit_result, memory_result, frontier_cov)` | remembered terrain; **still needs the radiance term** - see below |

`max()` is deleted. The `lum > 0.001` test is deleted. "World not ready" becomes
an explicit flag rather than a per-tile brightness race.

### The memory branch MUST keep `gpu_total` (Step 8 frontier cross-fade)

The obvious spec - "memory never reads `gpu_total`" - is wrong and would
reintroduce the exact artefact Step 8 was built to remove.

`frontier_cov` (`sprite.frag.hlsl:435-467`) is 1.0 deep inside a remembered
region and falls to 0 at the edge shared with a currently-visible tile. It
feathers THREE things today:

| line | feathered term |
|---|---|
| 489 | the greyscale texel treatment |
| 780 | the tint passthrough, via `mem_tint = i.tint.rgb * frontier_cov` |
| 845 | the memory dim, via `lerp(1.0, mem_mul, frontier_cov)` |

Line 780 is fed into the `max()` on purpose: as `frontier_cov -> 0` the tint goes
to 0, so `max(0, gpu_total)` yields `gpu_total` and the remembered tile renders
*exactly as its visible neighbour does*. The comment at 774-779 says so outright -
"the two terms together are a true cross-fade between the lit and remembered
looks".

A memory branch that never reads `gpu_total` has nothing to cross-fade toward, so
the feather ramps to black and the seen/remembered boundary comes back as a dark
rim. Hence the mode is a lerp, not an independent branch:

```
memory: lerp(gpu_lit_result, memory_result, frontier_cov)
```

`frontier_cov == 1` (deep inside) -> pure memory look; `frontier_cov == 0` (at the
edge) -> bit-identical to the visible neighbour. This is the one place the new
design must NOT simplify the branch structure.

Endpoint parity is NOT full parity: today's `max(frontier_cov, gpu_total)` and
this lerp agree at `frontier_cov` 0 and 1 but differ in between, so the frontier
band changes on purpose. See the Risks entry before treating "no visual diff" as
the gate there.

## Phases

### Phase 0 - make the work compiler-visible (no behaviour change)

- Add a `tile_sprite_options` struct (AGENTS.md mandates an options struct past
  3 parameters; there is precedent in this same header -
  `occluder_footprint_options`, `cata_tiles.h:949`).
- New signature: `enqueue_tile_sprite(sprite_light_mode mode, const tile_sprite_options &opts)`.
  `mode` is a required leading positional. **Do NOT add a transitional overload**
  - if you do, the compile errors never appear and the migration cannot be
  verified.
  (A non-defaulted *struct member* is not sufficient: omitting it in a designated
  initializer value-initialises to 0 rather than erroring.)
- Build. Every call site is now an error, across `cata_tiles.cpp`,
  `cata_tiles_anim.cpp`, `animation.cpp`, `lighting/gpu_geometry.cpp`,
  `lighting/rain_effect.cpp`, `lighting/render_state.cpp`,
  `lighting/solid_overlay.cpp`, `lighting/splatmap_pass.cpp`. That error list IS
  the migration checklist.
- Give each site an explicit mode. Non-world sprites are `unlit`.
- **Gate:** `cataclysm-bn-tiles` builds clean. Zero rendering required.

### Phase 1 - carry the mode to the shader (still no behaviour change)

- Grow `sprite_instance` 80 -> 96 bytes: `float light_mode; float lm_pad0, lm_pad1, lm_pad2;`.
  Update the `static_assert`.
- Mirror the field in `sprite.vert.hlsl`'s `SpriteInstance`; pass through to the
  fragment stage as a new `TEXCOORD`.
- Fix the stale `sizeof` claim in `src/lighting/CLAUDE.md:165`.
- The fragment shader still runs the old `max()` path. The mode is carried but
  unread.
- **Gate:** rendering is unchanged. This is the one step where a same-state
  capture is sufficient evidence - expect a diff at or below the capture null.

### Phase 2 - replace the composite (the actual fix)

- `sprite.frag.hlsl`: replace 780-782 with an explicit three-branch select on
  `light_mode`.
- Migrate consumers 4 and 5 to `mode == gpu_lit`.
- `cata_tiles.cpp`: delete the `lum > 0.001` gate. Classification becomes:
  `MEMORIZED` -> `memory`; UI / overmap / `as_independent_entity` / world-not-ready
  -> `unlit`; everything else -> `gpu_lit`.
- Add an explicit `world_lighting_ready` flag for pre-first-lightmap frames.
- Implement `memory` as the `frontier_cov` cross-fade, NOT an independent branch,
  and capture the reference frontier luma profile BEFORE changing the composite.
- **Gate:** the Catch2 classification table (below), the categorical debug view,
  AND the monotonic frontier-ramp check (see Risks) - the categorical view shows
  class, not brightness, so it cannot catch a dark rim on its own.

### Phase 3 - retune

Contrast will change, because genuinely dark visible tiles stop rendering at full
albedo. Re-check `night_floor` / `day_floor` (0.02 / 0.05) and `mem_dim`, and
re-eyeball `POINT_K_GAIN` (see Open item).

## Verification that does NOT depend on the in-game A/B harness

This matters because the harness was proven unreliable this session:

- Zoom keypresses sent back to back get dropped, so identical-shader runs land on
  different zoom levels - measured tile pitch 8.02 px vs 42.86 px, a **19.7 luma**
  swing, which manufactured a 67% "signal" out of nothing.
- The window is windowed-borderless on a virtual display whose resolution changes
  between launches (1920x1080 vs 2560x1440).
- Lua-fabricated terrain renders as **memory** tiles, which bypass GPU lighting
  entirely - so a scene built that way cannot test this change at all.

So the plan leans on methods that work before any pixel is compared:

1. **Compile-error enumeration (Phase 0).** The migration is provably complete
   when it builds. No rendering.
2. **Catch2 unit test of the classification.** The fail-bright defect lives in
   C++, not HLSL. Extract classification into a NEW header of pure functions
   (AGENTS.md: do not modify headers with >10 usages), e.g.
   `src/tile_light_mode.h`:
   `auto classify_tile_light(const tile_light_query &q) -> sprite_light_mode;`
   Table cases, tagged `[lighting]`:
   - `MEMORIZED` -> `memory`
   - UI / overmap / independent entity -> `unlit`
   - visible and lit -> `gpu_lit`
   - **visible and pitch dark -> `gpu_lit`** (this is the regression the old code
     got wrong; it must NOT be `unlit`)
   - world lighting not ready -> `unlit`
3. **Categorical debug view for the HLSL half.** Add a `debug_mode` that outputs
   `light_mode` as a flat colour (unlit red / gpu_lit green / memory blue).
   Categorical hues are immune to the luma and zoom bistability - you check class
   membership, not luminance. One capture answers "is every tile classified as
   intended".
4. **Gate every pixel comparison.** `tools/shadow_zoom_check.py` must report
   matching resolution AND matching tile pitch before any diff; the scene must
   assert `hour` / `is_night` via `gapi.current_turn()` (see
   `tools/visual_verify/scenes/shadowtest.lua`).
5. **Only measure terrain the character has actually seen.** Never Lua-rewritten
   unseen tiles.

## Risks

- Touches every pixel in the game. The Phase 1 / Phase 2 split exists so the wire
  change is provably inert before behaviour moves.
- Consumer 4 (palette-ramp gate) silently inverts if it lags the composite.
- **The frontier band WILL change, intentionally - do not use "no visual diff" as
  its acceptance criterion.** Today the memory composite is
  `max(frontier_cov, gpu_total)` (tint is 1.0 on memory tiles), i.e. brighter
  wins. The specced `lerp(gpu_lit_result, memory_result, frontier_cov)` agrees at
  BOTH endpoints but not in between: at `frontier_cov = 0.5` with
  `gpu_total = 0.9`, today yields 0.9 while the lerp yields the mean of the two
  looks. The lerp is the better formulation - a genuine monotonic cross-fade
  rather than a content-dependent "brighter wins" band - but it is a real change
  in that band and must be declared, not discovered. If pixel parity there turns
  out to matter, the fallback is `max(memory_result, gpu_lit_result)` scaled by
  `frontier_cov`, which preserves current output at the cost of keeping a `max`
  inside the memory branch.
- **The frontier cross-fade needs its own gate.** The categorical debug view shows
  CLASS, not brightness, so it cannot catch a dark rim. Gate with `debug_mode 15`
  (the existing vision-frontier view, `sprite.frag.hlsl:973-987`, which already
  encodes `frontier_cov` in GREEN) plus a luma profile sampled ACROSS a
  seen/remembered boundary: the ramp must be **monotonic** between the two
  plateaus - no dip below the remembered side, no overshoot above the visible
  side. Capture the reference profile BEFORE Phase 2.
- Memory tiles otherwise derive their look from `tint = 1.0` plus `mem_dim` and
  `mem_desat`. Phase 2 must reproduce that exactly or remembered terrain shifts
  brightness.
- Deleting the `lum` gate means genuinely dark visible tiles render dark instead
  of full albedo. That is the intended fix, but it is a visible change: if any
  content relies on the fail-bright to stay readable, it surfaces here.

## Open item - not part of this plan

`POINT_K_GAIN = 4.0` (`sprite.frag.hlsl`, commit `bbfda9cd0f`) is an
**art-direction constant chosen from an offline sweep, not by anyone looking at
the game.** Offline it saturates shadow tightness at the SDF's 1/8-tile
resolution limit and leaves unoccluded ground at exactly 1.000; in game it was
checked once at wide zoom (0.27% signal, global luma -0.7%). Every other in-game
measurement this session turned out to be zoom or resolution artifact.

If shadows read too hard or too soft in normal play, **that single number is the
dial**. Someone should play the game and judge it before this plan starts, since
composition changes will alter apparent contrast and confound the judgement
afterwards.
