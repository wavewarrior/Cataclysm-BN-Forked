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
    // FNV-1a: mix event type, position, and value into the running hash.
    // The client replicates this hash; a mismatch triggers resync_request.
    auto mix = [&](uint64_t v) {
        running_hash_ ^= v;
        running_hash_ *= 0x00000100000001B3ULL;
    };
    mix(static_cast<uint64_t>(e.type));
    mix(static_cast<uint64_t>(e.pos.x()));
    mix(static_cast<uint64_t>(e.pos.y()));
    mix(static_cast<uint64_t>(e.pos.z()));
    mix(static_cast<uint64_t>(e.value));
    mix(static_cast<uint64_t>(e.creature_id));

    events_.push_back(std::move(e));
}

auto coop_mutation_log::flush() -> std::vector<coop_world_event> {
    std::vector<coop_world_event> out;
    out.swap(events_);
    running_hash_ = 0xcbf29ce484222325ULL; // reset to FNV offset basis
    return out;
}

// ---------------------------------------------------------------------------
// RAII guard
// ---------------------------------------------------------------------------

coop_tick_log_guard::coop_tick_log_guard() { tl_current_log = &log_; }

coop_tick_log_guard::~coop_tick_log_guard() { tl_current_log = nullptr; }

#endif // COOP_ENABLED
