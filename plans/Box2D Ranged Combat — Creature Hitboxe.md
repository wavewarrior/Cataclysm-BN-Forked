# Box2D Ranged Combat — Creature Hitboxes and Raycast Hit Resolution

## Context

Replace the per-tile scalar hit probability system in `projectile_attack` (src/ballistics.cpp:333) with Box2D raycasting against geometric creature/terrain/vehicle shapes. Currently, creature hits use a perpendicular-distance-to-tile-center scalar divided by `ranged_target_size()` (a flat fraction per size class: tiny=0.1, medium=0.5, huge=1.0). Terrain/furniture obstruction uses flat `block_unaimed_chance` rolls with no shape geometry. Box2D v3.0.0 is already integrated (opt-in `-DBOX2D=ON`), with terrain static bodies and vehicle dynamic bodies present — but creatures have zero Box2D representation. This plan adds creature bodies and wires `b2World_CastRay` into the projectile flight loop for shape-accurate hit detection. Game logic (dodge rolls, bash tables, overpenetration modifiers, ammo effects) stays unchanged — Box2D only replaces the geometric intersection test.

This is Phase 11 of `plans/box2d-vehicle-physics-implementation.md`, expanded with creature body support. It has no hard dependency on Phase 10 Step 6 or Phase 12 — those retire vehicle motion fields unrelated to ranged combat.

## Approach

### Step 1: Fix filter bit overlap (prerequisite, independent)

Terrain z-bit `1ull << (z+10)` and vehicle z-bit `1ull << (z+30)` collide at bit 20 when terrain z=10 and vehicle z=-10. Currently harmless but prevents a clean third category. Fix by shifting vehicles to `1ull << (z+31)`:

- `src/physics/terrain_body.cpp:51-52` — terrain category `1ull << (z+10)` is correct, leave it. Change terrain **mask** from `1ull << (z+30)` to `1ull << (z+31)` so it matches the shifted vehicle category.
- `src/physics/physics_world.cpp:68-70` — change vehicle category from `1ull << (z+30)` to `1ull << (z+31)`. Change vehicle **mask** from `1ull << (z+10)` to `1ull << (z+10)` (unchanged — terrain category bits didn't move).

Add named helper functions in a new `src/physics/filter_bits.h` (no existing equivalent):

```cpp
#pragma once
#include <cstdint>

namespace physics {
inline auto terrain_category_bit( int z ) -> uint64_t { return 1ull << ( z + 10 ); }  // bits 0-20
inline auto vehicle_category_bit( int z ) -> uint64_t { return 1ull << ( z + 31 ); }  // bits 21-41
inline auto creature_category_bit( int z ) -> uint64_t { return 1ull << ( z + 52 ); } // bits 42-62
} // namespace physics
```

Replace raw `1ull << (z+N)` expressions in `terrain_body.cpp` and `physics_world.cpp` with these helpers. Grep `1ull <<.*z\s*\+\s*(10|30)` in `src/physics/` to find all sites — confirmed exactly 4 expressions across two files.

### Step 2: Creature body subsystem (depends on Step 1)

Add Box2D bodies for all creatures in the reality bubble — monsters, NPCs, and the player avatar. Bodies are kinematic (positioned explicitly via `b2Body_SetTransform`, not driven by forces) with circle shapes sized to the creature's `ranged_target_size()`.

#### 2a. Body creation and registry

Add to `PhysicsWorld` (src/physics/physics_world.h), mirroring the existing `vehicle_bodies_` pattern:

```cpp
std::unordered_map<const Creature*, b2BodyId> creature_bodies_;

void on_creature_added( const Creature& c );
void on_creature_moved( const Creature& c );
void on_creature_removed( const Creature* c );
```

Implement in `physics_world.cpp`:

- `on_creature_added`: Create a `b2_kinematicBody` at the creature's tile position (convert `bub_ms` → physics meters using `TILE_M`). Attach a `b2Circle` shape with radius = `c.ranged_target_size() * TILE_M * 0.5f` (half-tile scale, since `ranged_target_size` returns the occupied fraction of the tile, and the circle radius should be half that diameter). Set `sdef.filter.categoryBits = creature_category_bit(z)` and `sdef.filter.maskBits = terrain_category_bit(z) | vehicle_category_bit(z)` (creatures don't physically interact with each other — raycast queries use separate per-cast filters). Tag with `b2Body_SetUserData(bodyId, const_cast<Creature*>(&c))` for damage dispatch (same pattern as vehicle bodies, which store `vehicle*`).
- `on_creature_moved`: `b2Body_SetTransform(bodyId, new_pos_m, b2Rot_identity)`. Update filter bits if z changed.
- `on_creature_removed`: `b2DestroyBody(bodyId)`, erase from `creature_bodies_`.

#### 2b. Lifecycle hook wiring

Call the PhysicsWorld methods from existing creature lifecycle sites. All calls gated by `#ifdef BOX2D_ENABLED` and null-check on `get_map().phys_world`:

- **Creation**: `Creature_tracker::add()` (src/creature_tracker.cpp:57-82) — after successful insertion, call `on_creature_added(*mon)`. For NPCs: in `game::load_npcs()` (src/game_setup.cpp:956) after an NPC is pushed into `active_npc`. For the avatar: in `game::setup()` or `game::load()` after the avatar's position is set.
- **Movement**: `Creature::setpos(tripoint_abs_ms)` (src/creature.cpp:2740) — this is the base-class position write that all subclass overrides funnel through (confirmed by coop networking plan audit). Add `on_creature_moved(*this)` after the position field is written. This single site covers monster, NPC, and avatar movement.
- **Removal**: `Creature_tracker::remove()` (src/creature_tracker.cpp:203-224) — call `on_creature_removed(&mon)` before erasing from structures. For NPCs: `game::unload_npcs()` (src/game_setup.cpp:1055) — call `on_creature_removed` per NPC. For the avatar: not removed during gameplay (lives for the session).
- **Map shift**: `PhysicsWorld::on_map_shifted()` already handles terrain/vehicle body translation. Extend it to translate creature bodies by the same delta. The `creature_bodies_` map keys are `Creature*` pointers (stable across shifts), so the map doesn't need re-keying — only the `b2Body` positions need translating.

#### 2c. Shape sizing

Map creature sizes to circle radii (physics meters, where 1 tile = `TILE_M`):

| `creature_size` | `ranged_target_size()` | Circle radius (tiles) | Circle radius (meters) |
|---|---|---|---|
| tiny | 0.1 | 0.05 | 0.05 × TILE_M |
| small | 0.25 | 0.125 | 0.125 × TILE_M |
| medium | 0.5 | 0.25 | 0.25 × TILE_M |
| large | 0.75 | 0.375 | 0.375 × TILE_M |
| huge | 1.0 | 0.5 | 0.5 × TILE_M |

Crouching and `MF_HARDTOSHOOT` already reduce `ranged_target_size()` by one bracket. Since the circle radius derives from that function, the Box2D shape automatically shrinks when the creature crouches — no special case needed. The shape is recreated on size change (crouch toggle) via `on_creature_moved` which should also call `b2DestroyShape`/`b2CreateCircleShape` if the size changed. Track the last-used radius in the `creature_bodies_` value (change it from bare `b2BodyId` to a struct: `struct creature_body { b2BodyId body; b2ShapeId shape; float radius; };`) so the moved hook can detect size changes cheaply.

### Step 3: Raycast-based projectile resolution (depends on Step 2)

Replace the per-tile creature/terrain hit checks in `projectile_attack` with a `b2World_CastRay` call. The DDA tile loop stays for animation, trail effects, and tile-based game effects — only the HIT DETECTION is replaced.

#### 3a. Architecture: hybrid DDA + raycast

The existing per-tile DDA loop (ballistics.cpp:481) does three things per tile: (A) animation/visual, (B) hit detection, (C) game effects (trails, sounds). Box2D replaces only (B).

New flow:
1. Before the DDA loop, cast the full ray via `b2World_CastRay()` with a callback that accumulates all body hits along the ray, sorted by fraction (distance from origin). Use a `b2QueryFilter` with `maskBits = terrain_category_bit(z) | vehicle_category_bit(z) | creature_category_bit(z)` to hit all body types.
2. The callback collects hits into a `std::vector<ray_hit>` sorted by fraction, where `ray_hit = { b2BodyId body, b2Vec2 point, b2Vec2 normal, float fraction, b2ShapeId shape }`.
3. The DDA loop iterates tiles as before for animation and trail effects, but instead of `creature_at(tp)` + `iso_tangent` + `map::shoot()` per tile, it pops hits from the sorted hit list as the DDA advances past each hit's fraction position.
4. For each popped hit, identify the body type via `b2Body_GetUserData`:
   - **Creature body**: Cast user data to `Creature*`, call `deal_projectile_attack()` with `missed_by` derived from the raycast hit fraction (a hit IS a hit — `missed_by = 0.0` or computed from the perpendicular distance between ray and body center, preserving the existing graze/dodge/headshot logic).
   - **Terrain/vehicle body**: Call `map::shoot()` on the tile at the hit point, preserving existing bash-table resolution.
5. After each hit, check `has_momentum` (proj.impact.total_damage() > 0). If momentum is exhausted, stop processing further hits and truncate the DDA loop.

#### 3b. `missed_by` computation from raycast

The existing `deal_projectile_attack` uses `missed_by` to determine graze vs center hit vs headshot (creature.cpp:945). With a Box2D raycast hit, the projectile geometrically hit the creature's shape — but we still need to compute HOW WELL it hit for graze/crit resolution.

Compute `missed_by` from the perpendicular distance between the ray line and the creature's body center, normalized by the creature's shape radius. This preserves the existing graze gradient:
- Ray passes through center: `missed_by ≈ 0.0` → headshot chance
- Ray grazes edge: `missed_by ≈ 0.9` → graze
- Ray misses shape: no hit (Box2D doesn't report it)

```cpp
auto center = b2Body_GetPosition( hit.body );
// Perpendicular distance from ray to body center
auto perp_dist = perpendicular_distance( ray_origin, ray_dir, center );
auto radius = b2Shape_GetCircle( hit.shape ).radius;
missed_by = std::clamp( static_cast<double>( perp_dist / radius ), 0.0, 0.99 );
```

This replaces `iso_tangent(range, dist) / ranged_target_size()` with an equivalent geometric computation using the actual shape.

#### 3c. Callback implementation

Define a free function matching `b2CastResultFcn` (Box2D v3.0.0 callback signature — verify exact typedef from box2d headers before implementing):

```cpp
struct raycast_context {
    std::vector<ray_hit> hits;
    const Creature* shooter;  // self-hit exclusion
};

auto projectile_raycast_callback( b2ShapeId shape, b2Vec2 point, b2Vec2 normal,
                                   float fraction, void* ctx ) -> float
{
    auto* rctx = static_cast<raycast_context*>( ctx );
    auto body = b2Shape_GetBody( shape );
    auto* udata = b2Body_GetUserData( body );

    // Self-hit exclusion
    if( udata == rctx->shooter ) { return 1.0f; } // continue ray

    rctx->hits.push_back( { body, point, normal, fraction, shape } );
    return 1.0f; // continue ray past this hit (we process penetration later)
}
```

Return `1.0f` to continue the ray to maximum range — penetration decisions happen in the game-logic loop after collecting all hits, not inside the callback. This avoids calling game logic from inside Box2D's internal state.

#### 3d. Integration into `projectile_attack`

In `projectile_attack` (ballistics.cpp:333), after trajectory generation (line 435-436) and before the DDA loop (line 481):

```cpp
#ifdef BOX2D_ENABLED
auto ray_hits = std::vector<ray_hit>{};
if( auto* pw = here.phys_world.get() ) {
    auto ctx = raycast_context{ .hits = {}, .shooter = origin };
    auto origin_m = b2Vec2{ source.x() * TILE_M, source.y() * TILE_M };
    auto target_m = b2Vec2{
        ( trajectory.back().x() - source.x() ) * TILE_M,
        ( trajectory.back().y() - source.y() ) * TILE_M };
    auto filter = b2QueryFilter{
        .categoryBits = physics::terrain_category_bit( source.z() )
                      | physics::vehicle_category_bit( source.z() )
                      | physics::creature_category_bit( source.z() ),
        .maskBits = UINT64_MAX };
    b2World_CastRay( pw->world_id(), origin_m, target_m, filter,
                     projectile_raycast_callback, &ctx );
    ray_hits = std::move( ctx.hits );
    std::ranges::sort( ray_hits, {}, &ray_hit::fraction );
}
#endif
```

Then modify the DDA loop body: when `BOX2D_ENABLED` and `ray_hits` is non-empty, skip the existing `creature_at` + `iso_tangent` + `map::shoot` checks and instead process `ray_hits` entries whose fraction corresponds to the current DDA tile range. When `BOX2D_ENABLED` is off, the existing per-tile logic runs unchanged.

Guard the entire new path with `#ifdef BOX2D_ENABLED` / `#else` / `#endif` around the hit-detection portion of the loop body (lines ~511-650), keeping animation (lines ~513-523) and trail effects outside the guard.

#### 3e. `b2QueryFilter` vs `b2Filter` semantics

Box2D v3.0.0 `b2World_CastRay` takes a `b2QueryFilter` (per-query filter) distinct from the per-shape `b2Filter`. Verify from box2d v3.0.0 headers whether `b2QueryFilter` uses `categoryBits`/`maskBits` with the same AND-logic as shape filters. If the semantics differ (e.g., `b2QueryFilter` uses its `maskBits` to match against shape `categoryBits`), adjust the filter construction in Step 3d accordingly. The implementer MUST read `box2d/include/box2d/box2d.h` and `types.h` to confirm the exact filter struct layout before coding — this is a v3.0.0-specific API that changed from v2.x.

### Step 4: Fallback for non-Box2D builds (implicit, no new code)

When `BOX2D_ENABLED` is off: `creature_bodies_` doesn't exist, no raycast is performed, the existing per-tile DDA hit resolution runs unchanged. All new code in Steps 1-3 is inside `#ifdef BOX2D_ENABLED` blocks. The `filter_bits.h` header compiles always but is only included by physics code.

## Critical files & anchors

| File | Symbol / region | Why |
|---|---|---|
| `src/ballistics.cpp:481-650` | `projectile_attack` per-tile loop | The hit-detection code this plan replaces; animation and trail-effect code in the same loop must NOT be disturbed |
| `src/physics/physics_world.h` | `PhysicsWorld` class | Add `creature_bodies_` registry and `on_creature_{added,moved,removed}` methods alongside existing vehicle/terrain equivalents |
| `src/creature_tracker.cpp:57,203` | `Creature_tracker::add/remove` | Lifecycle hook sites for creature body creation/destruction — existing pattern mirrors `on_vehicle_added/removed` |
| `src/creature.cpp:2740` | `Creature::setpos(tripoint_abs_ms)` | Single choke point for all creature position changes — hook site for `on_creature_moved` |
| `src/physics/terrain_body.cpp:51-52` | Filter bit expressions | One of two sites with raw `1ull << (z+N)` expressions that must migrate to `filter_bits.h` helpers |

## Verification

### Step 1 (Filter bit fix)
Build with `cmake --preset osx-coop` (Box2D ON). Run `cata_test-tiles "[vehicle][box2d]"` — all 38 assertions must pass, confirming vehicle-terrain collision is unaffected by the bit shift. Verify with a debug print that terrain z=10 and vehicle z=-10 now have distinct category bits.

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

Build WITHOUT Box2D (`cmake --preset osx-arm-slim`): verify compile succeeds and ranged combat behaves identically to pre-change (DDA tile-based resolution, no regression).

## Assumptions & contingencies

- **`b2CastResultFcn` callback signature**: The plan assumes the v3.0.0 callback returns `float` where `1.0f` continues the ray. If the actual typedef differs (e.g., returns `bool`), adjust `projectile_raycast_callback` to match. The implementer MUST verify from `box2d/include/box2d/box2d.h` before implementing Step 3c.
- **`b2QueryFilter` vs shape `b2Filter`**: The plan uses `b2QueryFilter.maskBits` to select which shape categories the ray tests. If v3.0.0's query filter semantics differ from shape filter semantics (e.g., reversed category/mask roles), swap the bit assignments in Step 3d. Verify from headers.
- **Phase 10 Step 6 / Phase 12 independence**: This plan does not depend on those phases. If a merge conflict arises in `map_vehicle.cpp` or `vehicle.h`, it will be in the vehicle-motion code, not the ranged-combat code — resolve by keeping both changes.
- **Creature shape recreation on size change**: Crouch toggle changes `ranged_target_size()`, which changes the circle radius. The plan tracks the last radius in `creature_body::radius` and recreates the shape in `on_creature_moved` only when the size changes. If crouch state doesn't trigger `setpos` (it shouldn't), add a separate hook in the crouch toggle path (grep `is_crouching` state changes) that calls `on_creature_moved`. If this proves too invasive, defer dynamic shape resizing — use the uncouched size always. The geometric difference is one bracket (e.g., medium→small), which is a 2x radius change — noticeable but not game-breaking if deferred.
- **Z-level raycasting**: Box2D is 2D. Cross-z projectile paths (shooting up/down through floors) cannot use Box2D raycasting. The existing DDA floor-check code (ballistics.cpp:491-509) handles z-transitions. When the trajectory crosses z-levels, fall back to the existing per-tile logic for those tiles. The Box2D raycast only operates within a single z-level.
