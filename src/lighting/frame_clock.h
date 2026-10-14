#pragma once

// The frame clock: the one place a rendered frame asks what time it is.
//
// ADR-0006 requires a Render tier case to capture the same scene twice and expect the frames to
// agree. Four places in the frame read the wall clock directly, so two captures taken a
// millisecond apart differed in sprite animation phase, the godray shaft's animation term and the
// floating combat text's dt: the A/A floor was whatever the wall clock happened to drift, and no
// golden image could be regenerated. Those four now ask here instead, and a test can pin the
// answer.
//
// With no override installed — every production frame — this returns exactly what those sites
// computed before: SDL_GetTicks() expressed in seconds or milliseconds. The override is a
// process-wide scalar, not a scheduler: it freezes the frame clock rather than advancing it, which
// is all a paired capture needs. Header-only on purpose; the state is a function-local static of
// an inline function, so there is one instance per process and no new translation unit.

#include <SDL3/SDL_stdinc.h>

#include <cstdint>
#include <optional>
#include <utility>

namespace lighting {

namespace detail {
inline auto frame_clock_storage() -> std::optional<double> & {
    static std::optional<double> pinned;
    return pinned;
}
} // namespace detail

/// Seconds since SDL initialised, or the pinned value while a test has pinned one.
inline auto frame_time_seconds() -> double {
    if( const auto &pinned = detail::frame_clock_storage() ) { return *pinned; }
    return static_cast<double>( SDL_GetTicks() ) / 1000.0;
}

/// The same instant in milliseconds, for the site that ticks in ms.
inline auto frame_time_milliseconds() -> std::uint32_t {
    if( const auto &pinned = detail::frame_clock_storage() ) {
        return static_cast<std::uint32_t>( *pinned * 1000.0 );
    }
    return SDL_GetTicks();
}

/// True while a test has pinned the clock.
inline auto frame_clock_is_pinned() -> bool { return detail::frame_clock_storage().has_value(); }

/// Pin the frame clock to `seconds` for as long as this object lives, restoring whatever was
/// pinned before on destruction. Move-only: a copy would double-restore.
class frame_clock_pin {
public:
    explicit frame_clock_pin( const double seconds ):
        previous_( std::exchange( detail::frame_clock_storage(), seconds ) ) {}
    frame_clock_pin( const frame_clock_pin & ) = delete;
    frame_clock_pin &operator=( const frame_clock_pin & ) = delete;
    frame_clock_pin( frame_clock_pin &&other ) noexcept:
        previous_( std::move( other.previous_ ) ) {
        other.restored_ = true;
    }
    frame_clock_pin &operator=( frame_clock_pin && ) = delete;
    ~frame_clock_pin() {
        if( !restored_ ) { detail::frame_clock_storage() = previous_; }
    }

private:
    std::optional<double> previous_;
    bool restored_ = false;
};

} // namespace lighting
