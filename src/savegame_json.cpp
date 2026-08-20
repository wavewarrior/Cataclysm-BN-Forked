// Associated headers here are the ones for which their only non-inline
// functions are serialization functions.  This allows IWYU to check the
// includes in such headers.
#include "coordinates.h"
#include "enums.h" // IWYU pragma: associated
#include "npc_favor.h" // IWYU pragma: associated
#include "pldata.h" // IWYU pragma: associated

#include <algorithm>
#include <array>
#include <bitset>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <set>
#include <sstream>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "active_item_cache.h"
#include "activity_actor.h"
#include "assign.h"
#include "auto_pickup.h"
#include "avatar.h"
#include "bionics.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_cartesian_product.h"
#include "cata_io.h"
#include "cata_variant.h"
#include "cata_utility.h"
#include "character.h"
#include "character_encumbrance.h"
#include "character_id.h"
#include "character_martial_arts.h"
#include "clone_ptr.h"
#include "clzones.h"
#include "computer.h"
#include "construction.h"
#include "consumption.h"
#include "craft_command.h"
#include "creature.h"
#include "creature_tracker.h"
#include "debug.h"
#include "drop_token.h"
#include "effect.h"
#include "enum_conversions.h"
#include "event.h"
#include "faction.h"
#include "field.h"
#include "field_type.h"
#include "flag.h"
#include "flat_set.h"
#include "game.h"
#include "game_constants.h"
#include "int_id.h"
#include "inventory.h"
#include "item.h"
#include "world_type.h"
#include "item_contents.h"
#include "item_factory.h"
#include "itype.h"
#include "json.h"
#include "kill_tracker.h"
#include "light_emission.h"
#include "lru_cache.h"
#include "magic.h"
#include "magic_teleporter_list.h"
#include "map.h"
#include "map_memory.h"
#include "mapdata.h"
#include "mattack_common.h"
#include "mission.h"
#include "monster.h"
#include "morale.h"
#include "morale_types.h"
#include "mtype.h"
#include "mutation.h"
#include "newcharacter.h"
#include "npc.h"
#include "npc_class.h"
#include "options.h"
#include "overmapbuffer.h"
#include "pickup_token.h"
#include "pimpl.h"
#include "player.h"
#include "player_activity.h"
#include "point.h"
#include "profession.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "relic.h"
#include "requirements.h"
#include "rng.h"
#include "scenario.h"
#include "skill.h"
#include "stats_tracker.h"
#include "stomach.h"
#include "string_id.h"
#include "submap.h"
#include "text_snippets.h"
#include "tileray.h"
#include "trait_group.h"
#include "units.h"
#include "uistate.h"
#include "value_ptr.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vitamin.h"
#include "vpart_position.h"
#include "vpart_range.h"

static const efftype_id effect_riding( "riding" );

static const itype_id itype_battery( "battery" );
static const itype_id itype_rad_badge( "rad_badge" );
static const itype_id itype_radio( "radio" );
static const itype_id itype_radio_on( "radio_on" );

static const std::array<std::string, NUM_OBJECTS> obj_type_name = { { "OBJECT_NONE", "OBJECT_ITEM", "OBJECT_ACTOR", "OBJECT_PLAYER",
        "OBJECT_NPC", "OBJECT_MONSTER", "OBJECT_VEHICLE", "OBJECT_TRAP", "OBJECT_FIELD",
        "OBJECT_TERRAIN", "OBJECT_FURNITURE"
    }
};

// TODO: investigate serializing other members of the Creature class hierarchy
static void serialize( const weak_ptr_fast<monster> &obj, JsonOut &jsout )
{
    if( const auto monster_ptr = obj.lock() ) {
        jsout.start_object();

        jsout.member( "monster_at", monster_ptr->bub_pos() );
        // TODO: if monsters/Creatures ever get unique ids,
        // create a differently named member, e.g.
        //     jsout.member("unique_id", monster_ptr->getID());
        jsout.end_object();
    } else {
        // Monster went away. It's up the activity handler to detect this.
        jsout.write_null();
    }
}

static void deserialize( weak_ptr_fast<monster> &obj, JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    tripoint_bub_ms temp_pos;

    obj.reset();
    if( data.read( "monster_at", temp_pos ) ) {
        const auto monp = g->critter_tracker->find( temp_pos );

        if( monp == nullptr ) {
            debugmsg( "no monster found at %d,%d,%d", temp_pos.x(), temp_pos.y(), temp_pos.z() );
            return;
        }

        obj = monp;
    }

    // TODO: if monsters/Creatures ever get unique ids,
    // look for a differently named member, e.g.
    //     data.read( "unique_id", unique_id );
    //     obj = g->id_registry->from_id( unique_id)
    //    }
}

void item_contents::serialize( JsonOut &json ) const
{
    if( !items.empty() ) {
    json.start_object();

        json.member( "items", items );

        json.end_object();
    }
}

void item_contents::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    data.read( "items", items );
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///// player_activity.h

void player_activity::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "type", type );

    if( !type.is_null() ) {
    json.member( "actor", actor );
        json.member( "index", index );
        json.member( "position", position );
        json.member( "coords", coords );
        json.member( "coord_set", coord_set );
        json.member( "name", name );
        json.member( "targets", targets );
        json.member( "placement", placement );
        json.member( "values", values );
        json.member( "str_values", str_values );
        json.member( "auto_resume", auto_resume );
        json.member( "monsters", monsters );
        json.member( "tools", tools_ );
        json.member( "moves_total", moves_total );
        json.member( "moves_left", moves_left );
        json.member( "assistants_ids", assistants_ids_ );
    }
    json.end_object();
}

void player_activity::deserialize( JsonIn &jsin )
{
    static const activity_id ACT_MIGRATION_CANCEL( "ACT_MIGRATION_CANCEL" );
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    data.read( "type", type );

    if( type.is_null() ) {
        return;
    }

    const bool has_actor = activity_actors::deserialize_functions.contains( type );

    // Handle migration of pre-activity_actor activities
    // ACT_MIGRATION_CANCEL will clear the backlog and reset npc state
    // this may cause inconvenience but should avoid any lasting damage to npcs
    if( has_actor && type != ACT_MIGRATION_CANCEL ) {
        if( !data.has_member( "actor" ) || data.has_null( "actor" ) ) {
            type = ACT_MIGRATION_CANCEL;
        } else {
            auto actor = data.get_object( "actor" );
            actor.allow_omitted_members();
            if( !actor.has_member( "actor_data" ) ) {
                type = ACT_MIGRATION_CANCEL;
            } else if( !actor.has_null( "actor_data" ) ) {
                auto a_data = actor.get_object( "actor_data" );
                a_data.allow_omitted_members();
                if( !a_data.has_member( "progress" ) ) {
                    type = ACT_MIGRATION_CANCEL;
                }
            }
        }
    } else {
        data.read( "moves_total", moves_total );
        int ml = data.get_int( "moves_left" );
        if( ml <= 0 ) {
            type = ACT_MIGRATION_CANCEL;
        } else {
            moves_left = ml;
        }
    }
    if( type != ACT_MIGRATION_CANCEL ) {
        data.read( "actor", actor );
    }
    data.read( "index", index );
    data.read( "position", position );
    data.read( "coords", coords );
    data.read( "coord_set", coord_set );
    data.read( "name", name );
    data.read( "targets", targets );
    data.read( "placement", placement );
    values = data.get_int_array( "values" );
    str_values = data.get_string_array( "str_values" );
    data.read( "auto_resume", auto_resume );
    data.read( "monsters", monsters );
    data.read( "tools", tools_ );
    data.read( "assistants_ids", assistants_ids_ );

}

void progress_counter::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "moves_total", moves_total );
    json.member( "moves_left", moves_left );
    json.member( "idx", idx );
    json.member( "total_tasks", total_tasks );
    json.member( "targets", targets );
    json.end_object();
}

void progress_counter::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    data.read( "moves_total", moves_total );
    data.read( "moves_left", moves_left );
    data.read( "idx", idx );
    data.read( "total_tasks", total_tasks );
    auto arr = data.get_array( "targets" );
    for( JsonObject target : arr ) {
        targets.emplace_back( simple_task{
            .target_name = target.get_string( "target_name" ),
            .moves_total = target.get_int( "moves_total" ),
            .moves_left = target.get_int( "moves_left" ) } );
    }
}

void simple_task::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "target_name", target_name );
    json.member( "moves_total", moves_total );
    json.member( "moves_left", moves_left );
    json.end_object();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///// requirements.h
void requirement_data::serialize( JsonOut &json ) const
{
    json.start_object();

    if( !is_null() ) {
    json.member( "blacklisted", blacklisted );
        const std::vector<std::vector<item_comp>> req_comps = get_components();
        const std::vector<std::vector<tool_comp>> tool_comps = get_tools();
        const std::vector<std::vector<quality_requirement>> quality_comps = get_qualities();

        json.member( "req_comps_total", req_comps );

        json.member( "tool_comps_total", tool_comps );

        json.member( "quality_comps_total", quality_comps );
    }
    json.end_object();
}

void requirement_data::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();

    data.read( "blacklisted", blacklisted );

    data.read( "req_comps_total", components );
    data.read( "tool_comps_total", tools );
    data.read( "quality_comps_total", qualities );

}

////////////////////////////////////////////////////////////////////////////////////////////////////
///// skill.h
void SkillLevel::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "level", level() );
    json.member( "exercise", exercise( true ) );
    json.member( "istraining", isTraining() );
    json.member( "lastpracticed", _lastPracticed );
    json.member( "highestlevel", highestLevel() );
    json.end_object();
}

void SkillLevel::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    data.read( "level", _level );
    data.read( "exercise", _exercise );
    data.read( "istraining", _isTraining );
    if( !data.read( "lastpracticed", _lastPracticed ) ) {
        _lastPracticed = calendar::start_of_cataclysm + time_duration::from_hours(
                             get_option<int>( "INITIAL_TIME" ) );
    }
    data.read( "highestlevel", _highestLevel );
    if( _highestLevel < _level ) {
        _highestLevel = _level;
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///// character_id.h

void character_id::serialize( JsonOut &jsout ) const
{
    jsout.write( value );
}

void character_id::deserialize( JsonIn &jsin )
{
    value = jsin.get_int();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///// Character.h, avatar + npc

void char_trait_data::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "key", key );
    json.member( "charge", charge );
    json.member( "powered", powered );
    json.member( "show_sprite", show_sprite );
    json.end_object();
}

void char_trait_data::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    data.read( "key", key );
    data.read( "charge", charge );
    data.read( "powered", powered );
    data.read( "show_sprite", show_sprite );
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///// consumption.h

void consumption_history_t::serialize( JsonOut &json ) const
{
    json.write( elems );
}

void consumption_history_t::deserialize( JsonIn &jsin )
{
    jsin.read( elems );
}

void consumption_event::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "time", time );
    json.member( "type_id", type_id );
    json.member( "component_hash", component_hash );
    json.end_object();
}

void consumption_event::deserialize( JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    jo.read( "time", time );
    jo.read( "type_id", type_id );
    jo.read( "component_hash", component_hash );
}

/**
 * Gather variables for saving. These variables are common to both the avatar and NPCs.
 */

////////////////////////////////////////////////////////////////////////////////////////////////////
///// inventory.h
/*
 * Save invlet cache
 */
void location_inventory::json_save_invcache( JsonOut &json ) const
{
    json.start_array();
for( const auto &elem : inv.invlet_cache.get_invlets_by_id() ) {
    json.start_object();
        json.member( elem.first.str() );
        json.start_array();
        for( const auto &_sym : elem.second ) {
            json.write( static_cast<int>( _sym ) );
        }
        json.end_array();
        json.end_object();
    }
    json.end_array();
}

/*
 * Invlet cache: player specific, thus not wrapped in inventory::json_load/save
 */
void location_inventory::json_load_invcache( JsonIn &jsin )
{
    try {
        std::unordered_map<itype_id, std::string> map;
        for( JsonObject jo : jsin.get_array() ) {
            jo.allow_omitted_members();
            for( const JsonMember member : jo ) {
                std::string invlets;
                for( const int i : member.get_array() ) {
                    invlets.push_back( i );
                }
                map[itype_id( member.name() )] = invlets;
            }
        }
        inv.invlet_cache = { map };
    } catch( const JsonError &jsonerr ) {
        debugmsg( "bad invcache json:\n%s", jsonerr.c_str() );
    }
}

/*
 * save all items. Just this->items, invlet cache saved separately
 */
void location_inventory::json_save_items( JsonOut &json ) const
{
    json.start_array();
for( const auto &elem : inv.items ) {
    for( const auto &elem_stack_iter : elem ) {
            elem_stack_iter->serialize( json );
        }
    }
    json.end_array();
}

void location_inventory::json_load_items( JsonIn &jsin )
{
    jsin.start_array();
    while( !jsin.end_array() ) {
        add_item( item::spawn( jsin ), true, false );
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
void time_point::serialize( JsonOut &jsout ) const
{
    jsout.write( turn_ );
}

void time_point::deserialize( JsonIn &jsin )
{
    turn_ = jsin.get_int();
}

void time_duration::serialize( JsonOut &jsout ) const
{
    jsout.write( turns_ );
}

void time_duration::deserialize( JsonIn &jsin )
{
    if( jsin.test_string() ) {
        *this = read_from_json_string<time_duration>( jsin, time_duration::units );
    } else {
        turns_ = jsin.get_int();
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///// item.h

void item::craft_data::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "making", making->ident().str() );
    jsout.member( "comps_used", comps_used );
    jsout.member( "next_failure_point", next_failure_point );
    jsout.member( "tools_to_continue", tools_to_continue );
    jsout.member( "cached_tool_selections", cached_tool_selections );
    jsout.end_object();
}

void item::craft_data::deserialize( JsonIn &jsin )
{
    deserialize( jsin.get_object() );
}

void item::craft_data::deserialize( const JsonObject &obj )
{
    obj.allow_omitted_members();
    making = &recipe_id( obj.get_string( "making" ) ).obj();
    obj.read( "comps_used", comps_used );
    next_failure_point = obj.get_int( "next_failure_point", -1 );
    tools_to_continue = obj.get_bool( "tools_to_continue", false );
    obj.read( "cached_tool_selections", cached_tool_selections );
}

void dimension_info::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "dimension_id", dimension_id );
    jsout.member( "world_type", world_type );
    jsout.member( "display_name", display_name );
    if( pocket_info.has_value() ) {
    jsout.member( "pocket_info", *pocket_info );
    }
    jsout.end_object();
}

void dimension_info::deserialize( JsonIn &jsin )
{
    auto obj = jsin.get_object();
    obj.allow_omitted_members();
    obj.read( "dimension_id", dimension_id );
    obj.read( "world_type", world_type );
    obj.read( "display_name", display_name );
    if( obj.has_member( "pocket_info" ) ) {
        obj.read( "pocket_info", pocket_info );
    }
}

void pocket_dimension_data::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "entry_point", entry_point );
    jsout.member( "bounds", bounds );
    jsout.member( "is_initialized", is_initialized );
    jsout.member( "terrain_generated", terrain_generated );
    jsout.member( "return_dimension_id", return_dimension_id );
    jsout.member( "return_world_type", return_world_type );
    jsout.member( "return_point", return_point );
    if( last_player_exit.has_value() ) {
    jsout.member( "last_player_exit", *last_player_exit );
    }
    if( lifetime.has_value() ) {
    jsout.member( "lifetime", *lifetime );
    }
    jsout.end_object();
}

void pocket_dimension_data::deserialize( JsonIn &jsin )
{
    auto obj = jsin.get_object();
    obj.allow_omitted_members();

    // Current format stores explicit return dimension data.
    // Legacy compat reconstructs it from return_dimension + return_instance_id.
    if( obj.has_member( "return_dimension_id" ) || obj.has_member( "return_world_type" ) ) {
        obj.read( "return_dimension_id", return_dimension_id );
        obj.read( "return_world_type", return_world_type );
    } else {
        // Old format: reconstruct dimension_id and return_dimension_id
        auto old_dim_type = world_type_id{};
        auto old_instance_id = std::string{};
        obj.read( "dimension_type", old_dim_type );
        obj.read( "instance_id", old_instance_id );
        // Old return fields
        auto old_return_dim = world_type_id{};
        auto old_return_instance = std::string{};
        obj.read( "return_dimension", old_return_dim );
        obj.read( "return_instance_id", old_return_instance );
        return_world_type = old_return_dim;
        if( old_return_dim.is_valid() ) {
            return_dimension_id = old_return_dim.obj().save_prefix + old_return_instance + "_";
        }
        // Trim trailing "_" for the return if instance was empty (overworld return)
        if( return_dimension_id.ends_with( "_" ) && old_return_instance.empty() ) {
            return_dimension_id = old_return_dim.obj().save_prefix;
        }
    }

    obj.read( "entry_point", entry_point );
    obj.read( "bounds", bounds );
    is_initialized = obj.get_bool( "is_initialized", false );
    terrain_generated = obj.get_bool( "terrain_generated", false );
    obj.read( "return_point", return_point );
    if( obj.has_member( "last_player_exit" ) ) {
        time_point tp = calendar::turn_zero;
        obj.read( "last_player_exit", tp );
        last_player_exit = tp;
    }
    if( obj.has_member( "lifetime" ) ) {
        time_duration td = 0_turns;
        obj.read( "lifetime", td );
        lifetime = td;
    }
}

// Full equivalence. Consider only checking identifying data.
bool pocket_dimension_data::operator==( const pocket_dimension_data &rhs ) const
{
    return entry_point == rhs.entry_point &&
           bounds == rhs.bounds &&
           is_initialized == rhs.is_initialized &&
           terrain_generated == rhs.terrain_generated &&
           return_dimension_id == rhs.return_dimension_id &&
           return_world_type == rhs.return_world_type &&
           return_point == rhs.return_point;
}

void dimension_bounds::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "min_bound", min_bound );
    jsout.member( "max_bound", max_bound );
    jsout.member( "boundary_terrain", boundary_terrain );
    jsout.member( "boundary_overmap_terrain", boundary_overmap_terrain );
    jsout.end_object();
}

void dimension_bounds::deserialize( JsonIn &jsin )
{
    JsonObject obj = jsin.get_object();
    obj.allow_omitted_members();

    obj.read( "min_bound", min_bound );
    obj.read( "max_bound", max_bound );
    obj.read( "boundary_terrain", boundary_terrain );
    obj.read( "boundary_overmap_terrain", boundary_overmap_terrain );
}

bool dimension_bounds::operator==( const dimension_bounds &rhs ) const
{
    return min_bound == rhs.min_bound &&
           max_bound == rhs.max_bound &&
           boundary_terrain == rhs.boundary_terrain &&
           boundary_overmap_terrain == rhs.boundary_overmap_terrain;
}

// Template parameter because item::craft_data is private and I don't want to make it public.
template<typename T>
static void load_legacy_craft_data( io::JsonObjectInputArchive &archive, T &value )
{
    archive.allow_omitted_members();
    if( archive.has_member( "making" ) ) {
        value = cata::make_value<typename T::element_type>();
        value->deserialize( archive );
    }
}

// Dummy function as we never load anything from an output archive.
template<typename T>
static void load_legacy_craft_data( io::JsonObjectOutputArchive &, T & )
{
}

namespace charge_removal_blacklist
{
static std::set<itype_id> removal_list;

const std::set<itype_id> &get()
{
    return removal_list;
}
void load( const JsonObject &jo )
{
    std::set<std::string> d = jo.get_tags( "list" );
    for( const std::string &s : d ) {
        removal_list.insert( itype_id( s ) );
    }
}
void reset()
{
    removal_list.clear();
}
static std::vector<std::tuple<item *, int>> split_defer;
void defer( item *it, int cnt )
{
    split_defer.push_back( std::make_tuple( it, cnt ) );
}

void split_deferred()
{
    auto &m = get_map();

    for( const auto& [it, cnt] : split_defer ) {
        const auto pos = it->position();
        for( auto n = 0; n < cnt; n++ ) {
            auto tmp = item::spawn( *it );

            /* Handle Vehicle */
            const auto vp = m.veh_at( pos );
            if( vp.has_value() ) {
                const auto vpid = vp->part_index();
                const auto stk = vp->vehicle().get_items( vpid );
                if( std::ranges::contains( stk, it ) ) {
                    tmp = vp->vehicle().add_item( vpid, std::move( tmp ) );
                }
                if( !tmp ) {
                    continue;
                }
            }

            /* Handle Player  */
            {
                auto &u = get_avatar();
                if( u.has_item( *it ) ) {
                    tmp = u.i_add_or_drop( std::move( tmp ) );
                }
                if( !tmp ) {
                    continue;
                }
            }

            /* Handle NPCs */
            {
                const auto npc_vec = get_overmapbuffer( m.get_bound_dimension() ).get_overmap_npcs();
                for( const auto &p : npc_vec ) {
                    if( p->has_item( *it ) ) {
                        tmp = p->i_add_or_drop( std::move( tmp ) );
                    }
                    if( !tmp ) {
                        break;
                    }
                }
                if( !tmp ) {
                    continue;
                }
            }

            /* drop on map */
            tmp = m.add_item_or_charges( pos, std::move( tmp ) );

            if( tmp ) {
                debugmsg( "failed to split charges to items: %s", it->type_name( 1 ) );
            }
        }
    }
    split_defer.clear();
}

} // namespace charge_removal_blacklist

namespace to_cbc_migration
{
static std::set<itype_id> the_list;

void load( const JsonObject &jo )
{
    std::set<std::string> d = jo.get_tags( "list" );
    for( const std::string &s : d ) {
        the_list.insert( itype_id( s ) );
    }
}

void reset()
{
    the_list.clear();
}

static bool migration_required( const item &i )
{
    if( !i.count_by_charges() ) {
        return false;
    }
    return the_list.contains( i.typeId() );
}

/**
 * Merge old individual items into new count-by-charges items with same id.
 */
void migrate( std::vector<detached_ptr<item>> &stack )
{
    for( auto it_src = stack.begin(); it_src != stack.end(); ) {
        if( !migration_required( **it_src ) ) {
            it_src++;
            continue;
        }
        auto it_dst = it_src;
        it_dst++;
        bool merged = false;
        for( ; it_dst != stack.end(); it_dst++ ) {
            detached_ptr<item> &src = *it_src;
            if( src->stacks_with( **it_dst ) && ( *it_dst )->merge_charges( std::move( src ) ) ) {
                it_src = stack.erase( it_src );
                merged = true;
                break;
            }
        }
        if( !merged ) {
            it_src++;
        }
    }
}
} // namespace to_cbc_migration

namespace
{

namespace damage_instance_serialization
{

struct serialized_damage_unit {
    int type = 0;
    float amount = 0.0f;
    float res_pen = 0.0f;
    float res_mult = 1.0f;
    float damage_multiplier = 1.0f;

    auto serialize( JsonOut &jsout ) const -> void {
        jsout.start_array();
        jsout.write( type );
        jsout.write( amount );
        jsout.write( res_pen );
        jsout.write( res_mult );
        jsout.write( damage_multiplier );
        jsout.end_array();
    }

    auto deserialize( JsonIn &jsin ) -> void {
        jsin.start_array();
        jsin.read( type );
        jsin.read( amount );
        jsin.read( res_pen );
        jsin.read( res_mult );
        jsin.read( damage_multiplier );
        jsin.end_array();
    }
};

auto serialize_damage_instance( const damage_instance &dmg ) -> std::vector<serialized_damage_unit>
{
    auto result = std::vector<serialized_damage_unit> {};
    for( const auto &du : dmg.damage_units ) {
        result.push_back( {
            static_cast<int>( du.type ),
            du.amount,
            du.res_pen,
            du.res_mult,
            du.damage_multiplier
        } );
    }
    return result;
}

auto deserialize_damage_instance( const std::vector<serialized_damage_unit> &serialized ) ->
damage_instance
{
    auto result = damage_instance{};
    for( const auto &sdu : serialized ) {
        result.damage_units.emplace_back(
            static_cast<damage_type>( sdu.type ),
            sdu.amount,
            sdu.res_pen,
            sdu.res_mult,
            sdu.damage_multiplier
        );
    }
    return result;
}

} // namespace damage_instance_serialization

} // namespace

template<typename Archive>
void item::io( Archive &archive )
{

    itype_id orig; // original ID as loaded from JSON
    const auto load_type = [&]( const std::string & id ) {
        orig = itype_id( id );
        convert( item_controller->migrate_id( orig ) );
    };

    const auto load_curammo = [this]( const std::string & id ) {
        curammo = &*item_controller->migrate_id( itype_id( id ) );
    };
    const auto load_corpse = [this]( const std::string & id ) {
        if( itype_id( id ).is_null() ) {
            // backwards compatibility, nullptr should not be stored at all
            corpse = nullptr;
        } else {
            corpse = &mtype_id( id ).obj();
        }
    };
    archive.template io<const itype>( "typeid", type, load_type, []( const itype & i ) {
        return i.get_id().str();
    }, io::required_tag() );

    // normalize legacy saves to always have charges >= 0
    archive.io( "charges", charges, 0 );
    charges = std::max( charges, 0 );

    archive.io( "energy", energy, 0_J );

    archive.io( "burnt", burnt, 0 );
    archive.io( "poison", poison, 0 );
    archive.io( "frequency", frequency, 0 );
    archive.io( "snip_id", snip_id, snippet_id::NULL_ID() );
    // NB! field is named `irridation` in legacy files
    archive.io( "irridation", irradiation, 0 );
    archive.io( "bday", bday, calendar::start_of_cataclysm );
    archive.io( "mission_id", mission_id, -1 );
    archive.io( "player_id", player_id, -1 );
    archive.io( "item_vars", item_vars_, io::empty_default_tag() );
    // TODO: change default to empty string
    archive.io( "name", corpse_name, std::string() );
    archive.io( "owner", owner, faction_id::NULL_ID() );
    archive.io( "old_owner", old_owner, faction_id::NULL_ID() );
    archive.io( "invlet", invlet, '\0' );
    archive.io( "damaged", damage_, 0 );
    archive.io( "active", active, false );
    if( is_tool() ) {
        archive.io( "turns_active", type->tool->turns_active, 0 );
    }
    archive.io( "is_favorite", is_favorite, false );
    archive.io( "item_counter", item_counter, static_cast<decltype( item_counter )>( 0 ) );
    archive.io( "rot", rot, 0_turns );
    archive.io( "last_rot_check", last_rot_check, calendar::start_of_cataclysm );
    archive.io( "techniques", techniques, io::empty_default_tag() );
    {
        auto serialized_melee = std::vector<damage_instance_serialization::serialized_damage_unit> {};
        auto serialized_ranged = std::vector<damage_instance_serialization::serialized_damage_unit> {};

        archive.io( "melee_damage_bonus", serialized_melee );
        archive.io( "ranged_damage_bonus", serialized_ranged );

        // 로드 시에만 역직렬화
        if( !serialized_melee.empty() ) {
            melee_damage_bonus = deserialize_damage_instance( serialized_melee );
        }
        if( !serialized_ranged.empty() ) {
            ranged_damage_bonus = deserialize_damage_instance( serialized_ranged );
        }
    }
    archive.io( "range_bonus", range_bonus, 0 );
    archive.io( "dispersion_bonus", dispersion_bonus, 0 );
    archive.io( "recoil_bonus", recoil_bonus, 0 );
    archive.io( "faults", faults, io::empty_default_tag() );
    archive.io( "item_tags", item_tags, io::empty_default_tag() );
    archive.io( "components", components, io::empty_default_tag() );
    archive.io( "recipe_charges", recipe_charges, 1 );
    archive.template io<const itype>( "curammo", curammo, load_curammo,
    []( const itype & i ) {
        return i.get_id().str();
    } );
    archive.template io<const mtype>( "corpse", corpse, load_corpse,
    []( const mtype & i ) {
        return i.id.str();
    } );
    archive.io( "craft_data", craft_data_, decltype( craft_data_ )() );
    archive.io( "light", light.luminance, nolight.luminance );
    archive.io( "light_width", light.width, nolight.width );
    archive.io( "light_dir", light.direction, nolight.direction );

    static const cata::value_ptr<relic> null_relic_ptr = nullptr;
    archive.io( "relic_data", relic_data, null_relic_ptr );

    if constexpr( Archive::is_input::value ) {
        auto dim = dimension_info{};
        if( archive.read( "pocket_dim", dim ) ) {
            pocket_dim = dim;
        }
    } else if( pocket_dim.has_value() ) {
        archive.io( "pocket_dim", *pocket_dim );
    }

    archive.io( "drop_token", drop_token, decltype( drop_token )() );

    item_controller->migrate_item( orig, *this );

    if( !Archive::is_input::value ) {
        return;
    }
    /* Loading has finished, following code is to ensure consistency and fixes bugs in saves. */

    load_legacy_craft_data( archive, craft_data_ );

    double float_damage = 0;
    if( archive.read( "damage", float_damage ) ) {
        damage_ = std::min( std::max( min_damage(),
                                      static_cast<int>( float_damage * itype::damage_scale ) ),
                            max_damage() );
    }

    int note = 0;
    const bool note_read = archive.read( "note", note );

    // Old saves used to only contain one of those values (stored under "poison"), it would be
    // loaded into a union of those members. Now they are separate members and must be set separately.
    if( poison != 0 && note == 0 && !type->snippet_category.empty() ) {
        std::swap( note, poison );
    }
    if( poison != 0 && frequency == 0 && ( typeId() == itype_radio_on || typeId() == itype_radio ) ) {
        std::swap( frequency, poison );
    }
    if( poison != 0 && irradiation == 0 && typeId() == itype_rad_badge ) {
        std::swap( irradiation, poison );
    }

    // erase all invalid flags (not defined in flags.json)
    // warning was generated earlier on load
    erase_if( item_tags, [&]( const flag_id & f ) {
        return !f.is_valid();
    } );

    if( note_read ) {
        snip_id = SNIPPET.migrate_hash_to_id( note );
    } else {
        std::optional<std::string> snip;
        if( archive.read( "snippet_id", snip ) && snip ) {
            snip_id = snippet_id( snip.value() );
        }
    }

    // Compatibility for item type changes: for example soap changed from being a generic item
    // (item::charges -1 or 0 or anything else) to comestible (and thereby counted by charges),
    // old saves still have invalid charges, this fixes the charges value to the default charges.
    if( count_by_charges() && charges <= 0 ) {
        charges = item( type, calendar::start_of_cataclysm ).charges;
    }
    if( is_food() ) {
        active = true;
    }
    if( !is_active() && has_flag( flag_WET ) ) {
        // Some wet items from legacy saves may be inactive
        active = true;
    }
    std::string mode;
    if( archive.read( "mode", mode ) ) {
        // only for backward compatibility (nowadays mode is stored in item_vars)
        gun_set_mode( gun_mode_id( mode ) );
    }

    // Books without any chapters don't need to store a remaining-chapters
    // counter, it will always be 0 and it prevents proper stacking.
    if( get_chapters() == 0 ) {
        for( auto it = item_vars_.begin(); it != item_vars_.end(); ) {
            if( it->first.starts_with( "remaining-chapters-" ) ) {
                item_vars_.erase( it++ );
            } else {
                ++it;
            }
        }
    }

    // Remove stored translated gerund in favor of storing the inscription tool type
    item_vars_.erase( "item_label_type" );
    item_vars_.erase( "item_note_type" );

    // Activate corpses from old saves
    if( is_corpse() && !is_active() ) {
        active = true;
    }

    if( charges != 0 && !type->can_have_charges() ) {
        // Types that are known to have charges, but should not have them.
        // We fix it here, but it's expected from bugged saves and does not require a message.
        const auto to_split = charges - 1;
        charges = 0;
        curammo = nullptr;

        if( !charge_removal_blacklist::get().contains( type->get_id() ) ) {
            debugmsg( "Item %s was loaded with charges, but can not have any!", type->get_id() );
        } else if( to_split > 0 ) {
            charge_removal_blacklist::defer( this, to_split );
        }
    }

    // Relic check. Kinda late, but that's how relics have to be
    if( relic_data ) {
        relic_data->check();
    }
}

void item::deserialize( JsonIn &jsin )
{
    const JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    io::JsonObjectInputArchive archive( data );
    io( archive );
    // made for fast forwarding time from 0.D to 0.E
    if( savegame_loading_version < 27 ) {
        legacy_fast_forward_time();
    }
    if( data.has_array( "contents" ) ) {
        std::vector<detached_ptr<item>> items;
        data.read( "contents", items );
        for( detached_ptr<item> &obj : items ) {
            contents.insert_item( std::move( obj ) );
        }
    } else {
        data.read( "contents", contents );
    }
    if( data.has_member( "item_kill_tracker" ) ) {
        kills = std::make_unique<kill_tracker>( false );
        data.read( "item_kill_tracker", kills );
    }

    if( data.has_member( "id" ) ) {
        safe_reference<item>::id_type id;
        data.read( "id", id );
        safe_reference<item>::register_load( this, id );
    }

    // Sealed item migration: items with "unseals_into" set should always have contents
    if( contents.empty() && is_non_resealable_container() ) {
        convert( type->container->unseals_into );
    }
}

void item::serialize( JsonOut &json ) const
{
    io::JsonObjectOutputArchive archive( json );
    const_cast<item *>( this )->io( archive );

    if( !melee_damage_bonus.damage_units.empty() ) {
    json.member( "melee_damage_bonus",
                 damage_instance_serialization::serialize_damage_instance( melee_damage_bonus ) );
    }

    if( !ranged_damage_bonus.damage_units.empty() ) {
    json.member( "ranged_damage_bonus",
                 damage_instance_serialization::serialize_damage_instance( ranged_damage_bonus ) );
    }

    if( !contents.empty() ) {
    json.member( "contents", contents );
    }
    if( kills ) {
    json.member( "item_kill_tracker" );
        kills->serialize( json );
    }

    safe_reference<item>::id_type id = safe_reference<item>::lookup_id( this );
    if( id != safe_reference<item>::ID_NONE ) {
    json.member( "id", id );
    }

}

////////////////////////////////////////////////////////////////////////////////////////////////////
///// vehicle.h

////////////////// mission.h
////
void mission::deserialize( JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();

    if( jo.has_int( "type_id" ) ) {
        type = &mission_type::from_legacy( jo.get_int( "type_id" ) ).obj();
    } else if( jo.has_string( "type_id" ) ) {
        type = &mission_type_id( jo.get_string( "type_id" ) ).obj();
    } else {
        debugmsg( "Saved mission has no type" );
        type = &mission_type::get_all().front();
    }

    bool failed;
    bool was_started;
    std::string status_string;
    if( jo.read( "status", status_string ) ) {
        status = status_from_string( status_string );
    } else if( jo.read( "failed", failed ) && failed ) {
        status = mission_status::failure;
    } else if( jo.read( "was_started", was_started ) && !was_started ) {
        status = mission_status::yet_to_start;
    } else {
        // Note: old code had no idea of successful missions!
        // We can't check properly here, since most of the game isn't loaded
        status = mission_status::in_progress;
    }

    jo.read( "value", value );
    jo.read( "kill_count_to_reach", kill_count_to_reach );
    jo.read( "reward", reward );
    jo.read( "uid", uid );
    JsonArray ja = jo.get_array( "target" );
    if( ja.size() == 3 ) {
        target.x() = ja.get_int( 0 );
        target.y() = ja.get_int( 1 );
        target.z() = ja.get_int( 2 );
    } else if( ja.size() == 2 ) {
        target.x() = ja.get_int( 0 );
        target.y() = ja.get_int( 1 );
    }

    if( jo.has_int( "follow_up" ) ) {
        follow_up = mission_type::from_legacy( jo.get_int( "follow_up" ) );
    } else if( jo.has_string( "follow_up" ) ) {
        follow_up = mission_type_id( jo.get_string( "follow_up" ) );
    }

    jo.read( "item_id", item_id );
    jo.read( "target_id", target_id );

    if( jo.has_int( "recruit_class" ) ) {
        recruit_class = npc_class::from_legacy_int( jo.get_int( "recruit_class" ) );
    } else {
        recruit_class = npc_class_id( jo.get_string( "recruit_class", "NC_NONE" ) );
    }

    jo.read( "target_npc_id", target_npc_id );
    jo.read( "monster_type", monster_type );
    jo.read( "monster_species", monster_species );
    jo.read( "monster_kill_goal", monster_kill_goal );
    jo.read( "deadline", deadline );
    jo.read( "step", step );
    jo.read( "item_count", item_count );
    jo.read( "npc_id", npc_id );
    jo.read( "good_fac_id", good_fac_id );
    jo.read( "bad_fac_id", bad_fac_id );

    // Suppose someone had two living players in an 0.C stable world. When loading player 1 in 0.D
    // (or maybe even creating a new player), the former condition makes legacy_no_player_id true.
    // When loading player 2, there will be a player_id member in SAVE_MASTER (i.e. master.gsav),
    // but the bool member legacy_no_player_id will have been saved as true
    // (unless the mission belongs to a player that's been loaded into 0.D)
    // See player::deserialize and mission::set_player_id_legacy_0c
    legacy_no_player_id = !jo.read( "player_id", player_id ) ||
                          jo.get_bool( "legacy_no_player_id", false );
    jo.read( "dimension_id", dimension_id_ );
}

void mission::serialize( JsonOut &json ) const
{
    json.start_object();

    json.member( "type_id", type->id );
    json.member( "status", status_to_string( status ) );
    json.member( "value", value );
    json.member( "kill_count_to_reach", kill_count_to_reach );
    json.member( "reward", reward );
    json.member( "uid", uid );

    json.member( "target" );
    json.start_array();
    json.write( target.x() );
    json.write( target.y() );
    json.write( target.z() );
    json.end_array();

    json.member( "item_id", item_id );
    json.member( "item_count", item_count );
    json.member( "target_id", target_id );
    json.member( "recruit_class", recruit_class );
    json.member( "target_npc_id", target_npc_id );
    json.member( "monster_type", monster_type );
    json.member( "monster_species", monster_species );
    json.member( "monster_kill_goal", monster_kill_goal );
    json.member( "deadline", deadline );
    json.member( "npc_id", npc_id );
    json.member( "good_fac_id", good_fac_id );
    json.member( "bad_fac_id", bad_fac_id );
    json.member( "step", step );
    json.member( "follow_up", follow_up );
    json.member( "player_id", player_id );
    json.member( "legacy_no_player_id", legacy_no_player_id );
    if( !dimension_id_.empty() ) {
    json.member( "dimension_id", dimension_id_ );
    }

    json.end_object();
}

////////////////// faction.h
////
void faction::deserialize( JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();

    jo.read( "id", id_ );
    jo.read( "name", name_ );
    jo.read( "likes_u", likes_u_ );
    jo.read( "respects_u", respects_u_ );
    jo.read( "known_by_u", known_by_u_ );
    jo.read( "size", size_ );
    jo.read( "power", power_ );
    if( !jo.read( "food_supply", food_supply_ ) ) {
        food_supply_ = 100;
    }
    if( !jo.read( "wealth", wealth_ ) ) {
        wealth_ = 100;
    }
    if( jo.has_array( "opinion_of" ) ) {
        opinion_of = jo.get_int_array( "opinion_of" );
    }
    load_relations( jo );
}

void faction::serialize( JsonOut &json ) const
{
    json.start_object();

    json.member( "id", id_ );
    json.member( "name", name_ );
    json.member( "likes_u", likes_u_ );
    json.member( "respects_u", respects_u_ );
    json.member( "known_by_u", known_by_u_ );
    json.member( "size", size_ );
    json.member( "power", power_ );
    json.member( "food_supply", food_supply_ );
    json.member( "wealth", wealth_ );
    json.member( "opinion_of", opinion_of );
    json.member( "relations" );
    json.start_object();
for( const auto &rel_data : relations_ ) {
    json.member( rel_data.first );
        json.start_object();
        for( const auto &rel_flag : npc_factions::relation_strs ) {
            json.member( rel_flag.first, rel_data.second.test( rel_flag.second ) );
        }
        json.end_object();
    }
    json.end_object();

    json.end_object();
}

void Creature::store( JsonOut &jsout ) const
{
    jsout.member( "moves", moves );
    jsout.member( "pain", pain );

    // killer is not stored, it's temporary anyway, any creature that has a non-null
    // killer is dead (as per definition) and should not be stored.

    // Because JSON requires string keys we need to convert our int keys
    std::unordered_map<std::string, std::unordered_map<std::string, effect>> tmp_map;
for( const auto &maps : *effects ) {
    for( const auto &i : maps.second ) {
            if( i.second.is_removed() ) {
                continue;
            }
            std::ostringstream convert;
            convert << i.first->token;
            tmp_map[maps.first.str()][convert.str()] = i.second;
        }
    }
    jsout.member( "effects", tmp_map );

    jsout.member( "values", values );

    jsout.member( "blocks_left", num_blocks );
    jsout.member( "dodges_left", num_dodges );
    jsout.member( "num_blocks_bonus", num_blocks_bonus );
    jsout.member( "num_dodges_bonus", num_dodges_bonus );

    jsout.member( "armor_bash_bonus", armor_bash_bonus );
    jsout.member( "armor_cut_bonus", armor_cut_bonus );
    jsout.member( "armor_bullet_bonus", armor_bullet_bonus );

    jsout.member( "speed", speed_base );

    jsout.member( "speed_bonus", speed_bonus );
    jsout.member( "move_credit_remainder", move_credit_remainder );
    jsout.member( "dodge_bonus", dodge_bonus );
    jsout.member( "block_bonus", block_bonus );
    jsout.member( "hit_bonus", hit_bonus );
    jsout.member( "bash_bonus", bash_bonus );
    jsout.member( "cut_bonus", cut_bonus );
    jsout.member( "size_bonus", size_bonus );

    jsout.member( "underwater", underwater );

    jsout.member( "body", body );

    // fake is not stored, it's temporary anyway, only used to fire with a gun.
}

void Creature::load( const JsonObject &jsin )
{
    jsin.allow_omitted_members();
    jsin.read( "moves", moves );
    jsin.read( "pain", pain );

    killer.reset();

    if( jsin.has_object( "effects" ) ) {
        // Because JSON requires string keys we need to convert back to our bp keys
        std::unordered_map<std::string, std::unordered_map<std::string, effect>> tmp_map;
        jsin.read( "effects", tmp_map );
        int key_num = 0;
        for( const auto &maps : tmp_map ) {
            const efftype_id id( maps.first );
            if( !id.is_valid() ) {
                debugmsg( "Invalid effect: %s", id.c_str() );
                continue;
            }
            for( const auto &i : maps.second ) {
                if( !( std::istringstream( i.first ) >> key_num ) ) {
                    key_num = 0;
                }
                const bodypart_str_id &bp = convert_bp( static_cast<body_part>( key_num ) );
                const effect &e = i.second;

                ( *effects )[id][bp] = e;
                on_effect_int_change( id, e.get_intensity(), bp );
            }
        }
    }
    jsin.read( "values", values );

    jsin.read( "blocks_left", num_blocks );
    jsin.read( "dodges_left", num_dodges );
    jsin.read( "num_blocks_bonus", num_blocks_bonus );
    jsin.read( "num_dodges_bonus", num_dodges_bonus );

    jsin.read( "armor_bash_bonus", armor_bash_bonus );
    jsin.read( "armor_cut_bonus", armor_cut_bonus );
    jsin.read( "armor_bullet_bonus", armor_bullet_bonus );

    jsin.read( "speed", speed_base );

    jsin.read( "speed_bonus", speed_bonus );
    jsin.read( "move_credit_remainder", move_credit_remainder );
    jsin.read( "dodge_bonus", dodge_bonus );
    jsin.read( "block_bonus", block_bonus );
    jsin.read( "hit_bonus", hit_bonus );
    jsin.read( "bash_bonus", bash_bonus );
    jsin.read( "cut_bonus", cut_bonus );
    jsin.read( "size_bonus", size_bonus );

    jsin.read( "underwater", underwater );

    jsin.read( "body", body );

    for( auto &it : body ) {
        it.second.set_location( new wield_item_location( this ) );
    }

    fake = false; // see Creature::load

    on_stat_change( "pain", pain );
}

void player_morale::morale_subtype::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member_as_string( "subtype_type", subtype_type );
    switch( subtype_type ) {
    case morale_subtype_t::single:
        break;
    case morale_subtype_t::by_item:
        json.member( "item_type", item_type->get_id() );
            break;
        case morale_subtype_t::by_effect:
            json.member( "eff_type", eff_type );
            break;
        default:
            break;
    }
    json.end_object();
}

void player_morale::morale_subtype::deserialize( JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    jo.read( "subtype_type", subtype_type );
    switch( subtype_type ) {
        case morale_subtype_t::single:
            break;
        case morale_subtype_t::by_item:
            item_type = &*itype_id( jo.get_string( "item_type" ) );
            break;
        case morale_subtype_t::by_effect:
            eff_type = efftype_id( jo.get_string( "eff_type" ) );
            break;
        default:
            debugmsg( "invalid or missing morale_subtype_t: %d",
                      static_cast<int>( subtype_type ) );
            subtype_type = morale_subtype_t::single;
    }
}

void player_morale::morale_point::deserialize( JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    jo.read( "type", type );
    if( !jo.read( "subtype", subtype ) && jo.has_string( "item_type" ) ) {
        subtype = morale_subtype( *itype_id( jo.get_string( "item_type" ) ) );
    }
    jo.read( "bonus", bonus );
    jo.read( "duration", duration );
    jo.read( "decay_start", decay_start );
    jo.read( "age", age );
}

void player_morale::morale_point::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "type", type );
    json.member( "subtype", subtype );
    json.member( "bonus", bonus );
    json.member( "duration", duration );
    json.member( "decay_start", decay_start );
    json.member( "age", age );
    json.end_object();
}

void player_morale::store( JsonOut &jsout ) const
{
    jsout.member( "morale", points );
}

void player_morale::load( const JsonObject &jsin )
{
    jsin.allow_omitted_members();
    jsin.read( "morale", points );
}

struct mm_elem {
    memorized_terrain_tile tile;
    memorized_terrain_tile terrain;
    int symbol;

    bool operator==( const mm_elem &rhs ) const {
        return symbol == rhs.symbol && tile == rhs.tile && terrain == rhs.terrain;
    }
};

void mm_submap::serialize( JsonOut &jsout ) const
{
    jsout.start_array();

    // Uses RLE for compression.
    // Element format: [overlay_tile, subtile, rotation, symbol, ?terrain_str, terrain_sub, terrain_rot, ?count]
    // terrain fields are only written when non-empty, saving space for the ~92% of tiles with no furniture.

    mm_elem last;
    int num_same = 1;

    const auto write_seq = [&]() {
        jsout.start_array();
        jsout.write( last.tile.tile );
        jsout.write( last.tile.subtile );
        jsout.write( last.tile.rotation );
        jsout.write( last.symbol );
        if( !last.terrain.tile.empty() ) {
            jsout.write( last.terrain.tile );
            jsout.write( last.terrain.subtile );
            jsout.write( last.terrain.rotation );
        }
        if( num_same != 1 ) {
            jsout.write( num_same );
        }
        jsout.end_array();
    };

for( const auto p : submap_tiles() ) {
        const mm_elem elem = { tile( p ), terrain_tile( p ), symbol( p ) };
        if( p.x() == 0 && p.y() == 0 ) {
            last = elem;
        } else if( last == elem ) {
            num_same += 1;
        } else {
            write_seq();
            num_same = 1;
            last = elem;
        }
    }
    write_seq();

    jsout.end_array();
}

void mm_submap::deserialize( JsonIn &jsin )
{
    jsin.start_array();

    // Uses RLE for compression.

    mm_elem elem;
    size_t remaining = 0;

    for( const auto p : submap_tiles() ) {
        if( remaining > 0 ) {
            remaining -= 1;
        } else {
            jsin.start_array();
            elem.tile.tile = jsin.get_string();
            elem.tile.subtile = jsin.get_int();
            elem.tile.rotation = jsin.get_int();
            elem.symbol = jsin.get_int();
            elem.terrain = mm_submap::default_tile;
            if( jsin.test_string() ) {
                // New format: optional terrain tile fields follow symbol.
                elem.terrain.tile = jsin.get_string();
                elem.terrain.subtile = jsin.get_int();
                elem.terrain.rotation = jsin.get_int();
            } else if( elem.tile.tile.starts_with( "t_" ) ) {
                // Migration: old saves stored terrain in the overlay slot.
                // Move it to the terrain slot where draw_terrain now expects it.
                elem.terrain = elem.tile;
                elem.tile = mm_submap::default_tile;
            }
            if( jsin.test_int() ) {
                remaining = jsin.get_int() - 1;
            }
            jsin.end_array();
        }
        // Try to avoid assigning to save up on memory
        if( elem.tile != mm_submap::default_tile ) {
            set_tile( p, elem.tile );
        }
        if( elem.terrain != mm_submap::default_tile ) {
            set_terrain_tile( p, elem.terrain );
        }
        if( elem.symbol != mm_submap::default_symbol ) {
            set_symbol( p, elem.symbol );
        }
    }
    jsin.end_array();
}

void mm_region::serialize( JsonOut &jsout ) const
{
    jsout.start_array();
    // NOLINTNEXTLINE(modernize-loop-convert): leaving as is for readability
    for( size_t y = 0; y < MM_REG_SIZE; y++ ) {
    // NOLINTNEXTLINE(modernize-loop-convert): leaving as is for readability
    for( size_t x = 0; x < MM_REG_SIZE; x++ ) {
            const shared_ptr_fast<mm_submap> &sm = submaps[x][y];
            if( sm->is_empty() ) {
                jsout.write_null();
            } else {
                sm->serialize( jsout );
            }
        }
    }
    jsout.end_array();
}

void mm_region::deserialize( JsonIn &jsin )
{
    jsin.start_array();
    // NOLINTNEXTLINE(modernize-loop-convert): leaving as is for readability
    for( size_t y = 0; y < MM_REG_SIZE; y++ ) {
        // NOLINTNEXTLINE(modernize-loop-convert): leaving as is for readability
        for( size_t x = 0; x < MM_REG_SIZE; x++ ) {
            shared_ptr_fast<mm_submap> &sm = submaps[x][y];
            sm = make_shared_fast<mm_submap>();
            if( jsin.test_null() ) {
                jsin.skip_null();
            } else {
                sm->deserialize( jsin );
            }
        }
    }
    jsin.end_array();
}

void map_memory::load_legacy( JsonIn &jsin )
{
    struct mig_elem {
        int symbol;
        memorized_terrain_tile tile;
    };
    std::map<tripoint_abs_ms, mig_elem> elems;

    jsin.start_array();
    jsin.start_array();
    while( !jsin.end_array() ) {
        jsin.start_array();
        tripoint_abs_ms p;
        p.x() = jsin.get_int();
        p.y() = jsin.get_int();
        p.z() = jsin.get_int();
        mig_elem &elem = elems[p];
        elem.tile.tile = jsin.get_string();
        elem.tile.subtile = jsin.get_int();
        elem.tile.rotation = jsin.get_int();
        jsin.end_array();
    }
    jsin.start_array();
    while( !jsin.end_array() ) {
        jsin.start_array();
        tripoint_abs_ms p;
        p.x() = jsin.get_int();
        p.y() = jsin.get_int();
        p.z() = jsin.get_int();
        elems[p].symbol = jsin.get_int();
        jsin.end_array();
    }
    jsin.end_array();

    for( const std::pair<const tripoint_abs_ms, mig_elem> &elem : elems ) {
        const auto cp = project_remain<coords::sm>( elem.first );
        shared_ptr_fast<mm_submap> sm = find_submap( cp.quotient_tripoint );
        if( !sm ) {
            sm = allocate_submap( cp.quotient_tripoint );
        }
        if( elem.second.tile != mm_submap::default_tile ) {
            sm->set_tile( cp.remainder, elem.second.tile );
        }
        if( elem.second.symbol != mm_submap::default_symbol ) {
            sm->set_symbol( cp.remainder, elem.second.symbol );
        }
    }
}

void point::deserialize( JsonIn &jsin )
{
    jsin.start_array();
    x = jsin.get_int();
    y = jsin.get_int();
    jsin.end_array();
}

void point::serialize( JsonOut &jsout ) const
{
    jsout.start_array();
    jsout.write( x );
    jsout.write( y );
    jsout.end_array();
}

void tripoint::deserialize( JsonIn &jsin )
{
    jsin.start_array();
    x = jsin.get_int();
    y = jsin.get_int();
    z = jsin.get_int();
    jsin.end_array();
}

void tripoint::serialize( JsonOut &jsout ) const
{
    jsout.start_array();
    jsout.write( x );
    jsout.write( y );
    jsout.write( z );
    jsout.end_array();
}

void addiction::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "type_enum", type );
    json.member( "intensity", intensity );
    json.member( "sated", sated );
    json.end_object();
}

void addiction::deserialize( JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    type = static_cast<add_type>( jo.get_int( "type_enum" ) );
    intensity = jo.get_int( "intensity" );
    jo.read( "sated", sated );
}

void serialize( const recipe_subset &value, JsonOut &jsout )
{
    jsout.start_array();
    for( const auto &entry : value ) {
        jsout.write( entry->ident() );
    }
    jsout.end_array();
}

void deserialize( recipe_subset &value, JsonIn &jsin )
{
    value.clear();
    jsin.start_array();
    while( !jsin.end_array() ) {
        value.include( &recipe_id( jsin.get_string() ).obj() );
    }
}

static void serialize( const item_comp &value, JsonOut &jsout )
{
    jsout.start_object();

    jsout.member( "type", value.type );
    jsout.member( "count", value.count );
    jsout.member( "recoverable", value.recoverable );

    jsout.end_object();
}

static void serialize( const tool_comp &value, JsonOut &jsout )
{
    jsout.start_object();

    jsout.member( "type", value.type );
    jsout.member( "count", value.count );
    jsout.member( "recoverable", value.recoverable );

    jsout.end_object();
}

static void deserialize( item_comp &value, JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    jo.read( "type", value.type );
    jo.read( "count", value.count );
    jo.read( "recoverable", value.recoverable );
}

static void deserialize( tool_comp &value, JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    jo.read( "type", value.type );
    jo.read( "count", value.count );
    jo.read( "recoverable", value.recoverable );
}

static void serialize( const quality_requirement &value, JsonOut &jsout )
{
    jsout.start_object();

    jsout.member( "type", value.type );
    jsout.member( "count", value.count );
    jsout.member( "level", value.level );

    jsout.end_object();
}

static void deserialize( quality_requirement &value, JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    jo.read( "type", value.type );
    jo.read( "count", value.count );
    jo.read( "level", value.level );
}

void iuse_location::serialize( JsonOut &jsout ) const
{
    jsout.start_array();
    jsout.write( loc );
    jsout.write( count );
    jsout.end_array();
}

void iuse_location::deserialize( JsonIn &jsin )
{
    jsin.start_array();
    jsin.read( loc );
    jsin.read( count );
    jsin.end_array();
}

void pickup::act_item::serialize( JsonOut &jsout ) const
{
    jsout.start_array();
    jsout.write( loc );
    jsout.write( count );
    jsout.write( consumed_moves );
    jsout.end_array();
}

void pickup::act_item::deserialize( JsonIn &jsin )
{
    jsin.start_array();
    jsin.read( loc );
    jsin.read( count );
    jsin.read( consumed_moves );
    jsin.end_array();
}

void kill_tracker::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "kills" );
    jsout.start_object();
for( auto &elem : kills ) {
    jsout.member( elem.first.str(), elem.second );
    }
    jsout.end_object();

    jsout.member( "npc_kills" );
    jsout.start_array();
for( auto &elem : npc_kills ) {
    jsout.write( elem );
    }
    jsout.end_array();
    jsout.end_object();
}

void kill_tracker::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    for( const JsonMember member : data.get_object( "kills" ) ) {
        kills[mtype_id( member.name() )] = member.get_int();
    }

    for( const std::string npc_name : data.get_array( "npc_kills" ) ) {
        npc_kills.push_back( npc_name );
    }
}

void cata_variant::serialize( JsonOut &jsout ) const
{
    jsout.start_array();
    jsout.write_as_string( type_ );
    jsout.write( value_ );
    jsout.end_array();
}

void cata_variant::deserialize( JsonIn &jsin )
{
    jsin.start_array();
    if( !( jsin.read( type_ ) && jsin.read( value_ ) ) ) {
        jsin.error( "Failed to read cata_variant" );
    }
    jsin.end_array();
}

void event_multiset::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    std::vector<counts_type::value_type> copy( counts_.begin(), counts_.end() );
    jsout.member( "event_counts", copy );
    jsout.end_object();
}

void event_multiset::deserialize( JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    std::vector<std::pair<cata::event::data_type, int>> copy;
    jo.read( "event_counts", copy );
    counts_ = { copy.begin(), copy.end() };
}

void stats_tracker::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "data", data );
    jsout.member( "initial_scores", initial_scores );
    jsout.end_object();
}

void stats_tracker::deserialize( JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    jo.read( "data", data );
    for( std::pair<const event_type, event_multiset> &d : data ) {
        d.second.set_type( d.first );
    }
    jo.read( "initial_scores", initial_scores );
}

void submap::store( JsonOut &jsout ) const
{
    jsout.member( "turn_last_touched", last_touched );
    jsout.member( "temperature", temperature );

    // Terrain is saved using a simple RLE scheme.  Legacy saves don't have
    // this feature but the algorithm is backward compatible.
    jsout.member( "terrain" );
    jsout.start_array();
    std::string last_id;
    int num_same = 1;
for( const auto sm_ms : submap_tiles() ) {
    const std::string this_id = ter[sm_ms.x()][sm_ms.y()].obj().id.str();
        if( !last_id.empty() ) {
            if( this_id == last_id ) {
                num_same++;
            } else {
                if( num_same == 1 ) {
                    // if there's only one element don't write as an array
                    jsout.write( last_id );
                } else {
                    jsout.start_array();
                    jsout.write( last_id );
                    jsout.write( num_same );
                    jsout.end_array();
                    num_same = 1;
                }
                last_id = this_id;
            }
        } else {
            last_id = this_id;
        }
    }
    // Because of the RLE scheme we have to do one last pass
    if( num_same == 1 ) {
    jsout.write( last_id );
    } else {
        jsout.start_array();
        jsout.write( last_id );
        jsout.write( num_same );
        jsout.end_array();
    }
    jsout.end_array();

    // Write out the radiation array in a simple RLE scheme.
    // written in intensity, count pairs
    jsout.member( "radiation" );
    jsout.start_array();
    int lastrad = -1;
    int count = 0;
for( const auto p : submap_tiles() ) {
    // Save radiation, re-examine this because it doesn't look like it works right
    int r = get_radiation( p );
        if( r == lastrad ) {
            count++;
        } else {
            if( count ) {
                jsout.write( count );
            }
            jsout.write( r );
            lastrad = r;
            count = 1;
        }
    }
    jsout.write( count );
    jsout.end_array();

    // Serialize scent values using RLE (value, count pairs).
    // Omitted entirely when all tiles are zero to keep typical saves compact.
    if( std::ranges::any_of( &scent_values[0][0], &scent_values[0][0] + SEEX * SEEY,
    []( int v ) { return v != 0; } ) ) {
    jsout.member( "scent_values" );
        jsout.start_array();
        int last_scent = -1;
        int scent_count = 0;
        std::ranges::for_each(
            cata::views::cartesian_product( std::views::iota( 0, SEEY ),
                                            std::views::iota( 0, SEEX ) ),
        [&]( auto ji ) {
            auto [j, i] = ji;
            auto v = scent_values[i][j];
            if( v == last_scent ) {
                scent_count++;
            } else {
                if( scent_count ) {
                    jsout.write( scent_count );
                }
                jsout.write( v );
                last_scent = v;
                scent_count = 1;
            }
        } );
        jsout.write( scent_count );
        jsout.end_array();
    }

    jsout.member( "furniture" );
    jsout.start_array();
for( const auto p : submap_tiles() ) {
    // Save furniture
    if( get_furn( p ) ) {
            jsout.start_array();
            jsout.write( p.x() );
            jsout.write( p.y() );
            jsout.write( get_furn( p ).obj().id );
            jsout.end_array();
        }
    }
    jsout.end_array();

    jsout.member( "items" );
    jsout.start_array();
for( const auto sm_ms : submap_tiles() ) {
    if( !itm[sm_ms.x()][sm_ms.y()].empty() ) {
            jsout.write( sm_ms.x() );
            jsout.write( sm_ms.y() );
            jsout.write( itm[sm_ms.x()][sm_ms.y()] );
        }
    }
    jsout.end_array();

    jsout.member( "traps" );
    jsout.start_array();
for( const auto p : submap_tiles() ) {
    // Save traps
    if( get_trap( p ) ) {
            jsout.start_array();
            jsout.write( p.x() );
            jsout.write( p.y() );
            // TODO: jsout should support writing an id like jsout.write( trap_id )
            jsout.write( get_trap( p ).id().str() );
            jsout.end_array();
        }
    }
    jsout.end_array();

    jsout.member( "fields" );
    jsout.start_array();
for( const auto sm_ms : submap_tiles() ) {
    // Save fields
    if( fld[sm_ms.x()][sm_ms.y()].field_count() > 0 ) {
            jsout.write( sm_ms.x() );
            jsout.write( sm_ms.y() );
            jsout.start_array();
            for( auto &elem : fld[sm_ms.x()][sm_ms.y()] ) {
                const field_entry &cur = elem.second;
                jsout.write( cur.get_field_type().id() );
                jsout.write( cur.get_field_intensity() );
                jsout.write( cur.get_field_age() );
            }
            jsout.end_array();
        }
    }
    jsout.end_array();

    // Write out as array of arrays of single entries
    jsout.member( "cosmetics" );
    jsout.start_array();
for( const auto &cosm : cosmetics ) {
    jsout.start_array();
        jsout.write( cosm.pos.x() );
        jsout.write( cosm.pos.y() );
        jsout.write( cosm.type );
        jsout.write( cosm.str );
        jsout.end_array();
    }
    jsout.end_array();

    // Output the spawn points
    jsout.member( "spawns" );
    jsout.start_array();
for( auto &elem : spawns ) {
    jsout.start_array();
        // TODO: json should know how to write string_ids
        jsout.write( elem.type.str() );
        jsout.write( elem.count );
        jsout.write( elem.pos.x() );
        jsout.write( elem.pos.y() );
        jsout.write( elem.faction_id );
        jsout.write( elem.mission_id );
        jsout.write( elem.is_friendly() );
        jsout.write( elem.name );
        jsout.end_array();
    }
    jsout.end_array();

    jsout.member( "vehicles" );
    jsout.start_array();
for( auto &elem : vehicles ) {
    // json lib doesn't know how to turn a vehicle * into a vehicle,
    // so we have to iterate manually.
    jsout.write( *elem );
    }
    jsout.end_array();

    jsout.member( "partial_constructions" );
    jsout.start_array();
for( auto &elem : partial_constructions ) {
    jsout.write( elem.first.x() );
        jsout.write( elem.first.y() );
        jsout.write( elem.first.z() );
        jsout.write( elem.second->counter );
        jsout.write( elem.second->id.id() );
        jsout.start_array();
        for( auto &it : elem.second->components ) {
            jsout.write( it );
        }
        jsout.end_array();
    }
    jsout.end_array();

    if( legacy_computer ) {
    // it's possible that no access to computers has been made and legacy_computer
    // is not cleared
    jsout.member( "computers", *legacy_computer );
    } else if( !computers.empty() ) {
    jsout.member( "computers" );
        jsout.start_array();
        for( auto &elem : computers ) {
            jsout.write( elem.first );
            jsout.write( elem.second );
        }
        jsout.end_array();
    }

    jsout.member( "active_furniture" );
    jsout.start_array();
for( auto &pr : active_furniture ) {
    jsout.write( pr.first );
        pr.second.serialize( jsout );
    }
    jsout.end_array();

    jsout.member( "furniture_vars" );
    jsout.start_array();
for( const auto &[key, value] : frn_vars ) {
    if( value.empty() ) {
            continue;
        }
        jsout.write( key );
        jsout.write( value );
    }
    jsout.end_array();

    jsout.member( "terrain_vars" );
    jsout.start_array();
for( const auto &[key, value] : ter_vars ) {
    if( value.empty() ) {
            continue;
        }
        jsout.write( key );
        jsout.write( value );
    }
    jsout.end_array();
    jsout.member( "transformer_last_run" );
    jsout.start_array();
for( const auto &pr : transformer_last_run ) {
    jsout.write( pr.first );
        jsout.write( pr.second );
    }
    jsout.end_array();
}

void submap::load( JsonIn &jsin, const std::string &member_name, int version,
                   const tripoint_abs_ms offset )
{
    if( member_name == "turn_last_touched" ) {
        last_touched = calendar::turn_zero + time_duration::from_turns( jsin.get_int() );
        // Guard against corrupted saves: last_touched must not be in the future.
        last_touched = std::min( last_touched, calendar::turn );
    } else if( member_name == "temperature" ) {
        temperature = jsin.get_int();
    } else if( member_name == "terrain" ) {
        // TODO: try block around this to error out if we come up short?
        jsin.start_array();
        // terrain is encoded using simple RLE
        int remaining = 0;
        int_id<ter_t> iid;
        for( const auto sm_ms : submap_tiles() ) {
            if( !remaining ) {
                if( jsin.test_string() ) {
                    iid = ter_str_id( jsin.get_string() ).id();
                } else if( jsin.test_array() ) {
                    jsin.start_array();
                    iid = ter_str_id( jsin.get_string() ).id();
                    remaining = jsin.get_int() - 1;
                    jsin.end_array();
                } else {
                    debugmsg( "Mapbuffer terrain data is corrupt, expected string or array." );
                }
            } else {
                --remaining;
            }
            ter[sm_ms.x()][sm_ms.y()] = iid;
        }
        if( remaining ) {
            debugmsg( "Mapbuffer terrain data is corrupt, tile data remaining." );
        }
        jsin.end_array();
    } else if( member_name == "radiation" ) {
        int rad_cell = 0;
        jsin.start_array();
        while( !jsin.end_array() ) {
            int rad_strength = jsin.get_int();
            int rad_num = jsin.get_int();
            for( int i = 0; i < rad_num; ++i ) {
                if( rad_cell < SEEX * SEEY ) {
                    set_radiation( { 0 % SEEX, rad_cell / SEEX }, rad_strength );
                    rad_cell++;
                }
            }
        }
    } else if( member_name == "scent_values" ) {
        // Load RLE-encoded scent values (value, count pairs).
        int scent_cell = 0;
        jsin.start_array();
        while( !jsin.end_array() ) {
            const int val = jsin.get_int();
            const int num = jsin.get_int();
            for( int i = 0; i < num && scent_cell < SEEX * SEEY; ++i, ++scent_cell ) {
                scent_values[scent_cell % SEEX][scent_cell / SEEX] = val;
            }
        }
    } else if( member_name == "furniture" ) {
        jsin.start_array();
        while( !jsin.end_array() ) {
            jsin.start_array();
            int i = jsin.get_int();
            int j = jsin.get_int();
            frn[i][j] = furn_id( jsin.get_string() );
            jsin.end_array();
        }
    } else if( member_name == "items" ) {
        jsin.start_array();
        while( !jsin.end_array() ) {
            int i = jsin.get_int();
            int j = jsin.get_int();
            const point_sm_ms p( i, j );
            jsin.start_array();
            while( !jsin.end_array() ) {
                detached_ptr<item> tmp;
                jsin.read( tmp );

                if( tmp->is_emissive() ) {
                    update_lum_add( p, *tmp );
                }

                if( savegame_loading_version >= 27 && version < 27 ) {
                    tmp->legacy_fast_forward_time();
                }
                item &obj = *tmp;
                itm[p.x()][p.y()].push_back( std::move( tmp ) );
                if( obj.needs_processing() ) {
                    active_items.add( obj );
                }
            }
        }
        for( auto &it1 : itm ) {
            for( auto &it2 : it1 ) {
                std::vector<detached_ptr<item>> cleared = it2.clear();
                to_cbc_migration::migrate( cleared );
                for( detached_ptr<item> &item : cleared ) {
                    it2.push_back( std::move( item ) );
                }
            }
        }
    } else if( member_name == "traps" ) {
        jsin.start_array();
        while( !jsin.end_array() ) {
            jsin.start_array();
            int i = jsin.get_int();
            int j = jsin.get_int();
            const point_sm_ms p( i, j );
            // TODO: jsin should support returning an id like jsin.get_id<trap>()
            trp[p.x()][p.y()] = trap_str_id( jsin.get_string() ).id();
            trap_cache.push_back( p ); // null traps are not serialized, so this is always valid
            jsin.end_array();
        }
    } else if( member_name == "fields" ) {
        jsin.start_array();
        while( !jsin.end_array() ) {
            // Coordinates loop
            int i = jsin.get_int();
            int j = jsin.get_int();
            jsin.start_array();
            while( !jsin.end_array() ) {
                // TODO: Check enum->string migration below
                int type_int = 0;
                std::string type_str;
                if( jsin.test_int() ) {
                    type_int = jsin.get_int();
                } else {
                    type_str = jsin.get_string();
                }
                int intensity = jsin.get_int();
                int age = jsin.get_int();
                field_type_id ft;
                if( !type_str.empty() ) {
                    ft = field_type_id( type_str );
                } else {
                    ft = field_types::get_field_type_by_legacy_enum( type_int ).id;
                }
                if( fld[i][j].find_field( ft ) == nullptr ) {
                    field_count++;
                    field_cache.push_back( point_sm_ms( i, j ) );
                }
                fld[i][j].add_field( ft, intensity, time_duration::from_turns( age ) );
            }
        }
    } else if( member_name == "graffiti" ) {
        jsin.start_array();
        while( !jsin.end_array() ) {
            jsin.start_array();
            int i = jsin.get_int();
            int j = jsin.get_int();
            const point_sm_ms p( i, j );
            set_graffiti( p, jsin.get_string() );
            jsin.end_array();
        }
    } else if( member_name == "cosmetics" ) {
        jsin.start_array();
        std::map<std::string, std::string> tcosmetics;

        while( !jsin.end_array() ) {
            jsin.start_array();
            int i = jsin.get_int();
            int j = jsin.get_int();
            const point_sm_ms p( i, j );
            std::string type, str;
            // Try to read as current format
            if( jsin.test_string() ) {
                type = jsin.get_string();
                str = jsin.get_string();
                insert_cosmetic( p, type, str );
            } else {
                // Otherwise read as most recent old format
                jsin.read( tcosmetics );
                for( auto &cosm : tcosmetics ) {
                    insert_cosmetic( p, cosm.first, cosm.second );
                }
                tcosmetics.clear();
            }

            jsin.end_array();
        }
    } else if( member_name == "spawns" ) {
        jsin.start_array();
        while( !jsin.end_array() ) {
            jsin.start_array();
            // TODO: json should know how to read an string_id
            const mtype_id type = mtype_id( jsin.get_string() );
            int count = jsin.get_int();
            int i = jsin.get_int();
            int j = jsin.get_int();
            const point_sm_ms p( i, j );
            int faction_id = jsin.get_int();
            int mission_id = jsin.get_int();
            bool friendly = jsin.get_bool();
            std::string name = jsin.get_string();
            jsin.end_array();
            spawn_point tmp( type, count, p, faction_id, mission_id,
                             spawn_point::friendly_to_spawn_disposition( friendly ), name );
            spawns.push_back( tmp );
        }
    } else if( member_name == "vehicles" ) {
        jsin.start_array();
        while( !jsin.end_array() ) {
            std::unique_ptr<vehicle> tmp = std::make_unique<vehicle>();
            jsin.read( *tmp );
            vehicles.push_back( std::move( tmp ) );
        }
    } else if( member_name == "partial_constructions" ) {
        jsin.start_array();
        while( !jsin.end_array() ) {
            int i = jsin.get_int();
            int j = jsin.get_int();
            int k = jsin.get_int();
            auto sm_pt = tripoint_sm_ms( i, j, k );
            auto abs_pt = tripoint_abs_ms( offset.x() + i, offset.y() + j, k );
            std::unique_ptr<partial_con> pc = std::make_unique<partial_con>( abs_pt );
            pc->counter = jsin.get_int();
            if( jsin.test_int() ) {
                // Oops, int id incorrectly saved by legacy code, just load it and hope for the best
                pc->id = construction_id( jsin.get_int() );
            } else {
                pc->id = construction_str_id( jsin.get_string() ).id();
            }
            jsin.start_array();
            while( !jsin.end_array() ) {
                detached_ptr<item> tmp;
                jsin.read( tmp );
                pc->components.push_back( std::move( tmp ) );
            }
            partial_constructions[sm_pt] = std::move( pc );
        }
    } else if( member_name == "computers" ) {
        if( jsin.test_array() ) {
            jsin.start_array();
            while( !jsin.end_array() ) {
                point loc;
                jsin.read( loc );
                auto new_comp_it = computers.emplace( loc, computer( "BUGGED_COMPUTER", -100 ) ).first;
                jsin.read( new_comp_it->second );
            }
        } else {
            // only load legacy data here, but do not update to std::map, since
            // the terrain may not have been loaded yet.
            legacy_computer = std::make_unique<computer>( "BUGGED_COMPUTER", -100 );
            jsin.read( *legacy_computer );
        }
    } else if( member_name == "active_furniture" ) {
        jsin.start_array();
        while( !jsin.end_array() ) {
            point_sm_ms p;
            jsin.read( p );
            active_furniture[p].deserialize( jsin );
        }
    } else if( member_name == "furniture_vars" ) {
        jsin.start_array();
        while( !jsin.end_array() ) {
            point_sm_ms loc;
            jsin.read( loc );
            auto &vars = frn_vars[loc];
            jsin.read( vars );
        }
    } else if( member_name == "terrain_vars" ) {
        jsin.start_array();
        while( !jsin.end_array() ) {
            point_sm_ms loc;
            jsin.read( loc );
            auto &vars = ter_vars[loc];
            jsin.read( vars );
        }
    } else if( member_name == "transformer_last_run" ) {
        jsin.start_array();
        while( !jsin.end_array() ) {
            point_sm_ms p;
            time_point t;
            jsin.read( p );
            jsin.read( t );
            transformer_last_run[p] = t;
        }
    } else {
        jsin.skip_value();
    }
}

void advanced_inv_pane_save_state::serialize( JsonOut &json, const std::string &prefix ) const
{
    json.member( prefix + "sort_idx", sort_idx );
    json.member( prefix + "filter", filter );
    json.member( prefix + "area_idx", area_idx );
    json.member( prefix + "selected_idx", selected_idx );
    json.member( prefix + "in_vehicle", in_vehicle );
}

void advanced_inv_pane_save_state::deserialize( const JsonObject &jo,
        const std::string &prefix )
{

    jo.read( prefix + "sort_idx", sort_idx );
    jo.read( prefix + "filter", filter );
    jo.read( prefix + "area_idx", area_idx );
    jo.read( prefix + "selected_idx", selected_idx );
    jo.read( prefix + "in_vehicle", in_vehicle );
}

void advanced_inv_save_state::serialize( JsonOut &json, const std::string &prefix ) const
{
    json.member( prefix + "active_left", active_left );
    json.member( prefix + "last_popup_dest", last_popup_dest );

    json.member( prefix + "saved_area", saved_area );
    json.member( prefix + "saved_area_right", saved_area_right );
    pane.serialize( json, prefix + "pane_" );
    pane_right.serialize( json, prefix + "pane_right_" );
}

void advanced_inv_save_state::deserialize( const JsonObject &jo, const std::string &prefix )
{
    jo.read( prefix + "active_left", active_left );
    jo.read( prefix + "last_popup_dest", last_popup_dest );

    jo.read( prefix + "saved_area", saved_area );
    jo.read( prefix + "saved_area_right", saved_area_right );
    pane.area_idx = saved_area;
    pane_right.area_idx = saved_area_right;
    pane.deserialize( jo, prefix + "pane_" );
    pane_right.deserialize( jo, prefix + "pane_right_" );
}

void wisheffect_state::serialize( JsonOut &json ) const
{
    // Empty for now
    json.start_object();
    json.end_object();
}

void wisheffect_state::deserialize( const JsonObject &jo )
{
    // Empty for now
    jo.allow_omitted_members();
}

void debug_menu_state::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "effect", effect );
    json.end_object();
}

void debug_menu_state::deserialize( const JsonObject &jo )
{
    jo.read( "effect", effect );
}

void uistatedata::serialize( JsonOut &json ) const
{
    const unsigned int input_history_save_max = 25;
    json.start_object();

    transfer_save.serialize( json, "transfer_save_" );

    /**** if you want to save whatever so it's whatever when the game is started next, declare here and.... ****/
    // non array stuffs
    json.member( "adv_inv_container_location", adv_inv_container_location );
    json.member( "adv_inv_container_index", adv_inv_container_index );
    json.member( "adv_inv_container_in_vehicle", adv_inv_container_in_vehicle );
    json.member( "adv_inv_container_type", adv_inv_container_type );
    json.member( "adv_inv_container_content_type", adv_inv_container_content_type );
    json.member( "debug_menu", debug_menu );
    json.member( "editmap_nsa_viewmode", editmap_nsa_viewmode );
    json.member( "overmap_blinking", overmap_blinking );
    json.member( "overmap_show_overlays", overmap_show_overlays );
    json.member( "overmap_show_map_notes", overmap_show_map_notes );
    json.member( "overmap_show_land_use_codes", overmap_show_land_use_codes );
    json.member( "overmap_show_city_labels", overmap_show_city_labels );
    json.member( "overmap_show_hordes", overmap_show_hordes );
    json.member( "overmap_show_forest_trails", overmap_show_forest_trails );
    json.member( "overmap_highlighted_omts", overmap_highlighted_omts );
    json.member( "vmenu_show_items", vmenu_show_items );
    json.member( "list_item_sort", list_item_sort );
    json.member( "read_items", read_items );
    json.member( "list_item_filter_active", list_item_filter_active );
    json.member( "list_item_downvote_active", list_item_downvote_active );
    json.member( "list_item_priority_active", list_item_priority_active );
    json.member( "hidden_recipes", hidden_recipes );
    json.member( "favorite_recipes", favorite_recipes );
    json.member( "expanded_recipes", expanded_recipes );
    json.member( "read_recipes", read_recipes );
    json.member( "recent_recipes", recent_recipes );
    json.member( "favorite_construct_recipes", favorite_construct_recipes );
    json.member( "bionic_ui_sort_mode", bionic_sort_mode );
    json.member( "overmap_debug_weather", overmap_debug_weather );
    json.member( "overmap_visible_weather", overmap_visible_weather );
    json.member( "msg_window_wide_display", msg_window_wide_display );
    json.member( "msg_window_full_height_display", msg_window_full_height_display );
    json.member( "hud_soma_expanded", hud_soma_expanded );

    json.member( "input_history" );
    json.start_object();
    for( auto &e : input_history ) {
        json.member( e.first );
        const std::vector<std::string> &history = e.second;
        json.start_array();
        int save_start = 0;
        if( history.size() > input_history_save_max ) {
            save_start = history.size() - input_history_save_max;
        }
        for( std::vector<std::string>::const_iterator hit = history.begin() + save_start;
             hit != history.end(); ++hit ) {
            json.write( *hit );
        }
        json.end_array();
    }
    json.end_object(); // input_history

    json.end_object();
}

void uistatedata::deserialize( const JsonObject &jo )
{
    transfer_save.deserialize( jo, "transfer_save_" );

    // the rest
    jo.read( "adv_inv_container_location", adv_inv_container_location );
    jo.read( "adv_inv_container_index", adv_inv_container_index );
    jo.read( "adv_inv_container_in_vehicle", adv_inv_container_in_vehicle );
    jo.read( "adv_inv_container_type", adv_inv_container_type );
    jo.read( "adv_inv_container_content_type", adv_inv_container_content_type );
    jo.read( "debug_menu", debug_menu );
    jo.read( "editmap_nsa_viewmode", editmap_nsa_viewmode );
    jo.read( "overmap_blinking", overmap_blinking );
    jo.read( "overmap_show_overlays", overmap_show_overlays );
    jo.read( "overmap_show_map_notes", overmap_show_map_notes );
    jo.read( "overmap_show_land_use_codes", overmap_show_land_use_codes );
    jo.read( "overmap_show_city_labels", overmap_show_city_labels );
    jo.read( "overmap_show_hordes", overmap_show_hordes );
    jo.read( "overmap_show_forest_trails", overmap_show_forest_trails );
    jo.read( "overmap_highlighted_omts", overmap_highlighted_omts );
    jo.read( "hidden_recipes", hidden_recipes );
    jo.read( "favorite_recipes", favorite_recipes );
    jo.read( "expanded_recipes", expanded_recipes );
    jo.read( "read_recipes", read_recipes );
    jo.read( "recent_recipes", recent_recipes );
    jo.read( "favorite_construct_recipes", favorite_construct_recipes );
    jo.read( "bionic_ui_sort_mode", bionic_sort_mode );
    jo.read( "overmap_debug_weather", overmap_debug_weather );
    jo.read( "overmap_visible_weather", overmap_visible_weather );
    jo.read( "msg_window_wide_display", msg_window_wide_display );
    jo.read( "msg_window_full_height_display", msg_window_full_height_display );
    jo.read( "hud_soma_expanded", hud_soma_expanded );

    if( !jo.read( "vmenu_show_items", vmenu_show_items ) ) {
        // This is an old save: 1 means view items, 2 means view monsters,
        // -1 means uninitialized
        vmenu_show_items = jo.get_int( "list_item_mon", -1 ) != 2;
    }

    jo.read( "list_item_sort", list_item_sort );
    jo.read( "read_items", read_items );
    jo.read( "list_item_filter_active", list_item_filter_active );
    jo.read( "list_item_downvote_active", list_item_downvote_active );
    jo.read( "list_item_priority_active", list_item_priority_active );

    for( const JsonMember member : jo.get_object( "input_history" ) ) {
        std::vector<std::string> &v = gethistory( member.name() );
        v.clear();
        for( const std::string line : member.get_array() ) {
            v.push_back( line );
        }
    }
    // fetch list_item settings from input_history
    if( !gethistory( "item_filter" ).empty() ) {
        list_item_filter = gethistory( "item_filter" ).back();
    }
    if( !gethistory( "list_item_downvote" ).empty() ) {
        list_item_downvote = gethistory( "list_item_downvote" ).back();
    }
    if( !gethistory( "list_item_priority" ).empty() ) {
        list_item_priority = gethistory( "list_item_priority" ).back();
    }
}
