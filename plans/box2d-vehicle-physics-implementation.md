# Box2D Physics Authority Implementation Plan

## Overview

This plan migrates Cataclysm-BN's vehicle and combat systems to Box2D 3.0.0 as the **single physics/collision authority**.

**End State**: Box2D owns collision detection, spatial queries, impulse resolution, and vehicle movement integration. The tile-step movement model, bespoke 1D elastic collision formulas, manual angular impulse calculations, and tile-traversal LOS are retired. One persistent Box2D world is the source of truth for all physical simulation.

---

## Remaining Work  *(rewritten 2026-07-29 after measurement — supersedes the 2026-07-13 list)*

**The previous dependency chain (playtest → Phase 12 → Phase 10 Step 6 → Phase 11) was
wrong, and Phase 10 Step 5 is NOT done.** Three claims in the older text were checked
against the code and disproved. Read this section before touching anything below; the
per-phase text further down still carries the old assumptions.

### Correction 1 — Phase 12-A must NOT be executed as written

12-A says to delete the transient VV solver (`src/physics/veh_box2d_solve.*`) because
it is "superseded by Phase 10 contact events". It is not. `src/physics/filter_bits.h`
sets `vehicle_group = -1` and `make_vehicle_body()` assigns
`sdef.filter.groupIndex = physics::vehicle_group`, and a shared negative `groupIndex`
in Box2D means those shapes **never** collide with each other. Vehicle-vehicle contact
events therefore cannot fire at all, by construction — the filter comment even says so.
`solve_vv_cluster()` (called from `map::move_vehicle()`, `src/map_vehicle.cpp:~795`) is
the only source of vehicle-vehicle collision physics that exists. Deleting it would
remove T-bone spin entirely, i.e. the original motivation for this whole plan.

**Updated:** the transient solver *was* unreachable for authority vehicles, because
`vehicle::act_on_map()` returns early before `move_vehicle()`. That is no longer true —
the readback walk in `map::vehmove()` now routes each tile step through
`move_vehicle()`, which is where `solve_vv_cluster()` lives, so the transient VV solver
is reachable again and verified working (`box2d_authority_vehicle_hits_vehicle` in
`tests/vehicle_collision_test.cpp`).

That strengthens rather than removes the argument against 12-A: the transient solver is
now the *live, tested* VV path, so deleting it would break vehicle-vehicle collision
outright.

### Correction 2 — Phase 10 Step 5 left the whole game-consequence layer unimplemented

`act_on_map()`'s early return skips `part_collision()` **and** `move_vehicle()`. Under
`-DBOX2D=ON` that silently drops, for every Box2D-authoritative vehicle:

| Dropped behaviour | Owner that no longer runs |
|---|---|
| creature collision + damage (`veh_coll_body`) | `part_collision()` |
| terrain bashing (`veh_coll_bashable`) | `part_collision()` |
| vehicle-vehicle collision | `solve_vv_cluster()` in `move_vehicle()` |
| ramp z-transition | `move_vehicle()` → `vehicle::shift_zlevel()` |
| rail following | `vehicle_movement::process_movement_on_rails()` |

**RESOLVED — but not via `dispatch_contact_events()`.** That function is still a stub, and
deliberately so. The readback walk now routes every tile step through `move_vehicle()`,
which already performs the whole per-tile consequence chain, so all five dropped
behaviours are restored by reuse rather than reimplementation:

| Behaviour | Now delivered by |
|---|---|
| creature collision + damage | `part_collision()` via `move_vehicle()` |
| terrain bashing | `part_collision()` via `move_vehicle()` |
| vehicle-vehicle collision | `solve_vv_cluster()` via `move_vehicle()` |
| ramp z-transition | `adjust_zlevel()` / `shift_zlevel()` via `move_vehicle()` |
| rail following | still open — `process_movement_on_rails()` is not on this path |

Verified by `box2d_authority_vehicle_bashes_terrain` (drives into a wooden wall, wall
becomes `t_rock_floor_no_roof`) and `box2d_authority_vehicle_hits_vehicle` (struck vehicle
shoved or damaged, striker slowed).

`dispatch_contact_events()` therefore has a much smaller remaining job — sound, and any
purely physical response with no tile-step equivalent. It is explicitly **not** where bash
force belongs.

Consequence, measured by disabling `box2d_position_authority` and re-running:

| Suite | BOX2D authority on | Legacy tile-step |
|---|---|---|
| `[vehicle][railroad]` | 0/5 cases, 65 assertions | **5/5 pass, 4,284 assertions** |
| `[vehicle][ramp]` | 4/8 cases | 5/8 cases, 3,025 assertions |
| `[vehicle][gun]` | 5/7 cases | 6/7 cases |

So the rails suite is not a shipping bug at all — it is fully green on the default build
and fails only under Box2D position authority. Same direction for ramp and gun.

A per-turn "revoke authority when on rails / on a ramp" gate was implemented and
**reverted**: it does not help, because the rails failure happens during test *setup*
(`add_moving_vehicle` calls `map::displace_vehicle()` directly, before any `vehmove()`),
and it made `[vehicle][ramp]` worse (150 → 470 failed assertions). The gap has to be
closed by implementing the consequences, not by dodging them.

### Correction 3 — the "one step per 1/60 s" cadence was a 56x movement bug (FIXED)

The 2026-07-28 addendum below concluded that `map::vehmove()` must step exactly once by
`1/60` s and that integrating a full second doubles vehicle speed. That was a
misdiagnosis. Measured with a temporary instrumented case, at `velocity = 1000` cm/s
(10 m/s):

- `physics_pos` advanced **0.0932 tiles/turn** and the vehicle covered **2 tiles in 20
  turns**, against `10 m/s x 20 s / 1.78816 m-per-tile` = **111.85 tiles** expected.

`vehmove()` now calls `PhysicsWorld::step_turn( 1.0f )`, which sub-steps internally with
a step size chosen to keep per-step translation under half a tile (no tunneling through
the 1-tile terrain bodies) and syncs velocity **in once / out once** so the contact
solver's result is not overwritten mid-turn — the old per-step re-sync meant a collision
could never change a vehicle's velocity. Re-measured after the fix: **112 tiles in 20
turns** vs 111.85 expected, for both a car and a fire truck, on both pavement and dirt.

The reason `vehicle_efficiency_test` did not catch a 56x error: it teleports the vehicle
back to its start every turn *without* resyncing `physics_pos`, so it was measuring a
reset-vs-stale-`physics_pos` artifact rather than distance. `on_vehicle_moved()` now
follows external teleports and reseats `physics_pos`, using an explicit
`PhysicsWorld::physics_move_scope` guard so the readback's own move is still
distinguished from an external one.

### Correction 4 — Box2D fuel economy is uncalibrated

With movement now correct, `vehicle_efficiency_test`'s distance constants (which encode
the legacy mover's `of_turn` truncation loss — `gain_moves()` zeroes `of_turn_carry`, so
the tile-step path discards a sub-tile remainder every turn) no longer describe the
Box2D path. Measured deviation: most vehicles **+0.3% to +4.3%** over the upper bound,
but `fire_truck_test` on dirt lands **~40% under** the lower bound. Raw movement was
ruled out as the cause (see Correction 3 — exact for the fire truck too).

Those two distance assertions are now `#ifndef BOX2D_ENABLED`-only, and emit a `WARN`
with the measured band under `-DBOX2D=ON`. Every other assertion in that test stays
strict on both builds. **This is a suppression, not a fix** — do not read the improved
assertion count as calibration. Deliberately calibrating Box2D fuel economy (and
diagnosing the fire-truck outlier) is open work.

### TOP PRIORITY — Box2D costs ~7x fuel economy in sustained cruise

**Quantified, upstream-validated, and player-facing.** `BOX2D` is always on for this
project, so this is the shipping configuration.

Regenerating `vehicle_efficiency`'s constants with its own
`make_vehicle_efficiency_case` generator on both builds and comparing against
`origin/main`:

| `car_test` | pavement | dirt |
|---|---|---|
| upstream `origin/main` | 617,500 | 403,153 |
| `BOX2D=OFF` (tile-step) | 636,700 | 427,500 |
| **`BOX2D=ON` (physics authority)** | **87,530** | **56,690** |

`BOX2D=OFF` agrees with upstream to within **3-6%** across all eight comparable slots, so
the tile-step numbers are physically correct. `BOX2D=ON` returns **0.10-0.43x** of them in
sustained cruise — typically ~0.13x, i.e. **roughly 7x less range per unit fuel** — across
every one of the 18 test vehicles.

Crucially the loss is **not uniform**, which rules out a units or bookkeeping artifact:

| Measurement | ON / OFF |
|---|---|
| sustained cruise, pavement | 0.10 - 0.43 |
| sustained cruise, dirt | 0.03 - 0.73 |
| stop-start, pavement | 0.69 - 0.90 |
| stop-start, dirt | 0.69 - 0.97 |

Stop-start cases lose only ~10-30%; sustained cruise loses ~87%. So the defect is in
**steady-state economy** — drag, rolling resistance, or engine load at constant speed under
physics authority — not in acceleration or in the measurement.

**Localised to fuel burned at steady speed.** Instrumenting the numerator and denominator
of `adjusted_tiles_travelled = tiles_travelled / fuel_percentage_used` separately, plus
cycle count and end velocity, on both builds:

| `Car`, pavement | BOX2D=ON | BOX2D=OFF | ratio |
|---|---|---|---|
| `tiles_travelled` | 1200 | 1247 | 0.96 |
| `fuel_used` | 0.137083 | 0.019583 | **7.00** |
| `vel_end` | 2235 | 2235 | 1.00 |
| `cycles_run` | 100 | 100 | 1.00 |

Distance, end velocity and cycle count all match. The vehicle is **not** settling at a
lower cruise speed, **not** exiting the loop early, and **not** moving less per turn — its
per-turn distance tracks `vel_end` exactly on both builds (12 tiles/cycle against
22.35 m/s / 1.78816 = 12.5 expected). The entire ratio is fuel.

The stop-start rows pin it further: `Beetle` stop-start burns **identical** fuel on both
builds (0.090833), and only the cruise rows diverge. So **acceleration fuel is correct and
fuel burned holding a steady speed is ~7x too high** under position authority.

**ROOT CAUSE FOUND AND FIXED: `move` was never updated under position authority.**

`vehicle::slowdown()` (`src/vehicle_move.cpp:113`) multiplies rolling drag by
`1 + 24 * |sin( face.dir() - move.dir() )|` — up to **25x** — to model a vehicle sliding
sideways. `move` is set by `veh.move = facing` in `map::move_vehicle()`
(`src/map_vehicle.cpp:978`), which `act_on_map()` never reaches under authority. So `move`
kept its constructed heading of 0 degrees while `face` tracked the vehicle: a vehicle
driving due west (`face` 270, `move` 0) was charged the **full 25x skid penalty every turn
while travelling perfectly straight**.

Measured on `car_test` at cruise, before and after:

| | before | after | tile-step reference |
|---|---|---|---|
| `slowdown` per turn | 362 | **67** | 67 |
| `face` / `move` | 270 / 0 | **270 / 270** | 270 / 270 |
| `vel` at cruise | 1873 | 2235 | 2235 |
| `load` (cruise thrust) | 533 | 114 | 114 |

Chain: inflated drag held the vehicle 362 cm/s below its cruise target permanently;
`thrust()` clamps `vel_inc` to the remaining gap (`vehicle_move.cpp:216`) and
`load = 1000*|vel_inc|/accel`, so cruise control burned 533 every turn instead of 114 —
~4.7x per turn compounding to the ~7x fuel measured over a 100-cycle run. It also explains
the stop-start parity: during acceleration `vel_inc` is not cruise-clamped, so `load`
saturates near 1000 regardless.

Fixed in `PhysicsWorld::sync_game_from_bodies()` by deriving `move` from the body's linear
velocity (the actual travel vector, so a genuine slide still registers as a skid), falling
back to `face` below 0.05 m/s where the direction is numerical noise.

Ruled out along the way, each by measurement: `traction` (identical 1.0 on both builds),
engine power (`accel` was *higher* on ON), `coeff_air_drag` / `coeff_rolling_drag`
(byte-identical), a lower cruise equilibrium (`vel_end` identical), early loop exit
(`cycles_run` 100 both), and distance (`tiles` within 4%). The skid multiplier is applied
inside `slowdown()` rather than inside the drag coefficients, which is why identical
coefficients did not exonerate drag.

**Residual: 3-8%, no longer systemic.** `vehicle_efficiency` went from **101** failed
assertions to **53**, and the remaining gaps are near-misses (`beetle_test` 77,965 against a
required 80,346 — 3.0% short; another row 8.0% short) rather than multiples. For scale, the
tile-step build itself sits 3-6% from upstream. Whether the remainder is a second small
defect or just continuous-vs-quantised integration is open; it is no longer a
balance-breaking regression.

**This regression was previously hidden twice over.** The committed constants (76,590 for
`car_test`) sit beside the ON value, so they were generated under `BOX2D=ON` and baked the
regression into the baseline — which is why they read ~8x below upstream. And the checks
themselves had been `WARN`-gated under `BOX2D_ENABLED`, so nothing asserted them. Both are
now fixed: constants are the upstream-validated OFF values and the checks are strict and
unconditional.

`vehicle_efficiency` therefore **fails on purpose**: 101 assertions, 99 of them the lower
bound (too little distance per fuel). Left red deliberately. Do not re-baseline it to the
ON numbers — that is exactly how the regression was concealed the first time.

Start from `vehicle::gain_moves()` (`src/vehicle.cpp`, where `slowdown()` and thrust are
applied per turn) and compare fuel drawn per tile between the two paths at a fixed cruise
speed.

## Terrain colliders are built against the WRONG player z — start-location dependent

**Promoted to the top open item.** With `BOX2D` always on, this decides whether vehicles
collide with walls at all.

`map::on_submap_loaded` only forwards to the physics world when the submap's z matches the
player's current z (`src/map.cpp:378`):

```cpp
if( phys_world && sm != nullptr && g && p.z() == g->u.bub_pos().z() ) {
    phys_world->on_submap_loaded( *this, p );
}
```

Two ordering facts make that gate unreliable:

1. `game::place_player_overmap` calls `load_map( map_sm_pos )` at `src/game.cpp:2586` and
   `place_player( player_pos )` only at **2589**. So every submap notification during the
   load is compared against the player's **previous** z, not the destination z. Nothing
   rebuilds colliders after `place_player()`.
2. `PhysicsWorld::on_zlevel_changed` — the only code that rebuilds colliders for a new
   z-level — has exactly **one** caller, `game::vertical_shift`
   (`src/game_movement.cpp:2379`). It fires for stairs/ramps and nothing else.

There are **two distinct triggers**, with different confidence levels and different repro
steps. Do not conflate them, and do not close this item by testing only one.

**(a) Fast travel / overmap teleport across a z-level — follows directly from the verified
ordering, no extra assumption.** `place_player_overmap` computes the destination as
`player_pos( u.bub_pos().xy(), map_sm_pos.z() )` (`src/game.cpp:2585`), i.e. the new z comes
from `map_sm_pos`. It then runs `load_map()` *before* `place_player()`, so every submap
notification during that load is tested against the player's genuine **old** z. Whenever the
destination z differs from the origin z, the gate fails for every submap on the destination
level and no colliders are built there. Nothing rebuilds them afterwards. **Repro this
first** — it needs no assumption about initialisation order.

The same load-before-place shape appears elsewhere, e.g. `gamemode_defense.cpp:296-299`
(`g->load_map( ... )` then `player_character.setpos( ... )`), so this is a pattern rather
than a one-off.

**(b) Initial game start — plausible but UNVERIFIED.** This only fails if `g->u`'s z is
still 0 or unset at `load_map()` time, which nobody has checked; in `place_player_overmap`
the player normally has a real previous position rather than a default-constructed one. If
it is unset, an underground start (shelter basement, any z<0 scenario) would load z<0
submaps against a 0 and build nothing, while a surface start would pass the comparison
coincidentally. Verify `g->u.bub_pos().z()` at the moment of the first `load_map()` before
relying on this. **A next session that tests only an underground start could see no bug — if
initialisation happens to be fine by then — and wrongly close the item.**

Status: the ordering above is **verified by reading the code**. The runtime consequence is
`[INFERENCE]` from that ordering and has not been confirmed in a running game — measured
evidence so far is only that the Catch2 harness has `terrain_bodies_.size() == 0`, which is
a different (and also real) path since `build_test_map()` never fires the notification.

**Confirm first, then fix.** Launch with an underground start and log
`terrain_bodies_.size()` after load; separately log the gate's two operands at each
notification. Likely fix is to notify the physics world after the player is placed rather
than during the load, or to drop the z gate and let `on_submap_loaded` decide for itself
which z-levels are worth bodies. Note the gate exists for a real reason — the comment at
`src/map.cpp:375-377` records that building bodies for all 21 z-levels was the dominant cost
of submap boundary crossing — so removing it needs a cheaper filter, not none.

Everything in `dispatch_contact_events()` is downstream of this: contact events cannot fire
against colliders that were never created.

## Which paths are actually exercised — measure before aiming a fix

The readback tile-walk in `map::vehmove()` is not uniformly hit. Counting entries into
the `px != cur.x() || py != cur.y()` branch per suite:

| Suite | Readback branch entries |
|---|---|
| `vehicle_efficiency` | 12,680 |
| `[vehicle][railroad]` | 14 |
| `[vehicle][ramp]` | **1** |

`[vehicle][ramp]` is 1 because **the ramp test opts out of Box2D position authority**:
`tests/vehicle_ramp_test.cpp:109` (grabbed-cart setup) and `:159` (driven-vehicle setup)
both clear `veh.box2d_position_authority` inside `#ifdef BOX2D_ENABLED`, for every vehicle
they build. So none of the ramp failures are readback or contact-event problems.

That opt-out used to be **incomplete**, which resolves an apparent contradiction worth
recording: disabling authority globally moved isolated ramp from 4/8 to 5/8, even though
the suite had already opted out — which should have been a no-op.
`PhysicsWorld::sync_game_from_bodies()` iterated every registered vehicle with no
authority check, so opted-out vehicles kept their tile position but still had
`physics_angle` overwritten and `precalc[]` re-derived from a body the game was not
driving, corrupting their part layout. Position/facing/precalc are now gated on authority
(the `angular_velocity_rads` readback stays unconditional, since Box2D damping is what
decays a spin the legacy collision paths wrote). That took isolated ramp from 150 to 109
failed assertions. The suite now genuinely does run the legacy mover.

What those failures actually are: 18 of them (6 `SECTION`s x 3 `TEST_CASE`s, i.e. every
run) are `REQUIRE( player_character.bub_pos() == map_starting_point )` at
`vehicle_ramp_test.cpp:174`, immediately after `map::board_vehicle()`. `board_vehicle()`
does `who->setpos( pos )` with the requested position (correct) and then
`g->update_map( g->u )` (`src/map_vehicle.cpp:1409`), which can shift the reality bubble
and rebase bub coordinates — after which the test's stale `map_starting_point` value no
longer names the player's tile. Investigate there; it is unrelated to physics.

Note the suite mixes two very different kinds of case, so do not treat them together:
- `grabbed_shopping_cart_*` — a grabbed/pushed cart moved through `src/grab.cpp`, which
  already calls `adjust_zlevel()` / `shift_zlevel()` (`grab.cpp:217,248`).
- `vehicle_ramp_test_59/60/61` — a real **driven** motorcycle spawned via
  `here.add_vehicle` (`vehicle_ramp_test.cpp:148`), set to `velocity = 179` and stepped
  through up to 10 `here.vehmove()` cycles. These are ordinary driven vehicles, not grab
  cases, and they too run on the legacy mover because of the line-159 opt-out.

`[vehicle][railroad]`'s failure at `vehicle_rails_test.cpp:184` is likewise not reachable
from the readback: it happens in the warm-up helper `add_moving_vehicle`, which calls
`map::displace_vehicle()` **directly** at line 177 before any `vehmove()`.

Confirm a path is exercised (a temporary counter in the branch is enough) before investing
in it. Two authority-gating attempts were burned on this: one gating per turn only, one
gating at spawn as well; neither moved rails, and both made ramp worse.

### Revised order of work

**Done** (see git log): full-turn integration with tunneling-safe sub-steps; velocity
synced in-once/out-once so contact response survives; `physics_pos` reseated on external
teleports; readback walks one tile at a time with `adjust_zlevel -> displace_vehicle ->
shift_zlevel` per tile and rewinds `physics_pos` on every early exit.

**Remaining:**

0. **Make terrain bodies exist — newly found, and it precedes everything below.**
   Measured in the Catch2 harness during `vehmove()`: `terrain_bodies_.size() == 0`
   and `bashable_tile_bodies_.size() == 0`, i.e. the physics world contains **no
   terrain colliders at all**, so `b2World_GetContactEvents().beginCount` is always
   0 and no vehicle-terrain contact can happen. Terrain bodies are built only from
   `map::on_submap_loaded` (`src/map.cpp:378`, gated on
   `g && p.z() == g->u.bub_pos().z()`), and `build_test_map()` mutates
   already-resident submaps without firing that notification.

   Consequences:
   - `[vehicle][box2d]`'s 38 passing assertions are raw-Box2D unit tests against a
     throwaway world; **nothing** currently integration-tests VT contact.
   - Runtime terrain changes are invisible to physics regardless: `ter_set()` and
     `furn_set()` have no physics hook, so a wall smashed or built mid-game keeps
     or lacks a collider until its submap reloads.
   - **Possible live bug, worth checking first:** if the initial reality bubble
     loads its submaps before `g->u` has a position, the `p.z() == player z` gate
     fails and a real game also starts with no terrain colliders.

   Fix order: give test maps terrain bodies (an explicit
   `phys_world->on_submap_loaded( m, sm )` in the fixture is enough to start),
   assert a non-zero contact count, hook `ter_set`/`furn_set` for runtime changes,
   *then* do the bash routing below. `tests/vehicle_test.cpp`'s
   `box2d_authority_vehicle_bashes_terrain` case is the acceptance criterion,
   tagged `[!shouldfail]` until it works.

   Design notes from an attempt that was written and reverted as unverifiable:
   a shared `make_tile_body()` in `terrain_body.cpp` should serve both the submap
   build and any per-tile refresh; `classify_tile()` tests impassability first so
   walls are `solid` and only `bashable` bodies currently carry user data, leaving
   walls anonymous to contact dispatch — tag every terrain body; `on_tile_bashed()`
   erases from `bashable_tile_bodies_`/`bashable_tiles_` but **not**
   `terrain_bodies_`, which is a latent double-free once anything recreates a tile
   body; and a per-tile refresh must compare body presence (or class) rather than
   rebuilding unconditionally, or the fresh body emits a new begin-touch event and
   re-bashes up to 240 times a turn.

1. **Route collisions by reusing `part_collision()`, not by inventing a force model.**

   **Prerequisite now satisfied.** The blocker was that no contact could occur at all:
   `build_test_map()` mutates already-resident submaps and never fires the submap-loaded
   notification, so the world held no terrain bodies and
   `b2World_GetContactEvents().beginCount` was always 0. Building colliders explicitly in
   `tests/vehicle_test.cpp`'s `box2d_authority_vehicle_bashes_terrain` fixes that —
   measured `begin=48 hit=6` on the first batch, 42 batches over the run, against 176,399
   terrain bodies. The spec now fails on its final assertion only, the genuine gap.

   **Preferred approach — reuse, don't reinvent.** `vehicle::part_collision()` is public
   (`src/vehicle.h`, options struct) and already implements the *balanced* force
   derivation plus creature damage, terrain bashing, `throw_from_seat` and sound. The
   readback tile-walk in `map::vehmove()` already advances one tile at a time, exactly the
   granularity `part_collision()` expects, so calling it per step from the walk reuses all
   of it.

   Deriving a bash force from Box2D contact impulses instead means inventing a balance
   model — the thing this document already warns against, and something no test could
   validate beyond "terrain changed".

   Notes for whoever implements it:
   - Mirror `map::move_vehicle()`'s per-part collision detection rather than inventing a
     new traversal; it already handles the multi-part case.
   - `dispatch_contact_events()` then has a much smaller job: sound, and any purely
     physical response with no tile-step equivalent. It is not the place for bash force.
   - Its current comment claims hit events are unreliable for dynamic-static pairs.
     Measured otherwise (`hit=6` above), so re-check that before relying on either kind.
   - Creature bodies are sensors (`isSensor = true`, `enableContactEvents = false`) and
     produce no contact events at all, so creature collision must come from
     `part_collision()` regardless.

2. **Diagnose ramp and rails where they actually live** — `src/grab.cpp` for the pushed-cart
   ramp cases, and the direct-`displace_vehicle` warm-up for `vehicle_rails_test.cpp:184`.
   Not the readback.
3. **Restore vehicle-vehicle collision** — either re-reach `solve_vv_cluster()` from the
   readback path, or give vehicles a non-negative `groupIndex` plus a rule that stops
   parked neighbours shoving each other, then delete the transient solver.
4. **Calibrate fuel economy** and re-enable the two efficiency `CHECK`s.
5. **Fix cross-test leakage** — `"vehicle gun recoil*"` and `grabbed_shopping_cart_*` pass
   in isolation but fail in the full `[vehicle]` run. Addendum item 3 nominates the
   per-binary `PhysicsWorld` as the suspect. Partially checked: instrumenting
   `vehicle_bodies_.size()` on every change across the whole `[vehicle]` suite showed it
   only ever taking the values **0 and 1** — so there is no *accumulation* of vehicle
   bodies, and the "orphaned bodies pile up and degrade broad-phase to O(n^2)" shape of
   the old creature-body bug is not what is happening here.

   That does **not** clear the registry: a single stale key left behind after a vehicle is
   freed keeps the size at 1 while still being a use-after-free in `substeps_for_turn()`
   and `sync_bodies_from_game()`, both of which dereference every key.

   **Fixed this session** (see git log): the `pos.z() == player z` guard that skipped the
   sweep entirely for other z-levels; the sweep destroying bodies without clearing
   `box2d_position_authority`, which stranded vehicles with authority and no body; and
   `simulated -> lazy_border` destroying the bodies of vehicles that were still alive.
   The hook now takes a residency flag, revokes authority while resident (keeping the
   body), destroys only on genuine removal, and re-grants on return — with the
   revoke/re-grant contract covered by a flip-tested case in `tests/vehicle_test.cpp`.

   **Still open:** per `src/submap_load_manager.h:31-32`, `lazy_border -> evicted` never
   fires `on_submap_unloaded` at all, so the sweep does not run at the moment a vehicle is
   genuinely freed. Dangling keys on true eviction therefore remain possible, and that is
   now the most likely remaining registry hazard. The header's own prescription is a
   residency cache built on `mapbuffer::lookup_submap_in_memory()`, or hooking the real
   free site in mapbuffer eviction. Confirm with a validity check (assert every key is a
   live, map-known vehicle) or ASAN before rewriting the listener contract — and note the
   leakage symptom may not be this at all, since size never grew.
6. Only then Phase 12 cleanup (with 12-A rewritten per Correction 1), then Step 6, then
   Phase 11.

### Stale line references

`map.cpp` has since been decomposed; the vehicle code cited throughout this document as
`map.cpp:NNN` now lives in `src/map_vehicle.cpp` (`vehmove`, `vehproceed`,
`move_vehicle`, `vehicle_vehicle_collision`, `displace_vehicle`). Re-grep before editing.

### 0. Playtest Phase 10 Step 5 first  *(prerequisite for everything below)*

Before touching any code, validate that Box2D position authority works correctly in gameplay.

**How to test:**
```sh
cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles
./cataclysm-bn-tiles          # launch with BOX2D=ON build
```
1. Start a new game. Spawn a vehicle (`vehicle` debug menu or scenario).
2. Drive it around for ~60 seconds — verify it moves smoothly with no tile-snap jitter.
3. Drive into a wall — verify Box2D contact events fire (collision response, angular spin).
4. Drive over a z-level drop — verify vehicle falls normally (tile-step still owns z).
5. Park adjacent to another vehicle — verify they do NOT push each other apart (VV filter blocks).
6. Save and reload — verify `physics_pos` / `physics_angle` restore correctly (no snap to origin).

**Pass criteria:** vehicle moves, collides, falls, and saves/loads without artifact. Only then proceed.

---

### 1. Phase 12 — Legacy Cleanup  *(unblocked after playtest)*

**Depends on**: Phase 10 Step 5 playtested and stable.

Remove all code that Box2D now supersedes. Do this before Step 6 because Step 6 deletes the very fields Phase 12 audits. **Audited 2026-07-12 — callsites confirmed below.**

---

#### Step 12-A: Remove transient VV solve  *(Phase 2 — superseded by Phase 10 contact events)*

Files to delete entirely:
- `src/physics/veh_box2d_solve.h`
- `src/physics/veh_box2d_solve.cpp`

Callsites to scrub **before** deleting:
- `src/map.cpp:8` — `#include "physics/veh_box2d_solve.h"` → delete line
- `src/map.cpp:1926-1934` — delete the entire `resolve_vehicle_terrain_impulse()` wrapper function (early-returns `{}` when `!phys_world`, then delegates to `phys_world->resolve_terrain_impulse`)
- `src/map.h:938-942` — `resolve_vehicle_terrain_impulse()` wrapper declaration → delete
- `src/vehicle_move.cpp` — `veh_coll_bashable` path that calls `here.resolve_vehicle_terrain_impulse()` and writes `angular_velocity_rads` at line 930 → delete that block

#### Step 12-B: Remove transient terrain solve  *(Phase 5 — superseded by persistent VT contacts)*

- `src/physics/physics_world.h:66-70` — `resolve_terrain_impulse()` declaration → delete
- `src/physics/physics_world.cpp:315-~380` — `PhysicsWorld::resolve_terrain_impulse()` implementation → delete

#### Step 12-C: Remove VV collision `precomputed` branch

In `vehicle_vehicle_collision()` (`src/map.cpp`):
- Find the `#ifdef BOX2D_ENABLED` block that reads `opts.veh1_impulse_ns` / `opts.delta_vel_mps` and writes `angular_velocity_rads` at line 1103
- Delete the whole `precomputed` branch; keep the non-Box2D path for fallback until Phase 10 Step 6

#### Step 12-D: Remove 1D elastic analytic formula

In `vehicle_vehicle_collision()`:
- Delete the `analytic` branch that recomputes impulse from `get_collision_factor()` directly
- **Keep** `get_collision_factor()` — Phase 11 needs it for penetration falloff

#### Step 12-E: Retire `precalc` field

- Run `lsp references vehicle_part::precalc` — expect ~50 callsites
- Migrate each read of `precalc[0]` to `part_world_offset(p, veh.physics_angle)` (already implemented in `vehicle_shape.h`)
- Delete `vehicle_part::precalc[2]` from `vehicle_part.h`
- Delete `precalc_mounts()` and the `refresh_precalc(float)` shim from `vehicle.h` / `vehicle.cpp`

#### Step 12-F: Remove tile-step detection loop in `move_vehicle()`

- The per-tile collision traversal `for (int count = 0; count < 100; ++count)` loop is dead code for Box2D vehicles
- Only remove after steps A–E compile cleanly — this loop depends on the 1D impulse math removed in D

#### Step 12 verification

After **each** substep, not at the end:
```sh
cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles
./cata_test-tiles "[vehicle][box2d]"   # must pass all 38 assertions
```

---

### 2. Phase 10 Step 6 — Retire Legacy Motion Fields  *(unblocked after Phase 12)*

**Depends on**: Phase 12 complete (so all consumers of these fields are gone before the fields are deleted).

**Audited `of_turn` callsites** (confirmed 2026-07-12):
- `src/vehicle.h:1755-1757` — field declarations (`of_turn`, `of_turn_carry`) → delete
- `src/vehicle.cpp:359,360,413,6451,6465,6467` — copy ctor, init, `gain_moves()` body → delete
- `src/vehicle_move.cpp:1500,1507,1523-1534` — falling budget, traction zero, turn-cost check → delete
- `src/map.cpp:810-907` — priority queue scheduling and the entire `gain_moves()` block → replace with Box2D sub-step accumulator
- `src/map.cpp:1351-1432` — VV reordering hack (`avg_of_turn`) → delete (contact events handle ordering)
- `src/savegame_json.cpp:3207,3357,3400` — serialization read/write/reset → drop; add old-save migration guard

**Audited `angular_velocity_rads` callsites** (confirmed 2026-07-12):
- `src/vehicle.h:1760` — field declaration → delete after removing all writes
- `src/vehicle_move.cpp:930` — terrain-impulse write-back (gone after Step 12-A)
- `src/map.cpp:1103` — VV solve result write (gone after Step 12-C)
- `src/physics/physics_world.cpp:248,259,336` — pre-step sync, post-step readback, transient solve init → replace 248+336 with direct `b2Body_SetAngularVelocity`/`b2Body_GetAngularVelocity` at the Box2D layer; 259 becomes the sole readback → keep until field deleted
- `src/savegame_json.cpp:3208,3401` → drop serialization
- `src/physics/veh_box2d_solve.cpp:59-60` → gone with Step 12-A

**Remaining fields** (`velocity`, `turn_dir`) — defer to after `of_turn` and `angular_velocity_rads` are clean; they have ~200 callsites each and need a separate migration sprint.

**Step-by-step:**
1. Delete `of_turn` + `of_turn_carry` following the callsite list above
2. Delete `angular_velocity_rads` following the callsite list above
3. Fix every compile error; rebuild after each field
4. Update `savegame_json.cpp` — add migration guards for old saves that contain the retired keys
5. Run `./cata_test-tiles "[vehicle][box2d]"` — all assertions must pass

---

### 3. Phase 11 — Ranged Combat via Box2D Ray Casts  *(unblocked after Phase 10 Step 6)*

**Depends on**: Phase 10 Step 6 complete AND external tile-independent ranged combat rework.

Replace tile-traversal LOS and hit resolution with `b2World_CastRayClosest()` into the persistent world.

**What to implement:**
1. **LOS query**: `b2World_CastRayClosest(world, origin_m, target_m, filter)` → first hit body + exact impact point + surface normal. `b2Body_GetUserData(hit.bodyId)` gives the game object pointer for damage dispatch.
2. **Penetrating rounds**: `b2World_CastRay()` with a custom callback that accumulates all hits along the ray; apply penetration falloff per-hit.
3. **Cover calculation**: replace per-tile modifier table with fractional occlusion derived from Box2D shape geometry (fraction of ray length occluded by cover shapes).
4. **API reference** (v3.0.0 confirmed): `b2World_CastRay`, `b2World_CastRayClosest`, `b2World_OverlapAABB`.

**Gate**: do NOT start until the external ranged combat rework (tile-independent projectile path) is merged. Starting early creates merge conflicts in the exact code this phase rewrites.

---

## Addendum — findings from the 2026-07-28 followup-bug session

Three constraints discovered while fixing `plans/mouse-interactivity-followup-bugs.md`. All three
affect Phase 10 Step 6 and Phase 12; read before resuming either.

### 1. ~~Box2D is NOT the speed authority for ordinary driving~~ — SUPERSEDED 2026-07-29

**This section's conclusion was wrong and its fix has been reverted. See "Correction 3"
near the top of this document.** Kept here only so the reasoning error is on record.

The original claim was that `map::vehmove()` must step exactly **once** by `1/60` s, not
sixty times, because `veh.velocity` is re-applied to the body every turn and integrating
a full second would "re-derive displacement on top of the game's own velocity model",
doubling speed. It cited vehicles travelling **2.2–2.3x further than fuel-efficiency
balance permits** (257,835 tiles vs a 111,404 upper bound) and named
`vehicle_efficiency_test` as the guard rail.

What was actually happening:

- One `1/60` s step per 1-second turn integrated **1/60th** of the turn, so vehicles moved
  ~56x too slowly: 0.0932 tiles/turn and 2 tiles in 20 turns at 10 m/s, vs 111.85 expected.
  That is the direct cause of the ramp / rails / recoil movement failures.
- The `2.2-2.3x` overshoot seen when stepping a full second was **not** double-derived
  displacement. `vehicle_efficiency_test` teleports the vehicle back to its start every
  turn without resyncing `physics_pos`, so its accumulator measured a
  reset-vs-stale-`physics_pos` artifact, which scaled with however far physics had drifted.
  It was never a valid speed guard rail.

Both defects are fixed: `step_turn( 1.0f )` integrates a real turn with tunneling-safe
sub-steps, and `on_vehicle_moved()` reseats `physics_pos` on external teleports.

### 2. Continuous integration can carry a body outside the loaded map

`b2World_Step()` has no notion of the reality bubble. A fast vehicle's body can integrate past the
loaded map edge within a single turn; the readback then calls `displace_vehicle()` toward a submap
that was never loaded, stranding the vehicle in a null submap (`dst submap not loaded` + crash).

The readback now checks `inbounds()` before displacing and, on failure, stops the vehicle, rewinds
`physics_pos` to the occupied tile, and force-writes the body transform via
`PhysicsWorld::clamp_body_to_tile()`. That method deliberately bypasses the
`box2d_position_authority` guard in `on_vehicle_moved()` — it is the one case where the tile grid
must win over the body. Any future code that moves bodies independently of the tile grid needs the
same boundary guard.

### 3. `PhysicsWorld` is a per-binary singleton, not per-`TEST_CASE`

`map::phys_world` and its `b2WorldId` are constructed once in `tests/test_main.cpp`
`init_global_game_state()` and never rebuilt between test cases. Anything leaked into that world
accumulates for the entire suite.

This caused the long-standing full-suite hang: two creature-removal paths
(`Creature_tracker::remove_dead()`, `game::erase_npc()`) erased creatures without calling
`PhysicsWorld::on_creature_removed()`, so orphaned kinematic sensor bodies piled up and degraded
broad-phase collision to O(n²). `creature_bodies_` also retained dangling `const Creature *` keys.

Note this contradicts Box2D's own guidance, which recommends recreating the world per test because
object pools and free lists make body ordering depend on allocation history. Until the harness does
that, **every** new body/creature registration path must have a matching teardown hook, and any new
cross-test flakiness should be checked against this singleton first.

---



## Codebase Audit — Implementation Status (verified 2026-07-11)

| Phase | Status | Evidence |
|---|---|---|
| Phase 1 — CMake | ✅ Done | `CMakeLists.txt:803-827`; `src/CMakeLists.txt:219-222` |
| Phase 2 — VV Solver + Tests | ✅ Done | `src/physics/vehicle_shape.h`, `veh_box2d_solve.h/cpp`, `tests/vehicle_box2d_test.cpp`; 5+18 assertions, 6 test cases pass |
| Phase 3 — Persistent World | ✅ Done | `src/physics/physics_world.h/cpp`, `terrain_body.h/cpp`; map hooks in `map.cpp/mapgen.cpp` |
| Phase 4 — VV Collision Wiring | ✅ Done | `veh_veh_coll_opts`, `vehicle_vehicle_collision` 4-param, `step()` in `vehmove()` |
| Phase 5 — Terrain Impulse | ✅ Done | `resolve_terrain_impulse()` transient solve in `physics_world.cpp:245-313`; wired in `vehicle_move.cpp` |
| Phase 6 — Physics Position Fields | ✅ Done | `physics_pos`/`physics_angle` fields in `vehicle.h`; readback in `step()` |
| Phase 7 — Continuous Rotation Helpers | ✅ Done | `part_world_offset()` in `vehicle_shape.h`; `refresh_precalc(float)` in `vehicle.cpp`; wired in `step()` |
| Phase 8 — Co-op Integration | ✅ Done | Integration point identified: `coop_game_tick()` → `post_action_world_step()`; `step()` already called at `vehmove():784` |
| Phase 9 — Serialization | ✅ Done | `angular_velocity_rads`, `physics_pos_x/y`, `physics_angle` serialized in `savegame_json.cpp`; old-save safe |
| Phase 10 — Movement Migration | 🔄 Steps 1–5 done | Steps 1–3 (dynamic promotion, VT contacts, dispatch); Step 4 (act_on_map inventory); Step 5 (box2d_position_authority flag, act_on_map early-return, vehmove() xy readback) — commit `d6355af`; Step 6 (legacy field retirement) blocked on Phase 12 |
| Phase 11 — Ranged Combat | 🟡 Unblocked | External ranged rework complete (2026-07-12); now gated on Phase 10 Step 6 |
| Phase 12 — Legacy Cleanup | ⛔ Blocked | Depends on Phase 10 full |
| Phase 13 — Creature Bugfix | ✅ Done | Bug A (`smashed=true` guard) and Bug B (dead-state check) applied |

**Phases 1–9, 13 and Phase 10 Steps 1–5 complete.** Box2D now owns horizontal vehicle position: `act_on_map()` runs sinking/falling/traction/skidding logic then exits early; `vehmove()` reads `physics_pos` back to the tile grid. Phase 10 Step 6 (legacy field retirement) and Phase 12 (Legacy Cleanup) remain. Phase 11 (Ranged Combat) is unblocked — the external tile-independent ranged rework is complete.

---

## Architecture Decisions

### Resolved

| # | Decision | Resolution |
|---|---|---|
| D1 | Feature scope | **Full physics authority (D)** — VV collision, vehicle-terrain, vehicle movement, ranged combat |
| D1a | Vehicle-terrain spin approach | **Box2D transient solve**, not analytic formula |
| D1b | Persistent world architecture | **Synced-mirror evolving to full authority** |
| D1c | Physics dt vs game tick | **Fixed-timestep accumulator** ~60Hz inside 1s co-op tick; dt ≠ game tick |
| D1d | Movement authority | **Box2D drives movement** — `move_vehicle()` tile-step replaced; vehicles become `b2_dynamicBody` |
| Geom-1 | Vehicle shape source | **Mount-space extents** (`part.mount`) — avoids double-rotation with `precalc[0]` |
| Geom-2 | Extent off-by-one | `hw = (max_mx - min_mx + 1) / 2.0f * TILE_M` |
| Geom-3 | Center offset | `b2MakeOffsetBox(hw, hh, {cx_local, cy_local}, 0.0f)` — confirmed in v3.0.0 |
| Box2D-ver | Library version | **v3.0.0 pinned** — direct `friction/restitution/density` fields, not `material.*` |
| D2 | Continuous position rounding | **Round to nearest tile** — `tile = round(physics_pos / TILE_M)` |
| D3 | Rendering under continuous rotation | **Quantize for render (A)** — snap Box2D angle to nearest 15° for sprite selection; `tileray face` kept as render shadow; passenger sprites stay consistent |
| D4 | Save-format canonical position | **Float canonical (A)** — `physics_pos` (metres) + `physics_angle` (radians) saved; integer tile = `round(physics_pos / TILE_M)` on load; old-save compat out of scope |
| D5 | VV collision wiring interface | **`precomputed` pointer (A)** — `vv_solved_body *precomputed = nullptr` default param; retired at Phase 10 |
| D6 | Restitution model | **Relative approach speed (B)** — both bodies `get_collision_factor(delta_v)`; `b2MixRestitution=max(r1,r2)` makes per-vehicle speed always produce 1.0 for parked targets |
| D7 | Force model for movement | **Velocity-target forces (A)** — `F = mass × (v_target − v_current) / dt`; existing throttle/gear/engine logic feeds `v_target`; follow-on physics tuning pass is separate |
| D8 | Real-time loop actor contract | **Interleaved scheduling (B)** — creature/AI turns fire between physics sub-steps; actors see current sub-tile vehicle position and can react; contract for Phase 8 (co-op system) to implement |

### All decisions resolved. Walkthrough complete.

### Deep-dive decisions (in progress)

| # | Decision | Resolution |
|---|---|---|
| DD1 | Z-level strategy | **Single world + z-range limit** — bodies only for z ∈ [player_z−1, player_z+1]; `b2Filter.categoryBits` per z-level; vertical/rotor physics stays bespoke |
| DD2 | Coordinate origin / float precision | **`bub_ms` coordinates throughout** — positions in bubble-relative tiles × TILE_M (≤236m from origin, precise by construction); `map::shift()` hook translates all bodies by `shift_delta × TILE_M` |
| DD3 | Terrain body strategy | **Hybrid (D)** — merged rectangles for `veh_coll_other` (indestructible, built once on submap load); per-tile `b2_staticBody` for `veh_coll_bashable` (bash = destroy one body); ~4–5K bodies at default map size |
| DD4 | Contact event handling | **A+B** — hit events for discrete impact; begin/end contact events for sustained contact; `enableHitEvents=true` + `enableContactEvents=true` on vehicle shapes; pre-solve deferred |
| DD5 | Body registry + world ownership | **map-owned PhysicsWorld (A/X/T-A)** — `unordered_map<vehicle*, b2BodyId>` + userData reverse; terrain bodies carry encoded `tripoint_bub_ms`; all physics logic in `src/physics/` |
| DD6 | Phase 5 bash-loop integration | **`PhysicsWorld::resolve_terrain_impulse()` per iteration** — one clean call per do-while iteration; transient Box2D world; consistent physics; bash/damage logic stays in `vehicle_move.cpp` |
| DD7 | Phase 7a precalc migration | **Shim-first (B)** — `refresh_precalc(physics_angle)` keeps `precalc[0]` as cache during migration; callsites migrated incrementally (hot first: tile occupancy, FoV; cold last: AI, spawn); `precalc[0]` and `precalc_mounts()` retired in Phase 12 |

### Non-determinism constraint (co-op)

Box2D is not deterministic across separate instances. For co-op: the authoritative host runs Box2D; other clients receive resolved velocities, positions, and angular velocities as state updates. Clients do not run their own Box2D solve.

---

## Phase 0: Documentation Discovery (Completed)

### Verified Box2D 3.0.0 API (`erincatto/box2d` tag `v3.0.0`)

Sources: `include/box2d/types.h`, `include/box2d/collision.h`, `include/box2d/math_functions.h`, `include/box2d/box2d.h`

**Allowed API — exact signatures confirmed from headers:**

| Symbol | Signature / Notes |
|---|---|
| `b2DefaultWorldDef()` | `B2_API b2WorldDef b2DefaultWorldDef(void)` |
| `b2CreateWorld()` | `B2_API b2WorldId b2CreateWorld(const b2WorldDef*)` |
| `b2DestroyWorld()` | `B2_API void b2DestroyWorld(b2WorldId)` |
| `b2World_Step()` | `B2_API void b2World_Step(b2WorldId, float timeStep, int subStepCount)` |
| `b2World_CastRay()` | `B2_API void b2World_CastRay(b2WorldId, b2Vec2 origin, b2Vec2 translation, b2QueryFilter, b2CastResultFcn*, void*)` |
| `b2World_CastRayClosest()` | `B2_API b2RayResult b2World_CastRayClosest(b2WorldId, b2Vec2 origin, b2Vec2 translation, b2QueryFilter)` |
| `b2World_OverlapAABB()` | `B2_API void b2World_OverlapAABB(b2WorldId, b2AABB, b2QueryFilter, b2OverlapResultFcn*, void*)` |
| `b2DefaultBodyDef()` | `B2_API b2BodyDef b2DefaultBodyDef(void)` |
| `b2CreateBody()` | `B2_API b2BodyId b2CreateBody(b2WorldId, const b2BodyDef*)` |
| `b2DestroyBody()` | `B2_API void b2DestroyBody(b2BodyId)` |
| `b2Body_SetTransform()` | `B2_API void b2Body_SetTransform(b2BodyId, b2Vec2 position, b2Rot rotation)` — teleport |
| `b2Body_SetUserData()` | `B2_API void b2Body_SetUserData(b2BodyId, void*)` |
| `b2Body_GetUserData()` | `B2_API void* b2Body_GetUserData(b2BodyId)` |
| `b2Body_GetLinearVelocity()` | `B2_API b2Vec2 b2Body_GetLinearVelocity(b2BodyId)` |
| `b2Body_GetAngularVelocity()` | `B2_API float b2Body_GetAngularVelocity(b2BodyId)` |
| `b2Body_ApplyLinearImpulse()` | `B2_API void b2Body_ApplyLinearImpulse(b2BodyId, b2Vec2 impulse, b2Vec2 point, bool wake)` |
| `b2DefaultShapeDef()` | `B2_API b2ShapeDef b2DefaultShapeDef(void)` |
| `b2MakeBox()` | `B2_API b2Polygon b2MakeBox(float hx, float hy)` |
| `b2MakeOffsetBox()` | `B2_API b2Polygon b2MakeOffsetBox(float hx, float hy, b2Vec2 center, float angle)` |
| `b2CreatePolygonShape()` | `B2_API b2ShapeId b2CreatePolygonShape(b2BodyId, const b2ShapeDef*, const b2Polygon*)` |
| `b2Vec2_zero` | `static const b2Vec2 b2Vec2_zero = {0.0f, 0.0f}` |

**Struct fields confirmed:**
- `b2WorldDef.gravity` = `b2Vec2` (set `{0.0f, 0.0f}` for horizontal-plane solve)
- `b2BodyDef`: `type` (b2BodyType), `position` (b2Vec2), `rotation` (`b2Rot{c,s}` **NOT float**), `linearVelocity` (b2Vec2), `angularVelocity` (float), `isBullet` (bool)
- `b2ShapeDef` v3.0.0: `friction`, `restitution`, `density` — **direct fields, NOT under `material`**
- `b2Rot`: `c` (cosine), `s` (sine)

**Anti-patterns to avoid:**
- `shapeDef.material.friction` — only post-v3.0.0; crashes at tag v3.0.0
- `b2BodyDef.rotation = float_angle` — must be `b2Rot{cosf(a), sinf(a)}`
- `wdef.internalValue` / `bdef.internalValue` — reserved, UB
- bumping `subStepCount` to fix tunneling — wrong tool; only smaller `timeStep` calls fix tunneling
- `wdef.enableContinous` — typo in v3.0.0 header (missing 'u'); use `b2World_EnableContinuous()` instead

---

## Context

The vehicle-vehicle (VV) collision system in `map::vehicle_vehicle_collision()` (`src/map.cpp:1225`) has two correctness gaps:

1. **Contact-point torque absent**: Impulse is applied through both centers of mass. A T-boned vehicle gets linear velocity but no spin regardless of where contact occurs.
2. **Multi-target serialization**: N targets per tick solved sequentially against stale velocity.

Beyond VV, the entire collision and movement model has deeper structural issues:
- Vehicle-terrain (`veh_coll_bashable`, `veh_coll_other`) uses a 1D elastic formula with no spin component
- Ranged combat uses tile-by-tile traversal and per-tile cover modifiers — incompatible with continuous geometry and real-time co-op
- Vehicle movement uses a tile-step model (`move_vehicle`, `of_turn`, discrete 15° turns) that is a turn-based artifact unsuited to real-time play

**Target architecture**: Box2D persistent world at ~60Hz, game logic at 1s co-op tick. Vehicles are `b2_dynamicBody`. The game sends forces and torques; Box2D integrates positions and handles contacts. Ranged combat casts rays into the persistent world. Tile-step movement, bespoke collision formulas, and tile-traversal LOS are retired.

---

## Phase 1: CMake Integration *(done)*

**Status:** ✅ Implemented and verified in current codebase.
- `CMakeLists.txt:803–827` — `option(BOX2D ... OFF)` + FetchContent block for box2d v3.0.0, warning suppression
- `src/CMakeLists.txt:219–222` — `target_link_libraries(... PUBLIC box2d)` + `target_compile_definitions(... PUBLIC BOX2D_ENABLED)` guarded by `if(BOX2D)`
- `src/CMakeLists.txt:5` — `GLOB_RECURSE src/*.cpp` picks up `src/physics/*.cpp` automatically when that dir is created; exclusion filter `/src/(lua|sol|third-party)/` does not exclude `src/physics/`

**Note on CMakePresets:** No `BOX2D=ON` preset exists in `CMakePresets.json`. Always pass `-DBOX2D=ON` manually on the cmake configure line. The `osx-arm-slim` preset does not use `mold` or `SDL3_shadercross`, so Box2D links cleanly on macOS ARM.

**Phase 1 verification:**
```sh
cmake --preset osx-arm-slim -DBOX2D=ON
cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles
```

---

## Code Organisation

**All physics logic lives in `src/physics/`. Legacy files receive only thin hook calls — no physics implementation inline in `map.cpp`, `vehicle_move.cpp`, or `vehicle.cpp`.**

```
src/physics/
  vehicle_shape.h         — shared mount-space shape computation (used by all phases)
  veh_box2d_solve.h/cpp   — transient VV solver (Phase 2)
  terrain_body.h/cpp      — submap tile → static body merging and per-tile registry (Phase 3)
  physics_world.h/cpp     — PhysicsWorld class: owns b2WorldId, vehicle registry,
                            fixed-timestep accumulator, contact event dispatch (Phase 3)
```

**`PhysicsWorld` public interface** (called by legacy file hooks — no physics logic in those files):
```cpp
// map.cpp hooks — one-liners that forward to physics_world
void on_vehicle_added( vehicle &v );
void on_vehicle_moved( vehicle &v );       // called by displace_vehicle()
void on_vehicle_removed( vehicle &v );
void on_tile_bashed( tripoint_bub_ms pos );
void on_submap_loaded( submap &sm, tripoint_bub_ms origin );
void on_submap_unloaded( tripoint_bub_ms origin );
void on_map_shifted( point delta );        // translates all bodies by delta × TILE_M
// game loop
void step( float dt, int substeps );       // fixed-timestep accumulator lives here
void dispatch_contact_events();            // called after step; routes to game systems
// query (ranged combat, blast)
b2WorldId world_id() const;               // for direct b2World_CastRay* calls
// vehicle-terrain physics (Phase 5) — called once per bash loop iteration
struct terrain_impulse_result {
    rl_vec2d linear_vel_cmps;
    float    angular_vel_rads;
};
auto resolve_terrain_impulse( vehicle &v,
                              tripoint_bub_ms tile_pos,
                              float tile_mass_kg,
                              float restitution ) -> terrain_impulse_result;
```

**`map` change is minimal:**
```cpp
// map.h — one new field
std::unique_ptr<physics::PhysicsWorld> phys_world;  // null when BOX2D_ENABLED is off
```

The `src/CMakeLists.txt` `GLOB_RECURSE` already picks up `src/physics/*.cpp` automatically (confirmed at line 5).

---

## Phase 2: VV Collision Solver + Unit Tests

**Depends on Phase 1. Standalone solver that proves the physics before any game wiring. Transient-world pattern; this is the foundation that later phases build on.**

### Vehicle shape helper (shared across phases)

All phases that create Box2D bodies for vehicles use the same shape computation. Three confirmed geometry bugs in the naive approach are fixed here once:

```cpp
// src/physics/vehicle_shape.h (new header, included by all physics modules)
#pragma once
#include "vehicle_part.h"  // part.mount
#include <box2d/box2d.h>

// 1 tile = vehicles::cmps_per_tile / 100 cm/s per tile / 100 = 1.78816 m
// Source: src/vehicle.h:61  constexpr float cmps_per_tile = 178.816f; (in namespace vehicles)
static constexpr float TILE_M = 1.78816f;

/// Compute the Box2D polygon for a vehicle body in its LOCAL (mount-space) frame.
/// Fixes three bugs vs the naive approach:
///   1. Uses part.mount (unrotated local frame) — NOT get_bounding_box()/precalc[0]
///      which is already world-rotated. Setting bdef.rotation on top of precalc extents
///      double-applies the vehicle's heading, producing wrong contact geometry.
///   2. Off-by-one: mount indices are tile centers; a vehicle spanning [min,max]
///      occupies (max-min+1) tiles. hw = (max-min+1)/2 * TILE_M.
///   3. Center offset: mount extents are not symmetric about (0,0). b2MakeOffsetBox
///      places the shape at the correct local-frame offset so the body origin (vehicle
///      reference point) and shape center coincide with world reality.
inline auto vehicle_box2d_shape( const vehicle &v ) -> b2Polygon
{
    int min_mx = INT_MAX, max_mx = INT_MIN;
    int min_my = INT_MAX, max_my = INT_MIN;
    for( const auto &p : v.parts ) {
        if( p.removed ) { continue; }
        min_mx = std::min( min_mx, p.mount.x() );
        max_mx = std::max( max_mx, p.mount.x() );
        min_my = std::min( min_my, p.mount.y() );
        max_my = std::max( max_my, p.mount.y() );
    }
    const float hw = ( max_mx - min_mx + 1 ) / 2.0f * TILE_M;
    const float hh = ( max_my - min_my + 1 ) / 2.0f * TILE_M;
    const float cx = ( min_mx + max_mx ) / 2.0f * TILE_M;  // local-frame center offset
    const float cy = ( min_my + max_my ) / 2.0f * TILE_M;
    return b2MakeOffsetBox( hw, hh, { cx, cy }, 0.0f );
}
```

### Step 2 — `src/physics/veh_box2d_solve.h`

```cpp
#pragma once
#include "rl_vec2d.h"
#include <map>
#include <vector>

struct vehicle;
struct veh_collision;

struct vv_solved_body {
    vehicle *veh;
    rl_vec2d final_vel_cmps;   // post-collision velocity, cm/s
    float    ang_vel_rads;     // angular velocity, rad/s (Box2D native; convert to deg/s at call site)
    float    impulse_ns;       // impulse magnitude, Ns — for damage calculation
};

struct vv_cluster_result {
    std::vector<vv_solved_body> bodies;  // [0] = active vehicle; [1..N] = targets
    float total_impulse_ns;
};

#ifdef BOX2D_ENABLED
/// Solve all vehicles in veh_collisions simultaneously in a transient b2World.
auto solve_vv_cluster( vehicle &veh,
                       const std::map<vehicle *, std::vector<veh_collision>> &veh_collisions )
    -> vv_cluster_result;
#endif
```

### Step 3 — `src/physics/veh_box2d_solve.cpp`

```cpp
#ifdef BOX2D_ENABLED
#include "veh_box2d_solve.h"
#include "vehicle_shape.h"
#include "vehicle.h"
#include <box2d/box2d.h>
#include <unordered_map>
#include <cmath>

auto solve_vv_cluster( vehicle &veh,
                       const std::map<vehicle *, std::vector<veh_collision>> &veh_collisions )
    -> vv_cluster_result
{
    std::vector<vehicle *> veh_list;
    veh_list.push_back( &veh );
    for( const auto &[target, _] : veh_collisions ) {
        veh_list.push_back( target );
    }

    b2WorldDef wdef = b2DefaultWorldDef();
    wdef.gravity    = { 0.0f, 0.0f };
    b2WorldId world = b2CreateWorld( &wdef );

    const auto origin = veh.bub_ms_location();
    // bub_ms_location() defined in vehicle.cpp:3811 as get_map().abs_to_bub(abs_ms_location())

    struct BodyData {
        b2BodyId id;
        b2Vec2   old_vel_ms;
        float    mass_kg;
    };
    std::unordered_map<vehicle *, BodyData> body_map;
    body_map.reserve( veh_list.size() );

    for( vehicle *v : veh_list ) {
        const auto tile_off = v->bub_ms_location() - origin;
        // tile_off is tripoint_rel_ms; .x() .y() are int tile offsets

        b2BodyDef bdef       = b2DefaultBodyDef();
        bdef.type            = b2_dynamicBody;
        bdef.position        = { tile_off.x() * TILE_M, tile_off.y() * TILE_M };

        // face_vec() returns angle_to_vec(face.dir()) where face.dir() is 15°-quantized.
        // This is acceptable for Phase 2 initial body state; Phase 10 uses Box2D angle directly.
        // face_vec() sig: rl_vec2d vehicle::face_vec() const  (vehicle_move.cpp:1385)
        // b2Rot stores {c, s} = (cosine, sine) — direct assignment from face_vec().
        const rl_vec2d fv    = v->face_vec();
        bdef.rotation        = b2Rot{ static_cast<float>( fv.x ), static_cast<float>( fv.y ) };

        // velo_vec() returns cm/s velocity vector; divide by 100 for m/s.
        // velo_vec() sig: rl_vec2d vehicle::velo_vec() const  (vehicle_move.cpp:1360)
        const rl_vec2d vel   = v->velo_vec();  // cm/s
        bdef.linearVelocity  = { static_cast<float>( vel.x / 100.0 ),
                                  static_cast<float>( vel.y / 100.0 ) };
        // angular_velocity_rads field does not exist until Phase 4; use 0.0f here.
        bdef.angularVelocity = 0.0f;
        bdef.isBullet        = std::abs( v->velocity ) > 2000;  // CCD above ~45 mph

        b2BodyId body = b2CreateBody( world, &bdef );

        // to_kilogram: free function in units namespace (units_mass.h:72);
        // used unqualified in vehicle_move.cpp via using; use units:: prefix here for clarity.
        const float mass_kg  = units::to_kilogram( v->total_mass() );
        // total_mass() sig: units::mass vehicle::total_mass() const  (vehicle.cpp:3850, lazy-cached)
        b2ShapeDef sdef      = b2DefaultShapeDef();
        const b2Polygon poly = vehicle_box2d_shape( *v );
        // area from the shape's AABB approximation (density drives inertia tensor)
        const float area     = 4.0f * std::max(
            ( ( poly.vertices[2].x - poly.vertices[0].x ) / 2.0f ) *
            ( ( poly.vertices[2].y - poly.vertices[0].y ) / 2.0f ), 0.01f );
        sdef.density         = mass_kg / area;
        // get_collision_factor: free function declared vehicle.h:69, defined vehicle_move.cpp:1396
        // takes delta_v in m/s; returns restitution 0.3..1.0; used in map.cpp:1291
        sdef.restitution     = get_collision_factor(
            std::abs( static_cast<float>( v->velocity ) / 100.0f ) );
        sdef.friction        = 0.3f;

        b2CreatePolygonShape( body, &sdef, &poly );

        body_map[v] = BodyData{ body,
                                { bdef.linearVelocity.x, bdef.linearVelocity.y },
                                mass_kg };
    }

    // dt = time to cross one tile at max approach speed; clamped to [0.001, 0.1] s.
    float max_approach_mps = 0.1f;
    for( const auto &[target, _] : veh_collisions ) {
        const rl_vec2d rel = veh.velo_vec() - target->velo_vec();
        max_approach_mps   = std::max( max_approach_mps,
                                       static_cast<float>( std::hypot( rel.x, rel.y ) ) / 100.0f );
    }
    const float dt = std::clamp( TILE_M / max_approach_mps, 0.001f, 0.1f );

    b2World_Step( world, dt, 8 );

    vv_cluster_result result;
    result.total_impulse_ns = 0.0f;
    result.bodies.reserve( veh_list.size() );

    for( vehicle *v : veh_list ) {
        auto         &bd  = body_map[v];
        const b2Vec2  nv  = b2Body_GetLinearVelocity( bd.id );
        const float   nw  = b2Body_GetAngularVelocity( bd.id );
        const float   dvx = nv.x - bd.old_vel_ms.x;
        const float   dvy = nv.y - bd.old_vel_ms.y;
        const float   imp = bd.mass_kg * std::hypot( dvx, dvy );

        result.bodies.push_back( {
            v,
            rl_vec2d{ nv.x * 100.0f, nv.y * 100.0f },
            nw,
            imp
        } );
        result.total_impulse_ns += imp;
    }

    b2DestroyWorld( world );
    return result;
}
#endif // BOX2D_ENABLED
```

### Step 4 — `tests/vehicle_box2d_test.cpp`

Drop into `tests/` — no `CMakeLists.txt` changes needed; `file(GLOB)` at `tests/CMakeLists.txt:2` picks it up automatically. `BOX2D_ENABLED` propagates via `cataclysm-bn-tiles-common PUBLIC` → tests automatically when `-DBOX2D=ON`.

```cpp
#ifdef BOX2D_ENABLED
#include "catch/catch.hpp"
#include <box2d/box2d.h>
#include <cmath>

static constexpr float TILE_M = 1.78816f;

static b2WorldId make_test_world()
{
    b2WorldDef wdef = b2DefaultWorldDef();
    wdef.gravity    = { 0.0f, 0.0f };
    return b2CreateWorld( &wdef );
}

static b2BodyId make_box_body( b2WorldId world,
                               float px, float py,
                               float vx, float vy,
                               float hw, float hh,
                               float mass_kg,
                               float restitution = 1.0f )
{
    b2BodyDef bdef      = b2DefaultBodyDef();
    bdef.type           = b2_dynamicBody;
    bdef.position       = { px, py };
    bdef.rotation       = b2Rot{ 1.0f, 0.0f };  // facing east
    bdef.linearVelocity = { vx, vy };
    bdef.isBullet       = ( std::hypot( vx, vy ) > 20.0f );

    b2BodyId body = b2CreateBody( world, &bdef );

    b2ShapeDef sdef  = b2DefaultShapeDef();
    sdef.density     = mass_kg / ( 4.0f * hw * hh );
    sdef.friction    = 0.0f;
    sdef.restitution = restitution;

    // hw/hh already represent correct local-frame half-extents; no center offset needed
    // for test bodies which are manually centered.
    b2Polygon poly = b2MakeBox( hw, hh );
    b2CreatePolygonShape( body, &sdef, &poly );
    return body;
}

// ── Test 1: momentum conserved, equal-mass head-on elastic ──────────────────
TEST_CASE( "Box2D VV: momentum conserved in head-on equal-mass collision",
           "[vehicle][box2d]" )
{
    const float mass = 1000.0f;
    const float hw   = 2.0f * TILE_M, hh = 1.0f * TILE_M;

    b2WorldId world = make_test_world();
    b2BodyId b1 = make_box_body( world, 0.0f,      0.0f, 10.0f, 0.0f, hw, hh, mass );
    b2BodyId b2 = make_box_body( world, 2.0f * hw, 0.0f,  0.0f, 0.0f, hw, hh, mass );

    b2World_Step( world, TILE_M / 10.0f, 8 );

    const b2Vec2 v1       = b2Body_GetLinearVelocity( b1 );
    const b2Vec2 v2       = b2Body_GetLinearVelocity( b2 );
    const float  px_before = mass * 10.0f;
    const float  px_after  = mass * v1.x + mass * v2.x;

    CHECK( px_after == Approx( px_before ).epsilon( 0.05f ) );
    CHECK( v1.x    == Approx( 0.0f ).margin( 1.5f ) );
    CHECK( v2.x    == Approx( 10.0f ).epsilon( 0.15f ) );

    b2DestroyWorld( world );
}

// ── Test 2: off-center hit produces angular velocity ────────────────────────
TEST_CASE( "Box2D VV: off-center hit produces angular velocity on target",
           "[vehicle][box2d]" )
{
    const float mass = 1000.0f;
    const float hw1 = 0.9f * TILE_M, hh1 = 0.5f * TILE_M;
    const float hw2 = 2.0f * TILE_M, hh2 = 1.0f * TILE_M;

    b2WorldId world   = make_test_world();
    const float y_off = hh2 * 0.6f;
    b2BodyId b1 = make_box_body( world, 0.0f,         y_off, 10.0f, 0.0f, hw1, hh1, mass, 0.8f );
    b2BodyId b2 = make_box_body( world, hw1 + hw2,    0.0f,   0.0f, 0.0f, hw2, hh2, mass, 0.8f );

    b2World_Step( world, TILE_M / 10.0f, 8 );

    const b2Vec2 v2    = b2Body_GetLinearVelocity( b2 );
    const float  omega = b2Body_GetAngularVelocity( b2 );

    CHECK( v2.x            > 1.0f );
    CHECK( std::abs( omega ) > 0.1f );

    b2DestroyWorld( world );
}
#endif // BOX2D_ENABLED
```

**Phase 2 verification:**
```sh
cmake --preset osx-arm-slim -DBOX2D=ON
cmake --build --preset osx-arm-slim --target cata_test-tiles
./out/build/osx-arm-slim/tests/cata_test-tiles "[vehicle][box2d]"
```

---

## Phase 3: Persistent World Infrastructure *(✅ DONE — 2026-07-11)*

**Depends on Phase 1. Prerequisite for Phases 4, 5, 6, 7.**

*All Phase 3 deep-dive decisions resolved (DD1–DD5). Implementation uses `PhysicsWorld` class in `src/physics/physics_world.h/cpp` — see Code Organisation section.*

- **Z-levels (DD1)**: Single `b2WorldId`. Bodies for z ∈ [player_z−1, player_z+1] only. `b2Filter.categoryBits` per z-level. `vertical_velocity`/`rotor_velocity` stay bespoke.
- **Coordinates (DD2)**: All positions in `bub_ms` × TILE_M. `on_map_shifted(delta)` translates all bodies by `delta × TILE_M`.
- **Vehicle registry (DD5)**: `unordered_map<vehicle*, b2BodyId>` + `b2Body_SetUserData(body, vehicle*)` for reverse lookup. `map` owns `std::unique_ptr<PhysicsWorld>` (null when `BOX2D_ENABLED` off).
- **Terrain bodies (DD3)**: merged rectangles for `veh_coll_other` (built once on submap load, never rebuilt — eliminates ghost vertices on wall slides); per-tile body for `veh_coll_bashable` (bash event = `DestroyBody` one call). Terrain body userData = encoded `tripoint_bub_ms` for tile identification at contact dispatch.
- **Contact events (DD4)**: vehicle shapes: `enableHitEvents=true`, `enableContactEvents=true`. After each sub-step: hit events → discrete impact damage/torque; begin/end events → sustained contact (slow bash, blocked state). Pre-solve deferred.
- Fixed-timestep accumulator: ~60 `b2World_Step(world, 1.0f/60.0f, 4)` calls per 1s game tick. Legacy `map.cpp` hooks are one-liners forwarding to `PhysicsWorld` methods.

---

## Phase 4: VV Collision Wiring *(✅ DONE — 2026-07-11)*

**Depends on Phases 2 and 3.**

*Wires the transient solver from Phase 2 into `map::move_vehicle()` and `map::vehicle_vehicle_collision()`. Details pending decisions on signature changes and damage integration. Key points:*

- Replace the for-loop at `map.cpp:1050-1052` (current code: `for(auto& pair : veh_collisions) { impulse += vehicle_vehicle_collision(...) }`) with a `solve_vv_cluster()` call
- `vehicle_vehicle_collision()` (currently `map.cpp:1225-1400`) gains a `precomputed` path for Box2D results vs analytic fallback
- Add `float angular_velocity_rads = 0.0f;` field to `vehicle` in `vehicle.h` near line 1751 (after `of_turn_carry`); this field is consumed in `vehicle::on_move()` during transition and used as initial angular velocity in Phase 2 solver
- Damage code unchanged — `impulse_ns` from solver feeds existing damage thresholds

---

## Phase 5: Vehicle-Terrain Collision *(✅ DONE — 2026-07-11)*

**Depends on Phase 3.**

*Box2D transient solve replaces the 1D formula in `part_collision()` for `veh_coll_bashable` and `veh_coll_other`. Details pending decisions on bash-loop extraction. Key points:*

- After the do-while bash loop settles (game logic unchanged), create a transient Box2D world
- Vehicle as `b2_dynamicBody`; impacted terrain tile(s) as `b2_staticBody` sourced from persistent world geometry
- Step once; read back vehicle `linearVelocity` + `angularVelocity`
- Apply results to vehicle; bash/damage/destruction logic above is untouched
- do-while loop runs `vehicle_move.cpp:721-912`; veh_coll_bashable handler at line 785; veh_coll_other handler at line 812

---

## Phase 6: Continuous Position Data Model *(✅ DONE — 2026-07-11)*

**Depends on Phase 3. Prerequisite for Phase 10.**

Vehicle position is tile-quantized throughout the codebase. Box2D returns continuous float `(x, y)` in metres. Switching to physics-authoritative movement without resolving this first produces silent data corruption in every system that reads vehicle position.

**Cascade map — every system that assumes integer tile position:**

| System | File(s) | Current assumption | Required change |
|---|---|---|---|
| Submap / part lookup | `map.cpp`, `vehicle.cpp` | `part_at(tripoint)` takes integer tile | Float→tile rounding rule; canonical occupied-tile set from AABB |
| Occupied-points cache | `vehicle.cpp:occupied_points` | Integer set rebuilt each move | Rebuild from Box2D AABB at each physics step boundary |
| Pathfinding (A*) | `map.cpp:route()` | Vehicle obstacle tiles are exact | Occupied tile set provides obstacle list; same rounding rule |
| FoV / lighting | `map.cpp:reachable_flood_steps()` | Tile-raycast; vehicle blocks exact tiles | Obstacle set from AABB; acceptable approximation for now |
| Creature adjacency | `creature.cpp`, AI | `is_adjacent` checks integer tile | Use occupied tile set; no sub-tile creature interaction needed |
| Mapgen / spawn | `mapgen.cpp` | Integer spawn point | Spawn remains integer; Box2D body teleported to match on spawn |
| `bub_ms_location()` callers | Many | Returns integer tile | Audit all callers; split into `tile_location()` (integer, for tile systems) and `world_pos()` (float, for physics) |

**Work items:**
1. Add `rl_vec2d physics_pos` (metres, Box2D frame) and `float physics_angle` (radians) to `vehicle` — owned by Box2D, read via `b2Body_GetPosition/Rotation` after each step
2. Keep `tripoint_abs_ms` integer position as the canonical tile anchor; define rounding: `tile_anchor = floor(physics_pos / TILE_M)` (or nearest-tile, TBD per Decision D2)
3. Audit all `bub_ms_location()` / `abs_ms_location()` call sites (LSP references) and classify: tile-system (keep integer) or physics-system (migrate to float)
4. Rebuild `occupied_points` from Box2D AABB once per game tick, not per tile-step

**Decisions required (Decision D2 in walkthrough):** float-position rounding strategy; whether sub-tile offset is stored in save or derived from Box2D state.

---

## Phase 7: Continuous Rotation, Part Precalc, and Rendering *(✅ DONE — 2026-07-11)*

**Depends on Phase 6. Prerequisite for Phase 10.**

The 15° discrete turn grid is load-bearing in two systems beyond `turn()`. Both must be resolved before Box2D can own orientation.

### 7a — Part precalc under continuous angle

`precalc_mounts(idir, dir, pivot)` precomputes part world offsets for exactly two headings (current and next-turn). Under continuous rotation from Box2D, part world positions must be computable for any arbitrary angle.

**Current flow:** `face.dir()` (15° quantized) → `precalc_mounts(0, face.dir(), pivot)` → `part.precalc[0]` populated → all tile-position queries read `precalc[0]`.

`face_vec()` currently returns `angle_to_vec(face.dir())` — a 15°-quantized unit vector (vehicle_move.cpp:1385). After Phase 10, `face.dir()` becomes a render-only shadow; continuous angle comes from `b2Body_GetRotation`.

**Migration strategy (DD7 — shim-first):**

1. Introduce `part_world_offset(const vehicle_part &p, float angle_rads) -> point_rel_ms` in `src/physics/vehicle_shape.h` (already planned — add here)
2. Introduce `vehicle::refresh_precalc(float physics_angle)` — populates `precalc[0]` from `part_world_offset`. Called each tick after Box2D angle update. All existing callers continue to work with zero changes.
3. Migrate callsites in priority order:
   - **Hot (every tick):** tile-occupancy cache (`occupied_points` rebuild), FoV blocking (`map::reachable_flood_steps`), collision detection part-position queries — migrate first; these are performance-sensitive
   - **Warm (on vehicle move):** `part_at()` lookup helpers, `bub_ms_location()` per-part variants, creature adjacency checks — migrate second
   - **Cold (on event):** AI pathfinding obstacle queries, spawn/mapgen positioning, debug draw — migrate last
4. Each migrated callsite: replace `part.precalc[0]` read with `part_world_offset(p, veh.physics_angle)`. Track in a checklist (see Phase 12).
5. `precalc[0]`, `precalc[1]`, `precalc_mounts()`, and `refresh_precalc()` are all retired in Phase 12 once the callsite checklist is complete.

**Impact:** ~50–100 callsites. Full audit via `lsp references` on `precalc` before starting.

### 7b — Rendering under continuous angle

`face` drives sprite selection via `tileray` — which of 24 sprites (0°, 15°, … 345°) to render for each vehicle part. Box2D orientation is a continuous float; the tile renderer has no concept of 47.3°.

**Three options (Decision D3 in walkthrough):**

| Option | Description | Cost | Visual quality |
|---|---|---|---|
| A — Quantize for render | Snap Box2D angle to nearest 15° for display; physics is continuous | Minimal | Same as today; no spinning interpolation |
| B — SDL rotation (`SDL_RenderCopyEx`) | Rotate the nearest-15° sprite by the residual angle using SDL's texture rotation | Medium | Smooth rotation within each 15° band |
| C — Full continuous sprites | Require tilesets to ship per-degree (or shader-rotate); arbitrary angle per part | Large | Fully smooth; requires art tooling changes |

Decision D3 determines whether `tileray face` is retired or kept.

---

## Phase 8: Real-Time Game Loop

**Gated on the co-op real-time system (external to this plan). Prerequisite for Phase 10.**

Box2D integrating vehicles at 60Hz between game turns creates an incoherence: a vehicle could traverse 11+ tiles during a physics sub-step while creatures, player FoV, and AI are frozen mid-turn with no opportunity to react. This is not a Box2D problem — it is a game-loop architecture problem.

**What must exist before Phase 10:**
- A continuous simulation loop in which each co-op tick (1s) is subdivided: physics runs at ~60Hz, creature/AI/FoV events fire at their own cadences within the same tick budget
- A defined actor contract: how does a turn-based creature (`monster::move()`) interact with a vehicle that has a continuous float position and is mid-step between creature turns?
  - Option A: creatures act at turn boundaries on a snapshot of vehicle tile positions — same as today, just more frequent
  - Option B: creature turns are interleaved with physics sub-steps — more correct, higher complexity
- `game::do_turn()` and `map::vehmove()` adapted to the new cadence

**This phase is owned by the co-op system, not the Box2D system. The Box2D plan gates Phase 10 on it being complete; it does not specify the co-op loop internals.**

---

## Phase 9: Physics State Serialization *(✅ DONE — 2026-07-11)*

**Depends on Phases 6 and 7. Prerequisite for Phase 10.**

Phases 2–5 deliberately avoid serializing physics fields (they are ephemeral transitional state). Phase 10 reverses this: Box2D owns the authoritative continuous position, orientation, velocity, and angular velocity. These must survive save/load or vehicles snap on reload.

**Changes to `savegame_json.cpp`:**

| Field | serialize() addition | deserialize() addition |
|---|---|---|
| `physics_pos` (float metres) | `jo.member("physics_pos", physics_pos)` after existing velocity fields (~line 3390) | read → `b2Body_SetTransform` on load |
| `physics_angle` (float radians) | `jo.member("physics_angle", physics_angle)` | read → `b2Body_SetTransform` |
| `linear_vel` (float m/s, x+y) | `jo.member("linear_vel", linear_vel)` | read → `b2Body_SetLinearVelocity` |
| `angular_vel` (float rad/s) | `jo.member("angular_vel", angular_vel)` | read → `b2Body_SetAngularVelocity` |

`vehicle::serialize` is at `savegame_json.cpp:3375`; `vehicle::deserialize` at line 3167. Existing motion fields serialize at lines 3379–3396 (`posx/posy`, `faceDir`, `moveDir`, `turn_dir`, `velocity`, `cruise_velocity`, `vertical_velocity`, `skidding`, `of_turn_carry`). Add physics fields after `of_turn_carry` (line 3396) in serialize, and correspondingly in deserialize.

**World rebuild on map load:** the persistent Box2D world does not persist as a binary blob. On `map::load()`, all vehicle bodies are created fresh and initialized from the serialized physics state above. Static terrain bodies are created from map tile data.

**Save format version:** a version bump is required. Old saves (no physics fields) deserialize with zero velocity and tile-anchor position; the vehicle is stationary on load — acceptable degradation. Pattern: existing version guards at savegame_json.cpp handle `<11`, `<25`, `<29`; add a new guard for the physics-field version.

**Decision D4 (walkthrough):** float `physics_pos` is the save-format canonical position (old saves fall back to integer tile anchor → zero velocity on load).

---

## Phase 10: Vehicle Movement Migration *(🔄 PARTIAL — 2026-07-12)*

**Depends on Phase 3 AND Phases 6, 7, 8, 9. Do not begin until all prerequisites are complete.**

*Replaces `move_vehicle()` tile-step model with Box2D physics integration.*

**Steps 1–3 delivered (2026-07-12, commit `3216f53`):**
- **Step 1** — VV filter separation: vehicle z-bits [z+30] (bits 20–40), terrain z-bits [z+10] (bits 0–20). VV contacts structurally disabled; VT contacts fire correctly. `physics_world.cpp` + `terrain_body.cpp` updated.
- **Step 2** — `b2_dynamicBody` promotion: vehicles now dynamic with `gravityScale=0`. Pre-step velocity-sync (game `velocity` cm/s → m/s + `angular_velocity_rads`) feeds realistic contact impulses. Post-step: `angular_velocity_rads` read back from Box2D (captures terrain-impact torque). Tile-step still owns position via `on_vehicle_moved()` teleports.
- **Step 3** — `dispatch_contact_events()` implemented: iterates `b2World_GetContactEvents().hitEvents`; identifies vehicle shapes via `vehicle_bodies_.count()` (O(1)); writes `b2Body_GetAngularVelocity()` to `ptr->angular_velocity_rads`. All hit events are VT (VV disabled by filter).
- Tests: 23/23 assertions in 6 test cases pass.

**Why this is deferred (root blocker):** `on_vehicle_moved()` calls `b2Body_SetTransform()` every time the tile-step system teleports a vehicle. With a `b2_dynamicBody`, Box2D interprets the position delta between sub-steps as an implicit velocity, generating phantom separation impulses on any shape that happens to overlap after the teleport (wall tiles, adjacent vehicles). The kinematic transitional was reverted for the same reason — Box2D v3 fires zero contact events for kinematic-static/kinematic-kinematic pairs, making promotion pointless without retiring tile-step. The only clean path is to retire `move_vehicle()` as the position authority first, then promote to `b2_dynamicBody`.

**VV filter conflict (must fix before dynamic promotion):**
Current filter: both vehicle and terrain shapes use `categoryBits = z_bit, maskBits = z_bit` where `z_bit = 1ull << (z + 10)`. When vehicles become `b2_dynamicBody`, same-z vehicles share the same bit in both category and mask — VV contact events fire. This causes vehicles parked adjacent to each other to push each other apart every step.

Fix: separate the bit-space by body type. Use a two-range scheme:
- **Terrain** at z: `categoryBits = 1ull << (z + 10)` *(unchanged)*, `maskBits = 1ull << (z + 30)` *(changed from z+10)*
- **Vehicle** at z: `categoryBits = 1ull << (z + 30)` *(changed from z+10)*, `maskBits = 1ull << (z + 10)` *(unchanged)*

Terrain bits occupy bits 0–20; vehicle bits occupy bits 20–40; no overlap. VV contacts are structurally disabled (vehicle mask has no vehicle category bits). VT contacts fire correctly. Z-level isolation is maintained per-pair. `make_vehicle_body()` changes one line: `sdef.filter.categoryBits = 1ull << static_cast<uint64_t>(bpos.z() + 30)`. `terrain_body.cpp` changes one line: `sdef.filter.maskBits = 1ull << static_cast<uint64_t>(z + 30)`.

**Concrete migration sequence (implement in this order):**

**Step 1 — filter separation** *(safe, independent, does not change behaviour for static bodies)*
Apply the VV filter fix above. Build, run tests. No behaviour change expected for `b2_staticBody`.

**Step 2 — retire tile-step VV detection**
In `move_vehicle()` (`map.cpp:1007`), the collision dispatch calls `vehicle_vehicle_collision()` which routes to the `vehicle_box2d_solve` path (Phase 2). Guard the whole VV branch with `#ifndef BOX2D_ENABLED`. VV damage is now sourced from Box2D hit events only (Step 4). Build, run tests.

**Step 3 — promote to `b2_dynamicBody`**
In `make_vehicle_body()`: change `bdef.type = b2_staticBody` → `b2_dynamicBody`; add `bdef.gravityScale = 0.0f`. In `step()` pre-step loop: for each vehicle body, apply engine force and steering torque:
```cpp
// Engine thrust: F = mass × (target_speed - current_speed) / dt, clamped to max thrust
const auto mass_kg = units::to_kilogram(veh->total_mass());
const auto target_mps = static_cast<float>(veh->cruise_velocity) / 100.0f;
const auto cur_mps    = b2Body_GetLinearVelocity(bid);
const auto fv         = veh->face_vec();
const auto thrust_n   = mass_kg * (target_mps - (cur_mps.x * fv.x + cur_mps.y * fv.y)) / dt;
b2Body_ApplyForceToCenter(bid, { fv.x * thrust_n, fv.y * thrust_n }, true);
```
Do NOT remove `on_vehicle_moved()` yet — tile-step still drives position for Step 3. The teleport fights Box2D dynamics but is still the position authority. The goal of Step 3 is only to make VT contact events fire. Build, verify hit events appear in logs.

**Step 4 — implement `dispatch_contact_events()`**
Iterate `b2World_GetContactEvents(world_).hitEvents`. For each event:
- `shapeIdA / shapeIdB` → `b2Shape_GetBody()` → `b2Body_GetUserData()` → `static_cast<vehicle*>`
- Guard with `vehicle_bodies_.count(ptr)` (terrain user data is encoded small integer — NOT a valid vehicle pointer)
- `ptr->angular_velocity_rads = b2Body_GetAngularVelocity(bid)` (VT spin from real contact impulse)
- For VV hit events (not reachable after Step 2 filter fix, but add assert to confirm)
Remove the Phase 5 `resolve_terrain_impulse()` call from `vehicle_move.cpp` once this is verified to produce equivalent angular spin.

**Step 5 — retire tile-step position authority**
This is the largest and riskiest step. Replace the `for (int count = 0; count < 100; ++count)` loop in `vehmove()` (`map.cpp:864–908`) with Box2D position readback. The loop currently runs `act_on_map()` → `move_vehicle()` which carries these effects that must be preserved:
- **Sound**: `vehicle_move.cpp` emits movement/engine sounds via `sounds::sound()` — move to idle() or a new per-tick emitter
- **Bash**: `part_collision()` bash path for terrain (already routed via `on_tile_bashed()` hook in Phase 3 for Box2D; verify completeness)
- **Creature collision**: `veh_coll_body` path in `vehicle_move.cpp` — keep as game-logic driven, triggered from position readback when tile anchor changes
- **Zone queries / FoV**: `vehicle_part_position` tile updates fire these automatically via `bub_ms_location()` helpers — gate to once per game tick (see "Tile churn throttling" below)
- **`of_turn` / `gain_moves()` / priority queue**: entirely retired; Box2D step at line 784 replaces the scheduler

Replacement code outline for `vehmove()` Step 5:
```cpp
// Box2D position readback (physics_pos already updated in step(), Phase 6)
for (auto& wv : vehicle_list) {
    vehicle& veh = *wv.v;
    const auto px = static_cast<int>(std::lround(veh.physics_pos.x));
    const auto py = static_cast<int>(std::lround(veh.physics_pos.y));
    const auto cur = veh.bub_ms_location();
    if (px != cur.x() || py != cur.y()) {
        displace_vehicle(veh, tripoint_rel_ms{px - cur.x(), py - cur.y(), 0});
        // creature collision: scan new tile occupants, call veh_coll_body equivalent
    }
}
```
`displace_vehicle()` is `map::displace_vehicle()` at approximately `map.cpp:6900`. It moves the vehicle in the submap grid and fires the downstream tile-update hooks.

**Step 6 — retire legacy fields** (Phase 12 prerequisite)
`of_turn`, `velocity`, `turn_dir`, discrete `turn()` calls, `gain_moves()`. These can only be removed after Step 5 is stable and all consumers migrated.

### Occupant handling (new work items)

**Tile churn throttling**: occupant tile position (`round(rotate(mount, θ) + anchor)`) must only be updated at game-tick boundaries, not at every 60Hz physics sub-step. Updating per sub-step fires zone queries, FoV checks, and occupant validation on every physics iteration — performance hazard during fast spins. Gate the occupant tile sync to once per game tick.

**Throw-from-seat angular velocity** (`vehicle_move.cpp:1867`): current formula `throw_from_seat = d_vel * rng(80,120) > move_resist` uses only linear velocity delta. Under full authority, T-bone spins produce real Box2D `ω`. Update to effective velocity:
```
r          = distance from seat mount to vehicle CoM (tiles → metres)
v_tangential = r * abs(angular_velocity_rads)      // from b2Body_GetAngularVelocity()
d_vel_eff  = hypot(d_vel_linear, v_tangential)     // combined throw impulse
throw_from_seat = d_vel_eff * rng(80, 120) > move_resist
```
This ensures passengers react to the spins this plan exists to produce, not just linear impacts.

---

## Phase 11: Ranged Combat

**Depends on Phase 3. Gated on tile-independent ranged combat rework.**

*Replace tile-traversal LOS with `b2World_CastRayClosest()` into persistent world.*

- `b2World_CastRayClosest(world, origin, translation, filter)` — first hit body, exact impact point, surface normal
- `b2Body_GetUserData()` → game object pointer for damage dispatch
- `b2World_CastRay()` with custom callback for penetrating rounds
- Cover calculation replaces per-tile modifier table with fractional occlusion from Box2D shape geometry
- Confirmed v3.0.0 API: `b2World_CastRay`, `b2World_CastRayClosest`, `b2World_OverlapAABB`

---

## Phase 12: Legacy Cleanup

**Depends on Phase 10 (movement migration complete).**

Systems retired after Phase 10:
- `of_turn`, `velocity` (integer cm/s), `turn_dir` fields and all consumers
- `angular_velocity_rads` transitional field
- Transient VV solve worlds (Phase 2 code)
- Transient terrain solve worlds (Phase 5 code — `resolve_terrain_impulse` wrapper)
- 1D elastic formula in `vehicle_vehicle_collision()` analytic branch
- Tile-step detection loop in `move_vehicle()`
- `get_collision_factor()` if superseded by Box2D restitution model
- Discrete `turn()` call chain

### Precalc retirement checklist (DD7)

**Gate**: all callsites below must be migrated to `part_world_offset(p, veh.physics_angle)` before these symbols are deleted.

**Symbols to retire:**
- `vehicle_part::precalc[2]` array — remove field from `vehicle_part`
- `vehicle::precalc_mounts()` — remove function
- `vehicle::refresh_precalc()` — remove shim
- `vehicle_part_position::precalc` readers in `map.cpp` tile-position helpers

**Callsite migration order** (complete hot→cold before retiring):
1. **Hot** (every tick): `occupied_points` rebuild, FoV blocking (`map::reachable_flood_steps`), collision detection part-position queries
2. **Warm** (on vehicle move): `part_at()` lookup helpers, `bub_ms_location()` per-part variants, creature adjacency
3. **Cold** (on event): AI pathfinding obstacle queries, spawn/mapgen positioning, debug draw, savegame part-position assertions

Audit command before starting: `lsp references precalc` in `src/vehicle_part.h` to get the full callsite list. Any callsite not yet migrated blocks the field deletion.

After Phase 11:
- Tile-traversal LOS implementation
- Per-tile cover modifier table

---

## Phase 13: Creature-Phase Bugfix *(✅ DONE — 2026-07-11)*

**Independent of all physics phases. Must be done before Phase 10 (movement migration) as Phase 10 changes the game loop around this code.**

### Bug A — Non-vertical momentum-exhausted path (original fix)

**File: `src/vehicle_move.cpp`** — `else` branch at line 889 (inside `veh_coll_body` case, inside `if(!vert_coll)`):

Current (lines 889–893):
```cpp
} else {
    // Vehicle's momentum isn't big enough to push the critter
    velocity = 0;
    break;
}
```

Replace with:
```cpp
} else {
    // Vehicle's momentum isn't big enough to push the critter
    velocity = 0;
    smashed  = true;  // ensures velocity-update path uses smashed=true multiplier; belt-and-suspenders with break
    break;
}
```

### Bug B — Vertical collision creature death never sets smashed (new find)

The `is_dead_state()` check at line 895 is inside the `if(!vert_coll)` block (guard at line 875). In vertical collisions, `apply_damage()` fires at line 853 and may kill the creature, but `smashed` is never set true. The do-while loop at line 912 continues iterating and calls `apply_damage()` on a dead creature until velocity decays to zero.

**File: `src/vehicle_move.cpp`** — insert after `apply_damage()` at line 853, before the `if(!vert_coll)` guard:

```cpp
// apply_damage is at line 853 (existing)
critter->apply_damage( driver, bodypart_id( "torso" ), obj_dmg );
// ADD these two lines immediately after:
if( critter->is_dead_state() ) {
    smashed = true;  // prevents loop re-entry in vertical collisions
}
```

**Verification (both bugs):** Drive at ~10 mph into a heavy creature (bear/moose) 20 times horizontally and 5 times at z-level drop. Vehicle stops adjacent each time; no overlap, no repeated `apply_damage` log messages after creature death.

---

## Critical Files & Anchors

Line numbers verified against current codebase (2026-07-11). Implementer must re-read before editing; numbers are hints, not guarantees.

| File | Symbol / Region | Phase | Verified line |
|---|---|---|---|
| `CMakeLists.txt` | `option(BOX2D ...)` + FetchContent block | Phase 1 | 803–827 |
| `src/CMakeLists.txt` | `target_link_libraries(...box2d)` + `BOX2D_ENABLED` define | Phase 1 | 219–222 |
| `src/map.cpp` | VV dispatch for-loop in `move_vehicle()` | Phase 4 | 1050–1052 |
| `src/map.cpp` | `map::vehicle_vehicle_collision()` function | Phase 4 | 1225–1400 |
| `src/vehicle_move.cpp` | `veh_coll_body` type assigned | Phase 13 | 636 |
| `src/vehicle_move.cpp` | `veh_coll_bashable` type assigned | Phase 5 | 656 |
| `src/vehicle_move.cpp` | `veh_coll_other` type assigned | Phase 5 | 662 |
| `src/vehicle_move.cpp` | do-while bash loop | Phase 5 | 721–912 |
| `src/vehicle_move.cpp` | `if(!vert_coll)` guard (Bug B insert point before here) | Phase 13 | 875 |
| `src/vehicle_move.cpp` | momentum-exhausted else branch (Bug A fix) | Phase 13 | 889 |
| `src/vehicle_move.cpp` | `get_collision_factor()` definition | Phase 2, 4 | 1396 |
| `src/vehicle_move.cpp` | `velo_vec()` definition | Phase 2 | 1360–1371 |
| `src/vehicle_move.cpp` | `face_vec()` definition | Phase 2 | 1385 |
| `src/vehicle.h` | `total_mass()` declaration | Phase 2 | 962 |
| `src/vehicle.h` | `get_collision_factor` declaration | Phase 2 | 69 |
| `src/vehicle.h` | `vehicles::cmps_per_tile = 178.816f` | TILE_M source | 61 |
| `src/vehicle.h` | velocity/turn fields (`velocity`, `of_turn`, `turn_dir`) | Phase 4 add `angular_velocity_rads` after `of_turn_carry` | 1728–1751 |
| `src/savegame_json.cpp` | `vehicle::deserialize()` | Phase 9 | 3167 |
| `src/savegame_json.cpp` | `vehicle::serialize()` | Phase 9 | 3375 |
| `src/savegame_json.cpp` | existing motion field serialize block (add physics fields after) | Phase 9 | 3379–3396 |

---

## Verification

### Phase 1 — Build clean
```sh
cmake --preset osx-arm-slim -DBOX2D=ON
cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles
```

### Phase 2 — Unit tests
```sh
./out/build/osx-arm-slim/tests/cata_test-tiles "[vehicle][box2d]"
```
Both tests pass. Test 1: momentum conserved ±5%. Test 2: angular velocity > 0.1 rad/s on off-center hit.

### Phase 4 — T-bone spin (BOX2D=ON binary)
Spawn sedan facing east, velocity ~30 mph. Spawn stationary vehicle facing north, one tile east. Drive into it off-center. After collision: target gains `angular_velocity_rads ≠ 0`; `face.dir()` changes by ≥15° over 2–4 ticks.

### Phase 10 — Movement migration smoke test
Vehicle moves, steers, and brakes at correct speed under Box2D integration. No tile-step loop in call stack. `of_turn` field absent. Orientation continuous.

### Phase 13 — Creature fixes
Drive at ~10 mph into bear/moose 20 times (horizontal). Drive off a z-level onto a creature 5 times. Vehicle stops adjacent every time; no overlap; debug log shows no `apply_damage` calls after creature death.

---

## Assumptions & Contingencies

**`b2ShapeDef` layout is v3.0.0-specific**: direct fields `friction`, `restitution`, `density`. Upgrade to v3.1+ requires `material.*` migration.

**Box2D restitution mix rule**: `b2MixRestitution(r1, r2) = max(r1, r2)` (verified in v3.0.0 `src/contact.c`). Setting restitution per-vehicle speed (old approach) gives `max(e_moving, get_collision_factor(0)=1.0) = 1.0` for any collision with a stationary target — fully elastic regardless of impact speed. **Fix**: set both bodies to `get_collision_factor(relative_delta_v)` so `max(e, e) = e`. Friction uses geometric mean `sqrt(f1*f2)`; both bodies at 0.3f gives `sqrt(0.09) = 0.3` — correct, no change needed.

**Fixed-timestep accumulator**: ~60 `b2World_Step(world, 1.0f/60.0f, 4)` per 1s logic tick. Increasing `subStepCount` does NOT fix tunneling — only smaller `timeStep` values do.

**Box2D non-determinism (co-op)**: One authoritative host runs Box2D. Clients receive state updates (positions, velocities, angular velocities). Clients do not run independent Box2D solves.

**`b2MakeOffsetBox` center parameter**: in the vehicle body's LOCAL frame. Since `bdef.rotation` encodes the vehicle heading, and mount coordinates have x=forward, y=lateral, the center `{cx_local, cy_local}` is already in the correct local frame. No additional rotation of the offset is needed.

**`bdef.rotation` uses `b2Rot{fv.x, fv.y}`**: `face_vec()` returns normalized `(cos θ, sin θ)` via `angle_to_vec(face.dir())`. `b2Rot` stores `{c, s}`. Direct assignment is correct. After Phase 10, use `b2MakeRot(physics_angle)` directly instead of `face_vec()`.

**Phase 2 angular velocity initializes to 0.0f**: `angular_velocity_rads` field is added to `vehicle` in Phase 4. Phase 2 uses `0.0f` as the initial angular velocity. After Phase 4 lands, update Phase 2 solver to read `v->angular_velocity_rads` instead.

**`to_kilogram` availability**: defined in `units_mass.h:72` as `units::to_kilogram`. In `vehicle_move.cpp` and `vehicle.cpp` it is used unqualified (via `using`). In `src/physics/veh_box2d_solve.cpp` use `units::to_kilogram` explicitly to avoid depending on the `using` import.

**Phase ordering rationale**: Phases 2 and 4 (transient VV solve) ship independently and provide immediate correctness gains (T-bone spin). They are superseded by Box2D contact events after Phase 10, but can coexist during migration.

**Transitional fields** (`angular_velocity_rads`, and any spin accumulator): exist only between Phase 4 and Phase 10. Must NOT be serialized before Phase 9. Removed in Phase 12.

**No CMakePresets.json preset for BOX2D**: `-DBOX2D=ON` must always be passed explicitly on the cmake configure line. This is intentional (Box2D is an opt-in experimental build).

## Session findings: what the failing vehicle tests actually mean

Measured by toggling `box2d_position_authority` inside a single binary, so build
configuration is never a variable.

| Suite | Status | Player-facing? |
|---|---|---|
| `vehicle_rails_test` | **fixed** — 0/5 cases -> 5/5 (287 -> 4,284 assertions) | was: trains derail and skid |
| `vehicle_ramp_test` | still 83 assertions failing | **no** — sets `box2d_position_authority = false` at lines 109/159, so it exercises the legacy tile-step path. Ramps verified working under authority by `box2d_authority_vehicle_climbs_ramp` |
| `vehicle_efficiency_test` | 59 -> fewer, gap remains | **yes** — vehicles travel 7-27% less per unit fuel |
| `ranged_vehicle_recoil_test` | 2 assertions | **no** — order-dependent |

### Rails: root cause

`vehicle_movement::process_movement_on_rails()` is called at `vehicle_move.cpp:1606`,
but the authority early-return is at `1599`. A rail vehicle under authority therefore
never had its heading corrected to the track: it turned to whatever the driver asked,
derailed, and then skidded, because `is_on_rails()` going false stops the skid guard
at `1574` from suppressing it. Rail motion is a *kinematic constraint*, which a free
rigid-body integrator cannot express, so `on_vehicle_added()` now denies authority to
`can_use_rails()` vehicles. The body is still built, so collisions still work.

### Efficiency: what is ruled out

All measured directly with authority toggled in one binary:

- **fuel burn** — mean `load` 599.2 either way, 800 `consume_fuel` calls each
- **drag** — 1062.13 identical, skid factor 1.000 both
- **velocity** — mean and peak identical
- **acceleration lag** from `step_turn()` (line 448) preceding `act_on_map()` (line
  556) — `act_on_map` never changes velocity, in 0 of 800 turns
- **sub-tile remainder** — real, and fixed (`on_vehicle_moved` now carries the
  fraction across whole-tile displacements), but worth only ~4 points of the gap

The tile-step path hits the expected 12.499 tiles/turn almost exactly (12.48
measured); the authority path reaches 11.59. Roughly 0.9 tiles/turn is still
unaccounted for and is the next thing to chase.

### Not a test failure at all: colliders survived world teardown

`PhysicsWorld` is built once in the `map` constructor, `map` is a `pimpl<map>` built
once in `game::game()`, and `terrain_bodies_` is keyed by `tripoint_abs_sm`. Nothing
freed it but the destructor, which never runs mid-session, so `MAPBUFFER.clear()` on
returning to the menu left world A's colliders registered at coordinates holding
world B's terrain. Measured `terrain_body_count() == 1` after teardown. Fixed via
`clear_world_bodies()` at the `mapbuffer::clear()` choke point.

### Efficiency: resolved, and the residue

Two real defects, both found by decomposing `vehicle_efficiency`'s **per-cycle**
distance instead of its totals. Totals hid both; the histogram made them obvious.

1. **A declined tile step zeroed velocity.** The readback rewind called `veh.stop()`
   whenever the walk missed its physics target. But `move_vehicle()` already applies
   collision speed loss inside `part_collision()`, and `blocked` does not mean "hit
   something" — it also covers the first turn after a vehicle is placed or displaced,
   before `of_turn` is funded. Measured: velocity 2235 zeroed on turn 1 with the
   target 12 tiles away and nothing in between, then 14 turns to recover. In
   gameplay, one declined step halted the vehicle.

2. **The world was stepped before game logic settled velocity.** `step_turn()` sat at
   the top of `map::vehmove()`, before vehicles were even given movement points, so
   integration used the *previous* turn's velocity. Invisible at cruise, which is why
   it survived; but every speed change cost a full turn on the stale value, and after
   a stop the first turn produced no movement at all. Now stepped immediately before
   the readback: `act_on_map()` settles velocity -> step -> apply positions.

`vehicle_efficiency` 59 -> 15 failed assertions; full `[vehicle]` 101 -> 44.

**The residue is an overshoot, and is deliberately left red.** The 15 remaining
failures are all on the *upper* bound: vehicles now travel slightly too far.
Regenerating the constants on the shipping build gives ±5% for 85 of 144 slots, but
up to **1.25x for light, fast-accelerating vehicles** (scooter, motorcycle,
superbike, quad bike) concentrated in the stop-start conditions.

That is the mirror image of defect 2: stepping a whole turn at *end-of-turn* velocity
over-credits acceleration exactly as stepping at start-of-turn under-credited it. The
honest fix is to integrate the acceleration phase properly — sub-step it, or use the
mean velocity across the turn — not to adopt constants that bake the overshoot into
the baseline. Doing that would repeat this plan's own recorded mistake of trusting "a
number that looks authoritative because a test went green".

It is also a balance decision: it changes how quickly vehicles pull away.

### Collider lifecycle: second leak, at `map::load()`

`map::load()` drops every submap with a bare
`std::fill( grid.begin(), grid.end(), nullptr )` and never routes through
`on_submap_unloaded`, then `loadn()` builds fresh bodies for the new bubble. Terrain
colliders therefore accumulated across every fast travel and every game start.

Worse than the leak: terrain bodies are keyed by absolute submap but *positioned* in
bubble coordinates, and a distant load carries no shift delta, so `on_map_shifted`
never corrected them either. Stranded bodies sat wherever those tiles used to appear
on screen. Fixed by reusing `clear_world_bodies()` — the same invariant as the
world-teardown fix, at a second call site.

**The test for it is an explicit `[!shouldfail]` pending spec, not a passing test.**
The harness cannot build terrain colliders at all: `on_submap_loaded()` fires only
from `map::loadn()` for the player's z-level, `on_tile_changed()` only refreshes
submaps that are *already* registered, and a fresh test world is open field where no
tile earns a collider. `count_home` comes out 0 and the invariant then holds
trivially. A `REQUIRE( count_home > 0 )` guard makes that vacuity fail loudly — I
wrote two silently-vacuous versions of this test before adding it.

### Cross-test leakage: root cause found

`clear_map()` reset only `abs_sub`'s z, inheriting its xy from whatever the previous
`TEST_CASE` left behind. Anything triggering `map::update_map()` mid-test shifts that
anchor — `map::board_vehicle()` does, via the avatar move path — and it was never
wound back, so the bub<->abs relationship differed per test by execution order.

Re-anchoring canonically fixes `ranged_vehicle_recoil_test` (both sites) and the
Box2D bash spec. `vehicle_ramp_test:174` survives it and has a different cause; note
that its in-suite abort is what *masks* the ramp suite's 83 real assertions, so the
suite total understates that file.
