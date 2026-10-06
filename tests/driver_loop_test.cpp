#include "catch/catch_amalgamated.hpp"
#include "activity_type.h"
#include "avatar.h"
#include "calendar.h"
#include "driver_loop.h"
#include "game.h"
#include "item.h"
#include "json.h"
#include "monster.h"
#include "map.h"
#include "map_helpers.h"
#include "player_activity.h"
#include "recipe.h"
#include "rng.h"
#include "state_helpers.h"
#include "type_id.h"

#include <algorithm>
#include <sys/socket.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// The activity commands over the real wire, in this process: what the Bairdford fixture cannot
// reach (a craft that succeeds, a monster that interrupts) and the turn limits. One thread plays
// the agent over a socket pair; the test thread serves the requests, since the game is not
// meant to be driven from any other.

namespace {

constexpr tripoint_bub_ms centre{60, 60, 0};

/// One response line, read by field.
struct reply {
    std::string line;

    auto object_has(const char* name) const -> bool {
        std::istringstream in(line);
        JsonIn jsin(in);
        JsonObject jo = jsin.get_object();
        jo.allow_omitted_members();
        return jo.has_member(name);
    }

    auto text(const char* name) const -> std::string {
        std::istringstream in(line);
        JsonIn jsin(in);
        JsonObject jo = jsin.get_object();
        jo.allow_omitted_members();
        return jo.has_string(name) ? jo.get_string(name) : std::string();
    }

    auto number(const char* name) const -> int {
        std::istringstream in(line);
        JsonIn jsin(in);
        JsonObject jo = jsin.get_object();
        jo.allow_omitted_members();
        return jo.has_int(name) ? jo.get_int(name) : -1;
    }

    auto flag(const char* name) const -> bool {
        std::istringstream in(line);
        JsonIn jsin(in);
        JsonObject jo = jsin.get_object();
        jo.allow_omitted_members();
        return jo.has_bool(name) && jo.get_bool(name);
    }
};

/// Serves `requests`, one line each, and returns the response to each in turn.
auto converse(const std::vector<std::string>& requests) -> std::vector<reply> {
    int fds[2];
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

    const std::filesystem::path deny = std::filesystem::temp_directory_path() / "bnplay_loop_test_deny.json";
    std::ofstream(deny) << R"({"deny": []})";

    std::vector<reply> replies;
    std::thread agent([&]() {
        std::string pending;
        for (const std::string& request : requests) {
            const std::string line = request + "\n";
            if (write(fds[1], line.data(), line.size()) != static_cast<ssize_t>(line.size())) { break; }
            while (pending.find('\n') == std::string::npos) {
                char chunk[4096];
                const ssize_t got = read(fds[1], chunk, sizeof(chunk));
                if (got <= 0) { break; }
                pending.append(chunk, static_cast<size_t>(got));
            }
            const size_t end = pending.find('\n');
            replies.push_back({pending.substr(0, end)});
            pending.erase(0, end + 1);
        }
        // Hanging up ends the loop.
        close(fds[1]);
    });
    const bool served = run_driver_loop(fds[0], deny.string());
    agent.join();
    close(fds[0]);
    std::filesystem::remove(deny);
    REQUIRE(served);
    REQUIRE(replies.size() == requests.size());
    return replies;
}

auto setup() -> avatar& {
    clear_all_state();
    map& here = get_map();
    g->place_player(centre);
    for (const tripoint_bub_ms& pos : here.points_in_radius(centre, 3)) { here.i_clear(pos); }
    avatar& u = get_avatar();
    u.moves = 100;
    set_time(calendar::turn_zero + 12_hours);
    u.recalc_sight_limits();
    return u;
}

/// A craft the avatar can start: what the recipe needs is in its hands, and it knows how.
auto equip_for(avatar& u, const char* recipe_name, std::vector<const char*> things) -> void {
    const recipe& rec = recipe_id(recipe_name).obj();
    u.learn_recipe(&rec);
    u.set_skill_level(rec.skill_used, std::max(rec.difficulty, 1));
    for (const char* thing : things) { u.i_add(item::spawn(thing)); }
    u.invalidate_crafting_inventory();
}

auto has(avatar& u, const char* type) -> bool {
    return u.has_item_with([type](const item& it) { return it.typeId() == itype_id(type); });
}

/// The monster on `at`, if one is alive there.
auto monster_at(const tripoint_bub_ms& at) -> monster* {
    monster* mon = g->critter_at<monster>(at);
    return mon && !mon->is_dead() ? mon : nullptr;
}

auto alive_at(const tripoint_bub_ms& at) -> bool { return monster_at(at) != nullptr; }

/// Its hit points; 0 once it is gone.
auto hp_of(const tripoint_bub_ms& at) -> int {
    const monster* mon = monster_at(at);
    return mon ? mon->get_hp() : 0;
}

/// Wields a shotgun, loaded or not.
auto arm_with_shotgun(avatar& u, bool loaded) -> void {
    auto gun = item::spawn(itype_id("m1014"));
    if (loaded) { gun->ammo_set(itype_id("shot_bird")); }
    u.wield(std::move(gun));
    u.moves = 100;
}

} // namespace

TEST_CASE("driver_loop_craft_runs_to_its_end_and_the_result_is_carried", "[driver]") {
    avatar& u = setup();
    equip_for(u, "pointy_stick", {"knife_combat", "stick"});

    const std::vector<reply> out = converse({R"({"id":1,"cmd":"craft","recipe":"pointy_stick"})"});
    CAPTURE(out[0].line);
    CHECK(out[0].text("status") == "ok");
    CHECK(out[0].text("outcome") == "completed");
    CHECK(out[0].number("turns") > 0);
    CHECK_FALSE(out[0].object_has("reason"));
    CHECK(has(u, "pointy_stick"));
    CHECK_FALSE(u.activity);
}

TEST_CASE("driver_loop_max_turns_stops_an_activity_early_and_leaves_none_behind", "[driver]") {
    avatar& u = setup();
    equip_for(u, "pointy_stick", {"knife_combat", "stick"});

    const std::vector<reply> out = converse({
        R"({"id":1,"cmd":"craft","recipe":"pointy_stick","max_turns":3})",
        R"({"id":2,"cmd":"wait","turns":1})",
    });
    CAPTURE(out[0].line);
    CHECK(out[0].text("outcome") == "interrupted");
    CHECK(out[0].text("reason") == "turn_cap");
    CHECK(out[0].number("turns") == 3);
    CHECK(out[0].object_has("progress"));
    CHECK_FALSE(u.activity);
    // Nothing keeps running under the next command.
    CHECK(out[1].text("outcome") == "completed");
}

TEST_CASE("driver_loop_an_activity_with_no_limit_is_cut_at_the_turn_cap", "[driver]") {
    avatar& u = setup();
    u.set_fatigue(0);

    const std::vector<reply> out = converse({R"({"id":1,"cmd":"sleep"})"});
    CAPTURE(out[0].line);
    CHECK(out[0].text("outcome") == "interrupted");
    CHECK(out[0].text("reason") == "turn_cap");
    CHECK(out[0].number("turns") == 1000);
    CHECK_FALSE(u.activity);
}

TEST_CASE("driver_loop_a_monster_coming_close_interrupts_with_its_reason", "[driver]") {
    avatar& u = setup();
    equip_for(u, "pointy_stick", {"knife_combat", "stick"});
    spawn_test_monster("mon_zombie", centre + point_east * 2);

    const std::vector<reply> out = converse({R"({"id":1,"cmd":"craft","recipe":"pointy_stick"})"});
    CAPTURE(out[0].line);
    CHECK(out[0].text("outcome") == "interrupted");
    CHECK(out[0].text("reason") == "monster_in_view");
    CHECK(out[0].number("turns") < 1000);
    CHECK_FALSE(u.activity);
}

TEST_CASE("driver_loop_a_sleep_ends_at_its_limit_with_the_avatar_awake", "[driver]") {
    avatar& u = setup();
    u.set_fatigue(1000);

    const std::vector<reply> out = converse({R"({"id":1,"cmd":"sleep","max_turns":30})"});
    CAPTURE(out[0].line);
    CHECK(out[0].text("outcome") == "interrupted");
    CHECK(out[0].text("reason") == "turn_cap");
    CHECK(out[0].number("turns") == 30);
    CHECK_FALSE(u.in_sleep_state());
    CHECK_FALSE(u.activity);
}

TEST_CASE("driver_loop_refuses_a_craft_it_cannot_start_and_an_unknown_recipe", "[driver]") {
    avatar& u = setup();
    const std::vector<reply> out = converse({
        R"({"id":1,"cmd":"craft","recipe":"pointy_stick"})",
        R"({"id":2,"cmd":"craft","recipe":"no_such_recipe"})",
        R"({"id":3,"cmd":"craft","recipe":"pointy_stick","max_turns":0})",
    });
    CHECK(out[0].text("outcome") == "refused");
    CHECK_FALSE(out[0].text("detail").empty());
    CHECK(out[1].text("status") == "error");
    CHECK(out[2].text("status") == "error");
    CHECK_FALSE(u.activity);
}

// Combat: what the Bairdford fixture cannot reach (an adjacent monster, a kill, a shot that
// lands, the avatar's death). The fixture covers the refusals and protocol errors over the
// real binary; these cover the rest in-process.

TEST_CASE("driver_loop_melee_hits_an_adjacent_monster_by_direction_or_position", "[driver]") {
    avatar& u = setup();
    rng_set_engine_seed(1);
    u.wield(item::spawn(itype_id("knife_combat")));
    u.moves = 100;
    monster& zed = spawn_test_monster("mon_zombie", centre + point_east);
    const int hp_before = zed.get_hp();

    const std::vector<reply> out = converse({
        R"({"id":1,"cmd":"melee","dir":"e"})",
        R"({"id":2,"cmd":"melee","pos":[1,0]})",
        R"({"id":3,"cmd":"melee","dir":"e"})",
    });
    CAPTURE(out[0].line);
    for (const reply& each : out) {
        CHECK(each.text("status") == "ok");
        CHECK(each.text("outcome") == "completed");
        CHECK(each.flag("time_passed"));
    }
    CHECK(hp_of(centre + point_east) < hp_before);
}

TEST_CASE("driver_loop_melee_kills_and_the_next_swing_finds_nothing", "[driver]") {
    avatar& u = setup();
    rng_set_engine_seed(1);
    u.wield(item::spawn(itype_id("knife_combat")));
    u.moves = 100;
    spawn_test_monster("mon_zombie", centre + point_east).set_hp(1);

    std::vector<std::string> swings;
    for (int i = 0; i < 12; ++i) { swings.push_back(R"({"id":1,"cmd":"melee","dir":"e"})"); }
    const std::vector<reply> out = converse(swings);

    CHECK_FALSE(alive_at(centre + point_east));
    CHECK(out.front().text("outcome") == "completed");
    CAPTURE(out.back().line);
    CHECK(out.back().text("outcome") == "refused");
    CHECK_FALSE(out.back().text("detail").empty());
    CHECK_FALSE(out.back().flag("time_passed"));
}

TEST_CASE("driver_loop_melee_at_an_empty_tile_or_an_ally_is_refused_and_costs_nothing", "[driver]") {
    avatar& u = setup();
    monster& pet = spawn_test_monster("mon_zombie", centre + point_west);
    pet.friendly = -1;
    const int turn_before = to_turn<int>(calendar::turn);

    const std::vector<reply> out = converse({
        R"({"id":1,"cmd":"melee","dir":"e"})",
        R"({"id":2,"cmd":"melee","dir":"w"})",
        R"({"id":3,"cmd":"state"})",
    });
    for (int i : {0, 1}) {
        CAPTURE(out[i].line);
        CHECK(out[i].text("status") == "ok");
        CHECK(out[i].text("outcome") == "refused");
        CHECK_FALSE(out[i].text("detail").empty());
        CHECK_FALSE(out[i].flag("time_passed"));
    }
    CHECK(out[2].number("turn") == turn_before);
    CHECK(hp_of(centre + point_west) == pet.get_hp_max());
    CHECK(u.moves > 0);
}

TEST_CASE("driver_loop_fire_shoots_along_a_direction_or_at_a_position", "[driver]") {
    avatar& u = setup();
    rng_set_engine_seed(1);
    arm_with_shotgun(u, true);
    spawn_test_monster("mon_zombie", centre + point_east * 4);
    const int hp_before = hp_of(centre + point_east * 4);
    const int ammo_before = u.primary_weapon().ammo_remaining();

    const std::vector<reply> out = converse({
        R"({"id":1,"cmd":"fire","dir":"e"})",
        R"({"id":2,"cmd":"fire","pos":[4,0]})",
    });
    for (const reply& each : out) {
        CAPTURE(each.line);
        CHECK(each.text("status") == "ok");
        CHECK(each.text("outcome") == "completed");
        CHECK(each.flag("time_passed"));
    }
    CHECK(u.primary_weapon().ammo_remaining() == ammo_before - 2);
    CHECK(hp_of(centre + point_east * 4) < hp_before);
}

TEST_CASE("driver_loop_fire_refuses_what_cannot_be_fired_and_spends_nothing", "[driver]") {
    avatar& u = setup();
    const int turn_before = to_turn<int>(calendar::turn);

    std::string request = R"({"id":1,"cmd":"fire","dir":"e"})";
    SECTION("empty hands") {}
    SECTION("a knife") {
        u.wield(item::spawn(itype_id("knife_combat")));
        u.moves = 100;
    }
    SECTION("an unloaded gun") {
        arm_with_shotgun(u, false);
        request = R"({"id":1,"cmd":"fire","pos":[3,0]})";
    }
    const std::vector<reply> out = converse({request});
    CAPTURE(out[0].line);
    CHECK(out[0].text("status") == "ok");
    CHECK(out[0].text("outcome") == "refused");
    CHECK_FALSE(out[0].text("detail").empty());
    CHECK_FALSE(out[0].flag("time_passed"));
    CHECK(to_turn<int>(calendar::turn) == turn_before);
}

TEST_CASE("driver_loop_smash_breaks_furniture_and_open_air_has_nothing_to_smash", "[driver]") {
    avatar& u = setup();
    rng_set_engine_seed(1);
    u.set_str_bonus(20);
    const tripoint_bub_ms chair = centre + point_east;
    get_map().furn_set(chair, furn_id("f_chair"));
    get_map().ter_set(centre + point_west, ter_id("t_open_air"));

    const std::vector<reply> out = converse({
        R"({"id":1,"cmd":"smash","pos":[1,0]})",
        R"({"id":2,"cmd":"smash","pos":[1,0]})",
        R"({"id":3,"cmd":"smash","dir":"e"})",
        R"({"id":4,"cmd":"smash","dir":"w"})",
    });

    CAPTURE(out[0].line);
    CHECK(out[0].text("outcome") == "completed");
    CHECK(out[0].flag("time_passed"));
    CHECK_FALSE(get_map().has_furn(chair));
    CAPTURE(out[3].line);
    CHECK(out[3].text("outcome") == "refused");
    CHECK_FALSE(out[3].text("detail").empty());
    CHECK_FALSE(out[3].flag("time_passed"));
}

TEST_CASE("driver_loop_combat_commands_reject_a_bad_target_as_a_protocol_error", "[driver]") {
    setup();
    const int turn_before = to_turn<int>(calendar::turn);
    std::vector<std::string> requests;
    for (const char* cmd : {"melee", "fire", "smash"}) {
        for (const char* target : {R"()", R"(,"dir":"sideways")", R"(,"dir":3)", R"(,"dir":"up")",
                                   R"(,"dir":"e","pos":[1,0])", R"(,"pos":[])", R"(,"pos":[1])",
                                   R"(,"pos":[1,2,3])", R"(,"pos":[1.5,0])", R"(,"pos":"e")",
                                   R"(,"pos":[0,0])", R"(,"pos":[0,"x"])"}) {
            requests.push_back(std::string(R"({"id":1,"cmd":")") + cmd + "\"" + target + "}");
        }
    }
    // Out of reach: past the adjacent tile for melee and smash, past the loaded map for fire.
    requests.push_back(R"({"id":1,"cmd":"melee","pos":[2,0]})");
    requests.push_back(R"({"id":1,"cmd":"smash","pos":[0,-2]})");
    requests.push_back(R"({"id":1,"cmd":"fire","pos":[5000,0]})");
    requests.push_back(R"({"id":1,"cmd":"melee","dir":"e","max_turns":0})");
    requests.push_back(R"({"id":1,"cmd":"state"})");

    const std::vector<reply> out = converse(requests);
    for (size_t i = 0; i + 1 < out.size(); ++i) {
        CAPTURE(requests[i], out[i].line);
        CHECK(out[i].text("status") == "error");
        CHECK_FALSE(out[i].text("error").empty());
    }
    CHECK(out.back().number("turn") == turn_before);
}

TEST_CASE("driver_loop_the_avatar_dying_ends_the_response_with_died", "[driver]") {
    avatar& u = setup();
    rng_set_engine_seed(1);
    u.set_all_parts_hp_cur(1);
    for (const tripoint_rel_ms& around : {tripoint_rel_ms(1, 0, 0), tripoint_rel_ms(-1, 0, 0),
                                           tripoint_rel_ms(0, -1, 0)}) {
        spawn_test_monster("mon_zombie", centre + around);
    }

    std::vector<std::string> swings;
    for (int i = 0; i < 20; ++i) { swings.push_back(R"({"id":1,"cmd":"melee","dir":"e"})"); }
    swings.push_back(R"({"id":1,"cmd":"state"})");
    swings.push_back(R"({"id":1,"cmd":"wait","turns":5})");
    const std::vector<reply> out = converse(swings);

    CHECK(u.is_dead_state());
    const auto first = std::ranges::find_if(out, [](const reply& r) { return r.text("outcome") == "died"; });
    REQUIRE(first != out.end());
    // Death is terminal: nothing the agent asks afterwards changes the answer, and nothing hangs.
    for (auto each = first; each != out.end(); ++each) {
        CAPTURE(each->line);
        CHECK(each->text("status") == "ok");
        CHECK(each->text("outcome") == "died");
    }
}
