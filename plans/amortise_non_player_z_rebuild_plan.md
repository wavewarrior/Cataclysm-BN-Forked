# Tier 1b — Amortise non-player-z structural rebuild across frames

## Context

Phases 1a–1c of `build_map_cache` (floor, outside/sheltered, transparency) run
for **all z-levels every time**, even though only the player's z is immediately
needed for rendering. These are the structural caches — they cost ~7.5ms
(outside) + ~1.5ms (transparency) for non-player-z work.

The player's z is consumed immediately by shadowcast FOV (Phase: seen) and by
the GPU render pipeline. Deeper z (z-3, z+4) may not be read for many frames —
they're only needed when the player looks down a hole, or when 3D FOV needs them.

## Root cause

The z-loop in `build_map_cache` (`map.cpp:9887-9925`) runs `minz..maxz`
unconditionally. There's no "active z" set — every level is rebuilt every time,
regardless of whether the player can currently observe it.

The earlier "lazy rebuild" exploration was rejected because falling, the z-stack
render loop, 3D vision, and the solar cascade all read non-player-z caches
assuming freshness. **This plan avoids that trap** by time-slicing rather than
deferring: every z still gets rebuilt, but not all in the same frame.

## Approach

### Step 1 — Define rebuild budget per frame

In `build_map_cache`, after the player-z is rebuilt, spread non-player-z work
across subsequent frames:

```cpp
// Pseudocode — insert after player-z structural work is done.
const int total_z = maxz - minz + 1;
const int budget = 4;  // rebuild at most 4 non-player-z per frame
int rebuilt = 0;
static int next_z = minz;

for( int z = next_z; z <= maxz && rebuilt < budget; ++z ) {
    if( z == zlev ) continue;  // already done
    build_floor_cache( z );
    build_outside_cache( z );
    build_transparency_cache( z );
    ++rebuilt;
    next_z = z + 1;
}
if( next_z > maxz ) next_z = minz;  // wrap around
```

This is the **open-world streaming canon** pattern: amortise integration across
frames (Meta asset streaming, UE4 streaming/GC).

### Step 2 — Handle the look-down-a-hole case

When the player looks down (z-1 is visible on screen), that z's structural
caches must be up-to-date BEFORE the render pass reads them.

Add an **immediate-rebuild set**: z-levels that are currently on-screen or
reachable via 3D FOV. If a non-player-z enters this set, the amortisation skips
it and rebuilds immediately (same frame).

Detection:
- The z-stack render loop (`cata_tiles.cpp:~3499`) already iterates visible z.
- Pass the set of visible/needed z to `build_map_cache` (or check post-phase).

### Step 3 — Tune budget

The budget (4) is a starting guess. At 16.6ms/frame and ~3ms per non-player-z
(floor+outside+transparency), budget=4 = 12ms of structural work + player-z
<2ms = 14ms. That stays under budget. Tune based on actual measurements.

## Verification

1. **Before:** measure `other=` ms in `[build_cache][perf]` for a walking session.
2. **After:** non-player-z cost should drop from ~9ms to ~3ms (1-2 z/frame).
3. **Quality:**
   - Stand at a ledge looking down one z: lower-z lighting is correct (immediate
     rebuild set catches it).
   - Stand normally: z-1/z-2 structural caches may be 1-2 frames stale — confirm
     no visual flicker when looking down suddenly.
   - Fall through z-levels: the player's target z is rebuilt immediately because
     it becomes `zlev` in the next `build_map_cache` call.

## Risk

- The solar cascade (`build_sunlight_cache` in Phase 4) reads structural caches
  from z+1 downward. If z+1's structural data is stale (amortised out), the
  sunlight on the player's z will be computed from stale roof/outside data.
  **Mitigation:** sunlight cascade must either force-immediate its required z,
  or the amortisation must skip levels that the cascade will read in the current
  frame's Phase 4.

- The z-stack render loop reads non-player-z `outside_cache`, `floor_cache`,
  etc. If these are stale by 1-2 frames, tiles drawn for the wrong z might
  flash briefly. **Mitigation:** the immediate-rebuild set (Step 2) includes all
  z the render loop currently draws.
