# Tier 2a — Parallelise `build_map_cache` across z on the worker pool

## STATUS (reviewed 2026-06-27)
~30% done, and the plan's framing is now PARTLY STALE. Reality in code (map.cpp:9893-9978):
- Phase 1a floor, 1b outside, 1c transparency are still SERIAL z-loops AS THIS PLAN ASSUMES —
  BUT build_floor_cache/build_outside_cache already use intra-z `parallel_for` over smx
  (map.cpp:9540-9564), so each z is internally parallel. The proposed Phase A/B (parallel_for
  OVER z for 1a/1c) is NOT done and would conflict with the existing intra-z parallel_for
  (nested pools) — re-think before implementing.
- Phase 1d is already parallel-over-z via `parallel_for(minz,maxz+1,...)` gated on
  `parallel_map_cache`/`parallel_enabled` (9936-9956) — plan's "already parallel" note holds.
- Phase 4 lightmap already per-submap parallel (parallel_for over smx, lightmap.cpp:233/878).
- Phases 2/3 correctly still serial (matches Phase D).
Net: the cross-z parallelism this plan proposes is unbuilt, but the bigger lever (intra-z) is
already pulled. KEEP but rewrite the analysis to account for nested-parallelism risk before
acting. Still correctly gated AFTER 1a/1b.

## Context

`build_map_cache` (`map.cpp:9848`) already has per-z parallelism for Phase 1d
(vehicle clear + dirty level collection) and for Phase 4 lightmap
(`generate_lightmap_worker` via `parallel_for`). But Phases 1a (floor), 1b
(outside), 1c (transparency), 2 (suspension), and 3 (vehicles) are still serial
z-loops.

**This plan goes AFTER Tier 1a and 1b.** Until the per-submap incremental
lightmap (1a) and amortised non-player-z rebuild (1b) are in place, the z-loops
are the dominant cost and confusing them with parallelism adds risk for limited
gain. The Factorio lesson: naive per-chunk multithreading ran slower due to
cache-line contention / false sharing.

## Preconditions

- Per-submap incremental lightmap (Tier 1a) is committed.
- Amortised non-player-z rebuild (Tier 1b) is committed — or at minimum the
  per-z memory locality is verified.

## Analysis

### Phases and their independence

| Phase | Z-dependence | Parallelisable? |
|-------|-------------|-----------------|
| 1a floor | None | **Yes** — embarrassingly parallel |
| 1b outside | Reads floor[z+1] | **Wavefront** — top-down cascade serial per z-group |
| 1c transparency | Reads outside_cache | **Yes** — after 1b done |
| 1d parallel-caches | None | Already parallel (`parallel_for`) |
| 2 suspension | Writes shared `support_cache_dirty` set | **No** — must stay serial |
| 3 vehicles | Writes neighbour z floor_cache | **No** — must stay serial |
| seen | Player-z only | Already narrow scope |
| 4 lightmap | `build_sunlight_cache` cascade serial | Lightmap already parallel per dirty level |

### Key constraint: `parallel_for` currently wraps the z-loop but each iteration
calls `build_*_cache(z)` which writes to `get_cache(z)`. Per Factorio, if
adjacent z-levels' `level_cache` share cache lines, threads writing nearby
memory will contend. Verify per-z memory locality before enabling.

**Check: `level_cache` layout**

```cpp
std::array<unique_ptr<level_cache>, OVERMAP_LAYERS> caches;  // map.h:~308
```

Each `level_cache` is heap-allocated (`unique_ptr`). Adjacent z-levels' caches
are **dynamically allocated and likely not adjacent in memory** — heap allocator
spreads them. This means false sharing risk is LOW for cross-z parallelism
(unlike Factorio's contiguous per-chunk arrays), because each z writes to its
own heap allocation. **Exception:** the `std::array` of `unique_ptr` pointers
is contiguous and small, but writes to the pointed-to data — not the pointers
themselves.

### Assessment: low false-sharing risk for z-parallelism

The per-`level_cache` heap allocations act as natural cache-line isolators.
Parallelising 1a and 1c across z should produce real speedup without Factorio's
false-sharing pitfall. 1b remains a top-down dependency chain; it can use a
wavefront approach (z=maxz on thread A, z=maxz-1 on thread B with dependency
wait, etc.).

## Approach

### Phase A — Parallelise Phase 1a (floor)

Current:
```cpp
for( int z = minz; z <= maxz; ++z ) {
    build_floor_cache( z );
}
```

Replace with:
```cpp
if( parallel_enabled && parallel_map_cache && rebuild_z_count > threshold ) {
    parallel_for( minz, maxz + 1, [&]( int z ) {
        build_floor_cache( z );
    } );
} else {
    // existing serial loop
}
```

No data race: `build_floor_cache(z)` writes only to `get_cache(z).floor_cache`
and checks `floor_cache_dirty` bitset which is also per-z.

### Phase B — Parallelise Phase 1c (transparency)

Same pattern as 1a. Must wait for 1b (outside) to complete, but that's already
the serial flow — 1b is a separate block. If 1b is parallelised with wavefront,
the dependency is 1b-complete → 1c-parallel.

### Phase C — Wavefront for Phase 1b (outside)

`build_outside_cache(z)` reads `floor_cache[z+1]` and `outside_cache[z+1]`.
This is a top-down data dependency: z can start when z+1 is done.

With N threads:
- z=maxz starts immediately (no z+1 dependency — top of stack).
- z=maxz-1 starts when z=maxz completes.
- z=maxz-2 starts when z=maxz-1 completes.

Implementation options:
1. **Sequential pool tasks** — submit each z as a task with the previous as
   dependency.
2. **Simple partition** — N contiguous z-ranges per thread; only the boundary
   between ranges serialises. Good enough for N=4-8 threads and 21 z-levels.

### Phase D — Leave 2 and 3 serial

`update_suspension_cache` calls `support_dirty()` which writes to a shared set.
`do_vehicle_caching` writes to neighbour z's `floor_cache`. Both are inherently
serial. Their cost is small relative to 1a-1c.

## Verification

1. **Tracy before/after** — the per-phase zones (`Phase1_floor`, etc.) should
   show wall-clock reduction proportional to thread count.
2. **Determinism** — run the same seed twice with different thread counts.
   Caches are not save-serialised, so results should match. Verify visually:
   no lighting differences between runs.
3. **No regression with `parallel_map_cache=0`** — the `else` branch preserves
   the existing serial path. Toggle the option, confirm identical visuals.

## Risk

- Thread launch overhead for 21 z-levels: `parallel_for` has a minimum chunk
  size. If `maxz-minz` is small, serial is faster. Gate on threshold.
- 1b wavefront complexity may not be worth it if the serial 1b is already
  fast after Tier 1b (amortisation). Measure before implementing wavefront.
