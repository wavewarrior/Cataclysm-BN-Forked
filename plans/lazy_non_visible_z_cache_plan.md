# Tier 3b — Lazy non-visible-z cache allocation

## STATUS (reviewed 2026-06-27)
0% implemented — `caches` is still the dense `std::array<unique_ptr<level_cache>,
OVERMAP_LAYERS>` (map.h:2341); get_cache() unconditionally derefs caches[z+OVERMAP_DEPTH]
(2395/2415), no NULL/sparse path. `calc_max_populated_zlev` exists (map.cpp:10950) but is NOT
used to skip the structural phase loops. Plan's own assessment is honest: RAM win is small,
real win is loop-iteration skip. OVERLAPS `amortise_non_player_z_rebuild_plan.md` — both
attack non-player-z loop cost. Phase B here (skip empty z via populated-range) is a strictly
SIMPLER subset of 1b's amortisation and lower-risk; consider doing Phase B only and dropping
1b, or merging. KEEP but flag overlap.

## Context

`level_cache` per z (`map.h:308-436`) is a `std::array<unique_ptr<level_cache>,
OVERMAP_LAYERS>` of 21 entries. All 21 are **always allocated**, even for
z-levels that have almost no content (e.g., z+5 in a surface-only world).

`calc_max_populated_zlev` already exists and is used by `build_sunlight_cache`
to skip empty z-levels. But the level_cache structs themselves are full-sized:
each one contains flat `vector<char>` / `vector<float>` / etc. arrays sized to
`g_mapsize_x * g_mapsize_y` tiles. At default bubble size 4 (132×132 = 17424
tiles), each level_cache is ~1 MB (per tile estimate above). 21 × 1 MB = 21 MB
for the entire cache stack. At size 8 (228² = 51984 tiles), it's 21 × 3 MB =
63 MB.

The RAM is small relative to total game footprint, but the **all-z loops** in
`build_map_cache` iterate 21× even if only 4-5 z have content. The real savings
from lazy allocation is in loop iteration cost, not RAM.

## Approach

### Phase A — Sparse level_cache allocation

Replace the dense array with a sparse structure:

```cpp
// Before:
std::array<unique_ptr<level_cache>, OVERMAP_LAYERS> caches;

// After:
std::unordered_map<int, unique_ptr<level_cache>> caches;  // key = z offset
```

or keep the array but NULL-initialise empty entries:

```cpp
std::array<unique_ptr<level_cache>, OVERMAP_LAYERS> caches = {};
```

and add an accessor that returns a sentinel or default cache for unallocated z:

```cpp
auto get_cache( int z ) -> level_cache & {
    auto &ptr = caches[z + OVERMAP_HEIGHT];
    if( !ptr ) {
        ptr = make_unique<level_cache>();
    }
    return *ptr;
}
```

### Phase B — Shrink all-z loops

Once sparse allocation exists, the loops in `build_map_cache` can skip
unallocated z:

```cpp
for( int z = minz; z <= maxz; ++z ) {
    if( !caches[z + OVERMAP_HEIGHT] ) continue;  // no data = skip
    build_floor_cache( z );
}
```

`calc_max_populated_zlev` already provides the populated range; it's used in
`build_sunlight_cache` (`lightmap.cpp`). Extend its use to the structural
phases.

### Phase C — Allocation on first access

Allocate a z-level's cache lazily when a submap is first loaded for that z:
- In `map::loadn` or `mapbuffer::generate_omt`, if the target z is
  unallocated, allocate its `level_cache`.
- Deallocate when the last submap on that z is unloaded (when the bubble shifts
  away and no content remains).

## Verification

1. **RAM:** start a surface-only world. RSS should be lower than before (fewer
   allocated level_caches).
2. **Build time:** `[build_cache][perf]` should show fewer z iterated for
   phases 1a-1c (only populated z looped).
3. **Correctness:** move through z-levels (down stairs, up stairs, falling).
   Caches for newly-entered z allocate on first access and rebuild the same as
   before. No visual difference.
4. **Rolling world:** start at z=0 with 12 populated z. Walk shifts the bubble
   through all z. No cache for z=+9 is allocated until content arrives there.

## Risk

- The `std::array` access pattern is O(1) with known index. Switching to
  `unordered_map` or conditional-pointer-check adds branch overhead on the
  hot path (every tile lookup). **Mitigation:** keep the array but allow NULL
  entries; the branch is cheap and the loop skip saves far more.
- Some code assumes all caches exist (e.g., the solar cascade iterates all
  z unconditionally). Must audit every `get_cache(z)` call site to handle
  the unallocated case.
- Player viewpoint at an unpopulated z should immediately allocate, not
  silently produce defaults.
