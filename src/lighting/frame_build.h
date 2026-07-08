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

#include "gpu_emitter.h"

#include <cstddef>
#include <vector>

namespace lighting {

class render_state;

// HUD / debug snapshot bits the caller mirrors into its EmitterOverlayState.
struct frame_lighting_result {
    bool built_pertile = false; // per-tile buffers were rebuilt this call
    float trans_at_player = -1.f;
    int sdf_W = 0;
    std::size_t sdf_size = 0;
    // Populated only when want_hud_snapshot: a copy of this frame's emitter
    // snapshot for the F5 HUD (emit[0] line).
    std::vector<gpu_emitter> snapshot_copy;
};

// Rebuild flags for decoupled per-tile buffer updates. Each buffer only
// rebuilds when its actual dependency changed:
//   structure — SDF, sun_sdf, sky_vis (depends on transparency_generation)
//   vis       — FOV visibility mask (depends on player position / seen_cache)
struct lighting_rebuild_flags {
    bool structure = true;
    bool vis = true;
};

// 1-bounce GI is computed on the GPU (radiance_cascade_pass); this only builds
// + submits the emitter snapshot and per-tile SDF / sky-vis / vis. The HUD
// snapshot is filled when want_hud_snapshot.
//
// skylight_bleed (0..1): indoor daylight bleed strength. 0 = the old binary
// sky-vis (open sky 1.0 / roofed 0.0). >0 runs a wall-aware flood-fill that
// propagates open-sky into roofed tiles through transparent cells (windows /
// doorways), blocked by opaque walls, scaled by this strength. Pure sky-ambient
// lift — artificial light stays GPU-side, so no double-count.
//
// vision_blur (tiles, 0 = off): Gaussian sigma applied to BOTH the FOV `vis`
// mask and the `sky_vis` mask (at tile resolution) before upload. FOV
// shadowcasting expands through narrow apertures (windows) in tile-sized jumps,
// so the beam shape is a hard staircase in the source data that bilinear can't
// dissolve; a blur of radius >= a few tiles smears the steps into a smooth
// diagonal (Stoneshard's mask-blur technique). Render-only (modulates final_rgb),
// so gameplay LOS is untouched.
// cam_x0/cam_y0/cam_w/cam_h (B1): on-screen tile rect in bubble-local tile
// coords (origin from cata_tiles::get_tile_map_origin, extent from
// get_screentile_*). The expensive supersampled Euclidean DT (structure_rebuild)
// is limited to this rect + an internal margin; off-region SDF is a large
// "no-occluder" sentinel never sampled by on-screen fragments. cam_w<=0 or
// cam_h<=0 → whole-bubble DT (the pre-B1 behaviour). Absolute-world-tile
// indexing and the full-size upload are unchanged, so no shader edit is needed.
frame_lighting_result build_and_submit_lighting(
    render_state& rs, lighting_rebuild_flags rebuild, bool want_hud_snapshot,
    float skylight_bleed = 0.0f, float vision_blur = 0.0f, int cam_x0 = -1, int cam_y0 = -1,
    int cam_w = 0, int cam_h = 0);

} // namespace lighting
