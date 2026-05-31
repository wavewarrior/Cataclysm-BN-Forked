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
    float sp_pad;        // deprecated; debug visualisation moved to debug_params
};

// Debug visualisation + runtime tuning knobs (DebugParams cbuffer at
// register(b2, space3); 48 bytes; wire-stable). debug_mode dispatches per-
// component visualisations in the fragment shader; emitter/sun/sky_scale
// multiply the corresponding contributions; shadow_k and shadow_steps tune
// the shared sphere-trace (emitter + sun); dither_amt/dither_bands tune the
// world-locked ordered (Bayer) dither. Defaults are the shipping look
// (scale=1, k=8, steps=16, dither on at 6 bands).
struct debug_params {
    uint32_t debug_mode    = 0u;
    float    debug_opacity = 0.6f;
    float    emitter_scale = 1.0f;
    float    sun_scale     = 1.0f;
    float    sky_scale     = 1.0f;
    float    shadow_k      = 8.0f;
    uint32_t shadow_steps  = 16u;
    float    dither_amt    = 1.0f;
    float    dither_bands  = 6.0f;
    float    gi_strength   = 0.60f;  // 1-bounce indirect multiplier (0=off); Alt+F8/F9 to tune
    float    dp_pad1       = 0.0f;
    float    dp_pad2       = 0.0f;
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
        // target_w/h drive the GPU viewport (rasterizer extent — physical
        // swapchain pixels). proj_w/h drive the shader's pixel→NDC math
        // (the orthographic "logical" coordinate system the UI draws in).
        // When proj_w/h are 0 (default) they fall back to target_w/h, which
        // preserves legacy behaviour for callers that don't need HiDPI
        // decoupling (offscreen captures, fixed-res render-to-texture).
        // HiDPI render path passes target = physical, proj = logical so the
        // GPU stretches logical-coord draws across the full physical fb.
        void begin_pass( SDL_GPUCommandBuffer *cb,
                         SDL_GPUTexture *target,
                         std::uint32_t target_w,
                         std::uint32_t target_h,
                         const float *clear_color_rgba = nullptr,
                         std::uint32_t proj_w = 0,
                         std::uint32_t proj_h = 0 );

        // Bind a different atlas page. Calling this with the currently bound
        // page and the same is_lit state is a no-op; otherwise flushes the
        // pending batch implicitly before re-binding.
        //
        // `is_lit` controls whether the lighting fragment-shader path runs
        // for instances drawn in this segment. Default true preserves
        // existing tile-sprite behaviour. UI rects + HUD font glyphs pass
        // false; the shader's existing emitter_count==0 / sdf_map_w==0
        // guards short-circuit both the per-emitter loop and the sun
        // march for these segments, recovering the GPU cost of running
        // lighting math on fragments where the result would be discarded
        // by max(tint, gpu_total).
        void set_texture( SDL_GPUTexture *atlas, SDL_GPUSampler *sampler,
                          bool is_lit = true );
        // Set a GPU scissor rect for subsequent draws. nullptr = full viewport.
        void set_scissor( const SDL_Rect *rect );
        // Phase 7/8: supply per-frame lighting resources.
        // emitter_buf:  GRAPHICS_STORAGE_READ buffer of gpu_emitter records.
        //               Bound as fragment storage buffer slot 0 → HLSL
        //               StructuredBuffer<GpuEmitter> at register(t2, space2).
        // sdf_buf:      GRAPHICS_STORAGE_READ buffer of W×H floats (sdf[x*H+y])
        //               from sdf_pass — fragment storage slot 1 → HLSL
        //               StructuredBuffer<float> at register(t3, space2).
        //               (Was a sampler texture; Metal mis-binds those.)
        // sky_vis_buf:  GRAPHICS_STORAGE_READ buffer of W×H floats (1.0=open
        //               sky, 0.0=roofed) — fragment storage slot 2 → HLSL
        //               StructuredBuffer<float> at register(t3, space2).
        // sp:           sun+sky params pointer (nullptr = no sun)
        void set_lighting_resources( float             tile_pixel_size,
                                     float             z_level,
                                     Uint32            emitter_count,
                                     float             ambient,
                                     float             cam_off_x    = 0.0f,
                                     float             cam_off_y    = 0.0f,
                                     Uint32            sdf_map_w    = 0u,
                                     Uint32            sdf_map_h    = 0u,
                                     SDL_GPUBuffer    *emitter_buf  = nullptr,
                                     SDL_GPUBuffer    *sdf_buf      = nullptr,
                                     SDL_GPUSampler   *data_sampler = nullptr,
                                     SDL_GPUBuffer    *sky_vis_buf  = nullptr,
                                     SDL_GPUBuffer    *indirect_buf = nullptr,
                                     const sun_params *sp           = nullptr,
                                     const debug_params *dbg        = nullptr );

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
