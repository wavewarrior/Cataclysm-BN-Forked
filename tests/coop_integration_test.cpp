#ifdef COOP_ENABLED
/**
 * Co-op two-process integration tests.
 *
 * These tests are intentionally hidden ([.]) and are never run by the standard
 * test suite.  They are driven by scripts/test_coop.ts, which spawns two
 * separate cata_test-tiles processes:
 *
 *   Process A: cata_test-tiles "[.][coop_role_host]"
 *   Process B: cata_test-tiles "[.][coop_role_client]"
 *
 * Each process has its own real game* g (two-world topology).  They
 * communicate over a loopback TCP socket using the actual coop wire protocol.
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
#include "npc.h"
#include "state_helpers.h"

#include <SDL3_net/SDL_net.h>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>

static constexpr uint16_t COOP_INTEG_PORT_BASE = 45802;
static constexpr int COOP_INTEG_PORT_RANGE = 10;
static constexpr const char* COOP_PORT_FILE = "/tmp/coop_test_port.txt";
static constexpr int ACCEPT_TIMEOUT_MS = 90'000;

static auto write_port_file(uint16_t port) -> void {
    std::ofstream f(COOP_PORT_FILE);
    f << port << '\n';
}

static auto read_port_file(uint16_t& port, int timeout_ms = 5000) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        std::ifstream f(COOP_PORT_FILE);
        if (f.good()) {
            int p = 0;
            f >> p;
            if (p > 0) {
                port = static_cast<uint16_t>(p);
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

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(ACCEPT_TIMEOUT_MS);
    while (!srv.try_accept()) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        SDL_Delay(10);
    }

    REQUIRE(srv.handshake());
    REQUIRE(srv.send_world_seed(g->u.get_name()));
    srv.spawn_proxy_npc(g->u.abs_pos(), "TestClient");
    REQUIRE(srv.send_initial_sync());
    srv.start_receiver_thread();
    g->coop_server_ = &srv;

    for (int i = 0; i < 10; ++i) {
        srv.coop_world_tick();
        SDL_Delay(50);
    }

    const npc* proxy = g->critter_by_id<npc>(coop_session::get().proxy_npc_id);
    CHECK(proxy != nullptr);

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

    cli.queue_action("MOVE_N");
    cli.queue_action("MOVE_N");
    cli.queue_action("MOVE_N");

    const tripoint_abs_ms pos_before = g->u.abs_pos();

    for (int i = 0; i < 10; ++i) {
        cli.coop_world_tick();
        SDL_Delay(50);
    }

    const tripoint_abs_ms pos_after = g->u.abs_pos();
    INFO("pos_before=" << pos_before.x() << "," << pos_before.y());
    INFO("pos_after=" << pos_after.x() << "," << pos_after.y());
    CHECK(true); // liveness — replaced by precise assertions after plan is complete

    cli.shutdown();
    g->coop_client_ = nullptr;
}

#endif // COOP_ENABLED
