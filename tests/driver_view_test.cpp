#include "catch/catch_amalgamated.hpp"
#include "avatar.h"
#include "calendar.h"
#include "driver_items.h"
#include "driver_view.h"
#include "game.h"
#include "item.h"
#include "json.h"
#include "map.h"
#include "map_helpers.h"
#include "mapdata.h"
#include "monster.h"
#include "state_helpers.h"
#include "type_id.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// What the real-binary contract cannot reach on the Bairdford fixture: terrain classes, creatures
// and items in view, what is out of sight, and lists long enough to be cut.

namespace {

constexpr tripoint_bub_ms centre{60, 60, 0};

/// The largest a view may be: with the lean response's own worst case it stays within the ceiling.
constexpr size_t view_ceiling_bytes = 3000;

struct listed {
    std::string id;
    std::string name;
    int dx = 0;
    int dy = 0;
    bool hostile = false;
};

/// What `write_view` wrote, read back.
struct seen {
    bool cut = false;
    size_t bytes = 0;
    int radius = 0;
    std::vector<std::string> grid;
    std::map<std::string, std::string> legend;
    std::vector<listed> creatures;
    std::vector<listed> items;

    auto at(int dx, int dy) const -> char { return grid[radius + dy][radius + dx]; }

    auto item_at(int dx, int dy) const -> const listed* {
        for (const listed& it : items) {
            if (it.dx == dx && it.dy == dy) { return &it; }
        }
        return nullptr;
    }

    auto creature_at(int dx, int dy) const -> const listed* {
        for (const listed& c : creatures) {
            if (c.dx == dx && c.dy == dy) { return &c; }
        }
        return nullptr;
    }
};

auto read_list(const JsonObject& jo, const char* name) -> std::vector<listed> {
    std::vector<listed> out;
    for (JsonObject entry : jo.get_array(name)) {
        entry.allow_omitted_members();
        out.push_back({entry.get_string("id"), entry.get_string("name"), entry.get_int("dx"),
                       entry.get_int("dy"), entry.get_bool("hostile", false)});
    }
    return out;
}

auto view_of(int radius, size_t room = driver_view::unlimited) -> seen {
    std::ostringstream os;
    JsonOut jo(os, false);
    jo.start_object();
    seen out;
    out.cut = driver_view::write_view(jo, radius, room);
    jo.end_object();
    out.bytes = os.str().size();

    std::istringstream in(os.str());
    JsonIn jsin(in);
    JsonObject view = jsin.get_object();
    view.allow_omitted_members();
    out.radius = view.get_int("radius");
    out.grid = view.get_string_array("grid");
    JsonObject legend = view.get_object("legend");
    for (const JsonMember member : legend) { out.legend[member.name()] = member.get_string(); }
    out.creatures = read_list(view, "creatures");
    out.items = read_list(view, "items");
    return out;
}

/// An open, daylit floor around the avatar, free of items and creatures.
auto setup() -> avatar& {
    clear_all_state();
    map& here = get_map();
    g->place_player(centre);
    for (const tripoint_bub_ms& pos : here.points_in_radius(centre, 12)) {
        here.i_clear(pos);
        here.ter_set(pos, ter_id("t_floor"));
        here.furn_set(pos, furn_id("f_null"));
    }
    avatar& u = get_avatar();
    u.moves = 100;
    set_time(calendar::turn_zero + 12_hours);
    u.recalc_sight_limits();
    return u;
}

/// Lets the vision and light caches see what the test put on the map.
auto refresh() -> void {
    get_avatar().recalc_sight_limits();
    build_map_cache_from_plan(get_map(), centre.z());
}

auto put_terrain(int dx, int dy, const char* type) -> void {
    REQUIRE(get_map().ter_set(centre + tripoint_rel_ms(dx, dy, 0), ter_id(type)));
}

auto put_item(int dx, int dy, const char* type) -> item& {
    const tripoint_bub_ms pos = centre + tripoint_rel_ms(dx, dy, 0);
    get_map().add_item_or_charges(pos, item::spawn(type));
    item* placed = nullptr;
    for (item* it : get_map().i_at(pos)) {
        if (it->typeId() == itype_id(type)) { placed = it; }
    }
    REQUIRE(placed != nullptr);
    return *placed;
}

} // namespace

TEST_CASE("driver_view_grid_is_a_window_centred_on_the_avatar_with_a_legend", "[driver]") {
    setup();
    refresh();
    const int moves_before = get_avatar().moves;
    const int turn_before = to_turn<int>(calendar::turn);

    for (const int radius : {1, 3, 10}) {
        CAPTURE(radius);
        const seen view = view_of(radius);
        CHECK(view.radius == radius);
        REQUIRE(view.grid.size() == static_cast<size_t>(2 * radius + 1));
        for (const std::string& row : view.grid) { CHECK(row.size() == static_cast<size_t>(2 * radius + 1)); }
        CHECK(view.at(0, 0) == '@');
        for (const std::string& row : view.grid) {
            for (const char symbol : row) { CHECK(view.legend.contains(std::string(1, symbol))); }
        }
        CHECK(view.legend.at("@") == "you");
        CHECK(view.legend.at(".") == "ground or floor");
        CHECK_FALSE(view.cut);
    }
    // Reading the world spends nothing.
    CHECK(get_avatar().moves == moves_before);
    CHECK(to_turn<int>(calendar::turn) == turn_before);
}

TEST_CASE("driver_view_grid_draws_terrain_classes_and_hides_what_is_out_of_sight", "[driver]") {
    setup();
    put_terrain(1, 0, "t_wall");
    put_terrain(0, 1, "t_door_c");
    put_terrain(-1, 0, "t_stairs_up");
    put_terrain(0, -2, "t_water_dp");
    put_terrain(-2, -1, "t_water_sh");
    put_terrain(-2, 2, "t_tree");
    refresh();

    const seen view = view_of(4);
    CHECK(view.at(1, 0) == '#');
    CHECK(view.legend.at("#") == "wall or solid obstacle");
    CHECK(view.at(0, 1) == '+');
    CHECK(view.legend.at("+") == "closed door or window");
    CHECK(view.at(-1, 0) == '<');
    CHECK(view.legend.at("<") == "stairs up");
    CHECK(view.at(0, -2) == 'W');
    CHECK(view.at(-2, -1) == '~');
    CHECK(view.at(-2, 2) == 'T');
    CHECK(view.at(2, 2) == '.');
    // A wall and a closed door are seen; what is behind them is not.
    CHECK(view.at(3, 0) == '?');
    CHECK(view.at(0, 3) == '?');
    CHECK(view.legend.at("?") == "out of sight or off the loaded map");
}

TEST_CASE("driver_view_lists_creatures_in_sight_nearest_first_with_hostility", "[driver]") {
    setup();
    put_terrain(1, 0, "t_wall");
    refresh();
    spawn_test_monster("mon_zombie", centre + tripoint_rel_ms(3, -3, 0));
    spawn_test_monster("mon_zombie", centre + tripoint_rel_ms(-1, 2, 0));
    monster& pet = spawn_test_monster("mon_zombie", centre + tripoint_rel_ms(-2, -1, 0));
    pet.friendly = -1;
    // Behind the wall, out of sight.
    spawn_test_monster("mon_zombie", centre + tripoint_rel_ms(3, 0, 0));

    const seen view = view_of(5);
    REQUIRE(view.creatures.size() == 3);
    CHECK(view.creatures[0].dx == -2);
    CHECK(view.creatures[0].dy == -1);
    CHECK_FALSE(view.creatures[0].hostile);
    CHECK(view.creatures[1].dx == -1);
    CHECK(view.creatures[1].dy == 2);
    CHECK(view.creatures[1].hostile);
    CHECK(view.creatures[2].dx == 3);
    CHECK(view.creatures[2].dy == -3);
    for (const listed& creature : view.creatures) {
        CHECK(creature.id == "mon_zombie");
        CHECK_FALSE(creature.name.empty());
        CHECK(view.at(creature.dx, creature.dy) == 'M');
    }
    CHECK(view.legend.at("M") == "monster");
    CHECK(view.creature_at(3, 0) == nullptr);
    CHECK(view.at(3, 0) == '?');
    CHECK_FALSE(view.cut);
}

TEST_CASE("driver_view_lists_items_with_the_ids_query_inventory_reports", "[driver]") {
    setup();
    put_terrain(1, 0, "t_wall");
    item& rock = put_item(2, 2, "rock");
    item& stick = put_item(0, 0, "stick");
    item& jeans = put_item(0, 0, "jeans");
    put_item(3, 0, "rock");
    refresh();

    const seen view = view_of(4);
    // Underfoot: the ids and names the inventory query lists as `here`.
    std::vector<std::pair<std::string, std::string>> underfoot;
    for (const listed& it : view.items) {
        if (it.dx == 0 && it.dy == 0) { underfoot.emplace_back(it.id, it.name); }
    }
    std::ostringstream os;
    JsonOut jo(os, false);
    jo.start_object();
    driver_items::write_query(jo, driver_items::query_topic::inventory);
    jo.end_object();
    std::istringstream in(os.str());
    JsonIn jsin(in);
    JsonObject answer = jsin.get_object();
    answer.allow_omitted_members();
    std::vector<std::pair<std::string, std::string>> here;
    for (JsonObject entry : answer.get_array("here")) {
        entry.allow_omitted_members();
        here.emplace_back(entry.get_string("id"), entry.get_string("name"));
    }
    std::ranges::sort(underfoot);
    std::ranges::sort(here);
    REQUIRE(here.size() == 2);
    CHECK(underfoot == here);
    CHECK(driver_items::issue_id(stick) != driver_items::issue_id(jeans));

    const listed* const far_rock = view.item_at(2, 2);
    REQUIRE(far_rock != nullptr);
    CHECK(far_rock->id == driver_items::issue_id(rock));
    CHECK(far_rock->name == driver_items::truncate_name(rock.display_name()));
    CHECK(view.at(2, 2) == '*');
    CHECK(view.legend.at("*") == "items");
    // The rock behind the wall is out of sight.
    CHECK(view.item_at(3, 0) == nullptr);
    CHECK_FALSE(view.cut);
}

TEST_CASE("driver_view_cuts_long_lists_nearest_kept_and_says_so", "[driver]") {
    setup();
    // 30 items, one per tile in a block, and the farthest of all on the window's corner.
    for (int i = 0; i < 30; ++i) { put_item(1 + i % 6, i / 6 - 2, "tank_gun_auto"); }
    put_item(10, 10, "rock");
    for (int i = 0; i < 14; ++i) {
        spawn_test_monster("mon_zombie", centre + tripoint_rel_ms(-1 - i % 7, -3 + i / 7 * 3 + i % 2, 0));
    }
    refresh();

    const seen view = view_of(10);
    CHECK(view.cut);
    CHECK(view.creatures.size() == 10);
    CHECK(view.items.size() <= 16);
    CHECK(view.items.size() >= 8);
    CHECK(view.bytes <= view_ceiling_bytes);
    CHECK(view.item_at(10, 10) == nullptr);
    // What is kept is the nearest: nothing listed is farther than something left out.
    auto reach = [](const listed& it) { return std::max(std::abs(it.dx), std::abs(it.dy)); };
    CHECK(std::ranges::is_sorted(view.items, {}, reach));
    CHECK(std::ranges::is_sorted(view.creatures, {}, reach));
    // The grid still draws what the lists left out.
    CHECK(view.at(10, 10) == '*');
}

TEST_CASE("driver_view_shrinks_the_window_to_fit_the_room_it_is_given", "[driver]") {
    setup();
    for (int i = 0; i < 30; ++i) { put_item(1 + i % 6, i / 6 - 2, "tank_gun_auto"); }
    for (int i = 0; i < 14; ++i) {
        spawn_test_monster("mon_zombie", centre + tripoint_rel_ms(-1 - i % 7, -3 + i / 7 * 3 + i % 2, 0));
    }
    refresh();

    int widest = 0;
    for (const size_t room : {size_t{2600}, size_t{1400}, size_t{600}}) {
        CAPTURE(room);
        const seen view = view_of(10, room);
        CHECK(view.bytes <= room);
        CHECK(view.radius >= 1);
        CHECK(view.radius <= 10);
        REQUIRE(view.grid.size() == static_cast<size_t>(2 * view.radius + 1));
        CHECK(view.at(0, 0) == '@');
        // A window that was made smaller, or a list that was cut, says so.
        CHECK(view.cut);
        // A smaller room never gives a wider window.
        if (widest != 0) { CHECK(view.radius <= widest); }
        widest = view.radius;
    }
    CHECK(widest < 10);

    // Room for the window but not for much else: the lists are cut, the window stays whole.
    const seen roomy = view_of(2, 700);
    CHECK(roomy.radius == 2);
    CHECK(roomy.bytes <= 700);

    // Less room than even the smallest window takes: it is still answered, and says it is cut.
    const seen cramped = view_of(10, 50);
    CHECK(cramped.radius == 1);
    CHECK(cramped.cut);
}
