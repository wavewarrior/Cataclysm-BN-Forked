#pragma once

#include <string>
#include <optional>
#include <vector>
#include <functional>

#include "sdl_wrappers.h"
#include "lighting/render_state.h"

#include <unordered_map>

class texture;

namespace detail
{
class texture_packer
{
    protected:
        SDL_Rect bounds;
        explicit texture_packer( const SDL_Rect &bounds ) : bounds( bounds ) {}
    public:
        virtual std::optional<SDL_Rect> pack( uint32_t w, uint32_t h ) = 0;
        virtual ~texture_packer() = 0;
};
} // namespace detail

using atlas_texture = std::pair<SDL_Texture_SharedPtr, SDL_Rect>;

class dynamic_atlas
{
    public:
        /// Master switch for the procedural normal atlas. When false, GPU
        /// pages are created single-height, nothing is generated or uploaded
        /// into a normal half, and normal_v_offset() returns 0.0f — which is
        /// the fragment shader's "feature disabled, keep using
        /// surface_normal()" encoding (contract C5). Flipping this one flag
        /// reverts the whole feature, VRAM cost included.
        static constexpr bool ENABLE_NORMAL_ATLAS = true;

        struct sprite_sheet {
            SDL_Texture_SharedPtr texture;
            // Phase 2i-B-5: GPU mirror of `texture`. Built alongside the
            // legacy SDL_Texture in allocate_sprite; uploaded into via
            // upload_surface_subregion_to_gpu_texture each time
            // copy_surface_to_dynamic_atlas stamps a tile into the legacy
            // atlas. Sampled by tile_batcher in cata_tiles' GPU draw
            // path. Lifetime tracked by the unique_ptr — released against
            // the live device on sheet destruction.
            lighting::gpu_texture_unique_ptr gpu_texture;
            std::unique_ptr<detail::texture_packer> packer;
            int atlas_width;
            /// Height of the COLOUR region, and the height of the legacy
            /// SDL_Texture / `readback` surface. The stripe packer is bounded
            /// by this, so no sprite rect can ever land below it.
            int atlas_height;
            /// Height of the GPU mirror only. `2 * atlas_height` when
            /// ENABLE_NORMAL_ATLAS (colour in the top half, each sprite's
            /// generated normal map at the SAME rect in the bottom half),
            /// otherwise equal to `atlas_height`. This — not `atlas_height` —
            /// is what find_gpu_texture_full reports, because it is the
            /// divisor the sprite UV math uses.
            int gpu_atlas_height;
            SDL_Surface_Ptr readback;
            bool dirty;
        };
        using sprite_callback = std::function<void( SDL_Surface *, const SDL_Rect * )>;

        dynamic_atlas()
            : max_atlas_width( 0 ), max_atlas_height( 0 ), hint_sprite_width( 0 ), hint_sprite_height( 0 ) {}
        dynamic_atlas( const int w, const int h, const int sw = 0, const int sh = 0 )
            : max_atlas_width( w ), max_atlas_height( h ), hint_sprite_width( sw ), hint_sprite_height( sh ) {}

        auto find_sprite( size_t id ) -> std::optional<atlas_texture>;
        auto create_sprite( int w, int h, const std::optional<size_t> &id,
                            const sprite_callback & ) -> atlas_texture;
        auto get_or_create_sprite( int w, int h, const std::optional<size_t> &id,
                                   const sprite_callback & ) -> atlas_texture;
        void clear();

        void readback_load();
        auto readback_find( const texture &tex ) -> std::tuple<bool, SDL_Surface *, SDL_Rect>;
        void readback_dump( const std::string &s ) const;
        void readback_clear();

        auto get_staging_area( int width,
                               int height ) -> std::tuple<SDL_Texture *, SDL_Surface *, SDL_Rect>;

        auto begin() const { return sheets.begin(); }
        auto end() const { return sheets.end(); }

        // Phase 2i-B-5: locate the GPU mirror of a legacy atlas texture.
        // Used by cata_tiles' GPU draw path to bind the correct
        // SDL_GPUTexture before issuing tile_batcher draws. Returns
        // nullptr if `legacy_tex` is not an atlas sheet of ours or if
        // the GPU mirror failed to allocate at sheet-creation time.
        SDL_GPUTexture *find_gpu_texture( SDL_Texture *legacy_tex ) const;

        /// Same as find_gpu_texture but also returns the atlas page's
        /// dimensions, which the caller needs to convert pixel-space
        /// srcrects into normalised UV for sprite_instance.
        ///
        /// `atlas_h` IS THE FULL GPU TEXTURE HEIGHT, INCLUDING THE NORMAL
        /// HALF. That is deliberate and load-bearing: every consumer divides
        /// `srcrect.y` by it (`texture::enqueue_tile_sprite`,
        /// `cata_tiles::push_occluder_footprint`) and occ_raster.comp.hlsl
        /// multiplies UV back by it. A colour texel at row `y` of a
        /// `2*colour_h` texture is at V = y / (2*colour_h), so reporting the
        /// colour height here would silently HALVE every colour V and every
        /// sprite would sample the wrong pixels. Avoiding exactly that mistake
        /// is why `gpu_atlas_height` exists as its own field.
        struct gpu_lookup { SDL_GPUTexture *texture; int atlas_w; int atlas_h; };
        gpu_lookup find_gpu_texture_full( SDL_Texture *legacy_tex ) const;

        /// Generate the normal map for one already-packed sprite and upload it
        /// into the normal half of `legacy_tex`'s GPU page, at
        /// `(rect.x, rect.y + atlas_height)`.
        ///
        /// `rect` is the atlas-space rect the colour pixels were stamped into
        /// (i.e. the second element of the atlas_texture handed out by
        /// create_sprite). The source pixels are read from the top-left
        /// `rect.w x rect.h` of `src`; use the 4-argument overload when the
        /// sprite sits at a non-zero offset inside a larger staging surface.
        ///
        /// Returns false — silently, so callers degrade to the stock
        /// surface_normal() path — when normals are disabled, the sheet is not
        /// ours, or it has no GPU mirror.
        auto upload_sprite_normal( SDL_Texture *legacy_tex, const SDL_Rect &rect,
                                   SDL_Surface *src ) -> bool;
        auto upload_sprite_normal( SDL_Texture *legacy_tex, const SDL_Rect &rect,
                                   SDL_Surface *src, const SDL_Rect &src_rect ) -> bool;

        /// Normalised V distance from a colour texel to its normal texel on
        /// `legacy_tex`'s GPU page: `atlas_height / gpu_atlas_height`, i.e.
        /// 0.5 for a double-height page. Feeds the fragment `nrm_atlas_v`
        /// uniform (contract C5), where 0.0f means "feature disabled".
        /// Returns 0.0f when normals are off, the sheet is unknown, or it has
        /// no GPU mirror.
        auto normal_v_offset( SDL_Texture *legacy_tex ) const -> float;

        /// Frame-global form of the above: the fragment `nrm_atlas_v` uniform is
        /// pushed once per frame rather than per segment, so it needs a single value
        /// for the whole atlas. Well defined because every page derives the same
        /// colour height, hence the same 0.5 ratio. 0.0f when normals are off or no
        /// page has a GPU mirror.
        auto normal_v_offset() const -> float;
    private:
        auto assign_id_internal( size_t id, const atlas_texture &tex ) -> bool;
        auto allocate_sprite_internal( int w, int h ) -> atlas_texture;
        std::vector<sprite_sheet> sheets;
        std::unordered_map<size_t, std::pair<int, SDL_Rect>> sprite_ids;
        SDL_Surface_Ptr staging_surf;
        SDL_Texture_Ptr staging_tex;

        int max_atlas_width;
        int max_atlas_height;
        int hint_sprite_width;
        int hint_sprite_height;
};

