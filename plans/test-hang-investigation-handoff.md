# Test Hang Investigation Handoff

## Goal

Fix the test hang in shard 3 that prevents parallel test execution from completing.

## Current State

- Our activity fixed-window fix is committed and all activity tests pass.
- The hang is a **pre-existing** issue in the test suite, unrelated to our changes.
- The hang only manifests with `--shard-index 3` (full shard), not with manual test filters.

## Key Findings

| Configuration | Result | Wall Time |
|---|---|---|
| Reload tests alone (`reload_gun_with_swappable_magazine|automatic_reloading_action`) | ✅ Pass | ~5s |
| Reload tests + first 42 shard 3 tests | ✅ Pass | ~5s |
| Reload tests + first 85 shard 3 tests | ✅ Pass | ~5s |
| Reload tests + first 130 shard 3 tests | ✅ Pass | ~5s |
| Shard 3 with `--shard-index 3` (full 169 tests) | ❌ Hangs | ~280s+ |
| Shard 3 with `--shard-index 3` excluding reload tests | ✅ Pass | ~96s |
| Shard 3 with `--shard-index 3` excluding `automatic_reloading_action` | ✅ Pass | ~96s |

## What We Know

1. **The hang is in `automatic_reloading_action`** - specifically at `process_activity(dummy)` in `tests/reloading_test.cpp:151` (inside `reload_a_revolver`).
2. **The hang only happens when `automatic_reloading_action` is preceded by the full shard 3 test set.**
3. **No subset of tests reproduces the hang** - even the full first 130 tests + reload tests pass fine.
4. **The test lists are identical** between `--shard-index 3` and manual filters (verified with `diff`).
5. **Isolated user-dir (`--user-dir=`) doesn't fix the hang** - it's not a SQLite/resource contention issue.
6. **`vehicle_drag` and `vehicle_efficiency` pass individually** - earlier suspicion was wrong.

## What We Don't Know

1. **Why `--shard-index` triggers the hang but manual filters don't** - the test lists are identical.
2. **Which specific test pollutes the state** - we haven't found the polluting test yet.
3. **What global state is being corrupted** - could be a static variable, singleton, or game state that's not properly reset between tests.

## Next Steps

1. **Try running the full shard 3 with `--success` to see the exact test execution order** - compare with manual filter order.
2. **Try running tests one by one in the exact shard 3 order** to find the polluting test.
3. **Check if there's a Catch2 shard selection bug** - maybe the shard includes tests we're not seeing.
4. **Check for static/global state in the reload activity code** that might not be reset between tests.
5. **Try running the shard with `--durations Yes`** to see timing per test and identify where it hangs.

## Files Involved

- `tests/reloading_test.cpp` - the hanging test (`automatic_reloading_action`, `reload_a_revolver`)
- `src/game.cpp` - `process_activity()` and `execute_activity_fixed_window_skip()`
- `src/player_activity.cpp` - `player_activity::do_turn()`
- `tests/state_helpers.cpp` - `clear_states()` (test cleanup)

## Commands Used

```bash
# Run shard 3 (hangs)
./out/build/osx-arm-slim/tests/cata_test-tiles --shard-count 4 --shard-index 3 --user-dir=/tmp/test_shard3/

# Run reload tests alone (passes)
./out/build/osx-arm-slim/tests/cata_test-tiles "reload_gun_with_swappable_magazine|automatic_reloading_action" --user-dir=/tmp/test_reload/

# Run shard 3 excluding reload tests (passes in ~96s)
./out/build/osx-arm-slim/tests/cata_test-tiles --shard-count 4 --shard-index 3 --user-dir=/tmp/test_exclude/ ~"reload_gun_with_swappable_magazine" ~"automatic_reloading_action"

# List shard 3 tests
./out/build/osx-arm-slim/tests/cata_test-tiles --list-tests --shard-count 4 --shard-index 3
```

## Theory

The most likely theory is that a test in the shard 3 set leaves **global static state** that corrupts the reload activity processing. The fact that manual filters pass but `--shard-index` hangs suggests:

1. Either the test execution order is different (but we verified it's not)
2. Or there's a Catch2 shard selection quirk we're missing
3. Or the test count matters (maybe a threshold of tests triggers the issue)

Try running the shard with `--durations Yes --success` to see the exact execution order and timing.
