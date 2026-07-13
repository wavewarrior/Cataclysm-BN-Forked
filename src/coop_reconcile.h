#pragma once

#include "coordinates.h"

#include <cstdint>
#include <span>
#include <string>

/// Minimal action descriptor used by the reconciliation function.
/// Decoupled from coop_client::pending_action so the pure function and its
/// tests have no dependency on the full client struct or SDL3_net.
struct reconcile_action {
    uint32_t seq = 0;
    std::string key; ///< MOVE_N, MOVE_S, MOVE_E, MOVE_W, MOVE_NE/NW/SE/SW; others are no-ops
};

/// Compute the reconciled client position after receiving a seq-confirmed sync.
///
/// Returns @p server_pos with the directional deltas of all pending actions
/// whose seq > last_seq applied on top — the predicted position the client
/// should display after the server has confirmed through @p last_seq.
///
/// Behaviour by case:
///  • last_seq >= 0, pending empty    → returns server_pos (all caught up)
///  • last_seq >= 0, pending non-empty → server_pos + replayed deltas
///  • last_seq < 0 (pre-A2 host)     → returns server_pos unchanged (snap-only).
///    The caller must NOT replay when last_seq < 0; the ring buffer may hold
///    up to 32 stale, untrimmed entries that would fling the avatar away.
///
/// This function is pure: it never mutates @p pending.  The caller is
/// responsible for trimming confirmed entries from the ring buffer separately.
auto coop_reconcile_pos(
    tripoint_bub_ms server_pos, int last_seq, std::span<const reconcile_action> pending )
-> tripoint_bub_ms;
