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


namespace
{
struct healable_bp {
    mutable bool allowed;
    bodypart_id bp;
    std::string name;
    int bonus;
};

auto get_best_selection_index(
    const Character& c, const std::vector<healable_bp> &parts, float bandage_power,
    float disinfectant_power ) -> int
{
    int best_selection_index = -1;
    int max_priority = -1;
    const bool is_disinfectant = disinfectant_power > 0.0f;
    const bool is_bandage = bandage_power > 0.0f;
    for( size_t i = 0; i < parts.size(); ++i ) {
        if( !parts[i].allowed ) { continue; }
        const bodypart_id& bp = parts[i].bp;
        const bodypart_str_id& bp_str_id = bp.id();
        int current_priority = 0;
        const int cur_hp = c.get_part_hp_cur( bp );
        const int max_hp = c.get_part_hp_max( bp );
        const int cur_dmg = max_hp - cur_hp;
        if( is_bandage ) {
            if( c.has_effect( effect_bleed, bp_str_id ) ) {
                current_priority = 2000;
            } else if( !c.has_effect( effect_bandaged, bp_str_id ) ) {
                current_priority = 500 + cur_dmg;
            } else {
                const int b_power = c.get_effect_int( effect_bandaged, bp_str_id );
                int new_b_power = static_cast<int>( std::floor( bandage_power ) );
                if( new_b_power > b_power ) { current_priority = 100 + ( new_b_power - b_power ); }
            }
        }
        if( is_disinfectant && !c.has_effect( effect_bleed, bp_str_id ) ) {
            if( c.has_effect( effect_bite, bp_str_id ) ) {
                if( current_priority < 1000 ) { current_priority = 1000; }
            } else if( !c.has_effect( effect_disinfected, bp_str_id ) ) {
                if( current_priority < 500 + cur_dmg ) { current_priority = 500 + cur_dmg; }
            } else {
                const int d_power = c.get_effect_int( effect_disinfected, bp_str_id );
                int new_d_power = static_cast<int>( std::floor( disinfectant_power ) );
                if( new_d_power > d_power ) {
                    int potential_priority = 100 + ( new_d_power - d_power );
                    if( current_priority < potential_priority ) { current_priority = potential_priority; }
                }
            }
        }
        if( current_priority > max_priority ) {
            max_priority = current_priority;
            best_selection_index = i;
        }
    }
    if( best_selection_index == -1 ) { return 0; }
    return best_selection_index;
}
} // namespace

bodypart_str_id Character::body_window(
    const std::string& menu_header, bool show_all, bool precise, int normal_bonus, int head_bonus,
    int torso_bonus, float bleed, float bite, float infect, float bandage_power,
    float disinfectant_power ) const
{
    std::vector<healable_bp> parts;
    for( const bodypart_id& bp : get_all_body_parts( true ) ) {
        // Ugly!
        int heal_bonus =
            bp == body_part_head ? head_bonus
            : bp == body_part_torso
            ? torso_bonus
            : normal_bonus;
        parts.emplace_back( false, bp, bp->name_as_heading.translated(), heal_bonus );
    }

    int max_bp_name_len = 0;
    for( const auto& e : parts ) { max_bp_name_len = std::max( max_bp_name_len, utf8_width( e.name ) ); }

    uilist bmenu;
    bmenu.desc_enabled = true;
    bmenu.text = menu_header;

    bmenu.hilight_disabled = true;
    bool is_valid_choice = false;

    for( size_t i = 0; i < parts.size(); i++ ) {
        const auto& e = parts[i];
        const bodypart_id& bp = e.bp;
        const bodypart_str_id& bp_str_id = bp.id();
        const int maximal_hp = get_part_hp_max( bp );
        const int current_hp = get_part_hp_cur( bp );
        // This will c_light_gray if the part does not have any effects cured by the item/effect
        // (e.g. it cures only bites, but the part does not have a bite effect)
        const nc_color state_col = limb_color( bp.id(), bleed > 0.0f, bite > 0.0f, infect > 0.0f );
        const bool has_curable_effect = state_col != c_light_gray;
        // The same as in the main UI sidebar. Independent of the capability of the healing
        // item/effect!
        const nc_color all_state_col = limb_color( bp.id(), true, true, true );
        // Broken means no HP can be restored, it requires surgical attention.
        const bool limb_is_broken = is_limb_broken( bp );

        if( show_all ) {
            e.allowed = true;
        } else if( has_curable_effect ) {
            e.allowed = true;
        } else if( current_hp < maximal_hp
                   && ( e.bonus != 0 || bandage_power > 0.0f || disinfectant_power > 0.0f ) ) {
            e.allowed = true;
        } else {
            e.allowed = false;
        }

        std::string msg;
        std::string desc;
        bool bleeding = has_effect( effect_bleed, bp_str_id );
        bool bitten = has_effect( effect_bite, bp_str_id );
        bool infected = has_effect( effect_infected, bp_str_id );
        bool bandaged = has_effect( effect_bandaged, bp_str_id );
        bool disinfected = has_effect( effect_disinfected, bp_str_id );
        const int b_power = get_effect_int( effect_bandaged, bp_str_id );
        const int d_power = get_effect_int( effect_disinfected, bp_str_id );
        int new_b_power = static_cast<int>( std::floor( bandage_power ) );
        if( bandaged ) {
            const effect& eff = get_effect( effect_bandaged, bp_str_id );
            if( new_b_power > eff.get_max_intensity() ) { new_b_power = eff.get_max_intensity(); }
        }
        int new_d_power = static_cast<int>( std::floor( disinfectant_power ) );

        const auto& aligned_name = std::string( max_bp_name_len - utf8_width( e.name ), ' ' ) + e.name;
        std::string hp_str;
        if( limb_is_broken ) {
            const nc_color color =
                worn_with_flag( flag_SPLINT, bp ) || ( mutation_value( "mending_modifier" ) >= 1.0f )
                ? c_blue
                : c_light_red;
            desc +=
                colorize( _( "It is broken and must heal fully before it becomes functional again." ),
                          c_blue )
                + "\n";
            const int mend_perc = 100 * current_hp / maximal_hp;

            if( precise ) {
                hp_str = colorize( string_format( "=%2d%%=", mend_perc ), color );
            } else {
                const int num = mend_perc / 20;
                hp_str = colorize( std::string( num, '#' ) + std::string( 5 - num, '=' ), color );
            }
        } else if( precise ) {
            hp_str = string_format( "%d", current_hp );
        } else {
            std::pair<std::string, nc_color> h_bar = get_hp_bar( current_hp, maximal_hp, false );
            hp_str = colorize( h_bar.first, h_bar.second )
                     + colorize( std::string( 5 - utf8_width( h_bar.first ), '.' ), c_white );
        }
        msg += colorize( aligned_name, all_state_col ) + " " + hp_str;

        // BLEEDING block
        if( bleeding ) {
            desc +=
                colorize( string_format( "%s: %s", get_effect( effect_bleed, bp.id() ).get_speed_name(),
                                         get_effect( effect_bleed, bp.id() ).disp_short_desc() ),
                          c_red )
                + "\n";
            if( bleed > 0.0f ) {
                desc +=
                    colorize(
                        string_format( _( "Chance to stop: %d %%" ), static_cast<int>( bleed * 100 ) ),
                        c_light_green )
                    + "\n";
            } else {
                desc += colorize( _( "This will not stop the bleeding." ), c_yellow ) + "\n";
            }
        }
        // BANDAGE block
        if( bandaged ) {
            desc += string_format( _( "Bandaged [%s]" ), texitify_healing_power( b_power ) ) + "\n";
            if( new_b_power > b_power ) {
                desc +=
                    colorize( string_format( _( "Expected quality improvement: %s" ),
                                             texitify_healing_power( new_b_power ) ),
                              c_light_green )
                    + "\n";
            } else if( new_b_power > 0 ) {
                desc += colorize( _( "You don't expect any improvement from using this." ), c_yellow )
                        + "\n";
            }
        } else if( new_b_power > 0 && e.allowed ) {
            desc +=
                colorize( string_format( _( "Expected bandage quality: %s" ),
                                         texitify_healing_power( new_b_power ) ),
                          c_light_green )
                + "\n";
        }
        // BITTEN block
        if( bitten ) {
            desc += colorize(
                        string_format( "%s: ", get_effect( effect_bite, bp.id() ).get_speed_name() ), c_red );
            desc += colorize( _( "It has a deep bite wound that needs cleaning." ), c_red ) + "\n";
            if( bite > 0 ) {
                desc +=
                    colorize( string_format( _( "Chance to clean and disinfect: %d %%" ),
                                             static_cast<int>( bite * 100 ) ),
                              c_light_green )
                    + "\n";
            } else {
                desc += colorize( _( "This will not help in cleaning this wound." ), c_yellow ) + "\n";
            }
        }
        // INFECTED block
        if( infected ) {
            desc += colorize(
                        string_format( "%s: ", get_effect( effect_infected, bp.id() ).get_speed_name() ),
                        c_red );
            desc +=
                colorize( _( "It has a deep wound that looks infected.  Antibiotics might be "
                         "required." ),
                          c_red )
                + "\n";
            if( infect > 0 ) {
                desc +=
                    colorize( string_format( _( "Chance to heal infection: %d %%" ),
                                             static_cast<int>( infect * 100 ) ),
                              c_light_green )
                    + "\n";
            } else {
                desc += colorize( _( "This will not help in healing infection." ), c_yellow ) + "\n";
            }
        }
        // DISINFECTANT (general) block
        if( disinfected ) {
            desc += string_format( _( "Disinfected [%s]" ), texitify_healing_power( d_power ) ) + "\n";
            if( new_d_power > d_power ) {
                desc +=
                    colorize( string_format( _( "Expected quality improvement: %s" ),
                                             texitify_healing_power( new_d_power ) ),
                              c_light_green )
                    + "\n";
            } else if( new_d_power > 0 ) {
                desc += colorize( _( "You don't expect any improvement from using this." ), c_yellow )
                        + "\n";
            }
        } else if( new_d_power > 0 && e.allowed ) {
            desc +=
                colorize( string_format( _( "Expected disinfection quality: %s" ),
                                         texitify_healing_power( new_d_power ) ),
                          c_light_green )
                + "\n";
        }
        // END of blocks

        if( ( !e.allowed && !limb_is_broken )
            || ( show_all && current_hp == maximal_hp && !limb_is_broken && !bitten && !infected
                 && !bleeding ) ) {
            desc += colorize( _( "Healthy." ), c_green ) + "\n";
        }
        if( !e.allowed ) {
            desc += colorize( _( "You don't expect any effect from using this." ), c_yellow );
        } else {
            is_valid_choice = true;
        }
        bmenu.addentry_desc( i, e.allowed, MENU_AUTOASSIGN, msg, desc );
    }

    if( !is_valid_choice ) { // no body part can be chosen for this item/effect
        bmenu.init();
        bmenu.desc_enabled = false;
        bmenu.text = _( "No limb would benefit from it." );
        bmenu.addentry( parts.size(), true, 'q', "%s", _( "Cancel" ) );
    } else {
        const int preferred_index =
            get_best_selection_index( *this, parts, bandage_power, disinfectant_power );
        bmenu.selected = preferred_index;
    }

    bmenu.query();
    if( bmenu.ret >= 0 && static_cast<size_t>( bmenu.ret ) < parts.size()
        && parts[bmenu.ret].allowed ) {
        return parts[bmenu.ret].bp.id();
    } else {
        return bodypart_str_id::NULL_ID();
    }
}

nc_color Character::limb_color( const bodypart_str_id &bp, bool bleed, bool bite,
                                bool infect ) const
{
    if( !bp ) {
    return c_light_gray;
}
int color_bit = 0;
nc_color i_color = c_light_gray;
if( bleed && has_effect( effect_bleed, bp ) ) {
        color_bit += 1;
    }
    if( bite && has_effect( effect_bite, bp ) ) {
        color_bit += 10;
    }
    if( infect && has_effect( effect_infected, bp ) ) {
        color_bit += 100;
    }
    switch( color_bit ) {
    case 1:
        i_color = c_red;
        break;
    case 10:
        i_color = c_blue;
        break;
    case 100:
        i_color = c_green;
        break;
    case 11:
        i_color = c_magenta;
        break;
    case 101:
        i_color = c_yellow;
        break;
}

return i_color;
}

std::string Character::get_name() const { return name; }

std::vector<std::string> Character::get_grammatical_genders() const
{
    if( male ) {
    return { "m" };
} else {
    return { "f" };
}
}

nc_color Character::basic_symbol_color() const
{
    if( has_effect( effect_onfire ) ) {
    return c_red;
}
if( has_effect( effect_stunned ) ) {
    return c_light_blue;
}
if( has_effect( effect_boomered ) ) {
    return c_pink;
}
if( has_active_mutation( trait_id( "SHELL2" ) ) ) {
        return c_magenta;
    }
    if( is_underwater() ) {
    return c_blue;
}
if( has_active_bionic( bio_cloak ) || has_artifact_with( AEP_INVISIBLE ) ||
        is_wearing_active_optcloak() || has_trait( trait_DEBUG_CLOAK ) ) {
    return c_dark_gray;
}
if( move_mode == CMM_RUN ) {
    return c_yellow;
}
if( is_crouching() ) {
    return c_light_gray;
}
return c_white;
}

nc_color Character::symbol_color() const
{
    nc_color basic = basic_symbol_color();

    if( has_effect( effect_downed ) ) {
        return hilite( basic );
    } else if( has_effect( effect_grabbed ) ) {
        return cyan_background( basic );
    }

    const auto& fields = get_map().field_at( bub_pos() );

    // Priority: electricity, fire, acid, gases
    bool has_elec = false;
    bool has_fire = false;
    bool has_acid = false;
    bool has_fume = false;
    for( const auto& field : fields ) {
        has_elec = field.first.obj().has_elec;
        if( has_elec ) { return hilite( basic ); }
        has_fire = field.first.obj().has_fire;
        has_acid = field.first.obj().has_acid;
        has_fume = field.first.obj().has_fume;
    }
    if( has_fire ) { return red_background( basic ); }
    if( has_acid ) { return green_background( basic ); }
    if( has_fume ) { return white_background( basic ); }
    if( in_sleep_state() ) { return hilite( basic ); }
    return basic;
}

std::string Character::extended_description() const
{
    std::string ss;
    if( is_player() ) {
        // <bad>This is me, <player_name>.</bad>
        ss += string_format( _( "This is you - %s." ), name );
    } else {
        ss += string_format( _( "This is %s, %s" ), name, male ? _( "Male" ) : _( "Female" ) );
    }

    ss += "\n--\n";

    const std::vector<bodypart_id> &bps = get_all_body_parts( true );
    // Find length of bp names, to align
    // accumulate looks weird here, any better function?
    int longest = std::accumulate( bps.begin(), bps.end(), 0, []( int m, bodypart_id bp ) {
        return std::max( m, utf8_width( body_part_name_as_heading( bp->token, 1 ) ) );
    } );

    // This is a stripped-down version of the body_window function
    // This should be extracted into a separate function later on
    for( const bodypart_id& bp : bps ) {
        const std::string& bp_heading = body_part_name_as_heading( bp->token, 1 );

        const nc_color state_col = limb_color( bp.id(), true, true, true );
        nc_color name_color = state_col;
        std::pair<std::string, nc_color> hp_bar = get_hp_bar( get_part_hp_cur( bp ), get_part_hp_max( bp ),
            false );

        ss += colorize( left_justify( bp_heading, longest ), name_color );
        ss += colorize( hp_bar.first, hp_bar.second );
        // Trailing bars. UGLY!
        // TODO: Integrate into get_hp_bar somehow
        ss += colorize( std::string( 5 - utf8_width( hp_bar.first ), '.' ), c_white );
        ss += "\n";
    }

    ss += "--\n";

    std::vector<std::string> apperance_desc = get_apperance_description();
    if( !apperance_desc.empty() ) {
        ss += ( _( "Apperance: " ) + enumerate_as_string( apperance_desc ) );
        ss += "\n";
    }

    ss += _( "Wielding:" ) + std::string( " " );
    if( primary_weapon().is_null() ) {
        ss += _( "Nothing" );
    } else {
        ss += primary_weapon().tname();
    }

    ss += "\n";
    ss += _( "Wearing:" ) + std::string( " " );
    ss += enumerate_as_string( worn.begin(), worn.end(), []( const item * const & it ) {
        return it->tname();
    } );

    return replace_colors( ss );
}

std::vector<std::string> Character::get_apperance_description() const
{
    std::map<std::string, trait_id> apperance_muts;
    std::vector<std::string> valid_apperance_categories =
    {"hair_style", "hair_color", "eye_color", "skin_tone"};

    for( const trait_id& mutation : get_mutations() ) {
        for( std::string cat : valid_apperance_categories ) {
            auto mut_obj = mutation.obj();
            if( mut_obj.types.contains( cat ) ) { apperance_muts[cat] = mutation; }
        }
    }

    std::vector<std::string> apperance_desc;

    if( apperance_muts.count( "hair_style" ) && apperance_muts.count( "hair_color" ) ) {
        apperance_desc.push_back(
            apperance_muts["hair_color"].obj().apperance_desc() + " "
            + apperance_muts["hair_style"].obj().apperance_desc() + _( " hair" ) );
    }

    if( apperance_muts.count( "eye_color" ) ) {
        apperance_desc.push_back( apperance_muts["eye_color"].obj().apperance_desc() + _( " eyes" ) );
    }

    if( apperance_muts.count( "skin_tone" ) ) {
        apperance_desc.push_back( apperance_muts["skin_tone"].obj().apperance_desc() + _( " skin" ) );
    }

    return apperance_desc;
}

std::vector<std::string> Character::short_description_parts() const
{
    std::vector<std::string> result;

    std::string gender = male ? _( "Male" ) : _( "Female" );
    result.push_back( name + ", " + gender );
    if( is_armed() ) { result.push_back( _( "Wielding: " ) + primary_weapon().tname() ); }
    const std::string worn_str =
    enumerate_as_string( worn.begin(), worn.end(), []( const item * const & it ) {
        return it->tname();
    } );
    if( !worn_str.empty() ) { result.push_back( _( "Wearing: " ) + worn_str ); }
    const int visibility_cap = 0; // no cap
    const auto trait_str = visible_mutations( visibility_cap );
    if( !trait_str.empty() ) { result.push_back( _( "Traits: " ) + trait_str ); }
    return result;
}

std::string Character::short_description() const { return join( short_description_parts(), ";   " ); }

int Character::hp_percentage() const
{
    const bodypart_id head_id = bodypart_id( "head" );
    const bodypart_id torso_id = bodypart_id( "torso" );
    int total_cur = 0;
    int total_max = 0;
    // Head and torso HP are weighted 3x and 2x, respectively
    total_cur = get_part_hp_cur( head_id ) * 3 + get_part_hp_cur( torso_id ) * 2;
    total_max = get_part_hp_max( head_id ) * 3 + get_part_hp_max( torso_id ) * 2;
    for( const std::pair<const bodypart_str_id, bodypart> &elem : get_body() ) {
        total_cur += elem.second.get_hp_cur();
        total_max += elem.second.get_hp_max();
    }

    return ( 100 * total_cur ) / total_max;
}

