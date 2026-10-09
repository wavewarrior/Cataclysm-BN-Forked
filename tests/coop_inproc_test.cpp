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
#include "coop_checksum.h"
#include "coop_client.h"
#include "coop_server.h"
#include "coop_session.h"
#include "coop_sim_transport.h"
#include "field.h"
#include "game.h"
#include "item.h"
#include "json.h"
#include "map.h"
#include "map_helpers.h"
#include "npc.h"
#include "state_helpers.h"
#include "type_id.h"

#include <sstream>

namespace {

/// RAII guard: sets coop_session::mode on construction, restores on destruction.
/// Guarantees the singleton is never left in a stale mode after a test failure.
struct coop_mode_guard {
    coop_mode saved;
    explicit coop_mode_guard(coop_mode m): saved(coop_session::get().mode) {
        coop_session::get().mode = m;
    }
    ~coop_mode_guard() { coop_session::get().mode = saved; }
    coop_mode_guard(const coop_mode_guard&) = delete;
    auto operator=(const coop_mode_guard&) -> coop_mode_guard& = delete;
};

/// In-process co-op test harness.
///
/// Session stays in `host` mode throughout; RAII guards briefly flip to
/// `client` for methods that check coop_session::is_client().
struct inproc_harness {
    coop_server srv;
    coop_client cli;
    coop_sim_transport* srv_tx = nullptr; // raw ptr; owned by srv via transport_
    coop_sim_transport* cli_tx = nullptr; // raw ptr; owned by cli via transport_
    npc* proxy = nullptr;

    /// Set up world, wire transports, run the full join sequence.
    auto setup() -> void {
        clear_all_state();
        build_test_map(ter_id("t_grass"));

        auto& sess = coop_session::get();
        sess.mode = coop_mode::host;
        sess.partner_name = "TestClient";
        sess.dimension_id = g->get_current_dimension_id().str();

        // Create and wire sim transports (zero latency, no loss).
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

        // World-seed exchange — server sends directly through transport.
        REQUIRE(srv.send_world_seed("TestClient"));
        {
            coop_mode_guard mcli(coop_mode::client);
            INFO("cli_tx inbox_size="
                 << cli_tx->inbox_size() << " srv_tx inbox_size=" << srv_tx->inbox_size());
            REQUIRE(cli.receive_world_seed());
            REQUIRE(cli.send_join_info());
        }
        REQUIRE(srv.wait_for_join_info());

        const auto spawn_pos = srv.client_join_pos().value_or(g->u.abs_pos());
        proxy = srv.spawn_proxy_npc(spawn_pos, "TestClient");
        REQUIRE(proxy != nullptr);

        // send_initial_sync drains send_q_ internally and sends via transport.
        REQUIRE(srv.send_initial_sync());

        // Client receives the initial sync.
        {
            coop_mode_guard mcli(coop_mode::client);
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
            coop_mode_guard mcli(coop_mode::client);
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
// Join & connectivity
// ---------------------------------------------------------------------------

TEST_CASE("inproc: join sequence completes without crash", "[coop][inproc]") {
    inproc_harness h;
    h.setup();
    CHECK(h.proxy != nullptr);
    CHECK(h.proxy->is_coop_remote);
}

TEST_CASE("inproc: initial sync delivers tiles to client", "[coop][inproc]") {
    inproc_harness h;
    h.setup();
    // got_full_tile_sync is set during apply_sync when a non-empty tiles array
    // is present — the initial sync always includes the 5×5 submap grid.
    CHECK(h.cli.got_full_tile_sync_for_test());
}

// ---------------------------------------------------------------------------
// Movement relay
// ---------------------------------------------------------------------------

TEST_CASE("inproc: MOVE_N relays to proxy", "[coop][inproc]") {
    inproc_harness h;
    h.setup();

    const auto start = h.proxy->abs_pos();

    h.cli.queue_action("MOVE_N");
    h.tick();

    const auto end = h.proxy->abs_pos();
    CHECK(end.y() == start.y() - 1);
    CHECK(end.x() == start.x());
}

TEST_CASE("inproc: three consecutive MOVE_N relay sequentially", "[coop][inproc]") {
    inproc_harness h;
    h.setup();

    const auto start = h.proxy->abs_pos();

    h.cli.queue_action("MOVE_N");
    h.cli.queue_action("MOVE_N");
    h.cli.queue_action("MOVE_N");

    h.tick();
    h.tick();
    h.tick();

    const auto end = h.proxy->abs_pos();
    CHECK(end.y() == start.y() - 3);
    CHECK(end.x() == start.x());
}

TEST_CASE("inproc: all four cardinal directions relay correctly", "[coop][inproc]") {
    inproc_harness h;
    h.setup();

    const auto start = h.proxy->abs_pos();

    // N (+0,-1)
    h.cli.queue_action("MOVE_N");
    h.tick();
    CHECK(h.proxy->abs_pos() == tripoint_abs_ms{start.x(), start.y() - 1, start.z()});

    // E (+1,0)
    h.cli.queue_action("MOVE_E");
    h.tick();
    CHECK(h.proxy->abs_pos() == tripoint_abs_ms{start.x() + 1, start.y() - 1, start.z()});

    // S (0,+1)
    h.cli.queue_action("MOVE_S");
    h.tick();
    CHECK(h.proxy->abs_pos() == tripoint_abs_ms{start.x() + 1, start.y(), start.z()});

    // W (-1,0) — back to start
    h.cli.queue_action("MOVE_W");
    h.tick();
    CHECK(h.proxy->abs_pos() == start);
}

// ---------------------------------------------------------------------------
// Status sync
// ---------------------------------------------------------------------------

TEST_CASE("inproc: client_status reaches server each tick", "[coop][inproc]") {
    inproc_harness h;
    h.setup();

    // Run one tick — client sends client_status automatically in coop_world_tick.
    h.tick();

    // Server processes client_status in process_incoming_for_test and sets
    // client_hp_pct_.  The test player is alive, so hp_pct > 0.
    CHECK(h.srv.client_hp_pct() > 0);
}

// ---------------------------------------------------------------------------
// Force resync
// ---------------------------------------------------------------------------

TEST_CASE("inproc: force_resync triggers full tile sync", "[coop][inproc]") {
    inproc_harness h;
    h.setup();

    h.srv.set_force_resync_for_test();
    CHECK(h.srv.force_resync_pending_for_test());

    h.tick();

    // The server consumed the force_resync flag during build_and_send_sync.
    CHECK_FALSE(h.srv.force_resync_pending_for_test());
}

// ---------------------------------------------------------------------------
// Idle / fast-forward
// ---------------------------------------------------------------------------

TEST_CASE("inproc: both_idle false when neither is idle", "[coop][inproc]") {
    inproc_harness h;
    h.setup();

    // Neither host nor client is sleeping/crafting.
    h.tick();
    CHECK_FALSE(h.srv.both_idle());
}

// ---------------------------------------------------------------------------
// Multi-tick stability
// ---------------------------------------------------------------------------

TEST_CASE("inproc: 20 ticks without crash or assertion failure", "[coop][inproc]") {
    inproc_harness h;
    h.setup();

    // Intersperse movement with idle ticks.
    for (int i = 0; i < 20; ++i) {
        if (i % 3 == 0) { h.cli.queue_action("MOVE_N"); }
        h.tick();
    }
    // Just surviving 20 ticks with interleaved movement is the assertion.
    CHECK(h.proxy != nullptr);
}

// ---------------------------------------------------------------------------
// World-state checksum (Step 2)
// ---------------------------------------------------------------------------

TEST_CASE("inproc: checksum is stable across idle ticks", "[coop][inproc][checksum]") {
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
    CHECK(cs0 == cs1);
}

TEST_CASE("inproc: checksum changes after movement", "[coop][inproc][checksum]") {
    inproc_harness h;
    h.setup();

    const auto before = coop_world_checksum();

    h.cli.queue_action("MOVE_N");
    h.tick();

    const auto after = coop_world_checksum();

    // Proxy moved — position component of the hash changed.
    CHECK(before != after);
}

TEST_CASE("inproc: checksum converges after 10 ticks of movement", "[coop][inproc][checksum]") {
    inproc_harness h;
    h.setup();

    // 5 moves, then 5 idle ticks.
    for (int i = 0; i < 5; ++i) {
        h.cli.queue_action("MOVE_N");
        h.tick();
    }
    for (int i = 0; i < 5; ++i) { h.tick(); }

    // Two consecutive idle-tick checksums must match.
    const auto cs_a = coop_world_checksum();
    h.tick();
    const auto cs_b = coop_world_checksum();
    CHECK(cs_a == cs_b);
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
TEST_CASE("inproc: client turn step runs avatar biology", "[coop][inproc][parity]") {
    constexpr int turns = 400;

    inproc_harness h;
    h.setup();
    coop_mode_guard mcli(coop_mode::client);

    // Control: the old client behaviour.
    const int kcal_control_before = g->u.get_stored_kcal();
    for (int i = 0; i < turns; ++i) {
        calendar::turn += 1_turns;
        g->u.process_turn();
    }
    CHECK(g->u.get_stored_kcal() == kcal_control_before);

    // New behaviour: metabolism runs, so stored calories are billed.
    const int kcal_before = g->u.get_stored_kcal();
    const time_point turn_before = calendar::turn;
    for (int i = 0; i < turns; ++i) {
        calendar::turn += 1_turns;
        g->coop_client_turn_step();
    }
    CHECK(calendar::turn > turn_before);
    CHECK(g->u.get_stored_kcal() != kcal_before);
}

// The per-frame half must be safe to call unconditionally, including when a sync carried
// no advanced turns — that is how the client's vision cache and monster info stay fresh.
TEST_CASE("inproc: client frame step is safe with no turns advanced", "[coop][inproc][parity]") {
    inproc_harness h;
    h.setup();
    coop_mode_guard mcli(coop_mode::client);

    const time_point turn_before = calendar::turn;
    for (int i = 0; i < 5; ++i) { g->coop_client_frame_step(); }
    // Purely local caches/UI/audio: no world time may pass.
    CHECK(calendar::turn == turn_before);
}

// End-to-end: the new per-turn and per-frame calls in apply_sync() must not break the
// relay, and world time must still advance across a long run.
TEST_CASE("inproc: turn advances across a long synced run", "[coop][inproc][parity]") {
    inproc_harness h;
    h.setup();

    const int turn_before = to_turn<int>(calendar::turn);
    for (int i = 0; i < 60; ++i) { h.tick(); }
    CHECK(to_turn<int>(calendar::turn) > turn_before);
    CHECK(h.proxy != nullptr);
}

// ---------------------------------------------------------------------------
// A4 event-array wire round-trip (commit 1 of plans/coop-world-event-interpreter.md)
// ---------------------------------------------------------------------------
//
// Real serialiser, real wire, real parser: the events go into a
// coop_tick_log_guard the same way map_field.cpp / submap.cpp push them, are
// serialised by the production coop_server::build_and_send_sync (public,
// force_full=false so the DELTA path is taken), cross a coop_sim_transport, and
// are parsed and applied by the production coop_client::apply_sync.
//
// build_and_send_sync is called directly rather than through coop_world_tick()
// because coop_world_tick installs its OWN coop_tick_log_guard
// (coop_server.cpp:520), which would replace a log installed by the test.
//
// Server and client share one g->m in-process, so a map change observed after
// the client tick is the client's own apply of the wire event (pushing into the
// log does not touch the map).  Hash agreement is asserted indirectly: the
// client sends resync_request ONLY on a hash mismatch, and the server answers it
// by raising force_resync_.
//
// Field events are built the way map_field.cpp builds them: designated
// initialisers with the intensity in `.creature_id` (new intensity for
// created/changed, pre-expiry intensity for expired), which the wire carries as
// "cid" and the client reads back.
TEST_CASE("inproc: all five replicated world events survive the wire", "[coop][inproc][wire]") {
    constexpr tripoint_bub_ms TER_TILE{10, 10, 0};
    constexpr tripoint_bub_ms FURN_TILE{12, 10, 0};
    constexpr tripoint_bub_ms FIELD_TILE{14, 10, 0};
    constexpr tripoint_bub_ms EXPIRE_TILE{16, 10, 0};

    inproc_harness h;
    h.setup();
    clear_fields(0);

    REQUIRE(get_map().ter(TER_TILE) == ter_str_id("t_grass"));
    REQUIRE(get_map().furn(FURN_TILE) == furn_str_id("f_null"));
    REQUIRE(get_map().get_field(FIELD_TILE).find_field(field_type_id("fd_fire")) == nullptr);
    get_map().add_field(EXPIRE_TILE, field_type_id("fd_fire"), 1, 0_turns);
    REQUIRE(get_map().get_field(EXPIRE_TILE).find_field(field_type_id("fd_fire")) != nullptr);

    const auto abs_of = [](const tripoint_bub_ms& p) { return map_local_to_abs(get_map(), p); };

    {
        coop_tick_log_guard guard;
        // terrain / furniture: submap.cpp:610 / :623 push {type, abs, id}.
        guard.log().push(
            {coop_event_type::terrain_changed, abs_of(TER_TILE), ter_id("t_floor").to_i()});
        guard.log().push(
            {coop_event_type::furniture_changed, abs_of(FURN_TILE), furn_id("f_locker").to_i()});
        // Fields: map_field.cpp pushes intensity in `.creature_id`.
        guard.log().push(
            {.type = coop_event_type::field_created,
             .pos = abs_of(FIELD_TILE),
             .value = field_type_id("fd_fire").to_i(),
             .creature_id = 2});
        guard.log().push(
            {.type = coop_event_type::field_changed,
             .pos = abs_of(FIELD_TILE),
             .value = field_type_id("fd_fire").to_i(),
             .creature_id = 3});
        // The expiry carries the pre-expiry intensity.
        guard.log().push(
            {.type = coop_event_type::field_expired,
             .pos = abs_of(EXPIRE_TILE),
             .value = field_type_id("fd_fire").to_i(),
             .creature_id = 1});

        // Delta path: force_full=false, origin unchanged since the initial sync,
        // and the 30-tick periodic resync cannot fire this early.
        h.srv.build_and_send_sync();
    }
    h.srv.flush_send_queue_for_test();

    {
        coop_mode_guard mcli(coop_mode::client);
        h.cli.coop_world_tick();
    }

    // The client applied the terrain and furniture deltas.
    CHECK(get_map().ter(TER_TILE) == ter_str_id("t_floor"));
    CHECK(get_map().furn(FURN_TILE) == furn_str_id("f_locker"));
    // The field arrives at the host's intensity: created at 2, then changed to 3
    // (fd_fire tops out at 3, so 3 is also not the old lowest-intensity default).
    field_entry* created = get_map().get_field(FIELD_TILE).find_field(field_type_id("fd_fire"));
    REQUIRE(created != nullptr);
    CHECK(created->get_field_intensity() == 3);
    // The expiry landed: the field on EXPIRE_TILE is gone.
    CHECK(get_map().get_field(EXPIRE_TILE).find_field(field_type_id("fd_fire")) == nullptr);

    // Hash parity, checked on the wire: the client sends resync_request ({"t":25})
    // ONLY when its replica of the 6-field FNV chain disagrees with the server's,
    // so scanning the host inbox for that packet proves the event array hashed
    // identically on both ends.  client_status ({"t":13}) is expected traffic.
    bool resync_requested_on_wire = false;
    for (std::string frame; h.srv_tx->recv(frame, 0);) {
        if (frame.find(R"("t":25)") != std::string::npos) { resync_requested_on_wire = true; }
    }
    CHECK_FALSE(resync_requested_on_wire);
    // Belt and braces: dispatch whatever the client sent and confirm the server
    // did not end up armed for a forced full sync.
    h.srv.process_incoming_for_test();
    CHECK_FALSE(h.srv.force_resync_pending_for_test());
}

namespace {

/// A serialised knife, built as handle_action.cpp builds the trade_offer item_json.
auto knife_offer_json() -> std::string {
    const itype_id knife_id("knife_combat");
    REQUIRE(knife_id.is_valid());
    auto knife = item::spawn(knife_id, calendar::turn, item::solitary_tag{});
    REQUIRE(knife);
    std::ostringstream oss;
    JsonOut jout(oss);
    knife->serialize(jout);
    return oss.str();
}

/// Deliver one host sync to the client through the real wire and apply_sync.
auto sync_host_to_client(inproc_harness& h) -> void {
    {
        coop_tick_log_guard guard;
        h.srv.build_and_send_sync();
    }
    h.srv.flush_send_queue_for_test();
    coop_mode_guard mcli(coop_mode::client);
    h.cli.coop_world_tick();
}

} // namespace

// F2: the giver removes the item when offering, so a declined offer must come back.
// Host and client share one g->u in-process, so the knife count is the observable.
TEST_CASE("inproc: a declined trade offer returns the item exactly once", "[coop][inproc][trade]") {
    const itype_id knife_id("knife_combat");
    inproc_harness h;
    h.setup();
    const std::string offer = knife_offer_json();
    const int before = g->u.amount_of(knife_id);

    h.srv.resolve_trade_offer(offer, false);
    CHECK(g->u.amount_of(knife_id) == before);

    sync_host_to_client(h);
    CHECK(g->u.amount_of(knife_id) == before + 1);

    // One-shot: the next sync must not hand it back again.
    sync_host_to_client(h);
    CHECK(g->u.amount_of(knife_id) == before + 1);
}

TEST_CASE(
    "inproc: an accepted trade offer is kept and never echoed back", "[coop][inproc][trade]") {
    const itype_id knife_id("knife_combat");
    inproc_harness h;
    h.setup();
    const std::string offer = knife_offer_json();
    const int before = g->u.amount_of(knife_id);

    h.srv.resolve_trade_offer(offer, true);
    CHECK(g->u.amount_of(knife_id) == before + 1);

    sync_host_to_client(h);
    CHECK(g->u.amount_of(knife_id) == before + 1);
}
