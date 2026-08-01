#pragma once

// Per-frame capture of the screen-space sprite footprints whose ALPHA defines the
// ground occluders, so the SDF seed can be rasterised from the tileset artwork
// instead of from one binary flag per tile.
//
// Why this exists: jfa_seed.comp used to read a single opaque/open bit per tile
// and replicate it into all SDF_SUPERSAMPLE^2 subcells, so every tree, barrel,
// car and fence cast a tile-square, axis-aligned shadow while paying 64x the
// memory and flood cost to carry no sub-tile detail at all. The artwork already
// holds the exact silhouette; this class ferries it to the GPU.
//
// Lifetime: begin() at the top of cata_tiles::draw() (before any sprite is
// enqueued), push() from cata_tiles::push_occluder_footprint during the
// terrain / furniture / vpart draw layers only, drained by gpu_sdf_pass during
// the lighting build later in the same frame. The tile sprite queue is fully
// populated before refresh_display runs the lighting pass, so the list is
// complete by the time it is consumed.
//
// Deliberately EXCLUDED: items, creatures, fields, overlays. The SDF rebuild is
// gated on transparency_generation / camera-origin change, and per-frame creature
// motion would force a rebuild every single frame. Creature sun shadows already
// come from the screen-space silhouette pass (shadow.vert / shadow.frag).
#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <vector>

namespace lighting
{

/// One sprite's footprint over ONE tile. Wire-stable with OccQuad in
/// occ_raster.comp.hlsl (12 floats, stride 48).
///
/// The quad is described the way sprite.vert draws it — centre, size and rotation
/// — rather than as a precomputed UV band, because single-sprite terrain that
/// `rotates` is enqueued with a real -90/180/90 degree rotation applied about the
/// quad centre in the VERTEX shader. A precomputed axis-aligned band would sample
/// the unrotated silhouette and a rotated fence/wall/hull would seed the wrong
/// shape. occ_raster inverts this transform per subcell instead, which also gets
/// tall sprites, tile_type::offset and depth extrusion right for free.
struct occluder_quad {          // 48 bytes
    float u0, v0, uw, vh;       // FULL sprite atlas rect, normalised, flip folded in
    float tile_x, tile_y;       // bubble-local tile this footprint is seeded into
    float cx, cy;               // quad centre minus tile screen origin, in TILE units
    float sw, sh;               // quad size in TILE units
    float rot;                  // quad rotation, radians, screen-clockwise (sprite.vert)
    float block;                // 0..1 opacity multiplier from the transparency cache
};
static_assert( sizeof( occluder_quad ) == 48, "occluder_quad wire-stable with OccQuad" );

/// CPU-side companion to occluder_quad, parallel by index. The atlas page a quad
/// samples cannot live in the GPU struct: one compute dispatch reads one texture,
/// so gpu_sdf_pass sorts the quads by page and issues one dispatch per run.
struct occluder_page {
    SDL_GPUTexture *tex = nullptr;
    int atlas_w = 0;
    int atlas_h = 0;
    auto operator<=>( const occluder_page & ) const = default; // *NOPAD*
};

/// Per-frame accumulator. Cleared by begin(), filled from cata_tiles' terrain/
/// furniture/vpart draws, drained by gpu_sdf_pass.
class occluder_capture
{
    public:
        /// Drop last frame's quads and clear the captured mask. Cheap: the quad vector
        /// keeps its capacity and the mask is only memset over the live tile area.
        auto begin() -> void;
        /// Record one footprint and mark its tile as captured. Out-of-range tiles and
        /// quads with no GPU atlas page are dropped.
        auto push( const occluder_quad &q, const occluder_page &page ) -> void;
        auto quads() const -> const std::vector<occluder_quad> & { return quads_; } // *NOPAD*
        /// Parallel to quads(): the atlas page each quad samples.
        auto pages() const -> const std::vector<occluder_page> & { return pages_; } // *NOPAD*
        /// Tile-res mask: 1 where at least one quad was captured, else 0. x-major
        /// (mask[x * h + y]), matching the TransBuf / SdfBuf layout. occ_base.comp
        /// uses it to fall back to the tile-square seed for tiles nothing drew —
        /// that is what keeps off-camera walls casting their shadows into view.
        auto captured_mask() const -> const std::vector<std::uint8_t> & { return captured_; } // *NOPAD*
        auto resize( int w, int h ) -> void;
        auto width() const -> int { return w_; }
        auto height() const -> int { return h_; }

    private:
        std::vector<occluder_quad> quads_;
        std::vector<occluder_page> pages_;
        std::vector<std::uint8_t> captured_;
        int w_ = 0;
        int h_ = 0;
};

} // namespace lighting
