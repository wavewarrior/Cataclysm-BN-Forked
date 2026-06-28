# ❌ ARCHIVED 2026-06-28 — DEAD / superseded by per_submap_incremental_lightmap (B1/B2). Root cause already removed. Moved to plans/done/

# Tier 0c — Pin the non-shift all-z lightmap =8ms spike

## STATUS (reviewed 2026-06-27)
LARGELY SUPERSEDED by `per_submap_incremental_lightmap_plan.md` (B1/B2). The "what dirties
all-z seen cache every turn" culprit this plan hunts = the turn-start + player-move blanket
`invalidate_lightmap_caches()`, both now REMOVED (game.cpp:1878 visibility-only; player-move
uses per-submap dirty). The 8ms all-z lightmap path this plan targets no longer fires on a
normal stationary/walking turn. Recommend ARCHIVE — fold any residual measurement into 1a's
verification. Only kept-relevant bit: the `[shift-probe][lightmap]` >1-level diagnostic still
flags unexpected all-z drivers if one regresses.

## Context

Some **non-shift turns** show lightmap Phase 4 taking ≈8ms across all z-levels.
On a normal turn (no submap crossing) the lightmap should regenerate only for
the player's z-level (~1.5ms). When `dirty_seen_cache_levels` contains all z,
`generate_lightmap` runs for every level — that's the 8ms.

**The in-tree probe already exists:** `[shift-probe][lightmap]` logs when
`dirty_seen_cache_levels.size() > 1`, listing which levels regenerated. It's
at `map.cpp:10038-10040`.

## Diagnosis steps

### Step 1 — Collect data (one session)

1. Build with default options (probes are runtime-gated, no special build needed).
2. Run and walk a few turns. Stand still for 20 turns to isolate non-shift lightmap cost.
3. Search `debug.log` for `[shift-probe][lightmap]` and `[build_cache][perf]`:
   ```sh
   rg '\[shift-probe\]\[lightmap\]' ~/Library/Application\ Support/Cataclysm-BN/config/debug.log
   rg '\[build_cache\]\[perf\]' ~/Library/Application\ Support/Cataclysm-BN/config/debug.log
   ```
4. Count non-shift entries with `lightmap=…` > 3ms (baseline is player-z only, ~1.5ms).

### Step 2 — Identify what dirties all-z seen cache

`dirty_seen_cache_levels` is populated in Phase 1d (`map.cpp:9944-9949`) from
`ch.seen_cache_dirty`. `seen_cache_dirty` is set...

- In `map::shift` — only for player-z (and z±1 if `fov_3d`) after the fix
- In `invalidate_map_cache` — sets `ch.seen_cache_dirty = true` for the passed z
- In `map::set_seen_cache_dirty` — called from various places

Find the caller that sets `seen_cache_dirty` for non-player z on a stationary turn.

**Quick audit:** grep `set_seen_cache_dirty`:
```sh
rg 'set_seen_cache_dirty' src/
```

### Step 3 — Fix

If the culprit is a periodic system that unnecessarily dirties all z:
- Gate to player z ± active-relevant range.
- Or hoist to only run on actual state change, not every turn.

If it's the lightmap itself (e.g., `build_sunlight_cache` cascading and marking
every z dirty as a side effect), the fix belongs in Tier 1a (per-submap
incremental lightmap) — document it there and accept the 8ms for now.

## Verification

- Before: `lightmap=7-9ms` on stationary turns in debug log.
- After: `lightmap=1-2ms` (player z only) on stationary turns.
