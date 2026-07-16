# Rain effect — post-mortem & learnings

Written for the agent that first implemented the high-fidelity rain feature
(`src/lighting/rain_effect.*`, the `rain_*.hlsl` shaders, the
`sdl_render_frame.cpp` wiring). The feature shipped "complete" but rendered
**nothing visible**, and half of it could never have worked as designed. This
documents exactly why, so the same mistakes don't recur.

The architecture was sound. Every defect below is a *detail* bug — and each one
was individually sufficient to make the feature look broken. That is the lesson:
"all the pieces are present and it compiles" is not "it works."

---

## Bug 1 — Unsigned negation made every droplet spawn off-screen (THE invisibility bug)

```cpp
// rain_effect.cpp, spawn_droplets()
const float y = static_cast<float>( rng( -screen_h / 4, 0 ) );   // WRONG
```

`screen_h` is `std::uint32_t`. `-screen_h` does **not** give a negative number —
it wraps to ~4.29e9. `/4` → ~1.07e9. So the call was `rng(1073741644, 0)`.
`rng(lo, hi)` swaps when `lo > hi`, so spawn Y landed uniformly in
`[0, ~1.07e9]` — i.e. ~1e9 pixels below the screen. `update_droplets` culls
anything with `y > screen_h + 20` **the same frame it spawns**. Net result:
≈0 droplets ever existed in the visible band. The streaks were never drawn.

**Fix:** compute in signed space.
```cpp
const float y = static_cast<float>( rng( -static_cast<int>( screen_h ) / 4, 0 ) );
```

**Lesson:** any arithmetic that negates or subtracts past zero on an unsigned
type is a landmine. Window/texture dimensions are routinely `uint32_t`/`size_t`.
Cast to a signed type *before* negating or subtracting. A compiler warning
(`-Wsign-conversion`) would have caught this; treat those warnings as errors in
math that can go negative.

---

## Bug 2 — Premultiplied-alpha output vs `SRC_ALPHA` blend (double-dimmed)

The droplet fragment returns **premultiplied** alpha:
```hlsl
return float4( col * alpha, alpha );
```
but the pipeline blended with `src_color_blendfactor = SRC_ALPHA`:
```
out = (col*alpha)*alpha + dst*(1-alpha)   // alpha applied TWICE → col*alpha^2
```
Light-blue streaks at ~0.4 alpha, squared, over an HDR target, then tonemapped,
came out essentially invisible even on the frames they did draw.

**Fix:** premultiplied output requires `src_color = ONE` (and `src_alpha = ONE`),
keeping `dst = ONE_MINUS_SRC_ALPHA`. Either output premultiplied + `ONE`, or
output straight `(col, alpha)` + `SRC_ALPHA` — but the frag and the pipeline must
agree. They didn't.

**Lesson:** the blend factor is half of the contract; the shader's output
encoding is the other half. Decide premultiplied-vs-straight once and make both
sides match. Write it in a comment next to both.

---

## Bug 3 (the big one) — Ground wetness computed in SCREEN space

The original "wet-spot accumulation" used a 512² ping-pong **splat map** indexed
in screen space, plus splash positions stored as raw screen pixels. This was
wrong at the concept level, three ways over:

1. **It was never wired to anything.** `record()` wrote `splat_a_/splat_b_` and
   swapped them — but nothing ever sampled those textures back into the scene.
   The entire wet-spot subsystem rendered to an off-screen texture no pass read.
   Dead output. (It also burned a full GPU pass every frame for nothing.)

2. **Coordinate-space mismatch.** Splashes wrote raw screen-pixel coords
   (e.g. x=1900) but the splat pass ran a fullscreen tri over a 512²-viewport
   target, so `i.pos.xy` ran 0..512. A splash at pixel 1900 matched no fragment.
   Even if wired, it drew nothing.

3. **It would have rained indoors and smeared on scroll.** A screen-space
   accumulator has no notion of sky exposure (→ wet sheen on roofed interior
   floors) and is pinned to the screen while the world scrolls underneath
   (→ wet patches slide across the terrain as you walk). This entire codebase is
   **world-locked on purpose** (world-locked Bayer dither, `world_pos` mirror in
   the shaders, sub-tile SDF registration). A screen-space ground effect is
   fundamentally inconsistent with it.

**Fix / redesign:** the accumulator was dropped entirely. Impact splashes are now
short-lived **world-positioned rings**: spawned at map tiles, **sky-gated at
spawn** (`level_cache::outside_cache`, the same source `frame_build` uses for
SkyVis), and **projected to screen each frame** with the exact same
`(world_tile + camera_off) * tile_pixel_size` formula the sprite vertex shader
uses. They expand and fade over ~12 frames, drawn directly onto `world_target`.
No ping-pong, no second pass, no indoor rain, no scroll-smear.

**Lessons:**
- **Match the coordinate space of the thing you're decorating.** Ground is a
  world-space property → compute it in world space. Falling droplets/on-lens
  effects are screen-space → those stay screen-space (the droplets correctly do).
  Mixing them is the root error, and it produced *two* separate visible failures
  (indoor rain + scroll-smear).
- **Reuse the project's existing projection, don't reinvent it.** The
  `camera_off`/`tile_pixel_size` mirror of `sprite.vert` already existed (see
  `assemble_light_inputs` and the `CLAUDE.md` "coordinate system" note, including
  the documented `op/tile_width − o` footgun). Use it; don't roll a new mapping.
- **A persistent screen-space accumulator is the "accumulation texture"
  future-work item** in `project_rendering_pipeline.md` — it needs reprojection
  on scroll to be correct. Don't casually introduce one as a side feature.

---

## Bug 4 — HLSL `cbuffer` array stride vs tightly-packed C++ upload

The splat shader declared `float splash_x[512]` ×3. In an HLSL constant buffer,
**every array element is padded to a 16-byte (float4) stride** — so each array is
8 KB (24 KB total). The C++ side pushed only `splash_x` at `sizeof(splash_x)` =
2 KB tightly packed, and never uploaded `splash_y`/`splash_intensity` at all. The
shader would have read garbage. (Mooted by deleting the splat path, but it was
broken.)

**Lesson:** never `memcpy`/push a tightly-packed C++ `float[]` into an HLSL
`cbuffer float[]`. Either pack as `float4[]` (which matches the 16-byte stride
1:1), or use a `StructuredBuffer`/storage buffer (tight layout, what the rest of
this engine uses for per-element data — see the emitter/SDF buffers). The new
splash path uses an instance storage buffer, the same proven mechanism as the
sprite batcher, precisely to avoid this class of bug.

---

## Meta-lesson — "complete" was claimed without ever running it

The original plan file ended with **"Status: COMPLETE — all files written and
integrated,"** with compilation/visual verification listed as *remaining* work.
But:

- Bug 1 alone meant zero droplets ever rendered.
- The splat output was wired to nothing.

Neither could have survived a single in-game glance. The feature was declared
done at "the code exists and reads plausibly," not at "I saw it work."

**How to actually close the loop here (cheap, this session used all of it):**

1. **The empty log is not evidence of anything until you check the binary.**
   The running binary predated the rain code — proven with
   `strings <binary> | grep rain_effect` → 0 matches. A "no log output" result
   was a stale-binary artifact, not a logic finding. Always confirm the symbol
   you expect is actually in the binary you're running before reading its logs.

2. **Build once, then read the fresh log against named anchors.** `init()` logs
   `rain_effect: initialised` only after *both* pipelines + buffers succeed →
   that line alone confirms `ready()`. `record()` logs `droplets=N splashes=M`.
   Those are your "did it actually run / did particles exist" instruments — add
   them deliberately, not as an afterthought.

3. **Know where your code is even reached.** Rain records inside
   `render_world_pass_w`, which early-returns when there are no tile sprites — so
   it does *not* run at the text main menu, only once terrain is drawn. Don't
   conclude "broken" from a menu-only smoke test; the path is in-game.

4. **Default to the codebase's invariants.** When unsure whether an effect should
   be screen- or world-locked, look at what everything else does. Here,
   *everything* is world-locked. Matching that would have skipped Bug 3 entirely.

---

## Quick reference — files

| File | Role |
|---|---|
| `src/lighting/rain_effect.{h,cpp}` | Particle pools, GPU pipelines, per-frame record |
| `data/shaders/lighting/src/rain_droplet.vert.hlsl` | Procedural quad vert (shared by droplets + splash rings) |
| `data/shaders/lighting/src/rain_droplet.frag.hlsl` | Streak gradient (premultiplied) |
| `data/shaders/lighting/src/rain_splash.frag.hlsl` | Expanding-ring impact (premultiplied) |
| `src/sdl_render_frame.cpp` (`render_world_pass_w`) | Camera params + sky-gated world splash spawns + `record()` |

Spawn gate uses `map::access_cache(z).outside_cache` (x-major `[tx*map_h + ty]`,
matching `frame_build`). Projection mirrors `sprite.vert`:
`screen_px = (world_tile + camera_off) * tile_pixel_size`.

---

## Addendum — the wet-ground "splat map", rebuilt world-locked

The screen-space splat map (Bug 3) was later rebuilt as a **persistent
world-locked puddle grid** (`wetness_pass.{h,cpp}` + `wet.frag.hlsl`): a CPU
per-tile float grid (x-major `wet[x*map_h+y]`, same layout as SDF/SkyVis) that
fades each frame, accumulates rain hits (fed from the same sky-gated spawn loop,
so wetness inherits the sky gate — no indoor puddles), **shifts to track the
reality bubble** (`begin_frame` reprojects by `abs_sub*SEE` delta so puddles
stay glued to absolute ground — the thing screen-space couldn't do), uploads as
a storage buffer, and a fullscreen pass darkens + cool-sheens the ground
(world_pos reconstructed exactly like `vol.frag`). Two more lessons fell out:

- **A plausible default that's quantitatively too weak is the same bug as the
  unsigned-negation one — it just fails silently instead of loudly.** First-cut
  knobs were `accum=0.06`, `decay=0.992`. Do the arithmetic: ~11 hits/frame over
  ~2000 visible tiles = ~0.005 hits/tile/frame, so a tile is re-hit only every
  ~190 frames — but `decay=0.992` has a ~86-frame half-life, so each hit decays
  to nothing *before* the next one lands. Steady-state wetness ≈ 0.04, darken ≈
  0.018: invisible. The fix is a constraint, not a taste: **decay half-life must
  exceed the per-tile re-hit interval** or accumulation never builds. Always
  sanity-check accumulation defaults against the hit-rate-vs-decay math; "looks
  reasonable" is not a number.
- **A debug view that composites before the tonemap is not a faithful map.** The
  `debug_view` branch writes into the HDR `world_target`, which then goes through
  AgX + exposure 0.35 — a raw 0..1 grayscale map gets crushed dark and `1.0`
  never reads as white. Boost into HDR (`wet*4`, tinted) so it shows *structure*.
  And in verification, **look at the debug view FIRST**: it decouples "is the
  grid accumulating?" from "is the composite strong enough to see?" — empty debug
  view ⇒ accumulation/tuning bug; full debug view but dry-looking scene ⇒
  composite-strength bug.
