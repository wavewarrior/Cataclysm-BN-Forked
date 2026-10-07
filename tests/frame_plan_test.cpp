#include "catch/catch_amalgamated.hpp"
#include "frame_executor.h"
#include "frame_plan.h"
#include "frame_report.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// The frame plan seam (ADR-0003): the order of the render frame's steps, asserted here
// as a pure value with no GPU, no window and no world. The executor in
// `sdl_render_frame.cpp` dispatches from this same plan, so the asserted order is the
// executed order by construction.

namespace {

/// Today's phase order, spelled out independently of `default_frame_plan()` so a
/// reorder in the renderer fails here instead of surfacing as a visual glitch.
constexpr std::array<frame_step_kind, 16> today_order{{
    frame_step_kind::build_lighting,
    frame_step_kind::collector_flush,
    frame_step_kind::gpu_sdf,
    frame_step_kind::sky_sun,
    frame_step_kind::gi,
    frame_step_kind::gi_feedback,
    frame_step_kind::rc_readback,
    frame_step_kind::assemble,
    frame_step_kind::menu_background,
    frame_step_kind::overlays,
    frame_step_kind::ui_composite,
    frame_step_kind::avatar_composite,
    frame_step_kind::vehicle_composite,
    frame_step_kind::world_pass,
    frame_step_kind::tonemap,
    frame_step_kind::swapchain_composite,
}};

/// The ten legacy `[render][perf][phase avg/max ms]` names, in lap order.
constexpr std::array<std::string_view, 10> legacy_names{
    {"begin", "build_light", "flush_gather", "assemble", "menu_bg", "overlays", "ui_a", "world_w",
     "tonemap", "swap_b"}};

auto as_vector(std::span<const frame_step_kind> kinds) -> std::vector<frame_step_kind> {
    return std::vector<frame_step_kind>(kinds.begin(), kinds.end());
}

auto kinds_of(const frame_plan& plan) -> std::vector<frame_step_kind> {
    std::vector<frame_step_kind> kinds;
    for (const frame_step& s : plan.steps()) { kinds.push_back(s.kind); }
    return kinds;
}

auto steps_of(const frame_plan& plan) -> std::vector<frame_step> {
    return std::vector<frame_step>(plan.steps().begin(), plan.steps().end());
}

auto rebuild(std::vector<frame_step> steps) -> frame_plan {
    frame_plan plan;
    for (const frame_step& s : steps) { plan.push_back(s); }
    return plan;
}

/// Moves the step of `kind` to position `to`, shifting the steps in between.
auto move_step(std::vector<frame_step>& steps, frame_step_kind kind, std::size_t to)
    -> void { // *NOPAD*
    const auto it = std::ranges::find(steps, kind, &frame_step::kind);
    const frame_step moved = *it;
    steps.erase(it);
    steps.insert(steps.begin() + static_cast<std::ptrdiff_t>(to), moved);
}

} // namespace

TEST_CASE("the frame plan's step order is today's phase order", "[frame_plan]") {
    const frame_plan plan = default_frame_plan();
    CHECK(kinds_of(plan) == as_vector(today_order));
    // In this ticket the plan decides nothing but order: every step runs.
    for (const frame_step& s : plan.steps()) {
        CHECK(s.status == frame_step_status::run);
        CHECK(s.reason.empty());
    }
    // `begin_frame` is a precondition outside the plan, so it is not a step of it.
    CHECK(plan.step(frame_step_kind::build_lighting) != nullptr);
    CHECK(plan.step(frame_step_kind::collector_flush) != nullptr);
}

TEST_CASE("assemble precedes every step that begins a render pass", "[frame_plan]") {
    const frame_plan plan = default_frame_plan();
    const std::size_t assemble = plan.index_of(frame_step_kind::assemble);
    REQUIRE(assemble < plan.size());
    std::size_t checked = 0;
    for (const frame_step& s : plan.steps()) {
        // The flag covers GRAPHICS render passes on purpose: the lighting steps begin
        // compute passes only and legitimately run before the lighting inputs are
        // stamped, so a plain "begins any pass" flag would make the law false.
        if (s.has(frame_step_flag::begins_render_pass)) {
            CHECK(plan.index_of(s.kind) > assemble);
            ++checked;
        }
    }
    // The three composites, the world pass, tonemap and the swapchain composite.
    CHECK(checked == 6);
}

TEST_CASE("within lighting the order is SDF, then sky, then GI", "[frame_plan]") {
    const frame_plan plan = default_frame_plan();
    const std::size_t sdf = plan.index_of(frame_step_kind::gpu_sdf);
    const std::size_t sky = plan.index_of(frame_step_kind::sky_sun);
    const std::size_t gi = plan.index_of(frame_step_kind::gi);
    REQUIRE(sdf < plan.size());
    REQUIRE(sky < plan.size());
    REQUIRE(gi < plan.size());
    CHECK(sdf < sky);
    CHECK(sky < gi);
    // The fused group is the six lighting steps, contiguous and in plan order, so the
    // fused body's `split` sequence is the asserted sequence.
    const std::array<frame_step_kind, 6> members = plan.fused_lighting_members();
    const std::array<frame_step_kind, 6> expected{
        {frame_step_kind::collector_flush, frame_step_kind::gpu_sdf, frame_step_kind::sky_sun,
         frame_step_kind::gi, frame_step_kind::gi_feedback, frame_step_kind::rc_readback}};
    CHECK(members == expected);
    const std::size_t first = plan.index_of(members[0]);
    for (std::size_t i = 1; i < members.size(); ++i) {
        CHECK(plan.index_of(members[i]) == first + i);
    }
}

TEST_CASE("a reordered plan trips the ordering laws", "[frame_plan]") {
    // Guards that the laws above bite rather than passing vacuously.
    const std::vector<frame_step> steps = steps_of(default_frame_plan());

    std::vector<frame_step> sky_last = steps;
    move_step(sky_last, frame_step_kind::sky_sun, 5);
    const frame_plan lit = rebuild(std::move(sky_last));
    CHECK(lit.index_of(frame_step_kind::sky_sun) > lit.index_of(frame_step_kind::gi));

    std::vector<frame_step> world_first = steps;
    move_step(world_first, frame_step_kind::world_pass, 0);
    const frame_plan promoted = rebuild(std::move(world_first));
    CHECK(promoted.step(frame_step_kind::world_pass)->has(frame_step_flag::begins_render_pass));
    CHECK(promoted.index_of(frame_step_kind::world_pass)
          < promoted.index_of(frame_step_kind::assemble));
}

TEST_CASE("the ten legacy phase names survive the finer steps", "[frame_plan]") {
    REQUIRE(frame_lap_count() == legacy_names.size());
    for (std::size_t i = 0; i < legacy_names.size(); ++i) {
        CHECK(frame_lap_name(i) == legacy_names[i]);
    }
    const frame_plan plan = default_frame_plan();
    // Every step maps onto one of the ten names, and none of them onto `begin`, which
    // the precondition owns.
    std::array<bool, 10> seen{};
    for (const frame_step& s : plan.steps()) {
        const std::size_t lap = frame_lap_of(s.kind);
        CHECK(lap < legacy_names.size());
        CHECK(lap != 0);
        seen[lap] = true;
    }
    CHECK(seen[0] == false);
    for (std::size_t i = 1; i < seen.size(); ++i) { CHECK(seen[i] == true); }
    // The merged `ui_a` lap: UI composite plus the avatar and vehicle composites.
    CHECK(frame_lap_of(frame_step_kind::ui_composite)
          == frame_lap_of(frame_step_kind::avatar_composite));
    CHECK(frame_lap_of(frame_step_kind::ui_composite)
          == frame_lap_of(frame_step_kind::vehicle_composite));
    CHECK(frame_lap_name(frame_lap_of(frame_step_kind::ui_composite)) == "ui_a");
    // The fused lighting group reports as one legacy phase.
    for (const frame_step_kind k : plan.fused_lighting_members()) {
        CHECK(frame_lap_name(frame_lap_of(k)) == "flush_gather");
    }
}

TEST_CASE("the report carries each step's duration and the step-to-lap mapping", "[frame_plan]") {
    const frame_plan plan = default_frame_plan();
    frame_report report;
    frame_executor exec(plan, report);

    exec.begin_precondition();
    exec.end_precondition();
    std::vector<frame_step_kind> dispatched;
    while (const std::optional<frame_step> step = exec.next_step()) {
        dispatched.push_back(step->kind);
        if (step->kind == frame_step_kind::collector_flush) {
            // Stand in for the fused body: consume the group's members in plan order.
            exec.split(frame_step_kind::gpu_sdf);
            exec.split(frame_step_kind::sky_sun);
            exec.split(frame_step_kind::gi);
            exec.split(frame_step_kind::gi_feedback);
            exec.split(frame_step_kind::rc_readback);
        }
    }
    exec.finish();

    // The loop sees the group head once, never its absorbed members.
    const std::vector<frame_step_kind> heads_only{
        frame_step_kind::build_lighting,     frame_step_kind::collector_flush,
        frame_step_kind::assemble,           frame_step_kind::menu_background,
        frame_step_kind::overlays,           frame_step_kind::ui_composite,
        frame_step_kind::avatar_composite,   frame_step_kind::vehicle_composite,
        frame_step_kind::world_pass,         frame_step_kind::tonemap,
        frame_step_kind::swapchain_composite};
    CHECK(dispatched == heads_only);
    CHECK_FALSE(report.order_violation());

    // The report still accounts for all sixteen steps, in plan order.
    const auto records = report.step_records();
    REQUIRE(records.size() == today_order.size());
    for (std::size_t i = 0; i < records.size(); ++i) {
        CHECK(records[i].kind == today_order[i]);
        CHECK(records[i].status == frame_step_status::run);
        CHECK(records[i].lap == frame_lap_of(today_order[i]));
        CHECK(records[i].duration_ms >= 0.0);
    }
    CHECK(report.precondition().ran);
    CHECK(report.lap(0).steps == 1);
    // `flush_gather` aggregates the six fused members; `ui_a` the three composites.
    CHECK(report.lap(frame_lap_of(frame_step_kind::gpu_sdf)).steps == 6);
    CHECK(report.lap(frame_lap_of(frame_step_kind::ui_composite)).steps == 3);
    CHECK(report.record_for(frame_step_kind::assemble)->lap == 3);
    // Lap aggregation: each legacy phase's total is the sum of its member steps, and
    // the `begin` lap holds the precondition alone.
    for (std::size_t lap = 1; lap < 10; ++lap) {
        double members = 0.0;
        for (const frame_step_record& r : records) {
            if (r.lap == lap) { members += r.duration_ms; }
        }
        CHECK(report.lap_ms(lap) == Catch::Approx(members).margin(1e-9));
    }
    CHECK(report.lap_ms(0) == Catch::Approx(report.precondition().duration_ms));
    CHECK(report.total_step_ms() >= 0.0);
}

TEST_CASE("a split out of plan order is reported, not swallowed", "[frame_plan]") {
    const frame_plan plan = default_frame_plan();
    frame_report report;
    frame_executor exec(plan, report);
    auto drain = [&](frame_step_kind expect) {
        const std::optional<frame_step> s = exec.next_step();
        REQUIRE(s.has_value());
        CHECK(s->kind == expect);
    };
    drain(frame_step_kind::build_lighting);
    drain(frame_step_kind::collector_flush);
    // Skip ahead: the fused body would have jumped the SDF member.
    exec.split(frame_step_kind::gi);
    CHECK(report.order_violation());
    // The plan cursor is unmoved, so the frame still runs the remaining steps.
    drain(frame_step_kind::gpu_sdf);
}

TEST_CASE("a planned skip is recorded with its reason and no duration", "[frame_plan]") {
    frame_plan plan;
    plan.push_back({
        .kind = frame_step_kind::build_lighting,
        .status = frame_step_status::run,
        .flags = 0u,
        .reason = {},
    });
    plan.push_back({
        .kind = frame_step_kind::gi,
        .status = frame_step_status::skip,
        .flags = static_cast<unsigned>(frame_step_flag::fused_lighting),
        .reason = "disabled",
    });
    frame_report report;
    frame_executor exec(plan, report);
    const std::optional<frame_step> first = exec.next_step();
    REQUIRE(first.has_value());
    CHECK(first->kind == frame_step_kind::build_lighting);
    CHECK(exec.next_step() == std::nullopt);
    exec.finish();
    const auto records = report.step_records();
    REQUIRE(records.size() == 2);
    CHECK(records[1].status == frame_step_status::skip);
    CHECK(records[1].reason == "disabled");
    CHECK(records[1].duration_ms == 0.0);
    // A skipped step still occupies its lap slot, contributing no time.
    CHECK(report.lap(frame_lap_of(frame_step_kind::gi)).steps == 1);
    CHECK(report.lap_ms(frame_lap_of(frame_step_kind::gi)) == 0.0);
}
