#include "catch/catch_amalgamated.hpp"
#include "lighting/hud_particle_effect.h"

#include <algorithm>
#include <cmath>

// Pure simulation math behind the ambient HUD particle layer. Both functions
// encode a bug that shipped: particles that died in a band hugging their spawn
// edge, and an alpha that collapsed geometrically because the envelope was
// applied as a per-frame multiply instead of being derived from age.

static constexpr float SCREEN_H = 1080.f;
static constexpr float SCREEN_W = 1920.f;

TEST_CASE("hud_particle_lifetime_carries_across_the_screen", "[hud_particle]") {
    using lighting::hud_particle_travel_lifetime;

    // Slowest and fastest authored snow/pollen/dust speeds, as the spawner
    // derives them: a fraction of screen height per second.
    const float slowest = SCREEN_H * 0.035f; // pollen, lower bound
    const float fastest = SCREEN_H * 0.20f;  // snow, upper bound

    for (const float speed : {slowest, fastest}) {
        const float vertical = hud_particle_travel_lifetime(SCREEN_H + 40.f, speed);
        // The whole point: distance covered must clear the far edge, or the
        // particle dies mid-screen.
        CHECK(vertical * speed >= SCREEN_H + 40.f);
    }

    // Horizontal drift (dust) has to cross the WIDER axis.
    const float dust_speed = SCREEN_H * 0.05f;
    const float horizontal = hud_particle_travel_lifetime(SCREEN_W + 40.f, dust_speed);
    CHECK(horizontal * dust_speed >= SCREEN_W + 40.f);
}

TEST_CASE("hud_particle_lifetime_scales_inversely_with_speed", "[hud_particle]") {
    using lighting::hud_particle_travel_lifetime;

    // Doubling the speed (the dev-panel speed knob) must halve the lifetime, so
    // the travelled distance — and therefore the on-screen coverage — is unchanged.
    const float base = hud_particle_travel_lifetime(1000.f, 100.f);
    const float fast = hud_particle_travel_lifetime(1000.f, 200.f);
    CHECK(fast == Catch::Approx(base * 0.5f));
    CHECK(base * 100.f == Catch::Approx(fast * 200.f));

    // A zero or negative speed must not divide by zero.
    CHECK(hud_particle_travel_lifetime(1000.f, 0.f) > 0.f);
    CHECK(hud_particle_travel_lifetime(1000.f, -5.f) > 0.f);
}

TEST_CASE("hud_particle_alpha_envelope_shape", "[hud_particle]") {
    using lighting::hud_particle_alpha;

    constexpr float base = 0.8f;
    constexpr float life = 10.f;
    constexpr float fade_in = 0.4f;

    // Fades in from nothing, reaches full base alpha once the ramp completes.
    CHECK(hud_particle_alpha(base, 0.f, life, fade_in) == Catch::Approx(0.f));
    CHECK(hud_particle_alpha(base, 0.2f, life, fade_in) == Catch::Approx(base * 0.5f));
    CHECK(hud_particle_alpha(base, fade_in, life, fade_in) == Catch::Approx(base));

    // Holds at full alpha until the last 30% of life.
    CHECK(hud_particle_alpha(base, 5.f, life, fade_in) == Catch::Approx(base));
    CHECK(hud_particle_alpha(base, 7.f, life, fade_in) == Catch::Approx(base));

    // Then ramps to zero at the end, and stays there past it.
    CHECK(hud_particle_alpha(base, 8.5f, life, fade_in) == Catch::Approx(base * 0.5f));
    CHECK(hud_particle_alpha(base, life, life, fade_in) == Catch::Approx(0.f));
    CHECK(hud_particle_alpha(base, life * 2.f, life, fade_in) == Catch::Approx(0.f));

    // Degenerate lifetime must not divide by zero.
    CHECK(hud_particle_alpha(base, 1.f, 0.f, fade_in) == Catch::Approx(0.f));
}

TEST_CASE("hud_particle_alpha_does_not_compound_across_steps", "[hud_particle]") {
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
    while (age < 9.f) {
        age += 1.f / 60.f;
        stepped = hud_particle_alpha(base, age, life, fade_in);
    }
    CHECK(stepped == Catch::Approx(hud_particle_alpha(base, age, life, fade_in)));
    // 90% through a 10 s life, two thirds of the 3 s fade-out have elapsed, so
    // a third of the base alpha remains (measured: 0.267 of base 0.8).
    CHECK(stepped == Catch::Approx(base * (1.f - (age - 7.f) / 3.f)).margin(0.02f));
    // The old compounding fade reached ~0 within a handful of frames. Anything
    // in that ballpark means the multiply is back; a third of base is ~0.27.
    CHECK(stepped > 0.2f);
}

TEST_CASE("hud_particle_alpha_is_monotonic_over_the_fade_out", "[hud_particle]") {
    using lighting::hud_particle_alpha;

    float prev = hud_particle_alpha(1.f, 7.f, 10.f, 0.4f);
    for (int i = 1; i <= 30; ++i) {
        const float age = 7.f + static_cast<float>(i) * 0.1f;
        const float now = hud_particle_alpha(1.f, age, 10.f, 0.4f);
        CHECK(now <= prev);
        prev = now;
    }
    CHECK(prev == Catch::Approx(0.f).margin(0.001f));
}

TEST_CASE("hud_particle_sway_is_bounded_and_zero_mean", "[hud_particle]") {
    using lighting::hud_particle_sway;

    constexpr float amp = 40.f;
    constexpr float freq = 0.5f; // one cycle every 2 s
    constexpr float phase = 1.1f;

    // Sway is a VELOCITY, so its integral is the lateral displacement. Over a
    // whole number of cycles that must come back to ~0, or a "sway" would be a
    // steady sideways drift that walks every particle off one edge.
    float displacement = 0.f;
    constexpr float dt = 1.f / 240.f;
    for (int i = 0; i < 480; ++i) { // 2 s = exactly one cycle
        const float v = hud_particle_sway(amp, freq, phase, static_cast<float>(i) * dt);
        CHECK(std::abs(v) <= amp);
        displacement += v * dt;
    }
    CHECK(displacement == Catch::Approx(0.f).margin(amp * dt * 2.f));

    // Disabled by either factor being zero — that is how the calm emitters opt out.
    CHECK(hud_particle_sway(0.f, freq, phase, 1.f) == Catch::Approx(0.f));
    CHECK(hud_particle_sway(amp, 0.f, phase, 1.f) == Catch::Approx(0.f));
}

TEST_CASE("hud_particle_flicker_only_darkens", "[hud_particle]") {
    using lighting::hud_particle_flicker;

    // Depth 0 must be a true no-op: every non-ember emitter multiplies by this.
    for (int i = 0; i < 20; ++i) {
        const float age = static_cast<float>(i) * 0.037f;
        CHECK(hud_particle_flicker(0.f, 0.7f, age) == Catch::Approx(1.f));
    }

    // A guttering ember dips but never brightens past full and never inverts,
    // so the flicker can only ever subtract from the age envelope.
    constexpr float depth = 0.6f;
    float lowest = 1.f;
    for (int i = 0; i < 2000; ++i) {
        const float age = static_cast<float>(i) * 0.001f;
        const float f = hud_particle_flicker(depth, 0.3f, age);
        CHECK(f <= 1.0001f);
        CHECK(f >= 1.f - depth - 0.0001f);
        lowest = std::min(lowest, f);
    }
    // It must actually reach near the bottom of its range, not just hover at 1.
    CHECK(lowest == Catch::Approx(1.f - depth).margin(0.02f));
}

TEST_CASE("hud_particle_sway_makes_a_serpentine_not_a_straight_line", "[hud_particle]") {
    using lighting::hud_particle_step_velocity;

    // A snowflake: falls, and wanders wider than it drifts.
    auto flake = lighting::hud_particle{};
    flake.vy = 120.f;
    flake.vx = 5.f;
    flake.sway_amp = 60.f;
    flake.sway_freq = 0.4f;

    auto reversals = 0;
    auto prev_sign = 0;
    for (auto i = 0; i < 600; ++i) { // 5 s at 120 Hz
        flake.age = static_cast<float>(i) / 120.f;
        const auto v = hud_particle_step_velocity(flake);
        const auto sign = v.vx > 0.f ? 1 : -1;
        if (prev_sign != 0 && sign != prev_sign) { ++reversals; }
        prev_sign = sign;
        // Vertical motion is untouched by sway — only the horizontal wanders.
        CHECK(v.vy == Catch::Approx(flake.vy));
    }
    // 5 s at 0.4 Hz is two full cycles: the flake must change direction, not
    // slide steadily to one side.
    CHECK(reversals >= 3);
}

TEST_CASE("hud_particle_leaf_tumble_couples_glide_and_fall", "[hud_particle]") {
    using lighting::hud_particle_step_velocity;

    auto leaf = lighting::hud_particle{};
    leaf.vy = 100.f;
    leaf.swirl = 80.f;

    // Broadside (rotation 0): maximum sideways glide, minimum fall speed.
    leaf.rotation = 0.f;
    const auto flat = hud_particle_step_velocity(leaf);
    // Edge-on (rotation 90): no glide, fastest fall.
    leaf.rotation = 90.f;
    const auto edge = hud_particle_step_velocity(leaf);

    CHECK(flat.vx == Catch::Approx(80.f));
    CHECK(edge.vx == Catch::Approx(0.f).margin(0.01f));
    CHECK(edge.vy > flat.vy);
    CHECK(flat.vy == Catch::Approx(100.f));
    CHECK(edge.vy == Catch::Approx(135.f));

    // Half a turn later the glide reverses — that flip-flop IS the tumble.
    leaf.rotation = 180.f;
    CHECK(hud_particle_step_velocity(leaf).vx == Catch::Approx(-80.f));

    // A particle with no swirl is unaffected by its rotation.
    auto mote = lighting::hud_particle{};
    mote.vy = 100.f;
    mote.rotation = 37.f;
    const auto m = hud_particle_step_velocity(mote);
    CHECK(m.vx == Catch::Approx(0.f));
    CHECK(m.vy == Catch::Approx(100.f));
}
