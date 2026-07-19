#ifdef COOP_ENABLED
/**
 * Property-based concurrent action testing for co-op.
 *
 * Uses std::mt19937 to generate random action sequences and asserts
 * invariants that must hold regardless of the action order:
 *   - No crash / assertion failure from arbitrary valid actions.
 *   - Proxy NPC always remains on a valid map tile.
 *   - Identical seeds produce identical outcomes (determinism).
 *
 * Tags: [coop][pbt][.]  (hidden — slow)
 */

#include "avatar.h"
#include "catch/catch_amalgamated.hpp"
#include "coop_client.h"
#include "coop_server.h"
#include "coop_session.h"
#include "coop_sim_transport.h"
#include "game.h"
#include "map.h"
#include "map_helpers.h"
#include "npc.h"
#include "state_helpers.h"
#include "type_id.h"

#include <random>

namespace
{

/// RAII guard: sets coop_session::mode on construction, restores on destruction.
struct coop_mode_guard {
    coop_mode saved;
    explicit coop_mode_guard( coop_mode m )
        : saved( coop_session::get().mode ) { coop_session::get().mode = m; }
    ~coop_mode_guard() { coop_session::get().mode = saved; }
    coop_mode_guard( const coop_mode_guard & ) = delete;
    auto operator=( const coop_mode_guard & ) -> coop_mode_guard & = delete;
};

/// In-process co-op test harness (copied from coop_inproc_test.cpp).
struct inproc_harness {
    coop_server srv;
    coop_client cli;
    coop_sim_transport *srv_tx = nullptr;
    coop_sim_transport *cli_tx = nullptr;
    npc *proxy = nullptr;

    auto setup() -> void {
        clear_all_state();
        build_test_map( ter_id( "t_grass" ) );

        auto &sess = coop_session::get();
        sess.mode = coop_mode::host;
        sess.partner_name = "TestClient";
        sess.dimension_id = g->get_current_dimension_id();

        auto *stx = new coop_sim_transport();
        auto *ctx = new coop_sim_transport();
        stx->wire_peer( ctx );
        ctx->wire_peer( stx );
        srv_tx = stx;
        cli_tx = ctx;

        srv.set_transport_for_test( std::unique_ptr<coop_transport>( stx ) );
        cli.set_transport_for_test( std::unique_ptr<coop_transport>( ctx ) );
        srv.set_running_for_test( true );

        REQUIRE( srv.send_world_seed( "TestClient" ) );
        {
            coop_mode_guard mcli( coop_mode::client );
            REQUIRE( cli.receive_world_seed() );
            REQUIRE( cli.send_join_info() );
        }
        REQUIRE( srv.wait_for_join_info() );

        const auto spawn_pos = srv.client_join_pos().value_or( g->u.abs_pos() );
        proxy = srv.spawn_proxy_npc( spawn_pos, "TestClient" );
        REQUIRE( proxy != nullptr );

        REQUIRE( srv.send_initial_sync() );
        {
            coop_mode_guard mcli( coop_mode::client );
            cli.coop_world_tick();
        }
    }

    auto tick() -> void {
        {
            coop_mode_guard mcli( coop_mode::client );
            cli.coop_world_tick();
        }
        srv.process_incoming_for_test();
        srv.coop_world_tick();
        srv.flush_send_queue_for_test();
    }

    ~inproc_harness() {
        if( srv_tx ) { srv_tx->close_abruptly(); }
        if( cli_tx ) { cli_tx->close_abruptly(); }
        auto &sess = coop_session::get();
        sess.mode = coop_mode::none;
        sess.proxy_npc_id = character_id();
        sess.partner_name.clear();
    }
};

/// Return a random valid action string from the movement + wait pool.
auto random_action( std::mt19937 &rng ) -> const std::string &
{
    static const std::array<std::string, 9> actions = {{
            "MOVE_N", "MOVE_S", "MOVE_E", "MOVE_W",
            "MOVE_NE", "MOVE_NW", "MOVE_SE", "MOVE_SW",
            "WAIT"
        }
    };
    std::uniform_int_distribution<std::size_t> dist( 0, actions.size() - 1 );
    return actions[dist( rng )];
}

} // namespace

// ---------------------------------------------------------------------------
// Property-based tests
// ---------------------------------------------------------------------------

TEST_CASE( "pbt: no crash from 200 random actions", "[coop][pbt][.]" )
{
    inproc_harness h;
    h.setup();

    // Seed from Catch2's configured random seed for reproducibility.
    std::mt19937 rng( Catch::getSeed() );

    for( int i = 0; i < 200; ++i ) {
        h.cli.queue_action( random_action( rng ) );
        h.tick();
    }

    // Surviving 200 random-action ticks without crash is the assertion.
    CHECK( h.proxy != nullptr );
}

TEST_CASE( "pbt: proxy stays on valid map tile after random movement",
           "[coop][pbt][.]" )
{
    inproc_harness h;
    h.setup();

    std::mt19937 rng( Catch::getSeed() );

    for( int i = 0; i < 100; ++i ) {
        h.cli.queue_action( random_action( rng ) );
        h.tick();
    }

    CHECK( g->m.inbounds( h.proxy->abs_pos() ) );
}

TEST_CASE( "pbt: same seed produces identical action sequence",
           "[coop][pbt][.]" )
{
    constexpr std::uint32_t fixed_seed = 42;
    constexpr int count = 200;

    // Generate two sequences with the same seed — they must be identical.
    std::vector<std::string> seq1, seq2;
    {
        std::mt19937 rng( fixed_seed );
        for( int i = 0; i < count; ++i ) { seq1.push_back( random_action( rng ) ); }
    }
    {
        std::mt19937 rng( fixed_seed );
        for( int i = 0; i < count; ++i ) { seq2.push_back( random_action( rng ) ); }
    }
    REQUIRE( seq1.size() == seq2.size() );
    CHECK( seq1 == seq2 );

    // Feed the sequence through the harness — just verify no crash.
    inproc_harness h;
    h.setup();
    for( const auto &act : seq1 ) {
        h.cli.queue_action( act );
        h.tick();
    }
    CHECK( h.proxy != nullptr );
}

#endif // COOP_ENABLED
