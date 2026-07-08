#include "catch/catch.hpp"

#include <cstdint>

#include "sidebar_anim.h"
#include "ui_tween.h"

// Pure tween math + the sidebar animation state registry. No curses/GPU/SDL
// context: every check passes an explicit `now`, so this runs headless.

using ui_tween::ease_curve;
using ui_tween::tween;
using ui_tween::tween_loop;

TEST_CASE( "ease_curve endpoints are exact and clamped", "[ui_tween]" )
{
    for( int i = 0; i < static_cast<int>( ease_curve::num_curves ); ++i ) {
        const ease_curve c = static_cast<ease_curve>( i );
        CHECK( ui_tween::apply_ease( c, 0.0f ) == Approx( 0.0f ) );
        CHECK( ui_tween::apply_ease( c, 1.0f ) == Approx( 1.0f ) );
        // Out-of-range progress is clamped to the endpoints.
        CHECK( ui_tween::apply_ease( c, -0.5f ) == Approx( 0.0f ) );
        CHECK( ui_tween::apply_ease( c, 1.5f ) == Approx( 1.0f ) );
    }
}

TEST_CASE( "ease_curve known midpoints", "[ui_tween]" )
{
    CHECK( ui_tween::apply_ease( ease_curve::linear, 0.5f ) == Approx( 0.5f ) );
    CHECK( ui_tween::apply_ease( ease_curve::quad_in, 0.5f ) == Approx( 0.25f ) );
    CHECK( ui_tween::apply_ease( ease_curve::quad_out, 0.5f ) == Approx( 0.75f ) );
    // back_out overshoots above 1 before settling — that's the effect.
    CHECK( ui_tween::apply_ease( ease_curve::back_out, 0.5f ) > 1.0f );
}

TEST_CASE( "string_to_ease / string_to_loop parse with safe fallback", "[ui_tween]" )
{
    CHECK( ui_tween::string_to_ease( "back_out" ) == ease_curve::back_out );
    CHECK( ui_tween::string_to_ease( "sine_in_out" ) == ease_curve::sine_in_out );
    CHECK( ui_tween::string_to_ease( "nonsense" ) == ease_curve::linear );
    CHECK( ui_tween::string_to_loop( "pingpong" ) == tween_loop::pingpong );
    CHECK( ui_tween::string_to_loop( "loop" ) == tween_loop::loop );
    CHECK( ui_tween::string_to_loop( "nope" ) == tween_loop::once );
}

TEST_CASE( "tween once: holds before start and after end", "[ui_tween]" )
{
    const tween t{ 0.0f, 10.0f, 1000, 100, ease_curve::linear, tween_loop::once, 0 };
    CHECK( t.value_at( 500 ) == Approx( 0.0f ) );    // before start -> from
    CHECK( t.value_at( 1000 ) == Approx( 0.0f ) );   // at start
    CHECK( t.value_at( 1050 ) == Approx( 5.0f ) );   // mid
    CHECK( t.value_at( 1100 ) == Approx( 10.0f ) );  // end
    CHECK( t.value_at( 5000 ) == Approx( 10.0f ) );  // long after -> hold at to
    CHECK_FALSE( t.settled( 1050 ) );
    CHECK( t.settled( 1100 ) );
}

TEST_CASE( "tween loop: sawtooth, infinite never settles", "[ui_tween]" )
{
    const tween t{ 0.0f, 1.0f, 0, 100, ease_curve::linear, tween_loop::loop, 0 };
    CHECK( t.value_at( 50 ) == Approx( 0.5f ) );
    CHECK( t.value_at( 100 ) == Approx( 0.0f ) );  // wraps to start of next leg
    CHECK( t.value_at( 150 ) == Approx( 0.5f ) );
    CHECK_FALSE( t.settled( 100000 ) );
}

TEST_CASE( "tween pingpong: triangle wave", "[ui_tween]" )
{
    const tween t{ 0.0f, 1.0f, 0, 100, ease_curve::linear, tween_loop::pingpong, 0 };
    CHECK( t.value_at( 50 ) == Approx( 0.5f ) );    // rising
    CHECK( t.value_at( 100 ) == Approx( 1.0f ) );   // peak
    CHECK( t.value_at( 150 ) == Approx( 0.5f ) );   // falling
    CHECK( t.value_at( 200 ) == Approx( 0.0f ) );   // trough
}

TEST_CASE( "tween finite repeats settle at the right endpoint", "[ui_tween]" )
{
    // loop, 2 legs -> rests at `to`.
    const tween lp{ 0.0f, 1.0f, 0, 100, ease_curve::linear, tween_loop::loop, 2 };
    CHECK( lp.value_at( 250 ) == Approx( 1.0f ) );
    CHECK( lp.settled( 250 ) );
    // pingpong, 2 legs (up then down) -> rests at `from`.
    const tween pp{ 0.0f, 1.0f, 0, 100, ease_curve::linear, tween_loop::pingpong, 2 };
    CHECK( pp.value_at( 250 ) == Approx( 0.0f ) );
    CHECK( pp.settled( 250 ) );
}

// Helpers to build deterministic specs without touching shipped icons.json.
static sidebar_anim::anim_spec pop_spec()
{
    sidebar_anim::anim_spec s;
    s.trigger = sidebar_anim::anim_trigger::on_change;
    s.prop = sidebar_anim::anim_prop::scale;
    s.from = 1.3f;
    s.to = 1.0f;
    s.duration_ms = 300;
    s.ease = ease_curve::back_out;
    s.loop = tween_loop::once;
    return s;
}

static sidebar_anim::anim_spec crit_blink_spec()
{
    sidebar_anim::anim_spec s;
    s.trigger = sidebar_anim::anim_trigger::critical;
    s.prop = sidebar_anim::anim_prop::alpha;
    s.from = 1.0f;
    s.to = 0.3f;
    s.duration_ms = 260;
    s.ease = ease_curve::sine_in_out;
    s.loop = tween_loop::pingpong;
    return s;
}

TEST_CASE( "registry primes without animating on first sight", "[ui_tween][sidebar_anim]" )
{
    sidebar_anim::registry r;
    r.bind_specs( { { "heart", { pop_spec() } } } );
    r.update( "val_pain", "heart", 5.0, false, 1000 );
    const sidebar_anim::icon_transform tr = r.sample( "val_pain", 1000 );
    CHECK( tr.scale == Approx( 1.0f ) );  // no pop on first sample
    CHECK_FALSE( r.any_active( 1000 ) );
}

TEST_CASE( "registry pops on value change, then settles to identity", "[ui_tween][sidebar_anim]" )
{
    sidebar_anim::registry r;
    r.bind_specs( { { "heart", { pop_spec() } } } );
    r.update( "val_pain", "heart", 5.0, false, 1000 );   // prime
    r.update( "val_pain", "heart", 40.0, false, 1100 );  // change -> pop starts
    CHECK( r.any_active( 1100 ) );
    CHECK( r.sample( "val_pain", 1100 ).scale > 1.0f ); // scaled up at pop start
    // Well past the pop duration: a fresh update prunes the settled tween.
    r.update( "val_pain", "heart", 40.0, false, 5000 );
    CHECK_FALSE( r.any_active( 5000 ) );
    CHECK( r.sample( "val_pain", 5000 ).scale == Approx( 1.0f ) );
}

TEST_CASE( "registry without specs never animates (opt-in)", "[ui_tween][sidebar_anim]" )
{
    sidebar_anim::registry r;            // no bind_specs
    r.update( "val_speed", "gauge", 5.0, false, 1000 );
    r.update( "val_speed", "gauge", 99.0, false, 1100 ); // change, but no spec
    CHECK_FALSE( r.any_active( 1100 ) );
    CHECK( r.sample( "val_speed", 1100 ).scale == Approx( 1.0f ) );
}

TEST_CASE( "registry directional scale_y selects pivot by change sign", "[ui_tween][sidebar_anim]" )
{
    sidebar_anim::anim_spec up;
    up.trigger = sidebar_anim::anim_trigger::on_increase;
    up.prop = sidebar_anim::anim_prop::scale_y;
    up.from = 0.6f;
    up.to = 1.0f;
    up.duration_ms = 300;
    up.ease = ease_curve::back_out;
    up.pivot_y = 0.0f;            // anchored top
    sidebar_anim::anim_spec dn = up;
    dn.trigger = sidebar_anim::anim_trigger::on_decrease;
    dn.pivot_y = 1.0f;            // anchored bottom

    sidebar_anim::registry r;
    r.bind_specs( { { "heart", { up, dn } } } );
    r.update( "x", "heart", 10.0, false, 0 );     // prime
    r.update( "x", "heart", 20.0, false, 100 );   // increase -> top pivot
    CHECK( r.sample( "x", 100 ).pivot_y == Approx( 0.0f ) );
    CHECK( r.sample( "x", 100 ).scale_y < 1.0f );  // mid-squash
    r.update( "x", "heart", 5.0, false, 500 );    // decrease -> bottom pivot
    CHECK( r.sample( "x", 500 ).pivot_y == Approx( 1.0f ) );
}

TEST_CASE( "shipped icons.json parses through load_specs (strict JSON)",
           "[ui_tween][sidebar_anim]" )
{
    // Reads the real gfx/widgets/icons.json. If a parser left a field unvisited
    // (the strict-JSON report_unvisited bug), read_from_file_json swallows the
    // error and no specs bind — so a non-empty result proves the file parsed
    // cleanly through both the icon registry's and the animation system's view.
    sidebar_anim::registry r;
    r.load_specs();
    // heart ships with an on_change scale pop; a change must produce a tween.
    r.update( "val_pain", "heart", 5.0, false, 1000 );
    r.update( "val_pain", "heart", 40.0, false, 1100 );
    CHECK( r.any_active( 1100 ) );
}

TEST_CASE( "registry critical band blinks then eases back on exit", "[ui_tween][sidebar_anim]" )
{
    sidebar_anim::registry r;
    r.bind_specs( { { "heart", { crit_blink_spec() } } } );
    r.update( "hp", "heart", 100.0, false, 0 );      // prime, not critical
    r.update( "hp", "heart", 100.0, true, 100 );     // enter critical -> infinite blink
    CHECK( r.any_active( 100000 ) );        // pingpong never settles while critical
    r.update( "hp", "heart", 100.0, false, 200 );    // exit critical -> one-shot ease back
    CHECK( r.any_active( 200 ) );            // ease-out still in flight
    {
        const float a = r.sample( "hp", 200 ).alpha;
        CHECK( a > 0.0f );
        CHECK( a <= 1.0f );
    }
    // After the ease-out duration, alpha rests opaque and nothing is active.
    r.update( "hp", "heart", 100.0, false, 1000 );
    CHECK( r.sample( "hp", 1000 ).alpha == Approx( 1.0f ) );
    CHECK_FALSE( r.any_active( 1000 ) );
}
