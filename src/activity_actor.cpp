#include "activity_actor.h"
#include "activity_actor_definitions.h"

#include <cmath>
#include <list>
#include <memory>
#include <string>
#include <utility>

#include "activity_handlers.h" // put_into_vehicle_or_drop and drop_on_map
#include "activity_speed.h"
#include "advanced_inv.h"
#include "avatar.h"
#include "avatar_action.h"
#include "calendar.h"
#include "character.h"
#include "character_functions.h"
#include "construction.h"
#include "construction_partial.h"
#include "craft_command.h"
#include "crafting.h"
#include "debug.h"
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "field_type.h"
#include "flag.h"
#include "game.h"
#include "gates.h"
#include "iexamine.h"
#include "int_id.h"
#include "item.h"
#include "item_group.h"
#include "item_hauling.h"
#include "json.h"
#include "line.h"
#include "locations.h"
#include "map.h"
#include "map_iterator.h"
#include "map_selector.h"
#include "mapdata.h"
#include "messages.h"
#include "monster.h"
#include "morale_types.h"
#include "output.h"
#include "npc.h"
#include "options.h"
#include "pickup.h"
#include "player.h"
#include "player_activity.h"
#include "point.h"
#include "ranged.h"
#include "crafting_quality.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "rng.h"
#include "sounds.h"
#include "timed_event.h"
#include "translations.h"
#include "ui.h"
#include "uistate.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vpart_position.h"

#include "armor_layers.h"
#include "distribution_grid.h"
#include "fault.h"
#include "game_inventory.h"
#include "itype.h"
#include "iuse_actor.h"
#include "requirements.h"
#include "skill.h"
#include "bodypart.h"
#include "iuse.h"
#include "handle_liquid.h"
#include "magic.h"
#include "martialarts.h"
#include "character_martial_arts.h"
#include "mongroup.h"
#include "omdata.h"
#include "type_id.h"
#include "text_snippets.h"

#define dbg(x) DebugLog((x),DC::Game)

static const construction_str_id deconstruct_simple( "constr_deconstruct_simple" );
static const construction_str_id deconstruct( "constr_deconstruct" );
static const construction_group_str_id
advanced_object_deconstruction( "advanced_object_deconstruction" );

static const itype_id itype_bone_human( "bone_human" );
static const itype_id itype_electrohack( "electrohack" );
static const itype_id itype_log( "log" );
static const itype_id itype_splinter( "splinter" );
static const itype_id itype_stick_long( "stick_long" );
static const itype_id itype_UPS( "UPS" );
static const itype_id itype_wool_staple( "wool_staple" );
static const efftype_id effect_ai_waiting( "ai_waiting" );
static const efftype_id effect_sleep( "sleep" );
static const efftype_id effect_sheared( "sheared" );
static const efftype_id effect_tied( "tied" );
static const efftype_id effect_well_fed( "well_fed" );

static const trait_id trait_SPIRITUAL( "SPIRITUAL" );

static const activity_id ACT_MULTIPLE_CHOP_TREES( "ACT_MULTIPLE_CHOP_TREES" );
static const activity_id ACT_TRAVELLING( "ACT_TRAVELLING" );
static const activity_id ACT_MULTIPLE_FISH( "ACT_MULTIPLE_FISH" );
static const activity_id ACT_TIDY_UP( "ACT_TIDY_UP" );
static const skill_id skill_computer( "computer" );
static const skill_id skill_mechanics( "mechanics" );

static const mtype_id mon_zombie( "mon_zombie" );
static const mtype_id mon_zombie_fat( "mon_zombie_fat" );
static const mtype_id mon_zombie_rot( "mon_zombie_rot" );
static const mtype_id mon_skeleton( "mon_skeleton" );
static const mtype_id mon_zombie_crawler( "mon_zombie_crawler" );

static const quality_id qual_LOCKPICK( "LOCKPICK" );
static const quality_id qual_BUTCHER( "BUTCHER" );
static const quality_id qual_CUT_FINE( "CUT_FINE" );

static const trait_id trait_DEBUG_HS( "DEBUG_HS" );
static const trait_id trait_STOCKY_TROGLO( "STOCKY_TROGLO" );

static const skill_id skill_fabrication( "fabrication" );
static const skill_id skill_survival( "survival" );
static const skill_id skill_firstaid( "firstaid" );
static const skill_id skill_electronics( "electronics" );

static const itype_id itype_nail( "nail" );
static const itype_id itype_2x4( "2x4" );
static const itype_id itype_battery( "battery" );

static const zone_type_id zone_type_FARM_PLOT( "FARM_PLOT" );

static const std::string flag_PLANTABLE( "PLANTABLE" );
static const std::string has_thievery_witness( "has_thievery_witness" );

int simple_task::to_counter() const
{
    double ret = 10'000'000.0 / moves_total * ( moves_total - moves_left );
    return std::round( ret );
}

inline void progress_counter::pop()
{
    if( empty() ) {
        dbg( DL::Error ) << "task was popped out of empty progress queue";
        return;
    }
    moves_left -= targets.front().moves_left;
    targets.pop_front();
    idx++;
}

inline void progress_counter::purge()
{
    if( empty() ) {
        dbg( DL::Error ) << "task was purged out of empty progress queue";
        return;
    }
    moves_left -= targets.front().moves_left;
    moves_total -= targets.front().moves_total;
    total_tasks--;
    targets.pop_front();
}

inline void activity_actor::calc_all_moves( player_activity &act, Character &who )
{
    act.speed.calc_all_moves( who );
}

aim_activity_actor::aim_activity_actor() : fake_weapon( new fake_item_location() )
{
    initial_view_offset = get_avatar().view_offset;
}

std::unique_ptr<aim_activity_actor> aim_activity_actor::use_wielded()
{
    return std::make_unique<aim_activity_actor>();
}

std::unique_ptr<aim_activity_actor> aim_activity_actor::use_bionic( detached_ptr<item> &&fake_gun,
        const units::energy &cost_per_shot )
{
    std::unique_ptr<aim_activity_actor> act( new aim_activity_actor() );
    act->bp_cost_per_shot = cost_per_shot;
    act->fake_weapon = std::move( fake_gun );
    return act;
}

std::unique_ptr<aim_activity_actor> aim_activity_actor::use_gear( item *gun )
{
    std::unique_ptr<aim_activity_actor> act( new aim_activity_actor() );
    act->weapon = safe_reference<item>( gun );
    return act;
}

std::unique_ptr<aim_activity_actor> aim_activity_actor::use_mutation( detached_ptr<item>
        &&fake_gun )
{
    std::unique_ptr<aim_activity_actor> act( new aim_activity_actor() );
    act->fake_weapon = std::move( fake_gun );
    return act;
}

void aim_activity_actor::start( player_activity &/*act*/, Character &/*who*/ )
{
    // Time spent on aiming is determined on the go by the player
    // Dummy progress task to indicate ongoing activity
    progress.dummy();
}

void aim_activity_actor::do_turn( player_activity &act, Character &who )
{
    if( !who.is_avatar() ) {
        debugmsg( "ACT_AIM not implemented for NPCs" );
        aborted = true;
        progress.pop();
        return;
    }
    avatar &you = get_avatar();

    item *weapon = get_weapon();
    if( !weapon || !avatar_action::can_fire_weapon( you, get_map(), *weapon ) ) {
        aborted = true;
        progress.pop();
        return;
    }

    gun_mode gun = weapon->gun_current_mode();
    if( !gun->ammo_remaining() && !reload_loc && gun->has_flag( flag_RELOAD_AND_SHOOT ) ) {
        if( !load_RAS_weapon() ) {
            aborted = true;
            progress.pop();
            return;
        }
    }
    g->temp_exit_fullscreen();
    target_handler::trajectory trajectory;
    if( const auto shape_gen = ranged::get_shape_factory( *weapon ) ) {
        trajectory = target_handler::mode_shaped( you, *shape_gen, *this );
    } else {
        trajectory = target_handler::mode_fire( you, *this );
    }
    g->reenter_fullscreen();

    if( aborted ) {
        progress.pop();
    } else {
        if( !trajectory.empty() ) {
            fin_trajectory = trajectory;
            progress.pop();
        }

        // Allow interrupting activity only during 'aim and fire'.
        // Prevents '.' key for 'aim for 10 turns' from conflicting with '.' key for 'interrupt activity'
        // in case of high input lag (curses, sdl sometimes...), but allows to interrupt aiming
        // if a bug happens / stars align to cause an endless aiming loop.
        act.interruptable_with_kb = action != "AIM";
    }
}

void aim_activity_actor::finish( player_activity &act, Character &who )
{
    act.set_to_null();
    item *weapon = get_weapon();
    if( !weapon ) {
        restore_view();
        return;
    }
    if( aborted ) {
        if( reload_requested ) {
            // Reload the gun / select different arrows
            // May assign ACT_RELOAD
            avatar_action::reload_wielded( true );
        }
        restore_view();
        return;
    }

    // Fire!
    gun_mode gun = weapon->gun_current_mode();

    item *ammo_loc = reload_loc ? &*reload_loc : nullptr;

    int shots_fired = ranged::fire_gun( who, fin_trajectory.back(), gun.qty, *gun, ammo_loc );

    if( shots_fired > 0 ) {
        // TODO: bionic power cost of firing should be derived from a value of the relevant weapon.
        if( bp_cost_per_shot > 0_J ) {
            who.mod_power_level( -bp_cost_per_shot * shots_fired );
        }
        if( stamina_cost_per_shot > 0 ) {
            who.mod_stamina( -stamina_cost_per_shot * shots_fired );
        }
    }

    if( !get_option<bool>( "AIM_AFTER_FIRING" ) ) {
        restore_view();
        return;
    }

    // re-enter aiming UI with same parameters
    std::unique_ptr<aim_activity_actor> aim_actor = std::make_unique<aim_activity_actor>();
    aim_actor->abort_if_no_targets = true;
    aim_actor->fake_weapon = std::move( this->fake_weapon );
    aim_actor->bp_cost_per_shot = this->bp_cost_per_shot;
    aim_actor->initial_view_offset = this->initial_view_offset;

    // if invalid target or it's dead - reset it so a new one is acquired
    shared_ptr_fast<Creature> last_target = who.last_target.lock();
    if( last_target && last_target->is_dead_state() ) {
        who.last_target.reset();
    }
    who.assign_activity( std::unique_ptr<player_activity>( new player_activity( std::move( aim_actor ) ) ), false );
}

void aim_activity_actor::canceled( player_activity &/*act*/, Character &/*who*/ )
{
    restore_view();
}

void aim_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "fake_weapon", fake_weapon ? *fake_weapon : null_item_reference() );
    jsout.member( "bp_cost_per_shot", bp_cost_per_shot );
    jsout.member( "stamina_cost_per_shot", stamina_cost_per_shot );
    jsout.member( "action", action );
    jsout.member( "aif_duration", aif_duration );
    jsout.member( "aiming_at_critter", aiming_at_critter );
    jsout.member( "snap_to_target", snap_to_target );
    jsout.member( "shifting_view", shifting_view );
    jsout.member( "initial_view_offset", initial_view_offset );
    jsout.member( "loaded_RAS_weapon", loaded_RAS_weapon );
    jsout.member( "reload_loc", reload_loc );
    jsout.member( "aborted", aborted );
    jsout.member( "reload_requested", reload_requested );
    jsout.member( "abort_if_no_targets", abort_if_no_targets );

    jsout.end_object();
}

std::unique_ptr<activity_actor> aim_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<aim_activity_actor> actor( new aim_activity_actor() );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "fake_weapon", actor->fake_weapon );
    data.read( "bp_cost_per_shot", actor->bp_cost_per_shot );
    data.read( "stamina_cost_per_shot", actor->stamina_cost_per_shot );
    data.read( "action", actor->action );
    data.read( "aif_duration", actor->aif_duration );
    data.read( "aiming_at_critter", actor->aiming_at_critter );
    data.read( "snap_to_target", actor->snap_to_target );
    data.read( "shifting_view", actor->shifting_view );
    data.read( "initial_view_offset", actor->initial_view_offset );
    data.read( "loaded_RAS_weapon", actor->loaded_RAS_weapon );
    data.read( "reload_loc", actor->reload_loc );
    data.read( "aborted", actor->aborted );
    data.read( "reload_requested", actor->reload_requested );
    data.read( "abort_if_no_targets", actor->abort_if_no_targets );

    return actor;
}

item *aim_activity_actor::get_weapon()
{
    if( weapon ) {
        return &*weapon;
    }
    if( fake_weapon ) {
        // TODO: check if the player lost relevant bionic/mutation
        return &*fake_weapon;
    } else {
        // Check for lost gun (e.g. yanked by zombie technician)
        // TODO: check that this is the same gun that was used to start aiming
        item *weapon = &get_player_character().primary_weapon();
        return weapon->is_null() ? nullptr : weapon;
    }
}

void aim_activity_actor::restore_view()
{
    avatar &player_character = get_avatar();
    bool changed_z = player_character.view_offset.z() != initial_view_offset.z();
    player_character.view_offset = initial_view_offset;
    if( changed_z ) {
        get_map().invalidate_map_cache( player_character.view_offset.z() );
        g->invalidate_main_ui_adaptor();
    }
}

bool aim_activity_actor::load_RAS_weapon()
{
    // TODO: use activity for fetching ammo and loading weapon
    player &you = get_avatar();
    item *weapon = get_weapon();
    gun_mode gun = weapon->gun_current_mode();

    // Will burn (0.2% max base stamina * the strength required to fire)
    stamina_cost_per_shot = gun->get_min_str() * static_cast<int>
                            ( 0.002f * get_option<int>( "PLAYER_MAX_STAMINA" ) );
    if( you.get_stamina() < stamina_cost_per_shot ) {
        you.add_msg_if_player( m_bad, _( "You're too tired to draw your %s." ), weapon->tname() );
        return false;
    }

    const auto ammo_location_is_valid = [&]() -> bool {
        if( !you.ammo_location )
        {
            return false;
        }
        if( !gun->can_reload_with( you.ammo_location->typeId() ) )
        {
            return false;
        }
        if( square_dist( you.bub_pos(), you.ammo_location->position() ) > 1 )
        {
            return false;
        }
        return true;
    };
    item_reload_option opt = ammo_location_is_valid() ? item_reload_option( &you, weapon,
                             weapon, *you.ammo_location ) : character_funcs::select_ammo( you, *gun );
    if( !opt ) {
        // Menu canceled
        return false;
    }

    reload_loc = opt.ammo;
    loaded_RAS_weapon = true;
    return true;
}

void autodrive_activity_actor::start( player_activity &/* act */, Character &who )
{
    const bool in_vehicle = who.in_vehicle && who.controlling_vehicle;
    const optional_vpart_position vp = get_map().veh_at( who.bub_pos() );
    if( !( vp && in_vehicle ) ) {
        who.cancel_activity();
        return;
    }

    player_vehicle = &vp->vehicle();
    if( player_vehicle->is_flying_in_air() ) {
        int min_speed = player_vehicle->get_takeoff_speed( "t/t" );
        if( player_vehicle->velocity * 0.8 < min_speed * vehicles::cmps_per_tile ) {
            if( !g->u.query_yn( "Warning: Current Speed is below recommened values, proceed?" ) ) {
                who.cancel_activity();
                return;
            }
        }
        if( player_vehicle->min_autodrive_speed * 0.8 < min_speed ) {
            if( !g->u.query_yn( "Warning: Min Autodrive Speed is below recommened values, proceed?" ) ) {
                who.cancel_activity();
                return;
            }
        }
        if( player_vehicle->max_autodrive_speed * 0.5 < min_speed ) {
            if( !g->u.query_yn( "Warning: Max Autodrive Speed is below recommened values, proceed?" ) ) {
                who.cancel_activity();
                return;
            }
        }
    }
    player_vehicle->is_autodriving = true;
    progress.dummy();
}

void autodrive_activity_actor::do_turn( player_activity &/* act */, Character &who )
{
    if( who.in_vehicle && who.controlling_vehicle && player_vehicle ) {
        if( who.moves <= 0 ) {
            // out of moves? the driver's not doing anything this turn
            // (but the vehicle will continue moving)
            return;
        }
        switch( player_vehicle->do_autodrive( who ) ) {
            case autodrive_result::ok:
                if( who.moves > 0 ) {
                    // if do_autodrive() didn't eat up all our moves, end the turn
                    // equivalent to player pressing the "pause" button
                    who.moves = 0;
                }
                sounds::reset_markers();
                break;
            case autodrive_result::abort:
                who.cancel_activity();
                break;
            case autodrive_result::finished:
                progress.pop();
                break;
        }
    } else {
        who.cancel_activity();
    }
}

void autodrive_activity_actor::canceled( player_activity &act, Character &who )
{
    who.add_msg_if_player( m_info, _( "Auto-drive canceled." ) );
    who.omt_path.clear();
    if( player_vehicle ) {
        player_vehicle->stop_autodriving( false );
    }
    act.set_to_null();
}

void autodrive_activity_actor::finish( player_activity &act, Character &who )
{
    who.add_msg_if_player( m_info, _( "You have reached your destination." ) );
    player_vehicle->stop_autodriving( false );
    act.set_to_null();
}

void autodrive_activity_actor::serialize( JsonOut &jsout ) const
{
    // Activity is not being saved but still provide some valid json if called.
    jsout.write_null();
}

std::unique_ptr<activity_actor> autodrive_activity_actor::deserialize( JsonIn & )
{
    return std::make_unique<autodrive_activity_actor>();
}

void dig_activity_actor::start( player_activity &/*act*/, Character & )
{
    map &here = get_map();
    ter_id ter_here = here.ter( location );
    const bool grave = ter_here == t_grave;
    const std::string name = grave
                             ? "grave"
                             : ter_here->name();
    progress.emplace( name, moves_total );
}

void dig_activity_actor::do_turn( player_activity &/*act*/, Character & )
{
    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
    sfx::play_activity_sound( "tool", "shovel", sfx::get_heard_volume( location ) );
    if( calendar::once_every( 1_minutes ) ) {
        //~ Sound of a shovel digging a pit at work!
        sounds::sound( location, 10, sounds::sound_t::activity, _( "hsh!" ) );
    }
}

void dig_activity_actor::finish( player_activity &act, Character &who )
{
    map &here = get_map();
    const bool grave = here.ter( location ) == t_grave;

    if( grave ) {
        if( one_in( 10 ) ) {
            static const std::array<mtype_id, 5> monids = { {
                    mon_zombie, mon_zombie_fat,
                    mon_zombie_rot, mon_skeleton,
                    mon_zombie_crawler
                }
            };

            g->place_critter_at( random_entry( monids ), byproducts_location );
            here.furn_set( location, f_coffin_o );
            who.add_msg_if_player( m_warning, _( "Something crawls out of the coffin!" ) );
        } else {
            here.spawn_item( location, itype_bone_human, rng( 5, 15 ) );
            here.furn_set( location, f_coffin_c );
        }
        std::vector<item *> dropped = get_map().place_items( item_group_id( "allclothes" ), 50, location,
                                      location, false,
                                      calendar::turn );
        get_map().place_items( item_group_id( "grave" ), 25, location, location, false, calendar::turn );
        get_map().place_items( item_group_id( "jewelry_front" ), 20, location, location, false,
                               calendar::turn );
        for( item * const &it : dropped ) {
            if( it->is_armor() ) {
                it->set_damage( rng( 1, it->max_damage() - 1 ) );
            }
        }
        g->events().send<event_type::exhumes_grave>( who.getID() );
    }

    here.ter_set( location, ter_id( result_terrain ) );

    here.spawn_items( byproducts_location,
                      item_group::items_from( item_group_id( byproducts_item_group ),
                              calendar::turn ) );

    const int act_exertion = act.moves_total;

    who.mod_stored_kcal( std::min( -1, -act_exertion / to_moves<int>( 80_seconds ) ) );
    who.mod_thirst( std::max( 1, act_exertion / to_moves<int>( 12_minutes ) ) );
    who.mod_fatigue( std::max( 1, act_exertion / to_moves<int>( 6_minutes ) ) );
    if( grave ) {
        who.add_msg_if_player( m_good, _( "You finish exhuming a grave." ) );
    } else {
        who.add_msg_if_player( m_good, _( "You finish digging the %s." ),
                               here.ter( location )->name() );
    }

    act.set_to_null();
}

void dig_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "moves", moves_total );
    jsout.member( "location", location );
    jsout.member( "result_terrain", result_terrain );
    jsout.member( "byproducts_location", byproducts_location );
    jsout.member( "byproducts_item_group", byproducts_item_group );

    jsout.end_object();
}

std::unique_ptr<activity_actor> dig_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<dig_activity_actor> actor( new dig_activity_actor( 0, tripoint_bub_ms::zero(),
            {}, tripoint_bub_ms::zero(), {} ) );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "moves", actor->moves_total );
    data.read( "location", actor->location );
    data.read( "result_terrain", actor->result_terrain );
    data.read( "byproducts_location", actor->byproducts_location );
    data.read( "byproducts_item_group", actor->byproducts_item_group );

    return actor;
}

void dig_channel_activity_actor::start( player_activity &/*act*/, Character & )
{
    map &here = get_map();
    progress.emplace( here.ter( location )->name(), moves_total );
}

void dig_channel_activity_actor::do_turn( player_activity &/*act*/, Character & )
{
    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
    sfx::play_activity_sound( "tool", "shovel", sfx::get_heard_volume( location ) );
    if( calendar::once_every( 1_minutes ) ) {
        //~ Sound of a shovel digging a pit at work!
        sounds::sound( location, 10, sounds::sound_t::activity, _( "hsh!" ) );
    }
}

void dig_channel_activity_actor::finish( player_activity &act, Character &who )
{
    map &here = get_map();
    here.ter_set( location, ter_id( result_terrain ) );

    here.spawn_items( byproducts_location,
                      item_group::items_from( item_group_id( byproducts_item_group ),
                              calendar::turn ) );

    const int act_exertion = act.moves_total;

    who.mod_stored_kcal( std::min( -1, -act_exertion / to_moves<int>( 80_seconds ) ) );
    who.mod_thirst( std::max( 1, act_exertion / to_moves<int>( 12_minutes ) ) );
    who.mod_fatigue( std::max( 1, act_exertion / to_moves<int>( 6_minutes ) ) );
    who.add_msg_if_player( m_good, _( "You finish digging up %s." ),
                           here.ter( location )->name() );

    act.set_to_null();
}

void dig_channel_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "moves", moves_total );
    jsout.member( "location", location );
    jsout.member( "result_terrain", result_terrain );
    jsout.member( "byproducts_location", byproducts_location );
    jsout.member( "byproducts_item_group", byproducts_item_group );

    jsout.end_object();
}

std::unique_ptr<activity_actor> dig_channel_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<dig_channel_activity_actor> actor( new dig_channel_activity_actor( 0,
            tripoint_bub_ms::zero(),
            {}, tripoint_bub_ms::zero(), {} ) );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "moves", actor->moves_total );
    data.read( "location", actor->location );
    data.read( "result_terrain", actor->result_terrain );
    data.read( "byproducts_location", actor->byproducts_location );
    data.read( "byproducts_item_group", actor->byproducts_item_group );

    return actor;
}

bool disassemble_activity_actor::try_start_single( player_activity &/* act */, Character &who )
{
    if( targets.empty() ) {
        return false;
    }
    const iuse_location &target = targets.front();
    if( !target.loc ) {
        debugmsg( "Lost target of ACT_DISASSEMBLE" );
        targets.clear();
        return false;
    }
    const item &itm = *target.loc;

    // Have to check here again in case we ran out of tools
    const ret_val<bool> can_do = crafting::can_disassemble( who, itm, who.crafting_inventory() );
    if( !can_do.success() ) {
        who.add_msg_if_player( m_info, "%s", can_do.c_str() );
        return false;
    }
    return true;
}

inline void disassemble_activity_actor::process_target( player_activity &/*act*/,
        iuse_location &target )
{
    const item &itm = *target.loc;
    const recipe &dis = recipe_dictionary::get_uncraft( itm.typeId() );
    int moves_needed = dis.time * target.count;
    progress.emplace( itm.tname( target.count ), moves_needed );
}

inline void disassemble_activity_actor::calc_all_moves( player_activity &act, Character &who )
{
    const auto &target = targets.front().loc;
    auto reqs = activity_reqs_adapter( recipe_dictionary::get_uncraft( target->typeId() ),
                                       std::make_pair( target->weight(), target->volume() ) );
    act.speed.calc_all_moves( who, reqs );
}

void disassemble_activity_actor::start( player_activity &act, Character &who )
{
    if( !who.is_avatar() ) {
        debugmsg( "ACT_DISASSEMBLE is not implemented for NPCs" );
        act.set_to_null();
    } else if( !try_start_single( act, who ) ) {
        act.set_to_null();
    }
    for( auto &target : targets ) {
        process_target( act, target );
    }
}

void disassemble_activity_actor::do_turn( player_activity &act, Character &who )
{
    if( progress.front().complete() ) {
        const iuse_location &target = targets.front();
        if( !target.loc ) {
            debugmsg( "Lost target of ACT_DISASSEMBLY" );
        } else {
            crafting::complete_disassemble( who, target, get_map().abs_to_bub( pos ) );
        }
        targets.erase( targets.begin() );
        progress.pop();
        if( !progress.empty() ) {
            if( try_start_single( act, who ) ) {
                calc_all_moves( act, who );
            } else {
                act.set_to_null();
            }
        }
    }
}

void disassemble_activity_actor::finish( player_activity &act, Character &who )
{
    if( try_start_single( act, who ) ) {
        debugmsg( "disassemble_activity_actor call finish function while able to start new disassembly" );
    }
    // Make a copy to avoid use-after-free
    bool recurse = this->recursive;

    act.set_to_null();

    if( recurse ) {
        crafting::disassemble_all( *who.as_avatar(), recurse );
    }
}

void disassemble_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "targets", targets );
    jsout.member( "pos", pos );
    jsout.member( "recursive", recursive );

    jsout.end_object();
}

std::unique_ptr<activity_actor> disassemble_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<disassemble_activity_actor> actor( new disassemble_activity_actor() );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "targets", actor->targets );
    data.read( "pos", actor->pos );
    data.read( "recursive", actor->recursive );

    return actor;
}

drop_activity_actor::drop_activity_actor( Character &ch, const drop_locations &items,
        bool force_ground, const tripoint_rel_ms &relpos )
    : force_ground( force_ground ), relpos( relpos )
{
    this->items = pickup::reorder_for_dropping( ch, items );
}

void drop_activity_actor::start( player_activity &/* act */, Character & )
{
    // Dummy progress task to indicate ongoing activity
    progress.dummy();
}

void drop_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "items", items );
    jsout.member( "force_ground", force_ground );
    jsout.member( "relpos", relpos );

    jsout.end_object();
}

std::unique_ptr<activity_actor> drop_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<drop_activity_actor> actor( new drop_activity_actor() );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "items", actor->items );
    data.read( "force_ground", actor->force_ground );
    data.read( "relpos", actor->relpos );

    return actor;
}

enum hack_result {
    HACK_UNABLE,
    HACK_FAIL,
    HACK_NOTHING,
    HACK_SUCCESS
};

enum hack_type {
    HACK_SAFE,
    HACK_DOOR,
    HACK_GAS,
    HACK_NULL
};

static hack_type get_hack_type( tripoint_bub_ms examp )
{
    hack_type type = HACK_NULL;
    const map &here = get_map();
    const furn_t &xfurn_t = *here.furn( examp );
    const ter_t &xter_t = *here.ter( examp );
    if( xter_t.examine == &iexamine::pay_gas || xfurn_t.examine == &iexamine::pay_gas ) {
        type = HACK_GAS;
    } else if( xter_t.examine == &iexamine::cardreader || xfurn_t.examine == &iexamine::cardreader ) {
        type = HACK_DOOR;
    } else if( xter_t.examine == &iexamine::gunsafe_el || xfurn_t.examine == &iexamine::gunsafe_el ) {
        type = HACK_SAFE;
    }
    return type;
}

void hacking_activity_actor::start( player_activity &act, Character & )
{
    hack_type type = get_hack_type( abs_to_bub( act.placement ) );
    std::string name;

    switch( type ) {
        case hack_type::HACK_SAFE:
            name = "safe";
            break;
        case hack_type::HACK_DOOR:
            name = "door panel";
            break;
        case hack_type::HACK_GAS:
            name = "gas pump";
            break;
        default:
            name = "";
            break;
    }

    progress.emplace( name, to_moves<int>( 5_minutes ) );
}

void hacking_activity_actor::do_turn( player_activity &/*act*/, Character & )
{
    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
}

static int hack_level( const Character &who )
{
    ///\EFFECT_COMPUTER increases success chance of hacking card readers
    // odds go up with int>8, down with int<8
    // 4 int stat is worth 1 computer skill here
    ///\EFFECT_INT increases success chance of hacking card readers
    return who.get_skill_level( skill_computer ) + ( who.int_cur - 8 ) / 4;
}

static hack_result hack_attempt( Character &who, const bool using_bionic )
{
    who.practice( skill_computer, 20 );
    // only skilled supergenius never cause short circuits, but the odds are low for people
    // with moderate skills
    const int hack_stddev = 5;
    int success = std::ceil( normal_roll( hack_level( who ), hack_stddev ) );
    if( success < 0 ) {
        who.add_msg_if_player( _( "You cause a short circuit!" ) );
        if( using_bionic ) {
            who.mod_power_level( -25_kJ );
        } else {
            who.use_charges( itype_electrohack, 25 );
        }

        if( success <= -5 ) {
            if( !using_bionic ) {
                who.add_msg_if_player( m_bad, _( "Your electrohack is ruined!" ) );
                who.use_amount( itype_electrohack, 1 );
            } else {
                who.add_msg_if_player( m_bad, _( "Your power is drained!" ) );
                who.mod_power_level( units::from_kilojoule( -rng( 25,
                                     units::to_kilojoule( who.get_power_level() ) ) ) );
            }
        }
        return HACK_FAIL;
    } else if( success < 6 ) {
        return HACK_NOTHING;
    } else {
        return HACK_SUCCESS;
    }
}

hacking_activity_actor::hacking_activity_actor( use_bionic )
    : using_bionic( true )
{
}

void hacking_activity_actor::finish( player_activity &act, Character &who )
{
    tripoint_bub_ms examp = abs_to_bub( act.placement );
    hack_type type = get_hack_type( examp );
    map &here = get_map();
    switch( hack_attempt( who, using_bionic ) ) {
        case HACK_UNABLE:
            who.add_msg_if_player( _( "You cannot hack this." ) );
            break;
        case HACK_FAIL:
            // currently all things that can be hacked have equivalent alarm failure states.
            // this may not always be the case with new hackable things.
            g->events().send<event_type::triggers_alarm>( who.getID() );
            sounds::sound( who.bub_pos(), 60, sounds::sound_t::music, _( "an alarm sound!" ), true,
                           "environment",
                           "alarm" );
            if( examp.z() > 0 && !g->timed_events.queued( TIMED_EVENT_WANTED ) ) {
                g->timed_events.add( TIMED_EVENT_WANTED, calendar::turn + 30_minutes, 0,
                                     who.abs_sm_pos() );
            }
            break;
        case HACK_NOTHING:
            who.add_msg_if_player( _( "You fail the hack, but no alarms are triggered." ) );
            break;
        case HACK_SUCCESS:
            if( type == HACK_GAS ) {
                int tankGasUnits;
                const std::optional<tripoint_bub_ms> pTank_ = iexamine::getNearFilledGasTank( examp, tankGasUnits );
                if( !pTank_ ) {
                    break;
                }
                const tripoint_bub_ms pTank = *pTank_;
                const std::optional<tripoint_bub_ms> pGasPump = iexamine::getGasPumpByNumber( examp,
                        uistate.ags_pay_gas_selected_pump );
                if( pGasPump && iexamine::toPumpFuel( pTank, *pGasPump, tankGasUnits ) ) {
                    who.add_msg_if_player( _( "You hack the terminal and route all available fuel to your pump!" ) );
                    sounds::sound( examp, 6, sounds::sound_t::activity,
                                   _( "Glug Glug Glug Glug Glug Glug Glug Glug Glug" ), true, "tool", "gaspump" );
                } else {
                    who.add_msg_if_player( _( "Nothing happens." ) );
                }
            } else if( type == HACK_SAFE ) {
                who.add_msg_if_player( m_good, _( "The door on the safe swings open." ) );
                here.furn_set( examp, furn_str_id( "f_gunsafe_o" ) );
            } else if( type == HACK_DOOR ) {
                who.add_msg_if_player( _( "You activate the panel!" ) );
                who.add_msg_if_player( m_good, _( "The nearby doors unlock." ) );
                here.ter_set( examp, t_card_reader_broken );
                for( const tripoint_bub_ms &tmp : here.points_in_radius( ( examp ), 3 ) ) {
                    if( here.ter( tmp ) == t_door_metal_locked ) {
                        here.ter_set( tmp, t_door_metal_c );
                    }
                }
            }
            break;
    }
    act.set_to_null();
}

void hacking_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "using_bionic", using_bionic );

    jsout.end_object();
}

std::unique_ptr<activity_actor> hacking_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<hacking_activity_actor> actor( new hacking_activity_actor() );
    if( jsin.test_null() ) {
        // Old saves might contain a null instead of an object.
        // Since we do not know whether a bionic or an item was chosen we assume
        // it was an item.
        actor->using_bionic = false;
    } else {
        JsonObject jsobj = jsin.get_object();
        jsobj.read( "using_bionic", actor->using_bionic );
        jsobj.read( "progress", actor->progress );
    }
    return actor;
}

void move_items_activity_actor::do_turn( player_activity &act, Character &who )
{
    const auto dest = relative_destination + who.bub_pos();

    while( who.moves > 0 && !target_items.empty() ) {
        safe_reference<item> target = std::move( target_items.back() );
        const int quantity = quantities.back();
        target_items.pop_back();
        quantities.pop_back();

        if( !target ) {
            //TODO!: might not be appropriate to debugmsg just because something was destroyed/unloaded
            debugmsg( "Lost target item of ACT_MOVE_ITEMS" );
            continue;
        }

        // Check that we can pick it up.
        if( target->made_of( LIQUID ) ) {
            continue;
        }

        // This is for hauling across zlevels, remove when going up and down stairs
        // is no longer teleportation
        // Also ignores items owned by other NPCs, unless they'd already attack on sight
        if( target->is_owned_by( who, true ) || target->get_owner()->likes_u < -10 ) {
            target->set_owner( who );
        } else {
            continue;
        }

        const tripoint_bub_ms src = target->position();
        detached_ptr<item> newit = quantity == 0 ? target->detach() : target->split( quantity );

        const int distance = src.z() == dest.z() ? std::max( rl_dist( src, dest ), 1 ) : 1;
        who.mod_moves( -pickup::cost_to_move_item( who, *newit ) * distance );

        std::vector<detached_ptr<item>> vec;
        vec.push_back( std::move( newit ) );
        if( to_vehicle ) {
            put_into_vehicle_or_drop( who, item_drop_reason::deliberate, vec, dest );
        } else {
            drop_on_map( who, item_drop_reason::deliberate, vec, dest );
        }
    }

    if( target_items.empty() ) {
        // Nuke the current activity, leaving the backlog alone.
        act.set_to_null();
        if( who.is_hauling() && !has_haulable_items( who.bub_pos() ) ) {
            who.stop_hauling();
        }
    }
}

void move_items_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "target_items", target_items );
    jsout.member( "quantities", quantities );
    jsout.member( "to_vehicle", to_vehicle );
    jsout.member( "relative_destination", relative_destination );

    jsout.end_object();
}

std::unique_ptr<activity_actor> move_items_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<move_items_activity_actor> actor( new move_items_activity_actor( {}, {}, false,
            tripoint_rel_ms::zero() ) );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "target_items", actor->target_items );
    data.read( "quantities", actor->quantities );
    data.read( "to_vehicle", actor->to_vehicle );
    data.read( "relative_destination", actor->relative_destination );

    return actor;
}

void pickup_activity_actor::do_turn( player_activity &act, Character &who )
{
    // If we don't have target items bail out
    if( target_items.empty() ) {
        who.cancel_activity();
        return;
    }

    // If the player moves while picking up (i.e.: in a moving vehicle) cancel
    // the activity, only populate starting_pos when grabbing from the ground
    if( starting_pos && *starting_pos != who.bub_pos() ) {
        who.cancel_activity();
        who.add_msg_if_player( _( "Moving canceled auto-pickup." ) );
        return;
    }

    // Auto_resume implies autopickup.
    const bool autopickup = who.activity->auto_resume;

    // False indicates that the player canceled pickup when met with some prompt
    const bool keep_going = pickup::do_pickup( target_items, autopickup );

    // Check thievey witness
    npc *witness = nullptr;
    if( !act.str_values.empty() && act.str_values[0] == has_thievery_witness ) {
        for( npc &guy : g->all_npcs() ) {
            if( guy.get_attitude() == NPCATT_RECOVER_GOODS ) {
                witness = &guy;
                break;
            }
        }
    }

    // If there are items left we ran out of moves, so continue the activity
    // Otherwise, we are done.
    if( !keep_going || target_items.empty() || witness ) {
        who.cancel_activity();

        if( who.get_value( "THIEF_MODE_KEEP" ) != "YES" ) {
            who.set_value( "THIEF_MODE", "THIEF_ASK" );
        }

        if( !keep_going ) {
            // The user canceled the activity, so we're done
            // AIM might have more pickup activities pending, also cancel them.
            // TODO: Move this to advanced inventory instead of hacking it in here
            cancel_aim_processing();
        }

        if( witness ) {
            witness->talk_to_u();
            // Then remove "has_thievery_witness" from the activity
            act.str_values.clear();
        }
    }
}

void pickup_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "target_items", target_items );
    jsout.member( "starting_pos", starting_pos );

    jsout.end_object();
}

std::unique_ptr<activity_actor> pickup_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<pickup_activity_actor> actor( new pickup_activity_actor( {}, std::nullopt ) );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "target_items", actor->target_items );
    data.read( "starting_pos", actor->starting_pos );

    return actor;
}

void hacksaw_activity_actor::start( player_activity &act, Character &/*who*/ )
{
    const map &here = get_map();

    if( here.has_furn( target ) ) {
        const furn_id furn_type = here.furn( target );
        if( !furn_type->hacksaw->valid() ) {
            if( !testing ) {
                debugmsg( "%s hacksaw is invalid", furn_type.id().str() );
            }
            act.set_to_null();
            return;
        }
        progress.emplace( furn_type->name(), to_moves<int>( furn_type->hacksaw->duration() ) );
    } else if( !here.ter( target )->is_null() ) {
        const ter_id ter_type = here.ter( target );
        if( !ter_type->hacksaw->valid() ) {
            if( !testing ) {
                debugmsg( "%s hacksaw is invalid", ter_type.id().str() );
            }
            act.set_to_null();
            return;
        }
        progress.emplace( ter_type->name(), to_moves<int>( ter_type->hacksaw->duration() ) );
    } else {
        if( !testing ) {
            debugmsg( "hacksaw activity called on invalid terrain" );
        }
        act.set_to_null();
        return;
    }
}

void hacksaw_activity_actor::do_turn( player_activity &/* act */, Character &who )
{
    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
    if( tool->ammo_sufficient() ) {
        tool->ammo_consume( tool->ammo_required(), tool->position() );
        sfx::play_activity_sound( "tool", "hacksaw", sfx::get_heard_volume( target ) );
        if( calendar::once_every( 1_minutes ) ) {
            //~ Sound of a metal sawing tool at work!
            sounds::sound( target, 15, sounds::sound_t::destructive_activity, _( "grnd grnd grnd" ) );
        }
    } else {
        if( who.is_avatar() ) {
            who.add_msg_if_player( m_bad, _( "Your %1$s ran out of charges." ), tool->tname() );
        } else { // who.is_npc()
            if( get_avatar().sees( who.bub_pos() ) ) {
                add_msg( _( "%1$s %2$s ran out of charges." ), who.disp_name( false,
                         true ), tool->tname() );
            }
        }
        who.cancel_activity();
    }
}

void hacksaw_activity_actor::finish( player_activity &act, Character &who )
{
    map &here = get_map();
    const activity_data_common *data;

    if( here.has_furn( target ) ) {
        const furn_id furn_type = here.furn( target );
        if( !furn_type->hacksaw->valid() ) {
            if( !testing ) {
                debugmsg( "%s hacksaw is invalid", furn_type.id().str() );
            }
            act.set_to_null();
            return;
        }

        const furn_str_id new_furn = furn_type->hacksaw->result();
        if( !new_furn.is_valid() ) {
            if( !testing ) {
                debugmsg( "hacksaw furniture: %s invalid furniture", new_furn.str() );
            }
            act.set_to_null();
            return;
        }

        data = static_cast<const activity_data_common *>( &*furn_type->hacksaw );
        here.furn_set( target, new_furn );
    } else if( !here.ter( target )->is_null() ) {
        const ter_id ter_type = here.ter( target );
        if( !ter_type->hacksaw->valid() ) {
            if( !testing ) {
                debugmsg( "%s hacksaw is invalid", ter_type.id().str() );
            }
            act.set_to_null();
            return;
        }

        const ter_str_id new_ter = ter_type->hacksaw->result();
        if( !new_ter.is_valid() ) {
            if( !testing ) {
                debugmsg( "hacksaw terrain: %s invalid terrain", new_ter.str() );
            }
            act.set_to_null();
            return;
        }

        data = static_cast<const activity_data_common *>( &*ter_type->hacksaw );
        here.ter_set( target, new_ter );
    } else {
        if( !testing ) {
            debugmsg( "hacksaw activity finished on invalid terrain" );
        }
        act.set_to_null();
        return;
    }

    for( const activity_byproduct &byproduct : data->byproducts() ) {
        const int amount = byproduct.roll();
        if( byproduct.item->count_by_charges() ) {
            here.add_item_or_charges( target, item::spawn( byproduct.item, calendar::turn, amount ) );
        } else {
            for( int i = 0; i < amount; ++i ) {
                here.add_item_or_charges( target, item::spawn( byproduct.item, calendar::turn ) );
            }
        }
    }

    if( !data->message().empty() ) {
        who.add_msg_if_player( m_info, data->message().translated() );
    }

    act.set_to_null();
}

void hacksaw_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.member( "tool", tool );

    jsout.end_object();
}

std::unique_ptr<activity_actor> hacksaw_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<hacksaw_activity_actor> actor( new hacksaw_activity_actor(
                tripoint_bub_ms::zero(), safe_reference<item>() ) );
    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "target", actor->target );
    data.read( "tool", actor->tool );
    return actor;
}

void boltcutting_activity_actor::start( player_activity &act, Character &/*who*/ )
{
    const map &here = get_map();

    if( here.has_furn( target ) ) {
        const furn_id furn_type = here.furn( target );
        if( !furn_type->boltcut->valid() ) {
            if( !testing ) {
                debugmsg( "%s boltcut is invalid", furn_type.id().str() );
            }
            act.set_to_null();
            return;
        }
        progress.emplace( furn_type->name(), to_moves<int>( furn_type->boltcut->duration() ) );
    } else if( !here.ter( target )->is_null() ) {
        const ter_id ter_type = here.ter( target );
        if( !ter_type->boltcut->valid() ) {
            if( !testing ) {
                debugmsg( "%s boltcut is invalid", ter_type.id().str() );
            }
            act.set_to_null();
            return;
        }
        progress.emplace( ter_type->name(), to_moves<int>( ter_type->boltcut->duration() ) );
    } else {
        if( !testing ) {
            debugmsg( "boltcut activity called on invalid terrain" );
        }
        act.set_to_null();
        return;
    }
}

void boltcutting_activity_actor::do_turn( player_activity &/* act */, Character &who )
{
    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
    if( tool->ammo_sufficient() ) {
        tool->ammo_consume( tool->ammo_required(), tool->position() );
    } else {
        if( who.is_avatar() ) {
            who.add_msg_if_player( m_bad, _( "Your %1$s ran out of charges." ), tool->tname() );
        } else { // who.is_npc()
            if( get_avatar().sees( who.bub_pos() ) ) {
                add_msg( _( "%1$s %2$s ran out of charges." ), who.disp_name( false,
                         true ), tool->tname() );
            }
        }
        who.cancel_activity();
    }
}

void boltcutting_activity_actor::finish( player_activity &act, Character &who )
{
    map &here = get_map();
    const activity_data_common *data;

    if( here.has_furn( target ) ) {
        const furn_id furn_type = here.furn( target );
        if( !furn_type->boltcut->valid() ) {
            if( !testing ) {
                debugmsg( "%s boltcut is invalid", furn_type.id().str() );
            }
            act.set_to_null();
            return;
        }

        const furn_str_id new_furn = furn_type->boltcut->result();
        if( !new_furn.is_valid() ) {
            if( !testing ) {
                debugmsg( "boltcut furniture: %s invalid furniture", new_furn.str() );
            }
            act.set_to_null();
            return;
        }

        data = static_cast<const activity_data_common *>( &*furn_type->boltcut );
        here.furn_set( target, new_furn );
    } else if( !here.ter( target )->is_null() ) {
        const ter_id ter_type = here.ter( target );
        if( !ter_type->boltcut->valid() ) {
            if( !testing ) {
                debugmsg( "%s boltcut is invalid", ter_type.id().str() );
            }
            act.set_to_null();
            return;
        }

        const ter_str_id new_ter = ter_type->boltcut->result();
        if( !new_ter.is_valid() ) {
            if( !testing ) {
                debugmsg( "boltcut terrain: %s invalid terrain", new_ter.str() );
            }
            act.set_to_null();
            return;
        }

        data = static_cast<const activity_data_common *>( &*ter_type->boltcut );
        here.ter_set( target, new_ter );
    } else {
        if( !testing ) {
            debugmsg( "boltcut activity finished on invalid terrain" );
        }
        act.set_to_null();
        return;
    }

    if( data->sound().empty() ) {
        sounds::sound( target, 5, sounds::sound_t::combat, _( "Snick, snick, gachunk!" ),
                       true, "tool", "boltcutters" );
    } else {
        sounds::sound( target, 5, sounds::sound_t::combat, data->sound().translated(),
                       true, "tool", "boltcutters" );
    }


    for( const activity_byproduct &byproduct : data->byproducts() ) {
        const int amount = byproduct.roll();
        if( byproduct.item->count_by_charges() ) {
            here.add_item_or_charges( target, item::spawn( byproduct.item, calendar::turn, amount ) );
        } else {
            for( int i = 0; i < amount; ++i ) {
                here.add_item_or_charges( target, item::spawn( byproduct.item, calendar::turn ) );
            }
        }
    }

    if( !data->message().empty() ) {
        who.add_msg_if_player( m_info, data->message().translated() );
    }

    act.set_to_null();
}

void boltcutting_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.member( "tool", tool );

    jsout.end_object();
}

std::unique_ptr<activity_actor> boltcutting_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<boltcutting_activity_actor> actor( new boltcutting_activity_actor(
                tripoint_bub_ms::zero(), safe_reference<item>() ) );

    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "target", actor->target );
    data.read( "tool", actor->tool );
    return actor;
}

// ---- burrow_activity_actor ----

// Shared exertion cost for the mining-family activities (burrow/pickaxe/jackhammer):
// identical kcal/thirst/fatigue drain, avatar-only, halved for stocky troglodytes.
static void apply_mining_exertion( Character &who, int moves )
{
    if( !who.is_avatar() ) {
        return;
    }
    int act_exertion = moves;
    if( who.has_trait( trait_STOCKY_TROGLO ) ) {
        act_exertion /= 2;
    }
    who.mod_stored_kcal( std::min( -1, -act_exertion / to_moves<int>( 45_seconds ) ) );
    who.mod_thirst( std::max( 1, act_exertion / to_moves<int>( 6_minutes ) ) );
    who.mod_fatigue( std::max( 1, act_exertion / to_moves<int>( 3_minutes ) ) );
}

void burrow_activity_actor::start( player_activity &, Character &who )
{
    map &here = get_map();
    int moves = to_moves<int>( 20_minutes );
    moves += ( 24 - std::min( who.str_cur, 24 ) ) * to_moves<int>( 150_seconds );
    if( here.move_cost( here.abs_to_bub( target ) ) == 2 ) {
        moves /= 2;
    }
    const std::vector<npc *> helpers = character_funcs::get_crafting_helpers(
                                           static_cast<player &>( who ), 3 );
    moves = moves * ( 10 - static_cast<int>( helpers.size() ) ) / 10;
    progress.emplace( _( "Burrowing" ), moves );
}

void burrow_activity_actor::do_turn( player_activity &, Character &who )
{
    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
    map &here = get_map();
    sfx::play_activity_sound( "activity", "burrow",
                              sfx::get_heard_volume( here.abs_to_bub( target ) ) );
    if( calendar::once_every( 1_minutes ) ) {
        sounds::sound( here.abs_to_bub( target ), 10, sounds::sound_t::movement,
                       _( "ScratchCrunchScrabbleScurry." ) );
    }
}

void burrow_activity_actor::finish( player_activity &act, Character &who )
{
    map &here = get_map();
    apply_mining_exertion( who, progress.get_moves_total() );
    act.set_to_null();
    who.add_msg_if_player( m_good, _( "You finish burrowing." ) );
    here.destroy( here.abs_to_bub( target ), true );
}

void burrow_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.end_object();
}

std::unique_ptr<activity_actor> burrow_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<burrow_activity_actor> actor( new burrow_activity_actor( tripoint_abs_ms::zero() ) );
    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "target", actor->target );
    return actor;
}

// ---- pickaxe_activity_actor ----

void pickaxe_activity_actor::start( player_activity &, Character &who )
{
    map &here = get_map();
    int moves = to_moves<int>( 30_minutes );
    moves += ( 24 - std::min( who.str_cur, 24 ) ) * to_moves<int>( 225_seconds );
    if( here.move_cost( here.abs_to_bub( target ) ) == 2 ) {
        moves /= 2;
    }
    const std::vector<npc *> helpers = character_funcs::get_crafting_helpers(
                                           static_cast<player &>( who ), 3 );
    moves = moves * ( 10 - static_cast<int>( helpers.size() ) ) / 10;
    progress.emplace( _( "Mining" ), moves );
}

void pickaxe_activity_actor::do_turn( player_activity &, Character &who )
{
    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
    map &here = get_map();
    sfx::play_activity_sound( "tool", "pickaxe",
                              sfx::get_heard_volume( here.abs_to_bub( target ) ) );
    if( calendar::once_every( 1_minutes ) ) {
        sounds::sound( here.abs_to_bub( target ), 30, sounds::sound_t::destructive_activity,
                       _( "CHNK!  CHNK!  CHNK!" ) );
    }
}

void pickaxe_activity_actor::finish( player_activity &act, Character &who )
{
    map &here = get_map();
    const tripoint_bub_ms pos( here.abs_to_bub( target ) );
    apply_mining_exertion( who, progress.get_moves_total() );
    act.set_to_null();
    who.add_msg_player_or_npc( m_good,
                               _( "You finish digging." ),
                               _( "<npcname> finishes digging." ) );
    if( here.has_flag_furn( TFLAG_MINEABLE, pos ) ) {
        here.destroy_furn( pos, true );
    } else {
        here.destroy( pos, true );
    }
    if( !tool ) {
        debugmsg( "pickaxe activity has no tool" );
    } else {
        who.consume_charges( *tool, tool->ammo_required() );
    }
    if( activity_handlers::resume_for_multi_activities( static_cast<player &>( who ) ) ) {
        for( item *&elem : here.i_at( pos ) ) {
            elem->set_var( "activity_var", who.name );
        }
    }
}

void pickaxe_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.member( "tool", tool );
    jsout.end_object();
}

std::unique_ptr<activity_actor> pickaxe_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<pickaxe_activity_actor> actor( new pickaxe_activity_actor(
                tripoint_abs_ms::zero(), safe_reference<item>() ) );
    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "target", actor->target );
    data.read( "tool", actor->tool );
    return actor;
}

// ---- jackhammer_activity_actor ----

void jackhammer_activity_actor::start( player_activity &, Character &who )
{
    map &here = get_map();
    int moves = to_moves<int>( 10_minutes );
    moves += ( 24 - std::min( who.str_cur, 24 ) ) * to_moves<int>( 75_seconds );
    if( here.move_cost( here.abs_to_bub( target ) ) == 2 ) {
        moves /= 2;
    }
    const std::vector<npc *> helpers = character_funcs::get_crafting_helpers(
                                           static_cast<player &>( who ), 3 );
    moves = moves * ( 10 - static_cast<int>( helpers.size() ) ) / 10;
    progress.emplace( _( "Drilling" ), moves );
}

void jackhammer_activity_actor::do_turn( player_activity &, Character &who )
{
    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
    map &here = get_map();
    sfx::play_activity_sound( "tool", "jackhammer",
                              sfx::get_heard_volume( here.abs_to_bub( target ) ) );
    if( calendar::once_every( 1_minutes ) ) {
        sounds::sound( here.abs_to_bub( target ), 15, sounds::sound_t::destructive_activity,
                       _( "TATATATATATATAT!" ) );
    }
}

void jackhammer_activity_actor::finish( player_activity &act, Character &who )
{
    map &here = get_map();
    const auto &pos = here.abs_to_bub( target );

    if( here.has_flag_furn( TFLAG_MINEABLE, pos ) ) {
        here.destroy_furn( pos, true );
    } else {
        here.destroy( pos, true );
    }

    apply_mining_exertion( who, progress.get_moves_total() );
    who.add_msg_player_or_npc( m_good,
                               _( "You finish drilling." ),
                               _( "<npcname> finishes drilling." ) );
    act.set_to_null();
    if( !tool ) {
        debugmsg( "jackhammer activity has no tool" );
    } else {
        who.consume_charges( *tool, tool->ammo_required() );
    }
    if( activity_handlers::resume_for_multi_activities( static_cast<player &>( who ) ) ) {
        for( item *&elem : here.i_at( pos ) ) {
            elem->set_var( "activity_var", who.name );
        }
    }
}

void jackhammer_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.member( "tool", tool );
    jsout.end_object();
}

std::unique_ptr<activity_actor> jackhammer_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<jackhammer_activity_actor> actor( new jackhammer_activity_actor(
                tripoint_abs_ms::zero(), safe_reference<item>() ) );
    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "target", actor->target );
    data.read( "tool", actor->tool );
    return actor;
}

// ---- churn_activity_actor ----

void churn_activity_actor::start( player_activity &, Character & )
{
    progress.emplace( _( "Tilling" ), 18000 );
}

void churn_activity_actor::do_turn( player_activity &, Character & )
{
    if( progress.front().complete() ) {
        progress.pop();
    }
}

void churn_activity_actor::finish( player_activity &act, Character &who )
{
    map &here = get_map();
    who.add_msg_if_player( _( "You finish churning up the earth here." ) );
    here.ter_set( here.abs_to_bub( target ), t_dirtmound );
    act.set_to_null();
    activity_handlers::resume_for_multi_activities( static_cast<player &>( who ) );
}

void churn_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.end_object();
}

std::unique_ptr<activity_actor> churn_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<churn_activity_actor> actor( new churn_activity_actor( tripoint_abs_ms::zero() ) );
    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "target", actor->target );
    return actor;
}

// ---- fill_pit_activity_actor ----

void fill_pit_activity_actor::start( player_activity &, Character &who )
{
    map &here = get_map();
    ter_id ter = here.ter( here.abs_to_bub( target ) );
    int moves = to_moves<int>( time_duration::from_minutes( ter->fill_minutes ) );
    const std::vector<npc *> helpers = character_funcs::get_crafting_helpers(
                                           static_cast<player &>( who ), 3 );
    moves = moves * ( 10 - static_cast<int>( helpers.size() ) ) / 10;
    progress.emplace( _( "Filling" ), moves );
}

void fill_pit_activity_actor::do_turn( player_activity &, Character & )
{
    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
    sfx::play_activity_sound( "tool", "shovel", 100 );
    if( calendar::once_every( 1_minutes ) ) {
        map &here = get_map();
        sounds::sound( here.abs_to_bub( target ), 10, sounds::sound_t::activity, _( "hsh!" ) );
    }
}

void fill_pit_activity_actor::finish( player_activity &act, Character &who )
{
    map &here = get_map();
    const auto bub_pos = here.abs_to_bub( target );
    const ter_id old_ter = here.ter( bub_pos );

    here.ter_set( bub_pos, old_ter->fill_result );
    int act_exertion = to_moves<int>( time_duration::from_minutes( old_ter->fill_minutes ) );
    const int helpersize = character_funcs::get_crafting_helpers( who, 3 ).size();
    act_exertion = act_exertion * ( 10 - helpersize ) / 10;
    who.mod_stored_kcal( std::min( -1, -act_exertion / to_moves<int>( 20_seconds ) ) );
    who.mod_thirst( std::max( 1, act_exertion / to_moves<int>( 3_minutes ) ) );
    who.mod_fatigue( std::max( 1, act_exertion / to_moves<int>( 90_seconds ) ) );
    who.add_msg_if_player( m_good, _( "You finish filling up %s." ), old_ter->name() );
    act.set_to_null();
}

void fill_pit_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.member( "tool", tool );
    jsout.end_object();
}

std::unique_ptr<activity_actor> fill_pit_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<fill_pit_activity_actor> actor( new fill_pit_activity_actor(
                tripoint_abs_ms::zero(), safe_reference<item>() ) );
    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "target", actor->target );
    data.read( "tool", actor->tool );
    return actor;
}

// ---- clear_rubble_activity_actor ----

void clear_rubble_activity_actor::start( player_activity &, Character &who )
{
    int moves = to_moves<int>( 30_seconds );
    int bonus = std::max( who.max_quality( quality_id( "DIG" ) ) - 1, 1 );
    const std::vector<npc *> helpers = character_funcs::get_crafting_helpers(
                                           static_cast<player &>( who ), 3 );
    moves = moves * ( 10 - static_cast<int>( helpers.size() ) ) / 10;
    progress.emplace( _( "Clearing rubble" ), moves / bonus );
}

void clear_rubble_activity_actor::do_turn( player_activity &, Character & )
{
    if( progress.front().complete() ) {
        progress.pop();
    }
}

void clear_rubble_activity_actor::finish( player_activity &act, Character &who )
{
    map &here = get_map();
    const auto bub_pos = here.abs_to_bub( target );
    const map_bash_info &bash = here.furn( bub_pos ).obj().bash;
    who.add_msg_if_player( m_info, _( "You clear up the %s." ),
                           here.furnname( bub_pos ) );
    here.spawn_items( bub_pos, item_group::items_from( bash.drop_group, calendar::turn ) );
    here.furn_set( bub_pos, f_null );
    act.set_to_null();
}

void clear_rubble_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.end_object();
}

std::unique_ptr<activity_actor> clear_rubble_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<clear_rubble_activity_actor> actor( new clear_rubble_activity_actor(
                tripoint_abs_ms::zero() ) );
    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "target", actor->target );
    return actor;
}

// ---- pry_nails_activity_actor ----

void pry_nails_activity_actor::start( player_activity &, Character & )
{
    progress.emplace( _( "Prying nails" ), to_moves<int>( 30_seconds ) );
}

void pry_nails_activity_actor::do_turn( player_activity &, Character & )
{
    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
    map &here = get_map();
    const auto bub_loc = here.abs_to_bub( target );
    sfx::play_activity_sound( "tool", "hammer", sfx::get_heard_volume( bub_loc ) );
}

void pry_nails_activity_actor::finish( player_activity &act, Character &who )
{
    map &here = get_map();
    const auto bub_loc = here.abs_to_bub( target );
    const ter_id type = here.ter( bub_loc );

    who.add_msg_if_player( _( "You pry out the nails from the terrain." ) );

    who.practice( skill_fabrication, 1, 1 );
    here.spawn_item( who.bub_pos(), itype_nail, 1, type->nail_pull_items[0] );
    here.spawn_item( who.bub_pos(), itype_2x4, type->nail_pull_items[1] );
    here.ter_set( bub_loc, type->nail_pull_result );
    act.set_to_null();
}

void pry_nails_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.end_object();
}

std::unique_ptr<activity_actor> pry_nails_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<pry_nails_activity_actor> actor( new pry_nails_activity_actor( tripoint_abs_ms::zero() ) );
    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "target", actor->target );
    return actor;
}

// ---- plant_seed_activity_actor ----

void plant_seed_activity_actor::start( player_activity &, Character & )
{
    progress.emplace( _( "Planting" ), to_moves<int>( 30_seconds ) );
}

void plant_seed_activity_actor::do_turn( player_activity &, Character & )
{
    if( progress.front().complete() ) {
        progress.pop();
    }
}

void plant_seed_activity_actor::finish( player_activity &act, Character &who )
{
    map &here = get_map();
    auto examp = here.abs_to_bub( target );
    std::vector<detached_ptr<item>> used_seed;
    if( item::count_by_charges( seed_id ) ) {
        used_seed = who.use_charges( seed_id, 1 );
    } else {
        used_seed = who.use_amount( seed_id, 1 );
    }
    if( !used_seed.empty() ) {
        used_seed.front()->set_age( 0_turns );
        if( used_seed.front()->has_var( "activity_var" ) ) {
            used_seed.front()->erase_var( "activity_var" );
        }
        used_seed.front()->set_flag( flag_HIDDEN_ITEM );
        here.add_item_or_charges( examp, std::move( used_seed.front() ) );
        if( here.has_flag_furn( seed_id->seed->required_terrain_flag, examp ) ) {
            here.furn_set( examp, furn_str_id( here.furn( examp )->plant->transform ) );
        } else if( seed_id->seed->required_terrain_flag == flag_PLANTABLE ) {
            here.set( examp, t_dirt, f_plant_seed );
        } else {
            here.furn_set( examp, f_plant_seed );
        }
        who.add_msg_player_or_npc( _( "You plant some %s." ), _( "<npcname> plants some %s." ),
                                   item::nname( seed_id ) );
    }
    act.set_to_null();
    activity_handlers::resume_for_multi_activities( static_cast<player &>( who ) );
}

void plant_seed_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.member( "seed_id", seed_id );
    jsout.end_object();
}

std::unique_ptr<activity_actor> plant_seed_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<plant_seed_activity_actor> actor( new plant_seed_activity_actor(
                tripoint_abs_ms::zero(), itype_id() ) );
    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "target", actor->target );
    data.read( "seed_id", actor->seed_id );
    return actor;
}

// ---- forage_activity_actor ----

void forage_activity_actor::start( player_activity &act, Character &who )
{
    act.auto_resume = auto_resume;
    int move_cost = 100000 / ( 2 * who.get_skill_level( skill_survival ) + 5 );
    move_cost /= rng( std::max( 4, who.per_cur ), 4 + who.per_cur * 2 );
    progress.emplace( _( "Foraging" ), move_cost );
}

void forage_activity_actor::do_turn( player_activity &, Character & )
{
    if( progress.front().complete() ) {
        progress.pop();
    }
}

void forage_activity_actor::finish( player_activity &act, Character &who )
{
    // Don't forage if we aren't next to the bush - otherwise we get weird bugs
    bool next_to_bush = false;
    map &here = get_map();
    for( const auto &pnt : here.points_in_radius( who.bub_pos(), 1 ) ) {
        if( here.bub_to_abs( pnt ) == target ) {
            next_to_bush = true;
            break;
        }
    }

    if( !next_to_bush ) {
        act.set_to_null();
        return;
    }

    const int veggy_chance = rng( 1, 100 );
    bool found_something = false;

    item_group_id loc;
    ter_str_id next_ter;

    switch( season_of_year( calendar::turn ) ) {
        case SPRING:
            loc = item_group_id( "forage_spring" );
            next_ter = ter_str_id( "t_underbrush_harvested_spring" );
            break;
        case SUMMER:
            loc = item_group_id( "forage_summer" );
            next_ter = ter_str_id( "t_underbrush_harvested_summer" );
            break;
        case AUTUMN:
            loc = item_group_id( "forage_autumn" );
            next_ter = ter_str_id( "t_underbrush_harvested_autumn" );
            break;
        case WINTER:
            loc = item_group_id( "forage_winter" );
            next_ter = ter_str_id( "t_underbrush_harvested_winter" );
            break;
        default:
            debugmsg( "Invalid season" );
    }

    here.ter_set( here.abs_to_bub( target ), next_ter );

    // Survival gives a bigger boost, and Perception is leveled a bit.
    // Both survival and perception affect time to forage

    ///\EFFECT_PER slightly increases forage success chance
    ///\EFFECT_SURVIVAL increases forage success chance
    if( veggy_chance < who.get_skill_level( skill_survival ) * 3 + who.per_cur - 2 ) {
        const std::vector<item *> dropped = here.put_items_from_loc( loc, who.bub_pos(), calendar::turn );
        for( item *it : dropped ) {
            add_msg( m_good, _( "You found: %s!" ), it->tname() );
            found_something = true;
            if( it->has_flag( flag_FORAGE_POISON ) && one_in( 10 ) ) {
                it->set_flag( flag_HIDDEN_POISON );
                it->poison = rng( 2, 7 );
            }
            if( it->has_flag( flag_FORAGE_HALLU ) && !it->has_flag( flag_HIDDEN_POISON ) && one_in( 10 ) ) {
                it->set_flag( flag_HIDDEN_HALLU );
            }
        }
    }
    // 10% to drop a item/items from this group.
    if( one_in( 10 ) ) {
        const std::vector<item *> dropped = here.put_items_from_loc( item_group_id( "trash_forest" ),
                                            who.bub_pos(),
                                            calendar::turn );
        for( item * const &it : dropped ) {
            add_msg( m_good, _( "You found: %s!" ), it->tname() );
            found_something = true;
        }
    }

    if( !found_something ) {
        add_msg( _( "You didn't find anything." ) );
    }

    iexamine::practice_survival_while_foraging( static_cast<player *>( &who ) );

    act.set_to_null();
}

void forage_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.member( "auto_resume", auto_resume );
    jsout.end_object();
}

std::unique_ptr<activity_actor> forage_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<forage_activity_actor> actor( new forage_activity_actor(
                tripoint_abs_ms::zero(), false ) );
    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "target", actor->target );
    data.read( "auto_resume", actor->auto_resume );
    return actor;
}

// ---- hand_crank_activity_actor ----

void hand_crank_activity_actor::start( player_activity &, Character & )
{
    // Cranking is a sustained, time-based activity with no fixed end: it runs until the battery
    // is full or the player is too tired. Emplace an indefinite task so the activity has a valid
    // (non-empty) progress queue for the framework and progress display; do_turn ends it.
    progress.emplace( _( "Cranking" ), calendar::INDEFINITELY_LONG );
}

void hand_crank_activity_actor::do_turn( player_activity &act, Character &who )
{
    // Hand-crank chargers seem to range from 2 watt (very common easily verified)
    // to 10 watt (suspicious claims from some manufacturers) sustained output.
    // It takes 2.4 minutes to produce 1kj at just slightly under 7 watts (25 kj per hour)
    // time-based instead of speed based because it's a sustained activity
    if( !tool ) {
        act.set_to_null();
        return;
    }
    auto &hand_crank_item = *tool;
    constexpr auto interval_turns_index = 0;
    constexpr auto charge_amount_index = 1;
    constexpr auto fatigue_amount_index = 2;
    constexpr auto ammo_type_index = 0;
    constexpr auto fully_charged_message_index = 1;
    constexpr auto exhausted_message_index = 2;
    auto charge_interval = 144_seconds;
    auto charge_amount = 1;
    auto fatigue_amount = 1;
    auto ammo_type = itype_id( "battery" );
    auto fully_charged_message = std::string( "You've charged the battery completely." );
    auto exhausted_message = std::string( "You're too exhausted to keep cranking." );

    if( values.size() > interval_turns_index ) {
        charge_interval = time_duration::from_turns( values[interval_turns_index] );
    }
    if( values.size() > charge_amount_index ) {
        charge_amount = std::max( 1, values[charge_amount_index] );
    }
    if( values.size() > fatigue_amount_index ) {
        fatigue_amount = std::max( 0, values[fatigue_amount_index] );
    }
    if( str_values.size() > ammo_type_index &&
        !str_values[ammo_type_index].empty() ) {
        ammo_type = itype_id( str_values[ammo_type_index] );
    }
    if( str_values.size() > fully_charged_message_index &&
        !str_values[fully_charged_message_index].empty() ) {
        fully_charged_message = str_values[fully_charged_message_index];
    }
    if( str_values.size() > exhausted_message_index &&
        !str_values[exhausted_message_index].empty() ) {
        exhausted_message = str_values[exhausted_message_index];
    }
    if( charge_interval <= 0_turns ) {
        charge_interval = 144_seconds;
    }

    if( calendar::once_every( charge_interval ) ) {
        who.mod_fatigue( fatigue_amount );
        if( hand_crank_item.ammo_capacity() > hand_crank_item.ammo_remaining() ) {
            const auto current = hand_crank_item.ammo_remaining();
            const auto capacity = hand_crank_item.ammo_capacity();
            const auto next_charges = std::min( capacity, current + charge_amount );
            hand_crank_item.ammo_set( ammo_type, next_charges );
            if( next_charges >= capacity ) {
                add_msg( m_info, _( fully_charged_message ) );
                act.set_to_null();
                return;
            }
        } else {
            add_msg( m_info, _( fully_charged_message ) );
            act.set_to_null();
            return;
        }
    }
    if( who.get_fatigue() >= fatigue_levels::dead_tired ) {
        add_msg( m_info, _( exhausted_message ) );
        act.set_to_null();
        return;
    }
}

void hand_crank_activity_actor::finish( player_activity &, Character & )
{
}

void hand_crank_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "tool", tool );
    jsout.member( "values", values );
    jsout.member( "str_values", str_values );
    jsout.end_object();
}

std::unique_ptr<activity_actor> hand_crank_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<hand_crank_activity_actor> actor(
        new hand_crank_activity_actor( safe_reference<item>(), {}, {} ) );
    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "tool", actor->tool );
    data.read( "values", actor->values );
    data.read( "str_values", actor->str_values );
    return actor;
}

// ---- fill_liquid_activity_actor ----

void fill_liquid_activity_actor::start( player_activity &act, Character & )
{
    parent_activity = &act;
}

void fill_liquid_activity_actor::do_turn( player_activity &act, Character &who )
{
    map &here = get_map();
    try {
        // 1. Prepare source lambda
        liquid_source_type source_type = static_cast<liquid_source_type>( act.values.at( 0 ) );
        auto transfer = [source_type, &here,
                     &act]( const std::function < detached_ptr<item>( detached_ptr<item> &&it ) > & cb ) {
            auto pos = act.coords.at( 0 );
            static const units::volume volume_per_second = units::from_liter( 4.0F / 6.0F );
            int charges;
            detached_ptr<item> source;
            switch( source_type ) {
                case LST_INFINITE_MAP:
                    source = here.water_from( here.abs_to_bub( pos ) );
                    charges = std::max( 1, source->charges_per_volume( volume_per_second ) );
                    source->charges = charges;
                    source = cb( std::move( source ) );
                    return source && source->charges == charges;
                case LST_VEHICLE:
                    auto vp = here.veh_at( pos );
                    if( !vp ) {
                        debugmsg( "Lost track of vehicle source for fill_liquid activity" );
                    }
                    item &base = vp->vehicle().part( act.values.at( 1 ) ).get_base();
                    if( base.contents.empty() ) {
                        return true;
                    }
                    item &source_it = base.contents.back();
                    charges = std::max( 1, source_it.charges_per_volume( volume_per_second ) );
                    int orig = source_it.charges;
                    source_it.attempt_split( charges, cb );
                    return source_it.charges == 0 || source_it.charges == orig;
            }
            return false;
        };
        bool finished = true;
        // 2. Transfer charges.
        switch( static_cast<liquid_target_type>( act.values.at( 2 ) ) ) {
            case LTT_VEHICLE:
                if( const optional_vpart_position vp = here.veh_at( act.coords.at( 1 ) ) ) {
                    finished = transfer( [&who, &vp]( detached_ptr<item> &&it ) {
                        return who.pour_into( vp->vehicle(), std::move( it ) );
                    } );
                } else {
                    throw std::runtime_error( "could not find target vehicle for liquid transfer" );
                }
                break;
            case LTT_MAP: {
                const auto bub_loc = here.abs_to_bub( act.coords.at( 1 ) );
                if( iexamine::has_keg( bub_loc ) ) {
                    finished = transfer( [&bub_loc]( detached_ptr<item> &&it ) {
                        return iexamine::pour_into_keg( bub_loc, std::move( it ) );
                    } );
                } else {
                    finished = transfer( [&who, &bub_loc, &here]( detached_ptr<item> &&it ) {
                        who.add_msg_if_player( _( "You pour %1$s onto the ground." ), it->tname() );
                        here.add_item_or_charges( bub_loc, std::move( it ) );
                        return detached_ptr<item>();
                    } );
                }
            }
            break;
            case LTT_MONSTER:
                //Do nothing here
                break;
            case LTT_CONTAINER:
                safe_reference<item> &container = act.targets.at( 0 );
                if( !container ) {
                    throw std::runtime_error( "could not find target container for liquid transfer" );
                }

                finished = transfer( [&who, &container]( detached_ptr<item> &&it ) {
                    return who.pour_into( *container,  std::move( it ) );
                } );

                break;
        }
        if( finished ) {
            act.set_to_null();
        }

    } catch( const std::runtime_error &err ) {
        debugmsg( "error in activity data: \"%s\"", err.what() );
        act.set_to_null();
        return;
    }
}

void fill_liquid_activity_actor::finish( player_activity &, Character & )
{
}

void fill_liquid_activity_actor::serialize( JsonOut &jsout ) const
{
    // Fill_liquid serialization is handled via serialize_liquid_source/
    // serialize_liquid_target into the player_activity bag.
    // The actor itself delegates to those external helpers.
    jsout.write_null();
}

std::unique_ptr<activity_actor> fill_liquid_activity_actor::deserialize( JsonIn & )
{
    return std::make_unique<fill_liquid_activity_actor>();
}

// ---- fertilize_plot_activity_actor ----

void fertilize_plot_activity_actor::start( player_activity &act, Character & )
{
    if( !str_value.empty() ) {
        act.str_values.clear();
        act.str_values.emplace_back( str_value );
    }
}

void fertilize_plot_activity_actor::do_turn( player_activity &act, Character &who )
{
    itype_id fertilizer;
    auto check_fertilizer = [&]( bool ask_user = true ) -> void {
        if( act.str_values.empty() )
        {
            act.str_values.emplace_back( "" );
        }
        fertilizer = itype_id( act.str_values[0] );

        if( ask_user && ( fertilizer.is_empty() || !who.has_charges( fertilizer, 1 ) ) )
        {
            fertilizer = iexamine::choose_fertilizer( static_cast<player &>( who ), "plant",
                    false );
            act.str_values[0] = fertilizer.str();
        }
    };

    auto have_fertilizer = [&]() {
        return !fertilizer.is_empty() && who.has_charges( fertilizer, 1 );
    };

    const auto reject_tile = [&]( const tripoint_bub_ms & tile ) {
        check_fertilizer();
        ret_val<bool> can_fert = iexamine::can_fertilize( static_cast<player &>( who ), tile, fertilizer );
        return !can_fert.success();
    };

    const auto fertilize = [&]( player &p, const tripoint_bub_ms & tile ) {
        check_fertilizer();
        if( have_fertilizer() ) {
            iexamine::fertilize_plant( p, tile, fertilizer );
            if( !have_fertilizer() ) {
                add_msg( m_info, _( "You have run out of %s." ), item::nname( fertilizer ) );
            }
        }
    };

    check_fertilizer();
    if( !have_fertilizer() ) {
        act.set_to_null();
        return;
    }

    activity_handlers::perform_zone_activity_turn( static_cast<player *>( &who ),
                                zone_type_FARM_PLOT,
                                reject_tile,
                                fertilize,
                                _( "You fertilized every plot you could." ) );
}

void fertilize_plot_activity_actor::finish( player_activity &, Character & )
{
}

void fertilize_plot_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "str_value", str_value );
    jsout.end_object();
}

std::unique_ptr<activity_actor> fertilize_plot_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<fertilize_plot_activity_actor> actor( new fertilize_plot_activity_actor() );
    JsonObject data = jsin.get_object();
    data.read( "str_value", actor->str_value );
    return actor;
}

std::unique_ptr<lockpick_activity_actor> lockpick_activity_actor::use_item(
    int moves_total,
    item &lockpick,
    const tripoint_abs_ms &target
)
{
    return std::unique_ptr<lockpick_activity_actor> ( new lockpick_activity_actor(
                moves_total,
                safe_reference<item>( lockpick ),
                detached_ptr<item>(),
                target
            ) );
}

std::unique_ptr<lockpick_activity_actor> lockpick_activity_actor::use_bionic(
    detached_ptr<item> &&fake_lockpick,
    const tripoint_abs_ms &target
)
{
    return std::unique_ptr<lockpick_activity_actor>( new lockpick_activity_actor(
                to_moves<int>( 5_seconds ),
                safe_reference<item>(),
                std::move( fake_lockpick ),
                target
            ) );
}

void lockpick_activity_actor::start( player_activity &/*act*/, Character & )
{
    const auto target = get_map().abs_to_bub( this->target );
    const ter_id ter_type = get_map().ter( target );
    const furn_id furn_type = get_map().furn( target );
    const optional_vpart_position veh = get_map().veh_at( target );
    const auto door_lock = veh.part_with_feature( "DOOR_LOCKING", true );

    if( furn_type != f_null && !furn_type->lockpick_result.is_null() ) {
        progress.emplace( furn_type->name(), moves_total );
    } else if( veh && door_lock ) {
        progress.emplace( veh->vehicle().name, moves_total );
    } else {
        if( ter_type->lockpick_result.is_null() ) {
            debugmsg( "%s lockpick_result is null", ter_type.id().str() );
            return;
        }
        progress.emplace( ter_type->name(), moves_total );
    }
}

void lockpick_activity_actor::do_turn( player_activity &/* act */, Character & )
{
    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
}

void lockpick_activity_actor::finish( player_activity &act, Character &who )
{
    act.set_to_null();

    item *it = nullptr;
    if( lockpick ) {
        it = &*lockpick;
    } else if( fake_lockpick ) {
        it = &*fake_lockpick;
    }

    if( !it ) {
        debugmsg( "Lost ACT_LOCKPICK item" );
        return;
    }

    const auto target = get_map().abs_to_bub( this->target );
    const ter_id ter_type = get_map().ter( target );
    const furn_id furn_type = get_map().furn( target );
    const optional_vpart_position veh = get_map().veh_at( target );
    const auto door_lock = veh.part_with_feature( "DOOR_LOCKING", true );

    ter_id new_ter_type = t_null;
    furn_id new_furn_type = f_null;
    std::string open_message = _( "The lock opens…" );

    if( furn_type != f_null ) {
        if( furn_type->lockpick_result.is_null() ) {
            debugmsg( "%s lockpick_result is null", furn_type.id().str() );
            return;
        }

        new_furn_type = furn_type->lockpick_result;
        if( !furn_type->lockpick_message.empty() ) {
            open_message = furn_type->lockpick_message.translated();
        }
    } else if( veh ) {
        if( !door_lock ) {
            debugmsg( "%s has no pickable part", furn_type.id().str() );
            return;
        }
    } else {
        if( ter_type->lockpick_result.is_null() ) {
            debugmsg( "%s lockpick_result is null", ter_type.id().str() );
            return;
        }

        new_ter_type = ter_type->lockpick_result;
        if( !ter_type->lockpick_message.empty() ) {
            open_message = ter_type->lockpick_message.translated();
        }
    }

    bool perfect = it->has_flag( flag_PERFECT_LOCKPICK );
    bool durable = it->has_flag( flag_DURABLE_LOCKPICK );
    bool destroy = false;

    /** @EFFECT_DEX improves chances of successfully picking door lock, reduces chances of bad outcomes */
    /** @EFFECT_MECHANICS improves chances of successfully picking door lock, reduces chances of bad outcomes */
    int pick_roll = 5 *
                    ( std::pow( 1.3, who.get_skill_level( skill_mechanics ) ) +
                      it->get_quality( qual_LOCKPICK ) - it->damage() / 2000.0 ) +
                    who.dex_cur / 4.0;
    int lock_roll = rng( 1, 120 );
    int xp_gain = 0;
    if( perfect || ( pick_roll >= lock_roll ) ) {
        xp_gain += lock_roll;

        if( furn_type != f_null ) {
            get_map().furn_set( target, new_furn_type );
        } else if( door_lock ) {
            door_lock->part().enabled = false;
        } else {
            get_map().ter_set( target, new_ter_type );
        }

        who.add_msg_if_player( m_good, open_message );
    } else if( lock_roll > ( 1.5 * pick_roll ) && !durable ) {
        // damage lockpick on a low result, unless it's durable
        if( it->inc_damage() ) {
            who.add_msg_if_player( m_bad,
                                   _( "The lock stumps your efforts to pick it, and you destroy your tool." ) );
            destroy = true;
        } else {
            who.add_msg_if_player( m_bad,
                                   _( "The lock stumps your efforts to pick it, and you damage your tool." ) );
        }
    } else {
        who.add_msg_if_player( m_bad, _( "The lock stumps your efforts to pick it." ) );
    }

    if( !perfect ) {
        // You don't gain much skill since the item does all the hard work for you
        xp_gain += std::pow( 2, who.get_skill_level( skill_mechanics ) ) + 1;
    }
    who.practice( skill_mechanics, xp_gain );

    if( !perfect
        && ( lock_roll + dice( 1, 30 ) ) > pick_roll ) {

        if( get_map().has_flag( "ALARMED", target ) ) {
            sounds::sound( who.bub_pos(), 40, sounds::sound_t::alarm, _( "an alarm sound!" ),
                           true, "environment", "alarm" );
            if( !g->timed_events.queued( TIMED_EVENT_WANTED ) ) {
                g->timed_events.add( TIMED_EVENT_WANTED, calendar::turn + 30_minutes, 0,
                                     who.abs_sm_pos() );
            }
        } else if( veh && veh->vehicle().has_security_working() ) {
            veh->vehicle().is_alarm_on = true;
        }
    }

    if( destroy && lockpick ) {
        lockpick->detach();
    }
}

bool lockpick_activity_actor::is_pickable( const tripoint_bub_ms &p )
{
    const ter_id ter_type = get_map().ter( p );
    const furn_id furn_type = get_map().furn( p );
    const optional_vpart_position veh = get_map().veh_at( p );
    const auto door_lock = veh.part_with_feature( "DOOR_LOCKING", true );

    bool result;
    if( furn_type != f_null ) {
        result = !furn_type->lockpick_result.is_null();
    } else if( door_lock ) {
        result = door_lock.value().part().enabled;
    } else {
        result = !ter_type->lockpick_result.is_null();
    }

    return result;
}

std::optional<tripoint_bub_ms> lockpick_activity_actor::select_location( avatar &you )
{
    if( you.is_mounted() ) {
        you.add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return std::nullopt;
    }

    const std::optional<tripoint_bub_ms> target = choose_adjacent_highlight(
                _( "Use your lockpick where?" ), _( "There is nothing to lockpick nearby." ), is_pickable, false );
    if( !target ) {
        return std::nullopt;
    }

    if( is_pickable( *target ) ) {
        return *target;
    }

    const ter_id terr_type = get_map().ter( *target );
    if( *target == you.bub_pos() ) {
        you.add_msg_if_player( m_info, _( "You pick your nose and your sinuses swing open." ) );
    } else if( g->critter_at<npc>( *target ) ) {
        you.add_msg_if_player( m_info,
                               _( "You can pick your friends, and you can pick your nose, but you can't pick your friend's nose." ) );
    } else if( !terr_type->open.is_null() ) {
        you.add_msg_if_player( m_info, _( "That door isn't locked." ) );
    } else {
        you.add_msg_if_player( m_info, _( "That cannot be picked." ) );
    }
    return std::nullopt;
}

void lockpick_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "moves_total", moves_total );
    jsout.member( "lockpick", lockpick );
    jsout.member( "fake_lockpick", fake_lockpick );
    jsout.member( "target", target );

    jsout.end_object();
}

std::unique_ptr<activity_actor> lockpick_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<lockpick_activity_actor> actor( new lockpick_activity_actor( 0,
            safe_reference<item>(), detached_ptr<item>(), tripoint_abs_ms::zero() ) );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "moves_total", actor->moves_total );
    data.read( "lockpick", actor->lockpick );
    data.read( "fake_lockpick", actor->fake_lockpick );
    data.read( "target", actor->target );

    return actor;
}

void oxytorch_activity_actor::start( player_activity &act, Character &/*who*/ )
{
    const map &here = get_map();

    if( here.has_furn( target ) ) {
        const furn_id furn_type = here.furn( target );
        if( !furn_type->oxytorch->valid() ) {
            if( !testing ) {
                debugmsg( "%s oxytorch is invalid", furn_type.id().str() );
            }
            act.set_to_null();
            return;
        }
        progress.emplace( furn_type->name(), to_moves<int>( furn_type->oxytorch->duration() ) );
    } else if( !here.ter( target )->is_null() ) {
        const ter_id ter_type = here.ter( target );
        if( !ter_type->oxytorch->valid() ) {
            if( !testing ) {
                debugmsg( "%s oxytorch is invalid", ter_type.id().str() );
            }
            act.set_to_null();
            return;
        }
        progress.emplace( ter_type->name(), to_moves<int>( ter_type->oxytorch->duration() ) );
    } else {
        if( !testing ) {
            debugmsg( "oxytorch activity called on invalid terrain" );
        }
        act.set_to_null();
        return;
    }
}

void oxytorch_activity_actor::do_turn( player_activity &/*act*/, Character &who )
{
    // We check available charges when first starting the cut, but this prevents abnormal behavior if torch status changes mid-activity.
    if( tool->ammo_sufficient() ) {
        tool->ammo_consume( tool->ammo_required(), tool->position() );
        sfx::play_activity_sound( "tool", "oxytorch", sfx::get_heard_volume( target ) );
        if( calendar::once_every( 2_turns ) ) {
            sounds::sound( target, 10, sounds::sound_t::destructive_activity, _( "hissssssssss!" ) );
        }
    } else {
        if( who.is_avatar() ) {
            who.add_msg_if_player( m_bad, _( "Your %1$s ran out of charges." ), tool->tname() );
        } else { // who.is_npc()
            if( get_avatar().sees( who.bub_pos() ) ) {
                add_msg( _( "%1$s %2$s ran out of charges." ), who.disp_name( false,
                         true ), tool->tname() );
            }
        }
        who.cancel_activity();
    }
    if( progress.front().complete() ) {
        progress.pop();
    }
}

void oxytorch_activity_actor::finish( player_activity &act, Character &who )
{
    map &here = get_map();
    const activity_data_common *data;

    if( here.has_furn( target ) ) {
        const furn_id furn_type = here.furn( target );
        if( !furn_type->oxytorch->valid() ) {
            if( !testing ) {
                debugmsg( "%s oxytorch is invalid", furn_type.id().str() );
            }
            act.set_to_null();
            return;
        }

        const furn_str_id new_furn = furn_type->oxytorch->result();
        if( !new_furn.is_valid() ) {
            if( !testing ) {
                debugmsg( "oxytorch furniture: %s invalid furniture", new_furn.str() );
            }
            act.set_to_null();
            return;
        }

        data = static_cast<const activity_data_common *>( &*furn_type->oxytorch );
        here.furn_set( target, new_furn );
    } else if( !here.ter( target )->is_null() ) {
        const ter_id ter_type = here.ter( target );
        if( !ter_type->oxytorch->valid() ) {
            if( !testing ) {
                debugmsg( "%s oxytorch is invalid", ter_type.id().str() );
            }
            act.set_to_null();
            return;
        }

        const ter_str_id new_ter = ter_type->oxytorch->result();
        if( !new_ter.is_valid() ) {
            if( !testing ) {
                debugmsg( "oxytorch terrain: %s invalid terrain", new_ter.str() );
            }
            act.set_to_null();
            return;
        }

        data = static_cast<const activity_data_common *>( &*ter_type->oxytorch );
        here.ter_set( target, new_ter );
    } else {
        if( !testing ) {
            debugmsg( "oxytorch activity finished on invalid terrain" );
        }
        act.set_to_null();
        return;
    }

    for( const activity_byproduct &byproduct : data->byproducts() ) {
        const int amount = byproduct.roll();
        if( byproduct.item->count_by_charges() ) {
            here.add_item_or_charges( target, item::spawn( byproduct.item, calendar::turn, amount ) );
        } else {
            for( int i = 0; i < amount; ++i ) {
                here.add_item_or_charges( target, item::spawn( byproduct.item, calendar::turn ) );
            }
        }
    }

    // 50% chance of starting a fire.
    if( one_in( 2 ) && here.flammable_items_at( target ) ) {
        here.add_field( target, fd_fire, 1, 10_minutes );
    }

    if( !data->message().empty() ) {
        who.add_msg_if_player( m_info, data->message().translated() );
    }

    act.set_to_null();
}

void oxytorch_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.member( "tool", tool );
    jsout.end_object();
}

std::unique_ptr<activity_actor> oxytorch_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<oxytorch_activity_actor> actor( new oxytorch_activity_actor(
                tripoint_bub_ms::zero(), safe_reference<item>() ) );
    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "target", actor->target );
    data.read( "tool", actor->tool );
    return actor;
}

void migration_cancel_activity_actor::do_turn( player_activity &act, Character &who )
{
    // Stop the activity
    act.set_to_null();

    // Ensure that neither avatars nor npcs end up in an invalid state
    if( who.is_npc() ) {
        npc &npc_who = dynamic_cast<npc &>( who );
        npc_who.revert_after_activity();
    } else {
        avatar &avatar_who = dynamic_cast<avatar &>( who );
        avatar_who.clear_destination();
        avatar_who.backlog.clear();
    }
}

void migration_cancel_activity_actor::serialize( JsonOut &jsout ) const
{
    // This will probably never be called, but write null to avoid invalid json in
    // the case that it is
    jsout.write_null();
}

std::unique_ptr<activity_actor> migration_cancel_activity_actor::deserialize( JsonIn & )
{
    return std::unique_ptr<migration_cancel_activity_actor>();
}

void toggle_gate_activity_actor::start( player_activity &, Character & )
{
    progress.emplace( "gate", moves_total );
}

void toggle_gate_activity_actor::do_turn( player_activity &, Character & )
{
    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
}

void toggle_gate_activity_actor::finish( player_activity &act, Character & )
{
    gates::toggle_gate( placement );
    act.set_to_null();
}

void toggle_gate_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "moves", moves_total );
    jsout.member( "placement", placement );

    jsout.end_object();
}

std::unique_ptr<activity_actor> toggle_gate_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<toggle_gate_activity_actor> actor( new toggle_gate_activity_actor( 0,
            tripoint_bub_ms::zero() ) );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "moves", actor->moves_total );
    data.read( "placement", actor->placement );

    return actor;
}


stash_activity_actor::stash_activity_actor( Character &ch, const drop_locations &items,
        const tripoint_rel_ms &relpos ) : relpos( relpos )
{
    this->items = pickup::reorder_for_dropping( ch, items );
}

void stash_activity_actor::start( player_activity &, Character & )
{
    // Dummy progress task to indicate ongoing activity
    progress.dummy();
}

void stash_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "items", items );
    jsout.member( "relpos", relpos );

    jsout.end_object();
}

std::unique_ptr<activity_actor> stash_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<stash_activity_actor> actor( new stash_activity_actor() );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "items", actor->items );
    data.read( "relpos", actor->relpos );

    return actor;
}

void throw_activity_actor::do_turn( player_activity &act, Character &who )
{
    // Make copies of relevant values since the class would
    // not be available after act.set_to_null()
    if( !target ) {
        debugmsg( "Lost weapon while throwing" );
        act.set_to_null();
        return;
    }

    item *it = &*target;
    std::optional<tripoint_bub_ms> blind_throw_pos = blind_throw_from_pos;

    // Stop the activity. Whether we will or will not throw doesn't matter.
    act.set_to_null();
    if( !who.is_avatar() ) {
        // Sanity check
        debugmsg( "ACT_THROW is not applicable for NPCs." );
        return;
    }

    // Shift our position to our "peeking" position, so that the UI
    // for picking a throw point lets us target the location we couldn't
    // otherwise see.
    const auto original_player_position = who.bub_pos();
    if( blind_throw_pos ) {
        who.setpos( *blind_throw_pos );
    }

    target_handler::trajectory trajectory = target_handler::mode_throw( *who.as_avatar(), *it,
                                            blind_throw_pos.has_value() );

    // If we previously shifted our position, put ourselves back now that we've picked our target.
    if( blind_throw_pos ) {
        who.setpos( original_player_position );
    }

    if( trajectory.empty() ) {
        return;
    }

    if( it != &who.primary_weapon() ) {
        // This is to represent "implicit offhand wielding"
        int extra_cost = who.item_handling_cost( *it, true, INVENTORY_HANDLING_PENALTY / 2 );
        who.mod_moves( -extra_cost );
    }
    detached_ptr<item> det = target->split( 1 );
    ranged::throw_item( who, trajectory.back(), std::move( det ), blind_throw_pos );
}

void throw_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "target_loc", target );
    jsout.member( "blind_throw_from_pos", blind_throw_from_pos );

    jsout.end_object();
}

std::unique_ptr<activity_actor> throw_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<throw_activity_actor> actor( new throw_activity_actor() );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "target_loc", actor->target );
    data.read( "blind_throw_from_pos", actor->blind_throw_from_pos );

    return actor;
}


// ---- craft_activity_actor ----

craft_activity_actor::craft_activity_actor(
    const recipe *rec,
    int batch_size,
    int craft_counter,
    const tripoint_abs_ms &location,
    std::vector<comp_selection<item_comp>> item_selections,
    std::vector<comp_selection<tool_comp>> tool_selections,
    bool tools_prepaid,
    bool is_long
) : rec( rec ), batch_size( batch_size ), craft_counter( craft_counter ),
    location( location ),
    item_selections( std::move( item_selections ) ),
    tool_selections( std::move( tool_selections ) ),
    tools_prepaid( tools_prepaid ),
    is_long( is_long ),
    is_valid( rec != nullptr )
{}

auto craft_activity_actor::find_in_progress_craft( const player_activity &act,
        Character &who ) const -> item * // *NOPAD*
{
    if( !act.targets.empty() && act.targets.front() && act.targets.front()->is_craft() &&
        &act.targets.front()->get_making() == rec ) {
        return &*act.targets.front();
    }

    item *result = nullptr;
    who.visit_items( [&]( item * it ) {
        if( it->is_craft() && &it->get_making() == rec ) {
            result = it;
            return VisitResponse::ABORT;
        }
        return VisitResponse::NEXT;
    } );
    if( result ) {
        return result;
    }
    // If not in inventory, check the map at the crafter's feet — set_item_inventory
    // may have placed it there if the NPC was over their carry capacity.
    map_selector sel( who.bub_pos(), 0 );
    sel.visit_items( [&]( item * it ) {
        if( it->is_craft() && &it->get_making() == rec ) {
            result = it;
            return VisitResponse::ABORT;
        }
        return VisitResponse::NEXT;
    } );
    return result;
}

void craft_activity_actor::calc_all_moves( player_activity &act, Character &who )
{
    if( !rec || !is_valid ) {
        act.set_to_null();
        return;
    }

    const int current_turn = to_turn<int>( calendar::turn );

    // Catch-up: apply time elapsed while NPC was outside the reality bubble.
    // last_turn_nr >= 0 means start() already ran in a previous session.
    if( last_turn_nr >= 0 && current_turn > last_turn_nr ) {
        item *craft_item = find_in_progress_craft( act, who );
        if( craft_item ) {
            const int elapsed_turns = current_turn - last_turn_nr;
            const double base_total_moves = std::max( 1, rec->batch_time( batch_size, 1.0f, 0 ) );
            // 100 moves per turn at base speed (no modifiers applied while outside bubble)
            const double moves_elapsed = elapsed_turns * 100.0;
            const int old_counter = craft_item->get_counter();
            const int new_counter = std::min(
                                        static_cast<int>( old_counter + moves_elapsed / base_total_moves * 10'000'000.0 ),
                                        10'000'000 );
            craft_item->set_counter( new_counter );
            craft_counter = new_counter;

            const int five_percent_steps = new_counter / 500'000 - old_counter / 500'000;
            if( five_percent_steps > 0 ) {
                who.craft_skill_gain( *craft_item, five_percent_steps );
            }

            // Re-build progress counter to match updated craft state
            const int remaining = std::max( 0, static_cast<int>(
                                                base_total_moves * ( 1.0 - new_counter / 10'000'000.0 ) ) );
            if( !activity_actor::progress.empty() ) {
                activity_actor::progress.mod_moves_left(
                    remaining - activity_actor::progress.get_moves_left() );
            } else {
                activity_actor::progress.emplace( craft_item->tname(),
                                                  static_cast<int>( base_total_moves ), remaining );
            }

            if( new_counter >= 10'000'000 ) {
                // Drain so complete() fires on the next do_turn check
                activity_actor::progress.mod_moves_left( -activity_actor::progress.get_moves_left() );
            }
        }
    }

    last_turn_nr = current_turn;

    // Re-build progress counter after deserialization if catch-up didn't already do it
    if( activity_actor::progress.empty() ) {
        item *craft_item = find_in_progress_craft( act, who );
        const std::string name = craft_item ? craft_item->tname() : rec->result_name();
        const int base_total = std::max( 1, rec->batch_time( batch_size, 1.0f, 0 ) );
        const int remaining = std::max( 1, static_cast<int>(
                                            base_total * ( 1.0 - craft_counter / 10'000'000.0 ) ) );
        activity_actor::progress.emplace( name, base_total, remaining );
    }

    item *craft_item = find_in_progress_craft( act, who );
    if( craft_item ) {
        refresh_speed( act, who, *craft_item );
    }
}

void craft_activity_actor::refresh_speed( player_activity &act, const Character &who,
        const item &craft_item, std::optional<bench_location> bench ) const
{
    const bench_location resolved_bench = bench ? *bench : find_best_bench( who, craft_item );
    const recipe &making = *rec;
    const float tools_mult = cached_tools_mult != 0.0f ? cached_tools_mult
                             : crafting_tools_speed_multiplier( who, making );
    act.speed.light        = lighting_crafting_speed_multiplier( who, making );
    act.speed.bench_factor = workbench_crafting_speed_multiplier( craft_item, resolved_bench );
    act.speed.morale       = morale_crafting_speed_multiplier( who, making );
    act.speed.tools        = tools_mult;
    act.speed.player_speed = who.get_speed() / 100.0f;
    const int assistants   = who.available_assistant_count( making );
    if( assistants > 0 ) {
        const double base_no_assist   = std::max( 1, making.batch_time( batch_size, 1.0f, 0 ) );
        const double base_with_assist = std::max( 1, making.batch_time( batch_size, 1.0f, assistants ) );
        act.speed.assist = static_cast<float>( base_no_assist / base_with_assist );
    } else {
        act.speed.assist = 1.0f;
    }
    // Mutation and game-option multipliers have no dedicated speed field; fold them
    // into skills so act.speed.total() matches the actual crafting rate.
    const float mutation_mult = who.mutation_value( "crafting_speed_modifier" );
    const float game_opt_mult = get_option<int>( "CRAFTING_SPEED_MULT" ) == 0
                                ? 9999.0f
                                : 100.0f / static_cast<float>( get_option<int>( "CRAFTING_SPEED_MULT" ) );
    act.speed.skills = mutation_mult * game_opt_mult;
}

void craft_activity_actor::start( player_activity &act, Character &who )
{
    if( !rec || !is_valid ) {
        act.set_to_null();
        return;
    }

    item *craft_item = find_in_progress_craft( act, who );
    if( !craft_item ) {
        who.add_msg_player_or_npc(
            _( "You lost your in progress %s and had to stop crafting." ),
            _( "<npcname> lost the in progress %s and had to stop crafting." ),
            rec->result_name() );
        act.set_to_null();
        return;
    }

    cached_tools_mult = crafting_tools_speed_multiplier( who, *rec );
    craft_counter = craft_item->get_counter();
    last_turn_nr = to_turn<int>( calendar::turn );  // mark fresh start so calc_all_moves skips catch-up
    const int base_total = std::max( 1, rec->batch_time( batch_size, 1.0f, 0 ) );
    const int remaining = craft_counter == 0
                          ? base_total
                          : std::max( 1, static_cast<int>( base_total * ( 1.0 - craft_counter / 10'000'000.0 ) ) );
    activity_actor::progress.emplace( craft_item->tname(), base_total, remaining );
}

void craft_activity_actor::do_turn( player_activity &act, Character &who )
{
    if( !rec || !is_valid ) {
        act.set_to_null();
        return;
    }

    item *craft_item = find_in_progress_craft( act, who );
    if( !craft_item ) {
        who.add_msg_player_or_npc(
            _( "You no longer have the in progress craft in your possession.  "
               "You stop crafting.  "
               "Reactivate the in progress craft to continue crafting." ),
            _( "<npcname> no longer has the in progress craft in their possession.  "
               "<npcname> stops crafting." ) );
        act.set_to_null();
        return;
    }

    const recipe &making = *rec;
    if( cached_tools_mult == 0.0f ) {
        cached_tools_mult = crafting_tools_speed_multiplier( who, making );
    }
    const bench_location bench = find_best_bench( who, *craft_item );
    refresh_speed( act, who, *craft_item, bench );
    const float crafting_speed = crafting_speed_multiplier( who, *craft_item, bench, act.speed.tools );
    const int assistants = who.available_assistant_count( making );

    if( crafting_speed <= 0.0f ) {
        who.add_msg_player_or_npc( m_bad,
                                   _( "You cannot continue crafting." ),
                                   _( "<npcname> cannot continue crafting." ) );
        act.set_to_null();
        return;
    }

    const int old_counter = craft_item->get_counter();
    const double base_total_moves = std::max( 1, making.batch_time( batch_size, 1.0f, 0 ) );
    const double cur_total_moves = std::max( 1, making.batch_time( batch_size, crafting_speed,
                                   assistants ) );
    const double delta_progress = who.get_moves() > 0
                                  ? who.get_moves() * base_total_moves / cur_total_moves
                                  : 0.0;
    const double current_progress = old_counter * base_total_moves / 10'000'000.0 + delta_progress;
    const int new_counter = std::min(
                                static_cast<int>( std::round( current_progress / base_total_moves * 10'000'000.0 ) ),
                                10'000'000 );
    const int five_percent_steps = new_counter / 500'000 - old_counter / 500'000;
    craft_item->set_counter( new_counter );
    craft_counter = new_counter;

    who.set_moves( 0 );

    if( five_percent_steps > 0 ) {
        who.craft_skill_gain( *craft_item, five_percent_steps );

        if( !tools_prepaid && !who.craft_consume_tools( *craft_item, five_percent_steps, false ) ) {
            act.set_to_null();
            return;
        }
    }

    // Keep the progress_counter in sync so the UI shows correct values
    if( !activity_actor::progress.empty() ) {
        const int new_moves_left = static_cast<int>(
                                       base_total_moves * ( 1.0 - static_cast<double>( new_counter ) / 10'000'000.0 ) );
        const int delta = new_moves_left - activity_actor::progress.get_moves_left();
        if( delta != 0 ) {
            activity_actor::progress.mod_moves_left( delta );
        }
    }

    last_turn_nr = to_turn<int>( calendar::turn );

    if( new_counter >= 10'000'000 ) {
        // Signal completion so player_activity::do_turn calls finish()
        if( !activity_actor::progress.empty() ) {
            activity_actor::progress.mod_moves_left( -activity_actor::progress.get_moves_left() );
        }
    } else if( new_counter >= craft_item->get_next_failure_point() ) {
        const bool destroy = craft_item->handle_craft_failure( who );
        if( destroy ) {
            who.add_msg_player_or_npc(
                _( "There is nothing left of the %s to craft from." ),
                _( "There is nothing left of the %s <npcname> was crafting." ),
                craft_item->tname() );
            craft_item->detach();
            act.set_to_null();
        }
        // If !destroy, handle_craft_failure may have called cancel_activity already
    }
}

void craft_activity_actor::finish( player_activity &act, Character &who )
{
    act.set_to_null();
    do_complete_craft( act, who );
}

void craft_activity_actor::do_complete_craft( player_activity &act, Character &who )
{
    item *craft_item = find_in_progress_craft( act, who );
    if( !craft_item ) {
        debugmsg( "craft_activity_actor::do_complete_craft: no craft item found for %s",
                  rec ? rec->result_name() : "unknown" );
        return;
    }
    ::complete_craft( who, *craft_item );
    craft_item->detach();
    if( is_long && rec ) {
        if( who.making_would_work( rec->ident(), batch_size ) ) {
            who.last_craft->execute( get_map().abs_to_bub( location ) );
        }
    }
}

act_progress_message craft_activity_actor::get_progress_message(
    const player_activity &act, const Character &who ) const
{
    if( !rec || !is_valid ) {
        return act_progress_message::make_empty();
    }

    const int assistants = who.available_assistant_count( *rec );
    const double base_total_moves = std::max( 1, rec->batch_time( batch_size, 1.0f, 0 ) );
    const double remaining_pct = 1.0 - craft_counter / 10'000'000.0;
    const float total_mult = act.speed.total();
    const int remaining_turns = static_cast<int>( remaining_pct * base_total_moves / 100 /
                                std::max( 0.01f, total_mult ) );

    const std::string time_desc = string_format( _( "Time left: %s" ),
                                  to_string( time_duration::from_turns( remaining_turns ) ) );

    const auto fmt_spd = [&]( float level, const std::string & name ) -> std::string {
        const int pct = static_cast<int>( level * 100 );
        if( pct == 100 )
        {
            return "";
        }
        nc_color col = pct > 100 ? c_green : c_red;
        return string_format( " - %s: %s\n", name,
                              colorize( std::to_string( pct ) + '%', col ) );
    };

    std::string mults_desc = _( "Crafting speed multipliers:\n" );
    const int total_pct = static_cast<int>( total_mult * 100 );
    nc_color total_col = total_pct > 100 ? c_green : c_red;
    mults_desc += string_format( " - %s: %s\n", _( "Total" ),
                                 colorize( std::to_string( total_pct ) + '%', total_col ) );
    mults_desc += fmt_spd( act.speed.player_speed, _( "Speed" ) );
    mults_desc += fmt_spd( act.speed.light, _( "Light" ) );
    mults_desc += fmt_spd( act.speed.bench_factor, _( "Workbench" ) );
    mults_desc += fmt_spd( act.speed.morale, _( "Morale" ) );
    mults_desc += fmt_spd( act.speed.tools, _( "Tools" ) );
    if( assistants > 0 ) {
        mults_desc += fmt_spd( act.speed.assist, _( "Assistants" ) );
    }

    return act_progress_message::make_full(
               string_format( _( "%s: %s\n\n%s\n\n%s" ),
                              act.get_verb().translated(), rec->result_name(),
                              time_desc, mults_desc ) );
}

void craft_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", activity_actor::progress );
    jsout.member( "recipe", rec ? rec->ident().str() : std::string() );
    jsout.member( "batch_size", batch_size );
    jsout.member( "craft_counter", craft_counter );
    jsout.member( "location", location );
    jsout.member( "item_selections", item_selections );
    jsout.member( "tool_selections", tool_selections );
    jsout.member( "tools_prepaid", tools_prepaid );
    jsout.member( "is_long", is_long );
    jsout.member( "last_turn_nr", last_turn_nr );
    jsout.end_object();
}

std::unique_ptr<activity_actor> craft_activity_actor::deserialize( JsonIn &jsin )
{
    auto actor = std::make_unique<craft_activity_actor>();
    JsonObject data = jsin.get_object();

    data.read( "progress", actor->activity_actor::progress );
    std::string recipe_str;
    data.read( "recipe", recipe_str );
    if( !recipe_str.empty() ) {
        const recipe_id rid( recipe_str );
        if( rid.is_valid() ) {
            actor->rec = &*rid;
            actor->is_valid = true;
        }
    }
    data.read( "batch_size", actor->batch_size );
    data.read( "craft_counter", actor->craft_counter );
    data.read( "location", actor->location );
    data.read( "item_selections", actor->item_selections );
    data.read( "tool_selections", actor->tool_selections );
    data.read( "tools_prepaid", actor->tools_prepaid );
    data.read( "is_long", actor->is_long );
    data.read( "last_turn_nr", actor->last_turn_nr );

    return actor;
}

void construction_activity_actor::calc_all_moves( player_activity &act, Character &who )
{
    // Check if pc was lost for some reason, but actually still exists on map, e.g. save/load
    if( !pc ) {
        map &here = get_map();
        auto local = here.abs_to_bub( target );
        pc = here.partial_con_at( tripoint_bub_ms( local ) );
    }
    //if something goes terribly wrong we don't CTD
    if( !pc ) {
        act.set_to_null();
        return;
    }
    auto reqs = activity_reqs_adapter( *pc->id );
    act.speed.calc_all_moves( who, reqs );
}

void construction_activity_actor::start( player_activity &/*act*/, Character &/*who*/ )
{
    map &here = get_map();
    auto local = here.abs_to_bub( target );
    pc = here.partial_con_at( tripoint_bub_ms( local ) );
    auto &built = *pc->id;

    std::string name;

    if( pc->id == deconstruct || pc->id == deconstruct_simple ||
        built.group == advanced_object_deconstruction ) {
        if( here.has_furn( local ) ) {
            const furn_id furn_type = here.furn( local );
            name = furn_type->name();
        } else if( !here.ter( local )->is_null() ) {
            const ter_id ter_type = here.ter( local );
            name = ter_type->name();
        }
    } else {
        name = built.post_furniture.is_empty()
               ? ""
               : built.post_furniture->name();
        name = built.post_terrain.is_empty()
               ? name
               : built.post_terrain->name();
    }

    int total_time = std::max( 1, built.adjusted_time() );
    int left = pc->counter == 0
               ? total_time
               : total_time - pc->counter / 10'000'000.0 * total_time;

    progress.emplace( name, total_time, left );
}

void construction_activity_actor::do_turn( player_activity &act, Character &who )
{
    // Check if pc was lost for some reason, but actually still exists on map, e.g. save/load
    if( !pc ) {
        map &here = get_map();
        auto local = here.abs_to_bub( target );
        pc = here.partial_con_at( tripoint_bub_ms( local ) );
    }

    // Maybe the player and the NPC are working on the same construction at the same time or toubles during load
    if( !pc ) {
        act.set_to_null();
        add_msg( m_info, _( "%s did not find an unfinished construction at the activity spot." ),
                 who.disp_name() );
        return;
    }

    pc->counter = progress.front().to_counter();

    if( progress.front().complete() ) {
        progress.pop();
        return;
    } else {
        auto &built = *pc->id;
        if( !who.has_trait( trait_DEBUG_HS ) && !who.meets_skill_requirements( built ) ) {
            add_msg( m_info, _( "%s can't work on this construction anymore." ), who.disp_name() );
            act.set_to_null();
            return;
        }
    }
}

void construction_activity_actor::finish( player_activity &act, Character &who )
{
    complete_construction( who, target );
    act.set_to_null();
}

void construction_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.end_object();
}

std::unique_ptr<activity_actor> construction_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<construction_activity_actor> actor( new construction_activity_actor(
                tripoint_abs_ms( tripoint_zero ) ) );
    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "target", actor->target );
    return actor;
}

void assist_activity_actor::start( player_activity &/*act*/, Character &/*who*/ )
{
    progress.dummy();
}

void assist_activity_actor::serialize( JsonOut &jsout ) const
{
    // Activity is not being saved but still provide some valid json if called.
    jsout.write_null();
}

std::unique_ptr<activity_actor> assist_activity_actor::deserialize( JsonIn & )
{
    return std::make_unique<assist_activity_actor>();
}

std::unique_ptr<activity_actor> salvage_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<salvage_activity_actor> actor( new salvage_activity_actor() );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "targets", actor->targets );
    data.read( "pos", actor->pos );
    data.read( "mute_prompts", actor->mute_prompts );

    return actor;
}

// ---- butchery_activity_actor ----

auto butchery_activity_actor::setup_next_target( player_activity &act,
        Character &who ) -> bool
{
    if( this->targets.empty() ) {
        return false;
    }

    safe_reference<item> &target = this->targets.back();
    player &p = static_cast<player &>( who );

    // Check if the corpse still exists
    if( !target || target.is_destroyed() ) {
        p.add_msg_if_player( m_bad, _( "The corpse completely rotted away!" ) );
        this->targets.pop_back();
        return setup_next_target( act, who );
    }

    if( !target->is_corpse() ) {
        this->targets.pop_back();
        return setup_next_target( act, who );
    }

    butchery_setup setup = consider_butchery( *target, p, this->type );

    const auto print_reasons = [&p, &setup]() {
        for( const std::string &prob : setup.problems ) {
            p.add_msg_if_player( m_bad, prob );
        }
        if( setup.problems.empty() ) {
            for( const std::string &info : setup.info ) {
                p.add_msg_if_player( m_info, info );
            }
        }
    };

    if( setup.can_do == butchery_possibility::never ) {
        act.set_to_null();
        print_reasons();
        return false;
    }

    if( setup.can_do == butchery_possibility::not_this ) {
        this->targets.pop_back();
        print_reasons();
        return setup_next_target( act, who );
    }

    if( setup.can_do == butchery_possibility::need_confirmation ) {
        if( p.is_player() ) {
            if( query_yn( _( "Would you dare desecrate the mortal remains of a fellow human being?" ) ) ) {
                switch( rng( 1, 3 ) ) {
                    case 1:
                        p.add_msg_if_player( m_bad, _( "You clench your teeth at the prospect of this gruesome job." ) );
                        break;
                    case 2:
                        p.add_msg_if_player( m_bad, _( "This will haunt you in your dreams." ) );
                        break;
                    case 3:
                        p.add_msg_if_player( m_bad, _( "You try to look away, but this gruesome image will stay on your mind for some time." ) );
                        break;
                }
                g->u.add_morale( MORALE_BUTCHER, -50, 0, 2_days, 3_hours );
            } else {
                p.add_msg_if_player( m_good, _( "It needs a coffin, not a knife." ) );
                this->targets.pop_back();
                return setup_next_target( act, who );
            }
        } else {
            p.add_morale( MORALE_BUTCHER, -50, 0, 2_days, 3_hours );
        }
    }

    print_reasons();
    this->progress.emplace( target->display_name(), setup.move_cost );
    return true;
}

void butchery_activity_actor::start( player_activity &act, Character &who )
{
    if( !setup_next_target( act, who ) ) {
        act.set_to_null();
    }
}

void butchery_activity_actor::do_turn( player_activity &act, Character &who )
{
    // Completion is driven by the framework: when progress drains, finish() pops the task and
    // advances to the next corpse. Popping here too would double-pop the queue (empty-queue error)
    // and skip this turn's stamina drain, so do_turn only handles the per-turn upkeep.
    if( !this->targets.empty() && this->targets.back().is_destroyed() ) {
        who.add_msg_if_player( m_bad, _( "The corpse completely rotted away!" ) );
        act.set_to_null();
        return;
    }
    who.mod_stamina( -20 );
}

void butchery_activity_actor::finish( player_activity &act, Character &who )
{
    player &p = static_cast<player &>( who );
    map &here = get_map();

    if( this->targets.empty() ) {
        act.set_to_null();
        activity_handlers::resume_for_multi_activities( p );
        return;
    }

    safe_reference<item> &target = this->targets.back();

    if( !target || !target->is_corpse() ) {
        p.add_msg_if_player( m_info, _( "There's no corpse to butcher!" ) );
        this->progress.pop();
        this->targets.pop_back();
        if( !this->targets.empty() && setup_next_target( act, p ) ) {
            return;
        }
        act.set_to_null();
        activity_handlers::resume_for_multi_activities( p );
        return;
    }

    item &corpse_item = *target;
    const mtype *corpse = corpse_item.get_mtype();
    const inventory &inv = p.crafting_inventory();
    const field_type_id type_blood = corpse->bloodType();
    const field_type_id type_gib = corpse->gibType();

    if( this->type == QUARTER ) {
        butchery_quarter( &corpse_item, p );
        this->progress.pop();
        this->targets.pop_back();
        if( !this->targets.empty() && setup_next_target( act, p ) ) {
            return;
        }
        act.set_to_null();
        activity_handlers::resume_for_multi_activities( p );
        return;
    }

    int skill_level = p.get_skill_level( skill_survival );
    int factor = inv.max_quality( this->type == DISSECT ? qual_CUT_FINE : qual_BUTCHER );

    if( this->type == DISSECT ) {
        skill_level = p.get_skill_level( skill_firstaid ) / 2;
        skill_level += p.get_skill_level( skill_electronics ) / 2;
        skill_level += inv.max_quality( qual_CUT_FINE );
    }

    const auto roll_butchery = [&]() {
        double skill_shift = 0.0;
        skill_shift += skill_level;
        skill_shift += rng_float( 0, p.get_dex() - 8 ) / 4.0;
        if( factor < 0 ) {
            skill_shift -= rng_float( 0, -factor / 5.0 );
        }
        return static_cast<int>( std::round( skill_shift ) );
    };

    if( this->type == DISMEMBER ) {
        here.add_splatter( type_gib, p.bub_pos(), rng( corpse->size + 2, ( corpse->size + 1 ) * 2 ) );
    }

    // Fatal failure for non-dissect actions
    if( this->type != DISSECT && roll_butchery() <= ( -15 ) && one_in( 2 ) ) {
        switch( rng( 1, 3 ) ) {
            case 1:
                p.add_msg_if_player( m_warning,
                                     _( "You hack up the corpse so unskillfully, that there is nothing left to salvage from this bloody mess." ) );
                break;
            case 2:
                p.add_msg_if_player( m_warning,
                                     _( "You wanted to cut the corpse, but instead you hacked the meat, spilled the guts all over it, and made a bloody mess." ) );
                break;
            case 3:
                p.add_msg_if_player( m_warning,
                                     _( "You made so many mistakes during the process that you doubt even vultures will be interested in what's left of it." ) );
                break;
        }

        target->detach();

        this->progress.pop();
        this->targets.pop_back();

        here.add_splatter( type_gib, p.bub_pos(), rng( corpse->size + 2, ( corpse->size + 1 ) * 2 ) );
        here.add_splatter( type_blood, p.bub_pos(), rng( corpse->size + 2, ( corpse->size + 1 ) * 2 ) );
        for( int i = 1; i <= corpse->size; i++ ) {
            here.add_splatter_trail( type_gib, p.bub_pos(), random_entry( here.points_in_radius( p.bub_pos(),
                                     corpse->size + 1 ) ) );
            here.add_splatter_trail( type_blood, p.bub_pos(),
                                     random_entry( here.points_in_radius( p.bub_pos(),
                                                   corpse->size + 1 ) ) );
        }

        if( !this->targets.empty() && setup_next_target( act, p ) ) {
            return;
        }
        act.set_to_null();
        activity_handlers::resume_for_multi_activities( p );
        return;
    }

    const auto roll_drops = [&]() {
        factor = std::max( factor, -50 );
        return 0.5 * skill_level / 10 + 0.3 * ( factor + 50 ) / 100 + 0.2 * p.dex_cur / 20;
    };

    butchery_drops_harvest( &corpse_item, *corpse, p, roll_butchery, this->type, roll_drops );

    if( this->type == DISSECT ) {
        int roll = roll_butchery() - corpse_item.damage_level( 4 );
        roll = roll < 1 ? 1 : roll;
        std::vector<detached_ptr<item>> cbms = corpse_item.remove_components();
        std::vector<detached_ptr<item>> contents = corpse_item.contents.clear_items();
        for( detached_ptr<item> &it : contents ) {
            cbms.push_back( std::move( it ) );
        }
        extract_or_wreck_cbms( cbms, roll, p );
        int time_to_cut = size_factor_in_time_to_cut( corpse->size ) / 100;
        int level_cap = std::min<int>( MAX_SKILL,
                                       ( static_cast<int>( corpse->size ) + ( cbms.size() * 2 + 1 ) ) );
        int size_mult = corpse->size > creature_size::medium ? ( corpse->size * corpse->size ) : 8;
        int practice_amt = ( size_mult + 1 ) * ( ( time_to_cut / 150 ) + 1 ) *
                           ( cbms.size() * cbms.size() / 2 + 1 );
        p.practice( skill_firstaid, practice_amt, level_cap );
    }

    switch( this->type ) {
        case QUARTER:
            break;
        case BUTCHER:
            p.add_msg_if_player( m_good, _( "You apply few quick cuts to the %s and leave what's left of it for scavengers." ),
                                 corpse_item.tname() );
            target->detach();
            break;
        case BUTCHER_FULL:
            p.add_msg_if_player( m_good, _( "You finish butchering the %s." ), corpse_item.tname() );
            target->detach();
            break;
        case F_DRESS: {
            if( roll_butchery() < 0 ) {
                switch( rng( 1, 3 ) ) {
                    case 1:
                        p.add_msg_if_player( m_warning,
                                             _( "You unskillfully hack up the corpse and chop off some excess body parts.  You're left wondering how you did so poorly." ) );
                        break;
                    case 2:
                        p.add_msg_if_player( m_warning,
                                             _( "Your unskilled hands slip and damage the corpse.  You still hope it's not a total waste though." ) );
                        break;
                    case 3:
                        p.add_msg_if_player( m_warning,
                                             _( "You did something wrong and hacked the corpse badly.  Maybe it's still recoverable." ) );
                        break;
                }
                corpse_item.set_flag( flag_FIELD_DRESS_FAILED );
            } else {
                switch( rng( 1, 3 ) ) {
                    case 1:
                        p.add_msg_if_player( m_good, _( "You field dress the %s." ), corpse->nname() );
                        break;
                    case 2:
                        p.add_msg_if_player( m_good, _( "You slice the corpse's belly and remove intestines and organs, until you're confident that it will not rot from inside." ) );
                        break;
                    case 3:
                        p.add_msg_if_player( m_good, _( "You remove guts and excess parts, preparing the corpse for later use." ) );
                        break;
                }
                corpse_item.set_flag( flag_FIELD_DRESS );
            }
            here.add_splatter( type_gib, p.bub_pos(), rng( corpse->size + 2, ( corpse->size + 1 ) * 2 ) );
            here.add_splatter( type_blood, p.bub_pos(), rng( corpse->size + 2, ( corpse->size + 1 ) * 2 ) );
            for( int i = 1; i <= corpse->size; i++ ) {
                here.add_splatter_trail( type_gib, p.bub_pos(), random_entry( here.points_in_radius( p.bub_pos(),
                                         corpse->size + 1 ) ) );
                here.add_splatter_trail( type_blood, p.bub_pos(),
                                         random_entry( here.points_in_radius( p.bub_pos(),
                                                       corpse->size + 1 ) ) );
            }
            break;
        }
        case BLEED:
            p.add_msg_if_player( m_good, _( "You bleed the %s." ), corpse->nname() );
            corpse_item.set_flag( flag_BLED );
            break;
        case SKIN:
            switch( rng( 1, 4 ) ) {
                case 1:
                    p.add_msg_if_player( m_good, _( "You skin the %s." ), corpse->nname() );
                    break;
                case 2:
                    p.add_msg_if_player( m_good, _( "You carefully remove the hide from the %s" ), corpse->nname() );
                    break;
                case 3:
                    p.add_msg_if_player( m_good, _( "The %s is challenging to skin, but you get a good hide from it." ), corpse->nname() );
                    break;
                case 4:
                    p.add_msg_if_player( m_good, _( "With a few deft slices you take the skin from the %s" ), corpse->nname() );
                    break;
            }
            corpse_item.set_flag( flag_SKINNED );
            break;
        case DISMEMBER:
            switch( rng( 1, 3 ) ) {
                case 1:
                    p.add_msg_if_player( m_good, _( "You hack the %s apart." ), corpse_item.tname() );
                    break;
                case 2:
                    p.add_msg_if_player( m_good, _( "You lop the limbs off the %s." ), corpse_item.tname() );
                    break;
                case 3:
                    p.add_msg_if_player( m_good, _( "You cleave the %s into pieces." ), corpse_item.tname() );
                    break;
            }
            target->detach();
            break;
        case DISSECT:
            p.add_msg_if_player( m_good, _( "You finish dissecting the %s." ), corpse_item.tname() );
            target->detach();
            break;
    }

    this->progress.pop();
    this->targets.pop_back();

    if( !this->targets.empty() && setup_next_target( act, p ) ) {
        return;
    }

    act.set_to_null();
    activity_handlers::resume_for_multi_activities( p );
}

void butchery_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "type", static_cast<int>( type ) );
    jsout.member( "targets", targets );
    jsout.member( "placement", placement );
    jsout.end_object();
}

std::unique_ptr<activity_actor> butchery_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<butchery_activity_actor> actor( new butchery_activity_actor() );

    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    int type_val = 0;
    data.read( "type", type_val );
    actor->type = static_cast<butcher_type>( type_val );
    data.read( "targets", actor->targets );
    data.read( "placement", actor->placement );

    return actor;
}

// ---- repair_item_activity_actor -------------------------------------------

void repair_item_activity_actor::do_turn( player_activity &act, Character &who )
{
    const float vision_mod = character_funcs::fine_detail_vision_mod( who );
    const int effective_moves = who.moves / vision_mod;
    if( effective_moves <= act.moves_left ) {
        act.moves_left -= effective_moves;
        who.moves = 0;
    } else {
        who.moves -= act.moves_left * vision_mod;
        act.moves_left = 0;
    }
}

auto repair_item_activity_actor::get_fake_tool() const -> item *
{
    if( hack_type == hack_type_t::none ) {
        return &null_item_reference();
    }
    const tripoint_bub_ms position = abs_to_bub( hack_position );
    const map &m = get_map();
    item *fake_item = &null_item_reference();

    switch( hack_type ) {
        case hack_type_t::vehicle: {
            const optional_vpart_position pos = m.veh_at( position );
            if( !pos ) {
                debugmsg( "Failed to find vehicle while using it for repair at %s", position.to_string() );
                return fake_item;
            }
            const vehicle &veh = pos->vehicle();
            fake_item = item::spawn_temporary( hack_tool_type_id, calendar::turn, 0 );
            fake_item->charges = veh.fuel_left( itype_battery );
            break;
        }
        case hack_type_t::furniture: {
            if( !m.has_furn( position ) ) {
                debugmsg( "Failed to find furniture while using it for repair at %s", position.to_string() );
                return fake_item;
            }
            const furn_t &furniture = m.furn( position ).obj();
            const std::vector<itype> item_type_list = furniture.crafting_pseudo_item_types();
            for( const itype &item_type : item_type_list ) {
                if( item_type.get_id() == hack_tool_type_id ) {
                    const tripoint_abs_ms abspos = m.bub_to_abs( position );
                    const distribution_grid &grid = get_distribution_grid_tracker().grid_at( abspos );
                    fake_item = item::spawn_temporary( item_type.get_id(), calendar::turn, 0 );
                    fake_item->charges = grid.get_resource( true );
                    break;
                }
            }
            break;
        }
        case hack_type_t::none:
            break;
    }
    fake_item->set_flag( flag_PSEUDO );
    return fake_item;
}

void repair_item_activity_actor::discharge_real_power_source(
    item &tool, int original_charges ) const
{
    if( hack_type == hack_type_t::none ) {
        return;
    }
    const int used_charges = original_charges - tool.charges;
    if( used_charges <= 0 ) {
        return;
    }
    const tripoint_bub_ms position = abs_to_bub( hack_position );
    const map &m = get_map();
    int unfulfilled_demand = 0;

    switch( hack_type ) {
        case hack_type_t::vehicle: {
            optional_vpart_position pos = m.veh_at( position );
            if( !pos ) {
                return;
            }
            vehicle &veh = pos->vehicle();
            unfulfilled_demand = veh.discharge_battery( used_charges );
            break;
        }
        case hack_type_t::furniture: {
            const tripoint_abs_ms abspos = m.bub_to_abs( position );
            distribution_grid &grid = get_distribution_grid_tracker().grid_at( abspos );
            unfulfilled_demand = grid.mod_resource( -used_charges );
            break;
        }
        case hack_type_t::none:
            break;
    }
    if( unfulfilled_demand != 0 ) {
        debugmsg(
            "Fake tool discharged grid/veh more than grid/veh had!  Unfulfilled demand %d kJ",
            unfulfilled_demand );
    }
}

auto repair_item_activity_actor::show_repeat_menu(
    const std::string &title, repeat_type last_selection ) const -> repeat_type
{
    uilist rmenu;
    rmenu.text = title;
    rmenu.addentry( REPEAT_ONCE, true, '1', _( "Repeat once" ) );
    rmenu.addentry( REPEAT_FOREVER, true, '2', _( "Repeat until reinforced" ) );
    rmenu.addentry( REPEAT_FULL, true, '3', _( "Repeat until fully repaired, but don't reinforce" ) );
    rmenu.addentry( REPEAT_EVENT, true, '4', _( "Repeat until success/failure/level up" ) );
    rmenu.addentry( REPEAT_INIT, true, '5', _( "Back to item selection" ) );
    rmenu.selected = last_selection - REPEAT_ONCE;
    rmenu.query();
    if( rmenu.ret >= REPEAT_INIT && rmenu.ret <= REPEAT_EVENT ) {
        return static_cast<repeat_type>( rmenu.ret );
    }
    return REPEAT_CANCEL;
}

void repair_item_activity_actor::finish( player_activity &act, Character &who )
{
    player &p = static_cast<player &>( who );

    repeat = static_cast<repeat_type>( act.get_value( 0, REPEAT_INIT ) );

    item *fake_tool = nullptr;
    item *ploc = nullptr;

    if( hack_type != hack_type_t::none ) {
        fake_tool = get_fake_tool();
    } else if( tool_item ) {
        ploc = &*tool_item;
    }
    const int hack_original_charges = fake_tool ? fake_tool->charges : 0;

    item *main_tool = nullptr;
    if( hack_type != hack_type_t::none ) {
        main_tool = fake_tool;
    }
    if( main_tool == nullptr && ploc ) {
        main_tool = ploc;
    }
    if( main_tool == nullptr ) {
        main_tool = &p.i_at( act.index );
    }
    if( main_tool == nullptr ) {
        debugmsg( "Failed to get main_tool for long repair" );
        act.set_to_null();
        return;
    }

    item *used_tool = main_tool->get_usable_item( iuse_name_string );
    if( used_tool == nullptr ) {
        debugmsg( "Lost tool used for long repair" );
        act.set_to_null();
        return;
    }

    const use_function *use_fun = used_tool->get_use( iuse_name_string );
    const repair_item_actor *actor = dynamic_cast<const repair_item_actor *>
                                     ( use_fun->get_actor_ptr() );
    if( actor == nullptr ) {
        debugmsg( "iuse_actor type descriptor and actual type mismatch" );
        act.set_to_null();
        return;
    }

    if( repeat != REPEAT_INIT && fix_item ) {
        safe_reference<item> &fix_location = fix_item;
        const int old_level = p.get_skill_level( actor->used_skill );
        const repair_item_actor::attempt_hint attempt = actor->repair( p, *used_tool, *fix_location );
        if( attempt != repair_item_actor::AS_CANT ) {
            if( ploc && ploc->where() == item_location_type::map ) {
                used_tool->ammo_consume( used_tool->ammo_required(), ploc->position() );
            } else {
                p.consume_charges( *used_tool, used_tool->ammo_required() );
            }
            if( hack_type != hack_type_t::none ) {
                discharge_real_power_source( *used_tool, hack_original_charges );
            }
        }

        if( !used_tool->units_sufficient( p ) ) {
            p.add_msg_if_player( _( "Your %s ran out of charges" ), used_tool->tname() );
            act.set_to_null();
            return;
        }

        const bool destroyed = attempt == repair_item_actor::AS_DESTROYED;
        const bool cannot_continue_repair = attempt == repair_item_actor::AS_CANT ||
                                            destroyed || !actor->can_repair_target( p, *fix_location, !destroyed );
        if( cannot_continue_repair ) {
            fix_item = safe_reference<item>();
        }

        const bool event_happened = attempt == repair_item_actor::AS_FAILURE ||
                                    attempt == repair_item_actor::AS_SUCCESS ||
                                    old_level != p.get_skill_level( actor->used_skill );
        const bool need_input =
            ( repeat == REPEAT_ONCE ) ||
            ( repeat == REPEAT_EVENT && event_happened ) ||
            ( repeat == REPEAT_FULL && ( cannot_continue_repair || fix_location->damage() <= 0 ) );
        if( need_input ) {
            repeat = REPEAT_INIT;
        }
    }

    if( !actor->can_use_tool( p, *used_tool, true ) ) {
        act.set_to_null();
        return;
    }

    while( !fix_item ) {
        item *item_loc = game_menus::inv::repair( p, actor, main_tool );
        if( item_loc == nullptr ) {
            p.add_msg_if_player( m_info, _( "Never mind." ) );
            act.set_to_null();
            return;
        }
        if( actor->can_repair_target( p, *item_loc, true ) ) {
            fix_item = safe_reference<item>( *item_loc );
            repeat = REPEAT_INIT;
        }
    }

    const item &fix = *fix_item;

    if( repeat == REPEAT_INIT ) {
        const int level = p.get_skill_level( actor->used_skill );
        repair_item_actor::repair_type action_type = actor->default_action( fix, level );
        if( action_type == repair_item_actor::RT_NOTHING ) {
            p.add_msg_if_player( _( "You won't learn anything more by doing that." ) );
        }

        const std::pair<float, float> chance = actor->repair_chance( p, fix, action_type );
        if( chance.first <= 0.0f ) {
            action_type = repair_item_actor::RT_PRACTICE;
        }

        std::string title = string_format( _( "%s %s\n" ),
                                           repair_item_actor::action_description( action_type ),
                                           fix.tname() );
        title += string_format( _( "Charges: <color_light_blue>%s/%s</color> %s (%s per use)\n" ),
                                used_tool->ammo_remaining(), used_tool->ammo_capacity(),
                                item::nname( used_tool->ammo_current() ),
                                used_tool->ammo_required() );
        title += string_format( _( "Skill used: <color_light_blue>%s (%s)</color>\n" ),
                                actor->used_skill->name(), level );
        title += string_format( _( "Success chance: <color_light_blue>%.1f</color>%%\n" ),
                                100.0f * chance.first );
        title += string_format( _( "Damage chance: <color_light_blue>%.1f</color>%%" ),
                                100.0f * chance.second );

        repeat_type chosen;
        do {
            chosen = show_repeat_menu( title, repeat );
            if( chosen == REPEAT_CANCEL ) {
                act.set_to_null();
                return;
            }
            repeat = chosen;
            if( chosen == REPEAT_INIT ) {
                fix_item = safe_reference<item>();
                return;
            }
            if( chosen == REPEAT_FULL && fix.damage() <= 0 ) {
                p.add_msg_if_player( m_info, _( "Your %s is already fully repaired." ), fix.tname() );
                chosen = REPEAT_INIT;
            }
        } while( chosen == REPEAT_INIT );
    }

    act.moves_left = actor->move_cost;
}

void repair_item_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "iuse_name_string", iuse_name_string );
    jsout.member( "repeat", static_cast<int>( repeat ) );
    jsout.member( "tool_item", tool_item );
    jsout.member( "fix_item", fix_item );
    jsout.member( "hack_type", static_cast<int>( hack_type ) );
    jsout.member( "hack_position", hack_position );
    jsout.member( "hack_tool_type_id", hack_tool_type_id );
    jsout.member( "hack_crafter_index", hack_crafter_index );
    jsout.end_object();
}

std::unique_ptr<activity_actor> repair_item_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<repair_item_activity_actor> actor( new repair_item_activity_actor() );
    JsonObject data = jsin.get_object();
    data.read( "iuse_name_string", actor->iuse_name_string );
    int repeat_val = 0;
    data.read( "repeat", repeat_val );
    actor->repeat = static_cast<repeat_type>( repeat_val );
    data.read( "tool_item", actor->tool_item );
    data.read( "fix_item", actor->fix_item );
    int hack_val = -1;
    data.read( "hack_type", hack_val );
    actor->hack_type = static_cast<hack_type_t>( hack_val );
    data.read( "hack_position", actor->hack_position );
    data.read( "hack_tool_type_id", actor->hack_tool_type_id );
    data.read( "hack_crafter_index", actor->hack_crafter_index );
    return actor;
}

// ---- mend_item_activity_actor ---------------------------------------------

void mend_item_activity_actor::finish( player_activity &act, Character &who )
{
    act.set_to_null();
    if( !target_item ) {
        debugmsg( "invalid arguments to ACT_MEND_ITEM" );
        return;
    }
    item *target = &*target_item;

    const auto f = target->faults.find( fault_id( fault_id_str ) );
    if( f == target->faults.end() ) {
        debugmsg( "item %s does not have fault %s", target->tname(), fault_id_str );
        return;
    }

    player &p = static_cast<player &>( who );
    const mending_method *method = fault_id( fault_id_str )->find_mending_method( method_id );
    if( !method ) {
        debugmsg( "invalid mending_method id for ACT_MEND_ITEM." );
        return;
    }

    const inventory &inv = p.crafting_inventory();
    const requirement_data &reqs = method->requirements.obj();
    if( !reqs.can_make_with_inventory( inv, is_crafting_component ) ) {
        add_msg( m_info, _( "You are currently unable to mend the %s." ), target->tname() );
    }
    for( const auto &e : reqs.get_components() ) {
        p.consume_items( e );
    }
    for( const auto &e : reqs.get_tools() ) {
        p.consume_tools( e );
    }
    p.invalidate_crafting_inventory();

    const auto mend = [&]( item * tgt ) {
        tgt->faults.erase( *f );
        if( method->turns_into ) {
            tgt->faults.emplace( *method->turns_into );
        }
        if( method->also_mends ) {
            tgt->faults.erase( *method->also_mends );
        }
        if( fault_id_str == "fault_gun_blackpowder" || fault_id_str == "fault_gun_dirt" ) {
            tgt->set_var( "dirt", 0 );
        }
        add_msg( m_good, method->success_msg.translated(), tgt->tname() );
    };

    mend( target );
    for( const auto &mod : target->gunmods() ) {
        if( !mod->faults.contains( fault_id( fault_id_str ) ) ) {
            continue;
        }
        mend( mod );
    }
}

void mend_item_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "target_item", target_item );
    jsout.member( "fault_id_str", fault_id_str );
    jsout.member( "method_id", method_id );
    jsout.end_object();
}

std::unique_ptr<activity_actor> mend_item_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<mend_item_activity_actor> actor( new mend_item_activity_actor() );
    JsonObject data = jsin.get_object();
    data.read( "target_item", actor->target_item );
    data.read( "fault_id_str", actor->fault_id_str );
    data.read( "method_id", actor->method_id );
    return actor;
}

// ---- toolmod_add_activity_actor -------------------------------------------

void toolmod_add_activity_actor::finish( player_activity &act, Character &who )
{
    act.set_to_null();
    if( !base_tool || !mod_item ) {
        debugmsg( "Incompatible arguments to ACT_TOOLMOD_ADD" );
        return;
    }
    item &tool = *base_tool;
    item &mod = *mod_item;
    who.add_msg_if_player( m_good, _( "You successfully attached the %1$s to your %2$s." ),
                           mod.tname(), tool.tname() );
    mod.set_flag( flag_IRREMOVABLE );
    tool.put_in( mod.detach() );
}

void toolmod_add_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "base_tool", base_tool );
    jsout.member( "mod_item", mod_item );
    jsout.end_object();
}

std::unique_ptr<activity_actor> toolmod_add_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<toolmod_add_activity_actor> actor( new toolmod_add_activity_actor() );
    JsonObject data = jsin.get_object();
    data.read( "base_tool", actor->base_tool );
    data.read( "mod_item", actor->mod_item );
    return actor;
}

// ---- gunmod_add_activity_actor --------------------------------------------

void gunmod_add_activity_actor::finish( player_activity &act, Character &who )
{
    act.set_to_null();

    item &gun = *gun_item;
    item &mod = *mod_item;

    if( !gun.is_gunmod_compatible( mod ).success() ) {
        debugmsg( "Invalid arguments in ACT_GUNMOD_ADD" );
        return;
    }

    if( !tool_id.is_empty() && qty > 0 ) {
        who.use_charges( tool_id, qty );
    }

    if( rng( 0, 100 ) <= roll ) {
        add_msg( m_good, _( "You successfully attached the %1$s to your %2$s." ), mod.tname(),
                 gun.tname() );
        gun.put_in( mod.detach() );
    } else if( rng( 0, 100 ) <= risk ) {
        if( gun.inc_damage() ) {
            for( item *gunmod : gun.gunmods() ) {
                if( gunmod->is_irremovable() ) {
                    who.remove_item( *gunmod );
                }
            }
            add_msg( m_bad, _( "You failed at installing the %s and destroyed your %s!" ), mod.tname(),
                     gun.tname() );
            gun.detach();
        } else {
            add_msg( m_bad, _( "You failed at installing the %s and damaged your %s!" ), mod.tname(),
                     gun.tname() );
        }
    } else {
        add_msg( m_info, _( "You failed at installing the %s." ), mod.tname() );
    }
}

void gunmod_add_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "gun_item", gun_item );
    jsout.member( "mod_item", mod_item );
    jsout.member( "roll", roll );
    jsout.member( "risk", risk );
    jsout.member( "tool_id", tool_id );
    jsout.member( "qty", qty );
    jsout.end_object();
}

std::unique_ptr<activity_actor> gunmod_add_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<gunmod_add_activity_actor> actor( new gunmod_add_activity_actor() );
    JsonObject data = jsin.get_object();
    data.read( "gun_item", actor->gun_item );
    data.read( "mod_item", actor->mod_item );
    data.read( "roll", actor->roll );
    data.read( "risk", actor->risk );
    data.read( "tool_id", actor->tool_id );
    data.read( "qty", actor->qty );
    return actor;
}

// ---- reload_activity_actor ------------------------------------------------

void reload_activity_actor::finish( player_activity &act, Character &who )
{
    act.set_to_null();
    if( !target_item || !ammo_item || qty <= 0 ) {
        debugmsg( "invalid arguments to ACT_RELOAD" );
        return;
    }
    item &reloadable = *target_item;
    item &ammo = *ammo_item;
    std::string ammo_name = ammo.tname();

    if( !reloadable.reload( who, ammo, qty ) ) {
        add_msg( m_info, _( "Can't reload the %s." ), reloadable.tname() );
        return;
    }

    std::string msg = _( "You reload the %s." );

    if( reloadable.get_var( "dirt", 0 ) > 7800 ) {
        msg = _( "You manage to loosen some debris and make your %s somewhat operational." );
        reloadable.set_var( "dirt", ( reloadable.get_var( "dirt", 0 ) - rng( 790, 2750 ) ) );
    }

    player &p = static_cast<player &>( who );
    if( reloadable.is_gun() ) {
        p.recoil = MAX_RECOIL;
        if( reloadable.has_flag( flag_RELOAD_ONE ) && !ammo.has_flag( flag_SPEEDLOADER ) ) {
            for( int i = 0; i != qty; ++i ) {
                msg = _( "You insert one %2$s into the %1$s." );
            }
        }
        if( reloadable.type->gun->reload_noise_volume > 0 ) {
            sfx::play_variant_sound( "reload", reloadable.typeId().str(),
                                     sfx::get_heard_volume( p.bub_pos() ) );
            sounds::ambient_sound( p.bub_pos(), reloadable.type->gun->reload_noise_volume,
                                   sounds::sound_t::activity, reloadable.type->gun->reload_noise );
        }
    } else if( reloadable.is_container() ) {
        msg = _( "You refill the %s." );
    }
    add_msg( m_neutral, msg, reloadable.tname(), ammo_name );
}

void reload_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "target_item", target_item );
    jsout.member( "ammo_item", ammo_item );
    jsout.member( "qty", qty );
    jsout.end_object();
}

std::unique_ptr<activity_actor> reload_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<reload_activity_actor> actor( new reload_activity_actor() );
    JsonObject data = jsin.get_object();
    data.read( "target_item", actor->target_item );
    data.read( "ammo_item", actor->ammo_item );
    data.read( "qty", actor->qty );
    return actor;
}

// ---- wear_activity_actor --------------------------------------------------

void wear_activity_actor::do_turn( player_activity &act, Character &who )
{
    player &p = static_cast<player &>( who );
    while( p.moves > 0 && !items.empty() && !quantities.empty() ) {
        safe_reference<item> target = std::move( items.back() );
        int quantity = quantities.back();
        items.pop_back();
        quantities.pop_back();

        if( !target ) {
            debugmsg( "Lost target item of ACT_WEAR" );
            continue;
        }
        ret_val<bool> ret = p.can_wear( *target );
        if( ret.success() && ret.value() ) {
            detached_ptr<item> newit = target->split( quantity );
            p.wear_item( std::move( newit ) );
        }
    }

    if( items.empty() ) {
        p.cancel_activity();
    }
}

void wear_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "items", items );
    jsout.member( "quantities", quantities );
    jsout.end_object();
}

std::unique_ptr<activity_actor> wear_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<wear_activity_actor> actor( new wear_activity_actor() );
    JsonObject data = jsin.get_object();
    data.read( "items", actor->items );
    data.read( "quantities", actor->quantities );
    return actor;
}

// ---- armor_layers_activity_actor ------------------------------------------

void armor_layers_activity_actor::do_turn( player_activity &act, Character &who )
{
    who.cancel_activity();
    show_armor_layers_ui( who );
}

void armor_layers_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.end_object();
}

std::unique_ptr<activity_actor> armor_layers_activity_actor::deserialize( JsonIn &jsin )
{
    return std::unique_ptr<armor_layers_activity_actor>( new armor_layers_activity_actor() );
}

// ---- consume_menu_activity_actor ------------------------------------------

activity_id consume_menu_activity_actor::get_type() const
{
    switch( menu_type ) {
        case consume_menu_type::EAT:
            return activity_id( "ACT_EAT_MENU" );
        case consume_menu_type::FOOD:
            return activity_id( "ACT_CONSUME_FOOD_MENU" );
        case consume_menu_type::DRINK:
            return activity_id( "ACT_CONSUME_DRINK_MENU" );
        case consume_menu_type::MEDS:
            return activity_id( "ACT_CONSUME_MEDS_MENU" );
    }
    return activity_id( "ACT_EAT_MENU" );
}

void consume_menu_activity_actor::do_turn( player_activity & /*act*/, Character & /*who*/ )
{
    switch( menu_type ) {
        case consume_menu_type::EAT:
            avatar_action::eat( g->u );
            break;
        case consume_menu_type::FOOD:
            avatar_action::eat( g->u, game_menus::inv::consume_food( g->u ) );
            break;
        case consume_menu_type::DRINK:
            avatar_action::eat( g->u, game_menus::inv::consume_drink( g->u ) );
            break;
        case consume_menu_type::MEDS:
            avatar_action::eat( g->u, game_menus::inv::consume_meds( g->u ) );
            break;
    }
}

void consume_menu_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "menu_type", static_cast<int>( menu_type ) );
    jsout.end_object();
}

std::unique_ptr<activity_actor> consume_menu_activity_actor::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    int menu_type_int = 0;
    data.read( "menu_type", menu_type_int );
    return std::make_unique<consume_menu_activity_actor>( static_cast<consume_menu_type>( menu_type_int ) );
}


// ---- firstaid_activity_actor ----------------------------------------------

void firstaid_activity_actor::finish( player_activity &act, Character &who )
{
    static const std::string iuse_name_string( "heal" );

    item *it = &*healing_item;
    if( it == nullptr ) {
        debugmsg( "Lost healing item for firstaid activity" );
        act.set_to_null();
        return;
    }

    item *used_tool = it->get_usable_item( iuse_name_string );
    if( used_tool == nullptr ) {
        debugmsg( "Lost tool used for healing" );
        act.set_to_null();
        return;
    }

    const use_function *use_fun = used_tool->get_use( iuse_name_string );
    const heal_actor *actor = dynamic_cast<const heal_actor *>( use_fun->get_actor_ptr() );
    if( actor == nullptr ) {
        debugmsg( "iuse_actor type descriptor and actual type mismatch" );
        act.set_to_null();
        return;
    }

    player &patient = static_cast<player &>( who );
    const bodypart_str_id healed = bodypart_str_id( body_part );
    const int charges_consumed = actor->finish_using( patient, patient, *used_tool, healed );
    patient.consume_charges( *it, charges_consumed );

    act.set_to_null();
}

void firstaid_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "healing_item", healing_item.serialize() );
    jsout.member( "body_part", body_part );
    jsout.member( "moves", moves );
    jsout.end_object();
}

std::unique_ptr<activity_actor> firstaid_activity_actor::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    safe_reference<item> healing_item_ref;
    std::string body_part_str;
    int moves_val = 0;
    data.read( "healing_item", healing_item_ref );
    data.read( "body_part", body_part_str );
    data.read( "moves", moves_val );
    return std::make_unique<firstaid_activity_actor>( healing_item_ref, body_part_str, moves_val );
}


// ---- wood_chop_activity_actor ---------------------------------------------

activity_id wood_chop_activity_actor::get_type() const
{
    switch( chop_type ) {
        case wood_chop_type::TREE:
            return activity_id( "ACT_CHOP_TREE" );
        case wood_chop_type::LOGS:
            return activity_id( "ACT_CHOP_LOGS" );
        case wood_chop_type::PLANKS:
            return activity_id( "ACT_CHOP_PLANKS" );
    }
    return activity_id( "ACT_CHOP_TREE" );
}

void wood_chop_activity_actor::do_turn( player_activity &act, Character & /*who*/ )
{
    map &here = get_map();
    sfx::play_activity_sound( "tool", "axe",
                              sfx::get_heard_volume( here.abs_to_bub( act.placement ) ) );
    if( calendar::once_every( 1_minutes ) ) {
        sounds::sound( here.abs_to_bub( act.placement ), 15, sounds::sound_t::activity, _( "CHK!" ) );
    }
}

void wood_chop_activity_actor::finish( player_activity &act, Character &who )
{
    map &here = get_map();
    player &p = static_cast<player &>( who );

    switch( chop_type ) {
        case wood_chop_type::TREE: {
            const auto &pos = here.abs_to_bub( placement );

            tripoint_rel_ms direction;
            if( !p.is_npc() ) {
                if( p.backlog.empty() || p.backlog.front()->id() != ACT_MULTIPLE_CHOP_TREES ) {
                    while( true ) {
                        if( const auto dir = choose_direction(
                                     _( "Select a direction for the tree to fall in." ) ) ) {
                            direction = *dir;
                            break;
                        }
                    }
                }
            } else {
                std::vector<tripoint_rel_ms> valid_directions;

                for( const auto &elem : here.points_in_radius( pos, 1 ) ) {
                    bool cantuse = false;
                    auto direc = elem - pos;
                    auto proposed_to = pos + point_rel_ms( 3 * direction.x(), 3 * direction.y() );
                    std::vector<tripoint_bub_ms> rough_tree_line = line_to( pos, proposed_to );
                    for( const auto &elem : rough_tree_line ) {
                        if( g->critter_at( elem ) ) {
                            cantuse = true;
                            break;
                        }

                        ter_t ter = here.ter( elem ).obj();
                        furn_t furn = here.furn( elem ).obj();
                        if( elem != pos && ( ter.bash.str_max != -1 || ( furn.id && furn.bash.str_max != -1 ) ) ) {
                            cantuse = true;
                            break;
                        }
                        if( veh_pointer_or_null( here.veh_at( elem ) ) ) {
                            cantuse = true;
                            break;
                        }
                    }
                    if( !cantuse ) {
                        valid_directions.push_back( direc );
                    }
                }
                direction = random_entry( valid_directions, direction );
            }

            const auto to = pos + 3 * direction.xy() + point( rng( -1, 1 ), rng( -1, 1 ) );
            std::vector<tripoint_bub_ms> tree = line_to( pos, to, rng( 1, 8 ) );
            for( const auto &elem : tree ) {
                here.batter( elem, 300, 5 );
                here.ter_set( elem, t_trunk );
            }

            here.ter_set( pos, t_stump );
            p.add_msg_if_player( m_good, _( "You finish chopping down a tree." ) );
            here.collapse_at( pos, false, true, false );
            sfx::play_variant_sound( "misc", "timber",
                                     sfx::get_heard_volume( here.abs_to_bub( placement ) ) );

            // Exertion calculation
            if( !axe ) {
                debugmsg( "woodcutting item location not set" );
                activity_handlers::resume_for_multi_activities( p );
                act.set_to_null();
                return;
            }

            item *it = &*axe;
            int act_exertion = iuse::chop_moves( p, *it );
            const std::vector<npc *> helpers = character_funcs::get_crafting_helpers( p, 3 );
            act_exertion = act_exertion * ( 10 - helpers.size() ) / 10;

            p.mod_stored_kcal( std::min( -1, -act_exertion / to_moves<int>( 80_seconds ) ) );
            p.mod_thirst( std::max( 1, act_exertion / to_moves<int>( 12_minutes ) ) );
            p.mod_fatigue( std::max( 1, act_exertion / to_moves<int>( 6_minutes ) ) );

            activity_handlers::resume_for_multi_activities( p );
            act.set_to_null();
            break;
        }
        case wood_chop_type::LOGS: {
            const auto &pos = here.abs_to_bub( placement );
            int log_quan;
            int stick_quan;
            int splint_quan;
            if( here.ter( pos ) == t_trunk ) {
                log_quan = rng( 2, 3 );
                stick_quan = rng( 0, 1 );
                splint_quan = 0;
            } else if( here.ter( pos ) == t_stump ) {
                log_quan = rng( 0, 2 );
                stick_quan = 0;
                splint_quan = rng( 5, 15 );
            } else {
                log_quan = 0;
                stick_quan = 0;
                splint_quan = 0;
            }
            for( int i = 0; i != log_quan; ++i ) {
                detached_ptr<item> obj = item::spawn( itype_log, calendar::turn );
                obj->set_var( "activity_var", p.name );
                here.add_item_or_charges( pos, std::move( obj ) );
            }
            for( int i = 0; i != stick_quan; ++i ) {
                detached_ptr<item> obj = item::spawn( itype_stick_long, calendar::turn );
                obj->set_var( "activity_var", p.name );
                here.add_item_or_charges( pos, std::move( obj ) );
            }
            for( int i = 0; i != splint_quan; ++i ) {
                detached_ptr<item> obj = item::spawn( itype_splinter, calendar::turn );
                obj->set_var( "activity_var", p.name );
                here.add_item_or_charges( pos, std::move( obj ) );
            }
            here.ter_set( pos, t_dirt );
            p.add_msg_if_player( m_good, _( "You finish chopping wood." ) );

            if( !axe ) {
                debugmsg( "woodcutting item location lost" );
                activity_handlers::resume_for_multi_activities( p );
                act.set_to_null();
                return;
            }

            item *it = &*axe;
            int act_exertion = iuse::chop_moves( p, *it );
            const std::vector<npc *> helpers = character_funcs::get_crafting_helpers( p, 3 );
            act_exertion = act_exertion * ( 10 - helpers.size() ) / 10;

            p.mod_stored_kcal( std::min( -1, -act_exertion / to_moves<int>( 80_seconds ) ) );
            p.mod_thirst( std::max( 1, act_exertion / to_moves<int>( 12_minutes ) ) );
            p.mod_fatigue( std::max( 1, act_exertion / to_moves<int>( 6_minutes ) ) );

            activity_handlers::resume_for_multi_activities( p );
            act.set_to_null();
            break;
        }
        case wood_chop_type::PLANKS: {
            const int max_planks = 10;
            int planks = normal_roll( 2 + p.get_skill_level( skill_id( "fabrication" ) ), 1 );
            int wasted_planks = max_planks - planks;
            int scraps = rng( wasted_planks, wasted_planks * 3 );
            planks = std::min( planks, max_planks );

            if( planks > 0 ) {
                here.spawn_item( here.abs_to_bub( placement ), itype_2x4, planks, 0, calendar::turn );
                p.add_msg_if_player( m_good, _( "You produce %d planks." ), planks );
            }
            if( scraps > 0 ) {
                here.spawn_item( here.abs_to_bub( placement ), itype_splinter, scraps, 0, calendar::turn );
                p.add_msg_if_player( m_good, _( "You produce %d splinters." ), scraps );
            }
            if( planks < max_planks / 2 ) {
                p.add_msg_if_player( m_bad, _( "You waste a lot of the wood." ) );
            }
            activity_handlers::resume_for_multi_activities( p );
            act.set_to_null();
            break;
        }
    }
}

void wood_chop_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "chop_type", static_cast<int>( chop_type ) );
    jsout.member( "placement", placement );
    jsout.member( "axe", axe.serialize() );
    jsout.member( "moves", moves );
    jsout.end_object();
}

std::unique_ptr<activity_actor> wood_chop_activity_actor::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    int chop_type_int = 0;
    tripoint_abs_ms placement_val;
    safe_reference<item> axe_ref;
    int moves_val = 0;
    data.read( "chop_type", chop_type_int );
    data.read( "placement", placement_val );
    data.read( "axe", axe_ref );
    data.read( "moves", moves_val );
    auto act = std::make_unique<wood_chop_activity_actor>( static_cast<wood_chop_type>( chop_type_int ),
              placement_val, moves_val, axe_ref );
    return act;
}

// Wave 6a: Simple activities

activity_id game_activity_actor::get_type() const
{
    switch( gtype ) {
        case game_type::GAME:
            return activity_id( "ACT_GAME" );
        case game_type::GENERIC_GAME:
            return activity_id( "ACT_GENERIC_GAME" );
    }
    return activity_id( "ACT_GAME" );
}

void game_activity_actor::do_turn( player_activity &act, Character &who )
{
    player &p = dynamic_cast<player &>( who );

    if( gtype == game_type::GENERIC_GAME ) {
        // GENERIC_GAME: simple morale boost
        if( calendar::once_every( 1_minutes ) ) {
            p.add_morale( MORALE_GAME, 2, 60, 2_hours, 30_minutes, true );
        }
        return;
    }

    // ACT_GAME: uses game_item and drains battery
    if( !game_item ) {
        debugmsg( "ACT_GAME with no game item" );
        act.moves_left = 0;
        return;
    }
    item &game_item_ref = *game_item;

    if( calendar::once_every( 1_minutes ) ) {
        int energy = game_item_ref.ammo_required();
        energy -= game_item_ref.ammo_consume( energy, p.bub_pos() );
        if( energy > 0 && game_item_ref.has_flag( flag_USE_UPS ) ) {
            if( p.use_charges_if_avail( itype_UPS, energy ) ) {
                energy = 0;
            }
        }
        if( energy ) {
            act.moves_left = 0;
            add_msg( m_info, _( "The %s runs out of batteries." ), game_item_ref.tname() );
        }
    }
}

void game_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "game_type", static_cast<int>( gtype ) );
    jsout.member( "game_item", game_item.serialize() );
    jsout.end_object();
}

std::unique_ptr<activity_actor> game_activity_actor::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    int gtype_int = 0;
    safe_reference<item> item_ref;
    data.read( "game_type", gtype_int );
    data.read( "game_item", item_ref );
    auto act = std::make_unique<game_activity_actor>( static_cast<game_type>( gtype_int ), item_ref );
    return act;
}

void vibe_activity_actor::do_turn( player_activity &act, Character &who )
{
    player &p = dynamic_cast<player &>( who );

    if( !vibrator ) {
        debugmsg( "ACT_VIBE with no vibrator item" );
        act.moves_left = 0;
        return;
    }
    item &vibrator_item = *vibrator;

    if( p.encumb( body_part_mouth ) >= 30 ) {
        act.moves_left = 0;
        add_msg( m_bad, _( "You have trouble breathing, and stop." ) );
    }

    if( calendar::once_every( 1_minutes ) ) {
        p.mod_fatigue( 1 );
        if( vibrator_item.ammo_remaining() > 0 ) {
            vibrator_item.ammo_consume( 1, p.bub_pos() );
            p.add_morale( MORALE_FEELING_GOOD, 3, 40 );
            if( vibrator_item.ammo_remaining() == 0 ) {
                add_msg( m_info, _( "The %s runs out of batteries." ), vibrator_item.tname() );
            }
        } else {
            p.add_morale( MORALE_FEELING_GOOD, 1, 40 );
        }
    }

    if( p.get_fatigue() >= fatigue_levels::dead_tired ) {
        act.moves_left = 0;
        add_msg( m_info, _( "You're too tired to continue." ) );
    }
}

void vibe_activity_actor::finish( player_activity & /*act*/, Character &who )
{
    player &p = dynamic_cast<player &>( who );
    p.add_msg_if_player( m_good, _( "You feel much better." ) );
    p.add_morale( MORALE_FEELING_GOOD, 10, 40 );
}

void vibe_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "vibrator", vibrator.serialize() );
    jsout.end_object();
}

std::unique_ptr<activity_actor> vibe_activity_actor::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    safe_reference<item> item_ref;
    data.read( "vibrator", item_ref );
    auto act = std::make_unique<vibe_activity_actor>( item_ref );
    return act;
}

activity_id morale_activity_actor::get_type() const
{
    switch( mtype ) {
        case morale_act_type::MEDITATE:
            return activity_id( "ACT_MEDITATE" );
        case morale_act_type::SHAVE:
            return activity_id( "ACT_SHAVE" );
        case morale_act_type::HAIRCUT:
            return activity_id( "ACT_HAIRCUT" );
    }
    return activity_id( "ACT_MEDITATE" );
}

void morale_activity_actor::finish( player_activity & /*act*/, Character &who )
{
    player &p = dynamic_cast<player &>( who );

    switch( mtype ) {
        case morale_act_type::MEDITATE: {
            p.add_msg_if_player( m_good, _( "You pause to engage in spiritual contemplation." ) );
            p.add_morale( MORALE_FEELING_GOOD, 5, 10 );
            break;
        }
        case morale_act_type::SHAVE: {
            p.add_msg_if_player( _( "You open up your kit and shave." ) );
            p.add_morale( MORALE_SHAVE, 8, 8, 240_minutes, 3_minutes );
            break;
        }
        case morale_act_type::HAIRCUT: {
            p.add_msg_if_player( _( "You give your hair a trim." ) );
            p.add_morale( MORALE_HAIRCUT, 3, 3, 480_minutes, 3_minutes );
            break;
        }
    }
}

void morale_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "morale_type", static_cast<int>( mtype ) );
    jsout.end_object();
}

std::unique_ptr<activity_actor> morale_activity_actor::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    int mtype_int = 0;
    data.read( "morale_type", mtype_int );
    auto act = std::make_unique<morale_activity_actor>( static_cast<morale_act_type>( mtype_int ) );
    return act;
}

activity_id wait_activity_actor::get_type() const
{
    switch( wtype ) {
        case wait_type::WAIT:
            return activity_id( "ACT_WAIT" );
        case wait_type::WAIT_WEATHER:
            return activity_id( "ACT_WAIT_WEATHER" );
        case wait_type::WAIT_NPC:
            return activity_id( "ACT_WAIT_NPC" );
    }
    return activity_id( "ACT_WAIT" );
}

void wait_activity_actor::finish( player_activity & /*act*/, Character &who )
{
    player &p = dynamic_cast<player &>( who );

    if( wtype == wait_type::WAIT_NPC ) {
        p.add_msg_if_player( _( "%s finishes with you…" ), npc_name );
    } else {
        p.add_msg_if_player( _( "You finish waiting." ) );
    }
}

void wait_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "wait_type", static_cast<int>( wtype ) );
    jsout.member( "npc_name", npc_name );
    jsout.end_object();
}

std::unique_ptr<activity_actor> wait_activity_actor::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    int wtype_int = 0;
    std::string name;
    data.read( "wait_type", wtype_int );
    data.read( "npc_name", name );
    auto act = std::make_unique<wait_activity_actor>( static_cast<wait_type>( wtype_int ), name );
    return act;
}

void socialize_activity_actor::finish( player_activity & /*act*/, Character &who )
{
    player &p = dynamic_cast<player &>( who );
    p.add_msg_if_player( _( "%s finishes chatting with you." ), npc_name );
}

void socialize_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "npc_name", npc_name );
    jsout.end_object();
}

std::unique_ptr<activity_actor> socialize_activity_actor::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    std::string name;
    data.read( "npc_name", name );
    auto act = std::make_unique<socialize_activity_actor>( name );
    return act;
}

void atm_activity_actor::do_turn( player_activity & /*act*/, Character &who )
{
    player &p = dynamic_cast<player &>( who );
    iexamine::atm( p, p.bub_pos() );
}


std::unique_ptr<activity_actor> atm_activity_actor::deserialize( JsonIn & /*jsin*/ )
{
    return std::make_unique<atm_activity_actor>();
}

void play_with_pet_activity_actor::finish( player_activity & /*act*/, Character &who )
{
    player &p = dynamic_cast<player &>( who );
    p.add_morale( MORALE_PLAY_WITH_PET, rng( 3, 10 ), 10, 5_hours, 25_minutes );
    p.add_msg_if_player( m_good, _( "Playing with your %s has lifted your spirits a bit." ), pet_name );
}

void play_with_pet_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "pet_name", pet_name );
    jsout.end_object();
}

std::unique_ptr<activity_actor> play_with_pet_activity_actor::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    std::string name;
    data.read( "pet_name", name );
    auto act = std::make_unique<play_with_pet_activity_actor>( name );
    return act;
}

void train_pet_activity_actor::finish( player_activity & /*act*/, Character &who )
{
    player &p = dynamic_cast<player &>( who );
    auto mon = pet.lock();
    if( mon && mon->type->pet_training &&
        p.get_skill_level( skill_survival ) < mon->type->pet_training->min_skill ) {
        p.add_msg_if_player( m_bad,
                              _( "You lack the skill to train %s effectively." ),
                              pet_name );
        return;
    }
    if( !mon ) {
        return;
    }
    mon->remove_effect( effect_well_fed );
    mon->remove_effect( effect_ai_waiting );
    if( 4 * p.get_skill_level( skill_survival ) >= rng( 0, 100 ) ) {
        if( mon && mon->type->pet_training ) {
            mon->training_level = std::min( mon->training_level + 1, mon->type->pet_training->max_level );
            for( const auto &lf : mon->type->pet_training->level_flags ) {
                if( lf.level == mon->training_level ) {
                    for( const m_flag f : lf.flags ) {
                        mon->monster_flags.insert( f );
                    }
                }
            }
            p.add_msg_if_player( m_good,
                                  _( "Training your %s has paid off!  They are now at training level %d/%d." ),
                                  pet_name, mon->training_level,
                                  mon->type->pet_training->max_level );
        }
    } else {
        p.add_msg_if_player( m_neutral,
                              _( "Training your %s takes time, it seems they are making a bit of progress at least." ),
                              pet_name );
    }
}

void train_pet_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "pet_name", pet_name );
    jsout.end_object();
}

std::unique_ptr<activity_actor> train_pet_activity_actor::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    std::string name;
    data.read( "pet_name", name );
    auto act = std::make_unique<train_pet_activity_actor>( weak_ptr_fast<monster>(), name );
    return act;
}

void shear_activity_actor::finish( player_activity & /*act*/, Character &who )
{
    player &p = dynamic_cast<player &>( who );
    map &here = get_map();

    if( sheep_positions.empty() ) {
        debugmsg( "shearing activity with no position of monster stored" );
        return;
    }

    if( !shears ) {
        debugmsg( "shearing item location lost" );
        return;
    }

    const auto source_pos = here.abs_to_bub( sheep_positions[0] );
    monster *source_mon = g->critter_at<monster>( source_pos );
    if( source_mon == nullptr ) {
        debugmsg( "could not find source creature for shearing" );
        return;
    }

    for( int i = 0; i != 22; ++i ) {
        detached_ptr<item> wool_staple = item::spawn( itype_wool_staple, calendar::turn );
        here.add_item_or_charges( p.bub_pos(), std::move( wool_staple ) );
    }
    source_mon->add_effect( effect_sheared, calendar::season_length() );

    if( !tie_type.empty() && tie_type == "temp_tie" ) {
        source_mon->remove_effect( effect_tied );
    }
}

void shear_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "sheep_positions", sheep_positions );
    jsout.member( "shears", shears.serialize() );
    jsout.member( "tie_type", tie_type );
    jsout.end_object();
}

std::unique_ptr<activity_actor> shear_activity_actor::deserialize( JsonIn &jsin )
{
    JsonObject data = jsin.get_object();
    std::vector<tripoint_abs_ms> positions;
    safe_reference<item> tool;
    std::string tie;
    data.read( "sheep_positions", positions );
    data.read( "shears", tool );
    data.read( "tie_type", tie );
    auto act = std::make_unique<shear_activity_actor>( positions, tool, tie );
    return act;
}

void find_mount_activity_actor::do_turn( player_activity &act, Character &who )
{
    // NPC-only activity
    if( who.is_player() ) {
        act.set_to_null();
        return;
    }
    npc &guy = dynamic_cast<npc &>( who );
    monster *mon = guy.chosen_mount.lock().get();
    if( !mon ) {
        act.set_to_null();
        guy.revert_after_activity();
        return;
    }
    if( rl_dist( guy.bub_pos(), mon->bub_pos() ) <= 1 ) {
        if( mon->has_effect( effect_ai_waiting ) ) {
            mon->remove_effect( effect_ai_waiting );
        }
        if( guy.can_mount( *mon ) ) {
            act.set_to_null();
            guy.revert_after_activity();
            guy.chosen_mount = weak_ptr_fast<monster>();
            guy.mount_creature( *mon );
        } else {
            act.set_to_null();
            guy.revert_after_activity();
        }
    } else {
        const tripoint_bub_ms dest = guy.chosen_mount.lock()->bub_pos();
        if( !guy.update_path( dest ) ) {
            act.set_to_null();
            guy.revert_after_activity();
            mon->remove_effect( effect_ai_waiting );
        } else {
            mon->add_effect( effect_ai_waiting, 40_turns );
        }
    }
}


std::unique_ptr<activity_actor> find_mount_activity_actor::deserialize( JsonIn & /*jsin*/ )
{
    return std::make_unique<find_mount_activity_actor>();
}

// ---- adv_inventory_activity_actor ----

std::unique_ptr<activity_actor> adv_inventory_activity_actor::deserialize( JsonIn & /*jsin*/ )
{
    return std::make_unique<adv_inventory_activity_actor>();
}

// ---- cracking_activity_actor ----

void cracking_activity_actor::do_turn( player_activity & /*act*/, Character &who )
{
    if( who.is_deaf() && who.get_skill_level( skill_mechanics ) < 5 ) {
        who.add_msg_if_player( m_bad, _( "You can't hear the tumblers anymore, so you stop." ) );
        who.cancel_activity();
    }
}

void cracking_activity_actor::finish( player_activity &act, Character &who )
{
    auto &here = get_map();
    who.add_msg_if_player( m_good, _( "With a satisfying click, the lock on the safe opens!" ) );
    here.furn_set( here.abs_to_bub( placement ), f_safe_c );
    act.set_to_null();
}

void cracking_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "placement", placement );
    jsout.end_object();
}

std::unique_ptr<activity_actor> cracking_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<cracking_activity_actor> actor( new cracking_activity_actor( tripoint_abs_ms::zero() ) );
    JsonObject data = jsin.get_object();
    data.read( "placement", actor->placement );
    return actor;
}

// ---- wait_stamina_activity_actor ----

void wait_stamina_activity_actor::do_turn( player_activity &act, Character &who )
{
    player &p = static_cast<player &>( who );
    int stamina_threshold = p.get_stamina_max();
    if( stamina_threshold > 0 ) {
        // Check if we've reached the threshold
        if( p.get_stamina() >= stamina_threshold ) {
            finish( act, who );
        }
    } else {
        // Waiting for max stamina
        if( p.get_stamina() >= p.get_stamina_max() ) {
            finish( act, who );
        }
    }
}

void wait_stamina_activity_actor::finish( player_activity &act, Character &who )
{
    player &p = static_cast<player &>( who );
    if( stamina_threshold > 0 ) {
        if( p.get_stamina() < stamina_threshold ) {
            debugmsg( "Failed to wait until stamina threshold %d reached, only at %d.",
                      stamina_threshold, p.get_stamina() );
        }
    } else if( p.get_stamina() < p.get_stamina_max() ) {
        p.add_msg_if_player( _( "You are bored of waiting, so you stop." ) );
    } else {
        p.add_msg_if_player( _( "You finish waiting and feel refreshed." ) );
    }
    act.set_to_null();
}

void wait_stamina_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "stamina_threshold", stamina_threshold );
    jsout.end_object();
}

std::unique_ptr<activity_actor> wait_stamina_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<wait_stamina_activity_actor> actor( new wait_stamina_activity_actor( 0 ) );
    JsonObject data = jsin.get_object();
    data.read( "stamina_threshold", actor->stamina_threshold );
    return actor;
}

// ---- read_activity_actor ----

void read_activity_actor::do_turn( player_activity &act, Character &who )
{
    if( who.is_player() ) {
        // For martial art reading, drain stamina
        if( !act.str_values.empty() && act.str_values[0] == "martial_art" && one_in( 3 ) ) {
            if( act.values.empty() ) {
                player &p = static_cast<player &>( who );
                act.values.push_back( p.get_stamina() );
            }
            player &p = static_cast<player &>( who );
            p.set_stamina( act.values[0] - 1 );
            act.values[0] = p.get_stamina();
        }
    } else {
        who.moves = 0;
    }

    if( calendar::once_every( 1_minutes ) ) {
        if( !act.targets.empty() ) {
            safe_reference<item> &loc = act.targets[0];
            if( !loc || !loc->is_book() ) {
                who.add_msg_if_player( m_bad, _( "You lost your book!  You stop reading." ) );
                act.set_to_null();
            }
        }
    }
}

void read_activity_actor::finish( player_activity &act, Character &who )
{
    if( !act || act.targets.empty() || !act.targets.front() ) {
        debugmsg( "Lost target of ACT_READ" );
        return;
    }
    if( who.is_npc() ) {
        npc *guy = dynamic_cast<npc *>( &who );
        if( guy ) {
            guy->finish_read( &*act.targets.front() );
        }
    } else {
        if( avatar *u = dynamic_cast<avatar *>( &who ) ) {
            u->do_read( &*act.targets.front() );
        } else {
            act.set_to_null();
        }
        if( !act ) {
            who.add_msg_if_player( m_info, _( "You finish reading." ) );
        }
    }
}

std::unique_ptr<activity_actor> read_activity_actor::deserialize( JsonIn & /*jsin*/ )
{
    return std::make_unique<read_activity_actor>();
}

// ---- try_sleep_activity_actor ----

void try_sleep_activity_actor::do_turn( player_activity &act, Character &who )
{
    player &p = static_cast<player &>( who );
    if( !p.has_effect( effect_sleep ) ) {
        if( character_funcs::roll_can_sleep( p ) ) {
            act.set_to_null();
            p.fall_asleep();
            p.remove_value( "sleep_query" );
        } else if( one_in( 1000 ) ) {
            p.add_msg_if_player( _( "You toss and turn…" ) );
        }
        if( calendar::once_every( 30_minutes ) ) {
            // Query handled inline - skip for NPCs
            if( !p.is_npc() ) {
                if( p.get_value( "sleep_query" ) == "false" ) {
                    return;
                }
                uilist sleep_query;
                sleep_query.text = _( "You have trouble sleeping, keep trying?" );
                sleep_query.addentry( 1, true, 'S', _( "Stop trying to fall asleep and get up." ) );
                sleep_query.addentry( 2, true, 'c', _( "Continue trying to fall asleep." ) );
                sleep_query.addentry( 3, true, 'C',
                                      _( "Continue trying to fall asleep and don't ask again." ) );
                sleep_query.query();
                switch( sleep_query.ret ) {
                    case UILIST_CANCEL:
                    case 1:
                        act.set_to_null();
                        break;
                    case 3:
                        p.set_value( "sleep_query", "false" );
                        break;
                    case 2:
                    default:
                        break;
                }
            }
        }
    }
}

std::unique_ptr<activity_actor> try_sleep_activity_actor::deserialize( JsonIn & /*jsin*/ )
{
    return std::make_unique<try_sleep_activity_actor>();
}

// ---- travelling_activity_actor ----

void travelling_activity_actor::do_turn( player_activity &act, Character &who )
{
    player &p = static_cast<player &>( who );
    if( !p.omt_path.empty() ) {
        p.omt_path.pop_back();
        if( p.omt_path.empty() ) {
            p.add_msg_if_player( m_info, _( "You have reached your destination." ) );
            act.set_to_null();
            return;
        }
        const tripoint_abs_omt next_omt = p.omt_path.back();
        tripoint_abs_ms waypoint;
        if( p.omt_path.size() == 1 ) {
            waypoint = midpoint( project_bounds<coords::ms>( next_omt ) );
        } else {
            const auto cur_omt_mid = midpoint( project_bounds<coords::ms>( p.abs_omt_pos() ) );
            waypoint = clamp( cur_omt_mid, project_bounds<coords::ms>( next_omt ) );
        }
        map &here = get_map();
        auto centre_sub = here.abs_to_bub( waypoint );
        if( !here.passable( centre_sub ) ) {
            tripoint_range<tripoint_bub_ms> candidates = here.points_in_radius( centre_sub, 2 );
            for( const auto &elem : candidates ) {
                if( here.passable( elem ) ) {
                    centre_sub = elem;
                    break;
                }
            }
        }
        const auto route_to = here.route( p.bub_pos(), centre_sub,
                                          p.get_legacy_pathfinding_settings(),
                                          p.get_legacy_path_avoid() );
        if( !route_to.empty() ) {
            p.set_destination( route_to, std::unique_ptr<player_activity>( new player_activity( std::make_unique<travelling_activity_actor>() ) ) );
        } else {
            p.add_msg_if_player( _( "You cannot reach that destination" ) );
        }
    } else {
        p.add_msg_if_player( m_info, _( "You have reached your destination." ) );
    }
    act.set_to_null();
}

std::unique_ptr<activity_actor> travelling_activity_actor::deserialize( JsonIn & /*jsin*/ )
{
    return std::make_unique<travelling_activity_actor>();
}

// ---- start_fire_activity_actor ----

void start_fire_activity_actor::do_turn( player_activity &act, Character &who )
{
    player &p = static_cast<player &>( who );
    map &here = get_map();
    const auto bub_loc = here.abs_to_bub( placement );
    item &firestarter = *act.get_tools().front();

    if( !here.is_flammable( bub_loc ) || ( firestarter.has_flag( flag_REQUIRES_TINDER ) &&
                                           !here.tinder_at( bub_loc ) ) ) {
        try_fuel_fire( act, p, true );
        if( !here.is_flammable( bub_loc ) ) {
            p.add_msg_if_player( m_info, _( "There's nothing to light there." ) );
            p.cancel_activity();
            return;
        }
    }

    if( firestarter.has_flag( flag_REQUIRES_TINDER ) ) {
        if( !here.tinder_at( bub_loc ) ) {
            p.add_msg_if_player( m_info, _( "This item requires tinder to light." ) );
            p.cancel_activity();
            return;
        }
    }

    const use_function *usef = firestarter.type->get_use( "firestarter" );
    if( usef == nullptr || usef->get_actor_ptr() == nullptr ) {
        p.add_msg_if_player( m_bad, _( "You have lost the item you were using to start the fire." ) );
        p.cancel_activity();
        return;
    }

    p.mod_moves( -p.moves );
    const firestarter_actor *actor = dynamic_cast<const firestarter_actor *>( usef->get_actor_ptr() );
    const float light = actor->light_mod( p.bub_pos() );
    act.moves_left -= light * 100;
    if( light < 0.1 ) {
        p.add_msg_if_player( m_bad, _( "There is not enough sunlight to start a fire now.  You stop trying." ) );
        p.cancel_activity();
    }
}

void start_fire_activity_actor::finish( player_activity &act, Character &who )
{
    player &p = static_cast<player &>( who );
    static const std::string iuse_name_string( "firestarter" );

    item &it = *act.get_tools().front();
    item *used_tool = it.get_usable_item( iuse_name_string );
    if( used_tool == nullptr ) {
        debugmsg( "Lost tool used for starting fire" );
        act.set_to_null();
        return;
    }

    const use_function *use_fun = used_tool->get_use( iuse_name_string );
    const firestarter_actor *actor = dynamic_cast<const firestarter_actor *>( use_fun->get_actor_ptr() );
    if( actor == nullptr ) {
        debugmsg( "iuse_actor type descriptor and actual type mismatch" );
        act.set_to_null();
        return;
    }

    if( it.type->can_have_charges() ) {
        if( it.has_flag( flag_USE_UPS ) ) {
            p.use_charges( itype_UPS, it.type->charges_to_use() );
        }
        p.consume_charges( it, it.type->charges_to_use() );
    }
    p.practice( skill_survival, index, 5 );

    map &here = get_map();
    firestarter_actor::resolve_firestarter_use( p, here.abs_to_bub( placement ) );
    act.set_to_null();
}

void start_fire_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "tool", tool );
    jsout.member( "placement", placement );
    jsout.member( "index", index );
    jsout.end_object();
}

std::unique_ptr<activity_actor> start_fire_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<start_fire_activity_actor> actor( new start_fire_activity_actor(
                safe_reference<item>(), tripoint_abs_ms::zero(), 0 ) );
    JsonObject data = jsin.get_object();
    data.read( "tool", actor->tool );
    data.read( "placement", actor->placement );
    data.read( "index", actor->index );
    return actor;
}

// ---- fish_activity_actor ----

void fish_activity_actor::do_turn( player_activity &act, Character &who )
{
    player &p = static_cast<player &>( who );
    int fishing_mult = iuse::good_fishing_spot( abs_to_bub( placement ) );
    if( fishing_mult == 0 || p.is_blind() ) {
        act.set_to_null();
        p.add_msg_if_player( m_info,
                              _( "You realize fishing here at the moment is pointless, and stop." ) );
        if( !p.backlog.empty() && p.backlog.front()->id() == ACT_MULTIPLE_FISH ) {
            p.backlog.clear();
            p.assign_activity( ACT_TIDY_UP );
            return;
        }
        return;
    }
    item &rod = *act.get_tools().front();
    int fish_chance = 1;
    int survival_mod = p.get_skill_level( skill_survival );
    if( rod.has_flag( flag_FISH_POOR ) ) {
        survival_mod += dice( 1, 8 );
    } else if( rod.has_flag( flag_FISH_GOOD ) ) {
        survival_mod += dice( 3, 6 );
    }
    fish_chance += ( survival_mod * fishing_mult );
    fish_chance = std::min( survival_mod * 20, fish_chance );
    if( x_in_y( fish_chance, 600000 ) ) {
        p.add_msg_if_player( m_good, _( "You feel a tug on your line!" ) );
        weighted_int_list<std::pair<std::string, int>> caught;
        caught.add( { "fish", 1 }, 1 );
        // Inline rod_fish logic
        map &here = get_map();
        const std::pair<std::string, int> *caught_item = caught.pick();
        if( caught_item->first.contains( "fish" ) ) {
            const std::vector<mtype_id> fish_group = MonsterGroupManager::GetMonstersFromGroup(
                        mongroup_id( "GROUP_FISH" ) );
            const mtype_id fish_mon = random_entry_ref( fish_group );
            here.add_item_or_charges(
                p.bub_pos(), item::make_corpse( fish_mon, calendar::turn +
                                                 rng( 0_turns, 3_hours ) ) );
            p.add_msg_if_player( m_good, _( "You caught a %s." ), fish_mon.obj().nname() );
        } else {
            itype_id possible( caught_item->first );
            if( possible.is_valid() ) {
                here.add_item_or_charges( p.bub_pos(), item::spawn( caught_item->first, calendar::turn,
                                          caught_item->second ),
                                          true );
                p.add_msg_if_player( m_good, _( "You reeled in %s." ) );
            }
        }
        for( item *&elem : here.i_at( p.bub_pos() ) ) {
            if( elem->is_corpse() && !elem->has_var( "activity_var" ) ) {
                elem->set_var( "activity_var", p.name );
            }
        }
    }
    if( calendar::once_every( 60_minutes ) ) {
        p.practice( skill_survival, rng( 1, 3 ) );
    }
}

void fish_activity_actor::finish( player_activity &act, Character &who )
{
    player &p = static_cast<player &>( who );
    act.set_to_null();
    p.add_msg_if_player( m_info, _( "You finish fishing" ) );
    if( !p.backlog.empty() && p.backlog.front()->id() == ACT_MULTIPLE_FISH ) {
        p.backlog.clear();
        p.assign_activity( ACT_TIDY_UP );
    }
}

void fish_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "tool", tool );
    jsout.member( "placement", placement );
    jsout.end_object();
}

std::unique_ptr<activity_actor> fish_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<fish_activity_actor> actor( new fish_activity_actor(
                safe_reference<item>(), tripoint_abs_ms::zero() ) );
    JsonObject data = jsin.get_object();
    data.read( "tool", actor->tool );
    data.read( "placement", actor->placement );
    return actor;
}

// ---- milk_activity_actor ----

void milk_activity_actor::finish( player_activity &act, Character &who )
{
    if( coords == tripoint_abs_ms::zero() ) {
        debugmsg( "milking activity with no position of monster stored" );
        return;
    }
    map &here = get_map();
    monster *source_mon = g->critter_at<monster>( here.abs_to_bub( coords ) );
    if( source_mon == nullptr ) {
        debugmsg( "could not find source creature for liquid transfer" );
        return;
    }
    auto milked_item = source_mon->ammo.find( source_mon->type->starting_ammo.begin()->first );
    if( milked_item == source_mon->ammo.end() ) {
        debugmsg( "animal has no milkable ammo type" );
        return;
    }
    if( milked_item->second <= 0 ) {
        debugmsg( "started milking but udders are now empty before milking finishes" );
        return;
    }
    detached_ptr<item> milk = item::spawn( milked_item->first, calendar::turn, milked_item->second );
    liquid_handler::handle_liquid( std::move( milk ) );
    if( !milk ) {
        milked_item->second = 0;
        who.add_msg_if_player( _( "The %s's udders run dry." ), source_mon->get_name() );
    } else {
        milked_item->second = milk->charges;
    }
    if( !str_value.empty() && str_value == "temp_tie" ) {
        source_mon->remove_effect( effect_tied );
    }
    act.set_to_null();
}

void milk_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "coords", coords );
    jsout.member( "str_value", str_value );
    jsout.end_object();
}

std::unique_ptr<activity_actor> milk_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<milk_activity_actor> actor( new milk_activity_actor(
                tripoint_abs_ms::zero(), std::string() ) );
    JsonObject data = jsin.get_object();
    data.read( "coords", actor->coords );
    data.read( "str_value", actor->str_value );
    return actor;
}

// ---- make_zlave_activity_actor ----

void make_zlave_activity_actor::finish( player_activity &act, Character &who )
{
    player &p = static_cast<player &>( who );
    act.set_to_null();
    map_stack items = g->m.i_at( p.bub_pos() );
    item *body = nullptr;

    for( item *&it : items ) {
        if( it->display_name() == str_value ) {
            body = it;
        }
    }

    if( body == nullptr ) {
        p.add_msg_if_player( m_info, _( "There's no corpse to make into a zombie slave!" ) );
        return;
    }

    if( success > 0 ) {
        p.practice( skill_firstaid, rng( 2, 5 ) );
        p.practice( skill_survival, rng( 2, 5 ) );

        p.add_msg_if_player( m_good,
                              _( "You slice muscles and tendons, and remove body parts until you're confident the zombie won't be able to attack you when it reanimates." ) );

        body->set_var( "zlave", "zlave" );
        if( one_in( 10 ) ) {
            body->set_var( "zlave", "mutilated" );
        }
    } else if( success > -20 ) {
        p.practice( skill_firstaid, rng( 3, 6 ) );
        p.practice( skill_survival, rng( 3, 6 ) );

        p.add_msg_if_player( m_warning,
                              _( "You hack into the corpse and chop off some body parts.  You think the zombie won't be able to attack when it reanimates." ) );

        body->set_var( "zlave", "zlave" );
    } else {
        p.add_msg_if_player( m_bad, _( "You failed to properly dismember the corpse." ) );
    }
}

void make_zlave_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "str_value", str_value );
    jsout.member( "success", success );
    jsout.end_object();
}

std::unique_ptr<activity_actor> make_zlave_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<make_zlave_activity_actor> actor( new make_zlave_activity_actor(
                std::string(), 0 ) );
    JsonObject data = jsin.get_object();
    data.read( "str_value", actor->str_value );
    data.read( "success", actor->success );
    return actor;
}

// ---- tree_communion_activity_actor ----

void tree_communion_activity_actor::do_turn( player_activity &act, Character &who )
{
    player &p = static_cast<player &>( who );
    // Initial rooting process
    if( act.values.front() > 0 ) {
        act.values.front() -= 1;
        if( act.values.front() == 0 ) {
            if( p.has_trait( trait_id( trait_SPIRITUAL ) ) ) {
                p.add_msg_if_player( m_good, _( "The ancient tree spirits answer your call." ) );
            } else {
                p.add_msg_if_player( m_good, _( "Your communion with the trees has begun." ) );
            }
        }
        return;
    }
    // Information is received every minute
    if( !calendar::once_every( 1_minutes ) ) {
        return;
    }
    // BFS forest tiles
    std::queue<tripoint_abs_omt> q;
    std::unordered_set<tripoint_abs_omt> seen;
    tripoint_abs_omt loc = p.abs_omt_pos();
    q.push( loc );
    seen.insert( loc );
    const std::function<bool( const oter_id & )> filter = []( const oter_id & ter ) {
        return ter.obj().is_wooded() || ter.obj().get_name() == "field";
    };
    while( !q.empty() ) {
        tripoint_abs_omt tpt = q.front();
        if( get_overmapbuffer( p.get_dimension() ).reveal( tpt, 3, filter ) ) {
            if( p.has_trait( trait_SPIRITUAL ) ) {
                p.add_morale( MORALE_TREE_COMMUNION, 2, 30, 8_hours, 6_hours );
            } else {
                p.add_morale( MORALE_TREE_COMMUNION, 1, 15, 2_hours, 1_hours );
            }
            if( one_in( 128 ) ) {
                p.add_msg_if_player( "%s", SNIPPET.random_from_category( "tree_communion" ).value_or(
                                          translation() ) );
            }
            return;
        }
        for( const tripoint_abs_omt &neighbor : points_in_radius( tpt, 1 ) ) {
            if( seen.contains( neighbor ) ) {
                continue;
            }
            seen.insert( neighbor );
            if( !get_overmapbuffer( p.get_dimension() ).ter( neighbor ).obj().is_wooded() ) {
                continue;
            }
            q.push( neighbor );
        }
        q.pop();
    }
    p.add_msg_if_player( m_info, _( "The trees have shown you what they will." ) );
    act.set_to_null();
}

void tree_communion_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "initial_rooting_countdown", initial_rooting_countdown );
    jsout.end_object();
}

std::unique_ptr<activity_actor> tree_communion_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<tree_communion_activity_actor> actor( new tree_communion_activity_actor( 0 ) );
    JsonObject data = jsin.get_object();
    data.read( "initial_rooting_countdown", actor->initial_rooting_countdown );
    return actor;
}

// ---- train_activity_actor ----

void train_activity_actor::finish( player_activity &act, Character &who )
{
    player &p = static_cast<player &>( who );
    const skill_id sk( name );
    if( sk.is_valid() ) {
        const Skill &skill = sk.obj();
        std::string skill_name = skill.name();
        int old_skill_level = p.get_skill_level( sk );
        p.get_skill_level_object( sk ).train( 100 * ( old_skill_level + 1 ), true );
        int new_skill_level = p.get_skill_level( sk );
        if( old_skill_level != new_skill_level ) {
            p.add_msg_if_player( m_good, _( "You finish training %s to level %d." ),
                                 skill_name, new_skill_level );
            g->events().send<event_type::gains_skill_level>( p.getID(), sk, new_skill_level );
        } else {
            p.add_msg_if_player( m_good, _( "You get some training in %s." ), skill_name );
        }
        act.set_to_null();
        return;
    }

    const matype_id &ma_id = matype_id( name );
    if( ma_id.is_valid() ) {
        const martialart &mastyle = ma_id.obj();
        g->events().send<event_type::learns_martial_art>( p.getID(), ma_id );
        p.martial_arts_data->learn_style( mastyle.id, p.is_avatar() );
    } else {
        // Spell training
        const spell_id &sp_id = spell_id( name );
        if( sp_id.is_valid() ) {
            const bool knows = g->u.magic->knows_spell( sp_id );
            if( knows ) {
                spell &studying = p.magic->get_spell( sp_id );
                const int xp = roll_remainder( studying.exp_modifier( p ) * expert_multiplier );
                studying.gain_exp( xp );
                p.add_msg_if_player( m_good, _( "You learn a little about the spell: %s" ),
                                      sp_id->name );
            } else {
                p.magic->learn_spell( name, p );
                if( p.magic->knows_spell( sp_id ) ) {
                    p.add_msg_if_player( m_good, _( "You learn %s." ), sp_id->name.translated() );
                } else {
                    act.set_to_null();
                    return;
                }
            }
        } else {
            debugmsg( "train_finish without a valid skill or style or spell name" );
        }
    }

    act.set_to_null();
}

void train_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "name", name );
    jsout.member( "expert_multiplier", expert_multiplier );
    jsout.end_object();
}

std::unique_ptr<activity_actor> train_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<train_activity_actor> actor( new train_activity_actor( std::string(), 0 ) );
    JsonObject data = jsin.get_object();
    data.read( "name", actor->name );
    data.read( "expert_multiplier", actor->expert_multiplier );
    return actor;
}

// ---- train_skill_activity_actor ----

void train_skill_activity_actor::do_turn( player_activity &act, Character &who )
{
    player &p = static_cast<player &>( who );

    item *main_tool = nullptr;
    tripoint_bub_ms hack_pos_bub = tripoint_bub_ms{};
    int hack_original_charges = 0;

    if( hack_type == hack_type_t::furniture ) {
        // Actor-set hack (from iexamine.cpp)
        const map &m = get_map();
        hack_pos_bub = m.abs_to_bub( hack_position );
        if( m.has_furn( hack_pos_bub ) ) {
            const furn_t &furniture = m.furn( hack_pos_bub ).obj();
            const std::vector<itype> item_type_list = furniture.crafting_pseudo_item_types();
            for( const itype &item_type : item_type_list ) {
                if( item_type.get_id() == hack_tool_type_id ) {
                    const tripoint_abs_ms abspos = m.bub_to_abs( hack_pos_bub );
                    const distribution_grid &grid = get_distribution_grid_tracker().grid_at( abspos );
                    main_tool = item::spawn_temporary( item_type.get_id(), calendar::turn, 0 );
                    main_tool->charges = grid.get_resource( true );
                    main_tool->set_flag( flag_PSEUDO );
                    hack_original_charges = main_tool->charges;
                    break;
                }
            }
        }
    } else {
        if( !act.get_tools().empty() ) {
            main_tool = &*act.get_tools().front();
        }
    }

    if( main_tool == nullptr ) {
        debugmsg( "train skill tools array and hack values are empty. this would have caused invalid safe reference error" );
        act.moves_left = 0;
        return;
    }
    item &skill_training_item = *main_tool;
    int training_skill_interval = atoi( p.get_value( "training_iuse_skill_interval" ).c_str() );

    if( training_skill_interval <= 0 ) {
        debugmsg( "training_iuse_skill_interval is invalid ( %d )", training_skill_interval );
        act.moves_left = 0;
        return;
    }

    if( calendar::once_every( 1_minutes * training_skill_interval ) ) {
        std::string training_skill = p.get_value( "training_iuse_skill" );
        if( training_skill.empty() ) {
            debugmsg( "training_iuse_skill is empty" );
            act.moves_left = 0;
            return;
        }
        int training_skill_xp = atoi( p.get_value( "training_iuse_skill_xp" ).c_str() );
        int training_skill_max_level = atoi( p.get_value( "training_iuse_skill_xp_max_level" ).c_str() );
        int training_skill_xp_chance = atoi( p.get_value( "training_iuse_skill_xp_chance" ).c_str() );
        int training_skill_fatigue = atoi( p.get_value( "training_iuse_skill_fatigue" ).c_str() );

        p.mod_fatigue( training_skill_fatigue );
        if( skill_training_item.ammo_remaining() > 0 ) {
            skill_training_item.ammo_consume( 1, p.bub_pos() );
            if( hack_type == hack_type_t::furniture ) {
                const int used_charges = hack_original_charges - skill_training_item.charges;
                if( used_charges > 0 ) {
                    const tripoint_abs_ms abspos = get_map().bub_to_abs( hack_pos_bub );
                    distribution_grid &grid = get_distribution_grid_tracker().grid_at( abspos );
                    grid.mod_resource( -used_charges );
                }
            }
        } else if( skill_training_item.ammo_required() > 0 ) {
            act.moves_left = 0;
            add_msg( m_info, _( "The %s runs out of power." ), skill_training_item.tname() );
            return;
        }
        if( p.get_skill_level( skill_id( training_skill ) ) >= training_skill_max_level ) {
            act.moves_left = 0;
            add_msg( m_info, _( "You can no longer learn anything from this." ) );
            return;
        }
        if( rng( 1, 100 ) < training_skill_xp_chance ) {
            p.practice( skill_id( training_skill ), training_skill_xp,
                        training_skill_max_level );
        }
    }

    // needs rest
    if( p.get_fatigue() >= fatigue_levels::dead_tired ) {
        if( hack_type == hack_type_t::furniture ) {
            const int used_charges = hack_original_charges - skill_training_item.charges;
            if( used_charges > 0 ) {
                const tripoint_abs_ms abspos = get_map().bub_to_abs( hack_pos_bub );
                distribution_grid &grid = get_distribution_grid_tracker().grid_at( abspos );
                grid.mod_resource( -used_charges );
            }
        }
        act.moves_left = 0;
        add_msg( m_info, _( "You're too tired to continue." ) );
    }
}

void train_skill_activity_actor::finish( player_activity &act, Character &who )
{
    player &p = static_cast<player &>( who );
    p.add_msg_if_player( m_good, _( "You feel like you've learned a little bit." ) );
    act.set_to_null();
}

void train_skill_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "hack_type", static_cast<int>( hack_type ) );
    jsout.member( "hack_position", hack_position );
    jsout.member( "hack_tool_type_id", hack_tool_type_id );
    jsout.end_object();
}

std::unique_ptr<activity_actor> train_skill_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<train_skill_activity_actor> actor( new train_skill_activity_actor() );
    JsonObject data = jsin.get_object();
    int hack_val = -1;
    data.read( "hack_type", hack_val );
    actor->hack_type = static_cast<train_skill_activity_actor::hack_type_t>( hack_val );
    data.read( "hack_position", actor->hack_position );
    data.read( "hack_tool_type_id", actor->hack_tool_type_id );
    return actor;
}
// ---- mind_splicer_activity_actor ----

void mind_splicer_activity_actor::finish( player_activity &act, Character &who )
{
    Character &p = who;
    act.set_to_null();

    item *data_card_item = data_card.get();
    if( data_card_item == nullptr ) {
        debugmsg( "Incompatible arguments to: mind_splicer_activity_actor::finish" );
        return;
    }
    p.add_msg_if_player( m_info, _( "…you finally find the memory banks." ) );
    p.add_msg_if_player( m_info, _( "The kit makes a copy of the data inside the bionic." ) );
    data_card_item->contents.clear_items();
    data_card_item->put_in( item::spawn( itype_id( "mind_scan_robofac" ) ) );
}

void mind_splicer_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "data_card", data_card );
    jsout.member( "moves", moves );
    jsout.end_object();
}

std::unique_ptr<activity_actor> mind_splicer_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<mind_splicer_activity_actor> actor( new mind_splicer_activity_actor() );
    JsonObject data = jsin.get_object();
    data.read( "data_card", actor->data_card );
    data.read( "moves", actor->moves );
    return actor;
}
// ---- pulp_activity_actor ----

void pulp_activity_actor::do_turn( player_activity &act, Character &who )
{
    player &p = static_cast<player &>( who );
    map &here = get_map();
    const auto &pos = here.abs_to_bub( placement );

    const auto cut_power = std::max( p.primary_weapon().damage_melee( DT_CUT ),
                                     p.primary_weapon().damage_melee( DT_STAB ) / 2 );

    const auto pulp_effort = std::max( 0, p.str_cur + p.primary_weapon().damage_melee( DT_BASH ) );
    auto pulp_power = std::sqrt( pulp_effort * std::max( 0.0f, cut_power + 1.0f ) );
    pulp_power *= 40 + p.get_skill_level( skill_survival ) * 5;

    if( pulp_power <= 0.0f || !std::isfinite( pulp_power ) ) {
        p.add_msg_player_or_npc( m_bad, _( "You are unable to pulp the corpse." ),
                                  _( "<npcname> is unable to pulp the corpse." ) );
        act.moves_left = 0;
        return;
    }

    const auto mess_radius = p.primary_weapon().has_flag( flag_MESSY ) ? 2 : 1;

    int &num_corpses = act.index;
    map_stack corpse_pile = here.i_at( pos );
    for( item *&corpse : corpse_pile ) {
        const mtype *corpse_mtype = corpse->get_mtype();
        if( !corpse->is_corpse() || ( !corpse_mtype->has_flag( MF_REVIVES ) &&
                                      !corpse_mtype->zombify_into ) ||
            ( !str_value.empty() && str_value == "auto_pulp_no_acid" &&
              corpse_mtype->bloodType().obj().has_acid ) ) {
            continue;
        }

        while( corpse->damage() < corpse->max_damage() ) {
            if( x_in_y( pulp_power, corpse->volume() / units::legacy_volume_factor ) ) {
                corpse->inc_damage( DT_BASH );
                if( corpse->damage() == corpse->max_damage() ) {
                    num_corpses++;
                }
            }

            if( x_in_y( pulp_power, corpse->volume() / units::legacy_volume_factor ) ) {
                const int radius = mess_radius + x_in_y( pulp_power, 500 ) + x_in_y( pulp_power, 1000 );
                const tripoint_bub_ms dest( pos + point( rng( -radius, radius ), rng( -radius, radius ) ) );
                const field_type_id type_blood = ( mess_radius > 1 && x_in_y( pulp_power, 10000 ) ) ?
                                                 corpse->get_mtype()->gibType() :
                                                 corpse->get_mtype()->bloodType();
                here.add_splatter_trail( type_blood, pos, dest );
            }

            if( x_in_y( pulp_power, corpse->volume() / units::legacy_volume_factor ) ) {
                here.add_splatter_trail( corpse->get_mtype()->gibType(), pos,
                                         pos + point( rng( -mess_radius, mess_radius ), rng( -mess_radius, mess_radius ) ) );
            }

            act.moves_left -= 1;
            if( act.moves_left <= 0 ) {
                break;
            }
        }
        corpse->set_flag( flag_PULPED );
    }
    if( num_corpses == 0 ) {
        p.add_msg_if_player( m_bad, _( "The corpse moved before you could finish smashing it!" ) );
    } else {
        p.add_msg_player_or_npc( vgettext( "The corpse is thoroughly pulped.",
                                        "The corpses are thoroughly pulped.", num_corpses ),
                              vgettext( "<npcname> finished pulping the corpse.",
                                        "<npcname> finished pulping the corpses.", num_corpses ) );
    }
    act.moves_left = 0;
}

void pulp_activity_actor::finish( player_activity &act, Character &who )
{
    if( who.is_npc() ) {
        npc *guy = dynamic_cast<npc *>( &who );
        if( guy ) {
            guy->revert_after_activity();
        }
    } else {
        act.set_to_null();
    }
}

void pulp_activity_actor::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "placement", placement );
    jsout.member( "num_corpses", num_corpses );
    jsout.member( "str_value", str_value );
    jsout.end_object();
}

std::unique_ptr<activity_actor> pulp_activity_actor::deserialize( JsonIn &jsin )
{
    std::unique_ptr<pulp_activity_actor> actor( new pulp_activity_actor(
                tripoint_abs_ms::zero(), std::string() ) );
    JsonObject data = jsin.get_object();
    data.read( "placement", actor->placement );
    data.read( "num_corpses", actor->num_corpses );
    data.read( "str_value", actor->str_value );
    return actor;
}
namespace activity_actors
{

// Please keep this alphabetically sorted
const std::unordered_map<activity_id, std::unique_ptr<activity_actor>( * )( JsonIn & )>
deserialize_functions = {
    { activity_id( "ACT_AIM" ), &aim_activity_actor::deserialize },
    { activity_id( "ACT_ADV_INVENTORY" ), &adv_inventory_activity_actor::deserialize },
    { activity_id( "ACT_ARMOR_LAYERS" ), &armor_layers_activity_actor::deserialize },
    { activity_id( "ACT_ASSIST" ), &assist_activity_actor::deserialize },
    { activity_id( "ACT_ATM" ), &atm_activity_actor::deserialize },
    { activity_id( "ACT_AUTODRIVE" ), &autodrive_activity_actor::deserialize },
    { activity_id( "ACT_BLEED" ), &butchery_activity_actor::deserialize },
    { activity_id( "ACT_BOLTCUTTING" ), &boltcutting_activity_actor::deserialize },
    { activity_id( "ACT_BUILD" ), &construction_activity_actor::deserialize },
    { activity_id( "ACT_BURROW" ), &burrow_activity_actor::deserialize },
    { activity_id( "ACT_BUTCHER" ), &butchery_activity_actor::deserialize },
    { activity_id( "ACT_BUTCHER_FULL" ), &butchery_activity_actor::deserialize },
    { activity_id( "ACT_CHOP_LOGS" ), &wood_chop_activity_actor::deserialize },
    { activity_id( "ACT_CHOP_PLANKS" ), &wood_chop_activity_actor::deserialize },
    { activity_id( "ACT_CHOP_TREE" ), &wood_chop_activity_actor::deserialize },
    { activity_id( "ACT_CLEAR_RUBBLE" ), &clear_rubble_activity_actor::deserialize },
    { activity_id( "ACT_CHURN" ), &churn_activity_actor::deserialize },
    { activity_id( "ACT_CONSUME_DRINK_MENU" ), &consume_menu_activity_actor::deserialize },
    { activity_id( "ACT_CONSUME_FOOD_MENU" ), &consume_menu_activity_actor::deserialize },
    { activity_id( "ACT_CONSUME_MEDS_MENU" ), &consume_menu_activity_actor::deserialize },
    { activity_id( "ACT_CRAFT" ), &craft_activity_actor::deserialize },
    { activity_id( "ACT_CRACKING" ), &cracking_activity_actor::deserialize },
    { activity_id( "ACT_DIG" ), &dig_activity_actor::deserialize },
    { activity_id( "ACT_DIG_CHANNEL" ), &dig_channel_activity_actor::deserialize },
    { activity_id( "ACT_DISASSEMBLE" ), &disassemble_activity_actor::deserialize },
    { activity_id( "ACT_DISMEMBER" ), &butchery_activity_actor::deserialize },
    { activity_id( "ACT_DISSECT" ), &butchery_activity_actor::deserialize },
    { activity_id( "ACT_DROP" ), &drop_activity_actor::deserialize },
    { activity_id( "ACT_EAT_MENU" ), &consume_menu_activity_actor::deserialize },
    { activity_id( "ACT_FERTILIZE_PLOT" ), &fertilize_plot_activity_actor::deserialize },
    { activity_id( "ACT_FIELD_DRESS" ), &butchery_activity_actor::deserialize },
    { activity_id( "ACT_FILL_LIQUID" ), &fill_liquid_activity_actor::deserialize },
    { activity_id( "ACT_FILL_PIT" ), &fill_pit_activity_actor::deserialize },
    { activity_id( "ACT_FISH" ), &fish_activity_actor::deserialize },
    { activity_id( "ACT_FIRSTAID" ), &firstaid_activity_actor::deserialize },
    { activity_id( "ACT_FIND_MOUNT" ), &find_mount_activity_actor::deserialize },
    { activity_id( "ACT_FORAGE" ), &forage_activity_actor::deserialize },
    { activity_id( "ACT_GAME" ), &game_activity_actor::deserialize },
    { activity_id( "ACT_GENERIC_GAME" ), &game_activity_actor::deserialize },
    { activity_id( "ACT_GUNMOD_ADD" ), &gunmod_add_activity_actor::deserialize },
    { activity_id( "ACT_HACKING" ), &hacking_activity_actor::deserialize },
    { activity_id( "ACT_HACKSAW" ), &hacksaw_activity_actor::deserialize },
    { activity_id( "ACT_HAND_CRANK" ), &hand_crank_activity_actor::deserialize },
    { activity_id( "ACT_HAIRCUT" ), &morale_activity_actor::deserialize },
    { activity_id( "ACT_JACKHAMMER" ), &jackhammer_activity_actor::deserialize },
    { activity_id( "ACT_LOCKPICK" ), &lockpick_activity_actor::deserialize },
    { activity_id( "ACT_LONGSALVAGE" ), &salvage_activity_actor::deserialize },
    { activity_id( "ACT_MAKE_ZLAVE" ), &make_zlave_activity_actor::deserialize },
    { activity_id( "ACT_MEDITATE" ), &morale_activity_actor::deserialize },
    { activity_id( "ACT_MEND_ITEM" ), &mend_item_activity_actor::deserialize },
    { activity_id( "ACT_MIGRATION_CANCEL" ), &migration_cancel_activity_actor::deserialize },
    { activity_id( "ACT_MILK" ), &milk_activity_actor::deserialize },
    { activity_id( "ACT_MIND_SPLICER" ), &mind_splicer_activity_actor::deserialize },
    { activity_id( "ACT_MOVE_ITEMS" ), &move_items_activity_actor::deserialize },
    { activity_id( "ACT_OXYTORCH" ), &oxytorch_activity_actor::deserialize },
    { activity_id( "ACT_PICKAXE" ), &pickaxe_activity_actor::deserialize },
    { activity_id( "ACT_PICKUP" ), &pickup_activity_actor::deserialize },
    { activity_id( "ACT_PLANT_SEED" ), &plant_seed_activity_actor::deserialize },
    { activity_id( "ACT_PLAY_WITH_PET" ), &play_with_pet_activity_actor::deserialize },
    { activity_id( "ACT_PULP" ), &pulp_activity_actor::deserialize },
    { activity_id( "ACT_PRY_NAILS" ), &pry_nails_activity_actor::deserialize },
    { activity_id( "ACT_QUARTER" ), &butchery_activity_actor::deserialize },
    { activity_id( "ACT_READ" ), &read_activity_actor::deserialize },
    { activity_id( "ACT_RELOAD" ), &reload_activity_actor::deserialize },
    { activity_id( "ACT_REPAIR_ITEM" ), &repair_item_activity_actor::deserialize },
    { activity_id( "ACT_SHAVE" ), &morale_activity_actor::deserialize },
    { activity_id( "ACT_SHEAR" ), &shear_activity_actor::deserialize },
    { activity_id( "ACT_SKIN" ), &butchery_activity_actor::deserialize },
    { activity_id( "ACT_SOCIALIZE" ), &socialize_activity_actor::deserialize },
    { activity_id( "ACT_START_FIRE" ), &start_fire_activity_actor::deserialize },
    { activity_id( "ACT_STASH" ), &stash_activity_actor::deserialize },
    { activity_id( "ACT_THROW" ), &throw_activity_actor::deserialize },
    { activity_id( "ACT_TOGGLE_GATE" ), &toggle_gate_activity_actor::deserialize },
    { activity_id( "ACT_TOOLMOD_ADD" ), &toolmod_add_activity_actor::deserialize },
    { activity_id( "ACT_TRAIN" ), &train_activity_actor::deserialize },
    { activity_id( "ACT_TRAIN_SKILL" ), &train_skill_activity_actor::deserialize },
    { activity_id( "ACT_TRAIN_PET" ), &train_pet_activity_actor::deserialize },
    { activity_id( "ACT_TRAVELLING" ), &travelling_activity_actor::deserialize },
    { activity_id( "ACT_TREE_COMMUNION" ), &tree_communion_activity_actor::deserialize },
    { activity_id( "ACT_TRY_SLEEP" ), &try_sleep_activity_actor::deserialize },
    { activity_id( "ACT_VIBE" ), &vibe_activity_actor::deserialize },
    { activity_id( "ACT_WAIT" ), &wait_activity_actor::deserialize },
    { activity_id( "ACT_WAIT_NPC" ), &wait_activity_actor::deserialize },
    { activity_id( "ACT_WAIT_STAMINA" ), &wait_stamina_activity_actor::deserialize },
    { activity_id( "ACT_WAIT_WEATHER" ), &wait_activity_actor::deserialize },
    { activity_id( "ACT_WEAR" ), &wear_activity_actor::deserialize }
};
} // namespace activity_actors

void serialize( const std::unique_ptr<activity_actor> &actor, JsonOut &jsout )
{
    if( !actor ) {
        jsout.write_null();
    } else {
        jsout.start_object();

        jsout.member( "actor_type", actor->get_type() );
        jsout.member( "actor_data", *actor );

        jsout.end_object();
    }
}

void deserialize( std::unique_ptr<activity_actor> &actor, JsonIn &jsin )
{
    if( jsin.test_null() ) {
        actor = nullptr;
    } else {
        JsonObject data = jsin.get_object();
        if( data.has_member( "actor_data" ) ) {
            activity_id actor_type;
            data.read( "actor_type", actor_type );
            auto deserializer = activity_actors::deserialize_functions.find( actor_type );
            if( deserializer != activity_actors::deserialize_functions.end() ) {
                actor = deserializer->second( *data.get_raw( "actor_data" ) );
            } else {
                debugmsg( "Failed to find activity actor deserializer for type \"%s\"", actor_type.c_str() );
                actor = nullptr;
            }
        } else {
            debugmsg( "Failed to load activity actor" );
            actor = nullptr;
        }
    }
}
