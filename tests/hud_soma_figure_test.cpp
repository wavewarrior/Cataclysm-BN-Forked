#include "catch/catch_amalgamated.hpp"
#include "hud_soma_figure.h"

#include <set>
#include <string>
#include <string_view>

// Contract tests for the SOMA body figure's geometry and its collapsed summary.
// This TU touches no game state, so the invariants the collapsed card's honesty
// rests on can be pinned here rather than discovered by staring at a HUD.

namespace {

using hud_figure::limb;
using hud_figure::limb_state;

/// A body at full health: six limbs, 30/30 each except a 60/60 torso, which is
/// roughly the real shape (limb maxima differ, which is the whole reason
/// `overall_ratio` is max-weighted).
auto healthy() -> hud_figure::body_state {
    hud_figure::body_state b;
    for (limb_state& l : b.limbs) {
        l.cur = 30;
        l.max = 30;
    }
    b.limbs[static_cast<int>(limb::torso)] = {.cur = 60, .max = 60};
    return b;
}

auto& at(hud_figure::body_state& b, limb l) { return b.limbs[static_cast<int>(l)]; }

} // namespace

TEST_CASE("figure_overall_ratio_is_max_weighted_and_bounded", "[hud_figure]") {
    CHECK(hud_figure::overall_ratio(healthy()) == Catch::Approx(1.0f));

    auto dead = healthy();
    for (limb_state& l : dead.limbs) { l.cur = 0; }
    CHECK(hud_figure::overall_ratio(dead) == Catch::Approx(0.0f));

    // Max-weighted: the same ABSOLUTE damage moves the figure by the same amount
    // wherever it lands, but the same RATIO of damage moves it further on the
    // torso, because the torso carries more of the body's total.
    auto torso_half = healthy();
    at(torso_half, limb::torso).cur = 30; // 50% of a 60-point part
    auto arm_half = healthy();
    at(arm_half, limb::arm_l).cur = 15; // 50% of a 30-point part
    CHECK(hud_figure::overall_ratio(torso_half) < hud_figure::overall_ratio(arm_half));

    // A part with no hit points at all must not divide, and must not drag the
    // whole body to zero — a mod can define one.
    auto zeroed = healthy();
    at(zeroed, limb::head) = {.cur = 0, .max = 0};
    CHECK(hud_figure::overall_ratio(zeroed) == Catch::Approx(1.0f));

    // Over-full clamps rather than reporting above 1.0.
    auto over = healthy();
    at(over, limb::head).cur = 999;
    CHECK(hud_figure::overall_ratio(over) <= 1.0f);
}

TEST_CASE("figure_worst_limb_breaks_ties_by_severity", "[hud_figure]") {
    // The plain case: the lowest ratio wins outright.
    auto b = healthy();
    at(b, limb::leg_r).cur = 5;
    CHECK(hud_figure::worst_limb(b) == limb::leg_r);

    // A tie must not report whichever limb happens to be first. Bleeding ticks
    // damage between turns, so it is the answer the player needs.
    auto tie = healthy();
    at(tie, limb::arm_l).cur = 15;
    at(tie, limb::leg_r).cur = 15;
    at(tie, limb::leg_r).bleeding = true;
    CHECK(hud_figure::worst_limb(tie) == limb::leg_r);

    // …and with no condition to separate them, canonical order decides, so the
    // reading is at least stable frame to frame.
    auto plain_tie = healthy();
    at(plain_tie, limb::arm_l).cur = 15;
    at(plain_tie, limb::leg_r).cur = 15;
    CHECK(hud_figure::worst_limb(plain_tie) == limb::arm_l);

    // Severity never outranks a genuinely lower ratio: a scratched bleeding arm
    // is not "worse" than a leg at 10%.
    auto ratio_wins = healthy();
    at(ratio_wins, limb::arm_l).cur = 29;
    at(ratio_wins, limb::arm_l).bleeding = true;
    at(ratio_wins, limb::leg_r).cur = 3;
    CHECK(hud_figure::worst_limb(ratio_wins) == limb::leg_r);

    // An undamaged body still names a limb, so the collapsed row keeps a fixed
    // shape instead of a field appearing and disappearing.
    CHECK(hud_figure::worst_limb(healthy()) == limb::head);
}

TEST_CASE("figure_conditions_are_body_wide_and_exact", "[hud_figure]") {
    CHECK_FALSE(hud_figure::conditions_of(healthy()).any());

    auto b = healthy();
    at(b, limb::arm_r).bleeding = true;
    const auto c = hud_figure::conditions_of(b);
    CHECK(c.bleeding);
    CHECK(c.any());
    // Exact, not a smear: one bleeding arm must not light BITTEN as well.
    CHECK_FALSE(c.bitten);
    CHECK_FALSE(c.infected);
    CHECK_FALSE(c.broken);
}

TEST_CASE("figure_fill_class_walks_the_ladder_and_stays_visible", "[hud_figure]") {
    // Intact recedes, damage advances — the register's severity rule, and the
    // reason the figure reads at a glance rather than needing to be studied.
    const auto full = hud_figure::fill_class({.cur = 30, .max = 30});
    const auto grazed = hud_figure::fill_class({.cur = 25, .max = 30});
    const auto hurt = hud_figure::fill_class({.cur = 15, .max = 30});
    const auto crit = hud_figure::fill_class({.cur = 5, .max = 30, .critical = true});

    CHECK(full == "hud-fig-f1");
    CHECK(grazed == "hud-fig-f2");
    CHECK(hurt == "hud-fig-f3");
    CHECK(crit == "hud-fig-f4");

    // A fracture is not `critical` on its own, but at hp == 0 the ratio test
    // alone would rank it as loud as a bleeding limb. Splinted is being dealt
    // with and recedes a step; unsplinted still wants the eye.
    const auto broke = hud_figure::fill_class({.cur = 0, .max = 30, .broken = true});
    const auto splint = hud_figure::fill_class(
        {.cur = 0, .max = 30, .broken = true, .splinted = true});
    CHECK(broke == "hud-fig-f3");
    CHECK(splint == "hud-fig-f2");

    // THE REGRESSION THIS PINS. The ladder's floor is `f1` == `bg4`, not the two
    // rungs below it. An earlier draft started at `bg2` so a healthy limb would
    // recede; over the panel's own veil it receded out of existence and a
    // near-full-health body — the common case — rendered as an empty panel at a
    // measured peak luma of 69. Every state, including a fully healthy one, must
    // land on a rung that is actually visible against the ground.
    for (int cur = 0; cur <= 30; ++cur) {
        CAPTURE(cur);
        const auto c = hud_figure::fill_class({.cur = cur, .max = 30});
        CHECK(c != "hud-fig-f0");
        CHECK(c != "hud-fig-f5"); // four levels, not five
        CHECK(c.starts_with("hud-fig-f"));
    }

    // A part with no hit points must not divide.
    CHECK(hud_figure::fill_class({.cur = 0, .max = 0}) == "hud-fig-f4");
}

TEST_CASE("figure_element_ids_are_unique_and_stable", "[hud_figure]") {
    // hud_anim keys on these. Two limbs sharing an id would make one limb's
    // damage flash play on the other, and a renamed id silently stops both the
    // feed and the forget — the second of which pins the game's framerate.
    std::set<std::string> ids;
    for (int k = 0; k < static_cast<int>(hud_figure::limb_count); ++k) {
        const auto id = std::string(hud_figure::element_id(static_cast<limb>(k)));
        CAPTURE(k, id);
        CHECK(id.starts_with("hud-soma-fig-"));
        ids.insert(id);
    }
    CHECK(ids.size() == hud_figure::limb_count);

    // Paired by position with the shape classes, and the two arms and two legs
    // deliberately SHARE a shape while keeping distinct ids.
    CHECK(hud_figure::shape_class(limb::arm_l) == hud_figure::shape_class(limb::arm_r));
    CHECK(hud_figure::shape_class(limb::leg_l) == hud_figure::shape_class(limb::leg_r));
    CHECK(hud_figure::element_id(limb::arm_l) != hud_figure::element_id(limb::arm_r));
}

TEST_CASE("figure_fits_beside_the_rows_at_the_narrowest_column", "[hud_figure]") {
    // This is the arithmetic that lets the expanded panel keep its height: the
    // figure sits BESIDE the six detail rows, so it costs max(rows, figure)
    // rather than their sum. If it stopped fitting, the honest fix is a smaller
    // figure — not a taller panel, because SOMA overflowing does not scroll
    // (the HUD is passive, so the wheel falls through to the map zoom) and
    // POOLS and EFFECTS would simply leave the screen.
    //
    // col_w 300 is the layout's floor; inner width removes the 1dp border pair
    // and the 6dp .hud-body padding pair.
    constexpr float narrowest_inner = 300.0f - 2.0f - 12.0f;
    const auto rows = hud_figure::rows_width(narrowest_inner);

    // .hud-cell-name and .hud-cell-val are 74dp each; what is left is the meter.
    constexpr float name_and_val = 74.0f * 2.0f;
    CHECK(rows > name_and_val);
    const auto meter = rows - name_and_val;
    // Twelve pips with a 1dp gap each, plus the meter's own 8dp side margins.
    CHECK(meter >= 12.0f * 5.0f + 16.0f);

    // Degenerate widths clamp instead of going negative — a negative length is
    // an RCSS parse error every frame, not a layout glitch.
    CHECK(hud_figure::rows_width(10.0f) == 0.0f);
    CHECK(hud_figure::rows_width(0.0f) == 0.0f);
}
