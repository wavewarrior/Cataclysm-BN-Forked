#include "map.h"
#include "coop_mutation_log.h"
// S2: the GPU lightmap dispatch and the readiness latch both live in build_map_cache below.
#include "lightmap_ready.h"
#if defined( CATA_SDL )
#include "compute/gpu_lm.h"
#include "compute/gpu_platform.h"
#endif

#include "physics/physics_world.h"
#include "physics/veh_box2d_solve.h"

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
        // If we are invalidating the entire transparency cache for this zlevel, the sound
        // absorption cache will also be invalidated.
        cache.absorption_cache_dirty.set();
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
    // This also means the absorption and sound wall caches are marked dirty there as well.
    set_absorption_cache_dirty( zlev - 1 );
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
    // Setting the floor cache for a submap dirty should also automatically set the absorption
    // cache and sound_wall caches dirty.
    set_absorption_cache_dirty( p + tripoint_rel_ms::below() );
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

void map::set_absorption_cache_dirty( const tripoint_bub_ms& p )
{
    // Logic lifted shamelessly from set_outside_cache_dirty
    if( !inbounds( p ) ) { return; }
    level_cache& ch = get_cache( p.z() );
    const auto proj = project_remain<coords::sm>( p );
    const auto smp = proj.quotient_tripoint;
    const auto l = proj.remainder;

    // Helper: mark one submap grid cell dirty in both the bitset and the submap flag.
    auto mark = [&]( const tripoint_bub_sm & p ) {
        if( p.x() < 0 || p.y() < 0 || p.x() >= my_MAPSIZE || p.y() >= my_MAPSIZE ) { return; }
        ch.absorption_cache_dirty.set( static_cast<size_t>( ch.bidx( p.x(), p.y() ) ) );
        auto* sm = get_submap_at_grid( tripoint_bub_sm{p.x(), p.y(), p.z()} );
        if( sm ) { sm->absorption_dirty = true; }
    };

    // Always mark the tile's own submap.
    mark( smp );

    // rebuild_absorption_cache checks a 3×3 tile neighbourhood, so a tile on a
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

void map::set_absorption_cache_dirty( const int zlev )
{
    if( inbounds_z( zlev ) ) { get_cache( zlev ).absorption_cache_dirty.set(); }
}

void map::update_visibility_cache( const int zlev )
{
    ZoneScopedN( "update_visibility_cache" );
    const auto player_pos = g->u.bub_pos();
    visibility_variables_cache.variables_set = true; // Not used yet
    visibility_variables_cache.g_light_level = static_cast<int>( g->light_level( zlev ) );
    {
        const level_cache& plr_ch = get_cache_ref( player_pos.z() );
        // bub_pos() is derived from abs_sub, so the avatar can sit outside this
        // map's bubble whenever the anchor moved without it (a reality-bubble
        // resize before the map is re-centred, or a harness reloading the map at
        // a fixed anchor).  lm[] is indexed without bounds checking, so treat an
        // out-of-bubble avatar as standing in the dark instead of reading past
        // the end of the cache.
        const auto plr_light = plr_ch.inbounds( player_pos.xy() )
                               ? plr_ch.lm[plr_ch.idx( player_pos.x(), player_pos.y() )]
                               : 0.0f;
        visibility_variables_cache.vision_threshold = g->u.get_vision_threshold( plr_light );
    }

    visibility_variables_cache.u_clairvoyance = g->u.clairvoyance();
    visibility_variables_cache.u_unimpaired_range = g->u.unimpaired_range();
    visibility_variables_cache.u_sight_impaired = g->u.sight_impaired();
    visibility_variables_cache.u_is_boomered = g->u.has_effect( effect_boomered );
    visibility_variables_cache.visibility_scale_factor =
        60.0f / static_cast<float>( g_max_view_distance );

    auto sm_squares_seen = std::vector<int>( static_cast<size_t>( my_MAPSIZE ) * my_MAPSIZE, 0 );

    const auto min_z =
        -OVERMAP_DEPTH;
    const auto max_z = OVERMAP_HEIGHT;
    const auto max_delta_z =
        std::max( std::abs( min_z - player_pos.z() ), std::abs( max_z - player_pos.z() ) );
    // The table is indexed by |coord - player_coord|, so its extent has to span
    // the avatar's offset to BOTH ends of the bubble.  cache_x - 1 is that span
    // only while the avatar stands inside the bubble; when it does not (see the
    // vision_threshold comment above) dx runs past cache_x - 1 and the loops
    // below index the table out of bounds from a worker thread.  Size from the
    // widest span over every z-level this call actually iterates -- once, here,
    // so the inner loops stay a plain array index with no per-tile bounds check.
    //
    // extent - 1 stays in the max so an avatar walking around inside the bubble
    // always asks for the same extent and the table is built once, not rebuilt
    // whenever it steps closer to an edge.
    const auto axis_span = []( const int player_coord, const int extent ) -> int {
        if( extent <= 0 )
        {
            return -1;
        }
        return std::max( {
            extent - 1,
            std::abs( player_coord ),
            std::abs( extent - 1 - player_coord ) } );
    };
    auto span_dx = -1;
    auto span_dy = -1;
    for( const auto z : std::views::iota( min_z, max_z + 1 ) ) {
        const level_cache& span_cache = get_cache_ref( z );
        span_dx = std::max( span_dx, axis_span( player_pos.x(), span_cache.cache_x ) );
        span_dy = std::max( span_dy, axis_span( player_pos.y(), span_cache.cache_y ) );
    }
    const auto table_dimensions = rl_dist_lookup_table_dimensions{
        .max_dx = span_dx,
        .max_dy = span_dy,
        .max_dz = max_delta_z,
        .trigdist = trigdist,
    };
    // Owns the table for the whole function: a published table is immutable, and
    // holding the shared_ptr keeps this one alive even if another thread
    // publishes a larger one while the workers below are still reading it.
    const auto distance_table_owner =
        trigdist
        ? get_rl_dist_lookup_table( table_dimensions )
        : std::shared_ptr<const rl_dist_lookup_table> {};
    const auto* distance_table = distance_table_owner.get();
    if( distance_table != nullptr && !distance_table->matches( table_dimensions ) ) {
        // Unreachable through get_rl_dist_lookup_table's contract.  Checked once
        // per call rather than per tile so that a future regression on either
        // side of this seam degrades to the direct computation instead of
        // silently reading out of bounds from a worker thread.
        const auto& built = distance_table->dimensions();
        debugmsg( "rl_dist lookup table (%d,%d,%d) does not cover span (%d,%d,%d)",
                  built.max_dx, built.max_dy, built.max_dz, span_dx, span_dy, max_delta_z );
        distance_table = nullptr;
    }
    // Only read when the table is absent, i.e. trigdist is off or no iterated
    // z-level has an allocated cache.  Keeps the fallback in the same metric.
    const auto use_trigdist = trigdist;

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
                        : rl_dist_from_deltas( dx, dy, dz, use_trigdist );
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
                        : rl_dist_from_deltas( dx, dy, dz, use_trigdist );
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
    // DIAGNOSTIC (temporary, CBN_DIAG_SEG_LIGHTING). Measured in-game: outside_cache is
    // FALSE for all 32400 tiles while the player stands outdoors in bright daylight,
    // which zeroes SkyVisBuf and gates sprite.frag's entire sun term off
    // (`sky_vis > 0.05`) -- daylight ends up shadowless. Two very different causes
    // produce that identical symptom and guessing between them has already been wrong
    // once this session, so measure: (a) this function early-returns on
    // `outside_cache_dirty.none()` and the cache is simply never built, or (b) it DOES
    // build but every tile reads roofed because the z+1 floor cache claims a floor
    // overhead. Reports the early-out, and below, the resulting true-counts.
    const bool diag_oc = std::getenv( "CBN_DIAG_SEG_LIGHTING" ) != nullptr;
    static int diag_oc_n = 0;
    // Filter to the low levels: the recursion runs z=0 -> z=10 and then UNWINDS,
    // so z=0 is built LAST and a plain call-count cap logs only the sky levels.
    const bool diag_log = diag_oc && zlev <= 1 && ++diag_oc_n <= 8;
    if( ch.outside_cache_dirty.none() ) {
        if( diag_log ) {
            size_t t = 0;
            for( size_t i = 0; i < ch.outside_cache.size(); ++i ) {
                t += ch.outside_cache[i] ? 1u : 0u;
            }
            DebugLogFL( DL::Info, DC::Main )
                    << "[ocdiag] z=" << zlev << " EARLY-OUT (dirty.none) outside_true=" << t
                    << "/" << ch.outside_cache.size();
        }
        return;
    }

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
        // All-open is one fixed state; bump only if we were not already there.
        const std::uint64_t h_open = ~0ull;
        if( ch.outside_checksum != h_open ) {
            ch.outside_checksum = h_open;
            ++ch.outside_generation;
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

    if( diag_log ) {
        size_t ot = 0;
        for( size_t i = 0; i < ch.outside_cache.size(); ++i ) {
            ot += ch.outside_cache[i] ? 1u : 0u;
        }
        size_t ft = 0;
        size_t fsz = 0;
        if( above ) {
            fsz = above->floor_cache.size();
            for( size_t i = 0; i < fsz; ++i ) { ft += above->floor_cache[i] ? 1u : 0u; }
        }
        DebugLogFL( DL::Info, DC::Main )
                << "[ocdiag] z=" << zlev << " BUILT rebuild_all=" << rebuild_all
                << " outside_true=" << ot << "/" << ch.outside_cache.size()
                << " have_above=" << ( above ? "yes" : "no" )
                << " above_floor_true=" << ft << "/" << fsz;
    }
    // Tell the render side this cache actually changed; the lighting structure
    // snapshot keys on it so a snapshot taken before the first build cannot stick.
    // Content-gated: see level_cache::outside_checksum for why a bump-on-work would
    // re-snapshot the whole lighting structure on every vehicle move.
    {
        std::uint64_t h = 1469598103934665603ull;
        for( size_t i = 0; i < ch.outside_cache.size(); ++i ) {
            h = ( h ^ static_cast<std::uint64_t>( ch.outside_cache[i] ? 1u : 0u ) ) *
                1099511628211ull;
        }
        if( h != ch.outside_checksum ) {
            ch.outside_checksum = h;
            ++ch.outside_generation;
        }
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
    flush_lightmap_cpu_read_counters();
    const auto valid_lm_levels = std::ranges::count_if(
    std::views::iota( minz, maxz + 1 ), [this]( const int z ) {
        return get_cache_ref( z ).lm_cpu_cache_valid;
    } );
    TracyPlot( "Map CPU LM Valid Levels", static_cast<int64_t>( valid_lm_levels ) );
    TracyPlot( "Map CPU LM Stale Levels",
               static_cast<int64_t>( maxz - minz + 1 - valid_lm_levels ) );
    bool seen_cache_dirty = false;
    bool gpu_transparency_dirty = false;
    bool gpu_floor_dirty = false;
    bool gpu_vehicle_floor_dirty = false;
    bool gpu_vehicle_obscured_dirty = false;
    std::vector<int> dirty_seen_cache_levels;
    std::vector<int> gpu_transparency_dirty_levels;
    std::vector<int> gpu_transparency_residency_invalid_levels;
    std::vector<int> gpu_floor_dirty_levels;
    std::vector<int> gpu_vehicle_floor_dirty_levels;
    std::vector<int> gpu_vehicle_obscured_dirty_levels;

    auto add_gpu_dirty_level = []( auto & levels, const int z ) {
        if( z >= -OVERMAP_DEPTH && z <= OVERMAP_HEIGHT ) {
            levels.push_back( z );
        }
    };
    auto normalize_gpu_dirty_levels = []( auto & levels ) {
        std::ranges::sort( levels );
        levels.erase( std::ranges::unique( levels ).begin(), levels.end() );
    };
    auto level_has_vehicle_floor = []( const level_cache & ch ) {
        return std::ranges::any_of( ch.vehicle_floor_cache, []( const char c ) {
            return c != '\0';
        } );
    };

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
            const _bc::time_point _zt = _bc::now();
            const bool floor_was_dirty = !get_cache_ref( z ).floor_cache_dirty.none();
            const bool _floor_dirty = build_floor_cache( z );
            _zadd( z, _zt );
            // 3D field of view is unconditional now, so every level's floor cache can
            // affect the seen cache.
            if( _floor_dirty ) { seen_cache_dirty = true; }
            if( floor_was_dirty ) {
                gpu_floor_dirty = true;
                add_gpu_dirty_level( gpu_floor_dirty_levels, z );
            }
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
        // build_transparency_caches() batches every level so the GPU transparency pass
        // uploads once, so there is no per-z split to attribute for this phase.
        auto const transparency_dirty_levels = build_transparency_caches( minz, maxz );
        if( !transparency_dirty_levels.empty() ) {
            gpu_transparency_dirty = true;
            std::ranges::copy( transparency_dirty_levels,
                               std::back_inserter( gpu_transparency_dirty_levels ) );
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
                const bool vehicle_floor_was_dirty = level_has_vehicle_floor( ch );
                // vehicle_floor_cache, vehicle_obscured_cache, and vehicle_obstructed_cache all
                // retain stale entries once a vehicle leaves the level (vehicle_floor_cache is
                // written by vehicles one level below via vehicle_caching_internal_above; the
                // other two keep whatever diagonal-block flags the last active vehicle wrote).
                // All three must be cleared unconditionally — not gated on veh_in_active_range —
                // to prevent stale entries from surviving after the vehicle is gone.
                std::fill( ch.vehicle_floor_cache.begin(), ch.vehicle_floor_cache.end(), '\0' );
                const diagonal_blocks fill = {false, false};
                std::fill( ch.vehicle_obscured_cache.begin(), ch.vehicle_obscured_cache.end(),
                           fill );
                std::fill( ch.vehicle_obstructed_cache.begin(),
                           ch.vehicle_obstructed_cache.end(), fill );

                const bool level_seen_dirty = ch.seen_cache_dirty;
                if( level_seen_dirty || vehicle_floor_was_dirty || ch.veh_in_active_range ) {
                    std::lock_guard<std::mutex> lock( dirty_mutex );
                    if( level_seen_dirty ) {
                        seen_cache_dirty = true;
                        dirty_seen_cache_levels.push_back( z );
                    }
                    if( vehicle_floor_was_dirty ) {
                        add_gpu_dirty_level( gpu_vehicle_floor_dirty_levels, z );
                    }
                    if( ch.veh_in_active_range ) {
                        add_gpu_dirty_level( gpu_vehicle_obscured_dirty_levels, z );
                    }
                }
            } );
        } else {
            for( int z = minz; z <= maxz; ++z ) {
                level_cache& ch = get_cache( z );
                const bool vehicle_floor_was_dirty = level_has_vehicle_floor( ch );
                // vehicle_floor_cache, vehicle_obscured_cache, and vehicle_obstructed_cache all
                // retain stale entries once a vehicle leaves the level (vehicle_floor_cache is
                // written by vehicles one level below via vehicle_caching_internal_above; the
                // other two keep whatever diagonal-block flags the last active vehicle wrote).
                // All three must be cleared unconditionally — not gated on veh_in_active_range —
                // to prevent stale entries from surviving after the vehicle is gone.
                std::fill( ch.vehicle_floor_cache.begin(), ch.vehicle_floor_cache.end(), '\0' );
                const diagonal_blocks fill = {false, false};
                std::fill( ch.vehicle_obscured_cache.begin(), ch.vehicle_obscured_cache.end(),
                           fill );
                std::fill( ch.vehicle_obstructed_cache.begin(),
                           ch.vehicle_obstructed_cache.end(), fill );

                const bool level_seen_dirty = ch.seen_cache_dirty;
                if( level_seen_dirty ) {
                    seen_cache_dirty = true;
                    dirty_seen_cache_levels.push_back( z );
                }
                if( vehicle_floor_was_dirty ) {
                    add_gpu_dirty_level( gpu_vehicle_floor_dirty_levels, z );
                }
                if( ch.veh_in_active_range ) {
                    add_gpu_dirty_level( gpu_vehicle_obscured_dirty_levels, z );
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
        auto const mark_vehicle_gpu_structural_levels = [&]( const vehicle * const veh ) {
            if( veh == nullptr ) {
                return;
            }
            for( const vpart_reference &vp : veh->get_all_parts() ) {
                const auto &part_pos = veh->bub_part_location( vp.part() );
                if( !inbounds( part_pos ) || vp.part().removed ) {
                    continue;
                }
                add_gpu_dirty_level( gpu_transparency_dirty_levels, part_pos.z() );
                add_gpu_dirty_level( gpu_transparency_residency_invalid_levels, part_pos.z() );
                add_gpu_dirty_level( gpu_floor_dirty_levels, part_pos.z() );
                add_gpu_dirty_level( gpu_vehicle_obscured_dirty_levels, part_pos.z() );
            }
        };
        for( int z = minz; z <= maxz; z++ ) {
            if( get_cache( z ).veh_in_active_range ) {
                for( const vehicle *const veh : get_cache( z ).vehicle_list ) {
                    mark_vehicle_gpu_structural_levels( veh );
                }
                do_vehicle_caching( z );
            }
        }
        std::ranges::for_each( std::views::iota( minz, maxz + 1 ), [&]( const int z ) {
            if( level_has_vehicle_floor( get_cache_ref( z ) ) ) {
                add_gpu_dirty_level( gpu_vehicle_floor_dirty_levels, z );
            }
        } );
    }
    _lap( _ph_veh );

    normalize_gpu_dirty_levels( gpu_transparency_dirty_levels );
    normalize_gpu_dirty_levels( gpu_transparency_residency_invalid_levels );
    normalize_gpu_dirty_levels( gpu_floor_dirty_levels );
    normalize_gpu_dirty_levels( gpu_vehicle_floor_dirty_levels );
    normalize_gpu_dirty_levels( gpu_vehicle_obscured_dirty_levels );
    gpu_transparency_dirty = !gpu_transparency_dirty_levels.empty();
    gpu_floor_dirty = !gpu_floor_dirty_levels.empty();
    gpu_vehicle_floor_dirty = !gpu_vehicle_floor_dirty_levels.empty();
    gpu_vehicle_obscured_dirty = !gpu_vehicle_obscured_dirty_levels.empty();
#if defined( CATA_SDL )
    if( !gpu_transparency_residency_invalid_levels.empty() ) {
        cata_gpu::invalidate_lighting_transparency_levels( gpu_transparency_residency_invalid_levels );
    }
#endif
    TracyPlot( "Map GPU Transparency Dirty Levels",
               static_cast<int64_t>( gpu_transparency_dirty_levels.size() ) );
    TracyPlot( "Map GPU Floor Dirty Levels",
               static_cast<int64_t>( gpu_floor_dirty_levels.size() ) );
    TracyPlot( "Map GPU Vehicle Floor Dirty Levels",
               static_cast<int64_t>( gpu_vehicle_floor_dirty_levels.size() ) );
    TracyPlot( "Map GPU Vehicle Obscured Dirty Levels",
               static_cast<int64_t>( gpu_vehicle_obscured_dirty_levels.size() ) );
    TracyPlot( "Map GPU Transparency Invalidated Levels",
               static_cast<int64_t>( gpu_transparency_residency_invalid_levels.size() ) );

    const tripoint_bub_ms& p = g->u.bub_pos();
    auto force_seen_rebuild_for_gpu_residency = false;
    // D2: cata_gpu::get_device() hands back the fork renderer's SDL_GPU device, which is
    // null until lighting::gpu_device is ready and in headless test runs.  A null device is
    // upstream's documented "no compute available" contract, so every GPU branch below
    // degrades to the CPU lightmap / CPU shadowcasting path instead of erroring out.
#if defined( CATA_SDL )
    SDL_GPUDevice *const gpu_device = cata_gpu::get_device();
#else
    constexpr std::nullptr_t gpu_device = nullptr;
#endif
#if defined( CATA_SDL )
    if( !skip_lightmap && gpu_device != nullptr ) {
        const auto &visibility_cache = get_cache_ref( zlev );
        if( !cata_gpu::resident_lighting_ready_for_visibility( {
        .device = gpu_device,
        .cache_x = visibility_cache.cache_x,
        .cache_y = visibility_cache.cache_y,
        .z_count = OVERMAP_LAYERS,
    } ) ) {
            force_seen_rebuild_for_gpu_residency = true;
            invalidate_lightmap_caches();
            get_cache( zlev ).visibility_cache_dirty = true;
        }
    }
#endif

    auto dirty_lightmap_levels = std::vector<int> {};
    if( !skip_lightmap ) {
        ZoneScopedN( "Phase4_lightmap_prepare" );
        invalidate_lightmap_caches_if_light_state_changed();
        // Only include levels whose lightmap is actually stale this redraw.
        // lightmap_dirty is marked per-submap by map::shift (loadn), player
        // movement, terrain changes, and explicit invalidate calls (vehicle
        // lights, bionics, etc).  Levels with no dirty submaps are skipped,
        // and their shifted lm array from the last rebuild is reused.
        std::ranges::copy_if( std::views::iota( minz, maxz + 1 ),
                              std::back_inserter( dirty_lightmap_levels ),
        [this]( const int z ) { return get_cache( z ).lightmap_dirty.any(); } );
        TracyPlot( "Map Dirty LM Levels", static_cast<int64_t>( dirty_lightmap_levels.size() ) );

        // [shift-probe] Which levels regenerate lightmap this build.  >1 level on a
        // non-shift turn flags an unexpected all-z driver (residual lightmap spike).
        if( dirty_lightmap_levels.size() > 1 ) {
            std::string _zs;
            for( const int _z : dirty_lightmap_levels ) { _zs += std::to_string( _z ) + ","; }
            DebugLogFL( DL::Info, DC::Main )
                    << "[shift-probe][lightmap] regen " << dirty_lightmap_levels.size()
                    << " levels: " << _zs;
        }
    }

#if defined( CATA_SDL )
    auto pending_gpu_lighting = cata_gpu::gpu_lighting_work {};
    if( !skip_lightmap && gpu_device != nullptr && !dirty_lightmap_levels.empty() ) {
        ZoneScopedN( "Phase4_lightmap_begin" );
        update_solar_params();
        // GPU path: lightmap rebuilds only run for lightmap-dirty levels.
        // Player movement and other FoV-only updates are handled by
        // update_visibility_cache(), which can rebuild resident seen data.
        for( const int z : dirty_lightmap_levels ) {
            auto &c = get_cache( z );
            std::fill( c.sm.begin(), c.sm.end(), 0.0f );
            std::fill( c.light_source_buffer.begin(), c.light_source_buffer.end(),
                       level_cache::buffered_light_source{} );
            c.light_source_points.clear();
            std::ranges::fill( c.lm, 0.0f );
            // The GPU lighting pass does not write the fork's CPU coloured-light lane, so
            // zero it rather than let the previous CPU frame's tints be painted over a
            // GPU-lit world.  Producing real coloured light on the GPU path is follow-up
            // work, not part of this merge.
            std::ranges::fill( c.light_color_cache, light_color_rgb{} );
            c.has_colored_lights = false;
            c.lm_cpu_cache_valid = false;
            ++c.lm_cpu_cache_generation;
        }
        pending_gpu_lighting = cata_gpu::begin_gpu_lighting( gpu_device, {
            .m            = this,
            .dirty_levels = &dirty_lightmap_levels,
            .seen_dirty_levels = &dirty_seen_cache_levels,
            .player_x     = p.x(),
            .player_y     = p.y(),
            .player_zlev  = zlev,
            .transparency_dirty = gpu_transparency_dirty,
            .transparency_dirty_levels = &gpu_transparency_dirty_levels,
            .floor_dirty = gpu_floor_dirty,
            .floor_dirty_levels = &gpu_floor_dirty_levels,
            .vehicle_floor_dirty = gpu_vehicle_floor_dirty,
            .vehicle_floor_dirty_levels = &gpu_vehicle_floor_dirty_levels,
            .vehicle_obscured_dirty = gpu_vehicle_obscured_dirty,
            .vehicle_obscured_dirty_levels = &gpu_vehicle_obscured_dirty_levels,
            .rebuild_seen_cache = true,
            .download_seen_cache = true,
            .download_lightmap = true,
            .vision_block_mask = vision_transparency_block_mask(),
            .angled_sunlight_shadows = angled_sunlight_shadows,
            .direct_sunlight = m_solar.direct_active,
            .sun_dx_per_z = m_solar.dx_per_z,
            .sun_dy_per_z = m_solar.dy_per_z,
        } );
        if( pending_gpu_lighting.id == 0 ) {
            debugmsg( "SDL_GPU lighting dispatch failed; see debug.log for details" );
            return;
        }
        if( submap_loader.has_deferred_lazy_border_work() ) {
            ZoneScopedN( "Phase4_lightmap_pending_lazy_border" );
            submap_loader.process_deferred_lazy_border_work();
        }
    }
#endif

    {
        ZoneScopedN( "Phase3_sound_absorption" );
        // Absorption cache relies upon the floor, outside, and vehicle caches all being completed.
        for( int z = minz; z <= maxz; z++ ) { build_absorption_cache( z ); }
    }

    seen_cache_dirty |= build_vision_transparency_cache( get_player_character() );

    if( seen_cache_dirty ) { skew_vision_cache.assign( vision_cache_slots, vision_cache_slot{} ); }
    const auto need_seen_rebuild = seen_cache_dirty || force_seen_rebuild_for_gpu_residency ||
                                   m_last_seen_cache_origin != p;
    TracyPlot( "Map Need Seen Rebuild", need_seen_rebuild ? int64_t{ 1 } : int64_t{ 0 } );
    if( need_seen_rebuild ) {
        if( gpu_device != nullptr ) {
            // The GPU visibility pass owns the resident seen data; force the next
            // update_visibility_cache() to re-read it instead of trusting the origin.
            m_last_seen_cache_origin = tripoint_bub_ms( tripoint_min );
        } else {
            build_seen_cache( p, zlev );
            m_last_seen_cache_origin = p;
        }
        // seen_cache changed (or will be updated by the GPU pass); mark visibility stale.
        get_cache( zlev ).visibility_cache_dirty = true;
    }
    _lap( _ph_seen );

    if( !skip_lightmap ) {
        if( gpu_device != nullptr ) {
#if defined( CATA_SDL )
            if( !dirty_lightmap_levels.empty() ) {
                ZoneScopedN( "Phase4_lightmap_finish" );
                const bool gpu_lighting_ok = cata_gpu::finish_gpu_lighting( gpu_device,
                                             pending_gpu_lighting );
                if( !gpu_lighting_ok ) {
                    debugmsg( "SDL_GPU lighting completion failed; see debug.log for details" );
                    return;
                }
                // The GPU path never enters generate_lightmap_worker, which is where the CPU
                // path latches lightmap readiness.  Latch it here too, or
                // lightmap_ever_generated() stays false for the whole run and sprite lighting
                // classifies the entire world as unlit.
                mark_lightmap_generated();

                std::ranges::for_each( dirty_lightmap_levels, [this]( int z ) {
                    get_cache( z ).lightmap_dirty.reset();
                    get_cache( z ).visibility_cache_dirty = true;
                } );
            }
#endif
        } else {
            ZoneScopedN( "Phase4_lightmap" );
            if( !dirty_lightmap_levels.empty() ) {

                if( dirty_lightmap_levels.size() > 1 && parallel_enabled && parallel_map_cache ) {
                    // Multiple dirty levels: hoist shared initialization outside the
                    // parallel loop so worker threads never race on cross-level writes.
                    //
                    // Always run the sunlight cascade in the multi-level path because
                    // shift+loadn gives new-edge submaps stale lm values (from the old
                    // grid position's terrain).  Skipping the cascade here would leave
                    // those submaps with incorrect sunlight — causing a visible flash.
                    for( const int z : dirty_lightmap_levels ) {
                        auto &c = get_cache( z );
                        std::fill( c.sm.begin(), c.sm.end(), 0.0f );
                        std::fill( c.light_source_buffer.begin(), c.light_source_buffer.end(),
                                   level_cache::buffered_light_source{} );
                        c.light_source_points.clear();
                        // lm must be zeroed because build_sunlight_cache only writes outdoor tiles.
                        std::ranges::fill( c.lm, 0.0f );
                    }
                    // Build sunlight (all z-levels, top-to-bottom; serial).
                    build_sunlight_cache( zlev );
                    // Generate per-level dynamic lighting in parallel.
                    // skip_shared_init=true: workers only process entities on their own z-level.
                    // Pre-warm the vehicle list cache serially to avoid heap corruption
                    // from concurrent writes to last_full_vehicle_list.
                    get_vehicles();
                    parallel_for( 0, static_cast<int>( dirty_lightmap_levels.size() ), [&]( int i ) {
                        generate_lightmap_worker( dirty_lightmap_levels[i] );
                    } );
                } else {
                    // Single dirty level: run serially using the standard full path.
                    for( const int level : dirty_lightmap_levels ) { generate_lightmap( level ); }
                }

                // Diagnostic: log dirty-submap fraction for each regenerated level so the
                // per-submap scaling can be verified in debug.log.
                for( const int z : dirty_lightmap_levels ) {
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
                std::ranges::for_each( dirty_lightmap_levels, [this]( int z ) {
                    get_cache( z ).lightmap_dirty.reset();
                    get_cache( z ).lm_cpu_cache_valid = true;
                    get_cache( z ).visibility_cache_dirty = true;
                } );

            } // end if( !dirty_lightmap_levels.empty() )

            // Always apply entity lights on the CPU path when lightmap processing is
            // enabled, regardless of submap dirtiness.  Entity lights track current
            // position + state of creatures and are cheap (a few ray casts).  The GPU
            // path collects the same character/NPC/monster lights itself (see
            // compute/gpu_lm.cpp), so doing this on that path would double-apply them
            // on top of the downloaded lm.
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

