#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

class JsonOut;

/// The agent driver's `capture` (windowed mode only): writes the frame the game draws, and the
/// map snapshot of the same turn, into a directory the caller owns, and says how big the frame is.
/// Files are named `turn-<turn>-<n>-final.bmp` or `-state.png` and `turn-<turn>-<n>-map.json`,
/// where `n` counts the captures this process has written, so several captures of one turn (a
/// null frame, a toggled one, a restored one) each keep their own. It takes no game time.
namespace driver_capture
{

/// What a capture shows.
enum class mode {
    /// The final composite that goes to the window, lighting included: a BMP dumped from the
    /// swapchain. Needs a drawable.
    composite,
    /// An offscreen re-render of the drawn state that skips the lighting tonemap and the
    /// interface passes: a PNG. A state view, not a lighting oracle.
    state,
};

/// A request that has been checked: `dir` is absolute and `turn` names the files.
struct request {
    mode kind = mode::composite;
    std::string dir;
    int turn = 0;
};

/// A request read from the wire, or why it cannot be served. `mode_text` is the text of the
/// request's `mode`; empty when it carried none.
struct parsed_request {
    std::optional<request> value;
    std::string error;
};
auto parse_request( std::string_view dir, std::optional<std::string_view> mode_text,
                    int turn ) -> parsed_request;

/// What a capture wrote.
struct files {
    mode kind = mode::composite;
    std::string frame;
    std::string map;
    /// The frame's own size in pixels: the swapchain's for a final composite, the window's for
    /// a state view.
    int width = 0;
    int height = 0;
    /// The window's size in logical pixels; the frame is this large on a display with one device
    /// pixel per logical pixel, and larger on a HiDPI display.
    int window_width = 0;
    int window_height = 0;
};

/// How a capture ended: it wrote its files, or the window had no drawable to capture from, or it
/// failed (`error`). A refused or failed capture leaves no file of its own and never reports one
/// from an earlier capture.
struct result {
    std::optional<files> written;
    bool no_drawable = false;
    std::string error;
};

/// Captures what the game shows at this turn. `draw` brings the window up to date and presents
/// it (the driver's per-frame draw); a final capture arms a one-shot dump of that frame to a
/// path of this request, a state capture re-renders what `draw` queued. With no drawable nothing
/// is written: the window is hidden or minimised, or (final only, a state view is offscreen)
/// occluded, as it is while the screen is locked, or the display gave no frame.
auto capture( const request &req, const std::function<void()> &draw ) -> result;

/// Writes the `capture` member of a response into the object `out` is inside.
auto write( JsonOut &out, const files &written ) -> void;

/// The size of the file a state view wrote, in pixels.
struct state_view_size {
    int width = 0;
    int height = 0;
};

} // namespace driver_capture

// What the render side provides for `driver_capture`; each is defined beside the thing it uses.

/// Re-renders the drawn state into an offscreen texture that has no swapchain, skipping the
/// lighting tonemap and the interface passes, and saves it as a PNG at `png_path` (sdltiles.cpp,
/// beside `save_screenshot`). Empty when the render state is not ready or the file cannot be
/// written.
auto save_state_view( const std::string &png_path ) ->
std::optional<driver_capture::state_view_size>;

namespace lighting
{

/// What the one-shot swapchain dump armed by `arm_frame_capture` came to.
struct frame_capture_report {
    bool written = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    /// The device's frame counter at the dumped frame.
    std::uint64_t frame = 0;
    std::string error;
};

/// Asks the next submitted frame, and only that one, to be dumped to a BMP at `path` (gpu_device.cpp).
/// The file appears whole or not at all. Unlike the F13 and file-trigger dumps it takes a path of
/// the caller's and writes no global file, nor the map snapshot.
auto arm_frame_capture( std::string path ) -> void;

/// Disarms the request and returns how it went; empty when no frame was submitted since it was
/// armed (the window gave no drawable).
auto take_frame_capture() -> std::optional<frame_capture_report>;

} // namespace lighting

namespace sdl_lighting_devui
{

/// Writes the map snapshot (the CATA_MAP_DUMP JSON) of `frame` to `path` (sdl_lighting_devui.cpp).
/// Returns false when the file cannot be written.
auto dump_map_to( std::uint64_t frame, const std::string &path ) -> bool;

} // namespace sdl_lighting_devui
