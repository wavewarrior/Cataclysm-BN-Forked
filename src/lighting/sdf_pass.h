#pragma once
#include <cstdint>
#include <vector>

// Forward-declare SDL3 GPU types to avoid pulling in the full SDL header here.
struct SDL_GPUTexture;
struct SDL_GPUTransferBuffer;
struct SDL_GPUBuffer;
struct SDL_GPUDevice;
struct SDL_GPUCopyPass;

namespace lighting
{
class gpu_device;

// Manages per-z-level transparency and SDF (signed-distance-field) GPU textures.
//
// Layout (Phase 4):  1 texel per tile.
//   transparency_tex : R8_UNORM  (0 = opaque, 255 = fully transparent)
//   sdf_tex          : R32_FLOAT (distance in tiles to nearest opaque tile)
//
// Texture flags include COMPUTE_STORAGE_WRITE so Phase 6 can switch to
// GPU JFA without recreating these textures.
//
// The CPU BFS distance transform (Chebyshev 8-connected) is computed on the
// main thread (~17K ops for my_MAPSIZE=11, well under 1ms).  Data is then
// passed to the collector thread which calls upload() inside a copy pass.
class sdf_pass
{
public:
    sdf_pass() = default;
    ~sdf_pass();

    // Create GPU textures for a map_w × map_h tile grid.
    void init( gpu_device &dev, int map_w, int map_h );
    void shutdown( gpu_device &dev );

    // Upload pre-computed data.  Must be called from the collector thread
    // inside an existing SDL_GPUCopyPass; `dev` must match the device used
    // in init().
    // runtime_w/h  : actual map dimensions for this upload (may be < texture
    //                allocation; texture is sized for REALITY_BUBBLE_SIZE_MAX).
    //                Upload writes a runtime_w × runtime_h sub-rect at (0,0).
    //                Shader uses these dims (via map_w/map_h accessors) so the
    //                sample math matches the CPU x-major layout.
    // transparency : runtime_w*runtime_h bytes (0=opaque, 255=transparent).
    // sdf          : runtime_w*runtime_h floats (tiles to nearest opaque).
    // sky_vis      : runtime_w*runtime_h bytes (255=open sky, 0=indoor).
    //                Empty = skip.
    // vis          : runtime_w*runtime_h floats — per-tile visibility
    //                (>=0: raw seen_cache [0..1]; <0: memorized tile sentinel),
    //                x-major. Drives the Stoneshard-style soft vision falloff.
    //                Empty = skip.
    void upload( SDL_GPUCopyPass *cp,
                 SDL_GPUDevice   *dev,
                 int runtime_w, int runtime_h,
                 const std::vector<uint8_t> &transparency,
                 const std::vector<float>   &sdf,
                 const std::vector<uint8_t> &sky_vis = {},
                 const std::vector<float>   &vis = {} );

    SDL_GPUTexture *transparency_texture() const noexcept { return transparency_tex_; }
    SDL_GPUTexture *sdf_texture()          const noexcept { return sdf_tex_; }
    // Phase 6b: SDF values as a vertex-readable storage buffer.
    SDL_GPUBuffer  *sdf_buffer()           const noexcept { return sdf_storage_; }
    // Phase 8: sky visibility per tile (R8_UNORM, 255=open sky, 0=indoor).
    SDL_GPUTexture *sky_vis_texture()      const noexcept { return sky_vis_tex_; }
    // Sky visibility as a fragment-readable storage buffer of floats
    // (1.0=open sky, 0.0=roofed) — sampler-texture Load returns 0 on Metal.
    SDL_GPUBuffer  *sky_vis_buffer()       const noexcept { return skyvis_storage_; }
    // Per-tile visibility as a fragment-readable storage buffer of floats
    // (>=0 = live seen_cache [0..1]; <0 = memorized-tile sentinel). Drives the
    // soft vision falloff + memory desaturate-fade in the fragment shader.
    SDL_GPUBuffer  *vis_buffer()           const noexcept { return visbuf_storage_; }

    bool ready() const noexcept { return sdf_tex_ != nullptr; }
    // True after the first successful upload(). Until then the SDF/sky_vis
    // textures contain undefined/zero bytes — the fragment shader must NOT
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
    SDL_GPUTexture        *transparency_tex_ = nullptr;
    SDL_GPUTexture        *sdf_tex_          = nullptr;
    SDL_GPUTexture        *sky_vis_tex_      = nullptr; // Phase 8: R8_UNORM
    SDL_GPUBuffer         *sdf_storage_      = nullptr; // fragment storage buffer (SdfBuf)
    SDL_GPUBuffer         *skyvis_storage_   = nullptr; // fragment storage buffer (SkyVisBuf, floats)
    SDL_GPUBuffer         *visbuf_storage_   = nullptr; // fragment storage buffer (VisBuf, 1 float/tile)
    SDL_GPUTransferBuffer *xfer_transparency_ = nullptr;
    SDL_GPUTransferBuffer *xfer_sdf_          = nullptr;
    SDL_GPUTransferBuffer *xfer_sky_vis_      = nullptr; // R8 bytes for sky_vis_tex_
    SDL_GPUTransferBuffer *xfer_skyvis_f_     = nullptr; // float bytes for skyvis_storage_
    SDL_GPUTransferBuffer *xfer_vis_f_        = nullptr; // float bytes for visbuf_storage_ (1/tile)
    int  map_w_     = 0;   // physical texture extent (REALITY_BUBBLE_SIZE_MAX*SEEX)
    int  map_h_     = 0;
    int  runtime_w_ = 0;   // last-uploaded runtime dimensions (≤ map_w_)
    int  runtime_h_ = 0;
    bool populated_ = false;
};

// CPU-side distance transform: Chebyshev BFS from all opaque tiles.
// transparency_flat: row-major float array, transparency_flat[x * h + y].
//   0.0f = opaque; >0.0f = open.
// Returns a row-major float array with the same indexing.
// Result[i] = distance in tiles to nearest opaque; 0.0f if the tile is opaque.
std::vector<float> compute_sdf_cpu( const float *transparency_flat, int w, int h );

} // namespace lighting
