# Vehicle baseline triage (issue #166)

Status: **static triage only.** This lane has no compiler and must not run `cata_test`. Every run
column below is **not observed**. Nothing here is a pass/fail measurement.

Branch `factory/166-triage-flaky-vehicle-baseline-failures`, base HEAD `cd5ff54dd6`.

## Acceptance status

| Step                                                                                                    | Status                               |
| ------------------------------------------------------------------------------------------------------- | ------------------------------------ |
| Rebuild `cataclysm-bn-tiles` and `cata_test-tiles` from HEAD                                            | not observed (no build in this lane) |
| `[vehicle]` run x5, isolated, per case                                                                  | not observed                         |
| Full suite run x1, `rolling_steering_turns_vehicle` and `pulling_away_with_cruise_accelerates` recorded | not observed                         |

## Baseline cases

| Case                                   | Location                           | Isolated runs 1-5 | Full suite   | Classification           |
| -------------------------------------- | ---------------------------------- | ----------------- | ------------ | ------------------------ |
| `rolling_steering_turns_vehicle`       | `tests/vehicle_test.cpp:1260-1275` | not observed      | not observed | unclassified (needs run) |
| `pulling_away_with_cruise_accelerates` | `tests/vehicle_test.cpp:1352-1366` | not observed      | not observed | unclassified (needs run) |

No case is classified as a test defect, a product bug, or flaky, because each needs run output to
decide. The static reading below narrows the candidates.

### `rolling_steering_turns_vehicle`

- Uses `make_presteer_fixture()` (`:1177-1187`), which does not board the avatar. Control comes from
  the debug tag `IN_CONTROL_OVERRIDE` (`src/vehicle.cpp:484`, consumed in `player_in_control`).
- Steers with `pldrive({1,0,0})` (`src/vehicle_move.cpp:1393`) and runs one `vehmove()`, then
  expects `face.dir() == 15` at `:1274`.
- The assertion depends on the presteer snap at `src/vehicle_move.cpp:1461-1467` and on
  `presteer_steps` clamping. Neither is exercised by this run, so the failing value is unknown.

### `pulling_away_with_cruise_accelerates`

- Uses `make_driving_fixture()` (`:1283-1303`): avatar boarded on CONTROLS, `player_in_control`
  true without the override.
- Throttle: `pldrive({0,-1,0})` with cruise on calls `cruise_thrust(179)`
  (`src/vehicle_move.cpp:1474-1477`). `cruise_thrust` clamps to `max_velocity()` at
  `src/vehicle_move.cpp:379-382`. A non-positive `max_velocity()` would fail `:1362`.
- Motion: three `vehmove()` calls. The cruise branch calls `thrust(±1)`
  (`src/vehicle.cpp:3255-3256`). `thrust()` returns early when `accel == 0` while thrusting
  (`src/vehicle_move.cpp:210-220`), which would leave `velocity` at 0 and fail `:1365`.
- The two checks are `:1362` (`cruise_velocity > 0`) and `:1365` (`velocity > 0`). Which one fails
  is unknown.
- Sibling `pulling_away_with_cruise_after_presteer_accelerates` (`:1370-1391`) uses the same
  fixture and passes. The only difference is a preceding `pldrive({1,0,0})`. That is a clue, not a
  diagnosis.
- [INFERENCE] Both fixtures call `add_vehicle(..., 100, 0)` (`:1182`, `:1288`). The meaning of the
  trailing `0` was not verified. If it leaves the car unfuelled, `current_acceleration()` and
  `max_velocity()` could be zero, but that would also hit the sibling, so it does not explain the
  split by itself.

### Discriminating run (to do on a machine with a build)

Add temporary output, then remove it before any commit:

1. Before and after each `vehmove()` in `pulling_away_with_cruise_accelerates` and
   `rolling_steering_turns_vehicle`, print `velocity`, `cruise_velocity`, `current_acceleration()`,
   `max_velocity()`, `face.dir()` and `turn_dir`.
2. Run each case alone, then with the sibling first, and compare the printed values.
3. Whichever value differs between the failing and passing variants is the cause. Classify it as a
   product bug (file:line and the one-line cause) or a test defect (fix in `tests/`).

## Fixture and leak checks (static, verified)

- `make_presteer_fixture()` and `make_driving_fixture()` both call `clear_all_state()`
  (`tests/vehicle_test.cpp:1178`, `:1284`). The fixtures do not inherit state.
- The cross-test anchor leak is fixed. `clear_map()` re-anchors the bubble to a canonical xy
  (`tests/map_helpers.cpp:117-132`).
- Vehicles are cleared with the map. `clear_map()` calls `wipe_map_terrain()`
  (`tests/map_helpers.cpp:137`), which calls `clear_vehicles()` (`:40-51`). A vehicle left over
  from a previous test does not survive into these fixtures.
- No test defect was confirmed in the fixtures, so no `tests/` edits are made in this lane.

## Ticket corrections

The ticket's line references do not match the current file (1453 lines):

| Ticket                           | Actual                                                                                                                 |
| -------------------------------- | ---------------------------------------------------------------------------------------------------------------------- |
| `count_home > 0` at `:1121`      | `tests/vehicle_test.cpp:1120`                                                                                          |
| `in_vehicle` check at `:1298`    | `:1295` (`:1298` is `you.controlling_vehicle = true`)                                                                  |
| seat-move checks at `:1454-1455` | `:1441-1443` (`avatar_cannot_walk_inside_rolling_vehicle`) and `:1451-1452` (`avatar_can_walk_inside_stopped_vehicle`) |

`box2d_map_load_does_not_accumulate_colliders` (`:1086-1124`) is tagged `[!shouldfail]`. Its
`REQUIRE(count_home > 0)` failing is an expected failure. It is reported as a failure only if the
spec passes, so it is not a baseline failure.

## Not touched

- `tests/npc_vehicle_muscle_test.cpp` `npc_muscle_engine_energy_consumption` (`:242-289`) is not in
  the baseline. Its `CHECK(initial_fuel >= 10)` at `:285` is weak. It is not changed here, because
  the failure is not observed.
- `baselineFailures` in `tools/factory/config.json` and `config.ts` is a protected path and is not
  edited in this lane.
