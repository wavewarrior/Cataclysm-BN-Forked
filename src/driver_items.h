#pragma once

#include <string>
#include <string_view>

#include "safe_reference.h"

class JsonOut;
class item;

/// The agent driver's avatar-side item executor: stable item ids, the `inventory` and `effects`
/// queries, and the typed item commands. It acts directly on the avatar and never touches the
/// co-op proxy executor. Ids are stable only within an Episode (one driver process).
namespace driver_items
{

enum class command {
    pickup,
    drop,
    wield,
    wear,
    take_off,
    eat,
    use,
    read,
    reload,
};

/// What the typed commands take beyond the item they act on.
struct command_options {
    /// `eat`: the answer to the game's "eat it anyway?" question.
    bool anyway = false;
    /// `use`: which of the item's uses to run; empty when the item has just one.
    std::string method;
};

/// What an item command did, as far as the executor can tell. The driver adds the time it spent.
struct command_result {
    /// An observation `outcome` value: `completed`, `refused`, `no_effect` or `unsupported`.
    std::string_view outcome = "completed";
    /// Why, for `unsupported`.
    std::string_view reason;
    /// The game's message when it rejected the command; empty when it gave none.
    std::string detail;
};

/// An item named by an id, or the reason the id cannot be used.
struct found_item {
    safe_reference<item> ref;
    /// Non-empty when the id is malformed, was never issued in this Episode, or names an item
    /// that no longer exists. Such a request is a protocol error: nothing is acted on.
    std::string error;
};

/// True for the topics `write_query` knows.
auto is_query_topic( const std::string &topic ) -> bool;

/// Writes the members of the `topic` query into the response object `jo` is inside. Assigns ids
/// to every item it lists. Returns true when it cut a list to stay within the size ceiling.
auto write_query( JsonOut &jo, const std::string &topic ) -> bool;

/// The id the driver reports for `it`, assigning one the first time. Decimal digits, as a
/// string: the ids carry a 32-bit save prefix, which a JSON number would not hold exactly.
auto issue_id( item &it ) -> std::string;

/// Looks up an id issued earlier in this Episode.
auto find_item( const std::string &id_text ) -> found_item;

/// Runs `kind` on the item. Spends the avatar's moves as the game does; the caller lets the
/// world catch up. The item may have gone since `find_item`: that is a refusal. A command that
/// starts an activity (`read`, `reload`, `use` for some items) leaves it running for the caller.
auto run_command( command kind, const safe_reference<item> &target,
const command_options &options = {} ) -> command_result;

/// Empty when `recipe` names a recipe the game knows; otherwise why not. An unknown recipe id
/// is a protocol error, since an invented id is an agent bug.
auto recipe_error( const std::string &recipe ) -> std::string;

/// Starts crafting `recipe` once, from what the avatar has and what is near: the crafting
/// activity is left running for the caller. The game's own checks apply, and so does its pick
/// of components; a craft that would need a choice between components is `unsupported`.
auto run_craft( const std::string &recipe ) -> command_result;

/// Starts trying to fall asleep, as the sleep action does once its menu is answered "yes".
/// The sleep itself is left running for the caller.
auto run_sleep() -> command_result;

} // namespace driver_items
