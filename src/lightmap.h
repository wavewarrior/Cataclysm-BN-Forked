#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string_view>

#include "hsv_color.h"

static constexpr float LIGHT_SOURCE_LOCAL = 0.1f;
static constexpr float LIGHT_SOURCE_BRIGHT = 10.0f;

// Just enough light that you can see the current and adjacent squares with normal vision.
static constexpr float LIGHT_AMBIENT_MINIMAL = 3.7f;
// The threshold between not being able to see anything and things appearing shadowy.
static constexpr float LIGHT_AMBIENT_LOW = 3.5f;
// The lower threshold for seeing well enough to do detail work such as reading or crafting.
static constexpr float LIGHT_AMBIENT_DIM = 5.0f;
// The threshold between things being shadowed and being brightly lit.
static constexpr float LIGHT_AMBIENT_LIT = 10.0f;

/**
 * Transparency 101:
 * Transparency usually ranges between 0.038 (open air) and 0.38 (regular smoke).
 * The bigger the value, the more opaque it is (less light goes through).
 * To make sense of the specific value:
 * For transparency t, vision becomes "obstructed" (see map::apparent_light_helper) after
 *   ≈ (2.3 / t) consequent tiles of transparency `t`  ( derived from 1 / e^(dist * t) = 0.1 )
 *   e.g. for smoke with effective transparency 0.38  it's 2.3/0.38 ≈ 6 tiles
 *  for clean air with t=0.038  dist = 2.3/0.038 ≈ 60 tiles
 *
 * Note:  LIGHT_TRANSPARENCY_SOLID=0 is a special case (it indicates completely opaque tile)
 * */
static constexpr float LIGHT_TRANSPARENCY_SOLID = 0.0f;
// Calculated to run out at 60 squares.
// Cumulative transparency should drop to 0.1 or lower over 60 squares,
// Bright sunlight should drop to LIGHT_AMBIENT_LOW over 60 squares.
static constexpr float LIGHT_TRANSPARENCY_OPEN_AIR = 0.038376418216f;

// indicates starting (full) visibility (for seen_cache)
static constexpr float VISIBILITY_FULL = 1.0f;

// ── Per-tile accumulated light color energy (float RGB) ───────────────────────
// Stored as raw energy, not display-ready; the renderer converts to uint8.
struct light_color_rgb {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;

    bool is_colored() const { return r > 0.0f || g > 0.0f || b > 0.0f; }

    light_color_rgb &operator+=( const light_color_rgb &rhs ) {
        r += rhs.r;
        g += rhs.g;
        b += rhs.b;
        return *this;
    }

    light_color_rgb operator*( float scale ) const {
        return { r * scale, g * scale, b * scale };
    }

    bool operator==( const light_color_rgb &rhs ) const {
        return r == rhs.r && g == rhs.g && b == rhs.b;
    }
    bool operator!=( const light_color_rgb &rhs ) const { return !( *this == rhs ); }

    // HSV → RGB conversion (H in degrees [0,360), S/V in [0,1])
    static light_color_rgb from_hsv( float h, float s, float v );
};

// Dawn/dusk tint: returns cached warm color for twilight or empty outside.
light_color_rgb dawn_dusk_color_for_lightmap( std::string_view dimension );

// Converts a JSON light colour ( std::optional<RGBColor>, 0-255, a == 0 means unset )
// to the normalised energy form the CPU colour lane uses.  Mirrors color_is_set() in
// src/compute/gpu_lm.cpp so the CPU and GPU light paths agree on what counts as
// "no colour".
auto light_color_from_json( const std::optional<RGBColor> &color ) -> light_color_rgb;

#define LIGHT_RANGE(b) static_cast<int>( -std::log(LIGHT_AMBIENT_LOW / static_cast<float>(b)) * (1.0 / LIGHT_TRANSPARENCY_OPEN_AIR) )

enum class lit_level : int {
    DARK = 0,
    LOW, // Hard to see
    BRIGHT_ONLY, // bright but indistinct
    LIT,
    BRIGHT, // Probably only for light sources
    MEMORIZED, // Not a light level but behaves similarly
    BLANK // blank space, not an actual light level
};

template<typename T>
constexpr bool operator>( const T &lhs, const lit_level &rhs )
{
    return lhs > static_cast<T>( rhs );
}

template<typename T>
constexpr bool operator<=( const T &lhs, const lit_level &rhs )
{
    return !operator>( lhs, rhs );
}

template<typename T>
constexpr bool operator!=( const lit_level &lhs, const T &rhs )
{
    return static_cast<T>( lhs ) != rhs;
}

inline std::ostream &operator<<( std::ostream &os, const lit_level &ll )
{
    return os << static_cast<int>( ll );
}

enum vision_adjustment {
    VISION_ADJUST_NONE,
    VISION_ADJUST_SOLID,
    VISION_ADJUST_HIDDEN
};


