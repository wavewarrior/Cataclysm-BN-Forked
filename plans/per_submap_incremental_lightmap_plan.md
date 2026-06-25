# Tier 1a — Per-submap incremental lightmap (Phosphor/Starlight model)

## Context

Today the lightmap (`lm`, `sm`, `light_source_buffer` per `level_cache`) is
**whole-level** — if any tile changes, the entire lightmap for that z-level
regenerates. The structural caches it depends on (`transparency_cache`,
`outside_cache`, `floor_cache`) are already **per-submap incremental** with
dirty bitsets.

This is the biggest steady-state + hitch win available. It makes lighting
match the incremental pattern the structural caches already use.

## Root cause

The lightmap is rebuilt every time `seen_cache_dirty` or `lightmap_dirty` is
true for a level — which includes every submap crossing (after the walking-hitch
fix, this is player-z only, but still a full-level regen). For large bubbles
or dense scenes, even player-z-only lightmap cost is significant.

Industry precedent:
- **Minecraft Phosphor:** defer light updates until the region is queried,
  batch and dedupe scheduled updates.
- **Minecraft Starlight:** stateless — light tied to chunk, propagate only into
  chunks marked lit. Enables parallel per-region updates.

## Approach

### Phase A — per-submap dirty tracking for lightmap

Add per-submap dirty tracking to the lightmap, mirroring the pattern used by
`transparency_cache_dirty`:

1. **Add `lightmap_dirty` bitset** to `level_cache` (`src/map.h`), parallel to
   `transparency_cache_dirty`/`floor_cache_dirty`/`outside_cache_dirty`.

2. **In `map::shift`** — when shifting the bubble, translate the lightmap arrays
   (`lm`, `sm`, `light_source_buffer`) via `shift_flat_cache`, and shift the
   dirty bitset via `shift_bitset_cache`. Only new-edge submaps are marked dirty.

3. **In `build_map_cache` Phase 4** (`map.cpp:10011-10104`):
   - Replace the level-scope `lightmap_dirty` gate with per-submap iteration
     over only the dirty submaps.
   - `build_sunlight_cache` must still run top-down (z-cascade), but its
     *per-submap* cost is bounded by the dirty set.
   - `generate_lightmap` / `generate_lightmap_worker` already iterates submaps
     internally — add a dirty-submap skip.

4. **In `invalidate_map_cache`** — when invalidating a level, mark
   `lightmap_dirty` but do NOT `.set()` the per-submap bitset unless the whole
   level truly changed. Let the per-submap dirty bits mirror the structural
   cache invalidation scope.

### Phase B — deferred/regional regen

If Phase A alone is insufficient (e.g., the whole level still dirties from
structural cascade), add deferred regen:

1. When `build_map_cache` is called but only player-z lightmap is immediately
   needed, defer non-player-z lightmap regen to the next frame that actually
   reads those z (e.g., when the z-stack render loop visits them, or when FOV
   changes on that z).
2. Track a "pending regen" set of `(z, submap)` tuples. Process K per frame
   (time-sliced).

## Verification

1. **Before:** measure `lightmap=` ms in `[build_cache][perf]` for a walking
   session and a standing session.
2. **After:** same session — lightmap cost should scale with number of *dirty
   submaps*, not entire z-level.
3. **Quality:** stand at z=0, observe z-1 and z+1 lighting (hole, ledge, stairs).
   Lower-z lightmap should still be correct — if not, the deferred/regional
   regen needs the per-submap baseline first.
4. **Horde scene:** many emitters on the same z — lightmap cost should be
   proportional to submaps with changed emitters, not the whole z.

## Risk

- `build_sunlight_cache` is an all-z top-down cascade. It cannot be made
  per-submap without understanding the z+1 → z dependency. The cascade must
  still run for any submap whose z or z+1 above it is dirty.
- The `lm` array is read by GPU lighting (`frame_build.cpp`) for the full
  bubble. Partial regen must produce a consistent full-level `lm` when the GPU
  captures it.
- Deferred regen (Phase B) introduces state complexity: a deferred submap's
  light is stale until processed.
