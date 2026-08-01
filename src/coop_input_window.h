#pragma once

#include <cstddef>
#include <deque>
#include <string>

/// One buffered player action awaiting execution in the co-op main loop.
struct buffered_action {
    std::string action;        ///< resolved action string, e.g. "move_n"
    double enqueued_ms = 0.0;  ///< steady-clock ms, same clock as coop_admit_action's now_ms
    /// False for actions that cannot change world state (menus, info screens, toggles,
    /// save/quit).  Set by the caller from can_action_change_worldstate(); such entries are
    /// never evicted and never expire, so a menu key pressed during a burst always lands.
    bool evictable = true;
};

/// Exponentially-weighted mean of recent world-tick wall-clock costs.
/// Pure state holder — no game access, so [coop][inputwindow] tests drive it directly.
struct coop_tick_cost_tracker {
    double ewma_ms = 0.0;
    auto sample( double tick_ms ) -> void;
    auto value() const -> double { return ewma_ms; }
};

/// The input coalescing/staleness window: how long the slower side needs to resolve one
/// committed action.  clamp( max( local, remote ), MIN, MAX ).
auto coop_input_window_ms( double local_ewma_ms, double remote_ewma_ms ) -> double;

/// Append `act` at `now_ms`.  While the queue exceeds COOP_MAX_QUEUED_ACTIONS, erase the
/// oldest evictable entry; if none is evictable, erase the oldest entry regardless so the
/// bound is hard.  Returns the number of entries evicted.
auto coop_admit_action( std::deque<buffered_action> &q, buffered_action act )
-> std::size_t; // *NOPAD*

/// Erase every evictable entry older than `window_ms`, except the single most recent entry
/// in the queue, which is never dropped.  Returns the number erased.
auto coop_expire_stale_actions( std::deque<buffered_action> &q, double now_ms,
                                double window_ms ) -> std::size_t; // *NOPAD*
