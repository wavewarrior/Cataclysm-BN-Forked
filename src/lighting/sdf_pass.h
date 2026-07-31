#pragma once
#include <cstdint>
#include <vector>

// Forward-declare SDL3 GPU types to avoid pulling in the full SDL header here.
struct SDL_GPUTexture;
struct SDL_GPUTransferBuffer;
struct SDL_GPUBuffer;
struct SDL_GPUDevice;
struct SDL_GPUCopyPass;

namespace lighting {
class gpu_device;

// SDF supersampling factor: the Euclidean DT is built on a grid SDF_SUPERSAMPLE×
// finer than the tile grid (each tile → SDF_SUPERSAMPLE² subcells, seeds
// replicated). Tile occluder EDGES stay grid-aligned, but the distance falloff
// becomes sub-tile-fine → penumbra floor ~1/SDF_SUPERSAMPLE tile (tight) WITHOUT
// the stair-stepping that nearest-sampling a tile-res field would cause. The SDF
// GPU buffer is sized SDF_SUPERSAMPLE² larger; distances are rescaled to tile
// units (÷SDF_SUPERSAMPLE) at build so the shader's cone trace is unchanged.
// MUST match the SDF_SS constant in sprite.frag.hlsl. (This is the resolution
// fix; it is orthogonal to JFA, which is only a faster way to COMPUTE a DT.)
inline constexpr int SDF_SUPERSAMPLE = 8;

// Manages per-z-level transparency, SDF (signed-distance-field) GPU storage buffers.
//
// P3.3: SDF is now computed on GPU via JFA (gpu_sdf_pass). The CPU no longer builds
// the distance transform — transparency feeds trans_storage_ which the seed shader
// reads directly. sdf_storage_ is written by the JFA resolve pass.
//
// Layout: all per-tile data is in storage buffers (no sampler textures — nothing
// samples them). transparency feeds trans_storage_ (float, JFA seed input);
// SDF/sky_vis/vis are their own storage buffers.
//
// The data is passed to the collector thread which calls upload() inside a copy pass.
class sdf_pass {
public:
    sdf_pass() = default;
    ~sdf_pass();

    // Create GPU resources for a map_w × map_h tile grid.
    void init(gpu_device& dev, int map_w, int map_h);
    void shutdown(gpu_device& dev);

    // Upload pre-computed data.  Must be called from the collector thread
    // inside an existing SDL_GPUCopyPass; `dev` must match the device used
    // in init().
    // runtime_w/h  : actual map dimensions for this upload (may be < texture
    //                allocation; texture is sized for REALITY_BUBBLE_SIZE_MAX).
    //                Upload writes a runtime_w × runtime_h sub-rect at (0,0).
    //                Shader uses these dims (via map_w/map_h accessors) so the
    //                sample math matches the CPU x-major layout.
    // transparency : runtime_w*runtime_h bytes (0=opaque, 255=transparent).
    // sdf          : P3.3: no longer consumed — JFA writes SDF on GPU. Retained
    //                for backward compat with emitter_collector::submit().
    // sky_vis      : runtime_w*runtime_h bytes (255=open sky, 0=indoor).
    //                Empty = skip.
    // vis          : runtime_w*runtime_h floats — per-tile visibility
    //                (>=0: raw seen_cache [0..1]; <0: memorized tile sentinel),
    //                x-major. Drives the Stoneshard-style soft vision falloff.
    //                Empty = skip.
    void upload(
        SDL_GPUCopyPass* cp, SDL_GPUDevice* dev, int runtime_w, int runtime_h,
        const std::vector<uint8_t>& transparency, const std::vector<float>& sdf,
        const std::vector<uint8_t>& sky_vis = {}, const std::vector<float>& vis = {},
        // Stage 2b: unified coverage occluder field, tile-res, 2 floats/tile
        // (height, roof). Marched by sky_sun.comp. Empty = skip.
        const std::vector<float>& occ = {});

    // Phase 6b: SDF values as a vertex-readable storage buffer (JFA output).
    SDL_GPUBuffer* sdf_buffer() const noexcept { return sdf_storage_; }
    // Sky visibility as a fragment-readable storage buffer of floats
    // (1.0=open sky, 0.0=roofed) — sampler-texture Load returns 0 on Metal.
    SDL_GPUBuffer* sky_vis_buffer() const noexcept { return skyvis_storage_; }
    // Per-tile visibility as a fragment-readable storage buffer of floats
    // (>=0 = live seen_cache [0..1]; <0 = memorized-tile sentinel). Drives the
    // soft vision falloff + memory desaturate-fade in the fragment shader.
    SDL_GPUBuffer* vis_buffer() const noexcept { return visbuf_storage_; }
    // Stage 2b: unified coverage occluder field (tile-res, 2 floats/tile: height,
    // roof). Marched by sky_sun.comp for sun/moon/sky occlusion. COMPUTE-readable.
    SDL_GPUBuffer* occ_buffer() const noexcept { return occ_storage_; }
    // P3 JFA input: tile-res transparency as floats (0.0=opaque .. 1.0=open).
    // COMPUTE-readable so the seed shader can read it directly.
    SDL_GPUBuffer* trans_buffer() const noexcept { return trans_storage_; }

    bool ready() const noexcept { return sdf_storage_ != nullptr; }
    // True after the first successful upload(). Until then the SDF/sky_vis
    // buffers contain undefined/zero bytes — the fragment shader must NOT
    // run its shadow march over them (would read s=0 → shadow=0 →
    // emitter contribution clamped everywhere except <1 tile from the
    // light source). render_state::begin_lighting_frame uses this to
    // pass sdf_map_w/h=0 to the shader on the main menu (no world loaded).
    bool populated() const noexcept { return populated_; }

    // Runtime dimensions of the most recent successful upload.  These are
    // what the shader must use to clamp + sample (the texture itself may be
    // larger — sized for REALITY_BUBBLE_SIZE_MAX at init time).  Zero until
    // the first upload() lands.
    int map_w() const noexcept { return runtime_w_; }
    int map_h() const noexcept { return runtime_h_; }
    // Physical texture extent — diagnostic only.
    int tex_w() const noexcept { return map_w_; }
    int tex_h() const noexcept { return map_h_; }

private:
    SDL_GPUBuffer* sdf_storage_ = nullptr;      // fragment storage buffer (SdfBuf, JFA output)
    SDL_GPUBuffer* skyvis_storage_ = nullptr;   // fragment storage buffer (SkyVisBuf, floats)
    SDL_GPUBuffer* occ_storage_ = nullptr;      // Stage 2b unified coverage occluder (tile-res, 2
                                                // floats/tile)
    SDL_GPUTransferBuffer* xfer_occ_ = nullptr; // float bytes for occ_storage_
    SDL_GPUBuffer* trans_storage_ = nullptr;    // P3 JFA input: tile-res transparency as floats
    SDL_GPUTransferBuffer* xfer_trans_f_ = nullptr; // float bytes for trans_storage_
    SDL_GPUBuffer* visbuf_storage_ = nullptr; // fragment storage buffer (VisBuf, 1 float/tile)
    SDL_GPUTransferBuffer* xfer_sky_vis_ = nullptr;  // R8 bytes for sky_vis_tex_
    SDL_GPUTransferBuffer* xfer_skyvis_f_ = nullptr; // float bytes for skyvis_storage_
    SDL_GPUTransferBuffer* xfer_vis_f_ = nullptr;    // float bytes for visbuf_storage_ (1/tile)
    int map_w_ = 0; // physical texture extent (REALITY_BUBBLE_SIZE_MAX*SEEX)
    int map_h_ = 0;
    int runtime_w_ = 0; // last-uploaded runtime dimensions (≤ map_w_)
    int runtime_h_ = 0;
    bool populated_ = false;
};

} // namespace lighting
