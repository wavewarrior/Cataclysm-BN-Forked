#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>

/// Pure geometry for the tipping balance scale on the TRAITS and BIONICS creator
/// tabs. Split out of newcharacter_ui.cpp so the numbers can be exercised without
/// standing up an RmlUi document or a game world.
///
/// The scale is two independent RCSS animations that must agree: the BEAM rotates
/// (`transform`) and the PANS are nudged vertically (`top`). The pans are
/// deliberately NOT children of the beam — as children they would inherit its
/// rotation and the numbers would render tilted — so nothing structural keeps the two
/// in step.
///
/// Deriving both from a single balance value is what keeps them consistent. An earlier
/// version picked the tilt (8deg) and the pan travel (20dp) as two independent
/// constants and shipped them with OPPOSITE signs, which moved beam and pans equal
/// distances in opposite directions. Anything a caller can get wrong separately is
/// computed here instead.
namespace nc_scale
{

/// Half of `.nc-scale-beam`'s width in data/gui/newchar_common.rcss. Change that
/// width and this must change with it, or the beam ends stop meeting the pans.
constexpr float beam_half_dp = 140.0F;

/// Beam rotation at full imbalance.
constexpr float max_tilt_deg = 8.0F;

/// Degrees to radians, for the beam-rise trigonometry below.
constexpr float deg_to_rad = std::numbers::pi_v<float> / 180.0F;

/// Vertical travel of one pan at full imbalance.
///
/// `.nc-scale-arm`'s min-height must leave room for twice this plus a row of text, or
/// a pan escapes the row at high tilt — a state only reachable when one side of the
/// budget is fully spent and the other is empty.
inline auto max_pan_dp() -> float
{
    return beam_half_dp * std::sin( max_tilt_deg * deg_to_rad );
}

struct geometry {
    /// -1 (disadvantages outweigh) .. 0 (level) .. +1 (advantages outweigh).
    float bal = 0.0F;
    /// Degrees for RCSS `rotate()`, which is CLOCKWISE. The good pan is on the LEFT,
    /// so sinking that end needs a NEGATIVE angle to match a positive `good_dp`.
    float tilt_deg = 0.0F;
    /// Pan offsets for `top`, where POSITIVE sinks. Always exact opposites.
    float good_dp = 0.0F;
    float bad_dp = 0.0F;
};

/// `bad` arrives as a NEGATIVE running total (the accumulated cost of bad traits), so
/// its magnitude is the weight on the bad pan. A `cap` of 0 is treated as 1 rather
/// than dividing by zero.
inline auto compute( int good, int bad, int cap ) -> geometry
{
    const auto c = static_cast<float>( std::max( 1, cap ) );
    const auto w = static_cast<float>( good - std::abs( bad ) );
    geometry g;
    g.bal = std::clamp( w / c, -1.0F, 1.0F );
    g.tilt_deg = -g.bal * max_tilt_deg;
    // The pan sits where the beam END actually is: rise = half_length * sin(tilt).
    //
    // Scaling the pan linearly in `bal` instead (`bal * max_pan_dp()`) looks equivalent
    // and is not: sin is not linear, so `bal*sin(8deg) != sin(bal*8deg)`. That version
    // agreed with the beam only at 0 and full tilt and drifted in between — caught by
    // nc_scale_pan_travel_matches_the_beam_rise, not by looking at the screen, because
    // the worst-case gap is a fraction of a dp.
    g.good_dp = beam_half_dp * std::sin( -g.tilt_deg * deg_to_rad );
    g.bad_dp = -g.good_dp;
    return g;
}

} // namespace nc_scale
