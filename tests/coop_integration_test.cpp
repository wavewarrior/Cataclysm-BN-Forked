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

/// Host-side control socket: bind, accept one client, send/recv signals.
struct coop_ctrl_server {
    int listen_fd = -1;
    int conn_fd   = -1;

    ~coop_ctrl_server() { close_all(); }

    /// Bind on port 0 (OS-assigned).  Returns the assigned port, or 0 on error.
    auto bind_and_listen() -> uint16_t {
        listen_fd = ::socket( AF_INET, SOCK_STREAM, 0 );
        if( listen_fd == -1 ) { return 0; }
        int opt = 1;
        ::setsockopt( listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof( opt ) );
        sockaddr_in addr{};
        std::memset( &addr, 0, sizeof( addr ) );
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port        = 0; // OS picks
        if( ::bind( listen_fd, reinterpret_cast<sockaddr*>( &addr ), sizeof( addr ) ) != 0 ) {
            return 0;
        }
        socklen_t len = sizeof( addr );
        if( ::getsockname( listen_fd, reinterpret_cast<sockaddr*>( &addr ), &len ) != 0 ) {
            return 0;
        }
        if( ::listen( listen_fd, 1 ) != 0 ) { return 0; }
        return ntohs( addr.sin_port );
    }

    /// Block until a client connects or timeout_ms elapses.
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

    /// Send a newline-terminated signal to the client.
    auto send_signal( const std::string& msg ) -> bool {
        const auto line = msg + "\n";
        return ::send( conn_fd, line.c_str(), static_cast<int>( line.size() ), 0 ) ==
               static_cast<ssize_t>( line.size() );
    }

    auto close_all() -> void {
        if( conn_fd != -1 ) {
            ::close( conn_fd );
            conn_fd = -1;
        }
        if( listen_fd != -1 ) {
            ::close( listen_fd );
            listen_fd = -1;
        }
        std::remove( COOP_CTRL_PORT_FILE );
    }
};

/// Client-side control socket: connect, recv signals.
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
    REQUIRE( srv.send_initial_sync() );
    srv.start_receiver_thread();
    g->coop_server_ = &srv;

    // --- Accept control client ---
    REQUIRE( ctrl.accept_client( ACCEPT_TIMEOUT_MS ) );

    // --- Dispatch scenario ---
    if( scenario == "movement" ) {
        run_host_movement( srv, ctrl );
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
    } else {
        FAIL( "unknown COOP_SCENARIO: " + scenario );
    }

    cli.shutdown();
    g->coop_client_ = nullptr;
}

#endif // COOP_ENABLED
