#pragma once
#include <array>
#include <cstdint>

class vehicle;

/// A continuous position in bubble-tile units; integer values are tile centres.
struct vehicle_render_point { float x = 0.0f; float y = 0.0f; };

/// Where a vehicle's mount grid sits this frame, continuously. Always anchored at mount (0,0).
///
/// Two layouts exist in the engine and this picks between them:
///  * Under Box2D position authority, `part.precalc[0]` is rewritten every physics step by
///    `vehicle::refresh_precalc( physics_angle )`, which rotates the mount about mount (0,0)
///    with `lround`. That is the continuous frame reproduced here.
///  * Otherwise the tile-step mover owns `precalc[0]` via `rotate_to_world`, which is exact at
///    0/90/180/270 degrees and sheared at the intermediate 15-degree steps.
/// Note that a moving authority vehicle whose tile anchor stepped this turn has `precalc[1]`
/// (the discrete layout) swapped into `precalc[0]` by `displace_vehicle`, so the authority
/// branch only reproduces the tiles while the anchor is still.
struct vehicle_render_frame {
    float angle =
        0.0f;    ///< radians, clockwise on screen (y down); 0 = mount +x points east. Same unit as sprite_instance::rotation.
    float origin_x = 0.0f; ///< continuous bubble-tile position of mount (0,0)'s tile centre
    float origin_y = 0.0f;
    int mount_min_x = 0;   ///< bounds over veh.all_standalone_parts()
    int mount_min_y = 0;
    int mount_max_x = 0;
    int mount_max_y = 0;
};
auto make_vehicle_render_frame( const vehicle &veh ) -> vehicle_render_frame;
auto vehicle_mount_to_bubble( const vehicle_render_frame &f, float mx,
                              float my ) -> vehicle_render_point;

/// Render-only pose relative to the vehicle's committed tile anchor (tiles, radians).
struct vehicle_render_pose {
    float x = 0.0f;
    float y = 0.0f;
    float angle = 0.0f;
    auto operator==( const vehicle_render_pose & ) const -> bool = default; // *NOPAD*
};

enum class vehicle_motion_mode : std::uint8_t {
    ease,       ///< single-player: exponential follow at the camera rate
    tick_paced, ///< co-op: constant-speed segment per committed pose, lasting the last tick interval
};
struct vehicle_motion_state {
    vehicle_render_pose shown;    ///< pose drawn last update
    vehicle_render_pose seg_from; ///< tick_paced segment start
    vehicle_render_pose seg_to;   ///< last committed target seen
    double seg_start = 0.0;       ///< wall seconds
    double seg_len = 1.0;         ///< seconds
    double last_commit = 0.0;     ///< wall seconds of the previous target change
    double last_update = 0.0;     ///< wall seconds of the previous advance
};
struct vehicle_motion_options {
    vehicle_render_pose target;
    double now = 0.0;             ///< wall seconds
    vehicle_motion_mode mode = vehicle_motion_mode::ease;
    float rate = 12.0f;           ///< 1/s, ease mode (camera_dbg::smooth_speed)
    float snap_tiles = 64.0f;     ///< |target - shown| beyond this on either axis snaps (teleport)
};
auto reset_vehicle_motion( vehicle_motion_state &state, const vehicle_render_pose &pose,
                           double now ) -> void;
/// Shift every stored pose by (dx, dy) tiles because the committed anchor moved by (-dx, -dy).
auto rebase_vehicle_motion( vehicle_motion_state &state, float dx, float dy ) -> void;
/// Advance to `opts.now`; returns the pose to draw (also stored in state.shown).
auto advance_vehicle_motion( vehicle_motion_state &state,
                             const vehicle_motion_options &opts ) -> vehicle_render_pose;

struct vehicle_path_options {
    vehicle_render_frame frame;
    float steer_angle = 0.0f; ///< radians, frame.angle convention (units::to_radians( veh.turn_dir ))
    int velocity =
        0;         ///< cm/s; < 0 = reversing: path leaves the rear edge, travelling backwards
};
struct vehicle_path_band {
    static constexpr int samples = 24;
    std::array<vehicle_render_point, samples> centre;
    std::array<vehicle_render_point, samples> left;  ///< centre - normal * width/2
    std::array<vehicle_render_point, samples>
    right; ///< centre + normal * width/2, normal = (-tangent.y, tangent.x)
    float width = 0.0f;                              ///< tiles = mount_max_y - mount_min_y + 1
};
auto make_vehicle_path_band( const vehicle_path_options &opts ) -> vehicle_path_band;
