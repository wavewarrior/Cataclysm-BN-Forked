#pragma once

#include "input.h" // input_event

#include <functional>

struct mco_coro; // forward-declare minicoro type; defined in coop_fiber.cpp only

/// Wraps one minicoro coroutine representing one open modal.
///
/// **Yield point**: inside `input_context::handle_input()` at the
/// `inp_mngr.get_input_event()` call.  The fiber yields an `input_event`
/// and suspends; the main loop provides the next raw SDL event via
/// `resume(evt)`.  Resolution (`input_to_action`, TIMEOUT, retry loop)
/// continues in the modal's own `input_context` — correct per-context
/// action mapping is therefore preserved.
struct coop_fiber {
        // 512 KiB stack — sufficient for veh_interact + deepest sub-modals.
        static constexpr std::size_t STACK_SIZE = 512u * 1024u;

        explicit coop_fiber( std::function<void()> fn );
        ~coop_fiber();
        coop_fiber( const coop_fiber & ) = delete;
        coop_fiber &operator=( const coop_fiber & ) = delete;

        /// Resume the fiber, making the pending `yield_event()` return `evt`.
        /// Returns when the fiber calls `yield_event()` again or finishes.
        auto resume( const input_event& evt ) -> void;

        /// True once the fiber function has returned.
        auto done() const -> bool;

        // ---- called FROM INSIDE the fiber (inside input_context::handle_input) ----

        /// Replaces `inp_mngr.get_input_event()` when a fiber is active.
        /// Suspends the fiber; returns the event the main loop passed to resume().
        static auto yield_event() -> input_event; // *NOPAD*

        /// True when the current call stack is executing inside a coop_fiber.
        static auto active() -> bool;

    private:
        mco_coro *co_ = nullptr;
        std::function<void()> fn_;

        // Thread-local channel: resume() writes, yield_event() reads.
        static thread_local coop_fiber *active_fiber_;
        static thread_local input_event pending_event_;

        static auto entry_( mco_coro* co ) -> void;
};

