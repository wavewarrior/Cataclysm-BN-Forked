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
#ifdef COOP_ENABLED
#include "coop_client.h"
#include "field.h"
#include <set>
#endif
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


void hacksaw_activity_actor::start( player_activity& act, Character & /*who*/ )
{
    const map& here = get_map();

    if( here.has_furn( target ) ) {
        const furn_id furn_type = here.furn( target );
        if( !furn_type->hacksaw->valid() ) {
            if( !testing ) { debugmsg( "%s hacksaw is invalid", furn_type.id().str() ); }
            act.set_to_null();
            return;
        }
        progress.emplace( furn_type->name(), to_moves<int>( furn_type->hacksaw->duration() ) );
    } else if( !here.ter( target )->is_null() ) {
        const ter_id ter_type = here.ter( target );
        if( !ter_type->hacksaw->valid() ) {
            if( !testing ) { debugmsg( "%s hacksaw is invalid", ter_type.id().str() ); }
            act.set_to_null();
            return;
        }
        progress.emplace( ter_type->name(), to_moves<int>( ter_type->hacksaw->duration() ) );
    } else {
        if( !testing ) { debugmsg( "hacksaw activity called on invalid terrain" ); }
        act.set_to_null();
        return;
    }
}

void hacksaw_activity_actor::do_turn( player_activity & /* act */, Character& who )
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
                add_msg( _( "%1$s %2$s ran out of charges." ), who.disp_name( false, true ),
                         tool->tname() );
            }
        }
        who.cancel_activity();
    }
}

void hacksaw_activity_actor::finish( player_activity& act, Character& who )
{
    map& here = get_map();
    const activity_data_common* data;

    if( here.has_furn( target ) ) {
        const furn_id furn_type = here.furn( target );
        if( !furn_type->hacksaw->valid() ) {
            if( !testing ) { debugmsg( "%s hacksaw is invalid", furn_type.id().str() ); }
            act.set_to_null();
            return;
        }

        const furn_str_id new_furn = furn_type->hacksaw->result();
        if( !new_furn.is_valid() ) {
            if( !testing ) { debugmsg( "hacksaw furniture: %s invalid furniture", new_furn.str() ); }
            act.set_to_null();
            return;
        }

        data = static_cast<const activity_data_common *>( &*furn_type->hacksaw );
        here.furn_set( target, new_furn );
    } else if( !here.ter( target )->is_null() ) {
        const ter_id ter_type = here.ter( target );
        if( !ter_type->hacksaw->valid() ) {
            if( !testing ) { debugmsg( "%s hacksaw is invalid", ter_type.id().str() ); }
            act.set_to_null();
            return;
        }

        const ter_str_id new_ter = ter_type->hacksaw->result();
        if( !new_ter.is_valid() ) {
            if( !testing ) { debugmsg( "hacksaw terrain: %s invalid terrain", new_ter.str() ); }
            act.set_to_null();
            return;
        }

        data = static_cast<const activity_data_common *>( &*ter_type->hacksaw );
        here.ter_set( target, new_ter );
    } else {
        if( !testing ) { debugmsg( "hacksaw activity finished on invalid terrain" ); }
        act.set_to_null();
        return;
    }

    for( const activity_byproduct& byproduct : data->byproducts() ) {
        const int amount = byproduct.roll();
        if( byproduct.item->count_by_charges() ) {
            here.add_item_or_charges( target, item::spawn( byproduct.item, calendar::turn, amount ) );
        } else {
            for( int i = 0; i < amount; ++i ) {
                here.add_item_or_charges( target, item::spawn( byproduct.item, calendar::turn ) );
            }
        }
    }

    if( !data->message().empty() ) { who.add_msg_if_player( m_info, data->message().translated() ); }

    act.set_to_null();
}

void hacksaw_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.member( "tool", tool );

    jsout.end_object();
}

std::unique_ptr<activity_actor> hacksaw_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<hacksaw_activity_actor> actor(
        new hacksaw_activity_actor( tripoint_bub_ms::zero(), safe_reference<item>() ) );
    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "target", actor->target );
    data.read( "tool", actor->tool );
    return actor;
}

void boltcutting_activity_actor::start( player_activity& act, Character & /*who*/ )
{
    const map& here = get_map();

    if( here.has_furn( target ) ) {
        const furn_id furn_type = here.furn( target );
        if( !furn_type->boltcut->valid() ) {
            if( !testing ) { debugmsg( "%s boltcut is invalid", furn_type.id().str() ); }
            act.set_to_null();
            return;
        }
        progress.emplace( furn_type->name(), to_moves<int>( furn_type->boltcut->duration() ) );
    } else if( !here.ter( target )->is_null() ) {
        const ter_id ter_type = here.ter( target );
        if( !ter_type->boltcut->valid() ) {
            if( !testing ) { debugmsg( "%s boltcut is invalid", ter_type.id().str() ); }
            act.set_to_null();
            return;
        }
        progress.emplace( ter_type->name(), to_moves<int>( ter_type->boltcut->duration() ) );
    } else {
        if( !testing ) { debugmsg( "boltcut activity called on invalid terrain" ); }
        act.set_to_null();
        return;
    }
}

void boltcutting_activity_actor::do_turn( player_activity & /* act */, Character& who )
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
                add_msg( _( "%1$s %2$s ran out of charges." ), who.disp_name( false, true ),
                         tool->tname() );
            }
        }
        who.cancel_activity();
    }
}

void boltcutting_activity_actor::finish( player_activity& act, Character& who )
{
    map& here = get_map();
    const activity_data_common* data;

    if( here.has_furn( target ) ) {
        const furn_id furn_type = here.furn( target );
        if( !furn_type->boltcut->valid() ) {
            if( !testing ) { debugmsg( "%s boltcut is invalid", furn_type.id().str() ); }
            act.set_to_null();
            return;
        }

        const furn_str_id new_furn = furn_type->boltcut->result();
        if( !new_furn.is_valid() ) {
            if( !testing ) { debugmsg( "boltcut furniture: %s invalid furniture", new_furn.str() ); }
            act.set_to_null();
            return;
        }

        data = static_cast<const activity_data_common *>( &*furn_type->boltcut );
        here.furn_set( target, new_furn );
    } else if( !here.ter( target )->is_null() ) {
        const ter_id ter_type = here.ter( target );
        if( !ter_type->boltcut->valid() ) {
            if( !testing ) { debugmsg( "%s boltcut is invalid", ter_type.id().str() ); }
            act.set_to_null();
            return;
        }

        const ter_str_id new_ter = ter_type->boltcut->result();
        if( !new_ter.is_valid() ) {
            if( !testing ) { debugmsg( "boltcut terrain: %s invalid terrain", new_ter.str() ); }
            act.set_to_null();
            return;
        }

        data = static_cast<const activity_data_common *>( &*ter_type->boltcut );
        here.ter_set( target, new_ter );
    } else {
        if( !testing ) { debugmsg( "boltcut activity finished on invalid terrain" ); }
        act.set_to_null();
        return;
    }

    if( data->sound().empty() ) {
        sounds::sound( target, 5, sounds::sound_t::combat, _( "Snick, snick, gachunk!" ), true, "tool",
                       "boltcutters" );
    } else {
        sounds::sound( target, 5, sounds::sound_t::combat, data->sound().translated(), true, "tool",
                       "boltcutters" );
    }


    for( const activity_byproduct& byproduct : data->byproducts() ) {
        const int amount = byproduct.roll();
        if( byproduct.item->count_by_charges() ) {
            here.add_item_or_charges( target, item::spawn( byproduct.item, calendar::turn, amount ) );
        } else {
            for( int i = 0; i < amount; ++i ) {
                here.add_item_or_charges( target, item::spawn( byproduct.item, calendar::turn ) );
            }
        }
    }

    if( !data->message().empty() ) { who.add_msg_if_player( m_info, data->message().translated() ); }

    act.set_to_null();
}

void boltcutting_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.member( "tool", tool );

    jsout.end_object();
}

std::unique_ptr<activity_actor> boltcutting_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<boltcutting_activity_actor> actor(
        new boltcutting_activity_actor( tripoint_bub_ms::zero(), safe_reference<item>() ) );

    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "target", actor->target );
    data.read( "tool", actor->tool );
    return actor;
}

// ---- burrow_activity_actor ----

// Shared exertion cost for the mining-family activities (burrow/pickaxe/jackhammer):
// identical kcal/thirst/fatigue drain, avatar-only, halved for stocky troglodytes.
static void apply_mining_exertion( Character& who, int moves )
{
    if( !who.is_avatar() ) { return; }
    int act_exertion = moves;
    if( who.has_trait( trait_STOCKY_TROGLO ) ) { act_exertion /= 2; }
    who.mod_stored_kcal( std::min( -1, -act_exertion / to_moves<int>( 45_seconds ) ) );
    who.mod_thirst( std::max( 1, act_exertion / to_moves<int>( 6_minutes ) ) );
    who.mod_fatigue( std::max( 1, act_exertion / to_moves<int>( 3_minutes ) ) );
}

void burrow_activity_actor::start( player_activity &, Character& who )
{
    map& here = get_map();
    int moves = to_moves<int>( 20_minutes );
    moves += ( 24 - std::min( who.str_cur, 24 ) ) * to_moves<int>( 150_seconds );
    if( here.move_cost( here.abs_to_bub( target ) ) == 2 ) { moves /= 2; }
    const std::vector<npc *> helpers =
        character_funcs::get_crafting_helpers( static_cast<player &>( who ), 3 );
    moves = moves * ( 10 - static_cast<int>( helpers.size() ) ) / 10;
    progress.emplace( _( "Burrowing" ), moves );
}

void burrow_activity_actor::do_turn( player_activity &, Character& who )
{
    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
    map& here = get_map();
    sfx::play_activity_sound( "activity", "burrow",
                              sfx::get_heard_volume( here.abs_to_bub( target ) ) );
    if( calendar::once_every( 1_minutes ) ) {
        sounds::sound( here.abs_to_bub( target ), 10, sounds::sound_t::movement,
                       _( "ScratchCrunchScrabbleScurry." ) );
    }
}

void burrow_activity_actor::finish( player_activity& act, Character& who )
{
    map& here = get_map();
    apply_mining_exertion( who, progress.get_moves_total() );
    act.set_to_null();
    who.add_msg_if_player( m_good, _( "You finish burrowing." ) );
    here.destroy( here.abs_to_bub( target ), true );
}

void burrow_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.end_object();
}

std::unique_ptr<activity_actor> burrow_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<burrow_activity_actor> actor(
        new burrow_activity_actor( tripoint_abs_ms::zero() ) );
    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "target", actor->target );
    return actor;
}

// ---- pickaxe_activity_actor ----

void pickaxe_activity_actor::start( player_activity &, Character& who )
{
    map& here = get_map();
    int moves = to_moves<int>( 30_minutes );
    moves += ( 24 - std::min( who.str_cur, 24 ) ) * to_moves<int>( 225_seconds );
    if( here.move_cost( here.abs_to_bub( target ) ) == 2 ) { moves /= 2; }
    const std::vector<npc *> helpers =
        character_funcs::get_crafting_helpers( static_cast<player &>( who ), 3 );
    moves = moves * ( 10 - static_cast<int>( helpers.size() ) ) / 10;
    progress.emplace( _( "Mining" ), moves );
}

void pickaxe_activity_actor::do_turn( player_activity &, Character& who )
{
    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
    map& here = get_map();
    sfx::play_activity_sound( "tool", "pickaxe", sfx::get_heard_volume( here.abs_to_bub( target ) ) );
    if( calendar::once_every( 1_minutes ) ) {
        sounds::sound( here.abs_to_bub( target ), 30, sounds::sound_t::destructive_activity,
                       _( "CHNK!  CHNK!  CHNK!" ) );
    }
}

void pickaxe_activity_actor::finish( player_activity& act, Character& who )
{
    map& here = get_map();
    const tripoint_bub_ms pos( here.abs_to_bub( target ) );
    apply_mining_exertion( who, progress.get_moves_total() );
    act.set_to_null();
    who.add_msg_player_or_npc( m_good, _( "You finish digging." ), _( "<npcname> finishes digging." ) );
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
        for( item * &elem : here.i_at( pos ) ) { elem->set_var( "activity_var", who.name ); }
    }
}

void pickaxe_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.member( "tool", tool );
    jsout.end_object();
}

std::unique_ptr<activity_actor> pickaxe_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<pickaxe_activity_actor> actor(
        new pickaxe_activity_actor( tripoint_abs_ms::zero(), safe_reference<item>() ) );
    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "target", actor->target );
    data.read( "tool", actor->tool );
    return actor;
}

// ---- jackhammer_activity_actor ----

void jackhammer_activity_actor::start( player_activity &, Character& who )
{
    map& here = get_map();
    int moves = to_moves<int>( 10_minutes );
    moves += ( 24 - std::min( who.str_cur, 24 ) ) * to_moves<int>( 75_seconds );
    if( here.move_cost( here.abs_to_bub( target ) ) == 2 ) { moves /= 2; }
    const std::vector<npc *> helpers =
        character_funcs::get_crafting_helpers( static_cast<player &>( who ), 3 );
    moves = moves * ( 10 - static_cast<int>( helpers.size() ) ) / 10;
    progress.emplace( _( "Drilling" ), moves );
}

void jackhammer_activity_actor::do_turn( player_activity &, Character& who )
{
    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
    map& here = get_map();
    sfx::play_activity_sound( "tool", "jackhammer",
                              sfx::get_heard_volume( here.abs_to_bub( target ) ) );
    if( calendar::once_every( 1_minutes ) ) {
        sounds::sound( here.abs_to_bub( target ), 15, sounds::sound_t::destructive_activity,
                       _( "TATATATATATATAT!" ) );
    }
}

void jackhammer_activity_actor::finish( player_activity& act, Character& who )
{
    map& here = get_map();
    const auto& pos = here.abs_to_bub( target );

    if( here.has_flag_furn( TFLAG_MINEABLE, pos ) ) {
        here.destroy_furn( pos, true );
    } else {
        here.destroy( pos, true );
    }

    apply_mining_exertion( who, progress.get_moves_total() );
    who.add_msg_player_or_npc( m_good, _( "You finish drilling." ),
                               _( "<npcname> finishes drilling." ) );
    act.set_to_null();
    if( !tool ) {
        debugmsg( "jackhammer activity has no tool" );
    } else {
        who.consume_charges( *tool, tool->ammo_required() );
    }
    if( activity_handlers::resume_for_multi_activities( static_cast<player &>( who ) ) ) {
        for( item * &elem : here.i_at( pos ) ) { elem->set_var( "activity_var", who.name ); }
    }
}

void jackhammer_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.member( "tool", tool );
    jsout.end_object();
}

std::unique_ptr<activity_actor> jackhammer_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<jackhammer_activity_actor> actor(
        new jackhammer_activity_actor( tripoint_abs_ms::zero(), safe_reference<item>() ) );
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
    if( progress.front().complete() ) { progress.pop(); }
}

void churn_activity_actor::finish( player_activity& act, Character& who )
{
    map& here = get_map();
    who.add_msg_if_player( _( "You finish churning up the earth here." ) );
    here.ter_set( here.abs_to_bub( target ), t_dirtmound );
    act.set_to_null();
    activity_handlers::resume_for_multi_activities( static_cast<player &>( who ) );
}

void churn_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.end_object();
}

std::unique_ptr<activity_actor> churn_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<churn_activity_actor> actor( new churn_activity_actor( tripoint_abs_ms::zero() ) );
    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "target", actor->target );
    return actor;
}

// ---- fill_pit_activity_actor ----

void fill_pit_activity_actor::start( player_activity &, Character& who )
{
    map& here = get_map();
    ter_id ter = here.ter( here.abs_to_bub( target ) );
    int moves = to_moves<int>( time_duration::from_minutes( ter->fill_minutes ) );
    const std::vector<npc *> helpers =
        character_funcs::get_crafting_helpers( static_cast<player &>( who ), 3 );
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
        map& here = get_map();
        sounds::sound( here.abs_to_bub( target ), 10, sounds::sound_t::activity, _( "hsh!" ) );
    }
}

void fill_pit_activity_actor::finish( player_activity& act, Character& who )
{
    map& here = get_map();
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

void fill_pit_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.member( "tool", tool );
    jsout.end_object();
}

std::unique_ptr<activity_actor> fill_pit_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<fill_pit_activity_actor> actor(
        new fill_pit_activity_actor( tripoint_abs_ms::zero(), safe_reference<item>() ) );
    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "target", actor->target );
    data.read( "tool", actor->tool );
    return actor;
}

// ---- clear_rubble_activity_actor ----

void clear_rubble_activity_actor::start( player_activity &, Character& who )
{
    int moves = to_moves<int>( 30_seconds );
    int bonus = std::max( who.max_quality( quality_id( "DIG" ) ) - 1, 1 );
    const std::vector<npc *> helpers =
        character_funcs::get_crafting_helpers( static_cast<player &>( who ), 3 );
    moves = moves * ( 10 - static_cast<int>( helpers.size() ) ) / 10;
    progress.emplace( _( "Clearing rubble" ), moves / bonus );
}

void clear_rubble_activity_actor::do_turn( player_activity &, Character & )
{
    if( progress.front().complete() ) { progress.pop(); }
}

void clear_rubble_activity_actor::finish( player_activity& act, Character& who )
{
    map& here = get_map();
    const auto bub_pos = here.abs_to_bub( target );
    const map_bash_info& bash = here.furn( bub_pos ).obj().bash;
    who.add_msg_if_player( m_info, _( "You clear up the %s." ), here.furnname( bub_pos ) );
    here.spawn_items( bub_pos, item_group::items_from( bash.drop_group, calendar::turn ) );
    here.furn_set( bub_pos, f_null );
    act.set_to_null();
}

void clear_rubble_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.end_object();
}

std::unique_ptr<activity_actor> clear_rubble_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<clear_rubble_activity_actor> actor(
        new clear_rubble_activity_actor( tripoint_abs_ms::zero() ) );
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
    map& here = get_map();
    const auto bub_loc = here.abs_to_bub( target );
    sfx::play_activity_sound( "tool", "hammer", sfx::get_heard_volume( bub_loc ) );
}

void pry_nails_activity_actor::finish( player_activity& act, Character& who )
{
    map& here = get_map();
    const auto bub_loc = here.abs_to_bub( target );
    const ter_id type = here.ter( bub_loc );

    who.add_msg_if_player( _( "You pry out the nails from the terrain." ) );

    who.practice( skill_fabrication, 1, 1 );
    here.spawn_item( who.bub_pos(), itype_nail, 1, type->nail_pull_items[0] );
    here.spawn_item( who.bub_pos(), itype_2x4, type->nail_pull_items[1] );
    here.ter_set( bub_loc, type->nail_pull_result );
    act.set_to_null();
}

void pry_nails_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.end_object();
}

std::unique_ptr<activity_actor> pry_nails_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<pry_nails_activity_actor> actor(
        new pry_nails_activity_actor( tripoint_abs_ms::zero() ) );
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
    if( progress.front().complete() ) { progress.pop(); }
}

void plant_seed_activity_actor::finish( player_activity& act, Character& who )
{
    map& here = get_map();
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
        who.add_msg_player_or_npc(
            _( "You plant some %s." ), _( "<npcname> plants some %s." ), item::nname( seed_id ) );
    }
    act.set_to_null();
    activity_handlers::resume_for_multi_activities( static_cast<player &>( who ) );
}

void plant_seed_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.member( "seed_id", seed_id );
    jsout.end_object();
}

std::unique_ptr<activity_actor> plant_seed_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<plant_seed_activity_actor> actor(
        new plant_seed_activity_actor( tripoint_abs_ms::zero(), itype_id() ) );
    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "target", actor->target );
    data.read( "seed_id", actor->seed_id );
    return actor;
}

// ---- forage_activity_actor ----

void forage_activity_actor::start( player_activity& act, Character& who )
{
    act.auto_resume = auto_resume;
    int move_cost = 100000 / ( 2 * who.get_skill_level( skill_survival ) + 5 );
    move_cost /= rng( std::max( 4, who.per_cur ), 4 + who.per_cur * 2 );
    progress.emplace( _( "Foraging" ), move_cost );
}

void forage_activity_actor::do_turn( player_activity &, Character & )
{
    if( progress.front().complete() ) { progress.pop(); }
}

void forage_activity_actor::finish( player_activity& act, Character& who )
{
    // Don't forage if we aren't next to the bush - otherwise we get weird bugs
    bool next_to_bush = false;
    map& here = get_map();
    for( const auto& pnt : here.points_in_radius( who.bub_pos(), 1 ) ) {
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
        const std::vector<item *> dropped =
            here.put_items_from_loc( loc, who.bub_pos(), calendar::turn );
        for( item * it : dropped ) {
            add_msg( m_good, _( "You found: %s!" ), it->tname() );
            found_something = true;
            if( it->has_flag( flag_FORAGE_POISON ) && one_in( 10 ) ) {
                it->set_flag( flag_HIDDEN_POISON );
                it->poison = rng( 2, 7 );
            }
            if( it->has_flag( flag_FORAGE_HALLU ) && !it->has_flag( flag_HIDDEN_POISON )
                && one_in( 10 ) ) {
                it->set_flag( flag_HIDDEN_HALLU );
            }
        }
    }
    // 10% to drop a item/items from this group.
    if( one_in( 10 ) ) {
        const std::vector<item *> dropped =
            here.put_items_from_loc( item_group_id( "trash_forest" ), who.bub_pos(), calendar::turn );
        for( item * const& it : dropped ) {
            add_msg( m_good, _( "You found: %s!" ), it->tname() );
            found_something = true;
        }
    }

    if( !found_something ) { add_msg( _( "You didn't find anything." ) ); }

    iexamine::practice_survival_while_foraging( static_cast<player*>( &who ) );

    act.set_to_null();
}

void forage_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.member( "auto_resume", auto_resume );
    jsout.end_object();
}

std::unique_ptr<activity_actor> forage_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<forage_activity_actor> actor(
        new forage_activity_actor( tripoint_abs_ms::zero(), false ) );
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

void hand_crank_activity_actor::do_turn( player_activity& act, Character& who )
{
    // Hand-crank chargers seem to range from 2 watt (very common easily verified)
    // to 10 watt (suspicious claims from some manufacturers) sustained output.
    // It takes 2.4 minutes to produce 1kj at just slightly under 7 watts (25 kj per hour)
    // time-based instead of speed based because it's a sustained activity
    if( !tool ) {
        act.set_to_null();
        return;
    }
    auto& hand_crank_item = *tool;
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
    if( str_values.size() > ammo_type_index && !str_values[ammo_type_index].empty() ) {
        ammo_type = itype_id( str_values[ammo_type_index] );
    }
    if( str_values.size() > fully_charged_message_index
        && !str_values[fully_charged_message_index].empty() ) {
        fully_charged_message = str_values[fully_charged_message_index];
    }
    if( str_values.size() > exhausted_message_index
        && !str_values[exhausted_message_index].empty() ) {
        exhausted_message = str_values[exhausted_message_index];
    }
    if( charge_interval <= 0_turns ) { charge_interval = 144_seconds; }

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

void hand_crank_activity_actor::finish( player_activity &, Character & ) {}

void hand_crank_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "tool", tool );
    jsout.member( "values", values );
    jsout.member( "str_values", str_values );
    jsout.end_object();
}

std::unique_ptr<activity_actor> hand_crank_activity_actor::deserialize( JsonIn& jsin )
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

void fill_liquid_activity_actor::start( player_activity& act, Character & ) { parent_activity = &act; }

void fill_liquid_activity_actor::do_turn( player_activity& act, Character& who )
{
    map& here = get_map();
    try {
        // 1. Prepare source lambda
        liquid_source_type source_type = static_cast<liquid_source_type>( act.values.at( 0 ) );
        auto transfer =
            [source_type, &here,
        &act]( const std::function < detached_ptr<item>( detached_ptr<item>&& it ) > & cb ) {
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
                    item& base = vp->vehicle().part( act.values.at( 1 ) ).get_base();
                    if( base.contents.empty() ) { return true; }
                    item& source_it = base.contents.back();
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
                    finished = transfer( [&who, &vp]( detached_ptr<item>&& it ) {
                        return who.pour_into( vp->vehicle(), std::move( it ) );
                    } );
                } else {
                    throw std::runtime_error( "could not find target vehicle for liquid transfer" );
                }
                break;
            case LTT_MAP: {
                const auto bub_loc = here.abs_to_bub( act.coords.at( 1 ) );
                if( iexamine::has_keg( bub_loc ) ) {
                    finished = transfer( [&bub_loc]( detached_ptr<item>&& it ) {
                        return iexamine::pour_into_keg( bub_loc, std::move( it ) );
                    } );
                } else {
                    finished = transfer( [&who, &bub_loc, &here]( detached_ptr<item>&& it ) {
                        who.add_msg_if_player( _( "You pour %1$s onto the ground." ), it->tname() );
                        here.add_item_or_charges( bub_loc, std::move( it ) );
                        return detached_ptr<item>();
                    } );
                }
            }
            break;
            case LTT_MONSTER:
                // Do nothing here
                break;
            case LTT_CONTAINER:
                safe_reference<item> &container = act.targets.at( 0 );
                if( !container ) {
                    throw std::runtime_error( "could not find target container for liquid transfer" );
                }

                finished = transfer( [&who, &container]( detached_ptr<item>&& it ) {
                    return who.pour_into( *container, std::move( it ) );
                } );

                break;
        }
        if( finished ) { act.set_to_null(); }

    } catch( const std::runtime_error& err ) {
        debugmsg( "error in activity data: \"%s\"", err.what() );
        act.set_to_null();
        return;
    }
}

void fill_liquid_activity_actor::finish( player_activity &, Character & ) {}

void fill_liquid_activity_actor::serialize( JsonOut& jsout ) const
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

void fertilize_plot_activity_actor::start( player_activity& act, Character & )
{
    if( !str_value.empty() ) {
        act.str_values.clear();
        act.str_values.emplace_back( str_value );
    }
}

void fertilize_plot_activity_actor::do_turn( player_activity& act, Character& who )
{
    itype_id fertilizer;
    auto check_fertilizer = [&]( bool ask_user = true ) -> void {
        if( act.str_values.empty() ) { act.str_values.emplace_back( "" ); }
        fertilizer = itype_id( act.str_values[0] );

        if( ask_user && ( fertilizer.is_empty() || !who.has_charges( fertilizer, 1 ) ) )
        {
            fertilizer = iexamine::choose_fertilizer( static_cast<player &>( who ), "plant", false );
            act.str_values[0] = fertilizer.str();
        }
    };

    auto have_fertilizer = [&]() {
        return !fertilizer.is_empty() && who.has_charges( fertilizer, 1 );
    };

    const auto reject_tile = [&]( const tripoint_bub_ms & tile ) {
        check_fertilizer();
        ret_val<bool> can_fert =
            iexamine::can_fertilize( static_cast<player &>( who ), tile, fertilizer );
        return !can_fert.success();
    };

    const auto fertilize = [&]( player & p, const tripoint_bub_ms & tile ) {
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

    activity_handlers::perform_zone_activity_turn(
        static_cast<player *>( &who ), zone_type_FARM_PLOT, reject_tile, fertilize,
        _( "You fertilized every plot you could." ) );
}

void fertilize_plot_activity_actor::finish( player_activity &, Character & ) {}

void fertilize_plot_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "str_value", str_value );
    jsout.end_object();
}

std::unique_ptr<activity_actor> fertilize_plot_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<fertilize_plot_activity_actor> actor( new fertilize_plot_activity_actor() );
    JsonObject data = jsin.get_object();
    data.read( "str_value", actor->str_value );
    return actor;
}

std::unique_ptr<lockpick_activity_actor> lockpick_activity_actor::use_item(
    int moves_total, item& lockpick, const tripoint_abs_ms& target )
{
    return std::unique_ptr<lockpick_activity_actor>( new lockpick_activity_actor(
                moves_total, safe_reference<item>( lockpick ), detached_ptr<item>(), target ) );
}

std::unique_ptr<lockpick_activity_actor> lockpick_activity_actor::use_bionic(
    detached_ptr<item>&& fake_lockpick, const tripoint_abs_ms& target )
{
    return std::unique_ptr<lockpick_activity_actor>( new lockpick_activity_actor(
                to_moves<int>( 5_seconds ), safe_reference<item>(), std::move( fake_lockpick ), target ) );
}

void lockpick_activity_actor::start( player_activity & /*act*/, Character & )
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

void lockpick_activity_actor::do_turn( player_activity & /* act */, Character & )
{
    if( progress.front().complete() ) {
        progress.pop();
        return;
    }
}

void lockpick_activity_actor::finish( player_activity& act, Character& who )
{
    act.set_to_null();

    item* it = nullptr;
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

    /** @EFFECT_DEX improves chances of successfully picking door lock, reduces chances of bad
     * outcomes */
    /** @EFFECT_MECHANICS improves chances of successfully picking door lock, reduces chances of bad
     * outcomes */
    int pick_roll =
        5
        * ( std::pow( 1.3, who.get_skill_level( skill_mechanics ) ) + it->get_quality( qual_LOCKPICK )
            - it->damage() / 2000.0 )
        + who.dex_cur / 4.0;
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
            who.add_msg_if_player(
                m_bad,
                _( "The lock stumps your efforts to pick it, and you "
                   "destroy your tool." ) );
            destroy = true;
        } else {
            who.add_msg_if_player(
                m_bad,
                _( "The lock stumps your efforts to pick it, and you "
                   "damage your tool." ) );
        }
    } else {
        who.add_msg_if_player( m_bad, _( "The lock stumps your efforts to pick it." ) );
    }

    if( !perfect ) {
        // You don't gain much skill since the item does all the hard work for you
        xp_gain += std::pow( 2, who.get_skill_level( skill_mechanics ) ) + 1;
    }
    who.practice( skill_mechanics, xp_gain );

    if( !perfect && ( lock_roll + dice( 1, 30 ) ) > pick_roll ) {

        if( get_map().has_flag( "ALARMED", target ) ) {
            sounds::sound( who.bub_pos(), 40, sounds::sound_t::alarm, _( "an alarm sound!" ), true,
                           "environment", "alarm" );
            if( !g->timed_events.queued( TIMED_EVENT_WANTED ) ) {
                g->timed_events
                .add( TIMED_EVENT_WANTED, calendar::turn + 30_minutes, 0, who.abs_sm_pos() );
            }
        } else if( veh && veh->vehicle().has_security_working() ) {
            veh->vehicle().is_alarm_on = true;
        }
    }

    if( destroy && lockpick ) { lockpick->detach(); }
}

bool lockpick_activity_actor::is_pickable( const tripoint_bub_ms& p )
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

std::optional<tripoint_bub_ms> lockpick_activity_actor::select_location( avatar& you )
{
    if( you.is_mounted() ) {
        you.add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return std::nullopt;
    }

    const std::optional<tripoint_bub_ms> target = choose_adjacent_highlight(
            _( "Use your lockpick where?" ), _( "There is nothing to lockpick nearby." ), is_pickable,
            false );
    if( !target ) { return std::nullopt; }

    if( is_pickable( *target ) ) { return *target; }

    const ter_id terr_type = get_map().ter( *target );
    if( *target == you.bub_pos() ) {
        you.add_msg_if_player( m_info, _( "You pick your nose and your sinuses swing open." ) );
    } else if( g->critter_at<npc>( *target ) ) {
        you.add_msg_if_player(
            m_info,
            _( "You can pick your friends, and you can pick your nose, "
               "but you can't pick your friend's nose." ) );
    } else if( !terr_type->open.is_null() ) {
        you.add_msg_if_player( m_info, _( "That door isn't locked." ) );
    } else {
        you.add_msg_if_player( m_info, _( "That cannot be picked." ) );
    }
    return std::nullopt;
}

void lockpick_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();

    jsout.member( "progress", progress );
    jsout.member( "moves_total", moves_total );
    jsout.member( "lockpick", lockpick );
    jsout.member( "fake_lockpick", fake_lockpick );
    jsout.member( "target", target );

    jsout.end_object();
}

std::unique_ptr<activity_actor> lockpick_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<lockpick_activity_actor> actor( new lockpick_activity_actor(
                0, safe_reference<item>(), detached_ptr<item>(), tripoint_abs_ms::zero() ) );

    JsonObject data = jsin.get_object();

    data.read( "progress", actor->progress );
    data.read( "moves_total", actor->moves_total );
    data.read( "lockpick", actor->lockpick );
    data.read( "fake_lockpick", actor->fake_lockpick );
    data.read( "target", actor->target );

    return actor;
}

void oxytorch_activity_actor::start( player_activity& act, Character & /*who*/ )
{
    const map& here = get_map();

    if( here.has_furn( target ) ) {
        const furn_id furn_type = here.furn( target );
        if( !furn_type->oxytorch->valid() ) {
            if( !testing ) { debugmsg( "%s oxytorch is invalid", furn_type.id().str() ); }
            act.set_to_null();
            return;
        }
        progress.emplace( furn_type->name(), to_moves<int>( furn_type->oxytorch->duration() ) );
    } else if( !here.ter( target )->is_null() ) {
        const ter_id ter_type = here.ter( target );
        if( !ter_type->oxytorch->valid() ) {
            if( !testing ) { debugmsg( "%s oxytorch is invalid", ter_type.id().str() ); }
            act.set_to_null();
            return;
        }
        progress.emplace( ter_type->name(), to_moves<int>( ter_type->oxytorch->duration() ) );
    } else {
        if( !testing ) { debugmsg( "oxytorch activity called on invalid terrain" ); }
        act.set_to_null();
        return;
    }
}

void oxytorch_activity_actor::do_turn( player_activity & /*act*/, Character& who )
{
    // We check available charges when first starting the cut, but this prevents abnormal behavior
    // if torch status changes mid-activity.
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
                add_msg( _( "%1$s %2$s ran out of charges." ), who.disp_name( false, true ),
                         tool->tname() );
            }
        }
        who.cancel_activity();
    }
    if( progress.front().complete() ) { progress.pop(); }
}

void oxytorch_activity_actor::finish( player_activity& act, Character& who )
{
    map& here = get_map();
    const activity_data_common* data;

    if( here.has_furn( target ) ) {
        const furn_id furn_type = here.furn( target );
        if( !furn_type->oxytorch->valid() ) {
            if( !testing ) { debugmsg( "%s oxytorch is invalid", furn_type.id().str() ); }
            act.set_to_null();
            return;
        }

        const furn_str_id new_furn = furn_type->oxytorch->result();
        if( !new_furn.is_valid() ) {
            if( !testing ) { debugmsg( "oxytorch furniture: %s invalid furniture", new_furn.str() ); }
            act.set_to_null();
            return;
        }

        data = static_cast<const activity_data_common *>( &*furn_type->oxytorch );
        here.furn_set( target, new_furn );
    } else if( !here.ter( target )->is_null() ) {
        const ter_id ter_type = here.ter( target );
        if( !ter_type->oxytorch->valid() ) {
            if( !testing ) { debugmsg( "%s oxytorch is invalid", ter_type.id().str() ); }
            act.set_to_null();
            return;
        }

        const ter_str_id new_ter = ter_type->oxytorch->result();
        if( !new_ter.is_valid() ) {
            if( !testing ) { debugmsg( "oxytorch terrain: %s invalid terrain", new_ter.str() ); }
            act.set_to_null();
            return;
        }

        data = static_cast<const activity_data_common *>( &*ter_type->oxytorch );
        here.ter_set( target, new_ter );
    } else {
        if( !testing ) { debugmsg( "oxytorch activity finished on invalid terrain" ); }
        act.set_to_null();
        return;
    }

    for( const activity_byproduct& byproduct : data->byproducts() ) {
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

    if( !data->message().empty() ) { who.add_msg_if_player( m_info, data->message().translated() ); }

    act.set_to_null();
}

void oxytorch_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.member( "tool", tool );
    jsout.end_object();
}

std::unique_ptr<activity_actor> oxytorch_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<oxytorch_activity_actor> actor(
        new oxytorch_activity_actor( tripoint_bub_ms::zero(), safe_reference<item>() ) );
    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "target", actor->target );
    data.read( "tool", actor->tool );
    return actor;
}
