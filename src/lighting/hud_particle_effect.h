#pragma once

// Atmospheric HUD particle effects — GPU particle system for ambient particles
// drifting across the HUD: embers near fire, dust in caves, pollen outdoors,
// snowflakes in cold.
//
// Driven from composite_swapchain_pass_b (sdl_render_frame.cpp) after RmlUi
// renders but before the final blit. Particles are screen-space (not world-locked)
// so they drift independently of camera movement.
//
// Modeled on rain_effect: init pipelines, instanced rendering, procedural quad
// vertex shader. Key difference: renders to the UI composite target (screen-space),
// not the world target.

#include <SDL3/SDL_gpu.h>
#include <cstdint>
#include <vector>

namespace lighting {

class gpu_device;

// One HUD particle (CPU-side, screen-space).
struct hud_particle {
    float x = 0.f;           // screen X (logical px)
    float y = 0.f;           // screen Y (logical px)
    float vx = 0.f;          // horizontal velocity px/sec
    float vy = 0.f;          // vertical velocity px/sec
    float size = 2.f;        // diameter in px
    float alpha = 1.0f;      // base alpha
    float age = 0.f;         // seconds alive
    float lifetime = 3.f;    // total lifetime in seconds
    float rotation = 0.f;    // rotation angle (degrees)
    float rot_speed = 0.f;   // rotation speed deg/sec
    float r = 1.0f;          // color components
    float g = 1.0f;
    float b = 1.0f;
};

// Emitter type configuration.
enum class hud_emitter_type {
    ember,    // orange-red, upward drift, sway
    dust,     // gray-brown, slow horizontal drift
    pollen,   // yellow-green, lazy sine-wave float
    snow      // white, downward + sway
};

// Per-frame particle parameters.
struct hud_particle_params {
    hud_emitter_type type = hud_emitter_type::dust;
    float spawn_rate = 5.0f;       // particles per second
    float intensity = 1.0f;        // 0..1 scale (drives alpha)
    std::uint32_t screen_w = 1920;
    std::uint32_t screen_h = 1080;
};

class hud_particle_effect {
public:
    hud_particle_effect() = default;
    hud_particle_effect( const hud_particle_effect & ) = delete;
    hud_particle_effect &operator=( const hud_particle_effect & ) = delete;
    ~hud_particle_effect();

    // Build pipelines + instance buffers.
    auto init( gpu_device &dev, SDL_GPUTextureFormat ui_format,
               std::uint32_t screen_w, std::uint32_t screen_h ) -> bool;

    auto shutdown() noexcept -> void;

    // True when initialized (GPU pipeline creation is deferred; draw is stubbed).
    auto ready() const noexcept -> bool {
        return dev_ != nullptr;
    }

    // Update particles and record draws.
    auto record( SDL_GPUCommandBuffer *cb, SDL_GPUTexture *ui_tex,
                 std::uint32_t ui_w, std::uint32_t ui_h,
                 const hud_particle_params &params ) -> void;

private:
    // Spawn a new particle based on emitter type.
    auto spawn_particle( const hud_particle_params &params ) -> hud_particle;

    // Age particles; remove expired ones.
    auto update_particles( float dt ) -> void;

    // Upload instances to GPU.
    auto upload_instances( SDL_GPUCommandBuffer *cb,
                           const std::vector<hud_particle> &particles ) -> bool;

    // GPU state
    gpu_device *dev_ = nullptr;
    SDL_GPUTextureFormat ui_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;

    SDL_GPUShader *particle_vert_ = nullptr;
    SDL_GPUShader *particle_frag_ = nullptr;
    SDL_GPUGraphicsPipeline *particle_pipeline_ = nullptr;

    SDL_GPUTransferBuffer *particle_xfer_ = nullptr;
    SDL_GPUBuffer *particle_storage_ = nullptr;

    // Particle pool
    static constexpr int MAX_PARTICLES = 256;
    std::vector<hud_particle> particles_;
    float spawn_accumulator_ = 0.f;
};

} // namespace lighting