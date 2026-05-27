#pragma once
#include <atomic>
#include <vector>

#include "lighting/gpu_emitter.h"
// Forward declarations so this header doesn't pull in SDL3 GPU types.
struct SDL_GPUCommandBuffer;
struct SDL_GPUTexture;
struct SDL_GPUTransferBuffer;

namespace lighting
{
class render_state;

// Per-frame emitter data uploader. Owns a single 4×256 RGBA32F texture
// (`emitter_texture()`) that the lighting fragment shader samples via
// Load() at register(t1, space2).
//
// Single-threaded by design (Q8 cleanup): submit() stages the next
// frame's snapshot, flush_to_render_cb() drains it onto the caller's
// render command buffer. Both run from the main thread sequentially
// within refresh_display().
//
// Q9 (Path B): the previous 2-slot ring + manual read/write slot
// indices were collapsed into a single texture + transfer buffer, using
// SDL_GPU's cycle=true semantics for streaming-write. cycle=true tells
// SDL_GPU to orphan the underlying physical resource on each upload (so
// the GPU can still read the previous one if any work is in flight)
// while keeping the public handle stable across frames. Inside the same
// command buffer SDL_GPU emits the required write→sample barrier
// automatically. This removes the entire dual-rotation puzzle (sprite-
// batcher slot vs emitter-collector slot) — there's only one source of
// truth now: the single emitter_texture() handle.
//
// Q7 (earlier): the legacy 512 KB SSBO ring + read_buffer() accessor
// were deleted (no shader sampled them).
class emitter_collector
{
    public:
        explicit emitter_collector( render_state &rs );
        ~emitter_collector();

        // Submit a fresh snapshot for upload on the next frame's
        // flush_to_render_cb(). Stores into pending_*; the actual GPU
        // copy pass is recorded inside flush_to_render_cb(). Main-thread
        // only; no locking, no thread wake.
        void submit( std::vector<gpu_emitter> snapshot,
                     std::vector<uint8_t>    transparency = {},
                     std::vector<float>      sdf          = {},
                     std::vector<uint8_t>    sky_vis      = {} );

        // 4×256 RGBA32F texture handle. SDL_GPU's cycle=true on upload
        // swaps the underlying physical resource per frame; this handle
        // is stable for the process lifetime.
        SDL_GPUTexture *emitter_texture() const noexcept { return emitter_tex_; }

        // Number of emitters in the last completed upload.
        int last_count() const noexcept { return last_count_.load( std::memory_order_relaxed ); }

        // Drains any pending snapshot and records a copy pass on the
        // caller's render command buffer. Same-CB copy→sample ordering
        // means SDL_GPU emits the required barrier automatically; no
        // fence wait. No-op if no pending data.
        void flush_to_render_cb( SDL_GPUCommandBuffer *cb );

        // GPU texture readback (diagnostic). Returns what the GPU actually has
        // in EmitterTex pixel 0 (= pos_x, pos_y, pos_z, radius of the
        // first emitter row). Updated one frame after each flush so values
        // reflect the GPU-resident texture content, not the CPU snapshot.
        float debug_d0_x() const noexcept { return debug_d0_x_.load( std::memory_order_relaxed ); }
        float debug_d0_y() const noexcept { return debug_d0_y_.load( std::memory_order_relaxed ); }
        float debug_d0_z() const noexcept { return debug_d0_z_.load( std::memory_order_relaxed ); }
        float debug_d0_w() const noexcept { return debug_d0_w_.load( std::memory_order_relaxed ); }

    private:
        render_state &rs_;

        // Snapshot data published by submit(), consumed by the next
        // flush_to_render_cb(). Single-threaded: no mutex needed.
        std::vector<gpu_emitter> pending_;
        std::vector<uint8_t>    pending_transparency_;
        std::vector<uint8_t>    pending_sky_vis_;
        std::vector<float>      pending_sdf_;
        bool have_pending_ = false;

        // Single staging buffer + single texture. SDL_GPU's cycle=true on
        // map / upload handles frame-to-frame data dependencies internally;
        // the previous 2-slot manual ring is gone.
        SDL_GPUTransferBuffer *xfer_        = nullptr;
        SDL_GPUTexture        *emitter_tex_ = nullptr;

        // last_count is read by begin_lighting_frame → fragment shader; stays
        // atomic for cheap future-proofing post-Q8 worker deletion (relaxed
        // memory order on x86/ARM is a plain load/store).
        std::atomic<int> last_count_ = 0;

        // Diagnostic GPU→CPU readback of EmitterTex pixel (col=0, row=0).
        SDL_GPUTransferBuffer *download_xfer_ = nullptr;
        bool                   download_pending_ = false;
        std::atomic<float>     debug_d0_x_{0.f};
        std::atomic<float>     debug_d0_y_{0.f};
        std::atomic<float>     debug_d0_z_{0.f};
        std::atomic<float>     debug_d0_w_{0.f};
};

} // namespace lighting
