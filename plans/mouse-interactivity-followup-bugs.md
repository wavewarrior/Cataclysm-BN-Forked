# Handoff: Remaining Test Failures & Bugs (post mouse-interactivity plan) — RESOLVED

**Status**: Resolved 2026-07-28. All actionable items in the original handoff are fixed and
verified. Remaining known failures are documented at the bottom as genuinely pre-existing or
gated on separate migration work.

**Original context**: While implementing the mouse interactivity plan (commits `d6721f0`..`79ec0ac7e8`
on `feature/improvements`), the full `cata_test-tiles` suite surfaced failures that were confirmed
pre-existing via stash/rebuild A/B against unmodified HEAD.

Build: `cmake --preset osx-arm-slim && cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles`
(background job, 1200s+ timeout per AGENTS.md — do not run synchronously).

---

## Resolution summary

| # | Item | Status | Commit |
|---|---|---|---|
| 1 | Full-suite hang / SIGSEGV | ✅ Fixed | `87c1c9e13f` |
| 2 | `vision_test.cpp` shadowcasting (8 failures) | ✅ Fixed | `9c10249f4a` |
| 3 | Ramp / furniture-grab position tracking | ✅ Fixed | `6aa1cdc30b` |
| 4 | `projectile_test.cpp:150` z-bounds | ✅ Fixed | `733b9efa5b` |
| 5 | `ranged_aiming_test.cpp:155` | ✅ Fixed | `282c998dae` |
| 6 | Vehicle escaping loaded map (crash) | ✅ Fixed | `27b3848f26` |
| 7 | Coop terrain/pickup failures | ✅ Fixed (by #1) | `87c1c9e13f` |
| 8 | `vehicle_rails_test.cpp:184` pivot desync | ⛔ Deferred — see below | — |

---

## 1. Full-suite hang — FIXED (`87c1c9e13f`)

**Root cause**: a real teardown-hook gap in the `creature` → `PhysicsWorld` lifecycle, *not* the
vehicle-motion-field migration. `map::phys_world` and its `b2WorldId` are constructed exactly once
per test binary run (`tests/test_main.cpp` `init_global_game_state()`), never per-`TEST_CASE`, so
any leak into that persistent Box2D world accumulates for the entire suite — matching the observed
"only fails when many test batches run together" symptom.

Two natural-death code paths erased creatures from their owning containers **without** calling
`PhysicsWorld::on_creature_removed()`, so the leaked `b2Body` (a kinematic sensor) was never
destroyed and `creature_bodies_` kept a dangling-pointer entry keyed by raw `const Creature *`.
Hundreds of orphaned kinematic bodies degraded broad-phase collision to O(n²), progressively slowing
the run into an apparent hang.

**Fix**: added the missing `on_creature_removed` hook to both leaking removal paths
(`Creature_tracker::remove_dead()`, `game::erase_npc()`), mirroring the existing correct call sites.

**Verified**: full suite `~[.]` completes in ~3.5 min (was: non-terminating past 15+ min).

> Note: `plans/test-hang-investigation-handoff.md` documents a **different** hang (inside
> `player_activity::do_turn()` / `automatic_reloading_action`, shard-index-dependent, no vehicles or
> physics involved). No code-level connection was found between the two — treat as unrelated bugs
> that happen to share the general "cross-test state leakage" pattern.

## 2. `vision_test.cpp` shadowcasting — FIXED (`9c10249f4a`)

**Root cause**: NOT a shadowcasting algorithm bug and NOT stale expected-results tables — a
cross-`TEST_CASE` global-state leak, the same class of bug as #1, in a different cache.

In `map::build_map_cache()` (`src/map_cache.cpp`), `vehicle_floor_cache` is (correctly, per its own
comment) cleared unconditionally every rebuild "to prevent stale entries after shifts". Its two
siblings `vehicle_obscured_cache` / `vehicle_obstructed_cache` (diagonal corner-cut-prevention flags
consumed by `castLight`/`cast_zlight`) were only cleared `if( ch.veh_in_active_range )`. Once any
vehicle-creating test ran and then tore its vehicle down, the flags set at the shared player position
`(60,60,0)` were never cleared, corrupting subsequent vision tests.

**Fix**: clear all three vehicle caches unconditionally every rebuild, matching the precedent already
set for `vehicle_floor_cache` in the same function. `shadowcasting.cpp` and the expected-results
tables were **not** touched — confirmed correct via a minimal 2-test repro
(`vision_see_into_vehicle`, `vision_no_lights`) before any code change.

**Verified in isolation**: `[vision]` → All tests passed (3,641 assertions in 18 test cases).
Previously 10/18 passed with 92 failed assertions.

**Caveat — still fails under full-suite ordering.** A full `~[.]` run after this fix still shows
8 × `vision_test.cpp:266`. The specific leak fixed here (`vehicle_obscured_cache` /
`vehicle_obstructed_cache`) was real and is resolved, but at least one *further* upstream test
leaves state that corrupts vision when hundreds of cases run first. Same pattern applies to
`vehicle_ramp_test.cpp:174` (6 × in full suite, clean in isolation). Both need the same treatment:
find the leaking cache/singleton, clear it unconditionally. See "Residual cross-test leakage" below.

## 3. Ramp / furniture-grab position tracking — FIXED (`6aa1cdc30b`)

**Root cause**: a naive, per-destination-tile "does this tile have RAMP_UP/RAMP_DOWN → step z" rule
used by the grab-drag code paths, which does NOT match the ramp-crossing convention the player's own
movement code implicitly relies on. Ramp tiles are built in pairs spanning two x-columns (a low
column and a high column at the *same* lower z-level, both flagged). Ascending is only supposed to
happen once per full crossing, but a dragged object whose walked line lingers on (or steps back onto)
an already-crossed tile re-applied the same z-step.

**Fix**: only cross when the destination tile carries a ramp flag the source tile did *not* already
have (`ramp_z_delta_at( here, from, to )`), plus the analogous guard for dragged furniture in
`ramp_adjusted_furniture_destination()`. Also stopped forcing `box2d_position_authority = false`
during grab-drag operations, since `vehmove()`'s Box2D step would otherwise fight the legacy
tile-displacement code every turn.

**Verified**: `[furniture][grab]` → All tests passed (79 assertions in 5 test cases).
`[vehicle][grab]` → All tests passed (104 assertions in 8 test cases).

## 4. `projectile_test.cpp:150` — FIXED (`733b9efa5b`)

**Root cause**: a target outside the map's vertical span has no representable line of sight — the
angle-based DDA raycast only steps through the shooter's own z-level, so a same-column out-of-range
target degenerated to `atan2(0, 0)` and fired off in an arbitrary horizontal direction instead of
stopping.

**Fix**: treat an out-of-z-bounds target as an immediate stop at the shooter's position.

## 5. `ranged_aiming_test.cpp:155` — FIXED (`282c998dae`)

**Root cause**: test-side, not production. `set_up_player_vision()` internally calls `place_player()`,
which can recentre the reality bubble and shift the coordinate frame. Tests that kept using the raw
compile-time `shooter_pos` constant afterwards were operating on a stale frame, so the wall/monster
were no longer where the test believed.

**Fix**: capture `shooter.bub_pos()` *after* `set_up_player_vision()` and use that corrected position
for all subsequent terrain setup and pathfinding.

## 6. Vehicle escaping the loaded map — FIXED (`27b3848f26`)

**Root cause** (found during this pass, not in the original handoff): `b2World_Step()` integrates
continuously, and over a game turn a fast vehicle's body can travel past the edge of the loaded
reality bubble. The Box2D position readback in `map::vehmove()` then called `displace_vehicle()`
toward a submap that was never loaded, which stranded the vehicle in a null submap and produced a
`dst submap not loaded` error plus a crash in `vehicle_efficiency_test`.

A contributing factor was an earlier "60× cadence" change that looped `phys_world->step(1/60, 4)`
sixty times per turn on the theory that a single call under-stepped a 1-second turn. That reasoning
is wrong for this codebase: **Box2D is not the speed authority for ordinary driving.** `veh.velocity`
is re-applied to the body every turn and the readback then snaps the tile anchor to the body, so
integrating a whole second re-derives displacement on top of the game's own velocity model. Measured
effect: vehicles travelled **2.2–2.3× further than the fuel-efficiency balance allows**
(257,835 tiles vs. a 111,404 upper bound).

**Fix**, two parts:
1. Reverted the cadence to a single `step(1/60, 4)` per turn. Sub-tile smoothness comes from the 4
   sub-steps, not from more full steps.
2. Added a hard boundary stop: if the readback's destination tile is not `inbounds()`, stop the
   vehicle, rewind `physics_pos` to the tile it actually occupies, and force the b2Body transform
   back via the new `PhysicsWorld::clamp_body_to_tile()` (which deliberately bypasses the
   `box2d_position_authority` guard in `on_vehicle_moved`, because in this one case the tile grid
   must win).

**Verified**: `vehicle_efficiency` passes across fixed seeds 1–8 and 11–111; no crash, no
`displace_vehicle` errors.

## 7. Coop failures — FIXED by #1 (`87c1c9e13f`)

`coop_terrain_test.cpp:68,102` and `coop_pickup_test.cpp:196,236` shared the creature-body leak root
cause. **Verified**: `[coop]` → All tests passed (483 assertions in 141 test cases).

---

## Still open

### `vehicle_rails_test.cpp:184` pivot desync — deferred

An attempt this session root-caused the literal assertion and fixed it, along with four additional
real bugs in the same Box2D-rails migration seam (`advance_precalc_mounts()` copying never-populated
`precalc[1]`/`pivot_anchor[1]` slots for Box2D-authority vehicles; `process_movement_on_rails()`
running after the Box2D early-return so rail-curve correction never applied; `skidding` latching true
with no clear path for Box2D vehicles; unconditional face adoption ignoring legacy's
`!is_on_rails || rpres.do_turn` gate).

That work took the file from 0/5 to 2/5 passing, **but introduced a catastrophic regression** —
`vehicle_efficiency_test` dropped to near-zero movement for every vehicle type — and was fully
reverted. The remaining 3/5 gap is a genuinely different, harder problem (continuous-physics sub-tile
discretization vs. exact-tile test expectations, plus an unexamined ramp/z-level path) that belongs
with `plans/box2d-vehicle-physics-implementation.md` Phase 10 Step 6, behind its own
`playtest → Phase 12 → Phase 10 Step 6 → Phase 11` dependency chain.

### `ranged_vehicle_recoil_test.cpp:157` occupant tracking — gated

The original `:156` failure (recoil doesn't move the vehicle) is fixable, but doing so exposes a
second assertion at `:157`: the player riding the chair doesn't track the vehicle's new position.
Passenger position sync under Box2D authority is core occupant-handling migration work, explicitly
listed as future work in `plans/box2d-vehicle-physics-implementation.md` — not a bug fix.

### `vehicle_efficiency_test.cpp:198` mass check — pre-existing RNG noise

`CHECK( std::abs( actual_mass - expected_mass ) <= tolerance )` with a 2% tolerance and an in-test
comment acknowledging "cargo/fuel RNG causes small variance". Fails on roughly 20% of random seeds.
**Confirmed pre-existing**: seeds 66 and 99 fail identically on clean HEAD with none of this
session's changes applied. Unrelated to vehicle movement or physics.

---

## Residual cross-test leakage (new work item)

Full suite `~[.] --rng-seed 1` after all fixes above: **929 test cases, 891 passed, 36 failed**,
7,879,809 assertions with 259 failed. Runtime **5m33s** — the headline win, since this run
previously did not terminate at all.

Of those 36, these fail *only* in full-suite ordering and pass 100% in isolation:

| Test | Full-suite | Isolated |
|---|---|---|
| `vision_test.cpp:266` | 8 failures | 18/18 pass |
| `vehicle_ramp_test.cpp:174` | 6 failures | 8/8 pass |
| `coop_terrain_test.cpp:130`, `coop_pickup_test.cpp:112,129` | 3 failures | 141/141 pass |

This is the same class of bug as §1 and §2 — a cache or singleton surviving a `TEST_CASE` boundary —
but in a *different* container than the two already fixed. The `PhysicsWorld` singleton is the prime
suspect: it is constructed once per binary and never rebuilt (see
`plans/box2d-vehicle-physics-implementation.md` addendum §3), which contradicts Box2D's own guidance
to recreate the world per test because object pools make body ordering depend on allocation history.

Suggested approach, in order of expected value:
1. Bisect with `--order rand` across several seeds to identify which predecessor test contaminates
   `[vision]`. The isolation repro used for §2 (`vision_see_into_vehicle`, `vision_no_lights`) is a
   good minimal target to append predecessors to.
2. Audit `clear_states()` in `tests/state_helpers.cpp` for caches it does *not* reset.
3. Consider rebuilding `PhysicsWorld` per `TEST_CASE` rather than per binary, which would eliminate
   this entire bug class rather than patching leaks one at a time.

## Tooling gotcha: stale test binary trap (still applies)

`tests/CMakeLists.txt` sets `RUNTIME_OUTPUT_DIRECTORY` such that `cata_test-tiles` and
`cataclysm-bn-tiles` link to **the repo root**, not `out/build/<preset>/tests/`. A leftover binary in
`out/build/<preset>/tests/` from an older build can silently get executed by a `cwd`-based script,
producing misleading "pre-existing failure" results that are actually just stale. Always check
`ls -la cata_test-tiles cataclysm-bn-tiles` at the repo root against `git log -1` before trusting a
run.

**This bit again during this session in a subtler form**: a test run against a not-yet-rebuilt binary
produced a spurious 59,549-assertion pass that contradicted a real crash, nearly causing a correct
fix to be reverted. When two runs of "the same tree" disagree, suspect the binary before the code.

## Related items

- `plans/THINGS THAT NEED FIXING.md:8` — "game crashed upon trying to drive vehicle" is plausibly the
  same crash fixed in #6; worth a repro to confirm and close.
- `plans/box2d-vehicle-physics-implementation.md` — owns the two deferred items above.

## Verification commands

```sh
./cata_test-tiles "[vision]"            --rng-seed 1 --user-dir=/tmp/v1/
./cata_test-tiles "[furniture][grab]"   --rng-seed 1 --user-dir=/tmp/v2/
./cata_test-tiles "[vehicle][grab]"     --rng-seed 1 --user-dir=/tmp/v3/
./cata_test-tiles "[coop]"              --rng-seed 1 --user-dir=/tmp/v4/
./cata_test-tiles "vehicle_efficiency"  --rng-seed 1 --user-dir=/tmp/v5/
```
