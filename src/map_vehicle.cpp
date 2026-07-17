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


#define dbg(x) DebugLog((x), DC::Map)


VehicleList map::get_vehicles()
{
    if( last_full_vehicle_list_dirty ) {
        const auto sm_max = bubble_submaps().max();
        if( !zlevels ) {
            last_full_vehicle_list = get_vehicles(
                                         tripoint_bub_sm( point_bub_sm::zero(), abs_sub.z() ),
                                         tripoint_bub_sm( sm_max, abs_sub.z() ) );
        } else {
            last_full_vehicle_list = get_vehicles(
                                         tripoint_bub_sm( point_bub_sm::zero(), -OVERMAP_DEPTH ),
                                         tripoint_bub_sm( sm_max, OVERMAP_HEIGHT ) );
        }

        last_full_vehicle_list_dirty = false;
    }

    return last_full_vehicle_list;
}

void map::reset_vehicle_cache()
{
    last_full_vehicle_list_dirty = true;
    clear_vehicle_cache();

    // Cache all vehicles
    const int zmin = zlevels ? -OVERMAP_DEPTH : abs_sub.z();
    const int zmax = zlevels ? OVERMAP_HEIGHT : abs_sub.z();
    for( int zlev = zmin; zlev <= zmax; zlev++ ) {
        auto& ch = get_cache( zlev );
        for( const auto& elem : ch.vehicle_list ) {
            elem->adjust_zlevel( 0, tripoint_rel_ms::zero() );
            add_vehicle_to_cache( elem );
        }
    }
}

void map::add_vehicle_to_cache( vehicle* veh )
{
    if( veh == nullptr ) {
        debugmsg( "Tried to add null vehicle to cache" );
        return;
    }

    // Get parts
    for( const vpart_reference& vpr : veh->get_all_parts() ) {
        if( vpr.part().removed ) { continue; }
        const auto p = abs_to_bub( veh->abs_part_location( vpr.part() ) );
        int part = veh->part_with_feature( vpr.part_index(), VPFLAG_LADDER, true );
        if( part != -1 ) {
            // NOTE: This cache may need to be submapfied at some point
            cached_veh_rope[p.xy()] = std::make_pair( veh, static_cast<int>( part ) );
        }
        level_cache& ch = get_cache( p.z() );
        ch.veh_in_active_range = true;

        // DANGER: Unlike what you think where you can just use vpr.has_flag( VPFLAG_NOCOLLIDE )
        // THAT DOES NOT WORK DO NOT TRY AND CHANGE THIS MESS
        if( !ch.veh_cached_parts.contains( p )
            || ( !veh->part_info( vpr.part_index() ).has_flag( VPFLAG_NOCOLLIDE ) ) ) {
            ch.veh_cached_parts[p] = std::make_pair( veh, static_cast<int>( vpr.part_index() ) );
        }
        if( inbounds( p ) ) { ch.veh_exists_at[ch.idx( p.x(), p.y() )] = true; }
    }

    last_full_vehicle_list_dirty = true;
}

void map::clear_vehicle_point_from_cache( vehicle* veh, const tripoint_bub_ms& pt )
{
    if( veh == nullptr ) {
        debugmsg( "Tried to clear null vehicle from cache" );
        return;
    }

    level_cache& ch = get_cache( pt.z() );
    auto it = ch.veh_cached_parts.find( pt );
    if( it != ch.veh_cached_parts.end() && it->second.first == veh ) {
        if( inbounds( pt ) ) { ch.veh_exists_at[ch.idx( pt.x(), pt.y() )] = false; }
        ch.veh_cached_parts.erase( it );
        cached_veh_rope.erase( pt.xy() );
    }
}

void map::clear_vehicle_cache()
{
    const int zmin = zlevels ? -OVERMAP_DEPTH : abs_sub.z();
    const int zmax = zlevels ? OVERMAP_HEIGHT : abs_sub.z();
    for( int zlev = zmin; zlev <= zmax; zlev++ ) {
        level_cache& ch = get_cache( zlev );
        while( !ch.veh_cached_parts.empty() ) {
            const auto part = ch.veh_cached_parts.begin();
            const auto& p = part->first;
            if( inbounds( p ) ) { ch.veh_exists_at[ch.idx( p.x(), p.y() )] = false; }
            ch.veh_cached_parts.erase( part );
        }
        ch.veh_in_active_range = false;
    }
    cached_veh_rope.clear();
}

void map::clear_vehicle_list( const int zlev )
{
    auto& ch = get_cache( zlev );
    ch.vehicle_list.clear();
    ch.zone_vehicles.clear();

    last_full_vehicle_list_dirty = true;
}

void map::update_vehicle_list( const submap* const to, const int zlev )
{
    if( to == nullptr ) { return; }
    // Update vehicle data
    level_cache& ch = get_cache( zlev );
    for( const auto& elem : to->vehicles ) {
        ch.vehicle_list.insert( elem.get() );
        if( !elem->loot_zones.empty() ) { ch.zone_vehicles.insert( elem.get() ); }
    }

    last_full_vehicle_list_dirty = true;
}

std::unique_ptr<vehicle> map::detach_vehicle( vehicle* veh )
{
    if( veh == nullptr ) {
        debugmsg( "map::detach_vehicle was passed nullptr" );
        return std::unique_ptr<vehicle>();
    }

    int z = veh->abs_sm_pos.z();
    if( z < -OVERMAP_DEPTH || z > OVERMAP_HEIGHT ) {
        debugmsg( "detach_vehicle got a vehicle outside allowed z-level range!  name=%s, "
                  "submap:%d,%d,%d",
                  veh->name, veh->abs_sm_pos.x(), veh->abs_sm_pos.y(), veh->abs_sm_pos.z() );
        // Try to fix by moving the vehicle here
        z = veh->abs_sm_pos.z() = abs_sub.z();
    }

    // Unboard all passengers before detaching
    for( auto const& part : veh->get_avail_parts( VPFLAG_BOARDABLE ) ) {
        player* passenger = part.get_passenger();
        if( passenger ) { unboard_vehicle( part, passenger ); }
    }
    veh->invalidate_towing( true );
    // During mapgen, submaps are held in the local tinymap grid but have not yet been
    // transferred to MAPBUFFER (that happens at the end of generate()).  Fall back to
    // the grid lookup so wreck-merging works correctly during generation.
    const tripoint_bub_sm bub_sm(
        veh->abs_sm_pos.x() - abs_sub.x(), veh->abs_sm_pos.y() - abs_sub.y(), veh->abs_sm_pos.z() );
    submap* current_submap =
        MAPBUFFER_REGISTRY.get( bound_dimension_ ).lookup_submap_in_memory( veh->abs_sm_pos );
    if( current_submap == nullptr ) { current_submap = get_submap_at_grid( bub_sm ); }
    if( current_submap == nullptr ) {
        debugmsg( "detach_vehicle can't find submap!  name=%s, submap:%d,%d,%d", veh->name,
                  veh->abs_sm_pos.x(), veh->abs_sm_pos.y(), veh->abs_sm_pos.z() );
        loaded_vehicles.erase( veh );
        dirty_vehicle_list.erase( veh );
        return std::unique_ptr<vehicle>();
    }
    level_cache& ch = get_cache( z );
    for( size_t i = 0; i < current_submap->vehicles.size(); i++ ) {
        if( current_submap->vehicles[i].get() == veh ) {
            ch.vehicle_list.erase( veh );
            ch.zone_vehicles.erase( veh );
            reset_vehicle_cache();
            std::unique_ptr<vehicle> result = std::move( current_submap->vehicles[i] );
            current_submap->vehicles.erase( current_submap->vehicles.begin() + i );
            loaded_vehicles.erase( veh );
            if( veh->tracking_on ) { get_overmapbuffer( bound_dimension_ ).remove_vehicle( veh ); }
            dirty_vehicle_list.erase( veh );
            veh->detach();
            veh->refresh_position();
            return result;
        }
    }
    debugmsg( "detach_vehicle can't find it!  name=%s, submap:%d,%d,%d", veh->name,
              veh->abs_sm_pos.x(), veh->abs_sm_pos.y(), veh->abs_sm_pos.z() );
    return std::unique_ptr<vehicle>();
}

void map::destroy_vehicle( vehicle* veh )
{
#ifdef BOX2D_ENABLED
    if( phys_world ) { phys_world->on_vehicle_removed( veh ); }
#endif
    detach_vehicle( veh );
}

void map::on_vehicle_moved(
    const tripoint_bub_sm& sm_min, const tripoint_bub_sm& sm_max, const int &smz )
{
    ZoneScoped;

    if( !inbounds_z( smz ) ) { return; }

    auto& ch = get_cache( smz );
    invalidate_lightmap_caches();
    m_solar.last_built_hour = -1;
    set_seen_cache_dirty( smz );

    const auto for_clamped_submaps =
    [&]( const point_bub_sm & range_min, const point_bub_sm & range_max, const auto & callback ) {
        const auto bubble_bounds = bubble_submap_bounds();
        const auto requested = inclusive_rectangle<point_bub_sm>( range_min, range_max );
        if( !bubble_bounds.overlaps( requested ) ) { return; }
        for( const auto p : point_range<point_bub_sm>(
                 clamp( range_min, bubble_bounds ), clamp( range_max, bubble_bounds ) ) ) {
            callback( p );
        }
    };

    // Mark dirty only the submaps the vehicle actually occupies (union of old
    // and new footprint), rather than the entire z-level.
    for_clamped_submaps( sm_min.xy(), sm_max.xy(), [&]( const point_bub_sm & p ) {
        const auto idx = static_cast<size_t>( ch.bidx( p.x(), p.y() ) );
        ch.transparency_cache_dirty.set( idx );
        ch.floor_cache_dirty.set( idx );
        auto* sm = get_submap_at_grid( tripoint_bub_sm( p, smz ) );
        if( sm ) {
            sm->transparency_dirty = true;
            sm->floor_dirty = true;
            sm->pf_dirty = true;
        }
    } );

    // outside_cache has a 3x3 tile neighbourhood dependency, so expand the
    // dirty region by one submap in each direction.
    const auto outside_min = point_bub_sm( sm_min.x() - 1, sm_min.y() - 1 );
    const auto outside_max = point_bub_sm( sm_max.x() + 1, sm_max.y() + 1 );
    for_clamped_submaps( outside_min, outside_max, [&]( const point_bub_sm & p ) {
        const auto idx = static_cast<size_t>( ch.bidx( p.x(), p.y() ) );
        ch.outside_cache_dirty.set( idx );
        auto* sm = get_submap_at_grid( tripoint_bub_sm( p, smz ) );
        if( sm ) { sm->outside_dirty = true; }
    } );

    // Vehicles can extend through the floor; mark the level above as well.
    const auto above_z = smz + 1;
    if( inbounds_z( above_z ) ) {
        auto& ch_above = get_cache( above_z );
        set_seen_cache_dirty( above_z );
        for_clamped_submaps( sm_min.xy(), sm_max.xy(), [&]( const point_bub_sm & p ) {
            ch_above.floor_cache_dirty.set( static_cast<size_t>( ch_above.bidx( p.x(), p.y() ) ) );
            auto* sm = get_submap_at_grid( tripoint_bub_sm( p, above_z ) );
            if( sm ) { sm->floor_dirty = true; }
        } );
    }
}

void map::vehmove()
{
    ZoneScoped;
#ifdef BOX2D_ENABLED
    // Advance the persistent physics world one game tick (~1 s at 60 Hz, 4 sub-steps).
    if( phys_world ) { phys_world->step( 1.0f / 60.0f, 4 ); }
#endif


    // Give vehicles movement points.  Use per-z-level vehicle_list caches
    // (rebuilt from in-bubble grid submaps during shift) rather than
    // loaded_vehicles, which can hold stale pointers to evicted submaps.
    // Out-of-bubble vehicles are handled by batch_turns_vehicle().
    VehicleList vehicle_list;
    {
        ZoneScopedN( "veh_gain_moves" );
        const int zmin = zlevels ? -OVERMAP_DEPTH : abs_sub.z();
        const int zmax = zlevels ? OVERMAP_HEIGHT : abs_sub.z();
        const bool outer_stride_hit = calendar::stride_due( vehicle_outer_stride );
        for( int z = zmin; z <= zmax; ++z ) {
            for( vehicle * veh : get_cache( z ).vehicle_list ) {
                const bool on_player_z = z == abs_sub.z();
                const bool parked_off_z = !veh->is_moving()
                                          && !veh->engine_on
                                          && !veh->is_falling
                                          && !on_player_z;
                const bool skip_outer = parked_off_z && !outer_stride_hit;
                if( !skip_outer ) {
                    veh->gain_moves();
                    veh->slow_leak();
                } else {
                    veh->of_turn = 0.001f;
                }
                vehicle_list.push_back( wrapped_vehicle{.pos = veh->bub_ms_location(), .v = veh} );
            }
        }
    }
    TracyPlot( "Vehicles Active", static_cast<int64_t>( vehicle_list.size() ) );

    // Priority queue keyed on of_turn (max-heap) for O(log V) scheduling
    // instead of the previous O(V) linear scan per iteration.
    auto veh_cmp = []( const wrapped_vehicle * a, const wrapped_vehicle * b ) {
        return a->v->of_turn < b->v->of_turn;
    };
    using VehPQ =
        std::priority_queue<wrapped_vehicle *, std::vector<wrapped_vehicle *>, decltype( veh_cmp )>;
    VehPQ pq( veh_cmp );

    // Separate list of falling / aircraft-z-change vehicles, built alongside the PQ.
    std::vector<wrapped_vehicle *> falling_vehicles;

    // (Re)build pq from vehicle_list, applying the stationary-vehicle filter.
    // Also rebuilds falling_vehicles.
    auto rebuild_pq = [&]() {
        // Use swap-with-empty instead of repeated pop() to clear the queue.
        // pop() calls std::pop_heap which invokes veh_cmp, which dereferences
        // wrapped_vehicle* pointers.  If vehicle_list was just reassigned
        // (vehicle-destroyed path), those pointers are dangling; the comparator
        // dereference would be UB and can corrupt the heap allocator metadata.
        {
            VehPQ tmp( veh_cmp );
            std::swap( pq, tmp );
        }
        falling_vehicles.clear();
        for( wrapped_vehicle& w : vehicle_list ) {
            // gain_moves() sets of_turn=0.001 for velocity==0 non-falling
            // non-autopilot vehicles.  Moving and falling vehicles receive
            // of_turn = 1 + carry >= 1.0, so this threshold is unambiguous.
            if( w.v->of_turn >= 1.0f ) { pq.push( &w ); }
            if( w.v->is_falling || ( w.v->is_aircraft() && w.v->get_z_change() != 0 ) ) {
                falling_vehicles.push_back( &w );
            }
        }
    };
    rebuild_pq();

    // the update_active_range lambda that previously scanned all
    // veh_exists_at cells (up to MAPSIZE_X * MAPSIZE_Y per z-level) after every
    // vehicle move has been removed.  veh_in_active_range is maintained
    // incrementally by the existing update_vehicle_list() / vehicle removal
    // paths; a full per-move scan is redundant and O(M²) in the worst case.

    // 15 equals 3 >50mph vehicles, or up to 15 slow (1 square move) ones
    // But 15 is too low for V12 death-bikes, let's put 100 here
    auto moved_count = int64_t{0};
    for( int count = 0; count < 100; ++count ) {
        wrapped_vehicle* cur_veh = nullptr;

        // Horizontal movement — pop highest-of_turn vehicle from heap.
        if( !pq.empty() ) {
            cur_veh = pq.top();
            pq.pop();
        }

        // Vertical-only fallback (falling / aircraft z-change).
        // Scan the pre-built falling_vehicles list (O(falling_count)) instead of
        // the full vehicle_list.  Entries are re-checked for staleness — a vehicle
        // may have landed or been destroyed since falling_vehicles was last built.
        // Vehicles that start falling mid-turn (not in falling_vehicles) are caught
        // on the next turn when rebuild_pq() sees their updated state.
        if( cur_veh == nullptr ) {
            for( wrapped_vehicle * w : falling_vehicles ) {
                if( w->v != nullptr
                    && ( w->v->is_falling || ( w->v->is_aircraft() && w->v->get_z_change() != 0 ) ) ) {
                    cur_veh = w;
                    break;
                }
            }
        }

        if( cur_veh == nullptr ) { break; }

        {
            ZoneScopedN( "veh_act_on_map" );
            ++moved_count;
            cur_veh->v = cur_veh->v->act_on_map();
        }

        if( cur_veh->v == nullptr ) {
            // act_on_map() returns nullptr in two cases:
            //   1. Vehicle destroyed (e.g., fell into void, sank).
            //   2. Vehicle-vehicle collision: move_vehicle() yields to the hit
            //      vehicle by returning nullptr (veh_veh_coll_flag path).
            // In both cases refresh the list so destroyed vehicles are absent
            // and updated of_turn values are visible to rebuild_pq.
            vehicle_list = get_vehicles();
            rebuild_pq();
        } else {
            if( cur_veh->v->of_turn > 0.f ) { pq.push( cur_veh ); }
        }
    }
    TracyPlot( "Vehicles Moved", moved_count );
    static_cast<void>( moved_count );

    // A map shift can occur mid-loop when the player is a vehicle passenger:
    if( last_full_vehicle_list_dirty ) { vehicle_list = get_vehicles(); }
#ifdef BOX2D_ENABLED
    // Box2D position readback: apply physics_pos to the tile grid for vehicles
    // under physics authority.  act_on_map() above ran all game logic (sinking,
    // falling, traction, skidding) but returned early before move_vehicle().
    // physics_pos was written by step() at line 784 before the tile-step loop.
    if( phys_world ) {
        for( wrapped_vehicle &wv : vehicle_list ) {
            vehicle &veh = *wv.v;
            if( !veh.box2d_position_authority ) { continue; }
            // Falling and aircraft z-change: act_on_map() falls through to tile-step
            // for vertical movement (box2d_position_authority guard checks !should_fall
            // && requested_z_change==0).  Skip xy readback here; z handled separately.
            if( veh.is_falling
                || ( veh.is_aircraft() && veh.get_z_change() != 0 ) ) { continue; }
            const auto px  = static_cast<int>( std::lround( veh.physics_pos.x ) );
            const auto py  = static_cast<int>( std::lround( veh.physics_pos.y ) );
            const auto cur = veh.bub_ms_location();
            if( px != cur.x() || py != cur.y() ) {
                displace_vehicle( veh, tripoint_rel_ms{ px - cur.x(), py - cur.y(), 0 } );
            }
        }
    }
#endif


    // Process item removal on the vehicles that were modified this turn.
    // Use a copy because part_removal_cleanup can modify the container.
    {
        ZoneScopedN( "veh_cleanup" );
        auto temp = dirty_vehicle_list;
        for( const auto& elem : temp ) {
            auto same_ptr = [elem]( const struct wrapped_vehicle & tgt ) { return elem == tgt.v; };
            if( std::ranges::find_if( vehicle_list, same_ptr ) != vehicle_list.end() ) {
                elem->part_removal_cleanup();
            }
        }
        dirty_vehicle_list.clear();
    }
    // Build connected_vehicles from the full loaded-vehicle set.
    // All vehicles in vehicle_list are loaded (on_map=true); distribution-graph
    // neighbours reachable but not in the set get on_map=false as before.
    std::set<vehicle *> all_veh_ptrs;
    std::ranges::for_each( vehicle_list, [&]( const wrapped_vehicle & w ) {
        if( loaded_vehicles.contains( w.v ) ) { all_veh_ptrs.insert( w.v ); }
    } );
    std::map<vehicle *, bool> connected_vehicles;
    vehicle::enumerate_vehicles( connected_vehicles, all_veh_ptrs );
    {
        const bool stride_hit = calendar::stride_due( vehicle_idle_stride );
        std::ranges::for_each( connected_vehicles, [&]( std::pair<vehicle* const, bool> &veh_pair ) {
            vehicle& veh = *veh_pair.first;
            const bool on_map = veh_pair.second;
            const bool full_rate = veh.is_moving()
                                   || veh.is_falling
                                   || veh.engine_on
                                   || veh.player_in_control( g->u )
                                   || veh.is_following
                                   || veh.is_patrolling
                                   || !veh.reactors.empty()
                                   || ( veh.is_rotorcraft() && veh.is_flying_in_air() );
            if( full_rate || stride_hit ) {
                veh.idle( on_map );
            }
        } );
    }
}

bool map::vehproceed( VehicleList& vehicle_list )
{
    wrapped_vehicle* cur_veh = nullptr;
    float max_of_turn = 0;
    // First horizontal movement
    for( wrapped_vehicle& vehs_v : vehicle_list ) {
        if( vehs_v.v->of_turn > max_of_turn ) {
            cur_veh = &vehs_v;
            max_of_turn = cur_veh->v->of_turn;
        }
    }

    // Then vertical-only movement
    if( cur_veh == nullptr ) {
        for( wrapped_vehicle& vehs_v : vehicle_list ) {
            if( vehs_v.v->is_falling
                || ( vehs_v.v->is_aircraft() && vehs_v.v->get_z_change() != 0 ) ) {
                cur_veh = &vehs_v;
                break;
            }
        }
    }

    if( cur_veh == nullptr ) { return false; }

    cur_veh->v = cur_veh->v->act_on_map();
    if( cur_veh->v == nullptr ) { vehicle_list = get_vehicles(); }

    // confirm that veh_in_active_range is still correct for each z-level
    int minz = zlevels ? -OVERMAP_DEPTH : abs_sub.z();
    int maxz = zlevels ? OVERMAP_HEIGHT : abs_sub.z();
    for( int zlev = minz; zlev <= maxz; ++zlev ) {
        level_cache& cache = get_cache( zlev );

        // Check if any vehicles exist in the active range for this z-level
        cache.veh_in_active_range =
            cache.veh_in_active_range
        && std::ranges::any_of( cache.veh_exists_at, []( bool veh_exists ) { return veh_exists; } );
    }

    return true;
}

static bool sees_veh( const Creature& c, vehicle& veh, bool force_recalc )
{
    const auto& veh_points = veh.get_points( force_recalc );
    return std::ranges::any_of( veh_points, [&c]( const tripoint_abs_ms & pt ) {
        return c.sees( abs_to_bub( pt ) );
    } );
}

vehicle *map::move_vehicle( vehicle& veh, const tripoint_rel_ms& dp, const tileray& facing )
{
    if( dp == tripoint_rel_ms::zero() ) {
        debugmsg( "Empty displacement vector" );
        return &veh;
    } else if( std::abs( dp.x() ) > 1 || std::abs( dp.y() ) > 1 || std::abs( dp.z() ) > 1 ) {
        debugmsg( "Invalid displacement vector: %d, %d, %d", dp.x(), dp.y(), dp.z() );
        return &veh;
    }
    // Split the movement into horizontal and vertical for easier processing
    if( dp.xy() != point_rel_ms::zero() && dp.z() != 0 ) {
        vehicle* const new_pointer = move_vehicle( veh, tripoint_rel_ms( dp.xy(), 0 ), facing );
        if( !new_pointer ) { return nullptr; }

        vehicle* const result = move_vehicle( *new_pointer, tripoint_rel_ms( 0, 0, dp.z() ), facing );
        if( !result ) { return nullptr; }

        result->is_falling = false;
        return result;
    }
    const bool vertical = dp.z() != 0;
    // Ensured by the splitting above
    assert( vertical == ( dp.xy() == point_rel_ms::zero() ) );

    const int target_z = dp.z() + veh.abs_sm_pos.z();
    if( target_z < -OVERMAP_DEPTH || target_z > OVERMAP_HEIGHT ) { return &veh; }

    veh.precalc_mounts( 1, veh.skidding ? veh.turn_dir : facing.dir(), veh.pivot_point() );

    // cancel out any movement of the vehicle due only to a change in pivot
    // Pivot displacement is a point_rel_veh... Dunno how to convert that
    tripoint_rel_ms dp1 = tripoint_rel_ms( dp - veh.pivot_displacement() );

    if( !vertical ) { veh.adjust_zlevel( 1, dp1 ); }

    int impulse = 0;

    std::vector<veh_collision> collisions;
    std::vector<vehicle *> passthrough;

    // Find collisions
    // Velocity of car before collision
    // Split into vertical and horizontal movement
    const int &coll_velocity = vertical ? veh.vertical_velocity : veh.velocity;
    const int velocity_before = coll_velocity;
    if( velocity_before == 0 && !veh.is_aircraft() && !veh.is_flying_in_air() ) {
        debugmsg( "%s tried to move %s with no velocity", veh.name,
                  vertical ? "vertically" : "horizontally" );
        return &veh;
    }

    bool veh_veh_coll_flag = false;
    // Try to collide multiple times
    size_t collision_attempts = 10;
    do {
        collisions.clear();
        veh.collision( collisions, dp1, false );

        // Vehicle collisions
        std::map<vehicle *, std::vector<veh_collision>> veh_collisions;
        for( auto& coll : collisions ) {
            if( coll.type != veh_coll_veh ) { continue; }

            veh_veh_coll_flag = true;
            // Only collide with each vehicle once
            veh_collisions[static_cast<vehicle *>( coll.target )].push_back( coll );
        }

#ifdef BOX2D_ENABLED
        if( phys_world && !veh_collisions.empty() ) {
            const auto cluster = solve_vv_cluster( veh, veh_collisions );
            // bodies[0] is always &veh; bodies[1..N] are the targets in veh_collisions order.
            for( const auto &body : cluster.bodies ) {
                body.veh->angular_velocity_rads = body.ang_vel_rads;
                const auto &fv = body.final_vel_cmps;
                body.veh->velocity = static_cast<int>(
                                         fv.dot_product( body.veh->face_vec() ) < 0.0f
                                         ? -fv.magnitude()
                                         :  fv.magnitude() );
                body.veh->move.init( point_rel_ms( fv.as_point() ) );
            }
            const auto &veh1 = cluster.bodies[0];
            // Phase 4 limitation: veh1.impulse_ns is the *aggregate* Δp across all contacts.
            // In a 2-vehicle collision this is exact.  In a 3+ pileup it is passed unmodified
            // to each per-partner call, which over-counts damage.  Deferred to Phase 10.
            for( auto &[partner, cols] : veh_collisions ) {
                const auto veh2_it = std::ranges::find_if( cluster.bodies,
                [partner]( const auto & b ) { return b.veh == partner; } );
                if( veh2_it == cluster.bodies.end() ) {
                    impulse += vehicle_vehicle_collision( veh, *partner, cols );
                    continue;
                }
                const auto m1  = to_kilogram( veh.total_mass() );
                const auto m2  = to_kilogram( partner->total_mass() );
                const auto dv  = ( m1 + m2 > 0.0f )
                                 ? veh1.impulse_ns * ( 1.0f / m1 + 1.0f / m2 )
                                 : 0.0f;
                impulse += vehicle_vehicle_collision( veh, *partner, cols, {
                    .veh1_impulse_ns = veh1.impulse_ns,
                    .veh2_impulse_ns = veh2_it->impulse_ns,
                    .delta_vel_mps   = dv
                } );
            }
        } else {
#endif
            for( auto &pair : veh_collisions ) {
                impulse += vehicle_vehicle_collision( veh, *pair.first, pair.second );
            }
#ifdef BOX2D_ENABLED
        }
#endif

        // Non-vehicle collisions
        for( const auto& coll : collisions ) {
            if( coll.type == veh_coll_veh ) { continue; }
            if( coll.type == veh_coll_veh_nocollide ) {
                passthrough.push_back( static_cast<vehicle*>( coll.target ) );
                continue;
            }
            if( coll.part > veh.part_count() || veh.part( coll.part ).removed ) { continue; }

            tripoint_mnt_veh collision_point = veh.part( coll.part ).mount;
            const int coll_dmg = coll.imp;
            // Shock damage, if the target part is a rotor treat as an aimed hit.
            if( veh.part_info( coll.part ).rotor_diameter() > 0 ) {
                veh.damage( coll.part, coll_dmg, DT_BASH, true );
            } else {
                impulse += coll_dmg;
                veh.damage( coll.part, coll_dmg, DT_BASH );
                // Upper bound of shock damage
                int shock_max = coll_dmg;
                // Lower bound of shock damage
                int shock_min = coll_dmg / 2;
                float coll_part_bash_resist = veh.part_info( coll.part ).damage_reduction.type_resist(
                                                  DT_BASH );
                // Reduce shock damage by collision part DR to prevent bushes from damaging car
                // batteries
                shock_min = std::max<int>( 0, shock_min - coll_part_bash_resist );
                shock_max = std::max<int>( 0, shock_max - coll_part_bash_resist );
                // Shock damage decays exponentially, we only want to track shock damage that would
                // cause meaningful damage.
                if( shock_min >= 20 ) {
                    veh.damage_all( shock_min, shock_max, DT_BASH, collision_point );
                }
            }
        }

        // prevent vehicle bouncing after the first collision
        if( vertical && velocity_before < 0 && coll_velocity > 0 ) {
            veh.vertical_velocity = 0; // also affects `coll_velocity` and thus exits the loop
        }

    } while(
        collision_attempts-- > 0 && coll_velocity != 0 && sgn( coll_velocity ) == sgn( velocity_before )
        && !collisions.empty() && !veh_veh_coll_flag );

    const int velocity_after = coll_velocity;
    bool can_move = velocity_after != 0 && sgn( velocity_after ) == sgn( velocity_before );
    if( dp.z() != 0 && veh.is_aircraft() ) { can_move = true; }
    units::angle coll_turn = 0_degrees;
    if( impulse > 0 ) {
        coll_turn = shake_vehicle( veh, velocity_before, facing.dir() );
        veh.stop_autodriving();
        const int volume = std::min<int>( 100, std::sqrt( impulse ) );
        // TODO: Center the sound at weighted (by impulse) average of collisions
        sounds::sound( veh.bub_ms_location(), volume, sounds::sound_t::combat, _( "crash!" ), false,
                       "smash_success", "hit_vehicle" );
    }

    if( veh_veh_coll_flag ) {
        // Break here to let the hit vehicle move away
        return nullptr;
    }

    // If not enough wheels, mess up the ground a bit.
    if( !vertical && !veh.valid_wheel_config() && !veh.is_in_water() && !veh.is_flying_in_air()
        && !veh.has_sufficient_lift( true ) && dp.z() == 0 ) {
        veh.velocity += veh.velocity < 0 ? 2000 : -2000;
        for( const auto& p : veh.get_points() ) {
            const ter_id& pter = ter( abs_to_bub( p ).xy() );
            if( pter == t_dirt || pter == t_grass ) { ter_set( abs_to_bub( p ).xy(), t_dirtmound ); }
        }
    }

    const units::angle last_turn_dec = 1_degrees;
    if( veh.last_turn < 0_degrees ) {
        veh.last_turn += last_turn_dec;
        if( veh.last_turn > -last_turn_dec ) { veh.last_turn = 0_degrees; }
    } else if( veh.last_turn > 0_degrees ) {
        veh.last_turn -= last_turn_dec;
        if( veh.last_turn < last_turn_dec ) { veh.last_turn = 0_degrees; }
    }

    Character& player_character = get_player_character();
    const bool seen = sees_veh( player_character, veh, false );

    if( can_move || ( vertical && veh.is_falling ) ) {
        // Accept new direction
        if( veh.skidding ) {
            veh.face.init( veh.turn_dir );
        } else {
            veh.face = facing;
        }

        veh.move = facing;
        if( coll_turn != 0_degrees ) {
            veh.skidding = true;
            veh.turn( coll_turn );
        }
        veh.on_move();
        // Actually change position
        displace_vehicle( veh, tripoint_rel_ms( dp1 ) );
        veh.shift_zlevel();
    } else if( !vertical ) {
        veh.stop();
    }
    veh.check_falling_or_floating();
    // If the PC is in the currently moved vehicle, adjust the
    //  view offset.
    if( g->u.controlling_vehicle && veh_pointer_or_null( veh_at( g->u.bub_pos() ) ) == &veh ) {
        g->calc_driving_offset( &veh );
        if( veh.skidding && can_move ) {
            // TODO: Make skid recovery in air hard
            veh.possibly_recover_from_skid();
        }
    }
    // Now we're gonna handle traps we're standing on (if we're still moving).
    if( !vertical && can_move ) {
        const auto wheel_indices = veh.wheelcache; // Don't use a reference here, it causes a crash.

        // Values to deal with crushing items.
        // The math needs to be floating-point to work, so the values might as well be.
        const float vehicle_grounded_wheel_area = static_cast<int>(
                vehicle_wheel_traction( veh, true ) );
        const float weight_to_damage_factor = 0.05; // Nobody likes a magic number.
        const float vehicle_mass_kg = to_kilogram( veh.total_mass() );

        for( auto& w : wheel_indices ) {
            const auto wheel_p = veh.bub_part_location( w );
            if( one_in( 2 ) && displace_water( wheel_p ) ) {
                sounds::sound( wheel_p, 4, sounds::sound_t::movement, _( "splash!" ), false,
                               "environment", "splash" );
            }

            veh.handle_trap( wheel_p, w );
            if( !has_flag( "SEALED", wheel_p ) ) {
                const float wheel_area = veh.part( w ).wheel_area();

                // Damage is calculated based on the weight of the vehicle,
                // The area of it's wheels, and the area of the wheel running over the items.
                // This number is multiplied by weight_to_damage_factor to get reasonable results,
                // damage-wise.
                const int wheel_damage = static_cast<int>(
                                             ( ( wheel_area / vehicle_grounded_wheel_area ) * vehicle_mass_kg )
                                             * weight_to_damage_factor );

                //~ %1$s: vehicle name
                smash_items( wheel_p, wheel_damage,
                             string_format( _( "weight of %1$s" ), veh.disp_name() ), false );
            }
        }
    }
    if( veh.is_towing() ) {
        veh.do_towing_move();
        // veh.do_towing_move() may cancel towing, so we need to recheck is_towing here
        if( veh.is_towing() && veh.tow_data.get_towed()->tow_cable_too_far() ) {
            add_msg( m_info, _( "A towing cable snaps off of %s." ),
                     veh.tow_data.get_towed()->disp_name() );
            veh.tow_data.get_towed()->invalidate_towing( true );
        }
    }
    for( vehicle * colveh : passthrough ) { g->m.add_vehicle_to_cache( colveh ); }
    // Redraw scene, but only if the player is not engaged in an activity and
    // the vehicle was seen before or after the move.
    if( !player_character.activity && ( seen || sees_veh( player_character, veh, true ) ) ) {
        g->invalidate_main_ui_adaptor();
        inp_mngr.pump_events();
        ui_manager::redraw_invalidated();
        refresh_display();
    }
    return &veh;
}

auto map::vehicle_vehicle_collision(
    vehicle &veh, vehicle &veh2, const std::vector<veh_collision> &collisions,
    const veh_veh_coll_opts &opts ) -> float
{
    if( &veh == &veh2 ) {
        debugmsg( "Vehicle %s collided with itself", veh.name );
        return 0.0f;
    }

    // Effects of colliding with another vehicle:
    //  transfers of momentum, skidding,
    //  parts are damaged/broken on both sides,
    //  remaining times are normalized
    const veh_collision& c = collisions[0];
    add_msg( m_bad, _( "The %1$s's %2$s collides with %3$s's %4$s." ), veh.name,
             veh.part_info( c.part ).name(), veh2.name, veh2.part_info( c.target_part ).name() );

    const bool vertical = veh.abs_sm_pos.z() != veh2.abs_sm_pos.z();

    // Used to calculate the epicenter of the collision.
    tripoint_rel_veh epicenter1;
    tripoint_rel_veh epicenter2;

    float veh1_impulse = 0;
    float veh2_impulse = 0;
    float delta_vel = 0;
    // A constant to tune how many Ns of impulse are equivalent to 1 point of damage, look in
    // vehicle_move.cpp for the impulse to damage function.
    const float dmg_adjust = impulse_to_damage( 1 );
    float dmg_veh1 = 0;
    float dmg_veh2 = 0;
    // Vertical collisions will be simpler for a while (1D)
    if( !vertical ) {
#ifdef BOX2D_ENABLED
        if( opts.veh1_impulse_ns != 0.0f ) {
            // Box2D dispatch already applied final velocities, move direction, and
            // angular_velocity_rads.  Populate impulse/delta_vel for the damage section.
            veh1_impulse = opts.veh1_impulse_ns;
            veh2_impulse = opts.veh2_impulse_ns;
            delta_vel    = opts.delta_vel_mps;
            const auto avg = std::max( 0.1f, ( veh2.of_turn + veh.of_turn ) / 2.0f );
            veh.of_turn  = avg * 0.9f;
            veh2.of_turn = std::max( 1.0f, avg * 1.1f );
        } else {
#endif
            // For reference, a cargo truck weighs ~25300, a bicycle 690,
            //  and 38mph is 3800 'velocity'
            // Converting away from 100*mph, because mixing unit systems is bad.
            // 1 mph = 0.44704m/s = 100 "velocity". For velocity to m/s, *0.0044704
            rl_vec2d velo_veh1 = veh.velo_vec();
            rl_vec2d velo_veh2 = veh2.velo_vec();
            const float m1 = to_kilogram( veh.total_mass() );
            const float m2 = to_kilogram( veh2.total_mass() );

            // Collision_axis
            tripoint_mnt_veh cof1 = veh.rotated_center_of_mass();
            tripoint_mnt_veh cof2 = veh2.rotated_center_of_mass();
            int &x_cof1 = cof1.x();
            int &y_cof1 = cof1.y();
            int &x_cof2 = cof2.x();
            int &y_cof2 = cof2.y();
            rl_vec2d collision_axis_y;

            collision_axis_y.x =
                ( veh.bub_ms_location().x() + x_cof1 ) - ( veh2.bub_ms_location().x() + x_cof2 );
            collision_axis_y.y =
                ( veh.bub_ms_location().y() + y_cof1 ) - ( veh2.bub_ms_location().y() + y_cof2 );
            collision_axis_y = collision_axis_y.normalized();
            rl_vec2d collision_axis_x = collision_axis_y.rotated( M_PI / 2 );
            // imp? & delta? & final? reworked:
            // newvel1 =( vel1 * ( mass1 - mass2 ) + ( 2 * mass2 * vel2 ) ) / ( mass1 + mass2 )
            // as per http://en.wikipedia.org/wiki/Elastic_collision
            float vel1_y = cmps_to_mps( collision_axis_y.dot_product( velo_veh1 ) );
            float vel1_x = cmps_to_mps( collision_axis_x.dot_product( velo_veh1 ) );
            float vel2_y = cmps_to_mps( collision_axis_y.dot_product( velo_veh2 ) );
            float vel2_x = cmps_to_mps( collision_axis_x.dot_product( velo_veh2 ) );
            delta_vel = std::abs( vel1_y - vel2_y );
            // Keep in mind get_collision_factor is looking for m/s, not m/h.
            // e = 0 -> inelastic collision
            // e = 1 -> elastic collision
            float e = get_collision_factor( vel1_y - vel2_y );
            add_msg( m_debug, "Requested collision factor, received %.2f", e );

            // Velocity after collision
            // vel1_x_a = vel1_x, because in x-direction we have no transmission of force
            float vel1_x_a = vel1_x;
            float vel2_x_a = vel2_x;
            // Transmission of force only in direction of collision_axix_y
            // Equation: partially elastic collision
            float vel1_y_a = ( ( m2 * vel2_y * ( 1 + e ) + vel1_y * ( m1 - m2 * e ) ) / ( m1 + m2 ) );
            float vel2_y_a = ( ( m1 * vel1_y * ( 1 + e ) + vel2_y * ( m2 - m1 * e ) ) / ( m1 + m2 ) );
            // Add both components; Note: collision_axis is normalized
            rl_vec2d final1 = ( collision_axis_y * vel1_y_a + collision_axis_x * vel1_x_a ) * 100.0;
            rl_vec2d final2 = ( collision_axis_y * vel2_y_a + collision_axis_x * vel2_x_a ) * 100.0;

            veh.move.init( point_rel_ms( final1.as_point() ) );
            if( final1.dot_product( veh.face_vec() ) < 0 ) {
                // Car is being pushed backwards. Make it move backwards
                veh.velocity = -final1.magnitude();
            } else {
                veh.velocity = final1.magnitude();
            }

            veh2.move.init( point_rel_ms( final2.as_point() ) );
            if( final2.dot_product( veh2.face_vec() ) < 0 ) {
                // Car is being pushed backwards. Make it move backwards
                veh2.velocity = -final2.magnitude();
            } else {
                veh2.velocity = final2.magnitude();
            }

            // give veh2 the initiative to proceed next before veh1
            float avg_of_turn = ( veh2.of_turn + veh.of_turn ) / 2;
            if( avg_of_turn < .1f ) { avg_of_turn = .1f; }

            veh.of_turn = avg_of_turn * .9;
            // Clamp veh2's of_turn to at least 1.0 so the priority-queue scheduler
            // in vehmove() enqueues the hit vehicle for movement this turn.
            // Without this the V-2 pq threshold (>= 1.0) would never be reached by
            // avg * 1.1 alone, leaving the hit vehicle stuck and causing veh1 to
            // repeatedly ram it every subsequent turn (BUG-1 follow-up).
            veh2.of_turn = std::max( 1.0f, avg_of_turn * 1.1f );

            // Remember that the impulse on vehicle 1 is techncally negative, slowing it
            veh1_impulse = std::abs( m1 * ( vel1_y_a - vel1_y ) );
            veh2_impulse = std::abs( m2 * ( vel2_y_a - vel2_y ) );
#ifdef BOX2D_ENABLED
        } // end: analytic elastic formula (BOX2D_ENABLED else)
#endif
    } else {
        const float m1 = to_kilogram( veh.total_mass() );
        // Collision is perfectly inelastic for simplicity
        // Assume veh2 is standing still
        dmg_veh1 = ( std::abs( cmps_to_mps( veh.vertical_velocity ) ) * ( m1 / 10 ) ) / 2;
        dmg_veh2 = dmg_veh1;
        veh.vertical_velocity = 0;
    }

    // To facilitate pushing vehicles, because the simulation pretends cars are ping pong balls that
    // get all their velocity in zero starting distance to slam into eachother while touching. Stay
    // under 6 m/s to push cars without damaging them
    if( delta_vel >= 6.0f ) {
        dmg_veh1 = veh1_impulse * dmg_adjust;
        dmg_veh2 = veh2_impulse * dmg_adjust;
    } else {
        dmg_veh1 = 0;
        dmg_veh2 = 0;
    }


    int coll_parts_cnt = 0; // quantity of colliding parts between veh1 and veh2
    for( const auto& veh_veh_coll : collisions ) {
        if( &veh2 == static_cast<vehicle * >( veh_veh_coll.target ) ) { coll_parts_cnt++; }
    }

    const float dmg1_part = dmg_veh1 / coll_parts_cnt;
    const float dmg2_part = dmg_veh2 / coll_parts_cnt;

    // damage colliding parts (only veh1 and veh2 parts)
    for( const auto& veh_veh_coll : collisions ) {
        if( &veh2 != static_cast<vehicle * >( veh_veh_coll.target ) ) { continue; }

        int parm1 = veh.part_with_feature( veh_veh_coll.part, VPFLAG_ARMOR, true );
        if( parm1 < 0 ) { parm1 = veh_veh_coll.part; }
        int parm2 = veh2.part_with_feature( veh_veh_coll.target_part, VPFLAG_ARMOR, true );
        if( parm2 < 0 ) { parm2 = veh_veh_coll.target_part; }

        // NOTE: This should just be add
        // But for some reason you cant add mnt_veh to rel_veh?
        epicenter1 += veh.part( parm1 ).mount.raw();
        veh.damage( parm1, dmg1_part, DT_BASH );

        epicenter2 += veh2.part( parm2 ).mount.raw();
        veh2.damage( parm2, dmg2_part, DT_BASH );
    }

    epicenter2.x() /= coll_parts_cnt;
    epicenter2.y() /= coll_parts_cnt;

    if( dmg2_part > 100 ) {
        // Shake vehicle because of collision
        // FIXME: I dunno how else to do this, it comes out to a mount but is relative in the
        // meantime
        veh2.damage_all( dmg2_part / 2, dmg2_part, DT_BASH, tripoint_mnt_veh( epicenter2.raw() ) );
    }

    if( dmg_veh1 > 800 ) { veh.skidding = true; }

    if( dmg_veh2 > 800 ) { veh2.skidding = true; }

    // Return the impulse of the collision
    return dmg_veh1;
}

bool map::check_vehicle_zones( const int zlev )
{
    for( auto veh : get_cache( zlev ).zone_vehicles ) {
        if( veh->zones_dirty ) { return true; }
    }
    return false;
}

std::vector<zone_data *> map::get_vehicle_zones( const int zlev )
{
    std::vector<zone_data *> veh_zones;
    bool rebuild = false;
    for( auto veh : get_cache( zlev ).zone_vehicles ) {
        if( veh->refresh_zones() ) { rebuild = true; }
        for( auto& zone : veh->loot_zones ) { veh_zones.emplace_back( &zone.second ); }
    }
    if( rebuild ) { zone_manager::get_manager().cache_vzones(); }
    return veh_zones;
}

void map::register_vehicle_zone( vehicle* veh, const int zlev )
{
    auto& ch = get_cache( zlev );
    ch.zone_vehicles.insert( veh );
}

bool map::deregister_vehicle_zone( zone_data &zone )
{
    if( const std::optional<vpart_reference> vp = veh_at( abs_to_bub(
            tripoint_abs_ms( zone.get_start_point() ) ) ).part_with_feature( "CARGO", false ) ) {
        const auto bounds = vp->vehicle().loot_zones.equal_range( vp->mount() );
        const auto it = std::ranges::find_if( std::ranges::subrange( bounds.first, bounds.second ),
        [&zone]( const auto & entry ) {
            return &zone == &entry.second;
        } );
        if( it != bounds.second ) {
            vp->vehicle().loot_zones.erase( it );
            if( vp->vehicle().loot_zones.empty() ) {
                get_cache( vp->vehicle().abs_sm_pos.z() ).zone_vehicles.erase( &vp->vehicle() );
            }
            return true;
        }
    }
    return false;
}

// 3D vehicle functions

VehicleList map::get_vehicles( const tripoint_bub_sm& start, const tripoint_bub_sm& end )
{
    auto chunk_start = start;
    clip_to_bounds( chunk_start );
    auto chunk_end = end;
    clip_to_bounds( chunk_end );
    auto vehs = VehicleList{};

    if( chunk_start.x() > chunk_end.x() || chunk_start.y() > chunk_end.y()
        || chunk_start.z() > chunk_end.z() ) {
        return vehs;
    }

    for( const auto sm_pos : tripoint_range<tripoint_bub_sm>( chunk_start, chunk_end ) ) {
        auto* current_submap = get_submap_at_grid( sm_pos );
        if( current_submap == nullptr ) { continue; }
        for( const auto& elem : current_submap->vehicles ) {
            auto w = wrapped_vehicle{};
            w.v = elem.get();
            w.pos = w.v->bub_ms_location();
            vehs.push_back( w );
        }
    }

    return vehs;
}

optional_vpart_position map::veh_at( const tripoint_abs_ms& p ) const
{
    return veh_at( abs_to_bub( p ) );
}

optional_vpart_position map::veh_at( const tripoint_bub_ms &p ) const
{
    if( !inbounds( p ) || !const_cast<map *>( this )->get_cache( p.z() ).veh_in_active_range ) {
    return optional_vpart_position( std::nullopt );
    }

    auto part_num = 1;
    auto *const veh = const_cast<map *>( this )->veh_at_internal( p, part_num );
    if( !veh ) {
    return optional_vpart_position( std::nullopt );
    }
    return optional_vpart_position( vpart_position( *veh, part_num ) );
}

const vehicle *map::veh_at_internal( const tripoint_bub_ms& p, int &part_num ) const
{
    // This function is called A LOT. Move as much out of here as possible.
    const level_cache& ch = get_cache( p.z() );
    if( !ch.veh_in_active_range || !ch.veh_exists_at[ch.idx( p.x(), p.y() )] ) {
        part_num = -1;
        return nullptr; // Clear cache indicates no vehicle. This should optimize a great deal.
    }

    const auto it = ch.veh_cached_parts.find( p );
    if( it != ch.veh_cached_parts.end() ) {
        part_num = it->second.second;
        return it->second.first;
    }

    debugmsg( "vehicle part cache indicated vehicle not found: %d %d %d", p.x(), p.y(), p.z() );
    part_num = -1;
    return nullptr;
}

vehicle *map::veh_at_internal( const tripoint_bub_ms& p, int &part_num )
{
    return const_cast<vehicle *>( const_cast<const map*>( this )->veh_at_internal( p, part_num ) );
}

void map::board_vehicle( const tripoint_bub_ms& pos, Character* who )
{
    if( who == nullptr ) {
        debugmsg( "map::board_vehicle: null player" );
        return;
    }

    auto vp = veh_at( pos ).part_with_feature( VPFLAG_BOARDABLE, true );
    if( !vp ) {
        const auto abs_pos = bub_to_abs( pos );
        for( auto * veh : loaded_vehicles ) {
            if( veh == nullptr ) { continue; }
            auto boardable_parts = veh->get_avail_parts( VPFLAG_BOARDABLE );
            const auto part_it =
            std::ranges::find_if( boardable_parts, [&]( const vpart_reference & part ) {
                return veh->abs_part_location( part.part() ) == abs_pos;
            } );
            if( part_it == boardable_parts.end() ) { continue; }
            vp = *part_it;
            break;
        }
    }
    if( !vp ) {
        if( who->grab_point.x() == 0 && who->grab_point.y() == 0 ) {
            debugmsg( "map::board_vehicle: vehicle not found" );
        }
        return;
    }
    if( vp->part().has_flag( vehicle_part::passenger_flag ) ) {
        player* psg = vp->vehicle().get_passenger( vp->part_index() );
        debugmsg( "map::board_vehicle: passenger (%s) is already there", psg ? psg->name : "<null>" );
        unboard_vehicle( pos );
    }
    vp->part().set_flag( vehicle_part::passenger_flag );
    vp->part().passenger_id = who->getID();
    vp->vehicle().invalidate_mass();

    who->setpos( pos );
    who->in_vehicle = true;
    if( who->is_avatar() ) { g->update_map( g->u ); }
}

void map::unboard_vehicle( const vpart_reference& vp, Character* passenger, bool dead_passenger )
{
    // Mark the part as un-occupied regardless of whether there's a live passenger here.
    vp.part().remove_flag( vehicle_part::passenger_flag );
    vp.vehicle().invalidate_mass();

    if( !passenger ) {
        if( !dead_passenger ) { debugmsg( "map::unboard_vehicle: passenger not found" ); }
        return;
    }
    passenger->in_vehicle = false;
    // Only make vehicle go out of control if the driver is the one unboarding.
    if( passenger->controlling_vehicle ) { vp.vehicle().skidding = true; }
    passenger->controlling_vehicle = false;
}

void map::unboard_vehicle( const tripoint_bub_ms& p, bool dead_passenger )
{
    const std::optional<vpart_reference> vp = veh_at( p ).part_with_feature( VPFLAG_BOARDABLE, false );
    player* passenger = nullptr;
    if( !vp ) {
        debugmsg( "map::unboard_vehicle: vehicle not found" );
        // Try and force unboard the player anyway.
        passenger = g->critter_at<player>( p );
        if( passenger ) {
            passenger->in_vehicle = false;
            passenger->controlling_vehicle = false;
        }
        return;
    }
    passenger = vp->get_passenger();
    unboard_vehicle( *vp, passenger, dead_passenger );
}

bool map::displace_vehicle( vehicle& veh, const tripoint_rel_ms& dp )
{
    const auto src = veh.abs_ms_location();
    const auto dest = src + dp;
    const auto dest_proj = project_remain<coords::sm>( dest );
    const bool sm_shift = veh.abs_sm_pos != dest_proj.quotient_tripoint;

    submap* const src_submap =
        sm_shift ? MAPBUFFER_REGISTRY.get( bound_dimension_ ).lookup_submap_in_memory( veh.abs_sm_pos )
        : nullptr;
    submap* const dst_submap =
        sm_shift
        ? MAPBUFFER_REGISTRY.get( bound_dimension_ )
        .lookup_submap_in_memory( dest_proj.quotient_tripoint )
        : nullptr;

    std::set<int> smzs;
    size_t our_i = 0;

    if( sm_shift ) {
        if( src_submap == nullptr ) {
            debugmsg( "displace_vehicle: src submap null for '%s' at %d,%d,%d", veh.name, src.x(),
                      src.y(), src.z() );
            return false;
        }

        // Find the vehicle's index directly in its authoritative source submap.
        // get_submap_at() handles out-of-bubble positions via the mapbuffer fallback,
        // so this works for vehicles loaded outside the reality bubble.
        bool found = false;
        for( size_t i = 0; i < src_submap->vehicles.size(); ++i ) {
            if( src_submap->vehicles[i].get() == &veh ) {
                our_i = i;
                found = true;
                break;
            }
        }

        if( !found ) {
            add_msg( m_debug, "displace_vehicle [%s] failed", veh.name );
            return false;
        }

        // Stop the vehicle if its destination submap is not loaded.
        // Safety net for cases where act_on_map consumed movement before collision fired.
        if( dst_submap == nullptr ) {
            veh.stop();
            dbg( DL::Error ) << "map::displace_vehicle: dst submap not loaded, stopping vehicle dp="
                             << dp;
            return true;
        }
    }

    // Need old coordinates to check for remote control
    const bool remote = veh.remote_controlled( g->u );

    // record every passenger and pet inside
    std::vector<rider_data> riders = veh.get_riders();

    bool need_update = false;
    bool z_change = false;
    int z_to = 0;
    // Move passengers and pets
    bool complete = false;
    // loop until everyone has moved or for each passenger
    for( size_t i = 0; !complete && i < riders.size(); i++ ) {
        complete = true;
        for( rider_data& r : riders ) {
            if( r.moved ) { continue; }
            const int prt = r.prt;

            Creature* psg = r.psg;
            const tripoint_bub_ms part_pos = veh.bub_part_location( prt );
            if( psg == nullptr ) {
                debugmsg( "Empty passenger for part #%d at %d,%d,%d player at %d,%d,%d?", prt,
                          part_pos.x(), part_pos.y(), part_pos.z(), g->u.bub_pos().x(),
                          g->u.bub_pos().y(), g->u.bub_pos().z() );
                veh.part( prt ).remove_flag( vehicle_part::passenger_flag );
                r.moved = true;
                continue;
            }

            if( psg->bub_pos() != part_pos ) {
                add_msg(
                    m_debug,
                    "Part/passenger position mismatch: part #%d at %d,%d,%d "
                    "passenger at %d,%d,%d",
                    prt, part_pos.x(), part_pos.y(), part_pos.z(), psg->bub_pos().x(),
                    psg->bub_pos().y(), psg->bub_pos().z() );
            }
            const vehicle_part& veh_part = veh.part( prt );

            // Place passenger on the new part location.  Z must include mount
            // and terrain-topology offsets — precalc[1] is XY-only.
            auto psgp = abs_to_bub(
                            dest
                            + tripoint_rel_ms( veh_part.precalc[1].x(), veh_part.precalc[1].y(),
                                               veh_part.mount.z() + veh_part.z_terrain[1] ) );
            // someone is in the way so try again
            if( g->critter_at( psgp ) ) {
                complete = false;
                continue;
            }
            if( psg->is_avatar() ) {
                // If passenger is you, we need to update the map
                need_update = true;
                z_change = psgp.z() != part_pos.z();
                z_to = psgp.z();
            }

            psg->setpos( psgp );
            r.moved = true;
        }
    }

    // Capture the old footprint in submap grid coordinates BEFORE parts are
    // updated by advance_precalc_mounts.
    tripoint_bub_sm veh_sm_min = {INT_MAX, INT_MAX, INT_MAX};
    tripoint_bub_sm veh_sm_max = {INT_MIN, INT_MIN, INT_MIN};

    auto expand_bounds = [&]( const tripoint_abs_ms & base, const vehicle_part & prt ) {
        const auto p = abs_to_bub( project_to<coords::sm>(
                                       base + tripoint_rel_ms( prt.precalc[0], prt.mount.z() + prt.z_terrain[0] ) ) );
        veh_sm_min.x() = std::min( veh_sm_min.x(), p.x() );
        veh_sm_min.y() = std::min( veh_sm_min.y(), p.y() );
        veh_sm_min.z() = std::min( veh_sm_min.z(), p.z() );
        veh_sm_max.x() = std::max( veh_sm_max.x(), p.x() );
        veh_sm_max.y() = std::max( veh_sm_max.y(), p.y() );
        veh_sm_max.z() = std::max( veh_sm_max.z(), p.z() );
    };

    for( const vpart_reference& vpr : veh.get_all_parts() ) {
        if( !vpr.part().removed ) { expand_bounds( src, vpr.part() ); }
    }

    veh.shed_loose_parts();

    // Clear overlay map memory (vpart only) for every tile the vehicle is vacating.
    // precalc[0] and z_terrain[0] still hold the OLD offsets here (before
    // advance_precalc_mounts), so this gives the authoritative absolute tile
    // positions being left.
    // Terrain memory is preserved so the ground beneath doesn't go black.
    {
        avatar& you = get_avatar();
        const auto clear_matching_overlay =
        [&]( const tripoint_abs_ms & pos, const std::string & tile_id ) {
            if( you.get_memorized_tile( pos ).tile == tile_id ) {
                you.clear_memorized_overlay( pos );
            }
        };

        for( const auto& vpr : veh.get_all_parts() ) {
            if( !vpr.part().removed ) {
                const auto& part = vpr.part();
                const auto part_offset =
                    tripoint_rel_ms( part.precalc[0], part.mount.z() + part.z_terrain[0] );
                you.clear_memorized_overlay( src + part_offset );

                const auto& part_info = part.info();
                if( part_info.has_flag( VPFLAG_LADDER ) ) {
                    const auto ladder_pos = src + part_offset;
                    const auto rope_tile = "vp_" + part_info.get_id().str();
                    const auto min_rope_z =
                        std::max( ladder_pos.z() - part_info.ladder_length(), -OVERMAP_DEPTH );
                    for( const auto z : std::views::iota( min_rope_z, ladder_pos.z() ) ) {
                        auto rope_pos = ladder_pos;
                        rope_pos.z() = z;
                        clear_matching_overlay( rope_pos, rope_tile );
                    }
                }
            }
        }
    }

    smzs = veh.advance_precalc_mounts( src );
    veh.sm_ms_pos = dest_proj.remainder;

    // Expand bounds with the new footprint (precalc[0] now holds new offsets).
    for( const vpart_reference& vpr : veh.get_all_parts() ) {
        if( !vpr.part().removed ) { expand_bounds( dest, vpr.part() ); }
    }

    if( sm_shift && src_submap != dst_submap ) {
        auto src_submap_veh_it = src_submap->vehicles.begin() + our_i;
        dst_submap->vehicles.push_back( std::move( *src_submap_veh_it ) );
        src_submap->vehicles.erase( src_submap_veh_it );
        dst_submap->is_uniform = false;
        invalidate_max_populated_zlev( dest.z() );

        // Update abs_sm_pos for the submap boundary crossing.
        const auto prev = veh.abs_sm_pos;
        veh.abs_sm_pos = dest_proj.quotient_tripoint;
        veh.update_overmap( prev );
    }

    if( need_update ) { g->update_map( g->u ); }
    add_vehicle_to_cache( &veh );

    if( z_change || src.z() != dest.z() ) {
        if( z_change ) {
            g->vertical_shift( z_to );
            // vertical moves can flush the caches, so make sure we're still in the cache
            add_vehicle_to_cache( &veh );
        }
        update_vehicle_list( dst_submap, dest.z() );
        // delete the vehicle from the source z-level vehicle cache set if it is no longer on
        // that z-level
        if( src.z() != dest.z() ) {
            level_cache& ch2 = get_cache( src.z() );
            for( const vehicle * elem : ch2.vehicle_list ) {
                if( elem == &veh ) {
                    ch2.vehicle_list.erase( &veh );
                    ch2.zone_vehicles.erase( &veh );
                    break;
                }
            }
        }
        veh.check_is_heli_landed();
    }
    if( veh.is_flying_in_air() ) { veh.check_is_heli_landed(); }
    if( remote ) {
        // Has to be after update_map or coordinates won't be valid
        g->setremoteveh( &veh );
    }

    //
    // global positions of vehicle loot zones have changed.
    veh.zones_dirty = true;

    std::ranges::for_each( smzs, [&]( const int vsmz ) {
        on_vehicle_moved( veh_sm_min, veh_sm_max, dest.z() + vsmz );
    } );
#ifdef BOX2D_ENABLED
    if( phys_world ) { phys_world->on_vehicle_moved( veh ); }
#endif
    return true;
}
#ifdef BOX2D_ENABLED
auto map::resolve_vehicle_terrain_impulse( vehicle &v, tripoint_bub_ms tile_pos,
        float tile_mass_kg, float restitution )
-> physics::terrain_impulse_result
{
    if( !phys_world ) { return {}; }
return phys_world->resolve_terrain_impulse( v, tile_pos, tile_mass_kg, restitution );
}
#endif

#ifdef BOX2D_ENABLED
auto map::get_physics_world() const -> physics::PhysicsWorld *
{
    return phys_world.get();
}
#endif
