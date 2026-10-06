#pragma once

#include <cstddef>
#include <string>
#include <vector>

class JsonOut;

/// The agent driver's `run_scene`: runs a named Lua Scene (a script that builds a situation in
/// the world) at the turn the agent chooses, and reports whether it passed and what it logged.
/// Scenes are plain files in a Scenes directory; the existing lighting Scenes run as they are.
namespace driver_scene
{

/// What the response carries of one run, at most: a Scene's own lines are cut to these, keeping
/// the newest, since a Scene reports its summary last.
constexpr size_t max_lines = 40;
constexpr size_t max_line_bytes = 240;
constexpr size_t max_total_bytes = 2800;

/// The Scenes directory a driver started without one uses: the repository's Scene library,
/// found from the game's `--basepath`.
auto default_dir() -> std::string;

/// True for a name that can only mean one file directly inside the Scenes directory: letters,
/// digits, `_` and `-`.
auto valid_name( const std::string &name ) -> bool;

/// The file `name` is in `dir`, or empty when `dir` holds no such Scene.
auto find( const std::string &dir, const std::string &name ) -> std::string;

/// How a run went.
struct result {
    /// The Scene loaded, ran to its end without raising an error and did not return `false`.
    bool passed = false;
    /// What it logged while it ran (`gdebug.log_info` and the other log functions, `print`), in
    /// order; when it failed by an error, that error's message comes last, after `error: `.
    std::vector<std::string> lines;
};

/// Runs the Scene file at `path` in the game's Lua state, sandboxed as a mod script is: what it
/// assigns to a global is discarded with it. Never throws, and never waits for a key: a Scene
/// that fails in any way reports it in the result. Takes no game time of its own.
auto run( const std::string &path ) -> result;

/// Writes the `scene` member of a response into the object `jo` is inside: `status` (`passed`
/// or `failed`) and `lines`. Cuts the lines to the limits above; returns true when it did.
auto write( JsonOut &jo, result scene ) -> bool;

} // namespace driver_scene
