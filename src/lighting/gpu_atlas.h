#pragma once

// SDL_GPUTexture-backed sprite atlas — phase 2e of the lighting rework.
//
// Parallel to the legacy `dynamic_atlas` (src/dynamic_atlas.h). The old type
// renders sprites *into* SDL_Renderer textures via render-target draws; this
// type expects callers to hand it a CPU-side SDL_Surface and uploads the
// pixels through an SDL_GPU transfer buffer + copy pass.
//
// Phase 2e leaves `dynamic_atlas` untouched. Both atlases live side by side
// until sub-phase 2i flips the renderer and deletes the legacy path. The
// returned slot format (`gpu_atlas_slot`) is intentionally aligned with
// `lighting::sprite_instance`'s `src_u/v/uw/vh` fields so the cata_tiles
// migration in phase 2i is a 1:1 substitution.

#include "gpu_device.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

struct SDL_Surface;

namespace lighting {

// Reference into a packed atlas page. UVs are pre-normalised to [0..1] so the
// sprite_batcher can drop them straight into a `sprite_instance`.
struct gpu_atlas_slot {
    SDL_GPUTexture* page = nullptr;
    std::uint16_t page_index = 0;
    std::uint16_t px_x = 0;
    std::uint16_t px_y = 0;
    std::uint16_t px_w = 0;
    std::uint16_t px_h = 0;
    float u = 0.0f;
    float v = 0.0f;
    float uw = 0.0f;
    float vh = 0.0f;

    explicit operator bool() const noexcept { return page != nullptr; }
};

class gpu_atlas_impl;

class gpu_atlas {
public:
    // `page_w` / `page_h` — desired atlas page dimensions. The actual page
    // size is `min(page_*, SDL_GPU_TEXTURE_DIMENSION_MAX)`. `min_sprite_*`
    // is the stripe-packer's stripe height granularity — set to the most
    // common sprite size (typically the tileset's tile height) so the
    // packer wastes minimal space.
    gpu_atlas(int page_w, int page_h, int min_sprite_w, int min_sprite_h);
    gpu_atlas(const gpu_atlas&) = delete;
    gpu_atlas& operator=(const gpu_atlas&) = delete;
    gpu_atlas(gpu_atlas&&) noexcept;
    gpu_atlas& operator=(gpu_atlas&&) noexcept;
    ~gpu_atlas();

    // Bind to a device. Must be called before any allocate / upload.
    void init(gpu_device& dev);

    // Tear down all pages.
    void shutdown() noexcept;

    // Reserve a `w × h` region on an existing or freshly-created page.
    // Returns an unpopulated slot — caller must follow up with
    // `upload_surface()` to actually push pixels into it.
    gpu_atlas_slot allocate(int w, int h);

    // Convenience: allocate + upload in one call. `surf` must be in an
    // RGBA-equivalent format (SDL_PIXELFORMAT_RGBA32 / ABGR8888);
    // upload_surface() will format-convert if necessary. The upload is
    // appended to `cb`'s next copy pass.
    gpu_atlas_slot upload(SDL_GPUCommandBuffer* cb, SDL_Surface* surf);

    // Push surface pixels into an already-allocated slot.
    bool upload_surface(SDL_GPUCommandBuffer* cb, const gpu_atlas_slot& slot, SDL_Surface* surf);

    // Cache + lookup helpers, mirroring dynamic_atlas::id_*.
    bool cache_assign(std::size_t id, const gpu_atlas_slot& slot);
    const gpu_atlas_slot* cache_lookup(std::size_t id) const noexcept;

    // Release every page (so the atlas can be repopulated from a new
    // tileset without destroying / recreating the wrapper).
    void clear();

    std::size_t page_count() const noexcept;

private:
    std::unique_ptr<gpu_atlas_impl> p;
};

} // namespace lighting
