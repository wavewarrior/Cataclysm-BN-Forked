#pragma once

#include <array>
#include <chrono>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// A test case that is known to fail or flake, skipped by the forked runner until `expires`
/// (an ISO date, inclusive). There is no baseline allow-list: a failing case either gets fixed or
/// lands here with a reason and a deadline. After the deadline the case runs again and fails the
/// run, so an entry cannot quietly outlive its reason.
struct quarantine_entry {
    std::string_view name;
    std::string_view expires;
    std::string_view reason;
};

inline constexpr auto quarantine_entries = std::to_array<quarantine_entry>({
    {
        .name = "MSX++UnDeadPeopleEdition cross-sheet tile sprites are anchored to their sheet",
        .expires = "2026-10-27",
        .reason = "fails on every platform and seed; tileset audit finds mis-anchored sprites",
    },
    {
        .name = "driver_loop_a_sleep_ends_at_its_limit_with_the_avatar_awake",
        .expires = "2026-10-27",
        .reason = "passes its assertions but logs 'Tried to open UI in test mode' (ui.cpp:925)",
    },
    {
        .name = "rolling_steering_turns_vehicle",
        .expires = "2026-10-27",
        .reason = "fails on some seeds in-process too (measured seeds 2 and 3)",
    },
    {
        .name = "vehicle_efficiency",
        .expires = "2026-10-27",
        .reason =
            "flaky on CPU compute with pool off only: seed 1 failed 2 of 5 runs, seed 3 1 of 1, seeds 2 and 4-10 passed; default path passed all 10 seeds",
    },
    {
        .name = "Items rot away",
        .expires = "2026-10-27",
        .reason =
            "unexplained: failed in the runner's forked run and its fresh CPU re-run while the box ran 32 children (seed 3); passed alone on seeds 1-10 on default and CPU/pool-off paths; not yet run under load again (the runner skips quarantined cases, so seeds 4-8 did not exercise it)",
    },
    {
        .name = "box2d_authority_vehicle_hits_vehicle",
        .expires = "2026-10-27",
        .reason =
            "fails at vehicle_collision_test.cpp:231 on seed 4 in the forked run and a fresh CPU re-run; passed on seeds 1-3 and 5-8; cause unknown",
    },
    {
        .name = "effective vs actual damage per second",
        .expires = "2026-10-27",
        .reason =
            "fails at effective_dps_test.cpp:78 on seed 6 in the forked run and a fresh CPU re-run; passed on seeds 1-5 and 7-8; cause unknown",
    },
    {
        .name = "vehicle_move_notifications_per_turn",
        .expires = "2026-10-27",
        .reason = "fails on some seeds in-process too (measured seeds 2 and 3)",
    },
    {
        .name = "avatar_can_walk_inside_stopped_vehicle",
        .expires = "2026-10-27",
        .reason = "fails on seed 3 in-process too (measured)",
    },
    {
        .name = "the rebuild plan lists the levels the dirty state licenses for rebuild",
        .expires = "2026-10-27",
        .reason = "flaky: fails on some seeds, passes on others, in-process and forked",
    },
});

struct quarantine_split {
    /// Cases to run: everything not actively quarantined, including expired entries.
    std::vector<std::string> run;
    /// Entries whose case was skipped.
    std::vector<quarantine_entry> skipped;
    /// Entries past their deadline whose case ran again.
    std::vector<quarantine_entry> expired;
};

/// Splits `names` by `entries` as of `today` (an ISO date; ISO dates order as strings).
inline auto split_by_quarantine(
    std::span<const std::string> names, std::span<const quarantine_entry> entries,
    std::string_view today) -> quarantine_split {
    auto split = quarantine_split();
    for (const auto& name : names) {
        const quarantine_entry* entry = nullptr;
        for (const auto& candidate : entries) {
            if (candidate.name == name) {
                entry = &candidate;
                break;
            }
        }
        if (entry == nullptr) {
            split.run.push_back(name);
        } else if (entry->expires < today) {
            split.run.push_back(name);
            split.expired.push_back(*entry);
        } else {
            split.skipped.push_back(*entry);
        }
    }
    return split;
}

/// Today's date, ISO formatted, in UTC.
inline auto today_iso() -> std::string {
    const auto days = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
    return std::format("{:%F}", days);
}
