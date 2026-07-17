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

