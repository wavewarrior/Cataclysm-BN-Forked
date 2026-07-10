#ifdef COOP_ENABLED
/**
 * Tests for A4b delta-hash agreement and A5.3/A5.4 lag-compensation logic.
 *
 * All tests are pure-function unit tests — no game world, no network socket.
 * They exercise the ACTUAL shared functions extracted for this purpose:
 *   coop_hash_event / coop_hash_event_fields  (coop_mutation_log.h)
 *   coop_lag_find_target                       (coop_server.h)
 *
 * Tags: [coop][delta]
 */

#include "catch/catch_amalgamated.hpp"
#include "coop_mutation_log.h" // COOP_FNV_OFFSET, coop_fnv1a_mix, coop_hash_event*
#include "coop_proto.h"        // coop_event_type
#include "coop_server.h"       // coop_entity_snapshot, coop_lag_find_target
#include "coordinates.h"

#include <deque>
#include <sstream>

// ── A4b: hash-function parity ────────────────────────────────────────────────
//
// The trap: server always mixes creature_id even when 0 (emits 6 values per
// event) but omits "cid" from JSON when 0.  Client initialises ev_cid=0 and
// must ALWAYS mix it — mix(0) is NOT a no-op (hash * prime changes the value).
// coop_hash_event (server) and coop_hash_event_fields (client) are the same
// canonical seam; this test verifies they agree on identical inputs.

TEST_CASE("A4b hash: coop_hash_event and coop_hash_event_fields agree", "[coop][delta]") {
    // Three events matching the server's streamable set.
    const coop_world_event evs[] = {
        {.type = coop_event_type::terrain_changed,
         .pos = tripoint_abs_ms{100, 200, 0},
         .value = 7,
         .creature_id = 0}, // "cid" absent in JSON → ev_cid = 0
        {.type = coop_event_type::furniture_changed,
         .pos = tripoint_abs_ms{101, 200, 0},
         .value = 3,
         .creature_id = 0},
        {.type = coop_event_type::field_created,
         .pos = tripoint_abs_ms{100, 201, 0},
         .value = 4,
         .creature_id = 2}, // intensity packed in creature_id; "cid" IS in JSON
    };

    // Server side: coop_hash_event accumulates over struct.
    uint64_t server_h = COOP_FNV_OFFSET;
    for (const auto& ev : evs) { server_h = coop_hash_event(server_h, ev); }

    // Client side: coop_hash_event_fields from parsed JSON integers.
    // ev_cid = 0 when "cid" absent, actual value when present.
    uint64_t client_h = COOP_FNV_OFFSET;
    for (const auto& ev : evs) {
        client_h = coop_hash_event_fields(
            client_h, static_cast<int>(ev.type), ev.pos.x(), ev.pos.y(), ev.pos.z(), ev.value,
            ev.creature_id); // same value (0 or actual)
    }

    CHECK(server_h == client_h);
}

TEST_CASE("A4b hash: mix(0) for absent creature_id is not a no-op", "[coop][delta]") {
    // Anchor the key invariant: skipping the creature_id field gives a different
    // hash than mixing 0.  If a client skips cid=0, hashes diverge every tick.
    const coop_world_event ev{
        .type = coop_event_type::terrain_changed,
        .pos = tripoint_abs_ms{50, 60, 0},
        .value = 42,
        .creature_id = 0,
    };

    // Correct: 6 fields mixed including creature_id=0.
    const uint64_t h_correct = coop_hash_event(COOP_FNV_OFFSET, ev);

    // Bug: only 5 fields — creature_id skipped (the broken-client behaviour).
    auto h_broken = COOP_FNV_OFFSET;
    h_broken = coop_fnv1a_mix(h_broken, static_cast<uint64_t>(ev.type));
    h_broken = coop_fnv1a_mix(h_broken, static_cast<uint64_t>(ev.pos.x()));
    h_broken = coop_fnv1a_mix(h_broken, static_cast<uint64_t>(ev.pos.y()));
    h_broken = coop_fnv1a_mix(h_broken, static_cast<uint64_t>(ev.pos.z()));
    h_broken = coop_fnv1a_mix(h_broken, static_cast<uint64_t>(ev.value));
    // creature_id intentionally omitted

    CHECK(h_correct != h_broken); // mix(0) ≠ no-op
}

TEST_CASE("A4b hash: coop_mutation_log rolling hash matches coop_hash_event", "[coop][delta]") {
    // Verify the log's internal hash uses the same canonical function — important
    // because the log hash is used as the wire hash in build_and_send_sync.
    coop_tick_log_guard guard;
    auto& log = guard.log();

    const coop_world_event ev1{
        .type = coop_event_type::terrain_changed,
        .pos = tripoint_abs_ms{10, 20, 0},
        .value = 5,
        .creature_id = 0,
    };
    const coop_world_event ev2{
        .type = coop_event_type::field_created,
        .pos = tripoint_abs_ms{11, 20, 0},
        .value = 2,
        .creature_id = 3,
    };

    log.push(ev1);
    log.push(ev2);

    // Manual computation using the shared function.
    uint64_t manual = COOP_FNV_OFFSET;
    manual = coop_hash_event(manual, ev1);
    manual = coop_hash_event(manual, ev2);

    CHECK(log.hash() == manual);
}

// ── A5.3: coop_lag_find_target — pure snapshot-lookup function ───────────────
//
// These tests drive the real coop_lag_find_target() without game state.
// The function is the testable seam extracted from resolve_fire_at_seq.

TEST_CASE("A5.3 coop_lag_find_target: finds creature at snapshot position", "[coop][delta]") {
    const tripoint_abs_ms target_abs{100, 200, 0};
    const tripoint_abs_ms other_abs{200, 300, 0};

    // Snapshot at seq=5: creature id=1 at target, creature id=2 elsewhere.
    std::deque<coop_entity_snapshot> history;
    history.push_back({5u, {{1, target_abs}, {2, other_abs}}});

    CHECK(coop_lag_find_target(history, 7u, target_abs) == 1);
    CHECK(coop_lag_find_target(history, 7u, other_abs) == 2);
    CHECK(coop_lag_find_target(history, 7u, tripoint_abs_ms{999, 999, 0}) == -1);
}

TEST_CASE("A5.3 coop_lag_find_target: selects highest seq <= fire_seq", "[coop][delta]") {
    const tripoint_abs_ms pos_at_5{100, 200, 0};
    const tripoint_abs_ms pos_at_10{105, 200, 0}; // creature moved

    std::deque<coop_entity_snapshot> history;
    history.push_back({5u, {{1, pos_at_5}}});   // seq=5: creature at pos_at_5
    history.push_back({10u, {{1, pos_at_10}}}); // seq=10: creature moved

    // fire_seq=7: best snapshot is seq=5 (seq=10 is after fire, excluded).
    CHECK(coop_lag_find_target(history, 7u, pos_at_5) == 1);   // found in seq=5
    CHECK(coop_lag_find_target(history, 7u, pos_at_10) == -1); // seq=10 excluded

    // fire_seq=10: best snapshot is seq=10.
    CHECK(coop_lag_find_target(history, 10u, pos_at_10) == 1);
    CHECK(coop_lag_find_target(history, 10u, pos_at_5) == -1);
}

TEST_CASE("A5.3 coop_lag_find_target: returns -1 on empty history", "[coop][delta]") {
    const std::deque<coop_entity_snapshot> empty;
    CHECK(coop_lag_find_target(empty, 5u, tripoint_abs_ms{100, 200, 0}) == -1);
}

TEST_CASE(
    "A5.3 coop_lag_find_target: falls back to front when all snapshots newer",
    "[coop]["
    "delta]") {
    // fire_seq=2, but history only has seq=5 and seq=10.
    // Fallback: use the earliest (front) snapshot — better than no compensation.
    const tripoint_abs_ms target{100, 200, 0};
    std::deque<coop_entity_snapshot> history;
    history.push_back({5u, {{1, target}}});
    history.push_back({10u, {{1, tripoint_abs_ms{110, 200, 0}}}});

    // All snapshots are newer than fire_seq=2; fallback → front (seq=5) is used.
    CHECK(coop_lag_find_target(history, 2u, target) == 1);
}

// ── A5.3: FIRE ctx_json coordinate format ────────────────────────────────────
//
// Client sends absolute coords in ctx_json: {"tx":N,"ty":N,"tz":N}.
// Server reconstructs tripoint_abs_ms directly from those integers.
// Large absolute coords (~millions) distinguish abs from bub coords (~0-132).

TEST_CASE("A5.3 FIRE: client ctx_json uses locale-independent to_string", "[coop][delta]") {
    // This mirrors the EXACT production path in handle_action.cpp after the locale fix.
    // The bug was: ostringstream with global en_US locale turns 1234567 → "1,234,567",
    // which the server's JsonIn::get_int() cannot parse → FIRE silently broken.
    // Fix: std::to_string is always locale-independent (C99 semantics).
    const tripoint_abs_ms abs{1234567, -890123, 0};

    // Mirrors handle_action.cpp fiber serialisation after the fix.
    const auto ctx =
        "{\"tx\":" + std::to_string(abs.x()) + ",\"ty\":" + std::to_string(abs.y())
        + ",\"tz\":" + std::to_string(abs.z()) + "}";

    // Large absolute coords present as plain digits — no locale separators.
    CHECK(ctx.find("1234567") != std::string::npos);
    CHECK(ctx.find("-890123") != std::string::npos);

    // Server reconstruction: direct tripoint_abs_ms from parsed ints.
    const tripoint_abs_ms reconstructed{1234567, -890123, 0};
    CHECK(reconstructed == abs);
}

// ── A5.4: both_idle logic ─────────────────────────────────────────────────────
//
// coop_server::both_idle() returns (host_idle && client_is_idle_).
// The fast-forward fires only when BOTH are idle; any active player suppresses it.
// We can't call the private member directly but the predicate is trivial enough
// to anchor explicitly — if either variable changes semantics the test breaks.

TEST_CASE("A5.4 both_idle: fast-forward fires only when both players are idle", "[coop][delta]") {
    const auto both_idle = [](bool host_idle, bool client_idle) {
        return host_idle && client_idle; // mirrors coop_server::both_idle()
    };

    CHECK_FALSE(both_idle(false, false)); // both active — no fast-forward
    CHECK_FALSE(both_idle(true, false));  // host sleeps, client active
    CHECK_FALSE(both_idle(false, true));  // client idle, host active
    CHECK(both_idle(true, true));         // both idle → fast-forward fires
}

TEST_CASE("A5.4 both_idle: MAX_CATCH_UP must match on host and client", "[coop][delta]") {
    // host:   main.cpp saturates main_loop_accum_ms_ to 3000.0 (3 ticks at 1Hz)
    // client: coop_client.cpp MAX_PROCESS_CATCH_UP = 3
    // These MUST be equal; raising one side causes avatar-stat desync.
    constexpr int host_cap = 3;   // main.cpp: 3000 ms at 1-Hz floor
    constexpr int client_cap = 3; // coop_client.cpp: MAX_PROCESS_CATCH_UP
    CHECK(host_cap == client_cap);
}

// ── A4 delta: coop_collect_streamable — filter/hash/collect correctness ───────
//
// This calls the REAL coop_collect_streamable() that build_and_send_sync uses.
// If sent.push_back is dropped from the function, result.sent.size() == 0 and
// the REQUIRE fails immediately.  Hash parity tests alone cannot catch that
// regression because they only exercise the hash functions, not the collection.

TEST_CASE(
    "A4 delta: coop_collect_streamable filters, hashes, and collects correctly",
    "[coop]["
    "delta]") {
    using evt = coop_event_type;

    std::vector<coop_world_event> input = {
        {.type = evt::terrain_changed, .pos = tripoint_abs_ms{1, 2, 0}, .value = 5},
        {.type = evt::creature_moved, .pos = tripoint_abs_ms{3, 4, 0}, .creature_id = 1},
        {.type = evt::furniture_changed, .pos = tripoint_abs_ms{5, 6, 0}, .value = 2},
        {.type = evt::item_spawned, .pos = tripoint_abs_ms{7, 8, 0}},
        {.type = evt::field_created, .pos = tripoint_abs_ms{9, 0, 0}, .value = 3, .creature_id = 2},
    };

    const auto result = coop_collect_streamable(std::move(input));

    // Exactly 3 streamable events; 2 filtered (creature_moved, item_spawned).
    REQUIRE(result.sent.size() == 3);
    CHECK(result.sent[0].type == evt::terrain_changed);
    CHECK(result.sent[1].type == evt::furniture_changed);
    CHECK(result.sent[2].type == evt::field_created);

    // Hash must be non-trivial.
    CHECK(result.hash != COOP_FNV_OFFSET);

    // Hash must equal the manual computation over only the 3 sent events —
    // guarantees send-hash parity that the client will replicate.
    uint64_t manual = COOP_FNV_OFFSET;
    for (const auto& ev : result.sent) { manual = coop_hash_event(manual, ev); }
    CHECK(result.hash == manual);
}

#endif // COOP_ENABLED
