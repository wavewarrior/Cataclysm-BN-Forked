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


void Character::load( const JsonObject &data )
{
    data.allow_omitted_members();
    Creature::load( data );

    if( !data.read( "abs_pos", position ) ) {
        // Legacy: posx/posy/posz were bubble-space at save time.
        // The map is always restored to the same abs_sub before characters load,
        // so bub_to_abs conversion here recovers the correct absolute position.
        tripoint_bub_ms legacy_bub;
        if( !data.read( "posx", legacy_bub.x() ) ) {
            debugmsg( "BAD PLAYER/NPC JSON: no 'abs_pos' or 'posx'?" );
        }
        data.read( "posy", legacy_bub.y() );
        if( !data.read( "posz", legacy_bub.z() ) && g != nullptr ) {
            legacy_bub.z() = g->get_levz();
        }
        position = get_map().bub_to_abs( legacy_bub );
    }
    // stats
    data.read( "str_cur", str_cur );
    data.read( "str_max", str_max );
    data.read( "dex_cur", dex_cur );
    data.read( "dex_max", dex_max );
    data.read( "int_cur", int_cur );
    data.read( "int_max", int_max );
    data.read( "per_cur", per_cur );
    data.read( "per_max", per_max );

    data.read( "str_bonus", str_bonus );
    data.read( "dex_bonus", dex_bonus );
    data.read( "per_bonus", per_bonus );
    data.read( "int_bonus", int_bonus );
    data.read( "omt_path", omt_path );

    std::string new_name;
    data.read( "name", new_name );
    if( !new_name.empty() ) {
        // Bugfix for name not having been saved properly
        name = new_name;
    }

    data.read( "base_age", init_age );
    data.read( "base_height", init_height );

    if( !data.read( "profession", prof ) || !prof.is_valid() ) {
        // We are likely an older profession which has since been removed so just set to default.
        // This is only cosmetic after game start.
        prof = profession::generic();
    }
    data.read( "custom_profession", custom_profession );

    // needs
    data.read( "thirst", thirst );
    data.read( "fatigue", fatigue );
    data.read( "sleep_deprivation", sleep_deprivation );
    data.read( "stored_calories", stored_calories );
    data.read( "radiation", radiation );
    data.read( "oxygen", oxygen );
    data.read( "pkill", pkill );

    data.read( "type_of_scent", type_of_scent );

    if( data.has_array( "ma_styles" ) ) {
        std::vector<matype_id> temp_styles;
        data.read( "ma_styles", temp_styles );
        bool temp_keep_hands_free = false;
        data.read( "keep_hands_free", temp_keep_hands_free );
        matype_id temp_selected_style;
        data.read( "style_selected", temp_selected_style );
        if( !temp_selected_style.is_valid() ) {
            temp_selected_style = matype_id( "style_none" );
        }
        martial_arts_data = pimpl<character_martial_arts>( character_martial_arts(
                                temp_styles, temp_selected_style, temp_keep_hands_free
                            ) );
    } else {
        data.read( "martial_arts_data", martial_arts_data );
    }

    JsonObject vits = data.get_object( "vitamin_levels" );
    vits.allow_omitted_members();
    for( const std::pair<const vitamin_id, vitamin> &v : vitamin::all() ) {
        if( vits.has_member( v.first.str() ) ) {
            int lvl = vits.get_int( v.first.str() );
            vitamin_levels[v.first] = clamp( lvl, v.first->min(), v.first->max() );
        }
    }
    data.read( "consumption_history", consumption_history );
    data.read( "activity", activity );
    data.read( "destination_activity", destination_activity );
    data.read( "stashed_outbounds_activity", stashed_outbounds_activity );
    data.read( "stashed_outbounds_backlog", stashed_outbounds_backlog );
    data.read( "backlog", backlog );
    if( !backlog.empty() && !backlog.front()->str_values.empty() && ( ( activity &&
            activity->id() == activity_id( "ACT_FETCH_REQUIRED" ) ) || ( destination_activity &&
                    destination_activity->id() == activity_id( "ACT_FETCH_REQUIRED" ) ) ) ) {
        requirement_data fetch_reqs;
        data.read( "fetch_data", fetch_reqs );
        const requirement_id req_id( backlog.front()->str_values.back() );
        requirement_data::save_requirement( fetch_reqs, req_id );
    }
    // npc activity on vehicles.
    data.read( "activity_vehicle_part_index", activity_vehicle_part_index );
    // health
    data.read( "healthy", healthy );
    data.read( "healthy_mod", healthy_mod );

    // @todo Remove after stable
    {
        std::array<int, num_bp> temp_cur_old, temp_conv_old, frostbite_timer_old;
        if( data.read( "temp_cur", temp_cur_old ) &&
            data.read( "temp_conv", temp_conv_old ) &&
            data.read( "frostbite_timer", frostbite_timer_old ) ) {
            // We can assume exactly num_bp body parts, since it's an old save
            for( size_t bp_iter = 0; bp_iter < num_bp; bp_iter++ ) {
                body_part bp_token = static_cast<body_part>( bp_iter );
                auto &part = get_part( convert_bp( bp_token ) );
                part.set_temp_cur( temp_cur_old[bp_iter] );
                part.set_temp_conv( temp_conv_old[bp_iter] );
                part.set_frostbite_timer( frostbite_timer_old[bp_iter] );
            }
        }
    }

    //energy
    data.read( "stim", stim );
    data.read( "stamina", stamina );

    data.read( "magic", magic );
    JsonArray parray;

    // Migration for old combined hair mutations (e.g., hair_black_crewcut -> hair_black + hair_crewcut)
    std::vector<trait_id> valid_hair_colors = get_mutations_in_type( "hair_color" );
    std::vector<trait_id> valid_hair_styles = get_mutations_in_type( "hair_style" );

    data.read( "traits", my_traits );
    for( auto it = my_traits.begin(); it != my_traits.end(); ) {
        const auto &tid = *it;
        if( tid.is_valid() ) {
            ++it;
        } else {
            bool silence = false;
            auto pid = it->str();
            if( pid.starts_with( "hair_" ) ) {
                int cnt = 0;
                for( auto c : pid.substr( 5 ) ) {
                    if( c == '_' ) { cnt++; }
                }
                silence = cnt >= 1;
            }
            if( !silence ) {
                debugmsg( "character %s has invalid trait %s, it will be ignored", name, pid );
            }
            my_traits.erase( it++ );
        }
    }

    data.read( "mutations", my_mutations );
    std::vector<trait_id> migrations_to_add;
    bool has_hair_bald = false;
    bool has_hair_color = false;
    for( auto it = my_mutations.begin(); it != my_mutations.end(); ) {
        const auto &mid = it->first;
        auto pid = mid.str();
        if( mid.is_valid() ) {
            if( mid.str() == "HAIR_BALD" ) {
                has_hair_bald = true;
            }
            for( const trait_id &color_trait : valid_hair_colors ) {
                if( mid == color_trait ) {
                    has_hair_color = true;
                    break;
                }
            }
            ++it;
        } else {
            std::string mid_str = mid.str();
            bool migrated = false;

            if( mid_str.starts_with( "hair_" ) ) {
                for( const trait_id &color_trait : valid_hair_colors ) {
                    std::string color_str = color_trait.str();
                    if( !color_str.starts_with( "hair_" ) ) {
                        color_str = "hair_" + color_str;
                    }
                    std::string prefix = color_str + "_";
                    if( mid_str.starts_with( prefix ) ) {
                        std::string style_suffix = mid_str.substr( prefix.length() );
                        for( const trait_id &style_trait : valid_hair_styles ) {
                            std::string style_str = style_trait.str();
                            if( style_str.starts_with( "hair_" ) ) {
                                style_str = style_str.substr( 5 );
                            }
                            if( style_suffix == style_str ) {
                                migrations_to_add.push_back( color_trait );
                                migrations_to_add.push_back( style_trait );
                                has_hair_color = true;
                                migrated = true;
                                break;
                            }
                        }
                        if( migrated ) {
                            break;
                        }
                    }
                }
            }

            if( !migrated ) {
                debugmsg( "character %s has invalid mutation %s, it will be ignored", name, mid.c_str() );
            }
            it = my_mutations.erase( it );
        }
    }

    // Iterate twice to avoid issues with subsequent calls on invalid mutations
    for( auto it = my_mutations.begin(); it != my_mutations.end(); ) {
        const auto &mid = it->first;
        on_mutation_gain( mid );
        cached_mutations.push_back( &mid.obj() );
        it++;
    }

    for( const trait_id &tid : migrations_to_add ) {
        if( !has_trait( tid ) ) {
            if( !has_base_trait( tid ) ) {
                toggle_trait( tid );
            } else {
                set_mutation( tid );
            }
        }
    }
    // Handle old HAIR_BALD saves: if character has HAIR_BALD but no hair color,
    // add a random hair color (in the new system, even bald characters have a hair color)
    if( has_hair_bald && !has_hair_color ) {
        trait_group::Trait_list random_colors = trait_group::traits_from(
                trait_group::Trait_group_tag( "Hair_Color_Any" ) );
        if( !random_colors.empty() ) {
            const trait_id &color_tid = random_colors.front();
            if( !has_trait( color_tid ) ) {
                set_mutation( color_tid );
            }
            if( !has_base_trait( color_tid ) ) {
                toggle_trait( color_tid );
            }
        }
    }
    newcharacter::add_default_mutation_type_traits( *this );
    recalculate_size();

    data.read( "my_bionics", *my_bionics );

    for( auto &w : worn ) {
        w->on_takeoff( *this );
    }
    worn.clear();
    data.read( "worn", worn );
    for( auto &w : worn ) {
        on_item_wear( *w );
    }

    if( data.has_array( "hp_cur" ) ) {
        set_anatomy( anatomy_id( "human_anatomy" ) );
        set_body();
        std::array<int, 6> hp_cur;
        data.read( "hp_cur", hp_cur );
        std::array<int, 6> hp_max;
        data.read( "hp_max", hp_max );
        set_part_hp_max( bodypart_id( "head" ), hp_max[0] );
        set_part_hp_cur( bodypart_id( "head" ), hp_cur[0] );

        set_part_hp_max( bodypart_id( "torso" ), hp_max[1] );
        set_part_hp_cur( bodypart_id( "torso" ), hp_cur[1] );

        set_part_hp_max( bodypart_id( "arm_l" ), hp_max[2] );
        set_part_hp_cur( bodypart_id( "arm_l" ), hp_cur[2] );

        set_part_hp_max( bodypart_id( "arm_r" ), hp_max[3] );
        set_part_hp_cur( bodypart_id( "arm_r" ), hp_cur[3] );

        set_part_hp_max( bodypart_id( "leg_l" ), hp_max[4] );
        set_part_hp_cur( bodypart_id( "leg_l" ), hp_cur[4] );

        set_part_hp_max( bodypart_id( "leg_r" ), hp_max[5] );
        set_part_hp_cur( bodypart_id( "leg_r" ), hp_cur[5] );
    }


    inv.clear();
    if( data.has_member( "inv" ) ) {
        JsonIn *invin = data.get_raw( "inv" );
        inv.json_load_items( *invin );
    }

    remove_primary_weapon( );
    detached_ptr<item> weap;
    data.read( "weapon", weap );
    set_primary_weapon( std::move( weap ) );

    data.read( "move_mode", move_mode );

    if( has_effect( effect_riding ) ) {
        int temp_id;
        if( data.read( "mounted_creature", temp_id ) ) {
            mounted_creature_id = temp_id;
            mounted_creature = g->critter_tracker->from_temporary_id( temp_id );
        } else {
            mounted_creature = nullptr;
        }
    }

    morale->load( data );
    // Have to go through effects again, in case an effect gained a morale bonus
    for( const auto &elem : *effects ) {
        for( const std::pair<const bodypart_str_id, effect> &_effect_it : elem.second ) {
            const effect &e = _effect_it.second;
            on_effect_int_change( e.get_id(), e.get_intensity(), e.get_bp() );
        }
    }

    _skills->clear();
    for( const JsonMember member : data.get_object( "skills" ) ) {
        member.read( ( *_skills )[skill_id( member.name() )] );
    }

    data.read( "learned_recipes", *learned_recipes );
    autolearn_skills_stamp->clear(); // Invalidates the cache

    on_stat_change( "thirst", thirst );
    on_stat_change( "stored_calories", stored_calories );
    on_stat_change( "fatigue", fatigue );
    on_stat_change( "sleep_deprivation", sleep_deprivation );
    on_stat_change( "pkill", pkill );
    on_stat_change( "perceived_pain", get_perceived_pain() );
    recalc_sight_limits();
    reset_encumbrance();

    assign( data, "power_level", power_level, false, 0_kJ );
    assign( data, "max_power_level", max_power_level, false, 0_kJ );

    // Bionic power should not be negative!
    if( power_level < 0_J ) {
        power_level = 0_J;
    }

    JsonArray overmap_time_array = data.get_array( "overmap_time" );
    overmap_time.clear();
    while( overmap_time_array.has_more() ) {
        point_abs_omt pt;
        overmap_time_array.read_next( pt );
        time_duration tdr = 0_turns;
        overmap_time_array.read_next( tdr );
        overmap_time[pt] = tdr;
    }
    data.read( "stomach", stomach );
    data.read( "automoveroute", auto_move_route );

    known_traps.clear();
    for( JsonObject pmap : data.get_array( "known_traps" ) ) {
        pmap.allow_omitted_members();
        const tripoint p( pmap.get_int( "x" ), pmap.get_int( "y" ), pmap.get_int( "z" ) );
        const std::string t = pmap.get_string( "trap" );
        known_traps.insert( trap_map::value_type( p, t ) );
    }
}

/**
 * Load variables from json into object. These variables are common to both the avatar and NPCs.
 */
void Character::store( JsonOut &json ) const
{
    Creature::store( json );

    // assumes already in Character object
    // positional data — stored as absolute map-square coordinates
    json.member( "abs_pos", position );

    // stat
    json.member( "str_cur", str_cur );
    json.member( "str_max", str_max );
    json.member( "dex_cur", dex_cur );
    json.member( "dex_max", dex_max );
    json.member( "int_cur", int_cur );
    json.member( "int_max", int_max );
    json.member( "per_cur", per_cur );
    json.member( "per_max", per_max );

    json.member( "str_bonus", str_bonus );
    json.member( "dex_bonus", dex_bonus );
    json.member( "per_bonus", per_bonus );
    json.member( "int_bonus", int_bonus );

    json.member( "name", name );

    json.member( "base_age", init_age );
    json.member( "base_height", init_height );

    if( prof.is_valid() ) {
    json.member( "profession", prof );
    }
    json.member( "custom_profession", custom_profession );

    // health
    json.member( "healthy", healthy );
    json.member( "healthy_mod", healthy_mod );

    // needs
    json.member( "thirst", thirst );
    json.member( "fatigue", fatigue );
    json.member( "sleep_deprivation", sleep_deprivation );
    json.member( "stored_calories", stored_calories );
    json.member( "radiation", radiation );
    json.member( "stamina", stamina );
    json.member( "vitamin_levels", vitamin_levels );
    json.member( "pkill", pkill );
    json.member( "omt_path", omt_path );
    json.member( "consumption_history", consumption_history );

    // crafting etc
    json.member( "destination_activity", destination_activity );
    json.member( "activity", activity );
    json.member( "stashed_outbounds_activity", stashed_outbounds_activity );
    json.member( "stashed_outbounds_backlog", stashed_outbounds_backlog );
    json.member( "backlog", backlog );
    json.member( "activity_vehicle_part_index", activity_vehicle_part_index ); // NPC activity

    // handling for storing activity requirements
    if( !backlog.empty() && !backlog.front()->str_values.empty() && ( ( activity &&
            activity->id() == activity_id( "ACT_FETCH_REQUIRED" ) ) || ( destination_activity &&
                    destination_activity->id() == activity_id( "ACT_FETCH_REQUIRED" ) ) ) ) {
    requirement_data things_to_fetch = requirement_id( backlog.front()->str_values.back() ).obj();
        json.member( "fetch_data", things_to_fetch );
    }

    const item &weapon = primary_weapon();
    if( !weapon.is_null() ) {
    json.member( "weapon", weapon ); // also saves contents
    }

    json.member( "stim", stim );
    json.member( "type_of_scent", type_of_scent );

    // breathing
    json.member( "oxygen", oxygen );

    // traits: permanent 'mutations' more or less
    json.member( "traits", my_traits );
    json.member( "mutations", my_mutations );
    json.member( "magic", magic );
    json.member( "martial_arts_data", martial_arts_data );
    // "Fracking Toasters" - Saul Tigh, toaster
    json.member( "my_bionics", *my_bionics );

    json.member_as_string( "move_mode",  move_mode );

    // storing the mount
    if( is_mounted() ) {
    json.member( "mounted_creature", g->critter_tracker->temporary_id( *mounted_creature ) );
    }

    morale->store( json );

    // skills
    json.member( "skills" );
    json.start_object();
for( const auto &pair : *_skills ) {
    json.member( pair.first.str(), pair.second );
    }
    json.end_object();

    // npc: unimplemented, potentially useful
    json.member( "learned_recipes", *learned_recipes );
    autolearn_skills_stamp->clear(); // Invalidates the cache

    // npc; unimplemented
    if( power_level < 1_kJ ) {
    json.member( "power_level", std::to_string( units::to_joule( power_level ) ) + " J" );
    } else {
        json.member( "power_level", std::to_string( units::to_kilojoule( power_level ) ) + " kJ" );
    }
    json.member( "max_power_level", std::to_string( units::to_kilojoule( max_power_level ) ) + " kJ" );

    if( !overmap_time.empty() ) {
    json.member( "overmap_time" );
        json.start_array();
        for( const std::pair<const point_abs_omt, time_duration> &pr : overmap_time ) {
            json.write( pr.first );
            json.write( pr.second );
        }
        json.end_array();
    }
    json.member( "stomach", stomach );
    json.member( "automoveroute", auto_move_route );
    json.member( "known_traps" );
    json.start_array();
for( const auto &elem : known_traps ) {
    json.start_object();
        json.member( "x", elem.first.x() );
        json.member( "y", elem.first.y() );
        json.member( "z", elem.first.z() );
        json.member( "trap", elem.second );
        json.end_object();
    }
    json.end_array();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///// player.h, avatar + npc

/**
 * Gather variables for saving. These variables are common to both the avatar and npcs.
 */
void player::store( JsonOut &json ) const
{
    Character::store( json );

    // energy
    json.member( "last_sleep_check", last_sleep_check );
    // misc levels
    json.member( "tank_plut", tank_plut );
    json.member( "reactor_plut", reactor_plut );
    json.member( "slow_rad", slow_rad );
    json.member( "scent", static_cast<int>( scent ) );

    // gender
    json.member( "male", male );

    json.member( "cash", cash );
    json.member( "recoil", recoil );
    json.member( "in_vehicle", in_vehicle );
    json.member( "id", getID() );

    // "Looks like I picked the wrong week to quit smoking." - Steve McCroskey
    json.member( "addictions", addictions );
    json.member( "followers", follower_ids );

    json.member( "worn", worn ); // also saves contents
    json.member( "inv" );
    inv.json_save_items( json );


    if( const auto lt_ptr = last_target.lock() ) {
    if( const npc *const guy = dynamic_cast<const npc *>( lt_ptr.get() ) ) {
            json.member( "last_target", guy->getID() );
            json.member( "last_target_type", +1 );
        } else if( const monster *const mon = dynamic_cast<const monster *>( lt_ptr.get() ) ) {
            // monsters don't have IDs, so get its index in the Creature_tracker instead
            json.member( "last_target", g->critter_tracker->temporary_id( *mon ) );
            json.member( "last_target_type", -1 );
        }
    } else {
        json.member( "last_target_pos", last_target_pos );
    }

    json.member( "destination_point", destination_point );
    json.member( "last_emote", last_emote );
    json.member( "ammo_location", ammo_location );
}

/**
 * Load variables from json into object. These variables are common to both the avatar and NPCs.
 */
void player::load( const JsonObject &data )
{
    Character::load( data );

    JsonArray parray;
    character_id tmpid;

    data.read( "tank_plut", tank_plut );
    data.read( "reactor_plut", reactor_plut );
    data.read( "slow_rad", slow_rad );
    data.read( "scent", scent );
    data.read( "male", male );
    data.read( "cash", cash );
    data.read( "recoil", recoil );
    data.read( "in_vehicle", in_vehicle );
    data.read( "last_sleep_check", last_sleep_check );
    if( data.read( "id", tmpid ) && tmpid.is_valid() ) {
        // Templates have invalid ids, so we only assign here when valid.
        // When the game starts, a new valid id will be assigned if not already
        // present.
        setID( tmpid );
    }

    data.read( "addictions", addictions );
    data.read( "followers", follower_ids );

    // Add the earplugs.
    if( has_bionic( bionic_id( "bio_ears" ) ) && !has_bionic( bionic_id( "bio_earplugs" ) ) ) {
        add_bionic( bionic_id( "bio_earplugs" ) );
    }

    // Add the blindfold.
    if( has_bionic( bionic_id( "bio_sunglasses" ) ) && !has_bionic( bionic_id( "bio_blindfold" ) ) ) {
        add_bionic( bionic_id( "bio_blindfold" ) );
    }

    // Fixes bugged characters for CBM's preventing mutations.
    for( const bionic &i : get_bionic_collection() ) {
        const bionic_id &bid = i.id;
        for( const trait_id &mid : bid->canceled_mutations ) {
            if( has_trait( mid ) ) {
                remove_mutation( mid );
            }
        }
    }

    int tmptar;
    int tmptartyp = 0;

    data.read( "last_target", tmptar );
    data.read( "last_target_type", tmptartyp );
    data.read( "last_target_pos", last_target_pos );
    data.read( "ammo_location", ammo_location );
    if( tmptartyp == +1 ) {
        // Use the game's current dimension — set before character deserialization begins.
        last_target = get_overmapbuffer( g->get_current_dimension_id() ).find_npc( character_id( tmptar ) );
    } else if( tmptartyp == -1 ) {
        // Need to do this *after* the monsters have been loaded!
        last_target = g->critter_tracker->from_temporary_id( tmptar );
    }
    data.read( "destination_point", destination_point );
    data.read( "last_emote", last_emote );
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///// avatar.h

void avatar::serialize( JsonOut &json ) const
{
    json.start_object();

    store( json );

    json.end_object();
}

void avatar::store( JsonOut &json ) const
{
    player::store( json );

    if( g->scen != nullptr ) {
    json.member( "scenario", g->scen->ident() );
    }
    // someday, npcs may drive
    json.member( "controlling_vehicle", controlling_vehicle );

    // shopping carts, furniture etc
    json.member( "grab_point", grab_point );
    json.member( "grab_type", obj_type_name[static_cast<int>( grab_type ) ] );

    // misc player specific stuff
    json.member( "focus_pool", focus_pool );

    // bio_portal_tap persistent link
    if( bio_portal_tap_linked ) {
    json.member( "bio_portal_tap_linked", bio_portal_tap_linked );
        json.member( "bio_portal_tap_dim_id", bio_portal_tap_dim_id );
        json.member( "bio_portal_tap_pos", bio_portal_tap_pos.raw() );
    }

    if( shadow_npc ) {
    json.member( "shadow_npc", *shadow_npc );
    }

    // stats through kills
    json.member( "str_upgrade", std::abs( str_upgrade ) );
    json.member( "dex_upgrade", std::abs( dex_upgrade ) );
    json.member( "int_upgrade", std::abs( int_upgrade ) );
    json.member( "per_upgrade", std::abs( per_upgrade ) );

    // Player only, books they have read at least once.
    json.member( "items_identified", items_identified );

    json.member( "translocators", translocators );

    // mission stuff
    json.member( "active_mission", active_mission == nullptr ? -1 : active_mission->get_id() );

    json.member( "active_missions", mission::to_uid_vector( active_missions ) );
    json.member( "completed_missions", mission::to_uid_vector( completed_missions ) );
    json.member( "failed_missions", mission::to_uid_vector( failed_missions ) );

    json.member( "show_map_memory", show_map_memory );

    json.member( "assigned_invlet" );
    json.start_array();
    for( auto iter : inv.assigned_invlet ) {
    json.start_array();
        json.write( iter.first );
        json.write( iter.second );
        json.end_array();
    }
    json.end_array();

    json.member( "invcache" );
    inv.json_save_invcache( json );

    json.member( "preferred_aiming_mode", preferred_aiming_mode );

    json.member( "snippets_read", snippets_read );

    json.member( "known_monsters", known_monsters );

    json.member( "faction_warnings" );
    json.start_array();
for( const auto &elem : warning_record ) {
    json.start_object();
        json.member( "fac_warning_id", elem.first );
        json.member( "fac_warning_num", elem.second.first );
        json.member( "fac_warning_time", elem.second.second );
        json.end_object();
    }
    json.end_array();

    // Throw quick-slots
    json.member( "throw_slots" );
    json.start_array();
for( const auto &slot : throw_slots_ ) {
    json.write( slot.str() );
    }
    json.end_array();
    json.member( "active_throw_slot", active_throw_slot_ );
}

void avatar::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    load( data );
}

void avatar::load( const JsonObject &data )
{
    player::load( data );

    data.read( "controlling_vehicle", controlling_vehicle );

    data.read( "grab_point", grab_point );
    std::string grab_typestr = "OBJECT_NONE";
    if( grab_point.x() != 0 || grab_point.y() != 0 ) {
        grab_typestr = "OBJECT_VEHICLE";
        data.read( "grab_type", grab_typestr );
    }
    const auto iter = std::find( obj_type_name.begin(), obj_type_name.end(), grab_typestr );
    grab( iter == obj_type_name.end() ?
          OBJECT_NONE : static_cast<object_type>( std::distance( obj_type_name.begin(), iter ) ),
          grab_point );

    data.read( "focus_pool", focus_pool );

    // bio_portal_tap persistent link
    if( data.has_member( "bio_portal_tap_linked" ) ) {
        data.read( "bio_portal_tap_linked", bio_portal_tap_linked );
        data.read( "bio_portal_tap_dim_id", bio_portal_tap_dim_id );
        tripoint raw;
        data.read( "bio_portal_tap_pos", raw );
        bio_portal_tap_pos = tripoint_abs_ms( raw );
    }

    if( data.has_member( "shadow_npc" ) ) {
        shadow_npc = std::make_unique<npc>();
        data.read( "shadow_npc", *shadow_npc );
    }

    // stats through kills
    data.read( "str_upgrade", str_upgrade );
    data.read( "dex_upgrade", dex_upgrade );
    data.read( "int_upgrade", int_upgrade );
    data.read( "per_upgrade", per_upgrade );

    // this is so we don't need to call get_option in a draw function
    if( !get_option<bool>( "STATS_THROUGH_KILLS" ) )         {
        str_upgrade = -str_upgrade;
        dex_upgrade = -dex_upgrade;
        int_upgrade = -int_upgrade;
        per_upgrade = -per_upgrade;
    }

    data.read( "magic", magic );

    set_highest_cat_level();
    drench_mut_calc();
    std::string scen_ident = "(null)";
    if( data.read( "scenario", scen_ident ) && string_id<scenario>( scen_ident ).is_valid() ) {
        g->scen = &string_id<scenario>( scen_ident ).obj();

        if( !g->scen->allowed_start( start_location ) ) {
            start_location = g->scen->random_start_location();
        }
    } else {
        const scenario *generic_scenario = scenario::generic();
        // Only display error message if from a game file after scenarios existed.
        if( savegame_loading_version > 20 ) {
            debugmsg( "Tried to use non-existent scenario '%s'. Setting to generic '%s'.",
                      scen_ident.c_str(), generic_scenario->ident().c_str() );
        }
        g->scen = generic_scenario;
    }

    items_identified.clear();
    data.read( "items_identified", items_identified );

    // Player only, snippets they have read at least once.
    data.read( "snippets_read", snippets_read );

    data.read( "translocators", translocators );

    std::vector<int> tmpmissions;
    if( data.read( "active_missions", tmpmissions ) ) {
        active_missions = mission::to_ptr_vector( tmpmissions );
    }
    if( data.read( "failed_missions", tmpmissions ) ) {
        failed_missions = mission::to_ptr_vector( tmpmissions );
    }
    if( data.read( "completed_missions", tmpmissions ) ) {
        completed_missions = mission::to_ptr_vector( tmpmissions );
    }

    int tmpactive_mission = 0;
    if( data.read( "active_mission", tmpactive_mission ) ) {
        if( savegame_loading_version <= 23 ) {
            // In 0.C, active_mission was an index of the active_missions array (-1 indicated no active mission).
            // And it would as often as not be out of bounds (e.g. when a questgiver died).
            // Later, it became a mission * and stored as the mission's uid, and this change broke backward compatibility.
            // Unfortunately, nothing can be done about savegames between the bump to version 24 and 83808a941.
            if( tmpactive_mission >= 0 && tmpactive_mission < static_cast<int>( active_missions.size() ) ) {
                active_mission = active_missions[tmpactive_mission];
            } else if( !active_missions.empty() ) {
                active_mission = active_missions.back();
            }
        } else if( tmpactive_mission != -1 ) {
            active_mission = mission::find( tmpactive_mission );
        }
    }

    // Normally there is only one player character loaded, so if a mission that is assigned to
    // another character (not the current one) fails, the other character(s) are not informed.
    // We must inform them when they get loaded the next time.
    // Only active missions need checking, failed/complete will not change anymore.
    const auto last = std::remove_if( active_missions.begin(),
    active_missions.end(), []( mission const * m ) {
        return m->has_failed();
    } );
    std::copy( last, active_missions.end(), std::back_inserter( failed_missions ) );
    active_missions.erase( last, active_missions.end() );
    if( active_mission && active_mission->has_failed() ) {
        if( active_missions.empty() ) {
            active_mission = nullptr;
        } else {
            active_mission = active_missions.front();
        }
    }

    if( savegame_loading_version <= 23 && is_player() ) {
        // In 0.C there was no player_id member of mission, so it'll be the default -1.
        // When the member was introduced, no steps were taken to ensure compatibility with 0.C, so
        // missions will be buggy for saves between experimental commits bd2088c033 and dd83800.
        // see npc_chatbin::check_missions and npc::talk_to_u
        for( mission *miss : active_missions ) {
            miss->set_player_id_legacy_0c( getID() );
        }
    }

    data.read( "show_map_memory", show_map_memory );

    for( JsonArray pair : data.get_array( "assigned_invlet" ) ) {
        inv.assigned_invlet[static_cast<char>( pair.get_int( 0 ) )] =
            itype_id( pair.get_string( 1 ) );
    }

    if( data.has_member( "invcache" ) ) {
        JsonIn *jip = data.get_raw( "invcache" );
        inv.json_load_invcache( *jip );
    }

    data.read( "preferred_aiming_mode", preferred_aiming_mode );

    if( data.has_array( "faction_warnings" ) ) {
        for( JsonObject warning_data : data.get_array( "faction_warnings" ) ) {
            warning_data.allow_omitted_members();
            std::string fac_id = warning_data.get_string( "fac_warning_id" );
            int warning_num = warning_data.get_int( "fac_warning_num" );
            time_point warning_time = calendar::before_time_starts;
            warning_data.read( "fac_warning_time", warning_time );
            warning_record[faction_id( fac_id )] = std::make_pair( warning_num, warning_time );
        }
    }

    // monsters recorded by the character
    data.read( "known_monsters", known_monsters );

    // Throw quick-slots
    if( data.has_array( "throw_slots" ) ) {
        int idx = 0;
        for( const std::string &s : data.get_array( "throw_slots" ) ) {
            if( idx < MAX_THROW_SLOTS ) {
                throw_slots_[idx++] = s.empty() ? itype_id{} :
                                      itype_id( s );
            }
        }
    }
    data.read( "active_throw_slot", active_throw_slot_ );
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///// npc.h

void npc_follower_rules::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "engagement", static_cast<int>( engagement ) );
    json.member( "aim", static_cast<int>( aim ) );
    json.member( "cbm_reserve", static_cast<int>( cbm_reserve ) );
    json.member( "cbm_recharge", static_cast<int>( cbm_recharge ) );

    // serialize the flags so they can be changed between save games
for( const auto &rule : ally_rule_strs ) {
    json.member( "rule_" + rule.first, has_flag( rule.second.rule, false ) );
    }
for( const auto &rule : ally_rule_strs ) {
    json.member( "override_enable_" + rule.first, has_override_enable( rule.second.rule ) );
    }
for( const auto &rule : ally_rule_strs ) {
    json.member( "override_" + rule.first, has_override( rule.second.rule ) );
    }

    json.member( "pickup_whitelist", *pickup_whitelist );

    json.end_object();
}

void npc_follower_rules::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    int tmpeng = 0;
    data.read( "engagement", tmpeng );
    engagement = static_cast<combat_engagement>( tmpeng );
    int tmpaim = 0;
    data.read( "aim", tmpaim );
    aim = static_cast<aim_rule>( tmpaim );
    int tmpreserve = 50;
    data.read( "cbm_reserve", tmpreserve );
    cbm_reserve = static_cast<cbm_reserve_rule>( tmpreserve );
    int tmprecharge = 50;
    data.read( "cbm_recharge", tmprecharge );
    cbm_recharge = static_cast<cbm_recharge_rule>( tmprecharge );

    // deserialize the flags so they can be changed between save games
    for( const auto &rule : ally_rule_strs ) {
        bool tmpflag = false;
        // legacy to handle rules that were saved before overrides
        data.read( rule.first, tmpflag );
        if( tmpflag ) {
            set_flag( rule.second.rule );
        } else {
            clear_flag( rule.second.rule );
        }
        data.read( "rule_" + rule.first, tmpflag );
        if( tmpflag ) {
            set_flag( rule.second.rule );
        } else {
            clear_flag( rule.second.rule );
        }
        data.read( "override_enable_" + rule.first, tmpflag );
        if( tmpflag ) {
            enable_override( rule.second.rule );
        } else {
            disable_override( rule.second.rule );
        }
        data.read( "override_" + rule.first, tmpflag );
        if( tmpflag ) {
            set_override( rule.second.rule );
        } else {
            clear_override( rule.second.rule );
        }

        // This and the following two entries are for legacy save game handling.
        // "avoid_combat" was renamed "follow_close" to better reflect behavior.
        if( data.has_member( "rule_avoid_combat" ) ) {
            data.read( "rule_avoid_combat", tmpflag );
            if( tmpflag ) {
                set_flag( ally_rule::follow_close );
            } else {
                clear_flag( ally_rule::follow_close );
            }
        }
        if( data.has_member( "override_enable_avoid_combat" ) ) {
            data.read( "override_enable_avoid_combat", tmpflag );
            if( tmpflag ) {
                enable_override( ally_rule::follow_close );
            } else {
                disable_override( ally_rule::follow_close );
            }
        }
        if( data.has_member( "override_avoid_combat" ) ) {
            data.read( "override_avoid_combat", tmpflag );
            if( tmpflag ) {
                set_override( ally_rule::follow_close );
            } else {
                clear_override( ally_rule::follow_close );
            }
        }
    }

    data.read( "pickup_whitelist", *pickup_whitelist );
}

void npc_chatbin::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "first_topic", first_topic );
    if( mission_selected != nullptr ) {
    json.member( "mission_selected", mission_selected->get_id() );
    }
    json.member( "skill", skill );
    json.member( "style", style );
    json.member( "missions", mission::to_uid_vector( missions ) );
    json.member( "missions_assigned", mission::to_uid_vector( missions_assigned ) );
    json.end_object();
}

void npc_chatbin::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();

    if( data.has_int( "first_topic" ) ) {
        int tmptopic = 0;
        data.read( "first_topic", tmptopic );
        first_topic = convert_talk_topic( static_cast<talk_topic_enum>( tmptopic ) );
    } else {
        data.read( "first_topic", first_topic );
    }

    data.read( "skill", skill );
    data.read( "style", style );

    std::vector<int> tmpmissions;
    data.read( "missions", tmpmissions );
    missions = mission::to_ptr_vector( tmpmissions );
    std::vector<int> tmpmissions_assigned;
    data.read( "missions_assigned", tmpmissions_assigned );
    missions_assigned = mission::to_ptr_vector( tmpmissions_assigned );

    int tmpmission_selected = 0;
    mission_selected = nullptr;
    if( data.read( "mission_selected", tmpmission_selected ) && tmpmission_selected != -1 ) {
        if( savegame_loading_version <= 23 ) {
            // In 0.C, it was an index into the missions_assigned vector
            if( tmpmission_selected >= 0 &&
                tmpmission_selected < static_cast<int>( missions_assigned.size() ) ) {
                mission_selected = missions_assigned[tmpmission_selected];
            }
        } else {
            mission_selected = mission::find( tmpmission_selected );
        }
    }
}

void npc_personality::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    int tmpagg = 0;
    int tmpbrav = 0;
    int tmpcol = 0;
    int tmpalt = 0;
    if( data.read( "aggression", tmpagg ) &&
        data.read( "bravery", tmpbrav ) &&
        data.read( "collector", tmpcol ) &&
        data.read( "altruism", tmpalt ) ) {
        aggression = static_cast<signed char>( tmpagg );
        bravery = static_cast<signed char>( tmpbrav );
        collector = static_cast<signed char>( tmpcol );
        altruism = static_cast<signed char>( tmpalt );
    } else {
        debugmsg( "npc_personality: bad data" );
    }
}

void npc_personality::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "aggression", static_cast<int>( aggression ) );
    json.member( "bravery", static_cast<int>( bravery ) );
    json.member( "collector", static_cast<int>( collector ) );
    json.member( "altruism", static_cast<int>( altruism ) );
    json.end_object();
}

void npc_opinion::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    data.read( "trust", trust );
    data.read( "fear", fear );
    data.read( "value", value );
    data.read( "anger", anger );
    data.read( "owed", owed );
}

void npc_opinion::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "trust", trust );
    json.member( "fear", fear );
    json.member( "value", value );
    json.member( "anger", anger );
    json.member( "owed", owed );
    json.end_object();
}

void npc_favor::deserialize( JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    type = static_cast<npc_favor_type>( jo.get_int( "type" ) );
    jo.read( "value", value );
    jo.read( "itype_id", item_id );
    if( jo.has_int( "skill_id" ) ) {
        skill = Skill::from_legacy_int( jo.get_int( "skill_id" ) );
    } else if( jo.has_string( "skill_id" ) ) {
        skill = skill_id( jo.get_string( "skill_id" ) );
    } else {
        skill = skill_id::NULL_ID();
    }
}

void npc_favor::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "type", static_cast<int>( type ) );
    json.member( "value", value );
    json.member( "itype_id", static_cast<std::string>( item_id ) );
    json.member( "skill_id", skill );
    json.end_object();
}

/*
 * load npc
 */
void npc::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    load( data );
}

void npc::load( const JsonObject &data )
{
    player::load( data );

    int misstmp = 0;
    int classtmp = 0;
    int atttmp = 0;
    std::string facID;
    std::string comp_miss_id;
    std::string comp_miss_role;
    tripoint_abs_omt comp_miss_pt;
    std::string classid;
    std::string companion_mission_role;
    time_point companion_mission_t = calendar::start_of_cataclysm;
    time_point companion_mission_t_r = calendar::start_of_cataclysm;
    std::string act_id;

    data.read( "marked_for_death", marked_for_death );
    data.read( "dead", dead );
    data.read( "patience", patience );
    if( data.has_number( "myclass" ) ) {
        data.read( "myclass", classtmp );
        myclass = npc_class::from_legacy_int( classtmp );
    } else if( data.has_string( "myclass" ) ) {
        data.read( "myclass", classid );
        myclass = npc_class_id( classid );
    }
    data.read( "known_to_u", known_to_u );
    data.read( "personality", personality );
    if( !data.has_member( "abs_pos" ) ) {
        // Legacy NPC saves stored submap_coords + separate z-level.
        auto offset = project_remain<coords::sm>( bub_pos().reinterpret_as<tripoint_rel_ms>() );

        if( !data.read( "mapz", offset.remainder_tripoint.z() ) ) {
            data.read( "omz", offset.remainder_tripoint.z() );
        }

        point_abs_sm sm_coords;
        if( !data.read( "submap_coords", sm_coords ) ) {
            // Oldest format: submap coords encoded as omx/omy/mapx/mapy
            point_abs_sm old_coords;
            data.read( "mapx", old_coords.x() );
            data.read( "mapy", old_coords.y() );
            int o = 0;
            if( data.read( "omx", o ) ) {
                old_coords.x() += o * OMAPX * 2;
            }
            if( data.read( "omy", o ) ) {
                old_coords.y() += o * OMAPY * 2;
            }
            sm_coords = old_coords + offset.quotient;
        }

        position = project_combine( sm_coords, offset.remainder_tripoint );
    }

    if( data.has_member( "plx" ) ) {
        last_player_seen_pos.emplace();
        data.read( "plx", last_player_seen_pos->x() );
        data.read( "ply", last_player_seen_pos->y() );
        if( !data.read( "plz", last_player_seen_pos->z() ) ) {
            last_player_seen_pos->z() = bub_pos().z();
        }
        // old code used tripoint_min to indicate "not a valid point"
        if( *last_player_seen_pos == tripoint_bub_ms::zero() ) {
            last_player_seen_pos.reset();
        }
    } else {
        data.read( "last_player_seen_pos", last_player_seen_pos );
    }

    data.read( "goalx", goal.x() );
    data.read( "goaly", goal.y() );
    data.read( "goalz", goal.z() );

    data.read( "guardx", guard_pos.x() );
    data.read( "guardy", guard_pos.y() );
    data.read( "guardz", guard_pos.z() );
    if( data.read( "current_activity_id", act_id ) ) {
        current_activity_id = activity_id( act_id );
    } else if( activity ) {
        current_activity_id = activity->id();
    }

    if( data.has_member( "pulp_locationx" ) ) {
        pulp_location.emplace();
        data.read( "pulp_locationx", pulp_location->x() );
        data.read( "pulp_locationy", pulp_location->y() );
        data.read( "pulp_locationz", pulp_location->z() );
        // old code used tripoint_min to indicate "not a valid point"
        if( *pulp_location == tripoint_bub_ms::zero() ) {
            pulp_location.reset();
        }
    } else {
        data.read( "pulp_location", pulp_location );
    }
    data.read( "chair_pos", chair_pos );
    data.read( "wander_pos", wander_pos );
    if( data.read( "mission", misstmp ) ) {
        mission = static_cast<npc_mission>( misstmp );
        static const std::set<npc_mission> legacy_missions = {{
                NPC_MISSION_LEGACY_1, NPC_MISSION_LEGACY_2,
                NPC_MISSION_LEGACY_3
            }
        };
        if( legacy_missions.contains( mission ) ) {
            mission = NPC_MISSION_NULL;
        }
    }
    if( data.read( "previous_mission", misstmp ) ) {
        previous_mission = static_cast<npc_mission>( misstmp );
        static const std::set<npc_mission> legacy_missions = {{
                NPC_MISSION_LEGACY_1, NPC_MISSION_LEGACY_2,
                NPC_MISSION_LEGACY_3
            }
        };
        if( legacy_missions.contains( mission ) ) {
            previous_mission = NPC_MISSION_NULL;
        }
    }

    if( data.read( "my_fac", facID ) ) {
        fac_id = faction_id( facID );
    }
    int temp_fac_api_ver = 0;
    if( data.read( "faction_api_ver", temp_fac_api_ver ) ) {
        faction_api_version = temp_fac_api_ver;
    } else {
        faction_api_version = 0;
    }

    if( data.read( "attitude", atttmp ) ) {
        attitude = static_cast<npc_attitude>( atttmp );
        static const std::set<npc_attitude> legacy_attitudes = {{
                NPCATT_LEGACY_1, NPCATT_LEGACY_2, NPCATT_LEGACY_3,
                NPCATT_LEGACY_4, NPCATT_LEGACY_5, NPCATT_LEGACY_6
            }
        };
        if( legacy_attitudes.contains( attitude ) ) {
            attitude = NPCATT_NULL;
        }
    }
    if( data.read( "previous_attitude", atttmp ) ) {
        previous_attitude = static_cast<npc_attitude>( atttmp );
        static const std::set<npc_attitude> legacy_attitudes = {{
                NPCATT_LEGACY_1, NPCATT_LEGACY_2, NPCATT_LEGACY_3,
                NPCATT_LEGACY_4, NPCATT_LEGACY_5, NPCATT_LEGACY_6
            }
        };
        if( legacy_attitudes.contains( attitude ) ) {
            previous_attitude = NPCATT_NULL;
        }
    }

    if( data.read( "comp_mission_id", comp_miss_id ) ) {
        comp_mission.mission_id = comp_miss_id;
    }

    if( data.read( "comp_mission_pt", comp_miss_pt ) ) {
        comp_mission.position = comp_miss_pt;
    }

    if( data.read( "comp_mission_role", comp_miss_role ) ) {
        comp_mission.role_id = comp_miss_role;
    }

    if( data.read( "companion_mission_role_id", companion_mission_role ) ) {
        companion_mission_role_id = companion_mission_role;
    }

    std::vector<tripoint_abs_omt> companion_mission_pts;
    data.read( "companion_mission_points", companion_mission_pts );
    if( !companion_mission_pts.empty() ) {
        for( auto pt : companion_mission_pts ) {
            companion_mission_points.push_back( pt );
        }
    }

    if( !data.read( "companion_mission_time", companion_mission_t ) ) {
        companion_mission_time = calendar::before_time_starts;
    } else {
        companion_mission_time = companion_mission_t;
    }

    if( !data.read( "companion_mission_time_ret", companion_mission_t_r ) ) {
        companion_mission_time_ret = calendar::before_time_starts;
    } else {
        companion_mission_time_ret = companion_mission_t_r;
    }

    companion_mission_inv.clear();
    if( data.has_member( "companion_mission_inv" ) ) {
        JsonIn *invin_mission = data.get_raw( "companion_mission_inv" );
        companion_mission_inv.json_load_items( *invin_mission );
    }

    if( !data.read( "restock", restock ) ) {
        restock = calendar::before_time_starts;
    }

    data.read( "op_of_u", op_of_u );
    data.read( "chatbin", chatbin );
    if( !data.read( "rules", rules ) ) {
        data.read( "misc_rules", rules );
        data.read( "combat_rules", rules );
    }
    cbm_toggled = bionic_id::NULL_ID();
    data.read( "cbm_toggled", cbm_toggled );
    data.read( "cbm_fake_toggled", cbm_fake_toggled );
    if( cbm_toggled && !cbm_fake_toggled ) {
        cbm_fake_toggled = item::spawn( cbm_toggled->fake_item );
    }
    if( data.has_member( "cbm_weapon_index" ) ) {
        int index = 0;
        data.read( "cbm_weapon_index", index );
        if( index >= 0 ) {
            cbm_toggled = ( *my_bionics )[ index ].id;
            cbm_fake_toggled = item::spawn( cbm_toggled->fake_item );
        }
    }
    cbm_active = bionic_id::NULL_ID();
    data.read( "cbm_active", cbm_active );
    data.read( "cbm_fake_active", cbm_fake_active );

    if( !data.read( "last_updated", last_updated ) ) {
        last_updated = calendar::turn;
    }
    data.read( "dimension_id", dimension_id_ );
    complaints.clear();
    data.read( "complaints", complaints );
}

/*
 * save npc
 */
void npc::serialize( JsonOut &json ) const
{
    json.start_object();
    // This must be after the json object has been started, so any super class
    // puts their data into the same json object.
    store( json );
    json.end_object();
}

void npc::store( JsonOut &json ) const
{
    player::store( json );

    json.member( "marked_for_death", marked_for_death );
    json.member( "dead", dead );
    json.member( "patience", patience );
    json.member( "myclass", myclass.str() );
    json.member( "known_to_u", known_to_u );
    json.member( "personality", personality );

    json.member( "last_player_seen_pos", last_player_seen_pos );

    json.member( "goalx", goal.x() );
    json.member( "goaly", goal.y() );
    json.member( "goalz", goal.z() );

    json.member( "guardx", guard_pos.x() );
    json.member( "guardy", guard_pos.y() );
    json.member( "guardz", guard_pos.z() );
    json.member( "current_activity_id", current_activity_id.str() );
    json.member( "pulp_location", pulp_location );
    json.member( "chair_pos", chair_pos );
    json.member( "wander_pos", wander_pos );
    // TODO: stringid
    json.member( "mission", mission );
    json.member( "previous_mission", previous_mission );
    json.member( "faction_api_ver", faction_api_version );
    if( !fac_id.str().empty() ) { // set in constructor
    json.member( "my_fac", fac_id.c_str() );
    }
    json.member( "attitude", static_cast<int>( attitude ) );
    json.member( "previous_attitude", static_cast<int>( previous_attitude ) );
    json.member( "op_of_u", op_of_u );
    json.member( "chatbin", chatbin );
    json.member( "rules", rules );

    json.member( "cbm_toggled", cbm_toggled );
    if( cbm_fake_toggled ) {
    json.member( "cbm_fake_toggled", cbm_fake_toggled );
    }
    json.member( "cbm_active", cbm_active );
    if( cbm_fake_active ) {
    json.member( "cbm_fake_active", cbm_fake_active );
    }

    json.member( "comp_mission_id", comp_mission.mission_id );
    json.member( "comp_mission_pt", comp_mission.position );
    json.member( "comp_mission_role", comp_mission.role_id );
    json.member( "companion_mission_role_id", companion_mission_role_id );
    json.member( "companion_mission_points", companion_mission_points );
    json.member( "companion_mission_time", companion_mission_time );
    json.member( "companion_mission_time_ret", companion_mission_time_ret );
    json.member( "companion_mission_inv" );
    companion_mission_inv.json_save_items( json );
    json.member( "restock", restock );

    json.member( "last_updated", last_updated );
    if( !dimension_id_.empty() ) {
    json.member( "dimension_id", dimension_id_ );
    }
    json.member( "complaints", complaints );
}
