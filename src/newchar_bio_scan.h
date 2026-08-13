#pragma once

#include <algorithm>
#include <cmath>

/// Geometry for the diagnostic scan sweep on the BIONICS creator step's chassis diagram. Pure
/// arithmetic, split out of newcharacter_ui.cpp so the shape can be exercised without an RmlUi
/// document — and because the property that makes it read as a SWEEP rather than as rows blinking
/// is worth pinning: intensity is zero ahead of the scan head and decays behind it, so the glow
/// trails the pulse the way a CRT scan does.
///
/// Nothing here is a transform or an offset. The producer turns `intensity` into the ALPHA of one
/// gold colour per body row, and two elements consume it — the bus rail segment beside that row and
/// the hairline scanline inside its slot boxes. Alpha rather than a colour blend because the
/// compositor already knows how to fade gold into a dark ground, and because a wrong blend is
/// invisible until someone retunes the theme.
namespace nc_bio_scan
{

/// Body rows in the chassis grid. Matches the doll's six rows in newcharacter_ui.cpp; the sweep
/// travels one row at a time so the rail cannot drift out of step with the grid.
constexpr int rows = 6;

/// Seconds for one head-to-feet pass, including the dark gap after it. Slow on purpose: this sits
/// beside a list the player is reading.
constexpr float sweep_secs = 3.2F;

/// How many rows of trailing glow follow the head. Below ~1 the sweep reads as a single row
/// blinking; much above 2 the whole rail is lit at once and the motion disappears.
constexpr float tail = 1.7F;

/// Rows the head travels per pass. It must both START a full tail above the skull (so the first
/// row fades in rather than popping on) and END a full tail below the LAST row (so that row's
/// trailing glow has faded before the head wraps back to the top). The last row is at index
/// `rows - 1`, hence `(rows - 1) + 2 * tail` — a plain `rows + tail` clears the glow only while
/// `tail <= 1`, and this one is deliberately larger, so the bottom box would flash at 0.41
/// brightness once per pass.
constexpr float travel_rows = static_cast<float>( rows ) - 1.0F + 2.0F * tail;

/// Scan-head position in rows for a given elapsed time. Starts at -tail so the first pass fades in
/// from above the head rather than popping on at full brightness.
inline auto head_at( float seconds ) -> float
{
    const float t = std::fmod( seconds, sweep_secs ) / sweep_secs;
    return t * travel_rows - tail;
}

/// Brightness of one body row, 0 (dark) .. 1 (the head is on it).
///
/// Ahead of the head is zero, NOT a symmetric falloff: a scan leaves a wake, and a symmetric glow
/// makes the direction of travel ambiguous — which is the whole content of the animation.
inline auto intensity( float head, int row ) -> float
{
    const float behind = head - static_cast<float>( row );
    if( behind < 0.0F || behind > tail ) {
        return 0.0F;
    }
    return 1.0F - behind / tail;
}

/// Intensity as an 8-bit alpha. Clamped rather than trusted: `intensity` is exact at the ends, but
/// a future easing curve overshooting 1 would silently wrap the byte.
inline auto alpha_of( float intensity_01 ) -> int
{
    return std::clamp( static_cast<int>( std::lround( intensity_01 * 255.0F ) ), 0, 255 );
}

} // namespace nc_bio_scan
