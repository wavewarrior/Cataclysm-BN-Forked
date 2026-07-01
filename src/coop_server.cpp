#ifdef COOP_ENABLED

#include "coop_server.h"

#include "coop_net.h"
#include "coop_session.h"
#include "debug.h"

#include <SDL3_net/SDL_net.h>

coop_server::~coop_server() { shutdown(); }

auto coop_server::listen(uint16_t port) -> bool {
    if (!NET_Init()) {
        DebugLog(DL::Error, DC::Main) << "[coop] NET_Init failed: " << SDL_GetError();
        return false;
    }
    server_sock_ = NET_CreateServer(nullptr, port, 0);
    if (!server_sock_) {
        DebugLog(DL::Error, DC::Main) << "[coop] NET_CreateServer failed: " << SDL_GetError();
        return false;
    }
    DebugLog(DL::Info, DC::Main) << "[coop] listening on port " << port;
    return true;
}

auto coop_server::wait_for_client() -> bool {
    while (true) {
        NET_StreamSocket* candidate = nullptr;
        if (NET_AcceptClient(server_sock_, &candidate) && candidate) {
            // Wait for connection handshake to complete.
            while (NET_GetConnectionStatus(candidate) == 0) { SDL_Delay(10); }
            if (NET_GetConnectionStatus(candidate) < 0) {
                NET_DestroyStreamSocket(candidate);
                continue;
            }
            client_sock_ = candidate;
            DebugLog(DL::Info, DC::Main) << "[coop] client connected";
            coop_session::get().mode = coop_mode::host;
            return true;
        }
        SDL_Delay(100);
    }
}

auto coop_server::handshake() -> bool {
    // TODO: Phase 2
    return true;
}

auto coop_server::spawn_proxy_npc(
    const tripoint_ms& /*spawn_pos*/, const std::string& /*player_name*/) -> npc* {
    // TODO: Phase 3
    return nullptr;
}

auto coop_server::update_proxy_position(npc* /*proxy*/) -> void {
    // TODO: Phase 3
}

auto coop_server::start_receiver_thread() -> void {
    running_ = true;
    receiver_thread_ = std::jthread([this](std::stop_token /*st*/) { receiver_loop(); });
}

auto coop_server::receiver_loop() -> void {
    std::string buf;
    while (running_) {
        if (!coop_net::recv(client_sock_, buf, 100)) {
            if (running_) {
                DebugLog(DL::Info, DC::Main) << "[coop] receiver: client disconnected";
            }
            break;
        }
        // TODO: Phase 4 — parse buf, dispatch to queues
    }
    running_ = false;
}

auto coop_server::push_action(action_entry e) -> void {
    std::scoped_lock lk{action_mtx_};
    action_q_.push_back(std::move(e));
}

auto coop_server::try_pop_action() -> std::optional<action_entry> {
    std::scoped_lock lk{action_mtx_};
    if (action_q_.empty()) { return std::nullopt; }
    auto e = std::move(action_q_.front());
    action_q_.pop_front();
    return e;
}

auto coop_server::try_pop_chat() -> std::optional<chat_entry> {
    std::scoped_lock lk{chat_mtx_};
    if (chat_q_.empty()) { return std::nullopt; }
    auto e = std::move(chat_q_.front());
    chat_q_.pop_front();
    return e;
}

auto coop_server::coop_world_tick() -> void {
    if (!coop_session::get().is_host() || !running_) { return; }
    // TODO: Phase 4 — drain action, execute on proxy, post_action_world_step, sync
}

auto coop_server::execute_client_action(
    npc* /*proxy*/, const std::string& /*key*/, const std::string& /*ctx_json*/) -> void {
    // TODO: Phase 4
}

auto coop_server::build_and_send_sync() -> void {
    // TODO: Phase 5+
}

auto coop_server::shutdown() -> void {
    if (!running_.exchange(false)) { return; }
    if (receiver_thread_.joinable()) {
        receiver_thread_.request_stop();
        receiver_thread_.join();
    }
    if (client_sock_) {
        NET_DestroyStreamSocket(client_sock_);
        client_sock_ = nullptr;
    }
    if (server_sock_) {
        NET_DestroyServer(server_sock_);
        server_sock_ = nullptr;
    }
    NET_Quit();
    coop_session::get().mode = coop_mode::none;
    DebugLog(DL::Info, DC::Main) << "[coop] server shutdown";
}

#endif // COOP_ENABLED
