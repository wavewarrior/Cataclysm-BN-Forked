#ifdef COOP_ENABLED

#include "coop_server.h"

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
#include "npc.h"
#include "overmapbuffer.h"
#include "submap.h"

#include <SDL3_net/SDL_net.h>
#include <sstream>

coop_server::~coop_server() { shutdown(); }

auto coop_server::listen( uint16_t port ) -> bool
{
    if( !NET_Init() ) {
    DebugLog( DL::Error, DC::Main ) << "[coop] NET_Init failed: " << SDL_GetError();
        return false;
    }
    net_initialized_ = true;
    server_sock_ = NET_CreateServer( nullptr, port, 0 );
    if( !server_sock_ ) {
    DebugLog( DL::Error, DC::Main ) << "[coop] NET_CreateServer failed: " << SDL_GetError();
        return false;
    }
    DebugLog( DL::Info, DC::Main ) << "[coop] listening on port " << port;
    return true;
}

auto coop_server::try_accept() -> bool
{
    if( client_sock_ ) { return true; }
NET_StreamSocket* candidate = nullptr;
if( !NET_AcceptClient( server_sock_, &candidate ) || !candidate ) { return false; }
    while( NET_GetConnectionStatus( candidate ) == 0 ) { SDL_Delay( 10 ); }
    if( NET_GetConnectionStatus( candidate ) < 0 ) {
        NET_DestroyStreamSocket( candidate );
        return false;
    }
    client_sock_ = candidate;
    coop_session::get().mode = coop_mode::host;
    DebugLog( DL::Info, DC::Main ) << "[coop] client connected";
    return true;
}

auto coop_server::wait_for_client() -> bool
{
    // Short-circuit if try_accept() already stored the client.
    if( client_sock_ ) { return true; }
while( true ) {
    if( try_accept() ) { return true; }
        SDL_Delay( 100 );
    }
}

auto coop_server::handshake() -> bool
{
    // Build and send host handshake
    std::ostringstream oss;
    {
        JsonOut jout( oss );
        jout.start_object();
        jout.member( "t", static_cast<int>( coop_pkt::handshake ) );
        jout.member( "d" );
        jout.start_object();
        jout.member( "version", std::string( getVersionString() ) );
        jout.member( "mods" );
        jout.start_array();
        jout.end_array();
        jout.member( "mod_hash", std::string( "" ) );
        jout.end_object();
        jout.end_object();
    }
    if( !coop_net::send( client_sock_, oss.str() ) ) {
        DebugLog( DL::Error, DC::Main ) << "[coop] handshake: send failed";
        return false;
    }

    // Receive client handshake
    std::string buf;
    if( !coop_net::recv( client_sock_, buf, 5000 ) ) {
        DebugLog( DL::Error, DC::Main ) << "[coop] handshake: recv failed";
        return false;
    }

    std::istringstream iss( buf );
    JsonIn jin( iss );
    JsonObject pkt = jin.get_object();
    pkt.allow_omitted_members();
    JsonObject d = pkt.get_object( "d" );
    d.allow_omitted_members();
    const std::string client_ver = d.get_string( "version", "" );
    if( client_ver != getVersionString() ) {
        DebugLog( DL::Info, DC::Main )
                << "[coop] version mismatch: host=" << getVersionString() << " client=" << client_ver;
        // warn but allow
    }

    DebugLog( DL::Info, DC::Main ) << "[coop] handshake complete";
    return true;
}

auto coop_server::spawn_proxy_npc( const tripoint_abs_ms& spawn_pos,
                                   const std::string& player_name )
-> npc *
{
    shared_ptr_fast<npc> tmp = make_shared_fast<npc>();
    tmp->randomize();
    // L7: strip randomize()'s garbage inventory/worn before hooks see the proxy NPC.
    // inv_clear() is the public API (character.h:1275); worn.clear() is safe here
    // because the NPC hasn't been registered with the game world yet, so
    // location_vector tracking hasn't started for its items.
    tmp->inv_clear();
    tmp->worn.clear();
    if( !player_name.empty() ) { tmp->name = player_name; }
    tmp->is_coop_remote = true;
    tmp->set_attitude( NPCATT_FOLLOW );
    tmp->mission = NPC_MISSION_NULL;

    // Use spawn_at_precise, matching the game.cpp follower spawn pattern exactly.
    const auto proj = project_remain<coords::sm>( spawn_pos );
    tmp->spawn_at_precise( proj.quotient, proj.remainder_tripoint );

    // Register in the overmapbuffer (same as all other NPC spawns in game.cpp).
    const character_id npc_id = tmp->getID();
    get_overmapbuffer( g->get_current_dimension_id() ).insert_npc( tmp );

    // Mark as follower and pull into the active_npc list.
    g->add_npc_follower( npc_id );
    g->load_npcs();

    // Store the ID in the session so coop_world_tick() can find it.
    coop_session::get().proxy_npc_id = npc_id;

    // Retrieve the now-active pointer.
    npc* ptr = g->critter_by_id<npc>( npc_id );
    if( ptr ) {
        ptr->is_coop_remote = true; // re-set after load_npcs in case it was cleared
        DebugLog( DL::Info, DC::Main ) << "[coop] proxy NPC spawned: " << ptr->name;
    } else {
        DebugLog( DL::Error, DC::Main ) << "[coop] proxy NPC not found after load_npcs";
    }
    return ptr;
}

auto coop_server::send_world_seed( const std::string& player_name ) -> bool
{
    std::ostringstream oss;
    JsonOut jout( oss );
    jout.start_object();
    jout.member( "t", static_cast<int>( coop_pkt::world_seed ) );
    jout.member( "d" );
    jout.start_object();
    jout.member( "turn", to_turn<int>( calendar::turn ) );
    const tripoint_abs_ms hpos = g->u.abs_pos();
    jout.member( "spawn_x", hpos.x() );
    jout.member( "spawn_y", hpos.y() );
    jout.member( "spawn_z", hpos.z() );
    jout.member( "player_name", player_name );
    jout.member( "world_name", g->get_active_world()->info->world_name );
    jout.end_object();
    jout.end_object();
    return coop_net::send( client_sock_, oss.str() );
}

auto coop_server::send_initial_sync() -> bool
{
    // Reset tile-sync state so the first regular coop_world_tick
    // does not send another full blast.
    last_sync_origin_ = g->m.get_abs_sub();
    sync_tick_counter_ = 0;
    // Reuse build_and_send_sync logic: push to send_q_ then flush directly.
    // The receiver thread is not yet running, so we must drain send_q_ here.
    build_and_send_sync( true );
    std::deque<std::string> outgoing;
    {
        std::scoped_lock lk{send_mtx_};
        outgoing.swap( send_q_ );
    }
    for( const auto& frame : outgoing ) {
        if( !coop_net::send( client_sock_, frame ) ) { return false; }
    }
    return true;
}

auto coop_server::update_proxy_position( npc* /*proxy*/ ) -> void
{
    // Phase 3.5: update second bubble center — deferred
}

auto coop_server::start_receiver_thread() -> void
{
    running_ = true;
    receiver_thread_ = std::jthread( [this]( std::stop_token st ) { receiver_loop( st ); } );
}

auto coop_server::receiver_loop( std::stop_token st ) -> void
{
    std::string buf;
    while( running_ && !st.stop_requested() ) {
        // Drain outgoing send queue first — IO thread is the sole caller of send.
        {
            std::deque<std::string> outgoing;
            {
                std::scoped_lock lk{send_mtx_};
                outgoing.swap( send_q_ );
            }
            for( const auto& frame : outgoing ) { coop_net::send( client_sock_, frame ); }
        }

        // Non-blocking check: skip recv if nothing is waiting.
        if( !coop_net::poll( client_sock_ ) ) {
            SDL_Delay( 1 );
            continue;
        }

        if( !coop_net::recv( client_sock_, buf, 5000 ) ) {
            if( running_ ) {
                DebugLog( DL::Info, DC::Main ) << "[coop] receiver: client disconnected";
            }
            break;
        }

        // Parse JSON envelope
        try {
            std::istringstream iss( buf );
            JsonIn jin( iss );
            JsonObject pkt = jin.get_object();
            pkt.allow_omitted_members();
            const auto t = static_cast<coop_pkt>( pkt.get_int( "t" ) );

            if( t == coop_pkt::action ) {
                JsonObject d = pkt.get_object( "d" );
                d.allow_omitted_members();
                push_action( {d.get_string( "key", "" ), d.get_string( "ctx", "" )} );
            } else if( t == coop_pkt::chat ) {
                JsonObject d = pkt.get_object( "d" );
                d.allow_omitted_members();
                std::string text = d.get_string( "text", "" );
                if( text.size() > 512 ) { text = text.substr( 0, 512 ); }
                std::scoped_lock lk{chat_mtx_};
                if( chat_q_.size() >= 64 ) { chat_q_.pop_front(); }
                chat_q_.push_back( {std::move( text )} );
            } else if( t == coop_pkt::client_status ) {
                // Client reports its own idle state (sleeping/long activity) so
                // both_idle() doesn't have to guess from the stubbed proxy state.
                JsonObject d = pkt.get_object( "d" );
                d.allow_omitted_members();
                client_is_idle_.store( d.get_bool( "idle", false ) );
            } else if( t == coop_pkt::disconnect ) {
                DebugLog( DL::Info, DC::Main ) << "[coop] receiver: client sent disconnect";
                running_ = false;
                break;
            }
            // other packet types silently ignored until later phases
        } catch( const JsonError& e ) {
            DebugLog( DL::Error, DC::Main ) << "[coop] receiver: JSON parse error: " << e.what();
        }
    }
    running_ = false;
}

auto coop_server::push_action( action_entry e ) -> void
{
    std::scoped_lock lk{action_mtx_};
    if( action_q_.size() >= 32 ) { action_q_.pop_front(); }
    action_q_.push_back( std::move( e ) );
}

auto coop_server::try_pop_action() -> std::optional<action_entry>
{
    std::scoped_lock lk{action_mtx_};
    if( action_q_.empty() ) { return std::nullopt; }
    auto e = std::move( action_q_.front() );
    action_q_.pop_front();
    return e;
}

auto coop_server::try_pop_chat() -> std::optional<chat_entry>
{
    std::scoped_lock lk{chat_mtx_};
    if( chat_q_.empty() ) { return std::nullopt; }
    auto e = std::move( chat_q_.front() );
    chat_q_.pop_front();
    return e;
}

auto coop_server::coop_world_tick() -> void
{
    if( !coop_session::get().is_host() || !running_ ) { return; }

// 1. World simulation first — process_turn() inside post_action_world_step()
//    grants the proxy its move budget via mod_moves(get_speed()).
//    npcmove() skips AI for is_coop_remote without zeroing moves, so the
//    proxy exits the sim with fresh moves ready to consume below.
g->post_action_world_step();

// 2. Drain client actions NOW — proxy has valid moves from process_turn().
npc* proxy = g->critter_by_id<npc>( coop_session::get().proxy_npc_id );
while( proxy && proxy->moves > 0 ) {
    const auto act = try_pop_action();
        if( !act ) { break; }
        execute_client_action( proxy, act->key, act->ctx_json );
    }
    // Explicit consume: prevent residual moves carrying into next tick's sim.
    if( proxy ) { proxy->set_moves( 0 ); }

    // 3. Build and send sync (tiles only when host submap changes; always sends
    //    monsters + turn + proxy position).
    build_and_send_sync();

    // 4. Drain ALL pending chat messages — not just one per tick.
    while( auto msg = try_pop_chat() ) { add_msg( m_info, "[partner]: %s", msg->text ); }

    // 5. Fast-forward when both players are engaged in long activities (sleep,
    //    craft, read, wait…).  At 1 tick/sec no streak threshold needed — react
    //    immediately by saturating the accumulator to MAX_CATCH_UP (3 s).
    if( both_idle() ) { g->main_loop_accum_ms_ = 3000.0; }
}

auto coop_server::both_idle() const -> bool
{
    const auto host_idle = g->u.in_sleep_state() || bool( g->u.activity );
    const auto client_idle = client_is_idle_.load();
    return host_idle && client_idle;
}

auto coop_server::has_pending_actions() const -> bool
{
    std::scoped_lock lk{action_mtx_};
    return !action_q_.empty();
}

auto coop_server::execute_client_action(
    npc* proxy, const std::string& key, const std::string& ctx_json ) -> void
{
    if( !proxy ) { return; }

const tripoint_bub_ms cur = proxy->bub_pos();

if( key == "MOVE_N" || key == "UP" ) {
    proxy->move_to( cur + tripoint( 0, -1, 0 ) );
    } else if( key == "MOVE_S" || key == "DOWN" ) {
    proxy->move_to( cur + tripoint( 0, 1, 0 ) );
    } else if( key == "MOVE_E" || key == "RIGHT" ) {
    proxy->move_to( cur + tripoint( 1, 0, 0 ) );
    } else if( key == "MOVE_W" || key == "LEFT" ) {
    proxy->move_to( cur + tripoint( -1, 0, 0 ) );
    } else if( key == "MOVE_NE" ) {
    proxy->move_to( cur + tripoint( 1, -1, 0 ) );
    } else if( key == "MOVE_NW" ) {
    proxy->move_to( cur + tripoint( -1, -1, 0 ) );
    } else if( key == "MOVE_SE" ) {
    proxy->move_to( cur + tripoint( 1, 1, 0 ) );
    } else if( key == "MOVE_SW" ) {
    proxy->move_to( cur + tripoint( -1, 1, 0 ) );
    } else if( key == "SMASH" ) {
    if( !ctx_json.empty() ) {
            std::istringstream iss( ctx_json );
            JsonIn jin( iss );
            JsonObject ctx = jin.get_object();
            ctx.allow_omitted_members();
            const tripoint_bub_ms tpos{
                ctx.get_int( "tx", cur.x() ), ctx.get_int( "ty", cur.y() ), ctx.get_int( "tz", cur.z() )};
            if( const auto mon_ptr = g->critter_tracker->find( tpos ) ) {
                proxy->melee_attack( *mon_ptr, true );
            }
        }
    } else if( key == "FIRE" ) {
    if( !ctx_json.empty() ) {
            std::istringstream iss( ctx_json );
            JsonIn jin( iss );
            JsonObject ctx = jin.get_object();
            ctx.allow_omitted_members();
            DebugLog( DL::Info, DC::Main )
                    << "[coop] FIRE from proxy: target=" << ctx.get_int( "tx", 0 ) << ","
                    << ctx.get_int( "ty", 0 );
        } else {
            DebugLog( DL::Info, DC::Main ) << "[coop] FIRE from proxy: no target context";
        }
    } else if( key == "PAUSE" || key == "WAIT" ) {
    proxy->moves -= proxy->get_speed();
    } else if( key == "PICKUP" ) {
    // Phase 9: pickup is avatar-centric (g->u); proxy-targeted pickup
    // requires item::pickup_target on an npc — deferred to Ph9 implementation.
    // Deduct moves so the client's action is consumed.
    proxy->moves -= proxy->get_speed();
        DebugLog( DL::Info, DC::Main ) << "[coop] PICKUP from proxy: NPC pickup deferred";
    } else if( key == "MOVE_UP" || key == "MOVE_DOWN" ) {
    // Phase 10: vertical_move() acts on host avatar (g->u), not proxy.
    // True proxy vertical move needs NPC stair navigation — deferred.
    // Deduct moves to consume the queued action.
    proxy->moves -= proxy->get_speed();
        DebugLog( DL::Info, DC::Main ) << "[coop] " << key << " from proxy: deferred";
    } else if( key == "SLEEP" || key == "CRAFT" ) {
    // Phase 11: activity relay — deduct moves; NPC activity set elsewhere
    proxy->moves -= proxy->get_speed();
    } else {
        DebugLog( DL::Debug, DC::Main ) << "[coop] execute_client_action: unimplemented key=" << key;
    }
}

auto coop_server::build_and_send_sync( bool force_full ) -> void
{
    std::ostringstream oss;
    JsonOut jout( oss );
    jout.start_object();
    jout.member( "t", static_cast<int>( coop_pkt::sync ) );
    jout.member( "turn", to_turn<int>( calendar::turn ) );

    // Tile resync policy: send the 5×5 submap grid when:
    //   (a) host moved to a new submap origin (map shift), OR
    //   (b) periodic safety net every TILE_RESYNC_INTERVAL ticks.
    // (b) catches in-place terrain changes (smash, doors, fire, explosions,
    //     construction) that don't shift the origin.  First-tick sentinel
    //     last_sync_origin_ = INT_MIN guarantees an initial full sync.
    const tripoint_abs_sm abs_sub = g->m.get_abs_sub();
    ++sync_tick_counter_;
    const bool origin_changed = ( abs_sub != last_sync_origin_ );
    const bool periodic = ( sync_tick_counter_ % TILE_RESYNC_INTERVAL == 0 );
    jout.member( "tiles" );
    jout.start_array();
    if( force_full || origin_changed || periodic ) {
        last_sync_origin_ = abs_sub;
        for( int dy = -2; dy <= 2; ++dy ) {
            for( int dx = -2; dx <= 2; ++dx ) {
                const tripoint_abs_sm sm_pos{abs_sub.x() + dx, abs_sub.y() + dy, abs_sub.z()};
                const submap* sm = MAPBUFFER.lookup_submap( sm_pos );
                if( !sm ) { continue; }
                jout.start_object();
                jout.member( "version", savegame_version );
                jout.member( "coordinates" );
                jout.start_array();
                jout.write( sm_pos.x() );
                jout.write( sm_pos.y() );
                jout.write( sm_pos.z() );
                jout.end_array();
                sm->store( jout );
                jout.end_object();
            }
        }
    }
    // Empty array on idle ticks — client skips tile processing when empty.
    jout.end_array();

    // H5: assign stable IDs to live monsters keyed by pointer (stable in creature_tracker).
    std::unordered_set<const monster *> live_ptrs;
    for( monster& mon : g->all_monsters() ) {
        if( !mon.is_dead() ) { live_ptrs.insert( &mon ); }
    }
    std::erase_if( monster_id_map_, [&]( const auto & kv ) { return !live_ptrs.contains( kv.first ); } );
    for( const monster * ptr : live_ptrs ) {
        if( !monster_id_map_.contains( ptr ) ) { monster_id_map_.emplace( ptr, next_monster_id_++ ); }
    }

    // Serialize live monsters with host-assigned stable IDs.
    jout.member( "monsters" );
    jout.start_array();
    for( monster& mon : g->all_monsters() ) {
        if( mon.is_dead() ) { continue; }
        const tripoint_abs_ms apos = mon.abs_pos();
        jout.start_object();
        jout.member( "id", monster_id_map_.at( &mon ) );
        jout.member( "type", mon.type->id.str() );
        jout.member( "ax", apos.x() );
        jout.member( "ay", apos.y() );
        jout.member( "az", apos.z() );
        jout.member( "hp", mon.get_hp() );
        jout.member( "hp_max", mon.get_hp_max() );
        jout.end_object();
    }
    jout.end_array();

    // Proxy canonical position — client uses this to reconcile local prediction
    // if the two diverge beyond a threshold (e.g. blocked terrain).
    const npc* proxy = g->critter_by_id<npc>( coop_session::get().proxy_npc_id );
    if( proxy ) {
        const tripoint_abs_ms ppos = proxy->abs_pos();
        jout.member( "proxy_ax", ppos.x() );
        jout.member( "proxy_ay", ppos.y() );
        jout.member( "proxy_az", ppos.z() );
    }

    // Host player position — lets client render the host's avatar location.
    {
        const tripoint_abs_ms hpos = g->u.abs_pos();
        jout.member( "host_ax", hpos.x() );
        jout.member( "host_ay", hpos.y() );
        jout.member( "host_az", hpos.z() );
    }

    jout.end_object();
    // Push onto send queue — IO thread is the sole caller of send on the socket (C5).
    std::scoped_lock lk{send_mtx_};
    send_q_.push_back( oss.str() );
}

auto coop_server::shutdown() -> void
{
    const bool was_running = running_.exchange( false );
    // Join first — the IO thread checks running_ and st.stop_requested() at the top of
    // each iteration and exits quickly (no long-blocking recv under C5).
    if( receiver_thread_.joinable() ) {
        receiver_thread_.request_stop();
        receiver_thread_.join();
    }
    if( was_running && client_sock_ ) {
        // Only send disconnect when we initiated the shutdown (not when the receiver
        // already signaled it by clearing running_).
        std::ostringstream oss;
        JsonOut jout( oss );
        jout.start_object();
        jout.member( "t", static_cast<int>( coop_pkt::disconnect ) );
        jout.end_object();
        coop_net::send( client_sock_, oss.str() );
    }
    if( client_sock_ ) {
        NET_DestroyStreamSocket( client_sock_ );
        client_sock_ = nullptr;
    }
    if( server_sock_ ) {
        NET_DestroyServer( server_sock_ );
        server_sock_ = nullptr;
    }
    if( net_initialized_ ) {
        NET_Quit();
        net_initialized_ = false;
    }
    coop_session::get().mode = coop_mode::none;
    if( g ) { g->coop_server_ = nullptr; }
    DebugLog( DL::Info, DC::Main ) << "[coop] server shutdown";
}

auto coop_server::send_chat( const std::string& text ) -> void
{
    if( !running_ ) { return; }
std::ostringstream oss;
JsonOut jout( oss );
jout.start_object();
jout.member( "t", static_cast<int>( coop_pkt::chat ) );
    jout.member( "d" );
    jout.start_object();
    jout.member( "from", "host" );
    jout.member( "text", text );
    jout.end_object();
    jout.end_object();
    // Push onto send queue — IO thread is the sole caller of send on the socket (C5).
    // Drop oldest chat frame if queue is full (M6).
    std::scoped_lock lk{send_mtx_};
    if( send_q_.size() >= 64 ) { send_q_.pop_front(); }
    send_q_.push_back( oss.str() );
}

#endif // COOP_ENABLED
