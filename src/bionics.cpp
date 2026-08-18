#include "bionics.h"

#include <algorithm> //std::min
#include <array>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <forward_list>
#include <iterator>
#include <list>
#include <memory>
#include <optional>
#include <type_traits>

#include "action.h"
#include "activity_actor_definitions.h"
#include "assign.h"
#include "avatar.h"
#include "avatar_action.h"
#include "ballistics.h"
#include "calendar.h"
#include "cata_utility.h"
#include "catalua_icallback_actor.h"
#include "character.h"
#include "character_martial_arts.h"
#include "character_stat.h"
#include "color.h"
#include "consistency_report.h"
#include "coordinates.h"
#include "cursesdef.h"
#include "damage.h"
#include "debug.h"
#include "dispersion.h"
#include "effect.h"
#include "enum_conversions.h"
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "explosion.h"
#include "field_type.h"
#include "flag.h"
#include "game.h"
#include "generic_factory.h"
#include "handle_liquid.h"
#include "input.h"
#include "int_id.h"
#include "item.h"
#include "item_cable.h"
#include "item_functions.h"
#include "itype.h"
#include "json.h"
#include "line.h"
#include "magic.h"
#include "make_static.h"
#include "map.h"
#include "map_iterator.h"
#include "mapdata.h"
#include "memorial_logger.h"
#include "messages.h"
#include "monster.h"
#include "morale_types.h"
#include "mutation.h"
#include "npc.h"
#include "options.h"
#include "output.h"
#include "overmapbuffer.h"
#include "pimpl.h"
#include "player.h"
#include "player_activity.h"
#include "pldata.h"
#include "point.h"
#include "projectile.h"
#include "requirements.h"
#include "regen.h"
#include "rng.h"
#include "sounds.h"
#include "string_formatter.h"
#include "string_id.h"
#include "teleport.h"
#include "translations.h"
#include "ui.h"
#include "ui_manager.h"
#include "units.h"
#include "units_utility.h"
#include "value_ptr.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vpart_position.h"
#include "weather.h"
#include "weather_gen.h"
#include "active_tile_data_def.h"
#include "distribution_grid.h"

static const activity_id ACT_OPERATION( "ACT_OPERATION" );

static const efftype_id effect_adrenaline( "adrenaline" );
static const efftype_id effect_assisted( "assisted" );
static const efftype_id effect_asthma( "asthma" );
static const efftype_id effect_bleed( "bleed" );
static const efftype_id effect_bloodworms( "bloodworms" );
static const efftype_id effect_cig( "cig" );
static const efftype_id effect_cocaine_high( "cocaine_high" );
static const efftype_id effect_datura( "datura" );
static const efftype_id effect_dermatik( "dermatik" );
static const efftype_id effect_drunk( "drunk" );
static const efftype_id effect_fungus( "fungus" );
static const efftype_id effect_hallu( "hallu" );
static const efftype_id effect_heating_bionic( "heating_bionic" );
static const efftype_id effect_iodine( "iodine" );
static const efftype_id effect_meth( "meth" );
static const efftype_id effect_narcosis( "narcosis" );
static const efftype_id effect_operating( "operating" );
static const efftype_id effect_pblue( "pblue" );
static const efftype_id effect_pkill_l( "pkill_l" );
static const efftype_id effect_pkill1( "pkill1" );
static const efftype_id effect_pkill2( "pkill2" );
static const efftype_id effect_pkill3( "pkill3" );
static const efftype_id effect_poison( "poison" );
static const efftype_id effect_badpoison( "badpoison" );
static const efftype_id effect_sleep( "sleep" );
static const efftype_id effect_stung( "stung" );
static const efftype_id effect_teleglow( "teleglow" );
static const efftype_id effect_took_flumed( "took_flumed" );
static const efftype_id effect_took_prozac( "took_prozac" );
static const efftype_id effect_took_prozac_bad( "took_prozac_bad" );
static const efftype_id effect_took_xanax( "took_xanax" );
static const efftype_id effect_under_op( "under_operation" );
static const efftype_id effect_visuals( "visuals" );
static const efftype_id effect_weed_high( "weed_high" );
static const efftype_id effect_infected( "infected" );

static const itype_id fuel_type_battery( "battery" );
static const itype_id fuel_type_metabolism( "metabolism" );
static const itype_id fuel_type_muscle( "muscle" );
static const itype_id fuel_type_sun_light( "sunlight" );
static const itype_id fuel_type_wind( "wind" );

static const itype_id itype_anesthetic( "anesthetic" );
static const itype_id itype_burnt_out_bionic( "burnt_out_bionic" );
static const itype_id itype_radiocontrol( "radiocontrol" );
static const itype_id itype_remotevehcontrol( "remotevehcontrol" );
static const itype_id itype_UPS( "UPS" );
static const itype_id itype_water_clean( "water_clean" );

static const fault_id fault_bionic_nonsterile( "fault_bionic_nonsterile" );

static const skill_id skill_computer( "computer" );
static const skill_id skill_electronics( "electronics" );
static const skill_id skill_firstaid( "firstaid" );
static const skill_id skill_mechanics( "mechanics" );

static const bionic_id bio_adrenaline( "bio_adrenaline" );
static const bionic_id bio_advreactor( "bio_advreactor" );
static const bionic_id bio_ads( "bio_ads" );
static const bionic_id bio_blood_anal( "bio_blood_anal" );
static const bionic_id bio_blood_filter( "bio_blood_filter" );
static const bionic_id bio_electrosense_bscanner( "bio_electrosense_bscanner" );
static const bionic_id bio_cqb( "bio_cqb" );
static const bionic_id bio_earplugs( "bio_earplugs" );
static const bionic_id bio_ears( "bio_ears" );
static const bionic_id bio_electrosense( "bio_electrosense" );
static const bionic_id bio_electrosense_voltmeter( "bio_electrosense_voltmeter" );
static const bionic_id bio_emp( "bio_emp" );
static const bionic_id bio_evap( "bio_evap" );
static const bionic_id bio_eye_optic( "bio_eye_optic" );
static const bionic_id bio_flashbang( "bio_flashbang" );
static const bionic_id bio_geiger( "bio_geiger" );
static const bionic_id bio_gills( "bio_gills" );
static const bionic_id bio_hydraulics( "bio_hydraulics" );
static const bionic_id bio_infolink( "bio_infolink" );
static const bionic_id bio_jointservo( "bio_jointservo" );
static const bionic_id bio_lighter( "bio_lighter" );
static const bionic_id bio_lockpick( "bio_lockpick" );
static const bionic_id bio_magnet( "bio_magnet" );
static const bionic_id bio_nanobots( "bio_nanobots" );
static const bionic_id bio_painkiller( "bio_painkiller" );
static const bionic_id bio_probability_travel( "bio_probability_travel" );
static const bionic_id bio_radscrubber( "bio_radscrubber" );
static const bionic_id bio_reactor( "bio_reactor" );
static const bionic_id bio_remote( "bio_remote" );
static const bionic_id bio_resonator( "bio_resonator" );
static const bionic_id bio_shockwave( "bio_shockwave" );
static const bionic_id bio_teleport( "bio_teleport" );
static const bionic_id bio_time_freeze( "bio_time_freeze" );
static const bionic_id bio_tools( "bio_tools" );
static const bionic_id bio_torsionratchet( "bio_torsionratchet" );
static const bionic_id bio_water_extractor( "bio_water_extractor" );
static const bionic_id afs_bio_dopamine_stimulators( "afs_bio_dopamine_stimulators" );

static const trait_id trait_CENOBITE( "CENOBITE" );
static const trait_id trait_DEBUG_BIONICS( "DEBUG_BIONICS" );
static const trait_id trait_MASOCHIST( "MASOCHIST" );
static const trait_id trait_MASOCHIST_MED( "MASOCHIST_MED" );
static const trait_id trait_NOPAIN( "NOPAIN" );
static const trait_id trait_PROF_AUTODOC( "PROF_AUTODOC" );
static const trait_id trait_PROF_MED( "PROF_MED" );
static const trait_id trait_PYROMANIA( "PYROMANIA" );
static const trait_id trait_THRESH_MEDICAL( "THRESH_MEDICAL" );

static const flag_id flag_BIONIC_GUN( "BIONIC_GUN" );
static const flag_id flag_BIONIC_WEAPON( "BIONIC_WEAPON" );
static const flag_id flag_BIONIC_TOGGLED( "BIONIC_TOGGLED" );
static const std::string flag_SAFE_FUEL_OFF( "SAFE_FUEL_OFF" );
static const std::string flag_SEALED( "SEALED" );

// (De-)Installation difficulty for bionics that don't have item form
constexpr int BIONIC_NOITEM_DIFFICULTY = 12;

namespace
{
generic_factory<bionic_data> bionic_factory( "bionic" );
} //namespace
std::vector<bionic_id> faulty_bionics;

/** @relates string_id */
template<>
const bionic_data &string_id<bionic_data>::obj() const
{
    return bionic_factory.obj( *this );
}

/** @relates string_id */
template<>
bool string_id<bionic_data>::is_valid() const
{
    return bionic_factory.is_valid( *this );
}



std::vector<bodypart_id> get_occupied_bodyparts( const bionic_id &bid )
{
    std::vector<bodypart_id> parts;
    for( const std::pair<const bodypart_str_id, int> &element : bid->occupied_bodyparts ) {
        if( element.second > 0 ) {
            parts.push_back( element.first.id() );
        }
    }
    return parts;
}

bool bionic_data::has_flag( const flag_id &flag ) const
{
    return flags.contains( flag );
}

itype_id bionic_data::itype() const
{
    // Item id matches bionic id (as strings).
    return itype_id( id.str() );
}

bool bionic_data::is_included( const bionic_id &id ) const
{
    return std::ranges::contains( included_bionics, id );
}

void bionic_data::load_bionic( const JsonObject &jo, const std::string &src )
{
    bionic_factory.load( jo, src );
}

void bionic_data::check_consistency()
{
    bionic_factory.check();
}

void bionic_data::finalize_all()
{
    bionic_factory.finalize();
    for( const bionic_data &bd : bionic_factory.get_all() ) {
        bd.finalize();
    }
}

std::vector<bionic_data> bionic_data::get_all()
{
    return bionic_factory.get_all();
}

void bionic_data::resolve_lua_callbacks(
    const std::map<std::string, std::unique_ptr<lua_bionic_callback_actor>> &actors )
{
    for( const bionic_data &bd : bionic_factory.get_all() ) {
        auto it = actors.find( bd.id.str() );
        if( it != actors.end() ) {
            bd.lua_callbacks = it->second.get();
        }
    }
}

void bionic_data::reset()
{
    bionic_factory.reset();
    faulty_bionics.clear();
}

void bionic_data::load( const JsonObject &jsobj, const std::string &src )
{
    const bool strict = is_strict_enabled( src );

    mandatory( jsobj, was_loaded, "name", name );
    mandatory( jsobj, was_loaded, "description", description );

    assign( jsobj, "act_cost", power_activate, strict, 0_kJ );
    assign( jsobj, "deact_cost", power_deactivate, strict, 0_kJ );
    assign( jsobj, "react_cost", power_over_time, strict, 0_kJ );
    assign( jsobj, "trigger_cost", power_trigger, strict, 0_kJ );
    assign( jsobj, "kcal_trigger_cost", kcal_trigger, strict, 0 );
    assign( jsobj, "time", charge_time, strict, 0 );
    assign( jsobj, "capacity", capacity, strict, 0_kJ );
    assign( jsobj, "included", included, strict );
    assign( jsobj, "weight_capacity_modifier", weight_capacity_modifier, strict, 1.0f );
    assign( jsobj, "weight_capacity_bonus", weight_capacity_bonus, strict, 0_gram );
    assign_map_from_array( jsobj, "stat_bonus", stat_bonus, strict );
    assign( jsobj, "remote_fuel_draw", remote_fuel_draw, strict, 0_J );
    is_remote_fueled = remote_fuel_draw > 0_J;
    assign( jsobj, "fuel_options", fuel_opts, strict );
    assign( jsobj, "fuel_capacity", fuel_capacity, strict, 0 );
    assign( jsobj, "fuel_efficiency", fuel_efficiency, strict, 0.0f );
    assign( jsobj, "fuel_multiplier", fuel_multiplier, strict, 0 );
    assign( jsobj, "passive_fuel_efficiency", passive_fuel_efficiency, strict, 0.0f );
    assign( jsobj, "coverage_power_gen_penalty", coverage_power_gen_penalty, strict );
    assign( jsobj, "exothermic_power_gen", exothermic_power_gen, strict );
    assign( jsobj, "power_gen_emission", power_gen_emission, strict );
    assign_map_from_array( jsobj, "env_protec", env_protec, strict );
    assign_map_from_array( jsobj, "bash_protec", bash_protec, strict );
    assign_map_from_array( jsobj, "cut_protec", cut_protec, strict );
    assign_map_from_array( jsobj, "bullet_protec", bullet_protec, strict );
    assign_map_from_array( jsobj, "occupied_bodyparts", occupied_bodyparts, strict );
    assign_map_from_array( jsobj, "encumbrance", encumbrance, strict );
    assign( jsobj, "fake_item", fake_item, strict );
    assign( jsobj, "canceled_mutations", canceled_mutations, strict );
    assign( jsobj, "enchantments", enchantments, strict );
    assign_map_from_array( jsobj, "learned_spells", learned_spells, strict );
    assign( jsobj, "included_bionics", included_bionics, strict );
    assign( jsobj, "required_bionics", required_bionics, strict );
    assign( jsobj, "upgraded_bionic", upgraded_bionic, strict );
    assign( jsobj, "available_upgrades", available_upgrades, strict );
    assign( jsobj, "flags", flags, strict );
    assign( jsobj, "can_uninstall", can_uninstall, strict );
    assign( jsobj, "no_uninstall_reason", no_uninstall_reason, strict );
    assign( jsobj, "starting_bionic", starting_bionic, strict );
    assign( jsobj, "points", points, strict );


    activated = has_flag( flag_BIONIC_TOGGLED ) ||
                power_activate > 0_kJ ||
                charge_time > 0;
}

void bionic_data::finalize() const
{
    if( has_flag( STATIC( flag_id( "BIONIC_FAULTY" ) ) ) ) {
    faulty_bionics.push_back( id );
    }
}

void bionic_data::check() const
{
    consistency_report rep;
    if( !included && !itype().is_valid() ) {
        rep.warn( "has no defined item version" );
    }
    if( charge_time < 0 ) {
        rep.warn( "specifies charge_time < 0" );
    }
    for( const itype_id &it : fuel_opts ) {
        if( !it.is_valid() ) {
            rep.warn( "specifies as fuel option unknown item \"%s\"", it );
        }
    }
    if( power_gen_emission && !power_gen_emission.is_valid() ) {
        rep.warn( "specifies unknown emission \"%s\"", power_gen_emission.str() );
    }
    for( const auto &it : env_protec ) {
        if( !it.first.is_valid() ) {
            rep.warn( "env_protec specifies unknown body part \"%s\"", it.first.str() );
        }
    }
    for( const auto &it : cut_protec ) {
        if( !it.first.is_valid() ) {
            rep.warn( "cut_protec specifies unknown body part \"%s\"", it.first.str() );
        }
    }
    for( const auto &it : bullet_protec ) {
        if( !it.first.is_valid() ) {
            rep.warn( "bullet_protec specifies unknown body part \"%s\"", it.first.str() );
        }
    }
    for( const auto &it : bash_protec ) {
        if( !it.first.is_valid() ) {
            rep.warn( "bash_protec specifies unknown body part \"%s\"", it.first.str() );
        }
    }
    for( const enchantment_id &eid : id->enchantments ) {
        if( !eid.is_valid() ) {
            rep.warn( "uses undefined enchantment \"%s\"", eid.str() );
        }
    }
    for( const auto &it : occupied_bodyparts ) {
        if( !it.first.is_valid() ) {
            rep.warn( "occupies unknown body part \"%s\"", it.first.str() );
        }
    }
    for( const auto &it : encumbrance ) {
        if( !it.first.is_valid() ) {
            rep.warn( "encumbers unknown body part \"%s\"", it.first.str() );
        }
    }
    if( !fake_item.is_empty() && !fake_item.is_valid() ) {
        rep.warn( "has unknown fake_item \"%s\"", fake_item );
    }
    for( const trait_id &mid : canceled_mutations ) {
        if( !mid.is_valid() ) {
            rep.warn( "cancels undefined mutation \"%s\"", mid.str() );
        }
    }
    for( const auto &it : learned_spells ) {
        if( !it.first.is_valid() ) {
            rep.warn( "teaches unknown spell \"%s\"", it.first.str() );
        }
    }
    for( const bionic_id &bid : included_bionics ) {
        if( !bid.is_valid() ) {
            rep.warn( "includes undefined bionic \"%s\"", bid.str() );
        }
        if( !bid->occupied_bodyparts.empty() ) {
            rep.warn( "includes bionic \"%s\" which occupies slots.  Those slots should be occupied by this bionic instead.",
                      bid.str() );
        }
    }
    if( upgraded_bionic ) {
        if( upgraded_bionic == id ) {
            rep.warn( "is upgraded with itself" );
        } else if( !upgraded_bionic.is_valid() ) {
            rep.warn( "upgrades undefined bionic \"%s\"", upgraded_bionic.str() );
        }
    }
    if( !required_bionics.empty() ) {
        for( const bionic_id &it : required_bionics ) {
            if( it == id ) {
                rep.warn( "The CBM %s requires itself as a prerequisite for installation", it.str() );
            } else if( !it.is_valid() ) {
                rep.warn( "The CBM %s requires undefined bionic %s", id.str(), it.str() );
            }
        }
    }

    for( const bionic_id &it : available_upgrades ) {
        if( !it.is_valid() ) {
            rep.warn( "specifies unknown upgrade \"%s\"", it.str() );
        }
    }
    for( const flag_id &it : flags ) {
        if( !it.is_valid() ) {
            rep.warn( "specifies unknown flag \"%s\"", it.str() );
        }
    }
    if( has_flag( flag_BIONIC_GUN ) && has_flag( flag_BIONIC_WEAPON ) ) {
        rep.warn( "is specified as both gun and weapon bionic" );
    }
    if( ( has_flag( flag_BIONIC_GUN ) || has_flag( flag_BIONIC_WEAPON ) ) && fake_item.is_empty() ) {
        rep.warn( "is missing fake_item" );
    }
    if( !rep.is_empty() ) {
        debugmsg( rep.format( "bionic", id.str() ) );
    }
}

bionic_data::bionic_data() : name( no_translation( "bad bionic" ) ),
    description( no_translation( "This bionic was not set up correctly, this is a bug" ) )
{
}

bionic_data::~bionic_data() = default;

static void force_comedown( effect &eff )
{
    if( eff.is_null() || eff.get_effect_type() == nullptr || eff.get_duration() <= 1_turns ) {
        return;
    }

    eff.set_duration( std::min( eff.get_duration(), eff.get_int_dur_factor() ) );
}

void npc::discharge_cbm_weapon()
{
    if( cbm_active.is_null() ) {
        return;
    }
    mod_power_level( -cbm_active->power_activate );
    cbm_fake_active.release();
    cbm_active = bionic_id::NULL_ID();
}

void deactivate_weapon_cbm( npc &who )
{
    for( bionic &i : *who.my_bionics ) {
        if( i.powered && i.info().has_flag( flag_BIONIC_WEAPON ) ) {
            who.deactivate_bionic( i );
            who.clear_npc_ai_info_cache( npc_ai_info::ideal_weapon_value );
        }
    }
}

std::vector<std::pair<bionic_id, item *>> find_reloadable_cbms( npc &who )
{
    std::vector<std::pair<bionic_id, item *>> cbm_list;
    // Runs down full list of CBMs that qualify as weapons.
    // Need a way to make this less costly.
    for( const bionic &bio : *who.my_bionics ) {
        if( !bio.info().has_flag( flag_BIONIC_WEAPON ) ) {
            continue;
        }
        item &cbm_fake = *item::spawn_temporary( bio.info().fake_item );
        // I'd hope it's not possible to be greater than but...
        if( static_cast<int>( bio.ammo_count ) >= cbm_fake.ammo_capacity() ) {
            continue;
        }
        if( bio.ammo_count > 0 ) {
            cbm_fake.ammo_set( bio.ammo_loaded, bio.ammo_count );
        }
        cbm_list.emplace_back( bio.id, &cbm_fake );
    }
    return cbm_list;
}

std::map<item *, bionic_id> npc::check_toggle_cbm()
{
    std::map<item *, bionic_id> res;
    const float allowed_ratio = static_cast<int>( rules.cbm_reserve ) / 100.0f;
    const units::energy free_power = get_power_level() - get_max_power_level() * allowed_ratio;
    if( free_power <= 0_J ) {
        return res;
    }
    for( bionic &bio : get_bionic_collection() ) {
        // I'm not checking if NPC_USABLE because if it isn't it shouldn't be in them.
        if( bio.powered || !bio.info().has_flag( flag_BIONIC_WEAPON ) ||
            free_power < bio.info().power_activate ) {
            continue;
        }
        item *cbm_fake = item::spawn_temporary( bio.info().fake_item );
        if( bio.ammo_count > 0 ) {
            cbm_fake->ammo_set( bio.ammo_loaded, bio.ammo_count );
        }
        res[cbm_fake] = bio.id;
    }
    return res;
}

void npc::check_or_use_weapon_cbm()
{
    // if active bionics have been chosen, keep using them.
    if( !cbm_active.is_null() ) {
        return;
    }

    std::vector<int> avail_active_cbms;

    const float allowed_ratio = static_cast<int>( rules.cbm_reserve ) / 100.0f;
    const units::energy free_power = get_power_level() - get_max_power_level() * allowed_ratio;
    if( free_power <= 0_J ) {
        return;
    }

    int cbm_index = 0;
    for( bionic &bio : *my_bionics ) {
        // I'm not checking if NPC_USABLE because if it isn't it shouldn't be in them.
        if( free_power >= bio.info().power_activate && bio.info().has_flag( flag_BIONIC_GUN ) ) {
            avail_active_cbms.push_back( cbm_index );
        }
        cbm_index++;
    }

    if( !avail_active_cbms.empty() ) {
        Creature *critter = current_target();
        int dist = rl_dist( bub_pos(), critter->bub_pos() );
        int active_index = -1;
        int best_dps = -1;
        bool wield_gun = primary_weapon().is_gun();

        detached_ptr<item> best_cbm_active;
        // If wielding a gun, best_dps to beat is at minimum the gun wielded.
        if( wield_gun ) {
            std::optional<gun_mode> weap_mode = npc_ai::best_mode_for_range(
                                                    *this, this->primary_weapon(), dist ).second;
            best_dps = this->primary_weapon().ideal_ranged_dps( *this, weap_mode );
        }

        for( int i : avail_active_cbms ) {
            bionic &bio = ( *my_bionics )[ i ];
            detached_ptr<item> cbm_weapon = item::spawn( bio.info().fake_item );

            bool not_allowed = !rules.has_flag( ally_rule::use_guns ) ||
                               ( rules.has_flag( ally_rule::use_silent ) && !cbm_weapon->is_silent() );
            if( is_player_ally() && not_allowed ) {
                continue;
            }

            auto [mode_id, mode_] = npc_ai::best_mode_for_range( *this, *cbm_weapon, dist );
            double dps = cbm_weapon->ideal_ranged_dps( *this, mode_ );

            if( dps > best_dps ) {
                active_index = i;
                best_cbm_active = std::move( cbm_weapon );
                best_dps = dps;
            }
        }

        if( active_index > 0 ) {
            cbm_active = ( *my_bionics )[active_index].id;
            cbm_fake_active = std::move( best_cbm_active );
        }
    }
}

// Why put this in a Big Switch?  Why not let bionics have pointers to
// functions, much like monsters and items?
//
// Well, because like diseases, which are also in a Big Switch, bionics don't
// share functions....
bool Character::activate_bionic( bionic &bio, bool eff_only, bool *close_bionics_ui )
{
    const bool mounted = is_mounted();
    if( bio.incapacitated_time > 0_turns ) {
        add_msg_if_player( m_info, _( "Your %s is shorting out and can't be activated." ),
                           bio.info().name );
        return false;
    }

    // eff_only means only do the effect without messing with stats or displaying messages
    if( !eff_only ) {
        if( bio.powered ) {
            // It's already on!
            return false;
        }
        if( !enough_power_for( bio.id ) ) {
            add_msg_if_player( m_info, _( "You don't have the power to activate your %s." ),
                               bio.info().name );
            return false;
        }

        // HACK: burn_fuel() doesn't check for available fuel in remote source on start.
        // If CBM is successfully activated, the check will occur when it actually tries to draw power
        if( !bio.info().is_remote_fueled ) {
            if( !burn_fuel( bio, true ) ) {
                return false;
            }
        }

        // We can actually activate now, do activation-y things
        mod_power_level( -bio.info().power_activate );

        bio.powered = bio.info().has_flag( flag_BIONIC_TOGGLED ) || bio.info().charge_time > 0;

        if( bio.info().charge_time > 0 ) {
            bio.charge_timer = bio.info().charge_time;
        }
        if( !bio.id->enchantments.empty() ) {
            recalculate_enchantment_cache();
        }
    }

    auto add_msg_activate = [&]() {
        if( !eff_only && !bio.is_auto_start_keep_full() ) {
            add_msg_if_player( m_info, _( "You activate your %s." ), bio.info().name );
        } else if( get_player_character().sees( bub_pos() ) ) {
            add_msg_if_npc( m_info, _( "%s activates their %s." ), disp_name(),
                            bio.info().name );
        }
    };
    auto refund_power = [&]() {
        if( !eff_only ) {
            mod_power_level( bio.info().power_activate );
        }
    };

    const w_point &weatherPoint = get_weather().get_precise();

    map &here = get_map();
    // On activation effects go here
    if( bio.info().has_flag( flag_BIONIC_GUN ) ) {
        add_msg_activate();
        refund_power(); // Power usage calculated later, in avatar_action::fire

        if( close_bionics_ui ) {
            *close_bionics_ui = true;
        }
        avatar_action::fire_ranged_bionic( *this->as_avatar(),
                                           item::spawn( bio.info().fake_item ),
                                           bio.info().power_activate );
    } else if( bio.info().has_flag( flag_BIONIC_WEAPON ) ) {
        if( primary_weapon().has_flag( flag_NO_UNWIELD ) ) {
            add_msg_if_player( m_info, _( "Deactivate your %s first!" ), primary_weapon().tname() );
            refund_power();
            bio.powered = false;
            return false;
        }

        if( !primary_weapon().is_null() ) {
            const std::string query = string_format( _( "Stop wielding %s?" ), primary_weapon().tname() );
            if( !dispose_item( primary_weapon(), query ) ) {
                refund_power();
                bio.powered = false;
                return false;
            }
        }

        add_msg_activate();
        set_primary_weapon( item::spawn( bio.info().fake_item ) );
        primary_weapon().invlet = '#';
        if( is_player() && bio.ammo_count > 0 ) {
            primary_weapon().ammo_set( bio.ammo_loaded, bio.ammo_count );
            avatar_action::fire_wielded_weapon( g->u );
        }
        clear_npc_ai_info_cache( npc_ai_info::ideal_weapon_value );
    } else if( bio.id == bio_ears && has_active_bionic( bio_earplugs ) ) {
        add_msg_activate();
        for( bionic &bio : get_bionic_collection() ) {
            if( bio.id == bio_earplugs ) {
                bio.powered = false;
                add_msg_if_player( m_info, _( "Your %s automatically turn off." ),
                                   bio.info().name );
            }
        }
    } else if( bio.id == bio_earplugs && has_active_bionic( bio_ears ) ) {
        add_msg_activate();
        for( bionic &bio : get_bionic_collection() ) {
            if( bio.id == bio_ears ) {
                bio.powered = false;
                add_msg_if_player( m_info, _( "Your %s automatically turns off." ),
                                   bio.info().name );
            }
        }
    } else if( bio.id == bio_evap ) {
        add_msg_activate();
        const w_point &weatherPoint = get_weather().get_precise();
        int humidity = get_local_humidity( weatherPoint.humidity, get_weather().weather_id,
                                           g->is_sheltered( g->u.bub_pos() ) );
        // thirst units = 5 mL
        int water_available = std::lround( humidity * 3.0 / 100.0 );
        if( water_available == 0 ) {
            bio.powered = false;
            add_msg_if_player( m_bad, _( "There is not enough humidity in the air for your %s to function." ),
                               bio.info().name );
            return false;
        } else if( water_available == 1 ) {
            add_msg_if_player( m_mixed,
                               _( "Your %s issues a low humidity warning.  Efficiency will be reduced." ),
                               bio.info().name );
        }
    } else if( bio.id == bio_cqb ) {
        add_msg_activate();
        const avatar *you = as_avatar();
        if( you && !martial_arts_data->pick_style( *you ) ) {
            bio.powered = false;
            add_msg_if_player( m_info, _( "You change your mind and turn it off." ) );
            return false;
        }
    } else if( bio.id == bio_resonator ) {
        add_msg_activate();
        //~Sound of a bionic sonic-resonator shaking the area
        sound_event se;
        se.origin = bub_pos();
        se.volume = 80;
        se.category = sounds::sound_t::combat;
        se.description = _( "VRRRRMP!" );
        se.from_player = is_avatar();
        se.from_npc = !se.from_player;
        se.faction = get_faction()->id();
        se.monfaction = get_faction()->mon_faction();
        se.id = "bionic";
        se.variant = static_cast<std::string>( bio_resonator );
        sounds::sound( se );
        for( const tripoint_bub_ms &bashpoint : here.points_in_radius( bub_pos(), 1 ) ) {
            here.bash( bashpoint, 110 );
            // Multibash effect, so that doors &c will fall
            here.bash( bashpoint, 110 );
            here.bash( bashpoint, 110 );
        }

        mod_moves( -100 );
    } else if( bio.id == bio_time_freeze ) {
        if( mounted ) {
            refund_power();
            add_msg_if_player( m_info, _( "You cannot activate %s while mounted." ), bio.info().name );
            return false;
        }
        add_msg_activate();

        mod_moves( units::to_kilojoule( get_power_level() ) );
        set_power_level( 0_kJ );
        add_msg_if_player( m_good, _( "Your speed suddenly increases!" ) );
        if( one_in( 3 ) ) {
            add_msg_if_player( m_bad, _( "Your muscles tear with the strain." ) );
            apply_damage( nullptr, bodypart_id( "arm_l" ), rng( 5, 10 ) );
            apply_damage( nullptr, bodypart_id( "arm_r" ), rng( 5, 10 ) );
            apply_damage( nullptr, bodypart_id( "leg_l" ), rng( 7, 12 ) );
            apply_damage( nullptr, bodypart_id( "leg_r" ), rng( 7, 12 ) );
            apply_damage( nullptr, bodypart_id( "torso" ), rng( 5, 15 ) );
        }
        if( one_in( 5 ) ) {
            add_effect( effect_teleglow, rng( 5_minutes, 40_minutes ) );
        }
    } else if( bio.id == bio_teleport ) {
        if( mounted ) {
            refund_power();
            add_msg_if_player( m_info, _( "You cannot activate %s while mounted." ), bio.info().name );
            return false;
        }
        add_msg_activate();

        teleport::teleport( *this );
        add_effect( effect_teleglow, 30_minutes );
        mod_moves( -100 );
    } else if( bio.id == bio_blood_anal ) {
        add_msg_activate();
        conduct_blood_analysis();
    } else if( bio.id == bio_blood_filter ) {
        add_msg_activate();
        static const std::vector<efftype_id> removable = {{
                effect_adrenaline,
                effect_fungus, effect_dermatik, effect_bloodworms,
                effect_poison, effect_stung, effect_badpoison,
                effect_pkill1, effect_pkill2, effect_pkill3, effect_pkill_l,
                effect_drunk, effect_cig, effect_cocaine_high, effect_weed_high,
                effect_hallu, effect_visuals, effect_pblue, effect_iodine, effect_datura,
                effect_took_xanax, effect_took_prozac, effect_took_prozac_bad,
                effect_took_flumed,
            }
        };

        for( const auto &eff : removable ) {
            remove_effect( eff );
        }
        // Purging the substance won't remove the fatigue it caused
        force_comedown( get_effect( effect_meth ) );
        set_painkiller( 0 );
        set_stim( 0 );
        mod_moves( -100 );
    } else if( bio.id == bio_torsionratchet ) {
        add_msg_activate();
        add_msg_if_player( m_info, _( "Your torsion ratchet locks onto your joints." ) );
    } else if( bio.id == bio_jointservo ) {
        add_msg_activate();
        add_msg_if_player( m_info, _( "You can now run faster, assisted by joint servomotors." ) );
    } else if( bio.id == bio_lighter ) {
        const std::optional<tripoint_bub_ms> pnt = choose_adjacent( _( "Start a fire where?" ) );
        if( pnt && here.is_flammable( *pnt ) ) {
            add_msg_activate();
            here.add_field( *pnt, fd_fire, 1 );
            if( has_trait( trait_PYROMANIA ) ) {
                add_morale( MORALE_PYROMANIA_STARTFIRE, 5, 10, 3_hours, 2_hours );
                rem_morale( MORALE_PYROMANIA_NOFIRE );
                add_msg_if_player( m_good, _( "You happily light a fire." ) );
            }
            mod_moves( -100 );
        } else {
            refund_power();
            add_msg_if_player( m_info, _( "There's nothing to light there." ) );
            return false;
        }
    } else if( bio.id == bio_geiger ) {
        add_msg_activate();
        add_msg_if_player( m_info, _( "Your radiation level: %d" ), get_rad() );
    } else if( bio.id == bio_adrenaline ) {
        add_msg_activate();
        if( has_effect( effect_adrenaline ) ) {
            add_msg_if_player( m_bad, _( "Safeguards kick in, and the bionic refuses to activate!" ) );
            refund_power();
            return false;
        } else {
            add_msg_activate();
            add_effect( effect_adrenaline, 3_minutes );
        }
    } else if( bio.id == bio_emp ) {
        if( const std::optional<tripoint_bub_ms> pnt = choose_adjacent( _( "Create an EMP where?" ) ) ) {
            add_msg_activate();
            explosion_handler::emp_blast( *pnt );
            mod_moves( -100 );
        } else {
            refund_power();
            return false;
        }
    } else if( bio.id == bio_hydraulics ) {
        add_msg_activate();
        add_msg_if_player( m_good, _( "Your muscles hiss as hydraulic strength fills them!" ) );
        //~ Sound of hissing hydraulic muscle! (not quite as loud as a car horn)
        sound_event se;
        se.origin = bub_pos();
        se.volume = 65;
        se.category = sounds::sound_t::activity;
        se.description = _( "HISISSS!" );
        se.from_player = is_avatar();
        se.from_npc = !se.from_player;
        se.faction = get_faction()->id();
        se.monfaction = get_faction()->mon_faction();
        se.id = "bionic";
        se.variant = static_cast<std::string>( bio_hydraulics );
        sounds::sound( se );
    } else if( bio.id == bio_water_extractor ) {
        bool no_target = true;
        bool extracted = false;
        for( item * &it : here.i_at( bub_pos() ) ) {
            static const auto volume_per_water_charge = 500_ml;
            if( it->is_corpse() ) {
                const int avail = it->get_var( "remaining_water", it->volume() / volume_per_water_charge );
                if( avail > 0 ) {
                    no_target = false;
                    if( query_yn( _( "Extract water from the %s" ),
                                  colorize( it->tname(), it->color_in_inventory() ) ) ) {
                        detached_ptr<item> water = item::spawn( itype_water_clean, calendar::turn, avail );
                        liquid_handler::consume_liquid( std::move( water ) );
                        // NOLINTNEXTLINE(bugprone-use-after-move)
                        if( water && water->charges < avail ) {
                            add_msg_activate();
                            extracted = true;
                            it->set_var( "remaining_water", water->charges );
                        }
                        break;
                    }
                }
            }
        }
        if( no_target ) {
            add_msg_if_player( m_bad, _( "There is no suitable corpse on this tile." ) );
        }
        if( !extracted ) {
            refund_power();
            return false;
        }
    } else if( bio.id == bio_magnet ) {
        add_msg_activate();
        static const std::set<material_id> affected_materials =
        { material_id( "iron" ), material_id( "steel" ) };
        // Remember all items that will be affected, then affect them
        // Don't "snowball" by affecting some items multiple times
        std::vector<std::pair<detached_ptr<item>, const tripoint_bub_ms>> affected;
        const units::mass weight_cap = weight_capacity();
        for( const auto &p : here.points_in_radius( bub_pos(), 10 ) ) {
            if( p == bub_pos() || !here.has_items( p ) || here.has_flag( flag_SEALED, p ) ) {
                continue;
            }

            map_stack stack = here.i_at( p );
            const auto it = std::ranges::find_if( stack, [&]( item * const candidate ) {
                return candidate->weight() < weight_cap && candidate->made_of_any( affected_materials );
            } );
            if( it != stack.end() ) {
                detached_ptr<item> obj;
                stack.erase( it, &obj );

                affected.emplace_back( std::move( obj ), p );
            }
        }

        for( std::pair<detached_ptr<item>, const tripoint_bub_ms> &pr : affected ) {
            projectile proj;
            proj.speed  = 50;
            proj.impact = damage_instance::physical( pr.first->weight() / 250_gram, 0, 0, 0 );
            // make the projectile stop one tile short to prevent hitting the player
            proj.range = rl_dist( pr.second, bub_pos() ) - 1;
            static const std::set<ammo_effect_str_id> ammo_effects = {{
                    ammo_effect_str_id( "NO_ITEM_DAMAGE" ),
                    ammo_effect_str_id( "DRAW_AS_LINE" ),
                    ammo_effect_str_id( "NO_DAMAGE_SCALING" ),
                    ammo_effect_str_id( "JET" ),
                }
            };
            for( const auto &eff : ammo_effects ) {
                proj.add_effect( eff );
            }

            dealt_projectile_attack dealt = projectile_attack(
                                                proj, pr.second, bub_pos(), dispersion_sources{ 0 }, this );
            here.add_item_or_charges( dealt.end_point, std::move( pr.first ) );
        }

        mod_moves( -100 );
    } else if( bio.id == bio_lockpick ) {
        if( !is_avatar() ) {
            return false;
        }
        std::optional<tripoint_bub_ms> target = lockpick_activity_actor::select_location( g->u );
        if( target.has_value() ) {
            add_msg_activate();
            assign_activity( std::make_unique<player_activity>( lockpick_activity_actor::use_bionic(
                                 item::spawn( bio.info().fake_item ), g->m.bub_to_abs( *target ) ) ) );
            if( close_bionics_ui ) {
                *close_bionics_ui = true;
            }
        } else {
            refund_power();
            return false;
        }
    } else if( bio.id == bio_flashbang ) {
        add_msg_activate();
        explosion_handler::flashbang( bub_pos(), true, "explosion" );
        mod_moves( -100 );
    } else if( bio.id == bio_shockwave ) {
        add_msg_activate();

        shockwave_data sw;
        sw.affects_player = false;
        sw.radius = 3;
        sw.force = 4;
        sw.stun = 2;
        sw.dam_mult = 8;
        // affects_player is always false, so assuming the player is always the source of this
        explosion_handler::shockwave( bub_pos(), sw, "explosion", &get_player_character() );
        add_msg_if_player( m_neutral, _( "You unleash a powerful shockwave!" ) );
        mod_moves( -100 );
    } else if( bio.id == bio_infolink ) {
        const weather_manager &weather = get_weather();
        add_msg_activate();
        // Calculate local wind power
        int vehwindspeed = 0;
        if( optional_vpart_position vp = here.veh_at( bub_pos() ) ) {
            vehwindspeed = std::lround( cmps_to_mps( std::abs( vp->vehicle().velocity ) ) * 2.23694 );
        }
        const oter_id &cur_om_ter = get_overmapbuffer( get_dimension() ).ter( abs_omt_pos() );
        /* cache g->get_temperature( player location ) since it is used twice. No reason to recalc */
        const auto player_local_temp = weather.get_temperature( g->u.abs_pos() );
        /* windpower defined in internal velocity units (=.01 mph) */
        double windpower = 100.0f * get_local_windpower( weather.windspeed + vehwindspeed,
                           cur_om_ter, abs_pos(), weather.winddirection, g->is_sheltered( bub_pos() ) );
        add_msg_if_player( m_info, _( "Temperature: %s." ), print_temperature( player_local_temp ) );
        add_msg_if_player( m_info, _( "Relative Humidity: %s." ),
                           print_humidity(
                               get_local_humidity( weatherPoint.humidity, weather.weather_id,
                                       g->is_sheltered( g->u.bub_pos() ) ) ) );
        add_msg_if_player( m_info, _( "Pressure: %s." ),
                           print_pressure( static_cast<int>( weatherPoint.pressure ) ) );
        add_msg_if_player( m_info, _( "Wind Speed: %.1f %s." ),
                           convert_velocity( static_cast<int>( windpower ), VU_WIND ),
                           velocity_units( VU_WIND ) );
        add_msg_if_player( m_info, _( "Feels Like: %s." ),
                           print_temperature(
                               get_local_windchill( units::to_fahrenheit( weatherPoint.temperature ),
                                       weatherPoint.humidity,
                                       windpower / 100 ) + units::to_fahrenheit( player_local_temp ) ) );
        std::string dirstring = get_dirstring( weather.winddirection );
        add_msg_if_player( m_info, _( "Wind Direction: From the %s." ), dirstring );
    } else if( bio.id == bio_remote ) {
        add_msg_activate();
        int choice = uilist( _( "Perform which function:" ), {
            _( "Control vehicle" ), _( "RC radio" )
        } );
        if( choice >= 0 && choice <= 1 ) {
            item *ctr;
            if( choice == 0 ) {
                ctr = item::spawn_temporary( "remotevehcontrol", calendar::start_of_cataclysm );
            } else {
                ctr = item::spawn_temporary( "radiocontrol", calendar::start_of_cataclysm );
            }
            ctr->charges = units::to_kilojoule( get_power_level() );
            int power_use = invoke_item( ctr );
            mod_power_level( units::from_kilojoule( -power_use ) );
            bio.powered = ctr->is_active();
        } else {
            bio.powered = g->remoteveh() != nullptr || !get_value( "remote_controlling" ).empty();
        }
    } else if( bio.info().is_remote_fueled ) {
        std::vector<item *> cables = items_with( []( const item & it ) {
            return it.has_flag( flag_CABLE_SPOOL );
        } );
        bool has_cable = !cables.empty();
        bool free_cable = false;
        bool success = false;
        if( !has_cable ) {
            add_msg_if_player( m_info,
                               _( "You need a jumper cable connected to a power source to drain power from it." ) );
        } else {
            for( item *cable : cables ) {
                auto data = cable_connection_data::make_data( cable );
                if( !data ) {
                    continue;
                }

                switch( data->con2.state ) {
                    case state_none:
                        switch( data->con1.state ) {
                            case state_solar_pack:
                            case state_UPS:
                                add_msg_if_player( m_info,
                                                   _( "You have a cable plugged to a portable power source, but you need to plug it in to the CBM." ) );
                                break;
                            case state_vehicle:
                                add_msg_if_player( m_info,
                                                   _( "You have a cable plugged to a vehicle, but you need to plug it in to the CBM." ) );
                                break;
                            case state_grid:
                                add_msg_if_player( m_info,
                                                   _( "You have a cable plugged to a grid, but you need to plug it in to the CBM." ) );
                                break;
                            case state_self:
                                add_msg_if_player( m_info,
                                                   _( "Cable is plugged-in to the CBM but it has to be also connected to the power source." ) );
                                break;
                            case state_none:
                                free_cable = true;
                                break;
                            default:
                                break;
                        }
                        continue;
                    case state_self:
                        switch( data->con1.state ) {
                            case state_grid:
                                add_msg_if_player( m_info,
                                                   _( "You are plugged to the grid.  It will charge you if it has some juice in it." ) );
                                break;
                            case state_solar_pack:
                                add_msg_if_player( m_info,
                                                   _( "You are plugged to a solar pack.  It will charge you if it's unfolded and in sunlight." ) );
                                break;
                            case state_UPS:
                                add_msg_if_player( m_info,
                                                   _( "You are plugged to a UPS.  It will charge you if it has some juice in it." ) );
                                break;
                            case state_vehicle:
                                add_msg_if_player( m_info,
                                                   _( "You are plugged to the vehicle.  It will charge you if it has some juice in it." ) );
                                break;
                            case state_self:
                            case state_none:
                            default:
                                debugmsg( "Unexpected cable state %s", data->con1.state );
                                continue;
                        }
                        break;
                    case state_grid:
                        add_msg_if_player( m_info,
                                           _( "You are plugged to the grid.  It will charge you if it has some juice in it." ) );
                        break;
                    case state_solar_pack:
                        add_msg_if_player( m_info,
                                           _( "You are plugged to a solar pack.  It will charge you if it's unfolded and in sunlight." ) );
                        break;
                    case state_UPS:
                        add_msg_if_player( m_info,
                                           _( "You are plugged to a UPS.  It will charge you if it has some juice in it." ) );
                        break;
                    case state_vehicle:
                        add_msg_if_player( m_info,
                                           _( "You are plugged to the vehicle.  It will charge you if it has some juice in it." ) );
                        break;
                    default:
                        debugmsg( "Unexpected cable state %s", data->con2.state );
                        continue;
                }
                add_msg_activate();
                success = true;
            }
        }
        if( !success ) {
            if( free_cable ) {
                add_msg_if_player( m_info,
                                   _( "You have at least one free cable in your inventory that you could use to plug yourself in." ) );
            }
            refund_power();
            bio.powered = false;
            return false;
        }

    } else if( bio.id == bio_probability_travel ) {
        if( const std::optional<tripoint_bub_ms> pnt = choose_adjacent(
                _( "Tunnel in which direction?" ) ) ) {
            if( g->m.impassable( *pnt ) ) {
                add_msg_activate();
                g->phasing_move( *pnt );
            } else {
                refund_power();
                add_msg_if_player( m_info, _( "There's nothing to phase through there." ) );
                return false;
            }
        } else {
            refund_power();
            return false;
        }
    } else if( bio.id == bio_electrosense_voltmeter ) {
        add_msg_activate();
        item *vtm;
        vtm = item::spawn_temporary( "voltmeter_bionic", calendar::start_of_cataclysm );
        invoke_item( vtm );
    } else if( bio.info().has_flag( flag_BIONIC_TOOLS ) ) {
        add_msg_activate();
        invalidate_crafting_inventory();
    } else {
        add_msg_activate();
    }

    // Recalculate stats (strength, mods from pain etc.) that could have been affected
    reset_encumbrance();
    reset();
    here.invalidate_lightmap_caches();

    // Also reset crafting inventory cache if this bionic spawned a fake item
    if( !bio.info().fake_item.is_empty() ) {
        invalidate_crafting_inventory();
    }

    if( const auto *lcb = bio.info().lua_callbacks ) {
        lcb->call_on_activate( *this, bio );
    }

    return true;
}

bool Character::deactivate_bionic( bionic &bio, bool eff_only )
{
    if( bio.incapacitated_time > 0_turns ) {
        add_msg_if_player( m_info, _( "Your %s is shorting out and can't be deactivated." ),
                           bio.info().name );
        return false;
    }

    if( bio.info().is_remote_fueled ) {
        reset_remote_fuel();
    }

    // Just do the effect, no stat changing or messages
    if( !eff_only ) {
        if( !bio.powered ) {
            // It's already off!
            return false;
        }
        if( !bio.info().has_flag( flag_BIONIC_TOGGLED ) ) {
            // It's a fire-and-forget bionic, we can't turn it off but have to wait for
            //it to run out of charge
            add_msg_if_player( m_info, _( "You can't deactivate your %s manually!" ),
                               bio.info().name );
            return false;
        }
        if( get_power_level() < bio.info().power_deactivate ) {
            add_msg_if_player( m_info, _( "You don't have the power to deactivate your %s." ),
                               bio.info().name );
            return false;
        }

        // All checks green, get the deactivation cost and print the message.
        mod_power_level( -bio.info().power_deactivate );
        add_msg_if_player( m_neutral, _( "You deactivate your %s." ), bio.info().name );
    }

    // Deactivation is outwidth of the !eff_only block, as involuntary or voluntary it still needs to turn off.
    bio.powered = false;

    // Deactivation effects go here
    if( bio.info().has_flag( flag_BIONIC_WEAPON ) ) {
        if( primary_weapon().typeId() == bio.info().fake_item ) {
            add_msg_if_player( _( "You withdraw your %s." ), primary_weapon().tname() );
            if( g->u.sees( bub_pos() ) ) {
                add_msg_if_npc( m_info, _( "<npcname> withdraws their %s." ), primary_weapon().tname() );
            }
            bio.ammo_loaded =
                primary_weapon().ammo_data() != nullptr ? primary_weapon().ammo_data()->get_id() :
                itype_id::NULL_ID();
            bio.ammo_count = static_cast<unsigned int>( primary_weapon().ammo_remaining() );
            remove_primary_weapon();
            invalidate_crafting_inventory();
        }
    } else if( bio.id == bio_cqb ) {
        martial_arts_data->selected_style_check();
    } else if( bio.id == bio_remote ) {
        if( g->remoteveh() != nullptr && !has_active_item_with_action( "REMOTEVEH" ) ) {
            g->setremoteveh( nullptr );
        } else if( !get_value( "remote_controlling" ).empty() &&
                   !has_active_item_with_action( "REMOTEVEH" ) ) {
            set_value( "remote_controlling", "" );
        }
    } else if( bio.info().has_flag( flag_BIONIC_TOOLS ) ) {
        invalidate_crafting_inventory();
    } else if( bio.id == bio_ads ) {
        mod_power_level( bio.energy_stored );
        bio.energy_stored = 0_kJ;
    }

    // Recalculate stats (strength, mods from pain etc.) that could have been affected
    reset_encumbrance();
    reset();
    get_map().invalidate_lightmap_caches();
    if( !bio.id->enchantments.empty() ) {
        recalculate_enchantment_cache();
    }

    // Also reset crafting inventory cache if this bionic spawned a fake item
    if( !bio.info().fake_item.is_empty() ) {
        invalidate_crafting_inventory();
    }

    if( const auto *lcb = bio.info().lua_callbacks ) {
        lcb->call_on_deactivate( *this, bio );
    }

    return true;
}

bool Character::burn_fuel( bionic &bio, bool start )
{
    if( ( bio.info().fuel_opts.empty() && !bio.info().is_remote_fueled ) ||
        bio.is_this_fuel_powered( fuel_type_muscle ) ) {
        return true;
    }
    const bool is_metabolism_powered = bio.is_this_fuel_powered( fuel_type_metabolism );
    const bool is_cable_powered = bio.info().is_remote_fueled;
    std::vector<itype_id> fuel_available = get_fuel_available( bio.id );
    // When a bionic has passive_fuel_efficiency, perpetual fuels are handled by
    // passive_power_gen() while the bionic is off.  Exclude them from burn_fuel()
    // so they don't interfere with consumable fuel processing when active.
    if( bio.info().passive_fuel_efficiency > 0.0f ) {
        std::erase_if( fuel_available, []( const auto & fuel ) { return fuel->has_flag( flag_PERPETUAL ); } );
    }
    float effective_efficiency = get_effective_efficiency( bio, bio.info().fuel_efficiency );

    if( is_cable_powered ) {
        const itype_id remote_fuel = find_remote_fuel();
        if( !remote_fuel.is_empty() ) {
            fuel_available.emplace_back( remote_fuel );
            if( remote_fuel == fuel_type_sun_light ) {
                const item *pack = item_worn_with_flag( flag_SOLARPACK_ON );
                effective_efficiency = pack != nullptr ? pack->type->solar_efficiency : 0;
            }
            // TODO: check for available fuel in remote source
        } else if( !start ) {
            add_msg_player_or_npc( m_info,
                                   _( "Your %s runs out of fuel and turn off." ),
                                   _( "<npcname>'s %s runs out of fuel and turn off." ),
                                   bio.info().name );
            deactivate_bionic( bio, true );
            return false;
        }
    }

    if( start && fuel_available.empty() ) {
        add_msg_player_or_npc( m_bad, _( "Your %s does not have enough fuel to start." ),
                               _( "<npcname>'s %s does not have enough fuel to start." ),
                               bio.info().name );
        deactivate_bionic( bio );
        return false;
    }
    // don't produce power on start to avoid instant recharge exploit by turning bionic ON/OFF
    //in the menu
    if( !start ) {
        for( const itype_id &fuel : fuel_available ) {
            const item &tmp_fuel = *item::spawn_temporary( fuel );
            const int fuel_energy = tmp_fuel.fuel_energy();
            const bool is_perpetual_fuel = tmp_fuel.has_flag( flag_PERPETUAL );

            int current_fuel_stock;
            if( is_metabolism_powered ) {
                current_fuel_stock = std::max( 0.0f, get_stored_kcal() - 0.8f *
                                               max_stored_kcal() );
            } else if( is_perpetual_fuel ) {
                current_fuel_stock = 1;
            } else if( is_cable_powered ) {
                current_fuel_stock = std::stoi( get_value( "rem_" + fuel.str() ) );
            } else {
                current_fuel_stock = std::stoi( get_value( fuel.str() ) );
            }

            if( !bio.has_flag( flag_SAFE_FUEL_OFF ) &&
                get_power_level() + units::from_kilojoule( fuel_energy ) * effective_efficiency
                > get_max_power_level() ) {
                if( !bio.is_auto_start_keep_full() ) {
                    if( is_metabolism_powered ) {
                        add_msg_player_or_npc( m_info, _( "Your %s turns off to not waste calories." ),
                                               _( "<npcname>'s %s turns off to not waste calories." ),
                                               bio.info().name );
                    } else if( is_perpetual_fuel ) {
                        add_msg_player_or_npc( m_info, _( "Your %s turns off after filling your power banks." ),
                                               _( "<npcname>'s %s turns off after filling their power banks." ),
                                               bio.info().name );
                    } else {
                        add_msg_player_or_npc( m_info, _( "Your %s turns off to not waste fuel." ),
                                               _( "<npcname>'s %s turns off to not waste fuel." ),
                                               bio.info().name );
                    }
                }
                deactivate_bionic( bio, true );
                return false;
            } else {
                if( current_fuel_stock > 0 ) {
                    map &here = get_map();
                    if( is_metabolism_powered ) {
                        const int kcal_consumed = fuel_energy;
                        // 1kcal = 4187 J
                        const units::energy power_gain = kcal_consumed * 4184_J * effective_efficiency;
                        mod_stored_kcal( -kcal_consumed );
                        mod_power_level( power_gain );
                    } else if( is_perpetual_fuel ) {
                        if( fuel == fuel_type_sun_light ) {
                            if( g->is_in_sunlight( bub_pos() ) ) {
                                const weather_type_id &wtype = current_weather( abs_pos() );
                                const float tick_sunlight = incident_sunlight( wtype, calendar::turn );
                                const double intensity = tick_sunlight / default_daylight_level();
                                mod_power_level( units::from_kilojoule( fuel_energy ) * intensity * effective_efficiency );
                            }
                        } else if( fuel == fuel_type_wind ) {
                            int vehwindspeed = 0;
                            const optional_vpart_position vp = here.veh_at( abs_pos() );
                            if( vp ) {
                                vehwindspeed = std::lround( cmps_to_mps( std::abs( vp->vehicle().velocity ) ) * 2.23694 );
                            }
                            const weather_manager &wm = get_weather();
                            const double windpower = get_local_windpower( wm.windspeed + vehwindspeed,
                                                     get_overmapbuffer( get_dimension() ).ter( abs_omt_pos() ), abs_pos(), wm.winddirection,
                                                     g->is_sheltered( bub_pos() ) );
                            mod_power_level( units::from_kilojoule( fuel_energy ) * windpower * effective_efficiency );
                        } else {
                            mod_power_level( units::from_kilojoule( fuel_energy ) * effective_efficiency );
                        }
                    } else if( is_cable_powered ) {
                        auto to_consume = bio.info().remote_fuel_draw;
                        if( get_power_level() >= get_max_power_level() ) {
                            to_consume = 0_J;
                        }
                        const auto unconsumed = consume_remote_fuel( to_consume );
                        // we don't check if to_consume != unconsumed cuz we wouldn't get there otherwise
                        if( to_consume > 0_J ) {
                            if( unconsumed == 0_J ) {
                                mod_power_level( bio.info().remote_fuel_draw * effective_efficiency );
                                current_fuel_stock -= units::to_kilojoule( to_consume );
                            } else {
                                mod_power_level( ( to_consume - unconsumed ) * effective_efficiency );
                                current_fuel_stock = 0;
                            }
                        }
                        set_value( "rem_" + fuel.str(), std::to_string( current_fuel_stock ) );
                    } else {
                        current_fuel_stock -= 1;
                        set_value( fuel.str(), std::to_string( current_fuel_stock ) );
                        update_fuel_storage( fuel );
                        mod_power_level( units::from_kilojoule( fuel_energy ) * effective_efficiency );
                    }

                    heat_emission( bio, fuel_energy );
                    if( bio.info().power_gen_emission ) {
                        here.emit_field( bub_pos(), bio.info().power_gen_emission );
                    }
                } else {

                    if( is_metabolism_powered ) {
                        add_msg_player_or_npc( m_info,
                                               _( "Stored calories are below the safe threshold, your %s shuts down to preserve your health." ),
                                               _( "Stored calories are below the safe threshold, <npcname>'s %s shuts down to preserve their health." ),
                                               bio.info().name );
                    } else {
                        remove_value( fuel.str() );
                        add_msg_player_or_npc( m_info,
                                               _( "Your %s runs out of fuel and turn off." ),
                                               _( "<npcname>'s %s runs out of fuel and turn off." ),
                                               bio.info().name );
                    }
                    deactivate_bionic( bio, true );
                    return false;
                }
            }
        }
    }
    return true;
}

bool Character::has_indefinite_power_source() const
{
    return std::ranges::any_of( *my_bionics, []( const bionic & bio ) {
        return std::ranges::any_of( bio.info().fuel_opts, []( const itype_id & fuel ) {
            return fuel == fuel_type_metabolism ||
                   item::spawn_temporary( fuel )->has_flag( flag_PERPETUAL );
        } );
    } );
}

void Character::passive_power_gen( bionic &bio )
{
    const float passive_fuel_efficiency = bio.info().passive_fuel_efficiency;
    if( bio.info().fuel_opts.empty() || bio.is_this_fuel_powered( fuel_type_muscle ) ||
        passive_fuel_efficiency == 0.0 ) {
        return;
    }
    const float effective_passive_efficiency = get_effective_efficiency( bio, passive_fuel_efficiency );
    const std::vector<itype_id> &fuel_available = get_fuel_available( bio.id );
    map &here = get_map();

    for( const itype_id &fuel : fuel_available ) {
        const int fuel_energy = fuel->fuel ? fuel->fuel->energy : 0.0f;
        if( !fuel->has_flag( flag_PERPETUAL ) ) {
            continue;
        }

        if( fuel == fuel_type_sun_light ) {
            const double modifier = g->natural_light_level( bub_pos().z() ) / default_daylight_level();
            mod_power_level( units::from_kilojoule( fuel_energy ) * modifier * effective_passive_efficiency );
        } else if( fuel == fuel_type_wind ) {
            int vehwindspeed = 0;
            const optional_vpart_position vp = here.veh_at( bub_pos() );
            if( vp ) {
                vehwindspeed = std::lround( cmps_to_mps( std::abs( vp->vehicle().velocity ) ) * 2.23694 );
            }
            const weather_manager &weather = get_weather();
            const double windpower = get_local_windpower( weather.windspeed + vehwindspeed,
                                     get_overmapbuffer( get_dimension() ).ter( abs_omt_pos() ), abs_pos(), weather.winddirection,
                                     g->is_sheltered( bub_pos() ) );
            mod_power_level( units::from_kilojoule( fuel_energy ) * windpower * effective_passive_efficiency );
        } else {
            mod_power_level( units::from_kilojoule( fuel_energy ) * effective_passive_efficiency );
        }

        heat_emission( bio, fuel_energy );
        if( bio.info().power_gen_emission ) {
            here.emit_field( bub_pos(), bio.info().power_gen_emission );
        }
    }
}

itype_id Character::find_remote_fuel( bool look_only )
{
    itype_id remote_fuel;
    map &here = get_map();

    const std::vector<item *> cables = items_with( []( const item & it ) {
        return it.is_active() && it.has_flag( flag_CABLE_SPOOL );
    } );

    for( item *cable : cables ) {
        auto data = cable_connection_data::make_data( cable );
        if( !data || !data->character_connected() || !data->complete() ) {
            continue;
        }

        //At this point we are sure that non_char is not empty
        auto nonchar = *data->get_nonchar_connection();

        switch( nonchar.state ) {
            case state_none:
            case state_self:
            default:
                continue;
            case state_grid: {
                if( !nonchar.point_valid() ) {
                    debugmsg( "Cable_data was not properly initialized or cable map points were not set" );
                    add_msg_if_player( m_bad, _( "You notice the cable has come loose!" ) );
                    cable->reset_cable( this );
                    continue;
                }
                auto *grid_connector = active_tiles::furn_at<vehicle_connector_tile>( nonchar.point );
                if( grid_connector ) {
                    if( !look_only ) {
                        auto &grid = get_distribution_grid_tracker().grid_at( nonchar.point );
                        set_value( "rem_battery", std::to_string( grid.get_resource() ) );
                    }
                    remote_fuel = fuel_type_battery;
                }
                continue;
            }
            case state_vehicle: {
                if( !nonchar.point_valid() ) {
                    debugmsg( "Cable_data was not properly initialized or cable map points were not set" );
                    add_msg_if_player( m_bad, _( "You notice the cable has come loose!" ) );
                    cable->reset_cable( this );
                    continue;
                }
                const optional_vpart_position vp = here.veh_at( nonchar.point );
                if( vp ) {
                    if( !look_only ) {
                        set_value( "rem_battery", std::to_string( vp->vehicle().fuel_left( fuel_type_battery,
                                   true ) ) );
                    }
                    remote_fuel = fuel_type_battery;
                }
                continue;
            }
            case state_solar_pack:
                if( here.is_outside( bub_pos() ) && !is_night( calendar::turn ) ) {
                    if( !look_only ) {
                        set_value( "sunlight", "1" );
                    }
                    remote_fuel = fuel_type_sun_light;
                }
                continue;
            case state_UPS: {
                static const item_filter used_ups = [&]( const item & itm ) {
                    return itm.get_var( "cable" ) == "plugged_in";
                };
                if( !look_only ) {
                    if( has_charges( itype_UPS, 1, used_ups ) ) {
                        set_value( "rem_battery", std::to_string( charges_of( itype_UPS,
                                   units::to_kilojoule( max_power_level ), used_ups ) ) );
                    } else {
                        set_value( "rem_battery", std::to_string( 0 ) );
                    }
                }
                remote_fuel = fuel_type_battery;
                continue;
            }
        }
    }
    return remote_fuel;
}

units::energy Character::consume_remote_fuel( units::energy amount )
{
    int amount_kj = units::to_kilojoule( amount );
    units::energy unconsumed_amount = amount;
    const std::vector<item *> cables = items_with( []( const item & it ) {
        return it.is_active() && it.has_flag( flag_CABLE_SPOOL );
    } );

    map &here = get_map();
    for( const item *cable : cables ) {
        auto data = cable_connection_data::make_data( cable );
        if( !data || ( !data->character_connected() && !data->complete() ) ) {
            continue;
        }

        auto non_char = *data->get_nonchar_connection();
        switch( non_char.state ) {
            case state_vehicle: {
                if( !non_char.point_valid() ) {
                    debugmsg( "Cable_data was not properly initialized or cable map points were not set" );
                    continue;
                }
                const auto vp = here.veh_at( non_char.point );
                if( vp ) {
                    unconsumed_amount = units::from_kilojoule( vp->vehicle().discharge_battery( amount_kj ) );
                }
                break;
            }
            case state_grid: {
                if( !non_char.point_valid() ) {
                    debugmsg( "Cable_data was not properly initialized or cable map points were not set" );
                    continue;
                }
                const auto *grid_connector = active_tiles::furn_at<vehicle_connector_tile>( non_char.point );
                if( grid_connector ) {
                    auto grid = get_distribution_grid_tracker().grid_at( non_char.point );
                    unconsumed_amount = units::from_kilojoule( grid.mod_resource( -amount_kj ) );
                }
                break;
            }
            case state_UPS: {
                static const item_filter used_ups = [&]( const item & itm ) {
                    return itm.get_var( "cable" ) == "plugged_in";
                };
                if( has_charges( itype_UPS, amount_kj, used_ups ) ) {
                    use_charges( itype_UPS, amount_kj, used_ups );
                    unconsumed_amount = 0_J;
                }
                break;
            }
            default:
                continue;
        }
    }

    return unconsumed_amount;
}

void Character::reset_remote_fuel()
{
    //TOOD!: check what the shit
    if( get_bionic_fueled_with( *item::spawn_temporary( fuel_type_sun_light ) ).empty() ) {
        remove_value( "sunlight" );
    }
    remove_value( "rem_battery" );
}

void Character::heat_emission( bionic &bio, int fuel_energy )
{
    if( !bio.info().exothermic_power_gen ) {
        return;
    }
    const float efficiency = bio.info().fuel_efficiency;

    const int heat_prod = fuel_energy * ( 1.0f - efficiency );
    const int heat_level = std::min( heat_prod / 10, 4 );
    const emit_id hotness = emit_id( "emit_hot_air" + std::to_string( heat_level ) + "_cbm" );
    map &here = get_map();
    if( hotness.is_valid() ) {
        const int heat_spread = std::max( heat_prod / 10 - heat_level, 1 );
        here.emit_field( bub_pos(), hotness, heat_spread );
    }
    for( const std::pair<const bodypart_str_id, int> &bp : bio.info().occupied_bodyparts ) {
        add_effect( effect_heating_bionic, 2_seconds, bp.first, heat_prod );
    }
}

float Character::get_effective_efficiency( bionic &bio, float fuel_efficiency )
{
    const std::optional<float> &coverage_penalty = bio.info().coverage_power_gen_penalty;
    float effective_efficiency = fuel_efficiency;
    if( coverage_penalty ) {
        int coverage = 0;
        const std::map< bodypart_str_id, int > &occupied_bodyparts = bio.info().occupied_bodyparts;
        for( const std::pair< const bodypart_str_id, int > &elem : occupied_bodyparts ) {
            for( item * const &i : worn ) {
                if( i->covers( elem.first ) && !i->has_flag( flag_ALLOWS_NATURAL_ATTACKS ) &&
                    !i->has_flag( flag_SEMITANGIBLE ) &&
                    !i->has_flag( flag_PERSONAL ) && !i->has_flag( flag_AURA ) ) {
                    coverage += i->get_coverage( elem.first.id() );
                }
            }
        }
        effective_efficiency = fuel_efficiency * ( 1.0 - ( coverage / ( 100.0 *
                               occupied_bodyparts.size() ) )
                               * coverage_penalty.value() );
    }
    return effective_efficiency;
}

/**
 * @param p the player
 * @param bio the bionic that is meant to be recharged.
 * @param amount the amount of power that is to be spent recharging the bionic.
 * @param factor multiplies the power cost per turn.
 * @param rate divides the number of turns we may charge (rate of 2 discharges in half the time).
 * @return indicates whether we successfully charged the bionic.
 */
static bool attempt_recharge( Character &p, bionic &bio, units::energy &amount, int factor = 1,
                              int rate = 1 )
{
    const bionic_data &info = bio.info();
    units::energy power_cost = info.power_over_time * factor;
    bool recharged = false;

    if( power_cost > 0_kJ ) {
        if( p.get_power_level() >= power_cost ) {
            // Set the recharging cost and charge the bionic.
            amount = power_cost;
            // This is our first turn of charging, so subtract a turn from the recharge delay.
            bio.charge_timer = info.charge_time - rate;
            recharged = true;
        }
    }

    return recharged;
}

void Character::process_bionic( bionic &bio )
{
    if( ( !bio.id->fuel_opts.empty() || bio.id->is_remote_fueled ) && bio.is_auto_start_on() ) {
        const float start_threshold = bio.get_auto_start_thresh();
        std::vector<itype_id> fuel_available = get_fuel_available( bio.id );
        if( bio.id->is_remote_fueled ) {
            const itype_id rem_fuel = find_remote_fuel();
            const std::string rem_amount = get_value( "rem_" + rem_fuel.str() );
            int rem_fuel_stock = 0;
            if( !rem_amount.empty() ) {
                rem_fuel_stock = std::stoi( rem_amount );
            }
            if( !rem_fuel.is_empty() && ( rem_fuel_stock > 0 || rem_fuel->has_flag( flag_PERPETUAL ) ) ) {
                fuel_available.emplace_back( rem_fuel );
            }
        }
        if( !fuel_available.empty() && get_power_level() <= start_threshold * get_max_power_level() ) {
            g->u.activate_bionic( bio );
        } else if( get_power_level() <= start_threshold * get_max_power_level() &&
                   calendar::once_every( 1_hours ) ) {
            add_msg_player_or_npc( m_bad, _( "Your %s does not have enough fuel to use Auto Start." ),
                                   _( "<npcname>'s %s does not have enough fuel to use Auto Start." ),
                                   bio.info().name );
        }
    }

    // Only powered bionics should be processed
    if( !bio.powered ) {
        passive_power_gen( bio );
        return;
    }

    // These might be affected by environmental conditions, status effects, faulty bionics, etc.
    int discharge_factor = 1;
    int discharge_rate = 1;

    if( bio.charge_timer > 0 ) {
        bio.charge_timer -= discharge_rate;
    } else {
        if( bio.info().charge_time > 0 ) {
            if( bio.info().has_flag( STATIC( flag_id( "BIONIC_POWER_SOURCE" ) ) ) ) {
                // Convert fuel to bionic power
                burn_fuel( bio );
                // This is our first turn of charging, so subtract a turn from the recharge delay.
                bio.charge_timer = std::max( 0, bio.info().charge_time - 1 );
            } else {
                // Try to recharge our bionic if it is made for it
                units::energy cost = 0_J;
                bool recharged = attempt_recharge( *this, bio, cost, discharge_factor, discharge_rate );
                if( !recharged ) {
                    // No power to recharge, so deactivate
                    add_msg_if_player( m_neutral, _( "Your %s powers down." ), bio.info().name );
                    // This purposely bypasses the deactivation cost
                    deactivate_bionic( bio, true );
                    return;
                }
                if( cost > 0_J ) {
                    mod_power_level( -cost );
                }
            }
        }
    }

    // Bionic effects on every turn they are active go here.
    if( bio.id == bio_remote ) {
        if( g->remoteveh() == nullptr && get_value( "remote_controlling" ).empty() ) {
            bio.powered = false;
            add_msg_if_player( m_warning, _( "Your %s has lost connection and is turning off." ),
                               bio.info().name );
        }
    } else if( bio.id == bio_hydraulics ) {
        // Sound of hissing hydraulic muscle! (not quite as loud as a car horn)
        sound_event se;
        se.origin = bub_pos();
        se.volume = 65;
        se.category = sounds::sound_t::activity;
        se.description = _( "HISISSS!" );
        se.from_player = is_avatar();
        se.from_npc = !se.from_player;
        se.faction = get_faction()->id();
        se.monfaction = get_faction()->mon_faction();
        se.id = "bionic";
        se.variant = static_cast<std::string>( bio_hydraulics );
        sounds::sound( se );
    } else if( bio.id == bio_nanobots ) {
        int threshold_kcal = bio.info().kcal_trigger > 0 ? 0.85f * max_stored_kcal() +
                             bio.info().kcal_trigger : 0;
        const auto can_use_bionic = [this, &bio, threshold_kcal]() -> bool {
            const bool is_kcal_sufficient = get_stored_kcal() >= threshold_kcal;
            const bool is_power_sufficient = get_power_level() >= bio.info().power_trigger;
            return is_kcal_sufficient && is_power_sufficient;
        };
        if( get_stored_kcal() < threshold_kcal ) {
            bio.powered = false;
            add_msg_if_player( m_warning, _( "Your %s shut down to conserve calories." ), bio.info().name );
            deactivate_bionic( bio );
            return;
        }
        if( calendar::once_every( 30_turns ) ) {
            std::vector<effect *> bleeding_list = get_all_effects_of_type( effect_bleed );
            // Essential parts (Head/Torso) first.
            std::ranges::sort( bleeding_list,
            []( effect * a, effect * b ) {
                return a->get_bp()->essential > b->get_bp()->essential;
            } );
            if( !bleeding_list.empty() ) {
                effect *e = bleeding_list[0];
                if( e->get_intensity() > 1 ) {
                    add_msg_if_player( "Your %s slow the bleeding on your %s", bio.info().name, e->get_bp()->name );
                    e->mod_intensity( -1, false );
                } else {
                    add_msg_if_player( "Your %s staunch the bleeding on your %s", bio.info().name, e->get_bp()->name );
                    e->set_removed();
                }
            }
            if( calendar::once_every( 2_minutes ) ) {
                // Essential parts are considered 10 HP lower than non-essential parts for the purpose of determining priority.
                // I'd use the essential_value, but it's tied up in the heal_actor class of iuse_actor.
                const auto effective_hp = [this]( const bodypart_id & bp ) -> int {
                    return get_part_hp_cur( bp ) - bp->essential * 10;
                };
                const auto should_heal = [this]( const bodypart_id & bp ) -> bool {
                    return get_part_hp_cur( bp ) < get_part_hp_max( bp );
                };
                const auto sort_by = [effective_hp]( const bodypart_id & a, const bodypart_id & b ) -> bool {
                    return effective_hp( a ) < effective_hp( b );
                };
                const auto damaged_parts = [this, should_heal, sort_by]() {
                    const auto xs = get_all_body_parts( true );
                    auto ys = std::vector<bodypart_id> {};
                    std::ranges::copy_if( xs, std::back_inserter( ys ), should_heal );
                    std::ranges::sort( ys, sort_by );
                    return ys;
                };

                for( bodypart_id &bp : damaged_parts() ) {
                    if( !can_use_bionic() ) {
                        return;
                    }
                    heal_adjusted( *this, bp, 1 );
                    mod_power_level( -bio.info().power_trigger );
                    mod_stored_kcal( -bio.info().kcal_trigger );
                }
            }
        }
    } else if( bio.id == bio_painkiller ) {
        const int pkill = get_painkiller();
        const int pain = get_pain();
        const units::energy trigger_cost = bio.info().power_trigger;
        int max_pkill = std::min( 150, pain );
        if( pkill < max_pkill ) {
            mod_painkiller( 1 );
            mod_power_level( -trigger_cost );
        }

        // Only dull pain so extreme that we can't pkill it safely
        if( pkill >= 150 && pain > pkill && get_stim() > -150 ) {
            mod_pain( -1 );
            // Negative side effect: negative stim
            mod_stim( -1 );
            mod_power_level( -trigger_cost );
        }
    } else if( bio.id == bio_gills ) {
        if( has_effect( effect_asthma ) ) {
            add_msg_if_player( m_good,
                               _( "You feel your throat open up and air filling your lungs!" ) );
            remove_effect( effect_asthma );
        }
    } else if( bio.id == bio_evap ) {
        // Aero-Evaporator provides water at 60 watts with 2 L / kWh efficiency
        // which is 10 mL per 5 minutes.  Humidity can modify the amount gained.
        if( calendar::once_every( 5_minutes ) ) {
            const w_point &weatherPoint = get_weather().get_precise();
            int humidity = get_local_humidity( weatherPoint.humidity, get_weather().weather_id,
                                               g->is_sheltered( g->u.bub_pos() ) );
            // in thirst units = 5 mL water
            int water_available = std::lround( humidity * 3.0 / 100.0 );
            // At 50% relative humidity or more, the player will draw 10 mL
            // At 16% relative humidity or less, the bionic will give up
            if( water_available == 0 ) {
                add_msg_if_player( m_bad,
                                   _( "There is not enough humidity in the air for your %s to function." ),
                                   bio.info().name );
                deactivate_bionic( bio );
            } else if( water_available == 1 ) {
                add_msg_if_player( m_mixed,
                                   _( "Your %s issues a low humidity warning.  Efficiency is reduced." ),
                                   bio.info().name );
            }

            mod_thirst( -water_available );
        }

        if( get_thirst() <= thirst_levels::hydrated ) {
            add_msg_if_player( m_good,
                               _( "You are properly hydrated.  Your %s chirps happily." ),
                               bio.info().name );
            deactivate_bionic( bio );
        }
    } else if( bio.id == bio_ads ) {
        if( bio.charge_timer < 2 ) {
            bio.charge_timer = 2;
        }
        if( bio.energy_stored < 150_kJ ) {
            // Max recharge rate is influenced by whether you've been hit or not.
            // See character.cpp for how charge_timer keeps track of that for this bionic.
            units::energy max_rate = 10_kJ;
            if( bio.charge_timer > 2 ) {
                max_rate /= 2;
            }
            units::energy ads_recharge = std::min( max_rate, 150_kJ - bio.energy_stored );
            if( ads_recharge < get_power_level() ) {
                mod_power_level( - ads_recharge );
                bio.energy_stored += ads_recharge;
            } else if( get_power_level() != 0_kJ ) {
                mod_power_level( - get_power_level() );
                bio.energy_stored += get_power_level();
            }
            if( bio.energy_stored == 150_kJ ) {
                add_msg_if_player( m_good, _( "Your %s quietens to a satisfied thrum." ), bio.info().name );
            }
        } else if( bio.energy_stored > 150_kJ ) {
            bio.energy_stored = 150_kJ;
        }
    } else if( bio.id == afs_bio_dopamine_stimulators ) {
        add_morale( MORALE_FEELING_GOOD, 20, 20, 30_minutes, 20_minutes, true );
    } else if( bio.id == bio_electrosense_bscanner ) {
        // This is a horrible mess but can't use the active iuse behavior directly
        map &here = get_map();
        for( const auto &pt : here.points_in_radius( bub_pos(), PICKUP_RANGE ) ) {
            if( !here.has_items( pt ) || !sees( pt ) ) {
                continue;
            }
            for( item * const &corpse : here.i_at( pt ) ) {
                if( !corpse->is_corpse() ||
                    corpse->get_var( "bionics_scanned_by", -1 ) == getID().get_value() ) {
                    continue;
                }

                std::vector<const item *> cbms;
                for( const item * const &maybe_cbm : corpse->get_components() ) {
                    if( maybe_cbm->is_bionic() ) {
                        cbms.push_back( maybe_cbm );
                    }
                }

                units::energy enrg = cbms.size() * bio.info().power_trigger;
                if( get_power_level() >= enrg ) {
                    mod_power_level( -enrg );
                } else {
                    add_msg_if_player( m_bad,
                                       _( "Your %s doesn't have enough power for the %s" ),
                                       bio.info().name, corpse->display_name().c_str() );
                    if( get_power_level() < bio.info().power_trigger ) {
                        break;
                    } else {
                        continue;
                    }
                }

                corpse->set_var( "bionics_scanned_by", getID().get_value() );
                if( !cbms.empty() ) {
                    corpse->set_flag( flag_CBM_SCANNED );
                    std::string bionics_string =
                        enumerate_as_string( cbms.begin(), cbms.end(),
                    []( const item * entry ) -> std::string {
                        return entry->display_name();
                    }, enumeration_conjunction::none );
                    //~ %1 is corpse name, %2 is direction, %3 is bionic name
                    add_msg_if_player( m_good, _( "A %1$s located %2$s contains %3$s." ),
                                       corpse->display_name().c_str(),
                                       direction_name( direction_from( bub_pos(), pt ) ).c_str(),
                                       bionics_string.c_str()
                                     );
                }
            }
            if( get_power_level() < bio.info().power_trigger ) {
                add_msg_if_player( m_bad, _( "Your %s doesn't have enough power and shuts down." ),
                                   bio.info().name );
                deactivate_bionic( bio, true );
                break;
            }
        }
    } else if( bio.id == bio_radscrubber ) {
        if( calendar::once_every( 10_minutes ) ) {
            const units::energy trigger_cost = bio.info().power_trigger;

            if( get_rad() > 0 && bio.energy_stored >= trigger_cost ) {
                add_msg_if_player( m_good, _( "Your %s completed a scrubbing cycle." ), bio.info().name );

                mod_rad( std::max( -10, -get_rad() ) );
                mod_power_level( -trigger_cost );
            }
        }
    }
}

