#pragma once

// Fullscreen tonemap pass — the HDR RT backbone's resolve stage
// (LIGHTING_REWORK_PLAN.md step 1). Samples the lit scene target and writes a
// displayable result into a destination target via a single fullscreen
// triangle. Owns its own graphics pipeline + shaders (separate from the sprite
// batcher) and records a self-contained render pass on the destination, so it
// never touches the batcher's pass machinery.
//
// Curve lives in data/shaders/lighting/src/tonemap.frag.hlsl — identity in
// step 1a/1b, AgX in step 1c. The pass is the future home of the post chain
// (bloom / LUT hang off the same fullscreen-pass-into-offscreen-target pattern).

#include <cstdint>

#include <SDL3/SDL_gpu.h>

namespace lighting
{

class gpu_device;

class tonemap_pass
{
    public:
        tonemap_pass() = default;
        tonemap_pass( const tonemap_pass & ) = delete;
        tonemap_pass &operator=( const tonemap_pass & ) = delete;
        ~tonemap_pass();

        // Build the pipeline writing into a target of `dst_format`. Returns
        // false on failure (logs via DC::SDL); ready() stays false.
        bool init( gpu_device &dev, SDL_GPUTextureFormat dst_format );

        // Release pipeline + shaders. Idempotent; safe while the device lives.
        void shutdown() noexcept;

        bool ready() const noexcept { return pipeline_ != nullptr; }

        // Record a fullscreen tonemap into `dst`: sample `src` with `sampler`,
        // run the tonemap shader, write `dst`. Opens and closes its own render
        // pass on `dst` (the triangle covers the whole target, so the load-op
        // is DONT_CARE). `cb` is the frame's render command buffer. No-op if
        // not ready or any argument is null.
        // `exposure` (pre-AgX scale) + `min_ev`/`max_ev` (AgX log2 range) are
        // the F4 tonemap sliders, pushed as a fragment uniform (b0/space3).
        void record( SDL_GPUCommandBuffer *cb, SDL_GPUTexture *src,
                     SDL_GPUSampler *sampler, SDL_GPUTexture *dst,
                     std::uint32_t dst_w, std::uint32_t dst_h,
                     float exposure, float min_ev, float max_ev );

    private:
        // Create + fill the Phase 1a probe texture (one-shot submitted copy).
        // Returns false (logged, distinct from the gate) on format/alloc failure.
        bool init_probe_texture( gpu_device &dev );

        gpu_device              *dev_      = nullptr;
        SDL_GPUShader           *vert_     = nullptr;
        SDL_GPUShader           *frag_     = nullptr;
        SDL_GPUGraphicsPipeline *pipeline_ = nullptr;

        // Phase 1a HARD-GATE spike (LIGHTING_REWORK_PLAN.md step 3): a 1×1
        // GRAPHICS_STORAGE_READ probe texture (sentinel 1.0) bound to this
        // bufferless pass at storage slot 0 (→ t1/space2, after the t0 sampled
        // SrcTex). The frag does `.Load()` (NO sampler — that is what makes
        // shadercross reflect it as a read-only storage texture, not a 2nd
        // sampled image) and multiplies its output by the texel: works →
        // identity, broken → black. A binary test of whether fragment
        // storage-read textures bind at all on this Metal/shadercross build.
        // Filled once at init via a one-shot submitted copy cb (no per-frame
        // upload). Removed once the RC consumer path is proven.
        SDL_GPUTexture          *probe_tex_ = nullptr;
};

} // namespace lighting
