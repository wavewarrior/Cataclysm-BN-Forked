# Handoff: Remaining Test Failures & Bugs (post mouse-interactivity plan)

**Context**: While implementing the mouse interactivity plan (commits `d6721f0`..`79ec0ac7e8` on
`feature/improvements`), the full `cata_test-tiles` suite was run repeatedly to verify no
regressions. One genuine pre-existing bug was found and fixed in that work
(`79ec0ac7e8 fix(animation): guard draw_bullet_trajectories against null tilecontext`). The
remaining failures below were **confirmed pre-existing** via stash/rebuild A/B comparison against
unmodified `feature/improvements` HEAD (before the mouse-interactivity commits) and are **not**
caused by the mouse work. None of the affected files (`vehicle*.cpp`, `physics/*`, `vision_test.cpp`,
`coop_*.cpp`, `furniture_grab_test.cpp`, `projectile_test.cpp`, `ranged_*`) were touched by that
plan. This doc exists so the next session doesn't have to re-derive that A/B result.

Build: `cmake --preset osx-arm-slim && cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles`
(background job, 1200s+ timeout per AGENTS.md — do not run synchronously).

## 1. Vehicle-physics test failures (Box2D-related, ~majority of failures)

Run: `./cata_test-tiles "~[.]~[vision]"` → **28 failed / 912 test cases**, 125/7.68M assertions failed.

Confirmed root cause: `plans/box2d-vehicle-physics-implementation.md` is **partially implemented**
(Phase 10 marked "🔄 PARTIAL"). These are vehicle position/pivot bugs under the incomplete Box2D
migration, not new regressions. Exact current failures (fresh run, this session):

| File:line | Assertion | Likely subsystem |
|---|---|---|
| `tests/vehicle_rails_test.cpp:184` | `REQUIRE( pivot_bub_ms_location( veh ) == veh.bub_ms_location() )` | Box2D pivot/position desync on rails |
| `tests/vehicle_test.cpp:419` | `REQUIRE( part_pos )` — `std::optional` empty | part position lookup failing under continuous rotation |
| `tests/vehicle_ramp_test.cpp:306,307,339,340` | `player_character.bub_pos()` / `cart.bub_ms_location()` off by one tile | ramp traversal position tracking |
| `tests/ranged_vehicle_recoil_test.cpp:156` | `square_dist(starting_pos, veh->bub_ms_location()) >= 1` fails (recoil didn't move vehicle) | recoil impulse not applied to Box2D body |
| `tests/furniture_grab_test.cpp:110,127,128,132,133` | `grabbed_pos`/`player_character.bub_pos()`/`here.furn(...)` mismatches around ramps | grab-drag position tracking interacting with ramp z-transition |
| `tests/coop_terrain_test.cpp:68,102` | (no CHECK text captured — investigate directly) | coop terrain sync, possibly vehicle-adjacent |
| `tests/coop_pickup_test.cpp:196,236` | (no CHECK text captured — investigate directly) | coop pickup, possibly vehicle-adjacent |
| `tests/projectile_test.cpp:150` | `CHECK( attack.end_point == shooter_pos )` | projectile origin tracking — check if shooter is vehicle-mounted in this test |
| `tests/ranged_aiming_test.cpp:155` | `REQUIRE( impassable_tiles >= 1 )` | aiming obstruction detection |

**Next step**: resume `plans/box2d-vehicle-physics-implementation.md` at Phase 10 Step 6 (Retire
Legacy Motion Fields) per its own dependency chain (`playtest → Phase 12 → Phase 10 Step 6 →
Phase 11`). `vehicle_rails_test.cpp:184` and `vehicle_test.cpp:419` are the cleanest starting
points — both are pure position-desync assertions with no coop/UI entanglement.

## 2. `vision_test.cpp` shadowcasting failures (separate bug, NOT physics)

Run in isolation: `./cata_test-tiles "[vision]" --rng-seed 1` → **8 failed / 18 test cases**, 92
assertions failed, completes cleanly in ~7s (no crash, no hang — see §3 for why the full suite
behaves differently).

All 8 failures are `tests/vision_test.cpp:266: FAILED: success for: false`, with messages showing
**divergence between `'using 3d casting'` and `'using 2d casting'` at the same origin/threshold**,
e.g.:
```
'using 3d casting' and 'test case transformation: 0' and 'zlevels: 1' and 'origin: (59,59,0)' and
'player: (60,60,0)' and 'unimpaired_range: 60' and 'vision_threshold: 3.28305' and ...
```
Failures span both casting modes across multiple `test case transformation` indices (0,1,2,3,4,5,6,7)
and two `vision_threshold` values (`3.28305` = midnight, `3.31072` = noon), always at
`player: (60,60,0)` with `origin` one tile away. This looks like a **shadowcasting-vs-3D-lighting
edge-case regression at exactly the player's adjacent tile**, independent of the physics work —
worth its own investigation starting at `src/shadowcasting.cpp` / `src/lightmap.cpp` around whatever
computes `vision_threshold` and the 2D/3D cast comparison in `tests/vision_test.cpp:266`.

**Confirmed pre-existing**: reproduced identically on unmodified `feature/improvements` HEAD via
stash/rebuild before the mouse-interactivity commits landed.

## 3. Full-suite hang / intermittent SIGSEGV (order-dependent, needs its own repro)

Running the **entire** suite together — `./cata_test-tiles "~[.]"` (no tag exclusion) — hangs
indefinitely partway through in this session (observed 15+ min with no progress, log spinning on
`map_cache.cpp` shift-probe / lightmap rebuild messages). Earlier in this work session the same
full-suite run instead produced a `SIGSEGV` inside `ranged_aoe_test.cpp` (`"character using
birdshot against another character"`, fixed by the `animation.cpp` null-tilecontext guard — see
commit `79ec0ac7e8`) and, in a separate full run, a **different** SIGSEGV/hang combination
attributed to `[vision]` tests running after vehicle/Box2D tests (`Box2D physics threading
issue`, confirmed pre-existing against baseline HEAD by rebuild+run comparison).

**Key finding this session**: running `[vision]` alone does *not* hang or crash — it fails cleanly
(§2). Running `~[.]~[vision]` alone does *not* hang — it fails cleanly (§1) in ~4 minutes. The hang
only manifests when the *combined* full ordering runs, which strongly suggests **cross-test-case
state leakage** (a Box2D `PhysicsWorld`, thread, or map-cache singleton not fully torn down between
test cases) rather than a bug in any single test. This needs a bisection pass:

1. Run `./cata_test-tiles "~[.]" --order rand --rng-seed <N>` a few times with different seeds to
   see whether the hang location moves (confirms order/state-dependence vs. a fixed problem test).
2. If it's consistently triggered by adjacent vehicle-physics + vision tests, check
   `PhysicsWorld` singleton lifecycle / `tests/test_main.cpp` `clear_states()` for whether
   Box2D bodies or worker threads survive across `TEST_CASE` boundaries.
3. `debug.log` around the hang shows repeated `[shift-probe][invalidate]` / `[build_cache][perf]`
   entries — i.e. map cache is still being rebuilt in a loop. Check whether a test is stuck in
   `map::invalidate_map_cache` / `build_cache` due to a submap grid never converging (e.g.
   `dirty_submaps=225/225` never dropping to 0 across many iterations logged in quick succession).

This is squarely in `plans/box2d-vehicle-physics-implementation.md` territory (co-op non-determinism
section already documents Box2D isn't thread-safe across instances) but is a **test-harness
isolation bug**, not a gameplay bug — prioritize only if it starts blocking CI, since running tag
subsets (as this session did) sidesteps it.

## 4. Tooling gotcha: stale test binary trap

`tests/CMakeLists.txt` sets `RUNTIME_OUTPUT_DIRECTORY` such that `cata_test-tiles` (and
`cataclysm-bn-tiles`) link to **the repo root**, not `out/build/<preset>/tests/`. A leftover binary
can sit in `out/build/<preset>/tests/` from a much older build and silently get executed by a
`cwd`-based script, producing misleading "pre-existing failure" results that are actually just
stale. Always run `ls -la cata_test-tiles cataclysm-bn-tiles` at the repo root and check the
mtime/`git log -1` correlation before trusting a test run's results. This cost significant time
during this session — a fresh binary is confirmed by `git log -1` timestamp being older than the
binary's mtime.

## 5. Related open items already tracked elsewhere

- `plans/THINGS THAT NEED FIXING.md:8` — "game crashed upon trying to drive vehicle" is very
  likely the same Box2D issue as §1 above; worth confirming with a repro once §1 is fixed.
- `plans/box2d-vehicle-physics-implementation.md` — owning plan for §1; §3's harness-isolation
  question should probably get a short addendum there once triaged, since it touches the same
  `PhysicsWorld` lifecycle the co-op non-determinism section already discusses.
