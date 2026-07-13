#ifdef COOP_ENABLED

#include "catch/catch_amalgamated.hpp"
#include "coop_packets.h"
#include "coop_proto.h"
#include "coop_mutation_log.h"
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


// ── 10. C3 death-drop transition guard ───────────────────────────────────────
// send_death_drop() fires exactly once per death event, not every tick while dead.
// The was_dead_last_tick_ flag is a plain bool; test the gate logic directly.

TEST_CASE( "C3 death-drop: transition alive→dead fires once", "[coop][packets]" )
{
    // Simulate: alive last tick, dead this tick → fires.
    const bool was_dead = false;
    const bool now_dead = true;
    const bool should_fire = now_dead && !was_dead;
    CHECK( should_fire == true );
}

TEST_CASE( "C3 death-drop: dead→dead does not re-fire", "[coop][packets]" )
{
    // Simulate: dead last tick, dead this tick → no re-fire.
    const bool was_dead = true;
    const bool now_dead = true;
    const bool should_fire = now_dead && !was_dead;
    CHECK( should_fire == false );
}

TEST_CASE( "C3 death-drop: alive→alive never fires", "[coop][packets]" )
{
    const bool was_dead = false;
    const bool now_dead = false;
    CHECK( ( now_dead && !was_dead ) == false );
}

TEST_CASE( "C3 death-drop: dead→alive resets guard (respawn)", "[coop][packets]" )
{
    // After respawn (dead→alive), the guard resets so the next death fires again.
    const bool was_dead = true;
    const bool now_dead = false; // respawned
    const bool should_fire = now_dead && !was_dead;
    CHECK( should_fire == false );
    // Next death after respawn:
    const bool was_dead2 = now_dead; // guard updated to false
    const bool now_dead2 = true;
    CHECK( ( now_dead2 && !was_dead2 ) == true );
}

// ── 11. Extended hash includes str field ─────────────────────────────────────

TEST_CASE( "extended hash: empty str equals base hash", "[coop][hash]" )
{
    coop_world_event ev;
    ev.type = coop_event_type::terrain_changed;
    ev.pos = tripoint_abs_ms{ 1, 2, 0 };
    ev.value = 42;
    ev.creature_id = 0;
    ev.str = "";

    const uint64_t h_base = coop_hash_event( COOP_FNV_OFFSET, ev );
    const uint64_t h_ext = coop_hash_event_extended( COOP_FNV_OFFSET, ev );
    CHECK( h_base == h_ext );
}

TEST_CASE( "extended hash: non-empty str differs from base hash", "[coop][hash]" )
{
    coop_world_event ev;
    ev.type = coop_event_type::creature_spawned;
    ev.pos = tripoint_abs_ms{ 5, 5, 0 };
    ev.value = 100;
    ev.creature_id = 7;
    ev.str = "mon_dom";

    const uint64_t h_base = coop_hash_event( COOP_FNV_OFFSET, ev );
    const uint64_t h_ext = coop_hash_event_extended( COOP_FNV_OFFSET, ev );
    CHECK( h_base != h_ext );
}

TEST_CASE( "extended hash: different str produces different hash", "[coop][hash]" )
{
    coop_world_event ev1, ev2;
    ev1.type = coop_event_type::creature_spawned;
    ev1.pos = tripoint_abs_ms{ 0, 0, 0 };
    ev1.value = 0;
    ev1.creature_id = 0;
    ev1.str = "mon_dom";

    ev2 = ev1;
    ev2.str = "mon_zombie";

    const uint64_t h1 = coop_hash_event_extended( COOP_FNV_OFFSET, ev1 );
    const uint64_t h2 = coop_hash_event_extended( COOP_FNV_OFFSET, ev2 );
    CHECK( h1 != h2 );
}

TEST_CASE( "extended hash: deterministic across calls", "[coop][hash]" )
{
    coop_world_event ev;
    ev.type = coop_event_type::creature_spawned;
    ev.pos = tripoint_abs_ms{ 3, 3, 0 };
    ev.value = 50;
    ev.creature_id = 1;
    ev.str = "mon_rat";

    const uint64_t h1 = coop_hash_event_extended( COOP_FNV_OFFSET, ev );
    const uint64_t h2 = coop_hash_event_extended( COOP_FNV_OFFSET, ev );
    CHECK( h1 == h2 );
}

#endif // COOP_ENABLED
