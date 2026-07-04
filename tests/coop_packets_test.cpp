#ifdef COOP_ENABLED

#include "catch/catch.hpp"
#include "coop_packets.h"
#include "coordinates.h"

// ── 1. world_seed round-trip ──────────────────────────────────────────────────

TEST_CASE("world_seed round-trip", "[coop][packets]") {
    const world_seed_data original{42, tripoint_abs_ms{10, 20, 0}, "Alice", "MyWorld"};
    const std::string json = build_world_seed_packet(original);
    const auto parsed = parse_world_seed_packet(json);
    REQUIRE(parsed.has_value());
    CHECK(parsed->turn == 42);
    CHECK(parsed->spawn_pos == tripoint_abs_ms{10, 20, 0});
    CHECK(parsed->player_name == "Alice");
    CHECK(parsed->world_name == "MyWorld");
}

// ── 2. action round-trip ──────────────────────────────────────────────────────
// Verifies the wire key "ctx" (not "ctx_json") survives the round-trip correctly.

TEST_CASE("action round-trip", "[coop][packets]") {
    const action_packet_data original{7u, "MOVE_N", "{}"};
    const std::string json = build_action_packet(original);
    const auto parsed = parse_action_packet(json);
    REQUIRE(parsed.has_value());
    CHECK(parsed->seq == 7u);
    CHECK(parsed->key == "MOVE_N");
    CHECK(parsed->ctx_json == "{}");
}

// ── 3. parse_sync_header extracts last_seq and proxy_pos ─────────────────────

TEST_CASE("parse_sync_header extracts last_seq", "[coop][packets]") {
    // Hand-crafted sync packet; tiles/monsters are empty arrays.
    const std::string json =
        R"({"t":20,"turn":5,"last_seq":7,"tiles":[],"monsters":[],"proxy_ax":10,"proxy_ay":20,"proxy_az":0,"host_ax":1,"host_ay":2,"host_az":0})";
    const auto parsed = parse_sync_header(json);
    REQUIRE(parsed.has_value());
    CHECK(parsed->last_seq == 7);
    CHECK(parsed->proxy_pos == tripoint_abs_ms{10, 20, 0});
    CHECK(parsed->has_proxy_pos);
}

// ── 4. parse_sync_header missing last_seq defaults to -1 ─────────────────────

TEST_CASE("parse_sync_header missing last_seq is -1", "[coop][packets]") {
    // No last_seq field — pre-A2 host packet.
    const std::string json = R"({"t":20,"turn":3,"tiles":[],"monsters":[]})";
    const auto parsed = parse_sync_header(json);
    REQUIRE(parsed.has_value());
    CHECK(parsed->last_seq == -1);
}

// ── 5. Wrong packet type → nullopt ───────────────────────────────────────────

TEST_CASE("parse_world_seed_packet wrong type returns nullopt", "[coop][packets]") {
    const std::string action_json = build_action_packet({1u, "MOVE_S", ""});
    const auto parsed = parse_world_seed_packet(action_json);
    CHECK(!parsed.has_value());
}

#endif // COOP_ENABLED
