#include "catch/catch_amalgamated.hpp"

#include "lighting/hud_particle_effect.h"

// Pure simulation math behind the ambient HUD particle layer. Both functions
// encode a bug that shipped: particles that died in a band hugging their spawn
// edge, and an alpha that collapsed geometrically because the envelope was
// applied as a per-frame multiply instead of being derived from age.

static constexpr float SCREEN_H = 1080.f;
static constexpr float SCREEN_W = 1920.f;

TEST_CASE( "hud_particle_lifetime_carries_across_the_screen", "[hud_particle]" )
{
    using lighting::hud_particle_travel_lifetime;

    // Slowest and fastest authored snow/pollen/dust speeds, as the spawner
    // derives them: a fraction of screen height per second.
    const float slowest = SCREEN_H * 0.035f; // pollen, lower bound
    const float fastest = SCREEN_H * 0.20f;  // snow, upper bound

    for( const float speed : {
             slowest, fastest
         } ) {
        const float vertical = hud_particle_travel_lifetime( SCREEN_H + 40.f, speed );
        // The whole point: distance covered must clear the far edge, or the
        // particle dies mid-screen.
        CHECK( vertical * speed >= SCREEN_H + 40.f );
    }

    // Horizontal drift (dust) has to cross the WIDER axis.
    const float dust_speed = SCREEN_H * 0.05f;
    const float horizontal = hud_particle_travel_lifetime( SCREEN_W + 40.f, dust_speed );
    CHECK( horizontal * dust_speed >= SCREEN_W + 40.f );
}

TEST_CASE( "hud_particle_lifetime_scales_inversely_with_speed", "[hud_particle]" )
{
    using lighting::hud_particle_travel_lifetime;

    // Doubling the speed (the dev-panel speed knob) must halve the lifetime, so
    // the travelled distance — and therefore the on-screen coverage — is unchanged.
    const float base = hud_particle_travel_lifetime( 1000.f, 100.f );
    const float fast = hud_particle_travel_lifetime( 1000.f, 200.f );
    CHECK( fast == Catch::Approx( base * 0.5f ) );
    CHECK( base * 100.f == Catch::Approx( fast * 200.f ) );

    // A zero or negative speed must not divide by zero.
    CHECK( hud_particle_travel_lifetime( 1000.f, 0.f ) > 0.f );
    CHECK( hud_particle_travel_lifetime( 1000.f, -5.f ) > 0.f );
}

TEST_CASE( "hud_particle_alpha_envelope_shape", "[hud_particle]" )
{
    using lighting::hud_particle_alpha;

    constexpr float base = 0.8f;
    constexpr float life = 10.f;
    constexpr float fade_in = 0.4f;

    // Fades in from nothing, reaches full base alpha once the ramp completes.
    CHECK( hud_particle_alpha( base, 0.f, life, fade_in ) == Catch::Approx( 0.f ) );
    CHECK( hud_particle_alpha( base, 0.2f, life, fade_in ) == Catch::Approx( base * 0.5f ) );
    CHECK( hud_particle_alpha( base, fade_in, life, fade_in ) == Catch::Approx( base ) );

    // Holds at full alpha until the last 30% of life.
    CHECK( hud_particle_alpha( base, 5.f, life, fade_in ) == Catch::Approx( base ) );
    CHECK( hud_particle_alpha( base, 7.f, life, fade_in ) == Catch::Approx( base ) );

    // Then ramps to zero at the end, and stays there past it.
    CHECK( hud_particle_alpha( base, 8.5f, life, fade_in ) == Catch::Approx( base * 0.5f ) );
    CHECK( hud_particle_alpha( base, life, life, fade_in ) == Catch::Approx( 0.f ) );
    CHECK( hud_particle_alpha( base, life * 2.f, life, fade_in ) == Catch::Approx( 0.f ) );

    // Degenerate lifetime must not divide by zero.
    CHECK( hud_particle_alpha( base, 1.f, 0.f, fade_in ) == Catch::Approx( 0.f ) );
}

TEST_CASE( "hud_particle_alpha_does_not_compound_across_steps", "[hud_particle]" )
{
    using lighting::hud_particle_alpha;

    constexpr float base = 0.8f;
    constexpr float life = 10.f;
    constexpr float fade_in = 0.4f;

    // THE regression. The envelope must depend only on age, never on how many
    // steps were taken to get there: the old code multiplied the running alpha
    // by (1 - t) every frame, so a particle stepped at 60 Hz went dark ~5 frames
    // into a 3-second fade-out. Simulating the fade one small step at a time
    // must land on the same value as asking for that age directly.
    float age = 0.f;
    float stepped = 0.f;
    while( age < 9.f ) {
        age += 1.f / 60.f;
        stepped = hud_particle_alpha( base, age, life, fade_in );
    }
    CHECK( stepped == Catch::Approx( hud_particle_alpha( base, age, life, fade_in ) ) );
    // 90% through a 10 s life, two thirds of the 3 s fade-out have elapsed, so
    // a third of the base alpha remains (measured: 0.267 of base 0.8).
    CHECK( stepped == Catch::Approx( base * ( 1.f - ( age - 7.f ) / 3.f ) ).margin( 0.02f ) );
    // The old compounding fade reached ~0 within a handful of frames. Anything
    // in that ballpark means the multiply is back; a third of base is ~0.27.
    CHECK( stepped > 0.2f );
}

TEST_CASE( "hud_particle_alpha_is_monotonic_over_the_fade_out", "[hud_particle]" )
{
    using lighting::hud_particle_alpha;

    float prev = hud_particle_alpha( 1.f, 7.f, 10.f, 0.4f );
    for( int i = 1; i <= 30; ++i ) {
        const float age = 7.f + static_cast<float>( i ) * 0.1f;
        const float now = hud_particle_alpha( 1.f, age, 10.f, 0.4f );
        CHECK( now <= prev );
        prev = now;
    }
    CHECK( prev == Catch::Approx( 0.f ).margin( 0.001f ) );
}
