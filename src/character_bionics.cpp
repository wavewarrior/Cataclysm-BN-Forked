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


// *INDENT-OFF*

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
