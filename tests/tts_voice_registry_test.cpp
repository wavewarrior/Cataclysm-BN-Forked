#include "catch/catch_amalgamated.hpp"

#include "coordinates.h"
#include "npc.h"
#include "player_helpers.h"
#include "state_helpers.h"
#include "tts_voice_registry.h"
#include "type_id.h"

#include <optional>
#include <string>

// Regression coverage for the guarantee that every NPC resolves to *some* TTS
// voice (tts_voice_registry::resolve_voice() priority 4 "never fails"), and
// that an explicit per-class registry entry takes precedence over it. Fast,
// no subprocess -- unlike tts_piper_test.cpp's [.]-tagged tests, this runs in
// the default gate.

TEST_CASE( "tts_resolve_voice_falls_back_to_gender", "[tts]" )
{
    clear_all_state();
    npc &talker = spawn_npc( tripoint_bub_ms( 25, 25, 0 ), "test_talker" );
    // Defensive: tts_voice_registry is a process-wide singleton that persists
    // across TEST_CASEs -- make sure no earlier test left an entry for this
    // NPC's class (NC_DOCTOR, per data/mods/TEST_DATA/TALK_TEST.json).
    tts_voice_registry::instance().unregister_voice( talker.myclass );

    const std::optional<std::string> voice = tts_voice_registry::instance().resolve_voice( talker );
    REQUIRE( voice.has_value() );
    CHECK( *voice == ( talker.male ? "male" : "female" ) );
}

TEST_CASE( "tts_resolve_voice_prefers_explicit_registry_entry", "[tts]" )
{
    clear_all_state();
    npc &talker = spawn_npc( tripoint_bub_ms( 25, 25, 0 ), "test_talker" );
    tts_voice_registry::instance().unregister_voice( talker.myclass );

    tts_voice_registry::instance().register_voice( talker.myclass, "custom_test_voice" );
    const std::optional<std::string> overridden = tts_voice_registry::instance().resolve_voice( talker );
    REQUIRE( overridden.has_value() );
    CHECK( *overridden == "custom_test_voice" );

    tts_voice_registry::instance().unregister_voice( talker.myclass );
    const std::optional<std::string> fallback = tts_voice_registry::instance().resolve_voice( talker );
    REQUIRE( fallback.has_value() );
    CHECK( *fallback == ( talker.male ? "male" : "female" ) );
}
