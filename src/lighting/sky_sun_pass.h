#pragma once

// Sky/sun directional skylight compute pass — Stage 2a of
// GI_COMPUTE_AND_PERF_PLAN.md. One compute dispatch (one thread = one tile):
// marches the wall-only SunSdf in N hemisphere directions to compute per-tile
// directional sky-access (the sky as an occluded dome — alcove/overhang
// self-shading + directional indoor daylight from window openings), plus the
// sun shadow occlusion. Writes sky_buf_ (rgb = sky-access, a = sun-occ), read
// by sprite.frag as SkyBuf — replacing the flat sky ambient + the inline sun
// trace_shadow. Lets frame_build delete the CPU window-bleed flood-fill (the
// march owns indoor propagation now).
//
// Structurally mirrors gi_compute_pass (scalar StructuredBuffers, numthreads
// 8x8, readonly-declared inputs, region-limited grid). Inputs SunSdf + SkyVis
// gain SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ in sdf_pass so this can bind
// them. Shader: data/shaders/lighting/src/sky_sun.comp.hlsl.

#include <SDL3/SDL_gpu.h>
#include <cstdint>

namespace lighting {

class gpu_device;

// Per-dispatch tuning, pushed as the compute uniform (b0/space2). Field names +
// ORDER must match the SkySunParams cbuffer in sky_sun.comp.hlsl. 48 bytes.
struct sky_sun_params {
    std::uint32_t map_w; // runtime tile dims (thread/tile grid extent)
    std::uint32_t map_h;
    float sun_dir_x; // sun travel direction (toward_sun = -sun_dir)
    float sun_dir_y;
    float sun_sin_elev;         // sun elevation sine (2b heightfield; unused 2a)
    float shadow_k;             // sphere-trace cone hardness (sprite shadow_k)
    std::uint32_t shadow_steps; // sun march iteration cap
    // P5b: sky/sun quality knobs (was ss_pad).
    std::uint32_t sky_dirs = 8;     // hemisphere directions per tile
    float sky_reach = 10.0f;        // sky march max distance (tiles)
    std::uint32_t sun_steps = 24;   // celestial march steps
    std::uint32_t sun_penumbra = 4; // penumbra angular samples (1=hard edge)
};

class sky_sun_pass {
public:
    sky_sun_pass() = default;
    sky_sun_pass(const sky_sun_pass&) = delete;
    sky_sun_pass& operator=(const sky_sun_pass&) = delete;
    ~sky_sun_pass();

    // Compile the compute pipeline + allocate sky_buf_ for a max_w × max_h
    // tile grid (4 floats/tile). Zeroes sky_buf_ once so the sprite never
    // reads garbage before the first dispatch. Returns false on failure
    // (logged); a failed pipeline leaves sky_buf_ a valid zero buffer
    // (ready() is false → record() is a no-op → sky reads as zero = dark,
    // matching "no sky data yet").
    bool init(gpu_device& dev, std::uint32_t max_w, std::uint32_t max_h);

    // Reallocate sky_buf_ for a new max tile size. Cheap no-op if unchanged.
    bool resize(std::uint32_t max_w, std::uint32_t max_h);

    void shutdown() noexcept;

    bool ready() const noexcept { return pipeline_ != nullptr && sky_buf_ != nullptr; }

    // The sky/sun radiance buffer. Bound by the sprite pass as SkyBuf
    // (fragment storage buffer slot 6 ⇒ t8). Tile-res, x-major
    // sky[(x*map_h+y)*4 + c]: rgb = sky-access, a = sun-occ. Always non-null
    // after a successful init (even if the pipeline failed), so the sprite's
    // all-or-none storage-buffer bind always has a valid handle.
    SDL_GPUBuffer* sky_buffer() const noexcept { return sky_buf_; }

    // Run the compute pass on `cb`: one dispatch reading occ_buf (t0) — the
    // unified coverage occluder field (2 floats/tile: height + roof bit), which
    // must carry SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ — and writing sky_buf_.
    // SDL_GPU inserts the compute-write→graphics-read barrier on sky_buf_ before
    // the sprite pass. No-op if not ready or any arg invalid.
    void record(
        SDL_GPUCommandBuffer* cb, SDL_GPUBuffer* occ_buf, std::uint32_t runtime_w,
        std::uint32_t runtime_h, const sky_sun_params& params);

private:
    SDL_GPUBuffer* create_buffer(std::uint32_t floats, SDL_GPUBufferUsageFlags usage);
    void zero_buffer(SDL_GPUBuffer* buf, std::uint32_t floats);

    gpu_device* dev_ = nullptr;
    SDL_GPUComputePipeline* pipeline_ = nullptr;
    SDL_GPUBuffer* sky_buf_ = nullptr; // compute W | graphics R
    std::uint32_t max_w_ = 0;
    std::uint32_t max_h_ = 0;
};

} // namespace lighting
