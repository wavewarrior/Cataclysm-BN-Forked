#pragma once

/// Window-dimension and mouse-coordinate queries extracted from sdltiles.cpp.
///
/// Every function takes display_context& for the sizing state it needs
/// (WindowWidth, WindowHeight, scaling_factor).  fontwidth/fontheight
/// are accessed as globals (Tier-2 exports from sdltiles.h).
///
/// Declarations remain in their original headers:
///   sdltiles.h     — get_window_dimensions, get_sdl_window_size, get_sdl_font_size
///   cursesport.h    — projected_window_width, projected_window_height, get_scaling_factor
///   output.h       — get_terminal_width, get_terminal_height
///   input.h        — input_context::get_coordinates
///
/// sdl_window_dims.cpp is the single definition site for all of the above.

/// Returns true when the right mouse button is physically held down.
/// In curses builds (no SDL) always returns false.
auto is_rmb_held() -> bool;

/// Returns SDL_GetTicks() in tiles builds; returns 0 in curses builds.
auto get_sdl_ticks() -> uint64_t;

/// Returns the current mouse pixel position via SDL_GetMouseState (live, not last-event).
/// In curses builds returns point_zero.
struct point;
auto get_sdl_mouse_pos() -> point;
