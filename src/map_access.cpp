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



void map::set( const tripoint_bub_ms& p, const ter_id& new_terrain, const furn_id& new_furniture )
{
    furn_set( p, new_furniture );
    ter_set( p, new_terrain );
}

std::string map::name( const tripoint_bub_ms& p ) { return has_furn( p ) ? furnname( p ) : tername( p ); }

std::string map::disp_name( const tripoint_bub_ms& p ) { return string_format( _( "the %s" ), name( p ) ); }

std::string map::obstacle_name( const tripoint_bub_ms& p )
{
    if( const std::optional<vpart_reference> vp = veh_at( p ).obstacle_at_part() ) {
        return vp->info().name();
    }
    return name( p );
}

bool map::has_furn( const tripoint_bub_ms& p ) const { return furn( p ) != f_null; }

furn_id map::furn( const tripoint_bub_ms &p ) const
{
    if( is_out_of_bounds( tripoint_bub_ms( p ) ) ) {
    return f_null;
}

point_sm_ms l;
submap *const current_submap = get_submap_at( tripoint_bub_ms( p ), l );
if( current_submap == nullptr ) {
    return f_null;
}

return current_submap->get_furn( l );
}

void map::furn_set(
    const tripoint_bub_ms& p, const furn_id& new_furniture,
    const cata::poly_serialized<active_tile_data> &new_active, bool ignore_grab_check )
{
    if( is_out_of_bounds( p ) ) { return; }

    point_sm_ms l;
    submap* const current_submap = get_submap_at( p, l );
    if( current_submap == nullptr ) { return; }
    const furn_id old_id = current_submap->get_furn( l );
    if( old_id == new_furniture ) {
        // Nothing changed
        return;
    }

    current_submap->set_furn( l, new_furniture );

    // Set the dirty flags
    const furn_t &old_t = old_id.obj();
    const furn_t &new_t = new_furniture.obj();

    // If player has grabbed this furniture and it's no longer grabbable, release the grab.
    if( !ignore_grab_check && g->u.get_grab_type() == OBJECT_FURNITURE
        && g->u.bub_pos() + g->u.grab_point == p && !new_t.is_movable() ) {
        add_msg( _( "The %s you were grabbing is destroyed!" ), old_t.name() );
        g->u.grab( OBJECT_NONE );
    }
    // If a creature was crushed under a rubble -> free it
    if( old_id == f_rubble && new_furniture == f_null ) {
        Creature* c = g->critter_at( p );
        if( c ) { c->remove_effect( effect_crushed ); }
    }
    if( old_t.transparent != new_t.transparent ) {
        set_transparency_cache_dirty( p );
        set_seen_cache_dirty( p );
    }

    if( ( old_t.has_flag( TFLAG_NO_FLOOR ) != new_t.has_flag( TFLAG_NO_FLOOR ) )
        || ( old_t.has_flag( TFLAG_Z_TRANSPARENT ) != new_t.has_flag( TFLAG_Z_TRANSPARENT ) ) ) {
        set_floor_cache_dirty( p );
        // Changes to floor / z-transparency can reveal (or hide) tiles on the z-level below.
        // Invalidate seen caches unconditionally for both affected levels so tiles drawing
        // does not render stale BLANK visibility after events like explosions.
        set_seen_cache_dirty( p.z() );
        set_seen_cache_dirty( p.z() - 1 );
    }

    if( old_t.has_flag( TFLAG_SUN_ROOF_ABOVE ) != new_t.has_flag( TFLAG_SUN_ROOF_ABOVE ) ) {
        set_floor_cache_dirty( tripoint_bub_ms( p.xy(), p.z() + 1 ) );
    }

    invalidate_max_populated_zlev( p.z() );

    set_memory_seen_cache_dirty( p );

    // TODO: Limit to changes that affect move cost, traps and stairs
    set_pathfinding_cache_dirty( p );

    // Make sure the furniture falls if it needs to
    support_dirty( p );
    tripoint_bub_ms above( p.xy(), p.z() + 1 );
    // Make sure that if we supported something and no longer do so, it falls down
    support_dirty( above );

    if( old_t.active ) {
        current_submap->active_furniture.erase( point_sm_ms( l ) );
        // TODO: Only for g->m? Observer pattern?
        get_distribution_grid_tracker().on_changed( bub_to_abs( p ) );
    }
    if( new_t.active || new_active ) {
        cata::poly_serialized<active_tile_data> atd;
        if( new_active ) {
            atd = new_active;
        } else {
            atd = new_t.active->clone();
            atd->set_last_updated( calendar::turn );
        }
        current_submap->active_furniture[point_sm_ms( l )] = atd;
        get_distribution_grid_tracker().on_changed( bub_to_abs( p ) );
    }

    if( old_t.fluid_grid || new_t.fluid_grid ) { fluid_grid::on_structure_changed( bub_to_abs( p ) ); }
}

bool map::can_move_furniture( const tripoint_bub_ms& pos, player* p )
{
    if( !p ) { return false; }
    const furn_t &furniture_type = furn( pos ).obj();
    int required_str = furniture_type.move_str_req;

    // Object can not be moved (or nothing there)
    if( required_str < 0 ) { return false; }

    ///\EFFECT_STR determines what furniture the player can move
    int adjusted_str = p->str_cur;
    if( p->is_mounted() ) {
        auto mons = p->mounted_creature.get();
        if( mons->has_flag( MF_RIDEABLE_MECH ) && mons->mech_str_addition() != 0 ) {
            adjusted_str = mons->mech_str_addition();
        }
    }
    return adjusted_str >= required_str;
}

std::string map::furnname( const tripoint_bub_ms& p )
{
    const furn_t &f = furn( p ).obj();
    if( f.has_flag( "PLANT" ) ) {
        // Can't use item_stack::only_item() since there might be fertilizer
        map_stack items = i_at( p );
        const map_stack::iterator seed = std::ranges::find_if( items, []( const item * const & it ) {
            return it->is_seed();
        } );
        if( seed == items.end() ) {
            debugmsg( "Missing seed for plant at (%d, %d, %d)", p.x(), p.y(), p.z() );
            return "null";
        }
        const std::string& plant = ( *seed )->get_plant_name();
        return string_format( "%s (%s)", f.name(), plant );
    } else {
        return f.name();
    }
}

ter_id map::ter( const tripoint_bub_ms& p ) const
{
    // Check dimension bounds first - out-of-bounds areas show boundary terrain
    if( is_out_of_bounds( tripoint_bub_ms( p ) ) ) {
    return get_boundary_terrain();
    }

    point_sm_ms l;
    submap *const current_submap = get_submap_at( tripoint_bub_ms( p ), l );
    if( current_submap == nullptr ) {
    return t_null;
}

return current_submap->get_ter( l );
}

uint8_t map::get_known_connections(
    const tripoint_bub_ms& p, int connect_group,
    const std::map<tripoint_bub_ms, ter_id> &override ) const
{
    auto& ch = access_cache( p.z() );
    if( !ch.inbounds( p.xy() ) ) { return 0; }
    uint8_t val = 0;
    std::function<bool( const tripoint_bub_ms & )> is_memorized = [&]( const tripoint_bub_ms & q ) {
        return !g->u.get_memorized_tile( bub_to_abs( q ) ).tile.empty();
    };

    const bool overridden = override.contains( p );
    const bool is_transparent = ch.transparency_cache[ch.idx( p.x(),
                                p.y() )] > LIGHT_TRANSPARENCY_SOLID;

    // populate connection information
    for( int i = 0; i < 4; ++i ) {
        auto neighbour = p + offsets[i];
        if( !inbounds( neighbour ) ) { continue; }
        const auto neighbour_override = override.find( neighbour );
        const bool neighbour_overridden = neighbour_override != override.end();
        // if there's some non-memory terrain to show at the neighboring tile
        const bool may_connect = neighbour_overridden ||
                                 get_visibility( ch.visibility_cache[ch.idx( neighbour.x(), neighbour.y() )],
                                     get_visibility_variables_cache() ) == VIS_CLEAR ||
                                 // or if an actual center tile is transparent or next to a memorized tile
                                 ( !overridden && ( is_transparent || is_memorized( neighbour ) ) );
        if( may_connect ) {
            const ter_t &neighbour_terrain = neighbour_overridden ?
                                             neighbour_override->second.obj() : ter( neighbour ).obj();
            if( neighbour_terrain.connects_to( connect_group ) ) {
                val += 1 << i;
            }
        }
    }

    return val;
}

uint8_t map::get_known_connections_f(
    const tripoint_bub_ms& p, int connect_group,
    const std::map<tripoint_bub_ms, furn_id> &override ) const
{
    const level_cache& ch = access_cache( p.z() );
    if( !ch.inbounds( p.xy() ) ) { return 0; }
    uint8_t val = 0;
    avatar& player_character = get_avatar();
    std::function<bool( const tripoint_bub_ms & )> is_memorized = [&]( const tripoint_bub_ms & q ) {
        return !player_character.get_memorized_tile( bub_to_abs( q ) ).tile.empty();
    };

    const bool overridden = override.contains( p );
    const bool is_transparent = ch.transparency_cache[ch.idx( p.x(),
                                p.y() )] > LIGHT_TRANSPARENCY_SOLID;

    // populate connection information
    for( int i = 0; i < 4; ++i ) {
        auto pt = p + offsets[i];
        if( !inbounds( pt ) ) { continue; }
        const auto neighbour_override = override.find( pt );
        const bool neighbour_overridden = neighbour_override != override.end();
        // if there's some non-memory terrain to show at the neighboring tile
        const bool may_connect = neighbour_overridden ||
                                 get_visibility( ch.visibility_cache[ch.idx( pt.x(), pt.y() )],
                                     get_visibility_variables_cache() ) ==
                                 visibility_type::VIS_CLEAR ||
                                 // or if an actual center tile is transparent or
                                 // next to a memorized tile
                                 ( !overridden && ( is_transparent || is_memorized( pt ) ) );
        if( may_connect ) {
            const furn_t &neighbour_furn = neighbour_overridden ?
                                           neighbour_override->second.obj() : furn( pt ).obj();
            if( neighbour_furn.connects_to( connect_group ) ) {
                val += 1 << i;
            }
        }
    }

    return val;
}

ter_id map::get_ter_transforms_into( const tripoint_bub_ms& p ) const
{
    return ter( p ).obj().transforms_into.id();
}

furn_id map::get_furn_transforms_into( const tripoint_bub_ms& p ) const
{
    return furn( p ).obj().transforms_into.id();
}

void map::examine( Character& who, const tripoint_bub_ms& pos )
{
    const auto furn_here = furn( pos ).obj();
    if( furn_here.examine != iexamine::none ) {
        furn_here.examine( dynamic_cast<player &>( who ), pos );
    } else {
        ter( pos ).obj().examine( dynamic_cast<player &>( who ), pos );
    }
}

bool map::is_harvestable( const tripoint_bub_ms& pos ) const
{
    const auto& harvest_here = get_harvest( pos );
    return !harvest_here.is_null() && !harvest_here->empty();
}

bool map::ter_set( const tripoint_bub_ms& p, const ter_id& new_terrain )
{
    if( is_out_of_bounds( tripoint_bub_ms( p ) ) ) { return false; }

    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );
    if( current_submap == nullptr ) { return false; }
    const ter_id old_id = current_submap->get_ter( l );
    if( old_id == new_terrain ) {
        // Nothing changed
        return false;
    }

    current_submap->set_ter( l, new_terrain );

    // Set the dirty flags
    const ter_t& old_t = old_id.obj();
    const ter_t& new_t = new_terrain.obj();

    if( old_t.transparent != new_t.transparent ) {
        set_transparency_cache_dirty( p );
        set_seen_cache_dirty( p );
    }

    if( new_t.has_flag( TFLAG_NO_FLOOR ) != old_t.has_flag( TFLAG_NO_FLOOR ) ) {
        set_floor_cache_dirty( p );
        // It's a set, not a flag
        support_cache_dirty.insert( p );
        // Opening/closing a floor affects visibility on this and the level below.
        set_seen_cache_dirty( p.z() );
        set_seen_cache_dirty( p.z() - 1 );
    }

    if( new_t.has_flag( TFLAG_Z_TRANSPARENT ) != old_t.has_flag( TFLAG_Z_TRANSPARENT ) ) {
        set_floor_cache_dirty( p );
        // Changing z-transparency affects visibility between this z-level and the one below.
        set_seen_cache_dirty( p.z() );
        set_seen_cache_dirty( p.z() - 1 );
    }

    if( new_t.has_flag( TFLAG_SUSPENDED ) != old_t.has_flag( TFLAG_SUSPENDED ) ) {
        set_suspension_cache_dirty( p.z() );
        if( new_t.has_flag( TFLAG_SUSPENDED ) ) {
            level_cache& ch = get_cache( p.z() );
            ch.suspension_cache.emplace_back( bub_to_abs( p ).xy() );
        }
    }

    invalidate_max_populated_zlev( p.z() );
    set_memory_seen_cache_dirty( p );

    // TODO: Limit to changes that affect move cost, traps and stairs
    set_pathfinding_cache_dirty( p );

    tripoint_bub_ms above( p.xy(), p.z() + 1 );
    // Make sure that if we supported something and no longer do so, it falls down
    support_dirty( above );

    invalidate_lightmap_caches();

    return true;
}

std::string map::tername( const tripoint_bub_ms& p ) const { return ter( p ).obj().name(); }

std::string map::features( const tripoint_bub_ms& p )
{
    std::string result;
    const auto add = [&]( const std::string & text ) {
        if( !result.empty() ) { result += " "; }
        result += text;
    };
    const auto add_if = [&]( const bool cond, const std::string & text ) {
        if( cond ) { add( text ); }
    };
    // This is used in an info window that is 46 characters wide, and is expected
    // to take up one line.  So, make sure it does that.
    // FIXME: can't control length of localized text.
    const auto& feature_descriptions = map_feature_descriptions::get_map_feature_descriptions();
    using map_feature_descriptions::map_feature_description;
    for( const auto& description : feature_descriptions ) {
        bool condition = false;
        switch( description.test ) {
            case map_feature_description::test_type::bashable:
                condition = is_bashable( p );
                break;
            case map_feature_description::test_type::diggable:
                condition = ter( p )->is_diggable();
                break;
            case map_feature_description::test_type::flag:
                condition = has_flag( description.flag, p );
                break;
        }
        add_if( condition, description.text.translated() );
    }
    return result;
}

int map::move_cost_internal(
    const furn_t &furniture, const ter_t& terrain, const vehicle* veh, const int vpart ) const
{
    if( terrain.movecost == 0 || ( furniture.id && furniture.movecost < 0 ) ) { return 0; }

    if( veh != nullptr ) {
        const vpart_position vp( const_cast<vehicle &>( *veh ), vpart );
        if( vp.obstacle_at_part() ) {
            return 0;
        } else if( vp.part_with_feature( VPFLAG_AISLE, true ) ) {
            return 2;
        } else {
            return 8;
        }
    }

    if( furniture.id ) { return std::max( terrain.movecost + furniture.movecost, 0 ); }

    return std::max( terrain.movecost, 0 );
}

bool map::is_wall_adjacent( const tripoint_bub_ms &center ) const
{
for( const tripoint_bub_ms &p : points_in_radius( center, 1 ) ) {
    if( p != center && impassable( p ) ) {
            return true;
        }
    }
    return false;
}

int map::move_cost( const tripoint_bub_ms& p, const vehicle* ignored_vehicle ) const
{
    // Dimension bounds are always impassable
    if( is_out_of_bounds( tripoint_bub_ms( p ) ) ) {
    return 0;
}

const furn_t &furniture = furn( p ).obj();
const ter_t &terrain = ter( p ).obj();
const optional_vpart_position vp = veh_at( p );
vehicle *const veh = ( !vp || &vp->vehicle() == ignored_vehicle ) ? nullptr : &vp->vehicle();
const int part = veh ? vp->part_index() : -1;

return move_cost_internal( furniture, terrain, veh, part );
}

bool map::impassable( const tripoint_bub_ms& p ) const { return !passable( p ); }

bool map::passable( const tripoint_bub_ms& p ) const { return move_cost( p ) != 0; }

int map::move_cost_ter_furn( const tripoint_bub_ms& p ) const
{
    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );
    if( current_submap == nullptr ) { return 0; }

    const int tercost = current_submap->get_ter( l ).obj().movecost;
    if( tercost == 0 ) { return 0; }

    const int furncost = current_submap->get_furn( l ).obj().movecost;
    if( furncost < 0 ) { return 0; }

    const int cost = tercost + furncost;
    return cost > 0 ? cost : 0;
}

bool map::impassable_ter_furn( const tripoint_bub_ms& p ) const { return !passable_ter_furn( p ); }

bool map::passable_ter_furn( const tripoint_bub_ms& p ) const { return move_cost_ter_furn( p ) != 0; }

int map::combined_movecost(
    const tripoint_bub_ms& from, const tripoint_bub_ms& to, const vehicle* ignored_vehicle,
    const int modifier, const bool flying, const bool via_ramp ) const
{
    const int mults[4] = {0, 50, 71, 100};
    const int cost1 = move_cost( from, ignored_vehicle );
    const int cost2 = move_cost( to, ignored_vehicle );
    // Multiply cost depending on the number of differing axes
    // 0 if all axes are equal, 100% if only 1 differs, 141% for 2, 200% for 3
    size_t match =
        trigdist ? ( from.x() != to.x() ) + ( from.y() != to.y() ) + ( from.z() != to.z() ) : 1;
    if( flying || from.z() == to.z() ) { return ( cost1 + cost2 + modifier ) * mults[match] / 2; }

    // Inter-z-level movement by foot (not flying)
    if( !valid_move( from, to, false, via_ramp ) ) { return 0; }

    // TODO: Penalize for using stairs
    return ( cost1 + cost2 + modifier ) * mults[match] / 2;
}

bool map::valid_move(
    const tripoint_bub_ms& from, const tripoint_bub_ms& to, const bool bash, const bool flying,
    const bool via_ramp ) const
{
    // Used to account for the fact that older versions of GCC can trip on the if statement here.
    assert( to.z() > std::numeric_limits<int>::min() );
    if( std::abs( from.x() - to.x() ) > 1 || std::abs( from.y() - to.y() ) > 1 ||
    std::abs( from.z() - to.z() ) > 1 ) {
    return false;
}

if( from.z() == to.z() ) {
    // But here we need to, to prevent bashing critters
    return passable( to ) || ( bash && inbounds( to ) );
    } else if( !zlevels ) {
    return false;
}

const bool going_up = from.z() < to.z();

const auto &up_p = tripoint_bub_ms( going_up ? to : from );
const auto &down_p = tripoint_bub_ms( going_up ? from : to );

const maptile up = maptile_at( up_p );
const ter_t &up_ter = up.get_ter_t();
if( up_ter.id.is_null() ) {
    return false;
}
// Checking for ledge is a workaround for the case when mapgen doesn't
// actually make a valid ledge drop location with zlevels on, this forces
// at least one zlevel drop and if down_ter is impassible it's probably
// inside a wall, we could workaround that further but it's unnecessary.
const bool up_is_ledge = tr_at( up_p ).loadid == tr_ledge;

if( up_ter.movecost == 0 ) {
    // Unpassable tile
    return false;
}

const maptile down = maptile_at( down_p );
const ter_t &down_ter = down.get_ter_t();
if( down_ter.id.is_null() ) {
    return false;
}

if( !up_is_ledge && down_ter.movecost == 0 ) {
    // Unpassable tile
    return false;
}

if( !up_ter.has_flag( TFLAG_NO_FLOOR ) && !up_ter.has_flag( TFLAG_GOES_DOWN ) && !up_is_ledge &&
        !via_ramp ) {
    // Can't move from up to down
    if( std::abs( from.x() - to.x() ) == 1 || std::abs( from.y() - to.y() ) == 1 ) {
            // Break the move into two - vertical then horizontal
            tripoint_bub_ms midpoint( down_p.xy(), up_p.z() );
            return valid_move( down_p, midpoint, bash, flying, via_ramp )
                   && valid_move( midpoint, up_p, bash, flying, via_ramp );
        }
        return false;
    }

    if( !flying && !down_ter.has_flag( TFLAG_GOES_UP ) && !down_ter.has_flag( TFLAG_RAMP ) &&
        !up_is_ledge && !via_ramp ) {
    // Can't safely reach the lower tile
    return false;
}

if( bash ) {
    return true;
}
// get_cache() has no bounds check on z, and maptile_at's mapbuffer fallback
// can return non-null terrain for positions outside the reality bubble.
if( !inbounds( down_p ) || !inbounds_z( up_p.z() ) ) {
    return up.get_furn_t().movecost >= 0;
    }

    int part_up;
    const vehicle *veh_up = veh_at_internal( up_p, part_up );
    if( veh_up != nullptr && !veh_at( up_p ).part_with_feature( VPFLAG_NOCOLLIDEBELOW, false ) ) {
    // TODO: Hatches below the vehicle
    return false;
}

int part_down;
const vehicle *veh_down = veh_at_internal( down_p, part_down );
if( veh_down != nullptr && veh_down->roof_at_part( part_down ) >= 0 ) {
    // TODO: OPEN (and only open) hatches from above
    return false;
}

// Currently only furniture can block movement if everything else is OK
// TODO: Vehicles with boards in the given spot
return up.get_furn_t().movecost >= 0;
}

double map::ranged_target_size( const tripoint_bub_ms &p ) const
{
    if( impassable( p ) ) {
    return 1.0;
}

if( !has_floor( p ) ) {
    return 0.0;
}

// TODO: Handle cases like shrubs, trees, furniture, sandbags...
return 0.1;
}

int map::climb_difficulty( const tripoint_bub_ms &p ) const
{
    if( p.z() > OVERMAP_HEIGHT || p.z() < -OVERMAP_DEPTH ) {
    debugmsg( "climb_difficulty on out of bounds point: %d, %d, %d", p.x(), p.y(), p.z() );
        return INT_MAX;
    }

    int best_difficulty = INT_MAX;
    int blocks_movement = 0;
    if( has_flag( "LADDER", p ) ) {
    // Really easy, but you have to stand on the tile
    return 1;
} else if( has_flag( TFLAG_RAMP, p ) || has_flag( TFLAG_RAMP_UP, p ) ||
               has_flag( TFLAG_RAMP_DOWN, p ) ) {
    // We're on something stair-like, so halfway there already
    best_difficulty = 7;
}

for( const auto &pt : points_in_radius( p, 1 ) ) {
    if( impassable_ter_furn( pt ) ) {
            // TODO: Non-hardcoded climbability
            best_difficulty = std::min( best_difficulty, 10 );
            blocks_movement++;
        } else if( veh_at( pt ) ) {
            // Vehicle tiles are quite good for climbing
            // TODO: Penalize spiked parts?
            best_difficulty = std::min( best_difficulty, 7 );
        }

        if( best_difficulty > 5 && has_flag( "CLIMBABLE", pt ) ) { best_difficulty = 5; }
    }

    // TODO: Make this more sensible - check opposite sides, not just movement blocker count
    return std::max( 0, best_difficulty - blocks_movement );
}

bool map::has_floor( const tripoint_bub_ms &p, bool visible_only ) const
{
    if( p.z() < -OVERMAP_DEPTH || p.z() > OVERMAP_HEIGHT ) {
    return false;
}

point_sm_ms l;
submap *sm = get_submap_at( tripoint_bub_ms( p ), l );
if( !sm ) {
    return false;
}
if( sm->floor_dirty ) {
    const int smx = divide_round_to_minus_infinity( p.x(), SEEX );
        const int smy = divide_round_to_minus_infinity( p.y(), SEEY );
        sm->rebuild_floor_cache( *this, tripoint_bub_sm( smx, smy, p.z() ) );
    }
    return sm->floor_cache[l.x()][l.y()] || ( !visible_only && has_flag( TFLAG_Z_TRANSPARENT, p ) );
}

bool map::floor_between( const tripoint_bub_ms& first, const tripoint_bub_ms& second ) const
{
    int diff = std::abs( first.z() - second.z() );
    if( diff == 0 ) { // There's never a floor between two tiles on the same level
        return false;
    }
    if( diff != 1 ) {
        debugmsg( "map::floor_between should only be called on tiles that are exactly 1 z level "
                  "apart" );
        return true;
    }
    int upper = std::max( first.z(), second.z() );
    if( first.xy() == second.xy() ) { return has_floor( tripoint_bub_ms( first.xy(), upper ) ); }
    return has_floor( tripoint_bub_ms( first.xy(), upper ) )
           && has_floor( tripoint_bub_ms( second.xy(), upper ) );
}

bool map::supports_above( const tripoint_bub_ms& p ) const
{
    const maptile tile = maptile_at( tripoint_bub_ms( p ) );
    const ter_t& ter = tile.get_ter_t();
    if( ter.movecost == 0 ) { return true; }

    const furn_id frn_id = tile.get_furn();
    if( frn_id != f_null ) {
        const furn_t &frn = frn_id.obj();
        if( frn.movecost < 0 ) { return true; }
    }

    return veh_at( p ).has_value();
}

bool map::has_floor_or_support( const tripoint_bub_ms &p ) const
{
    if( p.z() < -OVERMAP_DEPTH || p.z() > OVERMAP_HEIGHT ) {
    return false;
}
const tripoint_bub_ms below( p.xy(), p.z() - 1 );
return !valid_move( p, below, false, true );
}

