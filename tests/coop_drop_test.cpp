#ifdef COOP_ENABLED
/**
 * C2a DROP — host-side manifest→addition tests.
 *
 * Exercises coop_server::apply_drop_manifest() directly (no live server,
 * no proxy NPC) against a real game map.
 *
 * Schema under test: {"items":[{"tx":N,"ty":N,"tz":N,"data":"<item json>"},…]}
 *
 *   1. Dropped item appears on the map at the specified abs position.
 *   2. cbc charges survive round-trip.
 *   3. Per-instance damage survives round-trip — validates full-JSON design: a reductive
 *      type/charges/qty schema would reset damage to 0, proving serialize→deserialize is needed.
 *   4. Multiple items in one manifest all land.
 *   5. Deserialize failure (bad JSON) is skipped gracefully.
 *   6. Out-of-bounds tile is silently skipped.
 *
 * Tags: [coop][drop]
 */

#include "calendar.h"
#include "catch/catch_amalgamated.hpp"
#include "coop_server.h"
#include "coordinates.h"
#include "game.h"
#include "item.h"
#include "json.h"
#include "map.h"
#include "map_helpers.h"
#include "state_helpers.h"
#include "type_id.h"

#include <sstream>
#include <string>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

static constexpr tripoint_bub_ms TILE{40, 40, 0};
static const itype_id AMMO_ID("9mm");           // count_by_charges
static const itype_id KNIFE_ID("knife_combat"); // discrete GENERIC

void setup_world() {
    clear_all_state();
    build_test_map(ter_id("t_grass"));
    put_player_underground();
}

/// Count items of a given type on TILE.
auto count_items(const itype_id& type) -> int {
    int n = 0;
    for (const item* it : get_map().i_at(TILE)) {
        if (it->typeId() == type) { ++n; }
    }
    return n;
}

/// Return charges of first matching item on TILE, -1 if absent.
auto charges_at(const itype_id& type) -> int {
    for (const item* it : get_map().i_at(TILE)) {
        if (it->typeId() == type) { return it->charges; }
    }
    return -1;
}

/// Serialize one item to a DROP manifest entry JSON string.
auto serialize_item_to_manifest(const item& it, const tripoint_abs_ms& pos) -> std::string {
    std::ostringstream oss;
    JsonOut jout(oss);
    jout.start_object();
    jout.member("items");
    jout.start_array();
    jout.start_object();
    jout.member("tx", pos.x());
    jout.member("ty", pos.y());
    jout.member("tz", pos.z());
    std::ostringstream item_oss;
    JsonOut jitem(item_oss);
    it.serialize(jitem);
    jout.member("data", item_oss.str());
    jout.end_object();
    jout.end_array();
    jout.end_object();
    return oss.str();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// 1. Item appears at the correct position
// ---------------------------------------------------------------------------

TEST_CASE("apply_drop_manifest — item lands on map", "[coop][drop]") {
    setup_world();

    REQUIRE(count_items(KNIFE_ID) == 0);

    const auto src = item::spawn(KNIFE_ID, calendar::turn, item::solitary_tag{});
    const tripoint_abs_ms abs = get_map().bub_to_abs(TILE);
    coop_server::apply_drop_manifest(serialize_item_to_manifest(*src, abs));

    CHECK(count_items(KNIFE_ID) == 1);
}

// ---------------------------------------------------------------------------
// 2. cbc item: charges survive round-trip through serialize/deserialize
// ---------------------------------------------------------------------------

TEST_CASE("apply_drop_manifest — cbc item charges preserved", "[coop][drop]") {
    setup_world();

    const auto src = item::spawn(AMMO_ID, calendar::turn, 17);
    REQUIRE(src->charges == 17);

    const tripoint_abs_ms abs = get_map().bub_to_abs(TILE);
    coop_server::apply_drop_manifest(serialize_item_to_manifest(*src, abs));

    CHECK(charges_at(AMMO_ID) == 17);
}

// ---------------------------------------------------------------------------
// 3. Per-instance damage survives round-trip — proves full-JSON is necessary.
//    A reductive type/charges/qty schema would spawn a fresh item (damage=0),
//    silently discarding the damage state. This test catches that regression.
// ---------------------------------------------------------------------------

TEST_CASE("apply_drop_manifest — per-instance damage preserved", "[coop][drop]") {
    setup_world();

    auto src = item::spawn(KNIFE_ID, calendar::turn, item::solitary_tag{});
    const int target_damage = 1500; // damaged but not destroyed
    src->set_damage(target_damage);
    REQUIRE(src->damage() == target_damage);

    const tripoint_abs_ms abs = get_map().bub_to_abs(TILE);
    coop_server::apply_drop_manifest(serialize_item_to_manifest(*src, abs));

    REQUIRE(count_items(KNIFE_ID) == 1);
    const item* landed = nullptr;
    for (const item* it : get_map().i_at(TILE)) {
        if (it->typeId() == KNIFE_ID) {
            landed = it;
            break;
        }
    }
    REQUIRE(landed != nullptr);
    CHECK(landed->damage() == target_damage);
}

// ---------------------------------------------------------------------------
// 4. Multiple items in one manifest all land
// ---------------------------------------------------------------------------

TEST_CASE("apply_drop_manifest — multiple items all land", "[coop][drop]") {
    setup_world();

    const tripoint_abs_ms abs = get_map().bub_to_abs(TILE);
    std::ostringstream oss;
    JsonOut jout(oss);
    jout.start_object();
    jout.member("items");
    jout.start_array();

    for (int i = 0; i < 3; ++i) {
        const auto it = item::spawn(KNIFE_ID, calendar::turn, item::solitary_tag{});
        jout.start_object();
        jout.member("tx", abs.x());
        jout.member("ty", abs.y());
        jout.member("tz", abs.z());
        std::ostringstream item_oss;
        JsonOut jitem(item_oss);
        it->serialize(jitem);
        jout.member("data", item_oss.str());
        jout.end_object();
    }

    jout.end_array();
    jout.end_object();

    coop_server::apply_drop_manifest(oss.str());

    CHECK(count_items(KNIFE_ID) == 3);
}

// ---------------------------------------------------------------------------
// 4. Bad item JSON in "data" field is skipped gracefully (no crash)
// ---------------------------------------------------------------------------

TEST_CASE("apply_drop_manifest — bad item data skipped gracefully", "[coop][drop]") {
    setup_world();

    const tripoint_abs_ms abs = get_map().bub_to_abs(TILE);
    // "data" contains invalid JSON — deserialize must fail without crashing.
    const std::string manifest =
        "{\"items\":[{\"tx\":" + std::to_string(abs.x()) + ",\"ty\":" + std::to_string(abs.y())
        + ",\"tz\":" + std::to_string(abs.z()) + ",\"data\":\"not valid item json\"}]}";

    REQUIRE_NOTHROW(coop_server::apply_drop_manifest(manifest));
    // Nothing should have been added.
    CHECK(count_items(KNIFE_ID) == 0);
    CHECK(count_items(AMMO_ID) == 0);
}

// ---------------------------------------------------------------------------
// 5. Out-of-bounds position is skipped silently
// ---------------------------------------------------------------------------

TEST_CASE("apply_drop_manifest — out-of-bounds tile skipped", "[coop][drop]") {
    setup_world();

    const auto src = item::spawn(KNIFE_ID, calendar::turn, item::solitary_tag{});
    std::ostringstream item_oss;
    JsonOut jitem(item_oss);
    src->serialize(jitem);

    // Build manifest with JsonOut so "data" is properly escaped as a JSON string value.
    std::ostringstream manifest_oss;
    JsonOut jm(manifest_oss);
    jm.start_object();
    jm.member("items");
    jm.start_array();
    jm.start_object();
    jm.member("tx", 999999);
    jm.member("ty", 999999);
    jm.member("tz", 0);
    jm.member("data", item_oss.str());
    jm.end_object();
    jm.end_array();
    jm.end_object();

    REQUIRE_NOTHROW(coop_server::apply_drop_manifest(manifest_oss.str()));
}

#endif // COOP_ENABLED
