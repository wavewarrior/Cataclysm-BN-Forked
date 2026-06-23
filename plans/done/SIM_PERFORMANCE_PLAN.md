# Simulation Performance — Master Plan (splittable)

This is a roadmap covering monster AND same-z hotspots. Each **Part** is
self-contained and can be spun off into its own detailed plan later. Part 1
(monster AI) is fully specified; Parts 2–4 are design sketches to be expanded.

## Shared context
Sim degrades with many entities (a) several z-levels below the player, and (b)
in large numbers on the player's own z. Existing optimizations (LOD tiers,
action budget, parallel planning, sight cache) cover **monsters only**. Same-z
cost from NPCs, vehicles, fields, and active items is largely unthrottled.
Per-turn pipeline lives in `game::do_turn` (`game.cpp:2018-2092`).

---

# PART 1 — Monster AI (Tiers A+B+C)  ·  FULLY SPECIFIED

### Root causes
1. z weighted equal to horizontal: `square_dist`=`max(dx,dy,|dz|)` (`line.h:172`) →
   horde N floors down = "distance N" → Tier-0 (`game.cpp:5397`).
2. Goal-holders forced Tier-0: `dist<=tier01_dist || !mon.is_wandering()`
   (`game.cpp:5398`); `is_wandering()==(goal==pos)`. Short-circuits before distance.
3. Unbudgeted lifecycle: LOD-B loop (`game.cpp:5968-6020`) ticks
   `process_items`/`process_turn`/`creature_in_field` for every simulated monster,
   every z, every turn.
4. O(M²) target scan: `compute_plan` walks the full snapshot
   (`monmove.cpp:508/527/547/575`).

Constraint: cross-z chase needs `fov_3d` (`monmove.cpp:1204/1226`); Tier-2 macro
(`do_tier2_macro`, `game.cpp:6145`) is same-z only → all z-demotion uses a
**`|dz|>=2` guard** so stair-followers one floor away keep full AI.

### Phase A — z-aware tiering (low risk; the reported bug)
- New option `LOD_Z_PENALTY` (default 16): `cached_options.{h,cpp}`,
  `options.cpp` (`add` ~2473, `setPrerequisite("MONSTER_LOD_ENABLED")` ~2514,
  cached read ~4336).
- Helper `lod_dist(a,b) = rl_dist(a.xy,b.xy) + lod_z_penalty*|dz|`.
- `tier_assign_all` (`game.cpp:5390-5405`): use `lod_dist`; goal clause →
  `dist<=tier01_dist || ( !mon.is_wandering() && |dz| < 2 )`.
- Apply `lod_dist` to eligible sort + budget cut, both paths (`game.cpp:6049`,
  `6087`).
- Files: `src/game.cpp`, `src/cached_options.{h,cpp}`, `src/options.cpp`.

### Phase B — stride the lifecycle floor (higher risk; profiling-gated)
- New option `LOD_LIFECYCLE_STRIDE` (default 4).
- In LOD-B, run lifecycle every stride turns only when `lod_tier==2` AND `|dz|>=2`
  AND submap field-free (`field_count==0`, pattern `map.cpp:3549`). Phase
  next_turn/cooldowns by the stride (reuse budget-drain logic `game.cpp:6105`).
- Risk: `process_turn → process_effects_internal()` (`monster.cpp:3489`) ticks
  effect durations → off-z Tier-2 timers become stride-granular. Bounded by the
  off-z + field-free gate (no DoT, off-screen). Document in comment.
- Optional #3 (cross-z sight trim, `game.cpp:5799-5843`): only if `|dz|==1` band
  still hot after A — mostly redundant once Tier-2 leaves `plannable`.

### Phase C — spatial-hash target acquisition (same-z; correctness-safe)
- Build a per-tick monster grid serially beside `faction_snap` (`game.cpp:~5884`),
  store in `compute_plan_context`. Rewrite the 4 `for_each_monster` scans to query
  buckets within `max_sight_range` → O(M²)→~O(M·k). Group-morale/swarm preserved via
  existing `faction_snap`/`hostile_fac_map`. Build-serial/read-parallel safety
  identical to current snapshots.
- Files: `src/monmove.cpp`, `src/monster.h`, `src/game.cpp`.

---

# PART 2 — NPC LOD / budget  ·  SKETCH (highest same-z ROI)

### Problem
`npcmove()` (`game.cpp:6325`) iterates `all_npcs()` with only an `is_simulated()`
gate — **no LOD, no budget, no parallelism**. Each NPC runs the full character sim:
`npc::process_turn → player::process_turn` (`npc.cpp:3130`) + a `while moves>0`
loop calling `npc::move()` (`npcmove.cpp:818`) which runs `regen_ai_cache()`
(`npcmove.cpp:765`) per iteration. One NPC ≫ one zombie.

### Approach
- **Do NOT attempt parallel planning** — `npc::move`/`regen_ai_cache` mutate broad
  state (path, targets, map). Unsafe to port the monster compute_plan split.
- Realistic win = **distance LOD + action budget + stride**, mirroring the
  monmove knobs:
  - Tier distant NPCs (reuse `lod_dist` from Part 1) and stride `regen_ai_cache` /
    AI for far, non-following, non-visible NPCs (every K turns).
  - Budget the number of full-AI NPCs per turn (sorted by distance), like the
    monster `effective_budget`.
- **Risk:** companions/visible NPCs are player-facing — striding an ally that is
  following or on-screen is noticeable. Gate strides to NPCs that are distant AND
  not in the player's follower set AND not currently visible.

### Files (likely)
`src/game.cpp` (`npcmove`), `src/npcmove.cpp` (`regen_ai_cache`, `move`),
`src/cached_options.*`, `src/options.cpp`.

### Effort: Med-High (full character sim makes correctness subtle).

---

# PART 3 — Vehicle throttling  ·  FULLY SPECIFIED

### Root causes
1. **`idle()` → `power_parts()` every turn for every connected vehicle**
   (`vehicle.cpp:6042`, `5624`): `enumerate_vehicles` runs a distribution-graph BFS
   from every loaded vehicle, then calls `idle()`→`power_parts()` on each connected
   vehicle — O(engines + alternators + reactors + part-group filters) per vehicle.
   For a parked, engine-off vehicle this still scans engines, alternators, reactors,
   and enabled-drains flags every turn (`vehicle.cpp:5624-5724`).

2. **Outer loop `gain_moves()` + `slow_leak()` on every bubble vehicle, all z**
   (`map.cpp:896-905`): z-loop spans up to 21 levels (`-OVERMAP_DEPTH` to
   `OVERMAP_HEIGHT`). `gain_moves()` does a distribution-graph BFS
   (`vehicle.cpp:6456-6457`) + turret checks (`6465-6468`) even for parked vehicles.
   `slow_leak()` scans all parts (`vehicle.cpp:6141-6174`) — O(N) per vehicle,
   multiplied by z × vehicle count.

3. **`tick_vehicle_portal_taps()` O(V × P) every turn** (`game.cpp:17159-17188`):
   iterates every vehicle, every part, checking `portal_tap_linked`. Common case (no
   portal taps) still pays the loop overhead.

4. **Grid-tracker update** (`game.cpp:2049-2055`) + `enumerate_vehicles` BFS
   (`map.cpp:1039-1040`): graph rebuild per turn scales with connected-vehicle count.

### Phase A — `idle()` stride for parked vehicles (low risk; the straightforward win)

**Condition — a vehicle must be STRIDED (full-rate on stride turn only) when ALL apply:**
- `!is_moving()` (velocity == 0)
- `!is_falling`
- `!engine_on`
- `!player_in_control( g->u )`
- `!is_following && !is_patrolling`
- `reactors.empty()` — skip stride when reactors exist (fuel consumption must be exact;
  stride would systematically under-count burn by up to K-1 turns).
- `!is_rotorcraft() || !is_flying_in_air()` — helicopters need rotor power every turn.

When ANY of the above conditions is false → full-rate (idle every turn).

**New option `VEHICLE_IDLE_STRIDE`** (default 5, range 1–20, integer):
`cached_options.{h,cpp}`, `options.cpp` (`add` next to LOD options,
`setPrerequisite( "MONSTER_LOD_ENABLED" )`). Cached as `vehicle_idle_stride`.

**Code change in `map::vehmove()` (`map.cpp:1041-1043`):**

```
// Before:
std::ranges::for_each( connected_vehicles, []( auto &veh_pair ) {
    veh_pair.first->idle( veh_pair.second );
} );

// After:
const int stride = get_option<int>( "VEHICLE_IDLE_STRIDE" );
const int turn_nr = to_turn<int>( calendar::turn );
const bool stride_hit = ( turn_nr % stride ) == 0;

std::ranges::for_each( connected_vehicles, [&]( auto &veh_pair ) {
    vehicle &veh = *veh_pair.first;
    const bool on_map = veh_pair.second;

    const bool full_rate = veh.is_moving()
        || veh.is_falling
        || veh.engine_on
        || veh.player_in_control( g->u )
        || veh.is_following
        || veh.is_patrolling
        || !veh.reactors.empty()
        || ( veh.is_rotorcraft() && veh.is_flying_in_air() );

    if( full_rate || stride_hit ) {
        veh.idle( on_map );
    }
} );
```

**Known limitation:** Power accounting for strided vehicles is stride-granular. A
parked vehicle with accessory drain only updates the battery on stride turns, so
battery level lags by up to `K − 1` turns. For off-screen, no-player-interaction
vehicles this is imperceptible (same rationale as Tier-2 monster lifecycle stride).

**Files:** `src/map.cpp` (1041-1043), `src/cached_options.{h,cpp}`,
`src/options.cpp`.

### Phase B — outer-loop stride for off-z parked vehicles (lower ROI; profiling-gated)

The outer loop (`map.cpp:896-905`) runs `gain_moves()` + `slow_leak()` on EVERY
bubble vehicle every turn. For off-z parked vehicles, these are mostly wasted.

**New option `VEHICLE_OUTER_STRIDE`** (default 2, range 1–10). Integer. Cache as
`vehicle_outer_stride`. Same prerequisite as Phase A.

**Qualification:** `!veh->is_moving() && !veh->engine_on && !veh->is_falling`
AND `z != abs_sub.z()` (off player z-level).

**Code change in `map::vehmove()` (`map.cpp:896-905`):**

```
const int outer_stride = get_option<int>( "VEHICLE_OUTER_STRIDE" );
const bool outer_stride_hit = ( turn_nr % outer_stride ) == 0;

for( int z = zmin; z <= zmax; ++z ) {
    for( vehicle *veh : get_cache( z ).vehicle_list ) {
        const bool on_player_z = z == abs_sub.z();
        const bool parked_off_z = !veh->is_moving()
            && !veh->engine_on
            && !veh->is_falling
            && !on_player_z;
        const bool skip_outer = parked_off_z && !outer_stride_hit;

        if( !skip_outer ) {
            veh->gain_moves();
            veh->slow_leak();
        } else {
            veh->of_turn = 0.001f;  // keep priority marker for parked
        }
        vehicle_list.push_back( wrapped_vehicle{ .pos = veh->bub_ms_location(), .v = veh } );
    }
}
```

**Risk:** Off-z turret vehicles fire every outer-stride turn instead of every turn.
Bounded by off-z gate (player can't see them). If profiling shows off-z turrets as
performance-critical, add `!veh->turrets().empty()` exclusion.

**Files:** `src/map.cpp` (~896-905), `src/cached_options.{h,cpp}`,
`src/options.cpp`.

### Phase C — portal-tap bool gate (trivial; correctness-safe)

`tick_vehicle_portal_taps()` (`game.cpp:17159-17188`) scans every vehicle, every
part for the `portal_tap_linked` flag. For the common case (no portal taps), this
is pure overhead.

**Add bool `has_portal_tap_parts` to `vehicle` (`vehicle.h` ~1874)** — initially
`false`. Set to `true` in `vehicle::refresh()` when any part has the
`POWER_DRAW_LINKED_PORTAL` flag.

**Gate the outer loop in `tick_vehicle_portal_taps()`:**

```
std::ranges::for_each( m.get_vehicles(), [&]( wrapped_vehicle & wv ) {
    vehicle &veh = *wv.v;
    if( !veh.has_portal_tap_parts ) {
        return;  // skip part scan
    }
    // existing for-loop over parts...
} );
```

**Files:** `src/vehicle.h` (bool field), `src/vehicle.cpp` (set in `refresh()`),
`src/game.cpp` (gate).

### Effort: Low-Med · Risk: Low-Med

| Phase | Effort | Risk | Rationale |
|-------|--------|------|-----------|
| A     | Low    | Low  | Simple conditional gate; known-bound power lag |
| B     | Low    | Low  | Off-z-only; same pattern as A |
| C     | Trivial| None | Read-only bool skip; zero semantic change |

---

# PART 4 — Active-item striding  ·  FULLY SPECIFIED

### Root causes

1. **`process_items()`** (`map.cpp:5913-5982`) iterates a snapshot of
   `submaps_with_active_items` every turn, calling `process_items_in_submap()` on
   each simulated submap — across all z-levels in the bubble. Each call copies the
   active item list and runs `process_map_items()` → `item::process_internal()`
   (`item.cpp:10912-11077`) on every fast-band (speed=1) item.

2. **Fast-band items** (`processing_speed() == 1`, `item.cpp:10085-10092`) include
   tools, artifacts, relics, emissions, LITCIG, wet/wind extinguishing,
   CABLE_SPOOL, UPS — processed every turn. For off-z submaps, these per-turn
   side effects are imperceptible.

3. **`active_item_cache::get_for_processing()`**
   (`active_item_cache.cpp:121-191`) already throttles slow-band items (food,
   corpses, speed=600) via credit-based round-robin. The fast band has no
   throttling — all items, every turn, on every submap.

4. **Countdown/explosive items** (grenades, timers, bombs) use `item_counter`
   decrement (`item.cpp:10973`) and `countdown_action` fire
   (`item.cpp:10976-10982`) or the `"explosion"` use action. These must tick
   **exactly** every turn — they cannot be strided.

### Phase A — Time-critical item counter in `active_item_cache` (trivial; enables B)

The existing `special_items[special_item_type::explosive]` index tracks items
with an `"explosion"` use action but has no index for countdown items
(`has_explicit_turn_timer`). Adding a full scan per turn defeats the purpose,
so we maintain a reference counter.

**Add to `active_item_cache`** (`active_item_cache.h`, alongside `special_items`):

```cpp
int time_critical_count = 0;

auto has_time_critical_items() const -> bool {
    return time_critical_count > 0;
}
```

**In `active_item_cache::add()`** (`active_item_cache.cpp:53-71`): after the
existing `special_items` checks, increment counter:

```cpp
if( it.get_use( "explosion" ) || it.has_explicit_turn_timer() ) {
    ++time_critical_count;
}
```

**In `active_item_cache::remove()`** (`active_item_cache.cpp:17-51`): after the
existing `special_items` cleanup, decrement counter:

```cpp
if( it->get_use( "explosion" ) || it->has_explicit_turn_timer() ) {
    --time_critical_count;
}
```

**Hysteresis note:** When `item_counter` drops to 0 in `process_internal()`,
`has_explicit_turn_timer()` returns false, but `remove()` hasn't run yet — the
counter stays ≥1 for up to 1 turn. This is conservative (prevents striding when
borderline) and safe.

**Empty submap note:** `active_item_cache` is a value member — default-
construction initializes `time_critical_count = 0`. On submap/vehicle unload,
the destructor runs normally (no special handling needed).

**Files:** `src/active_item_cache.h`, `src/active_item_cache.cpp`.

### Phase B — Off-z submap stride (low risk; the straightforward win)

**New option `ITEM_PROCESS_STRIDE`** (default 1, range 1–10, integer):
`cached_options.{h,cpp}`, `options.cpp` (add in a new option group
`"item_processing"` within the performance tab). Cached as
`item_process_stride`. `setPrerequisite("MONSTER_LOD_ENABLED")`.

**Qualification:** A submap is eligible for striding ONLY when ALL of:
- `abs_pos.z() != abs_sub.z()` — off the player's z-level.
- `!current_submap->active_items.has_time_critical_items()` — no explosives or countdowns.
- `item_process_stride > 1` — feature enabled.
- `( turn_nr % item_process_stride ) != 0` — not a stride-hit turn.

During the stride-hit turn (every K-th turn), the submap processes normally. On
skip turns, both counting and processing are skipped.

**Code change in `map::process_items()` (`map.cpp:5958-5977`):**

```cpp
// Before:
auto active_items = std::vector<item *> {};
{
    ZoneScopedN( "process_items_scan_active_submaps" );
    for( const tripoint_abs_sm &abs_pos : submaps_with_active_items_copy ) {
        if( !submap_loader.is_simulated( bound_dimension_,
                tripoint_abs_sm( abs_pos ) ) ) {
            continue;
        }
        const auto local_pos = abs_to_bub( abs_pos );
        submap *const current_submap = get_submap_at_grid( local_pos );
        if( current_submap == nullptr ) {
            continue;
        }
        if( !current_submap->active_items.empty() ) {
            {
                ZoneScopedN( "process_items_count_active_items" );
                const auto counts = current_submap->active_items.count();
                total_active_items += counts.total;
                total_rottable_active_items += counts.rottable;
            }
            process_items_in_submap( *current_submap, local_pos, active_items );
        }
    }
}

// After:
auto active_items = std::vector<item *> {};
{
    ZoneScopedN( "process_items_scan_active_submaps" );
    const int stride = item_process_stride;
    const bool stride_enabled = stride > 1;
    const int turn_nr = to_turn<int>( calendar::turn );
    const int map_z = abs_sub.z();

    for( const tripoint_abs_sm &abs_pos : submaps_with_active_items_copy ) {
        if( !submap_loader.is_simulated( bound_dimension_,
                tripoint_abs_sm( abs_pos ) ) ) {
            continue;
        }
        const auto local_pos = abs_to_bub( abs_pos );
        submap *const current_submap = get_submap_at_grid( local_pos );
        if( current_submap == nullptr ) {
            continue;
        }
        if( current_submap->active_items.empty() ) {
            continue;
        }

        // Stride: off-z submaps with no time-critical items skip K-1/K turns.
        if( stride_enabled
            && abs_pos.z() != map_z
            && !current_submap->active_items.has_time_critical_items()
            && ( turn_nr % stride ) != 0 )
        {
            continue;  // skip counting and processing this turn
        }

        {
            ZoneScopedN( "process_items_count_active_items" );
            const auto counts = current_submap->active_items.count();
            total_active_items += counts.total;
            total_rottable_active_items += counts.rottable;
        }
        process_items_in_submap( *current_submap, local_pos, active_items );
    }
}
```

**Known limitation:** Per-turn side effects (emissions, tool drain, artifact
effects, LITCIG, UPS) for off-z non-countdown items are delayed by up to `K − 1`
turns. Since the items are off the player's z-level, no player-facing behavior
changes. The `last_touched` field is not updated by `process_items()`, so if the
submap later re-enters the player's z-level, `loadn`'s
`run_submap_batch_turns()` catches up countdown items correctly (existing
behavior, unchanged).

**Vehicle item note:** Items inside vehicles on off-z submaps are processed by a
separate code path (`process_items_in_vehicles`) and are NOT strided by this
change. They remain full-rate. If profiling shows this is significant, a
follow-up can add the same gate to the per-submap vehicle-processing loop
(Phase D candidate).

**Files:** `src/map.cpp` (~5958-5977), `src/cached_options.{h,cpp}`,
`src/options.cpp`, `src/active_item_cache.h`, `src/active_item_cache.cpp`.

### Phase C — On-z distance submap stride (lower ROI; profiling-gated)

If profiling after B shows on-z submaps with many active items are still a
bottleneck, extend striding to distant same-z-level submaps.

**New option `ITEM_PROCESS_DIST`** (default 0 = off, range 0–20, integer).
When `> 0`, submaps whose Chebyshev distance from the player's submap position
exceeds this threshold are eligible for striding (same rate as
`ITEM_PROCESS_STRIDE`), provided they have no time-critical items.

**Qualification:** same as Phase B but with `abs_pos.z() == map_z` AND
horizontal Chebyshev distance `> item_process_dist` submaps.

**Risk:** On-z side effects (emissions, extinguish checks, artifact effects)
from distant items are imperceptible beyond view range, but the correctness
boundary is weaker than off-z. Conservative default: `0` (off, opt-in by
player).

**Files:** `src/map.cpp`, `src/cached_options.{h,cpp}`, `src/options.cpp`.

### Phase D — Vehicle items off-z stride (follow-up; profiling-gated)

If profiling shows vehicle active-item processing on off-z parked vehicles is
significant, add the same stride gate to the vehicle submap loop in Phase 1 of
`process_items()`.

The vehicle loop already collects submaps per-z-level via
`get_cache(z).vehicle_list`. The per-submap processing is at `map.cpp:5934-5947`.
The same `has_time_critical_items()` check applies (vehicle's `active_items` is
also an `active_item_cache`), but the lookup would be:
`veh->active_items.has_time_critical_items()` for any vehicle on the submap. A
submap is eligible if ALL vehicles on it have no time-critical items.

**Deferred unless profiling demands it.**

### Summary

| Phase | Effort | Risk | Rationale |
|-------|--------|------|-----------|
| A     | Trivial| None | Counter-based flag; zero semantic change |
| B     | Low    | Low  | Off-z gate; countdown/explosive carve-out |
| C     | Low    | Low-Med | On-z distant; weaker correctness boundary; opt-in by default |
| D     | Low    | Low  | Same pattern as B; vehicle-specific |

---

# Deferred — Tier D: active-z-set (per-z bubble)
Highest ceiling, highest risk; gate full sim to player-z ± vision + recently
interacted z. Pursue only if Parts 1–4 fall short.

---

# Verification (per part)
- **Tracy before/after** over the relevant stress scene; the per-system zones
  already exist: `monmove_*`, `npc_process_turn`/`npc_move_iter`,
  `process_items_in_submap`, vehicle/`do_turn_process_items` windows, plus
  `_perf_mon`/`_perf_world`/`_perf_cache` counters in `do_turn`.
- **Part 1 regressions:** with `fov_3d` ON a zombie one floor down still climbs to
  the player; off-z monster in fire still dies on time (field-free gate); same-z
  horde makes identical decisions, faster.
- **Part 2:** a following companion still acts every turn; only distant/off-screen
  NPCs slow.
- **Part 3:** vehicle batteries/charge match a full-rate baseline over N turns.
- **Part 4:** Tracy before/after over a large base with active items on z±1, z±2;
  the `process_items_scan_active_submaps` zone shrinks proportionally. An armed
  explosive on z−1 still detonates on its exact countdown. A LITCIG on z+2 (no
  explosives) persists on skip turns, processes normally on stride-hit turns.
  Re-entry catchup via `loadn` → `run_submap_batch_turns` is unaffected.
- Build green; run the fresh binary (verify mtime, not the stale root copy) in a
  heavy world and eyeball framerate.
