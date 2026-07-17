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

static location_vector<item> nulitems( new fake_item_location() ); // Returned when &i_at() is asked
// for an OOB value
static field nulfield;        // Returned when &field_at() is asked for an OOB value
static level_cache nullcache; // Dummy cache for z-levels outside bounds

bool disable_mapgen = false;

// Thread-local context for get_map().  Null means "use the global g->m."
// Worker threads never push a context, so they always fall through to g->m.
static thread_local map *tl_map_context = nullptr;

map &get_map() { return tl_map_context ? *tl_map_context : g->m; }

scoped_map_context::scoped_map_context( map& m ) noexcept: prev_( tl_map_context )
{
    tl_map_context = &m;
}

scoped_map_context::~scoped_map_context() noexcept { tl_map_context = prev_; }

// Map stack methods.
map_stack::iterator map_stack::erase( map_stack::const_iterator it, detached_ptr<item> *out )
{
    return myorigin->i_rem( location, std::move( it ), out );
}

void map_stack::insert( detached_ptr<item>&& newitem )
{
    myorigin->add_item_or_charges( location, std::move( newitem ) );
}
detached_ptr<item> map_stack::remove( item* to_remove )
{
    return myorigin->i_rem( location, to_remove );
}

units::volume map_stack::max_volume() const
{
    if( myorigin->has_furn( location ) ) {
    return myorigin->furn( location ).obj().max_volume;
    }
    return myorigin->ter( location ).obj().max_volume;
}

// Map class methods.

map::map( int mapsize, bool zlev )
{
    my_MAPSIZE = mapsize;
    zlevels = zlev;
    if( zlevels ) {
        grid.resize( static_cast<size_t>( my_MAPSIZE * my_MAPSIZE * OVERMAP_LAYERS ), nullptr );
    } else {
        grid.resize( static_cast<size_t>( my_MAPSIZE * my_MAPSIZE ), nullptr );
    }

    for( auto& ptr : caches ) {
        ptr = std::make_unique<level_cache>( SEEX * mapsize, SEEY * mapsize );
    }

    dbg( DL::Info ) << "map::map(): my_MAPSIZE: " << my_MAPSIZE << " z-levels enabled:" << zlevels;
    skew_vision_cache_mutex = std::make_unique<std::shared_mutex>();
    skew_vision_cache.resize( vision_cache_slots );
#ifdef BOX2D_ENABLED
    phys_world = std::make_unique<physics::PhysicsWorld>();
#endif
}

// Defined out-of-line so we can use g_mapsize (runtime) rather than the
// compile-time MAPSIZE constant that would be baked in by the delegating-
// constructor call expression in the header.
map::map( bool zlev ): map( g_mapsize, zlev ) {}

map::~map() = default;
map &map::operator=( map && ) noexcept = default;

auto map::resize( int new_mapsize ) -> void
{
    // Clear any stale pointers before reallocating — safety barrier against
    // dereferencing old pointers before grid.assign() overwrites everything.
    std::fill( grid.begin(), grid.end(), nullptr );
    my_MAPSIZE = new_mapsize;
    const auto grid_sz = static_cast<size_t>(
                             zlevels
                             ? my_MAPSIZE * my_MAPSIZE * OVERMAP_LAYERS
                             : my_MAPSIZE * my_MAPSIZE );
    grid.assign( grid_sz, nullptr );
    for( auto &ptr : caches ) {
    ptr = std::make_unique<level_cache>( SEEX * new_mapsize, SEEY * new_mapsize );
    }
    submaps_with_active_items.clear();
    loaded_vehicles.clear();
    funnel_locations_.clear();
    // Recompute the circular load footprint for the new bubble radius.
    // Radius = (mapsize - 1) / 2, matching g_half_mapsize.
    submap_loader.update_load_shape( ( new_mapsize - 1 ) / 2 );
    dbg( DL::Info ) << "map::resize(): my_MAPSIZE: " << my_MAPSIZE;
}

std::optional<pocket_dimension_data> map::get_pocket_info() const { return pocket_info_; }

void map::set_pocket_info( const pocket_dimension_data& info ) { pocket_info_ = info; }

void map::clear_pocket_info() { pocket_info_.reset(); }

void map::clear_grid() { std::fill( grid.begin(), grid.end(), nullptr ); }

bool map::has_dimension_bounds() const { return pocket_info_.has_value(); }

bool map::is_out_of_bounds( const tripoint_bub_ms &p ) const
{
    if( !pocket_info_ ) {
    return false;  // No bounds means infinite dimension
}
return !pocket_info_->bounds.contains( bub_to_abs( p ) );
}

ter_id map::get_boundary_terrain() const
{
    if( pocket_info_ && pocket_info_->bounds.boundary_terrain.is_valid() ) {
    return pocket_info_->bounds.boundary_terrain.id();
    }
    // Fallback to t_null if no boundary terrain is set
    return t_null;
}

void map::bind_dimension( const std::string& dim ) { bound_dimension_ = dim; }

bool map::contains_abs_sm( const tripoint_abs_sm& p ) const
{
    if( p.x() < abs_sub.x() || p.x() >= abs_sub.x() + my_MAPSIZE ) { return false; }
    if( p.y() < abs_sub.y() || p.y() >= abs_sub.y() + my_MAPSIZE ) { return false; }
    if( zlevels ) { return p.z() >= -OVERMAP_DEPTH && p.z() <= OVERMAP_HEIGHT; }
    return p.z() == abs_sub.z();
}

void map::on_submap_loaded( const tripoint_abs_sm& p, const std::string& dim_id )
{
    if( dim_id != bound_dimension_ ) { return; }

    // Submap lookup — shared by vehicle fixup, active-item tracking, and grid update.
    submap* sm = MAPBUFFER_REGISTRY.get( dim_id ).lookup_submap_in_memory( p );

    // Vehicle abs_sm_pos fixup and loaded_vehicles registration.
    // Covers all loaded submaps, including out-of-bubble ones.
    // For in-bubble submaps loadn() has already done this; the set insert is idempotent.
    if( sm != nullptr && !sm->vehicles.empty() ) {
        // Extended local grid index: may be outside [0, my_MAPSIZE) for out-of-bubble.
        for( const auto& veh : sm->vehicles ) {
            veh->abs_sm_pos = p;
            veh->dimension_id_ = dim_id;
            loaded_vehicles.insert( veh.get() );
        }
    }

    // Track submaps with active items across the full loaded set, not just the
    // reality bubble.  For in-bubble submaps loadn() also does this; idempotent.
    if( sm != nullptr && !sm->active_items.empty() ) { submaps_with_active_items.emplace( p ); }

    // Register any funnel traps so fill_water_collectors can skip the mapbuffer scan.
    if( sm != nullptr && !sm->trap_cache.empty() ) {
        std::ranges::for_each( sm->trap_cache, [&]( const point_sm_ms & lp ) {
            if( sm->get_trap( lp ).obj().is_funnel() ) { funnel_locations_.emplace_back( p, lp ); }
        } );
    }

    if( !contains_abs_sm( p ) ) { return; }
    const int lx = p.x() - abs_sub.x();
    const int ly = p.y() - abs_sub.y();
    // Index formula must match get_nonant(): in z-level builds the layout is
    //   (x + y * MAPSIZE) * OVERMAP_LAYERS + (z + OVERMAP_HEIGHT)
    // i.e. z is the INNERMOST (fastest-changing) dimension, not the outermost.
    const int grid_idx =
        zlevels ? ( lx + ly * my_MAPSIZE ) * OVERMAP_LAYERS + ( p.z() + OVERMAP_HEIGHT )
        : lx + ly * my_MAPSIZE;
    if( grid_idx >= 0 && grid_idx < static_cast<int>( grid.size() ) ) {
        // Reuse the submap pointer from the lookup above if available;
        // it may be null if the submap was not yet in memory at notification time.
        if( sm != nullptr ) { grid[grid_idx] = sm; }
        // If sm is still null the submap is not yet in memory; leave the grid slot
        // as set by loadn() (which may already hold a valid pointer).
    }
#ifdef BOX2D_ENABLED
    if( phys_world && sm != nullptr ) { phys_world->on_submap_loaded( *this, p ); }
#endif
}

void map::on_submap_unloaded( const tripoint_abs_sm& pos, const std::string& dim_id )
{
    if( dim_id != bound_dimension_ ) { return; }

    // Stamp the departure time so actualize() computes the correct time-since-simulated
    // on the next load.  Only meaningful for submaps that were actually simulated;
    // border-preloaded submaps that were never in the simulation set should not be
    // touched (their last_touched reflects their real generate-or-load time).
    if( submap_loader.is_in_simulated_set( dim_id, pos ) ) {
        submap* sm = MAPBUFFER_REGISTRY.get( dim_id ).lookup_submap_in_memory( pos );
        if( sm ) { sm->last_touched = calendar::turn; }
    }

    // Vehicle tracking: remove all vehicles whose home submap matches the unloaded position.
    {
        std::erase_if( loaded_vehicles, [&]( vehicle * veh ) { return veh->abs_sm_pos == pos; } );
    }
#ifdef BOX2D_ENABLED
    if( phys_world ) { phys_world->on_submap_unloaded( pos ); }
#endif

    // Stop tracking active items for this submap.
    submaps_with_active_items.erase( pos );

    // Remove any funnel locations belonging to this submap.
    std::erase_if( funnel_locations_, [&]( const auto & e ) { return e.first == pos; } );

    if( !contains_abs_sm( pos ) ) { return; }
    const auto& local_p = abs_to_bub( pos ).xy();
    // Index formula must match get_nonant() — see on_submap_loaded for details.
    const int grid_idx =
        zlevels
        ? ( local_p.x() + local_p.y() * my_MAPSIZE ) * OVERMAP_LAYERS + ( pos.z() + OVERMAP_HEIGHT )
        : local_p.x() + local_p.y() * my_MAPSIZE;
    if( grid_idx >= 0 && grid_idx < static_cast<int>( grid.size() ) ) { grid[grid_idx] = nullptr; }
}

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

static submap null_submap( tripoint_abs_sm::zero() );

maptile map::maptile_at( const tripoint_bub_ms& p ) const { return maptile_at_internal( p ); }

maptile map::maptile_at_internal( const tripoint_bub_ms& p ) const
{
    point_sm_ms l;
    submap* const sm = get_submap_at( tripoint_bub_ms( p ), l );
    if( sm == nullptr ) { return maptile( &null_submap, point_sm_ms{} ); }

    return maptile( sm, l );
}

// Vehicle functions




bool map::displace_water( const tripoint_bub_ms& p )
{
    // Check for shallow water
    if( has_flag( "SWIMMABLE", p ) && !has_flag( TFLAG_DEEP_WATER, p ) ) {
        int dis_places = 0;
        int sel_place = 0;
        for( int pass = 0; pass < 2; pass++ ) {
            // we do 2 passes.
            // first, count how many non-water places around
            // then choose one within count and fill it with water on second pass
            if( pass != 0 ) {
                sel_place = rng( 0, dis_places - 1 );
                dis_places = 0;
            }
            for( const auto& temp : points_in_radius( p, 1 ) ) {
                if( temp != p || impassable_ter_furn( temp ) || has_flag( TFLAG_DEEP_WATER, temp ) ) {
                    continue;
                }
                ter_id ter0 = ter( temp );
                if( ter0 == t_water_sh || ter0 == t_water_dp || ter0 == t_water_moving_sh
                    || ter0 == t_water_moving_dp ) {
                    continue;
                }
                if( pass != 0 && dis_places == sel_place ) {
                    ter_set( temp, t_water_sh );
                    ter_set( temp, t_dirt );
                    return true;
                }

                dis_places++;
            }
        }
    }
    return false;
}

// End of 3D vehicle

/*
 * Get the terrain integer id. This is -not- a number guaranteed to remain
 * the same across revisions; it is a load order, and can change when mods
 * are loaded or removed. The old t_floor style constants will still work but
 * are -not- guaranteed; if a mod removes t_lava, t_lava will equal t_null;
 * New terrains added to the core game generally do not need this, it's
 * retained for high performance comparisons, save/load, and gradual transition
 * to string terrain.id
 */
data_vars::data_set *map::ter_vars( const tripoint_bub_ms &p ) const
{
    if( !inbounds( p ) ) {
    return nullptr;
}

point_sm_ms l;
const auto sm = get_submap_at( tripoint_bub_ms( p ), l );
return &sm->get_ter_vars( l );
}


data_vars::data_set *map::furn_vars( const tripoint_bub_ms &p ) const
{
    if( !inbounds( p ) ) {
    return nullptr;
}

point_sm_ms l;
const auto sm = get_submap_at( tripoint_bub_ms( p ), l );
return &sm->get_furn_vars( l );
}

/*
 * Get the results of harvesting this tile's furniture or terrain
 */
const harvest_id &map::get_harvest( const tripoint_bub_ms& pos ) const
{
    const auto furn_here = furn( pos );
    if( furn_here->examine != iexamine::none ) {
        // Note: if furniture can be examined, the terrain can NOT (until furniture is removed)
        if( furn_here->has_flag( TFLAG_HARVESTED ) ) { return harvest_id::NULL_ID(); }

        return furn_here->get_harvest();
    }

    const auto ter_here = ter( pos );
    if( ter_here->has_flag( TFLAG_HARVESTED ) ) { return harvest_id::NULL_ID(); }

    return ter_here->get_harvest();
}

const std::set<std::string> &map::get_harvest_names( const tripoint_bub_ms& pos ) const
{
    static const std::set<std::string> null_harvest_names = {};
    const auto furn_here = furn( pos );
    if( furn_here->examine != iexamine::none ) {
        if( furn_here->has_flag( TFLAG_HARVESTED ) ) { return null_harvest_names; }

        return furn_here->get_harvest_names();
    }

    const auto ter_here = ter( pos );
    if( ter_here->has_flag( TFLAG_HARVESTED ) ) { return null_harvest_names; }

    return ter_here->get_harvest_names();
}

/*
 * Get the terrain transforms_into id (what will the terrain transforms into)
 */
/**
 * Examines the tile pos, with character as the "examinator"
 * Casts Character to player because player/NPC split isn't done yet
 */
/*
 * set terrain via string; this works for -any- terrain id
 */
// Move cost: 3D

// End of move cost

void map::drop_everything( const tripoint_bub_ms& p )
{
    // Do a suspension check so that there won't be a floor there for the rest of this check.
    if( has_flag( "SUSPENDED", p ) ) { collapse_invalid_suspension( p ); }
    if( has_floor( p ) ) { return; }

    drop_furniture( p );
    drop_items( p );
    drop_vehicle( p );
    drop_fields( p );
}

void map::drop_furniture( const tripoint_bub_ms& p )
{
    const furn_id frn = furn( p );
    if( frn == f_null ) { return; }

    enum support_state {
        SS_NO_SUPPORT = 0,
        SS_BAD_SUPPORT, // TODO: Implement bad, shaky support
        SS_GOOD_SUPPORT,
        SS_FLOOR, // Like good support, but bash floor instead of tile below
        SS_CREATURE
    };

    // Checks if the tile:
    // has floor (supports unconditionally)
    // has support below
    // has unsupporting furniture below (bad support, things should "slide" if possible)
    // has no support and thus allows things to fall through
    const auto check_tile = [this]( const tripoint_bub_ms & pt ) {
        if( has_floor( pt ) ) { return SS_FLOOR; }

        tripoint_bub_ms below_dest( pt.xy(), pt.z() - 1 );
        if( supports_above( below_dest ) ) { return SS_GOOD_SUPPORT; }

        const furn_id frn_id = furn( below_dest );
        if( frn_id != f_null ) {
            const furn_t &frn = frn_id.obj();
            // Allow crushing tiny/nocollide furniture
            if( !frn.has_flag( "TINY" ) && !frn.has_flag( "NOCOLLIDE" ) ) { return SS_BAD_SUPPORT; }
        }

        if( g->critter_at( below_dest ) != nullptr ) {
            // Smash a critter
            return SS_CREATURE;
        }

        return SS_NO_SUPPORT;
    };

    tripoint_bub_ms current( p.xy(), p.z() + 1 );
    support_state last_state = SS_NO_SUPPORT;
    while( last_state == SS_NO_SUPPORT && current.z() > -OVERMAP_DEPTH ) {
        current.z()--;
        // Check current tile
        last_state = check_tile( current );
    }

    if( current == p ) {
        // Nothing happened
        if( last_state != SS_FLOOR ) { support_dirty( current ); }

        return;
    }

    furn_set( p, f_null );
    furn_set( current, frn );

    // If it's sealed, we need to drop items with it
    const auto& frn_obj = frn.obj();
    if( frn_obj.has_flag( TFLAG_SEALED ) && has_items( p ) ) {
        auto old_items = i_at( p );
        auto new_items = i_at( current );

        old_items.move_all_to( &new_items );
    }

    // Approximate weight/"bulkiness" based on strength to drag
    int weight;
    if( frn_obj.has_flag( "TINY" ) || frn_obj.has_flag( "NOCOLLIDE" ) ) {
        weight = 5;
    } else {
        weight = frn_obj.is_movable() ? frn_obj.move_str_req : 20;
    }

    if( frn_obj.has_flag( "ROUGH" ) || frn_obj.has_flag( "SHARP" ) ) { weight += 5; }

    // TODO: Balance this.
    int dmg = weight * ( p.z() - current.z() );

    if( last_state == SS_FLOOR ) {
        // Bash the same tile twice - once for furniture, once for the floor
        bash( current, dmg, false, false, true );
        bash( current, dmg, false, false, true );
    } else if( last_state == SS_BAD_SUPPORT || last_state == SS_GOOD_SUPPORT ) {
        bash( current, dmg, false, false, false );
        tripoint_bub_ms below( current.xy(), current.z() - 1 );
        bash( below, dmg, false, false, false );
    } else if( last_state == SS_CREATURE ) {
        const std::string& furn_name = frn_obj.name();
        bash( current, dmg, false, false, false );
        tripoint_bub_ms below( current.xy(), current.z() - 1 );
        Creature* critter = g->critter_at( below );
        if( critter == nullptr ) {
            debugmsg( "drop_furniture couldn't find creature at %d,%d,%d", below.x(), below.y(),
                      below.z() );
            return;
        }

        critter->add_msg_player_or_npc(
            m_bad, _( "Falling %s hits you!" ), _( "Falling %s hits <npcname>" ), furn_name );
        // TODO: A chance to dodge/uncanny dodge
        player* pl = dynamic_cast<player *>( critter );
        monster* mon = dynamic_cast<monster *>( critter );
        if( pl != nullptr ) {
            pl->deal_damage( nullptr, bodypart_id( "torso" ),
                             damage_instance( DT_BASH, rng( dmg / 3, dmg ), 0, 0.5f ) );
            pl->deal_damage(
                nullptr, bodypart_id( "head" ), damage_instance( DT_BASH, rng( dmg / 3, dmg ), 0, 0.5f ) );
            pl->deal_damage( nullptr, bodypart_id( "leg_l" ),
                             damage_instance( DT_BASH, rng( dmg / 2, dmg ), 0, 0.4f ) );
            pl->deal_damage( nullptr, bodypart_id( "leg_r" ),
                             damage_instance( DT_BASH, rng( dmg / 2, dmg ), 0, 0.4f ) );
            pl->deal_damage( nullptr, bodypart_id( "arm_l" ),
                             damage_instance( DT_BASH, rng( dmg / 2, dmg ), 0, 0.4f ) );
            pl->deal_damage( nullptr, bodypart_id( "arm_r" ),
                             damage_instance( DT_BASH, rng( dmg / 2, dmg ), 0, 0.4f ) );
        } else if( mon != nullptr ) {
            // TODO: Monster's armor and size - don't crush hulks with chairs
            mon->apply_damage( nullptr, bodypart_id( "torso" ), rng( dmg, dmg * 2 ) );
        }
    }

    // Re-queue for another check, in case bash destroyed something
    support_dirty( current );
}

void map::drop_items( const tripoint_bub_ms& p )
{
    if( !has_items( p ) ) { return; }

    auto items = i_at( p );
    // TODO: Make items check the volume tile below can accept
    // rather than disappearing if it would be overloaded

    tripoint_bub_ms below( p );
    while( below.z() >= -OVERMAP_DEPTH && !has_floor( below ) ) { below.z()--; }

    if( below == p ) { return; }
    map_stack stack = i_at( below );
    items.move_all_to( &stack );

    // TODO: Bash the item up before adding it
    // TODO: Bash the creature, terrain, furniture and vehicles on the tile
    // Just to make a sound for now
    bash( below, 1 );
    i_clear( p );
}

void map::drop_vehicle( const tripoint_bub_ms& p )
{
    const optional_vpart_position vp = veh_at( p );
    if( !vp ) { return; }

    vp->vehicle().is_falling = true;
}

void map::drop_fields( const tripoint_bub_ms& p )
{
    field& fld = field_at( p );
    if( fld.field_count() == 0 ) { return; }

    std::list<field_type_id> dropped;
    const tripoint_bub_ms below = p + tripoint_below;
    for( const auto& iter : fld ) {
        const field_entry& entry = iter.second;
        // For now only drop cosmetic fields, which don't warrant per-turn check
        // Active fields "drop themselves"
        if( entry.decays_on_actualize() ) {
            add_field( below, entry.get_field_type(), entry.get_field_intensity(),
                       entry.get_field_age() );
            dropped.push_back( entry.get_field_type() );
        }
    }

    for( const auto& entry : dropped ) { fld.remove_field( entry ); }
}

void map::support_dirty( const tripoint_bub_ms& p )
{
    if( zlevels ) { support_cache_dirty.insert( p ); }
}

void map::process_falling()
{
    ZoneScoped;

    if( !zlevels ) {
        support_cache_dirty.clear();
        return;
    }

    if( !support_cache_dirty.empty() ) {
        add_msg( m_debug, "Checking %d tiles for falling objects", support_cache_dirty.size() );
        // We want the cache to stay constant, but falling can change it
        std::set<tripoint_bub_ms> last_cache = std::move( support_cache_dirty );
        support_cache_dirty.clear();
        for( const tripoint_bub_ms& p : last_cache ) { drop_everything( p ); }
    }
}


void map::add_splatter( const field_type_id& type, const tripoint_bub_ms& where, int intensity )
{
    if( !type.id() || intensity <= 0 ) { return; }
    if( type.obj().is_splattering ) {
        if( const optional_vpart_position vp = veh_at( where ) ) {
            vehicle* const veh = &vp->vehicle();
            // Might be -1 if all the vehicle's parts at where are marked for removal
            const int part = veh->part_displayed_at( vp->mount() );
            if( part != -1 ) {
                veh->part( part ).blood += 200 * std::min( intensity, 3 ) / 3;
                return;
            }
        }
    }
    mod_field_intensity( where, type, intensity );
}

void map::add_splatter_trail(
    const field_type_id& type, const tripoint_bub_ms& from, const tripoint_bub_ms& to )
{
    if( !type.id() ) { return; }
    auto trail = line_to( from, to );
    int remainder = trail.size();
    tripoint_bub_ms last_point = from;
    for( tripoint_bub_ms& elem : trail ) {
        add_splatter( type, elem );
        remainder--;
        if( obstructed_by_vehicle_rotation( last_point, elem ) ) {
            if( one_in( 2 ) ) {
                elem.x() = last_point.x();
                add_splatter( type, elem, remainder );
            } else {
                elem.y() = last_point.y();
                add_splatter( type, elem, remainder );
            }
            return;
        }
        if( impassable( elem ) ) { // Blood splatters stop at walls.
            add_splatter( type, elem, remainder );
            return;
        }
        last_point = elem;
    }
}

void map::add_splash(
    const field_type_id& type, const tripoint_bub_ms& center, int radius, int intensity )
{
    if( !type.id() ) { return; }
    // TODO: use Bresenham here and take obstacles into account
    for( const tripoint_bub_ms& pnt : points_in_radius( center, radius ) ) {
        if( trig_dist( pnt, center ) <= radius && !one_in( intensity ) ) { add_splatter( type, pnt ); }
    }
}

computer *map::computer_at( const tripoint_bub_ms& p )
{
    point_sm_ms l;
    submap* const sm = get_submap_at( tripoint_bub_ms( p ), l );
    return sm ? sm->get_computer( l ) : nullptr;
}

void map::update_submap_active_item_status( const tripoint_bub_ms& p )
{
    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );
    if( current_submap->active_items.empty() ) {
        submaps_with_active_items.erase(
            tripoint_abs_sm( abs_sub.x() + p.x() / SEEX, abs_sub.y() + p.y() / SEEY, p.z() ) );
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

const visibility_variables &map::get_visibility_variables_cache() const
{
    return visibility_variables_cache;
}

visibility_type map::get_visibility( const lit_level ll,
                                     const visibility_variables &cache ) const
{
    switch( ll ) {
    case lit_level::DARK:
        // can't see this square at all
        if( cache.u_is_boomered ) {
                return VIS_BOOMER_DARK;
            } else {
                return VIS_DARK;
            }
        case lit_level::BRIGHT_ONLY:
            // can only tell that this square is bright
            if( cache.u_is_boomered ) {
                return VIS_BOOMER;
            } else {
                return VIS_LIT;
            }

        case lit_level::LOW:
        // low light, square visible in monochrome
        case lit_level::LIT:
        // normal light
        case lit_level::BRIGHT:
            // bright light
            return VIS_CLEAR;
        case lit_level::BLANK:
        case lit_level::MEMORIZED:
            return VIS_HIDDEN;
    }
    return VIS_HIDDEN;
}

// a check to see if the lower floor needs to be rendered in tiles
bool map::dont_draw_lower_floor( const tripoint_bub_ms& p )
{
    return !zlevels || p.z() <= -OVERMAP_DEPTH
           || !( has_flag( TFLAG_NO_FLOOR, p ) || has_flag( TFLAG_Z_TRANSPARENT, p ) );
}


bool map::sees( const tripoint_bub_ms& F, const tripoint_bub_ms& T, const int range ) const
{
    int dummy = 0;
    return sees( F, T, range, dummy );
}

/**
 * This one is internal-only, we don't want to expose the slope tweaking ickiness outside the map
 * class.
 **/
bool map::sees(
    const tripoint_bub_ms& F, const tripoint_bub_ms& T, const int range,
    int &bresenham_slope ) const
{
    if( ( range >= 0 && range < rl_dist( F, T ) ) || !inbounds( T ) || !inbounds( F ) ) {
        bresenham_slope = 0;
        return false; // Out of range!
    }
    // Cannonicalize the order of the tripoints so the cache is reflexive.
    const tripoint_bub_ms& min = F < T ? F : T;
    const tripoint_bub_ms& max = !( F < T ) ? F : T;
    // Pack two tripoints into one int64_t: 29 bits each (12 x + 12 y + 5 z).
    // Handles coordinates up to 4095 — safe for g_mapsize up to ~340.
    auto pack_tp = []( const tripoint_bub_ms & p ) -> int64_t {
        return ( static_cast<int64_t>( p.x() ) & 0xFFF ) << 17 |
            ( static_cast<int64_t>( p.y() ) & 0xFFF ) <<  5 |
                ( static_cast<int64_t>( p.z() + OVERMAP_DEPTH ) & 0x1F );
    };
    const int64_t key = ( pack_tp( min ) << 29 ) | pack_tp( max );
    // P-6 / PERF-LOSS-1: shared_lock for the cache lookup so concurrent readers
    // don't serialize against each other.  The ray trace runs fully unlocked.
    const auto slot_idx = std::hash<int64_t> {}( key ) & ( vision_cache_slots - 1 );
    {
        std::shared_lock<std::shared_mutex> lock( *skew_vision_cache_mutex );
        const auto& slot = skew_vision_cache[slot_idx];
        if( slot.key == key && slot.value >= 0 ) { return slot.value > 0; }
    }

    bool visible = true;

    // Ugly `if` for now
    if( !fov_3d || F.z() == T.z() ) {

        auto last_point = F.xy();
        // Please someone make bresenham work with typed points, I'm running out of willpower
        bresenham( F.xy().raw(), T.xy().raw(), bresenham_slope,
        [this, &visible, &T, &last_point]( const point & new_point ) {
            // Exit before checking the last square, it's still visible even if opaque.
            if( new_point.x == T.x() && new_point.y == T.y() ) { return false; }
            if( !this->is_transparent( tripoint_bub_ms( point_bub_ms( new_point ), T.z() ) )
                || obscured_by_vehicle_rotation(
                    tripoint_bub_ms( last_point, T.z() ),
                    tripoint_bub_ms( point_bub_ms( new_point ), T.z() ) ) ) {
                visible = false;
                return false;
            }
            last_point = point_bub_ms( new_point );
            return true;
        } );
        {
            std::unique_lock<std::shared_mutex> lock( *skew_vision_cache_mutex );
            skew_vision_cache[slot_idx] = {key, static_cast<char>( visible ? 1 : 0 )};
        }
        return visible;
    }

    auto last_point = F;
    bresenham(
        F.raw(), T.raw(), bresenham_slope, 0,
    [this, &visible, &T, &last_point]( const tripoint & new_point ) {
        // Exit before checking the last square if it's not a vertical transition,
        // it's still visible even if opaque.
        if( new_point == T.raw() && last_point.z() == T.z() ) { return false; }

        // TODO: Allow transparent floors (and cache them!)
        if( new_point.z == last_point.z() ) {
            if( !this->is_transparent( tripoint_bub_ms( new_point ) )
                || obscured_by_vehicle_rotation( last_point, tripoint_bub_ms( new_point ) ) ) {
                visible = false;
                return false;
            }
        } else {
            const int max_z = std::max( new_point.z, last_point.z() );
            if( ( has_floor( tripoint_bub_ms{new_point.x, new_point.y, max_z}, true )
                  || !is_transparent( tripoint_bub_ms{new_point.x, new_point.y, last_point.z()} ) )
                && ( has_floor( {last_point.xy(), max_z}, true )
                     || !is_transparent( {last_point.xy(), new_point.z} ) ) ) {
                visible = false;
                return false;
            }
        }

        last_point = tripoint_bub_ms( new_point );
        return true;
    } );
    {
        std::unique_lock<std::shared_mutex> lock( *skew_vision_cache_mutex );
        skew_vision_cache[slot_idx] = {key, static_cast<char>( visible ? 1 : 0 )};
    }
    return visible;
}

int map::obstacle_coverage( const tripoint_bub_ms& loc1, const tripoint_bub_ms& loc2 ) const
{
    // Can't hide if you are standing on furniture, or non-flat slowing-down terrain tile.
    if( furn( loc2 ).obj().id || ( move_cost( loc2 ) > 2 && !has_flag_ter( TFLAG_FLAT, loc2 ) ) ) {
    return 0;
}
const point_bub_ms a( std::abs( loc1.x() - loc2.x() ) * 2, std::abs( loc1.y() - loc2.y() ) * 2 );
int offset = std::min( a.x(), a.y() ) - ( std::max( a.x(), a.y() ) / 2 );
tripoint_bub_ms obstaclepos;
bresenham( loc2.raw(), loc1.raw(), offset, 0, [&obstaclepos]( const tripoint & new_point ) {
    // Only adjacent tile between you and enemy is checked for cover.
    obstaclepos = tripoint_bub_ms( new_point );
        return false;
    } );
    if( const auto obstacle_f = furn( obstaclepos ) ) {
    return obstacle_f->coverage;
}
if( const auto vp = veh_at( obstaclepos ) ) {
    if( vp->obstacle_at_part() ) {
            return 60;
        } else if( !vp->part_with_feature( VPFLAG_AISLE, true ) ) {
            return 45;
        }
    }
    return ter( obstaclepos )->coverage;
}

int map::coverage( const tripoint_bub_ms& p ) const
{
    if( const auto obstacle_f = furn( p ) ) { return obstacle_f->coverage; }
    if( const auto vp = veh_at( p ) ) {
        if( vp->obstacle_at_part() ) {
            return 60;
        } else if( !vp->part_with_feature( VPFLAG_AISLE, true )
                   && !vp->part_with_feature( "PROTRUSION", true ) ) {
            return 45;
        }
    }
    return ter( p )->coverage;
}

// This method tries a bunch of initial offsets for the line to try and find a clear one.
// Basically it does, "Find a line from any point in the source that ends up in the target square".
std::vector<tripoint_bub_ms> map::find_clear_path(
    const tripoint_bub_ms& source, const tripoint_bub_ms& destination ) const
{
    // TODO: Push this junk down into the Bresenham method, it's already doing it.
    const point_rel_ms d = destination.xy() - source.xy();
    const point_rel_ms a( std::abs( d.x() ) * 2, std::abs( d.y() ) * 2 );
    const int dominant = std::max( a.x(), a.y() );
    const int minor = std::min( a.x(), a.y() );
    // This seems to be the method for finding the ideal start value for the error value.
    const int ideal_start_offset = minor - dominant / 2;
    const int start_sign = ( ideal_start_offset > 0 ) - ( ideal_start_offset < 0 );
    // Not totally sure of the derivation.
    const int max_start_offset = std::abs( ideal_start_offset ) * 2 + 1;
    for( int horizontal_offset = -1; horizontal_offset <= max_start_offset; ++horizontal_offset ) {
        int candidate_offset = horizontal_offset * start_sign;
        if( sees( source, destination, rl_dist( source, destination ), candidate_offset ) ) {
            return line_to( source, destination, candidate_offset, 0 );
        }
    }
    // If we couldn't find a clear LoS, just return the ideal one.
    return line_to( source, destination, ideal_start_offset, 0 );
}

auto map::ray_cast_angle( const tripoint_bub_ms &src, double angle_rad,
                          int max_range ) const -> std::vector<tripoint_bub_ms>
{
    const auto dx = std::cos( angle_rad );
    const auto dy = std::sin( angle_rad );
    const auto step_x = dx >= 0.0 ? 1 : -1;
    const auto step_y = dy >= 0.0 ? 1 : -1;
    // Start at tile centre
    const auto fx0 = src.x() + 0.5;
    const auto fy0 = src.y() + 0.5;
    const auto inf = std::numeric_limits<double>::infinity();
    const auto t_delta_x = dx != 0.0 ? std::abs( 1.0 / dx ) : inf;
    const auto t_delta_y = dy != 0.0 ? std::abs( 1.0 / dy ) : inf;
    auto t_max_x = dx != 0.0
                   ? std::abs( ( step_x > 0 ? std::ceil( fx0 ) - fx0
                                 : fx0 - std::floor( fx0 ) ) / dx )
                   : inf;
    auto t_max_y = dy != 0.0
                   ? std::abs( ( step_y > 0 ? std::ceil( fy0 ) - fy0
                                 : fy0 - std::floor( fy0 ) ) / dy )
                   : inf;
    auto tx = src.x();
    auto ty = src.y();
    auto result = std::vector<tripoint_bub_ms> {};
    result.reserve( static_cast<size_t>( max_range ) + 2 );
    while( true ) {
        if( t_max_x < t_max_y ) {
            t_max_x += t_delta_x;
            tx += step_x;
        } else {
            t_max_y += t_delta_y;
            ty += step_y;
        }
        const auto dist = std::hypot( static_cast<double>( tx - src.x() ),
                                      static_cast<double>( ty - src.y() ) );
        if( dist > static_cast<double>( max_range ) ) { break; }
        const auto tile = tripoint_bub_ms{ tx, ty, src.z() };
        if( !inbounds( tile ) ) { break; }
        result.push_back( tile );
    }
    return result;
}

void map::reachable_flood_steps( std::vector<tripoint_bub_ms> &reachable_pts,
                                 const tripoint_bub_ms &f,
                                 int range, const int cost_min, const int cost_max ) const
{
    if( range < 0 || !inbounds( f ) ) {
    return;
}

struct pq_item {
    int dist;
    int ndx;
};
struct pq_item_comp {
    bool operator()( const pq_item &left, const pq_item &right ) {
            return left.dist > right.dist;
        }
    };
    using PQ_type = std::priority_queue<pq_item, std::vector<pq_item>, pq_item_comp>;

    // temp buffer for grid
    const int grid_dim = range * 2 + 1;
    // init to -1 as "not visited yet"
    std::vector<int> t_grid( static_cast<size_t>( grid_dim * grid_dim ), -1 );
    const tripoint origin_offset = {range, range, 0};
    const int initial_visit_distance = range * range; // Large unreachable value

    // Fill positions that are visitable with initial_visit_distance
for( const tripoint_bub_ms &p : points_in_radius( f, range ) ) {
    const tripoint_bub_ms tp = { p.xy(), f.z() };
    const int tp_cost = move_cost( tp );
        // rejection conditions
        if( tp_cost < cost_min || tp_cost > cost_max || !has_floor_or_support( tp ) ) { continue; }
        // set initial cost for grid point
        auto origin_relative = tp - f;
        origin_relative += origin_offset;
        int ndx = origin_relative.x() + origin_relative.y() * grid_dim;
        if( ndx < 0 || ndx >= static_cast<int>( t_grid.size() ) ) { continue; }
        t_grid[ndx] = initial_visit_distance;
    }

    auto gen_neighbors = []( const pq_item & elem, int grid_dim, pq_item * neighbors ) {
        // Up to 8 neighbors
        int new_cost = elem.dist + 1;
        // *INDENT-OFF*
        int ox[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
        int oy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
        // *INDENT-ON*

        point e( elem.ndx % grid_dim, elem.ndx / grid_dim );
        for( int i = 0; i < 8; ++i ) {
            point n( e + point( ox[i], oy[i] ) );

            int ndx = n.x + n.y * grid_dim;
            neighbors[i] = {new_cost, ndx};
        }
    };

    PQ_type pq( pq_item_comp{} );
    pq_item first_item{0, range + range * grid_dim};
    pq.push( first_item );
    pq_item neighbor_elems[8];

    while( !pq.empty() ) {
    const pq_item item = pq.top();
        pq.pop();

        if( item.ndx < 0 || item.ndx >= static_cast<int>( t_grid.size() ) ) { continue; }
        if( t_grid[item.ndx] == initial_visit_distance ) {
            t_grid[item.ndx] = item.dist;
            if( item.dist + 1 < range ) {
                gen_neighbors( item, grid_dim, neighbor_elems );
                for( pq_item neighbor_elem : neighbor_elems ) { pq.push( neighbor_elem ); }
            }
        }
    }
    std::vector<char> o_grid( static_cast<size_t>( grid_dim * grid_dim ), 0 );
    for( int y = 0, ndx = 0; y < grid_dim; ++y ) {
    for( int x = 0; x < grid_dim; ++x, ++ndx ) {
            if( t_grid[ ndx ] != -1 && t_grid[ ndx ] < initial_visit_distance ) {
                // set self and neighbors to 1
                for( int dy = -1; dy <= 1; ++dy ) {
                    for( int dx = -1; dx <= 1; ++dx ) {
                        int tx = dx + x;
                        int ty = dy + y;

                        if( tx >= 0 && tx < grid_dim && ty >= 0 && ty < grid_dim ) {
                            o_grid[tx + ty * grid_dim] = 1;
                        }
                    }
                }
            }
        }
    }

    // Now go over again to pull out all of the reachable points
    for( int y = 0, ndx = 0; y < grid_dim; ++y ) {
    for( int x = 0; x < grid_dim; ++x, ++ndx ) {
            if( o_grid[ ndx ] ) {
                auto t = f - origin_offset + tripoint_rel_ms{ x, y, 0 };
                reachable_pts.push_back( t );
            }
        }
    }
}

bool map::clear_path(
    const tripoint_bub_ms& f, const tripoint_bub_ms& t, const int range, const int cost_min,
    const int cost_max ) const
{
    // Ugly `if` for now
    if( !fov_3d && f.z() != t.z() ) { return false; }

    if( f.z() == t.z() ) {
        if( ( range >= 0 && range < rl_dist( f.xy(), t.xy() ) ) || !inbounds( t ) ) {
            return false; // Out of range!
        }
        bool is_clear = true;
        auto last_point = f.xy();
        bresenham( f.xy().raw(), t.xy().raw(), 0,
        [this, &is_clear, cost_min, cost_max, &t, &last_point]( point new_point ) {
            // Exit before checking the last square, it's still reachable even if it is an
            // obstacle.
            if( new_point.x == t.x() && new_point.y == t.y() ) { return false; }

            const int cost = this->move_cost( point_bub_ms( new_point ) );
            if( cost < cost_min || cost > cost_max
                || obstructed_by_vehicle_rotation(
                    tripoint_bub_ms( last_point, t.z() ),
                    tripoint_bub_ms( point_bub_ms( new_point ), t.z() ) ) ) {
                is_clear = false;
                return false;
            }

            last_point = point_bub_ms( new_point );
            return true;
        } );
        return is_clear;
    }

    if( ( range >= 0 && range < rl_dist( f, t ) ) || !inbounds( t ) ) {
        return false; // Out of range!
    }
    bool is_clear = true;
    auto last_point = f;
    bresenham(
        f.raw(), t.raw(), 0, 0,
    [this, &is_clear, cost_min, cost_max, t, &last_point]( const tripoint & new_point ) {
        // Exit before checking the last square, it's still reachable even if it is an obstacle.
        if( new_point == t.raw() ) { return false; }

        // We have to check a weird case where the move is both vertical and horizontal
        if( new_point.z == last_point.z() ) {
            const int cost = move_cost( tripoint_bub_ms( new_point ) );
            if( cost < cost_min || cost > cost_max
                || obstructed_by_vehicle_rotation( last_point, tripoint_bub_ms( new_point ) ) ) {
                is_clear = false;
                return false;
            }
        } else {
            bool this_clear = false;
            const int max_z = std::max( new_point.z, last_point.z() );
            if( !has_floor_or_support( tripoint_bub_ms{new_point.x, new_point.y, max_z} ) ) {
                const int cost = move_cost(
                                     tripoint_bub_ms{new_point.x, new_point.y, last_point.z()} );
                if( cost > cost_min && cost < cost_max
                    && !obstructed_by_vehicle_rotation( last_point, tripoint_bub_ms( new_point ) ) ) {
                    this_clear = true;
                }
            }

            if( !this_clear && has_floor_or_support( {last_point.xy(), max_z} ) ) {
                const int cost = move_cost( tripoint_bub_ms{last_point.xy(), new_point.z} );
                if( cost > cost_min && cost < cost_max
                    && !obstructed_by_vehicle_rotation( last_point, tripoint_bub_ms( new_point ) ) ) {
                    this_clear = true;
                }
            }

            if( !this_clear ) {
                is_clear = false;
                return false;
            }
        }

        last_point = tripoint_bub_ms( new_point );
        return true;
    } );
    return is_clear;
}

bool map::obstructed_by_vehicle_rotation( const tripoint_bub_ms &from,
        const tripoint_bub_ms &to ) const
{
    if( !inbounds( from ) || !inbounds( to ) ) {
    return false;
}

if( from.z() != to.z() ) {
    //Split it into two checks, one for each z level
    tripoint_bub_ms flattened = { from.xy(), to.z() };
    if( obstructed_by_vehicle_rotation( flattened, to ) ) {
            return true;
        }
    }

    auto delta = to.xy() - from.xy();

    const level_cache& lc = get_cache_ref( from.z() );
    const auto& cache = lc.vehicle_obstructed_cache;

    if( delta == point_rel_ms::north_west() ) {
    return cache[lc.idx( from.x(), from.y() )].nw;
    }

    if( delta == point_rel_ms::north_east() ) {
    return cache[lc.idx( from.x(), from.y() )].ne;
    }

    if( delta == point_rel_ms::south_west() ) {
    return cache[lc.idx( to.x(), to.y() )].ne;
    }
    if( delta == point_rel_ms::south_east() ) {
    return cache[lc.idx( to.x(), to.y() )].nw;
    }

    return false;
}


bool map::obscured_by_vehicle_rotation( const tripoint_bub_ms &from,
                                        const tripoint_bub_ms &to ) const
{
    if( !inbounds( from ) || !inbounds( to ) ) {
    return false;
}

if( from.z() != to.z() ) {
    //Split it into two checks, one for each z level
    tripoint_bub_ms flattened = { from.xy(), to.z() };
    if( obscured_by_vehicle_rotation( flattened, to ) ) {
            return true;
        }
    }

    auto delta = to.xy() - from.xy();

    const level_cache& lc = get_cache_ref( from.z() );
    const auto& cache = lc.vehicle_obscured_cache;

    if( delta == point_rel_ms::north_west() ) {
    return cache[lc.idx( from.x(), from.y() )].nw;
    }

    if( delta == point_rel_ms::north_east() ) {
    return cache[lc.idx( from.x(), from.y() )].ne;
    }

    if( delta == point_rel_ms::south_west() ) {
    return cache[lc.idx( to.x(), to.y() )].ne;
    }
    if( delta == point_rel_ms::south_east() ) {
    return cache[lc.idx( to.x(), to.y() )].nw;
    }

    return false;
}

bool map::accessible_items( const tripoint_bub_ms& t ) const
{
    return !has_flag( "SEALED", t ) || has_flag( "LIQUIDCONT", t );
}

std::vector<tripoint_bub_ms> map::get_dir_circle(
    const tripoint_bub_ms& f, const tripoint_bub_ms& t ) const
{
    std::vector<tripoint_bub_ms> circle;
    circle.resize( 8 );

    // The line below can be crazy expensive - we only take the FIRST point of it
    const std::vector<tripoint_bub_ms> line = line_to( f, t, 0, 0 );
    const std::vector<tripoint_bub_ms> spiral = closest_points_first( f, 1 );
    const std::vector<int> pos_index{1, 2, 4, 6, 8, 7, 5, 3};

    //  All possible constellations (closest_points_first goes clockwise)
    //  753  531  312  124  246  468  687  875
    //  8 1  7 2  5 4  3 6  1 8  2 7  4 5  6 3
    //  642  864  786  578  357  135  213  421

    size_t pos_offset = 0;
    for( size_t i = 1; i < spiral.size(); i++ ) {
        if( spiral[i] == line[0] ) {
            pos_offset = i - 1;
            break;
        }
    }

    for( size_t i = 1; i < spiral.size(); i++ ) {
        if( pos_offset >= pos_index.size() ) { pos_offset = 0; }

        circle[pos_index[pos_offset++] - 1] = spiral[i];
    }

    return circle;
}

void map::load( const tripoint_abs_sm& w, const bool update_vehicle, const bool pump_events )
{
    std::fill( grid.begin(), grid.end(), nullptr );
    submaps_with_active_items.clear();
    loaded_vehicles.clear();
    funnel_locations_.clear();
    set_abs_sub( w );
    for( const auto p : bubble_submaps() ) {
        loadn( p, update_vehicle );
        if( pump_events ) { inp_mngr.pump_events(); }
    }
    reset_vehicle_cache();
}


// Shift a flat tile-coordinate cache array (x-major layout: vec[x * stride_y + y])
// by `s` submaps.  seex/seey give the tile count per submap in each direction.
// New edge positions retain stale values — the caller must mark those submaps
// dirty so the next build_*_cache() pass overwrites them.
template <typename T>
static void shift_flat_cache(
    std::vector<T> &cache, int cache_x, int cache_y, const point_rel_sm& s )
{
    T* data = cache.data();
    // X shift: each x-column is a contiguous block of cache_y elements.
    if( s.x() > 0 ) {
        std::memmove( data, data + SEEX * cache_y,
                      static_cast<size_t>( cache_x - SEEX ) * cache_y * sizeof( T ) );
    } else if( s.x() < 0 ) {
        std::memmove( data + SEEX * cache_y, data,
                      static_cast<size_t>( cache_x - SEEX ) * cache_y * sizeof( T ) );
    }
    // Y shift: move within each x-column.
    if( s.y() > 0 ) {
        for( int x = 0; x < cache_x; ++x ) {
            T* col = data + x * cache_y;
            std::memmove( col, col + SEEY, static_cast<size_t>( cache_y - SEEY ) * sizeof( T ) );
        }
    } else if( s.y() < 0 ) {
        for( int x = 0; x < cache_x; ++x ) {
            T* col = data + x * cache_y;
            std::memmove( col + SEEY, col, static_cast<size_t>( cache_y - SEEY ) * sizeof( T ) );
        }
    }
}

void shift_bitset_cache(
    cata_dynamic_bitset& cache, int size, int multiplier, const point_rel_sm& s )
{
    // sx shifts by MULTIPLIER rows, sy shifts by MULTIPLIER columns.
    int shift_amount = s.x() * multiplier + s.y() * size * multiplier;
    if( shift_amount > 0 ) {
        cache >>= static_cast<size_t>( shift_amount );
    } else if( shift_amount < 0 ) {
        cache <<= static_cast<size_t>( -shift_amount );
    }
    // Shifting in the y direction shifts in 0 values, so no additional clearing is necessary, but
    // a shift in the x direction makes values "wrap" to the next row, and they need to be zeroed.
    if( s.x() == 0 ) { return; }
    const size_t x_offset = s.x() > 0 ? static_cast<size_t>( size - multiplier ) : 0;
    for( size_t y = 0; y < static_cast<size_t>( size ); ++y ) {
        size_t y_offset = y * static_cast<size_t>( size );
        for( size_t x = 0; x < static_cast<size_t>( multiplier ); ++x ) {
            cache.reset( y_offset + x_offset + x );
        }
    }
}

static inline void shift_tripoint_set(
    std::set<tripoint_bub_ms> &set, const point_rel_ms& offset,
    const half_open_rectangle<point_bub_ms> &boundaries )
{
    std::set<tripoint_bub_ms> old_set = std::move( set );
    set.clear();
    for( const tripoint_bub_ms& pt : old_set ) {
        tripoint_bub_ms new_pt = pt + offset;
        if( boundaries.contains( new_pt.xy() ) ) { set.insert( new_pt ); }
    }
}

template <typename T>
static inline void shift_tripoint_map(
    std::map<tripoint_bub_ms, T> &map, const point_rel_ms& offset,
    const half_open_rectangle<point_bub_ms> &boundaries )
{
    std::map<tripoint_bub_ms, T> old_map = std::move( map );
    map.clear();
    for( const std::pair<tripoint_bub_ms, T> &pr : old_map ) {
        tripoint_bub_ms new_pt = pr.first + offset;
        if( boundaries.contains( new_pt.xy() ) ) { map.emplace( new_pt, pr.second ); }
    }
}

void map::shift_vehicle_z( vehicle& veh, int z_shift )
{
    auto src = veh.abs_sm_pos;
    auto dst = src + tripoint_rel_sm( 0, 0, z_shift );
    invalidate_lightmap_caches();
    auto dirty_vertical_vehicle_caches = [this]( const int zlev ) {
        if( !inbounds_z( zlev ) ) { return; }
        invalidate_map_cache( zlev );
    };
    dirty_vertical_vehicle_caches( src.z() );
    dirty_vertical_vehicle_caches( src.z() + 1 );
    dirty_vertical_vehicle_caches( dst.z() );
    dirty_vertical_vehicle_caches( dst.z() + 1 );

    submap* src_submap = MAPBUFFER_REGISTRY.get( bound_dimension_ ).lookup_submap_in_memory( src );
    submap* dst_submap = MAPBUFFER_REGISTRY.get( bound_dimension_ ).lookup_submap_in_memory( dst );

    int our_i = -1;
    for( size_t i = 0; i < src_submap->vehicles.size(); i++ ) {
        if( src_submap->vehicles[i].get() == &veh ) {
            our_i = i;
            break;
        }
    }

    if( our_i == -1 ) {
        debugmsg( "shift_vehicle [%s] failed could not find vehicle", veh.name );
        return;
    }

    for( auto& prt : veh.get_all_parts() ) {
        prt.part().z_terrain[0] -= z_shift;
        prt.part().z_terrain[1] -= z_shift;
    }

    auto src_submap_veh_it = src_submap->vehicles.begin() + our_i;
    dst_submap->vehicles.push_back( std::move( *src_submap_veh_it ) );
    src_submap->vehicles.erase( src_submap_veh_it );
    dst_submap->is_uniform = false;
    invalidate_max_populated_zlev( dst.z() );

    update_vehicle_list( dst_submap, dst.z() );

    level_cache& ch = get_cache( src.z() );
    for( const vehicle * elem : ch.vehicle_list ) {
        if( elem == &veh ) {
            ch.vehicle_list.erase( &veh );
            ch.zone_vehicles.erase( &veh );
            break;
        }
    }

    veh.abs_sm_pos = dst;
    veh.update_overmap( src );
}

void map::shift( const point_rel_sm& sp )
{
    ZoneScopedN( "map_shift" );
    // Special case of 0-shift; refresh the map
    if( sp == point_rel_sm::zero() ) {
        return; // Skip this?
    }

    if( std::abs( sp.x() ) > 1 || std::abs( sp.y() ) > 1 ) {
        debugmsg( "map::shift called with a shift of more than one submap" );
    }

    const tripoint_abs_sm abs = get_abs_sub();

    set_abs_sub( abs + sp );

    g->shift_destination_preview( -project_to<coords::ms>( sp ) );

    vehicle* remoteveh = g->remoteveh();

    const int zmin = zlevels ? -OVERMAP_DEPTH : abs.z();
    const int zmax = zlevels ? OVERMAP_HEIGHT : abs.z();
    for( const auto gridz : std::views::iota( zmin, zmax + 1 ) ) {
        for( auto * veh : get_cache( gridz ).vehicle_list ) { veh->zones_dirty = true; }
    }

    const half_open_rectangle<point_bub_ms>
    boundaries_2d( point_bub_ms::zero(), point_bub_ms( g_mapsize_x, g_mapsize_y ) );
    const point_rel_ms shift_offset_pt( -sp.x() * SEEX, -sp.y() * SEEY );


    // Run any Lua on_mapgen_postprocess hooks that were deferred from worker
    // threads (Lua is not thread-safe).  The submaps are already in the
    // mapbuffer; run_deferred_mapgen_hooks() loads them into temporary tinymaps
    // and executes each hook on the main thread.
    {
        ZoneScopedN( "shift_mapgen_hooks" );
        run_deferred_mapgen_hooks();
        run_deferred_autonotes();
    }

    // Clear vehicle list and rebuild after shift
    {
        ZoneScopedN( "shift_clear_vehicle_cache" );
        clear_vehicle_cache();
    }
    // Shift the map sx submaps to the right and sy submaps down.
    // sx and sy should never be bigger than +/-1.
    // absx and absy are our position in the world, for saving/loading purposes.
    {
        ZoneScopedN( "shift_grid_copy_load" );
        for( const auto gridz : std::views::iota( zmin, zmax + 1 ) ) {
            clear_vehicle_list( gridz );
            {
                ZoneScopedN( "shift_cache_arrays" );
                level_cache& gc = get_cache( gridz );
                shift_bitset_cache( gc.map_memory_seen_cache, gc.cache_x, SEEX, sp );
                // Shift per-submap dirty bitsets so retained submaps stay clean.
                shift_bitset_cache( gc.transparency_cache_dirty, gc.cache_mapsize, 1, sp );
                shift_bitset_cache( gc.floor_cache_dirty, gc.cache_mapsize, 1, sp );
                shift_bitset_cache( gc.outside_cache_dirty, gc.cache_mapsize, 1, sp );
                shift_bitset_cache( gc.lightmap_dirty, gc.cache_mapsize, 1, sp );
                // Shift flat cache data so retained submaps' data stays in the
                // correct tile position.  New edge submaps get stale values that
                // will be overwritten by the next build_*_cache() call.
                shift_flat_cache( gc.transparency_cache, gc.cache_x, gc.cache_y, sp );
                shift_flat_cache( gc.floor_cache, gc.cache_x, gc.cache_y, sp );
                shift_flat_cache( gc.outside_cache, gc.cache_x, gc.cache_y, sp );
                shift_flat_cache( gc.sheltered_cache, gc.cache_x, gc.cache_y, sp );
                // Translate the lightmap so a non-player z that is rendered
                // (visible lower z through open air / holes / ledges, or the
                // whole stack under fov_3d) stays at the correct world position.
                // Non-player-z lightmaps are no longer regenerated on a pure
                // horizontal shift (see set_seen_cache_dirty gate below), so the
                // translate is what keeps them visually correct; the player z is
                // rebuilt fresh anyway.  Light does not propagate lm->lm across z
                // (cross-z coupling is structural, via floor/outside, which is
                // rebuilt above), so a translated stale lm cannot corrupt player z.
                shift_flat_cache( gc.lm, gc.cache_x, gc.cache_y, sp );
                shift_flat_cache( gc.sm, gc.cache_x, gc.cache_y, sp );
                if( fov_3d_occlusion ) {
                    shift_flat_cache( gc.angled_sunlight_cache, gc.cache_x, gc.cache_y, sp );
                }
            }
            // Iterate in shift-direction order so copy_grid never reads an
            // already-overwritten source slot.  sp >= 0 → forward; sp < 0 → reverse.
            const auto for_grid_x = [&]( auto callback ) {
                if( sp.x() >= 0 ) {
                    std::ranges::for_each( std::views::iota( 0, my_MAPSIZE ), callback );
                } else {
                    std::ranges::
                    for_each( std::views::iota( 0, my_MAPSIZE ) | std::views::reverse, callback );
                }
            };
            const auto for_grid_y = [&]( auto callback ) {
                if( sp.y() >= 0 ) {
                    std::ranges::for_each( std::views::iota( 0, my_MAPSIZE ), callback );
                } else {
                    std::ranges::
                    for_each( std::views::iota( 0, my_MAPSIZE ) | std::views::reverse, callback );
                }
            };
            {
                ZoneScopedN( "shift_grid_slots" );
                ZoneValue( static_cast<uint64_t>( gridz + OVERMAP_DEPTH ) );
                for_grid_x( [&]( int gridx ) {
                    for_grid_y( [&]( int gridy ) {
                        // Erase tracking for old occupants that are leaving the bubble.
                        // An occupant leaves when its post-shift slot (gridx - sp.x(),
                        // gridy - sp.y) falls outside the grid.
                        if( ( sp.x() > 0 && gridx == 0 ) || ( sp.x() < 0 && gridx == my_MAPSIZE - 1 )
                            || ( sp.y() > 0 && gridy == 0 )
                            || ( sp.y() < 0 && gridy == my_MAPSIZE - 1 ) ) {
                            submaps_with_active_items.erase(
                            {abs.x() + gridx, abs.y() + gridy, gridz} );
                        }
                        if( gridx + sp.x() >= 0 && gridx + sp.x() < my_MAPSIZE
                            && gridy + sp.y() >= 0 && gridy + sp.y() < my_MAPSIZE ) {
                            copy_grid( tripoint_bub_sm( gridx, gridy, gridz ),
                                       tripoint_bub_sm( gridx + sp.x(), gridy + sp.y(), gridz ) );
                            update_vehicle_list(
                                get_submap_at_grid( tripoint_bub_sm{gridx, gridy, gridz} ), gridz );
                        } else {
                            loadn( tripoint_bub_sm( gridx, gridy, gridz ), true, true );
                        }
                    } );
                } );
            }
            // outside_cache/sheltered_cache were translated above and their dirty
            // bitset shifted; new edge submaps are marked dirty in loadn's
            // incremental block, so no blanket all-z rebuild is needed here.
            //
            // seen_cache/lightmap: dirtying a z here drives a full generate_lightmap
            // for that level (the costly all-z work, ~7-10ms).  A pure horizontal
            // shift does not change a level's lighting relative to its own world
            // tiles, and lm/sm were translated above to stay visually correct where
            // a non-player z is rendered.  Light does not propagate lm->lm across z
            // (coupling is structural, via floor/outside, rebuilt above), so a
            // translated stale lm cannot corrupt another level.  So regenerate only
            // the player's z, plus the immediately-adjacent z under fov_3d (the
            // levels most likely glimpsed through a hole/ledge while crossing).
            // Deeper visible levels (z±2..fov_3d_z_range) rely on the translate;
            // they are rarely viewed and mostly static.  FOV_3D defaults on, so
            // this must NOT fall back to all-z.
            const int player_z = g->u.bub_pos().z();
            if( gridz == player_z || ( fov_3d && std::abs( gridz - player_z ) <= 1 ) ) {
                set_seen_cache_dirty( gridz );
            }
            set_pathfinding_cache_dirty( gridz );
            set_suspension_cache_dirty( gridz );
        }
    } // shift_grid_copy_load
    // New edge submaps have stale solar cache data. Force a rebuild before the next draw.
    m_solar.last_built_hour = -1;
    if( zlevels ) {
        ZoneScopedN( "shift_add_roofs" );
        // Go through the generated maps and fill in the roofs
        for( const auto gridz : std::views::iota( zmin, zmax + 1 ) ) {
            const auto axis = std::views::iota( 0, my_MAPSIZE );
            if( sp.x() > 0 ) {
                for( const auto gridy : axis ) { add_roofs( {my_MAPSIZE - 1, gridy, gridz} ); }
            } else if( sp.x() < 0 ) {
                for( const auto gridy : axis ) { add_roofs( {0, gridy, gridz} ); }
            }

            if( sp.y() > 0 ) {
                for( const auto gridx : axis ) { add_roofs( {gridx, my_MAPSIZE - 1, gridz} ); }
            } else if( sp.y() < 0 ) {
                for( const auto gridx : axis ) { add_roofs( {gridx, 0, gridz} ); }
            }
        }
    }

    {
        ZoneScopedN( "shift_reset_vehicle_cache" );
        reset_vehicle_cache();
    }

    g->setremoteveh( remoteveh );

    if( !support_cache_dirty.empty() ) {
        shift_tripoint_set( support_cache_dirty, shift_offset_pt, boundaries_2d );
    }

    // Lightmap was translated via shift_flat_cache above, and the per-submap
    // lightmap_dirty bitset was shifted via shift_bitset_cache.  Only new-edge
    // submaps are marked dirty by loadn(incremental=true).  No blanket
    // invalidate_lightmap_caches() needed — retained submaps stay clean.
    // Entity lights are applied unconditionally in build_map_cache Phase 4.
#ifdef BOX2D_ENABLED
    if( phys_world ) { phys_world->on_map_shifted( point { shift_offset_pt.x(), shift_offset_pt.y() } ); }
#endif
}

auto map::apply_boundary_overlay( submap &sm, const tripoint_abs_sm &pos ) -> void
{
    if( !pocket_info_ ) {
    return;
}
const bool on_min_x = pos.x() == pocket_info_->bounds.min_bound.x();
const bool on_max_x = pos.x() == pocket_info_->bounds.max_bound.x();
const bool on_min_y = pos.y() == pocket_info_->bounds.min_bound.y();
const bool on_max_y = pos.y() == pocket_info_->bounds.max_bound.y();
if( !on_min_x && !on_max_x && !on_min_y && !on_max_y ) {
    return;
}
const auto border = get_boundary_terrain();
std::ranges::for_each(
    cata::views::cartesian_product( std::views::iota( 0, SEEX ), std::views::iota( 0, SEEY ) )
    | std::views::filter( [&]( const auto & tile ) {
        const auto [x, y] = tile;
        return ( on_min_y && y == 0 ) ||
               ( on_max_y && y == SEEY - 1 ) ||
               ( on_min_x && x == 0 ) ||
               ( on_max_x && x == SEEX - 1 );
    } ),
    [&]( const auto & tile ) {
        const auto [x, y] = tile;
        sm.set_ter( { x, y }, border );
    }
    );
}

void map::loadn( const tripoint_bub_sm& grid, const bool update_vehicles, const bool incremental )
{
    ZoneScopedN( "map_loadn" );
    ZoneValue( static_cast<uint64_t>( grid.z() + OVERMAP_DEPTH ) );

    const auto grid_abs_sub = bub_to_abs( tripoint_bub_sm( grid ) );
    const size_t gridn = get_nonant( tripoint_bub_sm( grid ) );

    // For out-of-bounds areas in bounded dimensions, use uniform boundary terrain
    // submaps instead of nullptr.  We check in-memory only (no DB lookup) because
    // most pocket-dimension submaps are out-of-bounds.
    if( pocket_info_ && !pocket_info_->bounds.contains( tripoint_abs_sm( grid_abs_sub ) ) ) {
        mapbuffer& dim_buf = MAPBUFFER_REGISTRY.get( bound_dimension_ );
        submap* bsub = dim_buf.lookup_submap_in_memory( grid_abs_sub );
        // Diagnostic: log boundary submap creation for dimension debugging
        if( bsub == nullptr ) {
            add_msg( m_debug, "[DIM-DIAG] loadn: creating boundary submap at (%d,%d,%d)",
                     grid_abs_sub.x(), grid_abs_sub.y(), grid_abs_sub.z() );
            auto sm = std::make_unique<submap>( grid_abs_sub );
            sm->is_uniform = true;
            sm->set_all_ter( get_boundary_terrain() );
            sm->last_touched = calendar::turn;
            dim_buf.add_submap( grid_abs_sub, sm );
            bsub = dim_buf.lookup_submap_in_memory( grid_abs_sub );
        }
        if( bsub != nullptr ) { setsubmap( gridn, bsub ); }
        return;
    }

    const int old_abs_z = abs_sub.z(); // Ugly, but necessary at the moment
    abs_sub.z() = grid.z();

    // Use the dimension-specific mapbuffer slot so each dimension's submaps
    // live independently and on_submap_loaded() finds them in the correct registry.
    submap* tmpsub = nullptr;
    {
        ZoneScopedN( "loadn_lookup" );
        tmpsub = MAPBUFFER_REGISTRY.get( bound_dimension_ ).lookup_submap( grid_abs_sub );
    }
    // Diagnostic: log in-bounds submap loading for dimension transition debugging
    if( pocket_info_ ) {
        add_msg( m_debug, "[DIM-DIAG] loadn: in-bounds submap at (%d,%d,%d) %s", grid_abs_sub.x(),
                 grid_abs_sub.y(), grid_abs_sub.z(), tmpsub ? "found" : "MISSING - will generate" );
    }
    if( tmpsub == nullptr ) {
        ZoneScopedN( "loadn_generate" );
        // It doesn't exist; we must generate it!
        dbg( DL::Info ) << "map::loadn: Missing mapbuffer data.  Regenerating.";

        // Each overmap square is two nonants; to prevent overlap, generate only at
        //  squares divisible by 2.
        // TODO: fix point types
        const auto grid_abs_omt = tripoint_abs_omt( project_to<coords::omt>( grid_abs_sub ) );
        auto& dim_buf = MAPBUFFER_REGISTRY.get( bound_dimension_ );
        dim_buf.generate_omt( grid_abs_omt );

        {
            ZoneScopedN( "loadn_generate_lookup" );
            tmpsub = MAPBUFFER_REGISTRY.get( bound_dimension_ ).lookup_submap( grid_abs_sub );
        }
        if( tmpsub == nullptr ) {
            debugmsg( "failed to generate a submap at %s", grid_abs_sub.to_string() );
            return;
        }
    }

    // New submap changes the content of the map and all caches must be recalculated.
    // In incremental mode (shift context), transparent and floor caches and
    // lightmap were shifted in shift() — only mark this specific submap dirty.
    // Avoid repeating full-level work here for every newly loaded edge submap.
    {
        ZoneScopedN( "loadn_dirty" );
        if( incremental ) {
            level_cache& ch = get_cache( grid.z() );
            const size_t bidx = static_cast<size_t>( ch.bidx( grid.x(), grid.y() ) );
            ch.transparency_cache_dirty.set( bidx );
            ch.floor_cache_dirty.set( bidx );
            ch.outside_cache_dirty.set( bidx );
            ch.lightmap_dirty.set( bidx );
            tmpsub->transparency_dirty = true;
            tmpsub->floor_dirty = true;
            tmpsub->outside_dirty = true;
            tmpsub->pf_dirty = true;
        } else {
            set_transparency_cache_dirty( grid.z() );
            set_floor_cache_dirty( grid.z() );
            set_outside_cache_dirty( grid.z() );
            set_seen_cache_dirty( grid.z() );
            set_pathfinding_cache_dirty( grid.z() );
            set_suspension_cache_dirty( grid.z() );
            get_cache( grid.z() ).lightmap_dirty.set();
        }
    }
    setsubmap( gridn, tmpsub );
    // Overlay boundary terrain on the edge tiles of this submap if it sits at the
    // edge of a bounded dimension.  Must run before actualize() so actualize() sees
    // the correct terrain.  The underlying saved/generated submap data is not modified.
    if( pocket_info_ ) { apply_boundary_overlay( *tmpsub, tripoint_abs_sm( grid_abs_sub ) ); }
    if( !tmpsub->active_items.empty() ) { submaps_with_active_items.emplace( grid_abs_sub.raw() ); }
    // field_cache removed — field_count is queried directly on each submap
    // Destroy bugged no-part vehicles
    {
        ZoneScopedN( "loadn_vehicles" );
        auto& veh_vec = tmpsub->vehicles;
        for( auto iter = veh_vec.begin(); iter != veh_vec.end(); ) {
            vehicle* veh = iter->get();
            if( veh->part_count() > 0 ) {
                // Always fix submap coordinates for easier Z-level-related operations
                veh->abs_sm_pos = grid_abs_sub;
                veh->dimension_id_ = bound_dimension_;
                loaded_vehicles.insert( veh );
                veh->attach();
                iter++;
            } else {
                reset_vehicle_cache();
                if( veh->tracking_on ) { get_overmapbuffer( bound_dimension_ ).remove_vehicle( veh ); }
                dirty_vehicle_list.erase( veh );
                iter = veh_vec.erase( iter );
            }
        }

        // Update vehicle data
        if( update_vehicles ) {
            auto& map_cache = get_cache( grid.z() );
            for( const auto& veh : tmpsub->vehicles ) {
                // Only add if not tracking already.
                if( !map_cache.vehicle_list.contains( veh.get() ) ) {
                    map_cache.vehicle_list.insert( veh.get() );
                    if( !veh->loot_zones.empty() ) { map_cache.zone_vehicles.insert( veh.get() ); }
                    add_vehicle_to_cache( veh.get() );
                }
            }
        }
    }

    if( tmpsub->last_touched == calendar::turn ) {
        ZoneScopedN( "loadn_skip_current_turn_actualize" );
    } else {
        // Batch-advance field decay, item timers, and vehicle power for any
        // turns this submap missed while outside the reality bubble.
        // Runs BEFORE actualize(); the two passes target disjoint effects.
        if( tmpsub->last_touched < calendar::turn ) {
            ZoneScopedN( "loadn_batch_turns" );
            const int missed = to_turns<int>( calendar::turn - tmpsub->last_touched );
            run_submap_batch_turns( *tmpsub, missed );
            tmpsub->last_touched = calendar::turn;
        }

        {
            ZoneScopedN( "loadn_actualize" );
            actualize( grid );
        }
    }

    abs_sub.z() = old_abs_z;
}

template <typename Container>
void map::remove_rotten_items(
    Container& items, const tripoint_bub_ms& pnt, temperature_flag temperature )
{
    std::vector<item *> corpses_handle;
    items.remove_with( [this, &pnt, &temperature, &corpses_handle]( detached_ptr<item>&& it ) {
        // Validate item pointer before access to catch use-after-free corruption
        // from stale pointers surviving dimension transitions.
        if( !it ) {
            debugmsg( "remove_rotten_items: null item pointer at %s", pnt.to_string() );
            return std::move( it );
        }
        if( !it->type ) {
            debugmsg( "remove_rotten_items: item with null type at %s", pnt.to_string() );
            return std::move( it );
        }
        item& obj = *it;
        it = item::actualize_rot( std::move( it ), pnt, temperature, get_weather() );
        // When !it, that means the item was removed from the world, ie: has rotted.
        if( !it ) {
            if( obj.is_comestible() ) {
                rotten_item_spawn( obj, pnt );
            } else if( obj.is_corpse() ) {
                // Corpses cannot be handled at this stage, as it causes memory errors by adding
                // items to the Container that haven't been accounted for.
                corpses_handle.push_back( &obj );
            }
        }
        return std::move( it );
    } );

    // Now that all the removing is done, add items from corpse spawns.
    for( const item * corpse : corpses_handle ) { handle_decayed_corpse( *corpse, pnt ); }
}

void map::handle_decayed_corpse( const item& it, const tripoint_bub_ms& pnt )
{
    const mtype* dead_monster = it.get_corpse_mon();
    if( !dead_monster ) {
        debugmsg( "Corpse at tripoint %s has no associated monster?!", bub_to_abs( pnt ).to_string() );
        return;
    }

    int decayed_weight_grams = to_gram( dead_monster->weight ); // corpse might have stuff in it!
    decayed_weight_grams *= rng_float( 0.5, 0.9 );

    for( const harvest_entry& entry : dead_monster->harvest.obj() ) {
        if( entry.type != "bionic" && entry.type != "bionic_group" && entry.type != "blood" ) {
            detached_ptr<item> harvest = item::spawn( entry.drop, it.birthday() );
            const float random_decay_modifier = rng_float( 0.0f, static_cast<float>( MAX_SKILL ) );
            const float min_num =
                entry.scale_num.first * random_decay_modifier + entry.base_num.first;
            const float max_num =
                entry.scale_num.second * random_decay_modifier + entry.base_num.second;
            int roll = 0;
            if( entry.mass_ratio != 0.00f ) {
                roll = static_cast<int>( std::round( entry.mass_ratio * decayed_weight_grams ) );
                roll = std::ceil( static_cast<double>( roll ) / to_gram( harvest->weight() ) );
            } else {
                roll = std::min<int>( entry.max, std::round( rng_float( min_num, max_num ) ) );
            }
            for( int i = 0; i < roll; i++ ) {
                // This sanity-checks harvest yields that have a default stack amount, e.g. copper
                // wire from cyborgs
                if( harvest->charges > 1 ) { harvest->charges = 1; }
                if( !harvest->rotten() ) { add_item_or_charges( pnt, item::spawn( *harvest ) ); }
            }
        }
    }
    for( item * const& comp : it.get_components() ) {
        if( comp->is_bionic() ) {
            // If CBM, decay successfully at same minimum as dissection
            // otherwise, yield burnt-out bionic and remove non-sterile if present
            if( !one_in( 10 ) ) {
                comp->convert( itype_burnt_out_bionic );
                if( comp->has_fault( fault_bionic_nonsterile ) ) {
                    comp->faults.erase( fault_bionic_nonsterile );
                }
            }
            add_item_or_charges( pnt, item::spawn( *comp ) );
        } else {
            // Same odds to spawn at all, clearing non-sterile if needed
            if( one_in( 10 ) ) {
                if( comp->has_fault( fault_bionic_nonsterile ) ) {
                    comp->faults.erase( fault_bionic_nonsterile );
                }
                add_item_or_charges( pnt, item::spawn( *comp ) );
            }
        }
    }
}

void map::rotten_item_spawn( const item& item, const tripoint_bub_ms& pnt )
{
    if( g->critter_at( pnt ) != nullptr ) { return; }
    const auto& comest = item.get_comestible();
    mongroup_id mgroup = comest->rot_spawn;
    if( !mgroup ) { return; }
    const int chance = static_cast<int>(
                           comest->rot_spawn_chance * get_option<float>( "CARRION_SPAWNRATE" ) );
    if( rng( 0, 100 ) < chance ) {
        MonsterGroupResult spawn_details = MonsterGroupManager::GetResultFromGroup( mgroup );
        const spawn_disposition disposition =
            item.has_own_flag( flag_SPAWN_FRIENDLY )
            ? spawn_disposition::SpawnDisp_Pet
            : spawn_disposition::SpawnDisp_Default;
        add_spawn( spawn_details.name, 1, pnt, disposition );
        if( g->u.sees( pnt ) ) {
            if( item.is_seed() ) {
                add_msg( m_warning, _( "Something has crawled out of the %s plants!" ),
                         item.get_plant_name() );
            } else {
                add_msg( m_warning, _( "Something has crawled out of the %s!" ), item.tname() );
            }
        }
    }
}

void map::fill_funnels( const tripoint_bub_ms& p, const time_point& since )
{
    const auto& tr = tr_at( p );
    if( !tr.is_funnel() ) { return; }
    if( !is_outside( p ) ) { return; }
    auto items = i_at( p );
    units::volume maxvolume = 0_ml;
    auto biggest_container = items.end();
    for( auto candidate = items.begin(); candidate != items.end(); ++candidate ) {
        if( ( *candidate )->is_funnel_container( maxvolume ) ) { biggest_container = candidate; }
    }
    if( biggest_container != items.end() ) {
        retroactively_fill_from_funnel(
            **biggest_container, tr, since, calendar::turn, bub_to_abs( p ) );
    }
}

void map::grow_plant( const tripoint_bub_ms& p )
{
    const auto& furn = this->furn( p ).obj();
    if( !furn.has_flag( "PLANT" ) ) { return; }
    // Can't use item_stack::only_item() since there might be fertilizer
    map_stack items = i_at( p );
    map_stack::iterator seed_it = std::ranges::find_if( items, []( const item * const & it ) {
        return it->is_seed();
    } );

    if( seed_it == items.end() ) {
        // No seed there anymore, we don't know what kind of plant it was.
        // TODO: Fix point types
        const oter_id ot =
            get_overmapbuffer( bound_dimension_ )
            .ter( project_to<coords::omt>( tripoint_abs_ms( bub_to_abs( p ) ) ) );
        dbg( DL::Error ) << "a planted item at " << p << " (within overmap terrain " << ot.id().str()
                         << ") has no seed data";
        i_clear( p );
        furn_set( p, f_null );
        return;
    }

    item* seed = *seed_it;
    seed_it = map_stack::iterator();
    const time_duration plantEpoch = seed->get_plant_epoch();
    if( seed->age() >= plantEpoch * furn.plant->growth_multiplier
        && !furn.has_flag( "GROWTH_HARVEST" ) ) {
        if( seed->age() < plantEpoch * 2 ) {
            if( has_flag_furn( "GROWTH_SEEDLING", p ) ) { return; }

            // Remove fertilizer if any
            map_stack::iterator fertilizer = std::ranges::find_if( items, []( const item * const & it ) {
                return it->has_flag( flag_FERTILIZER );
            } );
            if( fertilizer != items.end() ) { items.erase( fertilizer ); }
            fertilizer = map_stack::iterator();
            rotten_item_spawn( *seed, p );
            furn_set( p, furn_str_id( furn.plant->transform ) );
        } else if( seed->age() < plantEpoch * 3 * furn.plant->growth_multiplier ) {
            if( has_flag_furn( "GROWTH_MATURE", p ) ) { return; }

            // Remove fertilizer if any
            map_stack::iterator fertilizer = std::ranges::find_if( items, []( const item * const & it ) {
                return it->has_flag( flag_FERTILIZER );
            } );
            if( fertilizer != items.end() ) { items.erase( fertilizer ); }

            fertilizer = map_stack::iterator();
            rotten_item_spawn( *seed, p );
            // You've skipped the seedling stage so roll monsters twice
            if( !has_flag_furn( "GROWTH_SEEDLING", p ) ) { rotten_item_spawn( *seed, p ); }
            furn_set( p, furn_str_id( furn.plant->transform ) );
        } else {
            // You've skipped two stages so roll monsters two times
            if( has_flag_furn( "GROWTH_SEEDLING", p ) ) {
                rotten_item_spawn( *seed, p );
                rotten_item_spawn( *seed, p );
                // One stage change
            } else if( has_flag_furn( "GROWTH_MATURE", p ) ) {
                rotten_item_spawn( *seed, p );
                // Goes from seed to harvest in one check
            } else {
                rotten_item_spawn( *seed, p );
                rotten_item_spawn( *seed, p );
                rotten_item_spawn( *seed, p );
            }
            furn_set( p, furn_str_id( furn.plant->transform ) );
        }
    }
}

void map::restock_fruits( const tripoint_bub_ms& p,
                          const time_duration& time_since_last_actualize )
{
    const auto& ter = this->ter( p ).obj();
    const auto& furn = this->furn( p ).obj();
    if( !ter.has_flag( TFLAG_HARVESTED ) && !furn.has_flag( TFLAG_HARVESTED ) ) {
        return; // Already harvestable. Do nothing.
    }
    // Make it harvestable again if the last actualization was during a different season or year.
    const time_point last_touched = calendar::turn - time_since_last_actualize;
    if( season_of_year( calendar::turn ) != season_of_year( last_touched )
        || time_since_last_actualize >= calendar::season_length() ) {
        if( ter.has_flag( TFLAG_HARVESTED ) ) { ter_set( p, ter.transforms_into ); }
        if( furn.has_flag( TFLAG_HARVESTED ) ) { furn_set( p, furn.transforms_into ); }
    }
}

void map::produce_sap( const tripoint_bub_ms& p, const time_duration& time_since_last_actualize )
{
    if( time_since_last_actualize <= 0_turns ) { return; }

    if( t_tree_maple_tapped != ter( p ) ) { return; }

    // Amount of maple sap liters produced per season per tap
    static const int maple_sap_per_season = 56;

    // How many turns to produce 1 charge (250 ml) of sap?
    const time_duration producing_length = 0.75 * calendar::season_length();

    const time_duration turns_to_produce = producing_length / ( maple_sap_per_season * 4 );

    // How long of this time_since_last_actualize have we been in the producing period (late winter,
    // early spring)?
    time_duration time_producing = 0_turns;

    if( time_since_last_actualize >= calendar::year_length() ) {
        time_producing = producing_length;
    } else {
        // We are only producing sap on the intersection with the sap producing season.
        const time_duration early_spring_end = 0.5f * calendar::season_length();
        const time_duration late_winter_start = 3.75f * calendar::season_length();

        const time_point last_actualize = calendar::turn - time_since_last_actualize;
        const time_duration last_actualize_tof = time_past_new_year( last_actualize );
        bool last_producing =
            ( last_actualize_tof >= late_winter_start || last_actualize_tof < early_spring_end );
        const time_duration current_tof = time_past_new_year( calendar::turn );
        bool current_producing =
            ( current_tof >= late_winter_start || current_tof < early_spring_end );

        const time_duration non_producing_length = 3.25 * calendar::season_length();

        if( last_producing && current_producing ) {
            if( time_since_last_actualize < non_producing_length ) {
                time_producing = time_since_last_actualize;
            } else {
                time_producing = time_since_last_actualize - non_producing_length;
            }
        } else if( !last_producing && !current_producing ) {
            if( time_since_last_actualize > non_producing_length ) {
                time_producing = time_since_last_actualize - non_producing_length;
            }
        } else if( last_producing && !current_producing ) {
            // We hit the end of early spring
            if( last_actualize_tof < early_spring_end ) {
                time_producing = early_spring_end - last_actualize_tof;
            } else {
                time_producing = calendar::year_length() - last_actualize_tof + early_spring_end;
            }
        } else if( !last_producing && current_producing ) {
            // We hit the start of late winter
            if( current_tof >= late_winter_start ) {
                time_producing = current_tof - late_winter_start;
            } else {
                time_producing = 0.25f * calendar::season_length() + current_tof;
            }
        }
    }

    int new_charges = roll_remainder( time_producing / turns_to_produce );
    // Not enough time to produce 1 charge of sap
    if( new_charges <= 0 ) { return; }

    // Is there a proper container?
    auto items = i_at( p );
    for( auto& it : items ) {
        if( it->is_bucket() || it->is_watertight_container() ) {

            detached_ptr<item> sap = item::spawn( "maple_sap", calendar::turn );

            const int capacity = it->get_remaining_capacity_for_liquid( *sap, true );
            if( capacity > 0 ) {
                new_charges = std::min( new_charges, capacity );

                // The environment might have poisoned the sap with animals passing by, insects,
                // leaves or contaminants in the ground
                sap->poison = one_in( 10 ) ? 1 : 0;
                sap->charges = new_charges;

                it->fill_with( std::move( sap ) );
            }
            // Only fill up the first container.
            break;
        }
    }
}

void map::rad_scorch( const tripoint_bub_ms& p, const time_duration& time_since_last_actualize )
{
    const int rads = get_radiation( p );
    if( rads == 0 ) { return; }

    // TODO: More interesting rad scorch chance - base on season length?
    if( !x_in_y( 1.0 * rads * rads * time_since_last_actualize, 91_days ) ) { return; }

    // First destroy the farmable plants (those are furniture)
    // TODO: Rad-resistant mutant plants (that produce radioactive fruit)
    const furn_t &fid = furn( p ).obj();
    if( fid.has_flag( "PLANT" ) ) {
        i_clear( p );
        furn_set( p, f_null );
    }

    const ter_id tid = ter( p );
    // TODO: De-hardcode this
    static const std::map<ter_id, ter_str_id> dies_into{{
            {t_grass, ter_str_id( "t_dirt" )},
            {t_tree_young, ter_str_id( "t_dirt" )},
            {t_tree_pine, ter_str_id( "t_tree_deadpine" )},
            {t_tree_birch, ter_str_id( "t_tree_birch_harvested" )},
            {t_tree_willow, ter_str_id( "t_tree_willow_harvested" )},
            {t_tree_hickory, ter_str_id( "t_tree_hickory_dead" )},
            {t_tree_hickory_harvested, ter_str_id( "t_tree_hickory_dead" )},
        }};

    const auto iter = dies_into.find( tid );
    if( iter != dies_into.end() ) {
        ter_set( p, iter->second );
        return;
    }

    const ter_t& tr = tid.obj();
    if( tr.has_flag( "SHRUB" ) ) {
        ter_set( p, t_dirt );
    } else if( tr.has_flag( "TREE" ) ) {
        ter_set( p, ter_str_id( "t_tree_dead" ) );
    }
}

void map::decay_cosmetic_fields(
    const tripoint_bub_ms& p, const time_duration& time_since_last_actualize )
{
    for( auto& pr : field_at( p ) ) {
        auto& fd = pr.second;
        const time_duration hl = fd.get_field_type().obj().half_life;
        if( !fd.decays_on_actualize() || hl <= 0_turns ) { continue; }

        const time_duration added_age = 2 * time_since_last_actualize / rng( 2, 4 );
        fd.mod_field_age( added_age );
        const int intensity_drop = fd.get_field_age() / hl;
        if( intensity_drop > 0 ) {
            fd.set_field_intensity( fd.get_field_intensity() - intensity_drop );
            fd.mod_field_age( -hl * intensity_drop );
        }
    }
}

static temperature_flag temperature_flag_at_point( const map& m, const tripoint_bub_ms& p )
{
    if( m.ter( p ) == t_rootcellar ) { return temperature_flag::TEMP_ROOT_CELLAR; }
    if( m.has_flag_furn( TFLAG_FRIDGE, p ) ) { return temperature_flag::TEMP_FRIDGE; }
    if( m.has_flag_furn( TFLAG_FREEZER, p ) ) { return temperature_flag::TEMP_FREEZER; }
    return temperature_flag::TEMP_NORMAL;
}

void map::actualize( const tripoint_bub_sm& grid )
{
    ZoneScopedN( "map_actualize" );
    submap* const tmpsub = get_submap_at_grid( tripoint_bub_sm( grid ) );
    if( tmpsub == nullptr ) {
        debugmsg( "Actualize called on null submap (%d,%d,%d)", grid.x(), grid.y(), grid.z() );
        return;
    }

    // Uniform submaps (empty rock, open air, boundary fill) have no items, furniture,
    // fields, or plants — the entire 144-tile loop is wasted work.  Just stamp the
    // touch time and return.
    if( tmpsub->is_uniform ) {
        tmpsub->last_touched = calendar::turn;
        return;
    }

    const time_duration time_since_last_actualize = calendar::turn - tmpsub->last_touched;
    const bool do_funnels = ( grid.z() >= 0 );

    // check spoiled stuff, and fill up funnels while we're at it
    for( const auto p : submap_tiles() ) {
        const auto pnt = project_combine( grid, p );
        // plants contain a seed item which must not be removed under any circumstances
        auto& items = tmpsub->get_items( p );
        if( !items.empty() ) {
            const auto& furn = this->furn( pnt ).obj();
            if( !furn.has_flag( "DONT_REMOVE_ROTTEN" ) ) {
                const auto temperature = temperature_flag_at_point( *this, pnt );
                remove_rotten_items( items, pnt, temperature );
            }
        }

        if( do_funnels ) { fill_funnels( pnt, tmpsub->last_touched ); }

        grow_plant( pnt );

        restock_fruits( pnt, time_since_last_actualize );

        produce_sap( pnt, time_since_last_actualize );

        rad_scorch( pnt, time_since_last_actualize );

        decay_cosmetic_fields( pnt, time_since_last_actualize );
    }

    // the last time we touched the submap, is right now.
    tmpsub->last_touched = calendar::turn;
}

void map::add_roofs( const tripoint_bub_sm& grid )
{
    if( !zlevels ) {
        // No roofs required!
        // Why not? Because submaps below and above don't exist yet
        return;
    }

    submap* const sub_here = get_submap_at_grid( grid );
    if( sub_here == nullptr ) {
        // Null submaps are expected for corner slots outside the circular load footprint
        // and for out-of-bounds areas in bounded pocket dimensions.
        return;
    }

    bool check_roof = grid.z() > -OVERMAP_DEPTH;

    submap* const sub_below = check_roof ? get_submap_at_grid( grid + tripoint_below ) : nullptr;

    if( check_roof && sub_below == nullptr ) {
        if( !has_dimension_bounds() ) {
            debugmsg( "Tried to add roofs to sm at %d,%d,%d, but sm below doesn't exist", grid.x(),
                      grid.y(), grid.z() );
        }
        return;
    }

    for( const auto sm_ms : submap_tiles() ) {
        const ter_id ter_here = sub_here->get_ter( sm_ms );
        if( ter_here == t_open_air ) {
            if( !check_roof ) {
                // Make sure we don't have open air at lowest z-level
                sub_here->set_ter( sm_ms, t_rock_floor );
            } else {
                const ter_t& ter_below = sub_below->get_ter( sm_ms ).obj();
                if( ter_below.roof ) {
                    // TODO: Make roof variable a ter_id to speed this up
                    sub_here->set_ter( sm_ms, ter_below.roof.id() );
                }
            }
        }
    }
}

void map::copy_grid( const tripoint_bub_sm& to, const tripoint_bub_sm& from )
{
    const auto smap = get_submap_at_grid( from );
    setsubmap( get_nonant( to ), smap );
    if( smap == nullptr ) { return; }
    for( auto& it : smap->vehicles ) { it->abs_sm_pos = bub_to_abs( to ); }
}

void map::spawn_monsters_submap_group(
    const tripoint_bub_sm& gp, mongroup& group, bool ignore_sight )
{
    const int s_range =
        std::min( g_half_mapsize_x, g->u.sight_range( g->light_level( g->u.bub_pos().z() ) ) );
    int pop = group.population;
    std::vector<tripoint_bub_ms> locations;
    if( !ignore_sight ) {
        // If the submap is one of the outermost submaps, assume that monsters are
        // invisible there.
        if( gp.x() == 0 || gp.y() == 0 || gp.x() + 1 == my_MAPSIZE || gp.y() + 1 == my_MAPSIZE ) {
            ignore_sight = true;
        }
    }

    if( gp.z() != g->u.bub_pos().z() ) {
        // Note: this is only OK because 3D vision isn't a thing yet
        ignore_sight = true;
    }

    static const auto allow_on_terrain = [&]( const tripoint_bub_ms & p ) {
        // TODO: flying creatures should be allowed to spawn without a floor,
        // but the new creature is created *after* determining the terrain, so
        // we can't check for it here.
        return passable( p ) && has_floor( p );
    };

    // If the submap is uniform, we can skip many checks
    const submap* current_submap = get_submap_at_grid( tripoint_bub_sm( gp ) );
    if( current_submap == nullptr ) { return; }
    bool ignore_terrain_checks = false;
    bool ignore_inside_checks = false;
    if( current_submap->is_uniform ) {
        const tripoint_bub_ms upper_left{SEEX * gp.x(), SEEY * gp.y(), gp.z()};
        if( !allow_on_terrain( upper_left ) || ( !ignore_inside_checks && !is_outside( upper_left ) ) ) {
            const auto glp = bub_to_abs( gp );
            dbg( DL::Warn ) << "Empty locations for group " << group.type.str()
                            << " at uniform submap " << gp << " global " << glp;
            return;
        }

        ignore_terrain_checks = true;
        ignore_inside_checks = true;
    }

    for( const auto sm_ms : submap_tiles() ) {
        const auto fp = project_combine( gp, sm_ms );
        // If there is already a creature at this location, skip it
        if( ( g->critter_at( fp ) == nullptr ) &&
            // Skip impassable terrain
            ( ignore_terrain_checks || allow_on_terrain( fp ) ) &&
            // monster must spawn outside the viewing range of the player
            ( ignore_sight || !sees( g->u.bub_pos(), fp, s_range ) ) &&
            // monster must spawn outside.
            ( ignore_inside_checks || is_outside( fp ) ) &&
            // hordes must not appear inside player-owned vehicles.
            ( !horde_should_avoid_vehicle_tile( *this, fp, group ) ) ) {
            locations.push_back( fp );
        }
    }

    if( locations.empty() ) {
        // TODO: what now? there is no possible place to spawn monsters, most
        // likely because the player can see all the places.
        const auto glp = bub_to_abs( gp );
        dbg( DL::Warn ) << "Empty locations for group " << group.type.str() << " at " << gp
                        << " global " << glp;
        // Just kill the group. It's not like we're removing existing monsters
        // Unless it's a horde - then don't kill it and let it spawn behind a tree or smoke cloud
        if( !group.horde ) { group.clear(); }

        return;
    }

    if( pop ) {
        // Populate the group from its population variable.
        for( int m = 0; m < pop; m++ ) {
            MonsterGroupResult spawn_details =
                MonsterGroupManager::GetResultFromGroup( group.type, &pop );
            if( !spawn_details.name ) { continue; }
            monster tmp( spawn_details.name );

            // If a monster came from a horde population, configure them to always be willing to
            // rejoin a horde.
            if( group.horde ) { tmp.set_horde_attraction( MHA_ALWAYS ); }
            for( int i = 0; i < spawn_details.pack_size; i++ ) { group.monsters.push_back( tmp ); }
        }
    }

    // Find horde's target submap
    // TODO: fix point types
    auto horde_target = project_to<coords::ms>(
                            tripoint_abs_sm( -abs_sub.xy(), abs_sub.z() ) + group.target.xy().raw() );
    for( auto& tmp : group.monsters ) {
        for( int tries = 0; tries < 10 && !locations.empty(); tries++ ) {
            const auto p = random_entry_removed( locations );
            if( !tmp.can_move_to( p ) ) {
                continue; // target can not contain the monster
            }
            if( group.horde ) {
                // Give monster a random point near horde's expected destination
                const auto rand_dest =
                    abs_to_bub( horde_target ) + point_rel_ms( rng( 0, SEEX ), rng( 0, SEEY ) );
                const int turns = rl_dist( p, rand_dest ) + group.interest;
                tmp.wander_to( rand_dest, turns );
                add_msg( m_debug, "%s targeting %d,%d,%d", tmp.disp_name(), tmp.wander_pos.x(),
                         tmp.wander_pos.y(), tmp.wander_pos.z() );
            }

            monster* const placed = g->place_critter_at( make_shared_fast<monster>( tmp ), p );
            if( placed ) { placed->on_load(); }
            break;
        }
    }
    // indicates the group is empty, and can be removed later
    group.clear();
}

void map::spawn_monsters_submap( const tripoint_bub_sm& gp, bool ignore_sight )
{
    // Load unloaded monsters
    // TODO: fix point types
    get_overmapbuffer( bound_dimension_ ).spawn_monster( bub_to_abs( gp ) );

    // Only spawn new monsters after existing monsters are loaded.
    // TODO: fix point types
    auto groups = get_overmapbuffer( bound_dimension_ ).groups_at( bub_to_abs( gp ) );
    for( auto& mgp : groups ) { spawn_monsters_submap_group( gp, *mgp, ignore_sight ); }

    submap* const current_submap = get_submap_at_grid( tripoint_bub_sm( gp ) );
    if( current_submap == nullptr ) { return; }
    const auto gp_ms = project_to<coords::ms>( gp );

    for( auto& i : current_submap->spawns ) {
        const auto center = gp_ms + i.pos.raw();
        const tripoint_range<tripoint_bub_ms> points = points_in_radius( center, 3 );

        for( int j = 0; j < i.count; j++ ) {
            monster tmp( i.type );
            tmp.mission_id = i.mission_id;
            if( i.mission_id != -1 ) {
                mission* found_mission = mission::find( i.mission_id );
                if( found_mission != nullptr
                    && found_mission->get_type().goal == MGOAL_KILL_MONSTERS ) {
                    found_mission->register_kill_needed();
                }
            }
            if( i.name != "NONE" ) { tmp.unique_name = i.name; }
            if( i.is_friendly() ) { tmp.friendly = -1; }

            const auto valid_location = [&]( const tripoint_bub_ms & p ) {
                // Checking for creatures via g is only meaningful if this is the main game map.
                // If it's some local map instance, the coordinates will most likely not even match.
                return ( !g || &get_map() != this || !g->critter_at( p ) ) && tmp.can_move_to( p );
            };

            const auto place_it = [&]( const tripoint_bub_ms & p ) {
                monster* const placed = g->place_critter_at( make_shared_fast<monster>( tmp ), p );
                if( placed ) {
                    placed->on_load();
                    cata::run_hooks( "on_creature_spawn", [&]( sol::table & params ) {
                        params["creature"] = placed;
                    } );
                    cata::run_hooks( "on_monster_spawn", [&]( sol::table & params ) {
                        params["monster"] = placed;
                    } );
                    if( i.disposition == spawn_disposition::SpawnDisp_Pet ) { placed->make_pet(); }
                }
            };

            // First check out defined spawn location for a valid placement, and if that doesn't
            // work then fall back to picking a random point that is a valid location.
            if( valid_location( center ) ) {
                place_it( center );
            } else if(
                const std::optional<tripoint_bub_ms> pos = random_point( points, valid_location ) ) {
                place_it( *pos );
            }
        }
    }
    current_submap->spawns.clear();
}

void map::spawn_monsters( bool ignore_sight )
{
    ZoneScoped;
    const auto zmin = zlevels ? -OVERMAP_DEPTH : abs_sub.z();
    const auto zmax = zlevels ? OVERMAP_HEIGHT : abs_sub.z();

    for( const auto gp : tripoint_range<tripoint_bub_sm>(
             tripoint_bub_sm( point_bub_sm::zero(), zmin ),
             tripoint_bub_sm( bubble_submaps().max(), zmax ) ) ) {
        spawn_monsters_submap( gp, ignore_sight );
    }
}

auto map::spawn_monsters_new_submaps( const point_rel_sm& shift_amount ) -> void
{
    ZoneScoped;

    // If the shift covers the full map in any dimension every submap is new -
    // fall back to the full spawn to avoid an empty or degenerate strip.
    if( std::abs( shift_amount.x() ) >= my_MAPSIZE || std::abs( shift_amount.y() ) >= my_MAPSIZE ) {
        spawn_monsters( false );
        return;
    }

    const auto zmin = zlevels ? -OVERMAP_DEPTH : abs_sub.z();
    const auto zmax = zlevels ? OVERMAP_HEIGHT : abs_sub.z();
    const auto bounds = bubble_submap_bounds();
    for( const auto gp : tripoint_range<tripoint_bub_sm>(
             tripoint_bub_sm( point_bub_sm::zero(), zmin ),
             tripoint_bub_sm( bubble_submaps().max(), zmax ) ) ) {
        if( !bounds.contains( gp.xy() + shift_amount ) ) { spawn_monsters_submap( gp, false ); }
    }
}

void map::clear_spawns()
{
    for( auto& smap : grid ) {
        if( smap != nullptr ) { smap->spawns.clear(); }
    }
}

void map::clear_traps()
{
    for( auto& smap : grid ) {
        if( smap == nullptr ) { continue; }
        for( const auto p : submap_tiles() ) { smap->set_trap( p, tr_null ); }
    }
}

bool map::inbounds( const tripoint_bub_sm& p ) const
{
    // Use runtime my_MAPSIZE so this agrees with get_nonant()'s grid indexing.
    // MAPSIZE (and MAPSIZE_X/Y) are compile-time constants sized for the maximum
    // reality bubble; using them here when the live map is smaller allows p values
    // that pass inbounds() but produce an out-of-bounds grid[] index in get_nonant().
    const auto max_xy = my_MAPSIZE;
    return inbounds_z( p.z() ) && p.x() >= 0 && p.x() < max_xy && p.y() >= 0 && p.y() < max_xy;
}

bool map::inbounds( const tripoint_abs_sm& p ) const { return inbounds( abs_to_bub( p ) ); }

bool map::is_position_simulated( const tripoint_bub_sm& p ) const
{
    return submap_loader.is_simulated( bound_dimension_, bub_to_abs( p ) );
}

bool tinymap::inbounds( const tripoint_abs_sm& p ) const
{
    constexpr tripoint_abs_sm map_boundary_min( 0, 0, -OVERMAP_DEPTH );
    constexpr tripoint_abs_sm map_boundary_max( SEEX * 2, SEEY * 2, OVERMAP_HEIGHT + 1 );

    constexpr half_open_cuboid<tripoint_abs_sm> map_boundaries( map_boundary_min, map_boundary_max );

    return map_boundaries.contains( p );
}

// set up a map just long enough scribble on it
// this tinymap should never, ever get saved
fake_map::fake_map(
    const furn_id& fur_type, const ter_id& ter_type, const trap_id& trap_type,
    const int fake_map_z )
{
    const tripoint_abs_sm tripoint_below_zero( 0, 0, fake_map_z );

    set_abs_sub( tripoint_below_zero );
    for( const auto p : bubble_submaps() ) {
        const auto sm_pos = tripoint_bub_sm( p, fake_map_z );
        std::unique_ptr<submap> sm = std::make_unique<submap>( bub_to_abs( sm_pos ) );

        sm->set_all_ter( ter_type );
        sm->set_all_furn( fur_type );
        sm->set_all_traps( trap_type );

        setsubmap( get_nonant( sm_pos ), sm.get() );

        temp_submaps_.emplace_back( std::move( sm ) );
    }
}

fake_map::~fake_map() = default;

void map::set_graffiti( const tripoint_bub_ms& p, const std::string& contents )
{
    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );
    if( current_submap == nullptr ) { return; }
    current_submap->set_graffiti( l, contents );
}

void map::delete_graffiti( const tripoint_bub_ms& p )
{
    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );
    if( current_submap == nullptr ) { return; }
    current_submap->delete_graffiti( l );
}

const std::string &map::graffiti_at( const tripoint_bub_ms& p ) const
{
    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );
    if( current_submap == nullptr ) {
        static const std::string empty_string;
        return empty_string;
    }
    return current_submap->get_graffiti( l );
}

bool map::has_graffiti_at( const tripoint_bub_ms& p ) const
{
    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );
    if( current_submap == nullptr ) { return false; }
    return current_submap->has_graffiti( l );
}

int map::determine_wall_corner( const tripoint_bub_ms& p ) const
{
    int test_connect_group = ter( p ).obj().connect_group;
    uint8_t connections = get_known_connections( p, test_connect_group );
    // The bits in connections are SEWN, whereas the characters in LINE_
    // constants are NESW, so we want values in 8 | 2 | 1 | 4 order.
    switch( connections ) {
        case 8 | 2 | 1 | 4:
            return LINE_XXXX;
        case 0 | 2 | 1 | 4:
            return LINE_OXXX;

        case 8 | 0 | 1 | 4:
            return LINE_XOXX;
        case 0 | 0 | 1 | 4:
            return LINE_OOXX;

        case 8 | 2 | 0 | 4:
            return LINE_XXOX;
        case 0 | 2 | 0 | 4:
            return LINE_OXOX;
        case 8 | 0 | 0 | 4:
            return LINE_XOOX;
        case 0 | 0 | 0 | 4:
            return LINE_OXOX; // LINE_OOOX would be better

        case 8 | 2 | 1 | 0:
            return LINE_XXXO;
        case 0 | 2 | 1 | 0:
            return LINE_OXXO;
        case 8 | 0 | 1 | 0:
            return LINE_XOXO;
        case 0 | 0 | 1 | 0:
            return LINE_XOXO; // LINE_OOXO would be better
        case 8 | 2 | 0 | 0:
            return LINE_XXOO;
        case 0 | 2 | 0 | 0:
            return LINE_OXOX; // LINE_OXOO would be better
        case 8 | 0 | 0 | 0:
            return LINE_XOXO; // LINE_XOOO would be better

        case 0 | 0 | 0 | 0:
            return ter( p ).obj().symbol(); // technically just a column

        default:
            // assert( false );
            // this shall not happen
            return '?';
    }
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

void map::build_obstacle_cache(
    const tripoint_bub_ms& start, const tripoint_bub_ms& end, float *obstacle_cache, int cache_sy )
{
    const point_sm_ms min_submap{std::max( 0, start.x() / SEEX ), std::max( 0, start.y() / SEEY )};
    const point_sm_ms max_submap{
        std::min( my_MAPSIZE - 1, end.x() / SEEX ), std::min( my_MAPSIZE - 1, end.y() / SEEY )};
    // Find and cache all the map obstacles.
    // For now setting obstacles to be extremely dense and fill their squares.
    // In future, scale effective obstacle density by the thickness of the obstacle.
    // Also consider modelling partial obstacles.
    // TODO: Support z-levels.
    for( int smx = min_submap.x(); smx <= max_submap.x(); ++smx ) {
        for( int smy = min_submap.y(); smy <= max_submap.y(); ++smy ) {
            const auto gridp = tripoint_bub_sm( smx, smy, start.z() );
            const auto cur_submap = get_submap_at_grid( gridp );
            if( cur_submap == nullptr ) {
                for( const auto sm_ms : submap_tiles() ) {
                    const auto ms_pos = project_combine( gridp, sm_ms );
                    obstacle_cache[ms_pos.x() * cache_sy + ms_pos.y()] = 1000.0f;
                }
                continue;
            }

            // TODO: Init indices to prevent iterating over unused submap sections.
            for( const auto sm_ms : submap_tiles() ) {
                int ter_move = cur_submap->get_ter( sm_ms ).obj().movecost;
                int furn_move = cur_submap->get_furn( sm_ms ).obj().movecost;
                const auto ms_pos = project_combine( gridp, sm_ms );
                if( ter_move == 0 || furn_move < 0 || ter_move + furn_move == 0 ) {
                    obstacle_cache[ms_pos.x() * cache_sy + ms_pos.y()] = 1000.0f;
                } else {
                    obstacle_cache[ms_pos.x() * cache_sy + ms_pos.y()] = 0.0f;
                }
            }
        }
    }
    const auto start_sm = project_to<coords::sm>( start );
    const auto end_sm = project_to<coords::sm>( end );
    VehicleList vehs = get_vehicles( start_sm, end_sm );
    const inclusive_cuboid<tripoint_bub_ms> bounds( start, end );
    // Cache all the vehicle stuff in one loop
    for( auto& v : vehs ) {
        for( const vpart_reference& vp : v.v->get_all_parts() ) {
            auto p = v.pos + vp.part().precalc[0];
            if( p.z() != start.z() ) { break; }
            if( !bounds.contains( p ) ) { continue; }

            if( vp.obstacle_at_part() ) { obstacle_cache[p.x() * cache_sy + p.y()] = 1000.0f; }
        }
    }
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

void map::build_floor_caches()
{
    ZoneScoped;

    const int minz = zlevels ? -OVERMAP_DEPTH : abs_sub.z();
    const int maxz = zlevels ? OVERMAP_HEIGHT : abs_sub.z();
    for( int z = minz; z <= maxz; z++ ) { build_floor_cache( z ); }
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

void map::do_vehicle_caching( int z )
{
    level_cache& ch = get_cache( z );
    for( vehicle * v : ch.vehicle_list ) {
        for( const vpart_reference& vp : v->get_all_parts() ) {
            const tripoint_bub_ms& part_pos = v->bub_part_location( vp.part() );
            if( !inbounds( part_pos ) || vp.part().removed ) { continue; }
            vehicle_caching_internal( get_cache( part_pos.z() ), vp, v );
            if( part_pos.z() < OVERMAP_HEIGHT ) {
                vehicle_caching_internal_above( get_cache( part_pos.z() + 1 ), vp, v );
            }
        }
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

submap *map::getsubmap( const size_t grididx ) const
{
    if( grididx >= grid.size() ) {
        debugmsg( "Tried to access invalid grid index %d. Grid size: %d", grididx, grid.size() );
        return nullptr;
    }
    return grid[grididx];
}

void map::setsubmap( const size_t grididx, submap* const smap )
{
    if( grididx >= grid.size() ) {
        debugmsg( "Tried to access invalid grid index %d", grididx );
        return;
    }
    grid[grididx] = smap;
}

submap *map::get_submap_at( const tripoint_bub_ms &p ) const
{
    if( inbounds( p ) ) {
    // Fast path: tile is inside the reality bubble grid.
    return get_submap_at_grid( tripoint_bub_sm( p.x() / SEEX, p.y() / SEEY, p.z() ) );
    }
    if( is_out_of_bounds( p ) ) {
    // Outside dimension bounds — genuinely invalid position.
    return nullptr;
}
// Loaded-but-out-of-bubble fallback: look up from the bound dimension's mapbuffer.
// Uses lookup_submap_in_memory to avoid triggering disk loads from query functions.
const tripoint_abs_sm abs_sm_pos(
    abs_sub.x() + divide_round_to_minus_infinity( p.x(), SEEX ),
        abs_sub.y() + divide_round_to_minus_infinity( p.y(), SEEY ),
        p.z()
    );
    return MAPBUFFER_REGISTRY.get( bound_dimension_ ).lookup_submap_in_memory( abs_sm_pos );
}

submap *map::get_submap_at( const tripoint_bub_ms& p, point_sm_ms& offset_p ) const
{
    // Use floor-division so that negative local coords (out-of-bubble) give the
    // correct submap-local offset in [0, SEEX) rather than a negative value.
    const int smx = divide_round_to_minus_infinity( p.x(), SEEX );
    const int smy = divide_round_to_minus_infinity( p.y(), SEEY );
    offset_p = point_sm_ms( point( p.x() - smx * SEEX, p.y() - smy * SEEY ) );
    return get_submap_at( tripoint_bub_ms( p ) );
}

submap *map::get_submap_at_grid( const tripoint_bub_sm& gridp ) const
{
    if( gridp.x() >= 0 && gridp.x() < my_MAPSIZE && gridp.y() >= 0 && gridp.y() < my_MAPSIZE ) {
        return getsubmap( get_nonant( gridp ) );
    }
    // Out-of-bubble fallback: the submap may still be loaded in the bound
    // dimension's mapbuffer even though it has no slot in the grid[] array.
    const tripoint_abs_sm abs_sm( abs_sub.x() + gridp.x(), abs_sub.y() + gridp.y(), gridp.z() );
    return MAPBUFFER_REGISTRY.get( bound_dimension_ ).lookup_submap_in_memory( abs_sm );
}

size_t map::get_nonant( const tripoint_bub_sm& gridp ) const
{
    // There used to be a bounds check here
    // But this function is called a lot, so push it up if needed
    if( zlevels ) {
    const int indexz = gridp.z() + OVERMAP_HEIGHT; // Can't be lower than 0
        return indexz + ( gridp.x() + gridp.y() * my_MAPSIZE ) * OVERMAP_LAYERS;
    } else {
        return gridp.x() + gridp.y() * my_MAPSIZE;
    }
}

tinymap::tinymap( int mapsize, bool zlevels ): map( mapsize, zlevels ) {}

void tinymap::bind_submaps_for_hook( const tripoint_abs_sm& sm_base )
{
    // Directly wire the four 2×2 grid slots to the already-resident submaps.
    // Does NOT call loadn()/actualize() — freshly generated submaps need no
    // time-advance, and this tinymap is never rendered, simulated, or saved.
    set_abs_sub( sm_base );
    mapbuffer& mb = MAPBUFFER_REGISTRY.get( get_bound_dimension() );
    for( int di = 0; di < 2; ++di ) {
        for( int dj = 0; dj < 2; ++dj ) {
            const tripoint_abs_sm neighbor( sm_base.x() + di, sm_base.y() + dj, sm_base.z() );
            setsubmap( get_nonant( tripoint_bub_sm{di, dj, sm_base.z()} ),
                       mb.lookup_submap_in_memory( neighbor ) );
        }
    }
}

void map::draw_line_ter( const ter_id& type, const point_bub_ms& p1, const point_bub_ms& p2 )
{
    draw_line( [this, type]( const point & p ) { this->ter_set( point_bub_ms( p ), type ); }, p1.raw(),
            p2.raw() );
}

void map::draw_line_furn( const furn_id& type, const point_bub_ms& p1, const point_bub_ms& p2 )
{
    draw_line( [this, type]( const point & p ) { this->furn_set( point_bub_ms( p ), type ); }, p1.raw(),
            p2.raw() );
}

void map::draw_fill_background( const ter_id& type )
{
    // Need to explicitly set caches dirty - set_ter would do it before
    set_transparency_cache_dirty( abs_sub.z() );
    set_seen_cache_dirty( abs_sub.z() );
    set_outside_cache_dirty( abs_sub.z() );
    set_pathfinding_cache_dirty( abs_sub.z() );

    // Fill each submap rather than each tile
    for( const auto p : bubble_submaps() ) {
        auto sm = get_submap_at_grid( p );
        sm->is_uniform = true;
        sm->set_all_ter( type );
    }
}

void map::draw_fill_background( ter_id( *f )() )
{
    draw_square_ter(
        f, point_bub_ms::zero(), point_bub_ms( SEEX * my_MAPSIZE - 1, SEEY * my_MAPSIZE - 1 ) );
}
void map::draw_fill_background( const weighted_int_list<ter_id> &f )
{
    draw_square_ter(
        f, point_bub_ms::zero(), point_bub_ms( SEEX * my_MAPSIZE - 1, SEEY * my_MAPSIZE - 1 ) );
}

void map::draw_square_ter( const ter_id& type, const point_bub_ms& p1, const point_bub_ms& p2 )
{
    draw_square( [this, type]( const point & p ) { this->ter_set( point_bub_ms( p ), type ); },
    p1.raw(),
      p2.raw() );
}

void map::draw_square_furn( const furn_id& type, const point_bub_ms& p1, const point_bub_ms& p2 )
{
    draw_square( [this, type]( const point & p ) { this->furn_set( point_bub_ms( p ), type ); },
    p1.raw(),
      p2.raw() );
}

void map::draw_square_ter( ter_id( *f )(), const point_bub_ms& p1, const point_bub_ms& p2 )
{
    draw_square( [this, f]( const point & p ) { this->ter_set( point_bub_ms( p ), f() ); }, p1.raw(),
            p2.raw() );
}

void map::draw_square_ter(
    const weighted_int_list<ter_id> &f, const point_bub_ms& p1, const point_bub_ms& p2 )
{
    draw_square(
    [this, f]( const point & p ) {
        const ter_id* tid = f.pick();
        this->ter_set( point_bub_ms( p ), tid != nullptr ? *tid : t_null );
    },
    p1.raw(), p2.raw() );
}

void map::draw_rough_circle_ter( const ter_id& type, const point_bub_ms& p, int rad )
{
    draw_rough_circle(
    [this, type]( const point & p ) { this->ter_set( point_bub_ms( p ), type ); }, p.raw(), rad );
}

void map::draw_rough_circle_furn( const furn_id& type, const point_bub_ms& p, int rad )
{
    draw_rough_circle(
    [this, type]( const point & p ) { this->furn_set( point_bub_ms( p ), type ); }, p.raw(), rad );
}

void map::draw_circle_ter( const ter_id& type, const rl_vec2d& p, double rad )
{
    draw_circle( [this, type]( const point & p ) { this->ter_set( point_bub_ms( p ), type ); }, p,
    rad );
}

void map::draw_circle_ter( const ter_id& type, const point_bub_ms& p, int rad )
{
    draw_circle( [this, type]( const point & p ) { this->ter_set( point_bub_ms( p ), type ); }, p.raw(),
            rad );
}

void map::draw_circle_furn( const furn_id& type, const point_bub_ms& p, int rad )
{
    draw_circle( [this, type]( const point & p ) { this->furn_set( point_bub_ms( p ), type ); },
    p.raw(),
     rad );
}

void map::add_corpse( const tripoint_bub_ms& p )
{
    detached_ptr<item> body;

    const bool isReviveSpecial = one_in( 10 );

    if( !isReviveSpecial ) {
        body = item::make_corpse();
    } else {
        body = item::make_corpse( mon_zombie );
        body->set_flag( flag_REVIVE_SPECIAL );
    }

    put_items_from_loc( item_group_id( "default_zombie_clothes" ), p );
    if( one_in( 3 ) ) { put_items_from_loc( item_group_id( "default_zombie_items" ), p ); }

    add_item_or_charges( p, std::move( body ) );
}

field &map::get_field( const tripoint_bub_ms& p ) { return field_at( p ); }

void map::creature_on_trap( Creature& c, const bool may_avoid )
{
    const auto& tr = tr_at( c.bub_pos() );
    if( tr.is_null() ) { return; }
    // boarded in a vehicle means the player is above the trap, like a flying monster and can
    // never trigger the trap.
    const player* const p = dynamic_cast<const player *>( &c );
    if( p != nullptr && p->in_vehicle ) { return; }
    if( may_avoid && c.avoid_trap( c.bub_pos(), tr ) ) { return; }
    tr.trigger( c.bub_pos(), &c );
}

template <typename Functor>
auto map::function_over( const tripoint_bub_ms& start, const tripoint_bub_ms& end,
                         Functor fun ) const
-> void
{
    // start and end are just two points, end can be "before" start
    // Also clip the area to map area
    const auto min = tripoint_bub_ms(
                         std::max( std::min( start.x(), end.x() ), 0 ), std::max( std::min( start.y(), end.y() ), 0 ),
                         std::max( std::min( start.z(), end.z() ), -OVERMAP_DEPTH ) );
    const auto max = tripoint_bub_ms(
                         std::min( std::max( start.x(), end.x() ), SEEX * my_MAPSIZE - 1 ),
                         std::min( std::max( start.y(), end.y() ), SEEY * my_MAPSIZE - 1 ),
                         std::min( std::max( start.z(), end.z() ), OVERMAP_HEIGHT ) );

    if( min.x() > max.x() || min.y() > max.y() || min.z() > max.z() ) { return; }

    // Submaps that contain the bounding points
    const auto min_sm = project_to<coords::sm>( min.xy() );
    const auto max_sm = project_to<coords::sm>( max.xy() );
    const auto submap_range = point_range<point_bub_sm>( min_sm, max_sm );

    const auto apply_to_submap = [&]( const tripoint_bub_sm & sm_pos,
                                      const submap * cur_submap, const point_sm_ms & sm_min,
    const point_sm_ms & sm_max ) -> iteration_state {
for( const auto sm_ms : point_range<point_sm_ms>( sm_min, sm_max ) )
    {
        const auto rval = fun( sm_pos, cur_submap, sm_ms );
            if( rval != ITER_CONTINUE ) {
                return rval;
            }
        }
        return ITER_CONTINUE;
    };

    // Z outermost, because submaps are flat.
    for( const auto z : std::views::iota( min.z(), max.z() + 1 ) ) {
        auto skip_zlevel = false;
        for( const auto smp : submap_range ) {
            if( skip_zlevel ) { break; }
            const auto sm_pos = tripoint_bub_sm( smp, z );
            const auto* cur_submap = get_submap_at_grid( sm_pos );
            if( cur_submap == nullptr ) {
                // This can happen in pocket dimensions where out-of-bounds
                // submaps are intentionally set to null.
                continue;
            }
            const auto sm_ms_min = project_remain<coords::sm>( min ).remainder;
            const auto sm_ms_max = project_remain<coords::sm>( max ).remainder;

            const auto sm_min = point_sm_ms(
                                    smp.x() > min_sm.x() ? 0 : sm_ms_min.x(), smp.y() > min_sm.y() ? 0 : sm_ms_min.y() );
            const auto sm_max = point_sm_ms(
                                    smp.x() < max_sm.x() ? SEEX - 1 : sm_ms_max.x(),
                                    smp.y() < max_sm.y() ? SEEY - 1 : sm_ms_max.y() );
            switch( apply_to_submap( sm_pos, cur_submap, sm_min, sm_max ) ) {
                case ITER_CONTINUE:
                case ITER_SKIP_SUBMAP:
                    break;
                case ITER_SKIP_ZLEVEL:
                    skip_zlevel = true;
                    break;
                case ITER_FINISH:
                    return;
            }
        }
    }
}

void map::scent_blockers(
    std::vector<char> &scent_transfer, int st_sy, const point_bub_ms& min,
    const point_bub_ms& max )
{
    if( st_sy <= 0 ) { return; }
    const auto scent_cache_x = static_cast<int>( scent_transfer.size() / static_cast<size_t>( st_sy ) );
    const auto reduce = TFLAG_REDUCE_SCENT;
    const auto block = TFLAG_NO_SCENT;
    auto fill_values = [&]( const tripoint_bub_sm & gp, const submap * sm, point_sm_ms lp ) {
        // We need to generate the x/y coordinates, because we can't get them "for free"
        const auto p = project_combine( gp, lp );
        if( sm->get_ter( lp ).obj().has_flag( block ) ) {
            scent_transfer[p.x() * st_sy + p.y()] = 0;
        } else if( sm->get_ter( lp ).obj().has_flag( reduce )
                   || sm->get_furn( lp ).obj().has_flag( reduce ) ) {
            scent_transfer[p.x() * st_sy + p.y()] = 1;
        } else {
            scent_transfer[p.x() * st_sy + p.y()] = 5;
        }

        return ITER_CONTINUE;
    };

    function_over( tripoint_bub_ms( min.x(), min.y(), abs_sub.z() ),
                   tripoint_bub_ms( max.x(), max.y(), abs_sub.z() ), fill_values );

    const inclusive_rectangle<point_bub_ms> local_bounds( min, max );
    const auto mark_vehicle_obstruction = [&]( const tripoint_bub_ms & part_pos ) {
        if( !local_bounds.contains( part_pos.xy() ) || part_pos.x() < 0 || part_pos.y() < 0
            || part_pos.x() >= scent_cache_x || part_pos.y() >= st_sy ) {
            return;
        }
        const auto index = static_cast<size_t>( part_pos.x() * st_sy + part_pos.y() );
        if( scent_transfer[index] == 5 ) { scent_transfer[index] = 1; }
    };

    // Now vehicles

    auto vehs = get_vehicles();
    for( auto& wrapped_veh : vehs ) {
        vehicle& veh = *( wrapped_veh.v );
        for( const vpart_reference& vp : veh.get_any_parts( VPFLAG_OBSTACLE ) ) {
            mark_vehicle_obstruction( vp.pos() );
        }

        // Doors, but only the closed ones
        for( const vpart_reference& vp : veh.get_any_parts( VPFLAG_OPENABLE ) ) {
            if( !vp.part().open ) { mark_vehicle_obstruction( vp.pos() ); }
        }
    }
}

tripoint_range<tripoint_bub_ms> map::points_in_rectangle(
    const tripoint_bub_ms& from, const tripoint_bub_ms& to ) const
{
    const auto bubble_max = bubble_tiles().max();
    const auto min = tripoint_bub_ms(
                         std::max( 0, std::min( from.x(), to.x() ) ), std::max( 0, std::min( from.y(), to.y() ) ),
                         std::max( -OVERMAP_DEPTH, std::min( from.z(), to.z() ) ) );
    const auto max = tripoint_bub_ms(
                         std::min( bubble_max.x(), std::max( from.x(), to.x() ) ),
                         std::min( bubble_max.y(), std::max( from.y(), to.y() ) ),
                         std::min( OVERMAP_HEIGHT, std::max( from.z(), to.z() ) ) );
    if( min.x() > max.x() || min.y() > max.y() || min.z() > max.z() ) {
        return tripoint_range<tripoint_bub_ms>( tripoint_bub_ms::zero(), tripoint_bub_ms( 0, 0, -1 ) );
    }
    return tripoint_range<tripoint_bub_ms>( min, max );
}

tripoint_range<tripoint_bub_ms> map::points_in_radius(
    const tripoint_bub_ms& center, size_t radius, size_t radiusz ) const
{
    const auto xy_radius = static_cast<int>( radius );
    const auto z_radius = static_cast<int>( radiusz );
    const auto bubble_max = bubble_tiles().max();
    const auto min = tripoint_bub_ms(
                         std::max( 0, center.x() - xy_radius ), std::max( 0, center.y() - xy_radius ),
                         clamp( center.z() - z_radius, -OVERMAP_DEPTH, OVERMAP_HEIGHT ) );
    const auto max = tripoint_bub_ms(
                         std::min( bubble_max.x(), center.x() + xy_radius ),
                         std::min( bubble_max.y(), center.y() + xy_radius ),
                         clamp( center.z() + z_radius, -OVERMAP_DEPTH, OVERMAP_HEIGHT ) );
    if( min.x() > max.x() || min.y() > max.y() || min.z() > max.z() ) {
        return tripoint_range<tripoint_bub_ms>( tripoint_bub_ms::zero(), tripoint_bub_ms( 0, 0, -1 ) );
    }
    return tripoint_range<tripoint_bub_ms>( min, max );
}

tripoint_range<tripoint_bub_ms> map::points_on_zlevel( const int z ) const
{
    if( z < -OVERMAP_DEPTH || z > OVERMAP_HEIGHT ) {
    // TODO: need a default constructor that creates an empty range.
    return tripoint_range<tripoint_bub_ms>( tripoint_bub_ms::zero(),
                                            tripoint_bub_ms::zero() - tripoint_above );
    }
    return tripoint_range<tripoint_bub_ms>(
               tripoint_bub_ms( 0, 0, z ), tripoint_bub_ms( SEEX * my_MAPSIZE - 1, SEEY * my_MAPSIZE - 1, z ) );
}

tripoint_range<tripoint_bub_ms> map::points_on_zlevel() const
{
    return points_on_zlevel( abs_sub.z() );
}

std::vector<item *> map::get_active_items_in_radius(
    const tripoint_bub_ms& center, int radius ) const
{
    return get_active_items_in_radius( center, radius, special_item_type::none );
}

std::vector<item *> map::get_active_items_in_radius(
    const tripoint_bub_ms& center, int radius, special_item_type type ) const
{
    std::vector<item *> result;

    const point_bub_ms minp( center.xy() + point_rel_ms( -radius, -radius ) );
    const point_bub_ms maxp( center.xy() + point_rel_ms( radius, radius ) );

    const point_sm_ms ming( std::max( minp.x() / SEEX, 0 ), std::max( minp.y() / SEEY, 0 ) );
    const point_sm_ms
    maxg( std::min( maxp.x() / SEEX, my_MAPSIZE - 1 ), std::min( maxp.y() / SEEY, my_MAPSIZE - 1 ) );

    for( const tripoint_abs_sm& abs_submap_loc : submaps_with_active_items ) {
        const tripoint_bub_sm submap_loc = abs_to_bub( abs_submap_loc );
        if( submap_loc.x() < ming.x() || submap_loc.y() < ming.y() || submap_loc.x() > maxg.x()
            || submap_loc.y() > maxg.y() ) {
            continue;
        }
        const point_bub_ms sm_offset( submap_loc.x() * SEEX, submap_loc.y() * SEEY );

        submap* sm = get_submap_at_grid( tripoint_bub_sm( submap_loc ) );
        if( sm == nullptr ) { continue; }
        std::vector<item *> items =
            type == special_item_type::none
            ? sm->active_items.get()
            : sm->active_items.get_special( type );
        for( const auto& elem : items ) {
            if( rl_dist( elem->position(), center ) > radius ) { continue; }

            if( elem ) { result.emplace_back( elem ); }
        }
    }

    return result;
}

std::vector<tripoint_bub_ms> map::find_furnitures_with_flag_in_omt(
    const tripoint_bub_ms& p, const std::string& flag )
{
    // Some stupid code to get to the corner
    const auto omt_p = abs_to_bub( project_to<coords::ms>( project_to<coords::omt>( bub_to_abs(
                                       p ) ) ) );

    std::vector<tripoint_bub_ms> furn_locs;
    for( const auto& furn_loc : points_in_rectangle(
             omt_p,
             tripoint_bub_ms( omt_p.x() + 2 * SEEX - 1, omt_p.y() + 2 * SEEY - 1, omt_p.z() ) ) ) {
        if( has_flag_furn( flag, furn_loc ) ) { furn_locs.push_back( furn_loc ); }
    }
    return furn_locs;
};

std::list<tripoint_bub_ms> map::find_furnitures_with_flag_in_radius(
    const tripoint_bub_ms& center, size_t radius, const std::string& flag, size_t radiusz )
{
    std::list<tripoint_bub_ms> furn_locs;
    for( const auto& furn_loc : points_in_radius( center, radius, radiusz ) ) {
        if( has_flag_furn( flag, furn_loc ) ) { furn_locs.push_back( furn_loc ); }
    }
    return furn_locs;
}

std::list<tripoint_bub_ms> map::find_furnitures_or_vparts_with_flag_in_radius(
    const tripoint_bub_ms& center, size_t radius, const std::string& flag, size_t radiusz )
{
    std::list<tripoint_bub_ms> locs;
    for( const auto& loc : points_in_radius( center, radius, radiusz ) ) {
        // workaround for ramp bridges
        int dz = 0;
        if( has_flag( TFLAG_RAMP_UP, loc ) ) {
            dz = 1;
        } else if( has_flag( TFLAG_RAMP_DOWN, loc ) ) {
            dz = -1;
        }

        if( dz == 0 ) {
            if( has_flag_furn_or_vpart( flag, loc ) ) { locs.push_back( loc ); }
        } else {
            const auto newloc( loc + tripoint_rel_ms( 0, 0, dz ) );
            if( has_flag_furn_or_vpart( flag, newloc ) ) { locs.push_back( newloc ); }
        }
    }

    return locs;
}

std::list<Creature *> map::get_creatures_in_radius(
    const tripoint_bub_ms& center, size_t radius, size_t radiusz )
{
    std::list<Creature *> creatures;
    for( const auto& loc : points_in_radius( center, radius, radiusz ) ) {
        Creature* tmp_critter = g->critter_at( loc );
        if( tmp_critter != nullptr ) { creatures.push_back( tmp_critter ); }
    }
    return creatures;
}

bool map::has_rope_at( tripoint_bub_ms pt ) const
{
    if( cached_veh_rope.contains( pt.xy() ) ) {
    auto veh_pair = get_rope_at( tripoint_bub_ms( pt ).xy() );
        vehicle *veh = veh_pair.first;
        int veh_part = veh_pair.second;
        return veh->part( veh_part ).info().ladder_length() >= veh->bub_ms_location().z() - pt.z();
    }
    return false;
}
std::pair<vehicle *, int> map::get_rope_at( const point_bub_ms& pt ) const
{
    return cached_veh_rope.at( pt );
}

level_cache &map::access_cache( int zlev )
{
    if( zlev >= -OVERMAP_DEPTH && zlev <= OVERMAP_HEIGHT ) { return *caches[zlev + OVERMAP_DEPTH]; }

    debugmsg( "access_cache called with invalid z-level: %d", zlev );
    return nullcache;
}

const level_cache &map::access_cache( int zlev ) const
{
    if( zlev >= -OVERMAP_DEPTH && zlev <= OVERMAP_HEIGHT ) { return *caches[zlev + OVERMAP_DEPTH]; }

    debugmsg( "access_cache called with invalid z-level: %d", zlev );
    return nullcache;
}

// Default constructor: zero-sized null sentinel — not for normal use.
level_cache::level_cache() = default;

/// Normal constructor: mx = SEEX * mapsize, my = SEEY * mapsize.
// Tile-coordinate vectors are allocated at the runtime mx * my so that the
// cache correctly tracks the actual loaded-area dimensions.  idx() now uses
// the runtime cache_y stride, matching these allocations.
level_cache::level_cache( int mx, int my )
    : cache_x( mx ),
      cache_y( my ),
      cache_mapsize( mx / SEEX ),
      transparency_cache_dirty( static_cast<size_t>( mx / SEEX ) * ( my / SEEY ) ),
      outside_cache_dirty( static_cast<size_t>( mx / SEEX ) * ( my / SEEY ) ),
      floor_cache_dirty( static_cast<size_t>( mx / SEEX ) * ( my / SEEY ) ),
      lightmap_dirty( static_cast<size_t>( mx / SEEX ) * ( my / SEEY ) ),
      lm( static_cast<size_t>( mx * my ), four_quadrants( 0.0f ) ),
      sm( static_cast<size_t>( mx * my ), 0.0f ),
      light_source_buffer( static_cast<size_t>( mx * my ) ),
      light_color_cache( static_cast<size_t>( mx * my ) ),
      outside_cache( static_cast<size_t>( mx * my ), '\0' ),
      sheltered_cache( static_cast<size_t>( mx * my ), '\0' ),
      angled_sunlight_cache( static_cast<size_t>( mx * my ), '\0' ),
      floor_cache( static_cast<size_t>( mx * my ), false ),
      vehicle_floor_cache( static_cast<size_t>( mx * my ), '\0' ),
      transparency_cache( static_cast<size_t>( mx * my ), 0.0f ),
      vehicle_obscured_cache( static_cast<size_t>( mx * my ), diagonal_blocks{false, false} ),
      vehicle_obstructed_cache( static_cast<size_t>( mx * my ), diagonal_blocks{false, false} ),
      seen_cache( static_cast<size_t>( mx * my ), 0.0f ),
      camera_cache( static_cast<size_t>( mx * my ), 0.0f ),
      visibility_cache( static_cast<size_t>( mx * my ), lit_level::DARK ),
      map_memory_seen_cache( static_cast<size_t>( mx * my ) ),
      veh_exists_at( static_cast<size_t>( mx * my ), false )
{
    transparency_cache_dirty.set();
    outside_cache_dirty.set();
    floor_cache_dirty.set();
    lightmap_dirty.set();
}


void map::set_pathfinding_cache_dirty( const int zlev )
{
    if( !inbounds_z( zlev ) ) { return; }
    for( const auto p : bubble_submaps() ) {
        auto* sm = get_submap_at_grid( tripoint_bub_sm( p, zlev ) );
        if( sm ) { sm->pf_dirty = true; }
    }
}

void map::set_pathfinding_cache_dirty( const tripoint_bub_ms& p )
{
    point_sm_ms l;
    submap* const sm = get_submap_at( tripoint_bub_ms( p ), l );
    if( sm ) { sm->pf_dirty = true; }
}

auto map::get_pf_special( const tripoint_bub_ms& p ) const -> pf_special
{
    point_sm_ms l;
    submap* sm = get_submap_at( p, l );
    if( !sm ) { return PF_WALL; }
    if( sm->pf_dirty ) { sm->rebuild_pf_cache( *this, project_to<coords::sm>( p ) ); }
    return sm->pf_special_cache[l.x()][l.y()];
}

bool map::check_seen_cache( const tripoint_bub_ms& p ) const
{
    const level_cache& ch = get_cache( p.z() );
    return !ch.map_memory_seen_cache[static_cast<size_t>( p.x() + p.y() * ch.cache_x )];
}

bool map::check_and_set_seen_cache( const tripoint_bub_ms& p ) const
{
    level_cache& ch = get_cache( p.z() );
    const size_t offset = static_cast<size_t>( p.x() + p.y() * ch.cache_x );
    if( !ch.map_memory_seen_cache[offset] ) {
        ch.map_memory_seen_cache.set( offset );
        return true;
    }
    return false;
}

void map::invalidate_map_cache( const int zlev )
{
    // [shift-probe] invalidate_map_cache sets every dirty bitset .all() for a level,
    // forcing a full structural rebuild (the residual 20-23ms shift spike if it fires
    // across many z).  Log each call; emit a backtrace once per turn to identify the
    // caller without flooding.  Remove after the trigger is found.
#if defined(BACKTRACE)
    {
        DebugLogFL( DL::Info, DC::Main ) << "[shift-probe][invalidate] z=" << zlev;
        static int _last_bt_turn = -1;
        const int _now = to_turn<int>( calendar::turn );
        if( _now != _last_bt_turn ) {
            _last_bt_turn = _now;
            std::ostringstream _bt;
            debug_write_backtrace( _bt );
            DebugLogFL( DL::Info, DC::Main ) << "[shift-probe][invalidate-bt]\n" << _bt.str();
        }
    }
#endif
    if( inbounds_z( zlev ) ) {
        level_cache& ch = get_cache( zlev );
        ch.floor_cache_dirty.set();
        ch.transparency_cache_dirty.set();
        ch.seen_cache_dirty = true;
        ch.lightmap_dirty.set();
        ch.visibility_cache_dirty = true;
        ch.outside_cache_dirty.set();
        ch.suspension_cache_dirty = true;
        m_last_seen_cache_origin = tripoint_bub_ms( tripoint_min );
        m_solar.last_built_hour = -1;
        m_solar.last_built_light_level_int = -1;
    }
}

void map::invalidate_lightmap_caches()
{
    const int minz = zlevels ? -OVERMAP_DEPTH : abs_sub.z();
    const int maxz = zlevels ? OVERMAP_HEIGHT : abs_sub.z();
    std::ranges::for_each( std::views::iota( minz, maxz + 1 ), [this]( int z ) {
        get_cache( z ).lightmap_dirty.set();
    } );
}

void map::invalidate_visibility_caches()
{
    const int minz = zlevels ? -OVERMAP_DEPTH : abs_sub.z();
    const int maxz = zlevels ? OVERMAP_HEIGHT : abs_sub.z();
    std::ranges::for_each( std::views::iota( minz, maxz + 1 ), [this]( int z ) {
        get_cache( z ).visibility_cache_dirty = true;
    } );
}

void map::set_memory_seen_cache_dirty( const tripoint_bub_ms& p )
{
    level_cache& ch = get_cache( p.z() );
    const int offset = p.x() + p.y() * ch.cache_x;
    if( offset >= 0 && offset < ch.cache_x * ch.cache_y ) {
        ch.map_memory_seen_cache.reset( static_cast<size_t>( offset ) );
    }
}

void map::mark_lightmap_dirty( const tripoint_bub_ms& p )
{
    if( !inbounds_z( p.z() ) ) { return; }
    level_cache& ch = get_cache( p.z() );
    const int smx = p.x() / SEEX;
    const int smy = p.y() / SEEY;
    const size_t bidx = static_cast<size_t>( ch.bidx( smx, smy ) );
    ch.lightmap_dirty.set( bidx );
}

void map::clip_to_bounds( point_bub_ms& p ) const
{
    constexpr int sms = coords::map_squares_per( coords::scale::submap );
    p.x() = std::clamp( p.x(), 0, sms * my_MAPSIZE - 1 );
    p.y() = std::clamp( p.y(), 0, sms * my_MAPSIZE - 1 );
}

void map::clip_to_bounds( point_bub_sm& p ) const
{
    p.x() = std::clamp( p.x(), 0, my_MAPSIZE - 1 );
    p.y() = std::clamp( p.y(), 0, my_MAPSIZE - 1 );
}

void map::clip_to_bounds( tripoint_bub_ms& p ) const
{
    constexpr int sms = coords::map_squares_per( coords::scale::submap );
    p.x() = std::clamp( p.x(), 0, sms * my_MAPSIZE - 1 );
    p.y() = std::clamp( p.y(), 0, sms * my_MAPSIZE - 1 );
    p.z() = std::clamp( p.z(), -OVERMAP_DEPTH, OVERMAP_HEIGHT );
}

void map::clip_to_bounds( tripoint_bub_sm& p ) const
{
    p.x() = std::clamp( p.x(), 0, my_MAPSIZE - 1 );
    p.y() = std::clamp( p.y(), 0, my_MAPSIZE - 1 );
    p.z() = std::clamp( p.z(), -OVERMAP_DEPTH, OVERMAP_HEIGHT );
}

bool map::is_cornerfloor( const tripoint_bub_ms &p ) const
{
    if( impassable( p ) ) {
    return false;
}
std::set<tripoint_bub_ms> impassable_adjacent;
for( const tripoint_bub_ms &pt : points_in_radius( p, 1 ) ) {
    if( impassable( pt ) ) {
            impassable_adjacent.insert( pt );
        }
    }
    if( !impassable_adjacent.empty() ) {
    //to check if a floor is a corner we first search if any of its diagonal adjacent points is impassable
    std::set< tripoint_bub_ms> diagonals = { p + tripoint_north_east, p + tripoint_north_west, p + tripoint_south_east, p + tripoint_south_west };
    for( const auto &impassable_diagonal : diagonals ) {
            if( impassable_adjacent.contains( impassable_diagonal ) ) {
                //for every impassable diagonal found, we check if that diagonal terrain has at least two impassable neighbors that also neighbor point p
                int f = 0;
                for( const auto& l : points_in_radius( impassable_diagonal, 1 ) ) {
                    if( impassable_adjacent.contains( l ) ) { f++; }
                    if( f > 2 ) { return true; }
                }
            }
        }
    }
    return false;
}

int map::calc_max_populated_zlev()
{
    // cache is filled and valid, skip recalculation
    if( max_populated_zlev && max_populated_zlev->first == get_abs_sub() ) {
        return max_populated_zlev->second;
    }

    // We'll assume ground level is populated
    int max_z = 0;

    for( int sz = 1; sz <= OVERMAP_HEIGHT; sz++ ) {
        bool level_done = false;
        for( const auto p : bubble_submaps() ) {
            const submap* sm = get_submap_at_grid( tripoint_bub_sm( p, sz ) );
            if( sm == nullptr || !sm->is_uniform ) {
                max_z = sz;
                level_done = true;
                break;
            }
            if( level_done ) { break; }
        }
    }

    max_populated_zlev = std::pair<tripoint_abs_sm, int>( get_abs_sub(), max_z );
    return max_z;
}

void map::invalidate_max_populated_zlev( int zlev )
{
    if( max_populated_zlev && max_populated_zlev->second < zlev ) {
        max_populated_zlev->second = zlev;
    }
}

tripoint_bub_ms drawsq_params::center() const
{
    if( view_center == tripoint_bub_ms( tripoint_min ) ) {
        return g->u.bub_pos() + g->u.view_offset;
    } else {
        return view_center;
    }
}
