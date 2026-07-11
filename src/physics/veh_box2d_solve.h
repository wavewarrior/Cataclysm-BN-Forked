#pragma once

#include "point_float.h"  // rl_vec2d — complete type required for value member
#include "vehicle.h"      // veh_collision (defined at vehicle.h:103), vehicle class

#include <map>
#include <vector>

struct vv_solved_body {
    vehicle         *veh;
    rl_vec2d         final_vel_cmps;  // post-collision velocity, cm/s
    float            ang_vel_rads;    // angular velocity, rad/s
    float            impulse_ns;      // impulse magnitude, Ns — feeds damage calculation
};

struct vv_cluster_result {
    std::vector<vv_solved_body> bodies;  // [0] = active vehicle; [1..N] = targets
    float total_impulse_ns;
};

#ifdef BOX2D_ENABLED
/// Solve all vehicles in veh_collisions simultaneously in a transient b2World.
/// Returns post-collision velocities and impulse magnitudes for each participant.
auto solve_vv_cluster( vehicle &veh,
                       const std::map<vehicle *, std::vector<veh_collision>> &veh_collisions )
    -> vv_cluster_result;
#endif
