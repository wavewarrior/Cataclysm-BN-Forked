#include "catch/catch_amalgamated.hpp"
#include "quarantine.h"

#include <array>
#include <string>

namespace {
constexpr auto entries = std::to_array<quarantine_entry>({
    {.name = "flaky", .expires = "2026-10-27", .reason = "r"},
});
const auto names = std::array<std::string, 2>{"flaky", "solid"};
} // namespace

TEST_CASE("quarantine_skips_an_entry_until_its_deadline_inclusive", "[quarantine]") {
    SECTION("before the deadline") {
        const auto split = split_by_quarantine(names, entries, "2026-10-01");
        CHECK(split.run == std::vector<std::string>{"solid"});
        CHECK(split.skipped.size() == 1);
        CHECK(split.expired.empty());
    }
    SECTION("on the deadline day") {
        const auto split = split_by_quarantine(names, entries, "2026-10-27");
        CHECK(split.run == std::vector<std::string>{"solid"});
        CHECK(split.expired.empty());
    }
}

TEST_CASE("quarantine_runs_an_expired_entry_again_and_says_so", "[quarantine]") {
    const auto split = split_by_quarantine(names, entries, "2026-10-28");
    CHECK(split.run == std::vector<std::string>{"flaky", "solid"});
    CHECK(split.skipped.empty());
    REQUIRE(split.expired.size() == 1);
    CHECK(split.expired.front().name == "flaky");
}
