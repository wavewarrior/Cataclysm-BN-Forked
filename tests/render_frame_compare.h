#pragma once

// The two image oracles of the Render tier, ported from the bnplay pixel oracles
// (`tools/bnplay/frames.ts` `frameDelta`, `tools/bnplay/compare.ts`
// `channelDelta`/`dilate`/`noiseMask`/`changedOutsideMask`) so the comparison lives inside
// `cata_test-tiles` and needs neither a windowed capture nor a TypeScript runtime.
// ADR-0006 replaces the three windowed oracles with exactly these functions.
//
// Header-only and free of any game or Catch2 dependency: it is a Logic tier file, unit-tested
// with synthetic images in `render_frame_compare_test.cpp`.
//
// Frames are RGB, 3 bytes per pixel, top row first — the layout `decodeFrame` produces and the
// layout `render_state::capture_texture_to_rgba` yields after dropping alpha.
//
// Two deliberate departures from the TypeScript originals, both forced by the port:
//   * `noise_mask` takes a PAIR of same-state frames, not N>=3. The deno `noiseMask` refused
//     fewer than `MIN_MASK_FRAMES` because a windowed episode could afford three captures; a
//     Render tier case pays a full frame per capture inside the process, and ADR-0006's paired
//     run is "two frames of the same state under a noise mask". The union-of-pairs rule the
//     original applies over N frames degenerates to the single pair here, so the mask itself is
//     computed identically.
//   * `changed_pixel_count` takes a per-channel `threshold`: the original counted a pixel as
//     changed at `delta > 0`, which is `threshold == 0` here. Keeping the knob lets a case
//     tolerate a driver's ±1 LSB rounding without widening the mask.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace render_compare {

/// The default gate, carried over from `compare.ts` `DEFAULT_MAX_CHANGED`: about 0.04% of a
/// 2560x1440 frame, above the measured A/A floor.
inline constexpr unsigned max_changed_default = 1500;

/// How far the noise mask is grown, in pixels per direction: `compare.ts` `DEFAULT_DILATE`.
inline constexpr int dilate_default = 2;

/// A frame: `rgb` holds `width * height` pixels as R, G, B bytes, top row first.
struct image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb;

    image() = default;
    image( const int w, const int h ):
        width( w ), height( h ), rgb( static_cast<std::size_t>( w ) * h * 3, 0 ) {}

    std::size_t pixel_count() const { return static_cast<std::size_t>( width ) * height; }
    std::uint8_t *at( const int x, const int y ) {
        return rgb.data() + ( static_cast<std::size_t>( y ) * width + x ) * 3;
    }
    const std::uint8_t *at( const int x, const int y ) const {
        return rgb.data() + ( static_cast<std::size_t>( y ) * width + x ) * 3;
    }
};

/// Converts the RGBA8 readback of `render_state::capture_texture_to_rgba` into the RGB layout
/// every function here works in. Refuses a buffer that is not exactly w*h*4 bytes.
inline image image_from_rgba( const std::vector<std::uint8_t> &rgba, const int w, const int h ) {
    if( w <= 0 || h <= 0 ) {
        throw std::invalid_argument( "image_from_rgba: non-positive frame size" );
    }
    if( rgba.size() != static_cast<std::size_t>( w ) * h * 4 ) {
        throw std::invalid_argument(
            "image_from_rgba: expected " + std::to_string( static_cast<std::size_t>( w ) * h * 4 ) +
            " bytes for a " + std::to_string( w ) + "x" + std::to_string( h ) + " frame, got " +
            std::to_string( rgba.size() ) );
    }
    image out( w, h );
    for( std::size_t p = 0; p < out.pixel_count(); ++p ) {
        out.rgb[p * 3 + 0] = rgba[p * 4 + 0];
        out.rgb[p * 3 + 1] = rgba[p * 4 + 1];
        out.rgb[p * 3 + 2] = rgba[p * 4 + 2];
    }
    return out;
}

/// A part of a frame as fractions of its size, each in 0..1 — `frames.ts` `Region`.
struct region {
    double x = 0.0;
    double y = 0.0;
    double w = 1.0;
    double h = 1.0;
};

/// The pixel rectangle `reg` covers in a frame of this size, in the same half-open form
/// `frames.ts` `pixelsOf` derives: floored top-left, ceiled bottom-right, clamped to the frame,
/// and never empty.
struct pixel_rect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    int area() const { return ( right - left ) * ( bottom - top ); }
};

inline pixel_rect pixels_of( const image &img, const region &reg ) {
    const int left = std::min( img.width - 1, static_cast<int>( std::floor( reg.x * img.width ) ) );
    const int top = std::min( img.height - 1, static_cast<int>( std::floor( reg.y * img.height ) ) );
    const int right = std::max(
        left + 1, std::min( img.width, static_cast<int>( std::ceil( ( reg.x + reg.w ) * img.width ) ) ) );
    const int bottom = std::max(
        top + 1,
        std::min( img.height, static_cast<int>( std::ceil( ( reg.y + reg.h ) * img.height ) ) ) );
    return pixel_rect{ .left = left, .top = top, .right = right, .bottom = bottom };
}

[[noreturn]] inline void throw_size_mismatch( const image &a, const image &b ) {
    throw std::invalid_argument(
        "cannot compare a " + std::to_string( a.width ) + "x" + std::to_string( a.height ) +
        " frame with a " + std::to_string( b.width ) + "x" + std::to_string( b.height ) + " one" );
}

inline void assert_same_size( const image &a, const image &b ) {
    if( a.width != b.width || a.height != b.height ) { throw_size_mismatch( a, b ); }
}

/// Per-channel difference of two same-size frames, one byte per PIXEL: the largest of the three
/// channel differences (0 identical). `compare.ts` `channelDelta`.
inline std::vector<std::uint8_t> channel_delta( const image &a, const image &b ) {
    assert_same_size( a, b );
    std::vector<std::uint8_t> out( a.pixel_count() );
    for( std::size_t p = 0; p < out.size(); ++p ) {
        const std::size_t i = p * 3;
        const std::uint8_t dr = static_cast<std::uint8_t>(
            std::abs( static_cast<int>( a.rgb[i] ) - static_cast<int>( b.rgb[i] ) ) );
        const std::uint8_t dg = static_cast<std::uint8_t>(
            std::abs( static_cast<int>( a.rgb[i + 1] ) - static_cast<int>( b.rgb[i + 1] ) ) );
        const std::uint8_t db = static_cast<std::uint8_t>(
            std::abs( static_cast<int>( a.rgb[i + 2] ) - static_cast<int>( b.rgb[i + 2] ) ) );
        out[p] = std::max( { dr, dg, db } );
    }
    return out;
}

/// Every pixel of `seed` that is set, or is within `steps` pixels of one that is — measured in
/// the four cardinal directions, grown one ring per step out of the PREVIOUS step's array, which
/// is a diamond of radius `steps`. Exactly `compare.ts` `dilate`.
inline std::vector<std::uint8_t> dilate(
    const std::vector<std::uint8_t> &seed, const int width, const int height, const int steps ) {
    std::vector<std::uint8_t> grown = seed;
    for( int s = 0; s < steps; ++s ) {
        std::vector<std::uint8_t> next = grown;
        for( int y = 0; y < height; ++y ) {
            for( int x = 0; x < width; ++x ) {
                const std::size_t at = static_cast<std::size_t>( y ) * width + x;
                if( grown[at] != 0 ) { continue; }
                if( ( x > 0 && grown[at - 1] != 0 ) ||
                    ( x + 1 < width && grown[at + 1] != 0 ) ||
                    ( y > 0 && grown[at - static_cast<std::size_t>( width )] != 0 ) ||
                    ( y + 1 < height && grown[at + static_cast<std::size_t>( width )] != 0 ) ) {
                    next[at] = 1;
                }
            }
        }
        grown = std::move( next );
    }
    return grown;
}

/// The noise mask of a pair of same-state frames: every pixel the two disagree on, dilated by
/// `dilate_steps` steps. Anything this mask covers is excused later, so it is built from frames
/// of the UNCHANGED state only.
inline std::vector<std::uint8_t> noise_mask(
    const image &a, const image &b, const int dilate_steps = dilate_default ) {
    const auto delta = channel_delta( a, b );
    std::vector<std::uint8_t> mask( delta.size() );
    for( std::size_t p = 0; p < delta.size(); ++p ) {
        if( delta[p] > 0 ) { mask[p] = 1; }
    }
    if( dilate_steps <= 0 ) { return mask; }
    return dilate( mask, a.width, a.height, dilate_steps );
}

/// The bounding box of the changed pixels, inclusive, as `[left, top, right, bottom]`.
struct bbox {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

/// How many pixels of `test` differ from `base` by more than `threshold` outside `mask`, with
/// the same summary `compare.ts` `changedOutsideMask` reports.
struct changed_pixels {
    /// Pixels changed outside the mask.
    std::size_t count = 0;
    /// `count` as a percentage of the frame's pixels, to 4 decimals as in the original.
    double pct = 0.0;
    /// Largest single-channel difference among the changed pixels (0 when none changed).
    unsigned max_delta = 0;
    /// Extent of the changed pixels, or nullopt when none changed.
    std::optional<bbox> box;
    /// True when the count clears the gate.
    bool within( const unsigned max_changed = max_changed_default ) const {
        return count <= max_changed;
    }
};

/// `mask` must hold one byte per pixel of `base`/`test`; an empty mask excuses nothing, which is
/// the right default for a first comparison.
inline changed_pixels changed_pixel_count(
    const image &base, const image &test, const std::vector<std::uint8_t> &mask,
    const unsigned threshold = 0 ) {
    assert_same_size( base, test );
    const auto delta = channel_delta( base, test );
    changed_pixels out;
    int left = -1;
    int right = -1;
    int top = -1;
    int bottom = -1;
    for( std::size_t p = 0; p < delta.size(); ++p ) {
        if( delta[p] <= threshold ) { continue; }
        if( !mask.empty() && mask[p] != 0 ) { continue; }
        ++out.count;
        out.max_delta = std::max( out.max_delta, static_cast<unsigned>( delta[p] ) );
        const int x = static_cast<int>( p % static_cast<std::size_t>( base.width ) );
        const int y = static_cast<int>( p / static_cast<std::size_t>( base.width ) );
        if( left == -1 || x < left ) { left = x; }
        right = std::max( right, x );
        if( top == -1 || y < top ) { top = y; }
        bottom = std::max( bottom, y );
    }
    out.pct = std::round( 10000.0 * 100.0 * static_cast<double>( out.count ) /
                          static_cast<double>( delta.size() ) ) /
              10000.0;
    if( out.count == 0 ) {
        out.max_delta = 0;
    } else {
        out.box = bbox{ .left = left, .top = top, .right = right, .bottom = bottom };
    }
    return out;
}

/// How much two frames of the same size differ: the mean absolute difference of their colour
/// channels over `reg`, as a fraction of the full range (0 identical, 1 black against white).
/// `frames.ts` `frameDelta`.
inline double mean_abs_delta( const image &a, const image &b, const region &reg = {} ) {
    assert_same_size( a, b );
    const pixel_rect r = pixels_of( a, reg );
    double total = 0.0;
    for( int y = r.top; y < r.bottom; ++y ) {
        const std::size_t from = ( static_cast<std::size_t>( y ) * a.width + r.left ) * 3;
        const std::size_t to = ( static_cast<std::size_t>( y ) * a.width + r.right ) * 3;
        for( std::size_t i = from; i < to; ++i ) {
            total += std::abs( static_cast<int>( a.rgb[i] ) - static_cast<int>( b.rgb[i] ) );
        }
    }
    return total / ( 255.0 * 3.0 * static_cast<double>( r.area() ) );
}

} // namespace render_compare
