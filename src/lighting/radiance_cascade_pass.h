#pragma once

// Radiance-cascade GI pass — Step-3 Phase 2 (single cascade, GPU radiance
// gather). Models the tonemap_pass shape (owns its own pipeline + shaders +
// a self-contained render pass) but ALSO owns its output: cascade_tex_, an
// RGBA16F texture with COLOR_TARGET|GRAPHICS_STORAGE_READ usage. The pass
// renders a fullscreen triangle into it (one fragment = one probe/tile),
// gathering occluded emitter radiance by marching the SDF; the sprite shader
// then reads cascade_tex_ as its IndirectTex GI input (drop-in replacement for
// the CPU indirect texture — same transposed layout: width=tex_h height=tex_w).
//
// Phase 2 is a per-probe emitter gather (proves the gather + storage-buffer
// binding + transpose). The directional cascade hierarchy + bilinear-fix merge
// is Phase 3. Shader: data/shaders/lighting/src/rc.frag.hlsl (+ tonemap.vert).

#include <cstdint>

#include <SDL3/SDL_gpu.h>

namespace lighting
{

class gpu_device;

// Per-probe gather tuning, pushed as the fragment uniform (b0/space3).
struct rc_params {
    std::uint32_t emitter_count;
    std::uint32_t map_w;        // runtime SDF dims (probe grid extent)
    std::uint32_t map_h;
    float         current_z;    // probe z-plane (skip off-plane emitters)
    float         shadow_k;     // sphere-trace cone hardness (reuse sprite knob)
    std::uint32_t shadow_steps; // per-emitter march cap
    float         pad0;
    float         pad1;
};

class radiance_cascade_pass
{
    public:
        radiance_cascade_pass() = default;
        radiance_cascade_pass( const radiance_cascade_pass & ) = delete;
        radiance_cascade_pass &operator=( const radiance_cascade_pass & ) = delete;
        ~radiance_cascade_pass();

        // Build the pipeline + allocate cascade_tex_ at physical dims
        // (tex_w × tex_h, stored transposed so it is a drop-in for the sprite's
        // IndirectTex). Clears the texture once so the sprite never reads
        // garbage before the first gather. Returns false on failure (logged).
        bool init( gpu_device &dev, std::uint32_t tex_w, std::uint32_t tex_h );

        // Reallocate cascade_tex_ for a new physical map size. Cheap no-op if
        // unchanged. Returns false on failure.
        bool resize( std::uint32_t tex_w, std::uint32_t tex_h );

        void shutdown() noexcept;

        bool ready() const noexcept { return pipeline_ != nullptr && cascade_tex_ != nullptr; }

        // The GI irradiance field. Bound by the sprite pass as IndirectTex.
        SDL_GPUTexture *cascade_texture() const noexcept { return cascade_tex_; }

        // Gather one cascade into cascade_tex_: bind emitter_buf (t0) + sdf_buf
        // (t1) as fragment storage buffers, run the gather over the runtime
        // probe grid (viewport runtime_h × runtime_w — transposed). Opens and
        // closes its own render pass. No-op if not ready or any arg invalid.
        void record( SDL_GPUCommandBuffer *cb,
                     SDL_GPUBuffer *emitter_buf, SDL_GPUBuffer *sdf_buf,
                     std::uint32_t runtime_w, std::uint32_t runtime_h,
                     const rc_params &params );

    private:
        bool create_texture( std::uint32_t tex_w, std::uint32_t tex_h );
        void clear_texture(); // one-shot LOADOP_CLEAR so first read is defined

        gpu_device              *dev_      = nullptr;
        SDL_GPUShader           *vert_     = nullptr;
        SDL_GPUShader           *frag_     = nullptr;
        SDL_GPUGraphicsPipeline *pipeline_ = nullptr;
        SDL_GPUTexture          *cascade_tex_ = nullptr;
        std::uint32_t            tex_w_ = 0; // physical alloc (transposed: tex width=tex_h)
        std::uint32_t            tex_h_ = 0;
};

} // namespace lighting
