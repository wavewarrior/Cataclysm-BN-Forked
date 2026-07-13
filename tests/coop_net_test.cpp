#ifdef COOP_ENABLED

#include "catch/catch_amalgamated.hpp"
#include "coop_net.h"

#include <SDL3_net/SDL_net.h>
#include <array>
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
    NET_Server       *server     = nullptr;
    NET_Address      *addr       = nullptr;
    NET_StreamSocket *cli        = nullptr; ///< client-side stream socket
    NET_StreamSocket *srv        = nullptr; ///< server-side accepted socket

    ~LoopbackFixture() {
        if( cli )        { NET_DestroyStreamSocket( cli ); }
        if( srv )        { NET_DestroyStreamSocket( srv ); }
        if( server )     { NET_DestroyServer( server ); }
        if( addr )       { NET_UnrefAddress( addr ); }
        if( net_inited ) { NET_Quit(); }
    }
};

constexpr Uint16 k_adv_port = 45702; // separate port avoids TIME_WAIT conflicts

/// Send exactly 4 bytes as a big-endian uint32 — bypasses coop_net::send framing.
auto send_raw_be32( NET_StreamSocket* sock, uint32_t v ) -> bool
{
    std::array<uint8_t, 4> b{};
    b[0] = static_cast<uint8_t>( ( v >> 24 ) & 0xFF );
    b[1] = static_cast<uint8_t>( ( v >> 16 ) & 0xFF );
    b[2] = static_cast<uint8_t>( ( v >>  8 ) & 0xFF );
    b[3] = static_cast<uint8_t>( ( v ) & 0xFF );
    return NET_WriteToStreamSocket( sock, b.data(), 4 );
}

/// Set up a loopback pair.  Returns false if NET_Init or port bind fails
/// (caller should WARN + return in that case).
auto setup_loopback( LoopbackFixture& fix, Uint16 port ) -> bool
{
    if( !NET_Init() ) { return false; }
fix.net_inited = true;
fix.server = NET_CreateServer( nullptr, port, 0 );
if( !fix.server ) { return false; }
fix.addr = NET_ResolveHostname( "127.0.0.1" );
if( !fix.addr || NET_WaitUntilResolved( fix.addr, 2000 ) != NET_SUCCESS ) { return false; }
fix.cli = NET_CreateClient( fix.addr, port, 0 );
if( !fix.cli || NET_WaitUntilConnected( fix.cli, 2000 ) != NET_SUCCESS ) { return false; }
for( int t = 0; t < 2000 && !fix.srv; ++t ) {
    NET_AcceptClient( fix.server, &fix.srv );
        if( !fix.srv ) { SDL_Delay( 1 ); }
    }
    return fix.srv != nullptr;
}

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

// ---------------------------------------------------------------------------
// Adversarial framing tests — Gap A
// Each test sends a malformed or unexpected frame and asserts recv() handles
// it gracefully (returns false or true as documented, no crash, no hang, no
// unbounded allocation).
// ---------------------------------------------------------------------------

TEST_CASE( "coop_net recv: zero-length frame returns false, no crash", "[coop][net]" )
{
    LoopbackFixture fix;
    if( !setup_loopback( fix, k_adv_port ) ) {
        WARN( "loopback unavailable — skipping" );
        return;
    }
    // Send a 4-byte header with length=0.  recv() must return false immediately.
    REQUIRE( send_raw_be32( fix.cli, 0u ) );
    std::string buf;
    CHECK_FALSE( coop_net::recv( fix.srv, buf, 500 ) );
    CHECK( buf.empty() );
}

TEST_CASE( "coop_net recv: length=UINT32_MAX rejected without allocation", "[coop][net]" )
{
    LoopbackFixture fix;
    if( !setup_loopback( fix, k_adv_port ) ) {
        WARN( "loopback unavailable — skipping" );
        return;
    }
    // UINT32_MAX (~4 GB) exceeds the 64 MB cap — must return false immediately
    // without attempting to resize the buffer to 4 GB.
    REQUIRE( send_raw_be32( fix.cli, 0xFFFF'FFFFu ) );
    std::string buf;
    CHECK_FALSE( coop_net::recv( fix.srv, buf, 500 ) );
}

TEST_CASE( "coop_net recv: truncated frame returns false within timeout", "[coop][net]" )
{
    LoopbackFixture fix;
    if( !setup_loopback( fix, k_adv_port ) ) {
        WARN( "loopback unavailable — skipping" );
        return;
    }
    // Send a valid header claiming 20 bytes of payload, then close the sending
    // socket before sending the payload.  recv() must return false within the
    // timeout rather than blocking indefinitely.
    REQUIRE( send_raw_be32( fix.cli, 20u ) );
    NET_DestroyStreamSocket( fix.cli );
    fix.cli = nullptr; // prevent double-free in destructor
    std::string buf;
    // 300 ms is well above loopback RTT; failure mode is >300 ms (hang).
    CHECK_FALSE( coop_net::recv( fix.srv, buf, 300 ) );
}

TEST_CASE( "coop_net recv: partial header returns false within timeout", "[coop][net]" )
{
    LoopbackFixture fix;
    if( !setup_loopback( fix, k_adv_port ) ) {
        WARN( "loopback unavailable — skipping" );
        return;
    }
    // Send only 3 bytes (incomplete 4-byte header) then close.
    // recv() must return false rather than reading garbage as a length.
    const std::array<uint8_t, 3> junk = {0xDE, 0xAD, 0xBE};
    NET_WriteToStreamSocket( fix.cli, junk.data(), 3 );
    NET_DestroyStreamSocket( fix.cli );
    fix.cli = nullptr;
    std::string buf;
    CHECK_FALSE( coop_net::recv( fix.srv, buf, 300 ) );
}

TEST_CASE( "coop_net recv: unexpected packet type received successfully (transport is agnostic)",
           "[coop][net]" )
{
    LoopbackFixture fix;
    if( !setup_loopback( fix, k_adv_port ) ) {
        WARN( "loopback unavailable — skipping" );
        return;
    }
    // Send a valid action packet to a socket that has not completed a handshake.
    // coop_net::recv is a pure transport function — it does NOT enforce protocol
    // ordering.  It must return true and deliver the bytes unchanged.
    const std::string action = R"({"t":11,"d":{"seq":1,"key":"MOVE_N","ctx":""}})";
    REQUIRE( coop_net::send( fix.cli, action ) );
    std::string buf;
    REQUIRE( coop_net::recv( fix.srv, buf, 500 ) );
    CHECK( buf == action );
}

TEST_CASE( "coop_net recv: duplicate seq packet received twice without corruption",
           "[coop][net]" )
{
    LoopbackFixture fix;
    if( !setup_loopback( fix, k_adv_port ) ) {
        WARN( "loopback unavailable — skipping" );
        return;
    }
    // Send the same action packet twice.  Both must be received intact.
    // Deduplication is the application layer's responsibility (ring buffer);
    // the transport must not silently drop or corrupt either copy.
    const std::string action = R"({"t":11,"d":{"seq":5,"key":"PAUSE","ctx":""}})";
    REQUIRE( coop_net::send( fix.cli, action ) );
    REQUIRE( coop_net::send( fix.cli, action ) );
    std::string buf1, buf2;
    REQUIRE( coop_net::recv( fix.srv, buf1, 500 ) );
    REQUIRE( coop_net::recv( fix.srv, buf2, 500 ) );
    CHECK( buf1 == action );
    CHECK( buf2 == action );
}

#endif // COOP_ENABLED
