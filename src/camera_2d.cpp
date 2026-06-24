#include "camera_2d.h"

#include <algorithm>
#include <cmath>

void camera_2d::update( point target, bool snap )
{
    const double tx = target.x;
    const double ty = target.y;

    // Internal dt from a steady clock; first call has no delta.
    const auto now = std::chrono::steady_clock::now();
    float dt = 0.0f;
    if( have_last_ ) {
        dt = std::chrono::duration<float>( now - last_ ).count();
    }
    last_ = now;
    have_last_ = true;

    // Snap cases: first frame, explicit snap (look-around), smoothing disabled,
    // or no time elapsed. center jumps to target, no residual.
    if( !have_center_ || snap || follow_speed_ <= 0.0f || dt <= 0.0f ) {
        cx_ = tx;
        cy_ = ty;
        have_center_ = true;
        sub_x_ = 0.0f;
        sub_y_ = 0.0f;
        return;
    }

    // Large jumps (teleport, z-change, modal view_offset restore, debug possess)
    // must not slide the view across the world — snap instead of easing.
    constexpr double SNAP = 8.0;
    if( std::abs( tx - cx_ ) > SNAP || std::abs( ty - cy_ ) > SNAP ) {
        cx_ = tx;
        cy_ = ty;
        sub_x_ = 0.0f;
        sub_y_ = 0.0f;
        return;
    }

    // Clamp dt so a stalled frame (alt-tab, breakpoint) can't teleport the view.
    dt = std::min( dt, 0.1f );

    // Framerate-independent exponential ease toward target.
    const double t = 1.0 - std::exp( -static_cast<double>( follow_speed_ ) * dt );
    cx_ += ( tx - cx_ ) * t;
    cy_ += ( ty - cy_ ) * t;

    // Kill the infinite ease tail; keeps the steady state byte-identical to the
    // integer framing once the player stops.
    if( std::abs( tx - cx_ ) < 0.01 ) {
        cx_ = tx;
    }
    if( std::abs( ty - cy_ ) < 0.01 ) {
        cy_ = ty;
    }

    sub_x_ = static_cast<float>( cx_ - tx );
    sub_y_ = static_cast<float>( cy_ - ty );
}
