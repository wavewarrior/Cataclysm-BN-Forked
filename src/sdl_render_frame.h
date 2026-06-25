#pragma once

/// Toggle: show the FPS overlay (RmlUi HUD text) in composite_swapchain_pass_b.
/// Defined in sdl_render_frame.cpp; flipped by game::toggle_debug_fps().
/// The rolling averages it displays are file-static in sdl_render_frame.cpp —
/// no out-of-TU consumer, so they are intentionally not exported here.
extern bool g_show_fps;
