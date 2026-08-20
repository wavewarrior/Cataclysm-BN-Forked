#include "iuse.h"

#include "action.h"
#include "action_time_scale.h"
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

namespace
{
auto is_hackable_robot( const monster& mon ) -> bool
{
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

bool iuse::robotcontrol_can_target( player* p, const monster& m )
{
    return !m.is_dead() && is_hackable_robot( m ) && m.friendly == 0
           && rl_dist( p->bub_pos(), m.bub_pos() ) <= 10;
}

int iuse::robotcontrol( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( !it->units_sufficient( *p ) ) {
        p->add_msg_if_player( _( "The %s's batteries are dead." ), it->tname() );
        return 0;
    }
    if( p->has_trait( trait_ILLITERATE ) ) {
        p->add_msg_if_player( _( "You cannot read a computer screen." ) );
        return 0;
    }

    if( p->has_trait( trait_HYPEROPIC ) && !p->worn_with_flag( flag_FIX_FARSIGHT )
        && !p->has_effect( effect_contacts ) && !p->has_bionic( bio_eye_optic ) ) {
        p->add_msg_if_player( m_info, _( "You'll need to put on reading glasses before you can see "
                                         "the screen." ) );
        return 0;
    }

    int choice = uilist(
    _( "Welcome to hackPRO!:" ), {
        _( "Prepare IFF protocol override" ), _( "Set friendly robots to passive mode" ),
        _( "Set friendly robots to combat mode" )
    } );
    switch( choice ) {
        case 0: { // attempt to make a robot friendly
            uilist pick_robot;
            pick_robot.text = _( "Choose an endpoint to hack." );
            // Build a list of all unfriendly robots in range.
            // TODO: change into vector<Creature*>
            std::vector<shared_ptr_fast<monster>> mons;
            std::vector<tripoint_bub_ms> locations;
            int entry_num = 0;
            for( const monster& candidate : g->all_monsters() ) {
                if( robotcontrol_can_target( p, candidate ) ) {
                    mons.push_back( g->shared_from( candidate ) );
                    pick_robot.addentry( entry_num++, true, MENU_AUTOASSIGN, candidate.name() );
                    tripoint_bub_ms seen_loc;
                    // Show locations of seen robots, center on player if robot is not seen
                    if( p->sees( candidate ) ) {
                        seen_loc = candidate.bub_pos();
                    } else {
                        seen_loc = p->bub_pos();
                    }
                    locations.push_back( seen_loc );
                }
            }
            if( mons.empty() ) {
                p->add_msg_if_player( m_info, _( "No enemy robots in range." ) );
                return it->type->charges_to_use();
            }
            pointmenu_cb callback( locations );
            pick_robot.callback = &callback;
            pick_robot.query();
            if( pick_robot.ret < 0 || static_cast<size_t>( pick_robot.ret ) >= mons.size() ) {
                p->add_msg_if_player( m_info, _( "Never mind" ) );
                return it->type->charges_to_use();
            }
            const size_t mondex = pick_robot.ret;
            shared_ptr_fast<monster> z = mons[mondex];
            p->add_msg_if_player( _( "You start reprogramming the %s into an ally." ), z->name() );

            /** @EFFECT_INT speeds up hacking preperation */
            /** @EFFECT_COMPUTER speeds up hacking preperation */
            int move_cost =
                std::max( 100, 1000 - p->int_cur * 10 - p->get_skill_level( skill_computer ) * 10 );
            p->assign_activity(
                std::make_unique<player_activity>( std::make_unique<robot_control_activity_actor>(
                        weak_ptr_fast<monster>( z ), z->name(), move_cost ) ) );

            return it->type->charges_to_use();
        }
        case 1: { // make all friendly robots stop their purposeless extermination of (un)life.
            p->moves -= to_moves<int>( 1_seconds );
            auto hackables = get_hackable_friendly_monsters( *g );
            const auto f = hackables.empty() ? 0 : 1;
            std::ranges::for_each( hackables, [&]( const shared_ptr_fast<monster> &critter ) {
                p->add_msg_if_player( _( "A following %s goes into passive mode." ), critter->name() );
                critter->add_effect( effect_docile, 1_turns );
            } );
            if( f == 0 ) {
                p->add_msg_if_player( _( "You are not commanding any robots." ) );
                return 0;
            }
            return it->type->charges_to_use();
        }
        case 2: { // make all friendly robots terminate (un)life with extreme prejudice
            p->moves -= to_moves<int>( 1_seconds );
            auto hackables = get_hackable_friendly_monsters( *g );
            const auto f = hackables.empty() ? 0 : 1;
            std::ranges::for_each( hackables, [&]( const shared_ptr_fast<monster> &critter ) {
                p->add_msg_if_player( _( "A following %s goes into combat mode." ), critter->name() );
                critter->remove_effect( effect_docile );
            } );
            if( f == 0 ) {
                p->add_msg_if_player( _( "You are not commanding any robots." ) );
                return 0;
            }
            return it->type->charges_to_use();
        }
    }
    return 0;
}

static void init_memory_card_with_random_stuff( item& it )
{
    if( it.has_flag( flag_MC_MOBILE )
        && ( it.has_flag( flag_MC_RANDOM_STUFF ) || it.has_flag( flag_MC_SCIENCE_STUFF ) )
        && !( it.has_flag( flag_MC_USED ) || it.has_flag( flag_MC_HAS_DATA ) ) ) {

        it.set_flag( flag_MC_HAS_DATA );

        bool encrypted = false;

        if( it.has_flag( flag_MC_MAY_BE_ENCRYPTED ) && one_in( 8 ) ) {
            it.convert( itype_id( it.typeId().str() + "_encrypted" ) );
        }

        // some special cards can contain "MC_ENCRYPTED" flag
        if( it.has_flag( flag_MC_ENCRYPTED ) ) { encrypted = true; }

        int data_chance = 2;

        // encrypted memory cards often contain data
        if( encrypted && !one_in( 3 ) ) { data_chance--; }

        // just empty memory card
        if( !one_in( data_chance ) ) { return; }

        // add someone's personal photos
        if( one_in( data_chance ) ) {

            // decrease chance to more data
            data_chance++;

            if( encrypted && one_in( 3 ) ) { data_chance--; }

            const int duckfaces_count = rng( 5, 30 );
            it.set_var( "MC_PHOTOS", duckfaces_count );
        }
        // decrease chance to music and other useful data
        data_chance++;
        if( encrypted && one_in( 2 ) ) { data_chance--; }

        if( one_in( data_chance ) ) {
            data_chance++;

            if( encrypted && one_in( 3 ) ) { data_chance--; }

            const int new_songs_count = rng( 5, 15 );
            it.set_var( "MC_MUSIC", new_songs_count );
        }
        data_chance++;
        if( encrypted && one_in( 2 ) ) { data_chance--; }

        if( one_in( data_chance ) ) { it.set_var( "MC_RECIPE", "SIMPLE" ); }

        if( it.has_flag( flag_MC_SCIENCE_STUFF ) ) { it.set_var( "MC_RECIPE", "SCIENCE" ); }
    }
}

static bool einkpc_download_memory_card( player& p, item& eink, item& mc )
{
    bool something_downloaded = false;
    if( mc.get_var( "MC_PHOTOS", 0 ) > 0 ) {
        something_downloaded = true;

        int new_photos = mc.get_var( "MC_PHOTOS", 0 );
        mc.erase_var( "MC_PHOTOS" );

        p.add_msg_if_player(
            m_good,
            vgettext( "You download %d new photo into internal memory.",
                      "You download %d new photos into internal memory.", new_photos ),
            new_photos );

        const int old_photos = eink.get_var( "EIPC_PHOTOS", 0 );
        eink.set_var( "EIPC_PHOTOS", old_photos + new_photos );
    }

    if( mc.get_var( "MC_MUSIC", 0 ) > 0 ) {
        something_downloaded = true;

        int new_songs = mc.get_var( "MC_MUSIC", 0 );
        mc.erase_var( "MC_MUSIC" );

        p.add_msg_if_player(
            m_good,
            vgettext( "You download %d new song into internal memory.",
                      "You download %d new songs into internal memory.", new_songs ),
            new_songs );

        const int old_songs = eink.get_var( "EIPC_MUSIC", 0 );
        eink.set_var( "EIPC_MUSIC", old_songs + new_songs );
    }

    if( !mc.get_var( "MC_RECIPE" ).empty() ) {
        const bool science = mc.get_var( "MC_RECIPE" ) == "SCIENCE";

        mc.erase_var( "MC_RECIPE" );

        std::vector<const recipe *> candidates;

        for( const auto& e : recipe_dict ) {
            const auto& r = e.second;
            if( r.never_learn ) { continue; }
            if( science ) {
                if( r.difficulty >= 3 && one_in( r.difficulty + 1 ) ) { candidates.push_back( &r ); }
            } else {
                if( r.category == "CC_FOOD" ) {
                    if( r.difficulty <= 3 && one_in( r.difficulty ) ) { candidates.push_back( &r ); }
                }
            }
        }

        if( !candidates.empty() ) {

            const recipe* r = random_entry( candidates );
            const recipe_id& rident = r->ident();

            const auto old_recipes = eink.get_var( "EIPC_RECIPES" );
            if( old_recipes.empty() ) {
                something_downloaded = true;
                eink.set_var( "EIPC_RECIPES", "," + rident.str() + "," );

                p.add_msg_if_player(
                    m_good, _( "You download a recipe for %s into the tablet's memory." ),
                    r->result_name() );
            } else {
                if( old_recipes.find( "," + rident.str() + "," ) == std::string::npos ) {
                    something_downloaded = true;
                    eink.set_var( "EIPC_RECIPES", old_recipes + rident.str() + "," );

                    p.add_msg_if_player(
                        m_good, _( "You download a recipe for %s into the tablet's memory." ),
                        r->result_name() );
                } else {
                    p.add_msg_if_player(
                        m_good, _( "Your tablet already has a recipe for %s." ), r->result_name() );
                }
            }
        }
    }

    if( mc.has_var( "MC_EXTENDED_PHOTOS" ) ) {
        std::vector<extended_photo_def> extended_photos;
        try {
            item_read_extended_photos( mc, extended_photos, "MC_EXTENDED_PHOTOS" );
            item_read_extended_photos( eink, extended_photos, "EIPC_EXTENDED_PHOTOS", true );
            item_write_extended_photos( eink, extended_photos, "EIPC_EXTENDED_PHOTOS" );
            something_downloaded = true;
            p.add_msg_if_player( m_good, _( "You have downloaded your photos." ) );
        } catch( const JsonError& e ) {
            debugmsg( "Error card reading photos (loaded photos = %i) : %s", extended_photos.size(),
                      e.c_str() );
        }
    }

    const auto monster_photos = mc.get_var( "MC_MONSTER_PHOTOS" );
    if( !monster_photos.empty() ) {
        something_downloaded = true;
        p.add_msg_if_player( m_good, _( "You have updated your monster collection." ) );

        auto photos = eink.get_var( "EINK_MONSTER_PHOTOS" );
        if( photos.empty() ) {
            eink.set_var( "EINK_MONSTER_PHOTOS", monster_photos );
        } else {
            std::istringstream f( monster_photos );
            std::string s;
            while( getline( f, s, ',' ) ) {

                if( s.empty() ) { continue; }

                const std::string mtype = s;
                getline( f, s, ',' );
                char *chq = s.data();
                const int quality = atoi( chq );

                const size_t eink_strpos = photos.find( "," + mtype + "," );

                if( eink_strpos == std::string::npos ) {
                    photos += mtype + "," + string_format( "%d", quality ) + ",";
                } else {

                    const size_t strqpos = eink_strpos + mtype.size() + 2;
                    char *chq = &photos[strqpos];
                    const int old_quality = atoi( chq );

                    if( quality > old_quality ) {
                        photos[strqpos] = string_format( "%d", quality )[0];
                    }
                }
            }
            eink.set_var( "EINK_MONSTER_PHOTOS", photos );
        }
    }

    if( mc.has_flag( flag_MC_TURN_USED ) ) {
        mc.clear_vars();
        mc.unset_flags();
        mc.convert( itype_mobile_memory_card_used );
    }

    if( !something_downloaded ) {
        p.add_msg_if_player( m_info, _( "This memory card does not contain any new data." ) );
        return false;
    }

    return true;
}

static std::string photo_quality_name( const int index )
{
    static const std::array<std::string, 6> names{
        {
            //~ photo quality adjective
            {translate_marker( "awful" )},
            {translate_marker( "bad" )},
            {translate_marker( "not bad" )},
            {translate_marker( "good" )},
            {translate_marker( "fine" )},
            {translate_marker( "exceptional" )}
        }};
    return _( names[index] );
}

int iuse::einktabletpc( player* p, item* it, bool t, const tripoint_bub_ms& pos )
{
    if( t ) {
        if( !it->get_var( "EIPC_MUSIC_ON" ).empty() && ( it->ammo_remaining() > 0 ) ) {
            if( action_time_scale::once_every_this_tick( 5_minutes ) ) { it->ammo_consume( 1, p->bub_pos() ); }

            // the more varied music, the better max mood.
            const int songs = it->get_var( "EIPC_MUSIC", 0 );
            play_music( *p, pos, 8, std::min( 25, songs ) );
        } else {
            it->deactivate();
            it->erase_var( "EIPC_MUSIC_ON" );
            p->add_msg_if_player( m_info, _( "Tablet's batteries are dead." ) );
        }

        return 0;
    } else if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    } else if( !p->is_npc() ) {

        enum {
            ei_invalid,
            ei_photo,
            ei_music,
            ei_recipe,
            ei_uploaded_photos,
            ei_monsters,
            ei_download,
            ei_decrypt
        };

        if( p->is_underwater() ) {
            p->add_msg_if_player( m_info, _( "You can't do that while underwater." ) );
            return 0;
        }
        if( p->has_trait( trait_ILLITERATE ) ) {
            p->add_msg_if_player( m_info, _( "You cannot read a computer screen." ) );
            return 0;
        }
        if( p->has_trait( trait_HYPEROPIC ) && !p->worn_with_flag( flag_FIX_FARSIGHT )
            && !p->has_effect( effect_contacts ) && !p->has_bionic( bio_eye_optic ) ) {
            p->add_msg_if_player( m_info, _( "You'll need to put on reading glasses before you can "
                                             "see the screen." ) );
            return 0;
        }

        uilist amenu;

        amenu.text = _( "Choose menu option:" );

        const int photos = it->get_var( "EIPC_PHOTOS", 0 );
        if( photos > 0 ) {
            amenu.addentry( ei_photo, true, 'p', _( "Unsorted photos [%d]" ), photos );
        } else {
            amenu.addentry( ei_photo, false, 'p', _( "No photos on device" ) );
        }

        const int songs = it->get_var( "EIPC_MUSIC", 0 );
        if( songs > 0 ) {
            if( it->is_active() ) {
                amenu.addentry( ei_music, true, 'm', _( "Turn music off" ) );
            } else {
                amenu.addentry( ei_music, true, 'm', _( "Turn music on [%d]" ), songs );
            }
        } else {
            amenu.addentry( ei_music, false, 'm', _( "No music on device" ) );
        }

        if( !it->get_var( "EIPC_RECIPES" ).empty() ) {
            amenu.addentry( ei_recipe, true, 'r', _( "View recipes on E-ink screen" ) );
        }

        if( !it->get_var( "EIPC_EXTENDED_PHOTOS" ).empty() ) {
            amenu.addentry( ei_uploaded_photos, true, 'l', _( "Your photos" ) );
        }

        if( !it->get_var( "EINK_MONSTER_PHOTOS" ).empty() ) {
            amenu.addentry( ei_monsters, true, 'y', _( "Your collection of monsters" ) );
        } else {
            amenu.addentry( ei_monsters, false, 'y', _( "Collection of monsters is empty" ) );
        }

        amenu.addentry( ei_download, true, 'w', _( "Download data from memory card" ) );

        /** @EFFECT_COMPUTER >2 allows decrypting memory cards more easily */
        if( p->get_skill_level( skill_computer ) > 2 ) {
            amenu.addentry( ei_decrypt, true, 'd', _( "Decrypt memory card" ) );
        } else {
            amenu.addentry( ei_decrypt, false, 'd', _( "Decrypt memory card (low skill)" ) );
        }

        amenu.query();

        const int choice = amenu.ret;

        if( ei_photo == choice ) {

            const int photos = it->get_var( "EIPC_PHOTOS", 0 );
            const int viewed = std::min( photos, rng( 10, 30 ) );
            const int count = photos - viewed;
            if( count == 0 ) {
                it->erase_var( "EIPC_PHOTOS" );
            } else {
                it->set_var( "EIPC_PHOTOS", count );
            }

            p->moves -= to_moves<int>( rng( 3_seconds, 7_seconds ) );

            if( p->has_trait( trait_PSYCHOPATH ) ) {
                p->add_msg_if_player( m_info, _( "Wasted time, these pictures do not provoke your "
                                                 "senses." ) );
            } else {
                p->add_morale( MORALE_PHOTOS, rng( 15, 30 ), 100 );

                const int random_photo = rng( 1, 20 );
                switch( random_photo ) {
                    case 1:
                        p->add_msg_if_player( m_good, _( "You used to have a dog like this…" ) );
                        break;
                    case 2:
                        p->add_msg_if_player( m_good, _( "Ha-ha!  An amusing cat photo." ) );
                        break;
                    case 3:
                        p->add_msg_if_player( m_good, _( "Excellent pictures of nature." ) );
                        break;
                    case 4:
                        p->add_msg_if_player( m_good, _( "Food photos… your stomach rumbles!" ) );
                        break;
                    case 5:
                        p->add_msg_if_player( m_good, _( "Some very interesting travel photos." ) );
                        break;
                    case 6:
                        p->add_msg_if_player( m_good, _( "Pictures of a concert of popular band." ) );
                        break;
                    case 7:
                        p->add_msg_if_player( m_good, _( "Photos of someone's luxurious house." ) );
                        break;
                    default:
                        p->add_msg_if_player( m_good, _( "You feel nostalgic as you stare at the "
                                                         "photo." ) );
                        break;
                }
            }

            return it->type->charges_to_use();
        }

        if( ei_music == choice ) {

            p->moves -= 30;

            if( it->is_active() ) {
                // Turn music off - revert to original type
                const std::optional<itype_id> revert_to = it->type->tool->revert_to;
                if( revert_to.has_value() ) { it->convert( revert_to.value() ); }
                it->deactivate();
                it->erase_var( "EIPC_MUSIC_ON" );

                p->add_msg_if_player( m_info, _( "You turned off music on your %s." ), it->tname() );
            } else {
                // Turn music on - find music_player action's target if it exists
                itype_id music_type;
                const auto music_it = it->type->use_methods.find( "music_player" );
                if( music_it != it->type->use_methods.end() ) {
                    const iuse_music_player* music_actor = dynamic_cast<const iuse_music_player *>(
                            music_it->second.get_actor_ptr() );
                    if( music_actor ) { music_type = music_actor->target; }
                }

                // Transform to music variant if found
                if( !music_type.is_empty() && music_type.is_valid() ) { it->convert( music_type ); }

                it->activate();
                it->set_var( "EIPC_MUSIC_ON", "1" );

                p->add_msg_if_player( m_info, _( "You turned on music on your %s." ), it->tname() );
            }

            return it->type->charges_to_use();
        }

        if( ei_recipe == choice ) {
            p->moves -= 50;

            uilist rmenu;

            rmenu.text = _( "List recipes:" );

            std::vector<recipe_id> candidate_recipes;
            std::istringstream f( it->get_var( "EIPC_RECIPES" ) );
            std::string s;
            int k = 0;
            while( getline( f, s, ',' ) ) {

                if( s.empty() ) { continue; }

                candidate_recipes.emplace_back( s );

                const auto& recipe = *candidate_recipes.back();
                if( recipe ) {
                    rmenu.addentry( k++, true, -1, recipe.result_name( /*decorated=*/true ) );
                }
            }

            rmenu.query();

            return it->type->charges_to_use();
        }

        if( ei_uploaded_photos == choice ) {
            show_photo_selection( *p, *it, "EIPC_EXTENDED_PHOTOS" );
            return it->type->charges_to_use();
        }

        if( ei_monsters == choice ) {

            uilist pmenu;

            pmenu.text = _( "Your collection of monsters:" );

            std::vector<mtype_id> monster_photos;

            std::istringstream f( it->get_var( "EINK_MONSTER_PHOTOS" ) );
            std::string s;
            int k = 0;
            while( getline( f, s, ',' ) ) {
                if( s.empty() ) { continue; }
                monster_photos.emplace_back( s );
                std::string menu_str;
                const monster dummy( monster_photos.back() );
                menu_str = dummy.name();
                getline( f, s, ',' );
                char *chq = s.data();
                const int quality = atoi( chq );
                menu_str += " [" + photo_quality_name( quality ) + "]";
                pmenu.addentry( k++, true, -1, menu_str.c_str() );
            }

            int choice;
            do {
                pmenu.query();
                choice = pmenu.ret;

                if( choice < 0 ) { break; }

                const monster dummy( monster_photos[choice] );
                popup( dummy.type->get_description() );
            } while( true );
            return it->type->charges_to_use();
        }

        avatar* you = p->as_avatar();
        item* loc = nullptr;
        auto filter = []( const item & it ) { return it.has_flag( flag_MC_MOBILE ); };
        const std::string title = _( "Insert memory card" );

        if( ei_download == choice ) {

            p->moves -= to_moves<int>( 2_seconds );

            if( you != nullptr ) { loc = game_menus::inv::titled_filter_menu( filter, *you, title ); }
            if( !loc ) {
                p->add_msg_if_player( m_info, _( "You do not have that item!" ) );
                return it->type->charges_to_use();
            }
            item& mc = *loc;

            if( !mc.has_flag( flag_MC_MOBILE ) ) {
                p->add_msg_if_player( m_info, _( "This is not a compatible memory card." ) );
                return it->type->charges_to_use();
            }

            init_memory_card_with_random_stuff( mc );

            if( mc.has_flag( flag_MC_ENCRYPTED ) ) {
                p->add_msg_if_player( m_info, _( "This memory card is encrypted." ) );
                return it->type->charges_to_use();
            }
            if( !mc.has_flag( flag_MC_HAS_DATA ) ) {
                p->add_msg_if_player( m_info, _( "This memory card does not contain any new data." ) );
                return it->type->charges_to_use();
            }

            einkpc_download_memory_card( *p, *it, mc );

            return it->type->charges_to_use();
        }

        if( ei_decrypt == choice ) {
            p->moves -= to_moves<int>( 2_seconds );
            if( you != nullptr ) { loc = game_menus::inv::titled_filter_menu( filter, *you, title ); }
            if( !loc ) {
                p->add_msg_if_player( m_info, _( "You do not have that item!" ) );
                return it->type->charges_to_use();
            }
            item& mc = *loc;

            if( !mc.has_flag( flag_MC_MOBILE ) ) {
                p->add_msg_if_player( m_info, _( "This is not a compatible memory card." ) );
                return it->type->charges_to_use();
            }

            init_memory_card_with_random_stuff( mc );

            if( !mc.has_flag( flag_MC_ENCRYPTED ) ) {
                p->add_msg_if_player( m_info, _( "This memory card is not encrypted." ) );
                return it->type->charges_to_use();
            }

            p->practice( skill_computer, rng( 2, 5 ) );

            /** @EFFECT_INT increases chance of safely decrypting memory card */

            /** @EFFECT_COMPUTER increases chance of safely decrypting memory card */
            const int success =
                p->get_skill_level( skill_computer ) * rng( 1, p->get_skill_level( skill_computer ) )
                * rng( 1, p->int_cur )
                - rng( 30, 80 );
            if( success > 0 ) {
                p->practice( skill_computer, rng( 5, 10 ) );
                p->add_msg_if_player(
                    m_good, _( "You successfully decrypted content on %s!" ), mc.tname() );
                einkpc_download_memory_card( *p, *it, mc );
            } else {
                if( success > -10 || one_in( 5 ) ) {
                    p->add_msg_if_player( m_neutral, _( "You failed to decrypt the %s." ), mc.tname() );
                } else {
                    p->add_msg_if_player( m_bad, _( "You tripped the firmware protection, and the "
                                                    "card deleted its data!" ) );
                    mc.clear_vars();
                    mc.unset_flags();
                    mc.convert( itype_mobile_memory_card_used );
                }
            }
            return it->type->charges_to_use();
        }
    }
    return 0;
}


static std::string colorized_trap_name_at( const tripoint_bub_ms& point )
{
    const trap& trap = g->m.tr_at( point );
    std::string name;
    if( !trap.is_null() && trap.get_visibility() <= 1 ) {
        name = colorize( trap.name(), trap.color ) + _( " on " );
    }
    return name;
}

static std::string colorized_field_description_at( const tripoint_bub_ms& point )
{
    std::string field_text;
    const field& field = g->m.field_at( point );
    const field_entry* entry = field.find_field( field.displayed_field_type() );
    if( entry ) {
        field_text = string_format(
                         _( description_affixes.at( field.displayed_description_affix() ) ),
                         colorize( entry->name(), entry->color() ) );
    }
    return field_text;
}

static std::string colorized_item_name( const item& item )
{
    nc_color color = item.color_in_inventory();
    std::string damtext = item.damage() != 0 ? item.durability_indicator() : "";
    return damtext + colorize( item.tname( 1, false ), color );
}

static std::string colorized_item_description( const item& item )
{
    iteminfo_query query = iteminfo_query( std::vector<iteminfo_parts> {
        iteminfo_parts::DESCRIPTION, iteminfo_parts::DESCRIPTION_NOTES,
        iteminfo_parts::DESCRIPTION_CONTENTS
    } );
    return item.info_string( query, 1 );
}

static const item &get_top_item_at_point(
    const tripoint_bub_ms& point, const units::volume& min_visible_volume )
{
    map_stack items = g->m.i_at( point );
    // iterate from topmost item down to ground
    for( const item * const& it : items ) {
        if( it->volume() > min_visible_volume ) {
            // return top (or first big enough) item to the list
            return *it;
        }
    }
    return null_item_reference();
}

static std::string colorized_ter_name_flags_at(
    const tripoint_bub_ms& point, const std::vector<std::string> &flags,
    const std::vector<ter_str_id> &ter_whitelist )
{
    const ter_id ter = g->m.ter( point );
    std::string name = colorize( ter->name(), ter->color() );
    const std::string& graffiti_message = g->m.graffiti_at( point );

    if( !graffiti_message.empty() ) {
        name += string_format( _( " with graffiti \"%s\"" ), graffiti_message );
        return name;
    }
    if( ter_whitelist.empty() && flags.empty() ) { return name; }
    if( !ter->open.is_null()
        || ( ter->examine != iexamine::none && ter->examine != iexamine::fungus
             && ter->examine != iexamine::water_source && ter->examine != iexamine::dirtmound ) ) {
        return name;
    }
    for( const ter_str_id& ter_good : ter_whitelist ) {
        if( ter->id == ter_good ) { return name; }
    }
    for( const std::string& flag : flags ) {
        if( ter->has_flag( flag ) ) { return name; }
    }

    return std::string();
}

static std::string colorized_feature_description_at(
    const tripoint_bub_ms& center_point, bool& item_found,
    const units::volume& min_visible_volume )
{
    item_found = false;
    const furn_id furn = g->m.furn( center_point );
    if( furn != f_null && furn.is_valid() ) {
        std::string furn_str = colorize( furn->name(), c_yellow );
        std::string sign_message = g->m.get_signage( center_point );
        if( !sign_message.empty() ) {
            furn_str += string_format( _( " with message \"%s\"" ), sign_message );
        }
        if( !furn->has_flag( "CONTAINER" ) && !furn->has_flag( "SEALED" ) ) {
            const item& item = get_top_item_at_point( center_point, min_visible_volume );
            if( !item.is_null() ) {
                furn_str += string_format( _( " with %s on it" ), colorized_item_name( item ) );
                item_found = true;
            }
        }
        return furn_str;
    }
    return std::string();
}

static std::string format_object_pair(
    const std::pair<std::string, int> &pair, const std::string& article )
{
    if( pair.second == 1 ) {
        return article + pair.first;
    } else if( pair.second > 1 ) {
        return string_format( "%i %s", pair.second, pair.first );
    }
    return std::string();
}
static std::string format_object_pair_article( const std::pair<std::string, int> &pair )
{
    return format_object_pair(
               pair,
               pgettext( "Article 'a', replace it with empty "
                         "string if it is not used in language",
                         "a " ) );
}
static std::string format_object_pair_no_article( const std::pair<std::string, int> &pair )
{
    return format_object_pair( pair, "" );
}

static std::string effects_description_for_creature(
    Creature* const creature, std::string& pose, const std::string& pronoun_sex )
{
    struct ef_con { // effect constraint
        translation status;
        translation pose;
        int intensity_lower_limit;
        ef_con( const translation& status, const translation& pose, int intensity_lower_limit )
            : status( status ),
              pose( pose ),
              intensity_lower_limit( intensity_lower_limit ) {}
        ef_con( const translation& status, const translation& pose )
            : status( status ),
              pose( pose ),
              intensity_lower_limit( 0 ) {}
        ef_con( const translation& status, int intensity_lower_limit )
            : status( status ),
              intensity_lower_limit( intensity_lower_limit ) {}
        ef_con( const translation& status ): status( status ), intensity_lower_limit( 0 ) {}
    };
    static const std::unordered_map<efftype_id, ef_con> vec_effect_status = {
        {effect_onfire, ef_con( to_translation( " is on <color_red>fire</color>. " ) )},
        {effect_bleed, ef_con( to_translation( " is <color_red>bleeding</color>. " ), 1 )},
        {effect_happy, ef_con( to_translation( " looks <color_green>happy</color>. " ), 13 )},
        {effect_downed, ef_con( translation(), to_translation( "downed" ) )},
        {effect_in_pit, ef_con( translation(), to_translation( "stuck" ) )},
        {effect_stunned, ef_con( to_translation( " is <color_blue>stunned</color>. " ) )},
        {effect_dazed, ef_con( to_translation( " is <color_blue>dazed</color>. " ) )},
        {effect_beartrap, ef_con( to_translation( " is stuck in beartrap. " ) )},
        {
            effect_laserlocked, ef_con( to_translation( " have tiny <color_red>red dot</color> on "
                                                        "body. " ) )
        },
        {effect_boomered, ef_con( to_translation( " is covered in <color_magenta>bile</color>. " ) )},
        {
            effect_glowing, ef_con( to_translation( " is covered in <color_yellow>glowing "
                                                    "goo</color>. " ) )
        },
        {effect_slimed, ef_con( to_translation( " is covered in <color_green>thick goo</color>. " ) )},
        {
            effect_corroding, ef_con( to_translation( " is covered in "
                                                      "<color_light_green>acid</color>. " ) )
        },
        {effect_sap, ef_con( to_translation( " is coated in <color_brown>sap</color>. " ) )},
        {effect_webbed, ef_con( to_translation( " is covered in <color_dark_gray>webs</color>. " ) )},
        {effect_spores, ef_con( to_translation( " is covered in <color_green>spores</color>. " ), 1 )},
        {
            effect_crushed,
            ef_con( to_translation( " lies under <color_dark_gray>collapsed debris</color>. " ),
                    to_translation( "lies" ) )
        },
        {effect_lack_sleep, ef_con( to_translation( " looks <color_dark_gray>very tired</color>. " ) )},
        {
            effect_lying_down,
            ef_con( to_translation( " is <color_dark_blue>sleeping</color>. " ), to_translation( "lies" ) )
        },
        {
            effect_sleep,
            ef_con( to_translation( " is <color_dark_blue>sleeping</color>. " ), to_translation( "lies" ) )
        },
        {effect_haslight, ef_con( to_translation( " is <color_yellow>lit</color>. " ) )},
        {effect_saddled, ef_con( to_translation( " is <color_dark_gray>saddled</color>. " ) )},
        {
            effect_harnessed, ef_con( to_translation( " is being <color_dark_gray>harnessed</color> by "
                                                      "a vehicle. " ) )
        },
        {
            effect_monster_armor, ef_con( to_translation( " is <color_dark_gray>wearing "
                                                          "armor</color>. " ) )
        },
        {effect_has_bag, ef_con( to_translation( " have <color_dark_gray>bag</color> attached. " ) )},
        {effect_tied, ef_con( to_translation( " is <color_dark_gray>tied</color>. " ) )},
        {effect_bouldering, ef_con( translation(), to_translation( "balancing" ) )}
    };

    std::string figure_effects;
    if( creature ) {
        for( const auto& pair : vec_effect_status ) {
            if( creature->get_effect_int( pair.first ) > pair.second.intensity_lower_limit ) {
                if( !pair.second.status.empty() ) {
                    figure_effects += pronoun_sex + pair.second.status;
                }
                if( !pair.second.pose.empty() ) { pose = pair.second.pose.translated(); }
            }
        }
        if( creature->has_effect( effect_sad ) ) {
            int intensity = creature->get_effect_int( effect_sad );
            if( intensity > 500 && intensity <= 950 ) {
                figure_effects +=
                    pronoun_sex + pgettext( "Someone", " looks <color_blue>sad</color>. " );
            } else if( intensity > 950 ) {
                figure_effects +=
                    pronoun_sex + pgettext( "Someone", " looks <color_blue>depressed</color>. " );
            }
        }
        float pain = creature->get_pain() / 10.f;
        if( pain > 3 ) {
            figure_effects +=
                pronoun_sex + pgettext( "Someone", " is writhing in <color_red>pain</color>. " );
        }
        if( creature->has_effect( effect_riding ) ) {
            pose = _( "rides" );
            monster* const mon = g->critter_at<monster>( creature->bub_pos(), false );
            figure_effects +=
                pronoun_sex
                + string_format( _( " is riding %s. " ), colorize( mon->name(), c_light_blue ) );
        }
        if( creature->has_effect( effect_glowy_led ) ) {
            figure_effects += _( "A bionic LED is <color_yellow>glowing</color> softly. " );
        }
    }
    if( !figure_effects.empty() && figure_effects.back() == ' ' ) { // remove last space
        figure_effects.erase( figure_effects.end() - 1 );
    }
    return figure_effects;
}

struct object_names_collection {
    std::unordered_map<std::string, int> furniture, vehicles, items, terrain;

    std::string figure_text;
    std::string obj_nearby_text;
};

static object_names_collection enumerate_objects_around_point(
    const tripoint_bub_ms& point, const int radius, const tripoint_bub_ms& bounds_center_point,
    const int bounds_radius, const tripoint_bub_ms& camera_pos,
    const units::volume& min_visible_volume, bool create_figure_desc,
    std::unordered_set<tripoint_bub_ms> &ignored_points,
    std::unordered_set<const vehicle *> &vehicles_recorded )
{
    map& here = get_map();
    const tripoint_range<tripoint_bub_ms> bounds =
        here.points_in_radius( bounds_center_point, bounds_radius );
    const tripoint_range<tripoint_bub_ms> points_in_radius = here.points_in_radius( point, radius );
    int dist = rl_dist( camera_pos, point );

    bool item_found = false;
    std::unordered_set<const vehicle *> local_vehicles_recorded( vehicles_recorded );
    object_names_collection ret_obj;

    std::string description_part_on_figure;
    std::string description_furniture_on_figure;
    std::string description_terrain_on_figure;

    // store objects in radius
    for( const tripoint_bub_ms& point_around_figure : points_in_radius ) {
        if( !bounds.is_point_inside( point_around_figure )
            || !g->m.sees( camera_pos, point_around_figure, dist + radius )
            || ( ignored_points.contains( point_around_figure )
                 && !( point_around_figure == point && create_figure_desc ) ) ) {
            continue; // disallow photos with not visible objects
        }
        units::volume volume_to_search =
            point_around_figure == bounds_center_point ? 0_ml : min_visible_volume;

        std::string furn_desc =
            colorized_feature_description_at( point_around_figure, item_found, volume_to_search );

        const item& item = get_top_item_at_point( point_around_figure, volume_to_search );

        const optional_vpart_position veh_part_pos = g->m.veh_at( point_around_figure );
        std::string unusual_ter_desc = colorized_ter_name_flags_at(
                                           point_around_figure, camera_ter_whitelist_flags, camera_ter_whitelist_types );
        std::string ter_desc = colorized_ter_name_flags_at( point_around_figure );

        const std::string trap_name = colorized_trap_name_at( point_around_figure );
        const std::string field_desc = colorized_field_description_at( point_around_figure );

        if( !furn_desc.empty() ) {
            furn_desc = trap_name + furn_desc + field_desc;
            if( point == point_around_figure && create_figure_desc ) {
                description_furniture_on_figure = furn_desc;
            } else {
                ret_obj.furniture[furn_desc]++;
            }
        } else if( veh_part_pos.has_value() ) {
            const vehicle& veh = veh_part_pos->vehicle();
            const std::string veh_name = colorize( veh.disp_name(), c_light_blue );
            const vehicle* veh_hash = &veh_part_pos->vehicle();

            if( !local_vehicles_recorded.contains( veh_hash ) && point != point_around_figure ) {
                // new vehicle, point is not center
                ret_obj.vehicles[veh_name]++;
            } else if( point == point_around_figure ) {
                // point is center
                //~ %1$s: vehicle part name, %2$s: vehicle name
                description_part_on_figure = string_format(
                                                 pgettext( "vehicle part", "%1$s from %2$s" ),
                                                 veh_part_pos.part_displayed()->part().name(), veh_name );
                if( ret_obj.vehicles.contains( veh_name )
                    && local_vehicles_recorded.contains( veh_hash ) ) {
                    // remove vehicle name only if we previously added THIS vehicle name (in case of
                    // same name)
                    ret_obj.vehicles[veh_name]--;
                    if( ret_obj.vehicles[veh_name] <= 0 ) { ret_obj.vehicles.erase( veh_name ); }
                }
            }
            vehicles_recorded.insert( veh_hash );
            local_vehicles_recorded.insert( veh_hash );
        } else if( !item.is_null() ) {
            std::string item_name = colorized_item_name( item );
            item_name = trap_name + item_name + field_desc;
            if( point == point_around_figure && create_figure_desc ) {
                //~ %1$s: terrain description, %2$s: item name
                description_terrain_on_figure = string_format(
                                                    pgettext( "terrain and item", "%1$s with a %2$s" ), ter_desc, item_name );
            } else {
                ret_obj.items[item_name]++;
            }
        } else if( !unusual_ter_desc.empty() ) {
            unusual_ter_desc = trap_name + unusual_ter_desc + field_desc;
            if( point == point_around_figure && create_figure_desc ) {
                description_furniture_on_figure = unusual_ter_desc;
            } else {
                ret_obj.furniture[unusual_ter_desc]++;
            }
        } else if( !ter_desc.empty() && ( !field_desc.empty() || !trap_name.empty() ) ) {
            ter_desc = trap_name + ter_desc + field_desc;
            if( point == point_around_figure && create_figure_desc ) {
                description_terrain_on_figure = ter_desc;
            } else {
                ret_obj.terrain[ter_desc]++;
            }
        } else {
            ter_desc = trap_name + ter_desc + field_desc;
            if( point == point_around_figure && create_figure_desc ) {
                description_terrain_on_figure = ter_desc;
            }
        }
        ignored_points.insert( point_around_figure );
    }

    if( create_figure_desc ) {
        std::vector<std::string> objects_combined_desc;
        int objects_combined_num = 0;
        std::unordered_map<std::string, int> vecs_to_retrieve[4] =
        {ret_obj.furniture, ret_obj.vehicles, ret_obj.items, ret_obj.terrain};

        for( int i = 0; i < 4; i++ ) {
            for( const auto& p : vecs_to_retrieve[i] ) {
                objects_combined_desc.push_back(
                    i == 1 ? // vehicle name already includes "the"
                    format_object_pair_no_article( p )
                    : format_object_pair_article( p ) );
                objects_combined_num += p.second;
            }
        }

        const char *transl_str = pgettext( "someone stands/sits *on* something", " on %s." );
        if( !description_part_on_figure.empty() ) {
            ret_obj.figure_text = string_format( transl_str, description_part_on_figure );
        } else {
            if( !description_furniture_on_figure.empty() ) {
                ret_obj.figure_text = string_format( transl_str, description_furniture_on_figure );
            } else {
                ret_obj.figure_text = string_format( transl_str, description_terrain_on_figure );
            }
        }
        if( !objects_combined_desc.empty() ) {
            // store objects to description_figures_status
            std::string objects_text = enumerate_as_string( objects_combined_desc );
            ret_obj.obj_nearby_text = string_format(
                                          vgettext( "Nearby is %s.", "Nearby are %s.", objects_combined_num ), objects_text );
        }
    }
    return ret_obj;
}

static extended_photo_def photo_def_for_camera_point(
    const tripoint_bub_ms& aim_point, const tripoint_bub_ms& camera_pos,
    std::vector<monster *> &monster_vec, std::vector<Character *> &character_vec )
{
    // look for big items on top of stacks in the background for the selfie description
    const units::volume min_visible_volume = 490_ml;

    std::unordered_set<tripoint_bub_ms> ignored_points;
    std::unordered_set<const vehicle *> vehicles_recorded;

    std::unordered_map<std::string, std::string> description_figures_appearance;
    std::vector<std::pair<std::string, std::string>> description_figures_status;

    std::string timestamp = to_string( time_point( calendar::turn ) );
    int dist = rl_dist( camera_pos, aim_point );
    map& here = get_map();
    const tripoint_range<tripoint_bub_ms> bounds = here.points_in_radius( aim_point, 2 );
    extended_photo_def photo;
    bool need_store_weather = false;
    int outside_tiles_num = 0;
    int total_tiles_num = 0;

    const auto map_deincrement_or_erase =
    []( std::unordered_map<std::string, int> &obj_map, const std::string & key ) {
        if( obj_map.contains( key ) ) {
            obj_map[key]--;
            if( obj_map[key] <= 0 ) { obj_map.erase( key ); }
        }
    };

    // first scan for critters and mark nearby furniture, vehicles and items
    for( const tripoint_bub_ms& current : bounds ) {
        if( !g->m.sees( camera_pos, current, dist + 3 ) ) {
            continue; // disallow photos with non-visible objects
        }
        monster* const mon = g->critter_at<monster>( current, false );
        Character* guy = g->critter_at<Character>( current );

        total_tiles_num++;
        if( g->m.is_outside( current ) ) {
            need_store_weather = true;
            outside_tiles_num++;
        }

        if( guy || mon ) {
            std::string figure_appearance, figure_name, pose, pronoun_sex, figure_effects;
            Creature* creature;
            if( mon && mon->has_effect( effect_ridden ) ) {
                // only player can ride, see monexamine::mount_pet
                guy = &g->u;
                description_figures_appearance[mon->name()] =
                    "\"" + mon->type->get_description() + "\"";
            }

            if( guy ) {
                if( guy->is_hallucination() ) {
                    continue; // do not include hallucinations
                }
                if( guy->is_crouching() ) {
                    pose = _( "is sitting" );
                } else {
                    pose = _( "is standing" );
                }
                const std::vector<std::string> vec = describe_character( guy );
                figure_appearance = join( vec, "\n\n" );
                figure_name = guy->name;
                pronoun_sex = guy->male ? _( "He" ) : _( "She" );
                creature = guy;
                character_vec.push_back( guy );
            } else {
                if( mon->is_hallucination() || mon->type->in_species( HALLUCINATION ) ) {
                    continue; // do not include hallucinations
                }
                pose = _( "is standing" );
                figure_appearance = "\"" + mon->type->get_description() + "\"";
                figure_name = mon->name();
                pronoun_sex = pgettext( "Pronoun", "It" );
                creature = mon;
                monster_vec.push_back( mon );
            }

            figure_effects = effects_description_for_creature( creature, pose, pronoun_sex );
            description_figures_appearance[figure_name] = figure_appearance;

            object_names_collection obj_collection = enumerate_objects_around_point(
                    current, 1, aim_point, 2, camera_pos, min_visible_volume, true, ignored_points,
                    vehicles_recorded );
            std::string figure_text = pose + obj_collection.figure_text;

            if( !figure_effects.empty() ) { figure_text += " " + figure_effects; }
            if( !obj_collection.obj_nearby_text.empty() ) {
                figure_text += " " + obj_collection.obj_nearby_text;
            }
            auto name_text_pair = std::pair<std::string, std::string>( figure_name, figure_text );
            if( current == aim_point ) {
                description_figures_status
                .insert( description_figures_status.begin(), name_text_pair );
            } else {
                description_figures_status.push_back( name_text_pair );
            }
        }
    }

    // scan for everythin NOT near critters
    object_names_collection obj_coll = enumerate_objects_around_point(
                                           aim_point, 2, aim_point, 2, camera_pos, min_visible_volume, false, ignored_points,
                                           vehicles_recorded );

    std::string photo_text = _( "This is a photo of " );

    bool found_item_aim_point;
    std::string furn_desc = colorized_feature_description_at( aim_point, found_item_aim_point, 0_ml );
    const item& item = get_top_item_at_point( aim_point, 0_ml );
    const std::string trap_name = colorized_trap_name_at( aim_point );
    std::string ter_name = colorized_ter_name_flags_at( aim_point, {}, {} );
    const std::string field_desc = colorized_field_description_at( aim_point );

    bool found_vehicle_aim_point = g->m.veh_at( aim_point ).has_value(),
         found_furniture_aim_point = !furn_desc.empty();
    // colorized_feature_description_at do not update flag if no furniture found, so need to check
    // again
    if( !found_furniture_aim_point ) { found_item_aim_point = !item.is_null(); }

    const ter_id ter_aim = g->m.ter( aim_point );
    const furn_id furn_aim = g->m.furn( aim_point );

    if( !description_figures_status.empty() ) {
        std::string names = enumerate_as_string(
                                description_figures_status.begin(), description_figures_status.end(),
        []( const std::pair<std::string, std::string> &it ) {
            return colorize( it.first, c_light_blue );
        } );

        photo.name = names;
        photo_text += names + ".";

        for( const auto& figure_status : description_figures_status ) {
            photo_text +=
                "\n\n" + colorize( figure_status.first, c_light_blue ) + " " + figure_status.second;
        }
    } else if( found_vehicle_aim_point ) {
        const optional_vpart_position veh_part_pos = g->m.veh_at( aim_point );
        const std::string veh_name = colorize( veh_part_pos->vehicle().disp_name(), c_light_blue );
        photo.name = veh_name;
        photo_text += veh_name + ".";
        map_deincrement_or_erase( obj_coll.vehicles, veh_name );
    } else if( found_furniture_aim_point || found_item_aim_point ) {
        std::string item_name = colorized_item_name( item );
        if( found_furniture_aim_point ) {
            furn_desc = trap_name + furn_desc + field_desc;
            photo.name = furn_desc;
            photo_text += photo.name + ".";
            map_deincrement_or_erase( obj_coll.furniture, furn_desc );
        } else if( found_item_aim_point ) {
            item_name = trap_name + item_name + field_desc;
            photo.name = item_name;
            photo_text += item_name + ". " + string_format( _( "It lies on the %s." ), ter_name );
            map_deincrement_or_erase( obj_coll.items, item_name );
        }
        if( found_furniture_aim_point && !furn_aim->description.empty() ) {
            photo_text +=
                "\n\n" + colorize( furn_aim->name(), c_yellow ) + ":\n" + furn_aim->description;
        }
        if( found_item_aim_point ) {
            photo_text += "\n\n" + item_name + ":\n" + colorized_item_description( item );
        }
    } else {
        ter_name = trap_name + ter_name + field_desc;
        photo.name = ter_name;
        photo_text += photo.name + ".";
        map_deincrement_or_erase( obj_coll.terrain, ter_name );
        map_deincrement_or_erase( obj_coll.furniture, ter_name );

        if( !ter_aim->description.empty() ) {
            photo_text += "\n\n" + photo.name + ":\n" + ter_aim->description;
        }
    }

    auto num_of = []( const std::unordered_map<std::string, int> &m ) -> int {
        int ret = 0;
        for( const auto& it : m ) { ret += it.second; }
        return ret;
    };

    if( !obj_coll.items.empty() ) {
        std::string obj_list = enumerate_as_string(
                                   obj_coll.items.begin(), obj_coll.items.end(), format_object_pair_article );
        photo_text +=
            "\n\n"
            + string_format(
                vgettext( "There is something lying on the ground: %s.",
                          "There are some things lying on the ground: %s.", num_of( obj_coll.items ) ),
                obj_list );
    }
    if( !obj_coll.furniture.empty() ) {
        std::string obj_list = enumerate_as_string(
                                   obj_coll.furniture.begin(), obj_coll.furniture.end(), format_object_pair_article );
        photo_text +=
            "\n\n"
            + string_format(
                vgettext( "Something is visible in the background: %s.",
                          "Some objects are visible in the background: %s.",
                          num_of( obj_coll.furniture ) ),
                obj_list );
    }
    if( !obj_coll.vehicles.empty() ) {
        std::string obj_list = enumerate_as_string(
                                   obj_coll.vehicles.begin(), obj_coll.vehicles.end(), format_object_pair_no_article );
        photo_text +=
            "\n\n"
            + string_format(
                vgettext( "There is %s parked in the background.",
                          "There are %s parked in the background.", num_of( obj_coll.vehicles ) ),
                obj_list );
    }
    if( !obj_coll.terrain.empty() ) {
        std::string obj_list = enumerate_as_string(
                                   obj_coll.terrain.begin(), obj_coll.terrain.end(), format_object_pair_no_article );
        photo_text +=
            "\n\n"
            + string_format(
                vgettext( "There is %s in the background.", "There are %s in the background.",
                          num_of( obj_coll.terrain ) ),
                obj_list );
    }

    // TODO: fix point types
    const oter_id& cur_ter =
        get_overmapbuffer( get_map().get_bound_dimension() )
        .ter( tripoint_abs_omt( project_to<coords::omt>( g->m.bub_to_abs( aim_point ) ) ) );
    std::string overmap_desc = string_format(
                                   _( "In the background you can see a %s" ),
                                   colorize( cur_ter->get_name(), cur_ter->get_color() ) );
    if( outside_tiles_num == total_tiles_num ) {
        photo_text += _( "\n\nThis photo was taken <color_dark_gray>outside</color>." );
    } else if( outside_tiles_num == 0 ) {
        photo_text += _( "\n\nThis photo was taken <color_dark_gray>inside</color>." );
        overmap_desc += _( " interior" );
    } else if( outside_tiles_num < total_tiles_num / 2.0 ) {
        photo_text += _(
                          "\n\nThis photo was taken mostly <color_dark_gray>inside</color>,"
                          " but <color_dark_gray>outside</color> can be seen." );
        overmap_desc += _( " interior" );
    } else if( outside_tiles_num >= total_tiles_num / 2.0 ) {
        photo_text += _(
                          "\n\nThis photo was taken mostly <color_dark_gray>outside</color>,"
                          " but <color_dark_gray>inside</color> can be seen." );
    }
    photo_text += "\n" + overmap_desc + ".";

    if( g->get_levz() >= 0 && need_store_weather ) {
        photo_text += "\n\n";

        int hour = hour_of_day<int>( calendar::turn );
        std::string time_string;

        if( is_dawn( calendar::turn ) ) {
            time_string = _( "<color_yellow>sunrise</color>" );
        } else if( is_dusk( calendar::turn ) ) {
            time_string = _( "<color_magenta>sunset</color>" );
        } else if( is_night( calendar::turn ) ) {
            if( hour == 0 ) {
                time_string = _( "<color_dark_gray>midnight</color>" );
            } else {
                time_string = _( "<color_gray>night</color>" );
            }
        } else {
            if( hour < 12 ) {
                time_string = _( "<color_cyan>morning</color>" );
            } else if( hour > 12 ) {
                time_string = _( "<color_light_red>afternoon</color>" );
            } else {
                time_string = _( "<color_light_blue>midday</color>" );
            }
        }

        photo_text += string_format( _( "It is %s. " ), time_string );
        photo_text += string_format(
                          _( "The weather is %s." ),
                          colorize( get_weather().weather_id->name, get_weather().weather_id->color ) );
    }

    for( const auto& figure : description_figures_appearance ) {
        photo_text +=
            "\n\n" + string_format( _( "%s's appearance:" ), colorize( figure.first, c_light_blue ) )
            + "\n" + figure.second;
    }

    photo_text +=
        "\n\n"
        + string_format(
            pgettext( "Date", "The photo was taken on %s." ), colorize( timestamp, c_light_blue ) );

    photo.description = photo_text;

    return photo;
}

static std::vector<std::string> describe_character( Character* guy )
{
    std::vector<std::string> result;
    std::string pronoun = guy->male ? _( "He" ) : _( "She" );

    std::vector<std::string> apperance_desc = guy->get_apperance_description();
    if( !apperance_desc.empty() ) {
        result.push_back( pronoun + _( " has " ) + enumerate_as_string( apperance_desc ) + "." );
    }

    if( guy->is_armed() ) {
        result.push_back( pronoun + _( " is wielding a " ) + guy->primary_weapon().tname() + "." );
    }

    const std::string worn_str =
    enumerate_as_string( guy->worn.begin(), guy->worn.end(), []( const item * const & it ) {
        return it->tname();
    } );
    if( !worn_str.empty() ) {
        result.push_back( pronoun + " " + _( "is wearing: " ) + worn_str + "." );
    } else {
        result.push_back( pronoun + " " + _( "is not wearing anything." ) );
    }
    const int visibility_cap = 0;
    const auto trait_str = guy->visible_mutations( visibility_cap );
    if( !trait_str.empty() ) { result.push_back( _( "Traits: " ) + trait_str ); }
    return result;
}

static void item_save_monsters(
    player& p, item& it, const std::vector<monster *> &monster_vec, const int photo_quality )
{
    std::string monster_photos = it.get_var( "CAMERA_MONSTER_PHOTOS" );
    if( monster_photos.empty() ) { monster_photos = ","; }

    for( monster * const& monster_p : monster_vec ) {
        const std::string mtype = monster_p->type->id.str();
        const std::string name = monster_p->name();

        // position of <monster type string>
        const size_t mon_str_pos = monster_photos.find( "," + mtype + "," );

        // monster gets recorded by the character, add to known types
        p.set_knows_creature_type( monster_p->type->id );

        if( mon_str_pos == std::string::npos ) { // new monster
            monster_photos += string_format( "%s,%d,", mtype, photo_quality );
        } else { // replace quality character, if new photo is better
            const size_t quality_num_pos = mon_str_pos + mtype.size() + 2;
            char *quality_char = &monster_photos[quality_num_pos];
            const int old_quality = atoi( quality_char ); // get qual number from char

            if( photo_quality > old_quality ) {
                monster_photos[quality_num_pos] = string_format( "%d", photo_quality )[0];
            }
            if( !p.is_blind() ) {
                if( photo_quality > old_quality ) {
                    p.add_msg_if_player(
                        m_good, _( "The quality of %s image is better than the previous one." ),
                        colorize( name, c_light_blue ) );
                } else if( old_quality == 5 ) {
                    p.add_msg_if_player(
                        _( "The quality of stored %s image is already maximally detailed." ),
                        colorize( name, c_light_blue ) );
                } else {
                    p.add_msg_if_player(
                        m_bad, _( "But the quality of %s image is worse than the previous one." ),
                        colorize( name, c_light_blue ) );
                }
            }
        }
    }
    it.set_var( "CAMERA_MONSTER_PHOTOS", monster_photos );
}

// throws exception
static bool item_read_extended_photos(
    item& it, std::vector<extended_photo_def> &extended_photos, const std::string& var_name,
    bool insert_at_begin )
{
    bool result = false;
    std::istringstream extended_photos_data( it.get_var( var_name ) );
    JsonIn json( extended_photos_data );
    if( insert_at_begin ) {
        std::vector<extended_photo_def> temp_vec;
        result = json.read( temp_vec );
        extended_photos
        .insert( std::begin( extended_photos ), std::begin( temp_vec ), std::end( temp_vec ) );
    } else {
        result = json.read( extended_photos );
    }
    return result;
}

// throws exception
static void item_write_extended_photos(
    item& it, const std::vector<extended_photo_def> &extended_photos, const std::string& var_name )
{
    std::ostringstream extended_photos_data;
    JsonOut json( extended_photos_data );
    json.write( extended_photos );
    it.set_var( var_name, extended_photos_data.str() );
}

static bool show_photo_selection( player& p, item& it, const std::string& var_name )
{
    if( p.is_blind() ) {
        p.add_msg_if_player( _( "You can't see the camera screen, you're blind." ) );
        return false;
    }

    uilist pmenu;
    pmenu.text = _( "Photos saved on camera:" );

    std::vector<std::string> descriptions;
    std::vector<extended_photo_def> extended_photos;

    try {
        item_read_extended_photos( it, extended_photos, var_name );
    } catch( const JsonError& e ) { debugmsg( "Error reading photos: %s", e.c_str() ); }
    try { // if there is old photos format, append them; delete old and save new
        if( item_read_extended_photos( it, extended_photos, "CAMERA_NPC_PHOTOS", true ) ) {
            it.erase_var( "CAMERA_NPC_PHOTOS" );
            item_write_extended_photos( it, extended_photos, var_name );
        }
    } catch( const JsonError& e ) { debugmsg( "Error migrating old photo format: %s", e.c_str() ); }

    int k = 0;
    for( const extended_photo_def& extended_photo : extended_photos ) {
        std::string menu_str = extended_photo.name;

        size_t index = menu_str.find( p.name );
        if( index != std::string::npos ) { menu_str.replace( index, p.name.length(), _( "You" ) ); }

        descriptions.push_back( extended_photo.description );
        menu_str += " [" + photo_quality_name( extended_photo.quality ) + "]";

        pmenu.addentry( k++, true, -1, menu_str.c_str() );
    }

    int choice;
    do {
        pmenu.query();
        choice = pmenu.ret;

        if( choice < 0 ) { break; }
        auto desc = descriptions[choice];

        // calc window size
        // more or less the same logic as popups
        const auto new_win = [&desc]() {
            auto folded_msg = foldstring( desc, FULL_SCREEN_WIDTH - 1 * 2 );
            int msg_width = 0;
            int msg_height = folded_msg.size();

            for( const auto& line : folded_msg ) {
                msg_width = std::max( msg_width, utf8_width( line, true ) );
            }

            const int win_width = std::min( TERMX, msg_width + 1 * 2 );
            const int win_height = std::min( TERMY, msg_height + 1 * 2 );
            const int win_x = ( TERMX - win_width ) / 2;
            const int win_y = ( TERMY - win_height ) / 2;


            return catacurses::newwin( win_height, win_width, point( win_x, win_y ) );
        };

        scrollable_text( new_win, "", desc.c_str() );

    } while( true );
    return true;
}

int iuse::camera( player* p, item* it, bool, const tripoint_bub_ms & )
{
    enum { c_shot, c_photos, c_monsters, c_upload };

    // CAMERA_NPC_PHOTOS is old save variable
    bool found_extended_photos =
        !it->get_var( "CAMERA_NPC_PHOTOS" ).empty() || !it->get_var( "CAMERA_EXTENDED_PHOTOS" ).empty();
    bool found_monster_photos = !it->get_var( "CAMERA_MONSTER_PHOTOS" ).empty();

    uilist amenu;
    amenu.text = _( "What to do with camera?" );
    amenu.addentry( c_shot, true, 't', _( "Take a photo" ) );
    if( !found_extended_photos && !found_monster_photos ) {
        amenu.addentry( c_photos, false, 'l', _( "No photos in memory" ) );
    } else {
        if( found_extended_photos ) { amenu.addentry( c_photos, true, 'l', _( "List photos" ) ); }
        if( found_monster_photos ) {
            amenu.addentry( c_monsters, true, 'm', _( "Your collection of monsters" ) );
        }
        amenu.addentry( c_upload, true, 'u', _( "Upload photos to memory card" ) );
    }

    amenu.query();
    const int choice = amenu.ret;

    if( choice < 0 ) { return 0; }

    if( c_shot == choice ) {
        const std::optional<tripoint_bub_ms> aim_point_ = g->look_around();

        if( !aim_point_ ) {
            p->add_msg_if_player( _( "Never mind." ) );
            return 0;
        }
        auto aim_point = *aim_point_;
        bool incorrect_focus = false;
        tripoint_range<tripoint_bub_ms> aim_bounds = g->m.points_in_radius( aim_point, 2 );

        std::vector<tripoint_bub_ms> trajectory = line_to( p->bub_pos(), aim_point, 0, 0 );
        trajectory.push_back( aim_point );

        p->moves -= 50;
        sound_event se;
        se.origin = p->bub_pos();
        se.volume = 50;
        se.category = sounds::sound_t::activity;
        se.description = _( "Click." );
        se.id = "tool";
        se.variant = "camera_shutter";
        sounds::sound( se );

        for( std::vector<tripoint_bub_ms>::iterator point_it = trajectory.begin();
             point_it != trajectory.end(); ++point_it ) {
            const auto trajectory_point = *point_it;
            if( point_it != trajectory.end() ) {
                const auto next_point = *( point_it + 1 ); // Trajectory ends on last visible tile
                if( !g->m.sees( p->bub_pos(), next_point, rl_dist( p->bub_pos(), next_point ) + 3 ) ) {
                    p->add_msg_if_player( _( "You have the wrong camera focus." ) );
                    incorrect_focus = true;
                    // recalculate target point
                    aim_point = trajectory_point;
                    aim_bounds = g->m.points_in_radius( trajectory_point, 2 );
                }
            }

            monster* const mon = g->critter_at<monster>( trajectory_point, true );
            player* const guy = g->critter_at<player>( trajectory_point );
            if( mon || guy || trajectory_point == aim_point ) {
                int dist = rl_dist( p->bub_pos(), trajectory_point );

                int camera_bonus = it->has_flag( flag_CAMERA_PRO ) ? 10 : 0;
                int photo_quality =
                    20 - rng( dist, dist * 2 ) * 2 + rng( camera_bonus / 2, camera_bonus );
                if( photo_quality > 5 ) { photo_quality = 5; }
                if( photo_quality < 0 ) { photo_quality = 0; }
                if( p->is_blind() ) { photo_quality /= 2; }

                if( mon ) {
                    monster& z = *mon;

                    // shoot past small monsters and hallucinations
                    if( trajectory_point != aim_point
                        && ( z.type->size <= creature_size::small || z.is_hallucination()
                             || z.type->in_species( HALLUCINATION ) ) ) {
                        continue;
                    }
                    if( !aim_bounds.is_point_inside( trajectory_point ) ) {
                        // take a photo of the monster that's in the way
                        p->add_msg_if_player(
                            m_warning, _( "A %s got in the way of your photo." ), z.name() );
                        incorrect_focus = true;
                    } else if( trajectory_point != aim_point ) { // shoot past mon that will be in
                        // photo anyway
                        continue;
                    }
                    // get an special message if the target is a hallucination
                    if( trajectory_point == aim_point
                        && ( z.is_hallucination() || z.type->in_species( HALLUCINATION ) ) ) {
                        p->add_msg_if_player( _( "Strange… there's nothing in the center of "
                                                 "picture?" ) );
                    }
                } else if( guy ) {
                    if( trajectory_point == aim_point && guy->is_hallucination() ) {
                        p->add_msg_if_player(
                            _( "Strange… %s's not visible on the picture?" ), guy->name );
                    } else if( !aim_bounds.is_point_inside( trajectory_point ) ) {
                        // take a photo of the monster that's in the way
                        p->add_msg_if_player(
                            m_warning, _( "%s got in the way of your photo." ), guy->name );
                        incorrect_focus = true;
                    } else if( trajectory_point != aim_point ) { // shoot past guy that will be in
                        // photo anyway
                        continue;
                    }
                }
                if( incorrect_focus ) { photo_quality = photo_quality == 0 ? 0 : photo_quality - 1; }

                std::vector<extended_photo_def> extended_photos;
                std::vector<monster *> monster_vec;
                std::vector<Character *> character_vec;
                extended_photo_def photo = photo_def_for_camera_point(
                                               trajectory_point, p->bub_pos(), monster_vec, character_vec );
                photo.quality = photo_quality;

                try {
                    item_read_extended_photos( *it, extended_photos, "CAMERA_EXTENDED_PHOTOS" );
                    extended_photos.push_back( photo );
                    item_write_extended_photos( *it, extended_photos, "CAMERA_EXTENDED_PHOTOS" );
                } catch( const JsonError& e ) {
                    debugmsg( "Error when adding new photo (loaded photos = %i): %s",
                              extended_photos.size(), e.c_str() );
                }

                const bool selfie = std::ranges::contains( character_vec, p );

                if( selfie ) {
                    auto name = photo.name;

                    if( name == colorize( p->name, c_light_blue ) ) {
                        p->add_msg_if_player( _( "You took a selfie." ) );
                    } else {
                        size_t index = name.find( p->name );
                        if( index != std::string::npos ) {
                            name.replace( index, p->name.length(), _( "Yourself" ) );
                        }
                        p->add_msg_if_player( _( "You took a selfie with %1$s." ), name );
                    }
                } else {
                    if( p->is_blind() ) {
                        p->add_msg_if_player( _( "You took a photo of %s." ), photo.name );
                    } else {
                        p->add_msg_if_player(
                            _( "You took a photo of %1$s. It is %2$s." ), photo.name,
                            photo_quality_name( photo_quality ) );
                    }
                    std::vector<std::string> blinded_names;
                    for( monster * const& monster_p : monster_vec ) {
                        if( dist < 4 && one_in( dist + 2 ) && monster_p->has_flag( MF_SEES ) ) {
                            monster_p->add_effect( effect_blind, rng( 5_turns, 10_turns ) );
                            blinded_names.push_back( monster_p->name() );
                        }
                    }
                    for( Character * const& character_p : character_vec ) {
                        if( dist < 4 && one_in( dist + 2 ) && !character_p->is_blind() ) {
                            character_p->add_effect( effect_blind, rng( 5_turns, 10_turns ) );
                            blinded_names.push_back( character_p->name );
                        }
                    }
                    if( !blinded_names.empty() ) {
                        p->add_msg_if_player(
                            _( "%s looks blinded." ),
                            enumerate_as_string(
                                blinded_names.begin(), blinded_names.end(),
                        []( const std::string & it ) { return colorize( it, c_light_blue ); } ) );
                    }
                }
                if( !monster_vec.empty() ) {
                    item_save_monsters( *p, *it, monster_vec, photo_quality );
                }
                return it->type->charges_to_use();
            }
        }
        return it->type->charges_to_use();
    }

    if( c_photos == choice ) {
        show_photo_selection( *p, *it, "CAMERA_EXTENDED_PHOTOS" );
        return it->type->charges_to_use();
    }

    if( c_monsters == choice ) {
        if( p->is_blind() ) {
            p->add_msg_if_player( _( "You can't see the camera screen, you're blind." ) );
            return 0;
        }
        uilist pmenu;

        pmenu.text = _( "Your collection of monsters:" );

        std::vector<mtype_id> monster_photos;
        std::vector<std::string> descriptions;

        std::istringstream f_mon( it->get_var( "CAMERA_MONSTER_PHOTOS" ) );
        std::string s;
        int k = 0;
        while( getline( f_mon, s, ',' ) ) {

            if( s.empty() ) { continue; }

            monster_photos.emplace_back( s );

            std::string menu_str;

            const monster dummy( monster_photos.back() );
            menu_str = dummy.name();
            descriptions.push_back( dummy.type->get_description() );

            getline( f_mon, s, ',' );
            char *chq = s.data();
            const int quality = atoi( chq );

            menu_str += " [" + photo_quality_name( quality ) + "]";

            pmenu.addentry( k++, true, -1, menu_str.c_str() );
        }

        int choice;
        do {
            pmenu.query();
            choice = pmenu.ret;

            if( choice < 0 ) { break; }

            popup( "%s", descriptions[choice].c_str() );

        } while( true );

        return it->type->charges_to_use();
    }

    if( c_upload == choice ) {

        if( p->is_blind() ) {
            p->add_msg_if_player( _( "You can't see the camera screen, you're blind." ) );
            return 0;
        }

        p->moves -= to_moves<int>( 2_seconds );

        avatar* you = p->as_avatar();
        item* loc = nullptr;
        if( you != nullptr ) {
            loc = game_menus::inv::titled_filter_menu(
            []( const item & it ) { return it.has_flag( flag_MC_MOBILE ); }, *you,
            _( "Insert memory card" ) );
        }
        if( !loc ) {
            p->add_msg_if_player( m_info, _( "You do not have that item!" ) );
            return it->type->charges_to_use();
        }
        item& mc = *loc;

        if( !mc.has_flag( flag_MC_MOBILE ) ) {
            p->add_msg_if_player( m_info, _( "This is not a compatible memory card." ) );
            return it->type->charges_to_use();
        }

        init_memory_card_with_random_stuff( mc );

        if( mc.has_flag( flag_MC_ENCRYPTED ) ) {
            if( !query_yn( _( "This memory card is encrypted.  Format and clear data?" ) ) ) {
                return it->type->charges_to_use();
            }
        }
        if( mc.has_flag( flag_MC_HAS_DATA ) ) {
            if( !query_yn( _( "Are you sure you want to clear the old data on the card?" ) ) ) {
                return it->type->charges_to_use();
            }
        }

        mc.convert( itype_mobile_memory_card );
        mc.clear_vars();
        mc.unset_flags();
        mc.set_flag( flag_MC_HAS_DATA );

        mc.set_var( "MC_MONSTER_PHOTOS", it->get_var( "CAMERA_MONSTER_PHOTOS" ) );
        mc.set_var( "MC_EXTENDED_PHOTOS", it->get_var( "CAMERA_EXTENDED_PHOTOS" ) );
        p->add_msg_if_player( m_info, _( "You upload your photos and monster collection to memory "
                                         "card." ) );

        return it->type->charges_to_use();
    }

    return it->type->charges_to_use();
}

int iuse::ehandcuffs( player* p, item* it, bool t, const tripoint_bub_ms& pos )
{

    if( t ) {

        if( g->m.has_flag( "SWIMMABLE", pos.xy() ) ) {
            it->unset_flag( flag_NO_UNWIELD );
            it->ammo_unset();
            it->deactivate();
            add_msg( m_good, _( "%s automatically turned off!" ), it->tname() );
            return it->type->charges_to_use();
        }

        if( it->charges == 0 ) {

            sound_event se;
            se.origin = p->bub_pos();
            se.volume = 40;
            se.category = sounds::sound_t::combat;
            se.description = "Click.";
            se.id = "tool";
            se.variant = "handcuffs";
            sounds::sound( se );
            it->unset_flag( flag_NO_UNWIELD );
            it->deactivate();

            if( p->has_item( *it ) && p->primary_weapon().typeId() == itype_e_handcuffs ) {
                add_msg( m_good, _( "%s on your hands opened!" ), it->tname() );
            }

            return it->type->charges_to_use();
        }

        if( p->has_item( *it ) ) {
            if( p->has_active_bionic( bio_shock ) && p->get_power_level() >= bio_shock->power_trigger
                && one_in( 5 ) ) {
                p->mod_power_level( -bio_shock->power_trigger );

                it->unset_flag( flag_NO_UNWIELD );
                it->ammo_unset();
                it->deactivate();
                add_msg( m_good,
                         _( "The %s crackle with electricity from your bionic, then come off your "
                            "hands!" ),
                         it->tname() );

                return it->type->charges_to_use();
            }
        }

        if( action_time_scale::once_every_this_tick( 1_minutes ) ) {
            sound_event se;
            se.origin = p->bub_pos();
            se.volume = 70;
            se.category = sounds::sound_t::alarm;
            se.description = _( "a police siren, whoop WHOOP." );
            se.id = "environment";
            se.variant = "police_siren";
            sounds::sound( se );
        }

        const point p2( it->get_var( "HANDCUFFS_X", 0 ), it->get_var( "HANDCUFFS_Y", 0 ) );

        if( ( it->ammo_remaining() > it->type->maximum_charges() - 1000 )
            && ( p2.x != pos.x() || p2.y != pos.y() ) ) {
            if( p->has_item( *it ) && p->primary_weapon().typeId() == itype_e_handcuffs ) {
                if( p->is_elec_immune() ) {
                    if( one_in( 10 ) ) {
                        add_msg( m_good, _( "The cuffs try to shock you, but you're protected from "
                                            "electricity." ) );
                    }
                } else {
                    add_msg( m_bad, _( "Ouch, the cuffs shock you!" ) );

                    p->apply_damage( nullptr, bodypart_id( "arm_l" ), rng( 0, 2 ) );
                    p->apply_damage( nullptr, bodypart_id( "arm_r" ), rng( 0, 2 ) );
                    p->mod_pain( rng( 2, 5 ) );
                }

            } else {
                add_msg( m_bad, _( "The %s spark with electricity!" ), it->tname() );
            }

            it->charges -= 50;
            if( it->charges < 1 ) { it->charges = 1; }

            it->set_var( "HANDCUFFS_X", pos.x() );
            it->set_var( "HANDCUFFS_Y", pos.y() );

            return it->type->charges_to_use();
        }

        return it->type->charges_to_use();
    }

    if( it->is_active() ) {
        add_msg( _( "The %s are clamped tightly on your wrists.  You can't take them off." ),
                 it->tname() );
    } else {
        add_msg( _( "The %s have discharged and can be taken off." ), it->tname() );
    }

    return it->type->charges_to_use();
}

int iuse::foodperson( player* p, item* it, bool t, const tripoint_bub_ms& pos )
{
    if( t ) {
        if( action_time_scale::once_every_this_tick( 1_minutes ) ) {
            const SpeechBubble& speech = get_speech( "foodperson_mask" );
            sound_event se;
            se.origin = pos;
            se.volume = speech.volume;
            se.category = sounds::sound_t::alarm;
            se.description = speech.text.translated();
            se.id = "speech";
            se.variant = "foodperson_mask";
            sounds::sound( se );
        }
        return it->type->charges_to_use();
    }

    time_duration shift = time_duration::from_turns(
                              it->magazine_current()->ammo_remaining() * it->type->tool->turns_per_charge
                              - it->type->tool->turns_active );

    p->add_msg_if_player(
        m_info, _( "Your HUD lights-up: \"Your shift ends in %s\"." ), to_string( shift ) );
    return 0;
}

int iuse::radiocar( player* p, item* it, bool, const tripoint_bub_ms & )
{
    int choice = -1;
    item* bomb_it = it->contents.get_item_with( []( const item & c ) {
        return c.has_flag( flag_RADIOCARITEM );
    } );
    if( bomb_it == nullptr ) {
        choice = uilist( _( "Using RC car:" ), {_( "Turn on" ), _( "Put a bomb to car" )} );
    } else {
        choice = uilist( _( "Using RC car:" ), {_( "Turn on" ), bomb_it->tname()} );
    }
    if( choice < 0 ) { return 0; }

    if( choice == 0 ) { // Turn car ON
        if( !it->ammo_sufficient() ) {
            p->add_msg_if_player( _( "The RC car's batteries seem to be dead." ) );
            return 0;
        }

        it->convert( itype_radio_car_on );
        it->activate();

        p->add_msg_if_player( _( "You turned on your RC car, now place it on ground, and use radio "
                                 "control to play." ) );

        return 0;
    }

    if( choice == 1 ) {

        if( bomb_it == nullptr ) { // arming car with bomb

            avatar* you = p->as_avatar();
            item* loc = nullptr;
            if( you != nullptr ) {
                loc = game_menus::inv::titled_filter_menu(
                []( const item & it ) { return it.has_flag( flag_RADIOCARITEM ); }, *you,
                _( "Arm what?" ) );
            }
            if( !loc ) {
                p->add_msg_if_player( m_info, _( "You do not have that item!" ) );
                return 0;
            }
            item& put = *loc;

            if( put.has_flag( flag_RADIOCARITEM )
                && ( put.volume() <= 1250_ml || ( put.weight() <= 2_kilogram ) ) ) {
                p->moves -= to_moves<int>( 3_seconds );
                p->add_msg_if_player( _( "You armed your RC car with %s." ), put.tname() );
                it->put_in( put.detach() );
            } else if( !put.has_flag( flag_RADIOCARITEM ) ) {
                p->add_msg_if_player( _( "RC car with %s?  How?" ), put.tname() );
            } else {
                p->add_msg_if_player(
                    _( "Your %s is too heavy or bulky for this RC car." ), put.tname() );
            }
        } else { // Disarm the car
            p->moves -= to_moves<int>( 2_seconds );

            p->inv_assign_empty_invlet( *bomb_it, true ); // force getting an invlet.
            p->i_add( it->remove_item( *bomb_it ) );

            p->add_msg_if_player( _( "You disarmed your RC car." ) );
        }
    }

    return it->type->charges_to_use();
}

int iuse::radiocaron( player* p, item* it, bool t, const tripoint_bub_ms& pos )
{
    if( t ) {
        //~Sound of a radio controlled car moving around
        sound_event se;
        se.origin = pos;
        se.volume = 50;
        se.category = sounds::sound_t::movement;
        se.movement_noise = true;
        se.description = _( "buzzz…" );
        se.id = "misc";
        se.variant = "rc_car_drives";
        sounds::sound( se );

        return it->type->charges_to_use();
    } else if( !it->ammo_sufficient() ) {
        // Deactivate since other mode has an iuse too.
        it->deactivate();
        return 0;
    }

    int choice = uilist( _( "What to do with activated RC car?" ), {_( "Turn off" )} );

    if( choice < 0 ) { return it->type->charges_to_use(); }

    if( choice == 0 ) {
        it->convert( itype_radio_car );
        it->deactivate();

        p->add_msg_if_player( _( "You turned off your RC car." ) );
        return it->type->charges_to_use();
    }

    return it->type->charges_to_use();
}

/**
 * Send radio signal from player.
 */
static void emit_radio_signal( player &p, const flag_id &signal )
{
    const auto visitor = [&]( item & it, const tripoint_bub_ms & loc ) -> VisitResponse {
        if( it.has_flag( flag_RADIO_ACTIVATION ) && it.has_flag( signal ) )
    {
        sound_event se;
        se.origin = loc;
        se.volume = 50;
        se.category = sounds::sound_t::alarm;
        se.description = _( "beep" );
            se.id = "misc";
            se.variant = "beep";
            sounds::sound( se );
            bool invoke_proc = it.has_flag( flag_RADIO_INVOKE_PROC );
            // Invoke to transform item
            it.type->invoke( p, it, loc );
            if( invoke_proc ) {
                // Cause invocation of transformed item on next turn processing
                it.ammo_unset();
            }
        }
        return VisitResponse::NEXT;
    };

    int z_min = g->m.has_zlevels() ? -OVERMAP_DEPTH : 0;
    int z_max = g->m.has_zlevels() ? OVERMAP_HEIGHT : 0;
    for( int zlev = z_min; zlev <= z_max; zlev++ ) {
        for( auto loc : g->m.points_on_zlevel( zlev ) ) {
            // Items on ground
            map_cursor mc( loc );
            mc.visit_items( [&]( item * it ) {
                return visitor( *it, loc );
            } );

            // Items in vehicles
            optional_vpart_position vp = g->m.veh_at( loc );
            if( !vp ) {
                continue;
            }
            std::optional<vpart_reference> vpr = vp.part_with_feature( "CARGO", false );
            if( !vpr ) {
                continue;
            }
            vehicle_cursor vc( vp->vehicle(), vpr->part_index() );
            vc.visit_items( [&]( item * it ) {
                return visitor( *it, loc );
            } );
        }
    }

    // Items on creatures
    for( Creature &cr : g->all_creatures() ) {
        const auto &cr_pos = cr.bub_pos();
        if( cr.is_monster() ) {
            monster &mon = *cr.as_monster();
            mon.visit_items( [&]( item * it ) {
                return visitor( *it, cr_pos );
            } );
        } else {
            Character &ch = *cr.as_character();
            ch.visit_items( [&]( item * it ) {
                return visitor( *it, cr_pos );
            } );
        }
    }
}

int iuse::radiocontrol( player* p, item* it, bool t, const tripoint_bub_ms & )
{
    if( t ) {
        if( !it->units_sufficient( *p ) ) {
            it->deactivate();
            p->remove_value( "remote_controlling" );
        } else if( p->get_value( "remote_controlling" ).empty() ) {
            it->deactivate();
        }

        return it->type->charges_to_use();
    }

    const char *car_action = nullptr;

    if( !it->is_active() ) {
        car_action = _( "Take control of RC car" );
    } else {
        car_action = _( "Stop controlling RC car" );
    }

    int choice = uilist(
                     _( "What to do with radio control?" ),
    {car_action, _( "Press red button" ), _( "Press blue button" ), _( "Press green button" )} );

    if( choice < 0 ) {
        return 0;
    } else if( choice == 0 ) {
        if( it->is_active() ) {
            it->deactivate();
            p->remove_value( "remote_controlling" );
        } else {
            std::vector<std::pair<tripoint_bub_ms, item *>> rc_pairs;
            for( tripoint_bub_ms pt : g->m.points_on_zlevel( p->bub_pos().z() ) ) {
                map_cursor mc( pt );
                std::vector<item *> rc_items_here = mc.items_with( [&]( const item & it ) {
                    return it.has_flag( flag_RADIO_CONTROLLED );
                } );
                for( item * it : rc_items_here ) { rc_pairs.emplace_back( pt, it ); }
            }

            if( rc_pairs.empty() ) {
                p->add_msg_if_player( _( "No active RC cars on ground and in range." ) );
                return it->type->charges_to_use();
            }

            std::vector<tripoint_bub_ms> locations;
            uilist pick_rc;
            pick_rc.text = _( "Choose car to control." );
            for( size_t i = 0; i < rc_pairs.size(); i++ ) {
                pick_rc.addentry( i, true, MENU_AUTOASSIGN, rc_pairs[i].second->display_name() );
                locations.push_back( rc_pairs[i].first );
            }
            pointmenu_cb callback( locations );
            pick_rc.callback = &callback;
            pick_rc.query();
            if( pick_rc.ret < 0 || static_cast<size_t>( pick_rc.ret ) >= rc_pairs.size() ) {
                p->add_msg_if_player( m_info, _( "Never mind." ) );
                return it->type->charges_to_use();
            }

            auto rc_loc = locations[pick_rc.ret];

            p->add_msg_if_player( m_good, _( "You take control of the RC car." ) );
            p->set_value( "remote_controlling",
            serialize_wrapper( [&]( JsonOut & jo ) { rc_loc.serialize( jo ); } ) );
            it->activate();
        }
    } else if( choice > 0 ) {
        const flag_id signal( "RADIOSIGNAL_" + std::to_string( choice ) );

        std::vector<item *> bombs = p->items_with( [&]( const item & it ) -> bool {
            return it.has_flag( flag_RADIO_ACTIVATION ) && it.has_flag( flag_BOMB )
            && it.has_flag( signal );
        } );

        if( !bombs.empty() ) {
            p->add_msg_if_player(
                m_warning,
                _( "The %s in your inventory would explode on this signal.  Place it down before "
                   "sending the signal." ),
                bombs.front()->display_name() );
            return 0;
        }

        p->add_msg_if_player( _( "Click." ) );
        emit_radio_signal( *p, signal );
        p->moves -= to_moves<int>( 2_seconds );
    }

    return it->type->charges_to_use();
}

static bool hackveh( player& p, item& it, vehicle& veh )
{
    if( !veh.is_locked || !veh.has_security_working() ) { return true; }
    const auto advanced = !veh.get_avail_parts( "REMOTE_CONTROLS" ).empty();
    if( advanced && veh.is_alarm_on ) {
        p.add_msg_if_player( m_bad, _( "This vehicle's security system has locked you out!" ) );
        return false;
    }

    /** @EFFECT_INT increases chance of bypassing vehicle security system */

    /** @EFFECT_COMPUTER increases chance of bypassing vehicle security system */
    int roll = dice( p.get_skill_level( skill_computer ) + 2, p.int_cur ) - ( advanced ? 50 : 25 );
    int effort = 0;
    bool success = false;
    if( roll < -20 ) { // Really bad rolls will trigger the alarm before you know it exists
        effort = 1;
        p.add_msg_if_player( m_bad, _( "You trigger the alarm!" ) );
        veh.is_alarm_on = true;
    } else if( roll >= 20 ) { // Don't bother the player if it's trivial
        effort = 1;
        p.add_msg_if_player( m_good, _( "You quickly bypass the security system!" ) );
        success = true;
    }

    if( effort == 0 && !query_yn( _( "Try to hack this car's security system?" ) ) ) {
        // Scanning for security systems isn't free
        p.moves -= to_moves<int>( 1_seconds );
        it.charges -= 1;
        return false;
    }

    p.practice( skill_computer, advanced ? 10 : 3 );
    if( roll < -10 ) {
        effort = rng( 4, 8 );
        p.add_msg_if_player( m_bad, _( "You waste some time, but fail to affect the security "
                                       "system." ) );
    } else if( roll < 0 ) {
        effort = 1;
        p.add_msg_if_player( m_bad, _( "You fail to affect the security system." ) );
    } else if( roll < 20 ) {
        effort = rng( 2, 8 );
        p.add_msg_if_player( m_mixed, _( "You take some time, but manage to bypass the security "
                                         "system!" ) );
        success = true;
    }

    p.moves -= to_moves<int>( time_duration::from_seconds( effort ) );
    it.charges -= effort;
    if( success && advanced ) { // Unlock controls, but only if they're drive-by-wire
        veh.is_locked = false;
    }
    return success;
}

static vehicle *pickveh( const tripoint_bub_ms& center, bool advanced )
{
    static const std::string ctrl = "CTRL_ELECTRONIC";
    static const std::string advctrl = "REMOTE_CONTROLS";
    uilist pmenu;
    pmenu.title = _( "Select vehicle to access" );
    std::vector<vehicle *> vehs;

    for( auto& veh : g->m.get_vehicles() ) {
        auto& v = veh.v;
        if( rl_dist( center, v->bub_ms_location() ) < 40 && v->fuel_left( itype_battery, true ) > 0
            && ( !v->get_avail_parts( advctrl ).empty()
                 || ( !advanced && !v->get_avail_parts( ctrl ).empty() ) ) ) {
            vehs.push_back( v );
        }
    }
    std::vector<tripoint_bub_ms> locations;
    for( int i = 0; i < static_cast<int>( vehs.size() ); i++ ) {
        auto veh = vehs[i];
        locations.push_back( veh->bub_ms_location() );
        pmenu.addentry( i, true, MENU_AUTOASSIGN, veh->name );
    }

    if( vehs.empty() ) {
        add_msg( m_bad, _( "No vehicle available." ) );
        return nullptr;
    }

    pointmenu_cb callback( locations );
    pmenu.callback = &callback;
    pmenu.w_y_setup = 0;
    pmenu.query();

    if( pmenu.ret < 0 || pmenu.ret >= static_cast<int>( vehs.size() ) ) {
        return nullptr;
    } else {
        return vehs[pmenu.ret];
    }
}

int iuse::remoteveh( player* p, item* it, bool t, const tripoint_bub_ms& pos )
{
    vehicle* remote = g->remoteveh();
    if( t ) {
        bool stop = false;
        if( !it->units_sufficient( *p ) ) {
            p->add_msg_if_player( m_bad, _( "The remote control's battery goes dead." ) );
            stop = true;
        } else if( remote == nullptr ) {
            p->add_msg_if_player( _( "Lost contact with the vehicle." ) );
            stop = true;
        } else if( remote->fuel_left( itype_battery, true ) == 0 ) {
            p->add_msg_if_player( m_bad, _( "The vehicle's battery died." ) );
            stop = true;
        }
        if( stop ) {
            it->deactivate();
            g->setremoteveh( nullptr );
        }

        return it->type->charges_to_use();
    }

    bool controlling = it->is_active() && remote != nullptr;
    int choice = uilist(
    _( "What to do with remote vehicle control:" ), {
        controlling ? _( "Stop controlling the vehicle." ) : _( "Take control of a vehicle." ),
        _( "Execute one vehicle action" )
    } );

    if( choice < 0 || choice > 1 ) { return 0; }

    if( choice == 0 && controlling ) {
        it->deactivate();
        g->setremoteveh( nullptr );
        return 0;
    }

    const auto p2 = g->u.view_offset.xy();

    vehicle* veh = pickveh( pos, choice == 0 );

    if( veh == nullptr ) { return 0; }

    if( !hackveh( *p, *it, *veh ) ) { return 0; }

    if( choice == 0 ) {
        if( g->u.has_trait( trait_WAYFARER ) ) {
            add_msg( m_info, _( "Despite using a controller, you still refuse to take control of "
                                "this vehicle." ) );
        } else {
            it->activate();
            g->setremoteveh( veh );
            p->add_msg_if_player( m_good, _( "You take control of the vehicle." ) );
            if( !veh->engine_on ) { veh->start_engines(); }
        }
    } else if( choice == 1 ) {
        const auto rctrl_parts = veh->get_avail_parts( "REMOTE_CONTROLS" );
        // Revert to original behavior if we can't find remote controls.
        if( rctrl_parts.empty() ) {
            veh->use_controls( tripoint_bub_ms( pos ) );
        } else {
            veh->use_controls( tripoint_bub_ms( rctrl_parts.begin()->pos() ) );
        }
    }

    g->u.view_offset.x() = p2.x();
    g->u.view_offset.y() = p2.y();
    return it->type->charges_to_use();
}

int iuse::autoclave( player* p, item*, bool, const tripoint_bub_ms& pos )
{
    iexamine::autoclave_empty( *p, pos );
    return 0;
}

