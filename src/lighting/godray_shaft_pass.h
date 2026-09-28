#pragma once

// God-ray window-light-shaft pass ("smoke and mirrors", Step 6b of the
// atmospheric-lighting-coherence plan). Replaces the deleted SDF ray-march
// `volumetric_pass`: a full 3D volumetric march is expensive and reads weak on
// a 2D top-down view, so instead this draws one additive gradient BEAM SPRITE
// per visible window (the same fake-volumetric trick as Terraria/Dead Cells
// light shafts) — an oriented quad stretched along the sun direction, with a
// procedural far-end fade in the fragment shader. Recorded BEFORE bloom, like
// emitter_glow_pass, so the existing bloom pass supplies the soft halo around
// the beam rather than a second blur pass.
//
// Source data: the per-window CONE emitters already built CPU-side each frame
// (lighting::snapshot.cpp's make_cone calls for window openings) — no new
// detection pass. The CPU builder in sdl_render_frame.cpp filters
// EmitterOverlayState's snapshot for CONE emitters, applies the SAME VIS_CLEAR
// visibility gate emitter_glow_pass uses (so a shaft can only appear where the
// player already sees the window — no fog-of-war leak), and projects to
// screen space.
//
// Follows emitter_glow_pass's pattern exactly: instance storage buffer +
// procedural quad vertex shader + custom fragment shader, ADDITIVE-blended
// (ONE/ONE) onto the world target (same convention as bloom_composite.frag.hlsl).

#include <SDL3/SDL_gpu.h>
#include <cstdint>
#include <vector>

namespace lighting {

class gpu_device;

// One beam quad instance (wire-stable with the vertex shader).
// 40 bytes — one per visible window this frame.
//
// EVERY field is a plain scalar float, not float2/float3 — see
// emitter_glow_instance's comment for why: HLSL silently pads a vector to the
// next 16-byte boundary whenever it would otherwise straddle one, shifting
// every field after it out of sync with this tightly-packed C++ layout.
struct godray_shaft_instance {
    float cx;            // beam origin (window centre) X, screen pixels
    float cy;            // beam origin (window centre) Y, screen pixels
    float dir_x;         // unit beam axis X (light travel direction, into the room)
    float dir_y;         // unit beam axis Y
    float length_px;     // beam length, screen pixels
    float half_width_px; // beam half-width at the base, screen pixels
    float r;             // tint colour (0..1 linear)
    float g;
    float b;
    float strength; // opacity/intensity multiplier
};
static_assert(
    sizeof(godray_shaft_instance) == 40,
    "godray_shaft_instance must be 40 bytes (wire-stable with vert shader)");

// Per-record parameters for the shaft pass.
struct godray_shaft_record_options {
    SDL_GPUCommandBuffer* cb = nullptr;
    SDL_GPUTexture* target = nullptr;
    std::uint32_t proj_w = 0;
    std::uint32_t proj_h = 0;
    float anim_time = 0.f; // wrapped render seconds; faint beam shimmer
    const std::vector<godray_shaft_instance>* instances = nullptr;
};

class godray_shaft_pass {
public:
    godray_shaft_pass() = default;
    godray_shaft_pass(const godray_shaft_pass&) = delete;
    auto operator=(const godray_shaft_pass&) -> godray_shaft_pass& = delete; // *NOPAD*
    ~godray_shaft_pass();

    // Build pipeline + instance buffers. `target_format` is the world_target
    // format the pass renders into.
    auto init(gpu_device& dev, SDL_GPUTextureFormat target_format) -> bool;

    auto shutdown() noexcept -> void;

    auto ready() const noexcept -> bool {
        return dev_ != nullptr && pipeline_ != nullptr && storage_ != nullptr && xfer_ != nullptr;
    }

    // Record all shaft instances onto `target`, ADDITIVELY on top of whatever
    // is already there (LOAD, not CLEAR — this must run after tiles, before
    // bloom so bloom can spread the beam's own halo).
    auto record(const godray_shaft_record_options& opts) -> void;

private:
    auto upload_instances(SDL_GPUCommandBuffer* cb, const std::vector<godray_shaft_instance>& insts)
        -> bool;

    gpu_device* dev_ = nullptr;
    SDL_GPUTextureFormat target_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;

    SDL_GPUShader* vert_ = nullptr;
    SDL_GPUShader* frag_ = nullptr;
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;

    SDL_GPUTransferBuffer* xfer_ = nullptr;
    SDL_GPUBuffer* storage_ = nullptr;

    // Max instances: bounded by the CPU-side culling in sdl_render_frame.cpp
    // (player z-level + the same CULL_RADIUS_TILES as emitter_glow_pass), so
    // this only needs to cover on-screen windows, not the whole bubble.
    static constexpr int MAX_INSTANCES = 512;
};

} // namespace lighting
