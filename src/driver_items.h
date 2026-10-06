#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "coordinates.h"
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

/// What an observation reports a request did. The executors report `completed`, `refused`,
/// `no_effect` and `unsupported`; the driver loop adds the rest as it watches the world. Death
/// is not a value here: it overrides the outcome of whatever the action itself reported, at the
/// point the response is written.
enum class outcome {
    completed,
    refused,
    no_effect,
    unsupported,
    /// A move the world stopped, with nothing said by the game.
    blocked,
    interrupted,
    awaiting_input,
};

/// The `outcome` member's text for `o`: the word the protocol speaks.
auto outcome_name( outcome o ) -> std::string_view;

/// Which `query` a request asks for.
enum class query_topic {
    inventory,
    effects,
};

/// The `topic` member's text for `topic`: the word the protocol speaks.
auto query_topic_name( query_topic topic ) -> std::string_view;

/// What a typed command did, as far as the executor can tell. The driver adds the time it spent.
struct command_result {
    /// What the command did; `completed` unless it says otherwise.
    outcome outcome = driver_items::outcome::completed;
    /// Why, for `unsupported`.
    std::string_view reason;
    /// The game's message when it rejected the command; empty when it gave none.
    std::string detail;
};

/// Which `query` a request names, if it names one the driver knows.
auto topic_named( const std::string &name ) -> std::optional<query_topic>;

/// Writes the members of the `topic` query into the response object `jo` is inside. Assigns ids
/// to every item it lists. Returns true when it cut a list to stay within the size ceiling.
auto write_query( JsonOut &jo, query_topic topic ) -> bool;

/// Items on `pos` that the avatar could pick up from there: those lying on the tile (none when it
/// is sealed) and in the cargo of a vehicle part on it. For the avatar's own tile these are the
/// items `query inventory` reports as `here`.
auto items_at( const tripoint_bub_ms &pos ) -> std::vector<item *>;

/// `text` cut to the longest name any list entry carries.
auto truncate_name( std::string text ) -> std::string;

/// The id the driver reports for `it`, assigning one the first time. Decimal digits, as a
/// string: the ids carry a 32-bit save prefix, which a JSON number would not hold exactly.
auto issue_id( item &it ) -> std::string;

/// Looks up an id issued earlier in this Episode. The error says why an id cannot be used: it
/// is malformed, was never issued in this Episode, or names an item that no longer exists. Such
/// a request is a protocol error: nothing is acted on.
auto find_item( const std::string &id_text ) -> std::expected<safe_reference<item>, std::string>;

/// Runs `kind` on the item. Spends the avatar's moves as the game does; the caller lets the
/// world catch up. The item may have gone since `find_item`: that is a refusal. A command that
/// starts an activity (`read`, `reload`, `use` for some items) leaves it running for the caller.
auto run_command( command kind, const safe_reference<item> &target,
const command_options &options = {} ) -> command_result;

/// Ok when `recipe` names a recipe the game knows; the error says why not. An unknown recipe id
/// is a protocol error, since an invented id is an agent bug.
auto recipe_error( const std::string &recipe ) -> std::expected<void, std::string>;

/// Starts crafting `recipe` once, from what the avatar has and what is near: the crafting
/// activity is left running for the caller. The game's own checks apply, and so does its pick
/// of components; a craft that would need a choice between components is `unsupported`.
auto run_craft( const std::string &recipe ) -> command_result;

/// Starts trying to fall asleep, as the sleep action does once its menu is answered "yes".
/// The sleep itself is left running for the caller.
auto run_sleep() -> command_result;

} // namespace driver_items
