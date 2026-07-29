#pragma once
#ifdef BOX2D_ENABLED
#include <box2d/box2d.h>
#include "coordinates.h"
#include "point_float.h"  // rl_vec2d
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace lighting { class debug_line_pass; } // forward-declare GPU line buffer

class Creature;
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
    /// Called when `abs_sm_pos` leaves the *simulated* set.  `submap_still_resident`
    /// must say whether the submap is still in memory: this callback also fires on
    /// simulated -> lazy_border, where the submap and its vehicles are still alive.
    void on_submap_unloaded( const tripoint_abs_sm &abs_sm_pos, bool submap_still_resident );

    // ── Coordinate shift ───────────────────────────────────────────────────
    /// Translate all Box2D bodies by `delta_tiles × TILE_M` metres.
    /// `delta_tiles` = `shift_offset_pt` from `map::shift` (tile units, signed).
    void on_map_shifted( point delta_tiles );

    // ── Z-level transition ────────────────────────────────────────────────
    /// Destroy terrain bodies for old_z and create them for new_z.
    /// Called when the player changes z-level (stairs, ramps, etc.).
    void on_zlevel_changed( const map &m, int old_z, int new_z );

    /// Total number of terrain collider bodies currently registered, across all
    /// submaps and z-levels.  Exists so tests can assert that colliders were
    /// actually built: several code paths gate collider creation on the player's
    /// z-level, and a silent zero here means vehicles pass through walls.
    auto terrain_body_count() const -> size_t; // *NOPAD*

    /// Bodies as Box2D itself sees them.  Differs from `terrain_body_count()` on
    /// purpose: a body dropped from the registry without `b2DestroyBody` still
    /// collides but is invisible to the registry count, so leaks only show here.
    auto world_body_count() const -> size_t; // *NOPAD*

    // ── Phase 5 hooks (wired in Phase 5) ─────────────────────────────────
    /// Remove the Box2D body for the bashable tile at `pos` after it is bashed.
    void on_tile_bashed( tripoint_bub_ms pos );

    // ── Game-loop interface (wired from Phase 8 / Phase 10) ───────────────
    /// Integrate exactly one game turn.  Syncs game velocity into the bodies
    /// once, sub-steps `turn_seconds` with a step size small enough that no
    /// body translates far enough to tunnel through the 1-tile terrain bodies,
    /// then reads physics state back once.
    ///
    /// Sub-stepping (rather than one big step) is required for two reasons:
    ///  - tunneling: a 1-tile-wide static terrain body is missed entirely if a
    ///    body translates more than roughly half a tile within a single step.
    ///  - contact response: syncing velocity in once and out once (instead of
    ///    per step) is what lets a collision actually change the vehicle's
    ///    velocity — a per-step re-sync would overwrite the solver's result.
    void step_turn( float turn_seconds );

    /// Raw single step.  Prefer `step_turn()` from the game loop; this exists
    /// for tests and for callers that need an explicit dt.
    void step( float dt, int substeps );
    void dispatch_contact_events();

    /// RAII guard marking the *physics readback* as the active mover.
    ///
    /// `on_vehicle_moved()` cannot tell from geometry alone whether a tile move
    /// came from the readback (body is already authoritative — leave it alone)
    /// or from an external teleport (body must follow, or `physics_pos` and the
    /// tile anchor diverge permanently).  The readback is the only caller that
    /// means "physics-driven", so it states that intent explicitly here rather
    /// than having the hook guess.
    class physics_move_scope
    {
        public:
            explicit physics_move_scope( PhysicsWorld &w ) : w_( w ) { w_.applying_readback_ = true; }
            ~physics_move_scope() { w_.applying_readback_ = false; }
            physics_move_scope( const physics_move_scope & ) = delete;
            auto operator=( const physics_move_scope & ) -> physics_move_scope & = delete; // *NOPAD*
        private:
            PhysicsWorld &w_;
    };


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

    /// Vehicles whose position authority *this class* revoked because their home
    /// submap left the simulated set while staying resident.  Only these are
    /// re-granted when the submap comes back: a vehicle that cleared
    /// box2d_position_authority itself (e.g. tests/vehicle_ramp_test.cpp does, for
    /// every vehicle it builds) must stay opted out, and is indistinguishable from
    /// an unload revocation by the flag alone.
    std::unordered_set<vehicle *> authority_revoked_by_unload_;

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
    /// Push game-side velocity into the bodies (once per turn, before stepping).
    void sync_bodies_from_game();
    /// Read integrated position/angle/spin back out (once per turn, after stepping).
    void sync_game_from_bodies();
    /// Sub-step count that keeps per-step translation below half a tile.
    auto substeps_for_turn( float turn_seconds ) const -> int; // *NOPAD*
    bool debug_draw_ = false;
    /// Set only while a `physics_move_scope` is alive; see that class.
    bool applying_readback_ = false;

};

} // namespace physics
#endif // BOX2D_ENABLED
