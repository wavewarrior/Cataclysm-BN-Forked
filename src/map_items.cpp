#include "map.h"
#include "coop_mutation_log.h"

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

// for an OOB value


// Thread-local context for get_map().  Null means "use the global g->m."
// Worker threads never push a context, so they always fall through to g->m.




// Map stack methods.



// Map class methods.


static location_vector<item> nulitems( new fake_item_location() );

map_stack map::i_at( const tripoint_bub_ms& p )
{
    point_sm_ms l;
    submap* const current_submap = get_submap_at( p, l );
    if( current_submap == nullptr ) {
        nulitems.clear();
        return map_stack{&nulitems, p, this};
    }

    return map_stack{&current_submap->get_items( l ), p, this};
}

map_stack::iterator map::i_rem(
    const tripoint_bub_ms& p, map_stack::const_iterator it, detached_ptr<item> *out )
{
    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );

    // remove from the active items cache (if it isn't there does nothing)
    current_submap->active_items.remove( *it );
    if( current_submap->active_items.empty() ) {
        submaps_with_active_items.erase( project_to<coords::sm>( bub_to_abs( p ) ) );
    }

    current_submap->update_lum_rem( l, **it );

    return current_submap->get_items( l ).erase( std::move( it ), out );
}

detached_ptr<item> map::i_rem( const tripoint_bub_ms& p, item* it )
{
    map_stack map_items = i_at( p );
    detached_ptr<item> res;
    map_items.remove_top_items_with( [&res, it]( detached_ptr<item>&& e ) {
        if( &*e == it ) {
            res = std::move( e );
            return detached_ptr<item>();
        }
        return std::move( e );
    } );
    return res;
}

std::vector<detached_ptr<item>> map::i_clear( const tripoint_bub_ms& p )
{
    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );

    for( item * const& it : current_submap->get_items( l ) ) {
        // remove from the active items cache (if it isn't there does nothing)
        current_submap->active_items.remove( it );
    }
    if( current_submap->active_items.empty() ) {
        submaps_with_active_items.erase( project_to<coords::sm>( bub_to_abs( p ) ) );
    }

    current_submap->set_lum( l, 0 );
    return current_submap->get_items( l ).clear();
}

detached_ptr<item> map::spawn_an_item(
    const tripoint_bub_ms& p, detached_ptr<item>&& new_item, const int charges,
    const int damlevel )
{
    if( one_in( 3 ) && new_item->has_flag( flag_VARSIZE ) ) { new_item->set_flag( flag_FIT ); }

    if( charges && new_item->charges > 0 ) {
        // let's fail silently if we specify charges for an item that doesn't support it
        new_item->charges = charges;
    }
    detached_ptr<item> spawned_item = item::in_its_container( std::move( new_item ) );
    if( ( spawned_item->made_of( LIQUID ) && has_flag( "SWIMMABLE", p ) )
        || has_flag( "DESTROY_ITEM", p ) ) {
        return detached_ptr<item>();
    }

    spawned_item->set_damage( damlevel );

    return add_item_or_charges( p, std::move( spawned_item ) );
}

float map::item_category_spawn_rate( const item& itm )
{
    const std::string& cat = itm.get_category().id.c_str();
    float spawn_rate = get_option<float>( "SPAWN_RATE_" + cat );

    // strictly search for canned foods only in the first check
    if( itm.goes_bad_after_opening( true ) ) {
        float spawn_rate_mod = get_option<float>( "SPAWN_RATE_perishables_canned" );
        spawn_rate *= spawn_rate_mod;
    } else if( itm.goes_bad() ) {
        float spawn_rate_mod = get_option<float>( "SPAWN_RATE_perishables" );
        spawn_rate *= spawn_rate_mod;
    }

    return spawn_rate > 1.0f ? roll_remainder( spawn_rate ) : spawn_rate;
}

std::vector<detached_ptr<item>> map::spawn_items( const tripoint_bub_ms &p,
        std::vector<detached_ptr<item>> new_items )
{
    std::vector<detached_ptr<item>> ret;
    if( has_flag( "DESTROY_ITEM", p ) ) { return ret; }
    const bool swimmable = has_flag( "SWIMMABLE", p );
    for( detached_ptr<item> &new_item : new_items ) {
        if( new_item->made_of( LIQUID ) && swimmable ) { continue; }
        new_item = add_item_or_charges( p, std::move( new_item ) );
        if( new_item ) { ret.push_back( std::move( new_item ) ); }
    }

    return ret;
}

void map::spawn_artifact( const tripoint_bub_ms& p )
{
    add_item_or_charges( p, item::spawn( new_artifact(), calendar::start_of_cataclysm ) );
}

void map::spawn_natural_artifact( const tripoint_bub_ms& p, artifact_natural_property prop )
{
    add_item_or_charges( p, item::spawn( new_natural_artifact( prop ), calendar::start_of_cataclysm ) );
}

void map::spawn_item(
    const tripoint_bub_ms& p, const itype_id& type_id, const unsigned quantity, const int charges,
    const time_point& birthday, const int damlevel )
{
    if( type_id.is_null() ) { return; }

    if( item_is_blacklisted( type_id ) ) { return; }

    // Skip spawning items in dimension-bounded out-of-bounds areas
    if( is_out_of_bounds( tripoint_bub_ms( p ) ) ) { return; }

    for( size_t i = 0; i < quantity; i++ ) {
        // spawn the item
        detached_ptr<item> new_item = item::spawn( type_id, birthday );

        spawn_an_item( p, std::move( new_item ), charges, damlevel );
    }
}

units::volume map::max_volume( const tripoint_bub_ms& p ) { return i_at( p ).max_volume(); }

// total volume of all the things
units::volume map::stored_volume( const tripoint_bub_ms& p ) { return i_at( p ).stored_volume(); }

// free space
units::volume map::free_volume( const tripoint_bub_ms& p ) { return i_at( p ).free_volume(); }

detached_ptr<item> map::add_item_or_charges(
    const tripoint_bub_ms& pos, detached_ptr<item>&& obj, bool overflow )
{
    if( !obj ) { return std::move( obj ); }
    if( obj->is_null() ) {
        debugmsg( "Tried to add a null item to the map" );
        return std::move( obj );
    }

    // Checks if item would not be destroyed if added to this tile
    auto valid_tile = [&]( const tripoint_bub_ms & e ) {
        // Cannot add items to dimension-bounded out-of-bounds areas or unloaded submaps
        if( is_out_of_bounds( e ) ) { return false; }

        // Some tiles destroy items (e.g. lava)
        if( has_flag( "DESTROY_ITEM", e ) ) { return false; }

        // Cannot drop liquids into tiles that are comprised of liquid
        if( obj->made_of( LIQUID ) && has_flag( "SWIMMABLE", e ) ) { return false; }

        return true;
    };

    // Checks if sufficient space at tile to add item
    auto valid_limits = [&]( const tripoint_bub_ms & e ) {
        return obj->volume() <= free_volume( e ) && i_at( e ).size() < MAX_ITEM_IN_SQUARE;
    };

    // Performs the actual insertion of the object onto the map
    auto place_item = [&]( const tripoint_bub_ms & tile ) {
        if( obj->count_by_charges() ) {
            for( auto& e : i_at( tile ) ) {
                // NOLINTNEXTLINE(bugprone-use-after-move)
                if( e->merge_charges( std::move( obj ) ) ) { return; }
            }
        }

        support_dirty( tile );
        add_item( tile, std::move( obj ) );
    };

    // Some items never exist on map as a discrete item (must be contained by another item)
    if( obj->has_flag( flag_NO_DROP ) ) { return std::move( obj ); }

    // If intended drop tile destroys the item then we don't attempt to overflow
    if( !valid_tile( pos ) ) { return std::move( obj ); }

    if( ( !has_flag( "NOITEM", pos ) || ( has_flag( "LIQUIDCONT", pos ) && obj->made_of( LIQUID ) ) )
        && valid_limits( pos ) ) {
        // Pass map into on_drop, because this map may not be the global map object (in mapgen, for
        // instance).
        if( obj->made_of( LIQUID ) || !obj->has_flag( flag_DROP_ACTION_ONLY_IF_LIQUID ) ) {
            if( obj->on_drop( pos, *this ) ) { return std::move( obj ); }
        }
        // If tile can contain items place here...
        place_item( pos );
        return detached_ptr<item>();

    } else if( overflow ) {
        // ...otherwise try to overflow to adjacent tiles (if permitted)
        const int max_dist = 2;
        std::vector<tripoint_bub_ms> tiles = closest_points_first( pos, max_dist );
        tiles.erase( tiles.begin() ); // we already tried this position
        const int max_path_length = 4 * max_dist;
        const pathfinding_settings
        setting( 0, max_dist, max_path_length, 0, false, true, false, false, false );
        for( const auto& e : tiles ) {
            if( is_out_of_bounds( e ) ) { continue; }
            // must be a path to the target tile
            if( route( pos, e, setting ).empty() ) { continue; }
            if( obj->made_of( LIQUID ) || !obj->has_flag( flag_DROP_ACTION_ONLY_IF_LIQUID ) ) {
                if( obj->on_drop( e, *this ) ) { return std::move( obj ); }
            }

            if( !valid_tile( e ) || !valid_limits( e ) || has_flag( "NOITEM", e )
                || has_flag( "SEALED", e ) ) {
                continue;
            }
            place_item( e );
            return detached_ptr<item>();
        }
    }

    // failed due to lack of space at target tile (+/- overflow tiles)
    return std::move( obj );
}

void map::add_item( const tripoint_bub_ms& p, detached_ptr<item>&& new_item )
{
    if( !new_item ) { return; }
    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );
    if( current_submap == nullptr ) { return; }

    // Process foods when they are added to the map, here instead of add_item_at()
    // to avoid double processing food and corpses during active item processing.
    if( new_item->is_food() ) {
        new_item = item::process( std::move( new_item ), nullptr, p, false );
        if( !new_item ) { return; }
    }

    if( new_item->made_of( LIQUID ) && has_flag( "SWIMMABLE", p ) ) { return; }

    if( has_flag( "DESTROY_ITEM", p ) ) { return; }

    if( new_item->has_flag( flag_ACT_IN_FIRE ) && get_field( p, fd_fire ) != nullptr ) {
        if( new_item->has_flag( flag_BOMB ) && new_item->is_transformable() ) {
            // Convert a bomb item into its transformable version, e.g. incendiary grenade -> active
            // incendiary grenade
            new_item->convert(
                dynamic_cast<const iuse_transform *>(
                    new_item->type->get_use( "transform" )->get_actor_ptr() )
                ->target );
        }
        new_item->activate();
    }

    if( new_item->is_map() && !new_item->has_var( "reveal_map_center_omt" ) ) {
        new_item->set_var( "reveal_map_center_omt", project_to<coords::omt>( bub_to_abs( p ) ) );
    }

    current_submap->is_uniform = false;
    invalidate_max_populated_zlev( p.z() );

    current_submap->update_lum_add( l, *new_item );
    if( new_item->needs_processing() ) {
        if( current_submap->active_items.empty() ) {
            submaps_with_active_items.insert(
                tripoint_abs_sm( abs_sub.x() + p.x() / SEEX, abs_sub.y() + p.y() / SEEY, p.z() ) );
        }
        current_submap->active_items.add( *new_item );
    }

    new_item->on_map_placement( *this, p );

    current_submap->get_items( l ).push_back( std::move( new_item ) );
    if( auto * _log = coop_mutation_log::current() ) {
        _log->push( {coop_event_type::item_spawned, bub_to_abs( p ), 0} );
    }
    return;
}

detached_ptr<item> map::water_from( const tripoint_bub_ms& p )
{
    if( has_flag( "SALT_WATER", p ) ) {
        return item::spawn( "salt_water", calendar::start_of_cataclysm, item::INFINITE_CHARGES );
    }

    const ter_id terrain_id = ter( p );
    if( terrain_id == t_sewage ) {
        detached_ptr<item> ret =
            item::spawn( "water_sewage", calendar::start_of_cataclysm, item::INFINITE_CHARGES );
        ret->poison = rng( 1, 7 );
        return ret;
    }


    // iexamine::water_source requires a valid liquid from this function.
    if( terrain_id.obj().examine == &iexamine::water_source ) {
        detached_ptr<item> ret =
            item::spawn( "water", calendar::start_of_cataclysm, item::INFINITE_CHARGES );
        int poison_chance = 0;
        if( terrain_id.obj().has_flag( TFLAG_DEEP_WATER ) ) {
            if( terrain_id.obj().has_flag( TFLAG_CURRENT ) ) {
                poison_chance = 20;
            } else {
                poison_chance = 4;
            }
        } else {
            if( terrain_id.obj().has_flag( TFLAG_CURRENT ) ) {
                poison_chance = 10;
            } else {
                poison_chance = 3;
            }
        }
        if( one_in( poison_chance ) ) { ret->poison = rng( 1, 4 ); }
        return ret;
    }
    if( furn( p ).obj().examine == &iexamine::water_source ) {
        return item::spawn( "water", calendar::start_of_cataclysm, item::INFINITE_CHARGES );
    }
    if( furn( p ).obj().examine == &iexamine::clean_water_source
        || terrain_id.obj().examine == &iexamine::clean_water_source ) {
        return item::spawn( "water_clean", calendar::start_of_cataclysm, item::INFINITE_CHARGES );
    }
    if( furn( p ).obj().examine == &iexamine::liquid_source ) {
        // Terrains have no "provides_liquids" to work with generic source
        return item::spawn( furn( p ).obj().provides_liquids, calendar::turn, item::INFINITE_CHARGES );
    }
    return detached_ptr<item>();
}

void map::make_inactive( item& loc )
{
    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( loc.position() ), l );

    // remove from the active items cache (if it isn't there does nothing)
    current_submap->active_items.remove( &loc );
    if( current_submap->active_items.empty() ) {
        submaps_with_active_items.erase( tripoint_abs_sm(
                                             abs_sub.x() + loc.position().x() / SEEX, abs_sub.y() + loc.position().y() / SEEY,
                                             loc.position().z() ) );
    }
}

void map::make_active( item& loc )
{
    item* target = &loc;

    // Trust but verify, don't let stinking callers set items active when they shouldn't be.
    if( !target->needs_processing() ) { return; }
    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( loc.position() ), l );

    if( current_submap->active_items.empty() ) {
        submaps_with_active_items.insert( tripoint_abs_sm(
                                              abs_sub.x() + loc.position().x() / SEEX, abs_sub.y() + loc.position().y() / SEEY,
                                              loc.position().z() ) );
    }
    current_submap->active_items.add( *target );
}

void map::update_lum( item& loc, bool add )
{
    item* target = &loc;

    // if the item is not emissive, do nothing
    if( !target->is_emissive() ) { return; }

    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( loc.position() ), l );

    if( add ) {
        current_submap->update_lum_add( l, *target );
    } else {
        current_submap->update_lum_rem( l, *target );
    }
}

static bool process_map_items(
    item* item_ref, const tripoint_bub_ms& location, const temperature_flag flag )
{
    ZoneScopedN( "process_map_items" );
    return item_ref->attempt_detach( [&location, &flag]( detached_ptr<item>&& it ) {
        return item::process( std::move( it ), nullptr, location, false, flag );
    } );
}

static void process_vehicle_items( vehicle& cur_veh, int part )
{

    const int recharge_part_idx = cur_veh.part_with_feature( part, VPFLAG_RECHARGE, true );
    static const vehicle_part null_part;
    const vehicle_part &recharge_part = recharge_part_idx >= 0 ?
                                        cur_veh.part( recharge_part_idx ) :
                                        null_part;
    if( recharge_part_idx >= 0 && recharge_part.enabled &&
        !recharge_part.removed && !recharge_part.is_broken() ) {
        for( item * &outer : cur_veh.get_items( part ) ) {
            bool out_of_battery = false;
            outer->visit_items( [&cur_veh, &recharge_part, &out_of_battery]( item * it ) {
                item& n = *it;
                if( !n.has_flag( flag_RECHARGE ) && !n.has_flag( flag_USE_UPS ) ) {
                    return VisitResponse::NEXT;
                }
                if( n.ammo_capacity() > n.ammo_remaining()
                    || ( n.type->battery && n.type->battery->max_capacity > n.energy_remaining() ) ) {
                    int power = recharge_part.info().bonus;
                    while( power >= 1000 || x_in_y( power, 1000 ) ) {
                        const int missing = cur_veh.discharge_battery( 1, false );
                        if( missing > 0 ) {
                            out_of_battery = true;
                            return VisitResponse::ABORT;
                        }
                        if( n.is_battery() ) {
                            n.mod_energy( 1_kJ );
                        } else {
                            n.ammo_set( itype_battery, n.ammo_remaining() + 1 );
                        }
                        power -= 1000;
                    }
                    return VisitResponse::ABORT;
                }

                return VisitResponse::SKIP;
            } );
            if( out_of_battery ) { break; }
        }
    }
}

std::vector<tripoint_abs_sm> map::check_submap_active_item_consistency()
{
    std::vector<tripoint_abs_sm> result;

    // Direction 1: every in-grid submap with active items should be in the set.
    // Lazy-border submaps are intentionally excluded: they are pre-loaded for
    // shift performance but never registered in submaps_with_active_items.
    const int zmin = zlevels ? -OVERMAP_DEPTH : abs_sub.z();
    const int zmax = zlevels ? OVERMAP_HEIGHT : abs_sub.z();
    for( const auto p : bubble_submaps() ) {
        for( int z = zmin; z <= zmax; ++z ) {
            const auto sm_pos = tripoint_bub_sm( p, z );
            const submap* sm = getsubmap( get_nonant( sm_pos ) );
            if( sm == nullptr || sm->active_items.empty() ) { continue; }
            const auto abs_pos = bub_to_abs( sm_pos );
            if( !submaps_with_active_items.contains( abs_pos ) ) { result.push_back( abs_pos ); }
        }
    }

    // Direction 2: every entry in the set should point to a loaded submap with active items.
    mapbuffer& buf = MAPBUFFER_REGISTRY.get( bound_dimension_ );
    for( const tripoint_abs_sm& p : submaps_with_active_items ) {
        submap* s = buf.lookup_submap_in_memory( p );
        if( s == nullptr || s->active_items.empty() ) { result.push_back( p ); }
    }

    return result;
}

void map::process_items()
{
    auto total_active_items = int64_t{0};
    auto total_rottable_active_items = int64_t{0};

    // Process vehicle items from in-bubble submaps via per-z-level caches.
    // Out-of-bubble vehicle items are handled by batch_turns_items().
    {
        ZoneScopedN( "process_items_vehicles" );
        const int zmin = zlevels ? -OVERMAP_DEPTH : abs_sub.z();
        const int zmax = zlevels ? OVERMAP_HEIGHT : abs_sub.z();
        std::set<submap *> veh_submaps;
        for( int z = zmin; z <= zmax; ++z ) {
            for( vehicle * veh : get_cache( z ).vehicle_list ) {
                submap* sm =
                    MAPBUFFER_REGISTRY.get( bound_dimension_ )
                    .lookup_submap_in_memory( veh->abs_sm_pos );
                if( sm != nullptr ) { veh_submaps.insert( sm ); }
            }
        }
        std::ranges::for_each( veh_submaps, [&]( submap * sm ) {
            {
                ZoneScopedN( "process_items_count_vehicle_active_items" );
                for( const auto& veh : sm->vehicles ) {
                    if( !veh ) { continue; }
                    const auto counts = veh->active_items.count();
                    total_active_items += counts.total;
                    total_rottable_active_items += counts.rottable;
                }
            }
            process_items_in_vehicles( *sm );
        } );
    }
    // Snapshot because processing can add or remove active submaps.
    ZoneScopedN( "process_items_submaps" );
    std::vector<tripoint_abs_sm> submaps_with_active_items_copy;
    {
        ZoneScopedN( "process_items_snapshot_active_submaps" );
        submaps_with_active_items_copy = std::vector <
                                         tripoint_abs_sm > ( submaps_with_active_items.begin(), submaps_with_active_items.end() );
    }
    auto active_items = std::vector<item *> {};
    {
        ZoneScopedN( "process_items_scan_active_submaps" );
        const bool stride_skip_turn = !calendar::stride_due( item_process_stride );
        const int map_z = get_abs_sub().z();

        for( const tripoint_abs_sm& abs_pos : submaps_with_active_items_copy ) {
            if( !submap_loader.is_simulated( bound_dimension_, tripoint_abs_sm( abs_pos ) ) ) {
                continue;
            }
            const auto local_pos = abs_to_bub( abs_pos );
            submap* const current_submap = get_submap_at_grid( local_pos );
            if( current_submap == nullptr ) { continue; }
            if( current_submap->active_items.empty() ) { continue; }

            // Stride: off-z submaps with no time-critical items skip K-1/K turns.
            if( stride_skip_turn
                && abs_pos.z() != map_z
                && !current_submap->active_items.has_time_critical_items() ) {
                continue;  // skip counting and processing this turn
            }

            {
                ZoneScopedN( "process_items_count_active_items" );
                const auto counts = current_submap->active_items.count();
                total_active_items += counts.total;
                total_rottable_active_items += counts.rottable;
            }
            process_items_in_submap( *current_submap, local_pos, active_items );
        }
    }
    TracyPlot( "Total Active Items", total_active_items );
    TracyPlot( "Total Rottable Active Items", total_rottable_active_items );
}

static temperature_flag temperature_flag_at_point( const map& m, const tripoint_bub_ms& p )
{
    if( m.ter( p ) == t_rootcellar ) { return temperature_flag::TEMP_ROOT_CELLAR; }
    if( m.has_flag_furn( TFLAG_FRIDGE, p ) ) { return temperature_flag::TEMP_FRIDGE; }
    if( m.has_flag_furn( TFLAG_FREEZER, p ) ) { return temperature_flag::TEMP_FREEZER; }

    return temperature_flag::TEMP_NORMAL;
}

auto map::process_items_in_submap(
    submap& current_submap, const tripoint_bub_sm& gridp, std::vector<item *> &active_items )
-> void
{
    ZoneScopedN( "process_items_in_submap" );
    // Get a COPY of the active item list for this submap.
    // If more are added as a side effect of processing, they are ignored this turn.
    // If they are destroyed before processing, they don't get processed.
    {
        ZoneScopedN( "process_items_copy_active_items" );
        current_submap.active_items.get_for_processing( active_items );
    }
    const point grid_offset( gridp.x() * SEEX, gridp.y() * SEEY );
    {
        ZoneScopedN( "process_items_active_items" );
        for( item * &active_item_ref : active_items ) {
            if( !active_item_ref || !active_item_ref->is_loaded() ) {
                // The item was destroyed, so skip it.
                continue;
            }

            const auto map_location = active_item_ref->position();
            temperature_flag flag = temperature_flag_at_point( *this, tripoint_bub_ms( map_location ) );
            process_map_items( active_item_ref, map_location, flag );
        }
    }
}

void map::process_items_in_vehicles( submap& current_submap )
{
    // a copy, important if the vehicle list changes because a
    // vehicle got destroyed by a bomb (an active item!), this list
    // won't change, but veh_in_nonant will change.
    std::vector<vehicle *> vehicles;
    vehicles.reserve( current_submap.vehicles.size() );
    for( const auto& veh : current_submap.vehicles ) { vehicles.push_back( veh.get() ); }
    for( auto& cur_veh : vehicles ) {
        if( !current_submap.contains_vehicle( cur_veh ) ) {
            // vehicle not in the vehicle list of the nonant, has been
            // destroyed (or moved to another nonant?)
            // Can't be sure that it still exists, so skip it
            continue;
        }

        process_items_in_vehicle( *cur_veh, current_submap );
    }
}

void map::process_items_in_vehicle( vehicle& cur_veh, submap& current_submap )
{
    const bool engine_heater_is_on = cur_veh.has_part( "E_HEATER", true ) && cur_veh.engine_on;
    for( const vpart_reference& vp : cur_veh.get_any_parts( VPFLAG_FLUIDTANK ) ) {
        vp.part().process_contents( vp.pos(), engine_heater_is_on );
    }

    // OPP-4 / MISSED-4: if there is nothing to do (no active items and no cargo
    // recharge), skip the get_parts_including_carried() call entirely.
    // Building cargo_parts is not free on vehicles with many cargo slots.
    if( cur_veh.active_items.empty() && !cur_veh.has_cargo_recharge ) { return; }

    auto cargo_parts = cur_veh.get_parts_including_carried( VPFLAG_CARGO );
    if( cur_veh.has_cargo_recharge ) {
        for( const vpart_reference& vp : cargo_parts ) {
            process_vehicle_items( cur_veh, vp.part_index() );
        }
    }

    if( cur_veh.active_items.empty() ) { return; }

    for( item * active_item_ref : cur_veh.active_items.get_for_processing() ) {
        if( cargo_parts.empty() ) { return; }
        const auto it = std::ranges::find_if( cargo_parts, [&]( const vpart_reference & part ) {
            return active_item_ref->position() == cur_veh.bub_part_location( part.part() );
        } );

        if( it == cargo_parts.end() ) {
            continue; // Can't find a cargo part matching the active item.
        }
        const item& target = *active_item_ref;
        // Find the cargo part and coordinates corresponding to the current active item.
        const vehicle_part& pt = it->part();
        const auto item_loc = it->pos();
        auto items = cur_veh.get_items( static_cast<int>( it->part_index() ) );
        temperature_flag flag = temperature_flag::TEMP_NORMAL;
        if( target.is_food() || target.is_food_container() || target.is_corpse() ) {
            const vpart_info& pti = pt.info();
            if( engine_heater_is_on ) { flag = temperature_flag::TEMP_HEATER; }

            if( pt.enabled && pti.has_flag( VPFLAG_FRIDGE ) ) {
                flag = temperature_flag::TEMP_FRIDGE;
            } else if( pt.enabled && pti.has_flag( VPFLAG_FREEZER ) ) {
                flag = temperature_flag::TEMP_FREEZER;
            }
        }
        if( !process_map_items( active_item_ref, item_loc, flag ) ) {
            // If the item was NOT destroyed, we can skip the remainder,
            // which handles fallout from the vehicle being damaged.
            continue;
        }

        // item does not exist anymore, might have been an exploding bomb,
        // check if the vehicle is still valid (does exist)
        if( !current_submap.contains_vehicle( &cur_veh ) ) {
            // Nope, vehicle is not in the vehicle list of the submap,
            // it might have moved to another submap (unlikely)
            // or be destroyed, anyway it does not need to be processed here
            return;
        }

        // Vehicle still valid, reload the list of cargo parts,
        // the list of cargo parts might have changed (imagine a part with
        // a low index has been removed by an explosion, all the other
        // parts would move up to fill the gap).
        cargo_parts = cur_veh.get_any_parts( VPFLAG_CARGO );
    }
}

// Crafting/item finding functions

// Note: this is called quite a lot when drawing tiles
// Console build has the most expensive parts optimized out
bool map::sees_some_items( const tripoint_bub_ms& p, const Creature& who ) const
{
    // Can only see items if there are any items.
    return has_items( p ) && could_see_items( p, who.bub_pos() );
}

bool map::sees_some_items( const tripoint_bub_ms& p, const tripoint_bub_ms& from ) const
{
    return has_items( p ) && could_see_items( p, from );
}

bool map::could_see_items( const tripoint_bub_ms& p, const Creature& who ) const
{
    return could_see_items( p, who.bub_pos() );
}

bool map::could_see_items( const tripoint_bub_ms &p, const tripoint_bub_ms &from ) const
{
    static const std::string container_string( "CONTAINER" );
    const bool container = has_flag_ter_or_furn( container_string, p );
    const bool sealed = has_flag_ter_or_furn( TFLAG_SEALED, p );
    if( sealed && container ) {
    // never see inside of sealed containers
    return false;
}
if( container ) {
    // can see inside of containers if adjacent or
    // on top of the container
    return ( std::abs( p.x() - from.x() ) <= 1 &&
             std::abs( p.y() - from.y() ) <= 1 &&
             std::abs( p.z() - from.z() ) <= 1 );
    }
    return true;
}

bool map::has_items( const tripoint_bub_ms& p ) const
{
    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );
    if( current_submap == nullptr ) { return false; }

    return !current_submap->get_items( l ).empty();
}

template <typename Stack>
std::vector<detached_ptr<item>> use_amount_stack( Stack stack, const itype_id &type, int &quantity,
        const std::function<bool( const item & )> &filter )
{
    std::vector<detached_ptr<item>> ret;

    stack.remove_top_items_with( [&quantity, &filter, &type, &ret]( detached_ptr<item>&& it ) {
        if( quantity <= 0 ) { return std::move( it ); }
        detached_ptr<item> new_it = item::use_amount( std::move( it ), type, quantity, ret, filter );
        // NOLINTNEXTLINE(bugprone-use-after-move)
        if( it && !new_it ) { ret.push_back( std::move( it ) ); }
        return new_it;
    } );
    return ret;
}

std::vector<detached_ptr<item>> map::use_amount_square( const tripoint_bub_ms &p,
        const itype_id &type,
        int &quantity, const std::function<bool( const item & )> &filter )
{
    std::vector<detached_ptr<item>> ret;
    // Handle infinite map sources.
    detached_ptr<item> water = water_from( p );
    if( water && water->typeId() == type ) {
        ret.push_back( std::move( water ) );
        quantity = 0;
        return ret;
    }

    if( const std::optional<vpart_reference> vp = veh_at( p ).part_with_feature( "CARGO", true ) ) {
        std::vector<detached_ptr<item>> tmp =
            use_amount_stack( vp->vehicle().get_items( vp->part_index() ), type, quantity, filter );
        ret.insert( ret.end(), std::make_move_iterator( tmp.begin() ),
                    std::make_move_iterator( tmp.end() ) );
    }
    std::vector<detached_ptr<item>> tmp = use_amount_stack( i_at( p ), type, quantity, filter );
    ret.insert( ret.end(), std::make_move_iterator( tmp.begin() ),
                std::make_move_iterator( tmp.end() ) );
    return ret;
}

std::vector<detached_ptr<item>> map::use_amount( const tripoint_bub_ms &origin, const int range,
        const itype_id &type,
        int &quantity, const std::function<bool( const item & )> &filter )
{
    std::vector<detached_ptr<item>> ret;
    for( int radius = 0; radius <= range && quantity > 0; radius++ ) {
        for( const tripoint_bub_ms& p : points_in_radius( origin, radius ) ) {
            if( rl_dist( origin, p ) >= radius ) {
                std::vector<detached_ptr<item>> tmp = use_amount_square( p, type, quantity, filter );
                ret.insert( ret.end(), std::make_move_iterator( tmp.begin() ),
                            std::make_move_iterator( tmp.end() ) );
            }
        }
    }
    return ret;
}

template <typename Stack>
std::vector<detached_ptr<item>> use_charges_from_stack( Stack stack, const itype_id &type,
        int &quantity,
        const tripoint_bub_ms &pos, const std::function<bool( const item & )> &filter )
{

    std::vector<detached_ptr<item>> ret;

    stack.remove_top_items_with( [&quantity, &filter, &type, &pos, &ret]( detached_ptr<item>&& it ) {
        if( quantity <= 0 || it->made_of( LIQUID ) ) { return std::move( it ); }
        detached_ptr<item> new_it =
            item::use_charges( std::move( it ), type, quantity, ret, pos, filter );
        // NOLINTNEXTLINE(bugprone-use-after-move)
        if( it && !new_it ) { ret.push_back( std::move( it ) ); }
        return new_it;
    } );
    return ret;
}

static void use_charges_from_furn(
    const furn_t &f, const itype_id& type, int &quantity, map* m, const tripoint_bub_ms& p,
    std::vector<detached_ptr<item>> &ret, const std::function<bool( const item & )> &filter )
{
    if( m->has_flag( "LIQUIDCONT", p ) ) {
        auto item_list = m->i_at( p );
        auto current_item = item_list.begin();
        for( ; current_item != item_list.end(); ++current_item ) {
            // looking for a liquid that matches
            if( filter( **current_item ) && ( *current_item )->made_of( LIQUID )
                && type == ( *current_item )->typeId() ) {

                if( ( *current_item )->charges - quantity > 0 ) {
                    ret.push_back( ( *current_item )->split( quantity ) );
                    // All the liquid needed was found, no other sources will be needed
                    quantity = 0;
                } else {
                    // The liquid copy in ret already contains how much was available
                    // The leftover quantity returned will check other sources
                    quantity -= ( *current_item )->charges;
                    // Remove liquid item from the world
                    detached_ptr<item> det;
                    item_list.erase( current_item, &det );
                    ret.push_back( std::move( det ) );
                }
                return;
            }
        }
    }

    const std::vector<itype> item_list = f.crafting_pseudo_item_types();
    static const flag_id json_flag_USES_GRID_POWER( flag_USES_GRID_POWER );
    for( const itype& itt : item_list ) {
        if( itt.has_flag( json_flag_USES_GRID_POWER ) ) {
            const auto abspos( m->bub_to_abs( p ) );
            auto& grid = get_distribution_grid_tracker().grid_at( abspos );
            detached_ptr<item> furn_item =
                item::spawn( itt.get_id(), calendar::start_of_cataclysm, grid.get_resource() );
            int initial_quantity = quantity;
            if( filter( *furn_item ) ) {
                item::use_charges( std::move( furn_item ), type, quantity, ret, p );
                // That quantity math thing is atrocious. Punishment for the int& "argument".
                grid.mod_resource( quantity - initial_quantity );
            }
        } else if( itt.tool && !itt.tool->ammo_id.empty() ) {
            const itype_id ammo = ammotype( *itt.tool->ammo_id.begin() )->default_ammotype();
            if( itt.tool->subtype != type && type != ammo && itt.get_id() != type ) { continue; }
            auto stack = m->i_at( p );
            auto iter = std::ranges::find_if( stack, [ammo]( const item * const & i ) {
                return i->typeId() == ammo;
            } );
            if( iter != stack.end() ) {

                ( *iter )->attempt_detach(
                [&filter, &ammo, &quantity, &ret, &p]( detached_ptr<item>&& it ) {
                    if( filter( *it ) ) {
                        return item::use_charges( std::move( it ), ammo, quantity, ret, p );
                    }
                    return std::move( it );
                } );
            }
        }
    }
}

std::vector<detached_ptr<item>> map::use_charges( const tripoint_bub_ms &origin, const int range,
        const itype_id &type, int &quantity,
        const std::function<bool( const item & )> &filter )
{
    std::vector<detached_ptr<item>> ret;

    // populate a grid of spots that can be reached
    std::vector<tripoint_bub_ms> reachable_pts;

    if( range <= 0 ) {
        reachable_pts.push_back( origin );
    } else {
        reachable_flood_steps( reachable_pts, origin, range, 1, 100 );
    }

    // We prefer infinite map sources where available, so search for those
    // first
    for( const tripoint_bub_ms& p : reachable_pts ) {
        // Handle infinite map sources.
        detached_ptr<item> water = water_from( p );
        if( water && water->typeId() == type ) {
            water->charges = quantity;
            ret.push_back( std::move( water ) );
            quantity = 0;
            return ret;
        }
    }

    for( const tripoint_bub_ms& p : reachable_pts ) {
        if( has_furn( p ) ) {
            use_charges_from_furn( furn( p ).obj(), type, quantity, this, p, ret, filter );
            if( quantity <= 0 ) { return ret; }
        }

        if( accessible_items( p ) ) {
            std::vector<detached_ptr<item>> tmp =
                use_charges_from_stack( i_at( p ), type, quantity, p, filter );
            ret.insert( ret.end(), std::make_move_iterator( tmp.begin() ),
                        std::make_move_iterator( tmp.end() ) );
            if( quantity <= 0 ) { return ret; }
        }

        const optional_vpart_position vp = veh_at( p );
        if( !vp ) { continue; }

        const std::optional<vpart_reference> crafterpart = vp.part_with_feature( "CRAFTER", true );
        const std::optional<vpart_reference> faupart = vp.part_with_feature( "FAUCET", true );
        const std::optional<vpart_reference> autoclavepart =
            vp.part_with_feature( "AUTOCLAVE", true );
        const std::optional<vpart_reference> cargo = vp.part_with_feature( "CARGO", true );

        if( crafterpart ) {
            for( itype_id id : crafterpart->info().craftertools() ) {
                if( type == id ) {
                    detached_ptr<item> tmp = item::spawn( type, calendar::start_of_cataclysm );
                    tmp->charges = crafterpart->vehicle().drain( itype_battery, quantity );
                    quantity -= tmp->charges;
                    ret.push_back( std::move( tmp ) );

                    if( quantity == 0 ) { return ret; }
                }
            }
        }
        if( faupart ) { // we have a faucet, now to see what to drain
            itype_id ftype = itype_id::NULL_ID();

            ftype = type;

            // TODO: add a sane birthday arg
            // TODO!: check if we actually need the return  here
            detached_ptr<item> tmp = item::spawn( type, calendar::start_of_cataclysm );
            tmp->charges = faupart->vehicle().drain( ftype, quantity );
            // TODO: Handle water poison when crafting starts respecting it
            quantity -= tmp->charges;
            ret.push_back( std::move( tmp ) );

            if( quantity == 0 ) { return ret; }
        }

        if( autoclavepart ) { // we have an autoclave, now to see what to drain
            itype_id ftype = itype_id::NULL_ID();

            if( type == itype_autoclave ) { ftype = itype_battery; }

            // TODO: add a sane birthday arg
            detached_ptr<item> tmp = item::spawn( type, calendar::start_of_cataclysm );
            tmp->charges = autoclavepart->vehicle().drain( ftype, quantity );
            quantity -= tmp->charges;
            ret.push_back( std::move( tmp ) );

            if( quantity == 0 ) { return ret; }
        }

        if( cargo ) {
            std::vector<detached_ptr<item>> tmp =
                use_charges_from_stack( cargo->vehicle().get_items( cargo->part_index() ), type, quantity,
                                        tripoint_bub_ms( p ),
                                        filter );
            ret.insert( ret.end(), std::make_move_iterator( tmp.begin() ),
                        std::make_move_iterator( tmp.end() ) );
            if( quantity <= 0 ) {
                return ret;
            }
        }
    }

    return ret;
}
