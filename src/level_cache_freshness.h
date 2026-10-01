#pragma once
#ifndef CATA_SRC_LEVEL_CACHE_FRESHNESS_H
#define CATA_SRC_LEVEL_CACHE_FRESHNESS_H

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <vector>

#include "coordinates.h"
#include "cuboid_rectangle.h"
#include "enum_bitset.h"
#include "enum_traits.h"

struct level_cache;
class map;

/**
 * One piece of a Level cache whose freshness this module tracks.
 *
 * The set is closed: every dirty/valid bit and per-level flag of the Level cache
 * is addressed through one of these names. See GLOSSARY.md for "Level cache"
 * and "Level cache freshness".
 */
enum class level_cache_part : int {
    transparency,          ///< per-submap transparency_cache_dirty
    outside,               ///< per-submap outside_cache_dirty
    floor,                 ///< per-submap floor_cache_dirty
    absorption,            ///< per-submap absorption_cache_dirty
    sound_wall,            ///< per-submap sound_wall_cache_dirty
    lightmap,              ///< per-submap lightmap_dirty
    seen,                  ///< per-level seen_cache_dirty
    visibility,            ///< per-level visibility_cache_dirty
    lm_valid,              ///< per-level lm_cpu_cache_valid (inverted freshness)
    suspension_dirty,      ///< per-level suspension_cache_dirty
    suspension_init,       ///< per-level suspension_cache_initialized
    vehicle_caches,        ///< per-level vehicle_caches_dirty
    vehicle_floor,         ///< per-level vehicle_floor_cache_dirty
    has_any_floor,         ///< per-level has_any_floor, stamped by the floor builder
    has_any_vehicle_floor, ///< per-level has_any_vehicle_floor
    colored_light_active,  ///< per-level colored_light_cache_active
    veh_in_active_range,   ///< per-level veh_in_active_range
    memory_seen_all,       ///< per-level map_memory_seen_cache_dirty_all
    NUM
};

template<>
struct enum_traits<level_cache_part> {
        static constexpr level_cache_part last = level_cache_part::NUM;
};

/** The parts that are per-submap bitsets rather than per-level flags. */
enum_bitset<level_cache_part> level_cache_bitset_parts();

/** Assemble a part set; the spelling callers use for coupled families. */
inline enum_bitset<level_cache_part> freshness_parts(
    std::initializer_list<level_cache_part> list ) {
    enum_bitset<level_cache_part> parts;
    for( const level_cache_part part : list ) {
        parts.set( part );
    }
    return parts;
}
/**
 * Level cache freshness: the single owner of Level cache freshness state.
 *
 * Interface — everything a caller must know:
 *
 * - Verbs record a fact about freshness. They deliberately do NOT couple
 *   families: marking transparency stale does not mark absorption stale. The
 *   coupling rules (transparency implies absorption, floor implies the level
 *   below, a vehicle move implies the level above) live in the `map::set_*`,
 *   `map::mark_*` and `map::invalidate_*` helpers, which compose these verbs.
 *   Those helpers are the transitional per-family surface that change kinds
 *   will replace.
 * - Mutating verbs are main-thread only and assert it, following the existing
 *   `is_pool_worker_thread()` pattern. Exception: the per-level `clear` and
 *   `assign`, which the parallel region of `map::build_map_cache` needs. Those
 *   may only ever be called for the caller's own level — a worker that cleared
 *   another level's state would race with that level's builder.
 *   The assertion is an `assert`, so it is compiled out of the shipped
 *   `RelWithDebInfo` build (`-DNDEBUG`): it cannot fail a test run, which is what lets
 *   this refactor claim "no behaviour change". It is a development aid, not enforcement;
 *   every asserted verb was checked to have only main-thread callers.
 * - `mark`/`clear`/`assign`/`translate` take the `level_cache` rather than a
 *   z-level for the same reason: the caller has already resolved which level it
 *   owns, and the module never guesses.
 * - Reads are not part of this interface. Readers keep using the `level_cache`
 *   fields and `map::visibility_caches_dirty()`, so their freshness assumptions
 *   are unchanged.
 * - Nothing here decides whether a rebuild runs. In particular the map-wide
 *   visibility aggregate is only ever set true by `mark_visibility`. The clearing verbs
 *   exist for completeness, but the only caller that reaches them is
 *   `map::mark_visibility_caches_clean`, which nothing calls, so in practice the
 *   aggregate stays true once raised. That asymmetry is load-bearing today and is
 *   replaced by "view stale" in a later ticket.
 */
class level_cache_freshness
{
    public:
        /** Mark a whole level stale: every bit of each named bitset, or the flag. */
        static void mark( level_cache &cache, const enum_bitset<level_cache_part> &parts );
        /** Mark one submap of a level stale; `bit` indexes that level's bitsets. */
        static void mark( level_cache &cache, const enum_bitset<level_cache_part> &parts,
                          size_t bit );
        /** Mark a whole level fresh. Per-level-builder side: workers allowed. */
        static void clear( level_cache &cache, const enum_bitset<level_cache_part> &parts );
        /** Set named per-level flags to an explicit value. Workers allowed, own level only. */
        static void assign( level_cache &cache, const enum_bitset<level_cache_part> &parts,
                            bool value );
        /**
         * Move the dirty bits of a level along with the caches they describe when the
         * reality bubble shifts, so retained submaps stay fresh and only the shifted-in
         * edge has to be rebuilt. `mapsize` is the map's submap extent per axis.
         */
        static void translate( level_cache &cache, const enum_bitset<level_cache_part> &parts,
                               const point_rel_sm &shift, int mapsize );

        /** Advance the transparency generation the renderer polls for a structure rebuild. */
        static void advance_transparency_generation( level_cache &cache );
        /** Declare the CPU lightmap unusable and advance the generation it is memoed by. */
        static void invalidate_cpu_lightmap( level_cache &cache );
        /** Declare the CPU lightmap current for the whole level. */
        static void validate_cpu_lightmap( level_cache &cache );
        /**
         * Record the outside-cache content hash, advancing the outside generation only
         * when the content actually moved. The generation is a Structure-rebuild signal
         * for the renderer, so a bump on mere dirtiness would defeat it.
         */
        static void record_outside_content( level_cache &cache, std::uint64_t checksum );

        /**
         * A level's visibility is stale: the per-level bit plus the map-wide aggregate.
         * Out-of-range `zlev` is ignored, matching the helper this replaces.
         */
        static void mark_visibility( map &who, int zlev );
        /** Clear the visibility bit of a z-range only; the map-wide aggregate is untouched. */
        static void clear_visibility( map &who, int min_z, int max_z );
        /** Clear every level's visibility bit and the map-wide aggregate. */
        static void clear_visibility( map &who );
        /** Forget which viewer the seen cache was built for, forcing the next rebuild. */
        static void forget_seen_origin( map &who );
        /** Record the viewer position the seen cache was just built for. */
        static void stamp_seen_origin( map &who, const tripoint_bub_ms &origin );
        /**
         * Bookkeeping pair for a vehicle entering or leaving a level's caches: the
         * vehicle-only caches of `zlev` and the vehicle-floor cache one level above it.
         * Both parts are write-only today (issue #2) and disappear with #22; the verb
         * exists so vehicle code never raises a freshness bit directly.
         */
        static void mark_vehicle_caches( map &who, int zlev );
        /**
         * Forget both solar stamps so the sunlight cascade recomputes. Only
         * `invalidate_map_cache` needs the pair; the other two writers below each touch
         * one field, and resetting the wrong one forces a full sunlight rebuild.
         */
        static void forget_solar_stamps( map &who );
        /** Forget only the game-hour stamp, leaving the light-level gate intact. */
        static void forget_solar_hour( map &who );
        /** Record the light level the sunlight cascade just built for. */
        static void stamp_solar_light_level( map &who, int light_level_int );
        /**
         * Compare a lightmap source signature against the stored one. Stores it and
         * returns true — meaning every lightmap must be rebuilt — when it differs or was
         * not known yet; returns false when the light source set is unchanged.
         */
        static bool note_lightmap_source_signature( map &who, std::size_t signature );

        /** Report a tile that lost its support, for the next fall check. */
        static void support_lost( map &who, const tripoint_bub_ms &p );
        /** Take the tiles reported unsupported since the last fall check. */
        static std::set<tripoint_bub_ms> take_support_losses( map &who );
        /** Move the pending support losses along with a shifted reality bubble. */
        static void translate_support_losses( map &who, const point_rel_ms &shift,
                                              const half_open_rectangle<point_bub_ms> &boundaries );

        /** Queue one tile for re-memorising, unless the whole level is already queued. */
        static void queue_memory_seen( level_cache &cache, const tripoint_bub_ms &p );
        /** Queue every tile of a level for re-memorising, dropping the point list. */
        static void queue_memory_seen_all( level_cache &cache );
        /** Take the queued re-memorise points of a level. */
        static std::vector<tripoint_bub_ms> take_memory_seen_points( level_cache &cache );
        /** Acknowledge that a level's memorised tiles are all up to date. */
        static void acknowledge_memory_seen( level_cache &cache );

        /** Mark every per-submap cache of a freshly constructed Level cache stale. */
        static void initialise( level_cache &cache );

        // ---- Change kinds -------------------------------------------------

        // Report a change to the world or the viewer. Each kind raises exactly the
        // dependents the bit-setter sequence it replaces raised; the equivalence is
        // pinned per kind in the tests. Like the verbs above, `report` is main-thread
        // only: it asserts via the existing `is_pool_worker_thread()` pattern, and the
        // assertion is compiled out of the shipped build (`-DNDEBUG`).
        // Kinds carry location/level/diff information only.
        //
        // Deliberately left at the call sites, because they are not Level cache
        // freshness: the pathfinding-cache dirt (`map::set_pathfinding_cache_dirty`),
        // the `suspension_cache` emplace accompanying a suspension flag change,
        // `invalidate_max_populated_zlev`, the vehicle zone-dirty walk, and the GPU
        // residency pushes (those move to generation polling in a later stage).

        /**
         * Terrain or furniture changed at one tile. The flags carry the property diff
         * the old `ter_set`/`furn_set` inspected, so the dependents raised match the
         * branch that actually ran rather than a blanket union.
         */
        struct terrain_changed {
            /// How widely the opacity change dirties the transparency cache. Not every
            /// opacity change is tile-sized: a trap-triggered map regen repaints the
            /// whole level of the trap, and a weather change to the sight penalty
            /// invalidates every loaded level.
            enum class transparency_scope {
                tile,       ///< one submap: `ter_set`/`furn_set`, field add/remove
                level,      ///< the whole level of `at`: a trap-triggered map regen
                all_levels, ///< every loaded level: a weather sight-penalty change
            };
            tripoint_bub_ms at;
            /// Opacity flipped: transparency cache and the tile's seen entry.
            bool transparency = false;
            /// Extent over which `transparency` dirties the cache.
            transparency_scope scope = transparency_scope::tile;
            /// TFLAG_NO_FLOOR differs: floor cache plus seen here and one level down.
            bool no_floor = false;
            /// TFLAG_Z_TRANSPARENT differs: same dependents as `no_floor`.
            bool z_transparent = false;
            /// TFLAG_SUN_ROOF_ABOVE differs: floor cache one level up.
            bool sun_roof_above = false;
            /// TFLAG_SUSPENDED differs: suspension cache of this level.
            bool suspended = false;
            /// Invalidate the lightmap of every level. `ter_set` passes true
            /// unconditionally; `furn_set` only when emitted light differs.
            bool lightmap = false;
            /// Queue the support-loss check at `at` itself, as `furn_set` always does;
            /// `ter_set` only does it when `no_floor` is set.
            bool support_here = false;
            /// Queue the support-loss check one level above `at`, as both terrain
            /// mutators do. An opacity-only change (field, trap, weather) moves no
            /// support and passes false.
            bool support_above = true;
            /// Queue `at` for re-memorising, as both terrain mutators do. A change that
            /// leaves the terrain itself alone passes false.
            bool memory_seen = true;
            /// Tile whose seen-cache content gates the seen dirtying that accompanies
            /// `transparency`. The terrain mutators probe the tile that changed; a
            /// vehicle part edit that repaints a whole level probes the bubble origin
            /// instead, and a part edit that knows the part's tile probes that tile.
            /// Defaults to `at`, which is what the terrain mutators do.
            std::optional<tripoint_bub_ms> seen_probe;
        };
        /**
         * A light source appeared, disappeared or changed intensity somewhere.
         * Default: every loaded level's lightmap is stale, which is what every light
         * mutator raises today.
         */
        struct light_changed {
            /// How widely the light change dirties the lightmap. A viewer move inside
            /// the bubble changes only the entity lights of the submap it landed in;
            /// mutators that cannot localise the change take the default.
            enum class lightmap_scope {
                all_levels, ///< every loaded level, as `invalidate_lightmap_caches` does
                tile,       ///< only the submap containing `at`
            };
            tripoint_bub_ms at;
            /// Extent over which the lightmap goes stale.
            lightmap_scope scope = lightmap_scope::all_levels;
            /// Also raise the map-wide visibility aggregate, as the activity-cadence
            /// boundary does alongside the lightmap invalidate.
            bool visibility = false;
        };
        /**
         * A vehicle committed a move covering submap grid cells `sm_min..sm_max` on
         * level `z` — the notification `map::on_vehicle_moved` receives.
         */
        struct vehicle_moved {
            tripoint_bub_sm sm_min;
            tripoint_bub_sm sm_max;
            int z = 0;
        };
        /**
         * The viewer moved within the current bubble. Forces the seen rebuild the
         * origin check triggers today; raises no dirty bit directly.
         */
        struct player_moved {
        };
        /** The viewer changed z-level. Same origin-forcing mechanism as `player_moved`. */
        struct z_level_changed {
        };
        /**
         * The reality bubble shifted by `shift` submaps. Reproduces the shift's
         * per-submap translate plus dirty-edge sequence over every level — NOT a
         * blanket invalidate. `player_z` selects the levels whose seen cache is
         * force-dirtied.
         */
        struct map_shifted {
            point_rel_sm shift;
            int player_z = 0;
        };
        /**
         * The terrain of a whole rectangular area of submaps was replaced underneath
         * the caches (bulk load, co-op tile sync): every Level cache bitset of the
         * area AND the submap dirty flags must rise, or the builders early-return on
         * their clean bitsets and never see the new terrain.
         */
        struct world_replaced {
            tripoint_bub_sm first = tripoint_bub_sm::zero();
            tripoint_bub_sm last = tripoint_bub_sm::zero();
        };

        static void report( map &who, const terrain_changed &change );
        static void report( map &who, const light_changed &change );
        static void report( map &who, const vehicle_moved &change );
        static void report( map &who, const player_moved &change );
        static void report( map &who, const z_level_changed &change );
        static void report( map &who, const map_shifted &change );
        static void report( map &who, const world_replaced &change );

        // ---- Freshness reads (queries) ------------------------------------

        /** True when the named part of a level is not fresh. */
        static bool stale( const level_cache &cache, level_cache_part part );
        /** True when one submap bit of a per-submap part is not fresh. */
        static bool stale( const level_cache &cache, level_cache_part part, size_t bit );
        /** The Structure-rebuild generation the renderer polls for transparency. */
        static std::uint64_t transparency_generation( const level_cache &cache );
        /** The Structure-rebuild generation tied to outside-cache content. */
        static std::uint64_t outside_generation( const level_cache &cache );
        /** The generation the CPU lightmap memo is keyed by. */
        static std::uint64_t cpu_lightmap_generation( const level_cache &cache );
        /** The map-wide aggregate: does a gameplay consumer need a full refresh? */
        static bool visibility_stale( const map &who );
};

#endif // CATA_SRC_LEVEL_CACHE_FRESHNESS_H
