#pragma once

// SDL_GPU instance-batched sprite renderer — phase 2b of lighting rework.
//
// One batcher = one graphics pipeline + one persistent vertex buffer (a unit
// quad) + a ring of per-frame transfer/storage buffers holding sprite
// instances. Each draw call goes through SDL_DrawGPUPrimitives with an
// instance count, so a single pipeline binds the unit quad once and issues
// one draw per atlas-texture run.
//
// Phase 2 will instantiate two batchers:
//   1. tile_batcher  — full tile sprites (terrain, furniture, vehicles, mobs,
//                      effects, animated frames). Color-mod path emulates the
//                      legacy SDL_SetTextureColorMod tint exactly so the
//                      golden-image regression in sub-phase 2h passes.
//   2. ui_batcher    — UI glyphs / framebuffer chars (font texture pages
//                      produced by sdl_font.cpp's GPU text engine in 2f).
//
// Both share this class — different pipelines are achieved purely by
// supplying a different `pipeline_desc` at init().
//
// This is the *header* for sub-phase 2b. Implementation lands in 2d; the
// translation unit will reference no symbols until then so the build stays
// green. The struct layouts below are wire-stable: changing them requires a
// matching shader update in data/shaders/lighting/src/sprite.{vert,frag}.

#include "gpu_device.h"

#include <cstdint>
#include <memory>

namespace lighting
{

// One sprite = one instance. 64 bytes, packed std140-friendly.
// Layout must match data/shaders/lighting/src/sprite.vert input bindings.
//
//   dst_*    — pixel-space destination quad in the bound render target.
//   src_*    — normalised UV rect within the bound texture (0..1).
//   tint_*   — RGBA multiplier applied per-pixel (1.0 == passthrough).
//              Legacy SDL_SetTextureColorMod/SetTextureAlphaMod fold into this.
//   rotation — Radians, applied to the destination quad around its centre.
//              Sprite content is unchanged; only the quad's pixel coverage
//              rotates. 0 = no rotation; π/2 = 90° clockwise.
//   pad*     — keep struct 16-byte-aligned for std140; required by the
//              vertex shader's StructuredBuffer<SpriteInstance> binding.
struct sprite_instance {
    float dst_x;
    float dst_y;
    float dst_w;
    float dst_h;
    float src_u;
    float src_v;
    float src_uw;
    float src_vh;
    float tint_r;
    float tint_g;
    float tint_b;
    float tint_a;
    float rotation;
    float pad0;
    float pad1;
    float pad2;
};
static_assert( sizeof( sprite_instance ) == 64,
               "sprite_instance is wire-stable with the vertex shader; "
               "changing its layout requires shader edits." );

// Describes the graphics pipeline a batcher should build at init.
// `color_target_format` must match the SDL_GPUTexture the batcher will
// render into — usually the swapchain format for direct presentation, or an
// offscreen RT format (RGBA8 / RGBA16F) for the deferred lighting passes.
struct pipeline_desc {
    SDL_GPUTextureFormat color_target_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUBlendFactor   src_color_blend = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    SDL_GPUBlendFactor   dst_color_blend = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    SDL_GPUBlendOp       color_blend_op  = SDL_GPU_BLENDOP_ADD;
    SDL_GPUBlendFactor   src_alpha_blend = SDL_GPU_BLENDFACTOR_ONE;
    SDL_GPUBlendFactor   dst_alpha_blend = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    SDL_GPUBlendOp       alpha_blend_op  = SDL_GPU_BLENDOP_ADD;
    bool                 enable_blend    = true;
};

// PIMPL — keeps SDL_GPU storage-buffer / pipeline details out of the header
// so the rest of the codebase compiles without the SDL_gpu.h surface.
// Phase 8: sun + skylight parameters.  Wire-stable with SunParams cbuffer (b1, space3).
struct sun_params {
    float sun_dir_x, sun_dir_y;    // direction sun comes FROM (normalized 2D)
    float sun_sin_elev;             // sin(elevation): 0=horizon, 1=zenith
    float sun_intensity;            // 0=night, 1=noon
    float sun_r, sun_g, sun_b;     // sun color RGB
    float sky_r, sky_g, sky_b;     // sky ambient RGB
    float sky_intensity;            // overall sky brightness
    float sp_pad;
};

// Returns sun/sky params interpolated from a 24h LUT for the given hour (0..24).
sun_params make_sun_params( float hour_of_day ) noexcept;

class sprite_batcher_impl;

class sprite_batcher
{
    public:
        sprite_batcher();
        sprite_batcher( const sprite_batcher & ) = delete;
        sprite_batcher &operator=( const sprite_batcher & ) = delete;
        sprite_batcher( sprite_batcher && ) noexcept;
        sprite_batcher &operator=( sprite_batcher && ) noexcept;
        ~sprite_batcher();

        // Build pipeline + persistent buffers. Throws on shader / pipeline
        // creation failure — caller treats as fatal (same policy as
        // gpu_device::init).
        void init( gpu_device &dev, const pipeline_desc &desc,
                   const char *debug_label = "sprite_batcher" );

        void shutdown() noexcept;

        // Open a render pass that targets `target`. The batcher records all
        // subsequent draw() calls into `cb` until end_pass(). The target
        // resolution drives the viewport / orthographic projection sent to
        // the vertex shader as a push-constant.
        //
        //   clear_color present => LOAD_OP_CLEAR with that color
        //   clear_color absent  => LOAD_OP_LOAD (preserve)
        void begin_pass( SDL_GPUCommandBuffer *cb,
                         SDL_GPUTexture *target,
                         std::uint32_t target_w,
                         std::uint32_t target_h,
                         const float *clear_color_rgba = nullptr );

        // Bind a different atlas page. Calling this with the currently bound
        // page is a no-op; calling it with a different one flushes the
        // pending batch implicitly before re-binding.
        void set_texture( SDL_GPUTexture *atlas, SDL_GPUSampler *sampler );
        // Set a GPU scissor rect for subsequent draws. nullptr = full viewport.
        void set_scissor( const SDL_Rect *rect );
        // Phase 7/8: supply per-frame lighting resources.
        // emitter_tex:  4×64 RGBA32F (row=emitter, col 0=pos+radius, col 1=rgb+falloff)
        // sdf_tex:      W×H R32_FLOAT from sdf_pass
        // sky_vis_tex:  W×H R8_UNORM sky visibility (255=open sky, 0=indoor) Phase 8
        // sp:           sun+sky params pointer (nullptr = no sun)
        void set_lighting_resources( float             tile_pixel_size,
                                     float             z_level,
                                     Uint32            emitter_count,
                                     float             ambient,
                                     float             cam_off_x    = 0.0f,
                                     float             cam_off_y    = 0.0f,
                                     Uint32            sdf_map_w    = 0u,
                                     Uint32            sdf_map_h    = 0u,
                                     SDL_GPUTexture   *emitter_tex  = nullptr,
                                     SDL_GPUTexture   *sdf_tex      = nullptr,
                                     SDL_GPUSampler   *data_sampler = nullptr,
                                     SDL_GPUTexture   *sky_vis_tex  = nullptr,
                                     const sun_params *sp           = nullptr );

        // Append one sprite to the pending batch. Triggers an automatic
        // flush when the per-frame instance budget is reached so callers can
        // ignore buffer overflow.
        void draw( const sprite_instance &inst );
        void draw( const sprite_instance *insts, std::size_t count );

        // Force-flush the pending instances as one SDL_DrawGPUPrimitives
        // call. Normally implicit via set_texture() / end_pass(), exposed for
        // call sites that want explicit grouping.
        void flush();

        // Close the render pass started by begin_pass().
        void end_pass();

        // Per-frame reset — called by the render orchestrator at the start
        // of each frame to roll the instance-buffer ring forward. No GPU
        // sync; the buffers are sized so the in-flight frames never alias.
        void begin_frame();

    private:
        std::unique_ptr<sprite_batcher_impl> p;
};

} // namespace lighting
