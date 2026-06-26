# Kill the walking hitch — incremental cache rebuild on reality-bubble shift  (✅ Done: `5315065c12`)

## Context

Walking produces a periodic hitch + lighting "reset" every ~12 tiles. Investigation
(measured, not assumed) traced it end-to-end:

- Render is **not** the cause: `render_body` ≤ ~2ms, GPU JFA/lighting ≤0.6ms. The
  hitch is sim-side — `frame_period` spikes to 40–55ms while `render_body` stays ~2ms.
- The stall is `game::update_map` → `map::build_map_cache(get_levz())`, fired on every
  submap crossing. Measured breakdown (per shift, ~18–20ms total):
  - **`build_outside_cache` ≈ 7.5ms** — rebuilds **all ~21 z-levels, every submap**.
  - **Phase4 lightmap ≈ 7–10ms** — `generate_lightmap` for **all z-levels**.
  - transparency ≈1.5ms, seen ≈1.2ms, floor ≈0.4ms (these are already cheap).
  - z-split proof: non-player-z work = 8.9ms vs player-z = 0.5ms.

Root cause is **`map::shift` over-invalidating all z-levels**. It already does the right
thing for `transparency_cache`/`floor_cache` — `shift_flat_cache()` translates the data
and `shift_bitset_cache()` marks only new-edge submaps dirty, so `build_*` rebuilds just
the edge. But:
- `outside_cache`/`sheltered_cache` are **not** translated and get blanket-dirtied
  (`set_outside_cache_dirty(gridz)`, map.cpp:8272) → full all-z rebuild.
- `set_seen_cache_dirty(gridz)` for **all z** (map.cpp:8273) makes `dirty_seen_cache_levels`
  = all z → lightmap regenerates every level (on a normal turn it's just player-z, ~1.5ms).

Outcome: rebuild only what actually changed on a horizontal shift (translate + edge),
matching the existing transparency/floor pattern. Target: shift cost <16.6ms (no dropped
frame). The original "lazy non-player-z rebuild" idea is **rejected** — exploration showed
falling, the z-stack render loop, 3D vision, and the solar cascade all read non-player-z
caches assuming freshness (HIGH risk). The translate-and-edge approach has none of that
risk because the data stays correct.

## Phase 1 — `outside_cache`/`sheltered_cache` incremental shift (✅ Done: `5315065c12`)

`shift_bitset_cache( gc.outside_cache_dirty, ... )` + `shift_flat_cache` for
`outside_cache`/`sheltered_cache` added in `map::shift()` at lines 8221/8227/8228.
`set_outside_cache_dirty(gridz)` removed. `[build_cache][perf]` confirms `outside=` is
now ~0.002ms on shift (was ~7.5ms). Hitch eliminated.

## Phase 2 — lightmap (✅ Done: `5315065c12`)

`shift_flat_cache` for `lm`/`sm`/`angled_sunlight_cache` added at lines 8238/8239/8241.
`set_seen_cache_dirty(gridz)` restricted to `player_z` and `player_z ± 1` under `fov_3d`
(lines 8303–8306). Verified in-game: lower-z lighting correct through holes/ledges.

## Cleanup (🔄 Deferred — probes still useful for ongoing perf work)

The diagnostic probes remain in place for now since they're still useful for monitoring
ongoing B2 and Tier-1 perf work. Probes to remove when stable:
- `src/sdl_render_frame.cpp` — per-phase `g_phase_*` timers + `[render][perf][phase ...]` log.
- `src/game.cpp` — `update_map` `_sh_*` timers + `[shift][perf]` log.
- `src/map.cpp` — `build_map_cache` `_ph_*`/`_z_*` timers + `[build_cache][perf]` log.

## Notes
- Commit Phase 1 separately from Phase 2 and from the probe cleanup.
- Stage explicit file lists (repo has parallel uncommitted work) — never `git add -A`.
