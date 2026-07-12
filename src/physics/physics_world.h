#pragma once
#ifdef BOX2D_ENABLED
#include <box2d/box2d.h>
#include "coordinates.h"
#include "point_float.h"  // rl_vec2d
#include <map>
#include <unordered_map>
#include <vector>
struct SDL_Renderer; // forward-declare for draw_debug(); no SDL header pulled in here

struct vehicle;
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
    void on_vehicle_removed( vehicle *v );

    // ── Terrain lifecycle ──────────────────────────────────────────────────
    /// Called after `grid[idx] = sm` in `map::on_submap_loaded`.
    void on_submap_loaded( const map &m, const tripoint_abs_sm &abs_sm_pos );
    /// Called before `grid[idx] = nullptr` in `map::on_submap_unloaded`.
    void on_submap_unloaded( const tripoint_abs_sm &abs_sm_pos );

    // ── Coordinate shift ───────────────────────────────────────────────────
    /// Translate all Box2D bodies by `delta_tiles × TILE_M` metres.
    /// `delta_tiles` = `shift_offset_pt` from `map::shift` (tile units, signed).
    void on_map_shifted( point delta_tiles );

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
    /// Render collision shapes, contact manifolds, and body transforms via SDL.
    /// Call from cata_tiles::draw() after all tiles are flushed; skip in iso mode.
    /// Camera parameters derived from cata_tiles locals:
    ///   origin_p{x,y} = op.{x,y} - o.{x,y}() * tile_{width,height}
    ///   m2p{x,y}      = tile_{width,height} / TILE_M
    auto draw_debug( SDL_Renderer *renderer,
                     float origin_px, float origin_py,
                     float m2px,      float m2py ) const -> void;
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

    auto make_vehicle_body( vehicle &v ) -> b2BodyId;
    void rebuild_bashable_lookup();
    bool debug_draw_ = false;

};

} // namespace physics
#endif // BOX2D_ENABLED
