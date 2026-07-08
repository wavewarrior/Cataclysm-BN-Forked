#pragma once

// High-fidelity rain effect — GPU particle system for falling droplets and
// world-positioned impact-splash rings.
//
// Driven from refresh_display() (sdl_render_frame.cpp) inside the world pass,
// after the terrain/lighting draw and before the tonemap resolve. Droplets are
// a screen-space overlay; splash rings are WORLD-LOCKED (spawned at map tiles,
// sky-gated, projected to screen each frame with the same camera_off/tile_px
// the sprite shader uses) so they stay glued to the ground as the view scrolls.
//
// NOTE: an earlier revision used a screen-space ping-pong "splat map"
// accumulator for wet spots. It was removed — a screen-space accumulator rains
// indoors (no sky gate) and smears across the terrain when the world scrolls.
// See RAIN_EFFECT_LEARNINGS.md.

#include <SDL3/SDL_gpu.h>
#include <cstdint>
#include <vector>

namespace lighting {

class gpu_device;

// One falling rain droplet (CPU-side). Targeted at a WORLD tile: it falls a
// short distance straight onto that tile, then dies and spawns a splash ring
// there. Couples the two layers — a drop you see fall is the drop that splashes.
struct rain_droplet {
    float world_x = 0.f;  // landing tile X (+ sub-tile frac)
    float world_y = 0.f;  // landing tile Y
    float fall = 0.f;     // TILES above the landing tile, decreasing to 0
    float fall0 = 1.f;    // initial fall height in tiles (wind-lean interp)
    float opacity = 1.0f; // alpha for rendering
};

// One impact-splash ring (CPU-side, short-lived, WORLD-positioned).
struct rain_splash {
    float world_x = 0.f;    // map-tile X (local tile coords, +0.5 = tile centre)
    float world_y = 0.f;    // map-tile Y
    float intensity = 1.0f; // base brightness/alpha
    uint32_t age = 0u;      // frames alive
    uint32_t max_age = 12u; // lifetime in frames before removal
};

// Per-frame rain parameters driven by weather + camera state.
struct rain_params {
    bool active = false;    // true when rain should be drawn
    float intensity = 0.f;  // 0..1 scale (drives spawn rate)
    float wind_angle = 0.f; // degrees from north (0=north), clockwise positive

    // World→screen projection (mirrors sprite.vert / assemble_light_inputs):
    // screen_px = (world_tile + camera_off) * tile_pixel_size.
    float camera_off_x = 0.f;
    float camera_off_y = 0.f;
    float tile_pixel_size = 32.f;
};

class rain_effect {
public:
    rain_effect() = default;
    rain_effect(const rain_effect&) = delete;
    rain_effect& operator=(const rain_effect&) = delete;
    ~rain_effect();

    // Build pipelines + instance buffers. hdr_format is the world_target
    // format (RGBA16F for HDR). screen_w/screen_h size the droplet spawn
    // area (the falling-streak overlay is screen-space).
    bool init(
        gpu_device& dev, SDL_GPUTextureFormat hdr_format, std::uint32_t screen_w,
        std::uint32_t screen_h);

    void shutdown() noexcept;

    bool ready() const noexcept {
        return dev_ != nullptr && droplet_pipeline_ != nullptr && splash_pipeline_ != nullptr
            && droplet_xfer_ != nullptr && droplet_storage_ != nullptr && splash_xfer_ != nullptr
            && splash_storage_ != nullptr;
    }

    // Spawn a falling drop targeted at world tile (wx, wy). Called from the
    // frame code (which owns map + sky-exposure data) so spawns are sky-gated
    // at the source; rain_effect itself stays GPU-only and game-agnostic.
    // The drop falls onto the tile, then dies + spawns a splash ring there,
    // so the splash inherits the drop's sky-gating for free.
    void add_drop(float wx, float wy, float opacity);

    // Update internal particle state and record all rain draws.
    // `cb` is the current command buffer, `world_tex` is the HDR world
    // target that rain is drawn onto, `params` carries weather + camera data.
    void record(
        SDL_GPUCommandBuffer* cb, SDL_GPUTexture* world_tex, std::uint32_t world_w,
        std::uint32_t world_h, const rain_params& params);

private:
    // Spawn an impact-splash ring at world tile (wx, wy). Internal — driven
    // by drop landings in record(), inheriting the drop's sky-gating.
    void add_splash(float wx, float wy, float intensity);

    // Age splash rings; remove expired ones.
    void update_splashes();

    // Upload an instance array to (transfer→storage) and return the count
    // actually uploaded. Helper shared by droplet + splash draws.
    template <typename Inst>
    bool upload_instances(
        SDL_GPUCommandBuffer* cb, SDL_GPUTransferBuffer* xfer, SDL_GPUBuffer* storage,
        const std::vector<Inst>& insts);

    // GPU state --------------------------------------------------------
    gpu_device* dev_ = nullptr;
    SDL_GPUTextureFormat hdr_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;

    // Procedural quad vertex shader, shared by droplets + splash rings.
    SDL_GPUShader* quad_vert_ = nullptr;

    // Droplet rendering pipeline (alpha-blended streaks onto world_target).
    SDL_GPUShader* droplet_frag_ = nullptr;
    SDL_GPUGraphicsPipeline* droplet_pipeline_ = nullptr;

    // Splash-ring pipeline (same vert, ring fragment; same premult blend).
    SDL_GPUShader* splash_frag_ = nullptr;
    SDL_GPUGraphicsPipeline* splash_pipeline_ = nullptr;

    // Persistent instance buffers (transfer + storage), reused each frame.
    SDL_GPUTransferBuffer* droplet_xfer_ = nullptr;
    SDL_GPUBuffer* droplet_storage_ = nullptr;
    SDL_GPUTransferBuffer* splash_xfer_ = nullptr;
    SDL_GPUBuffer* splash_storage_ = nullptr;

    // Particle pools ---------------------------------------------------
    static constexpr int MAX_DROPLETS = 4096;
    static constexpr int MAX_SPLASHES = 1024;
    std::vector<rain_droplet> droplets_;
    std::vector<rain_splash> splashes_;
};

} // namespace lighting
