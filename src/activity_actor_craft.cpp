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



craft_activity_actor::craft_activity_actor(
    const recipe* rec, int batch_size, int craft_counter, const tripoint_abs_ms& location,
    std::vector<comp_selection<item_comp>> item_selections,
    std::vector<comp_selection<tool_comp>> tool_selections, bool tools_prepaid, bool is_long )
    : rec( rec ),
      batch_size( batch_size ),
      craft_counter( craft_counter ),
      location( location ),
      item_selections( std::move( item_selections ) ),
      tool_selections( std::move( tool_selections ) ),
      tools_prepaid( tools_prepaid ),
      is_long( is_long ),
      is_valid( rec != nullptr ) {}

auto craft_activity_actor::find_in_progress_craft(
    const player_activity& act,
    Character& who ) const -> item* // *NOPAD*
{
    if( !act.targets.empty() && act.targets.front() && act.targets.front()->is_craft()
        && &act.targets.front()->get_making() == rec ) {
        return &*act.targets.front();
    }

    item* result = nullptr;
    who.visit_items( [&]( item * it ) {
        if( it->is_craft() && &it->get_making() == rec ) {
            result = it;
            return VisitResponse::ABORT;
        }
        return VisitResponse::NEXT;
    } );
    if( result ) { return result; }
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

void craft_activity_actor::calc_all_moves( player_activity& act, Character& who )
{
    if( !rec || !is_valid ) {
        act.set_to_null();
        return;
    }

    const int current_turn = to_turn<int>( calendar::turn );

    // Catch-up: apply time elapsed while NPC was outside the reality bubble.
    // last_turn_nr >= 0 means start() already ran in a previous session.
    if( last_turn_nr >= 0 && current_turn > last_turn_nr ) {
        item* craft_item = find_in_progress_craft( act, who );
        if( craft_item ) {
            const int elapsed_turns = current_turn - last_turn_nr;
            const double base_total_moves = std::max( 1, rec->batch_time( batch_size, 1.0f, 0 ) );
            // 100 moves per turn at base speed (no modifiers applied while outside bubble)
            const double moves_elapsed = elapsed_turns * 100.0;
            const int old_counter = craft_item->get_counter();
            const int new_counter = std::
                                    min( static_cast<int>( old_counter + moves_elapsed / base_total_moves * 10'000'000.0 ),
                                         10'000'000 );
            craft_item->set_counter( new_counter );
            craft_counter = new_counter;

            const int five_percent_steps = new_counter / 500'000 - old_counter / 500'000;
            if( five_percent_steps > 0 ) { who.craft_skill_gain( *craft_item, five_percent_steps ); }

            // Re-build progress counter to match updated craft state
            const int remaining = std::
                                  max( 0, static_cast<int>( base_total_moves * ( 1.0 - new_counter / 10'000'000.0 ) ) );
            if( !activity_actor::progress.empty() ) {
                activity_actor::progress.mod_moves_left(
                    remaining - activity_actor::progress.get_moves_left() );
            } else {
                activity_actor::progress
                .emplace( craft_item->tname(), static_cast<int>( base_total_moves ), remaining );
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
        item* craft_item = find_in_progress_craft( act, who );
        const std::string name = craft_item ? craft_item->tname() : rec->result_name();
        const int base_total = std::max( 1, rec->batch_time( batch_size, 1.0f, 0 ) );
        const int remaining =
            std::max( 1, static_cast<int>( base_total * ( 1.0 - craft_counter / 10'000'000.0 ) ) );
        activity_actor::progress.emplace( name, base_total, remaining );
    }

    item* craft_item = find_in_progress_craft( act, who );
    if( craft_item ) { refresh_speed( act, who, *craft_item ); }
}

void craft_activity_actor::refresh_speed(
    player_activity& act, const Character& who, const item& craft_item,
    std::optional<bench_location> bench ) const
{
    const bench_location resolved_bench = bench ? *bench : find_best_bench( who, craft_item );
    const recipe& making = *rec;
    const float tools_mult =
        cached_tools_mult != 0.0f ? cached_tools_mult : crafting_tools_speed_multiplier( who, making );
    act.speed.light = lighting_crafting_speed_multiplier( who, making );
    act.speed.bench_factor = workbench_crafting_speed_multiplier( craft_item, resolved_bench );
    act.speed.morale = morale_crafting_speed_multiplier( who, making );
    act.speed.tools = tools_mult;
    act.speed.player_speed = who.get_speed() / 100.0f;
    const int assistants = who.available_assistant_count( making );
    if( assistants > 0 ) {
        const double base_no_assist = std::max( 1, making.batch_time( batch_size, 1.0f, 0 ) );
        const double base_with_assist =
            std::max( 1, making.batch_time( batch_size, 1.0f, assistants ) );
        act.speed.assist = static_cast<float>( base_no_assist / base_with_assist );
    } else {
        act.speed.assist = 1.0f;
    }
    // Mutation and game-option multipliers have no dedicated speed field; fold them
    // into skills so act.speed.total() matches the actual crafting rate.
    const float mutation_mult = who.mutation_value( "crafting_speed_modifier" );
    const float game_opt_mult =
        get_option<int>( "CRAFTING_SPEED_MULT" ) == 0
        ? 9999.0f
        : 100.0f / static_cast<float>( get_option<int>( "CRAFTING_SPEED_MULT" ) );
    act.speed.skills = mutation_mult * game_opt_mult;
}

void craft_activity_actor::start( player_activity& act, Character& who )
{
    if( !rec || !is_valid ) {
        act.set_to_null();
        return;
    }

    item* craft_item = find_in_progress_craft( act, who );
    if( !craft_item ) {
        who.add_msg_player_or_npc(
            _( "You lost your in progress %s and had to stop crafting." ),
            _( "<npcname> lost the in progress %s and had to stop crafting." ), rec->result_name() );
        act.set_to_null();
        return;
    }

    cached_tools_mult = crafting_tools_speed_multiplier( who, *rec );
    craft_counter = craft_item->get_counter();
    last_turn_nr = to_turn<int>( calendar::turn ); // mark fresh start so calc_all_moves skips
    // catch-up
    const int base_total = std::max( 1, rec->batch_time( batch_size, 1.0f, 0 ) );
    const int remaining =
        craft_counter == 0
        ? base_total
        : std::max( 1, static_cast<int>( base_total * ( 1.0 - craft_counter / 10'000'000.0 ) ) );
    activity_actor::progress.emplace( craft_item->tname(), base_total, remaining );
}

void craft_activity_actor::do_turn( player_activity& act, Character& who )
{
    if( !rec || !is_valid ) {
        act.set_to_null();
        return;
    }

    item* craft_item = find_in_progress_craft( act, who );
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

    const recipe& making = *rec;
    if( cached_tools_mult == 0.0f ) {
        cached_tools_mult = crafting_tools_speed_multiplier( who, making );
    }
    const bench_location bench = find_best_bench( who, *craft_item );
    refresh_speed( act, who, *craft_item, bench );
    const float crafting_speed =
        crafting_speed_multiplier( who, *craft_item, bench, act.speed.tools );
    const int assistants = who.available_assistant_count( making );

    if( crafting_speed <= 0.0f ) {
        who.add_msg_player_or_npc(
            m_bad, _( "You cannot continue crafting." ), _( "<npcname> cannot continue crafting." ) );
        act.set_to_null();
        return;
    }

    const int old_counter = craft_item->get_counter();
    const double base_total_moves = std::max( 1, making.batch_time( batch_size, 1.0f, 0 ) );
    const double cur_total_moves =
        std::max( 1, making.batch_time( batch_size, crafting_speed, assistants ) );
    const double delta_progress =
        who.get_moves() > 0 ? who.get_moves() * base_total_moves / cur_total_moves : 0.0;
    const double current_progress = old_counter * base_total_moves / 10'000'000.0 + delta_progress;
    const int new_counter = std::
                            min( static_cast<int>( std::round( current_progress / base_total_moves * 10'000'000.0 ) ),
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
        if( delta != 0 ) { activity_actor::progress.mod_moves_left( delta ); }
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
                _( "There is nothing left of the %s <npcname> was crafting." ), craft_item->tname() );
            craft_item->detach();
            act.set_to_null();
        }
        // If !destroy, handle_craft_failure may have called cancel_activity already
    }
}

void craft_activity_actor::finish( player_activity& act, Character& who )
{
    act.set_to_null();
    do_complete_craft( act, who );
}

void craft_activity_actor::do_complete_craft( player_activity& act, Character& who )
{
    item* craft_item = find_in_progress_craft( act, who );
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
    const player_activity& act, const Character& who ) const
{
    if( !rec || !is_valid ) { return act_progress_message::make_empty(); }

    const int assistants = who.available_assistant_count( *rec );
    const double base_total_moves = std::max( 1, rec->batch_time( batch_size, 1.0f, 0 ) );
    const double remaining_pct = 1.0 - craft_counter / 10'000'000.0;
    const float total_mult = act.speed.total();
    const int remaining_turns = static_cast<int>(
                                    remaining_pct * base_total_moves / 100 / std::max( 0.01f, total_mult ) );

    const std::string time_desc =
        string_format( _( "Time left: %s" ), to_string( time_duration::from_turns( remaining_turns ) ) );

    const auto fmt_spd = [&]( float level, const std::string & name ) -> std::string {
        const int pct = static_cast<int>( level * 100 );
        if( pct == 100 ) { return ""; }
        nc_color col = pct > 100 ? c_green : c_red;
        return string_format( " - %s: %s\n", name, colorize( std::to_string( pct ) + '%', col ) );
    };

    std::string mults_desc = _( "Crafting speed multipliers:\n" );
    const int total_pct = static_cast<int>( total_mult * 100 );
    nc_color total_col = total_pct > 100 ? c_green : c_red;
    mults_desc += string_format(
                      " - %s: %s\n", _( "Total" ), colorize( std::to_string( total_pct ) + '%', total_col ) );
    mults_desc += fmt_spd( act.speed.player_speed, _( "Speed" ) );
    mults_desc += fmt_spd( act.speed.light, _( "Light" ) );
    mults_desc += fmt_spd( act.speed.bench_factor, _( "Workbench" ) );
    mults_desc += fmt_spd( act.speed.morale, _( "Morale" ) );
    mults_desc += fmt_spd( act.speed.tools, _( "Tools" ) );
    if( assistants > 0 ) { mults_desc += fmt_spd( act.speed.assist, _( "Assistants" ) ); }

    return act_progress_message::make_full( string_format(
            _( "%s: %s\n\n%s\n\n%s" ), act.get_verb().translated(), rec->result_name(), time_desc,
            mults_desc ) );
}

void craft_activity_actor::serialize( JsonOut& jsout ) const
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

std::unique_ptr<activity_actor> craft_activity_actor::deserialize( JsonIn& jsin )
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

void construction_activity_actor::calc_all_moves( player_activity& act, Character& who )
{
    // Check if pc was lost for some reason, but actually still exists on map, e.g. save/load
    if( !pc ) {
        map& here = get_map();
        auto local = here.abs_to_bub( target );
        pc = here.partial_con_at( tripoint_bub_ms( local ) );
    }
    // if something goes terribly wrong we don't CTD
    if( !pc ) {
        act.set_to_null();
        return;
    }
    auto reqs = activity_reqs_adapter( *pc->id );
    act.speed.calc_all_moves( who, reqs );
}

void construction_activity_actor::start( player_activity & /*act*/, Character & /*who*/ )
{
    map& here = get_map();
    auto local = here.abs_to_bub( target );
    pc = here.partial_con_at( tripoint_bub_ms( local ) );
    auto& built = *pc->id;

    std::string name;

    if( pc->id == deconstruct || pc->id == deconstruct_simple
        || built.group == advanced_object_deconstruction ) {
        if( here.has_furn( local ) ) {
            const furn_id furn_type = here.furn( local );
            name = furn_type->name();
        } else if( !here.ter( local )->is_null() ) {
            const ter_id ter_type = here.ter( local );
            name = ter_type->name();
        }
    } else {
        name = built.post_furniture.is_empty() ? "" : built.post_furniture->name();
        name = built.post_terrain.is_empty() ? name : built.post_terrain->name();
    }

    int total_time = std::max( 1, built.adjusted_time() );
    int left = pc->counter == 0 ? total_time : total_time - pc->counter / 10'000'000.0 * total_time;

    progress.emplace( name, total_time, left );
}

void construction_activity_actor::do_turn( player_activity& act, Character& who )
{
    // Check if pc was lost for some reason, but actually still exists on map, e.g. save/load
    if( !pc ) {
        map& here = get_map();
        auto local = here.abs_to_bub( target );
        pc = here.partial_con_at( tripoint_bub_ms( local ) );
    }

    // Maybe the player and the NPC are working on the same construction at the same time or toubles
    // during load
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
        auto& built = *pc->id;
        if( !who.has_trait( trait_DEBUG_HS ) && !who.meets_skill_requirements( built ) ) {
            add_msg( m_info, _( "%s can't work on this construction anymore." ), who.disp_name() );
            act.set_to_null();
            return;
        }
    }
}

void construction_activity_actor::finish( player_activity& act, Character& who )
{
#ifdef COOP_ENABLED
    // C2d: snapshot terrain/furniture + exact character tile items before
    // complete_construction() so we can diff what changed and propagate to the host.
    ter_id   con_ter_before;
    furn_id  con_furn_before;
    ter_id   con_ter_above_before; // complete_construction may also set a roof tile
    std::vector<const item *> con_items_at_player; // byproducts spawn at who.bub_pos() only
    if( g->coop_client_ ) {
        map& here = get_map();
        const auto local = here.abs_to_bub( target );
        con_ter_before       = here.ter( local );
        con_furn_before      = here.furn( local );
        con_ter_above_before = here.ter( local + tripoint_above );
        // Snapshot ONLY who.bub_pos() — NOT a radius.  Pre-existing items are moved
        // to a random dump spot first (construction.cpp:1757-1776); a radius scan
        // would catch them there and emit spurious DROP, duplicating on the host.
        for( const item * it : here.i_at( who.bub_pos() ) ) {
            con_items_at_player.push_back( it );
        }
    }
#endif // COOP_ENABLED

    complete_construction( who, target );

#ifdef COOP_ENABLED
    if( g->coop_client_ ) {
        map& here = get_map();
        const auto local = here.abs_to_bub( target );
        // Target tile terrain/furniture change.
        if( here.ter( local ) != con_ter_before || here.furn( local ) != con_furn_before ) {
            g->coop_client_->queue_terrain_change(
                target, here.ter( local ).id().str(), here.furn( local ).id().str() );
        }
        // Roof tile: complete_construction may set the tile directly above (new_ter->roof).
        const tripoint_abs_ms target_above{target.x(), target.y(), target.z() + 1};
        const tripoint_bub_ms local_above = local + tripoint_above;
        if( here.ter( local_above ) != con_ter_above_before ) {
            g->coop_client_->queue_terrain_change(
                target_above, here.ter( local_above ).id().str(), furn_id( "f_null" ).id().str() );
        }
        // Byproduct items: scan only who.bub_pos() (spawn location in construction.cpp:1799).
        std::ostringstream drop_oss;
        JsonOut drop_jout( drop_oss );
        drop_jout.start_object();
        drop_jout.member( "items" );
        drop_jout.start_array();
        bool has_new = false;
        const tripoint_abs_ms player_abs = here.bub_to_abs( who.bub_pos() );
        for( const item * it : here.i_at( who.bub_pos() ) ) {
            // Skip items that were there before construction finished.
            using cip = const item*;
            if( std::ranges::find( con_items_at_player, cip{it} ) != con_items_at_player.end() ) {
                continue;
            }
            drop_jout.start_object();
            drop_jout.member( "tx", player_abs.x() );
            drop_jout.member( "ty", player_abs.y() );
            drop_jout.member( "tz", player_abs.z() );
            std::ostringstream item_oss;
            JsonOut jitem( item_oss );
            it->serialize( jitem );
            drop_jout.member( "data", item_oss.str() );
            drop_jout.end_object();
            has_new = true;
        }
        drop_jout.end_array();
        drop_jout.end_object();
        if( has_new ) { g->coop_client_->queue_action( "DROP", drop_oss.str() ); }
    }
#endif // COOP_ENABLED
    act.set_to_null();
}

void construction_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "target", target );
    jsout.end_object();
}

std::unique_ptr<activity_actor> construction_activity_actor::deserialize( JsonIn& jsin )
{
    std::unique_ptr<construction_activity_actor> actor(
        new construction_activity_actor( tripoint_abs_ms( tripoint_zero ) ) );
    JsonObject data = jsin.get_object();
    data.read( "progress", actor->progress );
    data.read( "target", actor->target );
    return actor;
}

void assist_activity_actor::start( player_activity & /*act*/, Character & /*who*/ )
{
    progress.dummy();
}

void assist_activity_actor::serialize( JsonOut& jsout ) const
{
    // Activity is not being saved but still provide some valid json if called.
    jsout.write_null();
}

std::unique_ptr<activity_actor> assist_activity_actor::deserialize( JsonIn & )
{
    return std::make_unique<assist_activity_actor>();
}

std::unique_ptr<activity_actor> salvage_activity_actor::deserialize( JsonIn& jsin )
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

auto butchery_activity_actor::setup_next_target( player_activity& act, Character& who ) -> bool
{
    if( this->targets.empty() ) { return false; }

safe_reference<item> &target = this->targets.back();
player& p = static_cast<player &>( who );

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
        for( const std::string& prob : setup.problems ) { p.add_msg_if_player( m_bad, prob ); }
        if( setup.problems.empty() ) {
            for( const std::string& info : setup.info ) { p.add_msg_if_player( m_info, info ); }
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
            if( query_yn( _( "Would you dare desecrate the mortal remains of a fellow human "
                             "being?" ) ) ) {
                switch( rng( 1, 3 ) ) {
                    case 1:
                        p.add_msg_if_player(
                            m_bad,
                            _( "You clench your teeth at the prospect of "
                               "this gruesome job." ) );
                        break;
                    case 2:
                        p.add_msg_if_player( m_bad, _( "This will haunt you in your dreams." ) );
                        break;
                    case 3:
                        p.add_msg_if_player(
                            m_bad,
                            _( "You try to look away, but this gruesome "
                               "image will stay on your mind for some "
                               "time." ) );
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

void butchery_activity_actor::start( player_activity& act, Character& who )
{
    if( !setup_next_target( act, who ) ) { act.set_to_null(); }
}

void butchery_activity_actor::do_turn( player_activity& act, Character& who )
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

void butchery_activity_actor::finish( player_activity& act, Character& who )
{
    player& p = static_cast<player &>( who );
    map& here = get_map();

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
        if( !this->targets.empty() && setup_next_target( act, p ) ) { return; }
        act.set_to_null();
        activity_handlers::resume_for_multi_activities( p );
        return;
    }

    item& corpse_item = *target;
    const mtype* corpse = corpse_item.get_mtype();
    const inventory& inv = p.crafting_inventory();
    const field_type_id type_blood = corpse->bloodType();
    const field_type_id type_gib = corpse->gibType();

    if( this->type == QUARTER ) {
        butchery_quarter( &corpse_item, p );
        this->progress.pop();
        this->targets.pop_back();
        if( !this->targets.empty() && setup_next_target( act, p ) ) { return; }
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
        if( factor < 0 ) { skill_shift -= rng_float( 0, -factor / 5.0 ); }
        return static_cast<int>( std::round( skill_shift ) );
    };

    if( this->type == DISMEMBER ) {
        here.add_splatter( type_gib, p.bub_pos(), rng( corpse->size + 2, ( corpse->size + 1 ) * 2 ) );
    }

    // Fatal failure for non-dissect actions
    if( this->type != DISSECT && roll_butchery() <= ( -15 ) && one_in( 2 ) ) {
        switch( rng( 1, 3 ) ) {
            case 1:
                p.add_msg_if_player(
                    m_warning,
                    _( "You hack up the corpse so unskillfully, that "
                       "there is nothing left to salvage from this "
                       "bloody mess." ) );
                break;
            case 2:
                p.add_msg_if_player(
                    m_warning,
                    _( "You wanted to cut the corpse, but instead you "
                       "hacked the meat, spilled the guts all over it, "
                       "and made a bloody mess." ) );
                break;
            case 3:
                p.add_msg_if_player(
                    m_warning,
                    _( "You made so many mistakes during the process "
                       "that you doubt even vultures will be interested "
                       "in what's left of it." ) );
                break;
        }

        target->detach();

        this->progress.pop();
        this->targets.pop_back();

        here.add_splatter( type_gib, p.bub_pos(), rng( corpse->size + 2, ( corpse->size + 1 ) * 2 ) );
        here.add_splatter( type_blood, p.bub_pos(), rng( corpse->size + 2, ( corpse->size + 1 ) * 2 ) );
        for( int i = 1; i <= corpse->size; i++ ) {
            here.add_splatter_trail(
                type_gib, p.bub_pos(),
                random_entry( here.points_in_radius( p.bub_pos(), corpse->size + 1 ) ) );
            here.add_splatter_trail(
                type_blood, p.bub_pos(),
                random_entry( here.points_in_radius( p.bub_pos(), corpse->size + 1 ) ) );
        }

        if( !this->targets.empty() && setup_next_target( act, p ) ) { return; }
        act.set_to_null();
        activity_handlers::resume_for_multi_activities( p );
        return;
    }

    const auto roll_drops = [&]() {
        factor = std::max( factor, -50 );
        return 0.5 * skill_level / 10 + 0.3 * ( factor + 50 ) / 100 + 0.2 * p.dex_cur / 20;
    };

#ifdef COOP_ENABLED
    std::vector<const item *> coop_items_before;
    bool coop_corpse_detached = false;
    if( g->coop_client_ ) {
        for( const item * it : here.i_at( p.bub_pos() ) ) {
            coop_items_before.push_back( it );
        }
    }
#endif // COOP_ENABLED
    butchery_drops_harvest( &corpse_item, *corpse, p, roll_butchery, this->type, roll_drops );

    if( this->type == DISSECT ) {
        int roll = roll_butchery() - corpse_item.damage_level( 4 );
        roll = roll < 1 ? 1 : roll;
        std::vector<detached_ptr<item>> cbms = corpse_item.remove_components();
        std::vector<detached_ptr<item>> contents = corpse_item.contents.clear_items();
        for( detached_ptr<item> &it : contents ) { cbms.push_back( std::move( it ) ); }
        extract_or_wreck_cbms( cbms, roll, p );
        int time_to_cut = size_factor_in_time_to_cut( corpse->size ) / 100;
        int level_cap =
            std::min<int>( MAX_SKILL, ( static_cast<int>( corpse->size ) + ( cbms.size() * 2 + 1 ) ) );
        int size_mult = corpse->size > creature_size::medium ? ( corpse->size * corpse->size ) : 8;
        int practice_amt =
            ( size_mult + 1 ) * ( ( time_to_cut / 150 ) + 1 ) * ( cbms.size() * cbms.size() / 2 + 1 );
        p.practice( skill_firstaid, practice_amt, level_cap );
    }

    switch( this->type ) {
        case QUARTER:
            break;
        case BUTCHER:
            p.add_msg_if_player(
                m_good,
                _( "You apply few quick cuts to the %s and leave what's left of it for scavengers." ),
                corpse_item.tname() );
            target->detach();
#ifdef COOP_ENABLED
            coop_corpse_detached = true;
#endif // COOP_ENABLED
            break;
        case BUTCHER_FULL:
            p.add_msg_if_player( m_good, _( "You finish butchering the %s." ), corpse_item.tname() );
            target->detach();
#ifdef COOP_ENABLED
            coop_corpse_detached = true;
#endif // COOP_ENABLED
            break;
        case F_DRESS: {
            if( roll_butchery() < 0 ) {
                switch( rng( 1, 3 ) ) {
                    case 1:
                        p.add_msg_if_player(
                            m_warning,
                            _( "You unskillfully hack up the corpse and "
                               "chop off some excess body parts.  You're "
                               "left wondering how you did so poorly." ) );
                        break;
                    case 2:
                        p.add_msg_if_player(
                            m_warning,
                            _( "Your unskilled hands slip and damage the "
                               "corpse.  You still hope it's not a total "
                               "waste though." ) );
                        break;
                    case 3:
                        p.add_msg_if_player(
                            m_warning,
                            _( "You did something wrong and hacked the "
                               "corpse badly.  Maybe it's still "
                               "recoverable." ) );
                        break;
                }
                corpse_item.set_flag( flag_FIELD_DRESS_FAILED );
            } else {
                switch( rng( 1, 3 ) ) {
                    case 1:
                        p.add_msg_if_player( m_good, _( "You field dress the %s." ), corpse->nname() );
                        break;
                    case 2:
                        p.add_msg_if_player(
                            m_good,
                            _( "You slice the corpse's belly and remove "
                               "intestines and organs, until you're "
                               "confident that it will not rot from "
                               "inside." ) );
                        break;
                    case 3:
                        p.add_msg_if_player(
                            m_good,
                            _( "You remove guts and excess parts, preparing "
                               "the corpse for later use." ) );
                        break;
                }
                corpse_item.set_flag( flag_FIELD_DRESS );
            }
            here.add_splatter( type_gib, p.bub_pos(), rng( corpse->size + 2, ( corpse->size + 1 ) * 2 ) );
            here.add_splatter(
                type_blood, p.bub_pos(), rng( corpse->size + 2, ( corpse->size + 1 ) * 2 ) );
            for( int i = 1; i <= corpse->size; i++ ) {
                here.add_splatter_trail(
                    type_gib, p.bub_pos(),
                    random_entry( here.points_in_radius( p.bub_pos(), corpse->size + 1 ) ) );
                here.add_splatter_trail(
                    type_blood, p.bub_pos(),
                    random_entry( here.points_in_radius( p.bub_pos(), corpse->size + 1 ) ) );
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
                    p.add_msg_if_player(
                        m_good, _( "You carefully remove the hide from the %s" ), corpse->nname() );
                    break;
                case 3:
                    p.add_msg_if_player(
                        m_good,
                        _( "The %s is challenging to skin, but you get a good hide from it." ),
                        corpse->nname() );
                    break;
                case 4:
                    p.add_msg_if_player(
                        m_good, _( "With a few deft slices you take the skin from the %s" ),
                        corpse->nname() );
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
                    p.add_msg_if_player(
                        m_good, _( "You lop the limbs off the %s." ), corpse_item.tname() );
                    break;
                case 3:
                    p.add_msg_if_player(
                        m_good, _( "You cleave the %s into pieces." ), corpse_item.tname() );
                    break;
            }
            target->detach();
#ifdef COOP_ENABLED
            coop_corpse_detached = true;
#endif // COOP_ENABLED
            break;
        case DISSECT:
            p.add_msg_if_player( m_good, _( "You finish dissecting the %s." ), corpse_item.tname() );
            target->detach();
#ifdef COOP_ENABLED
            coop_corpse_detached = true;
#endif // COOP_ENABLED
            break;
    }

    this->progress.pop();
    this->targets.pop_back();

    if( !this->targets.empty() && setup_next_target( act, p ) ) { return; }

#ifdef COOP_ENABLED
    if( g->coop_client_ && coop_corpse_detached ) {
        std::ostringstream ctx;
        JsonOut j( ctx );
        j.start_object();
        j.member( "ax", this->placement.x() );
        j.member( "ay", this->placement.y() );
        j.member( "az", this->placement.z() );
        j.end_object();
        g->coop_client_->queue_action( "BUTCHER", ctx.str() );
        const auto new_abs = here.bub_to_abs( p.bub_pos() );
        for( const item * it : here.i_at( p.bub_pos() ) ) {
            if( std::ranges::find( coop_items_before, it ) == coop_items_before.end() ) {
                std::ostringstream drop_ctx;
                JsonOut jd( drop_ctx );
                jd.start_object();
                jd.member( "ax", new_abs.x() );
                jd.member( "ay", new_abs.y() );
                jd.member( "az", new_abs.z() );
                jd.member( "item" );
                it->serialize( jd );
                jd.end_object();
                g->coop_client_->queue_action( "DROP", drop_ctx.str() );
            }
        }
    }
#endif // COOP_ENABLED
    act.set_to_null();
    activity_handlers::resume_for_multi_activities( p );
}

void butchery_activity_actor::serialize( JsonOut& jsout ) const
{
    jsout.start_object();
    jsout.member( "progress", progress );
    jsout.member( "type", static_cast<int>( type ) );
    jsout.member( "targets", targets );
    jsout.member( "placement", placement );
    jsout.end_object();
}

std::unique_ptr<activity_actor> butchery_activity_actor::deserialize( JsonIn& jsin )
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
