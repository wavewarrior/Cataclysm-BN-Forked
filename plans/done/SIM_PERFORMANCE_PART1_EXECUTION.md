# SIM_PERFORMANCE Part 1 — Monster AI Execution Plan

> **Source:** `plans/SIM_PERFORMANCE_PLAN.md` Part 1 (Monster AI, Tiers A+B+C)
> **Status:** ✅ **Done** — All three phases implemented, built, and verified.
> **Ground truth verified:** 2026-06-22 — all line numbers confirmed against current codebase.

## Shared context

The monster LOD system (`game::tier_assign_all` → budget → lifecycle → move loop) is well-structured
with Tracy zones, activity-skip caching, and parallel planning. Three known inefficiencies remain:

| # | Problem | Current code | Fix |
|---|---------|-------------|-----|
| 1 | z weighted equal to horizontal | `square_dist = max(dx,dy,dz)` → horde N floors down = "dist N" → Tier 0 | z-penalized `lod_dist` |
| 2 | Unbudgeted lifecycle | LOD-B ticks ALL monsters every turn, all z | Stride for off-z Tier-2 |
| 3 | O(M²) target scan | `compute_plan` scans ALL monsters for cub/friendly/guard | Spatial-hash buckets |

---

## Phase A — z-aware tiering (2–3 days)

### Root cause

`rl_dist()` at `src/line.h:184` uses `square_dist` (Chebyshev `max(dx,dy,dz)`). A horde 20 floors
below with `dx=5, dy=5` gets `dist = max(5,5,20) = 20` → Tier 0 full AI every turn. The same
over-weighting affects the budget/eligible sort and budget-cut boundary.

### Files to modify

| File | Change | Lines |
|------|--------|-------|
| `src/cached_options.h` | Add `extern int lod_z_penalty;` | +1 after line 117 |
| `src/cached_options.cpp` | Add `int lod_z_penalty = 16;` | +1 after line 39 |
| `src/options.cpp` | Register `LOD_Z_PENALTY` option + `setPrerequisite` + `cache_to_globals` | ~3 insertions |
| `src/game.cpp` | Add `lod_dist` helper; modify `tier_assign_all`; modify eligible-list building | ~30 lines |

### Step-by-step

#### Step A1 — cached_options.h (after line 117)

```cpp
/** Z-level distance penalty for monster LOD tier assignment. */
extern int lod_z_penalty;
```

#### Step A2 — cached_options.cpp (after line 39)

```cpp
int lod_z_penalty = 16;
```

#### Step A3 — options.cpp

**Inside** the `lod_monster` option group (after `LOD_COARSE_SCENT_INTERVAL`, ~line 2495):

```cpp
add( "LOD_Z_PENALTY", page_id,
     translate_marker( "Z-Level Distance Penalty" ),
     translate_marker( "Extra distance added per z-level when assigning monster AI tiers.  "
                       "Higher values push off-z monsters into coarser tiers faster.  "
                       "Monsters one floor away always keep full AI regardless of this value." ),
     0, 100, 16 );
```

**After** the group's `setPrerequisite` block (~line 2519):

```cpp
get_option( "LOD_Z_PENALTY" ).setPrerequisite( "MONSTER_LOD_ENABLED" );
```

**In** `cache_to_globals()` (~line 4192):

```cpp
lod_z_penalty = ::get_option<int>( "LOD_Z_PENALTY" );
```

#### Step A4 — game.cpp

##### A4a. Add `lod_dist` helper

Insert a file-local lambda (or inline helper) before `tier_assign_all`. The `|dz| < 2` guard is the
cross-z chase constraint: stair-followers one floor away keep full AI.

```cpp
/// z-penalized distance for LOD tier assignment.
/// Monsters >=2 z-levels away pay `lod_z_penalty` per extra z-level.
/// Monsters <2 z-levels away use raw 2D distance (stair-followers keep Tier 0).
static int lod_dist( const tripoint_bub_ms &a, const tripoint_bub_ms &b )
{
    const int dz = std::abs( a.z() - b.z() );
    if( dz < 2 ) {
        return rl_dist( a.xy(), b.xy() );
    }
    return rl_dist( a.xy(), b.xy() ) + lod_z_penalty * ( dz - 1 );
}
```

##### A4b. Modify `tier_assign_all()` (lines 5397-5404)

Replace:
```cpp
const int dist = rl_dist( mon.bub_pos(), player_pos );
if( dist <= tier01_dist || !mon.is_wandering() ) {
```
With:
```cpp
const int dist = lod_dist( mon.bub_pos(), player_pos );
const int abs_dz = std::abs( mon.bub_pos().z() - player_pos.z() );
if( dist <= tier01_dist || ( !mon.is_wandering() && abs_dz < 2 ) ) {
```

The `abs_dz < 2` in the goal-clause ensures a determined zombie one floor below still gets Tier 0
even if `lod_z_penalty` would push its numeric distance over `tier01_dist`. A zombie 20 floors down
with a goal is `abs_dz >= 2` → goal clause fails → uses numeric `dist` which includes the z-penalty
→ likely Tier 1 or 2.

##### A4c. Modify eligible-list distance (lines 6049 and 6087)

**Activity-skip path (line 6049):**
```cpp
// Replace:
eligible_order.emplace_back( rl_dist( critter->bub_pos(), player_pos ), critter );
// With:
eligible_order.emplace_back( lod_dist( critter->bub_pos(), player_pos ), critter );
```

**Normal path (line 6087):**
```cpp
// Replace:
eligible.emplace_back( rl_dist( critter.bub_pos(), player_pos ), &critter );
// With:
eligible.emplace_back( lod_dist( critter.bub_pos(), player_pos ), &critter );
```

### Verification

| # | Check | How |
|---|-------|-----|
| 1 | Build green | `cmake --build <preset> --target cataclysm-bn-tiles cata_test-tiles` |
| 2 | Off-z horde demoted | Load save with horde at dz=-5; `LOD_Z_PENALTY=16` → horde is Tier 2 (Tracy or debug log) |
| 3 | Stair-follower stays Tier 0 | Non-wandering zombie at dz=-1 stays Tier 0 regardless of z-penalty |
| 4 | Same-z identical | `lod_dist` with dz=0 == `rl_dist(a.xy, b.xy)` == `rl_dist(a,b)`. Existing `[monster]` tests pass |
| 5 | Budget still works | Budget-cut monsters are farthest via `lod_dist`, not `rl_dist` |
| 6 | Option round-trip | `LOD_Z_PENALTY=0` → same behavior as pre-patch (no z-penalty) |

---

## Phase B — stride the lifecycle floor (2–3 days)

### Root cause

The LOD-B lifecycle loop (`game.cpp:5963-6020`) runs `process_items()` / `process_turn()` /
`creature_in_field()` for **every** monster every turn regardless of tier or z-level. For off-z
Tier-2 hordes on field-free submaps, all three calls are wasted work — the monster is 2+ floors
away from the player and will just take a macro step.

### Files to modify

| File | Change | Lines |
|------|--------|-------|
| `src/cached_options.h` | Add `extern int lod_lifecycle_stride;` | +1 after `lod_z_penalty` |
| `src/cached_options.cpp` | Add `int lod_lifecycle_stride = 4;` | +1 after `lod_z_penalty` |
| `src/options.cpp` | Register `LOD_LIFECYCLE_STRIDE` option + `setPrerequisite` + `cache_to_globals` | ~3 insertions |
| `src/game.cpp` | Guard the lifecycle block with a stride check | ~15 lines |

### Step-by-step

#### Step B1 — cached_options.h

```cpp
/** How many turns between lifecycle processing for off-z Tier-2 monsters. */
extern int lod_lifecycle_stride;
```

#### Step B2 — cached_options.cpp

```cpp
int lod_lifecycle_stride = 4;
```

#### Step B3 — options.cpp

**Inside** the `lod_monster` option group (after `LOD_Z_PENALTY`):

```cpp
add( "LOD_LIFECYCLE_STRIDE", page_id,
     translate_marker( "Lifecycle Stride" ),
     translate_marker( "How many turns between lifecycle processing (item processing, effects, "
                       "field damage) for distant off-z Tier-2 monsters.  At 1 they process every "
                       "turn (disabled).  Higher values reduce CPU cost for off-z hordes on "
                       "field-free submaps.  Off-z monsters on field-containing submaps always "
                       "process every turn regardless of this setting." ),
     1, 10, is_android ? 6 : 4 );
```

**After** the group's `setPrerequisite` block (after the LOD_Z_PENALTY line):

```cpp
get_option( "LOD_LIFECYCLE_STRIDE" ).setPrerequisite( "MONSTER_LOD_ENABLED" );
```

**In** `cache_to_globals()`:

```cpp
lod_lifecycle_stride = ::get_option<int>( "LOD_LIFECYCLE_STRIDE" );
```

#### Step B4 — game.cpp

Modify the LOD-B loop (lines 5968-6020). Replace the inner block:

**Before** (lines 6000-6014):
```cpp
if( !critter.is_dead() ) {
    critter.process_items();
}

if( !critter.is_dead() ) {
    critter.process_turn();
}

m.creature_in_field( critter );
if( calendar::once_every( 1_days ) ) {
    if( critter.has_flag( MF_MILKABLE ) ) {
        critter.refill_udders();
    }
    critter.try_reproduce();
}
```

**After**:
```cpp
// ── Lifecycle stride gate ──────────────────────────────────────
// Tier-2 monsters far off-z (|dz| >= 2) on field-free submaps
// skip lifecycle processing every N turns.  Safe because:
//   (a) off-z + field-free → no field damage
//   (b) effect durations become N-granular (bounded by off-z +
//       field-free gate — no DoT, off-screen)
//   (c) process_turn → process_effects_internal() ticks durations;
//       N-granular on off-screen monsters is harmless
const auto monitor_pos = critter.bub_pos();
const bool striding = critter.lod_tier == 2 &&
                      lod_lifecycle_stride > 1 &&
                      std::abs( monitor_pos.z() - u.bub_pos().z() ) >= 2;

if( striding && current_turn % lod_lifecycle_stride != 0 ) {
    // Skip lifecycle this turn — will catch up next stride iteration.
    // process_turn side effects (cooldown decrement, emit fields,
    // grab persistence, electrical field) are deferred.
} else {
    if( !critter.is_dead() ) {
        critter.process_items();
    }
    if( !critter.is_dead() ) {
        critter.process_turn();
    }
    m.creature_in_field( critter );
}
// Daily events stay unconditional (already time-throttled):
if( calendar::once_every( 1_days ) ) {
    if( critter.has_flag( MF_MILKABLE ) ) {
        critter.refill_udders();
    }
    critter.try_reproduce();
}
```

### Optional: cross-z sight trim

In the sight-job builder (lines 5799-5843), skip monsters that are far off-z and Tier 2, since they
won't use sight in the macro step anyway:

```cpp
for( auto *mon : plannable ) {
    // ── NEW: skip sight pre-warm for far-off-z Tier-2 monsters ──
    if( mon->lod_tier == 2 && std::abs( mon->bub_pos().z() - u.bub_pos().z() ) >= 2 ) {
        continue;
    }
    // ... existing sight job logic continues ...
```

Only commit this if Tracy shows measurable sight-job count from off-z monsters. Otherwise it's an
unnecessary diff.

### Verification

| # | Check | How |
|---|-------|-----|
| 1 | Build green | `cmake --build <preset> --target cataclysm-bn-tiles cata_test-tiles` |
| 2 | Off-z horde skips lifecycle | Off-z Tier-2 monster on field-free submap: Tracy `monmove_lifecycle` per-monster time drops |
| 3 | Field submap always processes | Off-z Tier-2 monster on submap with `field_count > 0`: lifecycle runs every turn |
| 4 | Same-z unaffected | Same-z monsters (any tier) run lifecycle every turn |
| 5 | Existing tests pass | All `[monster]` tests pass (test monsters are typically Tier 0 or same-z) |

---

## Phase C — spatial-hash target acquisition (3–4 days)

### Root cause

`compute_plan()` in `monmove.cpp` uses `for_each_monster` (walks ALL monsters) in 3 scans:

| Scan | Lines | Purpose | Optimized? |
|------|-------|---------|------------|
| Cub-threatened | 507-544 (DUPLICATE) | Baby monsters nearby | No |
| Friendly-guard | 546-560 | Hostile monsters near friendly | No |
| Faction-hostiles | 650-712 | Enemy factions | Yes: `faction_snap` |
| Group-morale/swarm | 716-764 | Same-faction members | Yes: `faction_snap` |

The first two are O(M) per plannable monster → O(P·M). With a spatial hash, they become O(k) where
k = monsters in nearby buckets (~constant for bounded sight range).

### Files to modify

| File | Change | Lines |
|------|--------|-------|
| `src/monster.h` | Add `spatial_grid_t` to `compute_plan_context` | ~15 lines |
| `src/game.cpp` | Build spatial grid alongside faction_snap; add to `plan_ctx` | ~25 lines |
| `src/monmove.cpp` | Rewrite cub-threatened + friendly-guard scans to use grid; remove duplicate | ~60 lines |

### Step-by-step

#### Step C1 — monster.h

Inside `compute_plan_context` (line 262), add the spatial grid type and member:

```cpp
/// Spatial grid: monster positions bucketed by 8-tile granularity.
/// Built serially in game::monmove() alongside faction_snap.
/// Used by compute_plan() cub-threatened and friendly-guard scans
/// to replace O(M) iteration with O(k) bucket queries.
/// Nullptr = not built this tick (fall back to for_each_monster).
struct spatial_grid_t {
    static constexpr int bucket_size = 8;
    using key_t = std::pair<int, int>;
    std::unordered_map<key_t, std::vector<monster *>> buckets;
};
const spatial_grid_t *spatial_grid;
```

Update the constructors:

```cpp
constexpr compute_plan_context() noexcept
    : monsters( nullptr ), npcs( nullptr ), faction_snap( nullptr ),
      hostile_fac_map( nullptr ), spatial_grid( nullptr ) {}
constexpr compute_plan_context( const std::vector<monster *> *m,
                                const std::vector<npc *> *n,
                                const faction_snap_t *fs,
                                const hostile_fac_map_t *hfm,
                                const spatial_grid_t *sg = nullptr )
    noexcept : monsters( m ), npcs( n ), faction_snap( fs ),
               hostile_fac_map( hfm ), spatial_grid( sg ) {}
```

#### Step C2 — game.cpp

**Add `spatial_grid` to the activity-monmove cache** (~line 2385):

```cpp
monster::compute_plan_context::spatial_grid_t spatial_grid;
```

**Build the spatial grid** alongside the faction_snap (at ~line 5884):

```cpp
// Build spatial grid for O(k) target acquisition in compute_plan().
monster::compute_plan_context::spatial_grid_t spatial_grid;
const monster::compute_plan_context::spatial_grid_t *spatial_grid_for_plan = nullptr;
{
    ZoneScopedN( "monmove_build_spatial_grid" );
    if( use_activity_cache ) {
        spatial_grid_for_plan = &cache->spatial_grid;
    } else {
        for( monster *mon_ptr : *mon_snap ) {
            if( mon_ptr->is_dead() || !mon_ptr->is_simulated() ) {
                continue;
            }
            const auto pos = mon_ptr->bub_pos();
            const auto key = monster::compute_plan_context::spatial_grid_t::key_t{
                pos.x() / monster::compute_plan_context::spatial_grid_t::bucket_size,
                pos.y() / monster::compute_plan_context::spatial_grid_t::bucket_size
            };
            spatial_grid.buckets[key].push_back( mon_ptr );
        }
        if( cache != nullptr ) {
            cache->spatial_grid = spatial_grid;
            spatial_grid_for_plan = &cache->spatial_grid;
        } else {
            spatial_grid_for_plan = &spatial_grid;
        }
    }
}
```

**Wire into `plan_ctx`** at line 5926:

```cpp
const monster::compute_plan_context plan_ctx{
    mon_snap, npc_snap, faction_snap_for_plan,
    hostile_fac_map_for_plan, spatial_grid_for_plan
};
```

#### Step C3 — monmove.cpp

**Add a helper lambda** after `for_each_npc` (after line 410) to query the grid:

```cpp
// Spatial-grid query helper: iterates monsters in buckets within radius buckets.
// Returns false if the grid is unavailable (caller must fall back).
const auto for_monsters_nearby = [&]( int radius_buckets, auto &&fn ) -> bool {
    if( ctx.spatial_grid == nullptr ) {
        return false;
    }
    const auto bucket_of = []( const tripoint_bub_ms &pos ) {
        return monster::compute_plan_context::spatial_grid_t::key_t{
            pos.x() / monster::compute_plan_context::spatial_grid_t::bucket_size,
            pos.y() / monster::compute_plan_context::spatial_grid_t::bucket_size
        };
    };
    const auto my_bucket = bucket_of( bub_pos() );
    for( int dx = -radius_buckets; dx <= radius_buckets; ++dx ) {
        for( int dy = -radius_buckets; dy <= radius_buckets; ++dy ) {
            const auto it = ctx.spatial_grid->buckets.find(
                { my_bucket.first + dx, my_bucket.second + dy } );
            if( it != ctx.spatial_grid->buckets.end() ) {
                for( monster *mp : it->second ) {
                    if( mp != this ) {
                        fn( *mp );
                    }
                }
            }
        }
    }
    return true;
};
```

**Rewrite cub-threatened scan** (lines 507-544) — remove the duplicate block (526-544) and add grid
path:

```cpp
if( angers_cub_threatened > 0 ) {
    const auto process_cub = [&]( monster & tmp ) {
        if( type->baby_monster == tmp.type->id ) {
            dist = tmp.rate_target( g->u, dist, smart_planning );
            if( dist <= 3 ) {
                if( has_flag( MF_FACTION_MEMORY ) ) {
                    result.faction_angers.push_back(
                    { mfaction_id( "player" ), angers_cub_threatened } );
                } else {
                    local_anger += angers_cub_threatened;
                }
                local_morale += angers_cub_threatened / 2;
                result.aggro_triggers.push_back( "threatening cub" );
            }
        }
    };
    if( !for_monsters_nearby( 1, process_cub ) ) {
        for_each_monster( process_cub );
    }
}
```

Note: lines 507-525 and 526-544 are **identical code**. The grid rewrite replaces both — delete the
duplicate block entirely (lines 526-544).

**Rewrite friendly-guard hostile scan** (lines 546-560):

```cpp
} else if( local_friendly != 0 && !docile && !waiting ) {
    const auto process_foe = [&]( monster & tmp ) {
        if( tmp.friendly == 0 ) {
            const int d_tmp = rl_dist( bub_pos(), tmp.bub_pos() );
            if( d_tmp > max_sight_range ) {
                return;
            }
            float rating = rate_target( tmp, dist, smart_planning, d_tmp );
            if( rating < dist ) {
                target = &tmp;
                dist   = rating;
            }
        }
    };
    const int bucket_radius = max_sight_range /
        monster::compute_plan_context::spatial_grid_t::bucket_size + 1;
    if( !for_monsters_nearby( bucket_radius, process_foe ) ) {
        for_each_monster( process_foe );
    }
}
```

### Verification

| # | Check | How |
|---|-------|-----|
| 1 | Build green | `cmake --build <preset> --target cataclysm-bn-tiles cata_test-tiles` |
| 2 | Same decisions | Load save with 20+ zombies. A/B toggle via spatial grid build toggle (or just verify existing `[monster]` tests pass) |
| 3 | Cub-threatened works | Monster with `angers_cub_threatened` near a baby of its type triggers proximity anger at same distance |
| 4 | Friendly guard works | Friendly monster near hostile monsters selects targets correctly |
| 5 | No duplicate | `grep -c "angers_cub_threatened > 0" src/monmove.cpp` returns 1 |
| 6 | Tracy confirms | `cp_initial_target` zone time drops proportional to monster count |

---

## Build and test commands

```sh
# Format
cmake --build build --target format

# Build
cmake --preset osx-arm-slim
cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles

# Run monster regression tests
./out/build/osx-arm-slim/tests/cata_test-tiles "[monster]"

# Run vision tests
./out/build/osx-arm-slim/tests/cata_test-tiles "[vision]"

# Run behavior tests
./out/build/osx-arm-slim/tests/cata_test-tiles "[behavior]"

# Run field tests
./out/build/osx-arm-slim/tests/cata_test-tiles "[field]"

# Run activity tests (do_turn pipeline)
./out/build/osx-arm-slim/tests/cata_test-tiles "[activity][fixed_window]"
```

---

## Risk register

| # | Risk | Phase | Impact | Likelihood | Mitigation |
|---|------|-------|--------|------------|------------|
| 1 | z-penalty demotes a stair-follower | A | Zombie stops chasing down stairs | Low | `|dz| < 2` goal-clause escape hatch; test case validates |
| 2 | Lifecycle stride causes visible freeze/thaw on off-z monsters | B | Monster effects pause for N turns | Low | Off-z + field-free gate means no DoT/fire; effects just tick at N-granularity |
| 3 | Spatial grid bucket overhead > O(M) scan | C | Performance regression at low monster counts | Low | Fallback path retained; Tracy-gated | 
| 4 | Spatial grid misses targets at bucket boundaries | C | Monster doesn't see enemy 1 tile over bucket edge | Medium | Radius covers 3×3 buckets (24×24 tiles) even for cub check; sight-range bucket radius ensures full coverage |
| 5 | Phase B with `is_android` defaults too aggressive | B | Off-z monsters too slow on desktop | Low | Desktop default is 4, not 6 |
