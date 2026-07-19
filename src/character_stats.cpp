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


int Character::get_str() const { return std::max( 0, get_str_base() + str_bonus ); }

int Character::get_dex() const { return std::max( 0, get_dex_base() + dex_bonus ); }

int Character::get_per() const { return std::max( 0, get_per_base() + per_bonus ); }

int Character::get_int() const { return std::max( 0, get_int_base() + int_bonus ); }

int Character::get_str_base() const { return str_max; }

int Character::get_dex_base() const { return dex_max; }

int Character::get_per_base() const { return per_max; }

int Character::get_int_base() const { return int_max; }

int Character::get_str_bonus() const { return str_bonus; }

int Character::get_dex_bonus() const { return dex_bonus; }

int Character::get_per_bonus() const { return per_bonus; }

int Character::get_int_bonus() const { return int_bonus; }

int Character::get_speed() const
{
    if( is_mounted() ) {
    return mounted_creature.get()->get_speed();
    }
    return Creature::get_speed();
}

int Character::ranged_dex_mod() const
{
    ///\EFFECT_DEX <20 increases ranged penalty
    return std::max( ( 20.0 - get_dex() ) * 0.5, 0.0 );
}

int Character::ranged_per_mod() const
{
    ///\EFFECT_PER <20 increases ranged aiming penalty.
    return std::max( ( 20.0 - get_per() ) * 1.2, 0.0 );
}

float Character::get_healthy() const { return healthy; }

float Character::get_healthy_mod() const { return healthy_mod; }

void Character::set_str_bonus( int nstr )
{
    str_bonus = nstr;
    str_cur = std::max( 0, str_max + str_bonus );
}

void Character::set_dex_bonus( int ndex )
{
    dex_bonus = ndex;
    dex_cur = std::max( 0, dex_max + dex_bonus );
}

void Character::set_per_bonus( int nper )
{
    per_bonus = nper;
    per_cur = std::max( 0, per_max + per_bonus );
}

void Character::set_int_bonus( int nint )
{
    int_bonus = nint;
    int_cur = std::max( 0, int_max + int_bonus );
}

void Character::mod_str_bonus( int nstr )
{
    str_bonus += nstr;
    str_cur = std::max( 0, str_max + str_bonus );
}

void Character::mod_dex_bonus( int ndex )
{
    dex_bonus += ndex;
    dex_cur = std::max( 0, dex_max + dex_bonus );
}

void Character::mod_per_bonus( int nper )
{
    per_bonus += nper;
    per_cur = std::max( 0, per_max + per_bonus );
}

void Character::mod_int_bonus( int nint )
{
    int_bonus += nint;
    int_cur = std::max( 0, int_max + int_bonus );
}

void Character::print_health() const
{
    if( !is_player() ) {
    return;
}
int current_health = get_healthy();
if( get_option<std::string>( "HEALTH_STYLE" ) == "number" ) {
    add_msg_if_player( _( "Your current health value is %d." ), current_health );
    }

    static const std::map<int, std::string> msg_categories = {
        {-100, "health_horrible"}, {-50, "health_very_bad"},  {-10, "health_bad"},      {10, ""},
        {50, "health_good"},       {100, "health_very_good"}, {INT_MAX, "health_great"}
    };

    auto iter = msg_categories.lower_bound( current_health );
    if( iter != msg_categories.end() && !iter->second.empty() ) {
    const translation msg = SNIPPET.random_from_category( iter->second ).value_or( translation() );
        add_msg_if_player( current_health > 0 ? m_good : m_bad, "%s", msg );
    }
}

void Character::set_healthy( float nhealthy ) { healthy = nhealthy; }

void Character::mod_healthy( float nhealthy )
{
    float mut_rate = 1.0f;
    for( const trait_id& mut : get_mutations() ) { mut_rate *= mut.obj().healthy_rate; }
    healthy += nhealthy * mut_rate;
}

void Character::set_healthy_mod( float nhealthy_mod ) { healthy_mod = nhealthy_mod; }

void Character::mod_healthy_mod( float nhealthy_mod, float cap )
{
    // TODO: This really should be a full morale-like system, with per-effect caps
    //       and durations.  This version prevents any single effect from exceeding its
    //       intended ceiling, but multiple effects will overlap instead of adding.

    // Cap indicates how far the mod is allowed to shift in this direction.
    // It can have a different sign to the mod, e.g. for items that treat
    // extremely low health, but can't make you healthy.
    if( nhealthy_mod == 0 || cap == 0 ) { return; }
    float low_cap;
    float high_cap;
    if( nhealthy_mod < 0 ) {
        low_cap = cap;
        high_cap = 200;
    } else {
        low_cap = -200;
        high_cap = cap;
    }

    // If we're already out-of-bounds, we don't need to do anything.
    if( ( healthy_mod <= low_cap && nhealthy_mod < 0 )
        || ( healthy_mod >= high_cap && nhealthy_mod > 0 ) ) {
        return;
    }

    healthy_mod += nhealthy_mod;

    // Since we already bailed out if we were out-of-bounds, we can
    // just clamp to the boundaries here.
    healthy_mod = std::min( healthy_mod, high_cap );
    healthy_mod = std::max( healthy_mod, low_cap );
}

int Character::get_stim() const
{
    return stim;
}

void Character::set_stim( int new_stim )
{
    stim = new_stim;
}

void Character::mod_stim( int mod )
{
    stim += mod;
}

int Character::get_rad() const
{
    return radiation;
}

void Character::set_rad( int new_rad )
{
    radiation = new_rad;
}

void Character::mod_rad( int mod )
{
    if( has_trait_flag( flag_NO_RADIATION ) ) {
        return;
    }
    set_rad( std::max( 0, get_rad() + mod ) );
}

int Character::get_stamina() const
{
    if( has_trait( trait_DEBUG_STAMINA ) ) {
    return get_stamina_max();
    }

    return stamina;
}

int Character::get_stamina_max() const
{
    static const std::string player_max_stamina( "PLAYER_MAX_STAMINA" );
    static const std::string max_stamina_modifier( "max_stamina_modifier" );
    const int baseMaxStamina = get_option<int>( player_max_stamina );
    int maxStamina = baseMaxStamina;
    maxStamina *= Character::mutation_value( max_stamina_modifier );
    maxStamina += bonus_from_enchantments( maxStamina, enchant_vals::mod::STAMINA_CAP );
    return std::max( baseMaxStamina / 10, maxStamina );
}

void Character::set_stamina( int new_stamina ) { stamina = new_stamina; }

void Character::mod_stamina( int mod, bool skill )
{
    // If we're burning stamina then train athletics, unless we're losing stamina due to status
    // effects or other non-standard causes.
    if( skill && mod < 0 ) {
        as_player()->practice( skill_swimming, roll_remainder( std::abs( mod ) / 500.0 ), 10, true );
        // Athletics skill also reduces stamina drain for relevant activities.
        const int skill = get_skill_level( skill_swimming );
        const float skill_cost = std::max( 0.667f, ( ( 30.0f - skill ) / 30.0f ) );
        mod *= skill_cost;
    }
    stamina += mod;
    if( mod < 0 ) {
        add_msg( m_debug, "Stamina burn: %d", mod );
    } else {
        add_msg( m_debug, "Stamina recovery: %d", mod );
    }
    stamina = clamp( stamina, 0, get_stamina_max() );
}

void Character::mod_stamina( int mod ) { return mod_stamina( mod, true ); }

void Character::burn_move_stamina( int moves )
{
    int overburden_percentage = 0;
    units::mass current_weight = weight_carried();
    // Make it at least 1 gram to avoid divide-by-zero warning
    units::mass max_weight = std::max( weight_capacity(), 1_gram );
    if( current_weight > max_weight ) {
        overburden_percentage = ( current_weight - max_weight ) * 100 / max_weight;
    }

    int burn_ratio = get_option<int>( "PLAYER_BASE_STAMINA_BURN_RATE" );
    for( const bionic_id& bid : get_bionic_fueled_with( *item::spawn_temporary( "muscle" ) ) ) {
        if( has_active_bionic( bid ) ) { burn_ratio = burn_ratio * 2 - 3; }
    }
    burn_ratio += overburden_percentage;
    if( move_mode == CMM_RUN ) { burn_ratio = burn_ratio * 7; }
    mod_stamina( -( ( moves * burn_ratio ) / 100.0 ) * stamina_burn_cost_modifier() );
    // Chance to suffer pain if overburden and stamina runs out or has trait BADBACK
    // Starts at 1 in 25, goes down by 5 for every 50% more carried
    if( ( current_weight > max_weight ) && ( has_trait( trait_BADBACK ) || get_stamina() == 0 )
        && one_in( 35 - 5 * current_weight / ( max_weight / 2 ) ) ) {
        add_msg_if_player( m_bad, _( "Your body strains under the weight!" ) );
        // 1 more pain for every 800 grams more (5 per extra STR needed)
        if( ( ( current_weight - max_weight ) / 800_gram > get_pain() && get_pain() < 100 ) ) {
            mod_pain( 1 );
        }
    }
}

float Character::stamina_burn_cost_modifier() const
{
    // We no longer modify movecost with stamina, but we do modify the stamina cost
    // Convert stamina to a float first to allow for decimal place carrying
    float stamina_modifier = ( static_cast<float>( get_stamina() ) / get_stamina_max() + 1 ) / 2;
    return stamina_modifier * running_move_cost_modifier();
}

float Character::running_move_cost_modifier() const
{
    float movement_modifier = 1.0;
    if( move_mode == CMM_RUN && get_stamina() >= 0 ) {
        // Rationale: Average running speed is 2x walking speed. (NOT sprinting)
        movement_modifier *= 2.0;
    }
    if( is_crouching() ) { movement_modifier *= 0.5; }
    return movement_modifier;
}

void Character::update_stamina( int turns )
{
    static const std::string player_base_stamina_regen_rate( "PLAYER_BASE_STAMINA_REGEN_RATE" );
    static const std::string stamina_regen_modifier( "stamina_regen_modifier" );
    const float base_regen_rate = get_option<float>( player_base_stamina_regen_rate );
    const int current_stim = get_stim();
    float stamina_recovery = 0.0f;
    // Recover some stamina every turn.
    // max stamina modifers from mutation also affect stamina multi
    float stamina_multiplier =
        1.0f + mutation_value( stamina_regen_modifier )
        + ( mutation_value( "max_stamina_modifier" ) - 1.0f )
        + bonus_from_enchantments( 1.0, enchant_vals::mod::STAMINA_REGEN );
    // But mouth encumbrance interferes, even with mutated stamina.
    stamina_recovery +=
        stamina_multiplier * std::max( 1.0f, base_regen_rate - ( encumb( body_part_mouth ) / 5.0f ) );
    // TODO: recovering stamina causes hunger/thirst/fatigue.
    // TODO: Tiredness slowing recovery

    // stim recovers stamina (or impairs recovery)
    if( current_stim > 0 ) {
        // TODO: Make stamina recovery with stims cost health
        stamina_recovery += std::min( 5.0f, current_stim / 15.0f );
    } else if( current_stim < 0 ) {
        // Affect it less near 0 and more near full
        // Negative stim kill at -200
        // At -100 stim it inflicts -20 malus to regen at 100%  stamina,
        // effectivly countering stamina gain of default 20,
        // at 50% stamina its -10 (50%), cuts by 25% at 25% stamina
        // FIXME: this formula is only suitable for advancing by 1 turn
        stamina_recovery += current_stim / 5.0f * get_stamina() / get_stamina_max();
    }
    stamina_recovery = std::max( 0.0f, stamina_recovery );

    const int max_stam = get_stamina_max();
    if( get_power_level() >= 3_kJ && has_active_bionic( bio_gills ) ) {
        int bonus = std::min <
                    int > ( units::to_kilojoule( get_power_level() ) / 3,
                            max_stam - get_stamina() - stamina_recovery * turns );
        // so the effective recovery is up to 5x default
        bonus = std::min( bonus, 4 * static_cast<int>( base_regen_rate ) );
        if( bonus > 0 ) {
            stamina_recovery += bonus;
            bonus /= 10;
            bonus = std::max( bonus, 1 );
            mod_power_level( units::from_kilojoule( -bonus ) );
        }
    }

    mod_stamina( roll_remainder( stamina_recovery * turns ), false );
    // Cap at max
    set_stamina( std::min( std::max( get_stamina(), 0 ), max_stam ) );
}

void Character::update_morale( const time_duration& duration )
{
    morale->decay( duration );
    apply_persistent_morale();
}

void Character::apply_persistent_morale()
{
    // Hoarders get a morale penalty if they're not carrying a full inventory.
    if( has_trait( trait_HOARDER ) ) {
        int pen = ( volume_capacity() - volume_carried() ) / 125_ml;
        if( pen > 70 ) { pen = 70; }
        if( pen <= 0 ) { pen = 0; }
        if( has_effect( effect_took_xanax ) ) {
            pen = pen / 7;
        } else if( has_effect( effect_took_prozac ) ) {
            pen = pen / 2;
        }
        if( pen > 0 ) { add_morale( MORALE_PERM_HOARDER, -pen, -pen, 1_minutes, 1_minutes, true ); }
    }
    // Nomads get a morale penalty if they stay near the same overmap tiles too long.
    if( has_trait( trait_NOMAD ) || has_trait( trait_NOMAD2 ) || has_trait( trait_NOMAD3 ) ) {
        const tripoint_abs_omt ompos = abs_omt_pos();
        float total_time = 0;
        // Check how long we've stayed in any overmap tile within 5 of us.
        const int max_dist = 5;
        for( const tripoint_abs_omt& pos : points_in_radius( ompos, max_dist ) ) {
            const float dist = rl_dist( ompos, pos );
            if( dist > max_dist ) { continue; }
            const auto iter = overmap_time.find( pos.xy() );
            if( iter == overmap_time.end() ) { continue; }
            // Count time in own tile fully, tiles one away as 4/5, tiles two away as 3/5, etc.
            total_time += to_moves<float>( iter->second ) * ( max_dist - dist ) / max_dist;
        }
        // Characters with higher tiers of Nomad suffer worse morale penalties, faster.
        int max_unhappiness;
        float min_time, max_time;
        if( has_trait( trait_NOMAD ) ) {
            max_unhappiness = 20;
            min_time = to_moves<float>( 2_days );
            max_time = to_moves<float>( 4_days );
        } else if( has_trait( trait_NOMAD2 ) ) {
            max_unhappiness = 40;
            min_time = to_moves<float>( 1_days );
            max_time = to_moves<float>( 2_days );
        } else { // traid_NOMAD3
            max_unhappiness = 60;
            min_time = to_moves<float>( 12_hours );
            max_time = to_moves<float>( 1_days );
        }
        // The penalty starts at 1 at min_time and scales up to max_unhappiness at max_time.
        const float t = ( total_time - min_time ) / ( max_time - min_time );
        const int pen = std::ceil( lerp_clamped( 0, max_unhappiness, t ) );
        if( pen > 0 ) { add_morale( MORALE_PERM_NOMAD, -pen, -pen, 1_minutes, 1_minutes, true ); }
    }

    if( has_trait( trait_PROF_FOODP ) ) {
        // Loosing your face is distressing
        if( !( is_wearing( itype_id( "foodperson_mask" ) )
               || is_wearing( itype_id( "foodperson_mask_on" ) ) ) ) {
            add_morale( MORALE_PERM_NOFACE, -20, -20, 1_minutes, 1_minutes, true );
        } else if( is_wearing( itype_id( "foodperson_mask" ) )
                   || is_wearing( itype_id( "foodperson_mask_on" ) ) ) {
            rem_morale( MORALE_PERM_NOFACE );
        }

        if( is_wearing( itype_id( "foodperson_mask_on" ) ) ) {
            add_morale( MORALE_PERM_FPMODE_ON, 10, 10, 1_minutes, 1_minutes, true );
        } else {
            rem_morale( MORALE_PERM_FPMODE_ON );
        }
    }
}

int Character::get_morale_level() const { return morale->get_level(); }

void Character::add_morale(
    const morale_type& type, int bonus, int max_bonus, const time_duration& duration,
    const time_duration& decay_start, bool capped, const itype* item_type )
{
    if( item_type != nullptr ) {
        morale->add( type, bonus, max_bonus, duration, decay_start, capped, *item_type );
    } else {
        morale->add( type, bonus, max_bonus, duration, decay_start, capped );
    }
}

bool Character::has_morale( const morale_type& type ) const { return morale->has( type ); }

int Character::get_morale( const morale_type& type ) const { return morale->get( type ); }

void Character::rem_morale( const morale_type& type ) { morale->remove( type ); }

void Character::clear_morale() { morale->clear(); }

bool Character::has_morale_to_read() const { return get_morale_level() >= -40; }

bool Character::check_and_recover_morale()
{
    player_morale test_morale;

    for( const item * const& wit : worn ) { test_morale.on_item_wear( *wit ); }

    for( const trait_id& mut : get_mutations() ) { test_morale.on_mutation_gain( mut ); }

    for( const auto& elem : *effects ) {
        for( const std::pair<const bodypart_str_id, effect> &_effect_it : elem.second ) {
            const effect& e = _effect_it.second;
            if( !e.is_removed() ) {
                test_morale.on_effect_int_change( e.get_id(), e.get_intensity(), e.get_bp() );
            }
        }
    }

    test_morale.on_stat_change( "kcal", get_stored_kcal() );
    test_morale.on_stat_change( "thirst", get_thirst() );
    test_morale.on_stat_change( "fatigue", get_fatigue() );
    test_morale.on_stat_change( "pain", get_pain() );
    test_morale.on_stat_change( "pkill", get_painkiller() );
    test_morale.on_stat_change( "perceived_pain", get_perceived_pain() );

    apply_persistent_morale();

    if( !morale->consistent_with( test_morale ) ) {
        *morale = player_morale( test_morale ); // Recover consistency
        add_msg( m_debug, "%s morale was recovered.", disp_name( true ) );
        return false;
    }

    return true;
}

void Character::start_hauling()
{
    add_msg( _( "You start hauling items along the ground." ) );
    if( is_armed() ) {
        add_msg( m_warning, _( "Your hands are not free, which makes hauling slower." ) );
    }
    hauling = true;
}

void Character::stop_hauling()
{
    add_msg( _( "You stop hauling items." ) );
    hauling = false;
    if( has_activity( ACT_MOVE_ITEMS ) ) { cancel_activity(); }
}

bool Character::is_hauling() const { return hauling; }

std::unique_ptr<player_activity> Character::remove_activity()
{
    std::unique_ptr<player_activity> ret = activity.release();
    return ret;
}

void Character::assign_activity( std::unique_ptr<player_activity> act, bool allow_resume )
{
    bool resuming = false;
    if( allow_resume && !backlog.empty() && backlog.front()->can_resume_with( *act, *this ) ) {
        resuming = true;
        add_msg_if_player( _( "You resume your task." ) );
        activity = std::move( backlog.front() );
        backlog.pop_front();
    } else {
        if( activity ) { backlog.push_front( std::move( activity ) ); }

        activity = std::move( act );
    }

    activity->start_or_resume( *this, resuming );

    if( is_npc() ) {
        cancel_stashed_activity();
        npc* guy = dynamic_cast<npc *>( this );
        guy->current_activity_id = activity->id();
        if( activity->id() != ACT_ASSIST ) {
            guy->set_attitude( NPCATT_ACTIVITY );
            guy->set_mission( NPC_MISSION_ACTIVITY );
        }
    }
}

bool Character::has_activity( const activity_id& type ) const { return activity->id() == type; }

bool Character::has_activity( const std::vector<activity_id> &types ) const
{
    return std::ranges::contains( types, activity->id() );
}

void Character::cancel_activity()
{
    activity->canceled( *this );
    if( has_activity( ACT_MOVE_ITEMS ) && is_hauling() && !has_haulable_items( bub_pos() ) ) {
        stop_hauling();
    }
    if( has_activity( ACT_TRY_SLEEP ) ) { remove_value( "sleep_query" ); }
    // Clear any backlog items that aren't auto-resume.
    for( auto backlog_item = backlog.begin(); backlog_item != backlog.end(); ) {
        if( ( *backlog_item )->auto_resume ) {
            backlog_item++;
        } else {
            backlog_item = backlog.erase( backlog_item );
        }
    }
    // act wait stamina interrupts an ongoing activity.
    // and automatically puts auto_resume = true on it
    // we don't want that to persist if there is another interruption.
    // and player moves elsewhere.
    if( has_activity( ACT_WAIT_STAMINA ) && !backlog.empty() && backlog.front()->auto_resume ) {
        backlog.front()->auto_resume = false;
    }
    if( activity && activity->is_suspendable() ) {
        backlog.push_front( std::move( activity ) );
        activity = std::make_unique<player_activity>();
    }
    sfx::end_activity_sounds(); // kill activity sounds when canceled
    activity->set_to_null();
}

void Character::resume_backlog_activity()
{
    if( !backlog.empty() && backlog.front()->auto_resume ) {
        activity = std::move( backlog.front() );
        backlog.pop_front();
    }
}

