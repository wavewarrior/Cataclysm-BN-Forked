#pragma once
#ifndef CATA_SRC_UI_TWEEN_H
#define CATA_SRC_UI_TWEEN_H

#include <cstdint>
#include <string>

// Pure, time-based tween primitives for the sidebar animation system.
//
// Curves are sampled at real elapsed time (SDL_GetTicks ms), so a tween of a
// given duration completes in that wall-clock duration regardless of how often
// it is sampled — the sidebar redraw cadence (~8-30 fps) only changes sample
// smoothness, never timing. No global clock or update loop lives here: callers
// pass `now` and read `value_at(now)`. This file has no engine dependencies and
// is unit-tested in isolation.
namespace ui_tween
{

// Robert-Penner easing family, in normalized (t in [0,1]) -> output form. The
// `back`/`elastic` curves intentionally overshoot the [0,1] range; that is the
// effect. `bounce` stays within [0,1].
enum class ease_curve {
    linear,
    sine_in, sine_out, sine_in_out,
    quad_in, quad_out, quad_in_out,
    cubic_in, cubic_out, cubic_in_out,
    quart_in, quart_out, quart_in_out,
    expo_in, expo_out, expo_in_out,
    back_in, back_out, back_in_out,
    elastic_in, elastic_out, elastic_in_out,
    bounce_in, bounce_out, bounce_in_out,
    spring,  // damped harmonic oscillator (organic overshoot + settle)
    num_curves
};

// Parse an ease name (e.g. "back_out", "sine_in_out"); unknown -> linear.
auto string_to_ease( const std::string &s ) -> ease_curve;

// Apply a curve to a normalized progress t. t is clamped to [0,1] first. The
// returned value may exceed [0,1] for back/elastic (overshoot is the point).
auto apply_ease( ease_curve c, float t ) -> float;

enum class tween_loop {
    once,       // play from->to once, then hold at `to`
    loop,       // restart from->to each cycle (sawtooth)
    pingpong    // alternate from->to->from (triangle)
};

// Parse a loop name ("once"/"loop"/"pingpong"); unknown -> once.
auto string_to_loop( const std::string &s ) -> tween_loop;

// One animated scalar channel. POD; copy-cheap. `value_at` is the only consumer.
struct tween {
    float from = 0.0f;
    float to = 0.0f;
    std::uint32_t start_ms = 0;
    std::uint32_t duration_ms = 0;
    ease_curve ease = ease_curve::linear;
    tween_loop loop = tween_loop::once;
    // For loop/pingpong: number of legs (one `duration_ms` span = one leg) before
    // holding. <= 0 means infinite. Ignored for `once`.
    int repeats = 0;

    // Sampled value at wall-clock `now` (ms). Holds at the start value before
    // `start_ms`, and at the resting value once finished.
    auto value_at( std::uint32_t now ) const -> float;

    // True once the tween has reached its resting state and will not change
    // again: `once` past its end, or a finite-repeat loop/pingpong exhausted.
    // Infinite loops/pingpongs never settle (they keep the sidebar live).
    auto settled( std::uint32_t now ) const -> bool;
};
// Spring-damper state for organic animations (Phase 2). Uses a damped harmonic
// oscillator: a = (-k*(pos-target) - c*vel)/mass. Settles naturally instead of
// following a canned curve. `step` advances by wall-clock dt; `settled` returns
// true when position and velocity are within threshold of the target.
struct spring_state {
    float position = 0.f;
    float velocity = 0.f;
    float target = 1.f;
    float stiffness = 300.f;  // spring constant k
    float damping = 20.f;     // damping coefficient c
    float mass = 1.f;

    auto step( float dt_seconds ) -> void;
    auto settled( float threshold = 0.001f ) const -> bool;
};

} // namespace ui_tween


#endif // CATA_SRC_UI_TWEEN_H
