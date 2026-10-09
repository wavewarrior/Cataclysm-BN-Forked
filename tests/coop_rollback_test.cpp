/**
 * Round-trip tests for the World event Rollback path (commit 2 of
 * plans/coop-world-event-interpreter.md).
 *
 * Shape of every case: build a map, apply one World event through the World
 * event interpreter exactly as `coop_client::apply_sync` does, push the record it
 * returns into a `coop_rollback_engine`, call `rollback_to`, and assert the map is
 * back to its original state.
 *
 * Commit 1 transcribed the client's five branches by hand to pin the behaviour of
 * `reverse_delta` + `rollback_to`, defects included.  Those defects are gone with
 * the interpreter — the record names field type, old intensity and new intensity
 * explicitly, so inversion needs neither the type flip (D1) nor the
 * value <-> old_value swap (D2) — so the helper now drives production `apply`
 * instead of a transcription, and all four cases run under [coop].
 *
 * Rolling back `field_expired` restores the carried pre-expiry intensity exactly.
 */

#include "calendar.h"
#include "catch/catch_amalgamated.hpp"
#include "coop_mutation_log.h"
#include "coop_proto.h"
#include "coop_rollback.h"
#include "coop_world_event_interpreter.h"
#include "coordinates.h"
#include "field.h"
#include "game.h"
#include "map.h"
#include "map_helpers.h"
#include "state_helpers.h"
#include "type_id.h"

namespace {

constexpr tripoint_bub_ms TILE{40, 40, 0};
constexpr int SYNC_TURN = 7;

/// Production forward-apply: build the wire event the way the producers do, let
/// the interpreter mutate the map and record the pre-mutation state, and hand that
/// record to the rollback engine — the same three steps as apply_sync.
auto apply_sync_event(coop_rollback_engine& engine, coop_event_type type, int ev_val, int ev_cid)
    -> coop_recorded_event {
    // map_local_to_abs is the exact inverse of the abs_to_map_local used by both
    // apply_sync and the interpreter, so the round-trip does not depend on the
    // player's reality-bubble origin matching the map origin.
    coop_world_event ev;
    ev.type = type;
    ev.pos = map_local_to_abs(g->m, TILE);
    ev.value = ev_val;
    ev.creature_id = ev_cid;

    const coop_recorded_event recorded = coop_world_event_interpreter::apply(g->m, ev);
    engine.push(SYNC_TURN, recorded);
    return recorded;
}

/// Fresh grass world with no fields, no furniture, and the player out of the way.
void setup_world() {
    clear_all_state();
    build_test_map(ter_id("t_grass"));
    put_player_underground();
    clear_fields(0);
}

auto field_at_tile(const field_type_str_id& id) -> field_entry* {
    return g->m.get_field(TILE).find_field(field_type_id{id});
}

} // namespace

// ── Terrain and furniture ─────────────────────────────────────────────────────

TEST_CASE("world event rollback round-trip: terrain_changed", "[coop][rollback]") {
    setup_world();
    coop_rollback_engine engine;

    const int orig_ter = g->m.ter(TILE).to_i();
    REQUIRE(g->m.ter(TILE) == ter_str_id("t_grass"));

    const coop_recorded_event rec =
        apply_sync_event(engine, coop_event_type::terrain_changed, ter_id("t_floor").to_i(), 0);

    REQUIRE(g->m.ter(TILE) == ter_str_id("t_floor"));
    CHECK(rec.old_terrain_or_furniture == orig_ter);
    CHECK(rec.terrain_or_furniture == ter_id("t_floor").to_i());

    CHECK(engine.rollback_to(g->m, SYNC_TURN - 1) == 1);
    CHECK(g->m.ter(TILE) == ter_str_id("t_grass"));
    CHECK(engine.size() == 0);
}

TEST_CASE("world event rollback round-trip: furniture_changed", "[coop][rollback]") {
    setup_world();
    coop_rollback_engine engine;

    REQUIRE(g->m.furn(TILE) == furn_str_id("f_null"));

    const coop_recorded_event rec =
        apply_sync_event(engine, coop_event_type::furniture_changed, furn_id("f_locker").to_i(), 0);

    REQUIRE(g->m.furn(TILE) == furn_str_id("f_locker"));
    CHECK(rec.old_terrain_or_furniture == furn_id("f_null").to_i());

    CHECK(engine.rollback_to(g->m, SYNC_TURN - 1) == 1);
    CHECK(g->m.furn(TILE) == furn_str_id("f_null"));
}

// ── Field creation and expiry ────────────────────────────────────────────────

TEST_CASE("world event rollback round-trip: field_created and field_expired", "[coop][rollback]") {
    SECTION("field_created: rollback must remove the created field") {
        setup_world();
        coop_rollback_engine engine;
        REQUIRE(field_at_tile(field_type_str_id("fd_fire")) == nullptr);

        // value = field type, intensity carrier = 3 (interpreter accessor).
        const coop_recorded_event rec = apply_sync_event(
            engine, coop_event_type::field_created, field_type_id("fd_fire").to_i(), 3);
        REQUIRE(field_at_tile(field_type_str_id("fd_fire")) != nullptr);
        CHECK(rec.field == field_type_id("fd_fire").to_i());
        CHECK(rec.old_intensity == 0);
        CHECK(rec.new_intensity == 3);

        // The inverse of a creation is "remove this field", named as such: no
        // intermediate field_expired event to invert a second time (defect D1).
        CHECK(engine.rollback_to(g->m, SYNC_TURN - 1) == 1);
        CHECK(field_at_tile(field_type_str_id("fd_fire")) == nullptr);
    }

    SECTION("field_expired: rollback must restore the expired field") {
        setup_world();
        coop_rollback_engine engine;
        g->m.add_field(TILE, field_type_id("fd_fire"), 3, 0_turns);
        REQUIRE(field_at_tile(field_type_str_id("fd_fire")) != nullptr);

        const coop_recorded_event rec = apply_sync_event(
            engine, coop_event_type::field_expired, field_type_id("fd_fire").to_i(), 3);
        REQUIRE(field_at_tile(field_type_str_id("fd_fire")) == nullptr);
        // The record keeps the field type under its own name, so the inverse finds
        // fd_fire rather than whatever type index old_value happened to hold
        // (defect D2).
        CHECK(rec.field == field_type_id("fd_fire").to_i());

        // The expiry event carries the pre-expiry intensity, so the rollback
        // restores intensity 3 exactly.
        CHECK(engine.rollback_to(g->m, SYNC_TURN - 1) == 1);
        field_entry* restored = field_at_tile(field_type_str_id("fd_fire"));
        REQUIRE(restored != nullptr);
        CHECK(restored->get_field_intensity() == 3);
    }
}

// ── Field intensity change ───────────────────────────────────────────────────

TEST_CASE("world event rollback round-trip: field_changed", "[coop][rollback]") {
    setup_world();
    coop_rollback_engine engine;

    g->m.add_field(TILE, field_type_id("fd_fire"), 2, 0_turns);
    // Guard the choice of old intensity 2: it must not collide with a real field
    // type present on the tile, or a type-confused restore could pass by accident.
    const int fire_idx = field_type_str_id("fd_fire").id().to_i();
    REQUIRE(fire_idx != 2);
    REQUIRE(g->m.get_field(TILE).find_field(field_type_id{2}) == nullptr);

    const coop_recorded_event rec = apply_sync_event(
        engine, coop_event_type::field_changed, field_type_id("fd_fire").to_i(), 3);
    REQUIRE(field_at_tile(field_type_str_id("fd_fire"))->get_field_intensity() == 3);
    CHECK(rec.field == field_type_id("fd_fire").to_i());
    CHECK(rec.old_intensity == 2);
    CHECK(rec.new_intensity == 3);

    // The inverse names fd_fire and intensity 2 explicitly, so the restore hits
    // the field that changed instead of field_type_id{ 2 }.
    CHECK(engine.rollback_to(g->m, SYNC_TURN - 1) == 1);
    field_entry* fe = field_at_tile(field_type_str_id("fd_fire"));
    REQUIRE(fe != nullptr);
    CHECK(fe->get_field_intensity() == 2);
}

// ── The interpreter's own contract ───────────────────────────────────────────

TEST_CASE("world event interpreter replicates exactly five event types", "[coop][rollback]") {
    using evt = coop_event_type;
    CHECK(coop_world_event_interpreter::is_replicated(evt::terrain_changed));
    CHECK(coop_world_event_interpreter::is_replicated(evt::furniture_changed));
    CHECK(coop_world_event_interpreter::is_replicated(evt::field_created));
    CHECK(coop_world_event_interpreter::is_replicated(evt::field_changed));
    CHECK(coop_world_event_interpreter::is_replicated(evt::field_expired));
    CHECK_FALSE(coop_world_event_interpreter::is_replicated(evt::creature_moved));
    CHECK_FALSE(coop_world_event_interpreter::is_replicated(evt::creature_died));
    CHECK_FALSE(coop_world_event_interpreter::is_replicated(evt::creature_spawned));
    CHECK_FALSE(coop_world_event_interpreter::is_replicated(evt::creature_hp));
    CHECK_FALSE(coop_world_event_interpreter::is_replicated(evt::item_spawned));
    CHECK_FALSE(coop_world_event_interpreter::is_replicated(evt::item_removed));
    CHECK_FALSE(coop_world_event_interpreter::is_replicated(evt::turn_advanced));

    // coop_collect_streamable filters through the same predicate.
    auto event_with = [](const coop_event_type type, const int value) {
        coop_world_event ev;
        ev.type = type;
        ev.value = value;
        return ev;
    };
    std::vector<coop_world_event> mixed;
    mixed.push_back(event_with(evt::terrain_changed, 1));
    mixed.push_back(event_with(evt::creature_moved, 2));
    mixed.push_back(event_with(evt::field_expired, 3));
    mixed.push_back(event_with(evt::item_spawned, 4));
    const auto collected = coop_collect_streamable(std::move(mixed));
    REQUIRE(collected.sent.size() == 2);
    CHECK(collected.sent[0].type == evt::terrain_changed);
    CHECK(collected.sent[1].type == evt::field_expired);
}

TEST_CASE("world event interpreter leaves non-replicated events alone", "[coop][rollback]") {
    setup_world();
    coop_rollback_engine engine;
    const ter_id before = g->m.ter(TILE);

    coop_world_event ev;
    ev.type = coop_event_type::creature_moved;
    ev.pos = map_local_to_abs(g->m, TILE);
    ev.value = 42;
    const coop_recorded_event rec = coop_world_event_interpreter::apply(g->m, ev);

    CHECK(g->m.ter(TILE) == before);
    CHECK(rec.type == coop_event_type::creature_moved);
    // Nothing was mutated, so nothing can be undone.
    coop_world_event_interpreter::apply_inverse(g->m, coop_world_event_interpreter::invert(rec));
    CHECK(g->m.ter(TILE) == before);
}
