#include "driver_window.h"

#include <algorithm>
#include <charconv>
#include <string>
#include <system_error>

namespace
{

auto requested_window = std::optional<driver_window_size>();

/// A positive whole number that is the whole of `digits`.
auto read_dimension( std::string_view digits ) -> std::optional<int>
{
    int value = 0;
    const char *const end = digits.data() + digits.size();
    const auto [stop, error] = std::from_chars( digits.data(), end, value );
    if( digits.empty() || error != std::errc() || stop != end || value <= 0 ) {
        return std::nullopt;
    }
    return value;
}

} // namespace

auto parse_driver_window_size( std::string_view text ) -> std::optional<driver_window_size>
{
    const size_t x = text.find( 'x' );
    if( x == std::string_view::npos ) {
        return std::nullopt;
    }
    const std::optional<int> width = read_dimension( text.substr( 0, x ) );
    const std::optional<int> height = read_dimension( text.substr( x + 1 ) );
    if( !width || !height ) {
        return std::nullopt;
    }
    return driver_window_size{ .width = *width, .height = *height };
}

auto driver_window_corner( const screen_area &usable,
                           const driver_window_size &size ) -> screen_position
{
    return {
        .x = std::max( usable.x, usable.x + usable.width - size.width ),
        .y = std::max( usable.y, usable.y + usable.height - size.height ),
    };
}

auto request_driver_window( const driver_window_size &size ) -> void
{
    requested_window = size;
}

auto requested_driver_window() -> std::optional<driver_window_size>
{
    return requested_window;
}
