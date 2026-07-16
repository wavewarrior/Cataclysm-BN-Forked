#include "hud_particle_effect.h"

#include "debug.h"
#include "lighting/gpu_device.h"
#include "lighting/shader_compiler.h"
#include "rng.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#define dbg(x) DebugLogFL((x), DC::SDL)

namespace lighting {

// GPU-side instance layout matching hud_particle.vert.hlsl ParticleInstance.
// All scalar floats to avoid HLSL alignment pitfalls (matches rain_effect pattern).
struct particle_gpu_instance {
    float x, y, size, alpha;
    float r, g, b, rotation;
    float pad0, pad1, pad2, pad3;
}; // 48 bytes (12 floats)
static_assert( sizeof( particle_gpu_instance ) == 48 );

// ---- Constructor / Destructor --------------------------------------------

hud_particle_effect::~hud_particle_effect()
{
    shutdown();
}

// ---- Init ---------------------------------------------------------------

auto hud_particle_effect::init( gpu_device &dev, SDL_GPUTextureFormat ui_format,
                                 std::uint32_t screen_w, std::uint32_t screen_h ) -> bool
{
    shutdown();
    dev_ = &dev;
    ui_format_ = ui_format;
    ( void )screen_w;
    ( void )screen_h;

    if( !dev.ready() ) {
        dbg( DL::Error ) << "hud_particle_effect::init: gpu_device not ready";
        return false;
    }

    init_shader_compiler();

    const auto vert_src = load_lighting_shader_source( "hud_particle.vert.hlsl" );
    const auto frag_src = load_lighting_shader_source( "hud_particle.frag.hlsl" );
    if( vert_src.empty() || frag_src.empty() ) {
        dbg( DL::Error ) << "hud_particle_effect: failed to load shader source";
        return false;
    }

    auto v = compile_graphics_shader(
        dev, vert_src, "main", SDL_SHADERCROSS_SHADERSTAGE_VERTEX, "hud_particle.vert" );
    auto f = compile_graphics_shader(
        dev, frag_src, "main", SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "hud_particle.frag" );
    if( !v || !f ) {
        if( v ) { SDL_ReleaseGPUShader( dev.raw(), v.shader ); }
        if( f ) { SDL_ReleaseGPUShader( dev.raw(), f.shader ); }
        dbg( DL::Error ) << "hud_particle_effect: shader compile failed";
        return false;
    }
    particle_vert_ = v.shader;
    particle_frag_ = f.shader;

    // Pipeline: premultiplied alpha blend, matching rain_effect pattern.
    SDL_GPUColorTargetBlendState blend{};
    blend.enable_blend = true;
    blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G
                           | SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;

    SDL_GPUColorTargetDescription ctd{};
    ctd.format = ui_format_;
    ctd.blend_state = blend;

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = particle_vert_;
    pci.fragment_shader = particle_frag_;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pci.target_info.num_color_targets = 1;
    pci.target_info.color_target_descriptions = &ctd;
    pci.target_info.has_depth_stencil_target = false;

    particle_pipeline_ = SDL_CreateGPUGraphicsPipeline( dev.raw(), &pci );
    if( !particle_pipeline_ ) {
        dbg( DL::Error ) << "hud_particle_effect: pipeline creation failed";
        return false;
    }

    // Transfer + storage buffers for instanced draw.
    constexpr auto buf_size = static_cast<Uint32>( MAX_PARTICLES * sizeof( particle_gpu_instance ) );

    SDL_GPUTransferBufferCreateInfo xfer_ci{};
    xfer_ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    xfer_ci.size = buf_size;
    particle_xfer_ = SDL_CreateGPUTransferBuffer( dev.raw(), &xfer_ci );

    SDL_GPUBufferCreateInfo stor_ci{};
    stor_ci.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    stor_ci.size = buf_size;
    particle_storage_ = SDL_CreateGPUBuffer( dev.raw(), &stor_ci );

    if( !particle_xfer_ || !particle_storage_ ) {
        dbg( DL::Error ) << "hud_particle_effect: buffer creation failed";
        return false;
    }

    particles_.reserve( MAX_PARTICLES );
    return true;
}

// ---- Shutdown -----------------------------------------------------------

auto hud_particle_effect::shutdown() noexcept -> void
{
    if( dev_ ) {
        if( particle_pipeline_ ) { SDL_ReleaseGPUGraphicsPipeline( dev_->raw(), particle_pipeline_ ); }
        if( particle_vert_ ) { SDL_ReleaseGPUShader( dev_->raw(), particle_vert_ ); }
        if( particle_frag_ ) { SDL_ReleaseGPUShader( dev_->raw(), particle_frag_ ); }
        if( particle_storage_ ) { SDL_ReleaseGPUBuffer( dev_->raw(), particle_storage_ ); }
        if( particle_xfer_ ) { SDL_ReleaseGPUTransferBuffer( dev_->raw(), particle_xfer_ ); }
    }
    particle_pipeline_ = nullptr;
    particle_vert_ = nullptr;
    particle_frag_ = nullptr;
    particle_storage_ = nullptr;
    particle_xfer_ = nullptr;
    particles_.clear();
    spawn_accumulator_ = 0.f;
    dev_ = nullptr;
}

// ---- Particle spawning ----------------------------------------------------
auto hud_particle_effect::spawn_particle( const hud_particle_params &params ) -> hud_particle
{
    hud_particle p{};
    const float w = static_cast<float>( params.screen_w );
    const float h = static_cast<float>( params.screen_h );

    switch( params.type ) {
        case hud_emitter_type::ember:
            p.x = static_cast<float>( rng_float( 0.0, w ) );
            p.y = h * static_cast<float>( rng_float( 0.5, 1.0 ) );
            p.vx = static_cast<float>( rng_float( -5.0, 5.0 ) );
            p.vy = static_cast<float>( rng_float( -15.0, -5.0 ) );
            p.size = static_cast<float>( rng_float( 2.0, 4.0 ) );
            p.lifetime = static_cast<float>( rng_float( 2.0, 4.0 ) );
            p.rot_speed = static_cast<float>( rng_float( -30.0, 30.0 ) );
            p.r = static_cast<float>( rng_float( 0.9, 1.0 ) );
            p.g = static_cast<float>( rng_float( 0.3, 0.6 ) );
            p.b = static_cast<float>( rng_float( 0.0, 0.1 ) );
            break;
        case hud_emitter_type::dust:
            p.x = -10.f;
            p.y = static_cast<float>( rng_float( 0.0, h ) );
            p.vx = static_cast<float>( rng_float( 10.0, 30.0 ) );
            p.vy = static_cast<float>( rng_float( -2.0, 2.0 ) );
            p.size = static_cast<float>( rng_float( 1.0, 3.0 ) );
            p.lifetime = static_cast<float>( rng_float( 5.0, 8.0 ) );
            p.rot_speed = static_cast<float>( rng_float( -10.0, 10.0 ) );
            p.r = static_cast<float>( rng_float( 0.5, 0.7 ) );
            p.g = static_cast<float>( rng_float( 0.4, 0.6 ) );
            p.b = static_cast<float>( rng_float( 0.3, 0.5 ) );
            break;
        case hud_emitter_type::pollen:
            p.x = static_cast<float>( rng_float( 0.0, w ) );
            p.y = h + 10.f;
            p.vx = static_cast<float>( rng_float( -5.0, 5.0 ) );
            p.vy = static_cast<float>( rng_float( -8.0, -3.0 ) );
            p.size = static_cast<float>( rng_float( 1.0, 2.0 ) );
            p.lifetime = static_cast<float>( rng_float( 6.0, 10.0 ) );
            p.rot_speed = static_cast<float>( rng_float( -20.0, 20.0 ) );
            p.r = static_cast<float>( rng_float( 0.7, 0.9 ) );
            p.g = static_cast<float>( rng_float( 0.8, 1.0 ) );
            p.b = static_cast<float>( rng_float( 0.2, 0.4 ) );
            break;
        case hud_emitter_type::snow:
            p.x = static_cast<float>( rng_float( 0.0, w ) );
            p.y = -10.f;
            p.vx = static_cast<float>( rng_float( -10.0, 10.0 ) );
            p.vy = static_cast<float>( rng_float( 10.0, 25.0 ) );
            p.size = static_cast<float>( rng_float( 2.0, 4.0 ) );
            p.lifetime = static_cast<float>( rng_float( 4.0, 6.0 ) );
            p.rot_speed = static_cast<float>( rng_float( -40.0, 40.0 ) );
            p.r = 1.0f;
            p.g = 1.0f;
            p.b = 1.0f;
            break;
    }

    p.alpha = params.intensity;
    return p;
}

// ---- Particle management ------------------------------------------------

auto hud_particle_effect::update_particles( float dt ) -> void
{
    for( auto &p : particles_ ) {
        p.age += dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.rotation += p.rot_speed * dt;

        // Alpha fade-out in last 30% of lifetime
        const float fade_start = p.lifetime * 0.7f;
        if( p.age > fade_start ) {
            p.alpha *= std::max( 0.f, 1.f - ( p.age - fade_start ) / ( p.lifetime - fade_start ) );
        }
    }

    // Remove expired particles
    std::erase_if( particles_, []( const hud_particle &p ) {
        return p.age >= p.lifetime || p.alpha <= 0.01f;
    } );
}

// ---- Upload instances -----------------------------------------------------

auto hud_particle_effect::upload_instances(
    SDL_GPUCommandBuffer *cb, const std::vector<hud_particle> &parts ) -> bool
{
    if( parts.empty() ) {
        return false;
    }

    const auto count = static_cast<Uint32>(
        std::min( parts.size(), static_cast<size_t>( MAX_PARTICLES ) ) );
    const auto bytes = count * static_cast<Uint32>( sizeof( particle_gpu_instance ) );

    void *mapped = SDL_MapGPUTransferBuffer( dev_->raw(), particle_xfer_, true );
    if( !mapped ) {
        return false;
    }
    auto *dst = static_cast<particle_gpu_instance *>( mapped );
    for( Uint32 i = 0; i < count; ++i ) {
        const auto &p = parts[i];
        dst[i] = {
            .x = p.x, .y = p.y, .size = p.size, .alpha = p.alpha,
            .r = p.r, .g = p.g, .b = p.b,
            .rotation = p.rotation * 3.14159265f / 180.f,
            .pad0 = 0.f, .pad1 = 0.f, .pad2 = 0.f, .pad3 = 0.f,
        };
    }
    SDL_UnmapGPUTransferBuffer( dev_->raw(), particle_xfer_ );

    // Copy pass: transfer -> storage.
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass( cb );
    SDL_GPUTransferBufferLocation src{};
    src.transfer_buffer = particle_xfer_;
    src.offset = 0;
    SDL_GPUBufferRegion dst_region{};
    dst_region.buffer = particle_storage_;
    dst_region.offset = 0;
    dst_region.size = bytes;
    SDL_UploadToGPUBuffer( cp, &src, &dst_region, true );
    SDL_EndGPUCopyPass( cp );
    return true;
}

// ---- Per-frame record ---------------------------------------------------

auto hud_particle_effect::record(
    SDL_GPUCommandBuffer *cb, SDL_GPUTexture *ui_tex,
    std::uint32_t ui_w, std::uint32_t ui_h, const hud_particle_params &params ) -> void
{
    if( !ready() || !cb || !ui_tex ) {
        return;
    }

    // Spawn new particles
    spawn_accumulator_ += params.spawn_rate * 0.016f;
    while( spawn_accumulator_ >= 1.0f &&
           particles_.size() < static_cast<size_t>( MAX_PARTICLES ) ) {
        particles_.push_back( spawn_particle( params ) );
        spawn_accumulator_ -= 1.0f;
    }

    // Update particles
    update_particles( 0.016f );

    if( particles_.empty() ) {
        return;
    }

    // Upload instances to GPU.
    if( !upload_instances( cb, particles_ ) ) {
        return;
    }

    const auto count = static_cast<Uint32>(
        std::min( particles_.size(), static_cast<size_t>( MAX_PARTICLES ) ) );

    // Begin render pass on UI target — LOAD to preserve existing UI, STORE to keep.
    SDL_GPUColorTargetInfo ct{};
    ct.texture = ui_tex;
    ct.load_op = SDL_GPU_LOADOP_LOAD;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *rp = SDL_BeginGPURenderPass( cb, &ct, 1, nullptr );
    if( !rp ) {
        return;
    }

    // Push frame params uniform (vertex slot 0).
    struct FrameParams {
        float target_w;
        float target_h;
        std::uint32_t instance_base;
        std::uint32_t pad;
    };
    const FrameParams fp {
        .target_w = static_cast<float>( ui_w ),
        .target_h = static_cast<float>( ui_h ),
        .instance_base = 0,
        .pad = 0,
    };
    SDL_BindGPUGraphicsPipeline( rp, particle_pipeline_ );
    SDL_PushGPUVertexUniformData( cb, 0, &fp, sizeof( fp ) );

    const SDL_GPUViewport vp { 0.0f, 0.0f,
                               static_cast<float>( ui_w ),
                               static_cast<float>( ui_h ), 0.0f, 1.0f };
    SDL_SetGPUViewport( rp, &vp );

    // Bind instance storage buffer.
    SDL_BindGPUVertexStorageBuffers( rp, 0, &particle_storage_, 1 );

    // Draw: 6 vertices per instance (triangle list), N instances.
    SDL_DrawGPUPrimitives( rp, 6, count, 0, 0 );
    SDL_EndGPURenderPass( rp );
}

} // namespace lighting