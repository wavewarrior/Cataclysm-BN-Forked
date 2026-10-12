#include "catch/catch_amalgamated.hpp"
#include "turn_plan.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

// The turn plan seam: the order and gates of one game turn, asserted as a pure value built
// from `turn_inputs` only. No game, no world, no GPU. The executor in `turn_plan.cpp`
// dispatches from this same plan, so the asserted order is the executed order by construction.

namespace {

auto bit(const int bits, const int n) -> bool { return ((bits >> n) & 1) != 0; }

/// Every combination of the boolean inputs, for every coop role.
auto all_inputs() -> std::vector<turn_inputs> {
    auto out = std::vector<turn_inputs>{};
    for (const coop_role coop : {coop_role::solo, coop_role::host, coop_role::client}) {
        for (int bits = 0; bits < (1 << 14); ++bits) {
            auto in = turn_inputs{};
            in.coop = coop;
            in.sleep = {
                .vehicles = bit(bits, 0),
                .sound = bit(bits, 1),
                .monsters = bit(bits, 2),
                .npcs = bit(bits, 3),
            };
            in.calendar = {
                .daily = bit(bits, 4),
                .horde_move = bit(bits, 5),
                .npc_overmap_move = bit(bits, 6),
                .autosave = bit(bits, 7),
            };
            in.npcs_dirty = bit(bits, 8);
            in.avatar_idle = bit(bits, 9);
            in.driving_view_offset_set = bit(bits, 10);
            in.scent_masked = bit(bits, 11);
            in.surface_dry = bit(bits, 12);
            in.forced_redraw = bit(bits, 13);
            out.push_back(in);
        }
    }
    return out;
}

constexpr auto frame_kinds = std::array<turn_step_kind, 7>{{
    turn_step_kind::frame_caches,
    turn_step_kind::frame_driving_offset,
    turn_step_kind::frame_map_caches,
    turn_step_kind::frame_mon_info,
    turn_step_kind::frame_wait_redraw,
    turn_step_kind::frame_hearing_loss,
    turn_step_kind::frame_audio,
}};

auto count_of(const std::vector<turn_step>& steps, const turn_step_kind kind) -> int {
    return static_cast<int>(std::ranges::count(steps, kind, &turn_step::kind));
}

auto client_inputs() -> turn_inputs {
    auto in = turn_inputs{};
    in.coop = coop_role::client;
    return in;
}

} // namespace

TEST_CASE("every plan from every input combination satisfies the plan laws", "[turn_plan]") {
    auto bad_plans = 0;
    for (const turn_inputs& in : all_inputs()) {
        if (!check_plan_laws(derive_turn_plan(in)).empty()) { ++bad_plans; }
    }
    CHECK(bad_plans == 0);
}

TEST_CASE("calendar law: advance_calendar precedes every step that reads the turn", "[turn_plan]") {
    const auto plan = derive_turn_plan(turn_inputs{});
    const auto cal = index_of(plan.steps, turn_step_kind::advance_calendar);
    REQUIRE(cal.has_value());
    for (const turn_step& s : plan.steps) {
        if (s.reads_turn) { CHECK(*cal < *index_of(plan.steps, s.kind)); }
    }
}

TEST_CASE("fork law: the coop client turn view holds only avatar steps", "[turn_plan]") {
    const auto plan = derive_turn_plan(client_inputs());
    const auto view = plan.view(turn_mode::client_turn);
    REQUIRE_FALSE(view.empty());
    CHECK(std::ranges::count(view, turn_scope::world, &turn_step::scope) == 0);
    CHECK(std::ranges::all_of(view, [](const turn_step& s) {
        return s.scope == turn_scope::avatar;
    }));
}

TEST_CASE(
    "frame law: each frame step runs once in the client frame view and never in the client turn view",
    "[turn_plan]") {
    const auto plan = derive_turn_plan(client_inputs());
    const auto frame_view = plan.view(turn_mode::client_frame);
    const auto turn_view = plan.view(turn_mode::client_turn);
    for (const turn_step_kind kind : frame_kinds) {
        CHECK(count_of(frame_view, kind) == 1);
        CHECK(count_of(turn_view, kind) == 0);
    }
}

TEST_CASE(
    "views: host and solo turns run world and avatar steps, never frame steps", "[turn_plan]") {
    auto host_in = turn_inputs{};
    host_in.coop = coop_role::host;
    const auto plan = derive_turn_plan(host_in);
    for (const turn_mode mode : {turn_mode::solo, turn_mode::host}) {
        const auto view = plan.view(mode);
        CHECK(std::ranges::count(view, turn_scope::world, &turn_step::scope) > 0);
        CHECK(std::ranges::count(view, turn_scope::frame, &turn_step::scope) == 0);
    }
}

TEST_CASE("a coop client skips world steps and drains sounds itself", "[turn_plan]") {
    const auto plan = derive_turn_plan(client_inputs());
    const auto load = index_of(plan.steps, turn_step_kind::load_npcs);
    REQUIRE(load.has_value());
    CHECK(plan.steps[*load].status == step_status::skip);
    CHECK_FALSE(plan.steps[*load].reason.empty());

    const auto drain = index_of(plan.steps, turn_step_kind::clear_recent_sounds);
    REQUIRE(drain.has_value());
    CHECK(plan.steps[*drain].status == step_status::run);
}

TEST_CASE("a host drains sounds through process_sounds, not clear_recent_sounds", "[turn_plan]") {
    auto host_in = turn_inputs{};
    host_in.coop = coop_role::host;
    const auto plan = derive_turn_plan(host_in);
    const auto drain = index_of(plan.steps, turn_step_kind::clear_recent_sounds);
    REQUIRE(drain.has_value());
    CHECK(plan.steps[*drain].status == step_status::skip);
}

TEST_CASE("a calendar gate skips its step until the boundary is due", "[turn_plan]") {
    auto in = turn_inputs{};
    const auto mongroups = [&]() -> turn_step {
        const auto plan = derive_turn_plan(in);
        return plan.steps[*index_of(plan.steps, turn_step_kind::process_mongroups)];
    };
    CHECK(mongroups().status == step_status::skip);
    in.calendar.daily = true;
    CHECK(mongroups().status == step_status::run);
}

TEST_CASE("moving advance_calendar past a turn reader trips the calendar law", "[turn_plan]") {
    auto plan = derive_turn_plan(turn_inputs{});
    REQUIRE(check_plan_laws(plan).empty());

    const auto cal =
        std::ranges::find(plan.steps, turn_step_kind::advance_calendar, &turn_step::kind);
    REQUIRE(cal != plan.steps.end());
    const auto moved = *cal;
    plan.steps.erase(cal);
    const auto timed =
        std::ranges::find(plan.steps, turn_step_kind::timed_events_and_missions, &turn_step::kind);
    REQUIRE(timed != plan.steps.end());
    plan.steps.insert(std::next(timed), moved);

    const auto violations = check_plan_laws(plan);
    CHECK_FALSE(violations.empty());
}

TEST_CASE("a skipped step carries a reason and a run step carries none", "[turn_plan]") {
    const auto plan = derive_turn_plan(turn_inputs{});
    for (const turn_step& s : plan.steps) {
        CHECK((s.status == step_status::skip) == !s.reason.empty());
    }
}

TEST_CASE("sim perf line pins the legacy format", "[turn_plan][sim][perf]") {
    // Per-turn values whose twenty-turn sums are 30, 10, 5 and 15 ms.
    const auto sample = perf_sample{
        .sim_total_ms = 1.5,
        .build_map_cache_ms = 0.5,
        .monmove_ms = 0.25,
        .world_tick_ms = 0.75,
    };
    auto agg = sim_perf_aggregator{};
    for (int i = 0; i < turns_per_line - 1; ++i) { CHECK_FALSE(agg.add(sample).has_value()); }
    const auto line = agg.add(sample);
    REQUIRE(line.has_value());
    CHECK(
        *line
        == "[sim][perf] 20 turns avg: sim_total=1.5ms (build_map_cache=0.5 monmove=0.25 world_tick=0.75)");
    CHECK_FALSE(agg.add(perf_sample{}).has_value());
}

TEST_CASE("profiling zones keep the legacy do_turn names", "[turn_plan]") {
    CHECK(std::string_view{zone_name_of(turn_step_kind::process_items)} == "do_turn_process_items");
    CHECK(std::string_view{zone_name_of(turn_step_kind::scent_update)} == "do_turn_scent");
    CHECK(std::string_view{zone_name_of(turn_step_kind::advance_calendar)} == "do_turn_calendar");
    CHECK(std::string_view{zone_name_of(turn_step_kind::move_hordes)} == "do_turn_overmap_hordes");
    CHECK(std::string_view{zone_name_of(turn_step_kind::vehicle_theft)} == "vehicle_theft");
}
