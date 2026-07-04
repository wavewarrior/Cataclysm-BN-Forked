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
    auto push(coop_world_event e) -> void;

    /// Drain and return the event buffer; clears it in place.
    auto flush() -> std::vector<coop_world_event>;

    /// Running FNV-1a hash of all pushed events.  The client computes an
    /// equivalent hash and sends resync_request on mismatch (A4).
    auto hash() const -> uint64_t { return running_hash_; }

    /// Total events pushed this tick.
    auto size() const -> std::size_t { return events_.size(); }

private:
    std::vector<coop_world_event> events_;
    uint64_t running_hash_ = 0xcbf29ce484222325ULL; // FNV offset basis
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

    coop_tick_log_guard(const coop_tick_log_guard&) = delete;
    coop_tick_log_guard& operator=(const coop_tick_log_guard&) = delete;

    auto log() -> coop_mutation_log& { return log_; }

private:
    coop_mutation_log log_;
};

#endif // COOP_ENABLED
