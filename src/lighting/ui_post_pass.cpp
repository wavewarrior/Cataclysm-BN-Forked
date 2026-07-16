#include "ui_post_pass.h"

#include "debug.h"
#include "lighting/gpu_device.h"
#include "lighting/shader_compiler.h"

#include <string>

#define dbg(x) DebugLogFL((x), DC::SDL)

namespace lighting {

ui_post_pass::~ui_post_pass() { shutdown(); }

auto ui_post_pass::init( gpu_device &dev, SDL_GPUTextureFormat format,
                         std::uint32_t full_w, std::uint32_t full_h ) -> bool
{
    shutdown();
    dev_ = &dev;
    format_ = format;
    full_w_ = full_w;
    full_h_ = full_h;

    if( !dev.ready() ) {
        dbg( DL::Error ) << "ui_post_pass::init: gpu_device not ready";
        return false;
    }

    init_shader_compiler();

    const std::string vert_src = load_lighting_shader_source( "tonemap.vert.hlsl" );
    const std::string post_src = load_lighting_shader_source( "ui_post.frag.hlsl" );

    auto v = compile_graphics_shader(
        dev, vert_src, "main", SDL_SHADERCROSS_SHADERSTAGE_VERTEX, "ui_post.vert" );
    auto f = compile_graphics_shader(
        dev, post_src, "main", SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "ui_post.frag" );

    if( !v || !f ) {
        if( v ) { SDL_ReleaseGPUShader( dev.raw(), v.shader ); }
        if( f ) { SDL_ReleaseGPUShader( dev.raw(), f.shader ); }
        dbg( DL::Error ) << "ui_post_pass: shader compile failed";
        return false;
    }

    vert_ = v.shader;
    post_frag_ = f.shader;

    // No blending — write directly to swapchain.
    SDL_GPUColorTargetBlendState blend{};
    blend.enable_blend = false;
    blend.color_write_mask =
        SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B
        | SDL_GPU_COLORCOMPONENT_A;

    SDL_GPUColorTargetDescription color_target{};
    color_target.format = format;
    color_target.blend_state = blend;

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vert_;
    pci.fragment_shader = post_frag_;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pci.target_info.num_color_targets = 1;
    pci.target_info.color_target_descriptions = &color_target;
    pci.target_info.has_depth_stencil_target = false;

    pipeline_ = SDL_CreateGPUGraphicsPipeline( dev.raw(), &pci );
    if( !pipeline_ ) {
        dbg( DL::Error ) << "ui_post_pass: pipeline creation failed";
        return false;
    }

    // LINEAR sampler for blur taps.
    SDL_GPUSamplerCreateInfo sci{};
    sci.min_filter = SDL_GPU_FILTER_LINEAR;
    sci.mag_filter = SDL_GPU_FILTER_LINEAR;
    sci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_ = SDL_CreateGPUSampler( dev.raw(), &sci );
    if( !sampler_ ) {
        dbg( DL::Error ) << "ui_post_pass sampler: " << SDL_GetError();
        return false;
    }

    return true;
}

auto ui_post_pass::shutdown() noexcept -> void
{
    if( dev_ ) {
        SDL_ReleaseGPUGraphicsPipeline( dev_->raw(), pipeline_ );
        SDL_ReleaseGPUShader( dev_->raw(), post_frag_ );
        SDL_ReleaseGPUShader( dev_->raw(), vert_ );
        SDL_ReleaseGPUSampler( dev_->raw(), sampler_ );
    }
    pipeline_ = nullptr;
    post_frag_ = nullptr;
    vert_ = nullptr;
    sampler_ = nullptr;
    dev_ = nullptr;
}

auto ui_post_pass::record( SDL_GPUCommandBuffer *cb, SDL_GPUTexture *src_tex,
                           SDL_GPUTexture *dst_tex,
                           std::uint32_t full_w, std::uint32_t full_h,
                           float ca_intensity, float bloom_strength ) -> void
{
    if( !ready() || !src_tex || !dst_tex || !dev_ ) {
        return;
    }

    // Push uniform before the render pass (matches bloom_pass pattern).
    struct UiPostParams {
        float ca_intensity;
        float bloom_strength;
        float pad0;
        float pad1;
    };
    const UiPostParams params {
        .ca_intensity = ca_intensity,
        .bloom_strength = bloom_strength,
        .pad0 = 0.f,
        .pad1 = 0.f,
    };
    SDL_PushGPUFragmentUniformData( cb, 0, &params, sizeof( params ) );

    SDL_GPUColorTargetInfo ct{};
    ct.texture = dst_tex;
    ct.load_op = SDL_GPU_LOADOP_DONT_CARE;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *rp = SDL_BeginGPURenderPass( cb, &ct, 1, nullptr );
    if( !rp ) {
        return;
    }

    SDL_BindGPUGraphicsPipeline( rp, pipeline_ );
    const SDL_GPUViewport vp { 0.0f, 0.0f,
                               static_cast<float>( full_w ),
                               static_cast<float>( full_h ), 0.0f, 1.0f };
    SDL_SetGPUViewport( rp, &vp );

    SDL_GPUTextureSamplerBinding tsb{};
    tsb.texture = src_tex;
    tsb.sampler = sampler_;
    SDL_BindGPUFragmentSamplers( rp, 0, &tsb, 1 );

    SDL_DrawGPUPrimitives( rp, 3, 1, 0, 0 );
    SDL_EndGPURenderPass( rp );
}

} // namespace lighting