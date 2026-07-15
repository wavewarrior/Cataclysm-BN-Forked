#include "catch/catch_amalgamated.hpp"

#include "action.h"
#include "avatar.h"
#include "character.h"
#include "enum_conversions.h"
#include "game.h"
#include "player_helpers.h"
#include "sdl_lighting_devui.h"
#include "state_helpers.h"

TEST_CASE( "stealth_mode_mirrors_crouch_posture", "[stealth][movemode]" )
{
    clear_all_state();
    avatar &dummy = g->u;
    clear_character( dummy, false );

    CHECK( io::enum_to_string( CMM_STEALTH ) == "stealth" );

    dummy.set_movement_mode( CMM_CROUCH );
    REQUIRE( dummy.movement_mode_is( CMM_CROUCH ) );
    CHECK( dummy.is_crouching() );
    const float crouch_run_cost = dummy.running_move_cost_modifier();

    dummy.set_movement_mode( CMM_WALK );
    dummy.set_movement_mode( CMM_STEALTH );
    REQUIRE( dummy.movement_mode_is( CMM_STEALTH ) );
    CHECK( dummy.is_crouching() );
    CHECK( dummy.running_move_cost_modifier() == crouch_run_cost );
}

TEST_CASE( "sound_pulses_visible_gated_by_stealth_or_devui", "[stealth][sound]" )
{
    CHECK_FALSE( sdl_lighting_devui::sound_pulses_visible( false ) );
    CHECK( sdl_lighting_devui::sound_pulses_visible( true ) );

    sdl_lighting_devui::devui_visible() = true;
    CHECK( sdl_lighting_devui::sound_pulses_visible( false ) );
    sdl_lighting_devui::devui_visible() = false;
    CHECK_FALSE( sdl_lighting_devui::sound_pulses_visible( false ) );
}
