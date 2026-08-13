#include "catch/catch_amalgamated.hpp"

#include "newchar_bio_scan.h"
#include "newchar_bionic_gate.h"

// The BIONICS creator step shows three things about every implant that MUST agree: the checkbox
// glyph ([x] / [ ] / [-]), the Status field's reason line, and the popup CONFIRM raises when it
// refuses. They agree only because all three read one nc_bionic_gate::state, so the precedence
// inside evaluate() is the contract — and it is branchier than the trait gate's: taken x dependants
// x locked, and eight independent reasons an install can be refused.
//
// Two of these pin NEW behaviour rather than a port. `granted` did not exist as a concept, so a
// profession's own CBM fell through to the "prevents you from REMOVING this bionic" arm on a take
// attempt. And over_budget used to key off the cursor's column index (`iCurWorkingPage == 0` meant
// "counts against advantages"), which stops being true the moment the columns are grouped by
// anything else.

TEST_CASE( "nc_bionic_gate_installed_bionics_only_ask_about_removal", "[newchar][bionic_gate]" )
{
    // An installed bionic is already paid for, already fits and is already compatible, so
    // conflicts, space and the budget must not reach the answer. Feed it every hostile input at
    // once and it stays removable.
    nc_bionic_gate::inputs in;
    in.taken = true;
    in.trait_conflicts = true;
    in.scen_forbids = true;
    in.prof_forbids = true;
    in.no_space = true;
    in.missing_prereq = true;
    in.has_upgrade = true;
    in.points = 99;
    in.num_good = 99;
    in.max_points = 1;

    const nc_bionic_gate::state st = nc_bionic_gate::evaluate( in );
    CHECK( st.taken );
    CHECK( st.held() );
    CHECK( st.toggleable() );
    CHECK_FALSE( st.trait_conflicts );
    CHECK_FALSE( st.no_space );
    CHECK_FALSE( st.over_budget );
}

TEST_CASE( "nc_bionic_gate_installed_bionics_can_be_pinned_down", "[newchar][bionic_gate]" )
{
    nc_bionic_gate::inputs in;
    in.taken = true;

    SECTION( "another implant depends on it" ) {
        in.has_dependents = true;
        const nc_bionic_gate::state st = nc_bionic_gate::evaluate( in );
        CHECK( st.has_dependents );
        CHECK_FALSE( st.toggleable() );
    }
    SECTION( "the scenario forces it" ) {
        // This is the case whose popup existed but was unreachable: the old chain returned from the
        // taken branch before ever consulting the locks, so a scenario's forced bionic could be
        // dropped in the creator.
        in.scen_locked = true;
        const nc_bionic_gate::state st = nc_bionic_gate::evaluate( in );
        CHECK( st.locked );
        CHECK_FALSE( st.toggleable() );
    }
    SECTION( "the profession forces it" ) {
        in.prof_locked = true;
        CHECK_FALSE( nc_bionic_gate::evaluate( in ).toggleable() );
    }
}

TEST_CASE( "nc_bionic_gate_a_profession_fixture_is_held_and_untouchable",
           "[newchar][bionic_gate]" )
{
    // Profession CBMs are installed by add_profession_items AFTER the wizard, so `taken` is false
    // for them throughout creation while the row has always rendered them as held. Both halves
    // matter: held() drives the [x], and toggleable() must still refuse.
    nc_bionic_gate::inputs in;
    in.granted = true;

    const nc_bionic_gate::state st = nc_bionic_gate::evaluate( in );
    CHECK_FALSE( st.taken );
    CHECK( st.held() );
    CHECK_FALSE( st.toggleable() );
}

TEST_CASE( "nc_bionic_gate_every_refusal_closes_the_gate_on_its_own", "[newchar][bionic_gate]" )
{
    // Each of these is a separate popup in set_bionics, so each must independently close the gate —
    // a regression that dropped one would otherwise show only as a refusal with no explanation.
    nc_bionic_gate::inputs in;
    CHECK( nc_bionic_gate::evaluate( in ).toggleable() );

    SECTION( "the scenario forbids bionics wholesale" ) {
        in.scen_forbids_all = true;
        CHECK( nc_bionic_gate::evaluate( in ).forbidden );
        CHECK_FALSE( nc_bionic_gate::evaluate( in ).toggleable() );
    }
    SECTION( "the profession forbids bionics wholesale" ) {
        in.prof_forbids_all = true;
        CHECK( nc_bionic_gate::evaluate( in ).forbidden );
        CHECK_FALSE( nc_bionic_gate::evaluate( in ).toggleable() );
    }
    SECTION( "the scenario forbids this one" ) {
        in.scen_forbids = true;
        CHECK_FALSE( nc_bionic_gate::evaluate( in ).toggleable() );
    }
    SECTION( "the profession forbids this one" ) {
        in.prof_forbids = true;
        CHECK_FALSE( nc_bionic_gate::evaluate( in ).toggleable() );
    }
    SECTION( "a trait in the way" ) {
        in.trait_conflicts = true;
        CHECK_FALSE( nc_bionic_gate::evaluate( in ).toggleable() );
    }
    SECTION( "no room in the body" ) {
        in.no_space = true;
        CHECK_FALSE( nc_bionic_gate::evaluate( in ).toggleable() );
    }
    SECTION( "a missing prerequisite implant" ) {
        in.missing_prereq = true;
        CHECK_FALSE( nc_bionic_gate::evaluate( in ).toggleable() );
    }
    SECTION( "a lesser version already installed" ) {
        in.has_downgrade = true;
        CHECK_FALSE( nc_bionic_gate::evaluate( in ).toggleable() );
    }
    SECTION( "a better version already installed" ) {
        in.has_upgrade = true;
        CHECK_FALSE( nc_bionic_gate::evaluate( in ).toggleable() );
    }
}

TEST_CASE( "nc_bionic_gate_budget_is_keyed_off_the_point_sign", "[newchar][bionic_gate]" )
{
    using nc_bionic_gate::over_budget;
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

    SECTION( "a free implant can never breach either cap" ) {
        // The regression this guards: keyed off a column index, a zero-point bionic fell into the
        // disadvantage branch because it was "not an advantage".
        CHECK_FALSE( over_budget( 0, 99, -99, cap, false ) );
        CHECK_FALSE( over_budget( 0, 0, 0, 0, false ) );
    }

    SECTION( "freeform enforces nothing" ) {
        CHECK_FALSE( over_budget( 99, 99, 0, 0, true ) );
        CHECK_FALSE( over_budget( -99, 0, -99, 0, true ) );
    }
}

TEST_CASE( "nc_bionic_gate_over_budget_closes_the_gate", "[newchar][bionic_gate]" )
{
    nc_bionic_gate::inputs in;
    in.points = 5;
    in.num_good = 8;
    in.max_points = 12;
    const nc_bionic_gate::state st = nc_bionic_gate::evaluate( in );
    CHECK( st.over_budget );
    CHECK_FALSE( st.toggleable() );
}

// ── SCAN SWEEP ────────────────────────────────────────────────────────────────
//
// The chassis animation reads as a sweep only because the glow TRAILS the head: a symmetric falloff
// makes the direction of travel ambiguous, which is the whole content of the animation. These pin
// that, plus the loop's continuity — a jump at the wrap reads as a flicker, not a scan.

TEST_CASE( "nc_bio_scan_glow_trails_the_head", "[newchar][bio_scan]" )
{
    // Head parked on row 2: row 2 is full brightness, rows behind it fade, rows ahead are dark.
    CHECK( nc_bio_scan::intensity( 2.0F, 2 ) == Catch::Approx( 1.0F ) );
    CHECK( nc_bio_scan::intensity( 2.0F, 3 ) == 0.0F );
    CHECK( nc_bio_scan::intensity( 2.0F, 5 ) == 0.0F );
    CHECK( nc_bio_scan::intensity( 2.0F, 1 ) > 0.0F );
    CHECK( nc_bio_scan::intensity( 2.0F, 1 ) < 1.0F );
    // Past the tail, dark again — with tail 1.7 rows, row 0 is 2.0 behind.
    CHECK( nc_bio_scan::intensity( 2.0F, 0 ) == 0.0F );
}

TEST_CASE( "nc_bio_scan_decays_monotonically_behind_the_head", "[newchar][bio_scan]" )
{
    for( float head = 0.0F; head <= static_cast<float>( nc_bio_scan::rows ); head += 0.13F ) {
        float prev = 2.0F;
        // Walking BACKWARDS from the head, brightness must never rise.
        for( int row = static_cast<int>( head ); row >= 0; row-- ) {
            const float v = nc_bio_scan::intensity( head, row );
            CHECK( v <= prev + 1e-5F );
            CHECK( v >= 0.0F );
            CHECK( v <= 1.0F );
            prev = v;
        }
    }
}

TEST_CASE( "nc_bio_scan_head_sweeps_the_whole_body_and_loops", "[newchar][bio_scan]" )
{
    // Starts a full tail above the skull so the first pass fades in rather than popping on, and
    // ends a full tail below the LAST row so its trailing glow has faded before the head wraps.
    CHECK( nc_bio_scan::head_at( 0.0F ) == Catch::Approx( -nc_bio_scan::tail ) );
    const float last_frame = nc_bio_scan::head_at( nc_bio_scan::sweep_secs * 0.9999F );
    CHECK( last_frame > static_cast<float>( nc_bio_scan::rows ) - 0.01F );
    // Wrap continuity: one period later the head is back where it started.
    CHECK( nc_bio_scan::head_at( nc_bio_scan::sweep_secs + 0.4F ) ==
           Catch::Approx( nc_bio_scan::head_at( 0.4F ) ).margin( 1e-4 ) );
    // The OUTGOING side is the one that can flash, and head position alone does NOT prove it is
    // clear: with travel = rows + tail the head reaches the feet while the bottom row is still at
    // 0.41 brightness, then drops to nothing in one frame, every pass. Asserted on the ALPHA the
    // producer actually writes, so float dust in the last sampled frame does not read as a flash.
    for( int row = 0; row < nc_bio_scan::rows; row++ ) {
        CHECK( nc_bio_scan::alpha_of( nc_bio_scan::intensity( last_frame, row ) ) == 0 );
        CHECK( nc_bio_scan::intensity( nc_bio_scan::head_at( 0.0F ), row ) == 0.0F );
    }
}

TEST_CASE( "nc_bio_scan_alpha_covers_the_full_byte_range", "[newchar][bio_scan]" )
{
    CHECK( nc_bio_scan::alpha_of( 0.0F ) == 0 );
    CHECK( nc_bio_scan::alpha_of( 1.0F ) == 255 );
    // Clamped rather than trusted: a future easing curve overshooting would otherwise wrap the byte
    // and the brightest frame of the sweep would render transparent.
    CHECK( nc_bio_scan::alpha_of( 1.4F ) == 255 );
    CHECK( nc_bio_scan::alpha_of( -0.3F ) == 0 );
}
