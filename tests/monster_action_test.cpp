/**
 * Unit tests for the monster decide_action() / execute_action() split (Track B1).
 *
 * The two-pass architecture:
 *   - decide_action() const — pure read-only planning; safe to call from worker threads.
 *   - execute_action(const monster_action_t&) — sole mutation sink.
 *   - move() — thin shim: decide_action() + execute_action() (monmove.cpp:1655-1658).
 *
 * Tests deliberately use guard-condition states (stunned, moves<0, immobile) that are
 * fully deterministic so the idempotency checks don't race against path-selection RNG.
 */

#include "avatar.h"
#include "catch/catch_amalgamated.hpp"
#include "creature.h"
#include "game.h"
#include "map.h"
#include "map_helpers.h"
#include "monster.h"
#include "monster_action.h"
#include "mtype.h"
#include "state_helpers.h"
#include "type_id.h"

// ---------------------------------------------------------------------------
// Local effect IDs (string ids, not C++ variable names)
// ---------------------------------------------------------------------------

static const efftype_id eff_stunned("stunned");

// ---------------------------------------------------------------------------
// Observable snapshot: fields decide_action() must NOT mutate.
// (pos_abs and hp are private — use public accessors.)
// ---------------------------------------------------------------------------

struct monster_snapshot {
    tripoint_bub_ms pos;
    int moves;
    int anger;
    int morale;
    int wandf;

    explicit monster_snapshot(const monster& m)
        : pos(m.bub_pos()),
          moves(m.moves),
          anger(m.anger),
          morale(m.morale),
          wandf(m.wandf) {}

    auto operator==(const monster_snapshot& o) const -> bool {
        return pos == o.pos && moves == o.moves && anger == o.anger && morale == o.morale
            && wandf == o.wandf;
    }
};

// ---------------------------------------------------------------------------
// decide_action() — guard conditions
// ---------------------------------------------------------------------------

TEST_CASE("decide_action returns idle when moves are negative", "[monster][action][decide]") {
    clear_all_state();
    build_test_map( ter_id( "t_grass" ) );
    put_player_underground();

    const auto pos = tripoint_bub_ms{60, 60, 0};
    auto& mon = spawn_test_monster("mon_zombie", pos);
    put_player_underground();

    mon.moves = -50;

    const auto action = mon.decide_action();
    CHECK(action.kind == monster_action_kind::idle);
    CHECK(action.move_cost == 0);
}

TEST_CASE("decide_action returns stumble when monster is stunned", "[monster][action][decide]") {
    clear_all_state();
    build_test_map( ter_id( "t_grass" ) );
    put_player_underground();

    const auto pos = tripoint_bub_ms{60, 60, 0};
    auto& mon = spawn_test_monster("mon_zombie", pos);
    put_player_underground();

    mon.moves = 100;
    mon.add_effect(eff_stunned, 3_turns);

    const auto action = mon.decide_action();
    CHECK(action.kind == monster_action_kind::stumble);
    CHECK(action.move_cost == mon.moves);
}

TEST_CASE(
    "decide_action is pure — guard conditions are fully deterministic",
    "[monster][action]["
    "decide]") {
    clear_all_state();
    build_test_map( ter_id( "t_grass" ) );
    put_player_underground();

    // Use the "moves < 0" guard: no RNG, immediate return, fully deterministic.
    const auto pos = tripoint_bub_ms{60, 60, 0};
    auto& mon = spawn_test_monster("mon_zombie", pos);
    put_player_underground();

    mon.moves = -1; // triggers the negative-moves guard (decide_action:1033)
    mon.anger = 100;
    mon.set_dest(tripoint_bub_ms{65, 60, 0});

    const auto before = monster_snapshot{mon};
    const auto action1 = mon.decide_action();
    const auto mid = monster_snapshot{mon};
    const auto action2 = mon.decide_action();
    const auto after = monster_snapshot{mon};

    // State must be completely unchanged after both calls.
    CHECK(before == mid);
    CHECK(before == after);

    // Both calls must return identical results.
    CHECK(action1.kind == action2.kind);
    CHECK(action1.move_cost == action2.move_cost);
    CHECK(action1.dest == action2.dest);
}

TEST_CASE("decide_action is pure — stunned state is deterministic", "[monster][action][decide]") {
    clear_all_state();
    build_test_map( ter_id( "t_grass" ) );
    put_player_underground();

    const auto pos = tripoint_bub_ms{60, 60, 0};
    auto& mon = spawn_test_monster("mon_zombie", pos);
    put_player_underground();

    mon.moves = 100;
    mon.add_effect(eff_stunned, 10_turns); // clearly stunned for multiple ticks

    const auto before = monster_snapshot{mon};
    const auto action1 = mon.decide_action();
    const auto after1 = monster_snapshot{mon};
    const auto action2 = mon.decide_action();
    const auto after2 = monster_snapshot{mon};

    CHECK(before == after1);
    CHECK(before == after2);
    CHECK(action1.kind == action2.kind); // both stumble
    CHECK(action1.move_cost == action2.move_cost);
}

TEST_CASE(
    "decide_action returns attack when hostile creature is adjacent",
    "[monster][action]["
    "decide]") {
    clear_all_state();
    build_test_map( ter_id( "t_grass" ) );
    put_player_underground();

    const auto mon_pos = tripoint_bub_ms{60, 60, 0};
    const auto player_pos = tripoint_bub_ms{61, 60, 0};
    auto& mon = spawn_test_monster("mon_zombie", mon_pos);

    auto& player = get_avatar();
    player.setpos(player_pos);
    player.moves = 0;

    mon.moves = 100;
    mon.anger = 100;
    mon.set_dest(player_pos);
    mon.wander_pos = mon_pos;
    mon.wandf = 0;

    const auto action = mon.decide_action();
    REQUIRE(action.kind == monster_action_kind::attack);
    CHECK(action.target != nullptr);
    CHECK(action.dest == player_pos);
}

// ---------------------------------------------------------------------------
// execute_action() — mutation pass
// ---------------------------------------------------------------------------

TEST_CASE("execute_action idle consumes the specified move_cost", "[monster][action][execute]") {
    clear_all_state();
    build_test_map( ter_id( "t_grass" ) );
    put_player_underground();

    const auto pos = tripoint_bub_ms{60, 60, 0};
    auto& mon = spawn_test_monster("mon_zombie", pos);
    put_player_underground();

    mon.moves = 200;

    monster_action_t action;
    action.kind = monster_action_kind::idle;
    action.move_cost = 100;
    action.needs_stumble = false;

    mon.execute_action(action);

    CHECK(mon.moves == 100); // 200 - 100
}

TEST_CASE("execute_action idle with needs_stumble does not crash", "[monster][action][execute]") {
    clear_all_state();
    build_test_map( ter_id( "t_grass" ) );
    put_player_underground();

    const auto pos = tripoint_bub_ms{60, 60, 0};
    auto& mon = spawn_test_monster("mon_zombie", pos);
    put_player_underground();

    mon.moves = 100;

    monster_action_t action;
    action.kind = monster_action_kind::idle;
    action.move_cost = 100;
    action.needs_stumble = true;

    REQUIRE_NOTHROW(mon.execute_action(action));
}

TEST_CASE(
    "execute_action move consumes moves and changes position or stays blocked",
    "[monster]["
    "action]["
    "execute]") {
    clear_all_state();
    build_test_map( ter_id( "t_grass" ) );
    put_player_underground();

    const auto start = tripoint_bub_ms{60, 60, 0};
    const auto dest = tripoint_bub_ms{61, 60, 0};
    auto& mon = spawn_test_monster("mon_zombie", start);
    put_player_underground();

    mon.moves = 200;
    mon.anger = 100;
    mon.set_dest(dest);
    mon.wandf = 0;

    monster_action_t action;
    action.kind = monster_action_kind::move;
    action.dest = dest;
    action.stagger_adjust = 1.0f;
    action.needs_repath = false;

    const auto moves_before = mon.moves;
    mon.execute_action(action);

    // Moves must be consumed; position is dest or unchanged if terrain blocked.
    CHECK(mon.moves < moves_before);
    const auto new_pos = mon.bub_pos();
    CHECK((new_pos == dest || new_pos == start));
}

TEST_CASE("execute_action attack does not increase player HP", "[monster][action][execute]") {
    clear_all_state();
    build_test_map( ter_id( "t_grass" ) );
    put_player_underground();

    const auto mon_pos = tripoint_bub_ms{60, 60, 0};
    const auto player_pos = tripoint_bub_ms{61, 60, 0};
    auto& mon = spawn_test_monster("mon_zombie", mon_pos);

    auto& player = get_avatar();
    player.setpos(player_pos);
    player.moves = 0;
    const auto hp_before = player.get_hp();

    mon.moves = 200;
    mon.anger = 100;

    monster_action_t action;
    action.kind = monster_action_kind::attack;
    action.dest = player_pos;
    action.target = &player;

    mon.execute_action(action);

    CHECK(mon.moves < 200);
    CHECK(player.get_hp() <= hp_before);
}

// ---------------------------------------------------------------------------
// Pipeline: decide → execute
// ---------------------------------------------------------------------------

TEST_CASE(
    "decide then execute consumes moves — guard-condition path",
    "[monster][action]["
    "pipeline]") {
    clear_all_state();
    build_test_map( ter_id( "t_grass" ) );
    put_player_underground();

    const auto pos = tripoint_bub_ms{60, 60, 0};
    auto& mon = spawn_test_monster("mon_zombie", pos);
    put_player_underground();

    mon.moves = 100;

    const auto snap_before = monster_snapshot{mon};
    const auto action = mon.decide_action();

    // decide must leave state unchanged.
    CHECK(snap_before == monster_snapshot{mon});

    const auto moves_before_exec = mon.moves;
    mon.execute_action(action);

    // execute must consume moves.
    CHECK(mon.moves < moves_before_exec);
}

TEST_CASE("move() shim consumes moves and does not crash", "[monster][action][pipeline]") {
    // move() is exactly decide_action() + execute_action() (monmove.cpp:1655-1658).
    // Testing that the shim doesn't crash and is a net consumer of moves is
    // sufficient — the correctness of decide and execute are covered above.
    clear_all_state();
    build_test_map( ter_id( "t_grass" ) );
    put_player_underground();

    const auto pos = tripoint_bub_ms{60, 60, 0};
    auto& mon = spawn_test_monster("mon_zombie", pos);
    put_player_underground();

    mon.moves = 100;

    REQUIRE_NOTHROW(mon.move());
    CHECK(mon.moves < 100);
}
