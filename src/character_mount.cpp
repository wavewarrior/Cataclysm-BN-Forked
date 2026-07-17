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



// *INDENT-OFF*

void Character::mount_creature( monster& z )
{
    const auto pnt = z.bub_pos();
    shared_ptr_fast<monster> mons = g->shared_from( z );
    if( mons == nullptr ) {
        add_msg( m_debug, "mount_creature(): monster not found in critter_tracker" );
        return;
    }
    add_effect( effect_riding, 1_turns, bodypart_str_id::NULL_ID() );
    z.add_effect( effect_ridden, 1_turns, bodypart_str_id::NULL_ID() );
    if( z.has_effect( effect_tied ) ) {
        z.remove_effect( effect_tied );
        if( z.get_tied_item() ) { i_add( z.set_tied_item( detached_ptr<item>() ) ); }
    }
    z.mounted_player_id = getID();
    if( z.has_effect( effect_harnessed ) ) {
        z.remove_effect( effect_harnessed );
        add_msg_if_player( m_info, _( "You remove the %s's harness." ), z.get_name() );
    }
    mounted_creature = mons;
    mons->mounted_player = this;
    if( is_avatar() ) {
        if( g->u.is_hauling() ) { g->u.stop_hauling(); }
        if( g->u.get_grab_type() != OBJECT_NONE ) {
            add_msg( m_warning, _( "You let go of the grabbed object." ) );
            g->u.grab( OBJECT_NONE );
        }
        g->place_player( pnt );
    } else {
        npc& guy = dynamic_cast<npc &>( *this );
        guy.setpos( pnt );
    }
    z.facing = facing;
    // Make sure something didn't interrupt this process and knock the player off partway through!
    if( has_effect( effect_riding ) ) {
        add_msg_if_player( m_good, _( "You climb on the %s." ), z.get_name() );
        if( z.has_flag( MF_RIDEABLE_MECH ) ) {
            if( !z.type->mech_weapon.is_empty() ) { wield( item::spawn( z.type->mech_weapon ) ); }
            add_msg_if_player( m_good, _( "You hear your %s whir to life." ), z.get_name() );
        }
    }
    // Unfreeze recently-dismounted horses
    if( z.has_effect( effect_ai_waiting ) ) { z.remove_effect( effect_ai_waiting ); }
    // some rideable mechs have night-vision
    recalc_sight_limits();
    mod_moves( -100 );
}

bool Character::check_mount_will_move( const tripoint_bub_ms& dest_loc )
{
    if( !is_mounted() ) { return true; }
    if( mounted_creature->has_flag( MF_COMBAT_MOUNT ) ) { return true; }
    if( mounted_creature && mounted_creature->type->has_fear_trigger( mon_trigger::HOSTILE_CLOSE ) ) {
        for( const monster& critter : g->all_monsters() ) {
            Attitude att = critter.attitude_to( *this );
            if( att == Attitude::A_HOSTILE && sees( critter )
                && rl_dist( bub_pos(), critter.bub_pos() ) <= 15
                && rl_dist( dest_loc, critter.bub_pos() ) < rl_dist( bub_pos(), critter.bub_pos() ) ) {
                add_msg_if_player( _( "You fail to budge your %s!" ), mounted_creature->get_name() );
                return false;
            }
        }
    }
    return true;
}

bool Character::check_mount_is_spooked()
{
    if( !is_mounted() ) { return false; }
    // chance to spook per monster nearby:
    // base 1% per turn.
    // + 1% per square closer than 15 distanace. (1% - 15%)
    // * 2 if hostile monster is bigger than or same size as mounted creature.
    // / 2 if horse has full tack and saddle.
    // / 2 With Animal Empath
    // / 4 With Animal Kinship
    // -0.25% per point of dexterity (low -1%, average -2%, high -3%, extreme -3.5%)
    // -0.1% per point of strength ( low -0.4%, average -0.8%, high -1.2%, extreme -1.4% )
    // -0.075 per point of survival & driving
    // Monster in spear reach monster and average stat (8) player on saddled horse, 14% -2% -0.8% /
    // 2 = ~5%
    if( mounted_creature && mounted_creature->type->has_fear_trigger( mon_trigger::HOSTILE_CLOSE ) ) {
        if( mounted_creature->has_flag( MF_COMBAT_MOUNT ) ) { return false; }
        const creature_size mount_size = mounted_creature->get_size();
        for( const monster& critter : g->all_monsters() ) {
            double chance = 1.0;
            Attitude att = critter.attitude_to( *this );
            // actually too close now - horse might spook.
            if( att == Attitude::A_HOSTILE && sees( critter )
                && rl_dist( bub_pos(), critter.bub_pos() ) <= 10 ) {
                chance += 10 - rl_dist( bub_pos(), critter.bub_pos() );
                if( critter.get_size() >= mount_size ) { chance *= 2; }
                if( mounted_creature->has_effect( effect_saddled ) ) { chance /= 2; }
                if( has_trait( trait_id( "ANIMALEMPATH" ) ) ) { chance /= 2; }
                if( has_trait( trait_id( "ANIMALEMPATH2" ) ) ) { chance /= 4; }
                chance -= 0.25 * get_dex();
                chance -= 0.1 * get_str();
                chance -= 0.075 * get_skill_level( skill_survival );
                chance -= 0.075 * get_skill_level( skill_driving );
                chance = std::max( 0.0, chance );
                if( x_in_y( chance, 100.0 ) ) {
                    forced_dismount();
                    return true;
                }
            }
        }
    }
    return false;
}

bool Character::is_mounted() const { return has_effect( effect_riding ) && mounted_creature; }

void Character::forced_dismount()
{
    remove_effect( effect_riding );
    bool mech = false;
    if( mounted_creature ) {
        auto mon = mounted_creature.get();
        if( mon->has_flag( MF_RIDEABLE_MECH ) && !mon->type->mech_weapon.is_empty() ) {
            mech = true;
            remove_item( primary_weapon() );
        }
        mon->mounted_player_id = character_id();
        mon->remove_effect( effect_ridden );
        mon->add_effect( effect_ai_waiting, 5_turns );
        mounted_creature = nullptr;
        mon->mounted_player = nullptr;
    }
    std::vector<tripoint_bub_ms> valid;
    for( const tripoint_bub_ms& jk : get_map().points_in_radius( bub_pos(), 1 ) ) {
        if( g->is_empty( jk ) ) { valid.push_back( jk ); }
    }
    if( !valid.empty() ) {
        setpos( random_entry( valid ) );
        if( mech ) {
            add_msg_player_or_npc(
                m_bad, _( "You are ejected from your mech!" ),
                _( "<npcname> is ejected from their mech!" ) );
        } else {
            add_msg_player_or_npc(
                m_bad, _( "You fall off your mount!" ), _( "<npcname> falls off their mount!" ) );
        }
        const int dodge = get_dodge();
        const int damage = std::max( 0, rng( 1, 20 ) - rng( dodge, dodge * 2 ) );
        bodypart_id hit = bodypart_str_id::NULL_ID().id();
        switch( rng( 1, 10 ) ) {
            case 1:
                if( one_in( 2 ) ) {
                    hit = bodypart_id( "foot_l" );
                } else {
                    hit = bodypart_id( "foot_r" );
                }
                break;
            case 2:
            case 3:
            case 4:
                if( one_in( 2 ) ) {
                    hit = bodypart_id( "leg_l" );
                } else {
                    hit = bodypart_id( "leg_r" );
                }
                break;
            case 5:
            case 6:
            case 7:
                if( one_in( 2 ) ) {
                    hit = bodypart_id( "arm_l" );
                } else {
                    hit = bodypart_id( "arm_r" );
                }
                break;
            case 8:
            case 9:
                hit = bodypart_id( "torso" );
                break;
            case 10:
                hit = bodypart_id( "head" );
                break;
        }
        if( damage > 0 ) {
            add_msg_if_player( m_bad, _( "You hurt yourself!" ) );
            deal_damage( nullptr, hit, damage_instance( DT_BASH, damage ) );
            if( is_avatar() ) {
                g->memorial()
                .add( pgettext( "memorial_male", "Fell off a mount." ),
                      pgettext( "memorial_female", "Fell off a mount." ) );
            }
            check_dead_state();
        }
        add_effect( effect_downed, 5_turns, bodypart_str_id::NULL_ID() );
    } else {
        add_msg( m_debug, "Forced_dismount could not find a square to deposit player" );
    }
    if( is_avatar() ) {
        if( g->u.get_grab_type() != OBJECT_NONE ) {
            add_msg( m_warning, _( "You let go of the grabbed object." ) );
            g->u.grab( OBJECT_NONE );
        }
        set_movement_mode( CMM_WALK );
        if( g->u.is_auto_moving() || g->u.has_destination() || g->u.has_destination_activity() ) {
            g->u.clear_destination();
        }
        g->update_map( g->u );
    }
    if( activity ) { cancel_activity(); }
    moves -= 150;
}

void Character::dismount()
{
    if( !is_mounted() ) {
        add_msg( m_debug, "dismount called when not riding" );
        return;
    }
    if( const std::optional<tripoint_bub_ms> pnt = choose_adjacent( _( "Dismount where?" ) ) ) {
        if( !g->is_empty( *pnt ) ) {
            add_msg( m_warning, _( "You cannot dismount there!" ) );
            return;
        }
        remove_effect( effect_riding );
        monster* critter = mounted_creature.get();
        critter->mounted_player_id = character_id();
        item& weapon = primary_weapon();
        if( critter->has_flag( MF_RIDEABLE_MECH ) && !critter->type->mech_weapon.is_empty()
            && weapon.typeId() == critter->type->mech_weapon ) {
            remove_item( weapon );
        }
        if( is_avatar() && g->u.get_grab_type() != OBJECT_NONE ) {
            add_msg( m_warning, _( "You let go of the grabbed object." ) );
            g->u.grab( OBJECT_NONE );
        }
        critter->remove_effect( effect_ridden );
        critter->add_effect( effect_ai_waiting, 5_turns );
        mounted_creature = nullptr;
        critter->mounted_player = nullptr;
        setpos( *pnt );
        mod_moves( -100 );
        set_movement_mode( CMM_WALK );
    }
}
