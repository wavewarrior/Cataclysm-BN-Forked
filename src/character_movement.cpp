#include "character.h"
#include "coop_mutation_log.h"
#include "action.h"
#include "activity_actor_definitions.h"
#include "activity_handlers.h"
#include "anatomy.h"
#include "avatar.h"
#include "avatar_action.h"
#include "bionics.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "catalua_hooks.h"
#include "catalua_icallback_actor.h"
#include "catalua_sol.h"
#include "character_encumbrance.h"
#include "character_functions.h"
#include "character_martial_arts.h"
#include "character_stat.h"
#include "clothing_utils.h"
#include "clzones.h"
#include "combat_feedback.h"
#include "construction.h"
#include "consumption.h"
#include "coordinates.h"
#include "craft_command.h"
#include "creature.h"
#include "damage.h"
#include "debug.h"
#include "detached_ptr.h"
#include "disease.h"
#include "effect.h"
#include "event.h"
#include "event_bus.h"
#include "field.h"
#include "field_type.h"
#include "fire.h"
#include "flag.h"
#include "fungal_effects.h"
#include "game.h"
#include "game_constants.h"
#include "int_id.h"
#include "item_contents.h"
#include "item_hauling.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "legacy_pathfinding.h"
#include "lightmap.h"
#include "line.h"
#include "magic_enchantment.h"
#include "make_static.h"
#include "map.h"
#include "map_iterator.h"
#include "map_selector.h"
#include "mapdata.h"
#include "martialarts.h"
#include "material.h"
#include "math_defines.h"
#include "memorial_logger.h"
#include "messages.h"
#include "mission.h"
#include "monster.h"
#include "morale.h"
#include "morale_types.h"
#include "mtype.h"
#include "mutation.h"
#include "npc.h"
#include "omdata.h"
#include "options.h"
#include "output.h"
#include "overlay_ordering.h"
#include "overmapbuffer.h"
#include "player.h"
#include "player_activity.h"
#include "profession.h"
#include "profile.h"
#include "recipe_dictionary.h"
#include "regen.h"
#include "ret_val.h"
#include "rml_screen.h"
#include "rml_util.h"
#include "rng.h"
#include "scent_map.h"
#include "skill.h"
#include "skill_boost.h"
#include "sounds.h"
#include "stomach.h"
#include "string_formatter.h"
#include "string_id.h"
#include "string_utils.h"
#include "submap.h"
#include "text_snippets.h"
#include "translations.h"
#include "trap.h"
#include "ui.h"
#include "ui_manager.h"
#include "units_temperature.h"
#include "units_utility.h"
#include "value_ptr.h"
#include "veh_interact.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vehicle_selector.h"
#include "vitamin.h"
#include "vpart_position.h"
#include "vpart_range.h"
#include "weather.h"
#include "weather_gen.h"

#include <RmlUi/Core.h>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <numeric>
#include <ostream>
#include <ranges>
#include <type_traits>
#include <vector>

struct dealt_projectile_attack;

static const activity_id ACT_MOVE_ITEMS( "ACT_MOVE_ITEMS" );
static const activity_id ACT_TRAVELLING( "ACT_TRAVELLING" );
static const activity_id ACT_TREE_COMMUNION( "ACT_TREE_COMMUNION" );
static const activity_id ACT_TRY_SLEEP( "ACT_TRY_SLEEP" );
static const activity_id ACT_WAIT_STAMINA( "ACT_WAIT_STAMINA" );

static const bionic_id bio_eye_optic( "bio_eye_optic" );
static const bionic_id bio_infolink( "bio_infolink" );

static const matec_id WBLOCK_1( "WBLOCK_1" );
static const matec_id WBLOCK_2( "WBLOCK_2" );
static const matec_id WBLOCK_3( "WBLOCK_3" );

static const efftype_id effect_adrenaline( "adrenaline" );
static const efftype_id effect_ai_waiting( "ai_waiting" );
static const efftype_id effect_alarm_clock( "alarm_clock" );
static const efftype_id effect_bandaged( "bandaged" );
static const efftype_id effect_beartrap( "beartrap" );
static const efftype_id effect_bite( "bite" );
static const efftype_id effect_bleed( "bleed" );
static const efftype_id effect_blind( "blind" );
static const efftype_id effect_blisters( "blisters" );
static const efftype_id effect_bloated( "bloated" );
static const efftype_id effect_boomered( "boomered" );
static const efftype_id effect_cold( "cold" );
static const efftype_id effect_contacts( "contacts" );
static const efftype_id effect_corroding( "corroding" );
static const efftype_id effect_cough_aggravated_asthma( "cough_aggravated_asthma" );
static const efftype_id effect_cough_suppress( "cough_suppress" );
static const efftype_id effect_crushed( "crushed" );
static const efftype_id effect_darkness( "darkness" );
static const efftype_id effect_deaf( "deaf" );
static const efftype_id effect_disabled( "disabled" );
static const efftype_id effect_disinfected( "disinfected" );
static const efftype_id effect_downed( "downed" );
static const efftype_id effect_drunk( "drunk" );
static const efftype_id effect_took_antinarcoleptic( "took_antinarcoleptic" );
static const efftype_id effect_earphones( "earphones" );
static const efftype_id effect_foodpoison( "foodpoison" );
static const efftype_id effect_frostbite( "frostbite" );
static const efftype_id effect_frostbite_recovery( "frostbite_recovery" );
static const efftype_id effect_fungus( "fungus" );
static const efftype_id effect_glowing( "glowing" );
static const efftype_id effect_glowy_led( "glowy_led" );
static const efftype_id effect_got_checked( "got_checked" );
static const efftype_id effect_grabbed( "grabbed" );
static const efftype_id effect_grabbing( "grabbing" );
static const efftype_id effect_harnessed( "harnessed" );
static const efftype_id effect_heating_bionic( "heating_bionic" );
static const efftype_id effect_heavysnare( "heavysnare" );
static const efftype_id effect_hot( "hot" );
static const efftype_id effect_hot_speed( "hot_speed" );
static const efftype_id effect_in_pit( "in_pit" );
static const efftype_id effect_infected( "infected" );
static const efftype_id effect_jetinjector( "jetinjector" );
static const efftype_id effect_lack_sleep( "lack_sleep" );
static const efftype_id effect_lightsnare( "lightsnare" );
static const efftype_id effect_lying_down( "lying_down" );
static const efftype_id effect_melatonin_supplements( "melatonin" );
static const efftype_id effect_meth( "meth" );
static const efftype_id effect_masked_scent( "masked_scent" );
static const efftype_id effect_narcosis( "narcosis" );
static const efftype_id effect_nausea( "nausea" );
static const efftype_id effect_no_sight( "no_sight" );
static const efftype_id effect_onfire( "onfire" );
static const efftype_id effect_pkill1( "pkill1" );
static const efftype_id effect_pkill2( "pkill2" );
static const efftype_id effect_pkill3( "pkill3" );
static const efftype_id effect_recently_coughed( "recently_coughed" );
static const efftype_id effect_ridden( "ridden" );
static const efftype_id effect_riding( "riding" );
static const efftype_id effect_saddled( "monster_saddled" );
static const efftype_id effect_sleep( "sleep" );
static const efftype_id effect_slept_through_alarm( "slept_through_alarm" );
static const efftype_id effect_spores( "spores" );
static const efftype_id effect_stunned( "stunned" );
static const efftype_id effect_tied( "tied" );
static const efftype_id effect_took_prozac( "took_prozac" );
static const efftype_id effect_took_xanax( "took_xanax" );
static const efftype_id effect_webbed( "webbed" );

static const itype_id itype_apparatus( "apparatus" );
static const itype_id itype_beartrap( "beartrap" );
static const itype_id itype_e_handcuffs( "e_handcuffs" );
static const itype_id itype_fire( "fire" );
static const itype_id itype_rm13_armor_on( "rm13_armor_on" );
static const itype_id itype_rope_6( "rope_6" );
static const itype_id itype_snare_trigger( "snare_trigger" );
static const itype_id itype_string_36( "string_36" );
static const itype_id itype_toolset( "toolset" );
static const itype_id itype_voltmeter_bionic( "voltmeter_bionic" );
static const itype_id itype_UPS( "UPS" );
static const itype_id itype_bio_armor( "bio_armor" );

static const fault_id fault_bionic_nonsterile( "fault_bionic_nonsterile" );

static const skill_id skill_dodge( "dodge" );
static const skill_id skill_gun( "gun" );
static const skill_id skill_swimming( "swimming" );
static const skill_id skill_survival( "survival" );
static const skill_id skill_driving( "driving" );
static const skill_id skill_throw( "throw" );

static const species_id HUMAN( "HUMAN" );
static const species_id ROBOT( "ROBOT" );

static const trait_id trait_ACIDBLOOD( "ACIDBLOOD" );
static const trait_id trait_ACIDPROOF( "ACIDPROOF" );
static const trait_id trait_ADRENALINE( "ADRENALINE" );
static const trait_id trait_ANTENNAE( "ANTENNAE" );
static const trait_id trait_ANTLERS( "ANTLERS" );
static const trait_id trait_ASTHMA( "ASTHMA" );
static const trait_id trait_BADBACK( "BADBACK" );
static const trait_id trait_CF_HAIR( "CF_HAIR" );
static const trait_id trait_GLASSJAW( "GLASSJAW" );
static const trait_id trait_DEBUG_NODMG( "DEBUG_NODMG" );
static const trait_id trait_DEBUG_STAMINA( "DEBUG_STAMINA" );
static const trait_id trait_DEFT( "DEFT" );
static const trait_id trait_PROF_SKATER( "PROF_SKATER" );
static const trait_id trait_QUILLS( "QUILLS" );
static const trait_id trait_SPINES( "SPINES" );
static const trait_id trait_THORNS( "THORNS" );
static const trait_id trait_WOOLALLERGY( "WOOLALLERGY" );

static const bionic_id bio_ads( "bio_ads" );
static const bionic_id bio_blindfold( "bio_blindfold" );
static const bionic_id bio_climate( "bio_climate" );
static const bionic_id bio_cloak( "bio_cloak" );
static const bionic_id bio_earplugs( "bio_earplugs" );
static const bionic_id bio_ears( "bio_ears" );
static const bionic_id bio_electrosense( "bio_electrosense" );
static const bionic_id bio_faraday( "bio_faraday" );
static const bionic_id bio_flashlight( "bio_flashlight" );
static const bionic_id bio_gills( "bio_gills" );
static const bionic_id bio_ground_sonar( "bio_ground_sonar" );
static const bionic_id bio_heatsink( "bio_heatsink" );
static const bionic_id bio_infrared( "bio_infrared" );
static const bionic_id bio_jointservo( "bio_jointservo" );
static const bionic_id bio_laser( "bio_laser" );
static const bionic_id bio_leukocyte( "bio_leukocyte" );
static const bionic_id bio_lighter( "bio_lighter" );
static const bionic_id bio_membrane( "bio_membrane" );
static const bionic_id bio_memory( "bio_memory" );
static const bionic_id bio_night_vision( "bio_night_vision" );
static const bionic_id bio_ods( "bio_ods" );
static const bionic_id bio_railgun( "bio_railgun" );
static const bionic_id bio_recycler( "bio_recycler" );
static const bionic_id bio_shock_absorber( "bio_shock_absorber" );
static const bionic_id bio_storage( "bio_storage" );
static const bionic_id bio_synaptic_regen( "bio_synaptic_regen" );
static const bionic_id bio_tattoo_led( "bio_tattoo_led" );
static const bionic_id bio_tools( "bio_tools" );
static const bionic_id bio_ups( "bio_ups" );

// Aftershock stuff!
static const bionic_id afs_bio_linguistic_coprocessor( "afs_bio_linguistic_coprocessor" );

static const trait_id trait_BARK( "BARK" );
static const trait_id trait_BIRD_EYE( "BIRD_EYE" );
static const trait_id trait_CEPH_EYES( "CEPH_EYES" );
static const trait_id trait_DEAF( "DEAF" );
static const trait_id trait_DEBUG_CLOAK( "DEBUG_CLOAK" );
static const trait_id trait_DEBUG_LS( "DEBUG_LS" );
static const trait_id trait_DEBUG_NIGHTVISION( "DEBUG_NIGHTVISION" );
static const trait_id trait_DEBUG_NOTEMP( "DEBUG_NOTEMP" );
static const trait_id trait_DEBUG_STORAGE( "DEBUG_STORAGE" );
static const trait_id trait_DEBUG_WEIGHTLESSNESS( "DEBUG_WEIGHTLESSNESS" );
static const trait_id trait_DOWN( "DOWN" );
static const trait_id trait_ELECTRORECEPTORS( "ELECTRORECEPTORS" );
static const trait_id trait_FASTLEARNER( "FASTLEARNER" );
static const trait_id trait_GILLS_CEPH( "GILLS_CEPH" );
static const trait_id trait_GILLS( "GILLS" );
static const trait_id trait_HEAVYSLEEPER( "HEAVYSLEEPER" );
static const trait_id trait_HEAVYSLEEPER2( "HEAVYSLEEPER2" );
static const trait_id trait_HIBERNATE( "HIBERNATE" );
static const trait_id trait_HOARDER( "HOARDER" );
static const trait_id trait_HOLLOW_BONES( "HOLLOW_BONES" );
static const trait_id trait_HORNS_POINTED( "HORNS_POINTED" );
static const trait_id trait_INFRARED( "INFRARED" );
static const trait_id trait_LEG_TENT_BRACE( "LEG_TENT_BRACE" );
static const trait_id trait_LIGHT_BONES( "LIGHT_BONES" );
static const trait_id trait_LIZ_IR( "LIZ_IR" );
static const trait_id trait_M_DEPENDENT( "M_DEPENDENT" );
static const trait_id trait_M_IMMUNE( "M_IMMUNE" );
static const trait_id trait_M_SKIN2( "M_SKIN2" );
static const trait_id trait_M_SKIN3( "M_SKIN3" );
static const trait_id trait_MEMBRANE( "MEMBRANE" );
static const trait_id trait_MOREPAIN( "MORE_PAIN" );
static const trait_id trait_MOREPAIN2( "MORE_PAIN2" );
static const trait_id trait_MOREPAIN3( "MORE_PAIN3" );
static const trait_id trait_MYOPIC( "MYOPIC" );
static const trait_id trait_NO_THIRST( "NO_THIRST" );
static const trait_id trait_NOMAD( "NOMAD" );
static const trait_id trait_NOMAD2( "NOMAD2" );
static const trait_id trait_NOMAD3( "NOMAD3" );
static const trait_id trait_NOPAIN( "NOPAIN" );
static const trait_id trait_PACIFIST( "PACIFIST" );
static const trait_id trait_PADDED_FEET( "PADDED_FEET" );
static const trait_id trait_PAINRESIST_TROGLO( "PAINRESIST_TROGLO" );
static const trait_id trait_PAINRESIST( "PAINRESIST" );
static const trait_id trait_PAWS_LARGE( "PAWS_LARGE" );
static const trait_id trait_PAWS( "PAWS" );
static const trait_id trait_PER_SLIME_OK( "PER_SLIME_OK" );
static const trait_id trait_PER_SLIME( "PER_SLIME" );
static const trait_id trait_PROF_FOODP( "PROF_FOODP" );
static const trait_id trait_PYROMANIA( "PYROMANIA" );
static const trait_id trait_RADIOGENIC( "RADIOGENIC" );
static const trait_id trait_ROOTS2( "ROOTS2" );
static const trait_id trait_ROOTS3( "ROOTS3" );
static const trait_id trait_SAVANT( "SAVANT" );
static const trait_id trait_SEESLEEP( "SEESLEEP" );
static const trait_id trait_SHELL( "SHELL" );
static const trait_id trait_SHELL2( "SHELL2" );
static const trait_id trait_SHOUT2( "SHOUT2" );
static const trait_id trait_SHOUT3( "SHOUT3" );
static const trait_id trait_SLIMESPAWNER( "SLIMESPAWNER" );
static const trait_id trait_SLIMY( "SLIMY" );
static const trait_id trait_SLOWLEARNER( "SLOWLEARNER" );
static const trait_id trait_STRONGSTOMACH( "STRONGSTOMACH" );
static const trait_id trait_THRESH_CEPHALOPOD( "THRESH_CEPHALOPOD" );
static const trait_id trait_THRESH_INSECT( "THRESH_INSECT" );
static const trait_id trait_THRESH_PLANT( "THRESH_PLANT" );
static const trait_id trait_THRESH_SPIDER( "THRESH_SPIDER" );
static const trait_id trait_TRANSPIRATION( "TRANSPIRATION" );
static const trait_id trait_URSINE_EYE( "URSINE_EYE" );
static const trait_id trait_VISCOUS( "VISCOUS" );
static const trait_id trait_WEBBED( "WEBBED" );

static const std::string flag_PLOWABLE( "PLOWABLE" );
static const std::string iuse_TOGGLE_UPS_CHARGING( "TOGGLE_UPS_CHARGING" );

static const mtype_id mon_player_blob( "mon_player_blob" );
static const mtype_id mon_shadow_snake( "mon_shadow_snake" );

static const trait_flag_str_id trait_flag_PRED2( "PRED2" );
static const trait_flag_str_id trait_flag_PRED3( "PRED3" );
static const trait_flag_str_id trait_flag_PRED4( "PRED4" );

static const trait_flag_str_id flag_NO_THIRST( "NO_THIRST" );
static const trait_flag_str_id flag_NO_RADIATION( "NO_RADIATION" );
static const trait_flag_str_id flag_NON_THRESH( "NON_THRESH" );

static const activity_id ACT_ASSIST( "ACT_ASSIST" );


void static try_remove_downed( Character& c )
{

    /** @EFFECT_DEX increases chance to stand up when knocked down */

    /** @EFFECT_STR increases chance to stand up when knocked down, slightly */
    if( rng( 0, 40 ) > c.get_dex() + c.get_str() / 2 ) {
        c.add_msg_if_player( _( "You struggle to stand." ) );
    } else {
        c.add_msg_player_or_npc( m_good, _( "You stand up." ), _( "<npcname> stands up." ) );
        c.remove_effect( effect_downed );
    }
}

void static try_remove_bear_trap( Character& c )
{
    map& here = get_map();
    /* Real bear traps can't be removed without the proper tools or immense strength; eventually
       this should allow normal players two options: removal of the limb or removal of the trap from
       the ground (at which point the player could later remove it from the leg with the right
       tools). As such we are currently making it a bit easier for players and NPC's to get out of
       bear traps.
    */
    /** @EFFECT_STR increases chance to escape bear trap */
    // If is riding, then despite the character having the effect, it is the mounted creature that
    // escapes.
    if( c.is_player() && c.is_mounted() ) {
        auto mon = c.mounted_creature.get();
        if( mon->type->melee_dice * mon->type->melee_sides >= 18 ) {
            if( x_in_y( mon->type->melee_dice * mon->type->melee_sides, 200 ) ) {
                mon->remove_effect( effect_beartrap );
                c.remove_effect( effect_beartrap );
                here.spawn_item( c.bub_pos(), itype_beartrap );
                add_msg( _( "The %s escapes the bear trap!" ), mon->get_name() );
            } else {
                c.add_msg_if_player(
                    m_bad,
                    _( "Your %s tries to free itself from the bear trap, but can't get loose!" ),
                    mon->get_name() );
            }
        }
    } else {
        if( x_in_y( c.get_str(), 100 ) ) {
            c.remove_effect( effect_beartrap );
            c.add_msg_player_or_npc(
                m_good, _( "You free yourself from the bear trap!" ),
                _( "<npcname> frees themselves from the bear trap!" ) );
            here.spawn_item( c.bub_pos(), itype_beartrap );
        } else {
            c.add_msg_if_player(
                m_bad,
                _( "You try to free yourself from the bear trap, but can't "
                   "get loose!" ) );
        }
    }
}

void static try_remove_lightsnare( Character& c )
{
    map& here = get_map();
    if( c.is_mounted() ) {
        auto mon = c.mounted_creature.get();
        if( x_in_y( mon->type->melee_dice * mon->type->melee_sides, 12 ) ) {
            mon->remove_effect( effect_lightsnare );
            c.remove_effect( effect_lightsnare );
            here.spawn_item( c.bub_pos(), itype_string_36 );
            here.spawn_item( c.bub_pos(), itype_snare_trigger );
            add_msg( _( "The %s escapes the light snare!" ), mon->get_name() );
        }
    } else {
        /** @EFFECT_STR increases chance to escape light snare */

        /** @EFFECT_DEX increases chance to escape light snare */
        if( x_in_y( c.get_str(), 12 ) || x_in_y( c.get_dex(), 8 ) ) {
            c.remove_effect( effect_lightsnare );
            c.add_msg_player_or_npc(
                m_good, _( "You free yourself from the light snare!" ),
                _( "<npcname> frees themselves from the light snare!" ) );
            here.spawn_item( c.bub_pos(), itype_string_36 );
            here.spawn_item( c.bub_pos(), itype_snare_trigger );
        } else {
            c.add_msg_if_player(
                m_bad,
                _( "You try to free yourself from the light snare, but can't "
                   "get loose!" ) );
        }
    }
}

void static try_remove_heavysnare( Character& c )
{
    map& here = get_map();
    if( c.is_mounted() ) {
        auto mon = c.mounted_creature.get();
        if( mon->type->melee_dice * mon->type->melee_sides >= 7 ) {
            if( x_in_y( mon->type->melee_dice * mon->type->melee_sides, 32 ) ) {
                mon->remove_effect( effect_heavysnare );
                c.remove_effect( effect_heavysnare );
                here.spawn_item( c.bub_pos(), itype_rope_6 );
                here.spawn_item( c.bub_pos(), itype_snare_trigger );
                add_msg( _( "The %s escapes the heavy snare!" ), mon->get_name() );
            }
        }
    } else {
        /** @EFFECT_STR increases chance to escape heavy snare, slightly */

        /** @EFFECT_DEX increases chance to escape light snare */
        if( x_in_y( c.get_str(), 32 ) || x_in_y( c.get_dex(), 16 ) ) {
            c.remove_effect( effect_heavysnare );
            c.add_msg_player_or_npc(
                m_good, _( "You free yourself from the heavy snare!" ),
                _( "<npcname> frees themselves from the heavy snare!" ) );
            here.spawn_item( c.bub_pos(), itype_rope_6 );
            here.spawn_item( c.bub_pos(), itype_snare_trigger );
        } else {
            c.add_msg_if_player(
                m_bad,
                _( "You try to free yourself from the heavy snare, but can't "
                   "get loose!" ) );
        }
    }
}

void static try_remove_crushed( Character& c )
{
    /** @EFFECT_STR increases chance to escape crushing rubble */

    /** @EFFECT_DEX increases chance to escape crushing rubble, slightly */
    if( x_in_y( c.get_str() + c.get_dex() / 4.0, 100 ) ) {
        c.remove_effect( effect_crushed );
        c.add_msg_player_or_npc(
            m_good, _( "You free yourself from the rubble!" ),
            _( "<npcname> frees themselves from the rubble!" ) );
    } else {
        c.add_msg_if_player(
            m_bad,
            _( "You try to free yourself from the rubble, but can't get "
               "loose!" ) );
    }
}

bool static try_remove_grab( Character& c )
{
    int zed_number = 0;
    map& here = get_map();
    if( c.is_mounted() ) {
        auto mon = c.mounted_creature.get();
        if( mon->has_effect( effect_grabbed ) ) {
            if( ( dice( mon->type->melee_dice + mon->type->melee_sides, 3 )
                  < c.get_effect_int( effect_grabbed ) )
                || !one_in( 4 ) ) {
                add_msg( m_bad, _( "Your %s tries to break free, but fails!" ), mon->get_name() );
                return false;
            } else {
                add_msg( m_good, _( "Your %s breaks free from the grab!" ), mon->get_name() );
                c.remove_effect( effect_grabbed );
                mon->remove_effect( effect_grabbed );
            }
        } else {
            if( one_in( 4 ) ) {
                add_msg( m_bad, _( "You are pulled from your %s!" ), mon->get_name() );
                c.remove_effect( effect_grabbed );
                c.forced_dismount();
            }
        }
    } else {
        for( auto& dest : here.points_in_radius( c.bub_pos(), 1, 0 ) ) { // *NOPAD*
            const monster* const mon = g->critter_at<monster>( dest );
            if( mon && mon->has_effect( effect_grabbing ) ) { zed_number += mon->get_grab_strength(); }
        }
        if( zed_number == 0 ) {
            c.add_msg_player_or_npc(
                m_good, _( "You find yourself no longer grabbed." ),
                _( "<npcname> finds themselves no longer grabbed." ) );
            c.remove_effect( effect_grabbed );

            /** @EFFECT_STR increases chance to escape grab */
        } else if( rng( 0, c.get_str() ) < rng( c.get_effect_int( effect_grabbed, body_part_torso ), 8 ) ) {
            c.add_msg_player_or_npc(
                m_bad, _( "You try break out of the grab, but fail!" ),
                _( "<npcname> tries to break out of the grab, but fails!" ) );
            return false;
        } else {
            c.add_msg_player_or_npc(
                m_good, _( "You break out of the grab!" ), _( "<npcname> breaks out of the grab!" ) );
            c.remove_effect( effect_grabbed );
            for( auto& dest : here.points_in_radius( c.bub_pos(), 1, 0 ) ) { // *NOPAD*
                monster* mon = g->critter_at<monster>( dest );
                if( mon && mon->has_effect( effect_grabbing ) ) {
                    mon->remove_effect( effect_grabbing );
                }
            }
        }
    }
    return true;
}

void static try_remove_webs( Character& c )
{
    if( c.is_mounted() ) {
        auto mon = c.mounted_creature.get();
        if( x_in_y( mon->type->melee_dice * mon->type->melee_sides,
                    6 * c.get_effect_int( effect_webbed ) ) ) {
            add_msg( _( "The %s breaks free of the webs!" ), mon->get_name() );
            mon->remove_effect( effect_webbed );
            c.remove_effect( effect_webbed );
        }
        /** @EFFECT_STR increases chance to escape webs */
    } else if( x_in_y( c.get_str(), 6 * c.get_effect_int( effect_webbed ) ) ) {
        c.add_msg_player_or_npc(
            m_good, _( "You free yourself from the webs!" ),
            _( "<npcname> frees themselves from the webs!" ) );
        c.remove_effect( effect_webbed );
    } else {
        c.add_msg_if_player( _( "You try to free yourself from the webs, but can't get loose!" ) );
    }
}

int Character::swim_speed() const
{
    int ret;
    if( is_mounted() ) {
        monster* mon = mounted_creature.get();
        // no difference in swim speed by monster type yet.
        // TODO: difference in swim speed by monster type.
        // No monsters are currently mountable and can swim, though mods may allow this.
        if( mon->swims() ) {
            ret = 25;
            ret += get_weight() / 120_gram - 50 * mon->get_size();
            return ret;
        }
    }
    const auto usable = exclusive_flag_coverage( flag_ALLOWS_NATURAL_ATTACKS );
    float hand_bonus_mult =
        ( usable.test( STATIC( bodypart_str_id( "hand_l" ) ) ) ? 0.5f : 0.0f )
        + ( usable.test( STATIC( bodypart_str_id( "hand_r" ) ) ) ? 0.5f : 0.0f );

    // base swim speed.
    ret = ( 440 * mutation_value( "movecost_swim_modifier" ) )
          + weight_carried() / ( 60_gram / mutation_value( "movecost_swim_modifier" ) )
          - 50 * get_skill_level( skill_swimming );
    /** @EFFECT_STR increases swim speed bonus from PAWS */
    if( has_trait( trait_PAWS ) ) { ret -= hand_bonus_mult * ( 20 + str_cur * 3 ); }
    /** @EFFECT_STR increases swim speed bonus from PAWS_LARGE */
    if( has_trait( trait_PAWS_LARGE ) ) { ret -= hand_bonus_mult * ( 20 + str_cur * 4 ); }
    /** @EFFECT_STR increases swim speed bonus from swim_fins */
    if( worn_with_flag( flag_FIN, bodypart_id( "foot_l" ) )
        || worn_with_flag( flag_FIN, bodypart_id( "foot_r" ) ) ) {
        if( worn_with_flag( flag_FIN, bodypart_id( "foot_l" ) )
            && worn_with_flag( flag_FIN, bodypart_id( "foot_r" ) ) ) {
            ret -= ( 15 * str_cur );
        } else {
            ret -= ( 15 * str_cur ) / 2;
        }
    }
    /** @EFFECT_STR increases swim speed bonus from WEBBED */
    if( has_trait( trait_WEBBED ) ) { ret -= hand_bonus_mult * ( 60 + str_cur * 5 ); }
    /** @EFFECT_SWIMMING increases swim speed */
    ret += ( 50 - get_skill_level( skill_swimming ) * 2 ) * ( ( encumb( body_part_leg_l ) + encumb(
               body_part_leg_r ) ) / 10 );
    ret += ( 80 - get_skill_level( skill_swimming ) * 3 ) * ( encumb( body_part_torso ) / 10 );
    if( get_skill_level( skill_swimming ) < 10 ) {
        for( auto &i : worn ) {
            ret += i->volume() / 125_ml * ( 10 - get_skill_level( skill_swimming ) );
        }
    }
    /** @EFFECT_STR increases swim speed */

    /** @EFFECT_DEX increases swim speed */
    ret -= str_cur * 6 + dex_cur * 4;
    if( worn_with_flag( flag_FLOTATION ) && !get_map().has_flag( TFLAG_WATER_CUBE, bub_pos() ) ) {
        ret = std::min( ret, 400 );
        ret = std::max( ret, 200 );
    }
    // If (ret > 500), we can not swim; so do not apply the underwater bonus.
    if( is_underwater() && ret < 500 ) { ret -= 50; }

    // Running movement mode while swimming means faster swim style, like crawlstroke
    if( move_mode == CMM_RUN ) { ret -= 80; }
    // Crouching movement mode while swimming means slower swim style, like breaststroke
    if( is_crouching() ) { ret += 50; }

    if( ret < 30 ) { ret = 30; }
    return ret;
}

bool Character::is_on_ground() const
{
    return get_working_leg_count() < 2 || has_effect( effect_downed );
}

bool Character::can_run()
{
    return ( get_stamina() > get_stamina_max() * 0.1f ) && get_working_leg_count() >= 2;
}

bool Character::move_effects( bool attacking )
{
    if( has_effect( effect_downed ) ) {
        try_remove_downed( *this );
        return false;
    }
    if( has_effect( effect_webbed ) ) {
        try_remove_webs( *this );
        return false;
    }
    if( has_effect( effect_lightsnare ) ) {
        try_remove_lightsnare( *this );
        return false;
    }
    if( has_effect( effect_heavysnare ) ) {
        try_remove_heavysnare( *this );
        return false;
    }
    if( has_effect( effect_beartrap ) ) {
        try_remove_bear_trap( *this );
        return false;
    }
    if( has_effect( effect_crushed ) ) {
        try_remove_crushed( *this );
        return false;
    }
    // Below this point are things that allow for movement if they succeed

    // Currently we only have one thing that forces movement if you succeed, should we get more
    // than this will need to be reworked to only have success effects if /all/ checks succeed
    if( has_effect( effect_in_pit ) ) {
        /** @EFFECT_STR increases chance to escape pit */

        /** @EFFECT_DEX increases chance to escape pit, slightly */
        if( rng( 0, 40 ) > get_str() + get_dex() / 2 ) {
            add_msg_if_player( m_bad, _( "You try to escape the pit, but slip back in." ) );
            return false;
        } else {
            add_msg_player_or_npc( m_good, _( "You escape the pit!" ), _( "<npcname> escapes the pit!" ) );
            remove_effect( effect_in_pit );
        }
    }
    if( has_effect( effect_grabbed ) && !attacking && !try_remove_grab( *this ) ) {
        // NOLINTNEXTLINE(readability-simplify-boolean-expr)
        return false;
    }
    return true;
}

void Character::wait_effects()
{
    if( has_effect( effect_downed ) ) {
        try_remove_downed( *this );
        return;
    }
    if( has_effect( effect_beartrap ) ) {
        try_remove_bear_trap( *this );
        return;
    }
    if( has_effect( effect_lightsnare ) ) {
        try_remove_lightsnare( *this );
        return;
    }
    if( has_effect( effect_heavysnare ) ) {
        try_remove_heavysnare( *this );
        return;
    }
    if( has_effect( effect_webbed ) ) {
        try_remove_webs( *this );
        return;
    }
    if( has_effect( effect_grabbed ) ) {
        try_remove_grab( *this );
        return;
    }
}

character_movemode Character::get_movement_mode() const { return move_mode; }

bool Character::movement_mode_is( const character_movemode mode ) const { return move_mode == mode; }

bool Character::is_crouching() const { return is_crouch_like_movemode( move_mode ); }

int Character::run_cost( int base_cost, bool diag ) const
{
    float movecost = static_cast<float>( base_cost );
    if( diag ) {
        movecost *= 0.7071f; // because everything here assumes 100 is base
    }
    const bool flatground = movecost < 105;
    map& here = get_map();
    // The "FLAT" tag includes soft surfaces, so not a good fit.
    const bool on_road = flatground && here.has_flag( "ROAD", bub_pos() );
    const bool on_fungus = here.has_flag_ter_or_furn( "FUNGUS", bub_pos() );

    if( !is_mounted() ) {
        if( movecost > 100 ) {
            movecost *= mutation_value( "movecost_obstacle_modifier" );
            if( movecost < 100 ) { movecost = 100; }
        }
        if( has_trait( trait_M_IMMUNE ) && on_fungus ) {
            if( movecost > 75 ) {
                // Mycal characters are faster on their home territory, even through things like
                // shrubs
                movecost = 75;
            }
        }

        // Linearly increase move cost relative to individual leg hp.
        movecost +=
            50
            * ( 1
                - static_cast<float>( get_part_hp_cur( bodypart_id( "leg_l" ) ) )
                / static_cast<float>( get_part_hp_max( bodypart_id( "leg_l" ) ) ) );
        movecost +=
            50
            * ( 1
                - static_cast<float>( get_part_hp_cur( bodypart_id( "leg_r" ) ) )
                / static_cast<float>( get_part_hp_max( bodypart_id( "leg_r" ) ) ) );
        movecost *= mutation_value( "movecost_modifier" );
        if( flatground ) { movecost *= mutation_value( "movecost_flatground_modifier" ); }
        if( has_trait( trait_PADDED_FEET ) && !footwear_factor() ) { movecost *= .9f; }
        if( has_active_bionic( bio_jointservo ) ) {
            movecost *= ( move_mode == CMM_RUN ? 0.75f : 0.9f );
        } else if( has_bionic( bio_jointservo ) ) {
            movecost *= 0.95f;
        }

        if( worn_with_flag( flag_SLOWS_MOVEMENT ) ) { movecost *= 1.1f; }
        if( worn_with_flag( flag_FIN ) ) { movecost *= 1.5f; }
        if( worn_with_flag( flag_ROLLER_INLINE ) ) {
            if( on_road ) {
                movecost *= 0.5f;
            } else {
                movecost *= 1.5f;
            }
        }
        // Quad skates might be more stable than inlines,
        // but that also translates into a slower speed when on good surfaces.
        if( worn_with_flag( flag_ROLLER_QUAD ) ) {
            if( on_road ) {
                movecost *= 0.7f;
            } else {
                movecost *= 1.3f;
            }
        }
        // Skates with only one wheel (roller shoes) are fairly less stable
        // and fairly slower as well
        if( worn_with_flag( flag_ROLLER_ONE ) ) {
            if( on_road ) {
                movecost *= 0.85f;
            } else {
                movecost *= 1.1f;
            }
        }

        movecost +=
            ( ( encumb( body_part_foot_l ) + encumb( body_part_foot_r ) ) * 2.5
              + ( encumb( body_part_leg_l ) + encumb( body_part_leg_r ) ) * 1.5 )
            / 10;

        // ROOTS3 does slow you down as your roots are probing around for nutrients,
        // whether you want them to or not.  ROOTS1 is just too squiggly without shoes
        // to give you some stability.  Plants are a bit of a slow-mover.  Deal.
        if( has_trait( trait_ROOTS3 ) && here.ter( bub_pos() )->is_diggable() ) {
            movecost += 10 * footwear_factor();
        }

        movecost += bonus_from_enchantments( movecost, enchant_vals::mod::MOVE_COST );
        movecost /= running_move_cost_modifier();

        if( movecost < 20.0 ) { movecost = 20.0; }
    }

    if( diag ) { movecost *= M_SQRT2; }

    return static_cast<int>( movecost );
}

void Character::set_destination( const std::vector<tripoint_bub_ms> &route )
{
    set_destination( route, std::make_unique<player_activity>() );
}

void Character::set_destination(
    const std::vector<tripoint_bub_ms> &route,
    std::unique_ptr<player_activity> new_destination_activity )
{
    auto_move_route = route;
    set_destination_activity( std::move( new_destination_activity ) );
    destination_point.emplace( get_map().bub_to_abs( route.back() ) );
}

std::unique_ptr<player_activity> Character::clear_destination()
{
    auto_move_route.clear();
    std::unique_ptr<player_activity> ret = clear_destination_activity();
    destination_point = std::nullopt;
    next_expected_position = std::nullopt;
    return ret;
}

bool Character::has_distant_destination() const
{
    return has_destination() && !get_destination_activity().is_null() &&
    get_destination_activity().id() == ACT_TRAVELLING && !omt_path.empty();
}

bool Character::is_auto_moving() const { return destination_point.has_value(); }

bool Character::has_destination() const
{
    return !auto_move_route.empty();
}

bool Character::has_destination_activity() const
{
    return !get_destination_activity().is_null() && destination_point &&
    abs_pos() == *destination_point;
}

void Character::start_destination_activity()
{
    if( !has_destination_activity() ) {
        debugmsg( "Tried to start invalid destination activity" );
        return;
    }

    assign_activity( clear_destination() );
}

action_id Character::get_next_auto_move_direction()
{
    if( !has_destination() ) { return ACTION_NULL; }

    if( next_expected_position ) {
        if( bub_pos() != *next_expected_position ) {
            // We're off course, possibly stumbling or stuck, cancel auto move
            return ACTION_NULL;
        }
    }

    next_expected_position.emplace( auto_move_route.front() );
    auto_move_route.erase( auto_move_route.begin() );

    auto dp = *next_expected_position - bub_pos();

    // Make sure the direction is just one step and that
    // all diagonal moves have 0 z component
    if( std::abs( dp.x() ) > 1 || std::abs( dp.y() ) > 1 || std::abs( dp.z() ) > 1
        || ( std::abs( dp.z() ) != 0 && ( std::abs( dp.x() ) != 0 || std::abs( dp.y() ) != 0 ) ) ) {
        // Should never happen, but check just in case
        return ACTION_NULL;
    }
    return get_movement_action_from_delta( dp, iso_rotate::yes );
}

bool Character::defer_move( const tripoint_bub_ms& next )
{
    // next must be adjacent to current pos
    if( square_dist( next, bub_pos() ) != 1 ) { return false; }
    // next must be adjacent to subsequent move in any preexisting automove route
    if( has_destination() && square_dist( auto_move_route.front(), next ) != 1 ) { return false; }
    auto_move_route.insert( auto_move_route.begin(), next );
    next_expected_position = bub_pos();
    return true;
}

void Character::set_underwater( bool x )
{
    if( is_underwater() != x ) {
        Creature::set_underwater( x );
        recalc_sight_limits();
    }
}

float Character::stability_roll() const
{
    /** @EFFECT_STR improves player stability roll */

    /** @EFFECT_PER slightly improves player stability roll */

    /** @EFFECT_DEX slightly improves player stability roll */

    /** @EFFECT_DODGE improves player stability roll */
    return get_dodge() + get_str() + ( get_per() / 3.0f ) + ( get_dex() / 4.0f );
}

bool Character::uncanny_dodge() { return character_funcs::try_uncanny_dodge( *this ); }

float Character::fall_damage_mod() const
{
    if( has_effect_with_flag( flag_EFFECT_FEATHER_FALL ) ) {
    return 0.0f;
}
float ret = 1.0f;

// Ability to land properly is 2x as important as dexterity itself
/** @EFFECT_DEX decreases damage from falling */

/** @EFFECT_DODGE decreases damage from falling */
float dex_dodge = dex_cur / 2.0 + get_skill_level( skill_dodge );
// Penalize for wearing heavy stuff
const float average_leg_encumb = ( encumb( body_part_leg_l ) + encumb( body_part_leg_r ) ) / 2.0;
dex_dodge -= ( average_leg_encumb + encumb( body_part_torso ) ) / 10;
// But prevent it from increasing damage
dex_dodge = std::max( 0.0f, dex_dodge );
// 100% damage at 0, 75% at 10, 50% at 20 and so on
ret *= ( 100.0f - ( dex_dodge * 4.0f ) ) / 100.0f;

ret *= mutation_value( "falling_damage_multiplier" );

// TODO: Bonus for Judo, mutations. Penalty for heavy weight (including mutations)
return std::max( 0.0f, ret );
}

int Character::impact( const int force, const tripoint_bub_ms& p )
{
    // Falls over ~30m are fatal more often than not
    // But that would be quite a lot considering 21 z-levels in game
    // so let's assume 1 z-level is comparable to 30 force

    if( force <= 0 ) { return force; }

    // Damage modifier (post armor)
    float mod = 1.0f;
    int effective_force = force;
    int cut = 0;
    // Percentage armor penetration - armor won't help much here
    // TODO: Make cushioned items like bike helmets help more
    float armor_eff = 1.0f;
    // Shock Absorber CBM heavily reduces damage
    const bool shock_absorbers = has_active_bionic( bionic_id( "bio_shock_absorber" ) );

    // Being slammed against things rather than landing means we can't
    // control the impact as well
    const bool slam = p != bub_pos();
    std::string target_name = "a swarm of bugs";
    Creature* critter = g->critter_at( p );
    if( critter != this && critter != nullptr ) {
        target_name = critter->disp_name();
        // Slamming into creatures and NPCs
        // TODO: Handle spikes/horns and hard materials
        armor_eff = 0.5f; // 2x as much as with the ground
        // TODO: Modify based on something?
        mod = 1.0f;
        effective_force = force;
    } else if( const optional_vpart_position vp = g->m.veh_at( p ) ) {
        // Slamming into vehicles
        // TODO: Integrate it with vehicle collision function somehow
        target_name = vp->vehicle().disp_name();
        if( vp.part_with_feature( "SHARP", true ) ) {
            // Now we're actually getting impaled
            cut = force; // Lots of fun
        }

        mod = slam ? 1.0f : fall_damage_mod();
        armor_eff = 0.25f; // Not much
        if( !slam && vp->part_with_feature( "ROOF", true ) ) {
            // Roof offers better landing than frame or pavement
            // TODO: Make this not happen with heavy duty/plated roof
            effective_force /= 2;
        }
    } else {
        // Slamming into terrain/furniture
        target_name = g->m.disp_name( p );
        int hard_ground = g->m.ter( p )->is_diggable() ? 0 : 3;
        armor_eff = 0.25f; // Not much
        // Get cut by stuff
        // This isn't impalement on metal wreckage, more like flying through a closed window
        cut = g->m.has_flag( TFLAG_SHARP, p ) ? 5 : 0;
        effective_force = force + hard_ground;
        mod = slam ? 1.0f : fall_damage_mod();
        if( g->m.has_furn( p ) ) {
            // TODO: Make furniture matter
        } else if( g->m.has_flag( TFLAG_SWIMMABLE, p ) ) {
            // TODO: Some formula of swimming
            effective_force /= 4;
        }
    }

    // Rescale for huge force
    // At >30 force, proper landing is impossible and armor helps way less
    if( effective_force > 30 ) {
        // Armor simply helps way less
        armor_eff *= 30.0f / effective_force;
        if( mod < 1.0f ) {
            // Everything past 30 damage gets a worse modifier
            const float scaled_mod = std::pow( mod, 30.0f / effective_force );
            const float scaled_damage = ( 30.0f * mod ) + scaled_mod * ( effective_force - 30.0f );
            mod = scaled_damage / effective_force;
        }
    }

    if( !slam && mod < 1.0f && mod * force < 5 ) {
        // Perfect landing, no damage (regardless of armor)
        add_msg_if_player( m_warning, _( "You land on %s." ), target_name );
        return 0;
    }

    // Shock absorbers kick in only when they need to, so if our other protections fail, fall back
    // on them
    if( shock_absorbers ) {
        effective_force -= 15; // Provide a flat reduction to force
        if( mod > 0.25f ) {
            mod = 0.25f; // And provide a 75% reduction against that force if we don't have it
            // already
        }
        if( effective_force < 0 ) { effective_force = 0; }
    }

    int total_dealt = 0;
    if( mod * effective_force >= 5 ) {
        for( const bodypart_id& bp : get_all_body_parts( true ) ) {
            const int bash = effective_force * rng( 60, 100 ) / 100;
            damage_instance di;
            di.add_damage( DT_BASH, bash, 0, armor_eff, mod );
            // No good way to land on sharp stuff, so here modifier == 1.0f
            di.add_damage( DT_CUT, cut, 0, armor_eff, 1.0f );
            total_dealt += deal_damage( nullptr, bp, di ).total_damage();
        }
    }

    if( total_dealt > 0 && is_player() ) {
        // "You slam against the dirt" is fine
        add_msg( m_bad, _( "You are slammed against %1$s for %2$d damage." ), target_name,
                 total_dealt );
    } else if( is_player() && shock_absorbers ) {
        add_msg( m_bad, _( "You are slammed against %s!" ), target_name, total_dealt );
        add_msg( m_good, _( "…but your shock absorbers negate the damage!" ) );
    } else if( slam ) {
        // Only print this line if it is a slam and not a landing
        // Non-players should only get this one: player doesn't know how much damage was dealt
        // and landing messages for each slammed creature would be too much
        add_msg_player_or_npc(
            m_bad, _( "You are slammed against %s." ), _( "<npcname> is slammed against %s." ),
            target_name );
    } else {
        // No landing message for NPCs
        add_msg_if_player( m_warning, _( "You land on %s." ), target_name );
    }

    // Check if creature being impacted is player,
    // stop hauling if so (Since player has been flung away from haul spot)
    if( is_player() && is_hauling() ) { stop_hauling(); }

    if( x_in_y( mod, 1.0f ) ) { add_effect( effect_downed, rng( 1_turns, 1_turns + mod * 3_turns ) ); }

    return total_dealt;
}

void Character::knock_back_to( const tripoint_bub_ms& to )
{
    if( to == bub_pos() ) { return; }

    if( rl_dist( bub_pos(), to ) < 2 && get_map().obstructed_by_vehicle_rotation( bub_pos(), to ) ) {
        tripoint_bub_ms intervening = to;
        if( one_in( 2 ) ) {
            intervening.x() = bub_pos().x();
        } else {
            intervening.y() = bub_pos().y();
        }

        apply_damage( nullptr, bodypart_id( "torso" ), 3 );
        add_effect( effect_stunned, 2_turns );
        add_msg_player_or_npc(
            _( "You bounce off a %s!" ), _( "<npcname> bounces off a %s!" ),
            g->m.obstacle_name( intervening ) );
        return;
    }

    // First, see if we hit a monster
    if( monster * const critter = g->critter_at<monster>( to ) ) {
        deal_damage( critter, bodypart_id( "torso" ),
                     damage_instance( DT_BASH, static_cast<float>( critter->type->size ) ) );
        add_effect( effect_stunned, 1_turns );
        /** @EFFECT_STR_MAX allows knocked back player to knock back, damage, stun some monsters */
        if( ( str_max - 6 ) / 4 > critter->type->size ) {
            critter->knock_back_from( bub_pos() ); // Chain reaction!
            critter->apply_damage( this, bodypart_id( "torso" ), ( str_max - 6 ) / 4 );
            critter->add_effect( effect_stunned, 1_turns );
        } else if( ( str_max - 6 ) / 4 == critter->type->size ) {
            critter->apply_damage( this, bodypart_id( "torso" ), ( str_max - 6 ) / 4 );
            critter->add_effect( effect_stunned, 1_turns );
        }
        critter->check_dead_state();

        add_msg_player_or_npc(
            _( "You bounce off a %s!" ), _( "<npcname> bounces off a %s!" ), critter->name() );
        return;
    }

    if( npc * const np = g->critter_at<npc>( to ) ) {
        deal_damage( np, bodypart_id( "torso" ),
                     damage_instance( DT_BASH, static_cast<float>( np->get_size() + 1 ) ) );
        add_effect( effect_stunned, 1_turns );
        np->deal_damage( this, bodypart_id( "torso" ), damage_instance( DT_BASH, 3 ) );
        add_msg_player_or_npc( _( "You bounce off %s!" ), _( "<npcname> bounces off %s!" ), np->name );
        np->check_dead_state();
        return;
    }

    // If we're still in the function at this point, we're actually moving a tile!
    if( g->m.has_flag( "LIQUID", to ) && g->m.has_flag( TFLAG_DEEP_WATER, to ) ) {
        if( !is_npc() ) { avatar_action::swim( g->m, g->u, to ); }
        // TODO: NPCs can't swim!
    } else if( g->m.impassable( to ) ) { // Wait, it's a wall

        // It's some kind of wall.
        // TODO: who knocked us back? Maybe that creature should be the source of the damage?
        apply_damage( nullptr, bodypart_id( "torso" ), 3 );
        add_effect( effect_stunned, 2_turns );
        add_msg_player_or_npc(
            _( "You bounce off a %s!" ), _( "<npcname> bounces off a %s!" ), g->m.obstacle_name( to ) );

    } else { // It's no wall
        setpos( to );

        map& here = get_map();
        here.creature_on_trap( *this );
    }
}

