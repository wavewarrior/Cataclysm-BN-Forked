#ifdef BOX2D_ENABLED
#include "physics_world.h"
#include "terrain_body.h"
#include "filter_bits.h"
#include "vehicle_shape.h"  // vehicle_box2d_shape, TILE_M
#include "vehicle.h"        // vehicle, velo_vec, face_vec, bub_ms_location, total_mass
#include "creature.h"        // Creature::get_size, bub_pos
#include "map.h"            // map::abs_to_bub, impassable_ter_furn, is_bashable_ter_furn
#include "game_constants.h" // SEEX, SEEY
#include "units_mass.h"     // units::to_kilogram
#include <cmath>
#include "physics_debug_draw.h"

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
    // Dynamic body — gravity disabled for top-down game plane.  Tile-step still teleports
    // bodies via on_vehicle_moved() until Phase 10 Step 5 (tile-step retirement).  Dynamic
    // type is required so Box2D v3 fires contact events (kinematic and static pairs produce
    // zero hit events regardless of filter settings).
    bdef.type            = b2_dynamicBody;
    bdef.gravityScale    = 0.0f;
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

    // Single z-bit shared by all body types; groupIndex separates categories.
    const auto z_bit = physics::z_category_bit( bpos.z() );
    sdef.filter.categoryBits = z_bit;
    sdef.filter.maskBits     = z_bit;
    sdef.filter.groupIndex   = physics::vehicle_group;

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
    // Initialise physics_pos from the tile position so the first vehmove() readback
    // is a no-op rather than teleporting the vehicle to (0,0).
    const auto bpos = v.bub_ms_location();
    v.physics_pos   = rl_vec2d{ static_cast<float>( bpos.x() ),
                                 static_cast<float>( bpos.y() ) };
    // Mark the vehicle as Box2D-controlled: act_on_map() will skip move_vehicle()
    // and map::vehmove() will apply physics_pos to the tile grid.
    v.box2d_position_authority = true;
}

void PhysicsWorld::on_vehicle_moved( vehicle &v )
{
    const auto it = vehicle_bodies_.find( &v );
    if( it == vehicle_bodies_.end() ) { return; }
    // Box2D owns position for this vehicle: the readback in vehmove() moves the tile
    // anchor to match the body, NOT the other way around.  Teleporting the body here
    // would snap it back to integer-tile-centre metres every tick, defeating sub-tile
    // integration and creating a staircase feedback loop.
    if( v.box2d_position_authority ) { return; }


    const auto bpos = v.bub_ms_location();
    const auto fv   = v.face_vec();
    b2Body_SetTransform(
        it->second,
        { static_cast<float>( bpos.x() ) * TILE_M, static_cast<float>( bpos.y() ) * TILE_M },
        b2Rot{ static_cast<float>( fv.x ), static_cast<float>( fv.y ) } );
}

void PhysicsWorld::on_vehicle_removed( vehicle *v )
{
    v->box2d_position_authority = false;  // always clear, even if body is missing
    v->render_offset_x = 0.f;
    v->render_offset_y = 0.f;
    const auto it = vehicle_bodies_.find( v );
    if( it == vehicle_bodies_.end() ) { return; }
    b2DestroyBody( it->second );
    vehicle_bodies_.erase( it );
}

// ── Creature lifecycle (Phase 11) ─────────────────────────────────────────────

void PhysicsWorld::on_creature_added( const Creature &c )
{
    if( creature_bodies_.contains( &c ) ) { return; }

    const auto bub = c.bub_pos();
    const auto cx = static_cast<float>( bub.x() ) * TILE_M;
    const auto cy = static_cast<float>( bub.y() ) * TILE_M;

    auto bdef     = b2DefaultBodyDef();
    bdef.type     = b2_kinematicBody;
    bdef.position = { cx, cy };
    const auto bid = b2CreateBody( world_, &bdef );
    b2Body_SetUserData( bid, const_cast<Creature *>( &c ) );

    const auto radius = static_cast<float>( c.effective_target_size() ) * TILE_M * 0.5f;
    const auto circle = b2Circle{ { 0.f, 0.f }, radius };

    auto sdef                  = b2DefaultShapeDef();
    sdef.isSensor              = true;
    sdef.enableContactEvents   = false;
    sdef.enableSensorEvents    = false;
    sdef.filter.categoryBits = z_category_bit( bub.z() );
    sdef.filter.maskBits     = z_category_bit( bub.z() );
    sdef.filter.groupIndex   = creature_group;
    const auto sid = b2CreateCircleShape( bid, &sdef, &circle );

    creature_bodies_.emplace( &c, creature_body{ bid, sid, radius } );
}

void PhysicsWorld::on_creature_moved( const Creature &c )
{
    const auto it = creature_bodies_.find( &c );
    if( it == creature_bodies_.end() ) {
        on_creature_added( c );
        return;
    }

    auto &cb = it->second;
    const auto bub = c.bub_pos();
    const auto cx = static_cast<float>( bub.x() ) * TILE_M;
    const auto cy = static_cast<float>( bub.y() ) * TILE_M;
    b2Body_SetTransform( cb.body, { cx, cy }, b2Rot_identity );

    // Recreate shape if the creature's target size changed (crouch, MF_HARDTOSHOOT).
    const auto new_radius = static_cast<float>( c.effective_target_size() ) * TILE_M * 0.5f;
    if( std::abs( new_radius - cb.radius ) > 0.001f ) {
        b2DestroyShape( cb.shape );
        const auto circle = b2Circle{ { 0.f, 0.f }, new_radius };
        auto sdef                  = b2DefaultShapeDef();
        sdef.isSensor              = true;
        sdef.enableContactEvents   = false;
        sdef.enableSensorEvents    = false;
        sdef.filter.categoryBits = z_category_bit( bub.z() );
        sdef.filter.maskBits     = z_category_bit( bub.z() );
        sdef.filter.groupIndex   = creature_group;
        cb.shape  = b2CreateCircleShape( cb.body, &sdef, &circle );
        cb.radius = new_radius;
    }

    // Update filter bits if z changed.
    auto filter = b2Shape_GetFilter( cb.shape );
    const auto z_bit = z_category_bit( bub.z() );
    if( filter.categoryBits != z_bit ) {
        filter.categoryBits = z_bit;
        filter.maskBits     = z_bit;
        b2Shape_SetFilter( cb.shape, filter );
    }
}

void PhysicsWorld::on_creature_removed( const Creature *c )
{
    const auto it = creature_bodies_.find( c );
    if( it == creature_bodies_.end() ) { return; }
    b2DestroyBody( it->second.body );
    creature_bodies_.erase( it );
}

void PhysicsWorld::clear_creature_bodies()
{
    for( const auto &[c, cb] : creature_bodies_ ) {
        b2DestroyBody( cb.body );
    }
    creature_bodies_.clear();
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

    // Translate creature bodies.
    for( const auto &[c, cb] : creature_bodies_ ) {
        const auto p   = b2Body_GetPosition( cb.body );
        const auto rot = b2Body_GetRotation( cb.body );
        b2Body_SetTransform( cb.body, { p.x + delta.x, p.y + delta.y }, rot );
    }

    // Update bashable tile bub_ms keys — they are bubble-relative and change on shift.
    for( auto &[abs_sm, tile_list] : bashable_tiles_ ) {
        for( auto &[bub, bid] : tile_list ) {
            bub = tripoint_bub_ms{ bub.x() + delta_tiles.x, bub.y() + delta_tiles.y, bub.z() };
        }
    }
    rebuild_bashable_lookup();
}

void PhysicsWorld::on_zlevel_changed( const map &m, int old_z, int new_z )
{
    if( old_z == new_z ) { return; }

    // Destroy terrain bodies from the old z-level.
    std::vector<tripoint_abs_sm> to_remove;
    for( const auto &[abs_sm, body_list] : terrain_bodies_ ) {
        if( abs_sm.z() == old_z ) {
            to_remove.push_back( abs_sm );
        }
    }
    for( const auto &abs_sm : to_remove ) {
        on_submap_unloaded( abs_sm );
    }

    // Create terrain bodies for the new z-level (all in-bubble submaps).
    const auto &origin = m.get_abs_sub();
    for( int gx = 0; gx < m.getmapsize(); ++gx ) {
        for( int gy = 0; gy < m.getmapsize(); ++gy ) {
            const tripoint_abs_sm abs_sm{ origin.x() + gx, origin.y() + gy, new_z };
            on_submap_loaded( m, abs_sm );
        }
    }
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
    // ── Pre-step: sync game velocity into Box2D bodies so contact impulses are
    // physically meaningful.  Tile-step still owns position (on_vehicle_moved()
    // teleports bodies via b2Body_SetTransform after each tile move).  Direct
    // velocity set is used here; force-driven movement is wired in Step 5 when
    // Box2D becomes the position authority.
    for( auto &[veh, bid] : vehicle_bodies_ ) {
        const auto fv      = veh->face_vec();
        const auto spd_mps = static_cast<float>( veh->velocity ) / 100.0f;
        b2Body_SetLinearVelocity( bid, { static_cast<float>( fv.x ) * spd_mps,
                                         static_cast<float>( fv.y ) * spd_mps } );
        b2Body_SetAngularVelocity( bid, veh->angular_velocity_rads );
    }

    b2World_Step( world_, dt, substeps );

    // ── Post-step: read back physics state → vehicle fields ──────────────────
    for( auto &[veh, bid] : vehicle_bodies_ ) {
        const auto pos             = b2Body_GetPosition( bid );
        veh->physics_pos           = rl_vec2d{ pos.x / TILE_M, pos.y / TILE_M };
        const auto rot             = b2Body_GetRotation( bid );
        veh->physics_angle         = std::atan2( rot.s, rot.c );
        veh->angular_velocity_rads = b2Body_GetAngularVelocity( bid );
        veh->refresh_precalc( veh->physics_angle );
    }

    dispatch_contact_events();
}

void PhysicsWorld::dispatch_contact_events()
{
    // Responsibility split:
    //   post-step loop (step() above) — writes angular_velocity_rads for ALL vehicles
    //     unconditionally after every b2World_Step(); this is the authoritative writer.
    //   dispatch_contact_events() — reserved for per-contact game routing that the
    //     post-step loop cannot do: bash-tile destruction, VT damage dispatch, sound
    //     emission, and sustained-contact effects.  Angular velocity is NOT written here
    //     to avoid redundancy with the post-step loop.
    //
    // Hit events are NOT used here: Box2D v3.0.0 hitCount is unreliable for
    // dynamic–static pairs unless approach speed clearly exceeds hitEventThreshold.
    // Begin-contact events (beginCount) fire on the first frame of every new contact
    // regardless of speed and are confirmed reliable for dynamic–static pairs (Test 5).
    //
    // TODO Phase 10 Step 5 (tile-step retirement):
    //   For each begin-contact event that involves a terrain body (identified by
    //   vehicle_bodies_.count(ptr) == 0), dispatch to the bash/damage system:
    //     const auto bub = decode_tile_pos(reinterpret_cast<uintptr_t>(b2Body_GetUserData(bid)));
    //     on_tile_bashed(bub);  // already implemented; call here instead of from vehicle_move.cpp
    //   For VT hits on vehicle shapes, route sound emission.

    const auto events = b2World_GetContactEvents( world_ );
    for( int i = 0; i < events.beginCount; ++i ) {
        const auto &ev = events.beginEvents[i];
        const auto check = []( b2ShapeId sid ) {
            if( b2Shape_IsSensor( sid ) ) { return; } // creature sensors — not vehicle pointers
            const auto bid = b2Shape_GetBody( sid );
            auto *ptr      = static_cast<vehicle *>( b2Body_GetUserData( bid ) );
            // TODO Phase 10 Step 5: vehicle_bodies_.count(ptr) → route bash/damage/sound
            ( void )ptr;
            ( void )bid;
        };
        check( ev.shapeIdA );
        check( ev.shapeIdB );
    }
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


// ── Debug overlay ─────────────────────────────────────────────────────────────

auto PhysicsWorld::toggle_debug_draw() -> bool
{
    debug_draw_ = !debug_draw_;
    return debug_draw_;
}

auto PhysicsWorld::draw_debug( lighting::debug_line_pass &pass ) const -> void
{
    if( !debug_draw_ || B2_IS_NULL( world_ ) ) { return; }
    auto ctx = DebugDrawContext{ &pass, 1.0f / TILE_M };
    auto dd  = make_debug_draw( &ctx );
    b2World_Draw( world_, &dd );
}
} // namespace physics
#endif // BOX2D_ENABLED
