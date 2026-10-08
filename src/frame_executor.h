#pragma once
#ifndef CATA_SRC_FRAME_EXECUTOR_H
#define CATA_SRC_FRAME_EXECUTOR_H

#include <chrono>
#include <optional>

#include "frame_plan.h"
#include "frame_report.h"

/// The one loop the frame runs its plan through.
///
/// Usage (see `refresh_display`): time the `begin_frame` precondition, then pull steps
/// with `next_step()` and dispatch on `kind`. `next_step()` closes the step before it,
/// so the loop's own boundaries are the timings — the asserted order is the executed
/// order by construction. Inside the fused lighting body, `split()` marks the
/// boundaries of the member steps and refuses a split that is not the next planned
/// member, so the lighting order is guarded too.
class frame_executor
{
    public:
        using clock = std::chrono::steady_clock;
        /// Milliseconds between a start point and `now`, the unit the report stores.
        static auto elapsed_ms( clock::time_point now, clock::time_point start ) -> double;
        frame_executor( const frame_plan &plan, frame_report &report );

        /// Starts/stops the `begin_frame` precondition timing (legacy `begin` lap).
        void begin_precondition();
        void end_precondition();

        /// Next step to run, or nullopt when the plan is exhausted. Skipped steps are
        /// recorded with their reason and zero duration and not returned.
        auto next_step() -> std::optional<frame_step>;
        /// Boundary inside the fused lighting group: closes the member before `kind`
        /// and opens `kind`. A `kind` out of plan order is recorded as a violation.
        void split( frame_step_kind kind );
        /// Closes the last open step. Safe to call twice.
        void finish();

    private:
        void close_open( clock::time_point now );

        const frame_plan &plan_;
        frame_report &report_;
        std::size_t index_ = 0;
        std::optional<frame_step> open_;
        clock::time_point open_at_{};
        clock::time_point precondition_at_{};
        bool precondition_open_ = false;
};

#endif // CATA_SRC_FRAME_EXECUTOR_H
