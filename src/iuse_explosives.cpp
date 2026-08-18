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


static void do_purify( player& p )
{
    std::vector<trait_id> valid;
    mutation_category_id thresh =
        p.thresh_category != mutation_category_id::NULL_ID()
        ? p.thresh_category
        : p.get_highest_category();
    for( auto& traits_iter : mutation_branch::get_all() ) {
        if( p.has_trait( traits_iter.id )
            && ( !p.has_base_trait( traits_iter.id ) || get_option<bool>( "canmutprofmut" ) ) ) {
            bool threshlocked = false;
            for( auto cat : traits_iter.category ) {
                if( ( cat == thresh ) && p.crossed_threshold()
                    && ( p.thresh_tier > traits_iter.threshold_tier ) ) {
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

int iuse::can_goo( player* p, item* it, bool, const tripoint_bub_ms & )
{
    int tries = 0;
    tripoint_bub_ms goop;
    goop.z() = p->bub_pos().z();
    do {
        goop.x() = p->bub_pos().x() + rng( -2, 2 );
        goop.y() = p->bub_pos().y() + rng( -2, 2 );
        tries++;
    } while( g->m.impassable( goop ) && tries < 10 );
    if( tries == 10 ) {
        add_msg( _( "Nothing happens." ) );
        return 0;
    }
    if( monster * const mon_ptr = g->critter_at<monster>( goop ) ) {
        monster& critter = *mon_ptr;
        if( g->u.sees( goop ) ) {
            add_msg( _( "Black goo emerges from the canister and envelopes a %s!" ), critter.name() );
        }
        critter.poly( mon_blob );

        critter.set_speed_base( critter.get_speed_base() - rng( 5, 25 ) );
        critter.set_hp( critter.get_speed() );
    } else {
        if( g->u.sees( goop ) ) { add_msg( _( "Living black goo emerges from the canister!" ) ); }
        if( monster * const goo = g->place_critter_at( mon_blob, goop ) ) { goo->friendly = -1; }
    }
    if( x_in_y( 3.0, 4.0 ) ) {
        tries = 0;
        bool found = false;
        do {
            goop.x() = p->bub_pos().x() + rng( -2, 2 );
            goop.y() = p->bub_pos().y() + rng( -2, 2 );
            tries++;
            found = g->m.passable( goop ) && g->m.tr_at( goop ).is_null();
        } while( !found && tries < 10 );
        if( found ) {
            if( g->u.sees( goop ) ) {
                add_msg( m_warning, _( "A nearby splatter of goo forms into a goo pit." ) );
            }
            g->m.trap_set( goop, tr_goo );
        }
    }
    if( it->charges <= it->type->charges_to_use() ) {
        it->charges = 0;
        it->convert( itype_canister_empty );
        return 0;
    }
    return it->type->charges_to_use();
}

int iuse::throwable_extinguisher_act( player *, item *it, bool, const tripoint_bub_ms &pos )
{
    if( pos.x() == -999 || pos.y() == -999 ) {
        return 0;
    }
    if( g->m.get_field( pos, fd_fire ) != nullptr ) {
        sound_event se;
        se.origin = pos;
        se.volume = 90;
        se.category = sounds::sound_t::combat;
        se.description = _( "Bang!" );
        se.id = "explosion";
        se.variant = "small";
        sounds::sound( se );
        // Reduce the strength of fire (if any) in the target tile.
        g->m.mod_field_intensity( pos, fd_fire, 0 - 2 );
        // Slightly reduce the strength of fire around and in the target tile.
        for( const tripoint_bub_ms &dest : g->m.points_in_radius( pos, 1 ) ) {
            if( g->m.passable( dest ) && dest != pos ) {
                g->m.mod_field_intensity( dest, fd_fire, 0 - rng( 0, 2 ) );
            }
        }
        return 1;
    }
    it->deactivate();
    return 0;
}

/// Apply a debug grenade skill buff or nerf to a random valid skill.
enum class debug_grenade_skill_modifier_type { buff, nerf };
static auto apply_debug_grenade_skill_modifier(
    Character& ch, const debug_grenade_skill_modifier_type mode ) -> void
{
    const skill_id skill = Skill::random_skill();
    if( !skill ) { return; }
    const bool buff = ( mode == debug_grenade_skill_modifier_type::buff );
    const int current_level = ch.get_skill_level( skill );
    const int cap = buff ? MAX_SKILL - current_level : current_level;
    if( cap <= 0 ) { return; }
    const int change = 1;
    ch.mod_skill_level( skill, buff ? change : -change );
}

int iuse::debug_grenade( player* p, item* it, bool, const tripoint_bub_ms & )
{
    p->add_msg_if_player( _( "You pull the pin on the %s." ), it->tname() );
    it->convert( itype_debug_grenade_act );
    it->charges = 5;
    it->activate();
    return it->type->charges_to_use();
}

int iuse::debug_grenade_act( player* p, item* it, bool t, const tripoint_bub_ms& pos )
{
    if( pos.x() == -999 || pos.y() == -999 ) { return 0; }
    if( t ) { // Simple timer effects
        add_msg( m_info, _( "\"Merged!\"" ) );
    } else if( it->charges > 0 ) {
        p->add_msg_if_player(
            m_info, _( "You've already pulled the %s's pin, try throwing it instead." ), it->tname() );
        return 0;
    }

    if( it->charges == 0 ) { // When that timer runs down...
        int explosion_radius = 3;
        int effect_roll = rng( 1, 6 );
        auto buff_stat = [&]( int &current_stat, int modify_by ) {
            const auto modified_stat = current_stat + modify_by;
            current_stat = std::max( current_stat, modified_stat );
        };
        switch( effect_roll ) {
            case 1:
                add_msg( m_info, _( "\"BUGFIXES!\"" ) );
                explosion_handler::draw_explosion( pos, explosion_radius, c_light_cyan, "explosion" );
                for( const tripoint_bub_ms& dest : g->m.points_in_radius( pos, explosion_radius ) ) {
                    monster* const mon = g->critter_at<monster>( dest, true );
                    if( mon && ( mon->type->in_species( INSECT ) || mon->is_hallucination() ) ) {
                        mon->die_in_explosion( nullptr );
                    }
                }
                break;

            case 2:
                add_msg( m_info, _( "\"BUFFS!\"" ) );
                explosion_handler::draw_explosion( pos, explosion_radius, c_green, "explosion" );
                for( const tripoint_bub_ms& dest : g->m.points_in_radius( pos, explosion_radius ) ) {
                    if( monster * const mon_ptr = g->critter_at<monster>( dest ) ) {
                        monster& critter = *mon_ptr;
                        critter.set_speed_base( critter.get_speed_base() * rng_float( 1.1, 2.0 ) );
                        critter.set_hp( critter.get_hp() * rng_float( 1.1, 2.0 ) );
                    } else if( npc * const person = g->critter_at<npc>( dest ) ) {
                        /** @EFFECT_STR_MAX increases possible str buff for NPCs */
                        buff_stat( person->str_max, rng( 0, person->str_max / 2 ) );
                        /** @EFFECT_DEX_MAX increases possible dex buff for NPCs */
                        buff_stat( person->dex_max, rng( 0, person->dex_max / 2 ) );
                        /** @EFFECT_INT_MAX increases possible int buff for NPCs */
                        buff_stat( person->int_max, rng( 0, person->int_max / 2 ) );
                        /** @EFFECT_PER_MAX increases possible per buff for NPCs */
                        buff_stat( person->per_max, rng( 0, person->per_max / 2 ) );
                        apply_debug_grenade_skill_modifier(
                            *person, debug_grenade_skill_modifier_type::buff );
                    } else if( g->u.bub_pos() == dest ) {
                        /** @EFFECT_STR_MAX increases possible str buff */
                        buff_stat( g->u.str_max, rng( 0, g->u.str_max / 2 ) );
                        /** @EFFECT_DEX_MAX increases possible dex buff */
                        buff_stat( g->u.dex_max, rng( 0, g->u.dex_max / 2 ) );
                        /** @EFFECT_INT_MAX increases possible int buff */
                        buff_stat( g->u.int_max, rng( 0, g->u.int_max / 2 ) );
                        /** @EFFECT_PER_MAX increases possible per buff */
                        buff_stat( g->u.per_max, rng( 0, g->u.per_max / 2 ) );
                        g->u.recalc_hp();
                        for( const bodypart_id& bp : g->u.get_all_body_parts() ) {
                            g->u.set_part_hp_cur( bp, g->u.get_part_hp_cur( bp ) * rng_float( 1, 1.2 ) );
                            const int hp_max = g->u.get_part_hp_max( bp );
                            if( g->u.get_part_hp_cur( bp ) > hp_max ) {
                                g->u.set_part_hp_cur( bp, hp_max );
                            }
                        }
                        apply_debug_grenade_skill_modifier(
                            g->u, debug_grenade_skill_modifier_type::buff );
                    }
                }
                break;

            case 3:
                add_msg( m_info, _( "\"NERFS!\"" ) );
                explosion_handler::draw_explosion( pos, explosion_radius, c_red, "explosion" );
                for( const tripoint_bub_ms& dest : g->m.points_in_radius( pos, explosion_radius ) ) {
                    if( monster * const mon_ptr = g->critter_at<monster>( dest ) ) {
                        monster& critter = *mon_ptr;
                        critter.set_speed_base( rng( 0, critter.get_speed_base() ) );
                        critter.set_hp( rng( 1, critter.get_hp() ) );
                    } else if( npc * const person = g->critter_at<npc>( dest ) ) {
                        /** @EFFECT_STR_MAX increases possible str debuff for NPCs (NEGATIVE) */
                        person->str_max -= rng( 0, person->str_max / 2 );
                        /** @EFFECT_DEX_MAX increases possible dex debuff for NPCs (NEGATIVE) */
                        person->dex_max -= rng( 0, person->dex_max / 2 );
                        /** @EFFECT_INT_MAX increases possible int debuff for NPCs (NEGATIVE) */
                        person->int_max -= rng( 0, person->int_max / 2 );
                        /** @EFFECT_PER_MAX increases possible per debuff for NPCs (NEGATIVE) */
                        person->per_max -= rng( 0, person->per_max / 2 );
                        apply_debug_grenade_skill_modifier(
                            *person, debug_grenade_skill_modifier_type::nerf );
                    } else if( g->u.bub_pos() == dest ) {
                        /** @EFFECT_STR_MAX increases possible str debuff (NEGATIVE) */
                        g->u.str_max -= rng( 0, g->u.str_max / 2 );
                        /** @EFFECT_DEX_MAX increases possible dex debuff (NEGATIVE) */
                        g->u.dex_max -= rng( 0, g->u.dex_max / 2 );
                        /** @EFFECT_INT_MAX increases possible int debuff (NEGATIVE) */
                        g->u.int_max -= rng( 0, g->u.int_max / 2 );
                        /** @EFFECT_PER_MAX increases possible per debuff (NEGATIVE) */
                        g->u.per_max -= rng( 0, g->u.per_max / 2 );
                        g->u.recalc_hp();
                        for( const bodypart_id& bp : g->u.get_all_body_parts() ) {
                            const int hp_cur = g->u.get_part_hp_cur( bp );
                            if( hp_cur > 0 ) { g->u.set_part_hp_cur( bp, rng( 1, hp_cur ) ); }
                        }
                        apply_debug_grenade_skill_modifier(
                            g->u, debug_grenade_skill_modifier_type::nerf );
                    }
                }
                break;

            case 4:
                add_msg( m_info, _( "\"REVERTS!\"" ) );
                explosion_handler::draw_explosion( pos, explosion_radius, c_pink, "explosion" );
                for( const tripoint_bub_ms& dest : g->m.points_in_radius( pos, explosion_radius ) ) {
                    if( monster * const mon_ptr = g->critter_at<monster>( dest ) ) {
                        monster& critter = *mon_ptr;
                        critter.set_speed_base( critter.type->speed );
                        critter.set_hp( critter.get_hp_max() );
                        critter.clear_effects();
                    } else if( npc * const person = g->critter_at<npc>( dest ) ) {
                        person->environmental_revert_effect();
                    } else if( g->u.bub_pos() == dest ) {
                        g->u.environmental_revert_effect();
                        do_purify( g->u );
                    }
                }
                break;
            case 5:
                add_msg( m_info, _( "\"QUACK!\"" ) );
                explosion_handler::draw_explosion( pos, explosion_radius, c_yellow, "explosion" );
                for( const tripoint_bub_ms& dest : g->m.points_in_radius( pos, explosion_radius ) ) {
                    if( one_in( 5 ) && !g->critter_at( dest ) ) {
                        g->place_critter_at( mon_duck, dest );
                        ;
                    }
                }
                break;
            case 6:
                add_msg( m_info, _( "\"EEPY!\"" ) );
                explosion_handler::draw_explosion( pos, explosion_radius, c_magenta, "explosion" );
                for( const tripoint_bub_ms& dest : g->m.points_in_radius( pos, explosion_radius ) ) {
                    if( npc * const person = g->critter_at<npc>( dest ) ) {
                        person->fall_asleep( 5_minutes );
                    } else if( g->u.bub_pos() == dest ) {
                        g->u.fall_asleep( 5_minutes );
                    }
                }
                break;
        }
    }
    return it->type->charges_to_use();
}

int iuse::c4( player* p, item* it, bool, const tripoint_bub_ms & )
{
    int time;
    bool got_value = query_int( time, _( "Set the timer to (0 to cancel)?" ) );
    if( !got_value || time <= 0 ) {
        p->add_msg_if_player( _( "Never mind." ) );
        return 0;
    }
    p->add_msg_if_player( _( "You set the timer to %d." ), time );
    it->convert( itype_c4armed );
    it->charges = time;
    it->activate();
    return it->type->charges_to_use();
}

int iuse::acidbomb_act( player* p, item* it, bool, const tripoint_bub_ms& pos )
{
    if( !p->has_item( *it ) ) {
        it->charges = -1;
        for( const tripoint_bub_ms& tmp :
             g->m.points_in_radius( pos.x() == -999 ? p->bub_pos() : pos, 1 ) ) {
            g->m.add_field( tmp, fd_acid, 3 );
        }
        return 1;
    }
    return 0;
}

int iuse::grenade_inc_act( player* p, item* it, bool t, const tripoint_bub_ms& pos )
{
    if( pos.x() == -999 || pos.y() == -999 ) { return 0; }


    if( t ) {
        // Simple timer effects
        // Vol 0 = only heard if you hold it
        sound_event se;
        se.origin = pos;
        se.volume = 40;
        se.category = sounds::sound_t::alarm;
        se.description = _( "Tick!" );
        se.id = "misc";
        se.variant = "bomb_ticking";
        sounds::sound( se );
    } else if( it->charges > 0 ) {
        p->add_msg_if_player( m_info, _( "You've already released the handle, try throwing it "
                                         "instead." ) );
        return 0;
    }

    if( it->charges == 0 ) { // blow up
        int num_flames = rng( 3, 5 );
        for( int current_flame = 0; current_flame < num_flames; current_flame++ ) {
            tripoint_bub_ms dest( pos + point( rng( -5, 5 ), rng( -5, 5 ) ) );
            std::vector<tripoint_bub_ms> flames = line_to( pos, dest, 0, 0 );
            for( auto& flame : flames ) { g->m.add_field( flame, fd_fire, rng( 0, 2 ) ); }
        }
        explosion_handler::explosion( pos, p, 8, 0.8, true );
        for( const tripoint_bub_ms& dest : g->m.points_in_radius( pos, 2 ) ) {
            g->m.add_field( dest, fd_incendiary, 3 );
        }

        if( p->has_trait( trait_PYROMANIA ) ) {
            p->add_morale( MORALE_PYROMANIA_STARTFIRE, 15, 15, 8_hours, 6_hours );
            p->rem_morale( MORALE_PYROMANIA_NOFIRE );
            p->add_msg_if_player( m_good, _( "Fire…  Good…" ) );
        }
        return 0;
    }
    return 0;
}

int iuse::arrow_flammable( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_underwater() ) {
        p->add_msg_if_player( m_info, _( "You can't do that while underwater." ) );
        return 0;
    }
    if( !p->use_charges_if_avail( itype_fire, 1 ) ) {
        p->add_msg_if_player( m_info, _( "You need a source of fire!" ) );
        return 0;
    }
    p->add_msg_if_player( _( "You light the arrow!" ) );
    p->moves -= to_moves<int>( 1_seconds );
    if( it->charges == 1 ) {
        it->convert( itype_arrow_flamming );
        return 0;
    }
    detached_ptr<item> lit_arrow = item::spawn( *it );
    lit_arrow->convert( itype_arrow_flamming );
    lit_arrow->charges = 1;
    p->i_add( std::move( lit_arrow ) );
    return 1;
}

int iuse::molotov_lit( player* p, item* it, bool t, const tripoint_bub_ms& pos )
{
    if( pos.x() == -999 || pos.y() == -999 ) {
        return 0;
    } else if( !t ) {
        if( p->has_item( *it ) ) {
            if( !query_yn( "Really smash it on yourself?" ) ) {
                p->add_msg_if_player( m_info, _( "You should probably throw it instead." ) );
                return 0;
            }
        }
        for( const tripoint_bub_ms& pt : g->m.points_in_radius( pos, 1, 0 ) ) {
            const int intensity = 1 + one_in( 3 ) + one_in( 5 );
            g->m.add_field( pt, fd_fire, intensity );
        }
        if( p->has_trait( trait_PYROMANIA ) ) {
            p->add_morale( MORALE_PYROMANIA_STARTFIRE, 15, 15, 8_hours, 6_hours );
            p->rem_morale( MORALE_PYROMANIA_NOFIRE );
            p->add_msg_if_player( m_good, _( "Fire…  Good…" ) );
        }
        // If you exploded it on yourself through activation.
        return 1;
    } else if( p->has_item( *it ) && it->charges == 0 ) {
        return 0;
    }
    return 0;
}

int iuse::firecracker_pack( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_underwater() ) {
        p->add_msg_if_player( m_info, _( "You can't do that while underwater." ) );
        return 0;
    }
    if( !p->has_charges( itype_fire, 1 ) ) {
        p->add_msg_if_player( m_info, _( "You need a source of fire!" ) );
        return 0;
    }
    p->add_msg_if_player( _( "You light the pack of firecrackers." ) );
    it->convert( itype_firecracker_pack_act );
    it->charges = 26;
    it->set_age( 0_turns );
    it->activate();
    return 0; // don't use any charges at all. it has became a new item
}

int iuse::firecracker_pack_act( player *, item *it, bool, const tripoint_bub_ms &pos )
{
    time_duration timer = it->age();
    if( timer < 2_turns ) {
        sound_event se;
        se.origin = pos;
        se.volume = 30;
        se.category = sounds::sound_t::alarm;
        se.description = _( "ssss…" );
        se.id = "misc";
        se.variant = "lit_fuse";
        sounds::sound( se );
        it->inc_damage();
    } else if( it->charges > 0 ) {
        int ex = rng( 4, 6 );
        int i = 0;
        if( ex > it->charges ) {
            ex = it->charges;
        }
        for( i = 0; i < ex; i++ ) {
            sound_event se;
            se.origin = pos;
            se.volume = 80;
            se.category = sounds::sound_t::combat;
            se.description = _( "Bang!" );
            se.id = "explosion";
            se.variant = "small";
            sounds::sound( se );
        }
        it->charges -= ex;
    }
    if( it->charges == 0 ) {
        it->charges = -1;
    }
    return 0;
}

int iuse::firecracker( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( p->is_underwater() ) {
        p->add_msg_if_player( m_info, _( "You can't do that while underwater." ) );
        return 0;
    }
    if( !p->use_charges_if_avail( itype_fire, 1 ) ) {
        p->add_msg_if_player( m_info, _( "You need a source of fire!" ) );
        return 0;
    }
    p->add_msg_if_player( _( "You light the firecracker." ) );
    it->convert( itype_firecracker_act );
    it->charges = 2;
    it->activate();
    return it->type->charges_to_use();
}

int iuse::firecracker_act( player *p, item *it, bool t, const tripoint_bub_ms &pos )
{
    if( pos.x() == -999 || pos.y() == -999 ) {
        return 0;
    }

    if( t ) { // Simple timer effects
        sound_event se;
        se.origin = pos;
        se.volume = 40;
        se.category = sounds::sound_t::alarm;
        se.description = _( "ssss…" );
        se.id = "misc";
        se.variant = "lit_fuse";
        sounds::sound( se );
    } else if( it->charges > 0 ) {
        p->add_msg_if_player( m_info, _( "You've already lit the %s, try throwing it instead." ),
                              it->tname() );
        return 0;
    }

    if( it->charges == 0 ) { // When that timer runs down...
        sound_event se;
        se.origin = pos;
        se.volume = 80;
        se.category = sounds::sound_t::combat;
        se.description = _( "Bang!" );
        se.id = "explosion";
        se.variant = "small";
        sounds::sound( se );
    }
    return 0;
}

int iuse::mininuke( player* p, item* it, bool, const tripoint_bub_ms & )
{
    int time;
    bool got_value = query_int( time, _( "Set the timer to ___ turns (0 to cancel)?" ) );
    if( !got_value || time <= 0 ) {
        p->add_msg_if_player( _( "Never mind." ) );
        return 0;
    }
    p->add_msg_if_player( _( "You set the timer to %s." ),
                          to_string( time_duration::from_turns( time ) ) );
    g->events().send<event_type::activates_mininuke>( p->getID() );
    it->convert( itype_mininuke_act );
    it->charges = time;
    it->activate();
    return it->type->charges_to_use();
}

int iuse::pheromone( player* p, item* it, bool, const tripoint_bub_ms& pos )
{
    if( !it->ammo_sufficient() ) { return 0; }
    if( p->is_underwater() ) {
        p->add_msg_if_player( m_info, _( "You can't do that while underwater." ) );
        return 0;
    }

    if( pos.x() == -999 || pos.y() == -999 ) { return 0; }

    p->add_msg_player_or_npc( _( "You squeeze the pheromone ball…" ), _( "<npcname> squeezes the "
                                 "pheromone ball…" ) );

    p->moves -= 15;

    int converts = 0;
    for( const tripoint_bub_ms& dest : g->m.points_in_radius( pos, 4 ) ) {
        monster* const mon_ptr = g->critter_at<monster>( dest, true );
        if( !mon_ptr ) { continue; }
        monster& critter = *mon_ptr;
        if( critter.type->in_species( ZOMBIE ) && critter.friendly == 0
            && rng( 0, 500 ) > critter.get_hp() ) {
            converts++;
            critter.anger = 0;
        }
    }

    if( g->u.sees( *p ) ) {
        if( converts == 0 ) {
            add_msg( _( "…but nothing happens." ) );
        } else if( converts == 1 ) {
            add_msg( m_good, _( "…and a nearby zombie becomes passive!" ) );
        } else {
            add_msg( m_good, _( "…and several nearby zombies become passive!" ) );
        }
    }
    return it->type->charges_to_use();
}

int iuse::portal( player* p, item* it, bool, const tripoint_bub_ms & )
{
    if( !it->ammo_sufficient() ) { return 0; }
    if( p->is_mounted() ) {
        p->add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    tripoint_bub_ms
    t( p->bub_pos().x() + rng( -2, 2 ), p->bub_pos().y() + rng( -2, 2 ), p->bub_pos().z() );
    g->m.trap_set( t, tr_portal );
    return it->type->charges_to_use();
}
