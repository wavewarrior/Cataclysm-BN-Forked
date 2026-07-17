#include "map.h"
#ifdef COOP_ENABLED
#include "coop_mutation_log.h"
#endif

#ifdef BOX2D_ENABLED
#include "physics/physics_world.h"
#include "physics/veh_box2d_solve.h"
#endif

#include "active_item_cache.h"
#include "ammo.h"
#include "ammo_effect.h"
#include "animation.h"
#include "artifact.h"
#include "avatar.h"
#include "batch_turns.h"
#include "bodypart.h"
#include "cached_options.h"
#include "calendar.h"
#include "cata_cartesian_product.h"
#include "cata_utility.h"
#include "catalua_hooks.h"
#include "catalua_sol.h"
#include "character.h"
#include "character_id.h"
#include "clzones.h"
#include "color.h"
#include "construction.h"
#include "coordinates.h"
#include "creature.h"
#include "cursesdef.h"
#include "damage.h"
#include "debug.h"
#include "detached_ptr.h"
#include "distribution_grid.h"
#include "drawing_primitives.h"
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "explosion.h"
#include "field.h"
#include "field_type.h"
#include "flag.h"
#include "flat_set.h"
#include "fluid_grid.h"
#include "fragment_cloud.h"
#include "fungal_effects.h"
#include "game.h"
#include "harvest.h"
#include "iexamine.h"
#include "input.h"
#include "int_id.h"
#include "item.h"
#include "item_category.h"
#include "item_contents.h"
#include "item_factory.h"
#include "item_group.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "legacy_pathfinding.h"
#include "lightmap.h"
#include "line.h"
#include "map_feature_descriptions.h"
#include "map_functions.h"
#include "map_iterator.h"
#include "map_memory.h"
#include "map_selector.h"
#include "mapbuffer.h"
#include "mapgen_async.h"
#include "math_defines.h"
#include "memory_fast.h"
#include "messages.h"
#include "mission.h"
#include "mongroup.h"
#include "monster.h"
#include "morale_types.h"
#include "mtype.h"
#include "npc.h"
#include "options.h"
#include "output.h"
#include "overmapbuffer.h"
#include "player.h"
#include "point_float.h"
#include "profile.h"
#include "projectile.h"
#include "rng.h"
#include "safe_reference.h"
#include "scent_map.h"
#include "sounds.h"
#include "string_formatter.h"
#include "string_id.h"
#include "submap.h"
#include "submap_load_manager.h"
#include "thread_pool.h"
#include "tileray.h"
#include "timed_event.h"
#include "translations.h"
#include "trap.h"
#include "ui_manager.h"
#include "value_ptr.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "visitable.h"
#include "vpart_position.h"
#include "vpart_range.h"
#include "weather.h"
#include "weighted_list.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <ostream>
#include <queue>
#include <ranges>
#include <shared_mutex>
#include <sstream> // [shift-probe] backtrace buffer; remove with probes
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

struct ammo_effect;
using ammo_effect_str_id = string_id<ammo_effect>;

static const ammo_effect_str_id ammo_effect_INCENDIARY( "INCENDIARY" );
static const ammo_effect_str_id ammo_effect_LASER( "LASER" );
static const ammo_effect_str_id ammo_effect_LIGHTNING( "LIGHTNING" );
static const ammo_effect_str_id ammo_effect_NO_PENETRATE_OBSTACLES( "NO_PENETRATE_OBSTACLES" );
static const ammo_effect_str_id ammo_effect_PLASMA( "PLASMA" );

static const fault_id fault_bionic_nonsterile( "fault_bionic_nonsterile" );

static const itype_id itype_autoclave( "autoclave" );
static const itype_id itype_battery( "battery" );
static const itype_id itype_burnt_out_bionic( "burnt_out_bionic" );
static const itype_id itype_chemistry_set( "chemistry_set" );
static const itype_id itype_dehydrator( "dehydrator" );
static const itype_id itype_electrolysis_kit( "electrolysis_kit" );
static const itype_id itype_food_processor( "food_processor" );
static const itype_id itype_forge( "forge" );
static const itype_id itype_hotplate( "hotplate" );
static const itype_id itype_kiln( "kiln" );
static const itype_id itype_press( "press" );
static const itype_id itype_soldering_iron( "soldering_iron" );
static const itype_id itype_vac_sealer( "vac_sealer" );
static const itype_id itype_welder( "welder" );
static const itype_id itype_butchery( "fake_adv_butchery" );

static const mtype_id mon_zombie( "mon_zombie" );

static const skill_id skill_traps( "traps" );

static const efftype_id effect_boomered( "boomered" );
static const efftype_id effect_crushed( "crushed" );
static const efftype_id effect_onfire( "onfire" );

static const ter_str_id t_rock_floor_no_roof( "t_rock_floor_no_roof" );

static const std::string str_DOOR_LOCKING( "DOOR_LOCKING" );
static const std::string str_OPENCLOSE_INSIDE( "OPENCLOSE_INSIDE" );

namespace
{

auto horde_should_avoid_vehicle_tile( const map &here, const tripoint_bub_ms &p,
                                      const mongroup &group ) -> bool
{
    if( !group.horde ) {
    return false;
}

const auto vp = here.veh_at( p );
if( !vp ) {
    return false;
}

const auto &veh = vp->vehicle();
return veh.is_owned_by( get_avatar() );
}

} // namespace

#define dbg(x) DebugLog((x), DC::Map)



void map::set_transparency_cache_dirty( const int zlev )
{
    if( inbounds_z( zlev ) ) {
        auto& cache = get_cache( zlev );
        cache.transparency_cache_dirty.set();
        ++cache.transparency_generation;
        for( const auto p : bubble_submaps() ) {
            auto* sm = get_submap_at_grid( tripoint_bub_sm( p, zlev ) );
            if( sm ) { sm->transparency_dirty = true; }
        }
    }
}

void map::set_seen_cache_dirty( const tripoint_bub_ms& change_location )
{
    if( inbounds( change_location ) ) {
        level_cache& cache = get_cache( change_location.z() );
        if( cache.seen_cache_dirty ) { return; }
        const int ci = cache.idx( change_location.x(), change_location.y() );
        if( cache.seen_cache[ci] != 0.0 || cache.camera_cache[ci] != 0.0 ) {
            cache.seen_cache_dirty = true;
        }
    }
}

void map::set_outside_cache_dirty( const int zlev )
{
    if( inbounds_z( zlev ) ) {
        level_cache& ch = get_cache( zlev );
        ch.outside_cache_dirty.set();
        for( const auto p : bubble_submaps() ) {
            auto* sm = get_submap_at_grid( tripoint_bub_sm( p, zlev ) );
            if( sm ) { sm->outside_dirty = true; }
        }
    }
}

void map::set_outside_cache_dirty( const tripoint_bub_ms& p )
{
    if( !inbounds( p ) ) { return; }
    level_cache& ch = get_cache( p.z() );
    const auto proj = project_remain<coords::sm>( p );
    const auto smp = proj.quotient_tripoint;
    const auto l = proj.remainder;

    // Helper: mark one submap grid cell dirty in both the bitset and the submap flag.
    auto mark = [&]( const tripoint_bub_sm & p ) {
        if( p.x() < 0 || p.y() < 0 || p.x() >= my_MAPSIZE || p.y() >= my_MAPSIZE ) { return; }
        ch.outside_cache_dirty.set( static_cast<size_t>( ch.bidx( p.x(), p.y() ) ) );
        auto* sm = get_submap_at_grid( tripoint_bub_sm{p.x(), p.y(), p.z()} );
        if( sm ) { sm->outside_dirty = true; }
    };

    // Always mark the tile's own submap.
    mark( smp );

    // rebuild_outside_cache checks a 3×3 tile neighbourhood, so a tile on a
    // submap boundary can affect tiles in the adjacent submap.
    const bool on_left = ( l.x() == 0 );
    const bool on_right = ( l.x() == SEEX - 1 );
    const bool on_top = ( l.y() == 0 );
    const bool on_bottom = ( l.y() == SEEY - 1 );

    if( on_left ) { mark( smp + point_rel_sm::west() ); }
    if( on_right ) { mark( smp + point_rel_sm::east() ); }
    if( on_top ) { mark( smp + point_rel_sm::north() ); }
    if( on_bottom ) { mark( smp + point_rel_sm::south() ); }

    // Corner neighbours when on both x and y boundaries.
    if( on_left && on_top ) { mark( smp + point_rel_sm::north_west() ); }
    if( on_right && on_top ) { mark( smp + point_rel_sm::north_east() ); }
    if( on_left && on_bottom ) { mark( smp + point_rel_sm::south_west() ); }
    if( on_right && on_bottom ) { mark( smp + point_rel_sm::south_east() ); }
}

void map::set_suspension_cache_dirty( const int zlev )
{
    if( inbounds_z( zlev ) ) { get_cache( zlev ).suspension_cache_dirty = true; }
}

void map::set_floor_cache_dirty( const int zlev )
{
    if( inbounds_z( zlev ) ) {
        get_cache( zlev ).floor_cache_dirty.set();
        for( const auto p : bubble_submaps() ) {
            auto* sm = get_submap_at_grid( tripoint_bub_sm( p, zlev ) );
            if( sm ) { sm->floor_dirty = true; }
        }
    }
    // outside_cache and sheltered_cache at z-1 depend on floor_cache at z.
    set_outside_cache_dirty( zlev - 1 );
}

void map::set_floor_cache_dirty( const tripoint_bub_ms& p )
{
    if( !inbounds( p ) ) { return; }
    level_cache& ch = get_cache( p.z() );
    const auto smp = project_to<coords::sm>( p );
    ch.floor_cache_dirty.set( static_cast<size_t>( ch.bidx( smp.x(), smp.y() ) ) );
    auto* sm = get_submap_at_grid( tripoint_bub_sm{smp.x(), smp.y(), p.z()} );
    if( sm ) { sm->floor_dirty = true; }
    // outside_cache and sheltered_cache at z-1 depend on floor_cache at z.
    // The 3×3 neighbourhood means adjacent submaps at z-1 may also be affected.
    set_outside_cache_dirty( p + tripoint_rel_ms::below() );
}

void map::set_seen_cache_dirty( const int &zlevel )
{
    if( inbounds_z( zlevel ) ) {
        level_cache& cache = get_cache( zlevel );
        cache.seen_cache_dirty = true;
    }
}

void map::set_transparency_cache_dirty( const tripoint_bub_ms& p )
{
    if( inbounds( p ) ) {
        const auto smp = project_to<coords::sm>( p );
        level_cache& ch = get_cache( smp.z() );
        ch.transparency_cache_dirty.set( static_cast<size_t>( ch.bidx( smp.x(), smp.y() ) ) );
        ++ch.transparency_generation;
        auto* sm = get_submap_at_grid( smp );
        if( sm ) { sm->transparency_dirty = true; }
    }
}

void map::update_visibility_cache( const int zlev )
{
    ZoneScopedN( "update_visibility_cache" );
    const auto player_pos = g->u.bub_pos();
    visibility_variables_cache.variables_set = true; // Not used yet
    visibility_variables_cache.g_light_level = static_cast<int>( g->light_level( zlev ) );
    {
        const level_cache& plr_ch = get_cache_ref( player_pos.z() );
        visibility_variables_cache.vision_threshold = g->u.get_vision_threshold(
                plr_ch.lm[plr_ch.idx( player_pos.x(), player_pos.y() )].max() );
    }

    visibility_variables_cache.u_clairvoyance = g->u.clairvoyance();
    visibility_variables_cache.u_unimpaired_range = g->u.unimpaired_range();
    visibility_variables_cache.u_sight_impaired = g->u.sight_impaired();
    visibility_variables_cache.u_is_boomered = g->u.has_effect( effect_boomered );
    visibility_variables_cache.visibility_scale_factor =
        60.0f / static_cast<float>( g_max_view_distance );

    auto sm_squares_seen = std::vector<int>( static_cast<size_t>( my_MAPSIZE ) * my_MAPSIZE, 0 );

    const auto min_z =
        fov_3d ? -OVERMAP_DEPTH : ( zlevels ? std::max( zlev - 1, -OVERMAP_DEPTH ) : zlev );
    const auto max_z = fov_3d ? OVERMAP_HEIGHT : zlev;
    const auto max_delta_z =
        std::max( std::abs( min_z - player_pos.z() ), std::abs( max_z - player_pos.z() ) );
    const auto& reference_cache = get_cache_ref( zlev );
    const auto* const distance_table =
        trigdist
    ? &get_rl_dist_lookup_table( rl_dist_lookup_table_dimensions{
        .max_dx = reference_cache.cache_x - 1,
        .max_dy = reference_cache.cache_y - 1,
        .max_dz = max_delta_z,
        .trigdist = trigdist,
    } )
        : nullptr;

    for( const auto z : std::views::iota( min_z, max_z + 1 ) ) {

        level_cache& vc_cache = get_cache( z );
        auto& visibility_cache = vc_cache.visibility_cache;
        const auto dz = std::abs( z - player_pos.z() );

        // Fill visibility_cache.  apparent_light_at is read-only per tile.
        if( parallel_enabled && parallel_map_cache ) {
            parallel_for( 0, vc_cache.cache_x, [&]( int x ) {
                const auto dx = std::abs( x - player_pos.x() );
                for( const auto y : std::views::iota( 0, vc_cache.cache_y ) ) {
                    const auto dy = std::abs( y - player_pos.y() );
                    const auto dist =
                        distance_table != nullptr
                        ? distance_table->distance_3d( dx, dy, dz )
                        : std::max( {dx, dy, dz} );
                    visibility_cache[vc_cache.idx( x, y )] = apparent_light_at(
                            tripoint_bub_ms{x, y, z}, visibility_variables_cache, dist );
                }
            } );
            // Overmap discovery accumulation: serial, reads from the parallel-filled cache.
            // Kept separate because sm_squares_seen is not thread-safe to write from workers.
            if( z == zlev ) {
                for( const auto x : std::views::iota( 0, vc_cache.cache_x ) ) {
                    for( const auto y : std::views::iota( 0, vc_cache.cache_y ) ) {
                        const auto ll = visibility_cache[vc_cache.idx( x, y )];
                        sm_squares_seen[( x / SEEX ) * my_MAPSIZE + y / SEEY] +=
                            ( ll == lit_level::BRIGHT || ll == lit_level::LIT );
                    }
                }
            }
        } else {
            // Serial path: merge visibility fill and overmap discovery into one pass,
            // avoiding a second full scan of the cache at the player's z-level.
            const bool count_discovery = ( z == zlev );
            for( const auto x : std::views::iota( 0, vc_cache.cache_x ) ) {
                const auto dx = std::abs( x - player_pos.x() );
                for( const auto y : std::views::iota( 0, vc_cache.cache_y ) ) {
                    const auto dy = std::abs( y - player_pos.y() );
                    const auto dist =
                        distance_table != nullptr
                        ? distance_table->distance_3d( dx, dy, dz )
                        : std::max( {dx, dy, dz} );
                    const auto ll = apparent_light_at(
                                        tripoint_bub_ms{x, y, z}, visibility_variables_cache, dist );
                    visibility_cache[vc_cache.idx( x, y )] = ll;
                    if( count_discovery ) {
                        sm_squares_seen[( x / SEEX ) * my_MAPSIZE + y / SEEY] +=
                            ( ll == lit_level::BRIGHT || ll == lit_level::LIT );
                    }
                }
            }
        }
    }

    for( const auto p : bubble_submaps() ) {
        if( sm_squares_seen[p.x() * my_MAPSIZE + p.y()] > 36 ) { // 25% of the submap is visible
            const auto abs_sm = bub_to_abs( p );
            const auto abs_omt( project_to<coords::omt>( abs_sm ) );
            get_overmapbuffer( bound_dimension_ ).set_seen( tripoint_abs_omt( abs_omt, 0 ), true );
        }
    }

    // Mark all z-levels touched by this run as clean so subsequent draws within
    // the same turn can skip the rebuild entirely.
    std::ranges::for_each( std::views::iota( min_z, max_z + 1 ), [this]( int z ) {
        get_cache( z ).visibility_cache_dirty = false;
    } );
}



void map::build_outside_cache( const int zlev )
{
    ZoneScopedN( "build_outside_cache" );
    auto& ch = get_cache( zlev );
    if( ch.outside_cache_dirty.none() ) { return; }

    if( zlev >= OVERMAP_HEIGHT ) {
        // Base case: open sky at the top — every tile is outside, nothing above.
        std::fill( ch.outside_cache.begin(), ch.outside_cache.end(), true );
        for( const auto p : bubble_submaps() ) {
            auto* sm = get_submap_at_grid( tripoint_bub_sm( p, zlev ) );
            if( sm ) {
                std::ranges::fill( std::span( &sm->outside_cache[0][0], SEEX * SEEY ), true );
                sm->outside_dirty = false;
            }
        }
        ch.outside_cache_dirty.reset();
        return;
    }

    // Ensure z+1 floor and outside caches are current — they are the inputs.
    const int above_z = zlev + 1;
    if( inbounds_z( above_z ) ) {
        build_floor_cache( above_z );
        build_outside_cache( above_z );
    }

    const level_cache* above = inbounds_z( above_z ) ? &get_cache_ref( above_z ) : nullptr;
    const bool rebuild_all = ch.outside_cache_dirty.all();

    // [shift-probe] Count dirty submaps to tell an edge-incremental shift
    // (~my_MAPSIZE per axis) apart from a broad invalidate (whole level = the
    // residual 20-23ms structural spike).  Cheap: bitset is my_MAPSIZE² (~121 bits).
    {
        size_t _dn = 0;
        const size_t _sz = ch.outside_cache_dirty.size();
        for( size_t _i = 0; _i < _sz; ++_i ) {
            if( ch.outside_cache_dirty.test( _i ) ) { ++_dn; }
        }
        if( _dn > static_cast<size_t>( my_MAPSIZE * 3 ) ) {
            DebugLogFL( DL::Info, DC::Main )
                    << "[shift-probe][outside] z=" << zlev << " dirty_submaps=" << _dn << "/" << _sz
                    << " rebuild_all=" << rebuild_all;
        }
    }

    // Delegate to per-submap rebuild, then copy into the flat render cache.
    // Each smx column writes to unique flat positions; rebuild_outside_cache reads
    // only from the immutable above cache, so columns are safe to process concurrently.
    const auto process_smx = [&]( int smx ) {
        for( int smy = 0; smy < my_MAPSIZE; ++smy ) {
            if( !rebuild_all
                && !ch.outside_cache_dirty.test( static_cast<size_t>( ch.bidx( smx, smy ) ) ) ) {
                continue;
            }
            const auto sm_pos = tripoint_bub_sm( smx, smy, zlev );
            auto* cur_submap = get_submap_at_grid( sm_pos );
            if( cur_submap == nullptr ) { continue; }
            cur_submap->rebuild_outside_cache( above, sm_pos );

            for( const auto sm_ms : submap_tiles() ) {
                const auto ms_pos = project_combine( sm_pos, sm_ms );
                ch.outside_cache[static_cast<size_t>( ch.idx( ms_pos.x(), ms_pos.y() ) )] =
                    cur_submap->outside_cache[sm_ms.x()][sm_ms.y()];
            }
        }
    };

    if( parallel_enabled && parallel_map_cache && !is_pool_worker_thread() ) {
        parallel_for( 0, my_MAPSIZE, process_smx );
    } else {
        for( int smx = 0; smx < my_MAPSIZE; ++smx ) { process_smx( smx ); }
    }

    ch.outside_cache_dirty.reset();
}

bool map::build_floor_cache( const int zlev )
{
    ZoneScopedN( "build_floor_cache" );
    auto& ch = get_cache( zlev );
    if( ch.floor_cache_dirty.none() ) { return false; }

    auto& floor_cache = ch.floor_cache;
    const bool rebuild_all = ch.floor_cache_dirty.all();

    // When rebuilding all submaps we can bulk-initialize the whole level to
    // "has floor" (true) in one pass, then let per-submap rebuilds stamp out
    // the no-floor tiles.  For partial rebuilds we reset only dirty submap
    // regions individually inside the loop.
    if( rebuild_all ) { std::fill( floor_cache.begin(), floor_cache.end(), true ); }

    // Delegate to per-submap rebuild, then copy into the flat render cache.
    for( const auto p : bubble_submaps() ) {
        if( !rebuild_all
            && !ch.floor_cache_dirty.test( static_cast<size_t>( ch.bidx( p.x(), p.y() ) ) ) ) {
            continue;
        }
        const auto sm_pos = tripoint_bub_sm( p, zlev );
        submap* cur_submap = get_submap_at_grid( sm_pos );
        if( cur_submap == nullptr ) {
            // Null expected for circle corners and bounded-dimension edges.
            continue;
        }
        cur_submap->rebuild_floor_cache( *this, sm_pos );

        const auto ms_pos = project_to<coords::ms>( p );

        if( !rebuild_all ) {
            // Reset this submap's region to "has floor" before stamping no-floor tiles,
            // since a previously no-floor tile may have gained a floor since last build.
            for( int sx = 0; sx < SEEX; ++sx ) {
                std::fill_n( floor_cache.data() + ch.idx( ms_pos.x() + sx, ms_pos.y() ), SEEY, '\x01' );
            }
        }

        for( const auto sm_ms : submap_tiles() ) {
            if( !cur_submap->floor_cache[sm_ms.x()][sm_ms.y()] ) {
                floor_cache[ch.idx( ms_pos.x() + sm_ms.x(), ms_pos.y() + sm_ms.y() )] = false;
            }
        }
    }

    ch.floor_cache_dirty.reset();
    ch.has_any_floor = std::ranges::any_of( floor_cache, []( char c ) { return c != 0; } );
    return zlevels;
}

void map::update_suspension_cache( const int &z )
{
    level_cache& ch = get_cache( z );
    if( !ch.suspension_cache_dirty ) { return; }
    std::list<point_abs_ms> &suspension_cache = ch.suspension_cache;
    if( !ch.suspension_cache_initialized ) {
        for( const auto p : bubble_submaps() ) {
            const submap* cur_submap = get_submap_at_grid( tripoint_bub_sm( p, z ) );

            if( cur_submap == nullptr ) {
                // Null expected for circle corners and bounded-dimension edges.
                continue;
            }

            for( const auto sm_ms : submap_tiles() ) {
                const ter_t& terrain = cur_submap->get_ter( sm_ms ).obj();
                if( terrain.has_flag( TFLAG_SUSPENDED ) ) {
                    auto loc = coords::project_combine( p, sm_ms );
                    suspension_cache.emplace_back( bub_to_abs( loc ) );
                }
            }
        }
        ch.suspension_cache_initialized = true;
    }

    for( auto iter = suspension_cache.begin(); iter != suspension_cache.end(); ) {
        const point_abs_ms absp = *iter;
        const tripoint_bub_ms loctp( abs_to_bub( absp ), z );
        if( !inbounds( loctp ) ) {
            ++iter;
            continue;
        }
        const submap* cur_submap = get_submap_at( loctp );
        if( cur_submap == nullptr ) {
            debugmsg( "Tried to run suspension check at (%d,%d,%d) but the submap is not loaded",
                      loctp.x(), loctp.y(), loctp.z() );
            ++iter;
            continue;
        }
        const ter_t& terrain = ter( loctp.xy() ).obj();
        if( terrain.has_flag( TFLAG_SUSPENDED ) ) {
            if( !is_suspension_valid( loctp ) ) {
                support_dirty( loctp );
                iter = suspension_cache.erase( iter );
            } else {
                ++iter;
            }
        } else {
            iter = suspension_cache.erase( iter );
        }
    }
    ch.suspension_cache_dirty = false;
}

static void vehicle_caching_internal( level_cache& zch, const vpart_reference& vp, vehicle* v )
{
    auto& outside_cache = zch.outside_cache;
    auto& sheltered_cache = zch.sheltered_cache;
    auto& transparency_cache = zch.transparency_cache;
    auto& floor_cache = zch.floor_cache;
    auto& obscured_cache = zch.vehicle_obscured_cache;
    auto& obstructed_cache = zch.vehicle_obstructed_cache;

    const size_t part = vp.part_index();
    const tripoint_bub_ms& part_pos = v->bub_part_location( vp.part() );

    bool vehicle_is_opaque = vp.has_feature( VPFLAG_OPAQUE ) && !vp.part().is_broken();

    if( vehicle_is_opaque ) {
        int dpart = v->part_with_feature( part, VPFLAG_OPENABLE, true );
        if( dpart < 0 || !v->part( dpart ).open ) {
            transparency_cache[zch.idx( part_pos.x(), part_pos.y() )] = LIGHT_TRANSPARENCY_SOLID;
        } else {
            vehicle_is_opaque = false;
        }
    }

    if( vehicle_is_opaque || vp.is_inside() ) {
        const int veh_idx = zch.idx( part_pos.x(), part_pos.y() );
        outside_cache[veh_idx] = false;
        sheltered_cache[veh_idx] = true;
    }

    if( vp.has_feature( VPFLAG_BOARDABLE ) && !vp.part().is_broken() ) {
        floor_cache[zch.idx( part_pos.x(), part_pos.y() )] = true;
    }

    tripoint_mnt_veh t = v->bubble_to_mount( part_pos + point_north_west );
    if( !v->allowed_light( t, vp.mount() ) ) {
        obscured_cache[zch.idx( part_pos.x(), part_pos.y() )].nw = true;
    }
    if( !v->allowed_move( t, vp.mount() ) ) {
        obstructed_cache[zch.idx( part_pos.x(), part_pos.y() )].nw = true;
    }

    t = v->bubble_to_mount( part_pos + point_north_east );
    if( !v->allowed_light( t, vp.mount() ) ) {
        obscured_cache[zch.idx( part_pos.x(), part_pos.y() )].ne = true;
    }
    if( !v->allowed_move( t, vp.mount() ) ) {
        obstructed_cache[zch.idx( part_pos.x(), part_pos.y() )].ne = true;
    }

    if( part_pos.x() > 0 && part_pos.y() < zch.cache_y - 1 ) {
        t = v->bubble_to_mount( part_pos + point_south_west );
        if( !v->allowed_light( t, vp.mount() ) ) {
            obscured_cache[zch.idx( part_pos.x() - 1, part_pos.y() + 1 )].ne = true;
        }
        if( !v->allowed_move( t, vp.mount() ) ) {
            obstructed_cache[zch.idx( part_pos.x() - 1, part_pos.y() + 1 )].ne = true;
        }
    }

    if( part_pos.x() < zch.cache_x - 1 && part_pos.y() < zch.cache_y - 1 ) {
        t = v->bubble_to_mount( tripoint_bub_ms( part_pos + point_south_east ) );
        if( !v->allowed_light( t, vp.mount() ) ) {
            obscured_cache[zch.idx( part_pos.x() + 1, part_pos.y() + 1 )].nw = true;
        }
        if( !v->allowed_move( t, vp.mount() ) ) {
            obstructed_cache[zch.idx( part_pos.x() + 1, part_pos.y() + 1 )].nw = true;
        }
    }
}

static void vehicle_caching_internal_above(
    level_cache& zch_above, const vpart_reference& vp, vehicle* v )
{
    if( vp.has_feature( VPFLAG_ROOF ) || vp.has_feature( VPFLAG_OPAQUE ) ) {
        const tripoint_bub_ms& part_pos = v->bub_part_location( vp.part() );
        const int tile_idx = zch_above.idx( part_pos.x(), part_pos.y() );
        zch_above.vehicle_floor_cache[tile_idx] = true;
    }
}

void map::build_map_cache( const int zlev, bool skip_lightmap )
{
    ZoneScoped;
    // Submap-shift stall attribution (diagnostic, logged only when total >2ms):
    // per-phase split + player-z vs other-z for the unconditional all-z Phase1
    // loops. Decides z-range-limit (B) vs single-level work (C). Remove once pinned.
    using _bc = std::chrono::steady_clock;
    const _bc::time_point _bc_t0 = _bc::now();
    _bc::time_point _bc_tp = _bc_t0;
    double _ph_floor = 0, _ph_out = 0, _ph_trans = 0, _ph_par = 0, _ph_susp = 0, _ph_veh = 0,
           _ph_seen = 0, _ph_tail = 0;
    double _z_player = 0, _z_other = 0;
    auto _lap = [&]( double &acc ) {
        const _bc::time_point now = _bc::now();
        acc += std::chrono::duration<double, std::milli>( now - _bc_tp ).count();
        _bc_tp = now;
    };
    auto _zadd = [&]( int z, const _bc::time_point & t ) {
        ( z == zlev ? _z_player : _z_other ) +=
            std::chrono::duration<double, std::milli>( _bc::now() - t ).count();
    };
    const int minz = zlevels ? -OVERMAP_DEPTH : zlev;
    const int maxz = zlevels ? OVERMAP_HEIGHT : zlev;
    bool seen_cache_dirty = false;
    std::vector<int> dirty_seen_cache_levels;

    // Refresh the shared weather-transparency lookup table once, serially,
    // before the parallel block.  build_transparency_cache() reads the
    // table on every call, so updating it here guarantees all workers see a
    // consistent value without a data race.
    update_weather_transparency_lookup();

    // Parallelize the expensive per-z-level cache builds across all z-levels.
    // Each build_*_cache(z) writes only to get_cache(z) — no z-level aliasing.
    //
    // update_suspension_cache is intentionally excluded from this parallel block:
    // it calls support_dirty() which inserts into the shared support_cache_dirty
    // set and is not thread-safe.  It runs in a dedicated serial pass below.

    {
        ZoneScopedN( "Phase1_floor" );
        // Floor caches are z-independent so they can run in any order.
        // They must complete before outside/sheltered caches which read floor[z+1].
        for( int z = minz; z <= maxz; ++z ) {
            const bool affects_seen_cache = z == zlev || fov_3d;
            const _bc::time_point _zt = _bc::now();
            const bool _floor_dirty = build_floor_cache( z );
            _zadd( z, _zt );
            if( _floor_dirty && affects_seen_cache ) { seen_cache_dirty = true; }
        }
    }
    _lap( _ph_floor );

    {
        ZoneScopedN( "Phase1_outside_sheltered" );
        // outside_cache and sheltered_cache both depend on floor[z+1] and their own z+1,
        // so they must be computed top-down.  They use intra-z parallel_for, so they
        // cannot run inside a parallel-over-z block.
        for( int z = maxz; z >= minz; --z ) {
            const _bc::time_point _zt = _bc::now();
            build_outside_cache( z );
            _zadd( z, _zt );
        }
    }
    _lap( _ph_out );

    {
        ZoneScopedN( "Phase1_transparency" );
        // Transparency depends on outside_cache; runs after outside is complete.
        for( int z = minz; z <= maxz; ++z ) {
            const _bc::time_point _zt = _bc::now();
            build_transparency_cache( z );
            _zadd( z, _zt );
        }
    }
    _lap( _ph_trans );

    {
        ZoneScopedN( "Phase1_parallel_caches" );
        // Vehicle cache clearing only — floor/outside/sheltered are already done above.
        if( parallel_enabled && parallel_map_cache ) {
            std::mutex dirty_mutex;
            parallel_for( minz, maxz + 1, [&]( int z ) {
                level_cache& ch = get_cache( z );
                // vehicle_floor_cache is written by vehicles one level below (via
                // vehicle_caching_internal_above), so it must be cleared unconditionally —
                // not gated on veh_in_active_range — to prevent stale entries after shifts.
                std::fill( ch.vehicle_floor_cache.begin(), ch.vehicle_floor_cache.end(), '\0' );
                if( ch.veh_in_active_range ) {
                    const diagonal_blocks fill = {false, false};
                    std::fill( ch.vehicle_obscured_cache.begin(), ch.vehicle_obscured_cache.end(),
                               fill );
                    std::fill( ch.vehicle_obstructed_cache.begin(),
                               ch.vehicle_obstructed_cache.end(), fill );
                }

                const bool level_seen_dirty = ch.seen_cache_dirty;
                if( level_seen_dirty ) {
                    std::lock_guard<std::mutex> lock( dirty_mutex );
                    seen_cache_dirty = true;
                    dirty_seen_cache_levels.push_back( z );
                }
            } );
        } else {
            for( int z = minz; z <= maxz; ++z ) {
                level_cache& ch = get_cache( z );
                // vehicle_floor_cache is written by vehicles one level below (via
                // vehicle_caching_internal_above), so it must be cleared unconditionally —
                // not gated on veh_in_active_range — to prevent stale entries after shifts.
                std::fill( ch.vehicle_floor_cache.begin(), ch.vehicle_floor_cache.end(), '\0' );
                if( ch.veh_in_active_range ) {
                    const diagonal_blocks fill = {false, false};
                    std::fill( ch.vehicle_obscured_cache.begin(), ch.vehicle_obscured_cache.end(),
                               fill );
                    std::fill( ch.vehicle_obstructed_cache.begin(),
                               ch.vehicle_obstructed_cache.end(), fill );
                }

                const bool level_seen_dirty = ch.seen_cache_dirty;
                if( level_seen_dirty ) {
                    seen_cache_dirty = true;
                    dirty_seen_cache_levels.push_back( z );
                }
            }
        }
    }
    _lap( _ph_par );
    // implicit barrier; floor/outside/sheltered/transparency caches for all z-levels are complete.

    {
        ZoneScopedN( "Phase2_suspension" );
        // update_suspension_cache calls support_dirty() which writes to the shared
        // support_cache_dirty set; must remain serial.
        for( int z = minz; z <= maxz; z++ ) { update_suspension_cache( z ); }
    }
    _lap( _ph_susp );

    {
        ZoneScopedN( "Phase3_vehicles" );
        // needs a separate pass as it changes the caches on neighbour z-levels (e.g. floor_cache);
        // otherwise such changes might be overwritten by main cache-building logic.
        // This pass must remain serial: do_vehicle_caching() writes to neighbor z-level caches.
        for( int z = minz; z <= maxz; z++ ) {
            if( get_cache( z ).veh_in_active_range ) { do_vehicle_caching( z ); }
        }
    }
    _lap( _ph_veh );

    seen_cache_dirty |= build_vision_transparency_cache( get_player_character() );

    if( seen_cache_dirty ) { skew_vision_cache.assign( vision_cache_slots, vision_cache_slot{} ); }
    const tripoint_bub_ms& p = g->u.bub_pos();
    if( seen_cache_dirty || m_last_seen_cache_origin != p ) {
        build_seen_cache( p, zlev );
        m_last_seen_cache_origin = p;
        // seen_cache changed; any cached visibility derived from it is now stale.
        get_cache( zlev ).visibility_cache_dirty = true;
    }
    _lap( _ph_seen );
    if( !skip_lightmap ) {
        ZoneScopedN( "Phase4_lightmap" );
        // Only include levels whose lightmap is actually stale this redraw.
        // lightmap_dirty is marked per-submap by map::shift (loadn), player
        // movement, terrain changes, and explicit invalidate calls (vehicle
        // lights, bionics, etc).  Levels with no dirty submaps are skipped,
        // and their shifted lm array from the last rebuild is reused.
        if( get_cache( zlev ).lightmap_dirty.any() ) { dirty_seen_cache_levels.push_back( zlev ); }
        dirty_seen_cache_levels.erase(
            std::ranges::remove_if(
                dirty_seen_cache_levels,
        [this]( int z ) { return !get_cache( z ).lightmap_dirty.any(); } )
        .begin(),
        dirty_seen_cache_levels.end() );
        std::ranges::sort( dirty_seen_cache_levels );
        dirty_seen_cache_levels.erase(
            std::ranges::unique( dirty_seen_cache_levels ).begin(), dirty_seen_cache_levels.end() );

        if( !dirty_seen_cache_levels.empty() ) {

            // [shift-probe] Which levels regenerate lightmap this build.  >1 level on a
            // non-shift turn flags an unexpected all-z driver (residual lightmap spike).
            if( dirty_seen_cache_levels.size() > 1 ) {
                std::string _zs;
                for( const int _z : dirty_seen_cache_levels ) { _zs += std::to_string( _z ) + ","; }
                DebugLogFL( DL::Info, DC::Main )
                        << "[shift-probe][lightmap] regen " << dirty_seen_cache_levels.size()
                        << " levels: " << _zs;
            }

            if( dirty_seen_cache_levels.size() > 1 && parallel_enabled && parallel_map_cache ) {
                // Multiple dirty levels: hoist shared initialization outside the
                // parallel loop so worker threads never race on cross-level writes.
                //
                // Always run the sunlight cascade in the multi-level path because
                // shift+loadn gives new-edge submaps stale lm values (from the old
                // grid position's terrain).  Skipping the cascade here would leave
                // those submaps with incorrect sunlight — causing a visible flash.
                for( const int z : dirty_seen_cache_levels ) {
                    auto &c = get_cache( z );
                    std::fill( c.sm.begin(), c.sm.end(), 0.0f );
                    std::fill( c.light_source_buffer.begin(), c.light_source_buffer.end(),
                               level_cache::buffered_light_source{} );
                    // lm must be zeroed because build_sunlight_cache only writes outdoor tiles.
                    std::fill( c.lm.begin(), c.lm.end(), four_quadrants( 0.0f ) );
                }
                // Build sunlight (all z-levels, top-to-bottom; serial).
                build_sunlight_cache( zlev );
                // Generate per-level dynamic lighting in parallel.
                // skip_shared_init=true: workers only process entities on their own z-level.
                // Pre-warm the vehicle list cache serially to avoid heap corruption
                // from concurrent writes to last_full_vehicle_list.
                get_vehicles();
                parallel_for( 0, static_cast<int>( dirty_seen_cache_levels.size() ), [&]( int i ) {
                    generate_lightmap_worker( dirty_seen_cache_levels[i] );
                } );
            } else {
                // Single dirty level: run serially using the standard full path.
                for( const int level : dirty_seen_cache_levels ) { generate_lightmap( level ); }
            }

            // Diagnostic: log dirty-submap fraction for each regenerated level so the
            // per-submap scaling can be verified in debug.log.
            for( const int z : dirty_seen_cache_levels ) {
                const auto& ld = get_cache( z ).lightmap_dirty;
                const int total_sm = get_cache( z ).cache_mapsize * get_cache( z ).cache_mapsize;
                int dirty_sm = 0;
                for( int i = 0; i < total_sm; ++i ) {
                    if( ld[static_cast<size_t>( i )] ) { ++dirty_sm; }
                }
                DebugLogFL( DL::Info, DC::Main )
                        << "[build_cache][perf] lightmap_dirty z=" << z << " " << dirty_sm << "/"
                        << total_sm << " submaps";
            }

            // Mark each regenerated level clean so subsequent redraws this turn skip it.
            // Also mark visibility dirty: the lightmap just changed, so any visibility
            // cache computed before this rebuild (e.g. from handle_action's unconditional
            // update_visibility_cache call) is now stale and must be rebuilt in game::draw.
            std::ranges::for_each( dirty_seen_cache_levels, [this]( int z ) {
                get_cache( z ).lightmap_dirty.reset();
                get_cache( z ).visibility_cache_dirty = true;
            } );

        } // end if( !dirty_seen_cache_levels.empty() )

        // Always apply entity lights when lightmap processing is enabled,
        // regardless of submap dirtiness.  Entity lights track current
        // position + state of creatures and are cheap (a few ray casts).
        apply_character_light( get_player_character() );
        for( npc& guy : g->all_npcs() ) { apply_character_light( guy ); }
        for( monster& critter : g->all_monsters() ) {
            if( critter.is_hallucination() ) { continue; }
            const auto& mp = critter.bub_pos();
            if( inbounds( mp ) ) {
                if( critter.has_effect( effect_onfire ) ) { apply_light_source( mp, 8 ); }
                if( critter.type->luminance > 0 ) {
                    apply_light_source( mp, critter.type->luminance );
                }
            }
        }
    }
    _lap( _ph_tail );

    const double _bc_total = std::chrono::duration<double, std::milli>( _bc::now() - _bc_t0 ).count();
    if( _bc_total > 2.0 ) {
        DebugLogFL( DL::Info, DC::Main )
                << "[build_cache][perf] total=" << _bc_total << "ms (z " << minz << ".." << maxz
                << ") floor=" << _ph_floor << " outside=" << _ph_out << " trans=" << _ph_trans
                << " parclear=" << _ph_par << " susp=" << _ph_susp << " veh=" << _ph_veh
                << " seen=" << _ph_seen << " lightmap=" << _ph_tail
                << " | z-split: player=" << _z_player << " other=" << _z_other;
    }
}

