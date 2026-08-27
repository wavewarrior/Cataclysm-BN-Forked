#pragma once

// GI compute pass — Stage 1 of GI_COMPUTE_AND_PERF_PLAN.md (port the RC gather
// to GPU compute). Replaces radiance_cascade_pass. THREE compute dispatches per
// gather, on the caller's render command buffer:
//
//   1. FIELD  (gi_field.comp):  one thread = one tile. Per-tile direct radiance
//      = occluded emitter gather (sphere-march the SDF) + sun/sky injection,
//      tinted by the tile's albedo (albedo bleed). Writes field_buf_.
//   2. BOUNCE (gi_bounce.comp): one thread = one tile. March N rays through the
//      field, accumulating lit-surface radiance before a wall. Writes gi_buf_
//      — the 1st-bounce term.
//   3. BOUNCE2 (gi_bounce2.comp): one thread = one tile. March the SAME rays
//      through the 1st-bounce field (gi_buf_) → the 2nd-bounce term, temporally
//      EMA-filtered across rebuilds (ping-pong term buffer), then write the
//      COMBINED field (1st + k·2nd) to gi_out_buf_ — the sprite's GI input.
//
// Why compute, not the old fragment passes: rc.frag/rc_bounce.frag created on
// Metal but failed SDL_CreateGPUGraphicsPipeline root-signature construction on
// D3D12 (fragment storage buffer with no leading sampler). Compute uses a
// distinct binding/reflection model (SDL_BindGPUComputeStorageBuffers + RW
// bindings) that dodges it, keeps GI off the main thread, and writes a plain
// storage buffer (no transposed color-target, no all-or-none storage-texture
// hazard). gi_out_buf_ is the sprite's GI input: a tile-res RGB(+pad)
// StructuredBuffer the sprite reads as GiBuf.
//
// Shaders: data/shaders/lighting/src/gi_field.comp.hlsl + gi_bounce.comp.hlsl
// + gi_bounce2.comp.hlsl.

#include <SDL3/SDL_gpu.h>
#include <cstdint>

namespace lighting {

class gpu_device;

// Per-gather tuning, pushed as the compute uniform (b0/space2). Field names
// match the call site (sdl_render_frame.cpp). 64 bytes; shared by all three
// passes. Layout MUST match the GiParams cbuffer in gi_field/gi_bounce/
// gi_bounce2.comp.
struct gi_params {
    std::uint32_t emitter_count;
    std::uint32_t map_w; // runtime tile dims (thread/tile grid extent)
    std::uint32_t map_h;
    float current_z;            // probe z-plane (skip off-plane emitters)
    float shadow_k;             // sphere-trace cone hardness (reuse sprite knob)
    std::uint32_t shadow_steps; // per-emitter march cap
    float gi_temporal;          // 2nd-bounce EMA blend (0=off, 1=full replace)
    float gi_bounce2;           // 2nd-bounce mix: out = 1st + k·2nd (0=off)
    // P2 sun/sky surface-radiance injection into the field (gi_field.comp reads
    // SkyBuf). Colour/intensity mirror the sprite's direct sun/sky terms so the
    // bounced daylight matches.
    float sun_r = 0.f, sun_g = 0.f, sun_b = 0.f, sun_intensity = 0.f;
    float sky_r = 0.f, sky_g = 0.f, sky_b = 0.f, sky_intensity = 0.f;
    float gi_albedo; // albedo-bleed mix (0=off): field *= lerp(1, albedo, k)
};

class gi_compute_pass {
public:
    gi_compute_pass() = default;
    gi_compute_pass(const gi_compute_pass&) = delete;
    gi_compute_pass& operator=(const gi_compute_pass&) = delete;
    ~gi_compute_pass();

    // Compile all three compute pipelines + allocate field_buf_/gi_buf_/
    // gi_out_buf_/term buffers for a max_w × max_h tile grid (4 floats/tile).
    // Zeroes gi_out_buf_ + the term buffers once so the sprite never reads
    // garbage before the first gather. Returns false on failure (logged); a
    // failed pipeline leaves gi_out_buf_ a valid zero buffer (ready() is false
    // → record() is a no-op → GI reads as off).
    bool init(gpu_device& dev, std::uint32_t max_w, std::uint32_t max_h);

    // Reallocate the buffers for a new max tile size. Cheap no-op if
    // unchanged. Returns false on failure.
    bool resize(std::uint32_t max_w, std::uint32_t max_h);

    void shutdown() noexcept;

    bool ready() const noexcept {
        return field_pipeline_ != nullptr && bounce_pipeline_ != nullptr
               && bounce2_pipeline_ != nullptr && field_buf_ != nullptr && gi_buf_ != nullptr
               && gi_out_buf_ != nullptr && term_a_ != nullptr && term_b_ != nullptr;
    }

    // The GI radiance buffer (COMBINED 1st+k·2nd field). Bound by the sprite
    // pass as GiBuf (fragment storage buffer). Tile-res, x-major
    // gi[(x*map_h+y)*4 + c]. Always non-null after a successful init (even if
    // a pipeline failed), so the sprite's all-or-none storage-buffer bind
    // always has a valid handle.
    SDL_GPUBuffer* gi_buffer() const noexcept { return gi_out_buf_; }

    // Run the three compute passes on `cb`: field (writes field_buf_) →
    // bounce (writes gi_buf_, the 1st-bounce term) → bounce2 (reads gi_buf_ +
    // the ping-pong term buffer, writes the combined field to gi_out_buf_).
    // SDL_GPU inserts the compute→compute barriers between them and the
    // compute-write→graphics-read barrier on gi_out_buf_ before the sprite
    // pass. No-op if not ready or any arg invalid. The field pass binds
    // emitter_buf (t0) + sdf_buf (t1) + sky_buf (t2) + albedo_buf (t3) as
    // readonly compute storage buffers; all must carry
    // SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ. sky_buf (sky_sun_pass output)
    // feeds the P2 daylight-bounce injection — it must be recorded BEFORE this
    // call so SDL_GPU inserts the write→read barrier.
    void record(
        SDL_GPUCommandBuffer* cb, SDL_GPUBuffer* emitter_buf, SDL_GPUBuffer* sdf_buf,
        SDL_GPUBuffer* sky_buf, SDL_GPUBuffer* albedo_buf, std::uint32_t runtime_w,
        std::uint32_t runtime_h, const gi_params& params);

    // Dev oracle: synchronous GPU→CPU readback of gi_out_buf_ over the runtime
    // tile region; logs sum/max/nonzero/centroid to DC::Main. Stalls the GPU
    // (SDL_WaitForGPUIdle) — call on demand (F4 button), never per frame.
    void debug_log_stats(std::uint32_t runtime_w, std::uint32_t runtime_h);

private:
    SDL_GPUBuffer* create_buffer( std::uint32_t floats, SDL_GPUBufferUsageFlags usage );
    void zero_buffer( SDL_GPUBuffer* buf, std::uint32_t floats );

    gpu_device* dev_ = nullptr;
    SDL_GPUComputePipeline* field_pipeline_ = nullptr;
    SDL_GPUComputePipeline* bounce_pipeline_ = nullptr;
    SDL_GPUComputePipeline* bounce2_pipeline_ = nullptr;
    SDL_GPUBuffer* field_buf_ = nullptr;  // pass 1 out / pass 2 in (RW|R)
    SDL_GPUBuffer* gi_buf_ = nullptr;     // pass 2 out = 1st-bounce term (compute W|R)
    SDL_GPUBuffer* gi_out_buf_ = nullptr; // pass 3 out = combined (compute W | graphics R)
    SDL_GPUBuffer* term_a_ = nullptr;     // 2nd-bounce term ping-pong (compute RW)
    SDL_GPUBuffer* term_b_ = nullptr;     // 2nd-bounce term ping-pong (compute RW)
    bool term_flip_ = false;              // which term buffer is "prev" this frame
    std::uint32_t max_w_ = 0;
    std::uint32_t max_h_ = 0;
};

} // namespace lighting
