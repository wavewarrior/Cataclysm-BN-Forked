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

// For reference what each function is supposed to do, see their implementation in
// @ref DefaultRemovePartHandler. Add compatible code for it into @ref MapgenRemovePartHandler,
// if needed.

int vehicle::find_part( const item &it ) const
{
    auto idx = std::ranges::find_if( parts, [&it]( const vehicle_part & e ) {
        return e.base == &it;
    } );
    return idx != parts.end() ? std::distance( parts.begin(), idx ) : INT_MIN;
}

std::vector<detached_ptr<item>> vehicle_part::pieces_for_broken_part() const
{
    const item_group_id &group = info().breaks_into_group;
    // TODO: make it optional? Or use id of empty item group?
    if( !group ) {
        return {};
    }

    return item_group::items_from( group, calendar::turn );
}

std::vector<int> vehicle::parts_at_relative( const tripoint_mnt_veh &dp,
        const bool use_cache ) const
{
    if( !use_cache ) {
    std::vector<int> res;
    for( const vpart_reference &vp : get_all_parts() ) {
            if( vp.mount() == dp && !vp.part().removed ) {
                res.push_back( static_cast<int>( vp.part_index() ) );
            }
        }
        return res;
    } else {
        const auto &iter = relative_parts.find( dp );
        if( iter != relative_parts.end() ) {
            return iter->second;
        } else {
            std::vector<int> res;
            return res;
        }
    }
}

std::optional<vpart_reference> vpart_position::obstacle_at_part() const
{
    const std::optional<vpart_reference> part = part_with_feature( VPFLAG_OBSTACLE, true );
    if( !part ) {
        return std::nullopt; // No obstacle here
    }

    if( part->has_feature( VPFLAG_OPENABLE ) && part->part().open ) {
        return std::nullopt; // Open door here
    }

    return part;
}

std::optional<vpart_reference> vpart_position::part_displayed() const
{
    int part_id = vehicle().part_displayed_at( mount() );
    if( part_id == -1 ) {
        return std::nullopt;
    }
    return vpart_reference( vehicle(), part_id );
}

std::optional<vpart_reference> vpart_position::part_with_feature( const std::string &f,
        const bool unbroken ) const
{
    const int i = vehicle().part_with_feature( part_index(), f, unbroken );
    if( i < 0 ) {
        return std::nullopt;
    }
    return vpart_reference( vehicle(), i );
}

std::optional<vpart_reference> vpart_position::part_with_feature( const vpart_bitflags f,
        const bool unbroken ) const
{
    const int i = vehicle().part_with_feature( part_index(), f, unbroken );
    if( i < 0 ) {
        return std::nullopt;
    }
    return vpart_reference( vehicle(), i );
}

std::optional<vpart_reference> optional_vpart_position::part_with_feature( const std::string &f,
        const bool unbroken ) const
{
    return has_value() ? value().part_with_feature( f, unbroken ) : std::nullopt;
}

std::optional<vpart_reference> optional_vpart_position::part_with_feature( const vpart_bitflags f,
        const bool unbroken ) const
{
    return has_value() ? value().part_with_feature( f, unbroken ) : std::nullopt;
}

std::optional<vpart_reference> optional_vpart_position::obstacle_at_part() const
{
    return has_value() ? value().obstacle_at_part() : std::nullopt;
}

std::optional<vpart_reference> optional_vpart_position::part_displayed() const
{
    return has_value() ? value().part_displayed() : std::nullopt;
}

int vehicle::part_with_feature( int part, vpart_bitflags const flag, bool unbroken ) const
{
    if( part_flag( part, flag ) && ( !unbroken || !parts[part].is_broken() ) ) {
    return part;
}
const auto it = relative_parts.find( parts[part].mount );
if( it != relative_parts.end() ) {
    const std::vector<int> &parts_here = it->second;
    for( auto &i : parts_here ) {
            if( part_flag( i, flag ) && ( !unbroken || !parts[i].is_broken() ) ) {
                return i;
            }
        }
    }
    return -1;
}

int vehicle::part_with_feature( int part, const std::string &flag, bool unbroken ) const
{
    return part_with_feature( parts[part].mount, flag, unbroken );
}

int vehicle::part_with_feature( const tripoint_mnt_veh &pt, const std::string &flag,
                                bool unbroken ) const
{
    std::vector<int> parts_here = parts_at_relative( pt, true );
    for( auto &elem : parts_here ) {
        if( part_flag( elem, flag ) && ( !unbroken || !parts[ elem ].is_broken() ) ) {
            return elem;
        }
    }
    return -1;
}

int vehicle::avail_part_with_feature( int part, vpart_bitflags const flag, bool unbroken ) const
{
    int part_a = part_with_feature( part, flag, unbroken );
    if( ( part_a >= 0 ) && parts[ part_a ].is_available() ) {
        return part_a;
    }
    return -1;
}

int vehicle::avail_part_with_feature( int part, const std::string &flag, bool unbroken ) const
{
    return avail_part_with_feature( parts[ part ].mount, flag, unbroken );
}

int vehicle::avail_part_with_feature( const tripoint_mnt_veh &pt, const std::string &flag,
                                      bool unbroken ) const
{
    int part_a = part_with_feature( pt, flag, unbroken );
    if( ( part_a >= 0 ) && parts[ part_a ].is_available() ) {
        return part_a;
    }
    return -1;
}

bool vehicle::has_part( const std::string &flag, bool enabled ) const
{
    return std::ranges::any_of( parts, [&flag, &enabled]( const vehicle_part & e ) {
        return !e.removed && ( !enabled || e.enabled ) && !e.is_broken() && e.info().has_flag( flag );
    } );
}

bool vehicle::has_part( const vpart_bitflags &flag, bool enabled ) const
{
    return std::ranges::any_of( parts, [&flag, &enabled]( const vehicle_part & e ) {
        return !e.removed && ( !enabled || e.enabled ) && !e.is_broken() && e.info().has_flag( flag );
    } );
}

bool vehicle::has_part( const tripoint_bub_ms &pos, const std::string &flag, bool enabled ) const
{
    const auto relative_pos = pos - bub_ms_location();

    for( const auto &e : parts ) {
        if( e.precalc[0] != relative_pos.xy() || e.mount.z() + e.z_terrain[0] != relative_pos.z() ) {
            continue;
        }
        if( !e.removed && ( !enabled || e.enabled ) && !e.is_broken() && e.info().has_flag( flag ) ) {
            return true;
        }
    }
    return false;
}

int vehicle::obstacle_at_position( const tripoint_mnt_veh &pos ) const
{
    int i = part_with_feature( pos, "OBSTACLE", true );

    if( i == -1 ) {
        return -1;
    }

    auto &ref = parts[i];

    if( ref.info().has_flag( VPFLAG_OPENABLE ) && ref.open ) {
        return -1;
    }

    return i;
}

int vehicle::opaque_at_position( const tripoint_mnt_veh &pos ) const
{
    int i = part_with_feature( pos, "OPAQUE", true );

    if( i == -1 ) {
        return -1;
    }

    auto &ref = parts[i];

    if( ref.info().has_flag( VPFLAG_OPENABLE ) && ref.open ) {
        return -1;
    }

    return i;
}

std::vector<vehicle_part *> vehicle::get_parts_at( const tripoint_bub_ms &pos,
        const std::string &flag,
        const part_status_flag condition )
{
    const auto relative_pos = pos - bub_ms_location();
    std::vector<vehicle_part *> res;
    for( auto &e : parts ) {
        if( e.precalc[0] != relative_pos.xy() || e.mount.z() + e.z_terrain[0] != relative_pos.z() ) {
            continue;
        }
        if( !e.removed &&
            ( flag.empty() || e.info().has_flag( flag ) ) &&
            ( !( condition & part_status_flag::enabled ) || e.enabled ) &&
            ( !( condition & part_status_flag::working ) || !e.is_broken() ) ) {
            res.push_back( &e );
        }
    }
    return res;
}

std::vector<const vehicle_part *> vehicle::get_parts_at( const tripoint_bub_ms &pos,
        const std::string &flag,
        const part_status_flag condition ) const
{
    const auto relative_pos = pos - bub_ms_location();
    std::vector<const vehicle_part *> res;
    for( const auto &e : parts ) {
        if( e.precalc[0] != relative_pos.xy() || e.mount.z() + e.z_terrain[0] != relative_pos.z() ) {
            continue;
        }
        if( !e.removed &&
            ( flag.empty() || e.info().has_flag( flag ) ) &&
            ( !( condition & part_status_flag::enabled ) || e.enabled ) &&
            ( !( condition & part_status_flag::working ) || !e.is_broken() ) ) {
            res.push_back( &e );
        }
    }
    return res;
}

std::optional<std::string> vpart_position::get_label() const
{
    const auto it = vehicle().labels.find( label( mount() ) );
    if( it == vehicle().labels.end() ) {
        return std::nullopt;
    }
    if( it->text.empty() ) {
        // legacy support TODO: change labels into a map and keep track of deleted labels
        return std::nullopt;
    }
    return it->text;
}

void vpart_position::set_label( const std::string &text ) const
{
    auto &labels = vehicle().labels;
    const auto it = labels.find( label( mount() ) );
    // TODO: empty text should remove the label instead of just storing an empty string, see get_label
    if( it == labels.end() ) {
        labels.insert( label( mount(), text ) );
    } else {
        // labels should really be a map
        labels.insert( labels.erase( it ), label( mount(), text ) );
    }
}

int vehicle::next_part_to_close( int p, bool outside ) const
{
    std::vector<int> parts_here = parts_at_relative( parts[p].mount, true );

    // We want reverse, since we close the outermost thing first (curtains), and then the innermost thing (door)
    for( std::vector<int>::reverse_iterator part_it = parts_here.rbegin();
         part_it != parts_here.rend();
         ++part_it ) {

        if( part_flag( *part_it, VPFLAG_OPENABLE )
            && parts[ *part_it ].is_available()
            && parts[*part_it].open == 1
            && ( !outside || !part_flag( *part_it, "OPENCLOSE_INSIDE" ) ) ) {
            return *part_it;
        }
    }
    return -1;
}

int vehicle::next_part_to_open( int p, bool outside ) const
{
    std::vector<int> parts_here = parts_at_relative( parts[p].mount, true );

    // We want forwards, since we open the innermost thing first (curtains), and then the innermost thing (door)
    for( auto &elem : parts_here ) {
        if( part_flag( elem, VPFLAG_OPENABLE ) && parts[ elem ].is_available() && parts[elem].open == 0 &&
            ( !outside || !part_flag( elem, "OPENCLOSE_INSIDE" ) ) ) {
            return elem;
        }
    }
    return -1;
}

vehicle_part_with_feature_range<std::string> vehicle::get_avail_parts( std::string feature ) const
{
    return vehicle_part_with_feature_range<std::string>( const_cast<vehicle &>( *this ),
    std::move( feature ),
    ( part_status_flag::working |
    part_status_flag::available ) );
}

vehicle_part_with_feature_range<vpart_bitflags> vehicle::get_avail_parts(
    const vpart_bitflags feature ) const
{
    return vehicle_part_with_feature_range<vpart_bitflags>( const_cast<vehicle &>( *this ), feature,
    ( part_status_flag::working |
    part_status_flag::available ) );
}

vehicle_part_with_feature_range<std::string> vehicle::get_parts_including_carried(
    std::string feature ) const
{
    return vehicle_part_with_feature_range<std::string>( const_cast<vehicle &>( *this ),
    std::move( feature ), part_status_flag::working );
}

vehicle_part_with_feature_range<vpart_bitflags> vehicle::get_parts_including_carried(
    const vpart_bitflags feature ) const
{
    return vehicle_part_with_feature_range<vpart_bitflags>( const_cast<vehicle &>( *this ), feature,
    part_status_flag::working );
}

vehicle_part_with_feature_range<std::string> vehicle::get_any_parts( std::string feature ) const
{
    return vehicle_part_with_feature_range<std::string>( const_cast<vehicle &>( *this ),
    std::move( feature ), part_status_flag::any );
}

vehicle_part_with_feature_range<vpart_bitflags> vehicle::get_any_parts(
    const vpart_bitflags feature ) const
{
    return vehicle_part_with_feature_range<vpart_bitflags>( const_cast<vehicle &>( *this ), feature,
    part_status_flag::any );
}

vehicle_part_with_feature_range<std::string> vehicle::get_enabled_parts( std::string feature ) const
{
    return vehicle_part_with_feature_range<std::string>( const_cast<vehicle &>( *this ),
    std::move( feature ),
    ( part_status_flag::enabled |
    part_status_flag::working |
    part_status_flag::available ) );
}

vehicle_part_with_feature_range<vpart_bitflags> vehicle::get_enabled_parts(
    const vpart_bitflags feature ) const
{
    return vehicle_part_with_feature_range<vpart_bitflags>( const_cast<vehicle &>( *this ), feature,
    ( part_status_flag::enabled |
    part_status_flag::working |
    part_status_flag::available ) );
}

/**
 *
 * Returns all the parts in the vehicle that are either a structural part or
 * a extendable protusion part
 * @return A list of indices to all parts with the structure location or otherwise standalone
 */
std::vector<int> vehicle::all_standalone_parts() const
{
    std::vector<int> parts_found;
    for( size_t part_index = 0; part_index < parts.size(); ++part_index ) {
        if( ( part_info( part_index ).location == part_location_structure ||
              part_info( part_index ).has_flag( VPFLAG_EXTENDABLE ) ) &&
            !parts[part_index].removed ) {
            parts_found.push_back( part_index );
        }
    }
    return parts_found;
}
/**
 * Returns all parts in the vehicle that exist in the given location slot. If
 * the empty string is passed in, returns all parts with no slot.
 * @param location The location slot to get parts for.
 * @return A list of indices to all parts with the specified location.
 */
std::vector<int> vehicle::all_parts_at_location( const std::string &location ) const
{
    std::vector<int> parts_found;
    for( size_t part_index = 0; part_index < parts.size(); ++part_index ) {
        if( part_info( part_index ).location == location && !parts[part_index].removed ) {
            parts_found.push_back( part_index );
        }
    }
    return parts_found;
}

// another NPC probably removed a part in the time it took to walk here and start the activity.
// as the part index was first "chosen" before the NPC started traveling here.
// therefore the part index is now invalid shifted by one or two ( depending on how many other NPCs working on this vehicle )
// so loop over the part indexes in reverse order to get the next one down that matches the part type we wanted to remove
int vehicle::get_next_shifted_index( int original_index, Character &who )
{
    int ret_index = original_index;
    bool found_shifted_index = false;
    for( vehicle_part &part : parts | std::views::reverse ) {
        if( who.get_value( "veh_index_type" ) == part.info().name() ) {
            ret_index = index_of_part( &part );
            found_shifted_index = true;
            break;
        }
    }
    if( !found_shifted_index ) {
        // we are probably down to a few parts left, and things get messy here, so an alternative index maybe can't be found
        // if loads of npcs are all removing parts at the same time.
        // if that's the case, just bail out and give up, somebody else is probably doing the job right now anyway.
        return -1;
    } else {
        return ret_index;
    }
}

/**
 * Returns all parts in the vehicle that have the specified flag in their vpinfo and
 * are on the same X-axis or Y-axis as the input part and are contiguous with each other.
 * @param part The part to find adjacent parts to
 * @param flag The flag to match
 * @return A list of lists of indices of all parts sharing the flag and contiguous with the part
 * on the X or Y axis. Returns 0, 1, or 2 lists of indices.
 */
std::vector<std::vector<int>> vehicle::find_lines_of_parts( int part, const std::string &flag )
{
    const auto possible_parts = get_avail_parts( flag );
    std::vector<std::vector<int>> ret_parts;
    if( possible_parts.empty() ) {
        return ret_parts;
    }

    std::vector<int> x_parts;
    std::vector<int> y_parts;
    vpart_id part_id = part_info( part ).get_id();
    // create vectors of parts on the same X or Y axis
    auto target = parts[ part ].mount;
    for( const vpart_reference &vp : possible_parts ) {
        if( vp.part().is_unavailable() ||
            !vp.has_feature( "MULTISQUARE" ) ||
            vp.info().get_id() != part_id )  {
            continue;
        }
        if( vp.mount().x() == target.x() ) {
            x_parts.push_back( vp.part_index() );
        }
        if( vp.mount().y() == target.y() ) {
            y_parts.push_back( vp.part_index() );
        }
    }

    if( x_parts.size() > 1 ) {
        std::vector<int> x_ret;
        // sort by Y-axis, since they're all on the same X-axis
        const auto x_sorter = [&]( const int lhs, const int rhs ) {
            return( parts[lhs].mount.y() > parts[rhs].mount.y() );
        };
        std::sort( x_parts.begin(), x_parts.end(), x_sorter );
        int first_part = 0;
        int prev_y = parts[ x_parts[ 0 ] ].mount.y();
        int i;
        bool found_part = x_parts[ 0 ] == part;
        for( i = 1; static_cast<size_t>( i ) < x_parts.size(); i++ ) {
            // if the Y difference is > 1, there's a break in the run
            if( std::abs( parts[ x_parts[ i ] ].mount.y() - prev_y )  > 1 ) {
                // if we found the part, this is the run we wanted
                if( found_part ) {
                    break;
                }
                first_part = i;
            }
            found_part |= x_parts[ i ] == part;
            prev_y = parts[ x_parts[ i ] ].mount.y();
        }
        for( size_t j = first_part; j < static_cast<size_t>( i ); j++ ) {
            x_ret.push_back( x_parts[ j ] );
        }
        ret_parts.push_back( x_ret );
    }
    if( y_parts.size() > 1 ) {
        std::vector<int> y_ret;
        const auto y_sorter = [&]( const int lhs, const int rhs ) {
            return( parts[lhs].mount.x() > parts[rhs].mount.x() );
        };
        std::sort( y_parts.begin(), y_parts.end(), y_sorter );
        int first_part = 0;
        int prev_x = parts[ y_parts[ 0 ] ].mount.x();
        int i;
        bool found_part = y_parts[ 0 ] == part;
        for( i = 1; static_cast<size_t>( i ) < y_parts.size(); i++ ) {
            if( std::abs( parts[ y_parts[ i ] ].mount.x() - prev_x )  > 1 ) {
                if( found_part ) {
                    break;
                }
                first_part = i;
            }
            found_part |= y_parts[ i ] == part;
            prev_x = parts[ y_parts[ i ] ].mount.x();
        }
        for( size_t j = first_part; j < static_cast<size_t>( i ); j++ ) {
            y_ret.push_back( y_parts[ j ] );
        }
        ret_parts.push_back( y_ret );
    }
    if( y_parts.size() == 1 && x_parts.size() == 1 ) {
        ret_parts.push_back( x_parts );
    }
    return ret_parts;
}

bool vehicle::part_flag( int part, const std::string &flag ) const
{
    if( part < 0 || part >= static_cast<int>( parts.size() ) || parts[part].removed ) {
        return false;
    } else {
        return part_info( part ).has_flag( flag );
    }
}

bool vehicle::part_flag( int part, const vpart_bitflags flag ) const
{
    if( part < 0 || part >= static_cast<int>( parts.size() ) || parts[part].removed ) {
        return false;
    } else {
        return part_info( part ).has_flag( flag );
    }
}

int vehicle::part_at( const tripoint_rel_ms &dp ) const
{
for( const vpart_reference &vp : get_all_parts() ) {
    const vehicle_part &p = vp.part();
        if( !p.removed &&
            p.precalc[0] == dp.xy() &&
            p.mount.z() + p.z_terrain[0] == dp.z() ) {
            return static_cast<int>( vp.part_index() );
        }
    }
    return -1;
}

/**
 * Given a vehicle part which is inside of this vehicle, returns the index of
 * that part. This exists solely because activities relating to vehicle editing
 * require the index of the vehicle part to be passed around.
 * @param part The part to find.
 * @param check_removed Check whether this part can be removed
 * @return The part index, -1 if it is not part of this vehicle.
 */
int vehicle::index_of_part( const vehicle_part *const part, const bool check_removed ) const
{
    if( part != nullptr ) {
        for( const vpart_reference &vp : get_all_parts() ) {
            const vehicle_part &next_part = vp.part();
            if( !check_removed && next_part.removed ) {
                continue;
            }
            if( part->id == next_part.id && part->mount == vp.mount() ) {
                return vp.part_index();
            }
        }
    }
    return -1;
}

/**
 * Returns which part (as an index into the parts list) is the one that will be
 * displayed for the given square. Returns -1 if there are no parts in that
 * square.
 * @param dp The local coordinate.
 * @return The index of the part that will be displayed.
 */
int vehicle::part_displayed_at( const tripoint_mnt_veh &dp ) const
{
    // Z-order is implicitly defined in game::load_vehiclepart, but as
    // numbers directly set on parts rather than constants that can be
    // used elsewhere. A future refactor might be nice but this way
    // it's clear where the magic number comes from.
    const int ON_ROOF_Z = 9;

    std::vector<int> parts_in_square = parts_at_relative( dp, true );

    if( parts_in_square.empty() ) {
        return -1;
    }

    bool in_vehicle = g->u.in_vehicle;
    if( in_vehicle ) {
        // They're in a vehicle, but are they in /this/ vehicle?
        std::vector<int> psg_parts = boarded_parts();
        in_vehicle = false;
        for( auto &psg_part : psg_parts ) {
            if( get_passenger( psg_part ) == &g->u ) {
                in_vehicle = true;
                break;
            }
        }
    }

    int hide_z_at_or_above = ( in_vehicle ) ? ( ON_ROOF_Z ) : INT_MAX;

    int top_part = 0;
    for( size_t index = 1; index < parts_in_square.size(); index++ ) {
        if( ( part_info( parts_in_square[top_part] ).z_order <
              part_info( parts_in_square[index] ).z_order ) &&
            ( part_info( parts_in_square[index] ).z_order <
              hide_z_at_or_above ) ) {
            top_part = index;
        }
    }

    return parts_in_square[top_part];
}

int vehicle::roof_at_part( const int part ) const
{
    std::vector<int> parts_in_square = parts_at_relative( parts[part].mount, true );
    for( const int p : parts_in_square ) {
        if( part_info( p ).location == "on_roof" || part_flag( p, "ROOF" ) ) {
            return p;
        }
    }

    return -1;
}

const struct {
    float gradient;
    bool flipH;
    bool flipV;
    bool swapXY;
} rotation_info[24] = {
    {static_cast<float>( tan( units::to_radians( 0_degrees ) ) ),  false, false,   false}, //0 degrees
    {static_cast<float>( tan( units::to_radians( 15_degrees ) ) ), false, false,   false},
    {static_cast<float>( tan( units::to_radians( 30_degrees ) ) ), false, false,   false},
    {static_cast<float>( -tan( units::to_radians( 45_degrees ) ) ), true,  false, true}, //45 degrees
    {static_cast<float>( -tan( units::to_radians( 30_degrees ) ) ), true,  false, true},
    {static_cast<float>( -tan( units::to_radians( 15_degrees ) ) ), true,  false, true},
    {static_cast<float>( tan( units::to_radians( 0_degrees ) ) ),  true,  false,   true}, //90 degrees
    {static_cast<float>( tan( units::to_radians( 15_degrees ) ) ), true,  false,   true},
    {static_cast<float>( tan( units::to_radians( 30_degrees ) ) ), true,  false,   true},
    {static_cast<float>( tan( units::to_radians( 45_degrees ) ) ), true,  false,   true}, //135 degrees
    {static_cast<float>( -tan( units::to_radians( 30_degrees ) ) ), true,  true,  false},
    {static_cast<float>( -tan( units::to_radians( 15_degrees ) ) ), true,  true,  false},
    {static_cast<float>( tan( units::to_radians( 0_degrees ) ) ),  true,  true,    false}, //180 degrees
    {static_cast<float>( tan( units::to_radians( 15_degrees ) ) ), true,  true,    false},
    {static_cast<float>( tan( units::to_radians( 30_degrees ) ) ), true,  true,    false},
    {static_cast<float>( -tan( units::to_radians( 45_degrees ) ) ), false, true,  true}, //225 degrees
    {static_cast<float>( -tan( units::to_radians( 30_degrees ) ) ), false, true,  true},
    {static_cast<float>( -tan( units::to_radians( 15_degrees ) ) ), false, true,  true},
    {static_cast<float>( tan( units::to_radians( 0_degrees ) ) ),  false,  true,   true}, //270 degrees
    {static_cast<float>( tan( units::to_radians( 15_degrees ) ) ), false,  true,   true},
    {static_cast<float>( tan( units::to_radians( 30_degrees ) ) ), false,  true,   true},
    {static_cast<float>( tan( units::to_radians( 45_degrees ) ) ), false,  true,   true}, //315 degrees
    {static_cast<float>( -tan( units::to_radians( 30_degrees ) ) ), false,  false, false},
    {static_cast<float>( -tan( units::to_radians( 15_degrees ) ) ), false,  false, false},
};

tripoint_rel_ms vehicle::coord_translate( const tripoint_mnt_veh &p ) const
{
    return rotate_to_world( pivot_rotation[0], pivot_anchor[0], p );
}

void vehicle::coord_translate( units::angle dir, const tripoint_mnt_veh &pivot,
                               const tripoint_mnt_veh &p,
                               point_rel_ms &q ) const
{
    q = rotate_to_world( dir, pivot, p ).xy();
}

void vehicle::coord_translate_reverse( units::angle dir, const tripoint_mnt_veh &pivot,
                                       const tripoint_rel_ms &p,
                                       tripoint_mnt_veh &q ) const
{
    q = rotate_to_local( dir, pivot, p );
}

tripoint_bub_ms vehicle::mount_to_bubble( const tripoint_mnt_veh &mount ) const
{
    return bub_ms_location() + rotate_to_world( pivot_rotation[0], pivot_anchor[0], mount );
}

tripoint_bub_ms vehicle::mount_to_bubble( const tripoint_mnt_veh &mount,
        const tripoint_rel_veh &offset ) const
{
    return mount_to_bubble( mount + offset );
}

tripoint_mnt_veh vehicle::bubble_to_mount( const tripoint_bub_ms &p ) const
{
    return rotate_to_local( pivot_rotation[0], pivot_anchor[0], p - bub_ms_location() );
}

tripoint_rel_ms vehicle::rotate_to_world( units::angle dir, const tripoint_mnt_veh &pivot,
        const tripoint_mnt_veh &p ) const
{
    int increment = angle_to_increment( dir );
    auto relative = p - pivot;
    float skew = std::trunc( relative.x() * rotation_info[increment].gradient );

    tripoint result;
    result.x = relative.x();
    result.y = relative.y() + static_cast<int>( skew );

    if( rotation_info[increment].swapXY ) {
        std::swap( result.x, result.y );
    }
    if( rotation_info[increment].flipH ) {
        result.x = -result.x;
    }
    if( rotation_info[increment].flipV ) {
        result.y = -result.y;
    }
    return tripoint_rel_ms( result );
}

tripoint_mnt_veh vehicle::rotate_to_local( units::angle dir, const tripoint_mnt_veh &pivot,
        const tripoint_rel_ms &p ) const
{
    int increment = angle_to_increment( dir );

    tripoint result = p.raw();

    if( rotation_info[increment].flipV ) {
        result.y = -result.y;
    }
    if( rotation_info[increment].flipH ) {
        result.x = -result.x;
    }
    if( rotation_info[increment].swapXY ) {
        std::swap( result.x, result.y );
    }

    float skew = std::trunc( result.x * rotation_info[increment].gradient );
    result.y -= static_cast<int>( skew );
    result += pivot.raw();

    return tripoint_mnt_veh( result );
}

tripoint_abs_ms vehicle::mount_to_abs( const tripoint_mnt_veh &mount ) const
{
    return abs_ms_location() + rotate_to_world( pivot_rotation[0], pivot_anchor[0], mount );
}

tripoint_mnt_veh vehicle::abs_to_mount( const tripoint_abs_ms &abs ) const
{
    return rotate_to_local( pivot_rotation[0], pivot_anchor[0], abs - abs_ms_location() );
}

int vehicle::angle_to_increment( units::angle dir )
{
    int increment = ( std::lround( to_degrees( dir ) ) % 360 ) / 15;
    if( increment < 0 ) {
        increment += 360 / 15;
    }
    return increment;
}

void vehicle::precalc_mounts( int idir, units::angle dir, const tripoint_mnt_veh &pivot )
{
    if( idir < 0 || idir > 1 ) {
        idir = 0;
    }
    std::unordered_map<tripoint_mnt_veh, point_rel_ms> mount_to_precalc;
    for( auto &p : parts ) {
        if( p.removed ) {
            continue;
        }
        auto q = mount_to_precalc.find( p.mount );
        if( q == mount_to_precalc.end() ) {
            coord_translate( dir, pivot, p.mount, p.precalc[idir] );
            mount_to_precalc.insert( { p.mount, p.precalc[idir] } );
        } else {
            p.precalc[idir] = q->second;
        }
    }
    pivot_anchor[idir] = pivot;
    pivot_rotation[idir] = dir;
}

#ifdef BOX2D_ENABLED
void vehicle::refresh_precalc( float physics_angle )
{
    const float c = std::cos( physics_angle );
    const float s = std::sin( physics_angle );
    for( auto &p : parts ) {
        if( p.removed ) { continue; }
        const float mx = static_cast<float>( p.mount.x() );
        const float my = static_cast<float>( p.mount.y() );
        p.precalc[0] = point_rel_ms{
            static_cast<int>( std::round( mx * c - my * s ) ),
            static_cast<int>( std::round( mx * s + my * c ) )
        };
    }
}
#endif

bool vehicle::check_rotated_intervening( const tripoint_mnt_veh &from, const tripoint_mnt_veh &to,
        bool( *check )( const vehicle *, const tripoint_mnt_veh & ) ) const
{
    auto delta = to - from;
    if( abs( delta.x() ) <= 1 && abs( delta.y() ) <= 1 ) { //Just a normal move
        return true;
    }

    if( !( ( abs( delta.x() ) == 2 && abs( delta.y() ) == 1 ) || ( abs( delta.x() ) == 1 &&
            abs( delta.y() ) == 2 ) ) ) { //Check that we're moving like a knight
        debugmsg( "Unexpected movement in rotated vehicle vector:%d,%d", delta.x(), delta.y() );
        return false;
    }

    if( abs( delta.x() ) == 2 ) { //Mostly horizontal move
        auto t1 = from + point_rel_veh( delta.x() / 2, delta.y() );
        if( check( this, t1 ) ) {
            return true;
        }

        auto t2 = from + point_rel_veh( delta.x() / 2, 0 );
        if( check( this, t2 ) ) {
            return true;
        }

    } else { //Mostly vertical move
        auto t1 = from + point_rel_veh( delta.x(), delta.y() / 2 );
        if( check( this, t1 ) ) {
            return true;
        }

        auto t2 = from + point_rel_veh( 0, delta.y() / 2 );
        if( check( this, t2 ) ) {
            return true;
        }
    }

    return false;
}

bool vehicle::allowed_light( const tripoint_mnt_veh &from, const tripoint_mnt_veh &to ) const
{
    return check_rotated_intervening( from, to, []( const vehicle * veh, const tripoint_mnt_veh & p ) {
        return ( veh->opaque_at_position( p ) == -1 );
    } );
}

bool vehicle::allowed_move( const tripoint_mnt_veh &from, const tripoint_mnt_veh &to ) const
{
    return check_rotated_intervening( from, to, []( const vehicle * veh, const tripoint_mnt_veh & p ) {
        return ( veh->obstacle_at_position( p ) == -1 );
    } );
}

std::vector<int> vehicle::boarded_parts() const
{
    std::vector<int> res;
    for( const vpart_reference &vp : get_avail_parts( VPFLAG_BOARDABLE ) ) {
        if( vp.part().has_flag( vehicle_part::passenger_flag ) ) {
            res.push_back( static_cast<int>( vp.part_index() ) );
        }
    }
    return res;
}

std::vector<rider_data> vehicle::get_riders() const
{
    std::vector<rider_data> res;
    for( const vpart_reference &vp : get_avail_parts( VPFLAG_BOARDABLE ) ) {
        Creature *rider = g->critter_at( vp.pos() );
        if( rider ) {
            rider_data r;
            r.prt = vp.part_index();
            r.psg = rider;
            res.emplace_back( r );
        }
    }
    return res;
}

player *vehicle::get_passenger( int p ) const
{
    const auto &target_mount = parts[p].mount;
    for( auto &part : parts ) {
        if( part.removed || part.mount != target_mount ) {
            continue;
        }
        if( part.info().has_flag( "BOARDABLE" ) && part.has_flag( vehicle_part::passenger_flag ) ) {
            return g->critter_by_id<player>( part.passenger_id );
        }
    }
    return nullptr;
}

monster *vehicle::get_pet( int p ) const
{
    p = part_with_feature( p, VPFLAG_BOARDABLE, false );
    if( p >= 0 ) {
        return g->critter_at<monster>( bub_part_location( p ), true );
    }
    return nullptr;
}

tripoint_abs_ms vehicle::abs_ms_location() const
{
    return project_combine( abs_sm_pos, sm_ms_pos );
}

tripoint_bub_ms vehicle::bub_ms_location() const
{
    return get_map().abs_to_bub( abs_ms_location() );
}

tripoint_bub_ms vehicle::bub_part_location( const int &index ) const
{
    return bub_part_location( parts[index] );
}

tripoint_bub_ms vehicle::bub_part_location( const vehicle_part &pt ) const
{
    return bub_ms_location() + tripoint_rel_ms( pt.precalc[0],
    pt.mount.z() + pt.z_terrain[0] );
}

tripoint_abs_ms vehicle::abs_part_location( const int &index ) const
{
    return abs_part_location( parts[index] );
}

tripoint_abs_ms vehicle::abs_part_location( const vehicle_part &pt ) const
{
    return abs_ms_location() + tripoint_rel_ms( pt.precalc[0],
    pt.mount.z() + pt.z_terrain[0] );
}
