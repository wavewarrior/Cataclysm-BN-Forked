/**
 * Characterisation tests for the World event Rollback round-trip (commit 1 of
 * plans/coop-world-event-interpreter.md).
 *
 * Shape of every case: build a map, apply one World event's FORWARD effect the
 * way `coop_client::apply_sync` does (capturing the old value the same way),
 * push the recorded event into a `coop_rollback_engine`, call `rollback_to`, and
 * assert the map is back to its original state.
 *
 * The forward-apply helper below is a deliberate transcription of
 * coop_client.cpp:706-774 (the five replicated event types) so these tests pin
 * today's behaviour, defects included, without needing a live socket.
 *
 * KNOWN DEFECTS pinned by the [.] cases (fixed by commit 2, the interpreter):
 *   D1 Double inversion.  `reverse_delta` (coop_mutation_log.cpp:72-98) already
 *      flips field_created <-> field_expired; `rollback_to`
 *      (coop_rollback.cpp:39-63) then switches on the FLIPPED type and applies
 *      inverse semantics a second time.  Rolling back a creation re-adds the
 *      field; rolling back an expiry removes it.
 *   D2 value <-> old_value swap.  `reverse_delta` puts `old_value` into `value`,
 *      and `rollback_to` reads the FIELD TYPE out of `value`.  For
 *      field_created / field_expired the capture stores old_value = 0, so the
 *      inverted event carries a null field type and the add/remove silently
 *      no-ops.  For field_changed the lookup type becomes the old INTENSITY, so
 *      the intensity restore targets the wrong field type entirely.
 *
 * Tags: the passing cases are [coop][rollback].  The known-fail cases carry
 * [.][coop_known_fail][rollback] INSTEAD of [coop] — Catch2 counts a filter that
 * names a tag a hidden case carries as explicitly selecting it, so tagging them
 * [coop] would turn the plain "[coop]" acceptance run red.  Commit 2 restores
 * [coop] on all four.
 */

#include "calendar.h"
#include "catch/catch_amalgamated.hpp"
#include "coop_mutation_log.h"
#include "coop_proto.h"
#include "coop_rollback.h"
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

/// Transcription of the five replicated branches of coop_client::apply_sync
/// (coop_client.cpp:706-774): apply the forward effect to g->m, capture the old
/// value exactly as the client does, and push the recorded event.
auto apply_sync_event(coop_rollback_engine& engine, int ev_type, int ev_val, int ev_cid)
    -> coop_world_event {
    using evt = coop_event_type;
    // map_local_to_abs is the exact inverse of the abs_to_map_local used by both
    // apply_sync and rollback_to, so the round-trip does not depend on the
    // player's reality-bubble origin matching the map origin.
    const tripoint_abs_ms abs_pos = map_local_to_abs(g->m, TILE);
    const tripoint_bub_ms bpos = abs_to_map_local(g->m, abs_pos);
    coop_world_event recorded;
    recorded.type = static_cast<coop_event_type>(ev_type);
    recorded.pos = abs_pos;
    recorded.value = ev_val;
    recorded.creature_id = ev_cid;

    if (ev_type == static_cast<int>(evt::terrain_changed)) {
        const ter_id ter{ev_val};
        recorded.old_value = g->m.ter(bpos).to_i();
        if (ter) { g->m.ter_set(bpos, ter); }
    } else if (ev_type == static_cast<int>(evt::furniture_changed)) {
        recorded.old_value = g->m.furn(bpos).to_i();
        g->m.furn_set(bpos, furn_id{ev_val});
    } else if (ev_type == static_cast<int>(evt::field_created)) {
        const field_type_id ftype{ev_val};
        const int intensity = ev_cid > 0 ? ev_cid : 1;
        recorded.old_value = 0; // no field existed before
        if (ftype) { g->m.add_field(bpos, ftype, intensity, 0_turns); }
    } else if (ev_type == static_cast<int>(evt::field_changed)) {
        const field_type_id ftype{ev_val};
        const int new_int = ev_cid;
        recorded.old_value = 0;
        if (ftype && new_int > 0) {
            field_entry* fe = g->m.get_field(bpos).find_field(ftype);
            if (fe) {
                recorded.old_value = fe->get_field_intensity();
                fe->set_field_intensity(new_int);
            }
        }
    } else if (ev_type == static_cast<int>(evt::field_expired)) {
        recorded.old_value = 0; // field existed before expiration
        g->m.remove_field(bpos, field_type_id{ev_val});
    }

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

// ── Passing cases: terrain and furniture are genuinely reversible ────────────

TEST_CASE("world event rollback round-trip: terrain_changed", "[coop][rollback]") {
    setup_world();
    coop_rollback_engine engine;

    const int orig_ter = g->m.ter(TILE).to_i();
    REQUIRE(g->m.ter(TILE) == ter_str_id("t_grass"));

    const coop_world_event rec = apply_sync_event(
        engine, static_cast<int>(coop_event_type::terrain_changed), ter_id("t_floor").to_i(), 0);

    REQUIRE(g->m.ter(TILE) == ter_str_id("t_floor"));
    CHECK(rec.old_value == orig_ter);

    CHECK(engine.rollback_to(SYNC_TURN - 1) == 1);
    CHECK(g->m.ter(TILE) == ter_str_id("t_grass"));
    CHECK(engine.size() == 0);
}

TEST_CASE("world event rollback round-trip: furniture_changed", "[coop][rollback]") {
    setup_world();
    coop_rollback_engine engine;

    REQUIRE(g->m.furn(TILE) == furn_str_id("f_null"));

    const coop_world_event rec = apply_sync_event(
        engine, static_cast<int>(coop_event_type::furniture_changed), furn_id("f_locker").to_i(),
        0);

    REQUIRE(g->m.furn(TILE) == furn_str_id("f_locker"));
    CHECK(rec.old_value == furn_id("f_null").to_i());

    CHECK(engine.rollback_to(SYNC_TURN - 1) == 1);
    CHECK(g->m.furn(TILE) == furn_str_id("f_null"));
}

// ── Known-fail: field_created / field_expired (D1 double inversion + D2) ─────

TEST_CASE(
    "world event rollback round-trip: field_created and field_expired",
    "[.][coop_known_fail][rollback]") {
    SECTION("field_created: rollback must remove the created field") {
        setup_world();
        coop_rollback_engine engine;
        REQUIRE(field_at_tile(field_type_str_id("fd_fire")) == nullptr);

        // value = field type, creature_id = intensity (apply_sync:731-743).
        apply_sync_event(engine, static_cast<int>(coop_event_type::field_created),
                         field_type_id("fd_fire").to_i(), 3);
        REQUIRE(field_at_tile(field_type_str_id("fd_fire")) != nullptr);

        // D1: reverse_delta flips the type to field_expired, rollback_to then
        // treats it as "undo an expiry" and ADDs the field back.
        // D2: reverse_delta moves old_value (always 0 here) into value, so the
        // inverted event has a null field type and the add silently no-ops.
        // Either way the field survives the rollback.
        CHECK(engine.rollback_to(SYNC_TURN - 1) == 1);
        CHECK(field_at_tile(field_type_str_id("fd_fire")) == nullptr);
    }

    SECTION("field_expired: rollback must restore the expired field") {
        setup_world();
        coop_rollback_engine engine;
        g->m.add_field(TILE, field_type_id("fd_fire"), 1, 0_turns);
        REQUIRE(field_at_tile(field_type_str_id("fd_fire")) != nullptr);

        apply_sync_event(engine, static_cast<int>(coop_event_type::field_expired),
                         field_type_id("fd_fire").to_i(), 0);
        REQUIRE(field_at_tile(field_type_str_id("fd_fire")) == nullptr);

        // D1: reverse_delta flips the type to field_created, rollback_to then
        // treats it as "undo a creation" and REMOVES the (already gone) field.
        // D2: value becomes old_value = 0, so even the inverted add would have a
        // null field type.  Pinned limit: the expiry event carries no intensity
        // (map_field.cpp:1136), so a correct rollback restores intensity 1.
        CHECK(engine.rollback_to(SYNC_TURN - 1) == 1);
        field_entry* restored = field_at_tile(field_type_str_id("fd_fire"));
        REQUIRE(restored != nullptr);
        CHECK(restored->get_field_intensity() == 1);
    }
}

// ── Known-fail: field_changed (D2 only — the type is not flipped) ────────────

TEST_CASE("world event rollback round-trip: field_changed", "[.][coop_known_fail][rollback]") {
    setup_world();
    coop_rollback_engine engine;

    g->m.add_field(TILE, field_type_id("fd_fire"), 2, 0_turns);
    // Guard the choice of old intensity 2: it must not collide with a real field
    // type present on the tile, or the broken lookup could pass by accident.
    const int fire_idx = field_type_str_id("fd_fire").id().to_i();
    REQUIRE(fire_idx != 2);
    REQUIRE(g->m.get_field(TILE).find_field(field_type_id{2}) == nullptr);

    apply_sync_event(engine, static_cast<int>(coop_event_type::field_changed),
                     field_type_id("fd_fire").to_i(), 3);
    REQUIRE(field_at_tile(field_type_str_id("fd_fire"))->get_field_intensity() == 3);

    // D2: reverse_delta swaps value <-> old_value, so rollback_to looks up
    // field_type_id{ 2 } (whatever type that index is) instead of fd_fire and
    // restores nothing.
    CHECK(engine.rollback_to(SYNC_TURN - 1) == 1);
    field_entry* fe = field_at_tile(field_type_str_id("fd_fire"));
    REQUIRE(fe != nullptr);
    CHECK(fe->get_field_intensity() == 2);
}
