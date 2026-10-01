#include "level_cache_freshness.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <ranges>
#include <utility>

#include "cata_dynamic_bitset.h"
#include "coordinates.h"
#include "game_constants.h"
#include "map.h"
#include "map_iterator.h"
#include "mapbuffer.h"
#include "submap.h"
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

// Const mirrors of the two accessors, for the freshness queries. Defined after the
// non-const versions so the delegation resolves to them.
cata_dynamic_bitset const *bitset_of( level_cache const &cache, const level_cache_part part ) {
    return bitset_of( const_cast<level_cache &>( cache ), part );
}

bool const *flag_of( level_cache const &cache, const level_cache_part part ) {
    return flag_of( const_cast<level_cache &>( cache ), part );
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

void level_cache_freshness::mark_vehicle_caches( map &who, const int zlev ) {
    assert_main_thread();
    // The coupling this verb carries: a vehicle on this level
    // dirties the vehicle-floor cache of the level above it.
    if( who.inbounds_z( zlev ) ) {
        mark( who.get_cache( zlev ), freshness_parts( { level_cache_part::vehicle_caches } ) );
    }
    if( who.inbounds_z( zlev + 1 ) ) {
        mark( who.get_cache( zlev + 1 ), freshness_parts( { level_cache_part::vehicle_floor } ) );
    }
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

void level_cache_freshness::mark_submap_flag( map &who, level_cache &ch,
        const level_cache_part part, const tripoint_bub_sm &smp,
        bool submap::*const flag ) {
    mark( ch, freshness_parts( { part } ), static_cast<size_t>( ch.bidx( smp.x(), smp.y() ) ) );
    submap *const sm = who.get_submap_at_grid( smp );
    if( ( flag != nullptr ) && sm != nullptr ) {
        sm->*flag = true;
    }
}

void level_cache_freshness::mark_boundary_neighbours( map &who, level_cache &ch,
        const level_cache_part part, const tripoint_bub_ms &p, bool submap::*const flag ) {
    const auto proj = project_remain<coords::sm>( p );
    const tripoint_bub_sm smp = proj.quotient_tripoint;
    const auto l = proj.remainder;
    const auto mark_cell = [&]( const point_rel_sm & d ) {
        const tripoint_bub_sm n( smp.xy() + d, smp.z() );
        if( n.x() < 0 || n.y() < 0 || n.x() >= who.my_MAPSIZE || n.y() >= who.my_MAPSIZE ) {
            return;
        }
        mark_submap_flag( who, ch, part, n, flag );
    };
    mark_cell( point_rel_sm::zero() );
    const bool on_left = l.x() == 0;
    const bool on_right = l.x() == SEEX - 1;
    const bool on_top = l.y() == 0;
    const bool on_bottom = l.y() == SEEY - 1;
    if( on_left ) { mark_cell( point_rel_sm::west() ); }
    if( on_right ) { mark_cell( point_rel_sm::east() ); }
    if( on_top ) { mark_cell( point_rel_sm::north() ); }
    if( on_bottom ) { mark_cell( point_rel_sm::south() ); }
    if( on_left && on_top ) { mark_cell( point_rel_sm::north_west() ); }
    if( on_right && on_top ) { mark_cell( point_rel_sm::north_east() ); }
    if( on_left && on_bottom ) { mark_cell( point_rel_sm::south_west() ); }
    if( on_right && on_bottom ) { mark_cell( point_rel_sm::south_east() ); }
}

void level_cache_freshness::mark_transparency( map &who, const int zlev ) {
    assert_main_thread();
    if( !who.inbounds_z( zlev ) ) {
        return;
    }
    level_cache &ch = who.get_cache( zlev );
    // Coupling carried from the old helper: wiping the whole transparency cache also
    // wipes the sound absorption cache derived from it.
    mark( ch, freshness_parts( { level_cache_part::transparency, level_cache_part::absorption } ) );
    advance_transparency_generation( ch );
    for( const point_bub_sm p : who.flat_bubble_submaps() ) {
        if( submap * const sm = who.get_submap_at_grid( tripoint_bub_sm( p, zlev ) ) ) {
            sm->transparency_dirty = true;
        }
    }
}

void level_cache_freshness::mark_transparency( map &who, const tripoint_bub_ms &p ) {
    assert_main_thread();
    if( !who.inbounds( p ) ) {
        return;
    }
    const tripoint_bub_sm smp = project_to<coords::sm>( p );
    level_cache &ch = who.get_cache( smp.z() );
    mark_submap_flag( who, ch, level_cache_part::transparency, smp,
                      &submap::transparency_dirty );
    advance_transparency_generation( ch );
}

void level_cache_freshness::mark_outside( map &who, const int zlev ) {
    assert_main_thread();
    if( !who.inbounds_z( zlev ) ) {
        return;
    }
    mark( who.get_cache( zlev ), freshness_parts( { level_cache_part::outside } ) );
    for( const point_bub_sm p : who.flat_bubble_submaps() ) {
        if( submap * const sm = who.get_submap_at_grid( tripoint_bub_sm( p, zlev ) ) ) {
            sm->outside_dirty = true;
        }
    }
}

void level_cache_freshness::mark_outside( map &who, const tripoint_bub_ms &p ) {
    assert_main_thread();
    if( !who.inbounds( p ) ) {
        return;
    }
    mark_boundary_neighbours( who, who.get_cache( p.z() ), level_cache_part::outside, p,
                              &submap::outside_dirty );
}

void level_cache_freshness::mark_absorption( map &who, const int zlev ) {
    assert_main_thread();
    if( who.inbounds_z( zlev ) ) {
        mark( who.get_cache( zlev ), freshness_parts( { level_cache_part::absorption } ) );
    }
}

void level_cache_freshness::mark_absorption( map &who, const tripoint_bub_ms &p ) {
    assert_main_thread();
    if( !who.inbounds( p ) ) {
        return;
    }
    // The absorption builder reads a 3x3 tile neighbourhood like the outside builder, so
    // the raise has the same boundary shape; unlike outside and transparency it has no
    // submap dirty flag.
    mark_boundary_neighbours( who, who.get_cache( p.z() ), level_cache_part::absorption, p,
                              nullptr );
}

void level_cache_freshness::mark_floor( map &who, const int zlev ) {
    assert_main_thread();
    if( who.inbounds_z( zlev ) ) {
        mark( who.get_cache( zlev ), freshness_parts( { level_cache_part::floor } ) );
        for( const point_bub_sm p : who.flat_bubble_submaps() ) {
            if( submap * const sm = who.get_submap_at_grid( tripoint_bub_sm( p, zlev ) ) ) {
                sm->floor_dirty = true;
            }
        }
    }
    // outside_cache and sheltered_cache at z-1 depend on floor_cache at z, and with them
    // the absorption and sound wall caches.
    mark_outside( who, zlev - 1 );
    mark_absorption( who, zlev - 1 );
}

void level_cache_freshness::mark_floor( map &who, const tripoint_bub_ms &p ) {
    assert_main_thread();
    if( !who.inbounds( p ) ) {
        return;
    }
    mark_submap_flag( who, who.get_cache( p.z() ), level_cache_part::floor,
                      project_to<coords::sm>( p ), &submap::floor_dirty );
    // The 3x3 tile neighbourhood means adjacent submaps one level down may be affected
    // too, and the absorption cache follows the outside cache there.
    mark_outside( who, p + tripoint_rel_ms::below() );
    mark_absorption( who, p + tripoint_rel_ms::below() );
}

void level_cache_freshness::mark_suspension( map &who, const int zlev ) {
    assert_main_thread();
    if( who.inbounds_z( zlev ) ) {
        mark( who.get_cache( zlev ), freshness_parts( { level_cache_part::suspension_dirty } ) );
    }
}

void level_cache_freshness::mark_seen( map &who, const int zlev ) {
    assert_main_thread();
    if( who.inbounds_z( zlev ) ) {
        mark( who.get_cache( zlev ), freshness_parts( { level_cache_part::seen } ) );
    }
}

void level_cache_freshness::mark_seen( map &who, const tripoint_bub_ms &p ) {
    assert_main_thread();
    if( !who.inbounds( p ) ) {
        return;
    }
    level_cache &ch = who.get_cache( p.z() );
    if( ch.seen_cache_dirty ) {
        return;
    }
    const int ci = ch.idx( p.x(), p.y() );
    if( ch.seen_cache[ci] != 0.0 || ch.camera_cache[ci] != 0.0 ) {
        mark( ch, freshness_parts( { level_cache_part::seen } ) );
    }
}

void level_cache_freshness::mark_lightmap( map &who, const tripoint_bub_ms &p ) {
    assert_main_thread();
    if( !who.inbounds_z( p.z() ) ) {
        return;
    }
    level_cache &ch = who.get_cache( p.z() );
    const size_t bidx = static_cast<size_t>( ch.bidx( p.x() / SEEX, p.y() / SEEY ) );
    mark( ch, freshness_parts( { level_cache_part::lightmap } ), bidx );
}

void level_cache_freshness::invalidate_lightmap( map &who ) {
    assert_main_thread();
    const auto parts = freshness_parts( { level_cache_part::lightmap } );
    for( int z = -OVERMAP_DEPTH; z <= OVERMAP_HEIGHT; ++z ) {
        level_cache &ch = who.get_cache( z );
        mark( ch, parts );
        invalidate_cpu_lightmap( ch );
        mark_visibility( who, z );
    }
}

void level_cache_freshness::invalidate_visibility( map &who ) {
    assert_main_thread();
    for( int z = -OVERMAP_DEPTH; z <= OVERMAP_HEIGHT; ++z ) {
        mark_visibility( who, z );
    }
}

void level_cache_freshness::mark_memory_seen( map &who, const tripoint_bub_ms &p ) {
    assert_main_thread();
    level_cache &ch = who.get_cache( p.z() );
    const int offset = p.x() + p.y() * ch.cache_x;
    if( offset < 0 || offset >= ch.cache_x * ch.cache_y ) {
        return;
    }
    const auto bit = static_cast<size_t>( offset );
    if( ch.map_memory_seen_cache[bit] ) {
        ch.map_memory_seen_cache.reset( bit );
        queue_memory_seen( ch, p );
    }
}

void level_cache_freshness::mark_memory_seen( map &who, const int zlev ) {
    assert_main_thread();
    level_cache &ch = who.get_cache( zlev );
    ch.map_memory_seen_cache.reset();
    queue_memory_seen_all( ch );
}

void level_cache_freshness::invalidate_level( map &who, const int zlev ) {
    assert_main_thread();
    if( !who.inbounds_z( zlev ) ) {
        return;
    }
    level_cache &ch = who.get_cache( zlev );
    mark( ch, freshness_parts( {
        level_cache_part::floor,
        level_cache_part::transparency,
        level_cache_part::absorption,
        level_cache_part::sound_wall,
        level_cache_part::seen,
        level_cache_part::lightmap,
        level_cache_part::outside,
        level_cache_part::suspension_dirty,
    } ) );
    invalidate_cpu_lightmap( ch );
    mark_vehicle_caches( who, zlev );
    mark_visibility( who, zlev );
    forget_seen_origin( who );
    forget_solar_stamps( who );
}

namespace {

/// Walk the `band_count` bands of submaps the shift scrolls in from, in the
/// direction opposite to the shift (the edge column/row that just arrived).
void for_shifted_bands( const point_rel_sm &shift, int mapsize, int band_count,
                        const int gridz, auto &&mark ) {
    auto const mark_column = [&]( const int smx ) {
        if( smx < 0 || smx >= mapsize ) {
            return;
        }
        for( const auto smy : std::views::iota( 0, mapsize ) ) {
            mark( tripoint_bub_sm( smx, smy, gridz ) );
        }
    };
    auto const mark_row = [&]( const int smy ) {
        if( smy < 0 || smy >= mapsize ) {
            return;
        }
        for( const auto smx : std::views::iota( 0, mapsize ) ) {
            mark( tripoint_bub_sm( smx, smy, gridz ) );
        }
    };
    for( const auto band : std::views::iota( 0, band_count ) ) {
        if( shift.x() > 0 ) {
            mark_column( mapsize - 1 - band );
        } else if( shift.x() < 0 ) {
            mark_column( band );
        }
        if( shift.y() > 0 ) {
            mark_row( mapsize - 1 - band );
        } else if( shift.y() < 0 ) {
            mark_row( band );
        }
    }
}

} // namespace

void level_cache_freshness::report( map &who, const terrain_changed &change ) {
    assert_main_thread();
    const tripoint_bub_ms &p = change.at;
    const tripoint_bub_ms above( p.xy(), p.z() + 1 );
    // One raise per property diff, mirroring the branches of ter_set/furn_set
    // (map_access.cpp). The verbs keep their own couplings (floor dirties the
    // level below, transparency bumps the Structure-rebuild generation).
    if( change.transparency ) {
        switch( change.scope ) {
            case terrain_changed::transparency_scope::tile:
                mark_transparency( who, p );
                break;
            case terrain_changed::transparency_scope::level:
                mark_transparency( who, p.z() );
                break;
            case terrain_changed::transparency_scope::all_levels:
                for( int z = -OVERMAP_DEPTH; z <= OVERMAP_HEIGHT; ++z ) {
                    mark_transparency( who, z );
                }
                break;
        }
        mark_seen( who, change.seen_probe.value_or( p ) );
    }
    if( change.no_floor || change.z_transparent ) {
        mark_floor( who, p );
        // Floor/z-transparency changes reveal or hide tiles one level down.
        mark_seen( who, p.z() );
        mark_seen( who, p.z() - 1 );
    }
    if( change.sun_roof_above ) {
        mark_floor( who, above );
    }
    if( change.suspended ) {
        mark_suspension( who, p.z() );
    }
    if( change.support_here || change.no_floor ) {
        support_lost( who, p );
    }
    if( change.support_above ) {
        // Both terrain mutators always check whether something above lost its support.
        support_lost( who, above );
    }
    if( change.memory_seen ) {
        mark_memory_seen( who, p );
    }
    if( change.lightmap ) {
        invalidate_lightmap( who );
    }
    if( change.floor_level ) {
        // Whole-level floor shape, with the verb's own cascade to the outside and
        // absorption caches one level down.
        mark_floor( who, p.z() );
    }
}

void level_cache_freshness::report( map &who, const light_changed &change ) {
    assert_main_thread();
    // Today's light mutators (emissive item add/remove, vehicle light toggles, field
    // changes) all funnel through this one instrument: every level's lightmap is stale.
    switch( change.scope ) {
        case light_changed::lightmap_scope::all_levels:
            invalidate_lightmap( who );
            break;
        case light_changed::lightmap_scope::tile:
            // A viewer move inside the bubble moves only the entity lights of the
            // submap it landed in, which is what the player-move site raises today.
            mark_lightmap( who, change.at );
            break;
        case light_changed::lightmap_scope::none:
            // Nothing about the light sources changed; the caller reports a viewer-side
            // fact (a camera feed switched) that dirties what is seen, not what lights.
            break;
    }
    if( change.seen ) {
        mark_seen( who, change.at.z() );
    }
    if( change.visibility ) {
        // The activity-cadence boundary pairs the lightmap invalidate with the
        // map-wide visibility aggregate, because the light level may have moved.
        invalidate_visibility( who );
    }
}

void level_cache_freshness::report( map &who, const vehicle_moved &change ) {
    assert_main_thread();
    const int smz = change.z;
    if( !who.inbounds_z( smz ) ) {
        return;
    }
    level_cache &ch = who.get_cache( smz );
    // Mirrors map::on_vehicle_moved's non-batched body, minus the notification counter
    // (an observation, not freshness) and minus the GPU residency push, which stays at
    // the call site until the GPU seam turns to generation polling.
    mark( ch, freshness_parts( { level_cache_part::veh_in_active_range } ) );
    mark_vehicle_caches( who, smz );
    invalidate_lightmap( who );
    // Hour-only: the vehicle path must not reset the light-level gate, or every move
    // would force a full sunlight cascade.
    forget_solar_hour( who );
    mark_seen( who, smz );
    mark( ch, freshness_parts( { level_cache_part::visibility } ) );

    const auto bubble = who.reality_bubble_2D_bounds();
    const auto for_clamped_submaps = [&]( const point_bub_sm &range_min,
    const point_bub_sm &range_max, const auto &callback ) {
        const inclusive_rectangle<point_bub_sm> requested( range_min, range_max );
        if( !bubble.overlaps( requested ) ) {
            return;
        }
        for( const auto p : point_range<point_bub_sm>( clamp( range_min, bubble ),
                        clamp( range_max, bubble ) ) ) {
            callback( p );
        }
    };

    const auto mark_occupancy = [&]( const level_cache &target, const int z,
    const enum_bitset<level_cache_part> &parts ) {
        for_clamped_submaps( change.sm_min.xy(), change.sm_max.xy(),
        [&]( const point_bub_sm & p ) {
            level_cache &level = who.get_cache( z );
            mark( level, parts, static_cast<size_t>( target.bidx( p.x(), p.y() ) ) );
            submap *const sm = who.get_submap_at_grid( tripoint_bub_sm( p, z ) );
            if( sm == nullptr ) {
                return;
            }
            if( parts[level_cache_part::transparency] ) {
                sm->transparency_dirty = true;
            }
            if( parts[level_cache_part::floor] ) {
                sm->floor_dirty = true;
            }
            if( parts[level_cache_part::outside] ) {
                sm->outside_dirty = true;
            }
            // `on_vehicle_moved` sets pf_dirty on the occupancy pass (floor plus
            // transparency); the above-level floor-only pass leaves it alone.
            if( z == smz ) {
                sm->pf_dirty = true;
            }
        } );
    };

    mark_occupancy( ch, smz, freshness_parts( {
        level_cache_part::transparency,
        level_cache_part::floor,
    } ) );
    // outside_cache has a 3x3 tile neighbourhood dependency: one submap of slack.
    for_clamped_submaps( point_bub_sm( change.sm_min.x() - 1, change.sm_min.y() - 1 ),
    point_bub_sm( change.sm_max.x() + 1, change.sm_max.y() + 1 ),
    [&]( const point_bub_sm & p ) {
        mark( ch, freshness_parts( { level_cache_part::outside } ),
              static_cast<size_t>( ch.bidx( p.x(), p.y() ) ) );
        submap *const sm = who.get_submap_at_grid( tripoint_bub_sm( p, smz ) );
        if( sm != nullptr ) {
            sm->outside_dirty = true;
        }
    } );

    // Vehicles can extend through the floor; mark the level above as well.
    const int above_z = smz + 1;
    if( who.inbounds_z( above_z ) ) {
        mark_seen( who, above_z );
        mark( who.get_cache( above_z ), freshness_parts( { level_cache_part::visibility } ) );
        mark_occupancy( who.get_cache( above_z ), above_z,
                        freshness_parts( { level_cache_part::floor } ) );
    }
}

void level_cache_freshness::report( map &who, const player_moved & ) {
    assert_main_thread();
    // Today a player move raises no dirty bit at all: what makes the next build rebuild
    // the seen cache (and thereby raise the visibility bit) is the stored origin no
    // longer matching the viewer. Forgetting it is the faithful translation.
    forget_seen_origin( who );
}

void level_cache_freshness::report( map &who, const z_level_changed & ) {
    assert_main_thread();
    // Same route as a same-bubble move: `vertical_shift_notify` raises no bit either.
    forget_seen_origin( who );
}

void level_cache_freshness::report( map &who, const map_shifted &change ) {
    assert_main_thread();
    // `map::shift` forgets which viewer the seen cache was built for before anything
    // else, so the next build re-derives it for the new bubble position.
    forget_seen_origin( who );
    const point_rel_sm &shift = change.shift;
    const int mapsize = who.getmapsize();
    // The shift translates and edge-dirties every level, not a caller-supplied range:
    // a level left out would keep bitsets pointing at slots that no longer hold its
    // caches.
    for( int gridz = -OVERMAP_DEPTH; gridz <= OVERMAP_HEIGHT; ++gridz ) {
        level_cache &gc = who.get_cache( gridz );
        // Everything memorised on the level has moved relative to the world.
        queue_memory_seen_all( gc );
        // Carried caches stay valid where they landed; only the shifted-in edge is stale.
        translate( gc, freshness_parts( {
            level_cache_part::transparency,
            level_cache_part::floor,
            level_cache_part::outside,
        } ), shift, mapsize );
        translate( gc, freshness_parts( { level_cache_part::lightmap } ), shift, mapsize );
        translate( gc, freshness_parts( { level_cache_part::absorption } ), shift, mapsize );
        // Force-dirty the bands the shift scrolled in. The submap dirty flags rise
        // through the mapbuffer, which reaches resident and unloaded submaps alike; the
        // lightmap edge needs no such flag, matching the shift's own sequence.
        const auto mark_band = [&]( const enum_bitset<level_cache_part> &parts, int bands,
        const bool buffer_flags ) {
            for_shifted_bands( shift, mapsize, bands, gridz,
            [&]( const tripoint_bub_sm & smp ) {
                mark( gc, parts, static_cast<size_t>( gc.bidx( smp.x(), smp.y() ) ) );
                if( !buffer_flags ) {
                    return;
                }
                const tripoint_abs_sm abs_sm = map_local_to_abs( who, smp );
                who.get_mapbuffer().mark_submap_caches_dirty( {
                    .begin = abs_sm.xy(),
                    .end = abs_sm.xy() + point_rel_sm( 1, 1 ),
                    .zlev = abs_sm.z(),
                    .transparency = static_cast<bool>( parts[level_cache_part::transparency] ),
                    .floor = static_cast<bool>( parts[level_cache_part::floor] ),
                    .outside = static_cast<bool>( parts[level_cache_part::outside] ),
                    .absorption = static_cast<bool>( parts[level_cache_part::absorption] ),
                } );
            } );
        };
        // Band widths mirror the shift's own sequence: floor needs its own column,
        // outside and transparency a 3x3 tile neighbourhood, absorption two bands.
        mark_band( freshness_parts( { level_cache_part::floor } ), 1, true );
        mark_band( freshness_parts( { level_cache_part::outside } ), 3, true );
        mark_band( freshness_parts( { level_cache_part::transparency } ), 3, true );
        mark_band( freshness_parts( { level_cache_part::lightmap } ), 1, false );
        mark_band( freshness_parts( { level_cache_part::absorption } ), 2, true );
        // Seen work stays limited to the levels the player can glance at; deeper levels
        // ride along on the translate above (see the comment in `map::shift`).
        if( std::abs( gridz - change.player_z ) <= 1 ) {
            mark_seen( who, gridz );
        }
        mark_visibility( who, gridz );
        mark_suspension( who, gridz );
    }
    // Pending collapse checks are bubble-relative, so they travel with the bubble —
    // the last freshness write of `map::shift` (map.cpp).
    const half_open_rectangle<point_bub_ms> boundaries_2d( point_bub_ms::zero(),
        point_bub_ms( g_mapsize_x, g_mapsize_y ) );
    translate_support_losses( who, point_rel_ms( -shift.x() * SEEX, -shift.y() * SEEY ),
                              boundaries_2d );
}

void level_cache_freshness::report( map &who, const world_replaced &change ) {
    assert_main_thread();
    const int mapsize = who.getmapsize();
    const int xmin = std::min( change.first.x(), change.last.x() );
    const int xmax = std::max( change.first.x(), change.last.x() );
    const int ymin = std::min( change.first.y(), change.last.y() );
    const int ymax = std::max( change.first.y(), change.last.y() );
    const int zmin = std::min( change.first.z(), change.last.z() );
    const int zmax = std::max( change.first.z(), change.last.z() );
    // Bulk replacement of terrain under a whole area of submaps: the Level cache bitsets
    // AND the submap dirty flags must both rise. Setting only the submap flags — what the
    // co-op tile sync of issue #7 does today — leaves `build_floor_cache`,
    // `build_outside_cache` and the transparency builder early-returning on their clean
    // bitsets, so the swapped terrain is never rebuilt.
    //
    // Per covered level this is the non-incremental `loadn` sequence (map.cpp) at area
    // granularity, including its couplings: the transparency raise bumps the
    // Structure-rebuild generation, the floor raise cascades outside and absorption one
    // level down (with one submap of slack, the 3x3-tile neighbourhood of a submap), and
    // the vehicle raise touches the level above. Pathfinding dirt stays at the call site
    // like every other kind, and the visibility bit is not raised here: the seen rebuild
    // the dirty seen cache provokes raises it, exactly as it does for `loadn`.
    const auto mark_area = [&]( const int z, const enum_bitset<level_cache_part> &parts,
    const int slack ) {
        if( !who.inbounds_z( z ) ) {
            return;
        }
        level_cache &ch = who.get_cache( z );
        for( int sx = xmin - slack; sx <= xmax + slack; ++sx ) {
            for( int sy = ymin - slack; sy <= ymax + slack; ++sy ) {
                if( sx < 0 || sx >= mapsize || sy < 0 || sy >= mapsize ) {
                    continue;
                }
                mark( ch, parts, static_cast<size_t>( ch.bidx( sx, sy ) ) );
                submap *const sm = who.get_submap_at_grid( tripoint_bub_sm( sx, sy, z ) );
                if( sm == nullptr ) {
                    continue;
                }
                // Submap flags mirror the helpers `loadn` calls: transparency, floor and
                // outside have one; the absorption raise is bitset-only there.
                if( parts[level_cache_part::transparency] ) {
                    sm->transparency_dirty = true;
                }
                if( parts[level_cache_part::floor] ) {
                    sm->floor_dirty = true;
                }
                if( parts[level_cache_part::outside] ) {
                    sm->outside_dirty = true;
                }
            }
        }
    };
    enum_bitset<level_cache_part> area_parts;
    area_parts.set( level_cache_part::transparency );
    area_parts.set( level_cache_part::outside );
    if( change.absorption ) {
        area_parts.set( level_cache_part::absorption );
    }
    if( change.floor ) {
        area_parts.set( level_cache_part::floor );
    }
    if( change.lightmap ) {
        area_parts.set( level_cache_part::lightmap );
    }
    for( int z = zmin; z <= zmax; ++z ) {
        if( !who.inbounds_z( z ) ) {
            continue;
        }
        mark_area( z, area_parts, 0 );
        advance_transparency_generation( who.get_cache( z ) );
        if( change.seen ) {
            mark_seen( who, z );
        }
        if( change.suspension ) {
            mark_suspension( who, z );
        }
        if( change.vehicle ) {
            mark_vehicle_caches( who, z );
        }
        // Outside and absorption one level down: `loadn`'s floor raise cascades there
        // because outside at z-1 reads floor at z. A caller that leaves the floor cache
        // alone has no such dependency.
        if( change.floor ) {
            mark_area( z - 1, freshness_parts( {
                level_cache_part::outside,
                level_cache_part::absorption,
            } ), 1 );
        }
    }
}

bool level_cache_freshness::stale( const level_cache &cache, const level_cache_part part ) {
    if( const cata_dynamic_bitset * const bits = bitset_of( cache, part ) ) {
        return bits->any();
    }
    if( const bool * const flag = flag_of( cache, part ) ) {
        // `lm_valid` stores freshness, so staleness is the negation.
        return part == level_cache_part::lm_valid ? !*flag : *flag;
    }
    return false;
}

bool level_cache_freshness::stale( const level_cache &cache, const level_cache_part part,
                                   const size_t bit ) {
    if( const cata_dynamic_bitset * const bits = bitset_of( cache, part ) ) {
        return bits->test( bit );
    }
    return stale( cache, part );
}

std::uint64_t level_cache_freshness::transparency_generation( const level_cache &cache ) {
    return cache.transparency_generation;
}

std::uint64_t level_cache_freshness::outside_generation( const level_cache &cache ) {
    return cache.outside_generation;
}

std::uint64_t level_cache_freshness::cpu_lightmap_generation( const level_cache &cache ) {
    return cache.lm_cpu_cache_generation;
}

bool level_cache_freshness::visibility_stale( const map &who ) {
    return who.visibility_caches_dirty_;
}
