#pragma once
#ifndef CATA_SRC_FRAME_HISTORY_H
#define CATA_SRC_FRAME_HISTORY_H

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <optional>

#include "level_cache_freshness.h"

// Previous-frame gate state for the lighting rebuild decision (T2 of the
// frame-assembly module, ADR decision 1 of #59): the five stamps the gate in
// `build_lighting` used to keep as function-local statics, as a header-only
// value owned by `render_state` (the buffers they describe live there, so they
// share a lifetime). Copied into `frame_inputs` by the builder (T4); the
// decision below is pure, and `commit_frame_history` right after planning —
// before any pass runs — writes the stamps, clears the force-once knob and
// bumps the rebuild counter in exactly the order the statics were written in.
//
// Deliberately NOT in history (owners unchanged): GI feedback pending
// iterations, `render_state::last_frame_inputs_`, `sdf_pass::populated_`, the
// splat LRU, the target dirty flags, the perf accumulators. There is no reset
// on world change in the migration (filed as issue #125).

/// The five previous-frame stamps the lighting rebuild gate keeps. Fresh by
/// construction: an unset pose pair and the sentinel drift anchor reproduce
/// the very first frame (both buffers rebuild).
struct frame_history {
    /// Pose the structure buffers (SDF, sun_sdf, sky_vis) were last rebuilt for.
    std::optional<level_cache_freshness::pose_stamps> last_struct_pose;
    /// Pose the FOV visibility mask was last rebuilt for.
    std::optional<level_cache_freshness::pose_stamps> last_vis_pose;
    /// Occluder-set stamp the SDF was last baked against (viewed level).
    std::uint64_t last_occluder = 0;
    /// Camera-drift anchor: the viewer tile of the last structure rebuild.
    /// `INT_MIN` is the "never rebuilt" sentinel; the drift test short-circuits
    /// on it so `std::abs( px - INT_MIN )` never evaluates (signed overflow).
    int last_struct_px = INT_MIN;
    int last_struct_py = INT_MIN;

    auto operator==( const frame_history & ) const -> bool = default;
};

/// The gate knobs the decision reads. Snapshot semantics, exactly as the gate
/// read them today: `devui_visible` is polled once per decision (the two reads
/// at the old :255/:264 cannot differ because input is not pumped mid-frame),
/// and the force flags are the values the input path left behind.
struct rebuild_gate_knobs {
    /// F4 dev panel open: forces both buffers every frame.
    bool devui_visible = false;
    /// Knob `force_rc_rebuild 1`: force the structure rebuild every frame.
    bool force_every_frame = false;
    /// Knob `force_rc_rebuild 2`: force exactly one structure rebuild; the
    /// commit step clears it (the clear is part of the knob's contract).
    bool force_once = false;
};

/// The gate's verdict for one frame, plus the history the NEXT frame starts
/// from. Pure function of (history, plan stamps, knobs): no argument is
/// modified, so the plan can be rebuilt without disturbing the frame it
/// describes.
struct rebuild_decision {
    /// Flags for `lighting::build_and_submit_lighting`, in its field order.
    bool structure = false;
    bool vis = false;
    /// History for the next frame: the input history with the stamps the
    /// verdict advanced.
    frame_history next;
    /// The structure rebuild fired this frame: the commit step bumps the
    /// `[render][perf] rebuilds=k/n` counter by this.
    bool counted_rebuild = false;
};

/// Viewer-tile drift past the tolerance since the last structure rebuild. The
/// sentinel check comes FIRST: without the short-circuit, `px - INT_MIN` is
/// signed overflow on the first frame.
inline auto frame_cam_drifted( const frame_history &hist, int px, int py ) -> bool
{
    constexpr int SDF_CAM_DRIFT_TILES = 4;
    return hist.last_struct_px == INT_MIN
           || std::abs( px - hist.last_struct_px ) >= SDF_CAM_DRIFT_TILES
           || std::abs( py - hist.last_struct_py ) >= SDF_CAM_DRIFT_TILES;
}

/// The lighting rebuild gate, as a pure decision. Transcribes the former
/// `build_lighting` gate block verbatim:
/// - structure: panel open, either force knob, an occluder-set change on the
///   viewed level, a bubble-origin/z pose shift, or camera drift past
///   `SDF_CAM_DRIFT_TILES`;
/// - vis: panel open, or the viewer pose differs from the last vis rebuild.
/// The stamp ADVANCES (inside `next`) follow the verdicts, as today.
inline auto decide_lighting_rebuild( const frame_history &hist,
                                     const level_cache_freshness::rebuild_plan &plan,
                                     const rebuild_gate_knobs &knobs ) -> rebuild_decision
{
    rebuild_decision d;
    d.next = hist;
    const int z = plan.pose.viewer.z();
    const std::uint64_t gen =
        plan.occluder[static_cast<std::size_t>( z + OVERMAP_DEPTH )];
    const int px = plan.pose.viewer.x();
    const int py = plan.pose.viewer.y();

    const bool pose_shifted = !hist.last_struct_pose
                              || hist.last_struct_pose->bubble_origin != plan.pose.bubble_origin
                              || hist.last_struct_pose->viewer.z() != plan.pose.viewer.z();
    d.structure = knobs.devui_visible || knobs.force_every_frame || knobs.force_once
                  || gen != hist.last_occluder || pose_shifted
                  || frame_cam_drifted( hist, px, py );
    d.vis = knobs.devui_visible
            || !hist.last_vis_pose
            || hist.last_vis_pose->viewer != plan.pose.viewer;

    d.counted_rebuild = d.structure;
    if( d.structure ) {
        d.next.last_occluder = gen;
        d.next.last_struct_pose = plan.pose;
        d.next.last_struct_px = px;
        d.next.last_struct_py = py;
    }
    if( d.vis ) {
        d.next.last_vis_pose = plan.pose;
    }
    return d;
}

/// Options for the commit step (more than three operands, so a struct).
/// The counters are passed by reference because their owners stay where they
/// are: the one-shot knob is a knob global until the settings ticket, and the
/// rebuild counter is the TU-static behind the `[render][perf]` line.
struct frame_history_commit_args {
    frame_history &history;
    const rebuild_decision &decision;
    bool &force_once;
    int &rebuilds_in_window;
};

/// The commit step, run right after planning and before any pass: clears the
/// force-once knob, bumps the window's rebuild counter and writes the decided
/// stamps into the live history — in exactly the order the writes happened at
/// the old :258-294 (one-shot clear, then the counter, then the stamps).
inline auto commit_frame_history( const frame_history_commit_args &args ) -> void
{
    const rebuild_decision &d = args.decision;
    args.force_once = false;
    if( d.counted_rebuild ) {
        ++args.rebuilds_in_window;
    }
    args.history = d.next;
}

/// Options for the shipped decide-then-commit order (more than three
/// operands, so a struct). `knobs` is a snapshot taken BEFORE the commit, so
/// the decision sees the one-shot knob and the commit clears its storage.
struct frame_history_gate_args {
    frame_history &history;
    const level_cache_freshness::rebuild_plan &plan;
    rebuild_gate_knobs knobs;
    bool &force_once;
    int &rebuilds_in_window;
};

/// The shipped order in one call: the pure decision, then the commit step —
/// which is "right after planning, before the passes" wherever the frame
/// calls it. Observable consequence of the order: a frame armed with
/// `force_once` rebuilds AND leaves the knob disarmed; a clear that ran
/// before the decision would lose the rebuild.
inline auto gate_and_commit_frame_history( const frame_history_gate_args &args )
-> rebuild_decision   // *NOPAD*
{
    const rebuild_decision d = decide_lighting_rebuild( args.history, args.plan, args.knobs );
    commit_frame_history( { args.history, d, args.force_once, args.rebuilds_in_window } );
    return d;
}

#endif // CATA_SRC_FRAME_HISTORY_H
