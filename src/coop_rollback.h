#pragma once
#ifdef COOP_ENABLED

#include "coop_mutation_log.h"

#include <cstddef>
#include <deque>

/// Stores a rolling window of world events and supports reversing them
/// back to a given tick boundary.  Used by the client to undo locally-
/// applied deltas when a hash mismatch is detected.
struct coop_rollback_entry {
    int tick = 0;
    coop_world_event event;
};

struct coop_rollback_engine {
        /// Construct with a maximum ring-buffer capacity (default 100 events).
        explicit coop_rollback_engine( int capacity = 100 );

        /// Record an event for potential rollback.
        auto push( int tick, const coop_world_event& ev ) -> void;

        /// Reverse all events newer than `target_tick`, applying their inverse
        /// deltas to the world.  Returns the number of events rolled back.
        auto rollback_to( int target_tick ) -> int;

        /// Current number of entries in the buffer.
        auto size() const -> std::size_t { return entries_.size(); }

    private:
        std::deque<coop_rollback_entry> entries_;
        int capacity_;
};

#endif // COOP_ENABLED