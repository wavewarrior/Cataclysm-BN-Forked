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


void train_activity_actor::finish( player_activity& act, Character& who )
{
    player& p = static_cast<player &>( who );
    const skill_id sk( name );
    if( sk.is_valid() ) {
        const Skill& skill = sk.obj();
        std::string skill_name = skill.name();
        int old_skill_level = p.get_skill_level( sk );
        p.get_skill_level_object( sk ).train( 100 * ( old_skill_level + 1 ), true );
        int new_skill_level = p.get_skill_level( sk );
        if( old_skill_level != new_skill_level ) {
            p.add_msg_if_player(
                m_good, _( "You finish training %s to level %d." ), skill_name, new_skill_level );
            g->events().send<event_type::gains_skill_level>( p.getID(), sk, new_skill_level );
        } else {
            p.add_msg_if_player( m_good, _( "You get some training in %s." ), skill_name );
        }
        act.set_to_null();
        return;
    }

    const matype_id& ma_id = matype_id( name );
    if( ma_id.is_valid() ) {
        const martialart& mastyle = ma_id.obj();
        g->events().send<event_type::learns_martial_art>( p.getID(), ma_id );
        p.martial_arts_data->learn_style( mastyle.id, p.is_avatar() );
    } else {
        // Spell training
        const spell_id& sp_id = spell_id( name );
        if( sp_id.is_valid() ) {
            const bool knows = g->u.magic->knows_spell( sp_id );
            if( knows ) {
                spell& studying = p.magic->get_spell( sp_id );
                const int xp = roll_remainder( studying.exp_modifier( p ) * expert_multiplier );
                studying.gain_exp( xp );
                p.add_msg_if_player(
                    m_good, _( "You learn a little about the spell: %s" ), sp_id->name );
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

void train_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "name", name );
    jsout.member( "expert_multiplier", expert_multiplier );
    jsout.end_object();
}

std::unique_ptr<activity_actor> train_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<train_activity_actor> actor( new train_activity_actor( std::string(), 0 ) );
    JsonObject data = jsin.get_object();
    data.read( "name", actor->name );
    data.read( "expert_multiplier", actor->expert_multiplier );
    return actor;
}

// ---- train_skill_activity_actor ----

void train_skill_activity_actor::do_turn( player_activity& act, Character& who )
{
    player& p = static_cast<player &>( who );

    item* main_tool = nullptr;
    tripoint_bub_ms hack_pos_bub = tripoint_bub_ms{};
    int hack_original_charges = 0;

    if( hack_type == hack_type_t::furniture ) {
        // Actor-set hack (from iexamine.cpp)
        const map& m = get_map();
        hack_pos_bub = m.abs_to_bub( hack_position );
        if( m.has_furn( hack_pos_bub ) ) {
            const furn_t &furniture = m.furn( hack_pos_bub ).obj();
            const std::vector<itype> item_type_list = furniture.crafting_pseudo_item_types();
            for( const itype& item_type : item_type_list ) {
                if( item_type.get_id() == hack_tool_type_id ) {
                    const tripoint_abs_ms abspos = m.bub_to_abs( hack_pos_bub );
                    const distribution_grid& grid = get_distribution_grid_tracker().grid_at( abspos );
                    main_tool = item::spawn_temporary( item_type.get_id(), calendar::turn, 0 );
                    main_tool->charges = grid.get_resource( true );
                    main_tool->set_flag( flag_PSEUDO );
                    hack_original_charges = main_tool->charges;
                    break;
                }
            }
        }
    } else {
        if( !act.get_tools().empty() ) { main_tool = &*act.get_tools().front(); }
    }

    if( main_tool == nullptr ) {
        debugmsg(
            "train skill tools array and hack values are empty. this would have caused "
            "invalid safe reference error" );
        act.set_to_null();
        return;
    }
    item& skill_training_item = *main_tool;
    int training_skill_interval = atoi( p.get_value( "training_iuse_skill_interval" ).c_str() );

    if( training_skill_interval <= 0 ) {
        debugmsg( "training_iuse_skill_interval is invalid ( %d )", training_skill_interval );
        act.set_to_null();
        return;
    }

    if( calendar::once_every( 1_minutes * training_skill_interval ) ) {
        std::string training_skill = p.get_value( "training_iuse_skill" );
        if( training_skill.empty() ) {
            debugmsg( "training_iuse_skill is empty" );
            act.set_to_null();
            return;
        }
        int training_skill_xp = atoi( p.get_value( "training_iuse_skill_xp" ).c_str() );
        int training_skill_max_level = atoi(
                                           p.get_value( "training_iuse_skill_xp_max_level" ).c_str() );
        int training_skill_xp_chance = atoi( p.get_value( "training_iuse_skill_xp_chance" ).c_str() );
        int training_skill_fatigue = atoi( p.get_value( "training_iuse_skill_fatigue" ).c_str() );

        p.mod_fatigue( training_skill_fatigue );
        if( skill_training_item.ammo_remaining() > 0 ) {
            skill_training_item.ammo_consume( 1, p.bub_pos() );
            if( hack_type == hack_type_t::furniture ) {
                const int used_charges = hack_original_charges - skill_training_item.charges;
                if( used_charges > 0 ) {
                    const tripoint_abs_ms abspos = get_map().bub_to_abs( hack_pos_bub );
                    distribution_grid& grid = get_distribution_grid_tracker().grid_at( abspos );
                    grid.mod_resource( -used_charges );
                }
            }
        } else if( skill_training_item.ammo_required() > 0 ) {
            act.set_to_null();
            add_msg( m_info, _( "The %s runs out of power." ), skill_training_item.tname() );
            return;
        }
        if( p.get_skill_level( skill_id( training_skill ) ) >= training_skill_max_level ) {
            act.set_to_null();
            add_msg( m_info, _( "You can no longer learn anything from this." ) );
            return;
        }
        if( rng( 1, 100 ) < training_skill_xp_chance ) {
            p.practice( skill_id( training_skill ), training_skill_xp, training_skill_max_level );
        }
    }

    // needs rest
    if( p.get_fatigue() >= fatigue_levels::dead_tired ) {
        if( hack_type == hack_type_t::furniture ) {
            const int used_charges = hack_original_charges - skill_training_item.charges;
            if( used_charges > 0 ) {
                const tripoint_abs_ms abspos = get_map().bub_to_abs( hack_pos_bub );
                distribution_grid& grid = get_distribution_grid_tracker().grid_at( abspos );
                grid.mod_resource( -used_charges );
            }
        }
        act.set_to_null();
        add_msg( m_info, _( "You're too tired to continue." ) );
    }
}

void train_skill_activity_actor::finish( player_activity& act, Character& who )
{
    player& p = static_cast<player &>( who );
    p.add_msg_if_player( m_good, _( "You feel like you've learned a little bit." ) );
    act.set_to_null();
}

void train_skill_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "hack_type", static_cast<int>( hack_type ) );
    jsout.member( "hack_position", hack_position );
    jsout.member( "hack_tool_type_id", hack_tool_type_id );
    jsout.end_object();
}

std::unique_ptr<activity_actor> train_skill_activity_actor::deserialize( JsonIn& jsin )
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

void mind_splicer_activity_actor::finish( player_activity& act, Character& who )
{
    Character& p = who;
    act.set_to_null();

    item* data_card_item = data_card.get();
    if( data_card_item == nullptr ) {
        debugmsg( "Incompatible arguments to: mind_splicer_activity_actor::finish" );
        return;
    }
    p.add_msg_if_player( m_info, _( "…you finally find the memory banks." ) );
    p.add_msg_if_player( m_info, _( "The kit makes a copy of the data inside the bionic." ) );
    data_card_item->contents.clear_items();
    data_card_item->put_in( item::spawn( itype_id( "mind_scan_robofac" ) ) );
}

void mind_splicer_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "data_card", data_card );
    jsout.member( "moves", moves );
    jsout.end_object();
}

std::unique_ptr<activity_actor> mind_splicer_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<mind_splicer_activity_actor> actor( new mind_splicer_activity_actor() );
    JsonObject data = jsin.get_object();
    data.read( "data_card", actor->data_card );
    data.read( "moves", actor->moves );
    return actor;
}
// ---- robot_control_activity_actor ----

void robot_control_activity_actor::do_turn( player_activity& act, Character& who )
{
    Character& p = who;
    const shared_ptr_fast<monster> z = target.lock();

    if( z == nullptr || !iuse::robotcontrol_can_target( &static_cast<player &>( p ), *z ) ) {
        p.add_msg_if_player( _( "Target lost.  IFF override failed." ) );
        act.set_to_null();
        return;
    }
    // TODO: Add some kind of chance of getting the target's attention
}

void robot_control_activity_actor::finish( player_activity& act, Character& who )
{
    act.set_to_null();
    Character& p = who;
    player& pl = static_cast<player &>( p );

    shared_ptr_fast<monster> z = target.lock();
    if( z == nullptr || !iuse::robotcontrol_can_target( &pl, *z ) ) {
        p.add_msg_if_player( _( "Target lost.  IFF override failed." ) );
        return;
    }

    p.add_msg_if_player( _( "You unleash your override attack on the %s." ), target_name );

    const int computer_skill = pl.get_skill_level( skill_id( "computer" ) );
    const float randomized_skill = rng( 2, pl.int_cur ) + computer_skill;
    float success = computer_skill - 3 * z->type->difficulty / randomized_skill;
    if( z->has_flag( MF_RIDEABLE_MECH ) ) { success = randomized_skill - rng( 1, 11 ); }
    if( success >= 0 ) {
        p.add_msg_if_player( _( "You successfully override the %s's IFF protocols!" ), target_name );
        z->friendly = -1;
        if( z->has_flag( MF_RIDEABLE_MECH ) ) { z->add_effect( efftype_id( "pet" ), 1_turns ); }
    } else if( success >= -2 ) {
        p.add_msg_if_player( _( "The %s short circuits as you attempt to reprogram it!" ), target_name );
        z->apply_damage( &pl, bodypart_id( "torso" ), rng( 1, 10 ) );
        if( z->is_dead() ) {
            pl.practice( skill_id( "computer" ), 10 );
            return;
        }
        if( one_in( 3 ) ) {
            p.add_msg_if_player( _( "…and turns friendly!" ) );
            if( one_in( 3 ) ) {
                z->friendly = -1;
            } else {
                z->friendly = rng( 5, 40 );
            }
        }
    } else {
        p.add_msg_if_player( _( "…but the robot refuses to acknowledge you as an ally!" ) );
    }
    pl.practice( skill_computer, 10 );
}

void robot_control_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "target_name", target_name );
    jsout.member( "moves", moves );
    jsout.end_object();
}

std::unique_ptr<activity_actor> robot_control_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<robot_control_activity_actor> actor( new robot_control_activity_actor() );
    JsonObject data = jsin.get_object();
    data.read( "target_name", actor->target_name );
    data.read( "moves", actor->moves );
    return actor;
}
// ---- study_spell_activity_actor ----

void study_spell_activity_actor::do_turn( player_activity& act, Character& who )
{
    player& p = static_cast<player &>( who );
    if( !character_funcs::can_see_fine_details( p ) ) {
        dark_flag = -1;
        progress.mod_moves_left( -progress.get_moves_left() );
        return;
    }
    if( mode == "study" ) {
        spell& studying = p.magic->get_spell( spell_id( spell_name ) );
        const int old_level = studying.get_level();
        const int xp = roll_remainder( studying.exp_modifier( p ) / to_turns<float>( 6_seconds ) );

        total_exp_gained += xp;
        studying.gain_exp( xp );

        if( turn_counter % 600 == 599 ) {
            p.add_msg_if_player(
                m_good, _( "You gained %i experience in %s" ), total_exp_gained - last_exp_displayed,
                studying.name() );
            last_exp_displayed = total_exp_gained;
        }

        const int new_level = studying.get_level();

        if( new_level > old_level ) {
            total_levels_gained += new_level - old_level;
            g->events().send<event_type::player_levels_spell>( studying.id(), new_level );
            if( gain_level_mode ) { progress.mod_moves_left( -progress.get_moves_left() ); }
        } else if( gain_level_mode ) {
            progress.mod_moves_left( 1000000 - progress.get_moves_left() );
        }
    }
    turn_counter += 1;
}

void study_spell_activity_actor::finish( player_activity& act, Character& who )
{
    act.set_to_null();
    player& p = static_cast<player &>( who );

    if( mode == "study" ) {
        std::string level_string;
        if( total_levels_gained > 0 ) {
            level_string = string_format(
                               vgettext( " and %d level", " and %d levels", total_levels_gained ),
                               total_levels_gained );
        }
        p.add_msg_if_player(
            m_good, _( "You gained %i experience%s from your study session." ), total_exp_gained,
            level_string );
        const spell& sp = p.magic->get_spell( spell_id( spell_name ) );
        p.practice( sp.skill(), total_exp_gained, sp.get_difficulty() );
    } else if( mode == "learn" && dark_flag == 0 ) {
        p.magic->learn_spell( spell_name, p );
    }
    if( dark_flag == -1 ) { p.add_msg_if_player( m_bad, _( "It's too dark to read." ) ); }
}

void study_spell_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "spell_name", spell_name );
    jsout.member( "mode", mode );
    jsout.member( "gain_level_mode", gain_level_mode );
    jsout.member( "total_exp_gained", total_exp_gained );
    jsout.member( "total_levels_gained", total_levels_gained );
    jsout.member( "dark_flag", dark_flag );
    jsout.member( "turn_counter", turn_counter );
    jsout.member( "last_exp_displayed", last_exp_displayed );
    jsout.member( "moves", moves );
    jsout.end_object();
}

std::unique_ptr<activity_actor> study_spell_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<study_spell_activity_actor> actor( new study_spell_activity_actor() );
    JsonObject data = jsin.get_object();
    data.read( "spell_name", actor->spell_name );
    data.read( "mode", actor->mode );
    data.read( "gain_level_mode", actor->gain_level_mode );
    data.read( "total_exp_gained", actor->total_exp_gained );
    data.read( "total_levels_gained", actor->total_levels_gained );
    data.read( "dark_flag", actor->dark_flag );
    data.read( "turn_counter", actor->turn_counter );
    data.read( "last_exp_displayed", actor->last_exp_displayed );
    data.read( "moves", actor->moves );
    return actor;
}
namespace
{
/// Blood magic cost handler for hp_energy spells
void blood_magic( player* p, int cost )
{
    std::vector<uilist_entry> uile;
    std::vector<bodypart_id> parts;
    int i = 0;
    for( const bodypart_id& bp : p->get_all_body_parts( true ) ) {
        const int hp_cur = p->get_part_hp_cur( bp );
        uilist_entry entry( i, hp_cur > cost, i + 49, body_part_hp_bar_ui_text( bp ) );

        const std::pair<std::string, nc_color> &hp = get_hp_bar( hp_cur, p->get_part_hp_max( bp ) );
        entry.ctxt = colorize( hp.first, hp.second );
        uile.emplace_back( entry );
        parts.push_back( bp );
        i++;
    }
    int action = -1;
    while( action < 0 ) { action = uilist( _( "Choose part\nto draw blood from." ), uile ); }
    p->mod_part_hp_cur( parts[action], -cost );
    p->mod_pain( std::max( 1, cost / 3 ) );
}
} // namespace

// ---- spellcasting_activity_actor ----

// spellcasting_activity_actor::do_turn is a no-op; original do_turn was dead code
void spellcasting_activity_actor::do_turn( player_activity &, Character & ) {}

void spellcasting_activity_actor::finish( player_activity& act, Character& who )
{
    act.set_to_null();
    player& p = static_cast<player &>( who );

    spell_id sp( spell_name );
    spell temp_spell( sp );
    spell& spell_being_cast = ( level_override == -1 ) ? p.magic->get_spell( sp ) : temp_spell;

    if( level_override != -1 ) {
        while( spell_being_cast.get_level() < level_override && !spell_being_cast.is_max_level() ) {
            spell_being_cast.gain_level();
        }
    }

    const auto target = p.bub_pos();
    bool target_is_valid = false;
    if( spell_being_cast.range() > 0 && !spell_being_cast.is_valid_target( target_none )
        && !spell_being_cast.has_flag( RANDOM_TARGET ) ) {
        do {
            avatar& you = *p.as_avatar();
            std::vector<tripoint_bub_ms> trajectory =
                target_handler::mode_spell( you, spell_being_cast, no_fail, no_mana );

            if( !trajectory.empty() ) {
                const auto traj_target = trajectory.back();
                target_is_valid = spell_being_cast.is_valid_target( p, traj_target );
                if( !( spell_being_cast.is_valid_target( target_ground ) || p.sees( traj_target ) ) ) {
                    target_is_valid = false;
                }
            } else {
                target_is_valid = false;
            }
            if( !target_is_valid ) {
                if( query_yn( _( "Stop casting spell?  Time spent will be lost." ) ) ) { return; }
            }
        } while( !target_is_valid );
    } else if( spell_being_cast.has_flag( RANDOM_TARGET ) ) {
        const std::optional<tripoint_bub_ms> target_ =
            spell_being_cast.random_valid_target( p, p.bub_pos() );
        if( !target_ ) {
            p.add_msg_if_player(
                game_message_params{m_bad, gmf_bypass_cooldown},
                _( "Your spell "
                   "can't find a "
                   "suitable "
                   "target." ) );
            return;
        }
    }

    bool success = no_fail || rng_float( 0.0f, 1.0f ) >= spell_being_cast.spell_fail( p );
    int exp_gained = spell_being_cast.casting_exp( p );
    if( !success ) {
        p.add_msg_if_player(
            game_message_params{m_bad, gmf_bypass_cooldown},
            _( "You lose your "
               "concentration!" ) );
        if( !spell_being_cast.is_max_level() && level_override == -1 ) {
            spell_being_cast.gain_exp( exp_gained / 5 );
            p.add_msg_if_player(
                m_good, _( "You gain %i experience.  New total %i." ), exp_gained / 5,
                spell_being_cast.xp() );
        }
        return;
    }

    if( spell_being_cast.has_flag( spell_flag::VERBAL ) ) {
        sounds::sound( p.bub_pos(), p.get_shout_volume() / 2, sounds::sound_t::speech,
                       _( "cast a spell" ), false );
    }

    p.add_msg_if_player( spell_being_cast.message(), spell_being_cast.name() );

    spell_being_cast.cast_all_effects( p, target );

    if( no_mana ) {
        int cost = spell_being_cast.energy_cost( p );
        switch( spell_being_cast.energy_source() ) {
            case mana_energy:
                p.magic->mod_mana( p, -cost );
                break;
            case stamina_energy:
                p.mod_stamina( -cost, spell_being_cast.has_flag( spell_flag::PHYSICAL ) );
                break;
            case bionic_energy:
                p.mod_power_level( -units::from_kilojoule( cost ) );
                break;
            case hp_energy:
                blood_magic( &p, cost );
                break;
            case fatigue_energy:
                p.mod_fatigue( cost );
                break;
            case none_energy:
            default:
                break;
        }
        spell_being_cast.use_components( p );
    }
    if( level_override == -1 ) {
        if( !spell_being_cast.is_max_level() ) {
            int old_level = spell_being_cast.get_level();
            if( old_level == 0 ) {
                spell_being_cast.gain_level();
                p.add_msg_if_player(
                    m_good,
                    _( "Something about how this spell works just clicked!  "
                       "You gained a level!" ) );
            } else {
                spell_being_cast.gain_exp( exp_gained );
                p.add_msg_if_player(
                    m_good, _( "You gain %i experience.  New total %i." ), exp_gained,
                    spell_being_cast.xp() );
            }
            if( spell_being_cast.get_level() != old_level ) {
                g->events().send<event_type::player_levels_spell>(
                    spell_being_cast.id(), spell_being_cast.get_level() );
            }
        }
    }
    if( !act.targets.empty() && act.targets.front() ) {
        item& it = *act.targets.front();
        if( !it.has_flag( flag_USE_PLAYER_ENERGY ) ) {
            p.consume_charges( it, it.type->charges_to_use() );
        }
    }
}

void spellcasting_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "spell_name", spell_name );
    jsout.member( "level_override", level_override );
    jsout.member( "no_fail", no_fail );
    jsout.member( "no_mana", no_mana );
    jsout.member( "moves", moves );
    jsout.end_object();
}

std::unique_ptr<activity_actor> spellcasting_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<spellcasting_activity_actor> actor( new spellcasting_activity_actor() );
    JsonObject data = jsin.get_object();
    data.read( "spell_name", actor->spell_name );
    data.read( "level_override", actor->level_override );
    data.read( "no_fail", actor->no_fail );
    data.read( "no_mana", actor->no_mana );
    data.read( "moves", actor->moves );
    return actor;
}
// ---- pulp_activity_actor ----

void pulp_activity_actor::do_turn( player_activity& act, Character& who )
{
    player& p = static_cast<player &>( who );
    map& here = get_map();
    const auto& pos = here.abs_to_bub( placement );

    const auto cut_power = std::
                           max( p.primary_weapon().damage_melee( DT_CUT ), p.primary_weapon().damage_melee( DT_STAB ) / 2 );

    const auto pulp_effort = std::max( 0, p.str_cur + p.primary_weapon().damage_melee( DT_BASH ) );
    auto pulp_power = std::sqrt( pulp_effort * std::max( 0.0f, cut_power + 1.0f ) );
    pulp_power *= 40 + p.get_skill_level( skill_survival ) * 5;

    if( pulp_power <= 0.0f || !std::isfinite( pulp_power ) ) {
        p.add_msg_player_or_npc(
            m_bad, _( "You are unable to pulp the corpse." ),
            _( "<npcname> is unable to pulp the corpse." ) );
        finish( act, who );
        return;
    }

    const auto mess_radius = p.primary_weapon().has_flag( flag_MESSY ) ? 2 : 1;

    int &num_corpses = act.index;
    map_stack corpse_pile = here.i_at( pos );
    for( item * &corpse : corpse_pile ) {
        const mtype* corpse_mtype = corpse->get_mtype();
        if( !corpse->is_corpse()
            || ( !corpse_mtype->has_flag( MF_REVIVES ) && !corpse_mtype->zombify_into )
            || ( !str_value.empty() && str_value == "auto_pulp_no_acid"
                 && corpse_mtype->bloodType().obj().has_acid ) ) {
            continue;
        }

        while( corpse->damage() < corpse->max_damage() ) {
            if( x_in_y( pulp_power, corpse->volume() / units::legacy_volume_factor ) ) {
                corpse->inc_damage( DT_BASH );
                if( corpse->damage() == corpse->max_damage() ) { num_corpses++; }
            }

            if( x_in_y( pulp_power, corpse->volume() / units::legacy_volume_factor ) ) {
                const int radius = mess_radius + x_in_y( pulp_power, 500 ) + x_in_y( pulp_power, 1000 );
                const tripoint_bub_ms dest( pos + point( rng( -radius, radius ), rng( -radius, radius ) ) );
                const field_type_id type_blood =
                    ( mess_radius > 1 && x_in_y( pulp_power, 10000 ) )
                    ? corpse->get_mtype()->gibType()
                    : corpse->get_mtype()->bloodType();
                here.add_splatter_trail( type_blood, pos, dest );
            }

            if( x_in_y( pulp_power, corpse->volume() / units::legacy_volume_factor ) ) {
                here.add_splatter_trail(
                    corpse->get_mtype()->gibType(), pos,
                    pos + point( rng( -mess_radius, mess_radius ), rng( -mess_radius, mess_radius ) ) );
            }

            p.mod_moves( -100 );
            if( p.moves <= 0 ) { break; }
        }
        corpse->set_flag( flag_PULPED );
    }
    if( num_corpses == 0 ) {
        p.add_msg_if_player( m_bad, _( "The corpse moved before you could finish smashing it!" ) );
    } else {
        p.add_msg_player_or_npc(
            vgettext( "The corpse is thoroughly pulped.", "The corpses are thoroughly pulped.",
                      num_corpses ),
            vgettext( "<npcname> finished pulping the corpse.",
                      "<npcname> finished pulping the corpses.", num_corpses ) );
    }
    // Pulp is a "special" activity — the framework's moves block and complete()
    // are skipped.  Terminate directly; finish() handles NPC cleanup.
    finish( act, who );
}

void pulp_activity_actor::finish( player_activity& act, Character& who )
{
    if( who.is_npc() ) {
        npc* guy = dynamic_cast<npc *>( &who );
        if( guy ) { guy->revert_after_activity(); }
    } else {
        act.set_to_null();
    }
}

void pulp_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "placement", placement );
    jsout.member( "num_corpses", num_corpses );
    jsout.member( "str_value", str_value );
    jsout.end_object();
}

std::unique_ptr<activity_actor> pulp_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<pulp_activity_actor> actor(
        new pulp_activity_actor( tripoint_abs_ms::zero(), std::string() ) );
    JsonObject data = jsin.get_object();
    data.read( "placement", actor->placement );
    data.read( "num_corpses", actor->num_corpses );
    data.read( "str_value", actor->str_value );
    return actor;
}
// ---- hotwire_car_activity_actor ----
void hotwire_car_activity_actor::do_turn( player_activity & /*act*/, Character & /*who*/ ) {}


void hotwire_car_activity_actor::finish( player_activity& act, Character& who )
{
    act.set_to_null();
    player& p = static_cast<player &>( who );
    if( const optional_vpart_position vp = g->m.veh_at(
            tripoint_abs_ms( veh_x, veh_y, p.bub_pos().z() ) ) ) {
        vehicle* const veh = &vp->vehicle();
        if( mech_skill > rng( 1, 6 ) ) {
            veh->is_locked = false;
            add_msg( _( "This wire will start the engine." ) );
        } else if( mech_skill > rng( 0, 4 ) ) {
            veh->is_locked = false;
            veh->is_alarm_on = veh->has_security_working();
            add_msg( _( "This wire will probably start the engine." ) );
        } else if( veh->is_alarm_on ) {
            veh->is_locked = false;
            add_msg( _( "By process of elimination, this wire will start the engine." ) );
        } else {
            veh->is_alarm_on = veh->has_security_working();
            add_msg( _( "The red wire always starts the engine, doesn't it?" ) );
        }
    } else {
        debugmsg( "process_activity ACT_HOTWIRE_CAR: vehicle not found" );
    }
}

void hotwire_car_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.member( "veh_x", veh_x );
    jsout.member( "veh_y", veh_y );
    jsout.member( "mech_skill", mech_skill );
    jsout.member( "moves", moves );
}

std::unique_ptr<activity_actor> hotwire_car_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<hotwire_car_activity_actor> actor( new hotwire_car_activity_actor() );
    JsonObject data = jsin.get_object();
    data.read( "veh_x", actor->veh_x );
    data.read( "veh_y", actor->veh_y );
    data.read( "mech_skill", actor->mech_skill );
    data.read( "moves", actor->moves );
    return actor;
}
// ---- start_engines_activity_actor ----
void start_engines_activity_actor::do_turn( player_activity & /*act*/, Character & /*who*/ ) {}


void start_engines_activity_actor::finish( player_activity& act, Character& who )
{
    act.set_to_null();
    player& p = static_cast<player &>( who );
    vehicle* veh = g->remoteveh();
    map& here = get_map();
    if( !veh ) {
        veh = veh_pointer_or_null( here.veh_at( placement ) );
        if( !veh ) { return; }
    }

    int attempted = 0;
    int started = 0;
    int non_combustion_started = 0;

    for( size_t e = 0; e < veh->engines.size(); ++e ) {
        if( veh->is_engine_on( e ) ) {
            attempted++;
            if( veh->start_engine( e ) ) {
                started++;
                if( !veh->is_engine_type( e, itype_muscle )
                    && !veh->is_engine_type( e, itype_animal ) ) {
                    non_combustion_started++;
                }
            }
        }
    }

    if( started == 0 ) {
        if( attempted == 0 ) {
            add_msg( _( "No engines are running." ) );
        } else {
            add_msg( _( "None of the engines started." ) );
        }
    } else if( started == 1 ) {
        if( non_combustion_started == 1 ) {
            add_msg( _( "The engine started." ) );
        } else {
            add_msg( _( "One engine started." ) );
        }
    } else {
        if( non_combustion_started == started ) {
            add_msg( _( "The engines started." ) );
        } else {
            add_msg( _( "%d engines started." ), started );
        }
    }

    if( take_control && !veh->engine_on && !veh->velocity ) {
        p.controlling_vehicle = true;
        add_msg( _( "You take control of the %s." ), veh->name );
    }
}

void start_engines_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.member( "placement", placement );
    jsout.member( "take_control", take_control );
    jsout.member( "moves", moves );
}

std::unique_ptr<activity_actor> start_engines_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<start_engines_activity_actor> actor( new start_engines_activity_actor() );
    JsonObject data = jsin.get_object();
    data.read( "placement", actor->placement );
    data.read( "take_control", actor->take_control );
    data.read( "moves", actor->moves );
    return actor;
}

// ---- vehicle_activity_actor ----
void vehicle_activity_actor::start( player_activity& act, Character & )
{
    progress.emplace( _( "Vehicle work" ), moves );
    act.placement = placement;
    act.index = cmd;
    act.values.push_back( placement.x() );
    act.values.push_back( placement.y() );
    act.values.push_back( placement.z() );
    act.values.push_back( cursor_pos.x() );
    act.values.push_back( cursor_pos.y() );
    act.values.push_back( cursor_pos.z() );
    act.values.push_back( vehicle_part );
    act.str_values.push_back( part_id.str() );
}

void vehicle_activity_actor::do_turn( player_activity & /*act*/, Character & /*who*/ ) {}

void vehicle_activity_actor::finish( player_activity& act, Character& who )
{
    act.set_to_null();
    map& here = get_map();
    const optional_vpart_position vp = here.veh_at( placement );
    veh_interact::complete_vehicle( who );
    if( act.is_null() ) {
        if( npc * guy = dynamic_cast<npc * >( &who ) ) {
            guy->revert_after_activity();
            guy->set_moves( 0 );
        }
        return;
    }
    act.set_to_null();
    if( !who.is_npc() ) {
        if( vp ) {
            if( !activity_handlers::resume_for_multi_activities( static_cast<player &>( who ) ) ) {
                g->exam_vehicle( vp->vehicle(), cursor_pos );
            }
        }
    }
}

void vehicle_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.member( "placement", placement );
    jsout.member( "cursor_pos", cursor_pos );
    jsout.member( "vehicle_part", vehicle_part );
    jsout.member( "part_id", part_id.str() );
    jsout.member( "cmd", cmd );
}

std::unique_ptr<activity_actor> vehicle_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<vehicle_activity_actor> actor( new vehicle_activity_actor() );
    JsonObject data = jsin.get_object();
    data.read( "placement", actor->placement );
    data.read( "cursor_pos", actor->cursor_pos );
    data.read( "vehicle_part", actor->vehicle_part );
    std::string part_id_str;
    data.read( "part_id", part_id_str );
    actor->part_id = vpart_id( part_id_str );
    data.read( "cmd", actor->cmd );
    return actor;
}
// ---- move_loot_activity_actor ----
move_loot_activity_actor::move_loot_activity_actor() noexcept
    : stage( -1 ),
      num_processed( 0 ),
      placement( tripoint_abs_ms::zero() ) {}

void move_loot_activity_actor::do_turn( player_activity& act, Character& who )
{
    // Sync actor state to act fields before calling the legacy helper
    act.index = stage;
    if( act.values.empty() ) {
        act.values.push_back( num_processed );
    } else {
        act.values[0] = num_processed;
    }
    act.placement = placement;
    act.coord_set = coord_set;

    // Call the legacy helper function
    activity_on_turn_move_loot( act, static_cast<player &>( who ) );

    // Sync state back from act to actor members
    stage = act.index;
    num_processed = act.values[0];
    placement = act.placement;
    coord_set = act.coord_set;
}


void move_loot_activity_actor::start( player_activity& act, Character& who )
{
    placement = act.placement;
}

void move_loot_activity_actor::finish( player_activity& act, Character& who )
{
    // Activity self-terminates - no finish logic needed
    // The activity_on_turn_move_loot function calls set_to_null() when done
}

void move_loot_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.member( "stage", stage );
    jsout.member( "num_processed", num_processed );
    jsout.member( "placement", placement );
    jsout.member( "coord_set", coord_set );
}

std::unique_ptr<activity_actor> move_loot_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<move_loot_activity_actor> actor( new move_loot_activity_actor() );
    JsonObject data = jsin.get_object();
    data.read( "stage", actor->stage );
    data.read( "num_processed", actor->num_processed );
    data.read( "placement", actor->placement );
    data.read( "coord_set", actor->coord_set );
    return actor;
}
// ---- operation_activity_actor ----

operation_activity_actor::operation_activity_actor() noexcept
    : difficulty( 0 ),
      success( 0 ),
      max_power_level( 0 ),
      pl_skill( 0 ),
      autodoc( false ) {}

operation_activity_actor::operation_activity_actor(
    int diff, int succ, int power, int skill, const std::string& type, const bionic_id& b,
    const std::string& installer, bool auto_ )
    : difficulty( diff ),
      success( succ ),
      max_power_level( power ),
      pl_skill( skill ),
      operation_type( type ),
      bid( b ),
      installer_name( installer ),
      autodoc( auto_ ) {}

void operation_activity_actor::start( player_activity& act, Character& who )
{
    // No initialization needed, state is already set in constructor
}

void operation_activity_actor::do_turn( player_activity& act, Character& who )
{
    player* p = dynamic_cast<player *>( &who );
    if( p == nullptr ) { return; }

    const bionic_id upbid = bid->upgraded_bionic;
    const bool u_see =
        g->u.sees( p->bub_pos() )
        && ( !g->u.has_effect( effect_narcosis ) || g->u.has_bionic( bio_painkiller )
             || g->u.has_trait( trait_NOPAIN ) );

    const std::vector<bodypart_id> bps = get_occupied_bodyparts( bid );

    const time_duration half_op_duration = difficulty * 10_minutes;
    const time_duration message_freq = difficulty * 2_minutes;
    time_duration time_left = time_duration::from_turns( act.moves_left / 100 );

    map& here = get_map();

    if( autodoc && here.inbounds( p->bub_pos() ) ) {
        const auto autodocs =
            here.find_furnitures_or_vparts_with_flag_in_radius( p->bub_pos(), 1, flag_AUTODOC );
        if( !here.has_flag_furn_or_vpart( flag_AUTODOC_COUCH, p->bub_pos() ) || autodocs.empty() ) {
            p->remove_effect( effect_under_op );
            act.set_to_null();

            if( u_see ) {
                add_msg( m_bad, _( "The autodoc suffers a catastrophic failure." ) );
                p->add_msg_player_or_npc(
                    m_bad, _( "The Autodoc's failure damages you greatly." ),
                    _( "The Autodoc's failure damages <npcname> greatly." ) );
            }
            if( !bps.empty() ) {
                for( const bodypart_id& bp : bps ) {
                    p->add_effect( effect_bleed, 1_hours, bp.id(), difficulty );
                    p->apply_damage( nullptr, bp, 20 * difficulty );
                    if( u_see ) {
                        p->add_msg_player_or_npc(
                            m_bad, _( "Your %s is ripped open." ),
                            _( "<npcname>'s %s is ripped open." ),
                            body_part_name_accusative( bp->token ) );
                    }
                    if( bp == bodypart_id( "eyes" ) ) {
                        p->add_effect( effect_blind, 1_hours, bodypart_str_id::NULL_ID() );
                    }
                }
            } else {
                p->add_effect( effect_bleed, 1_hours, bodypart_str_id::NULL_ID(), difficulty );
                p->apply_damage( nullptr, bodypart_id( "torso" ), 20 * difficulty );
            }
            return;
        }
    }

    if( time_left > half_op_duration ) {
        if( !bps.empty() ) {
            for( const bodypart_id& bp : bps ) {
                if( calendar::once_every( message_freq ) && u_see && autodoc ) {
                    p->add_msg_player_or_npc(
                        m_info, _( "The Autodoc is meticulously cutting your %s open." ),
                        _( "The Autodoc is meticulously cutting <npcname>'s %s open." ),
                        body_part_name_accusative( bp->token ) );
                }
            }
        } else {
            if( calendar::once_every( message_freq ) && u_see ) {
                p->add_msg_player_or_npc(
                    m_info, _( "The Autodoc is meticulously cutting you open." ),
                    _( "The Autodoc is meticulously cutting <npcname> open." ) );
            }
        }
    } else if( time_left == half_op_duration ) {
        if( operation_type == "uninstall" ) {
            if( u_see && autodoc ) {
                add_msg( m_info, _( "The Autodoc attempts to carefully extract the bionic." ) );
            }
            if( p->has_bionic( bid ) ) {
                p->perform_uninstall(
                    bid, difficulty, success, units::from_joule( max_power_level ), pl_skill );
            } else {
                debugmsg( _( "Tried to uninstall %s, but you don't have this bionic installed." ),
                          bid.c_str() );
                p->remove_effect( effect_under_op );
                act.set_to_null();
            }
        } else {
            if( u_see && autodoc ) {
                add_msg( m_info, _( "The Autodoc attempts to carefully insert the bionic." ) );
            }
            if( bid.is_valid() ) {
                p->perform_install(
                    bid, upbid, difficulty, success, pl_skill, installer_name,
                    bid->canceled_mutations );
            } else {
                debugmsg( _( "%s is no a valid bionic_id" ), bid.c_str() );
                p->remove_effect( effect_under_op );
                act.set_to_null();
            }
        }
    } else if( success > 0 ) {
        if( !bps.empty() ) {
            for( const bodypart_id& bp : bps ) {
                if( calendar::once_every( message_freq ) && u_see && autodoc ) {
                    p->add_msg_player_or_npc(
                        m_info, _( "The Autodoc is stitching your %s back up." ),
                        _( "The Autodoc is stitching <npcname>'s %s back up." ),
                        body_part_name_accusative( bp->token ) );
                }
            }
        } else {
            if( calendar::once_every( message_freq ) && u_see && autodoc ) {
                p->add_msg_player_or_npc(
                    m_info, _( "The Autodoc is stitching you back up." ),
                    _( "The Autodoc is stitching <npcname> back up." ) );
            }
        }
    } else {
        if( calendar::once_every( message_freq ) && u_see && autodoc ) {
            p->add_msg_player_or_npc(
                m_bad,
                _( "The Autodoc is moving erratically through the rest of its program, not actually "
                   "stitching your wounds." ),
                _( "The Autodoc is moving erratically through the rest of its program, not actually "
                   "stitching <npcname>'s wounds." ) );
        }
    }

    if( p->has_effect( effect_narcosis ) ) {
        const time_duration remaining_time = p->get_effect_dur( effect_narcosis );
        if( remaining_time <= time_left ) {
            const time_duration top_off_time = time_left - remaining_time;
            p->add_effect( effect_narcosis, top_off_time );
            p->add_effect( effect_sleep, top_off_time );
        }
    } else {
        p->add_effect( effect_narcosis, time_left );
        p->add_effect( effect_sleep, time_left );
    }
}

void operation_activity_actor::finish( player_activity& act, Character& who )
{
    player* p = dynamic_cast<player *>( &who );
    if( p == nullptr ) { return; }

    map& here = get_map();
    if( autodoc ) {
        if( success > 0 ) {
            add_msg( m_good, _( "The Autodoc returns to its resting position after successfully "
                                "performing the operation." ) );
            const auto autodocs =
                here.find_furnitures_or_vparts_with_flag_in_radius( p->bub_pos(), 1, flag_AUTODOC );
            sounds::sound(
                autodocs.front(), 10, sounds::sound_t::music,
                _( "a short upbeat jingle: \"Operation successful\"" ), true, "Autodoc", "success" );
        } else {
            if( operation_type == "install" ) {
                add_msg( m_warning,
                         _( "The Autodoc completes installation and activates bionic but "
                            "reports about complications during operation." ) );
                const auto autodocs = here.find_furnitures_or_vparts_with_flag_in_radius(
                                          p->bub_pos(), 1, flag_AUTODOC );
                sounds::sound(
                    autodocs.front(), 10, sounds::sound_t::music,
                    _( "a sad beeping noise: \"Complications detected!  Report to medical personnel "
                       "immediately!\"" ),
                    true, "Autodoc", "failure" );
            } else {
                add_msg( m_bad, _( "The Autodoc jerks back to its resting position after failing the "
                                   "operation." ) );
                const auto autodocs = here.find_furnitures_or_vparts_with_flag_in_radius(
                                          p->bub_pos(), 1, flag_AUTODOC );
                sounds::sound(
                    autodocs.front(), 10, sounds::sound_t::music,
                    _( "a sad beeping noise: \"Operation failed\"" ), true, "Autodoc", "failure" );
            }
        }
    } else {
        if( success > 0 ) {
            add_msg( m_good, _( "The operation is a success." ) );
        } else {
            if( operation_type == "install" ) {
                add_msg( m_warning,
                         _( "Bionic was installed and activated but a complication "
                            "happened during operation!" ) );
            } else {
                add_msg( m_bad, _( "The operation is a failure." ) );
            }
        }
    }
    p->remove_effect( effect_under_op );
    act.set_to_null();
}

void operation_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.member( "difficulty", difficulty );
    jsout.member( "success", success );
    jsout.member( "max_power_level", max_power_level );
    jsout.member( "pl_skill", pl_skill );
    jsout.member( "operation_type", operation_type );
    jsout.member( "bid", bid );
    jsout.member( "installer_name", installer_name );
    jsout.member( "autodoc", autodoc );
}

std::unique_ptr<activity_actor> operation_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<operation_activity_actor> actor( new operation_activity_actor() );
    JsonObject data = jsin.get_object();
    data.read( "difficulty", actor->difficulty );
    data.read( "success", actor->success );
    data.read( "max_power_level", actor->max_power_level );
    data.read( "pl_skill", actor->pl_skill );
    data.read( "operation_type", actor->operation_type );
    data.read( "bid", actor->bid );
    data.read( "installer_name", actor->installer_name );
    data.read( "autodoc", actor->autodoc );
    return actor;
}
// ---- generic_multi_activity_actor ----

void generic_multi_activity_actor::start( player_activity& act, Character & /*who*/ )
{
    // No state to initialize; handler manages everything
}

void generic_multi_activity_actor::do_turn( player_activity& act, Character& who )
{
    player* p = dynamic_cast<player *>( &who );
    if( !p ) {
        debugmsg( "generic_multi_activity_actor::do_turn called on non-player" );
        act.set_to_null();
        return;
    }
    // The handler manages the entire multi-activity flow internally
    generic_multi_activity_handler( act, *p, false );
}

void generic_multi_activity_actor::finish( player_activity& act, Character & /*who*/ )
{
    // Handler manages its own completion; this is a no-op
}

void generic_multi_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.member( "type", type.str() );
}

std::unique_ptr<activity_actor> generic_multi_activity_actor::deserialize( JsonIn& jsin )
{
    JsonObject data = jsin.get_object();
    activity_id type;
    data.read( "type", type );
    return std::make_unique<generic_multi_activity_actor>( type );
}

namespace activity_actors
{

// Please keep this alphabetically sorted
const std::unordered_map<activity_id, std::unique_ptr<activity_actor> ( * )( JsonIn & )>
deserialize_functions = {
    {activity_id( "ACT_AIM" ), &aim_activity_actor::deserialize},
    {activity_id( "ACT_ADV_INVENTORY" ), &adv_inventory_activity_actor::deserialize},
    {activity_id( "ACT_ARMOR_LAYERS" ), &armor_layers_activity_actor::deserialize},
    {activity_id( "ACT_ASSIST" ), &assist_activity_actor::deserialize},
    {activity_id( "ACT_ATM" ), &atm_activity_actor::deserialize},
    {activity_id( "ACT_AUTODRIVE" ), &autodrive_activity_actor::deserialize},
    {activity_id( "ACT_BLEED" ), &butchery_activity_actor::deserialize},
    {activity_id( "ACT_BOLTCUTTING" ), &boltcutting_activity_actor::deserialize},
    {activity_id( "ACT_BUILD" ), &construction_activity_actor::deserialize},
    {activity_id( "ACT_BURROW" ), &burrow_activity_actor::deserialize},
    {activity_id( "ACT_BUTCHER" ), &butchery_activity_actor::deserialize},
    {activity_id( "ACT_BUTCHER_FULL" ), &butchery_activity_actor::deserialize},
    {activity_id( "ACT_CHOP_LOGS" ), &wood_chop_activity_actor::deserialize},
    {activity_id( "ACT_CHOP_PLANKS" ), &wood_chop_activity_actor::deserialize},
    {activity_id( "ACT_CHOP_TREE" ), &wood_chop_activity_actor::deserialize},
    {activity_id( "ACT_CLEAR_RUBBLE" ), &clear_rubble_activity_actor::deserialize},
    {activity_id( "ACT_CHURN" ), &churn_activity_actor::deserialize},
    {activity_id( "ACT_CONSUME_DRINK_MENU" ), &consume_menu_activity_actor::deserialize},
    {activity_id( "ACT_CONSUME_FOOD_MENU" ), &consume_menu_activity_actor::deserialize},
    {activity_id( "ACT_CONSUME_MEDS_MENU" ), &consume_menu_activity_actor::deserialize},
    {activity_id( "ACT_CRAFT" ), &craft_activity_actor::deserialize},
    {activity_id( "ACT_CRACKING" ), &cracking_activity_actor::deserialize},
    {activity_id( "ACT_DIG" ), &dig_activity_actor::deserialize},
    {activity_id( "ACT_DIG_CHANNEL" ), &dig_channel_activity_actor::deserialize},
    {activity_id( "ACT_DISASSEMBLE" ), &disassemble_activity_actor::deserialize},
    {activity_id( "ACT_DISMEMBER" ), &butchery_activity_actor::deserialize},
    {activity_id( "ACT_DISSECT" ), &butchery_activity_actor::deserialize},
    {activity_id( "ACT_DROP" ), &drop_activity_actor::deserialize},
    {activity_id( "ACT_EAT_MENU" ), &consume_menu_activity_actor::deserialize},
    {activity_id( "ACT_FETCH_REQUIRED" ), &generic_multi_activity_actor::deserialize},
    {activity_id( "ACT_FERTILIZE_PLOT" ), &fertilize_plot_activity_actor::deserialize},
    {activity_id( "ACT_FIELD_DRESS" ), &butchery_activity_actor::deserialize},
    {activity_id( "ACT_FILL_LIQUID" ), &fill_liquid_activity_actor::deserialize},
    {activity_id( "ACT_FILL_PIT" ), &fill_pit_activity_actor::deserialize},
    {activity_id( "ACT_FISH" ), &fish_activity_actor::deserialize},
    {activity_id( "ACT_FIRSTAID" ), &firstaid_activity_actor::deserialize},
    {activity_id( "ACT_FIND_MOUNT" ), &find_mount_activity_actor::deserialize},
    {activity_id( "ACT_FORAGE" ), &forage_activity_actor::deserialize},
    {activity_id( "ACT_GAME" ), &game_activity_actor::deserialize},
    {activity_id( "ACT_GENERIC_GAME" ), &game_activity_actor::deserialize},
    {activity_id( "ACT_GUNMOD_ADD" ), &gunmod_add_activity_actor::deserialize},
    {activity_id( "ACT_HACKING" ), &hacking_activity_actor::deserialize},
    {activity_id( "ACT_HACKSAW" ), &hacksaw_activity_actor::deserialize},
    {activity_id( "ACT_HOTWIRE_CAR" ), &hotwire_car_activity_actor::deserialize},
    {activity_id( "ACT_HAND_CRANK" ), &hand_crank_activity_actor::deserialize},
    {activity_id( "ACT_HAIRCUT" ), &morale_activity_actor::deserialize},
    {activity_id( "ACT_JACKHAMMER" ), &jackhammer_activity_actor::deserialize},
    {activity_id( "ACT_LOCKPICK" ), &lockpick_activity_actor::deserialize},
    {activity_id( "ACT_LONGSALVAGE" ), &salvage_activity_actor::deserialize},
    {activity_id( "ACT_MAKE_ZLAVE" ), &make_zlave_activity_actor::deserialize},
    {activity_id( "ACT_MEDITATE" ), &morale_activity_actor::deserialize},
    {activity_id( "ACT_MEND_ITEM" ), &mend_item_activity_actor::deserialize},
    {activity_id( "ACT_MIGRATION_CANCEL" ), &migration_cancel_activity_actor::deserialize},
    {activity_id( "ACT_MILK" ), &milk_activity_actor::deserialize},
    {activity_id( "ACT_MIND_SPLICER" ), &mind_splicer_activity_actor::deserialize},
    {activity_id( "ACT_MOVE_LOOT" ), &move_loot_activity_actor::deserialize},
    {activity_id( "ACT_MULTIPLE_BUTCHER" ), &generic_multi_activity_actor::deserialize},
    {activity_id( "ACT_MULTIPLE_CHOP_PLANKS" ), &generic_multi_activity_actor::deserialize},
    {activity_id( "ACT_MULTIPLE_CHOP_TREES" ), &generic_multi_activity_actor::deserialize},
    {activity_id( "ACT_MULTIPLE_CONSTRUCTION" ), &generic_multi_activity_actor::deserialize},
    {activity_id( "ACT_MULTIPLE_FARM" ), &generic_multi_activity_actor::deserialize},
    {activity_id( "ACT_MULTIPLE_FISH" ), &generic_multi_activity_actor::deserialize},
    {activity_id( "ACT_MULTIPLE_MINE" ), &generic_multi_activity_actor::deserialize},
    {activity_id( "ACT_MOVE_ITEMS" ), &move_items_activity_actor::deserialize},
    {activity_id( "ACT_OXYTORCH" ), &oxytorch_activity_actor::deserialize},
    {activity_id( "ACT_OPERATION" ), &operation_activity_actor::deserialize},
    {activity_id( "ACT_PICKAXE" ), &pickaxe_activity_actor::deserialize},
    {activity_id( "ACT_PICKUP" ), &pickup_activity_actor::deserialize},
    {activity_id( "ACT_PLANT_SEED" ), &plant_seed_activity_actor::deserialize},
    {activity_id( "ACT_PLAY_WITH_PET" ), &play_with_pet_activity_actor::deserialize},
    {activity_id( "ACT_PULP" ), &pulp_activity_actor::deserialize},
    {activity_id( "ACT_PRY_NAILS" ), &pry_nails_activity_actor::deserialize},
    {activity_id( "ACT_QUARTER" ), &butchery_activity_actor::deserialize},
    {activity_id( "ACT_READ" ), &read_activity_actor::deserialize},
    {activity_id( "ACT_RELOAD" ), &reload_activity_actor::deserialize},
    {activity_id( "ACT_REPAIR_ITEM" ), &repair_item_activity_actor::deserialize},
    {activity_id( "ACT_ROBOT_CONTROL" ), &robot_control_activity_actor::deserialize},
    {activity_id( "ACT_SHAVE" ), &morale_activity_actor::deserialize},
    {activity_id( "ACT_SPELLCASTING" ), &spellcasting_activity_actor::deserialize},
    {activity_id( "ACT_SHEAR" ), &shear_activity_actor::deserialize},
    {activity_id( "ACT_SKIN" ), &butchery_activity_actor::deserialize},
    {activity_id( "ACT_SOCIALIZE" ), &socialize_activity_actor::deserialize},
    {activity_id( "ACT_START_FIRE" ), &start_fire_activity_actor::deserialize},
    {activity_id( "ACT_START_ENGINES" ), &start_engines_activity_actor::deserialize},
    {activity_id( "ACT_STUDY_SPELL" ), &study_spell_activity_actor::deserialize},
    {activity_id( "ACT_STASH" ), &stash_activity_actor::deserialize},
    {activity_id( "ACT_THROW" ), &throw_activity_actor::deserialize},
    {activity_id( "ACT_TOGGLE_GATE" ), &toggle_gate_activity_actor::deserialize},
    {activity_id( "ACT_TOOLMOD_ADD" ), &toolmod_add_activity_actor::deserialize},
    {activity_id( "ACT_TRAIN" ), &train_activity_actor::deserialize},
    {activity_id( "ACT_TRAIN_SKILL" ), &train_skill_activity_actor::deserialize},
    {activity_id( "ACT_TRAIN_PET" ), &train_pet_activity_actor::deserialize},
    {activity_id( "ACT_TRAVELLING" ), &travelling_activity_actor::deserialize},
    {activity_id( "ACT_TREE_COMMUNION" ), &tree_communion_activity_actor::deserialize},
    {activity_id( "ACT_TRY_SLEEP" ), &try_sleep_activity_actor::deserialize},
    {activity_id( "ACT_TIDY_UP" ), &generic_multi_activity_actor::deserialize},
    {activity_id( "ACT_VEHICLE" ), &vehicle_activity_actor::deserialize},
    {activity_id( "ACT_VEHICLE_DECONSTRUCTION" ), &generic_multi_activity_actor::deserialize},
    {activity_id( "ACT_VEHICLE_REPAIR" ), &generic_multi_activity_actor::deserialize},
    {activity_id( "ACT_VIBE" ), &vibe_activity_actor::deserialize},
    {activity_id( "ACT_WAIT" ), &wait_activity_actor::deserialize},
    {activity_id( "ACT_WAIT_NPC" ), &wait_activity_actor::deserialize},
    {activity_id( "ACT_WAIT_STAMINA" ), &wait_stamina_activity_actor::deserialize},
    {activity_id( "ACT_WAIT_WEATHER" ), &wait_activity_actor::deserialize},
    {activity_id( "ACT_WEAR" ), &wear_activity_actor::deserialize}
};
} // namespace activity_actors

void serialize( const std::unique_ptr<activity_actor> &actor, JsonOut& jsout )
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

void deserialize( std::unique_ptr<activity_actor> &actor, JsonIn& jsin )
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
                debugmsg( "Failed to find activity actor deserializer for type \"%s\"",
                          actor_type.c_str() );
                actor = nullptr;
            }
        } else {
            debugmsg( "Failed to load activity actor" );
            actor = nullptr;
        }
    }
}
