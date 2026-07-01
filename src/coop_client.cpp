#ifdef COOP_ENABLED

#include "coop_client.h"

#include "calendar.h"
#include "coop_net.h"
#include "coop_session.h"
#include "coordinates.h"
#include "creature_tracker.h"
#include "debug.h"
#include "game.h"
#include "get_version.h"
#include "json.h"
#include "map.h"
#include "mapbuffer.h"
#include "messages.h"
#include "monster.h"
#include "submap.h"
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
            << "[coop] version mismatch: client=" << getVersionString() << " host=" << host_ver;
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

    // 2. Non-blocking poll for inbound packets from the host.
    if (coop_net::poll(socket_)) {
        std::string buf;
        if (!coop_net::recv(socket_, buf, 0)) {
            handle_disconnect();
            return;
        }
        try {
            std::istringstream iss(buf);
            JsonIn jin(iss);
            JsonObject pkt = jin.get_object();
            pkt.allow_omitted_members();
            const auto t = static_cast<coop_pkt>(pkt.get_int("t"));
            if (t == coop_pkt::sync) {
                apply_sync(buf);
            } else if (t == coop_pkt::chat) {
                JsonObject d = pkt.get_object("d");
                d.allow_omitted_members();
                add_msg(m_info, "[host]: %s", d.get_string("text", ""));
            } else if (t == coop_pkt::disconnect) {
                DebugLog(DL::Info, DC::Main) << "[coop] host sent disconnect";
                handle_disconnect();
                return;
            }
            // other packet types silently ignored
        } catch (const JsonError& e) {
            DebugLog(DL::Error, DC::Main)
                << "[coop] coop_world_tick: JSON parse error: " << e.what();
        }
    }
}

auto coop_client::queue_action(const std::string& key, const std::string& ctx_json) -> void {
    action_q_.push_back({key, ctx_json});
}

auto coop_client::apply_sync(const std::string& json_buf) -> void {
    std::istringstream iss(json_buf);
    JsonIn jin(iss);

    // Use raw JsonIn iteration to handle the mixed-member tile objects.
    // The outer sync object has: "t", "turn", "tiles", "monsters".
    jin.start_object();
    while (!jin.end_object()) {
        const std::string key = jin.get_member_name();

        if (key == "turn") {
            const int turn_val = jin.get_int();
            if (turn_val >= 0) { calendar::turn = time_point::from_turn(turn_val); }

        } else if (key == "tiles") {
            // Each entry is: { "version": N, "coordinates": [x,y,z], <submap members> }
            // This is the standard mapbuffer format — see mapbuffer::deserialize_into_vec.
            jin.start_array();
            while (!jin.end_array()) {
                int version = savegame_version;
                tripoint_abs_sm sm_pos;
                auto new_sm = std::unique_ptr<submap>{};

                jin.start_object();
                while (!jin.end_object()) {
                    const std::string tile_key = jin.get_member_name();
                    if (tile_key == "version") {
                        version = jin.get_int();
                    } else if (tile_key == "coordinates") {
                        jin.start_array();
                        const int x = jin.get_int();
                        const int y = jin.get_int();
                        const int z = jin.get_int();
                        jin.end_array();
                        sm_pos = tripoint_abs_sm{x, y, z};
                        new_sm = std::make_unique<submap>(sm_pos);
                    } else if (new_sm) {
                        // All other members are submap payload: terrain, furniture, items, etc.
                        new_sm->load(jin, tile_key, version, project_to<coords::ms>(sm_pos));
                    } else {
                        jin.skip_value();
                    }
                }

                if (new_sm) {
                    submap* existing = MAPBUFFER.lookup_submap_in_memory(sm_pos);
                    if (existing) {
                        // Atomically swap host-authoritative data into the live submap,
                        // then mark all caches dirty so the renderer sees updated tiles.
                        submap::swap(*existing, *new_sm);
                        existing->transparency_dirty = true;
                        existing->outside_dirty = true;
                        existing->floor_dirty = true;
                        existing->pf_dirty = true;
                    } else {
                        MAPBUFFER.add_submap(sm_pos, new_sm);
                    }
                }
            }
            // Invalidate the map's high-level visibility caches after bulk update.
            g->m.invalidate_visibility_caches();

        } else if (key == "monsters") {
            // Rebuild monster list from host state.
            for (monster& critter : g->all_monsters()) { g->despawn_monster(critter); }
            jin.start_array();
            while (!jin.end_array()) {
                jin.start_object();
                mtype_id type_id;
                tripoint_abs_ms apos;
                int hp = -1;
                bool dead = false;

                while (!jin.end_object()) {
                    const std::string mk = jin.get_member_name();
                    if (mk == "type") {
                        type_id = mtype_id(jin.get_string());
                    } else if (mk == "ax") {
                        apos.x() = jin.get_int();
                    } else if (mk == "ay") {
                        apos.y() = jin.get_int();
                    } else if (mk == "az") {
                        apos.z() = jin.get_int();
                    } else if (mk == "hp") {
                        hp = jin.get_int();
                    } else if (mk == "dead") {
                        dead = jin.get_bool();
                    } else {
                        jin.skip_value();
                    }
                }
                if (dead || type_id.is_empty()) { continue; }
                const tripoint_bub_ms bpos = g->m.abs_to_bub(apos);
                monster* mon = g->place_critter_at(type_id, bpos);
                if (mon && hp >= 0) { mon->set_hp(hp); }
            }
        } else {
            jin.skip_value();
        }
    }
}

auto coop_client::handle_disconnect() -> void { shutdown(); }

auto coop_client::shutdown() -> void {
    if (socket_) {
        {
            std::ostringstream oss;
            JsonOut jout(oss);
            jout.start_object();
            jout.member("t", static_cast<int>(coop_pkt::disconnect));
            jout.end_object();
            coop_net::send(socket_, oss.str());
        }
        NET_DestroyStreamSocket(socket_);
        socket_ = nullptr;
    }
    NET_Quit();
    coop_session::get().mode = coop_mode::none;
    DebugLog(DL::Info, DC::Main) << "[coop] client shutdown";
}

auto coop_client::send_chat(const std::string& text) -> void {
    if (!socket_) { return; }
    std::ostringstream oss;
    JsonOut jout(oss);
    jout.start_object();
    jout.member("t", static_cast<int>(coop_pkt::chat));
    jout.member("d");
    jout.start_object();
    jout.member("from", "client");
    jout.member("text", text);
    jout.end_object();
    jout.end_object();
    coop_net::send(socket_, oss.str());
}

#endif // COOP_ENABLED
