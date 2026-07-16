#pragma once

// Lightweight UI post-processing pass — applies subtle bloom and chromatic
// aberration to the composite UI texture before presenting to swapchain.
//
// Single fullscreen-triangle pass: reads the UI composite texture, applies
// a 5-tap horizontal + 5-tap vertical box blur on bright pixels for bloom,
// then offsets R/B channels radially for chromatic aberration. The chromatic
// aberration intensity is driven by hud_shake::intensity() so it only fires
// on damage events and decays naturally.
//
// Shader: data/shaders/lighting/src/ui_post.frag.hlsl (uses tonemap.vert).

#include <SDL3/SDL_gpu.h>
#include <cstdint>

namespace lighting {

class gpu_device;

class ui_post_pass {
public:
    ui_post_pass() = default;
    ui_post_pass( const ui_post_pass & ) = delete;
    ui_post_pass &operator=( const ui_post_pass & ) = delete;
    ~ui_post_pass();

    // Build pipeline + shader. `format` is the swapchain/composite texture format.
    auto init( gpu_device &dev, SDL_GPUTextureFormat format,
               std::uint32_t full_w, std::uint32_t full_h ) -> bool;

    void shutdown() noexcept;

    auto ready() const noexcept -> bool {
        return pipeline_ != nullptr && vert_ != nullptr && post_frag_ != nullptr && sampler_ != nullptr;
    }
    // Record a fullscreen post-process pass: reads `src_tex`, writes to
    // `dst_tex` (swapchain). `ca_intensity` in [0,1] controls chromatic
    // aberration strength (0 = off, tied to hud_shake). `bloom_strength`
    // in [0,1] controls bloom amount.
    auto record( SDL_GPUCommandBuffer *cb, SDL_GPUTexture *src_tex,
                 SDL_GPUTexture *dst_tex,
                 std::uint32_t full_w, std::uint32_t full_h,
                 float ca_intensity, float bloom_strength ) -> void;
private:
    gpu_device *dev_ = nullptr;
    SDL_GPUTextureFormat format_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUShader *vert_ = nullptr;
    SDL_GPUSampler *sampler_ = nullptr; // LINEAR for blur taps
    SDL_GPUShader *post_frag_ = nullptr;
    SDL_GPUGraphicsPipeline *pipeline_ = nullptr;
    std::uint32_t full_w_ = 0;
    std::uint32_t full_h_ = 0;
};

} // namespace lighting