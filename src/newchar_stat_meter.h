#pragma once

/// Pure point arithmetic behind the STATS creator tab. Split out of newcharacter_ui.cpp so
/// the boundaries can be exercised without standing up an RmlUi document or a game world.
///
/// Every number here is an off-by-one waiting to happen, which is the reason it is a header
/// rather than four more inline branches:
///
/// `HIGH_STAT` is the value AT which the next increment starts costing two points, so the
/// last pip that costs ONE is `HIGH_STAT` itself and the first that costs TWO is
/// `HIGH_STAT + 1`. Shift that by one and the meter's expensive band is simply drawn in the
/// wrong place — nothing on screen looks broken, and the pips quietly disagree with the
/// points total.
///
/// The three functions also collapse what used to be eight near-identical arms in
/// `set_stats`' input loop (four stats x two directions, each restating the double-cost rule).
namespace nc_stat_meter
{

/// Lowest value the creator will reduce a stat to. The first `floor_val` pips are granted and
/// can never be sold back, so they are not part of the spendable range.
constexpr int floor_val = 4;

enum class tier {
    /// Inside the granted floor.
    base,
    /// One point per step.
    cheap,
    /// Two points per step.
    steep,
};

/// Which tier the pip standing for value `val` (1-based) belongs to. `high` is `HIGH_STAT`.
constexpr auto pip_tier( int val, int high ) -> tier
{
    if( val <= floor_val ) {
        return tier::base;
    }
    return val > high ? tier::steep : tier::cheap;
}

/// Points the NEXT increment costs for a stat sitting at `val`, or 0 when it is capped.
constexpr auto next_cost( int val, int high, int max ) -> int
{
    if( val >= max ) {
        return 0;
    }
    return val >= high ? 2 : 1;
}

/// Points returned by decrementing a stat sitting at `val`, or 0 when it is at the floor.
constexpr auto refund( int val, int high ) -> int
{
    if( val <= floor_val ) {
        return 0;
    }
    return val > high ? 2 : 1;
}

} // namespace nc_stat_meter
