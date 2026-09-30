#pragma once
#include "lighting/event_queue.h"
#include "lighting/gpu_emitter.h"
#include "lighting/sprite_batcher.h" // sun_params

#include <cstdint>
#include <vector>

namespace lighting {

// Build the per-frame emitter snapshot on the main thread.
//
// Walks the loaded reality bubble (same sources lightmap.cpp enumerates)
// and converts each light source to a gpu_emitter.  Also drains live
// flash_events from the provided event_queue.
//
// Call once per frame from the main game loop after all sim logic has
// run but before submitting the snapshot to the emitter_collector.
//
// Preconditions: g != nullptr, map is loaded.
// frame_ms: elapsed milliseconds since last frame (for event aging).
// sun: current sun direction/intensity/colour (render_state::current_sun()),
// used to light window "portal" cone emitters by the sun's incidence angle
// on each window's outward-facing side.
std::vector<gpu_emitter> build_emitter_snapshot(event_queue& eq, float frame_ms,
                                                 const sun_params& sun);

/// Bubble-local whole-tile rectangle.
struct emitter_view_rect {
    int x0 = 0;
    int y0 = 0;
    int w = 0;
    int h = 0;
};

/// Slack around the camera rect: tall sprites shade from a base tile below/left of
/// the visible rows, and the camera pixel-slide (get_drawing_pixel_offset) shifts
/// shade_pos sub-tile; 4 tiles dominates both.
inline constexpr int EMITTER_VIEW_MARGIN_TILES = 4;

/// Stable-partitions `emitters` so every emitter whose radius disc reaches `view`
/// grown by EMITTER_VIEW_MARGIN_TILES comes first; returns that count. The rest
/// provably contribute nothing to on-screen sprite shading (point_light_atten is 0
/// at dist >= radius). Degenerate rect (w or h <= 0) → untouched, returns size().
auto partition_emitters_by_view(std::vector<gpu_emitter>& emitters,
                                const emitter_view_rect& view) -> int;

} // namespace lighting
