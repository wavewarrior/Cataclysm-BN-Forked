
#include "coop_mutation_log.h"

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
    using evt = coop_event_type;
    coop_streamable_result result;
    for( auto& ev : events ) {
        const bool streamable =
            ev.type == evt::terrain_changed || ev.type == evt::furniture_changed
            || ev.type == evt::field_created || ev.type == evt::field_changed
            || ev.type == evt::field_expired;
        if( !streamable ) { continue; }
        result.hash = coop_hash_event( result.hash, ev ); // hash BEFORE move
        result.sent.push_back( std::move( ev ) );
    }
    return result;
}

// ---------------------------------------------------------------------------
// Reverse delta — build the inverse of a world-event
// ---------------------------------------------------------------------------

auto reverse_delta( const coop_world_event& ev ) -> coop_world_event
{
    // Determine the inverse event type for the reversed delta.
    // Most types are self-inverse; field_created ↔ field_expired is the compound case.
    auto inverse_type = []( coop_event_type t ) -> coop_event_type {
        switch( t )
    {
        case coop_event_type::field_created:
            return coop_event_type::field_expired;
        case coop_event_type::field_expired:
            return coop_event_type::field_created;
        default:
            return t;
    }
};

coop_world_event rev;
rev.type = inverse_type( ev.type );
    rev.pos = ev.pos;
    rev.value = ev.old_value;
    rev.old_value = ev.value;
    rev.creature_id = ev.creature_id;
    rev.str = ev.str;
    // Store original type so reverse_delta(rev) round-trips back
    rev.reverse_type = ev.type;
    return rev;
}

