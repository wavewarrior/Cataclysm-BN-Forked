#include "catch/catch_amalgamated.hpp"
#include "newchar_stat_meter.h"

// The STATS creator tab draws a pip meter and charges points, and the two must agree: a pip
// coloured "two points each" that actually cost one is a screen lying about the budget it just
// spent. Both derive from the same three functions here, and all three turn on the same
// boundary.
//
// HIGH_STAT is the value AT which the NEXT increment starts costing two. So 14 -> 15 costs two
// while 13 -> 14 costs one, which puts the last one-point pip at 14 and the first two-point pip
// at 15. That is one apart from every other reasonable reading of the constant, and getting it
// wrong shifts the red band by a pip without anything on screen looking broken — which is why
// these are tests and not a screenshot.

// HIGH_STAT and MAX_STAT are not exported by a header the tests can reach (one is a file-local
// enum in newcharacter_ui.cpp, the other lives in game_constants.h), so the real values are
// restated here. A change to either must reach this file.
constexpr int high = 14;
constexpr int cap = 20;

TEST_CASE("nc_stat_meter_pip_tiers_at_the_boundaries", "[newchar][stat_meter]") {
    using nc_stat_meter::pip_tier;
    using nc_stat_meter::tier;

    SECTION("the granted floor ends exactly at floor_val") {
        CHECK(pip_tier(1, high) == tier::base);
        CHECK(pip_tier(nc_stat_meter::floor_val, high) == tier::base);
        CHECK(pip_tier(nc_stat_meter::floor_val + 1, high) == tier::cheap);
    }

    SECTION("the one-point band includes HIGH_STAT itself") {
        CHECK(pip_tier(high - 1, high) == tier::cheap);
        CHECK(pip_tier(high, high) == tier::cheap);
        CHECK(pip_tier(high + 1, high) == tier::steep);
    }

    SECTION("everything above stays steep to the cap") {
        CHECK(pip_tier(cap, high) == tier::steep);
    }
}

TEST_CASE("nc_stat_meter_next_cost_doubles_at_high_stat", "[newchar][stat_meter]") {
    using nc_stat_meter::next_cost;

    // A stat sitting BELOW the threshold pays one for its next point...
    CHECK(next_cost(high - 1, high, cap) == 1);
    // ...and a stat sitting AT it pays two, because that increment is the one that crosses.
    CHECK(next_cost(high, high, cap) == 2);
    CHECK(next_cost(high + 1, high, cap) == 2);
}

TEST_CASE("nc_stat_meter_next_cost_is_zero_at_the_cap", "[newchar][stat_meter]") {
    // Zero is the signal the card uses to dim its `+`, so a capped stat must not report a
    // price it cannot charge.
    CHECK(nc_stat_meter::next_cost(cap, high, cap) == 0);
    CHECK(nc_stat_meter::next_cost(cap - 1, high, cap) == 2);
}

TEST_CASE("nc_stat_meter_refund_mirrors_what_was_paid", "[newchar][stat_meter]") {
    using nc_stat_meter::next_cost;
    using nc_stat_meter::refund;

    // Selling a point back must return exactly what buying it cost, or the points total drifts
    // every time the player scrubs a stat up and down.
    for (int v = nc_stat_meter::floor_val; v < cap; v++) {
        CHECK(refund(v + 1, high) == next_cost(v, high, cap));
    }
}

TEST_CASE("nc_stat_meter_refund_is_zero_at_the_floor", "[newchar][stat_meter]") {
    // The floor is granted, so it cannot be sold. Zero is what dims the card's `-`.
    CHECK(nc_stat_meter::refund(nc_stat_meter::floor_val, high) == 0);
    CHECK(nc_stat_meter::refund(nc_stat_meter::floor_val + 1, high) == 1);
}
