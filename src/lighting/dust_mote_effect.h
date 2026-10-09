#pragma once

// Dust motes drifting inside window light shafts (Step 6c, atmospheric-
// lighting-coherence plan).
//
// CPU world-space particle pool (mirrors rain_effect's droplet pool + the
// real-dt spawn accumulator hud_particle_effect.cpp:424-455 uses), spawned
// along the SAME per-window shafts godray_shaft_pass draws this frame — a
// mote's origin is a point on the world-space beam segment, so dust only ever
// appears where light actually falls. Motion: slow Brownian drift plus a
// faint bias "rising in the light" (opposite the sun's travel direction),
// dt-driven; each mote fades in/out over its lifetime. A mote's colour and
// peak brightness are inherited from its origin shaft's tint/strength, so it
// glows the light's own colour.
//
// Spawn happens in WORLD space (not screen space) so a mote lingers correctly
// when the beam itself moves (camera pan, player walking) — the correct
// choice for a moving light source, matching rain_effect's world-locked
// splash rings rather than its screen-space droplet overlay.
//
// GPU-instanced procedural soft-dot quads, one LOADOP_LOAD pass onto
// world_target, premultiplied blend (mirrors rain_effect's droplet blend).
// FoW-safe by construction: motes only ever spawn along shafts that
// godray_shaft_pass already gated on VIS_CLEAR this frame (the
// emitter-glow-pass lesson) — a mote can only appear where the player
// already sees the window.

#include <SDL3/SDL_gpu.h>
#include <cstdint>
#include <vector>

namespace lighting {

class gpu_device;

// One dust mote (CPU-side).
struct dust_mote {
    float world_x = 0.f;
    float world_y = 0.f;
    float vx = 0.f; // world tiles/second
    float vy = 0.f;
    float age = 0.f;     // seconds alive
    float max_age = 4.f; // seconds before despawn
    float brightness = 1.f;
    float r = 1.f, g = 1.f, b = 1.f; // inherited from the origin shaft's tint
};

// GPU instance layout (matches dust_mote.vert.hlsl's DustMoteInstance).
// Scalar-only floats — see godray_shaft_instance's comment for why: HLSL
// pads a vector to the next 16-byte boundary if it would otherwise straddle
// one, silently desyncing every field after it from this tightly-packed
// layout. Public (not local to dust_mote_effect.cpp) so a C++<->HLSL wire
// test can pin it, mirroring tests/sprite_instance_wire_test.cpp.
struct dust_mote_instance {
    float cx;        // centre X, screen pixels
    float cy;        // centre Y, screen pixels
    float radius_px; // dot radius, screen pixels
    float r;         // premultiplied colour (already * alpha on the CPU)
    float g;
    float b;
    float alpha;
    float pad0;
};
static_assert(
    sizeof(dust_mote_instance) == 32,
    "dust_mote_instance must be 32 bytes (wire-stable with vert shader)");

// A world-space beam segment to spawn motes along. Built once per frame in
// sdl_render_frame.cpp's window-shaft loop (the SAME loop that builds
// godray_shaft_instance for godray_shaft_pass, before world->screen
// projection), so the two passes agree on exactly which windows are lit.
struct dust_mote_shaft_source {
    float world_x = 0.f; // window tile, world-space X (tile units)
    float world_y = 0.f; // window tile, world-space Y
    float dir_x = 0.f;   // unit beam direction, world-space (light travel dir)
    float dir_y = 0.f;
    float length_tiles = 0.f; // beam reach, world tiles
    float r = 1.f, g = 1.f, b = 1.f;
    float strength = 1.f;
};

// Per-record parameters.
struct dust_mote_params {
    // World->screen projection (mirrors sprite.vert / rain_params).
    float camera_off_x = 0.f;
    float camera_off_y = 0.f;
    float tile_pixel_size = 32.f;
    float proj_w = 0.f;
    float proj_h = 0.f;
    float dt = 0.f;        // real seconds since last frame (spawn/age/drift)
    float density = 0.5f;  // F4 knob: spawn-rate multiplier
    float size = 0.5f;     // F4 knob: mote radius multiplier
    float drift = 0.1f;    // F4 knob: Brownian + rising-bias drift strength
    float sun_dir_x = 0.f; // sun travel direction; motes rise along -sun_dir
    float sun_dir_y = 0.f;
};

class dust_mote_effect {
public:
    dust_mote_effect() = default;
    dust_mote_effect(const dust_mote_effect&) = delete;
    auto operator=(const dust_mote_effect&) -> dust_mote_effect& = delete; // *NOPAD*
    ~dust_mote_effect();

    // Build pipeline + instance buffer. hdr_format is the world_target format.
    auto init(gpu_device& dev, SDL_GPUTextureFormat hdr_format) -> bool;

    auto shutdown() noexcept -> void;

    auto ready() const noexcept -> bool {
        return dev_ != nullptr && pipeline_ != nullptr && xfer_ != nullptr && storage_ != nullptr;
    }

    // Age + drift the existing pool, spawn new motes along `active_shafts`,
    // then record the GPU draw onto `world_tex`. No-op if not ready; if
    // `active_shafts` is null/empty no NEW motes spawn but the existing pool
    // still ages/draws (so it fades out gracefully rather than popping when a
    // shaft momentarily has zero windows).
    auto record(
        SDL_GPUCommandBuffer* cb, SDL_GPUTexture* world_tex, std::uint32_t world_w,
        std::uint32_t world_h, const dust_mote_params& params,
        const std::vector<dust_mote_shaft_source>* active_shafts) -> void;

private:
    auto update_and_spawn(
        const dust_mote_params& params, const std::vector<dust_mote_shaft_source>* active_shafts)
        -> void;
    auto upload_instances(SDL_GPUCommandBuffer* cb, const dust_mote_params& params) -> bool;

    gpu_device* dev_ = nullptr;
    SDL_GPUTextureFormat hdr_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUShader* vert_ = nullptr;
    SDL_GPUShader* frag_ = nullptr;
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
    SDL_GPUTransferBuffer* xfer_ = nullptr;
    SDL_GPUBuffer* storage_ = nullptr;
    std::uint32_t uploaded_count_ = 0;

    static constexpr int MAX_MOTES = 2048;
    std::vector<dust_mote> motes_;
    // Deterministic-enough PRNG state (xorshift32); avoids pulling <random>'s
    // heavier engines for a purely cosmetic spawn scatter.
    std::uint32_t rng_state_ = 0x9E3779B9u;
};

} // namespace lighting
