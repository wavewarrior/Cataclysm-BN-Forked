#ifdef COOP_ENABLED

#include "coop_server.h"

#include "avatar.h"
#include "calendar.h"
#include "coop_mutation_log.h"
#include "coop_net.h"
#include "coop_packets.h"
#include "coop_session.h"
#include "coop_overmap.h"
#include "coordinates.h"
#include "creature_tracker.h"
#include "debug.h"
#include "game.h"
#include "get_version.h"
#include "item.h"
#include "json.h"
#include "map.h"
#include "mapbuffer.h"
#include "messages.h"
#include "monster.h"
#include "npc.h"
#include "overmapbuffer.h"
#include "ranged.h"
#include "submap.h"
#include "skill.h"
#include "type_id.h"
#include "world.h"
#include "rng.h"
#include "worldfactory.h"
#include "coop_menu.h"
#include "field_type.h"
#include "morale_types.h"
#include "player_activity.h"
#include "string_formatter.h"
#include "translations.h"
#include "vehicle.h"

#include <SDL3_net/SDL_net.h>
#include <algorithm>
#include <ctime>
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
    if( transport_ ) { return true; }
NET_StreamSocket* candidate = nullptr;
if( !NET_AcceptClient( server_sock_, &candidate ) || !candidate ) { return false; }
    while( NET_GetConnectionStatus( candidate ) == 0 ) { SDL_Delay( 10 ); }
    if( NET_GetConnectionStatus( candidate ) < 0 ) {
        NET_DestroyStreamSocket( candidate );
        return false;
    }
    transport_ = std::make_unique<coop_net_transport>( candidate );
    coop_session::get().mode = coop_mode::host;
    DebugLog( DL::Info, DC::Main ) << "[coop] client connected";
    return true;
}

auto coop_server::wait_for_client() -> bool
{
    // Short-circuit if try_accept() already stored the client.
    if( transport_ ) { return true; }
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
    if( !transport_->send( oss.str() ) ) {
        DebugLog( DL::Error, DC::Main ) << "[coop] handshake: send failed";
        return false;
    }

    // Receive client handshake
    std::string buf;
    if( !transport_->recv( buf, 5000 ) ) {
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
    // CL-RANGED: inv_clear() only clears inv, NOT primary_weapon(). Explicitly drop
    // whatever randomize() wielded — proxy starts unarmed; re-armed per shot via
    // client_status weapon_id before resolve_fire_at_seq.
    tmp->inv_clear();
    tmp->worn.clear();
    if( tmp->is_armed() ) { tmp->remove_primary_weapon(); }
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
        // G1: apply worn items received in join_info packet
        if( !client_worn_json_.empty() ) {
            try {
                std::istringstream iss( client_worn_json_ );
                JsonIn jin( iss );
                jin.start_array();
                while( !jin.end_array() ) {
                    detached_ptr<item> w = item::spawn( jin );
                    if( w ) { ptr->wear_item( std::move( w ), false ); }
                }
            } catch( const JsonError& e ) {
                DebugLog( DL::Error, DC::Main ) << "[coop] spawn_proxy_npc worn: " << e.what();
            }
        }
    } else {
        DebugLog( DL::Error, DC::Main ) << "[coop] proxy NPC not found after load_npcs";
    }
    return ptr;
}

auto coop_server::send_world_seed( const std::string& player_name ) -> bool
{
    // Generate a session token for reconnection validation
    session_token_ = std::to_string( std::time( nullptr ) ) + "-" + std::to_string( rng( 0, 999999 ) );
    const auto* aw = g->get_active_world();
    world_seed_data data;
    data.turn = to_turn<int>( calendar::turn );
    data.spawn_pos = g->u.abs_pos();
    data.player_name = player_name;
    data.world_name = aw ? aw->info->world_name : std::string{};
    data.rng_seed = g_main_rng_seed;
    data.session_token = session_token_;
    return transport_->send( build_world_seed_packet( data ) );
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
        if( !transport_->send( frame ) ) { return false; }
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

auto coop_server::wait_for_join_info( int timeout_ms ) -> bool
{
    // Pre-receiver: read directly from transport_ on the main thread, same pattern as
    // handshake() / send_world_seed().  The receiver thread is NOT yet running at this point.
    std::string buf;
    if( !transport_->recv( buf, timeout_ms ) ) {
        DebugLog( DL::Info, DC::Main ) << "[coop] wait_for_join_info: timeout — using spawn fallback";
        return false;
    }
    const auto parsed = parse_join_info_packet( buf );
    if( !parsed ) {
        DebugLog( DL::Info, DC::Main ) << "[coop] wait_for_join_info: unexpected packet — using fallback";
        return false;
    }
    client_join_pos_ = parsed->pos;
    client_worn_json_ = parsed->worn_json; // G1: applied by spawn_proxy_npc
    DebugLog( DL::Info, DC::Main )
            << "[coop] join_info: client start ("
            << parsed->pos.x() << "," << parsed->pos.y() << "," << parsed->pos.z() << ")";
    return true;
}

auto coop_server::client_join_pos() const -> std::optional<tripoint_abs_ms>   // *NOPAD*
{
    return client_join_pos_;
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
            for( const auto& frame : outgoing ) { transport_->send( frame ); }
        }

        // Non-blocking check: skip recv if nothing is waiting.
        if( !transport_->poll() ) {
            SDL_Delay( 1 );
            continue;
        }

        if( !transport_->recv( buf, 5000 ) ) {
            if( running_ ) {
                DebugLog( DL::Info, DC::Main ) << "[coop] receiver: client disconnected";
                client_disconnected_.store( true );
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
                const auto data = parse_action_packet( buf );
                if( data ) { push_action( {data->seq, data->key, data->ctx_json} ); }
            } else if( t == coop_pkt::chat ) {
                JsonObject d = pkt.get_object( "d" );
                d.allow_omitted_members();
                std::string text = d.get_string( "text", "" );
                if( text.size() > 512 ) { text = text.substr( 0, 512 ); }
                std::scoped_lock lk{chat_mtx_};
                if( chat_q_.size() >= 64 ) { chat_q_.pop_front(); }
                chat_q_.push_back( {std::move( text )} );
            } else if( t == coop_pkt::client_status ) {
                // Client reports its own state each tick.
                // idle: used by both_idle() for fast-forward.
                // hp_pct, dead: C3 — vital signs for death detection and display.
                // Inventory, effects, skills: deferred to a later C3 increment.
                JsonObject d = pkt.get_object( "d" );
                d.allow_omitted_members();
                client_is_idle_.store( d.get_bool( "idle", false ) );
                client_hp_pct_.store( d.get_int( "hp_pct", 100 ) );
                const bool now_dead = d.get_bool( "dead", false );
                if( now_dead && !client_dead_.load() ) {
                    DebugLog( DL::Info, DC::Main )
                            << "[coop] C3: client avatar died — hp_pct="
                            << d.get_int( "hp_pct", 0 );
                    // C3b: death message displayed in coop_world_tick() via client_death_announced_.
                    // Full respawn / session-end logic deferred to a later phase.
                }
                client_dead_.store( now_dead );
                // F1: extended client vitals
                client_stamina_pct_.store( d.get_int( "stamina_pct", 100 ) );
                {
                    std::scoped_lock lk{ chat_mtx_ };
                    client_activity_str_ = d.get_string( "activity", "" );
                    client_abs_pos_ = tripoint_abs_ms{
                        d.get_int( "ax", 0 ), d.get_int( "ay", 0 ), d.get_int( "az", 0 ) };
                }
                // Ping measurement: read echoed timestamp and compute RTT.
                const int64_t echo_ts = static_cast<int64_t>( d.get_int( "ping_echo", 0 ) );
                if( echo_ts > 0 ) {
                    const int64_t now = static_cast<int64_t>( SDL_GetTicks() );
                    coop_session::get().partner_ping_ms = static_cast<int>( now - echo_ts );
                }
                // Skill sync: parse and store for main-thread application in coop_world_tick.
                const auto skills = parse_skill_sync_fields( d );
                if( !skills.empty() ) {
                    std::scoped_lock lk{ pending_skills_mtx_ };
                    pending_skills_ = skills;
                }
            } else if( t == coop_pkt::resync_request ) {
                // A4: client detected a hash mismatch and needs a full sync.
                // Write the atomic flag — main thread reads it in build_and_send_sync().
                // NEVER touch sync_tick_counter_ here; it's main-thread-only.
                DebugLog( DL::Info, DC::Main ) << "[coop] receiver: client requested full resync";
                force_resync_.store( true );
            } else if( t == coop_pkt::overmap_mark ) {
                // F4: client placed or cleared a shared overmap marker.
                JsonObject d = pkt.get_object( "d" );
                d.allow_omitted_members();
                auto& sess = coop_session::get();
                if( d.get_bool( "clear", false ) ) {
                    sess.shared_mark = std::nullopt;
                    sess.shared_mark_label.clear();
                } else {
                    sess.shared_mark = tripoint_abs_omt{
                        d.get_int( "omx", 0 ), d.get_int( "omy", 0 ), d.get_int( "omz", 0 ) };
                    sess.shared_mark_label = d.get_string( "label", "" );
                }
            } else if( t == coop_pkt::disconnect ) {
                DebugLog( DL::Info, DC::Main ) << "[coop] receiver: client sent disconnect";
                running_ = false;
                break;
            } else if( t == coop_pkt::vehicle_state ) {
                // E1: store for main-thread application in coop_world_tick()
                JsonObject d = pkt.get_object( "d" );
                d.allow_omitted_members();
                pending_veh_state_t vs;
                vs.vid      = static_cast<uint32_t>( d.get_int( "vid", 0 ) );
                vs.abs_pos  = tripoint_abs_ms{ d.get_int( "ax", 0 ), d.get_int( "ay", 0 ),
                                               d.get_int( "az", 0 ) };
                vs.face_x   = d.get_int( "face_x", 0 );
                vs.face_y   = d.get_int( "face_y", 1 );
                vs.vel      = d.get_int( "velocity", 0 );
                vs.valid    = true;
                std::scoped_lock lk{ pending_veh_mtx_ };
                pending_veh_state_ = vs;
            } else if( t == coop_pkt::trade_offer ) {
                // F2: store item JSON for main-thread popup in coop_world_tick()
                JsonObject d = pkt.get_object( "d" );
                d.allow_omitted_members();
                const std::string item_json = d.get_string( "item_json", "" );
                if( !item_json.empty() ) {
                    std::scoped_lock lk{ action_mtx_ };
                    pending_trade_offer_json_ = item_json;
                }
            } else if( t == coop_pkt::tap_shoulder ) {
                // F3: client tapped host's shoulder
                pending_tap_.store( true );
            } else if( t == coop_pkt::emote ) {
                // F6: relay emote to main thread via action queue
                push_action( { 0, "EMOTE", buf } );
            } else if( t == coop_pkt::overmap_sync ) {
                // Shared overmap: client revealed tiles — apply to host's overmapbuffer.
                apply_overmap_sync_packet( buf, coop_session::get().dimension_id );
            }
            // other packet types silently ignored until later phases
        } catch( const JsonError& e ) {
            DebugLog( DL::Error, DC::Main ) << "[coop] receiver: JSON parse error: " << e.what();
        }
    }
    if( !client_disconnected_.load() ) { running_ = false; }
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
    if( !coop_session::get().is_host() || ( !running_ && !awaiting_reconnect_.load() ) ) { return; }

// Check for unexpected client disconnect — transition to reconnect window
if( client_disconnected_.exchange( false ) ) {
    handle_client_disconnect();
    }

    // While awaiting reconnection: try to accept, count down, skip normal action processing
    if( awaiting_reconnect_.load() ) {
    --reconnect_countdown_;
    if( reconnect_countdown_ <= 0 ) {
            add_msg( m_bad, _( "Reconnection timed out. Session ended." ) );
            DebugLog( DL::Info, DC::Main ) << "[coop] reconnect timeout — full shutdown";
            awaiting_reconnect_.store( false );
            shutdown();
            return;
        }
        // Non-blocking: try to accept a reconnecting client each tick
        if( accept_reconnect() ) {
            add_msg( m_good, _( "Partner reconnected!" ) );
            return; // resume normal processing next tick
        }
        // Still waiting — run world sim but skip client action processing
        g->post_action_world_step();
        return;
    }

    // 1. World simulation first — process_turn() inside post_action_world_step()
    //    grants the proxy its move budget via mod_moves(get_speed()).
    //    npcmove() skips AI for is_coop_remote without zeroing moves, so the
    //    proxy exits the sim with fresh moves ready to consume below.
    // A3: capture all world mutations during this tick for A4 delta stream.
    coop_tick_log_guard _tick_log;
    // Test seam: push a synthetic terrain_changed event directly into the mutation log
    // so the A4 delta sync carries ev_count=1 regardless of the current tile state.
    // Used by the resync integration test to trigger the client's hash-mismatch detection.
    if( pending_test_event_for_resync_ ) {
    pending_test_event_for_resync_ = false;
    if( auto * log = coop_mutation_log::current() ) {
            log->push( { .type = coop_event_type::terrain_changed,
                         .pos  = tripoint_abs_ms{ 999, 999, 0 }, // far-offscreen: no real tile
                         .value = 1 } );
        }
    }
    g->post_action_world_step();

    // 2. Snapshot entity positions BEFORE draining client actions so the
    //    snapshot represents the world state at the moment the client fired.
    push_entity_snapshot();

    // 3. Process one client action per world tick.  One-per-tick is intentional:
    //    the proxy moves once per sim step, mirroring the client's input rate, and
    //    avoids multi-tile teleport bursts.  Do NOT gate on proxy->moves — the
    //    is_coop_remote path in npcmove() skips the move-grant so proxy->moves is
    //    always 0 after the first drain tick; gating on it permanently blocks later
    //    actions.
    npc* proxy = g->critter_by_id<npc>( coop_session::get().proxy_npc_id );
    if( proxy ) {
    // C3a: mirror client's reported HP percentage onto the proxy so host-side
    // NPC AI and combat calculations see the correct health state.
    // IMPORTANT: always clamp to std::max(1,...) — never 0.  Setting any body
    // part to 0 triggers is_dead_state() → cleanup_dead() → npc::die(), which
    // spawns a corpse, drops inventory, and erases the proxy permanently.
    // The proxy is a session placeholder; client death is signalled via C3b only.
    const int pct = client_hp_pct_.load();
        for( const bodypart_id& bp : proxy->get_all_body_parts() ) {
            const int max_hp = proxy->get_part_hp_max( bp );
            proxy->set_part_hp_cur( bp, std::max( 1, max_hp * pct / 100 ) );
        }
        // Skill sync: apply client's skill levels to proxy NPC (main-thread safe).
        {
            std::vector<std::pair<std::string, int>> skills;
            {
                std::scoped_lock lk{ pending_skills_mtx_ };
                skills.swap( pending_skills_ );
            }
            for( const auto &[sid, lvl] : skills ) {
                const skill_id sk( sid );
                if( sk.is_valid() ) {
                    proxy->set_skill_level( sk, lvl );
                }
            }
        }

        if( const auto act = try_pop_action() ) {
            if( act->seq > last_confirmed_seq_ ) { last_confirmed_seq_ = act->seq; }
            proxy->set_moves( proxy->get_speed() ); // ensure move budget for execute
            execute_client_action( proxy, act->key, act->ctx_json, act->seq );
        }
        proxy->set_moves( 0 ); // prevent residual moves carrying into the NPC AI sim
    }


    // C3b: death event — display a host-side message the first time the client's
    // avatar reports dead.  The proxy stays alive (HP clamped to 1) so it remains
    // a valid session placeholder.  Full respawn / session-end logic is deferred.
    if( client_dead_.load() && !client_death_announced_ ) {
    client_death_announced_ = true;
    add_msg( m_warning, _( "Your co-op partner has died." ) );
    } else if( !client_dead_.load() ) {
    client_death_announced_ = false; // reset if partner respawns
}

// E1: apply pending vehicle state from client (main-thread safe)
{
    pending_veh_state_t vs;
    {
        std::scoped_lock lk{ pending_veh_mtx_ };
        vs = pending_veh_state_;
        pending_veh_state_.valid = false;
    }
    if( vs.valid ) {
            auto it = vehicle_id_map_rev_.find( vs.vid );
            if( it != vehicle_id_map_rev_.end() ) {
                vehicle* veh = it->second;
                if( veh ) {
                    const tripoint_bub_ms new_bub = g->m.abs_to_bub( vs.abs_pos );
                    const tripoint_bub_ms old_bub = veh->bub_ms_location();
                    const tripoint_rel_ms delta{ new_bub.x() - old_bub.x(),
                                                 new_bub.y() - old_bub.y(),
                                                 new_bub.z() - old_bub.z() };
                    if( delta != tripoint_rel_ms{} ) { g->m.displace_vehicle( *veh, delta ); }
                    veh->velocity = vs.vel;
                }
            }
        }
    }

    // F2: process pending trade offer from client
    {
        std::optional<std::string> trade_offer;
        {
            std::scoped_lock lk{ action_mtx_ };
            trade_offer = std::move( pending_trade_offer_json_ );
            pending_trade_offer_json_.reset();
        }
        if( trade_offer.has_value() && !trade_offer->empty() ) {
            try {
                std::istringstream iss( *trade_offer );
                JsonIn jin( iss );
                detached_ptr<item> offered = item::spawn( jin );
                if( offered ) {
                    const std::string msg = string_format(
                                                _( "%s wants to give you: %s. Accept?" ),
                                                coop_session::get().partner_name, offered->tname() );
                    const bool accepted = show_coop_popup( msg );
                    const std::string ack = accepted ? R"({"t":44})" : R"({"t":45})";
                    {
                        std::scoped_lock lk{ send_mtx_ };
                        send_q_.push_back( ack );
                    }
                    if( accepted ) { g->u.i_add( std::move( offered ) ); }
                }
            } catch( const JsonError& e ) {
                DebugLog( DL::Error, DC::Main ) << "[coop] F2 trade JSON: " << e.what();
            }
        }
    }

    // F3: tap-on-shoulder from client — cancel host's current activity
    if( pending_tap_.exchange( false ) ) {
    if( g->u.activity ) { g->u.cancel_activity(); }
        add_msg( m_info, _( "[%s] taps you on the shoulder!" ),
                 coop_session::get().partner_name );
    }

    // F5: team activity speed-up — reduce host activity progress when both doing same task
    if( g->u.activity ) {
    std::string client_verb;
    {
        std::scoped_lock lk{ chat_mtx_ };
        client_verb = client_activity_str_;
    }
    if( !client_verb.empty() ) {
            const std::string host_verb = g->u.activity->get_verb().translated();
            if( host_verb == client_verb ) {
                g->u.activity->moves_left = std::max( 0,
                                                      g->u.activity->moves_left - g->u.get_speed() / 2 );
            }
        }
    }

    // G2: client downed detection and countdown
    if( client_hp_pct_.load() == 0 && !client_dead_.load() && !client_downed_.load() ) {
    client_downed_.store( true );
        client_down_turns_remaining_ = COOP_DOWN_TIMEOUT_TURNS;
        send_chat( string_format( _( "[ALERT] %s is critically wounded! %d seconds remaining!" ),
                                  coop_session::get().partner_name,
                                  COOP_DOWN_TIMEOUT_TURNS ) );
    }
    if( client_downed_.load() ) {
    --client_down_turns_remaining_;
    if( client_down_turns_remaining_ > 0 && client_down_turns_remaining_ % 10 == 0 ) {
            send_chat( string_format( _( "[%s is downed] %d seconds remaining..." ),
                                      coop_session::get().partner_name,
                                      client_down_turns_remaining_ ) );
        }
        if( client_down_turns_remaining_ <= 0 ) {
            client_downed_.store( false );
            client_dead_.store( true );
        }
    }

    // 3. Build and send sync (tiles only when host submap changes; always sends
    //    monsters + turn + proxy position).
    build_and_send_sync();

    // 4. Drain ALL pending chat messages — not just one per tick.
    while( auto msg = try_pop_chat() ) { add_msg( m_info, "[partner]: %s", msg->text ); }

    // 5. Fast-forward when both players are engaged in long activities (sleep,
    //    craft, read, wait…).
    maybe_fast_forward();
}

auto coop_server::maybe_fast_forward() -> bool
{
    if( !both_idle() ) { return false; }
g->main_loop_accum_ms_ = COOP_FAST_FORWARD_ACCUM_MS;
return true;
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

auto coop_server::execute_player_cmd( npc* proxy, const player_cmd_t &cmd, const uint32_t seq )
-> void
{
    if( !proxy ) { return; }
using K = player_cmd_kind;
const tripoint_bub_ms cur = proxy->bub_pos();
switch( cmd.kind ) {
    case K::move: {
        // Authoritative client position: setpos(), not move_to().
        // move_to() pathfinds and may stumble diagonally on blocked tiles.
        const tripoint_bub_ms
        dest{cur.x() + cmd.delta.x(), cur.y() + cmd.delta.y(), cur.z() + cmd.delta.z()};
        if( g->m.inbounds( dest ) ) { proxy->setpos( dest ); }
            break;
        }
        case K::pause:
            proxy->moves -= proxy->get_speed();
            break;
        case K::pickup:
            // NPC pickup deferred: proxy-targeted pickup needs item::pickup_target on NPC.
            proxy->moves -= proxy->get_speed();
            DebugLog( DL::Info, DC::Main ) << "[coop] PICKUP from proxy: NPC pickup deferred";
            break;
        case K::sleep:
        case K::craft:
            // Activity relay: deduct moves; NPC activity set elsewhere.
            proxy->moves -= proxy->get_speed();
            break;
        case K::smash:
            // B3 Phase 5: bash terrain/creature at target_abs.
            // Proxy melee-attacks any creature at the target tile; terrain bashing
            // is propagated via TERRAIN_CHANGE by the client (C2c) so no map::bash here.
        {
            const tripoint_bub_ms tpos = g->m.abs_to_bub( cmd.target_abs );
            if( const auto mon_ptr = g->critter_tracker->find( tpos ) ) {
                proxy->melee_attack( *mon_ptr, true );
            } else {
                proxy->moves -= proxy->get_speed(); // tile bash: moves consumed
            }
        }
        break;
        case K::fire:
            // B3 Phase 6: fire weapon at target_abs using lag-comp seq.
            // resolve_fire_at_seq handles snapshot lookup, creature reposition, fire_gun, restore.
            resolve_fire_at_seq(
                proxy, seq,
                cmd.target_abs.x(), cmd.target_abs.y(), cmd.target_abs.z() );
            break;
        case K::eat:
        case K::reload:
            // B3 Phase 7: no-payload relay — proxy consumes moves only.
            // The client's own character handles eating/reloading; proxy mirrors the
            // time cost so the world sim stays in step.
            proxy->moves -= proxy->get_speed();
            break;
        case K::use:
            // B3 Phase 8: use/activate item — proxy consumes moves only.
            // Item effects are client-authoritative; proxy mirrors time cost.
            proxy->moves -= proxy->get_speed();
            break;
        case K::melee:
            // B3 Phase 9: melee attack at target_abs.
            // Proxy attacks the creature at the target tile — both adjacent and reach
            // autoattacks relay through this case (no MOVE packet fires for autoattack).
        {
            const tripoint_bub_ms tpos = g->m.abs_to_bub( cmd.target_abs );
            if( const auto mon_ptr = g->critter_tracker->find( tpos ) ) {
                proxy->melee_attack( *mon_ptr, true );
            } else {
                proxy->moves -= proxy->get_speed();
            }
        }
        break;
        case K::none:
        default:
            DebugLog( DL::Debug, DC::Main )
                    << "[coop] execute_player_cmd: unhandled kind " << static_cast<int>( cmd.kind );
            break;
    }
}

auto coop_server::apply_pickup_manifest( const std::string& ctx_json ) -> void
{
    if( ctx_json.empty() ) { return; }
std::istringstream iss( ctx_json );
JsonIn jin( iss );
JsonObject root = jin.get_object();
root.allow_omitted_members();
for( JsonObject entry : root.get_array( "items" ) ) {
    entry.allow_omitted_members();
        const tripoint_abs_ms
        abs_pos{entry.get_int( "tx" ), entry.get_int( "ty" ), entry.get_int( "tz" )};
        const itype_id type( entry.get_string( "type" ) );
        const int charges = entry.get_int( "charges", 0 );
        const int qty = entry.get_int( "qty", 0 );
        const tripoint_bub_ms bub = g->m.abs_to_bub( abs_pos );
        if( !g->m.inbounds( bub ) ) { continue; }
        auto stack = g->m.i_at( bub );
        if( charges > 0 ) {
            auto it = std::ranges::find_if( stack, [&]( const item * i ) {
                return i->typeId() == type;
            } );
            if( it != stack.end() ) {
                const int have = ( *it )->charges;
                if( charges >= have ) {
                    stack.erase( it );
                } else {
                    ( *it )->charges -= charges;
                }
            } else {
                DebugLog( DL::Info, DC::Main )
                        << "[coop] C1 PICKUP: cbc item not found: " << type.str();
            }
        } else if( qty > 0 ) {
            int remaining = qty;
            auto it = stack.begin();
            while( it != stack.end() && remaining > 0 ) {
                if( ( *it )->typeId() == type ) {
                    it = stack.erase( it );
                    --remaining;
                } else {
                    ++it;
                }
            }
            if( remaining > 0 ) {
                DebugLog( DL::Info, DC::Main )
                        << "[coop] C1 PICKUP: " << remaining << " of " << type.str()
                        << " not found at tile";
            }
        }
    }
}

auto coop_server::apply_drop_manifest( const std::string& ctx_json ) -> void
{
    if( ctx_json.empty() ) { return; }
std::istringstream iss( ctx_json );
JsonIn jin( iss );
JsonObject root = jin.get_object();
root.allow_omitted_members();
for( JsonObject entry : root.get_array( "items" ) ) {
    entry.allow_omitted_members();
        const tripoint_abs_ms
        abs_pos{entry.get_int( "tx" ), entry.get_int( "ty" ), entry.get_int( "tz" )};
        const std::string item_json = entry.get_string( "data" );
        const tripoint_bub_ms bub = g->m.abs_to_bub( abs_pos );
        if( !g->m.inbounds( bub ) ) { continue; }
        try {
            std::istringstream item_iss( item_json );
            JsonIn item_jin( item_iss );
            detached_ptr<item> it = item::spawn( item_jin );
            if( !it ) {
                DebugLog( DL::Info, DC::Main )
                        << "[coop] C2a DROP: item deserialized as null at (" << abs_pos.x() << ","
                        << abs_pos.y() << "," << abs_pos.z() << ")";
                continue;
            }
            g->m.add_item( bub, std::move( it ) );
        } catch( const JsonError& e ) {
            DebugLog( DL::Info, DC::Main )
                    << "[coop] C2a DROP: bad item JSON at (" << abs_pos.x() << "," << abs_pos.y() << ","
                    << abs_pos.z() << "): " << e.what();
        }
    }
}

namespace
{
/// Arm the proxy with the weapon embedded in ctx["weapon_id"].
/// require_gun=true (FIRE): weapon must pass is_gun(); ammo_id is also loaded.
/// require_gun=false (MELEE/SMASH): any valid itype accepted; no ammo.
/// Returns true if armed — caller must call remove_primary_weapon() after the action.
/// Both weapon_id and ammo_id are untrusted peer input; validated via is_valid().
auto arm_proxy_from_ctx( npc* proxy, JsonObject& ctx, bool require_gun ) -> bool
{
    const auto wid_str = ctx.get_string( "weapon_id", "" );
    if( wid_str.empty() ) { return false; }
    const itype_id wid( wid_str );
    if( !wid.is_valid() ) {
        DebugLog( DL::Error, DC::Main )
                << "[coop] arm_proxy: unknown weapon_id '" << wid_str << "' — skipped";
        return false;
    }
    auto weapon = item::spawn( wid );
    if( require_gun && !weapon->is_gun() ) {
        DebugLog( DL::Error, DC::Main )
                << "[coop] arm_proxy: weapon_id '" << wid_str << "' is not a gun — skipped";
        return false;
    }
    if( require_gun ) {
        const auto aid_str = ctx.get_string( "ammo_id", "" );
        if( !aid_str.empty() ) {
            const itype_id aid( aid_str );
            if( aid.is_valid() ) { weapon->ammo_set( aid ); }
        }
    }
    proxy->wield( std::move( weapon ) );
    return true;
}
} // namespace

auto coop_server::apply_terrain_change( const std::string &ctx_json ) -> void
{
    if( ctx_json.empty() ) { return; }
std::istringstream iss( ctx_json );
JsonIn jin( iss );
JsonObject root = jin.get_object();
root.allow_omitted_members();
const tripoint_abs_ms abs_pos{
    root.get_int( "tx" ), root.get_int( "ty" ), root.get_int( "tz" )};
const std::string ter_name  = root.get_string( "ter",  "" );
const std::string furn_name = root.get_string( "furn", "" );
const tripoint_bub_ms bub = g->m.abs_to_bub( abs_pos );
if( !g->m.inbounds( bub ) ) { return; }
if( !ter_name.empty() ) {
    const ter_str_id t( ter_name );
        if( t.is_valid() ) {
            g->m.ter_set( bub, t );
        } else {
            DebugLog( DL::Info, DC::Main )
                    << "[coop] C2b TERRAIN_CHANGE: unknown ter id '" << ter_name << "' — skipped";
        }
    }
    if( !furn_name.empty() ) {
    const furn_str_id f( furn_name );
        if( f.is_valid() ) {
            g->m.furn_set( bub, f );
        } else {
            DebugLog( DL::Info, DC::Main )
                    << "[coop] C2b TERRAIN_CHANGE: unknown furn id '" << furn_name << "' — skipped";
        }
    }
    DebugLog( DL::Info, DC::Main )
            << "[coop] C2b TERRAIN_CHANGE: " << ter_name << " at ("
            << abs_pos.x() << "," << abs_pos.y() << "," << abs_pos.z() << ")";
}

auto coop_server::execute_client_action(
    npc* proxy, const std::string& key, const std::string& ctx_json, uint32_t seq ) -> void
{
    if( !proxy ) { return; }

// Try the typed path first: movement and simple no-payload commands.
const auto move_cmd = parse_move_cmd( key );
if( move_cmd.kind == player_cmd_kind::move ) {
    execute_player_cmd( proxy, move_cmd, seq );
        return;
    }
    if( key == "PAUSE" || key == "WAIT" ) {
    execute_player_cmd( proxy, player_cmd_t{.kind = player_cmd_kind::pause}, seq );
        return;
    }
    if( key == "PICKUP" ) {
    // C1 (Option B): client picked up items locally; mirror the exact removals on the host.
    // Manifest: {"items":[{"tx":…,"ty":…,"tz":…,"type":"…","charges":N,"qty":N},…]}
    // Exactly-once: TCP guarantees no replay within a session. "Not found → skip" handles
    // the case where the host world already diverged (e.g. another entity took the item),
    // NOT as a dedup mechanism. C5 (reconnection) will need seq-based dedup.
    proxy->moves -= proxy->get_speed();
        apply_pickup_manifest( ctx_json );
        return;
    }
    if( key == "DROP" ) {
    // C2a (Option B): client dropped items locally; mirror the additions on the host.
    // Manifest: {"items":[{"tx":…,"ty":…,"tz":…,"data":"<full item JSON>"},…]}
    // Full item serialization preserves per-instance state (ammo, mods, damage, contents).
    // Items that went into a vehicle are NOT in the manifest (auto-excluded by client-side
    // diff).
    proxy->moves -= proxy->get_speed();
        apply_drop_manifest( ctx_json );
        return;
    }
    if( key == "TERRAIN_CHANGE" ) {
    // C2b: client opened/closed a door or otherwise mutated terrain.
    // Payload: {"tx":N,"ty":N,"tz":N,"ter":"t_id","furn":"f_id"}
    // Vehicle doors are excluded client-side (no terrain change there).
    apply_terrain_change( ctx_json );
        return;
    }
    if( key == "SLEEP" ) {
    execute_player_cmd( proxy, player_cmd_t{.kind = player_cmd_kind::sleep}, seq );
        return;
    }
    if( key == "CRAFT" ) {
    execute_player_cmd( proxy, player_cmd_t{.kind = player_cmd_kind::craft}, seq );
        return;
    }
    if( key == "EAT" ) {
    execute_player_cmd( proxy, make_player_eat_cmd(), seq );
        return;
    }
    if( key == "RELOAD" ) {
    execute_player_cmd( proxy, make_player_reload_cmd(), seq );
        return;
    }
    if( key == "USE" ) {
    execute_player_cmd( proxy, make_player_use_cmd(), seq );
        return;
    }
    if( key == "MELEE" ) {
    if( !ctx_json.empty() ) {
            std::istringstream iss( ctx_json );
            JsonIn jin( iss );
            JsonObject ctx = jin.get_object();
            ctx.allow_omitted_members();
            const tripoint_abs_ms abs_tpos{
                ctx.get_int( "tx", 0 ), ctx.get_int( "ty", 0 ), ctx.get_int( "tz", 0 )};
            // CL-MELEE-WEAPON: arm proxy with client's weapon (require_gun=false).
            const bool armed = arm_proxy_from_ctx( proxy, ctx, false );
            execute_player_cmd( proxy, make_player_melee_cmd( abs_tpos ), seq );
            if( armed ) { proxy->remove_primary_weapon(); }
        } else {
            proxy->moves -= proxy->get_speed();
        }
        return;
    }
    if( key == "BUTCHER" ) {
    if( !ctx_json.empty() ) {
            std::istringstream iss( ctx_json );
            JsonIn jin( iss );
            JsonObject ctx = jin.get_object();
            ctx.allow_omitted_members();
            const tripoint_abs_ms corpse_abs{
                ctx.get_int( "ax", 0 ), ctx.get_int( "ay", 0 ), ctx.get_int( "az", 0 ) };
            map& here = get_map();
            const auto local = here.abs_to_bub( corpse_abs );
            std::vector<item *> corpses_to_rm;
            for( item * const& it : here.i_at( local ) ) {
                if( it->is_corpse() ) { corpses_to_rm.push_back( it ); }
            }
            for( item * it : corpses_to_rm ) { here.i_rem( local, it ); }
        }
        return;
    }
    if( key == "ITEM_REMOVE" ) {
    if( !ctx_json.empty() ) {
            std::istringstream iss( ctx_json );
            JsonIn jin( iss );
            JsonObject ctx = jin.get_object();
            ctx.allow_omitted_members();
            const tripoint_abs_ms rem_abs{
                ctx.get_int( "ax", 0 ), ctx.get_int( "ay", 0 ), ctx.get_int( "az", 0 ) };
            const itype_id rem_type( ctx.get_string( "type", "" ) );
            map& here = get_map();
            const auto local = here.abs_to_bub( rem_abs );
            item* found = nullptr;
            for( item * const& it : here.i_at( local ) ) {
                if( it->typeId() == rem_type ) { found = it; break; }
            }
            if( found ) { here.i_rem( local, found ); }
        }
        return;
    }
    if( key == "ITEM_REMOVE_ALL" ) {
    if( !ctx_json.empty() ) {
            std::istringstream iss( ctx_json );
            JsonIn jin( iss );
            JsonObject ctx = jin.get_object();
            ctx.allow_omitted_members();
            const tripoint_abs_ms rem_abs{
                ctx.get_int( "ax", 0 ), ctx.get_int( "ay", 0 ), ctx.get_int( "az", 0 ) };
            get_map().i_clear( get_map().abs_to_bub( rem_abs ) );
        }
        return;
    }
    if( key == "FIELD_SET" ) {
    if( !ctx_json.empty() ) {
            std::istringstream iss( ctx_json );
            JsonIn jin( iss );
            JsonObject ctx = jin.get_object();
            ctx.allow_omitted_members();
            const tripoint_abs_ms fabs{
                ctx.get_int( "ax", 0 ), ctx.get_int( "ay", 0 ), ctx.get_int( "az", 0 ) };
            const field_type_id ftype( ctx.get_string( "field", "" ) );
            const int intensity = ctx.get_int( "intensity", 1 );
            if( ftype.is_valid() ) {
                get_map().add_field( get_map().abs_to_bub( fabs ), ftype, intensity );
            }
        }
        return;
    }
    if( key == "CAST_SPELL" ) {
    if( proxy ) { proxy->moves -= proxy->get_speed(); }
        return;
    }
    if( key == "WEAR" ) {
    if( proxy && !ctx_json.empty() ) {
            try {
                std::istringstream iss( ctx_json );
                JsonIn jin( iss );
                detached_ptr<item> w = item::spawn( jin );
                if( w ) { proxy->wear_item( std::move( w ), false ); }
            } catch( const JsonError& e ) {
                DebugLog( DL::Error, DC::Main ) << "[coop] WEAR: " << e.what();
            }
        }
        return;
    }
    if( key == "WORN_SYNC" ) {
    if( proxy && !ctx_json.empty() ) {
            proxy->worn.clear();
            try {
                std::istringstream iss( ctx_json );
                JsonIn jin( iss );
                jin.start_array();
                while( !jin.end_array() ) {
                    detached_ptr<item> w = item::spawn( jin );
                    if( w ) { proxy->wear_item( std::move( w ), false ); }
                }
            } catch( const JsonError& e ) {
                DebugLog( DL::Error, DC::Main ) << "[coop] WORN_SYNC: " << e.what();
            }
        }
        return;
    }
    if( key == "EMOTE" ) {
    if( !ctx_json.empty() ) {
            try {
                std::istringstream iss( ctx_json );
                JsonIn jin( iss );
                JsonObject pkt = jin.get_object();
                pkt.allow_omitted_members();
                if( pkt.has_object( "d" ) ) {
                    JsonObject d = pkt.get_object( "d" );
                    d.allow_omitted_members();
                    if( d.get_string( "type", "" ) == "high_five" ) {
                        auto& sess = coop_session::get();
                        const time_point now = calendar::turn;
                        if( now - sess.last_high_five_turn >= 600_turns ) {
                            g->u.add_morale( morale_type( "morale_coop_bonding" ), 5, 10,
                                             30_minutes, 30_minutes, true );
                            sess.last_high_five_turn = now;
                            send_emote( "high_five" ); // confirmation to client
                            add_msg( m_good, _( "You and %s share a high five!" ),
                                     sess.partner_name );
                        } else {
                            add_msg( m_info, _( "Too soon for another high five!" ) );
                        }
                    }
                }
            } catch( const JsonError& e ) {
                DebugLog( DL::Error, DC::Main ) << "[coop] EMOTE: " << e.what();
            }
        }
        return;
    }


    // Target-position paths: both SMASH and FIRE use typed commands (B3 Phase 5/6).
    if( key == "SMASH" ) {
    if( !ctx_json.empty() ) {
            std::istringstream iss( ctx_json );
            JsonIn jin( iss );
            JsonObject ctx = jin.get_object();
            ctx.allow_omitted_members();
            const tripoint_abs_ms abs_tpos{
                ctx.get_int( "tx", 0 ), ctx.get_int( "ty", 0 ), ctx.get_int( "tz", 0 )};
            // CL-MELEE-WEAPON: arm proxy with client's weapon (require_gun=false).
            const bool armed = arm_proxy_from_ctx( proxy, ctx, false );
            execute_player_cmd( proxy, make_player_smash_cmd( abs_tpos ), seq );
            if( armed ) { proxy->remove_primary_weapon(); }
        } else {
            proxy->moves -= proxy->get_speed();
        }
    } else if( key == "FIRE" ) {
    if( !ctx_json.empty() ) {
            std::istringstream iss( ctx_json );
            JsonIn jin( iss );
            JsonObject ctx = jin.get_object();
            ctx.allow_omitted_members();
            // B3 Phase 6: parse abs coords → typed command → execute_player_cmd.
            const tripoint_abs_ms abs_tpos{
                ctx.get_int( "tx", 0 ), ctx.get_int( "ty", 0 ), ctx.get_int( "tz", 0 )};
            // CL-RANGED: arm proxy with the client's weapon embedded in ctx (require_gun=true).
            const bool armed = arm_proxy_from_ctx( proxy, ctx, true );
            execute_player_cmd( proxy, make_player_fire_cmd( abs_tpos ), seq );
            // Disarm after the shot — proxy must not retain client's weapon between actions.
            if( armed ) { proxy->remove_primary_weapon(); }
        } else {
            DebugLog( DL::Info, DC::Main ) << "[coop] FIRE seq=" << seq << ": no target context";
            proxy->moves -= proxy->get_speed();
        }
    } else if( key == "MOVE_UP" || key == "MOVE_DOWN" ) {
    const auto vc = parse_vertical_move_ctx( ctx_json );
        if( vc ) {
            const tripoint_bub_ms bpos = g->m.abs_to_bub( vc->landing );
            if( g->m.inbounds( bpos ) ) {
                proxy->setpos( bpos );
                DebugLog( DL::Info, DC::Main )
                        << "[coop] " << key << " proxy→z=" << vc->landing.z();
            } else {
                proxy->moves -= proxy->get_speed();
                DebugLog( DL::Info, DC::Main )
                        << "[coop] " << key << " landing out of bounds at z=" << vc->landing.z();
            }
        } else {
            proxy->moves -= proxy->get_speed();
            DebugLog( DL::Info, DC::Main ) << "[coop] " << key << ": no ctx — move consumed";
        }
    } else {
        DebugLog( DL::Debug, DC::Main ) << "[coop] execute_client_action: unimplemented key=" << key;
    }
}

auto coop_lag_find_target(
    const std::deque<coop_entity_snapshot> &history, uint32_t fire_seq,
    const tripoint_abs_ms& target_abs ) -> int
{
    const coop_entity_snapshot* best = nullptr;
    for( const auto& snap : history ) {
        if( snap.seq <= fire_seq && ( !best || snap.seq > best->seq ) ) { best = &snap; }
    }
    if( !best && !history.empty() ) { best = &history.front(); }
    if( !best ) { return -1; }
    for( const auto& [cid, snap_apos] : best->creature_positions ) {
        if( snap_apos == target_abs ) { return cid; }
    }
    return -1;
}

auto coop_server::push_entity_snapshot() -> void
{
    // Ensure monster_id_map_ is up-to-date: assign IDs to any new live monsters.
    // Dead monsters are pruned here to avoid stale pointers in the snapshot.
    std::vector<const monster *> live_ptrs;
    for( monster& mon : g->all_monsters() ) {
        if( !mon.is_dead() ) { live_ptrs.push_back( &mon ); }
    }
    // Sort by abs_pos for deterministic ordering
    std::sort( live_ptrs.begin(), live_ptrs.end(), []( const monster * a, const monster * b ) {
        return a->abs_pos() < b->abs_pos();
    } );
    std::erase_if( monster_id_map_, [&]( const auto & kv ) { return std::find( live_ptrs.begin(), live_ptrs.end(), kv.first ) == live_ptrs.end(); } );
    for( const monster * ptr : live_ptrs ) {
        if( !monster_id_map_.contains( ptr ) ) { monster_id_map_.emplace( ptr, next_monster_id_++ ); }
    }

    coop_entity_snapshot snap;
    snap.seq = last_confirmed_seq_;
    snap.creature_positions.reserve( live_ptrs.size() );
    for( monster& mon : g->all_monsters() ) {
        if( mon.is_dead() ) { continue; }
        const auto it = monster_id_map_.find( &mon );
        if( it != monster_id_map_.end() ) {
            snap.creature_positions.emplace_back( it->second, mon.abs_pos() );
        }
    }

    position_history_.push_back( std::move( snap ) );
    if( position_history_.size() > 10 ) { position_history_.pop_front(); }
}

auto coop_server::resolve_fire_at_seq(
    npc* proxy, uint32_t seq, int target_ax, int target_ay, int target_az ) -> void
{
    if( !proxy ) { return; }

// A5.3: client sends tx/ty/tz as tripoint_abs_ms (globally consistent).
// Derive target_bub for fire_gun + inbounds checks on this machine's reality bubble.
const tripoint_abs_ms target_abs{target_ax, target_ay, target_az};
const tripoint_bub_ms target_bub = g->m.abs_to_bub( target_abs );

// A5.3 lag compensation: find the snapshot closest to the client's fire-seq and
// temporarily reposition the creature that was at the target tile in that snapshot
// but has since moved.  creature_moved events are filtered from the delta stream
// (build_and_send_sync streamable filter), so these temp setpos calls do not appear
// in the client's event log.  The monster section in the next sync always shows the
// authoritative final position.
monster* lag_target = nullptr;
tripoint_bub_ms lag_original_bub;

const int cid_at_target = coop_lag_find_target( position_history_, seq, target_abs );
if( cid_at_target >= 0 ) {
    for( const auto& [ptr, id] : monster_id_map_ ) {
            if( id == cid_at_target && !ptr->is_dead() ) {
                lag_target = const_cast<monster *>( ptr );
                break;
            }
        }
        // Reposition only when the creature has actually moved since the snapshot.
        if( lag_target && lag_target->abs_pos() != target_abs && g->m.inbounds( target_bub ) ) {
            lag_original_bub = lag_target->bub_pos();
            lag_target->setpos( target_bub );
            DebugLog( DL::Info, DC::Main )
                    << "[coop] lag-comp seq=" << seq << ": repositioned cid=" << cid_at_target
                    << " from (" << lag_original_bub.x() << "," << lag_original_bub.y() << ")"
                    << " to target (" << target_ax << "," << target_ay << ")";
        } else {
            lag_target = nullptr; // already at target — no restore needed
        }
    }

    // Execute the shot.  fire_gun does trajectory + hit resolution against the
    // (possibly lag-compensated) world state.
    if( proxy->is_armed() && proxy->primary_weapon().is_gun() ) {
        const int shots = ranged::fire_gun( *proxy, target_bub );
        DebugLog( DL::Info, DC::Main )
                << "[coop] FIRE seq=" << seq << " fired=" << shots << " at (" << target_ax << ","
                << target_ay << "," << target_az << ")";
    } else {
        proxy->moves -= proxy->get_speed();
        DebugLog( DL::Info, DC::Main )
                << "[coop] FIRE seq=" << seq << " proxy unarmed — action consumed";
    }

    // Restore the lag-compensated creature to its authoritative live position.
    if( lag_target && !lag_target->is_dead() ) { lag_target->setpos( lag_original_bub ); }
}

auto coop_server::build_and_send_sync( bool force_full ) -> void
{
    std::ostringstream oss;
    JsonOut jout( oss );
    jout.start_object();
    jout.member( "t", static_cast<int>( coop_pkt::sync ) );
    jout.member( "turn", to_turn<int>( calendar::turn ) );
    jout.member( "last_seq", static_cast<int>( last_confirmed_seq_ ) );

    // A4 delta stream: on regular ticks flush terrain/furniture events from the mutation log
    // instead of sending the full 5×5 submap grid.  Full sync is preserved for:
    //   (a) force_full — initial join, resync_request
    //   (b) map-origin shift — host crossed a submap boundary
    //   (c) 30-second safety net — catches any events the log missed
    const tripoint_abs_sm abs_sub = g->m.get_abs_sub();
    ++sync_tick_counter_;
    const bool origin_changed = ( abs_sub != last_sync_origin_ );
    const bool periodic = ( sync_tick_counter_ % TILE_RESYNC_INTERVAL == 0 );
    // Always evaluate force_resync_.exchange(false) unconditionally — the || short-circuit
    // would skip it when origin_changed or periodic is true, leaving force_resync_ set and
    // causing a spurious extra full sync on the next tick (and failing the test seam CHECK).
    const bool resync_requested = force_resync_.exchange( false );
    const bool send_full_tiles = force_full || origin_changed || periodic || resync_requested;
    if( send_full_tiles ) { last_sync_origin_ = abs_sub; }

    // --- events (delta path) ---
    // Flush terrain+furniture events from the active tick log.
    // Hash is computed only over the SENT subset so the client can replicate it.
    // Hash-based resync_request is NOT enabled yet (A4b) — hash is informational.
    auto* tick_log = coop_mutation_log::current();
    auto sr =
        ( !send_full_tiles && tick_log )
        ? coop_collect_streamable( tick_log->flush() )
        : coop_streamable_result{};
    const auto& sent_events = sr.sent;
    const auto events_hash = sr.hash;
    jout.member( "hash", static_cast<int64_t>( events_hash ) );
    jout.member( "events" );
    jout.start_array();
    for( const auto& ev : sent_events ) {
        jout.start_object();
        jout.member( "ev", static_cast<int>( ev.type ) );
        jout.member( "x", ev.pos.x() );
        jout.member( "y", ev.pos.y() );
        jout.member( "z", ev.pos.z() );
        jout.member( "v", ev.value );
        if( ev.creature_id != 0 ) { jout.member( "cid", ev.creature_id ); }
        jout.end_object();
    }
    jout.end_array();

    // --- tiles (full sync path) ---
    jout.member( "tiles" );
    jout.start_array();
    if( send_full_tiles ) {
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
    // Empty array when using delta — client skips tile processing when empty.
    jout.end_array();

    // H5: assign stable IDs to live monsters keyed by pointer (stable in creature_tracker).
    std::vector<const monster *> live_ptrs;
    for( monster& mon : g->all_monsters() ) {
        if( !mon.is_dead() ) { live_ptrs.push_back( &mon ); }
    }
    // Sort by abs_pos for deterministic ordering
    std::sort( live_ptrs.begin(), live_ptrs.end(), []( const monster * a, const monster * b ) {
        return a->abs_pos() < b->abs_pos();
    } );
    std::erase_if( monster_id_map_, [&]( const auto & kv ) { return std::find( live_ptrs.begin(), live_ptrs.end(), kv.first ) == live_ptrs.end(); } );
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

    // F1: host vitals for client partner HUD
    {
        const bodypart_id torso( "torso" );
        const int max_hp = g->u.get_part_hp_max( torso );
        const int cur_hp = g->u.get_part_hp_cur( torso );
        jout.member( "host_hp_pct", max_hp > 0 ? cur_hp * 100 / max_hp : 100 );
        const int stam_max = g->u.get_stamina_max();
        jout.member( "host_stamina_pct", stam_max > 0
                     ? g->u.get_stamina() * 100 / stam_max
                     : 100 );
        jout.member( "host_activity",
                     g->u.activity ? g->u.activity->get_verb().translated() : std::string{} );
    }
    // F1: client's own abs position echoed back so it can display the arrow
    {
        std::scoped_lock lk{ chat_mtx_ };
        jout.member( "partner_ax", client_abs_pos_.x() );
        jout.member( "partner_ay", client_abs_pos_.y() );
        jout.member( "partner_az", client_abs_pos_.z() );
    }
    // Ping measurement: embed current timestamp; client echoes it back in client_status.
    jout.member( "ping_ts", static_cast<int>( SDL_GetTicks() ) );

    // F3: one-shot tap notification to client (set by send_tap_shoulder() caller)
    if( pending_tap_sent_to_client_ ) {
        jout.member( "tap_pending", true );
        pending_tap_sent_to_client_ = false;
    }

    jout.end_object();
    // Push onto send queue — IO thread is the sole caller of send on the socket (C5).
    std::scoped_lock lk{send_mtx_};
    send_q_.push_back( oss.str() );
}

auto coop_server::handle_client_disconnect() -> void
{
    // Join receiver_thread_ — it has already exited the loop
    if( receiver_thread_.joinable() ) {
    receiver_thread_.request_stop();
        receiver_thread_.join();
    }
    // Drop the dead transport socket
    transport_.reset();
    // Drain stale send queue — packets destined for the dead socket
    {
        std::scoped_lock lk{ send_mtx_ };
        send_q_.clear();
    }
    awaiting_reconnect_.store( true );
    reconnect_countdown_ = RECONNECT_TIMEOUT_TICKS;
    DebugLog( DL::Info, DC::Main )
            << "[coop] Client disconnected — awaiting reconnection (300s timeout)";
    add_msg( m_warning, _( "[ALERT] Partner disconnected! Waiting for reconnection..." ) );
    // Preserve server_sock_, proxy NPC, session state, and ID maps
}

auto coop_server::accept_reconnect() -> bool
{
    // try_accept() is non-blocking; returns false if no pending connection
    if( !try_accept() ) {
    return false;
}
// A new TCP connection is established — read one packet and expect reconnect
std::string buf;
if( !transport_->recv( buf, 5000 ) ) {
    DebugLog( DL::Error, DC::Main ) << "[coop] accept_reconnect: recv failed";
        transport_.reset();
        return false;
    }
    try {
        std::istringstream iss( buf );
        JsonIn jin( iss );
        JsonObject pkt = jin.get_object();
        pkt.allow_omitted_members();
        const auto t = static_cast<coop_pkt>( pkt.get_int( "t" ) );
        if( t != coop_pkt::reconnect ) {
            DebugLog( DL::Error, DC::Main ) << "[coop] accept_reconnect: expected reconnect packet, got "
                                            << pkt.get_int( "t" );
            transport_.reset();
            return false;
        }
        JsonObject d = pkt.get_object( "d" );
        d.allow_omitted_members();
        const std::string token = d.get_string( "session_token", "" );
        if( token.empty() || token != session_token_ ) {
            DebugLog( DL::Error, DC::Main ) << "[coop] accept_reconnect: session token mismatch";
            transport_.reset();
            return false;
        }
    } catch( const JsonError& e ) {
        DebugLog( DL::Error, DC::Main ) << "[coop] accept_reconnect: JSON error: " << e.what();
        transport_.reset();
        return false;
    }
    // Token matches — send full sync and restart receiver
    awaiting_reconnect_.store( false );
    running_.store( true );
    build_and_send_sync( true );
    // Flush the sync packet directly before starting the receiver thread
    {
        std::deque<std::string> outgoing;
        {
            std::scoped_lock lk{ send_mtx_ };
            outgoing.swap( send_q_ );
        }
        for( const auto& frame : outgoing ) {
            transport_->send( frame );
        }
    }
    start_receiver_thread();
    DebugLog( DL::Info, DC::Main ) << "[coop] Client reconnected successfully";
    return true;
}

auto coop_server::shutdown() -> void
{
    const bool was_running = running_.exchange( false );
    awaiting_reconnect_.store( false );
    // Join first — the IO thread checks running_ and st.stop_requested() at the top of
    // each iteration and exits quickly (no long-blocking recv under C5).
    if( receiver_thread_.joinable() ) {
        receiver_thread_.request_stop();
        receiver_thread_.join();
    }
    if( was_running && transport_ ) {
        // Only send disconnect when we initiated the shutdown (not when the receiver
        // already signaled it by clearing running_).
        std::ostringstream oss;
        JsonOut jout( oss );
        jout.start_object();
        jout.member( "t", static_cast<int>( coop_pkt::disconnect ) );
        jout.end_object();
        transport_->send( oss.str() );
    }
    transport_.reset();
    if( server_sock_ ) {
        NET_DestroyServer( server_sock_ );
        server_sock_ = nullptr;
    }
    if( net_initialized_ ) {
        NET_Quit();
        net_initialized_ = false;
    }
    // Clean proxy NPC despawn — removes from active_npc, follower list, and overmapbuffer
    // WITHOUT triggering die(): die() fires C3 death-drop + QUIT_DIED on every shutdown,
    // including normal session-end, scattering proxy gear and spamming death messages.
    // Idempotent: proxy_npc_id reset to invalid, second call is a no-op.
    if( g && coop_session::get().proxy_npc_id.is_valid() ) {
        const character_id pid = coop_session::get().proxy_npc_id;
        coop_session::get().proxy_npc_id = character_id{}; // reset first — prevents re-entry
        const npc* proxy = g->critter_by_id<npc>( pid );
        if( proxy ) {
            g->remove_npc_follower( pid );
            get_overmapbuffer( proxy->get_dimension() ).remove_npc( pid );
        }
        if( !g->is_processing_npcs() ) {
            g->erase_npc( pid );
        }
        DebugLog( DL::Info, DC::Main ) << "[coop] proxy NPC despawned on server shutdown";
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

auto coop_server::send_raw( const std::string& json ) -> void
{
    if( !running_ ) { return; }
std::scoped_lock lk{send_mtx_};
if( send_q_.size() >= 64 ) { send_q_.pop_front(); }
    send_q_.push_back( json );
}

auto coop_server::stabilize_client() -> void
{
    client_downed_.store( false );
    client_down_turns_remaining_ = 0;
    const std::string msg = R"({"t":49})";
    std::scoped_lock lk{ send_mtx_ };
    send_q_.push_back( msg );
    add_msg( m_good, _( "You stabilize your partner." ) );
}

auto coop_server::send_tap_shoulder() -> void
{
    pending_tap_sent_to_client_ = true; // surfaced in next sync's "tap_pending" field
    const std::string msg = R"({"t":46})";
    std::scoped_lock lk{ send_mtx_ };
    send_q_.push_back( msg );
}

auto coop_server::send_emote( const std::string& emote_type ) -> void
{
    const auto msg = string_format( R"({"t":48,"d":{"type":"%s"}})", emote_type );
    std::scoped_lock lk{ send_mtx_ };
    send_q_.push_back( msg );
}

#endif // COOP_ENABLED
