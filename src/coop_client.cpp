#ifdef COOP_ENABLED

#include "coop_client.h"

#include "coop_net.h"
#include "coop_session.h"
#include "debug.h"
#include "get_version.h"
#include "json.h"

#include <SDL3_net/SDL_net.h>
#include <sstream>

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
    // Send client handshake first (client goes second per the protocol, but
    // both sides send then receive — order matches coop_server::handshake()).
    std::ostringstream oss;
    {
        JsonOut jout(oss);
        jout.start_object();
        jout.member("t", static_cast<int>(coop_pkt::handshake));
        jout.member("d");
        jout.start_object();
        jout.member("version", std::string(getVersionString()));
        jout.member("mods");
        jout.start_array();
        jout.end_array();
        jout.member("mod_hash", std::string(""));
        jout.end_object();
        jout.end_object();
    }
    if (!coop_net::send(socket_, oss.str())) {
        DebugLog(DL::Error, DC::Main) << "[coop] client handshake: send failed";
        return false;
    }

    // Receive host handshake
    std::string buf;
    if (!coop_net::recv(socket_, buf, 5000)) {
        DebugLog(DL::Error, DC::Main) << "[coop] client handshake: recv failed";
        return false;
    }
    std::istringstream iss(buf);
    JsonIn jin(iss);
    JsonObject pkt = jin.get_object();
    pkt.allow_omitted_members();
    JsonObject d = pkt.get_object("d");
    d.allow_omitted_members();
    const std::string host_ver = d.get_string("version", "");
    if (host_ver != getVersionString()) {
        DebugLog(DL::Info, DC::Main)
            << "[coop] version mismatch: client=" << getVersionString()
            << " host=" << host_ver;
    }
    DebugLog(DL::Info, DC::Main) << "[coop] client handshake complete";
    return true;
}

auto coop_client::coop_world_tick() -> void {
    if (!coop_session::get().is_client() || !socket_) { return; }

    // 1. Send one queued action (non-blocking — just serialise and push onto TCP).
    if (!action_q_.empty()) {
        const auto act = action_q_.front();
        action_q_.pop_front();
        std::ostringstream oss;
        JsonOut jout(oss);
        jout.start_object();
        jout.member("t", static_cast<int>(coop_pkt::action));
        jout.member("d");
        jout.start_object();
        jout.member("key", act.key);
        jout.member("ctx", act.ctx_json);
        jout.end_object();
        jout.end_object();
        if (!coop_net::send(socket_, oss.str())) {
            DebugLog(DL::Error, DC::Main) << "[coop] coop_world_tick: action send failed";
            handle_disconnect();
            return;
        }
    }

    // 2. Non-blocking poll for COOP_SYNC from the host.
    if (coop_net::poll(socket_)) {
        std::string buf;
        if (!coop_net::recv(socket_, buf, 0)) {
            handle_disconnect();
            return;
        }
        apply_sync(buf);
    }
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
