#pragma once
#include <cstdint>

namespace lighting {

// Shape tag for a gpu_emitter. Drives the per-emitter light model in the shader.
enum class emitter_shape : uint32_t {
    OMNI = 0,        // Omnidirectional point light (default).
    CONE = 1,        // Directed cone (flashlights, vehicle headlights).
    DIRECTIONAL = 2, // Infinite directional light (sun proxy).
};

// Wire-stable GPU emitter descriptor.
// Uploaded to an SDL_GPUBuffer (GRAPHICS_STORAGE_READ); the Phase-5+
// shaders read it via a storage binding.  Changing any field layout
// requires updating the corresponding HLSL/GLSL struct.
// Size: 64 bytes (static_assert enforced).
struct gpu_emitter {
    // World-space tile position.  Stored as float so the shader can do
    // sub-tile math; integer tile centres are .x/.y/.z + 0.5.
    float pos_x;
    float pos_y;
    float pos_z;
    // Emission radius in tiles.  Beyond this distance the light contributes
    // nothing (hard cutoff before any soft falloff).
    float radius;

    // RGB energy (0.0–1.0 linear per-channel).
    // 0,0,0 encodes "uncolored white" so the shader can skip tinting.
    float r, g, b;
    // Falloff exponent: 1.0 = linear, 2.0 = quadratic (default 1.5).
    float falloff;

    // Normalized direction vector for CONE / DIRECTIONAL emitters.
    // Unused (0,0) for OMNI.
    float cone_dir_x, cone_dir_y;
    // Half-angle in radians for CONE emitters.
    // π (3.14159…) for OMNI (covers full sphere).
    float cone_half_angle;

    // Shape discriminant — see emitter_shape enum above.
    uint32_t shape;
    // Seed for per-emitter flicker animation in the Phase-5 shader.
    // Typically derived from the emitter's world position for stable noise.
    uint32_t flicker_seed;

    float pad0, pad1, pad2; // Padding to reach 64 bytes.
};
static_assert(sizeof(gpu_emitter) == 64, "gpu_emitter must be 64 bytes (wire-stable)");

// Per-frame emitter budget.
// Estimate: my_MAPSIZE=11 → ~132×132 tiles/z-level; 1–3% emitting ≈ 500–5K.
// 3 z-levels × 3K peak + creatures/vehicles + 2× headroom ≈ 8K.
// Raise if profiling shows overflow (collector logs a warning).
static constexpr int MAX_EMITTERS = 8192;

} // namespace lighting
