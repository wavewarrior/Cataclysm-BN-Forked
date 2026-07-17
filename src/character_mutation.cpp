#include "character.h"
#ifdef COOP_ENABLED
#include "coop_mutation_log.h"
#endif
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


static auto get_enchantment_mut_visible(
    const trait_id &, const Character &,
    const enchantment &, const enchantment_source &src
)
{
    auto visitor = []<typename T>( const T & v ) -> bool {
        if constexpr( std::is_same_v<T, const item *> )
    {
        const item *it = v;
        return !it->has_flag( flag_id( "HIDDEN" ) );
        }
        if constexpr( std::is_same_v<T, const mutation *> )
    {
        const mutation *it = v;
        return it->second.show_sprite;
    }
    if constexpr( std::is_same_v<T, const bionic *> )
    {
        const bionic *it = v;
        return it->show_sprite;
    }
    return true;
};

return std::visit( visitor, src );
}

static auto get_enchantment_mut_active(
    const trait_id& mut, const Character &, const enchantment &, const enchantment_source & )
{
    // TODO: When mutations ui can deal with enchantment mutations, update this
    return mut->activated && mut->starts_active;
}

resistances Character::mutation_armor( bodypart_id bp ) const
{
    resistances res;
    for( const trait_id& iter : get_mutations() ) {
        res = res.combined_with( iter->damage_resistance( bp->token ) );
    }

    return res;
}

float Character::mutation_armor( bodypart_id bp, damage_type dt ) const
{
    return mutation_armor( bp ).type_resist( dt );
}

float Character::mutation_armor( bodypart_id bp, const damage_unit& du ) const
{
    return mutation_armor( bp ).get_effective_resist( du );
}

social_modifiers Character::get_mutation_social_mods() const
{
    social_modifiers mods;
    for( const mutation_branch * mut : cached_mutations ) { mods += mut->social_mods; }

    return mods;
}

template <float mutation_branch::* member>
float calc_mutation_value( const std::vector<const mutation_branch*> &mutations )
{
    float lowest = 0.0f;
    float highest = 0.0f;
    for( const mutation_branch * mut : mutations ) {
        float val = mut->*member;
        lowest = std::min( lowest, val );
        highest = std::max( highest, val );
    }

    return std::min( 0.0f, lowest ) + std::max( 0.0f, highest );
}

template <float mutation_branch::* member>
float calc_mutation_value_additive( const std::vector<const mutation_branch*> &mutations )
{
    float ret = 0.0f;
    for( const mutation_branch * mut : mutations ) { ret += mut->*member; }
    return ret;
}

template <float mutation_branch::* member>
float calc_mutation_value_multiplicative( const std::vector<const mutation_branch*> &mutations )
{
    float ret = 1.0f;
    for( const mutation_branch * mut : mutations ) { ret *= mut->*member; }
    return ret;
}

static const std::map<std::string, std::function<float( std::vector<const mutation_branch*> )>>
mutation_value_map = {
    {"pain_recovery", calc_mutation_value<&mutation_branch::pain_recovery>},
    {"healing_awake", calc_mutation_value<&mutation_branch::healing_awake>},
    {"healing_resting", calc_mutation_value<&mutation_branch::healing_resting>},
    {"mending_modifier", calc_mutation_value<&mutation_branch::mending_modifier>},
    {"hp_modifier", calc_mutation_value<&mutation_branch::hp_modifier>},
    {"hp_modifier_secondary", calc_mutation_value<&mutation_branch::hp_modifier_secondary>},
    {"hp_adjustment", calc_mutation_value<&mutation_branch::hp_adjustment>},
    {
        "temperature_speed_modifier",
        calc_mutation_value<&mutation_branch::temperature_speed_modifier>
    },
    {"kcal_scale", calc_mutation_value<&mutation_branch::kcal_scale>},
    {"metabolism_modifier", calc_mutation_value<&mutation_branch::metabolism_modifier>},
    {"thirst_modifier", calc_mutation_value<&mutation_branch::thirst_modifier>},
    {"fatigue_regen_modifier", calc_mutation_value<&mutation_branch::fatigue_regen_modifier>},
    {"fatigue_modifier", calc_mutation_value<&mutation_branch::fatigue_modifier>},
    {"stamina_regen_modifier", calc_mutation_value<&mutation_branch::stamina_regen_modifier>},
    {"stealth_modifier", calc_mutation_value<&mutation_branch::stealth_modifier>},
    {"str_modifier", calc_mutation_value<&mutation_branch::str_modifier>},
    {"bleed_resist", calc_mutation_value<&mutation_branch::bleed_resist>},
    {"dodge_modifier", calc_mutation_value_additive<&mutation_branch::dodge_modifier>},
    {"mana_modifier", calc_mutation_value_additive<&mutation_branch::mana_modifier>},
    {"mana_multiplier", calc_mutation_value_multiplicative<&mutation_branch::mana_multiplier>},
    {
        "mana_regen_multiplier",
        calc_mutation_value_multiplicative<&mutation_branch::mana_regen_multiplier>
    },
    {
        "mutagen_target_modifier",
        calc_mutation_value_additive<&mutation_branch::mutagen_target_modifier>
    },
    {"speed_modifier", calc_mutation_value_multiplicative<&mutation_branch::speed_modifier>},
    {
        "movecost_modifier",
        calc_mutation_value_multiplicative<&mutation_branch::movecost_modifier>
    },
    {
        "movecost_flatground_modifier",
        calc_mutation_value_multiplicative<&mutation_branch::movecost_flatground_modifier>
    },
    {
        "movecost_obstacle_modifier",
        calc_mutation_value_multiplicative<&mutation_branch::movecost_obstacle_modifier>
    },
    {
        "packmule_modifier",
        calc_mutation_value_multiplicative<&mutation_branch::packmule_modifier>
    },
    {
        "crafting_speed_modifier",
        calc_mutation_value_multiplicative<&mutation_branch::crafting_speed_modifier>
    },
    {
        "construction_speed_modifier",
        calc_mutation_value_multiplicative<&mutation_branch::construction_speed_modifier>
    },
    {
        "attackcost_modifier",
        calc_mutation_value_multiplicative<&mutation_branch::attackcost_modifier>
    },
    {
        "falling_damage_multiplier",
        calc_mutation_value_multiplicative<&mutation_branch::falling_damage_multiplier>
    },
    {
        "max_stamina_modifier",
        calc_mutation_value_multiplicative<&mutation_branch::max_stamina_modifier>
    },
    {
        "weight_capacity_modifier",
        calc_mutation_value_multiplicative<&mutation_branch::weight_capacity_modifier>
    },
    {
        "hearing_modifier",
        calc_mutation_value_multiplicative<&mutation_branch::hearing_modifier>
    },
    {
        "movecost_swim_modifier",
        calc_mutation_value_multiplicative<&mutation_branch::movecost_swim_modifier>
    },
    {"noise_modifier", calc_mutation_value_multiplicative<&mutation_branch::noise_modifier>},
    {"overmap_sight", calc_mutation_value_multiplicative<&mutation_branch::overmap_sight>},
    {
        "overmap_multiplier",
        calc_mutation_value_multiplicative<&mutation_branch::overmap_multiplier>
    },
    {"night_vision_range", calc_mutation_value<&mutation_branch::night_vision_range>},
    {
        "reading_speed_multiplier",
        calc_mutation_value_multiplicative<&mutation_branch::reading_speed_multiplier>
    },
    {
        "skill_rust_multiplier",
        calc_mutation_value_multiplicative<&mutation_branch::skill_rust_multiplier>
    }
};

float Character::mutation_value( const std::string& val ) const
{
    // Syntax similar to tuple get<n>()
    const auto found = mutation_value_map.find( val );

    if( found == mutation_value_map.end() ) {
        debugmsg( "Invalid mutation value name %s", val );
        return 0.0f;
    } else {
        return found->second( cached_mutations );
    }
}

float Character::healing_rate( float at_rest_quality ) const
{
    // TODO: Cache
    float heal_rate;
    if( !is_npc() ) {
        heal_rate = get_option<float>( "PLAYER_HEALING_RATE" );
    } else {
        heal_rate = get_option<float>( "NPC_HEALING_RATE" );
    }
    float awake_rate = heal_rate * mutation_value( "healing_awake" );
    float final_rate = 0.0f;
    if( awake_rate > 0.0f ) {
        final_rate += awake_rate;
    } else if( at_rest_quality < 1.0f ) {
        // Resting protects from rot
        final_rate += ( 1.0f - at_rest_quality ) * awake_rate;
    }
    float asleep_rate = 0.0f;
    if( at_rest_quality > 0.0f ) {
        asleep_rate = at_rest_quality * heal_rate * ( 1.0f + mutation_value( "healing_resting" ) );
    }
    if( asleep_rate > 0.0f ) { final_rate += asleep_rate * ( 1.0f + get_healthy() / 200.0f ); }

    // Most common case: awake player with no regenerative abilities
    // ~7e-5 is 1 hp per day, anything less than that is totally negligible
    static constexpr float eps = 0.000007f;
    add_msg( m_debug, "%s healing: %.6f", name, final_rate );
    if( std::abs( final_rate ) < eps ) { return 0.0f; }

    float primary_hp_mod = mutation_value( "hp_modifier" );
    if( primary_hp_mod < 0.0f ) {
        // HP mod can't get below -1.0
        final_rate *= 1.0f + primary_hp_mod;
    }

    return final_rate;
}

float Character::healing_rate_medicine( float at_rest_quality, const bodypart_id& bp ) const
{
    float rate_medicine = 0.0f;
    float bandaged_rate = 0.0f;
    float disinfected_rate = 0.0f;

    const effect& e_bandaged = get_effect( effect_bandaged, bp.id() );
    const effect& e_disinfected = get_effect( effect_disinfected, bp.id() );

    if( !e_bandaged.is_null() ) {
        bandaged_rate +=
            static_cast<float>( e_bandaged.get_amount( "HEAL_RATE" ) ) / to_turns<int>( 24_hours );
        if( bp == bodypart_id( "head" ) ) {
            bandaged_rate *= e_bandaged.get_amount( "HEAL_HEAD" ) / 100.0f;
        }
        if( bp == bodypart_id( "torso" ) ) {
            bandaged_rate *= e_bandaged.get_amount( "HEAL_TORSO" ) / 100.0f;
        }
    }

    if( !e_disinfected.is_null() ) {
        disinfected_rate +=
            static_cast<float>( e_disinfected.get_amount( "HEAL_RATE" ) ) / to_turns<int>( 24_hours );
        if( bp == bodypart_id( "head" ) ) {
            disinfected_rate *= e_disinfected.get_amount( "HEAL_HEAD" ) / 100.0f;
        }
        if( bp == bodypart_id( "torso" ) ) {
            disinfected_rate *= e_disinfected.get_amount( "HEAL_TORSO" ) / 100.0f;
        }
    }

    rate_medicine += bandaged_rate + disinfected_rate;
    rate_medicine *= 1.0f + mutation_value( "healing_resting" );
    rate_medicine *= 1.0f + at_rest_quality;

    // increase healing if character has both effects
    if( !e_bandaged.is_null() && !e_disinfected.is_null() ) { rate_medicine *= 2; }

    if( get_healthy() > 0.0f ) {
        rate_medicine *= 1.0f + get_healthy() / 200.0f;
    } else {
        rate_medicine *= 1.0f + get_healthy() / 400.0f;
    }
    float primary_hp_mod = mutation_value( "hp_modifier" );
    if( primary_hp_mod < 0.0f ) {
        // HP mod can't get below -1.0
        rate_medicine *= 1.0f + primary_hp_mod;
    }
    return rate_medicine;
}

void Character::build_mut_dependency_map(
    const trait_id& mut, std::unordered_map<trait_id, int> &dependency_map, int distance )
{
    // Skip base traits and traits we've seen with a lower distance
    const auto lowest_distance = dependency_map.find( mut );
    if( !has_base_trait( mut )
        && ( lowest_distance == dependency_map.end() || distance < lowest_distance->second ) ) {
        dependency_map[mut] = distance;
        // Recurse over all prerequisite and replacement mutations
        const mutation_branch& mdata = mut.obj();
        for( const trait_id& i : mdata.prereqs ) {
            build_mut_dependency_map( i, dependency_map, distance + 1 );
        }
        for( const trait_id& i : mdata.prereqs2 ) {
            build_mut_dependency_map( i, dependency_map, distance + 1 );
        }
        for( const trait_id& i : mdata.replacements ) {
            build_mut_dependency_map( i, dependency_map, distance + 1 );
        }
    }
}

void Character::set_highest_cat_level()
{
    mutation_category_level.clear();

    // For each of our mutations...
    for( const trait_id& mut : get_mutations() ) {
        // ...build up a map of all prerequisite/replacement mutations along the tree, along with
        // their distance from the current mutation
        std::unordered_map<trait_id, int> dependency_map;
        build_mut_dependency_map( mut, dependency_map, 0 );

        // Then use the map to set the category levels
        for( const std::pair<const trait_id, int> &i : dependency_map ) {
            const mutation_branch& mdata = i.first.obj();
            if( !mdata.flags.contains( flag_NON_THRESH ) ) {
                for( const mutation_category_id& cat : mdata.category ) {
                    // Decay category strength based on how far it is from the current mutation
                    mutation_category_level[cat] += 8 / static_cast<int>( std::pow( 2, i.second ) );
                }
            }
        }
    }
}

void Character::drench_mut_calc()
{
    for( std::pair<const bodypart_str_id, bodypart> &elem : get_body() ) {
        int ignored = 0;
        int neutral = 0;
        int good = 0;

        for( const trait_id& iter : get_mutations() ) {
            const mutation_branch& mdata = iter.obj();
            const auto wp_iter = mdata.protection.find( elem.first->token );
            if( wp_iter != mdata.protection.end() ) {
                ignored += wp_iter->second.x;
                neutral += wp_iter->second.y;
                good += wp_iter->second.z;
            }
        }

        std::array<int, static_cast<size_t>( water_tolerance::NUM_WATER_TOLERANCE )> mut_drench;
        mut_drench[static_cast<size_t>( water_tolerance::WT_GOOD )] = good;
        mut_drench[static_cast<size_t>( water_tolerance::WT_NEUTRAL )] = neutral;
        mut_drench[static_cast<size_t>( water_tolerance::WT_IGNORED )] = ignored;
        elem.second.set_mut_drench( mut_drench );
    }
}

mutation_category_id Character::get_highest_category() const
{
    int iLevel = 0;
    mutation_category_id sMaxCat;

    for( const std::pair<const mutation_category_id, int> &elem : mutation_category_level ) {
        if( elem.second > iLevel ) {
            sMaxCat = elem.first;
            iLevel = elem.second;
        } else if( elem.second == iLevel ) {
            sMaxCat = mutation_category_id(); // no category on ties
        }
    }
    return sMaxCat;
}

void Character::recalculate_enchantment_cache()
{
    // start by resetting the cache
    *enchantment_cache = enchantment();
    enchantment_sources.clear();

    visit_items( [&]( const item * it ) {
        for( const enchantment& ench : it->get_enchantments() ) {
            if( ench.is_active( *this, *it ) ) {
                enchantment_cache->force_add( ench );
                enchantment_sources.emplace_back( &ench, it );
            }
        }
        return VisitResponse::NEXT;
    } );

    // get from traits/ mutations
    for( const std::pair<const trait_id, char_trait_data> &mut_map : my_mutations ) {
        const mutation_branch& mut = mut_map.first.obj();

        for( const enchantment_id& ench_id : mut.enchantments ) {
            const enchantment& ench = ench_id.obj();
            if( ench.is_active( *this, mut.activated && mut_map.second.powered ) ) {
                enchantment_cache->force_add( ench );
                enchantment_sources.emplace_back( &ench, &mut_map );
            }
        }
    }

    for( const bionic& bio : get_bionic_collection() ) {
        const bionic_id& bid = bio.id;

        for( const enchantment_id& ench_id : bid->enchantments ) {
            const enchantment& ench = ench_id.obj();
            if( ench.is_active(
                    *this, bio.powered && bid->has_flag( STATIC( flag_id( "BIONIC_TOGGLED" ) ) ) ) ) {
                enchantment_cache->force_add( ench );
                enchantment_sources.emplace_back( &ench, &bio );
            }
        }
    }

    rebuild_mutation_cache();
}

void Character::rebuild_mutation_cache()
{
    cached_mutations.clear();
    for( const std::pair<const trait_id, char_trait_data> &mut : my_mutations ) {
        cached_mutations.push_back( &mut.first.obj() );
    }
    for( const trait_id& mut : enchantment_cache->get_mutations() ) {
        cached_mutations.push_back( &mut.obj() );
    }
}

double Character::bonus_from_enchantments( double base, enchant_vals::mod value, bool round ) const
{
    return enchantment_cache->calc_bonus( value, base, round );
}

bool Character::crossed_threshold() const
{
    // If the thresh category is set, we have to have crossed the threshold
    // This implicitly also checks thresh_tier >= 1 because they get changed at the same time
    if( thresh_category ) {
    return true;
}
for( const trait_id &mut : get_mutations() ) {
        if( mut->threshold ) {
            return true;
        }
    }
    return false;
}

void Character::update_type_of_scent( bool init )
{
    scenttype_id new_scent = scenttype_id( "sc_human" );
    for( const trait_id& mut : get_mutations() ) {
        if( mut.obj().scent_typeid ) { new_scent = mut.obj().scent_typeid.value(); }
    }

    if( !init && new_scent != get_type_of_scent() ) { g->scent.reset(); }
    set_type_of_scent( new_scent );
}

void Character::update_type_of_scent( const trait_id& mut, bool gain )
{
    const std::optional<scenttype_id> &mut_scent = mut->scent_typeid;
    if( mut_scent ) {
        if( gain && mut_scent.value() != get_type_of_scent() ) {
            set_type_of_scent( mut_scent.value() );
            g->scent.reset();
        } else {
            update_type_of_scent();
        }
    }
}

void Character::set_type_of_scent( const scenttype_id& id ) { type_of_scent = id; }

scenttype_id Character::get_type_of_scent() const { return type_of_scent; }

void Character::restore_scent()
{
    const std::string prev_scent = get_value( "prev_scent" );
    if( !prev_scent.empty() ) {
        remove_effect( effect_masked_scent );
        set_type_of_scent( scenttype_id( prev_scent ) );
        remove_value( "prev_scent" );
        remove_value( "waterproof_scent" );
        add_msg_if_player( m_info, _( "You smell like yourself again." ) );
    }
}

void Character::spores()
{
    map& here = get_map();
    fungal_effects fe( *g, here );
    //~spore-release sound
    sounds::sound( bub_pos(), 10, sounds::sound_t::combat, _( "Pouf!" ), false, "misc", "puff" );
    for( const auto& sporep : here.points_in_radius( bub_pos(), 1 ) ) {
        if( sporep == bub_pos() ) { continue; }
        fe.fungalize( sporep, this, fungal_opt.spore_chance );
    }
}

void Character::blossoms()
{
    // Player blossoms are shorter-ranged, but you can fire much more frequently if you like.
    sounds::sound( bub_pos(), 10, sounds::sound_t::combat, _( "Pouf!" ), false, "misc", "puff" );
    map& here = get_map();
    for( const auto& tmp : here.points_in_radius( bub_pos(), 2 ) ) {
        here.add_field( tmp, fd_fungal_haze, rng( 1, 2 ) );
    }
}

void Character::update_vitamins( const vitamin_id& vit )
{
    if( is_npc() ) {
        return; // NPCs cannot develop vitamin diseases
    }

    efftype_id def = vit.obj().deficiency();
    efftype_id exc = vit.obj().excess();

    int lvl = vit.obj().severity( vitamin_get( vit ) );
    if( lvl <= 0 ) { remove_effect( def ); }
    if( lvl >= 0 ) { remove_effect( exc ); }
    if( lvl > 0 ) {
        if( has_effect( def, bodypart_str_id::NULL_ID() ) ) {
            get_effect( def, bodypart_str_id::NULL_ID() ).set_intensity( lvl, true );
        } else {
            add_effect( def, 1_turns, bodypart_str_id::NULL_ID(), lvl );
        }
    }
    if( lvl < 0 ) {
        if( has_effect( exc, bodypart_str_id::NULL_ID() ) ) {
            get_effect( exc, bodypart_str_id::NULL_ID() ).set_intensity( -lvl, true );
        } else {
            add_effect( exc, 1_turns, bodypart_str_id::NULL_ID(), -lvl );
        }
    }
}

void Character::rooted_message() const
{
    bool wearing_shoes = is_wearing_shoes( side::LEFT ) || is_wearing_shoes( side::RIGHT );
    if( ( has_trait( trait_ROOTS2 ) || has_trait( trait_ROOTS3 ) )
        && get_map().has_flag( flag_PLOWABLE, bub_pos() ) && !wearing_shoes ) {
        add_msg( m_info, _( "You sink your roots into the soil." ) );
    }
}

void Character::rooted()
// Should average a point every two minutes or so
{
    double shoe_factor = footwear_factor();
    if( ( has_trait( trait_ROOTS2 ) || has_trait( trait_ROOTS3 ) )
        && get_map().has_flag( flag_PLOWABLE, bub_pos() ) && shoe_factor != 1.0 ) {
        if( one_in( 96 ) ) {
            vitamin_mod( vitamin_id( "iron" ), 1, true );
            vitamin_mod( vitamin_id( "calcium" ), 1, true );
        }
        if( get_thirst() <= thirst_levels::turgid && x_in_y( 75, 425 ) ) { mod_thirst( -1 ); }
        mod_healthy_mod( 5, 50 );
    }
}

bool Character::has_opposite_trait( const trait_id &flag ) const
{
for( const trait_id &i : flag->cancels ) {
    if( has_trait( i ) ) {
            return true;
        }
    }
for( const std::pair<const trait_id, char_trait_data> &mut : my_mutations ) {
    for( const trait_id &canceled_trait : mut.first->cancels ) {
            if( canceled_trait == flag ) {
                return true;
            }
        }
    }
    return false;
}

