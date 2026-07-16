#include "ui_tween.h"

#include <cmath>
#include <map>

namespace ui_tween
{

namespace
{
constexpr float PI = 3.14159265358979323846f;

// from + (to-from)*t, kept local so this TU has no engine dependency and so
// overshoot (back/elastic eased > 1) propagates correctly.
inline auto lerp_unclamped( float from, float to, float t ) -> float
{
    return from + ( to - from ) * t;
}

auto bounce_out( float t ) -> float
{
    constexpr float n1 = 7.5625f;
    constexpr float d1 = 2.75f;
    if( t < 1.0f / d1 ) {
        return n1 * t * t;
    } else if( t < 2.0f / d1 ) {
        t -= 1.5f / d1;
        return n1 * t * t + 0.75f;
    } else if( t < 2.5f / d1 ) {
        t -= 2.25f / d1;
        return n1 * t * t + 0.9375f;
    }
    t -= 2.625f / d1;
    return n1 * t * t + 0.984375f;
}
} // namespace

auto string_to_ease( const std::string &s ) -> ease_curve
{
    static const std::map<std::string, ease_curve> m = {
        { "linear", ease_curve::linear },
        { "sine_in", ease_curve::sine_in },
        { "sine_out", ease_curve::sine_out },
        { "sine_in_out", ease_curve::sine_in_out },
        { "quad_in", ease_curve::quad_in },
        { "quad_out", ease_curve::quad_out },
        { "quad_in_out", ease_curve::quad_in_out },
        { "cubic_in", ease_curve::cubic_in },
        { "cubic_out", ease_curve::cubic_out },
        { "cubic_in_out", ease_curve::cubic_in_out },
        { "quart_in", ease_curve::quart_in },
        { "quart_out", ease_curve::quart_out },
        { "quart_in_out", ease_curve::quart_in_out },
        { "expo_in", ease_curve::expo_in },
        { "expo_out", ease_curve::expo_out },
        { "expo_in_out", ease_curve::expo_in_out },
        { "back_in", ease_curve::back_in },
        { "back_out", ease_curve::back_out },
        { "back_in_out", ease_curve::back_in_out },
        { "elastic_in", ease_curve::elastic_in },
        { "elastic_out", ease_curve::elastic_out },
        { "elastic_in_out", ease_curve::elastic_in_out },
        { "bounce_in", ease_curve::bounce_in },
        { "bounce_out", ease_curve::bounce_out },
        { "bounce_in_out", ease_curve::bounce_in_out },
        { "spring", ease_curve::spring },
    };
    const auto it = m.find( s );
    return it != m.end() ? it->second : ease_curve::linear;
}

auto string_to_loop( const std::string &s ) -> tween_loop
{
    if( s == "loop" ) {
        return tween_loop::loop;
    }
    if( s == "pingpong" ) {
        return tween_loop::pingpong;
    }
    return tween_loop::once;
}

auto apply_ease( ease_curve c, float t ) -> float
{
    // Clamp progress; endpoints must be exact so a settled tween reads its target.
    if( t <= 0.0f ) {
        return 0.0f;
    }
    if( t >= 1.0f ) {
        return 1.0f;
    }
    switch( c ) {
        case ease_curve::linear:
            return t;
        case ease_curve::sine_in:
            return 1.0f - std::cos( ( t * PI ) * 0.5f );
        case ease_curve::sine_out:
            return std::sin( ( t * PI ) * 0.5f );
        case ease_curve::sine_in_out:
            return -( std::cos( PI * t ) - 1.0f ) * 0.5f;
        case ease_curve::quad_in:
            return t * t;
        case ease_curve::quad_out:
            return 1.0f - ( 1.0f - t ) * ( 1.0f - t );
        case ease_curve::quad_in_out:
            return t < 0.5f ? 2.0f * t * t
                   : 1.0f - std::pow( -2.0f * t + 2.0f, 2.0f ) * 0.5f;
        case ease_curve::cubic_in:
            return t * t * t;
        case ease_curve::cubic_out:
            return 1.0f - std::pow( 1.0f - t, 3.0f );
        case ease_curve::cubic_in_out:
            return t < 0.5f ? 4.0f * t * t * t
                   : 1.0f - std::pow( -2.0f * t + 2.0f, 3.0f ) * 0.5f;
        case ease_curve::quart_in:
            return t * t * t * t;
        case ease_curve::quart_out:
            return 1.0f - std::pow( 1.0f - t, 4.0f );
        case ease_curve::quart_in_out:
            return t < 0.5f ? 8.0f * t * t * t * t
                   : 1.0f - std::pow( -2.0f * t + 2.0f, 4.0f ) * 0.5f;
        case ease_curve::expo_in:
            return std::pow( 2.0f, 10.0f * t - 10.0f );
        case ease_curve::expo_out:
            return 1.0f - std::pow( 2.0f, -10.0f * t );
        case ease_curve::expo_in_out:
            return t < 0.5f ? std::pow( 2.0f, 20.0f * t - 10.0f ) * 0.5f
                   : ( 2.0f - std::pow( 2.0f, -20.0f * t + 10.0f ) ) * 0.5f;
        case ease_curve::back_in: {
            constexpr float c1 = 1.70158f;
            constexpr float c3 = c1 + 1.0f;
            return c3 * t * t * t - c1 * t * t;
        }
        case ease_curve::back_out: {
            constexpr float c1 = 1.70158f;
            constexpr float c3 = c1 + 1.0f;
            const float p = t - 1.0f;
            return 1.0f + c3 * p * p * p + c1 * p * p;
        }
        case ease_curve::back_in_out: {
            constexpr float c1 = 1.70158f;
            constexpr float c2 = c1 * 1.525f;
            if( t < 0.5f ) {
                const float q = 2.0f * t;
                return ( q * q * ( ( c2 + 1.0f ) * q - c2 ) ) * 0.5f;
            }
            const float q = 2.0f * t - 2.0f;
            return ( q * q * ( ( c2 + 1.0f ) * q + c2 ) + 2.0f ) * 0.5f;
        }
        case ease_curve::elastic_in: {
            constexpr float c4 = ( 2.0f * PI ) / 3.0f;
            return -std::pow( 2.0f, 10.0f * t - 10.0f ) * std::sin( ( t * 10.0f - 10.75f ) * c4 );
        }
        case ease_curve::elastic_out: {
            constexpr float c4 = ( 2.0f * PI ) / 3.0f;
            return std::pow( 2.0f, -10.0f * t ) * std::sin( ( t * 10.0f - 0.75f ) * c4 ) + 1.0f;
        }
        case ease_curve::elastic_in_out: {
            constexpr float c5 = ( 2.0f * PI ) / 4.5f;
            if( t < 0.5f ) {
                return -( std::pow( 2.0f, 20.0f * t - 10.0f ) *
                          std::sin( ( 20.0f * t - 11.125f ) * c5 ) ) * 0.5f;
            }
            return ( std::pow( 2.0f, -20.0f * t + 10.0f ) *
                     std::sin( ( 20.0f * t - 11.125f ) * c5 ) ) * 0.5f + 1.0f;
        }
        case ease_curve::bounce_in:
            return 1.0f - bounce_out( 1.0f - t );
        case ease_curve::bounce_out:
            return bounce_out( t );
        case ease_curve::bounce_in_out:
            return t < 0.5f ? ( 1.0f - bounce_out( 1.0f - 2.0f * t ) ) * 0.5f
                   : ( 1.0f + bounce_out( 2.0f * t - 1.0f ) ) * 0.5f;
        case ease_curve::num_curves:
            break;
    }
    return t;
}

auto tween::value_at( std::uint32_t now ) const -> float
{
    if( duration_ms == 0 ) {
        return to;
    }
    if( now <= start_ms ) {
        return from;
    }
    const std::uint32_t e = now - start_ms;
    float phase = 0.0f;
    bool reversed = false;
    switch( loop ) {
        case tween_loop::once:
            if( e >= duration_ms ) {
                return to;
            }
            phase = static_cast<float>( e ) / duration_ms;
            break;
        case tween_loop::loop: {
            const std::uint32_t leg = e / duration_ms;
            if( repeats > 0 && leg >= static_cast<std::uint32_t>( repeats ) ) {
                return to;
            }
            phase = static_cast<float>( e % duration_ms ) / duration_ms;
            break;
        }
        case tween_loop::pingpong: {
            const std::uint32_t leg = e / duration_ms;
            if( repeats > 0 && leg >= static_cast<std::uint32_t>( repeats ) ) {
                // Rest at whichever endpoint the final leg lands on.
                return ( repeats % 2 == 0 ) ? from : to;
            }
            phase = static_cast<float>( e % duration_ms ) / duration_ms;
            reversed = ( leg % 2 ) == 1;
            break;
        }
    }
    const float eased = apply_ease( ease, phase );
    return reversed ? lerp_unclamped( to, from, eased )
           : lerp_unclamped( from, to, eased );
}

auto tween::settled( std::uint32_t now ) const -> bool
{
    if( duration_ms == 0 ) {
        return true;
    }
    if( now <= start_ms ) {
        return false;
    }
    const std::uint32_t e = now - start_ms;
    switch( loop ) {
        case tween_loop::once:
            return e >= duration_ms;
        case tween_loop::loop:
        case tween_loop::pingpong:
            if( repeats <= 0 ) {
                return false;
            }
            return ( e / duration_ms ) >= static_cast<std::uint32_t>( repeats );
    }
    return true;
}
auto spring_state::step( float dt_seconds ) -> void
{
    // Clamp dt to 1/30s to prevent numerical explosion.
    dt_seconds = std::min( dt_seconds, 1.f / 30.f );
    const float displacement = position - target;
    const float accel = ( -stiffness * displacement - damping * velocity ) / mass;
    velocity += accel * dt_seconds;
    position += velocity * dt_seconds;
}

auto spring_state::settled( float threshold ) const -> bool
{
    return std::abs( position - target ) < threshold
    && std::abs( velocity ) < threshold;
}

} // namespace ui_tween

