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
};

/// The single display_context instance — defined in sdltiles.cpp.
extern display_context g_display;

/// Canonical font cell dimensions — defined in sdltiles.cpp.
extern int fontwidth;
extern int fontheight;
