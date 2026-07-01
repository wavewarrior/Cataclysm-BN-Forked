#ifdef COOP_ENABLED

#include "coop_client.h"

#include "coop_net.h"
#include "coop_session.h"
#include "calendar.h"
#include "creature_tracker.h"
#include "debug.h"
#include "game.h"
#include "get_version.h"
#include "json.h"
#include "map.h"
#include "monster.h"
#include "type_id.h"

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

auto coop_client::apply_sync(const std::string& json_buf) -> void {
    std::istringstream iss(json_buf);
    JsonIn jin(iss);
    JsonObject sync = jin.get_object();
    sync.allow_omitted_members(); // "tiles" section is not processed yet

    // Advance client calendar to match host.
    const int turn_val = sync.get_int("turn", -1);
    if (turn_val >= 0) {
        calendar::turn = time_point::from_turn(turn_val);
    }

    // Rebuild monster list from host state.
    // Clear existing monsters first (safe: all_monsters() snapshots weak_ptrs).
    for (monster& critter : g->all_monsters()) {
        g->despawn_monster(critter);
    }
    JsonArray monsters = sync.get_array("monsters");
    while (monsters.has_more()) {
        JsonObject mon_data = monsters.next_object();
        mon_data.allow_omitted_members();
        const mtype_id type_id(mon_data.get_string("type"));
        // Monster positions are sent as absolute coords (tripoint_abs_ms) so
        // they land correctly regardless of which map origin each side has.
        const tripoint_abs_ms apos{
            mon_data.get_int("ax"), mon_data.get_int("ay"), mon_data.get_int("az")
        };
        const tripoint_bub_ms bpos = g->m.abs_to_bub(apos);
        monster* mon = g->place_critter_at(type_id, bpos);
        if (mon) {
            mon->set_hp(mon_data.get_int("hp", mon->get_hp()));
        }
    }
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
