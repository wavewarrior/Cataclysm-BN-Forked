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


int Character::sight_range( int light_level ) const
{
    if( light_level == 0 ) { return 1; }
    /* Via Beer-Lambert we have:
     * light_level * (1 / exp( LIGHT_TRANSPARENCY_OPEN_AIR * distance) ) <= LIGHT_AMBIENT_LOW
     * Solving for distance:
     * 1 / exp( LIGHT_TRANSPARENCY_OPEN_AIR * distance ) <= LIGHT_AMBIENT_LOW / light_level
     * 1 <= exp( LIGHT_TRANSPARENCY_OPEN_AIR * distance ) * LIGHT_AMBIENT_LOW / light_level
     * light_level <= exp( LIGHT_TRANSPARENCY_OPEN_AIR * distance ) * LIGHT_AMBIENT_LOW
     * log(light_level) <= LIGHT_TRANSPARENCY_OPEN_AIR * distance + log(LIGHT_AMBIENT_LOW)
     * log(light_level) - log(LIGHT_AMBIENT_LOW) <= LIGHT_TRANSPARENCY_OPEN_AIR * distance
     * log(LIGHT_AMBIENT_LOW / light_level) <= LIGHT_TRANSPARENCY_OPEN_AIR * distance
     * log(LIGHT_AMBIENT_LOW / light_level) * (1 / LIGHT_TRANSPARENCY_OPEN_AIR) <= distance
     */
    int range = static_cast<int>(
                    -std::log( get_vision_threshold( static_cast<int>( get_map().ambient_light_at( bub_pos() ) ) )
                               / static_cast<float>( light_level ) )
                    * ( 1.0 / LIGHT_TRANSPARENCY_OPEN_AIR ) );

    // Clamp to [1, sight_max].
    return clamp( range, 1, sight_max );
}

auto Character::unimpaired_range() const -> int
{
    // Cap at g_max_view_distance (runtime bubble radius) so castLight's
    // row-distance limit produces a circular — not square — visible area.
    //
    // NOTE: g_max_view_distance currently equals SEEX * g_half_mapsize — the full
    // half-extent of the map cache — so this cap never bites for a normal character.
    // If a future change decouples actual vision range from bubble size (e.g. a hard
    // cap of ~60 tiles regardless of bubble), update_visibility_cache's inner loops
    // can be clamped to plr_pos ± unimpaired_range() for a large serial-path win.
    return std::min( sight_max, g_max_view_distance );
}

bool Character::overmap_los( const tripoint_abs_omt& omt, int sight_points )
{
    const tripoint_abs_omt ompos = abs_omt_pos();
    const point_rel_omt offset = omt.xy() - ompos.xy();
    if( offset.x() < -sight_points || offset.x() > sight_points || offset.y() < -sight_points
        || offset.y() > sight_points ) {
        // Outside maximum sight range
        return false;
    }

    const auto line = line_to( ompos, omt, 0, 0 );
    for( size_t i = 0; i < line.size() && sight_points >= 0; i++ ) {
        const tripoint_abs_omt& pt = line[i];
        const oter_id& ter = get_overmapbuffer( get_dimension() ).ter( pt );
        sight_points -= static_cast<int>( ter->get_see_cost() );
        if( sight_points < 0 ) { return false; }
    }
    return true;
}

int Character::overmap_sight_range( int light_level ) const
{
    int sight = sight_range( light_level );
    if( sight < SEEX ) { return 0; }
    if( sight <= SEEX * 4 ) { return ( sight / ( SEEX / 2 ) ); }

    sight = 6;
    // The higher your perception, the farther you can see.
    sight += ( get_per() / 2 );
    // The higher up you are, the farther you can see.
    sight += std::max( 0, bub_pos().z() ) * 2;
    // Mutations like Scout and Topographagnosia affect how far you can see.
    sight += mutation_value( "overmap_sight" );

    float multiplier = mutation_value( "overmap_multiplier" );
    // Binoculars double your sight range.
    const bool has_optic =
        ( has_item_with_flag( flag_ZOOM ) || has_bionic( bio_eye_optic )
          || ( is_mounted() && mounted_creature->has_flag( MF_MECH_RECON_VISION ) ) );
    if( has_optic ) { multiplier += 1; }

    sight = std::round( sight * multiplier );
    return std::max( sight, 3 );
}

int Character::clairvoyance() const
{
    if( vision_mode_cache[VISION_CLAIRVOYANCE_SUPER] ) { return MAX_CLAIRVOYANCE; }

    if( vision_mode_cache[VISION_CLAIRVOYANCE_PLUS] ) { return 8; }

    if( vision_mode_cache[VISION_CLAIRVOYANCE] ) { return 3; }

    // 0 would mean we have clairvoyance of own tile
    return -1;
}

bool Character::sight_impaired() const
{
    return ( ( ( has_effect( effect_boomered ) || has_effect( effect_no_sight ) ||
    has_effect( effect_darkness ) ) &&
    ( !( has_trait( trait_PER_SLIME_OK ) ) ) ) ||
    ( is_underwater() && !has_bionic( bio_membrane ) && !has_trait( trait_MEMBRANE ) &&
    !worn_with_flag( flag_SWIM_GOGGLES ) && !has_trait( trait_PER_SLIME_OK ) &&
    !has_trait( trait_CEPH_EYES ) && !has_trait( trait_SEESLEEP ) ) ||
    ( ( has_trait( trait_MYOPIC ) || has_trait( trait_URSINE_EYE ) ) &&
    !worn_with_flag( flag_FIX_NEARSIGHT ) &&
    !has_effect( effect_contacts ) &&
    !has_bionic( bio_eye_optic ) ) ||
    has_trait( trait_PER_SLIME ) );
}

void Character::recalc_sight_limits()
{
    sight_max = 9999;
    vision_mode_cache.reset();

    // Set sight_max.
    if( is_blind() || ( in_sleep_state() && !has_trait( trait_SEESLEEP ) )
        || has_effect( effect_narcosis ) ) {
        sight_max = 0;
    } else if( has_effect( effect_boomered ) && ( !( has_trait( trait_PER_SLIME_OK ) ) ) ) {
        sight_max = 1;
        vision_mode_cache.set( BOOMERED );
    } else if(
        has_effect( effect_in_pit ) || has_effect( effect_no_sight )
        || ( is_underwater() && !has_bionic( bio_membrane ) && !has_trait( trait_MEMBRANE )
             && !worn_with_flag( flag_SWIM_GOGGLES ) && !has_trait( trait_CEPH_EYES )
             && !has_trait( trait_PER_SLIME_OK ) ) ) {
        sight_max = 1;
    } else if( has_active_mutation( trait_SHELL2 ) ) {
        // You can kinda see out a bit.
        sight_max = 2;
    } else if( ( has_trait( trait_MYOPIC ) || has_trait( trait_URSINE_EYE ) )
               && !worn_with_flag( flag_FIX_NEARSIGHT ) && !has_effect( effect_contacts ) ) {
        sight_max = 4;
    } else if( has_trait( trait_PER_SLIME ) ) {
        sight_max = 6;
    } else if( has_effect( effect_darkness ) ) {
        vision_mode_cache.set( DARKNESS );
        sight_max = 10;
    }

    // Debug-only NV
    if( has_trait( trait_DEBUG_NIGHTVISION ) ) { vision_mode_cache.set( DEBUG_NIGHTVISION ); }

    float best_bonus_nv = 0.0f;
    for( const mutation_branch * mut : cached_mutations ) {
        best_bonus_nv = std::max( best_bonus_nv, mut->night_vision_range );
    }
    if( worn_with_flag( flag_RECON_VISION )
        || ( is_mounted() && mounted_creature->has_flag( MF_MECH_RECON_VISION ) ) ) {
        best_bonus_nv = std::max( best_bonus_nv, 10.0f );
    }
    if( worn_with_flag( flag_GNV_EFFECT ) || has_active_bionic( bio_night_vision )
        || has_effect_with_flag( flag_EFFECT_NIGHT_VISION ) ) {
        vision_mode_cache.set( NV_GOGGLES );
        best_bonus_nv = std::max( best_bonus_nv, 10.0f );
    }
    if( worn_with_flag( flag_GNVE_EFFECT ) ) {
        vision_mode_cache.set( ENV_GOGGLES );
        best_bonus_nv = std::max( best_bonus_nv, 18.0f );
    }
    if( has_trait( trait_BIRD_EYE ) ) { vision_mode_cache.set( BIRD_EYE ); }
    if( has_trait( trait_URSINE_EYE ) ) { vision_mode_cache.set( URSINE_VISION ); }

    // +1 because of the ugly -1 in _from_per
    nv_range = 1 + vision::nv_range_from_per( get_per() )
               + vision::nv_range_from_eye_encumbrance( encumb( body_part_eyes ) );
    nv_range += best_bonus_nv;
    if( vision_mode_cache[BIRD_EYE] ) { nv_range++; }

    // Not exactly a sight limit thing, but related enough
    if( has_active_bionic( bio_infrared ) || has_trait( trait_INFRARED ) || has_trait( trait_LIZ_IR )
        || worn_with_flag( flag_IR_EFFECT )
        || ( is_mounted() && mounted_creature->has_flag( MF_MECH_RECON_VISION ) ) ) {
        vision_mode_cache.set( IR_VISION );
    }

    if( has_artifact_with( AEP_SUPER_CLAIRVOYANCE )
        || has_effect_with_flag( flag_EFFECT_SUPER_CLAIRVOYANCE ) ) {
        vision_mode_cache.set( VISION_CLAIRVOYANCE_SUPER );
    } else if( has_artifact_with( AEP_CLAIRVOYANCE_PLUS )
               || has_effect_with_flag( flag_EFFECT_CLAIRVOYANCE_PLUS ) ) {
        vision_mode_cache.set( VISION_CLAIRVOYANCE_PLUS );
    } else if( has_artifact_with( AEP_CLAIRVOYANCE )
               || has_effect_with_flag( flag_EFFECT_CLAIRVOYANCE ) ) {
        vision_mode_cache.set( VISION_CLAIRVOYANCE );
    }
}

float Character::get_vision_threshold( float light_level ) const
{
    if( vision_mode_cache[DEBUG_NIGHTVISION] ) {
        // Debug vision always works with absurdly little light.
        return 0.01;
    }

    // As light_level goes from LIGHT_AMBIENT_MINIMAL to LIGHT_AMBIENT_LIT,
    // dimming goes from 1.0 to 2.0.
    const float dimming_from_light =
        1.0 + ( ( light_level - LIGHT_AMBIENT_MINIMAL ) / ( LIGHT_AMBIENT_LIT - LIGHT_AMBIENT_MINIMAL ) );

    // -1 because SOME math was changed from LIGHT_AMBIENT_MINIMAL to LIGHT_AMBIENT_LOW
    // but kept in other places.
    // *_LOW is the one actually used in math, *_MINIMAL is arbitrary.
    // TODO: Correct test cases and drop the ugliness

    // This guarantees at least 1 tile of range
    static const float threshold_cap =
        vision::threshold_for_nv_range( 1 - 1 ) * LIGHT_AMBIENT_LOW / LIGHT_AMBIENT_MINIMAL;

    return std::min( {
        static_cast<float>( LIGHT_AMBIENT_LOW ),
        vision::threshold_for_nv_range( nv_range - 1 ) * dimming_from_light, threshold_cap} );
}

bool Character::is_blind() const
{
    return ( worn_with_flag( flag_BLIND ) ||
    has_effect( effect_blind ) ||
    has_active_bionic( bio_blindfold ) );
}

bool Character::is_invisible() const
{
    return (
           has_effect_with_flag( flag_EFFECT_INVISIBLE ) ||
    is_wearing_active_optcloak() ||
    has_trait( trait_DEBUG_CLOAK ) ||
    has_artifact_with( AEP_INVISIBLE )
    );
}

int Character::visibility( bool, int ) const
{
    // 0-100 %
    if( is_invisible() ) {
    return 0;
}
// TODO:
// if ( dark_clothing() && light check ...
int stealth_modifier = std::floor( mutation_value( "stealth_modifier" ) );
int const crouching_bonus = 30;
if( g->u.is_crouching() ) {
    stealth_modifier += crouching_bonus;
};
map &here = get_map();
int const camo_modifier = 50;
if( worn_with_flag( flag_NATURE_CAMO )
        && ( here.has_flag( "PLOWABLE", bub_pos() ) || here.has_flag( "SHRUB", bub_pos() ) ) ) {
        stealth_modifier += camo_modifier;
    } else if( worn_with_flag( flag_URBAN_CAMO ) && ( here.has_flag( "ROAD", bub_pos() ) ||
                   here.has_flag( "MINEABLE", bub_pos() ) ) ) {
        stealth_modifier += camo_modifier;
    }
    return clamp( 100 - stealth_modifier, 20, 160 );
}

float Character::active_light() const
{
    float lumination = 0;

    int maxlum = 0;
    has_item_with( [&maxlum]( const item & it ) {
        const int lumit = it.getlight_emit();
        if( maxlum < lumit ) { maxlum = lumit; }
        return false; // continue search, otherwise has_item_with would cancel the search
    } );

    lumination = static_cast<float>( maxlum );

    float mut_lum = 0.0f;
    for( const std::pair<const trait_id, char_trait_data> &mut : my_mutations ) {
        if( mut.second.powered ) {
            float curr_lum = 0.0f;
            for( const auto& elem : mut.first->lumination ) {
                int coverage = 0;
                for( const item * const& i : worn ) {
                    if( i->covers( convert_bp( elem.first ).id() )
                        && !i->has_flag( flag_ALLOWS_NATURAL_ATTACKS )
                        && !i->has_flag( flag_SEMITANGIBLE ) && !i->has_flag( flag_PERSONAL )
                        && !i->has_flag( flag_AURA ) ) {
                        coverage += i->get_coverage( convert_bp( elem.first ) );
                    }
                }
                curr_lum += elem.second * ( 1 - ( coverage / 100.0f ) );
            }
            mut_lum += curr_lum;
        }
    }

    lumination = std::max( lumination, mut_lum );

    if( lumination < 300 && has_active_bionic( bio_flashlight ) ) {
        lumination = 300;
    } else if( lumination < 25 && has_artifact_with( AEP_GLOW ) ) {
        lumination = 25;
    } else if( lumination < 5
               && ( has_effect( effect_glowing ) || has_effect( effect_glowy_led )
                    || has_active_bionic( bio_tattoo_led ) ) ) {
        lumination = 5;
    }
    return lumination;
}

bool Character::sees_with_specials( const Creature& critter ) const
{
    // Prevent seeing through floors across z-levels
    if( bub_pos().z() != critter.bub_pos().z() ) { return false; }

    // electroreceptors grants vision of robots and electric monsters through walls
    if( ( has_trait( trait_ELECTRORECEPTORS ) || has_active_bionic( bio_electrosense ) )
        && ( critter.in_species( ROBOT ) || critter.has_flag( MF_ELECTRIC ) ) ) {
        return true;
    }

    if( critter.digging() && has_active_bionic( bio_ground_sonar ) ) {
        // Bypass the check below, the bionic sonar also bypasses the sees(point) check because
        // walls don't block sonar which is transmitted in the ground, not the air.
        // TODO: this might need checks whether the player is in the air, or otherwise not connected
        // to the ground. It also might need a range check.
        return true;
    }

    const int dist = rl_dist( bub_pos(), critter.bub_pos() );
    return (
               dist <= 5
               && ( has_active_mutation( trait_ANTENNAE )
                    || ( has_active_bionic( bio_ground_sonar ) && !critter.has_flag( MF_FLIES ) ) ) );
}

bool Character::sees_with_infrared( const Creature& critter ) const
{
    if( !vision_mode_cache[IR_VISION] || !critter.is_warm() ) { return false; }

    map &here = get_map();
    if( is_player() || critter.is_player() ) {
    // Players should not use map::sees
    // Likewise, players should not be "looked at" with map::sees, not to break symmetry
    return here.pl_line_of_sight( critter.bub_pos(),
                                  sight_range( current_daylight_level( calendar::turn ) ) );
    }

    return here
           .sees( bub_pos(), critter.bub_pos(), sight_range( current_daylight_level( calendar::turn ) ) );
}

bool Character::is_visible_in_range( const Creature& critter, const int range ) const
{
    return sees( critter ) && rl_dist( bub_pos(), critter.bub_pos() ) <= range;
}

std::vector<Creature *> Character::get_visible_creatures( const int range ) const
{
    return g->get_creatures_if( [this, range]( const Creature & critter ) -> bool {
        return this != &critter && bub_pos() != critter.bub_pos() && // TODO: get rid of fake npcs (pos() check)
                rl_dist( bub_pos(), critter.bub_pos() ) <= range && sees( critter );
    } );
}

std::vector<Creature *> Character::get_hostile_creatures( int range ) const
{
    return g->get_creatures_if( [this, range]( const Creature & critter ) -> bool {
        // Fixes circular distance range for ranged attacks
        float dist_to_creature = std::round( rl_dist_exact( bub_pos().raw(), critter.bub_pos().raw() ) );
        return this != &critter && bub_pos() != critter.bub_pos() && // TODO: get rid of fake npcs (pos() check)
                dist_to_creature <= range && critter.attitude_to( *this ) == Attitude::A_HOSTILE
                && sees( critter );
    } );
}

bool Character::knows_trap( const tripoint_bub_ms& pos ) const
{
    const auto p = get_map().bub_to_abs( pos );
    return known_traps.contains( p );
}

void Character::add_known_trap( const tripoint_bub_ms& pos, const trap& t )
{
    const auto p = get_map().bub_to_abs( pos );
    if( t.is_null() ) {
        known_traps.erase( p );
    } else {
        // TODO: known_traps should map to a trap_str_id
        known_traps[p] = t.id.str();
    }
}

bool Character::avoid_trap( const tripoint_bub_ms& pos, const trap& tr ) const
{
    /** @EFFECT_DEX increases chance to avoid traps */

    /** @EFFECT_DODGE increases chance to avoid traps */
    int myroll = dice( 3, dex_cur + get_skill_level( skill_dodge ) * 1.5 );
    int traproll;
    if( tr.can_see( pos, *this ) ) {
        traproll = dice( 3, tr.get_avoidance() );
    } else {
        traproll = dice( 6, tr.get_avoidance() );
    }

    return myroll >= traproll;
}

bool Character::can_hear( const tripoint_bub_ms &source, const int volume ) const
{
    if( is_deaf() ) {
    return false;
}

// source is in-ear and at our square, we can hear it
if( source == bub_pos() && volume == 0 ) {
    return true;
}
const int dist = rl_dist( source, bub_pos() );
const float volume_multiplier = hearing_ability();
return ( volume - get_weather().weather_id->sound_attn ) * volume_multiplier >= dist;
}

float Character::hearing_ability() const
{
    float volume_multiplier = 1.0;

    // Mutation/Bionic volume modifiers
    if( has_active_bionic( bio_ears ) && !has_active_bionic( bio_earplugs ) ) {
        volume_multiplier *= 3.5;
    }
    if( has_trait( trait_PER_SLIME ) ) {
        // Random hearing :-/
        // (when it's working at all, see player.cpp)
        // changed from 0.5 to fix Mac compiling error
        volume_multiplier *= ( rng( 1, 2 ) );
    }

    volume_multiplier *= Character::mutation_value( "hearing_modifier" );

    if( has_effect( effect_deaf ) ) {
        // Scale linearly up to 30 minutes
        volume_multiplier *= ( 30_minutes - get_effect_dur( effect_deaf ) ) / 30_minutes;
    }

    if( has_effect( effect_earphones ) ) { volume_multiplier *= .25; }

    return volume_multiplier;
}

bool Character::sees( const tripoint_bub_ms& t, bool, int ) const
{
    const int wanted_range = rl_dist( bub_pos(), t );
    bool can_see = is_player() ? get_map().pl_sees( t, wanted_range ) : Creature::sees( t );
    // Clairvoyance is now pretty cheap, so we can check it early
    if( wanted_range < MAX_CLAIRVOYANCE && wanted_range < clairvoyance() ) { return true; }

    if( can_see && wanted_range > unimpaired_range() ) { can_see = false; }

    return can_see;
}

bool Character::sees( const Creature& critter ) const
{
    // This handles only the player/npc specific stuff (monsters don't have traits or bionics).
    const int dist = rl_dist( bub_pos(), critter.bub_pos() );
    if( bub_pos().z() == critter.bub_pos().z() && dist <= 5
        && ( has_active_mutation( trait_ANTENNAE )
             || ( has_active_bionic( bio_ground_sonar ) && !critter.has_flag( MF_FLIES ) ) ) ) {
        return true;
    }

    return Creature::sees( critter );
}

