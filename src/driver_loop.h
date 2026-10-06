#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

/// Line-JSON agent driver: one request line in, one response line out, strictly in order.

/// What the driver loop is told besides the descriptor it serves.
struct driver_options {
    /// The data file listing actions the driver refuses; empty selects the default file under
    /// the data directory.
    std::string deny_list_path;
    /// Where `run_scene` finds Scenes; empty selects the Scene library of the checkout the game
    /// runs from (`tools/visual_verify/scenes` under `--basepath`).
    std::string scenes_dir;
    /// The game has a real window: the loop draws the game and refreshes the display before it
    /// waits for each request, so a fresh frame exists whenever the driver is idle. The command
    /// surface, the deny list and the no-fiber guard are the same as without a window.
    bool windowed = false;
};

/// Serves the driver protocol on an inherited, bidirectional file descriptor.
/// Call after the world is loaded. Returns on `quit` or when the peer closes the descriptor.
/// Returns false, having served nothing, when the deny list cannot be loaded.
auto run_driver_loop( int fd, const driver_options &options ) -> bool;

/// True while the driver serves requests. The input layer must not wait for a key then: nobody
/// is typing, so a read with no modal fiber to answer it would hang the process.
auto driver_mode_active() -> bool;

/// Says why the game cut the avatar's activity or sleep short: one of `monster_in_view`, `pain`,
/// `noise`, `other`. The driver reports the first reason noted while it runs an activity as the
/// `interrupted` outcome's reason. Does nothing unless the driver is serving.
auto driver_note_interruption( std::string_view reason ) -> void;

/// Raised by the input layer instead of waiting for a key nobody can send. The driver loop
/// reports it as `outcome: unsupported`.
struct driver_blocking_read : public std::runtime_error {
    driver_blocking_read();
};
