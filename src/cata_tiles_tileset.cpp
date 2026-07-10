#include "cata_tiles.h"
#include "cata_tiles_internal.h"

#include "game.h"
#include "catacharset.h"
#include "field_type.h"
#include "init.h"
#include "input.h"
#include "item_factory.h"
#include "itype.h"
#include "monstergenerator.h"
#include "overlay_ordering.h"
#include "profile.h"
#include "sdltiles.h"
#include "sdl_wrappers.h"
#include "trap.h"
#include "veh_type.h"
#include "debug.h"
#include "dynamic_atlas.h"
#include "rect_range.h"
#include "map.h"
#include "mod_manager.h"
#include "options.h"
#include "player.h"
#include "mod_tileset.h"

#include <algorithm>
#include <functional>
#include <map>
#include <numeric>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

using namespace cata_tiles_internal;
#define dbg(x) DebugLogFL((x), DC::SDL)


template <bool SkipTransparent = true, typename FilterFn>
static void apply_color_filter(
    SDL_Surface* dst, const SDL_Rect& dstRect, SDL_Surface* src, const SDL_Rect& srcRect,
    FilterFn filter_func )
{
    assert( dst );

    if( SDL_MUSTLOCK( dst ) ) { SDL_LockSurface( dst ); }
    if( SDL_MUSTLOCK( src ) ) { SDL_LockSurface( src ); }

    const int dx = std::min( dstRect.w, srcRect.w );
    const int dy = std::min( dstRect.h, srcRect.h );

    for( int y = 0; y < dy; ++y ) {

        const auto dst_offset = static_cast<uint32_t>( ( ( y + dstRect.y ) * dst->w ) + dstRect.x );
        const auto src_offset = static_cast<uint32_t>( ( ( y + srcRect.y ) * src->w ) + srcRect.x );

        auto pDst = static_cast<SDL_Color *>( dst->pixels ) + dst_offset;
        auto pSrc = static_cast<SDL_Color *>( src->pixels ) + src_offset;

        for( int x = 0; x < dx; ++x, ++pDst, ++pSrc ) {
            if constexpr( SkipTransparent ) {
                if( pSrc->a == 0x00 ) { *pDst = {0, 0, 0, 0}; }
            }
            *pDst = filter_func( *pSrc );
        }
    }

    if( SDL_MUSTLOCK( dst ) ) { SDL_UnlockSurface( dst ); }
    if( SDL_MUSTLOCK( src ) ) { SDL_UnlockSurface( src ); }
}

template <bool SkipTransparent = true, typename BlendFn>
static void apply_blend_filter(
    SDL_Surface* dst, const SDL_Rect& dstRect, SDL_Surface* srcA, const SDL_Rect& srcRectA,
    SDL_Surface* srcB, const SDL_Rect& srcRectB, BlendFn blend_func )
{
    assert( dst );
    assert( srcA );
    assert( srcB );

    if( SDL_MUSTLOCK( dst ) ) { SDL_LockSurface( dst ); }
    if( SDL_MUSTLOCK( srcA ) ) { SDL_LockSurface( srcA ); }
    if( SDL_MUSTLOCK( srcB ) ) { SDL_LockSurface( srcB ); }

    const int dx = std::min( dstRect.w, std::min( srcRectA.w, srcRectB.w ) );
    const int dy = std::min( dstRect.h, std::min( srcRectA.h, srcRectB.h ) );

    for( int y = 0; y < dy; ++y ) {

        const auto dst_offset = static_cast<uint32_t>( ( ( y + dstRect.y ) * dst->w ) + dstRect.x );
        const auto srcA_offset = static_cast<uint32_t>( ( ( y + srcRectA.y ) * srcA->w ) + srcRectA.x );
        const auto srcB_offset = static_cast<uint32_t>( ( ( y + srcRectB.y ) * srcB->w ) + srcRectB.x );

        auto pDst = static_cast<SDL_Color *>( dst->pixels ) + dst_offset;
        auto pSrcA = static_cast<SDL_Color *>( srcA->pixels ) + srcA_offset;
        auto pSrcB = static_cast<SDL_Color *>( srcB->pixels ) + srcB_offset;

        for( int x = 0; x < dx; ++x, ++pDst, ++pSrcA, ++pSrcB ) {
            if constexpr( SkipTransparent ) {
                if( pSrcA->a == 0x00 || pSrcB->a == 0x00 ) { *pDst = {0, 0, 0, 0}; }
            }
            *pDst = blend_func( *pSrcA, *pSrcB );
        }
    }

    if( SDL_MUSTLOCK( dst ) ) { SDL_UnlockSurface( dst ); }
    if( SDL_MUSTLOCK( srcA ) ) { SDL_UnlockSurface( srcA ); }
    if( SDL_MUSTLOCK( srcB ) ) { SDL_UnlockSurface( srcB ); }
}

template <bool SkipTransparent = true, typename FilterFn>
static SDL_Surface_Ptr apply_color_filter_blit_copy(
    const SDL_Surface_Ptr& src, FilterFn filter_func )
{
    assert( src );
    SDL_Surface_Ptr dst = create_surface_32( src->w, src->h );
    assert( dst );
    throwErrorIf(
        !SDL_BlitSurface( src.get(), nullptr, dst.get(), nullptr ),
        "SDL_BlitSurface "
        "failed" );

    auto pix = static_cast<SDL_Color *>( dst->pixels );

    for( int y = 0, ey = dst->h; y < ey; ++y ) {
        for( int x = 0, ex = dst->w; x < ex; ++x, ++pix ) {
            if constexpr( SkipTransparent ) {
                if( pix->a == 0x00 ) { continue; }
            }
            *pix = filter_func( *pix );
        }
    }

    return dst;
}

static bool is_contained( const SDL_Rect& smaller, const SDL_Rect& larger )
{
    return smaller.x >= larger.x && smaller.y >= larger.y
           && smaller.x + smaller.w <= larger.x + larger.w
           && smaller.y + smaller.h <= larger.y + larger.h;
}


/**
 * Chains a UV modifier onto an existing composite.
 * In offset mode, offsets are added together.
 * In normalized mode, the modifier is used to look up coordinates in the composite.
 */
[[maybe_unused]] static void chain_uv_modifier(
    SDL_Surface* composite, SDL_Surface* modifier, bool offset_mode )
{
    if( !composite || !modifier ) { return; }

    const int w = std::min( composite->w, modifier->w );
    const int h = std::min( composite->h, modifier->h );

    if( SDL_MUSTLOCK( composite ) ) { SDL_LockSurface( composite ); }
    if( SDL_MUSTLOCK( modifier ) ) { SDL_LockSurface( modifier ); }

    // Temporary copy to avoid reading modified values during iteration
    struct uv_pixel {
        Uint8 r, g;
    };
    std::vector<uv_pixel> temp_pixels( w * h );

    for( int y = 0; y < h; ++y ) {
        for( int x = 0; x < w; ++x ) {
            // Read composite pixel
            Uint8 comp_r, comp_g, comp_b, comp_a;
            get_pixel_rgba( composite, x, y, comp_r, comp_g, comp_b, comp_a );

            // Read modifier pixel
            Uint8 mod_r, mod_g, mod_b, mod_a;
            get_pixel_rgba( modifier, x, y, mod_r, mod_g, mod_b, mod_a );

            uv_pixel& result = temp_pixels[y * w + x];

            if( offset_mode ) {
                // Add offsets together (can temporarily exceed bounds)
                int new_r = static_cast<int>( comp_r ) + ( static_cast<int>( mod_r ) - 127 );
                int new_g = static_cast<int>( comp_g ) + ( static_cast<int>( mod_g ) - 127 );
                result.r = static_cast<Uint8>( std::clamp( new_r, 0, 255 ) );
                result.g = static_cast<Uint8>( std::clamp( new_g, 0, 255 ) );
            } else {
                // Normalized: the modifier's UV encodes positions relative to its own
                // dimensions. Convert to composite space (here at origin, so dst=0).
                int sample_x = modifier->w > 1 ? ( mod_r * ( modifier->w - 1 ) + 127 ) / 255 : 0;
                int sample_y =
                    modifier->h > 1 ? ( ( 255 - mod_g ) * ( modifier->h - 1 ) + 127 ) / 255 : 0;
                sample_x = std::clamp( sample_x, 0, composite->w - 1 );
                sample_y = std::clamp( sample_y, 0, composite->h - 1 );

                Uint8 sampled_r, sampled_g, sampled_b, sampled_a;
                get_pixel_rgba(
                    composite, sample_x, sample_y, sampled_r, sampled_g, sampled_b, sampled_a );

                result.r = sampled_r;
                result.g = sampled_g;
            }
        }
    }

    // Copy results back to composite
    for( int y = 0; y < h; ++y ) {
        for( int x = 0; x < w; ++x ) {
            const uv_pixel& p = temp_pixels[y * w + x];
            set_pixel_rgba( composite, x, y, p.r, p.g, 0, 255 );
        }
    }

    if( SDL_MUSTLOCK( composite ) ) { SDL_UnlockSurface( composite ); }
    if( SDL_MUSTLOCK( modifier ) ) { SDL_UnlockSurface( modifier ); }
}

/**
 * Applies a UV modifier to a source surface, producing a remapped result.
 * Each pixel is sampled from a location determined by the UV modifier.
 */
static void apply_uv_remap(
    SDL_Surface* dst, const SDL_Rect& dstRect, SDL_Surface* src, const SDL_Rect& srcRect,
    SDL_Surface* uv_modifier, const point uv_modifier_offset, const bool offset_mode,
    const point sprite_offset,
    const point output_offset, // Offset of output rect relative to sprite origin (for expanded
    // output)
    const int std_tile_w [[maybe_unused]], const int std_tile_h [[maybe_unused]],
    color_pixel_function_pointer color_func = nullptr )
{
    if( !dst || !src || !uv_modifier ) { return; }

    // Copy source to a temporary surface for consistent pixel format
    SDL_Surface_Ptr src_copy = create_surface_32( srcRect.w, srcRect.h );
    if( !src_copy ) { return; }
    SDL_SetSurfaceBlendMode( src, SDL_BLENDMODE_NONE );
    SDL_Rect src_copy_rect = {0, 0, srcRect.w, srcRect.h};
    SDL_BlitSurface( src, const_cast<SDL_Rect*>( &srcRect ), src_copy.get(), &src_copy_rect );

    if( SDL_MUSTLOCK( dst ) ) { SDL_LockSurface( dst ); }
    if( SDL_MUSTLOCK( src_copy.get() ) ) { SDL_LockSurface( src_copy.get() ); }
    if( SDL_MUSTLOCK( uv_modifier ) ) { SDL_LockSurface( uv_modifier ); }

    // Clear the destination to transparent
    for( int y = 0; y < dstRect.h; ++y ) {
        for( int x = 0; x < dstRect.w; ++x ) {
            set_pixel_rgba( dst, x + dstRect.x, y + dstRect.y, 0, 0, 0, 0 );
        }
    }

    // Iterate over UV modifier bounds
    // output_offset tells us where the output rect starts relative to sprite origin
    // So a pixel at UV position (uv_x, uv_y) maps to:
    //   - tile-relative position: uv_modifier_offset + (uv_x, uv_y)
    //   - sprite-local position: tile-relative - sprite_offset
    //   - output position: sprite-local - output_offset
    for( int uv_y = 0; uv_y < uv_modifier->h; ++uv_y ) {
        for( int uv_x = 0; uv_x < uv_modifier->w; ++uv_x ) {
            // Position relative to standard tile origin
            const int tile_rel_x = uv_modifier_offset.x + uv_x;
            const int tile_rel_y = uv_modifier_offset.y + uv_y;

            // Position relative to sprite origin
            const int sprite_local_x = tile_rel_x - sprite_offset.x;
            const int sprite_local_y = tile_rel_y - sprite_offset.y;

            // Position in output rect (accounting for output offset)
            const int dst_x = sprite_local_x - output_offset.x;
            const int dst_y = sprite_local_y - output_offset.y;

            // Skip if outside destination rect
            if( dst_x < 0 || dst_x >= dstRect.w || dst_y < 0 || dst_y >= dstRect.h ) { continue; }

            Uint8 uv_r, uv_g, uv_b, uv_a;
            get_pixel_rgba( uv_modifier, uv_x, uv_y, uv_r, uv_g, uv_b, uv_a );

            // Alpha=0 means render transparent
            if( uv_a == 0 ) {
                set_pixel_rgba( dst, dst_x + dstRect.x, dst_y + dstRect.y, 0, 0, 0, 0 );
                continue;
            }

            int src_x, src_y;
            if( offset_mode ) {
                // Offset mode: 127 = neutral, apply offset from sprite-local position
                src_x = sprite_local_x + ( static_cast<int>( uv_r ) - 127 );
                src_y = sprite_local_y + ( static_cast<int>( uv_g ) - 127 );
            } else {
                // Normalized mode: UV maps to modifier bounds, then to sprite coords (G inverted)
                int uv_target_x =
                    uv_modifier->w > 1 ? ( uv_r * ( uv_modifier->w - 1 ) + 127 ) / 255 : 0;
                int uv_target_y =
                    uv_modifier->h > 1 ? ( ( 255 - uv_g ) * ( uv_modifier->h - 1 ) + 127 ) / 255 : 0;
                int target_rel_x = uv_modifier_offset.x + uv_target_x;
                int target_rel_y = uv_modifier_offset.y + uv_target_y;
                src_x = target_rel_x - sprite_offset.x;
                src_y = target_rel_y - sprite_offset.y;
            }

            if( src_x < 0 || src_x >= srcRect.w || src_y < 0 || src_y >= srcRect.h ) {
                set_pixel_rgba( dst, dst_x + dstRect.x, dst_y + dstRect.y, 0, 0, 0, 0 );
            } else {
                Uint8 sr, sg, sb, sa;
                get_pixel_rgba( src_copy.get(), src_x, src_y, sr, sg, sb, sa );
                if( color_func && sa > 0 ) {
                    SDL_Color c = color_func( SDL_Color{sr, sg, sb, sa} );
                    sr = c.r;
                    sg = c.g;
                    sb = c.b;
                    sa = c.a;
                }
                set_pixel_rgba( dst, dst_x + dstRect.x, dst_y + dstRect.y, sr, sg, sb, sa );
            }
        }
    }

    // Fill areas outside UV modifier bounds but inside sprite bounds with original pixels
    for( int y = 0; y < srcRect.h; ++y ) {
        for( int x = 0; x < srcRect.w; ++x ) {
            // Position relative to standard tile origin
            const int tile_rel_x = x + sprite_offset.x;
            const int tile_rel_y = y + sprite_offset.y;

            // Position within UV modifier surface
            const int uv_check_x = tile_rel_x - uv_modifier_offset.x;
            const int uv_check_y = tile_rel_y - uv_modifier_offset.y;

            const bool in_uv_bounds =
                uv_check_x >= 0 && uv_check_x < uv_modifier->w && uv_check_y >= 0
                && uv_check_y < uv_modifier->h;

            if( !in_uv_bounds ) {
                // Outside UV bounds: pass-through original pixel
                // Map sprite-local (x, y) to output position
                const int dst_x = x - output_offset.x;
                const int dst_y = y - output_offset.y;
                if( dst_x >= 0 && dst_x < dstRect.w && dst_y >= 0 && dst_y < dstRect.h ) {
                    Uint8 sr, sg, sb, sa;
                    get_pixel_rgba( src_copy.get(), x, y, sr, sg, sb, sa );
                    if( color_func && sa > 0 ) {
                        SDL_Color c = color_func( SDL_Color{sr, sg, sb, sa} );
                        sr = c.r;
                        sg = c.g;
                        sb = c.b;
                        sa = c.a;
                    }
                    set_pixel_rgba( dst, dst_x + dstRect.x, dst_y + dstRect.y, sr, sg, sb, sa );
                }
            }
        }
    }

    if( SDL_MUSTLOCK( dst ) ) { SDL_UnlockSurface( dst ); }
    if( SDL_MUSTLOCK( src_copy.get() ) ) { SDL_UnlockSurface( src_copy.get() ); }
    if( SDL_MUSTLOCK( uv_modifier ) ) { SDL_UnlockSurface( uv_modifier ); }
}

static color_pixel_function_pointer get_pixel_function( const tileset_fx_type& type )
{
    switch( type ) {
        case tileset_fx_type::shadow:
            return get_color_pixel_function( "color_pixel_grayscale" );
            break;
        case tileset_fx_type::night:
            return get_color_pixel_function( "color_pixel_nightvision" );
            break;
        case tileset_fx_type::overexposed:
            return get_color_pixel_function( "color_pixel_overexposed" );
            break;
        case tileset_fx_type::underwater:
            return get_color_pixel_function( "color_pixel_underwater" );
            break;
        case tileset_fx_type::underwater_dark:
            return get_color_pixel_function( "color_pixel_underwater_dark" );
            break;
        case tileset_fx_type::memory:
            return get_color_pixel_function( tilecontext->memory_map_mode );
            break;
        case tileset_fx_type::z_overlay:
            return get_color_pixel_function( "color_pixel_zoverlay" );
            break;
        default:
            return get_color_pixel_function( "color_pixel_copy" );
            break;
    }
}

template <typename T, typename U, U max_t = std::numeric_limits<U>::max()>
static T ilerp( const T a, const T b, const U t )
{
    return ( ( b * t ) + ( a * ( max_t - t ) ) ) / max_t;
};

static void apply_surf_blend_effect(
    SDL_Surface* staging, const tint_config& tint, const bool use_mask, const SDL_Rect& dstRect,
    const SDL_Rect& srcRect, const SDL_Rect& maskRect )
{
    ZoneScoped;

    const auto blend_op =
        [&tint]( const SDL_Color base, const SDL_Color target,
    std::optional<SDL_Color> mask = std::nullopt ) -> SDL_Color {
        SDL_Color col;

        switch( tint.blend_mode )
        {
            case tint_blend_mode::additive: {
                col = RGBColor{
                    static_cast<uint8_t>( std::min<int>( base.r + target.r, 255 ) ),
                    static_cast<uint8_t>( std::min<int>( base.g + target.g, 255 ) ),
                    static_cast<uint8_t>( std::min<int>( base.b + target.b, 255 ) ),
                    static_cast<uint8_t>( std::min<int>( base.a + target.a, 255 ) )};
                break;
            }
            case tint_blend_mode::subtract: {
                col = RGBColor{
                    static_cast<uint8_t>( std::max<int>( base.r - ( 255 - target.r ), 0 ) ),
                    static_cast<uint8_t>( std::max<int>( base.g - ( 255 - target.g ), 0 ) ),
                    static_cast<uint8_t>( std::max<int>( base.b - ( 255 - target.b ), 0 ) ), base.a};
                break;
            }
            case tint_blend_mode::multiply: {
                col = RGBColor{
                    static_cast<uint8_t>( base.r * target.r / 256 ),
                    static_cast<uint8_t>( base.g * target.g / 256 ),
                    static_cast<uint8_t>( base.b * target.b / 256 ), base.a};
                break;
            }
            case tint_blend_mode::normal: {
                col = RGBColor{
                    static_cast<uint8_t>( ilerp<uint16_t, uint8_t>( base.r, target.r, target.a ) ),
                    static_cast<uint8_t>( ilerp<uint16_t, uint8_t>( base.g, target.g, target.a ) ),
                    static_cast<uint8_t>( ilerp<uint16_t, uint8_t>( base.b, target.b, target.a ) ),
                    base.a};
                break;
            }
            case tint_blend_mode::divide: {
                col = RGBColor{
                    static_cast<uint8_t>( base.r / std::max<uint8_t>( 1, target.r ) ),
                    static_cast<uint8_t>( base.g / std::max<uint8_t>( 1, target.g ) ),
                    static_cast<uint8_t>( base.b / std::max<uint8_t>( 1, target.b ) ), base.a};
                break;
            }
            case tint_blend_mode::screen: {
                auto screen_channel = []( const uint8_t base, const uint8_t blend ) -> uint8_t {
                    int result = std::clamp<int>( 255 - ( 255 - base ) * ( 255 - blend ) / 128, 0, 255 );
                    return std::clamp<int>( result, 0, 255 );
                };
                col = SDL_Color{screen_channel( base.r, target.r ), screen_channel( base.g, target.g ),
                                screen_channel( base.b, target.b ), base.a};
                break;
            }
            case tint_blend_mode::softlight: {
                auto softlight_channel = []( const uint8_t base, const uint8_t blend ) -> uint8_t {
                    // Pegtop soft light formula
                    int result = ( ( 255 - 2 * blend ) * base * base / 256 + 2 * blend * base ) / 256;
                    return std::clamp<int>( result, 0, 255 );
                };
                col = SDL_Color{
                    softlight_channel( base.r, target.r ), softlight_channel( base.g, target.g ),
                    softlight_channel( base.b, target.b ), base.a};
                break;
            }
            case tint_blend_mode::hardlight: {
                auto hardlight_channel = []( const uint8_t base, const uint8_t blend ) -> uint8_t {
                    if( blend > 127 )
                {
                    return static_cast<uint8_t>( std::clamp<int>(
                                                     255 - ( 255 - blend ) * ( ( std::max( 255 - base, 1 ) ) * 255 / 127 ) / 255, 0,
                                                     255 ) );
                    } else
                    {
                        return static_cast<uint8_t>(
                            std::clamp<int>( blend * ( base * 255 / 127 ) / 255, 0, 255 ) );
                    }
                };
                col = SDL_Color{
                    hardlight_channel( base.r, target.r ), hardlight_channel( base.g, target.g ),
                    hardlight_channel( base.b, target.b ), base.a};
                break;
            }
            case tint_blend_mode::overlay: {
                auto overlay_channel = []( const uint8_t base, const uint8_t blend ) -> uint8_t {
                    if( base > 127 )
                {
                    return static_cast<uint8_t>( std::clamp<int>(
                                                     255 - ( std::max( 255 - blend, 1 ) ) * ( ( 255 - base ) * 255 / 127 ) / 255, 0,
                                                     255 ) );
                    } else
                    {
                        return static_cast<uint8_t>(
                            std::clamp<int>( blend * ( base * 255 / 127 ) / 255, 0, 255 ) );
                    }
                };
                col = SDL_Color{
                    overlay_channel( base.r, target.r ), overlay_channel( base.g, target.g ),
                    overlay_channel( base.b, target.b ), base.a};
                break;
            }
            default:
            case tint_blend_mode::tint: {
                auto base_hsv = rgb2hsv( base );
                auto dest_hsv = rgb2hsv( target );

                constexpr auto overlay = []( const uint8_t base, const uint8_t blend ) -> uint8_t {
                    if( base > 127 )
                {
                    return static_cast<uint8_t>( std::clamp<int>(
                                                     255 - ( std::max( 255 - blend, 1 ) ) * ( ( 255 - base ) * 255 / 127 ) / 255, 0,
                                                     255 ) );
                    } else
                    {
                        return static_cast<uint8_t>(
                            std::clamp<int>( blend * ( base * 255 / 127 ) / 255, 0, 255 ) );
                    }
                };

                base_hsv.H = dest_hsv.H;
                base_hsv.S = ilerp<uint16_t, uint8_t>(
                                 std::min( base_hsv.S, dest_hsv.S ), dest_hsv.S,
                                 mask.has_value() ? mask.value().g : 127 );
                base_hsv.V = ilerp<uint16_t, uint8_t>(
                                 base_hsv.V, overlay( base_hsv.V, dest_hsv.V ),
                                 mask.has_value() ? mask.value().b : 127 );

                col = hsv2rgb( base_hsv );
                break;
            }
        }
        if( mask.has_value() )
        {
            col.r = ilerp( base.r, col.r, mask.value().r );
            col.g = ilerp( base.g, col.g, mask.value().r );
            col.b = ilerp( base.b, col.b, mask.value().r );
        }
        return col;
    };

    auto postprocess = [&tint]( SDL_Color c ) -> SDL_Color {
        auto [h, s, v, a] = rgb2hsv( c );
        if( fabs( tint.contrast - 1.0f ) > 0.001f )
        {
            const float adjusted = ( ( static_cast<float>( v ) - 128.0f ) * tint.contrast ) + 128.0f;
            v = static_cast<uint8_t>( std::clamp( adjusted, 0.0f, 255.0f ) );
        }
        if( fabs( tint.saturation - 1.0f ) > 0.001f )
        {
            s = static_cast<uint16_t>(
                std::clamp( static_cast<float>( s ) * tint.saturation, 0.0f, 65535.0f ) );
        }
        if( fabs( tint.brightness - 1.0f ) > 0.001f )
        {
            v = static_cast<uint8_t>(
                std::clamp( static_cast<float>( v ) * tint.brightness, 0.0f, 255.0f ) );
        }
        return hsv2rgb( HSVColor{h, s, v, a} );
    };

    if( use_mask ) {
        auto effect_mask = [&]( const SDL_Color & base_rgb, const SDL_Color & mask_rgb ) -> SDL_Color {
            RGBColor res = blend_op( base_rgb, tint.color, mask_rgb );
            return postprocess( res );
        };
        apply_blend_filter( staging, dstRect, staging, srcRect, staging, maskRect, effect_mask );
    } else {
        auto effect_no_mask = [&]( const SDL_Color & c ) -> SDL_Color {
            RGBColor res = blend_op( c, tint.color );
            return postprocess( res );
        };
        apply_color_filter( staging, dstRect, staging, srcRect, effect_no_mask );
    }
}
void cata_tiles::on_options_changed()
{
    memory_map_mode = get_option<std::string>( "MEMORY_MAP_MODE" );
}

void cata_tiles::load_tileset(
    const std::string& tileset_id, const std::vector<mod_id> &mod_list, const bool precheck,
    const bool force, const bool pump_events )
{
    if( !force && tileset_ptr && !get_option<bool>( "FORCE_TILESET_RELOAD" )
        && tileset_ptr->get_tileset_id() == tileset_id && tileset_mod_list_stamp == mod_list ) {
        return;
    }
    // TODO: move into clear or somewhere else.
    // reset the overlay ordering from the previous loaded tileset
    tileset_mutation_overlay_ordering.clear();

    // Load the tileset into a separate instance and only set this->tileset_ptr
    // when the loading has succeeded.
    std::unique_ptr<tileset> new_tileset_ptr = std::make_unique<tileset>();
    tileset_loader loader( *new_tileset_ptr, renderer );
    loader.load( tileset_id, precheck, /*pump_events=*/pump_events );
    tileset_ptr = std::move( new_tileset_ptr );
    tileset_mod_list_stamp = mod_list;

    set_draw_scale( 16 );
}

void cata_tiles::reinit()
{
    set_draw_scale( 16 );
    RenderClear( renderer );
}

void cata_tiles::init_light() { g->reset_light_level(); }

void cata_tiles::do_tile_loading_report( const std::function<void( std::string )> &out )
{
    out( "Loaded tileset: " + get_option<std::string>( "TILES" ) );

    if( !init::is_data_loaded() ) {
        // There's nothing to do anymore without the core data.
        return;
    }

    tile_loading_report<ter_t>( ter_t::count(), C_TERRAIN, out, "" );
    tile_loading_report<furn_t>( furn_t::count(), C_FURNITURE, out, "" );

    std::map<itype_id, const itype *> items;
    for( const itype * e : item_controller->all() ) { items.emplace( e->get_id(), e ); }
    tile_loading_report( items, C_ITEM, out, "" );

    auto mtypes = MonsterGenerator::generator().get_all_mtypes();
    lr_generic(
        mtypes.begin(), mtypes.end(),
    []( const std::vector<mtype>::iterator & m ) { return ( *m ).id.str(); }, C_MONSTER, out, "" );
    tile_loading_report( vpart_info::all(), C_VEHICLE_PART, out, "vp_" );
    tile_loading_report<trap>( trap::count(), C_TRAP, out, "" );
    tile_loading_report<field_type>( field_type::count(), C_FIELD, out, "" );
}

template <typename Iter, typename Func>
void cata_tiles::lr_generic(
    Iter begin, Iter end, Func id_func, TILE_CATEGORY category,
    std::function<void( std::string )> out, const std::string& prefix )
{
    std::string missing_list;
    std::string missing_with_looks_like_list;
    for( ; begin != end; ++begin ) {
        const std::string id_string = id_func( begin );

        if( !tileset_ptr->find_tile_type( prefix + id_string )
            && !find_tile_looks_like( id_string, category ) ) {
            missing_list.append( id_string + " " );
        } else if( !tileset_ptr->find_tile_type( prefix + id_string ) ) {
            missing_with_looks_like_list.append( id_string + " " );
        }
    }
    out( "Missing " + TILE_CATEGORY_IDS[category] + ": " + missing_list );
    out( "Missing " + TILE_CATEGORY_IDS[category]
         + " (but looks_like tile exists): " + missing_with_looks_like_list );
}

template <typename maptype>
void cata_tiles::tile_loading_report(
    const maptype& tiletypemap, TILE_CATEGORY category, std::function<void( std::string )> out,
    const std::string& prefix )
{
    lr_generic(
        tiletypemap.begin(), tiletypemap.end(),
    []( const decltype( tiletypemap.begin() )& v ) {
        // c_str works for std::string and for string_id!
        return v->first.c_str();
    },
    category, out, prefix );
}

template <typename base_type>
void cata_tiles::tile_loading_report(
    const size_t count, TILE_CATEGORY category, std::function<void( std::string )> out,
    const std::string& prefix )
{
    lr_generic(
        static_cast<size_t>( 0 ), count,
    []( const size_t i ) { return int_id<base_type>( i ).id().str(); }, category, out, prefix );
}

template <typename arraytype>
void cata_tiles::tile_loading_report(
    const arraytype& array, int array_length, TILE_CATEGORY category,
    std::function<void( std::string )> out, const std::string& prefix )
{
    const auto begin = &( array[0] );
    lr_generic(
    begin, begin + array_length, []( decltype( begin ) const v ) { return v->id; }, category, out,
    prefix );
}

std::vector<options_manager::id_and_option> cata_tiles::build_renderer_list()
{
    std::vector<options_manager::id_and_option> renderer_names;
    std::vector<options_manager::id_and_option> default_renderer_names = {
#if defined(_WIN32)
        {"direct3d", translate_marker( "direct3d" )},
#endif
        {"opengl", translate_marker( "opengl" )},
        {"opengles2", translate_marker( "opengles2" )},
        {"software", translate_marker( "software" )},
    };
    const int numRenderDrivers = SDL_GetNumRenderDrivers();
    DebugLog( DL::Info, DC::Main ) << "Number of render drivers on your system: " << numRenderDrivers;
    for( int ii = 0; ii < numRenderDrivers; ii++ ) {
        const char *name = SDL_GetRenderDriver( ii );
        if( !name ) { continue; }
        DebugLog( DL::Info, DC::Main ) << "Render driver: " << ii << "/" << name;
        // First default renderer name we will put first on the list. We can use it later as default
        // value.
        if( name == default_renderer_names.front().first ) {
            renderer_names.emplace( renderer_names.begin(), default_renderer_names.front() );
        } else {
            renderer_names.emplace_back( name, name );
        }
    }

    return renderer_names.empty() ? default_renderer_names : renderer_names;
}

std::vector<options_manager::id_and_option> cata_tiles::build_display_list()
{
    std::vector<options_manager::id_and_option> display_names;
    std::vector<options_manager::id_and_option> default_display_names = {
        {"0", translate_marker( "Display 0" )}
    };

    int numdisplays = 0;
    SDL_DisplayID* displays = SDL_GetDisplays( &numdisplays );
    display_names.reserve( numdisplays );
    if( displays ) {
        for( int i = 0; i < numdisplays; i++ ) {
            const char *display_name = SDL_GetDisplayName( displays[i] );
            display_names
            .emplace_back( std::to_string( i ), std::string( display_name ? display_name : "" ) );
        }
        SDL_free( displays );
    }

    return display_names.empty() ? default_display_names : display_names;
}
texture_result tileset::get_or_default(
    const int sprite_index, const int mask_index, const tileset_fx_type& type,
    const tint_config& tint, const size_t warp_hash, const point sprite_offset ) const
{
    ZoneScoped;

#if defined(DYNAMIC_ATLAS)

    const auto base_tex_key = tileset_lookup_key{
        sprite_index,     TILESET_NO_MASK, tileset_fx_type::none,
        TILESET_NO_COLOR, TILESET_NO_WARP, point_zero};
    const auto mask_tex_key = tileset_lookup_key{
        mask_index,       TILESET_NO_MASK, tileset_fx_type::none,
        TILESET_NO_COLOR, TILESET_NO_WARP, point_zero};
    const auto mod_tex_key =
        tileset_lookup_key{sprite_index, mask_index, type, tint, warp_hash, sprite_offset};

    if( g->display_overlay_state( ACTION_DISPLAY_TILES_NO_VFX ) ) {
        const auto base_tex_it = tile_lookup.find( base_tex_key );
        if( base_tex_it == tile_lookup.end() ) { return {nullptr, point_zero}; }
        return {&base_tex_it->second.tex, base_tex_it->second.warp_offset};
    }

    const auto mod_tex_it = tile_lookup.find( mod_tex_key );
    if( mod_tex_it != tile_lookup.end() ) {
        return {&mod_tex_it->second.tex, mod_tex_it->second.warp_offset};
    }

    const auto base_tex_it = tile_lookup.find( base_tex_key );
    if( base_tex_it == tile_lookup.end() ) { return {nullptr, point_zero}; }

    const auto mask_tex_it = tile_lookup.find( mask_tex_key );

    const color_pixel_function_pointer vfx_func = get_pixel_function( type );
    if( !vfx_func ) { debugmsg( "Error loading visual effect function" ); }

    {
        ZoneScoped;

        const auto& r = get_sdl_renderer();
        const auto rp = r.get();

        const texture& base_tex = base_tex_it->second.tex;
        const texture* mask_tex =
            ( mask_tex_it != tile_lookup.end() ) ? &mask_tex_it->second.tex : nullptr;

        const auto [spr_w, spr_h] = base_tex.dimension();

        // Calculate output bounds based on UV warp (if present)
        int out_w = spr_w;
        int out_h = spr_h;
        point warp_output_offset = point_zero;

        if( warp_hash != TILESET_NO_WARP ) {
            auto [warp_surf, warp_offset, offset_mode] = get_warp_surface( warp_hash );
            if( warp_surf ) {
                int min_x = 0;
                int min_y = 0;
                int max_x = spr_w;
                int max_y = spr_h;

                if( SDL_MUSTLOCK( warp_surf ) ) { SDL_LockSurface( warp_surf ); }

                for( int uv_y = 0; uv_y < warp_surf->h; ++uv_y ) {
                    for( int uv_x = 0; uv_x < warp_surf->w; ++uv_x ) {
                        Uint8 uv_r, uv_g, uv_b, uv_a;
                        get_pixel_rgba( warp_surf, uv_x, uv_y, uv_r, uv_g, uv_b, uv_a );

                        if( uv_a == 0 ) { continue; }

                        const int tile_rel_x = warp_offset.x + uv_x;
                        const int tile_rel_y = warp_offset.y + uv_y;
                        const int sprite_local_x = tile_rel_x - sprite_offset.x;
                        const int sprite_local_y = tile_rel_y - sprite_offset.y;

                        int src_x, src_y;
                        if( offset_mode ) {
                            src_x = sprite_local_x + ( static_cast<int>( uv_r ) - 127 );
                            src_y = sprite_local_y + ( static_cast<int>( uv_g ) - 127 );
                        } else {
                            int uv_target_x =
                                warp_surf->w > 1 ? ( uv_r * ( warp_surf->w - 1 ) + 127 ) / 255 : 0;
                            int uv_target_y =
                                warp_surf->h > 1
                                ? ( ( 255 - uv_g ) * ( warp_surf->h - 1 ) + 127 ) / 255
                                : 0;
                            int target_rel_x = warp_offset.x + uv_target_x;
                            int target_rel_y = warp_offset.y + uv_target_y;
                            src_x = target_rel_x - sprite_offset.x;
                            src_y = target_rel_y - sprite_offset.y;
                        }

                        if( src_x >= 0 && src_x < spr_w && src_y >= 0 && src_y < spr_h ) {
                            min_x = std::min( min_x, sprite_local_x );
                            min_y = std::min( min_y, sprite_local_y );
                            max_x = std::max( max_x, sprite_local_x + 1 );
                            max_y = std::max( max_y, sprite_local_y + 1 );
                        }
                    }
                }

                if( SDL_MUSTLOCK( warp_surf ) ) { SDL_UnlockSurface( warp_surf ); }

                out_w = max_x - min_x;
                out_h = max_y - min_y;
                warp_output_offset = point( min_x, min_y );
            }
        }

        const int staging_w = std::max( spr_w * 2, out_w );
        const int staging_h = spr_h * 2 + out_h * 2;
        const auto [st_tex, st_surf, st_sub_rect] =
            texture_atlas()->get_staging_area( staging_w, staging_h );

        const auto st_sub_rect_source =
            SDL_Rect{st_sub_rect.x + 0, st_sub_rect.y + 0, spr_w, spr_h};
        const auto st_sub_rect_mask =
            SDL_Rect{st_sub_rect.x + spr_w, st_sub_rect.y + 0, spr_w, spr_h};
        const auto st_sub_rect_tinted =
            SDL_Rect{st_sub_rect.x + 0, st_sub_rect.y + spr_h, spr_w, spr_h};
        const auto st_sub_rect_vfx =
            SDL_Rect{st_sub_rect.x + spr_w, st_sub_rect.y + spr_h, spr_w, spr_h};
        const auto st_sub_rect_warped =
            SDL_Rect{st_sub_rect.x + 0, st_sub_rect.y + spr_h * 2, out_w, out_h};
        const auto st_sub_rect_final =
            SDL_Rect{st_sub_rect.x + 0, st_sub_rect.y + spr_h * 2 + out_h, out_w, out_h};

        const auto state = sdl_save_render_state( rp );

        SDL_SetRenderTarget( rp, st_tex );
        SetRenderDrawColor( r, 255, 0, 255, 255 );
        SDL_RenderClear( rp );

        base_tex.set_blend_mode( SDL_BLENDMODE_NONE );
        base_tex.render_copy( r, &st_sub_rect_source );
        base_tex.set_blend_mode( SDL_BLENDMODE_BLEND );

        if( mask_tex ) {
            mask_tex->set_blend_mode( SDL_BLENDMODE_NONE );
            mask_tex->render_copy( r, &st_sub_rect_mask );
            mask_tex->set_blend_mode( SDL_BLENDMODE_BLEND );
        }

        {
            SDL_Surface* readback = SDL_RenderReadPixels( rp, nullptr );
            if( readback ) {
                SDL_SetSurfaceBlendMode( readback, SDL_BLENDMODE_NONE );
                SDL_BlitSurface( readback, nullptr, st_surf, nullptr );
                SDL_DestroySurface( readback );
            }
        }

        if( !tint.has_value() ) {
            apply_color_filter(
                st_surf, st_sub_rect_tinted, st_surf, st_sub_rect_source, color_pixel_copy );
        } else {
            apply_surf_blend_effect(
                st_surf, tint, mask_tex, st_sub_rect_tinted, st_sub_rect_source, st_sub_rect_mask );
        }

        switch( type ) {
            case tileset_fx_type::overexposed: {
                tint_config vfx_tint;
                if( get_option<std::string>( "NIGHT_VISION_DEFAULT_COLOR" ) == "custom" ) {
                    vfx_tint = tint_config{RGBColor::try_parse( get_option<std::string>(
                                               "NIGHT_"
                                               "VISION_"
                                               "COLOR" ) )};
                } else {
                    vfx_tint = tint_config{RGBColor::try_parse( get_option<std::string>(
                                               "NIGHT_"
                                               "VISION_"
                                               "DEFAULT_"
                                               "COLOR" ) )};
                }
                vfx_tint.blend_mode = tint_blend_mode::tint;
                vfx_tint.brightness = 1.25f;
                apply_surf_blend_effect(
                    st_surf, vfx_tint, false, st_sub_rect_vfx, st_sub_rect_tinted, {} );
                break;
            }
            case tileset_fx_type::night: {
                tint_config vfx_tint;
                if( get_option<std::string>( "NIGHT_VISION_DEFAULT_COLOR" ) == "custom" ) {
                    vfx_tint = tint_config{RGBColor::try_parse( get_option<std::string>(
                                               "NIGHT_"
                                               "VISION_"
                                               "COLOR" ) )};
                } else {
                    vfx_tint = tint_config{RGBColor::try_parse( get_option<std::string>(
                                               "NIGHT_"
                                               "VISION_"
                                               "DEFAULT_"
                                               "COLOR" ) )};
                }
                vfx_tint.blend_mode = tint_blend_mode::tint;
                vfx_tint.brightness = 0.75f;
                apply_surf_blend_effect(
                    st_surf, vfx_tint, false, st_sub_rect_vfx, st_sub_rect_tinted, {} );
                break;
            }
            case tileset_fx_type::enhanced_overexposed: {
                tint_config vfx_tint;
                if( get_option<std::string>( "ENHANCED_NIGHT_VISION_DEFAULT_COLOR" ) == "custom" ) {
                    vfx_tint = tint_config{RGBColor::try_parse( get_option<std::string>(
                                               "ENHANCED_"
                                               "NIGHT_"
                                               "VISION_"
                                               "COLOR" ) )};
                } else {
                    vfx_tint = tint_config{RGBColor::try_parse( get_option<std::string>(
                                               "ENHANCED_"
                                               "NIGHT_"
                                               "VISION_"
                                               "DEFAULT_"
                                               "COLOR" ) )};
                }
                vfx_tint.blend_mode = tint_blend_mode::tint;
                vfx_tint.brightness = 1.25f;
                apply_surf_blend_effect(
                    st_surf, vfx_tint, false, st_sub_rect_vfx, st_sub_rect_tinted, {} );
                break;
            }
            case tileset_fx_type::enhanced_night: {
                tint_config vfx_tint;
                if( get_option<std::string>( "ENHANCED_NIGHT_VISION_DEFAULT_COLOR" ) == "custom" ) {
                    vfx_tint = tint_config{RGBColor::try_parse( get_option<std::string>(
                                               "ENHANCED_"
                                               "NIGHT_"
                                               "VISION_"
                                               "COLOR" ) )};
                } else {
                    vfx_tint = tint_config{RGBColor::try_parse( get_option<std::string>(
                                               "ENHANCED_"
                                               "NIGHT_"
                                               "VISION_"
                                               "DEFAULT_"
                                               "COLOR" ) )};
                }
                vfx_tint.blend_mode = tint_blend_mode::tint;
                vfx_tint.brightness = 0.75f;
                apply_surf_blend_effect(
                    st_surf, vfx_tint, false, st_sub_rect_vfx, st_sub_rect_tinted, {} );
                break;
            }
            default: {
                apply_color_filter( st_surf, st_sub_rect_vfx, st_surf, st_sub_rect_tinted, vfx_func );
                break;
            }
        }

        SDL_Rect final_src_rect = st_sub_rect_vfx;
        int final_w = spr_w;
        int final_h = spr_h;
        if( warp_hash != TILESET_NO_WARP ) {
            auto [warp_surf, warp_offset, offset_mode] = get_warp_surface( warp_hash );
            if( warp_surf ) {
                apply_uv_remap(
                    st_surf, st_sub_rect_warped, st_surf, st_sub_rect_vfx, warp_surf, warp_offset,
                    offset_mode, sprite_offset, warp_output_offset, tile_width, tile_height,
                    nullptr );
                final_src_rect = st_sub_rect_warped;
                final_w = out_w;
                final_h = out_h;
            }
        }

        apply_color_filter( st_surf, st_sub_rect_final, st_surf, final_src_rect, color_pixel_copy );

        auto surf_hash = get_surface_hash( st_surf, &st_sub_rect_final );
        auto existing = tileset_atlas->id_search( surf_hash );

        atlas_texture atl_tex;
        if( existing.has_value() ) {
            atl_tex = std::move( existing.value() );
        } else {
            atl_tex = tileset_atlas->allocate_sprite( final_w, final_h );
            tileset_atlas->id_assign( surf_hash, atl_tex );

            SDL_UpdateTexture( st_tex, nullptr, st_surf->pixels, st_surf->pitch );
            SDL_SetRenderTarget( rp, atl_tex.first.get() );
            {
                const SDL_FRect
                fsrc{float( st_sub_rect_final.x ), float( st_sub_rect_final.y ),
                     float( st_sub_rect_final.w ), float( st_sub_rect_final.h )};
                const SDL_FRect
                fdst{float( atl_tex.second.x ), float( atl_tex.second.y ), float( atl_tex.second.w ),
                     float( atl_tex.second.h )};
                SDL_RenderTexture( rp, st_tex, &fsrc, &fdst );
            }
            if( SDL_GPUTexture * gpu_atlas = tileset_atlas->find_gpu_texture( atl_tex.first.get() ) ) {
                lighting::get_render_state().upload_surface_subregion_to_gpu_texture(
                    gpu_atlas, atl_tex.second.x, atl_tex.second.y, st_surf, &st_sub_rect_final );
            }
        }

        sdl_restore_render_state( rp, state );
        auto& [at_tex, at_rect] = atl_tex;
        auto [entry, ok] = tile_lookup.emplace(
                               mod_tex_key,
                               tile_lookup_entry{texture( std::move( at_tex ), at_rect ), warp_output_offset} );
        if( !ok ) {
            dbg( DL::Error ) << "dynamic atlas hash collision, you will likely see minor graphical "
                                "issues"
                             << std::endl;
        }
        return {&entry->second.tex, entry->second.warp_offset};
    }
#else
    if( sprite_index >= tile_values.size() ) { return {nullptr, point_zero}; }

    switch( type ) {
        case tileset_fx_type::shadow:
            return {&shadow_tile_values[sprite_index], point_zero};
        case tileset_fx_type::night:
            return {&night_tile_values[sprite_index], point_zero};
        case tileset_fx_type::overexposed:
            return {&overexposed_tile_values[sprite_index], point_zero};
        case tileset_fx_type::underwater:
            return {&underwater_tile_values[sprite_index], point_zero};
        case tileset_fx_type::underwater_dark:
            return {&underwater_dark_tile_values[sprite_index], point_zero};
        case tileset_fx_type::memory:
            return {&memory_tile_values[sprite_index], point_zero};
        case tileset_fx_type::z_overlay:
            return {&z_overlay_values[sprite_index], point_zero};
        default:
            return {&tile_values[sprite_index], point_zero};
    }
#endif
}
#if defined(DYNAMIC_ATLAS)
void tileset::ensure_readback_loaded() const
{
    if( tileset_atlas ) { tileset_atlas->readback_load(); }
}

void tileset::clear_warp_cache() const { warp_cache.clear(); }
#endif

#if defined(DYNAMIC_ATLAS)
bool tileset_loader::copy_surface_to_dynamic_atlas(
    const SDL_Surface_Ptr& surf, const point offset )
{
    assert( surf );
    const rect_range<SDL_Rect> input_range(
        sprite_width,
        sprite_height,
        point( surf->w / sprite_width, surf->h / sprite_height )
    );

    auto [st_tex, st_surf, st_sub_rect] =
        ts.texture_atlas()->get_staging_area( sprite_width, sprite_height );

    SDL_SetSurfaceBlendMode( surf.get(), SDL_BLENDMODE_NONE );

    auto state = sdl_save_render_state( renderer.get() );
    for( const SDL_Rect src_rect : input_range ) {
        assert( offset.x % sprite_width == 0 );
        assert( offset.y % sprite_height == 0 );

        const point pos( offset + point( src_rect.x, src_rect.y ) );
        assert( pos.x % sprite_width == 0 );
        assert( pos.y % sprite_height == 0 );

        const int index =
            this->offset + ( pos.x / sprite_width ) +
            ( pos.y / sprite_height ) * ( tile_atlas_width / sprite_width );

        SDL_FillSurfaceRect( st_surf, nullptr, SDL_MapRGBA( SDL_GetPixelFormatDetails( st_surf->format ),
                             nullptr, 255, 255, 255, 0 ) );
        SDL_BlitSurface( surf.get(), &src_rect, st_surf, &st_sub_rect );

        const auto surf_hash = get_surface_hash( st_surf, nullptr );
        const auto existing = ts.tileset_atlas->id_search( surf_hash );

        atlas_texture atl_tex;
        if( existing.has_value() ) {
            atl_tex = existing.value();
        } else {
            atl_tex = ts.tileset_atlas->allocate_sprite( sprite_width, sprite_height );
            ts.tileset_atlas->id_assign( surf_hash, atl_tex );

            SDL_UpdateTexture( st_tex, nullptr, st_surf->pixels, st_surf->pitch );
            SDL_SetRenderTarget( renderer.get(), atl_tex.first.get() );
            const SDL_FRect fsrc{float( st_sub_rect.x ), float( st_sub_rect.y ), float( st_sub_rect.w ), float( st_sub_rect.h )};
            const SDL_FRect fdst{float( atl_tex.second.x ), float( atl_tex.second.y ), float( atl_tex.second.w ), float( atl_tex.second.h )};
            SDL_RenderTexture( renderer.get(), st_tex, &fsrc, &fdst );
            // Mirror to the GPU atlas so no-VFX tiles (fx_type::none cache-hit in
            // get_or_default) are visible in the GPU world-render pass.  Without
            // this the GPU sheet is allocated but never populated: the shader
            // samples uninitialised VRAM → solid magenta ground tiles.
            if( SDL_GPUTexture * gpu_atlas =
                    ts.tileset_atlas->find_gpu_texture( atl_tex.first.get() ) ) {
                lighting::get_render_state().upload_surface_subregion_to_gpu_texture(
                    gpu_atlas, atl_tex.second.x, atl_tex.second.y, st_surf, &st_sub_rect );
            }
        }

        const auto tex_key = tileset_lookup_key{index, TILESET_NO_MASK, tileset_fx_type::none,
                                                TILESET_NO_COLOR};
        auto& [at_tex, at_rect] = atl_tex;
        ts.tile_lookup.emplace( tex_key, texture( std::move( at_tex ), at_rect ) );
    }
    sdl_restore_render_state( renderer.get(), state );

    return true;
}
#endif
bool tileset_loader::create_textures_from_tile_atlas(
    const SDL_Surface_Ptr& tile_atlas, point offset )
{
    assert( tile_atlas );

#if defined(DYNAMIC_ATLAS)
    return copy_surface_to_dynamic_atlas( tile_atlas, offset );
#else

    using tiles_pixel_color_entry = std::tuple<std::vector<texture>*, std::string>;
    std::array<tiles_pixel_color_entry, 8> tile_values_data = {
        {   {std::make_tuple( &ts.tile_values, "color_pixel_none" )},
            {std::make_tuple( &ts.shadow_tile_values, "color_pixel_grayscale" )},
            {std::make_tuple( &ts.night_tile_values, "color_pixel_nightvision" )},
            {std::make_tuple( &ts.overexposed_tile_values, "color_pixel_overexposed" )},
            {std::make_tuple( &ts.underwater_tile_values, "color_pixel_underwater" )},
            {std::make_tuple( &ts.underwater_dark_tile_values, "color_pixel_underwater_dark" )},
            {std::make_tuple( &ts.z_overlay_values, "color_pixel_zoverlay" )},
            {std::make_tuple( &ts.memory_tile_values, tilecontext->memory_map_mode )}
        }
    };
    for( tiles_pixel_color_entry& entry : tile_values_data ) {
        std::vector<texture> *tile_values = std::get<0>( entry );
        color_pixel_function_pointer color_pixel_function = get_color_pixel_function(
                std::get<1>( entry ) );
        bool success;
        if( !color_pixel_function ) {
            success = copy_surface_to_texture( tile_atlas, offset, *tile_values );
        } else {
            success = copy_surface_to_texture(
                          apply_color_filter_blit_copy( tile_atlas, color_pixel_function ), offset,
                          *tile_values );
        }
        if( !success ) { return false; }
    }
    return true;
#endif
}

template <typename T>
static void extend_vector_by( std::vector<T> &vec, const size_t additional_size )
{
    vec.resize( vec.size() + additional_size );
}

void tileset_loader::load_tileset( const std::string& img_path, const bool pump_events )
{
    const SDL_Surface_Ptr tile_atlas = load_image( img_path.c_str() );
    assert( tile_atlas );
    tile_atlas_width = tile_atlas->w;

    if( R >= 0 && R <= 255 && G >= 0 && G <= 255 && B >= 0 && B <= 255 ) {
        const Uint32 key =
            SDL_MapRGB( SDL_GetPixelFormatDetails( tile_atlas->format ), nullptr, 0, 0, 0 );
        throwErrorIf(
            !SDL_SetSurfaceColorKey( tile_atlas.get(), true, key ),
            "SDL_SetSurfaceColorKey "
            "failed" );
        throwErrorIf( !SDL_SetSurfaceRLE( tile_atlas.get(), true ), "SDL_SetSurfaceRLE failed" );
    }

    const bool is_software_renderer =
        ( std::string_view( SDL_GetRendererName( renderer.get() ) ) == "software" );
    int max_texture_width;
    int max_texture_height;
    if( is_software_renderer ) {
        max_texture_width = sprite_width;
        max_texture_height = sprite_height;
    } else {
        const auto props = SDL_GetRendererProperties( renderer.get() );
        const int max_tex = static_cast<int>(
                                SDL_GetNumberProperty( props, SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0 ) );
        max_texture_width = max_tex;
        max_texture_height = max_tex;
    }

    const int min_tile_xcount = 128;
    const int min_tile_ycount = min_tile_xcount * 2;

    if( max_texture_width == 0 ) {
        max_texture_width = sprite_width * min_tile_xcount;
        dbg( DL::Info ) << "max_texture_width was set to 0.  Changing it to " << max_texture_width;
    } else {
        throwErrorIf(
            max_texture_width < sprite_width,
            "Maximal texture width is smaller than tile "
            "width" );
    }

    if( max_texture_height == 0 ) {
        max_texture_height = sprite_height * min_tile_ycount;
        dbg( DL::Info ) << "max_texture_height was set to 0.  Changing it to " << max_texture_height;
    } else {
        throwErrorIf(
            max_texture_height < sprite_height,
            "Maximal texture height is smaller than "
            "tile height" );
    }

    const int max_tile_xcount = max_texture_width / sprite_width;
    const int max_tile_ycount = max_texture_height / sprite_height;
    const rect_range<SDL_Rect> output_range(
        max_tile_xcount * sprite_width, max_tile_ycount * sprite_height,
        point( divide_round_up( tile_atlas->w, max_texture_width ),
               divide_round_up( tile_atlas->h, max_texture_height ) ) );

    const int expected_tilecount = ( tile_atlas->w / sprite_width ) * ( tile_atlas->h / sprite_height );

#if !defined(DYNAMIC_ATLAS)
    extend_vector_by( ts.tile_values, expected_tilecount );
    extend_vector_by( ts.shadow_tile_values, expected_tilecount );
    extend_vector_by( ts.night_tile_values, expected_tilecount );
    extend_vector_by( ts.overexposed_tile_values, expected_tilecount );
    extend_vector_by( ts.underwater_tile_values, expected_tilecount );
    extend_vector_by( ts.underwater_dark_tile_values, expected_tilecount );
    extend_vector_by( ts.z_overlay_values, expected_tilecount );
    extend_vector_by( ts.memory_tile_values, expected_tilecount );
#endif

    for( const SDL_Rect sub_rect : output_range ) {
        assert( sub_rect.x % sprite_width == 0 );
        assert( sub_rect.y % sprite_height == 0 );
        assert( sub_rect.w % sprite_width == 0 );
        assert( sub_rect.h % sprite_height == 0 );
        SDL_Surface_Ptr smaller_surf;

        if( is_contained( SDL_Rect{0, 0, tile_atlas->w, tile_atlas->h}, sub_rect ) ) {
        } else {
            const int w = std::min( tile_atlas->w - sub_rect.x, sub_rect.w );
            const int h = std::min( tile_atlas->h - sub_rect.y, sub_rect.h );
            smaller_surf = ::create_surface_32( w, h );
            assert( smaller_surf );
            const SDL_Rect inp{sub_rect.x, sub_rect.y, w, h};
            throwErrorIf( !SDL_BlitSurface( tile_atlas.get(), &inp, smaller_surf.get(), nullptr ),
                          "SDL_BlitSurface failed" );
        }
        const SDL_Surface_Ptr& surf_to_use = smaller_surf ? smaller_surf : tile_atlas;
        assert( surf_to_use );

        if( !create_textures_from_tile_atlas( surf_to_use, point( sub_rect.x, sub_rect.y ) ) ) {
            throw std::runtime_error( _(
                                          "Failed to create texture atlas, see debug.log for details.  "
                                          "This commonly happens if the device is low on memory.  "
                                          "Try rebooting device, or using another tileset or a different renderer." ) );
        }

        if( pump_events ) { inp_mngr.pump_events(); }
    }

    size = expected_tilecount;
}
void tileset_loader::load(
    const std::string& tileset_id, const bool precheck, const bool pump_events )
{
    std::string json_conf;
    std::string tileset_path;
    std::string tileset_root;

    const auto tset_iter = TILESETS.find( tileset_id );
    if( tset_iter != TILESETS.end() ) {
        tileset_root = tset_iter->second;
        dbg( DL::Info ) << '"' << tileset_id << '"'
                        << " tileset: found config file path: " << tileset_root;
        get_tile_information(
            tileset_root + '/' + PATH_INFO::tileset_conf(), json_conf, tileset_path );
        dbg( DL::Info ) << "Current tileset is: " << tileset_id;
    } else {
        dbg( DL::Error ) << "Tileset \"" << tileset_id << "\" from options is invalid";
        json_conf = PATH_INFO::defaulttilejson();
        tileset_path = PATH_INFO::defaulttilepng();
    }

    std::string json_path = tileset_root + '/' + json_conf;
    std::string img_path = tileset_root + '/' + tileset_path;

    dbg( DL::Info ) << "Attempting to Load JSON file " << json_path;
    std::ifstream config_file( json_path.c_str(), std::ifstream::in | std::ifstream::binary );

    if( !config_file.good() ) {
        throw std::runtime_error( std::string( "Failed to open tile info json: " ) + json_path );
    }

    JsonIn config_json( config_file );
    JsonObject config = config_json.get_object();

    if( !config.has_member( "tile_info" ) ) { config.throw_error( "\"tile_info\" missing" ); }

    for( const JsonObject& curr_info : config.get_array( "tile_info" ) ) {
        ts.tile_height = curr_info.get_int( "height" );
        ts.tile_width = curr_info.get_int( "width" );
        ts.zlevel_height = curr_info.get_int( "zlevel_height", 0 );
        ts.zlevel_height = 0;
        tile_iso = curr_info.get_bool( "iso", false );
        ts.tile_pixelscale = curr_info.get_float( "pixelscale", 1.0f );
        ts.prevent_occlusion_min_dist = curr_info.get_float( "retract_dist_min", -1.0f );
        ts.prevent_occlusion_min_dist = -1.0f;
        ts.prevent_occlusion_max_dist = curr_info.get_float( "retract_dist_max", 0.0f );
        ts.prevent_occlusion_max_dist = 0.0f;
    }

    if( precheck ) {
        config.allow_omitted_members();
        return;
    }
#if defined(DYNAMIC_ATLAS)
    ts.tileset_atlas = std::make_unique<dynamic_atlas>( 4096, 4096, ts.tile_width, ts.tile_height );
#endif
    if( config.has_array( "depth_extrude_presets" ) ) {
        for( const JsonObject& obj : config.get_array( "depth_extrude_presets" ) ) {
            depth_extrude_preset p;
            p.name = obj.get_string( "name" );
            p.extrude_px = obj.get_float( "extrude_px", 0.0f );
            p.extrude_dark = obj.get_float( "extrude_dark", 0.0f );
            p.extrude_lean = obj.get_float( "extrude_lean", 0.0f );
            if( !p.name.empty() ) { depth_extrude_presets_.push_back( std::move( p ) ); }
        }
    }
    if( config.has_array( "depth_extrude_rules" ) ) {
        for( const JsonObject& obj : config.get_array( "depth_extrude_rules" ) ) {
            depth_extrude_rule rule;
            if( obj.has_string( "id_prefix" ) ) { rule.id_prefix = obj.get_string( "id_prefix" ); }
            if( obj.has_bool( "disable" ) ) { rule.disable = obj.get_bool( "disable", false ); }
            rule.min_height_3d = obj.get_int( "min_height_3d", 0 );
            if( obj.has_string( "preset" ) ) {
                const auto nm = obj.get_string( "preset" );
                for( const auto& p : depth_extrude_presets_ ) {
                    if( p.name == nm ) {
                        rule.extrude_px = p.extrude_px;
                        rule.extrude_dark = p.extrude_dark;
                        rule.extrude_lean = p.extrude_lean;
                        break;
                    }
                }
            } else {
                rule.extrude_px = obj.get_float( "extrude_px", 0.0f );
                rule.extrude_dark = obj.get_float( "extrude_dark", 0.0f );
                rule.extrude_lean = obj.get_float( "extrude_lean", 0.0f );
            }
            if( !rule.id_prefix.empty() || rule.min_height_3d > 0 ) {
                depth_extrude_rules_.push_back( std::move( rule ) );
            }
        }
        has_depth_extrude_rules_ = !depth_extrude_rules_.empty();
    }

    offset = 0;
    load_internal( config, tileset_root, img_path, pump_events );

    for( const mod_tileset& mts : all_mod_tilesets ) {
        sprite_id_offset = offset;
        tileset_root = mts.get_base_path();
        json_path = mts.get_full_path();

        if( !mts.is_compatible( tileset_id ) ) {
            dbg( DL::Info ) << "Mod tileset in \"" << json_path << "\" is not compatible.";
            continue;
        }
        dbg( DL::Info ) << "Attempting to Load JSON file " << json_path;
        std::ifstream mod_config_file( json_path.c_str(), std::ifstream::in | std::ifstream::binary );

        if( !mod_config_file.good() ) {
            throw std::runtime_error( std::string( "Failed to open tile info json: " ) + json_path );
        }

        JsonIn mod_config_json( mod_config_file );

        const auto mark_visited = []( const JsonObject & jobj ) {
            jobj.get_string_array( "compatibility" );
        };

        int num_in_file = 1;
        if( mod_config_json.test_array() ) {
            for( const JsonObject& mod_config : mod_config_json.get_array() ) {
                if( mod_config.get_string( "type" ) == "mod_tileset" ) {
                    if( num_in_file == mts.num_in_file() ) {
                        mark_visited( mod_config );
                        load_internal( mod_config, tileset_root, img_path, pump_events );
                        break;
                    }
                    num_in_file++;
                }
                mod_config.allow_omitted_members();
            }
        } else {
            JsonObject mod_config = mod_config_json.get_object();
            mark_visited( mod_config );
            load_internal( mod_config, tileset_root, img_path, pump_events );
        }
    }

    for( auto it = ts.tile_ids.begin(); it != ts.tile_ids.end(); ) {
        auto& td = it->second;
        process_variations_after_loading( td.sprite.fg );
        process_variations_after_loading( td.sprite.bg );
        if( td.sprite.bg.empty() && td.sprite.fg.empty() ) {
            dbg( DL::Warn ) << "tile " << it->first << " has no (valid) foreground nor background";
            for( auto& container : ts.tile_ids_by_season ) {
                if( container.contains( it->first ) ) { container.erase( it->first ); }
            }
            ts.tile_ids.erase( it++ );
        } else {
            ++it;
        }
    }

    if( has_depth_extrude_rules_ ) {
        namespace ranges = std::ranges;
        for( auto& [id, tile] : ts.tile_ids ) {
            if( tile.depth_extrude_px > 0.0f ) { continue; }
            for( const auto& rule : depth_extrude_rules_ ) {
                bool matches = true;
                if( !rule.id_prefix.empty() && !id.starts_with( rule.id_prefix ) ) { matches = false; }
                if( rule.min_height_3d > 0 && tile.height_3d < rule.min_height_3d ) {
                    matches = false;
                }
                if( !matches ) { continue; }
                if( !rule.disable ) {
                    tile.depth_extrude_px = rule.extrude_px;
                    tile.depth_extrude_dark = rule.extrude_dark;
                    tile.depth_extrude_lean = rule.extrude_lean;
                }
                break;
            }
        }
    }

    if( !ts.find_tile_type( "unknown" ) ) {
        dbg( DL::Warn ) << "The tileset you're using has no 'unknown' tile defined!";
    }
    ensure_default_item_highlight();

    ts.tileset_id = tileset_id;
#if defined(DYNAMIC_ATLAS)
    ts.tileset_atlas->readback_load();
#endif
}

void tileset_loader::load_internal(
    const JsonObject& config, const std::string& tileset_root, const std::string& img_path,
    const bool pump_events )
{
    if( config.has_array( "tiles-new" ) ) {
        for( const JsonObject& tile_part_def : config.get_array( "tiles-new" ) ) {
            const std::string tileset_image_path =
                tileset_root + '/' + tile_part_def.get_string( "file" );
            R = -1;
            G = -1;
            B = -1;
            if( tile_part_def.has_object( "transparency" ) ) {
                JsonObject tra = tile_part_def.get_object( "transparency" );
                R = tra.get_int( "R" );
                G = tra.get_int( "G" );
                B = tra.get_int( "B" );
            }
            sprite_width = tile_part_def.get_int( "sprite_width", ts.tile_width );
            sprite_height = tile_part_def.get_int( "sprite_height", ts.tile_height );
            sprite_offset.x = tile_part_def.get_int( "sprite_offset_x", 0 );
            sprite_offset.y = tile_part_def.get_int( "sprite_offset_y", 0 );
            sprite_offset_retracted.x =
                tile_part_def.get_int( "sprite_offset_x_retracted", sprite_offset.x );
            sprite_offset_retracted.y =
                tile_part_def.get_int( "sprite_offset_y_retracted", sprite_offset.y );
            sprite_pixelscale = tile_part_def.get_float( "pixelscale", 1.0f );
            const auto empty_ascii_fallback =
                tile_part_def.has_array( "ascii" ) && tile_part_def.has_array( "tiles" )
                && tile_part_def.get_array( "tiles" ).empty();
            if( !file_exist( tileset_image_path ) && empty_ascii_fallback ) {
                dbg( DL::Warn ) << "Skipping missing empty ASCII fallback tilesheet "
                                << tileset_image_path;
                tile_part_def.get_array( "ascii" );
                continue;
            }
            dbg( DL::Info ) << "Attempting to Load Tileset file " << tileset_image_path;
            load_tileset( tileset_image_path, pump_events );
            load_tilejson_from_file( tile_part_def );
            if( tile_part_def.has_member( "ascii" ) ) { load_ascii( tile_part_def ); }
            if( tile_part_def.has_array( "state-modifiers" ) ) { load_state_modifiers( tile_part_def ); }
            if( tile_part_def.has_array( "global-warp-whitelist" ) ) {
                ts.global_warp_whitelist.clear();
                for( const std::string& prefix : tile_part_def.get_array( "global-warp-whitelist" ) ) {
                    ts.global_warp_whitelist.push_back( prefix );
                }
            }
            if( tile_part_def.has_array( "global-warp-blacklist" ) ) {
                ts.global_warp_blacklist.clear();
                for( const std::string& prefix : tile_part_def.get_array( "global-warp-blacklist" ) ) {
                    ts.global_warp_blacklist.push_back( prefix );
                }
            }
            offset += size;
            if( pump_events ) { inp_mngr.pump_events(); }
        }
    } else if( config.has_array( "tiles" ) ) {
        sprite_width = ts.tile_width;
        sprite_height = ts.tile_height;
        sprite_offset = point_zero;
        sprite_offset_retracted = point_zero;
        sprite_pixelscale = 1.0f;
        R = -1;
        G = -1;
        B = -1;
        dbg( DL::Info ) << "Attempting to Load Tileset file " << img_path;
        load_tileset( img_path, pump_events );
        load_tilejson_from_file( config );
        offset = size;
    }

    if( config.has_array( "overlay_ordering" ) ) {
        load_overlay_ordering_into_array( config, tileset_mutation_overlay_ordering );
    }

    if( config.has_array( "tints" ) ) {
        const auto& colors = get_all_colors();
        struct color_parse_result {
            std::optional<SDL_Color> color;
            std::optional<float> brightness;
        };
        auto parse_color = [&colors]( const std::string & color_str ) -> color_parse_result {
            if( color_str.empty() ) { return {std::nullopt, std::nullopt}; }
        if( color_str.starts_with( '#' ) )
        {
            const std::string hex_part = color_str.substr( 1 );
                for( const char c : hex_part ) {
                    if( !std::isxdigit( c ) ) { return {std::nullopt, std::nullopt}; }
                }
                const auto tmp_color = RGBColor::try_parse( color_str );
                if( tmp_color.has_value() ) {
                    if( hex_part.size() == 6 ) {
                        return {.color = tmp_color, .brightness = std::nullopt};
                    }
                    if( hex_part.size() == 8 ) {
                        const float brightness = static_cast<float>( tmp_color->a ) / 128.0f;
                        const auto color = RGBColor( tmp_color->r, tmp_color->g, tmp_color->b, 255 );
                        return {.color = color, .brightness = brightness};
                    }
                }
                return {std::nullopt, std::nullopt};
            }
            const nc_color curse_color = colors.name_to_color( color_str );
            if( curse_color == c_unset ) { return {std::nullopt, std::nullopt}; }
        return {static_cast<SDL_Color>( curses_color_to_RGB( curse_color ) ), std::nullopt};
        };

        auto parse_blend_mode = []( const std::string & str ) -> tint_blend_mode {
            return string_to_tint_blend_mode( str );
        };

        auto parse_tint_config =
            [&parse_color, &parse_blend_mode](
                const JsonObject & obj, const std::string & key, bool has_top_level,
                tint_blend_mode top_blend_mode, std::optional<float> top_contrast,
                std::optional<float> top_saturation, std::optional<float> top_brightness )
        -> tint_config {
            tint_config cfg{};
            if( !obj.has_member( key ) ) { return cfg; }

            if( obj.has_string( key ) )
            {
                auto [color, brightness] = parse_color( obj.get_string( key ) );
                cfg.color = color.value_or( TILESET_NO_COLOR );
                cfg.brightness = brightness.value_or( 1.0f );
                cfg.blend_mode = top_blend_mode;
                if( has_top_level ) {
                    cfg.contrast = top_contrast.value_or( 1.0f );
                    cfg.saturation = top_saturation.value_or( 1.0f );
                    cfg.brightness = top_brightness.value_or( 1.0f );
                }
            } else if( obj.has_object( key ) && !has_top_level )
            {
                JsonObject color_obj = obj.get_object( key );
                auto [color, brightness] = parse_color( color_obj.get_string( "color", "" ) );
                cfg.color = color.value_or( TILESET_NO_COLOR );
                cfg.brightness = brightness.value_or( 1.0f );
                cfg.blend_mode = parse_blend_mode( color_obj.get_string( "blend_mode", "" ) );
                cfg.contrast = color_obj.get_float( "contrast", 1.0f );
                cfg.saturation = color_obj.get_float( "saturation", 1.0f );
                if( color_obj.has_float( "brightness" ) ) {
                    cfg.brightness = color_obj.get_float( "brightness" );
                }
            }
            return cfg;
        };

        for( const JsonObject& tint_def : config.get_array( "tints" ) ) {
            const std::string mut_id = tint_def.get_string( "id" );
            if( mut_id.empty() ) { continue; }

            std::optional<float> top_contrast;
            std::optional<float> top_saturation;
            std::optional<float> top_brightness;
            tint_blend_mode top_blend_mode = parse_blend_mode(
                                                 tint_def.get_string( "blend_mode", "" ) );
            const bool has_top_level =
                tint_def.has_float( "contrast" ) || tint_def.has_float( "saturation" );
            if( tint_def.has_float( "contrast" ) ) { top_contrast = tint_def.get_float( "contrast" ); }
            if( tint_def.has_float( "saturation" ) ) {
                top_saturation = tint_def.get_float( "saturation" );
            }
            if( tint_def.has_float( "brightness" ) ) {
                top_brightness = tint_def.get_float( "brightness" );
            }

            tint_config fg = parse_tint_config(
                                 tint_def, "fg", has_top_level, top_blend_mode, top_contrast, top_saturation,
                                 top_brightness );
            tint_config bg = parse_tint_config(
                                 tint_def, "bg", has_top_level, top_blend_mode, top_contrast, top_saturation,
                                 top_brightness );

            if( fg.has_value() || bg.has_value() ) { ts.tints[mut_id] = {bg, fg}; }
        }
    }

    if( config.has_array( "tint_pairs" ) ) {
        for( const JsonObject& tint_def : config.get_array( "tint_pairs" ) ) {
            const std::string source_type = tint_def.get_string( "source_type" );
            const std::string target_type = tint_def.get_string( "target_type" );
            const bool override = tint_def.get_bool( "override", false );
            if( source_type.empty() || target_type.empty() ) { continue; }
            ts.tint_pairs[target_type] = {source_type, override};
        }
    }
}

void tileset_loader::process_variations_after_loading( weighted_int_list<std::vector<int>> &vs )
{
    for( auto& v : vs ) {
        v.obj.erase(
            std::remove_if(
        v.obj.begin(), v.obj.end(), [&]( int id ) { return id >= offset || id < 0; } ),
        v.obj.end() );
    }
    vs.erase(
        std::remove_if(
            vs.begin(), vs.end(),
    [&]( const weighted_object<int, std::vector<int>> &o ) { return o.obj.empty(); } ),
    vs.end() );
    vs.precalc();
}

void tileset_loader::add_ascii_subtile(
    tile_type& curr_tile, const std::string& t_id, int sprite_id, const std::string& s_id )
{
    const std::string m_id = t_id + "_" + s_id;
    tile_type curr_subtile;
    curr_subtile.sprite.fg.add( std::vector<int>( {sprite_id} ), 1 );
    curr_subtile.masks.tint.fg.add( std::vector<int>( {TILESET_NO_MASK} ), 1 );
    curr_subtile.offset = sprite_offset;
    curr_subtile.offset_retracted = sprite_offset_retracted;
    curr_subtile.pixelscale = sprite_pixelscale;
    curr_subtile.rotates = true;
    curr_tile.available_subtiles.push_back( s_id );
    ts.create_tile_type( m_id, std::move( curr_subtile ) );
}

void tileset_loader::load_ascii( const JsonObject& config )
{
    if( !config.has_member( "ascii" ) ) { config.throw_error( "\"ascii\" section missing" ); }
    for( const JsonObject& entry : config.get_array( "ascii" ) ) { load_ascii_set( entry ); }
}

void tileset_loader::load_ascii_set( const JsonObject& entry )
{
    const int in_image_offset = entry.get_int( "offset" );
    if( in_image_offset >= size ) { entry.throw_error( "invalid offset (out of range)", "offset" ); }
    int FG = -1;
    const std::string scolor = entry.get_string( "color", "DEFAULT" );
    if( scolor == "BLACK" ) {
        FG = catacurses::black;
    } else if( scolor == "RED" ) {
        FG = catacurses::red;
    } else if( scolor == "GREEN" ) {
        FG = catacurses::green;
    } else if( scolor == "YELLOW" ) {
        FG = catacurses::yellow;
    } else if( scolor == "BLUE" ) {
        FG = catacurses::blue;
    } else if( scolor == "MAGENTA" ) {
        FG = catacurses::magenta;
    } else if( scolor == "CYAN" ) {
        FG = catacurses::cyan;
    } else if( scolor == "WHITE" ) {
        FG = catacurses::white;
    } else if( scolor == "DEFAULT" ) {
        FG = -1;
    } else {
        entry.throw_error( "invalid color for ASCII", "color" );
    }
    if( FG != -1 && entry.get_bool( "bold", false ) ) { FG += 8; }
    const int base_offset = offset + in_image_offset;

    for( int ascii_char = 0; ascii_char < 256; ascii_char++ ) {
        const int index_in_image = ascii_char + in_image_offset;
        if( index_in_image < 0 || index_in_image >= size ) {
            continue;
        }
        const std::string id = get_ascii_tile_id( ascii_char, FG, -1 );
        tile_type curr_tile;
        curr_tile.offset = sprite_offset;
        curr_tile.offset_retracted = sprite_offset_retracted;
        curr_tile.pixelscale = sprite_pixelscale;
        curr_tile.masks.tint.fg.add( std::vector<int>( {TILESET_NO_MASK} ), 1 );
        auto& sprites = *( curr_tile.sprite.fg.add( std::vector<int>( {index_in_image + offset} ), 1 ) );
        switch( ascii_char ) {
            case LINE_OXOX_C:
                sprites[0] = 205 + base_offset;
                break;
            case LINE_XOXO_C:
                sprites[0] = 186 + base_offset;
                break;
            case LINE_OXXO_C:
                sprites[0] = 201 + base_offset;
                break;
            case LINE_OOXX_C:
                sprites[0] = 187 + base_offset;
                break;
            case LINE_XOOX_C:
                sprites[0] = 188 + base_offset;
                break;
            case LINE_XXOO_C:
                sprites[0] = 200 + base_offset;
                break;
            case LINE_XXOX_C:
                sprites[0] = 202 + base_offset;
                break;
            case LINE_XXXO_C:
                sprites[0] = 208 + base_offset;
                break;
            case LINE_OXXX_C:
                sprites[0] = 203 + base_offset;
                break;
            case LINE_XXXX_C:
                sprites[0] = 206 + base_offset;
                break;
            case LINE_XOXX_C:
                sprites[0] = 184 + base_offset;
                break;
        }
        if( ascii_char == LINE_XOXO_C || ascii_char == LINE_OXOX_C ) {
            curr_tile.rotates = false;
            curr_tile.multitile = true;
            add_ascii_subtile( curr_tile, id, 206 + base_offset, "center" );
            add_ascii_subtile( curr_tile, id, 201 + base_offset, "corner" );
            add_ascii_subtile( curr_tile, id, 186 + base_offset, "edge" );
            add_ascii_subtile( curr_tile, id, 203 + base_offset, "t_connection" );
            add_ascii_subtile( curr_tile, id, 210 + base_offset, "end_piece" );
            add_ascii_subtile( curr_tile, id, 219 + base_offset, "unconnected" );
        }
        ts.create_tile_type( id, std::move( curr_tile ) );
    }
}

void tileset_loader::load_tilejson_from_file( const JsonObject& config )
{
    if( !config.has_member( "tiles" ) ) { config.throw_error( "\"tiles\" section missing" ); }

    for( const JsonObject& entry : config.get_array( "tiles" ) ) {
        std::vector<std::string> ids;
        if( entry.has_string( "id" ) ) {
            ids.push_back( entry.get_string( "id" ) );
        } else if( entry.has_array( "id" ) ) {
            ids = entry.get_string_array( "id" );
        }
        for( const std::string& t_id : ids ) {
            tile_type& curr_tile = load_tile( entry, t_id );
            curr_tile.offset = sprite_offset;
            curr_tile.offset_retracted = sprite_offset_retracted;
            curr_tile.pixelscale = sprite_pixelscale;
            const bool t_multi = entry.get_bool( "multitile", false );
            const bool t_rota = entry.get_bool( "rotates", t_multi );
            const int t_h3d = entry.get_int( "height_3d", 0 );
            const auto t_flags = entry.get_tags<flag_id>( "flags" );
            std::optional<RGBColor> t_tint;
            entry.read( "default_tint", t_tint );

            if( t_multi ) {
                for( const JsonObject& subentry : entry.get_array( "additional_tiles" ) ) {
                    const std::string s_id = subentry.get_string( "id" );
                    const std::string m_id = t_id + "_" + s_id;
                    tile_type& curr_subtile = load_tile( subentry, m_id );
                    curr_subtile.offset = sprite_offset;
                    curr_subtile.offset_retracted = sprite_offset_retracted;
                    curr_subtile.pixelscale = sprite_pixelscale;
                    curr_subtile.rotates = true;
                    curr_subtile.is_multitile_subtile =
                        std::ranges::find( multitile_keys, s_id ) != multitile_keys.end();
                    curr_subtile.height_3d = t_h3d;
                    curr_subtile.animated = subentry.get_bool( "animated", false );
                    curr_subtile.default_tint = t_tint;
                    curr_subtile.flags = t_flags;
                    curr_tile.available_subtiles.push_back( s_id );
                }
            } else if( entry.has_array( "additional_tiles" ) ) {
                entry.throw_error( "Additional tiles defined, but 'multitile' is not true." );
            }

            curr_tile.multitile = t_multi;
            curr_tile.rotates = t_rota;
            curr_tile.height_3d = t_h3d;
            curr_tile.default_tint = t_tint;
            curr_tile.flags = t_flags;
            curr_tile.is_multitile_subtile = false;
            curr_tile.animated = entry.get_bool( "animated", false );
        }
    }
    dbg( DL::Info ) << "Tile Width: " << ts.tile_width << " Tile Height: " << ts.tile_height
                    << " Tile Definitions: " << ts.tile_ids.size();
}

tile_type &tileset_loader::load_tile( const JsonObject& entry, const std::string& id )
{
    tile_type curr_subtile;

    load_tile_spritelists( entry, curr_subtile.sprite.fg, "fg" );
    load_tile_spritelists( entry, curr_subtile.sprite.bg, "bg" );

    if( entry.has_array( "masks" ) ) {
        for( const JsonObject mask_entry : entry.get_array( "masks" ) ) {
            const auto mask_type = mask_entry.get_string( "type" );
            if( mask_type == "tint" ) {
                load_tile_spritelists( mask_entry, curr_subtile.masks.tint.fg, "fg" );
                load_tile_spritelists( mask_entry, curr_subtile.masks.tint.bg, "bg" );
            } else {
                debugmsg_of( DL::Warn, "Invalid tile mask type: %s", mask_type );
            }
        }
    }

    using vslist = tile_type::sprite_list;
    auto ensure_mask = [&]( vslist & mask, const vslist & sprite ) {
        if( !mask.empty() ) {
            for( const auto& [a, b] : std::views::zip( mask, sprite ) ) {
                if( ( a.weight != b.weight ) || ( a.obj.size() != b.obj.size() ) ) {
                    debugmsg( "Tile mask definition must match sprite: %s", id );
                    mask.clear();
                }
            }
        }

        if( mask.empty() ) {
            for( auto& l : sprite ) {
                auto tmp = std::vector<int>( l.obj.size(), TILESET_NO_MASK );
                mask.add( tmp, l.weight );
            }
        }
    };

    ensure_mask( curr_subtile.masks.tint.fg, curr_subtile.sprite.fg );
    ensure_mask( curr_subtile.masks.tint.bg, curr_subtile.sprite.bg );

    curr_subtile.has_om_transparency = entry.get_bool( "has_om_transparency", false );

    return ts.create_tile_type( id, std::move( curr_subtile ) );
}

void tileset_loader::load_tile_spritelists(
    const JsonObject& entry, weighted_int_list<std::vector<int>> &vs, const std::string& objname )
{
    if( entry.has_array( objname ) ) {
        JsonArray g_array = entry.get_array( objname );
        if( g_array.test_int() ) {
            std::vector<int> v;
            for( const int entry : g_array ) {
                const int sprite_id = entry + sprite_id_offset;
                if( sprite_id >= 0 ) { v.push_back( sprite_id ); }
            }
            vs.add( v, 1 );
        } else if( g_array.test_object() ) {
            for( const JsonObject& vo : g_array ) {
                std::vector<int> v;
                int weight = vo.get_int( "weight" );
                if( weight < 0 ) {
                    vo.throw_error( "Invalid weight for sprite variation (<0)", objname );
                }
                if( vo.has_int( "sprite" ) ) {
                    const int sprite_id = vo.get_int( "sprite" ) + sprite_id_offset;
                    if( sprite_id >= 0 ) { v.push_back( sprite_id ); }
                } else if( vo.has_array( "sprite" ) ) {
                    for( const int entry : vo.get_array( "sprite" ) ) {
                        const int sprite_id = entry + sprite_id_offset;
                        if( sprite_id >= 0 ) { v.push_back( sprite_id ); }
                    }
                }
                if( v.size() != 1 && v.size() != 2 && v.size() != 4 ) {
                    vo.throw_error( "Invalid number of sprites (not 1, 2, or 4)", objname );
                }
                vs.add( v, weight );
            }
        }
    } else if( entry.has_int( objname ) && entry.get_int( objname ) >= 0 ) {
        vs.add( std::vector<int>( {entry.get_int( objname ) + sprite_id_offset} ), 1 );
    }
}

void tileset_loader::load_state_modifiers( const JsonObject& config )
{
    if( !config.has_array( "state-modifiers" ) ) { return; }

    for( const JsonObject& mod_group : config.get_array( "state-modifiers" ) ) {
        state_modifier_group group;
        group.group_id = mod_group.get_string( "id" );
        group.override_lower = mod_group.get_bool( "override", false );
        group.use_offset_mode = mod_group.get_bool( "use_offset", true );

        if( mod_group.has_array( "whitelist" ) ) {
            for( const std::string& prefix : mod_group.get_array( "whitelist" ) ) {
                group.whitelist.push_back( prefix );
            }
        }
        if( mod_group.has_array( "blacklist" ) ) {
            for( const std::string& prefix : mod_group.get_array( "blacklist" ) ) {
                group.blacklist.push_back( prefix );
            }
        }

        if( !mod_group.has_array( "tiles" ) ) {
            mod_group.throw_error( "state-modifier group must have a 'tiles' array" );
        }

        for( const JsonObject& tile_entry : mod_group.get_array( "tiles" ) ) {
            state_modifier_tile tile;
            tile.state_id = tile_entry.get_string( "id" );

            if( tile_entry.has_null( "fg" ) ) {
                tile.fg_sprite = std::nullopt;
            } else if( tile_entry.has_int( "fg" ) ) {
                const int fg_val = tile_entry.get_int( "fg" );
                if( fg_val >= 0 ) {
                    tile.fg_sprite = fg_val + sprite_id_offset;
                } else {
                    tile.fg_sprite = std::nullopt;
                }
            } else {
                tile.fg_sprite = std::nullopt;
            }

            tile.offset.x = tile_entry.get_int( "offset_x", sprite_offset.x );
            tile.offset.y = tile_entry.get_int( "offset_y", sprite_offset.y );

            group.tiles[tile.state_id] = std::move( tile );
        }

        std::sort( group.whitelist.begin(), group.whitelist.end() );
        group.whitelist.erase(
            std::unique( group.whitelist.begin(), group.whitelist.end() ), group.whitelist.end() );
        std::sort( group.blacklist.begin(), group.blacklist.end() );
        group.blacklist.erase(
            std::unique( group.blacklist.begin(), group.blacklist.end() ), group.blacklist.end() );

        auto existing = std::find_if(
                            ts.state_modifiers.begin(), ts.state_modifiers.end(),
        [&group]( const state_modifier_group & g ) {
            return g.group_id == group.group_id && g.whitelist == group.whitelist
                   && g.blacklist == group.blacklist;
        } );

        if( existing != ts.state_modifiers.end() ) {
            *existing = std::move( group );
            dbg( DL::Info ) << "Replaced state modifier group: " << existing->group_id;
        } else {
            ts.state_modifiers.push_back( std::move( group ) );
        }
    }

    dbg( DL::Info ) << "Total state modifier groups: " << ts.state_modifiers.size();
}

void tileset_loader::ensure_default_item_highlight()
{
    if( ts.find_tile_type( ITEM_HIGHLIGHT ) ) { return; }
#if defined(DYNAMIC_ATLAS)
    const Uint8 highlight_alpha = 127;

    int index = offset;

    const SDL_Surface_Ptr surface = create_surface_32( ts.tile_width, ts.tile_height );
    assert( surface );
    throwErrorIf(
        !SDL_FillSurfaceRect(
            surface.get(), nullptr,
            SDL_MapRGBA( SDL_GetPixelFormatDetails( surface->format ), nullptr, 0, 0, 127,
                         highlight_alpha ) ),
        "SDL_FillSurfaceRect failed" );

    auto [tex, rect] = ts.tileset_atlas->allocate_sprite( ts.tile_width, ts.tile_height );
    SDL_UpdateTexture( tex.get(), &rect, surface->pixels, surface->pitch );
    // Mirror to GPU atlas — same reason as copy_surface_to_dynamic_atlas.
    if( SDL_GPUTexture * gpu = ts.tileset_atlas->find_gpu_texture( tex.get() ) ) {
        lighting::get_render_state().upload_surface_subregion_to_gpu_texture(
            gpu, rect.x, rect.y, surface.get(), nullptr );
    }

    ts.tile_ids[ITEM_HIGHLIGHT].sprite.fg.add( std::vector<int>( {index} ), 1 );
    ts.tile_lookup.emplace(
        tileset_lookup_key{index, TILESET_NO_MASK, tileset_fx_type::none, TILESET_NO_COLOR,
                           TILESET_NO_WARP, point_zero},
        tileset::tile_lookup_entry{texture( tex, rect ), point_zero} );
#else
    const Uint8 highlight_alpha = 127;

    int index = ts.tile_values.size();

    const SDL_Surface_Ptr surface = create_surface_32( ts.tile_width, ts.tile_height );
    assert( surface );
    throwErrorIf(
        !SDL_FillSurfaceRect(
            surface.get(), nullptr,
            SDL_MapRGBA( SDL_GetPixelFormatDetails( surface->format ), nullptr, 0, 0, 127,
                         highlight_alpha ) ),
        "SDL_FillSurfaceRect failed" );
    ts.tile_values.emplace_back(
        CreateTextureFromSurface( renderer, surface ), SDL_Rect{0, 0, ts.tile_width, ts.tile_height} );
    ts.tile_ids[ITEM_HIGHLIGHT].sprite.fg.add( std::vector<int>( {index} ), 1 );
#endif
}
