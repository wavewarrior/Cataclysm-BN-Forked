#ifdef BOX2D_ENABLED
#include "physics_debug_draw.h"

#include <SDL3/SDL.h>
#include <cmath>
#include <numbers>
#include <vector>

namespace physics {
namespace {

// ── Helpers ──────────────────────────────────────────────────────────────────

auto to_screen( const DebugDrawContext &ctx, float bx, float by ) -> SDL_FPoint
{
    return { ctx.origin_px + bx * ctx.m2px,
             ctx.origin_py + by * ctx.m2py };
}

auto to_screen( const DebugDrawContext &ctx, b2Vec2 v ) -> SDL_FPoint
{
    return to_screen( ctx, v.x, v.y );
}

/// Apply b2Transform to a local vertex → world position.
auto apply_xf( b2Transform xf, b2Vec2 v ) -> b2Vec2
{
    return b2Vec2{ xf.p.x + v.x * xf.q.c - v.y * xf.q.s,
                   xf.p.y + v.x * xf.q.s + v.y * xf.q.c };
}

auto set_color( SDL_Renderer *r, b2HexColor c, Uint8 alpha = 210 ) -> void
{
    const auto v = static_cast<uint32_t>( c );
    SDL_SetRenderDrawColor( r,
                            static_cast<Uint8>( ( v >> 16 ) & 0xFF ),
                            static_cast<Uint8>( ( v >>  8 ) & 0xFF ),
                            static_cast<Uint8>(   v         & 0xFF ),
                            alpha );
}

/// Draw a closed polygon outline from screen-space points.
auto render_loop( SDL_Renderer *r, const SDL_FPoint *pts, int n ) -> void
{
    if( n < 2 ) { return; }
    auto closed = std::vector<SDL_FPoint>( pts, pts + n );
    closed.push_back( pts[0] ); // close the loop
    SDL_RenderLines( r, closed.data(), static_cast<int>( closed.size() ) );
}

constexpr int CIRCLE_SEGS = 32;

auto render_circle( SDL_Renderer *r, SDL_FPoint center, float radius_px ) -> void
{
    auto pts = std::vector<SDL_FPoint>{};
    pts.reserve( CIRCLE_SEGS + 1 );
    using std::numbers::pi_v;
    for( int i = 0; i <= CIRCLE_SEGS; ++i ) {
        const auto a = 2.0f * static_cast<float>( pi_v<double> ) * i / CIRCLE_SEGS;
        pts.push_back( { center.x + std::cos( a ) * radius_px,
                         center.y + std::sin( a ) * radius_px } );
    }
    SDL_RenderLines( r, pts.data(), static_cast<int>( pts.size() ) );
}

// ── b2DebugDraw callbacks ─────────────────────────────────────────────────────

void cb_DrawPolygon( const b2Vec2 *verts, int n, b2HexColor color, void *ctx_ )
{
    auto &ctx = *static_cast<DebugDrawContext *>( ctx_ );
    set_color( ctx.renderer, color );
    auto pts = std::vector<SDL_FPoint>{};
    pts.reserve( n );
    for( int i = 0; i < n; ++i ) { pts.push_back( to_screen( ctx, verts[i] ) ); }
    render_loop( ctx.renderer, pts.data(), n );
}

void cb_DrawSolidPolygon( b2Transform xf, const b2Vec2 *verts, int n,
                          float /*radius*/, b2HexColor color, void *ctx_ )
{
    auto &ctx = *static_cast<DebugDrawContext *>( ctx_ );
    set_color( ctx.renderer, color );
    auto pts = std::vector<SDL_FPoint>{};
    pts.reserve( n );
    for( int i = 0; i < n; ++i ) {
        pts.push_back( to_screen( ctx, apply_xf( xf, verts[i] ) ) );
    }
    render_loop( ctx.renderer, pts.data(), n );
}

void cb_DrawCircle( b2Vec2 center, float radius, b2HexColor color, void *ctx_ )
{
    auto &ctx = *static_cast<DebugDrawContext *>( ctx_ );
    set_color( ctx.renderer, color );
    render_circle( ctx.renderer, to_screen( ctx, center ), radius * ctx.m2px );
}

void cb_DrawSolidCircle( b2Transform xf, float radius, b2HexColor color, void *ctx_ )
{
    auto &ctx = *static_cast<DebugDrawContext *>( ctx_ );
    set_color( ctx.renderer, color );
    render_circle( ctx.renderer, to_screen( ctx, xf.p ), radius * ctx.m2px );
}

void cb_DrawCapsule( b2Vec2 p1, b2Vec2 p2, float radius, b2HexColor color, void *ctx_ )
{
    // Approximate: two end-circles + spine segment.
    cb_DrawCircle( p1, radius, color, ctx_ );
    cb_DrawCircle( p2, radius, color, ctx_ );
    auto &ctx = *static_cast<DebugDrawContext *>( ctx_ );
    set_color( ctx.renderer, color );
    SDL_FPoint seg[2] = { to_screen( ctx, p1 ), to_screen( ctx, p2 ) };
    SDL_RenderLines( ctx.renderer, seg, 2 );
}

void cb_DrawSolidCapsule( b2Vec2 p1, b2Vec2 p2, float radius, b2HexColor color,
                          void *ctx_ )
{
    cb_DrawCapsule( p1, p2, radius, color, ctx_ );
}

void cb_DrawSegment( b2Vec2 p1, b2Vec2 p2, b2HexColor color, void *ctx_ )
{
    auto &ctx = *static_cast<DebugDrawContext *>( ctx_ );
    set_color( ctx.renderer, color );
    SDL_FPoint pts[2] = { to_screen( ctx, p1 ), to_screen( ctx, p2 ) };
    SDL_RenderLines( ctx.renderer, pts, 2 );
}

void cb_DrawTransform( b2Transform xf, void *ctx_ )
{
    auto &ctx = *static_cast<DebugDrawContext *>( ctx_ );
    const auto origin = to_screen( ctx, xf.p );
    constexpr float arm = 0.5f; // 0.5 m axis length
    // X axis — red
    SDL_SetRenderDrawColor( ctx.renderer, 255, 60, 60, 230 );
    const auto x_tip = to_screen( ctx, { xf.p.x + arm * xf.q.c,
                                          xf.p.y + arm * xf.q.s } );
    SDL_FPoint xpts[2] = { origin, x_tip };
    SDL_RenderLines( ctx.renderer, xpts, 2 );
    // Y axis — green (perpendicular CCW)
    SDL_SetRenderDrawColor( ctx.renderer, 60, 255, 60, 230 );
    const auto y_tip = to_screen( ctx, { xf.p.x - arm * xf.q.s,
                                          xf.p.y + arm * xf.q.c } );
    SDL_FPoint ypts[2] = { origin, y_tip };
    SDL_RenderLines( ctx.renderer, ypts, 2 );
}

void cb_DrawPoint( b2Vec2 p, float /*size*/, b2HexColor color, void *ctx_ )
{
    auto &ctx = *static_cast<DebugDrawContext *>( ctx_ );
    set_color( ctx.renderer, color );
    const auto sp = to_screen( ctx, p );
    SDL_RenderPoint( ctx.renderer, sp.x, sp.y );
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
