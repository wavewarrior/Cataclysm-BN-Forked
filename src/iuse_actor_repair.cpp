#include "iuse_actor.h"

#include "action.h"
#include "active_tile_data_def.h"
#include "activity_actor_definitions.h"
#include "activity_handlers.h"
#include "addiction.h"
#include "ammo.h"
#include "animation.h"
#include "assign.h"
#include "avatar.h"
#include "avatar_functions.h"
#include "bionics.h"
#include "bodypart.h"
#include "cached_options.h"
#include "calendar.h"
#include "cata_utility.h"
#include "catalua_hooks.h"
#include "catalua_icallback_actor.h"
#include "catalua_sol.h"
#include "character.h"
#include "character_functions.h"
#include "character_id.h"
#include "cloning_utils.h"
#include "clothing_mod.h"
#include "crafting.h"
#include "creature.h"
#include "debug.h"
#include "dimension_info.h"
#include "effect.h"
#include "enum_conversions.h"
#include "enums.h"
#include "explosion.h"
#include "field_type.h"
#include "flag.h"
#include "flat_set.h"
#include "game.h"
#include "game_inventory.h"
#include "handle_liquid.h"
#include "hsv_color.h"
#include "iexamine.h"
#include "int_id.h"
#include "inventory.h"
#include "item.h"
#include "item_contents.h"
#include "item_factory.h"
#include "item_group.h"
#include "item_reload_option.h"
#include "itype.h"
#include "json.h"
#include "line.h"
#include "locations.h"
#include "magic.h"
#include "map.h"
#include "map_iterator.h"
#include "map_selector.h"
#include "map_utils.h"
#include "mapdata.h"
#include "material.h"
#include "memory_fast.h"
#include "messages.h"
#include "monster.h"
#include "morale_types.h"
#include "mtype.h"
#include "mutation.h"
#include "npc.h"
#include "options.h"
#include "output.h"
#include "overmap.h"
#include "overmap_special.h"
#include "overmap_ui.h"
#include "overmapbuffer.h"
#include "player.h"
#include "player_activity.h"
#include "pldata.h"
#include "popup.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "requirements.h"
#include "rng.h"
#include "skill.h"
#include "sounds.h"
#include "string_formatter.h"
#include "string_input_popup.h"
#include "string_utils.h"
#include "submap_load_manager.h"
#include "text_snippets.h"
#include "translations.h"
#include "trap.h"
#include "type_id.h"
#include "ui.h"
#include "uistate.h"
#include "units_utility.h"
#include "value_ptr.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vehicle_selector.h"
#include "visitable.h"
#include "vitamin.h"
#include "vpart_position.h"
#include "vpart_range.h"
#include "weather.h"
#include "world_type.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iterator>
#include <list>
#include <memory>
#include <ranges>
#include <ret_val.h>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

static const activity_id ACT_FIRSTAID( "ACT_FIRSTAID" );
static const activity_id ACT_MAKE_ZLAVE( "ACT_MAKE_ZLAVE" );
static const activity_id ACT_RELOAD( "ACT_RELOAD" );
static const activity_id ACT_REPAIR_ITEM( "ACT_REPAIR_ITEM" );
static const activity_id ACT_SPELLCASTING( "ACT_SPELLCASTING" );
static const activity_id ACT_STUDY_SPELL( "ACT_STUDY_SPELL" );
static const activity_id ACT_START_FIRE( "ACT_START_FIRE" );
static const activity_id ACT_VIBE( "ACT_VIBE" );

static const efftype_id effect_accumulated_mutagen( "accumulated_mutagen" );
static const efftype_id effect_asthma( "asthma" );
static const efftype_id effect_bandaged( "bandaged" );
static const efftype_id effect_bite( "bite" );
static const efftype_id effect_cig( "cig" );
static const efftype_id effect_bleed( "bleed" );
static const efftype_id effect_pet( "pet" );
static const efftype_id effect_disinfected( "disinfected" );
static const efftype_id effect_downed( "downed" );
static const efftype_id effect_infected( "infected" );
static const efftype_id effect_hallu( "hallu" );
static const efftype_id effect_music( "music" );
static const efftype_id effect_playing_instrument( "playing_instrument" );
static const efftype_id effect_recover( "recover" );
static const efftype_id effect_run( "run" );
static const efftype_id effect_sleep( "sleep" );
static const efftype_id effect_stunned( "stunned" );
static const efftype_id effect_visuals( "visuals" );

static const fault_id fault_bionic_nonsterile( "fault_bionic_nonsterile" );

static const bionic_id bio_syringe( "bio_syringe" );

static const itype_id itype_barrel_small( "barrel_small" );
static const itype_id itype_brazier( "brazier" );
static const itype_id itype_char_smoker( "char_smoker" );
static const itype_id itype_fire( "fire" );
static const itype_id itype_stock_small( "stock_small" );
static const itype_id itype_syringe( "syringe" );
static const itype_id itype_fertilizer( "fertilizer" );
static const itype_id itype_genome_drive( "genome_drive" );
static const itype_id itype_usb_drive( "usb_drive" );
static const flag_id flag_genome_drive( "GENOME_DRIVE" );
static const itype_id itype_mutagen( "mutagen" );
static const itype_id itype_biomaterial( "biomaterial" );

static const skill_id skill_fabrication( "fabrication" );
static const skill_id skill_firstaid( "firstaid" );
static const skill_id skill_survival( "survival" );

static const species_id HUMAN( "HUMAN" );
static const species_id ZOMBIE( "ZOMBIE" );

static const trait_id trait_CENOBITE( "CENOBITE" );
static const trait_id trait_DEBUG_BIONICS( "DEBUG_BIONICS" );
static const trait_id trait_TOLERANCE( "TOLERANCE" );
static const trait_id trait_INFRESIST( "INFRESIST" );
static const trait_id trait_LIGHTWEIGHT( "LIGHTWEIGHT" );
static const trait_id trait_PACIFIST( "PACIFIST" );
static const trait_id trait_PSYCHOPATH( "PSYCHOPATH" );
static const trait_id trait_PYROMANIA( "PYROMANIA" );
static const trait_id trait_NOPAIN( "NOPAIN" );
static const trait_id trait_MASOCHIST( "MASOCHIST" );
static const trait_id trait_MASOCHIST_MED( "MASOCHIST_MED" );
static const trait_id trait_MUT_JUNKIE( "MUT_JUNKIE" );
static const trait_id trait_SAPIOVORE( "SAPIOVORE" );

static const trait_flag_str_id trait_flag_PRED1( "PRED1" );
static const trait_flag_str_id trait_flag_PRED2( "PRED2" );
static const trait_flag_str_id trait_flag_PRED3( "PRED3" );
static const trait_flag_str_id trait_flag_PRED4( "PRED4" );

static const itype_id itype_UPS( "UPS" );

static const mtype_id mon_hallu_multicooker( "mon_hallu_multicooker" );


static const species_id species_HALLUCINATION( "HALLUCINATION" );
static const species_id species_ROBOT( "ROBOT" );
static const species_id species_ZOMBIE( "ZOMBIE" );
static const species_id species_NETHER( "NETHER" );
static const species_id species_SKELETON( "SKELETON" );

static const flag_id flag_NO_PAINT( "NO_PAINT" );

class npc;


static effect_data load_effect_data( const JsonObject& e )
{
    time_duration time;
    if( e.has_string( "duration" ) ) {
        time = read_from_json_string<time_duration>( *e.get_raw( "duration" ), time_duration::units );
    } else {
        time = time_duration::from_turns( e.get_int( "duration", 0 ) );
    }
    if( e.get_bool( "permanent", false ) ) {
        effect_data
        ret( efftype_id( e.get_string( "id" ) ), time,
             get_body_part_token( e.get_string( "bp", "NUM_BP" ) ) );
        ret.permanent = true;
        if( json_report_strict ) {
            try {
                e.throw_error( "Effect permanence has been moved to effect_type.  Set permanence "
                               "there.",
                               "permanent" );
            } catch( const JsonError& ex ) { debugmsg( "\n%s", ex.what() ); }
        }
        return ret;
    } else {
        return effect_data(
                   efftype_id( e.get_string( "id" ) ), time,
                   get_body_part_token( e.get_string( "bp", "NUM_BP" ) ) );
    }
}

void repair_item_actor::load( const JsonObject& obj )
{
    // Mandatory:
    for( const std::string line : obj.get_array( "materials" ) ) { materials.emplace( line ); }

    // TODO: Make skill non-mandatory while still erroring on invalid skill
    const std::string skill_string = obj.get_string( "skill" );
    used_skill = skill_id( skill_string );
    if( !used_skill.is_valid() ) { obj.throw_error( "Invalid skill", "skill" ); }

    cost_scaling = obj.get_float( "cost_scaling" );

    // Optional
    tool_quality = obj.get_int( "tool_quality", 0 );
    move_cost = obj.get_int( "move_cost", 500 );
    trains_skill_to = obj.get_int( "trains_skill_to", 5 ) - 1;
}

bool repair_item_actor::can_use_tool( const player& p, const item& tool, bool print_msg ) const
{
    if( p.is_underwater() ) {
    if( print_msg ) { p.add_msg_if_player( m_info, _( "You can't do that while underwater." ) ); }
        return false;
    }
    if( p.is_mounted() ) {
    if( print_msg ) {
            p.add_msg_player_or_npc(
                m_bad, _( "You can't do that while mounted." ),
                _( "<npcname> can't do that while mounted." ) );
        }
        return false;
    }
    if( !character_funcs::can_see_fine_details( p ) ) {
    if( print_msg ) { p.add_msg_if_player( m_info, _( "You can't see to do that!" ) ); }
        return false;
    }
    if( !tool.units_sufficient( p ) ) {
    if( print_msg ) {
            p.add_msg_if_player( m_info, _( "Your tool does not have enough charges to do that." ) );
        }
        return false;
    }

    return true;
}

int repair_item_actor::use( player& p, item& it, bool, const tripoint_bub_ms & ) const
{
    if( !can_use_tool( p, it, true ) ) { return 0; }

p.assign_activity( std::make_unique<player_activity>(
                       std::make_unique<repair_item_activity_actor>( safe_reference<item>( it ), type ) ) );
    return 0;
}

std::unique_ptr<iuse_actor> repair_item_actor::clone() const
{
    return std::make_unique<repair_item_actor>( *this );
}

bool repair_item_actor::handle_components(
    player& pl, const item& fix, bool print_msg, bool just_check ) const
{
    // Entries valid for repaired items
    std::set<material_id> valid_entries;
    for( const auto& mat : materials ) {
        if( fix.made_of( mat ) ) { valid_entries.insert( mat ); }
    }

    if( valid_entries.empty() ) {
        if( print_msg ) {
            pl.add_msg_if_player( m_info, _( "Your %s is not made of any of:" ), fix.tname() );
            for( const auto& mat_name : materials ) {
                const auto& mat = mat_name.obj();
                pl.add_msg_if_player(
                    m_info, _( "%s (repaired using %s)" ), mat.name(),
                    item::nname( mat.repaired_with(), 2 ) );
            }
        }

        return false;
    }

    const inventory& crafting_inv = pl.crafting_inventory();

    // Repairing or modifying items requires at least 1 repair item,
    //  otherwise number is related to size of item
    // Round up if checking, but roll if actually consuming
    // TODO: should 250_ml be part of the cost_scaling?
    const int items_needed = std::max <
                             int > ( 1, just_check ? std::ceil( fix.volume() / 250_ml * cost_scaling )
                                     : roll_remainder( fix.volume() / 250_ml * cost_scaling ) );


    // Go through all discovered repair items and see if we have any of them available
    std::vector<item_comp> comps;
    for( const auto& entry : valid_entries ) {
        const itype_id& component_id = entry.obj().repaired_with();
        // Certain (different!) materials are repaired with the same components (steel, iron, hard
        // steel use scrap metal). This checks avoids adding the same component twice, which is
        // annoying to the user.
        if( std::find_if( comps.begin(), comps.end(),
        [&]( const item_comp & ic ) { return ic.type == component_id; } )
    != comps.end() ) {
            continue;
        }
        if( item::count_by_charges( component_id ) ) {
            if( crafting_inv.has_charges( component_id, items_needed ) ) {
                comps.emplace_back( component_id, items_needed );
            }
        } else if(
            crafting_inv.has_amount( component_id, items_needed, false, is_crafting_component ) ) {
            comps.emplace_back( component_id, items_needed );
        }
    }

    if( comps.empty() ) {
        if( print_msg ) {
            for( const auto& entry : valid_entries ) {
                const auto& mat_comp = entry.obj().repaired_with();
                pl.add_msg_if_player(
                    m_info, _( "You don't have enough %s to do that.  Have: %d, need: %d" ),
                    item::nname( mat_comp, 2 ),
                    mat_comp->count_by_charges()
                    ? crafting_inv.amount_of( mat_comp, false )
                    : crafting_inv.charges_of( mat_comp, items_needed ),
                    items_needed );
            }
        }

        return false;
    }

    if( !just_check ) {
        if( comps.empty() ) {
            // This shouldn't happen - the check in can_repair_target should prevent it
            // But report it, just in case
            debugmsg( "Attempted repair with no components" );
        }

        pl.consume_items( comps, 1, is_crafting_component );
    }

    return true;
}

// Find the difficulty of the recipes that result in id
// If the recipe is not known by the player, +1 to difficulty
// If player doesn't meet the requirements of the recipe, +1 to difficulty
// Returns -1 if no recipe is found
static int find_repair_difficulty( const player& pl, const itype_id& id, bool training )
{
    // If the recipe is not found, this will remain unchanged
    int min = id->repair_difficulty;
    if( min != -1 ) { return min; }
    for( const auto& e : recipe_dict ) {
        const auto r = e.second;
        if( id != r.result() ) { continue; }
        // If this is the first time we found a recipe
        if( min == -1 ) { min = 5; }

        int cur_difficulty = r.difficulty;
        if( !training && !pl.knows_recipe( &r ) ) { cur_difficulty++; }

        if( !training && !pl.has_recipe_requirements( r ) ) { cur_difficulty++; }

        min = std::min( cur_difficulty, min );
    }

    return min;
}

// Returns the level of the lowest level recipe that results in item of `fix`'s type
// Or if it has a repairs_like, the lowest level recipe that results in that.
// If the recipe doesn't exist, difficulty is 10
int repair_item_actor::repair_recipe_difficulty(
    const player& pl, const item& fix, bool training ) const
{
    int diff = find_repair_difficulty( pl, fix.typeId(), training );

    // If we don't find a recipe, see if there's a repairs_like that has a recipe
    if( diff == -1 && !fix.type->repairs_like.is_empty() ) {
        diff = find_repair_difficulty( pl, fix.type->repairs_like, training );
    }

    // If we still don't find a recipe, difficulty is 10
    if( diff == -1 ) { diff = 10; }

    return diff;
}

bool repair_item_actor::can_repair_target( player& pl, const item& fix, bool print_msg ) const
{
    // In some rare cases (indices getting scrambled after inventory overflow)
    //  our `fix` can be a different item.
    if( fix.is_null() ) {
    if( print_msg ) { pl.add_msg_if_player( m_info, _( "You do not have that item!" ) ); }
        return false;
    }
    if( fix.is_firearm() ) {
    if( print_msg ) { pl.add_msg_if_player( m_info, _( "That requires gunsmithing tools." ) ); }
        return false;
    }
    if( fix.count_by_charges() || fix.has_flag( flag_NO_REPAIR ) ) {
    if( print_msg ) { pl.add_msg_if_player( m_info, _( "You cannot repair this type of item." ) ); }
        return false;
    }

    if( any_of( materials.begin(), materials.end(), [&fix]( const material_id & mat ) {
        return mat.obj().repaired_with() == fix.typeId();
    } ) ) {
        if( print_msg ) {
            pl.add_msg_if_player( m_info, _( "This can be used to repair other items, not itself." ) );
        }
        return false;
    }

    if( !handle_components( pl, fix, print_msg, true ) ) { return false; }

const bool can_be_refitted = fix.has_flag( flag_VARSIZE );
if( can_be_refitted && !fix.has_flag( flag_FIT ) ) { return true; }

const bool resizing_matters = fix.get_sizing( pl ) != item::sizing::ignore;
const bool small = pl.get_size() == creature_size::tiny;
const bool can_resize = small != fix.has_flag( flag_UNDERSIZE );
if( can_be_refitted && resizing_matters && can_resize ) { return true; }

if( fix.damage() > 0 ) { return true; }

if( fix.damage() <= fix.min_damage() ) {
    if( print_msg ) {
            pl.add_msg_if_player(
                m_info, _( "Your %s is already enhanced to its maximum potential." ), fix.tname() );
        }
        return false;
    }

    if( fix.has_flag( flag_PRIMITIVE_RANGED_WEAPON ) || !fix.reinforceable() ) {
    if( print_msg ) {
            pl.add_msg_if_player(
                m_info, _( "You cannot improve your %s any more this way." ), fix.tname() );
        }
        return false;
    }

    return true;
}

std::pair<float, float> repair_item_actor::repair_chance(
    const player& pl, const item& fix, repair_item_actor::repair_type action_type ) const
{
    /** @EFFECT_TAILOR randomly improves clothing repair efforts */
    /** @EFFECT_MECHANICS randomly improves metal repair efforts */
    const int skill = pl.get_skill_level( used_skill );
    const int recipe_difficulty = repair_recipe_difficulty( pl, fix );
    int action_difficulty = 0;
    switch( action_type ) {
        case RT_REPAIR:
            action_difficulty = fix.damage_level( 4 );
            break;
        case RT_REFIT:
            // Let's make refitting as hard as recovering an almost-wrecked item
            action_difficulty = fix.max_damage() / itype::damage_scale;
            break;
        case RT_REINFORCE:
            // Reinforcing is 50% harder than refitting
            action_difficulty = ( fix.max_damage() / itype::damage_scale ) + 2;
            break;
        case RT_PRACTICE:
            // Skill gain scales with recipe difficulty, so practice difficulty should too
            action_difficulty = recipe_difficulty;
        default:
            ;
    }

    const int difficulty = recipe_difficulty + action_difficulty;
    float success_chance =
        ( 10 + 2 * ( skill * ( 1 + tool_quality / 10.0f ) ) - 2 * difficulty ) / 100.0f;
    /** @EFFECT_DEX reduces the chances of damaging an item when repairing */
    float damage_chance =
        ( difficulty - ( skill * ( 1 + tool_quality / 10.0f ) ) - pl.dex_cur / 5.0f ) / 100.0f;

    damage_chance = std::max( 0.0f, std::min( 1.0f, damage_chance ) );
    success_chance = std::max( 0.0f, std::min( 1.0f - damage_chance, success_chance ) );

    return std::make_pair( success_chance, damage_chance );
}

repair_item_actor::repair_type repair_item_actor::default_action(
    const item& fix, int current_skill_level ) const
{
    if( fix.damage() > 0 ) { return RT_REPAIR; }

const bool can_be_refitted = fix.has_flag( flag_VARSIZE );
const bool doesnt_fit = !fix.has_flag( flag_FIT );
if( doesnt_fit && can_be_refitted ) { return RT_REFIT; }

Character& player_character = get_player_character();
const bool smol = player_character.get_size() == creature_size::tiny;

const bool is_undersized = fix.has_flag( flag_UNDERSIZE );
const bool is_oversized = fix.has_flag( flag_OVERSIZE );
const bool resizing_matters = fix.get_sizing( player_character ) != item::sizing::ignore;

const bool too_big_while_smol = smol && !is_undersized && !is_oversized;
if( too_big_while_smol && can_be_refitted && resizing_matters ) { return RT_DOWNSIZING; }

const bool too_small_while_big = !smol && is_undersized && !is_oversized;
if( too_small_while_big && can_be_refitted && resizing_matters ) { return RT_UPSIZING; }

if( fix.damage() > fix.min_damage() ) { return RT_REINFORCE; }

if( current_skill_level <= trains_skill_to ) { return RT_PRACTICE; }

return RT_NOTHING;
}

static bool damage_item( player& pl, item* fix )
{
    const std::string startdurability = fix->durability_indicator( true );
    const auto destroyed = fix->inc_damage();
    const std::string resultdurability = fix->durability_indicator( true );
    pl.add_msg_if_player(
        m_bad, _( "You damage your %s!  ( %s-> %s)" ), fix->tname( 1, false ), startdurability,
        resultdurability );
    if( destroyed ) {

        // Dump its contents on the ground
        // Destroy irremovable mods, if any
        fix->contents.remove_top_items_with( []( detached_ptr<item>&& mod ) {
            if( mod->is_gunmod() && !mod->is_irremovable() ) { return detached_ptr<item>(); }
            return std::move( mod );
        } );

        fix->contents.spill_contents( fix->position() );

        pl.add_msg_if_player( m_bad, _( "You destroy it!" ) );
        if( fix->where() == item_location_type::character ) {
            pl.i_rem_keep_contents( pl.get_item_position( fix ) );
        } else {
            for( detached_ptr<item> &it : fix->contents.clear_items() ) {
                put_into_vehicle_or_drop(
                    pl, item_drop_reason::deliberate, std::move( it ), fix->position() );
            }
            fix->detach();
        }

        return true;
    }

    return false;
}

repair_item_actor::attempt_hint repair_item_actor::repair( player& pl, item& tool,
        item& fix ) const
{
    if( !can_use_tool( pl, tool, true ) ) { return AS_CANT_USE_TOOL; }
if( !can_repair_target( pl, fix, true ) ) { return AS_CANT; }

const int current_skill_level = pl.get_skill_level( used_skill );
const auto action = default_action( fix, current_skill_level );
const auto chance = repair_chance( pl, fix, action );
int practice_amount = std::max( repair_recipe_difficulty( pl, fix, true ), 1 );
float roll_value = rng_float( 0.0, 1.0 );
enum roll_result { SUCCESS, FAILURE, NEUTRAL } roll;

if( roll_value > 1.0f - chance.second ) {
    roll = FAILURE;
} else if( roll_value < chance.first ) {
    roll = SUCCESS;
} else {
    roll = NEUTRAL;
}

if( action == RT_NOTHING ) {
    pl.add_msg_if_player( m_bad, _( "You won't learn anything more by doing that." ) );
        return AS_CANT;
    }

    // If not for this if, it would spam a lot
    if( current_skill_level > trains_skill_to ) { practice_amount = 0; }
pl.practice( used_skill, practice_amount, trains_skill_to );

if( roll == FAILURE ) { return damage_item( pl, &fix ) ? AS_DESTROYED : AS_FAILURE; }

    if( action == RT_PRACTICE ) { return AS_RETRY; }

if( action == RT_REPAIR ) {
    if( roll == SUCCESS ) {
            const std::string startdurability = fix.durability_indicator( true );
            const auto damage = fix.damage();
            handle_components( pl, fix, false, false );
            fix.set_damage( std::max( damage - itype::damage_scale, 0 ) );
            const std::string resultdurability = fix.durability_indicator( true );
            if( damage > itype::damage_scale ) {
                pl.add_msg_if_player(
                    m_good, _( "You repair your %s!  ( %s-> %s)" ), fix.tname( 1, false ),
                    startdurability, resultdurability );
            } else {
                pl.add_msg_if_player(
                    m_good, _( "You repair your %s completely!  ( %s-> %s)" ), fix.tname( 1, false ),
                    startdurability, resultdurability );
            }
            // Lua iequippable on_repair callback
            if( const auto * iequip_cb = fix.type->iequippable_callbacks ) {
                iequip_cb->call_on_repair( pl, fix );
            }
            return AS_SUCCESS;
        }

        return AS_RETRY;
    }

    if( action == RT_REFIT ) {
    if( roll == SUCCESS ) {
            if( !fix.has_flag( flag_FIT ) ) {
                pl.add_msg_if_player(
                    m_good, _( "You take your %s in, improving the fit." ), fix.tname() );
                fix.set_flag( flag_FIT );
            }
            handle_components( pl, fix, false, false );
            // Lua iequippable on_repair callback
            if( const auto * iequip_cb = fix.type->iequippable_callbacks ) {
                iequip_cb->call_on_repair( pl, fix );
            }
            return AS_SUCCESS;
        }

        return AS_RETRY;
    }

    if( action == RT_DOWNSIZING ) {
    // We don't need to check for smallness or undersize because DOWNSIZING already guarantees
    // that
    if( roll == SUCCESS ) {
            pl.add_msg_if_player(
                m_good, _( "You resize the %s to accommodate your tiny build." ),
                fix.tname().c_str() );
            fix.set_flag( flag_UNDERSIZE );
            handle_components( pl, fix, false, false );
            // Lua iequippable on_repair callback
            if( const auto * iequip_cb = fix.type->iequippable_callbacks ) {
                iequip_cb->call_on_repair( pl, fix );
            }
            return AS_SUCCESS;
        }
        return AS_RETRY;
    }

    if( action == RT_UPSIZING ) {
    // We don't need to check for smallness or undersize because UPSIZING already guarantees
    // that
    if( roll == SUCCESS ) {
            pl.add_msg_if_player(
                m_good, _( "You adjust the %s back to its normal size." ), fix.tname().c_str() );
            fix.unset_flag( flag_UNDERSIZE );
            handle_components( pl, fix, false, false );
            // Lua iequippable on_repair callback
            if( const auto * iequip_cb = fix.type->iequippable_callbacks ) {
                iequip_cb->call_on_repair( pl, fix );
            }
            return AS_SUCCESS;
        }
        return AS_RETRY;
    }

    if( action == RT_REINFORCE ) {
    if( fix.has_flag( flag_PRIMITIVE_RANGED_WEAPON ) || !fix.reinforceable() ) {
            pl.add_msg_if_player(
                m_info, _( "You cannot improve your %s any more this way." ), fix.tname() );
            return AS_CANT;
        }

        if( roll == SUCCESS ) {
            pl.add_msg_if_player( m_good, _( "You make your %s extra sturdy." ), fix.tname() );
            fix.mod_damage( -itype::damage_scale );
            handle_components( pl, fix, false, false );
            // Lua iequippable on_repair callback
            if( const auto * iequip_cb = fix.type->iequippable_callbacks ) {
                iequip_cb->call_on_repair( pl, fix );
            }
            return AS_SUCCESS;
        }

        return AS_RETRY;
    }

    pl.add_msg_if_player( m_info, _( "Your %s is already enhanced." ), fix.tname() );
    return AS_CANT;
}

std::string repair_item_actor::action_description( repair_item_actor::repair_type rt )
{
    static const std::array<std::string, NUM_REPAIR_TYPES> arr = {
        {
            translate_marker( "Nothing" ), translate_marker( "Repairing" ), translate_marker( "Refitting" ),
            translate_marker( "Downsizing" ), translate_marker( "Upsizing" ),
            translate_marker( "Reinforcing" ), translate_marker( "Practicing" )
        }
    };

    return _( arr[rt] );
}

std::string repair_item_actor::get_name() const
{
    const std::string mats =
        enumerate_as_string( materials.begin(), materials.end(), []( const material_id & mid ) {
        return _( mid->name() );
    } );
    return string_format( _( "Repair %s" ), mats );
}

void heal_actor::load( const JsonObject& obj )
{
    // Mandatory
    move_cost = obj.get_int( "move_cost" );
    limb_power = obj.get_float( "limb_power", 0 );

    // Optional
    bandages_power = obj.get_float( "bandages_power", 0 );
    bandages_scaling = obj.get_float( "bandages_scaling", 0.25f * bandages_power );
    disinfectant_power = obj.get_float( "disinfectant_power", 0 );
    disinfectant_scaling = obj.get_float( "disinfectant_scaling", 0.25f * disinfectant_power );

    head_power = obj.get_float( "head_power", 0.8f * limb_power );
    torso_power = obj.get_float( "torso_power", 1.5f * limb_power );

    limb_scaling = obj.get_float( "limb_scaling", 0.25f * limb_power );
    double scaling_ratio =
        limb_power < 0.0001f ? 0.0 : static_cast<double>( limb_scaling / limb_power );
    head_scaling = obj.get_float( "head_scaling", scaling_ratio * head_power );
    torso_scaling = obj.get_float( "torso_scaling", scaling_ratio * torso_power );

    bleed = obj.get_float( "bleed", 0.0f );
    bite = obj.get_float( "bite", 0.0f );
    infect = obj.get_float( "infect", 0.0f );

    long_action = obj.get_bool( "long_action", false );

    if( obj.has_array( "effects" ) ) {
        for( const JsonObject e : obj.get_array( "effects" ) ) {
            effects.push_back( load_effect_data( e ) );
        }
    }

    if( obj.has_string( "used_up_item" ) ) {
        obj.read( "used_up_item", used_up_item_id, true );
    } else if( obj.has_object( "used_up_item" ) ) {
        JsonObject u = obj.get_object( "used_up_item" );
        u.read( "id", used_up_item_id, true );
        used_up_item_quantity = u.get_int( "quantity", used_up_item_quantity );
        used_up_item_charges = u.get_int( "charges", used_up_item_charges );
        used_up_item_flags = u.get_tags<flag_id>( "flags" );
    }
}

static player &get_patient( player& healer, const tripoint_bub_ms& pos )
{
    if( healer.bub_pos() == pos ) { return healer; }

    player* const person = g->critter_at<player>( pos );
    if( !person ) {
        // Default to heal self on failure not to break old functionality
        add_msg( m_debug, "No heal target at position %d,%d,%d", pos.x(), pos.y(), pos.z() );
        return healer;
    }

    return *person;
}

int heal_actor::use( player& p, item& it, bool, const tripoint_bub_ms& pos ) const
{
    if( p.is_underwater() ) {
    p.add_msg_if_player( m_info, _( "You can't do that while underwater." ) );
        return 0;
    }
    if( p.is_mounted() ) {
    p.add_msg_if_player( m_info, _( "You can't do that while mounted." ) );
        return 0;
    }

    player& patient = get_patient( p, pos );
    const bodypart_str_id hpp = use_healing_item( p, patient, it, false );
    if( !hpp ) { return 0; }

int cost = move_cost;
if( long_action ) {
    // A hack: long action healing on NPCs isn't done yet.
    // So just heal at start and paralyze the player for 5 minutes.
    cost /= std::min( 10, p.get_skill_level( skill_firstaid ) + 1 );
    }

    // NPCs can use first aid now, but they can't perform long actions
    if( long_action && &patient == &p && !p.is_npc() ) {
    // Assign first aid long action.
    /** @EFFECT_FIRSTAID speeds up firstaid activity */
    p.assign_activity( std::make_unique<player_activity>(
                           std::make_unique<firstaid_activity_actor>( &it, hpp.str(), cost ) ) );
        p.moves = 0;
        return 0;
    }

    p.moves -= cost;
    p.add_msg_if_player( m_good, _( "You use your %s." ), it.tname() );
    return it.type->charges_to_use();
}

std::unique_ptr<iuse_actor> heal_actor::clone() const
{
    return std::make_unique<heal_actor>( *this );
}

int heal_actor::get_heal_value( const Character& healer, const bodypart_str_id& healed ) const
{
    int heal_base;
    float bonus_mult;
    if( healed == body_part_head ) {
        heal_base = head_power;
        bonus_mult = head_scaling;
    } else if( healed == body_part_torso ) {
        heal_base = torso_power;
        bonus_mult = torso_scaling;
    } else {
        heal_base = limb_power;
        bonus_mult = limb_scaling;
    }

    if( heal_base > 0 ) {
        /** @EFFECT_FIRSTAID increases healing item effects */
        return heal_base + bonus_mult * healer.get_skill_level( skill_firstaid );
    }

    return heal_base;
}

int heal_actor::get_bandaged_level( const Character& healer ) const
{
    if( bandages_power > 0 ) {
    /** @EFFECT_FIRSTAID increases healing item effects */
    return bandages_power + bandages_scaling * healer.get_skill_level( skill_firstaid );
    }

    return bandages_power;
}

int heal_actor::get_disinfected_level( const Character& healer ) const
{
    if( disinfectant_power > 0 ) {
    /** @EFFECT_FIRSTAID increases healing item effects */
    return disinfectant_power + disinfectant_scaling * healer.get_skill_level( skill_firstaid );
    }

    return disinfectant_power;
}

int heal_actor::finish_using(
    player& healer, player& patient, item& it, const bodypart_str_id& healed ) const
{
    float practice_amount = limb_power * 3.0f;
    const int dam = get_heal_value( healer, healed );

    const bodypart_id bp = healed.id();
    const int cur_hp = patient.get_part_hp_cur( bp );

    if( ( cur_hp >= 1 ) && ( dam > 0 ) ) { // Prevent first-aid from mending limbs
        patient.heal( bp, dam );
    } else if( ( cur_hp >= 1 ) && ( dam < 0 ) ) {
        patient.apply_damage( nullptr, bp, -dam ); // hurt takes + damage
    }

    const bodypart_str_id bp_healed = healed;

    Character& player_character = get_player_character();
    const bool u_see =
        healer.is_player() || patient.is_player() || player_character.sees( healer )
        || player_character.sees( patient );
    const bool player_healing_player = healer.is_player() && patient.is_player();
    // Need a helper here - messages are from healer's point of view
    // but it would be cool if NPCs could use this function too
    const auto heal_msg =
    [&]( game_message_type msg_type, const char* player_player_msg, const char* other_msg ) {
        if( !u_see ) { return; }

        if( player_healing_player ) {
            add_msg( msg_type, player_player_msg );
        } else {
            add_msg( msg_type, other_msg );
        }
    };

    if( patient.has_effect( effect_bleed, bp_healed ) ) {
        if( x_in_y( bleed, 1.0f ) ) {
            patient.remove_effect( effect_bleed, bp_healed );
            heal_msg( m_good, _( "You stop the bleeding." ), _( "The bleeding is stopped." ) );
        } else {
            heal_msg( m_warning, _( "You fail to stop the bleeding." ), _( "The wound still bleeds." ) );
        }

        practice_amount += bleed * 3.0f;
    }
    if( patient.has_effect( effect_bite, bp_healed ) ) {
        if( x_in_y( bite, 1.0f ) ) {
            patient.remove_effect( effect_bite, bp_healed );
            heal_msg( m_good, _( "You clean the wound." ), _( "The wound is cleaned." ) );
        } else {
            heal_msg( m_warning, _( "Your wound still aches." ), _( "The wound still looks bad." ) );
        }

        practice_amount += bite * 3.0f;
    }
    if( patient.has_effect( effect_infected, bp_healed ) ) {
        if( x_in_y( infect, 1.0f ) ) {
            const time_duration infected_dur = patient.get_effect_dur( effect_infected, bp_healed );
            patient.remove_effect( effect_infected, bp_healed );
            patient.add_effect( effect_recover, infected_dur );
            heal_msg( m_good, _( "You disinfect the wound." ), _( "The wound is disinfected." ) );
        } else {
            heal_msg( m_warning, _( "Your wound still hurts." ), _( "The wound still looks nasty." ) );
        }

        practice_amount += infect * 10.0f;
    }

    if( long_action ) { healer.add_msg_if_player( _( "You finish using the %s." ), it.tname() ); }

    for( const auto& eff : effects ) {
        patient.add_effect( eff.id, eff.duration, convert_bp( eff.bp ) );
        if( eff.permanent ) { patient.get_effect( eff.id, convert_bp( eff.bp ) ).set_permanent(); }
    }

    const auto copy_flags = [&]( item & it ) {
        for( const auto& flag : used_up_item_flags ) { it.set_flag( flag ); }
    };

    if( !used_up_item_id.is_empty() ) {
        item* used_up = item::spawn_temporary( used_up_item_id, it.birthday() );
        used_up->charges = used_up_item_charges;
        copy_flags( *used_up );
        for( int count = 0; count < used_up_item_quantity; count++ ) {
            healer.i_add_or_drop( item::spawn( *used_up ) );
        }
    }

    // apply healing over time effects
    if( bandages_power > 0 ) {
        int bandages_intensity = get_bandaged_level( healer );
        patient.add_effect( effect_bandaged, 1_turns, bp_healed );
        effect& e = patient.get_effect( effect_bandaged, bp_healed );
        e.set_duration( e.get_int_dur_factor() * bandages_intensity );
        patient.get_part( healed ).set_damage_bandaged(
                   patient.get_part_hp_max( bp ) - patient.get_part_hp_cur( bp ) );
        practice_amount += 3 * bandages_intensity;
    }
    if( disinfectant_power > 0 ) {
        int disinfectant_intensity = get_disinfected_level( healer );
        patient.add_effect( effect_disinfected, 1_turns, bp_healed );
        effect& e = patient.get_effect( effect_disinfected, bp_healed );
        e.set_duration( e.get_int_dur_factor() * disinfectant_intensity );
        patient.get_part( healed ).set_damage_disinfected(
                   patient.get_part_hp_max( bp ) - patient.get_part_hp_cur( bp ) );
        practice_amount += 3 * disinfectant_intensity;
    }
    practice_amount = std::max( 9.0f, practice_amount );

    healer.practice( skill_firstaid, static_cast<int>( practice_amount ) );
    return it.type->charges_to_use();
}

static bodypart_str_id pick_part_to_heal(
    const player& healer, const player& patient, const std::string& menu_header, int limb_power,
    int head_bonus, int torso_bonus, float bleed_chance, float bite_chance, float infect_chance,
    bool force, float bandage_power, float disinfectant_power )
{
    const bool bleed = bleed_chance > 0.0f;
    const bool bite = bite_chance > 0.0f;
    const bool infect = infect_chance > 0.0f;
    /** @EFFECT_PER slightly increases precision when using first aid */
    /** @EFFECT_FIRSTAID increases precision when using first aid */
    const bool precise = ( healer.get_skill_level( skill_firstaid ) * 4 + healer.per_cur >= 20 );

    while( true ) {
        bodypart_str_id healed_part = patient.body_window(
                                          menu_header, force, precise, limb_power, head_bonus, torso_bonus, bleed_chance,
                                          bite_chance, infect_chance, bandage_power, disinfectant_power );
        if( !healed_part ) { return bodypart_str_id::NULL_ID(); }

        if( ( infect && patient.has_effect( effect_infected, healed_part ) )
            || ( bite && patient.has_effect( effect_bite, healed_part ) )
            || ( bleed && patient.has_effect( effect_bleed, healed_part ) ) ) {
            return healed_part;
        }

        if( force || patient.get_part_hp_cur( healed_part ) < patient.get_part_hp_max( healed_part ) ) {
            return healed_part;
        }
    }
}

bodypart_str_id heal_actor::use_healing_item(
    player& healer, player& patient, item& it, bool force ) const
{
    bodypart_str_id healed = bodypart_str_id::NULL_ID();
    const int head_bonus = get_heal_value( healer, body_part_head );
    const int limb_power = get_heal_value( healer, body_part_arm_l );
    const int torso_bonus = get_heal_value( healer, body_part_torso );

    if( !patient.can_use_heal_item( it ) ) {
        patient.add_msg_player_or_npc(
            m_bad, _( "Your biology is not compatible with that item." ),
            _( "<npcname>'s biology is not compatible with that item." ) );
        return bodypart_str_id::NULL_ID(); // canceled
    }

    if( healer.is_npc() ) {
        // NPCs heal whatever has sustained the most damaged that they can heal but never
        // rebandage parts
        int highest_damage = 0;
        for( const auto& part : patient.get_all_body_parts( true ) ) {
            const auto& bp = patient.get_part( part );
            int damage = 0;
            if( ( !patient.has_effect( effect_bandaged, part.id() ) && bandages_power > 0 )
                || ( !patient.has_effect( effect_disinfected, part.id() ) && disinfectant_power > 0 ) ) {
                damage += bp.get_hp_max() - bp.get_hp_cur();
                damage += damage > 0 ? bp.get_id()->essential * essential_value : 0;
                damage += bleed * patient.get_effect_dur( effect_bleed, part.id() ) / 5_minutes;
                damage += bite * patient.get_effect_dur( effect_bite, part.id() ) / 10_minutes;
                damage += infect * patient.get_effect_dur( effect_infected, part.id() ) / 10_minutes;
            }
            if( damage > highest_damage ) {
                highest_damage = damage;
                healed = part.id();
            }
        }
    } else if( patient.is_player() ) {
        // Player healing self - let player select
        if( healer.activity->id() != ACT_FIRSTAID ) {
            const std::string menu_header = _( "Select a body part for: " ) + it.tname();
            healed = pick_part_to_heal(
                         healer, patient, menu_header, limb_power, head_bonus, torso_bonus, bleed, bite,
                         infect, force, get_bandaged_level( healer ), get_disinfected_level( healer ) );
            if( !healed ) {
                add_msg( m_info, _( "Never mind." ) );
                return bodypart_str_id::NULL_ID(); // canceled
            }
        }
        // Brick healing if using a first aid kit for the first time.
        if( long_action && healer.activity->id() != ACT_FIRSTAID ) {
            // Cancel and wait for activity completion.
            return healed;
        } else if( healer.activity->id() == ACT_FIRSTAID ) {
            // Completed activity, extract body part from it.
            healed = bodypart_str_id( healer.activity->str_values[0] );
        }
    } else {
        // Player healing NPC
        // TODO: Remove this hack, allow using activities on NPCs
        const std::string menu_header = string_format(
                                            pgettext( "healing",
                                                //~ %1$s: patient name, %2$s: healing item name
                                                "Select a body part of %1$s for %2$s:" ),
                                            patient.disp_name(), it.tname() );
        healed = pick_part_to_heal(
                     healer, patient, menu_header, limb_power, head_bonus, torso_bonus, bleed, bite, infect,
                     force, get_bandaged_level( healer ), get_disinfected_level( healer ) );
    }

    if( healed ) { finish_using( healer, patient, it, healed ); }

    return healed;
}

void heal_actor::info( const item &, std::vector<iteminfo> &dump ) const
{
    if( head_power > 0 || torso_power > 0 || limb_power > 0 || bandages_power > 0
    || disinfectant_power > 0 || bleed > 0.0f || bite > 0.0f || infect > 0.0f ) {
    dump.emplace_back( "HEAL", _( "<bold>Healing effects</bold> " ) );
    }

    Character& player_character = get_player_character();
    if( head_power > 0 || torso_power > 0 || limb_power > 0 ) {
    dump.emplace_back( "HEAL", _( "Base healing: " ) );
        dump.emplace_back( "HEAL_BASE", _( "Head: " ), "", iteminfo::no_newline, head_power );
        dump.emplace_back( "HEAL_BASE", _( "  Torso: " ), "", iteminfo::no_newline, torso_power );
        dump.emplace_back( "HEAL_BASE", _( "  Limbs: " ), limb_power );
        if( g != nullptr ) {
            dump.emplace_back( "HEAL", _( "Actual healing: " ) );
            dump.emplace_back( "HEAL_ACT", _( "Head: " ), "", iteminfo::no_newline,
                               get_heal_value( player_character, body_part_head ) );
            dump.emplace_back( "HEAL_ACT", _( "  Torso: " ), "", iteminfo::no_newline,
                               get_heal_value( player_character, body_part_torso ) );
            dump.emplace_back(
                "HEAL_ACT", _( "  Limbs: " ), get_heal_value( player_character, body_part_arm_l ) );
        }
    }

    if( bandages_power > 0 ) {
    dump.emplace_back( "HEAL", _( "Base bandaging quality: " ),
                       texitify_base_healing_power( static_cast<int>( bandages_power ) ) );
        if( g != nullptr ) {
            dump.emplace_back( "HEAL", _( "Actual bandaging quality: " ),
                               texitify_healing_power( get_bandaged_level( player_character ) ) );
        }
    }

    if( disinfectant_power > 0 ) {
    dump.emplace_back( "HEAL", _( "Base disinfecting quality: " ),
                       texitify_base_healing_power( static_cast<int>( disinfectant_power ) ) );
        if( g != nullptr ) {
            dump.emplace_back( "HEAL", _( "Actual disinfecting quality: " ),
                               texitify_healing_power( get_disinfected_level( player_character ) ) );
        }
    }

    if( bleed > 0.0f || bite > 0.0f || infect > 0.0f ) {
    dump.emplace_back( "HEAL", _( "Chance to heal (percent): " ) );
        if( bleed > 0.0f ) {
            dump.emplace_back( "HEAL", _( "* Bleeding: " ), static_cast<int>( bleed * 100 ) );
        }
        if( bite > 0.0f ) { dump.emplace_back( "HEAL", _( "* Bite: " ), static_cast<int>( bite * 100 ) ); }
        if( infect > 0.0f ) {
            dump.emplace_back( "HEAL", _( "* Infection: " ), static_cast<int>( infect * 100 ) );
        }
    }

    dump.emplace_back( "HEAL", _( "Moves to use: " ), move_cost );
}
