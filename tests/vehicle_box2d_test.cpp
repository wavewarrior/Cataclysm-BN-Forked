#ifdef BOX2D_ENABLED
#include "catch/catch_amalgamated.hpp"
#include <box2d/box2d.h>
#include <cmath>
#include <numbers>
#include <utility>

static constexpr float TILE_M = 1.78816f;

static auto make_test_world() -> b2WorldId
{
    auto wdef    = b2DefaultWorldDef();
    wdef.gravity = { 0.0f, 0.0f };
    return b2CreateWorld( &wdef );
}

static auto make_box_body( b2WorldId world,
                           float px, float py,
                           float vx, float vy,
                           float hw, float hh,
                           float mass_kg,
                           float restitution = 1.0f ) -> b2BodyId
{
    auto bdef      = b2DefaultBodyDef();
    bdef.type      = b2_dynamicBody;
    bdef.position  = { px, py };
    bdef.rotation  = b2Rot{ 1.0f, 0.0f };  // facing east
    bdef.linearVelocity = { vx, vy };
    bdef.isBullet  = ( std::hypot( vx, vy ) > 20.0f );

    const auto body = b2CreateBody( world, &bdef );

    auto sdef        = b2DefaultShapeDef();
    sdef.density     = mass_kg / ( 4.0f * hw * hh );
    sdef.friction    = 0.0f;
    sdef.restitution = restitution;

    // hw/hh are already correct local-frame half-extents; no centre offset needed for
    // manually centred test bodies.
    const auto poly = b2MakeBox( hw, hh );
    b2CreatePolygonShape( body, &sdef, &poly );
    return body;
}

// ── Test 1: momentum conserved, equal-mass head-on elastic ──────────────────
TEST_CASE( "Box2D VV: momentum conserved in head-on equal-mass collision",
           "[vehicle][box2d]" )
{
    const auto mass = 1000.0f;
    const auto hw   = 2.0f * TILE_M;
    const auto hh   = 1.0f * TILE_M;

    const auto world = make_test_world();
    const auto b1    = make_box_body( world, 0.0f,      0.0f, 10.0f, 0.0f, hw, hh, mass );
    const auto b2    = make_box_body( world, 2.0f * hw, 0.0f,  0.0f, 0.0f, hw, hh, mass );

    b2World_Step( world, TILE_M / 10.0f, 8 );

    const auto v1        = b2Body_GetLinearVelocity( b1 );
    const auto v2        = b2Body_GetLinearVelocity( b2 );
    // Total x-momentum must be conserved within 5%.
    const auto px_before = mass * 10.0f;
    const auto px_after  = mass * v1.x + mass * v2.x;

    CHECK( px_after == Catch::Approx( px_before ).epsilon( 0.05f ) );
    // Moving body should transfer velocity to stationary one.
    CHECK( v1.x == Catch::Approx( 0.0f ).margin( 1.5f ) );
    CHECK( v2.x == Catch::Approx( 10.0f ).epsilon( 0.15f ) );

    b2DestroyWorld( world );
}

// ── Test 2: off-centre hit produces angular velocity on target ───────────────
TEST_CASE( "Box2D VV: off-centre hit produces angular velocity on target",
           "[vehicle][box2d]" )
{
    const auto mass = 1000.0f;
    const auto hw1  = 0.9f * TILE_M;
    const auto hh1  = 0.5f * TILE_M;
    const auto hw2  = 2.0f * TILE_M;
    const auto hh2  = 1.0f * TILE_M;

    const auto world = make_test_world();
    // Strike the target 60% of the way off-centre to guarantee torque.
    const auto y_off = hh2 * 0.6f;
    make_box_body( world, 0.0f,       y_off, 10.0f, 0.0f, hw1, hh1, mass, 0.8f );
    const auto b2    = make_box_body( world, hw1 + hw2,  0.0f,   0.0f, 0.0f, hw2, hh2, mass, 0.8f );

    b2World_Step( world, TILE_M / 10.0f, 8 );

    const auto v2    = b2Body_GetLinearVelocity( b2 );
    const auto omega = b2Body_GetAngularVelocity( b2 );

    // Target must receive positive x-velocity from the impact.
    CHECK( v2.x > 1.0f );
    // Off-centre contact must produce non-trivial spin.
    CHECK( std::abs( omega ) > 0.1f );

    b2DestroyWorld( world );
}
// ── Test 3: Phase 5 — transient VT collision produces angular spin ────────────
// Tests the physics that resolve_terrain_impulse() relies on: a vehicle-like
// dynamic body making a glancing hit on a static tile must acquire angular velocity.
TEST_CASE( "Box2D Phase5 VT: glancing terrain hit produces angular velocity",
           "[vehicle][box2d]" )
{
    const auto mass = 1500.0f;
    const auto hw   = 1.5f * TILE_M;
    const auto hh   = 0.9f * TILE_M;

    const auto world = make_test_world();
    // Vehicle moving east at 15 m/s; offset upward so impact is off-centre.
    const auto veh = make_box_body( world, 0.0f, hh * 0.5f, 15.0f, 0.0f, hw, hh, mass, 0.3f );

    // Static tile body; near-edge contact creates torque.
    auto tbdef     = b2DefaultBodyDef();
    tbdef.type     = b2_staticBody;
    tbdef.position = { hw + TILE_M * 0.5f, 0.0f };
    const auto tile_body = b2CreateBody( world, &tbdef );
    auto tsdef           = b2DefaultShapeDef();
    tsdef.restitution    = 0.3f;
    tsdef.friction       = 0.0f;
    const auto tile_poly = b2MakeBox( TILE_M * 0.5f, TILE_M * 0.5f );
    b2CreatePolygonShape( tile_body, &tsdef, &tile_poly );

    b2World_Step( world, 1.0f / 60.0f, 8 );

    const auto omega = b2Body_GetAngularVelocity( veh );
    const auto nv    = b2Body_GetLinearVelocity( veh );
    // Glancing hit must produce non-trivial spin.
    CHECK( std::abs( omega ) > 0.05f );
    // Linear velocity must be finite and non-zero (vehicle is not stopped by one tile).
    CHECK( std::isfinite( nv.x ) );
    CHECK( std::hypot( nv.x, nv.y ) > 1.0f );
    // Kinetic energy must not be created (e = 0.3 < 1, energy is lost).
    const auto ke_before = 0.5f * mass * ( 15.0f * 15.0f );
    const auto ke_after  = 0.5f * mass * ( nv.x * nv.x + nv.y * nv.y );
    CHECK( ke_after <= ke_before * 1.01f );  // 1% tolerance for solver numerics

    b2DestroyWorld( world );
}

// ── Test 4: Phase 10 — kinematic body integrates position from set velocity ───
// Verifies the tile-step → Box2D velocity sync used in PhysicsWorld::step().
TEST_CASE( "Box2D Phase10: kinematic body integrates from velocity then reads back",
           "[vehicle][box2d]" )
{
    const auto world = make_test_world();

    auto bdef              = b2DefaultBodyDef();
    bdef.type              = b2_kinematicBody;
    bdef.position          = { 0.0f, 0.0f };
    bdef.linearVelocity    = { 10.0f, 0.0f };   // 10 m/s east
    bdef.angularVelocity   = 1.0f;               // 1 rad/s CCW
    const auto body        = b2CreateBody( world, &bdef );

    const auto dt = 1.0f / 60.0f;
    b2World_Step( world, dt, 4 );

    const auto pos   = b2Body_GetPosition( body );
    const auto rot       = b2Body_GetRotation( body );
    const auto angle     = b2Rot_GetAngle( rot );

    // Position integrates from velocity: x ≈ v*dt, y ≈ 0.
    CHECK( pos.x == Catch::Approx( 10.0f * dt ).epsilon( 0.02f ) );
    CHECK( pos.y == Catch::Approx( 0.0f ).margin( 0.001f ) );
    // Angle integrates from angular velocity: θ ≈ ω*dt.
    CHECK( angle == Catch::Approx( 1.0f * dt ).epsilon( 0.05f ) );
    // physics_angle readback: atan2(rot.s, rot.c) must equal b2Rot_GetAngle.
    const auto phys_ang = std::atan2( rot.s, rot.c );
    CHECK( phys_ang == Catch::Approx( angle ).margin( 1e-4f ) );

    b2DestroyWorld( world );
}

// ── Test 5: Phase 10 full — dynamic-static contact fires begin-touch event ────
// Box2D v3.0.0 contact events (begin/end/hit) only fire when at least one body
// is b2_dynamicBody.  Kinematic-static pairs are processed for collision geometry
// but produce NO events.  This test pins the event mechanism that
// dispatch_contact_events() will use once vehicles are promoted to b2_dynamicBody;
// it also validates that b2Shape_GetBody() + b2Body_GetUserData() round-trips
// the vehicle pointer correctly (the dispatch_contact_events() identification path).
TEST_CASE( "Box2D Phase10 full: dynamic body generates begin-touch event against static tile",
           "[vehicle][box2d]" )
{
    const auto world = make_test_world();  // zero gravity

    // Dynamic "vehicle" body moving east at 20 m/s (zero gravity, no drift).
    auto vbdef                = b2DefaultBodyDef();
    vbdef.type                = b2_dynamicBody;
    vbdef.position            = { 0.0f, 0.0f };
    vbdef.linearVelocity      = { 20.0f, 0.0f };
    const auto vbody          = b2CreateBody( world, &vbdef );
    // Tag the vehicle body with a sentinel pointer (mimics b2Body_SetUserData(&v)).
    int sentinel              = 42;
    b2Body_SetUserData( vbody, &sentinel );
    auto vsdef                = b2DefaultShapeDef();
    vsdef.enableContactEvents = true;
    vsdef.density             = 100.0f;
    vsdef.restitution         = 0.5f;
    vsdef.friction            = 0.0f;
    // Vehicle box: right edge at x = TILE_M.
    const auto vpoly = b2MakeBox( TILE_M, TILE_M );
    b2CreatePolygonShape( vbody, &vsdef, &vpoly );

    // Static terrain tile flush with vehicle right edge → contact fires on step 1.
    auto tbdef     = b2DefaultBodyDef();
    tbdef.type     = b2_staticBody;
    tbdef.position = { TILE_M + TILE_M * 0.5f, 0.0f };
    const auto tbody = b2CreateBody( world, &tbdef );
    auto tsdef                = b2DefaultShapeDef();
    tsdef.enableContactEvents = true;
    const auto tpoly = b2MakeBox( TILE_M * 0.5f, TILE_M * 0.5f );
    b2CreatePolygonShape( tbody, &tsdef, &tpoly );

    b2World_Step( world, 1.0f / 60.0f, 8 );

    const auto events = b2World_GetContactEvents( world );
    // Dynamic-static must produce at least one begin-touch event.
    CHECK( events.beginCount >= 1 );
    if( events.beginCount >= 1 ) {
        // Verify dispatch_contact_events() identification path:
        // b2Shape_GetBody → b2Body_GetUserData must return our sentinel on one side.
        const auto bidA  = b2Shape_GetBody( events.beginEvents[0].shapeIdA );
        const auto bidB  = b2Shape_GetBody( events.beginEvents[0].shapeIdB );
        const auto udA   = b2Body_GetUserData( bidA );
        const auto udB   = b2Body_GetUserData( bidB );
        CHECK( ( udA == &sentinel || udB == &sentinel ) );
    }

    b2DestroyWorld( world );
}

// ── Test 6: Phase 7 — rotation formula used in refresh_precalc is correct ─────
// Verifies the 2D rotation math that refresh_precalc() and part_world_offset()
// both use, at canonical angles where floating-point results are exact.
TEST_CASE( "Box2D Phase7: mount rotation formula gives correct offsets",
           "[vehicle][box2d]" )
{
    // Helper: apply the same formula as refresh_precalc / part_world_offset.
    const auto rotate = []( float mx, float my, float angle_rads )
        -> std::pair<int, int>
    {
        const float c = std::cos( angle_rads );
        const float s = std::sin( angle_rads );
        return {
            static_cast<int>( std::round( mx * c - my * s ) ),
            static_cast<int>( std::round( mx * s + my * c ) )
        };
    };
    using namespace std::numbers;
    // 0°: identity — mount (2, 1) stays (2, 1).
    {
        const auto [rx, ry] = rotate( 2.0f, 1.0f, 0.0f );
        CHECK( rx == 2 );
        CHECK( ry == 1 );
    }
    // 90° CCW: (1, 0) → (0, 1).
    {
        const auto [rx, ry] = rotate( 1.0f, 0.0f, static_cast<float>( pi_v<double> ) / 2.0f );
        CHECK( rx == 0 );
        CHECK( ry == 1 );
    }
    // 180°: (2, 1) → (-2, -1).
    {
        const auto [rx, ry] = rotate( 2.0f, 1.0f, static_cast<float>( pi_v<double> ) );
        CHECK( rx == -2 );
        CHECK( ry == -1 );
    }
    // 270° CCW (= 90° CW): (1, 0) → (0, -1).
    {
        const auto [rx, ry] = rotate( 1.0f, 0.0f, 3.0f * static_cast<float>( pi_v<double> ) / 2.0f );
        CHECK( rx == 0 );
        CHECK( ry == -1 );
    }
}
#endif // BOX2D_ENABLED
