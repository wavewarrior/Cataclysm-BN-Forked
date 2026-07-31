#include "solid_overlay.h"

#include "render_state.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace lighting
{

namespace
{

/// One flat-colour quad on the shared white texel.
///
/// pad2 > 0.5 selects sprite.frag's silhouette branch, which returns the tint
/// verbatim before any lighting, memory-fade, tone-grade or debug-mode code
/// runs — exactly the "this is an overlay, leave it alone" semantics we want.
/// It also makes flush_shadow_casters skip the quad, so a long wedge or arc
/// never casts a sun shadow.
auto make_flat_quad(
    float x, float y, float w, float h, float rotation, const overlay_color &color )
-> sprite_instance
{
    auto s = sprite_instance{};
    s.dst_x = x;
    s.dst_y = y;
    s.dst_w = w;
    s.dst_h = h;
    s.src_u = 0.0f;
    s.src_v = 0.0f;
    s.src_uw = 1.0f;
    s.src_vh = 1.0f;
    s.tint_r = color.r;
    s.tint_g = color.g;
    s.tint_b = color.b;
    s.tint_a = color.a;
    s.rotation = rotation;
    s.pad2 = 1.0f;
    return s;
}

auto push_quad( const sprite_instance &inst ) -> void
{
    auto &rs = get_render_state();
    if( SDL_GPUTexture *white = rs.geometry().white_texture() ) {
        rs.queue_tile_sprite( white, inst );
    }
}

/// Degenerate shapes would upload quads that rasterize to nothing (or to a
/// full-screen NaN smear once a zero length divides into a direction vector).
auto drawable( float w, float h, float alpha ) -> bool
{
    return w > 0.0f && h > 0.0f && alpha > 0.0f
           && std::isfinite( w ) && std::isfinite( h );
}

} // namespace

auto overlay_color_from_bytes( int r, int g, int b, int a ) -> overlay_color
{
    constexpr auto inv255 = 1.0f / 255.0f;
    return { .r = static_cast<float>( r ) * inv255,
             .g = static_cast<float>( g ) * inv255,
             .b = static_cast<float>( b ) * inv255,
             .a = static_cast<float>( a ) * inv255 };
}

auto overlay_rect( const SDL_FRect &r, const overlay_color &color ) -> void
{
    if( !drawable( r.w, r.h, color.a ) ) { return; }
    push_quad( make_flat_quad( r.x, r.y, r.w, r.h, 0.0f, color ) );
}

auto overlay_quad( const overlay_quad_options &opts ) -> void
{
    if( !drawable( opts.w, opts.h, opts.color.a ) ) { return; }
    push_quad( make_flat_quad(
                   opts.centre.x - opts.w * 0.5f, opts.centre.y - opts.h * 0.5f,
                   opts.w, opts.h, opts.rotation, opts.color ) );
}

auto overlay_line( const overlay_line_options &opts ) -> void
{
    const auto thickness = std::max( 1.0f, opts.thickness );
    const auto dx = opts.to.x - opts.from.x;
    const auto dy = opts.to.y - opts.from.y;
    // Axis-aligned segments stay unrotated so their edges land on exact pixel
    // boundaries (the corner brackets and dot trails depend on that crispness).
    if( dy == 0.0f ) {
        const auto x = std::min( opts.from.x, opts.to.x );
        overlay_rect( { x, opts.from.y, std::abs( dx ), thickness }, opts.color );
        return;
    }
    if( dx == 0.0f ) {
        const auto y = std::min( opts.from.y, opts.to.y );
        overlay_rect( { opts.from.x, y, thickness, std::abs( dy ) }, opts.color );
        return;
    }
    const auto len = std::sqrt( dx * dx + dy * dy );
    overlay_quad( {
        .centre = { ( opts.from.x + opts.to.x ) * 0.5f, ( opts.from.y + opts.to.y ) * 0.5f },
        .w = len,
        .h = thickness,
        .rotation = std::atan2( dy, dx ),
        .color = opts.color } );
}

auto overlay_polyline( const overlay_polyline_options &opts ) -> void
{
    if( opts.points.size() < 2 ) { return; }
    for( std::size_t i = 1; i < opts.points.size(); ++i ) {
        overlay_line( { .from = opts.points[i - 1],
                        .to = opts.points[i],
                        .thickness = opts.thickness,
                        .color = opts.color } );
    }
}

auto overlay_ring( const overlay_ring_options &opts ) -> void
{
    const auto segs = std::max( 3, opts.segments );
    if( opts.radius < 1.0f || opts.color.a <= 0.0f ) { return; }
    auto prev = SDL_FPoint{ opts.centre.x + opts.radius, opts.centre.y };
    for( auto i = 1; i <= segs; ++i ) {
        const auto ang =
            static_cast<float>( i ) * 2.0f * std::numbers::pi_v<float> / static_cast<float>( segs );
        const auto next = SDL_FPoint{ opts.centre.x + opts.radius * std::cos( ang ),
                                      opts.centre.y + opts.radius * std::sin( ang ) };
        overlay_line( { .from = prev,
                        .to = next,
                        .thickness = opts.thickness,
                        .color = opts.color } );
        prev = next;
    }
}

auto overlay_wedge( const overlay_wedge_options &opts ) -> void
{
    const auto slabs = std::max( 1, opts.slabs );
    // Clamp short of a quarter turn: tan() past that produces slab heights that
    // overshoot the wedge into a screen-wide bar.
    const auto half = std::clamp( opts.half_angle, 0.0f, 1.5f );
    if( opts.radius < 1.0f || half <= 0.0f || opts.color.a <= 0.0f ) { return; }
    const auto spread = std::tan( half );
    const auto dir_x = std::cos( opts.angle );
    const auto dir_y = std::sin( opts.angle );
    const auto step = opts.radius / static_cast<float>( slabs );
    for( auto i = 0; i < slabs; ++i ) {
        const auto mid = ( static_cast<float>( i ) + 0.5f ) * step;
        overlay_quad( { .centre = { opts.apex.x + dir_x * mid, opts.apex.y + dir_y * mid },
                        .w = step,
                        .h = 2.0f * mid * spread,
                        .rotation = opts.angle,
                        .color = opts.color } );
    }
}

} // namespace lighting
