#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <string>

#include "input.h"
#include "sdl_wrappers.h"

class Font;
class GeometryRenderer;

namespace cata_cursesport
{
struct curseline;
} // namespace cata_cursesport

//***********************************
// display_context                 *
//***********************************

/// Shared SDL core singleton; sdl_input helpers take it by ref.
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
    // Minimum ms between input-driven redraws in try_sdl_update(). Overwritten
    // at window init from the display's actual refresh rate (see WinCreate in
    // sdl_window.cpp); this default only applies if that query fails. The old
    // value of 25 was a curses-era FRAMERATE leftover that measured as a hard
    // 40 fps ceiling (39-40 fps observed in gameplay) — 16 makes the fallback
    // ceiling 60 fps instead.
    uint32_t interval = 16;
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
};

/// The single display_context instance — defined in sdltiles.cpp.
extern display_context g_display;

/// Canonical font cell dimensions — defined in sdltiles.cpp.
extern int fontwidth;
extern int fontheight;
