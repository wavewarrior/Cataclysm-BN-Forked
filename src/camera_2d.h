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

    private:
        bool have_center_ = false;
        double cx_ = 0.0;
        double cy_ = 0.0;
        float sub_x_ = 0.0f;
        float sub_y_ = 0.0f;
        float follow_speed_ = 12.0f;

        bool have_last_ = false;
        std::chrono::steady_clock::time_point last_;
};

#endif // CATA_SRC_CAMERA_2D_H
