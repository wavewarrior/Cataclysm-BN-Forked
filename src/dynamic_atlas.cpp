#include "dynamic_atlas.h"

#include "cata_tiles.h"

#include <cassert>
#include <format>
#include <ranges>
#include <stack>
#include <string_view>
#include <utility>
#include <vector>

#include "debug.h"
#include "lighting/normal_gen.h"
#include "sdl_utils.h"
#include "sdltiles.h"

#define dbg(x) DebugLogFL((x), DC::SDL)

detail::texture_packer::~texture_packer() = default;

template<typename T>
static T round_up( T n, T m )
{
    if( m == 0 ) {
        return n;
    }
    return ( ( n + m - 1 ) / m ) * m;
}

struct stripe_texture_packer final : detail::texture_packer {

    struct stripe {
        uint32_t height;
        uint32_t y_offset;
        uint32_t x_remainder;
    };

    std::vector<stripe> stripes;
    uint32_t y_remainder;
    uint32_t min_size;

    stripe_texture_packer( const SDL_Rect &bounds, const uint32_t min_size )
        : texture_packer( bounds ), y_remainder( bounds.h ), min_size( min_size ) {}

    std::optional<SDL_Rect> pack( const uint32_t width,
                                  const uint32_t height ) override {

        if( std::cmp_greater( width, bounds.w ) || std::cmp_greater( height, bounds.h ) ) {
        return std::nullopt;
    }

    const auto r_height = round_up( height, min_size );

    auto it = std::ranges::find_if( stripes, [&]( const stripe & s ) {
        return s.x_remainder >= width && s.height == r_height;
    } );

    if( it == stripes.end() ) {
        if( r_height > y_remainder || y_remainder < min_size ) {
                return std::nullopt;
            }

            const auto line_height = r_height;
            const auto y_offset = bounds.h - y_remainder;
            const auto x_remainder = static_cast<uint32_t>( bounds.w );

            it = stripes.emplace( stripes.end(), stripe{
                line_height,
                y_offset,
                x_remainder,
            } );
            y_remainder -= line_height;
        }

        auto &s = *it;

        const auto x_offset = bounds.w - s.x_remainder;
        SDL_Rect rect{
            static_cast<int>( bounds.x + x_offset ),
            static_cast<int>( bounds.y + s.y_offset ),
            static_cast<int>( width ),
            static_cast<int>( height )
        };

        s.x_remainder -= width;
        if( s.x_remainder < min_size ) {
        s.x_remainder = 0;
    }

    return rect;
}
};

struct null_texture_packer final : detail::texture_packer {

bool has_contents;

explicit null_texture_packer( const SDL_Rect &bounds )
        : texture_packer( bounds )
        , has_contents( false ) {
    }

    std::optional<SDL_Rect> pack( const uint32_t width, const uint32_t height ) override {
        if( has_contents
        || std::cmp_greater( width, bounds.w )
        || std::cmp_greater( height, bounds.h ) ) {
        return std::nullopt;
    }
    has_contents = true;
    return bounds;
};
};

auto dynamic_atlas::get_staging_area(
    const int width, const int height ) -> std::tuple<SDL_Texture *, SDL_Surface *, SDL_Rect>
{
    const auto r_width = round_up( width, hint_sprite_width );
    const auto r_height = round_up( height, hint_sprite_height );

    if( staging_surf == nullptr || staging_surf->w < r_width ||
        staging_surf->h < r_height ) {
        const auto &r = get_sdl_renderer();
        staging_surf = create_surface_32( r_width, r_height );
        staging_tex = CreateTexture( r, sdl_color_pixel_format,
                                     SDL_TEXTUREACCESS_TARGET, r_width, r_height );
        SDL_SetTextureBlendMode( staging_tex.get(), SDL_BLENDMODE_NONE );
        SDL_SetSurfaceBlendMode( staging_surf.get(), SDL_BLENDMODE_NONE );
    }

    return std::make_tuple( staging_tex.get(), staging_surf.get(),
                            SDL_Rect{0, 0, width, height} );
}

auto dynamic_atlas::assign_id_internal( const size_t id, const atlas_texture &tex ) -> bool
{
    const auto it = std::ranges::find_if( sheets, [&]( const sprite_sheet & s ) {
        return s.texture.get() == std::get<0>( tex ).get();
    } );
    if( it == sheets.end() ) {
        return false;
    }
    int sheet_index = std::distance( sheets.begin(), it );

    auto [iter, ok] = sprite_ids.emplace( id, std::make_pair( sheet_index, std::get<1>( tex ) ) );
    return ok;
}

auto dynamic_atlas::find_sprite( const size_t id ) -> std::optional<atlas_texture>
{
    const auto it = sprite_ids.find( id );
    if( it == sprite_ids.end() ) {
        return std::nullopt;
    }

    auto [sheet_idx, rect] = it->second;

    return atlas_texture{sheets[sheet_idx].texture, rect};
}

void dynamic_atlas::readback_load()
{
    const auto &r = get_sdl_renderer();
    const auto state = sdl_save_render_state( r.get() );
    for( auto &it : sheets ) {
        if( it.dirty ) {
            auto tmpTex = CreateTexture( r, sdl_color_pixel_format, SDL_TEXTUREACCESS_TARGET, it.atlas_width,
                                         it.atlas_height );
            SDL_SetRenderTarget( r.get(), tmpTex.get() );
            SDL_RenderTexture( r.get(), it.texture.get(), nullptr, nullptr );
            // SDL3: SDL_RenderReadPixels returns a new surface owned by us.
            it.readback.reset( SDL_RenderReadPixels( r.get(), nullptr ) );
            it.dirty = false;
        }
    }
    sdl_restore_render_state( r.get(), state );
}

void dynamic_atlas::readback_clear()
{
    for( auto &it : sheets ) {
        it.readback.reset();
        it.dirty = true;
    }
}

auto dynamic_atlas::readback_find( const texture &tex ) -> std::tuple<bool, SDL_Surface *, SDL_Rect>
{
    const auto it = std::ranges::find_if( sheets, [&]( const sprite_sheet & s ) {
        return s.texture == tex.sdl_texture_ptr;
    } );

    return ( it == sheets.end() )
           ? std::make_tuple( false, nullptr, SDL_Rect{} )
    : std::make_tuple( true, it->readback.get(), SDL_Rect{
        static_cast<int>( tex.srcrect.x ), static_cast<int>( tex.srcrect.y ),
        static_cast<int>( tex.srcrect.w ), static_cast<int>( tex.srcrect.h )
    } );
}

auto dynamic_atlas::get_or_create_sprite(
    const int w, const int h,
    const std::optional<size_t> &id,
    const sprite_callback &cb ) -> atlas_texture
{
    const auto existing = id.has_value() ? find_sprite( id.value() ) : std::nullopt;
    if( existing.has_value() ) {
        return existing.value();
    }
    return create_sprite( w, h, id, cb );
}

auto dynamic_atlas::create_sprite(
    const int w, const int h,
    const std::optional<size_t> &id,
    const sprite_callback &blitFn ) -> atlas_texture
{
    // TODO: Update sprite instead of allocating a new one if ID already exists?
    auto atl_tex = allocate_sprite_internal( w, h );
    if( id.has_value() && !this->assign_id_internal( id.value(), atl_tex ) ) {
        debugmsg( "Duplicate sprite ID in atlas: %x", id.value() );
    }
    auto& [tex, rect] = atl_tex;

    SDL_Surface *tmpSurf{};
    if( SDL_LockTextureToSurface( tex.get(), &rect, &tmpSurf ) ) {
        const auto tmpRect = SDL_Rect{0, 0, w, h};
        blitFn( tmpSurf, &tmpRect );
        SDL_UnlockTexture( tex.get() );
    } else {
        debugmsg( "Failed to lock dynamic atlas texture for writing." );
    }

    return atl_tex;
}

atlas_texture dynamic_atlas::allocate_sprite_internal( const int w, const int h )
{
    constexpr auto get_texture = []( const SDL_Texture_SharedPtr & tex, const SDL_Rect & r,
    const int actual_w, const int actual_h ) {
        assert( actual_w <= r.w && actual_h <= r.h );
        SDL_Rect r2 = {
            r.x, r.y, actual_w, actual_h
        };
        return atlas_texture{tex, r2};
    };

    for( auto &s : sheets ) {
        auto p = s.packer->pack( w, h );
        if( p.has_value() ) {
            s.dirty = true;
            return get_texture( s.texture, p.value(), w, h );
        }
    }

    const auto &r = get_sdl_renderer();
    const bool is_software = ( std::string_view( SDL_GetRendererName( r.get() ) ) == "software" );
    int tex_width;
    // Height of the COLOUR region. Everything CPU-side — the stripe packer
    // bounds, the legacy SDL_Texture, `readback` — is sized to this, so the
    // packer structurally cannot hand out a rect inside the normal half.
    int colour_h;

    std::unique_ptr<detail::texture_packer> packer;
    if( is_software ) {
        tex_width = w;
        colour_h = h;
        packer = std::make_unique<null_texture_packer>(
                     SDL_Rect{0, 0, tex_width, colour_h}
                 );
    } else {
        const auto props = SDL_GetRendererProperties( r.get() );
        const int max_tex = static_cast<int>(
                                SDL_GetNumberProperty( props, SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 4096 ) );
        tex_width = std::min( max_atlas_width, max_tex );
        // Contract C4: the device limit is respected by halving the COLOUR
        // budget, never by exceeding `max_tex` to win back the other half.
        // A page taller than the device max simply cannot be created.
        const int height_cap = ENABLE_NORMAL_ATLAS ? max_tex / 2 : max_tex;
        colour_h = std::min( max_atlas_height, height_cap );
        packer = std::make_unique<stripe_texture_packer>(
                     SDL_Rect{0, 0, tex_width, colour_h},
                     hint_sprite_width
                 );
    }

    assert( w <= tex_width && h <= colour_h );

    const auto tex = SDL_CreateTexture( r.get(), sdl_color_pixel_format, SDL_TEXTUREACCESS_STREAMING,
                                        tex_width, colour_h );
    SDL_SetTextureBlendMode( tex, SDL_BLENDMODE_BLEND );
    SDL_SetTextureScaleMode( tex, SDL_SCALEMODE_NEAREST );

    // Upstream switched the page to SDL_TEXTUREACCESS_STREAMING so create_sprite can
    // write through SDL_LockTextureToSurface; a streaming texture cannot be a render
    // target, so the old clear-to-transparent render pass is gone. Every locked rect is
    // fully overwritten by the blit callback, so no consumer reads an unwritten texel.

    // Contract C4: only the GPU mirror doubles. Colour occupies rows
    // [0, colour_h), the generated normal for the sprite at `rect` lives at
    // `rect.y + colour_h`.
    const int gpu_h = ENABLE_NORMAL_ATLAS ? 2 * colour_h : colour_h;

    sprite_sheet s;
    s.texture           = SDL_Texture_SharedPtr( SDL_Texture_Ptr( tex ) );
    s.packer            = std::move( packer );
    s.atlas_width       = tex_width;
    s.atlas_height      = colour_h;
    s.gpu_atlas_height  = gpu_h;
    s.readback          = nullptr;
    s.dirty             = true;

    // Phase 2i-B-5: allocate the GPU mirror. Soft-fail to nullptr if the
    // device isn't ready; cata_tiles' GPU draw path will fall back to legacy
    // SDL_RenderTexture when find_gpu_texture returns null.
    auto &rs = lighting::get_render_state();
    if( rs.ready() ) {
        SDL_GPUTexture *gpu = rs.create_rgba_gpu_texture( tex_width, gpu_h );
        if( gpu ) {
            s.gpu_texture.reset( gpu );
            // Neutralise the ENTIRE normal half once, up front. The per-sprite
            // prefill inside upload_sprite_normal() only covers texels a sprite
            // actually wrote, so without this every normal texel not reached by a
            // colour/normal pairing stays uninitialised VRAM — and the fragment
            // shader samples it as an arbitrary normal with an arbitrary blend
            // weight. Doing it here makes safety STRUCTURAL: the three pairings in
            // cata_tiles_tileset.cpp become an optimisation that adds real relief,
            // not a correctness requirement whose omission corrupts lighting.
            // Cost is one transient fill per page at load, never per frame.
            if( ENABLE_NORMAL_ATLAS && gpu_h > colour_h ) {
                const SDL_Surface_Ptr neutral = create_surface_32( tex_width, colour_h );
                if( neutral ) {
                    SDL_FillSurfaceRect(
                        neutral.get(), nullptr,
                        SDL_MapRGBA( SDL_GetPixelFormatDetails( neutral->format ), nullptr,
                                     128, 128, 0, 255 ) );
                    rs.upload_surface_subregion_to_gpu_texture(
                        gpu, 0, colour_h, neutral.get(), nullptr );
                }
            }
        } else {
            // No mirror: normal_v_offset() will report 0.0f for this sheet,
            // which the fragment shader reads as "feature disabled".
            s.gpu_atlas_height = colour_h;
        }
    } else {
        s.gpu_atlas_height = colour_h;
    }

    dbg( DL::Info ) << "dynamic_atlas: new page colour " << tex_width << "x" << colour_h
                    << ", GPU mirror " << tex_width << "x" << s.gpu_atlas_height
                    << ( s.gpu_texture ? "" : " (none)" )
                    << ", normals " << ( ENABLE_NORMAL_ATLAS ? "on" : "off" );

    const auto &entry = sheets.emplace_back( std::move( s ) );

    const auto rect = entry.packer->pack( w, h );
    // The invariant the whole double-height design rests on: the packer is
    // bounded by `colour_h`, so a sprite rect can never land in the normal
    // half. If this ever fires, colour and normals have started to overlap.
    assert( rect.value().y + rect.value().h <= entry.atlas_height );

    return get_texture( entry.texture, rect.value(), w, h );
}

SDL_GPUTexture *dynamic_atlas::find_gpu_texture( SDL_Texture *legacy_tex ) const
{
    if( !legacy_tex ) {
    return nullptr;
}
for( const auto &s : sheets ) {
    if( s.texture.get() == legacy_tex ) {
            return s.gpu_texture.get();
        }
    }
    return nullptr;
}

dynamic_atlas::gpu_lookup dynamic_atlas::find_gpu_texture_full( SDL_Texture *legacy_tex ) const
{
    if( !legacy_tex ) {
    return { nullptr, 0, 0 };
}
const auto it = std::ranges::find_if( sheets, [legacy_tex]( const sprite_sheet & s ) {
        return s.texture.get() == legacy_tex;
    } );
    if( it == sheets.end() ) {
        return { nullptr, 0, 0 };
    }
    // `gpu_atlas_height`, NOT `atlas_height`. This is the divisor every
    // consumer uses to turn a pixel srcrect into UV, so it must describe the
    // texture actually bound to the sampler. Returning the colour height here
    // would halve every colour V and every sprite would sample the wrong
    // pixels — the exact trap the separate field exists to prevent.
    return { it->gpu_texture.get(), it->atlas_width, it->gpu_atlas_height };
}

auto dynamic_atlas::upload_sprite_normal( SDL_Texture *legacy_tex, const SDL_Rect &rect,
        SDL_Surface *src ) -> bool
{
    return upload_sprite_normal( legacy_tex, rect, src, SDL_Rect{ 0, 0, rect.w, rect.h } );
}

auto dynamic_atlas::upload_sprite_normal( SDL_Texture *legacy_tex, const SDL_Rect &rect,
        SDL_Surface *src, const SDL_Rect &src_rect ) -> bool
{
    if( !ENABLE_NORMAL_ATLAS || !legacy_tex || !src || rect.w <= 0 || rect.h <= 0 ) {
        return false;
    }
    // The sprite is the same size in the source surface and in the atlas; only
    // its origin differs. Anything else means the caller mismatched the pair,
    // and generate_sprite_normal would write outside `nrm`.
    if( src_rect.w != rect.w || src_rect.h != rect.h
        || src_rect.x < 0 || src_rect.y < 0
        || src_rect.x + src_rect.w > src->w || src_rect.y + src_rect.h > src->h ) {
        return false;
    }
    const auto it = std::ranges::find_if( sheets, [legacy_tex]( const sprite_sheet & s ) {
        return s.texture.get() == legacy_tex;
    } );
    // No mirror (or a single-height one, i.e. the GPU texture failed to
    // allocate at page-creation time) => there is no normal half to write to.
    if( it == sheets.end() || !it->gpu_texture || it->gpu_atlas_height <= it->atlas_height ) {
        return false;
    }

    // The colour rect must sit inside the colour half, or `rect.y +
    // atlas_height` would run past the bottom of the GPU page. This holds
    // because the packer is bounded by the colour height (see the matching
    // assert in allocate_sprite); enforce it here too, since this is the call
    // that actually writes below the colour region.
    if( rect.x + rect.w > it->atlas_width || rect.y + rect.h > it->atlas_height ) {
        dbg( DL::Warn ) << "normal atlas: rect " << rect.x << "," << rect.y << " "
                        << rect.w << "x" << rect.h << " escapes the colour half ("
                        << it->atlas_width << "x" << it->atlas_height << ")";
        return false;
    }

    // create_surface_32 is RGBA32, which is what generate_sprite_normal
    // requires and what upload_surface_subregion_to_gpu_texture consumes
    // without a format conversion.
    const SDL_Surface_Ptr nrm = create_surface_32( rect.w, rect.h );
    if( !nrm ) {
        return false;
    }
    // Prefill the neutral texel per contract C3. NOTE: this is deliberately NOT
    // the usual (128,128,255) tangent-space convention, and it is NOT what the
    // prototype's contact-sheet encode() (tools/normal_gen_proto/nrmproto.py:251)
    // writes -- that one is a visualisation, not the wire format.
    //
    // C3 stores no z at all: R/G are nx/ny biased to 0.5, and B is the
    // coherence-gate amplitude, which the shader uses as the BLEND WEIGHT while
    // reconstructing n.z from nx/ny. That reconstruction is lossless because
    // nrmproto.py:244-245 normalises n to unit length with nz > 0, which is
    // precisely why B is free to carry the gate scalar (nrmproto.py:239).
    // So neutral is (128, 128, 0, 255): flat nx = ny = 0 and weight 0, i.e.
    // "keep the stock surface_normal() result". B = 255 here would instead
    // blend an ungenerated texel at FULL weight.
    //
    // A fresh surface is not guaranteed zeroed and generate_sprite_normal
    // reports diagnostics rather than success/failure, so this prefill is what
    // makes the normal half a *valid* map unconditionally -- worst case "no
    // relief", never garbage lighting.
    SDL_FillSurfaceRect( nrm.get(), nullptr,
                         SDL_MapRGBA( SDL_GetPixelFormatDetails( nrm->format ), nullptr,
                                      128, 128, 0, 255 ) );

    const lighting::normal_gen_stats stats = lighting::generate_sprite_normal( {
        .src = src,
        .rect = src_rect,
        .dst = nrm.get(),
        .dst_at = SDL_Point{ 0, 0 },
        .params = {},
    } );
    if( stats.amplitude <= 0.0f ) {
        // Deliberate outcome, not an error: the coherence/density gate refused
        // to fabricate relief for this sprite (dithered noise, or a full tile
        // with no internal contours). Still uploaded, as flat.
        dbg( DL::Debug ) << "normal atlas: flat sprite at " << rect.x << "," << rect.y
                         << " coh " << stats.coherence << " dens " << stats.density
                         << ( stats.full_tile ? " full_tile" : "" );
    }

    // Same rect as the colour, one colour-page down.
    return lighting::get_render_state().upload_surface_subregion_to_gpu_texture(
               it->gpu_texture.get(), rect.x, rect.y + it->atlas_height, nrm.get() );
}

auto dynamic_atlas::normal_v_offset( SDL_Texture *legacy_tex ) const -> float
{
    if( !ENABLE_NORMAL_ATLAS || !legacy_tex ) {
    return 0.0f;
}
const auto it = std::ranges::find_if( sheets, [legacy_tex]( const sprite_sheet & s ) {
        return s.texture.get() == legacy_tex;
    } );
    if( it == sheets.end() || !it->gpu_texture || it->gpu_atlas_height <= it->atlas_height ) {
        return 0.0f;
    }
    return static_cast<float>( it->atlas_height ) / static_cast<float>( it->gpu_atlas_height );
}

auto dynamic_atlas::normal_v_offset() const -> float
{
    if( !ENABLE_NORMAL_ATLAS ) {
    return 0.0f;
}
// The fragment `nrm_atlas_v` uniform is GLOBAL to a frame, not per segment, so it
// needs one value for the whole atlas. That is well defined here because every
// page is allocated with the same derived `colour_h` (allocate_sprite computes it
// from the same caps every time), hence the same 0.5 ratio. Report the first page
// that actually has a GPU mirror; pages without one are never sampled through the
// GPU path anyway. Returns 0.0f when there is no such page, which the shader reads
// as "feature disabled" and falls back to surface_normal().
const auto usable = std::ranges::find_if( sheets, []( const sprite_sheet & s ) {
        return s.gpu_texture && s.gpu_atlas_height > s.atlas_height;
    } );
    if( usable == sheets.end() ) {
        return 0.0f;
    }
    return static_cast<float>( usable->atlas_height ) /
           static_cast<float>( usable->gpu_atlas_height );
}

void dynamic_atlas::readback_dump( const std::string &s ) const
{
    int i = 0;
    for( auto &q : sheets ) {
        auto name = std::format( "{}/tile_dump_{}.png", s, i++ );
        // TODO: fix windows saving images with swapped red/blue channels (it seems to want ARGB not ABGR)
        IMG_SavePNG( q.readback.get(), name.c_str() );
    }
}


void dynamic_atlas::clear()
{
    sheets.clear();
}

