#pragma once

// World-locked GPU splatmap — persistent sub-tile decal accumulation.
//
// Each visited submap owns a small RGBA8 texture (SPLAT_TEX_DIM²) that decal
// stamps are rendered into and never cleared again. Stamps are rotated/scaled
// quads in SUBMAP-LOCAL splatmap pixels, so a splatter straddles tile
// boundaries instead of snapping to a 32×32 grid cell. Each frame the visible
// submaps' textures are alpha-composited over the terrain half of Pass W and
// under the entity half, which is what puts decals below creatures and items.
//
// This is the world-locked replacement for the screen-space "splat map"
// accumulator that rain_effect used to carry and removed (see rain_effect.h:12
// and RAIN_EFFECT_LEARNINGS.md): a screen-space accumulator smears across the
// terrain when the world scrolls, a per-submap one cannot.
//
// Game-agnostic, like rain_effect: no game headers. Submaps are addressed by an
// opaque packed key the caller mints (see splatmap_stamps.h).

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>

#include "lighting/ui_composite_target.h"

namespace lighting
{

class gpu_device;

// Splatmap resolution. 96x96 per 12x12-tile submap; bump to 16 for 192x192 if
// 8 px/tile reads mushy (single-constant change; texture size derives from it).
inline constexpr int SPLAT_PX_PER_TILE = 8;
inline constexpr int SPLAT_TEX_DIM = SPLAT_PX_PER_TILE * 12; // SEEX == 12

// Stamp atlas geometry. 8x8 cells of ATLAS_CELL px — hard cap 64 stamps.
inline constexpr int SPLAT_ATLAS_COLS = 8;
inline constexpr int SPLAT_ATLAS_CELLS = SPLAT_ATLAS_COLS * SPLAT_ATLAS_COLS;

// One decal stamp, in SUBMAP-LOCAL splatmap pixels.
struct splat_stamp {
    float px = 0.f, py = 0.f;   // centre, 0..SPLAT_TEX_DIM
    float size = 8.f;           // quad edge in splatmap px
    float rotation = 0.f;       // radians
    float alpha = 1.f;          // 0..1 coverage multiplier
    float mask_r = 1.f, mask_g = 0.f, mask_b = 0.f; // target channel mask
    int cell = 0;               // atlas cell index, 0..63
};

// One visible submap's composite quad, in LOGICAL projection pixels.
struct splat_quad {
    float dst_x = 0.f, dst_y = 0.f, dst_w = 0.f, dst_h = 0.f;
    std::uint64_t key = 0;
};

// Composite tuning, driven from the lighting devui (same pattern as
// bloom/volumetric: the pass never reads devui globals itself).
struct splat_colors {
    float blood_r = 0.30f, blood_g = 0.02f, blood_b = 0.02f;
    float blood_strength = 0.85f;
    float wet_strength = 0.0f;
    float snow_strength = 0.0f;
};

class splatmap_pass
{
    public:
        splatmap_pass() = default;
        splatmap_pass( const splatmap_pass & ) = delete;
        splatmap_pass &operator=( const splatmap_pass & ) = delete; // *NOPAD*
        ~splatmap_pass();

        auto init( gpu_device &dev, SDL_GPUTextureFormat world_format ) -> bool;
        auto shutdown() noexcept -> void;
        auto ready() const noexcept -> bool;

        // Number of atlas cells actually loaded per group (for stamp selection).
        auto cell_count( bool gibs ) const noexcept -> int;
        auto cell_base( bool gibs ) const noexcept -> int;

        // Look up / create the submap entry. Returns true when the entry ALREADY
        // existed; false when freshly created (caller must then seed history).
        auto touch( std::uint64_t key ) -> bool;
        auto add_stamp( std::uint64_t key, const splat_stamp &s ) -> void;

        // Render pending stamps into their submap textures. MUST be called with NO
        // render pass open.
        auto flush_stamps( SDL_GPUCommandBuffer *cb ) -> void;

        // Alpha-composite the given submap quads over world_tex. Opens its own
        // LOADOP_LOAD pass; call between the two halves of the split Pass W.
        auto composite(
            SDL_GPUCommandBuffer *cb, SDL_GPUTexture *world_tex, std::uint32_t proj_w,
            std::uint32_t proj_h, const std::vector<splat_quad> &quads,
            const splat_colors &colors ) -> void;

    private:
        // One cached submap's persistent decal texture plus its unflushed stamps.
        struct entry {
            std::uint64_t key = 0;
            ui_composite_target tex;
            std::vector<splat_stamp> pending;
            // False until the texture has been rendered into at least once, so the
            // first stamp pass uses LOADOP_CLEAR (reading an uninitialised colour
            // target is undefined) and every later pass uses LOADOP_LOAD.
            bool cleared = false;
        };

        // LRU cache: front == most recently touched. ~36 KB per entry at 96x96.
        static constexpr std::size_t MAX_SUBMAPS = 64;
        // Instance cap per flush; overflow stays pending for the next frame.
        static constexpr std::size_t MAX_STAMPS_PER_FLUSH = 1024;

        auto load_atlas() -> bool;
        auto find( std::uint64_t key ) -> entry *; // *NOPAD*

        gpu_device *dev_ = nullptr;
        SDL_GPUTextureFormat world_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;

        // Stamp pass: atlas quads into a submap texture (R8G8B8A8, MAX blend).
        SDL_GPUShader *stamp_vert_ = nullptr;
        SDL_GPUShader *stamp_frag_ = nullptr;
        SDL_GPUGraphicsPipeline *stamp_pipeline_ = nullptr;
        SDL_GPUTransferBuffer *stamp_xfer_ = nullptr;
        SDL_GPUBuffer *stamp_storage_ = nullptr;

        // Composite pass: submap texture over the HDR world target.
        SDL_GPUShader *composite_vert_ = nullptr;
        SDL_GPUShader *composite_frag_ = nullptr;
        SDL_GPUGraphicsPipeline *composite_pipeline_ = nullptr;

        // Stamp atlas (SPLAT_ATLAS_COLS² cells of `cell_size` px, RGBA8,
        // SAMPLER-only) + its linear sampler. LINEAR filtering is what keeps
        // 8 px/tile from reading as hard blocks; the same sampler serves the
        // atlas read and the composite read.
        SDL_GPUTexture *atlas_ = nullptr;
        SDL_GPUSampler *linear_ = nullptr;
        int atlas_dim_ = 0; // full atlas edge in px, = SPLAT_ATLAS_COLS * cell_size
        int blood_cells_ = 0;
        int gibs_cells_ = 0;

        std::list<entry> cache_;
        std::unordered_map<std::uint64_t, std::list<entry>::iterator> index_;
};

} // namespace lighting
