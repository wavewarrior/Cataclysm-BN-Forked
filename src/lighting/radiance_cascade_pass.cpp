#include "radiance_cascade_pass.h"

#include <string>

#include "debug.h"
#include "lighting/gpu_device.h"
#include "lighting/shader_compiler.h"

#define dbg( x ) DebugLogFL( ( x ), DC::SDL )

namespace lighting
{

// RGBA16F: HDR irradiance (gathered radiance can exceed 1.0 before the sprite
// scales it). COLOR_TARGET so the gather renders into it; GRAPHICS_STORAGE_READ
// so the sprite pass reads it as IndirectTex.
static constexpr SDL_GPUTextureFormat CASCADE_FORMAT =
    SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;

radiance_cascade_pass::~radiance_cascade_pass()
{
    shutdown();
}

bool radiance_cascade_pass::init( gpu_device &dev, std::uint32_t tex_w, std::uint32_t tex_h )
{
    shutdown();
    dev_ = &dev;
    if( !dev.ready() ) {
        dbg( DL::Error ) << "radiance_cascade_pass::init: gpu_device not ready";
        return false;
    }

    init_shader_compiler();

    const std::string vert_src = load_lighting_shader_source( "tonemap.vert.hlsl" );
    const std::string frag_src = load_lighting_shader_source( "rc.frag.hlsl" );
    auto v = compile_graphics_shader( dev, vert_src, "main",
                                      SDL_SHADERCROSS_SHADERSTAGE_VERTEX, "rc.vert" );
    if( !v ) {
        dbg( DL::Error ) << "radiance_cascade_pass: vert compile failed";
        return false;
    }
    auto f = compile_graphics_shader( dev, frag_src, "main",
                                      SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "rc.frag" );
    if( !f ) {
        SDL_ReleaseGPUShader( dev.raw(), v.shader );
        dbg( DL::Error ) << "radiance_cascade_pass: frag compile failed";
        return false;
    }
    vert_ = v.shader;
    frag_ = f.shader;

    // Phase 2 structural gate: rc.frag is the first sampler-less fragment pass
    // (0 sampled, 0 storage textures), so the storage buffers should start at
    // t0. Confirm shadercross reflected 2 storage buffers before the pipeline
    // create — logged to DC::Main since DC::SDL is filtered on this build.
    DebugLogFL( DL::Info, DC::Main )
            << "rc.frag reflection: samplers=" << f.resources.num_samplers
            << " storage_textures=" << f.resources.num_storage_textures
            << " storage_buffers=" << f.resources.num_storage_buffers
            << " (Phase 2 expects storage_buffers=2)";

    SDL_GPUColorTargetBlendState blend{};
    blend.enable_blend = false;
    blend.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G |
                             SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;

    SDL_GPUColorTargetDescription color_target{};
    color_target.format = CASCADE_FORMAT;
    color_target.blend_state = blend;

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader   = vert_;
    pci.fragment_shader = frag_;
    pci.primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.rasterizer_state.fill_mode  = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode  = SDL_GPU_CULLMODE_NONE;
    pci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pci.target_info.num_color_targets = 1;
    pci.target_info.color_target_descriptions = &color_target;
    pci.target_info.has_depth_stencil_target  = false;

    pipeline_ = SDL_CreateGPUGraphicsPipeline( dev.raw(), &pci );
    if( !pipeline_ ) {
        DebugLogFL( DL::Error, DC::Main ) << "radiance_cascade_pass pipeline: " << SDL_GetError();
        return false;
    }

    if( !create_texture( tex_w, tex_h ) ) {
        return false;
    }
    clear_texture();
    return true;
}

bool radiance_cascade_pass::create_texture( std::uint32_t tex_w, std::uint32_t tex_h )
{
    if( !SDL_GPUTextureSupportsFormat( dev_->raw(), CASCADE_FORMAT, SDL_GPU_TEXTURETYPE_2D,
                                       SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                                       SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ ) ) {
        DebugLogFL( DL::Error, DC::Main )
                << "radiance_cascade_pass: RGBA16F COLOR_TARGET|GRAPHICS_STORAGE_READ unsupported";
        return false;
    }
    SDL_GPUTextureCreateInfo tci{};
    tci.type   = SDL_GPU_TEXTURETYPE_2D;
    tci.format = CASCADE_FORMAT;
    tci.usage  = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ;
    tci.width                = tex_h; // transposed (drop-in for IndirectTex)
    tci.height               = tex_w;
    tci.layer_count_or_depth = 1;
    tci.num_levels   = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    cascade_tex_ = SDL_CreateGPUTexture( dev_->raw(), &tci );
    if( !cascade_tex_ ) {
        DebugLogFL( DL::Error, DC::Main ) << "radiance_cascade_pass: cascade tex create: " << SDL_GetError();
        return false;
    }
    tex_w_ = tex_w;
    tex_h_ = tex_h;
    return true;
}

void radiance_cascade_pass::clear_texture()
{
    if( !cascade_tex_ ) {
        return;
    }
    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer( dev_->raw() );
    if( !cb ) {
        return;
    }
    SDL_GPUColorTargetInfo ct{};
    ct.texture  = cascade_tex_;
    ct.load_op  = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    ct.clear_color = SDL_FColor{ 0.0f, 0.0f, 0.0f, 0.0f };
    SDL_GPURenderPass *rp = SDL_BeginGPURenderPass( cb, &ct, 1, nullptr );
    if( rp ) {
        SDL_EndGPURenderPass( rp );
    }
    SDL_SubmitGPUCommandBuffer( cb );
}

bool radiance_cascade_pass::resize( std::uint32_t tex_w, std::uint32_t tex_h )
{
    if( cascade_tex_ && tex_w == tex_w_ && tex_h == tex_h_ ) {
        return true;
    }
    if( cascade_tex_ && dev_ && dev_->ready() ) {
        SDL_ReleaseGPUTexture( dev_->raw(), cascade_tex_ );
        cascade_tex_ = nullptr;
    }
    if( !create_texture( tex_w, tex_h ) ) {
        return false;
    }
    clear_texture();
    return true;
}

void radiance_cascade_pass::shutdown() noexcept
{
    if( dev_ && dev_->ready() ) {
        if( pipeline_ ) {
            SDL_ReleaseGPUGraphicsPipeline( dev_->raw(), pipeline_ );
        }
        if( vert_ ) {
            SDL_ReleaseGPUShader( dev_->raw(), vert_ );
        }
        if( frag_ ) {
            SDL_ReleaseGPUShader( dev_->raw(), frag_ );
        }
        if( cascade_tex_ ) {
            SDL_ReleaseGPUTexture( dev_->raw(), cascade_tex_ );
        }
    }
    pipeline_ = nullptr;
    vert_ = nullptr;
    frag_ = nullptr;
    cascade_tex_ = nullptr;
    tex_w_ = 0;
    tex_h_ = 0;
}

void radiance_cascade_pass::record( SDL_GPUCommandBuffer *cb,
                                    SDL_GPUBuffer *emitter_buf, SDL_GPUBuffer *sdf_buf,
                                    std::uint32_t runtime_w, std::uint32_t runtime_h,
                                    const rc_params &params )
{
    if( !ready() || !cb || !emitter_buf || !sdf_buf || runtime_w == 0 || runtime_h == 0 ) {
        return;
    }

    SDL_PushGPUFragmentUniformData( cb, /*slot=*/0, &params, sizeof( params ) );

    SDL_GPUColorTargetInfo ct{};
    ct.texture  = cascade_tex_;
    ct.load_op  = SDL_GPU_LOADOP_DONT_CARE; // fullscreen tri covers the viewport
    ct.store_op = SDL_GPU_STOREOP_STORE;
    ct.cycle    = false;

    SDL_GPURenderPass *rp = SDL_BeginGPURenderPass( cb, &ct, 1, nullptr );
    if( !rp ) {
        dbg( DL::Error ) << "radiance_cascade_pass: BeginGPURenderPass failed: " << SDL_GetError();
        return;
    }
    SDL_BindGPUGraphicsPipeline( rp, pipeline_ );

    // Probe grid = runtime tiles, rendered into the top-left runtime sub-rect of
    // the (transposed) cascade texture: viewport width=runtime_h, height=runtime_w.
    const SDL_GPUViewport vp{
        0.0f, 0.0f,
        static_cast<float>( runtime_h ), static_cast<float>( runtime_w ),
        0.0f, 1.0f
    };
    SDL_SetGPUViewport( rp, &vp );

    // Emitters → t0, SdfBuf → t1 (no sampler / no storage texture in this pass).
    SDL_GPUBuffer *sbufs[2] = { emitter_buf, sdf_buf };
    SDL_BindGPUFragmentStorageBuffers( rp, /*first_slot=*/0, sbufs, 2 );

    SDL_DrawGPUPrimitives( rp, /*num_vertices=*/3, /*num_instances=*/1, 0, 0 );

    SDL_EndGPURenderPass( rp );
}

} // namespace lighting
