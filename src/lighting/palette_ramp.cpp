#include "palette_ramp.h"

#include <SDL3/SDL_surface.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace lighting
{

namespace
{

struct rgb_f {
    float r, g, b;
};
struct hsv_f {
    float h, s, v;
};
struct oklab_f {
    float l, a, b;
};

auto srgb_to_linear( float c ) -> float
{
    return c <= 0.04045f ? c / 12.92f : std::pow( ( c + 0.055f ) / 1.055f, 2.4f );
}

/// sRGB -> OkLab (Björn Ottosson). Perceptually uniform, so nearest-neighbour here
/// picks the row a human would call "the same colour" instead of the nearest byte
/// triple. No existing equivalent in the repo.
auto srgb_to_oklab( const rgb_f &c ) -> oklab_f
{
    const float r = srgb_to_linear( c.r );
    const float g = srgb_to_linear( c.g );
    const float b = srgb_to_linear( c.b );
    const float l = std::cbrt( 0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b );
    const float m = std::cbrt( 0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b );
    const float s = std::cbrt( 0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b );
    return { .l = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
             .a = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
             .b = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s };
}

auto rgb_to_hsv( const rgb_f &c ) -> hsv_f
{
    const float mx = std::max( { c.r, c.g, c.b } );
    const float mn = std::min( { c.r, c.g, c.b } );
    const float d = mx - mn;
    float h = 0.0f;
    if( d > 1e-6f ) {
        if( mx == c.r ) {
            h = std::fmod( ( c.g - c.b ) / d, 6.0f );
        } else if( mx == c.g ) {
            h = ( c.b - c.r ) / d + 2.0f;
        } else {
            h = ( c.r - c.g ) / d + 4.0f;
        }
        h /= 6.0f;
        if( h < 0.0f ) { h += 1.0f; }
    }
    return { .h = h, .s = mx > 1e-6f ? d / mx : 0.0f, .v = mx };
}

auto hsv_to_rgb( const hsv_f &c ) -> rgb_f
{
    const float h = ( c.h - std::floor( c.h ) ) * 6.0f;
    const float f = h - std::floor( h );
    const float p = c.v * ( 1.0f - c.s );
    const float q = c.v * ( 1.0f - c.s * f );
    const float t = c.v * ( 1.0f - c.s * ( 1.0f - f ) );
    switch( static_cast<int>( h ) % 6 ) {
        case 0: return { .r = c.v, .g = t, .b = p };
        case 1: return { .r = q, .g = c.v, .b = p };
        case 2: return { .r = p, .g = c.v, .b = t };
        case 3: return { .r = p, .g = q, .b = c.v };
        case 4: return { .r = t, .g = p, .b = c.v };
        default: return { .r = c.v, .g = p, .b = q };
    }
}

auto pack_rgba8( const rgb_f &c ) -> std::uint32_t
{
    const auto q = []( float v ) -> std::uint32_t {
        return static_cast<std::uint32_t>( std::clamp( v, 0.0f, 1.0f ) * 255.0f + 0.5f );
    };
    return q( c.r ) | ( q( c.g ) << 8 ) | ( q( c.b ) << 16 ) | 0xFF000000u;
}

} // namespace

auto palette_accumulator::add_surface( const SDL_Surface &s ) -> void
{
    SDL_Surface &src = const_cast<SDL_Surface &>( s );
    // Normalise to a known layout rather than decoding every possible source format.
    SDL_Surface *conv = SDL_ConvertSurface( &src, SDL_PIXELFORMAT_RGBA32 );
    if( !conv ) { return; }
    if( !SDL_LockSurface( conv ) ) {
        SDL_DestroySurface( conv );
        return;
    }
    const auto *pixels = static_cast<const std::uint8_t *>( conv->pixels );
    for( int y = 0; y < conv->h; ++y ) {
        const std::uint8_t *row = pixels + static_cast<std::ptrdiff_t>( y ) * conv->pitch;
        for( int x = 0; x < conv->w; ++x ) {
            const std::uint8_t *px = row + static_cast<std::ptrdiff_t>( x ) * 4;
            if( px[3] < 128 ) { continue; } // transparent / soft edge: not palette material
            const std::uint32_t key = static_cast<std::uint32_t>( px[0] )
                                      | ( static_cast<std::uint32_t>( px[1] ) << 8 )
                                      | ( static_cast<std::uint32_t>( px[2] ) << 16 );
            ++hist_[key];
        }
    }
    SDL_UnlockSurface( conv );
    SDL_DestroySurface( conv );
}

auto palette_accumulator::build( const ramp_gen_params &gen ) const -> palette_ramp_data
{
    palette_ramp_data out;
    out.steps = std::max( 2, gen.steps );
    if( hist_.empty() ) { return out; }

    // Keep the most frequent colours as palette rows.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> ranked( hist_.begin(), hist_.end() );
    const std::size_t keep =
        std::min<std::size_t>( ranked.size(), static_cast<std::size_t>( PALETTE_ROWS ) );
    std::ranges::partial_sort( ranked, ranked.begin() + static_cast<std::ptrdiff_t>( keep ),
    []( const auto & a, const auto & b ) { return a.second > b.second; } );
    ranked.resize( keep );
    out.palette_size = static_cast<int>( keep );

    // Coverage diagnostics (see palette_ramp_data). Computed before the ramps so the
    // caller can log whether PALETTE_ROWS is actually covering this tileset.
    out.unique_colours = static_cast<std::uint32_t>( hist_.size() );
    for( const auto &[c, n] : hist_ ) {
        out.total_pixels += n;
    }
    std::uint64_t kept_pixels = 0;
    out.kept_min_count = ranked.empty() ? 0u : ranked.back().second;
    for( const auto &[c, n] : ranked ) {
        kept_pixels += n;
    }
    out.tail_pixels = out.total_pixels - kept_pixels;
    // Generate `steps` shades per row. Step 0 is the deepest shadow, the top step the
    // brightest highlight; the base colour sits where the ramp multiplier crosses 1.
    out.ramp.resize( keep * static_cast<std::size_t>( out.steps ) );
    std::vector<oklab_f> lab_rows( keep );
    for( std::size_t i = 0; i < keep; ++i ) {
        const std::uint32_t k = ranked[i].first;
        const rgb_f base{ .r = static_cast<float>( k & 0xFFu ) / 255.0f,
                          .g = static_cast<float>( ( k >> 8 ) & 0xFFu ) / 255.0f,
                          .b = static_cast<float>( ( k >> 16 ) & 0xFFu ) / 255.0f };
        lab_rows[i] = srgb_to_oklab( base );
        const hsv_f hsv = rgb_to_hsv( base );
        for( int s = 0; s < out.steps; ++s ) {
            const float t = static_cast<float>( s ) / static_cast<float>( out.steps - 1 );
            const float hue_shift = std::lerp( gen.shadow_hue_shift, gen.light_hue_shift, t );
            const float val_mul = std::lerp( gen.shadow_value, gen.light_value, t );
            const float sat_mul = std::lerp( gen.shadow_sat, gen.light_sat, t );
            const hsv_f shaded{ .h = hsv.h + hue_shift,
                                .s = std::clamp( hsv.s * sat_mul, 0.0f, 1.0f ),
                                .v = std::clamp( hsv.v * val_mul, 0.0f, 1.0f ) };
            out.ramp[i * static_cast<std::size_t>( out.steps ) + static_cast<std::size_t>( s )] =
                pack_rgba8( hsv_to_rgb( shaded ) );
        }
    }

    // 32^3 nearest-row lookup in OkLab.
    constexpr int N = PALETTE_LUT_SIDE;
    out.index.resize( static_cast<std::size_t>( N ) * N * N );
    for( int r = 0; r < N; ++r ) {
        for( int g = 0; g < N; ++g ) {
            for( int b = 0; b < N; ++b ) {
                const rgb_f probe{ .r = static_cast<float>( r ) / ( N - 1 ),
                                   .g = static_cast<float>( g ) / ( N - 1 ),
                                   .b = static_cast<float>( b ) / ( N - 1 ) };
                const oklab_f pl = srgb_to_oklab( probe );
                std::uint32_t best = 0;
                float best_d = 1e30f;
                for( std::size_t i = 0; i < keep; ++i ) {
                    const oklab_f &c = lab_rows[i];
                    const float dl = pl.l - c.l;
                    const float da = pl.a - c.a;
                    const float db = pl.b - c.b;
                    const float d = dl * dl + da * da + db * db;
                    if( d < best_d ) {
                        best_d = d;
                        best = static_cast<std::uint32_t>( i );
                    }
                }
                out.index[( static_cast<std::size_t>( r ) * N + g ) * N + b] = best;
            }
        }
    }
    return out;
}

} // namespace lighting
