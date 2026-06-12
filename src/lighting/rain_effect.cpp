#include "rain_effect.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "debug.h"
#include "game_constants.h"
#include "lighting/gpu_device.h"
#include "lighting/shader_compiler.h"
#include "rng.h"

#define dbg( x ) DebugLogFL( ( x ), DC::SDL )

namespace lighting
{

// ---- Droplet instance layout (matches rain_droplet.vert.hlsl) -----------
struct droplet_instance {
    float dst_x;       // screen-space X centre
    float dst_y;       // screen-space Y centre
    float dst_w;       // quad width  (streak thickness)
    float dst_h;       // quad height (streak length)
    float src_u;       // unused
    float src_v;       // unused
    float src_uw;      // unused
    float src_vh;      // unused
    float tint_r;      // colour R
    float tint_g;      // colour G
    float tint_b;      // colour B
    float tint_a;      // colour A (opacity)
    float rotation;    // wind drift angle (radians, clockwise from vertical)
    float light_mul;   // unused
    float pad1;        // unused
    float pad2;        // unused
};
static_assert( sizeof( droplet_instance ) == 64,
               "droplet_instance must be 64 bytes (wire-stable with vert shader)" );

// ---- Splash params for splat fade shader ---------------------------------
// (moved inline into record() to avoid cbuffer size issues)

// ---- Constructor / Destructor --------------------------------------------

rain_effect::~rain_effect()
{
    shutdown();
}

// ---- Init ---------------------------------------------------------------

bool rain_effect::init( gpu_device &dev, SDL_GPUTextureFormat hdr_format,
                        std::uint32_t screen_w, std::uint32_t screen_h )
{
    shutdown();
    dev_ = &dev;
    hdr_format_ = hdr_format;
    screen_w_ = screen_w;
    screen_h_ = screen_h;

    if( !dev.ready() ) {
        dbg( DL::Error ) << "rain_effect::init: gpu_device not ready";
        return false;
    }

    dbg( DL::Info ) << "rain_effect: init called (screen=" << screen_w << "x" << screen_h << ")";
    init_shader_compiler();

    // ---- Droplet pipeline ------------------------------------------------
    const std::string vert_src  = load_lighting_shader_source( "rain_droplet.vert.hlsl" );
    const std::string frag_src  = load_lighting_shader_source( "rain_droplet.frag.hlsl" );
    if( vert_src.empty() || frag_src.empty() ) {
        dbg( DL::Error ) << "rain_effect: failed to load droplet shader source";
        return false;
    }

    auto v = compile_graphics_shader( dev, vert_src, "main",
                                      SDL_SHADERCROSS_SHADERSTAGE_VERTEX,
                                      "rain_droplet.vert" );
    auto f = compile_graphics_shader( dev, frag_src, "main",
                                      SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT,
                                      "rain_droplet.frag" );
    if( !v || !f ) {
        dbg( DL::Error ) << "rain_effect: droplet shader compile failed";
        return false;
    }

    // Alpha-blended pipeline for droplets onto the HDR world target.
    SDL_GPUColorTargetBlendState blend{};
    blend.enable_blend           = true;
    blend.src_color_blendfactor  = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    blend.dst_color_blendfactor  = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.color_blend_op         = SDL_GPU_BLENDOP_ADD;
    blend.src_alpha_blendfactor  = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    blend.dst_alpha_blendfactor  = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alpha_blend_op         = SDL_GPU_BLENDOP_ADD;
    blend.color_write_mask       = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G |\
                                   SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;

    SDL_GPUColorTargetDescription ctd{};
    ctd.format   = hdr_format_;
    ctd.blend_state = blend;

    SDL_GPUGraphicsPipelineCreateInfo dpi{};
    dpi.vertex_shader   = v.shader;
    dpi.fragment_shader = f.shader;
    dpi.primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    dpi.rasterizer_state.fill_mode  = SDL_GPU_FILLMODE_FILL;
    dpi.rasterizer_state.cull_mode  = SDL_GPU_CULLMODE_NONE;
    dpi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    dpi.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    dpi.target_info.num_color_targets = 1;
    dpi.target_info.color_target_descriptions = &ctd;
    dpi.target_info.has_depth_stencil_target = false;

    droplet_vert_   = v.shader;
    droplet_frag_   = f.shader;
    droplet_pipeline_ = SDL_CreateGPUGraphicsPipeline( dev.raw(), &dpi );
    if( !droplet_pipeline_ ) {
        dbg( DL::Error ) << "rain_effect: droplet pipeline create failed";
        return false;
    }

    // ---- Splat fade pipeline ---------------------------------------------
    const std::string splat_vert_src = load_lighting_shader_source( "tonemap.vert.hlsl" );
    const std::string splat_frag_src = load_lighting_shader_source( "rain_splat.frag.hlsl" );
    if( splat_vert_src.empty() || splat_frag_src.empty() ) {
        dbg( DL::Error ) << "rain_effect: failed to load splat shader source";
        return false;
    }

    auto sv = compile_graphics_shader( dev, splat_vert_src, "main",
                                       SDL_SHADERCROSS_SHADERSTAGE_VERTEX,
                                       "rain_splat.vert" );
    auto sf = compile_graphics_shader( dev, splat_frag_src, "main",
                                       SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT,
                                       "rain_splat.frag" );
    if( !sv || !sf ) {
        dbg( DL::Error ) << "rain_effect: splat shader compile failed";
        return false;
    }

    // Splat pipeline: DONT_CARE load (fully covered by fullscreen tri), no blend.
    SDL_GPUColorTargetBlendState splat_blend{};
    splat_blend.enable_blend = false;
    splat_blend.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G |\
                                   SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;

    SDL_GPUColorTargetDescription ctd_splat{};
    ctd_splat.format   = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ctd_splat.blend_state = splat_blend;

    SDL_GPUGraphicsPipelineCreateInfo spi{};
    spi.vertex_shader   = sv.shader;
    spi.fragment_shader = sf.shader;
    spi.primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    spi.rasterizer_state.fill_mode  = SDL_GPU_FILLMODE_FILL;
    spi.rasterizer_state.cull_mode  = SDL_GPU_CULLMODE_NONE;
    spi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    spi.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    spi.target_info.num_color_targets = 1;
    spi.target_info.color_target_descriptions = &ctd_splat;
    spi.target_info.has_depth_stencil_target = false;

    splat_vert_   = sv.shader;
    splat_frag_   = sf.shader;
    splat_pipeline_ = SDL_CreateGPUGraphicsPipeline( dev.raw(), &spi );
    if( !splat_pipeline_ ) {
        dbg( DL::Error ) << "rain_effect: splat pipeline create failed";
        return false;
    }

    // ---- Splat ping-pong textures (512x512 RGBA8) ------------------------
    SDL_GPUTextureCreateInfo tci{};
    tci.type   = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.usage  = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width                = splat_w_;
    tci.height               = splat_h_;
    tci.layer_count_or_depth = 1;
    tci.num_levels   = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;

    if( !SDL_GPUTextureSupportsFormat( dev.raw(), tci.format, tci.type, tci.usage ) ) {
        dbg( DL::Error ) << "rain_effect: RGBA8 COLOR_TARGET|SAMPLER unsupported";
        return false;
    }

    splat_a_ = SDL_CreateGPUTexture( dev.raw(), &tci );
    splat_b_ = SDL_CreateGPUTexture( dev.raw(), &tci );
    if( !splat_a_ || !splat_b_ ) {
        dbg( DL::Error ) << "rain_effect: splat texture create failed";
        return false;
    }

    // ---- Particle pools --------------------------------------------------
    droplets_.reserve( MAX_DROPLETS );
    splashes_.reserve( MAX_SPLASHES );

    // ---- Persistent droplet instance buffers (transfer + storage, reused each frame) --
    inst_capacity_ = static_cast<std::size_t>( MAX_DROPLETS ) * sizeof( droplet_instance );
    {
        SDL_GPUBufferCreateInfo bci{};
        bci.usage  = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        bci.size   = static_cast<Uint32>( inst_capacity_ );
        inst_storage_buf_ = SDL_CreateGPUBuffer( dev.raw(), &bci );

        SDL_GPUTransferBufferCreateInfo tci{};
        tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tci.size  = static_cast<Uint32>( inst_capacity_ );
        inst_transfer_buf_ = SDL_CreateGPUTransferBuffer( dev.raw(), &tci );
    }
    if( !inst_storage_buf_ || !inst_transfer_buf_ ) {
        dbg( DL::Error ) << "rain_effect: instance buffer create failed";
        return false;
    }

    // ---- Linear sampler for splat texture reads --------------------------
    {
        SDL_GPUSamplerCreateInfo sci{};
        sci.min_filter     = SDL_GPU_FILTER_LINEAR;
        sci.mag_filter     = SDL_GPU_FILTER_LINEAR;
        sci.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        splat_sampler_ = SDL_CreateGPUSampler( dev.raw(), &sci );
    }
    if( !splat_sampler_ ) {
        dbg( DL::Error ) << "rain_effect: splat sampler create failed";
        return false;
    }

    DebugLogFL( DL::Info, DC::Main )
        << "rain_effect: initialised (droplet_cap=" << MAX_DROPLETS
        << ", splash_cap=" << MAX_SPLASHES << ")";

    return true;
}

// ---- Shutdown -----------------------------------------------------------

void rain_effect::shutdown() noexcept
{
    if( dev_ && dev_->ready() ) {
        SDL_ReleaseGPUTexture( dev_->raw(), splat_a_ );
        SDL_ReleaseGPUTexture( dev_->raw(), splat_b_ );
        SDL_ReleaseGPUGraphicsPipeline( dev_->raw(), droplet_pipeline_ );
        SDL_ReleaseGPUGraphicsPipeline( dev_->raw(), splat_pipeline_ );
        SDL_ReleaseGPUShader( dev_->raw(), droplet_vert_ );
        SDL_ReleaseGPUShader( dev_->raw(), droplet_frag_ );
        SDL_ReleaseGPUShader( dev_->raw(), splat_vert_ );
        SDL_ReleaseGPUShader( dev_->raw(), splat_frag_ );
        SDL_ReleaseGPUBuffer( dev_->raw(), inst_storage_buf_ );
        SDL_ReleaseGPUTransferBuffer( dev_->raw(), inst_transfer_buf_ );
        SDL_ReleaseGPUSampler( dev_->raw(), splat_sampler_ );
    }

    droplet_pipeline_ = nullptr;
    splat_pipeline_   = nullptr;
    droplet_vert_     = nullptr;
    droplet_frag_     = nullptr;
    splat_vert_       = nullptr;
    splat_frag_       = nullptr;
    splat_a_          = nullptr;
    splat_b_          = nullptr;
    inst_storage_buf_   = nullptr;
    inst_transfer_buf_  = nullptr;
    inst_capacity_     = 0u;
    splat_sampler_     = nullptr;
    dev_              = nullptr;
    droplets_.clear();
    splashes_.clear();
}

// ---- Particle management ------------------------------------------------

void rain_effect::spawn_droplets( float intensity, float wind_angle_deg,
                                  std::uint32_t screen_w, std::uint32_t screen_h )
{
    if( intensity <= 0.f || screen_w == 0 || screen_h == 0 ) {
        return;
    }

    // Spawn rate: proportional to intensity. At intensity=1.0, spawn ~60 per
    // frame at 60fps (sustains ~2048 concurrent). Scale linearly.
    const float spawn_rate = intensity * 60.f;
    const int   count = static_cast<int>( spawn_rate );

    // Wind drift: angle from vertical, converted to radians.
    // wind_angle_deg is in degrees (0=north=up on compass, clockwise).
    // SDL screen Y goes down, so we negate for correct visual direction.
    const float wind_rad = -wind_angle_deg * 3.14159265f / 180.f;

    for( int i = 0; i < count && static_cast<int>( droplets_.size() ) < MAX_DROPLETS; ++i ) {
        // Spawn at top of screen with slight horizontal random offset.
        const float x = static_cast<float>( rng( 0, static_cast<int>( screen_w - 1 ) ) );
        const float y = static_cast<float>( rng( -screen_h / 4, 0 ) );

        // Base fall speed + intensity scaling.
        const float base_vel_y = 3.f + intensity * 2.f;
        const float vel_x = std::sin( wind_rad ) * ( 1.f + intensity * 0.5f );
        const float vel_y = base_vel_y + static_cast<float>( rng( -5, 5 ) ) / 10.f;

        // Slight opacity variation for depth feel.
        const float opacity = 0.4f + static_cast<float>( rng( 0, 60 ) ) / 100.f;

        droplets_.push_back( rain_droplet{
            x, y, vel_x, vel_y, opacity, 1.0f
        } );
    }
}

void rain_effect::update_droplets( std::uint32_t screen_w, std::uint32_t screen_h )
{
    if( screen_w == 0 || screen_h == 0 ) {
        return;
    }

    // Remove off-screen droplets and update positions.
    auto it = droplets_.begin();
    while( it != droplets_.end() ) {
        it->screen_x += it->vel_x;
        it->screen_y += it->vel_y;

        // Remove if below screen or outside horizontal bounds.
        const bool off_screen = it->screen_y > static_cast<float>( screen_h + 20 );
        const bool off_left   = it->screen_x < -10.f;
        const bool off_right  = it->screen_x > static_cast<float>( screen_w + 10 );

        if( off_screen || off_left || off_right ) {
            it = droplets_.erase( it );
        } else {
            ++it;
        }
    }
}

void rain_effect::spawn_splashes()
{
    // Find droplets that just crossed the ground plane (bottom of screen).
    for( auto &d : droplets_ ) {
        if( d.screen_y >= static_cast<float>( screen_h_ - 2 ) &&
            d.screen_y <= static_cast<float>( screen_h_ + 5 ) &&
            static_cast<int>( splashes_.size() ) < MAX_SPLASHES )
        {
            // Mark droplet as "splashed" by pushing it below ground so we don't
            // spawn another splash for the same droplet.
            d.screen_y = static_cast<float>( screen_h_ + 100 );

            splashes_.push_back( rain_splash{
                .x        = d.screen_x,
                .y        = static_cast<float>( screen_h_ - 2 ),
                .intensity = d.opacity * 0.5f, // splash brightness proportional to droplet opacity
                .age      = 0u,
                .max_age  = 10u + static_cast<uint32_t>( rng( 0, 5 ) ),
            } );
        }
    }
}

void rain_effect::update_splashes()
{
    // Remove expired splashes.
    auto it = splashes_.begin();
    while( it != splashes_.end() ) {
        ++it->age;
        if( it->age >= it->max_age ) {
            it = splashes_.erase( it );
        } else {
            ++it;
        }
    }
}

// ---- Per-frame record ---------------------------------------------------

void rain_effect::record( SDL_GPUCommandBuffer *cb, SDL_GPUTexture *world_tex,
                          std::uint32_t world_w, std::uint32_t world_h,
                          const rain_params &params )
{
    if( !ready() || !cb || !world_tex || world_w == 0 || world_h == 0 ) {
        return;
    }

    dbg( DL::Info ) << "rain_effect: record called (active=" << params.active
                    << ", intensity=" << params.intensity << ")";

    // ---- Update particles (CPU) ------------------------------------------
    if( params.active && params.intensity > 0.f ) {
        spawn_droplets( params.intensity, params.wind_angle, screen_w_, screen_h_ );
    }
    update_droplets( screen_w_, screen_h_ );

    // Spawn splashes for droplets hitting the ground plane.
    if( params.active ) {
        spawn_splashes();
    }
    update_splashes();

    dbg( DL::Info ) << "rain_effect: particles (droplets=" << droplets_.size()
                    << ", splashes=" << splashes_.size() << ")";

    // ---- Draw droplets onto world_target ---------------------------------
    if( !droplets_.empty() && droplet_pipeline_ ) {
        const uint32_t n = static_cast<uint32_t>( droplets_.size() );

        // Build instance array on CPU.
        std::vector<droplet_instance> instances;
        instances.reserve( n );

        for( const auto &d : droplets_ ) {
            // Streak dimensions: thin width, elongated height.
            const float w = 1.5f + static_cast<float>( rng( 0, 3 ) ) / 10.f;
            const float h = 6.f + d.opacity * 8.f;

            // Wind rotation: small tilt from vertical based on wind angle.
            const float wind_rad = params.wind_angle * 3.14159265f / 180.f;
            const float rot = std::sin( -wind_rad ) * 0.3f;

            instances.push_back( droplet_instance{
                .dst_x   = d.screen_x,
                .dst_y   = d.screen_y,
                .dst_w   = w,
                .dst_h   = h,
                .src_u   = 0.f, .src_v = 0.f,
                .src_uw  = 1.f, .src_vh = 1.f, // unused by procedural shader
                .tint_r  = 0.6f, // light blue tint
                .tint_g  = 0.75f,
                .tint_b  = 0.9f,
                .tint_a  = d.opacity,
                .rotation = rot,
                .light_mul = 0.f, .pad1 = 0.f, .pad2 = 0.f,
            } );
        }

        // Upload instances via transfer buffer (SDL3 no longer supports direct GPU buffer mapping).
        {
            const Uint32 byte_size = static_cast<Uint32>( n * sizeof( droplet_instance ) );
            void *mapped = SDL_MapGPUTransferBuffer( dev_->raw(), inst_transfer_buf_,
                                                     /*cycle=*/true );
            if( mapped ) {
                std::memcpy( mapped, instances.data(), byte_size );
                SDL_UnmapGPUTransferBuffer( dev_->raw(), inst_transfer_buf_ );

                SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass( cb );
                if( cp ) {
                    SDL_GPUTransferBufferLocation src{};
                    src.transfer_buffer = inst_transfer_buf_;
                    src.offset = 0;
                    SDL_GPUBufferRegion dst{};
                    dst.buffer   = inst_storage_buf_;
                    dst.offset   = 0;
                    dst.size     = byte_size;
                    SDL_UploadToGPUBuffer( cp, &src, &dst, /*cycle=*/true );
                    SDL_EndGPUCopyPass( cp );
                } else {
                    dbg( DL::Error ) << "rain_effect: copy pass begin failed";
                }
            } else {
                dbg( DL::Error ) << "MapGPUTransferBuffer failed: " << SDL_GetError();
            }
        }

        // Begin render pass on world_target.
        SDL_GPUColorTargetInfo ct{};
        ct.texture   = world_tex;
        ct.load_op   = SDL_GPU_LOADOP_LOAD;    // preserve existing terrain
        ct.store_op  = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *rp = SDL_BeginGPURenderPass( cb, &ct, 1, nullptr );
        if( rp ) {
            SDL_BindGPUGraphicsPipeline( rp, droplet_pipeline_ );

            // Push frame params (target size + instance base).
            struct { float target_size_w; float target_size_h; uint32_t instance_base; uint32_t fp_pad; } fp;
            fp.target_size_w = static_cast<float>( world_w );
            fp.target_size_h = static_cast<float>( world_h );
            fp.instance_base = 0u;
            fp.fp_pad = 0u;
            SDL_PushGPUVertexUniformData( cb, 0, &fp, sizeof( fp ) );

            // Bind instance storage buffer.
            SDL_BindGPUVertexStorageBuffers( rp, /*first_slot=*/0, &inst_storage_buf_, 1 );

            // Draw all instances (6 vertices per quad: 2 triangles).
            SDL_DrawGPUPrimitives( rp, 6, n, 0, 0 );

            dbg( DL::Info ) << "rain_effect: drew " << n << " droplet quads";

            SDL_EndGPURenderPass( rp );
        } else {
            dbg( DL::Error ) << "rain_effect: droplet render pass begin failed";
        }
    } else if( !droplets_.empty() ) {
        dbg( DL::Warn ) << "rain_effect: no droplet_pipeline_ or empty droplets";
    }

    // ---- Splat fade + splash accumulation --------------------------------
    if( splat_pipeline_ && splat_a_ && splat_b_ && splat_sampler_ ) {
        // Determine which texture is "in" and which is "out".
        SDL_GPUTexture *splat_in  = splat_a_;
        SDL_GPUTexture *splat_out = splat_b_;

        // Push splash params uniform (fade_rate + count).
        struct { float fade_rate; uint32_t splash_count; float pad0, pad1; } sp;
        sp.fade_rate     = params.fade_rate;
        sp.splash_count  = static_cast<uint32_t>( splashes_.size() );
        sp.pad0          = 0.f;
        sp.pad1          = 0.f;
        SDL_PushGPUFragmentUniformData( cb, 0, &sp, sizeof( sp ) );

        // Push per-splash position + intensity data (3 × 512 floats).
        float splash_x[512], splash_y[512], splash_intensity[512];
        std::memset( splash_x, 0, sizeof( splash_x ) );
        std::memset( splash_y, 0, sizeof( splash_y ) );
        std::memset( splash_intensity, 0, sizeof( splash_intensity ) );
        {
            const uint32_t count = static_cast<uint32_t>( splashes_.size() );
            for( uint32_t i = 0u; i < count && i < 512u; ++i ) {
                splash_x[ i ]       = splashes_[ i ].x;
                splash_y[ i ]       = splashes_[ i ].y;
                // Fade intensity over lifetime: bright at spawn, dim to nothing.
                const float life_ratio = static_cast<float>( splashes_[ i ].age ) /
                                         static_cast<float>( splashes_[ i ].max_age );
                splash_intensity[ i ] = splashes_[ i ].intensity *
                                        ( 1.0f - life_ratio );
            }
        }
        SDL_PushGPUFragmentUniformData( cb, 1, splash_x, sizeof( splash_x ) );

        // Begin render pass on ping texture.
        SDL_GPUColorTargetInfo ct{};
        ct.texture   = splat_out;
        ct.load_op   = SDL_GPU_LOADOP_DONT_CARE;  // fully covered by fullscreen tri
        ct.store_op  = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *rp = SDL_BeginGPURenderPass( cb, &ct, 1, nullptr );
        if( rp ) {
            SDL_BindGPUGraphicsPipeline( rp, splat_pipeline_ );

            // Viewport covers full splat texture.
            SDL_GPUViewport vp{ 0.0f, 0.0f,
                                static_cast<float>( splat_w_ ),
                                static_cast<float>( splat_h_ ),
                                0.0f, 1.0f };
            SDL_SetGPUViewport( rp, &vp );

            // Bind input splat texture + sampler.
            SDL_GPUTextureSamplerBinding tsb{};
            tsb.texture = splat_in;
            tsb.sampler = splat_sampler_;
            SDL_BindGPUFragmentSamplers( rp, /*first_slot=*/0, &tsb, 1 );

            // Draw fullscreen triangle.
            SDL_DrawGPUPrimitives( rp, 3, 1, 0, 0 );
            SDL_EndGPURenderPass( rp );

            dbg( DL::Info ) << "rain_effect: splat pass completed (fade=" << params.fade_rate
                            << ", splash_count=" << sp.splash_count << ")";
        } else {
            dbg( DL::Error ) << "rain_effect: splat render pass begin failed";
        }

        // Swap ping-pong textures for next frame.
        std::swap( splat_a_, splat_b_ );
    } else if( !splashes_.empty() ) {
        dbg( DL::Warn ) << "rain_effect: no splat pipeline or textures (splash_count=" << splashes_.size() << ")";
    }
}

} // namespace lighting
