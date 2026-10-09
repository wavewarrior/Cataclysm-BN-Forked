#include "driver_time.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <string_view>

#include "calendar.h"

namespace driver_time
{

namespace
{

/// The latest turn a pin may reach: the debug menu's own arbitrary ceiling, far enough from
/// the integer limit that the game's turn arithmetic stays safe.
constexpr auto latest_turn = std::int64_t { std::numeric_limits<int>::max() / 2 };

/// The digits `text` holds from `from`, `count` of them, as a number; none when any is not a digit.
auto digits( std::string_view text, std::size_t from, std::size_t count ) -> std::optional<int>
{
    auto value = 0;
    for( auto i = from; i < from + count; ++i ) {
        if( text[i] < '0' || text[i] > '9' ) {
            return std::nullopt;
        }
        value = value * 10 + ( text[i] - '0' );
    }
    return value;
}

/// Turns of a day, season and year at the world's current calendar settings.
struct spans {
    std::int64_t day = to_turns<int>( 1_days );
    std::int64_t season = to_turns<int>( calendar::season_length() );
    std::int64_t year = to_turns<int>( calendar::year_length() );
};

/// The turn a `YYYY-SS-DD` date starts at; says what is wrong with it otherwise.
auto date_start( const std::string &date,
                 const spans &span ) -> std::expected<std::int64_t, std::string>
{
    const auto bad = std::unexpected( std::format(
                                          "date must be YYYY-SS-DD: year from 0001, season 01 to 04 (spring to winter, the game has no months), "
                                          "day of the season from 01 to {:02}; got '{}'", span.season / span.day, date ) );
    if( date.size() != 10 || date[4] != '-' || date[7] != '-' ) {
        return bad;
    }
    const auto year = digits( date, 0, 4 );
    const auto season = digits( date, 5, 2 );
    const auto day = digits( date, 8, 2 );
    if( !year || !season || !day || *year < 1 || *season < 1 || *season > 4 || *day < 1 ||
        *day > span.season / span.day ) {
        return bad;
    }
    return ( *year - 1 ) * span.year + ( *season - 1 ) * span.season + ( *day - 1 ) * span.day;
}

/// The seconds into the day an `HH:MM` time is; says what is wrong with it otherwise.
auto time_of_day( const std::string &time ) -> std::expected<std::int64_t, std::string>
{
    const auto bad = std::unexpected( std::format( "time must be HH:MM, 00:00 to 23:59; got '{}'",
                                      time ) );
    if( time.size() != 5 || time[2] != ':' ) {
        return bad;
    }
    const auto hour = digits( time, 0, 2 );
    const auto minute = digits( time, 3, 2 );
    if( !hour || !minute || *hour > 23 || *minute > 59 ) {
        return bad;
    }
    return *hour * 3600 + *minute * 60;
}

} // namespace

auto pin( const request &asked ) -> std::expected<pinned, std::string>
{
    if( !asked.date && !asked.time ) {
        return std::unexpected( "set_time needs a date, a time, or both" );
    }
    const auto span = spans{};
    const auto now = std::int64_t{ to_turns<int>( calendar::turn - calendar::turn_zero ) };

    auto day_start = now - now % span.day;
    if( asked.date ) {
        const auto start = date_start( *asked.date, span );
        if( !start ) {
            return std::unexpected( start.error() );
        }
        day_start = *start;
    }
    auto into_day = now % span.day;
    if( asked.time ) {
        const auto seconds = time_of_day( *asked.time );
        if( !seconds ) {
            return std::unexpected( seconds.error() );
        }
        into_day = *seconds;
    }

    const auto target = day_start + into_day;
    if( target > latest_turn ) {
        return std::unexpected( std::format( "that date is too far ahead: the clock stops at turn {}",
                                             latest_turn ) );
    }
    calendar::turn = calendar::turn_zero + time_duration::from_turns( static_cast<int>( target ) );
    return pinned{
        .turn = static_cast<int>( target ),
        .date = std::format( "{:04}-{:02}-{:02}", target / span.year + 1,
                             target % span.year / span.season + 1, target % span.season / span.day + 1 ),
        .time = std::format( "{:02}:{:02}", target % span.day / 3600, target % 3600 / 60 ),
    };
}

} // namespace driver_time
