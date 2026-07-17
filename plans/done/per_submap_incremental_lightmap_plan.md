# Tier 1a — Per-submap incremental lightmap (Phosphor/Starlight model)

## STATUS (reviewed 2026-06-27)
~95% done. Phase A (lightmap_dirty bitset on level_cache, shift translate, per-submap skip in
build_map_cache Phase 4) verified: map.h:350, map.cpp:8222/8470/8482/10024-10034,
lightmap.cpp:632/767. Phase B1 (turn-start invalidate removed → game.cpp:1878 now
visibility-only) + B2 (player-move invalidate → per-submap dirty) + B3 (solar-level skip,
last_built_light_level_int) ALL implemented (lightmap.cpp:659-668; map.h:2167). In-plan B3
"[PLANNED]" tags are STALE — B3 shipped. Only remaining: the "[PLANNED]" verification/horde
measurements. Plan is effectively DONE; keep as the lighting-incremental reference. This is
the lightmap half of the walking-hitch cluster — do NOT merge into 1b (different cache).

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

### Phase A — per-submap dirty tracking for lightmap [DONE]

Add per-submap dirty tracking to the lightmap, mirroring the pattern used by
`transparency_cache_dirty`:

1. **[DONE]** **Add `lightmap_dirty` bitset** to `level_cache` (`src/map.h`),
   parallel to `transparency_cache_dirty`/`floor_cache_dirty`/`outside_cache_dirty`.

2. **[DONE]** **In `map::shift`** — when shifting the bubble, translate the
   lightmap arrays (`lm`, `sm`, `light_source_buffer`) via `shift_flat_cache`,
   and shift the dirty bitset via `shift_bitset_cache`. Only new-edge submaps
   are marked dirty. Removed the blanket `invalidate_lightmap_caches()` from
   shift — retained submaps stay correctly clean.

3. **[DONE]** **In `build_map_cache` Phase 4**:
   - `.any()` / `.reset()` on the bitset instead of bool checks.
   - Per-submap zeroing of `sm`, `light_source_buffer`, `light_color_cache`
     (lm remains full-level fill — overwritten by `build_sunlight_cache`).
   - Dirty-submap skip in `generate_lightmap_worker` collection loop.
   - Diagnostic log: `[build_cache][perf] lightmap_dirty z=N dirty/total submaps`.

4. **[DONE]** **In `loadn`** — mark the loaded submap dirty in incremental mode;
   mark all bits dirty in non-incremental mode.

### Phase B — Stop blanket invalidate every turn (B1/B2/B3) [DONE]

**Context from debug.log:**

After Phase A, per-submap tracking is correct but the savings are drowned out:
- `invalidate_lightmap_caches()` at game.cpp:1878 (turn start) sets ALL bits
  for ALL 21 z-levels every turn.
- `invalidate_lightmap_caches()` at game.cpp:13128 (player move) also sets all
  bits for all levels.
- Result: z=0 shows 225/225 submaps on every non-shift frame → full rebuild.
- The per-submap skip only saves 14/225 submaps on shift frames (211/225 dirty).
- Non-player-z levels are already gated (only processed during shifts) — not
  the bottleneck.

**Changes:**

B1. **Remove turn-start `invalidate_lightmap_caches()`** (game.cpp:1878)
    - Standing still: lightmap stays clean after first frame, Phase 4 skips
      entirely → ~0ms lightmap cost.
    - Entity lights (character, NPC, monster) are currently applied *inside*
      the Phase 4 dirty-submap gate — if Phase 4 skips, they don't render.
      Fix: extract entity-light application to run *outside* the Phase 4 gate,
      unconditionally (they're cheap — a few `apply_light_source` calls).

B2. **Shrink player-move `invalidate_lightmap_caches()`** (game.cpp:13128)
    - Change from all-z-levels to single-z-level, or remove entirely.
    - After `map::shift`, `loadn` already marks the 14 new-edge submaps dirty.
    - Entity lights are re-applied by the always-run step (B1) → player light
      follows the avatar without a Phase 4 entry.
    - Result: post-shift rebuild processes only ~14 submaps instead of 225.

B3. ✅ DONE (verified lightmap.cpp:659-668, map.h:2167) — **Add sun-angle tracking to skip `build_sunlight_cache` when unchanged** [DONE]
    - `build_sunlight_cache` is the remaining bottleneck when Phase 4 does
      run (it writes `lm` for ALL tiles in the z-cascade).
    - Track `m_solar.last_built_light_level_int` — truncated `natural_light_level(0)`
      from the last cascade.  Set to -1 by `invalidate_map_cache` so structural
      changes (new walls, destroyed roofs) force a rebuild even when the sun
      hasn't moved.
    - If `last_built_light_level_int` matches current truncated light level AND
      is >= 0: skip the `lm` full-zero + cascade.  `lm` retains previous outdoor
      values; the worker adds artificial lights via `max()`.  Dawn/dusk tint
      still reads `lm` for sunlight values (same result as last build).
    - Saves the remaining ~1-2ms cascade cost on frames where only entity
      lights or per-submap artificial sources changed — the dominant
      steady-state case after B1/B2.
    - **Known edge case (ponytail: accepted):** phantom old-artificial-light in
      `lm` for dirty submaps where a light source moved away.  During daytime
      the artifact is invisible (sunlight dominates).  At night it's a
      single-frame glow at the pre-move position.  Self-corrects on next full
      rebuild (structural invalidate or meaningful sun change).
    - Implemented in both code paths: `generate_lightmap` (single-level) and
      the multi-level hoisted init in `build_map_cache`.

**Key insight:** Phase B isn't about deferring non-player-z (they're already
naturally deferred). It's about removing the blanket-turn and player-move
invalidates that defeat the per-submap tracking Phase A built.

**Risk:**
- Extracting entity lights from Phase 4 changes the ordering: they currently
  run after `build_sunlight_cache` but before `generate_lightmap_worker`.
  Must ensure the new ordering (entity lights → worker skip) doesn't create
  races or incorrect results.
- When B3 skips the cascade, `lm` retains pre-zeroing values from the last
  full rebuild.  `invalidate_map_cache` resets `last_built_light_level_int`
  to -1, forcing a rebuild on the next frame — so structural changes that
  affect the sunlight cascade (new roofs, destroyed walls) always trigger a
  full rebuild.  Non-structural Phase 4 entries (per-submap dirty from player
  movement) skip the cascade, saving ~1-2ms.

## Verification

1. **[DONE]** **Before:** measure `lightmap=` ms in `[build_cache][perf]` for a
   walking session and a standing session. (Log captured: standing ~1.5-8ms,
   shifting ~2-10ms across 3 levels, lightmap≈0 for second-pass renders.)
2. **[DONE]** **After Phase B (B1/B2):** standing session → lightmap=`~0ms`.
   Walking session → lightmap scales with dirty submaps (~14), not whole level.
3. **[PLANNED — code shipped, measurement pending]** **B3 measurement:** compare `[build_cache][perf]` total ms
   before/after B3 on a walking session. Expected: ~1-2ms reduction on each
   non-shift walk frame (the saved `build_sunlight_cache` cascade).
3. **[DONE]** **Quality:** stand at z=0, observe z-1/z+1 lighting. Correctness
   confirmed during playtest.
4. **[PLANNED]** **Horde scene:** many emitters on the same z — lightmap cost
   should be proportional to submaps with changed emitters, not the whole z.

## Risk

- `build_sunlight_cache` is an all-z top-down cascade. It cannot be made
  per-submap without understanding the z+1 → z dependency. The cascade must
  still run for any submap whose z or z+1 above it is dirty.
- The `lm` array is read by GPU lighting (`frame_build.cpp`) for the full
  bubble. Partial regen must produce a consistent full-level `lm` when the GPU
  captures it.
- Deferred regen (Phase B) introduces state complexity: a deferred submap's
  light is stale until processed.
