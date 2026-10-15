#pragma once

#include "coordinates.h"

class map;

/**
 * Monster spawning for a map: placing the monsters stored in submap spawn points and the
 * monster groups waiting on the overmap into the reality bubble.
 *
 * Free functions over the public accessors of `map`, so a consumer does not rebuild when the
 * spawning code changes.
 */
namespace map_spawn {

/**
 * Spawn monsters from submap spawn points and from the overmap.
 * @param ignore_sight If true, monsters may spawn in the view of the player
 */
auto spawn_monsters(map& here, bool ignore_sight) -> void;

/**
 * Like spawn_monsters(), but only processes the strip of submaps that newly entered the
 * reality bubble due to a map shift. Avoids re-processing already-loaded submaps and
 * spuriously placing stale monster_map entries that correspond to already-active monsters.
 */
auto spawn_monsters_new_submaps(map& here, const point_rel_sm& shift_amount) -> void;

} // namespace map_spawn
