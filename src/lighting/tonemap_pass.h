#pragma once

// Fullscreen tonemap pass — the HDR RT backbone's resolve stage
// (LIGHTING_REWORK_PLAN.md step 1). Samples the lit scene target and writes a
// displayable result into a destination target via a single fullscreen
// triangle. Owns its own graphics pipeline + shaders (separate from the sprite
// batcher) and records a self-contained render pass on the destination, so it
// never touches the batcher's pass machinery.
//
// Curve lives in data/shaders/lighting/src/tonemap.frag.hlsl — identity in
// step 1a/1b, AgX in step 1c. The pass is the future home of the post chain
// (bloom / LUT hang off the same fullscreen-pass-into-offscreen-target pattern).

#include <SDL3/SDL_gpu.h>
#include <cstdint>

namespace lighting {

// ASC-CDL colour grade + post-processing parameters (b1/space3).
// Wire-stable: matches the GradeParams cbuffer in tonemap.frag.hlsl exactly.
struct grade_params {
    float cdl_slope_r = 1.0f, cdl_slope_g = 1.0f, cdl_slope_b = 1.0f, grade_pad0 = 0.0f;
    float cdl_offset_r = 0.0f, cdl_offset_g = 0.0f, cdl_offset_b = 0.0f, grade_pad1 = 0.0f;
    float cdl_power_r = 1.0f, cdl_power_g = 1.0f, cdl_power_b = 1.0f, grade_pad2 = 0.0f;
    float temperature = 0.0f;
    float tint = 0.0f;
    float saturation = 1.0f;
    float contrast = 1.05f;        // slightly punchy default
    float vignette_amount = 0.15f; // subtle default
    float grain_amount = 0.025f;   // subtle default
    float ca_amount = 0.0015f;     // very subtle default
    float gp_pad0 = 0.0f;          // align Row 4 to 16 bytes
};
static_assert( sizeof( grade_params ) == 80, "grade_params is wire-stable with GradeParams cbuffer" );

class gpu_device;

class tonemap_pass {
public:
    tonemap_pass() = default;
    tonemap_pass(const tonemap_pass&) = delete;
    tonemap_pass& operator=(const tonemap_pass&) = delete;
    ~tonemap_pass();

    // Build the pipeline writing into a target of `dst_format`. Returns
    // false on failure (logs via DC::SDL); ready() stays false.
    bool init(gpu_device& dev, SDL_GPUTextureFormat dst_format);

    // Release pipeline + shaders. Idempotent; safe while the device lives.
    void shutdown() noexcept;

    bool ready() const noexcept { return pipeline_ != nullptr; }

    // Record a fullscreen tonemap into `dst`: sample `src` with `sampler`,
    // run the tonemap shader, write `dst`. Opens and closes its own render
    // pass on `dst` (the triangle covers the whole target, so the load-op
    // is DONT_CARE). `cb` is the frame's render command buffer. No-op if
    // not ready or any argument is null.
    // `exposure` (pre-AgX scale) + `min_ev`/`max_ev` (AgX log2 range) are
    // the F4 tonemap sliders, pushed as a fragment uniform (b0/space3).
    // `ramp_enable` (Step 7) bypasses the AgX curve: palette-ramp output is already
    // display-referred, so re-mapping it would break the palette contract.
    void record(
        SDL_GPUCommandBuffer* cb, SDL_GPUTexture* src, SDL_GPUSampler* sampler, SDL_GPUTexture* dst,
        std::uint32_t dst_w, std::uint32_t dst_h, float exposure, float min_ev, float max_ev,
        float ramp_enable, const grade_params& grade );

private:
    gpu_device* dev_ = nullptr;
    SDL_GPUShader* vert_ = nullptr;
    SDL_GPUShader* frag_ = nullptr;
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
};

} // namespace lighting
