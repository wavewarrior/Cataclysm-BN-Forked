#pragma once
#ifdef COOP_ENABLED

#include "coop_proto.h"
#include "coordinates.h"

#include <SDL3_net/SDL_net.h>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>

class monster;

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
    auto is_connected() const -> bool { return socket_ != nullptr; }
    auto shutdown() -> void;
    auto send_chat(const std::string& text) -> void;
    /// C6: send the client's current abs position to the host immediately after receiving
    /// world_seed, so spawn_proxy_npc() can place the proxy at the client's saved position.
    auto send_join_info() -> bool; // *NOPAD*
    /// C3 on-death sync: call from game::is_game_over() when u.is_dead_state() fires.
    /// Sends client_status dead=true and death-drop manifest synchronously before teardown.
    /// Safe to call multiple times — guarded by death_notified_.
    auto notify_death() -> void;

private:
    auto apply_sync(const std::string& json_buf) -> void;
    auto handle_disconnect() -> void;
    /// C3 on-death inventory sync: serialise inv_dump() as a DROP manifest to the host.
    /// Called once on the first tick the client's avatar reports dead.
    auto send_death_drop() -> void;

    NET_StreamSocket* socket_ = nullptr;

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
    // H5: host-assigned monster ID → local monster pointer.
    // Stable for stationary monsters; updated on position change.
    std::unordered_map<int, monster*> coop_monster_map_;

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
