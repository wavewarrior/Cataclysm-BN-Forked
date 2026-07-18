#ifdef COOP_ENABLED

#include "coop_client.h"

#include "avatar.h"
#include "calendar.h"
#include "coop_mutation_log.h" // COOP_FNV_OFFSET, coop_hash_event_fields
#include "coop_overmap.h"
#include "coop_net.h"
#include "coop_packets.h"
#include "coop_proto.h"
#include "coop_reconcile.h"
#include "coop_session.h"
#include "coordinates.h"
#include "creature_tracker.h"
#include "debug.h"
#include "field.h"
#include "field_type.h"
#include "game.h"
#include "game_constants.h"
#include "get_version.h"
#include "json.h"
#include "map.h"
#include "mapbuffer.h"
#include "mapdata.h"
#include "messages.h"
#include "monster.h"
#include "submap.h"
#include "type_id.h"
#include "worldfactory.h"
#include "bodypart.h"
#include "item.h"
#include "morale_types.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "string_utils.h"
#include "rng.h"
#include "player_activity.h"
#include "skill.h"

#include <SDL3_net/SDL_net.h>
#include <sstream>
#include <unordered_set>
#include <vector>

coop_client::~coop_client() { shutdown(); }

auto coop_client::connect( const std::string& ip, uint16_t port ) -> bool
{
    if( !NET_Init() ) {
    DebugLog( DL::Error, DC::Main ) << "[coop] NET_Init failed: " << SDL_GetError();
        return false;
    }
    net_initialized_ = true;
    NET_Address* addr = NET_ResolveHostname( ip.c_str() );
    if( !addr ) {
    DebugLog( DL::Error, DC::Main ) << "[coop] resolve failed: " << SDL_GetError();
        return false;
    }
    // Wait up to 5 seconds for DNS resolution.
    while( NET_GetAddressStatus( addr ) == 0 ) { SDL_Delay( 10 ); }
    if( NET_GetAddressStatus( addr ) < 0 ) {
    NET_UnrefAddress( addr );
        DebugLog( DL::Error, DC::Main ) << "[coop] DNS failed: " << SDL_GetError();
        return false;
    }
    auto* socket = NET_CreateClient( addr, port, 0 );
    NET_UnrefAddress( addr );
    if( !socket ) {
    DebugLog( DL::Error, DC::Main ) << "[coop] connect failed: " << SDL_GetError();
        return false;
    }
    // Wait for non-blocking connect to complete.
    while( NET_GetConnectionStatus( socket ) == 0 ) { SDL_Delay( 10 ); }
    if( NET_GetConnectionStatus( socket ) < 0 ) {
    NET_DestroyStreamSocket( socket );
        DebugLog( DL::Error, DC::Main ) << "[coop] connection refused: " << SDL_GetError();
        return false;
    }
    transport_ = std::make_unique<coop_net_transport>( socket );
    last_host_ip_ = ip;
    last_host_port_ = port;
    coop_session::get().mode = coop_mode::client;
    DebugLog( DL::Info, DC::Main ) << "[coop] connected to " << ip << ":" << port;
    return true;
}

auto coop_client::handshake() -> bool
{
    // Send client handshake first (client goes second per the protocol, but
    // both sides send then receive — order matches coop_server::handshake()).
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
        DebugLog( DL::Error, DC::Main ) << "[coop] client handshake: send failed";
        return false;
    }

    // Receive host handshake
    std::string buf;
    if( !transport_->recv( buf, 5000 ) ) {
        DebugLog( DL::Error, DC::Main ) << "[coop] client handshake: recv failed";
        return false;
    }
    std::istringstream iss( buf );
    JsonIn jin( iss );
    JsonObject pkt = jin.get_object();
    pkt.allow_omitted_members();
    JsonObject d = pkt.get_object( "d" );
    d.allow_omitted_members();
    const std::string host_ver = d.get_string( "version", "" );
    if( host_ver != getVersionString() ) {
        DebugLog( DL::Info, DC::Main )
                << "[coop] version mismatch: client=" << getVersionString() << " host=" << host_ver;
    }
    DebugLog( DL::Info, DC::Main ) << "[coop] client handshake complete";
    return true;
}

auto coop_client::receive_world_seed() -> bool
{
    std::string buf;
    if( !transport_->recv( buf, 5000 ) ) {
        DebugLog( DL::Error, DC::Main ) << "[coop] receive_world_seed: recv failed";
        return false;
    }
    const auto data = parse_world_seed_packet( buf );
    if( !data ) {
        DebugLog( DL::Error, DC::Main ) << "[coop] receive_world_seed: unexpected packet type";
        return false;
    }
    // Guest-model design decision: the client joins as a session guest with no
    // save file (Track A).  All map data — submaps, terrain, items — arrives
    // via send_initial_sync() and periodic syncs; that is the authoritative
    // source of world state for the client.  world_generator::set_active_world
    // is only needed for save/load paths, which guests never exercise.
    //
    // If the client has the named world installed, set_active_world is called
    // for completeness (LAN case where both players share the same world).
    // If not (different machine, or no world installed), gameplay is unaffected:
    // the missing world is logged and the client continues.  This also correctly
    // handles a host with no active world (e.g. a freshly initialised game state).
    if( !data->world_name.empty() ) {
        WORLDINFO* world = world_generator->get_world( data->world_name );
        if( world ) {
            world_generator->set_active_world( world );
        } else {
            DebugLog( DL::Info, DC::Main )
                    << "[coop] receive_world_seed: world '" << data->world_name
                    << "' not found locally — world data will arrive via sync";
        }
    }

    // Store avatar data to apply after g->setup() completes
    world_seed_turn_ = data->turn;
    world_seed_spawn_ = data->spawn_pos;
    world_seed_partner_name_ = data->player_name.empty() ? "Partner" : data->player_name;
    session_token_ = data->session_token;

    // Apply host's RNG seed so world generation matches
    if( data->rng_seed != 0 ) {
        rng_set_engine_seed( data->rng_seed );
    }

    DebugLog( DL::Info, DC::Main ) << "[coop] received world_seed: world=" << data->world_name;
    return true;
}

auto coop_client::apply_world_seed_to_avatar() -> void
{
    if( world_seed_turn_ >= 0 ) { calendar::turn = time_point::from_turn( world_seed_turn_ ); }
    coop_session::get().partner_name = world_seed_partner_name_;
    // C6: skip position override if the client loaded a save (has a real saved position).
    // Fresh characters have abs_pos == {0,0,0}; loaded saves have a real non-zero position.
    // On first join: override to world_seed_spawn_ so the client starts near the host.
    // On rejoin:     keep the saved position; send_join_info() already told the host.
    const bool has_saved_position = ( g->u.abs_pos() != tripoint_abs_ms{0, 0, 0} );
    if( !has_saved_position ) {
        // g->setup() centered the map on the client's own character (or origin), NOT on
        // world_seed_spawn_.  abs_to_bub(world_seed_spawn_) returns nonsense bubble coords
        // until the reality bubble is repositioned — causing the massive drift and
        // movement crash on unloaded submaps.  Replicate start_game (game.cpp:924-936):
        // compute the top-left submap corner and call load_map() to reposition abs_sub.
        const int levz = g->get_levz();
        auto lev = project_to<coords::sm>( world_seed_spawn_ );
        const tripoint_abs_sm abs_sub_before = g->m.get_abs_sub();
        lev.x() -= g_half_mapsize;
        lev.y() -= g_half_mapsize;
        DebugLog( DL::Info, DC::Main )
                << "[coop] load_map: abs_sub_before=(" << abs_sub_before.x() << ","
                << abs_sub_before.y() << ") lev=(" << lev.x() << "," << lev.y() << ")";
        g->load_map( lev, /*pump_events=*/true );
        const tripoint_abs_sm abs_sub_after = g->m.get_abs_sub();
        DebugLog( DL::Info, DC::Main )
                << "[coop] load_map: abs_sub_after=(" << abs_sub_after.x() << ","
                << abs_sub_after.y() << ") spawn=(" << world_seed_spawn_.x() << ","
                << world_seed_spawn_.y() << ")";
        g->m.invalidate_map_cache( levz );
        g->m.build_map_cache( levz );
        const tripoint_bub_ms bpos = g->m.abs_to_bub( world_seed_spawn_ );
        g->u.setpos( bpos );
        DebugLog( DL::Info, DC::Main )
                << "[coop] setpos: bpos=(" << bpos.x() << "," << bpos.y() << ")"
                << " abs_pos_after=(" << g->u.abs_pos().x() << "," << g->u.abs_pos().y() << ")";
        g->m.invalidate_map_cache( levz );
        g->m.build_map_cache( levz );
    }
    g->u.process_turn(); // initialise avatar stats at spawn
    DebugLog( DL::Info, DC::Main )
            << "[coop] applied world_seed to avatar: turn=" << world_seed_turn_
            << " pos_override=" << ( !has_saved_position ? "yes" : "no" );
}

auto coop_client::coop_world_tick() -> void
{
    if( !coop_session::get().is_client() || !transport_ ) { return; }

// 1. Send the oldest unsent pending action.  Actions remain in pending_actions_
//    until the server echoes last_seq ≥ action.seq in a sync packet; they are
//    discarded in apply_sync().  One per tick matches the server's drain rate.
for( auto& act : pending_actions_ ) {
    if( act.sent ) { continue; }
        if( !transport_->send( build_action_packet( {act.seq, act.key, act.ctx_json} ) ) ) {
            DebugLog( DL::Error, DC::Main ) << "[coop] coop_world_tick: action send failed";
            handle_disconnect();
            return;
        }
        act.sent = true;
        break;
    }

#ifdef COOP_ENABLED
    // E1: push vehicle state to host while client is driving.
    if( g->u.controlling_vehicle ) {
    const auto vp = get_map().veh_at( g->u.bub_pos() );
        if( vp ) {
            const vehicle& veh = vp->vehicle();
            const auto vid_it = coop_vehicle_map_inv_.find( &veh );
            if( vid_it != coop_vehicle_map_inv_.end() ) {
                const bool moving = veh.velocity != 0;
                if( moving ) { coop_vehicle_stationary_ticks_ = 0; }
                else         { ++coop_vehicle_stationary_ticks_; }
                if( coop_vehicle_stationary_ticks_ < 3 ) {
                    const auto abs = veh.abs_ms_location();
                    const auto msg = string_format(
                                         R"({"t":42,"vid":%u,"ax":%d,"ay":%d,"az":%d,"face_x":%d,"face_y":%d,"velocity":%d})",
                                         vid_it->second, abs.x(), abs.y(), abs.z(),
                                         veh.face.dx(), veh.face.dy(), veh.velocity );
                    transport_->send( msg );
                }
            }
        }
    }
    // F5: team speed-up — reduce moves_left when both doing the same activity.
    if( g->u.activity && !host_activity_str_.empty() ) {
    if( to_lower_case( host_activity_str_ ) ==
            to_lower_case( g->u.activity->get_verb().translated() ) ) {
            g->u.activity->moves_left =
                std::max( 0, g->u.activity->moves_left - g->u.get_speed() / 2 );
        }
    }
#endif

    // 1b. Send client_status each tick — host uses this for both_idle() fast-forward.
    //     Reports the client's OWN g->u state; proxy-inference is unreliable since
    //     SLEEP/CRAFT stubs don't set proxy->activity.
    {
        const bool idle = g->u.in_sleep_state() || bool( g->u.activity );
        std::ostringstream status_oss;
        JsonOut status_jout( status_oss );
        status_jout.start_object();
        status_jout.member( "t", static_cast<int>( coop_pkt::client_status ) );
        status_jout.member( "d" );
        status_jout.start_object();
        status_jout.member( "idle", idle );
        // C3: per-tick character vital stats so the host can track client health/death.
        status_jout.member( "hp_pct", g->u.hp_percentage() );
        status_jout.member( "stamina", g->u.get_stamina() );
        status_jout.member( "stamina_max", g->u.get_stamina_max() );
        status_jout.member( "dead", g->u.is_dead_state() );
        // F1: new fields — stamina %, current activity, abs position
        const int stam_max = std::max( 1, g->u.get_stamina_max() );
        status_jout.member( "stamina_pct", g->u.get_stamina() * 100 / stam_max );
        status_jout.member( "activity",
                            g->u.activity ? g->u.activity->get_verb().translated() : std::string{} );
        const auto cpos = g->u.abs_pos();
        status_jout.member( "ax", cpos.x() );
        status_jout.member( "ay", cpos.y() );
        status_jout.member( "az", cpos.z() );
        // Ping: echo the host's timestamp back for RTT measurement on host side.
        status_jout.member( "ping_echo", static_cast<int>( last_ping_ts_ ) );
        // Approximate local ping: time since host sent the sync to when we process it.
        if( last_ping_ts_ > 0 ) {
            const auto now_ms = static_cast<uint64_t>( SDL_GetTicks() );
            coop_session::get().partner_ping_ms = static_cast<int>( now_ms - last_ping_ts_ );
        }
        // Skill sync: throttled to every 10 ticks to avoid per-tick overhead.
        if( ++skill_sync_counter_ >= 10 ) {
            skill_sync_counter_ = 0;
            std::vector<std::pair<std::string, int>> skill_pairs;
            for( const auto &[sid, slvl] : g->u.get_all_skills() ) {
                if( slvl.level() > 0 ) {
                    skill_pairs.emplace_back( sid.str(), slvl.level() );
                }
            }
            if( !skill_pairs.empty() ) {
                build_skill_sync_fields( status_jout, skill_pairs );
            }
        }
        status_jout.end_object();
        status_jout.end_object();
        if( !transport_->send( status_oss.str() ) ) {
            // INFO not ERROR: the host closing the connection is a normal teardown path.
            // Logging at ERROR causes Catch2 to fail every scenario's post-signal ticks.
            DebugLog( DL::Info, DC::Main ) <<
            "[coop] coop_world_tick: status send failed — host disconnected";
            handle_disconnect();
            return;
        }
    }


    // 2. Drain all buffered inbound packets from the host (H6).
    while( transport_->poll() ) {
    std::string buf;
    if( !transport_->recv( buf, 0 ) ) {
            handle_disconnect();
            return;
        }
        try {
            std::istringstream iss( buf );
            JsonIn jin( iss );
            JsonObject pkt = jin.get_object();
            pkt.allow_omitted_members();
            const auto t = static_cast<coop_pkt>( pkt.get_int( "t" ) );
            if( t == coop_pkt::sync ) {
                apply_sync( buf );
            } else if( t == coop_pkt::chat ) {
                JsonObject d = pkt.get_object( "d" );
                d.allow_omitted_members();
                add_msg( m_info, "[host]: %s", d.get_string( "text", "" ) );
            } else if( t == coop_pkt::disconnect ) {
                DebugLog( DL::Info, DC::Main ) << "[coop] host sent disconnect";
                handle_disconnect();
                return;
            } else if( t == coop_pkt::stabilize ) {
                // G2: host stabilized downed client — restore 5% torso HP.
                const int max_hp = g->u.get_part_hp_max( body_part_torso );
                g->u.set_part_hp_cur( body_part_torso, std::max( 1, max_hp * 5 / 100 ) );
                coop_session::get().is_downed = false;
                add_msg( m_good, _( "Your partner stabilizes you!" ) );
            } else if( t == coop_pkt::trade_accept ) {
                // F2: host accepted the item we sent.
                add_msg( m_good, _( "Partner accepted your item." ) );
            } else if( t == coop_pkt::trade_reject ) {
                // F2: host declined — item must be restored; CoopServer will re-send it in
                // next sync under "pending_gift".  Message only on this side.
                add_msg( m_info, _( "Partner declined your item." ) );
            } else if( t == coop_pkt::emote ) {
                // F6: high-five emote from host.
                JsonObject d = pkt.get_object( "d" );
                d.allow_omitted_members();
                const auto etype = d.get_string( "type", "" );
                if( etype == "high_five" ) {
                    const auto& sess = coop_session::get();
                    const auto delta = sess.partner_abs_pos - g->u.abs_pos();
                    const int dist2 = delta.x() * delta.x() + delta.y() * delta.y();
                    if( dist2 <= 4 ) {
                        g->u.add_morale( morale_type( "morale_coop_bonding" ),
                                         5, 10, 30_minutes, 30_minutes, true );
                        coop_session::get().last_high_five_turn = calendar::turn;
                        add_msg( m_good, _( "High five! You feel great." ) );
                    } else {
                        add_msg( m_info, _( "Partner tried to high five but is too far away." ) );
                    }
                }
            } else if( t == coop_pkt::overmap_mark ) {
                // F4: partner placed or cleared a shared overmap marker.
                JsonObject d = pkt.get_object( "d" );
                d.allow_omitted_members();
                auto& sess = coop_session::get();
                if( d.get_bool( "clear", false ) ) {
                    sess.shared_mark = std::nullopt;
                    sess.shared_mark_label.clear();
                } else {
                    sess.shared_mark = tripoint_abs_omt{
                        d.get_int( "omx", 0 ),
                        d.get_int( "omy", 0 ),
                        d.get_int( "omz", 0 ) };
                    sess.shared_mark_label = d.get_string( "label", "" );
                }
            } else if( t == coop_pkt::overmap_sync ) {
                // Shared overmap: host revealed tiles — main thread, safe to apply directly.
                const auto tiles = parse_overmap_sync_tiles( buf );
                if( !tiles.empty() ) {
                    apply_overmap_sync_tiles( tiles, coop_session::get().dimension_id );
                }
            }
            // other packet types silently ignored
        } catch( const JsonError& e ) {
            DebugLog( DL::Error, DC::Main )
                    << "[coop] coop_world_tick: JSON parse error: " << e.what();
        }
    }
}

auto coop_client::queue_action( const std::string& key, const std::string& ctx_json ) -> void
{
    // handle_action_from() already ran the local game simulation; we only need
    // to stamp a seq number and store the action for replay during reconciliation.
    pending_actions_.push_back( {next_seq_++, key, ctx_json, false, std::nullopt} );
    auto &act = pending_actions_.back();

    // Predict outcome for combat actions so we can verify against server on sync.
    if( key == "SMASH" || key == "FIRE" || key == "MELEE" ) {
    predict_action_locally( act );
    }

    // Ring buffer cap: 32 entries (~500 ms at 60 fps input rate).
    // Overflow is a rare teleport snap — not a crash.
    if( pending_actions_.size() > 32 ) { pending_actions_.pop_front(); }
}

auto coop_client::predict_action_locally( pending_action &act ) -> void
{
    if( !g ) { return; }

// Parse target position from ctx_json
std::istringstream iss( act.ctx_json );
JsonIn jin( iss );
JsonObject d = jin.get_object();

int tx = 0, ty = 0, tz = 0;
d.read( "tx", tx );
d.read( "ty", ty );
d.read( "tz", tz );
const tripoint_abs_ms target_abs{ tx, ty, tz };

predicted_outcome outcome;
outcome.target_pos = g->m.abs_to_bub( target_abs );

// Find monster at target position and record post-action HP
const auto bub_target = g->m.abs_to_bub( target_abs );
const monster *mon = g->critter_at<monster>( bub_target );
if( mon && mon->friendly <= 0 ) {
    outcome.target_expected_hp = mon->get_hp();
    }

    // Record self HP after action
    outcome.self_expected_hp = g->u.get_hp();

    // Record terrain state at target (post-smash)
    if( act.key == "SMASH" ) {
    const auto ter_sid = g->m.ter( bub_target ).id();
        const auto furn_sid = g->m.furn( bub_target ).id();
        if( !ter_sid.is_null() ) {
            outcome.terrain_change = ter_sid.str();
        }
        if( !furn_sid.is_null() ) {
            outcome.item_id = furn_sid.str();
        }
    }

    act.outcome = outcome;
}


auto coop_client::queue_terrain_change(
    const tripoint_abs_ms &pos, const std::string &ter_id,
    const std::string &furn_id ) -> void
{
    std::ostringstream oss;
    JsonOut jout( oss );
    jout.start_object();
    jout.member( "tx", pos.x() );
    jout.member( "ty", pos.y() );
    jout.member( "tz", pos.z() );
    jout.member( "ter",  ter_id );
    jout.member( "furn", furn_id );
    jout.end_object();
    queue_action( "TERRAIN_CHANGE", oss.str() );
}

auto coop_client::apply_sync( const std::string& json_buf ) -> void
{
    std::istringstream iss( json_buf );
    JsonIn jin( iss );

    // Capture turn before parsing so we know how many turns this sync advances.
    // During fast-forward the host may burst 2–3 turns per outer-loop cycle;
    // each turn needs exactly one process_turn() call to keep avatar stats in sync.
    const auto turn_before = calendar::turn;
    // A2: track confirmation seq and proxy position availability for deferred reconciliation.
    int last_seq_from_sync = -1; // -1 = not present in this packet (pre-A2 host)
    bool got_proxy_pos = false;
    // A4b: replicate the server-side FNV-1a hash over sent events so we can detect
    // delta desync.  Server mixes creature_id UNCONDITIONALLY (even when 0), but only
    // serialises "cid" in JSON when non-zero.  Client initialises ev_cid=0 per event and
    // always mixes it — so absent "cid" → ev_cid=0 → mix(0) — which is NOT a no-op
    // (hash *= prime).  Order must match server exactly: type,x,y,z,v,cid.
    uint64_t server_hash = COOP_FNV_OFFSET; // default: no events
    uint64_t local_hash = COOP_FNV_OFFSET;
    int ev_count = 0;
    bool got_tiles = false;
    int turn_val = to_turn<int>( calendar::turn );
    // Save previous host position before parsing — JSON member order is not guaranteed,
    // so we can't rely on seeing host_ax first.
    prev_host_apos_ = sync_host_apos_;

    // Use raw JsonIn iteration to handle the mixed-member tile objects.
    // The outer sync object has: "t", "turn", "tiles", "monsters", "proxy_*", "host_*".
    jin.start_object();
    while( !jin.end_object() ) {
    const std::string key = jin.get_member_name();

        if( key == "last_seq" ) {
            last_seq_from_sync = jin.get_int();

        } else if( key == "turn" ) {
            turn_val = jin.get_int();
            if( turn_val >= 0 ) { calendar::turn = time_point::from_turn( turn_val ); }

        } else if( key == "hash" ) {
            // A4b: read and store; compared after parsing when delta path was used.
            // Server stores as int64_t; reinterpret via uint64_t cast to preserve bits.
            server_hash = static_cast<uint64_t>( jin.get_int64() );

        } else if( key == "events" ) {
            // A4 delta stream: apply terrain/furniture events in-place.
            // Avoids the 5×5 submap blast on every tick.
            jin.start_array();
            while( !jin.end_array() ) {
                jin.start_object();
                int ev_type = 0, ex = 0, ey = 0, ez = 0, ev_val = 0, ev_cid = 0;
                while( !jin.end_object() ) {
                    const auto mk = jin.get_member_name();
                    if( mk == "ev" ) {
                        ev_type = jin.get_int();
                    } else if( mk == "x" ) {
                        ex = jin.get_int();
                    } else if( mk == "y" ) {
                        ey = jin.get_int();
                    } else if( mk == "z" ) {
                        ez = jin.get_int();
                    } else if( mk == "v" ) {
                        ev_val = jin.get_int();
                    } else if( mk == "cid" ) {
                        ev_cid = jin.get_int();
                    } else {
                        jin.skip_value();
                    }
                }
                // A4b: mix event into local hash — same 6-field order as server.
                // ev_cid is always mixed (including when 0 / "cid" absent from JSON).
                // Test seam: skip_one_hash_event_for_test_ causes the FIRST event's hash
                // to be omitted, inducing local_hash ≠ server_hash so the detection path
                // at line 543 fires naturally without a direct resync_request injection.
                if( skip_one_hash_event_for_test_ ) {
                    skip_one_hash_event_for_test_ = false; // consume — only skip once
                } else {
                    local_hash =
                        coop_hash_event_fields( local_hash, ev_type, ex, ey, ez, ev_val, ev_cid );
                }
                ++ev_count;
                const tripoint_abs_ms abs_pos{ex, ey, ez};
                const tripoint_bub_ms bpos = g->m.abs_to_bub( abs_pos );
                using evt = coop_event_type;
                if( ev_type == static_cast<int>( evt::terrain_changed ) ) {
                    const ter_id ter{ ev_val };
                    const int old_ter = g->m.ter( bpos ).to_i();
                    if( ter ) { g->m.ter_set( bpos, ter ); }
                    {
                        coop_world_event recorded_ev;
                        recorded_ev.type = static_cast<coop_event_type>( ev_type );
                        recorded_ev.pos = abs_pos;
                        recorded_ev.value = ev_val;
                        recorded_ev.old_value = old_ter;
                        recorded_ev.creature_id = ev_cid;
                        rollback_engine_.push( turn_val, recorded_ev );
                    }
                } else if( ev_type == static_cast<int>( evt::furniture_changed ) ) {
                    const int old_furn = g->m.furn( bpos ).to_i();
                    g->m.furn_set( bpos, furn_id{ ev_val } );
                    {
                        coop_world_event recorded_ev;
                        recorded_ev.type = static_cast<coop_event_type>( ev_type );
                        recorded_ev.pos = abs_pos;
                        recorded_ev.value = ev_val;
                        recorded_ev.old_value = old_furn;
                        recorded_ev.creature_id = ev_cid;
                        rollback_engine_.push( turn_val, recorded_ev );
                    }
                } else if( ev_type == static_cast<int>( evt::field_created ) ) {
                    const field_type_id ftype{ ev_val };
                    const int intensity = ev_cid > 0 ? ev_cid : 1;
                    if( ftype ) { g->m.add_field( bpos, ftype, intensity, 0_turns ); }
                    {
                        coop_world_event recorded_ev;
                        recorded_ev.type = static_cast<coop_event_type>( ev_type );
                        recorded_ev.pos = abs_pos;
                        recorded_ev.value = ev_val;
                        recorded_ev.old_value = 0; // no field existed before
                        recorded_ev.creature_id = ev_cid;
                        rollback_engine_.push( turn_val, recorded_ev );
                    }
                } else if( ev_type == static_cast<int>( evt::field_changed ) ) {
                    const field_type_id ftype{ ev_val };
                    const int new_int = ev_cid;
                    int old_int = 0;
                    if( ftype && new_int > 0 ) {
                        auto* fe = g->m.get_field( bpos ).find_field( ftype );
                        if( fe ) {
                            old_int = fe->get_field_intensity();
                            fe->set_field_intensity( new_int );
                        }
                    }
                    {
                        coop_world_event recorded_ev;
                        recorded_ev.type = static_cast<coop_event_type>( ev_type );
                        recorded_ev.pos = abs_pos;
                        recorded_ev.value = ev_val;
                        recorded_ev.old_value = old_int;
                        recorded_ev.creature_id = ev_cid;
                        rollback_engine_.push( turn_val, recorded_ev );
                    }
                } else if( ev_type == static_cast<int>( evt::field_expired ) ) {
                    g->m.remove_field( bpos, field_type_id{ ev_val } );
                    {
                        coop_world_event recorded_ev;
                        recorded_ev.type = static_cast<coop_event_type>( ev_type );
                        recorded_ev.pos = abs_pos;
                        recorded_ev.value = ev_val;
                        recorded_ev.old_value = 0; // field existed before expiration
                        recorded_ev.creature_id = ev_cid;
                        rollback_engine_.push( turn_val, recorded_ev );
                    }
                }
                // creature_moved/died: not streamed in A4b (monsters ride monster section).
                // item_spawned: deferred (no item payload; 30s full sync covers drift).
            }
        } else if( key == "tiles" ) {
            // Each entry is: { "version": N, "coordinates": [x,y,z], <submap members> }
            // This is the standard mapbuffer format — see mapbuffer::deserialize_into_vec.
            jin.start_array();
            while( !jin.end_array() ) {
                got_tiles                    = true;
                got_full_tile_sync_for_test_ = true; // record for test assertions
                int version = savegame_version;
                tripoint_abs_sm sm_pos;
                auto new_sm = std::unique_ptr<submap> {};

                jin.start_object();
                while( !jin.end_object() ) {
                    const std::string tile_key = jin.get_member_name();
                    if( tile_key == "version" ) {
                        version = jin.get_int();
                    } else if( tile_key == "coordinates" ) {
                        jin.start_array();
                        const int x = jin.get_int();
                        const int y = jin.get_int();
                        const int z = jin.get_int();
                        jin.end_array();
                        sm_pos = tripoint_abs_sm{x, y, z};
                        new_sm = std::make_unique<submap>( sm_pos );
                    } else if( new_sm ) {
                        // All other members are submap payload: terrain, furniture, items, etc.
                        new_sm->load( jin, tile_key, version, project_to<coords::ms>( sm_pos ) );
                    } else {
                        jin.skip_value();
                    }
                }

                if( new_sm ) {
                    submap* existing = MAPBUFFER.lookup_submap_in_memory( sm_pos );
                    if( existing ) {
                        // Atomically swap host-authoritative data into the live submap,
                        // then mark all caches dirty so the renderer sees updated tiles.
                        submap::swap( *existing, *new_sm );
                        existing->transparency_dirty = true;
                        existing->outside_dirty = true;
                        existing->floor_dirty = true;
                        existing->pf_dirty = true;
                    } else {
                        MAPBUFFER.add_submap( sm_pos, new_sm );
                    }
                }
            }
            // Invalidate the map's high-level visibility caches after bulk update.
            g->m.invalidate_visibility_caches();

        } else if( key == "monsters" ) {
            // H5: delta-update by host-assigned stable ID.
            // Server assigns sequential IDs to monster pointers (stable in creature_tracker);
            // client tracks host_id → local monster*.  Stationary monsters are updated in-place
            // (no respawn, references stay valid).  Moving monsters get despawned/respawned once
            // per move step — still far better than every-tick full respawn.
            std::unordered_set<int> received_ids;
            jin.start_array();
            while( !jin.end_array() ) {
                jin.start_object();
                int host_id = -1;
                mtype_id type_id;
                tripoint_abs_ms apos;
                int hp = -1;
                bool dead = false;

                while( !jin.end_object() ) {
                    const auto mk = jin.get_member_name();
                    if( mk == "id" ) {
                        host_id = jin.get_int();
                    } else if( mk == "type" ) {
                        type_id = mtype_id( jin.get_string() );
                    } else if( mk == "ax" ) {
                        apos.x() = jin.get_int();
                    } else if( mk == "ay" ) {
                        apos.y() = jin.get_int();
                    } else if( mk == "az" ) {
                        apos.z() = jin.get_int();
                    } else if( mk == "hp" ) {
                        hp = jin.get_int();
                    } else if( mk == "dead" ) {
                        dead = jin.get_bool();
                    } else {
                        jin.skip_value();
                    }
                }
                if( dead || type_id.is_empty() || host_id < 0 ) { continue; }
                received_ids.insert( host_id );

                const tripoint_bub_ms bpos = g->m.abs_to_bub( apos );
                const auto it = coop_monster_map_.find( host_id );
                if( it != coop_monster_map_.end() && it->second && !it->second->is_dead() ) {
                    monster& existing = *it->second;
                    if( existing.bub_pos() == bpos ) {
                        // Same position — update hp in-place, no respawn.
                        if( hp >= 0 ) { existing.set_hp( hp ); }
                    } else {
                        // Monster moved — despawn old, spawn at new position, update map.
                        g->despawn_monster( existing );
                        monster* mon = g->place_critter_at( type_id, bpos );
                        it->second = mon;
                        if( mon && hp >= 0 ) { mon->set_hp( hp ); }
                    }
                } else {
                    // New or stale entry — spawn and track.
                    monster* mon = g->place_critter_at( type_id, bpos );
                    coop_monster_map_[host_id] = mon;
                    if( mon && hp >= 0 ) { mon->set_hp( hp ); }
                }
            }

            // Despawn locals whose host_id was absent from the sync; remove from map.
            std::erase_if( coop_monster_map_, [&]( const auto & kv ) {
                if( received_ids.contains( kv.first ) ) { return false; }
                if( kv.second && !kv.second->is_dead() ) { g->despawn_monster( *kv.second ); }
                return true;
            } );
        } else if( key == "proxy_ax" ) {
            sync_proxy_apos_.x() = jin.get_int();
        } else if( key == "proxy_ay" ) {
            sync_proxy_apos_.y() = jin.get_int();
        } else if( key == "proxy_az" ) {
            sync_proxy_apos_.z() = jin.get_int();
            got_proxy_pos = true; // defer reconciliation until after JSON parsing
        } else if( key == "host_ax" ) {
            sync_host_apos_.x() = jin.get_int();
        } else if( key == "host_ay" ) {
            sync_host_apos_.y() = jin.get_int();
        } else if( key == "host_az" ) {
            sync_host_apos_.z() = jin.get_int();
            // Record timestamp after all three components are set
            last_host_pos_time_ = static_cast<uint64_t>( SDL_GetTicks() );
        } else if( key == "host_hp_pct" ) {
            coop_session::get().partner_hp_pct = jin.get_int();
        } else if( key == "host_stamina_pct" ) {
            coop_session::get().partner_stamina_pct = jin.get_int();
        } else if( key == "host_activity" ) {
            host_activity_str_ = jin.get_string();
            coop_session::get().partner_activity_str = host_activity_str_;
        } else if( key == "partner_ax" ) {
            coop_session::get().partner_abs_pos.x() = jin.get_int();
        } else if( key == "partner_ay" ) {
            coop_session::get().partner_abs_pos.y() = jin.get_int();
        } else if( key == "partner_az" ) {
            coop_session::get().partner_abs_pos.z() = jin.get_int();
        } else if( key == "tap_pending" ) {
            if( jin.get_bool() ) {
                if( g->u.activity ) { g->u.cancel_activity(); }
                add_msg( m_info, _( "[%s] taps you on the shoulder!" ),
                         coop_session::get().partner_name );
            }
        } else if( key == "pending_gift" ) {
            const auto gift_json = jin.get_string();
            if( !gift_json.empty() ) {
                try {
                    std::istringstream gift_iss( gift_json );
                    JsonIn gift_jin( gift_iss );
                    auto gifted = item::spawn( gift_jin );
                    if( gifted ) {
                        const auto name = gifted->tname();
                        g->u.i_add( std::move( gifted ) );
                        add_msg( m_good, _( "[Partner] gave you: %s" ), name );
                    }
                } catch( const JsonError & ) {
                    DebugLog( DL::Error, DC::Main ) << "[coop] pending_gift: JSON error";
                }
            }
        } else if( key == "ping_ts" ) {
            last_ping_ts_ = static_cast<uint64_t>( jin.get_int() );
        } else {
            jin.skip_value();
        }
    }

    // A4b: hash integrity gate — only valid when we used the delta path (events received,
    // no full-tile payload).  Full-tile syncs start from a clean state; hash is irrelevant.
    // On mismatch: log and send resync_request.  Server handles it in receiver_loop by
    // setting force_resync_ = true, which triggers build_and_send_sync(force_full=true).
    if( ev_count > 0 && !got_tiles && local_hash != server_hash ) {
    DebugLog( DL::Info, DC::Main )
                << "[coop] apply_sync: hash mismatch ev=" << ev_count << " local=0x" << std::hex
                << local_hash << " server=0x" << server_hash << std::dec << " — requesting resync";
        // Attempt to roll back locally-applied deltas before requesting a full resync.
        // This undoes events from the current turn so the incoming full sync applies cleanly.
        rollback_engine_.rollback_to( turn_val - 1 );
        std::ostringstream rss;
        JsonOut rsj( rss );
        rsj.start_object();
        rsj.member( "t", static_cast<int>( coop_pkt::resync_request ) );
        rsj.end_object();
        transport_->send( rss.str() );
    }

    // A2: deferred seq-based reconciliation.
    // 0. Verify predictions for confirmed combat actions BEFORE discarding them.
    if( last_seq_from_sync >= 0 ) {
    const auto confirmed = static_cast<uint32_t>( last_seq_from_sync );
        for( const auto &act : pending_actions_ ) {
            if( act.seq <= confirmed && act.outcome.has_value() ) {
                const auto &pred = act.outcome.value();
                if( pred.target_expected_hp >= 0 ) {
                    const monster *mon = g->critter_at<monster>( pred.target_pos );
                    const int actual_hp = mon ? mon->get_hp() : 0;
                    if( actual_hp != pred.target_expected_hp ) {
                        DebugLog( DL::Info, DC::Main )
                                << "[coop] prediction mismatch: " << act.key
                                << " seq=" << act.seq
                                << " expected_hp=" << pred.target_expected_hp
                                << " actual_hp=" << actual_hp;
                    }
                }
            }
        }
    }
    // 1. Discard actions the server has already processed.
    if( last_seq_from_sync >= 0 ) {
    const auto confirmed = static_cast<uint32_t>( last_seq_from_sync );
        std::erase_if( pending_actions_, [confirmed]( const auto & a ) { return a.seq <= confirmed; } );
    }
    // 2. Compute new position via pure reconcile function; apply it.
    //    coop_reconcile_pos handles both the seq-replay path and the fallback
    //    snap-only path (last_seq < 0) in one call.
    if( got_proxy_pos ) {
    const auto client_apos = g->u.abs_pos();
        const int dx = client_apos.x() - sync_proxy_apos_.x();
        const int dy = client_apos.y() - sync_proxy_apos_.y();
        const int dz = client_apos.z() - sync_proxy_apos_.z();
        if( last_seq_from_sync >= 0 || std::abs( dx ) > 20 || std::abs( dy ) > 20 || dz != 0 ) {
            // Build input for pure reconcile function.
            std::vector<reconcile_action> racts;
            racts.reserve( pending_actions_.size() );
            for( const auto& a : pending_actions_ ) { racts.emplace_back( a.seq, a.key ); }
            const tripoint_bub_ms server_bpos = g->m.abs_to_bub( sync_proxy_apos_ );
            g->u.setpos( coop_reconcile_pos( server_bpos, last_seq_from_sync, racts ) );
            if( std::abs( dx ) > 1 || std::abs( dy ) > 1 || dz != 0 ) {
                DebugLog( DL::Info, DC::Main )
                        << "[coop] reconciled: drift=" << dx << "," << dy
                        << " pending=" << pending_actions_.size() << " last_seq=" << last_seq_from_sync;
            }
        }
    }

    // Process turns for the delta between turn_before and the new calendar::turn.
    // During fast-forward the host may advance up to COOP_ACTIVITY_YIELD_INTERVAL turns in
    // one burst (A5.2 host-activity yield cap).  Without matching this cap the client's avatar
    // skips N-1 process_turn() calls → wakes unrested, heals incorrectly, effects don't tick.
    // Cap matches the host's burst limit so both sides always stay in step.
    const int turns_advanced =
        std::max( 0, to_turn<int>( calendar::turn ) - to_turn<int>( turn_before ) );
    const int catch_up = std::min( turns_advanced, COOP_ACTIVITY_YIELD_INTERVAL );
    for( int i = 0; i < catch_up; ++i ) { g->u.process_turn(); }
}

auto coop_client::handle_disconnect() -> void
{
    // Drop the dead transport — do NOT send disconnect packet (connection is already dead)
    transport_.reset();

    // Attempt automatic reconnection if we have a session token
    if( !session_token_.empty() && !last_host_ip_.empty() ) {
    add_msg( m_warning, _( "Connection lost — attempting to reconnect..." ) );
        for( int i = 0; i < 30; ++i ) { // 30 attempts over ~30 seconds
            SDL_Delay( 1000 );
            if( attempt_reconnect( last_host_ip_, last_host_port_ ) ) {
                add_msg( m_good, _( "Reconnected!" ) );
                return;
            }
        }
        add_msg( m_bad, _( "Failed to reconnect. Session ended." ) );
    }

    // C6: persist character state (inventory, position, skills, HP) on disconnect.
    // game::save(false) is the public save path (same as quicksave); saves player data,
    // map memory, and any world state the client owns.
    if( g ) { g->save( false ); }
    shutdown();
}

auto coop_client::attempt_reconnect( const std::string& ip, uint16_t port ) -> bool
{
    // Try to establish a new TCP connection to the host
    NET_Address* addr = NET_ResolveHostname( ip.c_str() );
    if( !addr ) {
        return false;
    }
    while( NET_GetAddressStatus( addr ) == 0 ) { SDL_Delay( 10 ); }
    if( NET_GetAddressStatus( addr ) < 0 ) {
        NET_UnrefAddress( addr );
        return false;
    }
    auto* socket = NET_CreateClient( addr, port, 0 );
    NET_UnrefAddress( addr );
    if( !socket ) {
        return false;
    }
    while( NET_GetConnectionStatus( socket ) == 0 ) { SDL_Delay( 10 ); }
    if( NET_GetConnectionStatus( socket ) < 0 ) {
        NET_DestroyStreamSocket( socket );
        return false;
    }
    transport_ = std::make_unique<coop_net_transport>( socket );

    // Send reconnect packet with session token
    {
        std::ostringstream oss;
        JsonOut jout( oss );
        jout.start_object();
        jout.member( "t", static_cast<int>( coop_pkt::reconnect ) );
        jout.member( "d" );
        jout.start_object();
        jout.member( "session_token", session_token_ );
        jout.end_object();
        jout.end_object();
        if( !transport_->send( oss.str() ) ) {
            transport_.reset();
            return false;
        }
    }

    // Wait for the host to respond with a full sync
    std::string buf;
    if( !transport_->recv( buf, 5000 ) ) {
        DebugLog( DL::Error, DC::Main ) << "[coop] attempt_reconnect: no sync response";
        transport_.reset();
        return false;
    }

    // Reset ring buffer — start fresh after reconnection
    pending_actions_.clear();
    next_seq_ = 1;

    // Apply the full sync from host
    try {
        std::istringstream iss( buf );
        JsonIn jin( iss );
        JsonObject pkt = jin.get_object();
        pkt.allow_omitted_members();
        const auto t = static_cast<coop_pkt>( pkt.get_int( "t" ) );
        if( t != coop_pkt::sync ) {
            DebugLog( DL::Error, DC::Main ) << "[coop] attempt_reconnect: expected sync, got "
                                            << pkt.get_int( "t" );
            transport_.reset();
            return false;
        }
        apply_sync( buf );
    } catch( const JsonError& e ) {
        DebugLog( DL::Error, DC::Main ) << "[coop] attempt_reconnect: JSON error: " << e.what();
        transport_.reset();
        return false;
    }

    coop_session::get().mode = coop_mode::client;
    DebugLog( DL::Info, DC::Main ) << "[coop] reconnected to " << ip << ":" << port;
    return true;
}

auto coop_client::shutdown() -> void
{
    if( transport_ ) {
    std::ostringstream oss;
    JsonOut jout( oss );
        jout.start_object();
        jout.member( "t", static_cast<int>( coop_pkt::disconnect ) );
        jout.end_object();
        transport_->send( oss.str() );
        transport_.reset();
    }
    if( net_initialized_ ) {
    NET_Quit();
        net_initialized_ = false;
    }
    coop_session::get().mode = coop_mode::none;
    if( g ) { g->coop_client_ = nullptr; }
DebugLog( DL::Info, DC::Main ) << "[coop] client shutdown";
}

auto coop_client::send_join_info() -> bool
{
    if( !transport_ || !g ) { return false; }
const auto ap = g->u.abs_pos();
// G1: serialize worn items so the host proxy NPC spawns with correct armor.
std::string worn_json;
{
    std::ostringstream worn_oss;
    JsonOut worn_jout( worn_oss );
        worn_jout.start_array();
        for( const item * w : g->u.worn ) {
            w->serialize( worn_jout );
        }
        worn_jout.end_array();
        worn_json = worn_oss.str();
    }
    const bool ok = transport_->send( build_join_info_packet( {ap, worn_json} ) );
    DebugLog( DL::Info, DC::Main )
            << "[coop] send_join_info: (" << ap.x() << "," << ap.y() << "," << ap.z() << ")"
            << " worn=" << g->u.worn.size() << ( ok ? "" : " — send failed" );
    return ok;
}

auto coop_client::send_chat( const std::string& text ) -> void
{
    if( !transport_ ) { return; }
std::ostringstream oss;
JsonOut jout( oss );
jout.start_object();
jout.member( "t", static_cast<int>( coop_pkt::chat ) );
    jout.member( "d" );
    jout.start_object();
    jout.member( "from", "client" );
    jout.member( "text", text );
    jout.end_object();
    jout.end_object();
    transport_->send( oss.str() );
}


auto coop_client::notify_death() -> void
{
    if( !transport_ || !g || death_notified_ ) { return; }
death_notified_ = true;

// 1. Send client_status dead=true immediately — this packet is normally sent
//    BEFORE apply_sync/process_turn on each tick, so the killing tick never sees
//    dead=true in the per-tick status.  Sending it here fixes C3b host message.
{
    std::ostringstream oss;
    JsonOut jout( oss );
        jout.start_object();
        jout.member( "t", static_cast<int>( coop_pkt::client_status ) );
        jout.member( "d" );
        jout.start_object();
        jout.member( "idle", false );
        jout.member( "hp_pct", 0 );
        jout.member( "stamina", 0 );
        jout.member( "stamina_max", g->u.get_stamina_max() );
        jout.member( "dead", true );
        jout.end_object();
        jout.end_object();
        transport_->send( oss.str() );
    }

    // 2. Death-drop: serialise inv_dump() as an action packet sent directly (not
    //    queued) so it transmits before is_game_over() → teardown closes the socket.
    const auto items = g->u.inv_dump();
    if( items.empty() ) {
        DebugLog( DL::Info, DC::Main ) << "[coop] C3 death-drop: no items to drop";
        return;
    }
    const auto drop_abs = g->u.abs_pos();
    // Build the DROP manifest (same format as C2a apply_drop_manifest).
    std::ostringstream mfst_oss;
    JsonOut mfst( mfst_oss );
    mfst.start_object();
    mfst.member( "items" );
    mfst.start_array();
    int serialized = 0;
for( const item * it : items ) {
    if( !it || it->is_null() ) { continue; }
        mfst.start_object();
        mfst.member( "tx", drop_abs.x() );
        mfst.member( "ty", drop_abs.y() );
        mfst.member( "tz", drop_abs.z() );
        std::ostringstream item_oss;
        JsonOut jitem( item_oss );
        it->serialize( jitem );
        mfst.member( "data", item_oss.str() );
        mfst.end_object();
        ++serialized;
    }
    mfst.end_array();
    mfst.end_object();
    // Wrap in an action packet and send directly (bypass pending_actions_ queue).
    const std::string action_json =
        build_action_packet( {next_seq_++, "DROP", mfst_oss.str()} );
    transport_->send( action_json );
    DebugLog( DL::Info, DC::Main )
            << "[coop] C3 death-drop: " << serialized << " items at ("
            << drop_abs.x() << "," << drop_abs.y() << "," << drop_abs.z() << ")";
}

// send_death_drop() is superseded by notify_death(); kept for any future deathcam path.
auto coop_client::send_death_drop() -> void { notify_death(); }

auto coop_client::send_tap_shoulder() -> void
{
    if( !transport_ ) { return; }
transport_->send( R"({"t":46})" );
}

auto coop_client::send_emote( const std::string& emote_type ) -> void
{
    if( !transport_ ) { return; }
// F6: cooldown check — 600 turns (≈10 game-minutes)
auto& sess = coop_session::get();
if( calendar::turn - sess.last_high_five_turn < 600_turns ) {
    add_msg( m_info, _( "Too soon for another high five!" ) );
        return;
    }
    transport_->send( string_format( R"({"t":48,"d":{"type":"%s"}})", emote_type ) );
}

auto coop_client::send_raw( const std::string& json ) -> void
{
    if( !transport_ ) { return; }
transport_->send( json );
}

auto coop_client::interpolate_host_pos() -> tripoint_abs_ms
{
    // No data yet — return zero position
    if( sync_host_apos_.x() == 0 && sync_host_apos_.y() == 0 ) {
        return sync_host_apos_;
    }

    const uint64_t elapsed = SDL_GetTicks() - last_host_pos_time_;
    // Cap interpolation at 500ms — don't extrapolate too far between syncs
    const float t = std::min( static_cast<float>( elapsed ) / 500.0f, 1.0f );

    return tripoint_abs_ms{
        static_cast<int>( prev_host_apos_.x() + ( sync_host_apos_.x() - prev_host_apos_.x() ) * t ),
        static_cast<int>( prev_host_apos_.y() + ( sync_host_apos_.y() - prev_host_apos_.y() ) * t ),
        sync_host_apos_.z() // Don't interpolate Z — discrete map levels
    };
}

#endif // COOP_ENABLED
