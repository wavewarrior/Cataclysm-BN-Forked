#include "map.h"
#include "coop_mutation_log.h"

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
#include "faction.h"
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


int map::bash_rating_internal(
    const int str, const furn_t &furniture, const ter_t& terrain, const bool allow_floor,
    const vehicle* veh, const int part ) const
{
    bool furn_smash = false;
    bool ter_smash = false;
    ///\EFFECT_STR determines what furniture can be smashed
    if( furniture.id && furniture.bash.str_max != -1 ) {
        furn_smash = true;
        ///\EFFECT_STR determines what terrain can be smashed
    } else if( terrain.bash.str_max != -1 && ( !terrain.bash.bash_below || allow_floor ) ) {
        ter_smash = true;
    }

    if( veh != nullptr && vpart_position( const_cast<vehicle &>( *veh ), part ).obstacle_at_part() ) {
        // Monsters only care about rating > 0, NPCs should want to path around cars instead
        return 2; // Should probably be a function of part hp (+armor on tile)
    }

    int bash_min = 0;
    int bash_max = 0;
    if( furn_smash ) {
        bash_min = furniture.bash.str_min;
        bash_max = furniture.bash.str_max;
    } else if( ter_smash ) {
        bash_min = terrain.bash.str_min;
        bash_max = terrain.bash.str_max;
    } else {
        return -1;
    }

    ///\EFFECT_STR increases smashing damage
    if( str < bash_min ) {
        return 0;
    } else if( str >= bash_max ) {
        return 10;
    }

    int ret = ( 10 * ( str - bash_min ) ) / ( bash_max - bash_min );
    // Round up to 1, so that desperate NPCs can try to bash down walls
    return std::max( ret, 1 );
}

// 3D bashable

bool map::is_bashable( const tripoint_bub_ms &p, const bool allow_floor ) const
{
    if( veh_at( p ).obstacle_at_part() ) {
    return true;
}

if( has_furn( p ) && furn( p ).obj().bash.str_max != -1 ) {
    return true;
}

const auto &ter_bash = ter( p ).obj().bash;
return ter_bash.str_max != -1 && ( !ter_bash.bash_below || allow_floor );
}

bool map::is_bashable_ter( const tripoint_bub_ms& p, const bool allow_floor ) const
{
    const auto& ter_bash = ter( p ).obj().bash;
    return ter_bash.str_max != -1
           && ( ( !ter_bash.bash_below && !ter( p ).obj().has_flag( "VEH_TREAT_AS_BASH_BELOW" ) )
                || allow_floor );
}

bool map::is_bashable_furn( const tripoint_bub_ms& p ) const
{
    return has_furn( p ) && furn( p ).obj().bash.str_max != -1;
}

bool map::is_bashable_ter_furn( const tripoint_bub_ms& p, const bool allow_floor ) const
{
    return is_bashable_furn( p ) || is_bashable_ter( p, allow_floor );
}

int map::bash_strength( const tripoint_bub_ms& p, const bool allow_floor ) const
{
    if( has_furn( p ) && furn( p ).obj().bash.str_max != -1 ) { return furn( p ).obj().bash.str_max; }

    const auto& ter_bash = ter( p ).obj().bash;
    if( ter_bash.str_max != -1 && ( !ter_bash.bash_below || allow_floor ) ) {
        return ter_bash.str_max;
    }

    return -1;
}

int map::bash_resistance( const tripoint_bub_ms& p, const bool allow_floor ) const
{
    if( has_furn( p ) && furn( p ).obj().bash.str_min != -1 ) { return furn( p ).obj().bash.str_min; }

    const auto& ter_bash = ter( p ).obj().bash;
    if( ter_bash.str_min != -1 && ( !ter_bash.bash_below || allow_floor ) ) {
        return ter_bash.str_min;
    }

    return -1;
}

int map::bash_rating( const int str, const tripoint_bub_ms& p, const bool allow_floor ) const
{
    if( str <= 0 ) { return -1; }

    const furn_t &furniture = furn( p ).obj();
    const ter_t& terrain = ter( p ).obj();
    const optional_vpart_position vp = veh_at( p );
    vehicle* const veh = vp ? &vp->vehicle() : nullptr;
    const int part = vp ? vp->part_index() : -1;
    return bash_rating_internal( str, furniture, terrain, allow_floor, veh, part );
}

// End of 3D bashable

void map::make_rubble(
    const tripoint_bub_ms& p, const furn_id& rubble_type, const ter_id& floor_type,
    bool overwrite )
{
    if( overwrite ) {
        ter_set( p, floor_type );
        furn_set( p, rubble_type );
    } else {
        // First see if there is existing furniture to destroy
        if( is_bashable_furn( p ) ) { destroy_furn( p, true ); }
        // Leave the terrain alone unless it interferes with furniture placement
        if( impassable( p ) && is_bashable_ter( p ) ) { destroy( p, true ); }
        // Check again for new terrain after potential destruction
        if( impassable( p ) ) { ter_set( p, floor_type ); }

        furn_set( p, rubble_type );
    }
}

bool map::is_water_shallow_current( const tripoint_bub_ms& p ) const
{
    return has_flag( "CURRENT", p ) && !has_flag( TFLAG_DEEP_WATER, p );
}

bool map::is_divable( const tripoint_bub_ms &p ) const
{
    const std::optional<vpart_reference> vp = veh_at( p ).part_with_feature( VPFLAG_BOARDABLE,
        true );
    if( !vp ) {
        return has_flag( "SWIMMABLE", p ) && has_flag( TFLAG_DEEP_WATER, p );
    }
    return false;
}

bool map::is_outside( const tripoint_bub_ms& p ) const
{
    point_sm_ms l;
    submap* sm = get_submap_at( tripoint_bub_ms( p ), l );
    if( !sm ) { return true; }
    if( sm->outside_dirty ) {
        const int smx = divide_round_to_minus_infinity( p.x(), SEEX );
        const int smy = divide_round_to_minus_infinity( p.y(), SEEY );
        const level_cache* above = ( p.z() < OVERMAP_HEIGHT ) ? &get_cache_ref( p.z() + 1 ) : nullptr;
        sm->rebuild_outside_cache( above, tripoint_bub_sm( smx, smy, p.z() ) );
    }
    return sm->outside_cache[l.x()][l.y()];
}

bool map::is_sheltered( const tripoint_bub_ms& p ) const
{
    point_sm_ms l;
    submap* sm = get_submap_at( p, l );
    if( !sm ) {
        return true; // outside loaded area — treat as sheltered
    }
    if( sm->outside_dirty ) {
        const level_cache* above = ( p.z() < OVERMAP_HEIGHT ) ? &get_cache_ref( p.z() + 1 ) : nullptr;
        sm->rebuild_outside_cache( above, project_to<coords::sm>( p ) );
    }
    return sm->sheltered_cache[l.x()][l.y()];
}

float map::get_transparency( const tripoint_bub_ms& p ) const
{
    point_sm_ms l;
    submap* sm = get_submap_at( p, l );
    if( !sm ) { return LIGHT_TRANSPARENCY_SOLID; }
    if( sm->transparency_dirty ) {
        sm->rebuild_transparency_cache( *this, project_to<coords::sm>( p ) );
    }
    return sm->transparency_cache[l.x()][l.y()];
}

bool map::is_last_ter_wall(
    const bool no_furn, const point_bub_ms& p, const point_bub_ms& max, const direction dir ) const
{
    point mov;
    switch( dir ) {
        case direction::NORTH:
            mov.y = -1;
            break;
        case direction::SOUTH:
            mov.y = 1;
            break;
        case direction::WEST:
            mov.x = -1;
            break;
        case direction::EAST:
            mov.x = 1;
            break;
        default:
            break;
    }
    auto p2( p );
    bool result = true;
    bool loop = true;
    while(
        ( loop )
        && ( ( dir == direction::NORTH && p2.y() >= 0 )
             || ( dir == direction::SOUTH && p2.y() < max.y() )
             || ( dir == direction::WEST && p2.x() >= 0 )
             || ( dir == direction::EAST && p2.x() < max.x() ) ) ) {
        if( no_furn && has_furn( p2 ) ) {
            loop = false;
            result = false;
        } else if( !has_flag_ter( "FLAT", p2 ) ) {
            loop = false;
            if( !has_flag_ter( "WALL", p2 ) ) { result = false; }
        }
        p2.x() += mov.x;
        p2.y() += mov.y;
    }
    return result;
}

bool map::tinder_at( const tripoint_bub_ms& p )
{
    for( const auto& i : i_at( p ) ) {
        if( i->has_flag( flag_TINDER ) ) { return true; }
    }
    return false;
}

bool map::flammable_items_at( const tripoint_bub_ms& p, int threshold )
{
    if( !has_items( p ) || ( has_flag( TFLAG_SEALED, p ) &&
                             !has_flag( TFLAG_ALLOW_FIELD_EFFECT, p ) ) ) {
        // Sealed containers don't allow fire, so shouldn't allow setting the fire either
        return false;
    }

    for( const auto& i : i_at( p ) ) {
        if( i->flammable( threshold ) ) { return true; }
    }

    return false;
}

bool map::is_flammable( const tripoint_bub_ms& p )
{
    if( flammable_items_at( p ) ) { return true; }

    if( has_flag( "FLAMMABLE", p ) ) { return true; }

    if( has_flag( "FLAMMABLE_ASH", p ) ) { return true; }

    if( get_field_intensity( p, fd_web ) > 0 ) { return true; }

    return false;
}

void map::decay_fields_and_scent( const time_duration& amount )
{
    ZoneScopedN( "decay_fields_and_scent" );
    // TODO: Make this happen on all z-levels

    // Decay scent separately, so that later we can use field count to skip empty submaps
    g->scent.decay();

    // Coordinate code copied from lightmap calculations
    // TODO: Z
    const int smz = abs_sub.z();
    level_cache& smz_cache = get_cache( smz );
    for( const auto p : bubble_submaps() ) {
        const auto sm_pos = tripoint_bub_sm( p, smz );
        const auto cur_submap = get_submap_at_grid( sm_pos );
        if( cur_submap == nullptr ) { continue; }
        int to_proc = cur_submap->field_count;
        if( to_proc < 1 ) {
            if( to_proc < 0 ) {
                cur_submap->field_count = 0;
                dbg( DL::Error ) << "map::decay_fields_and_scent: submap at " << bub_to_abs( sm_pos )
                                 << "has " << to_proc << " field_count";
            }
            // This submap has no fields
            continue;
        }

        if( to_proc > 0 ) {
            for( const auto sm_ms : submap_tiles() ) {
                const auto ms_pos = project_combine( sm_pos, sm_ms );

                field& fields = cur_submap->get_field( sm_ms );
                if( !smz_cache.outside_cache[smz_cache.idx( ms_pos.x(), ms_pos.y() )] ) {
                    to_proc -= fields.field_count();
                } else {
                    for( auto& fp : fields ) {
                        to_proc--;
                        field_entry& cur = fp.second;
                        const field_type_id type = cur.get_field_type();
                        const int decay_amount_factor = type.obj().decay_amount_factor;
                        if( decay_amount_factor != 0 ) {
                            const time_duration decay_amount = amount / decay_amount_factor;
                            cur.set_field_age( cur.get_field_age() + decay_amount );
                        }
                    }
                }
            }
        }

        if( to_proc > 0 ) {
            cur_submap->field_count = cur_submap->field_count - to_proc;
            dbg( DL::Warn ) << "map::decay_fields_and_scent: submap at " << bub_to_abs( sm_pos )
                            << "has " << cur_submap->field_count - to_proc << "fields, but "
                            << cur_submap->field_count << " field_count";
        }
    }
}

point_bub_ms map::random_outdoor_tile()
{
    std::vector<point_bub_ms> options;
    for( const tripoint_bub_ms& p : points_on_zlevel() ) {
        if( is_outside( p.xy() ) ) { options.push_back( p.xy() ); }
    }
    return random_entry( options, point_bub_ms::north_west() );
}

bool map::has_item_with( const tripoint_bub_ms& p,
                         const std::function<bool( const item & )> &filter )
{
    for( const item * it : i_at( p ) ) {
        if( filter( *it ) ) { return true; }
    }
    return false;
}

bool map::has_adjacent_item_with(
    const tripoint_bub_ms& p, const std::function<bool( const item & )> &filter )
{
    for( const auto& adj : points_in_radius( p, 1 ) ) {
        if( !has_items( adj ) ) { continue; }

        for( const item * it : i_at( adj ) ) {
            if( filter( *it ) ) { return true; }
        }
    }
    return false;
}

bool map::has_adjacent_furniture_with(
    const tripoint_bub_ms& p, const std::function<bool( const furn_t & )> &filter )
{
    for( const auto& adj : points_in_radius( p, 1 ) ) {
        if( has_furn( adj ) && filter( furn( adj ).obj() ) ) { return true; }
    }

    return false;
}

bool map::has_adjacent_terrain_with(
    const tripoint_bub_ms& p, const std::function<bool( const ter_t & )> &filter )
{
    for( const auto& adj : points_in_radius( p, 1 ) ) {
        if( filter( ter( adj ).obj() ) ) { return true; }
    }

    return false;
}

bool map::has_nearby(
    const tripoint_bub_ms& p, const std::function<bool( map& m, const tripoint_bub_ms& p )> &pred,
    int radius )
{
    for( const tripoint_bub_ms& adj : points_in_radius( p, radius ) ) {
        if( pred( *this, adj ) ) { return true; }
    }
    return false;
}

bool map::has_nearby_fire( const tripoint_bub_ms& p, int radius )
{
    for( const tripoint_bub_ms& pt : points_in_radius( p, radius ) ) {
        if( get_field( pt, fd_fire ) != nullptr ) { return true; }
        if( has_flag_ter_or_furn( "USABLE_FIRE", pt ) ) { return true; }
    }
    return false;
}

bool map::has_nearby_table( const tripoint_bub_ms& p, int radius )
{
    for( const tripoint_bub_ms& pt : points_in_radius( p, radius ) ) {
        const optional_vpart_position vp = veh_at( p );
        if( has_flag( "FLAT_SURF", pt ) ) { return true; }
        if( vp && ( vp->vehicle().has_part( "FLAT_SURF" ) ) ) { return true; }
    }
    return false;
}

bool map::has_nearby_chair( const tripoint_bub_ms& p, int radius )
{
    for( const tripoint_bub_ms& pt : points_in_radius( p, radius ) ) {
        const optional_vpart_position vp = veh_at( pt );
        if( has_flag( "CAN_SIT", pt ) ) { return true; }
        if( vp && vp->vehicle().has_part( "SEAT" ) ) { return true; }
    }
    return false;
}

bool map::mop_spills( const tripoint_bub_ms& p )
{
    bool retval = false;

    if( !has_flag( "LIQUIDCONT", p ) && !has_flag( "SEALED", p ) ) {
        auto items = i_at( p );

        items.remove_top_items_with( [&retval]( detached_ptr<item>&& e ) {
            if( e->made_of( LIQUID ) ) {
                retval = true;
                return detached_ptr<item>();
            }
            return std::move( e );
        } );
    }

    field& fld = field_at( p );
    static const std::vector<field_type_id> to_check = {
        fd_blood,      fd_blood_veggy, fd_blood_insect, fd_blood_invertebrate,
        fd_gibs_flesh, fd_gibs_veggy,  fd_gibs_insect,  fd_gibs_invertebrate,
        fd_bile,       fd_slime,       fd_sludge
    };
    for( field_type_id fid : to_check ) { retval |= fld.remove_field( fid ); }

    if( const optional_vpart_position vp = veh_at( p ) ) {
        vehicle* const veh = &vp->vehicle();
        std::vector<int> parts_here = veh->parts_at_relative( vp->mount(), true );
        for( auto& elem : parts_here ) {
            if( veh->part( elem ).blood > 0 ) {
                veh->part( elem ).blood = 0;
                retval = true;
            }
            // remove any liquids that somehow didn't fall through to the ground
            vehicle_stack here = veh->get_items( elem );
            here.remove_top_items_with( [&retval]( detached_ptr<item>&& e ) {
                if( e->made_of( LIQUID ) ) {
                    retval = true;
                    return detached_ptr<item>();
                }
                return std::move( e );
            } );
        }
    } // if veh != 0
    return retval;
}

int map::collapse_check( const tripoint_bub_ms& p )
{
    const bool collapses = has_flag( TFLAG_COLLAPSES, p );
    const bool supports_roof = has_flag( TFLAG_SUPPORTS_ROOF, p );

    int num_supports = p.z() == -OVERMAP_DEPTH ? 0 : -5;
    // if there's support below, things are less likely to collapse
    if( p.z() > -OVERMAP_DEPTH ) {
        const auto& pbelow = tripoint_bub_ms( p.xy(), p.z() - 1 );
        for( const auto& tbelow : points_in_radius( pbelow, 1 ) ) {
            if( has_flag( TFLAG_SUPPORTS_ROOF, pbelow ) ) {
                num_supports += 1;
                if( has_flag( TFLAG_WALL, pbelow ) ) { num_supports += 2; }
                if( tbelow == pbelow ) { num_supports += 2; }
            }
        }
    }

    for( const auto& t : points_in_radius( p, 1 ) ) {
        if( p == t ) { continue; }

        if( collapses ) {
            if( has_flag( TFLAG_COLLAPSES, t ) ) {
                num_supports++;
            } else if( has_flag( TFLAG_SUPPORTS_ROOF, t ) ) {
                num_supports += 2;
            }
        } else if( supports_roof ) {
            if( has_flag( TFLAG_SUPPORTS_ROOF, t ) ) {
                if( has_flag( TFLAG_WALL, t ) ) {
                    num_supports += 4;
                } else if( !has_flag( TFLAG_COLLAPSES, t ) ) {
                    num_supports += 3;
                }
            }
        }
    }

    return 1.7 * num_supports;
}

// there is still some odd behavior here and there and you can get floating chunks of
// unsupported floor, but this is much better than it used to be
void map::collapse_at(
    const tripoint_bub_ms& p, const bool silent, const bool was_supporting,
    const bool destroy_pos )
{
    const bool supports = was_supporting || has_flag( TFLAG_SUPPORTS_ROOF, p );
    const bool wall = was_supporting || has_flag( TFLAG_WALL, p );
    // don't bash again if the caller already bashed here
    if( destroy_pos ) {
        destroy( p, silent );
        crush( p );
        make_rubble( p );
    }
    const bool still_supports = has_flag( TFLAG_SUPPORTS_ROOF, p );

    // If something supporting the roof collapsed, see what else collapses
    if( supports && !still_supports ) {
        for( const auto& t : points_in_radius( p, 1 ) ) {
            // If z-levels are off, tz == t, so we end up skipping a lot of stuff to avoid bugs.
            const auto& tz = tripoint_bub_ms( t.xy(), t.z() + 1 );
            // if nothing above us had the chance of collapsing, move on
            if( !one_in( collapse_check( tz ) ) ) { continue; }
            // if a wall collapses, walls without support from below risk collapsing and
            // propagate the collapse upwards
            if( zlevels && wall && p == t && has_flag( TFLAG_WALL, tz ) ) { collapse_at( tz, silent ); }
            // floors without support from below risk collapsing into open air and can propagate
            // the collapse horizontally but not vertically
            if( p != t && ( has_flag( TFLAG_SUPPORTS_ROOF, t ) && has_flag( TFLAG_COLLAPSES, t ) ) ) {
                collapse_at( t, silent );
            }
        }
        // this tile used to support a roof, now it doesn't, which means there is only
        // open air above us
        if( zlevels ) {
            const tripoint_bub_ms tabove( p.xy(), p.z() + 1 );
            ter_set( tabove, t_open_air );
            furn_set( tabove, f_null );
            propagate_suspension_check( tabove );
        }
    }
    // it would be great to check if collapsing ceilings smashed through the floor, but
    // that's not handled for now
}

void map::propagate_suspension_check( const tripoint_bub_ms& point )
{
    for( const auto& neighbor : points_in_radius( point, 1 ) ) {
        if( neighbor != point && has_flag( TFLAG_SUSPENDED, neighbor ) ) {
            collapse_invalid_suspension( neighbor );
        }
    }
}

void map::collapse_invalid_suspension( const tripoint_bub_ms& point )
{
    if( !is_suspension_valid( point ) ) {
        ter_set( point, t_open_air );
        furn_set( point, f_null );

        propagate_suspension_check( point );
    }
}

bool map::is_suspension_valid( const tripoint_bub_ms& point )
{
    if( ter( point + tripoint_east ) != t_open_air && ter( point + tripoint_west ) != t_open_air ) {
        return true;
    }
    if( ter( point + tripoint_south_east ) != t_open_air
        && ter( point + tripoint_north_west ) != t_open_air ) {
        return true;
    }
    if( ter( point + tripoint_south ) != t_open_air && ter( point + tripoint_north ) != t_open_air ) {
        return true;
    }
    if( ter( point + tripoint_north_east ) != t_open_air
        && ter( point + tripoint_south_west ) != t_open_air ) {
        return true;
    }
    return false;
}

static bool will_explode_on_impact( const int power )
{
    const auto explode_threshold = get_option<int>( "MADE_OF_EXPLODIUM" );
    const bool is_explodium = explode_threshold != 0;

    return is_explodium && power >= explode_threshold;
}

void map::smash_trap( const tripoint_bub_ms& p, const int power, const std::string& cause_message )
{
    const trap& tr = get_map().tr_at( p );
    if( tr.is_null() ) { return; }

    const bool is_explosive_trap = !tr.is_benign() && tr.vehicle_data.do_explosion;

    if( !will_explode_on_impact( power ) || !is_explosive_trap ) { return; }
    // make a fake NPC to trigger the trap
    npc dummy;
    dummy.set_fake( true );
    dummy.name = cause_message;
    dummy.setpos( p );
    tr.trigger( p, &dummy );
}

void map::smash_items(
    const tripoint_bub_ms& p, const int power, const std::string& cause_message, bool do_destroy )
{
    if( !has_items( p ) ) { return; }

    // Keep track of how many items have been damaged, and what the first one is
    int items_damaged = 0;
    int items_destroyed = 0;
    std::string damaged_item_name;

    // TODO: Bullets should be pretty much corpse-only
    constexpr const int min_destroy_threshold = 50;

    std::vector<detached_ptr<item>> contents;
    map_stack items = i_at( p );

    items.remove_top_items_with( [&]( detached_ptr<item>&& it ) {
        if( it->has_flag( flag_EXPLOSION_SMASHED ) ) { return std::move( it ); }
        if( will_explode_on_impact( power ) && it->will_explode_in_fire() ) {
            return item::detonate( std::move( it ), p, contents );
        }
        if( it->is_corpse() ) {
            if( ( power < min_destroy_threshold || !do_destroy ) && !it->can_revive()
                && !it->get_mtype()->zombify_into ) {
                return std::move( it );
            }
        }
        bool is_active_explosive = it->is_active() && it->type->get_use( "explosion" ) != nullptr;
        if( is_active_explosive && it->charges == 0 ) { return std::move( it ); }

        const float material_factor = it->chip_resistance( true );
        // Intact non-rezing get a boost
        const float intact_mult =
            2.0f - ( static_cast<float>( it->damage_level( it->max_damage() ) ) / it->max_damage() );
        const float destroy_threshold = min_destroy_threshold + material_factor * intact_mult;
        // For pulping, only consider material resistance. Non-rezing can only be destroyed.
        const float pulp_threshold = it->can_revive() ? material_factor : destroy_threshold;
        // Active explosives that will explode this turn are indestructible (they are exploding
        // "now")
        if( power < pulp_threshold ) { return std::move( it ); }

        bool item_was_destroyed = false;
        float destroy_chance = ( power - pulp_threshold ) / 4.0;

        const bool by_charges = it->count_by_charges();
        if( by_charges ) {
            destroy_chance *= it->charges_per_volume( 250_ml );
            if( x_in_y( destroy_chance, destroy_threshold ) ) { item_was_destroyed = true; }
        } else {
            const field_type_id type_blood =
                it->is_corpse() ? it->get_mtype()->bloodType() : fd_null;
            float roll = rng_float( 0.0, destroy_chance );
            if( roll >= destroy_threshold ) {
                item_was_destroyed = true;
            } else if( roll >= pulp_threshold ) {
                // Only pulp
                it->set_damage( it->max_damage() );
                // TODO: Blood streak cone away from explosion
                add_splash( type_blood, p, 1, destroy_chance );
                // If it was the first item to be damaged, note it
                if( items_damaged == 0 ) { damaged_item_name = it->tname(); }
                items_damaged++;
            }
        }
        if( item_was_destroyed ) {
            // But save the contents, except for irremovable gunmods
            it->contents.remove_top_items_with( [&contents]( detached_ptr<item>&& it ) {
                if( !it->is_irremovable() ) {
                    contents.push_back( std::move( it ) );
                    return detached_ptr<item>();
                } else {
                    return std::move( it );
                }
            } );
            if( items_damaged == 0 ) { damaged_item_name = it->tname(); }

            items_damaged++;
            items_destroyed++;
            return detached_ptr<item>();
        } else {
            return std::move( it );
        }
    } );

    // Let the player know that the item was damaged if they can see it.
    if( items_destroyed > 1 && g->u.sees( p ) ) {
        add_msg( m_bad, _( "The %s destroys several items!" ), cause_message );
    } else if( items_destroyed == 1 && items_damaged == 1 && g->u.sees( p ) ) {
        //~ %1$s: the cause of destruction, %2$s: destroyed item name
        add_msg( m_bad, _( "The %1$s destroys the %2$s!" ), cause_message, damaged_item_name );
    } else if( items_damaged > 1 && g->u.sees( p ) ) {
        add_msg( m_bad, _( "The %s damages several items." ), cause_message );
    } else if( items_damaged == 1 && g->u.sees( p ) ) {
        //~ %1$s: the cause of damage, %2$s: damaged item name
        add_msg( m_bad, _( "The %1$s damages the %2$s." ), cause_message, damaged_item_name );
    }

    for( detached_ptr<item> &it : contents ) { add_item_or_charges( p, std::move( it ) ); }
}

ter_id map::get_roof( const tripoint_bub_ms& p, const bool allow_air ) const
{
    // This function should not be called from the 2D mode
    // Just use t_dirt instead
    assert( zlevels );

    if( p.z() <= -OVERMAP_DEPTH ) {
        // Could be magma/"void" instead
        return t_rock_floor;
    }

    const auto &ter_there = ter( p ).obj();
    const auto &roof = ter_there.roof;
    if( !roof ) {
    // No roof
    if( !allow_air ) {
            // TODO: Biomes? By setting? Forbid and treat as bug?
            if( p.z() < 0 ) { return t_rock_floor_no_roof; }

            return t_dirt;
        }

        return t_open_air;
    }

    ter_id new_ter = roof.id();
    if( new_ter == t_null ) {
    debugmsg( "map::get_new_floor: %d,%d,%d has invalid roof type %s",
              p.x(), p.y(), p.z(), roof.c_str() );
        return t_dirt;
    }

    if( p.z() == -1 && new_ter == t_rock_floor ) {
        // HACK: A hack to work around not having a "solid earth" tile
        new_ter = t_dirt;
    }

    return new_ter;
}

// Check if there is supporting furniture cardinally adjacent to the bashed furniture
// For example, a washing machine behind the bashed door
static bool furn_is_supported( const map& m, const tripoint_bub_ms& p )
{
    const signed char cx[4] = {0, -1, 0, 1};
    const signed char cy[4] = {-1, 0, 1, 0};

    for( int i = 0; i < 4; i++ ) {
        const point_bub_ms adj( p.xy() + point_rel_ms( cx[i], cy[i] ) );
        if( m.has_furn( tripoint_bub_ms( adj, p.z() ) )
            && m.furn( tripoint_bub_ms( adj, p.z() ) ).obj().has_flag( "BLOCKSDOOR" ) ) {
            return true;
        }
    }

    return false;
}

static auto get_sound_volume( const map_bash_info& bash, const bash_params& params ) -> int
{
    // Just take the minimum/base volume at 20dB.
    const auto minvol = 20;
    // Set maxvol to 140dB, which can be deafening for extreme impacts.
    const auto maxvol = 140;
    const auto impact_strength = params.destroy ? bash.str_max : params.strength;
    return bash.sound_vol.value_or( std::clamp( minvol + impact_strength, minvol, maxvol ) );
}

static void set_bash_sound_source( sound_event& se, const bash_params& params )
{
    if( !params.caused_by_player ) {
        return;
    }

    auto& player_character = get_avatar();
    se.from_player = true;
    se.faction = player_character.get_faction()->id();
    se.monfaction = player_character.get_faction()->mon_faction();
}

bash_results map::bash_ter_success( const tripoint_bub_ms &p, const bash_params &params )
{
    bash_results result;
    result.success = true;
    const ter_t &ter_before = ter( p ).obj();
    const map_bash_info &bash = ter_before.bash;
    if( has_flag_ter( "FUNGUS", p ) ) {
        fungal_effects( *g, *this ).create_spores( p );
    }
    const std::string soundfxvariant = ter_before.id.str();
    const bool will_collapse = ter_before.has_flag( TFLAG_SUPPORTS_ROOF );
    const bool suspended = ter_before.has_flag( TFLAG_SUSPENDED );
    bool follow_below = false;
    if( params.bashing_from_above && bash.ter_set_bashed_from_above ) {
        // If this terrain is being bashed from above and this terrain
        // has a valid post-destroy bashed-from-above terrain, set it
        ter_set( p, bash.ter_set_bashed_from_above );
    } else if( bash.ter_set ) {
        // If the terrain has a valid post-destroy terrain, set it
        ter_set( p, bash.ter_set );
        follow_below |= zlevels && bash.bash_below;
    } else if( suspended ) {
        // Its important that we change the ter value before recursing, otherwise we'll hit an infinite loop.
        // This could be prevented by assembling a visited list, but in order to avoid that cost, we're going
        // build our recursion to just be resilient.
        ter_set( p, t_open_air );
        propagate_suspension_check( p );
    } else {
        tripoint_bub_ms below( p.xy(), p.z() - 1 );
        const ter_t &ter_below = ter( below ).obj();
        // Only setting the flag here because we want drops and sounds in correct order
        follow_below |= zlevels && bash.bash_below && ter_below.roof;

        ter_set( p, t_open_air );
    }

    spawn_items( p, item_group::items_from( bash.drop_group, calendar::turn ) );

    if( !bash.sound.empty() && !params.silent ) {
        static const std::string soundfxid = "smash_success";
        const auto sound_volume = get_sound_volume( bash, params );
        sound_event se;
        se.origin = p;
        se.volume = sound_volume;
        se.category = sounds::sound_t::combat;
        se.description = bash.sound.translated();
        se.id = soundfxid;
        se.variant = soundfxvariant;
        set_bash_sound_source( se, params );
        sounds::sound( se );
    }

    if( !zlevels ) {
        if( ter( p ) == t_open_air ) {
            // We destroyed something, so we aren't just "plugging" air with dirt here
            ter_set( p, t_dirt );
        }
    } else if( follow_below || ter( p ) == t_open_air ) {
        const tripoint_bub_ms below( p.xy(), p.z() - 1 );
        // We may need multiple bashes in some weird cases
        // Example:
        //   W has roof A
        //   A bashes to B
        //   B bashes to nothing
        //   Below our point P, there is a W
        // If we bash down a B over a W, it might be from earlier A or just constructed over it!
        //
        // Current solution: bash roof until you reach same roof type twice, then bash down
        if( follow_below && params.do_recurse ) {
            bool blocked_by_roof = false;
            std::set<ter_id> encountered_types;
            encountered_types.insert( ter_before.id );
            encountered_types.insert( t_open_air );
            // Note: we're bashing the new roof, not the tile supported by it!
            int down_bash_tries = 10;
            do {
                const ter_id &ter_now = ter( p );
                if( encountered_types.contains( ter_now ) ) {
                    // We have encountered this type before and destroyed it (didn't block us)
                    ter_set( p, t_open_air );
                    bash_params params_below = params;
                    params_below.bashing_from_above = true;
                    params_below.bash_floor = false;
                    params_below.do_recurse = false;
                    params_below.destroy = true;
                    int impassable_bash_tries = 10;
                    // Unconditionally destroy, but don't go deeper
                    do {
                        result |= bash_ter_success( below, params_below );
                    } while( ter( below )->movecost == 0 && impassable_bash_tries-- > 0 );
                    if( impassable_bash_tries <= 0 ) {
                        debugmsg( "Loop in terrain bashing for type %s", ter_before.id.str() );
                    }
                } else if( ter_now == t_open_air ) {
                    const ter_id &roof = get_roof( below, params.bash_floor && ter( below )->movecost != 0 );
                    if( roof != t_open_air ) {
                        ter_set( p, roof );
                    }
                } else {
                    // This floor/roof tile wasn't destroyed in this loop yet
                    encountered_types.insert( ter_now );
                    bash_params params_copy = params;
                    params_copy.do_recurse = false;
                    // TODO: Unwrap the calls, don't recurse
                    // TODO: Don't bash furn
                    bash_results results_sub = bash_ter_furn( p, params_copy );
                    result |= results_sub;
                    if( !results_sub.success ) {
                        // Blocked, as in "the roof was too strong to bash"
                        blocked_by_roof = true;
                    }
                }
            } while( down_bash_tries-- > 0 && !blocked_by_roof &&
                     ( ter( p ) != t_open_air || ter( p )->movecost == 0 || ter( below )->roof ) );
            if( down_bash_tries <= 0 ) {
                debugmsg( "Loop in terrain bashing for type %s", ter_before.id.str() );
            }
        } else {
            const ter_id &roof = get_roof( below, params.bash_floor && ter( below )->movecost != 0 );

            ter_set( p, roof );
        }
    }

    if( will_collapse && !has_flag( TFLAG_SUPPORTS_ROOF, p ) ) {
        collapse_at( p, params.silent, true, bash.explosive > 0 );
    }

    if( bash.explosive > 0 ) {
        // TODO Implement if the player triggered the explosive terrain
        explosion_handler::explosion( p, nullptr, bash.explosive, 0.8, false );
    }

    return result;
}

bash_results map::bash_furn_success( const tripoint_bub_ms &p, const bash_params &params )
{
    bash_results result;
    const auto &furnid = furn( p ).obj();
    const map_bash_info &bash = furnid.bash;


    if( has_flag_furn( "FUNGUS", p ) ) {
        fungal_effects( *g, *this ).create_spores( p );
    }
    if( has_flag_furn( "MIGO_NERVE", p ) ) {
        map_funcs::migo_nerve_cage_removal( *this, p, true );
    }
    std::string soundfxvariant = furnid.id.str();
    const bool tent = !bash.tent_centers.empty();

    // Special code to collapse the tent if destroyed
    if( tent ) {
        // Get ids of possible centers
        std::set<furn_id> centers;
        for( const auto &cur_id : bash.tent_centers ) {
            if( cur_id.is_valid() ) {
                centers.insert( cur_id );
            }
        }

        std::optional<std::pair<tripoint_bub_ms, furn_id>> tentp;

        // Find the center of the tent
        // First check if we're not currently bashing the center
        if( centers.contains( furn( p ) ) ) {
            tentp.emplace( p, furn( p ) );
        } else {
            for( const tripoint_bub_ms &pt : points_in_radius( p, bash.collapse_radius ) ) {
                const furn_id &f_at = furn( pt );
                // Check if we found the center of the current tent
                if( centers.contains( f_at ) ) {
                    tentp.emplace( pt, f_at );
                    break;
                }
            }
        }
        // Didn't find any tent center, wreck the current tile
        if( !tentp ) {
            spawn_items( p, item_group::items_from( bash.drop_group, calendar::turn ) );
            furn_set( p, bash.furn_set );
        } else {
            // Take the tent down
            const int rad = tentp->second.obj().bash.collapse_radius;
            for( const auto &pt : points_in_radius( tripoint_bub_ms( tentp->first ), rad ) ) {
                const furn_id frn = furn( pt );
                if( frn == f_null ) {
                    continue;
                }

                const auto &furn_obj = frn.obj();
                const auto &recur_bash = furn_obj.bash;
                // Check if we share a center type and thus a "tent type"
                for( const auto &cur_id : recur_bash.tent_centers ) {
                    if( centers.contains( cur_id.id() ) ) {
                        // Found same center, wreck current tile
                        if( furn_obj.fluid_grid &&
                            furn_obj.fluid_grid->role == fluid_grid_role::tank ) {
                            fluid_grid::on_tank_removed( tripoint_abs_ms( bub_to_abs( pt ) ) );
                        }
                        spawn_items( p, item_group::items_from( recur_bash.drop_group, calendar::turn ) );
                        furn_set( pt, recur_bash.furn_set );
                        break;
                    }
                }
            }
        }
        soundfxvariant = "smash_cloth";
    } else {
        if( furnid.fluid_grid && furnid.fluid_grid->role == fluid_grid_role::tank ) {
            fluid_grid::on_tank_removed( tripoint_abs_ms( bub_to_abs( p ) ) );
        }
        furn_set( p, bash.furn_set );
        for( item * const &it : i_at( p ) )  {
            it->on_drop( p, *this );
        }
        // HACK: Hack alert.
        // Signs have cosmetics associated with them on the submap since
        // furniture can't store dynamic data to disk. To prevent writing
        // mysteriously appearing for a sign later built here, remove the
        // writing from the submap.
        delete_signage( p );
    }

    if( !tent ) {
        spawn_items( p, item_group::items_from( bash.drop_group, calendar::turn ) );
    }

    if( !bash.sound.empty() && !params.silent ) {
        static const std::string soundfxid = "smash_success";
        const auto sound_volume = get_sound_volume( bash, params );
        sound_event se;
        se.origin = p;
        se.volume = sound_volume;
        se.category = sounds::sound_t::combat;
        se.description = bash.sound.translated();
        se.id = soundfxid;
        se.variant = soundfxvariant;
        set_bash_sound_source( se, params );
        sounds::sound( se );
    }

    if( bash.explosive > 0 ) {
        // TODO implement if the player triggered the explosive furniture
        explosion_handler::explosion( p, nullptr, bash.explosive, 0.8, false );
    }

    return result;
}

bash_results map::bash_ter_furn( const tripoint_bub_ms& p, const bash_params& params )
{
    bash_results result;
    std::string soundfxvariant;
    const auto& ter_obj = ter( p ).obj();
    const auto& furn_obj = furn( p ).obj();
    bool smash_ter = false;
    const map_bash_info* bash = nullptr;

    if( furn_obj.id && furn_obj.bash.str_max != -1 ) {
        bash = &furn_obj.bash;
        soundfxvariant = furn_obj.id.str();
    } else if( ter_obj.bash.str_max != -1 ) {
        bash = &ter_obj.bash;
        smash_ter = true;
        soundfxvariant = ter_obj.id.str();
    }

    // Floor bashing check
    // Only allow bashing floors when we want to bash floors and we're in z-level mode
    // Unless we're destroying, then it gets a little weird
    if( smash_ter && bash->bash_below && ( !zlevels || !params.bash_floor ) ) {
        if( !params.destroy ) {
            smash_ter = false;
            bash = nullptr;
        } else if( !bash->ter_set && zlevels ) {
            // HACK: A hack for destroy && !bash_floor
            // We have to check what would we create and cancel if it is what we have now
            tripoint_bub_ms below( p.xy(), p.z() - 1 );
            const auto roof = get_roof( below, false );
            if( roof == ter( p ) ) {
                smash_ter = false;
                bash = nullptr;
            }
        } else if( !bash->ter_set && ter( p ) == t_dirt ) {
            // As above, except for no-z-levels case
            smash_ter = false;
            bash = nullptr;
        }
    }

    // TODO: what if silent is true?
    if( has_flag( "ALARMED", p ) && !g->timed_events.queued( TIMED_EVENT_WANTED ) ) {
        sound_event se;
        se.origin = p;
        se.volume = 90;
        se.category = sounds::sound_t::alarm;
        se.description = _( "an alarm go off!" );
        se.id = "environment";
        se.variant = "alarm";
        sounds::sound( se );
        // Blame nearby player
        if( rl_dist( g->u.bub_pos(), p ) <= 3 ) {
            g->events().send<event_type::triggers_alarm>( g->u.getID() );
            const auto abs = project_to<coords::sm>( bub_to_abs( p.xy() ) );
            g->timed_events.add(
                TIMED_EVENT_WANTED, calendar::turn + 30_minutes, 0, tripoint_abs_sm( abs, p.z() ) );
        }
    }

    if( bash == nullptr || ( bash->destroy_only && !params.destroy ) ) {
        // Nothing bashable here
        if( impassable( p ) ) {
            if( !params.silent ) {
                sound_event se;
                se.origin = p;
                se.volume = 80;
                se.category = sounds::sound_t::combat;
                se.description = _( "thump!" );
                se.id = "smash_fail";
                se.variant = "default";
                set_bash_sound_source( se, params );
                sounds::sound( se );
            }

            result.did_bash = true;
            result.bashed_solid = true;
        }

        return result;
    }

    result.did_bash = true;
    result.bashed_solid = true;
    result.success = params.destroy;

    int smin = bash->str_min;
    int smax = bash->str_max;
    if( !params.destroy ) {
        if( bash->str_min_blocked != -1 || bash->str_max_blocked != -1 ) {
            if( furn_is_supported( *this, p ) ) {
                if( bash->str_min_blocked != -1 ) { smin = bash->str_min_blocked; }
                if( bash->str_max_blocked != -1 ) { smax = bash->str_max_blocked; }
            }
        }

        if( bash->str_min_supported != -1 || bash->str_max_supported != -1 ) {
            tripoint_bub_ms below( p.xy(), p.z() - 1 );
            if( !zlevels || has_flag( TFLAG_SUPPORTS_ROOF, below ) ) {
                if( bash->str_min_supported != -1 ) { smin = bash->str_min_supported; }
                if( bash->str_max_supported != -1 ) { smax = bash->str_max_supported; }
            }
        }
        // Linear interpolation from str_min to str_max
        const int resistance = smin + ( params.roll * ( smax - smin ) );
        if( params.strength >= resistance ) { result.success = true; }
    }

    if( !result.success ) {
        // Cap out bash volume to 120dB for sanity checking.
        int sound_volume = std::min( 120, bash->sound_fail_vol.value_or( 70 ) );

        result.did_bash = true;
        if( !params.silent ) {
            sound_event se;
            se.origin = p;
            se.volume = sound_volume;
            se.category = sounds::sound_t::combat;
            se.description = bash->sound_fail.translated();
            se.id = "smash_fail";
            se.variant = soundfxvariant;
            set_bash_sound_source( se, params );
            sounds::sound( se );
        }

        if( !smash_ter && smax > 0 ) {
            const auto flipped_version = get_furn_transforms_into( p );
            if( flipped_version != furn_str_id::NULL_ID() ) {
                const int damage_percent = ( params.strength * 100 ) / smax;
                if( rng( 1, 100 ) <= damage_percent ) { furn_set( p, flipped_version ); }
            }
        }
        // Hard impacts have a chance to dislodge targets perching above
        if( params.strength >= smin / 2 && one_in( smin / 2 ) ) {
            tripoint_bub_ms above( p.xy(), p.z() + 1 );
            Character* character = g->critter_at<Character>( above );
            if( has_flag( TFLAG_UNSTABLE, above ) && character != nullptr ) {
                character->add_msg_if_player( m_warning, _( "You feel the ground beneath you shake "
                                                            "from the impact!" ) );

                if( character->stability_roll() < rng( 1, params.strength - ( smin / 2 ) ) ) {
                    character->add_msg_player_or_npc(
                        m_bad, _( "You lose your balance!" ), _( "<npcname> loses their balance!" ) );

                    g->fling_creature( character, rng_float( 0_degrees, 360_degrees ), 10 );
                }
            }
        }
    } else {
        if( smash_ter ) {
            result |= bash_ter_success( p, params );
        } else {
            result |= bash_furn_success( p, params );
        }
    }

    return result;
}

bash_results map::bash(
    const tripoint_bub_ms& p, const int str, bool silent, bool destroy, bool bash_floor,
    const vehicle* bashing_vehicle )
{
    const auto bsh = bash_params{
        .strength = str,
        .silent = silent,
        .destroy = destroy,
        .bash_floor = bash_floor,
        .roll = static_cast<float>( rng_float( 0, 1.0f ) ),
        .bashing_from_above = false,
        .do_recurse = true
    };
    return bash( p, bsh, bashing_vehicle );
}

bash_results map::bash( const tripoint_bub_ms& p, const bash_params& bsh,
                        const vehicle* bashing_vehicle )
{
    bash_results result;

    // Dimension bounds cannot be bashed - show message from boundary terrain
    if( is_out_of_bounds( tripoint_bub_ms( p ) ) ) {
        if( !bsh.silent && pocket_info_ ) {
            const ter_t& boundary_ter = pocket_info_->bounds.boundary_terrain.obj();
            if( !boundary_ter.bash.sound_fail.empty() ) {
                add_msg( m_info, boundary_ter.bash.sound_fail.translated() );
            }
        }
        return result; // Cannot bash dimension boundary
    }

    bool bashed_sealed = false;
    if( has_flag( "SEALED", p ) ) {
        result |= bash_ter_furn( p, bsh );
        bashed_sealed = true;
    }

    result |= bash_field( p, bsh );

    // Don't bash items inside terrain/furniture with SEALED flag
    if( !bashed_sealed ) { result |= bash_items( p, bsh ); }
    // Don't bash the vehicle doing the bashing
    const vehicle* veh = veh_pointer_or_null( veh_at( p ) );
    if( veh != nullptr && veh != bashing_vehicle ) { result |= bash_vehicle( p, bsh ); }

    // If we still didn't bash anything solid (a vehicle) or a tile with SEALED flag, bash ter/furn
    if( !result.bashed_solid && !bashed_sealed ) { result |= bash_ter_furn( p, bsh ); }

    // HEAD-only: sprite-animation bash shake. Lives in this overload rather than the int one so
    // that callers using the bash_params form (e.g. action_handlers::smash) still shake the tile.
    if( result.did_bash ) {
        note_tile_bash( p ); // any connecting bash, not only destruction
    }

    return result;
}

bash_results map::bash_items( const tripoint_bub_ms& p, const bash_params& params )
{
    bash_results result;
    if( !has_items( p ) ) { return result; }

    std::vector<detached_ptr<item>> smashed_contents;
    auto bashed_items = i_at( p );
    bool smashed_glass = false;
    for( auto bashed_item = bashed_items.begin(); bashed_item != bashed_items.end(); ) {
        // the check for active suppresses Molotovs smashing themselves with their own explosion
        if( ( *bashed_item )->can_shatter() && !( *bashed_item )->is_active() && one_in( 2 ) ) {
            result.did_bash = true;
            smashed_glass = true;
            for( detached_ptr<item> &bashed_content : ( *bashed_item )->contents.clear_items() ) {
                smashed_contents.push_back( std::move( bashed_content ) );
            }
            bashed_item = bashed_items.erase( bashed_item );
        } else {
            ++bashed_item;
        }
    }
    // Now plunk in the contents of the smashed items.
    spawn_items( p, std::move( smashed_contents ) );

    // Add a glass sound even when something else also breaks
    if( smashed_glass && !params.silent ) {
        sound_event se;
        se.origin = p;
        se.volume = 70;
        se.category = sounds::sound_t::combat;
        se.description = _( "glass shattering" );
        se.id = "smash_success";
        se.variant = "smash_glass_contents";
        set_bash_sound_source( se, params );
        sounds::sound( se );
    }
    return result;
}

bash_results map::bash_vehicle( const tripoint_bub_ms &p, const bash_params &params )
{
    bash_results result;
    // Smash vehicle if present
    if( const optional_vpart_position vp = veh_at( p ) ) {
        vp->vehicle().damage( vp->part_index(), params.strength, DT_BASH, true );
        if( !params.silent ) {
            sound_event se;
            se.origin = p;
            se.volume = 70;
            se.category = sounds::sound_t::combat;
            se.description = _( "crash!" );
            se.id = "smash_success";
            se.variant = "hit_vehicle";
            set_bash_sound_source( se, params );
            sounds::sound( se );
        }

        result.did_bash = true;
        result.success = true;
        result.bashed_solid = true;
    }
    return result;
}

bash_results map::bash_field( const tripoint_bub_ms& p, const bash_params & )
{
    bash_results result;
    if( get_field( p, fd_web ) != nullptr ) {
        result.did_bash = true;
        result.bashed_solid = true; // To prevent bashing furniture/vehicles
        remove_field( p, fd_web );
    }

    return result;
}

bash_results &bash_results::operator|=( const bash_results& other )
{
    did_bash |= other.did_bash;
    success |= other.success;
    bashed_solid |= other.bashed_solid;
    subresults.push_back( other );
    return *this;
}

void map::destroy( const tripoint_bub_ms& p, const bool silent )
{
    // Dimension bounds cannot be destroyed
    if( is_out_of_bounds( tripoint_bub_ms( p ) ) ) { return; }

    // Break if it takes more than 25 destructions to remove to prevent infinite loops
    // Example: A bashes to B, B bashes to A leads to A->B->A->...

    // If we were destroying a floor, allow destroying floors
    // If we were destroying something unpassable, destroy only that
    bool was_impassable = impassable( p );
    int count = 0;
    while(
        count <= 25 && bash( p, 999, silent, true ).success && ( !was_impassable || impassable( p ) ) ) {
        count++;
    }
}

void map::destroy_furn( const tripoint_bub_ms& p, const bool silent )
{
    // Break if it takes more than 25 destructions to remove to prevent infinite loops
    // Example: A bashes to B, B bashes to A leads to A->B->A->...
    int count = 0;
    while( count <= 25 && furn( p ) != f_null && bash( p, 999, silent, true ).success ) { count++; }
}

void map::batter( const tripoint_bub_ms& p, int power, int tries, const bool silent )
{
    int count = 0;
    while( count < tries && bash( p, power, silent ).success ) { count++; }
}

void map::crush( const tripoint_bub_ms& p )
{
    player* crushed_player = g->critter_at<player>( p );

    if( crushed_player != nullptr ) {
        bool player_inside = false;
        if( crushed_player->in_vehicle ) {
            const optional_vpart_position vp = veh_at( p );
            player_inside = vp && vp->is_inside();
        }
        if( !player_inside ) { // If there's a player at p and he's not in a covered vehicle...
            // This is the roof coming down on top of us, no chance to dodge
            crushed_player->add_msg_player_or_npc(
                m_bad, _( "You are crushed by the falling debris!" ),
                _( "<npcname> is crushed by the falling debris!" ) );
            // TODO: Make this depend on the ceiling material
            const int dam = rng( 0, 40 );
            // Torso and head take the brunt of the blow
            crushed_player
            ->deal_damage( nullptr, bodypart_id( "head" ), damage_instance( DT_BASH, dam * .25 ) );
            crushed_player
            ->deal_damage( nullptr, bodypart_id( "torso" ), damage_instance( DT_BASH, dam * .45 ) );
            // Legs take the next most through transferred force
            crushed_player
            ->deal_damage( nullptr, bodypart_id( "leg_l" ), damage_instance( DT_BASH, dam * .10 ) );
            crushed_player
            ->deal_damage( nullptr, bodypart_id( "leg_r" ), damage_instance( DT_BASH, dam * .10 ) );
            // Arms take the least
            crushed_player
            ->deal_damage( nullptr, bodypart_id( "arm_l" ), damage_instance( DT_BASH, dam * .05 ) );
            crushed_player
            ->deal_damage( nullptr, bodypart_id( "arm_r" ), damage_instance( DT_BASH, dam * .05 ) );

            // Pin whoever got hit
            crushed_player->add_effect( effect_crushed, 1_turns, bodypart_str_id::NULL_ID() );
            crushed_player->check_dead_state();
        }
    }

    if( monster * const monhit = g->critter_at<monster>( p ) ) {
        // 25 ~= 60 * .45 (torso)
        monhit->deal_damage( nullptr, bodypart_id( "torso" ), damage_instance( DT_BASH, rng( 0, 25 ) ) );

        // Pin whoever got hit
        monhit->add_effect( effect_crushed, 1_turns, bodypart_str_id::NULL_ID() );
        monhit->check_dead_state();
    }

    if( const optional_vpart_position vp = veh_at( p ) ) {
        // Arbitrary number is better than collapsing house roof crushing APCs
        vp->vehicle().damage( vp->part_index(), rng( 100, 1000 ), DT_BASH, false );
    }
}

void map::shoot(
    const tripoint_bub_ms& origin, const tripoint_bub_ms& p, projectile& proj,
    const bool hit_items )
{
    float initial_damage = 0.0;
    float initial_arpen = 0.0;
    float initial_armor_mult = 1.0;
    for( const damage_unit& dam : proj.impact ) {
        initial_damage += dam.amount * dam.damage_multiplier;
        initial_arpen += dam.res_pen;
        initial_armor_mult *= dam.res_mult;
    }
    if( initial_damage < 0 ) { return; }

    float dam = initial_damage;
    float pen = initial_arpen;

    if( has_flag( "ALARMED", p ) && !g->timed_events.queued( TIMED_EVENT_WANTED ) ) {
        sound_event se;
        se.origin = p;
        se.volume = 90;
        se.category = sounds::sound_t::alarm;
        se.description = _( "an alarm sound!" );
        se.id = "environment";
        se.variant = "alarm";
        sounds::sound( se );
        const auto abs = project_to<coords::sm>( bub_to_abs( p ) );
        g->timed_events.add( TIMED_EVENT_WANTED, calendar::turn + 30_minutes, 0, abs );
    }

    const bool inc =
        proj.has_effect( ammo_effect_INCENDIARY ) || proj.impact.type_damage( DT_HEAT ) > 0;
    const bool phys =
        proj.impact.type_damage( DT_BASH ) > 0 || proj.impact.type_damage( DT_CUT ) > 0
        || proj.impact.type_damage( DT_STAB ) > 0 || proj.impact.type_damage( DT_BULLET ) > 0;
    if( const optional_vpart_position vp = veh_at( p ) ) {
        dam = vp->vehicle().damage( vp->part_index(), dam, inc ? DT_HEAT : DT_STAB, hit_items );
    }

    furn_id furn_here = furn( p );
    const auto &furn = furn_here.obj();

    ter_id terrain = ter( p );
    const auto &ter = terrain.obj();

    double range = rl_dist( origin, p );
    const bool point_blank = range <= 1;
    if( furn.bash.ranged ) {
        // Damage cover like a crit if we're breaching at point blank range, otherwise randomize
        // like a normal hit.
        float destroy_roll = point_blank ? dam * 1.5 : dam * rng_float( 0.9, 1.1 );
        const ranged_bash_info& rfi = *furn.bash.ranged;
        if( !hit_items
            && ( !check( rfi.block_unaimed_chance )
                 || ( rfi.block_unaimed_chance < 100_pct && point_blank ) ) ) {
            // Nothing, it's a miss, we're shooting over nearby furniture.
        } else if( proj.has_effect( ammo_effect_NO_PENETRATE_OBSTACLES ) ) {
            // We shot something with a flamethrower or other non-penetrating weapon.
            // Try to bash the obstacle and stop the shot.
            add_msg( _( "The shot strikes the %s!" ), furnname( p ) );
            if( phys ) { bash( p, dam, false ); }
            dam = 0;
        } else if( rfi.reduction_laser && proj.has_effect( ammo_effect_LASER ) ) {
            dam -= std::max(
                       ( rng( rfi.reduction_laser->min, rfi.reduction_laser->max ) - pen )
                       * initial_armor_mult,
                       0.0f );
        } else {
            // Roll damage reduction value, reduce result by arpen, multiply by any armor mult, then
            // finally set to zero if negative result
            const float pen_reduction = rng( rfi.reduction.min, rfi.reduction.max );
            dam = std::max( dam - ( std::max( pen_reduction - pen, 0.0f ) * initial_armor_mult ), 0.0f );
            pen = std::max( 0.0f, pen - pen_reduction );
            // Only print if we hit something we can see enemies through, so we know cover did its
            // job
            if( get_avatar().sees( p ) ) {
                if( dam <= 0 ) {
                    add_msg( _( "The shot is stopped by the %s!" ), furnname( p ) );
                    // Only bother mentioning it punched through if it had any resistance, so zip
                    // through canvas with no message.
                } else if( rfi.reduction.min > 0 ) {
                    add_msg( _( "The shot hits the %s and punches through!" ), furnname( p ) );
                }
            }
            add_msg( m_debug, "%s: damage: %.0f -> %.0f, arpen: %.0f -> %.0f", furn.name(),
                     initial_damage, dam, initial_arpen, pen );
            if( destroy_roll > rfi.destroy_threshold && rfi.reduction.min > 0 ) {
                bash_params params{0, false, true, hit_items, 1.0, false};
                bash_furn_success( p, params );
            }
            if( rfi.flammable && inc ) { add_field( p, fd_fire, 1 ); }
        }
        // Check furniture and terrain separately, if this was an if/else then getting partial cover
        // embedded in a wall would let you fire through it.
    }
    if( ter.bash.ranged ) {
        // New values are used for debug message in case furniture did something.
        float modified_dam = dam;
        float modified_pen = pen;
        // Separate hit roll since damage might have been lowered by furniture first.
        float destroy_roll = point_blank ? dam * 1.5 : dam * rng_float( 0.9, 1.1 );
        const ranged_bash_info& ri = *ter.bash.ranged;
        if( !hit_items
            && ( !check( ri.block_unaimed_chance )
                 || ( ri.block_unaimed_chance < 100_pct && point_blank ) ) ) {
            // Nothing, it's a miss or we're shooting over nearby terrain
        } else if( proj.has_effect( ammo_effect_NO_PENETRATE_OBSTACLES ) ) {
            // We shot something with a flamethrower or other non-penetrating weapon.
            // Try to bash the obstacle if it was a thrown rock or the like, then stop the shot.
            add_msg( _( "The shot strikes the %s!" ), tername( p ) );
            if( phys ) { bash( p, dam, false ); }
            dam = 0;
        } else if( ri.reduction_laser && proj.has_effect( ammo_effect_LASER ) ) {
            dam -= std::max(
                       ( rng( ri.reduction_laser->min, ri.reduction_laser->max ) - pen ) * initial_armor_mult,
                       0.0f );
        } else {
            // Roll damage reduction value, reduce result by arpen, multiply by any armor mult, then
            // finally set to zero if negative result
            const float pen_reduction = rng( ri.reduction.min, ri.reduction.max );
            dam = std::max( dam - ( std::max( pen_reduction - pen, 0.0f ) * initial_armor_mult ), 0.0f );
            pen = std::max( 0.0f, pen - pen_reduction );
            // Only print if we hit something we can see enemies through, so we know cover did its
            // job
            if( get_avatar().sees( p ) ) {
                if( dam <= 0 ) {
                    add_msg( _( "The shot is stopped by the %s!" ), tername( p ) );
                    // Only bother mentioning it punched through if it had any resistance, so zip
                    // through canvas with no message.
                } else if( ri.reduction.min > 0 ) {
                    add_msg( _( "The shot hits the %s and punches through!" ), tername( p ) );
                }
            }
            add_msg( m_debug, "%s: damage: %.0f -> %.0f, arpen: %.0f -> %.0f", ter.name(),
                     modified_dam, dam, modified_pen, pen );
            // Destroy if the damage exceeds threshold, unless the target was meant to be shot
            // through with zero resistance like canvas.
            if( destroy_roll > ri.destroy_threshold && ri.reduction.min > 0 ) {
                bash_params params{0, false, true, hit_items, 1.0, false};
                bash_ter_success( p, params );
            }
            if( ri.flammable && inc ) { add_field( p, fd_fire, 1 ); }
        }
    } else if( impassable( p ) && !is_transparent( p ) ) {
        bash( p, dam, false );
        // TODO: Preserve some residual damage when it makes sense.
        dam = 0;
    }

    for( const ammo_effect_str_id& ae_id : proj.get_ammo_effects() ) {
        const ammo_effect& ae = *ae_id;
        if( ae.trail_field_type ) {
            if( x_in_y( ae.trail_chance, 100 ) ) {
                add_field( p, ae.trail_field_type,
                           rng( ae.trail_intensity_min, ae.trail_intensity_max ) );
            }
        }
    }

    // Check fields?
    const field_entry* fieldhit = get_field( p, fd_web );
    if( fieldhit != nullptr ) {
        if( inc ) {
            add_field( p, fd_fire, fieldhit->get_field_intensity() - 1 );
        } else if( dam > 5 + fieldhit->get_field_intensity() * 5
                   && one_in( 5 - fieldhit->get_field_intensity() ) ) {
            dam -= rng( 1, 2 + ( fieldhit->get_field_intensity() * 2 ) );
            remove_field( p, fd_web );
        }
    }

    // Rescale the damage
    if( dam <= 0 ) {
        proj.impact.damage_units.clear();
        return;
    } else if( dam < initial_damage ) {
        proj.impact.mult_damage( dam / static_cast<double>( initial_damage ) );
    }
    if( pen <= 0 ) {
        for( auto& elem : proj.impact.damage_units ) { elem.res_pen = 0.0f; }
    } else if( pen < initial_arpen ) {
        for( auto& elem : proj.impact.damage_units ) {
            elem.res_pen *= ( pen / static_cast<double>( initial_arpen ) );
        }
    }

    // Projectiles with NO_ITEM_DAMAGE flag won't damage items at all
    if( !hit_items ) { return; }

    // Make sure the message is sensible for the ammo effects. Lasers aren't projectiles.
    std::string damage_message;
    if( proj.has_effect( ammo_effect_LASER ) ) {
        damage_message = _( "laser beam" );
    } else if( proj.has_effect( ammo_effect_LIGHTNING ) ) {
        damage_message = _( "bolt of electricity" );
    } else if( proj.has_effect( ammo_effect_PLASMA ) ) {
        damage_message = _( "bolt of plasma" );
    } else {
        damage_message = _( "flying projectile" );
    }

    smash_trap( p, dam, string_format( _( "The %1$s" ), damage_message ) );
    smash_items( p, dam, damage_message, false );
}

bool map::hit_with_acid( const tripoint_bub_ms& p )
{
    if( passable( p ) ) {
        return false; // Didn't hit the tile!
    }
    const ter_id t = ter( p );
    if( t == t_wall_glass || t == t_wall_glass_alarm || t == t_vat ) {
        ter_set( p, t_floor );
    } else if( t == t_door_c || t == t_door_locked || t == t_door_locked_peep
               || t == t_door_locked_alarm ) {
        if( one_in( 3 ) ) { ter_set( p, t_door_b ); }
    } else if( t == t_door_bar_c || t == t_door_bar_o || t == t_door_bar_locked || t == t_bars
               || t == t_reb_cage ) {
        ter_set( p, t_floor );
        add_msg( m_warning, _( "The metal bars melt!" ) );
    } else if( t == t_door_b ) {
        if( one_in( 4 ) ) {
            ter_set( p, t_door_frame );
        } else {
            return false;
        }
    } else if( t == t_window || t == t_window_alarm || t == t_window_no_curtains ) {
        ter_set( p, t_window_empty );
    } else if( t == t_wax ) {
        ter_set( p, t_floor_wax );
    } else if( t == t_gas_pump || t == t_gas_pump_smashed ) {
        return false;
    } else if( t == t_card_science || t == t_card_military || t == t_card_industrial ) {
        ter_set( p, t_card_reader_broken );
    }
    return true;
}

// returns true if terrain stops fire
bool map::hit_with_fire( const tripoint_bub_ms& p )
{
    if( passable( p ) ) {
        return false; // Didn't hit the tile!
    }

    // non passable but flammable terrain, set it on fire
    if( has_flag( "FLAMMABLE", p ) || has_flag( "FLAMMABLE_ASH", p ) ) { add_field( p, fd_fire, 3 ); }
    return true;
}
