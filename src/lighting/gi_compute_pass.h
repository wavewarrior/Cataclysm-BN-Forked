#pragma once

// GI compute pass — Stage 1 of GI_COMPUTE_AND_PERF_PLAN.md (port the RC gather
// to GPU compute), bounce replaced by Radiance Cascades in Stage 7 of
// plans/gpu-daylight-black-scene-bisect-plan.md. FOUR compute dispatches per
// gather, on the caller's render command buffer:
//
//   1. FIELD   (gi_field.comp):  one thread = one tile. Per-tile direct
//      radiance = occluded emitter gather (sphere-march the SDF) + sun/sky
//      injection, tinted by the tile's albedo (albedo bleed). Writes
//      field_buf_. UNCHANGED by Stage 7 — Radiance Cascades consumes this as
//      its emitter texture exactly like the old bounce did.
//   2. RC BUILD  (rc_build.comp):  one dispatch per cascade (RC_CASCADES of
//      them). One thread = one cascade probe, looping over that cascade's
//      directions; sphere-traces the probe's own world-tile interval through
//      the SDF, sampling field_buf_ at the hit tile. Writes rc_atlas_.
//   3. RC MERGE  (rc_merge.comp):  one dispatch per cascade, DESCENDING
//      (RC_CASCADES-2 .. 0). Merges cascade i against the (already-merged)
//      cascade i+1 — the bilinear-fix cascade combine — writing the merged
//      result back into cascade i's own rc_atlas_ slot.
//   4. RC RESOLVE (rc_resolve.comp): one thread = one tile. Averages cascade
//      0's directions (now fully merged) into one irradiance value, writing
//      GiOut = gi_out_buf_ in the SAME layout the old gi_bounce2.comp wrote —
//      the sprite's GI input is unchanged; this is the only RC pass it
//      observes.
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
// Shaders: data/shaders/lighting/src/gi_field.comp.hlsl + rc_build.comp.hlsl
// + rc_merge.comp.hlsl + rc_resolve.comp.hlsl. Geometry: rc_params.h.

#include <SDL3/SDL_gpu.h>
#include <cstdint>

namespace lighting {

class gpu_device;

// Per-gather tuning, pushed as the compute uniform (b0/space2). Field names
// match the call site (sdl_render_frame.cpp). 68 bytes; shared by the field
// pass. Layout MUST match the GiParams cbuffer in gi_field.comp. `rc_pad1`
// replaces the retired `gi_bounce2` EMA-bounce knob (Radiance Cascades has
// no temporal filter to tune) — kept as a named pad, not removed, so this
// struct's size and the GiParams cbuffer's offsets stay unchanged for
// gi_field.comp, which still declares the full shared push. `gi_temporal`'s
// old slot is repurposed (not retired) as `gi_feedback`: multi-bounce
// radiance feedback, since a single RC bounce alone did not carry daylight
// through windows into deep interiors — see gi_field.comp.hlsl's PrevGiBuf.
struct gi_params {
    std::uint32_t emitter_count;
    std::uint32_t map_w; // runtime tile dims (thread/tile grid extent)
    std::uint32_t map_h;
    float current_z;            // probe z-plane (skip off-plane emitters)
    float shadow_k;              // sphere-trace cone hardness (reuse sprite knob)
    std::uint32_t shadow_steps; // per-emitter march cap
    float gi_feedback = 0.f; // multi-bounce feedback strength (0=off); was rc_pad0/gi_temporal
    float rc_pad1 = 0.f; // reserved: was gi_bounce2 (2nd-bounce mix), retired Stage 7
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

    // Compile all four compute pipelines + allocate field_buf_/rc_atlas_/
    // gi_out_buf_ for a max_w × max_h tile grid. Zeroes gi_out_buf_ +
    // rc_atlas_ once so the sprite never reads garbage before the first
    // gather. Returns false on failure (logged); a failed pipeline leaves
    // gi_out_buf_ a valid zero buffer (ready() is false → record() is a
    // no-op → GI reads as off).
    bool init(gpu_device& dev, std::uint32_t max_w, std::uint32_t max_h);

    // Reallocate the buffers for a new max tile size. Cheap no-op if
    // unchanged. Returns false on failure.
    bool resize(std::uint32_t max_w, std::uint32_t max_h);

    void shutdown() noexcept;

    bool ready() const noexcept {
        return field_pipeline_ != nullptr && rc_build_pipeline_ != nullptr
               && rc_merge_pipeline_ != nullptr && rc_resolve_pipeline_ != nullptr
               && field_buf_ != nullptr && rc_atlas_ != nullptr && gi_out_buf_ != nullptr;
    }

    // The GI radiance buffer (Radiance Cascades cascade-0 resolve). Bound by
    // the sprite pass as GiBuf (fragment storage buffer). Tile-res, x-major
    // gi[(x*map_h+y)*4 + c]. Always non-null after a successful init (even if
    // a pipeline failed), so the sprite's all-or-none storage-buffer bind
    // always has a valid handle.
    SDL_GPUBuffer* gi_buffer() const noexcept { return gi_out_buf_; }

    // Run the four compute passes on `cb`: field (writes field_buf_) →
    // RC build ×RC_CASCADES (writes rc_atlas_) → RC merge ×(RC_CASCADES-1)
    // descending (merges rc_atlas_ in place) → RC resolve (writes
    // gi_out_buf_). SDL_GPU inserts the compute→compute barriers between them
    // and the compute-write→graphics-read barrier on gi_out_buf_ before the
    // sprite pass. No-op if not ready or any arg invalid. The field pass
    // binds emitter_buf (t0) + sdf_buf (t1) + sky_buf (t2) + albedo_buf (t3)
    // as readonly compute storage buffers; all must carry
    // SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ. sky_buf (sky_sun_pass output)
    // feeds the P2 daylight-bounce injection — it must be recorded BEFORE
    // this call so SDL_GPU inserts the write→read barrier.
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
    SDL_GPUComputePipeline* rc_build_pipeline_ = nullptr;
    SDL_GPUComputePipeline* rc_merge_pipeline_ = nullptr;
    SDL_GPUComputePipeline* rc_resolve_pipeline_ = nullptr;
    SDL_GPUBuffer* field_buf_ = nullptr;  // FIELD out / RC BUILD in (RW|R)
    SDL_GPUBuffer* rc_atlas_ = nullptr;   // RC BUILD out / RC MERGE in-place (compute RW)
    SDL_GPUBuffer* gi_out_buf_ = nullptr; // RC RESOLVE out (compute W | graphics R)
    std::uint32_t max_w_ = 0;
    std::uint32_t max_h_ = 0;
    std::uint32_t rc_atlas_floats_ = 0; // allocated size, for the zero-fill and bounds
};

} // namespace lighting
