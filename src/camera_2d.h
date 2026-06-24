#pragma once
#ifndef CATA_SRC_CAMERA_2D_H
#define CATA_SRC_CAMERA_2D_H

#include <chrono>

#include "point.h"

// Smooth sub-tile camera follow.
//
// Holds a floating-point view center that eases toward an integer tile target
// each frame. The fractional lag (smooth_center - target) is exposed as
// sub_x()/sub_y() and injected into cata_tiles' o/op so sprites AND lighting
// scroll together at sub-tile precision. See plans/camera_subtile_contract.md.
//
// This is intentionally minimal: it does not own view_offset, zoom, shake, or
// the minimap. view_offset stays the discrete target; this only adds the
// smooth fractional residual on top.
class camera_2d
{
    public:
        // Advance the smooth center toward an integer tile target.
        // snap = true forces an immediate jump (look-around, teleport, init).
        // dt is measured internally from a steady clock.
        void update( point target, bool snap = false );

        // Fractional residual in tile units, range ~(-1, 1) while easing,
        // exactly 0 when snapped, disabled, or converged.
        float sub_x() const {
            return sub_x_;
        }
        float sub_y() const {
            return sub_y_;
        }

        // Exponential ease rate (1/s). <= 0 disables smoothing (snap mode),
        // which makes output byte-identical to the legacy integer framing.
        void set_follow_speed( float s ) {
            follow_speed_ = s;
        }
        float follow_speed() const {
            return follow_speed_;
        }

        // Tiles of look-ahead lead at walking speed. 0 = off.
        void set_look_ahead( float tiles ) {
            look_ahead_ = tiles;
        }
        // Dead-zone radius (tiles); view holds while target stays within it. 0 = off.
        void set_dead_zone( float tiles ) {
            dead_zone_ = tiles;
        }

        // Kick a decaying screen shake, layered on top of the follow residual.
        // amplitude is in tiles, duration in seconds. A stronger ongoing shake
        // is not weakened by a smaller new one.
        void shake( float amplitude, float seconds );

    private:
        bool have_center_ = false;
        double cx_ = 0.0;
        double cy_ = 0.0;
        float sub_x_ = 0.0f;
        float sub_y_ = 0.0f;
        float follow_speed_ = 12.0f;
        float look_ahead_ = 0.0f;
        float dead_zone_ = 0.0f;

        // Smoothed target velocity (tiles/s) for look-ahead direction + ramp.
        double vx_ = 0.0;
        double vy_ = 0.0;
        bool have_prev_target_ = false;
        double prev_tx_ = 0.0;
        double prev_ty_ = 0.0;

        // Screen shake state.
        float shake_amp_ = 0.0f;     // current amplitude (tiles)
        float shake_k_ = 0.0f;       // decay rate (1/s)
        float shake_phase_ = 0.0f;   // oscillation phase

        bool have_last_ = false;
        std::chrono::steady_clock::time_point last_;
};

#endif // CATA_SRC_CAMERA_2D_H
