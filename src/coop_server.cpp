#ifdef COOP_ENABLED

#include "coop_server.h"

#include "coop_net.h"
#include "coop_session.h"
#include "calendar.h"
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
#include "npc.h"
#include "overmapbuffer.h"
#include "submap.h"

#include <SDL3_net/SDL_net.h>
#include <sstream>

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

auto coop_server::try_accept() -> bool {
    if (client_sock_) { return true; }
    NET_StreamSocket* candidate = nullptr;
    if (!NET_AcceptClient(server_sock_, &candidate) || !candidate) {
        return false;
    }
    while (NET_GetConnectionStatus(candidate) == 0) { SDL_Delay(10); }
    if (NET_GetConnectionStatus(candidate) < 0) {
        NET_DestroyStreamSocket(candidate);
        return false;
    }
    client_sock_ = candidate;
    coop_session::get().mode = coop_mode::host;
    DebugLog(DL::Info, DC::Main) << "[coop] client connected";
    return true;
}

auto coop_server::wait_for_client() -> bool {
    // Short-circuit if try_accept() already stored the client.
    if (client_sock_) { return true; }
    while (true) {
        if (try_accept()) { return true; }
        SDL_Delay(100);
    }
}

auto coop_server::handshake() -> bool {
    // Build and send host handshake
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
    if (!coop_net::send(client_sock_, oss.str())) {
        DebugLog(DL::Error, DC::Main) << "[coop] handshake: send failed";
        return false;
    }

    // Receive client handshake
    std::string buf;
    if (!coop_net::recv(client_sock_, buf, 5000)) {
        DebugLog(DL::Error, DC::Main) << "[coop] handshake: recv failed";
        return false;
    }

    std::istringstream iss(buf);
    JsonIn jin(iss);
    JsonObject pkt = jin.get_object();
    pkt.allow_omitted_members();
    JsonObject d = pkt.get_object("d");
    d.allow_omitted_members();
    const std::string client_ver = d.get_string("version", "");
    if (client_ver != getVersionString()) {
        DebugLog(DL::Info, DC::Main)
            << "[coop] version mismatch: host=" << getVersionString()
            << " client=" << client_ver;
        // warn but allow
    }

    DebugLog(DL::Info, DC::Main) << "[coop] handshake complete";
    return true;
}

auto coop_server::spawn_proxy_npc(
    const tripoint_abs_ms& spawn_pos, const std::string& player_name) -> npc* {
    shared_ptr_fast<npc> tmp = make_shared_fast<npc>();
    tmp->randomize();
    if (!player_name.empty()) {
        tmp->name = player_name;
    }
    tmp->is_coop_remote = true;
    tmp->set_attitude(NPCATT_FOLLOW);
    tmp->mission = NPC_MISSION_NULL;

    // Use spawn_at_precise, matching the game.cpp follower spawn pattern exactly.
    const auto proj = project_remain<coords::sm>(spawn_pos);
    tmp->spawn_at_precise(proj.quotient, proj.remainder_tripoint);

    // Register in the overmapbuffer (same as all other NPC spawns in game.cpp).
    const character_id npc_id = tmp->getID();
    get_overmapbuffer(g->get_current_dimension_id()).insert_npc(tmp);

    // Mark as follower and pull into the active_npc list.
    g->add_npc_follower(npc_id);
    g->load_npcs();

    // Store the ID in the session so coop_world_tick() can find it.
    coop_session::get().proxy_npc_id = npc_id;

    // Retrieve the now-active pointer.
    npc* ptr = g->critter_by_id<npc>(npc_id);
    if (ptr) {
        ptr->is_coop_remote = true; // re-set after load_npcs in case it was cleared
        DebugLog(DL::Info, DC::Main) << "[coop] proxy NPC spawned: " << ptr->name;
    } else {
        DebugLog(DL::Error, DC::Main) << "[coop] proxy NPC not found after load_npcs";
    }
    return ptr;
}

auto coop_server::update_proxy_position(npc* /*proxy*/) -> void {
    // Phase 3.5: update second bubble center — deferred
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

        // Parse JSON envelope
        try {
            std::istringstream iss(buf);
            JsonIn jin(iss);
            JsonObject pkt = jin.get_object();
            pkt.allow_omitted_members();
            const auto t = static_cast<coop_pkt>(pkt.get_int("t"));

            if (t == coop_pkt::action) {
                JsonObject d = pkt.get_object("d");
                d.allow_omitted_members();
                push_action({d.get_string("key", ""), d.get_string("ctx", "")});
            } else if (t == coop_pkt::chat) {
                JsonObject d = pkt.get_object("d");
                d.allow_omitted_members();
                std::scoped_lock lk{chat_mtx_};
                chat_q_.push_back({d.get_string("text", "")});
            }
            // other packet types silently ignored until later phases
        } catch (const JsonError& e) {
            DebugLog(DL::Error, DC::Main) << "[coop] receiver: JSON parse error: " << e.what();
        }
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

    // 1. Drain one client action and execute on proxy NPC
    auto act = try_pop_action();
    npc* proxy = g->critter_by_id<npc>(coop_session::get().proxy_npc_id);
    if (proxy && act) {
        execute_client_action(proxy, act->key, act->ctx_json);
    }

    // 2. Update proxy bubble position (stub until Phase 3.5)
    if (proxy) { update_proxy_position(proxy); }

    // 3. World simulation
    g->post_action_world_step();

    // 4. Build and send sync (stub until Phase 5+)
    build_and_send_sync();

    // 5. Drain chat messages
    if (auto msg = try_pop_chat()) {
        add_msg(m_info, "[partner]: %s", msg->text);
    }
}

auto coop_server::execute_client_action(
    npc* proxy, const std::string& key, const std::string& ctx_json) -> void {
    if (!proxy) { return; }

    const tripoint_bub_ms cur = proxy->bub_pos();

    // Move to destination, or melee-attack any monster already standing there.
    const auto move_or_melee = [&](const tripoint_bub_ms& dest) {
        if (const auto mon_ptr = g->critter_tracker->find(dest)) {
            proxy->melee_attack(*mon_ptr, true);
        } else {
            proxy->move_to(dest);
        }
    };

    if (key == "MOVE_N" || key == "UP") {
        move_or_melee(cur + tripoint(0, -1, 0));
    } else if (key == "MOVE_S" || key == "DOWN") {
        move_or_melee(cur + tripoint(0, 1, 0));
    } else if (key == "MOVE_E" || key == "RIGHT") {
        move_or_melee(cur + tripoint(1, 0, 0));
    } else if (key == "MOVE_W" || key == "LEFT") {
        move_or_melee(cur + tripoint(-1, 0, 0));
    } else if (key == "MOVE_NE") {
        move_or_melee(cur + tripoint(1, -1, 0));
    } else if (key == "MOVE_NW") {
        move_or_melee(cur + tripoint(-1, -1, 0));
    } else if (key == "MOVE_SE") {
        move_or_melee(cur + tripoint(1, 1, 0));
    } else if (key == "MOVE_SW") {
        move_or_melee(cur + tripoint(-1, 1, 0));
    } else if (key == "SMASH") {
        if (!ctx_json.empty()) {
            std::istringstream iss(ctx_json);
            JsonIn jin(iss);
            JsonObject ctx = jin.get_object();
            ctx.allow_omitted_members();
            const tripoint_bub_ms tpos{
                ctx.get_int("tx", cur.x()),
                ctx.get_int("ty", cur.y()),
                ctx.get_int("tz", cur.z())
            };
            if (const auto mon_ptr = g->critter_tracker->find(tpos)) {
                proxy->melee_attack(*mon_ptr, true);
            }
        }
    } else if (key == "FIRE") {
        if (!ctx_json.empty()) {
            std::istringstream iss(ctx_json);
            JsonIn jin(iss);
            JsonObject ctx = jin.get_object();
            ctx.allow_omitted_members();
            DebugLog(DL::Info, DC::Main)
                << "[coop] FIRE from proxy: target="
                << ctx.get_int("tx", 0) << "," << ctx.get_int("ty", 0);
        } else {
            DebugLog(DL::Info, DC::Main) << "[coop] FIRE from proxy: no target context";
        }
    } else if (key == "PAUSE" || key == "WAIT") {
        proxy->moves -= proxy->get_speed();
    } else {
        DebugLog(DL::Debug, DC::Main)
            << "[coop] execute_client_action: unimplemented key=" << key;
    }
}

auto coop_server::build_and_send_sync() -> void {
    std::ostringstream oss;
    JsonOut jout(oss);
    jout.start_object();
    jout.member("t", static_cast<int>(coop_pkt::sync));
    jout.member("turn", to_turn<int>(calendar::turn));

    // Serialize a 5×5 submap grid centred on the player.
    jout.member("tiles");
    jout.start_array();
    const tripoint_abs_sm abs_sub = g->m.get_abs_sub();
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            const tripoint_abs_sm sm_pos{abs_sub.x() + dx, abs_sub.y() + dy, abs_sub.z()};
            const submap* sm = MAPBUFFER.lookup_submap(sm_pos);
            if (!sm) { continue; }
            jout.start_object();
            jout.member("x", sm_pos.x());
            jout.member("y", sm_pos.y());
            jout.member("z", sm_pos.z());
            sm->store(jout);
            jout.end_object();
        }
    }
    jout.end_array();

    // Serialize live monsters as absolute positions so the client can
    // place them correctly regardless of its own map origin.
    jout.member("monsters");
    jout.start_array();
    for (monster& mon : g->all_monsters()) {
        if (mon.is_dead()) { continue; }
        const tripoint_abs_ms apos = mon.abs_pos();
        jout.start_object();
        jout.member("type", mon.type->id.str());
        jout.member("ax", apos.x());
        jout.member("ay", apos.y());
        jout.member("az", apos.z());
        jout.member("hp", mon.get_hp());
        jout.member("hp_max", mon.get_hp_max());
        jout.end_object();
    }
    jout.end_array();

    jout.end_object();
    coop_net::send(client_sock_, oss.str());
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
