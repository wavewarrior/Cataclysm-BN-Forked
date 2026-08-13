#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

/// Geometry for the aptitude radar on the SKILLS creator step. Pure arithmetic over cell
/// coordinates, split out of newcharacter_ui.cpp so the shape can be exercised without an RmlUi
/// document — and because the one property that makes it read as a radar rather than as a dot field
/// twinkling is worth pinning: the glow TRAILS the beam head and is exactly zero ahead of it, so the
/// direction of the sweep is unambiguous. That is the rule `nc_bio_scan` pins in rows; this one pins
/// it in angle, across the 2pi wrap that a linear sweep does not have.
///
/// A circle drawn on a square grid, with no transform and nothing positioned: the disc is `grid`
/// flex rows of `grid` flex dots, and every dot's membership of the disc, of a ring and of a sector
/// is decided here from its indices alone. Overlaying rotated bars at one centre would need
/// `position: absolute`, which these documents resolve against the wrong ancestor.
///
/// Nothing here knows about skills, colours or RmlUi. The producer turns an intensity into the ALPHA
/// of one colour per layer, because on a dark ground that is what the compositor is for and it means
/// no colour arithmetic exists to get wrong.
namespace nc_apt
{

/// Dots per side. Odd, so there is a centre dot for the hub to sit on and the sectors to meet at.
constexpr int grid = 13;

/// Cells from the centre dot to the rim, i.e. the disc radius in cell units, and also the index of
/// the middle row and column.
constexpr int mid = grid / 2;

/// Seconds for one revolution of the beam. Slow on purpose: this sits beside a list the player is
/// reading, and anything brisk enough to notice while scanning names is a distraction.
constexpr float sweep_secs = 5.5F;

/// Radians of trailing glow behind the beam head. Below ~0.5 the sweep reads as a few dots blinking
/// rather than a beam; much above 2 a third of the disc is lit at once and the motion disappears.
constexpr float tail = 1.15F;

/// Dim rings drawn under the sector wedges, at radii `mid * k / rings` for k in 1..rings — here 2, 4 and 6
/// cells. Three is what gives the disc a readable scale; one ring reads as a border and five leaves
/// no dark field between them at this resolution.
constexpr int rings = 3;

/// How near a ring's radius a dot must be, in cells, to be drawn as part of it. 0.4 is under half a
/// cell, so the diagonal dots of a ring join it without the next dot out also qualifying.
constexpr float ring_tol = 0.4F;

/// Categories divide the disc into equal SECTORS — filled wedges out to each category's reach —
/// rather than into thin radial spokes. On a 13-cell lattice a spoke is at most one dot wide per
/// ring, which is too little ink to compare two categories at a glance; a wedge fills area, so the
/// comparison is between shapes rather than between hairlines. Sector 0 begins at 12 o'clock and
/// they run clockwise, the way a pie is read.

/// A dot's offset from the centre in cell units, in a MATHS frame: +y is UP, not down. The producer
/// hands over screen row/col and `offset_of` does the flip, so the trigonometry below is written once
/// in the frame it is natural in instead of carrying a sign flip through every function.
struct vec {
    float x = 0.0F;
    float y = 0.0F;
};

/// Grid indices (0..grid-1, row 0 at the TOP) to that frame. Invariant: the centre dot maps to the
/// origin, which is what makes the disc mask symmetric under both reflections.
inline auto offset_of( int col, int row ) -> vec
{
    return { .x = static_cast<float>( col - mid ), .y = static_cast<float>( mid - row ) };
}

/// Distance from the centre in cells.
inline auto radius_of( vec v ) -> float
{
    return std::hypot( v.x, v.y );
}

/// Radius of the disc in cells. `mid` plus a little slack, so the four rim dots at exactly `mid` and
/// their diagonal neighbours are all part of it. A category at full depth fills its wedge out to
/// THIS, not to `mid` — scaling to `mid` leaves the outermost dots dark however much the player
/// invested, which reads as a wedge that never quite arrives.
inline auto disc_radius() -> float
{
    return static_cast<float>( mid ) + 0.35F;
}

/// Whether a dot is part of the disc at all: everything outside is drawn as nothing, and that
/// omission is the only reason a square grid reads as a circle.
inline auto inside( vec v ) -> bool
{
    return radius_of( v ) <= disc_radius();
}

/// An angle wrapped into `[0, 2pi)`. Shared by every function below that produces or consumes one:
/// a second copy of this arithmetic is exactly how the discontinuity at the wrap — a visible flicker
/// once per revolution — gets reintroduced. Invariant: the range is HALF-OPEN, so a tiny negative
/// input cannot round up to a full turn and land outside it.
inline auto wrap_turn( float radians ) -> float
{
    constexpr float turn = 2.0F * std::numbers::pi_v<float>;
    float m = std::fmod( radians, turn );
    if( m < 0.0F ) {
        m += turn;
    }
    return m < turn ? m : 0.0F;
}

/// Bearing of a dot: radians in `[0, 2pi)`, 0 = UP and increasing CLOCKWISE. The centre returns 0.
///
/// `atan2( x, y )`, deliberately not `atan2( y, x )`: the frame is not the mathematical convention
/// because the beam must start pointing up and sectors must be numbered clockwise from there, which
/// is how a radar is read. Swapping the arguments back mirrors the whole display.
inline auto angle_of( vec v ) -> float
{
    if( v.x == 0.0F && v.y == 0.0F ) {
        return 0.0F;
    }
    return wrap_turn( std::atan2( v.x, v.y ) );
}

/// Beam bearing for a given elapsed time, one revolution per `sweep_secs`, in `[0, 2pi)`.
///
/// Wall-clock seconds, so the sweep neither accelerates while a key is held nor stalls when the
/// player stops typing. Invariant: exactly periodic in `sweep_secs`, so the loop cannot drift.
inline auto beam_at( float seconds ) -> float
{
    const float t = std::fmod( seconds, sweep_secs ) / sweep_secs;
    return wrap_turn( t * 2.0F * std::numbers::pi_v<float> );
}

/// How far the beam has already travelled past `theta`, in radians, wrapped into `[0, 2pi)`. An
/// angle just AHEAD of the head therefore reports nearly a full turn, which is what lets `glow`
/// reject it with the same comparison it uses for a stale tail.
inline auto behind( float beam, float theta ) -> float
{
    return wrap_turn( beam - theta );
}

/// Brightness at bearing `theta`, 0 (dark) .. 1 (the head is on it).
///
/// Ahead of the head is zero, NOT a symmetric falloff: a symmetric glow makes the direction of
/// travel ambiguous, which is the entire content of the animation. Invariant: continuous across the
/// 2pi wrap, because the only angular arithmetic is `behind`.
inline auto glow( float beam, float theta ) -> float
{
    const float b = behind( beam, theta );
    if( b > tail ) {
        return 0.0F;
    }
    return 1.0F - b / tail;
}

/// Whether a radius falls on one of the rings. Invariant: the rings never overlap, since `ring_tol`
/// is well under half the `mid / rings` spacing.
inline auto on_ring( float r_cells ) -> bool
{
    for( int k = 1; k <= rings; k++ ) {
        const float want = static_cast<float>( mid ) * static_cast<float>( k ) /
                           static_cast<float>( rings );
        if( std::fabs( r_cells - want ) <= ring_tol ) {
            return true;
        }
    }
    return false;
}

/// Which sector a bearing falls in, `0 .. n-1`, sector 0 starting at 12 o'clock and running
/// clockwise. A non-positive `n` answers 0 rather than dividing by zero, which is the degenerate
/// case of a category list that came back empty.
///
/// Invariant: the sectors PARTITION the turn — every bearing belongs to exactly one, with no gap at
/// the wrap and none between neighbours. That is why the upper bound is clamped rather than
/// wrapped: a bearing one ULP under 2pi must land in the last sector, not spill into a phantom
/// `n`-th one.
inline auto sector_of( float theta, int n ) -> int
{
    if( n <= 0 ) {
        return 0;
    }
    const float step = 2.0F * std::numbers::pi_v<float> / static_cast<float>( n );
    return std::clamp( static_cast<int>( wrap_turn( theta ) / step ), 0, n - 1 );
}

/// Whether the dot at `off` is inside sector `k`'s filled wedge, i.e. in that sector and no further
/// out than `reach_cells`.
///
/// Invariant: monotone in reach — growing a sector only ever adds dots, never moves one. The centre
/// dot is in sector 0 by convention (its bearing is 0); the hub layer paints over it anyway.
inline auto in_sector( vec off, int k, int n, float reach_cells ) -> bool
{
    return radius_of( off ) <= reach_cells && sector_of( angle_of( off ), n ) == k;
}

/// Intensity as an 8-bit alpha. Clamped rather than trusted: `glow` is exact at the ends, but a
/// future easing curve overshooting 1 would silently wrap the byte.
inline auto alpha_of( float intensity_01 ) -> int
{
    return std::clamp( static_cast<int>( std::lround( intensity_01 * 255.0F ) ), 0, 255 );
}

} // namespace nc_apt
