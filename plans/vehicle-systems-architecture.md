# Vehicle systems architecture: session record

## Status: partially complete, verified. **Scope corrected 2026-07-29 — read the note in
"Investigated, root cause corrected, left open" before trusting any earlier claim here.**

This document started as a record of three previously-reported vehicle defects
(boarded-rider ramp desync, `vehicle_efficiency_test` cross-test leakage,
`vehicle_rails_test.cpp:184`) plus a newly-found occupancy-resolution gap.

A later pass established that **the three reported defects are not shipping bugs**: they
reproduce only with `-DBOX2D=ON`, which is not the default and is not built by CI. Two of
them also had the wrong root cause recorded:

| Item | Originally recorded | Actually |
|---|---|---|
| `vehicle_rails_test.cpp:184` | XY pivot/`coord_translate` desync in shipping code | Box2D-path only; legacy tile-step passes 5/5 (4,284 assertions). Fails in the warm-up helper `add_moving_vehicle`, which calls `map::displace_vehicle()` directly before any `vehmove()` — still open |
| `vehicle_efficiency_test` leak | overmap/`MAPBUFFER` state never cleared | field residue on the vehicle footprint; `clear_overmap()` is dead *and* unsafe — **fixed** |
| `ranged_vehicle_recoil_test` | Box2D thrust/impulse integration issue, out of scope | `vehmove()` stepped 1/60 s per 1 s turn → 56x too slow — **fixed** (passes in isolation; still fails in the full run via cross-test leakage) |
| ramp rider desync | passenger-drag index mismatch, then "missing `shift_zlevel()` on the readback path" | **both wrong.** No mismatch exists in the drag path, and the readback is irrelevant: the ramp test clears `box2d_position_authority` for every vehicle it builds (`tests/vehicle_ramp_test.cpp:109,159`) and the readback branch fires **once** across the whole suite. 18 of the failures are `REQUIRE( player_character.bub_pos() == map_starting_point )` at `:174`, right after `map::board_vehicle()` → `g->update_map()`, which can shift the reality bubble and rebase bub coordinates so the test's stale `map_starting_point` no longer names the player's tile — still open |

The in-flight Box2D physics-authority migration
(`plans/box2d-vehicle-physics-implementation.md`) remains the authoritative plan for the
physics layer, and its "Remaining Work" section was rewritten in the same pass because
Phase 10 Step 5 turned out to be substantially incomplete.

## Fixed and verified

### 1. Occupancy resolution unified onto one id-keyed lookup (`src/vehicle_query.cpp`, `src/vehicle_parts.cpp`)

**Root cause**: `vehicle::get_riders()` resolved occupants via `g->critter_at(vp.pos())`
(a tile probe) while `vehicle::get_passenger()` resolved the same fact via the
stored `passenger_id` (an id lookup), and `vehicle::relocate_passengers()` used a
*third*, separately-written id-keyed loop. These three strategies could disagree.

**Fix**: `get_riders()` and `relocate_passengers()` now both call `get_passenger()`
directly instead of re-implementing resolution. `get_riders()` falls back to
`get_pet()` when `get_passenger()` returns null, preserving monster/pet coverage
(pets have no `passenger_id`-equivalent field and were never covered by the
id-keyed path).

**Verified**: `[vehicle][grab]` (104/104 assertions), `[npc][vehicle]` (55/55),
`[vehicle][engine]` (60,273/60,273), full `[vehicle]` tag identical to its
pre-change baseline.

**Caveat on that verification, added later:** every measurement above was taken on a
`-DBOX2D=ON` build. That is **not** the shipping configuration (`BOX2D` defaults OFF,
no preset or CI job enables it), and it hid a real regression in the sibling
`part_collision` change — see section 2. Unguarded vehicle code must be verified on a
`BOX2D=OFF` build; "zero regressions" measured only under `BOX2D=ON` does not support
that claim.

### 2. `part_collision`'s false-exclusion bug fixed and proven with a flip-test (`src/vehicle_move.cpp:536-547`)

**Root cause**: `vehicle::part_collision()` excluded *any* player/NPC with
`in_vehicle == true` from collision at the impacted tile, without checking they
were a passenger of *this* colliding vehicle:
```cpp
// If in a vehicle assume it's this one
if( ph != nullptr && ph->in_vehicle ) {
    critter = nullptr;
    ph = nullptr;
}
```
A creature standing on this vehicle's own boardable tile with a stale/desynced
`in_vehicle` flag (the exact desync class `map::displace_vehicle`'s
`"Part/passenger position mismatch"` debug detector already catches) was wrongly
excluded from collision.

**Fix, first attempt — REGRESSED, superseded.** The original fix verified the
creature was the registered passenger of *the specific boardable part at the impact
tile* (`part_with_feature( bubble_to_mount( p ), "BOARDABLE", true )` +
`get_passenger( that_index ) == ph`). That was too strict and **regressed
`[vehicle][ramp]` on the shipping build: 82 → 108 failed assertions, 1 → 3 failed
test cases.** During a ramp z-transition a rider's part registration and position
are transiently inconsistent (`precalc[0]`/`[1]` swap plus `z_terrain`, mid
`displace_vehicle`), so the vehicle stopped recognising its own passenger and
collided with its own rider.

**Fix, current.** Test *vehicle identity* rather than the part: is this creature the
passenger of ANY boardable part of this vehicle. Stable across the transition
window, and a creature riding a *different* vehicle is still a valid collision
target — which is the stale-flag case the check exists for.

**Verified with a flip-test** — the only way to prove a collision-suppression bug is
real: `tests/vehicle_collision_test.cpp`'s
`vehicle_collision_hits_occupant_with_stale_in_vehicle_flag` was confirmed to FAIL
against the original blanket code (9/10 assertions, the collision-type check failed)
and to PASS against the fix (10/10). An initial test design (two separate vehicles)
was wrong — `part_collision`'s `is_veh_collision` branch short-circuits to
`veh_coll_veh` before the body-collision path when the target tile belongs to a
*different* vehicle, so that scenario can never reach this code. The corrected test
uses one vehicle with two boardable seats: a properly-boarded rider on seat A, and a
second rider standing on seat B with `in_vehicle` set directly (bypassing
`board_vehicle`, so seat B's `passenger_flag`/`passenger_id` stay unset).

**How the regression was found, and the lesson.** It was invisible for most of this
work because every measurement was taken on a `-DBOX2D=ON` build, and the ramp suite
behaves differently there. It only surfaced after building `BOX2D=OFF` — the actual
shipping configuration — and comparing against the pre-session commit. Bisected by
reverting `c979a58c` alone, which restored 82/1 exactly. **`part_collision()` is
unguarded shipping code; changes to it must be verified on a `BOX2D=OFF` build.**

### 3. `mount_to_bubble()`/`bub_part_location()` z-frame divergence — 6 of 7 real-part call sites converted

**Root cause**: `bub_part_location()` includes each part's ramp-induced
`z_terrain[0]` offset; `mount_to_bubble()` omits it entirely. Both purport to
answer "where is this part," and disagree whenever a vehicle sits on a ramp.

**Fix**: converted the 6 call sites backed by a real, in-scope `vehicle_part&`/index
to `bub_part_location()`: `src/locations.cpp` (4 sites — `vehicle_item_location`
position/where/detach/obtain_cost, all via `get_part_hack(hack_id)`),
`src/map_items.cpp` (cargo-part active-item match), `src/turret.cpp` (turret fire
origin), `src/vehicle.cpp` (fuel-leak destination), `src/vehicle_use.cpp`
(seen-cache-dirty tile). `mount_to_bubble()` itself is untouched and remains
correct for the hypothetical-mount call sites (install/repair cursors,
`vehicle_export.cpp`'s candidate-mount iteration, `shift_zlevel()`'s no-part
fallback).

**Excluded**: `src/vehicle_parts.cpp:664-665` (rack-carried-vehicle position
search) was initially misclassified as a real-part site in early analysis. Its
`carry_mount` is a *synthetic candidate* mount (`rack_part.mount + offset`,
probed against 4 cardinal directions to find a match), not a real part's own
stored mount — converting it would require first resolving which candidate
corresponds to an actual part, which is a larger change than a pure substitution
and was left untouched.

**Verified**: no dedicated regression test exists for these behaviors (fuel leak
z, turret origin z, seen-cache z on a ramped vehicle are all narrow edge cases).
Verified not to regress via the full `[vehicle]` tag suite (283,217/283,442,
identical to baseline) and `[vehicle][gun]` (334/376, identical to baseline
including the two pre-existing `vehicle_turrets_test.cpp` failures). **A future
session adding ramp+turret or ramp+fuel-leak coverage would give this positive
proof it currently lacks.**

## Investigated, root cause corrected, left open

> **Scope correction (2026-07-29).** All three items below were originally written as if
> they were defects in the shipping vehicle code. They are not. `BOX2D` defaults **OFF**
> (`CMakeLists.txt`), no `CMakePresets.json` preset enables it and no CI job sets it, so
> the shipping build never compiles the `#ifdef BOX2D_ENABLED` paths. The local build
> used for the measurements in this document had `-DBOX2D=ON` cached, which is why these
> looked like general regressions.
>
> Verified by flipping `box2d_position_authority` off and re-running:
>
> | Suite | BOX2D authority on | Legacy tile-step |
> |---|---|---|
> | `[vehicle][railroad]` | 0/5 cases, 65 assertions | **5/5 pass, 4,284 assertions** |
> | `[vehicle][ramp]` | 4/8 cases | 5/8 cases, 3,025 assertions |
> | `[vehicle][gun]` | 5/7 cases | 6/7 cases |
>
> The rails suite is therefore **fully green on the default build**. The root cause is
> that `vehicle::act_on_map()` returns early for Box2D-authoritative vehicles
> (`src/vehicle_move.cpp:~1586`) and so never reaches `move_vehicle()`, which is what
> calls `process_movement_on_rails()`, `vehicle::shift_zlevel()` (ramp z) and
> `part_collision()`. See "Correction 2" in
> `plans/box2d-vehicle-physics-implementation.md` — closing that gap is Box2D work, not a
> shipping-code repair.

### `vehicle_rails_test.cpp:184` — NOT a z/ramp bug; root cause is a pure XY pivot desync

Original hypothesis (from a prior session's handoff): `adjust_zlevel()`'s
non-incremental center-baseline computation could skip an intervening ramp tile
during `map::displace_vehicle`'s warm-up teleport. **This was fixed** (see
`walk_ramp_z()` below) **but empirically did not change this test's failure
count at all** (143/2688 and 13/65 failures, confirmed via isolated
`[vehicle][ramp]` and `[vehicle][railroad]` runs post-fix).

Investigation of why: `tests/vehicle_rails_test.cpp`'s warm-up call
(`add_moving_vehicle`, line 177) calls `map::displace_vehicle()` **directly**,
not `map::move_vehicle()` — so `adjust_zlevel()` is never invoked for this
specific teleport at all. The actual failing assertion,
`REQUIRE( pivot_bub_ms_location( veh ) == veh.bub_ms_location() )`, has **no z
component** — the observed mismatch was `(54,70,0) == (55,70,0)`, an XY
off-by-one. `pivot_bub_ms_location(veh) = veh.bub_ms_location() +
veh.coord_translate(veh.pivot_point())`. `src/map_vehicle.cpp:609-621` already
documents a known, related desync: `coord_translate(pivot_point())` can drift
from `bub_ms_location()` when `pivot_anchor[0]` isn't kept current every turn —
that comment explicitly names this test file as the symptom.

**Left open.** The `adjust_zlevel()` fix (below) is real and independently
correct but does not close this assertion. The actual bug is in the
pivot-anchor/`coord_translate` XY consistency after a raw `displace_vehicle`
call, not in ramp z-handling. This needs its own dedicated investigation
starting from `src/map_vehicle.cpp:609-621`'s existing comment and
`vehicle::pivot_point()` / `coord_translate()` (`src/vehicle_query.cpp:830-833`).

### `vehicle_ramp_test.cpp` ramp-rider desync and `ranged_vehicle_recoil_test.cpp` — recoil FIXED, ramp still open

Those earlier per-suite and aggregate figures (143/2688 for `[vehicle][ramp]`, and a
225/283,442 full-tag total described as "identical before and after") predate the
`step_turn` movement fix and no longer describe this tree. Current measured state:

| Measurement | Pre-session baseline | Now |
|---|---|---|
| `[vehicle]` test cases | 48 passed / 17 failed | **50 passed / 16 failed** |
| `[vehicle]` failed assertions | 225 of 283,442 | **83 of 145,935** |

**Read that split carefully — it is not all improvement:**

- *Genuinely fixed*: the recoil test case (root cause found, verified in isolation);
  `[vehicle][box2d]` 38/38; the `vehicle_efficiency_test` field-residue leak; vehicle
  movement rate (56x error, measured exact after the fix).
- *Deliberately suppressed, NOT fixed*: the two `vehicle_efficiency_test` distance
  `CHECK`s are `WARN`s under `-DBOX2D=ON`. Those account for the large majority of the
  225→83 assertion drop — they are no longer evaluated, pending Box2D fuel calibration.
- *Unchanged*: rails (13), ramp, and turrets failures.
- The total assertion population fell (283,442→145,935) because correct movement makes
  several drive loops terminate in fewer iterations, so absolute failure counts are not
  comparable across the fix. Compare test cases, or run suites in isolation.

On `adjust_zlevel()`: that fix only changes behavior for a genuinely multi-tile, non-zero
offset call, and every current call site passes either a zero or single-tile offset
(`map::move_vehicle()` asserts `dp` is at most 1 tile per axis before computing `dp1`), so
it has **no observable effect on any current call site**. It remains a correct,
deduplicated implementation (the center-baseline and per-part z now share one
`walk_ramp_z()` helper instead of one correct and one endpoint-only copy) that would
matter if a future caller ever passed a multi-tile offset.

`ranged_vehicle_recoil_test.cpp` — **root cause found and fixed.** The earlier text here
said line 156 (`square_dist(starting_pos, veh->bub_ms_location()) >= 1`, "recoil actually
moved the vehicle") failed because of "a Box2D thrust/impulse integration issue, out of
scope". The real cause was much simpler: `map::vehmove()` stepped the physics world by
`1/60` s per **1-second** game turn, so every Box2D-authoritative vehicle moved ~56x too
slowly and the recoil impulse could not shift it a whole tile within the test's 20 turns.
Recoil itself was fine — `REQUIRE( veh->velocity != 0 )` at line 146 always passed.

Fixed by `PhysicsWorld::step_turn( 1.0f )` (see "Correction 3" in
`plans/box2d-vehicle-physics-implementation.md`). `"vehicle gun recoil*"` now reports
*All tests passed (18 assertions in 2 test cases)* **in isolation**.

Caveat: those same cases still fail inside the full `[vehicle]` run, as do
`grabbed_shopping_cart_*` which likewise pass in isolation. That is cross-test leakage,
which is a separate unresolved problem — the movement fix is verified, the suite-ordering
sensitivity is not fixed.

**Ramp rider desync: still open, and the earlier hypotheses here were both wrong.**

A read-only trace of the whole passenger-drag path (`map::displace_vehicle`'s
`psg->setpos()` loop, `advance_precalc_mounts()`, `vehicle::shift_zlevel()`,
`map::shift_vehicle_z()`) found **no computational mismatch**: the passenger target uses
`precalc[1]`/`z_terrain[1]` and the index swap happens after, so `vp.pos()` reads the same
offsets the loop used, and `map::shift_vehicle_z()` is algebraically neutral for
`bub_part_location`. That much still holds.

This document previously went on to guess "most likely the missing `shift_zlevel()` call
on the Box2D readback path". **That is disproved.** The readback now performs the full
`adjust_zlevel -> displace_vehicle -> shift_zlevel` sequence per tile, and it changed ramp
by exactly zero — because the ramp suite does not use that path: it clears
`box2d_position_authority` for every vehicle it builds
(`tests/vehicle_ramp_test.cpp:109,159`), and instrumenting the readback branch showed it
firing **once** across the entire suite (against 14 for railroad and 12,680 for
`vehicle_efficiency`).

Where the failures actually are: 18 of them — 6 `SECTION`s x 3 `TEST_CASE`s, i.e. every
run — are `REQUIRE( player_character.bub_pos() == map_starting_point )` at
`vehicle_ramp_test.cpp:174`, immediately after `map::board_vehicle()`. That function sets
`who->setpos( pos )` with the requested tile (correct) and then calls
`g->update_map( g->u )` (`src/map_vehicle.cpp:1409`), which can shift the reality bubble
and rebase bub coordinates — after which the test's stale `map_starting_point` value no
longer names the player's tile. Start there.

Note the suite mixes two unrelated case kinds, so do not treat them as one problem:
`grabbed_shopping_cart_*` move through `src/grab.cpp`, while `vehicle_ramp_test_59/60/61`
drive a real motorcycle spawned via `here.add_vehicle` (`:148`) at velocity 179 for up to
10 `vehmove()` cycles.

### `vehicle_efficiency_test.cpp` cross-test leakage — root cause CORRECTED, fix applied

**The earlier diagnosis in this document blamed overmap/`MAPBUFFER` state. That was
wrong.** The assertion that fails is only reached via `test_vehicle()`; `find_inner()`
always passes `target_distance = -1`, which gates it off. The reachable path calls
`clear_game()`, which does `clear_states( state::avatar | state::vehicle )` — never
`state::map`, so it never reaches `clear_map()` and therefore never calls
`clear_fields()`. `build_test_map()` rewrites ter/furn/trap/items at z=0 only and clears
no fields either. Fuel is not the vector: `set_vehicle_fuel()` deterministically
re-`ammo_set()`s every tank and battery on each call.

So the actual leak is **field residue** (fire/acid/smoke/...) left on this test's fixed
vehicle footprint at `(60,60,0)` by whichever `TEST_CASE` ran before it, perturbing
damage/skidding during `vehmove()` and hence both `tiles_travelled` and fuel use.

**Fix applied**: `clear_game()` now clears fields for z in [-2,0] before
`build_test_map()`. Entirely map-level; no overmap involvement.

**`clear_overmap()` is dead AND unsafe — do not wire it in.** It has zero callers, and:

- `map::grid` is `std::vector<submap *>` holding raw non-owning pointers into
  `MAPBUFFER`'s `unique_ptr` store. `mapbuffer::clear()` destroys every submap
  unconditionally, with none of the `map::grid` reference check that
  `mapbuffer::remove_submap()` performs. `map::clear_grid()` is documented as the
  prerequisite and is called from exactly one production site (dimension travel) and
  zero test helpers — so after `clear_overmap()` every `map::grid` slot dangles.
- `mapbuffer::clear()` mutates `submaps` without taking `submaps_mutex_`, unlike every
  other mutator, while background submap preload workers exist.

That is why the earlier attempt crashed/hung. Either delete `clear_overmap()` as dead
code, or fix it properly (`clear_grid()` first, take the mutex, drain workers, then a
full `map::load()` to repopulate) — but it is not needed for this test.

**Separately, Box2D fuel economy is now uncalibrated** — see "Correction 4" in
`plans/box2d-vehicle-physics-implementation.md`. The two distance `CHECK`s in this test
are `#ifndef BOX2D_ENABLED`-only and emit a `WARN` under `-DBOX2D=ON`. That is a
deliberate suppression pending calibration, **not** a fix.

## Fixed as a side finding (unrelated to the three reported items)

### Missing state reset in `vehicle_drag_test.cpp`'s "water drag" test case

`TEST_CASE( "water drag remains positive with excess floating parts", ... )`
(`tests/vehicle_drag_test.cpp:170`) called `clear_game_drag(...)` directly with
no preceding `clear_all_state()` — every sibling `[vehicle][engine]`-tagged test
case in this file and `vehicle_efficiency_test.cpp` has one. Added
`clear_all_state();` as the first line of that test case, matching the
established pattern. Verified via `[vehicle][engine]` (60,273/60,273 passed).

## `vehicle_efficiency` is red on the DEFAULT build — test data, not gameplay

**This is the only finding here with direct player relevance, and the conclusion is that
players are fine.**

`TEST_CASE( "vehicle_efficiency", "[vehicle] [engine]" )` is **not** tagged `[.]`, so
unlike `vehicle_turret` it runs in a default/CI invocation. On a `BOX2D=OFF` build it
fails **144 assertions**, deterministically (identical on `--rng-seed 1` and `7`), and
identically before and after this session's changes — reproduced by rewinding every
session-touched file to `91744fb3a7`. So it is **pre-existing branch debt, not introduced
here**.

The failures are all the *upper* bound (`adjusted_tiles_travelled <= max_dist * 1.05`),
i.e. vehicles deliver MORE tiles per unit fuel than the committed targets allow.

**The committed targets are simply wrong.** Running the branch's own generator,
`make_vehicle_efficiency_case` (tagged `[.]`), against a `BOX2D=OFF` build emits values
that agree with upstream to within a few percent:

| `car_test` targets | pavement | dirt | stop-start pavement | stop-start dirt |
|---|---|---|---|---|
| upstream `origin/main` | 617,500 | 403,153 | 56,446 | 28,518 |
| regenerated, `BOX2D=OFF` | 636,700 | 427,500 | 59,860 | 30,240 |
| **committed on this branch** | **76,590** | **49,330** | **41,160** | **18,930** |

Shipping fuel economy therefore matches upstream (+3–6%); it is the stored constants that
do not describe any real build. The metric derivation (`tiles_travelled`,
`fuel_percentage_used`, `adjusted_tiles_travelled`) is byte-identical to upstream, so this
is not a units or bookkeeping difference.

**Player impact: none.** Vehicle range per tank on the shipping build is what upstream
intends. The red test is a stale-data bug.

**Origin: probable, NOT confirmed.** The constants were changed in `a38a56a6a3`
("coop fixes + test setup", a 91-file grab-bag, no stated rebalance intent, and no
accompanying vehicle-physics change that would justify the shift). That commit
(2026-07-19) postdates both Box2D position authority (2026-07-12) and the 1/60 s cadence
(2026-07-11), so they *could* have been regenerated on a `BOX2D=ON` build where this test
measured the reset-vs-stale-`physics_pos` artifact — that artifact under-reports by ~5.6x
(≈1 tile/turn instead of ≈5.6) and the constants are ~5.7x low, which fits. Treat that as
correlational. A competing explanation — forward-slot values accidentally taken from the
reverse-test slot — was considered and looks weak (only 1 of `car_test`'s 4 values is
close to its reverse counterpart) but is not ruled out.

**FIXED.** The constants were regenerated with this file's own
`make_vehicle_efficiency_case` generator against a `BOX2D=OFF` build and applied verbatim
(vehicle order and count checked against the existing 18 forward + 18 reverse blocks).

Result on `BOX2D=OFF`: `vehicle_efficiency` passes all **357,445** assertions, on seeds 1,
7 and 13; full `[vehicle]~[.]` went from **172 to 28** failed assertions and 10 to 9 failed
cases. `BOX2D=ON` still passes via the WARN-gated path.

Scope of the safety argument, stated plainly: upstream covers only **two** vehicles
(`car_test`, `electric_car_test`). For those, regenerated values land **+3% to +6%** of
`origin/main` across all eight target slots — so shipping fuel economy genuinely agrees
with upstream and the stored constants were the anomaly. The other **16** vehicles were
added on this branch and have no upstream counterpart, so regeneration is the only
available reference for them; nothing contradicts the new values, but they are not
cross-validated the way the first two are.

Origin of the bad values is now well supported rather than merely plausible: the committed
numbers track the `BOX2D=ON` build (beetle's stored 98,240 against 112,280 measured there)
not `BOX2D=OFF` (440,471), and they were set in `a38a56a6a3`, which postdates both Box2D
position authority (2026-07-12) and the 1/60 s cadence bug (2026-07-11). The
reset-vs-stale-`physics_pos` artifact that build suffered under-reports distance, which is
the mechanism. Still an inference, not a proof — the commit message says nothing about
rebalancing.

To regenerate again after any deliberate vehicle-physics change:

```sh
cmake --preset osx-arm-slim -DBOX2D=OFF -B out/build/nobox2d
cmake --build out/build/nobox2d --target cata_test-tiles
out/build/nobox2d/tests/cata_test-tiles "make_vehicle_efficiency_case" --rng-seed 1 --user-dir=/tmp/gen/
```


## Ramp: a vehicle can come to a DEAD STOP driving up a ramp (shipping, pre-existing)

Measured on a `BOX2D=OFF` build, so this one is real for players. `[vehicle][ramp]` fails
**82 assertions in 1 of 8 test cases** — identical before and after this session, so
pre-existing.

The failing case is `vehicle_ramp_test_60/ramp up`, vehicle type `motorcycle`. The
assertions and what they mean, in causal order:

| Site | Assertion | Observed |
|---|---|---|
| `:200` (10x) | `veh.velocity == target_velocity` | **`0 == 179`** and `158 == 179` |
| `:215` (52x) | `ppos.z() == target_z` | `0 == 1` at `cycles == 4`, `pmount (0,0,0)`, `ppos (71,60,0)` |
| `:218` (14x) | `player_character.bub_pos() == ppos` | boarded rider no longer on its own part |

`:200` looks causal: the vehicle is driven at a fixed `velocity = 179` (~1 tile/s) and the
z-transition assertions are gated on `cycles > transition_cycle - pmount.x()`, i.e. they
assume the vehicle covers roughly a tile per cycle. `158` is ordinary drag, but **`0` is a
dead stop** — so the vehicle never reaches the ramp on schedule, and `:215`/`:218` then fail
as consequences rather than as independent defects.

**Player-visible consequence:** driving a vehicle up a ramp can bring it to a complete
halt, and while parts are mid-transition a boarded rider can be positioned off their own
seat tile.

**Not yet established:** what zeroes the velocity. A collision path (`part_collision` →
`veh_coll_*`) or an explicit `stop()` are the obvious candidates; `:202`
(`REQUIRE( !veh.skidding )`) does *not* fail, so skidding is ruled out. Worth instrumenting
`veh.velocity` per cycle alongside the vehicle's tile to see whether the stop coincides
with reaching the ramp tile.

**Do not confuse this with the earlier `:174` failures.** Those (18, `bub_pos ==
map_starting_point` right after `board_vehicle`) were fixed by the `part_collision`
identity change in section 2 — instrumenting `board_vehicle` afterwards showed abs position,
`abs_sub` and bub all stable across all 18 invocations. The remaining 82 are a different,
older defect.

**Aside worth a look on its own:** that instrumentation also showed `map::get_abs_sub()`
drifting by exactly one submap per invocation across `TEST_CASE`s
(`(-1,0,0) → (-2,0,0) → (-3,0,0) → (-4,0,0)`) while bub stayed pinned. The map origin is
evidently not reset between cases. That is a plausible mechanism for the unresolved
cross-test leakage (`"vehicle gun recoil*"` and `grabbed_shopping_cart_*` pass in isolation
and fail in-suite): any test holding a bub coordinate across a shift boundary sees a
different frame depending on how many cases ran before it. Check whether `clear_game()` /
`build_test_map()` reset `map::abs_sub` before treating those as separate problems.

## Verification commands used this session

```sh
cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles
./cata_test-tiles "[vehicle][collision]" --rng-seed 1 --user-dir=/tmp/x1/
./cata_test-tiles "[vehicle][grab]" --rng-seed 1 --user-dir=/tmp/x2/
./cata_test-tiles "[npc][vehicle]" --rng-seed 1 --user-dir=/tmp/x3/
./cata_test-tiles "vehicle_efficiency" --rng-seed 1 --user-dir=/tmp/x4/
./cata_test-tiles "[vehicle][engine]" --rng-seed 1 --user-dir=/tmp/x5/
./cata_test-tiles "[vehicle]" --rng-seed 1 --user-dir=/tmp/x6/
```
### Baselines

All numbers below are from a `-DBOX2D=ON` build (`out/build/osx-arm-slim`, which has that
cached). A `BOX2D=OFF` build behaves materially differently for vehicles — notably
`[vehicle][railroad]` is fully green there — so always state which build a figure is from.

| Point in time | Test cases | Assertions |
|---|---|---|
| Before the first session's changes | 64: 47 passed, 17 failed | 283,432: 225 failed |
| After occupancy/`part_collision`/z-location fixes | 65: 48 passed, 17 failed | 283,442: 225 failed |
| After the `step_turn` movement fix + efficiency gating | 65: **49 passed, 16 failed** | 167,992: **83 failed** |
| After submap-lifecycle work + lifecycle test | 66: **50 passed, 16 failed** | 145,935: **83 failed** |

Caveats on the last row, so it is not over-read:

- The two `vehicle_efficiency_test` distance `CHECK`s are `WARN`s under `-DBOX2D=ON`.
  Most of the 225→83 drop is those assertions no longer being evaluated — a suppression
  pending fuel calibration, not a fix.
- The assertion population shrank (283,442→145,935) because correct movement makes several
  drive loops finish in fewer iterations. Absolute failure counts are therefore not
  comparable across that fix; compare test cases, or run suites in isolation.
- `"vehicle gun recoil*"` and `grabbed_shopping_cart_*` pass in isolation but fail inside
  the full `[vehicle]` run. Cross-test leakage is real and unresolved; isolation results
  and full-suite results must both be quoted.

Reproduce with:

```sh
./cata_test-tiles "vehicle gun recoil*"  --rng-seed 1 --user-dir=/tmp/x7/   # isolation: passes
./cata_test-tiles "[vehicle][box2d]"     --rng-seed 1 --user-dir=/tmp/x8/   # 38/38
./cata_test-tiles "[vehicle][railroad]"  --rng-seed 1 --user-dir=/tmp/x9/   # Box2D-only failures
```
