#include "action_time_scale.h"
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


static void temp_equalizer(
    Character& c, const bodypart_str_id& bp1_id, const bodypart_str_id& bp2_id )
{
    auto iter_lhs = c.get_body().find( bp1_id );
    if( iter_lhs == c.get_body().end() ) {
        // @todo Rewrite this to handle exotic body types
        return;
    }
    auto iter_rhs = c.get_body().find( bp2_id );
    if( iter_rhs == c.get_body().end() ) { return; }
    // Body heat is moved around.
    // If bp1 is warmer, it will lose heat
    bodypart& bp1 = iter_lhs->second;
    bodypart& bp2 = iter_rhs->second;
    int diff = static_cast<int>( ( bp2.get_temp_cur() - bp1.get_temp_cur() ) * 0.001 );
    bp1.set_temp_cur( bp1.get_temp_cur() + diff );
    bp2.set_temp_cur( bp2.get_temp_cur() - diff );
}


// *INDENT-OFF*

int Character::get_stored_kcal() const { return stored_calories; }

void Character::mod_stored_kcal( int nkcal ) { set_stored_kcal( stored_calories + nkcal ); }

void Character::mod_stored_nutr( int nnutr )
{
    // nutr is legacy type code, this function simply converts old nutrition to new kcal
    mod_stored_kcal( -1 * std::round( nnutr * 2500.0f / ( 12 * 24 ) ) );
}

void Character::set_stored_kcal( int kcal )
{
    if( stored_calories != kcal ) { stored_calories = std::min( kcal, max_stored_kcal() ); }
}

int Character::max_stored_kcal() const { return 2500 * 7 * ( 1.0f + mutation_value( "kcal_scale" ) ); }

float Character::get_kcal_percent() const
{
    return static_cast<float>( get_stored_kcal() ) / static_cast<float>( max_stored_kcal() );
}

int Character::get_thirst() const { return thirst; }

std::pair<std::string, nc_color> Character::get_thirst_description() const
{
    int thirst = get_thirst();
    std::string hydration_string;
    nc_color hydration_color = c_white;
    if( thirst > thirst_levels::parched ) {
        hydration_color = c_light_red;
        hydration_string = _( "Parched" );
    } else if( thirst > thirst_levels::dehydrated ) {
        hydration_color = c_light_red;
        hydration_string = _( "Dehydrated" );
    } else if( thirst > thirst_levels::very_thirsty ) {
        hydration_color = c_yellow;
        hydration_string = _( "Very thirsty" );
    } else if( thirst > thirst_levels::thirsty ) {
        hydration_color = c_yellow;
        hydration_string = _( "Thirsty" );
    } else if( thirst > thirst_levels::slaked ) {
        // Nothing
    } else if( thirst > thirst_levels::hydrated ) {
        hydration_color = c_green;
        hydration_string = _( "Hydrated" );
    } else if( thirst > thirst_levels::turgid ) {
        hydration_color = c_green;
        hydration_string = _( "Turgid" );
    }
    return std::make_pair( hydration_string, hydration_color );
}

std::pair<std::string, nc_color> Character::get_hunger_description() const
{
    int total_kcal = stored_calories + stomach.get_calories();
    int max_kcal = max_stored_kcal();
    float days_left = static_cast<float>( total_kcal ) / bmr();
    float days_max = static_cast<float>( max_kcal ) / bmr();
    std::string hunger_string;
    nc_color hunger_color = c_white;
    if( days_left >= days_max ) {
        hunger_string = _( "Engorged" );
        hunger_color = c_pink;
    } else if( days_max - days_left < 0.5f ) {
        hunger_string = _( "Sated" );
        hunger_color = c_green;
    } else if( days_max - days_left < 1.0f ) {
        hunger_string = _( "Hungry" );
        hunger_color = c_yellow;
    } else if( days_max / days_left < 2.0f ) {
        hunger_string = _( "Very Hungry" );
        hunger_color = c_yellow;
    } else if( days_left > 1 ) {
        hunger_string = _( "Famished" );
        hunger_color = c_light_red;
    } else {
        hunger_string = _( "Starving" );
        hunger_color = c_red;
    }

    if( get_option<std::string>( "HEALTH_STYLE" ) == "number" ) {
        hunger_string = string_format( "%d kcal", total_kcal );
    }

    return std::make_pair( hunger_string, hunger_color );
}

std::pair<std::string, nc_color> Character::get_fatigue_description() const
{
    int fatigue = get_fatigue();
    std::string fatigue_string;
    nc_color fatigue_color = c_white;
    if( fatigue > fatigue_levels::exhausted ) {
        fatigue_color = c_red;
        fatigue_string = _( "Exhausted" );
    } else if( fatigue > fatigue_levels::dead_tired ) {
        fatigue_color = c_light_red;
        fatigue_string = _( "Dead Tired" );
    } else if( fatigue > fatigue_levels::tired ) {
        fatigue_color = c_yellow;
        fatigue_string = _( "Tired" );
    }
    return std::make_pair( fatigue_string, fatigue_color );
}

void Character::mod_thirst( int nthirst )
{
    if( has_trait_flag( flag_NO_THIRST ) ) { return; }
    set_thirst( std::max( -100, thirst + nthirst ) );
}

void Character::set_thirst( int nthirst )
{
    if( thirst != nthirst ) {
        thirst = nthirst;
        on_stat_change( "thirst", thirst );
    }
}

void Character::mod_fatigue( int nfatigue ) { set_fatigue( fatigue + nfatigue ); }

void Character::mod_sleep_deprivation( int nsleep_deprivation )
{
    set_sleep_deprivation( sleep_deprivation + nsleep_deprivation );
}

void Character::set_fatigue( int nfatigue )
{
    nfatigue = std::max( nfatigue, 0 );
    if( fatigue != nfatigue ) {
        fatigue = nfatigue;
        on_stat_change( "fatigue", fatigue );
    }
}

void Character::set_sleep_deprivation( int nsleep_deprivation )
{
    sleep_deprivation = std::
                        min( static_cast<int>( sleep_deprivation_levels::massive ), std::max( 0, nsleep_deprivation ) );
}

int Character::get_fatigue() const { return fatigue; }

int Character::get_sleep_deprivation() const { return sleep_deprivation; }

std::pair<std::string, nc_color> Character::get_pain_description() const
{
    const std::pair<std::string, nc_color> pain = Creature::get_pain_description();
    nc_color pain_color = pain.second;
    std::string pain_string;
    // get pain color
    if( get_perceived_pain() >= 60 ) {
        pain_color = c_red;
    } else if( get_perceived_pain() >= 40 ) {
        pain_color = c_light_red;
    }
    // get pain string
    if( ( get_option<std::string>( "HEALTH_STYLE" ) == "number" || has_effect( effect_got_checked ) )
        && get_perceived_pain() > 0 ) {
        pain_string = string_format( "%s %d", _( "Pain " ), get_perceived_pain() );
    } else if( get_perceived_pain() > 0 ) {
        pain_string = pain.first;
    }
    return std::make_pair( pain_string, pain_color );
}

bool Character::is_deaf() const
{
    return get_effect_int( effect_deaf ) > 2 || worn_with_flag( flag_DEAF ) ||
    has_trait( trait_DEAF ) ||
    ( has_active_bionic( bio_earplugs ) && !has_active_bionic( bio_ears ) ) ||
    ( has_trait( trait_M_SKIN3 ) && get_map().has_flag_ter_or_furn( "FUNGUS", bub_pos() )
    && in_sleep_state() );
}

void Character::on_damage_of_type( int adjusted_damage, damage_type type, const bodypart_id& bp )
{
    // Electrical damage has a chance to temporarily incapacitate bionics in the damaged body_part.
    if( type == DT_ELECTRIC ) {
        const time_duration min_disable_time = 10_turns * adjusted_damage;
        for( bionic& i : get_bionic_collection() ) {
            if( !i.powered ) {
                // Unpowered bionics are protected from power surges.
                continue;
            }
            const auto& info = i.info();
            if( info.has_flag( STATIC( flag_id( "BIONIC_SHOCKPROOF" ) ) )
                || info.has_flag( STATIC( flag_id( "BIONIC_FAULTY" ) ) ) ) {
                continue;
            }
            const std::map<bodypart_str_id, int> &bodyparts = info.occupied_bodyparts;
            if( bodyparts.contains( bp.id() ) ) {
                const int bp_hp = get_part_hp_cur( bp );
                // The chance to incapacitate is as high as 50% if the attack deals damage equal to
                // one third of the body part's current health.
                if( x_in_y( adjusted_damage * 3, bp_hp ) && one_in( 2 ) ) {
                    if( i.incapacitated_time == 0_turns ) {
                        add_msg_if_player( m_bad, _( "Your %s bionic shorts out!" ), info.name );
                    }
                    i.incapacitated_time += rng( min_disable_time, 10 * min_disable_time );
                }
            }
        }
    }
}

void Character::reset_bonuses()
{
    // Reset all bonuses to 0 and multipliers to 1.0
    str_bonus = 0;
    dex_bonus = 0;
    per_bonus = 0;
    int_bonus = 0;

    Creature::reset_bonuses();
}

std::string Character::get_weight_string() const
{
    double weight = convert_weight( bodyweight() );
    int display_weight = static_cast<int>( std::round( weight ) );
    return std::to_string( display_weight ) + " " + weight_units();
}

int Character::get_max_healthy() const { return 200; }

void Character::regen( int rate_multiplier )
{
    int pain_ticks = rate_multiplier;
    while( get_pain() > 0 && pain_ticks-- > 0 ) {
        mod_pain( -roll_remainder(
                      ( 0.2f + get_pain() / 50.0f ) * ( 1.0f + mutation_value( "pain_recovery" ) ) ) );
    }

    float rest = rest_quality();
    float heal_rate = healing_rate( rest ) * to_turns<int>( 5_minutes );
    const float broken_regen_mod = clamp( mutation_value( "mending_modifier" ), 0.25f, 1.0f );
    if( heal_rate > 0.0f ) {
        const int heal = roll_remainder( rate_multiplier * heal_rate );

        for( const bodypart_id& bp : get_all_body_parts( true ) ) {
            const int actually_healed = heal_adjusted( *this, bp, heal );
            mod_part_healed_total( bp, actually_healed );
        }
    } else if( heal_rate < 0.0f ) {
        int rot_rate = roll_remainder( rate_multiplier * -heal_rate );
        // Has to be in loop because some effects depend on rounding
        while( rot_rate-- > 0 ) { hurtall( 1, nullptr, false ); }
    }

    // include healing effects
    for( const bodypart_id& bp : get_all_body_parts( true ) ) {
        float healing = healing_rate_medicine( rest, bp ) * to_turns<int>( 5_minutes );

        const bool is_broken = is_limb_broken( bp ) && !worn_with_flag( flag_SPLINT, bp );
        const int healing_apply = roll_remainder( is_broken ? healing * broken_regen_mod : healing );

        heal( bp, healing_apply );

        bodypart& part = get_part( bp );
        if( part.get_damage_bandaged() > 0 ) {
            part.set_damage_bandaged( part.get_damage_bandaged() - healing_apply );
            if( part.get_damage_bandaged() <= 0 ) {
                part.set_damage_bandaged( 0 );
                remove_effect( effect_bandaged, bp.id() );
                add_msg_if_player( _( "Bandaged wounds on your %s healed." ), body_part_name( bp ) );
            }
        }
        if( part.get_damage_disinfected() > 0 ) {
            part.set_damage_disinfected( part.get_damage_disinfected() - healing_apply );
            if( part.get_damage_disinfected() <= 0 ) {
                part.set_damage_disinfected( 0 );
                remove_effect( effect_disinfected, bp.id() );
                add_msg_if_player( _( "Disinfected wounds on your %s healed." ), body_part_name( bp ) );
            }
        }

        // remove effects if the limb was healed by other way
        if( has_effect( effect_bandaged, bp.id() ) && ( get_part( bp ).is_at_max_hp() ) ) {
            part.set_damage_bandaged( 0 );
            remove_effect( effect_bandaged, bp.id() );
            add_msg_if_player( _( "Bandaged wounds on your %s healed." ), body_part_name( bp ) );
        }
        if( has_effect( effect_disinfected, bp.id() ) && ( get_part( bp ).is_at_max_hp() ) ) {
            part.set_damage_disinfected( 0 );
            remove_effect( effect_disinfected, bp.id() );
            add_msg_if_player( _( "Disinfected wounds on your %s healed." ), body_part_name( bp ) );
        }
    }

    if( get_rad() > 0 ) { mod_rad( -roll_remainder( rate_multiplier / 50.0f ) ); }
}

void Character::enforce_minimum_healing()
{
    for( const bodypart_id& bp : get_all_body_parts() ) {
        if( get_part_healed_total( bp ) <= 0 ) { heal( bp, 1 ); }
        set_part_healed_total( bp, 0 );
    }
}

void Character::update_health( int external_modifiers )
{
    if( has_artifact_with( AEP_SICK ) ) {
        // Carrying a sickness artifact makes your health 50 points worse on average
        external_modifiers -= 50;
    }
    // Limit healthy_mod to [-200, 200].
    // This also sets approximate bounds for the character's health.
    if( get_healthy_mod() > get_max_healthy() ) {
        set_healthy_mod( get_max_healthy() );
    } else if( get_healthy_mod() < -200 ) {
        set_healthy_mod( -200 );
    }

    // Active leukocyte breeder will keep your health near 100
    float effective_healthy_mod = get_healthy_mod();
    if( has_active_bionic( bio_leukocyte ) ) {
        // Side effect: dependency
        mod_healthy_mod( -50, -200 );
        effective_healthy_mod = 100;
    }

    // Health tends toward healthy_mod.
    // For small differences, it changes 4 points per day
    // For large ones, up to ~40% of the difference per day
    float health_change = effective_healthy_mod - get_healthy() + external_modifiers;
    mod_healthy( health_change * ( 1 - 0.9971 ) );

    // And healthy_mod decays over time.
    // Slowly near 0, but it's hard to overpower it near +/-100
    set_healthy_mod( get_healthy_mod() * 0.9955f );

    add_msg( m_debug, "Health: %d, Health mod: %d", static_cast<int>( get_healthy() ),
             static_cast<int>( get_healthy_mod() ) );
}

void Character::update_body() { update_body( 1_turns ); }

void Character::update_body( const time_duration& duration )
{
    ZoneScoped;
    update_stamina( to_turns<int>( duration ) );
    update_stomach( duration );
    recalculate_enchantment_cache();

    int three_mins = calendar::ticks_between( duration, 3_minutes );
    for( ; three_mins > 0; three_mins-- ) {
        magic->update_mana( *this->as_player(), to_turns<double>( 3_minutes ) );
    }

    // Is this good enough? I'm concerned that NPCs will magically survive very long periods of time
    // When they otherwise shouldn't. But simply swapping the order might cause problems that
    // an NPC who were simulated would've easily solved to prevent that.
    const int five_mins = calendar::ticks_between( duration, 5_minutes );
    if( five_mins > 0 ) {
        check_needs_extremes();
        update_needs( five_mins );
        regen( five_mins );
    }

    int days_passed = calendar::ticks_between( duration, 24_hours );
    for( ; days_passed > 0; days_passed-- ) { enforce_minimum_healing(); }

    int thirty_mins = calendar::ticks_between( duration, 30_minutes );
    for( ; thirty_mins > 0; thirty_mins-- ) {
        // Radiation kills health even at low doses
        update_health( has_trait( trait_RADIOGENIC ) ? 0 : -get_rad() );
    }

    for( const auto& v : vitamin::all() ) {
        const time_duration rate = vitamin_rate( v.first );
        if( rate > 0_turns ) {
            int qty = calendar::ticks_between( duration, rate );
            if( qty > 0 ) { vitamin_mod( v.first, 0 - qty ); }

        } else if( rate < 0_turns ) {
            // mutations can result in vitamins being generated (but never accumulated)
            int qty = calendar::ticks_between( duration, -rate );
            if( qty > 0 ) { vitamin_mod( v.first, qty ); }
        }
    }

    do_skill_rust( duration );
}

item *Character::best_quality_item( const quality_id& qual )
{
    std::vector<item *> qual_inv = items_with( [qual]( const item & itm ) {
        return itm.has_quality( qual );
    } );
    item* best_qual = random_entry( qual_inv );
    for( const auto elem : qual_inv ) {
        if( elem->get_quality( qual ) > best_qual->get_quality( qual ) ) { best_qual = elem; }
    }
    return best_qual;
}

namespace
{
constexpr int metabolic_base_kcals = 2500;
} // namespace

void Character::update_stomach( const time_duration& duration )
{
    const needs_rates rates = calc_needs_rates();
    // No food/thirst/fatigue clock at all
    const bool debug_ls = has_trait( trait_DEBUG_LS );
    // No food/thirst, capped fatigue clock (only up to tired)
    const bool npc_no_food = is_npc() && get_option<bool>( "NO_NPC_FOOD" );
    const bool foodless = debug_ls || npc_no_food;
    const bool mouse = has_trait( trait_NO_THIRST );
    const bool mycus = has_trait( trait_M_DEPENDENT );
    const float kcal_per_time = rates.hunger * metabolic_base_kcals / ( 12.0f * 24.0f );
    const int five_mins = calendar::ticks_between( duration, 5_minutes );

    if( five_mins > 0 ) {
        // Digest nutrients in stomach
        food_summary digested_to_body = stomach.digest( rates, five_mins );
        // Apply nutrients, unless this is an NPC and NO_NPC_FOOD is enabled.
        if( !npc_no_food ) {
            mod_stored_kcal( digested_to_body.nutr.kcal );
            vitamins_mod( digested_to_body.nutr.vitamins, false );
        }
        if( !foodless && rates.hunger > 0.0f ) {
            // instead of hunger keeping track of how you're living, burn calories instead
            mod_stored_kcal( -roll_remainder( five_mins * kcal_per_time ) );
        }
    }

    if( !foodless && rates.thirst > 0.0f ) { mod_thirst( roll_remainder( five_mins * rates.thirst ) ); }

    if( npc_no_food ) {
        set_thirst( static_cast<int>( thirst_levels::hydrated ) );
        set_stored_kcal( max_stored_kcal() );
    }

    // Mycus and Metabolic Rehydration makes thirst unnecessary
    // since water is not limited by intake but by absorption, we can just set thirst to zero
    if( mycus || mouse ) { set_thirst( 0 ); }
}

void Character::update_needs( int rate_multiplier )
{
    const int current_stim = get_stim();
    // Hunger, thirst, & fatigue up every 5 minutes
    effect& sleep = get_effect( effect_sleep );
    // No food/thirst/fatigue clock at all
    const bool debug_ls = has_trait( trait_DEBUG_LS );
    // No food/thirst, capped fatigue clock (only up to tired)
    const bool npc_no_food = is_npc() && get_option<bool>( "NO_NPC_FOOD" );
    const bool asleep = !sleep.is_null();
    const bool lying = asleep || has_effect( effect_lying_down ) || activity->id() == ACT_TRY_SLEEP;

    needs_rates rates = calc_needs_rates();

    const bool wasnt_fatigued = get_fatigue() <= fatigue_levels::dead_tired;
    // Don't increase fatigue if sleeping or trying to sleep or if we're at the cap.
    if( get_fatigue() < 1050 && !asleep && !debug_ls ) {
        if( rates.fatigue > 0.0f ) {
            int fatigue_roll = roll_remainder( rates.fatigue * rate_multiplier );
            mod_fatigue( fatigue_roll );

            // Synaptic regen bionic stops SD while awake and boosts it while sleeping
            if( !has_active_bionic( bio_synaptic_regen ) ) {
                // fatigue_roll should be around 1 - so the counter increases by 1 every minute on
                // average, but characters who need less sleep will also get less sleep deprived,
                // and vice-versa.

                // Note: Since needs are updated in 5-minute increments, we have to multiply the
                // roll again by
                // 5. If rate_multiplier is > 1, fatigue_roll will be higher and this will work out.
                mod_sleep_deprivation( fatigue_roll * 5 );
            }

            if( npc_no_food && get_fatigue() > fatigue_levels::tired ) {
                set_fatigue( static_cast<int>( fatigue_levels::tired ) );
            }
            if( npc_no_food ) { set_sleep_deprivation( 0 ); }
        }
    } else if( asleep && rates.recovery > 0.0f ) {
        int recovered = roll_remainder( rates.recovery * rate_multiplier );
        // Hibernation prevents waking up until you're hungry or thirsty
        if( get_fatigue() - recovered < -20 && !is_hibernating() ) {
            // Should be wake up, but that could prevent some retroactive regeneration
            sleep.set_duration( 1_turns );
            mod_fatigue( -25 );
        } else {
            if( has_effect( effect_recently_coughed ) ) { recovered *= .5; }
            mod_fatigue( -recovered );

            float rest_modifier = 1.0f;
            // Bionic doubles the base regen
            if( has_active_bionic( bio_synaptic_regen ) ) { rest_modifier += 1.0f; }
            if( has_effect( effect_melatonin_supplements ) ) { rest_modifier += 0.2f; }

            const character_funcs::comfort_level comfort =
                character_funcs::base_comfort_value( *this, bub_pos() ).level;

            // Best possible bed increases recovery by 30% of base
            if( comfort >= character_funcs::comfort_level::very_comfortable ) {
                rest_modifier += 0.3f;
            } else if( comfort >= character_funcs::comfort_level::comfortable ) {
                rest_modifier += 0.2f;
            } else if( comfort >= character_funcs::comfort_level::slightly_comfortable ) {
                rest_modifier += 0.1f;
            }

            // 6 hours of sleep per day will let you avoid deprivation
            // 4 hours if on great bed plus melatonin
            // Math: 5 (fatigue to minutes), 3 (1:3 sleep to waking),
            // 2 (legacy sleep non-linearity thing)
            mod_sleep_deprivation( -rest_modifier * ( recovered * 3.0f * 5.0f / 2.0f ) );
        }
    }
    if( is_player() && wasnt_fatigued && get_fatigue() > fatigue_levels::dead_tired && !lying ) {
        if( !activity ) {
            add_msg_if_player( m_warning, _( "You're feeling tired.  %s to lie down for sleep." ),
                               press_x( ACTION_SLEEP ) );
        } else {
            g->cancel_activity_query( _( "You're feeling tired." ) );
        }
    }

    if( current_stim < 0 ) {
        set_stim( std::min( current_stim + rate_multiplier, 0 ) );
    } else if( current_stim > 0 ) {
        set_stim( std::max( current_stim - rate_multiplier, 0 ) );
    }

    if( get_painkiller() > 0 ) { mod_painkiller( -std::min( get_painkiller(), rate_multiplier ) ); }

    // Huge folks take penalties for cramming themselves in vehicles
    if( in_vehicle && ( get_size() == creature_size::huge )
        && !( has_trait( trait_NOPAIN ) || has_effect( effect_narcosis ) ) ) {
        vehicle* veh = veh_pointer_or_null( get_map().veh_at( bub_pos() ) );
        // it's painful to work the controls, but passengers in open topped vehicles are fine
        if( veh && ( veh->enclosed_at( bub_pos() ) || veh->player_in_control( *this->as_player() ) ) ) {
            add_msg_if_player(
                m_bad,
                _( "You're cramping up from stuffing yourself in this "
                   "vehicle." ) );
            if( is_npc() ) {
                npc& as_npc = dynamic_cast<npc &>( *this );
                as_npc.complain_about( "cramped_vehicle", 1_hours, "<cramped_vehicle>", false );
            }

            mod_pain( rng( 4, 6 ) );
            focus_pool -= 1;
        }
    }
}
needs_rates Character::calc_needs_rates() const
{
    const effect& sleep = get_effect( effect_sleep );
    const bool has_recycler = has_bionic( bio_recycler );
    const bool asleep = !sleep.is_null();

    needs_rates rates;
    rates.hunger = metabolic_rate();

    add_msg_if_player( m_debug, "Metabolic rate: %.2f", rates.hunger );

    static const std::string player_thirst_rate( "PLAYER_THIRST_RATE" );
    rates.thirst = get_option<float>( player_thirst_rate );
    static const std::string thirst_modifier( "thirst_modifier" );
    rates.thirst *=
        1.0f + mutation_value( thirst_modifier )
        + bonus_from_enchantments( 1.0, enchant_vals::mod::THIRST );
    if( worn_with_flag( flag_SLOWS_THIRST ) ) { rates.thirst *= 0.7f; }

    static const std::string player_fatigue_rate( "PLAYER_FATIGUE_RATE" );
    rates.fatigue = get_option<float>( player_fatigue_rate );
    static const std::string fatigue_modifier( "fatigue_modifier" );
    rates.fatigue *=
        1.0f + mutation_value( fatigue_modifier )
        + bonus_from_enchantments( 1.0, enchant_vals::mod::FATIGUE );

    // Note: intentionally not in metabolic rate
    if( has_recycler ) {
        // Recycler won't help much with mutant metabolism - it is intended for human one
        rates.hunger = std::min( rates.hunger, std::max( 0.5f, rates.hunger - 0.5f ) );
        rates.thirst = std::min( rates.thirst, std::max( 0.5f, rates.thirst - 0.5f ) );
    }

    if( asleep ) {
        static const std::string fatigue_regen_modifier( "fatigue_regen_modifier" );
        // Multiplied by 2 to account for legacy (bugged to always apply)
        // bonus for sleeping over 2 hours
        rates.recovery = 2.0f * ( 1.0f + mutation_value( fatigue_regen_modifier ) );
        if( is_hibernating() ) {
            // Hunger and thirst advance *much* more slowly whilst we hibernate.
            // This will slow calories consumption enough to go through the 7 days of hibernation
            rates.hunger /= 2.0f;
            rates.thirst /= 14.0f;
        }
        rates.recovery -= static_cast<float>( get_perceived_pain() ) / 60;

    } else {
        rates.recovery = 0;
    }

    if( has_activity( ACT_TREE_COMMUNION ) ) {
        // Much of the body's needs are taken care of by the trees.
        // Hair Roots don't provide any bodily needs.
        if( has_trait( trait_ROOTS2 ) || has_trait( trait_ROOTS3 ) ) {
            rates.hunger *= 0.5f;
            rates.thirst *= 0.5f;
            rates.fatigue *= 0.5f;
        }
    }

    if( has_trait( trait_TRANSPIRATION ) ) {
        // Transpiration, the act of moving nutrients with evaporating water, can take a very heavy
        // toll on your thirst when it's really hot.
        rates.thirst *=
            ( ( units::to_fahrenheit( get_weather().get_temperature( abs_pos() ) ) - 32.5f ) / 40.0f );
    }

    if( is_npc() ) {
        rates.hunger *= 0.25f;
        rates.thirst *= 0.25f;
    }

    rates.thirst = std::max( rates.thirst, 0.0f );
    rates.hunger = std::max( rates.hunger, 0.0f );
    rates.fatigue = std::max( rates.fatigue, 0.0f );
    rates.recovery = std::max( rates.recovery, 0.0f );

    return rates;
}

void Character::check_needs_extremes()
{
    // Check if we've overdosed... in any deadly way.
    if( get_stim() > 250 ) {
        add_msg_if_player( m_bad, _( "You have a sudden heart attack!" ) );
        g->events().send<event_type::dies_from_drug_overdose>( getID(), efftype_id() );
        set_part_hp_cur( bodypart_id( "torso" ), 0 );
    } else if( get_stim() < -200 || get_painkiller() > 240 ) {
        add_msg_if_player( m_bad, _( "Your breathing stops completely." ) );
        g->events().send<event_type::dies_from_drug_overdose>( getID(), efftype_id() );
        set_part_hp_cur( bodypart_id( "torso" ), 0 );
        // taking GHB greatly reduces the amount of stimulation needed to die
    } else if( get_effect_int( effect_took_antinarcoleptic ) && get_stim() < -80 ) {
        add_msg_if_player( m_bad, _( "Your breathing slows down to a stop." ) );
        g->events().send<event_type::dies_from_drug_overdose>( getID(), effect_took_antinarcoleptic );
        set_part_hp_cur( bodypart_id( "torso" ), 0 );
    } else if( has_effect( effect_jetinjector ) && get_effect_dur( effect_jetinjector ) > 40_minutes ) {
        if( !( has_trait( trait_NOPAIN ) ) ) {
            add_msg_if_player( m_bad, _( "Your heart spasms painfully and stops." ) );
        } else {
            add_msg_if_player( _( "Your heart spasms and stops." ) );
        }
        g->events().send<event_type::dies_from_drug_overdose>( getID(), effect_jetinjector );
        set_part_hp_cur( bodypart_id( "torso" ), 0 );
    } else if( get_effect_int( effect_drunk ) > 4 ) {
        add_msg_if_player( m_bad, _( "Your breathing slows down to a stop." ) );
        g->events().send<event_type::dies_from_drug_overdose>( getID(), effect_drunk );
        set_part_hp_cur( bodypart_id( "torso" ), 0 );
    }

    // check if we've starved
    if( is_player() ) {
        if( get_stored_kcal() <= 0 ) {
            add_msg_if_player( m_bad, _( "You have starved to death." ) );
            g->events().send<event_type::dies_of_starvation>( getID() );
            set_part_hp_cur( bodypart_id( "torso" ), 0 );
        } else if( action_time_scale::once_every_this_tick( 6_hours ) ) {
            std::string category;
            if( get_kcal_percent() < 0.1f ) {
                category = "empty_starving";
            } else if( get_kcal_percent() < 0.25f ) {
                category = "empty_emaciated";
            } else if( get_kcal_percent() < 0.5f ) {
                category = "empty_malnutrition";
            } else if( get_kcal_percent() < 0.7f ) {
                category = "empty_low_cal";
            }
            if( !category.empty() ) {
                const translation message = SNIPPET.random_from_category( category ).value_or(
                                                translation() );
                add_msg_if_player( m_warning, message );
            }
        }
    }

    // Check if we're dying of thirst
    if( is_player() && get_thirst() >= thirst_levels::parched ) {
        if( get_thirst() >= thirst_levels::dead ) {
            add_msg_if_player( m_bad, _( "You have died of dehydration." ) );
            g->events().send<event_type::dies_of_thirst>( getID() );
            set_part_hp_cur( bodypart_id( "torso" ), 0 );
        } else if( get_thirst() >= lerp( +thirst_levels::parched, +thirst_levels::dead, 0.333f )
                   && action_time_scale::once_every_this_tick( 30_minutes ) ) {
            add_msg_if_player( m_warning, _( "Even your eyes feel dry…" ) );
        } else if( get_thirst() >= lerp( +thirst_levels::parched, +thirst_levels::dead, 0.666f )
                   && action_time_scale::once_every_this_tick( 30_minutes ) ) {
            add_msg_if_player( m_warning, _( "You are THIRSTY!" ) );
        } else if( action_time_scale::once_every_this_tick( 30_minutes ) ) {
            add_msg_if_player( m_warning, _( "Your mouth feels so dry…" ) );
        }
    }

    // Check if we're falling asleep, unless we're sleeping
    if( get_fatigue() >= fatigue_levels::exhausted + 25 && !in_sleep_state() ) {
        if( get_fatigue() >= fatigue_levels::massive ) {
            add_msg_if_player( m_bad, _( "Survivor sleep now." ) );
            g->events().send<event_type::falls_asleep_from_exhaustion>( getID() );
            mod_fatigue( -10 );
            fall_asleep();
        } else if( get_fatigue() >= 800 && action_time_scale::once_every_this_tick( 30_minutes ) ) {
            add_msg_if_player( m_warning, _( "Anywhere would be a good place to sleep…" ) );
        } else if( action_time_scale::once_every_this_tick( 30_minutes ) ) {
            add_msg_if_player( m_warning, _( "You feel like you haven't slept in days." ) );
        }
    }

    // Even if we're not Exhausted, we really should be feeling lack/sleep earlier
    // Penalties start at Dead Tired and go from there
    if( get_fatigue() >= fatigue_levels::dead_tired && !in_sleep_state() ) {
        if( get_fatigue() >= 700 ) {
            if( action_time_scale::once_every_this_tick( 30_minutes ) ) {
                add_msg_if_player( m_warning, _( "You're too physically tired to stop yawning." ) );
                add_effect( effect_lack_sleep, 30_minutes + 1_turns );
            }
            /** @EFFECT_INT slightly decreases occurrence of short naps when dead tired */
            if( one_in( 50 + int_cur ) ) { fall_asleep( 30_seconds ); }
        } else if( get_fatigue() >= fatigue_levels::exhausted ) {
            if( action_time_scale::once_every_this_tick( 30_minutes ) ) {
                add_msg_if_player( m_warning, _( "How much longer until bedtime?" ) );
                add_effect( effect_lack_sleep, 30_minutes + 1_turns );
            }
            /** @EFFECT_INT slightly decreases occurrence of short naps when exhausted */
            if( one_in( 100 + int_cur ) ) { fall_asleep( 30_seconds ); }
        } else if( get_fatigue() >= fatigue_levels::dead_tired && action_time_scale::once_every_this_tick( 30_minutes ) ) {
            add_msg_if_player( m_warning, _( "*yawn* You should really get some sleep." ) );
            add_effect( effect_lack_sleep, 30_minutes + 1_turns );
        }
    }

    // Sleep deprivation kicks in if lack of sleep is avoided with stimulants or otherwise for long
    // periods of time
    int sleep_deprivation = get_sleep_deprivation();
    float sleep_deprivation_pct =
        sleep_deprivation / static_cast<float>( sleep_deprivation_levels::massive );

    if( sleep_deprivation >= sleep_deprivation_levels::harmless && !in_sleep_state()
        && action_time_scale::once_every_this_tick( 60_minutes )
        && ( !has_effect( effect_meth ) || sleep_deprivation >= sleep_deprivation_levels::massive ) ) {
        if( sleep_deprivation < sleep_deprivation_levels::minor ) {
            add_msg_if_player(
                m_warning,
                _( "Your mind feels tired.  It's been a while since you've "
                   "slept well." ) );
            mod_fatigue( 1 );
        } else if( sleep_deprivation < sleep_deprivation_levels::serious ) {
            add_msg_if_player(
                m_bad,
                _( "Your mind feels foggy from lack of good sleep, and your "
                   "eyes keep trying to close against your will." ) );
            mod_fatigue( 5 );

            if( one_in( 10 ) ) { mod_healthy_mod( -1, 0 ); }
        } else if( sleep_deprivation < sleep_deprivation_levels::major ) {
            add_msg_if_player(
                m_bad,
                _( "Your mind feels weary, and you dread every wakeful minute "
                   "that passes.  You crave sleep, and feel like you're about "
                   "to collapse." ) );
            mod_fatigue( 10 );

            if( one_in( 5 ) ) { mod_healthy_mod( -2, -20 ); }
        } else if( sleep_deprivation < sleep_deprivation_levels::massive ) {
            add_msg_if_player(
                m_bad,
                _( "You haven't slept decently for so long that your whole "
                   "body is screaming for mercy.  It's a miracle that you're "
                   "still awake, but it just feels like a curse now." ) );
            mod_fatigue( 40 );

            mod_healthy_mod( -5, -50 );
        }
        // else you pass out for 20 hours, guaranteed

        // Microsleeps are slightly worse if you're sleep deprived, but not by much. (chance: 1 in
        // (75 + per_cur) at minor sleep deprivation) Note: these can coexist with fatigue-related
        // microsleeps
        /** @EFFECT_PER slightly decreases occurrence of short naps when sleep deprived */
        if( one_in( static_cast<int>( ( 1.0f - sleep_deprivation_pct ) * 75 + get_per() ) ) ) {
            fall_asleep( 30_seconds );
        }


        if( sleep_deprivation >= sleep_deprivation_levels::massive
            || ( ( action_time_scale::once_every_this_tick( 10_minutes )
                   && sleep_deprivation >= sleep_deprivation_levels::major &&
                   /** @EFFECT_PER slightly increases resilience against passing out from sleep
                      deprivation */
                   one_in( static_cast<int>( ( 1.0f - sleep_deprivation_pct ) * 100 ) + get_per() ) ) ) ) {
            add_msg_player_or_npc(
                m_bad,
                _( "Your body collapses due to sleep deprivation, your neglected fatigue rushing "
                   "back all at once, and you pass out on the spot." ),
                _( "<npcname> collapses to the ground from exhaustion." ) );
            if( get_fatigue() < fatigue_levels::exhausted ) {
                set_fatigue( static_cast<int>( fatigue_levels::exhausted ) );
            }

            if( sleep_deprivation >= sleep_deprivation_levels::major ) {
                fall_asleep( 20_hours );
            } else if( sleep_deprivation >= sleep_deprivation_levels::serious ) {
                fall_asleep( 16_hours );
            } else {
                fall_asleep( 12_hours );
            }
        }
    }
}

bool Character::is_hibernating() const
{
    return has_effect( effect_sleep ) && get_kcal_percent() > 0.5f &&
    get_thirst() <= thirst_levels::very_thirsty && has_active_mutation( trait_HIBERNATE );
}

/* Here lies the intended effects of body temperature

Assumption 1 : a naked person is comfortable at 19C/66.2F (31C/87.8F at rest).
Assumption 2 : a "lightly clothed" person is comfortable at 13C/55.4F (25C/77F at rest).
Assumption 3 : the player is always running, thus generating more heat.
Assumption 4 : frostbite cannot happen above 0C temperature.*
* In the current model, a naked person can get frostbite at 1C. This isn't true, but it's a
compromise with using nice whole numbers.

Here is a list of warmth values and the corresponding temperatures in which the player is
comfortable, and in which the player is very cold.

Warmth  Temperature (Comfortable)    Temperature (Very cold)    Notes
  0       19C /  66.2F               -11C /  12.2F               * Naked
 10       13C /  55.4F               -17C /   1.4F               * Lightly clothed
 20        7C /  44.6F               -23C /  -9.4F
 30        1C /  33.8F               -29C / -20.2F
 40       -5C /  23.0F               -35C / -31.0F
 50      -11C /  12.2F               -41C / -41.8F
 60      -17C /   1.4F               -47C / -52.6F
 70      -23C /  -9.4F               -53C / -63.4F
 80      -29C / -20.2F               -59C / -74.2F
 90      -35C / -31.0F               -65C / -85.0F
100      -41C / -41.8F               -71C / -95.8F

WIND POWER
Except for the last entry, pressures are sort of made up...

Breeze : 5mph (1015 hPa)
Strong Breeze : 20 mph (1000 hPa)
Moderate Gale : 30 mph (990 hPa)
Storm : 50 mph (970 hPa)
Hurricane : 100 mph (920 hPa)
HURRICANE : 185 mph (880 hPa) [Ref: Hurricane Wilma]
*/

void Character::update_bodytemp( const map& m, const weather_manager& weather )
{
    if( has_trait( trait_DEBUG_NOTEMP ) ) {
        for( auto& pr : get_body() ) {
            pr.second.set_temp_cur( BODYTEMP_NORM );
            pr.second.set_temp_conv( BODYTEMP_NORM );
        }
        return;
    }
    /* Cache calls to g->get_temperature( player position ), used in several places in function */
    const auto player_local_temp = weather.get_temperature( abs_pos() );
    // NOTE : visit weather.h for some details on the numbers used
    // In Celsius / 100
    int Ctemperature = units::to_millidegree_celsius( player_local_temp ) / 10;
    const w_point& weather_point = get_weather().get_precise();
    int vehwindspeed = 0;
    const optional_vpart_position vp = m.veh_at( bub_pos() );
    if( vp ) { vehwindspeed = std::lround( cmps_to_mps( std::abs( vp->vehicle().velocity ) ) * 2.23694 ); }
    const oter_id& cur_om_ter = get_overmapbuffer( get_dimension() ).ter( abs_omt_pos() );
    bool sheltered = weather::is_sheltered( m, bub_pos() );
    double total_windpower = get_local_windpower(
                                 weather.windspeed + vehwindspeed, cur_om_ter, abs_pos(), weather.winddirection, sheltered );
    int air_humidity = get_local_humidity( weather_point.humidity, weather.weather_id, sheltered );
    // Let's cache this not to check it num_bp times
    const bool has_bark = has_trait( trait_BARK );
    const bool has_heatsink =
        has_bionic( bio_heatsink ) || is_wearing( itype_rm13_armor_on ) || has_trait( trait_M_SKIN2 )
        || has_trait( trait_M_SKIN3 );
    const bool has_climate_control = in_climate_control();
    const bool use_floor_warmth = can_use_floor_warmth();
    // In bodytemp units
    const int ambient_norm = 1900 - BODYTEMP_NORM;

    /**
     * Calculations that affect all body parts equally go here, not in the loop
     */
    const int sunlight_warmth =
        weather::is_in_sunlight( m, bub_pos(), weather.weather_id )
        ? ( weather.weather_id->sun_intensity == sun_intensity_type::high ? 1000 : 500 )
        : 0;
    const int best_fire = get_heat_radiation( bub_pos(), true );
    const bool pyromania = has_trait( trait_PYROMANIA );

    const int lying_warmth = use_floor_warmth ? floor_warmth( bub_pos() ) : 0;
    const int water_temperature_raw =
        units::to_millidegree_celsius( weather.get_water_temperature( abs_pos() ) ) / 10;
    // Rescale so that 0C is 0 (FREEZING) and 30C is 5k (NORM).
    const int water_temperature = water_temperature_raw * 5 / 3;

    // Correction of body temperature due to traits and mutations
    // Lower heat is applied always
    const int mutation_heat_low = bodytemp_modifier_traits( true );
    const int mutation_heat_high = bodytemp_modifier_traits( false );
    // Difference between high and low is the "safe" heat - one we only apply if it's beneficial
    const int mutation_heat_bonus = mutation_heat_high - mutation_heat_low;

    // Note: this is included in @ref weather::get_temperature(), so don't add to bodytemp!
    const int h_radiation = get_heat_radiation( bub_pos(), false );

    // If you're standing in water, air temperature is replaced by water temperature. No wind.
    const ter_id ter_at_pos = m.ter( bub_pos() );
    const bool submerged = !in_vehicle && ter_at_pos->has_flag( TFLAG_DEEP_WATER );
    const bool submerged_low = !in_vehicle && ( submerged || ter_at_pos->has_flag( TFLAG_SWIMMABLE ) );

    std::map<bodypart_id, std::vector<const item *>> clothing_map;
    std::map<bodypart_id, std::vector<const item *>> bonus_clothing_map;
    for( auto& pr : get_body() ) {
        const bodypart_id& bp_id = pr.first;
        clothing_map.emplace( bp_id, std::vector<const item*>() );
        bonus_clothing_map.emplace( bp_id, std::vector<const item*>() );
        // HACK: we're using temp_conv here to temporarily save
        //       temperature values from before equalization.
        bodypart& bp = pr.second;
        bp.set_temp_conv( bp.get_temp_cur() );
    }

    // EQUALIZATION
    // We run it outside the loop because we can and so we should
    // Also, it makes bonus heat application more stable
    // TODO: Affect future convection temperature instead (might require adding back to loop)
    temp_equalizer( *this, body_part_torso, body_part_arm_l );
    temp_equalizer( *this, body_part_torso, body_part_arm_r );
    temp_equalizer( *this, body_part_torso, body_part_leg_l );
    temp_equalizer( *this, body_part_torso, body_part_leg_r );
    temp_equalizer( *this, body_part_torso, body_part_head );

    temp_equalizer( *this, body_part_arm_l, body_part_hand_l );
    temp_equalizer( *this, body_part_arm_r, body_part_hand_r );

    temp_equalizer( *this, body_part_leg_l, body_part_foot_l );
    temp_equalizer( *this, body_part_leg_r, body_part_foot_r );

    const auto& all_bps = get_all_body_parts();
    for( const item * const& it : worn ) {
        // TODO: Port body part set id changes
        const body_part_set& covered = it->get_covered_body_parts();
        for( const bodypart_id& bp : all_bps ) {
            if( covered.test( bp.id() ) ) { clothing_map[bp.id()].emplace_back( it ); }
            if( it->has_flag( flag_HOOD ) ) { bonus_clothing_map[body_part_head].emplace_back( it ); }
            if( it->has_flag( flag_COLLAR ) ) { bonus_clothing_map[body_part_mouth].emplace_back( it ); }
            if( it->has_flag( flag_POCKETS ) ) {
                bonus_clothing_map[body_part_hand_l].emplace_back( it );
                bonus_clothing_map[body_part_hand_r].emplace_back( it );
            }
        }
    }
    // If player is wielding something large, pockets are not usable
    if( primary_weapon().volume() >= 500_ml ) {
        bonus_clothing_map[body_part_hand_l].clear();
        bonus_clothing_map[body_part_hand_r].clear();
    }
    // If player's head is encumbered, hood can't be put up
    if( encumb( body_part_head ) >= 10 ) { bonus_clothing_map[body_part_head].clear(); }
    // Similar for mouth
    if( encumb( body_part_mouth ) >= 10 ) { bonus_clothing_map[body_part_mouth].clear(); }

    std::map<bodypart_id, int> warmth_per_bp = warmth::from_clothing( clothing_map );
    std::map<bodypart_id, int> bonus_warmth_per_bp = warmth::bonus_from_clothing(
            bonus_clothing_map );
    for( const auto& pr : warmth::from_effects( *this ) ) { warmth_per_bp[pr.first] += pr.second; }

    std::map<bodypart_id, int> wind_res_per_bp = warmth::wind_resistance_from_clothing(
            clothing_map );
    std::map<bodypart_id, int> wind_res_per_bp_bonus = warmth::wind_resistance_from_clothing(
            bonus_clothing_map );
    for( std::pair<const bodypart_id, int> &bp_wind_res : wind_res_per_bp ) {
        int exposed = std::max( 0, 100 - bp_wind_res.second );
        int exposed_bonus = std::max( 0, 100 - wind_res_per_bp_bonus.at( bp_wind_res.first ) );
        int exposed_final = exposed * exposed_bonus / ( 100 * 100 );
        bp_wind_res.second = 100 - exposed_final;
    }
    if( has_active_mutation( trait_SHELL2 ) ) {
        for( std::pair<const bodypart_id, int> &bp_wind_res : wind_res_per_bp ) {
            bp_wind_res.second = 100;
        }
    }
    // We might not use this at all, so leave it empty
    // If we do need to use it, we'll initialize it (once) there
    std::map<bodypart_id, int> fire_armor_per_bp;

    // Current temperature and converging temperature calculations
    for( auto& pr : get_body() ) {
        const bodypart_id& bp = pr.first;
        // Skip eyes
        if( bp == bodypart_id( "eyes" ) ) { continue; }

        bodypart& bp_stats = pr.second;

        const bool submerged_bp =
            submerged
            || ( submerged_low
                 && ( bp == body_part_foot_l || bp == body_part_foot_r || bp == body_part_leg_l
                      || bp == body_part_leg_r ) );
        // This adjusts the temperature scale to match the bodytemp scale
        const int adjusted_temp = submerged_bp ? water_temperature : ( Ctemperature - ambient_norm );

        // Represents the fact that the body generates heat when it is cold.
        double scaled_temperature =
            logarithmic_range( BODYTEMP_VERY_COLD, BODYTEMP_VERY_HOT, bp_stats.get_temp_cur() );
        // Produces a smooth curve between 30.0 and 60.0.
        double homeostasis_adjustment = 30.0 * ( 1.0 + scaled_temperature );
        int clothing_warmth_adjustment = static_cast<int>(
                                             homeostasis_adjustment * warmth_per_bp[bp] );
        int clothing_warmth_adjusted_bonus = static_cast<int>(
                homeostasis_adjustment * bonus_warmth_per_bp[bp] );
        // WINDCHILL
        double bp_windpower = total_windpower * ( 1 - wind_res_per_bp[bp] / 100.0 );
        // Calculate windchill
        int windchill =
            submerged_bp
            ? 0
            : get_local_windchill(
                units::to_fahrenheit( player_local_temp ), air_humidity, bp_windpower );

        // Convergent temperature is affected by ambient temperature,
        // clothing warmth, and body wetness.
        int bp_conv =
            adjusted_temp + windchill * 100 + clothing_warmth_adjustment + mutation_heat_low
            + sunlight_warmth;

        // Bark : lowers blister count to -5; harder to get blisters
        // If the counter is high, your skin starts to burn
        int blister_count = ( has_bark ? -5 : 0 );

        if( bp_stats.get_frostbite_timer() > 0 ) {
            bp_stats.set_frostbite_timer( bp_stats.get_frostbite_timer() - std::min( 5, h_radiation ) );
        }
        blister_count +=
            h_radiation - 111 > 0 ? std::max( static_cast<int>( std::sqrt( h_radiation - 111 ) ), 0 ) : 0;

        if( has_heatsink ) { blister_count -= 20; }
        if( fire_armor_per_bp.empty() && blister_count > 0 ) {
            fire_armor_per_bp = get_armor_fire( clothing_map );
        }
        // BLISTERS : Skin gets blisters from intense heat exposure.
        // Fire protection protects from blisters.
        // Heatsinks give near-immunity.
        if( blister_count - fire_armor_per_bp[bp] > 0 ) {
            add_effect( effect_blisters, 1_turns, bp.id() );
            if( pyromania ) {
                add_morale( MORALE_PYROMANIA_NEARFIRE, 10, 10, 1_hours,
                            30_minutes ); // Proximity that's close enough to harm us gives us a bit
                // of a thrill
                rem_morale( MORALE_PYROMANIA_NOFIRE );
            }
        } else if( pyromania && best_fire >= 1 ) { // Only give us fire bonus if there's actually
            // fire
            add_morale( MORALE_PYROMANIA_NEARFIRE, 5, 5, 30_minutes,
                        15_minutes ); // Gain a much smaller mood boost even if it doesn't hurt us
            rem_morale( MORALE_PYROMANIA_NOFIRE );
        }

        // Climate Control eases the effects of high and low ambient temps
        if( has_climate_control ) { bp_conv = temp_corrected_by_climate_control( bp_conv ); }

        int bonus_fire_warmth = best_fire * 500;

        const int comfortable_warmth = bonus_fire_warmth + lying_warmth;
        const int bonus_warmth =
            comfortable_warmth + mutation_heat_bonus + clothing_warmth_adjusted_bonus;
        if( bonus_warmth > 0 ) {
            // Approximate bp_conv needed to reach comfortable temperature in this very turn
            // Basically inverted formula for temp_cur below
            int desired = 501 * BODYTEMP_NORM - 499 * bp_stats.get_temp_cur();
            if( std::abs( BODYTEMP_NORM - desired ) < 1000 ) {
                desired = BODYTEMP_NORM; // Ensure that it converges
            } else if( desired > BODYTEMP_HOT ) {
                desired = BODYTEMP_HOT; // Cap excess at sane temperature
            }

            if( desired < bp_conv ) {
                // Too hot, can't help here
            } else if( desired < bp_conv + bonus_warmth ) {
                // Use some heat, but not all of it
                bp_conv = desired;
            } else {
                // Use all the heat
                bp_conv += bonus_warmth;
            }

            // Morale bonus for comfiness - only if actually comfy (not too warm/cold)
            // Spread the morale bonus in time.
            if( comfortable_warmth > 0 &&
                // TODO: make this simpler and use time_duration/time_point
                to_turn<int>( calendar::turn ) % to_turns<int>( 1_minutes )
                == to_turns<int>( 1_minutes * bp->token ) / to_turns<int>( 1_minutes * num_bp )
                && get_effect_int( effect_cold ) == 0 && get_effect_int( effect_hot ) == 0
                && bp_stats.get_temp_cur() > BODYTEMP_COLD
                && bp_stats.get_temp_cur() <= BODYTEMP_NORM ) {
                add_morale( MORALE_COMFY, 1, 10, 2_minutes, 1_minutes, true );
            }
        }

        // The current temperature model can't account for water temperature conduction well
        // Hack: cut non-water effects by 80% when in water
        if( submerged_bp ) { bp_conv = ( ( bp_conv - adjusted_temp ) / 5 ) + adjusted_temp; }

        // Because we don't actually model insulation very well at the moment, clothes are
        // oppressive in Summer So we make them half as effective at making you uncomfortably hot as
        // they are at making you not-cold
        if( bp_conv >= BODYTEMP_NORM ) {
            int bp_without_clothes = bp_conv - clothing_warmth_adjustment;
            if( bp_without_clothes >= BODYTEMP_NORM ) {
                // If the heat is above normal, clothes start to contribute less
                bp_conv -= clothing_warmth_adjustment / 2;
            } else {
                // Do the same to any clothing that contributes to above normal
                int clothes_to_norm = BODYTEMP_NORM - bp_without_clothes;
                bp_conv -= ( clothing_warmth_adjustment - clothes_to_norm ) / 2;
            }
        }

        // FINAL CALCULATION : Increments current body temperature towards convergent.
        int temp_before = bp_stats.get_temp_cur();
        int temp_difference = temp_before - bp_conv; // Negative if the player is warming up.
        int rounding_error = 0;
        // If temp_diff is small, the player cannot warm up due to rounding errors. This fixes that.
        if( temp_difference < 0 && temp_difference > -600 ) { rounding_error = 1; }
        // exp(-0.001) : half life of 60 minutes, exp(-0.002) : half life of 30 minutes,
        // exp(-0.003) : half life of 20 minutes, exp(-0.004) : half life of 15 minutes
        static const double change_mult_air = std::exp( -0.002 );
        static const double change_mult_water = std::exp( -0.008 );
        const double change_mult = submerged_bp ? change_mult_water : change_mult_air;
        if( bp_stats.get_temp_cur() != bp_conv ) {
            bp_stats.set_temp_cur(
                static_cast<int>( temp_difference * change_mult ) + bp_conv + rounding_error );
        }
        int temp_after = bp_stats.get_temp_cur();
        // PENALTIES
        if( bp_stats.get_temp_cur() < BODYTEMP_FREEZING ) {
            add_effect( effect_cold, 1_turns, bp.id(), 3 );
        } else if( bp_stats.get_temp_cur() < BODYTEMP_VERY_COLD ) {
            add_effect( effect_cold, 1_turns, bp.id(), 2 );
        } else if( bp_stats.get_temp_cur() < BODYTEMP_COLD ) {
            add_effect( effect_cold, 1_turns, bp.id(), 1 );
        } else if( bp_stats.get_temp_cur() > BODYTEMP_SCORCHING ) {
            add_effect( effect_hot, 1_turns, bp.id(), 3 );
            if( bp->main_part.id() == bp ) { add_effect( effect_hot_speed, 1_turns, bp.id(), 3 ); }
        } else if( bp_stats.get_temp_cur() > BODYTEMP_VERY_HOT ) {
            add_effect( effect_hot, 1_turns, bp.id(), 2 );
            if( bp->main_part.id() == bp ) { add_effect( effect_hot_speed, 1_turns, bp.id(), 2 ); }
        } else if( bp_stats.get_temp_cur() > BODYTEMP_HOT ) {
            add_effect( effect_hot, 1_turns, bp.id(), 1 );
            if( bp->main_part.id() == bp ) { add_effect( effect_hot_speed, 1_turns, bp.id(), 1 ); }
        } else {
            if( bp_stats.get_temp_cur() >= BODYTEMP_COLD ) { remove_effect( effect_cold, bp.id() ); }
            if( bp_stats.get_temp_cur() <= BODYTEMP_HOT ) {
                remove_effect( effect_hot, bp.id() );
                remove_effect( effect_hot_speed, bp.id() );
            }
        }

        // FROSTBITE - only occurs to hands, feet, face
        /**

        Source : http://www.atc.army.mil/weather/windchill.pdf

        Temperature and wind chill are main factors, mitigated by clothing warmth. Each 10 warmth
        protects against 2C of cold.

        1200 turns in low risk, + 3 tics
        450 turns in moderate risk, + 8 tics
        50 turns in high risk, +72 tics

        Let's say frostnip @ 1800 tics, frostbite @ 3600 tics

        >> Chunked into 8 parts (http://imgur.com/xlTPmJF)
        -- 2 hour risk --
        Between 30F and 10F
        Between 10F and -5F, less than 20mph, -4x + 3y - 20 > 0, x : F, y : mph
        -- 45 minute risk --
        Between 10F and -5F, less than 20mph, -4x + 3y - 20 < 0, x : F, y : mph
        Between 10F and -5F, greater than 20mph
        Less than -5F, less than 10 mph
        Less than -5F, more than 10 mph, -4x + 3y - 170 > 0, x : F, y : mph
        -- 5 minute risk --
        Less than -5F, more than 10 mph, -4x + 3y - 170 < 0, x : F, y : mph
        Less than -35F, more than 10 mp
        **/

        if( bp == body_part_mouth || bp == body_part_foot_r || bp == body_part_foot_l
            || bp == body_part_hand_r || bp == body_part_hand_l ) {
            // Handle the frostbite timer
            // Need temps in F, windPower already in mph
            int wetness_percentage =
                100 * bp_stats.get_wetness() / bp_stats.get_drench_capacity(); // 0 - 100
            // Warmth gives a slight buff to temperature resistance
            // Wetness gives a heavy nerf to temperature resistance
            double adjusted_warmth = warmth_per_bp.at( bp ) - wetness_percentage;
            int Ftemperature = static_cast<int>(
                                   units::to_fahrenheit( player_local_temp ) + 0.2 * adjusted_warmth );
            // Windchill reduced by your armor
            int FBwindPower = static_cast<int>( total_windpower * ( 1 - wind_res_per_bp[bp] / 100.0 ) );

            int intense = get_effect_int( effect_frostbite, bp.id() );

            // This has been broken down into 8 zones
            // Low risk zones (stops at frostnip)
            if( bp_stats.get_temp_cur() < BODYTEMP_COLD
                && ( ( Ftemperature < 30 && Ftemperature >= 10 )
                     || ( Ftemperature < 10 && Ftemperature >= -5 && FBwindPower < 20
                          && -4 * Ftemperature + 3 * FBwindPower - 20 >= 0 ) ) ) {
                if( bp_stats.get_frostbite_timer() < 2000 ) {
                    bp_stats.set_frostbite_timer( bp_stats.get_frostbite_timer() + 3 );
                }
                if( one_in( 100 ) && !has_effect( effect_frostbite, bp.id() ) ) {
                    add_msg( m_warning, _( "Your %s will be frostnipped in the next few hours." ),
                             body_part_name( bp->token ) );
                }
                // Medium risk zones
            } else if(
                bp_stats.get_temp_cur() < BODYTEMP_COLD
                && ( ( Ftemperature < 10 && Ftemperature >= -5 && FBwindPower < 20
                       && -4 * Ftemperature + 3 * FBwindPower - 20 < 0 )
                     || ( Ftemperature < 10 && Ftemperature >= -5 && FBwindPower >= 20 )
                     || ( Ftemperature < -5 && FBwindPower < 10 )
                     || ( Ftemperature < -5 && FBwindPower >= 10
                          && -4 * Ftemperature + 3 * FBwindPower - 170 >= 0 ) ) ) {
                bp_stats.set_frostbite_timer( bp_stats.get_frostbite_timer() + 8 );
                if( one_in( 100 ) && intense < 2 ) {
                    add_msg( m_warning, _( "Your %s will be frostbitten within the hour!" ),
                             body_part_name( bp->token ) );
                }
                // High risk zones
            } else if(
                bp_stats.get_temp_cur() < BODYTEMP_COLD
                && ( ( Ftemperature < -5 && FBwindPower >= 10
                       && -4 * Ftemperature + 3 * FBwindPower - 170 < 0 )
                     || ( Ftemperature < -35 && FBwindPower >= 10 ) ) ) {
                bp_stats.set_frostbite_timer( bp_stats.get_frostbite_timer() + 72 );
                if( one_in( 100 ) && intense < 2 ) {
                    add_msg( m_warning, _( "Your %s will be frostbitten any minute now!" ),
                             body_part_name( bp->token ) );
                }
                // Risk free, so reduce frostbite timer
            } else {
                bp_stats.set_frostbite_timer( bp_stats.get_frostbite_timer() - 3 );
            }

            // Handle the bestowing of frostbite
            if( bp_stats.get_frostbite_timer() < 0 ) {
                bp_stats.set_frostbite_timer( 0 );
            } else if( bp_stats.get_frostbite_timer() > 4200 ) {
                // This ensures that the player will recover in at most 3 hours.
                bp_stats.set_frostbite_timer( 4200 );
            }
            // Frostbite, no recovery possible
            if( bp_stats.get_frostbite_timer() >= 3600 ) {
                add_effect( effect_frostbite, 1_turns, bp.id(), 2 );
                remove_effect( effect_frostbite_recovery, bp.id() );
                // Else frostnip, add recovery if we were frostbitten
            } else if( bp_stats.get_frostbite_timer() >= 1800 ) {
                if( intense == 2 ) { add_effect( effect_frostbite_recovery, 1_turns, bp.id() ); }
                add_effect( effect_frostbite, 1_turns, bp.id(), 1 );
                // Else fully recovered
            } else if( bp_stats.get_frostbite_timer() == 0 ) {
                remove_effect( effect_frostbite, bp.id() );
                remove_effect( effect_frostbite_recovery, bp.id() );
            }
        }
        // Warn the player if condition worsens
        // HACK: we want overall temperature change, including equalization, and temp_conv
        //       at this moment contains temperature values from before the equalization.
        temp_before = bp_stats.get_temp_conv();
        if( temp_before > BODYTEMP_FREEZING && temp_after <= BODYTEMP_FREEZING ) {
            //~ %s is bodypart
            add_msg( m_warning, _( "You feel your %s beginning to go numb from the cold!" ),
                     body_part_name( bp->token ) );
        } else if( temp_before > BODYTEMP_VERY_COLD && temp_after <= BODYTEMP_VERY_COLD ) {
            //~ %s is bodypart
            add_msg( m_warning, _( "You feel your %s getting very cold." ), body_part_name( bp->token ) );
        } else if( temp_before > BODYTEMP_COLD && temp_after <= BODYTEMP_COLD ) {
            //~ %s is bodypart
            add_msg( m_warning, _( "You feel your %s getting chilly." ), body_part_name( bp->token ) );
        } else if( temp_before < BODYTEMP_SCORCHING && temp_after >= BODYTEMP_SCORCHING ) {
            //~ %s is bodypart
            add_msg( m_bad, _( "You feel your %s getting red hot from the heat!" ),
                     body_part_name( bp->token ) );
        } else if( temp_before < BODYTEMP_VERY_HOT && temp_after >= BODYTEMP_VERY_HOT ) {
            //~ %s is bodypart
            add_msg( m_warning, _( "You feel your %s getting very hot." ), body_part_name( bp->token ) );
        } else if( temp_before < BODYTEMP_HOT && temp_after >= BODYTEMP_HOT ) {
            //~ %s is bodypart
            add_msg( m_warning, _( "You feel your %s getting warm." ), body_part_name( bp->token ) );
        }

        // Note: Numbers are based off of BODYTEMP at the top of weather.h
        // If torso is BODYTEMP_COLD which is 34C, the early stages of hypothermia begin
        // constant shivering will prevent the player from falling asleep.
        // Otherwise, if any other body part is BODYTEMP_VERY_COLD, or 31C
        // AND you have frostbite, then that also prevents you from sleeping
        if( in_sleep_state() ) {
            int curr_temperature = bp_stats.get_temp_cur();
            if( bp == body_part_torso && curr_temperature <= BODYTEMP_COLD ) {
                add_msg( m_warning, _( "Your shivering prevents you from sleeping." ) );
                wake_up();
            } else if( bp != body_part_torso && curr_temperature <= BODYTEMP_VERY_COLD
                       && has_effect( effect_frostbite ) ) {
                add_msg( m_warning,
                         _( "You are too cold.  Your frostbite prevents you from "
                            "sleeping." ) );
                wake_up();
            }
        }

        // Warn the player that wind is going to be a problem.
        // But only if it can be a problem, no need to spam player with "wind chills your scorching
        // body"
        if( bp_conv <= BODYTEMP_COLD && windchill < -10 && one_in( 200 ) ) {
            add_msg( m_bad, _( "The wind is making your %s feel quite cold." ),
                     body_part_name( bp->token ) );
        } else if( bp_conv <= BODYTEMP_COLD && windchill < -20 && one_in( 100 ) ) {
            add_msg( m_bad,
                     _( "The wind is very strong, you should find some more wind-resistant clothing "
                        "for your %s." ),
                     body_part_name( bp->token ) );
        } else if( bp_conv <= BODYTEMP_COLD && windchill < -30 && one_in( 50 ) ) {
            add_msg( m_bad,
                     _( "Your clothing is not providing enough protection from the wind for your "
                        "%s!" ),
                     body_part_name( bp->token ) );
        }

        // Set temp_conv just once per bp for readability
        // TODO: Remove temp_conv, it's only really for display, so should not be in Character
        bp_stats.set_temp_conv( bp_conv );
    }
}

int Character::get_part_temp_cur( const bodypart_id& id ) const
{
    return get_part( id ).get_temp_cur();
}

void Character::set_part_temp_cur( const bodypart_id& id, int temp )
{
    get_part( id ).set_temp_cur( temp );
}

std::map<bodypart_id, int> Character::get_temp_cur()
{
    std::map<bodypart_id, int> temps;

    for( auto& pr : get_body() ) {
        bodypart& bp = pr.second;
        temps[bp.get_id()] = bp.get_temp_cur();
    }
    return temps;
}

void Character::set_temp_cur( int temp )
{
    for( auto& pr : get_body() ) {
        bodypart& bp = pr.second;
        bp.set_temp_cur( temp );
    }
}

int Character::bmr() const { return metabolic_rate_base() * metabolic_base_kcals; }
