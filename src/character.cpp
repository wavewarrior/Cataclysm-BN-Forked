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

namespace io
{

template <> std::string enum_to_string<character_movemode>( character_movemode data )
{
    switch( data ) {
            // *INDENT-OFF*
        case character_movemode::CMM_WALK:
            return "walk";
        case character_movemode::CMM_RUN:
            return "run";
        case character_movemode::CMM_CROUCH:
            return "crouch";
        case character_movemode::CMM_STEALTH:
            return "stealth";
            // *INDENT-ON*
        case character_movemode::CMM_COUNT:
            break;
    }
    debugmsg( "Invalid character_movemode" );
    abort();
}

} // namespace io

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

Character &get_player_character() { return g->u; }

// *INDENT-OFF*
Character::Character()
    : location_visitable<Character>(),
      worn(new worn_item_location(this)),
      cached_time(calendar::before_time_starts),
      inv(new character_item_location(this)),
      id(-1),
      next_climate_control_check(calendar::before_time_starts),
      last_climate_control_ret(false) {
    if (g != nullptr) { position = get_map().bub_to_abs(tripoint_bub_ms::zero()); }

    str_max = 0;
    dex_max = 0;
    per_max = 0;
    int_max = 0;
    str_cur = 0;
    dex_cur = 0;
    per_cur = 0;
    int_cur = 0;
    str_bonus = 0;
    dex_bonus = 0;
    per_bonus = 0;
    int_bonus = 0;
    healthy = 0;
    healthy_mod = 0;
    thirst = 0;
    fatigue = 0;
    sleep_deprivation = 0;
    set_rad(0);
    tank_plut = 0;
    reactor_plut = 0;
    slow_rad = 0;
    set_stim(0);
    set_stamina(10000); // Temporary value for stamina. It will be reset later from external json
                        // option.
    set_anatomy(anatomy_id("human_anatomy"));
    set_body();
    update_type_of_scent(true);
    pkill = 0;
    stored_calories = max_stored_kcal() - 100;
    initialize_stomach_contents();

    name.clear();
    custom_profession.clear();
    prof = profession::generic();

    *path_settings = pathfinding_settings{0, 1000, 1000, 0, true, true, true, false, true};

    move_mode = CMM_WALK;
    next_expected_position = std::nullopt;
    for (auto& pr : get_body()) {
        pr.second.set_temp_cur(BODYTEMP_NORM);
        pr.second.set_temp_conv(BODYTEMP_NORM);
        pr.second.set_frostbite_timer(0);
    }

    npc_ai_info_cache.fill(-1.0);
}
// *INDENT-ON*

void Character::swap_character( Character& other, Character& tmp )
{
    tmp = std::move( other );
    other = std::move( *this );
    *this = std::move( tmp );
    // Reset the dead state cache for both characters since HP values were swapped
    reset_cached_dead_state();
    other.reset_cached_dead_state();
}

void Character::move_operator_common( Character&& source ) noexcept
{

    death_drops = source.death_drops;
    controlling_vehicle = source.controlling_vehicle;

    str_max = source.str_max;
    dex_max = source.dex_max;
    int_max = source.int_max;
    per_max = source.per_max;

    str_cur = source.str_cur;
    dex_cur = source.dex_cur;
    int_cur = source.int_cur;
    per_cur = source.per_cur;
    blocks_left = source.blocks_left;
    dodges_left = source.dodges_left;

    recoil = source.recoil;

    prof = source.prof;
    custom_profession = std::move( source.custom_profession );

    reach_attacking = source.reach_attacking;

    magic = std::move( source.magic );

    name = std::move( source.name );
    male = source.male;

    worn = std::move( source.worn );
    in_vehicle = source.in_vehicle;
    hauling = source.hauling;

    stashed_outbounds_activity = std::move( source.stashed_outbounds_activity );
    stashed_outbounds_backlog = std::move( source.stashed_outbounds_backlog );
    activity = std::move( source.activity );
    backlog = std::move( source.backlog );
    destination_point = source.destination_point;
    last_item = source.last_item;
    last_emote = source.last_emote;

    scent = source.scent;
    my_bionics = std::move( source.my_bionics );
    martial_arts_data = std::move( source.martial_arts_data );

    stomach = std::move( source.stomach );
    consumption_history = std::move( source.consumption_history );

    oxygen = source.oxygen;
    tank_plut = source.tank_plut;
    reactor_plut = source.reactor_plut;
    slow_rad = source.slow_rad;

    focus_pool = source.focus_pool;
    cash = source.cash;
    follower_ids = std::move( source.follower_ids );
    cached_time = source.cached_time;

    addictions = std::move( source.addictions );

    mounted_creature = std::move( source.mounted_creature );
    mounted_creature_id = source.mounted_creature_id;
    activity_vehicle_part_index = source.activity_vehicle_part_index;
    inv = std::move( source.inv );
    omt_path = std::move( source.omt_path );

    position = source.position;

    str_bonus = source.str_bonus;
    dex_bonus = source.dex_bonus;
    per_bonus = source.per_bonus;
    int_bonus = source.int_bonus;

    healthy = source.healthy;
    healthy_mod = source.healthy_mod;

    init_age = source.init_age;
    init_height = source.init_height;
    size_class = source.size_class;

    known_traps = std::move( source.known_traps );
    encumbrance_cache = std::move( source.encumbrance_cache );
    my_mutations = std::move( source.my_mutations );
    last_sleep_check = source.last_sleep_check;
    bio_soporific_powered_at_last_sleep_check = source.bio_soporific_powered_at_last_sleep_check;
    my_traits = std::move( source.my_traits );
    cached_mutations = std::move( source.cached_mutations );
    _skills = std::move( source._skills );
    autolearn_skills_stamp = std::move( source.autolearn_skills_stamp );
    learned_recipes = std::move( source.learned_recipes );

    vision_mode_cache = source.vision_mode_cache;
    nv_range = source.nv_range;
    sight_max = source.sight_max;

    time_died = source.time_died;
    path_settings = std::move( source.path_settings );

    faction_api_version = source.faction_api_version;
    fac_id = source.fac_id;
    my_fac = source.my_fac;

    move_mode = source.move_mode;
    vitamin_levels = std::move( source.vitamin_levels );

    morale = std::move( source.morale );

    destination_activity = std::move( source.destination_activity );
    id = source.id;

    power_level = source.power_level;
    max_power_level = source.max_power_level;
    stored_calories = source.stored_calories;

    thirst = source.thirst;
    stamina = source.stamina;

    fatigue = source.fatigue;
    sleep_deprivation = source.sleep_deprivation;
    check_encumbrance = source.check_encumbrance;

    stim = source.stim;
    pkill = source.pkill;

    radiation = source.radiation;

    auto_move_route = std::move( source.auto_move_route );
    next_expected_position = source.next_expected_position;
    type_of_scent = source.type_of_scent;

    melee_miss_reasons = std::move( source.melee_miss_reasons );

    cached_moves = source.cached_moves;
    cached_position = source.cached_position;
    cached_crafting_inventory = std::move( source.cached_crafting_inventory );

    npc_ai_info_cache = source.npc_ai_info_cache;


    enchantment_cache = std::move( source.enchantment_cache );

    overmap_time = std::move( source.overmap_time );

    next_climate_control_check = source.next_climate_control_check;
    last_climate_control_ret = source.last_climate_control_ret;
}

Character::Character( Character&& source ) noexcept
    : Creature( std::move( source ) ),
      worn( new worn_item_location( this ) ),
      inv( new character_item_location( this ) )
{
    move_operator_common( std::move( source ) );
}

Character &Character::operator=( Character&& source ) noexcept
{
    move_operator_common( std::move( source ) );

    Creature::operator=( std::move( source ) );
    return *this;
}

Character::~Character() = default;

void Character::setID( character_id i, bool force )
{
    if( id.is_valid() && !force ) {
        debugmsg( "tried to set id of a npc/player, but has already a id: %d", id.get_value() );
    } else if( !i.is_valid() && !force ) {
        debugmsg( "tried to set invalid id of a npc/player: %d", i.get_value() );
    } else {
        id = i;
    }
}

character_id Character::getID() const { return this->id; }

auto Character::is_dead_state() const -> bool
{
    if( cached_dead_state.has_value() ) {
    return cached_dead_state.value();
    }

    const auto all_bps = get_all_body_parts( true );
    cached_dead_state = std::ranges::any_of( all_bps, [this]( const bodypart_id & bp ) {
        return bp->essential && get_part_hp_cur( bp ) <= 0;
    } );
    return cached_dead_state.value();
}

void Character::reset_cached_dead_state() { cached_dead_state.reset(); }

void Character::set_part_hp_cur( const bodypart_id& id, int set )
{
    if( set <= 0 ) { cached_dead_state.reset(); }
    Creature::set_part_hp_cur( id, set );
}

void Character::set_part_hp_max( const bodypart_id& id, int set )
{
    if( set <= 0 ) { cached_dead_state.reset(); }
    Creature::set_part_hp_max( id, set );
}

void Character::mod_part_hp_cur( const bodypart_id& id, int mod )
{
    if( mod < 0 ) { cached_dead_state.reset(); }
    Creature::mod_part_hp_cur( id, mod );
}

void Character::mod_part_hp_max( const bodypart_id& id, int mod )
{
    if( mod < 0 ) { cached_dead_state.reset(); }
    Creature::mod_part_hp_max( id, mod );
}

void Character::set_all_parts_hp_cur( int set )
{
    if( set <= 0 ) { cached_dead_state.reset(); }
    Creature::set_all_parts_hp_cur( set );
}


void Character::mod_all_parts_hp_cur( int mod )
{
    if( mod != 0 ) {
        cached_dead_state.reset();
    }
    Creature::mod_all_parts_hp_cur( mod );
}

field_type_id Character::bloodType() const
{
    if( has_trait( trait_ACIDBLOOD ) ) {
    return fd_acid;
}
if( has_trait( trait_THRESH_PLANT ) ) {
    return fd_blood_veggy;
}
if( has_trait( trait_THRESH_INSECT ) || has_trait( trait_THRESH_SPIDER ) ) {
    return fd_blood_insect;
}
if( has_trait( trait_THRESH_CEPHALOPOD ) ) {
    return fd_blood_invertebrate;
}
return fd_blood;
}
field_type_id Character::gibType() const { return fd_gibs_flesh; }

bool Character::in_species( const species_id& spec ) const { return spec == HUMAN; }

bool Character::is_warm() const
{
    // TODO: is there a mutation (plant?) that makes a npc not warm blooded?
    return true;
}

const std::string &Character::symbol() const
{
    static const std::string character_symbol( "@" );
    return character_symbol;
}

void Character::mod_stat( const std::string& stat, float modifier )
{
    if( stat == "str" ) {
        mod_str_bonus( modifier );
    } else if( stat == "dex" ) {
        mod_dex_bonus( modifier );
    } else if( stat == "per" ) {
        mod_per_bonus( modifier );
    } else if( stat == "int" ) {
        mod_int_bonus( modifier );
    } else if( stat == "healthy" ) {
        mod_healthy( modifier );
    } else if( stat == "kcal" ) {
        mod_stored_kcal( modifier );
    } else if( stat == "hunger" ) {
        mod_stored_kcal( -10 * modifier );
    } else if( stat == "thirst" ) {
        mod_thirst( modifier );
    } else if( stat == "fatigue" ) {
        mod_fatigue( modifier );
    } else if( stat == "oxygen" ) {
        oxygen += modifier;
    } else if( stat == "stamina" ) {
        mod_stamina( modifier, false );
    } else {
        Creature::mod_stat( stat, modifier );
    }
}

creature_size Character::get_size() const { return size_class; }

std::string Character::disp_name( bool possessive, bool capitalize_first ) const
{
    if( !possessive ) {
    if( is_player() ) {
            return capitalize_first ? _( "You" ) : _( "you" );
        }
        return name;
    } else {
        if( is_player() ) { return capitalize_first ? _( "Your" ) : _( "your" ); }
        return string_format( _( "%s's" ), name );
    }
}

std::string Character::skin_name() const
{
    // TODO: Return actual deflecting layer name
    return _( "armor" );
}

tripoint_bub_ms Character::bub_pos() const { return get_map().abs_to_bub( position ); }

tripoint_abs_ms Character::abs_pos() const { return position; }

auto Character::setpos( const tripoint_bub_ms& p ) -> void { position = get_map().bub_to_abs( p ); }

auto Character::setpos( const tripoint_abs_ms& p ) -> void { position = p; }

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

bool Character::has_alarm_clock() const
{
    map& here = get_map();
    return (
               has_item_with_flag( flag_ALARMCLOCK, true )
               || ( here.veh_at( bub_pos() )
                    && !here.veh_at( bub_pos() )->vehicle().get_avail_parts( "ALARMCLOCK" ).empty() )
               || has_bionic( bio_infolink ) );
}

bool Character::has_watch() const
{
    map& here = get_map();
    return (
               has_item_with_flag( flag_WATCH, true )
               || ( here.veh_at( bub_pos() )
                    && !here.veh_at( bub_pos() )->vehicle().get_avail_parts( "WATCH" ).empty() )
               || has_bionic( bio_infolink ) );
}

void Character::react_to_felt_pain( int intensity )
{
    if( intensity <= 0 ) { return; }
    if( is_player() && intensity >= 2 ) {
        g->cancel_activity_or_ignore_query( distraction_type::pain, _( "Ouch, something hurts!" ) );
    }
    // Only a large pain burst will actually wake people while sleeping.
    if( has_effect( effect_sleep ) && !has_effect( effect_narcosis ) ) {
        int pain_thresh = rng( 3, 5 );

        if( has_trait( trait_HEAVYSLEEPER ) ) {
            pain_thresh += 2;
        } else if( has_trait( trait_HEAVYSLEEPER2 ) ) {
            pain_thresh += 5;
        }

        if( intensity >= pain_thresh ) { wake_up(); }
    }
}

void Character::mod_pain( int npain )
{
    if( npain > 0 ) {
        if( has_trait( trait_NOPAIN ) || has_effect( effect_narcosis ) ) { return; }
        // always increase pain gained by one from these bad mutations
        if( has_trait( trait_MOREPAIN ) ) {
            npain += std::max( 1, roll_remainder( npain * 0.25 ) );
        } else if( has_trait( trait_MOREPAIN2 ) ) {
            npain += std::max( 1, roll_remainder( npain * 0.5 ) );
        } else if( has_trait( trait_MOREPAIN3 ) ) {
            npain += std::max( 1, roll_remainder( npain * 1.0 ) );
        }

        if( npain > 1 ) {
            // if it's 1 it'll just become 0, which is bad
            if( has_trait( trait_PAINRESIST_TROGLO ) ) {
                npain = roll_remainder( npain * 0.5 );
            } else if( has_trait( trait_PAINRESIST ) ) {
                npain = roll_remainder( npain * 0.67 );
            }
        }
    }
    Creature::mod_pain( npain );
}

void Character::set_pain( int npain )
{
    const int prev_pain = get_perceived_pain();
    Creature::set_pain( npain );
    const int cur_pain = get_perceived_pain();

    if( cur_pain != prev_pain ) {
        react_to_felt_pain( cur_pain - prev_pain );
        on_stat_change( "perceived_pain", cur_pain );
    }
}

namespace
{

/// normalize between 0 to 1
auto remaining_ratio( float value, float max_value ) -> float
{
    return max_value == 0 ? 0 : ( max_value - value ) / max_value;
}

int min_pain( const Character& c )
{
    constexpr int HP_LOSS_PAIN = 40;
    constexpr int BROKEN_LIMB_PAIN = 10;
    constexpr int BITE_PAIN = 5;
    constexpr int INFECTION_PAIN = 10;

    auto get_pain = [&]( const bodypart_id & bp ) -> int {
        // damage to body part, normalized to a scale of 0 to HP_LOSS_PAIN
        // 40 to 50 is "distressing pain"
        int hurt = remaining_ratio( c.get_hp( bp ), c.get_hp_max( bp ) ) * HP_LOSS_PAIN;
        // if body part is broken and not splinted, increase pain by BROKEN_LIMB_PAIN
        if( c.is_limb_broken( bp ) && !c.worn_with_flag( flag_SPLINT, bp ) )
        {
            hurt += BROKEN_LIMB_PAIN;
        }
        const bodypart_str_id bp_id = bp.id();
        // if body part has a bite wound, increase pain by BITE_PAIN
        if( c.has_effect( effect_bite, bp_id ) ) { hurt += BITE_PAIN; }
        // if body part is infected, increase pain by INFECTION_PAIN
        if( c.has_effect( effect_infected, bp_id ) ) { hurt += INFECTION_PAIN; }
        return hurt;
    };

    const auto& bps = c.get_all_body_parts( true );
    if( bps.empty() ) { return 0; }
    return std::ranges::max( bps | std::views::transform( get_pain ) );
}
} // namespace

int Character::get_pain() const
{
    if( get_option<bool>( "CHRONIC_PAIN" ) ) {
    return std::max( Creature::get_pain(), min_pain( *this ) );
    }
    return Creature::get_pain();
}

int Character::get_perceived_pain() const
{
    if( has_effect( effect_adrenaline ) ) {
    return 0;
}

return std::max( get_pain() - get_painkiller(), 0 );
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

void Character::cancel_stashed_activity()
{
    stashed_outbounds_activity = std::make_unique<player_activity>();
    stashed_outbounds_backlog = std::make_unique<player_activity>();
}

player_activity &Character::get_stashed_activity() const { return *stashed_outbounds_activity; }

void Character::set_stashed_activity( std::unique_ptr<player_activity>&& act )
{
    set_stashed_activity( std::move( act ), std::make_unique<player_activity>() );
}

void Character::set_stashed_activity(
    std::unique_ptr<player_activity>&& act, std::unique_ptr<player_activity>&& act_back )
{
    stashed_outbounds_activity = std::move( act );
    stashed_outbounds_backlog =
        act_back ? std::move( act_back ) : std::make_unique<player_activity>();
}

bool Character::has_stashed_activity() const
{
    return static_cast<bool>( *stashed_outbounds_activity );
}

std::unique_ptr<player_activity> Character::remove_stashed_activity()
{
    std::unique_ptr<player_activity> ret = stashed_outbounds_activity.release();
    return ret;
}

void Character::assign_stashed_activity()
{
    activity = std::move( stashed_outbounds_activity );
    backlog.push_front( std::move( stashed_outbounds_backlog ) );
    cancel_stashed_activity();
}


bool Character::check_outbounds_activity( player_activity& act )
{
    map& here = get_map();
    if( ( act.placement != tripoint_abs_ms::zero() && act.placement != tripoint_abs_ms::min()
          && !here.inbounds( here.abs_to_bub( tripoint_abs_ms( act.placement ) ) ) )
        || ( !act.coords.empty()
             && !here.inbounds( here.abs_to_bub( tripoint_abs_ms( act.coords.back() ) ) ) ) ) {

        add_msg( m_debug,
                 "npc %s at pos %d %d, activity target is not inbounds at %d %d therefore activity "
                 "was stashed",
                 disp_name(), bub_pos().x(), bub_pos().y(), act.placement.x(), act.placement.y() );
        return true;
    }
    return false;
}

bool Character::restore_outbounds_activity()
{
    if( check_outbounds_activity( *activity ) ) {
        // stash activity for when reloaded.
        stashed_outbounds_activity = std::move( activity );
        if( !backlog.empty() ) {
            stashed_outbounds_backlog = std::move( backlog.front() );
            backlog.pop_front();
        }
        activity = std::make_unique<player_activity>();
        return true;
    }
    return false;
}

void Character::set_destination_activity(
    std::unique_ptr<player_activity>&& new_destination_activity )
{
    destination_activity = std::move( new_destination_activity );
}

std::unique_ptr<player_activity> Character::clear_destination_activity()
{
    std::unique_ptr<player_activity> r = destination_activity.release();
    return r;
}

player_activity &Character::get_destination_activity() const { return *destination_activity; }

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

/** Returns true if the character has two functioning arms */
bool Character::has_two_arms() const { return get_working_arm_count() >= 2; }

// working is defined here as not disabled which means arms can be not broken
// and still not count if they have low enough hitpoints
int Character::get_working_arm_count() const
{
    if( has_active_mutation( trait_SHELL2 ) ) {
    return 0;
}

int limb_count = 0;
if( !is_limb_disabled( bodypart_id( "arm_l" ) ) ) {
        limb_count++;
    }
    if( !is_limb_disabled( bodypart_id( "arm_r" ) ) ) {
        limb_count++;
    }

    return limb_count;
}

// working is defined here as not broken
int Character::get_working_leg_count() const
{
    int limb_count = 0;
    if( !is_limb_broken( bodypart_id( "leg_l" ) ) ) { limb_count++; }
    if( !is_limb_broken( bodypart_id( "leg_r" ) ) ) { limb_count++; }
    return limb_count;
}

bool Character::is_limb_disabled( const bodypart_id &limb ) const
{
    return is_limb_broken( limb ) ||
    ( get_part_hp_cur( limb ) <= get_part_hp_max( limb ) * 0.125 );
}

// this is the source of truth on if a limb is broken so all code to determine
// if a limb is broken should point here to make any future changes to breaking easier
bool Character::is_limb_broken( const bodypart_id& limb ) const
{
    return has_effect( effect_disabled, limb.id() );
}

bool Character::can_run()
{
    return ( get_stamina() > get_stamina_max() * 0.1f ) && get_working_leg_count() >= 2;
}

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

void Character::expose_to_disease( const diseasetype_id dis_type )
{
    const std::optional<int> &healt_thresh = dis_type->health_threshold;
    if( healt_thresh && healt_thresh.value() < get_healthy() ) { return; }
    const std::set<body_part> &bps = dis_type->affected_bodyparts;
    if( !bps.empty() ) {
        for( const body_part& bp : bps ) {
            add_effect( dis_type->symptoms, rng( dis_type->min_duration, dis_type->max_duration ),
                        convert_bp( bp ), rng( dis_type->min_intensity, dis_type->max_intensity ) );
        }
    } else {
        add_effect(
            dis_type->symptoms, rng( dis_type->min_duration, dis_type->max_duration ),
            bodypart_str_id::NULL_ID(), rng( dis_type->min_intensity, dis_type->max_intensity ) );
    }
}

void Character::recalc_hp()
{
    int str_boost_val = 0;
    std::optional<skill_boost> str_boost = skill_boost::get( "str" );
    if( str_boost ) {
        int skill_total = 0;
        for( const std::string& skill_str : str_boost->skills() ) {
            skill_total += get_skill_level( skill_id( skill_str ) );
        }
        str_boost_val = str_boost->calc_bonus( skill_total );
    }
    // Mutated toughness stacks with starting, by design.
    float hp_mod = 1.0f + mutation_value( "hp_modifier" ) + mutation_value( "hp_modifier_secondary" );
    float hp_adjustment = mutation_value( "hp_adjustment" ) + ( str_boost_val * 3 );
    calc_all_parts_hp( hp_mod, hp_adjustment, get_str_base() );
    cached_dead_state.reset();
}

void Character::calc_all_parts_hp( float hp_mod, float hp_adjustment, int str_max )
{
    for( std::pair<const bodypart_str_id, bodypart> &part : get_body() ) {
        bodypart& bp = get_part( part.first );
        float hp_ratio = static_cast<float>( bp.get_hp_cur() ) / bp.get_hp_max();
        int new_max = ( part.first->base_hp + str_max * 3 + hp_adjustment ) * hp_mod;

        if( has_trait( trait_GLASSJAW ) && part.first == bodypart_str_id( "head" ) ) { new_max *= 0.8; }

        new_max = std::max( new_max, 1 );
        int new_cur = std::ceil( static_cast<float>( new_max ) * hp_ratio );

        bp.set_hp_max( new_max );
        bp.set_hp_cur( std::max( std::min( new_cur, new_max ), 0 ) );
    }
}

// This must be called when any of the following change:
// - effects
// - bionics
// - traits
// - underwater
// - clothes
// With the exception of clothes, all changes to these character attributes must
// occur through a function in this class which calls this function. Clothes are
// typically added/removed with wear() and takeoff(), but direct access to the
// 'wears' vector is still allowed due to refactor exhaustion.
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

namespace vision
{

float threshold_for_nv_range( float range )
{
    constexpr float epsilon = 0.0001f;
    return LIGHT_AMBIENT_LOW / std::exp( range * LIGHT_TRANSPARENCY_OPEN_AIR ) - epsilon;
}

float nv_range_from_per( int per )
{
    // The -1 is because the math is incorrect, but we want the UI to show correct numbers
    return per / 3.0f - 1.0f;
}

float nv_range_from_eye_encumbrance( int enc ) { return -( enc / 10.0f ); }

} // namespace vision

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

void Character::flag_encumbrance() { check_encumbrance = true; }

void Character::check_item_encumbrance_flag()
{
    bool update_required = check_encumbrance;
    for( auto& i : worn ) {
        if( !update_required && i->encumbrance_update_ ) { update_required = true; }
        i->encumbrance_update_ = false;
    }

    if( update_required ) { reset_encumbrance(); }
}

bool Character::natural_attack_restricted_on( const bodypart_id &bp ) const
{
for( const item * const &i : worn ) {
    if( i->covers( bp ) && !i->has_flag( flag_ALLOWS_NATURAL_ATTACKS ) &&
            !i->has_flag( flag_SEMITANGIBLE ) &&
            !i->has_flag( flag_PERSONAL ) && !i->has_flag( flag_AURA ) ) {
            return true;
        }
    }
    return false;
}

bionic_collection &Character::get_bionic_collection() const { return *my_bionics; }
std::vector<bionic_id> Character::get_bionics() const
{
    std::vector<bionic_id> result;
    for( const bionic& b : get_bionic_collection() ) { result.push_back( b.id ); }
    return result;
}

bionic &Character::get_bionic_state( const bionic_id& id )
{
    for( bionic& b : get_bionic_collection() ) {
        if( id == b.id ) { return b; }
    }
    debugmsg( "tried to get state of non-existent bionic with id \"%s\"", id );
    std::abort();
}

bool Character::has_bionic( const bionic_id &b ) const
{
for( const bionic &i : get_bionic_collection() ) {
    if( i.id == b ) {
            return true;
        }
    }
    return false;
}

bool Character::has_active_bionic( const bionic_id &b ) const
{
for( const bionic &i : get_bionic_collection() ) {
    if( i.id == b ) {
            return ( i.powered && i.incapacitated_time == 0_turns );
        }
    }
    return false;
}

bool Character::has_active_bionic_with_fake( const itype_id &it ) const
{
for( const bionic &i : get_bionic_collection() ) {
    if( i.info().fake_item == it ) {
            return ( i.powered && i.incapacitated_time == 0_turns );
        }
    }
    return false;
}

int Character::count_bionic_of_type( const bionic_id& bio ) const
{
    int i = 0;
    for( const bionic& b : get_bionic_collection() ) {
        if( b.id == bio ) { i++; }
    }
    return i;
}

bool Character::has_any_bionic() const { return !get_bionic_collection().empty(); }

bionic_id Character::get_remote_fueled_bionic() const
{
for( const bionic &i : get_bionic_collection() ) {
    const bionic_id &bid = i.id;
    if( bid->is_remote_fueled ) {
            return bid;
        }
    }
    return bionic_id();
}

bool Character::can_fuel_bionic_with( const item &it ) const
{
    if( !it.is_fuel() ) {
    return false;
}

for( const bionic &i : get_bionic_collection() ) {
    const bionic_id &bid = i.id;
    for( const itype_id &fuel : bid->fuel_opts ) {
            if( fuel == it.typeId() ) {
                return true;
            }
        }
    }
    return false;
}

std::vector<bionic_id> Character::get_bionic_fueled_with( const item& it ) const
{
    std::vector<bionic_id> bionics;

    for( const bionic& i : get_bionic_collection() ) {
        const bionic_id& bid = i.id;
        for( const itype_id& fuel : bid->fuel_opts ) {
            if( fuel == it.typeId() ) { bionics.emplace_back( bid ); }
        }
    }

    return bionics;
}

std::vector<bionic_id> Character::get_fueled_bionics() const
{
    std::vector<bionic_id> bionics;
    for( const bionic& i : get_bionic_collection() ) {
        const bionic_id& bid = i.id;
        if( !bid->fuel_opts.empty() ) { bionics.emplace_back( bid ); }
    }
    return bionics;
}

bionic_id Character::get_most_efficient_bionic( const std::vector<bionic_id> &bids ) const
{
    float temp_eff = 0;
    bionic_id bio( "null" );
    for( const bionic_id& bid : bids ) {
        if( bid->fuel_efficiency > temp_eff ) {
            temp_eff = bid->fuel_efficiency;
            bio = bid;
        }
    }
    return bio;
}

units::energy Character::get_power_level() const { return power_level; }

units::energy Character::get_max_power_level() const { return max_power_level; }

void Character::set_power_level( const units::energy& npower )
{
    power_level = std::min( npower, max_power_level );
}

void Character::set_max_power_level( const units::energy& npower_max )
{
    max_power_level = npower_max;
}

void Character::mod_power_level( const units::energy& npower )
{
    // Remaining capacity between current and maximum power levels we can make use of.
    const units::energy remaining_capacity = get_max_power_level() - get_power_level();
    // We can't add more than remaining capacity, so get the minimum of the two
    const units::energy minned_npower = std::min( npower, remaining_capacity );
    // new candidate power level
    const units::energy new_power = get_power_level() + minned_npower;
    // set new power level while prevending it from going negative
    set_power_level( std::max( 0_kJ, new_power ) );
}

void Character::mod_max_power_level( const units::energy& npower_max )
{
    max_power_level += npower_max;
    if( power_level > max_power_level ) { set_power_level( max_power_level ); }
}

bool Character::is_max_power() const { return power_level >= max_power_level; }

bool Character::has_power() const { return power_level > 0_kJ; }

bool Character::has_max_power() const { return max_power_level > 0_kJ; }

bool Character::enough_power_for( const bionic_id& bid ) const
{
    return power_level >= bid->power_activate;
}

void Character::conduct_blood_analysis() const
{
    std::vector<std::string> effect_descriptions;
    std::vector<nc_color> colors;

    for( auto& elem : *effects ) {
        if( elem.first->get_blood_analysis_description().empty() ) { continue; }
        effect_descriptions.emplace_back( elem.first->get_blood_analysis_description() );
        colors.emplace_back( elem.first->get_rating() == e_good ? c_green : c_red );
    }

    const int win_w = 46;
    size_t win_h = 0;
    catacurses::window w;
    ui_adaptor ui;
    ui.on_screen_resize( [&]( ui_adaptor & ui ) {
        win_h = std::
                min( static_cast<size_t>( TERMY ), std::max<size_t>( 1, effect_descriptions.size() ) + 2 );
        w = catacurses::newwin( win_h, win_w, point( ( TERMX - win_w ) / 2, ( TERMY - win_h ) / 2 ) );
        ui.position_from_window( w );
    } );
    ui.mark_resize();

    // RmlUi mirror state (toggle-OFF leaves the curses path untouched). The popup is
    // static, so the bound strings are built once below; on_redraw just re-DirtyVariables.
    struct bt_session {
        std::string title_rml;
        std::string body_rml;
        Rml::DataModelHandle handle;
    } bt_data;
    rml_doc bt_rml;
    bt_data.title_rml = cata_text_to_rml( colorize( _( "Blood Test Results" ), c_red ) );
    if( effect_descriptions.empty() ) {
        bt_data.body_rml = cata_text_to_rml( colorize( _( "No effects." ), c_white ) );
    } else {
        std::string body;
        for( size_t i = 0; i < effect_descriptions.size(); ++i ) {
            body += colorize( effect_descriptions[i], colors[i] );
            body += "\n";
        }
        bt_data.body_rml = cata_text_to_rml( body );
    }

    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( bt_rml ) {
            bt_data.handle.DirtyVariable( "title_rml" );
            bt_data.handle.DirtyVariable( "body_rml" );
        }
        // RmlUi owns the screen; curses fallback removed (rip-out B).
    } );
    input_context ctxt( "BLOOD_TEST_RESULTS" );
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );

    bt_rml.open( blood_test_rmlui_enabled(), "blood_test", ctxt, [&]( Rml::DataModelConstructor & c ) {
        c.Bind( "title_rml", &bt_data.title_rml );
        c.Bind( "body_rml", &bt_data.body_rml );
        bt_data.handle = c.GetModelHandle();
    } );

    bool stop = false;
    // Display new messages
    g->invalidate_main_ui_adaptor();
    while( !stop ) {
        ui_manager::redraw();
        const std::string action = ctxt.handle_input();
        if( action == "CONFIRM" || action == "QUIT" ) { stop = true; }
    }
}

bool &blood_test_rmlui_enabled()
{
    // Default ON (eyeball-confirmed) — curses fallback removed at rip-out.
    static bool enabled = true;
    return enabled;
}

std::vector<itype_id> Character::get_fuel_available( const bionic_id& bio ) const
{
    std::vector<itype_id> stored_fuels;
    for( const itype_id& fuel : bio->fuel_opts ) {
        if( !get_value( fuel.str() ).empty() || fuel->has_flag( flag_PERPETUAL ) ) {
            stored_fuels.emplace_back( fuel );
        }
    }
    return stored_fuels;
}

int Character::get_fuel_type_available( const itype_id& fuel ) const
{
    int amount_stored = 0;
    if( !get_value( fuel.str() ).empty() ) { amount_stored = std::stoi( get_value( fuel.str() ) ); }
    return amount_stored;
}

int Character::get_fuel_capacity( const itype_id& fuel ) const
{
    int amount_stored = 0;
    if( !get_value( fuel.str() ).empty() ) { amount_stored = std::stoi( get_value( fuel.str() ) ); }
    int capacity = 0;
    for( const bionic& i : get_bionic_collection() ) {
        const bionic_id& bid = i.id;
        for( const itype_id& fl : bid->fuel_opts ) {
            if( get_value( bid.str() ).empty() || get_value( bid.str() ) == fl.str() ) {
                if( fl == fuel ) { capacity += bid->fuel_capacity; }
            }
        }
    }
    return capacity - amount_stored;
}

int Character::get_total_fuel_capacity( const itype_id& fuel ) const
{
    int capacity = 0;
    for( const bionic& i : get_bionic_collection() ) {
        const bionic_id& bid = i.id;
        for( const itype_id& fl : bid->fuel_opts ) {
            if( get_value( bid.str() ).empty() || get_value( bid.str() ) == fl.str() ) {
                if( fl == fuel ) { capacity += bid->fuel_capacity; }
            }
        }
    }
    return capacity;
}

void Character::update_fuel_storage( const itype_id& fuel )
{
    const item& it = *item::spawn_temporary( fuel );
    if( get_value( fuel.str() ).empty() ) {
        for( const bionic_id& bid : get_bionic_fueled_with( it ) ) { remove_value( bid.c_str() ); }
        return;
    }

    std::vector<bionic_id> bids = get_bionic_fueled_with( it );
    if( bids.empty() ) { return; }
    int amount_fuel_loaded = std::stoi( get_value( fuel.str() ) );
    std::vector<bionic_id> loaded_bio;

    // Sort bionic in order of decreasing capacity
    // To fill the bigger ones firts.
    bool swap = true;
    while( swap ) {
        swap = false;
        for( size_t i = 0; i < bids.size() - 1; i++ ) {
            if( bids[i + 1]->fuel_capacity > bids[i]->fuel_capacity ) {
                std::swap( bids[i + 1], bids[i] );
                swap = true;
            }
        }
    }

    for( const bionic_id& bid : bids ) {
        remove_value( bid.c_str() );
        if( bid->fuel_capacity <= amount_fuel_loaded ) {
            amount_fuel_loaded -= bid->fuel_capacity;
            loaded_bio.emplace_back( bid );
        } else if( amount_fuel_loaded != 0 ) {
            loaded_bio.emplace_back( bid );
            break;
        }
    }

    for( const bionic_id& bd : loaded_bio ) { set_value( bd.str(), fuel.str() ); }
}

int Character::get_mod_stat_from_bionic( const character_stat& Stat ) const
{
    int ret = 0;
    for( const bionic& i : get_bionic_collection() ) {
        const bionic_id& bid = i.id;
        const auto St_bn = bid->stat_bonus.find( Stat );
        if( St_bn != bid->stat_bonus.end() ) { ret += St_bn->second; }
    }
    return ret;
}


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
std::vector<Character::overlay_entry> Character::get_overlay_ids() const
{
    std::vector<overlay_entry> rval;
    std::multimap<int, overlay_entry> mutation_sorting;
    int order;
    std::string overlay_id;

    // first get effects
    for( const auto& [eff_type, eff_by_part] : *effects ) {
        const auto& eff = eff_by_part.begin()->second;
        if( eff.get_id().is_valid() && !eff.is_removed() ) {
            const std::string& looks_like = eff_type.obj().get_looks_like();

            const overlay_entry
            ent{"effect_" + ( looks_like.empty() ? eff_type.str() : looks_like ), &eff};
            rval.emplace_back( ent );
        }
    }

    // then get mutations
    for( const mutation& mut : my_mutations ) {
        if( !mut.second.show_sprite ) { continue; }
        overlay_id = ( mut.second.powered ? "active_" : "" ) + mut.first.str();
        order = get_overlay_order_of_mutation( overlay_id );
        const overlay_entry ent{overlay_id, &mut};
        mutation_sorting.insert( std::make_pair( order, ent ) );
    }

    // then get bionics
    for( const bionic& bio : get_bionic_collection() ) {
        if( !bio.show_sprite ) { continue; }
        overlay_id = ( bio.powered ? "active_" : "" ) + bio.id.str();
        order = get_overlay_order_of_mutation( overlay_id );
        const overlay_entry ent{overlay_id, &bio};
        mutation_sorting.insert( std::make_pair( order, ent ) );
    }

    // and enchantments mutations
    for( const auto& [ench, src] : enchantment_sources ) {
        for( const auto& mut : ench->get_mutations() ) {
            if( !get_enchantment_mut_visible( mut, *this, *ench, src ) ) { continue; }

            const auto active = get_enchantment_mut_active( mut, *this, *ench, src );

            overlay_id = ( active ? "active_" : "" ) + mut.str();
            order = get_overlay_order_of_mutation( overlay_id );

            // Maybe don't inherit colors from source (entry = std::nullopt)?
            const overlay_entry
            ent{overlay_id, static_variant_cast<decltype( overlay_entry::entry )>( src )};

            mutation_sorting.insert( std::make_pair( order, ent ) );
        }
    }

    for( const auto& [id, ent] : mutation_sorting | std::views::values ) {
        const overlay_entry actual_ent{"mutation_" + id, ent};
        rval.push_back( actual_ent );
    }

    // next clothing
    // TODO: worry about correct order of clothing overlays
    for( const item * const& worn_item : worn ) {
        if( worn_item->has_flag( flag_id( "HIDDEN" ) ) ) { continue; }
        const overlay_entry ent{"worn_" + worn_item->typeId().str(), worn_item};
        rval.push_back( ent );
    }

    // last weapon
    // TODO: might there be clothing that covers the weapon?
    const item& weapon = primary_weapon();
    if( is_armed() ) {
        const overlay_entry ent{"wielded_" + weapon.typeId().str(), &weapon};
        rval.push_back( ent );
    }

    if( move_mode != CMM_WALK ) {
        const overlay_entry ent{io::enum_to_string( move_mode ), std::monostate{}};
        rval.push_back( ent );
    }
    return rval;
}

const SkillLevelMap &Character::get_all_skills() const { return *_skills; }

const SkillLevel &Character::get_skill_level_object( const skill_id& ident ) const
{
    return _skills->get_skill_level_object( ident );
}

SkillLevel &Character::get_skill_level_object( const skill_id& ident )
{
    return _skills->get_skill_level_object( ident );
}

int Character::get_skill_level( const skill_id& ident ) const
{
    return _skills->get_skill_level( ident );
}

int Character::get_skill_level( const skill_id& ident, const item& context ) const
{
    return _skills->get_skill_level( ident, context );
}

void Character::set_skill_level( const skill_id& ident, const int level )
{
    get_skill_level_object( ident ).level( level );
}

void Character::mod_skill_level( const skill_id& ident, const int delta )
{
    _skills->mod_skill_level( ident, delta );
}

std::string Character::enumerate_unmet_requirements( const item& it, const item* context ) const
{
    std::vector<std::string> unmet_reqs;

    const auto check_req = [&unmet_reqs]( const std::string & name, int cur, int req ) {
        if( cur < req ) { unmet_reqs.push_back( string_format( "%s %d", name, req ) ); }
    };

    check_req( _( "strength" ), get_str(), it.get_min_str() );
    check_req( _( "dexterity" ), get_dex(), it.type->min_dex );
    check_req( _( "intelligence" ), get_int(), it.type->min_int );
    check_req( _( "perception" ), get_per(), it.type->min_per );

    for( const auto& elem : it.type->min_skills ) {
        check_req( context->contextualize_skill( elem.first )->name(),
                   get_skill_level( elem.first, *context ), elem.second );
    }

    return enumerate_as_string( unmet_reqs );
}

int Character::rust_rate() const
{
    const std::string& rate_option = get_option<std::string>( "SKILL_RUST" );
    if( rate_option == "off" ) { return 0; }

    // Stat window shows stat effects on based on current stat
    int intel = get_int();
    /** @EFFECT_INT reduces skill rust by 10% per level above 8 */
    int ret =
        ( ( rate_option == "vanilla" || rate_option == "capped" ) ? 100 : 100 + 10 * ( intel - 8 ) );

    ret *= mutation_value( "skill_rust_multiplier" );

    if( ret < 0 ) { ret = 0; }

    return ret;
}

void Character::practice( const skill_id& id, int amount, int cap, bool suppress_warning )
{
    SkillLevel& level = get_skill_level_object( id );
    const Skill& skill = id.obj();
    std::string skill_name = skill.name();

    if( !level.can_train() && !in_sleep_state() ) {
        // If leveling is disabled, don't train, don't drain focus, don't print anything
        // This also checks if your skill level is maxed out at the cap of 10.
        return;
    }

    const auto highest_skill = [&]() {
        std::pair<skill_id, int> result( skill_id::NULL_ID(), -1 );
        for( const auto& pair : *_skills ) {
            const SkillLevel& lobj = pair.second;
            if( lobj.level() > result.second ) { result = std::make_pair( pair.first, lobj.level() ); }
        }
        return result.first;
    };

    const bool isSavant = has_trait( trait_SAVANT );
    const skill_id savantSkill = isSavant ? highest_skill() : skill_id::NULL_ID();

    if( !skill.unaffected_by_focus() ) { amount = adjust_for_focus( amount ); }

    if( has_trait( trait_PACIFIST ) && skill.is_combat_skill() ) {
        if( !one_in( 3 ) ) { amount = 0; }
    }
    if( has_trait_flag( trait_flag_PRED2 ) && skill.is_combat_skill() ) {
        if( one_in( 3 ) ) { amount *= 2; }
    }
    if( has_trait_flag( trait_flag_PRED3 ) && skill.is_combat_skill() ) { amount *= 2; }

    if( has_trait_flag( trait_flag_PRED4 ) && skill.is_combat_skill() ) { amount *= 3; }

    if( isSavant && id != savantSkill ) { amount /= 2; }

    if( amount > 0 && get_skill_level( id ) > cap ) { // blunt grinding cap implementation for crafting
        amount = 0;
        if( !suppress_warning && one_in( 5 ) ) {
            character_funcs::show_skill_capped_notice( *this, id );
        }
    }
    if( amount > 0 && level.isTraining() ) {
        int oldLevel = get_skill_level( id );
        get_skill_level_object( id ).train( amount );
        int newLevel = get_skill_level( id );
        if( newLevel > oldLevel ) {
            g->events().send<event_type::gains_skill_level>( getID(), id, newLevel );
            // Athletics (swimming) skill modifies encumbrance, so make sure encumbrance is updated.
            // No harm updating it for any skill level-up in general.
            reset_encumbrance();
        }
        if( is_player() && newLevel > oldLevel ) {
            add_msg( m_good, _( "Your skill in %s has increased to %d!" ), skill_name, newLevel );
        }
        if( is_player() && newLevel > cap ) {
            // inform player immediately that the current recipe can't be used to train further
            add_msg( m_info, _( "You feel that %s tasks of this level are becoming trivial." ),
                     skill_name );
        }

        int chance_to_drop = focus_pool;
        if( !skill.unaffected_by_focus() ) {
            focus_pool -= chance_to_drop / 100;
            // Apex Predators don't think about much other than killing.
            // They don't lose Focus when practicing combat skills.
            if( ( rng( 1, 100 ) <= ( chance_to_drop % 100 ) )
                && ( !( has_trait_flag( trait_flag_PRED4 ) && skill.is_combat_skill() ) ) ) {
                focus_pool--;
            }
        }
    }

    get_skill_level_object( id ).practice();
}

int Character::read_speed( bool return_stat_effect ) const
{
    // Stat window shows stat effects on based on current stat
    const int intel = get_int();
    /** @EFFECT_INT increases reading speed by 3s per level above 8*/
    int ret = to_moves<int>( 1_minutes ) - to_moves<int>( 3_seconds ) * ( intel - 8 );

    if( has_bionic( afs_bio_linguistic_coprocessor ) ) { ret *= .75; }

    ret *= mutation_value( "reading_speed_multiplier" );

    if( ret < to_moves<int>( 1_seconds ) ) { ret = to_moves<int>( 1_seconds ); }
    // return_stat_effect actually matters here
    return return_stat_effect ? ret : ret * 100 / to_moves<int>( 1_minutes );
}

bool Character::meets_skill_requirements(
    const std::map<skill_id, int> &req, const item* context ) const
{
    return _skills->meets_skill_requirements( req, context ? *context : null_item_reference() );
}

bool Character::meets_skill_requirements( const construction &con ) const
{
    return std::ranges::all_of( con.required_skills,
           [&]( const std::pair<skill_id, int> &pr ) {
        return get_skill_level( pr.first ) >= pr.second;
    } );
}

bool Character::meets_stat_requirements( const item& it ) const
{
    return ( it.has_flag( flag_STR_DRAW ) || get_str() >= it.get_min_str() )
           && get_dex() >= it.type->min_dex && get_int() >= it.type->min_int
           && get_per() >= it.type->min_per;
}

bool Character::meets_requirements( const item& it, const item* context ) const
{
    const auto& ctx = context ? *context : it;
    return meets_stat_requirements( it ) && meets_skill_requirements( it.type->min_skills, &ctx );
}

// Actual player death is mostly handled in game::is_game_over
void Character::die( Creature* nkiller )
{
    g->set_critter_died();
    set_killer( nkiller );
    set_time_died( calendar::turn );
    if( has_effect( effect_lightsnare ) ) {
        inv.add_item( item::spawn( itype_string_36, calendar::start_of_cataclysm ), false );
        inv.add_item( item::spawn( itype_snare_trigger, calendar::start_of_cataclysm ), false );
    }
    if( has_effect( effect_heavysnare ) ) {
        inv.add_item( item::spawn( itype_rope_6, calendar::start_of_cataclysm ), false );
        inv.add_item( item::spawn( itype_snare_trigger, calendar::start_of_cataclysm ), false );
    }
    if( has_effect( effect_beartrap ) ) {
        inv.add_item( item::spawn( itype_beartrap, calendar::start_of_cataclysm ), false );
    }
    mission::on_creature_death( *this );

    cata::run_hooks( "on_character_death", [ &, this]( auto & params ) {
        params["char"] = this;
        params["killer"] = get_killer();
    } );
#ifdef COOP_ENABLED
    if( auto * _log = coop_mutation_log::current() ) {
        _log->push( {
            .type = coop_event_type::creature_died,
            .pos = abs_pos(),
            .creature_id = getID().get_value()} );
    }
#endif
}

void Character::apply_skill_boost()
{
    for( const skill_boost& boost : skill_boost::get_all() ) {
        // For migration, reset previously applied bonus.
        // Remove after 0.E or so.
        const std::string bonus_name = boost.stat() + std::string( "_bonus" );
        std::string previous_bonus = get_value( bonus_name );
        if( !previous_bonus.empty() ) {
            if( boost.stat() == "str" ) {
                str_max -= atoi( previous_bonus.c_str() );
            } else if( boost.stat() == "dex" ) {
                dex_max -= atoi( previous_bonus.c_str() );
            } else if( boost.stat() == "int" ) {
                int_max -= atoi( previous_bonus.c_str() );
            } else if( boost.stat() == "per" ) {
                per_max -= atoi( previous_bonus.c_str() );
            }
            remove_value( bonus_name );
        }
        // End migration code
        int skill_total = 0;
        for( const std::string& skill_str : boost.skills() ) {
            skill_total += get_skill_level( skill_id( skill_str ) );
        }
        mod_stat( boost.stat(), boost.calc_bonus( skill_total ) );
        if( boost.stat() == "str" ) { recalc_hp(); }
    }
}

void Character::do_skill_rust( const time_duration& duration )
{
    const int rust_rate_tmp = rust_rate();

    // For NPC catch-up: check once whether any same-faction ally is in an
    // adjacent overmap tile (any z-level). Only evaluated if is_npc().
    const bool has_ally = [&]() -> bool {
        const tripoint_abs_omt self_omt = abs_omt_pos();
        return !g->get_npcs_if( [this, &self_omt]( const npc & other )
        {
            return other.is_ally( *this ) &&
                        rl_dist( other.abs_omt_pos().xy(), self_omt.xy() ) <= 1;
        } ).empty();
    }();

    for( std::pair<const skill_id, SkillLevel> &pair : *_skills ) {
        const Skill& aSkill = *pair.first;
        SkillLevel& skill_level_obj = pair.second;

        const int pred_tick =
            has_trait_flag( trait_flag_PRED2 )   ? calendar::ticks_between( duration, 8_hours )
            : has_trait_flag( trait_flag_PRED3 ) ? calendar::ticks_between( duration, 4_hours )
            : has_trait_flag( trait_flag_PRED4 )
            ? calendar::ticks_between( duration, 3_hours )
            : 0;

        if( aSkill.is_combat_skill() && pred_tick > 0 ) {
            // Their brain is optimized to remember this
            int tries = 0;
            for( int i = 0; i < pred_tick; i++ ) { tries += one_in( 13 ) ? 1 : 0; }
            if( tries > 0 && !has_effect( effect_sleep ) ) {
                // They've already passed the roll to avoid rust at
                // this point, but print a message about it now and
                // then.
                //
                // 13 combat skills.
                // This means PRED2/PRED3/PRED4 think of hunting on
                // average every 8/4/3 hours, enough for immersion
                // without becoming an annoyance.
                //
                // Additionally, catching up NPCs will prevent rust
                // for combat skills, presumably they'd hunt while
                // the player is gone.
                add_msg_if_player( _( "Your heart races as you recall your most recent hunt." ) );
                mod_stim( tries );
            }
            continue;
        }

        if( rust_rate_tmp <= 0 ) { continue; }

        const bool charged_bio_mem =
            get_power_level() > bio_memory->power_trigger && has_active_bionic( bio_memory );

        const int n_ticks =
            calendar::ticks_between( duration, skill_level_obj.rust_interval( rust_rate_tmp ) );

        if( is_npc() && n_ticks > 0 ) {
            // Catch-up path: simulate all rust ticks that would have fired during duration.

            // NPCs practice skills while the player is away, preventing rust.
            // Combat skills are always practiced; others require a nearby ally.
            if( skill_level_obj.can_train() ) {
                if( aSkill.is_combat_skill() || has_ally ) {
                    skill_level_obj.practice();
                    continue;
                }
            }

            // bio_memory saves up to what power can afford; indefinite sources are unlimited.
            // Technically this handwaves nutrition costs for metabolic power sources, but
            // that's fine.
            const int trigger_kj = units::to_kilojoule( bio_memory->power_trigger );
            const int max_bio_saves =
                !charged_bio_mem                ? 0
                : has_indefinite_power_source() ? std::numeric_limits<int>::max()
                : trigger_kj > 0
                ? units::to_kilojoule( get_power_level() ) / trigger_kj
                : 0;

            const int oldSkillLevel = skill_level_obj.level();
            const int bio_saves = skill_level_obj.rust_by( duration, max_bio_saves, rust_rate_tmp );

            if( bio_saves > 0 ) { mod_power_level( -units::from_kilojoule( trigger_kj * bio_saves ) ); }
            const int newSkill = skill_level_obj.level();
            if( newSkill < oldSkillLevel ) { reset_encumbrance(); }
        } else {
            // Per-tick path
            const int oldSkillLevel = skill_level_obj.level();
            if( skill_level_obj.rust( charged_bio_mem, rust_rate_tmp ) ) {
                add_msg_if_player(
                    m_warning,
                    _( "Your knowledge of %s begins to fade, but your memory banks retain it!" ),
                    aSkill.name() );
                mod_power_level( -bio_memory->power_trigger );
            }
            const int newSkill = skill_level_obj.level();
            if( newSkill < oldSkillLevel ) {
                add_msg_if_player(
                    m_bad, _( "Your skill in %s has reduced to %d!" ), aSkill.name(), newSkill );
                reset_encumbrance();
            }
        }
    }
}

void Character::reset()
{
    recalculate_enchantment_cache();
    // TODO: Move reset_stats here, remove it from Creature
    Creature::reset();
}

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

/*
 * Innate stats setters
 */

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

namespace io
{
template <> std::string enum_to_string<character_stat>( character_stat data )
{
    switch( data ) {
            // *INDENT-OFF*
        case character_stat::STRENGTH:
            return "STR";
        case character_stat::DEXTERITY:
            return "DEX";
        case character_stat::INTELLIGENCE:
            return "INT";
        case character_stat::PERCEPTION:
            return "PER";

        // *INDENT-ON*
        case character_stat::DUMMY_STAT:
            break;
    }
    abort();
}
} // namespace io

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



int Character::blood_loss( const bodypart_id& bp ) const
{
    int hp_cur_sum = get_part_hp_cur( bp );
    int hp_max_sum = get_part_hp_max( bp );

    if( bp == bodypart_id( "leg_l" ) || bp == bodypart_id( "leg_r" ) ) {
        hp_cur_sum = get_part_hp_cur( bodypart_id( "leg_l" ) ) + get_part_hp_cur( bodypart_id( "leg_r" ) );
        hp_max_sum = get_part_hp_max( bodypart_id( "leg_l" ) ) + get_part_hp_max( bodypart_id( "leg_r" ) );
    } else if( bp == bodypart_id( "arm_l" ) || bp == bodypart_id( "arm_r" ) ) {
        hp_cur_sum = get_part_hp_cur( bodypart_id( "arm_l" ) ) + get_part_hp_cur( bodypart_id( "arm_r" ) );
        hp_max_sum = get_part_hp_max( bodypart_id( "arm_l" ) ) + get_part_hp_max( bodypart_id( "arm_r" ) );
    }

    hp_cur_sum = std::min( hp_max_sum, std::max( 0, hp_cur_sum ) );
    hp_max_sum = std::max( hp_max_sum, 1 );
    return 100 - ( 100 * hp_cur_sum ) / hp_max_sum;
}

float Character::get_dodge_base() const
{
    /** @EFFECT_DEX increases dodge base */
    /** @EFFECT_DODGE increases dodge_base */
    return get_dex() / 4.0f + get_skill_level( skill_dodge );
}
float Character::get_hit_base() const
{
    /** @EFFECT_DEX increases hit base, slightly */
    return get_dex() / 4.0f;
}


namespace
{

struct healable_bp {
    mutable bool allowed;
    bodypart_id bp;
    std::string name; // Translated name as it appears in the menu.
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

        // Calculate damage deficit for tie-breaking/general priority
        const int cur_hp = c.get_part_hp_cur( bp );
        const int max_hp = c.get_part_hp_max( bp );
        const int cur_dmg = max_hp - cur_hp;

        // Bandaging Priority Check (Highest priority overall)
        if( is_bandage ) {
            if( c.has_effect( effect_bleed, bp_str_id ) ) {
                current_priority = 2000; // PRIORITY 1: Bleeding
            } else if( !c.has_effect( effect_bandaged, bp_str_id ) ) {
                // PRIORITY 2: Max Damage, not bandaged yet.
                current_priority = 500 + cur_dmg;
            } else {
                // PRIORITY 3: Bandaged, but can be improved
                const int b_power = c.get_effect_int( effect_bandaged, bp_str_id );
                int new_b_power = static_cast<int>( std::floor( bandage_power ) );
                if( new_b_power > b_power ) { current_priority = 100 + ( new_b_power - b_power ); }
            }
        }

        // Disinfectant Priority Check (Secondary/Fallback priority)
        if( is_disinfectant && !c.has_effect( effect_bleed, bp_str_id ) ) {
            if( c.has_effect( effect_bite, bp_str_id ) ) {
                // Check if this priority (1000) is higher than any non-bleeding bandaging priority
                // (max 500+dmg)
                if( current_priority < 1000 ) {
                    current_priority = 1000; // PRIORITY 1: Deep Bite
                }
            } else if( !c.has_effect( effect_disinfected, bp_str_id ) ) {
                // PRIORITY 2: Max Damage, not disinfected yet.
                if( current_priority < 500 + cur_dmg ) { current_priority = 500 + cur_dmg; }
            } else {
                // PRIORITY 3: Disinfected, but could benefit from quality improvement
                const int d_power = c.get_effect_int( effect_disinfected, bp_str_id );
                int new_d_power = static_cast<int>( std::floor( disinfectant_power ) );
                if( new_d_power > d_power ) {
                    int potential_priority = 100 + ( new_d_power - d_power );
                    if( current_priority < potential_priority ) {
                        current_priority = potential_priority;
                    }
                }
            }
        }

        // General update
        if( current_priority > max_priority ) {
            max_priority = current_priority;
            best_selection_index = i;
        }
    }

    // PRIORITY 4: Fallback to the first item (index 0 / Head)
    // This is "nothing will benefit from an item" path
    if( best_selection_index == -1 ) {
        return 0;
    } else {
        return best_selection_index;
    }
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

bool Character::is_immune_field( const field_type_id& fid ) const
{
    // Obviously this makes us invincible
    if( has_trait( trait_DEBUG_NODMG ) ) {
    return true;
}
// Check to see if we are immune
const field_type &ft = fid.obj();
for( const trait_id &t : ft.immunity_data_traits ) {
    if( has_trait( t ) ) {
            return true;
        }
    }
    bool immune_by_body_part_resistance = !ft.immunity_data_body_part_env_resistance.empty();
for( const std::pair<body_part, int> &fide : ft.immunity_data_body_part_env_resistance ) {
    immune_by_body_part_resistance = immune_by_body_part_resistance &&
                                     get_env_resist( convert_bp( fide.first ).id() ) >= fide.second;
    }
    if( immune_by_body_part_resistance ) {
    return true;
}
if( ft.has_elec ) {
    return is_elec_immune();
    }
    if( ft.has_fire ) {
    return has_active_bionic( bio_heatsink ) || is_wearing( itype_rm13_armor_on );
    }
    if( ft.has_acid ) {
    return !is_on_ground() && get_env_resist( bodypart_id( "foot_l" ) ) >= 15 &&
               get_env_resist( bodypart_id( "foot_r" ) ) >= 15 &&
               get_env_resist( bodypart_id( "leg_l" ) ) >= 15 &&
               get_env_resist( bodypart_id( "leg_r" ) ) >= 15 &&
               get_armor_type( DT_ACID, bodypart_id( "foot_l" ) ) >= 5 &&
               get_armor_type( DT_ACID, bodypart_id( "foot_r" ) ) >= 5 &&
               get_armor_type( DT_ACID, bodypart_id( "leg_l" ) ) >= 5 &&
               get_armor_type( DT_ACID, bodypart_id( "leg_r" ) ) >= 5;
    }
    // If we haven't found immunity yet fall up to the next level
    return Creature::is_immune_field( fid );
}

bool Character::is_elec_immune() const { return is_immune_damage( DT_ELECTRIC ); }

bool Character::is_immune_effect( const efftype_id& eff ) const
{
    if( eff == effect_downed ) {
        return is_throw_immune() || ( has_trait( trait_LEG_TENT_BRACE ) && footwear_factor() == 0 );
    } else if( eff == effect_onfire ) {
        return is_immune_damage( DT_HEAT );
    } else if( eff == effect_deaf ) {
        return worn_with_flag( flag_DEAF ) || worn_with_flag( flag_PARTIAL_DEAF )
               || has_bionic( bio_ears ) || is_wearing( itype_rm13_armor_on );
    } else if( eff == effect_corroding ) {
        return is_immune_damage( DT_ACID ) || has_trait( trait_SLIMY ) || has_trait( trait_VISCOUS );
    } else if( eff == effect_nausea ) {
        return has_trait( trait_STRONGSTOMACH );
    } else if( eff == effect_spores || eff == effect_fungus ) {
        return has_trait( trait_M_IMMUNE );
    } else if( eff == effect_bleed ) {
        // Ugly, it was badly implemented and should be a flag
        return mutation_value( "bleed_resist" ) > 0.0f;
    }

    return false;
}

bool Character::is_immune_damage( const damage_type dt ) const
{
    switch( dt ) {
    case DT_NULL:
        return true;
    case DT_TRUE:
        return false;
    case DT_BIOLOGICAL:
        return has_effect_with_flag( flag_EFFECT_BIO_IMMUNE ) ||
                   worn_with_flag( flag_BIO_IMMUNE );
        case DT_BASH:
            return has_effect_with_flag( flag_EFFECT_BASH_IMMUNE )
                   || worn_with_flag( flag_BASH_IMMUNE );
        case DT_CUT:
            return has_effect_with_flag( flag_EFFECT_CUT_IMMUNE ) || worn_with_flag( flag_CUT_IMMUNE );
        case DT_ACID:
            return has_trait( trait_ACIDPROOF ) || has_effect_with_flag( flag_EFFECT_ACID_IMMUNE )
                   || worn_with_flag( flag_ACID_IMMUNE );
        case DT_STAB:
            return has_effect_with_flag( flag_EFFECT_STAB_IMMUNE )
                   || worn_with_flag( flag_STAB_IMMUNE );
        case DT_BULLET:
            return has_effect_with_flag( flag_EFFECT_BULLET_IMMUNE )
                   || worn_with_flag( flag_BULLET_IMMUNE );
        case DT_HEAT:
            return has_trait( trait_M_SKIN2 ) || has_trait( trait_M_SKIN3 )
                   || has_effect_with_flag( flag_EFFECT_HEAT_IMMUNE )
                   || worn_with_flag( flag_HEAT_IMMUNE );
        case DT_COLD:
            return has_effect_with_flag( flag_EFFECT_COLD_IMMUNE )
                   || worn_with_flag( flag_COLD_IMMUNE );
        case DT_DARK:
            return has_effect_with_flag( flag_EFFECT_DARK_IMMUNE )
                   || worn_with_flag( flag_DARK_IMMUNE );
        case DT_LIGHT:
            return has_effect_with_flag( flag_EFFECT_LIGHT_IMMUNE )
                   || worn_with_flag( flag_LIGHT_IMMUNE );
        case DT_PSI:
            return has_effect_with_flag( flag_EFFECT_PSI_IMMUNE ) || worn_with_flag( flag_PSI_IMMUNE );
        case DT_ELECTRIC:
            return has_active_bionic( bio_faraday ) || worn_with_flag( flag_ELECTRIC_IMMUNE )
                   || has_artifact_with( AEP_RESIST_ELECTRICITY )
                   || has_effect_with_flag( flag_EFFECT_ELECTRIC_IMMUNE );
        default:
            return true;
    }
}

bool Character::is_rad_immune() const
{
    bool has_helmet = false;
    return ( is_wearing_power_armor( &has_helmet ) && has_helmet ) || worn_with_flag( flag_RAD_PROOF );
}

int Character::throw_range( const item &it ) const
{
    if( it.is_null() ) {
    return -1;
}

item &tmp = *item::spawn_temporary( it );

if( tmp.count_by_charges() && tmp.charges > 1 ) {
    tmp.charges = 1;
}

/** @EFFECT_STR determines maximum weight that can be thrown */
if( ( tmp.weight() / 100_gram ) > static_cast<int>( str_cur * 15 ) ) {
        return 0;
    }
    // Increases as weight decreases until 150 g, then decreases again
    /** @EFFECT_STR increases throwing range, vs item weight (high or low) */
    int str_override = str_cur;
    if( is_mounted() ) {
    auto mons = mounted_creature.get();
        str_override = mons->mech_str_addition() != 0 ? mons->mech_str_addition() : str_cur;
    }
    const int divisor =
        tmp.weight() >= 150_gram
        ? tmp.weight() / 100_gram
        : 10 - static_cast<int>( tmp.weight() / 15_gram );
    int ret = ( str_override * 10 ) / divisor;
    ret -= tmp.volume() / 1_liter;
    static const std::set<material_id> affected_materials = { material_id( "iron" ), material_id( "steel" ) };
    if( has_active_bionic( bio_railgun ) && tmp.made_of_any( affected_materials ) ) {
    ret *= 2;
}
if( ret < 1 ) {
    return 1;
}
// Cap at double our strength + skill
/** @EFFECT_STR caps throwing range */

/** @EFFECT_THROW caps throwing range */
return std::min( ret, str_override * 3 + get_skill_level( skill_throw ) );
}

const std::vector<material_id> Character::fleshy = {material_id( "flesh" ), material_id( "hflesh" )};
bool Character::made_of( const material_id& m ) const
{
    // TODO: check for mutations that change this.
    return std::ranges::contains( fleshy, m );
}
bool Character::made_of_any( const std::set<material_id> &ms ) const
{
    // TODO: check for mutations that change this.
    return std::ranges::any_of( fleshy, [&ms]( const material_id & e ) { return ms.count( e ); } );
}

tripoint_abs_sm Character::abs_sm_pos() const { return project_to<coords::sm>( abs_pos() ); }

tripoint_abs_omt Character::abs_omt_pos() const { return project_to<coords::omt>( abs_pos() ); }

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

/*
 * Calculate player brightness based on the brightest active item, as
 * per itype tag LIGHT_* and optional CHARGEDIM ( fade starting at 20% charge )
 * item.light.* is -unimplemented- for the moment, as it is a custom override for
 * applying light sources/arcs with specific angle and direction.
 */
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

detached_ptr<item> Character::pour_into( item& container, detached_ptr<item>&& liquid, int limit )
{
    std::string err;
    const int amount =
        std::min( limit, container.get_remaining_capacity_for_liquid( *liquid, *this, &err ) );

    if( !err.empty() ) {
        add_msg_if_player( m_bad, err );
        return std::move( liquid );
    }

    add_msg_if_player( _( "You pour %1$s into the %2$s." ), liquid->tname(), container.tname() );

    liquid = container.fill_with( std::move( liquid ), amount );
    inv.unsort();

    if( liquid ) { add_msg_if_player( _( "There's some left over!" ) ); }

    return std::move( liquid );
}

detached_ptr<item> Character::pour_into( vehicle& veh, detached_ptr<item>&& liquid, int limit )
{
    auto sel = [&]( const vehicle_part & pt ) { return pt.is_tank() && pt.can_reload( &*liquid ); };

    auto stack = units::legacy_volume_factor / liquid->type->stack_size;
    auto title = string_format(
                     _( "Select target tank for <color_%s>%.1fL %s</color>" ),
                     get_all_colors().get_name( liquid->color() ), round_up( to_liter( liquid->charges * stack ), 1 ),
                     liquid->tname() );

    auto& tank = veh_interact::select_part( veh, sel, title );
    if( !tank ) { return std::move( liquid ); }

    //~ $1 - vehicle name, $2 - part name, $3 - liquid type
    add_msg_if_player(
        _( "You refill the %1$s's %2$s with %3$s." ), veh.name, tank.name(), liquid->type_name() );

    liquid = tank.fill_with( std::move( liquid ), limit );


    if( liquid ) { add_msg_if_player( _( "There's some left over!" ) ); }
    return std::move( liquid );
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

float Character::rest_quality() const
{
    // Just a placeholder for now.
    // TODO: Waiting/reading/being unconscious on bed/sofa/grass
    return has_effect( effect_sleep ) ? 1.0f : 0.0f;
}

bodypart_str_id Character::bp_to_hp( const bodypart_str_id& bp ) { return bp->main_part; }

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

float Character::bmi() const { return 25; }

units::mass Character::bodyweight() const
{
    return units::from_kilogram( bmi() * std::pow( height() / 100.0f, 2 ) );
}

units::mass Character::bionics_weight() const
{
    units::mass bio_weight = 0_gram;
    for( const bionic& i : get_bionic_collection() ) {
        const bionic_id& bid = i.id;
        if( !bid->included ) { bio_weight += bid->itype()->weight; }
    }
    return bio_weight;
}

void Character::reset_chargen_attributes()
{
    init_age = 25;
    init_height = 175;
}

int Character::base_age() const { return init_age; }

void Character::set_base_age( int age ) { init_age = age; }

void Character::mod_base_age( int mod ) { init_age += mod; }

int Character::age() const
{
    int years_since_cataclysm =
        to_turns<int>( calendar::turn - calendar::turn_zero )
    / to_turns<int>( calendar::year_length() );
    return init_age + years_since_cataclysm;
}

std::string Character::age_string() const
{
    //~ how old the character is in years. try to limit number of characters to fit on the screen
    std::string unformatted = _( "%d years" );
    return string_format( unformatted, age() );
}

int Character::base_height() const { return init_height; }

void Character::set_base_height( int height ) { init_height = height; }

void Character::mod_base_height( int mod ) { init_height += mod; }

std::string Character::height_string() const
{
    const bool metric = get_option<std::string>( "DISTANCE_UNITS" ) == "metric";

    if( metric ) {
        std::string metric_string = _( "%d cm" );
        return string_format( metric_string, height() );
    }

    int total_inches = std::round( height() / 2.54 );
    int feet = std::floor( total_inches / 12 );
    int remainder_inches = total_inches % 12;
    return string_format( "%d\'%d\"", feet, remainder_inches );
}

int Character::height() const
{
    switch( get_size() ) {
    case creature_size::tiny:
        return init_height * 0.5;
    case creature_size::small:
        return init_height * 0.75;
    case creature_size::medium:
        return init_height;
    case creature_size::large:
        return init_height * 1.5;
    case creature_size::huge:
        return init_height * 2;
    default:
        break;
}

debugmsg( "Invalid size class" );
abort();
}

int Character::get_armor_bash( bodypart_id bp ) const
{
    return get_armor_bash_base( bp ) + armor_bash_bonus;
}

int Character::get_armor_cut( bodypart_id bp ) const
{
    return get_armor_cut_base( bp ) + armor_cut_bonus;
}

int Character::get_armor_bullet( bodypart_id bp ) const
{
    return get_armor_bullet_base( bp ) + armor_bullet_bonus;
}

// TODO: Reduce duplication with below function
int Character::get_armor_type( damage_type dt, bodypart_id bp ) const
{
    switch( dt ) {
    case DT_TRUE:
    case DT_BIOLOGICAL:
        return 0;
    case DT_BASH:
        return get_armor_bash( bp );
        case DT_CUT:
            return get_armor_cut( bp );
        case DT_STAB:
            return get_armor_cut( bp ) * 0.8f;
        case DT_BULLET:
            return get_armor_bullet( bp );
        case DT_ACID:
        case DT_HEAT:
        case DT_COLD:
        case DT_DARK:
        case DT_LIGHT:
        case DT_PSI:
        case DT_ELECTRIC: {
            int ret = 0;
            for( const auto& i : worn ) {
                if( i->covers( bp ) ) { ret += i->damage_resist( dt ); }
            }

            ret += mutation_armor( bp, dt );
            return ret;
        }
        case DT_NULL:
        case NUM_DT:
            // Let it error below
            break;
    }

    debugmsg( "Invalid damage type: %d", dt );
    return 0;
}

std::map<bodypart_id, int> Character::get_all_armor_type(
    damage_type dt, const std::map<bodypart_id, std::vector<const item *>> &clothing_map ) const
{
    std::map<bodypart_id, int> ret;
    for( const bodypart_id& bp : get_all_body_parts() ) { ret.emplace( bp, 0 ); }

    for( std::pair<const bodypart_id, int> &per_bp : ret ) {
        const bodypart_id& bp = per_bp.first;
        switch( dt ) {
            case DT_TRUE:
            case DT_BIOLOGICAL:
                // Characters cannot resist this
                return ret;
            /* BASH, CUT, STAB, and BULLET don't benefit from the clothing_map optimization */
            // TODO: Fix that
            case DT_BASH:
                per_bp.second += get_armor_bash( bp );
                break;
            case DT_CUT:
                per_bp.second += get_armor_cut( bp );
                break;
            case DT_STAB:
                per_bp.second += get_armor_cut( bp ) * 0.8f;
                break;
            case DT_BULLET:
                per_bp.second += get_armor_bullet( bp );
                break;
            case DT_ACID:
            case DT_HEAT:
            case DT_COLD:
            case DT_DARK:
            case DT_LIGHT:
            case DT_PSI:
            case DT_ELECTRIC: {
                for( const item * it : clothing_map.at( bp ) ) {
                    per_bp.second += it->damage_resist( dt );
                }

                per_bp.second += mutation_armor( bp, dt );
                break;
            }
            case DT_NULL:
            case NUM_DT:
                debugmsg( "Invalid damage type: %d", dt );
                return ret;
        }
    }

    return ret;
}

int Character::get_armor_bash_base( bodypart_id bp ) const
{
    int ret = 0;
    for( auto& i : worn ) {
        if( i->covers( bp ) ) { ret += i->bash_resist(); }
    }
    for( const bionic& i : get_bionic_collection() ) {
        const bionic_id& bid = i.id;
        const auto bash_prot = bid->bash_protec.find( bp.id() );
        if( bash_prot != bid->bash_protec.end() ) { ret += bash_prot->second; }
    }

    ret += mutation_armor( bp, DT_BASH );
    return ret;
}

int Character::get_armor_cut_base( bodypart_id bp ) const
{
    int ret = 0;
    for( auto& i : worn ) {
        if( i->covers( bp ) ) { ret += i->cut_resist(); }
    }
    for( const bionic& i : get_bionic_collection() ) {
        const bionic_id& bid = i.id;
        const auto cut_prot = bid->cut_protec.find( bp.id() );
        if( cut_prot != bid->cut_protec.end() ) { ret += cut_prot->second; }
    }

    ret += mutation_armor( bp, DT_CUT );
    return ret;
}

int Character::get_armor_bullet_base( bodypart_id bp ) const
{
    int ret = 0;
    for( auto& i : worn ) {
        if( i->covers( bp ) ) { ret += i->bullet_resist(); }
    }

    for( const bionic& i : get_bionic_collection() ) {
        const bionic_id& bid = i.id;
        const auto bullet_prot = bid->bullet_protec.find( bp.id() );
        if( bullet_prot != bid->bullet_protec.end() ) { ret += bullet_prot->second; }
    }

    ret += mutation_armor( bp, DT_BULLET );
    return ret;
}

int Character::get_env_resist( bodypart_id bp ) const
{
    int ret = 0;
    for( auto& i : worn ) {
        // Head protection works on eyes too (e.g. baseball cap)
        if( i->covers( bp ) || ( bp == bodypart_id( "eyes" ) && i->covers( bodypart_id( "head" ) ) ) ) {
            ret += i->get_env_resist();
        }
    }

    for( const bionic& i : get_bionic_collection() ) {
        const bionic_id& bid = i.id;
        const auto EP = bid->env_protec.find( bp.id() );
        if( ( !bid->activated || has_active_bionic( bid ) ) && EP != bid->env_protec.end() ) {
            ret += EP->second;
        }
    }

    if( bp == bodypart_id( "eyes" ) && has_trait( trait_SEESLEEP ) ) { ret += 8; }
    return ret;
}

int Character::get_armor_acid( bodypart_id bp ) const { return get_armor_type( DT_ACID, bp ); }


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

bool Character::invoke_item( item* used ) { return invoke_item( used, bub_pos() ); }

bool Character::invoke_item( item*, const tripoint_bub_ms & ) { return false; }

bool Character::invoke_item( item* used, const std::string& method )
{
    return invoke_item( used, method, bub_pos() );
}

bool Character::invoke_item( item* used, const std::string& method, const tripoint_bub_ms& pt )
{
    if( method != iuse_TOGGLE_UPS_CHARGING && !has_enough_charges( *used, true ) ) { return false; }
    if( method == iuse_TOGGLE_UPS_CHARGING ) {
        iuse::toggle_ups_charging( this->as_player(), used, false, pt );
        return true;
    }
    item* actually_used = used->get_usable_item( method );
    if( actually_used == nullptr ) {
        debugmsg( "Tried to invoke a method %s on item %s, which doesn't have this method",
                  method.c_str(), used->tname() );
        return false;
    }

    int charges_used = actually_used->type->invoke( *this->as_player(), *actually_used, pt, method );
    if( charges_used == 0 ) { return false; }
    // Prevent accessing the item as it may have been deleted by the invoked iuse function.

    if( used->is_tool() || used->is_medication() || used->get_contained().is_medication() ) {
        return consume_charges( *actually_used, charges_used );
    } else if( used->is_bionic() || used->is_deployable() || method == "place_trap" ) {
        used->detach();
        return true;
    } else if( used->count_by_charges() ) {
        used->charges -= charges_used;
        if( used->charges <= 0 ) { used->detach(); }
        return true;
    }

    return false;
}

detached_ptr<item> Character::dispose_item( detached_ptr<item>&& obj, const std::string& prompt )
{
    uilist menu;
    menu.text = prompt.empty() ? string_format( _( "Dispose of %s" ), obj->tname() ) : prompt;

    using dispose_option = struct {
        std::string prompt;
        bool enabled;
        char invlet;
        int moves;
        std::function<detached_ptr<item>()> action;
    };

    std::vector<dispose_option> opts;

    const bool bucket = obj->is_bucket_nonempty();

    opts.emplace_back( dispose_option{
        bucket ? _( "Spill contents and store in inventory" ) : _( "Store in inventory" ),
        volume_carried() + obj->volume() <= volume_capacity(), '1', item_handling_cost( *obj ),
        [this, bucket, &obj] {
            if( bucket && !obj->spill_contents( *this ) )
        {
            return std::move( obj );
            }

            moves -= item_handling_cost( *obj );
            inv.add_item( std::move( obj ), true );
            inv.unsort();
            return detached_ptr<item>();
        }} );

    opts.emplace_back( dispose_option{
        _( "Drop item" ), true, '2', 0, [this, &obj] {
            put_into_vehicle_or_drop( *this, item_drop_reason::deliberate, std::move( obj ) );
            return detached_ptr<item>();
        }} );

    opts.emplace_back( dispose_option{
        bucket ? _( "Spill contents and wear item" ) : _( "Wear item" ),
        can_wear( *obj ).success(), '3', item_wear_cost( *obj ),
        [this, bucket, &obj] {
            if( bucket && !obj->spill_contents( *this ) )
        {
            return std::move( obj );
            }

            return wear_item( std::move( obj ) );
        }} );

    for( auto& e : worn ) {
        if( e->can_holster( *obj ) ) {
            auto ptr = dynamic_cast<const holster_actor *>(
                           e->type->get_use( "holster" )->get_actor_ptr() );
            opts.emplace_back( dispose_option{
                string_format( _( "Store in %s" ), e->tname() ), true, e->invlet,
                item_store_cost( *obj, *e, false, ptr->draw_cost ), [this, ptr, &e, &obj] {
                    return ptr->store( *this->as_player(), *e, std::move( obj ) );
                }} );
        }
    }

    int w = utf8_width( menu.text, true ) + 4;
    for( const auto& e : opts ) { w = std::max( w, utf8_width( e.prompt, true ) + 4 ); }
    for( auto& e : opts ) { e.prompt += std::string( w - utf8_width( e.prompt, true ), ' ' ); }

    menu.text.insert( 0, 2, ' ' ); // add space for UI hotkeys
    menu.text += std::string( w + 2 - utf8_width( menu.text, true ), ' ' );
    menu.text += _( " | Moves  " );

    for( const auto& e : opts ) {
        menu.addentry( -1, e.enabled, e.invlet,
                       string_format( e.enabled ? "%s | %-7d" : "%s |", e.prompt, e.moves ) );
    }

    menu.query();
    if( menu.ret >= 0 ) { return opts[menu.ret].action(); }
    return std::move( obj );
}

bool Character::dispose_item( item& obj, const std::string& prompt )
{
    Character& who = *this;
    return obj.attempt_detach( [&who, &prompt]( detached_ptr<item>&& it ) {
        return who.dispose_item( std::move( it ), prompt );
    } );
}

bool Character::has_enough_charges( const item &it, bool show_msg ) const
{
    if( !it.is_tool() || !it.ammo_required() ) {
    return true;
}
if( it.is_power_armor() ) {
    if( ( character_funcs::can_interface_armor( *this ) &&
              has_charges( itype_bio_armor, it.ammo_required() ) ) ||
            ( it.has_flag( flag_USE_UPS ) && has_charges( itype_UPS, it.ammo_required() ) ) ||
            it.ammo_sufficient() ) {
            return true;
        }

        if( show_msg ) {
            if( it.has_flag( flag_USE_UPS ) ) {
                add_msg_if_player(
                    m_info,
                    vgettext( "Your %s needs %d charge, from some UPS or a Bionic Power Interface.",
                              "Your %s needs %d charges, from some UPS or a Bionic Power Interface.",
                              it.ammo_required() ),
                    it.tname(), it.ammo_required() );
            } else {
                add_msg_if_player(
                    m_info,
                    vgettext( "Your %s needs %d charge, from a Bionic Power Interface.",
                              "Your %s needs %d charges, from a Bionic Power Interface.",
                              it.ammo_required() ),
                    it.tname(), it.ammo_required() );
            }
        }
        return false;
    }
    if( it.has_flag( flag_USE_UPS ) ) {
    if( has_charges( itype_UPS, it.ammo_required() ) || it.ammo_sufficient() ) {
            return true;
        }
        if( show_msg ) {
            add_msg_if_player( m_info,
                               vgettext( "Your %s needs %d charge from some UPS.",
                                         "Your %s needs %d charges from some UPS.",
                                         it.ammo_required() ),
                               it.tname(), it.ammo_required() );
        }
        return false;
    } else if( !it.ammo_sufficient() ) {
    if( show_msg ) {
            add_msg_if_player( m_info,
                               vgettext( "Your %s has %d charge but needs %d.",
                                         "Your %s has %d charges but needs %d.",
                                         it.ammo_remaining() ),
                               it.tname(), it.ammo_remaining(), it.ammo_required() );
        }
        return false;
    }
    return true;
}

bool Character::consume_charges( item& used, int qty )
{
    if( qty < 0 ) {
        debugmsg( "Tried to consume negative charges" );
        return false;
    }

    if( qty == 0 ) { return false; }

    // Destroy items with specific flag
    if( used.has_flag( flag_DESTROY_ON_DECHARGE ) || used.get_use( "place_monster" ) != nullptr
        || used.get_use( "place_npc" ) != nullptr ) {
        used.detach();
        return true;
    }

    if( !used.is_tool() && !used.is_food() && !used.is_medication() ) {
        debugmsg( "Tried to consume charges for non-tool, non-food, non-med item" );
        return false;
    }

    // Consume comestibles destroying them if no charges remain
    if( used.is_food() || used.is_medication() ) {
        used.charges -= qty;
        if( used.charges <= 0 ) {
            used.detach();
            return true;
        }
        return false;
    }

    if( used.is_power_armor() ) {
        if( used.charges >= qty ) {
            used.ammo_consume( qty, bub_pos() );
        } else if( character_funcs::can_interface_armor( *this )
                   && has_charges( itype_bio_armor, qty ) ) {
            use_charges( itype_bio_armor, qty );
        } else {
            use_charges( itype_UPS, qty );
        }
    }

    // USE_UPS may occur on base items and is added by the UPS tool mod
    // If an item has the flag, then it should not be consumed on use.
    if( used.has_flag( flag_USE_UPS ) ) {
        // With the new UPS system, we'll want to use any charges built up in the tool before
        // pulling from the UPS The usage of the item was already approved, so drain item if
        // possible, otherwise use UPS
        if( used.charges >= qty ) {
            used.ammo_consume( qty, bub_pos() );
        } else {
            use_charges( itype_UPS, qty );
        }
    } else {
        used.ammo_consume( std::min( qty, used.ammo_remaining() ), bub_pos() );
    }
    return false;
}

int Character::item_handling_cost( const item& it, bool penalties, int base_cost ) const
{
    int mv = base_cost;
    if( penalties ) {
        // 40 moves per liter, up to 200 at 5 liters
        mv += std::min( 200, it.volume() / 20_ml );
    }

    if( primary_weapon().typeId() == itype_e_handcuffs ) {
        mv *= 4;
    } else if( penalties && has_effect( effect_grabbed ) ) {
        mv *= 2;
    }

    // For single handed items use the least encumbered hand
    if( it.is_two_handed( *this ) ) {
        mv += encumb( body_part_hand_l ) + encumb( body_part_hand_r );
    } else {
        mv += std::min( encumb( body_part_hand_l ), encumb( body_part_hand_r ) );
    }

    return std::min( std::max( mv, 0 ), MAX_HANDLING_COST );
}

int Character::item_store_cost(
    const item& it, const item & /* container */, bool penalties, int base_cost ) const
{
    /** @EFFECT_PISTOL decreases time taken to store a pistol */
    /** @EFFECT_SMG decreases time taken to store an SMG */
    /** @EFFECT_RIFLE decreases time taken to store a rifle */
    /** @EFFECT_SHOTGUN decreases time taken to store a shotgun */
    /** @EFFECT_LAUNCHER decreases time taken to store a launcher */
    /** @EFFECT_STABBING decreases time taken to store a stabbing weapon */
    /** @EFFECT_CUTTING decreases time taken to store a cutting weapon */
    /** @EFFECT_BASHING decreases time taken to store a bashing weapon */
    int lvl = get_skill_level( it.is_gun() ? it.gun_skill() : it.melee_skill() );
    return item_handling_cost( it, penalties, base_cost ) / ( ( lvl + 10.0f ) / 10.0f );
}

int Character::item_wear_cost( const item& it ) const
{
    double mv = item_handling_cost( it );

    switch( it.get_layer() ) {
        case PERSONAL_LAYER:
            break;

        case UNDERWEAR_LAYER:
            mv *= 1.5;
            break;

        case REGULAR_LAYER:
            break;

        case WAIST_LAYER:
        case OUTER_LAYER:
            mv /= 1.5;
            break;

        case BELTED_LAYER:
            mv /= 2.0;
            break;

        case AURA_LAYER:
            break;

        default:
            break;
    }

    mv *= std::max( it.get_avg_encumber( *this ) / 10.0, 1.0 );

    return mv;
}

void Character::cough( bool harmful, int loudness )
{
    if( has_effect( effect_cough_suppress ) ) { return; }

    if( harmful ) {
        const int stam = get_stamina();
        const int malus = get_stamina_max() * 0.05; // 5% max stamina
        mod_stamina( -malus, false );
        if( stam < malus && x_in_y( malus - stam, malus ) && one_in( 6 ) ) {
            apply_damage( nullptr, bodypart_id( "torso" ), 1 );
        }
        // Asthmatic characters gain increased risk of an asthma attack from smoke and other
        // dangerous respiratory effects.
        if( has_trait( trait_ASTHMA ) ) { add_effect( effect_cough_aggravated_asthma, 1_minutes ); }
    }

    if( !is_npc() ) { add_msg( m_bad, _( "You cough heavily." ) ); }
    sounds::sound( bub_pos(), loudness, sounds::sound_t::speech, _( "a hacking cough." ), false,
                   "misc", "cough" );

    moves -= 80;

    add_effect( effect_recently_coughed, 5_minutes );
}

void Character::wake_up()
{
    remove_effect( effect_slept_through_alarm );
    remove_effect( effect_lying_down );
    remove_effect( effect_alarm_clock );
    if( has_effect( effect_sleep ) ) {
        g->events().send<event_type::character_wakes_up>( getID() );
        remove_effect( effect_sleep );
        // Wake up might be called more than once per turn, but we only need to recalc after
        // removing sleep
        recalc_sight_limits();
    }
}

int Character::get_shout_volume() const
{
    int base = 10;
    int shout_multiplier = 2;

    // Mutations make shouting louder, they also define the default message
    if( has_trait( trait_SHOUT3 ) ) {
        shout_multiplier = 4;
        base = 20;
    } else if( has_trait( trait_SHOUT2 ) ) {
        base = 15;
        shout_multiplier = 3;
    }

    // You can't shout without your face
    if( has_trait( trait_PROF_FOODP )
        && !( is_wearing( itype_id( "foodperson_mask" ) )
              || is_wearing( itype_id( "foodperson_mask_on" ) ) ) ) {
        base = 0;
        shout_multiplier = 0;
    }

    // Masks and such dampen the sound
    // Balanced around whisper for wearing bondage mask
    // and noise ~= 10 (door smashing) for wearing dust mask for character with strength = 8
    /** @EFFECT_STR increases shouting volume */
    const int penalty = encumb( body_part_mouth ) * 3 / 2;
    int noise = base + str_cur * shout_multiplier - penalty;

    // Minimum noise volume possible after all reductions.
    // Volume 1 can't be heard even by player
    constexpr int minimum_noise = 2;

    if( noise <= base ) { noise = std::max( minimum_noise, noise ); }

    // Screaming underwater is not good for oxygen and harder to do overall
    if( is_underwater() ) { noise = std::max( minimum_noise, noise / 2 ); }
    return noise;
}

void Character::shout( std::string msg, bool order )
{
    int base = 10;
    std::string shout;

    // You can't shout without your face
    if( has_trait( trait_PROF_FOODP )
        && !( is_wearing( itype_id( "foodperson_mask" ) )
              || is_wearing( itype_id( "foodperson_mask_on" ) ) ) ) {
        add_msg_if_player( m_warning, _( "You try to shout but you have no face!" ) );
        return;
    }

    // Mutations make shouting louder, they also define the default message
    if( msg.empty() ) {
        if( has_trait( trait_SHOUT3 ) ) {
            base = 20;
            add_msg_if_player( m_warning, _( "You let out an ear-piercing howl!" ) );
            msg = is_player()
                  ? _( "yourself let out an ear-piercing howl!" )
                  : _( "an ear-piercing howl!" );
            shout = "howl";
        } else if( has_trait( trait_SHOUT2 ) ) {
            base = 15;
            add_msg_if_player( m_mixed, _( "You scream loudly!" ) );
            msg = is_player() ? _( "yourself scream loudly!" ) : _( "a loud scream!" );
            shout = "scream";
        } else {
            add_msg_if_player( m_info, _( "You yell loudly!" ) );
            msg = is_player() ? _( "yourself shout loudly!" ) : _( "a loud shout!" );
            shout = "default";
        }
    } else {
        add_msg_if_player( m_info, _( string_format( "You yell \"%s\"", msg ) ) );
        msg = is_player()
              ? _( string_format( "yourself yell \"%s\"", msg ) )
              : _( string_format( "yell \"%s\"", msg ) );
    }


    int noise = get_shout_volume();

    // Minimum noise volume possible after all reductions.
    // Volume 1 can't be heard even by player
    constexpr int minimum_noise = 2;

    if( noise <= base ) {
        std::string dampened_shout;
        std::ranges::transform( msg, std::back_inserter( dampened_shout ), tolower );
        msg = std::move( dampened_shout );
    }

    // Screaming underwater is not good for oxygen and harder to do overall
    if( is_underwater() ) {
        if( !has_trait( trait_GILLS ) && !has_trait( trait_GILLS_CEPH ) ) { mod_stat( "oxygen", -noise ); }
    }

    const int penalty = encumb( body_part_mouth ) * 3 / 2;
    // TODO: indistinct noise descriptions should be handled in the sounds code
    if( noise <= minimum_noise ) {
        add_msg_if_player( m_warning, _( "The sound of your voice is almost completely muffled!" ) );
        msg = is_player() ? _( "your muffled shout" ) : _( "an indistinct voice" );
    } else if( noise * 2 <= noise + penalty ) {
        // The shout's volume is 1/2 or lower of what it would be without the penalty
        add_msg_if_player( m_warning, _( "The sound of your voice is significantly muffled!" ) );
    }

    sounds::sound( bub_pos(), noise, order ? sounds::sound_t::order : sounds::sound_t::alert, msg,
                   false, "shout", shout );
}

void Character::signal_nemesis()
{
    const tripoint_abs_omt ompos = abs_omt_pos();
    const tripoint_abs_sm smpos = project_to<coords::sm>( ompos );
    get_overmapbuffer( get_dimension() ).signal_nemesis( smpos );
}

void Character::vomit()
{
    g->events().send<event_type::throws_up>( getID() );

    map& here = get_map();
    if( get_effect_int( effect_fungus ) >= 3 ) {
        add_msg_player_or_npc(
            m_bad, _( "You vomit thousands of live spores!" ),
            _( "<npcname> vomits thousands of live spores!" ) );
        fungal_effects( *g, here ).fungalize( bub_pos(), this );
    } else if( stomach.get_calories() > 0 || get_thirst() < 0 ) {
        add_msg_player_or_npc( m_bad, _( "You throw up heavily!" ), _( "<npcname> throws up heavily!" ) );
        here.add_field(
            tripoint_bub_ms( character_funcs::pick_safe_adjacent_tile( *this ).value_or( bub_pos() ) ),
            fd_bile, 1 );
    } else {
        return;
    }

    if( !has_effect( effect_nausea ) ) { // Prevents never-ending nausea
        const effect dummy_nausea(
            &effect_nausea.obj(), 0_turns, bodypart_str_id::NULL_ID(), 1, calendar::turn );
        add_effect( effect_nausea,
                    std::max( dummy_nausea.get_max_duration() * stomach.get_calories() / 100,
                              dummy_nausea.get_int_dur_factor() ) );
    }

    stomach.empty();
    set_thirst( std::max( 0, get_thirst() ) );
    remove_effect( effect_bloated );
    if( get_healthy_mod() > 0 ) { set_healthy_mod( 0 ); }

    moves -= 100;
    // get_effect is more correct than has_effect because of body parts
    effect& eff_foodpoison = get_effect( effect_foodpoison );
    if( eff_foodpoison ) { eff_foodpoison.mod_duration( -30_minutes ); }
    effect& eff_drunk = get_effect( effect_drunk );
    if( eff_drunk ) { eff_drunk.mod_duration( rng( -10_minutes, -50_minutes ) ); }
    remove_effect( effect_pkill1 );
    remove_effect( effect_pkill2 );
    remove_effect( effect_pkill3 );
    // Don't wake up when just retching
    wake_up();
}

void Character::set_fac_id( const std::string& my_fac_id ) { fac_id = faction_id( my_fac_id ); }

std::string get_stat_name( character_stat Stat )
{
    switch( Stat ) {
            // *INDENT-OFF*
        case character_stat::STRENGTH:
            return pgettext("strength stat", "STR");
        case character_stat::DEXTERITY:
            return pgettext("dexterity stat", "DEX");
        case character_stat::INTELLIGENCE:
            return pgettext("intelligence stat", "INT");
        case character_stat::PERCEPTION:
            return pgettext("perception stat", "PER");
        // *INDENT-ON*
        default:
            return pgettext( "fake stat there's an error", "ERR" );
            break;
    }
    return pgettext( "fake stat there's an error", "ERR" );
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

/// Returns the mutation category with the highest strength
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

bool Character::wearing_something_on( const bodypart_id &bp ) const
{
for( auto &i : worn ) {
    if( i->covers( bp ) ) {
            return true;
        }
    }
    return false;
}

bool Character::is_wearing_shoes( const side& which_side ) const
{
    bool left = true;
    bool right = true;
    if( which_side == side::LEFT || which_side == side::BOTH ) {
        left = false;
        for( const item * const& worn_item : worn ) {
            if( worn_item->covers( bodypart_id( "foot_l" ) ) && !worn_item->has_flag( flag_BELTED )
                && !worn_item->has_flag( flag_PERSONAL ) && !worn_item->has_flag( flag_AURA )
                && !worn_item->has_flag( flag_SEMITANGIBLE )
                && !worn_item->has_flag( flag_SKINTIGHT ) ) {
                left = true;
                break;
            }
        }
    }
    if( which_side == side::RIGHT || which_side == side::BOTH ) {
        right = false;
        for( const item * const& worn_item : worn ) {
            if( worn_item->covers( bodypart_id( "foot_r" ) ) && !worn_item->has_flag( flag_BELTED )
                && !worn_item->has_flag( flag_PERSONAL ) && !worn_item->has_flag( flag_AURA )
                && !worn_item->has_flag( flag_SEMITANGIBLE )
                && !worn_item->has_flag( flag_SKINTIGHT ) ) {
                right = true;
                break;
            }
        }
    }
    return ( left && right );
}

bool Character::is_wearing_helmet() const
{
for( const item * const &i : worn ) {
    if( i->covers( bodypart_id( "head" ) ) && !i->has_flag( flag_HELMET_COMPAT ) &&
            !i->has_flag( flag_SKINTIGHT ) &&
            !i->has_flag( flag_PERSONAL ) && !i->has_flag( flag_AURA ) && !i->has_flag( flag_SEMITANGIBLE ) &&
            !i->has_flag( flag_OVERSIZE ) ) {
            return true;
        }
    }
    return false;
}

int Character::head_cloth_encumbrance() const
{
    int ret = 0;
    for( auto& i : worn ) {
        if( i->covers( bodypart_id( "head" ) ) && !i->has_flag( flag_SEMITANGIBLE )
            && ( i->has_flag( flag_HELMET_COMPAT ) || i->has_flag( flag_SKINTIGHT ) ) ) {
            ret += i->get_encumber( *this, bodypart_id( "head" ) );
        }
    }
    return ret;
}

double Character::armwear_factor() const
{
    double ret = 0;
    if( wearing_something_on( bodypart_id( "arm_l" ) ) ) { ret += .5; }
    if( wearing_something_on( bodypart_id( "arm_r" ) ) ) { ret += .5; }
    return ret;
}

double Character::footwear_factor() const
{
    double ret = 0;
    if( wearing_something_on( bodypart_id( "foot_l" ) ) ) { ret += .5; }
    if( wearing_something_on( bodypart_id( "foot_r" ) ) ) { ret += .5; }
    return ret;
}

int Character::shoe_type_count( const itype_id& it ) const
{
    int ret = 0;
    if( is_wearing_on_bp( it, bodypart_id( "foot_l" ) ) ) { ret++; }
    if( is_wearing_on_bp( it, bodypart_id( "foot_r" ) ) ) { ret++; }
    return ret;
}

std::vector<item *> Character::inv_dump()
{
    std::vector<item *> ret;
    if( is_armed() && can_unwield( primary_weapon() ).success() ) { ret.push_back( &primary_weapon() ); }
    for( auto& i : worn ) { ret.push_back( i ); }
    inv.dump( ret );
    return ret;
}

std::vector<detached_ptr<item>> Character::inv_dump_remove()
{
    std::vector<detached_ptr<item>> ret;
    if( is_armed() && can_unwield( primary_weapon() ).success() ) {
        ret.push_back( remove_primary_weapon() );
    }
    for( auto it = worn.begin(); it != worn.end(); ) {
        detached_ptr<item> t;
        it = worn.erase( it, &t );
        ret.push_back( std::move( t ) );
    }
    inv.dump_remove( ret );
    return ret;
}

bool Character::covered_with_flag( const flag_id &flag, const body_part_set &parts ) const
{
    if( parts.none() ) {
    return true;
}

body_part_set to_cover( parts );

for( const auto &elem : worn ) {
    if( !elem->has_flag( flag ) ) {
            continue;
        }

        to_cover.substract_set( elem->get_covered_body_parts() );

        if( to_cover.none() ) {
            return true; // Allows early exit.
        }
    }

    return to_cover.none();
}

bool Character::is_waterproof( const body_part_set& parts ) const
{
    return covered_with_flag( flag_WATERPROOF, parts );
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

void Character::set_knows_creature_type( const mtype_id& c ) { known_monsters.emplace( c ); }

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

void Character::fall_asleep()
{
    // Communicate to the player that he is using items on the floor
    std::string item_name = is_snuggling();
    if( item_name == "many" ) {
        if( one_in( 15 ) ) {
            add_msg_if_player( _( "You nestle your pile of clothes for warmth." ) );
        } else {
            add_msg_if_player( _( "You use your pile of clothes for warmth." ) );
        }
    } else if( item_name != "nothing" ) {
        if( one_in( 15 ) ) {
            add_msg_if_player( _( "You snuggle your %s to keep warm." ), item_name );
        } else {
            add_msg_if_player( _( "You use your %s to keep warm." ), item_name );
        }
    }
    if( has_active_mutation( trait_HIBERNATE ) ) {
        if( get_stored_kcal() > max_stored_kcal() * 0.9 && get_thirst() < thirst_levels::thirsty ) {
            if( is_avatar() ) {
                g->memorial()
                .add( pgettext( "memorial_male", "Entered hibernation." ),
                      pgettext( "memorial_female", "Entered hibernation." ) );
            }

            add_msg_if_player( _( "You enter hibernation." ) );
            fall_asleep( 7_days );
        } else {
            add_msg_if_player(
                m_bad,
                _( "You need to be nearly full of food and water to enter "
                   "hibernation." ) );
        }
    }

    fall_asleep( 10_hours ); // default max sleep time.
}

void Character::fall_asleep( const time_duration& duration )
{
    if( activity ) {
        if( activity->id() == ACT_TRY_SLEEP ) {
            activity->set_to_null();
        } else {
            cancel_activity();
        }
    }
    add_effect( effect_sleep, duration );
}

bool Character::in_sleep_state() const
{
    return Creature::in_sleep_state() || activity->id() == ACT_TRY_SLEEP;
}

std::string Character::is_snuggling() const
{
    map& here = get_map();
    auto begin = here.i_at( bub_pos() ).begin();
    auto end = here.i_at( bub_pos() ).end();

    if( in_vehicle ) {
        if( const std::optional<vpart_reference> vp = here.veh_at( bub_pos() ).part_with_feature(
                VPFLAG_CARGO,
                false ) ) {
            vehicle *const veh = &vp->vehicle();
            const int cargo = vp->part_index();
            if( !veh->get_items( cargo ).empty() ) {
                begin = veh->get_items( cargo ).begin();
                end = veh->get_items( cargo ).end();
            }
        }
    }
    const item* floor_armor = nullptr;
    int ticker = 0;

    // If there are no items on the floor, return nothing
    if( begin == end ) { return "nothing"; }

    for( auto candidate = begin; candidate != end; ++candidate ) {
        if( !( *candidate )->is_armor() ) {
            continue;
        } else if(
            ( *candidate )->volume() > 250_ml && ( *candidate )->get_warmth() > 0
            && ( ( *candidate )->covers( bodypart_id( "torso" ) )
                 || ( *candidate )->covers( bodypart_id( "leg_l" ) )
                 || ( *candidate )->covers( bodypart_id( "leg_r" ) ) ) ) {
            floor_armor = *candidate;
            ticker++;
        }
    }

    if( ticker == 0 ) {
        return "nothing";
    } else if( ticker == 1 ) {
        return floor_armor->type_name();
    } else if( ticker > 1 ) {
        return "many";
    }

    return "nothing";
}

std::map<bodypart_id, int> Character::warmth(
    const std::map<bodypart_id, std::vector<const item *>> &clothing_map ) const
{
    std::map<bodypart_id, int> ret;
    std::map<bodypart_id, float> wetness_map;
    for( const std::pair<const bodypart_str_id, bodypart> &elem : get_body() ) {
        ret.emplace( elem.first.id(), 0 );
        wetness_map.emplace(
            elem.first.id(),
            static_cast<float>( elem.second.get_wetness() ) / elem.second.get_drench_capacity() );
    }

    for( const std::pair<const bodypart_id, std::vector<const item * >> &on_bp : clothing_map ) {
        const bodypart_id& bp = on_bp.first;
        for( const item * it : on_bp.second ) {
            double warmth = it->get_warmth();
            // Warmth reduced linearly with wetness
            const auto& materials = it->made_of();
            float max_wet_resistance = std::accumulate(
            materials.begin(), materials.end(), 0.0f, []( float best, const material_id & mat ) {
                return std::max( best, mat->warmth_when_wet() );
            } );
            float wet_mult = 1.0f - max_wet_resistance * wetness_map[bp];
            ret[bp] += warmth * wet_mult;
        }
        ret[bp] += get_effect_int( effect_heating_bionic, bp.id() );
    }
    return ret;
}

namespace warmth
{

template <typename Acc = int const&( int const &, int const & )>
static std::map<bodypart_id, int> acc_clothing_warmth(
    const std::map<bodypart_id, std::vector<const item *>> &clothing_map,
    Acc accumulation_function )
{
    std::map<bodypart_id, int> ret;
    for( const std::pair<const bodypart_id, std::vector<const item * >> &pr : clothing_map ) {
        ret[pr.first] = std::accumulate(
                            pr.second.begin(), pr.second.end(), 0,
        [accumulation_function]( int acc, const item * it ) {
            return accumulation_function( acc, it->get_warmth() );
        } );
    }

    return ret;
}

std::map<bodypart_id, int> from_clothing(
    const std::map<bodypart_id, std::vector<const item *>> &clothing_map )
{
    return acc_clothing_warmth( clothing_map, std::plus<int>() );
}

std::map<bodypart_id, int> bonus_from_clothing(
    const std::map<bodypart_id, std::vector<const item *>> &clothing_map )
{
    return acc_clothing_warmth( clothing_map, std::max<int> );
}

std::map<bodypart_id, int> from_effects( const Character& c )
{
    std::map<bodypart_id, int> ret;
    for( const effect * e : c.get_all_effects_of_type( effect_heating_bionic ) ) {
        ret[e->get_bp()] += e->get_intensity();
    }
    return ret;
}

} // namespace warmth

bool Character::can_use_floor_warmth() const
{
    static const auto allowed_activities = std::vector<activity_id> {
        activity_id( "ACT_WAIT" ),
        activity_id( "ACT_WAIT_NPC" ),
        activity_id( "ACT_WAIT_STAMINA" ),
        activity_id( "ACT_AUTODRIVE" ),
        activity_id( "ACT_READ" ),
        activity_id( "ACT_SOCIALIZE" ),
        activity_id( "ACT_MEDITATE" ),
        activity_id( "ACT_FISH" ),
        activity_id( "ACT_GAME" ),
        activity_id( "ACT_HAND_CRANK" ),
        activity_id( "ACT_HEATING" ),
        activity_id( "ACT_VIBE" ),
        activity_id( "ACT_TRY_SLEEP" ),
        activity_id( "ACT_OPERATION" ),
        activity_id( "ACT_TREE_COMMUNION" ),
        activity_id( "ACT_EAT_MENU" ),
        activity_id( "ACT_CONSUME_FOOD_MENU" ),
        activity_id( "ACT_CONSUME_DRINK_MENU" ),
        activity_id( "ACT_CONSUME_MEDS_MENU" ),
        activity_id( "ACT_STUDY_SPELL" ),
    };

    return in_sleep_state() || has_activity( allowed_activities );
}

int Character::floor_bedding_warmth( const tripoint_bub_ms& pos )
{
    map& here = get_map();
    const trap& trap_at_pos = here.tr_at( pos );
    const ter_id ter_at_pos = here.ter( pos );
    const furn_id furn_at_pos = here.furn( pos );
    int floor_bedding_warmth = 0;

    const optional_vpart_position vp = here.veh_at( pos );
    const std::optional<vpart_reference> boardable = vp.part_with_feature( "BOARDABLE", true );
    // Search the floor for bedding
    if( furn_at_pos != f_null ) {
        floor_bedding_warmth += furn_at_pos.obj().floor_bedding_warmth;
    } else if( !trap_at_pos.is_null() ) {
        floor_bedding_warmth += trap_at_pos.floor_bedding_warmth;
    } else if( boardable ) {
        floor_bedding_warmth += boardable->info().floor_bedding_warmth;
    } else if( ter_at_pos == t_improvised_shelter ) {
        floor_bedding_warmth -= 500;
    } else {
        floor_bedding_warmth -= 2000;
    }

    return floor_bedding_warmth;
}

int Character::floor_item_warmth( const tripoint_bub_ms& pos )
{
    int item_warmth = 0;

    const auto warm = [&item_warmth]( const auto & stack ) {
        for( const item * const& elem : stack ) {
            if( !elem->is_armor() ) { continue; }
            // Items that are big enough and covers the torso are used to keep warm.
            // Smaller items don't do as good a job
            if( elem->volume() > 250_ml
                && ( elem->covers( bodypart_id( "torso" ) ) || elem->covers( bodypart_id( "leg_l" ) )
                     || elem->covers( bodypart_id( "leg_r" ) ) ) ) {
                item_warmth += 60 * elem->get_warmth() * elem->volume() / 2500_ml;
            }
        }
    };

    map &here = get_map();
    if( !!here.veh_at( pos ) ) {
        if( const std::optional<vpart_reference> vp = here.veh_at( pos ).part_with_feature( VPFLAG_CARGO,
            false ) ) {
            vehicle *const veh = &vp->vehicle();
            const int cargo = vp->part_index();
            vehicle_stack vehicle_items = veh->get_items( cargo );
            warm( vehicle_items );
        }
        return item_warmth;
    }
    map_stack floor_items = here.i_at( pos );
    warm( floor_items );
    return item_warmth;
}

int Character::floor_warmth( const tripoint_bub_ms& pos ) const
{
    const int item_warmth = floor_item_warmth( pos );
    int bedding_warmth = floor_bedding_warmth( pos );

    // If the PC has fur, etc, that will apply too
    int floor_mut_warmth = bodytemp_modifier_traits_floor();
    // DOWN does not provide floor insulation, though.
    // Better-than-light fur or being in one's shell does.
    if( ( !( has_trait( trait_DOWN ) ) ) && ( floor_mut_warmth >= 200 ) ) {
        bedding_warmth = std::max( 0, bedding_warmth );
    }
    return ( item_warmth + bedding_warmth + floor_mut_warmth );
}

int Character::bodytemp_modifier_traits( bool overheated ) const
{
    int mod = 0;
    for( const trait_id& iter : get_mutations() ) {
        mod += overheated ? iter->bodytemp_min : iter->bodytemp_max;
    }
    return mod;
}

int Character::bodytemp_modifier_traits_floor() const
{
    int mod = 0;
    for( const trait_id& iter : get_mutations() ) { mod += iter->bodytemp_sleep; }
    return mod;
}

int Character::temp_corrected_by_climate_control( int temperature ) const
{
    const int variation = int( BODYTEMP_NORM * 0.5 );
    if( temperature < BODYTEMP_SCORCHING + variation
        && temperature > BODYTEMP_FREEZING - variation ) {
        if( temperature > BODYTEMP_SCORCHING ) {
            temperature = BODYTEMP_VERY_HOT;
        } else if( temperature > BODYTEMP_VERY_HOT ) {
            temperature = BODYTEMP_HOT;
        } else if( temperature > BODYTEMP_HOT ) {
            temperature = BODYTEMP_NORM;
        } else if( temperature < BODYTEMP_FREEZING ) {
            temperature = BODYTEMP_VERY_COLD;
        } else if( temperature < BODYTEMP_VERY_COLD ) {
            temperature = BODYTEMP_COLD;
        } else if( temperature < BODYTEMP_COLD ) {
            temperature = BODYTEMP_NORM;
        }
    }
    return temperature;
}

const item *Character::get_item_with_id( const itype_id& item_id, bool need_charges ) const
{
    const item* ret = nullptr;

    inv.visit_items( [&ret, &item_id, &need_charges]( const item * it ) {
        if( it->typeId() == item_id ) {
            if( it->is_tool() && need_charges ) {
                if( it->type->tool->max_charges && it->charges <= 0 ) { return VisitResponse::SKIP; }
            }
            ret = it;
            return VisitResponse::ABORT;
        }
        return VisitResponse::NEXT;
    } );

    return ret;
}

item &Character::add_item_with_id( const itype_id& item_id, int count )
{
    detached_ptr<item> new_item = item::spawn( item_id, calendar::turn, count );
    return i_add( std::move( new_item ), true );
}

bool Character::has_item_with_id( const itype_id& item_id, bool need_charges ) const
{
    return has_item_with( [&item_id, &need_charges]( const item & it ) {
        if( it.is_tool() && need_charges ) {
            return it.typeId() == item_id && it.type->tool->max_charges
                   ? it.charges > 0
                   : it.typeId() == item_id;
        }
        return it.typeId() == item_id;
    } );
}

std::vector<item *> Character::all_items_with_id( const itype_id& item_id,
        bool need_charges ) const
{
    return items_with( [&item_id, &need_charges]( const item & it ) {
        if( it.is_tool() && need_charges ) {
            return it.typeId() == item_id && it.type->tool->max_charges
                   ? it.charges > 0
                   : it.typeId() == item_id;
        }
        return it.typeId() == item_id;
    } );
}

bool Character::has_item_with_flag( const flag_id& flag, bool need_charges ) const
{
    return has_item_with( [&flag, &need_charges]( const item & it ) {
        if( it.is_tool() && need_charges ) {
            return it.has_flag( flag ) && it.type->tool->max_charges
                   ? it.charges > 0
                   : it.has_flag( flag );
        }
        return it.has_flag( flag );
    } );
}

std::vector<item *> Character::all_items_with_flag( const flag_id& flag, bool need_charges ) const
{
    return items_with( [&flag, &need_charges]( const item & it ) {
        if( it.is_tool() && need_charges ) {
            return it.has_flag( flag ) && it.type->tool->max_charges
                   ? it.charges > 0
                   : it.has_flag( flag );
        }
        return it.has_flag( flag );
    } );
}

std::vector<item *> Character::all_items( bool need_charges ) const
{
    return items_with( [&need_charges]( const item & it ) {
        if( it.is_tool() && need_charges ) {
            return it.type->tool->max_charges ? it.charges > 0 : true;
        }
        return true;
    } );
}

bool Character::has_charges(
    const itype_id& it, int quantity, const std::function<bool( const item & )> &filter ) const
{
    if( it == itype_fire || it == itype_apparatus ) { return has_fire( quantity ); }
    if( it == itype_UPS && is_mounted() && mounted_creature.get()->has_flag( MF_RIDEABLE_MECH ) ) {
        auto mons = mounted_creature.get();
        return quantity <= mons->get_battery_item()->ammo_remaining();
    }
    if( it == itype_bio_armor ) {
        int mod_qty = 0;
        float efficiency = 1;
        for( const bionic& bio : get_bionic_collection() ) {
            if( bio.powered && bio.info().has_flag( flag_BIONIC_ARMOR_INTERFACE ) ) {
                efficiency = std::max( efficiency, bio.info().fuel_efficiency );
            }
        }
        if( efficiency == 1 ) {
            debugmsg( "Player lacks a bionic armor interface with fuel efficiency field." );
        }
        mod_qty = quantity / efficiency;
        return ( has_power() && get_power_level() >= units::from_kilojoule( mod_qty ) );
    }
    return charges_of( it, quantity, filter ) == quantity;
}

std::vector<detached_ptr<item>> Character::use_amount( itype_id it, int quantity,
        const std::function<bool( const item & )> &filter )
{
    std::vector<detached_ptr<item>> ret;

    remove_items_with( [&ret, &quantity, &it, filter]( detached_ptr<item>&& a ) {
        if( quantity > 0 && a->typeId() == it && filter( *a ) ) {
            ret.push_back( std::move( a ) );
            quantity--;
            return VisitResponse::SKIP;
        }
        return VisitResponse::NEXT;
    } );

    return ret;
}

bool Character::use_charges_if_avail( const itype_id& it, int quantity )
{
    if( has_charges( it, quantity ) ) {
        use_charges( it, quantity );
        return true;
    }
    return false;
}

std::vector<detached_ptr<item>> Character::use_charges( const itype_id &what, int qty,
        const std::function<bool( const item & )> &filter )
{
    std::vector<detached_ptr<item>> res;
    if( qty <= 0 ) {
        return res;

    } else if( what == itype_voltmeter_bionic ) {
        mod_power_level( units::from_kilojoule( -qty ) );
        return res;

    } else if( what == itype_toolset ) {
        mod_power_level( units::from_kilojoule( -qty ) );
        return res;

    } else if( what == itype_fire ) {
        use_fire( qty );
        return res;

    } else if( what == itype_bio_armor ) {
        float mod_qty = 0;
        float efficiency = 1;
        for( const bionic& bio : get_bionic_collection() ) {
            if( bio.powered && bio.info().has_flag( flag_BIONIC_ARMOR_INTERFACE ) ) {
                efficiency = std::max( efficiency, bio.info().fuel_efficiency );
            }
        }
        if( efficiency == 1 ) {
            debugmsg( "Player lacks a bionic armor interface with fuel efficiency field." );
        }
        mod_qty = qty / efficiency;
        mod_power_level( units::from_kilojoule( -mod_qty ) );
        return res;

    } else if( what == itype_UPS ) {
        if( is_mounted() && mounted_creature.get()->has_flag( MF_RIDEABLE_MECH )
            && mounted_creature.get()->get_battery_item() ) {
            auto mons = mounted_creature.get();
            int power_drain = std::min( mons->get_battery_item()->ammo_remaining(), qty );
            mons->use_mech_power( -power_drain );
            qty -= std::min( qty, power_drain );
            return res;
        }
        if( has_power() && has_active_bionic( bio_ups ) ) {
            int bio = std::min( units::to_kilojoule( get_power_level() ), qty );
            mod_power_level( units::from_kilojoule( -bio ) );
            qty -= std::min( qty, bio );
        }

        remove_items_with( [&]( detached_ptr<item>&& e ) {
            if( e->has_flag( flag_IS_UPS ) && e->ammo_remaining() > 0 ) {
                int ups_eff_mult = e->type->tool->ups_eff_mult;
                detached_ptr<item> split = item::spawn( *e );
                split->ammo_set( e->ammo_current(), e->ammo_remaining() );
                int used = std::min( qty, e->ammo_remaining() * ups_eff_mult );
                qty -= used;
                int rand_increase = x_in_y( used % ups_eff_mult, ups_eff_mult );
                int really_used = ( used / ups_eff_mult ) + rand_increase;
                e->ammo_consume( really_used, bub_pos() );
                res.push_back( std::move( split ) );
            }
            return qty != 0 ? VisitResponse::NEXT : VisitResponse::ABORT;
        } );

        return res;
    }


    bool has_tool_with_UPS = false;
    const auto p = bub_pos();
    remove_items_with( [&qty, filter, &has_tool_with_UPS, &what, &res, &p]( detached_ptr<item>&& e ) {
        if( qty == 0 ) {
            // found sufficient charges
            return VisitResponse::ABORT;
        }
        if( !filter( *e ) ) { return VisitResponse::NEXT; }
        if( e->typeId() == what && e->has_flag( flag_USE_UPS ) ) { has_tool_with_UPS = true; }
        if( e->is_tool() ) {
            if( e->typeId() == what ) {
                int n = std::min( e->ammo_remaining(), qty );
                qty -= n;

                if( n == e->ammo_remaining() ) {
                    res.push_back( item::spawn( *e ) );
                    e->ammo_consume( n, p );
                } else {
                    detached_ptr<item> split = item::spawn( *e );
                    split->ammo_set( e->ammo_current(), n );
                    e->ammo_consume( n, p );
                    res.push_back( std::move( split ) );
                }
            }
            return VisitResponse::SKIP;

        } else if( e->count_by_charges() ) {
            if( e->typeId() == what ) {
                if( e->charges > qty ) {
                    e->charges -= qty;
                    detached_ptr<item> split = item::spawn( *e );
                    split->charges = qty;
                    res.push_back( std::move( split ) );
                    qty = 0;
                    return VisitResponse::ABORT;
                } else {
                    qty -= e->charges;
                    res.push_back( std::move( e ) );
                }
            }
            // items counted by charges are not themselves expected to be containers
            return VisitResponse::SKIP;
        }

        // recurse through any nested containers
        return VisitResponse::NEXT;
    } );

    if( has_tool_with_UPS ) {
        std::vector<detached_ptr<item>> found = use_charges( itype_UPS, qty );
        res.insert( res.end(), std::make_move_iterator( found.begin() ),
                    std::make_move_iterator( found.end() ) );
    }

    return res;
}

bool Character::has_fire( const int quantity ) const
{
    // TODO: Replace this with a "tool produces fire" flag.

    if( get_map().has_nearby_fire( bub_pos() ) ) {
    return true;
} else if( has_item_with_flag( flag_FIRE ) ) {
    return true;
} else if( has_item_with_flag( flag_FIRESTARTER ) ) {
    auto firestarters = all_items_with_flag( flag_FIRESTARTER );
        for( auto &i : firestarters ) {
            if( !i->type->can_have_charges() ) {
                const use_function *usef = i->type->get_use( "firestarter" );
                if( !usef ) {
                    debugmsg( "failed to get use func 'firestarter' for item '%s'", i->typeId().c_str() );
                    continue;
                }
                const firestarter_actor* actor = dynamic_cast<const firestarter_actor *>(
                                                     usef->get_actor_ptr() );
                if( actor->can_use( *this->as_character(), *i, false, tripoint_bub_ms::zero() )
                    .success() ) {
                    return true;
                }
            } else if( has_charges( i->typeId(), quantity ) ) {
                return true;
            }
        }
    } else if( has_active_bionic( bio_tools ) && get_power_level() >= quantity * 5_kJ ) {
    return true;
} else if( has_bionic( bio_lighter ) &&
               get_power_level() >= quantity * bio_lighter->power_activate ) {
    return true;
} else if( has_bionic( bio_laser ) &&
               get_power_level() >= quantity * bio_laser->power_activate ) {
    return true;
} else if( is_npc() ) {
    // HACK: A hack to make NPCs use their Molotovs
    return true;
}
return false;
}

void Character::mod_painkiller( int npkill ) { set_painkiller( pkill + npkill ); }

void Character::set_painkiller( int npkill )
{
    npkill = std::max( npkill, 0 );
    if( pkill != npkill ) {
        const int prev_pain = get_perceived_pain();
        pkill = npkill;
        on_stat_change( "pkill", pkill );
        const int cur_pain = get_perceived_pain();

        if( cur_pain != prev_pain ) {
            react_to_felt_pain( cur_pain - prev_pain );
            on_stat_change( "perceived_pain", cur_pain );
        }
    }
}

int Character::get_painkiller() const { return pkill; }

void Character::use_fire( const int quantity )
{
    // Okay, so checks for nearby fires first,
    // then held lit torch or candle, bionic tool/lighter/laser
    // tries to use 1 charge of lighters, matches, flame throwers
    // If there is enough power, will use power of one activation of the bio_lighter, bio_tools and
    // bio_laser
    //  (home made, military), hotplate, welder in that order.
    //  bio_lighter, bio_laser, bio_tools, has_active_bionic("bio_tools"

    if( get_map().has_nearby_fire( bub_pos() ) ) {
        return;
    } else if( has_item_with_flag( flag_FIRE ) ) {
        return;
    } else if( has_item_with_flag( flag_FIRESTARTER ) ) {
        auto firestarters = all_items_with_flag( flag_FIRESTARTER );
        for( auto& i : firestarters ) {
            if( has_charges( i->typeId(), quantity ) ) {
                use_charges( i->typeId(), quantity );
                return;
            }
        }
    } else if( has_active_bionic( bio_tools ) && get_power_level() >= quantity * 5_kJ ) {
        mod_power_level( -quantity * 5_kJ );
        return;
    } else if( has_bionic( bio_lighter )
               && get_power_level() >= quantity * bio_lighter->power_activate ) {
        mod_power_level( -quantity * bio_lighter->power_activate );
        return;
    } else if( has_bionic( bio_laser ) && get_power_level() >= quantity * bio_laser->power_activate ) {
        mod_power_level( -quantity * bio_laser->power_activate );
        return;
    }
}


void Character::on_item_wear( item& it )
{
    for( const trait_id& mut : it.mutations_from_wearing( *this ) ) {
        mutation_effect( mut );
        recalc_sight_limits();
        calc_encumbrance();

        // If the stamina is higher than the max (Languorous), set it back to max
        if( get_stamina() > get_stamina_max() ) { set_stamina( get_stamina_max() ); }
    }
    morale->on_item_wear( it );
    if( it.type->iwearable_callbacks ) { it.type->iwearable_callbacks->call_on_wear( *this, it ); }
}

void Character::on_item_takeoff( item& it )
{
    for( const trait_id& mut : it.mutations_from_wearing( *this ) ) {
        mutation_loss_effect( mut );
        recalc_sight_limits();
        calc_encumbrance();
        if( get_stamina() > get_stamina_max() ) { set_stamina( get_stamina_max() ); }
    }
    morale->on_item_takeoff( it );
    if( it.type->iwearable_callbacks ) { it.type->iwearable_callbacks->call_on_takeoff( *this, it ); }
}

void Character::on_effect_int_change(
    const efftype_id& effect_type, int intensity, const bodypart_str_id& bp )
{
    // Adrenaline can reduce perceived pain (or increase it when it times out).
    // See @ref get_perceived_pain()
    if( effect_type == effect_adrenaline ) {
        // Note that calling this does no harm if it wasn't changed.
        on_stat_change( "perceived_pain", get_perceived_pain() );
    }

    morale->on_effect_int_change( effect_type, intensity, bp );
}

void Character::on_mutation_gain( const trait_id& mid )
{
    morale->on_mutation_gain( mid );
    magic->on_mutation_gain( mid, *this );
    update_type_of_scent( mid );
    recalculate_enchantment_cache(); // mutations can have enchantments
}

void Character::on_mutation_loss( const trait_id& mid )
{
    morale->on_mutation_loss( mid );
    magic->on_mutation_loss( mid );
    update_type_of_scent( mid, false );
    recalculate_enchantment_cache(); // mutations can have enchantments
}

void Character::on_stat_change( const std::string& stat, int value )
{
    morale->on_stat_change( stat, value );
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

int Character::adjust_for_focus( int amount ) const
{
    int effective_focus = focus_pool;
    if( has_trait( trait_FASTLEARNER ) ) { effective_focus += 15; }
    if( has_active_bionic( bio_memory ) ) { effective_focus += 10; }
    if( has_trait( trait_SLOWLEARNER ) ) { effective_focus -= 15; }
    effective_focus +=
        ( get_int() - get_option<int>( "INT_BASED_LEARNING_BASE_VALUE" ) )
        * get_option<int>( "INT_BASED_LEARNING_FOCUS_ADJUSTMENT" );
    double tmp = amount * ( effective_focus / 100.0 );
    return roll_remainder( tmp );
}

std::set<tripoint_bub_ms> Character::get_legacy_path_avoid() const
{
    std::set<tripoint_bub_ms> ret;
    for( npc& guy : g->all_npcs() ) {
        if( sees( guy ) ) { ret.insert( guy.bub_pos() ); }
    }

    // TODO: Add known traps in a way that doesn't destroy performance

    return ret;
}

const pathfinding_settings &Character::get_legacy_pathfinding_settings() const
{
    return *path_settings;
}

std::pair<PathfindingSettings, RouteSettings> Character::get_pathfinding_pair() const
{
    PathfindingSettings path_settings;

    path_settings.door_open_cost = 2.0;
    path_settings.mob_presence_penalty = get_option<float>(
            "PATHFINDING_MOB_PRESENCE_PENALTY_NPC_"
            "DEFAULT" );
    path_settings.rough_terrain_cost = 0.0;
    path_settings.sharp_terrain_cost = INFINITY;
    path_settings.trap_cost = INFINITY;
    path_settings.can_climb_stairs = true;
    path_settings.bash_strength_val = 0;

    const int climb = std::min( 20, get_dex() );
    if( climb <= 1 ) {
        path_settings.climb_cost = INFINITY;
    } else {
        const float climb_success_prob = 1.0 - 1.0 / climb;
        path_settings.climb_cost = 5 / climb_success_prob;
    }

    RouteSettings route_settings;
    // TODO: Make it assign a stockfish preset instead
    route_settings.alpha = 1.0;
    route_settings.h_coeff = 1.0;
    route_settings.max_dist = INFINITY;
    route_settings.max_f_coeff = INFINITY;
    route_settings.max_s_coeff = INFINITY;
    route_settings.f_limit_based_on_max_dist = false;
    route_settings.search_cone_angle = 180.0;
    route_settings.search_radius_coeff = INFINITY;

    return {path_settings, route_settings};
}

float Character::power_rating() const
{
    const item& weapon = primary_weapon();
    int dmg = std::max(
    {weapon.damage_melee( DT_BASH ), weapon.damage_melee( DT_CUT ), weapon.damage_melee( DT_STAB )} );

    int ret = 2;
    // Small guns can be easily hidden from view
    if( weapon.volume() <= 250_ml ) {
        ret = 2;
    } else if( weapon.is_gun() ) {
        ret = 4;
    } else if( dmg > 12 ) {
        ret = 3; // Melee weapon or weapon-y tool
    }
    if( get_size() == creature_size::huge ) { ret += 1; }
    if( is_wearing_power_armor( nullptr ) ) {
        ret = 5; // No mercy!
    }
    return ret;
}

float Character::speed_rating() const
{
    float ret = get_speed() / 100.0f;
    ret *= 100.0f / run_cost( 100, false );
    // Adjustment for player being able to run, but not doing so at the moment
    if( move_mode != CMM_RUN ) {
        ret *= 1.0f + ( static_cast<float>( get_stamina() ) / static_cast<float>( get_stamina_max() ) );
    }
    return ret;
}

item &Character::item_with_best_of_quality( const quality_id& qid )
{
    int maxq = max_quality( qid );
    auto items_with_quality = items_with( [qid]( const item & it ) { return it.has_quality( qid ); } );
    for( item * it : items_with_quality ) {
        if( it->get_quality( qid ) == maxq ) { return *it; }
    }
    return null_item_reference();
}

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

// Used primarily for ressurection lua scripts
// count: number of items to drop < 0 means drop all
void Character::drop_inv( const int count )
{
    if( count < 0 || static_cast<size_t>( count ) >= inv.size() ) {
        std::vector<detached_ptr<item>> tmp = inv_dump_remove();
        for( auto& itm : tmp ) { get_map().add_item_or_charges( bub_pos(), std::move( itm ) ); }
    } else {
        for( int i = 0; i < count; i++ ) {
            int randidx = rng( 0, inv.size() );
            get_map().add_item_or_charges( bub_pos(), inv.remove_item( randidx ) );
        }
    }
}

void Character::place_corpse()
{
    // If the character/NPC is on a distant mission, don't drop their their gear when they die since
    // they still have a local pos
    if( !death_drops ) { return; }
    std::vector<detached_ptr<item>> tmp = inv_dump_remove();
    detached_ptr<item> body = item::make_corpse( mtype_id::NULL_ID(), calendar::turn, name );
    map& here = get_map();
    for( auto& itm : tmp ) { here.add_item_or_charges( bub_pos(), std::move( itm ) ); }
    for( const bionic& bio : get_bionic_collection() ) {
        if( bio.info().itype().is_valid() ) {
            detached_ptr<item> cbm = item::spawn( bio.id.str(), calendar::turn );
            cbm->faults.emplace( fault_bionic_nonsterile );
            body->add_component( std::move( cbm ) );
        }
    }

    here.add_item_or_charges( bub_pos(), std::move( body ) );
}

void Character::place_corpse( const tripoint_abs_omt& om_target )
{
    tinymap bay;
    bay.load( project_to<coords::sm>( om_target ), false );
    point_bub_ms fin{rng( 1, SEEX * 2 - 2 ), rng( 1, SEEX * 2 - 2 )};
    // This makes no sense at all. It may find a random tile without furniture, but
    // if the first try to find one fails, it will go through all tiles of the map
    // and essentially select the last one that has no furniture.
    // Q: Why check for furniture? (Check for passable or can-place-items seems more useful.)
    // Q: Why not grep a random point out of all the possible points (e.g. via random_entry)?
    // Q: Why use furn_str_id instead of f_null?
    // TODO: fix it, see above.
    if( bay.furn( fin ) != furn_str_id( "f_null" ) ) {
        for( const auto& p : bay.points_on_zlevel() ) {
            if( bay.furn( p ) == furn_str_id( "f_null" ) ) {
                fin.x() = p.x();
                fin.y() = p.y();
            }
        }
    }

    std::vector<detached_ptr<item>> tmp = inv_dump_remove();
    detached_ptr<item> body = item::make_corpse( mtype_id::NULL_ID(), calendar::turn, name );
    for( auto& itm : tmp ) { bay.add_item_or_charges( fin, std::move( itm ) ); }
    for( const bionic& bio : get_bionic_collection() ) {
        if( bio.info().itype().is_valid() ) {
            body->put_in( item::spawn( bio.info().itype(), calendar::turn ) );
        }
    }

    bay.add_item_or_charges( fin, std::move( body ) );
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

void Character::shift_destination( point_rel_ms shift )
{
    if( next_expected_position ) { *next_expected_position = *next_expected_position + shift; }

    for( auto& elem : auto_move_route ) { elem = elem + shift; }
}

bool Character::has_weapon() const { return !unarmed_attack(); }

int Character::get_lowest_hp() const
{
    // Set lowest_hp to an arbitrarily large number.
    int lowest_hp = 999;
    for( const std::pair<const bodypart_str_id, bodypart> &elem : get_body() ) {
        const int cur_hp = elem.second.get_hp_cur();
        if( cur_hp < lowest_hp ) { lowest_hp = cur_hp; }
    }
    return lowest_hp;
}

Attitude Character::attitude_to( const Creature& other ) const
{
    const auto m = dynamic_cast<const monster *>( &other );
    if( m != nullptr ) {
        if( m->friendly != 0 ) { return Attitude::A_FRIENDLY; }
        switch( m->attitude( const_cast<Character*>( this ) ) ) {
            // player probably does not want to harm them, but doesn't care much at all.
            case MATT_FOLLOW:
            case MATT_IGNORE:
            case MATT_FLEE:
                return Attitude::A_NEUTRAL;
            // player does not want to harm those.
            case MATT_FRIEND:
            case MATT_FPASSIVE:
            case MATT_ZLAVE:
                // Don't want to harm your zlave!
                return Attitude::A_FRIENDLY;
            case MATT_ATTACK:
                return Attitude::A_HOSTILE;
            case MATT_NULL:
            case MATT_UNKNOWN:
            case NUM_MONSTER_ATTITUDES:
                break;
        }

        return Attitude::A_NEUTRAL;
    }

    const auto p = dynamic_cast<const npc *>( &other );
    if( p != nullptr ) {
        if( p->is_enemy() ) {
            return Attitude::A_HOSTILE;
        } else if( p->is_player_ally() ) {
            return Attitude::A_FRIENDLY;
        } else {
            return Attitude::A_NEUTRAL;
        }
    } else if( &other == this ) {
        return Attitude::A_FRIENDLY;
    }

    return Attitude::A_NEUTRAL;
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

std::vector<tripoint_bub_ms> &Character::get_auto_move_route() { return auto_move_route; }

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

const recipe_subset &Character::get_learned_recipes() const
{
    if( *_skills != *autolearn_skills_stamp ) {
        for( const auto& r : recipe_dict.all_autolearn() ) {
            if( meets_skill_requirements( r->autolearn_requirements ) ) {
                learned_recipes->include( r );
            }
        }
        *autolearn_skills_stamp = *_skills;
    }

    return *learned_recipes;
}

bool Character::knows_recipe( const recipe* rec ) const
{
    return get_learned_recipes().contains( *rec );
}

void Character::learn_recipe( const recipe* const rec )
{
    if( rec->never_learn ) { return; }
    learned_recipes->include( rec );
}

bool Character::can_learn_by_disassembly( const recipe &rec ) const
{
    return !rec.learn_by_disassembly.empty() &&
    meets_skill_requirements( rec.learn_by_disassembly );
}

bool has_psy_protection( const Character& c, int partial_chance )
{
    return c.has_artifact_with( AEP_PSYSHIELD )
           || ( c.worn_with_flag( flag_PSYSHIELD_PARTIAL ) && one_in( partial_chance ) );
}

void Character::set_underwater( bool x )
{
    if( is_underwater() != x ) {
        Creature::set_underwater( x );
        recalc_sight_limits();
    }
}

void Character::clear_npc_ai_info_cache( npc_ai_info key ) const { npc_ai_info_cache[key] = -1.0; }

void Character::set_npc_ai_info_cache( npc_ai_info key, double val ) const
{
    npc_ai_info_cache[key] = val;
}

std::optional<double> Character::get_npc_ai_info_cache( npc_ai_info key ) const
{
    return npc_ai_info_cache[key];
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

namespace
{

auto is_foot_hit( const bodypart_id& bp_hit ) -> bool
{
    return bp_hit == bodypart_str_id( "foot_l" ) || bp_hit == bodypart_str_id( "foot_r" );
}

auto is_leg_hit( const bodypart_id& bp_hit ) -> bool
{
    return bp_hit == bodypart_str_id( "leg_l" ) || bp_hit == bodypart_str_id( "leg_r" );
}

/**
 * @brief Check if the given shield can protect the given bodypart.
 *
 * - Best item available doesn't count as a shield.
 * - Shield already protects the part we're interested in.
 * - Targeted bodypart is a foot, unlikely to ever successfully block that low.
 */
auto is_covered_by_shield( const bodypart_id &bp_hit, const item &shield ) -> bool
{
    return shield.has_flag( flag_BLOCK_WHILE_WORN )
    && !shield.covers( bp_hit )
    && !is_foot_hit( bp_hit );
}

enum class ShieldLevel { None, Block1, Block2, Block3 };
auto shield_level( const item &shield ) -> ShieldLevel
{
    if( shield.has_technique( WBLOCK_3 ) ) {
    return ShieldLevel::Block3;
} else if( shield.has_technique( WBLOCK_2 ) ) {
    return ShieldLevel::Block2;
} else if( shield.has_technique( WBLOCK_1 ) ) {
    return ShieldLevel::Block1;
}
return ShieldLevel::None;
}

auto coverage_modifier_by_technic( ShieldLevel level, bool leg_hit ) -> float
{
    switch( level ) {
    case ShieldLevel::Block3:
        return leg_hit ? 0.75f : 0.9f;
    case ShieldLevel::Block2:
        return leg_hit ? 0.5f : 0.8f;
    case ShieldLevel::Block1:
        return leg_hit ? 0.25f : 0.7f;
    default:
        return 0.0f;
}
}

auto is_valid_hallucination( Creature* source ) -> bool
{
    return source != nullptr && source->is_hallucination();
}

auto get_shield_resist( const item& shield, const damage_unit& damage ) -> int
{
    // *INDENT-OFF*
    switch (damage.type) {
        case DT_BASH:
            return shield.bash_resist();
        case DT_CUT:
            return shield.cut_resist();
        case DT_STAB:
            return shield.stab_resist();
        case DT_BULLET:
            return shield.bullet_resist();
        case DT_HEAT:
            return shield.fire_resist();
        case DT_ACID:
            return shield.acid_resist();
        default:
            return 0;
    }
    // *INDENT-ON*
}

} // namespace

float Character::get_block_amount( const item& shield, const damage_unit& unit )
{
    const int resist = get_shield_resist( shield, unit );

    return std::max( 0.0f, ( resist - unit.res_pen ) * unit.res_mult );
}

bool Character::block_ranged_hit( Creature* source, bodypart_id& bp_hit, damage_instance& dam )
{
    // Having access to more than one shield is not normal in vanilla, for now keep it simple and
    // only give one chance to catch a bullet.
    item& shield = best_shield();

    // Bail out early just in case, if blocking with bare hands.
    if( shield.is_null() ) { return false; }

    const auto level = shield_level( shield );
    if( level == ShieldLevel::None || !is_covered_by_shield( bp_hit, shield ) ) { return false; }
    // Modify chance based on coverage and blocking ability, with lowered chance if hitting the
    // legs. Exclude armguards here.
    const float technic_modifier = coverage_modifier_by_technic( level, is_leg_hit( bp_hit ) );
    const float shield_coverage_modifier = shield.get_avg_coverage() * technic_modifier;

    add_msg( m_debug, _( "block_ranged_hit success rate: %i%%" ),
             static_cast<int>( shield_coverage_modifier ) );

    // Now roll coverage to determine if we intercept the shot.
    if( rng( 1, 100 ) > shield_coverage_modifier ) {
        add_msg( m_debug, _( "block_ranged_hit attempt failed" ) );
        return false;
    }

    const float wear_modifier = is_valid_hallucination( source ) ? 0.0f : 1.0f;
    handle_melee_wear( shield, wear_modifier );

    int total_damage = 0;
    int blocked_damage = 0;
    for( auto& elem : dam.damage_units ) {
        total_damage += elem.amount * elem.damage_multiplier;
        // Go through all relevant damage types and reduce by armor value if one exists.
        const float block_amount = get_block_amount( shield, elem );
        elem.amount -= block_amount;
        blocked_damage += block_amount;
        const resistances res = resistances( shield );
        elem.res_pen = std::max( 0.0f, elem.res_pen - res.type_resist( elem.type ) );
    }
    blocked_damage = std::min( total_damage, blocked_damage );
    add_msg( m_debug, _( "expected base damage: %i" ), total_damage );

    const std::string thing_blocked_with = shield.tname();
    if( blocked_damage > 0 ) {
        add_msg_player_or_npc(
            _( "The shot hits your %s, absorbing %i damage." ),
            _( "The shot hits <npcname>'s %s, absorbing %i damage." ), thing_blocked_with,
            blocked_damage );
    } else {
        add_msg_player_or_npc(
            _( "The shot hits your %s, but it punches right through!" ),
            _( "The shot hits <npcname>'s %s, but it punches right through!" ), thing_blocked_with );
    }
    return true;
}

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

// force is maximum damage to hp before scaling
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

bool Character::can_reload( const item &it, const itype_id &ammo ) const
{
    if( it.is_holster() ) {
    const holster_actor *ptr = dynamic_cast<const holster_actor *>
                               ( it.get_use( "holster" )->get_actor_ptr() );
        return static_cast<int>( it.contents.num_item_stacks() ) < ptr->multi;
    }
    if( !it.is_reloadable_with( ammo ) ) {
    return false;
}

if( it.is_ammo_belt() ) {
    const auto &linkage = it.type->magazine->linkage;
    if( linkage && !has_charges( *linkage, 1 ) ) {
            return false;
        }
    }

    return true;
}

int Character::item_reload_cost( const item &it, item &ammo, int qty ) const
{
    if( ammo.is_ammo() ) {
    qty = std::max( std::min( ammo.charges, qty ), 1 );
    } else if( ammo.is_ammo_container() || ammo.is_container() ) {
    qty = clamp( qty, ammo.contents.front().charges, 1 );
    } else if( ammo.is_magazine() ) {
    qty = 1;
} else if( ammo.is_comestible() ) {
    qty = std::max( std::min( qty, ammo.charges ), 1 );
    } else {
        debugmsg( "cannot determine reload cost as %s is neither ammo or magazine", ammo.tname() );
        return 0;
    }

    // Save the quantity so we can change it for item_handling_cost and reset it after
    int saved_quantity = ammo.charges;
    ammo.charges = qty;
    // No base cost for handling ammo - that's already included in obtain cost
    // We have the ammo in our hands right now
    int mv = item_handling_cost( ammo, true, 0 );
    ammo.charges = saved_quantity;

    if( ammo.has_flag( flag_MAG_BULKY ) ) {
    mv *= 1.5; // bulky magazines take longer to insert
}

if( !it.is_gun() && !it.is_magazine() ) {
    return mv + 100; // reload a tool or sealable container
}

/** @EFFECT_GUN decreases the time taken to reload a magazine */
/** @EFFECT_PISTOL decreases time taken to reload a pistol */
/** @EFFECT_SMG decreases time taken to reload an SMG */
/** @EFFECT_RIFLE decreases time taken to reload a rifle */
/** @EFFECT_SHOTGUN decreases time taken to reload a shotgun */
/** @EFFECT_LAUNCHER decreases time taken to reload a launcher */

// If we're topping off an internal magazine in a gun, only use base reload time, magazines use time per round.
int cost = ( it.is_gun() ? it.get_reload_time() : it.type->magazine->reload_time ) *
           ( it.is_gun() ? 1 : qty );

skill_id sk = it.is_gun() ? it.type->gun->skill_used : skill_gun;
mv += cost / ( 1.0f + std::min( get_skill_level( sk ) * 0.1f, 1.0f ) );

    if( it.has_flag( flag_STR_RELOAD ) ) {
    /** @EFFECT_STR over 10 reduces reload time of some weapons */
    /** maximum reduction down to 25% of reload rate */
    mv *= std::max<float>( 10.0f / std::max<float>( 10.0f, get_str() ), 0.25f );
    } else if( it.has_flag( flag_STR_DRAW ) && it.get_min_str() > 1 ) {
    // Threshold depends on str_req of the weapon instead of a fixed value
    // Allow understrength characters to draw slower since base reload rate is about the same for all bows
    mv *= std::max<float>( it.get_min_str() / std::max<float>( 1, get_str() ), 0.25f );
    }

    return std::max( mv, 25 );
}

bool Character::studied_all_recipes( const itype &book ) const
{
    if( !book.book ) {
    return true;
}
for( auto &elem : book.book->recipes ) {
    if( !knows_recipe( elem.recipe ) ) {
            return false;
        }
    }
    return true;
}

recipe_subset Character::get_recipes_from_books(
    const inventory& crafting_inv, const recipe_filter& filter ) const
{
    recipe_subset res;

    for( const auto& stack : crafting_inv.const_slice() ) {
        const item& candidate = *stack->front();

        for( std::pair<const recipe *, int> recipe_entry : candidate.get_available_recipes( *this ) ) {
            if( filter && !filter( *recipe_entry.first ) ) { continue; }
            res.include( recipe_entry.first, recipe_entry.second );
        }
    }

    return res;
}

recipe_subset Character::get_available_recipes(
    const inventory& crafting_inv, const std::vector<npc *> *helpers, recipe_filter filter ) const
{
    recipe_subset res;

    if( filter ) {
        res.include_if( get_learned_recipes(), filter );
    } else {
        res.include( get_learned_recipes() );
    }

    res.include( get_recipes_from_books( crafting_inv, filter ) );

    if( helpers != nullptr ) {
        for( npc * np : *helpers ) {
            // Directly form the helper's inventory
            res.include( get_recipes_from_books( np->inv.as_inventory(), filter ) );
            // Being told what to do
            res.include_if( np->get_learned_recipes(), [this, &filter]( const recipe & r ) {
                if( filter && !filter( r ) ) { return false; }
                // Skilled enough to understand
                return get_skill_level( r.skill_used ) >= static_cast<int>( r.difficulty * 0.8f );
            } );
        }
    }

    return res;
}

bool Character::has_recipe_requirements( const recipe& rec ) const
{
    return get_all_skills().has_recipe_requirements( rec );
}

int Character::has_recipe( const recipe *r, const inventory &crafting_inv,
                           const std::vector<npc *> &helpers ) const
{
    if( !r->skill_used ) {
    return 0;
}

if( knows_recipe( r ) ) {
        return r->difficulty;
    }

    const auto available = get_available_recipes( crafting_inv, &helpers );
    return available.contains( *r ) ? available.get_custom_difficulty( r ) : -1;
}


detached_ptr<item> Character::reduce_charges( int position, int quantity )
{
    item& it = i_at( position );
    if( it.is_null() ) {
        debugmsg( "invalid item position %d for reduce_charges", position );
        return detached_ptr<item>();
    }
    if( it.charges <= quantity ) { return i_rem( position ); }
    it.mod_charges( -quantity );

    auto taken = item::spawn( it );
    taken->charges = quantity;
    return taken;
}

detached_ptr<item> Character::reduce_charges( item* it, int quantity )
{
    if( !has_item( *it ) ) {
        debugmsg( "invalid item (name %s) for reduce_charges", it->tname() );
        return detached_ptr<item>();
    }
    if( it->charges <= quantity ) { return it->detach(); }
    it->mod_charges( -quantity );

    auto taken = item::spawn( *it );
    taken->charges = quantity;
    return taken;
}
