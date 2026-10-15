#include "vehicle_wait.h"

#include "vehicle.h"
#include "vehicle_dynamics.h"

namespace vehicle_wait {

auto is_wait_blocked_by_movement(const vehicle& veh) -> bool {
    return (veh.velocity != 0 || veh.cruise_velocity != 0)
        && !vehicle_dynamics::is_flying_in_air(veh);
}

auto should_offer_flying_wait_durations(const vehicle& veh) -> bool {
    return vehicle_dynamics::is_flying_in_air(veh);
}

} // namespace vehicle_wait
