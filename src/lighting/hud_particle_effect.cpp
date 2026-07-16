#include "hud_particle_effect.h"

#include "debug.h"
#include "lighting/gpu_device.h"
#include "rng.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#define dbg(x) DebugLogFL((x), DC::SDL)

namespace lighting {

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

    particles_.reserve( MAX_PARTICLES );
    return true;
}

// ---- Shutdown -----------------------------------------------------------

auto hud_particle_effect::shutdown() noexcept -> void
{
    dev_ = nullptr;
    particles_.clear();
    spawn_accumulator_ = 0.f;
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
    SDL_GPUCommandBuffer *, const std::vector<hud_particle> & ) -> bool
{
    // GPU upload deferred — sprite pipeline integration is Phase 8 stretch goal.
    // Particle lifecycle (spawn/update/cull) is functional; draw is stubbed.
    return false;
}

// ---- Per-frame record ---------------------------------------------------

auto hud_particle_effect::record(
    SDL_GPUCommandBuffer *, SDL_GPUTexture *,
    std::uint32_t ui_w, std::uint32_t ui_h, const hud_particle_params &params ) -> void
{
    if( !ready() ) {
        return;
    }

    ( void )ui_w;
    ( void )ui_h;

    // Spawn new particles
    spawn_accumulator_ += params.spawn_rate * 0.016f;
    while( spawn_accumulator_ >= 1.0f && particles_.size() < MAX_PARTICLES ) {
        particles_.push_back( spawn_particle( params ) );
        spawn_accumulator_ -= 1.0f;
    }

    // Update particles
    update_particles( 0.016f );

    // Draw call deferred — sprite pipeline integration is Phase 8 stretch goal.
    // The particle system lifecycle is functional; visual rendering integrates
    // with sprite_batcher in a follow-up.
}

} // namespace lighting