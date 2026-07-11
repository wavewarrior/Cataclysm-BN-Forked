#ifdef BOX2D_ENABLED
#include "veh_box2d_solve.h"
#include "vehicle_shape.h"
#include "units_mass.h"

#include <box2d/box2d.h>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

auto solve_vv_cluster( vehicle &veh,
                       const std::map<vehicle *, std::vector<veh_collision>> &veh_collisions )
    -> vv_cluster_result
{
    auto veh_list = std::vector<vehicle *>{};
    veh_list.push_back( &veh );
    for( const auto &[target, ignored] : veh_collisions ) {
        veh_list.push_back( target );
    }

    auto wdef    = b2DefaultWorldDef();
    wdef.gravity = { 0.0f, 0.0f };
    auto world   = b2CreateWorld( &wdef );

    // Origin: use the active vehicle's bubble-map position as the local coordinate origin.
    // bub_ms_location() defined in vehicle.cpp:3811 as get_map().abs_to_bub(abs_ms_location())
    const auto origin = veh.bub_ms_location();

    struct BodyData {
        b2BodyId id;
        b2Vec2   old_vel_ms;  // velocity at body creation, m/s
        float    mass_kg;
    };
    auto body_map = std::unordered_map<vehicle *, BodyData>{};
    body_map.reserve( veh_list.size() );

    for( auto *v : veh_list ) {
        // tile_off is tripoint_rel_ms; .x() and .y() are int tile offsets.
        const auto tile_off = v->bub_ms_location() - origin;

        auto bdef      = b2DefaultBodyDef();
        bdef.type      = b2_dynamicBody;
        bdef.position  = { static_cast<float>( tile_off.x() ) * TILE_M,
                           static_cast<float>( tile_off.y() ) * TILE_M };

        // face_vec() returns angle_to_vec(face.dir()) where face.dir() is 15°-quantised.
        // Acceptable for Phase 2 initial body state; Phase 10 will use Box2D angle directly.
        // face_vec() sig: rl_vec2d vehicle::face_vec() const  (vehicle_move.cpp:1385)
        // b2Rot stores {c, s} = (cosine, sine) — direct assignment from face_vec().
        // rl_vec2d fields are float x, y (point_float.h:10-11).
        const auto fv    = v->face_vec();
        bdef.rotation    = b2Rot{ fv.x, fv.y };

        // velo_vec() returns cm/s velocity vector; divide by 100 for m/s.
        // velo_vec() sig: rl_vec2d vehicle::velo_vec() const  (vehicle_move.cpp:1360)
        const auto vel   = v->velo_vec();
        bdef.linearVelocity  = { vel.x / 100.0f, vel.y / 100.0f };
        // angular_velocity_rads field does not exist until Phase 4; initialise to zero.
        bdef.angularVelocity = 0.0f;
        bdef.isBullet        = std::abs( v->velocity ) > 2000;  // CCD above ~45 mph

        const auto body = b2CreateBody( world, &bdef );

        // units::to_kilogram: units_mass.h:72; total_mass() is lazy-cached (vehicle.cpp:3850).
        const auto mass_kg   = static_cast<float>( units::to_kilogram( v->total_mass() ) );
        auto sdef            = b2DefaultShapeDef();
        const auto poly      = vehicle_box2d_shape( *v );
        // Approximate AABB area for density → inertia tensor.
        const auto half_w    = ( poly.vertices[2].x - poly.vertices[0].x ) / 2.0f;
        const auto half_h    = ( poly.vertices[2].y - poly.vertices[0].y ) / 2.0f;
        const auto area      = 4.0f * std::max( half_w * half_h, 0.01f );
        sdef.density         = mass_kg / area;
        // Decision D6: set restitution on both bodies from get_collision_factor(relative delta_v)
        // so b2MixRestitution=max(r1,r2) doesn't collapse to 1.0 for stationary targets.
        // get_collision_factor() declared vehicle.h:69, defined vehicle_move.cpp:1396.
        sdef.restitution     = get_collision_factor(
            std::abs( static_cast<float>( v->velocity ) / 100.0f ) );
        sdef.friction        = 0.3f;

        b2CreatePolygonShape( body, &sdef, &poly );

        body_map[v] = BodyData{ body,
                                { bdef.linearVelocity.x, bdef.linearVelocity.y },
                                mass_kg };
    }

    // Choose dt = time to cross one tile at max approach speed; clamped to [0.001, 0.1] s.
    auto max_approach_mps = 0.1f;
    for( const auto &[target, ignored] : veh_collisions ) {
        const auto rel = veh.velo_vec() - target->velo_vec();
        max_approach_mps = std::max( max_approach_mps,
                                     std::hypot( rel.x, rel.y ) / 100.0f );
    }
    const auto dt = std::clamp( TILE_M / max_approach_mps, 0.001f, 0.1f );

    b2World_Step( world, dt, 8 );

    auto result          = vv_cluster_result{};
    result.total_impulse_ns = 0.0f;
    result.bodies.reserve( veh_list.size() );

    for( auto *v : veh_list ) {
        const auto &bd  = body_map.at( v );
        const auto nv   = b2Body_GetLinearVelocity( bd.id );
        const auto nw   = b2Body_GetAngularVelocity( bd.id );
        const auto dvx  = nv.x - bd.old_vel_ms.x;
        const auto dvy  = nv.y - bd.old_vel_ms.y;
        const auto imp  = bd.mass_kg * std::hypot( dvx, dvy );

        result.bodies.push_back( {
            v,
            rl_vec2d{ nv.x * 100.0f, nv.y * 100.0f },
            nw,
            imp
        } );
        result.total_impulse_ns += imp;
    }

    b2DestroyWorld( world );
    return result;
}
#endif // BOX2D_ENABLED
