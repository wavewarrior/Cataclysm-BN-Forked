# Fix: activity_fixed_window_test failing assertions

## Goal
Fix 3 failing assertions in `tests/activity_fixed_window_test.cpp:113, 114, 117-118` for test case `"fixed window activity skip completes a short wait with active creatures"`.

## Test Setup
- `duration = 30_seconds` → `to_moves<int>(30_seconds)` = 3000 moves
- `wait_activity_actor::start()` sets `progress.moves_total = 3000`
- Each `player_activity::do_turn()` consumes 100 moves → exactly 30 iterations needed
- Weather `nextweather = turn + 10_minutes` (600 turns away, so `activity_fixed_window_duration()` = min(300, 600) = 300 turns)
- Spawns 1 monster + 1 NPC (SHOPKEEP) to test "active creatures" path
- Places activated `gasbomb_act` timer to verify item timer decrements

## Current Failures

| Line | Assertion | Expected | Actual |
|------|-----------|----------|--------|
| 113 | `calendar::turn == start_turn + duration` | `start + 30` | `start + 31` (off-by-one) |
| 114 | `!static_cast<bool>(g->u.activity)` | `false` (cleared) | `true` (unique_ptr still exists) |
| 118 | `timer >= starting - 30 - 1` | `≥ 19` | `18` (timer decremented by 32) |

Line 117 passes: `timer <= starting - 30` → `18 <= 20` ✓ (timer went too far, but upper bound still passes)

## Root Causes

### 1. Off-by-one: 31 turns instead of 30
The skip loop at `src/game.cpp:2459-2581` increments `calendar::turn` and `skipped_turns` at the TOP of each iteration (lines 2469-2470), THEN processes the activity. The break check at line 2508 fires AFTER `process_activity()` completes the activity on iteration 30 (0-indexed), but `++skipped_turns` already ran for that iteration.

**Flow:**
- Iteration 0: calendar += 1, skipped=1, process_activity() → moves_left=2900
- Iteration 1: calendar += 1, skipped=2, process_activity() → moves_left=2800
- ...
- Iteration 29: calendar += 1, skipped=30, process_activity() → moves_left=0, complete()=true
- Iteration 30: calendar += 1, skipped=31, process_activity() → moves_left=-100 (already done), complete()=true → BREAK

**Wait, re-reading the code:** The break at line 2508 checks `!u.activity || !*u.activity || u.activity->complete()`. On iteration 29, after `process_activity()`, `complete()` returns true → breaks. So 30 iterations, not 31.

But debug output shows `[SKIP BREAK] activity end: turn=30 complete=1`. This means `turn_index=30`, which is the 31st iteration (0-indexed). So the loop runs 31 times before breaking.

**The real issue:** On iteration 29, `process_activity()` makes `moves_left = 0` and `complete() = true`. The break at line 2508 fires. But wait — `turn_index=30` in the debug output means we're on the 31st iteration. Let me trace more carefully:

Actually looking at the check at line 2508: `if( !u.activity || !*u.activity || u.activity->complete() )`. After `process_activity()` on iteration 29, `complete()` should be true. But the debug says `turn=30`. This means either:
- The activity completes on iteration 30 (31st iter), OR
- There's a second break check at line 2577 that fires later

Looking at line 2577: `if( !activity_continues || u.activity->complete() )` — this is a SECOND break check at the end of the loop body. If the first break at line 2508 doesn't fire, the loop continues through monster/NPC moves and reaches the second break.

**Most likely:** `player_activity::complete()` returns `actor->progress.complete()` which checks `moves_left <= 0`. On iteration 29, `moves_left` goes from 100 → 0, so `complete()` = true. The break at line 2508 SHOULD fire at `turn_index=29`.

But debug output says `turn=30`. This suggests the break fires at the SECOND check (line 2577), not the first (line 2508). Why wouldn't line 2508 fire?

**Hypothesis:** `process_activity()` at line 2507 calls `u.activity->do_turn(u)` which does `actor->progress.moves_left -= 100`. But `wait_activity_actor::do_turn()` is empty — move consumption happens in `player_activity::do_turn()` itself. Let me check if `complete()` is being called correctly.

Actually, re-reading `player_activity::do_turn()` (line 453-530 in player_activity.cpp): it calls `actor->do_turn(*this, p)`, then checks completion. The `complete()` check at line 2508 happens AFTER `process_activity()` returns.

**The real off-by-one:** The loop structure is:
1. Increment calendar, skipped_turns
2. Process turn (gives 100 moves)
3. Process activity (consumes 100 moves)
4. Check complete → break if done

So after 30 iterations (turn_index 0-29), the activity is done. The break fires at turn_index=29. Calendar advanced by 30. This should be correct.

But debug says `turn=30`. Something is causing an extra iteration.

**Possibility:** The `process_activity()` while-loop at `src/game.cpp:2294` might be running more than once per skip iteration if `u.moves > 0` after the first `do_turn()`. But `player_activity::do_turn()` sets `p.moves = 0`, so the while loop should exit after one iteration.

**Need to investigate:** Why does `turn_index=30` in the break? Is there an extra calendar tick somewhere?

### 2. Activity not cleared
After skip, `g->u.activity` is a non-null `unique_ptr<player_activity>` pointing to an activity with `type = ACT_NULL`. The test checks `static_cast<bool>(g->u.activity)` which tests the `unique_ptr` itself (not `player_activity::operator bool()`).

The normal `do_turn()` flow (after line 1989) has activity cleanup logic that the skip path bypasses via early return at line 1887.

**Fix:** After the skip loop breaks on completion, reset the activity:
```cpp
if( u.activity && u.activity->complete() ) {
    u.activity->finish( u );
    u.activity.reset();
}
```
Or call `u.assign_activity( {} )` or similar cleanup.

### 3. Timer decrements by 32 instead of 30/31
`run_activity_skip_batch_turns(skipped_turns)` at line 2582 calls `process_items(skipped_turns)`. If `skipped_turns = 31`, timer should decrement by 31. But actual decrement is 32.

**Possibilities:**
- `process_items()` adds an extra turn internally
- `run_activity_skip_batch_turns()` passes a different value
- There's an extra calendar tick somewhere else (e.g. `timed_events.process()` or `mission::process_all()`)

## Files Involved
- `src/game.cpp` — skip loop at lines 2451-2584, `do_turn()` at 1868-2240
- `src/player_activity.cpp` — `do_turn()` at 453-530, `complete()`, `finish()`
- `src/player_activity.h` — `unique_ptr<player_activity>` member, `operator bool()`
- `src/activity_actor.cpp` — `wait_activity_actor` implementations
- `src/activity_actor_definitions.h` — `wait_activity_actor` class def at 1414-1441
- `tests/activity_fixed_window_test.cpp` — test at line 79-119

## What's Already Done
- ✅ Moved `new_game = false` before skip attempt (lines 1883-1885)
- ✅ Swapped `u.process_turn()` before `process_activity()` in skip loop (line 2505 before 2507)
- ✅ Confirmed skip executes: `[SKIP BREAK] activity end: turn=30 complete=1`
- ✅ Traced `wait_activity_actor::start()` → `progress.emplace(…, to_moves<int>(wait_duration))` → 3000 moves
- ✅ Traced `player_activity::do_turn()` → `actor->progress.moves_left -= 100`, `p.moves = 0`
- ✅ Confirmed `process_activity()` while-loop runs exactly once per skip iteration
- ✅ Read `run_activity_skip_batch_turns()` and `process_items()`
- ✅ Read `activity_fixed_window_duration()` → `min(5_minutes, nextweather - turn)`

## Remaining Debug Tracing
There are still `std::cerr` debug lines in `src/game.cpp` at lines 2509, 2513, 2517, 2578. Remove these after fixing.

## Next Steps
1. **Add debug tracing** to count exact iterations and track `moves_left` during skip loop
2. **Fix off-by-one**: Determine why `turn_index=30` (31 iters) instead of `turn_index=29` (30 iters). Either:
   - The activity needs exactly 30 `do_turn()` calls but the loop runs 31 due to break ordering
   - Or there's a hidden extra iteration
3. **Fix activity cleanup**: After skip loop, if activity completed, call `u.activity->finish(u)` then `u.activity.reset()`
4. **Fix timer count**: Ensure `skipped_turns` passed to `run_activity_skip_batch_turns()` matches actual calendar advance
5. **Remove debug tracing**
6. **Build & test**: `cmake --build --preset osx-arm-slim --target cata_test-tiles` then run test

## Build Command
```sh
cmake --build --preset osx-arm-slim --target cata_test-tiles
./out/build/osx-arm-slim/tests/cata_test-tiles "fixed window activity skip completes a short wait with active creatures" -s
```

## Key Insight from Summary
> `static_cast<bool>(g->u.activity)` tests the `unique_ptr` itself, not `player_activity::operator bool()`. After activity completion, type becomes `ACT_NULL` making `*u.activity` false, but the `unique_ptr` itself is still non-null. The test expects the `unique_ptr` to be null/reset.
