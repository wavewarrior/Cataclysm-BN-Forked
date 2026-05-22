#pragma once
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "lighting/gpu_emitter.h"
// Forward declarations so this header doesn't pull in SDL3 GPU types.
struct SDL_GPUBuffer;
struct SDL_GPUTransferBuffer;

namespace lighting
{
class render_state;

// Worker thread that owns the SSBO pipeline for per-frame emitter data.
//
// Main thread calls submit() with a fresh vector<gpu_emitter> after the
// snapshot builder runs.  The worker thread wakes up, maps the transfer
// buffer, copies the data, and submits a copy pass to upload the storage
// buffer.  The SSBO is double-buffered so the GPU can read slot[read]
// while the CPU writes slot[write].
//
// Phase 3: no shader reads the SSBO yet; the infrastructure is present
// and verified to populate correctly.  Phase 4 binds read_buffer() as a
// storage resource in the lighting passes.
class emitter_collector
{
public:
    explicit emitter_collector( render_state &rs );
    ~emitter_collector();

    // Submit a fresh snapshot for asynchronous upload.
    // Called from the main thread; non-blocking.
    void submit( std::vector<gpu_emitter> snapshot );

    // Return the GPU buffer for the last fully-uploaded frame.
    // May return nullptr until the first upload completes.
    // Safe to call from the main thread; uses an atomic slot index.
    SDL_GPUBuffer *read_buffer() const noexcept;

    // Number of emitters in the last completed upload.
    int last_count() const noexcept { return last_count_.load( std::memory_order_relaxed ); }

private:
    void thread_main();
    void upload_to_gpu( const std::vector<gpu_emitter> &data );

    render_state &rs_;

    std::thread thread_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::vector<gpu_emitter> pending_;
    bool have_pending_ = false;
    bool stop_         = false;

    // Double-buffered SSBO: GPU reads [read_slot_], CPU writes [write_slot_].
    static constexpr int RING = 2;
    SDL_GPUBuffer         *ssbo_[RING] = {};
    SDL_GPUTransferBuffer *xfer_[RING] = {};

    std::atomic<int> read_slot_ = 0;
    int              write_slot_ = 1;
    std::atomic<int> last_count_ = 0;
};

} // namespace lighting
