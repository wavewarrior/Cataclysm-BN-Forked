#pragma once

/// Toggle: show the FPS overlay (RmlUi HUD text) in composite_swapchain_pass_b.
/// Defined in sdl_render_frame.cpp; flipped by game::toggle_debug_fps().
/// The rolling averages it displays are file-static in sdl_render_frame.cpp —
/// no out-of-TU consumer, so they are intentionally not exported here.
extern bool g_show_fps;

namespace lighting
{
class render_state;
struct frame_context;
} // namespace lighting

class frame_executor;

/// Dispatches a frame plan against a live render state: pulls steps from `exec` and runs
/// each step's body, in plan order. `refresh_display` calls it after acquiring the frame
/// context; the GPU-lane render test calls it with its own plan, report and executor, so
/// the order the plan asserts is the order the real pass bodies run (issue 130).
/// Timings and skip records land in `exec`'s report; the caller owns `exec.finish()`.
auto run_frame_plan( lighting::render_state &rs, lighting::frame_context &ctx,
                     frame_executor &exec ) -> void;
