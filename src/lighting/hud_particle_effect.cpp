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

// Speeds are FRACTIONS OF SCREEN HEIGHT PER SECOND and lifetimes are derived
// from the distance the particle has to cover, not picked independently.
//
// The original constants were absolute px/s (15-45) with independent 2.5-12 s
// lifetimes — roughly 40-400 px of travel. Every emitter but `ember` spawns just
// OUTSIDE an edge (x = -20, y = -20, y = h + 20), so on a 1920x1080 screen a
// particle died in a ~150 px band hugging the edge it came from and the other
// 85% of the screen never saw one. (Verified on this machine: forcing size x15
// produced a band of blobs across the top ~130 px and nothing below it.)
// Sizes scale with resolution for the same reason — a 3 px mote leaves about one
// pixel of visible core after the shader's radial falloff.
namespace
{

constexpr float REF_HEIGHT = 1080.f; ///< Resolution the px sizes below are authored at.

/// Local alias for the header's pure helper (unit-tested in
/// tests/hud_particle_test.cpp) — the math MUST have exactly one definition.
constexpr auto travel_lifetime( float distance, float speed ) -> float
{
    return hud_particle_travel_lifetime( distance, speed );
}

} // namespace

auto hud_particle_effect::spawn_particle( const hud_particle_params &params ) -> hud_particle
{
    hud_particle p{};
    const float w = static_cast<float>( params.screen_w );
    const float h = static_cast<float>( params.screen_h );
    // Resolution scale folded with the dev-panel size knob.
    const float px = h / REF_HEIGHT * std::max( 0.05f, params.size_scale );
    // Velocity knob. Speeds below are all `h * fraction * vel`, and the derived
    // lifetimes divide by that same speed, so this changes how FAST a particle
    // crosses the screen, never whether it makes it across.
    const float vel = std::max( 0.05f, params.speed_scale );

    switch( params.type ) {
        case hud_emitter_type::ember: {
            // Rises from the lower half and burns out — the one emitter meant to
            // stay near where it spawned, so its lifetime stays fixed.
            p.x = static_cast<float>( rng_float( 0.0, w ) );
            p.y = h * static_cast<float>( rng_float( 0.5, 1.0 ) );
            p.vx = vel * h * static_cast<float>( rng_float( -0.012, 0.012 ) );
            p.vy = -vel * h * static_cast<float>( rng_float( 0.06, 0.13 ) );
            p.size = px * static_cast<float>( rng_float( 5.0, 10.0 ) );
            p.lifetime = static_cast<float>( rng_float( 3.0, 6.0 ) );
            p.rot_speed = static_cast<float>( rng_float( -60.0, 60.0 ) );
            p.r = static_cast<float>( rng_float( 0.9, 1.0 ) );
            p.g = static_cast<float>( rng_float( 0.3, 0.6 ) );
            p.b = static_cast<float>( rng_float( 0.0, 0.1 ) );
            break;
        }
        case hud_emitter_type::dust: {
            // Drifts in from the left and must reach the right edge.
            const float speed = vel * h * static_cast<float>( rng_float( 0.05, 0.14 ) );
            p.x = -20.f;
            p.y = static_cast<float>( rng_float( 0.0, h ) );
            p.vx = speed;
            p.vy = vel * h * static_cast<float>( rng_float( -0.006, 0.006 ) );
            p.size = px * static_cast<float>( rng_float( 4.0, 9.0 ) );
            p.lifetime = travel_lifetime( w + 40.f, speed );
            p.rot_speed = static_cast<float>( rng_float( -15.0, 15.0 ) );
            p.r = static_cast<float>( rng_float( 0.5, 0.7 ) );
            p.g = static_cast<float>( rng_float( 0.4, 0.6 ) );
            p.b = static_cast<float>( rng_float( 0.3, 0.5 ) );
            break;
        }
        case hud_emitter_type::pollen: {
            // Floats up from below the bottom edge to past the top one.
            const float speed = vel * h * static_cast<float>( rng_float( 0.035, 0.08 ) );
            p.x = static_cast<float>( rng_float( 0.0, w ) );
            p.y = h + 20.f;
            p.vx = vel * h * static_cast<float>( rng_float( -0.012, 0.012 ) );
            p.vy = -speed;
            p.size = px * static_cast<float>( rng_float( 4.0, 8.0 ) );
            p.lifetime = travel_lifetime( h + 40.f, speed );
            p.rot_speed = static_cast<float>( rng_float( -30.0, 30.0 ) );
            p.r = static_cast<float>( rng_float( 0.7, 0.9 ) );
            p.g = static_cast<float>( rng_float( 0.8, 1.0 ) );
            p.b = static_cast<float>( rng_float( 0.2, 0.4 ) );
            break;
        }
        case hud_emitter_type::snow: {
            const float speed = vel * h * static_cast<float>( rng_float( 0.09, 0.20 ) );
            p.x = static_cast<float>( rng_float( 0.0, w ) );
            p.y = -20.f;
            p.vx = vel * h * static_cast<float>( rng_float( -0.02, 0.02 ) );
            p.vy = speed;
            p.size = px * static_cast<float>( rng_float( 5.0, 12.0 ) );
            p.lifetime = travel_lifetime( h + 40.f, speed );
            p.rot_speed = static_cast<float>( rng_float( -50.0, 50.0 ) );
            p.r = 1.0f;
            p.g = 1.0f;
            p.b = 1.0f;
            break;
        }
        case hud_emitter_type::leaf: {
            // Tumbling leaf — spawns from the top or the left, drifts down + right.
            const float fall = vel * h * static_cast<float>( rng_float( 0.055, 0.13 ) );
            if( rng_float( 0.0, 1.0 ) < 0.5 ) {
                p.x = static_cast<float>( rng_float( 0.0, w ) );
                p.y = -20.f;
            } else {
                p.x = static_cast<float>( rng_float( -20.0, 0.0 ) );
                p.y = static_cast<float>( rng_float( 0.0, h * 0.5 ) );
            }
            p.vx = vel * h * static_cast<float>( rng_float( 0.03, 0.08 ) );
            p.vy = fall;
            p.size = px * static_cast<float>( rng_float( 7.0, 15.0 ) );
            p.lifetime = travel_lifetime( h + 40.f, fall );
            p.rot_speed = static_cast<float>( rng_float( -120.0, 120.0 ) );
            // Autumn palette: browns, reds, oranges, yellows
            switch( rng( 0, 3 ) ) {
                case 0: // brown
                    p.r = static_cast<float>( rng_float( 0.45, 0.6 ) );
                    p.g = static_cast<float>( rng_float( 0.25, 0.35 ) );
                    p.b = static_cast<float>( rng_float( 0.1, 0.15 ) );
                    break;
                case 1: // red
                    p.r = static_cast<float>( rng_float( 0.7, 0.9 ) );
                    p.g = static_cast<float>( rng_float( 0.15, 0.3 ) );
                    p.b = static_cast<float>( rng_float( 0.05, 0.1 ) );
                    break;
                case 2: // orange
                    p.r = static_cast<float>( rng_float( 0.85, 1.0 ) );
                    p.g = static_cast<float>( rng_float( 0.45, 0.6 ) );
                    p.b = static_cast<float>( rng_float( 0.05, 0.15 ) );
                    break;
                default: // yellow
                    p.r = static_cast<float>( rng_float( 0.9, 1.0 ) );
                    p.g = static_cast<float>( rng_float( 0.75, 0.9 ) );
                    p.b = static_cast<float>( rng_float( 0.1, 0.25 ) );
                    break;
            }
            break;
        }
    }

    p.base_alpha = params.intensity;
    p.alpha = 0.f; // faded in by update_particles on the first step
    return p;
}

// ---- Particle management ------------------------------------------------

auto hud_particle_effect::clear() noexcept -> void
{
    particles_.clear();
    spawn_accumulator_ = 0.f;
    // Forget the clock too: the next prepare() would otherwise charge the whole
    // paused interval to its first step and shove the new particles off-screen.
    last_ticks_ms_ = 0;
}

auto hud_particle_effect::update_particles( float dt ) -> void
{
    // Fade in over the first 0.4 s (a particle spawns off-screen, so this is
    // mostly insurance for `ember`, which spawns in view) and out over the last
    // 30% of its life.
    constexpr float FADE_IN = 0.4f;

    for( auto &p : particles_ ) {
        p.age += dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.rotation += p.rot_speed * dt;

        // Envelope is a pure function of age (hud_particle_alpha, unit-tested):
        // it used to be a per-frame MULTIPLY into p.alpha, which compounds and
        // killed particles at ~70% of their nominal lifetime.
        p.alpha = hud_particle_alpha( p.base_alpha, p.age, p.lifetime, FADE_IN );
    }

    // Remove expired particles. Only `age` decides — alpha is a pure function of
    // it, so a separate alpha threshold could only ever reap a live particle early.
    std::erase_if( particles_, []( const hud_particle &p ) { return p.age >= p.lifetime; } );
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

auto hud_particle_effect::prepare( SDL_GPUCommandBuffer *cb,
                                   const hud_particle_params &params ) -> std::uint32_t
{
    if( !ready() || !cb ) {
        return 0;
    }

    // Real frame delta. record() used to advance a fixed 1/60 s per call, so the
    // simulation ran at "one tick per repaint" — and refresh_display only repaints
    // on a redraw, which in a turn-based game is neither 60 Hz nor steady. The
    // clamp keeps a load stall (or the first frame after one) from teleporting
    // every particle off-screen at once.
    const std::uint64_t now = SDL_GetTicks();
    const float dt = last_ticks_ms_ == 0
                     ? 1.f / 60.f
                     : std::min( 0.1f, static_cast<float>( now - last_ticks_ms_ ) / 1000.f );
    last_ticks_ms_ = now;

    spawn_accumulator_ += params.spawn_rate * dt;
    while( spawn_accumulator_ >= 1.0f &&
           particles_.size() < static_cast<size_t>( MAX_PARTICLES ) ) {
        particles_.push_back( spawn_particle( params ) );
        spawn_accumulator_ -= 1.0f;
    }
    // Nothing can spawn while the pool is full; without this the accumulator
    // would grow without bound and dump MAX_PARTICLES at once when space frees up.
    spawn_accumulator_ = std::min( spawn_accumulator_, 1.0f );

    update_particles( dt );

    if( particles_.empty() || !upload_instances( cb, particles_ ) ) {
        return 0;
    }
    return static_cast<std::uint32_t>(
               std::min( particles_.size(), static_cast<size_t>( MAX_PARTICLES ) ) );
}

auto hud_particle_effect::draw_in_pass( const hud_particle_draw &d ) -> void
{
    if( !ready() || !d.rp || !d.cb || d.count == 0 ) {
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
        .target_w = static_cast<float>( d.target_w ),
        .target_h = static_cast<float>( d.target_h ),
        .instance_base = 0,
        .pad = 0,
    };

    // Gameplay cutout (fragment slot 0). Matches MaskParams in
    // hud_particle.frag.hlsl: the rect is discarded, leaving particles on the
    // HUD chrome only.
    struct MaskParams {
        float x0, y0, x1, y1;
        float enable;
        float pad0, pad1, pad2;
    };
    const MaskParams mp {
        .x0 = d.play_x0,
        .y0 = d.play_y0,
        .x1 = d.play_x1,
        .y1 = d.play_y1,
        // A degenerate rect would discard nothing while still costing the test,
        // so an empty play area disables the mask outright.
        .enable = ( d.mask_play_area && d.play_x1 > d.play_x0 && d.play_y1 > d.play_y0 )
        ? 1.0f : 0.0f,
        .pad0 = 0.f, .pad1 = 0.f, .pad2 = 0.f,
    };

    SDL_BindGPUGraphicsPipeline( d.rp, particle_pipeline_ );
    SDL_PushGPUVertexUniformData( d.cb, 0, &fp, sizeof( fp ) );
    SDL_PushGPUFragmentUniformData( d.cb, 0, &mp, sizeof( mp ) );

    const SDL_GPUViewport vp { 0.0f, 0.0f,
                               static_cast<float>( d.target_w ),
                               static_cast<float>( d.target_h ), 0.0f, 1.0f };
    SDL_SetGPUViewport( d.rp, &vp );

    // Bind instance storage buffer.
    SDL_BindGPUVertexStorageBuffers( d.rp, 0, &particle_storage_, 1 );

    // Draw: 6 vertices per instance (triangle list), N instances.
    SDL_DrawGPUPrimitives( d.rp, 6, d.count, 0, 0 );
}

} // namespace lighting