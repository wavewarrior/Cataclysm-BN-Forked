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

#include <optional>

#include "coordinates.h"
#include "units_angle.h"

/// Returns true when the right mouse button is physically held down, as tracked
/// from the SDL event stream by sdl_input.
auto is_rmb_held() -> bool;

/// SDL_GetTicks(), in milliseconds.
auto get_sdl_ticks() -> uint64_t;

/// Returns the current mouse pixel position via SDL_GetMouseState (live, not last-event).
///
/// CAUTION: SDL only reports this while a window holds mouse focus. Inside the
/// ranged-targeting input loop it comes back (0, 0) — pair it with
/// get_tracked_mouse_pos() there.
auto get_sdl_mouse_pos() -> point;

/// The mouse pixel position input_manager recorded from the last real mouse event.
/// Survives input loops where SDL reports no mouse focus, at the cost of being a
/// frame or two stale.
auto get_tracked_mouse_pos() -> point;

/// The mouse pixel the aiming overlays should follow: the live SDL position when
/// a window holds mouse focus, otherwise the last tracked motion event. Modal
/// loops (ranged targeting) need the fallback; everything else prefers live.
auto get_aim_mouse_pos() -> point;

/// Angle from `src`'s tile centre to a screen pixel, in world space (screen +x is
/// 0, screen +y is +pi/2 — the convention map::ray_cast_angle uses).
///
/// Free function rather than an input_context method because free-aim samples the
/// pointer every tick: input_context::get_aim_angle_to_src only answers after an
/// input event set `coordinate_input_received`, which never happens while the
/// player merely holds RMB and moves the mouse.
///
/// nullopt when the pixel resolves onto the source tile centre (no direction).
auto aim_angle_from_pixel( point pixel, const tripoint_bub_ms &src )
-> std::optional<units::angle>;
