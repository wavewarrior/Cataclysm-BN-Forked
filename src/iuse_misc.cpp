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

        sound_event se;
        switch( used ) {
            case AEA_STORM: {
                se.origin = p->bub_pos();
                se.volume = 160;
                se.category = sounds::sound_t::combat;
                se.description = _( "Ka-BOOM!" );
                se.id = "environment";
                se.variant = "thunder_near";
                sounds::sound( se );
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
                se.origin = p->bub_pos();
                se.volume = 80;
                se.category = sounds::sound_t::combat;
                se.description = _( "The earth shakes!" );
                se.id = "misc";
                se.variant = "earthquake";
                sounds::sound( se );
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
                se.origin = p->bub_pos();
                se.volume = 135;
                se.category = sounds::sound_t::combat;
                se.description = string_format( _( "a deafening boom from %s %s" ),
                                                p->disp_name( true ), it->tname() );
                se.id = "misc";
                se.variant = "shockwave";
                sounds::sound( se );
                break;

            case AEA_SCREAM:
                se.origin = p->bub_pos();
                se.volume = 100;
                se.category = sounds::sound_t::alert;
                se.description = string_format( _( "a disturbing scream from %s %s" ),
                                                p->disp_name( true ), it->tname() );
                se.id = "shout";
                se.variant = "scream";
                sounds::sound( se );
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

    sound_event se;
    se.origin = p->bub_pos();
    se.volume = speech.volume;
    se.category = sounds::sound_t::electronic_speech;
    se.description = speech.text.translated();
    se.id = "speech";
    se.variant = it->typeId().str();
    sounds::sound( se );

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
    sound_event se;
    se.origin = p->bub_pos();
    se.category = sounds::sound_t::activity;
    se.description = _( "crunch" );
    se.id = "tool";
    se.variant = "repair_kit";
    if( fix.damage() <= 0 ) {
        se.volume = 50;
        sounds::sound( se );
        p->moves -= to_moves<int>( 20_seconds * vision_mod );
        p->practice( skill_mechanics, 10 );
        fix.mod_damage( -itype::damage_scale );
        p->add_msg_if_player( m_good, _( "You accurize your %s." ), fix.tname( 1, false ) );

    } else if( fix.damage() > itype::damage_scale ) {
        se.volume = 60;
        sounds::sound( se );
        p->moves -= to_moves<int>( 10_seconds * vision_mod );
        p->practice( skill_mechanics, 10 );
        fix.mod_damage( -itype::damage_scale );
        resultdurability = fix.durability_indicator( true );
        p->add_msg_if_player(
            m_good, _( "You repair your %s!  ( %s-> %s)" ), fix.tname( 1, false ), startdurability,
            resultdurability );

    } else {
        se.volume = 60;
        sounds::sound( se );
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
        sound_event se;
        se.origin = p->bub_pos();
        se.volume = 70;
        se.category = sounds::sound_t::music;
        se.description = _( "Clank!  Clank!" );
        se.id = "misc";
        se.variant = "cow_bell";
        sounds::sound( se );
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
        sound_event se;
        se.origin = p->bub_pos();
        se.volume = 40;
        se.category = sounds::sound_t::music;
        se.description = _( "Ring!  Ring!" );
        se.id = "misc";
        se.variant = "bell";
        sounds::sound( se );
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

