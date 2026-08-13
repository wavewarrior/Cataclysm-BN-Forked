#pragma once

#include <cmath>
#include <numbers>

/// Geometry for the spinning DNA helix on the TRAITS creator step. Pure trigonometry, split out of
/// newcharacter_ui.cpp so the shape can be exercised without an RmlUi document — and because the
/// one property that makes it read as a helix rather than as two unrelated wobbling dots is an
/// invariant worth pinning: the two backbones are ANTIPHASE at every rung and every phase.
///
/// A double helix viewed side-on is two sine waves half a turn apart. Spinning the model about its
/// vertical axis is therefore not a rotation of anything — it is advancing that shared phase, which
/// is why nothing here rotates and no transform is involved. Each rung's horizontal positions come
/// from sin, and which backbone is nearer the viewer comes from cos of the same angle.
namespace nc_dna
{

/// Rungs drawn. Each is one flex row of the strand column, so they divide whatever height the
/// column has and the strand rescales with the window without any pixel arithmetic.
constexpr int rungs = 22;

/// Radians of twist between one rung and the next. 22 rungs * 0.55 rad is a little under two full
/// turns down the strand, which reads as a helix rather than as a single lazy wave.
constexpr float twist_per_rung = 0.55F;

/// Turns per second. Slow on purpose: this sits beside a list the player is reading, and anything
/// brisk enough to notice while scanning names would be a distraction rather than flair.
constexpr float spin_turns_per_sec = 0.09F;

/// Horizontal travel available to a backbone, in dp. MUST equal `.nc-dna-wrap`'s width minus
/// `.nc-dna-dot`'s width in data/gui/newchartraits.rcss (120dp box, 10dp dot) — change either and
/// this must change with it, or the dots walk out of the column. Named here rather than left in the
/// producer so the coupling is stated once, in the same place as the rest of the geometry.
constexpr float travel_dp = 100.0F;

/// Phase for a given elapsed time, in radians.
inline auto phase_at( float seconds ) -> float
{
    return seconds * spin_turns_per_sec * 2.0F * std::numbers::pi_v<float>;
}

struct rung {
    /// Position of each backbone across the strand's width, 0 (far left) .. 1 (far right).
    float a = 0.0F;
    float b = 0.0F;
    /// True when backbone A is the one nearer the viewer. Drives the brightness that sells depth;
    /// it flips every half turn, which is exactly when the two backbones cross.
    bool a_front = true;
};

inline auto at( int index, float phase ) -> rung
{
    const float t = phase + static_cast<float>( index ) * twist_per_rung;
    const float s = std::sin( t );
    rung r;
    // ONE expression drives both backbones, so they cannot drift out of antiphase. Writing them as
    // two independent sines with a hand-added pi is how that invariant gets broken later.
    r.a = 0.5F + 0.5F * s;
    r.b = 0.5F - 0.5F * s;
    r.a_front = std::cos( t ) >= 0.0F;
    return r;
}

/// Left edge and span of the pair, as fractions of the strand width. The caller turns these into
/// a leading gap and a bond width, which is all the markup needs — no absolute positioning, which
/// RmlUi resolves against the wrong ancestor in this document (see the balance scale).
struct span {
    float left = 0.0F;
    float width = 0.0F;
    /// True when the LEFT-hand dot of the pair is the one nearer the viewer.
    bool left_front = true;
};

inline auto span_of( const rung &r ) -> span
{
    const bool a_is_left = r.a <= r.b;
    return { .left = a_is_left ? r.a : r.b,
             .width = std::abs( r.b - r.a ),
             .left_front = a_is_left ? r.a_front : !r.a_front };
}

} // namespace nc_dna
