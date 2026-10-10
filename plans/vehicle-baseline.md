# Vehicle baseline triage (issue #166)

Branch `factory/166-triage-flaky-vehicle-baseline-failures`. Observed on MSVC RelWithDebInfo,
seed 1, absolute test-exe paths. Earlier relative-path runs are void (they may have executed
bootstrap's binary).

## Acceptance status

| Step                                      | Status                                  |
| ----------------------------------------- | --------------------------------------- |
| Build `cata_test-tiles` from working tree | observed, ok (`build_fix2.log`)         |
| `[vehicle]` seed 1                        | observed (`vehicle_fix2.log`)           |
| `[vehicle]` declaration order             | observed (`vehicle_decl2.log`)          |
| Full suite `~[.]` seed 1                  | observed (`full_fix2.log`), see Results |
| Isolated runs x5 per case                 | not done                                |

## Results: full suite, seed 1

1430 cases: 1421 passed, 5 failed, 1 skipped (not identified), 3 failed as expected.

Unexpected failures, all in the baseline:

| Case                                                       | Location                                      | Note                        |
| ---------------------------------------------------------- | --------------------------------------------- | --------------------------- |
| `rolling_steering_turns_vehicle`                           | `tests/vehicle_test.cpp:1274` (`0 == 15`)     | Box2D steering, see below   |
| `driver_items_craft_by_recipe_id_makes_the_item`           | `tests/driver_items_test.cpp:450`             | not investigated            |
| `place_player_can_safely_move_multiple_submaps`            | `tests/map_test.cpp:586`                      | not investigated            |
| MSX++UnDeadPeopleEdition cross-sheet sprite audit          | `tests/tileset_config_audit_test.cpp:215-232` | not investigated (5 checks) |
| the rebuild plan lists the levels the dirty state licenses | `tests/level_cache_freshness_test.cpp:675`    | not investigated            |

Expected failures, not in the baseline: `avatar diving` (`[!mayfail]`),
`all_nutrition_starve_test` (`[!mayfail]`), and `box2d_map_load_does_not_accumulate_colliders`
(`[!shouldfail]`, `tests/vehicle_test.cpp:1120`).

Removed from `baselineFailures` (pass in this run):
`box2d_terrain_colliders_build_and_rebuild`, `pulling_away_with_cruise_accelerates`.

## Fixed in test setup

### `can autodrive` is order-dependent (leak, fixed)

- Under `[vehicle]` seed 1 it failed (`1 == 0` at `tests/vehicle_test.cpp:531`). Solo and in
  declaration order it passed.
- Probe output: `Character::sight_max` stuck at 0 (`unimpaired_range() == 0`), so only the
  avatar's own tile was visible. Light was ruled out (`cached=100`, `light_mod=0`).
- Cause: three `[vehicle]` tests apply a 365-day blindness to the avatar and never remove it
  (`tests/vehicle_drag_test.cpp:38`, `tests/vehicle_efficiency_test.cpp:52`,
  `tests/vehicle_rails_test.cpp:97`). `clear_character()` cleared effects but did not refresh the
  cached sight limits. Fix: `recalc_sight_limits()` after the effect is cleared
  (`tests/player_helpers.cpp`). No `src/` change.

### Avatar placed before map clear (leak, fixed)

- `clear_states()` (`tests/state_helpers.cpp`) placed the avatar before `clear_map()`.
  `clear_map()` re-anchors the reality bubble, so `bub_to_abs()` (avatar-derived) and
  `map_local_to_abs()` (map-derived) disagreed and `critter_at` returned nullptr.
- Fix: the avatar is placed after the map is cleared. This fixed `vehicle_occupant_commit`,
  `vehicle_collision_with_hallucination`, `vehicle_rails` and `pulling_away_with_cruise_accelerates`
  in the seed-1 `[vehicle]` run.

## Open: `rolling_steering_turns_vehicle` (product path, not fixed)

- The fixture (`make_presteer_fixture`) requires `box2d_position_authority`. Under authority,
  `vehicle::act_on_map()` returns early (`src/vehicle_move.cpp:1737`), and facing is read back from
  Box2D body rotation (`src/physics/physics_world.cpp:628-629`).
- Fails solo as well (`rolling_steering_turns_vehicle` alone, seed 1: `0 == 15` at
  `tests/vehicle_test.cpp:1274`), so it is not order-dependent and not a test-setup leak.
- The test assumes the legacy tile-step steering path, but its fixture runs under Box2D position
  authority. Not traced: why one `vehmove()` tick does not turn the body.
- Decision (user): keep the entry in `baselineFailures` and record this as an open product defect.
  No code or test change in #166.

## Ticket corrections

The ticket's line references do not match the current file (1453 lines):

| Ticket                           | Actual                                                                                                                 |
| -------------------------------- | ---------------------------------------------------------------------------------------------------------------------- |
| `count_home > 0` at `:1121`      | `tests/vehicle_test.cpp:1120`                                                                                          |
| `in_vehicle` check at `:1298`    | `:1295` (`:1298` is `you.controlling_vehicle = true`)                                                                  |
| seat-move checks at `:1454-1455` | `:1441-1443` (`avatar_cannot_walk_inside_rolling_vehicle`) and `:1451-1452` (`avatar_can_walk_inside_stopped_vehicle`) |

## Not touched

- `tests/npc_vehicle_muscle_test.cpp` `npc_muscle_engine_energy_consumption` (`:242-289`) has a weak
  `CHECK(initial_fuel >= 10)` at `:285`. Not changed; its failure was not observed.
- The four non-vehicle baseline entries (`driver_items`, `place_player`, MSX, rebuild plan) are
  unchanged and remain in the baseline.
- `baselineFailures` (`tools/factory/config.ts`) is shrunk by two entries and nothing is added. It is
  a protected path in a factory lane.
