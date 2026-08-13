#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

#include "newchar_aptitude.h"

/// Geometry for the RECORD SEAL on the OVERVIEW creator step: a ring carrying one node per earlier
/// creator step, welded shut once per cycle. Pure arithmetic, split out of newcharacter_ui.cpp so
/// the shape can be exercised without an RmlUi document — and because the properties that make it
/// read as a seal being STRUCK rather than as a ring of dots twinkling are worth pinning:
///
///   * thread ahead of the bead is dark, thread behind it STAYS lit, and the bead is brightest. That
///     is `nc_bio_scan`'s rule (nothing ahead, decay behind) with a floor under the decay, because a
///     weld leaves a bead where a scan leaves a fading wake;
///   * closing the ring flares the hub and holds the whole ring bright for a beat. That flare is the
///     single deliberate discontinuity in the cycle, and everything else is continuous across the
///     cycle boundary so the loop does not pop;
///   * the seven nodes sit at the bearings a seven-slice pie would use, node 0 at 12 o'clock running
///     clockwise, so the ring reads as the step rail across the top of the screen closed into a loop.
///
/// The lattice and the angle arithmetic are `nc_apt`'s, deliberately NOT copied. That header states
/// the reason: a second copy of `wrap_turn` is exactly how the discontinuity at the 2pi wrap — a
/// flicker once per revolution — gets reintroduced. The seal therefore sits on the same 13x13 dot
/// lattice the aptitude radar does, which also keeps the two widgets visually of a piece.
///
/// Nothing here is a transform or an offset, and nothing knows about steps, colours or RmlUi. The
/// producer turns a glow into the ALPHA of one colour per layer; on a dark ground that is what the
/// compositor is for, and it means no colour arithmetic exists to get wrong.
namespace nc_seal
{

/// Nodes on the ring: the seven creator steps this OVERVIEW summarises, POINTS through SKILLS. The
/// eighth step is OVERVIEW itself, which is the hub rather than a node — it is the record the other
/// seven feed.
constexpr int nodes = 7;

/// Ring radius in cells. `nc_apt::mid` is 6, so 4.6 leaves a clear cell of margin outside the ring
/// for the flare to sit in and keeps every node cell inside the grid.
constexpr float ring_r = 4.6F;

/// How near the ring's radius a cell must be, in cells, to be part of the thread. 0.55 is the
/// smallest band that catches every one of the seven node cells as well — a node sitting off the
/// thread would break the ring visibly at that node, which is the one defect a seal cannot have.
constexpr float ring_tol = 0.55F;

/// Seconds the bead spends on one segment, i.e. between two nodes.
constexpr float weld_secs = 0.55F;

/// Seconds the closed ring holds after the strike, before the next pass starts from dark.
constexpr float hold_secs = 1.6F;

constexpr float weld_total = static_cast<float>( nodes ) * weld_secs;
constexpr float cycle_secs = weld_total + hold_secs;

/// Radians of bead behind the head. Small on purpose: the trail is not what shows progress — the
/// steady `weld_level` behind it is — so this only has to be wide enough to read as a hot spot.
constexpr float tail = 0.55F;

/// Steady brightness of thread the bead has already passed, 0..1.
constexpr float weld_level = 0.42F;

/// Uniform brightness of the whole ring during the hold, once the strike has closed it.
constexpr float struck_level = 0.78F;

/// Hub glow at the end of the weld, i.e. just before the strike. The strike takes it to 1.
constexpr float hub_warm = 0.55F;

/// State of one animation cycle at a given time.
struct phase {
    /// Radians of ring welded so far this pass, 0 .. 2pi. Only meaningful while `!struck`.
    float arc = 0.0F;
    /// The ring is closed and holding. A FLAG rather than `arc == 2pi`, and that is not a style
    /// choice: `nc_apt::wrap_turn` is half-open, so a full turn wraps to 0 and every bearing would
    /// report itself un-welded — the closed ring would render dark at the exact moment it completes.
    bool struck = false;
    /// Central emblem glow, 0..1.
    float hub = 0.0F;
};

/// Wall-clock seconds to a cycle state. Wall clock rather than a frame counter, so the weld neither
/// accelerates while a key is held nor stalls when the player stops typing.
///
/// Invariant: continuous across the cycle boundary — the hub decays to exactly 0 by the end of the
/// hold, which is where the next pass starts it.
inline auto at( float seconds ) -> phase
{
    constexpr float turn = 2.0F * std::numbers::pi_v<float>;
    const float t = std::fmod( std::max( 0.0F, seconds ), cycle_secs );
    if( t < weld_total ) {
        const float f = t / weld_total;
        return { .arc = f * turn, .struck = false, .hub = hub_warm * f };
    }
    const float h = ( t - weld_total ) / hold_secs;
    return { .arc = turn, .struck = true, .hub = std::clamp( 1.0F - h, 0.0F, 1.0F ) };
}

/// Bearing of node `i`: 0 = up, increasing clockwise, the frame `nc_apt::angle_of` produces.
inline auto node_bearing( int i ) -> float
{
    constexpr float turn = 2.0F * std::numbers::pi_v<float>;
    return nc_apt::wrap_turn( static_cast<float>( i ) * turn / static_cast<float>( nodes ) );
}

struct cell {
    int col = 0;
    int row = 0;
    auto operator<=>( const cell & ) const = default; // *NOPAD*
};

/// The lattice cell a node draws in: its ring position rounded to the nearest cell.
///
/// Invariant: the seven cells are distinct, and each is on the thread band — both pinned in tests,
/// because either failure is a ring that looks broken rather than one that fails to build.
inline auto node_cell( int i ) -> cell
{
    const float th = node_bearing( i );
    // Same frame as nc_apt::angle_of, which is atan2( x, y ): x runs with sin, y with cos.
    const float x = ring_r * std::sin( th );
    const float y = ring_r * std::cos( th );
    return { .col = nc_apt::mid + static_cast<int>( std::lround( x ) ),
             .row = nc_apt::mid - static_cast<int>( std::lround( y ) ) };
}

/// Which node draws in this cell, or -1.
inline auto node_at( int col, int row ) -> int
{
    for( int i = 0; i < nodes; i++ ) {
        if( node_cell( i ) == cell{ .col = col, .row = row } ) {
            return i;
        }
    }
    return -1;
}

/// Whether a cell is on the thread band. Nodes are on it too — the ring runs THROUGH them.
inline auto on_ring( int col, int row ) -> bool
{
    return std::fabs( nc_apt::radius_of( nc_apt::offset_of( col, row ) ) - ring_r ) <= ring_tol;
}

/// What a lattice cell draws. `empty` cells are drawn as nothing, which is the only reason a square
/// grid reads as a ring.
enum struct layer {
    empty,
    thread,
    node,
    hub,
};

inline auto classify( int col, int row ) -> layer
{
    if( col == nc_apt::mid && row == nc_apt::mid ) {
        return layer::hub;
    }
    if( node_at( col, row ) >= 0 ) {
        return layer::node;
    }
    return on_ring( col, row ) ? layer::thread : layer::empty;
}

/// Brightness at bearing `theta`, 0 (not yet welded) .. 1 (the bead is on it).
///
/// Three regimes, and the middle one is what distinguishes a weld from a scan: ahead of the bead is
/// zero, at the bead is 1, and behind it the glow decays to `weld_level` and STAYS there rather than
/// to zero — the ring being closed is the content of the animation, so it has to persist.
inline auto glow_at( const phase &p, float theta ) -> float
{
    if( p.struck ) {
    return struck_level;
}
const float behind = nc_apt::behind( p.arc, theta );
// `behind` wraps, so a bearing the bead has not reached yet reports nearly a full turn — always
// more than the arc travelled so far, which is what rejects it with the same comparison.
if( behind > p.arc ) {
    return 0.0F;
}
return std::max( weld_level, 1.0F - behind / tail );
}

/// Brightness of node `i`'s socket. Same curve as the thread it sits on, so a node cannot flare out
/// of step with the bead arriving at it.
inline auto node_glow( const phase &p, int i ) -> float
{
    return glow_at( p, node_bearing( i ) );
}

/// Glow as an 8-bit alpha, with a floor. Thread dots may fade to nothing; a node must not, because
/// its glyph names a step the player has already been through — a step that vanishes from the ring
/// for two thirds of the cycle reads as a rendering fault.
inline auto alpha_of( float glow_01 ) -> int
{
    return nc_apt::alpha_of( glow_01 );
}

constexpr int node_floor_alpha = 70;

inline auto node_alpha_of( float glow_01 ) -> int
{
    const float g = std::clamp( glow_01, 0.0F, 1.0F );
    return node_floor_alpha +
           static_cast<int>( std::lround( g * static_cast<float>( 255 - node_floor_alpha ) ) );
}

} // namespace nc_seal
