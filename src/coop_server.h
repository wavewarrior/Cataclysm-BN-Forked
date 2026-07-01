#pragma once
#ifdef COOP_ENABLED

#include "coop_proto.h"

#include <SDL3_net/SDL_net.h>
#include <atomic>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>

#include "coordinates.h"

class npc;

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
    auto spawn_proxy_npc(const tripoint_abs_ms& spawn_pos, const std::string& player_name) -> npc*;
    auto start_receiver_thread() -> void;
    auto coop_world_tick() -> void;
    auto update_proxy_position(npc* proxy) -> void;
    auto build_and_send_sync() -> void;
    auto shutdown() -> void;
    auto is_running() const -> bool { return running_.load(); }

private:
    struct action_entry {
        std::string key;
        std::string ctx_json;
    };
    struct chat_entry {
        std::string text;
    };

    auto push_action(action_entry e) -> void;
    auto try_pop_action() -> std::optional<action_entry>;
    auto try_pop_chat() -> std::optional<chat_entry>;
    auto execute_client_action(npc* proxy, const std::string& key, const std::string& ctx_json)
        -> void;
    auto receiver_loop() -> void;

    NET_Server* server_sock_ = nullptr;
    NET_StreamSocket* client_sock_ = nullptr;

    std::atomic<bool> running_{false};
    std::jthread receiver_thread_;

    std::mutex action_mtx_;
    std::deque<action_entry> action_q_;
    std::mutex chat_mtx_;
    std::deque<chat_entry> chat_q_;

    std::unordered_set<std::string> client_known_vehicles_;
};

#endif // COOP_ENABLED
