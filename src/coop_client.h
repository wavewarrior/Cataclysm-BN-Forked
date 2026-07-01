#pragma once
#ifdef COOP_ENABLED

#include "coop_proto.h"

#include <SDL3_net/SDL_net.h>
#include <deque>
#include <optional>
#include <string>

/// Client-side co-op thin path.
struct coop_client {
    coop_client() = default;
    ~coop_client();
    coop_client(const coop_client&) = delete;
    coop_client& operator=(const coop_client&) = delete;

    auto connect(const std::string& ip, uint16_t port = 8080) -> bool;
    auto handshake() -> bool;
    auto coop_world_tick() -> void;
    auto queue_action(const std::string& key, const std::string& ctx_json = {}) -> void;
    auto is_connected() const -> bool { return socket_ != nullptr; }
    auto shutdown() -> void;
    auto send_chat(const std::string& text) -> void;

private:
    auto apply_sync(const std::string& json_buf) -> void;
    auto handle_disconnect() -> void;

    NET_StreamSocket* socket_ = nullptr;

    struct pending_action {
        std::string key;
        std::string ctx_json;
    };
    std::deque<pending_action> action_q_; // main-thread only
};

#endif // COOP_ENABLED
