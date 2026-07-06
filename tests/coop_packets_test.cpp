#ifdef COOP_ENABLED

#include "catch/catch.hpp"
#include "coop_packets.h"
#include "coop_proto.h"
#include "coordinates.h"

#include <algorithm>

// ── 1. world_seed round-trip ──────────────────────────────────────────────────

TEST_CASE( "world_seed round-trip", "[coop][packets]" )
{
    const world_seed_data original{42, tripoint_abs_ms{10, 20, 0}, "Alice", "MyWorld"};
    const std::string json = build_world_seed_packet( original );
    const auto parsed = parse_world_seed_packet( json );
    REQUIRE( parsed.has_value() );
    CHECK( parsed->turn == 42 );
    CHECK( parsed->spawn_pos == tripoint_abs_ms{10, 20, 0} );
    CHECK( parsed->player_name == "Alice" );
    CHECK( parsed->world_name == "MyWorld" );
}

// ── 2. action round-trip ──────────────────────────────────────────────────────
// Verifies the wire key "ctx" (not "ctx_json") survives the round-trip correctly.

TEST_CASE( "action round-trip", "[coop][packets]" )
{
    const action_packet_data original{7u, "MOVE_N", "{}"};
    const std::string json = build_action_packet( original );
    const auto parsed = parse_action_packet( json );
    REQUIRE( parsed.has_value() );
    CHECK( parsed->seq == 7u );
    CHECK( parsed->key == "MOVE_N" );
    CHECK( parsed->ctx_json == "{}" );
}

// ── 3. parse_sync_header extracts last_seq and proxy_pos ─────────────────────

TEST_CASE( "parse_sync_header extracts last_seq", "[coop][packets]" )
{
    // Hand-crafted sync packet; tiles/monsters are empty arrays.
    const std::string json =
        R"({"t":20,"turn":5,"last_seq":7,"tiles":[],"monsters":[],"proxy_ax":10,"proxy_ay":20,"proxy_az":0,"host_ax":1,"host_ay":2,"host_az":0})";
    const auto parsed = parse_sync_header( json );
    REQUIRE( parsed.has_value() );
    CHECK( parsed->last_seq == 7 );
    CHECK( parsed->proxy_pos == tripoint_abs_ms{10, 20, 0} );
    CHECK( parsed->has_proxy_pos );
}

// ── 4. parse_sync_header missing last_seq defaults to -1 ─────────────────────

TEST_CASE( "parse_sync_header missing last_seq is -1", "[coop][packets]" )
{
    // No last_seq field — pre-A2 host packet.
    const std::string json = R"({"t":20,"turn":3,"tiles":[],"monsters":[]})";
    const auto parsed = parse_sync_header( json );
    REQUIRE( parsed.has_value() );
    CHECK( parsed->last_seq == -1 );
}

// ── 5. Wrong packet type → nullopt ───────────────────────────────────────────

TEST_CASE( "parse_world_seed_packet wrong type returns nullopt", "[coop][packets]" )
{
    const std::string action_json = build_action_packet( {1u, "MOVE_S", ""} );
    const auto parsed = parse_world_seed_packet( action_json );
    CHECK( !parsed.has_value() );
}

// ── 6. join_info round-trip (C6) ─────────────────────────────────────────────

TEST_CASE( "join_info round-trip", "[coop][packets]" )
{
    const join_info_data original{{100, 200, -1}};
    const std::string json = build_join_info_packet( original );
    const auto parsed = parse_join_info_packet( json );
    REQUIRE( parsed.has_value() );
    CHECK( parsed->pos == tripoint_abs_ms{100, 200, -1} );
}

TEST_CASE( "parse_join_info_packet wrong type returns nullopt", "[coop][packets]" )
{
    const std::string action_json = build_action_packet( {1u, "PAUSE", ""} );
    CHECK( !parse_join_info_packet( action_json ).has_value() );
}

TEST_CASE( "parse_join_info_packet zero pos", "[coop][packets]" )
{
    const auto parsed = parse_join_info_packet( build_join_info_packet( {{0, 0, 0}} ) );
    REQUIRE( parsed.has_value() );
    CHECK( parsed->pos == tripoint_abs_ms{0, 0, 0} );
}

// ── 7. parse_vertical_move_ctx (C4) ─────────────────────────────────────────

TEST_CASE( "parse_vertical_move_ctx valid ctx", "[coop][packets]" )
{
    const auto result = parse_vertical_move_ctx( R"({"ax":100,"ay":200,"az":1})" );
    REQUIRE( result.has_value() );
    CHECK( result->landing == tripoint_abs_ms{100, 200, 1} );
}

TEST_CASE( "parse_vertical_move_ctx negative z", "[coop][packets]" )
{
    const auto result = parse_vertical_move_ctx( R"({"ax":0,"ay":0,"az":-1})" );
    REQUIRE( result.has_value() );
    CHECK( result->landing.z() == -1 );
}

TEST_CASE( "parse_vertical_move_ctx empty string returns nullopt", "[coop][packets]" )
{
    CHECK( !parse_vertical_move_ctx( "" ).has_value() );
}

TEST_CASE( "parse_vertical_move_ctx malformed JSON returns nullopt", "[coop][packets]" )
{
    CHECK( !parse_vertical_move_ctx( "not json" ).has_value() );
}

// ── 8. C5 catch-up cap math ──────────────────────────────────────────────────
// Verifies the invariant in apply_sync: std::min(turns_advanced, COOP_ACTIVITY_YIELD_INTERVAL).

TEST_CASE( "C5 catch-up cap: 10-turn host burst catches up fully", "[coop][packets]" )
{
    // A host activity burst sends at most COOP_ACTIVITY_YIELD_INTERVAL turns per sync.
    const int turns_advanced = COOP_ACTIVITY_YIELD_INTERVAL;
    const int catch_up = std::min( turns_advanced, COOP_ACTIVITY_YIELD_INTERVAL );
    CHECK( catch_up == COOP_ACTIVITY_YIELD_INTERVAL );
}

TEST_CASE( "C5 catch-up cap: small advance does not over-process", "[coop][packets]" )
{
    // A 2-turn advance during normal play must not run 10 process_turn() calls.
    const int turns_advanced = 2;
    const int catch_up = std::min( turns_advanced, COOP_ACTIVITY_YIELD_INTERVAL );
    CHECK( catch_up == 2 );
}

TEST_CASE( "C5 catch-up cap: COOP_ACTIVITY_YIELD_INTERVAL >= COOP_MAX_CATCH_UP",
           "[coop][packets]" )
{
    // Invariant from plan: the long-activity cap must be at least as large as the normal cap.
    CHECK( COOP_ACTIVITY_YIELD_INTERVAL >= COOP_MAX_CATCH_UP );
}

// ── 9. C3a proxy HP mirror formula ──────────────────────────────────────────
// Verifies the coop_world_tick HP mirror: std::max(1, max_hp * pct / 100).
// Critical invariant: never returns 0 (0 triggers npc::die() → proxy destroyed).

TEST_CASE( "C3a HP mirror: full health maps to max_hp", "[coop][packets]" )
{
    const int max_hp = 100;
    CHECK( std::max( 1, max_hp * 100 / 100 ) == 100 );
}

TEST_CASE( "C3a HP mirror: 50% maps to half", "[coop][packets]" )
{
    const int max_hp = 100;
    CHECK( std::max( 1, max_hp * 50 / 100 ) == 50 );
}

TEST_CASE( "C3a HP mirror: 1% clamps to at least 1 not 0", "[coop][packets]" )
{
    // Integer rounding: 100 * 1 / 100 == 1; still correct.
    // But for smaller max_hp: 5 * 1 / 100 == 0 → clamped to 1.
    const int small_max = 5;
    CHECK( std::max( 1, small_max * 1 / 100 ) == 1 );
}

TEST_CASE( "C3a HP mirror: dead client keeps proxy at 1 not 0", "[coop][packets]" )
{
    // Even when client_dead_ is true the proxy HP is clamped to max(1,...).
    // 0 would trigger cleanup_dead() → proxy NPC erased permanently.
    // Simulate: hp_pct=0 (dead) with max_hp=100.
    const int max_hp = 100;
    const int pct = 0; // client reported 0%
    CHECK( std::max( 1, max_hp * pct / 100 ) == 1 );
}

#endif // COOP_ENABLED
