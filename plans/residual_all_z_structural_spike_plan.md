# Tier 0b — Pin the residual all-z structural spike

## Context

Post-walking-hitch-fix, ~2/10 shifts still spike to **20–23ms all-z STRUCTURAL
rebuild** (outside=11ms + trans=7ms). The trigger is **unpinned** — not the
z-over-invalidation that was fixed (Phase 1 of the walking-hitch fix translates
outside_cache and shifts dirty bitsets, so the common shift no longer
blanket-dirties all z). Loader ≈1ms rules out mapgen.

**The in-tree probe already exists:** `[shift-probe][invalidate-bt]` emits a
backtrace when `invalidate_map_cache` is called. The backtrace data just needs
to be collected and correlated.

## Root cause candidates

From `build_map_cache()` callers in `src/game.cpp`:

| Caller | Line | Frequency | Notes |
|--------|------|-----------|-------|
| `m.invalidate_map_cache(get_levz())` → `build_map_cache` | 3794-3795 | On game load | Rare |
| `resize_reality_bubble_to` → `invalidate_map_cache` chain | 13929 | On option change | Manual |
| `vertical_move` → `build_map_cache` | 10087 | On z-change | Rare |
| `dimension-load` → `here.invalidate_map_cache(z)` → `build_map_cache` | 15091-15094 | On dimension transition | **Candidate** |
| `update_map` → `build_map_cache` | 15548 | Per shift | The fixed path |

The dimension-load path (`game.cpp:15091-15094`) calls `invalidate_map_cache`
for every z, which sets **all per-submap dirty bitsets to `.all()`** — forcing a
full structural rebuild identical to the pre-fix cost.

## Diagnosis steps

### Step 1 — Collect backtraces (one session)

1. Ensure `BACKTRACE` is enabled in the build (add `-DBACKTRACE=ON` to cmake args).
2. Build and run:
   ```sh
   cmake --build out/build/osx-arm-slim --target cataclysm-bn-tiles
   ```
3. Walk across submap boundaries until you feel/see a hitch.
4. Search `debug.log` for `[shift-probe][invalidate-bt]`:
   ```sh
   rg '[invalidate-bt]' ~/Library/Application\ Support/Cataclysm-BN/config/debug.log
   ```
5. For each hit, correlate the backtrace to the call site in `src/game.cpp`
   (or `src/map.cpp:8110`/`src/activity_handlers.cpp`/etc.).

### Step 2 — Identify the stray all-z invalidate

If the dimension-load path (game.cpp:15091) is the trigger:
- It calls `invalidate_map_cache(z)` for each `z`, each of which `.set()`'s
  the per-submap bitsets → forces full rebuild on the subsequent `build_map_cache`.
- The call is correct-by-intent (dimension data changed) but should be refined:
  - If the dimension load only affects 1–2 z, pass the specific z.
  - If the dimension load truly dirties all z, accept it as a rare cost.

Other candidates:
- `game.cpp:925-931` — `teleport`/`reevaluate` path; also calls `invalidate_map_cache`.
- `game.cpp:9988` — `vertical_move` clears submap on old z.
- `game.cpp:8478/8487` — ranged/fire actions.
- `game.cpp:3794-3797` — game load (expected).

### Step 3 — Fix

If the trigger is a single stray all-z invalidate that runs on a normal walking
shift (i.e., it's called from some periodic sim path, not a rare event):
- Gate the `invalidate` to only mark affected z-levels.
- Or remove the redundant `invalidate` if the folowing `build_map_cache` already
  handles the same dirtiness correctly.

If the trigger is an inherently-rare path (dimension load, game load): document
as accepted and move on. The spike is real but not frequent enough to block
Tier-1 work.

## Verification

1. Before fix: walk ~50 submap crossings; count spikes >16ms.
2. After fix: repeat; spikes should drop to ≈0.
3. Confirm `[shift-probe][invalidate]` appears only on legitimately rare paths.
