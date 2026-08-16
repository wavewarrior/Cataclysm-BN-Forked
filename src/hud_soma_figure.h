#pragma once
#ifndef CATA_SRC_HUD_SOMA_FIGURE_H
#define CATA_SRC_HUD_SOMA_FIGURE_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

/// Geometry and summary arithmetic for the SOMA panel's body figure and its
/// collapsed one-line reading. Pure arithmetic over plain numbers, split out of
/// `hud_runic_panels.cpp` so the invariants can be exercised without an RmlUi
/// document — the same split `newchar_aptitude.h` and `newchar_dna.h` make, and
/// for the same reason: this is the only part of the feature verifiable without
/// looking at the screen.
///
/// Nothing here knows about `avatar`, `bodypart_id`, colours or RmlUi. The
/// producer reads the game state, fills a `body_state`, and turns the answers
/// below into markup.
///
/// **The figure is six flex boxes in pure flow, not a raster.** `position:
/// absolute` resolves against the wrong ancestor in these documents, so nothing
/// can be overlaid at a shared centre; and six elements rather than a hundred
/// cells is what makes the animation possible at all, because `hud_anim`
/// addresses elements by id and a raster's cells cannot be grouped per limb.
///
/// **Orientation: the avatar seen from BEHIND**, so a limb's side on screen
/// matches its name and `L ARM` lights on the left. The anatomical-chart
/// convention — the figure facing the viewer, its left on your right — is
/// correct for a medical diagram and wrong here, because the figure sits
/// directly beside a column of rows labelled `L ARM` / `R ARM` and the two
/// would contradict each other.
namespace hud_figure
{

/// Canonical display order, matching `data/json/body_parts.json` `sort_order`
/// (head 0, torso 100, arm_l 200, arm_r 300, leg_l 500, leg_r 600). The producer
/// iterates `get_all_body_parts( true )`, which yields that order, and indexes
/// this enum by position.
enum class limb : int {
    head,
    torso,
    arm_l,
    arm_r,
    leg_l,
    leg_r,
};

inline constexpr std::size_t limb_count = 6;

/// Everything the figure and the summary need about one body part. Mirrors what
/// `hud_soma` already reads per part; no new game state is consulted.
struct limb_state {
    int cur = 0;
    int max = 0;
    bool bleeding = false;
    bool bitten = false;
    bool infected = false;
    bool broken = false;
    bool splinted = false;
    bool critical = false;  ///< `hud_runic::is_critical`, decided by the producer
};

struct body_state {
    std::array<limb_state, limb_count> limbs;
};

/// Conditions present ANYWHERE on the body, for the collapsed chip row. These
/// are the states the player must never have to expand a card to see: the first
/// two tick damage between turns.
struct conditions {
    bool bleeding = false;
    bool bitten = false;
    bool infected = false;
    bool broken = false;

    auto any() const -> bool { return bleeding || bitten || infected || broken; }
};

/// A limb's health, 0..1. A non-positive `max` reads as 0 rather than dividing:
/// a mod may define a part with no hit points, and a HUD that crashes on one is
/// worse than a HUD that draws it empty.
inline auto ratio_of( const limb_state &l ) -> float
{
    if( l.max <= 0 ) {
        return 0.0f;
    }
    return std::clamp( static_cast<float>( l.cur ) / static_cast<float>( l.max ), 0.0f, 1.0f );
}

/// Whole-body health, 0..1: sum of current over sum of maximum.
///
/// Max-weighted on purpose — limb maxima differ (`base_hp + str * 3`, and
/// GLASSJAW cuts the head by 20%, `character.cpp:1341-1343`), so a torso point
/// is worth more than an arm point and a mean of ratios would say otherwise.
/// The cost of that choice is that one crippled limb hides inside five healthy
/// ones, which is exactly why `worst_limb` sits beside this in the collapsed
/// row rather than the percentage standing alone.
inline auto overall_ratio( const body_state &b ) -> float
{
    auto cur = 0;
    auto max = 0;
    for( const limb_state &l : b.limbs ) {
        if( l.max > 0 ) {
            cur += std::clamp( l.cur, 0, l.max );
            max += l.max;
        }
    }
    if( max <= 0 ) {
        return 0.0f;
    }
    return std::clamp( static_cast<float>( cur ) / static_cast<float>( max ), 0.0f, 1.0f );
}

/// How urgent a limb's state is, for tie-breaking only. Bleeding and bitten tick
/// damage between turns, so they outrank a fracture, which outranks plain
/// damage.
inline auto severity_of( const limb_state &l ) -> int
{
    if( l.bleeding || l.bitten ) {
    return 3;
}
if( l.infected ) {
    return 2;
}
if( l.broken ) {
    return 1;
}
return 0;
}

/// The limb the player should look at.
///
/// Lowest health ratio wins. A TIE is broken by severity, then by canonical
/// order — without the severity term two limbs at 50% would report whichever
/// happened to come first, and the bleeding one is the answer the player needs.
/// Always names a limb: on an undamaged body it reports the head, which is
/// truthful (nothing is worse than anything else) and keeps the collapsed row a
/// fixed shape instead of appearing and disappearing.
inline auto worst_limb( const body_state &b ) -> limb
{
    auto best = static_cast<std::size_t>( 0 );
    for( std::size_t k = 1; k < limb_count; ++k ) {
        const auto r = ratio_of( b.limbs[k] );
        const auto r_best = ratio_of( b.limbs[best] );
        if( r < r_best ||
            ( r == r_best && severity_of( b.limbs[k] ) > severity_of( b.limbs[best] ) ) ) {
            best = k;
        }
    }
    return static_cast<limb>( best );
}

inline auto conditions_of( const body_state &b ) -> conditions
{
    conditions c;
    for( const limb_state &l : b.limbs ) {
        c.bleeding = c.bleeding || l.bleeding;
        c.bitten = c.bitten || l.bitten;
        c.infected = c.infected || l.infected;
        c.broken = c.broken || l.broken;
    }
    return c;
}

/// Element id for a limb's box in the figure.
///
/// Load-bearing and stable: `hud_anim` feeds and forgets by exactly this string,
/// and a fed element that leaves the DOM without being forgotten keeps its
/// registry key alive — which holds `sidebar_requires_animation()` true and pins
/// the game to 33 ms input plus full-screen redraws.
inline auto element_id( limb l ) -> std::string_view
{
    constexpr std::array<std::string_view, limb_count> ids = {
        "hud-soma-fig-head", "hud-soma-fig-torso", "hud-soma-fig-arml",
        "hud-soma-fig-armr", "hud-soma-fig-legl", "hud-soma-fig-legr"
    };
    const auto k = static_cast<std::size_t>( l );
    return ids[k < limb_count ? k : 0];
}

/// The layout class for a limb's box, paired with `element_id` by position.
inline auto shape_class( limb l ) -> std::string_view
{
    constexpr std::array<std::string_view, limb_count> shapes = {
        "hud-fig-head", "hud-fig-torso", "hud-fig-arm",
        "hud-fig-arm", "hud-fig-leg", "hud-fig-leg"
    };
    const auto k = static_cast<std::size_t>( l );
    return shapes[k < limb_count ? k : 0];
}

/// Fill class for a limb, from its health and state.
///
/// FOUR levels, not five, and the dimmest is `bg4` — measured, not chosen. An
/// earlier draft started the ladder at `bg2` (luma 74) so that a healthy limb
/// "receded"; over the panel's own `hud-veil` ground it receded out of
/// existence, and a near-full-health body — the common case — drew six boxes at
/// a peak luma of 69 that read as an empty panel. A limb filled with something
/// indistinguishable from the background is a limb that has vanished, which is
/// the one thing this element exists not to do.
///
/// So the floor is legibility and the direction is the register's severity rule:
/// **intact recedes, damage advances.** Healthy sits at `bg4`, visible but quiet;
/// damage brightens through `fg4` and `fg`; critical reaches `fg0` and also takes
/// the gold outline. All four are rungs of the same `hud_runic::ink` ladder every
/// other producer uses, so the figure encodes health as LUMINANCE and survives
/// greyscale and every form of colour blindness. There is no red and no second
/// hue.
///
/// Four rather than five because four is what the data distinguishes — healthy,
/// grazed, hurt, critical — and a fifth step between `fg` and `fg0` is 20 luma
/// apart, which is under the just-noticeable difference on a 10 dp box.
inline auto fill_class( const limb_state &l ) -> std::string_view
{
    if( l.critical ) {
    return "hud-fig-f4";
}
if( l.broken ) {
    // A fracture is not `critical` on its own (see `hud_runic::is_critical`),
    // but at hp == 0 the ratio test below would answer the brightest rung and
    // shout as loudly as a bleeding limb. Splinted is being dealt with and
    // recedes a step; unsplinted still wants the eye.
    return l.splinted ? "hud-fig-f2" : "hud-fig-f3";
}
const auto r = ratio_of( l );
if( r >= 1.0f ) {
    return "hud-fig-f1";
}
if( r >= 2.0f / 3.0f ) {
    return "hud-fig-f2";
}
if( r >= 1.0f / 3.0f ) {
    return "hud-fig-f3";
}
return "hud-fig-f4";
}

// ── Figure metrics ──────────────────────────────────────────────────────────
//
// These four are the RCSS boxes' sizes restated for the height budget in
// `plans/hud-soma-collapsible-figure.md`. Each MUST agree with the rule named
// beside it in `data/gui/sidebar_hud.rcss`; a dp constant shared between a
// producer and a stylesheet drifts the moment only one of them is edited, and
// the split below only fits because these numbers are what they are.

inline constexpr float fig_w = 46.0f;   ///< `.hud-fig` width:  8 + 2 + 22 + 2 + 8 + 4 margins
inline constexpr float fig_h = 78.0f;   ///< `.hud-fig` height: 12 + 2 + 32 + 2 + 26 + 4 padding
inline constexpr float fig_gap = 8.0f;  ///< `.hud-fig` margin-right

/// Width the detail rows keep beside the figure, given the panel's inner width.
///
/// The narrowest column the layout produces is 300 dp, whose inner width is
/// 286 dp after the 1 dp border pair and the 6 dp body padding pair — leaving
/// 232 dp here, of which `.hud-cell-name` and `.hud-cell-val` take 74 each and
/// the twelve-pip meter gets the remaining 84. That is the arithmetic that
/// decides the figure can sit beside the rows rather than above them, which in
/// turn is why the expanded panel's height does not grow.
inline auto rows_width( float panel_inner_w ) -> float
{
    return std::max( 0.0f, panel_inner_w - fig_w - fig_gap );
}

} // namespace hud_figure

#endif // CATA_SRC_HUD_SOMA_FIGURE_H
