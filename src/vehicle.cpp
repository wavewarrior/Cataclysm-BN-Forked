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
class RemovePartHandler
{
    public:
        virtual ~RemovePartHandler() = default;

        virtual void unboard( const tripoint_bub_ms &loc ) = 0;
        virtual detached_ptr<item> add_item_or_charges( const tripoint_bub_ms &loc, detached_ptr<item> &&it,
                bool permit_oob ) = 0;
        virtual void set_transparency_cache_dirty( int z ) = 0;
        virtual void set_floor_cache_dirty( int z ) = 0;
        virtual void removed( vehicle &veh, int part ) = 0;
        virtual void spawn_animal_from_part( item &base, const tripoint_bub_ms &loc ) = 0;
        virtual auto part_location( const vehicle &veh, const int part ) const -> tripoint_bub_ms = 0;
};

class DefaultRemovePartHandler : public RemovePartHandler
{
    public:
        ~DefaultRemovePartHandler() override = default;

        void unboard( const tripoint_bub_ms &loc ) override {
            g->m.unboard_vehicle( loc );
        }
        detached_ptr<item> add_item_or_charges( const tripoint_bub_ms &loc, detached_ptr<item> &&it,
                                                bool /*permit_oob*/ ) override {
            return g->m.add_item_or_charges( loc, std::move( it ) );
        }
        void set_transparency_cache_dirty( const int z ) override {
            map &here = get_map();
            here.set_transparency_cache_dirty( z );
            here.set_seen_cache_dirty( tripoint_bub_ms::zero() );
        }
        void set_floor_cache_dirty( const int z ) override {
            get_map().set_floor_cache_dirty( z );
        }
        void removed( vehicle &veh, const int part ) override {
            avatar &player_character = get_avatar();
            // If the player is currently working on the removed part, stop them as it's futile now.
            const player_activity &act = *player_character.activity;
            map &here = get_map();
            if( act.id() == ACT_VEHICLE && act.moves_left > 0 && act.values.size() > 6 ) {
                if( veh_pointer_or_null( here.veh_at( tripoint_bub_ms( act.values[0], act.values[1],
                                                      player_character.bub_pos().z() ) ) ) == &veh ) {
                    if( act.values[6] >= part ) {
                        player_character.cancel_activity();
                        add_msg( m_info, _( "The vehicle part you were working on has gone!" ) );
                    }
                }
            }
            // TODO: maybe do this for all the nearby NPCs as well?

            if( g->u.get_grab_type() == OBJECT_VEHICLE &&
                g->u.bub_pos() + g->u.grab_point == veh.bub_part_location( part ) ) {
                if( veh.parts_at_relative( veh.part( part ).mount, false ).empty() ) {
                    add_msg( m_info, _( "The vehicle part you were holding has been destroyed!" ) );
                    g->u.grab( OBJECT_NONE );
                }
            }

            here.dirty_vehicle_list.insert( &veh );
        }
        void spawn_animal_from_part( item &base, const tripoint_bub_ms &loc ) override {
            base.release_monster( loc, 1 );
        }
        auto part_location( const vehicle &veh, const int part ) const -> tripoint_bub_ms override {
            return veh.bub_part_location( part );
        }
};

class MapgenRemovePartHandler : public RemovePartHandler
{
    private:
        map &m;

    public:
        MapgenRemovePartHandler( map &m ) : m( m ) { }

        ~MapgenRemovePartHandler() override = default;

        void unboard( const tripoint_bub_ms &/*loc*/ ) override {
            debugmsg( "Tried to unboard during mapgen!" );
            // Ignored. Will almost certainly not be called anyway, because
            // there are no creatures that could have been mounted during mapgen.
        }
        detached_ptr<item> add_item_or_charges( const tripoint_bub_ms &loc, detached_ptr<item> &&it,
                                                bool permit_oob ) override {
            if( !m.inbounds( loc ) ) {
            point_sm_ms offset;
            if( !m.is_out_of_bounds( loc ) && m.get_submap_at( loc, offset ) != nullptr ) {
                    return m.add_item_or_charges( loc, std::move( it ) );
                }
                if( !permit_oob ) {
                    return std::move( it );
                }
                auto copy = loc;
                m.clip_to_bounds( copy );
                assert( m.inbounds( copy ) ); // prevent infinite recursion
                return add_item_or_charges( copy, std::move( it ), false );
            }
            return m.add_item_or_charges( loc, std::move( it ) );
        }
        void set_transparency_cache_dirty( const int /*z*/ ) override {
            // Ignored for now. We don't initialize the transparency cache in mapgen anyway.
        }
        void set_floor_cache_dirty( const int /*z*/ ) override {
            // Ignored for now. We don't initialize the floor cache in mapgen anyway.
        }
        void removed( vehicle &veh, const int /*part*/ ) override {
            // TODO: check if this is necessary, it probably isn't during mapgen
            m.dirty_vehicle_list.insert( &veh );
        }
        void spawn_animal_from_part( item &/*base*/, const tripoint_bub_ms &/*loc*/ ) override {
            debugmsg( "Tried to spawn animal from vehicle part during mapgen!" );
            // Ignored. The base item will not be changed and will spawn as is:
            // still containing the animal.
            // This should not happend during mapgen anyway.
            // TODO: *if* this actually happens: create a spawn point for the animal instead.
        }
        auto part_location( const vehicle &veh, const int part ) const -> tripoint_bub_ms override {
            return m.abs_to_bub( veh.abs_part_location( part ) );
        }
};

// Vehicle stack methods.
vehicle_stack::iterator vehicle_stack::erase( vehicle_stack::const_iterator it,
        detached_ptr<item> *out )
{
    return myorigin->remove_item( part_num, std::move( it ), out );
}

void vehicle_stack::insert( detached_ptr<item> &&newitem )
{
    myorigin->add_item( part_num, std::move( newitem ) );
}

detached_ptr<item> vehicle_stack::remove( item *to_remove )
{
    return myorigin->remove_item( part_num, to_remove );
}

units::volume vehicle_stack::max_volume() const
{
    if( myorigin->part_flag( part_num, "CARGO" ) && !myorigin->part( part_num ).is_broken() ) {
    // Set max volume for vehicle cargo to prevent integer overflow
    return std::min( myorigin->part( part_num ).info().size, 10000_liter );
    }
    return 0_ml;
}

// Vehicle class methods.

void vehicle::copy_static_from( const vehicle &source )
{
    //next_hack_id isn't copied
    //parts isn't copied;
    collision_check_points = source.collision_check_points;
    owner = source.owner;
    old_owner = source.old_owner;
    coefficient_air_resistance = source.coefficient_air_resistance;
    coefficient_rolling_resistance = source.coefficient_rolling_resistance;
    coefficient_water_resistance = source.coefficient_water_resistance;
    draft_m = source.draft_m;
    hull_height = source.hull_height;
    hull_area = source.hull_area;
    occupied_points = source.occupied_points;
    alternators = source.alternators;
    engines = source.engines;
    reactors = source.reactors;
    solar_panels = source.solar_panels;
    wind_turbines = source.wind_turbines;
    water_wheels = source.water_wheels;
    sails = source.sails;
    funnels = source.funnels;
    emitters = source.emitters;
    loose_parts = source.loose_parts;
    wheelcache = source.wheelcache;
    rotors = source.rotors;
    propellers = source.propellers;
    wings = source.wings;
    balloons = source.balloons;
    converters = source.converters;
    tanks = source.tanks;
    droppers = source.droppers;
    rail_wheelcache = source.rail_wheelcache;
    steering = source.steering;
    speciality = source.speciality;
    floating = source.floating;
    rail_profile = source.rail_profile;
    name = source.name;
    type = source.type;
    relative_parts = source.relative_parts;
    labels = source.labels;
    tags = source.tags;
    fuel_remainder = source.fuel_remainder;
    fuel_used_last_turn = source.fuel_used_last_turn;
    loot_zones = source.loot_zones;
    active_items = source.active_items;
    magic = source.magic;
    summon_time_limit = source.summon_time_limit;
    mass_cache = source.mass_cache;
    pivot_cache = source.pivot_cache;
    mount_max = source.mount_max;
    mount_min = source.mount_min;
    mass_center_precalc = source.mass_center_precalc;
    mass_center_no_precalc = source.mass_center_no_precalc;
    autodrive_local_target = source.autodrive_local_target;
    active_autodrive_controller = source.active_autodrive_controller;
    removed_part_count = source.removed_part_count;
    abs_sm_pos = source.abs_sm_pos;
    sm_ms_pos = source.sm_ms_pos;
    alternator_load = source.alternator_load;
    occupied_cache_time = source.occupied_cache_time;
    last_update = source.last_update;
    velocity = source.velocity;
    cruise_velocity = source.cruise_velocity;
    vertical_velocity = source.vertical_velocity;
    om_id = source.om_id;
    turn_dir = source.turn_dir;
    last_turn = source.last_turn;
    of_turn = source.of_turn;
    of_turn_carry = source.of_turn_carry;
    extra_drag = source.extra_drag;
    last_fluid_check = source.last_fluid_check;
    theft_time = source.theft_time;
    pivot_rotation = source.pivot_rotation;
    front_left = source.front_left;
    front_right = source.front_right;
    tow_data = source.tow_data;
    pivot_anchor = source.pivot_anchor;
    face = source.face;
    move = source.move;
    no_refresh = source.no_refresh;
    pivot_dirty = source.pivot_dirty;
    mass_dirty = source.mass_dirty;
    mass_center_precalc_dirty = source.mass_center_precalc_dirty;
    mass_center_no_precalc_dirty = source.mass_center_no_precalc_dirty;
    coeff_rolling_dirty = source.coeff_rolling_dirty;
    coeff_air_dirty = source.coeff_air_dirty;
    coeff_water_dirty = source.coeff_water_dirty;
    coeff_air_changed = source.coeff_air_changed;
    is_floating = source.is_floating;
    in_water = source.in_water;
    is_flying = source.is_flying;
    requested_z_change = source.requested_z_change;
    attached = source.attached;
    is_autodriving = source.is_autodriving;
    is_following = source.is_following;
    follow_distance = source.follow_distance;
    is_patrolling = source.is_patrolling;
    cruise_on = source.cruise_on;
    engine_on = source.engine_on;
    brake_hold = source.brake_hold;
    tracking_on = source.tracking_on;
    is_locked = source.is_locked;
    is_alarm_on = source.is_alarm_on;
    camera_on = source.camera_on;
    autopilot_on = source.autopilot_on;
    skidding = source.skidding;
    check_environmental_effects = source.check_environmental_effects;
    insides_dirty = source.insides_dirty;
    is_falling = source.is_falling;
    zones_dirty = source.zones_dirty;
    vehicle_noise = source.vehicle_noise;
}

vehicle::vehicle(
    const vproto_id &type_id, int init_veh_fuel, int init_veh_status, std::optional<bool> locked,
    std::optional<bool> has_keys )
    : type( type_id )
{
    turn_dir = 0_degrees;
    face.init( 0_degrees );
    move.init( 0_degrees );
    of_turn_carry = 0;

    if( !type.str().empty() && type.is_valid() ) {
        const vehicle_prototype &proto = type.obj();
        // Copy the already made vehicle. The blueprint is created when the json data is loaded
        // and is guaranteed to be valid (has valid parts etc.).
        copy_static_from( *proto.blueprint );
        if( proto.color_palette.is_valid() ) {
            std::vector<RGBColor> colors = proto.color_palette->pick_colors();
            for( vehicle_part &part : proto.blueprint->parts ) {
                parts.emplace_back( part, this );
                auto &real_part = parts.back();
                if( proto.color_match.contains( real_part.id.str() ) ) {
                    const auto [old_bg, old_fg] = real_part.get_color( true );
                    const auto c = colors.at( proto.color_match.at( real_part.id.str() ) );
                    real_part.set_color(
                        old_bg == RGBColor{} ? c : old_bg,
                        old_fg == RGBColor{} ? c : old_fg
                    );
                }
            }
        } else {
            for( vehicle_part &part : proto.blueprint->parts ) {
                parts.emplace_back( part, this );
            }
        }
        refresh_locations_hack();
        init_state( init_veh_fuel, init_veh_status, locked, has_keys );
    }
    precalc_mounts( 0, pivot_rotation[0], pivot_anchor[0] );
    refresh();
}

vehicle::vehicle() : vehicle( vproto_id() )
{
    abs_sm_pos = tripoint_abs_sm::zero();
    sm_ms_pos = point_sm_ms::zero();
}

vehicle::~vehicle() = default;

bool vehicle::player_in_control( const Character &who ) const
{
    // Debug switch to prevent vehicles from skidding
    // without having to place the player in them.
    if( tags.contains( "IN_CONTROL_OVERRIDE" ) ) {
    return true;
}

const optional_vpart_position vp = g->m.veh_at( who.bub_pos() );
if( vp && &vp->vehicle() == this &&
        ( ( part_with_feature( vp->part_index(), "CONTROL_ANIMAL", true ) >= 0 &&
            has_engine_type( fuel_type_animal, false ) && has_harnessed_animal() ) ||
              ( part_with_feature( vp->part_index(), VPFLAG_CONTROLS, false ) >= 0 ) ) &&
            who.controlling_vehicle ) {
        return true;
    }

    return remote_controlled( who );
}

bool vehicle::remote_controlled( const Character &who ) const
{
    vehicle *veh = g->remoteveh();
    if( veh != this ) {
        return false;
    }

    for( const vpart_reference &vp : get_avail_parts( "REMOTE_CONTROLS" ) ) {
        if( rl_dist( who.bub_pos(), vp.pos() ) <= 40 ) {
            return true;
        }
    }

    add_msg( m_bad, _( "Lost connection with the vehicle due to distance!" ) );
    g->setremoteveh( nullptr );
    return false;
}

/** Checks all parts to see if frames are missing (as they might be when
 * loading from a game saved before the vehicle construction rules overhaul). */
void vehicle::add_missing_frames()
{
    static const vpart_id frame_id( "frame_vertical" );
    //No need to check the same spot more than once
    std::set<tripoint_mnt_veh> locations_checked;
    for( auto &i : parts ) {
        if( locations_checked.contains( i.mount ) ) {
            continue;
        }
        locations_checked.insert( i.mount );

        bool found = false;
        for( auto &elem : parts_at_relative( i.mount, false ) ) {
            if( part_info( elem ).location == part_location_structure ) {
                found = true;
                break;
            }
        }
        if( !found ) {
            // Install missing frame
            parts.emplace_back( frame_id, i.mount, item::spawn( frame_id->item ), this );
            refresh_locations_hack();
        }
    }
}

// Called when loading a vehicle that predates steerable wheels.
// Tries to convert some wheels to steerable versions on the front axle.
void vehicle::add_steerable_wheels()
{
    int axle = INT_MIN;
    std::vector< std::pair<int, vpart_id> > wheels;

    // Find wheels that have steerable versions.
    // Convert the wheel(s) with the largest x value.
    for( const vpart_reference &vp : get_all_parts() ) {
        if( vp.has_feature( "STEERABLE" ) || vp.has_feature( "TRACKED" ) ) {
            // Has a wheel that is inherently steerable
            // (e.g. unicycle, casters), this vehicle doesn't
            // need conversion.
            return;
        }

        if( vp.mount().x() < axle ) {
            // there is another axle in front of this
            continue;
        }

        if( vp.has_feature( VPFLAG_WHEEL ) ) {
            vpart_id steerable_id( vp.info().get_id().str() + "_steerable" );
            if( steerable_id.is_valid() ) {
                // We can convert this.
                if( vp.mount().x() != axle ) {
                    // Found a new axle further forward than the
                    // existing one.
                    wheels.clear();
                    axle = vp.mount().x();
                }

                wheels.emplace_back( static_cast<int>( vp.part_index() ), steerable_id );
            }
        }
    }

    // Now convert the wheels to their new types.
    for( auto &wheel : wheels ) {
        parts[ wheel.first ].id = wheel.second;
    }
}

void vehicle::init_state( const int init_veh_fuel, const int init_veh_status,
                          const std::optional<bool> locked,
                          const std::optional<bool> has_keys )
{
    // vehicle parts excluding engines are by default turned off
    for( auto &pt : parts ) {
        pt.enabled = pt.base->is_engine();
    }

    bool destroySeats = false;
    bool destroyControls = false;
    bool destroyTank = false;
    bool destroyEngine = false;
    bool destroyTires = false;
    bool blood_covered = false;
    bool blood_inside = false;
    bool lockDoors = false;
    bool needsHotwire = false;
    bool destroyAlarm = false;

    remove_old_owner();
    remove_owner();

    // More realistically it should be -5 days old
    last_update = calendar::start_of_cataclysm;

    // veh_fuel_multiplier is percentage of fuel
    // 0 is empty, 100 is full tank, -1 is random 7% to 35%
    int veh_fuel_mult = init_veh_fuel;
    if( init_veh_fuel == - 1 ) {
        veh_fuel_mult = rng( 1, 7 );
    }
    if( init_veh_fuel > 100 ) {
        veh_fuel_mult = 100;
    }

    // veh_status is initial vehicle damage
    // -1 = light damage (DEFAULT)
    //  0 = undamaged
    //  1 = disabled, destroyed tires OR engine
    const int veh_status = init_veh_status;
    if( init_veh_status == 0 ) {
        // vehicle locked 100%
        lockDoors = needsHotwire = true;
    } else if( init_veh_status == -1 ) {
        // vehicle locked 67%
        lockDoors = needsHotwire = rng( 1, 100 ) <= 67;

        // if locked, 16% chance something damaged
        if( one_in( 6 ) && ( needsHotwire || lockDoors ) ) {
            if( one_in( 3 ) ) {
                destroyTank = true;
            } else if( one_in( 2 ) ) {
                destroyEngine = true;
            } else {
                destroyTires = true;
            }
        }
    } else if( init_veh_status == 1 ) {
        //  seats are destroyed 5%
        destroySeats = rng( 1, 100 ) <= 5;
        // controls are destroyed 10%
        destroyControls = rng( 1, 100 ) <= 10;
        // battery, minireactor or gasoline tank are destroyed 8%
        destroyTank = rng( 1, 100 ) <= 8;
        // engine are destroyed 6%
        destroyEngine = rng( 1, 100 ) <= 6;
        // tires are destroyed 37%
        destroyTires = rng( 1, 100 ) <= 37;
        // locked 34%
        lockDoors = needsHotwire = rng( 1, 100 ) <= 34;

        if( destroyEngine ) {
            veh_fuel_mult += rng( 3, 12 );  // add 3-12% more fuel if engine is destroyed
        }
        if( destroyControls ) {
            veh_fuel_mult += rng( 0, 7 );   // add 0-7% more fuel if controls are destroyed
        }
        if( destroyTires ) {
            veh_fuel_mult += rng( 0, 18 );  // add 0-18% more fuel if tires are destroyed
        }
    }

    //most cars should have a destroyed alarm
    if( !one_in( 3 ) ) {
        destroyAlarm = true;
    }

    // Check Prototype Flags
    if( get_avail_parts( VPFLAG_CONTROLS ).part_count() > 0 ) {
        const auto &proto_flags = type.obj().flags;
        if( has_keys.has_value() ) {
            needsHotwire = !has_keys.value();
        } else if( proto_flags.contains( f_VEHICLE_HOTWIRE ) ) {
            needsHotwire = true;
        } else if( proto_flags.contains( f_VEHICLE_NO_HOTWIRE ) ) {
            needsHotwire = false;
        } else  {
            // No horse-wiring
            for( const auto &vp : get_all_parts() ) {
                if( std::ranges::any_of( vs_NO_HOTWIRING, [&]( const std::string & flag ) { return vp.has_feature( flag );} ) ) {
                    needsHotwire = false;
                    break;
                }
            }
        }
    } else {
        needsHotwire = false;
    }

    is_locked = needsHotwire;

    const auto &proto_flags = type.obj().flags;
    if( locked.has_value() ) {
        lockDoors = locked.value();
    } else if( proto_flags.contains( f_VEHICLE_UNLOCKED ) ) {
        lockDoors = false;
    } else if( proto_flags.contains( f_VEHICLE_LOCKED ) ) {
        lockDoors = true;
    } else {
        lockDoors = lockDoors && get_option<bool>( "VEHICLE_LOCKS" );
    }

    //Provide some variety to non-mint vehicles
    if( veh_status != 0 ) {
        //Leave engine running in some vehicles, if the engine has not been destroyed
        //chance decays from 1 in 4 vehicles on day 0 to 1 in (day + 4) in the future.
        int current_day = std::max( to_days<int>( calendar::turn - calendar::turn_zero ), 0 );
        if( veh_fuel_mult > 0 && !get_avail_parts( "ENGINE" ).empty() &&
            one_in( current_day + 4 ) && !destroyEngine && !needsHotwire &&
            has_engine_type_not( fuel_type_muscle, true ) ) {
            engine_on = true;
        }

        auto light_head  = one_in( 20 );
        auto light_whead  = one_in( 20 ); // wide-angle headlight
        auto light_dome  = one_in( 16 );
        auto light_aisle = one_in( 8 );
        auto light_hoverh = one_in( 4 ); // half circle overhead light
        auto light_overh = one_in( 4 );
        auto light_atom  = one_in( 2 );
        for( auto &pt : parts ) {
            if( pt.info().has_flag( VPFLAG_CONE_LIGHT ) ) {
                pt.enabled = light_head;
            } else if( pt.info().has_flag( VPFLAG_WIDE_CONE_LIGHT ) ) {
                pt.enabled = light_whead;
            } else if( pt.info().has_flag( VPFLAG_DOME_LIGHT ) ) {
                pt.enabled = light_dome;
            } else if( pt.info().has_flag( VPFLAG_AISLE_LIGHT ) ) {
                pt.enabled = light_aisle;
            } else if( pt.info().has_flag( VPFLAG_HALF_CIRCLE_LIGHT ) ) {
                pt.enabled = light_hoverh;
            } else if( pt.info().has_flag( VPFLAG_CIRCLE_LIGHT ) ) {
                pt.enabled = light_overh;
            } else if( pt.info().has_flag( VPFLAG_ATOMIC_LIGHT ) ) {
                pt.enabled = light_atom;
            }
        }

        if( one_in( 10 ) ) {
            blood_covered = true;
        }

        if( one_in( 8 ) ) {
            blood_inside = true;
        }

        for( const vpart_reference &vp : get_parts_including_carried( "FRIDGE" ) ) {
            if( one_in( 2 ) ) {
                vp.part().enabled = true;
            }
        }

        for( const vpart_reference &vp : get_parts_including_carried( "FREEZER" ) ) {
            if( one_in( 2 ) ) {
                vp.part().enabled = true;
            }
        }

        for( const vpart_reference &vp : get_parts_including_carried( "WATER_PURIFIER" ) ) {
            vp.part().enabled = true;
        }
    }

    // Install Locks
    if( !proto_flags.contains( f_VEHICLE_NO_LOCKS ) ) {
        std::set<tripoint_mnt_veh> doors;
        for( const vpart_reference &vp : get_all_parts() ) {
            if( vp.has_feature( "OPENABLE" ) && vp.has_feature( "BOARDABLE" ) &&
                !vp.has_feature( "CURTAIN" ) ) {
                doors.emplace( vp.mount() );
            }
        }

        for( const auto &door : doors ) {
            const auto idx = install_part( door, vp_door_lock );
            if( idx >= 0 ) {
                // Newly installed part
                parts[idx].enabled = lockDoors;
            } else {
                // Already installed from blueprint
                const auto lock = part_with_feature( door, str_DOOR_LOCKING, true );
                if( lock >= 0 ) {
                    parts[lock].enabled = lockDoors;
                } else {
                    // Already installed from blueprint
                    debugmsg( "Failed to install door locks on vehicle" );
                }
            }
        }
    }

    std::optional<tripoint_mnt_veh> blood_inside_pos;
    for( const vpart_reference &vp : get_all_parts() ) {
        const size_t p = vp.part_index();
        vehicle_part &pt = vp.part();

        if( pt.info().has_flag( str_PERPETUAL ) ) {
            pt.enabled = true;
        } else if( vp.has_feature( VPFLAG_REACTOR ) && one_in( 4 ) ) {
            // De-hardcoded reactors may or may not start active.
            pt.enabled = true;
        }

        if( pt.is_reactor() ) {
            if( veh_fuel_mult == 100 ) { // Mint condition vehicle
                pt.ammo_set( itype_plut_cell, pt.ammo_capacity() );
            } else if( one_in( 2 ) && veh_fuel_mult > 0 ) { // Randomize charge a bit
                pt.ammo_set( itype_plut_cell, pt.ammo_capacity() * ( veh_fuel_mult + rng( 0, 10 ) ) / 100 );
            } else if( one_in( 2 ) && veh_fuel_mult > 0 ) {
                pt.ammo_set( itype_plut_cell, pt.ammo_capacity() * ( veh_fuel_mult - rng( 0, 10 ) ) / 100 );
            } else {
                pt.ammo_set( itype_plut_cell, pt.ammo_capacity() * veh_fuel_mult / 100 );
            }
        }

        if( pt.is_battery() ) {
            if( veh_fuel_mult == 100 ) { // Mint condition vehicle
                pt.ammo_set( itype_battery, pt.ammo_capacity() );
            } else if( one_in( 2 ) && veh_fuel_mult > 0 ) { // Randomize battery ammo a bit
                pt.ammo_set( itype_battery, pt.ammo_capacity() * ( veh_fuel_mult + rng( 0, 10 ) ) / 100 );
            } else if( one_in( 2 ) && veh_fuel_mult > 0 ) {
                pt.ammo_set( itype_battery, pt.ammo_capacity() * ( veh_fuel_mult - rng( 0, 10 ) ) / 100 );
            } else {
                pt.ammo_set( itype_battery, pt.ammo_capacity() * veh_fuel_mult / 100 );
            }
        }

        if( pt.is_tank() && !type->parts[p].fuel.is_null() ) {
            auto qty = pt.ammo_capacity() * veh_fuel_mult / 100;
            qty *= std::max( type->parts[p].fuel->stack_size, 1 );
            qty /= to_milliliter( units::legacy_volume_factor );

            const auto global_rate = get_option<float>( "ITEM_SPAWNRATE" );
            const auto fuel_rate = get_option<float>( "SPAWN_RATE_fuel" );
            const auto combined_rate = global_rate * fuel_rate;

            if( combined_rate < 1.0f ) {
                if( rng_float( 0, 1 ) < combined_rate ) {
                    pt.ammo_set( type->parts[ p ].fuel, qty );
                }
            } else {
                auto scaled_qty = std::min( static_cast<int>( qty * combined_rate ), pt.ammo_capacity() );
                pt.ammo_set( type->parts[ p ].fuel, scaled_qty );
            }
        } else if( pt.is_fuel_store() && !type->parts[p].fuel.is_null() ) {
            auto qty = pt.ammo_capacity() * veh_fuel_mult / 100;

            const auto global_rate = get_option<float>( "ITEM_SPAWNRATE" );
            const auto fuel_rate = get_option<float>( "SPAWN_RATE_fuel" );
            const auto combined_rate = global_rate * fuel_rate;

            if( combined_rate < 1.0f ) {
                if( rng_float( 0, 1 ) < combined_rate ) {
                    pt.ammo_set( type->parts[ p ].fuel, qty );
                }
            } else {
                auto scaled_qty = std::min( static_cast<int>( qty * combined_rate ), pt.ammo_capacity() );
                pt.ammo_set( type->parts[ p ].fuel, scaled_qty );
            }
        }

        if( vp.has_feature( "OPENABLE" ) ) { // doors are closed
            if( !pt.open && one_in( 4 ) ) {
                open( p );
                const auto lock = vp.part_with_feature( str_DOOR_LOCKING, true );
                if( lock ) {
                    lock->part().enabled = false;
                }
            }
        }
        if( vp.has_feature( "BOARDABLE" ) ) {   // no passengers
            pt.remove_flag( vehicle_part::passenger_flag );
        }

        // initial vehicle damage
        if( veh_status == 0 ) {
            // Completely mint condition vehicle
            set_hp( pt, vp.info().durability );
        } else {
            //a bit of initial damage :)
            //clamp 4d8 to the range of [8,20]. 8=broken, 20=undamaged.
            const float chance =  get_option<float>( "VEHICLE_DAMAGE" ) ;
            int broken = 8 * chance;
            int unhurt = 20 * chance;
            int roll = dice( 4, 8 );
            if( roll < unhurt ) {
                if( roll <= broken ) {
                    set_hp( pt, 0 );
                    pt.ammo_unset(); //empty broken batteries and fuel tanks
                } else {
                    set_hp( pt, ( roll - broken ) / static_cast<double>( unhurt - broken ) * vp.info().durability );
                }
            } else {
                set_hp( pt, vp.info().durability );
            }

            if( vp.has_feature( VPFLAG_ENGINE ) ) {
                // If possible set an engine fault rather than destroying the engine outright
                if( destroyEngine && pt.faults_potential().empty() ) {
                    set_hp( pt, 0 );
                } else if( destroyEngine || one_in( 3 ) ) {
                    do {
                        pt.fault_set( random_entry( pt.faults_potential() ) );
                    } while( one_in( 3 ) );
                }

            } else if( ( destroySeats && ( vp.has_feature( "SEAT" ) || vp.has_feature( "SEATBELT" ) ) ) ||
                       ( destroyControls && ( vp.has_feature( "CONTROLS" ) || vp.has_feature( "SECURITY" ) ) ) ||
                       ( destroyAlarm && vp.has_feature( "SECURITY" ) ) ) {
                set_hp( pt, 0 );
            }

            // Fuel tanks should be emptied as well
            if( destroyTank && pt.is_fuel_store() ) {
                set_hp( pt, 0 );
                pt.ammo_unset();
            }

            //Solar panels have 25% of being destroyed
            if( vp.has_feature( "SOLAR_PANEL" ) && one_in( 4 ) ) {
                set_hp( pt, 0 );
            }

            // An added 5% chance to bust each windshield
            if( vp.has_feature( "WINDSHIELD" ) && one_in( 20 ) ) {
                set_hp( pt, 0 );
            }

            /* Bloodsplatter the front-end parts. Assume anything with x > 0 is
            * the "front" of the vehicle (since the driver's seat is at (0, 0).
            * We'll be generous with the blood, since some may disappear before
            * the player gets a chance to see the vehicle. */
            if( blood_covered && vp.mount().x() > 0 ) {
                if( one_in( 3 ) ) {
                    //Loads of blood. (200 = completely red vehicle part)
                    pt.blood = rng( 200, 600 );
                } else {
                    //Some blood
                    pt.blood = rng( 50, 200 );
                }
            }

            if( blood_inside ) {
                // blood is splattered around (blood_inside_pos),
                // coordinates relative to mount point; the center is always a seat
                if( blood_inside_pos ) {
                    const int distSq = std::pow( blood_inside_pos->x() - vp.mount().x(), 2 ) +
                                       std::pow( blood_inside_pos->y() - vp.mount().y(), 2 );
                    if( distSq <= 1 ) {
                        pt.blood = rng( 200, 400 ) - distSq * 100;
                    }
                } else if( vp.has_feature( "SEAT" ) ) {
                    // Set the center of the bloody mess inside
                    blood_inside_pos.emplace( vp.mount() );
                }
            }

            // Potentially bust a single tire if not already wrecking them
            if( !destroyTires && !wheelcache.empty() && one_in( 20 ) ) {
                set_hp( parts[random_entry( wheelcache )], 0 );
            }
        }
        // if there is no key and an alarm part exists
        // and vehicle has immobilizer 50% chance to add additional fault
        if( vp.has_feature( "SECURITY" ) && needsHotwire && pt.is_available() && one_in( 2 ) ) {
            pt.fault_set( fault_immobiliser );
        }
    }

    // destroy a random number of tires, vehicles with more wheels are more likely to survive
    if( destroyTires && !wheelcache.empty() ) {
        int tries = 0;
        int maxtries = wheelcache.size();
        while( valid_wheel_config() && tries < maxtries ) {
            // keep going until either we've ruined all wheels or made one attempt for every wheel
            set_hp( parts[random_entry( wheelcache )], 0 );
            tries++;
        }
    }

    invalidate_mass();
}

void vehicle::activate_magical_follow()
{
    for( vehicle_part &vp : parts ) {
        if( vp.info().fuel_type == fuel_type_mana ) {
            vp.enabled = true;
            is_following = true;
            follow_distance = 12 + mount_max.y() * 3;
            engine_on = true;
        } else {
            vp.enabled = true;
        }
    }
    refresh();
}

void vehicle::activate_animal_follow()
{
    for( size_t e = 0; e < parts.size(); e++ ) {
        vehicle_part &vp = parts[ e ];
        if( vp.info().fuel_type == fuel_type_animal ) {
            monster *mon = get_pet( e );
            if( mon && mon->has_effect( effect_harnessed ) ) {
                vp.enabled = true;
                is_following = true;
                follow_distance = 12 + mount_max.y() * 3;
                engine_on = true;
            }
        } else {
            vp.enabled = true;
        }
    }
    refresh();
}

void vehicle::autopilot_patrol()
{
    /** choose one single zone ( multiple zones too complex for now )
     * choose a point at the far edge of the zone
     * the edge chosen is the edge that is smaller, therefore the longer side
     * of the rectangle is the one the vehicle drives mostly parallel too.
     * if its  perfect square then choose a point that is on any edge that the
     * vehicle is not currently at
     * drive to that point.
     * then once arrived, choose a random opposite point of the zone.
     * this should ( in a simple fashion ) cause a patrolling behavior
     * in a criss-cross fashion.
     * in an auto-tractor, this would eventually cover the entire rectangle.
     */
    // if we are close to a waypoint, then return to come back to this function next turn.
    if( autodrive_local_target != tripoint_abs_ms::zero() ) {
        if( rl_dist( abs_ms_location(), autodrive_local_target ) <= 3 ) {
            autodrive_local_target = tripoint_abs_ms::zero();
            return;
        }
        if( !g->m.inbounds( g->m.abs_to_bub( autodrive_local_target ) ) ) {
            autodrive_local_target = tripoint_abs_ms::zero();
            is_patrolling = false;
            return;
        }
        drive_to_local_target( autodrive_local_target, false );
        return;
    }
    zone_manager &mgr = zone_manager::get_manager();
    const auto &zone_src_set = mgr.get_near( zone_type_id( "VEHICLE_PATROL" ),
                               abs_ms_location(), g_max_view_distance );
    if( zone_src_set.empty() ) {
        is_patrolling = false;
        return;
    }
    // get corners.
    tripoint_abs_ms min;
    tripoint_abs_ms max;
    for( const auto &box : zone_src_set ) {
        if( min == tripoint_abs_ms::zero() ) {
            min =  box;
            max =  box;
            continue;
        }
        min.x() = std::min( box.x(), min.x() );
        min.y() = std::min( box.y(), min.y() );
        min.z() = std::min( box.z(), min.z() );
        max.x() = std::max( box.x(), max.x() );
        max.y() = std::max( box.y(), max.y() );
        max.z() = std::max( box.z(), max.z() );
    }
    const bool x_side = ( max.x() - min.x() ) < ( max.y() - min.y() );
    const int point_along = x_side ? rng( min.x(), max.x() ) : rng( min.y(), max.y() );
    const tripoint_abs_ms max_tri = x_side ? tripoint_abs_ms( point_along, max.y(),
                                    min.z() ) : tripoint_abs_ms( max.x(),
                                        point_along, min.z() );
    const tripoint_abs_ms min_tri = x_side ? tripoint_abs_ms( point_along, min.y(),
                                    min.z() ) : tripoint_abs_ms( min.x(),
                                        point_along, min.z() );
    tripoint_abs_ms chosen_tri = min_tri;
    if( rl_dist( max_tri, abs_ms_location() ) >= rl_dist( min_tri, abs_ms_location() ) ) {
        chosen_tri = max_tri;
    }
    autodrive_local_target = chosen_tri;
    drive_to_local_target( autodrive_local_target, false );
}

std::set<point_abs_ms> vehicle::immediate_path( units::angle rotate )
{
    std::set<point_abs_ms> points_to_check;
    const int distance_to_check = 10 + ( velocity / 358 );
    units::angle adjusted_angle = normalize( face.dir() + rotate );
    // clamp to multiples of 15.
    adjusted_angle = round_to_multiple_of( adjusted_angle, 15_degrees );
    tileray collision_vector;
    collision_vector.init( adjusted_angle );
    auto fl_bub = bub_ms_location() + coord_translate( front_left );
    auto fr_bub = bub_ms_location() + coord_translate( front_right );
    std::vector<point_abs_ms> front_row = line_to( g->m.bub_to_abs( fl_bub ).xy(),
                                          g->m.bub_to_abs( fr_bub ).xy() );
    for( point_abs_ms elem : front_row ) {
        for( int i = 0; i < distance_to_check; ++i ) {
            collision_vector.advance( i );
            point_abs_ms point_to_add = elem + point_rel_ms( collision_vector.dx(), collision_vector.dy() );
            points_to_check.emplace( point_to_add );
        }
    }
    collision_check_points = points_to_check;
    return points_to_check;
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
        // edge case of being exactly on the button for the target.
        // just keep driving, the next path point will be picked up.
    } else if( ( angle == 180_degrees || angle == -180_degrees ) && vehpos == target ) {
        return 0;
    }
    return 0;
}

void vehicle::drive_to_local_target( const tripoint_abs_ms &target, bool follow_protocol )
{
    if( follow_protocol && g->u.in_vehicle ) {
        stop_autodriving();
        return;
    }
    refresh();
    auto vehpos = abs_ms_location();
    units::angle angle = get_angle_from_targ( target );
    // now we got the angle to the target, we can work out when we are heading towards disaster.
    // Check the tileray in the direction we need to head towards.
    std::set<point_abs_ms> points_to_check = immediate_path( angle );
    bool stop = false;
    for( auto pt_elem : points_to_check ) {
        auto elem = g->m.abs_to_bub( pt_elem );
        if( stop ) {
            break;
        }
        const optional_vpart_position ovp = g->m.veh_at( tripoint_bub_ms( elem, abs_sm_pos.z() ) );
        if( g->m.impassable_ter_furn( tripoint_bub_ms( elem, abs_sm_pos.z() ) ) || ( ovp &&
                &ovp->vehicle() != this ) ) {
            stop = true;
            break;
        }
        if( elem == g->u.bub_pos().xy() ) {
            if( follow_protocol || g->u.in_vehicle ) {
                continue;
            } else {
                stop = true;
                break;
            }
        }
        bool its_a_pet = false;
        if( g->critter_at( tripoint_bub_ms( elem, abs_sm_pos.z() ) ) ) {
            npc *guy = g->critter_at<npc>( tripoint_bub_ms( elem, abs_sm_pos.z() ) );
            if( guy && !guy->in_vehicle ) {
                stop = true;
                break;
            }
            for( const auto &p : parts ) {
                monster *mon = get_pet( index_of_part( &p ) );
                if( mon && mon->bub_pos().xy() == elem ) {
                    its_a_pet = true;
                    break;
                }
            }
            if( !its_a_pet ) {
                stop = true;
                break;
            }
        }
    }
    if( stop ) {
        if( autopilot_on ) {
            sounds::sound( bub_ms_location(), 30, sounds::sound_t::alert,
                           string_format( _( "the %s emitting a beep and saying \"Obstacle detected!\"" ),
                                          name ) );
        }
        stop_autodriving();
        return;
    }
    int turn_x = get_turn_from_angle( angle, vehpos, target );
    int accel_y = 0;
    // best to cruise around at a safe velocity or 40mph, whichever is lowest
    // accelerate when it dosnt need to turn.
    // when following player, take distance to player into account.
    // we really want to avoid running the player over.
    // If its a helicopter, we dont need to worry about airborne obstacles so much
    // And fuel efficiency is terrible at low speeds.
    int safe_player_follow_speed = 179;
    if( g->u.movement_mode_is( CMM_RUN ) ) {
        safe_player_follow_speed = 358;
    } else if( g->u.is_crouching() ) {
        safe_player_follow_speed = 89;
    }
    if( follow_protocol ) {
        if( ( ( turn_x > 0 || turn_x < 0 ) && velocity > safe_player_follow_speed ) ||
            rl_dist( vehpos, g->m.bub_to_abs( g->u.bub_pos() ) ) < 7 + ( ( mount_max.y() * 3 ) + 4 ) ) {
            accel_y = 1;
        }
        if( ( velocity < std::min( safe_velocity(), safe_player_follow_speed ) && turn_x == 0 &&
              rl_dist( vehpos, g->m.bub_to_abs( g->u.bub_pos() ) ) > 8 + ( ( mount_max.y() * 3 ) + 4 ) ) ||
            velocity < 45 ) {
            accel_y = -1;
        }
    } else {
        if( ( turn_x > 0 || turn_x < 0 ) && velocity > 447 ) {
            accel_y = 1;
        }
        if( ( velocity < std::min( safe_velocity(), is_aircraft() &&
                                   is_flying_in_air() ? 5364 : 1431 ) && turn_x == 0 ) ||
            velocity < 224 ) {
            accel_y = -1;
        }
        if( is_patrolling && velocity > 179 ) {
            accel_y = 1;
        }
    }
    selfdrive( point( turn_x, accel_y ) );
}

tripoint_abs_ms vehicle::get_autodrive_target()
{
    return autodrive_local_target;
}

units::angle vehicle::get_angle_from_targ( const tripoint_abs_ms &targ )
{
    auto vehpos = abs_ms_location();
    rl_vec2d facevec = face_vec();
    auto rel_pos_target = targ.xy() - vehpos.xy();
    rl_vec2d targetvec = rl_vec2d( rel_pos_target.x(), rel_pos_target.y() );
    // cross product
    double crossy = ( facevec.x * targetvec.y ) - ( targetvec.x * facevec.y );
    // dot product.
    double dotx = ( facevec.x * targetvec.x ) + ( facevec.y * targetvec.y );

    return units::atan2( crossy, dotx );
}

/**
 * Smashes up a vehicle that has already been placed; used for generating
 * very damaged vehicles. Additionally, any spot where two vehicles overlapped
 * (i.e., any spot with multiple frames) will be completely destroyed, as that
 * was the collision point.
 */
void vehicle::smash( map &m, float hp_percent_loss_min, float hp_percent_loss_max,
                     float percent_of_parts_to_affect, tripoint_rel_ms damage_origin, float damage_size )
{
    for( auto &part : parts ) {
        //Skip any parts already mashed up or removed.
        if( part.is_broken() || part.removed || part.info().has_flag( VPFLAG_NOSMASH ) ) {
            continue;
        }

        std::vector<int> parts_in_square = parts_at_relative( part.mount, true );
        int structures_found = 0;
        for( auto &square_part_index : parts_in_square ) {
            if( part_info( square_part_index ).location == part_location_structure ||
                part_info( square_part_index ).has_flag( VPFLAG_EXTENDABLE ) ) {
                structures_found++;
            }
        }

        if( structures_found > 1 ) {
            //Destroy everything in the square
            for( int idx : parts_in_square ) {
                mod_hp( parts[ idx ], 0 - parts[ idx ].hp(), DT_BASH );
                parts[ idx ].ammo_unset();
            }
            continue;
        }

        int roll = dice( 1, 1000 );
        int pct_af = ( percent_of_parts_to_affect * 1000.0f );
        if( roll < pct_af ) {
            double dist = damage_size == 0.0f ? 1.0f :
                          clamp( 1.0f - trig_dist( damage_origin.xy(), part.precalc[0] ) /
                                 damage_size, 0.0f, 1.0f );
            //Everywhere else, drop by 10-120% of max HP (anything over 100 = broken)
            if( mod_hp( part, 0 - ( rng_float( hp_percent_loss_min * dist,
                                               hp_percent_loss_max * dist ) *
                                    part.info().durability ), DT_BASH ) ) {
                part.ammo_unset();
            }
        }
    }

    std::unique_ptr<RemovePartHandler> handler_ptr;
    // clear out any duplicated locations
    for( int p = static_cast<int>( parts.size() ) - 1; p >= 0; p-- ) {
        vehicle_part &part = parts[ p ];
        if( part.removed ) {
            continue;
        }
        std::vector<int> parts_here = parts_at_relative( part.mount, true );
        for( int other_i = static_cast<int>( parts_here.size() ) - 1; other_i >= 0; other_i -- ) {
            int other_p = parts_here[ other_i ];
            if( p == other_p ) {
                continue;
            }
            const vpart_info &p_info = part_info( p );
            const vpart_info &other_p_info = part_info( other_p );

            if( p_info.get_id() == other_p_info.get_id() ||
                ( !p_info.location.empty() && p_info.location == other_p_info.location ) ) {
                // Deferred creation of the handler to here so it is only created when actually needed.
                if( !handler_ptr ) {
                    // This is a heuristic: we just assume the default handler is good enough when called
                    // on the main game map. And assume that we run from some mapgen code if called on
                    // another instance.
                    if( g && &g->m == &m ) {
                        handler_ptr = std::make_unique<DefaultRemovePartHandler>();
                    } else {
                        handler_ptr = std::make_unique<MapgenRemovePartHandler>( m );
                    }
                }
                remove_part( other_p, *handler_ptr );
            }
        }
    }
}

int vehicle::lift_strength() const
{
    units::mass mass = total_mass();
    return std::max<std::int64_t>( mass / 10000_gram, 1 );
}

void vehicle::toggle_specific_engine( int e, bool on )
{
    // Special validation for muscle engines
    if( on && is_engine_type( e, fuel_type_muscle ) ) {
        std::string failure_reason;
        if( !can_enable_muscle_engine( e, failure_reason ) ) {
            add_msg( m_info, "%s", failure_reason.c_str() );
            return; // Don't enable the engine
        }
    }

    toggle_specific_part( engines[e], on );
}
void vehicle::toggle_specific_part( int p, bool on )
{
    parts[p].enabled = on;
}

bool vehicle::can_enable_muscle_engine( int e, std::string &failure_reason ) const
{
    const int part_idx = engines[e];
    const vpart_info &engine_info = part_info( part_idx );

    const player *passenger = get_passenger( part_idx );
    if( passenger != nullptr ) {
        if( engine_info.has_flag( "MUSCLE_LEGS" ) && passenger->get_working_leg_count() < 2 ) {
            failure_reason = string_format( _( "%s cannot operate the %s due to injured legs." ),
                                            passenger->name, engine_info.name() );
            return false;
        }
        if( engine_info.has_flag( "MUSCLE_ARMS" ) && passenger->get_working_arm_count() < 2 ) {
            failure_reason = string_format( _( "%s cannot operate the %s due to injured arms." ),
                                            passenger->name, engine_info.name() );
            return false;
        }
        return true;
    }

    failure_reason = string_format( _( "The %s cannot operate without an occupant." ),
                                    engine_info.name() );
    return false;
}

bool vehicle::has_muscle_engine_operator( int e ) const
{
    const int part_idx = engines[e];
    const vpart_info &engine_info = part_info( part_idx );

    const player *passenger = get_passenger( part_idx );
    if( passenger != nullptr ) {
        if( engine_info.has_flag( "MUSCLE_LEGS" ) && passenger->get_working_leg_count() >= 2 ) {
            return true;
        }
        if( engine_info.has_flag( "MUSCLE_ARMS" ) && passenger->get_working_arm_count() >= 2 ) {
            return true;
        }
    }
    return false;
}

void vehicle::validate_muscle_engines()
{
    for( size_t e = 0; e < engines.size(); e++ ) {
        if( is_engine_type( e, fuel_type_muscle ) && is_engine_on( e ) ) {
            if( !has_muscle_engine_operator( e ) ) {
                const int part_idx = engines[e];
                const vpart_info &engine_info = part_info( part_idx );
                parts[part_idx].enabled = false;
                add_msg( m_info, _( "The %s shuts down - it cannot operate without an occupant." ),
                         engine_info.name() );
            }
        }
    }
}
bool vehicle::is_engine_type_on( int e, const itype_id &ft ) const
{
    return is_engine_on( e ) && is_engine_type( e, ft );
}

bool vehicle::has_engine_type( const itype_id &ft, const bool enabled ) const
{
    for( size_t e = 0; e < engines.size(); ++e ) {
        if( is_engine_type( e, ft ) && ( !enabled || is_engine_on( e ) ) ) {
            return true;
        }
    }
    return false;
}
bool vehicle::has_engine_type_not( const itype_id &ft, const bool enabled ) const
{
    for( size_t e = 0; e < engines.size(); ++e ) {
        if( !is_engine_type( e, ft ) && ( !enabled || is_engine_on( e ) ) ) {
            return true;
        }
    }
    return false;
}

bool vehicle::has_engine_conflict( const vpart_info *possible_conflict,
                                   std::string &conflict_type ) const
{
    std::vector<std::string> new_excludes = possible_conflict->engine_excludes();
    // skip expensive string comparisons if there are no exclusions
    if( new_excludes.empty() ) {
        return false;
    }

    bool has_conflict = false;

    for( int engine : engines ) {
        std::vector<std::string> install_excludes = part_info( engine ).engine_excludes();
        std::vector<std::string> conflicts;
        std::set_intersection( new_excludes.begin(), new_excludes.end(), install_excludes.begin(),
                               install_excludes.end(), back_inserter( conflicts ) );
        if( !conflicts.empty() ) {
            has_conflict = true;
            conflict_type = conflicts.front();
            break;
        }
    }
    return has_conflict;
}

bool vehicle::is_engine_type( const int e, const itype_id  &ft ) const
{
    auto engine_fuel = parts[engines[e]].info().engine_fuel_opts();
    bool engine_has_fuel = std::find( engine_fuel.begin(), engine_fuel.end(), ft ) != engine_fuel.end();
    return parts[engines[e]].ammo_current().is_null() ? engine_has_fuel :
           parts[engines[e]].ammo_current() == ft;
}

bool vehicle::is_perpetual_type( const int e ) const
{
    const itype_id  &ft = part_info( engines[e] ).fuel_type;
    //TODO!: push up
    return item::spawn_temporary( ft )->has_flag( flag_PERPETUAL );
}

bool vehicle::is_engine_on( const int e ) const
{
    return parts[ engines[ e ] ].is_available() && is_part_on( engines[ e ] );
}

bool vehicle::is_part_on( const int p ) const
{
    const auto &pt = parts[p];
    return pt.enabled || ( pt.is_available() && pt.info().has_flag( str_PERPETUAL ) );
}

bool vehicle::is_alternator_on( const int a ) const
{
    auto &alt = parts[ alternators [ a ] ];
    if( alt.is_unavailable() ) {
        return false;
    }

    return std::ranges::any_of( engines, [this, &alt]( int idx ) {
        const auto &eng = parts [ idx ];
        //fuel_left checks that the engine can produce power to be absorbed
        return eng.is_available() && eng.enabled && fuel_left( eng.fuel_current() ) &&
               eng.mount == alt.mount && !eng.faults().contains( fault_belt );
    } );
}

bool vehicle::has_security_working() const
{
    bool found_security = false;
    if( fuel_left( fuel_type_battery ) > 0 ) {
        const auto [c, s] = get_controls_and_security();
        found_security = s >= 0;
    }
    return found_security;
}

void vehicle::backfire( const int e ) const
{
    const int power = part_vpower_w( engines[e], true );
    const auto pos = bub_part_location( engines[e] );
    sounds::sound( pos, 40 + power / 10000, sounds::sound_t::movement,
                   // single space after the exclaimation mark because it does not end the sentence
                   //~ backfire sound
                   string_format( _( "a loud BANG! from the %s" ), // NOLINT(cata-text-style)
                                  parts[ engines[ e ] ].name() ), true, "vehicle", "engine_backfire" );
}

const vpart_info &vehicle::part_info( int index, bool include_removed ) const
{
    if( index < static_cast<int>( parts.size() ) ) {
    if( !parts[index].removed || include_removed ) {
            return parts[index].info();
        }
    }
    return vpart_id::NULL_ID().obj();
}

// engines & alternators all have power.
// engines provide, whilst alternators consume.
int vehicle::part_vpower_w( const int index, const bool at_full_hp ) const
{
    const vehicle_part &vp = parts[ index ];
    int pwr = vp.info().power;
    if( part_flag( index, VPFLAG_ENGINE ) ) {
        if( pwr == 0 ) {
            pwr = vhp_to_watts( vp.base->engine_displacement() );
        }
        if( vp.info().fuel_type == fuel_type_animal ) {
            monster *mon = get_pet( index );
            if( mon != nullptr && mon->has_effect( effect_harnessed ) ) {
                // An animal that can carry twice as much weight, can pull a cart twice as hard.
                pwr = mon->get_speed() * mon->get_size() * 3
                      * ( mon->get_mountable_weight_ratio() * 5 );
            } else {
                pwr = 0;
            }
        }
        ///\EFFECT_STR increases power produced for MUSCLE_* vehicles
        pwr += ( g->u.str_cur - 8 ) * part_info( index ).engine_muscle_power_factor();
        /// wind-powered vehicles have differing power depending on wind direction
        if( vp.info().fuel_type == fuel_type_wind ) {
            const weather_manager &weather = get_weather();
            int windpower = weather.windspeed;
            // We're dead in the water.
            if( windpower < 1 ) {
                pwr = 0;
            } else {
                // For gameplay purposes, permit adjusting sails enough to sail upwind so long as it's blowing at all.
                rl_vec2d windvec;
                double raddir = ( ( weather.winddirection + 180 ) % 360 ) * ( M_PI / 180 );
                windvec = windvec.normalized();
                windvec.y = -std::cos( raddir );
                windvec.x = std::sin( raddir );
                rl_vec2d fv = face_vec();
                // We want 0.5 multiplier at 90 degrees instead of 0.0, so add 0.5.
                double dot = windvec.dot_product( fv ) + 0.5;
                // We don't want negatives or over 100% power, however.
                dot = std::min( 1.0, std::max( 0.0, dot ) );
                int windeffectint = static_cast<int>( ( windpower * dot ) * 200 );
                pwr = pwr + windeffectint;
            }
        }
    }

    if( pwr < 0 ) {
        return pwr; // Consumers always draw full power, even if broken
    }
    if( at_full_hp ) {
        return pwr; // Assume full hp
    }
    // Damaged engines give less power, but some engines handle it better
    double health = parts[index].health_percent();
    // dpf is 0 for engines that scale power linearly with damage and
    // provides a floor otherwise
    float dpf = part_info( index ).engine_damaged_power_factor();
    double effective_percent = dpf + ( ( 1 - dpf ) * health );
    return static_cast<int>( pwr * effective_percent );
}

// alternators, solar panels, reactors, and accessories all have epower.
// alternators, solar panels, and reactors provide, whilst accessories consume.
// for motor consumption see @ref vpart_info::energy_consumption instead
int vehicle::part_epower_w( const int index ) const
{
    int e = part_info( index ).epower;
    if( e < 0 ) {
        return e; // Consumers always draw full power, even if broken
    }
    return e * parts[ index ].health_percent();
}

int vehicle::power_to_energy_bat( const int power_w, const time_duration &d ) const
{
    // Integrate constant epower (watts) over time to get units of battery energy
    // Thousands of watts over millions of seconds can happen, so 32-bit int
    // insufficient.
    int64_t energy_j = power_w * to_seconds<int64_t>( d );
    int energy_bat = energy_j / bat_energy_j;
    int sign = power_w >= 0 ? 1 : -1;
    // energy_bat remainder results in chance at additional charge/discharge
    energy_bat += x_in_y( std::abs( energy_j % bat_energy_j ), bat_energy_j ) ? sign : 0;
    return energy_bat;
}

int vehicle::vhp_to_watts( const int power_vhp )
{
    // Convert vhp units (0.5 HP ) to watts
    // Used primarily for calculating battery charge/discharge
    // TODO: convert batteries to use energy units based on watts (watt-ticks?)
    constexpr int conversion_factor = 373; // 373 watts == 1 power_vhp == 0.5 HP
    return power_vhp * conversion_factor;
}

bool vehicle::has_structural_part( const tripoint_mnt_veh &dp ) const
{
for( const int elem : parts_at_relative( dp, false ) ) {
    if( part_info( elem ).location == part_location_structure &&
            !part_info( elem ).has_flag( "PROTRUSION" ) ) {
            return true;
        }
    }
    return false;
}

bool vehicle::has_structural_or_extendable_part( const tripoint_mnt_veh &dp ) const
{
for( const int elem : parts_at_relative( dp, false ) ) {
    if( ( part_info( elem ).location == part_location_structure &&
              !part_info( elem ).has_flag( "PROTRUSION" ) ) ||
            part_info( elem ).has_flag( VPFLAG_EXTENDABLE ) ) {
            return true;
        }
    }
    return false;
}

/**
 * Returns whether or not the vehicle has a structural part queued for removal,
 * @return true if a structural is queue for removal, false if not.
 * */
bool vehicle::is_structural_part_removed() const
{
for( const vpart_reference &vp : get_all_parts() ) {
    if( vp.part().removed && vp.info().location == part_location_structure ) {
            return true;
        }
    }
    return false;
}

/**
 * Returns whether or not the vehicle part with the given id can be mounted in
 * the specified square.
 * @param dp The local coordinate to mount in.
 * @param id The id of the part to install.
 * @return true if the part can be mounted, false if not.
 */
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

/**
 * Performs a breadth-first search from one part to another, to see if a path
 * exists between the two without going through the excluded part. Used to see
 * if a part can be legally removed.
 * @param to The part to reach.
 * @param from The part to start the search from.
 * @param excluded_part The part that is being removed and, therefore, should not
 *        be included in the path.
 * @return true if a path exists without the excluded part, false otherwise.
 */
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

/**
 * Installs a part into this vehicle.
 * @param dp The coordinate of where to install the part.
 * @param id The string ID of the part to install. (see vehicle_parts.json)
 * @param force Skip check of whether we can mount the part here.
 * @return false if the part could not be installed, true otherwise.
 */
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

/**
 * Mark a part as removed from the vehicle.
 * @return bool true if the vehicle's 0,0 point shifted.
 */
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

// split the current vehicle into up to 3 new vehicles that do not connect to each other
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
            if( vp.part().passenger_id == passenger->getID() ) {
                passenger->setpos( vp.pos() );
            }
        }
    }
}

// Split a vehicle into an old vehicle and one or more new vehicles by moving vehicle_parts
// from one the old vehicle to the new vehicles.
// some of the logic borrowed from remove_part
// skipped the grab, curtain, player activity, and engine checks because they deal
// with pos, not a vehicle pointer
// @param new_vehs vector of vectors of part indexes to move to new vehicles
// @param new_vehicles vector of vehicle pointers containing the new vehicles; if empty, new
// vehicles will be created
// @param new_mounts vector of vector of mount points. must have one vector for every vehicle*
// in new_vehicles, and forces the part indices in new_vehs to be mounted on the new vehicle
// at those mount points
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

item &vehicle::part_base( int p )
{
    return *parts[ p ].base;
}


void vehicle::update_overmap( const tripoint_abs_sm &prev_sm )
{
    if( !tracking_on ) {
        return;
    }
    const auto prev_omt = project_to<coords::omt>( prev_sm );
    if( project_to<coords::omt>( abs_sm_pos ) == prev_omt ) {
        return;
    }
    get_overmapbuffer( dimension_id_ ).move_vehicle( this, prev_omt.xy() );
}

units::mass vehicle::total_mass() const
{
    if( mass_dirty ) {
    refresh_mass();
    }

    return mass_cache;
}

units::volume vehicle::total_folded_volume() const
{
    units::volume m = 0_ml;
    for( const vpart_reference &vp : get_all_parts() ) {
        if( vp.part().removed ) {
            continue;
        }
        m += vp.info().folded_volume;
    }
    return m;
}

tripoint_mnt_veh vehicle::rotated_center_of_mass() const
{
    // TODO: Bring back caching of this point
    calc_mass_center( true );

    return mass_center_precalc;
}

tripoint_mnt_veh vehicle::local_center_of_mass() const
{
    if( mass_center_no_precalc_dirty ) {
    calc_mass_center( false );
    }

    return mass_center_no_precalc;
}

tripoint_rel_ms vehicle::pivot_displacement() const
{
    // precalc_mounts always produces a result that puts the pivot point at (0,0).
    // If the pivot point changes, this artificially moves the vehicle, as the position
    // of the old pivot point will appear to move from (posx+0, posy+0) to some other point
    // (posx+dx,posy+dy) even if there is no change in vehicle position or rotation.
    // This method finds that movement so it can be canceled out when actually moving
    // the vehicle.

    // rotate the old pivot point around the new pivot point with the old rotation angle
    return rotate_to_world( pivot_rotation[0], pivot_anchor[1], pivot_anchor[0] );
}

int vehicle::fuel_left( const itype_id &ftype, bool recurse ) const
{
    int fl = std::accumulate( parts.begin(), parts.end(), 0, [&ftype]( const int &lhs,
    const vehicle_part & rhs ) {
        return lhs + ( rhs.ammo_current() == ftype ? rhs.ammo_remaining() : 0 );
    } );

    if( recurse && ftype == fuel_type_battery ) {
        using tvr = distribution_graph::traverse_visitor_result;
        auto fuel_counting_visitor = [&fl, &ftype]( vehicle const & veh ) {
            fl += veh.fuel_left( ftype, false );
            return tvr::continue_further;
        };
        auto power_counting_visitor = [&fl]( distribution_grid const & grid ) {
            fl += grid.get_resource( false );
            return tvr::continue_further;
        };

        distribution_graph::traverse( *this, fuel_counting_visitor, power_counting_visitor );
    }

    if( ftype == fuel_type_muscle ) {
        int active_operators = 0;
        for( int ep = 0; ep < static_cast<int>( engines.size() ); ep++ ) {
            if( is_engine_type( ep, fuel_type_muscle ) && is_engine_on( ep ) ) {
                if( has_muscle_engine_operator( ep ) ) {
                    active_operators++;
                }
            }
        }
        if( active_operators > 0 ) {
            fl += 10 * active_operators;
        }
        // As do any other engine flagged as perpetual
        //TODO!: push up
    } else if( item::spawn_temporary( ftype )->has_flag( flag_PERPETUAL ) ) {
        fl += 10;
    }

    return fl;
}

int vehicle::static_drag( bool actual ) const
{
    return extra_drag + ( actual && !engine_on && !is_towed() && brake_hold ? -1500 : 0 );
}

float vehicle::strain() const
{
    int mv = max_velocity();
    int sv = safe_velocity();
    if( mv <= sv ) {
        mv = sv + 1;
    }
    if( velocity < sv && velocity > -sv ) {
        return 0;
    } else {
        return static_cast<float>( std::abs( velocity ) - sv ) / static_cast<float>( mv - sv );
    }
}

bool vehicle::sufficient_wheel_config() const
{
    if( wheelcache.empty() ) {
    // No wheels!
    return false;
} else if( wheelcache.size() == 1 ) {
    //Has to be a stable wheel, and one wheel can only support a 1-3 tile vehicle
    if( !part_info( wheelcache.front() ).has_flag( "STABLE" ) ||
            all_parts_at_location( part_location_structure ).size() > 3 ) {
            return false;
        }
    }
    return true;
}

bool vehicle::is_owned_by( const Character &c, bool available_to_take ) const
{
    if( owner.is_null() ) {
    return available_to_take;
}
if( !c.get_faction() ) {
    debugmsg( "vehicle::is_owned_by() player %s has no faction", c.disp_name() );
        return false;
    }
    return c.get_faction()->id() == get_owner();
}

bool vehicle::is_old_owner( const Character &c, bool available_to_take ) const
{
    if( old_owner.is_null() ) {
    return available_to_take;
}
if( !c.get_faction() ) {
    debugmsg( "vehicle::is_old_owner() player %s has no faction", c.disp_name() );
        return false;
    }
    return c.get_faction()->id() == get_old_owner();
}

std::string vehicle::get_owner_name() const
{
    if( !g->faction_manager_ptr->get( owner ) ) {
    debugmsg( "vehicle::get_owner_name() vehicle %s has no valid nor null faction id ", disp_name() );
        return _( "no owner" );
    }
    return _( g->faction_manager_ptr->get( owner )->name() );
}

bool vehicle::has_owner() const
{
    return !owner.is_null();
}

faction_id vehicle::get_owner() const
{
    return owner;
}

void vehicle::set_owner( const Character &c )
{
    const auto faction = c.get_faction();
    if( !faction ) {
        debugmsg( "vehicle::set_owner() player %s has no valid faction",
                  c.disp_name() );
        return;
    }
    owner = faction->id();
}

void vehicle::set_owner( const faction_id &new_owner )
{
    owner = new_owner;
}

void vehicle::remove_owner()
{
    owner = faction_id::NULL_ID();
}

bool vehicle::has_old_owner() const
{
    return !old_owner.is_null();
}

faction_id vehicle::get_old_owner() const
{
    return old_owner;
}

void vehicle::set_old_owner( const faction_id &temp_owner )
{
    theft_time = calendar::turn;
    old_owner = temp_owner;
}

void vehicle::remove_old_owner()
{
    theft_time = std::nullopt;
    old_owner = faction_id::NULL_ID();
}

bool vehicle::handle_potential_theft( avatar &you, bool check_only, bool prompt )
{
    const bool is_owned_by_player = is_owned_by( you );
    bool has_witnesses;
    if( has_owner() && !is_owned_by_player ) {
        has_witnesses = !avatar_funcs::list_potential_theft_witnesses( you, get_owner() ).empty();
    } else {
        has_witnesses = false;
    }
    // the vehicle is yours, that's fine.
    if( is_owned_by_player ) {
        return true;
        // if There is no owner
        // handle transfer of ownership
    }

    // if we are just checking if we could continue without problems, then the rest is assumed false
    if( check_only ) {
        return false;
    }

    if( !has_owner() ) {
        set_owner( you.get_faction()->id() );
        remove_old_owner();
        return true;
        // if there is a marker for having been stolen, but 15 minutes have passed, then officially transfer ownership
    }

    if( !has_witnesses && has_old_owner() ) {
        if( !is_old_owner( you ) && theft_time && calendar::turn - *theft_time > 15_minutes ) {
            set_owner( you.get_faction()->id() );
            remove_old_owner();
        }
        return true;
        // No witnesses? then don't need to prompt, we assume the player is in process of stealing it.
        // Ownership transfer checking is handled above, and warnings handled below.
        // This is just to perform interaction with the vehicle without a prompt.
        // It will prompt first-time, even with no witnesses, to inform player it is owned by someone else
        // subsequently, no further prompts, the player should know by then.
    }

    // if we got here, there's some theft occurring
    if( prompt ) {
        if( !query_yn(
                _( "This vehicle belongs to: %s, there may be consequences if you are observed interacting with it, continue?" ),
                _( get_owner_name() ) ) ) {
            return false;
        }
    }
    // set old owner so that we can restore ownership if there are witnesses.
    set_old_owner( get_owner() );
    if( avatar_funcs::handle_theft_witnesses( you, get_owner() ) ) {
        // remove the temporary marker for a successful theft, as it was witnessed.
        remove_old_owner();
    }
    // if we got here, then the action will proceed after the previous warning
    return true;
}

bool vehicle::balanced_wheel_config() const
{
    auto min = tripoint_mnt_veh( tripoint_max );
    auto max = tripoint_mnt_veh( tripoint_min );
    // find the bounding box of the wheels
    for( auto &w : wheelcache ) {
        const auto &pt = parts[ w ].mount;
        min.x() = std::min( min.x(), pt.x() );
        min.y() = std::min( min.y(), pt.y() );
        max.x() = std::max( max.x(), pt.x() );
        max.y() = std::max( max.y(), pt.y() );
        min.z() = std::min( min.z(), pt.z() );
        max.z() = std::max( max.z(), pt.z() );
    }

    // Check center of mass inside support of wheels (roughly)
    const inclusive_cuboid<tripoint_mnt_veh> support( min, max );
    return support.contains( local_center_of_mass() );
}

bool vehicle::valid_wheel_config() const
{
    return sufficient_wheel_config() && balanced_wheel_config();
}

float vehicle::steering_effectiveness() const
{
    if( is_floating ) {
    // I'M ON A BOAT
    return can_float() ? 1.0f : 0.0f;
    }
    if( is_flying || has_sufficient_lift( true ) ) {
        // I'M IN THE AIR
        // May need to add a separate check for planes, if/when they happen
        return is_aircraft() ? 1.0f : 0.0f;
    }
    // irksome special case for boats in shallow water
    if( is_watercraft() && can_float() ) {
        return 1.0f;
    }

    if( steering.empty() ) {
        return -1.0f; // No steering installed
    }
    // If the only steering part is an animal harness, with no animal in, it
    // is not steerable.
    const vehicle_part &vp = parts[ steering[0] ];
    if( steering.size() == 1 && vp.info().fuel_type == fuel_type_animal ) {
        monster *mon = get_pet( steering[0] );
        if( mon == nullptr || !mon->has_effect( effect_harnessed ) ) {
            return -2.0f;
        }
    }
    // For now, you just need one wheel working for 100% effective steering.
    // TODO: return something less than 1.0 if the steering isn't so good
    // (unbalanced, long wheelbase, back-heavy vehicle with front wheel steering,
    // etc)
for( int p : steering ) {
    if( parts[ p ].is_available() ) {
            return 1.0f;
        }
    }

    // We have steering, but it's all broken.
    return 0.0f;
}

float vehicle::handling_difficulty() const
{
    const float steer = std::max( 0.0f, steering_effectiveness() );
    const float ktraction = k_traction( g->m.vehicle_wheel_traction( *this ) );
    const float aligned = std::max( 0.0f, 1.0f - ( face_vec() - dir_vec() ).magnitude() );

    // TestVehicle: perfect steering, moving on road at 100 mph (25 tiles per turn) = 0.0
    // TestVehicle but on grass (0.75 friction) = 2.5
    // TestVehicle but with bad steering (0.5 steer) = 5
    // TestVehicle but on fungal bed (0.5 friction) and bad steering = 10
    // TestVehicle but turned 90 degrees during this turn (0 align) = 10
    const float diff_mod = ( ( 1.0f - steer ) + ( 1.0f - ktraction ) + ( 1.0f - aligned ) );
    return velocity * diff_mod / vehicles::cmps_per_tile;
}

std::map<itype_id, int> vehicle::fuel_usage() const
{
    std::map<itype_id, int> ret;
    for( size_t i = 0; i < engines.size(); i++ ) {
        // Note: functions with "engine" in name do NOT take part indices
        // TODO: Use part indices and not engine vector indices
        if( !is_engine_on( i ) ) {
            continue;
        }

        const size_t e = engines[ i ];
        const auto &info = part_info( e );
        static const itype_id null_fuel_type( "null" );
        const itype_id &cur_fuel = parts[ e ].fuel_current();
        if( cur_fuel  == null_fuel_type ) {
            continue;
        }

        if( !is_perpetual_type( i ) ) {
            int usage = info.energy_consumption;
            if( parts[ e ].faults().contains( fault_filter_air ) ) {
                usage *= 2;
            }

            ret[ cur_fuel ] += usage;
        }
    }

    return ret;
}

double vehicle::drain_energy( const itype_id &ftype, double energy_j )
{
    // Consumption of battery power is done differently.
    // From all batteries at once and doesn't change mass.
    if( ftype == fuel_type_battery ) {
        // Batteries stored in kilojoules
        const int total_kj_to_drain = static_cast<int>( energy_j / 1000.0 );
        if( total_kj_to_drain <= 0 ) {
            return 0.0;
        }
        const int not_fulfilled = discharge_battery( total_kj_to_drain );
        return static_cast<double>( total_kj_to_drain - not_fulfilled ) * 1000.0;
    }

    double drained = 0.0f;
    for( auto &p : parts ) {
        if( energy_j <= 0.0f ) {
            break;
        }

        const double consumed = p.consume_energy( ftype, energy_j );
        drained += consumed;
        energy_j -= consumed;
    }

    invalidate_mass();
    return drained;
}

void vehicle::consume_fuel( int load, const int t_seconds, bool skip_electric )
{
    double st = strain();
    if( current_acceleration() == 0 ) {
        return;
    }
    for( auto &fuel_pr : fuel_usage() ) {
        auto &ft = fuel_pr.first;
        if( skip_electric && ft == fuel_type_battery ) {
            continue;
        }

        double amnt_precise_j = static_cast<double>( fuel_pr.second ) * t_seconds;
        amnt_precise_j *= load / 1000.0 * ( 1.0 + st * st * 100.0 );
        auto inserted = fuel_used_last_turn.insert( { ft, 0.0f } );
        inserted.first->second += amnt_precise_j;
        double remainder = fuel_remainder[ ft ];
        amnt_precise_j -= remainder;

        if( amnt_precise_j > 0.0f ) {
            fuel_remainder[ ft ] = drain_energy( ft, amnt_precise_j ) - amnt_precise_j;
        } else {
            fuel_remainder[ ft ] = -amnt_precise_j;
        }
    }
    // we want this to update the activity level whenever the engine is running
    if( load > 0 && fuel_left( fuel_type_muscle ) > 0 ) {
        //do this as a function of current load
        // But only if the player is actually there!
        int eff_load = load / 10;
        int mod = 4 * st; // strain
        int base_burn = static_cast<int>( get_option<float>( "PLAYER_BASE_STAMINA_REGEN_RATE" ) ) -
                        3;
        base_burn = std::max( eff_load / 3, base_burn );

        // Check if player is contributing muscle power
        const bool npc_needs_enabled = !get_option<bool>( "NO_NPC_FOOD" );
        for( vpart_reference vp : get_enabled_parts( VPFLAG_ENGINE ) ) {
            if( vp.info().fuel_type == fuel_type_muscle ) {
                player *p = get_passenger( vp.part_index() );
                if( p && ( ( vp.info().has_flag( "MUSCLE_LEGS" ) && ( p->get_working_leg_count() >= 2 ) ) ||
                           ( vp.info().has_flag( "MUSCLE_ARMS" ) && ( p->get_working_arm_count() >= 2 ) ) ) ) {
                    const item &muscle = *item::spawn_temporary( "muscle" );
                    for( const bionic_id &bid : p->get_bionic_fueled_with( muscle ) ) {
                        if( p->has_active_bionic( bid ) ) { // active power gen
                            // more pedaling = more power
                            p->mod_power_level( units::from_kilojoule( muscle.fuel_energy() ) * bid->fuel_efficiency *
                                                ( load / 1000.0f ) );
                            mod += eff_load / 5;
                        } else { // passive power gen
                            p->mod_power_level( units::from_kilojoule( muscle.fuel_energy() ) * bid->passive_fuel_efficiency *
                                                ( load / 1000.0f ) );
                            mod += eff_load / 10;
                        }
                    }
                    // decreased stamina burn scalable with load
                    if( p->has_active_bionic( bio_jointservo ) ) {
                        p->mod_power_level( -bio_jointservo->power_trigger * std::max( eff_load / 20, 1 ) );
                        mod -= std::max( eff_load / 5, 5 );
                    }
                    if( p->is_player() || npc_needs_enabled ) {
                        if( one_in( 1000 / load ) && one_in( 10 ) ) {
                            p->mod_stored_kcal( -10 );
                            p->mod_thirst( 1 );
                            p->mod_fatigue( 1 );
                        }
                        p->mod_stamina( -( base_burn + mod ) );
                        add_msg( m_debug, "Load: %d", load );
                        add_msg( m_debug, "Mod: %d", mod );
                        add_msg( m_debug, "Burn: %d", -( base_burn + mod ) );
                    }
                }
            }
        }
    }
}


vehicle *vehicle::find_vehicle( const tripoint_abs_ms &where )
{
    return find_vehicle( where, MAPBUFFER_REGISTRY.get( get_map().get_bound_dimension() ) );
}

vehicle *vehicle::find_vehicle( const tripoint_abs_ms &where, mapbuffer &mbuf )
{
    // Is it in the reality bubble?
    auto veh_local = g->m.abs_to_bub( where );
    if( const optional_vpart_position vp = g->m.veh_at( veh_local ) ) {
        return &vp->vehicle();
    }

    // Nope. Load up its submap...
    auto proj = project_remain<coords::sm>( where );
    auto sm = mbuf.lookup_submap( proj.quotient_tripoint );
    if( sm == nullptr ) {
        return nullptr;
    }

    for( auto &elem : sm->vehicles ) {
        vehicle *found_veh = elem.get();
        if( proj.remainder == found_veh->sm_ms_pos ) {
            return found_veh;
        }
    }

    return nullptr;
}

void vehicle::enumerate_vehicles( std::map<vehicle *, bool> &connected_vehicles,
                                  const std::set<vehicle *> &vehicle_list )
{
    auto enumerate_visitor = [&connected_vehicles]( vehicle & veh ) {
        // Only emplaces if element is not present already.
        connected_vehicles.emplace( &veh, false );
        return distribution_graph::traverse_visitor_result::continue_further;
    };
    for( vehicle *veh : vehicle_list ) {
        // This autovivifies, and also overwrites the value if already present.
        connected_vehicles[veh] = true;
        distribution_graph::traverse( *veh, enumerate_visitor, distribution_graph::noop_visitor_grid );
    }
}

// TODO: It looks out of place in vehicle.cpp
namespace distribution_graph
{

template <bool IsConst,
          typename Vehicle = std::conditional_t<IsConst, const vehicle, vehicle>,
          typename Grid = std::conditional_t<IsConst, const distribution_grid, distribution_grid>>
struct vehicle_or_grid {
    enum class type_t : char {
        vehicle,
        grid
    } type;

    Vehicle *veh = nullptr;
    Grid *grid = nullptr;

    vehicle_or_grid( Vehicle *veh )
        : type( type_t::vehicle )
        , veh( veh )
    {}

    vehicle_or_grid( Grid *grid )
        : type( type_t::grid )
        , grid( grid )
    {}

    bool operator==( const vehicle_or_grid &other ) const {
        return veh == other.veh && grid == other.grid;
    }

    bool operator==( const vehicle *veh ) const {
        return this->veh == veh;
    }

    bool operator==( const distribution_grid *grid ) const {
        return this->grid == grid;
    }
};

template <typename VehFunc, typename GridFunc, typename StartPoint>
void traverse( StartPoint &start,
               VehFunc veh_action, GridFunc grid_action )
{
    using tvr = traverse_visitor_result;
    constexpr bool IsConst = std::is_const_v<StartPoint>;
    struct hash {
        const std::hash<char> char_hash = std::hash<char>();
        const std::hash<size_t> ptr_hash = std::hash<size_t>();
        auto operator()( const vehicle_or_grid<IsConst> &elem ) const {
            return char_hash( static_cast<char>( elem.type ) ) ^
            ptr_hash(
            // Because only one of pointers is not nullptr, binary OR would get value of set pointer.
            reinterpret_cast<size_t>( elem.veh ) | reinterpret_cast<size_t>( elem.grid )
            );
        }
    };

    // Actually, they are visited elements with unvisited neighbours.
    // Not all connected elements are here.
    std::queue<vehicle_or_grid<IsConst>> connected_elements;
    // For fast checking if we should visit some neighbour.
    std::unordered_set<vehicle_or_grid<IsConst>, hash> visited_elements;
    connected_elements.emplace( &start );
    visited_elements.insert( &start );
    auto &grid_tracker = get_distribution_grid_tracker();

    auto enqueue = [&connected_elements, &visited_elements]( vehicle_or_grid<IsConst> newly_visited ) {
        connected_elements.push( newly_visited );
        visited_elements.insert( newly_visited );
    };
    auto was_already_visited = [&visited_elements]( vehicle_or_grid<IsConst> to_visit ) {
        return visited_elements.count( to_visit ) != 0;
    };

    auto process_vehicle = [&]( const tripoint_abs_ms & target_pos ) {
        auto *veh = vehicle::find_vehicle( target_pos );
        if( veh == nullptr ) {
            debugmsg( "lost vehicle at %s", target_pos.to_string() );
            return tvr::continue_further;
        }

        if( was_already_visited( veh ) ) {
            return tvr::continue_further;
        }

        const tvr result = veh_action( *veh );
        g->u.add_msg_if_player( m_debug, "After remote veh %p",
                                static_cast<void *>( veh ) );

        // We do not need to check neighbours if we stop.
        if( result == tvr::continue_further ) {
            enqueue( veh );
        }

        return result;
    };

    auto process_grid = [&]( const tripoint_abs_ms & target_pos ) {
        auto &grid = grid_tracker.grid_at( target_pos );
        if( !grid ) {
            debugmsg( "lost grid at %s", target_pos.to_string() );
            return tvr::continue_further;
        }

        if( was_already_visited( &grid ) ) {
            return tvr::continue_further;
        }

        const tvr result = grid_action( grid );
        g->u.add_msg_if_player( m_debug, "After remote grid %p",
                                static_cast<void *>( &grid ) );

        // We do not need to check neighbours if we stop.
        if( result == tvr::continue_further ) {
            enqueue( &grid );
        }

        return result;
    };

    while( !connected_elements.empty() ) {
        auto current = connected_elements.front();
        connected_elements.pop();

        if( current.type == vehicle_or_grid<IsConst>::type_t::vehicle ) {
            const vehicle &current_veh = *current.veh;
            for( auto &p : current_veh.loose_parts ) {
                if( !current_veh.part_info( p ).has_flag( "POWER_TRANSFER" ) ) {
                    // Ignore loose parts that aren't power transfer cables
                    continue;
                }

                const tripoint_abs_ms target_pos( current_veh.cpart( p ).target.second );
                if( current_veh.cpart( p ).has_flag( vehicle_part::targets_grid ) ) {
                    if( process_grid( target_pos ) == tvr::stop ) {
                        return;
                    }
                } else {
                    if( process_vehicle( target_pos ) == tvr::stop ) {
                        return;
                    }
                }
            }
        } else {
            // Grids can only be connected to vehicles at the moment
            auto &current_grid = *current.grid;
            for( auto &p : current_grid.get_contents() ) {
                const vehicle_connector_tile *connector = active_tiles::furn_at<vehicle_connector_tile>( p );
                if( connector == nullptr ) {
                    continue;
                }

                for( const tripoint_abs_ms &target_pos : connector->connected_vehicles ) {
                    if( process_vehicle( target_pos ) == tvr::stop ) {
                        return;
                    }
                }
            }
        }
    }
}

} // namespace distribution_graph

int vehicle::charge_battery( int amount, bool include_other_vehicles )
{
    // Key parts by percentage charge level.
    std::multimap<int, vehicle_part *> chargeable_parts;
    for( vehicle_part &p : parts ) {
        if( p.is_available() && p.is_battery() && p.ammo_capacity() > p.ammo_remaining() ) {
            chargeable_parts.insert( { ( p.ammo_remaining() * 100 ) / p.ammo_capacity(), &p } );
        }
    }
    while( amount > 0 && !chargeable_parts.empty() ) {
        // Grab first part, charge until it reaches the next %, then re-insert with new % key.
        auto iter = chargeable_parts.begin();
        int charge_level = iter->first;
        vehicle_part *p = iter->second;
        chargeable_parts.erase( iter );
        // Calculate number of charges to reach the next %, but insure it's at least
        // one more than current charge.
        int next_charge_level = ( ( charge_level + 1 ) * p->ammo_capacity() ) / 100;
        next_charge_level = std::max( next_charge_level, p->ammo_remaining() + 1 );
        int qty = std::min( amount, next_charge_level - p->ammo_remaining() );
        p->ammo_set( fuel_type_battery, p->ammo_remaining() + qty );
        amount -= qty;
        if( p->ammo_capacity() > p->ammo_remaining() ) {
            chargeable_parts.insert( { ( p->ammo_remaining() * 100 ) / p->ammo_capacity(), p } );
        }
    }

    if( amount > 0 && include_other_vehicles ) {
        // still a bit of charge we could send out...
        using tvr = distribution_graph::traverse_visitor_result;
        auto charge_veh = [&amount]( vehicle & veh ) {
            g->u.add_msg_if_player( m_debug, "CHv: %d", amount );
            amount = veh.charge_battery( amount, false );
            return amount > 0 ? tvr::continue_further : tvr::stop;
        };
        auto charge_grid = [&amount]( distribution_grid & grid ) {
            g->u.add_msg_if_player( m_debug, "CHg: %d", amount );
            amount = grid.mod_resource( amount, false );
            return amount > 0 ? tvr::continue_further : tvr::stop;
        };
        distribution_graph::traverse( *this, charge_veh, charge_grid );
    }


    return amount;
}

int vehicle::discharge_battery( int amount, bool recurse )
{
    // Key parts by percentage charge level.
    std::multimap<int, vehicle_part *> dischargeable_parts;
    for( vehicle_part &p : parts ) {
        if( p.is_available() && p.is_battery() && p.ammo_remaining() > 0 ) {
            dischargeable_parts.insert( { ( p.ammo_remaining() * 100 ) / p.ammo_capacity(), &p } );
        }
    }
    while( amount > 0 && !dischargeable_parts.empty() ) {
        // Grab first part, discharge until it reaches the next %, then re-insert with new % key.
        auto iter = std::prev( dischargeable_parts.end() );
        int charge_level = iter->first;
        vehicle_part *p = iter->second;
        dischargeable_parts.erase( iter );
        // Calculate number of charges to reach the previous %.
        int prev_charge_level = ( ( charge_level - 1 ) * p->ammo_capacity() ) / 100;
        prev_charge_level = std::max( 0, prev_charge_level );
        int amount_to_discharge = std::min( p->ammo_remaining() - prev_charge_level, amount );
        p->ammo_consume( amount_to_discharge, bub_part_location( *p ) );
        amount -= amount_to_discharge;
        if( p->ammo_remaining() > 0 ) {
            dischargeable_parts.insert( { ( p->ammo_remaining() * 100 ) / p->ammo_capacity(), p } );
        }
    }

    if( amount > 0 && recurse ) {
        // need more power!
        using tvr = distribution_graph::traverse_visitor_result;
        auto discharge_vehicle = [&amount]( vehicle & veh ) {
            g->u.add_msg_if_player( m_debug, "CHv: %d", amount );
            amount = veh.discharge_battery( amount, false );
            return amount > 0 ? tvr::continue_further : tvr::stop;
        };
        auto discharge_grid = [&amount]( distribution_grid & grid ) {
            g->u.add_msg_if_player( m_debug, "CHg: %d", amount );
            amount = -grid.mod_resource( -amount, false );
            return amount > 0 ? tvr::continue_further : tvr::stop;
        };
        distribution_graph::traverse( *this, discharge_vehicle, discharge_grid );
    }

    return amount; // non-zero if we weren't able to fulfill demand.
}

void vehicle::do_engine_damage( size_t e, int strain )
{
    strain = std::min( 25, strain );
    if( is_engine_on( e ) && !is_perpetual_type( e ) &&
        engine_fuel_left( e ) && rng( 1, 100 ) < strain ) {
        int dmg = rng( 0, strain * 4 );
        damage_direct( engines[e], dmg );
        if( one_in( 2 ) ) {
            add_msg( _( "Your engine emits a high pitched whine." ) );
        } else {
            add_msg( _( "Your engine emits a loud grinding sound." ) );
        }
    }
}

void vehicle::idle( bool on_map )
{
    power_parts();
    // Validate muscle engines - auto-disable if conditions are not met
    validate_muscle_engines();
    if( engine_on && total_power_w() > 0 ) {
        bool no_electric_power = true;
        int idle_rate = alternator_load;
        if( idle_rate < 10 ) {
            idle_rate = 10;    // minimum idle is 1% of full throttle
        }
        // Helicopters use extra power just to stay in the air
        // 100 means 10% of power
        /*
            TODO: Consider different formula for idling aircraft, may need a formula to determine this
            Possibly something like total lift / total engine power, maybe some factors for hovering efficiency of different types
            Also consider adding a hover efficiency field
        */
        if( is_rotorcraft() && is_flying_in_air() ) {
            const auto rotor_newtons = std::max( 0.0,
                                                 to_newton( total_mass() ) - total_balloon_lift() - total_wing_lift() );
            const auto rotor_capacity = rotor_newtons / thrust_of_rotorcraft( true );
            idle_rate = std::max( 10, int( std::floor( 100 * rotor_capacity ) ) );
            no_electric_power = false;
        }
        if( has_engine_type_not( fuel_type_muscle, true ) ) {
            consume_fuel( idle_rate, to_turns<int>( 1_turns ), no_electric_power );
        }

        if( on_map ) {
            noise_and_smoke( idle_rate, 1_turns );
        }
    } else {
        if( engine_on && g->u.sees( bub_ms_location() ) &&
            ( has_engine_type_not( fuel_type_muscle, true ) && has_engine_type_not( fuel_type_animal, true ) &&
              has_engine_type_not( fuel_type_wind, true ) && has_engine_type_not( fuel_type_mana, true ) ) ) {
            add_msg( _( "The %s's engine dies!" ), name );
        }
        engine_on = false;
    }

    // Disallow running a planter underground for now
    if( !warm_enough_to_plant( g->u.abs_pos() ) || abs_ms_location().z() < 0 ) {
        for( const vpart_reference &vp : get_enabled_parts( "PLANTER" ) ) {
            if( g->u.sees( bub_ms_location() ) ) {
                add_msg( _( "The %s's planter turns off due to low temperature." ), name );
            }
            vp.part().enabled = false;
        }
    }

    if( !on_map ) {
        return;
    } else {
        update_time( calendar::turn );
    }

    process_emitters();

    if( has_part( "STEREO", true ) ) {
        play_music();
    }

    if( has_part( "CHIMES", true ) ) {
        play_chimes();
    }

    if( has_part( "CRASH_TERRAIN_AROUND", true ) ) {
        crash_terrain_around();
    }

    if( is_alarm_on ) {
        alarm();
    }

    // V-3: skip the full part scan when no AUTOLOADER parts are installed.
    if( has_autoloaders ) {
        vehicle_funcs::process_autoloaders( *this );
    }
}

void vehicle::on_move()
{
    if( has_part( "TRANSFORM_TERRAIN", true ) ) {
        transform_terrain();
    }
    if( has_part( "SCOOP", true ) ) {
        operate_scoop();
    }
    if( has_part( "PLANTER", true ) ) {
        operate_planter();
    }
    if( has_part( "REAPER", true ) ) {
        operate_reaper();
    }

    occupied_cache_time = calendar::before_time_starts;
}

void vehicle::slow_leak()
{
    // for each badly damaged tanks (lower than 50% health), leak a small amount
    for( auto &p : parts ) {
        auto health = p.health_percent();
        if( !p.is_leaking() || p.ammo_remaining() <= 0 ) {
            continue;
        }

        auto fuel = p.ammo_current();
        int qty = std::max( ( 0.5 - health ) * ( 0.5 - health ) * p.ammo_remaining() / 10, 1.0 );
        const auto dest = mount_to_bubble( p.mount );

        if( fuel != fuel_type_battery && !g->m.inbounds( dest ) ) {
            // Don't try to leak off the edge of the world
            continue;
        }

        // damaged batteries self-discharge without leaking, plutonium leaks slurry
        if( fuel != fuel_type_battery && fuel != fuel_type_plutonium_cell ) {
            g->m.add_item_or_charges( dest, item::spawn( fuel, calendar::turn, qty ) );
            p.ammo_consume( qty, bub_part_location( p ) );
        } else if( fuel == fuel_type_plutonium_cell ) {
            if( p.ammo_remaining() >= PLUTONIUM_CHARGES / 10 ) {
                g->m.add_item_or_charges( dest, item::spawn( "plut_slurry_dense", calendar::turn, qty ) );
                p.ammo_consume( qty * PLUTONIUM_CHARGES / 10, bub_part_location( p ) );
            } else {
                p.ammo_consume( p.ammo_remaining(), bub_part_location( p ) );
            }
        } else {
            p.ammo_consume( qty, bub_part_location( p ) );
        }
    }
}

// total volume of all the things


bool vehicle::is_foldable() const
{
for( const vpart_reference &vp : get_all_parts() ) {
    if( !vp.has_feature( "FOLDABLE" ) ) {
            return false;
        }
    }
    return true;
}

bool vehicle::restore( const std::string &data )
{
    std::istringstream veh_data( data );
    try {
        JsonIn json( veh_data );
        parts.clear();
        json.read( parts );
    } catch( const JsonError &e ) {
        debugmsg( "Error restoring vehicle: %s", e.c_str() );
        return false;
    }
    for( vehicle_part &part : parts ) {
        part.hack_id = get_next_hack_id();
    }
    refresh_locations_hack();
    refresh();
    face.init( 0_degrees );
    turn_dir = 0_degrees;
    turn( 0_degrees );
    precalc_mounts( 0, pivot_rotation[0], pivot_anchor[0] );
    precalc_mounts( 1, pivot_rotation[1], pivot_anchor[1] );
    last_update = calendar::turn;
    return true;
}

std::set<tripoint_abs_ms> &vehicle::get_points( const bool force_refresh )
{
    if( force_refresh || occupied_cache_time != calendar::turn ) {
        occupied_cache_time = calendar::turn;
        occupied_points.clear();
        for( const auto &p : parts ) {
            occupied_points.insert( g->m.bub_to_abs( bub_part_location( p ) ) );
        }
    }

    return occupied_points;
}

vehicle_part &vpart_reference::part() const
{
    assert( part_index() < static_cast<size_t>( vehicle().part_count() ) );
    return vehicle().part( part_index() );
}

const vpart_info &vpart_reference::info() const
{
    return part().info();
}

player *vpart_reference::get_passenger() const
{
    return vehicle().get_passenger( part_index() );
}

tripoint_mnt_veh vpart_position::mount() const
{
    return vehicle().part( part_index() ).mount;
}

tripoint_bub_ms vpart_position::pos() const
{
    return vehicle().bub_part_location( part_index() );
}

bool vpart_reference::has_feature( const std::string &f ) const
{
    return info().has_flag( f );
}

bool vpart_reference::has_feature( const vpart_bitflags f ) const
{
    return info().has_flag( f );
}

static bool is_sm_tile_over_water( const tripoint_abs_ms &pos )
{
    const auto proj = project_remain<coords::sm>( pos );
    auto &mbuf = MAPBUFFER_REGISTRY.get( get_map().get_bound_dimension() );
    auto sm = mbuf.lookup_submap( proj.quotient_tripoint );
    if( sm == nullptr ) {
        debugmsg( "is_sm_tile_over_water(): couldn't find submap %d,%d,%d",
                  proj.quotient_tripoint.x(), proj.quotient_tripoint.y(), proj.quotient_tripoint.z() );
        return false;
    }

    return ( sm->get_ter( proj.remainder ).obj().has_flag( TFLAG_CURRENT ) ||
             sm->get_furn( proj.remainder ).obj().has_flag( TFLAG_CURRENT ) );
}

static bool is_sm_tile_outside( const tripoint_abs_ms &pos )
{
    const auto proj = project_remain<coords::sm>( pos );
    auto &m = get_map();
    auto &mbuf = MAPBUFFER_REGISTRY.get( m.get_bound_dimension() );
    auto sm = mbuf.lookup_submap( proj.quotient_tripoint );
    if( sm == nullptr ) {
        debugmsg( "is_sm_tile_over_water(): couldn't find submap %d,%d,%d",
                  proj.quotient_tripoint.x(), proj.quotient_tripoint.y(), proj.quotient_tripoint.z() );
        return false;
    }

    return m.is_outside( m.abs_to_bub( pos ) );
}

void vehicle::update_time( const time_point &update_to )
{
    const time_point update_from = last_update;
    if( update_to < update_from ) {
        // Special case going backwards in time - that happens
        last_update = update_to;
        return;
    }

    if( update_to >= update_from && update_to - update_from < 1_minutes ) {
        // We don't need to check every turn
        return;
    }
    time_duration elapsed = update_to - last_update;
    last_update = update_to;

    if( !converters.empty() ) {
        for( int p : converters ) {
            const auto &part = parts[p];
            if( !part.is_unavailable() && part.enabled ) {
                int repeat = part.info().get_max_conversions() * to_seconds<int>( elapsed ) / 60;
                auto [ consume_type, consume_charges ] = part.info().get_conversion_input();
                auto [ output_type, output_charges ] = part.info().get_conversion_output();
                const item *output = item::spawn_temporary( output_type, calendar::turn, output_charges );
                vehicle_part *consume_tank = nullptr;
                if( !consume_type.is_null() ) {
                    auto consume_tank_idx = std::ranges::find_if( tanks, [&]( int tank ) {
                        const auto &part = parts[tank];
                        if( part.ammo_current() == consume_type && part.ammo_remaining() > consume_charges ) {
                            return true;
                        }
                        return false;
                    } );
                    if( consume_tank_idx == tanks.end() ) {
                        continue;
                    }
                    consume_tank = &parts[*consume_tank_idx];
                }
                vehicle_part *output_tank = nullptr;
                if( !output_type.is_null() ) {
                    auto output_tank_idx = std::ranges::find_if( tanks, [&]( int tank ) {
                        const auto &part = parts[tank];
                        if( part.can_reload( output ) && part.ammo_capacity() - part.ammo_remaining() > output_charges ) {
                            return true;
                        }
                        return false;
                    } );
                    if( output_tank_idx == tanks.end() ) {
                        continue;
                    }
                    output_tank = &parts[*output_tank_idx];
                }
                int max_repeats = repeat;
                if( consume_tank ) {
                    max_repeats = std::min( max_repeats, consume_tank->ammo_remaining() / consume_charges );
                }
                if( output_tank ) {
                    max_repeats = std::min( max_repeats,
                                            ( output_tank->ammo_capacity() - output_tank->ammo_remaining() ) / output_charges );
                }
                if( part.info().get_conversion_charges() > 0 ) {
                    max_repeats = std::min( max_repeats,
                                            fuel_left( itype_battery ) / part.info().get_conversion_charges() );
                }
                if( consume_tank != nullptr ) {
                    consume_tank->ammo_consume( max_repeats * consume_charges, bub_part_location( *consume_tank ) );
                }
                if( output_tank != nullptr ) {
                    output_tank->ammo_set( output_type, output_tank->ammo_remaining() + max_repeats * output_charges );
                }
                discharge_battery( part.info().get_conversion_charges() * max_repeats );
            }
        }
    }

    if( abs_sm_pos.z() < 0 ) {
        return;
    }

    // Weather stuff, only for z-levels >= 0
    // TODO: Have it wash cars from blood?
    if( funnels.empty() && solar_panels.empty() && wind_turbines.empty() && water_wheels.empty() ) {
        return;
    }
    // Get one weather data set per vehicle, they don't differ much across vehicle area
    const weather_sum accum_weather = sum_conditions( update_from, update_to,
                                      abs_ms_location() );
    // make some reference objects to use to check for reload
    const item *water = item::spawn_temporary( "water" );
    const item *water_clean = item::spawn_temporary( "water_clean" );

    for( int idx : funnels ) {
        const auto &pt = parts[idx];

        // we need an unbroken funnel mounted on the exterior of the vehicle
        if( pt.is_unavailable() || !is_sm_tile_outside( g->m.bub_to_abs( bub_part_location( pt ) ) ) ) {
            continue;
        }

        // we need an empty tank (or one already containing water) below the funnel
        auto tank = std::find_if( parts.begin(), parts.end(), [&]( const vehicle_part & e ) {
            return pt.mount == e.mount && e.is_tank() &&
                   ( e.can_reload( water ) || e.can_reload( water_clean ) );
        } );

        if( tank == parts.end() ) {
            continue;
        }

        double area = std::pow( pt.info().size / units::legacy_volume_factor, 2 ) * M_PI;
        int qty = roll_remainder( funnel_charges_per_turn( area, accum_weather.rain_amount ) );
        int c_qty = qty + ( tank->can_reload( water_clean ) ?  tank->ammo_remaining() : 0 );
        int cost_to_purify = c_qty * itype_water_purifier->charges_to_use();

        if( qty > 0 ) {
            if( has_part( bub_part_location( pt ), "WATER_PURIFIER", true ) &&
                ( fuel_left( itype_battery, true ) > cost_to_purify ) ) {
                tank->ammo_set( itype_water_clean, c_qty );
                discharge_battery( cost_to_purify );
            } else {
                tank->ammo_set( itype_water, tank->ammo_remaining() + qty );
            }
            invalidate_mass();
        }
    }

    if( !solar_panels.empty() ) {
        int epower_w = 0;
        for( int part : solar_panels ) {
            if( parts[ part ].is_unavailable() ) {
                continue;
            }

            if( !is_sm_tile_outside( g->m.bub_to_abs( bub_part_location( part ) ) ) ) {
                continue;
            }

            epower_w += part_epower_w( part );
        }
        double intensity = accum_weather.sunlight / default_daylight_level() / to_turns<double>( elapsed );
        int energy_bat = power_to_energy_bat( epower_w * intensity, elapsed );
        if( energy_bat > 0 ) {
            add_msg( m_debug, "%s got %d kJ energy from solar panels", name, energy_bat );
            charge_battery( energy_bat );
        }
    }
    if( !wind_turbines.empty() ) {
        // TODO: use accum_weather wind data to backfill wind turbine
        // generation capacity.
        int epower_w = total_wind_epower_w();
        int energy_bat = power_to_energy_bat( epower_w, elapsed );
        if( energy_bat > 0 ) {
            add_msg( m_debug, "%s got %d kJ energy from wind turbines", name, energy_bat );
            charge_battery( energy_bat );
        }
    }
    if( !water_wheels.empty() ) {
        int epower_w = total_water_wheel_epower_w();
        int energy_bat = power_to_energy_bat( epower_w, elapsed );
        if( energy_bat > 0 ) {
            add_msg( m_debug, "%s got %d kJ energy from water wheels", name, energy_bat );
            charge_battery( energy_bat );
        }
    }
}

void vehicle::process_emitters()
{
    // Parts emitting fields
    for( int idx : emitters ) {
        const vehicle_part &pt = parts[idx];
        if( pt.is_unavailable() || !pt.enabled ) {
            continue;
        }
        for( const emit_id &e : pt.info().emissions ) {
            g->m.emit_field( bub_part_location( pt ), e );
        }
    }
}

void vehicle::invalidate_mass()
{
    mass_dirty = true;
    mass_center_precalc_dirty = true;
    mass_center_no_precalc_dirty = true;
    // Anything that affects mass will also affect the pivot
    pivot_dirty = true;
    coeff_rolling_dirty = true;
    coeff_water_dirty = true;
}

void vehicle::refresh_mass() const
{
    calc_mass_center( true );
}

void vehicle::calc_mass_center( bool use_precalc ) const
{
    units::quantity<float, units::mass::unit_type> xf;
    units::quantity<float, units::mass::unit_type> yf;
    units::mass m_total = 0_gram;
    for( const vpart_reference &vp : get_all_parts() ) {
        const size_t i = vp.part_index();
        if( vp.part().removed ) {
            continue;
        }

        units::mass m_part = 0_gram;
        units::mass m_part_items = 0_gram;
        m_part += vp.part().base->weight();
        const int weight_modifier = vp.part().info().weight_modifier;
        const int cargo_weight_modifier = vp.part().info().cargo_weight_modifier;
        if( weight_modifier != 100 ) {
            m_part *= static_cast<float>( weight_modifier ) / 100.0f;
        }
        for( const auto &j : get_items( i ) ) {
            m_part_items += j->weight();
        }
        if( cargo_weight_modifier != 100 ) {
            m_part_items *= static_cast<float>( cargo_weight_modifier ) / 100.0f;
        }
        m_part += m_part_items;

        if( vp.has_feature( VPFLAG_BOARDABLE ) && vp.part().has_flag( vehicle_part::passenger_flag ) ) {
            const player *p = get_passenger( i );
            // Sometimes flag is wrongly set, don't crash!
            m_part += p != nullptr ? p->get_weight() : 0_gram;
        }

        if( use_precalc ) {
            xf += vp.part().precalc[0].x() * m_part;
            yf += vp.part().precalc[0].y() * m_part;
        } else {
            xf += vp.mount().x() * m_part;
            yf += vp.mount().y() * m_part;
        }

        m_total += m_part;
    }

    mass_cache = m_total;
    mass_dirty = false;

    const float x = xf / mass_cache;
    const float y = yf / mass_cache;
    if( use_precalc ) {
        mass_center_precalc.x() = std::round( x );
        mass_center_precalc.y() = std::round( y );
        mass_center_precalc_dirty = false;
    } else {
        mass_center_no_precalc.x() = std::round( x );
        mass_center_no_precalc.y() = std::round( y );
        mass_center_no_precalc_dirty = false;
    }
}

bounding_box vehicle::get_bounding_box( )
{
    int min_x = INT_MAX;
    int max_x = INT_MIN;
    int min_y = INT_MAX;
    int max_y = INT_MIN;

    set_facing( turn_dir );

    int i_use = 0;
    for( const auto &p : get_points( true ) ) {
        const auto pt = parts[part_at( p - abs_ms_location() )].precalc[i_use];
        if( pt.x() < min_x ) {
            min_x = pt.x();
        }
        if( pt.x() > max_x ) {
            max_x = pt.x();
        }
        if( pt.y() < min_y ) {
            min_y = pt.y();
        }
        if( pt.y() > max_y ) {
            max_y = pt.y();
        }
    }
    bounding_box b;
    b.p1 = point( min_x, min_y );
    b.p2 = point( max_x, max_y );
    return b;
}

vehicle_part_range vehicle::get_all_parts() const
{
    return vehicle_part_range( const_cast<vehicle &>( *this ) );
}

int vehicle::part_count() const
{
    return static_cast<int>( parts.size() );
}

vehicle_part &vehicle::part( int part_num )
{
    return parts[part_num];
}

const vehicle_part &vehicle::cpart( int part_num ) const
{
    return const_cast<vehicle_part &>( parts[part_num] );
}

bool vehicle::valid_part( int part_num ) const
{
    return part_num >= 0 && part_num < static_cast<int>( parts.size() );
}

std::set<int> vehicle::advance_precalc_mounts( const tripoint_abs_ms &src )
{
    map &here = get_map();
    std::set<int> smzs;
    for( vehicle_part &prt : parts ) {
        here.clear_vehicle_point_from_cache( this, here.abs_to_bub( src ) +
                                             tripoint_rel_ms( prt.precalc[0].x(), prt.precalc[0].y(),
                                                     prt.mount.z() + prt.z_terrain[0] ) );
        prt.precalc[0] = prt.precalc[1];
        prt.z_terrain[0] = prt.z_terrain[1];

        smzs.insert( prt.z_terrain[0] );
    }

    pivot_anchor[0] = pivot_anchor[1];
    pivot_rotation[0] = pivot_rotation[1];

    // Invalidate vehicle's point cache
    occupied_cache_time = calendar::before_time_starts;
    return smzs;
}

bool vehicle::refresh_zones()
{
    if( zones_dirty ) {
        decltype( loot_zones ) new_zones;
        for( auto const &z : loot_zones ) {
            zone_data zone = z.second;
            //Get the global position of the first cargo part at the relative coordinate

            const int part_idx = part_with_feature( z.first, "CARGO", false );
            if( part_idx == -1 ) {
                debugmsg( "Could not find cargo part at %d,%d on vehicle %s for loot zone.  Removing loot zone.",
                          z.first.x(), z.first.y(), this->name );

                // If this loot zone refers to a part that no longer exists at this location, then its unattached somehow.
                // By continuing here and not adding to new_zones, we effectively remove it
                continue;
            }
            auto zone_pos = g->m.bub_to_abs( bub_part_location( part_idx ) );
            //Set the position of the zone to that part
            zone.set_position( std::pair<tripoint_abs_ms, tripoint_abs_ms>( zone_pos, zone_pos ), false );
            new_zones.emplace( z.first, zone );
        }
        loot_zones = new_zones;
        zones_dirty = false;
        return true;
    }
    return false;
}

template<>
bool vehicle_part_with_feature_range<std::string>::matches( const size_t part ) const
{
    const vehicle_part &vp = this->vehicle().part( part );
    return vp.info().has_flag( feature_ ) &&
           !vp.removed &&
           ( !( part_status_flag::working & required_ ) || !vp.is_broken() ) &&
           ( !( part_status_flag::available & required_ ) || vp.is_available() ) &&
           ( !( part_status_flag::enabled & required_ ) || vp.enabled );
}

template<>
bool vehicle_part_with_feature_range<vpart_bitflags>::matches( const size_t part ) const
{
    const vehicle_part &vp = this->vehicle().part( part );
    return vp.info().has_flag( feature_ ) &&
           !vp.removed &&
           ( !( part_status_flag::working & required_ ) || !vp.is_broken() ) &&
           ( !( part_status_flag::available & required_ ) || vp.is_available() ) &&
           ( !( part_status_flag::enabled & required_ ) || vp.enabled );
}

bool vehicle::is_loaded() const
{
    return attached && get_map().get_submap_at( tripoint_bub_ms( bub_ms_location() ) ) != nullptr;
}

void vehicle::refresh_locations_hack()
{
    for( vehicle_part &part : parts ) {
        part.refresh_locations_hack( this );
    }
}

vehicle_part &vehicle::get_part_hack( int id )
{
    for( vehicle_part &part : parts ) {
        if( part.hack_id == id ) {
            return part;
        }
    }
    debugmsg( "Could not find part via hack id" );
    return parts[0];
}

int vehicle::get_part_id_hack( int id )
{
    int i = 0;
    for( vehicle_part &part : parts ) {
        if( part.hack_id == id ) {
            return i;
        }
        i++;
    }
    debugmsg( "Could not find part id via hack id" );
    return -1;
}

// The mythical invokation to summon the cargo part on the same tile
vehicle_part *vehicle::get_cargo_part( vehicle_part *part )
{
    vehicle_part *vp = nullptr;
    int vpr = part_with_feature( part->mount, "CARGO", false );
    if( vpr != -1 ) {
        vp = &parts[ vpr ];
    }
    return vp;
}
bool vehicle::has_item_stored( vehicle_part *part )
{
    vehicle_part *vp = get_cargo_part( part );
    if( vp ) {
        return stored_volume( index_of_part( vp ) ) > 0_ml;
    }
    return false;
}

void vehicle::item_dropper_drop( std::vector<vehicle_part *> droppers, bool single )
{
    if( single ) {
        vehicle_part *part = get_cargo_part( droppers[0] );
        std::vector<std::string> option_names;
        std::vector<item *> options;
        for( item *it : part->items ) {
            option_names.push_back( it->display_name() );
            options.push_back( it );
        }
        const int idx = uilist( _( "Drop which item?" ), option_names );
        if( idx < 0 ) {
            return;
        }
        map &here = get_map();
        auto pos = bub_part_location( index_of_part( part ) );
        while( here.has_flag_ter_or_furn( TFLAG_NO_FLOOR, pos ) ) {
            pos.z() -= 1;
        }
        item *dropper = options[idx];
        if( dropper->get_use( "transform" ) ) {
            g->u.invoke_item( dropper, "transform" );
        }
        g->m.add_item_or_charges( pos, part->remove_item( *dropper ) );
    } else {
        for( vehicle_part *d : droppers ) {
            vehicle_part *part = get_cargo_part( d );
            map &here = get_map();
            if( part ) {
                auto pos = bub_part_location( index_of_part( part ) );
                while( here.has_flag_ter_or_furn( TFLAG_NO_FLOOR, pos ) ) {
                    pos.z() -= 1;
                }
                // DANGER: DO NOT PUT THIS IN THE FOR LOOP
                const std::vector<item *> items = part->get_items();
                for( item *it : items ) {
                    if( it->get_use( "transform" ) ) {
                        g->u.invoke_item( it, "transform" );
                    }
                    g->m.add_item_or_charges( pos, part->remove_item( *it ) );
                }
            }
        }
    }
}

void vehicle::item_dropper_drop_single( bool single )
{
    std::vector<std::string> option_names;
    std::vector<vehicle_part *> options;

    // Find all droppers that are loaded
    for( int idx : droppers ) {
        vehicle_part *d = &parts[ idx ];
        if( has_item_stored( d ) ) {
            option_names.push_back( d->name() );
            options.push_back( d );
        }
    }

    // Select one
    if( options.empty() ) {
        add_msg( m_warning, _( "None of the droppers are loaded." ) );
        return;
    }
    const int idx = uilist( _( "Drop from which dropper?" ), option_names );
    if( idx < 0 ) {
        return;
    }
    vehicle_part *dropper = options[idx];

    std::vector<vehicle_part *> droppers;
    droppers.push_back( dropper );
    item_dropper_drop( droppers, single );
}

void vehicle::item_dropper_drop_all( )
{
    std::vector<vehicle_part *> ret;

    // Find all droppers that are loaded
    for( int idx : droppers ) {
        vehicle_part *d = &parts[ idx ];
        if( has_item_stored( d ) ) {
            ret.push_back( d );
        }
    }

    item_dropper_drop( ret, false );
}
void vehicle::set_cruise_control_speed()
{
    const auto requested_min_autodrive_speed = string_input_popup()
        .title( _( "Minumum Speed in tiles per tick? ( 6 km/hr and 4 mph per )" ) )
        .text( std::to_string( min_autodrive_speed ) )
        .only_digits( true )
        .max_length( 3 )
        .query_int();
    min_autodrive_speed = requested_min_autodrive_speed > 0 ? requested_min_autodrive_speed :
                          min_autodrive_speed;
    const auto requested_max_autodrive_speed = string_input_popup()
        .title( _( "Maxumum Speed in tiles per tick? ( 6 km/hr and 4 mph per )" ) )
        .text( std::to_string( max_autodrive_speed ) )
        .only_digits( true )
        .max_length( 3 )
        .query_int();
    max_autodrive_speed = requested_max_autodrive_speed > 0 ? requested_max_autodrive_speed :
                          max_autodrive_speed;
    if( max_autodrive_speed < min_autodrive_speed ) {
        max_autodrive_speed = min_autodrive_speed;
    }
    const auto takeoff = get_takeoff_speed( "t/t" );
    if( takeoff > 0 && takeoff > min_autodrive_speed * 0.8 ) {
        popup( "Warning: Minimum speed is less than safe flight speed" );
    }
    if( takeoff > 0 && takeoff > max_autodrive_speed * 0.5 ) {
        popup( "Warning: Maximum speed is less then safe flight speed" );
    }
}

void vehicle::gain_moves()
{
    fuel_used_last_turn.clear();
    check_falling_or_floating();
    const bool pl_control = player_in_control( g->u );
    if( is_moving() || is_falling ) {
        if( !loose_parts.empty() ) {
            shed_loose_parts();
        }
        of_turn = 1 + of_turn_carry;
        const int vslowdown = slowdown( velocity );
        if( vslowdown > std::abs( velocity ) ) {
            if( cruise_on && cruise_velocity && pl_control ) {
                velocity = velocity > 0 ? 1 : -1;
            } else {
                stop();
            }
        } else if( velocity < 0 ) {
            velocity += vslowdown;
        } else {
            velocity -= vslowdown;
        }
    } else {
        of_turn = .001;
    }
    of_turn_carry = 0;
    // cruise control TODO: enable for NPC?
    if( ( pl_control || is_following || is_patrolling ) && cruise_on && cruise_velocity != velocity ) {
        thrust( ( cruise_velocity ) > velocity ? 1 : -1 );
    }

    // Force off-map vehicles to load by visiting them every time we gain moves.
    // Shouldn't be too expensive if there aren't fifty trillion vehicles in the graph...
    // ...and if there are, it's the player's fault for putting them there.
    distribution_graph::traverse( *this, distribution_graph::noop_visitor_veh,
                                  distribution_graph::noop_visitor_grid );

    if( check_environmental_effects ) {
        check_environmental_effects = do_environmental_effects();
    }

    // turrets which are enabled will try to reload and then automatically fire
    // Turrets which are disabled but have targets set are a special case
    for( auto e : turrets() ) {
        if( e->enabled || e->target.second != e->target.first ) {
            automatic_fire_turret( *e );
        }
    }

    if( velocity < 0 ) {
        beeper_sound();
    }
}
