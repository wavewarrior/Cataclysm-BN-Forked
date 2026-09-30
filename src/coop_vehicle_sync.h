#pragma once
#include "coordinates.h"
#include <cstdint>
#include <vector>

class JsonIn;
class JsonOut;
class map;
class vehicle;

/// One vehicle's host-authoritative pose, sent host → client in every sync (`"vehicles"`).
struct coop_vehicle_pose {
    std::uint32_t vid = 0;
    tripoint_abs_ms anchor;     ///< vehicle::abs_ms_location()
    float frac_x = 0.0f;        ///< physics_pos − bub anchor (tiles); 0 without authority
    float frac_y = 0.0f;
    float angle = 0.0f;         ///< physics_angle (rad)
    int face_deg = 0;           ///< lround(to_degrees(face.dir()))
    int steer_deg = 0;          ///< lround(to_degrees(turn_dir))
    int velocity = 0;
    int cruise_velocity = 0;
    bool engine_on = false;
    bool authority = false;     ///< box2d_position_authority
    bool host_driving = false;  ///< host avatar drives it (client: partner-driven)
    auto operator==( const coop_vehicle_pose & ) const -> bool = default; // *NOPAD*
};
auto make_coop_vehicle_pose( const vehicle &veh, std::uint32_t vid,
                             bool host_driving ) -> coop_vehicle_pose;
/// Writes the array value only; caller writes the member name.
auto write_coop_vehicle_poses( JsonOut &jout, const std::vector<coop_vehicle_pose> &poses ) -> void;
auto read_coop_vehicle_poses( JsonIn &jin ) -> std::vector<coop_vehicle_pose>;
/// Apply `pose` to this side's copy of the vehicle. Returns true when its part layout
/// rotated (caller then calls map::vehicle_footprint_changed once for it).
auto apply_coop_vehicle_pose( map &here, vehicle &veh, const coop_vehicle_pose &pose ) -> bool;
/// Vehicle the co-op partner drives, as seen on this side; nullptr outside co-op or when none.
auto coop_partner_driven_vehicle() -> const vehicle *; // *NOPAD*
