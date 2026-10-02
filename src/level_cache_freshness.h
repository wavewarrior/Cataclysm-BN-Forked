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
#include "type_id.h"

struct level_cache;
class map;
class submap;

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
    lightmap,              ///< per-submap lightmap_dirty
    seen,                  ///< per-level seen_cache_dirty
    visibility,            ///< per-level visibility_cache_dirty
    lm_valid,              ///< per-level lm_cpu_cache_valid (inverted freshness)
    suspension_dirty,      ///< per-level suspension_cache_dirty
    suspension_init,       ///< per-level suspension_cache_initialized
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
 * - Verbs record a fact about freshness. The low-level `mark`/`clear` verbs
 *   deliberately do NOT couple families: marking transparency stale does not
 *   mark absorption stale. The coupling rules (transparency implies absorption,
 *   floor implies the level below, a vehicle move implies the level above) live
 *   in the per-cache `mark_*` and `invalidate_*` verbs below, which compose the
 *   low-level ones. Those verbs are the successors of the deleted
 *   `map::set_*_cache_dirty` helpers; the change kinds compose them further.
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
 * - Reads go through the freshness queries at the bottom of this interface and the
 *   `level_cache` fields; nothing else reads freshness state.
 * - Whether a rebuild runs is decided by the view-stale condition, spelled by
 *   `visibility_stale`: the visibility cache is stale when the viewer is not the
 *   one the seen cache was built for (origin or z-level moved) or when any loaded
 *   level's visibility bit is dirty. There is no map-wide sticky aggregate.
 * - Geometry-only visibility is the read contract: while a level is stale,
 *   visibility queries answer from line of sight only. A reader that needs exact
 *   visibility refreshes first (`game::refresh_player_visibility_cache_if_needed`).
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
        /**
         * Advance the per-level residency generation. Unlike the two Structure-rebuild
         * generations, this one is event-based: it advances on every reported change
         * kind affecting the level, even when the resulting content is identical, so a
         * residency consumer that polls it never misses an event (issue #20). The
         * change-kind verbs call it; direct callers should report a kind instead.
         */
        static void advance_residency_generation( level_cache &cache );
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
         * A level's visibility is stale: its per-level bit, which is what the
         * view-stale condition reads. Out-of-range `zlev` is ignored.
         */
        static void mark_visibility( map &who, int zlev );
        /** Clear the visibility bit of a z-range: a builder reporting what it rebuilt. */
        static void clear_visibility( map &who, int min_z, const int max_z );
        /** Clear every level's visibility bit. */
        static void clear_visibility( map &who );
        /** Forget which viewer the seen cache was built for, forcing the next rebuild. */
        static void forget_seen_origin( map &who );
        /** Record the viewer position the seen cache was just built for. */
        static void stamp_seen_origin( map &who, const tripoint_bub_ms &origin );
        /** Record the viewer the visibility cache was just rebuilt for. */
        static void stamp_visibility_origin( map &who, const tripoint_bub_ms &origin );
        /**
         * Forget both solar stamps so the sunlight cascade recomputes. Only
         * `invalidate_level` needs the pair; the other two writers below each touch
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

        // ---- Per-cache staleness verbs ----------------------------------
        //
        // Successors of the `map::set_*_cache_dirty` helpers. Each carries the
        // couplings of the helper it replaces, so a caller states which cache went
        // stale and never assembles a subset of bits.

        /** Transparency cache of a whole level, plus the absorption cache derived from it. */
        static void mark_transparency( map &who, int zlev );
        /** Transparency cache of the submap containing `p`. */
        static void mark_transparency( map &who, const tripoint_bub_ms &p );
        /** Outside cache of a whole level. */
        static void mark_outside( map &who, int zlev );
        /** Outside cache of the tile's submap and its boundary neighbours. */
        static void mark_outside( map &who, const tripoint_bub_ms &p );
        /** Floor cache of a whole level, cascading outside and absorption one level down. */
        static void mark_floor( map &who, int zlev );
        /** Floor cache of the tile's submap, cascading outside and absorption below. */
        static void mark_floor( map &who, const tripoint_bub_ms &p );
        /** Sound absorption cache of a whole level. */
        static void mark_absorption( map &who, int zlev );
        /** Sound absorption cache of the tile's submap and its boundary neighbours. */
        static void mark_absorption( map &who, const tripoint_bub_ms &p );
        /** Suspension cache of a whole level. */
        static void mark_suspension( map &who, int zlev );
        /** Seen cache of a whole level, unconditionally. */
        static void mark_seen( map &who, int zlev );
        /** Seen cache of `p`'s level, only where `p` was actually remembered. */
        static void mark_seen( map &who, const tripoint_bub_ms &p );
        /** Lightmap of the single submap containing `p`. */
        static void mark_lightmap( map &who, const tripoint_bub_ms &p );
        /** Lightmap of every loaded level, with the CPU lightmap memo and visibility. */
        static void invalidate_lightmap( map &who );
        /** Visibility cache of every loaded level, plus the map-wide aggregate. */
        static void invalidate_visibility( map &who );
        /** Forget one remembered tile and queue it for re-memorising. */
        static void mark_memory_seen( map &who, const tripoint_bub_ms &p );
        /** Forget everything remembered on a level and queue a full re-memorise. */
        static void mark_memory_seen( map &who, int zlev );
        /**
         * Escape hatch, coarse by design: every Level cache of one z-level is stale.
         *
         * This is the literal content of the old `map::invalidate_map_cache`, kept for
         * the sites that genuinely mean "rebuild this whole level" — a viewer re-centre,
         * a save/load restore, a teleport, a z jump in the map editor. No change kind
         * covers it and the ticket forbids inventing one. Callers that know what
         * changed use a kind or one of the verbs above instead.
         */
        static void invalidate_level( map &who, int zlev );

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
        // `invalidate_max_populated_zlev`, and the vehicle zone-dirty walk. The GPU
        // residency pushes became generation polling in ticket #21.

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
            /// A roof or opaque part came off: the whole floor cache of `at`'s level, as
            /// `mark_floor( who, zlev )` does, instead of the one-tile shape of
            /// `no_floor`/`sun_roof_above`. A vehicle part edit repaints the level.
            bool floor_level = false;
        };
        /**
         * The terrain at one tile was replaced: the caller states the FACT of a
         * replacement (which id was there, which id is there now) and the module
         * computes the Property diff. Use this wherever the old/new ids are known —
         * the terrain mutators and the co-op remote-apply arms. Sites that know only
         * an effect (a field's opacity, a trap's regen, a weather sight penalty) keep
         * constructing `terrain_changed` directly.
         *
         * `raise_absorption` is the per-kind policy field: the diff evaluates the
         * three absorption predicates (NO_FLOOR, BLOCK_WIND, CONNECT_TO_WALL)
         * uniformly, but only a caller that knows the local setters did NOT already
         * raise the sound-absorption cache — a remote-apply arm replaying a change
         * that happened on another machine — turns this on. Off for the local setters.
         */
        struct terrain_replaced {
            tripoint_bub_ms at;
            ter_id old_id;
            ter_id new_id;
            bool raise_absorption = false;
        };
        /**
         * The furniture at one tile was replaced. Same contract as `terrain_replaced`,
         * with the furniture diff: the suspension predicate never fires (furniture
         * carries no SUSPENDED semantics here), and the lightmap raises on an emitted
         * light OR opacity flip.
         */
        struct furniture_replaced {
            tripoint_bub_ms at;
            furn_id old_id;
            furn_id new_id;
            bool raise_absorption = false;
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
                all_levels, ///< every loaded level, as `invalidate_lightmap` does
                tile,       ///< only the submap containing `at`
                none,       ///< lighting untouched; only the `seen`/`visibility` flags below
            };
            tripoint_bub_ms at;
            /// Extent over which the lightmap goes stale.
            lightmap_scope scope = lightmap_scope::all_levels;
            /// Also mark every loaded level's visibility stale, so the view-stale
            /// condition forces a rebuild: the activity-cadence boundary does this
            /// alongside the lightmap invalidate because the light level may have moved.
            bool visibility = false;
            /// Also unconditionally dirty the seen cache of `at`'s level, as a vehicle
            /// camera-system toggle does alongside the visibility invalidate.
            bool seen = false;
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
         *
         * The defaults describe a full replacement (what a non-incremental `loadn`
         * does). Callers that replace less than everything — a paint that touches only
         * sight-related caches, a regeneration that leaves the lightmap alone — turn
         * the corresponding part off rather than reaching for a bit setter.
         */
        struct world_replaced {
            tripoint_bub_sm first = tripoint_bub_sm::zero();
            tripoint_bub_sm last = tripoint_bub_sm::zero();
            /// Dirt the seen cache of each covered level.
            bool seen = true;
            /// Dirt the lightmap of each covered level.
            bool lightmap = true;
            /// Dirt the floor cache, and with it the outside/absorption cascade below.
            bool floor = true;
            /// Dirt the sound absorption cache of each covered level.
            bool absorption = true;
            /// Dirt the suspension cache of each covered level.
            bool suspension = true;
        };

        static void report( map &who, const terrain_changed &change );
        static void report( map &who, const terrain_replaced &change );
        static void report( map &who, const furniture_replaced &change );
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
        /**
         * The event-based residency generation the GPU residency layer polls (issue
         * #20). Advances on every change kind affecting this level, even when the
         * content ends up identical; see `advance_residency_generation`.
         */
        static std::uint64_t residency_generation( const level_cache &cache );
        /**
         * View stale: does the visibility cache need rebuilding before exact
         * visibility may be read? True when `viewer` is not the origin the caches
         * were last built for (so a move or a z-level change always answers true)
         * or when any loaded level's visibility bit is dirty. Until that rebuild
         * runs, visibility queries answer with geometry-only visibility.
         */
        static bool visibility_stale( const map &who, const tripoint_bub_ms &viewer );
        /**
         * Are the GEOMETRY inputs visibility is computed from (transparency, seen,
         * outside, floor) stale on any loaded level? True means the Level cache itself
         * must be rebuilt before an exact visibility answer is readable: recomputing
         * the visibility cache from stale geometry reproduces the stale answer, so a
         * caller told "the map cache is current" should verify with this rather than
         * trust the claim (issue #18). The lightmap is excluded: a recompute can still
         * answer correctly from a stale lightmap, and light freshness is issue #19.
         */
        static bool visibility_inputs_stale( const map &who );
        /**
         * Is any loaded level's lightmap stale? The lightmap is the OTHER input an
         * exact visibility answer is read from, and it is deliberately NOT folded into
         * `visibility_inputs_stale`: only a build that processes the lightmap can clear
         * the bit, so a caller must ask this separately and act on it only when it is
         * about to rebuild the lightmap (issue #19).
         */
        static bool lightmap_stale( const map &who );
        /**
         * Reconcile the light-source signature and report whether the lightmap is now
         * stale. Entity lights (a burning monster walking into view, a friend switching
         * on a lamp, the player's own held light) change `lm` and raise no freshness bit
         * of their own; the signature that detects them is otherwise sampled only inside
         * a build's lightmap phase, so a refresh that believes everything is current
         * never notices them (issue #19). Sampling here raises the lightmap bit when the
         * sources moved or changed. Only a caller that is about to process the lightmap
         * may call this: a `skip_lightmap` refresh cannot clear the bit it raises, and
         * escalating on it would rebuild forever.
         */
        static bool lightmap_needs_rebuild( map &who );


    private:
        /**
         * Advance the residency generation of one in-bounds level. The change kinds
         * call this at their top for every level they can affect, so a residency
         * consumer polling the generation never misses an event (issue #20).
         */
        static void advance_residency( map &who, int zlev );
        /** Advance the residency generation of every loaded level. */
        static void advance_residency_all( map &who );
        /**
         * Mark one submap grid cell of `part` stale in the level bitset and, when the
         * submap is resident and `flag` names one, in its own dirty flag. Member
         * because the submap lookup is private to `map`.
         */
        static void mark_submap_flag( map &who, level_cache &ch, level_cache_part part,
                                      const tripoint_bub_sm &smp, bool submap::*flag );
        /**
         * The 3x3-tile-neighbourhood shape shared by the outside, absorption and floor
         * point raises: the tile's own submap, the edge neighbours when the tile sits on
         * a submap boundary, and the corners when it sits on two.
         */
        static void mark_boundary_neighbours( map &who, level_cache &ch, level_cache_part part,
                const tripoint_bub_ms &p, bool submap::*flag );
};

#endif // CATA_SRC_LEVEL_CACHE_FRESHNESS_H
