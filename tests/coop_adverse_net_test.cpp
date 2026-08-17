/**
 * Adverse network condition tests for co-op.
 *
 * Exercises the real coop_server/coop_client tick loops through
 * coop_sim_transport configured with latency, loss, and reorder.
 * These conditions are tested in isolation in coop_net_sim_test.cpp;
 * here they are applied to actual game objects for the first time.
 *
 * Tags: [coop][adverse]
 */

#include "avatar.h"
#include "catch/catch_amalgamated.hpp"
#include "coop_checksum.h"
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

namespace {

struct coop_mode_guard {
    coop_mode saved;
    explicit coop_mode_guard(coop_mode m): saved(coop_session::get().mode) {
        coop_session::get().mode = m;
    }
    ~coop_mode_guard() { coop_session::get().mode = saved; }
    coop_mode_guard(const coop_mode_guard&) = delete;
    auto operator=(const coop_mode_guard&) -> coop_mode_guard& = delete;
};

struct inproc_harness {
    coop_server srv;
    coop_client cli;
    coop_sim_transport* srv_tx = nullptr;
    coop_sim_transport* cli_tx = nullptr;
    npc* proxy = nullptr;

    auto setup() -> void {
        clear_all_state();
        build_test_map(ter_id("t_grass"));
        auto& sess = coop_session::get();
        sess.mode = coop_mode::host;
        sess.partner_name = "TestClient";
        sess.dimension_id = g->get_current_dimension_id();

        auto* stx = new coop_sim_transport();
        auto* ctx = new coop_sim_transport();
        stx->wire_peer(ctx);
        ctx->wire_peer(stx);
        srv_tx = stx;
        cli_tx = ctx;
        srv.set_transport_for_test(std::unique_ptr<coop_transport>(stx));
        cli.set_transport_for_test(std::unique_ptr<coop_transport>(ctx));
        srv.set_running_for_test(true);
        srv.set_join_phase_for_test(client_join_phase::connected);

        REQUIRE(srv.send_world_seed("TestClient"));
        {
            coop_mode_guard mcli(coop_mode::client);
            REQUIRE(cli.receive_world_seed());
            REQUIRE(cli.send_join_info());
        }
        REQUIRE(srv.wait_for_join_info());
        const auto sp = srv.client_join_pos().value_or(g->u.abs_pos());
        proxy = srv.spawn_proxy_npc(sp, "TestClient");
        REQUIRE(proxy != nullptr);
        REQUIRE(srv.send_initial_sync());
        {
            coop_mode_guard mcli(coop_mode::client);
            cli.coop_world_tick();
        }
    }

    /// Tick with sim-time advancement so latency-delayed messages become
    /// deliverable.  `tick_ms` should match the configured latency.
    auto tick(int tick_ms = 0) -> void {
        if (tick_ms > 0) {
            srv_tx->advance(tick_ms);
            cli_tx->advance(tick_ms);
        }
        {
            coop_mode_guard mcli(coop_mode::client);
            cli.coop_world_tick();
        }
        srv.process_incoming_for_test();
        srv.coop_world_tick();
        srv.flush_send_queue_for_test();
    }

    ~inproc_harness() {
        if (srv_tx) { srv_tx->close_abruptly(); }
        if (cli_tx) { cli_tx->close_abruptly(); }
        auto& sess = coop_session::get();
        sess.mode = coop_mode::none;
        sess.proxy_npc_id = character_id();
        sess.partner_name.clear();
    }
};

} // namespace

// ---------------------------------------------------------------------------
// High latency (200 ms one-way)
// ---------------------------------------------------------------------------

TEST_CASE("adverse: movement under 200ms latency converges", "[coop][adverse]") {
    inproc_harness h;
    h.setup();

    // Apply latency AFTER the join sequence (which needs zero-latency).
    h.srv_tx->latency_ms = 200;
    h.cli_tx->latency_ms = 200;

    const auto start = h.proxy->abs_pos();

    // Queue 3 moves.
    h.cli.queue_action("MOVE_N");
    h.cli.queue_action("MOVE_N");
    h.cli.queue_action("MOVE_N");

    // Tick with 200 ms advancement — messages become deliverable.
    // Need extra ticks because messages are delayed by latency.
    for (int t = 0; t < 10; ++t) { h.tick(200); }

    const auto end = h.proxy->abs_pos();
    // Proxy should have moved 3 tiles north within 10 ticks.
    CHECK(end.y() == start.y() - 3);
    CHECK(end.x() == start.x());
}

// ---------------------------------------------------------------------------
// Packet loss (5%)
// ---------------------------------------------------------------------------

TEST_CASE("adverse: 5% loss does not crash", "[coop][adverse]") {
    inproc_harness h;
    h.setup();

    h.srv_tx->loss_rate = 0.05f;
    h.cli_tx->loss_rate = 0.05f;

    // Run 50 ticks with movement — some packets will be lost.
    // The test just asserts no crash or assertion failure.
    for (int t = 0; t < 50; ++t) {
        if (t % 2 == 0) { h.cli.queue_action("MOVE_N"); }
        h.tick();
    }

    CHECK(h.proxy != nullptr);
}

// ---------------------------------------------------------------------------
// Message reorder
// ---------------------------------------------------------------------------

TEST_CASE("adverse: reordered messages do not crash", "[coop][adverse]") {
    inproc_harness h;
    h.setup();

    h.srv_tx->reorder = true;
    h.cli_tx->reorder = true;

    // Run 30 ticks with movement and reordering.
    for (int t = 0; t < 30; ++t) {
        h.cli.queue_action("MOVE_E");
        h.tick();
    }

    CHECK(h.proxy != nullptr);
}

// ---------------------------------------------------------------------------
// Jitter (alternating 0ms and 150ms)
// ---------------------------------------------------------------------------

TEST_CASE("adverse: jitter does not crash or desync positions", "[coop][adverse]") {
    inproc_harness h;
    h.setup();

    for (int t = 0; t < 40; ++t) {
        // Alternate latency to simulate jitter.
        const int lat = (t % 2 == 0) ? 0 : 150;
        h.srv_tx->latency_ms = lat;
        h.cli_tx->latency_ms = lat;

        h.cli.queue_action("MOVE_S");
        h.tick(150); // always advance enough for worst-case delivery
    }

    CHECK(h.proxy != nullptr);

    // Drain remaining in-flight messages.
    for (int t = 0; t < 5; ++t) { h.tick(150); }

    // After draining, two idle-tick checksums must converge.
    h.srv_tx->latency_ms = 0;
    h.cli_tx->latency_ms = 0;
    h.tick();
    const auto cs1 = coop_world_checksum();
    h.tick();
    const auto cs2 = coop_world_checksum();
    CHECK(cs1 == cs2);
}
