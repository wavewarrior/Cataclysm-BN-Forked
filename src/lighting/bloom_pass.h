#pragma once

// Bloom post pass — Step-4 (HDR RT backbone post chain). Extracts bright pixels
// from the HDR world_target, blurs them at half resolution, and composites the
// glow back ADDITIVELY onto world_target — before the tonemap pass maps it down.
//
// Four fullscreen-triangle sub-passes per frame, each with a SINGLE sampler
// (the tonemap shader is left untouched — no unprecedented 2-sampler Metal
// path):
//   1. EXTRACT   world_target (full) → bloom_a_ (half): luminance above threshold
//   2. BLUR H    bloom_a_ → bloom_b_  (separable Gaussian, horizontal)
//   3. BLUR V    bloom_b_ → bloom_a_  (separable Gaussian, vertical)
//   4. COMPOSITE bloom_a_ (half) → world_target (full), additive blend × intensity
//
// Owns its half-res RGBA16F textures + a LINEAR sampler (bilinear upscale).
// Shaders: data/shaders/lighting/src/bloom_extract.frag, bloom_blur.frag,
// bloom_composite.frag (+ tonemap.vert fullscreen tri).

#include <SDL3/SDL_gpu.h>
#include <cstdint>

namespace lighting {

class gpu_device;

class bloom_pass {
public:
    bloom_pass() = default;
    bloom_pass(const bloom_pass&) = delete;
    bloom_pass& operator=(const bloom_pass&) = delete;
    ~bloom_pass();

    // Build pipelines + half-res textures + linear sampler. hdr_format is the
    // world_target format the composite pipeline blends into. full_w/full_h
    // are the world_target pixel dims (bloom textures are half of these).
    bool init(
        gpu_device& dev, SDL_GPUTextureFormat hdr_format, std::uint32_t full_w,
        std::uint32_t full_h);

    bool resize(std::uint32_t full_w, std::uint32_t full_h);

    void shutdown() noexcept;

    bool ready() const noexcept {
        return extract_pipeline_ && blur_pipeline_ && composite_pipeline_ && bloom_a_ && bloom_b_
            && sampler_;
    }

    // Run extract → blur → composite, adding glow into `hdr_tex` in place.
    // No-op if not ready or any argument invalid. `hdr_tex` is both the
    // extract source and the composite destination (separate passes).
    void record(
        SDL_GPUCommandBuffer* cb, SDL_GPUTexture* hdr_tex, std::uint32_t full_w,
        std::uint32_t full_h, float threshold, float intensity);

private:
    bool create_textures(std::uint32_t full_w, std::uint32_t full_h);

    gpu_device* dev_ = nullptr;
    SDL_GPUTextureFormat hdr_format_ = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    SDL_GPUShader* vert_ = nullptr;
    SDL_GPUShader* extract_frag_ = nullptr;
    SDL_GPUShader* blur_frag_ = nullptr;
    SDL_GPUShader* composite_frag_ = nullptr;
    SDL_GPUGraphicsPipeline* extract_pipeline_ = nullptr;   // → bloom format
    SDL_GPUGraphicsPipeline* blur_pipeline_ = nullptr;      // → bloom format
    SDL_GPUGraphicsPipeline* composite_pipeline_ = nullptr; // → hdr_format, additive
    SDL_GPUSampler* sampler_ = nullptr;                     // linear/clamp
    SDL_GPUTexture* bloom_a_ = nullptr;                     // half-res ping
    SDL_GPUTexture* bloom_b_ = nullptr;                     // half-res pong
    std::uint32_t half_w_ = 0;
    std::uint32_t half_h_ = 0;
};

} // namespace lighting
