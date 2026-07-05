#ifdef COOP_ENABLED

#include "coop_mutation_log.h"

// ---------------------------------------------------------------------------
// Thread-local singleton
// ---------------------------------------------------------------------------

namespace {
thread_local coop_mutation_log* tl_current_log = nullptr;
} // namespace

auto coop_mutation_log::current() -> coop_mutation_log* { return tl_current_log; }

// ---------------------------------------------------------------------------
// Event buffer
// ---------------------------------------------------------------------------

auto coop_mutation_log::push(coop_world_event e) -> void {
    // Delegate to the shared canonical hasher — same function as server/client.
    running_hash_ = coop_hash_event(running_hash_, e);
    events_.push_back(std::move(e));
}

auto coop_mutation_log::flush() -> std::vector<coop_world_event> {
    std::vector<coop_world_event> out;
    out.swap(events_);
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

auto coop_collect_streamable(std::vector<coop_world_event> events) -> coop_streamable_result {
    using evt = coop_event_type;
    coop_streamable_result result;
    for (auto& ev : events) {
        const bool streamable =
            ev.type == evt::terrain_changed || ev.type == evt::furniture_changed
            || ev.type == evt::field_created || ev.type == evt::field_changed
            || ev.type == evt::field_expired;
        if (!streamable) { continue; }
        result.hash = coop_hash_event(result.hash, ev); // hash BEFORE move
        result.sent.push_back(std::move(ev));
    }
    return result;
}

#endif // COOP_ENABLED
