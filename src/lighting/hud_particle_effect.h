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
#include <algorithm> // std::min / std::max in the constexpr envelope below
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
    float alpha = 1.0f;      // current alpha (derived from base_alpha + age)
    float base_alpha = 1.0f; // spawn alpha, never mutated — the fade reads it
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
    snow,     // white, downward + sway
    leaf      // autumn brown/red/orange, tumbling drift
};

// Per-frame particle parameters.
struct hud_particle_params {
    hud_emitter_type type = hud_emitter_type::dust;
    float spawn_rate = 5.0f;       // particles per second
    float intensity = 1.0f;        // 0..1 scale (drives alpha)
    std::uint32_t screen_w = 1920;
    std::uint32_t screen_h = 1080;
    // Dev-panel multipliers (F4 → Effects → HUD particles). 1.0 = authored look.
    // size_scale multiplies the spawn diameter; speed_scale multiplies velocity,
    // and because lifetimes are derived from travel distance / speed, a particle
    // still crosses the same screen distance — it just gets there faster.
    float size_scale = 1.0f;
    float speed_scale = 1.0f;
};

// ── Pure simulation math (free functions so they are unit-testable) ─────────

// Lifetime that carries a particle `distance` px at `speed` px/s, with `slack`
// extra so it dies just past the far edge instead of popping out mid-screen.
// Emitters spawn just OUTSIDE an edge, so a lifetime picked independently of
// speed is what stranded every particle in a band hugging its spawn edge.
constexpr auto hud_particle_travel_lifetime( float distance, float speed,
        float slack = 1.15f ) -> float
{
    return speed > 0.f ? distance * slack / speed : 1.f;
}

// Alpha envelope at `age`: ramps up over `fade_in` seconds, holds, then ramps
// down over the last 30% of `lifetime`.
//
// A PURE FUNCTION OF AGE ON PURPOSE. This was once applied as a per-frame
// multiply into the particle's alpha, which compounds — the factor is < 1 on
// every frame past the fade start, so alpha collapsed geometrically within a
// few frames and the reap threshold then deleted the particle at ~70% of its
// nominal lifetime, far from where it was supposed to travel.
constexpr auto hud_particle_alpha( float base_alpha, float age, float lifetime,
                                   float fade_in ) -> float
{
    if( lifetime <= 0.f || age >= lifetime ) {
        return 0.f;
    }
    const float in = fade_in > 0.f ? std::min( 1.f, age / fade_in ) : 1.f;
    const float fade_start = lifetime * 0.7f;
    const float out = age > fade_start
                      ? std::max( 0.f, 1.f - ( age - fade_start ) / ( lifetime - fade_start ) )
                      : 1.f;
    return base_alpha * in * out;
}

// Arguments for one in-pass particle draw.
struct hud_particle_draw {
    SDL_GPURenderPass *rp = nullptr;
    SDL_GPUCommandBuffer *cb = nullptr;
    /// Instance count returned by prepare(). 0 = nothing to draw.
    std::uint32_t count = 0;
    /// Render target size in PHYSICAL pixels — the viewport this draw sets, and
    /// the space the vertex shader maps particle positions into.
    std::uint32_t target_w = 0;
    std::uint32_t target_h = 0;
    /// Gameplay viewport in the SAME physical pixels, kept clear of particles so
    /// a drifting mote is never mistaken for an item or a creature. Ignored when
    /// mask_play_area is false (no map on screen, or the dev toggle is off).
    float play_x0 = 0.f;
    float play_y0 = 0.f;
    float play_x1 = 0.f;
    float play_y1 = 0.f;
    bool mask_play_area = false;
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
        return dev_ != nullptr && particle_pipeline_ != nullptr
            && particle_xfer_ != nullptr && particle_storage_ != nullptr;
    }

    // Drop every live particle. Switching the effect off has to remove what is
    // already on screen, not merely stop spawning — otherwise the last poolful
    // hangs frozen until each one ages out.
    auto clear() noexcept -> void;

    // Advance the simulation with the real frame delta and upload this frame's
    // instances. MUST be called BEFORE the render pass opens (it records a copy
    // pass, which cannot nest inside one). Returns the instance count to draw,
    // 0 when there is nothing to draw.
    auto prepare( SDL_GPUCommandBuffer *cb, const hud_particle_params &params ) -> std::uint32_t;

    // Issue the draw INSIDE an already-open render pass, so the particles land
    // over everything that pass has drawn — including the RmlUi HUD, which is
    // itself an overlay callback in that same pass. A standalone pass of our own
    // is not an option: D3D12 allows one render pass per swapchain target per
    // command buffer (see sdl_render_frame.cpp), and drawing into the cached
    // ui_composite_target instead (the old behaviour) put the particles UNDER the
    // HUD and smeared trails into a texture that is only re-cleared when the UI
    // goes dirty.
    auto draw_in_pass( const hud_particle_draw &d ) -> void;

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
    // Wall-clock of the previous record(), for the real frame delta. 0 = first
    // frame. The simulation used to advance a hardcoded 1/60 s per record()
    // call, which is not a time step at all: refresh_display only runs on a
    // redraw, so particles aged (and therefore travelled) at whatever rate the
    // UI happened to repaint.
    std::uint64_t last_ticks_ms_ = 0;
};

} // namespace lighting