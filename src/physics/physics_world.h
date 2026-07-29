#pragma once
#ifdef BOX2D_ENABLED
#include <box2d/box2d.h>
#include "coordinates.h"
#include "point_float.h"  // rl_vec2d
#include <map>
#include <unordered_map>
#include <vector>
namespace lighting { class debug_line_pass; } // forward-declare GPU line buffer

class Creature;
// MUST be `class`, matching vehicle.h:375 and every other forward declaration in
// the tree (map.h:69 et al). MSVC mangles `class` and `struct` differently, so a
// `struct vehicle;` here makes every TU that includes this header emit/expect a
// different symbol for any map:: member taking a vehicle pointer — LNK2019 on
// map::move_cost / map::bash / map::detach_vehicle. Only reachable with
// BOX2D_ENABLED, which is why it stayed latent while BOX2D defaulted OFF.
// GCC/Clang do not distinguish the two, so CI never caught it.
class vehicle;
class map;

namespace physics {

/// Result of a one-shot transient terrain-impulse solve (Phase 5).
struct terrain_impulse_result {
    rl_vec2d linear_vel_cmps{ 0.0f, 0.0f }; ///< Post-collision velocity in cm/s.
    float    angular_vel_rads = 0.0f;        ///< Post-collision angular velocity in rad/s.
};

/// Persistent Box2D world owned by `map`.
///
/// Responsibilities (phased in across Phases 3–11):
///   Phase 3  — Vehicle and terrain body registry; map-shift translation.
///   Phase 4  — Contact events route impulse to damage system.
///   Phase 5  — `resolve_terrain_impulse` replaces 1-D elastic formula.
///   Phase 10 — Vehicles promoted to b2_dynamicBody; movement driven by Box2D.
///   Phase 11 — Ray-cast queries replace tile-traversal LOS.
///
/// Null (i.e. not constructed) when built without `-DBOX2D=ON`.
class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld( const PhysicsWorld & )                         = delete;
    auto operator=( const PhysicsWorld & ) -> PhysicsWorld &     = delete; // *NOPAD*

    // ── Vehicle lifecycle ──────────────────────────────────────────────────
    void on_vehicle_added( vehicle &v );
    void on_vehicle_moved( vehicle &v );
    /// Force the b2Body transform back to the vehicle's current tile anchor,
    /// bypassing the `box2d_position_authority` guard in `on_vehicle_moved`.
    /// Used when continuous integration carried the body past the loaded map
    /// edge and the tile grid must win instead.
    void clamp_body_to_tile( vehicle &v );
    void on_vehicle_removed( vehicle *v );

    // ── Creature lifecycle (Phase 11) ─────────────────────────────────────
    void on_creature_added( const Creature &c );
    void on_creature_moved( const Creature &c );
    void on_creature_removed( const Creature *c );
    void clear_creature_bodies();

    // ── Terrain lifecycle ──────────────────────────────────────────────────
    /// Called after `grid[idx] = sm` in `map::on_submap_loaded`.
    void on_submap_loaded( const map &m, const tripoint_abs_sm &abs_sm_pos );
    /// Called before `grid[idx] = nullptr` in `map::on_submap_unloaded`.
    void on_submap_unloaded( const tripoint_abs_sm &abs_sm_pos );

    // ── Coordinate shift ───────────────────────────────────────────────────
    /// Translate all Box2D bodies by `delta_tiles × TILE_M` metres.
    /// `delta_tiles` = `shift_offset_pt` from `map::shift` (tile units, signed).
    void on_map_shifted( point delta_tiles );

    // ── Z-level transition ────────────────────────────────────────────────
    /// Destroy terrain bodies for old_z and create them for new_z.
    /// Called when the player changes z-level (stairs, ramps, etc.).
    void on_zlevel_changed( const map &m, int old_z, int new_z );

    // ── Phase 5 hooks (wired in Phase 5) ─────────────────────────────────
    /// Remove the Box2D body for the bashable tile at `pos` after it is bashed.
    void on_tile_bashed( tripoint_bub_ms pos );

    // ── Game-loop interface (wired from Phase 8 / Phase 10) ───────────────
    void step( float dt, int substeps );
    void dispatch_contact_events();


    // ── Debug overlay (Phase 10 debugging tool) ───────────────────────────
    /// Toggle the real-time Box2D shape overlay (tiles mode only).
    /// Returns the new enabled state.
    auto toggle_debug_draw() -> bool;
    auto debug_draw_enabled() const -> bool { return debug_draw_; } // *NOPAD*
    /// Populate the GPU debug_line_pass with collision shapes, contact manifolds,
    /// and body transforms.  Called from cata_tiles::draw() after tiles are
    /// flushed; the actual GPU draw happens later in render_world_pass_w.
    auto draw_debug( lighting::debug_line_pass &pass ) const -> void;
    // ── Query access ──────────────────────────────────────────────────────
    auto world_id() const -> b2WorldId; // *NOPAD*

    // ── Phase 5: transient terrain-impulse solve ──────────────────────────
    auto resolve_terrain_impulse( vehicle        &v,
                                  tripoint_bub_ms tile_pos,
                                  float           tile_mass_kg,
                                  float           restitution ) -> terrain_impulse_result;

private:
    b2WorldId world_;

    /// vehicle* → body in the persistent world.
    std::unordered_map<vehicle *, b2BodyId> vehicle_bodies_;

    /// abs_sm_pos → flat list of all terrain bodies for that submap.
    std::map<tripoint_abs_sm, std::vector<b2BodyId>> terrain_bodies_;

    /// abs_sm_pos → list of (bub_ms, b2BodyId) for bashable tiles only.
    /// Maintained separately from terrain_bodies_ so on_tile_bashed / on_map_shifted
    /// can update bub_ms keys cheaply without decoding user-data after each shift.
    std::map<tripoint_abs_sm, std::vector<std::pair<tripoint_bub_ms, b2BodyId>>>
        bashable_tiles_;

    /// bub_ms → b2BodyId fast-lookup for on_tile_bashed.  Rebuilt after each map shift.
    std::map<tripoint_bub_ms, b2BodyId> bashable_tile_bodies_;

    /// Creature body registry: kinematic sensor bodies for raycast hit detection.
    struct creature_body {
        b2BodyId body;
        b2ShapeId shape;
        float radius; // last-used circle radius (physics meters)
    };
    std::unordered_map<const Creature *, creature_body> creature_bodies_;

    auto make_vehicle_body( vehicle &v ) -> b2BodyId;
    void rebuild_bashable_lookup();
    bool debug_draw_ = false;

};

} // namespace physics
#endif // BOX2D_ENABLED
