#include "catch/catch_amalgamated.hpp"
#include "driver_message_delta.h"

#include <string>
#include <vector>

using messages = std::vector<std::string>;
using entries = std::vector<log_entry>;

TEST_CASE("driver_message_delta_reports_appended_messages", "[driver]") {
    const message_delta delta =
        compute_message_delta({{1, "a"}, {2, "b"}}, {{1, "a"}, {2, "b"}, {3, "c"}, {4, "d"}});
    CHECK(delta.fresh == messages{"c", "d"});
    CHECK_FALSE(delta.lost);
}

TEST_CASE("driver_message_delta_is_empty_when_nothing_was_logged", "[driver]") {
    const entries log = {{1, "a"}, {2, "b"}};
    CHECK(compute_message_delta(log, log).fresh.empty());
    CHECK(compute_message_delta({}, {}).fresh.empty());
}

TEST_CASE("driver_message_delta_reports_a_repeat_merged_into_the_newest_entry", "[driver]") {
    // The log length does not change when a repeat merges, so a count-based delta sees nothing.
    CHECK(compute_message_delta({{1, "a"}, {2, "wall"}}, {{1, "a"}, {2, "wall x 2"}}).fresh
          == messages{"wall x 2"});
    CHECK(compute_message_delta({{1, "a"}, {2, "wall x 2"}}, {{1, "a"}, {2, "wall x 3"}}).fresh
          == messages{"wall x 3"});
    CHECK(compute_message_delta({{1, "wall"}}, {{1, "wall x 2"}, {2, "dust"}}).fresh
          == messages{"wall x 2", "dust"});
}

TEST_CASE("driver_message_delta_does_not_mistake_older_repeats_for_new_ones", "[driver]") {
    const entries log = {{1, "wall x 2"}, {2, "a"}};
    CHECK(compute_message_delta(log, log).fresh.empty());
}

TEST_CASE("driver_message_delta_follows_a_log_that_drops_entries_from_the_front", "[driver]") {
    const message_delta delta =
        compute_message_delta({{1, "a"}, {2, "b"}, {3, "c"}}, {{2, "b"}, {3, "c"}, {4, "d"}});
    CHECK(delta.fresh == messages{"d"});
    CHECK_FALSE(delta.lost);
    CHECK(
        compute_message_delta({{1, "a"}, {2, "b"}, {3, "c"}}, {{3, "c"}, {4, "d"}, {5, "e"}}).fresh
        == messages{"d", "e"});
}

TEST_CASE("driver_message_delta_is_not_fooled_by_a_repeating_pattern", "[driver]") {
    // A full window of "A, B, A, B" that gains "A, B" looks identical by content alone.
    const message_delta delta = compute_message_delta(
        {{1, "A"}, {2, "B"}, {3, "A"}, {4, "B"}}, {{3, "A"}, {4, "B"}, {5, "A"}, {6, "B"}});
    CHECK(delta.fresh == messages{"A", "B"});
    CHECK_FALSE(delta.lost);
}

TEST_CASE("driver_message_delta_flags_additions_that_scrolled_out_of_the_window", "[driver]") {
    const message_delta delta =
        compute_message_delta({{1, "a"}, {2, "b"}}, {{5, "w"}, {6, "x"}, {7, "y"}, {8, "z"}});
    CHECK(delta.fresh == messages{"w", "x", "y", "z"});
    CHECK(delta.lost);
}

TEST_CASE("driver_message_delta_reports_everything_when_the_log_was_empty", "[driver]") {
    const message_delta delta = compute_message_delta({}, {{1, "a"}, {2, "b"}});
    CHECK(delta.fresh == messages{"a", "b"});
    CHECK_FALSE(delta.lost);
}

TEST_CASE("driver_message_delta_reports_everything_when_the_log_restarted", "[driver]") {
    // A reload gives the log fresh, lower sequence numbers.
    const message_delta delta = compute_message_delta({{40, "old"}}, {{1, "new"}});
    CHECK(delta.fresh == messages{"new"});
}

TEST_CASE("driver_cap_messages_keeps_the_newest_and_cuts_long_ones", "[driver]") {
    messages many = {"a", "b", "c", "d"};
    CHECK(cap_messages(many, 2, 10));
    CHECK(many == messages{"c", "d"});

    messages fits = {"a", "b"};
    CHECK_FALSE(cap_messages(fits, 2, 10));
    CHECK(fits == messages{"a", "b"});

    messages longer = {"abcdef"};
    CHECK(cap_messages(longer, 2, 4));
    CHECK(longer == messages{"abcd"});
}

TEST_CASE("driver_cap_messages_never_splits_a_multibyte_character", "[driver]") {
    // "é" is two bytes (0xC3 0xA9): a cut after one byte must back up to before it.
    messages accented = {
        "abc\xC3\xA9"
        "def"};
    CHECK(cap_messages(accented, 2, 4));
    CHECK(accented == messages{"abc"});
}
