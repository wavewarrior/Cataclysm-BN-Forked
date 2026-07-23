#pragma once
#ifndef CATA_SRC_PARTICLE_SYSTEM_H
#define CATA_SRC_PARTICLE_SYSTEM_H

#include <string>
#include <vector>

#include "coordinates.h"

/// A single visual-only particle: position + sprite + trajectory + timing.
/// No game logic, no collision — pure render gloss. Not serialized.
struct particle {
    std::string sprite;
    int rotation = 0;

    /// Current render state (written by particle_system::update).
    tripoint_bub_ms tile;
    float off_x = 0.f; // sub-tile offset, tile units
    float off_y = 0.f;

    /// Ordered tile waypoints the particle interpolates along.
    std::vector<tripoint_bub_ms> path;
    double start_wall = 0.0; // SDL wall-clock seconds at emission
    float duration = 0.f;    // total flight time, seconds
    float alpha = 1.f;       ///< current opacity (1 = opaque); decayed by update()

    bool tumble = false; ///< cycle rotation per waypoint (thrown-item tumble)
    bool alive = true;
};

/// Lightweight particle system: emit, update per frame, iterate for rendering.
/// Owned by cata_tiles. No SDL/render types — the renderer reads state and draws.
class particle_system
{
    public:
        void emit( particle p );

        /// Advance all particles to wall_now; expire completed ones.
        void update( double wall_now );

        auto idle() const -> bool { return particles_.empty(); }
        void clear() { particles_.clear(); }
        auto active() const -> const std::vector<particle> & { return particles_; }

    private:
        std::vector<particle> particles_;
};

#endif // CATA_SRC_PARTICLE_SYSTEM_H
