#pragma once

#include "cata_dynamic_bitset.h"
#include "coordinates.h"
#include "lightmap.h"
#include "shadowcasting.h"
#include "vehicle_handle.h"

#include <cstdint>
#include <list>
#include <map>
#include <set>
#include <vector>

struct level_cache {
    // Zeros all relevant values.
    // Default constructor creates a zero-sized cache used as a null sentinel only.
    level_cache();
    // Normal constructor: mx = SEEX * mapsize, my = SEEY * mapsize.
    explicit level_cache(int mx, int my);
    level_cache(const level_cache& other) = default;
    level_cache& operator=(const level_cache& other) = default;

    // Runtime dimensions for this cache.
    // cache_x = SEEX * mapsize, cache_y = SEEY * mapsize, cache_mapsize = mapsize.
    int cache_x = 0;
    int cache_y = 0;
    int cache_mapsize = 0;

    /// Flat index for tile-coordinate arrays: vec[x * cache_y + y].
    /// Uses the runtime cache_y stride (= SEEY * mapsize) so that all
    /// vector accesses correctly reflect the actual loaded-area dimensions.
    auto idx(int x, int y) const -> int { return x * cache_y + y; }
    // Flat index for submap-coordinate bitsets: bitset[sx * cache_mapsize + sy]
    int bidx(int sx, int sy) const { return sx * cache_mapsize + sy; }
    /// True if the tile-coordinate point_bub_ms & is within this cache's rendered area.
    bool inbounds(const point_bub_ms& p) const {
        return p.x() >= 0 && p.x() < cache_x && p.y() >= 0 && p.y() < cache_y;
    }

    // ---- per-submap dirty bitsets (size: cache_mapsize²) ----
    cata_dynamic_bitset transparency_cache_dirty;
    // P3: monotonically-increasing generation counter, bumped whenever
    // `level_cache_freshness::mark_transparency` is called. Render code compares against
    // its last-seen value to decide whether the SDF needs rebuilding — avoids
    // rebuilding on every combat turn when terrain hasn't actually changed.
    std::uint64_t transparency_generation = 0;
    // Companion to transparency_generation, for the SAME reason but a different
    // consumer: the lighting structure snapshot derives sky_vis from outside_cache, and
    // that snapshot could previously go permanently stale. MEASURED with timestamps: two
    // structure rebuilds read outside_true=0/32400 at 20:44:04.504 and .527 while
    // build_outside_cache reported 30586/32400 at 20:44:04.828 — the consumer ran ~300ms
    // FIRST, and because the rebuild only re-fired on a transparency-generation or
    // bubble-origin change, SkyVisBuf stayed all-zero for the whole session. sprite.frag
    // gates its entire sun term on `sky_vis > 0.05`, so daylight rendered with no
    // directional term and no cast shadows anywhere. Bumped whenever outside_cache is
    // actually rebuilt, so the render side can notice and re-snapshot.
    std::uint64_t outside_generation = 0;
    // Content hash of outside_cache at the last bump. outside_cache_dirty is set far
    // more often than the CONTENT changes -- every moving vehicle part dirties it, and
    // mark_floor() always dirties the level below -- so bumping the
    // generation on every rebuild would force a full lighting re-snapshot on each
    // vehicle move and door open, which is exactly the cost the generation counters
    // exist to avoid. Bump only when this hash actually changes.
    std::uint64_t outside_checksum = 0;
    cata_dynamic_bitset outside_cache_dirty;
    cata_dynamic_bitset floor_cache_dirty;
    // absorption_cache_dirty is for tile sound absorption checking/rebuild purposes.
    // Should be set for a tile position if the tile in question changes significantly, or if a tile
    // feature that affects sound propagation is added/removed.
    cata_dynamic_bitset absorption_cache_dirty;

    bool seen_cache_dirty = false;
    // Per-submap dirty bitset for lightmap, parallel to transparency_cache_dirty
    // etc.  Set at the start of each game turn (all bits), cleared per submap
    // after generate_lightmap recomputes that submap.  Allows incremental regen
    // within a level and skip of levels with no dirty submaps.
    cata_dynamic_bitset lightmap_dirty;
    // True when CPU lm contains current lighting for the whole level. SDL GPU
    // lighting may keep resident GPU lm current while leaving this false.
    bool lm_cpu_cache_valid = false;
    // Incremented whenever CPU lm contents are invalidated before a rebuild.
    uint64_t lm_cpu_cache_generation = 0;
    // Issue #20: event-based per-level residency generation, polled by the GPU
    // residency layer (spec #8, user stories 18/19). Advances on EVERY change kind
    // the freshness module reports, for every level the kind affects, EVEN WHEN the
    // resulting content is identical — residency work that depends on the event
    // rather than the content must not be skipped. Distinct from the two
    // Structure-rebuild generations above, which stay content/event gated exactly
    // as before.
    std::uint64_t residency_generation = 0;
    // Per-level visibility dirtiness. The map-level aggregate flag is the source
    // of truth for gameplay consumers that need completed player visibility.
    bool visibility_cache_dirty = true;
    bool has_any_vehicle_floor = false;
    bool suspension_cache_initialized = false;
    bool suspension_cache_dirty = false;
    std::list<point_abs_ms> suspension_cache;

    // ---- 12 tile-coordinate arrays (size: cache_x * cache_y) ----
    // All indexed as: vec[x * cache_y + y]  (X-outer layout, matching old C-array
    // [MAPSIZE_X][MAPSIZE_Y])
    std::vector<float> lm;
    std::vector<float> sm;
    // To prevent redundant ray casting into neighbors: precalculate bulk light source positions.
    // This is only valid for the duration of generate_lightmap
    std::vector<float> light_source_buffer;
    std::vector<float> colored_light_source_buffer;
    std::vector<uint32_t> light_source_color_buffer;
    // Accumulated colored light energy per tile. Populated during generate_lightmap
    // alongside lm/sm. Zero = uncolored (white) light only.
    std::vector<light_color_rgb> light_color_cache;
    // True when at least one light source on this z-level has non-white color.
    // Used to skip the color blur pass when all lights are white.
    bool has_colored_lights = false;
    // Source tiles touched in light_source_buffer.
    std::vector<point_bub_ms> light_source_points;

    // True when the tile has sky access via the 3×3 overhang rule (top-down floor cascade).
    // False means fully enclosed — protected from rain, wind, weather effects.
    std::vector<char> outside_cache;

    // True when at least one tile within 3×3 above has overhead coverage (floor or sheltered
    // tile at z+1).  Distinct from outside_cache: a tile can be outside yet sheltered (overhang).
    std::vector<char> sheltered_cache;

    // true when vehicle below has "ROOF" or "OPAQUE" part, furniture below has "SUN_ROOF_ABOVE"
    //      or terrain doesn't have "NO_FLOOR" flag
    // false otherwise
    // i.e. non-zero == has floor
    // Stored as char (not bool) for contiguous storage; non-zero means true.
    std::vector<char> floor_cache;

    // Subset of floor_cache: entries contributed by vehicle ROOF or OPAQUE parts only
    // (set by vehicle_caching_internal_above).  Used to exclude vehicle-derived floors
    // from floor_crossing_blocked and to stamp a fixed roof-shadow pass.
    // Stored as char for contiguous storage; non-zero means true.
    std::vector<char> vehicle_floor_cache;

    // stores cached transparency of the tiles
    // units: "transparency" (see LIGHT_TRANSPARENCY_OPEN_AIR)
    std::vector<float> transparency_cache;

    // true when light entering a tile diagonally is blocked by the walls of a turned vehicle. The
    // direction is the direction that the light must be travelling. check the nw value of x+1, y+1
    // to find the se value of a tile and the ne of x-1, y+1 for sw
    std::vector<diagonal_blocks> vehicle_obscured_cache;

    // same as above but for obstruction rather than light
    std::vector<diagonal_blocks> vehicle_obstructed_cache;

    // stores "visibility" of the tiles to the player
    // values range from 1 (fully visible to player) to 0 (not visible)
    std::vector<float> seen_cache;

    // same as `seen_cache` (same units) but contains values for cameras and mirrors
    // effective "visibility_cache" is calculated as "max(seen_cache, camera_cache)"
    std::vector<float> camera_cache;

    // stores resulting apparent brightness to player, calculated by map::apparent_light_at
    std::vector<lit_level> visibility_cache;

    std::vector<uint32_t> colored_light_cache;
    bool colored_light_cache_active = false;

    // per-tile map-memory seen bitset (size: cache_x * cache_y), indexed [x + y * cache_x]
    cata_dynamic_bitset map_memory_seen_cache;
    std::vector<tripoint_bub_ms> map_memory_seen_cache_dirty_points;
    bool map_memory_seen_cache_dirty_all = true;

    bool veh_in_active_range = false;
    std::vector<bool> veh_exists_at;
    std::map<tripoint_bub_ms, std::pair<vehicle_handle, int>> veh_cached_parts;
    std::set<vehicle_handle> vehicle_list;
    std::set<vehicle_handle> zone_vehicles;

    // stores cached sound absorption amounts of tiles
    // In 100ths of decibels
    // This is in level_cache instead of sound cache as this is a function of terrain,
    // and we dont want to regenerate this for every single sound.
    std::vector<short> absorption_cache;
    // sound_wall_cache is set alongside the absorption cache, and is used during the process for
    // floodfilling sounds.
    std::vector<bool> sound_wall_cache;
};
