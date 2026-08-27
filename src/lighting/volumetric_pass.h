#pragma once

// Volumetric "lit fog" pass — Step-6 / C2 (first slice: full-res sun shafts).
//
// One fullscreen-triangle sub-pass that computes, per screen pixel, the sunlit
// haze reaching the air over that pixel and composites it ADDITIVELY into the
// HDR world_target — before the bloom + tonemap passes. It is a SEPARATE pass
// (not inline in sprite.frag) so the additive emissive term is evaluated once
// per pixel, not once per stacked sprite layer.
//
// Sampler-less: reads SdfBuf (t0) + SkyVisBuf (t1) as fragment storage buffers
// (same buffers sprite.frag / rc use), and pushes VolParams (b0, space3). It
// owns NO textures — it draws into the world_target handed to record(). World
// position is reconstructed from SV_Position with the SAME camera_off /
// tile_pixel_size the sprite vertex shader uses (single source of truth).
//
// Shader: data/shaders/lighting/src/vol.frag.hlsl (+ tonemap.vert fullscreen tri).

#include <SDL3/SDL_gpu.h>
#include <cstdint>

namespace lighting {

class gpu_device;

// Wire-stable with the VolParams cbuffer in vol.frag.hlsl (21 floats, 84 B).
struct vol_params {
    float tile_pixel_size = 32.0f;
    float camera_off_x = 0.0f;
    float camera_off_y = 0.0f;
    float current_z = 0.0f;
    float sun_dir_x = 0.0f;
    float sun_dir_y = 0.0f;
    float sun_intensity = 0.0f;
    float sun_r = 1.0f;
    float sun_g = 1.0f;
    float sun_b = 1.0f;
    float vol_density = 0.3f;         // uniform haze amount (F4)
    float vol_intensity = 1.0f;       // overall multiplier (F4)
    float vol_reach = 8.0f;           // sun-shadow march reach (tiles)
    float shadow_k = 8.0f;            // reuse sprite soft-shadow hardness
    std::uint32_t shadow_steps = 16u; // reuse sprite march cap
    std::uint32_t sdf_map_w = 0u;
    std::uint32_t sdf_map_h = 0u;
    float proj_w = 0.0f; // projection (game-view) px; world_pos reconstruct
    float proj_h = 0.0f;
    float vol_shadow = 0.0f; // 0 = uniform haze (no cast shadow); >0 = directional lanes
    float vol_indoor = 0.0f; // indoor shaft strength: 0 = sky-gated only (old behaviour);
                             // >0 = the sun march (windows are transparent in the SDF)
                             // lights air under roofs — godrays through window openings
};

class volumetric_pass {
public:
    volumetric_pass() = default;
    volumetric_pass(const volumetric_pass&) = delete;
    volumetric_pass& operator=(const volumetric_pass&) = delete;
    ~volumetric_pass();

    // Build the fullscreen pipeline writing into hdr_format (additive blend).
    bool init(gpu_device& dev, SDL_GPUTextureFormat hdr_format);

    void shutdown() noexcept;

    bool ready() const noexcept { return pipeline_ != nullptr; }

    // Composite sun-shaft haze additively into hdr_tex (the world_target),
    // reading sdf_buf (t0) + skyvis_buf (t1). No-op if not ready or any
    // argument invalid. params carries the per-frame camera / sun / knobs.
    void record(
        SDL_GPUCommandBuffer* cb, SDL_GPUTexture* hdr_tex, std::uint32_t full_w,
        std::uint32_t full_h, SDL_GPUBuffer* sdf_buf, SDL_GPUBuffer* skyvis_buf,
        const vol_params& params);

private:
    gpu_device* dev_ = nullptr;
    SDL_GPUTextureFormat hdr_format_ = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    SDL_GPUShader* vert_ = nullptr;
    SDL_GPUShader* frag_ = nullptr;
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr; // → hdr_format, additive
};

} // namespace lighting
