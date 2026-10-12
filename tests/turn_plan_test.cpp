#include "catch/catch_amalgamated.hpp"
#include "turn_plan.h"

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace {

// Every tick and option that gates a step is on, and no sleep or activity state is set.
auto ticking_inputs() -> turn_inputs {
    return turn_inputs{
        .tick_daily = true,
        .tick_two_and_half_minutes = true,
        .tick_five_minutes = true,
        .tick_autosave = true,
        .autosave_option = true,
        .scent_writable = true,
        .driving_offset = true,
        .force_redraw = true,
        .weather_effects = true,
    };
}

auto runs(const turn_plan& plan, std::string_view name) -> bool {
    return std::ranges::any_of(plan.items(), [name](const turn_step& step) -> bool {
        return step.name == name && step.status == turn_step_status::run;
    });
}

auto position_of(const turn_plan& plan, std::string_view name) -> std::size_t {
    const auto items = plan.items();
    const auto found = std::ranges::find(items, name, &turn_step::name);
    return static_cast<std::size_t>(std::ranges::distance(items.begin(), found));
}

auto sample_turn() -> turn_report {
    auto report = turn_report{};
    report.push_back(turn_step_result{
        .name = "build_map_cache",
        .status = turn_step_status::run,
        .reason = "",
        .group = turn_perf_group::build_map_cache,
        .duration_ms = 1.0,
    });
    report.push_back(turn_step_result{
        .name = "monmove",
        .status = turn_step_status::run,
        .reason = "",
        .group = turn_perf_group::monmove,
        .duration_ms = 2.0,
    });
    report.push_back(turn_step_result{
        .name = "player_process_turn",
        .status = turn_step_status::run,
        .reason = "",
        .group = turn_perf_group::none,
        .duration_ms = 0.5,
    });
    report.push_back(turn_step_result{
        .name = "world_tick",
        .status = turn_step_status::run,
        .reason = "",
        .group = turn_perf_group::world_tick,
        .duration_ms = 4.0,
    });
    return report;
}

} // namespace

TEST_CASE("The client turn plan runs only avatar-scope steps", "[turn_plan]") {
    const auto plan = make_turn_plan(ticking_inputs(), turn_mode::client_turn);
    REQUIRE(!plan.items().empty());
    CHECK(std::ranges::all_of(plan.items(), [](const turn_step& step) -> bool {
        return step.scope == turn_scope::avatar;
    }));
}

TEST_CASE("Frame steps appear only in the client frame plan", "[turn_plan]") {
    for (const auto mode : {turn_mode::solo, turn_mode::host, turn_mode::client_turn}) {
        const auto plan = make_turn_plan(ticking_inputs(), mode);
        CHECK(std::ranges::none_of(plan.items(), [](const turn_step& step) -> bool {
            return step.scope == turn_scope::frame;
        }));
    }
    const auto frame = make_turn_plan(ticking_inputs(), turn_mode::client_frame);
    REQUIRE(!frame.items().empty());
    CHECK(std::ranges::all_of(frame.items(), [](const turn_step& step) -> bool {
        return step.scope == turn_scope::frame;
    }));
}

TEST_CASE("Each frame step appears exactly once in the client frame plan", "[turn_plan]") {
    const auto frame = make_turn_plan(ticking_inputs(), turn_mode::client_frame);
    const auto names =
        frame.items() | std::views::transform(&turn_step::name) | std::ranges::to<std::vector>();
    for (const turn_step& step : frame.items()) {
        CHECK(std::ranges::count(names, step.name) == 1);
    }
}

TEST_CASE("The calendar advances before any running step reads the turn", "[turn_plan]") {
    for (const auto mode : {turn_mode::solo, turn_mode::host}) {
        const auto plan = make_turn_plan(ticking_inputs(), mode);
        REQUIRE(runs(plan, "calendar_advance"));
        REQUIRE(std::ranges::any_of(plan.items(), &turn_step::reads_turn));
        const auto calendar = position_of(plan, "calendar_advance");
        for (const turn_step& step : plan.items()) {
            if (step.reads_turn && step.status == turn_step_status::run) {
                CHECK(calendar < position_of(plan, step.name));
            }
        }
    }
}

TEST_CASE("A new game starts instead of advancing the calendar", "[turn_plan]") {
    for (const auto mode : {turn_mode::solo, turn_mode::host}) {
        for (const bool new_game : {false, true}) {
            auto inputs = ticking_inputs();
            inputs.new_game = new_game;
            const auto plan = make_turn_plan(inputs, mode);
            CHECK(runs(plan, "new_game_start") == new_game);
            CHECK(runs(plan, "calendar_advance") != new_game);
        }
    }
}

TEST_CASE("Every skipped step records a reason", "[turn_plan]") {
    for (const auto mode :
         {turn_mode::solo, turn_mode::host, turn_mode::client_turn, turn_mode::client_frame}) {
        for (const auto& inputs : {ticking_inputs(), turn_inputs{}}) {
            const auto plan = make_turn_plan(inputs, mode);
            CHECK(std::ranges::all_of(plan.items(), [](const turn_step& step) -> bool {
                return step.status == turn_step_status::run || !step.reason.empty();
            }));
        }
    }
}

TEST_CASE("Step names are unique and non-empty in every plan", "[turn_plan]") {
    for (const auto mode :
         {turn_mode::solo, turn_mode::host, turn_mode::client_turn, turn_mode::client_frame}) {
        for (const auto& inputs : {ticking_inputs(), turn_inputs{}}) {
            const auto plan = make_turn_plan(inputs, mode);
            auto names =
                plan.items() | std::views::transform(&turn_step::name)
                | std::ranges::to<std::vector>();
            CHECK(std::ranges::all_of(names, [](std::string_view name) -> bool {
                return !name.empty();
            }));
            std::ranges::sort(names);
            CHECK(std::ranges::adjacent_find(names) == names.end());
        }
    }
}

TEST_CASE("Sleep-skip runs exactly one of npcmove and sleep_skip_npc_process", "[turn_plan]") {
    for (const bool asleep : {false, true}) {
        for (const bool skip_npc : {false, true}) {
            const auto inputs = turn_inputs{.asleep = asleep, .sleep_skip_npc = skip_npc};
            const auto plan = make_turn_plan(inputs, turn_mode::solo);
            CHECK(runs(plan, "npcmove") != runs(plan, "sleep_skip_npc_process"));
        }
    }
}

TEST_CASE("The sim perf window emits on the twentieth turn", "[turn_plan]") {
    auto window = sim_perf_window{};
    const auto turn = sample_turn();
    for (int i = 1; i < 20; ++i) { CHECK_FALSE(add_turn_to_perf_window(window, turn)); }
    CHECK(add_turn_to_perf_window(window, turn));
}

TEST_CASE("The sim perf line reproduces the pinned format", "[turn_plan]") {
    auto window = sim_perf_window{};
    for (int i = 0; i < 20; ++i) { add_turn_to_perf_window(window, sample_turn()); }
    CHECK(
        format_sim_perf_line(window)
        == "[sim][perf] 20 turns avg: sim_total=7.5ms (build_map_cache=1 monmove=2 world_tick=4)");
}
