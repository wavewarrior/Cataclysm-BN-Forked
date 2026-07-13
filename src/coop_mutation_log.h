#pragma once
#ifdef COOP_ENABLED

#include "coop_proto.h"
#include "coordinates.h"

#include <cstdint>
#include <string>
#include <vector>

/// A single world-state mutation that occurred during one game tick.
/// Serialised into the A4 delta packet; consumed by the client to apply
/// incremental changes instead of re-sending the full 5×5 submap blast.
struct coop_world_event {
    coop_event_type type = coop_event_type::terrain_changed;
    tripoint_abs_ms pos;
    int value = 0;       ///< ter_id, furn_id, field intensity, hp, …
    int creature_id = 0; ///< stable host-assigned monster/npc id
    std::string str;     ///< mtype_id for creature_spawned; empty otherwise
};

/// FNV-1a offset basis — initial value for per-tick event hash accumulators.
static constexpr uint64_t COOP_FNV_OFFSET = 0xcbf29ce484222325ULL;

/// Mix one 64-bit value into an FNV-1a accumulator.  Pure; no side effects.
/// Canonical shared implementation used by coop_server (build_and_send_sync)
/// and coop_client (apply_sync) — a single site prevents hash-drift bugs.
constexpr auto coop_fnv1a_mix( uint64_t h, uint64_t v ) -> uint64_t
{
    return ( h ^ v ) * 0x00000100000001B3ULL;
}

/// Hash one event's 6 fields in the canonical wire order (type,x,y,z,value,cid).
/// creature_id is ALWAYS mixed even when 0 — the server omits "cid" from JSON
/// when zero but the hash must include it; mix(0) is NOT a no-op (prime multiply).
/// Server calls this after flushing the mutation log.
constexpr auto coop_hash_event( uint64_t h, const coop_world_event& ev ) -> uint64_t
{
    h = coop_fnv1a_mix( h, static_cast<uint64_t>( ev.type ) );
    h = coop_fnv1a_mix( h, static_cast<uint64_t>( ev.pos.x() ) );
    h = coop_fnv1a_mix( h, static_cast<uint64_t>( ev.pos.y() ) );
    h = coop_fnv1a_mix( h, static_cast<uint64_t>( ev.pos.z() ) );
    h = coop_fnv1a_mix( h, static_cast<uint64_t>( ev.value ) );
    h = coop_fnv1a_mix( h, static_cast<uint64_t>( ev.creature_id ) );
    return h;
}

/// Client-side variant: mixes the same 6 fields from JSON-parsed integers.
/// Called once per event after parsing "ev","x","y","z","v","cid" from JSON.
constexpr auto coop_hash_event_fields(
    uint64_t h, int type, int x, int y, int z, int value, int creature_id ) -> uint64_t
{
    h = coop_fnv1a_mix( h, static_cast<uint64_t>( type ) );
    h = coop_fnv1a_mix( h, static_cast<uint64_t>( x ) );
    h = coop_fnv1a_mix( h, static_cast<uint64_t>( y ) );
    h = coop_fnv1a_mix( h, static_cast<uint64_t>( z ) );
    h = coop_fnv1a_mix( h, static_cast<uint64_t>( value ) );
    h = coop_fnv1a_mix( h, static_cast<uint64_t>( creature_id ) );
    return h;
}

/// Result of coop_collect_streamable: streamable A4 delta events plus their
/// aggregate FNV-1a hash.  Splitting filter/hash/collect into one place means
/// dropping sent.push_back causes a test failure — hash tests alone would not
/// catch that regression.
struct coop_streamable_result {
    std::vector<coop_world_event> sent;
    uint64_t hash = COOP_FNV_OFFSET;
};

/// Filter `events` to the A4 streamable set (terrain/furniture/field only),
/// hash each retained event with coop_hash_event, and collect into sent.
/// Called by build_and_send_sync (server) and exercised directly by unit tests.
auto coop_collect_streamable( std::vector<coop_world_event> events ) // *NOPAD*
-> coop_streamable_result;

/// Per-tick mutation log.  Exactly one instance lives on the main thread while
/// a co-op host tick is running; it is set/cleared via RAII (see coop_tick_log_guard).
///
/// Zero overhead in single-player: current() returns nullptr when no guard is active,
/// so every hook is a single null-check that predicts well.
///
/// Thread safety: all world simulation runs on the main thread.  The IO thread
/// (receiver_thread_) never touches the log.  No locking needed.
struct coop_mutation_log {
        /// Returns the active log for this thread, or nullptr (SP / outside tick).
        static auto current() -> coop_mutation_log*;

        /// Push an event into the buffer.
        auto push( coop_world_event e ) -> void;

        /// Drain and return the event buffer; clears it in place.
        auto flush() -> std::vector<coop_world_event>;

        /// Running FNV-1a hash of all pushed events.  The client computes an
        /// equivalent hash and sends resync_request on mismatch (A4).
        auto hash() const -> uint64_t { return running_hash_; }

        /// Total events pushed this tick.
        auto size() const -> std::size_t { return events_.size(); }

    private:
        std::vector<coop_world_event> events_;
        uint64_t running_hash_ = COOP_FNV_OFFSET;
};

/// RAII guard: installs a coop_mutation_log as the thread-local current log
/// for the duration of one host tick, then removes it.
///
/// Usage (in coop_server::coop_world_tick):
///   coop_tick_log_guard guard;
///   g->post_action_world_step();   // all mutations inside are logged
///   auto events = guard.log().flush();
struct coop_tick_log_guard {
        explicit coop_tick_log_guard();
        ~coop_tick_log_guard();

        coop_tick_log_guard( const coop_tick_log_guard & ) = delete;
        coop_tick_log_guard &operator=( const coop_tick_log_guard & ) = delete;

        auto log() -> coop_mutation_log & { return log_; }

    private:
        coop_mutation_log log_;
};

#endif // COOP_ENABLED
