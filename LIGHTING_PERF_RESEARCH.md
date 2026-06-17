# Lighting performance research — SDF rebuild gate

Status: research only (no code changed). Grounds the earlier generic "prebake SDF"
notes (obs #141054, #142525) in the actual code. Date: 2026-06-13.

## TL;DR

The per-tile lighting build (`frame_build.cpp::build_and_submit_lighting`) recomputes
a **whole-reality-bubble Euclidean distance transform twice** on every turn **and**
every camera scroll. Most of that work is redundant: the SDF occluder set is
dominated by static walls and changes far less often than the gate fires. The win is
**change-gated memoization** of the structure buffers, decoupled from the one buffer
that genuinely moves with the player (`vis`).

This is **not "prebaking"** in the literal sense. The DT is *global* — one wall change
propagates distance across the entire bubble — so you cannot patch one submap's SDF
incrementally without recomputing its neighbours. The correct granularity is:
*recompute the whole-bubble SDF only when the occluder set actually changes.*

## Ground truth (verified against current code)

### The gate (`sdl_render_frame.cpp:118-135`)

```cpp
rebuild_pertile = imgui_layer::visible()      // F4 dev panel open → every frame
                  || now != last_turn          // turn advanced
                  || z   != last_z             // z-level changed
                  || origin != last_origin;    // camera tile-origin moved (player walked)
```

One boolean drives **all** per-tile buffers (`frame_build.cpp`):

| Lines    | Buffer(s)                         | True dependency            | Changes when…                    |
|----------|-----------------------------------|----------------------------|----------------------------------|
| ~111-146 | `transparency` → `sdf`            | fully-opaque tile set      | wall/vehicle/door/opaque-field   |
| ~148-175 | `sun_sdf` (trees forced clear)    | opaque set minus tree set  | same, minus foliage              |
| ~177-266 | `sky_vis` + indoor skylight bleed | `outside_cache` (roofs)    | roof built/destroyed             |
| ~268-310 | `vis` (FOV)                       | `seen_cache` (player sight)| **every player move / light Δ**  |

Only `vis` legitimately needs per-move rebuild. The other three are **bubble-indexed
structure** — identical under camera scroll, identical turn-to-turn while walls/roofs
are static — yet they recompute on every `now != last_turn` and `origin != last_origin`.

### The SDF spans the whole bubble, not the screen

`W = mapsize*SEEX`, `H = mapsize*SEEY` (`frame_build.cpp:103-105`), indexed
`buf[x*H + y]` in world/map-tile space. Therefore **camera scroll within the bubble
produces a byte-identical SDF** — the gate's `origin != last_origin` term recomputes
data that did not change. (Crossing a submap boundary re-centres the bubble and *does*
remap the cache; a content signature catches that automatically.)

### The occluder is a hard binary threshold at exactly 0.0

`compute_sdf_cpu` (`sdf_pass.cpp:29`) seeds the DT with:

```cpp
grid[i] = ( trans[i] == 0.0f ) ? 0.0f : INF;   // ONLY exactly-opaque tiles seed
```

This is the most important fact for the gate. `transparency_cache` folds in fields
(smoke/gas/fog) and vehicles, **but** partial transparency (smoke at 0.3, etc.) is
`!= 0.0` → it does **not** seed the DT → it does **not** change the SDF. Only
*fully* opaque blockers matter: walls, vehicle parts, and smoke dense enough to hit
exactly 0. So the occluder set is far more stable than the raw float cache implies —
it is effectively "the set of fully-opaque tiles," which static walls dominate.

The DT input (`trans_ss`, `frame_build.cpp:127-139`) is filled from the **raw float**
`mc.transparency_cache`, not the uint8 `transparency[]` pack — so the threshold is
literally exact `== 0.0f`, and the signature must hash the **raw-float exact-zero
mask**.

## The optimisation

**1. Decouple the gate into two frequencies.**
- *Structure* (`sdf`, `sun_sdf`, `sky_vis`, skylight bleed): rebuild only when the
  occluder set changes.
- *FOV* (`vis`): rebuild on player move / turn, as today.

**2. Gate structure on a signature of the thresholded occluder mask, not raw floats.**
Hash the binary `trans[i]==0.0f` mask (+ a tree-set signature for `sun_sdf`, since the
sun mask = opaque-minus-tree and trees move ~never) + `zlev`. Compare to last frame; if
unchanged, skip both DTs, both 1.1 MB allocs, and the bleed flood-fill, and keep the
retained GPU buffers (the skip path already submits empty vectors and skips upload —
obs #118841). Hashing raw floats would over-invalidate on sub-threshold field flicker;
hashing the thresholded mask survives firefights unless smoke goes fully opaque.

The signature pass is O(total) ≈ 17–24k tile reads (one cheap pass over the
*tile-resolution* cache, before the 4× supersample) — negligible against the work it
gates.

## Scope of the win (be honest about the bound)

- **Big win:** standing/walking in a built interior or static outdoor scene. Walls
  don't change → structure rebuild skipped on every turn and every scroll. Only the
  `vis` rebuild remains — and `vis` is **not** free: it is also supersampled (~279k-cell
  fill, `frame_build.cpp:~302-310`) plus an optional Gaussian blur. `vis` is the
  irreducible per-move floor the gate cannot remove; measure it, don't assume it cheap.
- **Near-zero win:** driving (vehicle parts shift the opaque set every turn) and
  dense-smoke firefights *if* smoke reaches full opacity. There the signature differs
  every turn and the gate correctly rebuilds. This is the exact heavy-actor scene the
  original lag complaint named — so the SDF gate is **not** a fix for that case; it is a
  fix for the common static case.

## Cost — currently UNMEASURED (instrument before implementing)

There is **no `ZoneScopedN` anywhere in `frame_build.cpp` or the DT** — this hot path is
invisible to Tracy, which the rest of the engine uses heavily. That is the first gap.

Analytical size (per structure rebuild, default `MAPSIZE≈11`, `SEEX=12`,
`SDF_SUPERSAMPLE=4`):
- supersampled grid = `(132·4)² ≈ 279k cells`
- separable Euclidean DT (2 passes) run **twice** (sdf + sun_sdf) → ~0.56 M cell-passes
- plus 2 × ~1.1 MB heap alloc/free per rebuild (`trans_ss`, `trans_ss_sun`)
- fired every turn + every scroll frame

Plausible cost is a few ms/frame on the render thread while walking, but this is an
*estimate, not a measurement*. **Gating step before any implementation:** wrap the two
DTs and the `vis` rebuild in `ZoneScopedN`, read the ms in Tracy. If SDF-DT is ~0.5 ms
this is not worth the complexity; if it is ~5–6 ms it is the headline render-thread win.

## Implementation sketch (after the Tracy number justifies it)

**The load-bearing step is partial-rebuild plumbing, not the signature.** Today retain
is *all-or-nothing*: a single `rebuild_pertile` bool gates compute, the result struct,
**and** every GPU upload together. The existing "skip" path only retains because nothing
else is in flight — there is **no path that skips SDF/sun_sdf/sky_vis while still
uploading `vis`**. That mixed frame is exactly what this proposal needs and it does not
exist yet.

**Decisive pre-work question (decides the whole effort):** in the submit/upload chain
(`frame_build.cpp:~337-350` → `sdf_pass` upload + sky_vis + vis), can `vis` be uploaded
alone while the SDF storage buffer on the GPU is left untouched? Answer that before
calling the sketch complete.

1. Add `ZoneScopedN` to: occluder pack, `compute_sdf_cpu` (sdf + sun), bleed flood-fill,
   `vis` build. Measure. (There is currently none anywhere in this file.)
2. Make the result struct carry **per-buffer present/absent** flags, and gate each
   upload site (`sdf_pass`, sky_vis, vis) independently so an absent buffer is retained
   on the GPU rather than cleared.
3. Split `rebuild_pertile` into `rebuild_structure` and `rebuild_vis`. Compute
   `rebuild_structure` from a raw-float exact-zero occluder-mask signature (+tree set,
   +zlev) held `static` in the function or on `render_state`. `rebuild_vis` keeps the
   move/turn/z terms.
4. Keep `imgui_layer::visible()` forcing a full rebuild (dev tuning needs live response).

Risks / caveats:
- **Correct-by-construction:** the signature is safe regardless of when `map::shift` /
  bubble re-centre fires. If content differs → rebuild; if not → skip; both safe. No need
  to characterise bubble-shift cadence — that removes a verification burden.
- 64-bit signature collision is astronomically unlikely; worst case is one stale frame
  until the next real change — acceptable.
- First frame / cache-not-yet-populated already guarded (`size() >= total`).

## Other avenues (the "and such") — low ROI, noted for completeness

The sim side is already heavily optimised: `game::monmove` uses LOD tiers (distance-
banded AI), parallel tier-0/1 planning, actor snapshots, and reuse caches; `world_tick`
gates submap simulation on pocket-simulation level and active fire/field requests
(obs #142008, #141633). These are mature. The render-thread SDF gate above is the
clearest untapped win and the one that matches the literal "prebake SDF" ask.

---

# Addendum 2026-06-14 — two-layer SDF, spatial LOD, GI re-evaluation

All three below are **designs conditional on the same unmeasured Tracy number** as
above. None should be built before the DT/vis zones are timed: two-layer SDF only pays
if the dynamic-churn case (driving / opaque smoke) is a profiled pain point; RC only
pays if pass-1 cost scales badly with emitter count. Measure first.

## Two-layer SDF (static + dynamic) — standard, fits, but bounded value

Online precedent (UE5 Lumen, confirmed via web search 2026-06-14): a **Global Distance
Field** = a coarse clipmap composited from cached per-object SDFs, recomputing **only
"dirty" voxel regions** — those touched by a moved/added/deleted object or revealed by
camera shift. Amortised maintenance is near-zero; spikes only on large discontinuous
change (teleport). The static set is cached/precomputed; the dynamic set updates its
local region. This is exactly the "static layer redrawn on change + dynamic layer" idea.

**The composite is mathematically exact.** Distance to the union of two occluder sets =
`min(distance to A, distance to B)`. So `combined_sdf = min(static_sdf, dynamic_sdf)` is
an identity, not an approximation. ✓

**Mapping onto this grid engine:**
- *Static layer*: DT over **terrain+furniture** transparency. Change-gated (the gate
  above), recompute only on structural change.
- *Dynamic layer*: DT over **vehicles + opaque fields** (the per-frame churners).
- *Combine*: one `min()` pass.

**Windowing the dynamic DT (the actual cost win) — get the justification right.** A
full-grid dynamic DT costs the same as the full DT (grid-bound), so the win requires
either (a) skipping the dynamic layer entirely when no dynamic occluder is opaque
(common), or (b) restricting the dynamic DT to a **window = dynamic-occluder bbox + max
shadow-march reach**. The correct reason windowing is safe: tiles beyond
`(occluder + light reach)` **cannot be shadowed by that occluder at all**, so the march
never queries the dynamic field there — INF-padding outside the window is harmless.
(The tempting "dynamic_sdf > static_sdf out there so min picks static" justification is
**wrong** — it fails in open terrain where a vehicle far from walls has the smallest
distance arbitrarily far out, and would pop a shadow at the window edge.) Concrete
margin from the shaders: sun march reach = `8.0` tiles (`sprite.frag` line 475);
emitter reach = each emitter's falloff radius (line 416, `dist = length(dv)`). So
margin = max light-reach in range — bounded, but large emitters (floodlight/fire) erode
the window.

**Two named gates before this is feasible (the load-bearing pre-work):**
1. **Structural-only transparency.** `transparency_cache` is the *composite*
   (terrain+furniture+vehicle+field). The static layer needs a vehicle/field-**excluded**
   transparency. The `sun_sdf` tree-forcing is *additive* (force tiles clear); excluding
   vehicles is the opposite operation and needs per-tile knowledge of *which source* set
   the opacity. Does the map expose terrain+furniture-only transmittance cheaply, or must
   it be rebuilt from `ter`/`furn` flags? This decides the whole split. (Analog of last
   round's "can vis upload alone?")
2. **Per-buffer upload plumbing** — same gate as the change-gate section.

**Honest bound (advisor-corrected):** the static/dynamic split is by *source type*, which
is **not** the change-frequency axis. Doors are terrain/furniture ("static layer") yet
toggle constantly; a parked vehicle is "dynamic" yet never moves. So the split alone does
**not** deliver "static rarely rebuilds" — what makes rebuild cheap is the per-layer change
signature + windowing. The split's marginal value *over the change-gate already proposed*
is therefore narrow: it makes the **dynamic-churn case** (driving / opaque smoke) cheap via
windowed rebuild — and that case is exactly the rare, scene-bound one. Worth it only if the
profile says that case hurts.

## Spatial LOD / clipmaps (the "tessellation" question)

Not tessellation (that subdivides a *mesh*; these are grid fields). The real technique is
**clipmaps / cascades**: fine near player, coarse far. Hard blocker for the SDF: the
separable Euclidean DT needs a *uniform* grid and is *global* (nearest occluder may be
far), so variable resolution requires nested clipmap grids with boundary-seeded levels —
a re-architecture, and the *wrong* place to spend the LOD idea (see GI below). A cheaper
cousin that fits today: lower/condition the global `SDF_SUPERSAMPLE`, or graduate it only
in the dynamic-churn case.

## GI re-evaluation — deferred, not rejected (correcting the prior record)

The earlier record (memory) framed the radiance-cascade **hierarchy** as "~pointless for
point lights — angular bins add nothing." **That is overstated and conflated the direct
and indirect terms.** Owning the reversal:

- **Direct term: skipping RC is correct and should stay.** `rc.frag` pass 1 loops every
  emitter with exact direction + distance + SDF occlusion — analytically exact. RC's
  angular discretisation could only *approximate* what is already computed exactly.
- **Indirect (bounce) term: the claim is false.** `rc_bounce.frag` is a fixed **16-ray,
  20-step** march per probe (`RC_DIRS=16`) — itself a low, non-hierarchical angular
  discretisation. The bounce is precisely where RC earns its keep (cheap many-directional
  integration, range scaling, coarse-far/fine-near probes). "Angular bins add nothing"
  does not hold here.
- **It was a deferral, not a principled rejection.** `rc.frag`'s own comment says the
  "directional cascade hierarchy + bilinear-fix merge is Phase 3" — and Phase 3 was
  repurposed into the 16-ray bounce. So the hierarchy was shelved as MVP-sufficient, not
  proven unnecessary.

**But the deferral still correctly stands for *current* scenes** (few point lights, single
bounce, ~8-tile reach) — RC would not visibly improve them yet. **The discriminator that
flips it:** pass-1 cost is `O(probes × emitters × steps)` (loops all emitters per probe);
RC's cost is emitter-count-*independent* after injection. So revisit RC when **emitter
count is high** (lit city / many fires / lamps) or for future **area/sky/emissive**
lighting — checkable by profiling pass-1 against emitter count in the worst lit scene.

**The throughline worth keeping:** radiance cascades *are* the principled spatial-LOD for
lighting — fine/near, coarse/far probes. That is the honest home for the "high-res near
player" idea: on the **indirect (GI)** term, not on the SDF. If spatial LOD is pursued at
all, it belongs there.
