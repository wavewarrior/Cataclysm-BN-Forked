# Profiling guide — render + creature perf (Tracy)

Step-by-step. Goal: turn every "is it worth it?" in `LIGHTING_PERF_RESEARCH.md` into a real
ms number. One capture session covers two independent tracks — **render/lighting** (zones
added 2026-06-14) and **creature/sim** (zones already existed).

The game is the Tracy *client* (instrumented). You run the Tracy *profiler GUI* (server)
separately and connect to it. **Versions must match: this build vendors Tracy v0.13.3 — get
profiler GUI v0.13.x.**

---

## Step 0 — one-time: get the Tracy profiler GUI (v0.13.x)

You only need this once. Pick one:

- **Download a release build** (easiest): grab Tracy **v0.13.x** from
  <https://github.com/wolfpld/tracy/releases>. macOS: there's a notarized `.dmg`/app on
  recent releases; if 0.13.3 has no mac binary, build from source (next option).
- **Build from the vendored source** (guaranteed version match): the source is already
  fetched under the build dir. Build the profiler:
  ```
  brew install glfw freetype capstone   # deps
  cmake -S out/build/osx-arm-slim/_deps/tracy-src/profiler -B /tmp/tracy-gui -DCMAKE_BUILD_TYPE=Release
  cmake --build /tmp/tracy-gui -j
  open /tmp/tracy-gui/tracy-profiler   # binary name may be tracy-profiler or Tracy
  ```
  (If the `_deps` path differs, find it: `find out -name TracyVersion.hpp`.)

You'll know it works when the profiler window opens to a "Connect" screen.

---

## Step 1 — build the game with Tracy (DONE)

`osx-arm-slim` is already configured `-DUSE_TRACY=ON`, built, and the root
`./cataclysm-bn-tiles` is the Tracy binary. If you change code and rebuild:

```
cmake --build out/build/osx-arm-slim --target cataclysm-bn-tiles
cp -f out/build/osx-arm-slim/src/cataclysm-bn-tiles ./cataclysm-bn-tiles
```

Sanity-check you're on the Tracy binary (else zones are silent):
```
nm ./cataclysm-bn-tiles | grep -qi tracy && echo "Tracy IN" || echo "Tracy OUT"
```

When you're done profiling, revert: reconfigure `-DUSE_TRACY=OFF` and rebuild. The zone
macros become no-ops (`src/profile.h`) — zero overhead, safe to leave the code in.

---

## Step 2 — connect

1. Open the Tracy profiler GUI **first** (it listens for clients).
2. Launch the game: `./cataclysm-bn-tiles`.
3. In Tracy, the running game appears in the client list (or it auto-connects to
   `localhost`). Click **Connect**. You'll see a live timeline filling left-to-right, one
   bar per CPU thread; zones nest as colored boxes; `TracyPlot` values get their own rows.
4. Load a save (zones only fire in-game, not the main menu — `active_world` gates the
   per-tile build).

Tip: the timeline scrolls fast. Press the **pause** (⏸) button to freeze, or use the
**Statistics** window (Step 5) which aggregates across all frames regardless of scroll.

---

## Step 3 — Capture A: render / lighting

Do three ~5-second holds. Don't open the F4 dev panel during a hold — it forces a rebuild
every frame and hides the gate behaviour you're measuring.

**A1 — stand completely still**, indoors or any built area, ~5 s.
- Expectation: `light_pertile_rebuild` should fire on **almost no frames** (the gate skips
  when turn/scroll/z don't change).
- If it fires *every* frame while standing still → the dirty-gate is busting wrongly. That
  alone is the change-gate win described in the research doc. Note it.

**A2 — walk in a straight line** for ~5 s.
- `light_pertile_rebuild` now fires each step. Read the split inside it:
  - `light_sdf_dt` — appears **twice per rebuild** (sdf + sun_sdf). Note the *per-call* ms.
  - `light_vis_build` — the FOV rebuild.
- The ratio decides the optimisation: if `sdf_dt`×2 dominates → the change-gate (skip SDF
  when walls didn't change) is the win. If `vis_build` dominates → the gate saves less,
  because `vis` must rebuild on every move regardless.

**A3 — drive a vehicle, or stand in dense smoke** for ~5 s.
- Here the occluder set churns, so structure rebuilds every frame. This is the case the
  two-layer SDF targets. Confirm it's *actually* expensive before anyone builds that.

**Bonus (the GI/creature link):** watch `light_emitter_snapshot` while near **glowing or
burning** creatures (fungal, fire). Only luminous/on-fire mobs add emitters; if this zone
grows with their count, that's the emitter-count pressure that the GI pass-1 cost (and the
RC-revival question) hinges on. Ordinary zombies should not move it.

**Record (per hold): mean and max ms.**

| Zone | A1 still | A2 walk | A3 drive/smoke |
|---|---|---|---|
| `render_build_lighting` (umbrella) | | | |
| `light_pertile_rebuild` | | | |
| `light_sdf_dt` (one call) | | | |
| `light_vis_build` | | | |
| `light_emitter_snapshot` | | | |

Read-out rule of thumb: total lighting build ~0.5 ms → SDF optimisations aren't worth it;
~5–6 ms → it's the render-thread headline.

---

## Step 4 — Capture B: creature / sim

1. Open the **debug menu** (default `~` or the `debug` action key) → **Spawn → Monster**,
   spawn **5–6 awake hostile** creatures (e.g. zombies) right next to you. Repeat to reach
   10, then 15, for the scaling curve.
2. Let turns tick (move back and forth so they path toward you) and watch `game::monmove`
   and its children for several turns.

**What to read:**
- Which child zone dominates — `compute_plan` (the O(M²) faction scan + planning),
  per-creature `map::route` (A* pathfinding), or LOS?
- **The dominant zone picks the fix** (branch table in `LIGHTING_PERF_RESEARCH.md`
  Addendum 2):
  - `compute_plan` / faction scan dominates → spatial-hash the neighbour query, or cap
    tier-0 fidelity (budget).
  - `map::route` dominates → shared Dijkstra / flow-field from the player (confirmed *not*
    currently done; the canonical "horde chases player" fix).
  - LOS dominates → tighten the sight cache.
- **Scaling:** does the dominant zone grow ~linearly or ~quadratically from 5 → 10 → 15
  creatures? Quadratic ⇒ the tier-0 O(M²) path; linear-but-steep ⇒ per-creature A*.
- Watch the **LOD-budget `TracyPlot`** row — does tier-0 count spike with the lag?

**Record:**

| Creatures | dominant `monmove` child | its mean ms | `do_turn` total ms |
|---|---|---|---|
| 5 | | | |
| 10 | | | |
| 15 | | | |

---

## Step 5 — how to read a number in Tracy (mechanics)

- **Statistics window**: menu → *Statistics* (or the Σ button). Lists every zone with
  *count*, *total*, *mean (MTPC)*, *median*. This is the fastest way to get a stable mean —
  it aggregates all captured frames, no need to freeze on one.
- **Find Zone window**: menu → *Find Zone*, type e.g. `light_sdf_dt`. Shows a histogram of
  that zone's durations + mean/median/min/max. Use this for the per-call SDF cost.
- **Single frame**: pause, hover a zone box → tooltip shows that instance's duration; the
  zone may show "2 calls" where `light_sdf_dt` fired twice that frame.
- Copy the **mean (or median if spiky)** into the tables above.

> **Scope caveat:** these are **CPU** zones. GPU-pass *execution* time (RC gather, bounce,
> bloom, tonemap, sprite flush) is **not** captured — that needs Tracy GPU zones
> (`TracyD3D12Zone` / `TracyVkZone`) bound to a GPU context, which this codebase doesn't set
> up yet. Separate effort. The CPU DT is the current render suspect and is now covered.

---

## Step 6 — bring the numbers back

Each filled cell settles a specific go/no-go already written up in
`LIGHTING_PERF_RESEARCH.md`:

- A1/A2 `light_pertile_rebuild` + `light_sdf_dt` → the **change-gate** (skip SDF when walls
  static) and whether the structure/vis decouple is worth it.
- A3 → the **two-layer SDF** (static + windowed-dynamic).
- `light_emitter_snapshot` vs luminous-creature count → the **RC-revival** trigger.
- Capture B dominant zone + scaling → the **creature fix branch**.

Nothing gets built before its number justifies it.
