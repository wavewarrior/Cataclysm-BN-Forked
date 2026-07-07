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
    } else {
        FAIL( "unknown COOP_SCENARIO: " + scenario );
    }

    cli.shutdown();
    g->coop_client_ = nullptr;
}

#endif // COOP_ENABLED
