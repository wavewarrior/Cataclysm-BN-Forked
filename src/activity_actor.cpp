#include "activity_actor.h"

#include "action_time_scale.h"
#include "activity_actor_definitions.h"
#include "activity_handlers.h" // put_into_vehicle_or_drop and drop_on_map
#include "activity_speed.h"
#include "advanced_inv.h"
#include "armor_layers.h"
#include "avatar.h"
#include "avatar_action.h"
#include "bionics.h"
#include "bodypart.h"
#include "calendar.h"
#include "character.h"
#include "character_functions.h"
#include "character_martial_arts.h"
#include "construction.h"
#include "construction_partial.h"
#include "craft_command.h"
#include "crafting.h"
#include "crafting_quality.h"
#include "debug.h"
#include "distribution_grid.h"
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "fault.h"
#include "field_type.h"
#include "flag.h"
#include "game.h"
#include "game_inventory.h"
#include "gates.h"
#include "handle_liquid.h"
#include "iexamine.h"
#include "int_id.h"
#include "item.h"
#include "item_group.h"
#include "item_hauling.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "json.h"
#include "line.h"
#include "locations.h"
#include "magic.h"
#include "map.h"
#include "map_iterator.h"
#include "map_selector.h"
#include "mapdata.h"
#include "martialarts.h"
#include "messages.h"
#include "mongroup.h"
#include "monster.h"
#include "morale_types.h"
#include "npc.h"
#include "omdata.h"
#include "options.h"
#include "output.h"
#include "overmapbuffer.h"
#include "pickup.h"
#include "player.h"
#include "player_activity.h"
#include "point.h"
#include "ranged.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "requirements.h"
#include "rng.h"
#include "skill.h"
#include "sounds.h"
#include "text_snippets.h"
#include "timed_event.h"
#include "translations.h"
#include "type_id.h"
#include "ui.h"
#include "uistate.h"
#include "veh_interact.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vpart_position.h"

#include <cmath>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include "coop_client.h"
#include "field.h"
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#define dbg(x) DebugLog((x), DC::Game)

#include "item_reload_option.h"
static const construction_str_id deconstruct_simple( "constr_deconstruct_simple" );
static const construction_str_id deconstruct( "constr_deconstruct" );
static const construction_group_str_id advanced_object_deconstruction(
    "advanced_object_"
    "deconstruction" );

static const itype_id itype_bone_human( "bone_human" );
static const itype_id itype_electrohack( "electrohack" );
static const itype_id itype_log( "log" );
static const itype_id itype_splinter( "splinter" );
static const itype_id itype_stick_long( "stick_long" );
static const itype_id itype_UPS( "UPS" );
static const itype_id itype_muscle( "muscle" );
static const itype_id itype_animal( "animal" );
static const itype_id itype_wool_staple( "wool_staple" );
static const efftype_id effect_ai_waiting( "ai_waiting" );
static const efftype_id effect_sleep( "sleep" );
static const efftype_id effect_sheared( "sheared" );
static const efftype_id effect_tied( "tied" );
static const efftype_id effect_well_fed( "well_fed" );
static const efftype_id effect_narcosis( "narcosis" );
static const efftype_id effect_under_op( "under_op" );
static const efftype_id effect_bleed( "bleed" );
static const efftype_id effect_blind( "blind" );

static const bionic_id bio_painkiller( "painkiller" );

static const trait_id trait_NOPAIN( "NOPAIN" );

static const std::string flag_AUTODOC( "AUTODOC" );
static const std::string flag_AUTODOC_COUCH( "AUTODOC_COUCH" );

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

inline void activity_actor::calc_all_moves( player_activity& act, Character& who )
{
    act.speed.calc_all_moves( who );
}

aim_activity_actor::aim_activity_actor(): fake_weapon( new fake_item_location() )
{
    initial_view_offset = get_avatar().view_offset;
}

std::unique_ptr<aim_activity_actor> aim_activity_actor::use_wielded()
{
    return std::make_unique<aim_activity_actor>();
}

std::unique_ptr<aim_activity_actor> aim_activity_actor::use_bionic(
    detached_ptr<item>&& fake_gun, const units::energy& cost_per_shot )
{
    std::unique_ptr<aim_activity_actor> act( new aim_activity_actor() );
    act->bp_cost_per_shot = cost_per_shot;
    act->fake_weapon = std::move( fake_gun );
    return act;
}

std::unique_ptr<aim_activity_actor> aim_activity_actor::use_gear( item* gun )
{
    std::unique_ptr<aim_activity_actor> act( new aim_activity_actor() );
    act->weapon = safe_reference<item>( gun );
    return act;
}

std::unique_ptr<aim_activity_actor> aim_activity_actor::use_mutation(
    detached_ptr<item>&& fake_gun )
{
    std::unique_ptr<aim_activity_actor> act( new aim_activity_actor() );
    act->fake_weapon = std::move( fake_gun );
    return act;
}

void aim_activity_actor::start( player_activity & /*act*/, Character & /*who*/ )
{
    // Time spent on aiming is determined on the go by the player
    // Dummy progress task to indicate ongoing activity
    progress.dummy();
}

void aim_activity_actor::do_turn( player_activity& act, Character& who )
{
    if( !who.is_avatar() ) {
        debugmsg( "ACT_AIM not implemented for NPCs" );
        aborted = true;
        progress.pop();
        return;
    }
    avatar& you = get_avatar();

    item* weapon = get_weapon();
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
        // Prevents '.' key for 'aim for 10 turns' from conflicting with '.' key for 'interrupt
        // activity' in case of high input lag (curses, sdl sometimes...), but allows to interrupt
        // aiming if a bug happens / stars align to cause an endless aiming loop.
        act.interruptable_with_kb = action != "AIM";
    }
}

void aim_activity_actor::finish( player_activity& act, Character& who )
{
    act.set_to_null();
    item* weapon = get_weapon();
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

    item* ammo_loc = reload_loc ? &*reload_loc : nullptr;

    int shots_fired = ranged::fire_gun( who, fin_trajectory.back(), gun.qty, *gun, ammo_loc );

    if( shots_fired > 0 ) {
        // TODO: bionic power cost of firing should be derived from a value of the relevant weapon.
        if( bp_cost_per_shot > 0_J ) { who.mod_power_level( -bp_cost_per_shot * shots_fired ); }
        if( stamina_cost_per_shot > 0 ) { who.mod_stamina( -stamina_cost_per_shot * shots_fired ); }
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
    if( last_target && last_target->is_dead_state() ) { who.last_target.reset(); }
    who.assign_activity(
        std::unique_ptr<player_activity>( new player_activity( std::move( aim_actor ) ) ), false );
}

void aim_activity_actor::canceled( player_activity & /*act*/, Character & /*who*/ ) { restore_view(); }

void aim_activity_actor::serialize( JsonOut& jsout ) const
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

std::unique_ptr<activity_actor> aim_activity_actor::deserialize( JsonIn& jsin )
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
    if( weapon ) { return &*weapon; }
    if( fake_weapon ) {
        // TODO: check if the player lost relevant bionic/mutation
        return &*fake_weapon;
    } else {
        // Check for lost gun (e.g. yanked by zombie technician)
        // TODO: check that this is the same gun that was used to start aiming
        item* weapon = &get_player_character().primary_weapon();
        return weapon->is_null() ? nullptr : weapon;
    }
}

void aim_activity_actor::restore_view()
{
    avatar& player_character = get_avatar();
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
    player& you = get_avatar();
    item* weapon = get_weapon();
    gun_mode gun = weapon->gun_current_mode();

    // Will burn (0.2% max base stamina * the strength required to fire)
    stamina_cost_per_shot =
        gun->get_min_str() * static_cast<int>( 0.002f * get_option<int>( "PLAYER_MAX_STAMINA" ) );
    if( you.get_stamina() < stamina_cost_per_shot ) {
        you.add_msg_if_player( m_bad, _( "You're too tired to draw your %s." ), weapon->tname() );
        return false;
    }

    const auto ammo_location_is_valid = [&]() -> bool {
        if( !you.ammo_location ) { return false; }
    if( !gun->can_reload_with( you.ammo_location->typeId() ) ) { return false; }
        if( square_dist( you.bub_pos(), you.ammo_location->position() ) > 1 ) { return false; }
        return true;
    };
    item_reload_option opt =
        ammo_location_is_valid()
        ? item_reload_option( &you, weapon, weapon, *you.ammo_location )
        : character_funcs::select_ammo( you, *gun );
    if( !opt ) {
        // Menu canceled
        return false;
    }

    reload_loc = opt.ammo;
    loaded_RAS_weapon = true;
    return true;
}

void autodrive_activity_actor::start( player_activity & /* act */, Character& who )
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
            if( !g->u.query_yn(
                    "Warning: Min Autodrive Speed is below recommened values, "
                    "proceed?" ) ) {
                who.cancel_activity();
                return;
            }
        }
        if( player_vehicle->max_autodrive_speed * 0.5 < min_speed ) {
            if( !g->u.query_yn(
                    "Warning: Max Autodrive Speed is below recommened values, "
                    "proceed?" ) ) {
                who.cancel_activity();
                return;
            }
        }
    }
    player_vehicle->is_autodriving = true;
    progress.dummy();
}

void autodrive_activity_actor::do_turn( player_activity & /* act */, Character& who )
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

void autodrive_activity_actor::canceled( player_activity& act, Character& who )
{
    who.add_msg_if_player( m_info, _( "Auto-drive canceled." ) );
    who.omt_path.clear();
    if( player_vehicle ) { player_vehicle->stop_autodriving( false ); }
    act.set_to_null();
}

void autodrive_activity_actor::finish( player_activity& act, Character& who )
{
    who.add_msg_if_player( m_info, _( "You have reached your destination." ) );
    player_vehicle->stop_autodriving( false );
    act.set_to_null();
}

void autodrive_activity_actor::serialize( JsonOut& jsout ) const
{
    // Activity is not being saved but still provide some valid json if called.
    jsout.write_null();
}

std::unique_ptr<activity_actor> autodrive_activity_actor::deserialize( JsonIn & )
{
    return std::make_unique<autodrive_activity_actor>();
}

void dig_activity_actor::start( player_activity & /*act*/, Character & )
{
    map& here = get_map();
    ter_id ter_here = here.ter( location );
    const bool grave = ter_here == t_grave;
    const std::string name = grave ? "grave" : ter_here->name();
    progress.emplace( name, moves_total );
}

void dig_activity_actor::do_turn( player_activity & /*act*/, Character& who )
{
    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
    sfx::play_activity_sound( "tool", "shovel", sfx::get_heard_volume( location, 60 ) );
    if( action_time_scale::once_every_this_tick( 1_minutes ) ) {
        //~ Sound of a shovel digging a pit at work!
        sound_event se;
        se.origin = location;
        se.volume = 60;
        se.category = sounds::sound_t::activity;
        se.description = _( "hsh!" );
        se.id =  "tool";
        se.variant = "shovel";
        se.from_player = who.is_player();
        se.from_npc = !se.from_player;
        se.faction = who.get_faction()->id();
        se.monfaction = who.get_faction()->mon_faction();
        sounds::sound( se );
    }
}

void dig_activity_actor::finish( player_activity& act, Character& who )
{
    map& here = get_map();
    const bool grave = here.ter( location ) == t_grave;

    if( grave ) {
        if( one_in( 10 ) ) {
            static const std::array<mtype_id, 5> monids = {
                {mon_zombie, mon_zombie_fat, mon_zombie_rot, mon_skeleton, mon_zombie_crawler}
            };

            g->place_critter_at( random_entry( monids ), byproducts_location );
            here.furn_set( location, f_coffin_o );
            who.add_msg_if_player( m_warning, _( "Something crawls out of the coffin!" ) );
        } else {
            here.spawn_item( location, itype_bone_human, rng( 5, 15 ) );
            here.furn_set( location, f_coffin_c );
        }
        std::vector<item *> dropped = get_map().place_items(
                                          item_group_id( "allclothes" ), 50, location, location, false, calendar::turn );
        get_map()
        .place_items( item_group_id( "grave" ), 25, location, location, false, calendar::turn );
        get_map().place_items(
            item_group_id( "jewelry_front" ), 20, location, location, false, calendar::turn );
        for( item * const& it : dropped ) {
            if( it->is_armor() ) { it->set_damage( rng( 1, it->max_damage() - 1 ) ); }
        }
        g->events().send<event_type::exhumes_grave>( who.getID() );
    }

    here.ter_set( location, ter_id( result_terrain ) );

    here.spawn_items( byproducts_location,
                      item_group::items_from( item_group_id( byproducts_item_group ), calendar::turn ) );

    const int act_exertion = act.moves_total;

    who.mod_stored_kcal( std::min( -1, -act_exertion / to_moves<int>( 80_seconds ) ) );
    who.mod_thirst( std::max( 1, act_exertion / to_moves<int>( 12_minutes ) ) );
    who.mod_fatigue( std::max( 1, act_exertion / to_moves<int>( 6_minutes ) ) );
    if( grave ) {
        who.add_msg_if_player( m_good, _( "You finish exhuming a grave." ) );
    } else {
        who.add_msg_if_player( m_good, _( "You finish digging the %s." ), here.ter( location )->name() );
    }

    act.set_to_null();
}

void dig_activity_actor::serialize( JsonOut& jsout ) const
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

std::unique_ptr<activity_actor> dig_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<dig_activity_actor> actor(
        new dig_activity_actor( 0, tripoint_bub_ms::zero(), {}, tripoint_bub_ms::zero(), {} ) );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "moves", actor->moves_total );
    data.read( "location", actor->location );
    data.read( "result_terrain", actor->result_terrain );
    data.read( "byproducts_location", actor->byproducts_location );
    data.read( "byproducts_item_group", actor->byproducts_item_group );

    return actor;
}

void dig_channel_activity_actor::start( player_activity & /*act*/, Character & )
{
    map& here = get_map();
    progress.emplace( here.ter( location )->name(), moves_total );
}

void dig_channel_activity_actor::do_turn( player_activity & /*act*/, Character& who )
{
    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
    sfx::play_activity_sound( "tool", "shovel", sfx::get_heard_volume( location, 70 ) );
    if( action_time_scale::once_every_this_tick( 1_minutes ) ) {
        //~ Sound of a shovel digging a pit at work!
        sound_event se;
        se.origin = location;
        se.volume = 70;
        se.category = sounds::sound_t::activity;
        se.description = _( "hsh!" );
        se.id =  "tool";
        se.variant =  "shovel";
        se.from_player = who.is_player();
        se.from_npc = !se.from_player;
        se.faction = who.get_faction()->id();
        se.monfaction = who.get_faction()->mon_faction();
        sounds::sound( se );
    }
}

void dig_channel_activity_actor::finish( player_activity& act, Character& who )
{
    map& here = get_map();
    here.ter_set( location, ter_id( result_terrain ) );

    here.spawn_items( byproducts_location,
                      item_group::items_from( item_group_id( byproducts_item_group ), calendar::turn ) );

    const int act_exertion = act.moves_total;

    who.mod_stored_kcal( std::min( -1, -act_exertion / to_moves<int>( 80_seconds ) ) );
    who.mod_thirst( std::max( 1, act_exertion / to_moves<int>( 12_minutes ) ) );
    who.mod_fatigue( std::max( 1, act_exertion / to_moves<int>( 6_minutes ) ) );
    who.add_msg_if_player( m_good, _( "You finish digging up %s." ), here.ter( location )->name() );

    act.set_to_null();
}

void dig_channel_activity_actor::serialize( JsonOut& jsout ) const
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

std::unique_ptr<activity_actor> dig_channel_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<dig_channel_activity_actor> actor(
        new dig_channel_activity_actor( 0, tripoint_bub_ms::zero(), {}, tripoint_bub_ms::zero(), {} ) );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "moves", actor->moves_total );
    data.read( "location", actor->location );
    data.read( "result_terrain", actor->result_terrain );
    data.read( "byproducts_location", actor->byproducts_location );
    data.read( "byproducts_item_group", actor->byproducts_item_group );

    return actor;
}

bool disassemble_activity_actor::try_start_single( player_activity & /* act */, Character& who )
{
    if( targets.empty() ) { return false; }
    const iuse_location& target = targets.front();
    if( !target.loc ) {
        debugmsg( "Lost target of ACT_DISASSEMBLE" );
        targets.clear();
        return false;
    }
    const item& itm = *target.loc;

    // Have to check here again in case we ran out of tools
    const ret_val<bool> can_do = crafting::can_disassemble( who, itm, who.crafting_inventory() );
    if( !can_do.success() ) {
        who.add_msg_if_player( m_info, "%s", can_do.c_str() );
        return false;
    }
    return true;
}

inline void disassemble_activity_actor::process_target(
    player_activity & /*act*/, iuse_location& target )
{
    const item& itm = *target.loc;
    const recipe& dis = recipe_dictionary::get_uncraft( itm.typeId() );
    int moves_needed = dis.time * target.count;
    progress.emplace( itm.tname( target.count ), moves_needed );
}

inline void disassemble_activity_actor::calc_all_moves( player_activity& act, Character& who )
{
    const auto& target = targets.front().loc;
    auto reqs = activity_reqs_adapter(
                    recipe_dictionary::get_uncraft( target->typeId() ),
                    std::make_pair( target->weight(), target->volume() ) );
    act.speed.calc_all_moves( who, reqs );
}

void disassemble_activity_actor::start( player_activity& act, Character& who )
{
    if( !who.is_avatar() ) {
        debugmsg( "ACT_DISASSEMBLE is not implemented for NPCs" );
        act.set_to_null();
    } else if( !try_start_single( act, who ) ) {
        act.set_to_null();
    }
    for( auto& target : targets ) { process_target( act, target ); }
}

void disassemble_activity_actor::do_turn( player_activity& act, Character& who )
{
    if( progress.front().complete() ) {
        const iuse_location& target = targets.front();
        if( !target.loc ) {
            debugmsg( "Lost target of ACT_DISASSEMBLY" );
        } else {
            const itype_id rem_type = target.loc->typeId();
            crafting::complete_disassemble( who, target, get_map().abs_to_bub( pos ) );
            if( g->coop_client_ ) {
                std::ostringstream ctx;
                JsonOut jd( ctx );
                jd.start_object();
                jd.member( "ax", this->pos.x() );
                jd.member( "ay", this->pos.y() );
                jd.member( "az", this->pos.z() );
                jd.member( "type", rem_type.str() );
                jd.end_object();
                g->coop_client_->queue_action( "ITEM_REMOVE", ctx.str() );
            }
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

void disassemble_activity_actor::finish( player_activity& act, Character& who )
{
    if( try_start_single( act, who ) ) {
        debugmsg(
            "disassemble_activity_actor call finish function while able to start new "
            "disassembly" );
    }
    // Make a copy to avoid use-after-free
    bool recurse = this->recursive;

    act.set_to_null();

    if( recurse ) { crafting::disassemble_all( *who.as_avatar(), recurse ); }
}

void disassemble_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "targets", targets );
    jsout.member( "pos", pos );
    jsout.member( "recursive", recursive );

    jsout.end_object();
}

std::unique_ptr<activity_actor> disassemble_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<disassemble_activity_actor> actor( new disassemble_activity_actor() );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "targets", actor->targets );
    data.read( "pos", actor->pos );
    data.read( "recursive", actor->recursive );

    return actor;
}

drop_activity_actor::drop_activity_actor(
    Character& ch, const drop_locations& items, bool force_ground, const tripoint_rel_ms& relpos )
    : force_ground( force_ground ),
      relpos( relpos )
{
    this->items = pickup::reorder_for_dropping( ch, items );
}

void drop_activity_actor::start( player_activity & /* act */, Character & )
{
    // Dummy progress task to indicate ongoing activity
    progress.dummy();
}

void drop_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "items", items );
    jsout.member( "force_ground", force_ground );
    jsout.member( "relpos", relpos );

    jsout.end_object();
}

std::unique_ptr<activity_actor> drop_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<drop_activity_actor> actor( new drop_activity_actor() );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "items", actor->items );
    data.read( "force_ground", actor->force_ground );
    data.read( "relpos", actor->relpos );

    return actor;
}

enum hack_result { HACK_UNABLE, HACK_FAIL, HACK_NOTHING, HACK_SUCCESS };

enum hack_type { HACK_SAFE, HACK_DOOR, HACK_GAS, HACK_NULL };

static hack_type get_hack_type( tripoint_bub_ms examp )
{
    hack_type type = HACK_NULL;
    const map& here = get_map();
    const furn_t &xfurn_t = *here.furn( examp );
    const ter_t& xter_t = *here.ter( examp );
    if( xter_t.examine == &iexamine::pay_gas || xfurn_t.examine == &iexamine::pay_gas ) {
        type = HACK_GAS;
    } else if( xter_t.examine == &iexamine::cardreader || xfurn_t.examine == &iexamine::cardreader ) {
        type = HACK_DOOR;
    } else if( xter_t.examine == &iexamine::gunsafe_el || xfurn_t.examine == &iexamine::gunsafe_el ) {
        type = HACK_SAFE;
    }
    return type;
}

void hacking_activity_actor::start( player_activity& act, Character & )
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

void hacking_activity_actor::do_turn( player_activity & /*act*/, Character & )
{
    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
}

static int hack_level( const Character& who )
{
    ///\EFFECT_COMPUTER increases success chance of hacking card readers
    // odds go up with int>8, down with int<8
    // 4 int stat is worth 1 computer skill here
    ///\EFFECT_INT increases success chance of hacking card readers
    return who.get_skill_level( skill_computer ) + ( who.int_cur - 8 ) / 4;
}

static hack_result hack_attempt( Character& who, const bool using_bionic )
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
                who.mod_power_level(
                    units::from_kilojoule( -rng( 25, units::to_kilojoule( who.get_power_level() ) ) ) );
            }
        }
        return HACK_FAIL;
    } else if( success < 6 ) {
        return HACK_NOTHING;
    } else {
        return HACK_SUCCESS;
    }
}

hacking_activity_actor::hacking_activity_actor( use_bionic ): using_bionic( true ) {}

void hacking_activity_actor::finish( player_activity& act, Character& who )
{
    tripoint_bub_ms examp = abs_to_bub( act.placement );
    hack_type type = get_hack_type( examp );
    map& here = get_map();
    sound_event se;
    switch( hack_attempt( who, using_bionic ) ) {
        case HACK_UNABLE:
            who.add_msg_if_player( _( "You cannot hack this." ) );
            break;
        case HACK_FAIL:
            // currently all things that can be hacked have equivalent alarm failure states.
            // this may not always be the case with new hackable things.
            g->events().send<event_type::triggers_alarm>( who.getID() );
            se.origin = who.bub_pos();
            se.volume = 120;
            se.category = sounds::sound_t::music;
            se.description = _( "an alarm sound!" );
            se.id = "environment";
            se.variant = "alarm";
            sounds::sound( se );
            if( examp.z() > 0 && !g->timed_events.queued( TIMED_EVENT_WANTED ) ) {
                g->timed_events
                .add( TIMED_EVENT_WANTED, calendar::turn + 30_minutes, 0, who.abs_sm_pos() );
            }
            break;
        case HACK_NOTHING:
            who.add_msg_if_player( _( "You fail the hack, but no alarms are triggered." ) );
            break;
        case HACK_SUCCESS:
            if( type == HACK_GAS ) {
                int tankGasUnits;
                const std::optional<tripoint_bub_ms> pTank_ =
                    iexamine::getNearFilledGasTank( examp, tankGasUnits );
                if( !pTank_ ) { break; }
                const tripoint_bub_ms pTank = *pTank_;
                const std::optional<tripoint_bub_ms> pGasPump =
                    iexamine::getGasPumpByNumber( examp, uistate.ags_pay_gas_selected_pump );
                if( pGasPump && iexamine::toPumpFuel( pTank, *pGasPump, tankGasUnits ) ) {
                    who.add_msg_if_player( _(
                                               "You hack the terminal and route all available fuel to "
                                               "your pump!" ) );
                    se.origin = examp;
                    se.volume = 40;
                    se.category = sounds::sound_t::activity;
                    se.description = _( "Glug Glug Glug Glug Glug Glug Glug Glug Glug" );
                    se.id = "tool";
                    se.variant =  "gaspump";
                    se.from_player = who.is_player();
                    se.from_npc = !se.from_player;
                    se.faction = who.get_faction()->id();
                    se.monfaction = who.get_faction()->mon_faction();
                    sounds::sound( se );
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
                for( const tripoint_bub_ms& tmp : here.points_in_radius( ( examp ), 3 ) ) {
                    if( here.ter( tmp ) == t_door_metal_locked ) { here.ter_set( tmp, t_door_metal_c ); }
                }
            }
            break;
    }
    act.set_to_null();
}

void hacking_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "using_bionic", using_bionic );

    jsout.end_object();
}

std::unique_ptr<activity_actor> hacking_activity_actor::deserialize( JsonIn& jsin )
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

void move_items_activity_actor::do_turn( player_activity& act, Character& who )
{
    const auto dest = relative_destination + who.bub_pos();

    while( who.moves > 0 && !target_items.empty() ) {
        safe_reference<item> target = std::move( target_items.back() );
        const int quantity = quantities.back();
        target_items.pop_back();
        quantities.pop_back();

        if( !target ) {
            // TODO!: might not be appropriate to debugmsg just because something was
            // destroyed/unloaded
            debugmsg( "Lost target item of ACT_MOVE_ITEMS" );
            continue;
        }

        // Check that we can pick it up.
        if( target->made_of( LIQUID ) ) { continue; }

        // This is for hauling across zlevels, remove when going up and down stairs
        // is no longer teleportation
        // Also ignores items owned by other NPCs, unless they'd already attack on sight
        if( target->is_owned_by( who, true ) || target->get_owner()->likes_u() < -10 ) {
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
        if( who.is_hauling() && !has_haulable_items( who.bub_pos() ) ) { who.stop_hauling(); }
    }
}

void move_items_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "target_items", target_items );
    jsout.member( "quantities", quantities );
    jsout.member( "to_vehicle", to_vehicle );
    jsout.member( "relative_destination", relative_destination );

    jsout.end_object();
}

std::unique_ptr<activity_actor> move_items_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<move_items_activity_actor> actor(
        new move_items_activity_actor( {}, {}, false, tripoint_rel_ms::zero() ) );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "target_items", actor->target_items );
    data.read( "quantities", actor->quantities );
    data.read( "to_vehicle", actor->to_vehicle );
    data.read( "relative_destination", actor->relative_destination );

    return actor;
}

void pickup_activity_actor::do_turn( player_activity& act, Character& who )
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
    bool keep_going;

    // C1 (co-op client only) — zero overhead in SP/host paths.
    // Snapshot tile + type + charge info BEFORE do_pickup() removes items.
    // After pickup, refs of taken items become invalid; refs of skipped/interrupted
    // items stay valid — this is the "capture at actual removal" filter.
    if( g->coop_client_ ) {
        struct PickupSnap {
            safe_reference<item> ref;
            itype_id type;
            int charges_before; // only meaningful when is_cbc
            bool is_cbc;        // count_by_charges: ammo, food, etc.
            tripoint_abs_ms tile;
        };
        // Build exact item-pointer → tile map by scanning nearby tiles.
        // Items within radius 2 of starting_pos covers all pickup cases.
        map& here = get_map();
        const tripoint_bub_ms scan_center = starting_pos.value_or( who.bub_pos() );
        std::unordered_map<const item *, tripoint_abs_ms> ptr_to_tile;
        for( const tripoint_bub_ms& p : here.points_in_radius( scan_center, 2 ) ) {
            for( const item * it : here.i_at( p ) ) { ptr_to_tile[it] = here.bub_to_abs( p ); }
        }
        std::vector<PickupSnap> snaps;
        for( const pickup::pick_drop_selection& sel : target_items ) {
            if( !sel.target ) { continue; }
            const item* raw = &*sel.target;
            const auto found = ptr_to_tile.find( raw );
            if( found == ptr_to_tile.end() ) { continue; }
            snaps.push_back(
            {sel.target, raw->typeId(), raw->charges, raw->count_by_charges(), found->second} );
        }

        keep_going = pickup::do_pickup( target_items, autopickup );

        // Build and emit manifest for items that were ACTUALLY removed.
        if( !snaps.empty() ) {
            std::ostringstream oss;
            JsonOut jout( oss );
            jout.start_object();
            jout.member( "items" );
            jout.start_array();
            for( const PickupSnap& snap : snaps ) {
                const bool item_gone = !snap.ref;
                if( snap.is_cbc ) {
                    const int charges_now = item_gone ? 0 : ( &*snap.ref )->charges;
                    const int charges_taken = snap.charges_before - charges_now;
                    if( charges_taken <= 0 ) { continue; }
                    jout.start_object();
                    jout.member( "tx", snap.tile.x() );
                    jout.member( "ty", snap.tile.y() );
                    jout.member( "tz", snap.tile.z() );
                    jout.member( "type", snap.type.str() );
                    jout.member( "charges", charges_taken );
                    jout.member( "qty", 0 );
                    jout.end_object();
                } else {
                    if( !item_gone ) { continue; }
                    jout.start_object();
                    jout.member( "tx", snap.tile.x() );
                    jout.member( "ty", snap.tile.y() );
                    jout.member( "tz", snap.tile.z() );
                    jout.member( "type", snap.type.str() );
                    jout.member( "charges", 0 );
                    jout.member( "qty", 1 );
                    jout.end_object();
                }
            }
            jout.end_array();
            jout.end_object();
            g->coop_client_->queue_action( "PICKUP", oss.str() );
        }
    } else {

        // False indicates that the player canceled pickup when met with some prompt
        keep_going = pickup::do_pickup( target_items, autopickup );

    } // end coop_client_ else branch

    // Check thievey witness
    npc* witness = nullptr;
    if( !act.str_values.empty() && act.str_values[0] == has_thievery_witness ) {
        for( npc& guy : g->all_npcs() ) {
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

        if( who.get_value( "THIEF_MODE_KEEP" ) != "YES" ) { who.set_value( "THIEF_MODE", "THIEF_ASK" ); }

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

void pickup_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "target_items", target_items );
    jsout.member( "starting_pos", starting_pos );

    jsout.end_object();
}

std::unique_ptr<activity_actor> pickup_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<pickup_activity_actor> actor( new pickup_activity_actor( {}, std::nullopt ) );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "target_items", actor->target_items );
    data.read( "starting_pos", actor->starting_pos );

    return actor;
}


void migration_cancel_activity_actor::do_turn( player_activity& act, Character& who )
{
    // Stop the activity
    act.set_to_null();

    // Ensure that neither avatars nor npcs end up in an invalid state
    if( who.is_npc() ) {
        npc& npc_who = dynamic_cast<npc &>( who );
        npc_who.revert_after_activity();
    } else {
        avatar& avatar_who = dynamic_cast<avatar &>( who );
        avatar_who.clear_destination();
        avatar_who.backlog.clear();
    }
}

void migration_cancel_activity_actor::serialize( JsonOut& jsout ) const
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

void toggle_gate_activity_actor::finish( player_activity& act, Character & )
{
    gates::toggle_gate( placement );
    act.set_to_null();
}

void toggle_gate_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "moves", moves_total );
    jsout.member( "placement", placement );

    jsout.end_object();
}

std::unique_ptr<activity_actor> toggle_gate_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<toggle_gate_activity_actor> actor(
        new toggle_gate_activity_actor( 0, tripoint_bub_ms::zero() ) );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "moves", actor->moves_total );
    data.read( "placement", actor->placement );

    return actor;
}


stash_activity_actor::stash_activity_actor(
    Character& ch, const drop_locations& items, const tripoint_rel_ms& relpos )
    : relpos( relpos )
{
    this->items = pickup::reorder_for_dropping( ch, items );
}

void stash_activity_actor::start( player_activity &, Character & )
{
    // Dummy progress task to indicate ongoing activity
    progress.dummy();
}

void stash_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "items", items );
    jsout.member( "relpos", relpos );

    jsout.end_object();
}

std::unique_ptr<activity_actor> stash_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<stash_activity_actor> actor( new stash_activity_actor() );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "items", actor->items );
    data.read( "relpos", actor->relpos );

    return actor;
}

void throw_activity_actor::do_turn( player_activity& act, Character& who )
{
    // Make copies of relevant values since the class would
    // not be available after act.set_to_null()
    if( !target ) {
        debugmsg( "Lost weapon while throwing" );
        act.set_to_null();
        return;
    }

    item* it = &*target;
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
    if( blind_throw_pos ) { who.setpos( *blind_throw_pos ); }

    target_handler::trajectory trajectory =
        target_handler::mode_throw( *who.as_avatar(), *it, blind_throw_pos.has_value() );

    // If we previously shifted our position, put ourselves back now that we've picked our target.
    if( blind_throw_pos ) { who.setpos( original_player_position ); }

    if( trajectory.empty() ) { return; }

    if( it != &who.primary_weapon() ) {
        // This is to represent "implicit offhand wielding"
        int extra_cost = who.item_handling_cost( *it, true, INVENTORY_HANDLING_PENALTY / 2 );
        who.mod_moves( -extra_cost );
    }
    // C2e: capture thrown item type before split so we can identify the landed item after.
    // throw_activity_actor::do_turn is the correct hook — plthrow() only assigns this activity;
    // the actual throw (aim UI + split + throw_item) happens here.
    const itype_id c2e_thrown_type = target->typeId();
    using coop_field_key = std::pair<tripoint_abs_ms, field_type_id>;
    std::set<coop_field_key> coop_fields_before;
    if( g->coop_client_ ) {
        map& fhere = get_map();
        if( !trajectory.empty() ) {
            const auto impact = tripoint_bub_ms( trajectory.back() );
            for( const tripoint_bub_ms& tp : fhere.points_in_radius( impact, 5 ) ) {
                const field& f = fhere.field_at( tp );
                for( const auto& [ft, fe] : f ) {
                    if( fe.get_field_intensity() > 0 ) {
                        coop_fields_before.emplace( fhere.bub_to_abs( tp ), ft );
                    }
                }
            }
        }
    }
    detached_ptr<item> det = target->split( 1 );
    const auto throw_result =
        ranged::throw_item( who, trajectory.back(), std::move( det ), blind_throw_pos );
    if( g->coop_client_ ) {
        // Only relay items that landed on the ground (not consumed/exploded).
        // Discriminator: check if an item of the thrown type is at end_point after the throw.
        // Molotovs/grenades are consumed on impact → nothing at end_point → relay skipped.
        // Inert items (rocks, knives) land as new map items with fresh split pointers.
        // Note: hit_critter != nullptr means item stopped at a creature's tile; skip to
        // avoid relaying embedded weapons (item is held by creature, not on ground).
        if( throw_result.hit_critter == nullptr ) {
            const tripoint_bub_ms end_bub = throw_result.end_point;
            const tripoint_abs_ms end_abs = get_map().bub_to_abs( end_bub );
            for( const item * it : get_map().i_at( end_bub ) ) {
                if( it->typeId() != c2e_thrown_type ) { continue; }
                // Found the landed item — emit DROP so it appears on the host map.
                std::ostringstream drop_oss;
                JsonOut drop_jout( drop_oss );
                drop_jout.start_object();
                drop_jout.member( "items" );
                drop_jout.start_array();
                drop_jout.start_object();
                drop_jout.member( "tx", end_abs.x() );
                drop_jout.member( "ty", end_abs.y() );
                drop_jout.member( "tz", end_abs.z() );
                std::ostringstream item_oss;
                JsonOut jitem( item_oss );
                it->serialize( jitem );
                drop_jout.member( "data", item_oss.str() );
                drop_jout.end_object();
                drop_jout.end_array();
                drop_jout.end_object();
                g->coop_client_->queue_action( "DROP", drop_oss.str() );
                DebugLog( DL::Info, DC::Main )
                        << "[coop] C2e throw relay: " << c2e_thrown_type.str() << " at ("
                        << end_abs.x() << "," << end_abs.y() << "," << end_abs.z() << ")";
                break; // one item per throw
            }
        }
    }
    if( g->coop_client_ && !trajectory.empty() ) {
        map& fhere = get_map();
        const auto impact = tripoint_bub_ms( trajectory.back() );
        std::vector<std::pair<tripoint_abs_ms, std::pair<field_type_id, int>>> new_fields;
        for( const tripoint_bub_ms& tp : fhere.points_in_radius( impact, 5 ) ) {
            const field& f = fhere.field_at( tp );
            for( const auto& [ft, fe] : f ) {
                const auto abs = fhere.bub_to_abs( tp );
                if( fe.get_field_intensity() > 0
                    && coop_fields_before.find( {abs, ft} ) == coop_fields_before.end() ) {
                    new_fields.emplace_back( abs, std::make_pair( ft, fe.get_field_intensity() ) );
                }
            }
        }
        const auto limit = std::min( static_cast<int>( new_fields.size() ), 25 );
        for( int i = 0; i < limit; ++i ) {
            const auto& [abs, finfo] = new_fields[i];
            std::ostringstream fctx;
            JsonOut jf( fctx );
            jf.start_object();
            jf.member( "ax", abs.x() );
            jf.member( "ay", abs.y() );
            jf.member( "az", abs.z() );
            jf.member( "field", finfo.first.id().str() );
            jf.member( "intensity", finfo.second );
            jf.end_object();
            g->coop_client_->queue_action( "FIELD_SET", fctx.str() );
        }
    }
}

void throw_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "target_loc", target );
    jsout.member( "blind_throw_from_pos", blind_throw_from_pos );

    jsout.end_object();
}

std::unique_ptr<activity_actor> throw_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<throw_activity_actor> actor( new throw_activity_actor() );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "target_loc", actor->target );
    data.read( "blind_throw_from_pos", actor->blind_throw_from_pos );

    return actor;
}


// ---- craft_activity_actor ----


// ---- repair_item_activity_actor -------------------------------------------

void repair_item_activity_actor::do_turn( player_activity& /*act*/, Character& who )
{
    const float vision_mod = character_funcs::fine_detail_vision_mod( who );
    const int effective_moves = static_cast<int>(
                                    action_time_scale::activity_progress_from_actor_moves( who ) / vision_mod );
    const int remaining = progress.get_moves_left();
    if( effective_moves <= remaining ) {
        progress.mod_moves_left( -effective_moves );
        who.moves = 0;
    } else {
        who.moves -= action_time_scale::actor_moves_for_activity_progress( who,
                     remaining * vision_mod );
        progress.mod_moves_left( -remaining );
    }
}

auto repair_item_activity_actor::get_fake_tool() const -> item*
{
    if( hack_type == hack_type_t::none ) { return &null_item_reference(); }
    const tripoint_bub_ms position = abs_to_bub( hack_position );
    const map& m = get_map();
    item* fake_item = &null_item_reference();

    switch( hack_type ) {
        case hack_type_t::vehicle: {
            const optional_vpart_position pos = m.veh_at( position );
            if( !pos ) {
                debugmsg( "Failed to find vehicle while using it for repair at %s",
                          position.to_string() );
                return fake_item;
            }
            const vehicle& veh = pos->vehicle();
            fake_item = item::spawn_temporary( hack_tool_type_id, calendar::turn, 0 );
            fake_item->charges = veh.fuel_left( itype_battery );
            break;
        }
        case hack_type_t::furniture: {
            if( !m.has_furn( position ) ) {
                debugmsg( "Failed to find furniture while using it for repair at %s",
                          position.to_string() );
                return fake_item;
            }
            const furn_t &furniture = m.furn( position ).obj();
            const std::vector<itype> item_type_list = furniture.crafting_pseudo_item_types();
            for( const itype& item_type : item_type_list ) {
                if( item_type.get_id() == hack_tool_type_id ) {
                    const tripoint_abs_ms abspos = m.bub_to_abs( position );
                    const distribution_grid& grid = get_distribution_grid_tracker().grid_at( abspos );
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
    item& tool, int original_charges ) const
{
    if( hack_type == hack_type_t::none ) { return; }
    const int used_charges = original_charges - tool.charges;
    if( used_charges <= 0 ) { return; }
    const tripoint_bub_ms position = abs_to_bub( hack_position );
    const map& m = get_map();
    int unfulfilled_demand = 0;

    switch( hack_type ) {
        case hack_type_t::vehicle: {
            optional_vpart_position pos = m.veh_at( position );
            if( !pos ) { return; }
            vehicle& veh = pos->vehicle();
            unfulfilled_demand = veh.discharge_battery( used_charges );
            break;
        }
        case hack_type_t::furniture: {
            const tripoint_abs_ms abspos = m.bub_to_abs( position );
            distribution_grid& grid = get_distribution_grid_tracker().grid_at( abspos );
            unfulfilled_demand = grid.mod_resource( -used_charges );
            break;
        }
        case hack_type_t::none:
            break;
    }
    if( unfulfilled_demand != 0 ) {
        debugmsg( "Fake tool discharged grid/veh more than grid/veh had!  Unfulfilled demand %d kJ",
                  unfulfilled_demand );
    }
}

auto repair_item_activity_actor::show_repeat_menu(
    const std::string& title, repeat_type last_selection ) const -> repeat_type
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

void repair_item_activity_actor::finish( player_activity& act, Character& who )
{
    player& p = static_cast<player &>( who );

    repeat = static_cast<repeat_type>( act.get_value( 0, REPEAT_INIT ) );

    item* fake_tool = nullptr;
    item* ploc = nullptr;

    if( hack_type != hack_type_t::none ) {
        fake_tool = get_fake_tool();
    } else if( tool_item ) {
        ploc = &*tool_item;
    }
    const int hack_original_charges = fake_tool ? fake_tool->charges : 0;

    item* main_tool = nullptr;
    if( hack_type != hack_type_t::none ) { main_tool = fake_tool; }
    if( main_tool == nullptr && ploc ) { main_tool = ploc; }
    if( main_tool == nullptr ) { main_tool = &p.i_at( act.index ); }
    if( main_tool == nullptr ) {
        debugmsg( "Failed to get main_tool for long repair" );
        act.set_to_null();
        return;
    }

    item* used_tool = main_tool->get_usable_item( iuse_name_string );
    if( used_tool == nullptr ) {
        debugmsg( "Lost tool used for long repair" );
        act.set_to_null();
        return;
    }

    const use_function* use_fun = used_tool->get_use( iuse_name_string );
    const repair_item_actor* actor = dynamic_cast<const repair_item_actor *>(
                                         use_fun->get_actor_ptr() );
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
        const bool cannot_continue_repair =
            attempt == repair_item_actor::AS_CANT || destroyed
            || !actor->can_repair_target( p, *fix_location, !destroyed );
        if( cannot_continue_repair ) { fix_item = safe_reference<item>(); }

        const bool event_happened =
            attempt == repair_item_actor::AS_FAILURE || attempt == repair_item_actor::AS_SUCCESS
            || old_level != p.get_skill_level( actor->used_skill );
        const bool need_input =
            ( repeat == REPEAT_ONCE ) || ( repeat == REPEAT_EVENT && event_happened )
            || ( repeat == REPEAT_FULL && ( cannot_continue_repair || fix_location->damage() <= 0 ) );
        if( need_input ) { repeat = REPEAT_INIT; }
    }

    if( !actor->can_use_tool( p, *used_tool, true ) ) {
        act.set_to_null();
        return;
    }

    while( !fix_item ) {
        item* item_loc = game_menus::inv::repair( p, actor, main_tool );
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

    const item& fix = *fix_item;

    if( repeat == REPEAT_INIT ) {
        const int level = p.get_skill_level( actor->used_skill );
        repair_item_actor::repair_type action_type = actor->default_action( fix, level );
        if( action_type == repair_item_actor::RT_NOTHING ) {
            p.add_msg_if_player( _( "You won't learn anything more by doing that." ) );
        }

        const std::pair<float, float> chance = actor->repair_chance( p, fix, action_type );
        if( chance.first <= 0.0f ) { action_type = repair_item_actor::RT_PRACTICE; }

        std::string title = string_format(
                                _( "%s %s\n" ), repair_item_actor::action_description( action_type ), fix.tname() );
        title += string_format(
                     _( "Charges: <color_light_blue>%s/%s</color> %s (%s per use)\n" ),
                     used_tool->ammo_remaining(), used_tool->ammo_capacity(),
                     item::nname( used_tool->ammo_current() ), used_tool->ammo_required() );
        title += string_format(
                     _( "Skill used: <color_light_blue>%s (%s)</color>\n" ), actor->used_skill->name(), level );
        title += string_format(
                     _( "Success chance: <color_light_blue>%.1f</color>%%\n" ), 100.0f * chance.first );
        title += string_format(
                     _( "Damage chance: <color_light_blue>%.1f</color>%%" ), 100.0f * chance.second );

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

    progress.purge();
    progress.emplace( _( "Repairing" ), actor->move_cost );
}

void repair_item_activity_actor::serialize( JsonOut& jsout ) const
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

std::unique_ptr<activity_actor> repair_item_activity_actor::deserialize( JsonIn& jsin )
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

void mend_item_activity_actor::finish( player_activity& act, Character& who )
{
    act.set_to_null();
    if( !target_item ) {
        debugmsg( "invalid arguments to ACT_MEND_ITEM" );
        return;
    }
    item* target = &*target_item;

    const auto f = target->faults.find( fault_id( fault_id_str ) );
    if( f == target->faults.end() ) {
        debugmsg( "item %s does not have fault %s", target->tname(), fault_id_str );
        return;
    }

    player& p = static_cast<player &>( who );
    const mending_method* method = fault_id( fault_id_str )->find_mending_method( method_id );
    if( !method ) {
        debugmsg( "invalid mending_method id for ACT_MEND_ITEM." );
        return;
    }

    const inventory& inv = p.crafting_inventory();
    const requirement_data& reqs = method->requirements.obj();
    if( !reqs.can_make_with_inventory( inv, is_crafting_component ) ) {
        add_msg( m_info, _( "You are currently unable to mend the %s." ), target->tname() );
    }
    for( const auto& e : reqs.get_components() ) { p.consume_items( e ); }
    for( const auto& e : reqs.get_tools() ) { p.consume_tools( e ); }
    p.invalidate_crafting_inventory();

    const auto mend = [&]( item * tgt ) {
        tgt->faults.erase( *f );
        if( method->turns_into ) { tgt->faults.emplace( *method->turns_into ); }
        if( method->also_mends ) { tgt->faults.erase( *method->also_mends ); }
        if( fault_id_str == "fault_gun_blackpowder" || fault_id_str == "fault_gun_dirt" ) {
            tgt->set_var( "dirt", 0 );
        }
        add_msg( m_good, method->success_msg.translated(), tgt->tname() );
    };

    mend( target );
    for( const auto& mod : target->gunmods() ) {
        if( !mod->faults.contains( fault_id( fault_id_str ) ) ) { continue; }
        mend( mod );
    }
}

void mend_item_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "target_item", target_item );
    jsout.member( "fault_id_str", fault_id_str );
    jsout.member( "method_id", method_id );
    jsout.end_object();
}

std::unique_ptr<activity_actor> mend_item_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<mend_item_activity_actor> actor( new mend_item_activity_actor() );
    JsonObject data = jsin.get_object();
    data.read( "target_item", actor->target_item );
    data.read( "fault_id_str", actor->fault_id_str );
    data.read( "method_id", actor->method_id );
    return actor;
}

// ---- toolmod_add_activity_actor -------------------------------------------

void toolmod_add_activity_actor::finish( player_activity& act, Character& who )
{
    act.set_to_null();
    if( !base_tool || !mod_item ) {
        debugmsg( "Incompatible arguments to ACT_TOOLMOD_ADD" );
        return;
    }
    item& tool = *base_tool;
    item& mod = *mod_item;
    who.add_msg_if_player(
        m_good, _( "You successfully attached the %1$s to your %2$s." ), mod.tname(), tool.tname() );
    mod.set_flag( flag_IRREMOVABLE );
    tool.put_in( mod.detach() );
}

void toolmod_add_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "base_tool", base_tool );
    jsout.member( "mod_item", mod_item );
    jsout.end_object();
}

std::unique_ptr<activity_actor> toolmod_add_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<toolmod_add_activity_actor> actor( new toolmod_add_activity_actor() );
    JsonObject data = jsin.get_object();
    data.read( "base_tool", actor->base_tool );
    data.read( "mod_item", actor->mod_item );
    return actor;
}

// ---- gunmod_add_activity_actor --------------------------------------------

void gunmod_add_activity_actor::finish( player_activity& act, Character& who )
{
    act.set_to_null();

    item& gun = *gun_item;
    item& mod = *mod_item;

    if( !gun.is_gunmod_compatible( mod ).success() ) {
        debugmsg( "Invalid arguments in ACT_GUNMOD_ADD" );
        return;
    }

    if( !tool_id.is_empty() && qty > 0 ) { who.use_charges( tool_id, qty ); }

    if( rng( 0, 100 ) <= roll ) {
        add_msg( m_good, _( "You successfully attached the %1$s to your %2$s." ), mod.tname(),
                 gun.tname() );
        gun.put_in( mod.detach() );
    } else if( rng( 0, 100 ) <= risk ) {
        if( gun.inc_damage() ) {
            for( item * gunmod : gun.gunmods() ) {
                if( gunmod->is_irremovable() ) { who.remove_item( *gunmod ); }
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

void gunmod_add_activity_actor::serialize( JsonOut& jsout ) const
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

std::unique_ptr<activity_actor> gunmod_add_activity_actor::deserialize( JsonIn& jsin )
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

void reload_activity_actor::calc_all_moves( player_activity& act, Character& who )
{
    activity_actor::calc_all_moves( act, who );
    if( !target_item || !ammo_item || qty <= 0 ) {
        act.set_to_null();
        return;
    }
    int moves = ammo_item->obtain_cost( who, qty ) + who.item_reload_cost( *target_item, *ammo_item,
                qty );
    progress.emplace( _( "Reloading" ), moves );
}
void reload_activity_actor::finish( player_activity& act, Character& who )
{
    act.set_to_null();
    if( !target_item || !ammo_item || qty <= 0 ) {
        debugmsg( "invalid arguments to ACT_RELOAD" );
        return;
    }
    item& reloadable = *target_item;
    item& ammo = *ammo_item;
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

    player& p = static_cast<player &>( who );
    if( reloadable.is_gun() ) {
        p.recoil = MAX_RECOIL;
        if( reloadable.has_flag( flag_RELOAD_ONE ) && !ammo.has_flag( flag_SPEEDLOADER ) ) {
            for( int i = 0; i != qty; ++i ) { msg = _( "You insert one %2$s into the %1$s." ); }
        }
        if( reloadable.type->gun->reload_noise_volume > 0 ) {
            sound_event se;
            se.origin = p.bub_pos();
            se.volume = reloadable.type->gun->reload_noise_volume;
            se.category = sounds::sound_t::activity;
            se.description = reloadable.type->gun->reload_noise;
            se.id = "reload";
            se.variant = reloadable.typeId().str();
            sounds::sound( se );
            sfx::play_variant_sound( "reload", reloadable.typeId().str(),
                                     sfx::get_heard_volume( p.bub_pos(), se.volume ) );
        }
    } else if( reloadable.is_container() ) {
        msg = _( "You refill the %s." );
    }
    add_msg( m_neutral, msg, reloadable.tname(), ammo_name );
}

void reload_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "target_item", target_item );
    jsout.member( "ammo_item", ammo_item );
    jsout.member( "qty", qty );
    jsout.end_object();
}

std::unique_ptr<activity_actor> reload_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<reload_activity_actor> actor( new reload_activity_actor() );
    JsonObject data = jsin.get_object();
    data.read( "target_item", actor->target_item );
    data.read( "ammo_item", actor->ammo_item );
    data.read( "qty", actor->qty );
    return actor;
}

// ---- wear_activity_actor --------------------------------------------------

void wear_activity_actor::do_turn( player_activity& act, Character& who )
{
    player& p = static_cast<player &>( who );
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

    if( items.empty() ) { p.cancel_activity(); }
}

void wear_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "items", items );
    jsout.member( "quantities", quantities );
    jsout.end_object();
}

std::unique_ptr<activity_actor> wear_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<wear_activity_actor> actor( new wear_activity_actor() );
    JsonObject data = jsin.get_object();
    data.read( "items", actor->items );
    data.read( "quantities", actor->quantities );
    return actor;
}

// ---- armor_layers_activity_actor ------------------------------------------

void armor_layers_activity_actor::do_turn( player_activity& act, Character& who )
{
    who.cancel_activity();
    show_armor_layers_ui( who );
}

void armor_layers_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.end_object();
}

std::unique_ptr<activity_actor> armor_layers_activity_actor::deserialize( JsonIn& jsin )
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

void consume_menu_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "menu_type", static_cast<int>( menu_type ) );
    jsout.end_object();
}

std::unique_ptr<activity_actor> consume_menu_activity_actor::deserialize( JsonIn& jsin )
{
    JsonObject data = jsin.get_object();
    int menu_type_int = 0;
    data.read( "menu_type", menu_type_int );
    return std::make_unique<consume_menu_activity_actor>(
               static_cast<consume_menu_type>( menu_type_int ) );
}


// ---- firstaid_activity_actor ----------------------------------------------

void firstaid_activity_actor::finish( player_activity& act, Character& who )
{
    static const std::string iuse_name_string( "heal" );

    item* it = &*healing_item;
    if( it == nullptr ) {
        debugmsg( "Lost healing item for firstaid activity" );
        act.set_to_null();
        return;
    }

    item* used_tool = it->get_usable_item( iuse_name_string );
    if( used_tool == nullptr ) {
        debugmsg( "Lost tool used for healing" );
        act.set_to_null();
        return;
    }

    const use_function* use_fun = used_tool->get_use( iuse_name_string );
    const heal_actor* actor = dynamic_cast<const heal_actor *>( use_fun->get_actor_ptr() );
    if( actor == nullptr ) {
        debugmsg( "iuse_actor type descriptor and actual type mismatch" );
        act.set_to_null();
        return;
    }

    player& patient = static_cast<player &>( who );
    const bodypart_str_id healed = bodypart_str_id( body_part );
    const int charges_consumed = actor->finish_using( patient, patient, *used_tool, healed );
    patient.consume_charges( *it, charges_consumed );

    act.set_to_null();
}

void firstaid_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "healing_item", healing_item.serialize() );
    jsout.member( "body_part", body_part );
    jsout.member( "moves", moves );
    jsout.end_object();
}

std::unique_ptr<activity_actor> firstaid_activity_actor::deserialize( JsonIn& jsin )
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

void wood_chop_activity_actor::do_turn( player_activity& act, Character& who )
{
    map& here = get_map();
    sfx::play_activity_sound( "tool", "axe",
                              sfx::get_heard_volume( here.abs_to_bub( act.placement ), 85 ) );
    if( action_time_scale::once_every_this_tick( 1_minutes ) ) {
        //~ Sound of a wood chopping tool at work!
        sound_event se;
        se.origin = here.abs_to_bub( act.placement );
        se.volume = 85;
        se.category = sounds::sound_t::activity;
        se.description = _( "CHK!" );
        se.id = "tool";
        se.variant = "axe";
        se.from_player = who.is_avatar();
        se.from_npc = !se.from_player;
        se.faction = who.get_faction()->id();
        se.monfaction = who.get_faction()->mon_faction();
        sounds::sound( se );
    }
}

void wood_chop_activity_actor::finish( player_activity& act, Character& who )
{
    map& here = get_map();
    player& p = static_cast<player &>( who );

    switch( chop_type ) {
        case wood_chop_type::TREE: {
            const auto& pos = here.abs_to_bub( placement );

            tripoint_rel_ms direction;
            if( !p.is_npc() ) {
                if( p.backlog.empty() || p.backlog.front()->id() != ACT_MULTIPLE_CHOP_TREES ) {
                    while( true ) {
                        if( const auto dir = choose_direction( _(
                                "Select a direction for the tree "
                                "to fall in." ) ) ) {
                            direction = *dir;
                            break;
                        }
                    }
                }
            } else {
                std::vector<tripoint_rel_ms> valid_directions;

                for( const auto& elem : here.points_in_radius( pos, 1 ) ) {
                    bool cantuse = false;
                    auto direc = elem - pos;
                    auto proposed_to = pos + point_rel_ms( 3 * direction.x(), 3 * direction.y() );
                    std::vector<tripoint_bub_ms> rough_tree_line = line_to( pos, proposed_to );
                    for( const auto& elem : rough_tree_line ) {
                        if( g->critter_at( elem ) ) {
                            cantuse = true;
                            break;
                        }

                        ter_t ter = here.ter( elem ).obj();
                        furn_t furn = here.furn( elem ).obj();
                        if( elem != pos
                            && ( ter.bash.str_max != -1 || ( furn.id && furn.bash.str_max != -1 ) ) ) {
                            cantuse = true;
                            break;
                        }
                        if( veh_pointer_or_null( here.veh_at( elem ) ) ) {
                            cantuse = true;
                            break;
                        }
                    }
                    if( !cantuse ) { valid_directions.push_back( direc ); }
                }
                direction = random_entry( valid_directions, direction );
            }

            const auto to = pos + 3 * direction.xy() + point( rng( -1, 1 ), rng( -1, 1 ) );
            std::vector<tripoint_bub_ms> tree = line_to( pos, to, rng( 1, 8 ) );
            for( const auto& elem : tree ) {
                here.batter( elem, 300, 5 );
                here.ter_set( elem, t_trunk );
            }

            here.ter_set( pos, t_stump );
            p.add_msg_if_player( m_good, _( "You finish chopping down a tree." ) );
            here.collapse_at( pos, false, true, false );
            sfx::play_variant_sound(
                "misc", "timber", sfx::get_heard_volume( here.abs_to_bub( placement ), 95 ) );

            // Exertion calculation
            if( !axe ) {
                debugmsg( "woodcutting item location not set" );
                activity_handlers::resume_for_multi_activities( p );
                act.set_to_null();
                return;
            }

            item* it = &*axe;
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
            const auto& pos = here.abs_to_bub( placement );
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

            item* it = &*axe;
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
                here.spawn_item(
                    here.abs_to_bub( placement ), itype_splinter, scraps, 0, calendar::turn );
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

void wood_chop_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "chop_type", static_cast<int>( chop_type ) );
    jsout.member( "placement", placement );
    jsout.member( "axe", axe.serialize() );
    jsout.member( "moves", moves );
    jsout.end_object();
}

std::unique_ptr<activity_actor> wood_chop_activity_actor::deserialize( JsonIn& jsin )
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
    auto act = std::make_unique<wood_chop_activity_actor>(
                   static_cast<wood_chop_type>( chop_type_int ), placement_val, moves_val, axe_ref );
    return act;
}

// Wave 6a: Simple activities


// ---- train_activity_actor ----

