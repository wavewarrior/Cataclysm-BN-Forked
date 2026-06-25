# Kill the walking hitch — incremental cache rebuild on reality-bubble shift

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

## Phase 1 — `outside_cache`/`sheltered_cache` incremental shift (safe, ~7.5ms)

Mirror the proven `transparency_cache`/`floor_cache` treatment onto `outside_cache`.

**File: `src/map.cpp`, `map::shift()` — `shift_cache_arrays` block (~lines 8213–8228).**
Add alongside the existing transparency/floor shifts:
```cpp
shift_bitset_cache( gc.outside_cache_dirty, gc.cache_mapsize, 1, sp );
shift_flat_cache( gc.outside_cache,   gc.cache_x, gc.cache_y, sp );
shift_flat_cache( gc.sheltered_cache, gc.cache_x, gc.cache_y, sp );
```
(`outside_cache`/`sheltered_cache` are `vector<char>`, same layout as `floor_cache` which
`shift_flat_cache` already handles; both are gated by the single `outside_cache_dirty`
bitset.)

**File: `src/map.cpp`, `map::shift()` — line 8272.** Remove `set_outside_cache_dirty( gridz );`.
The bitset shift now marks only new-edge submaps dirty, so `build_outside_cache`'s
`rebuild_all = outside_cache_dirty.all()` (map.cpp:9479) is false → per-submap test at
9486 skips retained (translated-correct) submaps and rebuilds only the edge.

**Why correct:** an interior tile's outside value = f(floor[z+1] in a 3×3 tile reach).
After a pure translate both the tile and its 3×3 source are the same world tiles, so the
translated value is exact. Only edge submaps (3×3 reaching past the translate window) need
rebuild — exactly what the shifted bitset marks. Top-down build order (maxz→minz) already
ensures z+1 is current first. This is the identical correctness model transparency/floor
already rely on.

**Verification (Phase 1):**
1. Build raw (rtk err masks build failures here): `rtk proxy cmake --build out/build/osx-arm-slim --target cataclysm-bn-tiles`; confirm `src/cataclysm-bn-tiles` mtime advanced.
2. Run `out/build/osx-arm-slim/src/cataclysm-bn-tiles --world "Clara City"`, walk across several submap boundaries, quit.
3. Grep `[build_cache][perf]` in the debug log — `outside=` should drop from ~7.5ms to ~1ms; shift `total` from ~18–20ms to ~11–14ms.
4. Eyeball while walking: sky-access / sun-shadow correctness unchanged, no shadow/lighting artifacts at the incoming screen edge as the bubble shifts. Indoors↔outdoors transitions correct.

If shift `total` is now <16.6ms and the hitch is gone, **stop here** and skip Phase 2.

## Phase 2 — lightmap (conditional, riskier, ~7–10ms) — only if Phase 1 leaves a hitch

The lightmap is whole-level (not submap-incremental), and `build_sunlight_cache` is an
inherently all-z top-down cascade, so it can't be made edge-incremental like outside.
The cost is driven by `set_seen_cache_dirty(gridz)` for all z (map.cpp:8273) →
`dirty_seen_cache_levels` = all z → `generate_lightmap` per level.

Approach (implement + verify carefully, do NOT do blind):
1. In `map::shift`, translate the lightmap arrays so non-player-z stays visually correct:
   `shift_flat_cache( gc.lm, ... )` and `shift_flat_cache( gc.sm, ... )` (element types
   `four_quadrants` / `float`).
2. Restrict the shift's seen-cache invalidation to player-z (and z±1 only when `fov_3d`):
   replace the unconditional `set_seen_cache_dirty(gridz)` so non-player-z lightmaps are
   not regenerated on a pure horizontal shift.
3. **Verification is mandatory and visual** — the risk is the z-stack-down render
   (`cata_tiles.cpp:~3499`) and 3D vision reading non-player-z visibility/lightmap:
   stand at a hole/ledge and look down a z-level while walking across a boundary; confirm
   lower-z lighting stays correct (translated, not stale/popping). Test with `FOV_3D` both
   off (default) and on. If lower-z lighting regresses, fall back to translating lm/sm only
   and keep the all-z regen (no saving) — i.e. abandon Phase 2.

## Cleanup (after the fix is verified)

Remove the three diagnostic probes added during investigation (all behind perf-log lines,
no behavior change):
- `src/sdl_render_frame.cpp` — per-phase `g_phase_*` timers + `[render][perf][phase ...]` log.
- `src/game.cpp` — `update_map` `_sh_*` timers + `[shift][perf]` log.
- `src/map.cpp` — `build_map_cache` `_ph_*`/`_z_*` timers + `[build_cache][perf]` log (keep
  until Phase 1 is measured-verified, then strip; also drop the `#include <chrono>` if newly added).

Keep the FPS overlay (`g_show_fps` / F-key toggle) — that was pre-existing work, not a probe.

## Notes
- Commit Phase 1 separately from Phase 2 and from the probe cleanup.
- Stage explicit file lists (repo has parallel uncommitted work) — never `git add -A`.
