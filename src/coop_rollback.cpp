
#include "coop_rollback.h"

#include "coop_world_event_interpreter.h"
#include "map.h"

#include <algorithm>

coop_rollback_engine::coop_rollback_engine( int capacity )
    : capacity_( capacity )
{
}

auto coop_rollback_engine::push( int tick, const coop_recorded_event& recorded ) -> void
{
    entries_.push_back( {tick, recorded} );
    while( static_cast<int>( entries_.size() ) > capacity_ ) {
        entries_.pop_front();
    }
}

auto coop_rollback_engine::rollback_to( map& target, int target_tick ) -> int
{
    // Reverse newest-first, stopping at the boundary; each entry is popped as it
    // is reversed, so a second rollback can never double-invert it.
    int count = 0;
    while( !entries_.empty() && entries_.back().tick > target_tick ) {
        coop_world_event_interpreter::apply_inverse(
            target, coop_world_event_interpreter::invert( entries_.back().event ) );
        entries_.pop_back();
        ++count;
    }
    return count;
}
