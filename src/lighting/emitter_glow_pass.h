#pragma once

// GPU shader-based decorative light glow pass ("smoke and mirrors").
//
// The real GPU lighting math (sprite.frag.hlsl) computes emitter contribution
// per-pixel and correctly gives fires/torches a genuine radiance term, but
// that term is summed into `gpu_total` alongside ambient/sun/sky and is easy
// to lose against a bright daylight scene even after excluding it from the
// 2.0 ceiling clamp — a real point light's falloff is subtle next to broad
// daylight. This pass does NOT touch that math. It draws one soft, additive,
// colour-tinted radial-gradient quad per visible emitter directly on top of
// the fully composited world_target, exactly like the glow halo overlays
// used by countless 2D games (a billboard sprite, not a lighting model). It
// makes fires/torches visually read as light sources at a glance, regardless
// of what the physically-based term underneath is doing.
//
// Follows the sound_wave_pass pattern: instance storage buffer + procedural
// quad vertex shader + custom fragment shader, ADDITIVE-blended onto the
// world target (same blend convention as bloom_composite.frag.hlsl).

#include <SDL3/SDL_gpu.h>
#include <cstdint>
#include <vector>

namespace lighting {

class gpu_device;

// One glow quad instance (wire-stable with vertex shader).
// 32 bytes — one per visible light-emitting source this frame.
//
// EVERY field is a plain scalar float, not float2/float3, and the mirrored
// HLSL struct (emitter_glow.vert.hlsl) MUST stay scalar-only too: HLSL pads
// a vector to the next 16-byte boundary whenever it would otherwise straddle
// one, which silently shifts every field after it out of sync with this
// tightly-packed layout. Cost one real bug already — `strength` was reading
// the always-zero `pad0` byte once `float3 color` triggered that padding,
// so the whole pass drew but was invisible (zero output every frame).
struct emitter_glow_instance {
    float x;         // center X in screen pixels
    float y;         // center Y in screen pixels
    float radius_px; // visual glow radius in pixels
    float r;         // tint colour (0..1 linear)
    float g;
    float b;
    float strength; // opacity/intensity multiplier, roughly 0..1
    float pad0;     // reserved, keeps the struct a multiple of 16 bytes
};
static_assert(sizeof(emitter_glow_instance) == 32,
              "emitter_glow_instance must be 32 bytes (wire-stable with vert shader)");

// Per-record parameters for the glow pass.
struct emitter_glow_record_options {
    SDL_GPUCommandBuffer* cb = nullptr;
    SDL_GPUTexture* target = nullptr;
    std::uint32_t proj_w = 0;
    std::uint32_t proj_h = 0;
    const std::vector<emitter_glow_instance>* instances = nullptr;
};

class emitter_glow_pass {
public:
    emitter_glow_pass() = default;
    emitter_glow_pass(const emitter_glow_pass&) = delete;
    auto operator=(const emitter_glow_pass&) -> emitter_glow_pass& = delete; // *NOPAD*
    ~emitter_glow_pass();

    // Build pipeline + instance buffers. `target_format` is the world_target
    // format the pass renders into.
    auto init(gpu_device& dev, SDL_GPUTextureFormat target_format) -> bool;

    auto shutdown() noexcept -> void;

    auto ready() const noexcept -> bool
    {
        return dev_ != nullptr && pipeline_ != nullptr && storage_ != nullptr && xfer_ != nullptr;
    }

    // Record all glow instances onto `target`, ADDITIVELY on top of whatever
    // is already there (LOAD, not CLEAR — this must run after tiles/bloom).
    auto record(const emitter_glow_record_options& opts) -> void;

private:
    auto upload_instances(
        SDL_GPUCommandBuffer* cb, const std::vector<emitter_glow_instance>& insts) -> bool;

    gpu_device* dev_ = nullptr;
    SDL_GPUTextureFormat target_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;

    SDL_GPUShader* vert_ = nullptr;
    SDL_GPUShader* frag_ = nullptr;
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;

    SDL_GPUTransferBuffer* xfer_ = nullptr;
    SDL_GPUBuffer* storage_ = nullptr;

    // Max instances: bounded by the CPU-side culling in sdl_render_frame.cpp
    // (player z-level + a modest world-space radius around the camera), so
    // this only needs to cover on-screen light sources, not the whole bubble.
    static constexpr int MAX_INSTANCES = 1024;
};

} // namespace lighting
