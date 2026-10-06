#include "driver_capture.h"

#include <filesystem>
#include <string>
#include <system_error>

#include "json.h"
#include "lighting/gpu_device.h"
#include "lighting/render_state.h"

namespace driver_capture
{

namespace
{

auto mode_name( mode kind ) -> std::string
{
    return kind == mode::composite ? "final" : "state";
}

/// What a reader of the response should take the frame for.
auto mode_label( mode kind ) -> std::string
{
    return kind == mode::composite ? "final composite" : "state view";
}

/// Moves `from` onto `to` whole: a reader never sees half a file at `to`.
auto publish( const std::string &from, const std::string &to ) -> bool
{
    std::error_code ec;
    std::filesystem::rename( from, to, ec );
    if( ec ) {
        std::filesystem::remove( from, ec );
        return false;
    }
    return true;
}

/// How many captures this process has written: the next one's name carries one more, so the
/// captures of a single turn (a null frame and a toggled one) never overwrite each other.
int captures_written = 0;

/// The window cannot give a frame while it is hidden or minimised. A final composite also needs
/// it to be seen at all: an occluded window (covered whole, on another Space, the screen locked or
/// asleep) has no drawable to dump from. A state view is drawn offscreen and does not.
auto window_has_drawable( SDL_Window *window, mode kind ) -> bool
{
    SDL_WindowFlags unusable = SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED;
    if( kind == mode::composite ) {
        unusable |= SDL_WINDOW_OCCLUDED;
    }
    return window != nullptr && ( SDL_GetWindowFlags( window ) & unusable ) == 0;
}

} // namespace

// *INDENT-OFF*
auto parse_request( std::string_view dir, std::optional<std::string_view> mode_text,
                    int turn ) -> parsed_request
{
    const std::filesystem::path where( dir );
    if( dir.empty() || !where.is_absolute() ) {
        return { .error = "dir must be an absolute directory path" };
    }
    mode kind = mode::composite;
    if( mode_text && *mode_text == "state" ) {
        kind = mode::state;
    } else if( mode_text && *mode_text != "final" ) {
        return { .error = "mode must be final or state" };
    }
    return { .value = request{ .kind = kind, .dir = std::string( dir ), .turn = turn } };
}
// *INDENT-ON*

auto capture( const request &req, const std::function<void()> &draw ) -> result
{
    auto &rs = lighting::get_render_state();
    SDL_Window *window = rs.ready() ? rs.device().window_ptr() : nullptr;
    if( !window_has_drawable( window, req.kind ) ) {
        return { .no_drawable = true };
    }

    std::error_code ec;
    std::filesystem::create_directories( req.dir, ec );
    if( ec ) {
        return { .error = "cannot create " + req.dir + ": " + ec.message() };
    }

    const bool composite = req.kind == mode::composite;
    const std::string stem = req.dir + "/turn-" + std::to_string( req.turn ) + "-" +
                             std::to_string( captures_written + 1 ) + "-";
    const std::string frame_path = stem + ( composite ? "final.bmp" : "state.png" );
    const std::string map_path = stem + "map.json";

    int width = 0;
    int height = 0;
    std::uint64_t frame = 0;
    if( composite ) {
        // One frame is dumped, the one `draw` presents; if it presents none there is no drawable,
        // and the request is disarmed so that no later frame can be taken for this one.
        lighting::arm_frame_capture( frame_path );
        draw();
        const std::optional<lighting::frame_capture_report> dumped = lighting::take_frame_capture();
        if( dumped && dumped->written && !window_has_drawable( window, req.kind ) ) {
            // `draw` pumps the window's events: a window that went away since the request
            // arrived shows itself only now, and what it dumped is not to be trusted.
            std::filesystem::remove( frame_path, ec );
            return { .no_drawable = true };
        }
        if( !dumped ) {
            return { .no_drawable = true };
        }
        if( !dumped->written ) {
            return { .error = "the frame could not be dumped: " + dumped->error };
        }
        width = static_cast<int>( dumped->width );
        height = static_cast<int>( dumped->height );
        frame = dumped->frame;
    } else {
        draw();
        // `draw` pumps the window's events: a window hidden or minimised since the request
        // arrived shows itself only now.
        if( !window_has_drawable( window, req.kind ) ) {
            return { .no_drawable = true };
        }
        const std::string part = frame_path + ".part";
        const std::optional<state_view_size> saved = save_state_view( part );
        if( !saved ) {
            std::filesystem::remove( part, ec );
            return { .error = "the state view could not be rendered or saved" };
        }
        if( !publish( part, frame_path ) ) {
            return { .error = "cannot write " + frame_path };
        }
        width = saved->width;
        height = saved->height;
        frame = rs.device().frame_count();
    }

    // The snapshot is of the same turn: the game has not moved since `draw`.
    const std::string map_part = map_path + ".part";
    if( !sdl_lighting_devui::dump_map_to( frame, map_part ) || !publish( map_part, map_path ) ) {
        // A frame without its map is half a capture: take it back.
        std::filesystem::remove( frame_path, ec );
        return { .error = "cannot write the map snapshot " + map_path };
    }

    ++captures_written;
    int window_width = 0;
    int window_height = 0;
    SDL_GetWindowSize( window, &window_width, &window_height );
    return { .written = files{
            .kind = req.kind,
            .frame = frame_path,
            .map = map_path,
            .width = width,
            .height = height,
            .window_width = window_width,
            .window_height = window_height,
        }
    };
}

auto write( JsonOut &out, const files &written ) -> void
{
    out.member( "capture" );
    out.start_object();
    out.member( "mode", mode_name( written.kind ) );
    out.member( "label", mode_label( written.kind ) );
    out.member( "frame", written.frame );
    out.member( "map", written.map );
    out.member( "width", written.width );
    out.member( "height", written.height );
    out.member( "window_width", written.window_width );
    out.member( "window_height", written.window_height );
    out.end_object();
}

} // namespace driver_capture
