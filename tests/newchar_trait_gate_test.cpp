#include "catch/catch_amalgamated.hpp"

#include "newchar_dna.h"
#include "newchar_trait_gate.h"

// The TRAITS creator step shows three things about every trait that MUST agree: the checkbox glyph
// ([x] / [ ] / [-]), the Status field's reason line, and the popup CONFIRM raises when it refuses.
// They agree only because all three read one nc_trait_gate::state, so the precedence inside
// evaluate() is the contract, and it is branchy enough to be worth pinning: taken x locked x
// mandatory, conflicts x swaps x has-a-holder, and a budget check at the cap on both signs.
//
// Two of these are NEW behaviour rather than a port. over_budget used to key off the cursor's
// column index (`iCurWorkingPage == 0` meant "counts against advantages"), which stopped being true
// once traits were grouped by anything else. And can_swap did not exist as a concept: a conflicting
// trait whose type swaps is a REPLACEMENT, not a refusal.

TEST_CASE( "nc_trait_gate_held_traits_only_ask_about_release", "[newchar][trait_gate]" )
{
    // A held trait is already paid for and already compatible, so conflicts and the budget must not
    // reach the answer. Feed it every hostile input at once and it stays droppable.
    nc_trait_gate::inputs in;
    in.taken = true;
    in.conflicts = true;
    in.scen_forbids = true;
    in.prof_forbids = true;
    in.bionic_blocks = true;
    in.points = 99;
    in.num_good = 99;
    in.max_points = 1;

    const nc_trait_gate::state st = nc_trait_gate::evaluate( in );
    CHECK( st.taken );
    CHECK( st.toggleable() );
    CHECK_FALSE( st.conflicts );
    CHECK_FALSE( st.over_budget );
    CHECK_FALSE( st.scen_forbids );
}

TEST_CASE( "nc_trait_gate_held_traits_can_be_pinned_down", "[newchar][trait_gate]" )
{
    nc_trait_gate::inputs in;
    in.taken = true;

    SECTION( "a lock from a profession or scenario blocks dropping" ) {
        in.locked = true;
        CHECK_FALSE( nc_trait_gate::evaluate( in ).toggleable() );
    }
    SECTION( "so does being the last of a mandatory_one type" ) {
        in.mandatory = true;
        CHECK_FALSE( nc_trait_gate::evaluate( in ).toggleable() );
    }
}

TEST_CASE( "nc_trait_gate_a_swap_needs_something_to_give_up", "[newchar][trait_gate]" )
{
    // This is the distinction the old code did not draw. `swaps` alone is not permission: with no
    // held trait of that type there is nothing to replace, so the conflict is a plain refusal.
    nc_trait_gate::inputs in;
    in.conflicts = true;
    in.swaps = true;

    SECTION( "no holder: refused" ) {
        in.has_swap_holder = false;
        const nc_trait_gate::state st = nc_trait_gate::evaluate( in );
        CHECK( st.conflicts );
        CHECK_FALSE( st.can_swap );
        CHECK_FALSE( st.toggleable() );
    }
    SECTION( "a holder exists: allowed, as a replacement" ) {
        in.has_swap_holder = true;
        const nc_trait_gate::state st = nc_trait_gate::evaluate( in );
        CHECK( st.conflicts );
        CHECK( st.can_swap );
        CHECK( st.toggleable() );
    }
    SECTION( "a non-swap type is refused however many holders there are" ) {
        in.swaps = false;
        in.has_swap_holder = true;
        CHECK_FALSE( nc_trait_gate::evaluate( in ).can_swap );
        CHECK_FALSE( nc_trait_gate::evaluate( in ).toggleable() );
    }
}

TEST_CASE( "nc_trait_gate_each_forbid_refuses_on_its_own", "[newchar][trait_gate]" )
{
    // Each of these is a separate popup in set_traits, so each must independently close the gate —
    // a regression that dropped one would otherwise only show as a refusal with no explanation.
    nc_trait_gate::inputs in;
    CHECK( nc_trait_gate::evaluate( in ).toggleable() );

    SECTION( "scenario" ) {
        in.scen_forbids = true;
        CHECK_FALSE( nc_trait_gate::evaluate( in ).toggleable() );
    }
    SECTION( "profession" ) {
        in.prof_forbids = true;
        CHECK_FALSE( nc_trait_gate::evaluate( in ).toggleable() );
    }
    SECTION( "a starting bionic" ) {
        in.bionic_blocks = true;
        CHECK_FALSE( nc_trait_gate::evaluate( in ).toggleable() );
    }
}

TEST_CASE( "nc_trait_gate_budget_is_keyed_off_the_point_sign", "[newchar][trait_gate]" )
{
    using nc_trait_gate::over_budget;
    constexpr int cap = 12;

    SECTION( "an advantage is measured against the advantage total only" ) {
        // At the cap exactly: allowed. One past it: refused. num_bad is irrelevant either way.
        CHECK_FALSE( over_budget( 4, 8, -99, cap, false ) );
        CHECK( over_budget( 5, 8, 0, cap, false ) );
    }

    SECTION( "a disadvantage is measured against the disadvantage total only" ) {
        // num_bad is kept NEGATIVE, and the cap is -cap.
        CHECK_FALSE( over_budget( -4, 99, -8, cap, false ) );
        CHECK( over_budget( -5, 0, -8, cap, false ) );
    }

    SECTION( "a free trait can never breach either cap" ) {
        // The regression this guards: keyed off a column index, a 0-point trait fell into the
        // disadvantage branch because it was "not an advantage".
        CHECK_FALSE( over_budget( 0, 99, -99, cap, false ) );
        CHECK_FALSE( over_budget( 0, 0, 0, 0, false ) );
    }

    SECTION( "freeform enforces nothing" ) {
        CHECK_FALSE( over_budget( 99, 99, 0, 0, true ) );
        CHECK_FALSE( over_budget( -99, 0, -99, 0, true ) );
    }
}

TEST_CASE( "nc_trait_gate_over_budget_closes_the_gate", "[newchar][trait_gate]" )
{
    nc_trait_gate::inputs in;
    in.points = 5;
    in.num_good = 8;
    in.max_points = 12;
    const nc_trait_gate::state st = nc_trait_gate::evaluate( in );
    CHECK( st.over_budget );
    CHECK_FALSE( st.toggleable() );
}

// ── DNA STRAND ────────────────────────────────────────────────────────────────
//
// The strand only reads as a helix because its two backbones stay exactly half a turn apart. They
// are derived from ONE sine for that reason; these pin the property so a later "tidy-up" into two
// independent sines with a hand-added pi cannot pass.

TEST_CASE( "nc_dna_backbones_stay_antiphase", "[newchar][dna]" )
{
    for( int i = 0; i < nc_dna::rungs; i++ ) {
        for( float phase = 0.0F; phase < 6.5F; phase += 0.37F ) {
            const nc_dna::rung r = nc_dna::at( i, phase );
            // Antiphase about the mid-line: the pair is always symmetric around 0.5.
            CHECK( r.a + r.b == Catch::Approx( 1.0F ).margin( 1e-5 ) );
            CHECK( r.a >= -1e-5F );
            CHECK( r.a <= 1.0F + 1e-5F );
            CHECK( r.b >= -1e-5F );
            CHECK( r.b <= 1.0F + 1e-5F );
        }
    }
}

TEST_CASE( "nc_dna_span_never_asks_for_a_negative_width", "[newchar][dna]" )
{
    // The markup lays the pair out in flow as [gap][dot][bond][dot], so a negative bond width would
    // be a parse error every frame — and the two backbones cross twice per turn.
    for( int i = 0; i < nc_dna::rungs; i++ ) {
        for( float phase = 0.0F; phase < 6.5F; phase += 0.11F ) {
            const nc_dna::span sp = nc_dna::span_of( nc_dna::at( i, phase ) );
            CHECK( sp.width >= 0.0F );
            CHECK( sp.left >= -1e-5F );
            CHECK( sp.left + sp.width <= 1.0F + 1e-5F );
        }
    }
}

TEST_CASE( "nc_dna_span_reports_the_left_dot_s_own_depth", "[newchar][dna]" )
{
    // span_of swaps the two backbones when B is the left one, so it must swap the depth flag with
    // them; getting this wrong lights the wrong dot and the strand appears to spin inside out.
    for( float phase = 0.0F; phase < 6.5F; phase += 0.05F ) {
        const nc_dna::rung r = nc_dna::at( 0, phase );
        const nc_dna::span sp = nc_dna::span_of( r );
        const bool a_is_left = r.a <= r.b;
        CHECK( sp.left_front == ( a_is_left ? r.a_front : !r.a_front ) );
    }
}

TEST_CASE( "nc_dna_phase_advances_with_time", "[newchar][dna]" )
{
    // One full turn per 1/spin_turns_per_sec seconds, so the strand cannot silently stop.
    const float one_turn = 1.0F / nc_dna::spin_turns_per_sec;
    CHECK( nc_dna::phase_at( 0.0F ) == Catch::Approx( 0.0F ) );
    CHECK( nc_dna::phase_at( one_turn ) ==
           Catch::Approx( 2.0F * std::numbers::pi_v<float> ).margin( 1e-4 ) );
    CHECK( nc_dna::phase_at( 1.0F ) > 0.0F );
}
