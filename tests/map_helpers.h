#pragma once
#ifndef CATA_TESTS_MAP_HELPERS_H
#    define CATA_TESTS_MAP_HELPERS_H

#    include "coordinates.h"
#    include "type_id.h"

#    include <initializer_list>
#    include <string>

class monster;
class time_point;

void wipe_map_terrain();
void clear_creatures();
void clear_npcs();
void clear_fields(int zlevel);
void clear_items(int zlevel);
void clear_map();
void clear_overmap();
void put_player_underground();
auto move_player_out_of_the_way() -> void;
monster& spawn_test_monster(const std::string& monster_type, const tripoint_bub_ms& start);
void clear_vehicles();
void build_test_map(const ter_id& terrain);
void build_water_test_map(const ter_id& surface, const ter_id& mid, const ter_id& bottom);
void set_time(const time_point& time);

/**
 * Bring the Level cache to a fresh state through the same public refresh path
 * production uses (`game::refresh_player_visibility_cache_if_needed`).
 *
 * Invalidates the Level cache of every z-level in `invalidate_zlevels` (the
 * player's z-level when the list is empty), then runs the standard refresh.
 * `skip_lightmap` is forwarded to the refresh's lightmap knob.
 */
void refresh_level_cache(
    std::initializer_list<int> invalidate_zlevels = {}, bool skip_lightmap = false);

/**
 * Structure-only variant of refresh_level_cache: invalidate then rebuild the
 * Level cache of one z-level without running the visibility refresh. Matches
 * the hand-rolled invalidate/build pairs it replaces.
 */
void rebuild_level_cache(int zlev, bool skip_lightmap = true);

#endif // CATA_TESTS_MAP_HELPERS_H
