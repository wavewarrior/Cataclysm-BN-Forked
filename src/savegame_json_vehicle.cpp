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

static const itype_id itype_battery( "battery" );

namespace to_cbc_migration
{
void migrate( std::vector<detached_ptr<item>> &stack );
} // namespace to_cbc_migration
/*
 * vehicle_part
 */
void vehicle_part::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    vpart_id pid;
    data.read( "id", pid );

    std::map<std::string, std::pair<std::string, std::string>> deprecated = {
        { "laser_gun", { "laser_rifle", "none" } },
        { "seat_nocargo", { "seat", "none" } },
        { "engine_plasma", { "minireactor", "none" } },
        { "battery_truck", { "battery_car", "battery" } },

        { "diesel_tank_little", { "tank_little", "diesel" } },
        { "diesel_tank_small", { "tank_small", "diesel" } },
        { "diesel_tank_medium", { "tank_medium", "diesel" } },
        { "diesel_tank", { "tank", "diesel" } },
        { "external_diesel_tank_small", { "external_tank_small", "diesel" } },
        { "external_diesel_tank", { "external_tank", "diesel" } },

        { "gas_tank_little", { "tank_little", "gasoline" } },
        { "gas_tank_small", { "tank_small", "gasoline" } },
        { "gas_tank_medium", { "tank_medium", "gasoline" } },
        { "gas_tank", { "tank", "gasoline" } },
        { "external_gas_tank_small", { "external_tank_small", "gasoline" } },
        { "external_gas_tank", { "external_tank", "gasoline" } },

        { "water_dirty_tank_little", { "tank_little", "water" } },
        { "water_dirty_tank_small", { "tank_small", "water" } },
        { "water_dirty_tank_medium", { "tank_medium", "water" } },
        { "water_dirty_tank", { "tank", "water" } },
        { "external_water_dirty_tank_small", { "external_tank_small", "water" } },
        { "external_water_dirty_tank", { "external_tank", "water" } },
        { "dirty_water_tank_barrel", { "tank_barrel", "water" } },

        { "water_tank_little", { "tank_little", "water_clean" } },
        { "water_tank_small", { "tank_small", "water_clean" } },
        { "water_tank_medium", { "tank_medium", "water_clean" } },
        { "water_tank", { "tank", "water_clean" } },
        { "external_water_tank_small", { "external_tank_small", "water_clean" } },
        { "external_water_tank", { "external_tank", "water_clean" } },
        { "water_tank_barrel", { "tank_barrel", "water_clean" } },

        { "napalm_tank", { "tank", "napalm" } },

        { "hydrogen_tank", { "tank", "none" } }
    };

    // required for compatibility with 0.C saves
    itype_id legacy_fuel;

    auto dep = deprecated.find( pid.str() );
    if( dep != deprecated.end() ) {
        pid = vpart_id( dep->second.first );
        legacy_fuel = itype_id( dep->second.second );
    }

    // if we don't know what type of part it is, it'll cause problems later.
    if( !pid.is_valid() ) {
        if( pid.str() == "wheel_underbody" ) {
            pid = vpart_id( "wheel_wide" );
        } else {
            data.throw_error( "bad vehicle part", "id" );
        }
    }
    id = pid;

    if( data.has_object( "base" ) ) {
        data.read( "base", base );
    } else {
        // handle legacy format which didn't include the base item
        set_base( item::spawn( id.obj().item ) );
    }

    data.read( "mount_dx", mount.x() );
    data.read( "mount_dy", mount.y() );
    data.read( "open", open );
    int direction_int;
    data.read( "direction", direction_int );
    direction = units::from_degrees( direction_int );
    data.read( "blood", blood );
    data.read( "proxy_part_id", proxy_part_id );
    data.read( "proxy_sym", proxy_sym );
    data.read( "enabled", enabled );
    data.read( "flags", flags );
    data.read( "passenger_id", passenger_id );
    if( data.has_int( "z_offset" ) ) {
        int z_offset = data.get_int( "z_offset" );
        if( std::abs( z_offset ) > 10 ) {
            data.throw_error( "z_offset out of range", "z_offset" );
        }
        z_terrain[0] = z_offset;
        z_terrain[1] = z_offset;
    }
    JsonArray ja = data.get_array( "carry" );
    size_t sz = ja.size();
    for( size_t index = 0; index < sz; index++ ) {
        carry_names.push( ja.get_string( sz - index - 1 ) );
    }
    data.read( "crew_id", crew_id );
    data.read( "items", items );
    data.read( "target_first_x", target.first.x() );
    data.read( "target_first_y", target.first.y() );
    data.read( "target_first_z", target.first.z() );
    data.read( "target_second_x", target.second.x() );
    data.read( "target_second_y", target.second.y() );
    data.read( "target_second_z", target.second.z() );
    data.read( "ammo_pref", ammo_pref );
    data.read( "part_color", part_color_ );
    if( data.has_member( "portal_tap_linked" ) ) {
        data.read( "portal_tap_linked", portal_tap_linked );
        data.read( "portal_tap_dim_id", portal_tap_dim_id );
        tripoint raw;
        data.read( "portal_tap_pos", raw );
        portal_tap_pos = tripoint_abs_ms( raw );
    }

    if( legacy_fuel.is_empty() ) {
        legacy_fuel = id.obj().fuel_type;
    }

    // with VEHICLE tag migrate fuel tanks only if amount field exists
    if( base->has_flag( flag_id( "VEHICLE" ) ) ) {
        if( data.has_int( "amount" ) && ammo_capacity() > 0 && legacy_fuel != itype_battery ) {
            ammo_set( legacy_fuel, data.get_int( "amount" ) );
        }

        // without VEHICLE flag always migrate both batteries and fuel tanks
    } else {
        if( ammo_capacity() > 0 ) {
            ammo_set( legacy_fuel, data.get_int( "amount" ) );
        }
        base->set_flag( flag_id( "VEHICLE" ) );
    }

    if( data.has_int( "hp" ) && id.obj().durability > 0 ) {
        // migrate legacy savegames exploiting that all base items at that time had max_damage() of 4
        base->set_damage( 4 * itype::damage_scale - 4 * itype::damage_scale * data.get_int( "hp" ) /
                          id.obj().durability );
    }

    // legacy turrets loaded ammo via a pseudo CARGO space
    if( is_turret() && !items.empty() ) {
        const int qty = std::accumulate( items.begin(), items.end(), 0, []( int lhs,
        const item * const & rhs ) {
            return lhs + rhs->charges;
        } );
        ammo_set( ( *items.begin() )->ammo_current(), qty );
        items.clear();
    }
}

void vehicle_part::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "id", id.str() );
    json.member( "base", base );
    json.member( "mount_dx", mount.x() );
    json.member( "mount_dy", mount.y() );
    json.member( "open", open );
    json.member( "direction", std::lround( to_degrees( direction ) ) );
    json.member( "blood", blood );
    json.member( "proxy_part_id", proxy_part_id );
    json.member( "proxy_sym", proxy_sym );
    json.member( "enabled", enabled );
    json.member( "flags", flags );
    if( !carry_names.empty() ) {
    std::stack<std::string, std::vector<std::string> > carry_copy = carry_names;
    json.member( "carry" );
        json.start_array();
        while( !carry_copy.empty() ) {
            json.write( carry_copy.top() );
            carry_copy.pop();
        }
        json.end_array();
    }
    json.member( "passenger_id", passenger_id );
    json.member( "crew_id", crew_id );
    if( z_terrain[0] ) {
    json.member( "z_offset", z_terrain[0] );
    }
    json.member( "items", items );
    if( target.first != tripoint_abs_ms::min() ) {
    json.member( "target_first_x", target.first.x() );
        json.member( "target_first_y", target.first.y() );
        json.member( "target_first_z", target.first.z() );
    }
    if( target.second != tripoint_abs_ms::min() ) {
    json.member( "target_second_x", target.second.x() );
        json.member( "target_second_y", target.second.y() );
        json.member( "target_second_z", target.second.z() );
    }
    json.member( "ammo_pref", ammo_pref );
    json.member( "part_color", part_color_ );
    if( portal_tap_linked ) {
    json.member( "portal_tap_linked", portal_tap_linked );
        json.member( "portal_tap_dim_id", portal_tap_dim_id );
        json.member( "portal_tap_pos", portal_tap_pos.raw() );
    }
    json.end_object();
}

/*
 * label
 */
void label::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();
    data.read( "x", x() );
    data.read( "y", y() );
    data.read( "z", z() );
    data.read( "text", text );
}

void label::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "x", x() );
    json.member( "y", y() );
    json.member( "z", z() );
    json.member( "text", text );
    json.end_object();
}

namespace
{

/// Vehicle pivots used to be 2D mount-space points before tripoint migration.
auto read_legacy_vehicle_pivot( const JsonObject &data, tripoint_mnt_veh &pivot ) -> void
{
    if( !data.has_member( "pivot" ) ) {
    return;
}

const auto pivot_json = data.get_array( "pivot" );
if( pivot_json.size() != 2 && pivot_json.size() != 3 ) {
    data.throw_error( "vehicle pivot must have 2 or 3 coordinates", "pivot" );
    }

    const auto z = pivot_json.size() == 3 ? pivot_json.get_int( 2 ) : 0;
    pivot = tripoint_mnt_veh( pivot_json.get_int( 0 ), pivot_json.get_int( 1 ), z );
}

auto read_saved_vehicle_parts( const JsonObject &data, std::vector<vehicle_part> &parts ) -> void
{
    if( !data.has_array( "parts" ) ) {
    return;
}

parts.clear();
const auto part_array = data.get_array( "parts" );
for( auto part_index = size_t{ 0 }; part_index < part_array.size(); ++part_index ) {
    auto part = vehicle_part();
        try {
            part_array.read( part_index, part, true );
            parts.push_back( std::move( part ) );
        } catch( const JsonError &err ) {
            DebugLog( DL::Warn, DC::DebugMsg ) << "Skipping invalid saved vehicle part: " << err.c_str();
        }
    }
}

} // namespace

/*
 * Load vehicle from a json blob that might just exceed player in size.
 */
void vehicle::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    data.allow_omitted_members();

    int fdir = 0;
    int mdir = 0;

    data.read( "type", type );
    data.read( "posx", sm_ms_pos.x() );
    data.read( "posy", sm_ms_pos.y() );
    data.read( "om_id", om_id );
    data.read( "faceDir", fdir );
    data.read( "moveDir", mdir );
    int turn_dir_int;
    data.read( "turn_dir", turn_dir_int );
    turn_dir = units::from_degrees( turn_dir_int );
    int loaded_velocity = 0;
    int loaded_cruise_velocity = 0;
    int loaded_vertical_velocity = 0;
    data.read( "velocity", loaded_velocity );
    data.read( "falling", is_falling );
    data.read( "floating", is_floating );
    data.read( "flying", is_flying );
    data.read( "cruise_velocity", loaded_cruise_velocity );
    data.read( "vertical_velocity", loaded_vertical_velocity );
    if( savegame_loading_version < 29 ) {
        velocity = std::lround( static_cast<double>( loaded_velocity ) * 0.44704 );
        cruise_velocity = std::lround( static_cast<double>( loaded_cruise_velocity ) * 0.44704 );
        vertical_velocity = std::lround( static_cast<double>( loaded_vertical_velocity ) * 0.44704 );
    } else {
        velocity = loaded_velocity;
        cruise_velocity = loaded_cruise_velocity;
        vertical_velocity = loaded_vertical_velocity;
    }
    data.read( "cruise_on", cruise_on );
    data.read( "engine_on", engine_on );
    data.read( "brake_hold", brake_hold );
    data.read( "tracking_on", tracking_on );
    data.read( "skidding", skidding );
    data.read( "of_turn_carry", of_turn_carry );
    data.read( "angular_velocity_rads", angular_velocity_rads );
    data.read( "physics_pos_x", physics_pos.x );
    data.read( "physics_pos_y", physics_pos.y );
    data.read( "physics_angle", physics_angle );
    data.read( "is_locked", is_locked );
    data.read( "is_alarm_on", is_alarm_on );
    data.read( "camera_on", camera_on );
    if( !data.read( "last_update_turn", last_update ) ) {
        last_update = calendar::turn;
    }

    units::angle fdir_angle = units::from_degrees( fdir );
    face.init( fdir_angle );
    move.init( units::from_degrees( mdir ) );
    data.read( "name", name );
    std::string temp_id;
    std::string temp_old_id;
    data.read( "owner", temp_id );
    data.read( "old_owner", temp_old_id );
    // for savegames before the change to faction_id for ownership.
    if( temp_id.empty() ) {
        owner = faction_id::NULL_ID();
    } else {
        owner = faction_id( temp_id );
    }
    if( temp_old_id.empty() ) {
        old_owner = faction_id::NULL_ID();
    } else {
        old_owner = faction_id( temp_old_id );
    }
    data.read( "theft_time", theft_time );
    data.read( "dimension_id", dimension_id_ );

    // we persist the pivot anchor so that if the rules for finding
    // the pivot change, existing vehicles do not shift around.
    // Loading vehicles that predate the pivot logic is a special
    // case of this, they will load with an anchor of (0,0) which
    // is what they're expecting.
    read_legacy_vehicle_pivot( data, pivot_anchor[0] );
    pivot_anchor[1] = pivot_anchor[0];
    pivot_rotation[1] = pivot_rotation[0] = fdir_angle;

    read_saved_vehicle_parts( data, parts );
    data.read( "is_following", is_following );
    data.read( "follow_distance", follow_distance );
    data.read( "is_patrolling", is_patrolling );
    data.read( "autodrive_local_target", autodrive_local_target );
    data.read( "min_autodrive_speed", min_autodrive_speed );
    data.read( "max_autodrive_speed", max_autodrive_speed );
    data.read( "summon_time_limit", summon_time_limit );
    data.read( "magic", magic );

    // Loose items -> count-by-charges migration
    for( vehicle_part &part : parts ) {
        //TODO!: this is awful
        std::vector<detached_ptr<item>> clears = part.clear_items();

        to_cbc_migration::migrate( clears );
        part.set_vehicle_hack( this );
        for( detached_ptr<item> &it :  clears ) {
            part.add_item( std::move( it ) );
        }
    }

    // Need to manually backfill the active item cache since the part loader can't call its vehicle.
    for( const vpart_reference &vp : get_any_parts( VPFLAG_CARGO ) ) {
        auto it = vp.part().items.begin();
        auto end = vp.part().items.end();
        for( ; it != end; ++it ) {
            if( ( *it )->needs_processing() ) {
                active_items.add( **it );
            }
        }
    }

    for( const vpart_reference &vp : get_any_parts( "TURRET" ) ) {
        install_part( vp.mount(), vpart_id( "turret_mount" ), false );

        //Forcibly set turrets' targeting mode to manual if no turret control unit is present on turret's tile on loading save
        if( !has_part( bub_part_location( vp.part() ), "TURRET_CONTROLS" ) ) {
            vp.part().enabled = false;
        }
        //Set turret control unit's state equal to turret's targeting mode on loading save
        for( const vpart_reference &turret_part : get_any_parts( "TURRET_CONTROLS" ) ) {
            turret_part.part().enabled = vp.part().enabled;
        }
    }

    // Add vehicle mounts to cars that are missing them.
    for( const vpart_reference &vp : get_any_parts( "NEEDS_WHEEL_MOUNT_LIGHT" ) ) {
        if( vp.info().has_flag( "STEERABLE" ) ) {
            install_part( vp.mount(), vpart_id( "wheel_mount_light_steerable" ), false );
        } else {
            install_part( vp.mount(), vpart_id( "wheel_mount_light" ), false );
        }
    }
    for( const vpart_reference &vp : get_any_parts( "NEEDS_WHEEL_MOUNT_MEDIUM" ) ) {
        if( vp.info().has_flag( "STEERABLE" ) ) {
            install_part( vp.mount(), vpart_id( "wheel_mount_medium_steerable" ), false );
        } else {
            install_part( vp.mount(), vpart_id( "wheel_mount_medium" ), false );
        }
    }
    for( const vpart_reference &vp : get_any_parts( "NEEDS_WHEEL_MOUNT_HEAVY" ) ) {
        if( vp.info().has_flag( "STEERABLE" ) ) {
            install_part( vp.mount(), vpart_id( "wheel_mount_heavy_steerable" ), false );
        } else {
            install_part( vp.mount(), vpart_id( "wheel_mount_heavy" ), false );
        }
    }

    /* After loading, check if the vehicle is from the old rules and is missing
     * frames. */
    if( savegame_loading_version < 11 ) {
        add_missing_frames();
    }

    // Handle steering changes
    if( savegame_loading_version < 25 ) {
        add_steerable_wheels();
    }

    refresh();

    data.read( "tags", tags );
    data.read( "labels", labels );

    tripoint_mnt_veh p;
    zone_data zd;
    for( JsonObject sdata : data.get_array( "zones" ) ) {
        sdata.allow_omitted_members();
        sdata.read( "point", p );
        sdata.read( "zone", zd );
        loot_zones.emplace( p, zd );
    }
    data.read( "other_tow_point", tow_data.other_towing_point );
    // Note that it's possible for a vehicle to be loaded midway
    // through a turn if the player is driving REALLY fast and their
    // own vehicle motion takes them in range. An undefined value for
    // on_turn caused occasional weirdness if the undefined value
    // happened to be positive.
    //
    // Setting it to zero means it won't get to move until the start
    // of the next turn, which is what happens anyway if it gets
    // loaded anywhere but midway through a driving cycle.
    //
    // Something similar to vehicle::gain_moves() would be ideal, but
    // that can't be used as it currently stands because it would also
    // make it instantly fire all its turrets upon load.
    of_turn = 0;

    /** Legacy saved games did not store part enabled status within parts */
    const auto set_legacy_state = [&]( const std::string & var, const std::string & flag ) {
        if( data.get_bool( var, false ) ) {
            for( const vpart_reference &vp : get_any_parts( flag ) ) {
                vp.part().enabled = true;
            }
        }
    };

    set_legacy_state( "stereo_on", "STEREO" );
    set_legacy_state( "chimes_on", "CHIMES" );
    set_legacy_state( "fridge_on", "FRIDGE" );
    set_legacy_state( "reaper_on", "REAPER" );
    set_legacy_state( "planter_on", "PLANTER" );
    set_legacy_state( "recharger_on", "RECHARGE" );
    set_legacy_state( "scoop_on", "SCOOP" );
    set_legacy_state( "plow_on", "PLOW" );
    set_legacy_state( "reactor_on", "REACTOR" );
}

void vehicle::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "type", type );
    json.member( "posx", sm_ms_pos.x() );
    json.member( "posy", sm_ms_pos.y() );
    json.member( "om_id", om_id );
    json.member( "faceDir", std::lround( to_degrees( face.dir() ) ) );
    json.member( "moveDir", std::lround( to_degrees( move.dir() ) ) );
    json.member( "turn_dir", std::lround( to_degrees( turn_dir ) ) );
    json.member( "velocity", velocity );
    json.member( "falling", is_falling );
    json.member( "floating", is_floating );
    json.member( "flying", is_flying );
    json.member( "cruise_velocity", cruise_velocity );
    json.member( "vertical_velocity", vertical_velocity );
    json.member( "cruise_on", cruise_on );
    json.member( "engine_on", engine_on );
    json.member( "brake_hold", brake_hold );
    json.member( "tracking_on", tracking_on );
    json.member( "skidding", skidding );
    json.member( "of_turn_carry", of_turn_carry );
    json.member( "angular_velocity_rads", angular_velocity_rads );
    json.member( "physics_pos_x", physics_pos.x );
    json.member( "physics_pos_y", physics_pos.y );
    json.member( "physics_angle", physics_angle );
    json.member( "name", name );
    json.member( "owner", owner );
    json.member( "old_owner", old_owner );
    json.member( "theft_time", theft_time );
    json.member( "parts", parts );
    json.member( "tags", tags );
    json.member( "labels", labels );
    json.member( "zones" );
    json.start_array();
for( auto const &z : loot_zones ) {
    json.start_object();
        json.member( "point", z.first );
        json.member( "zone", z.second );
        json.end_object();
    }
    json.end_array();
    tripoint_bub_ms other_tow_temp_point;
    if( is_towed() ) {
    vehicle *tower = tow_data.get_towed_by();
        if( tower ) {
            other_tow_temp_point = tower->bub_part_location( tower->get_tow_part() );
        }
    }
    json.member( "other_tow_point", other_tow_temp_point );

    json.member( "is_locked", is_locked );
    json.member( "is_alarm_on", is_alarm_on );
    json.member( "camera_on", camera_on );
    json.member( "last_update_turn", last_update );
    if( !dimension_id_.empty() ) {
    json.member( "dimension_id", dimension_id_ );
    }
    json.member( "pivot", pivot_anchor[0] );
    json.member( "is_following", is_following );
    json.member( "follow_distance", follow_distance );
    json.member( "is_patrolling", is_patrolling );
    json.member( "autodrive_local_target", autodrive_local_target );
    json.member( "min_autodrive_speed", min_autodrive_speed );
    json.member( "max_autodrive_speed", max_autodrive_speed );
    json.member( "summon_time_limit", summon_time_limit );
    json.member( "magic", magic );
    json.end_object();
}

