#ifdef COOP_ENABLED

#include "coop_rollback.h"
#include "calendar.h"
#include "coop_mutation_log.h"
#include "field_type.h"
#include "field.h"
#include "game.h"
#include "map.h"

#include <algorithm>

coop_rollback_engine::coop_rollback_engine( int capacity )
    : capacity_( capacity )
{
}

auto coop_rollback_engine::push( int tick, const coop_world_event& ev ) -> void
{
    entries_.push_back( {tick, ev} );
    while( static_cast<int>( entries_.size() ) > capacity_ ) {
        entries_.pop_front();
    }
}

auto coop_rollback_engine::rollback_to( int target_tick ) -> int
{
    // Find the split point: everything with tick > target_tick gets reversed.
    auto it = entries_.rbegin();
    int count = 0;

    for( ; it != entries_.rend(); ++it ) {
        if( it->tick <= target_tick ) {
            break;
        }
        // Build the inverse delta and apply it to the world.
        coop_world_event rev = reverse_delta( it->event );
        const tripoint_bub_ms bpos = g->m.abs_to_bub( rev.pos );

        using evt = coop_event_type;
        if( rev.type == evt::terrain_changed ) {
            g->m.ter_set( bpos, ter_id{ rev.value } );
        } else if( rev.type == evt::furniture_changed ) {
            g->m.furn_set( bpos, furn_id{ rev.value } );
        } else if( rev.type == evt::field_created ) {
            // Reverse of field_created is field_expired — remove the field.
            g->m.remove_field( bpos, field_type_id{ rev.value } );
        } else if( rev.type == evt::field_expired ) {
            // Reverse of field_expired is field_created — restore the field.
            const field_type_id ftype{ rev.value };
            const int intensity = rev.creature_id > 0 ? rev.creature_id : 1;
            if( ftype ) {
                g->m.add_field( bpos, ftype, intensity, 0_turns );
            }
        } else if( rev.type == evt::field_changed ) {
            // Restore old intensity.
            const field_type_id ftype{ rev.value };
            if( ftype ) {
                auto* fe = g->m.get_field( bpos ).find_field( ftype );
                if( fe ) {
                    fe->set_field_intensity( rev.creature_id );
                }
            }
        }

        ++count;
    }

    // Erase the rolled-back entries from the back of the deque.
    // rbegin distance gives us how many to pop.
    for( int i = 0; i < count; ++i ) {
        entries_.pop_back();
    }

    return count;
}

#endif // COOP_ENABLED