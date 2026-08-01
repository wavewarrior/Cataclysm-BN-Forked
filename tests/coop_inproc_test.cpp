/**
 * In-process co-op integration tests.
 *
 * Wires a coop_server and coop_client through coop_sim_transport in a single
 * process.  NO receiver thread — the main test thread drives receive manually
 * via process_incoming_for_test(), giving deterministic, single-threaded
 * control over message delivery and avoiding the std::deque data-race that
 * would occur with start_receiver_thread() + coop_sim_transport.
 *
 * What these tests prove that unit tests do NOT:
 *   - The full relay chain: queue_action → transport send → server receive →
 *     dispatch_packet → action_q_ → execute_client_action → proxy mutation.
 *   - The full sync chain: build_and_send_sync → transport send → client
 *     receive → apply_sync.
 *   - Server and client objects interoperate through the transport abstraction
 *     without production code changes.
 *
 * Tags: [coop][inproc]
 */

#include "avatar.h"
#include "calendar.h"
#include "catch/catch_amalgamated.hpp"
#include "coop_client.h"
#include "coop_checksum.h"
#include "coop_server.h"
#include "coop_session.h"
#include "coop_sim_transport.h"
#include "game.h"
#include "map.h"
#include "map_helpers.h"
#include "npc.h"
#include "state_helpers.h"
#include "type_id.h"

namespace
{

/// RAII guard: sets coop_session::mode on construction, restores on destruction.
/// Guarantees the singleton is never left in a stale mode after a test failure.
struct coop_mode_guard {
    coop_mode saved;
    explicit coop_mode_guard( coop_mode m )
        : saved( coop_session::get().mode ) { coop_session::get().mode = m; }
    ~coop_mode_guard() { coop_session::get().mode = saved; }
    coop_mode_guard( const coop_mode_guard & ) = delete;
    auto operator=( const coop_mode_guard & ) -> coop_mode_guard & = delete;
};

/// In-process co-op test harness.
///
/// Session stays in `host` mode throughout; RAII guards briefly flip to
/// `client` for methods that check coop_session::is_client().
struct inproc_harness {
    coop_server srv;
    coop_client cli;
    coop_sim_transport *srv_tx = nullptr; // raw ptr; owned by srv via transport_
    coop_sim_transport *cli_tx = nullptr; // raw ptr; owned by cli via transport_
    npc *proxy = nullptr;

    /// Set up world, wire transports, run the full join sequence.
    auto setup() -> void {
        clear_all_state();
        build_test_map( ter_id( "t_grass" ) );

        auto &sess = coop_session::get();
        sess.mode = coop_mode::host;
        sess.partner_name = "TestClient";
        sess.dimension_id = g->get_current_dimension_id();

        // Create and wire sim transports (zero latency, no loss).
        auto *stx = new coop_sim_transport();
        auto *ctx = new coop_sim_transport();
        stx->wire_peer( ctx );
        ctx->wire_peer( stx );
        srv_tx = stx;
        cli_tx = ctx;

        srv.set_transport_for_test( std::unique_ptr<coop_transport>( stx ) );
        cli.set_transport_for_test( std::unique_ptr<coop_transport>( ctx ) );
        srv.set_running_for_test( true );
        srv.set_join_phase_for_test( client_join_phase::connected );

        // World-seed exchange — server sends directly through transport.
        REQUIRE( srv.send_world_seed( "TestClient" ) );
        {
            coop_mode_guard mcli( coop_mode::client );
            INFO( "cli_tx inbox_size=" << cli_tx->inbox_size()
                  << " srv_tx inbox_size=" << srv_tx->inbox_size() );
            REQUIRE( cli.receive_world_seed() );
            REQUIRE( cli.send_join_info() );
        }
        REQUIRE( srv.wait_for_join_info() );

        const auto spawn_pos = srv.client_join_pos().value_or( g->u.abs_pos() );
        proxy = srv.spawn_proxy_npc( spawn_pos, "TestClient" );
        REQUIRE( proxy != nullptr );

        // send_initial_sync drains send_q_ internally and sends via transport.
        REQUIRE( srv.send_initial_sync() );

        // Client receives the initial sync.
        {
            coop_mode_guard mcli( coop_mode::client );
            cli.coop_world_tick();
        }
    }

    /// Run one full tick cycle.
    ///
    /// Order: client-tick (send actions) → server-incoming → server-tick →
    ///        flush-to-client.  Client goes first so the actions it queued
    ///        before tick() are sent and available for the server to process
    ///        in the same cycle.
    auto tick() -> void {
        // 1. Client tick: send queued actions + status, receive previous sync.
        {
            coop_mode_guard mcli( coop_mode::client );
            cli.coop_world_tick();
        }

        // 2. Server processes incoming (actions the client just sent).
        srv.process_incoming_for_test();

        // 3. Server world tick: game sim, action drain, sync generation.
        srv.coop_world_tick();

        // 4. Flush server send queue → client inbox (for next tick's step 1).
        srv.flush_send_queue_for_test();
    }

    ~inproc_harness() {
        // Sever transport peer links BEFORE member destructors run.
        // Without this, ~coop_client sends disconnect (destroying cli_tx),
        // then ~coop_server sends through srv_tx whose peer_ is dangling → UAF.
        if( srv_tx ) { srv_tx->close_abruptly(); }
        if( cli_tx ) { cli_tx->close_abruptly(); }

        auto &sess = coop_session::get();
        sess.mode = coop_mode::none;
        sess.proxy_npc_id = character_id();
        sess.partner_name.clear();
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Join & connectivity
// ---------------------------------------------------------------------------

TEST_CASE( "inproc: join sequence completes without crash", "[coop][inproc]" )
{
    inproc_harness h;
    h.setup();
    CHECK( h.proxy != nullptr );
    CHECK( h.proxy->is_coop_remote );
}

TEST_CASE( "inproc: initial sync delivers tiles to client", "[coop][inproc]" )
{
    inproc_harness h;
    h.setup();
    // got_full_tile_sync is set during apply_sync when a non-empty tiles array
    // is present — the initial sync always includes the 5×5 submap grid.
    CHECK( h.cli.got_full_tile_sync_for_test() );
}

// ---------------------------------------------------------------------------
// Movement relay
// ---------------------------------------------------------------------------

TEST_CASE( "inproc: MOVE_N relays to proxy", "[coop][inproc]" )
{
    inproc_harness h;
    h.setup();

    const auto start = h.proxy->abs_pos();

    h.cli.queue_action( "MOVE_N" );
    h.tick();

    const auto end = h.proxy->abs_pos();
    CHECK( end.y() == start.y() - 1 );
    CHECK( end.x() == start.x() );
}

TEST_CASE( "inproc: three consecutive MOVE_N relay sequentially",
           "[coop][inproc]" )
{
    inproc_harness h;
    h.setup();

    const auto start = h.proxy->abs_pos();

    h.cli.queue_action( "MOVE_N" );
    h.cli.queue_action( "MOVE_N" );
    h.cli.queue_action( "MOVE_N" );

    h.tick();
    h.tick();
    h.tick();

    const auto end = h.proxy->abs_pos();
    CHECK( end.y() == start.y() - 3 );
    CHECK( end.x() == start.x() );
}

TEST_CASE( "inproc: all four cardinal directions relay correctly",
           "[coop][inproc]" )
{
    inproc_harness h;
    h.setup();

    const auto start = h.proxy->abs_pos();

    // N (+0,-1)
    h.cli.queue_action( "MOVE_N" );
    h.tick();
    CHECK( h.proxy->abs_pos() == tripoint_abs_ms{
        start.x(), start.y() - 1, start.z()} );

    // E (+1,0)
    h.cli.queue_action( "MOVE_E" );
    h.tick();
    CHECK( h.proxy->abs_pos() == tripoint_abs_ms{
        start.x() + 1, start.y() - 1, start.z()} );

    // S (0,+1)
    h.cli.queue_action( "MOVE_S" );
    h.tick();
    CHECK( h.proxy->abs_pos() == tripoint_abs_ms{
        start.x() + 1, start.y(), start.z()} );

    // W (-1,0) — back to start
    h.cli.queue_action( "MOVE_W" );
    h.tick();
    CHECK( h.proxy->abs_pos() == start );
}

// ---------------------------------------------------------------------------
// Status sync
// ---------------------------------------------------------------------------

TEST_CASE( "inproc: client_status reaches server each tick", "[coop][inproc]" )
{
    inproc_harness h;
    h.setup();

    // Run one tick — client sends client_status automatically in coop_world_tick.
    h.tick();

    // Server processes client_status in process_incoming_for_test and sets
    // client_hp_pct_.  The test player is alive, so hp_pct > 0.
    CHECK( h.srv.client_hp_pct() > 0 );
}

// ---------------------------------------------------------------------------
// Force resync
// ---------------------------------------------------------------------------

TEST_CASE( "inproc: force_resync triggers full tile sync", "[coop][inproc]" )
{
    inproc_harness h;
    h.setup();

    h.srv.set_force_resync_for_test();
    CHECK( h.srv.force_resync_pending_for_test() );

    h.tick();

    // The server consumed the force_resync flag during build_and_send_sync.
    CHECK_FALSE( h.srv.force_resync_pending_for_test() );
}

// ---------------------------------------------------------------------------
// Idle / fast-forward
// ---------------------------------------------------------------------------

TEST_CASE( "inproc: both_idle false when neither is idle", "[coop][inproc]" )
{
    inproc_harness h;
    h.setup();

    // Neither host nor client is sleeping/crafting.
    h.tick();
    CHECK_FALSE( h.srv.both_idle() );
}

// ---------------------------------------------------------------------------
// Multi-tick stability
// ---------------------------------------------------------------------------

TEST_CASE( "inproc: 20 ticks without crash or assertion failure",
           "[coop][inproc]" )
{
    inproc_harness h;
    h.setup();

    // Intersperse movement with idle ticks.
    for( int i = 0; i < 20; ++i ) {
        if( i % 3 == 0 ) {
            h.cli.queue_action( "MOVE_N" );
        }
        h.tick();
    }
    // Just surviving 20 ticks with interleaved movement is the assertion.
    CHECK( h.proxy != nullptr );
}

// ---------------------------------------------------------------------------
// World-state checksum (Step 2)
// ---------------------------------------------------------------------------

TEST_CASE( "inproc: checksum is stable across idle ticks", "[coop][inproc][checksum]" )
{
    inproc_harness h;
    h.setup();

    // Let the world settle — initial setup + first post_action_world_step()
    // may mutate calendar/weather/NPC state once. After settling, idle ticks
    // with no queued actions must produce zero world-state delta.
    h.tick();
    h.tick();
    const auto cs0 = coop_world_checksum();
    h.tick();
    h.tick();
    const auto cs1 = coop_world_checksum();

    // No mutations occurred — checksum must be identical.
    CHECK( cs0 == cs1 );
}

TEST_CASE( "inproc: checksum changes after movement", "[coop][inproc][checksum]" )
{
    inproc_harness h;
    h.setup();

    const auto before = coop_world_checksum();

    h.cli.queue_action( "MOVE_N" );
    h.tick();

    const auto after = coop_world_checksum();

    // Proxy moved — position component of the hash changed.
    CHECK( before != after );
}

TEST_CASE( "inproc: checksum converges after 10 ticks of movement",
           "[coop][inproc][checksum]" )
{
    inproc_harness h;
    h.setup();

    // 5 moves, then 5 idle ticks.
    for( int i = 0; i < 5; ++i ) {
        h.cli.queue_action( "MOVE_N" );
        h.tick();
    }
    for( int i = 0; i < 5; ++i ) {
        h.tick();
    }

    // Two consecutive idle-tick checksums must match.
    const auto cs_a = coop_world_checksum();
    h.tick();
    const auto cs_b = coop_world_checksum();
    CHECK( cs_a == cs_b );
}

// ---------------------------------------------------------------------------
// Client world-step parity
// ---------------------------------------------------------------------------

// Before the parity work the client ran only u.process_turn() per synced turn, which
// leaves avatar biology frozen: no metabolism, no weather, no body temperature.  The
// contract now is that a co-op client runs the avatar-local half of
// post_action_world_step() via game::coop_client_turn_step().
//
// The control half of this case is what makes it discriminating: u.process_turn() alone
// must NOT move stored kcal, so any movement in the second half is attributable to the
// new step.  Character::update_stomach() only bills calories when a 5-minute boundary is
// crossed (character_needs.cpp:811), hence the 400-turn spans.
TEST_CASE( "inproc: client turn step runs avatar biology", "[coop][inproc][parity]" )
{
    constexpr int turns = 400;

    inproc_harness h;
    h.setup();
    coop_mode_guard mcli( coop_mode::client );

    // Control: the old client behaviour.
    const int kcal_control_before = g->u.get_stored_kcal();
    for( int i = 0; i < turns; ++i ) {
        calendar::turn += 1_turns;
        g->u.process_turn();
    }
    CHECK( g->u.get_stored_kcal() == kcal_control_before );

    // New behaviour: metabolism runs, so stored calories are billed.
    const int kcal_before = g->u.get_stored_kcal();
    const time_point turn_before = calendar::turn;
    for( int i = 0; i < turns; ++i ) {
        calendar::turn += 1_turns;
        g->coop_client_turn_step();
    }
    CHECK( calendar::turn > turn_before );
    CHECK( g->u.get_stored_kcal() != kcal_before );
}

// The per-frame half must be safe to call unconditionally, including when a sync carried
// no advanced turns — that is how the client's vision cache and monster info stay fresh.
TEST_CASE( "inproc: client frame step is safe with no turns advanced",
           "[coop][inproc][parity]" )
{
    inproc_harness h;
    h.setup();
    coop_mode_guard mcli( coop_mode::client );

    const time_point turn_before = calendar::turn;
    for( int i = 0; i < 5; ++i ) {
        g->coop_client_frame_step();
    }
    // Purely local caches/UI/audio: no world time may pass.
    CHECK( calendar::turn == turn_before );
}

// End-to-end: the new per-turn and per-frame calls in apply_sync() must not break the
// relay, and world time must still advance across a long run.
TEST_CASE( "inproc: turn advances across a long synced run", "[coop][inproc][parity]" )
{
    inproc_harness h;
    h.setup();

    const int turn_before = to_turn<int>( calendar::turn );
    for( int i = 0; i < 60; ++i ) {
        h.tick();
    }
    CHECK( to_turn<int>( calendar::turn ) > turn_before );
    CHECK( h.proxy != nullptr );
}
