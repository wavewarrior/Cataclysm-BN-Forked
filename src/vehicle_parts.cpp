#include "vehicle.h"
#include "detached_ptr.h"
#include "units_mass.h"
#include "vehicle_part.h" // IWYU pragma: associated
#include "vpart_position.h" // IWYU pragma: associated
#include "vpart_range.h" // IWYU pragma: associated

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <list>
#include <memory>
#include <numeric>
#include <queue>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <ranges>

#include "active_tile_data_def.h"
#include "avatar.h"
#include "avatar_functions.h"
#include "bionics.h"
#include "cata_utility.h"
#include "character.h"
#include "clzones.h"
#include "coordinates.h"
#include "creature.h"
#include "cuboid_rectangle.h"
#include "debug.h"
#include "distribution_grid.h"
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "explosion.h"
#include "faction.h"
#include "field_type.h"
#include "flag.h"
#include "game.h"
#include "item.h"
#include "item_category.h"
#include "item_contents.h"
#include "item_group.h"
#include "itype.h"
#include "json.h"
#include "make_static.h"
#include "map.h"
#include "map_iterator.h"
#include "mapbuffer.h"
#include "mapdata.h"
#include "messages.h"
#include "monster.h"
#include "npc.h"
#include "options.h"
#include "output.h"
#include "overmapbuffer.h"
#include "player.h"
#include "player_activity.h"
#include "point_float.h"
#include "rng.h"
#include "sounds.h"
#include "string_formatter.h"
#include "string_id.h"
#include "string_input_popup.h"
#include "submap.h"
#include "translations.h"
#include "units_utility.h"
#include "veh_type.h"
#include "vehicle_palette.h"
#include "vehicle_functions.h"
#include "weather.h"
#include "ui.h"
/*
 * Speed up all those if ( blarg == "structure" ) statements that are used everywhere;
 *   assemble "structure" once here instead of repeatedly later.
 */
static const std::string part_location_structure( "structure" );
static const std::string part_location_under( "under" );
static const std::string part_location_center( "center" );
static const std::string part_location_onroof( "on_roof" );

static const itype_id fuel_type_animal( "animal" );
static const itype_id fuel_type_battery( "battery" );
static const itype_id fuel_type_muscle( "muscle" );
static const itype_id fuel_type_plutonium_cell( "plut_cell" );
static const itype_id fuel_type_wind( "wind" );

static const fault_id fault_belt( "fault_engine_belt_drive" );
static const fault_id fault_filter_air( "fault_engine_filter_air" );
static const fault_id fault_filter_fuel( "fault_engine_filter_fuel" );
static const fault_id fault_immobiliser( "fault_engine_immobiliser" );

static const activity_id ACT_VEHICLE( "ACT_VEHICLE" );

static const bionic_id bio_jointservo( "bio_jointservo" );

static const efftype_id effect_harnessed( "harnessed" );

static const itype_id itype_battery( "battery" );
static const itype_id itype_plut_cell( "plut_cell" );
static const itype_id itype_water( "water" );
static const itype_id itype_water_clean( "water_clean" );
static const itype_id itype_water_purifier( "water_purifier" );
static const vpart_id vp_door_lock( "door_lock" );

static const flag_id f_VEHICLE_UNLOCKED( "VEHICLE_UNLOCKED" );
static const flag_id f_VEHICLE_LOCKED( "VEHICLE_LOCKED" );
static const flag_id f_VEHICLE_NO_LOCKS( "VEHICLE_NO_LOCKS" );

static const flag_id f_VEHICLE_NO_HOTWIRE( "VEHICLE_NO_HOTWIRE" );
static const flag_id f_VEHICLE_HOTWIRE( "VEHICLE_HOTWIRE" );

static const std::string str_DOOR_LOCKING( "DOOR_LOCKING" );
static const std::string str_OPENCLOSE_INSIDE( "OPENCLOSE_INSIDE" );
static const std::string str_PERPETUAL( "PERPETUAL" );

static const std::vector<std::string> vs_NO_HOTWIRING = {
    "MUSCLE_LEGS",
    "MUSCLE_ARMS",
    "ANIMAL_CTRL",
};

static bool is_sm_tile_outside( const tripoint_abs_ms &pos );
static bool is_sm_tile_over_water( const tripoint_abs_ms &pos );

static const itype_id fuel_type_mana( "mana" );

// 1 kJ per battery charge
const int bat_energy_j = 1000;

#include "vehicle_part_handler.h"

// Vehicle stack methods.

#include "vehicle_part_handler.h"

bool vehicle::can_mount( const tripoint_mnt_veh &dp, const vpart_id &id ) const
{
    // The part has to actually exist.
    if( !id.is_valid() ) {
    return false;
}

// It also has to be a real part, not the null part
const vpart_info &part = id.obj();
if( part.has_flag( "NOINSTALL" ) ) {
    return false;
}

const std::vector<int> parts_in_square = parts_at_relative( dp, false );

// New Subpath for balloon type structures when on the edge
if( part.has_flag( "EXTENDABLE" ) ) {
    if( parts_in_square.empty() ) {
            // There needs to be parts for these
            if( !parts.empty() ) {
                if( !has_structural_or_extendable_part( dp ) &&
                    !has_structural_or_extendable_part( dp + point_east ) &&
                    !has_structural_or_extendable_part( dp + point_south ) &&
                    !has_structural_or_extendable_part( dp + point_west ) &&
                    !has_structural_or_extendable_part( dp + point_north ) ) {
                    return false;
                }
                return true;
            }
            // If there are no parts, we go on our merry day
        } else {
            return false;
        }
    }
    // First part in an empty square MUST be a structural part
    if( parts_in_square.empty() && part.location != part_location_structure ) {
    return false;
}
// If its a part that harnesses animals that don't allow placing on it.
if( !parts_in_square.empty() && part_info( parts_in_square[0] ).has_flag( "ANIMAL_CTRL" ) ) {
        return false;
    }
    // No other part can be placed on a protrusion
    if( !parts_in_square.empty() && part_info( parts_in_square[0] ).has_flag( "PROTRUSION" ) ) {
        return false;
    }
    // NOCOLLIDE parts can not have other parts on the same tile
    if( !parts_in_square.empty() && part_info( parts_in_square[0] ).has_flag( "NOCOLLIDE" ) ) {
        return false;
    }
    // EXTENDABLE parts can not have other parts on the same tile
    // Todo: let there be an exception for mount points when added
    // Like turret mount points
    if( !parts_in_square.empty() && part_info( parts_in_square[0] ).has_flag( "EXTENDABLE" ) ) {
        return false;
    }

    //No part type can stack with itself, or any other part in the same slot
for( const auto &elem : parts_in_square ) {
    const vpart_info &other_part = parts[elem].info();

        //Parts with no location can stack with each other (but not themselves)
        if( part.get_id() == other_part.get_id() ||
            ( !part.location.empty() && part.location == other_part.location ) ) {
            return false;
        }
        // Until we have an interface for handling multiple components with CARGO space,
        // exclude them from being mounted in the same tile.
        if( part.has_flag( "CARGO" ) && other_part.has_flag( "CARGO" ) ) {
            return false;
        }

    }

    // All parts after the first must be installed on or next to an existing part
    // the exception is when a single tile only structural object is being repaired
    if( !parts.empty() ) {
    if( !is_structural_part_removed() &&
            !has_structural_part( dp ) &&
            !has_structural_part( dp + point_east ) &&
            !has_structural_part( dp + point_south ) &&
            !has_structural_part( dp + point_west ) &&
            !has_structural_part( dp + point_north ) ) {
            return false;
        }
    }

    // only one exclusive engine allowed
    std::string empty;
    if( has_engine_conflict( &part, empty ) ) {
    return false;
}

// Check all the flags of the part to see if they require other flags
// If other flags are required check if those flags are present
for( const std::string &flag : part.get_flags() ) {
    if( !json_flag::get( flag ).requires_flag().empty() ) {
            bool anchor_found = false;
            for( const auto &elem : parts_in_square ) {
                if( part_info( elem ).has_flag( json_flag::get( flag ).requires_flag() ) ) {
                    anchor_found = true;
                }
            }
            if( !anchor_found ) {
                return false;
            }
        }
    }

    //Mirrors cannot be mounted on OPAQUE parts
    if( part.has_flag( "VISION" ) && !part.has_flag( "CAMERA" ) ) {
    for( const auto &elem : parts_in_square ) {
            if( part_info( elem ).has_flag( "OPAQUE" ) ) {
                return false;
            }
        }
    }
    //Opaque parts cannot be mounted on mirrors parts
    if( part.has_flag( "OPAQUE" ) ) {
    for( const auto &elem : parts_in_square ) {
            if( part_info( elem ).has_flag( "VISION" ) &&
                !part_info( elem ).has_flag( "CAMERA" ) ) {
                return false;
            }
        }
    }

    //Turret mounts must NOT be installed on other (modded) turret mounts
    if( part.has_flag( "TURRET_MOUNT" ) ) {
    for( const auto &elem : parts_in_square ) {
            if( part_info( elem ).has_flag( "TURRET_MOUNT" ) ) {
                return false;
            }
        }
    }

    //Don't allow turret controls or laser designators on manual-only turrets.
    if( part.has_flag( "TURRET_CONTROLS" ) || part.has_flag( "LASER_DESIGNATOR" ) ) {
    for( const auto &elem : parts_in_square ) {
            if( part_info( elem ).has_flag( "MANUAL" ) ) {
                return false;
            }
        }
    }

    //Anything not explicitly denied is permitted
    return true;
}

bool vehicle::can_unmount( const int p ) const
{
    std::string no_reason;
    return can_unmount( p, no_reason );
}

bool vehicle::can_unmount( const int p, std::string &reason ) const
{
    if( p < 0 || p > static_cast<int>( parts.size() ) ) {
    return false;
}

// Find all the flags on parts in this tile that require other flags
const auto pt = parts[p].mount;
std::vector<int> parts_here = parts_at_relative( pt, false );

if( part_info( p ).has_flag( "NOREMOVE_SECURITY" ) ) {
    const auto [c, s] = get_controls_and_security();
        if( s >= 0 ) {
            reason = string_format( _( "Remove the %1$s %2$s first." ), name, part_info( s ).name() );
            return false;
        }
    }

    const auto no_remove_closed = part_info( p ).has_flag( "NOREMOVE_CLOSED" );
    const auto no_remove_open =  part_info( p ).has_flag( "NOREMOVE_OPEN" );
for( const auto &elem : parts_here ) {
    const auto is_openable = part_info( elem ).has_flag( "OPENABLE" );
        const auto is_working = !parts[elem].is_broken();
        if( no_remove_closed && is_openable && is_working && !parts[elem].open ) {
            reason = string_format( _( "Open the attached %s first." ), part_info( elem ).name() );
            return false;
        }
        if( no_remove_open && is_openable && is_working && parts[elem].open ) {
            reason = string_format( _( "Close the attached %s first." ), part_info( elem ).name() );
            return false;
        }

        if( !is_working ) {
            continue;
        }

        for( const std::string &flag : part_info( elem ).get_flags() ) {
            const auto require_flag = json_flag::get( flag ).requires_flag();
            if( !require_flag.empty() && part_info( p ).has_flag( require_flag ) ) {
                reason = string_format( _( "Remove the attached %s first." ), part_info( elem ).name() );
                return false;
            }
        }
    }

    //Can't remove an animal part if the animal is still contained
    if( parts[p].has_flag( vehicle_part::animal_flag ) ) {
    reason = _( "Remove carried animal first." );
        return false;
    }

    //Structural parts have extra requirements
    if( part_info( p ).location == part_location_structure ||
        part_info( p ).has_flag( VPFLAG_EXTENDABLE ) ) {

    std::vector<int> parts_in_square = parts_at_relative( parts[p].mount, false );
        /* To remove a structural part, there can be only structural parts left
         * in that square (might be more than one in the case of wreckage) */
        for( auto &elem : parts_in_square ) {
            if( part_info( elem ).location != part_location_structure &&
                !part_info( elem ).has_flag( VPFLAG_EXTENDABLE ) ) {
                reason = _( "Remove all other attached parts first." );
                return false;
            }
        }

        //If it's the last part in the square...
        if( parts_in_square.size() == 1 ) {

            /* This is the tricky part: We can't remove a part that would cause
             * the vehicle to 'break into two' (like removing the middle section
             * of a quad bike, for instance). This basically requires doing some
             * breadth-first searches to ensure previously connected parts are
             * still connected. */

            //First, find all the squares connected to the one we're removing
            std::vector<const vehicle_part *> connected_parts;

            for( int i = 0; i < 4; i++ ) {
                const auto next = parts[p].mount + point_rel_veh( i < 2 ? ( i == 0 ? -1 : 1 ) : 0,
                                  i < 2 ? 0 : ( i == 2 ? -1 : 1 ) );
                std::vector<int> parts_over_there = parts_at_relative( next, false );
                //Ignore empty squares
                if( !parts_over_there.empty() ) {
                    //Just need one part from the square to track the x/y
                    connected_parts.push_back( &parts[parts_over_there[0]] );
                }
            }

            /* If size = 0, it's the last part of the whole vehicle, so we're OK
             * If size = 1, it's one protruding part (i.e., bicycle wheel), so OK
             * Otherwise, it gets complicated... */
            if( connected_parts.size() > 1 ) {

                /* We'll take connected_parts[0] to be the target part.
                 * Every other part must have some path (that doesn't involve
                 * the part about to be removed) to the target part, in order
                 * for the part to be legally removable. */
                for( const auto &next_part : connected_parts ) {
                    if( !is_connected( *connected_parts[0], *next_part, parts[p] ) ) {
                        //Removing that part would break the vehicle in two
                        reason = _( "Removing this part would split the vehicle." );
                        return false;
                    }
                }

            }

        }
    }
    //Anything not explicitly denied is permitted
    return true;
}

bool vehicle::is_connected( const vehicle_part &to, const vehicle_part &from,
                            const vehicle_part &excluded_part ) const
{
    const auto target = to.mount;
    const auto excluded = excluded_part.mount;

    //Breadth-first-search components
    std::list<const vehicle_part *> discovered;
    std::list<const vehicle_part *> searched;

    //We begin with just the start point
    discovered.push_back( &from );

    while( !discovered.empty() ) {
        const vehicle_part &current_part = *discovered.front();
        discovered.pop_front();
        auto current = current_part.mount;

        for( point offset : four_adjacent_offsets ) {
            auto next = current + offset;

            if( next == target ) {
                //Success!
                return true;
            } else if( next == excluded ) {
                //There might be a path, but we're not allowed to go that way
                continue;
            }

            std::vector<int> parts_there = parts_at_relative( next, true );

            if( !parts_there.empty() && !parts[ parts_there[ 0 ] ].removed &&
                ( part_info( parts_there[ 0 ] ).location == "structure" ||
                  part_info( parts_there[ 0 ] ).has_flag( VPFLAG_EXTENDABLE ) ) &&
                !part_info( parts_there[ 0 ] ).has_flag( "PROTRUSION" ) ) {
                //Only add the part if we haven't been here before
                bool found = false;
                for( auto &elem : discovered ) {
                    if( elem->mount == next ) {
                        found = true;
                        break;
                    }
                }
                if( !found ) {
                    for( auto &elem : searched ) {
                        if( elem->mount == next ) {
                            found = true;
                            break;
                        }
                    }
                }
                if( !found ) {
                    const vehicle_part &next_part = parts[parts_there[0]];
                    discovered.push_back( &next_part );
                }
            }
        }
        //Now that that's done, we've finished exploring here
        searched.push_back( &current_part );
    }
    //If we completely exhaust the discovered list, there's no path
    return false;
}

int vehicle::install_part( const tripoint_mnt_veh &dp, const vpart_id &id, bool force )
{
    if( !( force || can_mount( dp, id ) ) ) {
        return -1;
    }
    detached_ptr<item> obj = item::spawn( id.obj().item );
    int ret = install_part( dp, vehicle_part( id, dp, std::move( obj ), this ) );
    return ret;
}

int vehicle::install_part( const tripoint_mnt_veh &dp, const vpart_id &id, detached_ptr<item> &&obj,
                           bool force )
{
    if( !( force || can_mount( dp, id ) ) ) {
        return -1;
    }

    int ret = install_part( dp, vehicle_part( id, dp, std::move( obj ), this ) );
    return ret;
}

int vehicle::install_part( const tripoint_mnt_veh &dp, vehicle_part &&new_part )
{
    // Should be checked before installing the part
    bool enable = false;
    if( new_part.is_engine() ) {
        enable = true;
    } else {
        // TODO: read toggle groups from JSON
        static const std::vector<std::string> enable_like = {{
                "CONE_LIGHT",
                "CIRCLE_LIGHT",
                "AISLE_LIGHT",
                "AUTOPILOT",
                "DOME_LIGHT",
                "ATOMIC_LIGHT",
                "STEREO",
                "CHIMES",
                "FRIDGE",
                "FREEZER",
                "RECHARGE",
                "PLOW",
                "REAPER",
                "PLANTER",
                "SCOOP",
                "SPACE_HEATER",
                "COOLER",
                "WATER_PURIFIER",
                "ROCKWHEEL",
                "ROADHEAD",
                "CONVERTER"
            }
        };

        for( const std::string &flag : enable_like ) {
            if( new_part.info().has_flag( flag ) ) {
                enable = has_part( flag, true );
                break;
            }
        }
    }

    parts.push_back( std::move( new_part ) );
    if( !no_refresh ) {
        refresh_locations_hack();
    }
    auto &pt = parts.back();
    pt.set_vehicle_hack( this );

    pt.enabled = enable;

    pt.mount = dp;

    if( pt.base->has_var( TINT_COLOR_VAR_NAME ) || pt.base->has_var( TINT_COLOR_FG_VAR_NAME ) ||
        pt.base->has_var( TINT_COLOR_BG_VAR_NAME ) ) {
        const auto c = pt.base->get_var<RGBColor>( TINT_COLOR_VAR_NAME, {} );
        const auto bg = pt.base->get_var<RGBColor>( TINT_COLOR_FG_VAR_NAME, c );
        const auto fg = pt.base->get_var<RGBColor>( TINT_COLOR_BG_VAR_NAME, c );
        pt.part_color_ = {.bg = bg, .fg = fg };
    }

    refresh();
    get_map().invalidate_lightmap_caches();
    coeff_air_changed = true;
    return parts.size() - 1;
}

bool vehicle::try_to_rack_nearby_vehicle( const std::vector<std::vector<int>> &list_of_racks )
{
    for( const auto &this_bike_rack : list_of_racks ) {
        std::vector<vehicle *> carry_vehs;
        carry_vehs.assign( 4, nullptr );
        vehicle *test_veh = nullptr;
        std::set<tripoint_abs_ms> veh_partial_match;
        std::vector<std::set<tripoint_abs_ms>> partial_matches;
        partial_matches.assign( 4, veh_partial_match );
        for( auto rack_part : this_bike_rack ) {
            auto rack_pos = g->m.bub_to_abs( bub_part_location( rack_part ) );
            int i = 0;
            for( point offset : four_cardinal_directions ) {
                auto search_pos = rack_pos + offset;
                test_veh = veh_pointer_or_null( g->m.veh_at( search_pos ) );
                if( test_veh == nullptr || test_veh == this ) {
                    continue;
                } else if( test_veh != carry_vehs[ i ] ) {
                    carry_vehs[ i ] = test_veh;
                    partial_matches[ i ].clear();
                }
                partial_matches[ i ].insert( search_pos );
                if( partial_matches[ i ] == test_veh->get_points() ) {
                    return merge_rackable_vehicle( test_veh, this_bike_rack );
                }
                ++i;
            }
        }
    }
    return false;
}

bool vehicle::merge_rackable_vehicle( vehicle *carry_veh, const std::vector<int> &rack_parts )
{
    // Mapping between the old vehicle and new vehicle mounting points
    struct mapping {
        // All the parts attached to this mounting point
        std::vector<int> carry_parts_here;

        // the index where the racking part is on the vehicle with the rack
        int rack_part;

        // the mount point we are going to add to the vehicle with the rack
        tripoint_mnt_veh carry_mount;

        // the mount point on the old vehicle (carry_veh) that will be destroyed
        tripoint_mnt_veh old_mount;
    };
    invalidate_towing( true );
    // By structs, we mean all the parts of the carry vehicle that are at the structure location
    // of the vehicle (i.e. frames)
    std::vector<int> carry_veh_structs = carry_veh->all_parts_at_location( part_location_structure );
    std::vector<mapping> carry_data;
    carry_data.reserve( carry_veh_structs.size() );

    //X is forward/backward, Y is left/right
    std::string axis;
    if( carry_veh_structs.size() == 1 ) {
        axis = "X";
    } else {
        for( auto carry_part : carry_veh_structs ) {
            if( carry_veh->parts[ carry_part ].mount.x() || carry_veh->parts[ carry_part ].mount.y() ) {
                axis = carry_veh->parts[ carry_part ].mount.x() ? "X" : "Y";
            }
        }
    }

    units::angle relative_dir = normalize( carry_veh->face.dir() - face.dir() );
    units::angle relative_180 = units::fmod( relative_dir, 180_degrees );
    units::angle face_dir_180 = normalize( face.dir(), 180_degrees );

    // if the carrier is skewed N/S and the carried vehicle isn't aligned with
    // the carrier, force the carried vehicle to be at a right angle
    if( face_dir_180 >= 45_degrees && face_dir_180 <= 135_degrees ) {
        if( relative_180 >= 45_degrees && relative_180 <= 135_degrees ) {
            if( relative_dir < 180_degrees ) {
                relative_dir = 90_degrees;
            } else {
                relative_dir = 270_degrees;
            }
        }
    }

    // We look at each of the structure parts (mount points, i.e. frames) for the
    // carry vehicle and then find a rack part adjacent to it. If we don't find a rack part,
    // then we can't merge.
    bool found_all_parts = true;
    for( auto carry_part : carry_veh_structs ) {

        // The current position on the original vehicle for this part
        auto carry_pos = carry_veh->bub_part_location( carry_part );

        bool merged_part = false;
        for( int rack_part : rack_parts ) {
            size_t j = 0;
            // There's no mathematical transform from global pos3 to vehicle mount, so search for the
            // carry part in global pos3 after translating
            // TODO: Check if still true. bubble_to_mount exists, and global pos3 used to refer to bubble space position
            // bubble_to_mount( carry_pos );
            tripoint_mnt_veh carry_mount;
            for( point offset : four_cardinal_directions ) {
                carry_mount = parts[ rack_part ].mount + offset;
                auto possible_pos = mount_to_bubble( carry_mount );
                if( possible_pos.raw() == carry_pos.raw() ) {
                    break;
                }
                ++j;
            }

            // We checked the adjacent points from the mounting rack and managed
            // to find the current structure part were looking for nearby. If the part was not
            // near this particular rack, we would look at each in the list of rack_parts
            const bool carry_part_next_to_this_rack = j < four_adjacent_offsets.size();
            if( carry_part_next_to_this_rack ) {
                mapping carry_map;
                auto old_mount = carry_veh->parts[ carry_part ].mount;
                carry_map.carry_parts_here = carry_veh->parts_at_relative( old_mount, true );
                carry_map.rack_part = rack_part;
                carry_map.carry_mount = carry_mount;
                carry_map.old_mount = old_mount;
                carry_data.push_back( carry_map );
                merged_part = true;
                break;
            }
        }

        // We checked all the racks and could not find a place for this structure part.
        if( !merged_part ) {
            found_all_parts = false;
            break;
        }
    }

    // Now that we have mapped all the parts of the carry vehicle to the vehicle with the rack
    // we can go ahead and merge
    if( found_all_parts ) {
        decltype( loot_zones ) new_zones;
        for( const auto &carry_map : carry_data ) {
            std::string offset = string_format( "%s%3d",
                                                carry_map.old_mount == tripoint_mnt_veh::zero() ? axis : " ",
                                                axis == "X" ? carry_map.old_mount.x() : carry_map.old_mount.y() );
            std::string unique_id = string_format( "%s%3d%s", offset,
                                                   static_cast<int>( to_degrees( relative_dir ) ),
                                                   carry_veh->name );
            for( int carry_part : carry_map.carry_parts_here ) {
                parts.push_back( std::move( carry_veh->parts[ carry_part ] ) );
                vehicle_part &carried_part = parts.back();
                carried_part.set_vehicle_hack( this );
                carried_part.mount = carry_map.carry_mount;
                carried_part.carry_names.push( unique_id );
                carried_part.enabled = false;
                carried_part.set_flag( vehicle_part::carried_flag );
                //give each carried part a tracked_flag so that we can re-enable overmap tracking on unloading if necessary
                if( carry_veh->tracking_on ) {
                    carried_part.set_flag( vehicle_part::tracked_flag );
                }
                parts[ carry_map.rack_part ].set_flag( vehicle_part::carrying_flag );
                carry_veh->parts[ carry_part ].removed = true;
            }
            refresh_locations_hack();

            const std::pair<std::unordered_multimap<tripoint_mnt_veh, zone_data>::iterator, std::unordered_multimap<tripoint_mnt_veh, zone_data>::iterator>
            zones_on_point = carry_veh->loot_zones.equal_range( carry_map.old_mount );
            for( std::unordered_multimap<tripoint_mnt_veh, zone_data>::const_iterator it = zones_on_point.first;
                 it != zones_on_point.second; ++it ) {
                new_zones.emplace( carry_map.carry_mount, it->second );
            }
        }

        for( auto &new_zone : new_zones ) {
            zone_manager::get_manager().create_vehicle_loot_zone(
                *this, new_zone.first, new_zone.second );
        }

        // Now that we've added zones to this vehicle, we need to make sure their positions
        // update when we next interact with them
        zones_dirty = true;

        //~ %1$s is the vehicle being loaded onto the bicycle rack
        add_msg( _( "You load the %1$s on the rack" ), carry_veh->name );
        map &here = get_map();
        carry_veh->part_removal_cleanup();
        here.dirty_vehicle_list.insert( this );
        here.set_transparency_cache_dirty( abs_sm_pos.z() );
        here.set_seen_cache_dirty( tripoint_bub_ms::zero() );
        refresh();
    } else {
        //~ %1$s is the vehicle being loaded onto the bicycle rack
        add_msg( m_bad, _( "You can't get the %1$s on the rack" ), carry_veh->name );
    }
    return found_all_parts;
}

bool vehicle::remove_part( const int p )
{
    DefaultRemovePartHandler handler;
    return remove_part( p, handler );
}

bool vehicle::remove_part( const int p, RemovePartHandler &handler )
{
    // NOTE: Don't access g or g->m or anything from it directly here.
    // Forward all access to the handler.
    // There are currently two implementations of it:
    // - one for normal game play (vehicle is on the main map g->m),
    // - one for mapgen (vehicle is on a temporary map used only during mapgen).
    if( p >= static_cast<int>( parts.size() ) ) {
        debugmsg( "Tried to remove part %d but only %d parts!", p, parts.size() );
        return false;
    }
    if( parts[p].removed ) {
        /* This happens only when we had to remove part, because it was depending on
         * other part (using recursive remove_part() call) - currently curtain
         * depending on presence of window and seatbelt depending on presence of seat.
         */
        return false;
    }

    const auto part_loc = handler.part_location( *this, p );

    // Unboard any entities standing on removed boardable parts
    if( part_flag( p, "BOARDABLE" ) && parts[p].has_flag( vehicle_part::passenger_flag ) ) {
        handler.unboard( part_loc );
    }

    // If `p` has flag `parent_flag`, remove child with flag `child_flag`
    // Returns true if removal occurs
    const auto remove_dependent_part = [&]( const std::string & parent_flag,
    const std::string & child_flag ) {
        if( part_flag( p, parent_flag ) ) {
            int dep = part_with_feature( p, child_flag, false );
            if( dep >= 0 && !magic ) {
                handler.add_item_or_charges( part_loc, parts[dep].properties_to_item(), false );
                remove_part( dep, handler );
                return true;
            }
        }
        return false;
    };

    // if a windshield is removed (usually destroyed) also remove curtains
    // attached to it.
    if( remove_dependent_part( "WINDOW", "CURTAIN" ) || part_flag( p, VPFLAG_OPAQUE ) ) {
        handler.set_transparency_cache_dirty( abs_sm_pos.z() );
    }

    if( part_flag( p, VPFLAG_ROOF ) || part_flag( p, VPFLAG_OPAQUE ) ) {
        handler.set_floor_cache_dirty( abs_sm_pos.z() + 1 );
    }

    remove_dependent_part( "SEAT", "SEATBELT" );
    remove_dependent_part( "BATTERY_MOUNT", "NEEDS_BATTERY_MOUNT" );

    // Release any animal held by the part
    if( parts[p].has_flag( vehicle_part::animal_flag ) ) {
        //TODO!: check, not sure what's going on here
        item &base = parts[p].get_base();
        handler.spawn_animal_from_part( base, part_loc );
        parts[p].remove_flag( vehicle_part::animal_flag );
    }

    // Update current engine configuration if needed
    if( part_flag( p, "ENGINE" ) && engines.size() > 1 ) {
        bool any_engine_on = false;

        for( auto &e : engines ) {
            if( e != p && is_part_on( e ) ) {
                any_engine_on = true;
                break;
            }
        }

        if( !any_engine_on ) {
            engine_on = false;
            for( auto &e : engines ) {
                toggle_specific_part( e, true );
            }
        }
    }

    //Remove loot zone if Cargo was removed.
    const auto lz_iter = loot_zones.find( parts[p].mount );
    const bool no_zone = lz_iter != loot_zones.end();

    if( no_zone && part_flag( p, "CARGO" ) ) {
        // Using the key here (instead of the iterator) will remove all zones on
        // this mount points regardless of how many there are
        loot_zones.erase( parts[p].mount );
        zones_dirty = true;
    }
    parts[p].removed = true;
    removed_part_count++;

    handler.removed( *this, p );

    auto vp_mount = parts[p].mount;
    const auto iter = labels.find( label( vp_mount ) );
    if( iter != labels.end() && parts_at_relative( vp_mount, false ).empty() ) {
        labels.erase( iter );
    }

    for( auto &i : get_items( p ).clear() ) {
        // Note: this can spawn items on the other side of the wall!
        // TODO: fix this ^^
        auto dest( part_loc + point_rel_ms( rng( -3, 3 ), rng( -3, 3 ) ) );
        if( !magic ) {
            // This new point might be out of the map bounds.  It's not
            // reasonable to try to spawn it outside the currently valid map,
            // so we pass true here to cause such points to be clamped to the
            // valid bounds without printing an error (as would normally
            // occur).
            handler.add_item_or_charges( dest, std::move( i ), true );
        }
    }
    refresh();
    get_map().invalidate_lightmap_caches();
    coeff_air_changed = true;
    return shift_if_needed();
}

void vehicle::part_removal_cleanup()
{
    bool changed = false;
    map &here = get_map();
    for( std::vector<vehicle_part>::iterator it = parts.begin(); it != parts.end(); /* noop */ ) {
        if( it->removed ) {
            auto items = get_items( std::distance( parts.begin(), it ) );
            while( !items.empty() ) {
                items.erase( items.begin() );
            }
            const auto &pt = bub_part_location( *it );
            here.clear_vehicle_point_from_cache( this, pt );
            it = parts.erase( it );
            changed = true;
        } else {
            ++it;
        }
    }
    refresh_locations_hack();
    removed_part_count = 0;
    if( changed || parts.empty() ) {
        refresh();
        here.invalidate_lightmap_caches();
        if( parts.empty() ) {
            here.destroy_vehicle( this );
            return;
        } else {
            here.add_vehicle_to_cache( this );
        }
    }
    shift_if_needed();
    refresh(); // Rebuild cached indices
    coeff_air_dirty = coeff_air_changed;
    coeff_air_changed = false;
}

void vehicle::remove_carried_flag()
{
    for( vehicle_part &part : parts ) {
        if( part.carry_names.empty() ) {
            // note: we get here if the part was added while the vehicle was carried / mounted. This is not expected.
            // still try to remove the carried flag, if any.
            part.remove_flag( vehicle_part::carried_flag );
        } else {
            part.carry_names.pop();
            if( part.carry_names.empty() ) {
                part.remove_flag( vehicle_part::carried_flag );
            }
        }
    }
}

void vehicle::remove_tracked_flag()
{
    for( vehicle_part &part : parts ) {
        if( part.carry_names.empty() ) {
            part.remove_flag( vehicle_part::tracked_flag );
        } else {
            part.carry_names.pop();
            if( part.carry_names.empty() ) {
                part.remove_flag( vehicle_part::tracked_flag );
            }
        }
    }
}

bool vehicle::remove_carried_vehicle( const std::vector<int> &carried_parts )
{
    if( carried_parts.empty() ) {
        return false;
    }
    std::string veh_record;
    tripoint_bub_ms nbp;
    bool x_aligned = false;
    bool tracked_parts =
        false; // we will set this to true if any of the vehicle parts carry a tracked_flag
    for( int carried_part : carried_parts ) {
        //check if selected part carries tracking flag
        if( parts[carried_part].has_flag(
                vehicle_part::tracked_flag ) ) { //this should only need to run once
            tracked_parts = true;
        }
        const auto &carry_names = parts[carried_part].carry_names;
        if( !carry_names.empty() ) {
            std::string id_string = carry_names.top().substr( 0, 1 );
            if( id_string == "X" || id_string == "Y" ) {
                veh_record = carry_names.top();
                nbp = bub_part_location( carried_part );
                x_aligned = id_string == "X";
                break;
            }
        }
    }
    if( veh_record.empty() ) {
        return false;
    }
    units::angle new_dir =
        normalize( units::from_degrees( std::stoi( veh_record.substr( 4, 3 ) ) ) + face.dir() );
    units::angle host_dir = normalize( face.dir(), 180_degrees );
    // if the host is skewed N/S, and the carried vehicle is going to come at an angle,
    // force it to east/west instead
    if( host_dir >= 45_degrees && host_dir <= 135_degrees ) {
        if( new_dir <= 45_degrees || new_dir >= 315_degrees ) {
            new_dir = 0_degrees;
        } else if( new_dir >= 135_degrees && new_dir <= 225_degrees ) {
            new_dir = 180_degrees;
        }
    }
    vehicle *new_vehicle = g->m.add_vehicle( vproto_id( "none" ), nbp, new_dir );
    if( new_vehicle == nullptr ) {
        add_msg( m_debug, "Unable to unload bike rack, host face %d, new_dir %d!",
                 to_degrees( face.dir() ), to_degrees( new_dir ) );
        return false;
    }
    new_vehicle->owner = owner;
    new_vehicle->old_owner = old_owner;
    std::vector<tripoint_mnt_veh> new_mounts;
    new_vehicle->name = veh_record.substr( vehicle_part::name_offset );
    for( auto carried_part : carried_parts ) {
        tripoint_mnt_veh new_mount;
        std::string mount_str;
        if( !parts[carried_part].carry_names.empty() ) {
            // the mount string should be something like "X 0 0" for ex. We get the first number out of it.
            mount_str = parts[carried_part].carry_names.top().substr( 1, 3 );
        } else {
            // FIX #28712; if we get here it means that a part was added to the bike while the latter was a carried vehicle.
            // This part didn't get a carry_names because those are assigned when the carried vehicle is loaded.
            // We can't be sure to which vehicle it really belongs to, so it will be detached from the vehicle.
            // We can at least inform the player that there's something wrong.
            add_msg( m_warning,
                     _( "A part of the vehicle ('%s') has no containing vehicle's name.  It will be detached from the %s vehicle." ),
                     parts[carried_part].name(),  new_vehicle->name );

            // check if any other parts at the same location have a valid carry name so we can still have a valid mount location.
            for( auto &local_part : parts_at_relative( parts[carried_part].mount, true ) ) {
                if( !parts[local_part].carry_names.empty() ) {
                    mount_str = parts[local_part].carry_names.top().substr( 1, 3 );
                    break;
                }
            }
        }
        if( mount_str.empty() ) {
            add_msg( m_bad,
                     _( "There's not viable mount location on this vehicle: %s. It can't be unloaded from the rack." ),
                     new_vehicle->name );
            return false;
        }
        if( x_aligned ) {
            new_mount.x() = std::stoi( mount_str );
        } else {
            new_mount.y() = std::stoi( mount_str );
        }
        new_mounts.push_back( new_mount );
    }

    std::vector<vehicle *> new_vehicles;
    new_vehicles.push_back( new_vehicle );
    std::vector<std::vector<int>> carried_vehicles;
    carried_vehicles.push_back( carried_parts );
    std::vector<std::vector<tripoint_mnt_veh>> carried_mounts;
    carried_mounts.push_back( new_mounts );
    const bool success = split_vehicles( carried_vehicles, new_vehicles, carried_mounts );
    if( success ) {
        //~ %s is the vehicle being loaded onto the bicycle rack
        add_msg( _( "You unload the %s from the bike rack." ), new_vehicle->name );
        new_vehicle->remove_carried_flag();
        if( tracked_parts ) {
            new_vehicle->toggle_tracking(); //turn on tracking for our newly created vehicle
            new_vehicle->remove_tracked_flag(); //remove our tracking flags now that the vehicle isn't carried
        }
        g->m.dirty_vehicle_list.insert( this );
        part_removal_cleanup();
    } else {
        //~ %s is the vehicle being loaded onto the bicycle rack
        add_msg( m_bad, _( "You can't unload the %s from the bike rack." ), new_vehicle->name );
    }
    return success;
}

bool vehicle::find_and_split_vehicles( int exclude )
{
    std::vector<int> valid_parts = all_parts_at_location( part_location_structure );
    std::set<int> checked_parts;
    checked_parts.insert( exclude );

    std::vector<std::vector <int>> all_vehicles;

    for( size_t cnt = 0 ; cnt < 4 ; cnt++ ) {
        int test_part = -1;
        for( auto p : valid_parts ) {
            if( parts[ p ].removed ) {
                continue;
            }
            if( !checked_parts.contains( p ) ) {
                test_part = p;
                break;
            }
        }
        if( test_part == -1 || static_cast<size_t>( test_part ) > parts.size() ) {
            break;
        }

        std::queue<std::pair<int, std::vector<int>>> search_queue;

        const auto push_neighbor = [&]( int p, const std::vector<int> &with_p ) {
            std::pair<int, std::vector<int>> data( p, with_p );
            search_queue.push( data );
        };
        auto pop_neighbor = [&]() {
            std::pair<int, std::vector<int>> result = search_queue.front();
            search_queue.pop();
            return result;
        };

        std::vector<int> veh_parts;
        push_neighbor( test_part, parts_at_relative( parts[ test_part ].mount, true ) );
        while( !search_queue.empty() ) {
            std::pair<int, std::vector<int>> test_set = pop_neighbor();
            test_part = test_set.first;
            if( checked_parts.contains( test_part ) ) {
                continue;
            }
            for( auto p : test_set.second ) {
                veh_parts.push_back( p );
            }
            checked_parts.insert( test_part );
            for( point offset : four_adjacent_offsets ) {
                const tripoint_mnt_veh &dp = parts[test_part].mount + offset;
                std::vector<int> all_neighbor_parts = parts_at_relative( dp, true );
                int neighbor_struct_part = -1;
                for( int p : all_neighbor_parts ) {
                    if( parts[ p ].removed ) {
                        continue;
                    }
                    if( part_info( p ).location == part_location_structure ) {
                        neighbor_struct_part = p;
                        break;
                    }
                }
                if( neighbor_struct_part != -1 ) {
                    push_neighbor( neighbor_struct_part, all_neighbor_parts );
                }
            }
        }
        // don't include the first vehicle's worth of parts
        if( cnt > 0 ) {
            all_vehicles.push_back( veh_parts );
        }
    }

    if( !all_vehicles.empty() ) {
        bool success = split_vehicles( all_vehicles );
        if( success ) {
            // update the active cache
            g->m.reset_vehicle_cache();
            return true;
        }
    }
    return false;
}

void vehicle::relocate_passengers( const std::vector<Character *> &passengers )
{
    const auto boardables = get_avail_parts( "BOARDABLE" );
    for( auto *passenger : passengers ) {
        for( const vpart_reference &vp : boardables ) {
            if( get_passenger( static_cast<int>( vp.part_index() ) ) == passenger ) {
                passenger->setpos( vp.pos() );
            }
        }
    }
}

bool vehicle::split_vehicles( const std::vector<std::vector <int>> &new_vehs,
                              const std::vector<vehicle *> &new_vehicles,
                              const std::vector<std::vector <tripoint_mnt_veh>> &new_mounts )
{
    bool did_split = false;
    size_t i = 0;
    for( i = 0; i < new_vehs.size(); i ++ ) {
        std::vector<int> split_parts = new_vehs[ i ];
        if( split_parts.empty() ) {
            continue;
        }
        std::vector<tripoint_mnt_veh> split_mounts = new_mounts[ i ];
        did_split = true;

        vehicle *new_vehicle = nullptr;
        if( i < new_vehicles.size() ) {
            new_vehicle = new_vehicles[ i ];
        }
        int split_part0 = split_parts.front();
        tripoint_bub_ms nvp;
        tripoint_mnt_veh mnt_offset;

        decltype( labels ) new_labels;
        decltype( loot_zones ) new_zones;
        if( new_vehicle == nullptr ) {
            // make sure the split_part0 is a legal 0,0 part
            if( split_parts.size() > 1 ) {
                for( const auto p : split_parts ) {
                    if( part_info( p ).location == part_location_structure &&
                        !part_info( p ).has_flag( "PROTRUSION" ) ) {
                        split_part0 = p;
                        break;
                    }
                }
            }
            nvp = bub_part_location( parts[ split_part0 ] );
            mnt_offset = parts[ split_part0 ].mount;
            new_vehicle = g->m.add_vehicle( vproto_id( "none" ), nvp, face.dir() );
            if( new_vehicle == nullptr ) {
                // the split part was out of the map bounds.
                continue;
            }
            new_vehicle->owner = owner;
            new_vehicle->old_owner = old_owner;
            new_vehicle->name = name;
            new_vehicle->move = move;
            new_vehicle->turn_dir = turn_dir;
            new_vehicle->velocity = velocity;
            new_vehicle->vertical_velocity = vertical_velocity;
            new_vehicle->cruise_velocity = cruise_velocity;
            new_vehicle->cruise_on = cruise_on;
            new_vehicle->engine_on = engine_on;
            new_vehicle->brake_hold = brake_hold;
            new_vehicle->tracking_on = tracking_on;
            new_vehicle->camera_on = camera_on;
        }
        new_vehicle->last_fluid_check = last_fluid_check;

        std::vector<Character *> passengers;
        for( size_t new_part = 0; new_part < split_parts.size(); new_part++ ) {
            int mov_part = split_parts[ new_part ];
            auto cur_mount = parts[ mov_part ].mount;
            auto new_mount = cur_mount;
            if( !split_mounts.empty() ) {
                new_mount = split_mounts[ new_part ];
            } else {
                new_mount = ( cur_mount - mnt_offset )
                            .reinterpret_as<tripoint_mnt_veh>();
            }

            player *passenger = nullptr;
            // Unboard any entities standing on any transferred part
            if( part_flag( mov_part, "BOARDABLE" ) ) {
                passenger = get_passenger( mov_part );
                if( passenger ) {
                    passengers.push_back( passenger );
                }
            }
            // if this part is a towing part, transfer the tow_data to the new vehicle.
            if( part_flag( mov_part, "TOW_CABLE" ) ) {
                if( is_towed() ) {
                    tow_data.get_towed_by()->tow_data.set_towing( tow_data.get_towed_by(), new_vehicle );
                    tow_data.clear_towing();
                } else if( is_towing() ) {
                    tow_data.get_towed()->tow_data.set_towing( new_vehicle, tow_data.get_towed() );
                    tow_data.clear_towing();
                }
            }
            // transfer the vehicle_part to the new vehicle
            new_vehicle->parts.emplace_back( parts[ mov_part ], new_vehicle );
            vehicle_part &np = new_vehicle->parts.back();
            np.mount = new_mount;
            np.set_vehicle_hack( new_vehicle );

            // remove labels associated with the mov_part
            const auto iter = labels.find( label( cur_mount ) );
            if( iter != labels.end() ) {
                std::string label_str = iter->text;
                labels.erase( iter );
                new_labels.insert( label( new_mount, label_str ) );
            }
            // Prepare the zones to be moved to the new vehicle
            const std::pair<std::unordered_multimap<tripoint_mnt_veh, zone_data>::iterator, std::unordered_multimap<tripoint_mnt_veh, zone_data>::iterator>
            zones_on_point = loot_zones.equal_range( cur_mount );
            for( std::unordered_multimap<tripoint_mnt_veh, zone_data>::const_iterator lz_iter =
                     zones_on_point.first;
                 lz_iter != zones_on_point.second; ++lz_iter ) {
                new_zones.emplace( new_mount, lz_iter->second );
            }

            // Erasing on the key removes all the zones from the point at once
            loot_zones.erase( cur_mount );

            // The zone manager will be updated when we next interact with it through get_vehicle_zones
            zones_dirty = true;

            // remove the passenger from the old vehicle
            if( passenger ) {
                parts[ mov_part ].remove_flag( vehicle_part::passenger_flag );
                parts[ mov_part ].passenger_id = character_id();
            }
            // indicate the part needs to be removed from the old vehicle
            parts[ mov_part].removed = true;
            removed_part_count++;
        }

        new_vehicle->refresh_locations_hack();
        // We want to create the vehicle zones after we've setup the parts
        // because we need only to move the zone once per mount, not per part. If we move per
        // part, we will end up with duplicates of the zone per part on the same mount
        for( std::pair<tripoint_mnt_veh, zone_data> zone : new_zones ) {
            zone_manager::get_manager().create_vehicle_loot_zone( *new_vehicle, zone.first, zone.second );
        }

        // create_vehicle_loot_zone marks the vehicle as not dirty but since we got these zones
        // in an unknown state from the previous vehicle, we need to let the cache rebuild next
        // time we interact with them
        new_vehicle->zones_dirty = true;

        map &here = get_map();
        here.dirty_vehicle_list.insert( new_vehicle );
        here.set_transparency_cache_dirty( abs_sm_pos.z() );
        here.set_seen_cache_dirty( tripoint_bub_ms::zero() );
        if( !new_labels.empty() ) {
            new_vehicle->labels = new_labels;
        }

        if( split_mounts.empty() ) {
            new_vehicle->refresh();
        } else {
            // include refresh
            new_vehicle->shift_parts( -mnt_offset.reinterpret_as<tripoint_rel_veh>() );
        }

        // update the precalc points
        new_vehicle->precalc_mounts( 1, new_vehicle->skidding ?
                                     new_vehicle->turn_dir : new_vehicle->face.dir(),
                                     new_vehicle->pivot_point() );
        new_vehicle->adjust_zlevel( 1, tripoint_rel_ms::zero() );
        new_vehicle->shift_zlevel();
        if( !passengers.empty() ) {
            new_vehicle->relocate_passengers( passengers );
        }
    }
    if( did_split ) {
        part_removal_cleanup();
    }
    return did_split;
}

bool vehicle::split_vehicles( const std::vector<std::vector <int>> &new_vehs )
{
    std::vector<vehicle *> null_vehicles;
    std::vector<std::vector <tripoint_mnt_veh>> null_mounts;
    std::vector<tripoint_mnt_veh> nothing;
    null_vehicles.assign( new_vehs.size(), nullptr );
    null_mounts.assign( new_vehs.size(), nothing );
    return split_vehicles( new_vehs, null_vehicles, null_mounts );
}

