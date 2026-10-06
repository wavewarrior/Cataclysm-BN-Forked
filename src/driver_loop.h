#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

/// Line-JSON agent driver: one request line in, one response line out, strictly in order.

/// Serves the driver protocol on an inherited, bidirectional file descriptor.
/// Call after the world is loaded. Returns on `quit` or when the peer closes the descriptor.
/// `deny_list_path` names the data file listing actions the driver refuses; empty selects the
/// default file under the data directory. Returns false, having served nothing, when that file
/// cannot be loaded.
auto run_driver_loop( int fd, const std::string &deny_list_path ) -> bool;

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
