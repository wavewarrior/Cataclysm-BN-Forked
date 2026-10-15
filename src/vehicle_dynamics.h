#pragma once

#include "coordinates.h"
#include "units_angle.h"
#include "units_mass.h"

#include <string>

class vehicle;

/**
 * The movement model of a vehicle: mass and pivot, power, acceleration and velocity limits,
 * drag, buoyancy, lift, traction, and the predicates built on them.
 *
 * Free functions over the accessors of `vehicle`, so changing the model does not rebuild every
 * translation unit that includes `vehicle.h`.
 */
namespace vehicle_dynamics {

// Get combined power of all engines, the ideal amount of power, not the current power
auto ideal_engine_power(const vehicle& veh, bool safe = false) -> int;

// Get combined power of all engines. If fueled == true, then only engines which
// vehicle have fuel for are accounted.  If safe == true, then limit engine power to
// their safe power.
auto total_power_w(const vehicle& veh, bool fueled = true, bool safe = false) -> int;

// Get ground acceleration gained by combined power of all engines. If fueled == true,
// then only engines which the vehicle has fuel for are included
auto ground_acceleration(
    const vehicle& veh, bool fueled = true, int at_vel_in_vmi = -1, bool ideal = false) -> int;

// Get water acceleration gained by combined power of all engines. If fueled == true,
// then only engines which the vehicle has fuel for are included
auto water_acceleration(
    const vehicle& veh, bool fueled = true, int at_vel_in_vmi = -1, bool ideal = false) -> int;

// get air acceleration gained by combined power of all engines. If fueled == true,
// then only engines which the vehicle hs fuel for are included
auto aircraft_acceleration(
    const vehicle& veh, bool fueled = true, int at_vel_in_vmi = -1, bool ideal = false) -> int;

// Get acceleration for the current movement mode
auto acceleration(const vehicle& veh, bool fueled = true, int at_vel_in_vmi = -1) -> int;

// Get the vehicle's actual current acceleration
auto current_acceleration(const vehicle& veh, bool fueled = true) -> int;

// is the vehicle currently moving?
auto is_moving(const vehicle& veh) -> bool;

// can the vehicle use rails?
auto can_use_rails(const vehicle& veh) -> bool;

// Get maximum ground velocity gained by combined power of all engines.
// If fueled == true, then only the engines which the vehicle has fuel for are included
auto max_ground_velocity(const vehicle& veh, bool fueled = true, bool ideal = false) -> int;

// Get maximum water velocity gained by combined power of all engines.
// If fueled == true, then only the engines which the vehicle has fuel for are included
auto max_water_velocity(const vehicle& veh, bool fueled = true, bool ideal = false) -> int;

// get maximum air velocity based on rotor physics
auto max_air_velocity(const vehicle& veh, bool fueled = true, bool ideal = false) -> int;

// Get maximum velocity for the current movement mode
auto max_velocity(const vehicle& veh, bool fueled = true, bool ideal = false) -> int;

// Get maximum reverse velocity for the current movement mode
auto max_reverse_velocity(const vehicle& veh, bool fueled = true, bool ideal = false) -> int;

// Get safe ground velocity gained by combined power of all engines.
// If fueled == true, then only the engines which the vehicle has fuel for are included
auto safe_ground_velocity(const vehicle& veh, bool fueled = true, bool ideal = false) -> int;

// get safe air velocity gained by combined power of all engines.
// if fueled == true, then only the engines which the vehicle hs fuel for are included
auto safe_aircraft_velocity(const vehicle& veh, bool fueled = true, bool ideal = false) -> int;

// Get safe water velocity gained by combined power of all engines.
// If fueled == true, then only the engines which the vehicle has fuel for are included
auto safe_water_velocity(const vehicle& veh, bool fueled = true, bool ideal = false) -> int;

// Get maximum velocity for the current movement mode
auto safe_velocity(const vehicle& veh, bool fueled = true) -> int;

/**
 * Calculates the sum of the area under the wheels of the vehicle.
 */
auto wheel_area(const vehicle& veh) -> int;

// average off-road rating for displaying off-road performance
auto average_or_rating(const vehicle& veh) -> float;

/**
 * coefficient of airship balloon drag
 */
auto coeff_balloon_drag(const vehicle& veh) -> double;

/**
 * maximum possible buoyancy in Newtons.
 *
 * buoyancy force = V * D * g
 *
 * V: total volume of the vehicle (because it's maximally submerged)
 * D: density of submerged fluid (in our case, water)
 * g: force of gravity
 *
 * @return The max buoyancy in Newtons.
 */
auto max_buoyancy(const vehicle& veh) -> double;

/**
 * can_float
 * does the vehicle have freeboard or does it overflow with water?
 */
auto can_float(const vehicle& veh) -> bool;

/**
 * is the vehicle mostly in water or mostly on fairly dry land?
 */
auto is_in_water(const vehicle& veh, bool deep_water = false) -> bool;

auto is_watercraft(const vehicle& veh) -> bool;

/**
 * is the vehicle flying? is it an aircraft?
 */
auto is_aircraft(const vehicle& veh) -> bool;

/**
 * does the vehicle use rotors?
 */
auto is_rotorcraft(const vehicle& veh) -> bool;

/**
 * does the vehicle have lift-generating parts?
 */
auto has_lift(const vehicle& veh) -> bool;

/**
 * total area of every rotors in m^2
 */
auto total_rotor_area(const vehicle& veh) -> double;

/**
 * lift of balloons in newtons
 */
auto total_balloon_lift(const vehicle& veh) -> double;

/**
 * lift from wings in newtons
 */
auto total_wing_lift(const vehicle& veh) -> double;

/**
 * speed needed for aircraft takeoff
 */
auto get_takeoff_speed(const vehicle& veh, std::string speed_type = "default") -> int;

/**
 * total area of every propeller in m^2
 */
auto total_propeller_area(const vehicle& veh) -> double;

/**
 * lift of rotorcraft in newton
 */
auto thrust_of_rotorcraft(const vehicle& veh, bool fuelled, bool safe = false, bool ideal = false)
    -> double;

/**
 * foward thrust of propellers in newtons
 */
auto foward_thrust_of_propellers(
    const vehicle& veh, bool fuelled, bool safe = false, bool ideal = false) -> double;

/**
 * total foward thrust of all airborn pushers
 */
auto total_thrust(const vehicle& veh, bool fuelled, bool safe = false, bool ideal = false)
    -> double;

/**
 * total lift of all lifters
 */
auto total_lift(
    const vehicle& veh, bool fuelled, bool safe = false, bool ideal = false, bool unpowered = false,
    bool idle = false) -> double;

auto has_sufficient_lift(const vehicle& veh, bool unpowered = false, bool idle = false) -> bool;

auto get_lift_percent(const vehicle& veh, bool unpowered = false) -> double;

auto get_z_change(const vehicle& veh) -> int;

auto is_flying_in_air(const vehicle& veh) -> bool;

/**
 * Traction coefficient of the vehicle.
 * 1.0 on road. Outside roads, depends on mass divided by wheel area
 * and the surface beneath wheels.
 *
 * Affects safe velocity, acceleration and handling difficulty.
 */
auto k_traction(const vehicle& veh, float wheel_traction_area) -> float;

// Extra drag on the vehicle from components other than wheels.
// @param actual is current drag if true or nominal drag otherwise
auto static_drag(const vehicle& veh, bool actual = true) -> int;

// strain of engine(s) if it works higher that safe speed (0-1.0)
auto strain(const vehicle& veh) -> float;

// Calculate if it can move using its wheels
auto sufficient_wheel_config(const vehicle& veh) -> bool;

auto balanced_wheel_config(const vehicle& veh) -> bool;

auto valid_wheel_config(const vehicle& veh) -> bool;

// return the relative effectiveness of the steering (1.0 is normal)
// <0 means there is no steering installed at all.
auto steering_effectiveness(const vehicle& veh) -> float;

/** Returns roughly driving skill level at which there is no chance of fumbling. */
auto handling_difficulty(const vehicle& veh) -> float;

} // namespace vehicle_dynamics
