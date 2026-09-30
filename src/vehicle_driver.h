#pragma once

class Character;
class vehicle;

/// The character operating this vehicle's controls: the avatar (in person or by remote),
/// else any boarded character at CONTROLS with controlling_vehicle set (the co-op proxy).
/// nullptr when nobody drives.
auto vehicle_driver( const vehicle &veh ) -> const Character *; // *NOPAD*
