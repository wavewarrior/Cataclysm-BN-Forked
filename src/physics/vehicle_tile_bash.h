#pragma once
#include "coordinates.h"

class vehicle;

namespace physics {

/// Bash the bashable terrain/furniture at `p` using `veh`'s current momentum, reusing
/// the impulse/damage model from `vehicle::part_collision`'s terrain branch. Used by
/// the Box2D contact-event dispatch, which detects a vehicle touching a bashable tile
/// directly rather than through the per-tile readback walk that `part_collision`
/// normally runs in (see PhysicsWorld::dispatch_contact_events).
///
/// A free function (not a `vehicle` member) so this header stays a pure leaf: vehicle.h
/// has 178 includers project-wide, and adding a member there would force a near-full
/// rebuild for every change to this bash-dispatch helper.
///
/// Returns true iff the bash succeeded (tile changed).
auto bash_vehicle_tile( vehicle &veh, const tripoint_bub_ms &p ) -> bool;

} // namespace physics
