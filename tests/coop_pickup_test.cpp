#ifdef COOP_ENABLED
/**
 * C1 PICKUP — host-side manifest→removal tests.
 *
 * Exercises coop_server::apply_pickup_manifest() directly (no live server,
 * no proxy NPC) against a real game map populated with items.
 *
 * Three risk areas from the advisory:
 *   1. cbc (count-by-charges) branch: charges_taken math and
 *      charges >= have erase-vs-decrement decision.
 *   2. qty discrete-item removal loop.
 *   3. Same type, multiple stacks on one tile — host removes by type only,
 *      so repeated entries in the manifest must each consume one instance.
 *
 * Schema under test: {"items":[{"tx":N,"ty":N,"tz":N,"type":"…","charges":N,"qty":N},…]}
 *
 * Tags: [coop][pickup]
 */

#include "calendar.h"
#include "catch/catch.hpp"
#include "coop_server.h"
#include "coordinates.h"
#include "game.h"
#include "item.h"
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

/// Tile well inside the test map, away from the player.
static constexpr tripoint_bub_ms TILE{40, 40, 0};

/// Count-by-charges ammo (cbc).
static const itype_id AMMO_ID("9mm");
/// Discrete item: GENERIC knife, no charges field, never cbc.
static const itype_id DISCRETE_ID("knife_combat");

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

/// Return the charges of the first matching item on TILE, or -1 if absent.
auto charges_at(const itype_id& type) -> int {
    for (const item* it : get_map().i_at(TILE)) {
        if (it->typeId() == type) { return it->charges; }
    }
    return -1;
}

/// Build a PICKUP manifest JSON for a single entry.
auto make_manifest(const tripoint_abs_ms& pos, const std::string& type, int charges, int qty)
    -> std::string {
    std::ostringstream oss;
    oss << "{\"items\":[{\"tx\":" << pos.x() << ",\"ty\":" << pos.y() << ",\"tz\":" << pos.z()
        << ",\"type\":\"" << type << "\",\"charges\":" << charges << ",\"qty\":" << qty << "}]}";
    return oss.str();
}

/// Two entries for the same type/tile — exercises the repeated-entry removal path.
auto make_manifest_2x(const tripoint_abs_ms& pos, const std::string& type, int charges, int qty)
    -> std::string {
    std::ostringstream oss;
    const auto entry = [&](std::ostringstream& o) {
        o << "{\"tx\":" << pos.x() << ",\"ty\":" << pos.y() << ",\"tz\":" << pos.z()
          << ",\"type\":\"" << type << "\",\"charges\":" << charges << ",\"qty\":" << qty << "}";
    };
    oss << "{\"items\":[";
    entry(oss);
    oss << ",";
    entry(oss);
    oss << "]}";
    return oss.str();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// 1. cbc — full charge removal (charges >= have → erase the item)
// ---------------------------------------------------------------------------

TEST_CASE("apply_pickup_manifest — cbc full removal", "[coop][pickup]") {
    setup_world();

    get_map().add_item(TILE, item::spawn(AMMO_ID, calendar::turn, 30));
    REQUIRE(charges_at(AMMO_ID) == 30);

    const tripoint_abs_ms abs = get_map().bub_to_abs(TILE);
    coop_server::apply_pickup_manifest(make_manifest(abs, AMMO_ID.str(), 30, 0));

    CHECK(count_items(AMMO_ID) == 0);
}

// ---------------------------------------------------------------------------
// 2. cbc — partial charge removal (charges < have → decrement, keep item)
// ---------------------------------------------------------------------------

TEST_CASE("apply_pickup_manifest — cbc partial removal", "[coop][pickup]") {
    setup_world();

    get_map().add_item(TILE, item::spawn(AMMO_ID, calendar::turn, 30));
    REQUIRE(charges_at(AMMO_ID) == 30);

    const tripoint_abs_ms abs = get_map().bub_to_abs(TILE);
    coop_server::apply_pickup_manifest(make_manifest(abs, AMMO_ID.str(), 10, 0));

    CHECK(charges_at(AMMO_ID) == 20);
}

// ---------------------------------------------------------------------------
// 3. qty — discrete item removal loop removes exactly N
// ---------------------------------------------------------------------------

TEST_CASE("apply_pickup_manifest — qty removes correct count", "[coop][pickup]") {
    setup_world();

    // Guard: DISCRETE_ID must not be count_by_charges — if someone later changes
    // knife_combat to cbc, this test would silently exercise the wrong branch.
    REQUIRE(!item::spawn(DISCRETE_ID, calendar::turn, item::solitary_tag{})->count_by_charges());

    // Place 3 discrete knives on the tile.
    for (int i = 0; i < 3; ++i) {
        get_map().add_item(TILE, item::spawn(DISCRETE_ID, calendar::turn, item::solitary_tag{}));
    }
    REQUIRE(count_items(DISCRETE_ID) == 3);

    const tripoint_abs_ms abs = get_map().bub_to_abs(TILE);
    coop_server::apply_pickup_manifest(make_manifest(abs, DISCRETE_ID.str(), 0, 2));

    // Exactly 1 must remain.
    CHECK(count_items(DISCRETE_ID) == 1);
}

// ---------------------------------------------------------------------------
// 4. not-found → skip gracefully (no crash, no modification to other items)
// ---------------------------------------------------------------------------

TEST_CASE("apply_pickup_manifest — missing type is skipped", "[coop][pickup]") {
    setup_world();

    get_map().add_item(TILE, item::spawn(DISCRETE_ID, calendar::turn, item::solitary_tag{}));
    REQUIRE(count_items(DISCRETE_ID) == 1);

    const tripoint_abs_ms abs = get_map().bub_to_abs(TILE);
    // Ask to remove a type that doesn't exist on the tile.
    coop_server::apply_pickup_manifest(make_manifest(abs, "9mm", 0, 1));

    // The knife must be untouched.
    CHECK(count_items(DISCRETE_ID) == 1);
}

// ---------------------------------------------------------------------------
// 5. same type × 2 manifest entries on one tile → removes exactly 2 instances
//    (exercises "host removes by type only" with repeated entries)
// ---------------------------------------------------------------------------

TEST_CASE("apply_pickup_manifest — two manifest entries same type removes two", "[coop][pickup]") {
    setup_world();

    REQUIRE(!item::spawn(DISCRETE_ID, calendar::turn, item::solitary_tag{})->count_by_charges());

    for (int i = 0; i < 3; ++i) {
        get_map().add_item(TILE, item::spawn(DISCRETE_ID, calendar::turn, item::solitary_tag{}));
    }
    REQUIRE(count_items(DISCRETE_ID) == 3);

    const tripoint_abs_ms abs = get_map().bub_to_abs(TILE);
    coop_server::apply_pickup_manifest(make_manifest_2x(abs, DISCRETE_ID.str(), 0, 1));

    CHECK(count_items(DISCRETE_ID) == 1);
}

// ---------------------------------------------------------------------------
// 6. out-of-bounds tile → silently skipped, no crash
// ---------------------------------------------------------------------------

TEST_CASE("apply_pickup_manifest — out-of-bounds tile is skipped", "[coop][pickup]") {
    setup_world();

    // Wildly out-of-bounds absolute position — inbounds() must reject it cleanly.
    const std::string manifest =
        "{\"items\":[{\"tx\":999999,\"ty\":999999,\"tz\":0,\"type\":"
        "\"9mm\",\"charges\":0,\"qty\":1}]}";

    REQUIRE_NOTHROW(coop_server::apply_pickup_manifest(manifest));
}

#endif // COOP_ENABLED
