# Tier 0b — Pin the residual all-z structural spike

## STATUS (measured 2026-08-02) — DIAGNOSED. Original premise FALSIFIED.

Step 1 was executed against the installed Windows build: 17 submap crossings walked in a
co-op host session, with the in-tree `[shift-probe][invalidate-bt]`, `[shift][perf]` and
`[build_cache][perf]` probes collected from `config/debug.log`.

**The spike is real and reproduces at the predicted rate — but it is NOT an all-z
structural rebuild, and `invalidate_map_cache` is not involved.**

Measured over 17 shifts (`game::update_map`, `game_misc.cpp:1679`):

| component | median ms | max ms |
|-----------|-----------|--------|
| `shift` (`map::shift`) | 2.14 | **13.33** |
| `cache` (`build_map_cache`) | 5.99 | 7.10 |
| `loader` | 1.19 | 1.50 |
| `npc` / `spawn` / `om_seen` | ≤0.16 | ≤0.35 |

- Totals: min 8.3, median 10.0, max 22.1 ms. **2/17 shifts >16 ms** — matches the ~2/10 rate.
- **`invalidate_map_cache` fired 0 times across all 17 shifts** (probe verified live:
  `BACKTRACE` is defined in `out/build/win-rel-deb`, and the probe logs at `DL::Info`/
  `DC::Main`, so absence is a real negative, not a compiled-out probe).
- `build_map_cache` peaked at 7.08 ms with `outside=1.35`, `trans=1.64` — an order of
  magnitude below the hypothesised `outside=11ms + trans=7ms`. The all-z structural
  blowup this plan was written to hunt **no longer occurs**; Tier 0a + 1a removed it.
- All the variance lives in `map::shift` itself (2.1 → 13.3 ms, a 6x swing).

### Actual root cause: synchronous mapgen inside `map::shift`

`map::loadn` (called from the `shift_grid_slots` block) emits
`"map::loadn: Missing mapbuffer data. Regenerating."` when the incoming edge submaps have
never been generated, and generates them **inline on the main thread**. Correlating those
events against the spiking shifts:

| shift | mapgen events within 1.5 s |
|-------|----------------------------|
| 22.1 ms | 74 |
| 19.3 ms | 139 |
| 10.5 ms (typical) | **0** |

213 regeneration events occurred across the walk. The note in the old Context that
"Loader ≈1ms rules out mapgen" was wrong: `loader=` measures the async `submap_loader`,
not the inline `loadn` regeneration inside `map::shift`.

### Fix direction (not implemented)

This is **not** missing infrastructure. Async worker-thread mapgen already exists
(`map::generate()` on pool workers, Lua hooks deferred via `src/mapgen_async.h`:
`push_deferred_mapgen_hook` / `run_deferred_mapgen_hooks`), and the lazy-border
prefetcher was **enabled during the measured run** — `LAZY_BORDER` is `true` in that
session's `config/options.json` (the `lazy_border_enabled = false` in
`cached_options.cpp:55` is only the pre-option initial value, overwritten at
`options.cpp:2057`). The existing prefetch simply **loses the race**.

The mechanism is an ordering problem in `game::update_map` (`game_misc.cpp`):

```
1580  m.shift( this_shift );                            // loadn() pulls the new edge in
                                                        //   -> regenerates inline if missing
1592  submap_loader.update_request( reality_bubble_handle_, new_center );
1601  submap_loader.update_request( lazy_border_handle_, new_center );
1611  submap_loader.update_lazy_border_focus( ..., u.abs_pos() );
1612  submap_loader.update();                           // prefetcher only NOW learns the
                                                        //   new centre/direction
```

`map::shift` consumes the leading edge before the prefetcher is told where the player
moved, so the loader is structurally one crossing behind at the edge that matters.
Candidate fixes, cheapest first:

1. Move the `update_request` / `update_lazy_border_focus` calls **before** `m.shift()` so
   the prefetcher is aimed at the new centre while the shift is still in progress.
2. Widen the lazy-border lookahead in the direction of travel
   (`lazy_omt_preload_direction_`), so generation completes before the edge is consumed.
3. Note this session ran `REALITY_BUBBLE_SIZE=6` → 15x15 submaps (180x180 tiles), not the
   legacy 11x11 the roadmap's architecture section assumes. A larger bubble means more
   edge submaps consumed per crossing and correspondingly more prefetch pressure, so
   re-baseline any fix at both sizes.

Cost/benefit: 2/17 crossings at ~20 ms is roughly one dropped frame at 60 fps, so this is
polish, not a blocker. It was deliberately NOT fixed in the 2026-08-02 pass because
reordering worldgen scheduling carries far more regression risk than the measured gain.

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
