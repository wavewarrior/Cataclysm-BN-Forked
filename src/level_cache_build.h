#pragma once

#include "coordinates.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class Character;
class map;
struct sound_event;

/**
 * Builders of the per-z-level derived data of a map: the transparency, floor, outside,
 * absorption and suspension caches, the sunlight and light maps, the seen cache, and the
 * sound caches.
 *
 * Free functions over the accessors of `map`, so editing a builder does not rebuild every
 * translation unit that includes `map.h`. A builder reports what it rebuilt; deciding which
 * builders to run is the job of `level_cache_freshness`.
 */
namespace cache_build {

// Builds a transparency cache and returns true if the cache was invalidated.
// Used to determine if seen cache should be rebuilt.
auto build_transparency_cache(map& here, int zlev) -> bool;
auto build_transparency_caches(map& here, int minz, int maxz) -> std::vector<int>;
// Refreshes the weather-transparency lookup table if the sight penalty has changed. Must be
// called once serially before any parallel call to build_transparency_cache() to avoid a
// data race on the shared table.
auto update_weather_transparency_lookup(map& here) -> void;
auto build_vision_transparency_cache(map& here, const Character& player) -> bool;
// fills lm with sunlight. pzlev is current player's zlevel
auto build_sunlight_cache(map& here, int pzlev) -> void;
// Recomputes sun direction and scatter factor from the current game time.
auto update_solar_params(map& here) -> void;
enum class direct_sunlight_state : int { none, shadow, direct };
// Distinguishes roofed tiles, angled-sun shadow, and full direct sun.
auto direct_sunlight_state_at(const map& here, point_bub_ms p, int zlev) -> direct_sunlight_state;
auto has_direct_sunlight_at(const map& here, point_bub_ms p, int zlev) -> bool;
auto current_lightmap_source_signature(map& here) -> std::size_t;

// Rebuilds outside_caches for zlev top-down:
// A tile is outside if any neighbour in the 3x3 at z+1 is (outside AND has no floor).
// A tile is sheltered if any neighbour in the 3x3 at z+1 has a floor or is itself sheltered.
// Recursively ensures zlev+1 is current before proceeding.
auto build_outside_cache(map& here, int zlev) -> void;
// Builds a floor cache and returns true if the cache was invalidated.
// Used to determine if seen cache should be rebuilt.
auto build_floor_cache(map& here, int zlev) -> bool;
// We want this visible in `game`, because we want it built earlier in the turn than the rest
auto build_floor_caches(map& here) -> void;
// Checks all suspended tiles on a z level and adds those that are invalid to the
// support_dirty_cache
auto update_suspension_cache(map& here, const int& z) -> void;
// Builds a sound absorption cache and returns true if the cache was invalidated.
// If true, update the absorption cache. We want this built after the other caches, but before
// sounds are calced. Function logic located in sounds.cpp
auto build_absorption_cache(map& here, int zlev) -> bool;
// Builds a sound_instance_cache by flood filling from a given sound event.
// Function logic located in sounds.cpp
auto flood_fill_sound(map& here, sound_event soundevent, int zlev) -> void;
// Batch builds a set of sound caches from std::vector<sound_event> sound_batch_floodfill_que
// Similar to flood_fill_sound, used for monster sounds for performance.
auto batch_flood_fill_sounds(map& here) -> void;
// Checks and culls sound_instance_caches from the sound_instance_caches vector.
// Sounds that have been heard by monsters and by the player are culled so they are not
// re-heard.
auto cull_heard_sounds(map& here) -> void;

// When skip_shared_init is true the caller has already: cleared sm/lsb for this level, called
// build_sunlight_cache() once, and applied character lights. The function then processes only
// entities whose position z matches zlev, avoiding cross-level cache writes for parallel safety.
auto generate_lightmap(map& here, int zlev) -> void;
auto generate_lightmap_worker(map& here, int zlev) -> void;
auto flush_lightmap_cpu_read_counters(const map& here) -> void;
auto build_seen_cache(map& here, const tripoint_bub_ms& origin, int target_z) -> void;
auto vision_transparency_block_mask(const map& here) -> std::uint32_t;
// Applies vehicle mirror/camera FOV from @p origin's vehicle.
// Separated from build_seen_cache for readability and Tracy granularity.
auto apply_vehicle_optics(map& here, const tripoint_bub_ms& origin, int target_z) -> void;
auto apply_character_light(map& here, Character& who) -> void;

// Adds/removes player specific transparencies
auto apply_vision_transparency_cache(
    map& here, const tripoint_bub_ms& center, int target_z, float (&vision_restore_cache)[9],
    bool (&blocked_restore_cache)[8]) -> void;
auto restore_vision_transparency_cache(
    map& here, const tripoint_bub_ms& center, int target_z, float (&vision_restore_cache)[9],
    bool (&blocked_restore_cache)[8]) -> void;

} // namespace cache_build
