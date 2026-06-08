#pragma once

#include <optional>

#include "lighting/gpu_device.h"

namespace lighting {

class render_state;

} // namespace lighting

/// Begin a new frame: test_mode guard, lazy ImGui init, begin_frame, acquire ctx.
auto begin_frame( lighting::render_state &rs ) -> std::optional<lighting::frame_context>;

/// Build lighting: per-tile dirty-gate + cursor light emit + build_and_submit.
auto build_lighting( lighting::render_state &rs ) -> bool;

/// Flush uploads, gather radiance cascade, and do dev RC readback.
auto flush_and_gather_rc( lighting::render_state &rs, lighting::frame_context &ctx,
                          bool rc_rebuild ) -> void;

/// Assemble frame lighting inputs (camera, sun/sky, weather, debug, volumetric params).
auto assemble_light_inputs( lighting::render_state &rs, lighting::frame_context &ctx ) -> void;

/// Push main-menu decorative background sprite when no world is loaded.
auto maybe_push_menu_background( lighting::render_state &rs,
                                 lighting::frame_context &ctx ) -> void;

/// Draw HUD overlays (transient routing + debug grid/emitters/crosses/labels).
auto draw_lighting_overlays( lighting::render_state &rs, lighting::frame_context &ctx ) -> void;

/// UI compositor Pass A: dirty-gated render of UI into compositor texture.
auto composite_ui_pass_a( lighting::render_state &rs, lighting::frame_context &ctx,
                          int proj_w, int proj_h ) -> void;

/// Pass W: world accumulation (shadow casters + tile sprites + volumetric + bloom).
auto render_world_pass_w( lighting::render_state &rs, lighting::frame_context &ctx,
                          int proj_w, int proj_h ) -> void;

/// Pass T: tonemap resolve from world_target to world_ldr_target.
auto tonemap_pass_t( lighting::render_state &rs, lighting::frame_context &ctx ) -> void;

/// Pass B: swapchain composite (world + UI blit + ImGui overlay + submit).
auto composite_swapchain_pass_b( lighting::render_state &rs, lighting::frame_context &ctx,
                                 int proj_w, int proj_h ) -> void;
