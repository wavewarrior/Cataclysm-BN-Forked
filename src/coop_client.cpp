#ifdef COOP_ENABLED

#include "coop_client.h"

#include "coop_net.h"
#include "coop_session.h"
#include "debug.h"

#include <SDL3_net/SDL_net.h>

coop_client::~coop_client() { shutdown(); }

auto coop_client::connect(const std::string& ip, uint16_t port) -> bool {
    if (!NET_Init()) {
        DebugLog(DL::Error, DC::Main) << "[coop] NET_Init failed: " << SDL_GetError();
        return false;
    }
    NET_Address* addr = NET_ResolveHostname(ip.c_str());
    if (!addr) {
        DebugLog(DL::Error, DC::Main) << "[coop] resolve failed: " << SDL_GetError();
        return false;
    }
    // Wait up to 5 seconds for DNS resolution.
    while (NET_GetAddressStatus(addr) == 0) { SDL_Delay(10); }
    if (NET_GetAddressStatus(addr) < 0) {
        NET_UnrefAddress(addr);
        DebugLog(DL::Error, DC::Main) << "[coop] DNS failed: " << SDL_GetError();
        return false;
    }
    socket_ = NET_CreateClient(addr, port, 0);
    NET_UnrefAddress(addr);
    if (!socket_) {
        DebugLog(DL::Error, DC::Main) << "[coop] connect failed: " << SDL_GetError();
        return false;
    }
    // Wait for non-blocking connect to complete.
    while (NET_GetConnectionStatus(socket_) == 0) { SDL_Delay(10); }
    if (NET_GetConnectionStatus(socket_) < 0) {
        NET_DestroyStreamSocket(socket_);
        socket_ = nullptr;
        DebugLog(DL::Error, DC::Main) << "[coop] connection refused: " << SDL_GetError();
        return false;
    }
    coop_session::get().mode = coop_mode::client;
    DebugLog(DL::Info, DC::Main) << "[coop] connected to " << ip << ":" << port;
    return true;
}

auto coop_client::handshake() -> bool {
    // TODO: Phase 2
    return true;
}

auto coop_client::coop_world_tick() -> void {
    if (!coop_session::get().is_client() || !socket_) { return; }
    // TODO: Phase 4 — send queued action, poll for COOP_SYNC
}

auto coop_client::queue_action(const std::string& key, const std::string& ctx_json) -> void {
    action_q_.push_back({key, ctx_json});
}

auto coop_client::apply_sync(const std::string& /*json_buf*/) -> void {
    // TODO: Phase 5+
}

auto coop_client::handle_disconnect() -> void { shutdown(); }

auto coop_client::shutdown() -> void {
    if (socket_) {
        NET_DestroyStreamSocket(socket_);
        socket_ = nullptr;
    }
    NET_Quit();
    coop_session::get().mode = coop_mode::none;
    DebugLog(DL::Info, DC::Main) << "[coop] client shutdown";
}

#endif // COOP_ENABLED
