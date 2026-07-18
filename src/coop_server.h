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
#include "coop_net_transport.h"
#include <memory>

class monster;

class npc;
class vehicle;


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
    const tripoint_abs_ms& target_abs ) -> int; // *NOPAD*

/// Host-side co-op server.
struct coop_server {
        coop_server() = default;
        ~coop_server();
        coop_server( const coop_server & ) = delete;
        coop_server &operator=( const coop_server & ) = delete;

        auto listen( uint16_t port = 8080 ) -> bool;
        /// Non-blocking: attempt to accept one pending client. Returns true if a
        /// client connected and transport_ is now valid.
        auto try_accept() -> bool;
        auto wait_for_client() -> bool;
        auto handshake() -> bool;
        auto send_world_seed( const std::string& player_name ) -> bool;
        auto send_initial_sync() -> bool;
        auto spawn_proxy_npc( const tripoint_abs_ms& spawn_pos, const std::string& player_name ) -> npc*;
        auto start_receiver_thread() -> void;
        auto coop_world_tick() -> void;
        auto update_proxy_position( npc* proxy ) -> void;
        auto build_and_send_sync( bool force_full = false ) -> void;
        auto shutdown() -> void;
        auto send_chat( const std::string& text ) -> void;
        /// F4: send a raw JSON packet directly onto the send queue (overmap mark, emotes, etc.)
        auto send_raw( const std::string& json ) -> void;
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
        auto set_client_idle_for_test( bool v ) -> void { client_is_idle_.store( v ); }
        /// Test seam: read force_resync_ without consuming it (main-thread only).
        auto force_resync_pending_for_test() const -> bool { return force_resync_.load(); }
        /// Test seam: directly set client HP percentage (normally written by receiver thread).
        auto set_client_hp_pct_for_test( int v ) -> void { client_hp_pct_.store( v ); }
        /// Test seam: directly set client-dead flag (normally written by receiver thread).
        auto set_client_dead_for_test( bool v ) -> void { client_dead_.store( v ); }
        /// Test seam: read client_death_announced_ (main-thread only, non-atomic).
        auto client_death_announced_for_test() const -> bool { return client_death_announced_; }
        /// Test seam: push a synthetic streamable event into the NEXT coop_world_tick()'s
        /// mutation log directly — immune to the tile already having the target terrain
        /// (which would make ter_set a no-op → ev_count=0 → hash check never fires).
        auto queue_test_event_for_resync() -> void { pending_test_event_for_resync_ = true; }
        /// Test seam: directly set force_resync_=true (bypasses hash detection path;
        /// use for E2E smoke test of the server's full-sync response).
        auto set_force_resync_for_test() -> void { force_resync_.store( true ); }
        /// Test seam: reset last_sync_origin_ so origin_changed fires on the next tick
        /// (triggers a full sync without needing the host avatar to actually move).
        auto reset_sync_origin_for_test() -> void {
            last_sync_origin_ = tripoint_abs_sm{
                std::numeric_limits<int>::min(), std::numeric_limits<int>::min(),
                std::numeric_limits<int>::min() };
        }
        /// C3: client's last-reported HP percentage (0–100).  Thread-safe read.
        auto client_hp_pct() const -> int { return client_hp_pct_.load(); }
        /// C3: true if the client's avatar reported dead on the last tick.
        auto client_dead() const -> bool { return client_dead_.load(); }
        /// Typed dispatcher: apply a pre-parsed player_cmd_t to the proxy NPC.
        /// Called by execute_client_action() after parsing the string key.
        /// Public so unit tests can call it directly with typed commands.
        auto execute_player_cmd( npc* proxy, const player_cmd_t& cmd, uint32_t seq ) -> void; // *NOPAD*
        /// Test seam: parse a PICKUP manifest JSON and apply item removals to g->m.
        /// Does NOT need a live server instance or a proxy NPC — safe to call from
        /// unit tests with a world set up via build_test_map().
        static auto apply_pickup_manifest( const std::string& ctx_json ) -> void; // *NOPAD*
        /// Test seam: parse a DROP manifest JSON and add items to g->m.
        /// Full item JSON (item::serialize) is used — no per-instance state loss.
        /// Safe to call from unit tests with a world set up via build_test_map().
        static auto apply_drop_manifest( const std::string& ctx_json ) -> void; // *NOPAD*
        /// Test seam: apply a TERRAIN_CHANGE message to g->m (ter_set + furn_set).
        /// Emitted by the client when it opens/closes a door or changes terrain.
        static auto apply_terrain_change( const std::string &ctx_json ) -> void; // *NOPAD*
        /// C6: block up to timeout_ms for the client's join_info packet.
        /// Stores the client's starting abs position in client_join_pos_.
        /// Returns true on receipt; false on timeout (non-fatal — spawn uses fallback).
        auto wait_for_join_info( int timeout_ms = 3000 ) -> bool; // *NOPAD*
        /// C6: client's starting position received via join_info; nullopt if not yet received.
        auto client_join_pos() const -> std::optional<tripoint_abs_ms>; // *NOPAD*
        /// G2: true if the client avatar is in the downed state (server-tracked).
        auto client_downed() const -> bool { return client_downed_.load(); } // *NOPAD*
        auto stabilize_client() -> void;
        auto send_tap_shoulder() -> void;
        auto send_emote( const std::string& emote_type ) -> void;

        /// Split from shutdown(): tear down transport + receiver thread only,
        /// preserving server_sock_, proxy NPC, ID maps, and session state.
        /// Called when the receiver detects a client disconnect.
        auto handle_client_disconnect() -> void;

        /// Accept a reconnecting client on the existing server_sock_.
        /// Validates session token, re-establishes transport, sends full sync.
        /// Returns true if reconnection succeeded.
        auto accept_reconnect() -> bool;

        /// True if client has disconnected but session is still alive for reconnect.
        auto awaiting_reconnect() const -> bool { return awaiting_reconnect_.load(); }

        // Session token for reconnection validation
        std::string session_token_;

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

        auto push_action( action_entry e ) -> void;
        auto try_pop_action() -> std::optional<action_entry>;
        auto try_pop_chat() -> std::optional<chat_entry>;
        auto execute_client_action(
            npc* proxy, const std::string& key, const std::string& ctx_json, uint32_t seq ) -> void;
        auto receiver_loop( std::stop_token st ) -> void;
        auto push_entity_snapshot() -> void;
        auto resolve_fire_at_seq( npc* proxy, uint32_t seq, int target_ax, int target_ay, int target_az )
        -> void;

        NET_Server *server_sock_ = nullptr;
        std::unique_ptr<coop_transport> transport_; ///< owned; nullptr until client connects

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
        // C3 character vitals (written by receiver thread, read by main thread).
        std::atomic<int>  client_hp_pct_{100};    ///< 0–100; 100 = full health
        std::atomic<bool> client_dead_{false};    ///< true once client avatar reports death
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
        std::unordered_map<const monster *, int> monster_id_map_;
        int next_monster_id_ = 1;
        uint32_t last_confirmed_seq_ = 0;
        std::deque<coop_entity_snapshot> position_history_; // rolling window, capped at 10 entries
        bool client_death_announced_ = false; ///< main-thread only; gate for C3b death message
        // C6: client's starting abs position from the join_info packet.
        // Written by wait_for_join_info() (main thread, pre-receiver); read by client_join_pos()
        // and spawn_proxy_npc() on the main thread.  No mutex needed — single-threaded access.
        std::optional<tripoint_abs_ms> client_join_pos_; ///< nullopt until join_info arrives
        /// Test seam: when true, next coop_world_tick() pushes a synthetic terrain event
        /// directly into the mutation log (see queue_test_event_for_resync()).
        bool pending_test_event_for_resync_ = false;
        // E1: vehicle tracking — pointer stable while vehicle is alive in current map
        std::unordered_map<const vehicle *, uint32_t> vehicle_id_map_;
        std::unordered_map<uint32_t, vehicle *> vehicle_id_map_rev_;
        uint32_t next_vehicle_id_ = 1;
        // E1: latest vehicle state from client — receiver writes, world_tick applies
        struct pending_veh_state_t {
            uint32_t vid = 0;
            tripoint_abs_ms abs_pos{};
            int face_x = 0;
            int face_y = 1;
            int vel = 0;
            bool valid = false;
        };
        mutable std::mutex pending_veh_mtx_;
        pending_veh_state_t pending_veh_state_;
        // F1: extended client vitals — receiver writes, guarded by chat_mtx_
        std::atomic<int> client_stamina_pct_{100};
        std::string client_activity_str_;   ///< guarded by chat_mtx_
        tripoint_abs_ms client_abs_pos_{};  ///< guarded by chat_mtx_
        // F2: pending trade offer from client — receiver writes via action_mtx_; world_tick consumes
        std::optional<std::string> pending_trade_offer_json_; ///< guarded by action_mtx_
        // F3: tap shoulder
        std::atomic<bool> pending_tap_{false};
        bool pending_tap_sent_to_client_ = false; ///< main-thread only
        // G1: worn JSON from join_info — main-thread only (pre-receiver window)
        std::string client_worn_json_;
        // G2: downed state
        std::atomic<bool> client_downed_{false};
        int client_down_turns_remaining_ = 0; ///< main-thread only
        static constexpr int COOP_DOWN_TIMEOUT_TURNS = 100;
        // Receiver→main-thread sync buffers (guarded by pending_sync_mtx_)
        std::mutex pending_sync_mtx_;
        std::vector<std::pair<std::string, int>> pending_skills_;
        std::vector<tripoint_abs_omt> pending_overmap_tiles_;
        // F4: overmap mark — receiver writes, world_tick applies (guarded by pending_sync_mtx_)
        struct pending_mark_t {
            bool valid = false;
            bool clear = false;
            tripoint_abs_omt pos{};
            std::string label;
        };
        pending_mark_t pending_mark_;
        // Reconnection state
        std::atomic<bool> awaiting_reconnect_{false};
        std::atomic<bool> client_disconnected_{false};
        static constexpr int RECONNECT_TIMEOUT_TICKS = 300; // 5 minutes at 1Hz
        int reconnect_countdown_ = 0;
};

#endif // COOP_ENABLED
