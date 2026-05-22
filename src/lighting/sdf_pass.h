#pragma once
#include <cstdint>
#include <vector>

// Forward-declare SDL3 GPU types to avoid pulling in the full SDL header here.
struct SDL_GPUTexture;
struct SDL_GPUTransferBuffer;
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
    // transparency : map_w*map_h bytes (0=opaque, 255=transparent).
    // sdf          : map_w*map_h floats (tiles to nearest opaque).
    void upload( SDL_GPUCopyPass *cp,
                 SDL_GPUDevice   *dev,
                 const std::vector<uint8_t> &transparency,
                 const std::vector<float>   &sdf );

    SDL_GPUTexture *transparency_texture() const noexcept { return transparency_tex_; }
    SDL_GPUTexture *sdf_texture()          const noexcept { return sdf_tex_; }

    bool ready() const noexcept { return sdf_tex_ != nullptr; }

    int map_w() const noexcept { return map_w_; }
    int map_h() const noexcept { return map_h_; }

private:
    SDL_GPUTexture        *transparency_tex_ = nullptr;
    SDL_GPUTexture        *sdf_tex_          = nullptr;
    SDL_GPUTransferBuffer *xfer_transparency_ = nullptr;
    SDL_GPUTransferBuffer *xfer_sdf_          = nullptr;
    int map_w_ = 0;
    int map_h_ = 0;
};

// CPU-side distance transform: Chebyshev BFS from all opaque tiles.
// transparency_flat: row-major float array, transparency_flat[x * h + y].
//   0.0f = opaque; >0.0f = open.
// Returns a row-major float array with the same indexing.
// Result[i] = distance in tiles to nearest opaque; 0.0f if the tile is opaque.
std::vector<float> compute_sdf_cpu( const float *transparency_flat, int w, int h );

} // namespace lighting
