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

units::volume vehicle::stored_volume( const int part ) const
{
    return get_items( part ).stored_volume();
}

units::volume vehicle::max_volume( const int part ) const
{
    return get_items( part ).max_volume();
}

units::volume vehicle::free_volume( const int part ) const
{
    return get_items( part ).free_volume();
}

void vehicle::make_inactive( item &target )
{
    auto cargo_parts = get_parts_at( tripoint_bub_ms( target.position() ), "CARGO",
                                     part_status_flag::any );
    if( cargo_parts.empty() ) {
        return;
    }
    active_items.remove( &target );
}

void vehicle::make_active( item &target )
{
    if( !target.needs_processing() ) {
        return;
    }
    auto cargo_parts = get_parts_at( tripoint_bub_ms( target.position() ), "CARGO",
                                     part_status_flag::any );
    if( cargo_parts.empty() ) {
        return;
    }
    active_items.add( target );
}

detached_ptr<item> vehicle::add_charges( int part, detached_ptr<item> &&itm )
{
    if( !itm->count_by_charges() ) {
        debugmsg( "Add charges was called for an item not counted by charges!" );
        return std::move( itm );
    }
    const int amount = get_items( part ).amount_can_fit( *itm );
    if( amount == 0 ) {
        return std::move( itm );
    }

    detached_ptr<item> itm_copy = item::spawn( *itm );
    itm_copy->charges = amount;
    itm->charges -= amount;
    detached_ptr<item> remaining = add_item( part, std::move( itm_copy ) );
    if( remaining ) {
        itm->charges += remaining->charges;
    }
    return itm->charges > 0 ? std::move( itm ) : detached_ptr<item>();
}

detached_ptr<item> vehicle::add_item( vehicle_part &pt, detached_ptr<item> &&obj )
{
    int idx = index_of_part( &pt );
    if( idx < 0 ) {
        debugmsg( "Tried to add item to invalid part" );
        return std::move( obj );
    }
    return add_item( idx, std::move( obj ) );
}

detached_ptr<item> vehicle::add_item( int part, detached_ptr<item> &&itm )
{
    if( part < 0 || part >= static_cast<int>( parts.size() ) ) {
        debugmsg( "int part (%d) is out of range", part );
        return std::move( itm );
    }
    // const int max_weight = ?! // TODO: weight limit, calculation per vpart & vehicle stats, not a hard user limit.
    // add creaking sounds and damage to overloaded vpart, outright break it past a certain point, or when hitting bumps etc
    vehicle_part &p = parts[ part ];
    if( p.is_broken() ) {
        return std::move( itm );
    }

    if( p.base->is_gun() ) {
        if( !itm->is_ammo() || !p.base->ammo_types().contains( itm->ammo_type() ) ) {
            return std::move( itm );
        }
    }
    bool charge = itm->count_by_charges();
    vehicle_stack istack = get_items( part );
    const int to_move = istack.amount_can_fit( *itm );
    if( to_move == 0 || ( charge && to_move < itm->charges ) ) {
        return std::move( itm ); // @add_charges should be used in the latter case
    }
    if( charge ) {
        item *here = istack.stacks_with( *itm );
        if( here ) {
            invalidate_mass();
            if( !here->merge_charges( std::move( itm ) ) ) {
                // NOLINTNEXTLINE(bugprone-use-after-move)
                return std::move( itm );
            } else {
                return detached_ptr<item>();
            }
        }
    }


    if( itm->is_bucket_nonempty() ) {
        itm->contents.spill_contents( bub_part_location( part ) );
    }
    if( itm->needs_processing() ) {
        active_items.add( *itm );
    }
    p.items.push_back( std::move( itm ) );

    invalidate_mass();
    return detached_ptr<item>();
}

detached_ptr<item> vehicle::remove_item( int part, item *it )
{
    const location_vector<item> &veh_items = parts[part].items;

    const location_vector<item>::const_iterator iter = std::find_if( veh_items.begin(),
    veh_items.end(), [&it]( const item * const & item ) {
        return it == item;
    } );

    if( iter == veh_items.end() ) {
        return detached_ptr<item>();
    }
    detached_ptr<item> det;
    remove_item( part, iter, &det );
    return det;
}

vehicle_stack::iterator vehicle::remove_item( int part, vehicle_stack::const_iterator it,
        detached_ptr<item> *ret )
{
    // remove from the active items cache (if it isn't there does nothing)
    active_items.remove( *it );

    vehicle_stack::iterator iter = parts[part].items.erase( std::move( it ), ret );
    invalidate_mass();
    return iter;
}

vehicle_stack vehicle::get_items( const int part )
{
    return vehicle_stack( &parts[part].items, bub_part_location( part ), this, part );
}

vehicle_stack vehicle::get_items( const int part ) const
{
    // HACK: callers could modify items through this
    // TODO: a const version of vehicle_stack is needed
    return const_cast<vehicle *>( this )->get_items( part );
}

void vehicle::place_spawn_items()
{
    if( !type.is_valid() ) {
        return;
    }

    std::ranges::for_each( type->parts, [this]( const auto & pt ) {
        if( !pt.with_ammo ) {
            return;
        }

        auto turret = part_with_feature( pt.pos, "TURRET", true );
        if( turret < 0 ) {
            return;
        }

        const auto global_rate = get_option<float>( "ITEM_SPAWNRATE" );
        const auto ammo_rate = get_option<float>( "SPAWN_RATE_ammo" );
        const auto combined_rate = std::min( global_rate * ammo_rate, 1.0f );
        const auto scaled_chance = static_cast<int>( pt.with_ammo * combined_rate );

        if( x_in_y( scaled_chance, 100 ) ) {
            parts[ turret ].ammo_set( random_entry( pt.ammo_types ), rng( pt.ammo_qty.first,
                                      pt.ammo_qty.second ) );
        }
    } );

    std::ranges::for_each( type.obj().item_spawns, [this]( const auto & spawn ) {
        if( rng( 1, 100 ) > spawn.chance ) {
            return;
        }

        auto part = part_with_feature( spawn.pos, "CARGO", false );
        if( part < 0 ) {
            debugmsg( "No CARGO parts at (%d, %d) of %s!", spawn.pos.x(), spawn.pos.y(), name );
            return;
        }

        auto broken = parts[ part ].is_broken();
        if( broken && one_in( 2 ) ) {
            return;
        }

        std::vector<detached_ptr<item>> created;
        created.reserve( spawn.item_ids.size() );
        std::ranges::transform( spawn.item_ids, std::back_inserter( created ),
        []( const itype_id & e ) {
            return item::in_its_container( item::spawn( e ) );
        } );

        std::ranges::for_each( spawn.item_groups, [&created]( const item_group_id & e ) {
            auto group_items = item_group::items_from( e, calendar::start_of_cataclysm );
            std::ranges::move( group_items, std::back_inserter( created ) );
        } );

        const auto global_spawn_rate = get_option<float>( "ITEM_SPAWNRATE" );

        std::erase_if( created, [broken, global_spawn_rate]( detached_ptr<item> &e ) {
            if( e->is_null() ) {
                return true;
            }
            if( broken && e->mod_damage( rng( 1, e->max_damage() ) ) ) {
                return true;
            }

            const auto category_rate = g->m.item_category_spawn_rate( *e );
            const auto final_rate = std::min( global_spawn_rate * category_rate, 1.0f );

            return rng_float( 0, 1 ) >= final_rate;
        } );

        std::ranges::for_each( created, [this, &spawn, part]( detached_ptr<item> &e ) {
            if( e->is_tool() || e->is_gun() || e->is_magazine() ) {
                auto spawn_ammo = rng( 0, 99 ) < spawn.with_ammo && e->ammo_remaining() == 0;
                auto spawn_mag  = rng( 0, 99 ) < spawn.with_magazine && !e->magazine_integral() &&
                                  !e->magazine_current();

                if( spawn_mag ) {
                    e->put_in( item::spawn( e->magazine_default(), e->birthday() ) );
                }
                if( spawn_ammo ) {
                    e->ammo_set( e->ammo_default() );
                }
            }
            add_item( part, std::move( e ) );
        } );
    } );
}


void vehicle::dump_items_from_part( const size_t index )
{
    vehicle_part &vp = parts[ index ];
    for( detached_ptr<item> &e : vp.items.clear() ) {
        g->m.add_item_or_charges( bub_part_location( vp ), std::move( e ) );
    }
}

bool vehicle::decrement_summon_timer()
{
    if( !summon_time_limit ) {
        return false;
    }
    if( *summon_time_limit <= 0_turns ) {
        for( const vpart_reference &vp : get_all_parts() ) {
            const size_t p = vp.part_index();
            dump_items_from_part( p );
        }
        if( g->u.sees( bub_ms_location() ) ) {
            add_msg( m_info, _( "Your %s winks out of existence." ), name );
        }
        g->m.destroy_vehicle( this );
        return true;
    } else {
        *summon_time_limit -= 1_turns;
    }
    return false;
}

void vehicle::suspend_refresh()
{
    // disable refresh and cache recalculation
    no_refresh = true;
    mass_dirty = false;
    mass_center_precalc_dirty = false;
    mass_center_no_precalc_dirty = false;
    coeff_rolling_dirty = false;
    coeff_air_dirty = false;
    coeff_water_dirty = false;
    coeff_air_changed = false;
}

void vehicle::enable_refresh()
{
    // force all caches to recalculate
    no_refresh = false;
    mass_dirty = true;
    mass_center_precalc_dirty = true;
    mass_center_no_precalc_dirty = true;
    coeff_rolling_dirty = true;
    coeff_air_dirty = true;
    coeff_water_dirty = true;
    coeff_air_changed = true;
    // Run the locations hack once here, covering all parts installed while suspended.
    // During suspend, install_part() skips refresh_locations_hack() to avoid O(N²).
    refresh_locations_hack();
    refresh();
}

/**
 * Refreshes all caches and refinds all parts. Used after the vehicle has had a part added or removed.
 * Makes indices of different part types so they're easy to find. Also calculates power drain.
 */
void vehicle::refresh()
{
    if( no_refresh ) {
        return;
    }

    alternators.clear();
    engines.clear();
    reactors.clear();
    solar_panels.clear();
    wind_turbines.clear();
    sails.clear();
    water_wheels.clear();
    funnels.clear();
    emitters.clear();
    relative_parts.clear();
    loose_parts.clear();
    wheelcache.clear();
    rail_wheelcache.clear();
    rotors.clear();
    wings.clear();
    propellers.clear();
    droppers.clear();
    balloons.clear();
    converters.clear();
    tanks.clear();
    steering.clear();
    speciality.clear();
    floating.clear();
    alternator_load = 0;
    extra_drag = 0;
    rail_profile.clear();
    has_autoloaders = false;
    has_cargo_recharge = false;
    has_portal_tap_parts = false;

    // Used to sort part list so it displays properly when examining
    struct sort_veh_part_vector {
        vehicle *veh;
        bool operator()( const int p1, const int p2 ) {
            return veh->part_info( p1 ).list_order < veh->part_info( p2 ).list_order;
        }
    } svpv = { this };

    mount_min.x() = 123;
    mount_min.y() = 123;
    mount_max.x() = -123;
    mount_max.y() = -123;

    bool refresh_done = false;

    // Main loop over all vehicle parts.
    for( const vpart_reference &vp : get_all_parts() ) {
        const size_t p = vp.part_index();
        const vpart_info &vpi = vp.info();
        if( vp.part().removed ) {
            continue;
        }
        refresh_done = true;

        // Build map of point -> all parts in that point
        const auto pt = vp.mount();
        mount_min.x() = std::min( mount_min.x(), pt.x() );
        mount_min.y() = std::min( mount_min.y(), pt.y() );
        mount_max.x() = std::max( mount_max.x(), pt.x() );
        mount_max.y() = std::max( mount_max.y(), pt.y() );

        // This will keep the parts at point pt sorted
        std::vector<int>::iterator vii = std::lower_bound( relative_parts[pt].begin(),
                                         relative_parts[pt].end(),
                                         static_cast<int>( p ), svpv );
        relative_parts[pt].insert( vii, p );

        if( vpi.has_flag( VPFLAG_FLOATS ) ) {
            floating.push_back( p );
        }

        if( vp.part().is_unavailable() ) {
            continue;
        }
        if( vpi.has_flag( VPFLAG_ALTERNATOR ) ) {
            alternators.push_back( p );
        }
        if( vpi.has_flag( VPFLAG_ENGINE ) ) {
            engines.push_back( p );
        }
        if( vp.part().is_reactor() || vp.part().is_perpetual_power_source() ) {
            reactors.push_back( p );
        }
        if( vpi.has_flag( VPFLAG_SOLAR_PANEL ) ) {
            solar_panels.push_back( p );
        }
        if( vpi.has_flag( VPFLAG_ROTOR ) ) {
            rotors.push_back( p );
        }
        if( vpi.has_flag( VPFLAG_PROPELLER ) ) {
            propellers.push_back( p );
        }
        if( vpi.has_flag( VPFLAG_WING ) ) {
            wings.push_back( p );
        }
        if( vpi.has_flag( VPFLAG_BALLOON ) ) {
            balloons.push_back( p );
        }
        if( vpi.has_flag( "CONVERTER" ) ) {
            converters.push_back( p );
        }
        if( vpi.has_flag( "FLUIDTANK" ) ) {
            tanks.push_back( p );
        }
        if( vpi.has_flag( VPFLAG_DROPPER ) ) {
            droppers.push_back( p );
        }
        if( vpi.has_flag( "WIND_TURBINE" ) ) {
            wind_turbines.push_back( p );
        }
        if( vpi.has_flag( "WIND_POWERED" ) ) {
            sails.push_back( p );
        }
        if( vpi.has_flag( "WATER_WHEEL" ) ) {
            water_wheels.push_back( p );
        }
        if( vpi.has_flag( "FUNNEL" ) ) {
            funnels.push_back( p );
        }
        if( vpi.has_flag( "UNMOUNT_ON_MOVE" ) ) {
            loose_parts.push_back( p );
        }
        if( vpi.has_flag( "EMITTER" ) ) {
            emitters.push_back( p );
        }
        if( vpi.has_flag( "AUTOLOADER" ) ) {
            has_autoloaders = true;
        }
        if( vpi.has_flag( VPFLAG_RECHARGE ) ) {
            has_cargo_recharge = true;
        }
        if( vpi.has_flag( "POWER_DRAW_LINKED_PORTAL" ) ) {
            has_portal_tap_parts = true;
        }
        if( vpi.has_flag( VPFLAG_WHEEL ) ) {
            wheelcache.push_back( p );
        }
        if( vpi.has_flag( VPFLAG_RAIL ) ) {
            rail_wheelcache.push_back( p );

            const int rail_pos = vp.mount().y();
            const auto it = std::find( rail_profile.cbegin(), rail_profile.cend(), rail_pos );
            if( it == rail_profile.cend() ) {
                rail_profile.push_back( rail_pos );
            }
        }
        if( ( vpi.has_flag( "STEERABLE" ) && part_with_feature( pt, "STEERABLE", true ) != -1 ) ||
            vpi.has_flag( "TRACKED" ) ) {
            // TRACKED contributes to steering effectiveness but
            //  (a) doesn't count as a steering axle for install difficulty
            //  (b) still contributes to drag for the center of steering calculation
            steering.push_back( p );
        }
        if( vpi.has_flag( "SECURITY" ) ) {
            speciality.push_back( p );
        }
        if( vp.part().enabled && vpi.has_flag( "EXTRA_DRAG" ) ) {
            extra_drag += vpi.power;
        }
        if( vpi.has_flag( "EXTRA_DRAG" ) && ( vpi.has_flag( "WIND_TURBINE" ) ||
                                              vpi.has_flag( "WATER_WHEEL" ) ) ) {
            extra_drag += vpi.power;
        }
        if( vpi.has_flag( "POWERED_BY_ENGINE" ) ) {
            extra_drag += vpi.power;
        }
        if( camera_on && vpi.has_flag( "CAMERA" ) ) {
            vp.part().enabled = true;
        } else if( !camera_on && vpi.has_flag( "CAMERA" ) ) {
            vp.part().enabled = false;
        }
        if( vpi.has_flag( "TURRET" ) && !has_part( bub_part_location( vp.part() ), "TURRET_CONTROLS" ) ) {
            vp.part().enabled = false;
        }
    }

    front_left.x() = mount_max.x();
    front_left.y() = mount_min.y();
    front_right = mount_max;

    if( !refresh_done ) {
        mount_min = mount_max = tripoint_mnt_veh::zero();
    }

    refresh_position();

    check_environmental_effects = true;
    insides_dirty = true;
    zones_dirty = true;
    invalidate_mass();
}

void vehicle::refresh_position()
{
    if( !parts.empty() ) {
        precalc_mounts( 0, pivot_rotation[0], pivot_anchor[0] );
        if( attached ) {
            adjust_zlevel();
            shift_zlevel();
        }
    }
}

tripoint_mnt_veh vehicle::pivot_point() const
{
    if( pivot_dirty ) {
    refresh_pivot();
    }

    return pivot_cache;
}

void vehicle::refresh_pivot() const
{
    // Const method, but messes with mutable fields
    pivot_dirty = false;

    if( wheelcache.empty() || !valid_wheel_config() ) {
        // No usable wheels, use CoM (dragging)
        pivot_cache = local_center_of_mass();
        return;
    }

    // The model here is:
    //
    //  We are trying to rotate around some point (xc,yc)
    //  This produces a friction force / moment from each wheel resisting the
    //  rotation. We want to find the point that minimizes that resistance.
    //
    //  For a given wheel w at (xw,yw), find:
    //   weight(w): a scaling factor for the friction force based on wheel
    //              size, brokenness, steerability/orientation
    //   center_dist: the distance from (xw,yw) to (xc,yc)
    //   centerline_angle: the angle between the X axis and a line through
    //                     (xw,yw) and (xc,yc)
    //
    //  Decompose the force into two components, assuming that the wheel is
    //  aligned along the X axis and we want to apply different weightings to
    //  the in-line vs perpendicular parts of the force:
    //
    //   Resistance force in line with the wheel (X axis)
    //    Fi = weightI(w) * center_dist * sin(centerline_angle)
    //   Resistance force perpendicular to the wheel (Y axis):
    //    Fp = weightP(w) * center_dist * cos(centerline_angle);
    //
    //  Then find the moment that these two forces would apply around (xc,yc)
    //    moment(w) = center_dist * cos(centerline_angle) * Fi +
    //                center_dist * sin(centerline_angle) * Fp
    //
    //  Note that:
    //    cos(centerline_angle) = (xw-xc) / center_dist
    //    sin(centerline_angle) = (yw-yc) / center_dist
    // -> moment(w) = weightP(w)*(xw-xc)^2 + weightI(w)*(yw-yc)^2
    //              = weightP(w)*xc^2 - 2*weightP(w)*xc*xw + weightP(w)*xw^2 +
    //                weightI(w)*yc^2 - 2*weightI(w)*yc*yw + weightI(w)*yw^2
    //
    //  which happily means that the X and Y axes can be handled independently.
    //  We want to minimize sum(moment(w)) due to wheels w=0,1,..., which
    //  occurs when:
    //
    //    sum( 2*xc*weightP(w) - 2*weightP(w)*xw ) = 0
    //     -> xc = (weightP(0)*x0 + weightP(1)*x1 + ...) /
    //             (weightP(0) + weightP(1) + ...)
    //    sum( 2*yc*weightI(w) - 2*weightI(w)*yw ) = 0
    //     -> yc = (weightI(0)*y0 + weightI(1)*y1 + ...) /
    //             (weightI(0) + weightI(1) + ...)
    //
    // so it turns into a fairly simple weighted average of the wheel positions.

    float xc_numerator = 0;
    float xc_denominator = 0;
    float yc_numerator = 0;
    float yc_denominator = 0;

    for( int p : wheelcache ) {
        const auto &wheel = parts[p];

        // TODO: load on tire?
        int contact_area = wheel.wheel_area();
        float weight_i;  // weighting for the in-line part
        float weight_p;  // weighting for the perpendicular part
        if( wheel.is_broken() ) {
            // broken wheels don't roll on either axis
            weight_i = contact_area * 2.0;
            weight_p = contact_area * 2.0;
        } else if( part_with_feature( wheel.mount, "STEERABLE", true ) != -1 ) {
            // Unbroken steerable wheels can handle motion on both axes
            // (but roll a little more easily inline)
            weight_i = contact_area * 0.1;
            weight_p = contact_area * 0.2;
        } else {
            // Regular wheels resist perpendicular motion
            weight_i = contact_area * 0.1;
            weight_p = contact_area;
        }

        xc_numerator += weight_p * wheel.mount.x();
        yc_numerator += weight_i * wheel.mount.y();
        xc_denominator += weight_p;
        yc_denominator += weight_i;
    }

    if( xc_denominator < 0.1 || yc_denominator < 0.1 ) {
        debugmsg( "vehicle::refresh_pivot had a bad weight: xc=%.3f/%.3f yc=%.3f/%.3f",
                  xc_numerator, xc_denominator, yc_numerator, yc_denominator );
        pivot_cache = local_center_of_mass();
    } else {
        pivot_cache.x() = std::round( xc_numerator / xc_denominator );
        pivot_cache.y() = std::round( yc_numerator / yc_denominator );
    }
}

static int get_turn_from_angle( const units::angle angle, const tripoint_abs_ms &vehpos,
                                const tripoint_abs_ms &target, bool reverse = false )
{
    if( angle > 10.0_degrees && angle <= 45.0_degrees ) {
        return reverse ? 4 : 1;
    } else if( angle > 45.0_degrees && angle <= 90.0_degrees ) {
        return 3;
    } else if( angle > 90.0_degrees && angle < 180.0_degrees ) {
        return reverse ? 1 : 4;
    } else if( angle < -10.0_degrees && angle >= -45.0_degrees ) {
        return reverse ? -4 : -1;
    } else if( angle < -45.0_degrees && angle >= -90.0_degrees ) {
        return -3;
    } else if( angle < -90.0_degrees && angle > -180.0_degrees ) {
        return reverse ? -1 : -4;
    } else if( ( angle == 180_degrees || angle == -180_degrees ) && vehpos == target ) {
        return 0;
    }
    return 0;
}

void vehicle::do_towing_move()
{
    if( !no_towing_slack() || velocity <= 0 ) {
        return;
    }
    bool invalidate = false;
    if( !tow_data.get_towed() ) {
        debugmsg( "tried to do towing move, but no towed vehicle!" );
        invalidate = true;
    }
    const int tow_index = get_tow_part();
    if( tow_index == -1 ) {
        debugmsg( "tried to do towing move, but no tow part" );
        invalidate = true;
    }
    vehicle *towed_veh = tow_data.get_towed();
    if( !towed_veh ) {
        debugmsg( "tried to do towing move, but towed vehicle dosnt exist." );
        invalidate_towing();
        return;
    }
    const int other_tow_index = towed_veh->get_tow_part();
    if( other_tow_index == -1 ) {
        debugmsg( "tried to do towing move but towed vehicle has no towing part" );
        invalidate = true;
    }
    if( towed_veh->bub_ms_location().z() != bub_ms_location().z() ) {
        // how the hellicopter did this happen?
        // yes, this can happen when towing over a bridge (see #47293)
        invalidate = true;
        add_msg( m_info, _( "A towing cable snaps off of %s." ), tow_data.get_towed()->disp_name() );
    }
    if( invalidate ) {
        invalidate_towing( true );
        return;
    }
    const auto tower_tow_point = g->m.bub_to_abs( bub_part_location( tow_index ) );
    const auto towed_tow_point = g->m.bub_to_abs( towed_veh->bub_part_location( other_tow_index ) );
    // same as above, but where the pulling vehicle is pulling from
    units::angle towing_veh_angle = towed_veh->get_angle_from_targ( tower_tow_point );
    const bool reverse = towed_veh->tow_data.tow_direction == TOW_BACK;
    int accel_y = 0;
    auto vehpos = abs_ms_location();
    int turn_x = get_turn_from_angle( towing_veh_angle, vehpos, tower_tow_point, reverse );
    if( rl_dist( towed_tow_point, tower_tow_point ) < 6 ) {
        accel_y = reverse ? -1 : 1;
    }
    if( towed_veh->velocity <= velocity && rl_dist( towed_tow_point, tower_tow_point ) >= 7 ) {
        accel_y = reverse ? 1 : -1;
    }
    if( rl_dist( towed_tow_point, tower_tow_point ) >= 12 ) {
        towed_veh->velocity = velocity * 1.8;
        if( reverse ) {
            towed_veh->velocity = -towed_veh->velocity;
        }
    } else {
        towed_veh->velocity = reverse ? -velocity : velocity;
    }
    if( towed_veh->tow_data.tow_direction == TOW_FRONT ) {
        towed_veh->selfdrive( point( turn_x, accel_y ) );
    } else if( towed_veh->tow_data.tow_direction == TOW_BACK ) {
        accel_y = 10;
        towed_veh->selfdrive( point( turn_x, accel_y ) );
    } else {
        towed_veh->skidding = true;
        std::vector<tripoint_bub_ms> lineto = line_to( g->m.abs_to_bub( towed_tow_point ),
                                              g->m.abs_to_bub( tower_tow_point ) );
        tripoint_bub_ms nearby_destination;
        if( lineto.size() >= 2 ) {
            nearby_destination = lineto[1];
        } else {
            nearby_destination = g->m.abs_to_bub( tower_tow_point );
        }
        const int destination_delta_x = g->m.abs_to_bub( tower_tow_point ).x() - nearby_destination.x();
        const int destination_delta_y = g->m.abs_to_bub( tower_tow_point ).y() - nearby_destination.y();
        const int destination_delta_z = towed_veh->bub_ms_location().z();
        const tripoint_rel_ms move_destination( clamp( destination_delta_x, -1, 1 ),
                                                clamp( destination_delta_y, -1, 1 ),
                                                clamp( destination_delta_z, -1, 1 ) );
        g->m.move_vehicle( *towed_veh, move_destination, towed_veh->face );
        towed_veh->move = tileray( point_rel_ms( destination_delta_x, destination_delta_y ) );
    }

}

bool vehicle::is_external_part( const tripoint_bub_ms &part_pt ) const
{
for( const auto &elem : g->m.points_in_radius( part_pt, 1 ) ) {
    const optional_vpart_position vp = g->m.veh_at( elem );
        if( !vp ) {
            return true;
        }
        if( vp && &vp->vehicle() != this ) {
            return true;
        }
    }
    return false;
}

bool vehicle::is_towing() const
{
    bool ret = false;
    if( !tow_data.get_towed() ) {
        return ret;
    } else {
        if( !tow_data.get_towed()->tow_data.get_towed_by() ) {
            debugmsg( "vehicle %s is towing, but the towed vehicle has no tower defined", name );
            return ret;
        }
        ret = true;
    }
    return ret;
}

bool vehicle::is_towed() const
{
    bool ret = false;
    if( !tow_data.get_towed_by() ) {
        return ret;
    } else {
        if( !tow_data.get_towed_by()->tow_data.get_towed() ) {
            debugmsg( "vehicle %s is marked as towed, but the tower vehicle has no towed defined", name );
            return ret;
        }
        ret = true;
    }
    return ret;
}

int vehicle::get_tow_part() const
{
for( const vpart_reference &vp : get_all_parts() ) {
    const size_t p = vp.part_index();
        if( vp.part().removed ) {
            continue;
        }

        if( part_with_feature( p, "TOW_CABLE", true ) >= 0 && vp.part().is_available() ) {
            return p;
        }
    }
    return -1;
}

bool vehicle::has_tow_attached() const
{
    bool ret = false;
    for( const vpart_reference &vp : get_all_parts() ) {
        const size_t p = vp.part_index();
        if( vp.part().removed ) {
            continue;
        }

        if( part_with_feature( p, "TOW_CABLE", true ) >= 0 && vp.part().is_available() ) {
            ret = true;
            break;
        }
    }
    return ret;
}

void vehicle::set_tow_directions()
{
    const int length = mount_max.x() - mount_min.x() + 1;
    const auto mount_of_tow = parts[get_tow_part()].mount;
    // Fairly certain including the z axis here does nothing for now, but might be useful in the future
    const auto normalized_tow_mount = tripoint_mnt_veh( std::abs( mount_of_tow.x() - mount_min.x() ),
                                      std::abs( mount_of_tow.y() - mount_min.y() ),
                                      std::abs( mount_of_tow.z() - mount_min.z() ) );
    if( length >= 3 ) {
        const int trisect = length / 3;
        if( normalized_tow_mount.x() <= trisect ) {
            tow_data.tow_direction = TOW_BACK;
        } else if( normalized_tow_mount.x() > trisect && normalized_tow_mount.x() <= trisect * 2 ) {
            tow_data.tow_direction = TOW_SIDE;
        } else {
            tow_data.tow_direction = TOW_FRONT;
        }
    } else {
        // its a small vehicle, no danger if it flips around.
        tow_data.tow_direction = TOW_FRONT;
    }
}

bool towing_data::set_towing( vehicle *tower_veh, vehicle *towed_veh )
{
    if( !towed_veh || !tower_veh ) {
        return false;
    }
    towed_veh->tow_data.towed_by = tower_veh;
    tower_veh->tow_data.towing = towed_veh;
    tower_veh->set_tow_directions();
    towed_veh->set_tow_directions();
    return true;
}

void vehicle::invalidate_towing( bool first_vehicle )
{
    if( !is_towing() && !is_towed() ) {
        return;
    }
    vehicle *other_veh = nullptr;
    if( is_towing() ) {
        other_veh = tow_data.get_towed();
    } else if( is_towed() ) {
        other_veh = tow_data.get_towed_by();
    }
    if( other_veh && first_vehicle ) {
        other_veh->invalidate_towing();
    }
    for( const vpart_reference &vp : get_all_parts() ) {
        const size_t p = vp.part_index();
        if( vp.part().removed ) {
            continue;
        }

        if( part_with_feature( p, "TOW_CABLE", true ) >= 0 ) {
            if( first_vehicle ) {
                vehicle_part *part = &parts[part_with_feature( p, "TOW_CABLE", true )];
                g->m.add_item_or_charges( bub_part_location( *part ),  part->properties_to_item() );
            }
            //TODO!: check part removal in general, what happens to their base?
            remove_part( part_with_feature( p, "TOW_CABLE", true ) );
            break;
        }
    }
    tow_data.clear_towing();
}

// to be called on the towed vehicle
bool vehicle::tow_cable_too_far() const
{
    if( !tow_data.get_towed_by() ) {
    debugmsg( "checking tow cable length on a vehicle that has no towing vehicle" );
        return false;
    }
    int index = get_tow_part();
    if( index == -1 ) {
    debugmsg( "towing data exists but no towing part" );
        return false;
    }
    auto towing_point = g->m.bub_to_abs( bub_part_location( index ) );
    if( !tow_data.get_towed_by()->tow_data.get_towed() ) {
    debugmsg( "vehicle %s has data for a towing vehicle, but that towing vehicle does not have %s listed as towed",
              disp_name(), disp_name() );
        return false;
    }
    int other_index = tow_data.get_towed_by()->get_tow_part();
    if( other_index == -1 ) {
    debugmsg( "towing data exists but no towing part" );
        return false;
    }
    auto towed_point = g->m.bub_to_abs( tow_data.get_towed_by()->bub_part_location( other_index ) );
    if( towing_point == tripoint_abs_ms::zero() || towed_point == tripoint_abs_ms::zero() ) {
    debugmsg( "towing data exists but no towing part" );
        return false;
    }
    return rl_dist( towing_point, towed_point ) >= 25;
}

// the towing cable only starts pulling at a certain distance between the vehicles
// to be called on the towing vehicle
bool vehicle::no_towing_slack() const
{
    if( !tow_data.get_towed() ) {
    return false;
}
int index = get_tow_part();
if( index == -1 ) {
    debugmsg( "towing data exists but no towing part" );
        return false;
    }
    auto towing_point = g->m.bub_to_abs( bub_part_location( index ) );
    if( !tow_data.get_towed()->tow_data.get_towed_by() ) {
    debugmsg( "vehicle %s has data for a towed vehicle, but that towed vehicle does not have %s listed as tower",
              disp_name(), disp_name() );
        return false;
    }
    int other_index = tow_data.get_towed()->get_tow_part();
    if( other_index == -1 ) {
    debugmsg( "towing data exists but no towing part" );
        return false;
    }
    auto towed_point = g->m.bub_to_abs( tow_data.get_towed()->bub_part_location( other_index ) );
    if( towing_point == tripoint_abs_ms::zero() || towed_point == tripoint_abs_ms::zero() ) {
    debugmsg( "towing data exists but no towing part" );
        return false;
    }
    return rl_dist( towing_point, towed_point ) >= 8;

}

void vehicle::remove_remote_part( int part_num )
{
    if( parts[part_num].has_flag( vehicle_part::targets_grid ) ) {
        vehicle_connector_tile *connector =
            active_tiles::furn_at<vehicle_connector_tile>( parts[part_num].target.second );
        if( connector != nullptr ) {
            auto &vehs = connector->connected_vehicles;
            const auto iter = std::ranges::find( vehs, abs_ms_location() );
            if( iter != vehs.end() ) {
                vehs.erase( iter );
            }
        }
        return;
    }
    auto veh = find_vehicle( parts[part_num].target.second, MAPBUFFER_REGISTRY.get( dimension_id_ ) );

    // If the target vehicle is still there, ask it to remove its part
    if( veh != nullptr ) {
        const auto local_abs = abs_part_location( part_num );

        for( size_t j = 0; j < veh->loose_parts.size(); j++ ) {
            int remote_partnum = veh->loose_parts[j];
            auto remote_part = &veh->parts[remote_partnum];

            if( veh->part_flag( remote_partnum, "POWER_TRANSFER" ) && remote_part->target.first == local_abs ) {
                veh->remove_part( remote_partnum );
                return;
            }
        }
    }
}
