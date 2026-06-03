#pragma once

// Per-frame lighting data build for the GPU fragment pipeline. Extracted from
// refresh_display() (sdltiles.cpp) so the lighting CPU work lives in the
// lighting module rather than the windowing file. See LIGHTING_REWORK_PLAN.md
// step 0 (pre-backbone cleanup).
//
// build_and_submit_lighting():
//   - ALWAYS builds the emitter snapshot (transient flashes age per-frame at
//     FRAME_MS, so this must not be gated) and submits it to rs.collector().
//   - Builds the per-tile SDF / sky-vis / 1-bounce GI / vision buffers ONLY
//     when rebuild_pertile is true. When false, empty per-tile vectors are
//     submitted; emitter_collector::flush_to_render_cb then skips the per-tile
//     GPU upload and the previous frame's buffers stay resident (the dirty-gate
//     win — step 0b). The expensive CPU BFS + big buffer upload are avoided.

#include <cstddef>
#include <vector>

#include "gpu_emitter.h"

namespace lighting
{

class render_state;

// HUD / debug snapshot bits the caller mirrors into its EmitterOverlayState.
struct frame_lighting_result {
    bool   built_pertile   = false; // per-tile buffers were rebuilt this call
    float  sdf_at_player   = -1.f;
    float  trans_at_player = -1.f;
    int    sdf_W           = 0;
    std::size_t sdf_size   = 0;
    // Populated only when want_hud_snapshot: a copy of this frame's emitter
    // snapshot for the F5 HUD (emit[0] line).
    std::vector<gpu_emitter> snapshot_copy;
};

// gi_passes / gi_decay tune the 1-bounce indirect diffusion (more passes =
// light bleeds/bounces further; higher decay = more energy carried per ring →
// richer colored spread). Only used when rebuild_pertile.
frame_lighting_result build_and_submit_lighting( render_state &rs,
        bool rebuild_pertile, bool want_hud_snapshot,
        int gi_passes, float gi_decay );

} // namespace lighting
