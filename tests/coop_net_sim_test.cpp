#ifdef COOP_ENABLED
/**
 * Phase 5 foundation: coop_sim_transport self-tests.
 *
 * Tests the simulator's own behaviour (delivery timing, loss, reorder) —
 * NOT coop_server/coop_client behaviour under latency.
 *
 * The behavioural sim-driven tests (Gap C: reconcile/lag-comp under 100ms RTT
 * accumulating multi-entry pending; coalescing 16ms→1 tick vs 80ms→2 ticks;
 * Gap 4: ring-buffer cap/drop-oldest; Gap 6: next_seq_ uint32_t wrap in
 * coop_client's ring-buffer trim) all require driving a real coop_server +
 * coop_client through the sim transport.  That requires a transport-injection
 * seam on both classes (e.g. coop_server::inject_transport_for_test(),
 * coop_client::inject_transport_for_test()) which is not yet implemented.
 * Those tests remain open in the plan.
 *
 * Tags: [coop][simtransport]
 */

#include "catch/catch.hpp"
#include "coop_sim_transport.h"

// ---------------------------------------------------------------------------
// Delivery timing
// ---------------------------------------------------------------------------

TEST_CASE( "coop_sim_transport: message delivered at correct sim time",
           "[coop][simtransport]" ) {
    coop_sim_transport a, b;
    a.wire_peer( &b );
    a.latency_ms = 50;

    REQUIRE( a.send( "hello" ) );

    b.advance( 49 );
    CHECK_FALSE( b.poll() ); // t=49: not yet

    b.advance( 1 );          // t=50: exactly on time
    CHECK( b.poll() );
    std::string out;
    REQUIRE( b.recv( out ) );
    CHECK( out == "hello" );
}

TEST_CASE( "coop_sim_transport: message not delivered before latency elapses",
           "[coop][simtransport]" ) {
    coop_sim_transport a, b;
    a.wire_peer( &b );
    a.latency_ms = 100;

    a.send( "early" );
    b.advance( 99 );
    CHECK_FALSE( b.poll() );
    CHECK( b.inbox_size() == 1 ); // still pending
}

TEST_CASE( "coop_sim_transport: zero-latency delivers immediately",
           "[coop][simtransport]" ) {
    coop_sim_transport a, b;
    a.wire_peer( &b );
    a.latency_ms = 0;

    a.send( "instant" );
    CHECK( b.poll() );
    std::string out;
    b.recv( out );
    CHECK( out == "instant" );
}

TEST_CASE( "coop_sim_transport: multiple messages arrive in-order at correct times",
           "[coop][simtransport]" ) {
    coop_sim_transport a, b;
    a.wire_peer( &b );
    a.latency_ms = 10;

    // Send at t=0, 5, 10 — deliverable at t=10, 15, 20 respectively
    a.send( "m1" );
    a.advance( 5 ); a.send( "m2" );
    a.advance( 5 ); a.send( "m3" );

    b.advance( 10 );
    std::string s;
    REQUIRE( b.recv( s ) ); CHECK( s == "m1" );
    CHECK_FALSE( b.poll() );

    b.advance( 5 );
    REQUIRE( b.recv( s ) ); CHECK( s == "m2" );

    b.advance( 5 );
    REQUIRE( b.recv( s ) ); CHECK( s == "m3" );
}

// ---------------------------------------------------------------------------
// Loss
// ---------------------------------------------------------------------------

TEST_CASE( "coop_sim_transport: loss_rate=1.0 drops every message",
           "[coop][simtransport]" ) {
    coop_sim_transport a, b;
    a.wire_peer( &b );
    a.loss_rate = 1.0f;

    for( int i = 0; i < 20; ++i ) { a.send( "lost" ); }
    b.advance( 0 );
    CHECK( b.inbox_empty() );
}

TEST_CASE( "coop_sim_transport: loss_rate=0.0 delivers all messages",
           "[coop][simtransport]" ) {
    coop_sim_transport a, b;
    a.wire_peer( &b );
    a.loss_rate = 0.0f;

    for( int i = 0; i < 5; ++i ) {
        a.send( std::string( 1, static_cast<char>( 'A' + i ) ) );
    }
    b.advance( 0 );
    CHECK( b.inbox_size() == 5 );
}

// ---------------------------------------------------------------------------
// Reorder
// ---------------------------------------------------------------------------

TEST_CASE( "coop_sim_transport: reorder flag swaps last two enqueued messages",
           "[coop][simtransport]" ) {
    coop_sim_transport a, b;
    a.wire_peer( &b );
    a.reorder = true;

    a.send( "first" );
    a.send( "second" ); // → inbox becomes [second, first]

    b.advance( 0 );
    std::string s1, s2;
    b.recv( s1 ); b.recv( s2 );
    CHECK( s1 == "second" );
    CHECK( s2 == "first" );
}

// ---------------------------------------------------------------------------
// Bidirectional wiring
// ---------------------------------------------------------------------------

TEST_CASE( "coop_sim_transport: bidirectional wiring delivers cross-direction",
           "[coop][simtransport]" ) {
    coop_sim_transport a, b;
    a.wire_peer( &b );
    b.wire_peer( &a );
    a.latency_ms = 20;
    b.latency_ms = 20;

    a.send( "a→b" );
    b.send( "b→a" );
    a.advance( 20 );
    b.advance( 20 );

    std::string ra, rb;
    REQUIRE( a.recv( ra ) ); CHECK( ra == "b→a" );
    REQUIRE( b.recv( rb ) ); CHECK( rb == "a→b" );
}

// ---------------------------------------------------------------------------
// close_abruptly
// ---------------------------------------------------------------------------

TEST_CASE( "coop_sim_transport: close_abruptly drains inbox and severs reverse reference",
           "[coop][simtransport]" ) {
    coop_sim_transport a, b;
    a.wire_peer( &b );
    b.wire_peer( &a ); // bidirectional so close_abruptly can clear a.peer_
    a.send( "msg1" );
    a.send( "msg2" );

    b.close_abruptly();
    CHECK( b.inbox_empty() ); // b's inbox drained

    // a.peer_ was cleared by close_abruptly (b.peer_ pointed back to a)
    // so a.send() returns false and nothing reaches b
    const bool sent = a.send( "msg3" );
    CHECK_FALSE( sent );
    a.advance( 0 );
    CHECK( b.inbox_empty() ); // confirmed: reverse reference severed
}

// ---------------------------------------------------------------------------
// recv() contract: always non-blocking (timeout_ms ignored)
// The sim is driven by advance(); blocking recv semantics are not emulated.
// ---------------------------------------------------------------------------

TEST_CASE( "coop_sim_transport: recv returns false when inbox empty regardless of timeout",
           "[coop][simtransport]" ) {
    coop_sim_transport a, b;
    a.wire_peer( &b );
    // Nothing sent — recv must return false immediately even with large timeout
    std::string out;
    CHECK_FALSE( b.recv( out, 5000 ) ); // timeout arg ignored
    CHECK_FALSE( b.recv( out, -1 ) );   // blocking sentinel also ignored
}

#endif // COOP_ENABLED
