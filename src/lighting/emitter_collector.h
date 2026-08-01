#pragma once
#include "lighting/gpu_emitter.h"

#include <atomic>
#include <vector>
// Forward declarations so this header doesn't pull in SDL3 GPU types.
struct SDL_GPUBuffer;
struct SDL_GPUCommandBuffer;
struct SDL_GPUTransferBuffer;

namespace lighting {
class render_state;

// Per-frame emitter data uploader. Owns a single GRAPHICS_STORAGE_READ
// buffer (`emitter_buffer()`) that the fragment shader reads via a
// StructuredBuffer<GpuEmitter> binding at register(t3, space2) — the
// "sampled textures first, then storage buffers" convention documented
// by SDL_CreateGPUShader.
//
// History — previously this was a 4×256 RGBA32F sampler texture
// (register(t1, space2)). SDL_shadercross 6b06e55c silently mis-binds
// FP32 sampler textures on Metal: readback proves the upload reaches
// the GPU, but the fragment shader sees all-zero samples. Switching to
// a storage buffer side-steps the codegen path entirely.
//
// Single-threaded by design: submit() stages the next frame's snapshot,
// flush_to_render_cb() drains it onto the caller's render command buffer.
// Both run from the main thread sequentially within refresh_display().
class emitter_collector {
public:
    explicit emitter_collector(render_state& rs);
    ~emitter_collector();

    // Submit a fresh snapshot for upload on the next frame's
    // flush_to_render_cb(). Stores into pending_*; the actual GPU
    // copy pass is recorded inside flush_to_render_cb(). Main-thread
    // only; no locking, no thread wake.
    void submit(
        std::vector<gpu_emitter> snapshot, std::vector<uint8_t> transparency = {},
        std::vector<float> sdf = {}, std::vector<uint8_t> sky_vis = {},
        int runtime_w = 0, int runtime_h = 0,
        // Stage 2b: unified coverage occluder, tile-res, 2 floats/tile.
        std::vector<float> occ = {});

    // GRAPHICS_STORAGE_READ buffer handle, sized for MAX_EMITTERS
    // entries. SDL_GPU's cycle=true on upload swaps the underlying
    // physical resource per frame; this handle is stable for the
    // process lifetime.
    SDL_GPUBuffer* emitter_buffer() const noexcept { return emitter_buf_; }

    // Number of emitters in the last completed upload.
    int last_count() const noexcept { return last_count_.load(std::memory_order_relaxed); }

    // Drains any pending snapshot and records a copy pass on the
    // caller's render command buffer. Same-CB copy→sample ordering
    // means SDL_GPU emits the required barrier automatically; no
    // fence wait. No-op if no pending data.
    void flush_to_render_cb(SDL_GPUCommandBuffer* cb);

private:
    render_state& rs_;

    // Snapshot data published by submit(), consumed by the next
    // flush_to_render_cb(). Single-threaded: no mutex needed.
    std::vector<gpu_emitter> pending_;
    std::vector<uint8_t> pending_transparency_;
    std::vector<uint8_t> pending_sky_vis_;
    std::vector<float> pending_sdf_;
    std::vector<float> pending_occ_; // Stage 2b coverage occluder
    int pending_runtime_w_ = 0;
    int pending_runtime_h_ = 0;
    bool have_pending_ = false;

    SDL_GPUTransferBuffer* xfer_ = nullptr;
    SDL_GPUBuffer* emitter_buf_ = nullptr;

    std::atomic<int> last_count_ = 0;
};

} // namespace lighting
