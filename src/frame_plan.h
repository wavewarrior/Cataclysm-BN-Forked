#pragma once
#ifndef CATA_SRC_FRAME_PLAN_H
#define CATA_SRC_FRAME_PLAN_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// The frame plan: the ordered list of named steps the render frame runs, as a pure
// value. Built by `default_frame_plan()` (every step runs, in today's phase order)
// and executed by `frame_executor` in `sdl_render_frame.cpp`, which times each step
// into a `frame_report`.
//
// Pure by construction: no windowing, GPU or game includes here, so `cata_test-tiles`
// can assert step order and the ordering laws without a GPU (ADR-0003).
//
// Later tickets make the plan conditional (skip reasons for SDF, sky, GI, the world
// half). In this ticket the plan decides nothing but order, and `begin_frame` stays a
// precondition outside the plan.

/// A step of the frame, in execution order. The lighting steps are the sub-blocks of
/// the fused `flush_and_gather_rc` body, so the plan's lighting order is the order the
/// fused body really dispatches; the executor's timer rejects a split out of order.
enum class frame_step_kind : std::uint8_t {
    build_lighting,
    collector_flush,
    gpu_sdf,
    sky_sun,
    gi,
    gi_feedback,
    rc_readback,
    assemble,
    menu_background,
    overlays,
    ui_composite,
    avatar_composite,
    vehicle_composite,
    world_pass,
    tonemap,
    swapchain_composite,
};

/// Why a step is planned. `run` is the only status this ticket produces; the others
/// exist so a skip reason is a first-class field of the plan from the ticket that
/// starts skipping.
enum class frame_step_status : std::uint8_t {
    run,
    skip,
};

/// A step's role in the frame's bookkeeping.
enum class frame_step_flag : std::uint8_t {
    /// The step begins a graphics render pass (`SDL_BeginGPURenderPass`, directly or
    /// through a batcher). The lighting steps begin compute passes only and are
    /// deliberately excluded: the law that `assemble` precedes every pass-beginning
    /// step is the "begin_lighting_frame before begin_pass" rule, which is about the
    /// lit sprite passes, not about compute dispatches.
    begins_render_pass = 1 << 0,
    /// The step is a member of the fused lighting group dispatched by
    /// `flush_and_gather_rc`, so the executor does not time it as its own lap.
    fused_lighting = 1 << 1,
};

/// One planned step.
struct frame_step {
    frame_step_kind kind = frame_step_kind::build_lighting;
    frame_step_status status = frame_step_status::run;
    unsigned flags = 0;
    /// Skip reason; empty while the step runs.
    std::string_view reason;

    constexpr auto has( frame_step_flag f ) const -> bool {
        return ( flags & static_cast<unsigned>( f ) ) != 0u;
    }
};

/// An ordered list of steps. A value; copyable; nothing in it reaches outside itself.
class frame_plan
{
    public:
        auto steps() const -> std::span<const frame_step> {
            return std::span<const frame_step>( steps_.data(), size_ );
        }
        auto size() const -> std::size_t {
            return size_;
        }
        bool empty() const {
            return size_ == 0;
        }
        /// Index of `kind`, or `size()` when the plan has no such step.
        auto index_of( frame_step_kind kind ) const -> std::size_t;
        auto step_at( std::size_t index ) const -> const frame_step & {
            return steps_[index];
        }
        auto step( frame_step_kind kind ) const -> const frame_step * ; // *NOPAD*

        /// Builder-style append, used by `default_frame_plan` and by tests that build
        /// a hypothetical order to check a law against.
        auto push_back( frame_step step ) -> frame_plan & ; // *NOPAD*
        /// Members of the fused lighting group, in plan order.
        auto fused_lighting_members() const -> std::array<frame_step_kind, 6>;

    private:
        std::array<frame_step, 24> steps_;
        std::size_t size_ = 0;
};

/// Today's frame, as a plan: every step runs, in the order `refresh_display` has always
/// used. The single source of the executed order.
auto default_frame_plan() -> frame_plan;

/// The ten legacy phase names, in lap order — the `[render][perf][phase avg/max ms]`
/// vocabulary the recorded baseline cites.
auto frame_lap_names() -> std::span<const std::string_view>;
auto frame_lap_count() -> std::size_t;

/// The lap a step reports under, so the ten names survive the finer steps.
auto frame_lap_of( frame_step_kind kind ) -> std::size_t;
auto frame_lap_name( std::size_t lap ) -> std::string_view;

#endif // CATA_SRC_FRAME_PLAN_H
