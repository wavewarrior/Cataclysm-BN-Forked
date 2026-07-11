#pragma once
#ifdef COOP_ENABLED

#include "coop_proto.h"
#include "coordinates.h"

#include "coop_net_transport.h"
#include <memory>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>

class monster;
class vehicle;

/// Client-side co-op thin path.
struct coop_client {
    coop_client() = default;
    ~coop_client();
    coop_client(const coop_client&) = delete;
    coop_client& operator=(const coop_client&) = delete;

    auto connect(const std::string& ip, uint16_t port = 8080) -> bool;
    auto handshake() -> bool;
    auto receive_world_seed() -> bool;
    auto apply_world_seed_to_avatar() -> void;
    auto coop_world_tick() -> void;
    auto queue_action(const std::string& key, const std::string& ctx_json = {}) -> void;
    /// Convenience: build and queue a TERRAIN_CHANGE packet from typed ids.
    /// Avoids duplicating JSON-building logic across C2b/C2c call sites.
    auto queue_terrain_change( const tripoint_abs_ms &pos, const std::string &ter_id,
                               const std::string &furn_id ) -> void;
    auto is_connected() const -> bool { return transport_ != nullptr; }
    auto shutdown() -> void;
    auto send_chat(const std::string& text) -> void;
    /// F4: send a raw JSON packet directly via transport (overmap mark, emotes, etc.)
    auto send_raw(const std::string& json) -> void;
    /// C6: send the client's current abs position to the host immediately after receiving
    /// world_seed, so spawn_proxy_npc() can place the proxy at the client's saved position.
    auto send_join_info() -> bool; // *NOPAD*
    /// C3 on-death sync: call from game::is_game_over() when u.is_dead_state() fires.
    /// Sends client_status dead=true and death-drop manifest synchronously before teardown.
    /// Safe to call multiple times — guarded by death_notified_.
    auto notify_death() -> void;
    // ---- Test seams (all inline; no game-world access) ----
    /// Test seam: skip hashing the next ONE mutation event in apply_sync, inducing a
    /// local_hash ≠ server_hash divergence so the client naturally sends resync_request.
    auto set_skip_one_hash_event_for_test() -> void { skip_one_hash_event_for_test_ = true; }
    /// Test seam: true if apply_sync has processed at least one full-tile sync (got_tiles).
    auto got_full_tile_sync_for_test() const -> bool { return got_full_tile_sync_for_test_; }
    /// Test seam: close the underlying TCP socket immediately WITHOUT sending the
    /// protocol disconnect packet — simulates a client crash.  Host should detect
    /// the abrupt drop via receiver_thread_ and set running_=false.
    auto close_socket_abruptly_for_test() -> void {
        if( transport_ ) {
            transport_->close_abruptly();
            transport_.reset();
        }
    }
    /// Test seam: number of entries in the pending action ring buffer (Gap 4).
    auto pending_actions_size_for_test() const -> std::size_t { return pending_actions_.size(); }
    /// Test seam: smallest seq in the ring buffer — the oldest unconfirmed action (Gap 4).
    auto pending_actions_front_seq_for_test() const -> uint32_t {
        return pending_actions_.empty() ? 0u : pending_actions_.front().seq;
    }
    /// Test seam: set next_seq_ for Gap 6 uint32_t wrap-without-crash test.
    auto set_next_seq_for_test(uint32_t seq) -> void { next_seq_ = seq; }
    auto send_tap_shoulder() -> void;
    auto send_emote( const std::string& emote_type ) -> void;

private:
    auto apply_sync(const std::string& json_buf) -> void;
    auto handle_disconnect() -> void;
    auto send_death_drop() -> void;

    std::unique_ptr<coop_transport> transport_;

    struct pending_action {
        uint32_t seq = 0;
        std::string key;
        std::string ctx_json;
        bool sent = false; ///< true once the packet has been written to the socket
    };
    /// Ring buffer of unconfirmed actions (queued + sent, kept until server confirms via
    /// last_seq).  Entries are discarded in apply_sync() once seq ≤ last_seq_from_sync.
    /// Main-thread only — no locking needed.
    std::deque<pending_action> pending_actions_;
    uint32_t next_seq_ = 1;
    bool net_initialized_ = false;
    bool death_notified_ = false; ///< guard for notify_death(); prevents double-send
    /// Test seam: when true, the next event's hash mixing is skipped (see apply_sync).
    /// Cleared automatically after the first event is skipped.
    bool skip_one_hash_event_for_test_  = false;
    /// Test seam: set to true the first time apply_sync processes got_tiles == true.
    bool got_full_tile_sync_for_test_   = false;
    // H5: host-assigned monster ID → local monster pointer.
    std::unordered_map<int, monster*> coop_monster_map_;

    // E1: vehicle tracking — populated from initial-sync vehicle-ID map sent by host
    std::unordered_map<const vehicle*, uint32_t> coop_vehicle_map_inv_;
    std::unordered_map<uint32_t, vehicle*>        coop_vehicle_map_;
    int coop_vehicle_stationary_ticks_ = 0;

    // F1: host activity string received from sync — used for F5 team speed-up
    std::string host_activity_str_;

    // Last proxy and host positions received from sync — used for reconciliation
    // and future host-avatar rendering.  Initialized to zero; valid after first sync.
    tripoint_abs_ms sync_proxy_apos_{};
    tripoint_abs_ms sync_host_apos_{};

    // World seed data extracted from the packet
    int world_seed_turn_ = 0;
    tripoint_abs_ms world_seed_spawn_;
    std::string world_seed_partner_name_ = "Partner";
};

#endif // COOP_ENABLED
