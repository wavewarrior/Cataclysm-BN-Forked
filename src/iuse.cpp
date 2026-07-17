#include "iuse.h"

#include "action.h"
#include "active_tile_data_def.h"
#include "activity_actor.h"
#include "activity_actor_definitions.h"
#include "animation.h"
#include "artifact.h"
#include "avatar.h"
#include "avatar_action.h"
#include "avatar_functions.h"
#include "bionics.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_utility.h"
#include "character.h"
#include "character_functions.h"
#include "character_martial_arts.h"
#include "color.h"
#include "coordinates.h"
#include "crafting.h"
#include "creature.h"
#include "damage.h"
#include "debug.h"
#include "distribution_grid.h"
#include "effect.h" // for weed_msg
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "explosion.h"
#include "field.h"
#include "field_type.h"
#include "flag.h"
#include "flat_set.h"
#include "fluid_grid.h"
#include "fstream_utils.h"
#include "fungal_effects.h"
#include "game.h"
#include "game_constants.h"
#include "game_inventory.h"
#include "handle_liquid.h"
#include "harvest.h"
#include "iexamine.h"
#include "int_id.h"
#include "inventory.h"
#include "item.h"
#include "item_cable.h"
#include "item_contents.h"
#include "iteminfo_query.h"
#include "itype.h"
#include "iuse_actor.h" // For firestarter
#include "json.h"
#include "line.h"
#include "locations.h"
#include "map.h"
#include "map_iterator.h"
#include "map_selector.h"
#include "mapdata.h"
#include "martialarts.h"
#include "memorial_logger.h"
#include "memory_fast.h"
#include "messages.h"
#include "monattack.h"
#include "mongroup.h"
#include "monster.h"
#include "morale_types.h"
#include "mtype.h"
#include "mutation.h"
#include "npc.h"
#include "omdata.h"
#include "options.h"
#include "output.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "pimpl.h"
#include "player.h"
#include "player_activity.h"
#include "pldata.h"
#include "point.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "requirements.h"
#include "ret_val.h"
#include "rng.h"
#include "skill.h"
#include "sounds.h"
#include "speech.h"
#include "string_formatter.h"
#include "string_id.h"
#include "string_input_popup.h"
#include "string_utils.h"
#include "teleport.h"
#include "text_snippets.h"
#include "timed_event.h"
#include "translations.h"
#include "trap.h"
#include "type_id.h"
#include "ui.h"
#include "units_utility.h"
#include "value_ptr.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vehicle_selector.h"
#include "visitable.h"
#include "vpart_position.h"
#include "vpart_range.h"
#include "weather.h"
#include "weather_gen.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

static const activity_id ACT_CRAFT( "ACT_CRAFT" );
static const activity_id ACT_FISH( "ACT_FISH" );
static const activity_id ACT_GAME( "ACT_GAME" );
static const activity_id ACT_GENERIC_GAME( "ACT_GENERIC_GAME" );
static const activity_id ACT_HAIRCUT( "ACT_HAIRCUT" );
static const activity_id ACT_MEDITATE( "ACT_MEDITATE" );
static const activity_id ACT_MIND_SPLICER( "ACT_MIND_SPLICER" );
static const activity_id ACT_ROBOT_CONTROL( "ACT_ROBOT_CONTROL" );
static const activity_id ACT_SHAVE( "ACT_SHAVE" );
static const activity_id ACT_VIBE( "ACT_VIBE" );

static const efftype_id effect_adrenaline( "adrenaline" );
static const efftype_id effect_antibiotic( "antibiotic" );
static const efftype_id effect_asthma( "asthma" );
static const efftype_id effect_attention( "attention" );
static const efftype_id effect_beartrap( "beartrap" );
static const efftype_id effect_bleed( "bleed" );
static const efftype_id effect_blind( "blind" );
static const efftype_id effect_bloodworms( "bloodworms" );
static const efftype_id effect_boomered( "boomered" );
static const efftype_id effect_bouldering( "bouldering" );
static const efftype_id effect_brainworms( "brainworms" );
static const efftype_id effect_cig( "cig" );
static const efftype_id effect_contacts( "contacts" );
static const efftype_id effect_corroding( "corroding" );
static const efftype_id effect_crushed( "crushed" );
static const efftype_id effect_datura( "datura" );
static const efftype_id effect_dazed( "dazed" );
static const efftype_id effect_well_fed( "well_fed" );
static const efftype_id effect_dermatik( "dermatik" );
static const efftype_id effect_docile( "docile" );
static const efftype_id effect_downed( "downed" );
static const efftype_id effect_drunk( "drunk" );
static const efftype_id effect_earphones( "earphones" );
static const efftype_id effect_foodpoison( "foodpoison" );
static const efftype_id effect_formication( "formication" );
static const efftype_id effect_fungus( "fungus" );
static const efftype_id effect_glowing( "glowing" );
static const efftype_id effect_glowy_led( "glowy_led" );
static const efftype_id effect_hallu( "hallu" );
static const efftype_id effect_happy( "happy" );
static const efftype_id effect_harnessed( "harnessed" );
static const efftype_id effect_has_bag( "has_bag" );
static const efftype_id effect_haslight( "haslight" );
static const efftype_id effect_in_pit( "in_pit" );
static const efftype_id effect_infected( "infected" );
static const efftype_id effect_jetinjector( "jetinjector" );
static const efftype_id effect_lack_sleep( "lack_sleep" );
static const efftype_id effect_laserlocked( "laserlocked" );
static const efftype_id effect_lying_down( "lying_down" );
static const efftype_id effect_melatonin_supplements( "melatonin" );
static const efftype_id effect_meth( "meth" );
static const efftype_id effect_monster_armor( "monster_armor" );
static const efftype_id effect_music( "music" );
static const efftype_id effect_onfire( "onfire" );
static const efftype_id effect_paincysts( "paincysts" );
static const efftype_id effect_pet( "pet" );
static const efftype_id effect_poison( "poison" );
static const efftype_id effect_ridden( "ridden" );
static const efftype_id effect_riding( "riding" );
static const efftype_id effect_run( "run" );
static const efftype_id effect_sad( "sad" );
static const efftype_id effect_saddled( "monster_saddled" );
static const efftype_id effect_sap( "sap" );
static const efftype_id effect_shakes( "shakes" );
static const efftype_id effect_sleep( "sleep" );
static const efftype_id effect_slimed( "slimed" );
static const efftype_id effect_smoke( "smoke" );
static const efftype_id effect_spores( "spores" );
static const efftype_id effect_stimpack( "stimpack" );
static const efftype_id effect_strong_antibiotic( "strong_antibiotic" );
static const efftype_id effect_stunned( "stunned" );
static const efftype_id effect_tapeworm( "tapeworm" );
static const efftype_id effect_teargas( "teargas" );
static const efftype_id effect_teleglow( "teleglow" );
static const efftype_id effect_tied( "tied" );
static const efftype_id effect_took_antiasthmatic( "took_antiasthmatic" );
static const efftype_id effect_took_flumed( "took_flumed" );
static const efftype_id effect_took_prozac( "took_prozac" );
static const efftype_id effect_took_prozac_bad( "took_prozac_bad" );
static const efftype_id effect_took_thorazine( "took_thorazine" );
static const efftype_id effect_took_xanax( "took_xanax" );
static const efftype_id effect_valium( "valium" );
static const efftype_id effect_visuals( "visuals" );
static const efftype_id effect_weak_antibiotic( "weak_antibiotic" );
static const efftype_id effect_webbed( "webbed" );
static const efftype_id effect_weed_high( "weed_high" );

static const itype_id itype_advanced_ecig( "advanced_ecig" );
static const itype_id itype_afs_atomic_smartphone( "afs_atomic_smartphone" );
static const itype_id itype_afs_atomic_smartphone_music( "afs_atomic_smartphone_music" );
static const itype_id itype_afs_wraitheon_smartphone( "afs_wraitheon_smartphone" );
static const itype_id itype_afs_atomic_wraitheon_music( "afs_atomic_wraitheon_music" );
static const itype_id itype_apparatus( "apparatus" );
static const itype_id itype_arrow_flamming( "arrow_flamming" );
static const itype_id itype_battery( "battery" );
static const itype_id itype_barometer( "barometer" );
static const itype_id itype_c4armed( "c4armed" );
static const itype_id itype_canister_empty( "canister_empty" );
static const itype_id itype_cig( "cig" );
static const itype_id itype_cigar( "cigar" );
static const itype_id itype_cow_bell( "cow_bell" );
static const itype_id itype_data_card( "data_card" );
static const itype_id itype_e_handcuffs( "e_handcuffs" );
static const itype_id itype_ecig( "ecig" );
static const itype_id itype_fire( "fire" );
static const itype_id itype_firecracker_act( "firecracker_act" );
static const itype_id itype_firecracker_pack_act( "firecracker_pack_act" );
static const itype_id itype_geiger_off( "geiger_off" );
static const itype_id itype_geiger_on( "geiger_on" );
static const itype_id itype_debug_grenade_act( "debug_grenade_act" );
static const itype_id itype_handrolled_cig( "handrolled_cig" );
static const itype_id itype_hygrometer( "hygrometer" );
static const itype_id itype_joint( "joint" );
static const itype_id itype_log( "log" );
static const itype_id itype_mask_h20survivor_on( "mask_h20survivor_on" );
static const itype_id itype_mininuke_act( "mininuke_act" );
static const itype_id itype_mobile_memory_card( "mobile_memory_card" );
static const itype_id itype_mobile_memory_card_used( "mobile_memory_card_used" );
static const itype_id itype_mp3( "mp3" );
static const itype_id itype_mp3_on( "mp3_on" );
static const itype_id itype_multi_cooker( "multi_cooker" );
static const itype_id itype_multi_cooker_filled( "multi_cooker_filled" );
static const itype_id itype_nicotine_liquid( "nicotine_liquid" );
static const itype_id itype_noise_emitter( "noise_emitter" );
static const itype_id itype_noise_emitter_on( "noise_emitter_on" );
static const itype_id itype_radio( "radio" );
static const itype_id itype_radio_car( "radio_car" );
static const itype_id itype_radio_car_on( "radio_car_on" );
static const itype_id itype_radio_on( "radio_on" );
static const itype_id itype_rebreather_on( "rebreather_on" );
static const itype_id itype_rebreather_xl_on( "rebreather_xl_on" );
static const itype_id itype_rmi2_corpse( "rmi2_corpse" );
static const itype_id itype_smart_phone( "smart_phone" );
static const itype_id itype_smartphone_music( "smartphone_music" );
static const itype_id itype_soldering_iron( "soldering_iron" );
static const itype_id itype_spiral_stone( "spiral_stone" );
static const itype_id itype_thermometer( "thermometer" );
static const itype_id itype_towel( "towel" );
static const itype_id itype_towel_soiled( "towel_soiled" );
static const itype_id itype_towel_wet( "towel_wet" );
static const itype_id itype_UPS( "UPS" );
static const itype_id itype_water( "water" );
static const itype_id itype_water_clean( "water_clean" );
static const itype_id itype_wax( "wax" );
static const itype_id itype_weather_reader( "weather_reader" );

static const skill_id skill_computer( "computer" );
static const skill_id skill_cooking( "cooking" );
static const skill_id skill_electronics( "electronics" );
static const skill_id skill_fabrication( "fabrication" );
static const skill_id skill_firstaid( "firstaid" );
static const skill_id skill_mechanics( "mechanics" );
static const skill_id skill_melee( "melee" );
static const skill_id skill_survival( "survival" );

static const trait_id trait_ACIDBLOOD( "ACIDBLOOD" );
static const trait_id trait_ACIDPROOF( "ACIDPROOF" );
static const trait_id trait_ALCMET( "ALCMET" );
static const trait_id trait_CHLOROMORPH( "CHLOROMORPH" );
static const trait_id trait_EATDEAD( "EATDEAD" );
static const trait_id trait_GILLS( "GILLS" );
static const trait_id trait_GILLS_CEPH( "GILLS_CEPH" );
static const trait_id trait_HYPEROPIC( "HYPEROPIC" );
static const trait_id trait_ILLITERATE( "ILLITERATE" );
static const trait_id trait_LIGHTWEIGHT( "LIGHTWEIGHT" );
static const trait_id trait_M_DEPENDENT( "M_DEPENDENT" );
static const trait_id trait_MARLOSS( "MARLOSS" );
static const trait_id trait_MARLOSS_AVOID( "MARLOSS_AVOID" );
static const trait_id trait_MARLOSS_BLUE( "MARLOSS_BLUE" );
static const trait_id trait_MARLOSS_YELLOW( "MARLOSS_YELLOW" );
static const trait_id trait_MYOPIC( "MYOPIC" );
static const trait_id trait_NOPAIN( "NOPAIN" );
static const trait_id trait_POISRESIST( "POISRESIST" );
static const trait_id trait_PROF_FERAL( "PROF_FERAL" );
static const trait_id trait_PSYCHOPATH( "PSYCHOPATH" );
static const trait_id trait_PYROMANIA( "PYROMANIA" );
static const trait_id trait_SAPROVORE( "SAPROVORE" );
static const trait_id trait_SPIRITUAL( "SPIRITUAL" );
static const trait_id trait_THRESH_MARLOSS( "THRESH_MARLOSS" );
static const trait_id trait_THRESH_MYCUS( "THRESH_MYCUS" );
static const trait_id trait_THRESH_PLANT( "THRESH_PLANT" );
static const trait_id trait_TOLERANCE( "TOLERANCE" );
static const trait_id trait_URSINE_EYE( "URSINE_EYE" );
static const trait_id trait_WAYFARER( "WAYFARER" );

static const quality_id qual_AXE( "AXE" );
static const quality_id qual_DIG( "DIG" );
static const quality_id qual_LOCKPICK( "LOCKPICK" );

static const requirement_id requirement_add_grid_connection =
    requirement_id( "add_grid_connection" );
static const auto requirement_add_fluid_grid_connection = requirement_id( "add_fluid_grid_"
                                                                          "connection" );

static const species_id FUNGUS( "FUNGUS" );
static const species_id HALLUCINATION( "HALLUCINATION" );
static const species_id INSECT( "INSECT" );
static const species_id ZOMBIE( "ZOMBIE" );

static const mongroup_id GROUP_FISH( "GROUP_FISH" );

static const mtype_id mon_bee( "mon_bee" );
static const mtype_id mon_blob( "mon_blob" );
static const mtype_id mon_dog_thing( "mon_dog_thing" );
static const mtype_id mon_duck( "mon_duck" );
static const mtype_id mon_fly( "mon_fly" );
static const mtype_id mon_hologram( "mon_hologram" );
static const mtype_id mon_shadow( "mon_shadow" );
static const mtype_id mon_spore( "mon_spore" );
static const mtype_id mon_vortex( "mon_vortex" );
static const mtype_id mon_wasp( "mon_wasp" );

static const bionic_id bio_digestion( "bio_digestion" );
static const bionic_id bio_eye_optic( "bio_eye_optic" );
static const bionic_id bio_shock( "bio_shock" );

// terrain/furn flags
static const std::string flag_CURRENT( "CURRENT" );
static const std::string flag_FISHABLE( "FISHABLE" );
static const std::string flag_PLANT( "PLANT" );
static const std::string flag_PLOWABLE( "PLOWABLE" );

// how many characters per turn of radio
static constexpr int RADIO_PER_TURN = 25;

#include "iuse_software.h"


struct object_names_collection;

struct extended_photo_def: public JsonDeserializer, public JsonSerializer {
    int quality;
    std::string name;
    std::string description;

    extended_photo_def() = default;
    void deserialize( JsonIn& jsin ) override {
        JsonObject obj = jsin.get_object();
        quality = obj.get_int( "quality" );
        name = obj.get_string( "name" );
        description = obj.get_string( "description" );
    }

    void serialize( JsonOut& jsout ) const override {
        jsout.start_object();
        jsout.member( "quality", quality );
        jsout.member( "name", name );
        jsout.member( "description", description );
        jsout.end_object();
    }
};

static std::vector<std::string> describe_character( Character* guy );
static void item_save_monsters(
    player& p, item& it, const std::vector<monster *> &monster_vec, int photo_quality );
static bool show_photo_selection( player& p, item& it, const std::string& var_name );

static bool item_read_extended_photos(
    item &, std::vector<extended_photo_def> &, const std::string &, bool = false );
static void item_write_extended_photos(
    item &, const std::vector<extended_photo_def> &, const std::string & );

static std::string format_object_pair(
    const std::pair<std::string, int> &pair, const std::string& article );
static std::string format_object_pair_article( const std::pair<std::string, int> &pair );
static std::string format_object_pair_no_article( const std::pair<std::string, int> &pair );

static std::string colorized_field_description_at( const tripoint_bub_ms& point );
static std::string colorized_trap_name_at( const tripoint_bub_ms& point );
static std::string colorized_ter_name_flags_at(
    const tripoint_bub_ms& point, const std::vector<std::string> &flags = {},
    const std::vector<ter_str_id> &ter_whitelist = {} );
static std::string colorized_feature_description_at(
    const tripoint_bub_ms& center_point, bool& item_found, const units::volume& min_visible_volume );

static std::string colorized_item_name( const item& item );
static std::string colorized_item_description( const item& item );
static const item &get_top_item_at_point(
    const tripoint_bub_ms& point, const units::volume& min_visible_volume );

static std::string effects_description_for_creature(
    Creature* creature, std::string& pose, const std::string& pronoun_sex );

static object_names_collection enumerate_objects_around_point(
    const tripoint_bub_ms& point, int radius, const tripoint_bub_ms& bounds_center_point,
    int bounds_radius, const tripoint_bub_ms& camera_pos, const units::volume& min_visible_volume,
    bool create_figure_desc, std::unordered_set<tripoint_bub_ms> &ignored_points,
    std::unordered_set<const vehicle *> &vehicles_recorded );
static extended_photo_def photo_def_for_camera_point(
    const tripoint_bub_ms& aim_point, const tripoint_bub_ms& camera_pos,
    std::vector<monster *> &monster_vec, std::vector<Character *> &character_vec );

static const std::vector<std::string> camera_ter_whitelist_flags = {
    "HIDE_PLACE",    "FUNGUS",  "TREE",      "PERMEABLE", "SHRUB", "PLACE_ITEM", "GROWTH_HARVEST",
    "GROWTH_MATURE", "GOES_UP", "GOES_DOWN", "RAMP",      "SHARP", "SIGN",       "CLIMBABLE"
};
static const std::vector<ter_str_id> camera_ter_whitelist_types = {
    ter_str_id( "t_pit_covered" ), ter_str_id( "t_grave_new" ),          ter_str_id( "t_grave" ),
    ter_str_id( "t_pit" ),         ter_str_id( "t_pit_shallow" ),        ter_str_id( "t_pit_corpsed" ),
    ter_str_id( "t_pit_spiked" ),  ter_str_id( "t_pit_spiked_covered" ), ter_str_id( "t_pit_glass" ),
    ter_str_id( "t_pit_glass" ),   ter_str_id( "t_utility_light" )
};

void remove_radio_mod( item& it, player& p )
{
    if( !it.has_flag( flag_RADIO_MOD ) ) { return; }
    p.add_msg_if_player( _( "You remove the radio modification from your %s!" ), it.tname() );
    p.i_add_or_drop( item::spawn( "radio_mod" ) );
    it.unset_flag( flag_RADIO_ACTIVATION );
    it.unset_flag( flag_RADIO_MOD );
    it.unset_flag( flag_RADIOSIGNAL_1 );
    it.unset_flag( flag_RADIOSIGNAL_2 );
    it.unset_flag( flag_RADIOSIGNAL_3 );
    it.unset_flag( flag_RADIOCARITEM );
}

/* iuse methods return the number of charges expended, which is usually it->charges_to_use().
 * Some items that don't normally use charges return 1 to indicate they're used up.
 * Regardless, returning 0 indicates the item has not been used up,
 * though it may have been successfully activated.
 */

int iuse::blech( player* p, item* it, bool, const tripoint_bub_ms & )
{
    // TODO: Add more effects?
    if( it->made_of( LIQUID ) ) {
        if( !p->query_yn( _( "This looks unhealthy, sure you want to drink it?" ) ) ) { return 0; }
    } else { // Assume that if a blech consumable isn't a drink, it will be eaten.
        if( !p->query_yn( _( "This looks unhealthy, sure you want to eat it?" ) ) ) { return 0; }
    }

    if( it->has_flag( flag_ACID )
        && ( p->has_trait( trait_ACIDPROOF ) || p->has_trait( trait_ACIDBLOOD ) ) ) {
        p->add_msg_if_player( m_bad, _( "Blech, that tastes gross!" ) );
        // reverse the harmful values of drinking this acid.
        double multiplier = -1;
        p->mod_stored_kcal( 10 * p->nutrition_for( *it ) * multiplier );
        p->mod_thirst( -it->get_comestible()->quench * multiplier + 20 );
        p->mod_healthy_mod(
            it->get_comestible()->healthy * multiplier, it->get_comestible()->healthy * multiplier );
        p->add_morale( MORALE_FOOD_BAD, it->get_comestible_fun() * multiplier, 60, 1_hours,
                       30_minutes, false, it->type );
    } else {
        p->add_msg_if_player( m_bad, _( "Blech, that burns your throat!" ) );
        p->mod_pain( rng( 32, 64 ) );
        p->add_effect( effect_poison, 15_minutes );
        p->apply_damage( nullptr, bodypart_id( "torso" ), rng( 4, 12 ) );
        p->vomit();
    }
    return it->type->charges_to_use();
}

int iuse::blech_because_unclean( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( !p->is_npc() && !p->has_bionic( bio_digestion ) ) {
        if( it->made_of( LIQUID ) ) {
            if( !p->query_yn( _( "This looks unclean, sure you want to drink it?" ) ) ) { return 0; }
        } else { // Assume that if a blech consumable isn't a drink, it will be eaten.
            if( !p->query_yn( _( "This looks unclean, sure you want to eat it?" ) ) ) { return 0; }
        }
    }
    return it->type->charges_to_use();
}

int iuse::plantblech( player* p, item* it, bool, const tripoint_bub_ms& pos )
{
    if( p->has_trait( trait_THRESH_PLANT ) ) {
        double multiplier = -1;
        if( p->has_trait( trait_CHLOROMORPH ) ) {
            multiplier = -3;
            p->add_msg_if_player( m_good, _( "The meal is revitalizing." ) );
        } else {
            p->add_msg_if_player( m_good, _( "Oddly enough, this doesn't taste so bad." ) );
        }

        // reverses the harmful values of drinking fertilizer
        p->mod_stored_kcal( -10 * p->nutrition_for( *it ) * multiplier );
        p->mod_thirst( -it->get_comestible()->quench * multiplier );
        p->mod_healthy_mod(
            it->get_comestible()->healthy * multiplier, it->get_comestible()->healthy * multiplier );
        p->add_morale( MORALE_FOOD_GOOD, -10 * multiplier, 60, 1_hours, 30_minutes, false, it->type );
        return it->type->charges_to_use();
    } else {
        return blech( p, it, true, pos );
    }
}

// Helper to handle the logic of removing some random mutations.
static void do_purify( player& p )
{
    std::vector<trait_id> valid; // Which flags the player has
    // We should use the actual thresh category if present, but old characters might not have it
    // So if they don't have it then we default to old behavior of highest category
    mutation_category_id thresh =
        p.thresh_category != mutation_category_id::NULL_ID()
        ? p.thresh_category
        : p.get_highest_category();
    for( auto& traits_iter : mutation_branch::get_all() ) {
        if( p.has_trait( traits_iter.id )
            && ( !p.has_base_trait( traits_iter.id ) || get_option<bool>( "canmutprofmut" ) ) ) {
            // Looks for active mutation
            bool threshlocked = false;
            for( auto cat : traits_iter.category ) {
                if( ( cat == thresh ) && p.crossed_threshold()
                    && ( p.thresh_tier > traits_iter.threshold_tier ) ) {
                    // We shouldn't be able to get rid of mutations that we have a threshold from
                    threshlocked = true;
                    break;
                }
            }
            if( !threshlocked ) { valid.push_back( traits_iter.id ); }
        }
    }
    if( valid.empty() ) {
        p.add_msg_if_player( _( "You feel cleansed." ) );
        return;
    }
    int num_cured = rng( 1, valid.size() );
    num_cured = std::min( 4, num_cured );
    for( int i = 0; i < num_cured && !valid.empty(); i++ ) {
        const trait_id id = random_entry_removed( valid );
        if( id->purifiable ) {
            p.remove_mutation( id );
        } else {
            p.add_msg_if_player( m_warning, _( "You feel a slight itching inside, but it passes." ) );
        }
    }
}

int iuse::purifier( player* p, item* it, bool, const tripoint_bub_ms & )
{
    mutagen_attempt checks =
        mutagen_common_checks( *p, *it, false, mutagen_technique::consumed_purifier );
    if( !checks.allowed ) { return checks.charges_used; }

    do_purify( *p );
    return it->type->charges_to_use();
}

int iuse::purify_iv( player* p, item* it, bool, const tripoint_bub_ms & )
{
    mutagen_attempt checks =
        mutagen_common_checks( *p, *it, false, mutagen_technique::injected_purifier );
    if( !checks.allowed ) { return checks.charges_used; }
    // We should use the actual thresh category if present, but old characters might not have it
    // So if they don't have it then we default to old behavior of highest category
    mutation_category_id thresh =
        p->thresh_category != mutation_category_id::NULL_ID()
        ? p->thresh_category
        : p->get_highest_category();
    std::vector<trait_id> valid; // Which flags the player has
    for( auto& traits_iter : mutation_branch::get_all() ) {
        if( p->has_trait( traits_iter.id )
            && ( !p->has_base_trait( traits_iter.id ) || get_option<bool>( "canmutprofmut" ) ) ) {
            // Looks for active mutation
            bool threshlocked = false;
            for( auto cat : traits_iter.category ) {
                if( ( cat == thresh ) && p->crossed_threshold()
                    && ( p->thresh_tier > traits_iter.threshold_tier ) ) {
                    // We shouldn't be able to get rid of mutations that we have a threshold from
                    threshlocked = true;
                    break;
                }
            }
            if( !threshlocked ) { valid.push_back( traits_iter.id ); }
        }
    }
    if( valid.empty() ) {
        p->add_msg_if_player( _( "You feel cleansed." ) );
        return it->type->charges_to_use();
    }
    int num_cured =
        rng( 4,
             valid.size() ); // Essentially a double-strength purifier, but guaranteed at least 4.
    // Double-edged and all
    if( num_cured > 8 ) { num_cured = 8; }
    for( int i = 0; i < num_cured && !valid.empty(); i++ ) {
        const trait_id id = random_entry_removed( valid );
        if( id->purifiable ) {
            p->remove_mutation( id );
        } else {
            p->add_msg_if_player( m_warning, _( "You feel a distinct burning inside, but it "
                                                "passes." ) );
        }
        if( !( p->has_trait( trait_NOPAIN ) ) ) {
            p->mod_pain( 2 * num_cured ); // Hurts worse as it fixes more
            p->add_msg_if_player( m_warning, _( "Feels like you're on fire, but you're OK." ) );
        }
        p->mod_stored_nutr( 2 * num_cured );
        p->mod_thirst( 2 * num_cured );
        p->mod_fatigue( 2 * num_cured );
    }
    return it->type->charges_to_use();
}

int iuse::purify_smart( player* p, item* it, bool, const tripoint_bub_ms & )
{
    mutagen_attempt checks =
        mutagen_common_checks( *p, *it, false, mutagen_technique::injected_smart_purifier );
    if( !checks.allowed ) { return checks.charges_used; }

    // We should use the actual thresh category if present, but old characters might not have it
    // So if they don't have it then we default to old behavior of highest category
    mutation_category_id thresh =
        p->thresh_category != mutation_category_id::NULL_ID()
        ? p->thresh_category
        : p->get_highest_category();
    std::vector<trait_id> valid;          // Which flags the player has
    std::vector<std::string> valid_names; // Which flags the player has
    for( auto& traits_iter : mutation_branch::get_all() ) {
        if( p->has_trait( traits_iter.id )
            && ( !p->has_base_trait( traits_iter.id ) || get_option<bool>( "canmutprofmut" ) )
            && traits_iter.id->purifiable ) {
            // Looks for active mutation
            bool threshlocked = false;
            for( auto cat : traits_iter.category ) {
                if( ( cat == thresh ) && p->crossed_threshold()
                    && ( p->thresh_tier > traits_iter.threshold_tier ) ) {
                    // We shouldn't be able to get rid of mutations that we have a threshold from
                    threshlocked = true;
                    break;
                }
            }
            if( !threshlocked ) {
                valid.push_back( traits_iter.id );
                valid_names.push_back( traits_iter.id->name() );
            }
        }
    }
    if( valid.empty() ) {
        p->add_msg_if_player( _( "You don't have any mutations to purify." ) );
        return 0;
    }

    int mutation_index = uilist( _( "Choose a mutation to purify" ), valid_names );
    if( mutation_index < 0 ) { return 0; }

    p->add_msg_if_player( _( "You inject the purifier.  The liquid thrashes inside the tube and goes "
                             "down reluctantly." ) );

    p->remove_mutation( valid[mutation_index] );
    valid.erase( valid.begin() + mutation_index );

    p->mod_pain( 3 );

    p->i_add( item::spawn( "syringe", it->birthday() ) );
    return it->type->charges_to_use();
}

static void spawn_spores( const player& p )
{
    int spores_spawned = 0;
    map& here = get_map();
    fungal_effects fe( *g, here );
    for( const tripoint_bub_ms& dest : closest_points_first( p.bub_pos(), 4 ) ) {
        if( here.impassable( dest ) ) { continue; }
        float dist = rl_dist( dest, p.bub_pos() );
        if( x_in_y( 1, dist ) ) { fe.marlossify( dest ); }
        if( g->critter_at( dest ) != nullptr ) { continue; }
        if( one_in( 10 + 5 * dist ) && one_in( spores_spawned * 2 ) ) {
            if( monster * const spore = g->place_critter_at( mon_spore, dest ) ) {
                spore->friendly = -1;
                spores_spawned++;
            }
        }
    }
}

static void marloss_common( player& p, item& it, const trait_id& current_color )
{
    static const std::map<trait_id, add_type> mycus_colors = {
        {   {trait_MARLOSS_BLUE, add_type::MARLOSS_B},
            {trait_MARLOSS_YELLOW, add_type::MARLOSS_Y},
            {trait_MARLOSS, add_type::MARLOSS_R}
        }
    };

    if( p.has_trait( current_color ) || p.has_trait( trait_THRESH_MARLOSS ) ) {
        p.add_msg_if_player(
            m_good,
            _( "As you eat the %s, you have a near-religious experience, feeling at one with your "
               "surroundings…" ),
            it.tname() );
        p.add_morale( MORALE_MARLOSS, 100, 1000 );
        for( const std::pair<const trait_id, add_type> &pr : mycus_colors ) {
            if( pr.first != current_color ) { p.add_addiction( pr.second, 50 ); }
        }

        p.set_stored_kcal( p.max_stored_kcal() );
        spawn_spores( p );
        return;
    }

    int marloss_count = std::count_if(
                            mycus_colors.begin(), mycus_colors.end(),
    [&p]( const std::pair<trait_id, add_type> &pr ) { return p.has_trait( pr.first ); } );

    /* If we're not already carriers of current type of Marloss, roll for a random effect:
     * 1 - Mutate
     * 2 - Mutate
     * 3 - Mutate
     * 4 - Purify
     * 5 - Purify
     * 6 - Cleanse radiation + Purify
     * 7 - Fully satiate
     * 8 - Vomit
     * 9-12 - Give Marloss mutation
     */
    int effect = rng( 1, 12 );
    if( effect <= 3 ) {
        p.add_msg_if_player( _( "It tastes extremely strange!" ) );
        p.mutate();
        // Gruss dich, mutation drain, missed you!
        p.mod_pain( 2 * rng( 1, 5 ) );
        p.mod_stored_nutr( 10 );
        p.mod_thirst( 10 );
        p.mod_fatigue( 5 );
    } else if( effect <= 6 ) { // Radiation cleanse is below
        p.add_msg_if_player( m_good, _( "You feel better all over." ) );
        p.mod_painkiller( 30 );
        iuse::purifier( &p, &it, false, p.bub_pos() );
        if( effect == 6 ) { p.set_rad( 0 ); }
    } else if( effect == 7 ) {
        p.add_msg_if_player( m_good, _( "It is delicious, and very filling!" ) );
        p.set_stored_kcal( p.max_stored_kcal() );
    } else if( effect == 8 ) {
        p.add_msg_if_player( m_bad, _( "You take one bite, and immediately vomit!" ) );
        p.vomit();
    } else if( p.crossed_threshold() || p.has_trait( trait_PROF_FERAL ) ) {
        // Mycus Rejection.  Goo already present fights off the fungus.
        p.add_msg_if_player( m_bad, _( "You feel a familiar warmth, but suddenly it surges into an "
                                       "excruciating burn as you convulse, vomiting, and black "
                                       "out…" ) );
        if( p.is_avatar() ) {
            g->memorial()
            .add( pgettext( "memorial_male", "Suffered Marloss Rejection." ),
                  pgettext( "memorial_female", "Suffered Marloss Rejection." ) );
        }
        p.vomit();
        p.mod_pain( 90 );
        p.hurtall( rng( 40, 65 ), nullptr ); // No good way to say "lose half your current HP"
        /** @EFFECT_INT slightly reduces sleep duration when eating mycus+goo */
        p.fall_asleep( 10_hours - p.int_cur * 1_minutes ); // Hope you were eating someplace safe.
        // Mycus v. Goo in your guts is no joke.
        for( const std::pair<const trait_id, add_type> &pr : mycus_colors ) {
            p.unset_mutation( pr.first );
            p.rem_addiction( pr.second );
        }
        p.set_mutation( trait_MARLOSS_AVOID ); // And if you survive it's etched in your RNA, so
        // you're unlikely to repeat the experiment.
    } else if( marloss_count >= 2 ) {
        p.add_msg_if_player( m_bad, _( "You feel a familiar warmth, but suddenly it surges into "
                                       "painful burning as you convulse and collapse to the "
                                       "ground…" ) );
        /** @EFFECT_INT reduces sleep duration when eating wrong color marloss */
        p.fall_asleep( 40_minutes - 1_minutes * p.int_cur / 2 );
        for( const std::pair<const trait_id, add_type> &pr : mycus_colors ) {
            p.unset_mutation( pr.first );
            p.rem_addiction( pr.second );
        }

        p.set_mutation( trait_THRESH_MARLOSS );
        g->m.ter_set( p.bub_pos(), t_marloss );
        g->events().send<event_type::crosses_marloss_threshold>( p.getID() );
        p.add_msg_if_player( m_good, _( "You wake up in a marloss bush.  Almost *cradled* in it, "
                                        "actually, as though it grew there for you." ) );
        p.add_msg_if_player(
            m_good,
            //~ Beginning to hear the Mycus while conscious: that's it speaking
            _( "unity.  together we have reached the door.  we provide the final key.  now to pass "
               "through…" ) );
    } else {
        p.add_msg_if_player( _( "You feel a strange warmth spreading throughout your body…" ) );
        p.set_mutation( current_color );
        // Give us addictions to the other two colors, but cure one for current color
        for( const std::pair<const trait_id, add_type> &pr : mycus_colors ) {
            if( pr.first == current_color ) {
                p.rem_addiction( pr.second );
            } else {
                p.add_addiction( pr.second, 60 );
            }
        }
    }
}

static bool marloss_prevented( const player& p )
{
    if( p.is_npc() ) { return true; }
    if( p.has_trait( trait_MARLOSS_AVOID ) ) {
        p.add_msg_if_player(
            m_warning,
            //~ "Uh-uh" is a sound used for "nope", "no", etc.
            _( "After what happened that last time?  uh-uh.  You're not eating that alien poison." ) );
        return true;
    }
    if( p.has_trait( trait_THRESH_MYCUS ) ) {
        p.add_msg_if_player( m_info, _( "We no longer require this scaffolding.  We reserve it for "
                                        "other uses." ) );
        return true;
    }

    return false;
}

int iuse::marloss( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( marloss_prevented( *p ) ) { return 0; }

    g->events().send<event_type::consumes_marloss_item>( p->getID(), it->typeId() );

    marloss_common( *p, *it, trait_MARLOSS );
    return it->type->charges_to_use();
}

int iuse::marloss_seed( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( !query_yn( _( "Sure you want to eat the %s?  You could plant it in a mound of dirt." ),
                   colorize( it->tname(), it->color_in_inventory() ) ) ) {
        return 0; // Save the seed for later!
    }

    if( marloss_prevented( *p ) ) { return 0; }

    g->events().send<event_type::consumes_marloss_item>( p->getID(), it->typeId() );

    marloss_common( *p, *it, trait_MARLOSS_BLUE );
    return it->type->charges_to_use();
}

int iuse::marloss_gel( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( marloss_prevented( *p ) ) { return 0; }

    g->events().send<event_type::consumes_marloss_item>( p->getID(), it->typeId() );

    marloss_common( *p, *it, trait_MARLOSS_YELLOW );
    return it->type->charges_to_use();
}

int iuse::mycus( player* p, item* it, bool t, const tripoint_bub_ms& pos )
{
    if( p->is_npc() ) { return it->type->charges_to_use(); }
    // Welcome our guide.  Welcome.  To. The Mycus.
    if( p->has_trait( trait_THRESH_MARLOSS ) ) {
        g->events().send<event_type::crosses_mycus_threshold>( p->getID() );
        p->add_msg_if_player( m_neutral, _( "It tastes amazing, and you finish it quickly." ) );
        p->add_msg_if_player( m_good, _( "You feel better all over." ) );
        p->mod_painkiller( 30 );
        purifier( p, it, t, pos ); // Clear out some of that goo you may have floating around
        p->set_rad( 0 );
        p->healall( 4 ); // Can't make you a whole new person, but not for lack of trying
        p->add_msg_if_player( m_good, _( "As it settles in, you feel ecstasy radiating through every "
                                         "part of your body…" ) );
        p->add_morale( MORALE_MARLOSS, 1000, 1000 ); // Last time you'll ever have it this good.  So
        // enjoy.
        p->add_msg_if_player( m_good, _( "Your eyes roll back in your head.  Everything dissolves "
                                         "into a blissful haze…" ) );
        /** @EFFECT_INT slightly reduces sleep duration when eating mycus */
        p->fall_asleep( 5_hours - p->int_cur * 1_minutes );
        p->unset_mutation( trait_THRESH_MARLOSS );
        p->set_mutation( trait_THRESH_MYCUS );
        // Cleanse fungal infections
        p->remove_effect( effect_fungus );
        p->remove_effect( effect_spores );
        g->invalidate_main_ui_adaptor();
        //~ The Mycus does not use the term (or encourage the concept of) "you".  The PC is a
        //local/native organism, but is now the Mycus. ~ It still understands the concept, but
        //uninitelligent fungaloids and mind-bent symbiotes should not need it. ~ We are the Mycus.
        popup( _( "We welcome into us.  We have endured long in this forbidding world." ) );
        p->add_msg_if_player( " " );
        p->add_msg_if_player( m_good, _( "A sea of white caps, waving gently.  A haze of spores "
                                         "wafting silently over a forest." ) );
        g->invalidate_main_ui_adaptor();
        popup( _( "The natives have a saying: \"E Pluribus Unum.\"  Out of many, one." ) );
        p->add_msg_if_player( " " );
        p->add_msg_if_player( m_good, _( "The blazing pink redness of the berry.  The juices "
                                         "spreading across your tongue, the warmth draping over us "
                                         "like a lover's embrace." ) );
        g->invalidate_main_ui_adaptor();
        popup( _( "We welcome the union of our lines in our local guide.  We will prosper, and unite "
                  "this world.  Even now, our fruits adapt to better serve local physiology." ) );
        p->add_msg_if_player( " " );
        p->add_msg_if_player( m_good, _( "The sky-blue of the seed.  The nutty, creamy flavors "
                                         "intermingling with the berry, a memory that will never "
                                         "leave us." ) );
        g->invalidate_main_ui_adaptor();
        popup( _( "As, in time, shall we adapt to better welcome those who have not received us." ) );
        p->add_msg_if_player( " " );
        p->add_msg_if_player( m_good, _( "The amber-yellow of the sap.  Feel it flowing through our "
                                         "veins, taking the place of the strange, thin red gruel "
                                         "called \"blood.\"" ) );
        g->invalidate_main_ui_adaptor();
        popup( _( "We are the Mycus." ) );
        /*p->add_msg_if_player( m_good,
                              _( "We welcome into us.  We have endured long in this forbidding
        world." ) ); p->add_msg_if_player( m_good,
                              _( "The natives have a saying: \"E Pluribus Unum\"  Out of many, one."
        ) ); p->add_msg_if_player( m_good,
                              _( "We welcome the union of our lines in our local guide.  We will
        prosper, and unite this world." ) ); p->add_msg_if_player( m_good, _( "Even now, our fruits
        adapt to better serve local physiology." ) ); p->add_msg_if_player( m_good,
                              _( "As, in time, shall we adapt to better welcome those who have not
        received us." ) );*/
        fungal_effects fe( *g, g->m );
        for( const tripoint_bub_ms& nearby_pos : g->m.points_in_radius( p->bub_pos(), 3 ) ) {
            fe.marlossify( nearby_pos );
        }
        p->rem_addiction( add_type::MARLOSS_R );
        p->rem_addiction( add_type::MARLOSS_B );
        p->rem_addiction( add_type::MARLOSS_Y );
    } else if( p->has_trait( trait_THRESH_MYCUS ) && !p->has_trait( trait_M_DEPENDENT ) ) { // OK, now
        // set the
        // hook.
        if( !one_in( 3 ) ) {
            p->mutate_category( mutation_category_id( "MYCUS" ) );
            p->mod_stored_nutr( 10 );
            p->mod_thirst( 10 );
            p->mod_fatigue( 5 );
            p->add_morale( MORALE_MARLOSS, 25, 200 ); // still covers up mutation pain
        }
    } else if( p->has_trait( trait_THRESH_MYCUS ) ) {
        p->mod_painkiller( 5 );
        p->mod_stim( 5 );
    } else { // In case someone gets one without having been adapted first.
        // Marloss is the Mycus' method of co-opting humans.  Mycus fruit is for symbiotes'
        // maintenance and development.
        p->add_msg_if_player( _( "This tastes really weird!  You're not sure it's good for you…" ) );
        p->mutate();
        p->mod_pain( 2 * rng( 1, 5 ) );
        p->mod_stored_nutr( 10 );
        p->mod_thirst( 10 );
        p->mod_fatigue( 5 );
        p->vomit(); // no hunger/quench benefit for you
        p->mod_healthy_mod( -8, -50 );
    }
    return it->type->charges_to_use();
}

int iuse::petfood( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( !it->is_comestible() ) {
        p->add_msg_if_player( _( "You doubt someone would want to eat %1$s." ), it->tname() );
        return 0;
    }

    const std::optional<tripoint_bub_ms> pnt_ = choose_adjacent(
            string_format( _( "Tame which animal with the %s?" ), it->tname() ) );
    if( !pnt_ ) { return 0; }
    const auto pnt = *pnt_;
    p->moves -= to_moves<int>( 1_seconds );

    // First a check to see if we are trying to feed a NPC dog food.
    if( g->critter_at<npc>( pnt ) != nullptr ) {
        if( npc * const person_ = g->critter_at<npc>( pnt ) ) {
            npc& person = *person_;
            if( query_yn( _( "Are you sure you want to feed a person the pet food?" ) ) ) {
                p->add_msg_if_player(
                    _( "You put your %1$s into %2$s's mouth!" ), it->tname(), person.name );
                if( person.is_ally( *p ) || x_in_y( 9, 10 ) ) {
                    person.say( _( "Okay, but please, don't give me this again.  I don't want to eat "
                                   "pet food in the cataclysm all day." ) );
                    p->consume_charges( *it, 1 );
                    return 0;
                } else {
                    p->add_msg_if_player( _( "%s knocks it out from your hand!" ), person.name );
                    person.make_angry();
                    p->consume_charges( *it, 1 );
                    return 0;
                }
            } else {
                p->add_msg_if_player( _( "Never mind." ) );
                return 0;
            }
        }

        // Then monsters.
    } else if( monster * const mon_ptr = g->critter_at<monster>( pnt, true ) ) {
        monster& mon = *mon_ptr;

        if( mon.is_hallucination() ) {
            p->add_msg_if_player(
                _( "You try to feed the %s some %s, but it vanishes!" ), mon.type->nname(),
                it->tname() );
            mon.die( nullptr );
            return 0;
        }

        // Feral survivors don't get to tame normal critters.
        if( p->has_trait( trait_PROF_FERAL ) ) {
            // TODO: Allow player ferals to tame zombie animals, but make sure non-feral players
            // can't tame them, and for flavor possibly only allow taming with meat-based items.
            p->add_msg_if_player(
                _( "You reach for the %s, but it recoils away from you!" ), mon.type->nname() );
            return 0;
        }

        // check to see if the item has a petfood data entry deterimine if the item can be fed to a
        // bet
        bool can_feed = false;
        const pet_food_data& petfood = mon.type->petfood;
        const std::set<std::string> &itemfood = it->get_comestible()->petfood;
        if( !petfood.food.empty() ) {
            for( const std::string& food : petfood.food ) {
                if( itemfood.contains( food ) ) {
                    can_feed = true;
                    break;
                }
            }
        }

        // if the item cannot be fed, give a message to the player and return
        if( !can_feed ) {
            p->add_msg_if_player( _( "The %s doesn't want that kind of food." ), mon.type->nname() );
            return 0;
        }

        if( !petfood.tamer_traits.empty() ) {
            for( const TraitSet& trait_set : petfood.tamer_traits ) {
                if( !p->has_one_of_traits( trait_set ) ) {
                    can_feed = false;
                } else {
                    can_feed = true;
                }
            }
            if( !can_feed ) {
                p->add_msg_if_player( _( "The %s does not trust your kind." ), mon.type->nname() );
                return 0;
            }
        }

        if( mon.type->id == mon_dog_thing ) {
            p->deal_damage( &mon, bodypart_id( "hand_r" ), damage_instance( DT_CUT, rng( 1, 10 ) ) );
            p->add_msg_if_player( m_bad, _( "You want to feed it the pet food, but it bites your "
                                            "fingers!" ) );
            if( one_in( 5 ) ) {
                p->add_msg_if_player( _( "Apparently it's more interested in your flesh than the pet "
                                         "food in your hand!" ) );
                p->consume_charges( *it, 1 );
                return 0;
            }
        }

        if( mon.is_pet() && mon.has_effect( effect_well_fed ) ) {
            if( !query_yn( _( "The %s is already well-fed.  Feed it anyway?" ), mon.get_name() ) ) {
                p->add_msg_if_player( _( "Never mind." ) );
                return 0;
            }
        }

        p->add_msg_if_player( _( "You feed your %1$s to the %2$s." ), it->tname(), mon.get_name() );

        if( petfood.feed.empty() ) {
            p->add_msg_if_player( _( "The %1$s is your pet now!" ), mon.get_name() );
        } else {
            p->add_msg_if_player( _( petfood.feed ), mon.get_name() );
        }

        mon.make_pet();

        // Apply well_fed effect to improve monster productivity
        // This effect increases reproduction rate, milk production, growth speed, and HP recovery
        // Duration: 24 hours (one full day cycle)
        const time_duration well_fed_duration = 24_hours;

        if( mon.has_effect( effect_well_fed ) ) {
            // Refresh duration if already well-fed
            mon.add_effect( effect_well_fed, well_fed_duration );
        } else {
            // Apply new well-fed effect
            mon.add_effect( effect_well_fed, well_fed_duration );
            p->add_msg_if_player(
                m_good, _( "The %s looks healthier and more productive." ), mon.get_name() );
        }

        p->consume_charges( *it, 1 );
        return 0;
    }

    p->add_msg_if_player( _( "There is nothing to be fed here." ) );
    return 0;
}

int iuse::radio_mod( player* p, item*, bool, const tripoint_bub_ms & )
{
    if( p->is_npc() ) {
        // Now THAT would be kinda cruel
        return 0;
    }

    auto filter = []( const item & itm ) { return itm.has_flag( flag_RADIO_MODABLE ); };

    // note: if !p->is_npc() then p is avatar
    item* loc = game_menus::inv::titled_filter_menu( filter, *p->as_avatar(), _( "Modify what?" ) );

    if( !loc ) {
        p->add_msg_if_player( _( "You do not have that item!" ) );
        return 0;
    }
    item& modded = *loc;

    int choice = uilist(
                     _( "Which signal should activate the item?" ), {_( "\"Red\"" ), _( "\"Blue\"" ), _( "\"Green\"" )} );

    flag_id newtag;
    std::string colorname;
    switch( choice ) {
        case 0:
            newtag = flag_RADIOSIGNAL_1;
            colorname = _( "\"Red\"" );
            break;
        case 1:
            newtag = flag_RADIOSIGNAL_2;
            colorname = _( "\"Blue\"" );
            break;
        case 2:
            newtag = flag_RADIOSIGNAL_3;
            colorname = _( "\"Green\"" );
            break;
        default:
            return 0;
    }

    if( modded.has_flag( flag_RADIO_MOD ) && modded.has_flag( newtag ) ) {
        p->add_msg_if_player( _( "This item has been modified this way already." ) );
        return 0;
    }

    remove_radio_mod( modded, *p );

    p->add_msg_if_player(
        _( "You modify your %1$s to listen for %2$s activation signal on the radio." ),
        modded.tname(), colorname );
    modded.set_flag( flag_RADIO_ACTIVATION );
    modded.set_flag( flag_RADIOCARITEM );
    modded.set_flag( flag_RADIO_MOD );
    modded.set_flag( newtag );
    return 1;
}

int iuse::remove_all_mods( player* p, item*, bool, const tripoint_bub_ms & )
{
    if( !p ) { return 0; }

    item* loc = g->inv_map_splice(
    []( const item & e ) {
        for( const item * it : e.toolmods() ) {
            if( !it->is_irremovable() ) { return true; }
        }
        if( e.has_flag( flag_RADIO_MOD ) ) { return true; }
        return false;
    },
    _( "Remove mods from tool?" ), 1, _( "You don't have any modified tools." ) );

    if( !loc ) {
        add_msg( m_info, _( "Never mind." ) );
        return 0;
    }

    if( !loc->ammo_remaining() || avatar_funcs::unload_item( *p->as_avatar(), *loc ) ) {
        bool done = false;
        loc->contents.remove_top_items_with( [&p, &done]( detached_ptr<item>&& e ) {
            if( !done && e->is_toolmod() && !e->is_irremovable() ) {
                done = true;
                add_msg( m_info, _( "You remove the %s from the tool." ), e->tname() );
                return p->i_add_or_drop( std::move( e ) );
            }
            return std::move( e );
        } );

        remove_radio_mod( *loc, *p );
    }
    return 0;
}
// Returns 0-5 based on how good the fishing spot is, 5 being "Middle of the
// ocean" and 0 being "no"
int iuse::good_fishing_spot( const tripoint_bub_ms& pos )
{
    int fishable_locations = g->get_fishable_locations( 60, pos ).size();
    map& here = get_map();
    const oter_id& cur_omt =
        get_overmapbuffer( get_map().get_bound_dimension() )
        .ter( tripoint_abs_omt( project_to<coords::omt>( here.bub_to_abs( pos ) ) ) );
    std::string om_id = cur_omt.id().c_str();
    if( fishable_locations < 100 && !g->m.has_flag( "CURRENT", pos )
        && om_id.find( "river_" ) == std::string::npos && !cur_omt->is_lake()
        && !cur_omt->is_lake_shore() ) {
        return 0;
    }
    // I hate I cant use a switch for this.
    // Tiles in range is 144k, so knock off 14k to be nice for max fishing e-'fish'-iency.
    if( fishable_locations >= 10400 ) {
        return 5;
    } else if( fishable_locations < 10400 && fishable_locations >= 7800 ) {
        return 4;
    } else if( fishable_locations < 7800 && fishable_locations >= 5200 ) {
        return 3;
    } else if( fishable_locations < 5200 && fishable_locations >= 2600 ) {
        return 2; // If you cant amass a 10x10 for fishing womp womp.
    } else if( fishable_locations < 2600 && fishable_locations >= 100 ) {
        return 1;
    }
    g->u.add_msg_if_player( m_info, _( "You doubt you will catch anything here, best look "
                                       "elsewhere" ) );
    return 0;
}

int iuse::fishing_rod( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_npc() ) {
        // Long actions - NPCs don't like those yet.
        return 0;
    }
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    std::optional<tripoint_bub_ms> found;
    for( const tripoint_bub_ms& pnt : g->m.points_in_radius( p->bub_pos(), 1 ) ) {
        if( g->m.has_flag( flag_FISHABLE, pnt ) && iuse::good_fishing_spot( pnt ) != 0 ) {
            found = pnt;
            break;
        }
    }
    if( !found ) {
        p->add_msg_if_player( m_info, _( "You can't fish there!" ) );
        return 0;
    }
    switch( iuse::good_fishing_spot( *found ) ) {
        case 1: {
            p->add_msg_if_player( m_info, _( "You doubt you will catch too much here, but surely "
                                             "theres some" ) );
            break;
        }
        case 2: {
            p->add_msg_if_player( m_info, _( "You think there might be a few things to catch here" ) );
            break;
        }
        case 3: {
            p->add_msg_if_player( m_info, _( "You see at least one catch, should be a decent spot" ) );
            break;
        }
        case 4: {
            p->add_msg_if_player( m_info, _( "You see several catches already, this should be a "
                                             "great spot" ) );
            break;
        }
        case 5: {
            p->add_msg_if_player( m_info, _( "You see a massive number of catches here, this is as "
                                             "good as it gets" ) );
            break;
        }
    }
    p->add_msg_if_player( _( "You cast your line and wait to hook something…" ) );
    p->assign_activity(
        std::make_unique<player_activity>(
            std::make_unique<fish_activity_actor>( it, bub_to_abs( *found ) ) ),
        to_moves<int>( 5_hours ) );
    const auto fishable_locations = g->get_fishable_locations( 60, *found );
    p->activity->coord_set.reserve( fishable_locations.size() );
    std::ranges::transform(
        fishable_locations, std::inserter( p->activity->coord_set, p->activity->coord_set.end() ),
    []( const tripoint_bub_ms & pnt ) { return g->m.bub_to_abs( pnt ); } );
    return 0;
}

int iuse::fish_trap( player* p, item* it, bool t, const tripoint_bub_ms& pos )
{
    if( !t ) {
        // Handle deploying fish trap.
        if( it->is_active() ) {
            it->deactivate();
            return 0;
        }

        if( it->charges < 0 ) {
            it->charges = 0;
            return 0;
        }
        if( p->is_mounted() ) {
            p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
            return 0;
        }
        if( p->is_underwater() ) {
            p->add_msg_if_player( m_info, _( "You can't do that while underwater." ) );
            return 0;
        }

        if( it->charges == 0 ) {
            p->add_msg_if_player( _( "Fish are not foolish enough to go in here without bait." ) );
            return 0;
        }

        const std::optional<tripoint_bub_ms> pnt_ = choose_adjacent( _( "Put fish trap where?" ) );
        if( !pnt_ ) { return 0; }
        const auto pnt = *pnt_;

        if( !g->m.has_flag( "FISHABLE", pnt ) ) {
            p->add_msg_if_player( m_info, _( "You can't fish there!" ) );
            return 0;
        }
        if( good_fishing_spot( pnt ) == 0 ) { return 0; }
        it->activate();
        it->set_age( 0_turns );
        g->m.add_item_or_charges( pnt, it->detach() );
        p->add_msg_if_player( m_info, _( "You place the fish trap, in three hours or so you may "
                                         "catch some fish." ) );

        return 0;

    } else {
        // Handle processing fish trap over time.
        if( it->charges == 0 ) {
            it->deactivate();
            return 0;
        }
        if( it->age() > 3_hours ) {
            it->deactivate();

            if( !g->m.has_flag( "FISHABLE", pos ) ) { return 0; }
            int fish = good_fishing_spot( pos );
            int success = -250 + ( good_fishing_spot( pos ) * 15 );
            const int surv_mod = p->get_skill_level( skill_survival ) + ( fish );
            for( int i = 0; i < it->charges; i++ ) { success += surv_mod + fish; }

            it->charges = 0;

            int caught = 0;

            if( success >= 200 ) {
                caught = rng( 2, 4 );
            } else if( success < 200 && success >= 100 ) {
                caught = rng( 1, 2 );
            } else if( success < 100 && success >= 50 ) {
                caught = rng( 0, 2 );
            } else if( success < 50 && success >= 0 ) {
                caught = rng( 0, 1 );
            } else {
                caught = 0;
            }

            if( caught == 0 ) {
                p->practice( skill_survival, rng( 10, 25 ) );
                return 0;
            }
            for( int i = 0; i < caught; i++ ) {
                p->practice( skill_survival, rng( 4, 8 ) );
                const std::vector<mtype_id> fish_group = MonsterGroupManager::GetMonstersFromGroup(
                        GROUP_FISH );
                const mtype_id& fish_mon = random_entry_ref( fish_group );
                g->m.add_item_or_charges(
                    pos, item::make_corpse( fish_mon, it->birthday() + rng( 0_turns, 3_hours ) ) );
            }
        }
        return 0;
    }
}


int iuse::teleport( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_npc() ) {
        // That would be evil
        return 0;
    }
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    if( !it->ammo_sufficient() ) { return 0; }
    p->moves -= to_moves<int>( 1_seconds );
    teleport::teleport( *p );
    return it->type->charges_to_use();
}


int iuse::tazer( player* p, item* it, bool, const tripoint_bub_ms& pos )
{
    if( !it->units_sufficient( *p ) ) { return 0; }

    auto pnt = pos;
    if( pos == p->bub_pos() ) {
        const std::optional<tripoint_bub_ms> pnt_ = choose_adjacent( _( "Shock where?" ) );
        if( !pnt_ ) { return 0; }
        pnt = *pnt_;
    }

    if( pnt == p->bub_pos() ) {
        p->add_msg_if_player( m_info, _( "Umm.  No." ) );
        return 0;
    }

    Creature* target = g->critter_at( pnt, true );
    if( target == nullptr ) {
        p->add_msg_if_player( _( "There's nothing to zap there!" ) );
        return 0;
    }

    npc* foe = dynamic_cast<npc *>( target );
    if( foe != nullptr && !foe->is_enemy()
        && !p->query_yn( _( "Really shock %s?" ), target->disp_name() ) ) {
        return 0;
    }

    /** @EFFECT_DEX slightly increases chance of successfully using tazer */
    /** @EFFECT_MELEE increases chance of successfully using a tazer */
    int numdice = 3 + ( p->dex_cur / 2.5 ) + p->get_skill_level( skill_melee ) * 2;
    p->moves -= to_moves<int>( 1_seconds );

    /** @EFFECT_DODGE increases chance of dodging a tazer attack */
    const bool tazer_was_dodged = dice( numdice, 10 ) < dice( target->get_dodge(), 10 );
    if( tazer_was_dodged ) {
        p->add_msg_player_or_npc(
            _( "You attempt to shock %s, but miss." ),
            _( "<npcname> attempts to shock %s, but misses." ), target->disp_name() );
    } else {
        // TODO: Maybe - Execute an attack and maybe zap something other than torso
        // Maybe, because it's torso (heart) that fails when zapped with electricity
        int dam =
            target->deal_damage( p, bodypart_id( "torso" ), damage_instance( DT_ELECTRIC, rng( 5, 25 ) ) )
            .total_damage();
        if( dam > 0 ) {
            p->add_msg_player_or_npc(
                m_good, _( "You shock %s!" ), _( "<npcname> shocks %s!" ), target->disp_name() );
        } else {
            p->add_msg_player_or_npc(
                m_warning, _( "You unsuccessfully attempt to shock %s!" ),
                _( "<npcname> unsuccessfully attempts to shock %s!" ), target->disp_name() );
        }
    }

    if( foe != nullptr ) { foe->on_attacked( *p ); }

    return it->type->charges_to_use();
}

int iuse::tazer2( player* p, item* it, bool b, const tripoint_bub_ms& pos )
{
    if( it->ammo_remaining() >= 100 ) {
        // Instead of having a ctrl+c+v of the function above, spawn a fake tazer and use it
        // Ugly, but less so than copied blocks
        item* fake = item::spawn_temporary( "tazer", calendar::start_of_cataclysm );
        fake->charges = 100;
        return tazer( p, fake, b, pos );
    } else {
        p->add_msg_if_player( m_info, _( "Insufficient power" ) );
    }

    return 0;
}

static std::string get_music_description()
{
    const std::array<std::string, 5> descriptions = {
        {
            translate_marker( "a sweet guitar solo!" ), translate_marker( "a funky bassline." ),
            translate_marker( "some amazing vocals." ), translate_marker( "some pumping bass." ),
            translate_marker( "dramatic classical music." )
        }
    };

    if( one_in( 50 ) ) { return _( "some bass-heavy post-glam speed polka." ); }

    size_t i = static_cast<size_t>( rng( 0, descriptions.size() * 2 ) );
    if( i < descriptions.size() ) { return _( descriptions[i] ); }
    // Not one of the hard-coded versions, let's apply a random string made up
    // of snippets {a, b, c}, but only a 50% chance
    // Actual chance = 24.5% of being selected
    if( one_in( 2 ) ) {
        return SNIPPET.expand(
                   SNIPPET.random_from_category( "<music_description>" )
                   .value_or( translation() )
                   .translated() );
    }

    return _( "a sweet guitar solo!" );
}

void iuse::play_music(
    player& p, const tripoint_bub_ms& source, const int volume, const int max_morale )
{
    // TODO: what about other "player", e.g. when a NPC is listening or when the PC is listening,
    // the other characters around should be able to profit as well.
    const bool do_effects = p.can_hear( source, volume ) && !p.has_effect( effect_sleep );
    std::string sound = "music";
    if( calendar::once_every( 1_hours ) ) {
        // Every 5 minutes, describe the music
        const std::string music = get_music_description();
        if( !music.empty() ) {
            sound = music;
            // descriptions aren't printed for sounds at our position
            if( p.bub_pos() == source && p.can_hear( source, volume ) ) {
                p.add_msg_if_player( _( "You listen to %s" ), music );
            }
        }
    }
    // do not process mp3 player
    if( volume != 0 ) { sounds::ambient_sound( source, volume, sounds::sound_t::music, sound ); }
    if( do_effects ) {
        p.add_effect( effect_music, 1_turns );
        p.add_morale( MORALE_MUSIC, 1, max_morale, 5_minutes, 2_minutes, true );
        // mp3 player reduces hearing
        if( volume == 0 ) { p.add_effect( effect_earphones, 1_turns ); }
    }
}

int iuse::mp3_on( player* p, item* it, bool t, const tripoint_bub_ms& pos )
{
    if( t ) { // Normal use
        if( p->has_item( *it ) ) {
            // mp3 player in inventory, we can listen
            play_music( *p, pos, 0, 20 );
        }
    } else { // Turning it off
        // Creatively make it so that the reversion isn't hard-coded
        // There's *probably* a better way to do this, but this works
        std::string active_item = it->typeId().str();
        std::string base_item = active_item.erase( active_item.rfind( '_' ) );

        p->add_msg_if_player( _( "The %s turns off." ), it->display_name() );
        it->convert( itype_id( base_item ) );
        it->deactivate();

        p->mod_moves( -200 );
    }
    return it->type->charges_to_use();
}

int iuse::rpgdie( player* you, item* die, bool, const tripoint_bub_ms & )
{
    if( you->is_mounted() ) {
        you->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    int num_sides = die->get_var( "die_num_sides", 0 );
    if( num_sides == 0 ) {
        const std::vector<int> sides_options = {4, 6, 8, 10, 12, 20, 50};
        const int sides = sides_options[rng( 0, sides_options.size() - 1 )];
        num_sides = sides;
        die->set_var( "die_num_sides", sides );
    }
    const int roll = rng( 1, num_sides );
    //~ %1$d: roll number, %2$d: side number of a die, %3$s: die item name
    you->add_msg_if_player(
        pgettext( "dice", "You roll a %1$d on your %2$d sided %3$s" ), roll, num_sides, die->tname() );
    if( roll == num_sides ) { add_msg( m_good, _( "Critical!" ) ); }
    return 0;
}

int iuse::dive_tank( player* p, item* it, bool t, const tripoint_bub_ms & )
{
    if( t ) { // Normal use
        if( p->is_worn( *it ) ) {
            if( p->is_underwater() && p->oxygen < 10 ) { p->oxygen += 20; }
            if( one_in( 15 ) ) {
                p->add_msg_if_player( m_bad, _( "You take a deep breath from your %s." ), it->tname() );
            }
            if( it->charges == 0 ) {
                p->add_msg_if_player( m_bad, _( "Air in your %s runs out." ), it->tname() );
                it->set_var( "overwrite_env_resist", 0 );
                it->convert( itype_id( it->typeId().str().substr( 0, it->typeId().str().size() - 3 ) ) );
                it->deactivate(); // 3 = "_on"
            }
        } else { // not worn = off thanks to on-demand regulator
            it->set_var( "overwrite_env_resist", 0 );
            it->convert( itype_id( it->typeId().str().substr( 0, it->typeId().str().size() - 3 ) ) );
            it->deactivate(); // 3 = "_on"
        }

    } else { // Turning it on/off
        if( it->charges == 0 ) {
            p->add_msg_if_player( _( "Your %s is empty." ), it->tname() );
        } else if( it->is_active() ) { // off
            p->add_msg_if_player( _( "You turn off the regulator and close the air valve." ) );
            it->set_var( "overwrite_env_resist", 0 );
            it->convert( itype_id( it->typeId().str().substr( 0, it->typeId().str().size() - 3 ) ) );
            it->deactivate(); // 3 = "_on"
        } else {              // on
            if( !p->is_worn( *it ) ) {
                p->add_msg_if_player( _( "You should wear it first." ) );
            } else {
                p->add_msg_if_player( _( "You turn on the regulator and open the air valve." ) );
                it->set_var( "overwrite_env_resist", it->get_base_env_resist_w_filter() );
                it->convert( itype_id( it->typeId().str() + "_on" ) );
                it->activate();
            }
        }
    }
    if( it->charges == 0 ) {
        it->set_var( "overwrite_env_resist", 0 );
        it->convert( itype_id( it->typeId().str().substr( 0, it->typeId().str().size() - 3 ) ) );
        it->deactivate(); // 3 = "_on"
    }
    return it->type->charges_to_use();
}

int iuse::solarpack( player* p, item* it, bool, const tripoint_bub_ms & )
{
    const bionic_id rem_bid = p->get_remote_fueled_bionic();
    if( rem_bid.is_empty() ) { // Cable CBM required
        p->add_msg_if_player( _( "You have no cable charging system to plug it in, so you leave it "
                                 "alone." ) );
        return 0;
    } else if( !p->has_active_bionic( rem_bid ) ) { // when OFF it takes no effect
        p->add_msg_if_player( _( "Activate your cable charging system to take advantage of it." ) );
    }

    if( it->is_armor() && !( p->is_worn( *it ) ) ) {
        p->add_msg_if_player(
            m_neutral, _( "You need to wear the %1$s before you can unfold it." ), it->tname() );
        return 0;
    }
    // no doubled sources of power
    if( p->worn_with_flag( flag_SOLARPACK_ON ) ) {
        p->add_msg_if_player(
            m_neutral, _( "You cannot use the %1$s with another of it's kind." ), it->tname() );
        return 0;
    }
    p->add_msg_if_player( _( "You unfold solar array from the pack.  You still need to connect it "
                             "with a cable." ) );

    it->convert( itype_id( it->typeId().str() + "_on" ) );
    return 0;
}

int iuse::solarpack_off( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( !p->is_worn( *it ) ) { // folding when not worn
        p->add_msg_if_player( _( "You fold your portable solar array into the pack." ) );
    } else {
        p->add_msg_if_player( _( "You unplug and fold your portable solar array into the pack." ) );
    }

    // 3 = "_on"
    it->convert( itype_id( it->typeId().str().substr( 0, it->typeId().str().size() - 3 ) ) );
    it->deactivate();
    return 0;
}

int iuse::gasmask( player* p, item* it, bool t, const tripoint_bub_ms& pos )
{
    if( t ) { // Normal use
        if( p->is_worn( *it ) ) {
            // calculate amount of absorbed gas per filter charge
            const field& gasfield = g->m.field_at( pos );
            for( auto& dfield : gasfield ) {
                const field_entry& entry = dfield.second;
                if( entry.get_gas_absorption_factor() > 0 ) {
                    it->set_var( "gas_absorbed",
                                 it->get_var( "gas_absorbed", 0 ) + entry.get_gas_absorption_factor() );
                }
            }
            if( it->get_var( "gas_absorbed", 0 ) >= 100 ) {
                it->ammo_consume( 1, p->bub_pos() );
                it->set_var( "gas_absorbed", 0 );
            }
            if( it->charges == 0 ) {
                p->add_msg_player_or_npc(
                    m_bad, _( "Your %s requires new filter!" ),
                    _( "<npcname> needs new gas mask filter!" ), it->tname() );
            }
        }
    } else { // activate
        if( it->charges == 0 ) {
            p->add_msg_if_player( _( "Your %s don't have a filter." ), it->tname() );
        } else {
            p->add_msg_if_player( _( "You prepared your %s." ), it->tname() );
            it->activate();
            it->set_var( "overwrite_env_resist", it->get_base_env_resist_w_filter() );
        }
    }
    if( it->charges == 0 ) {
        it->set_var( "overwrite_env_resist", 0 );
        it->deactivate();
    }
    return it->type->charges_to_use();
}

int iuse::portable_game( player* p, item* it, bool t, const tripoint_bub_ms & )
{
    if( t ) { return 0; }
    if( p->is_npc() ) {
        // Long action
        return 0;
    }
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    if( p->is_underwater() ) {
        p->add_msg_if_player( m_info, _( "You can't do that while underwater." ) );
        return 0;
    }
    if( p->has_trait( trait_ILLITERATE ) ) {
        p->add_msg_if_player( m_info, _( "You're illiterate!" ) );
        return 0;
    } else if( it->units_remaining( *p ) < ( it->ammo_required() * 15 ) ) {
        p->add_msg_if_player( m_info, _( "You don't have enough charges to play." ) );
        return 0;
    } else {
        std::string loaded_software = "robot_finds_kitten";

        uilist as_m;
        as_m.text = _( "What do you want to play?" );
        as_m.entries.emplace_back( 1, true, '1', _( "Robot finds Kitten" ) );
        as_m.entries.emplace_back( 2, true, '2', _( "S N A K E" ) );
        as_m.entries.emplace_back( 3, true, '3', _( "Sokoban" ) );
        as_m.entries.emplace_back( 4, true, '4', _( "Minesweeper" ) );
        as_m.entries.emplace_back( 5, true, '5', _( "Lights on!" ) );
        as_m.entries.emplace_back( 6, true, '6', _( "Play anything for a while" ) );
        as_m.query();

        switch( as_m.ret ) {
            case 1:
                loaded_software = "robot_finds_kitten";
                break;
            case 2:
                loaded_software = "snake_game";
                break;
            case 3:
                loaded_software = "sokoban_game";
                break;
            case 4:
                loaded_software = "minesweeper_game";
                break;
            case 5:
                loaded_software = "lightson_game";
                break;
            case 6:
                loaded_software = "null";
                break;
            default:
                // Cancel
                return 0;
        }

        // Play in 15-minute chunks
        const int moves = to_moves<int>( 15_minutes );

        p->add_msg_if_player( _( "You play on your %s for a while." ), it->tname() );
        p->assign_activity( std::make_unique<player_activity>(
                                std::make_unique<game_activity_actor>( game_type::GAME, safe_reference<item>( it ) ) ) );
        std::string end_message;
        end_message.clear();
        int game_score = 0;

        play_videogame( loaded_software, end_message, game_score );

        if( !end_message.empty() ) { p->add_msg_if_player( end_message ); }

        if( game_score != 0 ) {
            p->add_morale( MORALE_GAME, game_score, 60, 2_hours, 30_minutes, true );
        }
    }
    return 0;
}

int iuse::vibe( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_npc() ) {
        // Long action
        // Also, that would be creepy as fuck, seriously
        return 0;
    }
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do… that while mounted." ) );
        return 0;
    }
    if( ( p->is_underwater() )
        && ( !( ( p->has_trait( trait_GILLS ) ) || ( p->has_trait( trait_GILLS_CEPH ) )
                || ( p->is_wearing( itype_rebreather_on ) ) || ( p->is_wearing( itype_rebreather_xl_on ) )
                || ( p->is_wearing( itype_mask_h20survivor_on ) ) ) ) ) {
        p->add_msg_if_player( m_info, _( "It's waterproof, but oxygen maybe?" ) );
        return 0;
    }
    if( !it->units_sufficient( *p ) ) {
        p->add_msg_if_player( m_info, _( "The %s's batteries are dead." ), it->tname() );
        return 0;
    }
    if( p->get_fatigue() >= fatigue_levels::dead_tired ) {
        p->add_msg_if_player( m_info, _( "*Your* batteries are dead." ) );
        return 0;
    } else {
        int moves = to_moves<int>( 20_minutes );
        if( it->ammo_remaining() > 0 ) {
            p->add_msg_if_player(
                _( "You fire up your %s and start getting the tension out." ), it->tname() );
        } else {
            p->add_msg_if_player(
                _( "You whip out your %s and start getting the tension out." ), it->tname() );
        }
        p->assign_activity( std::make_unique<player_activity>(
                                std::make_unique<vibe_activity_actor>( safe_reference<item>( it ) ) ) );
    }
    return it->type->charges_to_use();
}

int iuse::vortex( player* p, item* it, bool, const tripoint_bub_ms & )
{
    std::vector<point_rel_ms> spawn;
    for( int i = -3; i <= 3; i++ ) {
        spawn.emplace_back( -3, i );
        spawn.emplace_back( +3, i );
        spawn.emplace_back( i, -3 );
        spawn.emplace_back( i, +3 );
    }

    while( !spawn.empty() ) {
        monster* const mon =
            g->place_critter_at( mon_vortex, random_entry_removed( spawn ) + p->bub_pos() );
        if( !mon ) { continue; }
        p->add_msg_if_player( m_warning, _( "Air swirls all over…" ) );
        p->moves -= to_moves<int>( 1_seconds );
        it->convert( itype_spiral_stone );
        mon->friendly = -1;
        return it->type->charges_to_use();
    }

    // Only reachable when no monster has been spawned.
    p->add_msg_if_player( m_warning, _( "Air swirls around you for a moment." ) );
    it->convert( itype_spiral_stone );
    return it->type->charges_to_use();
}

int iuse::dog_whistle( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_underwater() ) {
        p->add_msg_if_player( m_info, _( "You can't do that while underwater." ) );
        return 0;
    }
    p->add_msg_if_player( _( "You blow your dog whistle." ) );
    for( monster& critter : g->all_monsters() ) {
        if( critter.friendly != 0 && critter.has_flag( MF_DOGFOOD ) ) {
            bool u_see = g->u.sees( critter );
            if( critter.has_effect( effect_docile ) ) {
                if( u_see ) {
                    p->add_msg_if_player( _( "Your %s looks ready to attack." ), critter.name() );
                }
                critter.remove_effect( effect_docile );
            } else {
                if( u_see ) { p->add_msg_if_player( _( "Your %s goes docile." ), critter.name() ); }
                critter.add_effect( effect_docile, 1_turns );
            }
        }
    }
    return it->type->charges_to_use();
}

int iuse::call_of_tindalos( player* p, item* it, bool, const tripoint_bub_ms & )
{
    for( const tripoint_bub_ms& dest : g->m.points_in_radius( p->bub_pos(), 12 ) ) {
        if( g->m.is_cornerfloor( dest ) ) {
            g->m.add_field( dest, fd_tindalos_rift, 3 );
            add_msg( m_info, _( "You hear a low-pitched echoing howl." ) );
        }
    }
    return it->type->charges_to_use();
}

int iuse::blood_draw( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_npc() ) {
        return 0; // No NPCs for now!
    }
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    if( !it->contents.empty() ) {
        p->add_msg_if_player( m_info, _( "That %s is full!" ), it->tname() );
        return 0;
    }

    const mtype* mt = nullptr;
    bool drew_blood = false;
    bool acid_blood = false;
    for( auto& map_it : g->m.i_at( p->bub_pos().xy() ) ) {
        if( map_it->is_corpse() ) {
            bool has_blood = false;
            mt = map_it->get_mtype();
            if( mt != nullptr ) {
                for( const harvest_entry& entry : mt->harvest.obj() ) {
                    if( entry.type == "blood" ) { has_blood = true; }
                }
            }
            if( !has_blood ) {
                p->add_msg_if_player(
                    m_info, _( "The %s doesn't seem to have any blood to draw." ), map_it->tname() );
                break;
            }
            if( map_it->has_flag( flag_BLED ) ) {
                p->add_msg_if_player(
                    m_info, _( "That %s has already been bled dry." ), map_it->tname() );
                break;
            }
            if( query_yn( _( "Draw blood from %s?" ),
                          colorize( map_it->tname(), map_it->color_in_inventory() ) ) ) {
                // No real way to track and deplete the max potential yield of blood so just
                // randomize
                if( one_in( 10 ) ) {
                    map_it->set_flag( flag_BLED );
                    p->add_msg_if_player(
                        m_info, _( "You drained the last dregs of blood from the %s…" ),
                        map_it->tname() );
                } else {
                    p->add_msg_if_player( m_info, _( "You drew blood from the %s…" ), map_it->tname() );
                }
                drew_blood = true;
                auto bloodtype( map_it->get_mtype()->bloodType() );
                if( bloodtype.obj().has_acid ) {
                    acid_blood = true;
                } else {
                    // Checking again here to actually get the detached_ptr
                    for( const harvest_entry& entry : mt->harvest.obj() ) {
                        if( entry.type == "blood" ) {
                            detached_ptr<item> blood = item::spawn( entry.drop, map_it->birthday() );

                            if( !liquid_handler::handle_liquid( std::move( blood ), 1 ) ) {
                                // NOLINTNEXTLINE(bugprone-use-after-move)
                                it->put_in( std::move( blood ) );
                            }
                            return it->type->charges_to_use();
                        }
                    }
                }
                break;
            }
        }
    }

    if( !drew_blood && query_yn( _( "Draw your own blood?" ) ) ) {
        p->add_msg_if_player( m_info, _( "You drew your own blood…" ) );
        drew_blood = true;
        detached_ptr<item> blood = item::spawn( "blood", calendar::turn );
        if( p->has_trait( trait_ACIDBLOOD ) ) { acid_blood = true; }
        p->mod_stored_nutr( 10 );
        p->mod_thirst( 10 );
        p->mod_pain( 3 );
        if( !liquid_handler::handle_liquid( std::move( blood ), 1 ) ) {
            // NOLINTNEXTLINE(bugprone-use-after-move)
            it->put_in( std::move( blood ) );
        }
        return it->type->charges_to_use();
    }

    if( acid_blood ) {
        detached_ptr<item> acid = item::spawn( "acid", calendar::turn );
        if( one_in( 3 ) ) {
            if( it->inc_damage( DT_ACID ) ) {
                p->add_msg_if_player(
                    m_info, _( "…but acidic blood melts the %s, destroying it!" ), it->tname() );
                it->detach();
                return 0;
            }
            p->add_msg_if_player( m_info, _( "…but acidic blood damages the %s!" ), it->tname() );
        }
        if( !liquid_handler::handle_liquid( std::move( acid ), 1 ) ) {
            // NOLINTNEXTLINE(bugprone-use-after-move)
            it->put_in( std::move( acid ) );
        }
        return it->type->charges_to_use();
    }

    return it->type->charges_to_use();
}

// This is just used for robofac_intercom_mission_2
int iuse::mind_splicer( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    for( auto& map_it : g->m.i_at( p->bub_pos().xy() ) ) {
        if( map_it->typeId() == itype_rmi2_corpse
            && query_yn( _( "Use the mind splicer kit on the %s?" ),
                         colorize( map_it->tname(), map_it->color_in_inventory() ) ) ) {

            auto filter = []( const item & it ) { return it.typeId() == itype_data_card; };
            avatar* you = p->as_avatar();
            item* loc = nullptr;
            if( you != nullptr ) {
                loc = game_menus::inv::titled_filter_menu( filter, *you, _( "Select storage media" ) );
            }
            if( !loc ) {
                add_msg( m_info, _( "Nevermind." ) );
                return 0;
            }
            item& data_card = *loc;
            ///\EFFECT_DEX makes using the mind splicer faster
            ///\EFFECT_FIRSTAID makes using the mind splicer faster
            const time_duration time = std::max(
                                           150_minutes - 20_minutes * ( p->get_skill_level( skill_firstaid ) - 1 )
                                           - 10_minutes * ( p->get_dex() - 8 ),
                                           30_minutes );

            p->assign_activity(
                std::make_unique<player_activity>( std::make_unique<mind_splicer_activity_actor>(
                        safe_reference<item>( &data_card ), to_moves<int>( time ) ) ) );
            return it->type->charges_to_use();
        }
    }
    add_msg( m_info, _( "There's nothing to use the %s on here." ), it->tname() );
    return 0;
}

void iuse::cut_log_into_planks( player& p )
{
    if( p.is_mounted() ) {
        p.add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return;
    }
    const int moves = to_moves<int>( 20_minutes );
    p.add_msg_if_player( _( "You cut the log into planks." ) );

    p.assign_activity( std::make_unique<player_activity>( std::make_unique<wood_chop_activity_actor>(
                           wood_chop_type::PLANKS, g->m.bub_to_abs( p.bub_pos() ), moves ) ) );
}

int iuse::lumber( player* p, item* it, bool t, const tripoint_bub_ms & )
{
    if( t ) { return 0; }
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    // Check if player is standing on any lumber
    item* standing = nullptr;
    for( auto& i : g->m.i_at( p->bub_pos() ) ) {
        if( i->typeId() == itype_log ) {
            standing = &*i;
            break;
        }
    }
    if( standing ) {
        g->m.i_rem( p->bub_pos(), standing );
        cut_log_into_planks( *p );
        return it->type->charges_to_use();
    }

    // If the player is not standing on a log, check inventory
    avatar* you = p->as_avatar();
    item* loc = nullptr;
    auto filter = []( const item & it ) { return it.typeId() == itype_log; };
    if( you != nullptr ) {
        loc = game_menus::inv::titled_filter_menu( filter, *you, _( "Cut up what?" ) );
    }

    if( !loc ) {
        p->add_msg_if_player( m_info, _( "You do not have that item!" ) );
        return 0;
    }
    loc->detach();
    cut_log_into_planks( *p );
    return it->type->charges_to_use();
}

int iuse::chop_moves( Character& ch, item& tool )
{
    // quality of tool
    const int quality = tool.get_quality( qual_AXE );

    // attribute; regular tools - based on STR, powered tools - based on DEX
    const int attr = tool.has_flag( flag_POWERED ) ? ch.dex_cur : ch.str_cur;

    return to_moves<int>( std::max( 10_minutes, time_duration::from_minutes( 60 - attr ) / quality ) );
}

int iuse::chop_tree( player* p, item* it, bool t, const tripoint_bub_ms & )
{
    if( !p || t ) { return 0; }
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    const std::function<bool( const tripoint_bub_ms & )> f = []( const tripoint_bub_ms & pnt ) {
        if( pnt == g->u.bub_pos() ) { return false; }
        return g->m.has_flag( "TREE", pnt );
    };

    const std::optional<tripoint_bub_ms> pnt_ = choose_adjacent_highlight(
            _( "Chop down which tree?" ), _( "There is no tree to chop down nearby." ), f, false );
    if( !pnt_ ) { return 0; }
    const tripoint_bub_ms& pnt = *pnt_;
    if( !f( pnt ) ) {
        if( pnt == p->bub_pos() ) {
            p->add_msg_if_player( m_info, _( "You're not stern enough to shave yourself with THIS." ) );
        } else {
            p->add_msg_if_player( m_info, _( "You can't chop down that." ) );
        }
        return 0;
    }
    int moves = chop_moves( *p, *it );

    const std::vector<npc *> helpers = character_funcs::get_crafting_helpers( *p, 3 );
    for( const npc * np : helpers ) { add_msg( m_info, _( "%s helps with this task…" ), np->name ); }
    moves = moves * ( 10 - helpers.size() ) / 10;

    p->assign_activity( std::make_unique<player_activity>( std::make_unique<wood_chop_activity_actor>(
                            wood_chop_type::TREE, g->m.bub_to_abs( pnt ), moves, it ) ) );

    return it->type->charges_to_use();
}

int iuse::chop_logs( player* p, item* it, bool t, const tripoint_bub_ms & )
{
    if( !p || t ) { return 0; }
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }

    const std::set<ter_id> allowed_ter_id{t_trunk, t_stump};
    const std::function<bool( const tripoint_bub_ms & )> f =
    [&allowed_ter_id]( const tripoint_bub_ms & pnt ) {
        const ter_id type = g->m.ter( pnt );
        const bool is_allowed_terrain = allowed_ter_id.contains( type );
        return is_allowed_terrain;
    };

    const std::optional<tripoint_bub_ms> pnt_ = choose_adjacent_highlight(
            _( "Chop which tree trunk?" ), _( "There is no tree trunk to chop nearby." ), f, false );
    if( !pnt_ ) { return 0; }
    const tripoint_bub_ms& pnt = *pnt_;
    if( !f( pnt ) ) {
        p->add_msg_if_player( m_info, _( "You can't chop that." ) );
        return 0;
    }

    int moves = chop_moves( *p, *it );

    const std::vector<npc *> helpers = character_funcs::get_crafting_helpers( *p, 3 );
    for( const npc * np : helpers ) { add_msg( m_info, _( "%s helps with this task…" ), np->name ); }
    moves = moves * ( 10 - helpers.size() ) / 10;

    p->assign_activity( std::make_unique<player_activity>( std::make_unique<wood_chop_activity_actor>(
                            wood_chop_type::LOGS, g->m.bub_to_abs( pnt ), moves, it ) ) );

    return it->type->charges_to_use();
}

int iuse::oxytorch( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_npc() ) {
        // Long action
        return 0;
    }
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    static const quality_id GLARE( "GLARE" );
    if( !p->has_quality( GLARE, 2 ) ) {
        p->add_msg_if_player( m_info, _( "You need welding goggles to do that." ) );
        return 0;
    }

    map& here = get_map();
    const std::function<bool( const tripoint_bub_ms & )> f = [&here, p]( const tripoint_bub_ms & pnt ) {
        if( pnt == p->bub_pos() ) {
            return false;
        } else if( here.has_furn( pnt ) ) {
            return here.furn( pnt )->oxytorch->valid();
        } else if( !here.ter( pnt )->is_null() ) {
            return here.ter( pnt )->oxytorch->valid();
        }
        return false;
    };

    const std::optional<tripoint_bub_ms> pnt_ = choose_adjacent_highlight(
            _( "Cut up metal where?" ), _( "There is no metal to cut up nearby." ), f, false );
    if( !pnt_ ) { return 0; }
    const tripoint_bub_ms& pnt = *pnt_;
    if( !f( pnt ) ) {
        if( pnt == p->bub_pos() ) {
            p->add_msg_if_player( m_info, _( "Yuck.  Acetylene gas smells weird." ) );
        } else {
            p->add_msg_if_player( m_info, _( "You can't cut that." ) );
        }
        return 0;
    }
    const int fuel_requirement =
        it->ammo_required()
        * ( here.has_furn( pnt )
            ? to_seconds<int>( here.furn( pnt )->oxytorch->duration() )
            : to_seconds<int>( here.ter( pnt )->oxytorch->duration() ) );

    if( fuel_requirement > it->ammo_remaining() ) {
        p->add_msg_if_player(
            m_bad, _( "Your %1$s doesn't have enough charges to cut this, %2$s required." ),
            it->tname(), fuel_requirement );
        return 0;
    }

    p->assign_activity( std::make_unique<player_activity>(
                            std::make_unique<oxytorch_activity_actor>( pnt, safe_reference<item>( *it ) ) ) );

    return 0;
}

int iuse::hacksaw( player* p, item* it, bool t, const tripoint_bub_ms & )
{
    if( !p || t ) { return 0; }
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }

    map& here = get_map();
    const std::function<bool( const tripoint_bub_ms & )> f = [&here, p]( const tripoint_bub_ms & pnt ) {
        if( pnt == p->bub_pos() ) {
            return false;
        } else if( here.has_furn( pnt ) ) {
            return here.furn( pnt )->hacksaw->valid();
        } else if( !here.ter( pnt )->is_null() ) {
            return here.ter( pnt )->hacksaw->valid();
        }
        return false;
    };

    const std::optional<tripoint_bub_ms> pnt_ = choose_adjacent_highlight(
            _( "Cut up metal where?" ), _( "There is no metal to cut up nearby." ), f, false );
    if( !pnt_ ) { return 0; }
    const tripoint_bub_ms& pnt = *pnt_;
    if( !f( pnt ) ) {
        if( pnt == p->bub_pos() ) {
            p->add_msg_if_player( m_info, _( "Why would you do that?" ) );
            p->add_msg_if_player( m_info, _( "You're not even chained to a boiler." ) );
        } else {
            p->add_msg_if_player( m_info, _( "You can't cut that." ) );
        }
        return 0;
    }

    p->assign_activity( std::make_unique<player_activity>(
                            std::make_unique<hacksaw_activity_actor>( pnt, safe_reference<item>( *it ) ) ) );

    return 0;
}

int iuse::boltcutters( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }

    map& here = get_map();
    const std::function<bool( const tripoint_bub_ms & )> f = [&here, p]( const tripoint_bub_ms & pnt ) {
        if( pnt == p->bub_pos() ) {
            return false;
        } else if( here.has_furn( pnt ) ) {
            return here.furn( pnt )->boltcut->valid();
        } else if( !here.ter( pnt )->is_null() ) {
            return here.ter( pnt )->boltcut->valid();
        }
        return false;
    };

    const std::optional<tripoint_bub_ms> pnt_ = choose_adjacent_highlight(
            _( "Cut up metal where?" ), _( "There is no metal to cut up nearby." ), f, false );
    if( !pnt_ ) { return 0; }
    const tripoint_bub_ms& pnt = *pnt_;
    if( !f( pnt ) ) {
        if( pnt == p->bub_pos() ) {
            p->add_msg_if_player( m_info, _( "You neatly sever all of the veins and arteries in your "
                                             "body.  Oh wait, Never mind." ) );
        } else {
            p->add_msg_if_player( m_info, _( "You can't cut that." ) );
        }
        return 0;
    }

    p->assign_activity( std::make_unique<player_activity>(
                            std::make_unique<boltcutting_activity_actor>( pnt, safe_reference<item>( *it ) ) ) );

    return 0;
}

namespace
{

auto mop_normal( const tripoint_bub_ms& pos ) -> bool { return get_map().mop_spills( pos ); }

auto mop_blindly( const tripoint_bub_ms& pos ) -> bool
{
    return one_in( 3 ) && get_map().mop_spills( pos );
}

} // namespace

auto iuse::mop( player* p, item* it, bool, const tripoint_bub_ms & ) -> int
{
    const auto mop = p->is_blind() ? mop_blindly : mop_normal;
    const auto xs = closest_points_first( p->bub_pos(), 1 );

    const int mopped_tiles = std::count_if( xs.begin(), xs.end(), mop );

    if( p->is_blind() ) {
        p->add_msg_if_player( m_info, _( "You move the mop around, unsure whether it's doing any "
                                         "good." ) );
    } else if( mopped_tiles == 0 ) {
        p->add_msg_if_player( m_bad, _( "There's nothing to mop there." ) );
    } else {
        p->add_msg_if_player( m_info, _( "You mop up the spill." ) );
    }

    p->moves -= 150 * mopped_tiles;
    return it->type->charges_to_use();
}

int iuse::artifact( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_npc() ) {
        // TODO: Allow this for trusting NPCs
        return 0;
    }

    if( !it->is_artifact() ) {
        debugmsg( "iuse::artifact called on a non-artifact item!  %s", it->tname() );
        return 0;
    } else if( !it->is_tool() ) {
        debugmsg( "iuse::artifact called on a non-tool artifact!  %s", it->tname() );
        return 0;
    }
    g->events().send<event_type::activates_artifact>( p->getID(), it->tname( 1, false ) );

    const auto& art = it->type->artifact;
    size_t num_used = rng( 1, art->effects_activated.size() );
    if( num_used < art->effects_activated.size() ) {
        num_used += rng( 1, art->effects_activated.size() - num_used );
    }

    std::vector<art_effect_active> effects = art->effects_activated;
    for( size_t i = 0; i < num_used && !effects.empty(); i++ ) {
        const art_effect_active used = random_entry_removed( effects );

        switch( used ) {
            case AEA_STORM: {
                sounds::sound( p->bub_pos(), 10, sounds::sound_t::combat, _( "Ka-BOOM!" ), true,
                               "environment", "thunder_near" );
                int num_bolts = rng( 2, 4 );
                for( int j = 0; j < num_bolts; j++ ) {
                    point dir;
                    while( dir.x == 0 && dir.y == 0 ) {
                        dir.x = rng( -1, 1 );
                        dir.y = rng( -1, 1 );
                    }
                    int dist = rng( 4, 12 );
                    auto bolt = p->bub_pos().xy();
                    for( int n = 0; n < dist; n++ ) {
                        bolt.x() += dir.x;
                        bolt.y() += dir.y;
                        g->m.add_field( {bolt, p->bub_pos().z()}, fd_electricity, rng( 2, 3 ) );
                        if( one_in( 4 ) ) {
                            if( dir.x == 0 ) {
                                dir.x = rng( 0, 1 ) * 2 - 1;
                            } else {
                                dir.x = 0;
                            }
                        }
                        if( one_in( 4 ) ) {
                            if( dir.y == 0 ) {
                                dir.y = rng( 0, 1 ) * 2 - 1;
                            } else {
                                dir.y = 0;
                            }
                        }
                    }
                }
            }
            break;

            case AEA_FIREBALL: {
                if( const std::optional<tripoint_bub_ms> fireball = g->look_around() ) {
                    // only the player can trigger artifact
                    explosion_handler::explosion( *fireball, p, 180, 0.5, true );
                }
            }
            break;

            case AEA_ADRENALINE:
                p->add_msg_if_player( m_good, _( "You're filled with a roaring energy!" ) );
                p->add_effect( effect_adrenaline, rng( 2_minutes, 3_minutes ) );
                break;

            case AEA_MAP: {
                const tripoint_abs_omt center = p->abs_omt_pos();
                const bool new_map =
                    get_overmapbuffer( p->get_dimension() ).reveal( center.xy(), 20, center.z() );
                if( new_map ) {
                    p->add_msg_if_player( m_warning, _( "You have a vision of the surrounding "
                                                        "area…" ) );
                    p->moves -= to_moves<int>( 1_seconds );
                }
            }
            break;

            case AEA_BLOOD: {
                bool blood = false;
                for( const tripoint_bub_ms& tmp : g->m.points_in_radius( p->bub_pos(), 4 ) ) {
                    if( !one_in( 4 ) && g->m.add_field( tmp, fd_blood, 3 )
                        && ( blood || g->u.sees( tmp ) ) ) {
                        blood = true;
                    }
                }
                if( blood ) {
                    p->add_msg_if_player( m_warning, _( "Blood soaks out of the ground and walls." ) );
                }
            }
            break;

            case AEA_FATIGUE: {
                p->add_msg_if_player( m_warning, _( "The fabric of space seems to decay." ) );
                point_bub_ms
                p2{rng( p->bub_pos().x() - 3, p->bub_pos().x() + 3 ),
                   rng( p->bub_pos().y() - 3, p->bub_pos().y() + 3 )};
                g->m.add_field( {p2, p->bub_pos().z()}, fd_fatigue, rng( 1, 2 ) );
            }
            break;

            case AEA_ACIDBALL: {
                if( const std::optional<tripoint_bub_ms> acidball = g->look_around() ) {
                    for( const tripoint_bub_ms& tmp : g->m.points_in_radius( *acidball, 1 ) ) {
                        g->m.add_field( tmp, fd_acid, rng( 2, 3 ) );
                    }
                }
            }
            break;

            case AEA_PULSE:
                sounds::sound( p->bub_pos(), 30, sounds::sound_t::combat, _( "The earth shakes!" ),
                               true, "misc", "earthquake" );
                for( const tripoint_bub_ms& pt : g->m.points_in_radius( p->bub_pos(), 2 ) ) {
                    g->m.bash( pt, 40 );
                    g->m.bash( pt, 40 ); // Multibash effect, so that doors &c will fall
                    g->m.bash( pt, 40 );
                    if( g->m.is_bashable( pt ) && rng( 1, 10 ) >= 3 ) {
                        g->m.bash( pt, 999, false, true );
                    }
                }
                break;

            case AEA_HEAL:
                p->add_msg_if_player( m_good, _( "You feel healed." ) );
                p->healall( 2 );
                break;

            case AEA_CONFUSED:
                for( const tripoint_bub_ms& dest : g->m.points_in_radius( p->bub_pos(), 8 ) ) {
                    if( monster * const mon = g->critter_at<monster>( dest, true ) ) {
                        mon->add_effect( effect_stunned, rng( 5_turns, 15_turns ) );
                    }
                }
                break;

            case AEA_ENTRANCE:
                for( const tripoint_bub_ms& dest : g->m.points_in_radius( p->bub_pos(), 8 ) ) {
                    monster* const mon = g->critter_at<monster>( dest, true );
                    if( mon && mon->friendly == 0 && rng( 0, 600 ) > mon->get_hp() ) {
                        mon->make_friendly();
                    }
                }
                break;

            case AEA_BUGS: {
                int roll = rng( 1, 10 );
                mtype_id bug = mtype_id::NULL_ID();
                int num = 0;
                if( roll <= 4 ) {
                    p->add_msg_if_player( m_warning, _( "Flies buzz around you." ) );
                } else if( roll <= 7 ) {
                    p->add_msg_if_player( m_warning, _( "Giant flies appear!" ) );
                    bug = mon_fly;
                    num = rng( 2, 4 );
                } else if( roll <= 9 ) {
                    p->add_msg_if_player( m_warning, _( "Giant bees appear!" ) );
                    bug = mon_bee;
                    num = rng( 1, 3 );
                } else {
                    p->add_msg_if_player( m_warning, _( "Giant wasps appear!" ) );
                    bug = mon_wasp;
                    num = rng( 1, 2 );
                }
                if( bug ) {
                    for( int j = 0; j < num; j++ ) {
                        if( monster * const b = g->place_critter_around( bug, p->bub_pos(), 1 ) ) {
                            b->friendly = -1;
                            b->add_effect( effect_pet, 1_turns );
                        }
                    }
                }
            }
            break;

            case AEA_TELEPORT:
                teleport::teleport( *p );
                break;

            case AEA_LIGHT:
                p->add_msg_if_player( _( "The %s glows brightly!" ), it->tname() );
                g->timed_events.add( TIMED_EVENT_ARTIFACT_LIGHT, calendar::turn + 3_minutes );
                break;

            case AEA_GROWTH: {
                monster tmptriffid( mtype_id::NULL_ID(), p->bub_pos() );
                mattack::growplants( &tmptriffid );
            }
            break;

            case AEA_HURTALL:
                for( monster& critter : g->all_monsters() ) {
                    critter.apply_damage( nullptr, bodypart_id( "torso" ), rng( 0, 5 ) );
                }
                break;

            case AEA_RADIATION:
                add_msg( m_warning, _( "Horrible gases are emitted!" ) );
                for( const tripoint_bub_ms& dest : g->m.points_in_radius( p->bub_pos(), 1 ) ) {
                    g->m.add_field( dest, fd_nuke_gas, rng( 2, 3 ) );
                }
                break;

            case AEA_PAIN:
                p->add_msg_if_player( m_bad, _( "You're wracked with pain!" ) );
                // OK, the Lovecraftian thingamajig can bring Deadened
                // masochists & Cenobites the stimulation they've been
                // craving ;)
                p->mod_pain_noresist( rng( 5, 15 ) );
                break;

            case AEA_MUTATE:
                if( !one_in( 3 ) ) { p->mutate(); }
                break;

            case AEA_PARALYZE:
                p->add_msg_if_player( m_bad, _( "You're paralyzed!" ) );
                p->moves -= rng( 50, 200 );
                break;

            case AEA_FIRESTORM: {
                p->add_msg_if_player( m_bad, _( "Fire rains down around you!" ) );
                std::vector<tripoint_bub_ms> ps = closest_points_first( p->bub_pos(), 3 );
                for( auto p_it : ps ) {
                    if( !one_in( 3 ) ) {
                        g->m.add_field( p_it, fd_fire, 1 + rng( 0, 1 ) * rng( 0, 1 ), 3_minutes );
                    }
                }
                break;
            }

            case AEA_ATTENTION:
                p->add_msg_if_player( m_warning, _( "You feel like your action has attracted "
                                                    "attention." ) );
                p->add_effect( effect_attention, rng( 1_hours, 3_hours ) );
                break;

            case AEA_TELEGLOW:
                p->add_msg_if_player( m_warning, _( "You feel unhinged." ) );
                p->add_effect( effect_teleglow, rng( 30_minutes, 120_minutes ) );
                break;

            case AEA_NOISE:
                sounds::sound(
                    p->bub_pos(), 100, sounds::sound_t::combat,
                    string_format( _( "a deafening boom from %s %s" ), p->disp_name( true ), it->tname() ),
                    true, "misc", "shockwave" );
                break;

            case AEA_SCREAM:
                sounds::sound(
                    p->bub_pos(), 40, sounds::sound_t::alert,
                    string_format(
                        _( "a disturbing scream from %s %s" ), p->disp_name( true ), it->tname() ),
                    true, "shout", "scream" );
                if( !p->is_deaf() ) { p->add_morale( MORALE_SCREAM, -10, 0, 30_minutes, 1_minutes ); }
                break;

            case AEA_DIM:
                p->add_msg_if_player( _( "The sky starts to dim." ) );
                g->timed_events.add( TIMED_EVENT_DIM, calendar::turn + 5_minutes );
                break;

            case AEA_FLASH:
                p->add_msg_if_player( _( "The %s flashes brightly!" ), it->tname() );
                explosion_handler::flashbang( p->bub_pos(), false, "explosion" );
                break;

            case AEA_VOMIT:
                p->add_msg_if_player( m_bad, _( "A wave of nausea passes through you!" ) );
                p->vomit();
                break;

            case AEA_SHADOWS: {
                int num_shadows = rng( 4, 8 );
                int num_spawned = 0;
                for( int j = 0; j < num_shadows; j++ ) {
                    for( int tries = 0; tries < 10; ++tries ) {
                        auto monp = p->bub_pos();
                        if( one_in( 2 ) ) {
                            monp.x() = rng( p->bub_pos().x() - 5, p->bub_pos().x() + 5 );
                            monp.y() = ( one_in( 2 ) ? p->bub_pos().y() - 5 : p->bub_pos().y() + 5 );
                        } else {
                            monp.x() = ( one_in( 2 ) ? p->bub_pos().x() - 5 : p->bub_pos().x() + 5 );
                            monp.y() = rng( p->bub_pos().y() - 5, p->bub_pos().y() + 5 );
                        }
                        if( !g->m.sees( monp, p->bub_pos(), 10 ) ) { continue; }
                        if( monster * const spawned = g->place_critter_at( mon_shadow, monp ) ) {
                            num_spawned++;
                            spawned->reset_special_rng( "DISAPPEAR" );
                            break;
                        }
                    }
                }
                if( num_spawned > 1 ) {
                    p->add_msg_if_player( m_warning, _( "Shadows form around you." ) );
                } else if( num_spawned == 1 ) {
                    p->add_msg_if_player( m_warning, _( "A shadow forms nearby." ) );
                }
            }
            break;

            case AEA_STAMINA_EMPTY:
                p->add_msg_if_player( m_bad, _( "Your body feels like jelly." ) );
                p->set_stamina( p->get_stamina() * 1 / ( rng( 3, 8 ) ) );
                break;

            case AEA_FUN:
                p->add_msg_if_player( m_good, _( "You're filled with euphoria!" ) );
                p->add_morale( MORALE_FEELING_GOOD, rng( 20, 50 ), 0, 5_minutes, 5_turns, false );
                break;

            case AEA_SPLIT:
                // TODO: Add something
                break;

            case AEA_NULL:
            // BUG
            case NUM_AEAS:
            default:
                debugmsg( "iuse::artifact(): wrong artifact type (%d)", used );
                break;
        }
    }
    return it->type->charges_to_use();
}

int iuse::spray_can( player* p, item* it, bool, const tripoint_bub_ms & )
{
    const std::optional<tripoint_bub_ms> dest_ = choose_adjacent( _( "Spray where?" ) );
    if( !dest_ ) { return 0; }
    return handle_ground_graffiti( *p, it, _( "Spray what?" ), dest_.value() );
}

int iuse::handle_ground_graffiti(
    player& p, item* it, const std::string& prefix, const tripoint_bub_ms& where )
{
    string_input_popup popup;
    std::string message =
        popup.description( prefix + " " + _( "(To delete, clear the text and confirm)" ) )
        .text( g->m.has_graffiti_at( where ) ? g->m.graffiti_at( where ) : std::string() )
        .identifier( "graffiti" )
        .query_string();
    if( popup.canceled() ) { return 0; }

    bool grave = g->m.ter( where ) == t_grave_new;
    int move_cost;
    if( message.empty() ) {
        if( g->m.has_graffiti_at( where ) ) {
            move_cost = 3 * g->m.graffiti_at( where ).length();
            g->m.delete_graffiti( where );
            if( grave ) {
                p.add_msg_if_player( m_info, _( "You blur the inscription on the grave." ) );
            } else {
                p.add_msg_if_player( m_info, _( "You manage to get rid of the message on the "
                                                "surface." ) );
            }
        } else {
            return 0;
        }
    } else {
        g->m.set_graffiti( where, message );
        if( grave ) {
            p.add_msg_if_player( m_info, _( "You carve an inscription on the grave." ) );
        } else {
            p.add_msg_if_player( m_info, _( "You write a message on the surface." ) );
        }
        move_cost = 2 * message.length();
    }
    p.moves -= move_cost;
    if( it != nullptr ) {
        return it->type->charges_to_use();
    } else {
        return 0;
    }
}

int iuse::towel( player* p, item* it, bool t, const tripoint_bub_ms & )
{
    return towel_common( p, it, t );
}

int iuse::towel_common( player* p, item* it, bool t )
{
    if( t ) {
        // Continuous usage, do nothing as not initiated by the player, this is for
        // wet towels only as they are active items.
        return 0;
    }
    bool slime = p->has_effect( effect_slimed );
    bool boom = p->has_effect( effect_boomered );
    bool glow = p->has_effect( effect_glowing );
    int mult = slime + boom + glow; // cleaning off more than one at once makes it take longer
    bool towelUsed = false;
    const std::string name = it ? it->tname() : _( "towel" );

    // can't use an already wet towel!
    if( it && it->has_flag( flag_WET ) ) {
        p->add_msg_if_player(
            m_info, _( "That %s is too wet to soak up any more liquid!" ), it->tname() );
        // clean off the messes first, more important
    } else if( slime || boom || glow ) {
        p->remove_effect( effect_slimed ); // able to clean off all at once
        p->remove_effect( effect_boomered );
        p->remove_effect( effect_glowing );
        p->add_msg_if_player(
            _( "You use the %s to clean yourself off, saturating it with slime!" ), name );

        towelUsed = true;
        if( it && it->typeId() == itype_towel ) { it->convert( itype_towel_soiled ); }

        // dry off from being wet
    } else if( p->has_morale( MORALE_WET ) ) {
        p->rem_morale( MORALE_WET );
        for( auto& pr : p->get_body() ) { pr.second.set_wetness( 0 ); }
        p->add_msg_if_player( _( "You use the %s to dry off, saturating it with water!" ), name );

        towelUsed = true;
        if( it ) { it->set_counter( to_turns<int>( 30_minutes ) ); }

        // default message
    } else {
        p->add_msg_if_player( _( "You are already dry, the %s does nothing." ), name );
    }

    // towel was used
    if( towelUsed ) {
        if( mult == 0 ) { mult = 1; }
        p->moves -= 50 * mult;
        if( it ) {
            // change "towel" to a "towel_wet" (different flavor text/color)
            if( it->typeId() == itype_towel ) { it->convert( itype_towel_wet ); }

            // WET, active items have their timer decremented every turn
            it->set_flag( flag_WET );
            it->activate();
        }
    }
    return it ? it->type->charges_to_use() : 0;
}

int iuse::unfold_generic( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_underwater() ) {
        p->add_msg_if_player( m_info, _( "You can't do that while underwater." ) );
        return 0;
    }
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    map& here = get_map();
    vehicle* veh = here.add_vehicle( vproto_id( "none" ), p->bub_pos(), 0_degrees, 0, 0, false );
    if( veh == nullptr ) {
        p->add_msg_if_player( m_info, _( "There's no room to unfold the %s." ), it->tname() );
        return 0;
    }
    veh->name = it->get_var( "vehicle_name" );
    if( !veh->restore( it->get_var( "folding_bicycle_parts" ) ) ) {
        g->m.destroy_vehicle( veh );
        return 0;
    }
    const bool can_float = veh->can_float();

    const auto invalid_pos = []( const tripoint_bub_ms & pp, bool can_float ) {
        return ( g->m.has_flag_ter( TFLAG_DEEP_WATER, pp ) && !can_float ) || g->m.veh_at( pp )
               || g->m.impassable( pp );
    };
    for( const vpart_reference& vp : veh->get_all_parts() ) {
        if( vp.info().location != "structure" && !vp.info().has_flag( VPFLAG_EXTENDABLE ) ) {
            continue;
        }
        const tripoint_bub_ms pp = vp.pos();
        if( invalid_pos( pp, can_float ) ) {
            p->add_msg_if_player( m_info, _( "There's no room to unfold the %s." ), it->tname() );
            g->m.destroy_vehicle( veh );
            return 0;
        }
    }

    g->m.add_vehicle_to_cache( veh );

    std::string unfold_msg = it->get_var( "unfold_msg" );
    if( unfold_msg.empty() ) {
        unfold_msg = _( "You painstakingly unfold the %s and make it ready to ride." );
    } else {
        unfold_msg = _( unfold_msg );
    }
    veh->set_owner( *p );
    if( g->m.veh_at( p->bub_pos() ).part_with_feature( VPFLAG_BOARDABLE, true ) ) {
        g->m.board_vehicle( p->bub_pos(), p );
    }
    p->add_msg_if_player( m_neutral, unfold_msg, veh->name );

    p->moves -= it->get_var( "moves", to_turns<int>( 5_seconds ) );
    return 1;
}

int iuse::adrenaline_injector( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_npc() && p->get_effect_dur( effect_adrenaline ) >= 3_minutes ) { return 0; }

    p->moves -= to_moves<int>( 1_seconds );
    p->add_msg_player_or_npc( _( "You inject yourself with adrenaline." ), _( "<npcname> injects "
                                 "themselves with "
                                 "adrenaline." ) );

    p->i_add( item::spawn( "syringe", it->birthday() ) );
    if( p->has_effect( effect_adrenaline ) ) {
        p->add_msg_if_player( m_bad, _( "Your heart spasms!" ) );
        // Note: not the mod, the health
        p->mod_healthy( -20 );
    }

    p->add_effect( effect_adrenaline, 2_minutes );

    return it->type->charges_to_use();
}

int iuse::jet_injector( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( !it->ammo_sufficient() ) {
        p->add_msg_if_player( m_info, _( "The jet injector is empty." ) );
        return 0;
    } else {
        p->add_msg_if_player( _( "You inject yourself with the jet injector." ) );
        // Intensity is 2 here because intensity = 1 is the comedown
        p->add_effect( effect_jetinjector, 20_minutes, bodypart_str_id::NULL_ID(), 2 );
        p->mod_painkiller( 20 );
        p->mod_stim( 10 );
        p->healall( 20 );
    }

    if( p->has_effect( effect_jetinjector ) ) {
        if( p->get_effect_dur( effect_jetinjector ) > 20_minutes ) {
            p->add_msg_if_player( m_warning, _( "Your heart is beating alarmingly fast!" ) );
        }
    }
    return it->type->charges_to_use();
}

int iuse::stimpack( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->get_item_position( it ) >= -1 ) {
        p->add_msg_if_player( m_info, _( "You must wear the stimulant delivery system before you can "
                                         "activate it." ) );
        return 0;
    }

    if( !it->ammo_sufficient() ) {
        p->add_msg_if_player( m_info, _( "The stimulant delivery system is empty." ) );
        return 0;
    } else {
        p->add_msg_if_player( _( "You inject yourself with the stimulants." ) );
        // Intensity is 2 here because intensity = 1 is the comedown
        p->add_effect( effect_stimpack, 25_minutes, bodypart_str_id::NULL_ID(), 2 );
        p->mod_painkiller( 2 );
        p->mod_stim( 20 );
        p->mod_fatigue( -100 );
        p->set_stamina( p->get_stamina_max() );
    }
    return it->type->charges_to_use();
}

int iuse::radglove( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->get_item_position( it ) >= -1 ) {
        p->add_msg_if_player( m_info, _( "You must wear the radiation biomonitor before you can "
                                         "activate it." ) );
        return 0;
    } else if( !it->units_sufficient( *p ) ) {
        p->add_msg_if_player( m_info, _( "The radiation biomonitor needs batteries to function." ) );
        return 0;
    } else {
        p->add_msg_if_player( _( "You activate your radiation biomonitor." ) );
        if( p->get_rad() >= 1 ) {
            p->add_msg_if_player( m_warning, _( "You are currently irradiated." ) );
            p->add_msg_player_or_say(
                m_info, _( "Your radiation level: %d mSv." ),
                _( "It says here that my radiation level is %d mSv." ), p->get_rad() );
        } else {
            p->add_msg_player_or_say(
                m_info, _( "You are not currently irradiated." ), _( "It says I'm not irradiated" ) );
        }
        p->add_msg_if_player( _( "Have a nice day!" ) );
    }

    return it->type->charges_to_use();
}

int iuse::contacts( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_underwater() ) {
        p->add_msg_if_player( m_info, _( "You can't do that while underwater." ) );
        return 0;
    }
    const time_duration duration = rng( 6_days, 8_days );
    if( p->has_effect( effect_contacts ) ) {
        if( query_yn( _( "Replace your current lenses?" ) ) ) {
            p->moves -= to_moves<int>( 20_seconds );
            p->add_msg_if_player( _( "You replace your current %s." ), it->tname() );
            p->remove_effect( effect_contacts );
            p->add_effect( effect_contacts, duration );
            return it->type->charges_to_use();
        } else {
            p->add_msg_if_player( _( "You don't do anything with your %s." ), it->tname() );
            return 0;
        }
    } else if( p->has_trait( trait_HYPEROPIC ) || p->has_trait( trait_MYOPIC )
               || p->has_trait( trait_URSINE_EYE ) ) {
        p->moves -= to_moves<int>( 20_seconds );
        p->add_msg_if_player( _( "You put the %s in your eyes." ), it->tname() );
        p->add_effect( effect_contacts, duration );
        return it->type->charges_to_use();
    } else {
        p->add_msg_if_player( m_info, _( "Your vision is fine already." ) );
        return 0;
    }
}

int iuse::talking_doll( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( !it->units_sufficient( *p ) ) {
        p->add_msg_if_player( m_info, _( "The %s's batteries are dead." ), it->tname() );
        return 0;
    }

    const SpeechBubble& speech = get_speech( it->typeId().str() );

    sounds::sound( p->bub_pos(), speech.volume, sounds::sound_t::electronic_speech,
                   speech.text.translated(), true, "speech", it->typeId().str() );

    // Sound code doesn't describe noises at the player position
    if( p->can_hear( p->bub_pos(), speech.volume ) ) {
        p->add_msg_if_player( _( "You hear \"%s\"" ), speech.text );
    }

    return it->type->charges_to_use();
}

int iuse::gun_clean( player* p, item*, bool, const tripoint_bub_ms & )
{
    item* loc = game_menus::inv::titled_menu( g->u, ( "Select the firearm to clean or mend" ) );
    if( !loc ) {
        p->add_msg_if_player( m_info, _( "You do not have that item!" ) );
        return 0;
    }
    item& fix = *loc;
    if( !fix.is_firearm() ) {
        p->add_msg_if_player( m_info, _( "That isn't a firearm!" ) );
        return 0;
    }

    const auto is_gunmods_not_faulty = []( const auto & xs ) -> bool {
        return std::all_of( xs.begin(), xs.end(), []( const item * mod ) -> bool {
            return mod->faults.empty();
        } );
    };

    if( fix.faults.empty() && is_gunmods_not_faulty( fix.gunmods() ) ) {
        p->add_msg_if_player( m_info, _( "There's nothing you can clean or mend with this." ) );
        return 0;
    }
    avatar_funcs::mend_item( *p->as_avatar(), *loc );
    return 0;
}

int iuse::gun_repair( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( !it->units_sufficient( *p ) ) { return 0; }
    if( p->is_underwater() ) {
        p->add_msg_if_player( m_info, _( "You can't do that while underwater." ) );
        return 0;
    }
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    /** @EFFECT_MECHANICS >1 allows gun repair */
    if( p->get_skill_level( skill_mechanics ) < 2 ) {
        p->add_msg_if_player( m_info, _( "You need a mechanics skill of 2 to use this repair kit." ) );
        return 0;
    }
    item* loc = game_menus::inv::titled_menu( g->u, ( "Select the firearm to repair" ) );
    if( !loc ) {
        p->add_msg_if_player( m_info, _( "You do not have that item!" ) );
        return 0;
    }
    item& fix = *loc;
    if( !fix.is_firearm() ) {
        p->add_msg_if_player( m_info, _( "That isn't a firearm!" ) );
        return 0;
    }
    if( fix.has_flag( flag_NO_REPAIR ) ) {
        p->add_msg_if_player( m_info, _( "You cannot repair your %s." ), fix.tname() );
        return 0;
    }
    if( fix.damage() <= fix.min_damage() ) {
        p->add_msg_if_player(
            m_info, _( "You cannot improve your %s any more this way." ), fix.tname() );
        return 0;
    }
    if( fix.damage() <= 0 && p->get_skill_level( skill_mechanics ) < 8 ) {
        p->add_msg_if_player( m_info, _( "Your %s is already in peak condition." ), fix.tname() );
        p->add_msg_if_player( m_info, _( "With a higher mechanics skill, you might be able to "
                                         "improve it." ) );
        return 0;
    }
    /** @EFFECT_MECHANICS >=8 allows accurizing ranged weapons */
    const std::string startdurability = fix.durability_indicator( true );
    std::string resultdurability;
    const float vision_mod = character_funcs::fine_detail_vision_mod( *p );
    // TODO: this may render player unable to move for minutes, and so should start an activity
    // instead
    if( fix.damage() <= 0 ) {
        sounds::
        sound( p->bub_pos(), 6, sounds::sound_t::activity, "crunch", true, "tool", "repair_kit" );
        p->moves -= to_moves<int>( 20_seconds * vision_mod );
        p->practice( skill_mechanics, 10 );
        fix.mod_damage( -itype::damage_scale );
        p->add_msg_if_player( m_good, _( "You accurize your %s." ), fix.tname( 1, false ) );

    } else if( fix.damage() > itype::damage_scale ) {
        sounds::
        sound( p->bub_pos(), 8, sounds::sound_t::activity, "crunch", true, "tool", "repair_kit" );
        p->moves -= to_moves<int>( 10_seconds * vision_mod );
        p->practice( skill_mechanics, 10 );
        fix.mod_damage( -itype::damage_scale );
        resultdurability = fix.durability_indicator( true );
        p->add_msg_if_player(
            m_good, _( "You repair your %s!  ( %s-> %s)" ), fix.tname( 1, false ), startdurability,
            resultdurability );

    } else {
        sounds::
        sound( p->bub_pos(), 8, sounds::sound_t::activity, "crunch", true, "tool", "repair_kit" );
        p->moves -= to_moves<int>( 5_seconds * vision_mod );
        p->practice( skill_mechanics, 10 );
        fix.set_damage( 0 );
        resultdurability = fix.durability_indicator( true );
        p->add_msg_if_player(
            m_good, _( "You repair your %s completely!  ( %s-> %s)" ), fix.tname( 1, false ),
            startdurability, resultdurability );
    }
    return it->type->charges_to_use();
}

int iuse::gunmod_attach( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( !it || !it->is_gunmod() ) {
        debugmsg( "tried to attach non-gunmod" );
        return 0;
    }

    if( !p ) { return 0; }

    auto loc = game_menus::inv::gun_to_modify( *p, *it );

    if( !loc ) {
        add_msg( m_info, _( "Never mind." ) );
        return 0;
    }

    avatar_funcs::gunmod_add( *p->as_avatar(), *loc, *it );

    return 0;
}

int iuse::toolmod_attach( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( !it || !it->is_toolmod() ) {
        debugmsg( "tried to attach non-toolmod" );
        return 0;
    }

    if( !p ) { return 0; }

    auto filter = [&it]( const item & e ) {
        // don't allow ups battery mods on a UPS or UPS-powered tools
        if( it->has_flag( flag_USE_UPS ) && ( e.has_flag( flag_IS_UPS ) || e.has_flag( flag_USE_UPS ) ) ) {
            return false;
        }

        // can only attach to unmodified tools that use compatible ammo
        return e.is_tool() && e.toolmods().empty() && !e.magazine_current()
               && std::any_of(
                   it->type->mod->acceptable_ammo.begin(), it->type->mod->acceptable_ammo.end(),
        [&]( const ammotype & at ) { return e.ammo_types( false ).count( at ); } );
    };

    auto loc = g->inv_map_splice(
                   filter, _( "Select tool to modify" ), 1, _( "You don't have compatible tools." ) );

    if( !loc ) {
        add_msg( m_info, _( "Never mind." ) );
        return 0;
    }

    if( loc->ammo_remaining() ) {
        if( !avatar_funcs::unload_item( *p->as_avatar(), *loc ) ) {
            p->add_msg_if_player( m_info, _( "You cancel unloading the tool." ) );
            return 0;
        }
    }

    avatar_funcs::toolmod_add( *p->as_avatar(), *loc, *it );
    return 0;
}

int iuse::bell( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( it->typeId() == itype_cow_bell ) {
        sounds::sound( p->bub_pos(), 12, sounds::sound_t::music, _( "Clank!  Clank!" ), true, "misc",
                       "cow_bell" );
        if( !p->is_deaf() ) {
            auto cattle_level = p->mutation_category_level.find( mutation_category_id( "CATTLE" ) );
            const int cow_factor =
                1
                + ( cattle_level == p->mutation_category_level.end()
                    ? 0
                    : ( cattle_level->second ) / 8 );
            if( x_in_y( cow_factor, 1 + cow_factor ) ) {
                p->add_morale( MORALE_MUSIC, 1, 15 * ( cow_factor > 10 ? 10 : cow_factor ) );
            }
        }
    } else {
        sounds::
        sound( p->bub_pos(), 4, sounds::sound_t::music, _( "Ring!  Ring!" ), true, "misc", "bell" );
    }
    return it->type->charges_to_use();
}

int iuse::seed( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_npc()
        || query_yn( _( "Sure you want to eat the %s?  You could plant it in a mound of dirt." ),
                     colorize( it->tname(), it->color_in_inventory() ) ) ) {
        return it->type->charges_to_use(); // This eats the seed object.
    }
    return 0;
}

namespace
{
auto is_hackable_robot( const monster& mon ) -> bool
{
    // HackPRO targets electronic robots regardless of species naming.
    return mon.has_flag( MF_ELECTRONIC );
}

auto get_hackable_friendly_monsters( game& game_ref ) -> std::vector<shared_ptr_fast<monster>>
{
    auto monsters = game_ref.all_monsters();
    auto& items = monsters.items;
    auto results = std::vector<shared_ptr_fast<monster>> {};
    if( !items ) { return results; }
    std::ranges::for_each( *items, [&]( const auto & weak_monster ) {
        auto current = weak_monster.lock();
        if( !current || current->is_dead() ) { return; }
        if( current->friendly == 0 || !is_hackable_robot( *current ) ) { return; }
        results.push_back( std::move( current ) );
    } );
    return results;
}
} // namespace


static auto confirm_source_vehicle( const tripoint_abs_ms& global )
{
    optional_vpart_position source_vp = g->m.veh_at( global );
    vehicle* const source_veh = veh_pointer_or_null( source_vp );
    return std::make_tuple( source_vp, source_veh );
};

static tripoint_abs_ms process_map_connection(
    const Character* who, cable_state state, bool tow = false )
{
    const std::optional<tripoint_bub_ms> posp_ = choose_adjacent( _( "Attach cable where?" ) );
    if( !posp_ ) { return tripoint_abs_ms_min; }
    map& here = get_map();
    const auto posp = here.bub_to_abs( *posp_ );

    switch( state ) {
        case state_vehicle: {
            const optional_vpart_position vp = here.veh_at( posp );
            if( !vp ) {
                who->add_msg_if_player( _( "There's no vehicle there." ) );
                if( !tow ) {
                    return tripoint_abs_ms_min;
                } else {
                    vehicle* const source_veh = veh_pointer_or_null( vp );
                    if( source_veh ) {
                        if( source_veh->has_tow_attached() || source_veh->is_towed()
                            || source_veh->is_towing() ) {
                            who->add_msg_if_player( _( "That vehicle already has a tow-line "
                                                       "attached." ) );
                            return tripoint_abs_ms_min;
                        }
                        if( !source_veh->is_external_part( *posp_ ) ) {
                            who->add_msg_if_player( _( "You can't attach the tow-line to an internal "
                                                       "part." ) );
                            return tripoint_abs_ms_min;
                        }
                    }
                }
            }
            break;
        }
        case state_grid: {
            auto* grid_connector = active_tiles::furn_at<vehicle_connector_tile>( posp );
            if( !grid_connector ) {
                who->add_msg_if_player( _( "There's no grid connector there." ) );
                return tripoint_abs_ms_min;
            }
            break;
        }
        default:
            return tripoint_abs_ms_min;
    }
    return posp;
}

static cable_state cable_menu( Character* who, cable_state& state, cable_state& state_other )
{
    const bool has_bio_cable = !who->get_remote_fueled_bionic().is_empty();
    // const bool has_solar_pack = who->worn_with_flag( flag_SOLARPACK );
    const bool has_solar_pack_on = who->worn_with_flag( flag_SOLARPACK_ON );
    // const bool wearing_solar_pack = has_solar_pack || has_solar_pack_on;
    const bool has_ups = who->has_charges( itype_UPS, 1 );

    const bool allow_self = state != state_self && state_other != state_self;
    const bool allow_ups =
        state_other == state_self || ( state == state_none && state_other == state_none );
    const bool allow_grid = state_other != state_UPS && state_other != state_solar_pack;

    // Currently those bools equal provided counterparts, feel free to change those if it's needed
    // in future
    const bool allow_solar = allow_ups;
    const bool allow_vehicle = allow_grid;

    uilist kmenu;
    kmenu.text = _( "Using cable:" );
    if( state != state_none || state_other != state_none ) {
        kmenu.addentry( state_none, true, -1, _( "Detach and re-spool the cable" ) );
    }
    kmenu.addentry( state_self, has_bio_cable && allow_self, -1, _( "Attach cable to self" ) );
    kmenu.addentry( state_vehicle, allow_vehicle, -1, _( "Attach cable to vehicle" ) );
    kmenu.addentry( state_grid, allow_grid, -1, _( "Attach cable to grid connector" ) );
    kmenu.addentry( state_UPS, has_ups && allow_ups, -1, _( "Attach cable to ups" ) );
    kmenu.addentry(
        state_solar_pack, has_solar_pack_on && allow_solar, -1, _( "Attach cable to solar pack" ) );

    kmenu.query();
    return cable_state( kmenu.ret );
}


static cable_state tow_cable_menu( cable_state& state, cable_state& state_other, bool towed )
{
    uilist kmenu;
    kmenu.text =
        towed ? _( "Using cable tow cable. Attached vehicle will be towed:" )
        : _( "Using cable tow cable. Attached vehicle will tow the other:" );
    if( state != state_none || state_other != state_none ) {
        kmenu.addentry( state_none, true, -1, _( "Detach and re-spool the cable" ) );
    }
    kmenu.addentry( state_vehicle, true, -1, _( "Attach loose end to vehicle" ) );

    kmenu.query();
    return cable_state( kmenu.ret );
}

static void set_cable_active( player* const who, item* const it,
                              const cable_connection_data& data )
{
    data.set_vars( it );
    it->activate();
    it->attempt_detach( [&who]( detached_ptr<item>&& e ) {
        return item::process( std::move( e ), who, who->bub_pos(), false );
    } );
    who->mod_moves( -15 );
};

int iuse::tow_attach( player* who, item* cable, bool, const tripoint_bub_ms & )
{
    if( !who ) { return 0; }
    auto data = cable_connection_data::make_data( cable );
    if( !data ) { return 0; }
    cable_connection_data::connection* last = nullptr;

    if( data->con1.empty() ) {
        last = &data->con1;
        switch( data->con1.state = tow_cable_menu( data->con1.state, data->con2.state, false ) ) {
            case state_none:
                cable->reset_cable( who );
                return 0;
            case state_vehicle:
                data->con1.point = process_map_connection( who, state_vehicle, true );
                if( data->con1.point_valid() ) { set_cable_active( who, cable, data.value() ); }
                break;
            default:
                add_msg( _( "Never mind" ) );
                return 0;
        }
    } else if( data->con2.empty() ) {
        last = &data->con2;
        switch( data->con2.state = tow_cable_menu( data->con2.state, data->con1.state, true ) ) {
            case state_none:
                cable->reset_cable( who );
                return 0;
            case state_vehicle:
                data->con2.point = process_map_connection( who, state_vehicle, true );
                if( data->con2.point_valid() ) { set_cable_active( who, cable, data.value() ); }
                break;
            default:
                add_msg( _( "Never mind" ) );
                return 0;
        }
    }
    if( data->intermap_connection() ) {
        const auto [vp1, v1] = confirm_source_vehicle( data->con1.point );
        const auto [vp2, v2] = confirm_source_vehicle( data->con2.point );

        if( !vp1 || !vp2 ) {
            debugmsg( "Something went wrong with cable connection" );
            who->add_msg_if_player( m_bad, _( "You notice the cable has come loose!" ) );
            cable->reset_cable( who );
            return 0;
        }

        if( v1 == v2 ) {
            who->add_msg_if_player( m_warning, _( "You cannot set a vehicle to tow itself!" ) );
            if( last ) {
                data->unset_con( cable, *last );
            } else {
                cable->reset_cable( who );
            }
            return 0;
        }
        const vpart_id vpid( cable->typeId().str() );

        tripoint_mnt_veh vcoords = vp1->mount();
        vehicle_part v1_part( vpid, vcoords, item::spawn( *cable ), v1 );
        v1->install_part( vcoords, std::move( v1_part ) );
        vcoords = vp2->mount();
        vehicle_part v2_part( vpid, vcoords, item::spawn( *cable ), v2 );
        v2->install_part( vcoords, std::move( v2_part ) );

        if( who->has_item( *cable ) ) {
            who->add_msg_if_player(
                m_good, _( "You link up the %1$s and the %2$s." ), v1->name, v2->name );
        }
        v1->tow_data.set_towing( v1, v2 );
        return 1; // Let the cable be destroyed.
    }
    return 0;
}

int iuse::cable_attach( player* who, item* cable, bool, const tripoint_bub_ms & )
{
    item* ups_loc = nullptr;
    avatar* you = who->as_avatar();
    const std::string choose_ups = _( "Choose UPS:" );
    const std::string dont_have_ups = _( "You don't have any UPS." );
    auto filter = [&]( const item & itm ) { return itm.has_flag( flag_IS_UPS ); };

    auto data = cable_connection_data::make_data( cable );
    if( !data ) { return 0; }

    cable_connection_data::connection* con = nullptr;
    cable_connection_data::connection* con_other = nullptr;

    if( data->con1.empty() ) {
        con = &data->con1;
        con_other = &data->con2;
    } else if( data->con2.empty() ) {
        con = &data->con2;
        con_other = &data->con1;
    }

    if( con && con->empty() ) {
        con->state = cable_menu( who, con->state, con_other->state );
        switch( con->state ) {
            case state_self:
                who->add_msg_if_player( m_info, _( "You attach the cable to the Cable Charger "
                                                   "System." ) );
                break;
            case state_none:
                cable->reset_cable( who );
                return 0;
            case state_solar_pack:
                who->add_msg_if_player( m_info, _( "You attach the cable to the solar pack." ) );
                break;
            case state_grid: {
                con->point = process_map_connection( who, con->state );
                if( !con->point_valid() ) { return 0; }
                // Basically we allow player to try and use grid to grid connection, only TO give
                // them some insight
                if( con_other->state == state_grid ) {
                    if( con->point == con_other->point ) {
                        who->add_msg_if_player( m_info, _( "That would be unwise to short-circuit "
                                                           "this grid connector." ) );
                    } else {
                        who->add_msg_if_player( m_info, _( "To directly connect two networks "
                                                           "together, use a voltmeter instead." ) );
                    }
                    return 0;
                }
                break;
            }
            case state_UPS:
                if( you ) {
                    ups_loc = game_menus::inv::
                              titled_filter_menu( filter, *you, choose_ups, dont_have_ups );
                }
                if( !ups_loc ) {
                    add_msg( _( "Never mind" ) );
                    return 0;
                }
                ups_loc->set_var( "cable", "plugged_in" );
                ups_loc->activate();
                who->add_msg_if_player( m_info, _( "You attach the cable to the UPS." ) );
                break;
            case state_vehicle:
                con->point = process_map_connection( who, con->state );
                if( !con->point_valid() ) { return 0; }
                break;
            default:
                return 0;
        }
        set_cable_active( who, cable, data.value() );
    }
    // Both ends are currently connected, respool or do nothing
    else {
        uilist kmenu;
        kmenu.text = _( "Using cable:" );
        kmenu.addentry( state_none, true, -1, _( "Detach and re-spool the cable" ) );
        kmenu.query();

        if( kmenu.ret == state_none ) {
            cable->reset_cable( who );
        } else {
            you->add_msg_if_player( m_info, _( "Never mind." ) );
        }
        return 0;
    }

    // Two connections are made, let's process result
    if( data->complete() ) {
        // We've connected something to Character
        if( data->character_connected() ) {
            auto* const nonchar = data->get_nonchar_connection();
            switch( nonchar->state ) {
                case state_grid:
                    who->add_msg_if_player( m_good, _( "You are now plugged to the grid." ) );
                    break;
                case state_solar_pack:
                    who->add_msg_if_player( m_good, _( "You are now plugged to the solar backpack." ) );
                    break;
                case state_UPS:
                    who->add_msg_if_player( m_good, _( "You are now plugged to the UPS." ) );
                    break;
                case state_vehicle: {
                    const auto [_, veh] = confirm_source_vehicle( nonchar->point );
                    if( veh ) {
                        who->add_msg_if_player( m_good, _( "You are now plugged to the vehicle." ) );
                    } else {
                        who->add_msg_if_player( m_bad, _( "You notice the cable has come loose!" ) );
                        cable->reset_cable( who );
                        return 0;
                    }
                    break;
                }
                case state_none:
                    // How tf?
                    cable->reset_cable( who );
                    debugmsg( "Unexpected cable state %s", nonchar->state );
                    break;
                default:
                    add_msg( _( "Never mind" ) );
                    return 0;
            };
            who->find_remote_fuel();
            return 0;
        }
        // We've connected two vehicles
        if( data->con1.state == state_vehicle && data->con2.state == state_vehicle ) {
            const auto [vp1, v1] = confirm_source_vehicle( data->con1.point );
            const auto [vp2, v2] = confirm_source_vehicle( data->con2.point );

            if( !vp1 || !vp2 ) {
                debugmsg( "Something went wrong with cable connection" );
                who->add_msg_if_player( m_bad, _( "You notice the cable has come loose!" ) );
                cable->reset_cable( who );
                return 0;
            }

            if( v1 == v2 ) {
                who->add_msg_if_player(
                    m_warning, _( "The %s already has access to its own electric system!" ),
                    v1->name );
                cable_connection_data::unset_con2( cable );
                return 0;
            }
            const vpart_id vpid( cable->typeId().str() );

            // Vehicle part1
            tripoint_mnt_veh vcoords = vp1->mount();
            vehicle_part p1( vpid, vcoords, item::spawn( *cable ), v1 );
            p1.target.first = data->con2.point;
            p1.target.second = v2->abs_ms_location();
            v1->install_part( vcoords, std::move( p1 ) );

            // Vehicle part2
            vcoords = vp2->mount();
            vehicle_part p2( vpid, vcoords, item::spawn( *cable ), v2 );
            p2.target.first = data->con1.point;
            p2.target.second = v1->abs_ms_location();
            v2->install_part( vcoords, std::move( p2 ) );

            if( who != nullptr && who->has_item( *cable ) ) {
                who->add_msg_if_player(
                    m_good, _( "You link up the electric systems of the %1$s and the %2$s." ),
                    v1->name, v2->name );
            }
            return 1; // Let the cable be destroyed.
        }
        // We've connected vehicle to grid
        else if( data->con1.state == state_vehicle || data->con2.state == state_vehicle ) {
            auto [vp1, v1] = confirm_source_vehicle( data->con1.point );
            auto [vp2, v2] = confirm_source_vehicle( data->con2.point );

            vehicle* v = nullptr;
            optional_vpart_position vp( std::nullopt );
            tripoint_abs_ms connector;

            if( v1 ) {
                v = v1;
                vp = vp1;
                connector = data->con2.point;
            } else if( v2 ) {
                v = v2;
                vp = vp2;
                connector = data->con1.point;
            } else {
                debugmsg( "Something went wrong with cable connection" );
                who->add_msg_if_player( m_bad, _( "You notice the cable has come loose!" ) );
                cable->reset_cable( who );
                return 0;
            }

            auto* grid_connector = active_tiles::furn_at<vehicle_connector_tile>( connector );
            if( !grid_connector ) {
                who->add_msg_if_player( _( "There's no grid connection there." ) );
                cable->reset_cable( who );
                return 0;
            }

            const vpart_id vpid( cable->typeId().str() );
            tripoint_mnt_veh vcoords = vp->mount();
            vehicle_part v_part( vpid, vcoords, item::spawn( *cable ), v );
            v_part.target.first = connector;
            v_part.target.second = connector;
            v_part.set_flag( vehicle_part::targets_grid );
            if( who && who->has_item( *cable ) ) {
                who->add_msg_if_player(
                    m_good, _( "You connect the %s to the electric grid." ), v->name );
                grid_connector->connected_vehicles.emplace_back( v->abs_ms_location() );
                v->install_part( vcoords, std::move( v_part ) );
            }
            return 1; // Let the cable be destroyed.
        }
    }
    return 0;
}

int iuse::shavekit( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    if( !it->ammo_sufficient() ) {
        p->add_msg_if_player( _( "You need soap to use this." ) );
    } else {
        const int moves = to_moves<int>( 5_minutes );
        p->assign_activity( std::make_unique<player_activity>(
                                std::make_unique<morale_activity_actor>( morale_act_type::SHAVE ) ) );
    }
    return it->type->charges_to_use();
}

int iuse::hairkit( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    const int moves = to_moves<int>( 30_minutes );
    p->assign_activity( std::make_unique<player_activity>(
                            std::make_unique<morale_activity_actor>( morale_act_type::HAIRCUT ) ) );
    return it->type->charges_to_use();
}

int iuse::weather_tool( player* p, item* it, bool, const tripoint_bub_ms & )
{
    const weather_manager& weather = get_weather();
    const w_point& weatherPoint = get_weather().get_precise();

    /* Possibly used twice. Worth spending the time to precalculate. */
    const auto player_local_temp = weather.get_temperature( p->abs_pos() );

    map& here = get_map();
    if( it->typeId() == itype_weather_reader ) {
        p->add_msg_if_player( m_neutral, _( "The %s's monitor slowly outputs the data…" ), it->tname() );
    }
    if( it->has_flag( flag_THERMOMETER ) ) {
        if( it->typeId() == itype_thermometer ) {
            p->add_msg_if_player(
                m_neutral, _( "The %1$s reads %2$s." ), it->tname(),
                print_temperature( player_local_temp ) );
        } else {
            p->add_msg_if_player(
                m_neutral, _( "Temperature: %s." ), print_temperature( player_local_temp ) );
        }
        // TODO: Don't output air temp if we aren't near air
        if( g->m.has_flag( TFLAG_SWIMMABLE, p->bub_pos() ) ) {
            const units::temperature water_temp =
                weather.get_cur_weather_gen().get_water_temperature(
                    tripoint_abs_ms( here.bub_to_abs( p->bub_pos() ) ), calendar::turn,
                    calendar::config, g->get_seed() );
            p->add_msg_if_player(
                m_neutral, _( "Water temperature: %s." ), print_temperature( water_temp ) );
        }
    }
    if( it->has_flag( flag_HYGROMETER ) ) {
        if( it->typeId() == itype_hygrometer ) {
            p->add_msg_if_player(
                m_neutral, _( "The %1$s reads %2$s." ), it->tname(),
                print_humidity( get_local_humidity(
                                    weatherPoint.humidity, get_weather().weather_id,
                                    g->is_sheltered( p->bub_pos() ) ) ) );
        } else {
            p->add_msg_if_player(
                m_neutral, _( "Relative Humidity: %s." ),
                print_humidity( get_local_humidity(
                                    weatherPoint.humidity, get_weather().weather_id,
                                    g->is_sheltered( p->bub_pos() ) ) ) );
        }
    }
    if( it->has_flag( flag_BAROMETER ) ) {
        if( it->typeId() == itype_barometer ) {
            p->add_msg_if_player(
                m_neutral, _( "The %1$s reads %2$s." ), it->tname(),
                print_pressure( static_cast<int>( weatherPoint.pressure ) ) );
        } else {
            p->add_msg_if_player(
                m_neutral, _( "Pressure: %s." ),
                print_pressure( static_cast<int>( weatherPoint.pressure ) ) );
        }
    }
    if( it->has_flag( flag_WEATHER_FORECAST ) ) {
        std::string message = string_format( "", message );
        const auto tref = get_overmapbuffer( p->get_dimension() ).find_radio_station( it->frequency );
        if( tref ) {
            { message = weather_forecast( tref.abs_sm_pos ); }
            p->add_msg_if_player( m_neutral, _( "Automatic weather report %s" ), message );
        }
    }
    if( it->has_flag( flag_WINDMETER ) ) {
        int vehwindspeed = 0;
        if( optional_vpart_position vp = g->m.veh_at( p->bub_pos() ) ) {
            vehwindspeed = std::lround( cmps_to_mps( std::abs( vp->vehicle().velocity ) ) * 2.23694 );
        }
        const oter_id& cur_om_ter = get_overmapbuffer( p->get_dimension() ).ter( p->abs_omt_pos() );
        /* windpower defined in internal velocity units (=.01 mph) */
        const double windpower =
            100
            * get_local_windpower( weather.windspeed + vehwindspeed, cur_om_ter, p->abs_pos(),
                                   weather.winddirection, g->is_sheltered( p->bub_pos() ) );
        const int windpower_vehicle_units = std::lround( windpower * 0.44704 );
        std::string dirstring = get_dirstring( weather.winddirection );
        p->add_msg_if_player(
            m_neutral, _( "Wind: %.1f %2$s from the %3$s.\nFeels like: %4$s." ),
            convert_velocity( windpower_vehicle_units, VU_VEHICLE ), velocity_units( VU_VEHICLE ),
            dirstring,
            print_temperature(
                get_local_windchill( units::to_fahrenheit( weatherPoint.temperature ),
                                     weatherPoint.humidity, windpower / 100 )
                + units::to_fahrenheit( player_local_temp ) ) );
    }

    return 0;
}

int iuse::directional_hologram( player* p, item* it, bool, const tripoint_bub_ms& pos )
{
    if( it->is_armor() && !( p->is_worn( *it ) ) ) {
        p->add_msg_if_player(
            m_neutral, _( "You need to wear the %1$s before activating it." ), it->tname() );
        return 0;
    }
    const std::optional<tripoint_bub_ms> posp_ = choose_adjacent( _( "Choose hologram direction." ) );
    if( !posp_ ) { return 0; }
    const auto posp = *posp_;

    monster* const hologram = g->place_critter_at( mon_hologram, posp );
    if( !hologram ) {
        p->add_msg_if_player( m_info, _( "Can't create a hologram there." ) );
        return 0;
    }
    tripoint_bub_ms target = pos;
    target.x() = p->bub_pos().x() + 4 * SEEX * ( posp.x() - p->bub_pos().x() );
    target.y() = p->bub_pos().y() + 4 * SEEY * ( posp.y() - p->bub_pos().y() );
    hologram->friendly = -1;
    hologram->add_effect( effect_docile, 1_hours );
    hologram->wandf = -30;
    hologram->set_summon_time( 60_seconds );
    hologram->set_dest( target );
    p->mod_moves( -to_turns<int>( 1_seconds ) );
    return it->type->charges_to_use();
}

int iuse::capture_monster_veh( player* p, item* it, bool, const tripoint_bub_ms& pos )
{
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    if( !it->has_flag( flag_VEHICLE ) ) {
        p->add_msg_if_player(
            m_info, _( "The %s must be installed in a vehicle before being loaded." ), it->tname() );
        return 0;
    }
    capture_monster_act( p, it, false, pos );
    return 0;
}

bool item::release_monster( const tripoint_bub_ms& target, const int radius )
{
    shared_ptr_fast<monster> new_monster = make_shared_fast<monster>();
    try {
        ::deserialize( *new_monster, get_var( "contained_json", "" ) );
    } catch( const std::exception& e ) {
        debugmsg( _( "Error restoring monster: %s" ), e.what() );
        return false;
    }
    if( !g->place_critter_around( new_monster, target, radius ) ) { return false; }
    erase_var( "contained_name" );
    erase_var( "contained_json" );
    erase_var( "name" );
    erase_var( "weight" );
    return true;
}

// didn't want to drag the monster:: definition into item.h, so just reacquire the monster
// at target
int item::contain_monster( const tripoint_bub_ms& target )
{
    const monster* const mon_ptr = g->critter_at<monster>( target );
    if( !mon_ptr ) { return 0; }
    const monster& f = *mon_ptr;

    set_var( "contained_json", ::serialize( f ) );
    set_var( "contained_name", f.type->nname() );
    set_var( "name", string_format( _( "%s holding %s" ), type->nname( 1 ), f.type->nname() ) );
    // Need to add the weight of the empty container because item::weight uses the "weight" variable
    // directly.
    set_var( "weight", to_milligram( type->weight + f.get_weight() ) );
    g->remove_zombie( f );
    return 0;
}

int iuse::capture_monster_act( player* p, item* it, bool, const tripoint_bub_ms& pos )
{
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot capture a creature mounted." ) );
        return 0;
    }
    if( it->has_var( "contained_name" ) ) {
        // Remember contained_name for messages after release_monster erases it
        const std::string contained_name = it->get_var( "contained_name", "" );

        if( it->release_monster( pos ) ) {
            // It's been activated somewhere where there isn't a player or monster, good.
            return 0;
        }
        if( it->has_flag( flag_PLACE_RANDOMLY ) ) {
            if( it->release_monster( p->bub_pos(), 1 ) ) { return 0; }
            p->add_msg_if_player( _( "There is no place to put the %s." ), contained_name );
            return 0;
        } else {
            const std::string query = string_format( _( "Place the %s where?" ), contained_name );
            const std::optional<tripoint_bub_ms> pos_ = choose_adjacent( query );
            if( !pos_ ) { return 0; }
            if( it->release_monster( *pos_ ) ) {
                p->add_msg_if_player( _( "You release the %s." ), contained_name );
                return 0;
            }
            p->add_msg_if_player( m_info, _( "You cannot place the %s there!" ), contained_name );
            return 0;
        }
    } else {
        if( !it->has_property( "creature_size_capacity" ) ) {
            debugmsg( "%s has no creature_size_capacity.", it->tname() );
            return 0;
        }
        const std::string capacity = it->get_property_string( "creature_size_capacity" );
        if( !Creature::size_map.contains( capacity ) ) {
            debugmsg( "%s has invalid creature_size_capacity %s.", it->tname(), capacity.c_str() );
            return 0;
        }
        const std::function<bool( const tripoint_bub_ms & )> adjacent_capturable =
        []( const tripoint_bub_ms & pnt ) {
            const monster* mon_ptr = g->critter_at<monster>( pnt );
            return mon_ptr != nullptr;
        };
        const std::string query =
            string_format( _( "Grab which creature to place in the %s?" ), it->tname() );
        const std::optional<tripoint_bub_ms> target_ = choose_adjacent_highlight(
                query, _( "There is no creature nearby you can capture." ), adjacent_capturable, false );
        if( !target_ ) {
            p->add_msg_if_player( m_info, _( "You cannot use a %s there." ), it->tname() );
            return 0;
        }
        const auto target = *target_;

        // Capture the thing, if it's on the target square.
        if( const monster * const mon_ptr = g->critter_at<monster>( target ) ) {
            const monster& f = *mon_ptr;

            if( f.get_size() > Creature::size_map.find( capacity )->second ) {
                p->add_msg_if_player(
                    m_info, _( "The %1$s is too big to put in your %2$s." ), f.type->nname(),
                    it->tname() );
                return 0;
            }
            // TODO: replace this with some kind of melee check.
            int chance = f.hp_percentage() / 10;
            // A weaker monster is easier to capture.
            // If the monster is friendly, then put it in the item
            // without checking if it rolled a success.
            if( f.friendly != 0 || one_in( chance ) ) {
                p->add_msg_if_player(
                    _( "You capture the %1$s in your %2$s." ), f.type->nname(), it->tname() );
                return it->contain_monster( target );
            } else {
                p->add_msg_if_player(
                    m_bad, _( "The %1$s avoids your attempts to put it in the %2$s." ),
                    f.type->nname(), it->type->nname( 1 ) );
            }
            p->moves -= to_moves<int>( 1_seconds );
        } else {
            add_msg( _( "The %s can't capture nothing" ), it->tname() );
            return 0;
        }
    }
    return 0;
}

int iuse::ladder( player* p, item*, bool, const tripoint_bub_ms & )
{
    if( !g->m.has_zlevels() ) {
        debugmsg( "Ladder can't be used in non-z-level mode" );
        return 0;
    }
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    const std::optional<tripoint_bub_ms> pnt_ = choose_adjacent( _( "Put the ladder where?" ) );
    if( !pnt_ ) { return 0; }
    const auto pnt = *pnt_;

    if( !g->is_empty( pnt ) || g->m.has_furn( pnt ) ) {
        p->add_msg_if_player( m_bad, _( "Can't place it there." ) );
        return 0;
    }

    p->add_msg_if_player( _( "You set down the ladder." ) );
    p->moves -= to_moves<int>( 5_seconds );
    g->m.furn_set( pnt, furn_str_id( "f_ladder" ) );
    return 1;
}


int iuse::weak_antibiotic( player* p, item* it, bool, const tripoint_bub_ms & )
{
    p->add_msg_player_or_npc(
        m_neutral, _( "You take some weak antibiotics." ),
        _( "<npcname> takes some weak antibiotics." ) );
    if( p->has_effect( effect_infected ) && !p->has_effect( effect_weak_antibiotic ) ) {
        p->add_msg_if_player( m_good, _( "The throbbing of the infection diminishes.  Slightly." ) );
    }
    p->add_effect( effect_weak_antibiotic, 12_hours );
    return it->type->charges_to_use();
}

int iuse::strong_antibiotic( player* p, item* it, bool, const tripoint_bub_ms & )
{
    p->add_msg_player_or_npc(
        m_neutral, _( "You take some strong antibiotics." ),
        _( "<npcname> takes some strong antibiotics." ) );
    if( p->has_effect( effect_infected ) && !p->has_effect( effect_strong_antibiotic ) ) {
        p->add_msg_if_player( m_good, _( "You feel much better - almost entirely." ) );
    }
    p->add_effect( effect_strong_antibiotic, 12_hours );
    return it->type->charges_to_use();
}

int iuse::craft( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }

    const std::string craft_name = it->tname();

    if( !it->is_craft() ) {
        debugmsg( "Attempted to start working on non craft '%s.'  Aborting.", craft_name );
        return 0;
    }

    if( !p->can_continue_craft( *it ) ) { return 0; }
    const recipe& rec = it->get_making();
    if( p->has_recipe( &rec, p->crafting_inventory(), character_funcs::get_crafting_helpers( *p ) )
        == -1 ) {
        p->add_msg_player_or_npc(
            _( "You don't know the recipe for the %s and can't continue crafting." ),
            _( "<npcname> doesn't know the recipe for the %s and can't continue crafting." ),
            rec.result_name() );
        return 0;
    }

    bench_location best_bench = find_best_bench( *p, *it );
    p->add_msg_player_or_npc(
        pgettext( "in progress craft", "You start working on the %s." ),
        pgettext( "in progress craft", "<npcname> starts working on the %s." ), craft_name );

    {
        const recipe& rec = it->get_making();
        auto actor = std::make_unique<craft_activity_actor>(
                         &rec, it->charges, it->get_counter(), best_bench.position,
                         std::vector<comp_selection<item_comp>> {}, it->get_cached_tool_selections(),
                         it->get_var( "craft_tools_fully_prepaid", 0 ) == 1 );
        p->assign_activity( std::make_unique<player_activity>( std::move( actor ) ) );
    }

    return 0;
}

int iuse::disassemble( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( !p->is_avatar() ) {
        debugmsg( "disassemble iuse is not implemented for NPCs." );
        return 0;
    }
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    if( !p->has_item( *it ) ) { return 0; }

    crafting::disassemble( *p->as_avatar(), *it );

    return 0;
}

int iuse::melatonin_tablet( player* p, item* it, bool, const tripoint_bub_ms & )
{
    p->add_msg_if_player( _( "You pop a %s." ), it->tname() );
    if( p->has_effect( effect_melatonin_supplements ) ) {
        p->add_msg_if_player( m_warning, _( "Simply taking more melatonin won't help.  You have to "
                                            "go to sleep for it to work." ) );
    }
    p->add_effect( effect_melatonin_supplements, 16_hours );
    return it->type->charges_to_use();
}

int iuse::coin_flip( player* p, item* it, bool, const tripoint_bub_ms & )
{
    p->add_msg_if_player( m_info, _( "You flip a %s." ), it->tname() );
    p->add_msg_if_player( m_info, one_in( 2 ) ? _( "Heads!" ) : _( "Tails!" ) );
    return 0;
}

int iuse::play_game( player* p, item* it, bool t, const tripoint_bub_ms & )
{
    if( t ) { return 0; }

    if( query_yn( _( "Play a game with the %s?" ), it->tname() ) ) {
        p->add_msg_if_player( _( "You start playing." ) );
        p->assign_activity( std::make_unique<player_activity>(
                                std::make_unique<game_activity_actor>( game_type::GENERIC_GAME ) ) );
    }
    return 0;
}

int iuse::magic_8_ball( player* p, item* it, bool, const tripoint_bub_ms & )
{
    enum { BALL8_GOOD, BALL8_UNK = 10, BALL8_BAD = 15 };
    static const std::array<const char *, 20> tab = {
        {
            translate_marker( "It is certain." ),
            translate_marker( "It is decidedly so." ),
            translate_marker( "Without a doubt." ),
            translate_marker( "Yes - definitely." ),
            translate_marker( "You may rely on it." ),
            translate_marker( "As I see it, yes." ),
            translate_marker( "Most likely." ),
            translate_marker( "Outlook good." ),
            translate_marker( "Yes." ),
            translate_marker( "Signs point to yes." ),
            translate_marker( "Reply hazy, try again." ),
            translate_marker( "Ask again later." ),
            translate_marker( "Better not tell you now." ),
            translate_marker( "Cannot predict now." ),
            translate_marker( "Concentrate and ask again." ),
            translate_marker( "Don't count on it." ),
            translate_marker( "My reply is no." ),
            translate_marker( "My sources say no." ),
            translate_marker( "Outlook not so good." ),
            translate_marker( "Very doubtful." )
        }
    };

    p->add_msg_if_player( m_info, _( "You ask the %s, then flip it." ), it->tname() );
    int rn = rng( 0, tab.size() - 1 );
    auto color = ( rn >= BALL8_BAD ? m_bad : rn >= BALL8_UNK ? m_info : m_good );
    p->add_msg_if_player( color, _( "The %s says: %s" ), it->tname(), _( tab[rn] ) );
    return 0;
}

int iuse::toggle_heats_food( player *p, item *it, bool, const tripoint_bub_ms & )
{
    static const flag_id json_flag_HEATS_FOOD( flag_HEATS_FOOD );
    if( !it->has_flag( json_flag_HEATS_FOOD ) ) {
        it->set_flag( json_flag_HEATS_FOOD );
        p->add_msg_if_player(
            _( "You will try to use %s to heat food next time you eat something that should be "
               "eaten hot." ),
            it->tname().c_str() );
    } else {
        it->unset_flag( json_flag_HEATS_FOOD );
        p->add_msg_if_player( _( "You will no longer use %s to heat food." ), it->tname().c_str() );
    }

    return 0;
}

int iuse::toggle_ups_charging( player *p, item *it, bool, const tripoint_bub_ms & )
{
    static const flag_id json_flag_USE_UPS( flag_USE_UPS );
    if( !it->has_flag( json_flag_USE_UPS ) ) {
        it->set_flag( json_flag_USE_UPS );
        p->add_msg_if_player(
            _( "You will recharge the %s using any available Unified Power System." ),
            it->tname().c_str() );
    } else {
        it->unset_flag( json_flag_USE_UPS );
        p->add_msg_if_player( _( "You will no longer recharge the %s via UPS." ), it->tname().c_str() );
    }

    return 0;
}

int iuse::report_grid_charge( player* p, item*, bool, const tripoint_bub_ms& pos )
{
    const tripoint_abs_ms pos_abs( get_map().bub_to_abs( pos ) );
    const distribution_grid& gr = get_distribution_grid_tracker().grid_at( pos_abs );
    const int amt = gr.get_resource();
    const auto stat = gr.get_power_stat();

    std::string msg = string_format( _( "This electric grid stores %d kJ of electric power." ), amt );

    // format in MW/kW with three-point precision
    auto display_watt = []( int64_t watts = 0 ) {
        if( std::abs( watts ) >= 1'000'000 ) {
            return string_format( "%.3f MW", watts / 1'000'000.0 );
        } else if( std::abs( watts ) >= 1'000 ) {
            return string_format( "%.3f kW", watts / 1'000.0 );
        } else {
            return string_format( "%d W", watts );
        }
    };

    if( stat.gen_w > 0 || stat.use_w > 0 ) {
        msg += string_format( _( "\nGeneration: %s" ), display_watt( stat.gen_w ) );
        msg += string_format( _( "\nConsumption: %s" ), display_watt( stat.use_w ) );
        msg += string_format( _( "\nNet: %s" ), display_watt( stat.net_w() ) );
    }
    p->add_msg_if_player( "%s", msg );
    return 0;
}

int iuse::report_grid_connections( player* p, item*, bool, const tripoint_bub_ms& pos )
{
    tripoint_abs_omt pos_abs = project_to<coords::omt>( tripoint_abs_ms( get_map().bub_to_abs(
                                   pos ) ) );
    std::vector<tripoint_rel_omt> connections =
        get_overmapbuffer( p->get_dimension() ).electric_grid_connectivity_at( pos_abs );

    std::vector<std::string> connection_names;
    connection_names.reserve( connections.size() );
    for( const tripoint_rel_omt& delta : connections ) {
        connection_names.push_back( direction_name( direction_from( delta.raw() ) ) );
    }

    std::string msg;
    if( connection_names.empty() ) {
        msg = _( "This electric grid has no connections." );
    } else {
        //~ %s is list of directions
        msg = string_format(
                  _( "This electric grid has connections: %s." ), enumerate_as_string( connection_names ) );
    }
    p->add_msg_if_player( msg );

    return 0;
}

auto iuse::report_fluid_grid_connections( player* p, item*, bool, const tripoint_bub_ms& pos )
-> int
{
    const auto pos_abs = project_to<coords::omt>( tripoint_abs_ms( get_map().bub_to_abs( pos ) ) );
    const auto connections = fluid_grid::grid_connectivity_at( pos_abs );
    const auto fluid_stats = fluid_grid::storage_stats_at( pos_abs );

    auto connection_names = std::vector<std::string> {};
    connection_names.reserve( connections.size() );
    std::ranges::for_each( connections, [&]( const tripoint_rel_omt & delta ) {
        connection_names.push_back( direction_name( direction_from( delta.raw() ) ) );
    } );

    auto msg = std::string{};
    if( connection_names.empty() ) {
        msg = _( "This fluid grid has no connections." );
    } else {
        msg = string_format(
                  _( "This fluid grid has connections: %s." ), enumerate_as_string( connection_names ) );
    }
    p->add_msg_if_player( msg );
    p->add_msg_if_player( string_format(
                              _( "Fluid stored: %1$s/%2$s %3$s." ), format_volume( fluid_stats.stored ),
                              format_volume( fluid_stats.capacity ), volume_units_abbr() ) );
    auto fluid_entries = std::vector<std::string> {};
    std::ranges::for_each( fluid_stats.stored_by_type, [&]( const auto & entry ) {
        if( entry.second <= 0_ml ) { return; }
        const auto name = item::nname( entry.first );
        const auto volume = format_volume( entry.second );
        fluid_entries.push_back( string_format( _( "%1$s: %2$s" ), name, volume ) );
    } );
    if( fluid_entries.empty() ) {
        p->add_msg_if_player( _( "Fluids: empty." ) );
    } else {
        p->add_msg_if_player( string_format( _( "Fluids: %s." ), enumerate_as_string( fluid_entries ) ) );
    }

    return 0;
}

int iuse::modify_grid_connections( player* p, item* it, bool, const tripoint_bub_ms& pos )
{
    tripoint_abs_omt pos_abs = project_to<coords::omt>( tripoint_abs_ms( get_map().bub_to_abs(
                                   pos ) ) );
    std::vector<tripoint_rel_omt> connections =
        get_overmapbuffer( p->get_dimension() ).electric_grid_connectivity_at( pos_abs );

    uilist ui;

    std::bitset<six_cardinal_directions.size()> connection_present;
    for( size_t i = 0; i < six_cardinal_directions.size(); i++ ) {
        const tripoint& delta = six_cardinal_directions[i];
        connection_present[i] =
            std::count( connections.begin(), connections.end(), tripoint_rel_omt( delta ) );
        std::string name = direction_name( direction_from( delta ) );
        int i_int = static_cast<int>( i );
        const char *format =
            connection_present[i]
            ? _( "Remove connection in direction: %s" )
            : _( "Add connection in direction: %s" );
        int new_z = pos.z() + delta.z;
        bool enabled = new_z >= -10 && new_z <= 10;
        ui.addentry( i_int, enabled, i_int, format, name.c_str() );
    }

    ui.query();
    if( ui.ret < 0 ) { return 0; }

    size_t ret = static_cast<size_t>( ui.ret );
    tripoint_abs_omt destination_pos_abs = pos_abs + tripoint_rel_omt( six_cardinal_directions[ret] );
    if( connection_present[ret] ) {
        get_overmapbuffer( p->get_dimension() ).remove_grid_connection( pos_abs, destination_pos_abs );
    } else {
        std::set<tripoint_abs_omt> lhs_locations =
            get_overmapbuffer( p->get_dimension() ).electric_grid_at( pos_abs );
        std::set<tripoint_abs_omt> rhs_locations =
            get_overmapbuffer( p->get_dimension() ).electric_grid_at( destination_pos_abs );
        int cost_mult;
        if( lhs_locations == rhs_locations ) {
            cost_mult = 0;
        } else {
            cost_mult = lhs_locations.size() + rhs_locations.size();
        }
        const requirement_data& reqs = *requirement_add_grid_connection * cost_mult;
        const inventory& crafting_inv = p->crafting_inventory();
        std::string grid_connection_string;
        if( cost_mult == 0 ) {
            grid_connection_string = string_format(
                                         _( "You are connecting two locations in the same grid, with %lu elements." ),
                                         std::max( lhs_locations.size(), rhs_locations.size() ) );
        } else if( lhs_locations.size() == 1 || rhs_locations.size() == 1 ) {
            grid_connection_string = string_format(
                                         _( "You are extending a grid with %lu elements." ),
                                         std::max( lhs_locations.size(), rhs_locations.size() ) );
        } else {
            grid_connection_string = string_format(
                                         _( "You are connecting a grid with %lu elements to a grid with %lu elements." ),
                                         lhs_locations.size(), rhs_locations.size() );
        }

        if( !requirement_add_grid_connection
            ->can_make_with_inventory( crafting_inv, is_crafting_component ) ) {
            popup( string_format(
                       _( "%s\n%s\n%s" ), grid_connection_string, reqs.list_missing(), reqs.list_all() ) );
            return 0;
        }

        // TODO: Long action
        if( ( cost_mult == 0
              && query_yn( string_format(
                               _( "%s\nThis action will not consume any resources.\nAre you sure?" ),
                               grid_connection_string ) ) )
            || query_yn( string_format(
                             std::string( "%s\n%s\n" ) + _( "Are you sure?" ), grid_connection_string,
                             reqs.list_all() ) ) ) {
        } else {
            return 0;
        }


        for( const auto& e : reqs.get_components() ) { p->consume_items( e ); }
        for( const auto& e : reqs.get_tools() ) { p->consume_tools( e ); }
        p->invalidate_crafting_inventory();

        bool success =
            get_overmapbuffer( p->get_dimension() ).add_grid_connection( pos_abs, destination_pos_abs );
        if( success ) { return it->type->charges_to_use(); }
    }

    return 0;
}

auto iuse::modify_fluid_grid_connections( player* p, item* it, bool, const tripoint_bub_ms& pos )
-> int
{
    const auto pos_abs = project_to<coords::omt>( tripoint_abs_ms( get_map().bub_to_abs( pos ) ) );
    const auto connections = fluid_grid::grid_connectivity_at( pos_abs );

    uilist ui;

    auto connection_present = std::bitset<six_cardinal_directions.size()> {};
    std::ranges::
    for_each( std::views::iota( size_t{0}, six_cardinal_directions.size() ), [&]( size_t i ) {
        const auto& delta = six_cardinal_directions[i];
        connection_present[i] =
            std::ranges::find( connections, tripoint_rel_omt( delta ) ) != connections.end();
        const auto name = direction_name( direction_from( delta ) );
        const auto i_int = static_cast<int>( i );
        const auto format =
            connection_present[i]
            ? _( "Remove fluid grid connection in direction: %s" )
            : _( "Add fluid grid connection in direction: %s" );
        const auto new_z = pos.z() + delta.z;
        const auto enabled = new_z >= -10 && new_z <= 10;
        ui.addentry( i_int, enabled, i_int, format, name.c_str() );
    } );

    ui.query();
    if( ui.ret < 0 ) { return 0; }

    const auto ret = static_cast<size_t>( ui.ret );
    const auto destination_pos_abs = pos_abs + tripoint_rel_omt( six_cardinal_directions[ret] );
    if( connection_present[ret] ) {
        fluid_grid::remove_grid_connection( pos_abs, destination_pos_abs );
    } else {
        const auto lhs_locations = fluid_grid::grid_at( pos_abs );
        const auto rhs_locations = fluid_grid::grid_at( destination_pos_abs );
        auto cost_mult = 0;
        if( lhs_locations != rhs_locations ) {
            cost_mult = static_cast<int>( lhs_locations.size() + rhs_locations.size() );
        }
        const auto& reqs = *requirement_add_fluid_grid_connection * cost_mult;
        const auto& crafting_inv = p->crafting_inventory();
        auto grid_connection_string = std::string{};
        if( cost_mult == 0 ) {
            grid_connection_string = string_format(
                                         _( "You are connecting two locations in the same grid, with %lu elements." ),
                                         std::max( lhs_locations.size(), rhs_locations.size() ) );
        } else if( lhs_locations.size() == 1 || rhs_locations.size() == 1 ) {
            grid_connection_string = string_format(
                                         _( "You are extending a grid with %lu elements." ),
                                         std::max( lhs_locations.size(), rhs_locations.size() ) );
        } else {
            grid_connection_string = string_format(
                                         _( "You are connecting a grid with %lu elements to a grid with %lu elements." ),
                                         lhs_locations.size(), rhs_locations.size() );
        }

        if( !reqs.can_make_with_inventory( crafting_inv, is_crafting_component ) ) {
            popup( string_format(
                       _( "%s\n%s\n%s" ), grid_connection_string, reqs.list_missing(), reqs.list_all() ) );
            return 0;
        }

        if( ( cost_mult == 0
              && query_yn( string_format(
                               _( "%s\nThis action will not consume any resources.\nAre you sure?" ),
                               grid_connection_string ) ) )
            || query_yn( string_format(
                             std::string( "%s\n%s\n" ) + _( "Are you sure?" ), grid_connection_string,
                             reqs.list_all() ) ) ) {
        } else {
            return 0;
        }

        std::ranges::for_each( reqs.get_components(), [&]( const auto & e ) { p->consume_items( e ); } );
        std::ranges::for_each( reqs.get_tools(), [&]( const auto & e ) { p->consume_tools( e ); } );
        p->invalidate_crafting_inventory();

        const auto success = fluid_grid::add_grid_connection( pos_abs, destination_pos_abs );
        if( success ) { return it->type->charges_to_use(); }
    }

    return 0;
}

int iuse::amputate( player*, item* it, bool, const tripoint_bub_ms& pos )
{
    if( !it->ammo_sufficient() ) { return 0; }

    Creature* patient = g->critter_at<Character>( pos );
    if( !patient ) {
        add_msg( m_info, _( "Nevermind." ) );
        return 0;
    }

    auto& body = patient->get_body();

    uilist bp_menu;
    bp_menu.text = _( "Select body part to amputate:" );
    bp_menu.allow_cancel = true;

    for( const auto& pr : body ) { bp_menu.addentry( pr.first->name_as_heading.translated() ); }

    bp_menu.query();
    if( bp_menu.ret < 0 ) {
        add_msg( m_info, _( "Nevermind." ) );
        return 0;
    }

    auto bp_iter = std::next( body.begin(), bp_menu.ret );
    // Prepare for bugs!
    add_msg( m_bad, _( "Body part removed: %s" ), bp_iter->first->name_as_heading.translated() );
    body.erase( bp_iter );

    return it->type->charges_to_use();
}

void use_function::dump_info( const item& it, std::vector<iteminfo> &dump ) const
{
    if( actor != nullptr ) { actor->info( it, dump ); }
}

ret_val<bool> use_function::can_call(
    const Character& p, const item& it, bool t, const tripoint_bub_ms& pos ) const
{
    if( actor == nullptr ) {
        return ret_val <
               bool >::make_failure( _( "You can't do anything interesting with your %s." ), it.tname() );
    }

    return actor->can_use( p, it, t, pos );
}

int use_function::call( player& p, item& it, bool active, const tripoint_bub_ms& pos ) const
{
    return actor->use( p, it, active, pos );
}

int iuse::bullet_vibe_on( player* p, item* it, bool t, const tripoint_bub_ms & )
{
    if( t ) { // Normal use
        if( p->has_item( *it ) ) {
            // Only triggers every 1 minute so that fatigue isn't ridiculous
            if( calendar::once_every( 1_minutes ) ) {
                p->add_morale( MORALE_FEELING_GOOD, 1, 30, 20_minutes, 10_minutes, true );
                p->mod_fatigue( 1 );
            }
        }
    } else {
        // Most generic way to figure out the base item I can think of
        // There's *probably* a better way to do this, but this works
        std::string active_item = it->typeId().str();
        std::string base_item = active_item.erase( active_item.rfind( '_' ) );

        p->add_msg_if_player( _( "The %s turns off." ), it->display_name() );
        it->convert( itype_id( base_item ) );
        it->deactivate();
    }
    return it->type->charges_to_use();
}
