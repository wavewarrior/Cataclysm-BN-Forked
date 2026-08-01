#pragma once

// GPU Jump-Flood Algorithm SDF pass (P3 of GI_COMPUTE_AND_PERF_PLAN). Replaces
// the CPU Euclidean DT (compute_sdf_cpu) with a near-exact GPU JFA that runs in
// parallel on the render command buffer. Three compute dispatches per frame:
//
//   1. SEED    (jfa_seed.comp):     one thread = one SS-subcell. Reads tile-res
//      transparency, seeds opaque tiles with their own subcell coord.
//   2. FLOOD   (jfa_flood.comp):    ping-pong N passes (powers of two). Each pass
//      propagates the nearest seed across ±step offsets.
//   3. RESOLVE (jfa_resolve.comp):  one thread = one SS-subcell. Computes distance
//      from subcell to its nearest seed, divides by SDF_SS → tile units.
//
// P3.3: the resolve pass writes sdf_storage_ directly (the live consumer buffer);
// the CPU Euclidean DT path is gone.
//
// Shaders: data/shaders/lighting/src/jfa_seed.comp.hlsl, jfa_flood.comp.hlsl,
//          jfa_resolve.comp.hlsl.

#include "occluder_capture.h"

#include <SDL3/SDL_gpu.h>
#include <cstdint>
#include <vector>

namespace lighting {

class gpu_device;

// Per-dispatch uniform (b0/space2). Field order MUST match all three JFA shader
// cbuffer declarations. 16 bytes.
struct jfa_params {
    std::uint32_t map_w; // runtime tile width
    std::uint32_t map_h; // runtime tile height
    float step;          // flood step size (unused by seed/resolve)
    float pad;           // alignment padding
};

// Per-dispatch uniform (b0/space2) for occ_base / occ_raster. 32 bytes.
struct occ_params {
    std::uint32_t map_w;
    std::uint32_t map_h;
    std::uint32_t quad_base; // index of this dispatch's first quad
    std::uint32_t atlas_w;
    std::uint32_t atlas_h;
    float occ_soft_gain;
    float op_pad0;
    float op_pad1;
};

class gpu_sdf_pass {
public:
    gpu_sdf_pass() = default;
    gpu_sdf_pass(const gpu_sdf_pass&) = delete;
    gpu_sdf_pass& operator=(const gpu_sdf_pass&) = delete;
    ~gpu_sdf_pass();

    // Compile the five compute pipelines + allocate the coverage / seed ping-pong
    // buffers. max_w/max_h are TILE dimensions (the max mapsize this pass will ever
    // run); the SS grid is sized max_w*SDF_SS x max_h*SDF_SS internally. Returns
    // false on failure (logged).
    bool init(gpu_device& dev, std::uint32_t max_w, std::uint32_t max_h);

    void shutdown() noexcept;

    bool ready() const noexcept {
        return seed_pipeline_ != nullptr && flood_pipeline_ != nullptr
            && resolve_pipeline_ != nullptr && occ_base_pipeline_ != nullptr
            && occ_raster_pipeline_ != nullptr;
    }

    // Rasterise the captured sprite footprints into the sub-tile coverage field, then
    // run seed -> flood (ping-pong) -> resolve on `cb`. `trans_buf` (tile-res floats,
    // 0=opaque .. 1=open) is the off-camera fallback; `occ` supplies the on-camera
    // silhouettes. Writes `target_sdf` (SS-grid, 1 float/subcell, TILE units). Each
    // dispatch is its own BeginGPUComputePass/EndGPUComputePass so SDL_GPU inserts
    // the write->read barriers. No-op if not ready.
    void record(
        SDL_GPUCommandBuffer* cb, SDL_GPUBuffer* trans_buf, SDL_GPUBuffer* target_sdf,
        std::uint32_t runtime_w, std::uint32_t runtime_h, const occluder_capture& occ,
        float occ_soft_gain);

private:
    SDL_GPUBuffer* create_buffer(std::uint32_t bytes, SDL_GPUBufferUsageFlags usage);
    // Upload this frame's captured mask + page-sorted quads. Returns false when there
    // is nothing to rasterise (the base pass still runs, so the SDF stays valid).
    bool upload_occluders(
        SDL_GPUCommandBuffer* cb, const occluder_capture& occ, std::uint32_t runtime_w,
        std::uint32_t runtime_h);

    gpu_device* dev_ = nullptr;
    SDL_GPUComputePipeline* seed_pipeline_ = nullptr;
    SDL_GPUComputePipeline* flood_pipeline_ = nullptr;
    SDL_GPUComputePipeline* resolve_pipeline_ = nullptr;
    SDL_GPUComputePipeline* occ_base_pipeline_ = nullptr;
    SDL_GPUComputePipeline* occ_raster_pipeline_ = nullptr;

    // Ping-pong seed buffers: SS-grid, 2 floats/subcell (nearest-seed subcell coord).
    // Sentinel (-1,-1) = no seed yet.
    SDL_GPUBuffer* seed_a_ = nullptr;
    SDL_GPUBuffer* seed_b_ = nullptr;

    // Sub-tile coverage field: SS-grid, 1 uint/subcell, coverage * 65535. uint so
    // overlapping sprites resolve with InterlockedMax (order-independent).
    SDL_GPUBuffer* occ_ss_ = nullptr;
    // Tile-res "a sprite footprint was captured here" mask, 1 uint/tile.
    SDL_GPUBuffer* captured_buf_ = nullptr;
    SDL_GPUTransferBuffer* xfer_captured_ = nullptr;
    // Page-sorted occluder_quad array.
    SDL_GPUBuffer* quads_buf_ = nullptr;
    SDL_GPUTransferBuffer* xfer_quads_ = nullptr;
    std::uint32_t quads_capacity_ = 0; // quads the current buffer can hold

    // Scratch for the page sort, kept across frames so record() does not allocate.
    std::vector<std::uint32_t> sort_order_;
    struct page_run {
        SDL_GPUTexture* tex;
        std::uint32_t atlas_w, atlas_h;
        std::uint32_t first, count;
    };
    std::vector<page_run> page_runs_;

    std::uint32_t max_sw_ = 0; // physical SS width (max_w * SDF_SS)
    std::uint32_t max_sh_ = 0; // physical SS height (max_h * SDF_SS)
    std::uint32_t max_tiles_ = 0;
};

} // namespace lighting
