#include "catch/catch_amalgamated.hpp"
#include "avatar.h"
#include "game.h"
#include "map.h"
#include "map_helpers.h"
#include "state_helpers.h"
#include "type_id.h"

// Walking into a t_door_locked through game::handle_action_from, the entry point of the agent
// driver's `move` and of the co-op main loop. The door's OPENCLOSE_INSIDE flag decides: from
// outside it blocks the avatar and costs nothing; from inside (under a roof) the avatar unlocks
// the deadbolt and opens it, and stays put until its next step.

namespace {

constexpr tripoint_bub_ms centre{60, 60, 0};
constexpr tripoint_bub_ms door{61, 60, 0};

auto setup_door() -> avatar& {
    clear_all_state();
    // A started or loaded game always has one, and the action handler calls into it.
    REQUIRE(g->gamemode);
    auto& here = get_map();
    g->place_player(centre);
    here.ter_set(door, ter_id("t_door_locked"));
    auto& u = get_avatar();
    u.moves = 100;
    return u;
}

} // namespace

TEST_CASE("locked_door_blocks_a_move_from_outside", "[movement][door]") {
    auto& u = setup_door();
    auto& here = get_map();
    rebuild_level_cache(0);
    REQUIRE(here.is_outside(centre));

    g->handle_action_from("move_e");

    CHECK(u.bub_pos() == centre);
    CHECK(here.ter(door) == ter_id("t_door_locked"));
    CHECK(u.moves == 100);

    // The lock is what stopped it: the same step into an unlocked door opens that one.
    here.ter_set(door, ter_id("t_door_c"));
    g->handle_action_from("move_e");

    CHECK(u.bub_pos() == centre);
    CHECK(here.ter(door) == ter_id("t_door_o"));
    CHECK(u.moves == 0);
}

TEST_CASE("locked_door_opens_from_inside_and_the_next_move_walks_through", "[movement][door]") {
    auto& u = setup_door();
    auto& here = get_map();
    for (const tripoint_bub_ms& pos : here.points_in_radius(centre, 3)) { here.ter_set(pos + tripoint_above, ter_id("t_flat_roof")); }
    rebuild_level_cache(0);
    REQUIRE_FALSE(here.is_outside(centre));

    g->handle_action_from("move_e");

    CHECK(u.bub_pos() == centre);
    CHECK(here.ter(door) == ter_id("t_door_o"));
    CHECK(u.moves == 0);

    u.moves = 100;
    g->handle_action_from("move_e");

    CHECK(u.bub_pos() == door);
}
