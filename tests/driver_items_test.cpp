#include "catch/catch_amalgamated.hpp"
#include "avatar.h"
#include "calendar.h"
#include "driver_items.h"
#include "game.h"
#include "item.h"
#include "json.h"
#include "map.h"
#include "state_helpers.h"
#include "type_id.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

// What the real-binary contract cannot reach on the Bairdford fixture: ids of items that were
// destroyed, a pickup the game rejects as too heavy, and a list long enough to be cut.

namespace {

constexpr tripoint_bub_ms centre{60, 60, 0};

auto setup() -> avatar& {
    clear_all_state();
    map& here = get_map();
    g->place_player(centre);
    for (const tripoint_bub_ms& pos : here.points_in_radius(centre, 2)) { here.i_clear(pos); }
    avatar& u = get_avatar();
    u.moves = 100;
    return u;
}

/// The item of `type` on the avatar's tile, which the test put there.
auto on_ground(const itype_id& type) -> item* {
    for (item* it : get_map().i_at(centre)) {
        if (it->typeId() == type) { return it; }
    }
    return nullptr;
}

auto put_on_ground(const char* type) -> item& {
    get_map().add_item_or_charges(centre, item::spawn(type));
    item* const placed = on_ground(itype_id(type));
    REQUIRE(placed != nullptr);
    return *placed;
}

/// Runs a command on the item behind `id`, and says how many moves it cost.
struct ran {
    driver_items::command_result result;
    int spent = 0;
};

auto run(driver_items::command kind, const std::string& id) -> ran {
    const driver_items::found_item found = driver_items::find_item(id);
    REQUIRE(found.error.empty());
    // The driver only acts on an avatar that has moves to spend.
    get_avatar().moves = 100;
    ran out;
    out.result = driver_items::run_command(kind, found.ref);
    out.spent = 100 - get_avatar().moves;
    return out;
}

using driver_items::command;

} // namespace

TEST_CASE("driver_items_refuses_ids_that_were_never_issued", "[driver]") {
    setup();
    for (const std::string bad : {"", "abc", "-1", "12x", "999999999999"}) {
        CHECK_FALSE(driver_items::find_item(bad).error.empty());
    }
}

TEST_CASE("driver_items_stale_id_is_an_error_and_leaves_other_items_alone", "[driver]") {
    setup();
    item& rock = put_on_ground("rock");
    item& stick = put_on_ground("stick");
    const std::string rock_id = driver_items::issue_id(rock);
    const std::string stick_id = driver_items::issue_id(stick);
    REQUIRE(rock_id != stick_id);
    REQUIRE(driver_items::find_item(rock_id).error.empty());

    {
        // The rock is destroyed when the detached item leaves scope.
        detached_ptr<item> gone = get_map().i_rem(centre, &rock);
    }

    const driver_items::found_item stale = driver_items::find_item(rock_id);
    CHECK(stale.error.find("stale") != std::string::npos);
    const driver_items::found_item other = driver_items::find_item(stick_id);
    CHECK(other.error.empty());
    CHECK(on_ground(itype_id("stick")) != nullptr);
    CHECK(on_ground(itype_id("rock")) == nullptr);
}

TEST_CASE("driver_items_commands_map_game_results_to_outcomes", "[driver]") {
    avatar& u = setup();
    item& jeans = put_on_ground("jeans");
    const std::string jeans_id = driver_items::issue_id(jeans);

    SECTION("pickup completes and costs moves") {
        const ran took = run(command::pickup, jeans_id);
        CHECK(took.result.outcome == "completed");
        CHECK(took.spent > 0);
        CHECK(on_ground(itype_id("jeans")) == nullptr);
        CHECK(u.has_item(jeans));
    }

    SECTION("a pickup the game rejects as too heavy is refused, free, with its message") {
        // The test avatar may carry anything; the real game's limits apply to this one.
        u.unset_mutation(trait_id("DEBUG_STORAGE"));
        const std::string heavy = driver_items::issue_id(put_on_ground("tank_gun_auto"));
        const ran took = run(command::pickup, heavy);
        CHECK(took.result.outcome == "refused");
        CHECK(took.result.detail.find("too heavy") != std::string::npos);
        CHECK(took.spent == 0);
        CHECK(on_ground(itype_id("tank_gun_auto")) != nullptr);
    }

    SECTION("an item that is not carried cannot be worn, wielded, dropped or taken off") {
        for (const command kind : {command::wear, command::wield, command::drop,
                                   command::take_off}) {
            const ran took = run(kind, jeans_id);
            CHECK(took.result.outcome == "refused");
            CHECK_FALSE(took.result.detail.empty());
            CHECK(took.spent == 0);
        }
    }

    SECTION("wear, wield and take_off walk an item through its places") {
        REQUIRE(run(command::pickup, jeans_id).result.outcome == "completed");
        CHECK(run(command::wear, jeans_id).result.outcome == "completed");
        CHECK(u.is_worn(jeans));

        const ran again = run(command::wear, jeans_id);
        CHECK(again.result.outcome == "refused");
        CHECK(again.spent == 0);

        const ran off = run(command::take_off, jeans_id);
        CHECK(off.result.outcome == "completed");
        CHECK(off.spent > 0);
        CHECK_FALSE(u.is_worn(jeans));

        const ran unworn = run(command::take_off, jeans_id);
        CHECK(unworn.result.outcome == "refused");
        CHECK(unworn.spent == 0);

        CHECK(run(command::wield, jeans_id).result.outcome == "completed");
        CHECK(u.is_wielding(jeans));
        const ran wielded_again = run(command::wield, jeans_id);
        CHECK(wielded_again.result.outcome == "no_effect");
        CHECK(wielded_again.spent == 0);
    }

    SECTION("wielding while armed puts the old weapon away without a menu") {
        item& rock = put_on_ground("rock");
        const std::string rock_id = driver_items::issue_id(rock);
        REQUIRE(run(command::pickup, jeans_id).result.outcome == "completed");
        REQUIRE(run(command::pickup, rock_id).result.outcome == "completed");
        REQUIRE(run(command::wield, jeans_id).result.outcome == "completed");
        const ran swapped = run(command::wield, rock_id);
        CAPTURE(swapped.result.outcome, swapped.result.detail, u.primary_weapon().typeId().str());
        CHECK(swapped.result.outcome == "completed");
        CHECK(swapped.spent > 0);
        const driver_items::found_item wielded = driver_items::find_item(rock_id);
        REQUIRE(wielded.error.empty());
        CHECK(u.is_wielding(*wielded.ref.get()));
        CHECK(u.has_item_with([](const item& it) { return it.typeId() == itype_id("jeans"); }));
    }

    SECTION("drop puts a carried item on the ground, and the ground cannot be dropped from") {
        REQUIRE(run(command::pickup, jeans_id).result.outcome == "completed");
        const ran dropped = run(command::drop, jeans_id);
        CAPTURE(dropped.result.detail);
        CHECK(dropped.result.outcome == "completed");
        CHECK(on_ground(itype_id("jeans")) != nullptr);
        const ran again = run(command::drop, jeans_id);
        CHECK(again.result.outcome == "refused");
        CHECK(again.spent == 0);
    }

    SECTION("an item the game will not let the avatar wear is refused with its message") {
        const std::string rock_id = driver_items::issue_id(put_on_ground("rock"));
        REQUIRE(run(command::pickup, rock_id).result.outcome == "completed");
        const ran took = run(command::wear, rock_id);
        CHECK(took.result.outcome == "refused");
        CHECK_FALSE(took.result.detail.empty());
        CHECK(took.spent == 0);
    }
}

TEST_CASE("driver_items_inventory_query_cuts_long_lists_and_says_so", "[driver]") {
    setup();
    for (int i = 0; i < 15; ++i) { put_on_ground("knife_combat"); }

    std::ostringstream out;
    JsonOut jo(out, false);
    jo.start_object();
    const bool cut = driver_items::write_query(jo, "inventory");
    jo.end_object();

    CHECK(cut);
    std::istringstream in(out.str());
    JsonIn jsin(in);
    JsonObject answer = jsin.get_object();
    answer.allow_omitted_members();
    CHECK(answer.get_string("topic") == "inventory");
    CHECK(answer.get_array("here").size() == 10);
    JsonObject first = answer.get_array("here").next_object();
    first.allow_omitted_members();
    CHECK(first.get_string("name").size() > 0);
}

TEST_CASE("driver_items_inventory_query_is_not_cut_when_it_fits", "[driver]") {
    setup();
    put_on_ground("rock");
    std::ostringstream out;
    JsonOut jo(out, false);
    jo.start_object();
    const bool cut = driver_items::write_query(jo, "inventory");
    jo.end_object();
    CHECK_FALSE(cut);
}

TEST_CASE("driver_items_effects_query_lists_active_effects", "[driver]") {
    avatar& u = setup();
    u.add_effect(efftype_id("pkill1"), 30_minutes);

    std::ostringstream out;
    JsonOut jo(out, false);
    jo.start_object();
    const bool cut = driver_items::write_query(jo, "effects");
    jo.end_object();

    CHECK_FALSE(cut);
    std::istringstream in(out.str());
    JsonIn jsin(in);
    JsonObject answer = jsin.get_object();
    answer.allow_omitted_members();
    JsonArray effects = answer.get_array("effects");
    bool found = false;
    for (JsonObject effect : effects) {
        effect.allow_omitted_members();
        if (effect.get_string("id") == "pkill1") {
            found = true;
            CHECK(effect.get_int("intensity") >= 1);
        }
    }
    CHECK(found);
}
