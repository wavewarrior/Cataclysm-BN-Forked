
#include "coop_mutation_log.h"
#include "coop_world_event_interpreter.h"

// ---------------------------------------------------------------------------
// Thread-local singleton
// ---------------------------------------------------------------------------

namespace
{
thread_local coop_mutation_log *tl_current_log = nullptr;
} // namespace

auto coop_mutation_log::current() -> coop_mutation_log* { return tl_current_log; }

// ---------------------------------------------------------------------------
// Event buffer
// ---------------------------------------------------------------------------

auto coop_mutation_log::push( coop_world_event e ) -> void
{
    // Events with str content (creature_spawned, creature_hp) need the extended
    // hash so that mtype_id strings are included in the integrity check.
    if( e.type == coop_event_type::creature_spawned
        || e.type == coop_event_type::creature_hp ) {
        running_hash_ = coop_hash_event_extended( running_hash_, e );
    } else {
        running_hash_ = coop_hash_event( running_hash_, e );
    }
    events_.push_back( std::move( e ) );
}

auto coop_mutation_log::flush() -> std::vector<coop_world_event>
{
    std::vector<coop_world_event> out;
    out.swap( events_ );
    running_hash_ = COOP_FNV_OFFSET; // reset to FNV offset basis
    return out;
}

// ---------------------------------------------------------------------------
// RAII guard
// ---------------------------------------------------------------------------

coop_tick_log_guard::coop_tick_log_guard() { tl_current_log = &log_; }

coop_tick_log_guard::~coop_tick_log_guard() { tl_current_log = nullptr; }

// ---------------------------------------------------------------------------
// A4 delta: streamable event collection + hash
// ---------------------------------------------------------------------------

auto coop_collect_streamable( std::vector<coop_world_event> events ) -> coop_streamable_result
{
    coop_streamable_result result;
    for( auto& ev : events ) {
        if( !coop_world_event_interpreter::is_replicated( ev.type ) ) { continue; }
        result.hash = coop_hash_event( result.hash, ev ); // hash BEFORE move
        result.sent.push_back( std::move( ev ) );
    }
    return result;
}
