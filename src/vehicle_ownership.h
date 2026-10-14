#pragma once

#include <string>

class Character;
class avatar;
class vehicle;

/**
 * Who owns a vehicle and what happens when someone else uses it: free functions over the class's
 * raw ownership state (`get_owner`, `get_old_owner`, `stolen_at` and their setters), so a consumer
 * of this header does not have to be rebuilt when the vehicle internals change.
 */
namespace vehicle_ownership {

/// True when `c` belongs to the faction that owns `veh`. An unowned vehicle answers
/// `available_to_take`.
auto is_owned_by(const vehicle& veh, const Character& c, bool available_to_take = false) -> bool;

/// True when `c` belongs to the faction that owned `veh` before it was stolen.
auto is_old_owner(const vehicle& veh, const Character& c, bool available_to_take = false) -> bool;

/// The owning faction's display name, or "no owner".
auto get_owner_name(const vehicle& veh) -> std::string;

auto has_owner(const vehicle& veh) -> bool;
auto has_old_owner(const vehicle& veh) -> bool;

/// Make `c`'s faction the owner of `veh`.
auto set_owner(vehicle& veh, const Character& c) -> void;

struct theft_options {
    /// Only ask whether the avatar could use the vehicle without trouble; change nothing.
    bool check_only = false;
    /// Ask the avatar to confirm before using someone else's vehicle.
    bool prompt = true;
};

/// Decide whether `you` may interact with `veh`, transferring ownership of an unowned or
/// long-stolen vehicle and handling witnesses. Returns whether the avatar is willing to interact.
auto handle_potential_theft(vehicle& veh, avatar& you, const theft_options& opts = {}) -> bool;

} // namespace vehicle_ownership
