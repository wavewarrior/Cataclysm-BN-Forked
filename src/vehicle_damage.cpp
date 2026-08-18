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

void vehicle::shed_loose_parts()
{
    // remove_part rebuilds the loose_parts vector, when all of those parts have been removed,
    // it will stay empty.
    while( !loose_parts.empty() ) {
        const int elem = loose_parts.front();
        if( part_flag( elem, "POWER_TRANSFER" ) ) {
            remove_remote_part( elem );
        }
        if( is_towing() || is_towed() ) {
            vehicle *other_veh = is_towing() ? tow_data.get_towed() : tow_data.get_towed_by();
            if( other_veh ) {
                other_veh->remove_part( other_veh->part_with_feature( other_veh->get_tow_part(), "TOW_CABLE",
                                        true ) );
                other_veh->tow_data.clear_towing();
            }
            tow_data.clear_towing();
        }
        auto part = &parts[elem];
        if( !magic ) {
            g->m.add_item_or_charges( bub_part_location( *part ), part->properties_to_item() );
        }

        remove_part( elem );
    }
}

bool vehicle::enclosed_at( const tripoint_bub_ms &pos )
{
    refresh_insides();
    std::vector<vehicle_part *> parts_here = get_parts_at( pos, "BOARDABLE",
        part_status_flag::working );
    if( !parts_here.empty() ) {
        return parts_here.front()->inside;
    }
    return false;
}

void vehicle::refresh_insides()
{
    if( !insides_dirty ) {
        return;
    }
    insides_dirty = false;
    for( const vpart_reference &vp : get_all_parts() ) {
        const size_t p = vp.part_index();
        if( vp.part().removed ) {
            continue;
        }
        /* If there's no roof, or there is a roof but it's broken, it's outside.
         * (Use short-circuiting && so broken frames don't screw this up) */
        if( !( part_with_feature( p, "ROOF", true ) >= 0 && vp.part().is_available() ) ) {
            vp.part().inside = false;
            continue;
        }

        // inside if not otherwise
        parts[p].inside = true;
        // let's check four neighbor parts
        for( point offset : four_adjacent_offsets ) {
            auto near_mount = parts[ p ].mount + offset;
            std::vector<int> parts_n3ar = parts_at_relative( near_mount, true );
            // if we aren't covered from sides, the roof at p won't save us
            bool cover = false;
            for( auto &j : parts_n3ar ) {
                // another roof -- cover
                if( part_flag( j, "ROOF" ) && parts[ j ].is_available() ) {
                    cover = true;
                    break;
                } else if( part_flag( j, "OBSTACLE" ) && parts[ j ].is_available() ) {
                    // found an obstacle, like board or windshield or door
                    if( parts[j].inside || ( part_flag( j, "OPENABLE" ) && parts[j].open ) ) {
                        // door and it's open -- can't cover
                        continue;
                    }
                    cover = true;
                    break;
                }
                //Otherwise keep looking, there might be another part in that square
            }
            if( !cover ) {
                vp.part().inside = false;
                break;
            }
        }
    }
}

bool vpart_position::is_inside() const
{
    // TODO: this is a bit of a hack as refresh_insides has side effects
    // this should be called elsewhere and not in a function that intends to just query
    // it's also a no-op if the insides are up to date.
    vehicle().refresh_insides();
    return vehicle().part( part_index() ).inside;
}

void vehicle::unboard_all()
{
    std::vector<int> bp = boarded_parts();
    for( auto &i : bp ) {
        g->m.unboard_vehicle( bub_part_location( i ) );
    }
}

int vehicle::damage( int p, int dmg, damage_type type, bool aimed, bool random_part )
{
    if( dmg < 1 ) {
        return dmg;
    }

    std::vector<int> pl = parts_at_relative( parts[p].mount, true );
    if( pl.empty() ) {
        // We ran out of non removed parts at this location already.
        return dmg;
    }

    if( !aimed ) {
        bool found_obs = false;
        for( auto &i : pl ) {
            if( part_flag( i, "OBSTACLE" ) &&
                ( !part_flag( i, "OPENABLE" ) || !parts[i].open ) ) {
                found_obs = true;
                break;
            }
        }

        if( !found_obs ) { // not aimed at this tile and no obstacle here -- fly through
            return dmg;
        }
    }

    int target_part = [&]() {
        if( random_part ) {
            if( part_info( p ).rotor_diameter() && one_in( 2 ) ) {
                return p;
            }
            return random_entry( pl );
        }
        return p;
    }
    ();

    // door motor mechanism is protected by closed doors
    if( part_flag( target_part, "DOOR_MOTOR" ) ) {
        // find the most strong openable that is not open
        int strongest_door_part = -1;
        int strongest_door_durability = INT_MIN;
        for( int part : pl ) {
            if( part_flag( part, "OPENABLE" ) && !parts[part].open ) {
                int door_durability = part_info( part ).durability;
                if( door_durability > strongest_door_durability ) {
                    strongest_door_part = part;
                    strongest_door_durability = door_durability;
                }
            }
        }

        // if we found a closed door, target it instead of the door_motor
        if( strongest_door_part != -1 ) {
            target_part = strongest_door_part;
        }
    }

    int damage_dealt;

    int armor_part = part_with_feature( p, "ARMOR", true );
    if( armor_part < 0 ) {
        // Not covered by armor -- damage part
        damage_dealt = damage_direct( target_part, dmg, type );
    } else {
        // Covered by armor -- hit both armor and part, but reduce damage by armor's reduction
        int protection = part_info( armor_part ).damage_reduction.type_resist( type );
        // Parts on roof aren't protected
        bool overhead = part_flag( target_part, "ROOF" ) || part_info( target_part ).location == "on_roof";
        // Calling damage_direct may remove the damaged part
        // completely, therefore the other index (target_part) becomes
        // wrong if target_part > armor_part.
        // Damaging the part with the higher index first is save,
        // as removing a part only changes indices after the
        // removed part.
        if( armor_part < target_part ) {
            damage_direct( target_part, overhead ? dmg : dmg - protection, type );
            damage_dealt = damage_direct( armor_part, dmg, type );
        } else {
            damage_dealt = damage_direct( armor_part, dmg, type );
            damage_direct( target_part, overhead ? dmg : dmg - protection, type );
        }
    }

    return damage_dealt;
}

void vehicle::damage_all( int dmg1, int dmg2, damage_type type, const tripoint_mnt_veh &impact )
{
    if( dmg2 < dmg1 ) {
        std::swap( dmg1, dmg2 );
    }
    if( dmg1 < 1 ) {
        return;
    }
    const float damage_min = std::abs( dmg1 );
    const float damage_max = std::abs( dmg2 );
    add_msg( m_debug, "Shock damage to vehicle of %.2f to %.2f", damage_min, damage_max );
    for( const vpart_reference &vp : get_all_parts() ) {
        const size_t p = vp.part_index();
        const vpart_info &shockpart = part_info( p );
        int distance = 1 + square_dist( vp.mount(), impact );
        if( distance > 1 ) {
            int net_dmg = rng( dmg1, dmg2 ) / ( distance * distance );
            if( shockpart.location != part_location_structure ||
                !shockpart.has_flag( "PROTRUSION" ) ) {
                if( shockpart.has_flag( "SHOCK_IMMUNE" ) ) {
                    net_dmg = 0;
                    continue;
                }
                int shock_absorber = part_with_feature( p, "SHOCK_ABSORBER", true );
                if( shock_absorber >= 0 ) {
                    net_dmg = std::max( 0.0f, net_dmg - ( parts[ shock_absorber ].info().bonus ) -
                                        shockpart.damage_reduction.type_resist( type ) );
                }
                if( shockpart.has_flag( "SHOCK_RESISTANT" ) ) {
                    float damage_resist = 0;
                    for( const int elem : all_parts_at_location( shockpart.location ) ) {
                        //Original intent was to find the frame that the part was mounted on and grab that objects resistance, but instead we will go with half the largest damage resist in the stack.
                        damage_resist = std::max( damage_resist, part_info( elem ).damage_reduction.type_resist( type ) );
                    }
                    damage_resist = damage_resist / 2;

                    add_msg( m_debug, "%1s inherited %.1f damage resistance!", shockpart.name(), damage_resist );
                    net_dmg = std::max( 0.0f, net_dmg - damage_resist );
                }
            }
            if( net_dmg > part_info( p ).damage_reduction.type_resist( type ) ) {
                damage_direct( p, net_dmg, type );
                add_msg( m_debug, _( "%1s took %.1f damage from shock." ), part_info( p ).name(), 1.0f * net_dmg );
            }

        }
    }
}

/**
 * Shifts all parts of the vehicle by the given amounts, and then shifts the
 * vehicle itself in the opposite direction. The end result is that the vehicle
 * appears to have not moved. Useful for re-zeroing a vehicle to ensure that a
 * (0, 0) part is always present.
 * @param delta How much to shift along each axis
 */
void vehicle::shift_parts( const tripoint_rel_veh &delta )
{
    for( auto &elem : parts ) {
        elem.mount -= delta;
    }

    decltype( labels ) new_labels;
    for( auto &l : labels ) {
        new_labels.insert( label( l - delta, l.text ) );
    }
    labels = new_labels;

    decltype( loot_zones ) new_zones;
    for( auto const &z : loot_zones ) {
        new_zones.emplace( z.first - delta, z.second );
    }
    loot_zones = new_zones;

    pivot_anchor[0] -= delta;
    refresh();
    //Need to also update the map after this
    g->m.reset_vehicle_cache( );
}

/**
 * Detect if the vehicle is currently missing a 0,0 part, and
 * adjust if necessary.
 * @return bool true if the shift was needed.
 */
bool vehicle::shift_if_needed()
{
    std::vector<int> vehicle_origin = parts_at_relative( tripoint_mnt_veh::zero(), true );
    if( !vehicle_origin.empty() && !parts[ vehicle_origin[ 0 ] ].removed ) {
        // Shifting is not needed.
        return false;
    }
    //Find a frame, any frame, to shift to
    for( const vpart_reference &vp : get_all_parts() ) {
        if( vp.info().location == "structure"
            && !vp.has_feature( "PROTRUSION" )
            && !vp.part().removed ) {
            shift_parts( vp.mount().reinterpret_as<tripoint_rel_veh>() );
            refresh();
            return true;
        }
    }
    // There are only parts with PROTRUSION left, choose one of them.
    for( const vpart_reference &vp : get_all_parts() ) {
        if( !vp.part().removed ) {
            shift_parts( vp.mount().reinterpret_as<tripoint_rel_veh>() );
            refresh();
            return true;
        }
    }
    return false;
}

int vehicle::break_off( int p, int dmg )
{
    /* Already-destroyed part - chance it could be torn off into pieces.
     * Chance increases with damage, and decreases with part max durability
     * (so lights, etc are easily removed; frames and plating not so much) */
    if( rng( 0, part_info( p ).durability / 10 ) >= dmg ) {
        return dmg;
    }
    const auto pos = bub_part_location( p );
    const auto scatter_parts = [&]( const vehicle_part & pt ) {
        for( detached_ptr<item> &piece : pt.pieces_for_broken_part() ) {
            // inside the loop, so each piece goes to a different place
            // TODO: this may spawn items behind a wall
            const auto where = random_entry( g->m.points_in_radius( pos, SCATTER_DISTANCE ) );
            // TODO: balance audit, ensure that less pieces are generated than one would need
            // to build the component (smash a vehicle box that took 10 lumps of steel,
            // find 12 steel lumps scattered after atom-smashing it with a tree trunk)
            if( !magic ) {
                g->m.add_item_or_charges( where, std::move( piece ) );
            }
        }
    };
    if( part_info( p ).location == part_location_structure ) {
        // For structural parts, remove other parts first
        std::vector<int> parts_in_square = parts_at_relative( parts[p].mount, true );
        for( int index = parts_in_square.size() - 1; index >= 0; index-- ) {
            // Ignore the frame being destroyed
            if( parts_in_square[index] == p ) {
                continue;
            }

            if( parts[ parts_in_square[ index ] ].is_broken() ) {
                // Tearing off a broken part - break it up
                if( g->u.sees( pos ) ) {
                    add_msg( m_bad, _( "The %s's %s breaks into pieces!" ), name,
                             parts[ parts_in_square[ index ] ].name() );
                }
                scatter_parts( parts[parts_in_square[index]] );
            } else {
                // Intact (but possibly damaged) part - remove it in one piece
                if( g->u.sees( pos ) ) {
                    add_msg( m_bad, _( "The %1$s's %2$s is torn off!" ), name,
                             parts[ parts_in_square[ index ] ].name() );
                }
                if( !magic ) {
                    g->m.add_item_or_charges( pos, parts[parts_in_square[index]].properties_to_item() );
                }
            }
            remove_part( parts_in_square[index] );
        }
        // After clearing the frame, remove it.
        if( g->u.sees( pos ) ) {
            add_msg( m_bad, _( "The %1$s's %2$s is destroyed!" ), name, parts[ p ].name() );
        }
        scatter_parts( parts[p] );
        remove_part( p );
        find_and_split_vehicles( p );
    } else {
        //Just break it off
        if( g->u.sees( pos ) ) {
            add_msg( m_bad, _( "The %1$s's %2$s is destroyed!" ), name, parts[ p ].name() );
        }

        scatter_parts( parts[p] );
        remove_part( p );
    }

    return dmg;
}

bool vehicle::explode_fuel( int p, damage_type type )
{
    const itype_id &ft = part_info( p ).fuel_type;
    item &fuel = *item::spawn_temporary( ft );
    if( !fuel.has_explosion_data() ) {
        return false;
    }
    fuel_explosion data = fuel.get_explosion_data();

    if( parts[ p ].is_broken() ) {
        leak_fuel( parts[ p ] );
    }

    int explosion_chance = type == DT_HEAT ? data.explosion_chance_hot : data.explosion_chance_cold;
    if( one_in( explosion_chance ) ) {
        g->events().send<event_type::fuel_tank_explodes>( name );
        const int pow = 120 * ( 1 - std::exp( data.explosion_factor / -5000 *
                                              ( parts[p].ammo_remaining() * data.fuel_size_factor ) ) );
        //debugmsg( "damage check dmg=%d pow=%d amount=%d", dmg, pow, parts[p].amount );

        explosion_handler::explosion( bub_part_location( p ), nullptr, pow, 0.7,
                                      data.fiery_explosion );
        mod_hp( parts[p], 0 - parts[ p ].hp(), DT_HEAT );
        parts[p].ammo_unset();
    }

    return true;
}

unsigned int vehicle::hits_to_destroy( int p, int dmg, damage_type type ) const
{
    const int armor_part = part_with_feature( p, VPFLAG_ARMOR, true );
    const bool is_armor_considered = !(
                                         armor_part < 0 ||
                                         part_flag( p, VPFLAG_ROOF ) ||
                                         part_info( p ).location == "on_roof"
                                     );


    const int part_hp = parts[p].hp();
    const int part_damage_reduction = part_info( p ).damage_reduction.type_resist( type );
    const int part_threshold_damage = std::clamp( part_info( p ).durability / 10, 1, 20 );

    const int part_dmg_without_armor = dmg - part_damage_reduction;

    // Easiest case: part will not get destroyed, period
    if( part_dmg_without_armor <= 0 ||
        ( type != DT_TRUE &&
          part_dmg_without_armor < part_threshold_damage ) ) {
        return 0;
    }

    // Easy case: part unprotected and will be destroyed
    if( !is_armor_considered ) {
        const int part_htd = part_hp / part_dmg_without_armor +
                             ( part_hp % part_dmg_without_armor > 0 );
        return part_htd;
    }

    const int armor_hp = parts[armor_part].hp();
    const int armor_damage_reduction = part_info( armor_part ).damage_reduction.type_resist( type );
    const int armor_threshold_damage = std::clamp( part_info( armor_part ).durability / 10, 1, 20 );
    const int armor_dmg = dmg - armor_damage_reduction;

    // First, determine how long armor will remain for
    const int armor_htd = armor_dmg <= 0 || ( type != DT_TRUE && armor_dmg < armor_threshold_damage ) ?
                          INT_MAX :
                          armor_hp / armor_dmg + ( armor_hp % armor_dmg > 0 );

    const int part_dmg_with_armor = part_dmg_without_armor - armor_damage_reduction;
    // How long will the part remain with armor unbroken?
    const int part_htd_with_armor = ( part_dmg_with_armor <= 0 ||
                                      ( type != DT_TRUE && part_dmg_with_armor < part_threshold_damage ) ) ?
                                    INT_MAX :
                                    part_hp / part_dmg_with_armor + ( part_hp % part_dmg_with_armor  > 0 );

    // Part gets destroyed before armor does
    if( part_htd_with_armor <= armor_htd ) {
        return part_htd_with_armor;
    }

    // Armor gets destroyed before part does
    const int part_hp_after_armor = part_hp - armor_htd * std::max( part_dmg_with_armor, 0 );
    const int part_htd_after_armor = part_hp_after_armor / part_dmg_without_armor +
                                     ( part_hp_after_armor % part_dmg_without_armor  > 0 );

    return armor_htd + part_htd_after_armor;
}

int vehicle::damage_direct( int p, int dmg, damage_type type )
{
    map &here = get_map();
    // Make sure p is within range and hasn't been removed already
    if( ( static_cast<size_t>( p ) >= parts.size() ) || parts[p].removed ||
        !here.inbounds( bub_part_location( p ) ) ) {
        return dmg;
    }
    // If auto-driving and damage happens, bail out
    if( is_autodriving ) {
        stop_autodriving();
    }
    here.set_memory_seen_cache_dirty( bub_part_location( p ) );
    if( parts[p].is_broken() ) {
        return break_off( p, dmg );
    }

    int tsh = std::min( 20, part_info( p ).durability / 10 );
    if( dmg < tsh && type != DT_TRUE ) {
        if( type == DT_HEAT && parts[p].is_fuel_store() ) {
            explode_fuel( p, type );
        }

        return 0;
    }

    dmg -= std::min<int>( dmg, part_info( p ).damage_reduction.type_resist( type ) );
    int dres = dmg - parts[p].hp();
    if( mod_hp( parts[ p ], 0 - dmg, type ) ) {
        insides_dirty = true;
        pivot_dirty = true;

        // destroyed parts lose any contained fuels, battery charges or ammo
        leak_fuel( parts [ p ] );

        for( auto &e : parts[p].items.clear() ) {
            g->m.add_item_or_charges( bub_part_location( p ), std::move( e ) );
        }

        invalidate_mass();
        coeff_air_changed = true;

        // refresh cache in case the broken part has changed the status
        refresh();
    }

    if( parts[p].is_fuel_store() ) {
        explode_fuel( p, type );
    } else if( parts[ p ].is_broken() && part_flag( p, "UNMOUNT_ON_DAMAGE" ) ) {
        here.spawn_item( bub_part_location( p ), part_info( p ).item, 1, 0, calendar::turn );
        monster *mon = get_pet( p );
        if( mon != nullptr && mon->has_effect( effect_harnessed ) ) {
            mon->remove_effect( effect_harnessed );
        }
        if( part_flag( p, "TOW_CABLE" ) ) {
            invalidate_towing( true );
        } else {
            remove_part( p );
        }
    }

    return std::max( dres, 0 );
}

void vehicle::leak_fuel( vehicle_part &pt )
{
    // only liquid fuels from non-empty tanks can leak out onto map tiles
    if( !pt.is_tank() || pt.ammo_remaining() <= 0 ) {
        return;
    }

    // leak in random directions but prefer closest tiles and avoid walls or other obstacles
    std::vector<tripoint_bub_ms> tiles = closest_points_first( bub_part_location( pt ), 1 );
    tiles.erase( std::remove_if( tiles.begin(), tiles.end(), []( const tripoint_bub_ms & e ) {
        return !g->m.passable( e );
    } ), tiles.end() );

    // leak up to 1/3 of remaining fuel per iteration and continue until the part is empty
    const itype *fuel = &*pt.ammo_current();
    while( !tiles.empty() && pt.ammo_remaining() ) {
        int qty = pt.ammo_consume( rng( 0, std::max( pt.ammo_remaining() / 3, 1 ) ),
                                   bub_part_location( pt ) );
        if( qty > 0 ) {
            g->m.add_item_or_charges( random_entry( tiles ), item::spawn( fuel, calendar::turn, qty ) );
        }
    }

    pt.ammo_unset();
}

std::map<itype_id, int> vehicle::fuels_left() const
{
    std::map<itype_id, int> result;
    for( const auto &p : parts ) {
        if( p.is_fuel_store() && !p.ammo_current().is_null() ) {
            result[ p.ammo_current() ] += p.ammo_remaining();
        }
    }
    return result;
}
