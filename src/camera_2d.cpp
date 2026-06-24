#include "camera_2d.h"

#include <algorithm>
#include <cmath>

namespace
{
// Beyond this target jump (tiles) we snap instead of easing, so a teleport or
// modal view_offset restore does not slide the view across the world.
constexpr double SNAP_DIST = 8.0;
// Shake oscillation frequency (rad/s) — fast enough to read as a jolt.
constexpr float SHAKE_FREQ = 38.0f;
// Velocity-estimate smoothing rate (1/s) for look-ahead.
constexpr double VEL_SMOOTH = 6.0;
// Target speed (tiles/s) at which look-ahead reaches full lead.
constexpr double LOOKAHEAD_FULL_SPEED = 4.0;
} // namespace

void camera_2d::update( point target, bool snap )
{
    const double tx = target.x;
    const double ty = target.y;

    // Internal dt from a steady clock; first call has no delta. Clamp so a
    // stalled frame (alt-tab, breakpoint) cannot teleport the view or the shake.
    const auto now = std::chrono::steady_clock::now();
    float dt = 0.0f;
    if( have_last_ ) {
        dt = std::chrono::duration<float>( now - last_ ).count();
    }
    last_ = now;
    have_last_ = true;
    dt = std::min( dt, 0.1f );

    // Smoothed target velocity (tiles/s) for look-ahead. Player steps are
    // discrete 1-tile jumps, so the per-frame rate is spiky — smooth it into a
    // moving average that ramps the lead in and out.
    if( have_prev_target_ && dt > 0.0f ) {
        const double k = 1.0 - std::exp( -VEL_SMOOTH * dt );
        vx_ += ( ( tx - prev_tx_ ) / dt - vx_ ) * k;
        vy_ += ( ( ty - prev_ty_ ) / dt - vy_ ) * k;
    }
    prev_tx_ = tx;
    prev_ty_ = ty;
    have_prev_target_ = true;

    // ── Follow residual ──────────────────────────────────────────────────
    double bx = 0.0;
    double by = 0.0;

    const bool can_smooth = have_center_ && !snap && follow_speed_ > 0.0f && dt > 0.0f;
    const bool big_jump = have_center_ &&
                          ( std::abs( tx - cx_ ) > SNAP_DIST || std::abs( ty - cy_ ) > SNAP_DIST );

    if( !can_smooth || big_jump ) {
        // Snap: first frame, look-around, smoothing disabled, or a large jump.
        cx_ = tx;
        cy_ = ty;
        have_center_ = true;
        vx_ = 0.0;
        vy_ = 0.0;
    } else {
        // Aim at the target, plus a look-ahead lead in the direction of travel,
        // unless the target is inside the dead zone (then hold the view still).
        double aim_x = tx;
        double aim_y = ty;
        const bool in_dead_zone = dead_zone_ > 0.0f &&
                                  std::abs( tx - cx_ ) <= dead_zone_ &&
                                  std::abs( ty - cy_ ) <= dead_zone_;
        if( in_dead_zone ) {
            aim_x = cx_;
            aim_y = cy_;
        } else if( look_ahead_ > 0.0f ) {
            const double speed = std::hypot( vx_, vy_ );
            if( speed > 0.05 ) {
                const double act = std::min( 1.0, speed / LOOKAHEAD_FULL_SPEED );
                aim_x += ( vx_ / speed ) * look_ahead_ * act;
                aim_y += ( vy_ / speed ) * look_ahead_ * act;
            }
        }

        // Framerate-independent exponential ease toward the aim point.
        const double t = 1.0 - std::exp( -static_cast<double>( follow_speed_ ) * dt );
        cx_ += ( aim_x - cx_ ) * t;
        cy_ += ( aim_y - cy_ ) * t;
        // With no dead zone, kill the ease tail so the steady state is
        // byte-identical to the integer framing once the player stops. (A dead
        // zone intentionally rests off-center, so skip the snap there.)
        if( dead_zone_ <= 0.0f ) {
            if( std::abs( tx - cx_ ) < 0.01 ) {
                cx_ = tx;
            }
            if( std::abs( ty - cy_ ) < 0.01 ) {
                cy_ = ty;
            }
        }
        bx = cx_ - tx;
        by = cy_ - ty;
    }

    // ── Screen shake, layered on top of the follow residual ──────────────
    float sx = 0.0f;
    float sy = 0.0f;
    if( shake_amp_ > 0.001f && dt > 0.0f ) {
        shake_phase_ += dt * SHAKE_FREQ;
        // Different multipliers on x/y so the jolt is not a clean diagonal.
        sx = std::sin( shake_phase_ ) * shake_amp_;
        sy = std::cos( shake_phase_ * 1.3f ) * shake_amp_;
        shake_amp_ *= std::exp( -shake_k_ * dt );
        if( shake_amp_ < 0.001f ) {
            shake_amp_ = 0.0f;
        }
    }

    sub_x_ = static_cast<float>( bx ) + sx;
    sub_y_ = static_cast<float>( by ) + sy;
}

void camera_2d::shake( float amplitude, float seconds )
{
    if( amplitude <= 0.0f || seconds <= 0.0f ) {
        return;
    }
    // Don't let a small new shake weaken a stronger ongoing one.
    shake_amp_ = std::max( shake_amp_, amplitude );
    // Decay rate so amplitude falls to ~1% after `seconds`. -ln(0.01) ≈ 4.6.
    shake_k_ = 4.6f / seconds;
}
