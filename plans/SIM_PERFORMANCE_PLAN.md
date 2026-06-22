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

# PART 3 — Vehicle throttling  ·  SKETCH

### Problem
`m.vehmove()` + per-vehicle `idle()`→`power_parts()` (`vehicle.cpp:6042/5624`,
`O(part_count)`) + grid updates (`grid_trackers_`, `game.cpp:2050`) run each turn.
Cost drivers: large part counts, active electrical grids, moving vehicles.

### Approach
- Stride `idle()`/`power_parts()` for vehicles that are parked, unoccupied, and
  engine-off — process every K turns, accumulating charge/discharge by K.
- Leave moving / player-occupied / engine-on vehicles full-rate.
- Possibly distance/z-gate grid + portal-tap updates (`game.cpp:2056-2059`).

### Files (likely)
`src/map.cpp` (`vehmove` ~886), `src/vehicle.cpp` (`idle`/`power_parts`),
`src/game.cpp` (grid tick).

### Effort: Med · Risk: Med (battery/charge bookkeeping must stay consistent).

---

# PART 4 — Active-item striding  ·  SKETCH (lower risk)

### Problem
`m.process_items()` (`game.cpp:2044`) walks `submaps_with_active_items`
(`map.cpp:5875/5932`) every turn, ticking every rotting food, smoldering corpse,
burning light, ticking explosive, powered electronic across the bubble.

### Approach
- Stride non-time-critical active-item submaps that are far / off player z: process
  every K turns, advancing item ages by K (rot/charge are already time-based, so
  coarser cadence is mostly lossless).
- Never stride submaps containing countdown/explosive items (must tick exactly).
- Reuse the existing per-submap iteration in `process_items_in_submap`
  (`map.cpp:5971`).

### Files (likely)
`src/map.cpp` (`process_items`, `process_items_in_submap`),
`src/cached_options.*`, `src/options.cpp`.

### Effort: Low-Med · Risk: Low (time-based decay tolerates coarse cadence;
explosives carved out).

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
- **Part 4:** an armed explosive still detonates on its exact countdown; rot timing
  unchanged within stride granularity.
- Build green; run the fresh binary (verify mtime, not the stale root copy) in a
  heavy world and eyeball framerate.
