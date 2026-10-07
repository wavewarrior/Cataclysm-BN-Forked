#include "catch/catch_amalgamated.hpp"
#include "avatar.h"
#include "calendar.h"
#include "game.h"
#include "state_helpers.h"

// game::post_action_world_step is the world tick of the agent driver and of co-op. Like the
// single-player game::do_turn, every tick that runs moves the clock, the first one after a load
// included: monsters acting while the turn stands still is time the game never counts.

TEST_CASE("the_first_world_step_after_a_load_advances_the_turn", "[calendar]") {
    clear_all_state();
    auto& u = get_avatar();
    // What loading or starting a game leaves behind (game_save.cpp, game_setup.cpp).
    g->new_game = true;
    u.moves = 0;
    const auto turn_before = calendar::turn;

    g->post_action_world_step();

    CHECK(calendar::turn == turn_before + 1_turns);
    CHECK_FALSE(g->new_game);

    g->post_action_world_step();

    CHECK(calendar::turn == turn_before + 2_turns);
}
