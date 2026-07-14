#pragma once

// GPU shader-based sound wave visualization pass.
//
// Renders expanding circular wavefronts for sound pulses using one instanced
// quad per pulse. The vertex shader places a large quad centered at the
// source; the fragment shader computes per-pixel distance from source and
// draws a bright ring at the wavefront edge with a colored wake behind it.
// Optional SDF modulation adds a diffraction glow near walls.
//
// Follows the rain_effect pattern: instance storage buffer + procedural quad
// vertex shader + custom fragment shader, alpha-blended onto the world target.

#include <SDL3/SDL_gpu.h>
#include <cstdint>
#include <vector>

namespace lighting {

class gpu_device;

// One sound wave pulse instance (wire-stable with vertex shader).
// 16 bytes — one per active sound pulse (source position + current state).
struct sound_wave_instance {
    float source_x;  // source center X in screen pixels
    float source_y;  // source center Y in screen pixels
    float radius_px; // current wavefront radius in pixels
    float life;      // 0..1 fade curve
};
static_assert(sizeof(sound_wave_instance) == 16,
              "sound_wave_instance must be 16 bytes (wire-stable with vert shader)");
// Fragment cbuffer for SDF diffraction modulation (b0/space3).
// Pushed with SDL_PushGPUFragmentUniformData each record().
struct alignas(16) snd_frag_params {
    float camera_off_x;
    float camera_off_y;
    float op_x;
    float op_y;
    float tile_px_inv;
    float pixel_ratio; // physical / logical pixel ratio (e.g. 2.0 on Retina)
    std::uint32_t sdf_map_w;
    std::uint32_t sdf_map_h;
};
static_assert(sizeof(snd_frag_params) == 32,
              "snd_frag_params must be 32 bytes (wire-stable with snd_frag.frag.hlsl)");

// Per-record parameters for sound wave rendering.
struct sound_wave_record_options {
    SDL_GPUCommandBuffer* cb = nullptr;
    SDL_GPUTexture* target = nullptr;
    std::uint32_t proj_w = 0;
    std::uint32_t proj_h = 0;
    const std::vector<sound_wave_instance>* instances = nullptr;
    // SDF modulation. Nullptr = SDF disabled (falls back to plain discs).
    SDL_GPUBuffer* sdf_buffer = nullptr;
    snd_frag_params snd_frag_params{};
};

class sound_wave_pass {
public:
    sound_wave_pass() = default;
    sound_wave_pass(const sound_wave_pass&) = delete;
    auto operator=(const sound_wave_pass&) -> sound_wave_pass& = delete; // *NOPAD*
    ~sound_wave_pass();

    // Build pipelines + instance buffers. `target_format` is the world_target
    // format the pass renders into (swapchain format for direct rendering).
    auto init(gpu_device& dev, SDL_GPUTextureFormat target_format) -> bool;

    auto shutdown() noexcept -> void;

    auto ready() const noexcept -> bool
    {
        return dev_ != nullptr && pipeline_ != nullptr && storage_ != nullptr && xfer_ != nullptr;
    }

    // Record all sound wave instances onto `target`.
    auto record(const sound_wave_record_options& opts) -> void;

private:
    // Upload instance array to GPU storage buffer via transfer buffer.
    auto upload_instances(
        SDL_GPUCommandBuffer* cb, const std::vector<sound_wave_instance>& insts) -> bool;

    gpu_device* dev_ = nullptr;
    SDL_GPUTextureFormat target_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;

    SDL_GPUShader* vert_ = nullptr;
    SDL_GPUShader* frag_ = nullptr;
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;

    // Persistent instance buffers (transfer + storage), reused each frame.
    SDL_GPUTransferBuffer* xfer_ = nullptr;
    SDL_GPUBuffer* storage_ = nullptr;

    // Max instances: enough for multiple pulses × ~256 tiles each.
    static constexpr int MAX_INSTANCES = 2048;
};

} // namespace lighting