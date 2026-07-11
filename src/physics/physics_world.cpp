#ifdef BOX2D_ENABLED
#include "physics_world.h"
#include "terrain_body.h"
#include "vehicle_shape.h"  // vehicle_box2d_shape, TILE_M
#include "vehicle.h"        // vehicle, velo_vec, face_vec, bub_ms_location, total_mass
#include "map.h"            // map::abs_to_bub, impassable_ter_furn, is_bashable_ter_furn
#include "game_constants.h" // SEEX, SEEY
#include "units_mass.h"     // units::to_kilogram
#include <cmath>

namespace physics {

// ── Construction / destruction ────────────────────────────────────────────────

PhysicsWorld::PhysicsWorld()
{
    auto wdef    = b2DefaultWorldDef();
    wdef.gravity = { 0.0f, 0.0f };
    world_       = b2CreateWorld( &wdef );
}

PhysicsWorld::~PhysicsWorld()
{
    // Destroying the world frees all bodies and shapes atomically.
    if( B2_IS_NON_NULL( world_ ) ) { b2DestroyWorld( world_ ); }
}

// ── Private helpers ───────────────────────────────────────────────────────────

auto PhysicsWorld::make_vehicle_body( vehicle &v ) -> b2BodyId
{
    const auto bpos = v.bub_ms_location();
    const auto fv   = v.face_vec();   // normalized (cos θ, sin θ) in face direction

    auto bdef            = b2DefaultBodyDef();
    // Static body — teleported by on_vehicle_moved() each time the tile-step system moves
    // the vehicle.  Phase 6/7 read back position/angle after step() for physics_pos /
    // physics_angle.  Phase 10 full promotes to b2_dynamicBody so Box2D drives movement.
    bdef.type            = b2_staticBody;
    bdef.position        = { static_cast<float>( bpos.x() ) * TILE_M,
                              static_cast<float>( bpos.y() ) * TILE_M };
    bdef.rotation        = b2Rot{ static_cast<float>( fv.x ), static_cast<float>( fv.y ) };

    const auto bid = b2CreateBody( world_, &bdef );
    b2Body_SetUserData( bid, &v );

    const auto poly    = vehicle_box2d_shape( v );
    const auto mass_kg = units::to_kilogram( v.total_mass() );

    // Approximate body area for density (shape area in m²).
    // For a box poly vertices[0]=min-corner, vertices[2]=max-corner.
    const auto half_w = std::max( ( poly.vertices[2].x - poly.vertices[0].x ) * 0.5f, 0.01f );
    const auto half_h = std::max( ( poly.vertices[2].y - poly.vertices[0].y ) * 0.5f, 0.01f );

    auto sdef                = b2DefaultShapeDef();
    sdef.density             = mass_kg / ( 4.0f * half_w * half_h );
    sdef.friction            = 0.3f;
    sdef.restitution         = 0.5f;  // refined per-collision in transient worlds (Phase 2 / 5)
    sdef.enableContactEvents = true;
    sdef.enableHitEvents     = true;

    const auto z_bit = 1ull << static_cast<uint64_t>( bpos.z() + 10 );
    sdef.filter.categoryBits = z_bit;
    sdef.filter.maskBits     = z_bit;

    b2CreatePolygonShape( bid, &sdef, &poly );
    return bid;
}

void PhysicsWorld::rebuild_bashable_lookup()
{
    bashable_tile_bodies_.clear();
    for( const auto &[abs_sm, tile_list] : bashable_tiles_ ) {
        for( const auto &[bub, bid] : tile_list ) {
            bashable_tile_bodies_[bub] = bid;
        }
    }
}

// ── Vehicle lifecycle ─────────────────────────────────────────────────────────

void PhysicsWorld::on_vehicle_added( vehicle &v )
{
    vehicle_bodies_[&v] = make_vehicle_body( v );
}

void PhysicsWorld::on_vehicle_moved( vehicle &v )
{
    const auto it = vehicle_bodies_.find( &v );
    if( it == vehicle_bodies_.end() ) { return; }

    const auto bpos = v.bub_ms_location();
    const auto fv   = v.face_vec();
    b2Body_SetTransform(
        it->second,
        { static_cast<float>( bpos.x() ) * TILE_M, static_cast<float>( bpos.y() ) * TILE_M },
        b2Rot{ static_cast<float>( fv.x ), static_cast<float>( fv.y ) } );
}

void PhysicsWorld::on_vehicle_removed( vehicle *v )
{
    const auto it = vehicle_bodies_.find( v );
    if( it == vehicle_bodies_.end() ) { return; }
    b2DestroyBody( it->second );
    vehicle_bodies_.erase( it );
}

// ── Terrain lifecycle ─────────────────────────────────────────────────────────

void PhysicsWorld::on_submap_loaded( const map &m, const tripoint_abs_sm &abs_sm_pos )
{
    // Compute bub_ms origin of this submap's (0,0) local tile.
    const tripoint_abs_ms abs_corner{ abs_sm_pos.x() * SEEX,
                                       abs_sm_pos.y() * SEEY,
                                       abs_sm_pos.z() };
    const auto bub_origin = m.abs_to_bub( abs_corner );

    auto bodies = build_submap_terrain_bodies( world_, m, bub_origin );

    // Populate per-submap bashable tile list for shift-safe on_tile_bashed lookup.
    std::vector<std::pair<tripoint_bub_ms, b2BodyId>> tile_list;
    for( const auto bid : bodies ) {
        const auto ud = b2Body_GetUserData( bid );
        if( ud != nullptr ) {
            const auto bub = decode_tile_pos( reinterpret_cast<std::uintptr_t>( ud ) );
            tile_list.emplace_back( bub, bid );
            bashable_tile_bodies_[bub] = bid;
        }
    }
    if( !tile_list.empty() ) {
        bashable_tiles_[abs_sm_pos] = std::move( tile_list );
    }

    terrain_bodies_[abs_sm_pos] = std::move( bodies );
}

void PhysicsWorld::on_submap_unloaded( const tripoint_abs_sm &abs_sm_pos )
{
    // Remove bashable tile entries before destroying the bodies.
    const auto bash_it = bashable_tiles_.find( abs_sm_pos );
    if( bash_it != bashable_tiles_.end() ) {
        for( const auto &[bub, bid] : bash_it->second ) {
            bashable_tile_bodies_.erase( bub );
        }
        bashable_tiles_.erase( bash_it );
    }

    const auto it = terrain_bodies_.find( abs_sm_pos );
    if( it != terrain_bodies_.end() ) {
        for( const auto bid : it->second ) { b2DestroyBody( bid ); }
        terrain_bodies_.erase( it );
    }

    // Destroy vehicle bodies whose home submap is being unloaded.
    // map::on_submap_unloaded already erases these vehicles from loaded_vehicles;
    // without this sweep the b2BodyIds would leak in the Box2D world.
    std::erase_if( vehicle_bodies_, [&]( const auto &kv ) {
        if( kv.first->abs_sm_pos == abs_sm_pos ) {
            b2DestroyBody( kv.second );
            return true;
        }
        return false;
    } );
}

// ── Map shift ─────────────────────────────────────────────────────────────────

void PhysicsWorld::on_map_shifted( point delta_tiles )
{
    const b2Vec2 delta{ static_cast<float>( delta_tiles.x ) * TILE_M,
                         static_cast<float>( delta_tiles.y ) * TILE_M };

    // Translate vehicle bodies.
    for( const auto &[v, bid] : vehicle_bodies_ ) {
        const auto p   = b2Body_GetPosition( bid );
        const auto rot = b2Body_GetRotation( bid );
        b2Body_SetTransform( bid, { p.x + delta.x, p.y + delta.y }, rot );
    }

    // Translate terrain bodies.
    for( const auto &[abs_sm, body_list] : terrain_bodies_ ) {
        for( const auto bid : body_list ) {
            const auto p   = b2Body_GetPosition( bid );
            const auto rot = b2Body_GetRotation( bid );
            b2Body_SetTransform( bid, { p.x + delta.x, p.y + delta.y }, rot );
        }
    }

    // Update bashable tile bub_ms keys — they are bubble-relative and change on shift.
    for( auto &[abs_sm, tile_list] : bashable_tiles_ ) {
        for( auto &[bub, bid] : tile_list ) {
            bub = tripoint_bub_ms{ bub.x() + delta_tiles.x, bub.y() + delta_tiles.y, bub.z() };
        }
    }
    rebuild_bashable_lookup();
}

// ── Phase 5 hooks ─────────────────────────────────────────────────────────────

void PhysicsWorld::on_tile_bashed( tripoint_bub_ms pos )
{
    const auto it = bashable_tile_bodies_.find( pos );
    if( it == bashable_tile_bodies_.end() ) { return; }
    const auto bid = it->second;
    bashable_tile_bodies_.erase( it );

    // Remove from per-submap list (linear scan, bashable list is small per submap).
    for( auto &[abs_sm, tile_list] : bashable_tiles_ ) {
        const auto erase_it = std::ranges::find_if(
            tile_list, [bid]( const auto &p ) { return B2_ID_EQUALS( p.second, bid ); } );
        if( erase_it != tile_list.end() ) {
            tile_list.erase( erase_it );
            break;
        }
    }

    b2DestroyBody( bid );
}

// ── Game-loop interface ───────────────────────────────────────────────────────

void PhysicsWorld::step( float dt, int substeps )
{
    // Vehicle bodies are b2_staticBody (teleported by on_vehicle_moved).
    // No velocity-sync needed; position readback after step reflects the
    // most recent on_vehicle_moved teleport and initialises physics_pos /
    // physics_angle for Phase 10 full (b2_dynamicBody) migration.
    b2World_Step( world_, dt, substeps );

    // ── Phase 6: read back physics position + angle → vehicle fields ──────────
    for( auto &[veh, bid] : vehicle_bodies_ ) {
        const auto pos     = b2Body_GetPosition( bid );
        veh->physics_pos   = rl_vec2d{ pos.x / TILE_M, pos.y / TILE_M };
        const auto rot     = b2Body_GetRotation( bid );
        veh->physics_angle = std::atan2( rot.s, rot.c );
        // Phase 7: repopulate precalc[0] from continuous angle.
        veh->refresh_precalc( veh->physics_angle );
    }

    dispatch_contact_events();
}

void PhysicsWorld::dispatch_contact_events()
{
    // Phase 10 (kinematic transitional):
    //
    // Box2D v3.0.0 contact events (begin/end/hit) are only generated when at
    // least one body in the pair is b2_dynamicBody.  With vehicles as
    // b2_kinematicBody, ALL contact event counts are zero — kinematic-static
    // and kinematic-kinematic pairs produce no events.
    //
    // Angular spin for terrain impacts is already computed by
    // resolve_terrain_impulse() (Phase 5) called from vehicle::part_collision(),
    // so no additional dispatch is needed for Phase 10 kinematic.
    //
    // TODO Phase 10 full (vehicles promoted to b2_dynamicBody):
    //   iterate hitEvents; identify struck vehicle via
    //   vehicle_bodies_.count(static_cast<vehicle*>(b2Body_GetUserData(bid)));
    //   apply angular_velocity_rads = b2Body_GetAngularVelocity(bid).
}

// ── Query access ──────────────────────────────────────────────────────────────

auto PhysicsWorld::world_id() const -> b2WorldId
{
    return world_;
}

// ── Phase 5: transient terrain-impulse solve ──────────────────────────────────

auto PhysicsWorld::resolve_terrain_impulse( vehicle        &v,
                                             tripoint_bub_ms tile_pos,
                                             float           tile_mass_kg,
                                             float           restitution ) -> terrain_impulse_result
{
    ( void )tile_mass_kg;  // terrain tile is b2_staticBody; mass not needed

    // ── 1. Temporary world (zero gravity, isolated from persistent world_) ────
    auto wdef    = b2DefaultWorldDef();
    wdef.gravity = { 0.0f, 0.0f };
    const auto tmp_world = b2CreateWorld( &wdef );

    // ── 2. Vehicle as b2_dynamicBody at origin ────────────────────────────────
    const auto fv     = v.face_vec();   // (cos θ, sin θ)
    const auto vel    = v.velo_vec();   // cm/s

    auto vbdef             = b2DefaultBodyDef();
    vbdef.type             = b2_dynamicBody;
    vbdef.position         = { 0.0f, 0.0f };
    vbdef.rotation         = b2Rot{ static_cast<float>( fv.x ), static_cast<float>( fv.y ) };
    vbdef.linearVelocity   = { vel.x / 100.0f, vel.y / 100.0f };
    vbdef.angularVelocity  = v.angular_velocity_rads;

    const auto vbody = b2CreateBody( tmp_world, &vbdef );
    b2Body_SetUserData( vbody, &v );

    const auto poly    = vehicle_box2d_shape( v );
    const auto mass_kg = units::to_kilogram( v.total_mass() );

    // Approximate body area for density (same pattern as make_vehicle_body()).
    const auto half_w = std::max( ( poly.vertices[2].x - poly.vertices[0].x ) * 0.5f, 0.01f );
    const auto half_h = std::max( ( poly.vertices[2].y - poly.vertices[0].y ) * 0.5f, 0.01f );

    auto vsdef       = b2DefaultShapeDef();
    vsdef.density    = mass_kg / ( 4.0f * half_w * half_h );
    vsdef.restitution = restitution;
    vsdef.friction   = 0.3f;
    b2CreatePolygonShape( vbody, &vsdef, &poly );

    // ── 3. Terrain tile as b2_staticBody relative to vehicle origin ───────────
    const auto vpos = v.bub_ms_location();
    const auto tx   = static_cast<float>( tile_pos.x() - vpos.x() ) * TILE_M;
    const auto ty   = static_cast<float>( tile_pos.y() - vpos.y() ) * TILE_M;

    auto tbdef      = b2DefaultBodyDef();
    tbdef.type      = b2_staticBody;
    tbdef.position  = { tx, ty };

    const auto tbody = b2CreateBody( tmp_world, &tbdef );

    const auto tile_poly = b2MakeBox( TILE_M * 0.5f, TILE_M * 0.5f );
    auto tsdef           = b2DefaultShapeDef();
    tsdef.density        = 0.0f;
    tsdef.restitution    = restitution;
    tsdef.friction       = 0.5f;
    b2CreatePolygonShape( tbody, &tsdef, &tile_poly );

    // ── 4. Single physics step ─────────────────────────────────────────────────
    b2World_Step( tmp_world, 1.0f / 60.0f, 8 );

    // ── 5. Read back resolved velocities ──────────────────────────────────────
    const auto nv = b2Body_GetLinearVelocity( vbody );
    const auto nw = b2Body_GetAngularVelocity( vbody );

    // ── 6. Destroy temporary world ────────────────────────────────────────────
    b2DestroyWorld( tmp_world );

    // ── 7. Return result (m/s → cm/s for linear velocity) ─────────────────────
    return terrain_impulse_result{
        rl_vec2d{ nv.x * 100.0f, nv.y * 100.0f },
        nw
    };
}

} // namespace physics
#endif // BOX2D_ENABLED
