#include "radiance_cascade_pass.h"

#include <cstdint>
#include <cstring>
#include <string>

#include "debug.h"
#include "lighting/gpu_device.h"
#include "lighting/shader_compiler.h"

#define dbg( x ) DebugLogFL( ( x ), DC::SDL )

namespace lighting
{

// RGBA16F: HDR irradiance (gathered radiance can exceed 1.0 before the sprite
// scales it). COLOR_TARGET so the passes render into it; GRAPHICS_STORAGE_READ
// so the field is sampled by pass 2 and the cascade is read by the sprite.
static constexpr SDL_GPUTextureFormat CASCADE_FORMAT =
    SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;

// IEEE half (R16G16B16A16_FLOAT storage) → float, for the readback oracle.
static float half_to_float( std::uint16_t h )
{
    const std::uint32_t sign = static_cast<std::uint32_t>( h & 0x8000u ) << 16;
    std::uint32_t exp  = ( h >> 10 ) & 0x1Fu;
    std::uint32_t mant = h & 0x3FFu;
    std::uint32_t f;
    if( exp == 0u ) {
        if( mant == 0u ) {
            f = sign;
        } else {
            exp = 127u - 15u + 1u;
            while( ( mant & 0x400u ) == 0u ) {
                mant <<= 1; --exp;
            }
            mant &= 0x3FFu;
            f = sign | ( exp << 23 ) | ( mant << 13 );
        }
    } else if( exp == 0x1Fu ) {
        f = sign | 0x7F800000u | ( mant << 13 );
    } else {
        f = sign | ( ( exp - 15u + 127u ) << 23 ) | ( mant << 13 );
    }
    float out;
    std::memcpy( &out, &f, sizeof( out ) );
    return out;
}

radiance_cascade_pass::~radiance_cascade_pass()
{
    shutdown();
}

// Build one fullscreen-tri pipeline writing CASCADE_FORMAT, given a frag shader.
static SDL_GPUGraphicsPipeline *make_pipeline( SDL_GPUDevice *d,
        SDL_GPUShader *vert, SDL_GPUShader *frag )
{
    SDL_GPUColorTargetBlendState blend{};
    blend.enable_blend = false;
    blend.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G |
                             SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
    SDL_GPUColorTargetDescription color_target{};
    color_target.format = CASCADE_FORMAT;
    color_target.blend_state = blend;
    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader   = vert;
    pci.fragment_shader = frag;
    pci.primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.rasterizer_state.fill_mode  = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode  = SDL_GPU_CULLMODE_NONE;
    pci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pci.target_info.num_color_targets = 1;
    pci.target_info.color_target_descriptions = &color_target;
    pci.target_info.has_depth_stencil_target  = false;
    return SDL_CreateGPUGraphicsPipeline( d, &pci );
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

    const std::string vert_src   = load_lighting_shader_source( "tonemap.vert.hlsl" );
    const std::string field_src  = load_lighting_shader_source( "rc.frag.hlsl" );
    const std::string bounce_src = load_lighting_shader_source( "rc_bounce.frag.hlsl" );
    // Liveness probe: confirms the running game actually loaded THIS edited
    // rc.frag from datadir (vs a stale synced/copied data/ dir). If this tag is
    // absent, the shader edits are not reaching the binary — fix deployment, not
    // the shader.
    DebugLogFL( DL::Info, DC::Main )
            << "rc.frag liveness: field_src len=" << field_src.size()
            << " has_tag_i=" << ( field_src.find( "rc_field_tag_i" ) != std::string::npos ? 1 : 0 );

    auto v  = compile_graphics_shader( dev, vert_src, "main",
                                       SDL_SHADERCROSS_SHADERSTAGE_VERTEX, "rc.vert" );
    auto ff = compile_graphics_shader( dev, field_src, "main",
                                       SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "rc.frag" );
    auto bf = compile_graphics_shader( dev, bounce_src, "main",
                                       SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "rc_bounce.frag" );
    if( !v || !ff || !bf ) {
        if( v ) {
            SDL_ReleaseGPUShader( dev.raw(), v.shader );
        }
        if( ff ) {
            SDL_ReleaseGPUShader( dev.raw(), ff.shader );
        }
        if( bf ) {
            SDL_ReleaseGPUShader( dev.raw(), bf.shader );
        }
        dbg( DL::Error ) << "radiance_cascade_pass: shader compile failed";
        return false;
    }
    vert_        = v.shader;
    field_frag_  = ff.shader;
    bounce_frag_ = bf.shader;

    // Structural gate (DC::Main — DC::SDL is filtered): field gathers emitters
    // (2 storage buffers, sampler-less); bounce reads the field texture + SDF
    // (1 storage texture + 1 storage buffer).
    DebugLogFL( DL::Info, DC::Main )
            << "rc.frag (field) reflection: samplers=" << ff.resources.num_samplers
            << " storage_textures=" << ff.resources.num_storage_textures
            << " storage_buffers=" << ff.resources.num_storage_buffers
            << " (expects storage_buffers=2)";
    DebugLogFL( DL::Info, DC::Main )
            << "rc_bounce.frag reflection: samplers=" << bf.resources.num_samplers
            << " storage_textures=" << bf.resources.num_storage_textures
            << " storage_buffers=" << bf.resources.num_storage_buffers
            << " (expects storage_textures=1 storage_buffers=1)";

    // Allocate the cascade textures FIRST, before the pipelines. Consumers
    // (sprite.frag IndirectTex) bind cascade_texture() unconditionally and the
    // shader expects a valid storage texture every frame; if pipeline creation
    // later fails on a backend (e.g. D3D12 0x80070057) we must STILL hand back a
    // valid cleared-black cascade so the bind never sees null. ready() gates
    // record(), so a failed pipeline just leaves the cascade cleared = GI off.
    radiance_field_tex_ = create_texture( tex_w, tex_h );
    cascade_tex_        = create_texture( tex_w, tex_h );
    if( !radiance_field_tex_ || !cascade_tex_ ) {
        return false;
    }
    tex_w_ = tex_w;
    tex_h_ = tex_h;
    clear_texture( radiance_field_tex_ );
    clear_texture( cascade_tex_ );

    // Create + check each pipeline separately: a second SDL_CreateGPU* call
    // clobbers the first's SDL_GetError(), so a combined check can't tell field
    // from bounce. Both are needed; log whichever fails with its own error.
    field_pipeline_ = make_pipeline( dev.raw(), vert_, field_frag_ );
    if( !field_pipeline_ ) {
        DebugLogFL( DL::Error, DC::Main )
                << "radiance_cascade_pass FIELD pipeline (st=0 sb=2): " << SDL_GetError()
                << " — GI disabled, cascade bound as cleared black";
        return false;
    }
    bounce_pipeline_ = make_pipeline( dev.raw(), vert_, bounce_frag_ );
    if( !bounce_pipeline_ ) {
        DebugLogFL( DL::Error, DC::Main )
                << "radiance_cascade_pass BOUNCE pipeline (st=1 sb=1): " << SDL_GetError()
                << " — GI disabled, cascade bound as cleared black";
        return false;
    }
    return true;
}

SDL_GPUTexture *radiance_cascade_pass::create_texture( std::uint32_t tex_w, std::uint32_t tex_h )
{
    if( !SDL_GPUTextureSupportsFormat( dev_->raw(), CASCADE_FORMAT, SDL_GPU_TEXTURETYPE_2D,
                                       SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                                       SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ ) ) {
        DebugLogFL( DL::Error, DC::Main )
                << "radiance_cascade_pass: RGBA16F COLOR_TARGET|GRAPHICS_STORAGE_READ unsupported";
        return nullptr;
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
    SDL_GPUTexture *t = SDL_CreateGPUTexture( dev_->raw(), &tci );
    if( !t ) {
        DebugLogFL( DL::Error, DC::Main ) << "radiance_cascade_pass: tex create: " << SDL_GetError();
    }
    return t;
}

void radiance_cascade_pass::clear_texture( SDL_GPUTexture *tex )
{
    if( !tex ) {
        return;
    }
    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer( dev_->raw() );
    if( !cb ) {
        return;
    }
    SDL_GPUColorTargetInfo ct{};
    ct.texture  = tex;
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
    if( radiance_field_tex_ && cascade_tex_ && tex_w == tex_w_ && tex_h == tex_h_ ) {
        return true;
    }
    if( dev_ && dev_->ready() ) {
        if( radiance_field_tex_ ) {
            SDL_ReleaseGPUTexture( dev_->raw(), radiance_field_tex_ );
            radiance_field_tex_ = nullptr;
        }
        if( cascade_tex_ ) {
            SDL_ReleaseGPUTexture( dev_->raw(), cascade_tex_ );
            cascade_tex_ = nullptr;
        }
    }
    radiance_field_tex_ = create_texture( tex_w, tex_h );
    cascade_tex_        = create_texture( tex_w, tex_h );
    if( !radiance_field_tex_ || !cascade_tex_ ) {
        return false;
    }
    tex_w_ = tex_w;
    tex_h_ = tex_h;
    clear_texture( radiance_field_tex_ );
    clear_texture( cascade_tex_ );
    return true;
}

void radiance_cascade_pass::shutdown() noexcept
{
    if( dev_ && dev_->ready() ) {
        if( field_pipeline_ ) {
            SDL_ReleaseGPUGraphicsPipeline( dev_->raw(), field_pipeline_ );
        }
        if( bounce_pipeline_ ) {
            SDL_ReleaseGPUGraphicsPipeline( dev_->raw(), bounce_pipeline_ );
        }
        if( vert_ ) {
            SDL_ReleaseGPUShader( dev_->raw(), vert_ );
        }
        if( field_frag_ ) {
            SDL_ReleaseGPUShader( dev_->raw(), field_frag_ );
        }
        if( bounce_frag_ ) {
            SDL_ReleaseGPUShader( dev_->raw(), bounce_frag_ );
        }
        if( radiance_field_tex_ ) {
            SDL_ReleaseGPUTexture( dev_->raw(), radiance_field_tex_ );
        }
        if( cascade_tex_ ) {
            SDL_ReleaseGPUTexture( dev_->raw(), cascade_tex_ );
        }
    }
    vert_ = field_frag_ = bounce_frag_ = nullptr;
    field_pipeline_ = bounce_pipeline_ = nullptr;
    radiance_field_tex_ = cascade_tex_ = nullptr;
    tex_w_ = tex_h_ = 0;
}

void radiance_cascade_pass::record( SDL_GPUCommandBuffer *cb,
                                    SDL_GPUBuffer *emitter_buf, SDL_GPUBuffer *sdf_buf,
                                    std::uint32_t runtime_w, std::uint32_t runtime_h,
                                    const rc_params &params )
{
    if( !ready() || !cb || !emitter_buf || !sdf_buf || runtime_w == 0 || runtime_h == 0 ) {
        return;
    }

    // Probe grid = runtime tiles in the top-left runtime sub-rect of the
    // (transposed) textures: viewport width=runtime_h, height=runtime_w.
    const SDL_GPUViewport vp{
        0.0f, 0.0f,
        static_cast<float>( runtime_h ), static_cast<float>( runtime_w ),
        0.0f, 1.0f
    };

    // ── Pass 1: FIELD — per-tile direct radiance (occluded emitter gather). ──
    SDL_PushGPUFragmentUniformData( cb, /*slot=*/0, &params, sizeof( params ) );
    {
        SDL_GPUColorTargetInfo ct{};
        ct.texture  = radiance_field_tex_;
        ct.load_op  = SDL_GPU_LOADOP_DONT_CARE;
        ct.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *rp = SDL_BeginGPURenderPass( cb, &ct, 1, nullptr );
        if( !rp ) {
            dbg( DL::Error ) << "rc field pass: BeginGPURenderPass failed: " << SDL_GetError();
            return;
        }
        SDL_BindGPUGraphicsPipeline( rp, field_pipeline_ );
        SDL_SetGPUViewport( rp, &vp );
        SDL_GPUBuffer *sbufs[2] = { emitter_buf, sdf_buf }; // t0, t1
        SDL_BindGPUFragmentStorageBuffers( rp, /*first_slot=*/0, sbufs, 2 );
        SDL_DrawGPUPrimitives( rp, 3, 1, 0, 0 );
        SDL_EndGPURenderPass( rp );
    }

    // ── Pass 2: BOUNCE — march rays through the field → colored bounce. ──
    SDL_PushGPUFragmentUniformData( cb, /*slot=*/0, &params, sizeof( params ) );
    {
        SDL_GPUColorTargetInfo ct{};
        ct.texture  = cascade_tex_;
        ct.load_op  = SDL_GPU_LOADOP_DONT_CARE;
        ct.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *rp = SDL_BeginGPURenderPass( cb, &ct, 1, nullptr );
        if( !rp ) {
            dbg( DL::Error ) << "rc bounce pass: BeginGPURenderPass failed: " << SDL_GetError();
            return;
        }
        SDL_BindGPUGraphicsPipeline( rp, bounce_pipeline_ );
        SDL_SetGPUViewport( rp, &vp );
        // RadianceField → storage-texture slot 0 (t0); SdfBuf → storage-buffer
        // slot 0 (t1, after the storage texture in t-space).
        SDL_BindGPUFragmentStorageTextures( rp, /*first_slot=*/0, &radiance_field_tex_, 1 );
        SDL_BindGPUFragmentStorageBuffers( rp, /*first_slot=*/0, &sdf_buf, 1 );
        SDL_DrawGPUPrimitives( rp, 3, 1, 0, 0 );
        SDL_EndGPURenderPass( rp );
    }
}

void radiance_cascade_pass::debug_log_stats( std::uint32_t runtime_w, std::uint32_t runtime_h )
{
    if( !cascade_tex_ || !dev_ || !dev_->ready() || runtime_w == 0 || runtime_h == 0 ) {
        return;
    }
    SDL_GPUDevice *d = dev_->raw();
    const std::uint32_t bytes = tex_w_ * tex_h_ * 4u * 2u; // RGBA16F = 8 B/texel
    SDL_GPUTransferBufferCreateInfo tbci{};
    tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tbci.size  = bytes;
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer( d, &tbci );
    if( !tb ) {
        return;
    }

    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer( d );
    if( !cb ) {
        SDL_ReleaseGPUTransferBuffer( d, tb );
        return;
    }
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass( cb );
    SDL_GPUTextureRegion region{};
    region.texture = cascade_tex_;
    region.w = tex_h_; // texture width (transposed)
    region.h = tex_w_; // texture height
    region.d = 1;
    SDL_GPUTextureTransferInfo dst{};
    dst.transfer_buffer = tb;
    dst.pixels_per_row  = tex_h_;
    SDL_DownloadFromGPUTexture( cp, &region, &dst );
    SDL_EndGPUCopyPass( cp );
    SDL_SubmitGPUCommandBuffer( cb );
    SDL_WaitForGPUIdle( d ); // synchronous — dev oracle only

    const std::uint16_t *px = static_cast<const std::uint16_t *>(
                                  SDL_MapGPUTransferBuffer( d, tb, false ) );
    if( !px ) {
        SDL_ReleaseGPUTransferBuffer( d, tb );
        return;
    }

    // cascade_tex_ is transposed: texel(col,row)=tile(x=row,y=col), width=tex_h_.
    // Stats over the runtime probe region: row∈[0,runtime_w), col∈[0,runtime_h).
    double sum = 0.0, cx = 0.0, cy = 0.0, wsum = 0.0;
    float  mx = 0.0f;
    long   nz = 0;
    const std::uint32_t W = tex_h_; // texel row stride
    for( std::uint32_t row = 0; row < runtime_w; ++row ) {
        for( std::uint32_t col = 0; col < runtime_h; ++col ) {
            const std::uint32_t idx = ( row * W + col ) * 4u;
            const float lum = half_to_float( px[idx + 0] ) + half_to_float( px[idx + 1] )
                              + half_to_float( px[idx + 2] );
            sum += lum;
            if( lum > mx ) {
                mx = lum;
            }
            if( lum > 0.0001f ) {
                ++nz;
                cx += static_cast<double>( row ) * lum; // tile x
                cy += static_cast<double>( col ) * lum; // tile y
                wsum += lum;
            }
        }
    }
    SDL_UnmapGPUTransferBuffer( d, tb );
    SDL_ReleaseGPUTransferBuffer( d, tb );

    const double cxn = wsum > 0.0 ? cx / wsum : -1.0;
    const double cyn = wsum > 0.0 ? cy / wsum : -1.0;
    DebugLogFL( DL::Info, DC::Main )
            << "rc cascade readback [" << runtime_w << "x" << runtime_h << "]: sum=" << sum
            << " max=" << mx << " nonzero=" << nz
            << " centroid_tile=(" << cxn << "," << cyn << ")";
}

} // namespace lighting
