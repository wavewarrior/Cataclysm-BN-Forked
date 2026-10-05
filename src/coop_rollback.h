#pragma once

#include "coop_world_event_interpreter.h"

#include <cstddef>
#include <deque>

class map;

/// Stores a rolling window of recorded World events and supports reversing them
/// back to a given tick boundary.  Used by the client to undo locally-applied
/// deltas when a hash mismatch is detected.
///
/// Entries are the interpreter's records, so inverting one never has to guess
/// what a slot meant; all per-type knowledge lives in the interpreter.
struct coop_rollback_entry {
    int tick = 0;
    coop_recorded_event event;
};

struct coop_rollback_engine {
        /// Construct with a maximum ring-buffer capacity (default 100 events).
        explicit coop_rollback_engine( int capacity = 100 );

        /// Record an applied event for potential rollback.
        auto push( int tick, const coop_recorded_event& recorded ) -> void;

        /// Reverse all events newer than `target_tick`, delegating the inverse of
        /// each to the World event interpreter.  Returns the number of events
        /// rolled back.
        auto rollback_to( map& target, int target_tick ) -> int;

        /// Current number of entries in the buffer.
        auto size() const -> std::size_t { return entries_.size(); }

    private:
        std::deque<coop_rollback_entry> entries_;
        int capacity_;
};
