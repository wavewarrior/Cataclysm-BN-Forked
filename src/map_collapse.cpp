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

