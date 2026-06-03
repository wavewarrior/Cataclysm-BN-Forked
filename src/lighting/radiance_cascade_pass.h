#pragma once

// Radiance GI pass — Step-3 Phase 2/3 (GPU two-pass single-bounce GI). Models
// the tonemap_pass shape (owns pipelines + shaders + self-contained render
// passes) and owns its textures. Two fullscreen-triangle passes per gather:
//
//   1. FIELD  (rc.frag):        one fragment = one probe/tile. Per-tile direct
//      radiance = occluded emitter gather (sphere-march the SDF). Writes
//      radiance_field_tex_. (This is the Phase-2 gather, now an intermediate.)
//   2. BOUNCE (rc_bounce.frag): per probe, march N rays through the radiance
//      field, accumulating the lit-surface radiance reachable before a wall.
//      Writes cascade_tex_ — real colored bounce into shadow / around corners,
//      which a direct gather (Phase 2) cannot produce.
//
// cascade_tex_ is RGBA16F COLOR_TARGET|GRAPHICS_STORAGE_READ, a drop-in for the
// sprite's IndirectTex (same transposed layout width=tex_h height=tex_w); the
// sprite reads it as its GI input. Shaders: data/shaders/lighting/src/
// rc.frag.hlsl + rc_bounce.frag.hlsl (+ tonemap.vert fullscreen tri).

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

        bool ready() const noexcept {
            return field_pipeline_ != nullptr && bounce_pipeline_ != nullptr
                   && radiance_field_tex_ != nullptr && cascade_tex_ != nullptr;
        }

        // The GI irradiance field. Bound by the sprite pass as IndirectTex.
        SDL_GPUTexture *cascade_texture() const noexcept { return cascade_tex_; }

        // Dev oracle (Step-3): synchronous GPU→CPU readback of cascade_tex_ over
        // the runtime probe region; logs sum/max/nonzero/centroid to DC::Main.
        // Stalls the GPU (SDL_WaitForGPUIdle) — call it on demand (F4 button),
        // never per frame. The quantitative check that substitutes for the eye:
        // bounce shows nonzero radiance in occluded tiles a direct gather can't.
        void debug_log_stats( std::uint32_t runtime_w, std::uint32_t runtime_h );

        // Gather one cascade into cascade_tex_: bind emitter_buf (t0) + sdf_buf
        // (t1) as fragment storage buffers, run the gather over the runtime
        // probe grid (viewport runtime_h × runtime_w — transposed). Opens and
        // closes its own render pass. No-op if not ready or any arg invalid.
        void record( SDL_GPUCommandBuffer *cb,
                     SDL_GPUBuffer *emitter_buf, SDL_GPUBuffer *sdf_buf,
                     std::uint32_t runtime_w, std::uint32_t runtime_h,
                     const rc_params &params );

    private:
        SDL_GPUTexture *create_texture( std::uint32_t tex_w, std::uint32_t tex_h );
        void clear_texture( SDL_GPUTexture *tex ); // one-shot LOADOP_CLEAR

        gpu_device              *dev_  = nullptr;
        SDL_GPUShader           *vert_        = nullptr; // shared fullscreen tri
        SDL_GPUShader           *field_frag_  = nullptr; // rc.frag (emitter gather)
        SDL_GPUShader           *bounce_frag_ = nullptr; // rc_bounce.frag (field march)
        SDL_GPUGraphicsPipeline *field_pipeline_  = nullptr;
        SDL_GPUGraphicsPipeline *bounce_pipeline_ = nullptr;
        SDL_GPUTexture          *radiance_field_tex_ = nullptr; // pass 1 out / pass 2 in
        SDL_GPUTexture          *cascade_tex_        = nullptr; // pass 2 out (sprite GI)
        std::uint32_t            tex_w_ = 0; // physical alloc (transposed: tex width=tex_h)
        std::uint32_t            tex_h_ = 0;
};

} // namespace lighting
