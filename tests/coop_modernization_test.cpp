
#include "catch/catch_amalgamated.hpp"
#include "coop_overmap.h"
#include "coop_packets.h"
#include "coop_proto.h"
#include "json.h"

#include <sstream>
#include <string>
#include <vector>

// ── 1. Overmap sync packet build/parse round-trip ────────────────────────────

TEST_CASE( "overmap_sync packet: build serializes tiles correctly", "[coop][overmap]" )
{
    const std::vector<tripoint_abs_omt> tiles{
        tripoint_abs_omt{ 10, 20, 0 },
        tripoint_abs_omt{ -5, 100, -1 },
        tripoint_abs_omt{ 0, 0, 3 },
    };

    const std::string json = build_overmap_sync_packet( tiles );

    // Parse and verify structure
    std::istringstream iss( json );
    JsonIn jin( iss );
    JsonObject pkt = jin.get_object();
    pkt.allow_omitted_members();

    CHECK( pkt.get_int( "t" ) == static_cast<int>( coop_pkt::overmap_sync ) );
    REQUIRE( pkt.has_array( "tiles" ) );

    JsonArray arr = pkt.get_array( "tiles" );
    int count = 0;
    while( arr.has_more() ) {
        JsonArray tp = arr.next_array();
        REQUIRE( tp.size() == 3 );
        if( count == 0 ) {
            CHECK( tp.get_int( 0 ) == 10 );
            CHECK( tp.get_int( 1 ) == 20 );
            CHECK( tp.get_int( 2 ) == 0 );
        } else if( count == 1 ) {
            CHECK( tp.get_int( 0 ) == -5 );
            CHECK( tp.get_int( 1 ) == 100 );
            CHECK( tp.get_int( 2 ) == -1 );
        } else if( count == 2 ) {
            CHECK( tp.get_int( 0 ) == 0 );
            CHECK( tp.get_int( 1 ) == 0 );
            CHECK( tp.get_int( 2 ) == 3 );
        }
        ++count;
    }
    CHECK( count == 3 );
}

TEST_CASE( "overmap_sync packet: empty tile list produces valid JSON", "[coop][overmap]" )
{
    const std::vector<tripoint_abs_omt> tiles{};
    const std::string json = build_overmap_sync_packet( tiles );

    std::istringstream iss( json );
    JsonIn jin( iss );
    JsonObject pkt = jin.get_object();
    pkt.allow_omitted_members();

    CHECK( pkt.get_int( "t" ) == static_cast<int>( coop_pkt::overmap_sync ) );
    REQUIRE( pkt.has_array( "tiles" ) );
    JsonArray arr = pkt.get_array( "tiles" );
    CHECK_FALSE( arr.has_more() );
}

// ── 2. Skill sync serialization round-trip ──────────────────────────────────

TEST_CASE( "skill sync: build and parse round-trip", "[coop][packets]" )
{
    const std::vector<std::pair<std::string, int>> skills{
        { "melee", 5 },
        { "dodge", 3 },
        { "cooking", 8 },
    };

    // Build: serialize into a wrapper object
    std::ostringstream oss;
    JsonOut jout( oss );
    jout.start_object();
    build_skill_sync_fields( jout, skills );
    jout.end_object();

    // Parse
    std::istringstream iss( oss.str() );
    JsonIn jin( iss );
    JsonObject obj = jin.get_object();
    obj.allow_omitted_members();
    const auto parsed = parse_skill_sync_fields( obj );

    REQUIRE( parsed.size() == 3 );
    CHECK( parsed[0].first == "melee" );
    CHECK( parsed[0].second == 5 );
    CHECK( parsed[1].first == "dodge" );
    CHECK( parsed[1].second == 3 );
    CHECK( parsed[2].first == "cooking" );
    CHECK( parsed[2].second == 8 );
}

TEST_CASE( "skill sync: empty skills list round-trips", "[coop][packets]" )
{
    const std::vector<std::pair<std::string, int>> empty{};

    std::ostringstream oss;
    JsonOut jout( oss );
    jout.start_object();
    build_skill_sync_fields( jout, empty );
    jout.end_object();

    std::istringstream iss( oss.str() );
    JsonIn jin( iss );
    JsonObject obj = jin.get_object();
    obj.allow_omitted_members();
    const auto parsed = parse_skill_sync_fields( obj );

    CHECK( parsed.empty() );
}

TEST_CASE( "skill sync: missing skills key returns empty", "[coop][packets]" )
{
    // Object with no "skills" member
    std::istringstream iss( R"({"other": 42})" );
    JsonIn jin( iss );
    JsonObject obj = jin.get_object();
    obj.allow_omitted_members();
    const auto parsed = parse_skill_sync_fields( obj );

    CHECK( parsed.empty() );
}

// ── 3. Session token in world_seed round-trip ───────────────────────────────

TEST_CASE( "world_seed: session_token survives round-trip", "[coop][packets]" )
{
    const world_seed_data original{
        42,
        tripoint_abs_ms{ 10, 20, 0 },
        "Alice",
        "MyWorld",
        12345u,
        "1750000000-42"
    };
    const std::string json = build_world_seed_packet( original );
    const auto parsed = parse_world_seed_packet( json );

    REQUIRE( parsed.has_value() );
    CHECK( parsed->session_token == "1750000000-42" );
    // Other fields still intact
    CHECK( parsed->turn == 42 );
    CHECK( parsed->player_name == "Alice" );
}

TEST_CASE( "world_seed: empty session_token defaults correctly", "[coop][packets]" )
{
    const world_seed_data original{ 1, tripoint_abs_ms{}, "Bob", "W2", 0u, "" };
    const std::string json = build_world_seed_packet( original );
    const auto parsed = parse_world_seed_packet( json );

    REQUIRE( parsed.has_value() );
    CHECK( parsed->session_token.empty() );
}

// ── 4. Reconnect packet type exists ─────────────────────────────────────────

TEST_CASE( "coop_pkt::reconnect has correct value", "[coop][packets]" )
{
    CHECK( static_cast<uint8_t>( coop_pkt::reconnect ) == 15 );
}

// ── 5. Overmap sync build→parse round-trip ──────────────────────────────────

TEST_CASE( "overmap_sync: build then parse round-trip", "[coop][overmap]" )
{
    const std::vector<tripoint_abs_omt> original{
        tripoint_abs_omt{ 42, -7, 0 },
        tripoint_abs_omt{ 0, 0, -3 },
    };
    const std::string json = build_overmap_sync_packet( original );
    const auto parsed = parse_overmap_sync_tiles( json );

    REQUIRE( parsed.size() == 2 );
    CHECK( parsed[0] == tripoint_abs_omt{ 42, -7, 0 } );
    CHECK( parsed[1] == tripoint_abs_omt{ 0, 0, -3 } );
}

TEST_CASE( "overmap_sync: parse empty packet returns empty", "[coop][overmap]" )
{
    const std::string json = build_overmap_sync_packet( {} );
    const auto parsed = parse_overmap_sync_tiles( json );
    CHECK( parsed.empty() );
}

// ── 6. Mutation sync serialization round-trip ───────────────────────────────

TEST_CASE( "mutation sync: array round-trip via JSON", "[coop][packets]" )
{
    // Simulate what the client sends in client_status
    std::ostringstream oss;
    JsonOut jout( oss );
    jout.start_object();
    jout.member( "mutations" );
    jout.start_array();
    jout.write( "THICK_SKIN" );
    jout.write( "NIGHTVISION" );
    jout.write( "FLEET" );
    jout.end_array();
    jout.end_object();

    // Parse like the server does
    std::istringstream iss( oss.str() );
    JsonIn jin( iss );
    JsonObject d = jin.get_object();
    d.allow_omitted_members();

    REQUIRE( d.has_array( "mutations" ) );
    std::vector<std::string> parsed;
    for( const std::string &s : d.get_array( "mutations" ) ) {
        parsed.push_back( s );
    }
    REQUIRE( parsed.size() == 3 );
    CHECK( parsed[0] == "THICK_SKIN" );
    CHECK( parsed[1] == "NIGHTVISION" );
    CHECK( parsed[2] == "FLEET" );
}

TEST_CASE( "mutation sync: empty array parses cleanly", "[coop][packets]" )
{
    std::istringstream iss( R"({"other": 1})" );
    JsonIn jin( iss );
    JsonObject d = jin.get_object();
    d.allow_omitted_members();
    CHECK_FALSE( d.has_array( "mutations" ) );
}

// ── 7. Bionic sync serialization round-trip ─────────────────────────────────

TEST_CASE( "bionic sync: array round-trip via JSON", "[coop][packets]" )
{
    std::ostringstream oss;
    JsonOut jout( oss );
    jout.start_object();
    jout.member( "bionics" );
    jout.start_array();
    jout.write( "bio_power_storage" );
    jout.write( "bio_solar" );
    jout.end_array();
    jout.end_object();

    std::istringstream iss( oss.str() );
    JsonIn jin( iss );
    JsonObject d = jin.get_object();
    d.allow_omitted_members();

    REQUIRE( d.has_array( "bionics" ) );
    std::vector<std::string> parsed;
    for( const std::string &s : d.get_array( "bionics" ) ) {
        parsed.push_back( s );
    }
    REQUIRE( parsed.size() == 2 );
    CHECK( parsed[0] == "bio_power_storage" );
    CHECK( parsed[1] == "bio_solar" );
}

TEST_CASE( "bionic sync: empty array parses cleanly", "[coop][packets]" )
{
    std::istringstream iss( R"({"other": 1})" );
    JsonIn jin( iss );
    JsonObject d = jin.get_object();
    d.allow_omitted_members();
    CHECK_FALSE( d.has_array( "bionics" ) );
}

