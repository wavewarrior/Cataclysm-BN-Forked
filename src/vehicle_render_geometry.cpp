#include "vehicle_render_geometry.h"

#include "vehicle.h"
#include "vehicle_part.h"

#include <algorithm>
#include <cmath>

#include "coordinates.h"
#include "units.h"

namespace
{

/// Shortest signed arc from `a` to `b` in radians, in (-pi, pi].
auto wrap_pi( float a ) -> float
{
    constexpr float two_pi = 2.0f * static_cast<float>( M_PI );
    float w = std::fmod( a + static_cast<float>( M_PI ), two_pi );
    if( w < 0.0f ) {
        w += two_pi;
    }
    return w - static_cast<float>( M_PI );
}

} // namespace

auto make_vehicle_render_frame( const vehicle &veh ) -> vehicle_render_frame
{
    vehicle_render_frame f;
    const std::vector<int> standalone = veh.all_standalone_parts();
    if( standalone.empty() ) {
        return f;
    }

    bool have_bounds = false;
    for( const int p : standalone ) {
        const tripoint_mnt_veh &m = veh.cpart( p ).mount;
        f.mount_min_x = have_bounds ? std::min( f.mount_min_x, m.x() ) : m.x();
        f.mount_min_y = have_bounds ? std::min( f.mount_min_y, m.y() ) : m.y();
        f.mount_max_x = have_bounds ? std::max( f.mount_max_x, m.x() ) : m.x();
        f.mount_max_y = have_bounds ? std::max( f.mount_max_y, m.y() ) : m.y();
        have_bounds = true;
    }

    const point_bub_ms anchor = veh.bub_ms_location().xy();

    // The committed part tiles are always `anchor + precalc[0]`, whichever code wrote
    // that layout: `refresh_precalc( physics_angle )` after a physics step (rotation
    // about mount (0,0)), or `rotate_to_world( pivot_rotation[0], pivot_anchor[0] )`
    // after `displace_vehicle` swapped precalc[1] into precalc[0] during a tile
    // crossing. Those two layouts differ by the rotated pivot offset, so deriving the
    // continuous origin one way while the tiles sit the other way teleports the
    // composite by that integer offset the moment the layout is rewritten - a 2-tile
    // lurch on a car whose pivot is 2 tiles off its mount origin.
    //
    // Correct therefore by the difference between the committed layout and the
    // continuous rotation, per part: `precalc[0] - round( R(a) * mount )`. It is an
    // integer vector, so it is exact and jitter-free in both writers: zero for the
    // `refresh_precalc` layout, which rounds the very same rotation, and the rotated
    // pivot offset for the pivot layout at 0/90/180/270 degrees. Anything finer (say
    // the unrounded residual) would make the composite step every time a part's tile
    // rounding flipped, and would drag the drawn vehicle off its own committed tiles.
    const bool physics_pose = veh.box2d_position_authority;
    const float a = physics_pose ? veh.physics_angle : units::to_radians( veh.pivot_rotation[0] );
    const float c = std::cos( a );
    const float s = std::sin( a );
    const float roff_x = physics_pose ? veh.render_offset_x : 0.0f;
    const float roff_y = physics_pose ? veh.render_offset_y : 0.0f;
    int sum_x = 0;
    int sum_y = 0;
    for( const int p : standalone ) {
        const vehicle_part &vp = veh.cpart( p );
        const float mx = static_cast<float>( vp.mount.x() );
        const float my = static_cast<float>( vp.mount.y() );
        sum_x += vp.precalc[0].x() - static_cast<int>( std::lround( c * mx - s * my ) );
        sum_y += vp.precalc[0].y() - static_cast<int>( std::lround( s * mx + c * my ) );
    }
    const float inv_n = 1.0f / static_cast<float>( standalone.size() );
    f.angle = a;
    f.origin_x = static_cast<float>( anchor.x() ) + roff_x + static_cast<float>( sum_x ) * inv_n;
    f.origin_y = static_cast<float>( anchor.y() ) + roff_y + static_cast<float>( sum_y ) * inv_n;
    return f;
}

auto vehicle_mount_to_bubble( const vehicle_render_frame &f, float mx,
                              float my ) -> vehicle_render_point
{
    const float c = std::cos( f.angle );
    const float s = std::sin( f.angle );
    return vehicle_render_point{ f.origin_x + ( c * mx - s * my ), f.origin_y + ( s * mx + c * my ) };
}

auto reset_vehicle_motion( vehicle_motion_state &state, const vehicle_render_pose &pose,
                           double now ) -> void
{
    state.shown = pose;
    state.seg_from = pose;
    state.seg_to = pose;
    state.seg_start = now;
    state.last_commit = now;
    state.last_update = now;
    state.seg_len = 1.0;
}

auto rebase_vehicle_motion( vehicle_motion_state &state, float dx, float dy ) -> void
{
    for( vehicle_render_pose *p : { &state.shown, &state.seg_from, &state.seg_to } ) {
        p->x += dx;
        p->y += dy;
    }
}

auto advance_vehicle_motion( vehicle_motion_state &state, const vehicle_motion_options &opts )
-> vehicle_render_pose
{
    const vehicle_render_pose &target = opts.target;
    const double now = opts.now;

    if( std::abs( target.x - state.shown.x ) > opts.snap_tiles ||
        std::abs( target.y - state.shown.y ) > opts.snap_tiles ) {
        reset_vehicle_motion( state, target, now );
        return state.shown;
    }

    if( opts.mode == vehicle_motion_mode::ease ) {
        const float dt = std::clamp( static_cast<float>( now - state.last_update ), 0.0f, 0.1f );
        const float t = 1.0f - std::exp( -opts.rate * dt );
        state.shown.x += ( target.x - state.shown.x ) * t;
        state.shown.y += ( target.y - state.shown.y ) * t;
        state.shown.angle += wrap_pi( target.angle - state.shown.angle ) * t;
        // Close out the asymptote so a settled vehicle costs nothing to compare.
        if( std::abs( target.x - state.shown.x ) < 0.01f ) { state.shown.x = target.x; }
        if( std::abs( target.y - state.shown.y ) < 0.01f ) { state.shown.y = target.y; }
        if( std::abs( wrap_pi( target.angle - state.shown.angle ) ) < 0.001f ) {
            state.shown.angle = target.angle;
        }
    } else {
        const bool new_commit = std::abs( target.x - state.seg_to.x ) > 1e-4f ||
                                std::abs( target.y - state.seg_to.y ) > 1e-4f ||
                                std::abs( wrap_pi( target.angle - state.seg_to.angle ) ) > 1e-5f;
        if( new_commit ) {
            state.seg_from = state.shown;
            state.seg_to = target;
            state.seg_len = std::clamp( now - state.last_commit, 0.016, 1.0 );
            state.seg_start = now;
            state.last_commit = now;
        }
        const float f = std::clamp( static_cast<float>( ( now - state.seg_start ) / state.seg_len ),
                                    0.0f, 1.0f );
        state.shown.x = state.seg_from.x + ( state.seg_to.x - state.seg_from.x ) * f;
        state.shown.y = state.seg_from.y + ( state.seg_to.y - state.seg_from.y ) * f;
        state.shown.angle = state.seg_from.angle +
                            wrap_pi( state.seg_to.angle - state.seg_from.angle ) * f;
    }

    state.last_update = now;
    return state.shown;
}

auto make_vehicle_path_band( const vehicle_path_options &opts ) -> vehicle_path_band
{
    const vehicle_render_frame &f = opts.frame;
    vehicle_path_band band;
    band.width = static_cast<float>( f.mount_max_y - f.mount_min_y + 1 );

    const float s = opts.velocity < 0 ? -1.0f : 1.0f;
    const float a = f.angle;
    const float delta = wrap_pi( opts.steer_angle - a );
    // The band leaves the leading edge (the trailing edge when reversing), mid-width.
    const float lead_mount_x = s > 0.0f ? static_cast<float>( f.mount_max_x ) + 0.5f :
                               static_cast<float>( f.mount_min_x ) - 0.5f;
    const vehicle_render_point start = vehicle_mount_to_bubble( f, lead_mount_x,
                                       0.5f * static_cast<float>( f.mount_min_y + f.mount_max_y ) );
    const float d0x = s * std::cos( a );
    const float d0y = s * std::sin( a );
    const float d1x = s * std::cos( a + delta );
    const float d1y = s * std::sin( a + delta );

    // Two turns of look-ahead, clamped to a band worth drawing.
    constexpr float path_lookahead_turns = 2.0f;
    constexpr float path_min_tiles = 4.0f;
    constexpr float path_max_tiles = 24.0f;
    const float length = std::clamp(
                             path_lookahead_turns * std::abs( static_cast<float>( opts.velocity ) ) / vehicles::cmps_per_tile,
                             path_min_tiles, path_max_tiles );

    // Quadratic Bezier: start, control half a length on along the current heading, end half a
    // length further along the steer heading.
    const float cx = start.x + d0x * length * 0.5f;
    const float cy = start.y + d0y * length * 0.5f;
    const float ex = cx + d1x * length * 0.5f;
    const float ey = cy + d1y * length * 0.5f;
    const float half_width = band.width * 0.5f;

    for( int i = 0; i < vehicle_path_band::samples; ++i ) {
        const float t = static_cast<float>( i ) / ( vehicle_path_band::samples - 1 );
        const float mt = 1.0f - t;
        const vehicle_render_point c{
            mt *mt *start.x + 2.0f * mt *t *cx + t *t * ex,
            mt *mt *start.y + 2.0f * mt *t *cy + t *t *ey
        };
        float tx = 2.0f * mt * ( cx - start.x ) + 2.0f * t * ( ex - cx );
        float ty = 2.0f * mt * ( cy - start.y ) + 2.0f * t * ( ey - cy );
        const float mag = std::sqrt( tx * tx + ty * ty );
        if( mag > 0.0f ) {
            tx /= mag;
            ty /= mag;
        }
        band.centre[i] = c;
        band.left[i] = vehicle_render_point{ c.x + ty * half_width, c.y - tx * half_width };
        band.right[i] = vehicle_render_point{ c.x - ty * half_width, c.y + tx * half_width };
    }
    return band;
}

