# [DONE] Parallelise `build_map_cache` — Corrected Analysis

**Verdict (2026-07-10): SKIPPED — negligible player-visible win.**

This is a runtime optimization (map shift redraw), not startup. In the common case (single-z mode, player walking), only 1 z-level rebuilds — parallelizing 1 iteration does nothing. Even in zlevels mode (full 21-z rebuild), the saving is ~14ms on a map shift. Invisible to the player.

The startup work (parallel file reads, mapgen parallel, LTO, PGO, pre-warm, JSON packs) already reduced `total_wall_ms` from ~4.7s to ~2.7s and hides most of that behind the title screen. A 14ms map shift improvement is not worth the 3 days of work.

The code below is correct and low-risk if revisited later — the analysis is preserved for reference.

## STATUS (rewritten 2026-07-10)

**Previous plan was PARTLY STALE.** Here's what the code actually does (verified against `src/map.cpp` and `src/lightmap.cpp`):

| Phase | z-loop | intra-z parallel? | Actual Code |
|-------|--------|-------------------|-------------|
| 1a floor | **Serial** z-loop | ❌ **NO** — serial `bubble_submaps()` loop | `map.cpp:9455-9463` |
| 1b outside/sheltered | **Serial** z-loop (top-down dep) | ✅ YES — `parallel_for(0, my_MAPSIZE, process_smx)` | `map.cpp:9121-9127` |
| 1c transparency | **Serial** z-loop | ✅ YES — `parallel_for(0, my_MAPSIZE, process_smx)` | `lightmap.cpp:232-238` |
| 1d parallel caches | ✅ **Parallel** z-loop | N/A (just cache clearing) | `map.cpp:9494-9514` |
| 2 suspension | Serial (shared `support_cache_dirty`) | N/A | `map.cpp:9543-9545` |
| 3 vehicles | Serial (neighbor z writes) | N/A | `map.cpp:9554-9558` |
| 4 lightmap | ✅ **Parallel** over dirty levels | ✅ per-submap parallel | `map.cpp:9631-9633` |

**Key correction**: `build_floor_cache(z)` does **NOT** have intra-z parallelism. It iterates `bubble_submaps()` serially (`map.cpp:9211`). The old plan assumed it did — it doesn't.

**What's already parallel**: 1b and 1c already use intra-z `parallel_for` over smx columns. Phase 1d uses cross-z `parallel_for`. Phase 4 uses both.

---

## Dependency Graph

```
floor[z]          ← z-independent (no cross-z reads)
outside[z]        ← reads floor[z+1], outside[z+1] (top-down cascade)
transparency[z]   ← reads outside[z] (must wait for outside complete)
suspension[z]     ← writes shared support_cache_dirty (serial)
vehicles[z]       ← writes neighbor z floor_cache (serial)
lightmap[z]       ← reads transparency[z] (parallel over dirty levels)
```

---

## Approach

### Phase A — Add intra-z parallelism to `build_floor_cache` (LOWEST RISK)

**Why**: Floor is the only Phase 1 function missing intra-z parallelism. It's z-independent (no cross-z reads), making it also the best candidate for cross-z later.

**Current code** (`map.cpp:9210-9240`):
```cpp
// Delegate to per-submap rebuild, then copy into the flat render cache.
for( const auto p : bubble_submaps() ) {
    if( !rebuild_all && !ch.floor_cache_dirty.test(...) ) {
        continue;
    }
    // ... rebuild_floor_cache + copy to flat cache
}
```

**Change**: Extract `process_smx` lambda (same pattern as outside/transparency), gate behind `parallel_enabled && parallel_map_cache && !is_pool_worker_thread()`:

```cpp
const auto process_smx = [&]( int smx ) {
    for( int smy = 0; smy < my_MAPSIZE; ++smy ) {
        const auto p = point_bub_sm( smx, smy );
        if( !rebuild_all && !ch.floor_cache_dirty.test(
                static_cast<size_t>( ch.bidx( smx, smy ) ) ) ) {
            continue;
        }
        const auto sm_pos = tripoint_bub_sm( p, zlev );
        submap *cur_submap = get_submap_at_grid( sm_pos );
        if( cur_submap == nullptr ) {
            continue;
        }
        cur_submap->rebuild_floor_cache( *this, sm_pos );
        const auto ms_pos = project_to<coords::ms>( p );
        if( !rebuild_all ) {
            for( int sx = 0; sx < SEEX; ++sx ) {
                std::fill_n( floor_cache.data() + ch.idx( ms_pos.x() + sx, ms_pos.y() ),
                             SEEY, '\x01' );
            }
        }
        for( const auto sm_ms : submap_tiles() ) {
            if( !cur_submap->floor_cache[sm_ms.x()][sm_ms.y()] ) {
                floor_cache[ch.idx( ms_pos.x() + sm_ms.x(), ms_pos.y() + sm_ms.y() )] = false;
            }
        }
    }
};

if( parallel_enabled && parallel_map_cache && !is_pool_worker_thread() ) {
    parallel_for( 0, my_MAPSIZE, process_smx );
} else {
    for( int smx = 0; smx < my_MAPSIZE; ++smx ) {
        process_smx( smx );
    }
}
```

**Safety**: Each smx column writes to unique flat-cache positions (same isolation guarantee as outside/transparency). No cross-smx reads. The `rebuild_all` bulk init (`std::fill`) runs before the parallel block — already done at `map.cpp:9207`.

**Effort**: ~1 day. Same pattern as 1b/1c, just mechanical extraction.

---

### Phase B — Cross-z parallelism for floor (z-independent)

**Why**: Floor is z-independent. After Phase A adds intra-z parallelism, we have a choice:
- **Option 1**: Keep serial z-loop + intra-z parallel (current pattern for 1b/1c). Safe, no nested parallelism.
- **Option 2**: Cross-z `parallel_for` **without** intra-z parallel inside. Each worker calls `build_floor_cache(z)` serially over its submaps.

**Option 2 is the right call** because:
- 21 z-levels × 1 worker each = 21 tasks distributed across the pool
- No nested parallelism (worker calls serial submap loop)
- Each z writes only to `get_cache(z).floor_cache` — heap-allocated `level_cache`, low false-sharing risk

**Implementation**:

```cpp
{
    ZoneScopedN( "Phase1_floor" );
    const bool affects_seen = zlevels; // all z may affect seen in zlevels mode
    if( parallel_enabled && parallel_map_cache ) {
        std::atomic<bool> floor_seen_dirty{ false };
        parallel_for( minz, maxz + 1, [&]( int z ) {
            const _bc::time_point _zt = _bc::now();
            const bool _dirty = build_floor_cache( z );
            _zadd( z, _zt );
            if( _dirty && ( z == zlev || fov_3d ) ) {
                floor_seen_dirty.store( true, std::memory_order_relaxed );
            }
        } );
        seen_cache_dirty |= floor_seen_dirty.load();
    } else {
        for( int z = minz; z <= maxz; ++z ) {
            const bool affects_seen_cache = z == zlev || fov_3d;
            const _bc::time_point _zt = _bc::now();
            const bool _floor_dirty = build_floor_cache( z );
            _zadd( z, _zt );
            if( _floor_dirty && affects_seen_cache ) {
                seen_cache_dirty = true;
            }
        }
    }
}
```

**BUT**: This creates nested parallelism when `build_floor_cache(z)` is called from a worker AND `parallel_map_cache` is enabled, because Phase A added `parallel_for` inside `build_floor_cache`. **Solution**: Pass a flag or check `is_pool_worker_thread()` inside `build_floor_cache` to skip intra-z parallel when already on a worker.

`build_outside_cache` and `build_transparency_cache` already guard with `!is_pool_worker_thread()` — floor should do the same.

**Effort**: ~1 day after Phase A.

---

### Phase C — Cross-z parallelism for transparency (z-independent reads)

**Why**: Transparency reads `outside[z]` but NOT `transparency[z+1]`. Once outside is complete, all transparency builds are z-independent.

**Current**: Serial z-loop at `map.cpp:9483-9487`.

**Change**: Same pattern as Phase B — `parallel_for(minz, maxz+1)` after outside completes. Each worker calls `build_transparency_cache(z)` which already has `!is_pool_worker_thread()` guard on its intra-z parallel.

**Safety**: `update_weather_transparency_lookup()` must complete before the parallel block (already done at `map.cpp:9442`). The `weather_lookup_` is read-only during transparency builds.

**Effort**: ~1 day.

---

### Phase D — Wavefront for outside (top-down dependency)

**Why**: Outside reads `floor[z+1]` and `outside[z+1]`. This is a strict top-down cascade: z can start only when z+1 is done.

**Assessment**: With 21 z-levels and each z taking ~X ms, the total is 21×X. Wavefront parallelism (submit z as task dependent on z+1) gives no speedup over serial — it's the same critical path. The only benefit is overlapping thread scheduling overhead, which is negligible.

**Decision**: **SKIP**. The top-down dependency makes outside inherently serial. The intra-z parallelism (already present) is the right lever.

---

### Phase E — Leave 2 and 3 serial

**Phase 2 (suspension)**: Writes to shared `support_cache_dirty` set. Would need mutex or per-thread collection + merge. Its cost is small relative to 1a-1c.

**Phase 3 (vehicles)**: Writes to neighbor z's `floor_cache`. Inherently cross-z.

**Decision**: **SKIP** unless profiling shows they're significant after A/C.

---

## Implementation Order

```
Step  What                                      Effort    Risk    Status
────────────────────────────────────────────────────────────────────
  A   Intra-z parallel for floor (smx loop)    1 day     low     ⬜ TODO
  B   Cross-z parallel for floor               1 day     low     ⬜ TODO (after A)
  C   Cross-z parallel for transparency        1 day     low     ⬜ TODO (after B)
  D   Wavefront for outside                   skip       —       ❌ SKIPPED
  E   Parallel suspension/vehicles            skip       —       ❌ SKIPPED
```

**Total effort**: ~3 days for A+B+C.

---

## Verification

1. **Tracy before/after**: The per-phase zones (`Phase1_floor`, `Phase1_transparency`) should show wall-clock reduction proportional to thread count.
2. **Determinism**: Run the same seed twice with different thread counts. Caches are not save-serialised — verify visually: no lighting differences.
3. **No regression with `parallel_map_cache=0`**: The `else` branch preserves the existing serial path.
4. **`--check-mods`**: Run 50+ times without error variation (established pattern from parallel loading work).

---

## Risk Assessment

- **Nested parallelism**: The primary risk. Mitigated by `!is_pool_worker_thread()` guard — already proven in 1b/1c. Floor needs the same guard added in Phase A.
- **Thread launch overhead for 21 z-levels**: `parallel_for` handles chunking internally. The existing 1d parallel caches use the same pattern successfully.
- **False sharing**: Each `level_cache` is heap-allocated via `unique_ptr` — heap allocator spreads them. Low risk (assessed in original plan, still valid).
- **`seen_cache_dirty` atomic**: Floor's `_floor_dirty` return value feeds `seen_cache_dirty`. Use `std::atomic<bool>` for the parallel path — same pattern as the dirty mutex in Phase 1d (`map.cpp:9495`).
