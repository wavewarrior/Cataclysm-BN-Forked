#include "catch/catch_amalgamated.hpp"
#include "newchar_balance.h"

#include <cmath>
#include <numbers>

// The TRAITS/BIONICS balance scale draws two things that must agree but are not
// structurally coupled: the beam rotates, the pans are nudged vertically. The pans
// cannot be children of the beam (they would inherit its rotation and render tilted),
// so only nc_scale::compute keeps them consistent.
//
// These tests defend that agreement. The bug they exist to catch already shipped once:
// tilt and pan travel were separate hand-picked constants with opposite signs, so beam
// and pans moved equal distances the wrong ways. The states that expose it — level,
// inverted, and full tilt — are the ones that are awkward to reach by hand in-game,
// which is exactly why they belong here rather than in a screenshot.

TEST_CASE("nc_scale_level_when_evenly_matched", "[newchar][balance]") {
    // bad arrives as a negative running total, so -6 is a weight of 6.
    const nc_scale::geometry g = nc_scale::compute(6, -6, 12);
    CHECK(g.bal == Catch::Approx(0.0F));
    CHECK(g.tilt_deg == Catch::Approx(0.0F));
    CHECK(g.good_dp == Catch::Approx(0.0F));
    CHECK(g.bad_dp == Catch::Approx(0.0F));
}

TEST_CASE("nc_scale_heavier_side_sinks", "[newchar][balance]") {
    SECTION("advantages outweigh: good pan sinks, bad pan rises") {
        const nc_scale::geometry g = nc_scale::compute(8, -4, 12);
        CHECK(g.bal > 0.0F);
        CHECK(g.good_dp > 0.0F); // positive `top` sinks
        CHECK(g.bad_dp < 0.0F);
    }
    SECTION("disadvantages outweigh: the pans swap") {
        const nc_scale::geometry g = nc_scale::compute(2, -9, 12);
        CHECK(g.bal < 0.0F);
        CHECK(g.good_dp < 0.0F);
        CHECK(g.bad_dp > 0.0F);
    }
}

TEST_CASE("nc_scale_beam_sinks_the_same_side_as_the_pan", "[newchar][balance]") {
    // The regression that shipped. RCSS rotate() is clockwise and the good pan is on
    // the LEFT, so sinking the good pan (good_dp > 0) requires a NEGATIVE tilt. Equal
    // magnitudes with the wrong sign move beam and pans in opposite directions, which
    // is the maximally broken case rather than a subtle one.
    for (const int good : {0, 1, 5, 7, 12, 40}) {
        for (const int bad : {0, -1, -5, -7, -12, -40}) {
            const nc_scale::geometry g = nc_scale::compute(good, bad, 12);
            CAPTURE(good, bad, g.bal, g.tilt_deg, g.good_dp);
            // Opposite signs, or both exactly zero.
            CHECK(g.tilt_deg * g.good_dp <= 0.0F);
            if (g.good_dp != 0.0F) { CHECK(g.tilt_deg != 0.0F); }
            // Pans are always exact mirrors.
            CHECK(g.good_dp == Catch::Approx(-g.bad_dp));
        }
    }
}

TEST_CASE("nc_scale_pan_travel_matches_the_beam_rise", "[newchar][balance]") {
    // A pan must sit where the beam end actually is: |pan| == half_length * sin(tilt).
    // Drift here is what makes the beam look detached from its own pans.
    for (const int good : {0, 3, 6, 9, 12}) {
        const nc_scale::geometry g = nc_scale::compute(good, 0, 12);
        const float rise =
            nc_scale::beam_half_dp
            * std::sin(std::abs(g.tilt_deg) * std::numbers::pi_v<float> / 180.0F);
        CAPTURE(good, g.tilt_deg, g.good_dp, rise);
        CHECK(std::abs(g.good_dp) == Catch::Approx(rise).margin(0.01F));
    }
}

TEST_CASE("nc_scale_clamps_beyond_the_budget", "[newchar][balance]") {
    // Overspending must saturate the scale, not tip it past vertical. `.nc-scale-arm`
    // is sized to contain exactly max_pan_dp() of travel, so an unclamped value would
    // push a pan out of its row.
    const nc_scale::geometry over = nc_scale::compute(500, 0, 12);
    CHECK(over.bal == Catch::Approx(1.0F));
    CHECK(over.tilt_deg == Catch::Approx(-nc_scale::max_tilt_deg));
    CHECK(over.good_dp == Catch::Approx(nc_scale::max_pan_dp()));

    const nc_scale::geometry under = nc_scale::compute(0, -500, 12);
    CHECK(under.bal == Catch::Approx(-1.0F));
    CHECK(under.tilt_deg == Catch::Approx(nc_scale::max_tilt_deg));
    CHECK(under.good_dp == Catch::Approx(-nc_scale::max_pan_dp()));

    // Travel is bounded for every input, which is what lets the RCSS reserve a fixed
    // row height instead of growing to fit.
    for (const int good : {-99, 0, 6, 99, 100000}) {
        const nc_scale::geometry g = nc_scale::compute(good, -50, 12);
        CAPTURE(good, g.good_dp);
        CHECK(std::abs(g.good_dp) <= nc_scale::max_pan_dp() + 0.01F);
    }
}

TEST_CASE("nc_scale_zero_budget_does_not_divide_by_zero", "[newchar][balance]") {
    // maxp can be 0 (a scenario granting no trait points); cap must floor at 1.
    const nc_scale::geometry g = nc_scale::compute(0, 0, 0);
    CHECK(std::isfinite(g.bal));
    CHECK(g.bal == Catch::Approx(0.0F));

    const nc_scale::geometry spent = nc_scale::compute(3, 0, 0);
    CHECK(std::isfinite(spent.bal));
    CHECK(spent.bal == Catch::Approx(1.0F)); // 3/1, clamped
    CHECK(spent.good_dp == Catch::Approx(nc_scale::max_pan_dp()));
}
