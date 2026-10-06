#pragma once

#include <optional>
#include <string>

#include "coordinates.h"
#include "driver_items.h"

class JsonObject;

/// The agent driver's avatar-side combat executor: the typed `melee`, `fire` and `smash`
/// commands. It acts directly on the avatar and never touches the co-op proxy executor. The
/// target is a compass direction (`dir`) or a tile offset from the avatar (`pos`, as
/// `[dx, dy]`, east and south positive).
namespace driver_combat
{

enum class command {
    melee,
    fire,
    smash,
};

/// The combat command a request's `cmd` names, if it names one.
auto command_named( const std::string &name ) -> std::optional<command>;

/// Where a request points. A request with a malformed or unusable target carries an `error`:
/// that is a protocol error, since an invented direction or position is an agent bug.
struct parsed_target {
    /// How the target is named: a direction (firing along it) or an exact tile.
    bool by_direction = false;
    /// The tile, as an offset from the avatar; for a direction, one step along it.
    tripoint_rel_ms offset;
    std::string error;
};

/// Reads `dir` or `pos` from the request. Exactly one must be present. Melee and smash reach
/// one tile around the avatar; fire reaches as far as the loaded map goes. Throws, like any
/// bad request, when a member has the wrong JSON type.
auto parse_target( const JsonObject &jo, command kind ) -> parsed_target;

/// Runs `kind` against the target. Spends the avatar's moves as the game does; the caller lets
/// the world catch up. A smash that starts an activity (pulping a corpse) leaves it running for
/// the caller. `outcome` is `completed`, `refused` (the game's rules said no: `detail` says why)
/// or, when the game asked a question only a menu could answer, left to the caller's guard.
auto run_command( command kind, const parsed_target &target ) -> driver_items::command_result;

} // namespace driver_combat
