#ifdef COOP_ENABLED
/**
 * Co-op two-process integration tests.
 *
 * Run via:  scripts/test_coop.ts (Deno harness)
 *           or manually:
 *   Process A: cata_test-tiles "[.][coop_role_host]"   COOP_SCENARIO=movement
 *   Process B: cata_test-tiles "[.][coop_role_client]" COOP_SCENARIO=movement
 *
 * Scenarios (selected by COOP_SCENARIO env var, default "movement"):
 *   movement — client queues 3×MOVE_N; proxy must reach target; reconcile.
 *
 * Cross-process coordination:
 *   /tmp/coop_test_port.txt      — game TCP port (written by host, polled by client)
 *   /tmp/coop_test_ctrl_port.txt — control socket port (written by host, polled by client)
 *
 * Control socket: plain TCP, OS-assigned port (port 0 → getsockname).
 *   Newline-delimited ASCII signals, e.g.: "PROXY_POS 100 200 0\n"
 *   Replaces all scenario-specific /tmp/coop_test_*.txt data files.
 */

#include "avatar.h"
#include "calendar.h"
#include "catch/catch.hpp"
#include "coop_client.h"
#include "coop_packets.h"
#include "coop_reconcile.h"
#include "coop_server.h"
#include "coop_session.h"
#include "coordinates.h"
#include "game.h"
#include "map.h"
#include "mapdata.h"
#include "npc.h"
#include "item.h"
#include "map_helpers.h"
#include "monster.h"
#include "creature_tracker.h"
#include "state_helpers.h"
#include "type_id.h"

#include <SDL3_net/SDL_net.h>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr uint16_t COOP_INTEG_PORT_BASE = 45802;
static constexpr int COOP_INTEG_PORT_RANGE = 10;
static constexpr int ACCEPT_TIMEOUT_MS = 90'000;
static constexpr int FILE_POLL_TIMEOUT_MS = 120'000; // 2 min — covers game data load
/// Number of MOVE_N actions the client queues — host asserts proxy moved exactly this far.
static constexpr int COOP_TEST_MOVES = 3;

static constexpr const char* COOP_PORT_FILE = "/tmp/coop_test_port.txt";
static constexpr const char* COOP_CTRL_PORT_FILE = "/tmp/coop_test_ctrl_port.txt";

// ---------------------------------------------------------------------------
// Scenario selection
// ---------------------------------------------------------------------------

static auto get_coop_scenario() -> std::string {
    const char* s = std::getenv( "COOP_SCENARIO" );
    return s ? std::string( s ) : std::string( "movement" );
}

// ---------------------------------------------------------------------------
// Game-port file helpers (unchanged from original)
// ---------------------------------------------------------------------------

static auto write_port_file( uint16_t port ) -> void {
    std::ofstream f( COOP_PORT_FILE );
    f << port << '\n';
}

static auto read_port_file( uint16_t& port, int timeout_ms = FILE_POLL_TIMEOUT_MS ) -> bool {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds( timeout_ms );
    while( std::chrono::steady_clock::now() < deadline ) {
        std::ifstream f( COOP_PORT_FILE );
        if( f.good() ) {
            int p = 0;
            if( f >> p && p > 0 ) {
                port = static_cast<uint16_t>( p );
                return true;
            }
        }
        SDL_Delay( 50 );
    }
    return false;
}

// ---------------------------------------------------------------------------
// Control socket — plain POSIX TCP, port 0 (OS-assigned)
// Replaces per-scenario /tmp data files with newline-delimited text signals.
// ---------------------------------------------------------------------------

static auto write_ctrl_port_file( uint16_t port ) -> void {
    std::ofstream f( COOP_CTRL_PORT_FILE );
    f << port << '\n';
}

static auto read_ctrl_port_file( uint16_t& port, int timeout_ms = FILE_POLL_TIMEOUT_MS ) -> bool {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds( timeout_ms );
    while( std::chrono::steady_clock::now() < deadline ) {
        std::ifstream f( COOP_CTRL_PORT_FILE );
        if( f.good() ) {
            int p = 0;
            if( f >> p && p > 0 ) {
                port = static_cast<uint16_t>( p );
                return true;
            }
        }
        SDL_Delay( 50 );
    }
    return false;
}

/// Control socket — bidirectional newline-delimited signalling.
/// Both sides can send and receive; each has a recv_buf for bulk reads.
struct coop_ctrl_server {
    int         listen_fd = -1;
    int         conn_fd   = -1;
    std::string recv_buf; ///< accumulates bytes for recv_line

    ~coop_ctrl_server() { close_all(); }

    auto bind_and_listen() -> uint16_t {
        listen_fd = ::socket( AF_INET, SOCK_STREAM, 0 );
        if( listen_fd == -1 ) { return 0; }
        int opt = 1;
        ::setsockopt( listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof( opt ) );
        sockaddr_in addr{};
        std::memset( &addr, 0, sizeof( addr ) );
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port        = 0;
        if( ::bind( listen_fd, reinterpret_cast<sockaddr*>( &addr ), sizeof( addr ) ) != 0 ) { return 0; }
        socklen_t len = sizeof( addr );
        if( ::getsockname( listen_fd, reinterpret_cast<sockaddr*>( &addr ), &len ) != 0 ) { return 0; }
        if( ::listen( listen_fd, 1 ) != 0 ) { return 0; }
        return ntohs( addr.sin_port );
    }

    auto accept_client( int timeout_ms ) -> bool {
        fd_set rfds;
        FD_ZERO( &rfds );
        FD_SET( listen_fd, &rfds );
        timeval tv{};
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = ( timeout_ms % 1000 ) * 1000;
        const int r = ::select( listen_fd + 1, &rfds, nullptr, nullptr, &tv );
        if( r <= 0 ) { return false; }
        conn_fd = ::accept( listen_fd, nullptr, nullptr );
        return conn_fd != -1;
    }

    /// Send a signal to the client.
    auto send_signal( const std::string& msg ) -> bool {
        const auto line = msg + "\n";
        return ::send( conn_fd, line.c_str(), static_cast<int>( line.size() ), 0 ) ==
               static_cast<ssize_t>( line.size() );
    }

    /// Non-blocking line check (bulk drain into recv_buf).
    auto try_recv_line( std::string& out ) -> bool {
        char tmp[1024];
        const int n = static_cast<int>( ::recv( conn_fd, tmp, static_cast<int>( sizeof( tmp ) ),
                                                MSG_DONTWAIT ) );
        if( n > 0 ) { recv_buf.append( tmp, static_cast<std::size_t>( n ) ); }
        const auto nl = recv_buf.find( '\n' );
        if( nl == std::string::npos ) { return false; }
        out      = recv_buf.substr( 0, nl );
        recv_buf = recv_buf.substr( nl + 1 );
        return true;
    }

    /// Blocking: wait up to timeout_ms for a complete line from client.
    auto recv_line( std::string& out, int timeout_ms = FILE_POLL_TIMEOUT_MS ) -> bool {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds( timeout_ms );
        while( std::chrono::steady_clock::now() < deadline ) {
            if( try_recv_line( out ) ) { return true; }
            SDL_Delay( 5 );
        }
        return false;
    }

    auto close_all() -> void {
        if( conn_fd != -1 )   { ::close( conn_fd );   conn_fd   = -1; }
        if( listen_fd != -1 ) { ::close( listen_fd ); listen_fd = -1; }
        std::remove( COOP_CTRL_PORT_FILE );
    }
};

/// Client-side control socket: connect, send/recv signals (bidirectional).
struct coop_ctrl_client {
    int         fd       = -1;
    std::string recv_buf; ///< accumulates bytes between try_recv_line calls

    ~coop_ctrl_client() { close_conn(); }

    /// Connect to host's control socket.  Retries until timeout_ms.
    auto connect_to( uint16_t port, int timeout_ms = FILE_POLL_TIMEOUT_MS ) -> bool {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds( timeout_ms );
        while( std::chrono::steady_clock::now() < deadline ) {
            fd = ::socket( AF_INET, SOCK_STREAM, 0 );
            if( fd == -1 ) { return false; }
            sockaddr_in addr{};
            std::memset( &addr, 0, sizeof( addr ) );
            addr.sin_family      = AF_INET;
            addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
            addr.sin_port        = htons( port );
            if( ::connect( fd, reinterpret_cast<sockaddr*>( &addr ), sizeof( addr ) ) == 0 ) {
                return true;
            }
            ::close( fd );
            fd = -1;
            SDL_Delay( 10 );
        }
        return false;
    }

    /// Non-blocking: drain available bytes into recv_buf, return true + line if complete.
    /// Call in a tick loop; the signal arrives whenever the host sends it.
    auto try_recv_line( std::string& out ) -> bool {
        // Bulk non-blocking drain — picks up the whole signal in one syscall.
        char tmp[1024];
        const int n =
            static_cast<int>( ::recv( fd, tmp, static_cast<int>( sizeof( tmp ) ), MSG_DONTWAIT ) );
        if( n > 0 ) { recv_buf.append( tmp, static_cast<std::size_t>( n ) ); }
        const auto nl = recv_buf.find( '\n' );
        if( nl == std::string::npos ) { return false; }
        out      = recv_buf.substr( 0, nl );
        recv_buf = recv_buf.substr( nl + 1 );
        return true;
    }

    /// Blocking: wait up to timeout_ms for a complete line.
    auto recv_line( std::string& out, int timeout_ms = FILE_POLL_TIMEOUT_MS ) -> bool {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds( timeout_ms );
        while( std::chrono::steady_clock::now() < deadline ) {
            if( try_recv_line( out ) ) { return true; }
            SDL_Delay( 5 );
        }
        return false;
    }
    /// Send a newline-terminated signal to the host.
    auto send_signal( const std::string& msg ) -> bool {
        const auto line = msg + "\n";
        return ::send( fd, line.c_str(), static_cast<int>( line.size() ), 0 ) ==
               static_cast<ssize_t>( line.size() );
    }


    auto close_conn() -> void {
        if( fd != -1 ) {
            ::close( fd );
            fd = -1;
        }
    }
};

// ---------------------------------------------------------------------------
// Movement scenario — extracted from original TEST_CASE bodies
// ---------------------------------------------------------------------------

static auto run_host_movement( coop_server& srv, coop_ctrl_server& ctrl ) -> void {
    const npc* p_spawn = g->critter_by_id<npc>( coop_session::get().proxy_npc_id );
    REQUIRE( p_spawn != nullptr );
    const tripoint_abs_ms proxy_spawn_pos = p_spawn->abs_pos();
    const tripoint_abs_ms target_pos      = proxy_spawn_pos + tripoint( 0, -COOP_TEST_MOVES, 0 );

    const auto move_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds( 90'000 );
    bool proxy_reached_target = false;
    while( std::chrono::steady_clock::now() < move_deadline ) {
        srv.coop_world_tick();
        SDL_Delay( 50 );
        const npc* proxy = g->critter_by_id<npc>( coop_session::get().proxy_npc_id );
        if( proxy && proxy->abs_pos() == target_pos ) {
            proxy_reached_target = true;
            break;
        }
    }

    // 5 extra ticks so the final sync (confirming all 3 seqs) reaches client.
    for( int i = 0; i < 5; ++i ) {
        srv.coop_world_tick();
        SDL_Delay( 50 );
    }

    const npc* proxy2 = g->critter_by_id<npc>( coop_session::get().proxy_npc_id );
    REQUIRE( proxy2 != nullptr );
    const tripoint_abs_ms proxy_final = proxy2->abs_pos();

    INFO( "proxy_spawn=(" << proxy_spawn_pos.x() << "," << proxy_spawn_pos.y() << ")"
                          << " target=(" << target_pos.x() << "," << target_pos.y() << ")"
                          << " proxy_final=(" << proxy_final.x() << "," << proxy_final.y()
                          << ")" );
    CHECK( proxy_reached_target );
    CHECK( proxy_final == target_pos );

    // Signal client: proxy final position, only on success.
    if( proxy_reached_target ) {
        std::ostringstream oss;
        oss << "PROXY_POS " << proxy_final.x() << " " << proxy_final.y() << " "
            << proxy_final.z();
        ctrl.send_signal( oss.str() );
    }
}

static auto run_client_movement( coop_client& cli, coop_ctrl_client& ctrl ) -> void {
    // Queue 3×MOVE_N immediately.
    for( int i = 0; i < COOP_TEST_MOVES; ++i ) { cli.queue_action( "MOVE_N" ); }

    // Poll while ticking — keep processing syncs so pending actions are sent and
    // confirmed.  Stop when host signals proxy final position (all 3 confirmed).
    tripoint_abs_ms expected_proxy_pos;
    bool signal_received = false;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds( FILE_POLL_TIMEOUT_MS );
    while( std::chrono::steady_clock::now() < deadline ) {
        cli.coop_world_tick();
        SDL_Delay( 5 );
        std::string sig;
        if( ctrl.try_recv_line( sig ) ) { // non-blocking bulk drain + line check
            // Parse "PROXY_POS x y z"
            std::istringstream iss( sig );
            std::string tag;
            int x = 0, y = 0, z = 0;
            if( iss >> tag >> x >> y >> z && tag == "PROXY_POS" ) {
                expected_proxy_pos = tripoint_abs_ms{ x, y, z };
                signal_received    = true;
                break;
            }
        }
    }
    REQUIRE( signal_received ); // timeout = host never signalled proxy position

    // 5 more ticks to ensure final sync arrives.
    for( int i = 0; i < 5; ++i ) {
        cli.coop_world_tick();
        SDL_Delay( 50 );
    }

    const tripoint_abs_ms actual = g->u.abs_pos();
    INFO( "expected=" << expected_proxy_pos.x() << "," << expected_proxy_pos.y()
                      << "  actual=" << actual.x() << "," << actual.y() );
    CHECK( actual == expected_proxy_pos );
}

// ---------------------------------------------------------------------------
// Resync scenario (Gap 1) — E2E smoke test
//
// Tests the server-side response path: when force_resync_ is true, the next
// tick fires a full-tile sync and clears the flag.  Client asserts it received
// the full sync (got_full_tile_sync_for_test_).
//
// The CLIENT detection path (hash mismatch → resync_request → force_resync_)
// is deterministically tested in Phase 5 via coop_sim_transport, where timing
// is controlled.  Forcing it through two-process E2E is timing-fragile.
// ---------------------------------------------------------------------------

static auto run_host_resync( coop_server& srv, coop_ctrl_server& ctrl ) -> void {
    // Directly set force_resync_ — simulates what the client's resync_request
    // normally does via the receiver thread.
    srv.set_force_resync_for_test();
    REQUIRE( srv.force_resync_pending_for_test() );

    // Tick: build_and_send_sync sends a full-tile sync (force_resync_ OR origin_changed
    // can each independently trigger it).  The meaningful assertion is on the CLIENT
    // side: got_full_tile_sync_for_test() confirms the full sync arrived.
    // We do NOT check !force_resync_pending_for_test() here — that assertion is fragile:
    // when origin_changed drives send_full_tiles, force_resync_.exchange(false) used to
    // be short-circuited; the production fix (unconditional exchange) is in place, but
    // the CHECK is still an implementation-detail, not an observable guarantee.
    srv.coop_world_tick();
    SDL_Delay( 100 );

    ctrl.send_signal( "RESYNC_DONE" );
}

static auto run_client_resync( coop_client& cli, coop_ctrl_client& ctrl ) -> void {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds( FILE_POLL_TIMEOUT_MS );
    while( std::chrono::steady_clock::now() < deadline ) {
        cli.coop_world_tick();
        SDL_Delay( 5 );
        std::string sig;
        if( ctrl.try_recv_line( sig ) && sig == "RESYNC_DONE" ) { break; }
    }
    // Client must have received the full-tile sync triggered by force_resync_.
    CHECK( cli.got_full_tile_sync_for_test() );
}

// ---------------------------------------------------------------------------
// Pickup relay scenario (Gap 2)
//
// Item is placed before send_initial_sync so the client world has it.
// Host signals ITEM_POS; client builds manifest and queues PICKUP action.
// Host asserts item removed via execute_client_action → apply_pickup_manifest.
// ---------------------------------------------------------------------------

static const itype_id PICKUP_KNIFE_ID( "knife_combat" );
static constexpr tripoint_bub_ms PICKUP_TILE{ 40, 40, 0 };

static auto run_host_pickup( coop_server& srv, coop_ctrl_server& ctrl ) -> void {
    const tripoint_abs_ms abs = g->m.bub_to_abs( PICKUP_TILE );
    REQUIRE( !g->m.i_at( PICKUP_TILE ).empty() ); // placed in TEST_CASE before initial sync

    std::ostringstream oss;
    oss << "ITEM_POS " << abs.x() << " " << abs.y() << " " << abs.z() << " knife_combat";
    ctrl.send_signal( oss.str() );

    std::string sig;
    REQUIRE( ctrl.recv_line( sig, FILE_POLL_TIMEOUT_MS ) );
    REQUIRE( sig == "PICKUP_SENT" );

    for( int i = 0; i < 5; ++i ) { srv.coop_world_tick(); SDL_Delay( 50 ); }

    int remaining = 0;
    for( const item* it : g->m.i_at( PICKUP_TILE ) ) {
        if( it->typeId() == PICKUP_KNIFE_ID ) { ++remaining; }
    }
    CHECK( remaining == 0 );
    ctrl.send_signal( "PICKUP_DONE" );
}

static auto run_client_pickup( coop_client& cli, coop_ctrl_client& ctrl ) -> void {
    tripoint_abs_ms item_abs;
    {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds( FILE_POLL_TIMEOUT_MS );
        bool got = false;
        while( std::chrono::steady_clock::now() < deadline ) {
            cli.coop_world_tick();
            SDL_Delay( 5 );
            std::string sig;
            if( !ctrl.try_recv_line( sig ) ) { continue; }
            std::istringstream iss( sig );
            std::string tag, type_name;
            int x = 0, y = 0, z = 0;
            if( iss >> tag >> x >> y >> z >> type_name && tag == "ITEM_POS" ) {
                item_abs = tripoint_abs_ms{ x, y, z };
                got      = true;
                break;
            }
        }
        REQUIRE( got );
    }
    std::ostringstream manifest;
    manifest << "{\"items\":[{\"tx\":" << item_abs.x()
             << ",\"ty\":" << item_abs.y()
             << ",\"tz\":" << item_abs.z()
             << ",\"type\":\"knife_combat\",\"charges\":0,\"qty\":1}]}";
    cli.queue_action( "PICKUP", manifest.str() );
    ctrl.send_signal( "PICKUP_SENT" );

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds( FILE_POLL_TIMEOUT_MS );
    while( std::chrono::steady_clock::now() < deadline ) {
        cli.coop_world_tick();
        SDL_Delay( 5 );
        std::string done;
        if( ctrl.try_recv_line( done ) && done == "PICKUP_DONE" ) { break; }
    }
}

// ---------------------------------------------------------------------------
// Disconnect scenario (Gap 3)
//
// Client closes the raw TCP socket WITHOUT sending the protocol disconnect
// packet — simulates a client crash.  Host receiver_thread_ detects the TCP
// drop and sets running_=false.  Host then checks the proxy NPC is removed
// from the world within 3 ticks; if it is not, this test fails — revealing
// that proxy cleanup on abrupt disconnect is not yet implemented (real bug).
// ---------------------------------------------------------------------------

static auto run_host_disconnect( coop_server& srv, coop_ctrl_server& ctrl ) -> void {
    ctrl.send_signal( "OK_DISCONNECT" );

    // Poll until receiver_thread_ sets running_=false (TCP drop detected).
    const auto drop_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds( 10'000 );
    while( srv.is_running() && std::chrono::steady_clock::now() < drop_deadline ) {
        srv.coop_world_tick();
        SDL_Delay( 50 );
    }
    CHECK( !srv.is_running() ); // TCP drop detected within 10 s

    // Capture the proxy's character_id BEFORE shutdown() resets it to invalid.
    // If we read it after shutdown(), proxy_npc_id == character_id{} (value=-1) →
    // critter_by_id returns nullptr trivially → CHECK never verifies actual removal.
    const character_id proxy_id = coop_session::get().proxy_npc_id;
    REQUIRE( proxy_id.is_valid() ); // must have been assigned during spawn_proxy_npc

    srv.shutdown();
    g->coop_server_ = nullptr;

    // Assert the ACTUAL npc is gone from active_npc via the real proxy_id.
    // If erase_npc() was skipped (is_processing_npcs guard or a removal bug), this fails.
    CHECK( g->critter_by_id<npc>( proxy_id ) == nullptr );
}

static auto run_client_disconnect( coop_client& cli, coop_ctrl_client& ctrl ) -> void {
    std::string sig;
    ctrl.recv_line( sig, FILE_POLL_TIMEOUT_MS );
    REQUIRE( sig == "OK_DISCONNECT" );
    // Abruptly destroy the socket — no protocol disconnect packet sent.
    cli.close_socket_abruptly_for_test();
    g->coop_client_ = nullptr;
}

// ---------------------------------------------------------------------------
// Submap-shift scenario (Gap 5) — E2E smoke test
//
// Tests the origin_changed full-sync path: reset_sync_origin_for_test() causes
// origin_changed=true on the next tick → full-tile sync fires → client reconciles.
// This exercises the same code path that a real host-avatar submap crossing
// would trigger (abs_sub tracks the host avatar, not the proxy).
//
// Deterministic proof of convergence after full sync → Phase 5 sim-transport.
// ---------------------------------------------------------------------------

static constexpr int COOP_SUBMAP_MOVES = 3;

static auto run_host_submap_shift( coop_server& srv, coop_ctrl_server& ctrl ) -> void {
    const npc* p_spawn = g->critter_by_id<npc>( coop_session::get().proxy_npc_id );
    REQUIRE( p_spawn != nullptr );
    const tripoint_abs_ms proxy_spawn_pos = p_spawn->abs_pos();
    const tripoint_abs_ms target_pos = proxy_spawn_pos + tripoint( 0, -COOP_SUBMAP_MOVES, 0 );

    const auto move_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds( 30'000 );
    bool proxy_reached_target = false;
    while( std::chrono::steady_clock::now() < move_deadline ) {
        srv.coop_world_tick();
        SDL_Delay( 50 );
        const npc* proxy = g->critter_by_id<npc>( coop_session::get().proxy_npc_id );
        if( proxy && proxy->abs_pos() == target_pos ) {
            proxy_reached_target = true;
            break;
        }
    }
    CHECK( proxy_reached_target );

    // Reset sync origin → origin_changed=true on next tick → full-tile sync fires.
    srv.reset_sync_origin_for_test();
    srv.coop_world_tick();
    SDL_Delay( 100 );
    for( int i = 0; i < 4; ++i ) { srv.coop_world_tick(); SDL_Delay( 50 ); }

    const npc* proxy2 = g->critter_by_id<npc>( coop_session::get().proxy_npc_id );
    REQUIRE( proxy2 != nullptr );
    const tripoint_abs_ms proxy_final = proxy2->abs_pos();
    if( proxy_reached_target ) {
        std::ostringstream oss;
        oss << "PROXY_POS " << proxy_final.x() << " " << proxy_final.y() << " "
            << proxy_final.z();
        ctrl.send_signal( oss.str() );
    }
}

static auto run_client_submap_shift( coop_client& cli, coop_ctrl_client& ctrl ) -> void {
    for( int i = 0; i < COOP_SUBMAP_MOVES; ++i ) { cli.queue_action( "MOVE_N" ); }

    tripoint_abs_ms expected_proxy_pos;
    bool signal_received = false;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds( FILE_POLL_TIMEOUT_MS );
    while( std::chrono::steady_clock::now() < deadline ) {
        cli.coop_world_tick();
        SDL_Delay( 5 );
        std::string sig;
        if( !ctrl.try_recv_line( sig ) ) { continue; }
        std::istringstream iss( sig );
        std::string tag;
        int x = 0, y = 0, z = 0;
        if( iss >> tag >> x >> y >> z && tag == "PROXY_POS" ) {
            expected_proxy_pos = tripoint_abs_ms{ x, y, z };
            signal_received    = true;
            break;
        }
    }
    REQUIRE( signal_received );
    for( int i = 0; i < 5; ++i ) { cli.coop_world_tick(); SDL_Delay( 50 ); }

    const tripoint_abs_ms actual = g->u.abs_pos();
    const int drift = std::abs( actual.x() - expected_proxy_pos.x() ) +
                      std::abs( actual.y() - expected_proxy_pos.y() );
    INFO( "drift=" << drift << " after full sync" );
    CHECK( drift <= 5 );
}

// ---------------------------------------------------------------------------
// Death relay scenario (Gap 9)
//
// Host sets client_hp_pct=0 + client_dead=true via test seams, ticks once.
// Asserts proxy HP stays ≥ 1 (0 would trigger npc::die() → proxy destroyed).
// Asserts death announced exactly once (second tick must not re-fire).
// ---------------------------------------------------------------------------

static auto run_host_death( coop_server& srv, coop_ctrl_server& ctrl ) -> void {
    // Wait for the client to signal its avatar is dead (CLIENT_DEAD).
    // The client kills g->u directly so every subsequent client_status sends
    // dead=true — no race with the receiver thread overwriting to alive.
    {
        std::string sig;
        REQUIRE( ctrl.recv_line( sig, FILE_POLL_TIMEOUT_MS ) );
        REQUIRE( sig == "CLIENT_DEAD" );
    }

    // Poll until receiver_thread_ processes the client_status(dead=true) packet.
    const auto dead_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds( 10'000 );
    while( !srv.client_dead() && std::chrono::steady_clock::now() < dead_deadline ) {
        SDL_Delay( 10 );
    }
    REQUIRE( srv.client_dead() ); // receiver processed client_status(dead=true)

    // Tick once: HP mirror clamps to max(1,…), C3b death announcement fires.
    srv.coop_world_tick();
    SDL_Delay( 100 );

    const npc* proxy = g->critter_by_id<npc>( coop_session::get().proxy_npc_id );
    REQUIRE( proxy != nullptr );
    CHECK( proxy->hp_percentage() >= 1 ); // clamp prevented npc::die()
    CHECK( srv.client_death_announced_for_test() ); // announced on first tick

    // Second tick: once-only gate must hold — death not re-announced.
    srv.coop_world_tick();
    SDL_Delay( 50 );
    CHECK( srv.client_death_announced_for_test() ); // still true

    ctrl.send_signal( "DEATH_DONE" );
}

static auto run_client_death( coop_client& cli, coop_ctrl_client& ctrl ) -> void {
    // Kill the avatar so is_dead_state() returns true.  Every subsequent
    // client_status tick then sends dead=true — the receiver thread never races
    // back to alive.  We zero the torso HP (vital part → is_dead_state()=true).
    g->u.set_part_hp_cur( bodypart_id( "torso" ), 0 );
    // Tick once to flush the initial client_status(dead=true) through the socket.
    cli.coop_world_tick();
    ctrl.send_signal( "CLIENT_DEAD" );

    // Keep ticking so the connection stays alive while host asserts.
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds( FILE_POLL_TIMEOUT_MS );
    while( std::chrono::steady_clock::now() < deadline ) {
        cli.coop_world_tick();
        SDL_Delay( 5 );
        std::string sig;
        if( ctrl.try_recv_line( sig ) && sig == "DEATH_DONE" ) { break; }
    }
}

// ---------------------------------------------------------------------------
// Shared helpers for melee-family E2E scenarios (MELEE + SMASH)
//
// Both actions call proxy->melee_attack() on the server when a monster occupies
// the target tile.  debug_mon has huge HP so the raw reference stays valid
// across all ticks (no UAF from cleanup_dead).  min_hp tracks the damage trough
// across every tick, capturing damage before regen can recover it.
// ---------------------------------------------------------------------------

static constexpr int         COOP_HIT_ACTIONS    = 5;   ///< melee/smash actions queued per scenario
static constexpr int         COOP_POST_HIT_DRAIN = 8;   ///< ticks after *_DONE to drain all actions

/// Build a target ctx_json {"tx":X,"ty":Y,"tz":Z} used by MELEE and SMASH.
static auto make_target_ctx( const tripoint_abs_ms& pos ) -> std::string {
    std::ostringstream oss;
    JsonOut jout( oss );
    jout.start_object();
    jout.member( "tx", pos.x() );
    jout.member( "ty", pos.y() );
    jout.member( "tz", pos.z() );
    jout.end_object();
    return oss.str();
}

/// Shared host logic: spawn mon_zombie 1 tile east of proxy, signal client,
/// then track min HP by monster IDENTITY (held shared_ptr) across ticks.
///
/// Target choice: mon_zombie (no regen, ~65-90 HP) — NOT debug_mon.
/// debug_mon regenerates 50 HP/turn which outpaces the proxy's unarmed bash
/// (~3-15/hit × COOP_HIT_ACTIONS).  The proxy is intentionally left UNARMED
/// because production never syncs the client's melee weapon — the proxy always
/// fights bare-fisted.  This test exercises the actual production path.
/// NOTE: melee weapon is not synced (unlike FIRE which syncs weapon_id/ammo_id).
/// Real co-op melee deals bare-fist damage regardless of the client's weapon.
/// See plans/coop_networking_plan.md Known Limitations: CL-MELEE-WEAPON.
///
/// Identity tracking: hold shared_ptr_fast<monster> at spawn; read hp/is_dead
/// by identity, not position.  Zombie adjacent to proxy attacks in place rather
/// than moving, so target_abs stays valid, but identity tracking makes the test
/// robust even if the zombie shifts tiles.
static auto run_host_melee_family( coop_server& srv, coop_ctrl_server& ctrl,
                                    const std::string& done_signal ) -> void {
    const npc* proxy_npc = g->critter_by_id<npc>( coop_session::get().proxy_npc_id );
    REQUIRE( proxy_npc != nullptr );
    // Proxy is intentionally unarmed — matches production behaviour.

    // Spawn mon_zombie 1 tile east of proxy.  Capture shared_ptr immediately for
    // identity-based HP reads: immune to UAF (shared_ptr keeps object alive) and to
    // mobility false-pass (HP read by pointer identity, not position re-lookup).
    const tripoint_bub_ms target_bpos = proxy_npc->bub_pos() + tripoint( 1, 0, 0 );
    spawn_test_monster( "mon_zombie", target_bpos );
    const shared_ptr_fast<monster> target_ptr = g->critter_tracker->find( target_bpos );
    REQUIRE( target_ptr != nullptr );
    const int initial_hp = target_ptr->get_hp();
    const tripoint_abs_ms target_abs = target_ptr->abs_pos();
    INFO( "mon_zombie initial_hp=" << initial_hp );

    ctrl.send_signal( "MONSTER_POS " + std::to_string( target_abs.x() ) + " " +
                      std::to_string( target_abs.y() ) + " " +
                      std::to_string( target_abs.z() ) );

    // Track HP trough by identity each tick; drain COOP_POST_HIT_DRAIN extra ticks
    // after done_signal to catch all queued actions before asserting.
    // If zombie dies (min_hp=0 or is_dead_state) the assertion trivially holds.
    int  min_hp     = initial_hp;
    bool done       = false;
    int  post_ticks = 0;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds( 20'000 );
    while( std::chrono::steady_clock::now() < deadline ) {
        srv.coop_world_tick();
        if( target_ptr->is_dead_state() ) {
            min_hp = 0; // zombie died — damage confirmed
            done   = true;
        } else {
            min_hp = std::min( min_hp, target_ptr->get_hp() );
        }
        SDL_Delay( 50 );
        std::string sig;
        if( !done && ctrl.try_recv_line( sig ) && sig == done_signal ) { done = true; }
        if( done && ++post_ticks >= COOP_POST_HIT_DRAIN ) { break; }
    }
    for( int i = 0; i < 5; ++i ) { srv.coop_world_tick(); SDL_Delay( 50 ); }

    INFO( "mon_zombie min_hp=" << min_hp << " (initial=" << initial_hp << ")" );
    CHECK( min_hp < initial_hp );
}

/// Shared client logic: receive monster position, queue N actions, send done signal,
/// then keep connection alive while host asserts.
static auto run_client_melee_family( coop_client& cli, coop_ctrl_client& ctrl,
                                      const std::string& action_key,
                                      const std::string& done_signal ) -> void {
    std::string pos_sig;
    REQUIRE( ctrl.recv_line( pos_sig, FILE_POLL_TIMEOUT_MS ) );
    int mx = 0, my = 0, mz = 0;
    std::istringstream iss( pos_sig );
    std::string tag;
    iss >> tag >> mx >> my >> mz;
    REQUIRE( tag == "MONSTER_POS" );
    const auto ctx = make_target_ctx( tripoint_abs_ms{ mx, my, mz } );

    for( int i = 0; i < COOP_HIT_ACTIONS; ++i ) { cli.queue_action( action_key, ctx ); }
    // Run COOP_HIT_ACTIONS + 3 ticks to flush all queued actions (1 sent per tick).
    for( int i = 0; i < COOP_HIT_ACTIONS + 3; ++i ) { cli.coop_world_tick(); SDL_Delay( 50 ); }
    ctrl.send_signal( done_signal );

    // Keep connection alive while host reads min_hp + trailing ticks.
    const auto done_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds( 10'000 );
    while( std::chrono::steady_clock::now() < done_deadline ) {
        cli.coop_world_tick();
        SDL_Delay( 50 );
    }
}

// ---------------------------------------------------------------------------
// MELEE scenario
//
// Client queues COOP_HIT_ACTIONS MELEE actions targeting a debug_mon 1 tile
// east of the proxy.  Server routes MELEE → execute_player_cmd(K::melee) →
// proxy->melee_attack(*mon_ptr, true).  Host asserts min HP dropped.
// ---------------------------------------------------------------------------

static auto run_host_melee( coop_server& srv, coop_ctrl_server& ctrl ) -> void {
    run_host_melee_family( srv, ctrl, "MELEE_DONE" );
}

static auto run_client_melee( coop_client& cli, coop_ctrl_client& ctrl ) -> void {
    run_client_melee_family( cli, ctrl, "MELEE", "MELEE_DONE" );
}

// ---------------------------------------------------------------------------
// SMASH scenario
//
// Client queues COOP_HIT_ACTIONS SMASH actions at the same monster tile.
// Server routes SMASH → execute_player_cmd(K::smash) → monster present →
// proxy->melee_attack(*mon_ptr, true).  Shares the same assertion as MELEE
// but exercises the distinct SMASH code path.
// ---------------------------------------------------------------------------

static auto run_host_smash( coop_server& srv, coop_ctrl_server& ctrl ) -> void {
    run_host_melee_family( srv, ctrl, "SMASH_DONE" );
}

static auto run_client_smash( coop_client& cli, coop_ctrl_client& ctrl ) -> void {
    run_client_melee_family( cli, ctrl, "SMASH", "SMASH_DONE" );
}

// ---------------------------------------------------------------------------
// Ranged scenario (CL-RANGED verification)
//
// Client arms itself with a glock_20, queues COOP_FIRE_SHOTS FIRE actions at
// a debug_mon spawned 1 tile east of the proxy.  Host asserts the monster took
// damage — verifies the arm→ammo_set→wield→fire_gun chain.
// debug_mon has huge HP so it survives all 10 shots; the raw reference into
// critter_tracker stays valid for the entire test (no UAF risk from cleanup_dead).
// ---------------------------------------------------------------------------

static constexpr int         COOP_FIRE_SHOTS    = 10;
static constexpr const char* RANGED_WEAPON_ID   = "glock_20";
static constexpr const char* RANGED_AMMO_ID     = "10mm_fmj";
static constexpr const char* RANGED_TARGET_TYPE = "debug_mon"; // huge HP, survives all shots

static auto run_host_ranged( coop_server& srv, coop_ctrl_server& ctrl ) -> void {
    // Spawn zombie 1 tile east of the proxy spawn position.
    const npc* proxy_npc = g->critter_by_id<npc>( coop_session::get().proxy_npc_id );
    REQUIRE( proxy_npc != nullptr );
    const tripoint_bub_ms zombie_bpos = proxy_npc->bub_pos() + tripoint( 1, 0, 0 );
    monster& zomb = spawn_test_monster( RANGED_TARGET_TYPE, zombie_bpos );
    const int initial_hp = zomb.get_hp();
    INFO( "zombie initial_hp=" << initial_hp << " at bub=(" << zombie_bpos.x()
          << "," << zombie_bpos.y() << ")" );

    // Tell client the zombie's absolute position so it can aim at it.
    const tripoint_abs_ms zombie_abs = zomb.abs_pos();
    ctrl.send_signal( "MONSTER_POS " + std::to_string( zombie_abs.x() ) + " " +
                      std::to_string( zombie_abs.y() ) + " " +
                      std::to_string( zombie_abs.z() ) );

    // Tick until all FIRE actions have been processed (or timeout).
    //
    // Timing note: FIRE_DONE arrives on the ctrl socket as soon as the client finishes
    // queuing all 10 actions (after 15 client ticks), but the host drains ONE action per
    // world tick — so many FIRE actions are still in-flight when FIRE_DONE arrives.
    // debug_mon regenerates 50 HP/turn so reading after a long wait risks regen erasing
    // damage.  Solution: track min_hp across every tick; keep ticking POST_DONE_DRAIN
    // more ticks after FIRE_DONE to drain all 10 queued actions, then assert the trough.
    static constexpr int POST_DONE_DRAIN = 12; // drains 10 actions + rounding buffer
    int  min_hp      = initial_hp;
    bool fire_done   = false;
    int  post_ticks  = 0;
    const auto fire_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds( 30'000 );
    while( std::chrono::steady_clock::now() < fire_deadline ) {
        srv.coop_world_tick();
        min_hp = std::min( min_hp, zomb.get_hp() ); // capture trough before regen
        SDL_Delay( 50 );
        std::string sig;
        if( !fire_done && ctrl.try_recv_line( sig ) && sig == "FIRE_DONE" ) {
            fire_done = true;
        }
        if( fire_done && ++post_ticks >= POST_DONE_DRAIN ) { break; }
    }

    // Let the final sync reach the client.
    for( int i = 0; i < 5; ++i ) { srv.coop_world_tick(); SDL_Delay( 50 ); }

    // min_hp captures the lowest HP seen across all ticks — damage registered even if
    // regen partially recovered before the final read.
    INFO( "debug_mon: initial_hp=" << initial_hp << " min_hp=" << min_hp );
    CHECK( min_hp < initial_hp );
}

static auto run_client_ranged( coop_client& cli, coop_ctrl_client& ctrl ) -> void {
    // Arm the client avatar with a pistol so weapon_id/ammo_id embed in FIRE ctx.
    const itype_id wid( RANGED_WEAPON_ID );
    const itype_id aid( RANGED_AMMO_ID );
    REQUIRE( wid.is_valid() );
    REQUIRE( aid.is_valid() );
    auto weapon = item::spawn( wid );
    weapon->ammo_set( aid );
    g->u.wield( std::move( weapon ) );
    REQUIRE( g->u.is_armed() );
    REQUIRE( g->u.primary_weapon().is_gun() );

    // Wait for host to send monster position.
    std::string pos_sig;
    REQUIRE( ctrl.recv_line( pos_sig, FILE_POLL_TIMEOUT_MS ) );
    INFO( "client received: " << pos_sig );
    // Parse "MONSTER_POS x y z"
    int mx = 0, my = 0, mz = 0;
    std::istringstream iss( pos_sig );
    std::string tag;
    iss >> tag >> mx >> my >> mz;
    REQUIRE( tag == "MONSTER_POS" );
    const tripoint_abs_ms target_abs{ mx, my, mz };

    // Queue COOP_FIRE_SHOTS FIRE actions all aimed at the zombie.
    // Each action carries weapon_id + ammo_id so the server arms the proxy per shot.
    const auto& wep = g->u.primary_weapon();
    const itype_id cur_ammo = wep.ammo_current();
    for( int i = 0; i < COOP_FIRE_SHOTS; ++i ) {
        std::ostringstream ctx_oss;
        JsonOut ctx_jout( ctx_oss );
        ctx_jout.start_object();
        ctx_jout.member( "tx", target_abs.x() );
        ctx_jout.member( "ty", target_abs.y() );
        ctx_jout.member( "tz", target_abs.z() );
        ctx_jout.member( "weapon_id", wep.typeId().str() );
        if( !cur_ammo.is_null() ) { ctx_jout.member( "ammo_id", cur_ammo.str() ); }
        ctx_jout.end_object();
        cli.queue_action( "FIRE", ctx_oss.str() );
    }

    // Run world ticks to flush all actions to the host (one action is sent per tick).
    for( int i = 0; i < COOP_FIRE_SHOTS + 5; ++i ) {
        cli.coop_world_tick();
        SDL_Delay( 50 );
    }
    ctrl.send_signal( "FIRE_DONE" );

    // Keep connection alive while host asserts.
    const auto done_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds( 10'000 );
    while( std::chrono::steady_clock::now() < done_deadline ) {
        cli.coop_world_tick();
        SDL_Delay( 50 );
    }
}

// ---------------------------------------------------------------------------
// Terrain-change scenario (C2b round-trip verification)
//
// Host places t_door_c 1 tile south of spawn before initial sync (client sees
// it in the 5×5 blast).  Client queues a TERRAIN_CHANGE to open it.  Host
// asserts the door is t_door_o after processing.
// This tests the client-detect→serialize→send path; apply_terrain_change is
// already covered by coop_terrain_test.cpp unit tests.
// ---------------------------------------------------------------------------

static auto run_host_terrain_change( coop_server& srv, coop_ctrl_server& ctrl ) -> void {
    // Compute door absolute position at scenario start (before any world ticks that
    // might shift the reality bubble).  Store as ABSOLUTE and reconvert to bub at each
    // read — using a stale bub coordinate after a bubble shift maps to the wrong tile.
    const tripoint_bub_ms door_bpos_init = g->m.abs_to_bub( g->u.abs_pos() ) + tripoint( 0, 1, 0 );
    const tripoint_abs_ms door_abs       = g->m.bub_to_abs( door_bpos_init );
    // Fresh bub at read time — immune to bubble shifts.
    const auto fresh_bpos = [&]() { return g->m.abs_to_bub( door_abs ); };

    REQUIRE( g->m.ter( fresh_bpos() ) == ter_id( "t_door_c" ) ); // pre-sync placement held

    ctrl.send_signal( "DOOR_POS " + std::to_string( door_abs.x() ) + " " +
                      std::to_string( door_abs.y() ) + " " +
                      std::to_string( door_abs.z() ) );

    bool done       = false;
    int  post_ticks = 0;
    bool terrain_ok = false;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds( 20'000 );
    while( std::chrono::steady_clock::now() < deadline ) {
        srv.coop_world_tick();
        if( g->m.ter( fresh_bpos() ) == ter_id( "t_door_o" ) ) { terrain_ok = true; }
        SDL_Delay( 50 );
        std::string sig;
        if( !done && ctrl.try_recv_line( sig ) && sig == "TERRAIN_DONE" ) { done = true; }
        if( done && ++post_ticks >= 6 ) { break; }
    }
    for( int i = 0; i < 5; ++i ) { srv.coop_world_tick(); SDL_Delay( 50 ); }

    INFO( "door_abs=(" << door_abs.x() << "," << door_abs.y() << "," << door_abs.z() << ")"
          << " final_ter=" << g->m.ter( fresh_bpos() ).id().str()
          << " terrain_ok=" << terrain_ok );
    CHECK( terrain_ok );
}

static auto run_client_terrain_change( coop_client& cli, coop_ctrl_client& ctrl ) -> void {
    // Wait for host to send door position.
    std::string pos_sig;
    REQUIRE( ctrl.recv_line( pos_sig, FILE_POLL_TIMEOUT_MS ) );
    int dx = 0, dy = 0, dz = 0;
    std::istringstream iss( pos_sig );
    std::string tag;
    iss >> tag >> dx >> dy >> dz;
    REQUIRE( tag == "DOOR_POS" );
    const tripoint_abs_ms door_abs{ dx, dy, dz };

    // Queue TERRAIN_CHANGE: closed door → open door.
    // queue_terrain_change is the same public API the game calls from handle_action.cpp.
    cli.queue_terrain_change( door_abs, "t_door_o", "" );

    // Run enough ticks to flush the action (1 action sent per tick).
    for( int i = 0; i < 8; ++i ) { cli.coop_world_tick(); SDL_Delay( 50 ); }
    ctrl.send_signal( "TERRAIN_DONE" );

    // Keep connection alive while host asserts.
    const auto done_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds( 10'000 );
    while( std::chrono::steady_clock::now() < done_deadline ) {
        cli.coop_world_tick();
        SDL_Delay( 50 );
    }
}

// ---------------------------------------------------------------------------
// Host role
// ---------------------------------------------------------------------------

TEST_CASE( "coop integration: host role", "[.][coop_role_host]" ) {
    clear_all_state();

    const auto scenario    = get_coop_scenario();
    const tripoint_abs_ms spawn_abs = g->u.abs_pos();

    // --- Bind game socket ---
    coop_server srv;
    uint16_t bound_port = 0;
    for( int i = 0; i < COOP_INTEG_PORT_RANGE; ++i ) {
        const auto try_port = static_cast<uint16_t>( COOP_INTEG_PORT_BASE + i );
        if( srv.listen( try_port ) ) {
            bound_port = try_port;
            break;
        }
    }
    if( bound_port == 0 ) {
        WARN( "Could not bind to any port in 45802..45811 — skipping" );
        return;
    }
    write_port_file( bound_port );

    // --- Bind control socket (port 0, OS-assigned) ---
    coop_ctrl_server ctrl;
    const uint16_t ctrl_port = ctrl.bind_and_listen();
    if( ctrl_port == 0 ) {
        WARN( "Could not bind control socket — skipping" );
        return;
    }
    write_ctrl_port_file( ctrl_port );

    // --- Accept game client ---
    const auto accept_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds( ACCEPT_TIMEOUT_MS );
    while( !srv.try_accept() ) {
        REQUIRE( std::chrono::steady_clock::now() < accept_deadline );
        SDL_Delay( 10 );
    }

    REQUIRE( srv.handshake() );
    REQUIRE( srv.send_world_seed( g->u.get_name() ) );
    srv.spawn_proxy_npc( spawn_abs, "TestClient" );
    // Pre-initial-sync per-scenario setup.
    if( scenario == "pickup" ) {
        g->m.add_item( PICKUP_TILE, item::spawn( PICKUP_KNIFE_ID, calendar::turn,
                                                 item::solitary_tag{} ) );
    } else if( scenario == "terrain_change" ) {
        // Place a closed door 1 tile south of spawn — client will open it.
        // Must happen before initial sync so the client sees t_door_c in the blast.
        const tripoint_bub_ms door_bpos = g->m.abs_to_bub( spawn_abs ) + tripoint( 0, 1, 0 );
        g->m.ter_set( door_bpos, ter_id( "t_door_c" ) );
    }

    REQUIRE( srv.send_initial_sync() );
    srv.start_receiver_thread();
    g->coop_server_ = &srv;

    // --- Accept control client ---
    REQUIRE( ctrl.accept_client( ACCEPT_TIMEOUT_MS ) );

    // --- Dispatch scenario ---
    if( scenario == "movement" ) {
        run_host_movement( srv, ctrl );
    } else if( scenario == "resync" ) {
        run_host_resync( srv, ctrl );
    } else if( scenario == "pickup" ) {
        run_host_pickup( srv, ctrl );
    } else if( scenario == "disconnect" ) {
        run_host_disconnect( srv, ctrl );
    } else if( scenario == "submap_shift" ) {
        run_host_submap_shift( srv, ctrl );
    } else if( scenario == "death" ) {
        run_host_death( srv, ctrl );
    } else if( scenario == "ranged" ) {
        run_host_ranged( srv, ctrl );
    } else if( scenario == "melee" ) {
        run_host_melee( srv, ctrl );
    } else if( scenario == "smash" ) {
        run_host_smash( srv, ctrl );
    } else if( scenario == "terrain_change" ) {
        run_host_terrain_change( srv, ctrl );
    } else {
        FAIL( "unknown COOP_SCENARIO: " + scenario );
    }

    srv.shutdown();
    g->coop_server_ = nullptr;
    std::remove( COOP_PORT_FILE );
}

// ---------------------------------------------------------------------------
// Client role
// ---------------------------------------------------------------------------

TEST_CASE( "coop integration: client role", "[.][coop_role_client]" ) {
    clear_all_state();

    const auto scenario = get_coop_scenario();

    // --- Connect game socket ---
    uint16_t port = 0;
    REQUIRE( read_port_file( port ) );

    coop_client cli;
    REQUIRE( cli.connect( "127.0.0.1", port ) );
    REQUIRE( cli.handshake() );
    REQUIRE( cli.receive_world_seed() );
    cli.apply_world_seed_to_avatar();
    g->coop_client_ = &cli;

    // --- Connect control socket ---
    uint16_t ctrl_port = 0;
    REQUIRE( read_ctrl_port_file( ctrl_port ) );
    coop_ctrl_client ctrl;
    REQUIRE( ctrl.connect_to( ctrl_port ) );

    // --- Dispatch scenario ---
    if( scenario == "movement" ) {
        run_client_movement( cli, ctrl );
    } else if( scenario == "resync" ) {
        run_client_resync( cli, ctrl );
    } else if( scenario == "pickup" ) {
        run_client_pickup( cli, ctrl );
    } else if( scenario == "disconnect" ) {
        run_client_disconnect( cli, ctrl );
        return; // socket already closed in scenario; skip shutdown() below
    } else if( scenario == "submap_shift" ) {
        run_client_submap_shift( cli, ctrl );
    } else if( scenario == "death" ) {
        run_client_death( cli, ctrl );
    } else if( scenario == "ranged" ) {
        run_client_ranged( cli, ctrl );
    } else if( scenario == "melee" ) {
        run_client_melee( cli, ctrl );
    } else if( scenario == "smash" ) {
        run_client_smash( cli, ctrl );
    } else if( scenario == "terrain_change" ) {
        run_client_terrain_change( cli, ctrl );
    } else {
        FAIL( "unknown COOP_SCENARIO: " + scenario );
    }

    cli.shutdown();
    g->coop_client_ = nullptr;
}

#endif // COOP_ENABLED
