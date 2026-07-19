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

static location_vector<item> nulitems( new fake_item_location() ); // Returned when &i_at() is asked
// for an OOB value
static field nulfield;        // Returned when &field_at() is asked for an OOB value
static level_cache nullcache; // Dummy cache for z-levels outside bounds

// Map class methods.


bool map::has_flag( const std::string& flag, const tripoint_bub_ms& p ) const
{
    return has_flag_ter_or_furn( flag, p ); // Does bound checking
}

bool map::can_put_items( const tripoint_bub_ms &p ) const
{
    if( can_put_items_ter_furn( p ) ) {
    return true;
}
const optional_vpart_position vp = veh_at( p );
return static_cast<bool>( vp.part_with_feature( "CARGO", true ) );
}

bool map::can_put_items_ter_furn( const tripoint_bub_ms& p ) const
{
    return !has_flag( "NOITEM", p ) && !has_flag( "SEALED", p );
}

bool map::has_flag_ter( const std::string& flag, const tripoint_bub_ms& p ) const
{
    return ter( p ).obj().has_flag( flag );
}

bool map::has_flag_furn( const std::string& flag, const tripoint_bub_ms& p ) const
{
    return furn( p ).obj().has_flag( flag );
}

bool map::has_flag_ter_or_furn( const std::string& flag, const tripoint_bub_ms& p ) const
{
    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );

    return current_submap && // FIXME: can be null during mapgen
           ( current_submap->get_ter( l ).obj().has_flag( flag )
             || current_submap->get_furn( l ).obj().has_flag( flag ) );
}

bool map::has_flag( const ter_bitflags flag, const tripoint_bub_ms& p ) const
{
    return has_flag_ter_or_furn( flag, p ); // Does bound checking
}

bool map::has_flag_ter( const ter_bitflags flag, const tripoint_bub_ms& p ) const
{
    return ter( p ).obj().has_flag( flag );
}

bool map::has_flag_furn( const ter_bitflags flag, const tripoint_bub_ms& p ) const
{
    return furn( p ).obj().has_flag( flag );
}

bool map::has_flag_vpart( const std::string& flag, const tripoint_bub_ms& p ) const
{
    const optional_vpart_position vp = veh_at( p );
    return static_cast<bool>( vp.part_with_feature( flag, true ) );
}

bool map::has_flag_furn_or_vpart( const std::string& flag, const tripoint_bub_ms& p ) const
{
    return has_flag_furn( flag, p ) || has_flag_vpart( flag, p );
}

bool map::has_flag_ter_or_furn( const ter_bitflags flag, const tripoint_bub_ms& p ) const
{
    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );

    return current_submap && // FIXME: can be null during mapgen
           ( current_submap->get_ter( l ).obj().has_flag( flag )
             || current_submap->get_furn( l ).obj().has_flag( flag ) );
}

// End of 3D flags

// Bashable - common function


bool map::can_open_door(
    const const_interacting_entity& who, const tripoint_bub_ms& p, bool inside ) const
{

    const auto& ter = this->ter( p ).obj();
    if( ter.open ) { return can_open_door_ter( who, ter, p, inside ); }

    const auto& furn = this->furn( p ).obj();
    if( furn.open ) { return can_open_door_furn( who, furn, p, inside ); }

    const optional_vpart_position vp = veh_at( p );
    if( vp ) { return can_open_door_veh( who, vp, p, inside ); }

    return false;
}


bool map::open_door( const interacting_entity& who, const tripoint_bub_ms& p, const bool inside )
{
    const auto& ter = this->ter( p ).obj();
    if( ter.open ) { return open_door_ter( who, ter, p, inside ); }

    const auto& furn = this->furn( p ).obj();
    if( furn.open ) { return open_door_furn( who, furn, p, inside ); }

    const optional_vpart_position vp = veh_at( p );
    if( vp ) { return open_door_veh( who, vp, p, inside ); }
    return false;
}

struct can_open_while_mounted {
    template <typename T> auto operator()( T u ) -> bool {
        constexpr auto is_const_char = std::is_same_v<Character *, T>;
        constexpr auto is_char = std::is_same_v<const Character *, T>;
        if constexpr( is_const_char || is_char ) {
            if( u->is_mounted() ) {
                auto mon = u->mounted_creature.get();
                if( !mon->has_flag( MF_MOUNTABLE_DOORS ) ) {
                    u->add_msg_if_player( m_info, _( "You can't open things while you're riding." ) );
                    return false;
                }
            }
        }
        return true;
    };
};

bool map::can_open_door_ter(
    const const_interacting_entity& who, const ter_t &, const tripoint_bub_ms& p,
    bool inside ) const
{

    if( has_flag( str_OPENCLOSE_INSIDE, p ) && !inside ) {
    return false;
}

if( !std::visit( can_open_while_mounted{}, who ) ) {
    return false;
}

return true;
}


bool map::open_door_ter(
    const interacting_entity& who, const ter_t& ter, const tripoint_bub_ms& p, const bool inside )
{
    if( !can_open_door_ter( static_variant_cast<const_interacting_entity>( who ), ter, p, inside ) ) {
        return false;
    }

    sounds::sound( p, 6, sounds::sound_t::movement, _( "swish" ), true, "open_door", ter.id.str() );
    ter_set( p, ter.open );

    const auto is_schizo = std::visit( []<typename T>( T u ) -> bool {
        if constexpr( std::is_same_v<T, Character *> )
    {
        return u->has_trait( trait_id( "SCHIZOPHRENIC" ) ) || u->has_artifact_with( AEP_SCHIZO );
        }
        return false;
    }, who );

    const tripoint_bub_ms you_pos = std::visit( []<typename T>( T u ) { return u->bub_pos(); }, who );

    if( is_schizo && one_in( 50 ) && !ter.has_flag( "TRANSPARENT" ) ) {
        // This math is schizophrenic
        const tripoint_bub_ms mp =
            p + -2 * you_pos.xy().raw() + tripoint_rel_ms( 2 * p.x(), 2 * p.y(), p.z() );
        g->spawn_hallucination( mp );
    }

    return true;
}

bool map::can_open_door_furn(
    const const_interacting_entity& who, const furn_t &, const tripoint_bub_ms& p,
    bool inside ) const
{

    if( has_flag( str_OPENCLOSE_INSIDE, p ) && !inside ) {
    return false;
}

if( !std::visit( can_open_while_mounted{}, who ) ) {
    return false;
}

return true;
}


bool map::open_door_furn(
    const interacting_entity& who, const furn_t &furn, const tripoint_bub_ms& p,
    const bool inside )
{
    if( !can_open_door_furn( static_variant_cast<const_interacting_entity>( who ), furn, p, inside ) ) {
        return false;
    }

    sounds::sound( p, 6, sounds::sound_t::movement, _( "swish" ), true, "open_door", furn.id.str() );
    furn_set( p, furn.open );
    return true;
}

bool map::can_open_door_veh(
    const const_interacting_entity& who, const optional_vpart_position& vp, const tripoint_bub_ms &,
    bool inside ) const
{

    const int openable = vp->vehicle().next_part_to_open( vp->part_index(), !inside );
    if( openable < 0 ) {
        const int openable_other_way = vp->vehicle().next_part_to_open( vp->part_index(), !inside );
        if( openable_other_way >= 0 ) {
            const auto you =
            std::visit( []( auto&& v ) { return static_cast<const Creature*>( v ); }, who );
            if( inside ) {
                you->add_msg_if_player(
                    _( "The %1$s's %2$s can only be opened from outside." ), vp->vehicle().name,
                    vp->vehicle().part_info( vp->part_index() ).name() );
            } else {
                you->add_msg_if_player(
                    _( "The %1$s's %2$s can only be opened from inside." ), vp->vehicle().name,
                    vp->vehicle().part_info( vp->part_index() ).name() );
            }
        }
        return false;
    }

    if( !std::visit( can_open_while_mounted{}, who ) ) { return false; }

    return true;
}


bool map::open_door_veh(
    const interacting_entity& who, const optional_vpart_position& vp, const tripoint_bub_ms& p,
    bool inside )
{
    if( !can_open_door_veh( static_variant_cast<const_interacting_entity>( who ), vp, p, inside ) ) {
        return false;
    }

    if( std::holds_alternative<Character * >( who ) ) {
        auto& you = *std::get<Character *>( who );
        if( you.is_avatar() && !vp->vehicle().handle_potential_theft( *you.as_avatar() ) ) {
            return false;
        }
    }

    const auto is_owner = std::visit(
    [&]<typename T>( T u ) -> bool {
        if constexpr( std::is_same_v<T, Character *> )
    {
        return vp->vehicle().is_owned_by( *u );
        }
        return false;
    },
    who );

    const auto lock_part = vp.part_with_feature( str_DOOR_LOCKING, true );
    const bool has_locked_door = lock_part.has_value() && lock_part.value().part().enabled;
    // vehicle::is_locked = you have no keys / vehicle has not been hotwired yet
    // unrelated to the door lock itself
    if( has_locked_door && ( !is_owner || vp->vehicle().is_locked ) ) {

        const auto& veh = vp->vehicle();
        const auto you = std::visit( []( auto&& v ) { return static_cast<const Creature*>( v ); }, who );
        const auto dpart = veh.next_part_to_open( vp->part_index(), !inside );
        you->add_msg_if_player(
            _( "The %1$s's %2$s is locked." ), veh.name, veh.part_info( dpart ).name() );

        return false;
    }

    const int openable = vp->vehicle().next_part_to_open( vp->part_index(), !inside );
    vp->vehicle().open_all_at( openable );
    return true;
}

void map::translate( const ter_id& from, const ter_id& to )
{
    if( from == to ) {
        debugmsg( "map::translate %s => %s", from.obj().name(), from.obj().name() );
        return;
    }
    for( const tripoint_bub_ms& p : points_on_zlevel() ) {
        if( ter( p ) == from ) { ter_set( p, to ); }
    }
}

// This function performs the translate function within a given radius of the player.
void map::translate_radius(
    const ter_id& from, const ter_id& to, float radi, const tripoint_bub_ms& p,
    const bool same_submap, const bool toggle_between )
{
    if( from == to ) {
        debugmsg( "map::translate %s => %s", from.obj().name(), to.obj().name() );
        return;
    }

    const tripoint_abs_omt abs_omt_p = project_to<coords::omt>( bub_to_abs( p ) );
    for( const tripoint_bub_ms& t : points_on_zlevel() ) {
        const tripoint_abs_omt abs_omt_t = project_to<coords::omt>( bub_to_abs( t ) );
        const float radiX = trig_dist( p, t );
        if( ter( t ) == from ) {
            // within distance, and either no submap limitation or same overmap coords.
            if( radiX <= radi && ( !same_submap || abs_omt_t == abs_omt_p ) ) { ter_set( t, to ); }
        } else if( toggle_between && ter( t ) == to ) {
            if( radiX <= radi && ( !same_submap || abs_omt_t == abs_omt_p ) ) { ter_set( t, from ); }
        }
    }
}

bool map::close_door( const tripoint_bub_ms& p, const bool inside, const bool check_only )
{
    if( has_flag( str_OPENCLOSE_INSIDE, p ) && !inside ) { return false; }

    const auto& ter = this->ter( p ).obj();
    const auto& furn = this->furn( p ).obj();
    if( ter.close && !furn.id ) {
        if( !check_only ) {
            sounds::sound(
                p, 10, sounds::sound_t::movement, _( "swish" ), true, "close_door", ter.id.str() );
            ter_set( p, ter.close );
        }
        return true;
    } else if( furn.close ) {
        if( !check_only ) {
            sounds::sound(
                p, 10, sounds::sound_t::movement, _( "swish" ), true, "close_door", furn.id.str() );
            furn_set( p, furn.close );
        }
        return true;
    }
    return false;
}

std::string map::get_signage( const tripoint_bub_ms& p ) const
{
    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );

    if( current_submap == nullptr ) { return ""; }
    return current_submap->get_signage( l );
}
void map::set_signage( const tripoint_bub_ms& p, const std::string& message ) const
{
    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );
    if( current_submap == nullptr ) { return; }
    current_submap->set_signage( l, message );
}
void map::delete_signage( const tripoint_bub_ms& p ) const
{
    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );
    if( current_submap == nullptr ) { return; }
    current_submap->delete_signage( l );
}

int map::get_radiation( const tripoint_bub_ms& p ) const
{
    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );
    if( current_submap == nullptr ) { return 0; }
    return current_submap->get_radiation( l );
}

void map::set_radiation( const tripoint_bub_ms& p, const int value )
{
    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );
    if( current_submap == nullptr ) { return; }
    current_submap->set_radiation( l, value );
}

void map::adjust_radiation( const tripoint_bub_ms& p, const int delta )
{
    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );
    if( current_submap == nullptr ) { return; }
    int current_radiation = current_submap->get_radiation( l );
    current_submap->set_radiation( l, current_radiation + delta );
}

int map::get_temperature( const tripoint_bub_ms &p ) const
{
    if( is_out_of_bounds( tripoint_bub_ms( p ) ) ) {
    return 0;
}

const submap *sm = get_submap_at( tripoint_bub_ms( p ) );
if( !sm ) {
    return 0;
}
return sm->get_temperature();
}

void map::set_temperature( const tripoint_bub_ms& p, int new_temperature )
{
    if( is_out_of_bounds( p ) ) { return; }

    submap* sm = get_submap_at( p );
    if( !sm ) { return; }
    sm->set_temperature( new_temperature );
}
// Items: 3D


bool map::can_see_trap_at( const tripoint_bub_ms& p, const Character& c ) const
{
    return tr_at( p ).can_see( p, c );
}

const trap &map::tr_at( const tripoint_bub_ms &p ) const
{
    if( is_out_of_bounds( tripoint_bub_ms( p ) ) ) {
    return tr_null.obj();
    }

    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );

    if( current_submap == nullptr ) {
    return tr_null.obj();
    }

    if( current_submap->get_ter( l ).obj().trap != tr_null ) {
    return current_submap->get_ter( l ).obj().trap.obj();
    }

    return current_submap->get_trap( l ).obj();
}

partial_con *map::partial_con_at( const tripoint_bub_ms& p )
{
    if( is_out_of_bounds( p ) ) { return nullptr; }
    point_sm_ms l;
    submap* const current_submap = get_submap_at( p, l );
    if( current_submap == nullptr ) { return nullptr; }
    auto it = current_submap->partial_constructions.find( tripoint_sm_ms( l, p.z() ) );
    if( it != current_submap->partial_constructions.end() ) { return &*it->second; }
    return nullptr;
}

void map::partial_con_remove( const tripoint_bub_ms& p )
{
    if( is_out_of_bounds( tripoint_bub_ms( p ) ) ) { return; }
    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );
    if( current_submap == nullptr ) { return; }
    current_submap->partial_constructions.erase( tripoint_sm_ms( l, p.z() ) );
}

void map::partial_con_set( const tripoint_bub_ms& p, std::unique_ptr<partial_con> con )
{
    if( is_out_of_bounds( tripoint_bub_ms( p ) ) ) { return; }
    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );
    if( current_submap == nullptr ) { return; }
    if( !current_submap->partial_constructions.emplace( tripoint_sm_ms( l, p.z() ), std::move( con ) )
        .second ) {
        debugmsg( "set partial con on top of terrain which already has a partial con" );
    }
}

void map::trap_set( const tripoint_bub_ms& p, const trap_id& type )
{
    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );
    if( current_submap == nullptr ) { return; }
    const ter_t& ter = current_submap->get_ter( l ).obj();
    if( ter.trap != tr_null ) {
        debugmsg( "set trap %s on top of terrain %s which already has a builit-in trap",
                  type.obj().name(), ter.name() );
        return;
    }

    // If there was already a trap here, remove it.
    if( current_submap->get_trap( l ) != tr_null ) { remove_trap( p ); }

    current_submap->set_trap( l, type );
    if( type.obj().is_funnel() ) {
        const tripoint sm_abs( abs_sub.x() + p.x() / SEEX, abs_sub.y() + p.y() / SEEY, p.z() );
        funnel_locations_.emplace_back( sm_abs, l.raw() );
    }
}

void map::disarm_trap( const tripoint_bub_ms& p )
{
    const trap& tr = tr_at( p );
    if( tr.is_null() ) {
        debugmsg( "Tried to disarm a trap where there was none (%d %d %d)", p.x(), p.y(), p.z() );
        return;
    }

    const int tSkillLevel = g->u.get_skill_level( skill_traps );
    const int diff = tr.get_difficulty();
    const int tReward = diff + tr.get_avoidance();
    int roll = rng( tSkillLevel, 4 * tSkillLevel );

    // Some traps are not actual traps. Skip the rolls, different message and give the option to
    // grab it right away.
    if( tr.get_avoidance() == 0 && tr.get_difficulty() == 0 ) {
        add_msg( _( "The %s is taken down." ), tr.name() );
        tr.on_disarmed( *this, p );
        return;
    }

    ///\EFFECT_PER increases chance of disarming trap

    ///\EFFECT_DEX increases chance of disarming trap

    ///\EFFECT_TRAPS increases chance of disarming trap
    while( ( rng( 5, 20 ) < g->u.per_cur || rng( 1, 20 ) < g->u.dex_cur ) && roll < 50 ) { roll++; }
    if( roll >= diff ) {
        add_msg( _( "You disarm the trap!" ) );
        const int morale_buff = tr.get_avoidance() * 0.4 + tr.get_difficulty() + rng( 0, 4 );
        g->u.rem_morale( MORALE_FAILURE );
        g->u.add_morale( MORALE_ACCOMPLISHMENT, morale_buff, 40 );
        tr.on_disarmed( *this, p );
        if( diff > 1.25 * tSkillLevel ) { // failure might have set off trap
            g->u.practice( skill_traps, tReward );
        }
    } else if( roll >= diff * .8 ) {
        add_msg( _( "You fail to disarm the trap." ) );
        const int morale_debuff = -rng( 6, 18 );
        g->u.rem_morale( MORALE_ACCOMPLISHMENT );
        g->u.add_morale( MORALE_FAILURE, morale_debuff, -40 );
        if( diff > 1.25 * tSkillLevel ) { g->u.practice( skill_traps, tReward / 2 ); }
    } else {
        add_msg( m_bad, _( "You fail to disarm the trap, and you set it off!" ) );
        const int morale_debuff = -rng( 12, 24 );
        g->u.rem_morale( MORALE_ACCOMPLISHMENT );
        g->u.add_morale( MORALE_FAILURE, morale_debuff, -40 );
        tr.trigger( p, &g->u );
        g->u.practice( skill_traps, tReward / 4 );
    }
    g->u.mod_moves( -100 );
}

void map::remove_trap( const tripoint_bub_ms& p )
{
    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );
    if( current_submap == nullptr ) { return; }

    trap_id tid = current_submap->get_trap( l );
    if( tid != tr_null ) {
        if( g != nullptr && this == &get_map() ) { g->u.add_known_trap( p, tr_null.obj() ); }

        if( tid.obj().is_funnel() ) {
            const tripoint_abs_sm
            sm_abs( abs_sub.x() + p.x() / SEEX, abs_sub.y() + p.y() / SEEY, p.z() );
            std::erase_if( funnel_locations_, [&]( const auto & e ) {
                return e.first == sm_abs && e.second == l;
            } );
        }

        current_submap->set_trap( l, tr_null );
    }
}
/*
 * Get wrapper for all fields at xyz
 */
const field &map::field_at( const tripoint_bub_ms &p ) const
{
    if( is_out_of_bounds( tripoint_bub_ms( p ) ) ) {
    nulfield = field();
        return nulfield;
    }

    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );

    if( current_submap == nullptr ) {
    nulfield = field();
        return nulfield;
    }

    return current_submap->get_field( l );
}

/*
 * As above, except not const
 */
field &map::field_at( const tripoint_bub_ms& p )
{
    if( is_out_of_bounds( tripoint_bub_ms( p ) ) ) {
        nulfield = field();
        return nulfield;
    }

    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );

    if( current_submap == nullptr ) {
        nulfield = field();
        return nulfield;
    }

    return current_submap->get_field( l );
}

time_duration map::mod_field_age(
    const tripoint_bub_ms& p, const field_type_id& type, const time_duration& offset )
{
    return set_field_age( p, type, offset, true );
}

int map::mod_field_intensity(
    const tripoint_bub_ms& p, const field_type_id& type, const int offset )
{
    return set_field_intensity( p, type, offset, true );
}

time_duration map::set_field_age(
    const tripoint_bub_ms& p, const field_type_id& type, const time_duration& age,
    const bool isoffset )
{
    if( field_entry * const field_ptr = get_field( p, type ) ) {
        return field_ptr->set_field_age( ( isoffset ? field_ptr->get_field_age() : 0_turns ) + age );
    }
    return -1_turns;
}

/*
 * set intensity of field type at point, creating if not present, removing if intensity is 0
 * returns resulting intensity, or 0 for not present
 */
int map::set_field_intensity(
    const tripoint_bub_ms& p, const field_type_id& type, const int new_intensity, bool isoffset )
{
    field_entry* field_ptr = get_field( p, type );
    if( field_ptr != nullptr ) {
        int adj = ( isoffset ? field_ptr->get_field_intensity() : 0 ) + new_intensity;
        if( adj > 0 ) {
            field_ptr->set_field_intensity( adj );
            return adj;
        } else {
            remove_field( p, type );
            return 0;
        }
    } else if( 0 + new_intensity > 0 ) {
        return add_field( p, type, new_intensity ) ? new_intensity : 0;
    }

    return 0;
}

time_duration map::get_field_age( const tripoint_bub_ms& p, const field_type_id& type ) const
{
    auto field_ptr = field_at( p ).find_field( type );
    return field_ptr == nullptr ? -1_turns : field_ptr->get_field_age();
}

int map::get_field_intensity( const tripoint_bub_ms& p, const field_type_id& type ) const
{
    auto field_ptr = field_at( p ).find_field( type );
    return ( field_ptr == nullptr ? 0 : field_ptr->get_field_intensity() );
}


bool map::has_field_at( const tripoint_bub_ms& p, bool check_bounds )
{
    if( check_bounds && is_out_of_bounds( tripoint_bub_ms( p ) ) ) { return false; }
    point_sm_ms l;
    const submap* sm = get_submap_at( tripoint_bub_ms( p ), l );
    return sm != nullptr && sm->field_count > 0;
}

field_entry *map::get_field( const tripoint_bub_ms& p, const field_type_id& type )
{
    if( !has_field_at( p, false ) ) { return nullptr; }

    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );

    return current_submap->get_field( l ).find_field( type );
}

bool map::dangerous_field_at( const tripoint_bub_ms& p )
{
    for( auto& pr : field_at( p ) ) {
        auto& fd = pr.second;
        if( fd.is_dangerous() ) { return true; }
    }
    return false;
}

bool map::add_field(
    const tripoint_bub_ms& p, const field_type_id& type_id, int intensity, const time_duration& age,
    bool hit_player )
{
    if( !type_id ) {
        debugmsg( "Tried to add null field" );
        return false;
    }

    const field_type& fd_type = *type_id;
    intensity = std::min( intensity, fd_type.get_max_intensity() );
    if( intensity <= 0 ) { return false; }

    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );
    if( current_submap == nullptr ) { return false; }
    current_submap->is_uniform = false;
    invalidate_max_populated_zlev( p.z() );

    if( current_submap->get_field( l ).add_field( type_id, intensity, age ) ) {
        // Only adding it to the count if it doesn't exist.
        current_submap->field_count++;
        current_submap->field_cache.push_back( l );
    }

    if( hit_player ) {
        Character& player_character = get_player_character();
        if( g != nullptr && this == &get_map() && p == player_character.bub_pos() ) {
            // Hit the player with the field if it spawned on top of them.
            creature_in_field( player_character );
        }
    }

    // Dirty the transparency cache now that field processing doesn't always do it
    if( fd_type.dirty_transparency_cache || !fd_type.is_transparent() ) {
        set_transparency_cache_dirty( p );
        set_seen_cache_dirty( p );
    }

    if( fd_type.is_dangerous() ) { set_pathfinding_cache_dirty( p ); }

    // Ensure blood type fields don't hang in the air
    if( zlevels && fd_type.accelerated_decay ) { support_dirty( p ); }

    return true;
}

void map::remove_field( const tripoint_bub_ms& p, const field_type_id& field_to_remove )
{
    point_sm_ms l;
    submap* const current_submap = get_submap_at( tripoint_bub_ms( p ), l );
    if( current_submap == nullptr ) { return; }

    if( current_submap->get_field( l ).remove_field( field_to_remove ) ) {
        // Only adjust the count if the field actually existed.
        --current_submap->field_count;
        const auto& fdata = field_to_remove.obj();
        if( fdata.dirty_transparency_cache || !fdata.is_transparent() ) {
            set_transparency_cache_dirty( p );
            set_seen_cache_dirty( p );
        }
        if( fdata.is_dangerous() ) { set_pathfinding_cache_dirty( p ); }
    }
}
