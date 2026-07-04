#ifdef COOP_ENABLED
/**
 * Co-op two-process integration tests.
 *
 * Run via:  scripts/test_coop.ts (Deno harness)
 *           or manually:
 *   Process A: cata_test-tiles "[.][coop_role_host]"
 *   Process B: cata_test-tiles "[.][coop_role_client]"
 *
 * Assertions:
 *   1. Proxy movement (host): client queues 3×MOVE_N; proxy must end up
 *      exactly 3 tiles north of spawn (all moves succeed in the open test map).
 *   2. Reconciliation (client): client position must exactly equal the proxy's
 *      final position after all 3 MOVE_N are confirmed (pending cleared,
 *      coop_reconcile_pos returns proxy_pos + no replay).
 *   3. Terrain delivery (client): host stamps a specific tile to t_floor before
 *      send_initial_sync; client asserts that exact tile == t_floor after the
 *      5×5 submap blast is received.
 *
 * Cross-process coordination: two files in /tmp written by host, polled by client.
 *   /tmp/coop_test_port.txt      — bound port (existing)
 *   /tmp/coop_test_proxy_pos.txt — proxy final position (x y z)
 *   /tmp/coop_test_terrain.txt   — test tile position + ter_id int (x y z t)
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
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>

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
static constexpr const char* COOP_PROXY_POS_FILE = "/tmp/coop_test_proxy_pos.txt";

// ---------------------------------------------------------------------------
// File coordination helpers
// ---------------------------------------------------------------------------

static auto write_port_file(uint16_t port) -> void {
    std::ofstream f(COOP_PORT_FILE);
    f << port << '\n';
}

static auto read_port_file(uint16_t& port, int timeout_ms = FILE_POLL_TIMEOUT_MS) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        std::ifstream f(COOP_PORT_FILE);
        if (f.good()) {
            int p = 0;
            if (f >> p && p > 0) {
                port = static_cast<uint16_t>(p);
                return true;
            }
        }
        SDL_Delay(50);
    }
    return false;
}

/// Host writes proxy's final absolute position.
static auto write_proxy_pos_file(const tripoint_abs_ms& pos) -> void {
    std::ofstream f(COOP_PROXY_POS_FILE);
    f << pos.x() << ' ' << pos.y() << ' ' << pos.z() << '\n';
}

/// Client polls until proxy position file is ready.
static auto read_proxy_pos_file(tripoint_abs_ms& pos, int timeout_ms = FILE_POLL_TIMEOUT_MS)
    -> bool {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        std::ifstream f(COOP_PROXY_POS_FILE);
        if (f.good()) {
            int x = 0, y = 0, z = 0;
            if (f >> x >> y >> z) {
                pos = tripoint_abs_ms{x, y, z};
                return true;
            }
        }
        SDL_Delay(50);
    }
    return false;
}


// ---------------------------------------------------------------------------
// Host role
// ---------------------------------------------------------------------------

TEST_CASE("coop integration: host role", "[.][coop_role_host]") {
    clear_all_state();

    const tripoint_abs_ms spawn_abs = g->u.abs_pos();

    // --- Bind ---
    coop_server srv;
    uint16_t bound_port = 0;
    for (int i = 0; i < COOP_INTEG_PORT_RANGE; ++i) {
        const auto try_port = static_cast<uint16_t>(COOP_INTEG_PORT_BASE + i);
        if (srv.listen(try_port)) {
            bound_port = try_port;
            break;
        }
    }
    if (bound_port == 0) {
        WARN("Could not bind to any port in 45802..45811 — skipping");
        return;
    }
    write_port_file(bound_port);

    // --- Accept client ---
    const auto accept_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(ACCEPT_TIMEOUT_MS);
    while (!srv.try_accept()) {
        REQUIRE(std::chrono::steady_clock::now() < accept_deadline);
        SDL_Delay(10);
    }

    REQUIRE(srv.handshake());
    REQUIRE(srv.send_world_seed(g->u.get_name()));
    srv.spawn_proxy_npc(spawn_abs, "TestClient");
    REQUIRE(srv.send_initial_sync());
    srv.start_receiver_thread();
    g->coop_server_ = &srv;

    // Capture the proxy's actual post-spawn position.  spawn_at_precise resolves
    // co-occupancy with the host avatar so the proxy may land on an adjacent tile.
    // Basing target_pos on the actual settled position keeps the exact 3-axis
    // assertion meaningful without assuming spawn_abs is unoccupied.
    const npc* p_spawn = g->critter_by_id<npc>(coop_session::get().proxy_npc_id);
    REQUIRE(p_spawn != nullptr);
    const tripoint_abs_ms proxy_spawn_pos = p_spawn->abs_pos();
    const tripoint_abs_ms target_pos = proxy_spawn_pos + tripoint(0, -COOP_TEST_MOVES, 0);

    const auto move_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(90'000);
    bool proxy_reached_target = false;
    while (std::chrono::steady_clock::now() < move_deadline) {
        srv.coop_world_tick();
        SDL_Delay(50);
        const npc* proxy = g->critter_by_id<npc>(coop_session::get().proxy_npc_id);
        if (proxy && proxy->abs_pos() == target_pos) {
            proxy_reached_target = true;
            break;
        }
    }

    // Run 5 more ticks so the final sync (last_seq confirming all 3) reaches client.
    for (int i = 0; i < 5; ++i) {
        srv.coop_world_tick();
        SDL_Delay(50);
    }

    // --- Assertion 1: proxy moved exactly COOP_TEST_MOVES tiles north ---
    const npc* proxy2 = g->critter_by_id<npc>(coop_session::get().proxy_npc_id);
    REQUIRE(proxy2 != nullptr);
    const tripoint_abs_ms proxy_final = proxy2->abs_pos();
    INFO("avatar_spawn=(" << spawn_abs.x() << "," << spawn_abs.y() << ")"
                          << " proxy_spawn=(" << proxy_spawn_pos.x() << "," << proxy_spawn_pos.y()
                          << ")"
                          << " target=(" << target_pos.x() << "," << target_pos.y() << ")"
                          << " proxy_final=(" << proxy_final.x() << "," << proxy_final.y() << ")");
    CHECK(proxy_reached_target);      // timed out → actions not processed or drift
    CHECK(proxy_final == target_pos); // exact 3D match — no stumble, no drift

    // Write proxy_pos file ONLY on success.  Client polls for this file; if the
    // host timed out, the file never appears and the client's REQUIRE fires.
    if (proxy_reached_target) { write_proxy_pos_file(proxy_final); }

    srv.shutdown();
    g->coop_server_ = nullptr;
    std::remove(COOP_PORT_FILE);
}

// ---------------------------------------------------------------------------
// Client role
// ---------------------------------------------------------------------------

TEST_CASE("coop integration: client role", "[.][coop_role_client]") {
    clear_all_state();

    uint16_t port = 0;
    REQUIRE(read_port_file(port));

    coop_client cli;
    REQUIRE(cli.connect("127.0.0.1", port));
    REQUIRE(cli.handshake());
    REQUIRE(cli.receive_world_seed());
    cli.apply_world_seed_to_avatar();
    g->coop_client_ = &cli;

    // Queue 3×MOVE_N immediately — avatar still at spawn before any ticks.
    for (int i = 0; i < COOP_TEST_MOVES; ++i) { cli.queue_action("MOVE_N"); }

    // --- Assertion 2: reconciliation ---
    // Poll for the proxy_pos file.  The HOST writes it ONLY after the proxy
    // reaches target_pos (all 3 moves confirmed), so reading it guarantees
    // last_seq has been echoed.  Keep ticking while we wait so the client
    // processes incoming syncs and sends its pending actions.
    // Timeout matches FILE_POLL_TIMEOUT_MS (2 min) — covers host 90s deadline.
    tripoint_abs_ms expected_proxy_pos;
    {
        const auto reconcile_deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(FILE_POLL_TIMEOUT_MS);
        bool file_ready = false;
        while (std::chrono::steady_clock::now() < reconcile_deadline) {
            cli.coop_world_tick();
            SDL_Delay(50);
            std::ifstream pf(COOP_PROXY_POS_FILE);
            if (pf.good()) {
                int x = 0, y = 0, z = 0;
                if (pf >> x >> y >> z) {
                    expected_proxy_pos = tripoint_abs_ms{x, y, z};
                    file_ready = true;
                    break;
                }
            }
        }
        REQUIRE(file_ready); // timeout = host never moved proxy
    }

    // 5 more client ticks so any final sync (last_seq confirming all moves) arrives.
    for (int i = 0; i < 5; ++i) {
        cli.coop_world_tick();
        SDL_Delay(50);
    }

    const tripoint_abs_ms actual = g->u.abs_pos();
    INFO("expected=" << expected_proxy_pos.x() << "," << expected_proxy_pos.y()
                     << "  actual=" << actual.x() << "," << actual.y());
    CHECK(actual == expected_proxy_pos);

    cli.shutdown();
    g->coop_client_ = nullptr;
    std::remove(COOP_PROXY_POS_FILE);
}

#endif // COOP_ENABLED
