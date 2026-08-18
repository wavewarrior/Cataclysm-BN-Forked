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

int iuse::extinguisher( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( !it->ammo_sufficient() ) { return 0; }
    // If anyone other than the player wants to use one of these,
    // they're going to need to figure out how to aim it.
    const std::optional<tripoint_bub_ms> dest_ = choose_adjacent( _( "Spray where?" ) );
    if( !dest_ ) { return 0; }
    auto dest = *dest_;

    p->moves -= to_moves<int>( 2_seconds );

    // Reduce the strength of fire (if any) in the target tile.
    g->m.mod_field_intensity( dest, fd_fire, 0 - rng( 2, 3 ) );

    // Also spray monsters in that tile.
    if( monster * const mon_ptr = g->critter_at<monster>( dest, true ) ) {
        monster& critter = *mon_ptr;
        critter.moves -= to_moves<int>( 2_seconds );
        bool blind = false;
        if( one_in( 2 ) && critter.has_flag( MF_SEES ) ) {
            blind = true;
            critter.add_effect( effect_blind, rng( 1_minutes, 2_minutes ) );
        }
        if( g->u.sees( critter ) ) {
            p->add_msg_if_player( _( "The %s is sprayed!" ), critter.name() );
            if( blind ) { p->add_msg_if_player( _( "The %s looks blinded." ), critter.name() ); }
        }
        if( critter.made_of( LIQUID ) ) {
            if( g->u.sees( critter ) ) {
                p->add_msg_if_player( _( "The %s is frozen!" ), critter.name() );
            }
            critter.apply_damage( p, bodypart_id( "torso" ), rng( 20, 60 ) );
            critter.set_speed_base( critter.get_speed_base() / 2 );
        }
    }

    // Whatever we sprayed, if present extinguish it too.
    if( Creature * target = g->critter_at( dest, true ) ) {
        if( target->has_effect( effect_onfire ) ) { target->remove_effect( effect_onfire ); }
    }

    // Slightly reduce the strength of fire immediately behind the target tile.
    if( g->m.passable( dest ) ) {
        dest.x() += ( dest.x() - p->bub_pos().x() );
        dest.y() += ( dest.y() - p->bub_pos().y() );

        g->m.mod_field_intensity( dest, fd_fire, std::min( 0 - rng( 0, 1 ) + rng( 0, 1 ), 0 ) );
    }

    return it->type->charges_to_use();
}

int iuse::unpack_item( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_underwater() ) {
        p->add_msg_if_player( m_info, _( "You can't do that while underwater." ) );
        return 0;
    }
    std::string oname = it->typeId().str() + "_on";
    p->moves -= to_moves<int>( 10_seconds );
    p->add_msg_if_player( _( "You unpack your %s for use." ), it->tname() );
    it->convert( itype_id( oname ) );
    it->deactivate();
    return 0;
}

int iuse::pack_cbm( player* p, item* it, bool, const tripoint_bub_ms & )
{
    item* bionic = g->inv_map_splice(
    []( const item & e ) { return e.is_bionic() && e.has_flag( flag_NO_PACKED ); },
    _( "Choose CBM to pack" ), PICKUP_RANGE, _( "You don't have any CBMs." ) );

    if( !bionic ) { return 0; }
    if( !bionic->faults.empty() ) {
        if( p->query_yn( _( "This CBM is faulty.  You should mend it first.  Do you want to try?" ) ) ) {
            avatar_funcs::mend_item( *p->as_avatar(), *bionic );
        }
        return 0;
    }

    const int success = p->get_skill_level( skill_firstaid ) - rng( 0, 6 );
    if( success > 0 ) {
        p->add_msg_if_player( m_good, _( "You carefully prepare the CBM for sterilization." ) );
        bionic->unset_flag( flag_NO_PACKED );
    } else {
        p->add_msg_if_player( m_bad, _( "You fail to properly prepare the CBM." ) );
    }

    std::vector<item_comp> comps;
    comps.emplace_back( it->typeId(), 1 );
    p->consume_items( comps, 1, is_crafting_component );

    return 0;
}

int iuse::pack_item( player* p, item* it, bool t, const tripoint_bub_ms & )
{
    if( p->is_underwater() ) {
        p->add_msg_if_player( m_info, _( "You can't do that while underwater." ) );
        return 0;
    }
    if( t ) { // Normal use
        // Numbers below -1 are reserved for worn items
    } else if( p->get_item_position( it ) < -1 ) {
        p->add_msg_if_player(
            m_info, _( "You can't pack your %s until you take it off." ), it->tname() );
        return 0;
    } else { // Turning it off
        std::string oname = it->typeId().str();
        if( oname.ends_with( "_on" ) ) {
            oname.erase( oname.length() - 3, 3 );
        } else {
            debugmsg( "no item type to turn it into (%s)!", oname );
            return 0;
        }
        p->moves -= to_moves<int>( 10_seconds );
        p->add_msg_if_player( _( "You pack your %s for storage." ), it->tname() );
        it->convert( itype_id( oname ) );
        it->deactivate();
    }
    return 0;
}

int iuse::water_purifier( player* p, item* it, bool, const tripoint_bub_ms & )
{
    constexpr auto purification_efficiency = 8; // one tablet purifies 250ml x 8 = 2L

    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    auto obj = g->inv_map_splice(
    []( const item & e ) {
        return !e.contents.empty() && e.contents.front().typeId() == itype_water;
    },
    _( "Purify what?" ), 1, _( "You don't have water to purify." ) );

    if( !obj ) {
        p->add_msg_if_player( m_info, _( "You do not have that item!" ) );
        return 0;
    }

    item& liquid = obj->contents.front();
    const auto used_charges = std::max( liquid.charges / purification_efficiency, 1 );
    if( !it->units_sufficient( *p, used_charges ) ) {
        p->add_msg_if_player( m_info, _( "That volume of water is too large to purify." ) );
        return 0;
    }

    p->moves -= to_moves<int>( 2_seconds );

    liquid.convert( itype_water_clean );
    liquid.poison = 0;
    return used_charges;
}

int iuse::radio_off( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( !it->units_sufficient( *p ) ) {
        p->add_msg_if_player( _( "It's dead." ) );
    } else {
        p->add_msg_if_player( _( "You turn the radio on." ) );
        it->convert( itype_radio_on );
        it->activate();
    }
    return it->type->charges_to_use();
}

int iuse::directional_antenna( player* p, item* it, bool, const tripoint_bub_ms & )
{
    // Find out if we have an active radio
    auto radios = p->items_with( []( const item & it ) { return it.typeId() == itype_radio_on; } );
    // If we don't wield the radio, also check on the ground
    if( radios.empty() ) {
        map_stack items = get_map().i_at( p->bub_pos() );
        for( item * const& an_item : items ) {
            if( an_item->typeId() == itype_radio_on ) { radios.push_back( an_item ); }
        }
    }
    if( radios.empty() ) {
        add_msg( m_info, _( "Must have an active radio to check for signal direction." ) );
        return 0;
    }
    const item& radio = *radios.front();
    // Find the radio station its tuned to (if any)
    const auto tref = get_overmapbuffer( p->get_dimension() ).find_radio_station( radio.frequency );
    if( !tref ) {
        p->add_msg_if_player( m_info, _( "You can't find the direction if your radio isn't tuned." ) );
        return 0;
    }
    // Report direction.
    // TODO: fix point types
    const tripoint_abs_sm player_pos( p->abs_sm_pos() );
    direction angle = direction_from( player_pos.xy(), tref.abs_sm_pos );
    add_msg( _( "The signal seems strongest to the %s." ), direction_name( angle ) );
    return it->type->charges_to_use();
}

int iuse::radio_on( player* p, item* it, bool t, const tripoint_bub_ms& pos )
{
    if( t ) {
        // Normal use
        std::string message = _( "Radio: Kssssssssssssh." );
        const auto tref = get_overmapbuffer( p->get_dimension() ).find_radio_station( it->frequency );
        if( tref ) {
            const auto selected_tower = tref.tower;
            if( selected_tower->type == radio_type::MESSAGE_BROADCAST ) {
                message = selected_tower->message;
            } else if( selected_tower->type == radio_type::WEATHER_RADIO ) {
                message = weather_forecast( tref.abs_sm_pos );
            }

            message = obscure_message( message, [&]() -> int {
                int signal_roll = dice( 10, tref.signal_strength * 3 );
                int static_roll = dice( 10, 100 );
                if( static_roll > signal_roll )
                {
                    if( static_roll < signal_roll * 1.1 && one_in( 4 ) ) {
                        return 0;
                    } else {
                        return '#';
                    }
                } else
                {
                    return -1;
                }
            } );

            std::vector<std::string> segments = foldstring( message, RADIO_PER_TURN );
            int index = to_turn<int>( calendar::turn ) % segments.size();
            message = string_format( _( "radio: %s" ), segments[index] );
        }
        sound_event se;
        se.origin = pos;
        se.volume = 60;
        se.category = sounds::sound_t::electronic_speech;
        se.description = message;
        sounds::sound( se );
        if( !sfx::is_channel_playing( sfx::channel::radio ) ) {
            if( one_in( 10 ) ) {
                sfx::play_ambient_variant_sound(
                    "radio", "static", 100, sfx::channel::radio, 300, -1, 0 );
            } else if( one_in( 10 ) ) {
                sfx::play_ambient_variant_sound(
                    "radio", "inaudible_chatter", 100, sfx::channel::radio, 300, -1, 0 );
            }
        }
    } else { // Activated
        int ch = 1;
        if( it->ammo_remaining() > 0 ) { ch = uilist( _( "Radio:" ), {_( "Scan" ), _( "Turn off" )} ); }

        switch( ch ) {
            case 0: {
                const int old_frequency = it->frequency;
                const radio_tower* lowest_tower = nullptr;
                const radio_tower* lowest_larger_tower = nullptr;
                for( auto& tref : get_overmapbuffer( p->get_dimension() ).find_all_radio_stations() ) {
                    const auto new_frequency = tref.tower->frequency;
                    if( new_frequency == old_frequency ) { continue; }
                    if( new_frequency > old_frequency
                        && ( lowest_larger_tower == nullptr
                             || new_frequency < lowest_larger_tower->frequency ) ) {
                        lowest_larger_tower = tref.tower;
                    } else if( lowest_tower == nullptr || new_frequency < lowest_tower->frequency ) {
                        lowest_tower = tref.tower;
                    }
                }
                if( lowest_larger_tower != nullptr ) {
                    it->frequency = lowest_larger_tower->frequency;
                } else if( lowest_tower != nullptr ) {
                    it->frequency = lowest_tower->frequency;
                }
            }
            break;
            case 1:
                p->add_msg_if_player( _( "The radio dies." ) );
                it->convert( itype_radio );
                it->deactivate();
                sfx::fade_audio_channel( sfx::channel::radio, 300 );
                break;
            default:
                break;
        }
    }
    return it->type->charges_to_use();
}

int iuse::noise_emitter_off( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( !it->units_sufficient( *p ) ) {
        p->add_msg_if_player( _( "It's dead." ) );
    } else {
        p->add_msg_if_player( _( "You turn the noise emitter on." ) );
        it->convert( itype_noise_emitter_on );
        it->activate();
    }
    return it->type->charges_to_use();
}

int iuse::noise_emitter_on( player *p, item *it, bool t, const tripoint_bub_ms &pos )
{
    if( t ) { // Normal use
        //~ the sound of a noise emitter when turned on
        sound_event se;
        se.origin = pos;
        se.volume = 100;
        se.category = sounds::sound_t::alarm;
        se.description = _( "KXSHHHHRRCRKLKKK!" );
        se.id = "tool";
        se.variant = "noise_emitter";
        sounds::sound( se );
    } else { // Turning it off
        p->add_msg_if_player( _( "The infernal racket dies as the noise emitter turns off." ) );
        it->convert( itype_noise_emitter );
        it->deactivate();
    }
    return it->type->charges_to_use();
}

// Ugly and uses variables that shouldn't be public
int iuse::note_bionics( player* p, item* it, bool t, const tripoint_bub_ms& pos )
{
    const bool possess = p->has_item( *it );

    if( !t ) {
        it->revert( p, true );
        it->deactivate();
        return 0;
    }
    if( !p->is_avatar() ) {
        // Not supported at the moment
        return 0;
    }
    map& here = get_map();

    if( !p->has_enough_charges( *it, false ) ) {
        it->revert( p, true );
        it->deactivate();
        return 0;
    }

    // Try to minimize the use of has_enough_charges() because it's kind of expensive.
    bool no_charges = false;
    for( const tripoint_bub_ms& pt : here.points_in_radius( pos, PICKUP_RANGE ) ) {
        if( !here.has_items( pt ) || !p->sees( pt ) ) { continue; }
        for( item * const& corpse : here.i_at( pt ) ) {
            if( !corpse->is_corpse()
                || corpse->get_var( "bionics_scanned_by", -1 ) == p->getID().get_value() ) {
                continue;
            }

            std::vector<const item *> cbms;
            for( const item * const& maybe_cbm : corpse->get_components() ) {
                if( maybe_cbm->is_bionic() ) { cbms.push_back( maybe_cbm ); }
            }

            int charges = static_cast<int>( cbms.size() );
            charges -= it->ammo_consume( charges, pos );
            if( possess && it->has_flag( flag_USE_UPS ) ) {
                if( p->use_charges_if_avail( itype_UPS, charges ) ) { charges = 0; }
            }
            if( charges ) {
                p->add_msg_if_player(
                    m_bad, "Your %s doesn't have enough power for the %s", it->tname(),
                    corpse->display_name().c_str() );
                if( !p->has_enough_charges( *it, false ) ) {
                    no_charges = true;
                    break;
                } else {
                    continue;
                }
            }

            corpse->set_var( "bionics_scanned_by", p->getID().get_value() );
            if( !cbms.empty() ) {
                corpse->set_flag( flag_CBM_SCANNED );
                std::string bionics_string = enumerate_as_string(
                                                 cbms.begin(), cbms.end(),
                                                 []( const item * entry ) -> std::string { return entry->display_name(); },
                                                 enumeration_conjunction::none );
                //~ %1 is corpse name, %2 is direction, %3 is bionic name
                p->add_msg_if_player(
                    m_good, _( "A %1$s located %2$s contains %3$s." ), corpse->display_name().c_str(),
                    direction_name( direction_from( p->bub_pos(), pt ) ).c_str(),
                    bionics_string.c_str() );
            }
        }
        if( no_charges ) {
            it->revert( p );
            it->deactivate();
            return 0;
        }
    }

    return 0;
}

int iuse::ma_manual( player* p, item* it, bool, const tripoint_bub_ms & )
{
    // [CR] - should NPCs just be allowed to learn this stuff? Just like that?

    const matype_id style_to_learn = martial_art_learned_from( *it->type );

    if( !style_to_learn.is_valid() ) {
        debugmsg( "ERROR: Invalid martial art" );
        return 0;
    }

    p->martial_arts_data->learn_style( style_to_learn, p->is_avatar() );

    return 1;
}

int iuse::hammer( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }

    const std::function<bool( const tripoint_bub_ms & )> f = []( const tripoint_bub_ms & pnt ) {
        if( pnt == g->u.bub_pos() ) { return false; }
        const ter_id ter = g->m.ter( pnt );

        return ( ter->nail_pull_result != ter_str_id::NULL_ID() );
    };

    const std::optional<tripoint_bub_ms> pnt_ =
        choose_adjacent_highlight( _( "Pry where?" ), _( "There is nothing to pry nearby." ), f, false );
    if( !pnt_ ) { return 0; }
    const tripoint_bub_ms& pnt = *pnt_;
    if( !f( pnt ) ) {
        if( pnt == p->bub_pos() ) {
            p->add_msg_if_player( _( "You try to hit yourself with the hammer." ) );
            p->add_msg_if_player( _( "But you can't touch this." ) );
        } else {
            p->add_msg_if_player( m_info, _( "You can't pry that." ) );
        }
        return 0;
    } else {
        // pry action
        p->assign_activity( std::make_unique<player_activity>(
                                std::make_unique<pry_nails_activity_actor>( bub_to_abs( pnt ) ) ) );
        return it->type->charges_to_use();
    }
}

int iuse::crowbar( player* p, item* it, bool, const tripoint_bub_ms& pos )
{
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    const pry_result* pry = nullptr;
    bool pry_furn;

    const std::function<bool( const tripoint_bub_ms & )> can_pry = [&p]( const tripoint_bub_ms & pnt ) {
        if( pnt == p->bub_pos() ) { return false; }
        const ter_id ter = g->m.ter( pnt );
        const furn_id furn = g->m.furn( pnt );

        const bool is_allowed = ter->pry.pry_quality != -1 || furn->pry.pry_quality != -1;
        return is_allowed;
    };

    const std::optional<tripoint_bub_ms> pnt_ =
        ( pos != p->bub_pos() )
        ? pos
        : choose_adjacent_highlight(
            _( "Pry where?" ), _( "There is nothing to pry nearby." ), can_pry, false );
    if( !pnt_ ) { return 0; }
    const tripoint_bub_ms& pnt = *pnt_;
    const ter_id ter = g->m.ter( pnt );
    const furn_id furn = g->m.furn( pnt );

    if( !can_pry( pnt ) ) {
        if( pnt == p->bub_pos() ) {
            p->add_msg_if_player(
                m_info,
                _( "You attempt to pry open your wallet "
                   "but alas.  You are just too miserly." ) );
        } else if( !ter->has_flag( "LOCKED" ) && ter->open ) {
            p->add_msg_if_player( m_info, _( "You notice the door is unlocked, so you simply open "
                                             "it." ) );
            g->m.ter_set( pnt, ter->open );
        } else {
            p->add_msg_if_player( m_info, _( "You can't pry that." ) );
        }

        return 0;
    }

    if( furn->pry.pry_quality != -1 ) {
        pry_furn = true;
        pry = &furn->pry;
    } else {
        pry_furn = false;
        pry = &ter->pry;
    }

    // Doors need PRY 2 which is on a crowbar, crates need PRY 1 which is on a crowbar
    // & a claw hammer.
    // The iexamine function for crate supplies a hammer object.
    // So this stops the player (A)ctivating a Hammer with a Crowbar in their backpack
    // then managing to open a door.
    const int pry_level = it->get_quality( quality_id( "PRY" ) );

    if( pry_level < pry->pry_quality ) {
        p->add_msg_if_player(
            _( "You can't get sufficient leverage to open that with your %s." ), it->tname() );
        p->mod_moves( 10 ); // spend a few moves trying it.
        return 0;
    }

    // For every level of PRY over the requirement, remove n from the difficulty.
    // Then multiply n by pry_bonus_mult. It's recommended that you don't allow
    // the result to be negative if you can help it.
    int diff = pry->difficulty;
    diff -= ( ( pry_level - pry->pry_quality ) * pry->pry_bonus_mult );

    /** @EFFECT_STR speeds up crowbar prying attempts */
    p->mod_moves( -std::max( 20, diff * 50 - p->str_cur * 10 ) );
    /** @EFFECT_STR increases chance of crowbar prying success */

    if( dice( 4, diff ) < dice( 4, p->str_cur ) ) {
        p->add_msg_if_player( m_good, pry->success_message );

        if( pry_furn ) {
            g->m.furn_set( pnt, pry->new_furn_type );
        } else {
            g->m.ter_set( pnt, pry->new_ter_type );
        }

        if( pry->noise > 0 ) {
            sound_event se;
            se.origin = pnt;
            se.volume = pry->noise;
            se.category = sounds::sound_t::combat;
            se.description = pry->sound.translated();
            se.id = "tool";
            se.variant = "crowbar";
            sounds::sound( se );
        }
        g->m.spawn_items( pnt, item_group::items_from( pry->pry_items, calendar::turn ) );
        if( pry->alarm ) {
            g->events().send<event_type::triggers_alarm>( p->getID() );
            sound_event se;
            se.origin = p->bub_pos();
            se.volume = 100;
            se.category = sounds::sound_t::alarm;
            se.description = _( "an alarm sound!" );
            se.id = "environment";
            se.variant = "alarm";
            sounds::sound( se );
            if( !g->timed_events.queued( TIMED_EVENT_WANTED ) ) {
                g->timed_events
                .add( TIMED_EVENT_WANTED, calendar::turn + 30_minutes, 0, p->abs_sm_pos() );
            }
        }
    } else {
        if( pry->breakable ) {
            // chance of breaking the glass if pry attempt fails
            /** @EFFECT_STR reduces chance of breaking window with crowbar */

            /** @EFFECT_MECHANICS reduces chance of breaking window with crowbar */
            if( dice( 4, diff )
                > ( dice( 2, p->get_skill_level( skill_mechanics ) ) + dice( 2, p->str_cur ) ) * pry_level ) {
                p->add_msg_if_player( m_mixed, pry->break_message );
                sound_event se;
                se.origin = pnt;
                se.volume = pry->break_noise;
                se.category = sounds::sound_t::combat;
                se.description = pry->break_sound.translated();
                se.id = "smash";
                se.variant = "door";
                sounds::sound( se );
                if( pry_furn ) {
                    g->m.furn_set( pnt, pry->break_furn_type );
                } else {
                    g->m.ter_set( pnt, pry->break_ter_type );
                }
                g->m.spawn_items( pnt, item_group::items_from( pry->break_items, calendar::turn ) );
                if( pry->alarm ) {
                    g->events().send<event_type::triggers_alarm>( p->getID() );
                    sound_event se;
                    se.origin = p->bub_pos();
                    se.volume = 100;
                    se.category = sounds::sound_t::alarm;
                    se.description = _( "an alarm sound!" );
                    se.id = "environment";
                    se.variant = "alarm";
                    sounds::sound( se );
                    if( !g->timed_events.queued( TIMED_EVENT_WANTED ) ) {
                        g->timed_events.add(
                            TIMED_EVENT_WANTED, calendar::turn + 30_minutes, 0, p->abs_sm_pos() );
                    }
                }
                return it->type->charges_to_use();
            }
        }
        p->add_msg_if_player( pry->fail_message );
    }
    return it->type->charges_to_use();
}

int iuse::makemound( player* p, item* it, bool t, const tripoint_bub_ms & )
{
    if( !p || t ) { return 0; }
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    const std::optional<tripoint_bub_ms> pnt_ = choose_adjacent( _( "Till soil where?" ) );
    if( !pnt_ ) { return 0; }
    const auto pnt = *pnt_;

    if( pnt == p->bub_pos() ) {
        p->add_msg_if_player( m_info, _( "You think about jumping on a shovel, but then change up "
                                         "your mind." ) );
        return 0;
    }

    if( g->m.has_flag( flag_PLOWABLE, pnt ) && !g->m.has_flag( flag_PLANT, pnt ) ) {
        p->add_msg_if_player( _( "You start churning up the earth here." ) );
        p->assign_activity( std::make_unique<player_activity>(
                                std::make_unique<churn_activity_actor>( g->m.bub_to_abs( pnt ) ) ) );
        return it->type->charges_to_use();
    } else {
        p->add_msg_if_player( _( "You can't churn up this ground." ) );
        return 0;
    }
}

struct digging_moves_and_byproducts {
    int moves;
    std::string byproducts_item_group;
    ter_id result_terrain;
};

static digging_moves_and_byproducts dig_pit_moves_and_byproducts(
    player* p, item* it, const tripoint_bub_ms& pos, const bool channel )
{
    // Vastly simplified version of DDA's version, which had a 77-line-long explanation.
    //
    // Here, we simply set a target base time to dig, 60 minutes to dig a shallow pit,
    // 120 minutes for a deep pit. This is meant to be more in line with woodcutting,
    // mining, and other activities instead. Crafting quality is used to divide this like
    // we do with woodcutting, so deep pit is balanced around the minimum permitted quality
    // of 2 cutting that base time in half.
    //
    // We also must tone down the yield of dirt to avoid potential problems,
    // the old math was generating more than the tile volume limit.
    //
    // So to keep it simple, 200 liters for shallow pits, 400 for deep pit. We're basically
    // assuming that the first step is about one-third of the total work.

    // Get the dig quality of the tool.
    const int quality = it->get_quality( qual_DIG );

    ///\EFFECT_STR modifies dig rate
    // Adjust the dig rate if the player is above or below strength of 10.
    // Floor it at 1 so we don't divide by zero, of course!
    const double attr = 10.0 / std::max( 1, p->str_cur );

    // And now determine the moves...
    int dig_minutes = channel ? 60 : g->m.ter( pos )->digging_results.num_minutes;
    int moves = to_moves<int>(
                    std::max( 10_minutes, time_duration::from_minutes( dig_minutes * attr ) / quality ) );
    // Channel can be assumed to always be moving water because it doesn't create magic terraforming
    // in theory.
    ter_id result_terrain =
        channel ? ter_id( "t_water_moving_sh" ) : g->m.ter( pos )->digging_results.result_ter;

    return {moves, g->m.ter( pos )->digging_results.result_items.str(), result_terrain};
}

int iuse::dig( player* p, item* it, bool t, const tripoint_bub_ms & )
{
    if( !p || t ) { return 0; }
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    const auto dig_point = p->bub_pos();

    const bool can_dig_here =
        g->m.ter( dig_point )->is_diggable() && !g->m.has_furn( dig_point )
        && g->m.tr_at( dig_point ).is_null()
        && ( g->m.ter( dig_point ) == t_grave_new || g->m.i_at( dig_point ).empty() )
        && !g->m.veh_at( dig_point );

    if( !can_dig_here ) {
        p->add_msg_if_player( _( "You can't dig a pit in this location.  Ensure it is clear diggable "
                                 "ground with no items or obstacles." ) );
        return 0;
    }
    const bool grave = g->m.ter( dig_point ) == t_grave;

    if( !( p->crafting_inventory().max_quality( qual_DIG )
           >= g->m.ter( dig_point )->digging_results.dig_min ) ) {
        if( grave ) {
            p->add_msg_if_player( _( "You can't exhume a grave without a better digging tool." ) );
            return 0;
        } else {
            p->add_msg_if_player( _( "You don't have a good enough digging tool to dig there!" ) );
            return 0;
        }
    }

    const std::function<bool( const tripoint_bub_ms & )> f = []( const tripoint_bub_ms & pnt ) {
        return g->m.passable( pnt );
    };

    const std::optional<tripoint_bub_ms> pnt_ = choose_adjacent_highlight(
            _( "Deposit excavated materials where?" ),
            _( "There is nowhere to deposit the excavated materials." ), f, false );
    if( !pnt_ ) { return 0; }
    const auto deposit_point = *pnt_;

    if( !f( deposit_point ) ) {
        p->add_msg_if_player( _( "You can't deposit the excavated materials onto an impassable "
                                 "location." ) );
        return 0;
    }

    if( grave ) {
        if( g->u.has_trait( trait_SPIRITUAL ) && !g->u.has_trait( trait_PSYCHOPATH )
            && g->u.query_yn( _( "Would you really touch the sacred resting place of the dead?" ) ) ) {
            add_msg( m_info, _( "Exhuming a grave is really against your beliefs." ) );
            g->u.add_morale( MORALE_GRAVEDIGGER, -50, -100, 48_hours, 12_hours );
            if( one_in( 3 ) ) { g->u.vomit(); }
        } else if( g->u.has_trait( trait_PSYCHOPATH ) ) {
            p->add_msg_if_player( m_good, _( "Exhuming a grave is fun now, where there is no one to "
                                             "object." ) );
            g->u.add_morale( MORALE_GRAVEDIGGER, 25, 50, 2_hours, 1_hours );
        } else if( !g->u.has_trait( trait_EATDEAD ) && !g->u.has_trait( trait_SAPROVORE ) ) {
            p->add_msg_if_player( m_bad, _( "Exhuming this grave is utterly disgusting!" ) );
            g->u.add_morale( MORALE_GRAVEDIGGER, -25, -50, 2_hours, 1_hours );
            if( one_in( 5 ) ) { p->vomit(); }
        }
    }

    digging_moves_and_byproducts moves_and_byproducts =
        dig_pit_moves_and_byproducts( p, it, dig_point, false );

    const std::vector<npc *> helpers = character_funcs::get_crafting_helpers( *p, 3 );
    for( const npc * np : helpers ) { add_msg( m_info, _( "%s helps with this task…" ), np->name ); }
    moves_and_byproducts.moves = moves_and_byproducts.moves * ( 10 - helpers.size() ) / 10;

    p->assign_activity( std::make_unique<player_activity>( std::make_unique<dig_activity_actor>(
                            moves_and_byproducts.moves, dig_point, moves_and_byproducts.result_terrain.id().str(),
                            deposit_point, moves_and_byproducts.byproducts_item_group ) ) );

    return it->type->charges_to_use();
}

int iuse::dig_channel( player* p, item* it, bool t, const tripoint_bub_ms & )
{
    if( !p || t ) { return 0; }
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    const auto dig_point = p->bub_pos();

    auto north = dig_point + point_north;
    auto south = dig_point + point_south;
    auto west = dig_point + point_west;
    auto east = dig_point + point_east;

    const bool can_dig_here =
        g->m.ter( dig_point )->is_diggable() && !g->m.has_furn( dig_point )
        && g->m.tr_at( dig_point ).is_null() && g->m.i_at( dig_point ).empty()
        && !g->m.veh_at( dig_point )
        && ( g->m.has_flag( flag_CURRENT, north ) || g->m.has_flag( flag_CURRENT, south )
             || g->m.has_flag( flag_CURRENT, east ) || g->m.has_flag( flag_CURRENT, west ) );

    if( !can_dig_here ) {
        p->add_msg_if_player( _( "You can't dig a channel in this location.  Ensure it is clear "
                                 "diggable ground with no items or obstacles, adjacent to flowing "
                                 "water." ) );
        return 0;
    }

    const std::function<bool( const tripoint_bub_ms & )> f = []( const tripoint_bub_ms & pnt ) {
        return g->m.passable( pnt );
    };

    const std::optional<tripoint_bub_ms> pnt_ = choose_adjacent_highlight(
            _( "Deposit excavated materials where?" ),
            _( "There is nowhere to deposit the excavated materials." ), f, false );
    if( !pnt_ ) { return 0; }
    const auto deposit_point = *pnt_;

    if( !f( deposit_point ) ) {
        p->add_msg_if_player( _( "You can't deposit the excavated materials onto an impassable "
                                 "location." ) );
        return 0;
    }

    digging_moves_and_byproducts moves_and_byproducts =
        dig_pit_moves_and_byproducts( p, it, dig_point, true );

    const std::vector<npc *> helpers = character_funcs::get_crafting_helpers( *p, 3 );
    for( const npc * np : helpers ) { add_msg( m_info, _( "%s helps with this task…" ), np->name ); }
    moves_and_byproducts.moves = moves_and_byproducts.moves * ( 10 - helpers.size() ) / 10;

    p->assign_activity(
        std::make_unique<player_activity>( std::make_unique<dig_channel_activity_actor>(
                moves_and_byproducts.moves, dig_point, moves_and_byproducts.result_terrain.id().str(),
                deposit_point, moves_and_byproducts.byproducts_item_group ) ) );
    return it->type->charges_to_use();
}

int iuse::fill_pit( player* p, item* it, bool t, const tripoint_bub_ms & )
{
    if( !p || t ) { return 0; }
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }

    const std::function<bool( const tripoint_bub_ms & )> f = []( const tripoint_bub_ms & pnt ) {
        if( pnt == g->u.bub_pos() ) { return false; }
        const ter_id type = g->m.ter( pnt );
        return ( type->fill_result != ter_str_id::NULL_ID() );
    };

    const std::optional<tripoint_bub_ms> pnt_ = choose_adjacent_highlight(
            _( "Fill which pit or mound?" ), _( "There is no pit or mound to fill nearby." ), f, false );
    if( !pnt_ ) { return 0; }
    const tripoint_bub_ms& pnt = *pnt_;
    const ter_id ter = g->m.ter( pnt );
    if( !f( pnt ) ) {
        if( pnt == p->bub_pos() ) {
            p->add_msg_if_player( m_info, _( "You decide not to bury yourself that early." ) );
        } else {
            p->add_msg_if_player( m_info, _( "There is nothing to fill." ) );
        }
        return 0;
    }

    int moves = to_moves<int>( time_duration::from_minutes( ter->fill_minutes ) );

    const std::vector<npc *> helpers = character_funcs::get_crafting_helpers( *p, 3 );
    for( const npc * np : helpers ) { add_msg( m_info, _( "%s helps with this task…" ), np->name ); }
    moves = moves * ( 10 - helpers.size() ) / 10;

    p->assign_activity( std::make_unique<player_activity>(
                            std::make_unique<fill_pit_activity_actor>( bub_to_abs( pnt ), safe_reference<item>( *it ) ) ) );

    return it->type->charges_to_use();
}

/**
 * Explanation of ACT_CLEAR_RUBBLE activity values:
 *
 * coords[0]: Where the rubble is.
 * index: The bonus, for calculating hunger and thirst penalties.
 */

int iuse::clear_rubble( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    const std::function<bool( const tripoint_bub_ms & )> f = []( const tripoint_bub_ms & pnt ) {
        return g->m.has_flag( "RUBBLE", pnt );
    };

    const std::optional<tripoint_bub_ms> pnt_ = choose_adjacent_highlight(
            _( "Clear rubble where?" ), _( "There is no rubble to clear nearby." ), f, false );
    if( !pnt_ ) { return 0; }
    const tripoint_bub_ms& pnt = *pnt_;
    if( !f( pnt ) ) {
        p->add_msg_if_player( m_bad, _( "There's no rubble to clear." ) );
        return 0;
    }

    int moves = to_moves<int>( 30_seconds );
    int bonus = std::max( it->get_quality( quality_id( "DIG" ) ) - 1, 1 );

    const std::vector<npc *> helpers = character_funcs::get_crafting_helpers( *p, 3 );
    for( const npc * np : helpers ) { add_msg( m_info, _( "%s helps with this task…" ), np->name ); }
    moves = moves * ( 10 - helpers.size() ) / 10;

    p->assign_activity( std::make_unique<player_activity>(
                            std::make_unique<clear_rubble_activity_actor>( bub_to_abs( pnt ) ) ) );
    return it->type->charges_to_use();
}

void act_vehicle_siphon( vehicle* ); // veh_interact.cpp

int iuse::siphon( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    const std::function<bool( const tripoint_bub_ms & )> f = []( const tripoint_bub_ms & pnt ) {
        const optional_vpart_position vp = g->m.veh_at( pnt );
        return !!vp;
    };

    vehicle* v = nullptr;
    bool found_more_than_one = false;
    for( const tripoint_bub_ms& pos : g->m.points_in_radius( g->u.bub_pos(), 1 ) ) {
        const optional_vpart_position vp = g->m.veh_at( pos );
        if( !vp ) { continue; }
        vehicle* vfound = &vp->vehicle();
        if( v == nullptr ) {
            v = vfound;
        } else {
            // found more than one vehicle?
            if( v != vfound ) {
                v = nullptr;
                found_more_than_one = true;
                break;
            }
        }
    }
    if( found_more_than_one ) {
        std::optional<tripoint_bub_ms> pnt_ = choose_adjacent_highlight(
                _( "Siphon from where?" ), _( "There is nothing to siphon nearby." ), f, false );
        if( !pnt_ ) { return 0; }
        const optional_vpart_position vp = g->m.veh_at( *pnt_ );
        if( vp ) { v = &vp->vehicle(); }
    }

    if( v == nullptr ) {
        p->add_msg_if_player( m_info, _( "There's no vehicle there." ) );
        return 0;
    }
    act_vehicle_siphon( v );
    return it->type->charges_to_use();
}

int iuse::jackhammer( player* p, item* it, bool, const tripoint_bub_ms& pos )
{
    // use has_enough_charges to check for UPS availability
    // p is assumed to exist for iuse cases
    if( !p->has_enough_charges( *it, false ) ) { return 0; }
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    if( p->is_underwater() ) {
        p->add_msg_if_player( m_info, _( "You can't do that while underwater." ) );
        return 0;
    }

    auto pnt = pos;
    if( pos == p->bub_pos() ) {
        const std::optional<tripoint_bub_ms> pnt_ = choose_adjacent( _( "Drill where?" ) );
        if( !pnt_ ) { return 0; }
        pnt = *pnt_;
    }

    if( !g->m.has_flag( "MINEABLE", pnt ) ) {
        p->add_msg_if_player( m_info, _( "You can't drill there." ) );
        return 0;
    }
    if( g->m.veh_at( pnt ) ) {
        p->add_msg_if_player( _( "There's a vehicle in the way!" ) );
        return 0;
    }

    // Base time of 30 minutes at 8 strength
    int moves = to_moves<int>( 10_minutes );
    moves += ( 24 - std::min( p->str_cur, 24 ) ) * to_moves<int>( 75_seconds );
    if( g->m.move_cost( pnt ) == 2 ) {
        // We're breaking up some flat surface like pavement, which is much easier
        moves /= 2;
    }

    const std::vector<npc *> helpers = character_funcs::get_crafting_helpers( *p, 3 );
    for( const npc * np : helpers ) { add_msg( m_info, _( "%s helps with this task…" ), np->name ); }
    moves = moves * ( 10 - helpers.size() ) / 10;

    p->assign_activity( std::make_unique<player_activity>(
                            std::make_unique <
                            jackhammer_activity_actor > ( g->m.bub_to_abs( pnt ), safe_reference<item>( *it ) ) ) );
    p->add_msg_if_player(
        _( "You start drilling into the %1$s with your %2$s." ), g->m.tername( pnt ), it->tname() );

    return it->type->charges_to_use();
}

int iuse::pick_lock( player* p, item* it, bool, const tripoint_bub_ms& pos )
{
    if( p->is_npc() ) { return 0; }
    avatar& you = dynamic_cast<avatar &>( *p );

    std::optional<tripoint_bub_ms> target;
    // Prompt for a target lock to pick, or use the given tripoint
    if( pos == you.bub_pos() ) {
        target = lockpick_activity_actor::select_location( you );
    } else {
        target = pos;
    }
    if( !target.has_value() ) { return 0; }

    int qual = it->get_quality( qual_LOCKPICK );

    /** @EFFECT_DEX speeds up door lock picking */
    /** @EFFECT_MECHANICS speeds up door lock picking */
    int duration;
    if( it->has_flag( flag_PERFECT_LOCKPICK ) ) {
        duration = to_moves<int>( 5_seconds );
    } else {
        duration = std::max(
                       to_moves<int>( 10_seconds ),
                       to_moves<int>( 10_minutes - time_duration::from_minutes( qual ) )
                       - ( you.dex_cur + you.get_skill_level( skill_mechanics ) ) * 2300 );
    }

    you.assign_activity( std::make_unique<player_activity>(
                             lockpick_activity_actor::use_item( duration, *it, g->m.bub_to_abs( *target ) ) ) );
    return it->type->charges_to_use();
}

int iuse::pickaxe( player* p, item* it, bool, const tripoint_bub_ms& pos )
{
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

    auto pnt = pos;
    if( pos == p->bub_pos() ) {
        const std::optional<tripoint_bub_ms> pnt_ = choose_adjacent( _( "Mine where?" ) );
        if( !pnt_ ) { return 0; }
        pnt = *pnt_;
    }

    if( !g->m.has_flag( "MINEABLE", pnt ) ) {
        p->add_msg_if_player( m_info, _( "You can't mine there." ) );
        return 0;
    }
    if( g->m.veh_at( pnt ) ) {
        p->add_msg_if_player( _( "There's a vehicle in the way!" ) );
        return 0;
    }

    // Base time of 90 minutes at 8 strength
    int moves = to_moves<int>( 30_minutes );
    moves += ( 24 - std::min( p->str_cur, 24 ) ) * to_moves<int>( 225_seconds );
    if( g->m.move_cost( pnt ) == 2 ) {
        // We're breaking up some flat surface like pavement, which is much easier
        moves /= 2;
    }

    const std::vector<npc *> helpers = character_funcs::get_crafting_helpers( *p, 3 );
    for( const npc * np : helpers ) { add_msg( m_info, _( "%s helps with this task…" ), np->name ); }
    moves = moves * ( 10 - helpers.size() ) / 10;

    p->assign_activity( std::make_unique<player_activity>(
                            std::make_unique<pickaxe_activity_actor>( g->m.bub_to_abs( pnt ), safe_reference<item>( *it ) ) ) );
    p->add_msg_if_player( _( "You strike the %1$s with your %2$s." ), g->m.tername( pnt ),
                          it->tname() );
    return 0; // handled when the activity finishes
}

int iuse::burrow( player* p, item* it, bool, const tripoint_bub_ms& pos )
{
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

    auto pnt = pos;
    if( pos == p->bub_pos() ) {
        const std::optional<tripoint_bub_ms> pnt_ = choose_adjacent( _( "Burrow where?" ) );
        if( !pnt_ ) { return 0; }
        pnt = *pnt_;
    }

    if( !g->m.has_flag( "MINEABLE", pnt ) ) {
        p->add_msg_if_player( m_info, _( "You can't burrow there." ) );
        return 0;
    }
    if( g->m.veh_at( pnt ) ) {
        p->add_msg_if_player( _( "There's a vehicle in the way!" ) );
        return 0;
    }

    // Base time of 60 minutes at 8 strength
    int moves = to_moves<int>( 20_minutes );
    moves += ( 24 - std::min( p->str_cur, 24 ) ) * to_moves<int>( 150_seconds );
    if( g->m.move_cost( pnt ) == 2 ) {
        // We're breaking up some flat surface like pavement, which is much easier
        moves /= 2;
    }

    // For consistency, makes as much sense as NPCs helping you mine faster when you're the only one
    // with the tool
    const std::vector<npc *> helpers = character_funcs::get_crafting_helpers( *p, 3 );
    for( const npc * np : helpers ) { add_msg( m_info, _( "%s helps with this task…" ), np->name ); }
    moves = moves * ( 10 - helpers.size() ) / 10;

    p->assign_activity( std::make_unique<player_activity>(
                            std::make_unique<burrow_activity_actor>( bub_to_abs( pnt ) ) ) );
    p->add_msg_if_player(
        _( "You start tearing into the %1$s with your %2$s." ), g->m.tername( pnt ), it->tname() );
    return 0; // handled when the activity finishes
}

int iuse::geiger( player* p, item* it, bool t, const tripoint_bub_ms& pos )
{
    if( t ) { // Every-turn use when it's on
        const int rads = g->m.get_radiation( pos );
        if( rads == 0 ) { return it->type->charges_to_use(); }
        std::string description =
            rads > 50   ? _( "buzzing" )
            : rads > 25 ? _( "rapid clicking" )
            : _( "clicking" );
        std::string sound_var =
            rads > 50   ? _( "geiger_high" )
            : rads > 25 ? _( "geiger_medium" )
            : _( "geiger_low" );

        sound_event se;
        se.origin = pos;
        se.volume = 50;
        se.category = sounds::sound_t::alarm;
        se.description = description;
        se.id = "tool";
        se.variant = sound_var;
        sounds::sound( se );
        if( !p->can_hear( pos, se.volume ) ) {
            // can not hear it, but may have alarmed other creatures
            return it->type->charges_to_use();
        }
        if( rads > 50 ) {
            add_msg( m_warning, _( "The geiger counter buzzes intensely." ) );
        } else if( rads > 35 ) {
            add_msg( m_warning, _( "The geiger counter clicks wildly." ) );
        } else if( rads > 25 ) {
            add_msg( m_warning, _( "The geiger counter clicks rapidly." ) );
        } else if( rads > 15 ) {
            add_msg( m_warning, _( "The geiger counter clicks steadily." ) );
        } else if( rads > 8 ) {
            add_msg( m_warning, _( "The geiger counter clicks slowly." ) );
        } else if( rads > 4 ) {
            add_msg( _( "The geiger counter clicks intermittently." ) );
        } else {
            add_msg( _( "The geiger counter clicks once." ) );
        }
        return it->type->charges_to_use();
    }
    // Otherwise, we're activating the geiger counter
    if( it->typeId() == itype_geiger_on ) {
        add_msg( _( "The geiger counter's SCANNING LED turns off." ) );
        it->convert( itype_geiger_off );
        it->deactivate();
        return 0;
    }

    int ch = uilist(
                 _( "Geiger counter:" ),
    {_( "Scan yourself or other person" ), _( "Scan the ground" ), _( "Turn continuous scan on" )} );
    switch( ch ) {
        case 0: {
            const std::function<bool( const tripoint_bub_ms & )> f = [&]( const tripoint_bub_ms & pnt ) {
                return g->critter_at<npc>( pnt ) != nullptr || g->critter_at<player>( pnt ) != nullptr;
            };

            const std::optional<tripoint_bub_ms> pnt_ = choose_adjacent_highlight(
                    _( "Scan whom?" ), _( "There is no one to scan nearby." ), f, false );
            if( !pnt_ ) { return 0; }
            const tripoint_bub_ms& pnt = *pnt_;
            if( pnt == g->u.bub_pos() ) {
                p->add_msg_if_player(
                    m_info, _( "Your radiation level: %d mSv (%d mSv from items)" ), p->get_rad(),
                    p->leak_level( flag_RADIOACTIVE ) );
                break;
            }
            if( npc * const person_ = g->critter_at<npc>( pnt ) ) {
                npc& person = *person_;
                p->add_msg_if_player(
                    m_info, _( "%s's radiation level: %d mSv (%d mSv from items)" ), person.name,
                    person.get_rad(), person.leak_level( flag_RADIOACTIVE ) );
            }
            break;
        }
        case 1:
            p->add_msg_if_player(
                m_info, _( "The ground's radiation level: %d mSv/h" ),
                g->m.get_radiation( p->bub_pos() ) );
            break;
        case 2:
            p->add_msg_if_player( _( "The geiger counter's scan LED turns on." ) );
            it->convert( itype_geiger_on );
            it->activate();
            break;
        default:
            return 0;
    }
    p->mod_moves( -100 );

    return it->type->charges_to_use();
}
