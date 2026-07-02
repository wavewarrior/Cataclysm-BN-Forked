#ifdef COOP_ENABLED

#include "coop_client.h"

#include "avatar.h"
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
#include <unordered_set>

coop_client::~coop_client() { shutdown(); }

auto coop_client::connect(const std::string& ip, uint16_t port) -> bool {
    if (!NET_Init()) {
        DebugLog(DL::Error, DC::Main) << "[coop] NET_Init failed: " << SDL_GetError();
        return false;
    }
    net_initialized_ = true;
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

    // 1b. Send client_status each tick — host uses this for both_idle() fast-forward.
    //     Reports the client's OWN g->u state; proxy-inference is unreliable since
    //     SLEEP/CRAFT stubs don't set proxy->activity.
    {
        const bool idle = g->u.in_sleep_state() || bool(g->u.activity);
        std::ostringstream status_oss;
        JsonOut status_jout(status_oss);
        status_jout.start_object();
        status_jout.member("t", static_cast<int>(coop_pkt::client_status));
        status_jout.member("d");
        status_jout.start_object();
        status_jout.member("idle", idle);
        status_jout.end_object();
        status_jout.end_object();
        if (!coop_net::send(socket_, status_oss.str())) {
            DebugLog(DL::Error, DC::Main) << "[coop] coop_world_tick: status send failed";
            handle_disconnect();
            return;
        }
    }

    // 2. Drain all buffered inbound packets from the host (H6).
    while (coop_net::poll(socket_)) {
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

    // Capture turn before parsing so we know how many turns this sync advances.
    // During fast-forward the host may burst 2–3 turns per outer-loop cycle;
    // each turn needs exactly one process_turn() call to keep avatar stats in sync.
    const auto turn_before = calendar::turn;

    // Use raw JsonIn iteration to handle the mixed-member tile objects.
    // The outer sync object has: "t", "turn", "tiles", "monsters", "proxy_*", "host_*".
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
            // H5: delta-update by host-assigned stable ID.
            // Server assigns sequential IDs to monster pointers (stable in creature_tracker);
            // client tracks host_id → local monster*.  Stationary monsters are updated in-place
            // (no respawn, references stay valid).  Moving monsters get despawned/respawned once
            // per move step — still far better than every-tick full respawn.
            std::unordered_set<int> received_ids;
            jin.start_array();
            while (!jin.end_array()) {
                jin.start_object();
                int host_id = -1;
                mtype_id type_id;
                tripoint_abs_ms apos;
                int hp = -1;
                bool dead = false;

                while (!jin.end_object()) {
                    const auto mk = jin.get_member_name();
                    if (mk == "id") {
                        host_id = jin.get_int();
                    } else if (mk == "type") {
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
                if (dead || type_id.is_empty() || host_id < 0) { continue; }
                received_ids.insert(host_id);

                const tripoint_bub_ms bpos = g->m.abs_to_bub(apos);
                const auto it = coop_monster_map_.find(host_id);
                if (it != coop_monster_map_.end() && it->second && !it->second->is_dead()) {
                    monster& existing = *it->second;
                    if (existing.bub_pos() == bpos) {
                        // Same position — update hp in-place, no respawn.
                        if (hp >= 0) { existing.set_hp(hp); }
                    } else {
                        // Monster moved — despawn old, spawn at new position, update map.
                        g->despawn_monster(existing);
                        monster* mon = g->place_critter_at(type_id, bpos);
                        it->second = mon;
                        if (mon && hp >= 0) { mon->set_hp(hp); }
                    }
                } else {
                    // New or stale entry — spawn and track.
                    monster* mon = g->place_critter_at(type_id, bpos);
                    coop_monster_map_[host_id] = mon;
                    if (mon && hp >= 0) { mon->set_hp(hp); }
                }
            }

            // Despawn locals whose host_id was absent from the sync; remove from map.
            std::erase_if(coop_monster_map_, [&](const auto& kv) {
                if (received_ids.contains(kv.first)) { return false; }
                if (kv.second && !kv.second->is_dead()) { g->despawn_monster(*kv.second); }
                return true;
            });
        } else if (key == "proxy_ax") {
            sync_proxy_apos_.x() = jin.get_int();
        } else if (key == "proxy_ay") {
            sync_proxy_apos_.y() = jin.get_int();
        } else if (key == "proxy_az") {
            sync_proxy_apos_.z() = jin.get_int();
            // Reconcile: if our local position has drifted far from the host's
            // canonical proxy position, teleport back.  Small drift (≤5 tiles) is
            // acceptable — local prediction runs ahead by up to one action.
            // Large drift means we hit a wall the proxy didn't, or vice versa.
            const auto client_apos = g->u.abs_pos();
            // Compute drift as raw integer deltas — avoids coordinate-type arithmetic.
            const int dx = client_apos.x() - sync_proxy_apos_.x();
            const int dy = client_apos.y() - sync_proxy_apos_.y();
            const int dz = client_apos.z() - sync_proxy_apos_.z();
            if (std::abs(dx) > 5 || std::abs(dy) > 5 || dz != 0) {
                const tripoint_bub_ms bpos = g->m.abs_to_bub(sync_proxy_apos_);
                g->u.setpos(bpos);
                DebugLog(DL::Info, DC::Main)
                    << "[coop] client reconciled position: drift=" << dx << "," << dy << "," << dz;
            }
        } else if (key == "host_ax") {
            sync_host_apos_.x() = jin.get_int();
        } else if (key == "host_ay") {
            sync_host_apos_.y() = jin.get_int();
        } else if (key == "host_az") {
            sync_host_apos_.z() = jin.get_int();
        } else {
            jin.skip_value();
        }
    }

    // Process turns for the delta between turn_before and the new calendar::turn.
    // During fast-forward the host may advance N turns in one burst; without this
    // loop the client's avatar skips N-1 process_turn() calls → wakes unrested,
    // heals incorrectly, effects don't tick, etc.  Capped at MAX_CATCH_UP (3) to
    // match the host's burst limit and prevent blocking the render loop.
    const int turns_advanced =
        std::max(0, to_turn<int>(calendar::turn) - to_turn<int>(turn_before));
    constexpr int MAX_PROCESS_CATCH_UP = 3;
    const int catch_up = std::min(turns_advanced, MAX_PROCESS_CATCH_UP);
    for (int i = 0; i < catch_up; ++i) { g->u.process_turn(); }
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
    if (net_initialized_) {
        NET_Quit();
        net_initialized_ = false;
    }
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
