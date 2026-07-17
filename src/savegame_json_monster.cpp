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

///// monster.h

void monster::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    load( data );
}

auto monster::deserialize_from_overmap( JsonIn &jsin, const point_abs_om &om_pos,
                                        const tripoint_om_sm &submap_pos ) -> void
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    load( data, legacy_position_context{ om_pos, submap_pos } );
}

auto monster::load( const JsonObject &data,
                    const std::optional<legacy_position_context> &legacy_context ) -> void
{
    Creature::load( data );

    std::string sidtmp;
    // load->str->int
    data.read( "typeid", sidtmp );
    type = &mtype_id( sidtmp ).obj();

    data.read( "unique_name", unique_name );

    auto legacy_bub_pos = tripoint_bub_ms::zero();
    const auto has_legacy_x = data.read( "posx", legacy_bub_pos.x() );
    const auto has_legacy_y = data.read( "posy", legacy_bub_pos.y() );
    if( !data.read( "posz", legacy_bub_pos.z() ) ) {
        legacy_bub_pos.z() = g != nullptr ? g->get_levz() : 0;
    }
    auto stored_pos_abs = tripoint_abs_ms::zero();
    if( data.read( "pos_abs", stored_pos_abs ) ) {
    pos_abs = stored_pos_abs;
}
if( has_legacy_x && has_legacy_y ) {
    if( legacy_context ) {
            const auto abs_sm_pos = project_combine( legacy_context->om_pos, legacy_context->submap_pos );
            const auto legacy_remainder = project_remain<coords::sm>( legacy_bub_pos );
            pos_abs = project_combine( abs_sm_pos, legacy_remainder.remainder );
        } else {
            pos_abs = get_map().bub_to_abs( legacy_bub_pos );
        }
    }

    wandf = 0;
    wander_pos = get_map().abs_to_bub( pos_abs );
    if( !legacy_context ) {
    auto stored_wander_pos_abs = tripoint_abs_ms::zero();
        if( data.read( "wander_pos_abs", stored_wander_pos_abs ) ) {
            data.read( "wandf", wandf );
            wander_pos = get_map().abs_to_bub( stored_wander_pos_abs );
        } else {
            const auto has_legacy_wander_x = data.read( "wandx", wander_pos.x() );
            const auto has_legacy_wander_y = data.read( "wandy", wander_pos.y() );
            if( has_legacy_wander_x && has_legacy_wander_y ) {
                if( !data.read( "wandz", wander_pos.z() ) ) {
                    wander_pos.z() = legacy_bub_pos.z();
                }
                data.read( "wandf", wandf );
            }
        }
    }
    if( data.has_object( "tied_item" ) ) {
    JsonIn *tied_item_json = data.get_raw( "tied_item" );
        set_tied_item( item::spawn( *tied_item_json ) );
    }
    if( data.has_object( "tack_item" ) ) {
    JsonIn *tack_item_json = data.get_raw( "tack_item" );
        set_tack_item( item::spawn( *tack_item_json ) );
    }
    if( data.has_object( "armor_item" ) ) {
    JsonIn *armor_item_json = data.get_raw( "armor_item" );
        set_armor_item( item::spawn( *armor_item_json ) );
    }
    if( data.has_object( "storage_item" ) ) {
    JsonIn *storage_item_json = data.get_raw( "storage_item" );
        set_storage_item( item::spawn( *storage_item_json ) );
    }
    if( data.has_object( "battery_item" ) ) {
    JsonIn *battery_item_json = data.get_raw( "battery_item" );
        set_battery_item( item::spawn( *battery_item_json ) );
    }
    data.read( "hp", hp );

    // sp_timeout indicates an old save, prior to the special_attacks refactor
    if( data.has_array( "sp_timeout" ) ) {
    JsonArray parray = data.get_array( "sp_timeout" );
        size_t index = 0;
        int ptimeout = 0;
        while( parray.has_more() && index < type->special_attacks_names.size() ) {
            if( parray.read_next( ptimeout ) ) {
                // assume timeouts saved in same order as current monsters.json listing
                const std::string &aname = type->special_attacks_names[index++];
                auto &entry = special_attacks[aname];
                if( ptimeout >= 0 ) {
                    entry.cooldown = ptimeout;
                } else { // -1 means disabled, unclear what <-1 values mean in old saves
                    entry.cooldown = type->special_attacks.at( aname )->cooldown;
                    entry.enabled = false;
                }
            }
        }
    }

    // special_attacks indicates a save after the special_attacks refactor
    if( data.has_object( "special_attacks" ) ) {
    for( const JsonMember member : data.get_object( "special_attacks" ) ) {
            JsonObject saobject = member.get_object();
            saobject.allow_omitted_members();
            auto &entry = special_attacks[member.name()];
            entry.cooldown = saobject.get_int( "cooldown" );
            entry.enabled = saobject.get_bool( "enabled" );
        }
    }

    // make sure the loaded monster has every special attack its type says it should have
for( auto &sa : type->special_attacks ) {
    const std::string &aname = sa.first;
    if( !special_attacks.contains( aname ) ) {
            auto &entry = special_attacks[aname];
            entry.cooldown = rng( 0, sa.second->cooldown );
        }
    }

    data.read( "friendly", friendly );
    data.read( "training_level", training_level );
    data.read( "mission_id", mission_id );
    data.read( "no_extra_death_drops", no_extra_death_drops );
    data.read( "dead", dead );
    data.read( "anger", anger );
    data.read( "morale", morale );

    if( data.has_member( "faction_anger" ) ) {
    JsonObject ja = data.get_object( "faction_anger" );
        for( const auto &member : ja ) {
            mfaction_str_id faction_str( member.name() );
            faction_anger[mfaction_id( faction_str )] = member.get_int();
        }
    }

    data.read( "hallucination", hallucination );
    data.read( "aggro_character", aggro_character );
    data.read( "stairscount", staircount ); // really?
    data.read( "fish_population", fish_population );
    // Load legacy plans.
    std::vector<tripoint> plans;
    data.read( "plans", plans );
    if( !plans.empty() ) {
    goal = tripoint_bub_ms( plans.back() );
    }

    data.read( "summon_time_limit", summon_time_limit );

    // This is relative to the monster so it isn't invalidated by map shifting.
    tripoint destination;
    data.read( "destination", destination );
    const auto load_bub_pos = has_legacy_x &&
                              has_legacy_y ? legacy_bub_pos : get_map().abs_to_bub( pos_abs );
    goal = load_bub_pos + destination;

    upgrades = data.get_bool( "upgrades", type->upgrades );
    upgrade_time = data.get_int( "upgrade_time", -1 );

    reproduces = data.get_bool( "reproduces", type->reproduces );
    baby_timer.reset();
    data.read( "baby_timer", baby_timer );
    if( baby_timer && *baby_timer == calendar::before_time_starts ) {
    baby_timer.reset();
    }

    data.read( "udder_timer", udder_timer );

    horde_attraction = static_cast<monster_horde_attraction>( data.get_int( "horde_attraction", 0 ) );

    data.read( "inv", inv );
    data.read( "corpse_components", corpse_components );
    data.read( "dragged_foe_id", dragged_foe_id );

    if( data.has_int( "ammo" ) && !type->starting_ammo.empty() ) {
    // Legacy loading for ammo.
    normalize_ammo( data.get_int( "ammo" ) );
    } else {
        data.read( "ammo", ammo );
        // legacy loading for milkable creatures, fix mismatch.
        if( has_flag( MF_MILKABLE ) && !type->starting_ammo.empty() && !ammo.empty() &&
            type->starting_ammo.begin()->first != ammo.begin()->first ) {
            const itype_id old_type = ammo.begin()->first;
            const int old_value = ammo.begin()->second;
            ammo[type->starting_ammo.begin()->first] = old_value;
            ammo.erase( old_type );
        }
    }

    faction = mfaction_str_id( data.get_string( "faction", "" ) );
    if( !data.read( "last_updated", last_updated ) ) {
    last_updated = calendar::turn;
}
data.read( "dimension_id", dimension_id_ );
data.read( "mounted_player_id", mounted_player_id );
data.read( "path", path );
data.read( "monster_flags", monster_flags );
}

/*
 * Save, json ed; serialization that won't break as easily. In theory.
 */
void monster::serialize( JsonOut &json ) const
{
    json.start_object();
    // This must be after the json object has been started, so any super class
    // puts their data into the same json object.
    store( json, true );
    json.end_object();
}

auto monster::serialize_for_overmap( JsonOut &json ) const -> void
{
    json.start_object();
    store( json, false );
    json.end_object();
}

auto monster::store( JsonOut &json, bool include_local_state ) const -> void
{
    Creature::store( json );
    json.member( "typeid", type->id );
    json.member( "unique_name", unique_name );
    json.member( "pos_abs", pos_abs );
    if( include_local_state ) {
    json.member( "wander_pos_abs", get_map().bub_to_abs( wander_pos ) );
        json.member( "wandf", wandf );
    }
    json.member( "hp", hp );
    json.member( "special_attacks", special_attacks );
    json.member( "friendly", friendly );
    json.member( "training_level", training_level );
    json.member( "fish_population", fish_population );
    json.member( "faction", faction.id().str() );
    json.member( "mission_id", mission_id );
    json.member( "no_extra_death_drops", no_extra_death_drops );
    json.member( "dead", dead );
    json.member( "anger", anger );
    json.member( "morale", morale );

    if( !faction_anger.empty() ) {
    json.member( "faction_anger" );
        json.start_object();
        for( const auto &pair : faction_anger ) {
            json.member( pair.first.id().str(), pair.second );
        }
        json.end_object();
    }

    json.member( "hallucination", hallucination );
    json.member( "aggro_character", aggro_character );
    json.member( "stairscount", staircount );
    if( tied_item ) {
    json.member( "tied_item", *tied_item );
    }
    if( tack_item ) {
    json.member( "tack_item", *tack_item );
    }
    if( armor_item ) {
    json.member( "armor_item", *armor_item );
    }
    if( storage_item ) {
    json.member( "storage_item", *storage_item );
    }
    if( battery_item ) {
    json.member( "battery_item", *battery_item );
    }
    // Store the relative position of the goal so it loads correctly after a map shift.
    json.member( "destination", goal - bub_pos() );
    json.member( "ammo", ammo );
    json.member( "upgrades", upgrades );
    json.member( "upgrade_time", upgrade_time );
    json.member( "last_updated", last_updated );
    if( !dimension_id_.empty() ) {
    json.member( "dimension_id", dimension_id_ );
    }
    json.member( "reproduces", reproduces );
    json.member( "baby_timer", baby_timer );
    json.member( "udder_timer", udder_timer );

    json.member( "summon_time_limit", summon_time_limit );

    if( horde_attraction > MHA_NULL && horde_attraction < NUM_MONSTER_HORDE_ATTRACTION ) {
    json.member( "horde_attraction", horde_attraction );
    }
    json.member( "inv", inv );
    json.member( "corpse_components", corpse_components );

    json.member( "dragged_foe_id", dragged_foe_id );
    // storing the rider
    json.member( "mounted_player_id", mounted_player_id );
    json.member( "path", path );
    json.member( "monster_flags", monster_flags );
}

void mon_special_attack::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "cooldown", cooldown );
    json.member( "enabled", enabled );
    json.end_object();
}

