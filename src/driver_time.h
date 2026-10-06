#pragma once

#include <expected>
#include <optional>
#include <string>

/// The agent driver's `set_time`: pins the world clock, the way a Trial's `start_date` and
/// `time_of_day` ask. The game has no months: its calendar is year, season and day of the season,
/// so the date is written `YYYY-SS-DD` (year from 0001, season 01 spring to 04 winter, day of the
/// season from 01 to the world's season length) and the time `HH:MM`.
namespace driver_time
{

/// What to pin; a field left out keeps the clock's current value for it (the day for a time,
/// the time of day for a date).
struct request {
    std::optional<std::string> date;
    std::optional<std::string> time;
};

/// The clock after a pin.
struct pinned {
    /// The game turn counter (seconds since the calendar's start).
    int turn = 0;
    std::string date;
    std::string time;
};

/// Moves `calendar::turn` as asked and reports where it landed. Time jumps without the world
/// simulating the interval. A refused request names why and leaves the clock where it was.
auto pin( const request &asked ) -> std::expected<pinned, std::string>;

} // namespace driver_time
