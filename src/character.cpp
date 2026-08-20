#include "character.h"
#include "coop_mutation_log.h"
#include "physics/physics_world.h"
#include "action.h"
#include "action_time_scale.h"
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

auto Character::setpos( const tripoint_bub_ms& p ) -> void { setpos( get_map().bub_to_abs( p ) ); }

auto Character::setpos( const tripoint_abs_ms& p ) -> void
{
    position = p;
    if( auto *pw = get_map().get_physics_world() ) {
        pw->on_creature_moved( *this );
    }
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
    if( auto * _log = coop_mutation_log::current() ) {
        _log->push( {
            .type = coop_event_type::creature_died,
            .pos = abs_pos(),
            .creature_id = getID().get_value()} );
    }
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

/*
 * Innate stats setters
 */

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
               || has_bionic( bio_ears );
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

/*
 * Calculate player brightness based on the brightest active item, as
 * per itype tag LIGHT_* and optional CHARGEDIM ( fade starting at 20% charge )
 * item.light.* is -unimplemented- for the moment, as it is a custom override for
 * applying light sources/arcs with specific angle and direction.
 */
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

float Character::rest_quality() const
{
    // Just a placeholder for now.
    // TODO: Waiting/reading/being unconscious on bed/sofa/grass
    return has_effect( effect_sleep ) ? 1.0f : 0.0f;
}

bodypart_str_id Character::bp_to_hp( const bodypart_str_id& bp ) { return bp->main_part; }


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

/**
 * Returns the total normal hearing protection of a characters worn items, in dB spl.
 * If bool advanced is true, gets the advanced hearing protection.
 */
int Character::get_char_hearing_protection( bool advanced ) const
{
    int ret = 0;
    for( const item * const& worn_item : worn ) {
        ret += worn_item->get_hearing_protection( advanced );
    }
    return ret;
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
    sound_event se;
    se.origin = bub_pos();
    se.volume = loudness;
    se.category = sounds::sound_t::speech;
    se.description = _( "a hacking cough." );
    se.from_player = is_avatar();
    se.from_npc = !se.from_player;
    se.faction = get_faction()->id();
    se.monfaction = get_faction()->mon_faction();
    se.id = "misc";
    se.variant = "cough";
    sounds::sound( se );

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
    // Base shout set at 65 dB
    int base = 65;
    int shout_multiplier = 2;

    // Mutations make shouting louder, they also define the default message
    if( has_trait( trait_SHOUT3 ) ) {
        shout_multiplier = 3;
        base = 80;
    } else if( has_trait( trait_SHOUT2 ) ) {
        base = 70;
        shout_multiplier = 2;
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
    const int penalty = static_cast<int>( std::floor( encumb( body_part_mouth ) * 1.5 ) );
    int noise = base + static_cast<int>( std::floor( str_cur * shout_multiplier ) ) - penalty;

    // Minimum noise volume possible after all reductions.
    // Volume 20dB or less is generally inaudible.
    constexpr int minimum_noise = 20;

    if( noise <= base ) { noise = std::max( minimum_noise, noise ); }

    // Screaming underwater is not good for oxygen and harder to do overall
    if( is_underwater() ) {
        noise = static_cast<int>( std::max( minimum_noise * 1.0, noise * 0.75 ) );
    }
    // Cap shouting to 180dB, which is already going to deafen people.
    return std::min( 180, noise );
}

void Character::shout( std::string msg, bool order )
{
    int base = 65;
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
            base = 80;
            add_msg_if_player( m_warning, _( "You let out an ear-piercing howl!" ) );
            msg = is_player()
                  ? _( "yourself let out an ear-piercing howl!" )
                  : _( "an ear-piercing howl!" );
            shout = "howl";
        } else if( has_trait( trait_SHOUT2 ) ) {
            base = 70;
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
    // 20dB is generally inaudible.
    constexpr int minimum_noise = 20;

    if( noise <= base ) {
        std::string dampened_shout;
        std::ranges::transform( msg, std::back_inserter( dampened_shout ), tolower );
        msg = std::move( dampened_shout );
    }

    // Screaming underwater is not good for oxygen and harder to do overall
    if( is_underwater() ) {
        if( !has_trait( trait_GILLS ) && !has_trait( trait_GILLS_CEPH ) ) { mod_stat( "oxygen", -noise ); }
    }

    const int penalty = static_cast<int>( std::floor( encumb( body_part_mouth ) * 1.5 ) );
    // TODO: indistinct noise descriptions should be handled in the sounds code
    if( noise <= minimum_noise ) {
        add_msg_if_player( m_warning, _( "The sound of your voice is almost completely muffled!" ) );
        msg = is_player() ? _( "your muffled shout" ) : _( "an indistinct voice" );
    } else if( noise * 2 <= noise + penalty ) {
        // The shout's volume is 1/2 or lower of what it would be without the penalty
        add_msg_if_player( m_warning, _( "The sound of your voice is significantly muffled!" ) );
    }

    sound_event se;
    se.origin = bub_pos();
    se.volume = noise;
    se.category = order ? sounds::sound_t::order : sounds::sound_t::alert;
    se.description = msg;
    se.from_player = is_avatar();
    se.from_npc = !se.from_player;
    se.faction = get_faction()->id();
    se.monfaction = get_faction()->mon_faction();
    se.id = "shout";
    se.variant = shout;
    sounds::sound( se );
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

/// Returns the mutation category with the highest strength
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

void Character::set_knows_creature_type( const mtype_id& c ) { known_monsters.emplace( c ); }

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

std::vector<tripoint_bub_ms> &Character::get_auto_move_route() { return auto_move_route; }

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

void Character::clear_npc_ai_info_cache( npc_ai_info key ) const { npc_ai_info_cache[key] = -1.0; }

void Character::set_npc_ai_info_cache( npc_ai_info key, double val ) const
{
    npc_ai_info_cache[key] = val;
}

std::optional<double> Character::get_npc_ai_info_cache( npc_ai_info key ) const
{
    return npc_ai_info_cache[key];
}

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

// force is maximum damage to hp before scaling
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


