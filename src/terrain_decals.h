#pragma once

// Large multi-tile terrain decals — cosmetic overlays that break up visual
// monotony in clusters of the same terrain type (e.g. grass).
//
// Deterministic, coordinate-seeded placement computed lazily at render time.
// No per-tile storage, no save format changes, no gameplay effect.
//
// Game-side API — the rendering integration lives in sdl_render_frame.cpp.
// Stateless free functions mirror splatmap_stamps.h; the manager class mirrors
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "coordinates.h"
#include "type_id.h"

struct SDL_GPUTexture;
/// One placed decal, anchored to a submap-local tile.
/// The tile is kept as an exact index (not folded into a float) so the renderer
/// can test that tile's visibility before drawing — a decal must never paint
/// over unexplored black space.
struct terrain_decal_instance {
    std::uint8_t tile_x = 0, tile_y = 0; // submap-local tile (0..SEEX-1 / 0..SEEY-1)
    float off_x = 0.f, off_y = 0.f;      // sub-tile centre offset, in tiles
    float rotation = 0.f;                // radians
    int variant = 0;                     // atlas variant index (0..N-1)
};

/// One decal resolved for the current frame, in logical projection pixels.
/// Built by cata_tiles once visibility and tile size are known; consumed by the
/// render pass.
struct terrain_decal_draw {
    float dst_x = 0.f, dst_y = 0.f; // top-left of the sprite rect
    float dst_w = 0.f, dst_h = 0.f; // size, already scaled to the current zoom
    float rotation = 0.f;
    int variant = 0;
};

/// Per-terrain-type decal configuration (parsed from terrain JSON).
struct terrain_decal_config {
    std::string group;           // atlas group name (e.g. "grass")
    float density = 0.12f;       // probability per eligible tile (0..1)
    int min_spacing = 2;         // minimum tiles between decal centres
};

/// One packed sprite in the atlas: UV rect plus its footprint measured in TILES.
/// Size is deliberately not stored in pixels — the sprite must scale with the
/// tileset's tile size and the player's zoom, not with the display's DPI.
struct decal_variant_info {
    float u = 0.f, v = 0.f, uw = 0.f, vh = 0.f;
    float w_tiles = 0.f, h_tiles = 0.f;
};

namespace lighting { class gpu_device; }

namespace terrain_decals
{

using lighting::gpu_device;

class manager
{
    public:
        manager() = default;
        manager( const manager & ) = delete;
        manager &operator=( const manager & ) = delete; // *NOPAD*
        ~manager();

        auto init( gpu_device &dev ) -> bool;
        auto shutdown() noexcept -> void;
        auto ready() const noexcept -> bool;

        // Atlas access.
        auto variant_count() const noexcept -> int;
        auto atlas() const noexcept -> const SDL_GPUTexture *;
        auto variants() const noexcept -> const std::vector<decal_variant_info> &;

        // Compute decal placements for a submap based on current terrain layout.
        // Deterministic from absolute submap coordinates. Result cached per key.
        // `ter_at` callback returns ter_id for submap-local tile (x,y).
        auto compute_placements( std::uint64_t key,
                                 const std::function<ter_id( int, int )> &ter_at )
        -> const std::vector<terrain_decal_instance> &;

        // Invalidate cached placements for a submap (called on terrain change).
        auto invalidate( std::uint64_t key ) -> void;

        // Clear all caches (e.g. on dimension change).
        auto clear() -> void;

    private:
        auto load_atlas( gpu_device &dev ) -> bool;

        gpu_device *dev_ = nullptr;
        SDL_GPUTexture *atlas_ = nullptr;
        std::vector<decal_variant_info> variants_;

        // Per-submap placement cache.
        struct cache_entry {
            std::uint64_t key = 0;
            std::vector<terrain_decal_instance> placements;
        };
        std::vector<cache_entry> cache_;
};

} // namespace terrain_decals