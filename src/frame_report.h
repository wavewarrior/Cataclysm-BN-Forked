#pragma once
#ifndef CATA_SRC_FRAME_REPORT_H
#define CATA_SRC_FRAME_REPORT_H

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

#include "frame_plan.h"

/// What one planned step did, as executed.
struct frame_step_record {
    frame_step_kind kind = frame_step_kind::build_lighting;
    /// What actually happened: a planned `run` step that was dispatched is `run`; a
    /// step the plan skipped stays `skip` and costs nothing.
    frame_step_status status = frame_step_status::run;
    std::string_view reason;
    double duration_ms = 0.0;
    /// Legacy phase this step reports under (`frame_lap_of`).
    std::size_t lap = 0;
};

/// Accumulated cost of one legacy phase over the steps that report under it.
struct frame_lap_timing {
    std::size_t steps = 0;
    double sum_ms = 0.0;
    double max_ms = 0.0;
};

/// The frame's precondition step, timed outside the plan: `begin_frame`, which
/// acquires the drawable. Reported under the legacy `begin` lap.
struct frame_precondition_timing {
    bool ran = false;
    double duration_ms = 0.0;
};

/// What a frame did: per-step status, reason and duration, aggregated into the ten
/// legacy phase timings the `[render][perf]` logger publishes. Produced by the
/// executor; the 120-frame window in `refresh_display` reads it rather than owning
/// phase timing itself.
class frame_report
{
    public:
        void add_step( frame_step_record record );
        void set_precondition( double duration_ms );
        /// The fused lighting body split out of plan order: the executed order drifted
        /// from the asserted one. Recorded, never fatal.
        void note_order_violation();
        bool order_violation() const {
            return order_violation_;
        }

        auto precondition() const -> const frame_precondition_timing & {
            return precondition_;
        }
        auto step_records() const -> std::span<const frame_step_record> {
            return std::span<const frame_step_record>( records_.data(), record_count_ );
        }
        auto lap( std::size_t index ) const -> const frame_lap_timing & {
            return laps_[index];
        }
        auto laps() const -> std::span<const frame_lap_timing> {
            return std::span<const frame_lap_timing>( laps_.data(), laps_.size() );
        }
        /// Cost of one legacy phase in milliseconds: the precondition lap `begin`
        /// reports its own timing, every other lap sums its member steps.
        auto lap_ms( std::size_t index ) const -> double;
        /// Sum of every recorded step's duration: the CPU work the frame's steps cost.
        auto total_step_ms() const -> double;
        /// Record for `kind`, or nullptr when the step was not in the executed plan.
        auto record_for( frame_step_kind kind ) const -> const frame_step_record * ; // *NOPAD*

    private:
        static constexpr std::size_t max_records_ = 24;
        std::array<frame_step_record, max_records_> records_;
        std::size_t record_count_ = 0;
        std::array<frame_lap_timing, 10> laps_;
        frame_precondition_timing precondition_;
        bool order_violation_ = false;
};

#endif // CATA_SRC_FRAME_REPORT_H
