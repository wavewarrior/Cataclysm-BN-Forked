# Box2D Ranged Combat — Creature Hitboxes and Raycast Hit Resolution

## Context

Replace the per-tile scalar hit probability system in `projectile_attack` (src/ballistics.cpp:333) with Box2D raycasting against geometric creature/terrain/vehicle shapes. Currently, creature hits use a perpendicular-distance-to-tile-center scalar divided by `ranged_target_size()` (a flat fraction per size class: tiny=0.1, medium=0.5, huge=1.0). Terrain/furniture obstruction uses flat `block_unaimed_chance` rolls with no shape geometry. Box2D v3.0.0 is already integrated (now an unconditional link dependency — see Step 4 note below), with terrain static bodies and vehicle dynamic bodies present — but creatures have zero Box2D representation. This plan adds creature bodies and wires `b2World_CastRay` into the projectile flight loop for shape-accurate hit detection. Game logic (dodge rolls, bash tables, overpenetration modifiers, ammo effects) stays unchanged - Box2D only replaces the geometric intersection test.

This is Phase 11 of `plans/box2d-vehicle-physics-implementation.md`, expanded with creature body support. It has no hard dependency on Phase 10 Step 6 or Phase 12 — those retire vehicle motion fields unrelated to ranged combat.

## Approach

### Step 1 — superseded.

`src/physics/filter_bits.h` replaced the dual-range category-bit design entirely (both the plan's original two-range scheme AND this step's proposed three-range fix) with a single `z_category_bit(int z)` shared by all body types plus `b2Filter::groupIndex` (`terrain_group=0`, `vehicle_group=-1`, `creature_group=-2`) for type separation. No `terrain_category_bit`/`vehicle_category_bit`/`creature_category_bit` helpers exist or are needed. Nothing to do here.

### Step 2 and Step 3(a-d) — DONE.

`src/physics/physics_world.h` has `creature_bodies_` (`std::unordered_map<const Creature*, creature_body>`) plus `on_creature_added`/`on_creature_moved`/`on_creature_removed`/`clear_creature_bodies`, wired from `Creature_tracker::add()` and both `Character::setpos`/`monster::setpos` (not a single `Creature::setpos` choke point as originally assumed — each subclass override calls the hook independently). `src/ballistics.cpp` runs a `b2World_CastRay` before the DDA loop, collecting sorted-by-fraction creature hits and computing `missed_by` as `perp/radius` — matching the design. **Mechanism deviation**: the shipped code uses `groupIndex` + sensor shapes (`enableSensorEvents=false`, checked via `b2Shape_IsSensor` in the raycast callback) rather than the plan's originally specified `categoryBits`/`maskBits` split — same isolation goal, different mechanism. Note this so a future reader isn't confused when the code doesn't match the pseudocode verbatim.

### Step 3e and Step 4 — resolved by the landed implementation

Step 3e's `b2QueryFilter` vs `b2Filter` semantics question is moot: the shipped code does use a `b2QueryFilter` (`ballistics.cpp:539-541`, `categoryBits`/`maskBits` both `z_category_bit(source.z())`) but only to select the z-level, not to split terrain/vehicle/creature by category/mask as originally designed — body-type selection happens separately, via `b2Shape_IsSensor()` inside the callback (see Mechanism deviation above), sidestepping the ambiguity this step worried about.

Step 4's premise no longer holds: `BOX2D_ENABLED` does not exist anywhere in the codebase. `src/CMakeLists.txt:250-252` documents Box2D as an unconditional link dependency ("no BOX2D_ENABLED define and no #ifdef guards in the sources") — there is no non-Box2D fallback build to support.

### Remaining open scope

`src/physics/` is a high-churn, HEAD-only area during the in-flight `plans/merge-main-into-improvements.md` merge — do not start the terrain/vehicle raycast work until that merge lands and stabilizes.

- **Terrain/vehicle body raycast dispatch** to replace the per-tile `map::shoot()` path — currently `src/ballistics.cpp:674-691,737-749` still resolves terrain/vehicle hits via `here.shoot(source, rand/tp, proj, ...)` and an ad-hoc `obstructed_by_vehicle_rotation` branch, completely independent of the creature-only Box2D raycast already landed above.
- **LOS/cover-table retirement** (formerly tracked as "After Phase 11" in `plans/box2d-vehicle-physics-implementation.md`, now tracked only here):
  - Tile-traversal LOS implementation retirement
  - Per-tile cover modifier table retirement

## Critical files & anchors

| File | Symbol / region | Why |
|---|---|---|
| `src/ballistics.cpp:481-650` | `projectile_attack` per-tile loop | The hit-detection code this plan replaces; animation and trail-effect code in the same loop must NOT be disturbed |
| `src/physics/physics_world.h` | `PhysicsWorld` class | Add `creature_bodies_` registry and `on_creature_{added,moved,removed}` methods alongside existing vehicle/terrain equivalents |
| `src/creature_tracker.cpp:57,203` | `Creature_tracker::add/remove` | Lifecycle hook sites for creature body creation/destruction — existing pattern mirrors `on_vehicle_added/removed` |
| `src/character.cpp:812` / `src/monster.cpp:397` | `Character::setpos` / `monster::setpos` | Dual hook sites for `on_creature_moved` — NOT a single `Creature::setpos` base-class choke point; each subclass override calls the hook independently |
| `src/physics/terrain_body.cpp:39,71` | Filter bit expressions | Already migrated — calls `physics::z_category_bit(z)` and `physics::terrain_group`, no raw `1ull << (z+N)` expressions remain |

## Verification

### Step 1 (Filter bit fix) — OBSOLETE, no verification needed
Step 1 itself was superseded (see above) — `filter_bits.h`'s `z_category_bit(z)` was never a "bit shift" needing verification against a prior scheme; there is no separate terrain-z-10-vs-vehicle-z-10 category-bit distinction to check for.

### Step 2 (Creature bodies)
Add a temporary debug log in `on_creature_added` that prints creature name + body position. Launch game, load a save with monsters nearby. Confirm log shows one body per visible creature. Move the player — confirm `on_creature_moved` fires for the avatar. Kill a monster — confirm `on_creature_removed` fires. Shift map (walk to edge of reality bubble) — confirm creature bodies translate correctly and no orphaned bodies remain.

### Step 3 (Raycast hit resolution)
Build with Box2D ON. Spawn a gun + ammo via debug menu. Set `ANIMATION_DELAY` to ≥50ms. Fire at a monster at various ranges and angles:
- Point-blank center shot → should hit with low `missed_by` (headshot chance)
- Edge graze shot → should hit with high `missed_by` (graze)
- Shot past a monster into a wall → wall stops projectile, monster unhit
- Shot through a window (passable furniture) → furniture may block (bash roll), then wall
- Penetrating ammo through multiple monsters → verify both take damage in correct order

Run `cata_test-tiles "[ranged]"` to confirm existing ranged combat tests pass — hit probabilities should be statistically equivalent since the circle shapes match the existing `occupied_tile_fraction` sizes.

Build (`cmake --preset osx-arm-slim`): Box2D is an unconditional link dependency (see Step 3e/Step 4 note above) — there is no non-Box2D build configuration to verify against.

## Assumptions & contingencies

- **`b2CastResultFcn` callback signature** — RESOLVED: confirmed `float` return (`1.0f` continues the ray), matching this assumption; the shipped callback in `ballistics.cpp` returns `float` as assumed.
- **`b2QueryFilter` vs shape `b2Filter`** — RESOLVED: the shipped raycast uses `b2DefaultQueryFilter()` with `categoryBits`/`maskBits` both set to `z_category_bit(z)` (matches all bodies at that z-level, no category/mask split), then discriminates terrain/vehicle/creature via `b2Shape_IsSensor()` + `groupIndex` inside the callback instead — see Mechanism deviation and Step 3e note above.
- **Phase 10 Step 6 / Phase 12 independence**: This plan does not depend on those phases. If a merge conflict arises in `map_vehicle.cpp` or `vehicle.h`, it will be in the vehicle-motion code, not the ranged-combat code — resolve by keeping both changes.
- **Creature shape recreation on size change**: Crouch toggle changes `ranged_target_size()`, which changes the circle radius. The plan tracks the last radius in `creature_body::radius` and recreates the shape in `on_creature_moved` only when the size changes. If crouch state doesn't trigger `setpos` (it shouldn't), add a separate hook in the crouch toggle path (grep `is_crouching` state changes) that calls `on_creature_moved`. If this proves too invasive, defer dynamic shape resizing — use the uncouched size always. The geometric difference is one bracket (e.g., medium→small), which is a 2x radius change — noticeable but not game-breaking if deferred.
- **Z-level raycasting**: Box2D is 2D. Cross-z projectile paths (shooting up/down through floors) cannot use Box2D raycasting. The existing DDA floor-check code (ballistics.cpp:491-509) handles z-transitions. When the trajectory crosses z-levels, fall back to the existing per-tile logic for those tiles. The Box2D raycast only operates within a single z-level.
