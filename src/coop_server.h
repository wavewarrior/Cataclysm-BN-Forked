#pragma once
#ifdef COOP_ENABLED

#include "coop_proto.h"
#include "coordinates.h"
#include "player_cmd.h"

#include <SDL3_net/SDL_net.h>
#include <atomic>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

class monster;

class npc;

/// Single-tick entity position snapshot for A5.3 lag compensation.
/// Stored in coop_server::position_history_; exposed here so the pure lookup
/// function coop_lag_find_target() can be unit-tested without game state.
struct coop_entity_snapshot {
    uint32_t seq = 0;
    std::vector<std::pair<int, tripoint_abs_ms>> creature_positions; // stable_id → abs_pos
};

/// Given a rolling history of entity snapshots, return the stable_id of the
/// creature at `target_abs` in the best snapshot for `fire_seq`, or -1 if none.
/// Pure function — no side effects; safe to call from unit tests.
auto coop_lag_find_target(
    const std::deque<coop_entity_snapshot>& history, // *NOPAD*
    uint32_t fire_seq,
    const tripoint_abs_ms& target_abs) -> int; // *NOPAD*

/// Host-side co-op server.
struct coop_server {
    coop_server() = default;
    ~coop_server();
    coop_server(const coop_server&) = delete;
    coop_server& operator=(const coop_server&) = delete;

    auto listen(uint16_t port = 8080) -> bool;
    /// Non-blocking: attempt to accept one pending client. Returns true if a
    /// client connected and client_sock_ is now valid.
    auto try_accept() -> bool;
    auto wait_for_client() -> bool;
    auto handshake() -> bool;
    auto send_world_seed(const std::string& player_name) -> bool;
    auto send_initial_sync() -> bool;
    auto spawn_proxy_npc(const tripoint_abs_ms& spawn_pos, const std::string& player_name) -> npc*;
    auto start_receiver_thread() -> void;
    auto coop_world_tick() -> void;
    auto update_proxy_position(npc* proxy) -> void;
    auto build_and_send_sync(bool force_full = false) -> void;
    auto shutdown() -> void;
    auto send_chat(const std::string& text) -> void;
    auto is_running() const -> bool { return running_.load(); }
    auto has_pending_actions() const -> bool;
    /// True iff both host and client are in a long idle activity (sleep, craft, …).
    /// Drives maybe_fast_forward(); public so unit tests can verify the truth table.
    auto both_idle() const -> bool; // *NOPAD*
    /// Saturate main_loop_accum_ms_ to COOP_FAST_FORWARD_ACCUM_MS when both players
    /// are idle, causing the main loop to fire COOP_MAX_CATCH_UP game ticks.
    /// Returns true if fast-forward was triggered.
    /// Extracted from coop_world_tick() for testability.
    auto maybe_fast_forward() -> bool; // *NOPAD*
    /// Test seam: directly set the client-idle flag that is normally written by the
    /// receiver thread when processing client_status packets.
    auto set_client_idle_for_test(bool v) -> void { client_is_idle_.store(v); }
    /// Typed dispatcher: apply a pre-parsed player_cmd_t to the proxy NPC.
    /// Called by execute_client_action() after parsing the string key.
    /// Public so unit tests can call it directly with typed commands.
    auto execute_player_cmd(npc* proxy, const player_cmd_t& cmd, uint32_t seq) -> void; // *NOPAD*
    /// Test seam: parse a PICKUP manifest JSON and apply item removals to g->m.
    /// Does NOT need a live server instance or a proxy NPC — safe to call from
    /// unit tests with a world set up via build_test_map().
    static auto apply_pickup_manifest(const std::string& ctx_json) -> void; // *NOPAD*
    /// Test seam: parse a DROP manifest JSON and add items to g->m.
    /// Full item JSON (item::serialize) is used — no per-instance state loss.
    /// Safe to call from unit tests with a world set up via build_test_map().
    static auto apply_drop_manifest(const std::string& ctx_json) -> void; // *NOPAD*
    /// Test seam: apply a TERRAIN_CHANGE message to g->m (ter_set + furn_set).
    /// Emitted by the client when it opens/closes a door or changes terrain.
    static auto apply_terrain_change( const std::string &ctx_json ) -> void; // *NOPAD*

private:
    struct action_entry {
        uint32_t seq = 0;
        std::string key;
        std::string ctx_json;
    };
    struct chat_entry {
        std::string text;
    };
    // entity_snapshot moved to coop_entity_snapshot (public, before this struct)

    auto push_action(action_entry e) -> void;
    auto try_pop_action() -> std::optional<action_entry>;
    auto try_pop_chat() -> std::optional<chat_entry>;
    auto execute_client_action(
        npc* proxy, const std::string& key, const std::string& ctx_json, uint32_t seq) -> void;
    auto receiver_loop(std::stop_token st) -> void;
    auto push_entity_snapshot() -> void;
    auto resolve_fire_at_seq(npc* proxy, uint32_t seq, int target_ax, int target_ay, int target_az)
        -> void;

    NET_Server* server_sock_ = nullptr;
    // client_sock_ is owned exclusively by the IO thread once receiver_thread_ starts.
    // Main thread MUST NOT call send/recv on it directly — use send_q_ instead.
    NET_StreamSocket* client_sock_ = nullptr;

    std::atomic<bool> running_{false};
    std::jthread receiver_thread_;

    mutable std::mutex action_mtx_;
    std::deque<action_entry> action_q_;
    mutable std::mutex chat_mtx_;
    std::deque<chat_entry> chat_q_;

    // Outgoing frame queue: main thread pushes; IO thread drains and sends.
    std::mutex send_mtx_;
    std::deque<std::string> send_q_;

    bool net_initialized_ = false;

    std::unordered_set<std::string> client_known_vehicles_;
    // Position-based tile dirty gate: resend tiles only when host moves to a new submap.
    // Sentinel {INT_MIN,INT_MIN,INT_MIN} forces a full sync on first tick.
    tripoint_abs_sm last_sync_origin_{
        std::numeric_limits<int>::min(), std::numeric_limits<int>::min(),
        std::numeric_limits<int>::min()};

    // Set by the receiver thread when a client_status packet declares the client
    // is sleeping or in a long activity.  Read by both_idle() on the main thread —
    // atomic so no lock needed for this single bool.
    std::atomic<bool> client_is_idle_{false};
    // Set by the receiver thread (resync_request packet) to request a forced full
    // submap sync on the next tick.  Must be atomic — IO thread writes, main thread
    // reads+clears in build_and_send_sync().  exchange(false) clears atomically.
    std::atomic<bool> force_resync_{false};

    // Periodic full tile resync safety net — catches in-place terrain changes
    // (smash, doors, fire, explosions, construction) when the host is stationary
    // and abs_sub hasn't changed.  Forced resync every TILE_RESYNC_INTERVAL ticks
    // (30 s at 1 tick/sec) regardless of position.
    static constexpr int TILE_RESYNC_INTERVAL = 30;
    int sync_tick_counter_ = 0;

    // H5: stable monster IDs — pointer is stable while held in creature_tracker.
    std::unordered_map<const monster*, int> monster_id_map_;
    int next_monster_id_ = 1;
    uint32_t last_confirmed_seq_ = 0;
    std::deque<coop_entity_snapshot> position_history_; // rolling window, capped at 10 entries
};

#endif // COOP_ENABLED
