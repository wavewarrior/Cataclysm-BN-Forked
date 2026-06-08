#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <string>

#include "input.h"
#include "sdl_wrappers.h"

class Font;
class GeometryRenderer;

namespace cata_cursesport {
struct curseline;
} // namespace cata_cursesport

//***********************************
// display_context                 *
//***********************************

/// Tier-1 shared SDL core — file-scope singleton bundled into a single
/// owned struct.  Every extracted module receives display_context& by
/// reference (never reaches an ambient global).
struct display_context {
    // Window lifecycle
    SDL_Window_Ptr window;
    SDL_Window_Ptr legacy_window;
    SDL_Renderer_Ptr renderer;
    SDL_PixelFormat format = SDL_PIXELFORMAT_UNKNOWN;
    std::unique_ptr<GeometryRenderer> geometry;
    int WindowWidth = 0;
    int WindowHeight = 0;
    int TERMINAL_WIDTH = 0;
    int TERMINAL_HEIGHT = 0;
    int scaling_factor = 1;
    bool fullscreen = false;

    // Timing / dirty flags
    Uint64 lastupdate = 0;
    uint32_t interval = 25;
    bool needupdate = false;
    bool need_invalidate_framebuffers = false;

    // Input
    input_event last_input;
    int inputdelay = 0;
    Uint64 delaydpad = std::numeric_limits<Uint64>::max();
    Uint64 dpad_delay = 100;
    bool dpad_continuous = false;
    int lastdpad = -1;
    int queued_dpad = -1;
    SDL_Joystick *joystick = nullptr;

    // Fonts
    std::unique_ptr<Font> font;
    std::unique_ptr<Font> map_font;
    std::unique_ptr<Font> overmap_font;

    // Curses dispatch / framebuffer caches
    std::weak_ptr<void> winBuffer;
};

/// The single display_context instance — defined in sdltiles.cpp.
extern display_context g_display;

/// Input processing helpers extracted from sdltiles.cpp.
///
/// Every function takes display_context& for the input state it needs
/// (joystick, dpad timing, window for mouse capture).  No function touches
/// renderer/geometry/fonts/tiles — cleanest decomposition seam.
namespace sdl_input {

/// Poll the gamepad d-pad (joystick hat) and synthesise directional input
/// events.  Handles diagonal delay so quick orthogonal presses produce a
/// diagonal.
/// @return 1 if a d-pad event was synthesised into d.last_input, 0 otherwise.
auto handle_dpad( display_context &d ) -> int;

/// Translate an SDL key press to an ncurses key identifier.
/// @return curses constant (>0), 0 for unknown, or -1 when ALT+number
///         sequence has been started (caller should check add_alt_code).
auto keysym_to_curses( SDL_Keycode sym, SDL_Keymod mod ) -> int;

/// Begin accumulating an ALT+nnnn decimal code (Alt-key down event).
void begin_alt_code();
/// Accumulate one decimal digit into the ALT+nnnn buffer.
/// @return true if the buffer was non-empty and the digit was accumulated.
auto add_alt_code( char c ) -> bool;
/// Finish ALT+nnnn and return the accumulated code (0 if none).
auto end_alt_code() -> int;

/// Arrow-combo diagonal movement helpers (mode1: Ctrl+two-arrow -> numpad).
auto handle_arrow_combo( SDL_Keycode key ) -> int;
void end_arrow_combo();

/// @return true if a joystick/gamepad is available.
auto gamepad_available( const display_context &d ) -> bool;

/// Main event loop — polls SDL events and updates display_context input state.
/// Currently defined in sdltiles.cpp (too coupled to ImGui / debug F-keys /
/// resize to extract fully); this declaration exposes it to the dispatch
/// pipeline below.  Callers always pass g_display.
void CheckMessages( display_context &d );

} // namespace sdl_input
