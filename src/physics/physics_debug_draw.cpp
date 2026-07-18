#ifdef BOX2D_ENABLED
#include "physics_debug_draw.h"

#include "lighting/debug_line_pass.h"

#include <cmath>
#include <numbers>

namespace physics {
namespace {

// ── Helpers ──────────────────────────────────────────────────────────────────

/// Convert Box2D metres to world-tile coordinates.
auto to_tile( const DebugDrawContext &ctx, float bx, float by )
    -> std::pair<float, float>
{
    return { bx * ctx.m2t, by * ctx.m2t };
}

auto to_tile( const DebugDrawContext &ctx, b2Vec2 v )
    -> std::pair<float, float>
{
    return to_tile( ctx, v.x, v.y );
}

/// Apply b2Transform to a local vertex → world position (still in metres).
auto apply_xf( b2Transform xf, b2Vec2 v ) -> b2Vec2
{
    return b2Vec2{ xf.p.x + v.x * xf.q.c - v.y * xf.q.s,
                   xf.p.y + v.x * xf.q.s + v.y * xf.q.c };
}

/// Extract RGBA floats [0,1] from a Box2D b2HexColor.
struct rgba {
    float r, g, b, a;
};

auto hex_to_rgba( b2HexColor c, float alpha = 0.82f ) -> rgba
{
    const auto v = static_cast<uint32_t>( c );
    return { static_cast<float>( ( v >> 16 ) & 0xFF ) / 255.f,
             static_cast<float>( ( v >>  8 ) & 0xFF ) / 255.f,
             static_cast<float>(   v         & 0xFF ) / 255.f,
             alpha };
}

constexpr int CIRCLE_SEGS = 32;

/// Push a circle approximation (CIRCLE_SEGS line segments) centred at
/// (cx,cy) with radius_t — all in tile coords.
auto add_circle( lighting::debug_line_pass &pass,
                 float cx, float cy, float radius_t, rgba col ) -> void
{
    using std::numbers::pi_v;
    constexpr auto two_pi = 2.0f * static_cast<float>( pi_v<double> );
    for( int i = 0; i < CIRCLE_SEGS; ++i ) {
        const float a0 = two_pi * static_cast<float>( i )     / CIRCLE_SEGS;
        const float a1 = two_pi * static_cast<float>( i + 1 ) / CIRCLE_SEGS;
        pass.add_line( cx + std::cos( a0 ) * radius_t,
                       cy + std::sin( a0 ) * radius_t,
                       cx + std::cos( a1 ) * radius_t,
                       cy + std::sin( a1 ) * radius_t,
                       col.r, col.g, col.b, col.a );
    }
}

// ── b2DebugDraw callbacks ─────────────────────────────────────────────────────

void cb_DrawPolygon( const b2Vec2 *verts, int n, b2HexColor color, void *ctx_ )
{
    auto &ctx = *static_cast<DebugDrawContext *>( ctx_ );
    const auto col = hex_to_rgba( color );
    for( int i = 0; i < n; ++i ) {
        const int j = ( i + 1 ) % n;
        auto [x0, y0] = to_tile( ctx, verts[i] );
        auto [x1, y1] = to_tile( ctx, verts[j] );
        ctx.pass->add_line( x0, y0, x1, y1, col.r, col.g, col.b, col.a );
    }
}

void cb_DrawSolidPolygon( b2Transform xf, const b2Vec2 *verts, int n,
                          float /*radius*/, b2HexColor color, void *ctx_ )
{
    auto &ctx = *static_cast<DebugDrawContext *>( ctx_ );
    const auto col = hex_to_rgba( color );
    for( int i = 0; i < n; ++i ) {
        const int j = ( i + 1 ) % n;
        auto [x0, y0] = to_tile( ctx, apply_xf( xf, verts[i] ) );
        auto [x1, y1] = to_tile( ctx, apply_xf( xf, verts[j] ) );
        ctx.pass->add_line( x0, y0, x1, y1, col.r, col.g, col.b, col.a );
    }
}

void cb_DrawCircle( b2Vec2 center, float radius, b2HexColor color, void *ctx_ )
{
    auto &ctx = *static_cast<DebugDrawContext *>( ctx_ );
    auto [cx, cy] = to_tile( ctx, center );
    const float radius_t = radius * ctx.m2t;
    add_circle( *ctx.pass, cx, cy, radius_t, hex_to_rgba( color ) );
}

void cb_DrawSolidCircle( b2Transform xf, float radius, b2HexColor color, void *ctx_ )
{
    auto &ctx = *static_cast<DebugDrawContext *>( ctx_ );
    auto [cx, cy] = to_tile( ctx, xf.p );
    const float radius_t = radius * ctx.m2t;
    add_circle( *ctx.pass, cx, cy, radius_t, hex_to_rgba( color ) );
}

void cb_DrawCapsule( b2Vec2 p1, b2Vec2 p2, float radius, b2HexColor color, void *ctx_ )
{
    // Approximate: two end-circles + spine segment.
    cb_DrawCircle( p1, radius, color, ctx_ );
    cb_DrawCircle( p2, radius, color, ctx_ );
    auto &ctx = *static_cast<DebugDrawContext *>( ctx_ );
    const auto col = hex_to_rgba( color );
    auto [x0, y0] = to_tile( ctx, p1 );
    auto [x1, y1] = to_tile( ctx, p2 );
    ctx.pass->add_line( x0, y0, x1, y1, col.r, col.g, col.b, col.a );
}

void cb_DrawSolidCapsule( b2Vec2 p1, b2Vec2 p2, float radius, b2HexColor color,
                          void *ctx_ )
{
    cb_DrawCapsule( p1, p2, radius, color, ctx_ );
}

void cb_DrawSegment( b2Vec2 p1, b2Vec2 p2, b2HexColor color, void *ctx_ )
{
    auto &ctx = *static_cast<DebugDrawContext *>( ctx_ );
    const auto col = hex_to_rgba( color );
    auto [x0, y0] = to_tile( ctx, p1 );
    auto [x1, y1] = to_tile( ctx, p2 );
    ctx.pass->add_line( x0, y0, x1, y1, col.r, col.g, col.b, col.a );
}

void cb_DrawTransform( b2Transform xf, void *ctx_ )
{
    auto &ctx = *static_cast<DebugDrawContext *>( ctx_ );
    auto [ox, oy] = to_tile( ctx, xf.p );
    constexpr float arm = 0.5f; // 0.5 m axis length
    // X axis — red
    auto [xx, xy] = to_tile( ctx, xf.p.x + arm * xf.q.c,
                                   xf.p.y + arm * xf.q.s );
    ctx.pass->add_line( ox, oy, xx, xy, 1.f, 0.235f, 0.235f, 0.9f );
    // Y axis — green (perpendicular CCW)
    auto [yx, yy] = to_tile( ctx, xf.p.x - arm * xf.q.s,
                                   xf.p.y + arm * xf.q.c );
    ctx.pass->add_line( ox, oy, yx, yy, 0.235f, 1.f, 0.235f, 0.9f );
}

void cb_DrawPoint( b2Vec2 p, float /*size*/, b2HexColor color, void *ctx_ )
{
    auto &ctx = *static_cast<DebugDrawContext *>( ctx_ );
    const auto col = hex_to_rgba( color );
    auto [tx, ty] = to_tile( ctx, p );
    ctx.pass->add_point( tx, ty, col.r, col.g, col.b, col.a );
}

void cb_DrawString( b2Vec2 /*p*/, const char * /*s*/, void * /*ctx_*/ )
{
    // No font access at this layer; on-world text labels are skipped.
}

} // namespace

auto make_debug_draw( DebugDrawContext *ctx ) -> b2DebugDraw
{
    auto dd = b2DebugDraw{};
    dd.context           = ctx;
    dd.DrawPolygon       = cb_DrawPolygon;
    dd.DrawSolidPolygon  = cb_DrawSolidPolygon;
    dd.DrawCircle        = cb_DrawCircle;
    dd.DrawSolidCircle   = cb_DrawSolidCircle;
    dd.DrawCapsule       = cb_DrawCapsule;
    dd.DrawSolidCapsule  = cb_DrawSolidCapsule;
    dd.DrawSegment       = cb_DrawSegment;
    dd.DrawTransform     = cb_DrawTransform;
    dd.DrawPoint         = cb_DrawPoint;
    dd.DrawString        = cb_DrawString;
    // What to show — useful defaults for gameplay debugging:
    dd.drawShapes          = true;   // vehicle + terrain collision outlines
    dd.drawJoints          = false;
    dd.drawJointExtras     = false;
    dd.drawAABBs           = false;
    dd.drawMass            = true;   // mass-centre crosses — useful for CoM debugging
    dd.drawContacts        = true;   // contact manifolds — validate VT/VV filter scheme
    dd.drawGraphColors     = false;
    dd.drawContactNormals  = true;   // normal arrows on active contacts
    dd.drawContactImpulses = false;
    dd.drawFrictionImpulses = false;
    dd.useDrawingBounds    = false;
    return dd;
}

} // namespace physics
#endif // BOX2D_ENABLED
