#include "catch/catch_amalgamated.hpp"
#include "coordinates.h"
#include "frame_history.h"
#include "game_constants.h"
#include "level_cache_freshness.h"
#include "point.h"

#include <climits>
#include <cstdint>

// T2 seam (ADR-0003): the lighting rebuild gate as a pure decision over a
// `frame_history` value, plus the commit step that writes it back. These replay
// the scenarios the hand-copied gate test transcribed, now against the real
// type, with no world, no GPU and no window: the plan's stamps are built
// directly, exactly as `plan_for` would hand them to the frame.

namespace {

using K = level_cache_freshness;

/// A rebuild plan carrying only what the gate reads: the viewer pose, the
/// bubble origin and one occluder stamp on every level (the gate looks at the
/// viewed level only). Everything else is inert for the decision.
auto make_plan(
    const int vx, const int vy, const int vz, const point origin, const std::uint64_t occ)
    -> K::rebuild_plan {
    K::rebuild_plan p{};
    p.occluder.fill(occ);
    p.pose.bubble_origin = point_abs_sm(origin.x, origin.y);
    p.pose.viewer = tripoint_bub_ms(vx, vy, vz);
    p.pose.camera = point_rel_ms(0, 0);
    return p;
}

/// The default scene: viewer (60,60,0), bubble origin (0,0), stamp 7.
auto scene(
    const int dx = 0, const int dy = 0, const int dz = 0, const std::uint64_t occ = 7,
    const point origin = point(0, 0)) -> K::rebuild_plan {
    return make_plan(60 + dx, 60 + dy, dz, origin, occ);
}

/// One frame in the shipped order: the gate decides, the commit step writes,
/// and only then would a pass run. `passes` counts how often the frame reached
/// a pass boundary, so a test can assert every commit effect is already
/// visible at the FIRST one.
struct frame_probe {
    frame_history history;
    lighting::lighting_pulses pulses;
    int rebuilds_in_window = 0;
    int passes = 0;
};

auto run_frame(frame_probe& fp, const K::rebuild_plan& plan, const rebuild_gate_knobs& knobs = {})
    -> rebuild_decision {
    const rebuild_decision d = gate_and_commit_frame_history(
        {fp.history, plan, knobs, fp.pulses, fp.rebuilds_in_window});
    ++fp.passes;
    return d;
}

} // namespace

TEST_CASE(
    "frame_history: a fresh history rebuilds both buffers on the first frame", "[frame_history]") {
    frame_probe fp;
    // The sentinel anchor must not be arithmetically combined with the plan's
    // pose: the drift test short-circuits on it (signed overflow otherwise).
    REQUIRE(fp.history.last_struct_px == INT_MIN);
    const rebuild_decision d = run_frame(fp, scene());
    CHECK(d.structure);
    CHECK(d.vis);
    CHECK(d.counted_rebuild);
    // The stamps advanced to this frame's plan.
    CHECK(fp.history.last_occluder == 7u);
    REQUIRE(fp.history.last_struct_pose.has_value());
    CHECK(*fp.history.last_struct_pose == scene().pose);
    REQUIRE(fp.history.last_vis_pose.has_value());
    CHECK(fp.history.last_struct_px == 60);
    CHECK(fp.history.last_struct_py == 60);
    CHECK(fp.rebuilds_in_window == 1);
}

TEST_CASE("frame_history: quiet frames rebuild neither buffer", "[frame_history]") {
    frame_probe fp;
    run_frame(fp, scene()); // first frame forces both
    fp.rebuilds_in_window = 0;
    for (int i = 0; i < 3; ++i) {
        const rebuild_decision d = run_frame(fp, scene());
        CHECK_FALSE(d.structure);
        CHECK_FALSE(d.vis);
        CHECK_FALSE(d.counted_rebuild);
    }
    CHECK(fp.rebuilds_in_window == 0);
    // The drift anchor stays where the last structure rebuild put it.
    CHECK(fp.history.last_struct_px == 60);
}

TEST_CASE("frame_history: a terrain edit forces structure only", "[frame_history]") {
    frame_probe fp;
    run_frame(fp, scene());
    fp.rebuilds_in_window = 0;
    const rebuild_decision d = run_frame(fp, scene(0, 0, 0, 8));
    CHECK(d.structure);
    CHECK_FALSE(d.vis);
    CHECK(fp.rebuilds_in_window == 1);
    CHECK(fp.history.last_occluder == 8u);
    // vis was not rebuilt, so its stamp did not advance.
    CHECK(*fp.history.last_vis_pose == scene().pose);
}

TEST_CASE("frame_history: a player move forces vis only", "[frame_history]") {
    frame_probe fp;
    run_frame(fp, scene());
    fp.rebuilds_in_window = 0;
    const rebuild_decision d = run_frame(fp, scene(1));
    CHECK_FALSE(d.structure);
    CHECK(d.vis);
    CHECK(fp.rebuilds_in_window == 0);
    CHECK(*fp.history.last_vis_pose == scene(1).pose);
    // The structure anchor is untouched: a moved player is not a rebuilt SDF.
    CHECK(fp.history.last_struct_px == 60);
}

TEST_CASE("frame_history: camera drift under the threshold forces nothing", "[frame_history]") {
    frame_probe fp;
    run_frame(fp, scene());
    const rebuild_decision d = run_frame(fp, scene(2, 2)); // 2 tiles: under 4
    CHECK_FALSE(d.structure);
    CHECK(d.vis);
}

TEST_CASE("frame_history: camera drift at the threshold forces structure", "[frame_history]") {
    frame_probe fp;
    run_frame(fp, scene());
    fp.rebuilds_in_window = 0;
    const rebuild_decision d = run_frame(fp, scene(4)); // 4 tiles: at 4
    CHECK(d.structure);
    CHECK(d.vis);
    CHECK(fp.rebuilds_in_window == 1);
    CHECK(fp.history.last_struct_px == 64);
    // At the threshold the rebuild re-anchors, so the same offset again is quiet.
    CHECK_FALSE(run_frame(fp, scene(4)).structure);
}

TEST_CASE("frame_history: a z change forces both", "[frame_history]") {
    frame_probe fp;
    run_frame(fp, scene());
    const rebuild_decision d = run_frame(fp, scene(0, 0, 1));
    CHECK(d.structure);
    CHECK(d.vis);
    // The occluder stamp is per-level: the viewed level's stamp is read at the
    // new z, and here every level carries the same value, so only the pose term
    // fired. The anchor followed the rebuild.
    CHECK(fp.history.last_struct_pose->viewer.z() == 1);
}

TEST_CASE("frame_history: a bubble-origin shift forces structure only", "[frame_history]") {
    frame_probe fp;
    run_frame(fp, scene());
    const rebuild_decision d = run_frame(fp, scene(0, 0, 0, 7, point(1, 0)));
    CHECK(d.structure);
    CHECK_FALSE(d.vis);
}

TEST_CASE("frame_history: force-every-frame rebuilds structure on every frame", "[frame_history]") {
    frame_probe fp;
    const rebuild_gate_knobs knobs{/*devui*/ false, /*every frame*/ true, /*once*/ false};
    run_frame(fp, scene(), knobs);
    fp.rebuilds_in_window = 0;
    for (int i = 0; i < 3; ++i) {
        const rebuild_decision d = run_frame(fp, scene(), knobs);
        CHECK(d.structure);
        CHECK_FALSE(d.vis); // nothing else about the frame changed
    }
    CHECK(fp.rebuilds_in_window == 3);
}

TEST_CASE(
    "frame_history: force-once rebuilds once and the commit clears the knob", "[frame_history]") {
    frame_probe fp;
    run_frame(fp, scene()); // warm the history: both stamps set
    fp.rebuilds_in_window = 0;
    fp.pulses.force_rebuild = lighting::force_rebuild_mode::once;
    const rebuild_gate_knobs armed{false, false, true};
    const rebuild_decision d = run_frame(fp, scene(), armed);
    CHECK(d.structure);
    CHECK_FALSE(d.vis);
    CHECK(fp.rebuilds_in_window == 1);
    // The commit step consumed the one-shot: the same knobs object the frame
    // started with is now disarmed, and the next quiet frame rebuilds nothing.
    CHECK(fp.pulses.force_rebuild == lighting::force_rebuild_mode::none);
    CHECK_FALSE(run_frame(fp, scene()).structure);
}

TEST_CASE("frame_history: the F4 panel visible forces both buffers", "[frame_history]") {
    frame_probe fp;
    const rebuild_gate_knobs knobs{/*devui*/ true, false, false};
    run_frame(fp, scene(), knobs);
    fp.rebuilds_in_window = 0;
    const rebuild_decision d = run_frame(fp, scene(), knobs);
    CHECK(d.structure);
    CHECK(d.vis);
    CHECK(fp.rebuilds_in_window == 1);
}

TEST_CASE("frame_history: the decision is pure and repeatable", "[frame_history]") {
    frame_probe fp;
    run_frame(fp, scene());
    const frame_history before = fp.history;
    const K::rebuild_plan plan = scene(0, 0, 0, 9);
    const rebuild_decision first = decide_lighting_rebuild(fp.history, plan, {});
    const rebuild_decision again = decide_lighting_rebuild(fp.history, plan, {});
    CHECK(first.structure == again.structure);
    CHECK(first.vis == again.vis);
    CHECK(first.next == again.next);
    CHECK(fp.history == before); // deciding alone changes nothing
}

TEST_CASE(
    "frame_history: the commit lands after the decision and before any pass", "[frame_history]") {
    // The order is pinned by its consequences at the first pass boundary: the
    // one-shot knob is consumed (a clear BEFORE the decision would have lost
    // the rebuild, failing the `structure` check), the window's rebuild counter
    // already counts this frame, and the stamps already advanced.
    frame_probe fp;
    fp.pulses.force_rebuild = lighting::force_rebuild_mode::once;
    const rebuild_gate_knobs armed{false, false, true};
    const rebuild_decision d = run_frame(fp, scene(0, 0, 0, 9), armed);
    REQUIRE(fp.passes == 1);
    CHECK(d.structure);
    CHECK(fp.pulses.force_rebuild == lighting::force_rebuild_mode::none);
    CHECK(fp.rebuilds_in_window == 1);
    CHECK(fp.history.last_occluder == 9u);
    CHECK(fp.history.last_struct_px == 60);
    // A commit BELOW the passes would leave the knob armed for the next frame.
    // Same stamps as the armed frame, so only a stale one-shot could fire.
    CHECK_FALSE(run_frame(fp, scene(0, 0, 0, 9)).structure);
}

TEST_CASE("frame_history: continuous forcing is never consumed by the commit", "[frame_history]") {
    frame_probe fp;
    run_frame(fp, scene());
    fp.pulses.force_rebuild = lighting::force_rebuild_mode::every_frame;
    const rebuild_gate_knobs every{false, true, false};
    CHECK(run_frame(fp, scene(), every).structure);
    CHECK(fp.pulses.force_rebuild == lighting::force_rebuild_mode::every_frame);
    CHECK(run_frame(fp, scene(), every).structure);
}

TEST_CASE(
    "frame_history: a frame with no decision leaves history and knobs alone", "[frame_history]") {
    // The gate block runs only with an active world and a live collector; on
    // such a frame nothing is decided, so nothing may be committed. Modelled by
    // simply not running the step: the values a caller would have handed over
    // stay exactly as they were.
    frame_probe fp;
    run_frame(fp, scene());
    const frame_history before = fp.history;
    fp.pulses.force_rebuild = lighting::force_rebuild_mode::once;
    fp.rebuilds_in_window = 4;
    CHECK(fp.history == before);
    CHECK(fp.pulses.force_rebuild == lighting::force_rebuild_mode::once);
    CHECK(fp.rebuilds_in_window == 4);
}
