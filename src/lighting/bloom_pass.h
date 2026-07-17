#pragma once

// Bloom post pass — multi-scale dual-filter Kawase bloom. Extracts bright
// pixels from the HDR world_target, downsamples through a mip chain using a
// Kawase downfilter, then upsamples back with additive Kawase upfilter, and
// composites the glow ADDITIVELY onto world_target before tonemapping.
//
// Sub-passes per frame:
//   1. EXTRACT     world_target (full) → mip_chain_[0] (half): luminance threshold
//   2. DOWN chain  mip_chain_[i-1] → mip_chain_[i] for i=1..mip_count_-1
//   3. UP chain    mip_chain_[i+1] → mip_chain_[i] for i=mip_count_-2..0 (additive)
//   4. COMPOSITE   mip_chain_[0] → world_target (full), additive blend × intensity
//
// Owns progressively-halved RGBA16F mip textures + a LINEAR sampler.
// Shaders: bloom_extract.frag, bloom_kawase_down.frag, bloom_kawase_up.frag,
// bloom_composite.frag (+ tonemap.vert fullscreen tri).

#include <SDL3/SDL_gpu.h>
#include <cstdint>

namespace lighting {

class gpu_device;

class bloom_pass {
public:
    static constexpr int MAX_MIP_LEVELS = 5;

    bloom_pass() = default;
    bloom_pass(const bloom_pass&) = delete;
    bloom_pass& operator=(const bloom_pass&) = delete;
    ~bloom_pass();

    // Build pipelines + mip chain textures + linear sampler. hdr_format is the
    // world_target format the composite pipeline blends into. full_w/full_h
    // are the world_target pixel dims (mip_chain_[0] is half of these).
    bool init(
        gpu_device& dev, SDL_GPUTextureFormat hdr_format, std::uint32_t full_w,
        std::uint32_t full_h);

    bool resize(std::uint32_t full_w, std::uint32_t full_h);

    void shutdown() noexcept;

    auto ready() const noexcept -> bool {
        return extract_pipeline_ && down_pipeline_ && up_pipeline_ && composite_pipeline_
            && mip_chain_[0] && sampler_;
    }

    // Run extract → kawase down chain → kawase up chain → composite, adding
    // glow into `hdr_tex` in place. No-op if not ready or any argument invalid.
    void record(
        SDL_GPUCommandBuffer* cb, SDL_GPUTexture* hdr_tex, std::uint32_t full_w,
        std::uint32_t full_h, float threshold, float intensity);

private:
    bool create_textures(std::uint32_t full_w, std::uint32_t full_h);

    gpu_device* dev_ = nullptr;
    SDL_GPUTextureFormat hdr_format_ = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    SDL_GPUShader* vert_ = nullptr;
    SDL_GPUShader* extract_frag_ = nullptr;
    SDL_GPUShader* down_frag_ = nullptr;
    SDL_GPUShader* up_frag_ = nullptr;
    SDL_GPUShader* composite_frag_ = nullptr;
    SDL_GPUGraphicsPipeline* extract_pipeline_ = nullptr;   // → bloom format, no blend
    SDL_GPUGraphicsPipeline* down_pipeline_ = nullptr;      // → bloom format, no blend
    SDL_GPUGraphicsPipeline* up_pipeline_ = nullptr;        // → bloom format, additive
    SDL_GPUGraphicsPipeline* composite_pipeline_ = nullptr; // → hdr_format, additive
    SDL_GPUSampler* sampler_ = nullptr;                     // linear/clamp

    SDL_GPUTexture* mip_chain_[MAX_MIP_LEVELS] = {};  // progressively halved
    std::uint32_t mip_w_[MAX_MIP_LEVELS] = {};
    std::uint32_t mip_h_[MAX_MIP_LEVELS] = {};
    int mip_count_ = 0;
};

} // namespace lighting
