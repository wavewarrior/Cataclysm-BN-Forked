#include "normal_gen.h"

#include <SDL3/SDL_endian.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

namespace lighting
{

namespace
{

// ---------------------------------------------------------------- EDT (exact)

/// Finite stand-in for "no seed on this line yet". Felzenszwalb & Huttenlocher's
/// parabola intersection SUBTRACTS two sampled values, so a true infinity there would
/// produce NaN and the sentinel must be finite. It must also survive `+ q*q` exactly:
/// in `double` the ulp at 1e12 is 2^-12, so 1e12 + k is exact for every k a sprite rect
/// can produce, whereas in `float` 1e12 + q*q == 1e12, every intersection between two
/// unseeded parabolas collapses to 0 and the lower-envelope ordering is destroyed. That
/// is the only reason this one kernel is `double` while everything else here is `float`.
constexpr double EDT_INF = 1e12;

/// Reusable line buffers for `edt_1d`, so a whole-rect transform allocates once.
struct edt_scratch {
    std::vector<double> line;
    std::vector<double> d;
    std::vector<double> z;
    std::vector<int> v;

    explicit edt_scratch( int n )
        : line( static_cast<std::size_t>( n ) )
        , d( static_cast<std::size_t>( n ) )
        , z( static_cast<std::size_t>( n ) + 1 )
        , v( static_cast<std::size_t>( n ) ) {}
};

/// Felzenszwalb & Huttenlocher exact 1-D squared distance transform, in place over `f`:
/// on return `f[q] == min over p of ( (q - p)^2 + f_in[p] )`. O(n), by walking the lower
/// envelope of the n parabolas rooted at each sample.
///
/// Ported statement for statement from the prototype, including the `k > 0` guard on the
/// pop: `z[0]` is -infinity so the comparison alone already stops there, but the guard
/// makes the invariant explicit and cannot cost anything measurable.
auto edt_1d( std::span<double> f, edt_scratch &s ) -> void
{
    const int n = static_cast<int>( f.size() );
    if( n <= 0 ) {
        return;
    }
    // `z` holds envelope boundaries, which are only ever COMPARED and never subtracted,
    // so a real infinity is safe here even though `f` needs the finite EDT_INF above.
    constexpr double inf = std::numeric_limits<double>::infinity();
    int k = 0;
    s.v[0] = 0;
    s.z[0] = -inf;
    s.z[1] = inf;
    for( int q = 1; q < n; ++q ) {
        while( true ) {
            const int p = s.v[k];
            const double sect = ( ( f[q] + static_cast<double>( q ) * q )
                                  - ( f[p] + static_cast<double>( p ) * p ) )
                                / ( 2.0 * q - 2.0 * p );
            if( sect <= s.z[k] && k > 0 ) {
                --k;
                continue;
            }
            ++k;
            s.v[k] = q;
            s.z[k] = sect;
            s.z[k + 1] = inf;
            break;
        }
    }
    k = 0;
    for( int q = 0; q < n; ++q ) {
        while( s.z[k + 1] < q ) {
            ++k;
        }
        const double dq = q - s.v[k];
        s.d[q] = dq * dq + f[s.v[k]];
    }
    std::copy_n( s.d.begin(), n, f.begin() );
}

// ---------------------------------------------------------------- 2-D float field

/// A w*h scalar field. Exists so the numeric kernels below take (field, parameter)
/// instead of (data, w, h, parameter) argument lists.
struct field2 {
    std::vector<float> v;
    int w = 0;
    int h = 0;

    field2() = default;
    field2( int width, int height )
        : v( static_cast<std::size_t>( std::max( width, 0 ) ) * std::max( height, 0 ), 0.0f )
        , w( width )
        , h( height ) {}

    auto ref( int x, int y ) -> float & { return v[y * w + x]; } // *NOPAD*
    /// Edge-clamped read. Every filter below pads by clamping, as the prototype did.
    auto clamped( int x, int y ) const -> float
    {
        return v[std::clamp( y, 0, h - 1 ) * w + std::clamp( x, 0, w - 1 )];
    }
};

struct gradient2 {
    field2 gx;
    field2 gy;
};

/// Separable Gaussian, radius `ceil( 3 * sigma )` (minimum 1), edge-clamped.
///
/// The prototype used `round( 3 * sigma )`, i.e. radius 3 at the default sigma 1.1 where
/// this uses 4. The extra tap pair carries ~1.0e-3 of the normalised kernel weight; the
/// height map is in 0..1 and the Sobel then divides by 8, so the contribution is roughly
/// 1/4000 of a normal-axis code value -- far below the 1/255 encoding quantum, and the
/// generated atlas is byte-identical either way. `ceil` is used because it is the honest
/// "cover 3 sigma" rule and does not silently shrink the kernel for sigma just under a
/// half-integer.
auto gaussian_blur( const field2 &src, float sigma ) -> field2
{
    if( sigma <= 0.0f || src.w <= 0 || src.h <= 0 ) {
        return src;
    }
    const int r = std::max( 1, static_cast<int>( std::ceil( 3.0f * sigma ) ) );
    std::vector<float> k( static_cast<std::size_t>( 2 * r + 1 ) );
    float sum = 0.0f;
    for( int i = -r; i <= r; ++i ) {
        const float x = static_cast<float>( i );
        const float t = std::exp( -( x * x ) / ( 2.0f * sigma * sigma ) );
        k[i + r] = t;
        sum += t;
    }
    for( float &c : k ) {
        c /= sum;
    }

    field2 mid( src.w, src.h );
    for( int y = 0; y < src.h; ++y ) {
        for( int x = 0; x < src.w; ++x ) {
            float acc = 0.0f;
            for( int i = -r; i <= r; ++i ) {
                acc += k[i + r] * src.clamped( x + i, y );
            }
            mid.ref( x, y ) = acc;
        }
    }
    field2 out( src.w, src.h );
    for( int y = 0; y < src.h; ++y ) {
        for( int x = 0; x < src.w; ++x ) {
            float acc = 0.0f;
            for( int i = -r; i <= r; ++i ) {
                acc += k[i + r] * mid.clamped( x, y + i );
            }
            out.ref( x, y ) = acc;
        }
    }
    return out;
}

/// 3x3 Sobel with both kernels divided by 8, edge-clamped padding.
///
/// The /8 is not cosmetic: it makes gx the per-pixel slope of a linear ramp (the taps
/// sum to a central difference over two pixels times four), which is the scale
/// `normal_gen_params::slope` was calibrated against. Changing the divisor silently
/// rescales every generated normal.
auto sobel( const field2 &f ) -> gradient2
{
    gradient2 g{ .gx = field2( f.w, f.h ), .gy = field2( f.w, f.h ) };
    for( int y = 0; y < f.h; ++y ) {
        for( int x = 0; x < f.w; ++x ) {
            const float tl = f.clamped( x - 1, y - 1 );
            const float tc = f.clamped( x, y - 1 );
            const float tr = f.clamped( x + 1, y - 1 );
            const float ml = f.clamped( x - 1, y );
            const float mr = f.clamped( x + 1, y );
            const float bl = f.clamped( x - 1, y + 1 );
            const float bc = f.clamped( x, y + 1 );
            const float br = f.clamped( x + 1, y + 1 );
            g.gx.ref( x, y ) = ( -tl + tr - 2.0f * ml + 2.0f * mr - bl + br ) / 8.0f;
            g.gy.ref( x, y ) = ( -tl - 2.0f * tc - tr + bl + 2.0f * bc + br ) / 8.0f;
        }
    }
    return g;
}

// ---------------------------------------------------------------- source pixels

/// Locks a surface only when SDL says it must be (RLE-encoded surfaces). Atlas pages and
/// freshly loaded sheets never are, so this is normally a no-op; it exists so a
/// colour-keyed RLE sheet cannot silently hand us garbage instead of pixels.
class surface_lock
{
    public:
        explicit surface_lock( SDL_Surface *s )
            : s_( s != nullptr && SDL_MUSTLOCK( s ) ? s : nullptr )
        {
            if( s_ != nullptr && !SDL_LockSurface( s_ ) ) {
                s_ = nullptr;
                ok_ = false;
            }
        }
        ~surface_lock()
        {
            if( s_ != nullptr ) {
                SDL_UnlockSurface( s_ );
            }
        }
        surface_lock( const surface_lock & ) = delete;
        surface_lock( surface_lock && ) = delete;
        auto operator=( const surface_lock & ) -> surface_lock & = delete; // *NOPAD*
        auto operator=( surface_lock && ) -> surface_lock & = delete; // *NOPAD*

        auto ok() const -> bool { return ok_; }

    private:
        SDL_Surface *s_ = nullptr;
        bool ok_ = true;
};

/// A sprite rect decoded once: colour in 0..1 units, plus the alpha silhouette test.
struct sprite_pixels {
    std::vector<float> rgb;          // 3 * w * h, interleaved, 0..1
    std::vector<std::uint8_t> solid; // w * h, 1 where alpha / 255 > 0.5
    int w = 0;
    int h = 0;
};

/// Decodes `rect` of `src`. Goes through SDL_GetPixelFormatDetails rather than assuming
/// a layout, because the tileset loader hands us whatever the sheet's PNG produced. The
/// RGBA32 fast path exists because that is what every converted atlas page actually is
/// and the generic path costs an out-of-line SDL_GetRGBA per pixel.
auto read_rect( const SDL_Surface &src, const SDL_Rect &rect ) -> sprite_pixels
{
    sprite_pixels out;
    const SDL_PixelFormatDetails *fmt = SDL_GetPixelFormatDetails( src.format );
    if( fmt == nullptr || src.pixels == nullptr ) {
        return out;
    }
    // SDL_GetSurfacePalette / SDL_MUSTLOCK are non-const in the SDL3 API even though
    // neither mutates the surface; palette_ramp.cpp casts for the same reason.
    SDL_Surface *mut = const_cast<SDL_Surface *>( &src );
    const surface_lock lock( mut );
    if( !lock.ok() ) {
        return out;
    }
    const SDL_Palette *pal = SDL_GetSurfacePalette( mut );

    out.w = rect.w;
    out.h = rect.h;
    out.rgb.resize( static_cast<std::size_t>( rect.w ) * rect.h * 3 );
    out.solid.resize( static_cast<std::size_t>( rect.w ) * rect.h, 0 );

    const auto *base = static_cast<const std::uint8_t *>( src.pixels );
    const int bpp = fmt->bytes_per_pixel;
    const bool fast = src.format == SDL_PIXELFORMAT_RGBA32;
    for( int y = 0; y < rect.h; ++y ) {
        const std::uint8_t *row =
            base + static_cast<std::ptrdiff_t>( rect.y + y ) * src.pitch;
        for( int x = 0; x < rect.w; ++x ) {
            const std::uint8_t *px = row + static_cast<std::ptrdiff_t>( rect.x + x ) * bpp;
            std::uint8_t r = 0;
            std::uint8_t g = 0;
            std::uint8_t b = 0;
            std::uint8_t a = 255;
            if( fast ) {
                // SDL_PIXELFORMAT_RGBA32 is the byte-order alias, so memory is R,G,B,A
                // on every platform.
                r = px[0];
                g = px[1];
                b = px[2];
                a = px[3];
            } else {
                std::uint32_t pixel = 0;
                switch( bpp ) {
                    case 1:
                        pixel = px[0];
                        break;
                    case 2:
                        if constexpr( SDL_BYTEORDER == SDL_BIG_ENDIAN ) {
                            pixel = static_cast<std::uint32_t>( px[0] ) << 8
                                    | static_cast<std::uint32_t>( px[1] );
                        } else {
                            pixel = static_cast<std::uint32_t>( px[1] ) << 8
                                    | static_cast<std::uint32_t>( px[0] );
                        }
                        break;
                    case 3:
                        if constexpr( SDL_BYTEORDER == SDL_BIG_ENDIAN ) {
                            pixel = static_cast<std::uint32_t>( px[0] ) << 16
                                    | static_cast<std::uint32_t>( px[1] ) << 8 | px[2];
                        } else {
                            pixel = static_cast<std::uint32_t>( px[2] ) << 16
                                    | static_cast<std::uint32_t>( px[1] ) << 8 | px[0];
                        }
                        break;
                    case 4:
                        if constexpr( SDL_BYTEORDER == SDL_BIG_ENDIAN ) {
                            pixel = static_cast<std::uint32_t>( px[0] ) << 24
                                    | static_cast<std::uint32_t>( px[1] ) << 16
                                    | static_cast<std::uint32_t>( px[2] ) << 8 | px[3];
                        } else {
                            pixel = static_cast<std::uint32_t>( px[3] ) << 24
                                    | static_cast<std::uint32_t>( px[2] ) << 16
                                    | static_cast<std::uint32_t>( px[1] ) << 8 | px[0];
                        }
                        break;
                    default:
                        break;
                }
                SDL_GetRGBA( pixel, fmt, pal, &r, &g, &b, &a );
            }
            const int i = y * rect.w + x;
            out.rgb[i * 3 + 0] = static_cast<float>( r ) / 255.0f;
            out.rgb[i * 3 + 1] = static_cast<float>( g ) / 255.0f;
            out.rgb[i * 3 + 2] = static_cast<float>( b ) / 255.0f;
            out.solid[i] = static_cast<float>( a ) / 255.0f > 0.5f ? 1 : 0;
        }
    }
    return out;
}

/// Max 4-neighbour colour distance per pixel, in 0..1 units.
///
/// Colour DISTANCE as a segment boundary, never luminance as height -- that distinction
/// is what keeps pre-baked shading from inverting volumes, the documented failure mode of
/// Sobel-from-colour (arXiv:2212.09692 sec IV-B), and BN art is heavily pre-shaded.
///
/// Neighbours WRAP at the rect border, reproducing the prototype's `np.roll`. That is
/// load-bearing for calibration rather than an accident: `edge_threshold`, `coh_lo` and
/// `coh_hi` were all measured on 12 UnDeadPeople tiles with wrapping neighbours, so
/// switching to the edge-clamped padding used by the filters above would shift the very
/// statistic those thresholds are pinned to. It only affects the 1-texel border, and the
/// wrap stays inside the sprite's own rect (never bleeding into a neighbouring sprite on
/// the sheet), exactly as the prototype's per-tile crop did.
auto colour_gradient( const sprite_pixels &p ) -> field2
{
    field2 g( p.w, p.h );
    // Order is irrelevant -- the result is a max over all four.
    constexpr std::array<std::array<int, 2>, 4> nb{ { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } } };
    for( int y = 0; y < p.h; ++y ) {
        for( int x = 0; x < p.w; ++x ) {
            const int c = ( y * p.w + x ) * 3;
            float best = 0.0f;
            for( const auto &d : nb ) {
                const int sy = ( y + d[0] + p.h ) % p.h;
                const int sx = ( x + d[1] + p.w ) % p.w;
                const int o = ( sy * p.w + sx ) * 3;
                const float dr = p.rgb[c + 0] - p.rgb[o + 0];
                const float dg = p.rgb[c + 1] - p.rgb[o + 1];
                const float db = p.rgb[c + 2] - p.rgb[o + 2];
                best = std::max( best, std::sqrt( dr * dr + dg * dg + db * db ) );
            }
            g.ref( x, y ) = best;
        }
    }
    return g;
}

// ---------------------------------------------------------------- encoding

/// C3 axis encode: -1..1 -> 0..255, rounded to nearest so 0 lands exactly on 128.
auto encode_axis( float n ) -> std::uint8_t
{
    return static_cast<std::uint8_t>(
               std::clamp( ( n * 0.5f + 0.5f ) * 255.0f + 0.5f, 0.0f, 255.0f ) );
}

auto encode_unit( float n ) -> std::uint8_t
{
    return static_cast<std::uint8_t>( std::clamp( n * 255.0f + 0.5f, 0.0f, 255.0f ) );
}

/// Rescale so the maximum is 1. A field of all zeros is left alone: the prototype's
/// `if h.max() > 0` guard, and the reason it matters is that a constant field's Sobel is
/// zero either way, so there is nothing to normalise toward.
auto normalise_peak( field2 &f ) -> void
{
    if( f.v.empty() ) {
        return;
    }
    const float mx = *std::ranges::max_element( f.v );
    if( mx > 0.0f ) {
        for( float &c : f.v ) {
            c /= mx;
        }
    }
}

} // namespace

auto exact_edt( std::span<const std::uint8_t> mask, int w, int h ) -> std::vector<float>
{
    if( w <= 0 || h <= 0 || mask.size() < static_cast<std::size_t>( w ) * h ) {
        return {};
    }
    const auto n = static_cast<std::size_t>( w ) * h;
    const auto bits = mask.first( n );
    // No unset pixel means no boundary, so the distance is undefined. The prototype
    // returns sqrt(EDT_INF) ~ 1e6 everywhere; every caller then divides by the maximum,
    // which turns that into a constant 1.0 whose Sobel is zero. Zeros is therefore
    // behaviour-identical and cannot leak a sentinel into anyone's arithmetic.
    // `generate_sprite_normal` never reaches this: it only transforms masks it has
    // already proven contain an unset pixel.
    if( std::ranges::all_of( bits, []( std::uint8_t m ) { return m != 0; } ) ) {
        return std::vector<float>( n, 0.0f );
    }

    std::vector<double> buf( n );
    std::ranges::transform( bits, buf.begin(),
    []( std::uint8_t m ) { return m != 0 ? EDT_INF : 0.0; } );

    edt_scratch scratch( std::max( w, h ) );
    // Per column, then per row. This separation is what makes the transform EXACT: after
    // the column pass each entry is the squared distance to the nearest unset pixel in
    // its own column, and the row pass minimises (dx^2 + that) across the row, which is
    // the true squared euclidean distance.
    for( int x = 0; x < w; ++x ) {
        for( int y = 0; y < h; ++y ) {
            scratch.line[y] = buf[y * w + x];
        }
        edt_1d( std::span<double>( scratch.line ).first( h ), scratch );
        for( int y = 0; y < h; ++y ) {
            buf[y * w + x] = scratch.line[y];
        }
    }
    for( int y = 0; y < h; ++y ) {
        edt_1d( std::span<double>( buf ).subspan( static_cast<std::size_t>( y ) * w, w ),
                scratch );
    }

    std::vector<float> out( n );
    std::ranges::transform( buf, out.begin(), []( double d ) {
        return static_cast<float>( std::sqrt( std::max( d, 0.0 ) ) );
    } );
    return out;
}

auto coherence_gap_run( std::span<const std::uint8_t> mask, int w, int h ) -> float
{
    if( w <= 0 || h <= 0 || mask.size() < static_cast<std::size_t>( w ) * h ) {
        return 0.0f;
    }
    // Runs are pooled across every line and the mean taken over RUNS, not over lines, so
    // a line containing no gap at all contributes nothing rather than a zero.
    const auto scan = [&]( bool by_row ) -> float {
        double total = 0.0;
        int runs = 0;
        int cur = 0;
        const int lines = by_row ? h : w;
        const int len = by_row ? w : h;
        for( int l = 0; l < lines; ++l ) {
            for( int i = 0; i < len; ++i ) {
                const int at = by_row ? l * w + i : i * w + l;
                if( mask[at] == 0 ) {
                    ++cur;
                } else if( cur > 0 ) {
                    total += cur;
                    ++runs;
                    cur = 0;
                }
            }
            if( cur > 0 ) {
                total += cur;
                ++runs;
                cur = 0;
            }
        }
        return runs > 0 ? static_cast<float>( total / runs ) : 0.0f;
    };
    return 0.5f * ( scan( true ) + scan( false ) );
}

auto generate_sprite_normal( const normal_gen_request &req ) -> normal_gen_stats
{
    normal_gen_stats stats;
    if( req.src == nullptr || req.dst == nullptr ) {
        return stats;
    }
    const SDL_Surface &src = *req.src;
    SDL_Surface &dst = *req.dst;
    const int w = req.rect.w;
    const int h = req.rect.h;
    if( w <= 0 || h <= 0 ) {
        return stats;
    }
    if( req.rect.x < 0 || req.rect.y < 0 || req.rect.x + w > src.w || req.rect.y + h > src.h ) {
        return stats;
    }
    if( req.dst_at.x < 0 || req.dst_at.y < 0
        || req.dst_at.x + w > dst.w || req.dst_at.y + h > dst.h ) {
        return stats;
    }
    // The write loop below addresses `dst` as raw R,G,B,A bytes, which is only valid for
    // the byte-order alias. Any other format is a wiring mistake, not something to
    // silently reinterpret.
    if( dst.format != SDL_PIXELFORMAT_RGBA32 || dst.pixels == nullptr ) {
        return stats;
    }

    const sprite_pixels px = read_rect( src, req.rect );
    if( px.w != w || px.h != h ) {
        return stats;
    }
    const normal_gen_params &p = req.params;
    const auto n = static_cast<std::size_t>( w ) * h;

    // --- external term ------------------------------------------------------------
    // GATE: when the opaque region fills the tile the silhouette carries no shape at
    // all; the alpha EDT degenerates to distance-from-tile-border and stamps the SAME
    // pyramid onto every wall and floor tile -- a repeating diamond crease locked to the
    // tile grid, and one pyramid per tile up a multi-tile wall instead of one gradient.
    // All 12 tiles sampled during calibration hit this gate, so internal contours carry
    // essentially everything for BN terrain. Drop the external term entirely there.
    const auto row_has_solid = [&]( int y ) -> bool {
        return std::ranges::any_of( std::views::iota( 0, w ),
        [&]( int x ) { return px.solid[y * w + x] != 0; } );
    };
    const auto col_has_solid = [&]( int x ) -> bool {
        return std::ranges::any_of( std::views::iota( 0, h ),
        [&]( int y ) { return px.solid[y * w + x] != 0; } );
    };
    const auto solid_count = std::ranges::count_if( px.solid,
    []( std::uint8_t s ) { return s != 0; } );
    const float solid_mean = static_cast<float>( solid_count ) / static_cast<float>( n );
    stats.full_tile = row_has_solid( 0 ) && row_has_solid( h - 1 )
                      && col_has_solid( 0 ) && col_has_solid( w - 1 )
                      && solid_mean > 0.97f;

    field2 h_ext( w, h );
    float ext_w = 0.0f;
    if( !stats.full_tile ) {
        // Size-checked rather than assigned blind: `field2` carries w/h separately, so a
        // short vector here would turn every later read into an out-of-bounds one.
        auto d = exact_edt( px.solid, w, h );
        if( d.size() == n ) {
            h_ext.v = std::move( d );
            normalise_peak( h_ext );
        }
        ext_w = p.ext_weight;
    }

    // --- internal term ------------------------------------------------------------
    const field2 grad = colour_gradient( px );
    std::vector<std::uint8_t> edges( n, 0 );
    for( std::size_t i = 0; i < n; ++i ) {
        edges[i] = grad.v[i] > p.edge_threshold && px.solid[i] != 0 ? 1 : 0;
    }
    const auto edge_count = std::ranges::count( edges, std::uint8_t{ 1 } );
    stats.density = static_cast<float>( edge_count ) / static_cast<float>( n );

    // Absolute short-circuit: a genuinely flat sprite has no contours to find, and
    // anything extracted from it would be quantisation noise promoted to geometry.
    float grad_max_in_silhouette = 0.0f;
    for( std::size_t i = 0; i < n; ++i ) {
        if( px.solid[i] != 0 ) {
            grad_max_in_silhouette = std::max( grad_max_in_silhouette, grad.v[i] );
        }
    }
    stats.flat = solid_count == 0 || grad_max_in_silhouette < p.flat_eps;

    // Sparse-mask short-circuit: this is the hole in the gap-run statistic. A handful of
    // isolated speckles leaves huge clean gaps, so the coherence score comes out HIGH,
    // and the EDT would then grow big smooth domes out of a few stray pixels -- geometry
    // that is not in the art at all. Too sparse to be structure => no relief. The
    // statistic cannot close this itself; that is why `min_density` exists.
    const bool sparse = stats.density < p.min_density;
    stats.coherence = stats.flat || sparse ? 0.0f : coherence_gap_run( edges, w, h );

    field2 h_int( w, h );
    if( edge_count > 0 && !stats.flat ) {
        std::vector<std::uint8_t> inner( n, 0 );
        for( std::size_t i = 0; i < n; ++i ) {
            inner[i] = edges[i] == 0 && px.solid[i] != 0 ? 1 : 0;
        }
        auto d = exact_edt( inner, w, h );
        if( d.size() == n ) {
            h_int.v = std::move( d );
            normalise_peak( h_int );
        }
    } else if( !stats.full_tile ) {
        // Nothing internal to merge, so a cut-out sprite falls back entirely to its
        // silhouette bevel rather than blending toward a zero field.
        ext_w = 1.0f;
    }

    field2 merged( w, h );
    for( std::size_t i = 0; i < n; ++i ) {
        merged.v[i] = ext_w * h_ext.v[i] + ( 1.0f - ext_w ) * h_int.v[i];
    }
    const field2 hmap = gaussian_blur( merged, p.blur_sigma );

    // Coherence gate: fade the relief out for noise-textured sprites. Dither carries no
    // geometry, so inventing relief there is strictly worse than staying flat -- it
    // reads as "cottage cheese" over the whole surface. `std::clamp` rather than the
    // prototype's bare `min` so a degenerate coh_hi <= coh_lo cannot produce a negative
    // or NaN amplitude; inside the valid range the two are identical.
    stats.amplitude = stats.coherence <= p.coh_lo
                      ? 0.0f
                      : std::clamp( ( stats.coherence - p.coh_lo ) / ( p.coh_hi - p.coh_lo ),
                                    0.0f, 1.0f );
    // A full tile has ONLY internal relief, so the gate scales it to nothing. A cut-out
    // sprite keeps its silhouette bevel at `ext_weight` even when the gate zeroes the
    // internal term -- the bevel is real geometry from the alpha, not extracted texture.
    const float slope = p.slope * ( stats.full_tile
                                    ? stats.amplitude
                                    : std::max( stats.amplitude, p.ext_weight ) );

    const gradient2 g = sobel( hmap );
    const std::uint8_t amp_byte = encode_unit( stats.amplitude );

    const surface_lock dst_lock( &dst );
    if( !dst_lock.ok() ) {
        return stats;
    }
    auto *dst_base = static_cast<std::uint8_t *>( dst.pixels );
    for( int y = 0; y < h; ++y ) {
        std::uint8_t *row =
            dst_base + static_cast<std::ptrdiff_t>( req.dst_at.y + y ) * dst.pitch;
        for( int x = 0; x < w; ++x ) {
            std::uint8_t *out = row + static_cast<std::ptrdiff_t>( req.dst_at.x + x ) * 4;
            const int i = y * w + x;
            float nx = 0.0f;
            float ny = 0.0f;
            std::uint8_t weight = 0;
            if( px.solid[i] != 0 ) {
                const float ux = -g.gx.v[i] * slope;
                const float uy = -g.gy.v[i] * slope;
                // nz starts at 1 and is only ever divided by a positive norm, so it is
                // strictly positive and the shader can reconstruct it from nx, ny.
                const float len = std::sqrt( ux * ux + uy * uy + 1.0f );
                nx = ux / len;
                ny = uy / len;
                weight = amp_byte;
            }
            // Transparent pixels get exactly (0,0,1) with ZERO blend weight -- see the
            // header: the silhouette rim is the only place the existing alpha-bevel
            // `surface_normal()` produces a non-flat normal, and blending a hard flat
            // over it at full weight would delete the one relief that works today.
            out[0] = encode_axis( nx );
            out[1] = encode_axis( ny );
            out[2] = weight;
            out[3] = 255;
        }
    }
    return stats;
}

} // namespace lighting
