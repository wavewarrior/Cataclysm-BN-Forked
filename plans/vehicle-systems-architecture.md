# Vehicle systems architecture: session record

## Status: partially complete, verified

This session investigated three previously-reported vehicle defects (boarded-rider
ramp desync, `vehicle_efficiency_test` cross-test leakage, `vehicle_rails_test.cpp:184`
regression) plus a newly-found occupancy-resolution architecture gap. **Two of the
four investigated issues were fixed and verified; two were investigated, found to
have a different root cause than initially hypothesized, and left open with accurate
findings recorded below** rather than shipped as unverified guesses. This document
is the permanent record; the original research/verdict on Project-Zomboid-style
vehicle occupancy is at the top of this repo's git history for this session (see
the companion analysis referenced in commit messages) and is not repeated here.

The in-flight Box2D physics-authority migration
(`plans/box2d-vehicle-physics-implementation.md`) remains the authoritative plan for
the physics layer; nothing here duplicates it.

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
`[vehicle][engine]` (60,273/60,273, covers `vehicle_efficiency` + `vehicle_drag` +
the standalone "water drag" case), full `[vehicle]` tag (283,217/283,442 passed,
identical to pre-change baseline — zero regressions, zero new failures among the
17 pre-existing failing test cases).

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

**Fix**: verify the creature is actually the registered passenger of the specific
boardable part at the impact tile (`part_with_feature(bubble_to_mount(p), "BOARDABLE", true)`
+ `get_passenger(that_index) == ph`) before excluding them.

**Verified with a flip-test** — the only way to prove a collision-suppression bug
is real: `tests/vehicle_collision_test.cpp`'s new
`vehicle_collision_hits_occupant_with_stale_in_vehicle_flag` was built, confirmed
to FAIL against the original buggy code (9/10 assertions passed, the collision
type check failed), then confirmed to PASS against the fix (10/10). Note: an
initial test design (two separate vehicles) was wrong — `part_collision`'s
`is_veh_collision` branch (line ~552) short-circuits to `veh_coll_veh` before the
body-collision path when the target tile belongs to a *different* vehicle, so
that scenario can never reach this fix. The corrected test uses one vehicle with
two boardable seats: a properly-boarded rider on seat A, and a second rider
standing on seat B with `in_vehicle` set directly (bypassing `board_vehicle`, so
seat B's `passenger_flag`/`passenger_id` remain unset) — this reaches the actual
code path the fix changes.

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

### `vehicle_ramp_test.cpp:218,239` and `ranged_vehicle_recoil_test.cpp:157` — boarded-rider ramp desync, unaffected by any fix in this session

Current, reproducible state (confirmed deterministic across 3 repeated runs
of `[vehicle][ramp]`): 143/2688 failed assertions. A mid-session figure of 145
was recorded at an earlier, different build state and is not a reliable
pre-fix baseline — don't treat it as one. The reliable before/after evidence
that `adjust_zlevel()`'s fix caused no regression is the full `[vehicle]` tag
aggregate (see "Verification commands" below): 225/283,442 failed assertions
at the true pre-session baseline vs. 225/283,442+10 (the 10 all from the new
regression test, all passing) after every change in this session — identical
failure count. The
`adjust_zlevel()` fix only changes behavior for a genuinely multi-tile,
non-zero offset call — and every current call site either passes a zero offset
or a single-tile offset (`map::move_vehicle()` asserts `dp` is at most 1 tile
per axis before computing `dp1`), so the fix has **no observable effect on any
current call site**, including this one. It remains a correct, deduplicated
implementation (the center-baseline and per-part z now share one
`walk_ramp_z()` helper instead of one correct and one buggy copy of the same
logic) that would matter if a future caller ever passed a multi-tile offset.

`ranged_vehicle_recoil_test.cpp:157`'s assertion currently passes *trivially*:
line 156 (`square_dist(starting_pos, veh->bub_ms_location()) >= 1`, i.e. "recoil
actually moved the vehicle") fails first — the vehicle doesn't move at all, so
the player-position-matches-vehicle check at 157 has nothing to disagree with.
Fixing 157 meaningfully requires first fixing 156 (a Box2D thrust/impulse
integration issue, out of scope for this session).

**Left open**, requiring a dedicated investigation into where a boarded rider's
position is actually written relative to `precalc[1]`/`z_terrain[1]` across a
ramp transition — starting point: instrument `map::displace_vehicle`'s
`psg->setpos(psgp)` call (`map_vehicle.cpp:~1508`) across
`vehicle_ramp_test.cpp`'s `transition_cycle` boundary.

### `vehicle_efficiency_test.cpp` cross-test leakage — root cause diagnosed, fix attempt reverted as unsafe

**Root cause** (confirmed): `clear_overmap()` (`MAPBUFFER.clear();
ACTIVE_OVERMAP_BUFFER.clear();`, `tests/map_helpers.cpp:127-131`) has zero
callers anywhere in the test suite — dead code. `tests/state_helpers.cpp`'s
`full_test_state()` bitset has no representation for overmap/mapbuffer state, so
`clear_all_state()` can never reach it. Confirmed reproducible: `vehicle_efficiency`
passes in isolation (59,549/59,549) but produces real failures
(`vehicle_efficiency_test.cpp:265`) when run after other `[vehicle]`-tagged tests
in the same process.

**Fix attempted and reverted.** Wiring `clear_overmap()` into the test harness
(as either a blanket default or an opt-in `clear_all_state_with_overmap()` called
once per `vehicle_efficiency` `TEST_CASE`) caused the test to fail or hang even
when correctly ordered before `state::map`'s dispatch (to avoid the map holding
dangling submap pointers into a freshly-cleared `MAPBUFFER`). The exact
interaction is not understood — clearing `MAPBUFFER`/`ACTIVE_OVERMAP_BUFFER` at
a `TEST_CASE` boundary appears to trigger either a crash or extremely expensive
overmap regeneration that this session could not safely root-cause within budget.
**All scaffolding for this attempt (the `overmap` enum bit, the dispatch block,
`clear_all_state_with_overmap()`) was fully reverted** rather than ship something
broken. `tests/state_helpers.cpp`/`.h` are byte-identical to their pre-session
state.

**Left open**, requiring careful, incremental investigation of exactly what
`clear_overmap()` invalidates that the live `map`/reality-bubble still depends
on, ideally with ASAN/debugger attached rather than trial-and-error against a
300+ second test binary. The diagnosis above (which test-harness gap causes the
leak) is solid and does not need to be re-derived; only the fix implementation
needs a safer approach.

## Fixed as a side finding (unrelated to the three reported items)

### Missing state reset in `vehicle_drag_test.cpp`'s "water drag" test case

`TEST_CASE( "water drag remains positive with excess floating parts", ... )`
(`tests/vehicle_drag_test.cpp:170`) called `clear_game_drag(...)` directly with
no preceding `clear_all_state()` — every sibling `[vehicle][engine]`-tagged test
case in this file and `vehicle_efficiency_test.cpp` has one. Added
`clear_all_state();` as the first line of that test case, matching the
established pattern. Verified via `[vehicle][engine]` (60,273/60,273 passed).

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
Full-`[vehicle]`-tag baseline (before any change in this session):
64 test cases, 47 passed, 17 failed; 283,432 assertions, 283,207 passed, 225 failed.
Full-`[vehicle]`-tag after all changes in this session: 65 test cases (+1, the new
regression test, passing), 48 passed, 17 failed (unchanged); 283,442 assertions
(+10, the new test's assertions, all passing), 283,217 passed, 225 failed
(unchanged). Zero regressions.
