#include "coop_reconcile.h"

namespace {

/// Map a movement key string to a tile-space delta.
/// Non-movement keys (PAUSE, SMASH, FIRE, PICKUP, …) return {0,0,0} so they
/// are effectively skipped during replay.
auto key_to_delta(const std::string& key) -> tripoint {
    // Strings are exactly what handle_action.cpp emits (see queue_action call site).
    // UP/DOWN/LEFT/RIGHT are never sent — handle_action only produces MOVE_* strings.
    if (key == "MOVE_N") {
        return {0, -1, 0};
    } else if (key == "MOVE_S") {
        return {0, 1, 0};
    } else if (key == "MOVE_E") {
        return {1, 0, 0};
    } else if (key == "MOVE_W") {
        return {-1, 0, 0};
    } else if (key == "MOVE_NE") {
        return {1, -1, 0};
    } else if (key == "MOVE_NW") {
        return {-1, -1, 0};
    } else if (key == "MOVE_SE") {
        return {1, 1, 0};
    } else if (key == "MOVE_SW") {
        return {-1, 1, 0};
    }
    return {0, 0, 0}; // PAUSE, SMASH, FIRE, PICKUP, … — no position change
}

} // namespace

auto coop_reconcile_pos(
    tripoint_bub_ms server_pos, int last_seq, std::span<const reconcile_action> pending)
    -> tripoint_bub_ms {
    if (last_seq < 0) {
        // Pre-A2 host: snap-only.  Do not replay — pending may hold up to 32
        // stale, untrimmed entries that would fling the avatar away.
        return server_pos;
    }

    const auto confirmed = static_cast<uint32_t>(last_seq);
    auto pos = server_pos;
    for (const auto& act : pending) {
        if (act.seq <= confirmed) { continue; } // already confirmed by server
        pos = pos + key_to_delta(act.key);
    }
    return pos;
}
