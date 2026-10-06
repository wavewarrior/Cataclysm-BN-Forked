#include "catch/catch_amalgamated.hpp"
#include "activity_type.h"
#include "avatar.h"
#include "calendar.h"
#include "driver_loop.h"
#include "game.h"
#include "item.h"
#include "json.h"
#include "map.h"
#include "map_helpers.h"
#include "player_activity.h"
#include "recipe.h"
#include "state_helpers.h"
#include "type_id.h"

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
