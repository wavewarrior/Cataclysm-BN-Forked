#pragma once

#include <optional>
#include <string_view>

/// The window of the windowed driver boot (`--driver-windowed WxH`): a real, visible game window
/// of a fixed size, placed in a corner of the screen, opened without taking focus. The windowless
/// driver has no window and none of this applies.

/// A window size in logical pixels.
struct driver_window_size {
    int width = 0;
    int height = 0;
};

/// A rectangle of the desktop in global screen coordinates.
struct screen_area {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct screen_position {
    int x = 0;
    int y = 0;
};

/// Reads `<width>x<height>`: two positive whole numbers, nothing else.
auto parse_driver_window_size( std::string_view text ) -> std::optional<driver_window_size>;

/// Where a window of `size` goes so that it sits in the bottom right corner of `usable` (the
/// screen without the menu bar and dock). A window larger than the area is pinned to its top
/// left corner instead, so its title bar stays reachable.
auto driver_window_corner( const screen_area &usable,
                           const driver_window_size &size ) -> screen_position;

/// Asks the interface init to open the game window as the driver's window. Call before
/// `init_interface`.
auto request_driver_window( const driver_window_size &size ) -> void;

/// The window `request_driver_window` asked for; empty when the boot is not windowed.
auto requested_driver_window() -> std::optional<driver_window_size>;
