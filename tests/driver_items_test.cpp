#include "catch/catch_amalgamated.hpp"
#include "activity_type.h"
#include "avatar.h"
#include "calendar.h"
#include "driver_items.h"
#include "game.h"
#include "item.h"
#include "json.h"
#include "map.h"
#include "map_helpers.h"
#include "player_activity.h"
#include "recipe.h"
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

namespace {

using driver_items::command_options;

/// Like `run`, with the options some commands take.
auto run_with(driver_items::command kind, const std::string& id, const command_options& options)
    -> ran {
    const driver_items::found_item found = driver_items::find_item(id);
    REQUIRE(found.error.empty());
    get_avatar().moves = 100;
    ran out;
    out.result = driver_items::run_command(kind, found.ref, options);
    out.spent = 100 - get_avatar().moves;
    return out;
}

/// An item of `type` in the avatar's inventory; says its id.
auto carry(const char* type) -> std::string {
    item& placed = put_on_ground(type);
    const std::string id = driver_items::issue_id(placed);
    REQUIRE(run(command::pickup, id).result.outcome == "completed");
    return id;
}

/// Lets the avatar's activity run, a turn at a time, until it ends; the turns it took.
auto finish_activity(avatar& u) -> int {
    int turns = 0;
    while (u.activity && *u.activity && turns < 100000) {
        u.moves = 100;
        u.activity->do_turn(u);
        ++turns;
    }
    REQUIRE(turns < 100000);
    return turns;
}

auto midday_light() -> void {
    set_time(calendar::turn_zero + 12_hours);
    get_avatar().recalc_sight_limits();
}

} // namespace

TEST_CASE("driver_items_eat_consumes_food_and_refuses_what_the_game_would_ask_about", "[driver]") {
    avatar& u = setup();
    const std::string apple = carry("apple");

    SECTION("a hungry avatar eats it, which costs moves") {
        u.set_stored_kcal(u.max_stored_kcal() / 2);
        u.set_thirst(300);
        const ran ate = run(command::eat, apple);
        CAPTURE(ate.result.detail);
        CHECK(ate.result.outcome == "completed");
        CHECK(ate.spent > 0);
        CHECK_FALSE(u.has_item_with([](const item& it) { return it.typeId() == itype_id("apple"); }));
    }

    SECTION("a full avatar is refused with the game's words, and the food is kept") {
        u.set_stored_kcal(u.max_stored_kcal());
        const ran refused = run(command::eat, apple);
        CHECK(refused.result.outcome == "refused");
        CHECK_FALSE(refused.result.detail.empty());
        CHECK(refused.spent == 0);
        CHECK(u.has_item_with([](const item& it) { return it.typeId() == itype_id("apple"); }));

        // The game's "eat it anyway?" answered yes.
        const ran anyway = run_with(command::eat, apple, {.anyway = true});
        CHECK(anyway.result.outcome == "completed");
        CHECK(anyway.spent > 0);
        CHECK_FALSE(u.has_item_with([](const item& it) { return it.typeId() == itype_id("apple"); }));
    }

    SECTION("something that is not food is refused") {
        const std::string rock = carry("rock");
        const ran refused = run(command::eat, rock);
        CHECK(refused.result.outcome == "refused");
        CHECK_FALSE(refused.result.detail.empty());
        CHECK(refused.spent == 0);
    }

    SECTION("what is not carried cannot be eaten") {
        const std::string ground = driver_items::issue_id(put_on_ground("apple"));
        const ran refused = run(command::eat, ground);
        CHECK(refused.result.outcome == "refused");
        CHECK(refused.spent == 0);
    }
}

TEST_CASE("driver_items_use_runs_the_item_s_use_and_says_when_it_has_none", "[driver]") {
    avatar& u = setup();

    SECTION("an item with one use runs it") {
        const std::string stick = carry("glowstick");
        const ran used = run(command::use, stick);
        CAPTURE(used.result.detail);
        // The use costs no charge, so the game returns false; the item changing is the proof.
        CHECK(used.result.outcome == "completed");
        CHECK(u.has_item_with([](const item& it) { return it.typeId() == itype_id("glowstick_lit"); }));
    }

    SECTION("a use the item does not have is refused and the ones it has are named") {
        const std::string stick = carry("glowstick");
        const ran refused = run_with(command::use, stick, {.method = "no_such_use"});
        CHECK(refused.result.outcome == "refused");
        CHECK(refused.result.detail.find("transform") != std::string::npos);
        CHECK(refused.spent == 0);
    }

    SECTION("an item with no use is refused") {
        const std::string rock = carry("rock");
        const ran refused = run(command::use, rock);
        CHECK(refused.result.outcome == "refused");
        CHECK_FALSE(refused.result.detail.empty());
    }
}

TEST_CASE("driver_items_read_starts_the_reading_activity", "[driver]") {
    avatar& u = setup();
    midday_light();

    SECTION("a book is read, once, as an activity") {
        const std::string book = carry("mag_cooking");
        const ran started = run(command::read, book);
        CHECK(started.result.outcome == "completed");
        REQUIRE(u.activity);
        CHECK(u.activity->id() == activity_id("ACT_READ"));
        // Nothing else may be started over it.
        CHECK(run(command::read, book).result.outcome == "refused");
        u.cancel_activity();
    }

    SECTION("something that is not a book is refused with the game's words") {
        const std::string rock = carry("rock");
        const ran refused = run(command::read, rock);
        CHECK(refused.result.outcome == "refused");
        CHECK_FALSE(u.activity);
    }
}

TEST_CASE("driver_items_reload_starts_the_reload_and_it_fills_the_magazine", "[driver]") {
    avatar& u = setup();

    SECTION("a magazine is loaded from the ammo the avatar carries") {
        const std::string mag = carry("glockmag");
        carry("9mm");
        const ran started = run(command::reload, mag);
        CHECK(started.result.outcome == "completed");
        REQUIRE(u.activity);
        CHECK(u.activity->id() == activity_id("ACT_RELOAD"));
        finish_activity(u);
        const driver_items::found_item found = driver_items::find_item(mag);
        REQUIRE(found.error.empty());
        CHECK(found.ref.get()->ammo_remaining() > 0);
    }

    SECTION("with no ammo it is refused with the game's words") {
        const std::string mag = carry("glockmag");
        const ran refused = run(command::reload, mag);
        CHECK(refused.result.outcome == "refused");
        CHECK_FALSE(u.activity);
    }

    SECTION("what cannot be reloaded is refused") {
        const std::string rock = carry("rock");
        const ran refused = run(command::reload, rock);
        CHECK(refused.result.outcome == "refused");
        CHECK_FALSE(refused.result.detail.empty());
    }
}

TEST_CASE("driver_items_craft_by_recipe_id_makes_the_item", "[driver]") {
    avatar& u = setup();
    midday_light();
    const recipe& rec = recipe_id("pointy_stick").obj();

    SECTION("an unknown recipe id is an error the driver reports before acting") {
        CHECK_FALSE(driver_items::recipe_error("no_such_recipe").empty());
        CHECK_FALSE(driver_items::recipe_error("").empty());
        CHECK(driver_items::recipe_error("pointy_stick").empty());
    }

    SECTION("a recipe the avatar does not know is refused") {
        REQUIRE_FALSE(u.knows_recipe(&recipe_id("carver_off").obj()));
        const driver_items::command_result res = driver_items::run_craft("carver_off");
        CHECK(res.outcome == "refused");
        CHECK_FALSE(res.detail.empty());
        CHECK_FALSE(u.activity);
    }

    SECTION("a recipe without the components is refused and names what is missing") {
        u.learn_recipe(&rec);
        u.invalidate_crafting_inventory();
        const driver_items::command_result res = driver_items::run_craft("pointy_stick");
        CHECK(res.outcome == "refused");
        CHECK_FALSE(res.detail.empty());
        CHECK_FALSE(u.activity);
    }

    SECTION("with what it needs the craft runs and the result is carried") {
        u.learn_recipe(&rec);
        u.set_skill_level(rec.skill_used, std::max(rec.difficulty, 1));
        u.i_add(item::spawn("knife_combat"));
        u.i_add(item::spawn("stick"));
        u.invalidate_crafting_inventory();
        const driver_items::command_result res = driver_items::run_craft("pointy_stick");
        REQUIRE(res.outcome == "completed");
        REQUIRE(u.activity);
        CHECK(u.activity->id() == activity_id("ACT_CRAFT"));
        finish_activity(u);
        CHECK(u.has_item_with([](const item& it) { return it.typeId() == itype_id("pointy_stick"); }));
        CHECK_FALSE(u.has_item_with([](const item& it) { return it.typeId() == itype_id("stick"); }));
    }
}

TEST_CASE("driver_items_sleep_starts_trying_to_sleep_once", "[driver]") {
    avatar& u = setup();
    const driver_items::command_result started = driver_items::run_sleep();
    CHECK(started.outcome == "completed");
    REQUIRE(u.activity);
    CHECK(u.activity->id() == activity_id("ACT_TRY_SLEEP"));
    CHECK(driver_items::run_sleep().outcome == "no_effect");
    u.cancel_activity();
}
