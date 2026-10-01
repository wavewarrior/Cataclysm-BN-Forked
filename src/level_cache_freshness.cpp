#include "level_cache_freshness.h"

#include <cassert>
#include <ranges>
#include <utility>

#include "cata_dynamic_bitset.h"
#include "coordinates.h"
#include "game_constants.h"
#include "map.h"
#include "thread_pool.h"

namespace {

/// Every part the module knows about, in declaration order.
constexpr level_cache_part all_parts[] = {
    level_cache_part::transparency,
    level_cache_part::outside,
    level_cache_part::floor,
    level_cache_part::absorption,
    level_cache_part::sound_wall,
    level_cache_part::lightmap,
    level_cache_part::seen,
    level_cache_part::visibility,
    level_cache_part::lm_valid,
    level_cache_part::suspension_dirty,
    level_cache_part::suspension_init,
    level_cache_part::vehicle_caches,
    level_cache_part::vehicle_floor,
    level_cache_part::has_any_floor,
    level_cache_part::has_any_vehicle_floor,
    level_cache_part::colored_light_active,
    level_cache_part::veh_in_active_range,
    level_cache_part::memory_seen_all,
};

/**
 * Fail loudly when a mutating call arrives on a pool worker.
 *
 * The freshness bookkeeping is deliberately unsynchronised, so a worker raising
 * bits would race with the main thread rather than merely be slow. Compiled out
 * under NDEBUG, like the `assert`s elsewhere in the map cache code.
 */
void assert_main_thread() {
    // The parallel region of `map::build_map_cache` runs per-level builders on pool
    // workers; those may only clear or assign the state of the level they own, and
    // they go through `clear`/`assign`, never through the verbs guarded here.
    assert( !is_pool_worker_thread() );
}

cata_dynamic_bitset *bitset_of( level_cache &cache, const level_cache_part part ) {
    switch( part ) {
        case level_cache_part::transparency:
            return &cache.transparency_cache_dirty;
        case level_cache_part::outside:
            return &cache.outside_cache_dirty;
        case level_cache_part::floor:
            return &cache.floor_cache_dirty;
        case level_cache_part::absorption:
            return &cache.absorption_cache_dirty;
        case level_cache_part::sound_wall:
            return &cache.sound_wall_cache_dirty;
        case level_cache_part::lightmap:
            return &cache.lightmap_dirty;
        default:
            return nullptr;
    }
}

bool *flag_of( level_cache &cache, const level_cache_part part ) {
    switch( part ) {
        case level_cache_part::seen:
            return &cache.seen_cache_dirty;
        case level_cache_part::visibility:
            return &cache.visibility_cache_dirty;
        case level_cache_part::lm_valid:
            return &cache.lm_cpu_cache_valid;
        case level_cache_part::suspension_dirty:
            return &cache.suspension_cache_dirty;
        case level_cache_part::suspension_init:
            return &cache.suspension_cache_initialized;
        case level_cache_part::vehicle_caches:
            return &cache.vehicle_caches_dirty;
        case level_cache_part::vehicle_floor:
            return &cache.vehicle_floor_cache_dirty;
        case level_cache_part::has_any_floor:
            return &cache.has_any_floor;
        case level_cache_part::has_any_vehicle_floor:
            return &cache.has_any_vehicle_floor;
        case level_cache_part::colored_light_active:
            return &cache.colored_light_cache_active;
        case level_cache_part::veh_in_active_range:
            return &cache.veh_in_active_range;
        case level_cache_part::memory_seen_all:
            return &cache.map_memory_seen_cache_dirty_all;
        default:
            return nullptr;
    }
}

} // namespace

enum_bitset<level_cache_part> level_cache_bitset_parts() {
    static const enum_bitset<level_cache_part> parts = [] {
        enum_bitset<level_cache_part> result;
        result.set( level_cache_part::transparency );
        result.set( level_cache_part::outside );
        result.set( level_cache_part::floor );
        result.set( level_cache_part::absorption );
        result.set( level_cache_part::sound_wall );
        result.set( level_cache_part::lightmap );
        return result;
    }();
    return parts;
}

void level_cache_freshness::mark( level_cache &cache,
                                  const enum_bitset<level_cache_part> &parts ) {
    assert_main_thread();
    for( const level_cache_part part : all_parts ) {
        if( !parts[ part ] ) {
            continue;
        }
        if( cata_dynamic_bitset * const bits = bitset_of( cache, part ) ) {
            bits->set();
        } else if( bool * const flag = flag_of( cache, part ) ) {
            // `mark` means "no longer fresh"; `lm_valid` stores freshness, so marking
            // it stale clears the flag.
            *flag = part != level_cache_part::lm_valid;
        }
    }
}

void level_cache_freshness::mark( level_cache &cache,
                                  const enum_bitset<level_cache_part> &parts,
                                  const size_t bit ) {
    assert_main_thread();
    for( const level_cache_part part : all_parts ) {
        if( !parts[ part ] ) {
            continue;
        }
        if( cata_dynamic_bitset * const bits = bitset_of( cache, part ) ) {
            bits->set( bit );
        } else if( bool * const flag = flag_of( cache, part ) ) {
            *flag = part != level_cache_part::lm_valid;
        }
    }
}

void level_cache_freshness::clear( level_cache &cache,
                                   const enum_bitset<level_cache_part> &parts ) {
    // No main-thread assertion: the parallel region of `map::build_map_cache` clears
    // the state of the level its own builder just rebuilt. Callers must keep to the
    // level they own.
    for( const level_cache_part part : all_parts ) {
        if( !parts[ part ] ) {
            continue;
        }
        if( cata_dynamic_bitset * const bits = bitset_of( cache, part ) ) {
            bits->reset();
        } else if( bool * const flag = flag_of( cache, part ) ) {
            // Fresh again: for `lm_valid` that means setting the flag.
            *flag = part == level_cache_part::lm_valid;
        }
    }
}

void level_cache_freshness::assign( level_cache &cache,
                                    const enum_bitset<level_cache_part> &parts,
                                    const bool value ) {
    // Worker-allowed like `clear`: this stamps derived facts a builder just computed
    // for its own level.
    for( const level_cache_part part : all_parts ) {
        if( !parts[ part ] ) {
            continue;
        }
        if( bool * const flag = flag_of( cache, part ) ) {
            *flag = value;
        }
    }
}

void level_cache_freshness::translate( level_cache &cache,
                                       const enum_bitset<level_cache_part> &parts,
                                       const point_rel_sm &shift, const int mapsize ) {
    assert_main_thread();
    for( const level_cache_part part : all_parts ) {
        if( !parts[ part ] ) {
            continue;
        }
        cata_dynamic_bitset * const bits = bitset_of( cache, part );
        if( bits == nullptr ) {
            continue;
        }
        // Carry the dirty bits along with the caches they describe: a submap that was
        // dirty before the shift is still dirty in its new slot, and a submap that was
        // clean must not be rebuilt just because the bubble moved.
        const cata_dynamic_bitset old_dirty = *bits;
        bits->reset();
        for( const auto smx : std::views::iota( 0, mapsize ) ) {
            const auto source_smx = smx + shift.x();
            for( const auto smy : std::views::iota( 0, mapsize ) ) {
                const auto source_smy = smy + shift.y();
                if( source_smx >= 0 && source_smx < mapsize && source_smy >= 0 &&
                    source_smy < mapsize &&
                    old_dirty.test( static_cast<size_t>( cache.bidx( source_smx, source_smy ) ) ) ) {
                    bits->set( static_cast<size_t>( cache.bidx( smx, smy ) ) );
                }
            }
        }
    }
}

void level_cache_freshness::advance_transparency_generation( level_cache &cache ) {
    assert_main_thread();
    ++cache.transparency_generation;
}

void level_cache_freshness::invalidate_cpu_lightmap( level_cache &cache ) {
    assert_main_thread();
    cache.lm_cpu_cache_valid = false;
    ++cache.lm_cpu_cache_generation;
}

void level_cache_freshness::validate_cpu_lightmap( level_cache &cache ) {
    assert_main_thread();
    cache.lm_cpu_cache_valid = true;
}

void level_cache_freshness::record_outside_content( level_cache &cache,
                                                   const std::uint64_t checksum ) {
    assert_main_thread();
    // Advance the Structure-rebuild generation only on real content change; see the
    // comment on `level_cache::outside_checksum`.
    if( cache.outside_checksum != checksum ) {
        cache.outside_checksum = checksum;
        ++cache.outside_generation;
    }
}

void level_cache_freshness::mark_visibility( map &who, const int zlev ) {
    assert_main_thread();
    if( !who.inbounds_z( zlev ) ) {
        return;
    }
    who.get_cache( zlev ).visibility_cache_dirty = true;
    // The aggregate is the source of truth for gameplay consumers. It is only ever set
    // true here and cleared wholesale by `clear_visibility`; that asymmetry is
    // load-bearing today and is replaced by "view stale" in a later ticket.
    who.visibility_caches_dirty_ = true;
}

void level_cache_freshness::clear_visibility( map &who, const int min_z, const int max_z ) {
    assert_main_thread();
    // Per-level bits only: a range clear is a builder reporting what it just rebuilt and
    // deliberately leaves the map-wide aggregate alone.
    for( int z = min_z; z <= max_z; ++z ) {
        who.get_cache( z ).visibility_cache_dirty = false;
    }
}

void level_cache_freshness::clear_visibility( map &who ) {
    assert_main_thread();
    clear_visibility( who, -OVERMAP_DEPTH, OVERMAP_HEIGHT );
    who.visibility_caches_dirty_ = false;
}

void level_cache_freshness::forget_seen_origin( map &who ) {
    assert_main_thread();
    who.m_last_seen_cache_origin = tripoint_bub_ms( tripoint_min );
}

void level_cache_freshness::stamp_seen_origin( map &who, const tripoint_bub_ms &origin ) {
    assert_main_thread();
    who.m_last_seen_cache_origin = origin;
}

void level_cache_freshness::forget_solar_stamps( map &who ) {
    assert_main_thread();
    who.m_solar.last_built_hour = -1;
    who.m_solar.last_built_light_level_int = -1;
}

void level_cache_freshness::forget_solar_hour( map &who ) {
    assert_main_thread();
    who.m_solar.last_built_hour = -1;
}

void level_cache_freshness::stamp_solar_light_level( map &who, const int light_level_int ) {
    assert_main_thread();
    who.m_solar.last_built_light_level_int = light_level_int;
}

bool level_cache_freshness::note_lightmap_source_signature( map &who,
                                                           const std::size_t signature ) {
    assert_main_thread();
    if( who.m_last_lightmap_source_signature_valid &&
        signature == who.m_last_lightmap_source_signature ) {
        return false;
    }
    who.m_last_lightmap_source_signature = signature;
    who.m_last_lightmap_source_signature_valid = true;
    return true;
}

void level_cache_freshness::support_lost( map &who, const tripoint_bub_ms &p ) {
    assert_main_thread();
    who.support_cache_dirty.insert( p );
}

std::set<tripoint_bub_ms> level_cache_freshness::take_support_losses( map &who ) {
    assert_main_thread();
    std::set<tripoint_bub_ms> losses = std::move( who.support_cache_dirty );
    who.support_cache_dirty.clear();
    return losses;
}

void level_cache_freshness::translate_support_losses(
    map &who, const point_rel_ms &shift,
    const half_open_rectangle<point_bub_ms> &boundaries ) {
    assert_main_thread();
    if( who.support_cache_dirty.empty() ) {
        return;
    }
    // Same rule as the tile caches: keep what survived the shift, drop what scrolled
    // out of the bubble.
    std::set<tripoint_bub_ms> shifted;
    for( const tripoint_bub_ms &pt : who.support_cache_dirty ) {
        const tripoint_bub_ms moved = pt + shift;
        if( boundaries.contains( moved.xy() ) ) {
            shifted.insert( moved );
        }
    }
    who.support_cache_dirty = std::move( shifted );
}

void level_cache_freshness::queue_memory_seen( level_cache &cache, const tripoint_bub_ms &p ) {
    assert_main_thread();
    if( !cache.map_memory_seen_cache_dirty_all ) {
        cache.map_memory_seen_cache_dirty_points.push_back( p );
    }
}

void level_cache_freshness::queue_memory_seen_all( level_cache &cache ) {
    assert_main_thread();
    cache.map_memory_seen_cache_dirty_points.clear();
    cache.map_memory_seen_cache_dirty_all = true;
}

std::vector<tripoint_bub_ms> level_cache_freshness::take_memory_seen_points(
    level_cache &cache ) {
    assert_main_thread();
    auto points = std::move( cache.map_memory_seen_cache_dirty_points );
    cache.map_memory_seen_cache_dirty_points.clear();
    return points;
}

void level_cache_freshness::acknowledge_memory_seen( level_cache &cache ) {
    assert_main_thread();
    cache.map_memory_seen_cache_dirty_all = false;
    cache.map_memory_seen_cache_dirty_points.clear();
}

void level_cache_freshness::initialise( level_cache &cache ) {
    // No main-thread assertion, and no trip through `mark`: a Level cache is constructed
    // whenever a map is, and a map can be constructed on a pool worker during premaw.
    //
    // A brand-new Level cache holds no derived data at all, so every per-submap cache in it
    // has to be built before it can be read. Per-level flags keep their declared defaults:
    // visibility starts dirty, the rest start at their resting values, which is what the
    // constructor relied on before this verb existed.
    for( const level_cache_part part : all_parts ) {
        if( cata_dynamic_bitset * const bits = bitset_of( cache, part ) ) {
            bits->set();
        }
    }
}
