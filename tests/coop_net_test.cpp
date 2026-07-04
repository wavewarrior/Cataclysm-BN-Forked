#ifdef COOP_ENABLED

#include "catch/catch.hpp"
#include "coop_net.h"

#include <SDL3_net/SDL_net.h>
#include <chrono>
#include <string>
#include <thread>

// ---------------------------------------------------------------------------
// Infrastructure
// ---------------------------------------------------------------------------

namespace
{

constexpr Uint16 k_test_port = 45701;

/// RAII holder — guarantees cleanup even when REQUIRE throws
/// (Catch2 exception-mode unwinds the stack through destructors).
struct LoopbackFixture {
    bool              net_inited = false;
    NET_Server*       server     = nullptr;
    NET_Address*      addr       = nullptr;
    NET_StreamSocket* cli        = nullptr; ///< client-side stream socket
    NET_StreamSocket* srv        = nullptr; ///< server-side accepted socket

    ~LoopbackFixture() {
        if( cli )        { NET_DestroyStreamSocket( cli ); }
        if( srv )        { NET_DestroyStreamSocket( srv ); }
        if( server )     { NET_DestroyServer( server ); }
        if( addr )       { NET_UnrefAddress( addr ); }
        if( net_inited ) { NET_Quit(); }
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// TEST CASE 1: framing round-trip
// ---------------------------------------------------------------------------

TEST_CASE( "coop_net framing: send/recv round-trip over loopback", "[coop][net]" )
{
    LoopbackFixture fix;

    // NET_Init — skip gracefully if SDL_net is unavailable (e.g. sandbox CI).
    // Catch2 v2 has no SKIP(); WARN + return is the v2 idiom.
    if( !NET_Init() ) {
        WARN( "NET_Init failed — skipping: " + std::string( SDL_GetError() ) );
        return;
    }
    fix.net_inited = true;

    // Server — skip if the port is already bound by another process.
    fix.server = NET_CreateServer( nullptr, k_test_port, 0 );
    if( !fix.server ) {
        WARN( "Port " + std::to_string( k_test_port ) + " in use — skipping" );
        return;
    }

    // Resolve loopback address (DNS is async; wait up to 2 s).
    fix.addr = NET_ResolveHostname( "127.0.0.1" );
    REQUIRE( fix.addr != nullptr );
    REQUIRE( NET_WaitUntilResolved( fix.addr, 2000 ) == NET_SUCCESS );

    // Connect client (also async; NET_WaitUntilConnected blocks with timeout).
    fix.cli = NET_CreateClient( fix.addr, k_test_port, 0 );
    REQUIRE( fix.cli != nullptr );
    REQUIRE( NET_WaitUntilConnected( fix.cli, 2000 ) == NET_SUCCESS );

    // Accept on the server side: poll with SDL_Delay(1) for up to 2 s.
    for( int t = 0; t < 2000 && !fix.srv; ++t ) {
        REQUIRE( NET_AcceptClient( fix.server, &fix.srv ) ); // hard error → abort
        if( !fix.srv ) { SDL_Delay( 1 ); }
    }
    if( !fix.srv ) { FAIL( "server accept timed out after 2 s" ); }

    // --- Client → server: 3 different-length payloads -----------------------

    const std::string payloads[] = {
        // minimal
        "{}",
        // world_seed wire format
        R"({"t":2,"d":{"turn":1,"spawn_x":0,"spawn_y":0,"spawn_z":0,"player_name":"Alice","world_name":"W"}})",
        // large frame (1 KiB of padding)
        std::string( 1024, 'x' ),
    };

    for( const auto& p : payloads ) {
        REQUIRE( coop_net::send( fix.cli, p ) );
        std::string buf;
        REQUIRE( coop_net::recv( fix.srv, buf, 5000 ) );
        REQUIRE( buf == p );
    }

    // --- Server → client: one payload ----------------------------------------

    const std::string reply =
        R"({"t":20,"turn":1,"last_seq":0,"tiles":[],"monsters":[],"proxy_ax":0,"proxy_ay":0,"proxy_az":0,"host_ax":0,"host_ay":0,"host_az":0})";
    REQUIRE( coop_net::send( fix.srv, reply ) );
    std::string buf;
    REQUIRE( coop_net::recv( fix.cli, buf, 5000 ) );
    REQUIRE( buf == reply );
}

// ---------------------------------------------------------------------------
// TEST CASE 2: poll readiness
// ---------------------------------------------------------------------------

TEST_CASE( "coop_net poll: returns false when no data, true when data present",
           "[coop][net]" )
{
    LoopbackFixture fix;

    if( !NET_Init() ) {
        WARN( "NET_Init failed — skipping: " + std::string( SDL_GetError() ) );
        return;
    }
    fix.net_inited = true;

    fix.server = NET_CreateServer( nullptr, k_test_port, 0 );
    if( !fix.server ) {
        WARN( "Port " + std::to_string( k_test_port ) + " in use — skipping" );
        return;
    }

    fix.addr = NET_ResolveHostname( "127.0.0.1" );
    REQUIRE( fix.addr != nullptr );
    REQUIRE( NET_WaitUntilResolved( fix.addr, 2000 ) == NET_SUCCESS );

    fix.cli = NET_CreateClient( fix.addr, k_test_port, 0 );
    REQUIRE( fix.cli != nullptr );
    REQUIRE( NET_WaitUntilConnected( fix.cli, 2000 ) == NET_SUCCESS );

    for( int t = 0; t < 2000 && !fix.srv; ++t ) {
        REQUIRE( NET_AcceptClient( fix.server, &fix.srv ) );
        if( !fix.srv ) { SDL_Delay( 1 ); }
    }
    if( !fix.srv ) { FAIL( "server accept timed out after 2 s" ); }

    // Before send: no bytes are queued on the server-side receive end.
    CHECK_FALSE( coop_net::poll( fix.srv ) );

    // Send an action packet from client to server.
    const std::string msg = R"({"t":11,"d":{"seq":1,"key":"MOVE_N","ctx":""}})";
    REQUIRE( coop_net::send( fix.cli, msg ) );

    // poll() must transition to true within 100 ms.
    bool became_ready = false;
    for( int t = 0; t < 100; ++t ) {
        if( coop_net::poll( fix.srv ) ) {
            became_ready = true;
            break;
        }
        SDL_Delay( 1 );
    }
    REQUIRE( became_ready );

    // recv() drains the frame and content must be intact.
    std::string buf;
    REQUIRE( coop_net::recv( fix.srv, buf, 5000 ) );
    REQUIRE( buf == msg );
}

#endif // COOP_ENABLED
