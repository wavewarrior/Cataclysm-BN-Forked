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
#include <map>
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

/// Serves `requests`, one line each, and returns the response to each in turn. `scenes_dir` is
/// where `run_scene` looks for Scenes; empty selects the driver's default. `windowed` serves as
/// the windowed driver does, which in this process has no window.
auto converse(const std::vector<std::string>& requests, const std::string& scenes_dir = "",
              bool windowed = false) -> std::vector<reply> {
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
    const bool served = run_driver_loop(fds[0], {.deny_list_path = deny.string(), .scenes_dir = scenes_dir, .windowed = windowed});
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
        // Only a refusal that has a reason (a capture with no drawable) carries one.
        CHECK_FALSE(out[i].object_has("reason"));
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

/// Rows of the grid a response carries: in its `view` member when `nested`, else at top level.
/// -1 when there is none.
auto grid_rows(const reply& r, bool nested) -> int {
    std::istringstream in(r.line);
    JsonIn jsin(in);
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    if (!nested) { return jo.has_array("grid") ? static_cast<int>(jo.get_array("grid").size()) : -1; }
    if (!jo.has_object("view")) { return -1; }
    JsonObject view = jo.get_object("view");
    view.allow_omitted_members();
    return view.has_array("grid") ? static_cast<int>(view.get_array("grid").size()) : -1;
}

TEST_CASE("driver_loop_view_answers_in_no_time_and_rejects_a_bad_radius", "[driver]") {
    avatar& u = setup();
    const int turn_before = to_turn<int>(calendar::turn);
    const int moves_before = u.moves;

    const std::vector<reply> out = converse({
        R"({"id":1,"cmd":"view"})",
        R"({"id":2,"cmd":"view","radius":2})",
        R"({"id":3,"cmd":"view","radius":0})",
        R"({"id":4,"cmd":"view","radius":11})",
        R"({"id":5,"cmd":"view","radius":2.5})",
        R"({"id":6,"cmd":"view","radius":"3"})",
        R"({"id":7,"cmd":"state"})",
    });
    CHECK(out[0].text("status") == "ok");
    CHECK(out[0].text("outcome") == "completed");
    CHECK_FALSE(out[0].flag("time_passed"));
    CHECK(out[0].number("turn") == turn_before);
    CHECK(out[0].number("radius") == 5);
    CHECK(grid_rows(out[0], false) == 11);
    CHECK_FALSE(out[0].object_has("view"));
    CHECK(grid_rows(out[1], false) == 5);
    for (const size_t i : {2, 3, 4, 5}) {
        CAPTURE(i, out[i].line);
        CHECK(out[i].text("status") == "error");
        CHECK_FALSE(out[i].text("error").empty());
    }
    CHECK(out[6].number("turn") == turn_before);
    CHECK(u.moves == moves_before);
}

TEST_CASE("driver_loop_attach_view_adds_the_view_to_every_observation", "[driver]") {
    setup();
    const std::vector<reply> out = converse({
        R"({"id":1,"cmd":"state"})",
        R"({"id":2,"cmd":"attach_view","radius":-1})",
        R"({"id":3,"cmd":"attach_view","radius":11})",
        R"({"id":4,"cmd":"attach_view"})",
        R"({"id":5,"cmd":"state"})",
        R"({"id":6,"cmd":"attach_view","radius":2})",
        R"({"id":7,"cmd":"state"})",
        R"({"id":8,"cmd":"wait","turns":2})",
        R"({"id":9,"cmd":"query","topic":"inventory"})",
        R"({"id":10,"cmd":"view","radius":3})",
        R"({"id":11,"cmd":"attach_view","radius":0})",
        R"({"id":12,"cmd":"state"})",
    });
    CHECK(grid_rows(out[0], true) == -1);
    for (const size_t i : {1, 2, 3}) {
        CAPTURE(i, out[i].line);
        CHECK(out[i].text("status") == "error");
    }
    CHECK(grid_rows(out[4], true) == -1);
    CHECK(out[5].text("status") == "ok");
    CHECK(grid_rows(out[6], true) == 5);
    CHECK(grid_rows(out[6], false) == -1);
    CHECK(grid_rows(out[7], true) == 5);
    // A query's answer carries the view too, within what its own list leaves of the ceiling.
    CHECK(out[8].text("status") == "ok");
    CHECK(grid_rows(out[8], true) == 5);
    // A view asked for is the asked-for window, not the attached one nested in itself.
    CHECK(grid_rows(out[9], false) == 7);
    CHECK_FALSE(out[9].object_has("view"));
    CHECK(out[10].text("status") == "ok");
    CHECK(grid_rows(out[11], true) == -1);
}

TEST_CASE("driver_loop_attached_view_does_not_outlive_the_session", "[driver]") {
    setup();
    converse({R"({"id":1,"cmd":"attach_view","radius":3})", R"({"id":2,"cmd":"state"})"});
    const std::vector<reply> out = converse({R"({"id":1,"cmd":"state"})"});
    CHECK(grid_rows(out[0], true) == -1);
}

TEST_CASE("driver_loop_attached_view_keeps_the_response_within_the_ceiling_and_says_when_it_cut", "[driver]") {
    avatar& u = setup();
    for (int i = 0; i < 30; ++i) { get_map().add_item_or_charges(centre + tripoint_rel_ms(1 + i % 6, i / 6 - 2, 0), item::spawn("tank_gun_auto")); }
    for (int i = 0; i < 14; ++i) { spawn_test_monster("mon_zombie", centre + tripoint_rel_ms(-1 - i % 7, -3 + i / 7 * 3 + i % 2, 0)); }
    // A full inventory query is the longest answer the lean part of a response can carry.
    for (int i = 0; i < 30; ++i) { u.i_add(item::spawn("tank_gun_auto")); }
    for (int i = 0; i < 12; ++i) { get_map().add_item_or_charges(centre, item::spawn("tank_gun_auto")); }
    u.recalc_sight_limits();
    build_map_cache_from_plan(get_map(), centre.z());

    const std::vector<reply> out = converse({
        R"({"id":1,"cmd":"attach_view","radius":10})",
        R"({"id":2,"cmd":"state"})",
        R"({"id":3,"cmd":"query","topic":"inventory"})",
        R"({"id":4,"cmd":"wait","turns":3})",
    });
    for (const size_t i : {1, 2, 3}) {
        CAPTURE(i, out[i].line);
        CHECK(out[i].flag("truncated"));
        CHECK(out[i].line.size() <= 6000);
        std::istringstream in(out[i].line);
        JsonIn jsin(in);
        JsonObject jo = jsin.get_object();
        jo.allow_omitted_members();
        REQUIRE(jo.has_object("view"));
        JsonObject view = jo.get_object("view");
        view.allow_omitted_members();
        CHECK(view.get_bool("truncated", false));
        CHECK(view.get_array("grid").size() >= 3);
    }
}

namespace {

/// Scenes on disk for one test, removed with the object: `name` -> Lua source.
struct scene_dir {
    std::filesystem::path path = std::filesystem::temp_directory_path() / "bnplay_scene_test";

    explicit scene_dir(const std::map<std::string, std::string>& scenes) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
        for (const auto& [name, source] : scenes) { std::ofstream(path / (name + ".lua")) << source; }
    }
    ~scene_dir() { std::filesystem::remove_all(path); }
    scene_dir(const scene_dir&) = delete;
    auto operator=(const scene_dir&) -> scene_dir& = delete;
};

/// The `scene` member of a response.
struct scene_report {
    bool present = false;
    std::string status;
    std::vector<std::string> lines;
    bool truncated = false;
};

auto scene_of(const reply& r) -> scene_report {
    std::istringstream in(r.line);
    JsonIn jsin(in);
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    scene_report out;
    if (!jo.has_object("scene")) { return out; }
    JsonObject scene = jo.get_object("scene");
    scene.allow_omitted_members();
    out.present = true;
    out.status = scene.get_string("status", "");
    out.truncated = jo.get_bool("truncated", false);
    for (JsonValue line : scene.get_array("lines")) { out.lines.push_back(line.get_string()); }
    return out;
}

auto has_line_with(const scene_report& scene, const std::string& text) -> bool {
    return std::ranges::any_of(scene.lines, [&](const std::string& line) { return line.find(text) != std::string::npos; });
}

} // namespace

TEST_CASE("driver_loop_run_scene_reports_what_the_scene_logged_in_no_time", "[driver]") {
    setup();
    const scene_dir scenes({{"logs", "gdebug.log_info(\"LOGS_RESULT first\")\nprint(\"LOGS_RESULT second\")\nreturn true"},
        {"quiet", "return true"}});
    const int turn_before = to_turn<int>(calendar::turn);

    const std::vector<reply> out = converse({
        R"({"id":1,"cmd":"run_scene","name":"logs"})",
        R"({"id":2,"cmd":"run_scene","name":"logs"})",
        R"({"id":3,"cmd":"run_scene","name":"quiet"})",
    }, scenes.path.string());

    for (const reply& each : out) {
        CAPTURE(each.line);
        CHECK(each.text("status") == "ok");
        CHECK(each.text("outcome") == "completed");
        CHECK_FALSE(each.flag("time_passed"));
        CHECK(each.number("turn") == turn_before);
    }
    // What a Scene logs belongs to that run alone: the second run does not carry the first's lines.
    for (const size_t i : {0, 1}) {
        const scene_report scene = scene_of(out[i]);
        CHECK(scene.status == "passed");
        CHECK(scene.lines == std::vector<std::string>{"LOGS_RESULT first", "LOGS_RESULT second"});
        CHECK_FALSE(scene.truncated);
    }
    const scene_report quiet = scene_of(out[2]);
    CHECK(quiet.status == "passed");
    CHECK(quiet.lines.empty());
    // The result is its own member, not folded into the messages.
    CHECK_FALSE(out[0].line.find("LOGS_RESULT") == std::string::npos);
    CHECK(out[0].line.find("\"new_messages\":[]") != std::string::npos);
}

TEST_CASE("driver_loop_a_failing_scene_reports_failed_with_its_lines_and_the_driver_carries_on", "[driver]") {
    setup();
    const scene_dir scenes({
        {"raises", "gdebug.log_info(\"RAISES_RESULT before\")\nerror(\"scene exploded\")"},
        {"says_no", "gdebug.log_info(\"SAYS_NO_RESULT checked\")\nreturn false"},
        {"no_syntax", "this is not lua"},
    });

    const std::vector<reply> out = converse({
        R"({"id":1,"cmd":"run_scene","name":"raises"})",
        R"({"id":2,"cmd":"run_scene","name":"says_no"})",
        R"({"id":3,"cmd":"run_scene","name":"no_syntax"})",
        R"({"id":4,"cmd":"state"})",
    }, scenes.path.string());

    for (const size_t i : {0, 1, 2}) {
        CAPTURE(i, out[i].line);
        // The request itself succeeded: the failure is the Scene's result, not a protocol error.
        CHECK(out[i].text("status") == "ok");
        CHECK(scene_of(out[i]).status == "failed");
    }
    const scene_report raises = scene_of(out[0]);
    CHECK(raises.lines.front() == "RAISES_RESULT before");
    CHECK(has_line_with(raises, "scene exploded"));
    CHECK(scene_of(out[1]).lines.front() == "SAYS_NO_RESULT checked");
    CHECK_FALSE(scene_of(out[2]).lines.empty());
    // The driver still answers, and a passing Scene afterwards is unaffected by the failures.
    CHECK(out[3].text("status") == "ok");
    CHECK_FALSE(out[3].object_has("scene"));
}

TEST_CASE("driver_loop_run_scene_rejects_a_bad_or_unknown_name_as_a_protocol_error", "[driver]") {
    setup();
    const scene_dir scenes({{"real", "return true"}});
    const int turn_before = to_turn<int>(calendar::turn);

    const std::vector<reply> out = converse({
        R"({"id":1,"cmd":"run_scene"})",
        R"({"id":2,"cmd":"run_scene","name":3})",
        R"({"id":3,"cmd":"run_scene","name":""})",
        R"({"id":4,"cmd":"run_scene","name":"no_such_scene"})",
        R"({"id":5,"cmd":"run_scene","name":"../real"})",
        R"({"id":6,"cmd":"run_scene","name":"real.lua"})",
        R"({"id":7,"cmd":"run_scene","name":"a/b"})",
        R"({"id":8,"cmd":"state"})",
    }, scenes.path.string());

    for (const size_t i : {0, 1, 2, 3, 4, 5, 6}) {
        CAPTURE(i, out[i].line);
        CHECK(out[i].text("status") == "error");
        CHECK_FALSE(out[i].text("error").empty());
        CHECK_FALSE(out[i].object_has("scene"));
    }
    CHECK(out[7].number("turn") == turn_before);
}

TEST_CASE("driver_loop_run_scene_cuts_a_long_result_and_says_so", "[driver]") {
    setup();
    const scene_dir scenes({{"chatty",
        "for i = 1, 200 do gdebug.log_info(\"CHATTY_RESULT line \" .. i .. string.rep(\"x\", 400)) end\nreturn true"}});

    const std::vector<reply> out = converse({
        R"({"id":1,"cmd":"run_scene","name":"chatty"})",
    }, scenes.path.string());

    const scene_report scene = scene_of(out[0]);
    CHECK(scene.status == "passed");
    CHECK(scene.truncated);
    CHECK(!scene.lines.empty());
    CHECK(out[0].line.size() <= 6000);
    // The newest lines are the ones kept: a Scene reports its summary last.
    CHECK(has_line_with(scene, "line 200"));
}

TEST_CASE("driver_loop_the_lighting_scenes_run_unchanged_through_run_scene", "[driver]") {
    setup();
    const tripoint_bub_ms light = centre + tripoint_rel_ms(12, 0, 0);

    const std::vector<reply> out = converse({
        R"({"id":1,"cmd":"run_scene","name":"lightone"})",
        R"({"id":2,"cmd":"run_scene","name":"lightmobs"})",
        R"({"id":3,"cmd":"run_scene","name":"lightscene"})",
        R"({"id":4,"cmd":"run_scene","name":"shadowtest"})",
    }, "tools/visual_verify/scenes");

    const std::vector<std::string> tags{"LIGHTONE_RESULT", "LIGHTMOBS_RESULT", "LIGHTSCENE_RESULT", "SHADOWTEST_RESULT"};
    for (size_t i = 0; i < out.size(); ++i) {
        CAPTURE(i, out[i].line);
        CHECK(out[i].text("status") == "ok");
        const scene_report scene = scene_of(out[i]);
        CHECK(scene.status == "passed");
        CHECK(has_line_with(scene, tags[i]));
    }
    // The Scenes did their work in the world, not just in the log.
    CHECK(!get_map().i_at(light).empty());
}

// `capture` needs a window and the display session it opens in, neither of which a test process
// has: what is checked here is what an agent sees when the window gives no frame. The frames
// themselves are checked against the real windowed binary by tools/bnplay/capture_contract.ts.

TEST_CASE("driver_loop_capture_without_a_window_is_a_protocol_error_that_names_the_windowed_mode", "[driver]") {
    setup();
    const int turn_before = to_turn<int>(calendar::turn);
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "bnplay_capture_test_windowless";
    std::filesystem::remove_all(dir);

    const std::vector<reply> out = converse({
        "{\"id\":1,\"cmd\":\"capture\",\"dir\":\"" + dir.string() + "\"}",
        "{\"id\":2,\"cmd\":\"capture\",\"dir\":\"" + dir.string() + "\",\"mode\":\"state\"}",
        R"({"id":3,"cmd":"state"})",
    });

    for (const size_t i : {0, 1}) {
        CAPTURE(i, out[i].line);
        CHECK(out[i].text("status") == "error");
        CHECK(out[i].text("error").find("windowed") != std::string::npos);
        CHECK_FALSE(out[i].object_has("capture"));
    }
    CHECK(out[2].number("turn") == turn_before);
    CHECK_FALSE(std::filesystem::exists(dir));
}

TEST_CASE("driver_loop_capture_rejects_a_bad_dir_or_mode_as_a_protocol_error", "[driver]") {
    setup();
    const int turn_before = to_turn<int>(calendar::turn);

    const std::vector<reply> out = converse({
        R"({"id":1,"cmd":"capture"})",
        R"({"id":2,"cmd":"capture","dir":""})",
        R"({"id":3,"cmd":"capture","dir":7})",
        R"({"id":4,"cmd":"capture","dir":"relative/dir"})",
        R"({"id":5,"cmd":"capture","dir":"/tmp/bnplay_capture_test_bad","mode":"lighting"})",
        R"({"id":6,"cmd":"capture","dir":"/tmp/bnplay_capture_test_bad","mode":3})",
        R"({"id":7,"cmd":"state"})",
    }, "", true);

    for (size_t i = 0; i < 6; ++i) {
        CAPTURE(i, out[i].line);
        CHECK(out[i].text("status") == "error");
        CHECK_FALSE(out[i].text("error").empty());
        CHECK_FALSE(out[i].object_has("capture"));
    }
    CHECK(out[6].number("turn") == turn_before);
    CHECK_FALSE(std::filesystem::exists("/tmp/bnplay_capture_test_bad"));
}

TEST_CASE("driver_loop_capture_with_no_drawable_is_refused_and_never_returns_an_old_frame", "[driver]") {
    avatar& u = setup();
    const int turn_before = to_turn<int>(calendar::turn);
    const int moves_before = u.moves;
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "bnplay_capture_test_refused";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    // A frame left from an earlier capture, under the name the first capture of this turn would take.
    const std::filesystem::path old_frame = dir / ("turn-" + std::to_string(turn_before) + "-1-final.bmp");
    std::ofstream(old_frame) << "an old frame";

    const std::vector<reply> out = converse({
        "{\"id\":1,\"cmd\":\"capture\",\"dir\":\"" + dir.string() + "\"}",
        "{\"id\":2,\"cmd\":\"capture\",\"dir\":\"" + dir.string() + "\",\"mode\":\"state\"}",
    }, "", true);

    for (const reply& each : out) {
        CAPTURE(each.line);
        CHECK(each.text("status") == "ok");
        CHECK(each.text("outcome") == "refused");
        CHECK(each.text("reason") == "no_drawable");
        CHECK_FALSE(each.flag("time_passed"));
        CHECK(each.number("turn") == turn_before);
        CHECK_FALSE(each.object_has("capture"));
    }
    CHECK(u.moves == moves_before);
    // Nothing was written, and the old frame is neither reported nor touched.
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) { names.push_back(entry.path().filename().string()); }
    CHECK(names == std::vector<std::string>{old_frame.filename().string()});
    std::ifstream in(old_frame);
    std::string content;
    std::getline(in, content);
    CHECK(content == "an old frame");
    std::filesystem::remove_all(dir);
}

// `set_time` pins the world clock the way the Trial's `start_date` and `time_of_day` ask: the date
// is "YYYY-SS-DD" (the game has no months: year from 1, season 01 spring to 04 winter, day of the
// season from 01), the time is "HH:MM".

TEST_CASE("driver_loop_set_time_pins_the_date_and_the_time_of_day", "[driver]") {
    setup();
    const auto season = to_turns<int>(calendar::season_length());
    const auto year = to_turns<int>(calendar::year_length());
    const auto expected = 2 * year + season + 9 * 86400 + 8 * 3600 + 30 * 60;

    const auto out = converse({
        R"({"id":1,"cmd":"set_time","date":"0003-02-10","time":"08:30"})",
        R"({"id":2,"cmd":"state"})",
    });

    CHECK(out[0].text("status") == "ok");
    CHECK(out[0].number("turn") == expected);
    CHECK(out[0].text("date") == "0003-02-10");
    CHECK(out[0].text("time") == "08:30");
    CHECK(out[1].number("turn") == expected);
    CHECK(to_turn<int>(calendar::turn) == expected);
}

TEST_CASE("driver_loop_set_time_with_one_field_keeps_the_other", "[driver]") {
    setup();
    // setup() puts the clock at noon on the first day.
    const auto out = converse({
        R"({"id":1,"cmd":"set_time","time":"06:15"})",
        R"({"id":2,"cmd":"set_time","date":"0001-01-03"})",
    });

    CHECK(out[0].text("status") == "ok");
    CHECK(out[0].number("turn") == 6 * 3600 + 15 * 60);
    CHECK(out[0].text("date") == "0001-01-01");
    CHECK(out[1].text("status") == "ok");
    CHECK(out[1].number("turn") == 2 * 86400 + 6 * 3600 + 15 * 60);
    CHECK(out[1].text("time") == "06:15");
}

TEST_CASE("driver_loop_set_time_refuses_impossible_values_and_leaves_the_clock_alone", "[driver]") {
    setup();
    const auto before = to_turn<int>(calendar::turn);
    const auto days = std::to_string(to_days<int>(calendar::season_length()) + 1);
    const auto bad = std::vector<std::string>{
        R"({"id":1,"cmd":"set_time"})",
        R"({"id":2,"cmd":"set_time","date":"0001-05-01"})",
        R"({"id":3,"cmd":"set_time","date":"0001-00-01"})",
        R"({"id":4,"cmd":"set_time","date":"0000-01-01"})",
        R"({"id":5,"cmd":"set_time","date":"0001-01-00"})",
        "{\"id\":6,\"cmd\":\"set_time\",\"date\":\"0001-01-" + days + "\"}",
        R"({"id":7,"cmd":"set_time","date":"yesterday"})",
        R"({"id":8,"cmd":"set_time","time":"24:00"})",
        R"({"id":9,"cmd":"set_time","time":"12:60"})",
        R"({"id":10,"cmd":"set_time","time":"noon"})",
        R"({"id":11,"cmd":"set_time","time":1200})",
        R"({"id":12,"cmd":"set_time","date":"9999-04-91"})",
    };

    const auto out = converse(bad);

    for (auto i = size_t{0}; i < out.size(); ++i) {
        CAPTURE(bad[i], out[i].line);
        CHECK(out[i].text("status") == "error");
        CHECK_FALSE(out[i].text("error").empty());
    }
    CHECK(to_turn<int>(calendar::turn) == before);
}
