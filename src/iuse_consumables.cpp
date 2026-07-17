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


/* iuse methods return the number of charges expended, which is usually it->charges_to_use().
 * Some items that don't normally use charges return 1 to indicate they're used up.
 * Regardless, returning 0 indicates the item has not been used up,
 * though it may have been successfully activated.
 */

int iuse::sewage( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( !p->query_yn( _( "Are you sure you want to drink… this?" ) ) ) { return 0; }

    g->events().send<event_type::eats_sewage>();
    p->vomit();
    if( one_in( 4 ) ) { p->mutate(); }
    return it->type->charges_to_use();
}

int iuse::honeycomb( player* p, item* it, bool, const tripoint_bub_ms & )
{
    g->m.spawn_item( p->bub_pos(), itype_wax, 2 );
    return it->type->charges_to_use();
}

int iuse::xanax( player* p, item* it, bool, const tripoint_bub_ms & )
{
    p->add_msg_if_player( _( "You take some %s." ), it->tname() );
    p->add_effect( effect_took_xanax, 90_minutes );
    return it->type->charges_to_use();
}

static constexpr time_duration alc_strength(
    const int strength, const time_duration& weak, const time_duration& medium,
    const time_duration& strong )
{
    return strength == 0 ? weak : strength == 1 ? medium : strong;
}

static int alcohol( player& p, const item& it, const int strength )
{
    // Weaker characters are cheap drunks
    /** @EFFECT_STR_MAX reduces drunkenness duration */
    time_duration duration =
        alc_strength( strength, 22_minutes, 34_minutes, 45_minutes )
        - ( alc_strength( strength, 36_seconds, 1_minutes, 72_seconds ) * p.str_max );
    if( p.has_trait( trait_ALCMET ) ) {
        duration = alc_strength( strength, 6_minutes, 14_minutes, 18_minutes )
                   - ( alc_strength( strength, 36_seconds, 1_minutes, 1_minutes ) * p.str_max );
        // Metabolizing the booze improves the nutritional value;
        // might not be healthy, and still causes Thirst problems, though
        p.mod_stored_kcal( 10 * ( std::abs( it.get_comestible() ? it.type->comestible->stim : 0 ) ) );
        // Metabolizing it cancels out the depressant
        p.mod_stim( std::abs( it.get_comestible() ? it.get_comestible()->stim : 0 ) );
    } else if( p.has_trait( trait_TOLERANCE ) ) {
        duration -= alc_strength( strength, 9_minutes, 16_minutes, 24_minutes );
    } else if( p.has_trait( trait_LIGHTWEIGHT ) ) {
        duration += alc_strength( strength, 9_minutes, 16_minutes, 24_minutes );
    }
    p.add_effect( effect_drunk, duration );
    return it.type->charges_to_use();
}

int iuse::alcohol_weak( player* p, item* it, bool, const tripoint_bub_ms & )
{
    return alcohol( *p, *it, 0 );
}

int iuse::alcohol_medium( player* p, item* it, bool, const tripoint_bub_ms & )
{
    return alcohol( *p, *it, 1 );
}

int iuse::alcohol_strong( player* p, item* it, bool, const tripoint_bub_ms & )
{
    return alcohol( *p, *it, 2 );
}

int iuse::antibiotic( player* p, item* it, bool, const tripoint_bub_ms & )
{
    p->add_msg_player_or_npc(
        m_neutral, _( "You take some antibiotics." ), _( "<npcname> takes some antibiotics." ) );
    if( p->has_effect( effect_infected ) && !p->has_effect( effect_antibiotic ) ) {
        p->add_msg_if_player( m_good, _( "Maybe just placebo effect, but you feel a little better as "
                                         "the dose settles in." ) );
    }
    p->add_effect( effect_antibiotic, 12_hours );
    return it->type->charges_to_use();
}

int iuse::eyedrops( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_underwater() ) {
        p->add_msg_if_player( m_info, _( "You can't do that while underwater." ) );
        return 0;
    }
    if( it->charges < it->type->charges_to_use() ) {
        p->add_msg_if_player( _( "You're out of %s." ), it->tname() );
        return 0;
    }
    p->add_msg_if_player( _( "You use your %s." ), it->tname() );
    p->moves -= to_moves<int>( 10_seconds );
    if( p->has_effect( effect_boomered ) ) {
        p->remove_effect( effect_boomered );
        p->add_msg_if_player( m_good, _( "You wash the slime from your eyes." ) );
    }
    return it->type->charges_to_use();
}

int iuse::fungicide( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_underwater() ) {
        p->add_msg_if_player( m_info, _( "You can't do that while underwater." ) );
        return 0;
    }

    const bool has_fungus = p->has_effect( effect_fungus );
    const bool has_spores = p->has_effect( effect_spores );

    if( p->is_npc() && !has_fungus && !has_spores ) { return 0; }

    p->add_msg_player_or_npc( _( "You use your fungicide." ), _( "<npcname> uses some fungicide" ) );
    if( has_fungus && ( one_in( 3 ) ) ) {
        p->remove_effect( effect_fungus );
        p->add_msg_if_player( m_warning, _( "You feel a burning sensation under your skin that "
                                            "quickly fades away." ) );
    }
    if( has_spores && ( one_in( 2 ) ) ) {
        if( !p->has_effect( effect_fungus ) ) {
            p->add_msg_if_player( m_warning, _( "Your skin grows warm for a moment." ) );
        }
        p->remove_effect( effect_spores );
        int spore_count = rng( 1, 6 );
        for( const tripoint_bub_ms& dest : g->m.points_in_radius( p->bub_pos(), 1 ) ) {
            if( spore_count == 0 ) { break; }
            if( dest == p->bub_pos() ) { continue; }
            if( g->m.passable( dest ) && !get_map().obstructed_by_vehicle_rotation( p->bub_pos(), dest )
                && x_in_y( spore_count, 8 ) ) {
                if( monster * const mon_ptr = g->critter_at<monster>( dest ) ) {
                    monster& critter = *mon_ptr;
                    if( g->u.sees( dest ) && !critter.type->in_species( FUNGUS ) ) {
                        add_msg( m_warning, _( "The %s is covered in tiny spores!" ), critter.name() );
                    }
                    if( !critter.make_fungus() ) {
                        critter.die( p ); // counts as kill by player
                    }
                } else {
                    g->place_critter_at( mon_spore, dest );
                }
                spore_count--;
            }
        }
    }
    return it->type->charges_to_use();
}

int iuse::antifungal( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_underwater() ) {
        p->add_msg_if_player( m_info, _( "You can't do that while underwater." ) );
        return 0;
    }
    p->add_msg_if_player( _( "You take some antifungal medication." ) );
    if( p->has_effect( effect_fungus ) ) {
        p->remove_effect( effect_fungus );
        p->add_msg_if_player( m_warning, _( "You feel a burning sensation under your skin that "
                                            "quickly fades away." ) );
    }
    if( p->has_effect( effect_spores ) ) {
        if( !p->has_effect( effect_fungus ) ) {
            p->add_msg_if_player( m_warning, _( "Your skin grows warm for a moment." ) );
        }
    }
    return it->type->charges_to_use();
}

int iuse::antiparasitic( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_underwater() ) {
        p->add_msg_if_player( m_info, _( "You can't do that while underwater." ) );
        return 0;
    }
    p->add_msg_if_player( _( "You take some antiparasitic medication." ) );
    if( p->has_effect( effect_dermatik ) ) {
        p->remove_effect( effect_dermatik );
        p->add_msg_if_player( m_good, _( "The itching sensation under your skin fades away." ) );
    }
    if( p->has_effect( effect_tapeworm ) ) {
        p->remove_effect( effect_tapeworm );
        if( p->has_trait( trait_NOPAIN ) ) {
            p->add_msg_if_player( m_good, _( "Your bowels clench as something inside them dies." ) );
        } else {
            p->add_msg_if_player( m_mixed, _( "Your bowels spasm painfully as something inside them "
                                              "dies." ) );
            p->mod_pain( rng( 8, 24 ) );
        }
    }
    if( p->has_effect( effect_bloodworms ) ) {
        p->remove_effect( effect_bloodworms );
        p->add_msg_if_player( _( "Your skin prickles and your veins itch for a few moments." ) );
    }
    if( p->has_effect( effect_brainworms ) ) {
        p->remove_effect( effect_brainworms );
        if( p->has_trait( trait_NOPAIN ) ) {
            p->add_msg_if_player( m_good, _( "The pressure inside your head feels better already." ) );
        } else {
            p->add_msg_if_player( m_mixed, _( "Your head pounds like a sore tooth as something "
                                              "inside of it dies." ) );
            p->mod_pain( rng( 8, 24 ) );
        }
    }
    if( p->has_effect( effect_paincysts ) ) {
        p->remove_effect( effect_paincysts );
        if( p->has_trait( trait_NOPAIN ) ) {
            p->add_msg_if_player( m_good, _( "The stiffness in your joints goes away." ) );
        } else {
            p->add_msg_if_player( m_good, _( "The pain in your joints goes away." ) );
        }
    }
    return it->type->charges_to_use();
}

int iuse::anticonvulsant( player* p, item* it, bool, const tripoint_bub_ms & )
{
    p->add_msg_if_player( _( "You take some anticonvulsant medication." ) );
    /** @EFFECT_STR reduces duration of anticonvulsant medication */
    time_duration duration = 8_hours - p->str_cur * rng( 0_turns, 10_minutes );
    if( p->has_trait( trait_TOLERANCE ) ) { duration -= 1_hours; }
    if( p->has_trait( trait_LIGHTWEIGHT ) ) { duration += 2_hours; }
    p->add_effect( effect_valium, duration );
    if( p->has_effect( effect_shakes ) ) {
        p->remove_effect( effect_shakes );
        p->add_msg_if_player( m_good, _( "You stop shaking." ) );
    }
    return it->type->charges_to_use();
}

int iuse::meth( player* p, item* it, bool, const tripoint_bub_ms & )
{
    /** @EFFECT_STR reduces duration of meth */
    time_duration duration = 1_minutes * ( 60 - p->str_cur );
    if( p->has_amount( itype_apparatus, 1 ) && p->use_charges_if_avail( itype_fire, 1 ) ) {
        p->add_msg_if_player( m_neutral, _( "You smoke your meth." ) );
        p->add_msg_if_player( m_good, _( "The world seems to sharpen." ) );
        p->mod_fatigue( -375 );
        if( p->has_trait( trait_TOLERANCE ) ) {
            duration *= 1.2;
        } else {
            duration *= ( p->has_trait( trait_LIGHTWEIGHT ) ? 1.8 : 1.5 );
        }
        // breathe out some smoke
        for( int i = 0; i < 3; i++ ) {
            g->m.add_field(
            {p->bub_pos().x() + rng( -2, 2 ), p->bub_pos().y() + rng( -2, 2 ), p->bub_pos().z()},
            fd_methsmoke, 2 );
        }
    } else {
        p->add_msg_if_player( _( "You snort some crystal meth." ) );
        p->mod_fatigue( -300 );
    }
    if( !p->has_effect( effect_meth ) ) { duration += 1_hours; }
    if( duration > 0_turns ) {
        // meth actually inhibits hunger, weaker characters benefit more
        /** @EFFECT_STR_MAX >4 experiences less hunger benefit from meth */
        int hungerpen = ( p->str_max < 5 ? 35 : 40 - ( 2 * p->str_max ) );
        if( hungerpen > 0 ) { p->mod_stored_kcal( 10 * hungerpen ); }
        p->add_effect( effect_meth, duration );
    }
    return it->type->charges_to_use();
}

int iuse::vaccine( player* p, item* it, bool, const tripoint_bub_ms & )
{
    p->add_msg_if_player( _( "You inject the vaccine." ) );
    p->add_msg_if_player( m_good, _( "You feel tough." ) );
    p->mod_healthy_mod( 200, 200 );
    p->mod_pain( 3 );
    p->i_add( item::spawn( "syringe", it->birthday() ) );
    return it->type->charges_to_use();
}

int iuse::antiasthmatic( player* p, item* it, bool, const tripoint_bub_ms & )
{
    p->add_msg_if_player( m_good, _( "You no longer need to worry about asthma attacks, at least for "
                                     "a while." ) );
    p->add_effect( effect_took_antiasthmatic, 1_days, bodypart_str_id::NULL_ID() );
    return it->type->charges_to_use();
}

int iuse::poison( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( ( p->has_trait( trait_EATDEAD ) ) ) { return it->type->charges_to_use(); }

    // NPCs have a magical sense of what is inedible
    // Players can abuse the crafting menu instead...
    if( !it->has_flag( flag_HIDDEN_POISON )
        && ( p->is_npc()
             || !p->query_yn( _( "Are you sure you want to eat this?  It looks poisonous…" ) ) ) ) {
        return 0;
    }
    if( !p->has_trait( trait_POISRESIST ) ) { p->add_effect( effect_poison, 15_minutes ); }

    p->add_effect( effect_foodpoison, 3_hours );
    return it->type->charges_to_use();
}

int iuse::meditate( player* p, item* it, bool t, const tripoint_bub_ms & )
{
    if( !p || t ) { return 0; }
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    if( p->has_trait( trait_SPIRITUAL ) ) {
        const int moves = to_moves<int>( 20_minutes );
        p->assign_activity( std::make_unique<player_activity>(
                                std::make_unique<morale_activity_actor>( morale_act_type::MEDITATE ) ) );
    } else {
        p->add_msg_if_player( _( "This %s probably meant a lot to someone at one time." ), it->tname() );
    }
    return it->type->charges_to_use();
}

int iuse::thorazine( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->has_effect( effect_took_thorazine ) ) {
        p->remove_effect( effect_took_thorazine );
        p->mod_fatigue( 15 );
    }
    p->add_effect( effect_took_thorazine, 12_hours );
    p->mod_fatigue( 5 );
    p->remove_effect( effect_hallu );
    p->remove_effect( effect_visuals );
    if( !p->has_effect( effect_dermatik ) ) { p->remove_effect( effect_formication ); }
    if( one_in( 50 ) ) { // adverse reaction
        p->add_msg_if_player( m_bad, _( "You feel completely exhausted." ) );
        p->mod_fatigue( 50 );
    } else {
        p->add_msg_if_player( m_warning, _( "You feel a bit wobbly." ) );
    }
    return it->type->charges_to_use();
}

int iuse::prozac( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( !p->has_effect( effect_took_prozac ) ) {
        p->add_effect( effect_took_prozac, 12_hours );
    } else {
        p->mod_stim( 3 );
    }
    if( one_in( 16 ) ) { // adverse reaction, same duration as prozac effect.
        p->add_msg_if_player( m_warning, _( "You suddenly feel hollow inside." ) );
        p->add_effect( effect_took_prozac_bad, p->get_effect_dur( effect_took_prozac ) );
    }
    return it->type->charges_to_use();
}

int iuse::sleep( player* p, item* it, bool, const tripoint_bub_ms & )
{
    p->add_msg_if_player( m_warning, _( "You feel sleepy…" ) );
    return it->type->charges_to_use();
}

int iuse::datura( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_npc() ) { return 0; }

    p->add_effect( effect_datura, rng( 3_hours, 13_hours ) );
    p->add_msg_if_player( _( "You eat the datura seed." ) );
    if( p->has_trait( trait_SPIRITUAL ) ) {
        p->add_morale( MORALE_FOOD_GOOD, 36, 72, 2_hours, 1_hours, false, it->type );
    }
    return it->type->charges_to_use();
}

int iuse::flumed( player* p, item* it, bool, const tripoint_bub_ms & )
{
    p->add_effect( effect_took_flumed, 10_hours );
    p->add_msg_if_player( _( "You take some %s" ), it->tname() );
    return it->type->charges_to_use();
}

int iuse::flusleep( player* p, item* it, bool, const tripoint_bub_ms & )
{
    p->add_effect( effect_took_flumed, 12_hours );
    p->add_msg_if_player( _( "You take some %s" ), it->tname() );
    p->add_msg_if_player( m_warning, _( "You feel sleepy…" ) );
    return it->type->charges_to_use();
}

int iuse::inhaler( player* p, item* it, bool, const tripoint_bub_ms & )
{
    p->add_msg_player_or_npc(
        m_neutral, _( "You take a puff from your inhaler." ),
        _( "<npcname> takes a puff from their inhaler." ) );
    if( !p->remove_effect( effect_asthma ) ) {
        p->mod_fatigue( -3 ); // if we don't have asthma can be used as stimulant
        if( one_in( 20 ) ) { // with a small but significant risk of adverse reaction
            p->add_effect( effect_shakes, rng( 2_minutes, 5_minutes ) );
        }
    }
    p->add_effect( effect_took_antiasthmatic, rng( 1_hours, 2_hours ) );
    p->remove_effect( effect_smoke );
    return it->type->charges_to_use();
}

int iuse::oxygen_bottle( player* p, item* it, bool, const tripoint_bub_ms & )
{
    p->moves -= to_moves<int>( 10_seconds );
    p->add_msg_player_or_npc(
        m_neutral, string_format( _( "You breathe deeply from the %s" ), it->tname() ),
        string_format( _( "<npcname> breathes from the %s" ), it->tname() ) );
    if( p->has_effect( effect_smoke ) ) {
        p->remove_effect( effect_smoke );
    } else if( p->has_effect( effect_teargas ) ) {
        p->remove_effect( effect_teargas );
    } else if( p->has_effect( effect_asthma ) ) {
        p->remove_effect( effect_asthma );
    } else if( p->get_stim() < 16 ) {
        p->mod_stim( 8 );
        p->mod_painkiller( 2 );
    }
    p->mod_painkiller( 2 );
    return it->type->charges_to_use();
}
