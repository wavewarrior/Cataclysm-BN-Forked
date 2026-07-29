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
#include <algorithm>
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

    // Physics-driven move (vehmove()'s readback): the body already holds the
    // authoritative sub-tile position and the tile anchor was just snapped to
    // match it.  Writing the body here would slam it to the new tile's centre
    // every turn, destroying sub-tile integration and creating a staircase
    // feedback loop.
    if( applying_readback_ ) { return; }

    // Anything else moved the vehicle (debug teleport, tow, test reset, rail
    // shift, z-change).  The body MUST follow, and physics_pos must be reseated
    // with it: leaving physics_pos stale makes the next readback compute a
    // bogus delta from a position the vehicle no longer occupies.  That was the
    // defect which let vehicle_efficiency_test's per-turn "teleport back to
    // start" measure a reset-vs-stale-physics_pos artifact instead of the real
    // per-turn distance, masking the movement-rate bug entirely.
    const auto bpos = v.bub_ms_location();
    const auto fv   = v.face_vec();
    if( v.box2d_position_authority ) {
        v.physics_pos = rl_vec2d{ static_cast<float>( bpos.x() ),
                                  static_cast<float>( bpos.y() ) };
    }
    b2Body_SetTransform(
        it->second,
        { static_cast<float>( bpos.x() ) * TILE_M, static_cast<float>( bpos.y() ) * TILE_M },
        b2Rot{ static_cast<float>( fv.x ), static_cast<float>( fv.y ) } );
}

void PhysicsWorld::clamp_body_to_tile( vehicle &v )
{
    const auto it = vehicle_bodies_.find( &v );
    if( it == vehicle_bodies_.end() ) { return; }
    // Deliberately ignores box2d_position_authority: the caller has determined
    // the body integrated somewhere the tile grid cannot follow (unloaded map),
    // so the tile anchor is authoritative for this one write.
    const auto bpos = v.bub_ms_location();
    const auto fv   = v.face_vec();
    b2Body_SetTransform(
        it->second,
        { static_cast<float>( bpos.x() ) * TILE_M, static_cast<float>( bpos.y() ) * TILE_M },
        b2Rot{ static_cast<float>( fv.x ), static_cast<float>( fv.y ) } );
    b2Body_SetLinearVelocity( it->second, b2Vec2{ 0.0f, 0.0f } );
    b2Body_SetAngularVelocity( it->second, 0.0f );
}

void PhysicsWorld::on_vehicle_removed( vehicle *v )
{
    authority_revoked_by_unload_.erase( v );
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

    // Idempotency guard.  terrain_bodies_[key] below is a plain assignment, so a
    // second call for a key that already holds bodies would drop those b2BodyIds
    // without destroying them — leaving live duplicate colliders stacked on the
    // same tiles and leaking them for the lifetime of the world.  Previously
    // unreachable because on_zlevel_changed always ran its removal loop first, but
    // that is a fragile invariant now that other callers exist (e.g. the
    // destination-z rebuild in game::place_player_overmap).
    //
    // The bashable_tile_bodies_ keys come from bashable_tiles_, NOT from decoding
    // each body's userData: on_map_shifted() re-bases the bub keys stored in
    // bashable_tiles_ but does not rewrite the bodies' encoded userData, so after
    // any shift decode_tile_pos() yields the pre-shift tile.  Erasing by that would
    // remove the wrong entry and leave the real one holding a destroyed b2BodyId,
    // which the next on_tile_bashed() at that tile would try to destroy again.
    if( const auto prev = terrain_bodies_.find( abs_sm_pos ); prev != terrain_bodies_.end() ) {
        if( const auto tl = bashable_tiles_.find( abs_sm_pos ); tl != bashable_tiles_.end() ) {
            for( const auto &[bub, bid] : tl->second ) {
                if( const auto e = bashable_tile_bodies_.find( bub );
                    e != bashable_tile_bodies_.end() && B2_ID_EQUALS( e->second, bid ) ) {
                    bashable_tile_bodies_.erase( e );
                }
            }
            bashable_tiles_.erase( tl );
        }
        for( const auto bid : prev->second ) {
            b2DestroyBody( bid );
        }
        terrain_bodies_.erase( prev );
    }


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

    // Re-grant position authority to vehicles whose home submap just re-entered
    // the simulated set, undoing the revocation on on_submap_unloaded's
    // still-resident path.  Without this, each bubble excursion would permanently
    // convert more vehicles to the tile-step mover and Box2D would decay away
    // over a long session.
    //
    // physics_pos has to be reseated to the current anchor first: while the
    // tile-step mover owned the vehicle that field went stale (on_vehicle_moved()
    // only resyncs it under authority), and handing authority back with a stale
    // value would make the very next readback walk the vehicle back to wherever
    // it was when authority was revoked.
    for( auto &[veh, bid] : vehicle_bodies_ ) {
        if( veh->abs_sm_pos != abs_sm_pos ) { continue; }
        // Only re-grant what this class revoked.  A vehicle that opted out itself
        // must stay opted out, and the flag alone cannot tell the two apart.
        if( authority_revoked_by_unload_.erase( veh ) == 0 ) { continue; }
        const auto bpos  = veh->bub_ms_location();
        veh->physics_pos = rl_vec2d{ static_cast<float>( bpos.x() ),
                                     static_cast<float>( bpos.y() ) };
        veh->box2d_position_authority = true;
    }
}

void PhysicsWorld::on_submap_unloaded( const tripoint_abs_sm &abs_sm_pos,
                                       bool submap_still_resident )
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

    // Vehicle bodies for the submap leaving the simulated set.
    //
    // This callback tracks *simulation* membership, not memory residency (see
    // src/submap_load_manager.h), so it fires on simulated -> lazy_border while
    // the submap and every vehicle in it are still alive.  The two cases need
    // different handling.
    //
    // Still resident: keep the body, but revoke authority.  map::on_submap_unloaded
    // has already dropped these vehicles from loaded_vehicles, so nothing walks
    // their tile anchor any more — left under authority they would keep being
    // integrated by step_turn() while sync_game_from_bodies() rewrote their facing
    // and precalc[] every turn, and physics_pos would drift far from the anchor
    // until re-entering the simulated set teleported them.  Revoking hands them
    // back to the tile-step mover, which the authority guards in
    // sync_game_from_bodies() and the vehmove() readback both honour, while
    // on_vehicle_moved() keeps the body tracking the anchor.  The body is kept
    // because nothing ever re-registers one (on_vehicle_added has exactly one
    // caller, map::add_vehicle), so destroying it here would be permanent.
    if( submap_still_resident ) {
        for( auto &[veh, bid] : vehicle_bodies_ ) {
            if( veh->abs_sm_pos != abs_sm_pos ) { continue; }
            // Only record vehicles that actually held authority.  Recording an
            // already-opted-out vehicle would make the re-grant hand it authority
            // when the submap returns, which is precisely what the set exists to
            // prevent.
            if( !veh->box2d_position_authority ) { continue; }
            authority_revoked_by_unload_.insert( veh );
            veh->box2d_position_authority = false;
            veh->render_offset_x = 0.f;
            veh->render_offset_y = 0.f;
        }
        return;
    }

    // Genuinely gone: destroy the body and clear authority, mirroring
    // on_vehicle_removed().  Leaving the flag set would strand the vehicle with
    // authority but no body, and the readback would keep driving it from a
    // physics_pos that can never be updated again.
    std::erase_if( vehicle_bodies_, [&]( const auto &kv ) {
        if( kv.first->abs_sm_pos == abs_sm_pos ) {
            // Drop any revocation record too — the vehicle is going away, and a
            // stale entry here would be a dangling pointer.
            authority_revoked_by_unload_.erase( kv.first );
            kv.first->box2d_position_authority = false;
            kv.first->render_offset_x = 0.f;
            kv.first->render_offset_y = 0.f;
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

    // Translate terrain bodies, and re-encode their userData tile alongside.
    //
    // The userData is an encoded bub_ms tile, and bub coordinates are
    // bubble-relative, so a shift invalidates it just as it invalidates the
    // transform.  Updating only the transform left every terrain body advertising
    // its pre-shift tile, which any consumer that identifies a tile by userData
    // would then get wrong — including the contact-event routing that still has to
    // be written, where it would bash the wrong tile once the bubble had moved.
    for( const auto &[abs_sm, body_list] : terrain_bodies_ ) {
        for( const auto bid : body_list ) {
            const auto p   = b2Body_GetPosition( bid );
            const auto rot = b2Body_GetRotation( bid );
            b2Body_SetTransform( bid, { p.x + delta.x, p.y + delta.y }, rot );
            if( auto *ud = b2Body_GetUserData( bid ); ud != nullptr ) {
                const auto old_bub = decode_tile_pos( reinterpret_cast<std::uintptr_t>( ud ) );
                const auto new_bub = tripoint_bub_ms{ old_bub.x() + delta_tiles.x,
                                                      old_bub.y() + delta_tiles.y,
                                                      old_bub.z() };
                b2Body_SetUserData( bid, reinterpret_cast<void *>( encode_tile_pos( new_bub ) ) );
            }
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

auto PhysicsWorld::terrain_body_count() const -> size_t
{
    size_t n = 0;
    for( const auto &[abs_sm, body_list] : terrain_bodies_ ) {
        n += body_list.size();
    }
    return n;
}

auto PhysicsWorld::world_body_count() const -> size_t
{
    // Counts bodies as Box2D sees them, which is deliberately NOT the same as
    // terrain_body_count(): terrain_bodies_[key] is an assignment, so a body that
    // was dropped from the registry without b2DestroyBody is invisible to that
    // count while still colliding in the world.  Leak-style bugs only show up here.
    return static_cast<size_t>( b2World_GetCounters( world_ ).bodyCount );
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
        // Still resident: only the z-level focus changed, so these submaps and any
        // vehicles in them are alive.  Passing false would strip the body of every
        // vehicle on the old z-level, permanently, on each z change.
        on_submap_unloaded( abs_sm, /*submap_still_resident=*/true );
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

void PhysicsWorld::sync_bodies_from_game()
{
    // Push the game's velocity model into the bodies.  Done once per turn, not
    // per sub-step: a per-sub-step re-sync would overwrite whatever the contact
    // solver produced, so collisions could never actually change velocity.
    for( auto &[veh, bid] : vehicle_bodies_ ) {
        const auto fv      = veh->face_vec();
        const auto spd_mps = static_cast<float>( veh->velocity ) / 100.0f;
        b2Body_SetLinearVelocity( bid, { static_cast<float>( fv.x ) * spd_mps,
                                         static_cast<float>( fv.y ) * spd_mps } );
        b2Body_SetAngularVelocity( bid, veh->angular_velocity_rads );
        // Continuous collision for anything fast enough to cross a whole tile
        // within one turn; without this a fast body can still skip a 1-tile
        // static terrain body between sub-steps.
        b2Body_SetBullet( bid, std::abs( veh->velocity ) > 2000 );
    }
}

void PhysicsWorld::sync_game_from_bodies()
{
    for( auto &[veh, bid] : vehicle_bodies_ ) {
        // Angular velocity is a genuine round-trip for every registered vehicle:
        // sync_bodies_from_game() pushes veh->angular_velocity_rads into the body
        // each turn, and Box2D's damping is what decays it.  Reading it back
        // unconditionally keeps that loop closed — gating it would leave the
        // field write-only, so a legacy vehicle spun up by part_collision() or
        // solve_vv_cluster() would keep that spin forever.
        veh->angular_velocity_rads = b2Body_GetAngularVelocity( bid );

        // Position, facing and the precalc[] layout derived from facing are only
        // taken from the body for vehicles under physics authority.  For the rest
        // the tile-step mover owns them and the body is a passive mirror kept in
        // step by on_vehicle_moved(); re-deriving precalc[] from a body the game
        // is not driving corrupts that vehicle's part layout.
        //
        // This guard matters because opting out is a real, used pattern: e.g.
        // tests/vehicle_ramp_test.cpp clears box2d_position_authority for every
        // vehicle it sets up.  Without the guard those vehicles kept their tile
        // position but had their facing overwritten from Box2D anyway, which is
        // why toggling authority globally changed that suite's results even
        // though it had already opted out.
        if( !veh->box2d_position_authority ) { continue; }
        const auto pos     = b2Body_GetPosition( bid );
        veh->physics_pos   = rl_vec2d{ pos.x / TILE_M, pos.y / TILE_M };
        const auto rot     = b2Body_GetRotation( bid );
        veh->physics_angle = std::atan2( rot.s, rot.c );
        veh->refresh_precalc( veh->physics_angle );

        // Keep `move` (the travel-direction tileray) in step with reality.
        //
        // Under authority act_on_map() returns early and never reaches
        // `veh.move = facing` in map::move_vehicle(), so `move` kept whatever
        // heading it was constructed with — 0 degrees — while `face` tracked the
        // vehicle.  vehicle::slowdown() then multiplies rolling drag by
        // `1 + 24 * |sin( face.dir() - move.dir() )|`, so a vehicle driving due
        // west (face 270, move 0) was charged the FULL 25x skid penalty every
        // turn while going perfectly straight.  Measured on car_test at cruise:
        // slowdown 362 cm/s per turn against 67 on the tile-step path, which held
        // the vehicle 362 cm/s below its cruise target permanently and made
        // cruise-control thrust burn load 533 instead of 114 — about 7x the fuel
        // over a run.
        //
        // Derive it from the body's linear velocity rather than from `face`: that
        // is the actual travel vector, so a genuine sideways slide still registers
        // as a skid.  Below a small speed the direction is numerical noise, so
        // fall back to `face` — otherwise a stationary or just-starting vehicle
        // would get a random heading and a random drag penalty.
        const auto lv = b2Body_GetLinearVelocity( bid );
        constexpr auto min_dir_mps = 0.05f;
        if( std::hypot( lv.x, lv.y ) >= min_dir_mps ) {
            veh->move.init( units::atan2( lv.y, lv.x ) );
        } else {
            veh->move = veh->face;
        }
    }
}

auto PhysicsWorld::substeps_for_turn( float turn_seconds ) const -> int
{
    // Keep per-step translation under half a tile so a body cannot pass through
    // a 1-tile-wide static terrain body between steps.
    constexpr auto max_tiles_per_step = 0.5f;
    auto max_mps = 0.0f;
    for( const auto &[veh, bid] : vehicle_bodies_ ) {
        max_mps = std::max( max_mps, std::abs( static_cast<float>( veh->velocity ) ) / 100.0f );
    }
    if( max_mps <= 0.0f ) { return 1; }
    const auto travel_m   = max_mps * turn_seconds;
    const auto step_limit = max_tiles_per_step * TILE_M;
    return std::clamp( static_cast<int>( std::ceil( travel_m / step_limit ) ), 1, 240 );
}

void PhysicsWorld::step_turn( float turn_seconds )
{
    if( turn_seconds <= 0.0f ) { return; }
    sync_bodies_from_game();
    const auto steps = substeps_for_turn( turn_seconds );
    const auto dt    = turn_seconds / static_cast<float>( steps );
    for( int i = 0; i < steps; ++i ) {
        b2World_Step( world_, dt, 4 );
        dispatch_contact_events();
    }
    sync_game_from_bodies();
}

void PhysicsWorld::step( float dt, int substeps )
{
    sync_bodies_from_game();
    b2World_Step( world_, dt, substeps );
    sync_game_from_bodies();
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
