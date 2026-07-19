#include "activity_actor.h"

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

void game_activity_actor::do_turn( player_activity& act, Character& who )
{
    player& p = dynamic_cast<player &>( who );

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
        act.set_to_null();
        return;
    }
    item& game_item_ref = *game_item;

    if( calendar::once_every( 1_minutes ) ) {
        int energy = game_item_ref.ammo_required();
        energy -= game_item_ref.ammo_consume( energy, p.bub_pos() );
        if( energy > 0 && game_item_ref.has_flag( flag_USE_UPS ) ) {
            if( p.use_charges_if_avail( itype_UPS, energy ) ) { energy = 0; }
        }
        if( energy ) {
            act.set_to_null();
            add_msg( m_info, _( "The %s runs out of batteries." ), game_item_ref.tname() );
        }
    }
}

void game_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "game_type", static_cast<int>( gtype ) );
    jsout.member( "game_item", game_item.serialize() );
    jsout.end_object();
}

std::unique_ptr<activity_actor> game_activity_actor::deserialize( JsonIn& jsin )
{
    JsonObject data = jsin.get_object();
    int gtype_int = 0;
    safe_reference<item> item_ref;
    data.read( "game_type", gtype_int );
    data.read( "game_item", item_ref );
    auto act = std::make_unique<game_activity_actor>( static_cast<game_type>( gtype_int ), item_ref );
    return act;
}

void vibe_activity_actor::do_turn( player_activity& act, Character& who )
{
    player& p = dynamic_cast<player &>( who );

    if( !vibrator ) {
        debugmsg( "ACT_VIBE with no vibrator item" );
        act.set_to_null();
        return;
    }
    item& vibrator_item = *vibrator;

    if( p.encumb( body_part_mouth ) >= 30 ) {
        act.set_to_null();
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
        act.set_to_null();
        add_msg( m_info, _( "You're too tired to continue." ) );
    }
}

void vibe_activity_actor::finish( player_activity & /*act*/, Character& who )
{
    player& p = dynamic_cast<player &>( who );
    p.add_msg_if_player( m_good, _( "You feel much better." ) );
    p.add_morale( MORALE_FEELING_GOOD, 10, 40 );
}

void vibe_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "vibrator", vibrator.serialize() );
    jsout.end_object();
}

std::unique_ptr<activity_actor> vibe_activity_actor::deserialize( JsonIn& jsin )
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

void morale_activity_actor::finish( player_activity & /*act*/, Character& who )
{
    player& p = dynamic_cast<player &>( who );

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

void morale_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "morale_type", static_cast<int>( mtype ) );
    jsout.end_object();
}

std::unique_ptr<activity_actor> morale_activity_actor::deserialize( JsonIn& jsin )
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

void wait_activity_actor::finish( player_activity & /*act*/, Character& who )
{
    player& p = dynamic_cast<player &>( who );

    if( wtype == wait_type::WAIT_NPC ) {
        p.add_msg_if_player( _( "%s finishes with you…" ), npc_name );
    } else {
        p.add_msg_if_player( _( "You finish waiting." ) );
    }
}

void wait_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "wait_type", static_cast<int>( wtype ) );
    jsout.member( "npc_name", npc_name );
    jsout.member( "wait_duration", wait_duration );
    jsout.end_object();
}

std::unique_ptr<activity_actor> wait_activity_actor::deserialize( JsonIn& jsin )
{
    JsonObject data = jsin.get_object();
    int wtype_int = 0;
    std::string name;
    time_duration duration = 0_minutes;
    data.read( "wait_type", wtype_int );
    data.read( "npc_name", name );
    data.read( "wait_duration", duration );
    auto act = std::make_unique<wait_activity_actor>( static_cast<wait_type>( wtype_int ), name,
               duration );
    return act;
}

void socialize_activity_actor::finish( player_activity & /*act*/, Character& who )
{
    player& p = dynamic_cast<player &>( who );
    p.add_msg_if_player( _( "%s finishes chatting with you." ), npc_name );
}

void socialize_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "npc_name", npc_name );
    jsout.end_object();
}

std::unique_ptr<activity_actor> socialize_activity_actor::deserialize( JsonIn& jsin )
{
    JsonObject data = jsin.get_object();
    std::string name;
    data.read( "npc_name", name );
    auto act = std::make_unique<socialize_activity_actor>( name );
    return act;
}

void atm_activity_actor::do_turn( player_activity & /*act*/, Character& who )
{
    player& p = dynamic_cast<player &>( who );
    iexamine::atm( p, p.bub_pos() );
}


std::unique_ptr<activity_actor> atm_activity_actor::deserialize( JsonIn & /*jsin*/ )
{
    return std::make_unique<atm_activity_actor>();
}

void play_with_pet_activity_actor::finish( player_activity & /*act*/, Character& who )
{
    player& p = dynamic_cast<player &>( who );
    p.add_morale( MORALE_PLAY_WITH_PET, rng( 3, 10 ), 10, 5_hours, 25_minutes );
    p.add_msg_if_player( m_good, _( "Playing with your %s has lifted your spirits a bit." ), pet_name );
}

void play_with_pet_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "pet_name", pet_name );
    jsout.end_object();
}

std::unique_ptr<activity_actor> play_with_pet_activity_actor::deserialize( JsonIn& jsin )
{
    JsonObject data = jsin.get_object();
    std::string name;
    data.read( "pet_name", name );
    auto act = std::make_unique<play_with_pet_activity_actor>( name );
    return act;
}

void train_pet_activity_actor::finish( player_activity &act, Character& who )
{
    player& p = dynamic_cast<player &>( who );
    auto mon = pet.lock();
    if( mon && mon->type->pet_training
        && p.get_skill_level( skill_survival ) < mon->type->pet_training->min_skill ) {
        p.add_msg_if_player( m_bad, _( "You lack the skill to train %s effectively." ), pet_name );
        return;
    }
    if( !mon ) { act.set_to_null(); return; }
    mon->remove_effect( effect_well_fed );
    mon->remove_effect( effect_ai_waiting );
    if( 4 * p.get_skill_level( skill_survival ) >= rng( 0, 100 ) ) {
        if( mon && mon->type->pet_training ) {
            mon->training_level =
                std::min( mon->training_level + 1, mon->type->pet_training->max_level );
            for( const auto& lf : mon->type->pet_training->level_flags ) {
                if( lf.level == mon->training_level ) {
                    for( const m_flag f : lf.flags ) { mon->monster_flags.insert( f ); }
                }
            }
            p.add_msg_if_player(
                m_good, _( "Training your %s has paid off!  They are now at training level %d/%d." ),
                pet_name, mon->training_level, mon->type->pet_training->max_level );
        }
    } else {
        p.add_msg_if_player(
            m_neutral,
            _( "Training your %s takes time, it seems they are making a bit of progress at least." ),
            pet_name );
    }
}

void train_pet_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "pet_name", pet_name );
    jsout.end_object();
}

std::unique_ptr<activity_actor> train_pet_activity_actor::deserialize( JsonIn& jsin )
{
    JsonObject data = jsin.get_object();
    std::string name;
    data.read( "pet_name", name );
    auto act = std::make_unique<train_pet_activity_actor>( weak_ptr_fast<monster>(), name );
    return act;
}

void shear_activity_actor::finish( player_activity & /*act*/, Character& who )
{
    player& p = dynamic_cast<player &>( who );
    map& here = get_map();

    if( sheep_positions.empty() ) {
        debugmsg( "shearing activity with no position of monster stored" );
        return;
    }

    if( !shears ) {
        debugmsg( "shearing item location lost" );
        return;
    }

    const auto source_pos = here.abs_to_bub( sheep_positions[0] );
    monster* source_mon = g->critter_at<monster>( source_pos );
    if( source_mon == nullptr ) {
        debugmsg( "could not find source creature for shearing" );
        return;
    }

    for( int i = 0; i != 22; ++i ) {
        detached_ptr<item> wool_staple = item::spawn( itype_wool_staple, calendar::turn );
        here.add_item_or_charges( p.bub_pos(), std::move( wool_staple ) );
    }
    source_mon->add_effect( effect_sheared, calendar::season_length() );

    if( !tie_type.empty() && tie_type == "temp_tie" ) { source_mon->remove_effect( effect_tied ); }
}

void shear_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "sheep_positions", sheep_positions );
    jsout.member( "shears", shears.serialize() );
    jsout.member( "tie_type", tie_type );
    jsout.end_object();
}

std::unique_ptr<activity_actor> shear_activity_actor::deserialize( JsonIn& jsin )
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

void find_mount_activity_actor::do_turn( player_activity& act, Character& who )
{
    // NPC-only activity
    if( who.is_player() ) {
        act.set_to_null();
        return;
    }
    npc& guy = dynamic_cast<npc &>( who );
    monster* mon = guy.chosen_mount.lock().get();
    if( !mon ) {
        act.set_to_null();
        guy.revert_after_activity();
        return;
    }
    if( rl_dist( guy.bub_pos(), mon->bub_pos() ) <= 1 ) {
        if( mon->has_effect( effect_ai_waiting ) ) { mon->remove_effect( effect_ai_waiting ); }
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

void cracking_activity_actor::do_turn( player_activity & /*act*/, Character& who )
{
    if( who.is_deaf() && who.get_skill_level( skill_mechanics ) < 5 ) {
        who.add_msg_if_player( m_bad, _( "You can't hear the tumblers anymore, so you stop." ) );
        who.cancel_activity();
    }
}

void cracking_activity_actor::finish( player_activity& act, Character& who )
{
    auto& here = get_map();
    who.add_msg_if_player( m_good, _( "With a satisfying click, the lock on the safe opens!" ) );
    here.furn_set( here.abs_to_bub( placement ), f_safe_c );
    act.set_to_null();
}

void cracking_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "placement", placement );
    jsout.end_object();
}

std::unique_ptr<activity_actor> cracking_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<cracking_activity_actor> actor(
        new cracking_activity_actor( tripoint_abs_ms::zero() ) );
    JsonObject data = jsin.get_object();
    data.read( "placement", actor->placement );
    return actor;
}

// ---- wait_stamina_activity_actor ----

void wait_stamina_activity_actor::do_turn( player_activity& act, Character& who )
{
    player& p = static_cast<player &>( who );
    int stamina_threshold = p.get_stamina_max();
    if( stamina_threshold > 0 ) {
        // Check if we've reached the threshold
        if( p.get_stamina() >= stamina_threshold ) { finish( act, who ); }
    } else {
        // Waiting for max stamina
        if( p.get_stamina() >= p.get_stamina_max() ) { finish( act, who ); }
    }
}

void wait_stamina_activity_actor::finish( player_activity& act, Character& who )
{
    player& p = static_cast<player &>( who );
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

void wait_stamina_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "stamina_threshold", stamina_threshold );
    jsout.end_object();
}

std::unique_ptr<activity_actor> wait_stamina_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<wait_stamina_activity_actor> actor( new wait_stamina_activity_actor( 0 ) );
    JsonObject data = jsin.get_object();
    data.read( "stamina_threshold", actor->stamina_threshold );
    return actor;
}

// ---- read_activity_actor ----

void read_activity_actor::do_turn( player_activity& act, Character& who )
{
    if( who.is_player() ) {
        // For martial art reading, drain stamina
        if( !act.str_values.empty() && act.str_values[0] == "martial_art" && one_in( 3 ) ) {
            if( act.values.empty() ) {
                player& p = static_cast<player &>( who );
                act.values.push_back( p.get_stamina() );
            }
            player& p = static_cast<player &>( who );
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

void read_activity_actor::finish( player_activity& act, Character& who )
{
    if( !act || act.targets.empty() || !act.targets.front() ) {
        debugmsg( "Lost target of ACT_READ" );
        return;
    }
    if( who.is_npc() ) {
        npc* guy = dynamic_cast<npc *>( &who );
        if( guy ) { guy->finish_read( &*act.targets.front() ); }
    } else {
        if( avatar * u = dynamic_cast<avatar * >( &who ) ) {
            u->do_read( &*act.targets.front() );
        } else {
            act.set_to_null();
        }
        if( !act ) { who.add_msg_if_player( m_info, _( "You finish reading." ) ); }
    }
}

std::unique_ptr<activity_actor> read_activity_actor::deserialize( JsonIn & /*jsin*/ )
{
    return std::make_unique<read_activity_actor>();
}

// ---- try_sleep_activity_actor ----

void try_sleep_activity_actor::do_turn( player_activity& act, Character& who )
{
    player& p = static_cast<player &>( who );
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
                if( p.get_value( "sleep_query" ) == "false" ) { return; }
                uilist sleep_query;
                sleep_query.text = _( "You have trouble sleeping, keep trying?" );
                sleep_query.addentry( 1, true, 'S', _( "Stop trying to fall asleep and get up." ) );
                sleep_query.addentry( 2, true, 'c', _( "Continue trying to fall asleep." ) );
                sleep_query.addentry(
                    3, true, 'C', _( "Continue trying to fall asleep and don't ask again." ) );
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

void travelling_activity_actor::do_turn( player_activity& act, Character& who )
{
    player& p = static_cast<player &>( who );
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
        map& here = get_map();
        auto centre_sub = here.abs_to_bub( waypoint );
        if( !here.passable( centre_sub ) ) {
            tripoint_range<tripoint_bub_ms> candidates = here.points_in_radius( centre_sub, 2 );
            for( const auto& elem : candidates ) {
                if( here.passable( elem ) ) {
                    centre_sub = elem;
                    break;
                }
            }
        }
        const auto route_to = here.route(
                                  p.bub_pos(), centre_sub, p.get_legacy_pathfinding_settings(),
                                  p.get_legacy_path_avoid() );
        if( !route_to.empty() ) {
            p.set_destination(
                route_to,
                std::unique_ptr<player_activity>(
                    new player_activity( std::make_unique<travelling_activity_actor>() ) ) );
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

void start_fire_activity_actor::do_turn( player_activity& act, Character& who )
{
    player& p = static_cast<player &>( who );
    map& here = get_map();
    const auto bub_loc = here.abs_to_bub( placement );
    if( act.get_tools().empty() || !act.get_tools().front() ) {
        p.add_msg_if_player( m_bad,
                             _( "You have lost the item you were using to start the fire." ) );
        p.cancel_activity();
        return;
    }
    item& firestarter = *act.get_tools().front();

    if( !here.is_flammable( bub_loc )
        || ( firestarter.has_flag( flag_REQUIRES_TINDER ) && !here.tinder_at( bub_loc ) ) ) {
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

    const use_function* usef = firestarter.type->get_use( "firestarter" );
    if( usef == nullptr || usef->get_actor_ptr() == nullptr ) {
        p.add_msg_if_player( m_bad, _( "You have lost the item you were using to start the fire." ) );
        p.cancel_activity();
        return;
    }

    p.mod_moves( -p.moves );
    const firestarter_actor* actor = dynamic_cast<const firestarter_actor *>( usef->get_actor_ptr() );
    const float light = actor->light_mod( p.bub_pos() );
    progress.mod_moves_left( -static_cast<int>( light * 100 ) );
    if( light < 0.1 ) {
        p.add_msg_if_player(
            m_bad,
            _( "There is not enough sunlight to start a fire now.  You stop "
               "trying." ) );
        p.cancel_activity();
    }
}

void start_fire_activity_actor::finish( player_activity& act, Character& who )
{
    player& p = static_cast<player &>( who );
    static const std::string iuse_name_string( "firestarter" );

    if( act.get_tools().empty() || !act.get_tools().front() ) {
        debugmsg( "Lost tool used for starting fire" );
        act.set_to_null();
        return;
    }
    item& it = *act.get_tools().front();
    item* used_tool = it.get_usable_item( iuse_name_string );
    if( used_tool == nullptr ) {
        debugmsg( "Lost tool used for starting fire" );
        act.set_to_null();
        return;
    }

    const use_function* use_fun = used_tool->get_use( iuse_name_string );
    const firestarter_actor* actor = dynamic_cast<const firestarter_actor *>(
                                         use_fun->get_actor_ptr() );
    if( actor == nullptr ) {
        debugmsg( "iuse_actor type descriptor and actual type mismatch" );
        act.set_to_null();
        return;
    }

    if( it.type->can_have_charges() ) {
        if( it.has_flag( flag_USE_UPS ) ) { p.use_charges( itype_UPS, it.type->charges_to_use() ); }
        p.consume_charges( it, it.type->charges_to_use() );
    }
    p.practice( skill_survival, index, 5 );

    map& here = get_map();
    firestarter_actor::resolve_firestarter_use( p, here.abs_to_bub( placement ) );
    act.set_to_null();
}

void start_fire_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "tool", tool );
    jsout.member( "placement", placement );
    jsout.member( "index", index );
    jsout.member( "fire_moves", fire_moves );
    jsout.end_object();
}

std::unique_ptr<activity_actor> start_fire_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<start_fire_activity_actor> actor(
        new start_fire_activity_actor( safe_reference<item>(), tripoint_abs_ms::zero(), 0, 0 ) );
    JsonObject data = jsin.get_object();
    data.read( "tool", actor->tool );
    data.read( "placement", actor->placement );
    data.read( "index", actor->index );
    data.read( "fire_moves", actor->fire_moves );
    return actor;
}

// ---- fish_activity_actor ----

void fish_activity_actor::do_turn( player_activity& act, Character& who )
{
    player& p = static_cast<player &>( who );
    int fishing_mult = iuse::good_fishing_spot( abs_to_bub( placement ) );
    if( fishing_mult == 0 || p.is_blind() ) {
        act.set_to_null();
        p.add_msg_if_player(
            m_info,
            _( "You realize fishing here at the moment is pointless, and "
               "stop." ) );
        if( !p.backlog.empty() && p.backlog.front()->id() == ACT_MULTIPLE_FISH ) {
            p.backlog.clear();
            p.assign_activity( std::make_unique<player_activity>(
                                   std::make_unique<generic_multi_activity_actor>( ACT_MULTIPLE_FISH ) ) );
            return;
        }
        return;
    }
    if( act.get_tools().empty() || !act.get_tools().front() ) {
        p.add_msg_if_player( m_bad, _( "You have lost your fishing tool." ) );
        p.cancel_activity();
        return;
    }
    item& rod = *act.get_tools().front();
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
        caught.add( {"fish", 1}, 1 );
        // Inline rod_fish logic
        map& here = get_map();
        const std::pair<std::string, int> *caught_item = caught.pick();
        if( caught_item->first.contains( "fish" ) ) {
            const std::vector<mtype_id> fish_group = MonsterGroupManager::GetMonstersFromGroup(
                    mongroup_id( "GROUP_FISH" ) );
            const mtype_id fish_mon = random_entry_ref( fish_group );
            here.add_item_or_charges(
                p.bub_pos(), item::make_corpse( fish_mon, calendar::turn + rng( 0_turns, 3_hours ) ) );
            p.add_msg_if_player( m_good, _( "You caught a %s." ), fish_mon.obj().nname() );
        } else {
            itype_id possible( caught_item->first );
            if( possible.is_valid() ) {
                here.add_item_or_charges(
                    p.bub_pos(),
                    item::spawn( caught_item->first, calendar::turn, caught_item->second ), true );
                p.add_msg_if_player( m_good, _( "You reeled in %s." ) );
            }
        }
        for( item * &elem : here.i_at( p.bub_pos() ) ) {
            if( elem->is_corpse() && !elem->has_var( "activity_var" ) ) {
                elem->set_var( "activity_var", p.name );
            }
        }
    }
    if( calendar::once_every( 60_minutes ) ) { p.practice( skill_survival, rng( 1, 3 ) ); }
}

void fish_activity_actor::finish( player_activity& act, Character& who )
{
    player& p = static_cast<player &>( who );
    act.set_to_null();
    p.add_msg_if_player( m_info, _( "You finish fishing" ) );
    if( !p.backlog.empty() && p.backlog.front()->id() == ACT_MULTIPLE_FISH ) {
        p.backlog.clear();
        p.assign_activity( std::make_unique<player_activity>(
                               std::make_unique<generic_multi_activity_actor>( ACT_MULTIPLE_FISH ) ) );
    }
}

void fish_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "tool", tool );
    jsout.member( "placement", placement );
    jsout.end_object();
}

std::unique_ptr<activity_actor> fish_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<fish_activity_actor> actor(
        new fish_activity_actor( safe_reference<item>(), tripoint_abs_ms::zero() ) );
    JsonObject data = jsin.get_object();
    data.read( "tool", actor->tool );
    data.read( "placement", actor->placement );
    return actor;
}

// ---- milk_activity_actor ----

void milk_activity_actor::finish( player_activity& act, Character& who )
{
    if( coords == tripoint_abs_ms::zero() ) {
        debugmsg( "milking activity with no position of monster stored" );
        return;
    }
    map& here = get_map();
    monster* source_mon = g->critter_at<monster>( here.abs_to_bub( coords ) );
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
    if( !str_value.empty() && str_value == "temp_tie" ) { source_mon->remove_effect( effect_tied ); }
    act.set_to_null();
}

void milk_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "coords", coords );
    jsout.member( "str_value", str_value );
    jsout.end_object();
}

std::unique_ptr<activity_actor> milk_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<milk_activity_actor> actor(
        new milk_activity_actor( tripoint_abs_ms::zero(), std::string() ) );
    JsonObject data = jsin.get_object();
    data.read( "coords", actor->coords );
    data.read( "str_value", actor->str_value );
    return actor;
}

// ---- make_zlave_activity_actor ----

void make_zlave_activity_actor::finish( player_activity& act, Character& who )
{
    player& p = static_cast<player &>( who );
    act.set_to_null();
    map_stack items = g->m.i_at( p.bub_pos() );
    item* body = nullptr;

    for( item * &it : items ) {
        if( it->display_name() == str_value ) { body = it; }
    }

    if( body == nullptr ) {
        p.add_msg_if_player( m_info, _( "There's no corpse to make into a zombie slave!" ) );
        return;
    }

    if( success > 0 ) {
        p.practice( skill_firstaid, rng( 2, 5 ) );
        p.practice( skill_survival, rng( 2, 5 ) );

        p.add_msg_if_player(
            m_good,
            _( "You slice muscles and tendons, and remove body parts until "
               "you're confident the zombie won't be able to attack you "
               "when it reanimates." ) );

        body->set_var( "zlave", "zlave" );
        if( one_in( 10 ) ) { body->set_var( "zlave", "mutilated" ); }
    } else if( success > -20 ) {
        p.practice( skill_firstaid, rng( 3, 6 ) );
        p.practice( skill_survival, rng( 3, 6 ) );

        p.add_msg_if_player(
            m_warning,
            _( "You hack into the corpse and chop off some body parts.  "
               "You think the zombie won't be able to attack when it "
               "reanimates." ) );

        body->set_var( "zlave", "zlave" );
    } else {
        p.add_msg_if_player( m_bad, _( "You failed to properly dismember the corpse." ) );
    }
}

void make_zlave_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "str_value", str_value );
    jsout.member( "success", success );
    jsout.end_object();
}

std::unique_ptr<activity_actor> make_zlave_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<make_zlave_activity_actor> actor(
        new make_zlave_activity_actor( std::string(), 0 ) );
    JsonObject data = jsin.get_object();
    data.read( "str_value", actor->str_value );
    data.read( "success", actor->success );
    return actor;
}

// ---- tree_communion_activity_actor ----

void tree_communion_activity_actor::do_turn( player_activity& act, Character& who )
{
    player& p = static_cast<player &>( who );
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
    if( !calendar::once_every( 1_minutes ) ) { return; }
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
                p.add_msg_if_player(
                    "%s", SNIPPET.random_from_category( "tree_communion" ).value_or( translation() ) );
            }
            return;
        }
        for( const tripoint_abs_omt& neighbor : points_in_radius( tpt, 1 ) ) {
            if( seen.contains( neighbor ) ) { continue; }
            seen.insert( neighbor );
            if( !get_overmapbuffer( p.get_dimension() ).ter( neighbor ).obj().is_wooded() ) { continue; }
            q.push( neighbor );
        }
        q.pop();
    }
    p.add_msg_if_player( m_info, _( "The trees have shown you what they will." ) );
    act.set_to_null();
}

void tree_communion_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "initial_rooting_countdown", initial_rooting_countdown );
    jsout.end_object();
}

std::unique_ptr<activity_actor> tree_communion_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<tree_communion_activity_actor> actor( new tree_communion_activity_actor( 0 ) );
    JsonObject data = jsin.get_object();
    data.read( "initial_rooting_countdown", actor->initial_rooting_countdown );
    return actor;
}
