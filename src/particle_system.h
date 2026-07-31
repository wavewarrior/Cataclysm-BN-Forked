#pragma once
#ifndef CATA_SRC_PARTICLE_SYSTEM_H
#define CATA_SRC_PARTICLE_SYSTEM_H

#include <string>
#include <vector>

#include "coordinates.h"

/// How a particle is rasterised.
enum class particle_style {
    /// Tileset sprite snapped to `tile` + a sub-tile pixel offset. The authored
    /// art path; impact flashes want it.
    sprite,
    /// Pixel-space glowing streak oriented along the direction of travel — a
    /// bullet tracer. Independent of tile size and of tileset content.
    tracer,
    /// Pixel-space tumbling shard — a thrown object in flight.
    debris,
};

/// A single visual-only particle: position + sprite + trajectory + timing.
/// No game logic, no collision — pure render gloss. Not serialized.
struct particle {
    particle_style style = particle_style::sprite;

    /// `sprite`-style only.
    std::string sprite;
    int rotation = 0;

    /// Current render state (written by particle_system::update).
    tripoint_bub_ms tile;      ///< waypoint being approached
    tripoint_bub_ms tile_prev; ///< waypoint departed from (== `tile` when stationary)
    float frac = 0.f;          ///< 0..1 progress from `tile_prev` to `tile`
    float off_x = 0.f;         ///< sub-tile offset from `tile`, tile units (sprite path)
    float off_y = 0.f;

    /// Ordered tile waypoints the particle interpolates along.
    std::vector<tripoint_bub_ms> path;
    double start_wall = 0.0; // SDL wall-clock seconds at emission
    float duration = 0.f;    // total flight time, seconds
    float alpha = 1.f;       ///< current opacity (1 = opaque); decayed by update()

    /// Emissive tint for the pixel-space styles. Components MAY exceed 1.0: the
    /// world target is RGBA16F and bloom thresholds at 1.0, so >1 is what turns a
    /// tracer into a glowing streak instead of a flat coloured line.
    float tint_r = 1.f;
    float tint_g = 1.f;
    float tint_b = 1.f;
    /// Core thickness in pixels (tracer streak width / debris shard edge).
    float size_px = 3.f;
    /// Trail length in pixels behind the head, along travel (tracer only).
    float length_px = 20.f;

    /// Thrown-item tumble. `sprite` style cycles `rotation` per waypoint; `debris`
    /// style spins the shard on the wall clock. Clear it for a projectile that must
    /// stay aligned to its flight direction (FLY_STRAIGHT arrows, spears).
    bool tumble = false;
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
