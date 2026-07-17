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

static const std::string flag_SAFE_FUEL_OFF( "SAFE_FUEL_OFF" );
constexpr int BIONIC_NOITEM_DIFFICULTY = 12;

// Defined in bionics.cpp
extern std::vector<bionic_id> faulty_bionics;
void Character::bionics_uninstall_failure( int difficulty, int success, float adjusted_skill )
{
    // "success" should be passed in as a negative integer representing how far off we
    // were for a successful removal.  We use this to determine consequences for failing.
    success = std::abs( success );

    // failure level is decided by how far off the character was from a successful removal, and
    // this is scaled up or down by the ratio of difficulty/skill.  At high skill levels (or low
    // difficulties), only minor consequences occur.  At low skill levels, severe consequences
    // are more likely.
    const int failure_level = static_cast<int>( std::sqrt( success * 4.0 * difficulty /
                              adjusted_skill ) );
    const int fail_type = std::min( 5, failure_level );

    if( fail_type <= 1 ) {
        add_msg( m_neutral, _( "The removal fails without incident." ) );
        return;
    }

    add_msg( m_neutral, _( "The removal is a failure." ) );
    std::set<body_part> bp_hurt;
    switch( fail_type ) {
        case 2:
        case 3:
            do_damage_for_bionic_failure( 2, 6 );
            break;

        case 4:
        case 5:
            do_damage_for_bionic_failure( 5, difficulty * 5 );
            break;
    }

}

void Character::bionics_uninstall_failure( monster &installer, Character &patient, int difficulty,
        int success, float adjusted_skill )
{

    // "success" should be passed in as a negative integer representing how far off we
    // were for a successful removal.  We use this to determine consequences for failing.
    success = std::abs( success );

    // failure level is decided by how far off the monster was from a successful removal, and
    // this is scaled up or down by the ratio of difficulty/skill.  At high skill levels (or low
    // difficulties), only minor consequences occur.  At low skill levels, severe consequences
    // are more likely.
    const int failure_level = static_cast<int>( std::sqrt( success * 4.0 * difficulty /
                              adjusted_skill ) );
    const int fail_type = std::min( 5, failure_level );

    bool u_see = sees( patient );

    if( u_see || patient.is_player() ) {
        if( fail_type <= 1 ) {
            add_msg( m_neutral, _( "The removal fails without incident." ) );
            return;
        }
        switch( rng( 1, 5 ) ) {
            case 1:
                add_msg( m_mixed, _( "The %s flub the operation." ), installer.name() );
                break;
            case 2:
                add_msg( m_mixed, _( "The %s messes up the operation." ), installer.name() );
                break;
            case 3:
                add_msg( m_mixed, _( "The operation fails." ) );
                break;
            case 4:
                add_msg( m_mixed, _( "The operation is a failure." ) );
                break;
            case 5:
                add_msg( m_mixed, _( "The %s screws up the operation." ), installer.name() );
                break;
        }
    }
    switch( fail_type ) {
        case 2:
        case 3:
            do_damage_for_bionic_failure( failure_level, failure_level * 2 );
            break;

        case 4:
        case 5:
            do_damage_for_bionic_failure( 5, difficulty * 5 );
            break;
    }
}

// bionic manipulation adjusted skill
float Character::bionics_adjusted_skill( const skill_id &most_important_skill,
        const skill_id &important_skill,
        const skill_id &least_important_skill,
        int skill_level )
{
    int pl_skill = bionics_pl_skill( most_important_skill, important_skill, least_important_skill,
                                     skill_level );

    // for chance_of_success calculation, shift skill down to a float between ~0.4 - 30
    float adjusted_skill = static_cast<float>( pl_skill ) - std::min( static_cast<float>( 40 ),
                           static_cast<float>( pl_skill ) - static_cast<float>( pl_skill ) / static_cast<float>( 10.0 ) );
    adjusted_skill *= env_surgery_bonus( 1 ) + get_effect_int( effect_assisted );
    return adjusted_skill;
}

int Character::bionics_pl_skill( const skill_id &most_important_skill,
                                 const skill_id &important_skill,
                                 const skill_id &least_important_skill, int skill_level )
{
    int pl_skill;
    if( skill_level == -1 ) {
        pl_skill = int_cur                                  * 4 +
                   get_skill_level( most_important_skill )  * 4 +
                   get_skill_level( important_skill )       * 3 +
                   get_skill_level( least_important_skill ) * 1;
    } else {
        // override chance as though all values were skill_level if it is provided
        pl_skill = 12 * skill_level;
    }

    // Medical residents have some idea what they're doing
    if( has_trait( trait_PROF_MED ) ) {
        pl_skill += 3;
    }

    // People trained in bionics gain an additional advantage towards using it
    if( has_trait( trait_PROF_AUTODOC ) ) {
        pl_skill += 7;
    }
    return pl_skill;
}

// bionic manipulation chance of success
int bionic_manip_cos( float adjusted_skill, int bionic_difficulty )
{
    if( g->u.has_trait( trait_DEBUG_BIONICS ) ) {
        return 100;
    }

    int chance_of_success = 0;
    // we will base chance_of_success on a ratio of skill and difficulty
    // when skill=difficulty, this gives us 1.  skill < difficulty gives a fraction.
    float skill_difficulty_parameter = static_cast<float>( adjusted_skill /
                                       ( 4.0 * bionic_difficulty ) );

    // when skill == difficulty, chance_of_success is 50%. Chance of success drops quickly below that
    // to reserve bionics for characters with the appropriate skill.  For more difficult bionics, the
    // curve flattens out just above 80%
    chance_of_success = static_cast<int>( ( 100 * skill_difficulty_parameter ) /
                                          ( skill_difficulty_parameter + std::sqrt( 1 / skill_difficulty_parameter ) ) );

    return chance_of_success;
}

bool Character::can_uninstall_bionic( const bionic_id &b_id, Character &installer, bool autodoc,
                                      int skill_level )
{
    // If malfunctioning bionics doesn't have associated item it gets predefined difficulty
    int difficulty = BIONIC_NOITEM_DIFFICULTY;
    if( b_id->itype().is_valid() ) {
        const itype *type = &*b_id->itype();
        if( type->bionic ) {
            difficulty = type->bionic->difficulty;
        }
    }

    if( !has_bionic( b_id ) ) {
        popup( _( "%s don't have this bionic installed." ), disp_name() );
        return false;
    }

    if( ( b_id == bio_reactor ) || ( b_id == bio_advreactor ) ) {
        if( !g->u.query_yn(
                _( "WARNING: Removing a reactor may leave radioactive material!  Remove anyway?" ) ) ) {
            return false;
        }
    }

    for( const bionic &i : get_bionic_collection() ) {
        const bionic_id &bid = i.id;
        if( bid->is_included( b_id ) ) {
            popup( _( "%s must remove the %s bionic to remove the %s." ), installer.disp_name(),
                   bid->name, b_id->name );
            return false;
        }
    }

    // make sure the bionic you are removing is not required by other installed bionics
    std::vector<std::string> dependent_bionics;
    for( const bionic &i : get_bionic_collection() ) {
        const bionic_id &bid = i.id;
        // look at required bionics for every installed bionic
        for( const bionic_id &req_bid : bid->required_bionics ) {
            if( req_bid == b_id ) {
                dependent_bionics.push_back( "" + bid->name );
            }
        }
    }
    if( !dependent_bionics.empty() ) {
        std::string concatenated_list_of_dependent_bionics;
        for( const std::string &req_bionic : dependent_bionics ) {
            concatenated_list_of_dependent_bionics += " " + req_bionic;
            if( req_bionic != dependent_bionics.back() ) {
                concatenated_list_of_dependent_bionics += ",";
            }
        }
        popup( _( "%s cannot be removed because it is required by the following bionics:%s." ),
               b_id->name, concatenated_list_of_dependent_bionics );
        return false;
    }

    if( !b_id->can_uninstall ) {
        popup( _( b_id->no_uninstall_reason ) );
        return false;
    }

    // removal of bionics adds +2 difficulty over installation
    float adjusted_skill;
    if( autodoc ) {
        adjusted_skill = installer.bionics_adjusted_skill( skill_firstaid,
                         skill_computer,
                         skill_electronics,
                         skill_level );
    } else {
        adjusted_skill = installer.bionics_adjusted_skill( skill_electronics,
                         skill_firstaid,
                         skill_mechanics,
                         skill_level );
    }
    int chance_of_success = bionic_manip_cos( adjusted_skill, difficulty + 2 );

    if( chance_of_success >= 100 ) {
        if( !g->u.query_yn(
                _( "Are you sure you wish to uninstall the selected bionic?" ),
                100 - chance_of_success ) ) {
            return false;
        }
    } else {
        if( !g->u.query_yn(
                _( "WARNING: %i percent chance of SEVERE damage to all body parts!  Continue anyway?" ),
                ( 100 - chance_of_success ) ) ) {
            return false;
        }
    }

    return true;
}

bool Character::uninstall_bionic( const bionic_id &b_id, Character &installer, bool autodoc,
                                  int skill_level )
{
    // If malfunctioning bionics doesn't have associated item it gets predefined difficulty
    int difficulty = BIONIC_NOITEM_DIFFICULTY;
    if( b_id->itype().is_valid() ) {
        const itype *type = &*b_id->itype();
        if( type->bionic ) {
            difficulty = type->bionic->difficulty;
        }
    }

    // removal of bionics adds +2 difficulty over installation
    float adjusted_skill;
    int pl_skill;
    if( autodoc ) {
        adjusted_skill = installer.bionics_adjusted_skill( skill_firstaid,
                         skill_computer,
                         skill_electronics,
                         skill_level );
        pl_skill = installer.bionics_pl_skill( skill_firstaid,
                                               skill_computer,
                                               skill_electronics,
                                               skill_level );
    } else {
        adjusted_skill = installer.bionics_adjusted_skill( skill_electronics,
                         skill_firstaid,
                         skill_mechanics,
                         skill_level );
        pl_skill = installer.bionics_pl_skill( skill_electronics,
                                               skill_firstaid,
                                               skill_mechanics,
                                               skill_level );
    }

    int chance_of_success = bionic_manip_cos( adjusted_skill, difficulty + 2 );

    int success = chance_of_success - rng( 1, 100 );
    if( installer.has_trait( trait_DEBUG_BIONICS ) ) {
        perform_uninstall( b_id, difficulty, success, b_id->capacity, pl_skill );
        return true;
    }
    assign_activity( std::make_unique<player_activity>(
                         std::make_unique<operation_activity_actor>(
                             difficulty, success, units::to_kilojoule( b_id->capacity ), pl_skill,
                             "uninstall", b_id, "", autodoc ) ) );
    for( const std::pair<const bodypart_str_id, int> &elem : b_id->occupied_bodyparts ) {
        add_effect( effect_under_op, difficulty * 20_minutes, elem.first, difficulty );
    }

    return true;
}

void Character::perform_uninstall( bionic_id bid, int difficulty, int success,
                                   const units::energy &power_lvl, int pl_skill )
{
    map &here = get_map();
    if( success > 0 ) {
        g->events().send<event_type::removes_cbm>( getID(), bid );

        // until bionics can be flagged as non-removable
        add_msg_player_or_npc( m_neutral, _( "Your parts are jiggled back into their familiar places." ),
                               _( "<npcname>'s parts are jiggled back into their familiar places." ) );
        add_msg( m_good, _( "Successfully removed %s." ), bid.obj().name );
        remove_bionic( bid );

        if( const auto *lcb = bid.obj().lua_callbacks ) {
            lcb->call_on_removed( *this, bid );
        }

        // remove power bank provided by bionic
        mod_max_power_level( -power_lvl );

        detached_ptr<item> cbm;
        if( bid->itype().is_valid() && !bid.obj().has_flag( flag_BIONIC_FAULTY ) ) {
            cbm = item::spawn( bid.c_str() );
            cbm->faults.emplace( fault_bionic_nonsterile );
        } else {
            cbm = item::spawn( itype_burnt_out_bionic );
        }
        here.add_item( bub_pos(), std::move( cbm ) );
    } else {
        g->events().send<event_type::fails_to_remove_cbm>( getID(), bid );
        // for chance_of_success calculation, shift skill down to a float between ~0.4 - 30
        float adjusted_skill = static_cast<float>( pl_skill ) - std::min( static_cast<float>( 40 ),
                               static_cast<float>( pl_skill ) - static_cast<float>( pl_skill ) / static_cast<float>
                               ( 10.0 ) );
        bionics_uninstall_failure( difficulty, success, adjusted_skill );

    }
    here.invalidate_map_cache( g->get_levz() );
}

bool Character::uninstall_bionic( const bionic &target_cbm, monster &installer, Character &patient,
                                  float adjusted_skill )
{
    if( installer.ammo[itype_anesthetic] <= 0 ) {
        if( g->u.sees( installer ) ) {
            add_msg( _( "The %s's anesthesia kit looks empty." ), installer.name() );
        }
        return false;
    }

    const itype_id itemtype = target_cbm.info().itype();
    int difficulty = itemtype.is_valid() ? itemtype->bionic->difficulty : BIONIC_NOITEM_DIFFICULTY;
    int chance_of_success = bionic_manip_cos( adjusted_skill, difficulty + 2 );
    int success = chance_of_success - rng( 1, 100 );

    const time_duration duration = difficulty * 20_minutes;
    // don't stack up the effect
    if( !installer.has_effect( effect_operating ) ) {
        installer.add_effect( effect_operating, duration + 5_turns );
    }

    if( patient.is_player() ) {
        add_msg( m_bad,
                 _( "You feel a tiny pricking sensation in your right arm, and lose all sensation before abruptly blacking out." ) );
    } else if( g->u.sees( installer ) ) {
        add_msg( m_bad,
                 _( "The %1$s gently inserts a syringe into %2$s's arm and starts injecting something while holding them down." ),
                 installer.name(), patient.disp_name() );
    }

    installer.ammo[itype_anesthetic] -= 1;

    patient.add_effect( effect_narcosis, duration );
    patient.add_effect( effect_sleep, duration );

    if( patient.is_player() ) {
        add_msg( _( "You fall asleep and %1$s starts operating." ), installer.disp_name() );
    } else if( g->u.sees( patient ) ) {
        add_msg( _( "%1$s falls asleep and %2$s starts operating." ), patient.disp_name(),
                 installer.disp_name() );
    }

    if( success > 0 ) {

        if( patient.is_player() ) {
            add_msg( m_neutral, _( "Your parts are jiggled back into their familiar places." ) );
            add_msg( m_mixed, _( "Successfully removed %s." ), target_cbm.info().name );
        } else if( patient.is_npc() && g->u.sees( patient ) ) {
            add_msg( m_neutral, _( "%s's parts are jiggled back into their familiar places." ),
                     patient.disp_name() );
            add_msg( m_mixed, _( "Successfully removed %s." ), target_cbm.info().name );
        }

        // remove power bank provided by bionic
        patient.mod_max_power_level( -target_cbm.info().capacity );
        patient.remove_bionic( target_cbm.id );
        const itype_id iid = itemtype.is_valid() &&
                             !target_cbm.info().has_flag( flag_BIONIC_FAULTY ) ? itemtype : itype_burnt_out_bionic;
        detached_ptr<item> cbm = item::spawn( iid, calendar::start_of_cataclysm );

        if( itemtype.is_valid() ) {
            cbm->faults.emplace( fault_bionic_nonsterile );
        }
        get_map().add_item( patient.bub_pos(), std::move( cbm ) );
    } else {
        bionics_uninstall_failure( installer, patient, difficulty, success, adjusted_skill );
    }

    return false;
}

bool Character::can_install_bionics( const itype &type, Character &installer, bool autodoc,
                                     int skill_level )
{
    if( !type.bionic ) {
        debugmsg( "Tried to install NULL bionic" );
        return false;
    }
    if( is_mounted() ) {
        return false;
    }

    const bionic_id &bioid = type.bionic->id;
    const int difficult = type.bionic->difficulty;
    float adjusted_skill;

    if( autodoc ) {
        adjusted_skill = installer.bionics_adjusted_skill( skill_firstaid,
                         skill_computer,
                         skill_electronics,
                         skill_level );
    } else {
        adjusted_skill = installer.bionics_adjusted_skill( skill_electronics,
                         skill_firstaid,
                         skill_mechanics,
                         skill_level );
    }
    int chance_of_success = bionic_manip_cos( adjusted_skill, difficult );

    if( !bioid->required_bionics.empty() ) {
        std::string list_of_missing_required_bionics;
        for( const bionic_id &req_bid : bioid->required_bionics ) {
            if( !has_bionic( req_bid ) ) {
                list_of_missing_required_bionics += " " + req_bid->name;
                if( req_bid != bioid->required_bionics.back() ) {
                    list_of_missing_required_bionics += ",";
                }
            }
        }
        if( !list_of_missing_required_bionics.empty() ) {
            popup( _( "CBM requires prior installation of%s." ), list_of_missing_required_bionics );
            return false;
        }
    }

    std::vector<std::string> conflicting_muts;
    for( const trait_id &mid : bioid->canceled_mutations ) {
        if( has_trait( mid ) ) {
            conflicting_muts.push_back( mid->name() );
        }
    }

    if( !conflicting_muts.empty() &&
        !g->u.query_yn(
            _( "Installing this bionic will remove the conflicting traits: %s.  Continue anyway?" ),
            enumerate_as_string( conflicting_muts ) ) ) {
        return false;
    }

    const std::map<bodypart_id, int> &issues = bionic_installation_issues( bioid );
    // show all requirements which are not satisfied
    if( !issues.empty() ) {
        std::string detailed_info;
        for( auto &elem : issues ) {
            //~ <Body part name>: <number of slots> more slot(s) needed.
            detailed_info += string_format( _( "\n%s: %i more slot(s) needed." ),
                                            body_part_name_as_heading( elem.first->token, 1 ),
                                            elem.second );
        }
        popup( _( "Not enough space for bionic installation!%s" ), detailed_info );
        return false;
    }

    if( chance_of_success >= 100 ) {
        if( !g->u.query_yn(
                _( "Are you sure you wish to install the selected bionic?" ),
                100 - chance_of_success ) ) {
            return false;
        }
    } else {
        if( autodoc ) {
            if( !g->u.query_yn(
                    _( "WARNING: There is a %i percent chance of complications, such as damage or faulty installation!  Continue anyway?" ),
                    ( 100 - chance_of_success ) ) ) {
                return false;
            }
        } else {
            if( !g->u.query_yn(
                    _( "WARNING: There is a %i percent chance of complications, such as damage or faulty installation!  The following skills affect self-installation: First Aid, Electronics, and Mechanics.\n\nContinue anyway?" ),
                    ( 100 - chance_of_success ) ) ) {
                return false;
            }
        }
    }

    return true;
}

float Character::env_surgery_bonus( int radius )
{
    float bonus = 1.0;
    map &here = get_map();
    for( const auto &cell : here.points_in_radius( bub_pos(), radius ) ) {
        if( here.furn( cell )->surgery_skill_multiplier ) {
            bonus = std::max( bonus, *here.furn( cell )->surgery_skill_multiplier );
        }
    }
    return bonus;
}

bool Character::install_bionics( const itype &type, Character &installer, bool autodoc,
                                 int skill_level )
{
    if( !type.bionic ) {
        debugmsg( "Tried to install NULL bionic" );
        return false;
    }

    const bionic_id &bioid = type.bionic->id;
    const bionic_id &upbioid = bioid->upgraded_bionic;
    const int difficulty = type.bionic->difficulty;
    float adjusted_skill;
    int pl_skill;
    if( autodoc ) {
        adjusted_skill = installer.bionics_adjusted_skill( skill_firstaid,
                         skill_computer,
                         skill_electronics,
                         skill_level );
        pl_skill = installer.bionics_pl_skill( skill_firstaid,
                                               skill_computer,
                                               skill_electronics,
                                               skill_level );
    } else {
        adjusted_skill = installer.bionics_adjusted_skill( skill_electronics,
                         skill_firstaid,
                         skill_mechanics,
                         skill_level );
        pl_skill = installer.bionics_pl_skill( skill_electronics,
                                               skill_firstaid,
                                               skill_mechanics,
                                               skill_level );
    }
    int chance_of_success = bionic_manip_cos( adjusted_skill, difficulty );

    // Practice skills only if conducting manual installation
    if( !autodoc ) {
        installer.practice( skill_electronics, static_cast<int>( ( 100 - chance_of_success ) * 1.5 ) );
        installer.practice( skill_firstaid, static_cast<int>( ( 100 - chance_of_success ) * 1.0 ) );
        installer.practice( skill_mechanics, static_cast<int>( ( 100 - chance_of_success ) * 0.5 ) );
    }

    int success = chance_of_success - rng( 0, 99 );
    if( installer.has_trait( trait_DEBUG_BIONICS ) ) {
        perform_install( bioid, upbioid, difficulty, success, pl_skill, "NOT_MED",
                         bioid->canceled_mutations );
        return true;
    }
    const std::string installer_name = ( installer.has_trait( trait_PROF_MED ) ||
                                         installer.has_trait( trait_PROF_AUTODOC ) )
                                       ? installer.disp_name( true )
                                       : "NOT_MED";
    assign_activity( std::make_unique<player_activity>(
                         std::make_unique<operation_activity_actor>(
                             difficulty, success, units::to_joule( bioid->capacity ), pl_skill,
                             "install", bioid, installer_name, autodoc ) ) );
    for( const std::pair<const bodypart_str_id, int> &elem : bioid->occupied_bodyparts ) {
        add_effect( effect_under_op, difficulty * 20_minutes, elem.first, difficulty );
    }

    return true;
}

void Character::perform_install( bionic_id bid, bionic_id upbid, int difficulty, int success,
                                 int pl_skill, const std::string &installer_name,
                                 const std::vector<trait_id> &trait_to_rem )
{

    g->events().send<event_type::installs_cbm>( getID(), bid );
    if( upbid != bionic_id( "" ) ) {
        remove_bionic( upbid );
        //~ %1$s - name of the bionic to be upgraded (inferior), %2$s - name of the upgraded bionic (superior).
        add_msg( m_good, _( "Upgraded %1$s to %2$s." ),
                 upbid.obj().name, bid.obj().name );
    } else {
        //~ %s - name of the bionic.
        add_msg( m_good, _( "Installed %s." ), bid.obj().name );
    }

    add_bionic( bid );

    if( const auto *lcb = bid.obj().lua_callbacks ) {
        lcb->call_on_installed( *this, bid );
    }

    if( !trait_to_rem.empty() ) {
        for( const trait_id &tid : trait_to_rem ) {
            if( has_trait( tid ) ) {
                remove_mutation( tid );
            }
        }
    }
    if( success <= 0 ) {
        g->events().send<event_type::fails_to_install_cbm>( getID(), bid );

        // for chance_of_success calculation, shift skill down to a float between ~0.4 - 30
        float adjusted_skill = static_cast<float>( pl_skill ) - std::min( static_cast<float>( 40 ),
                               static_cast<float>( pl_skill ) - static_cast<float>( pl_skill ) / static_cast<float>
                               ( 10.0 ) );
        bionics_install_failure( installer_name, difficulty, success, adjusted_skill );
    }
    get_map().invalidate_map_cache( g->get_levz() );
}

void Character::do_damage_for_bionic_failure( int min_damage, int max_damage )
{
    std::set<bodypart_id> bp_hurt;
    for( const bodypart_id &bp : get_all_body_parts() ) {
        if( has_effect( effect_under_op, bp.id() ) ) {
            if( bp_hurt.contains( bp->main_part ) ) {
                continue;
            }
            bp_hurt.emplace( bp->main_part );
        }
    }

    if( bp_hurt.empty() ) {
        // If no bodypart associetd with bionic- just damage torso.
        // Special check for power storage - it does not belong to any body part.
        bp_hurt.emplace( bodypart_str_id( "torso" ) );
    }

    for( const bodypart_id &bp : bp_hurt ) {
        int damage = rng( min_damage, max_damage );
        int hp = get_hp( bp );
        if( damage >= hp && ( bp == bodypart_str_id( "head" ) || bp == bodypart_str_id( "torso" ) ) ) {
            add_effect( effect_infected, 1_hours, bp.id() );
            add_msg_player_or_npc( m_bad, _( "Your %s is infected." ), _( "<npcname>'s %s is infected." ),
                                   body_part_name_accusative( bp ) );
            damage = hp * 0.8f;
        }
        apply_damage( this, bp, damage, true );
        if( damage > 15 )
            add_msg_player_or_npc( m_bad, _( "Your %s is severely damaged." ),
                                   _( "<npcname>'s %s is severely damaged." ),
                                   body_part_name_accusative( bp ) );
        else
            add_msg_player_or_npc( m_bad, _( "Your %s is damaged." ), _( "<npcname>'s %s is damaged." ),
                                   body_part_name_accusative( bp ) );

    }
}


void Character::bionics_install_failure( const std::string &installer,
        int difficulty, int success, float adjusted_skill )
{
    // "success" should be passed in as a negative integer representing how far off we
    // were for a successful install.  We use this to determine consequences for failing.
    success = std::abs( success );

    // failure level is decided by how far off the character was from a successful install, and
    // this is scaled up or down by the ratio of difficulty/skill.  At high skill levels (or low
    // difficulties), only minor consequences occur.  At low skill levels, severe consequences
    // are more likely.
    int failure_level = static_cast<int>( std::sqrt( success * 4.0 * difficulty / adjusted_skill ) );
    int fail_type = ( failure_level > 5 ? 5 : failure_level );

    if( installer != "NOT_MED" ) {
        //~"Complications" is USian medical-speak for "unintended damage from a medical procedure".
        add_msg( m_neutral, _( "%s training helps to minimize the complications." ),
                 installer );
        // In addition to the bonus, medical residents know enough OR protocol to avoid botching.
        // Take MD and be immune to faulty bionics.
        if( fail_type > 3 ) {
            fail_type = rng( 1, 3 );
        }
    }

    switch( fail_type ) {
        case 0:
        case 1:
            do_damage_for_bionic_failure( 2, 5 );
            break;
        case 2:
        case 3:
            do_damage_for_bionic_failure( 5, difficulty * 5 );
            break;
        case 4:
        case 5: {
            std::vector<bionic_id> valid;
            std::ranges::copy_if( faulty_bionics, std::back_inserter( valid ),
            [&]( const bionic_id & id ) {
                return !has_bionic( id );
            } );

            // We've got all the bad bionics!
            if( valid.empty() ) {
                if( has_max_power() ) {
                    units::energy old_power = get_max_power_level();
                    add_msg( m_bad, _( "%s lose power capacity!" ), disp_name() );
                    set_max_power_level( units::from_kilojoule( rng( 0,
                                         units::to_kilojoule( get_max_power_level() ) - 25 ) ) );
                    if( get_max_power_level() < units::from_kilojoule( 0 ) ) {
                        set_max_power_level( units::from_kilojoule( 0 ) );
                    }
                    if( is_player() ) {
                        g->memorial().add(
                            pgettext( "memorial_male", "Lost %d units of power capacity." ),
                            pgettext( "memorial_female", "Lost %d units of power capacity." ),
                            units::to_kilojoule( old_power - get_max_power_level() ) );
                    }
                    // If no faults available and no power capacity, downgrade to second-worst complication.
                } else {
                    do_damage_for_bionic_failure( 5, difficulty * 5 );
                    break;
                }
            } else {
                const bionic_id &id = random_entry( valid );
                add_bionic( id );
                g->events().send<event_type::installs_faulty_cbm>( getID(), id );
                add_msg( m_bad,
                         _( "Complication in installation caused a malfunction - %s.  Uninstall it to clear the malfunction." ),
                         id.obj().name );
            }
        }
        break;
    }

}

std::string list_occupied_bps( const bionic_id &bio_id, const std::string &intro,
                               const bool each_bp_on_new_line )
{
    if( bio_id->occupied_bodyparts.empty() ) {
        return "";
    }
    std::string desc = intro;
    for( const std::pair<const bodypart_str_id, int> &elem : bio_id->occupied_bodyparts ) {
        desc += ( each_bp_on_new_line ? "\n" : " " );
        //~ <Bodypart name> (<number of occupied slots> slots);
        desc += string_format( _( "%s (%i slots);" ),
                               body_part_name_as_heading( elem.first->token, 1 ),
                               elem.second );
    }
    return desc;
}

int Character::get_used_bionics_slots( const bodypart_id &bp ) const
{
    int used_slots = 0;
    for( const bionic &i : get_bionic_collection() ) {
        const bionic_id &bid = i.id;
        auto search = bid->occupied_bodyparts.find( bp.id() );
        if( search != bid->occupied_bodyparts.end() ) {
            used_slots += search->second;
        }
    }

    return used_slots;
}

std::map<bodypart_id, int> Character::bionic_installation_issues( const bionic_id &bioid ) const
{
    std::map<bodypart_id, int> issues;
    if( !get_option < bool >( "CBM_SLOTS_ENABLED" ) ) {
        return issues;
    }
    for( const std::pair<const bodypart_str_id, int> &elem : bioid->occupied_bodyparts ) {
        int lacked_slots = elem.second - get_free_bionics_slots( elem.first );
        if( bioid->upgraded_bionic ) {
            lacked_slots -= bioid->upgraded_bionic->occupied_bodyparts.at( elem.first );
        }
        if( lacked_slots > 0 ) {
            issues.emplace( elem.first, lacked_slots );
        }
    }
    return issues;
}

int Character::get_total_bionics_slots( const bodypart_id &bp ) const
{
    return bp->bionic_slots();
}

int Character::get_free_bionics_slots( const bodypart_id &bp ) const
{
    return get_total_bionics_slots( bp ) - get_used_bionics_slots( bp );
}

bool cbm_needs_anesthesia( const Character &who )
{
    return !( who.has_bionic( bio_painkiller ) || who.has_trait( trait_NOPAIN ) ||
              who.has_trait( trait_DEBUG_BIONICS ) );
}

bool has_enough_anesthesia( const itype *cbm, Character &doc, const Character &patient )
{
    if( !cbm->bionic ) {
        debugmsg( "has_enough_anesthesia( const itype *cbm ): %s is not a bionic", cbm->get_id() );
        return false;
    }

    if( !cbm_needs_anesthesia( patient ) ) {
        return true;
    }

    const int weight = 7;
    const requirement_data req_anesth = *requirement_id( "anesthetic" ) *
                                        cbm->bionic->difficulty * 2 * weight;

    return req_anesth.can_make_with_inventory( doc.crafting_inventory(), is_crafting_component );
}

void Character::add_bionic( const bionic_id &b )
{
    if( !b->has_flag( flag_MULTIINSTALL ) && has_bionic( b ) ) {
        debugmsg( "Tried to install bionic %s that is already installed!", b.c_str() );
        return;
    }

    const units::energy pow_up = b->capacity;
    mod_max_power_level( pow_up );
    if( pow_up != 0_J ) {
        add_msg_if_player( m_good, _( "Increased storage capacity by %i." ),
                           units::to_kilojoule( pow_up ) );
    }

    const auto invlet = b.obj().activated ? get_free_invlet( *my_bionics ) : ' ';
    my_bionics->push_back( bionic( b, invlet ) );
    if( b->has_flag( flag_INITIALLY_ACTIVATE ) ) {
        activate_bionic( my_bionics->back() );
    }

    for( const bionic_id &inc_bid : b->included_bionics ) {
        add_bionic( inc_bid );
    }

    for( const std::pair<const spell_id, int> &spell_pair : b->learned_spells ) {
        const spell_id learned_spell = spell_pair.first;
        if( learned_spell->spell_class != trait_id( "NONE" ) ) {
            const trait_id spell_class = learned_spell->spell_class;
            // spells you learn from a bionic overwrite the opposite spell class.
            // for best UX, include those spell classes in "canceled_mutations"
            if( !has_trait( spell_class ) ) {
                set_mutation( spell_class );
                on_mutation_gain( spell_class );
                add_msg_if_player( spell_class->desc() );
            }
        }
        if( !magic->knows_spell( learned_spell ) ) {
            magic->learn_spell( learned_spell, *this, true );
        }
        spell &known_spell = magic->get_spell( learned_spell );
        // spells you learn from installing a bionic upgrade spells you know if they are the same
        if( known_spell.get_level() < spell_pair.second ) {
            known_spell.set_level( spell_pair.second );
        }
    }

    reset_encumbrance();
    recalc_sight_limits();
    if( !b->enchantments.empty() ) {
        recalculate_enchantment_cache();
    }
}

void Character::remove_bionic( const bionic_id &b )
{
    bionic_collection new_my_bionics;
    // any spells you should not forget due to still having a bionic installed that has it.
    std::set<spell_id> cbm_spells;
    std::set<bionic_id> removed_bionics;
    for( bionic &i : *my_bionics ) {

        // Remove the specified bionic and any linked bionics
        if( ( b == i.id || b->is_included( i.id ) || i.id->is_included( b ) ) &&
            !removed_bionics.contains( i.id ) ) {
            const units::energy pow_up = i.id->capacity;
            mod_max_power_level( -1 * pow_up );
            if( i.powered ) {
                deactivate_bionic( i, true );
            }
            removed_bionics.emplace( i.id );
            continue;
        }

        for( const std::pair<const spell_id, int> &spell_pair : i.id->learned_spells ) {
            cbm_spells.emplace( spell_pair.first );
        }

        new_my_bionics.push_back( i );
    }

    // any spells you learn from installing a bionic you forget.
    for( const std::pair<const spell_id, int> &spell_pair : b->learned_spells ) {
        if( !cbm_spells.contains( spell_pair.first ) ) {
            magic->forget_spell( spell_pair.first );
        }
    }

    *my_bionics = new_my_bionics;
    reset_encumbrance();
    recalc_sight_limits();
    if( !b->enchantments.empty() ) {
        recalculate_enchantment_cache();
    }
}

bool Character::has_bionics() const
{
    return !my_bionics->empty() || has_max_power();
}

void Character::clear_bionics()
{
    my_bionics->clear();
}

void bionic::set_flag( const std::string &flag )
{
    bionic_tags.insert( flag );
}

void bionic::remove_flag( const std::string &flag )
{
    bionic_tags.erase( flag );
}

bool bionic::has_flag( const std::string &flag ) const
{
    return bionic_tags.find( flag ) != bionic_tags.end();
}

int bionic::get_quality( const quality_id &quality ) const
{
    const auto &i = info();
    if( i.fake_item.is_empty() ) {
        return INT_MIN;
    }

    //TODO!: move some logic up to item type
    return item::spawn_temporary( i.fake_item )->get_quality( quality );
}

bool bionic::is_this_fuel_powered( const itype_id &this_fuel ) const
{
    const std::vector<itype_id> fuel_op = info().fuel_opts;
    return std::ranges::contains( fuel_op, this_fuel );
}

void bionic::toggle_safe_fuel_mod()
{
    if( info().fuel_opts.empty() && !info().is_remote_fueled ) {
        return;
    }
    if( !has_flag( flag_SAFE_FUEL_OFF ) ) {
        set_flag( flag_SAFE_FUEL_OFF );
    } else {
        remove_flag( flag_SAFE_FUEL_OFF );
    }
}

void bionic::toggle_auto_start_mod()
{
    if( info().fuel_opts.empty() && !info().is_remote_fueled ) {
        return;
    }
    if( !is_auto_start_on() ) {
        uilist tmenu;
        tmenu.text = _( "Chose Start Power Level Threshold" );
        tmenu.addentry( 1, true, 'o', _( "No Power Left" ) );
        tmenu.addentry( 2, true, 't', _( "Below 25 %%" ) );
        tmenu.addentry( 3, true, 'f', _( "Below 50 %%" ) );
        tmenu.addentry( 4, true, 's', _( "Below 75 %%" ) );
        tmenu.addentry( 5, true, 'a', _( "Below 99 %%" ) );
        tmenu.query();

        switch( tmenu.ret ) {
            case 1:
                set_auto_start_thresh( 0.0 );
                break;
            case 2:
                set_auto_start_thresh( 0.25 );
                break;
            case 3:
                set_auto_start_thresh( 0.5 );
                break;
            case 4:
                set_auto_start_thresh( 0.75 );
                break;
            case 5:
                set_auto_start_thresh( 0.99 );
                break;
            default:
                break;
        }
    } else {
        set_auto_start_thresh( -1.0 );
    }
}

void bionic::set_auto_start_thresh( float val )
{
    auto_start_threshold = val;
}

float bionic::get_auto_start_thresh() const
{
    return auto_start_threshold;
}

bool bionic::is_auto_start_on() const
{
    return get_auto_start_thresh() > -1.0;
}

bool bionic::is_auto_start_keep_full() const
{
    return get_auto_start_thresh() > 0.99;
}

void bionic::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "id", id );
    json.member( "invlet", static_cast<int>( invlet ) );
    json.member( "powered", powered );
    json.member( "charge", charge_timer );
    json.member( "ammo_loaded", ammo_loaded );
    json.member( "ammo_count", ammo_count );
    json.member( "bionic_tags", bionic_tags );
    if( incapacitated_time > 0_turns ) {
    json.member( "incapacitated_time", incapacitated_time );
    }
    if( is_auto_start_on() ) {
    json.member( "auto_start_threshold", auto_start_threshold );
    }
    if( energy_stored > 0_kJ ) {
    json.member( "energy_stored", energy_stored );
    }
    json.member( "show_sprite", show_sprite );

    json.end_object();
}

void bionic::deserialize( JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    id = bionic_id( jo.get_string( "id" ) );
    invlet = jo.get_int( "invlet" );
    powered = jo.get_bool( "powered" );
    charge_timer = jo.get_int( "charge" );
    jo.read( "energy_stored", energy_stored, true );
    if( jo.has_string( "ammo_loaded" ) ) {
        jo.read( "ammo_loaded", ammo_loaded, true );
    }
    if( jo.has_int( "ammo_count" ) ) {
        ammo_count = jo.get_int( "ammo_count" );
    }
    if( jo.has_int( "incapacitated_time" ) ) {
        incapacitated_time = 1_turns * jo.get_int( "incapacitated_time" );
    }
    if( jo.has_float( "auto_start_threshold" ) ) {
        auto_start_threshold = jo.get_float( "auto_start_threshold" );
    }
    if( jo.has_bool( "show_sprite" ) ) {
        show_sprite = jo.get_bool( "show_sprite" );
    }
    if( jo.has_array( "bionic_tags" ) ) {
        for( const std::string line : jo.get_array( "bionic_tags" ) ) {
            bionic_tags.insert( line );
        }
    }

}

std::vector<bionic_id> bionics_cancelling_trait( const std::vector<bionic_id> &bios,
        const trait_id &tid )
{
    // Vector of bionics to return
    std::vector<bionic_id> bionics_cancelling;

    // Search through the vector of of bionics, and see if the trait is cancelled by one of them
    for( const bionic_id &bid : bios ) {
        for( const trait_id &trait : bid->canceled_mutations ) {
            if( trait == tid ) {
                bionics_cancelling.emplace_back( bid );
            }
        }
    }

    return bionics_cancelling;
}

void Character::introduce_into_anesthesia( const time_duration &duration, Character &installer,
        bool needs_anesthesia )   //used by the Autodoc
{
    if( installer.has_trait( trait_DEBUG_BIONICS ) ) {
        installer.add_msg_if_player( m_info,
                                     _( "You tell the pain to bug off and proceed with the operation." ) );
        return;
    }
    installer.add_msg_player_or_npc( m_info,
                                     _( "You set up the operation step-by-step, configuring the Autodoc to manipulate a CBM." ),
                                     _( "<npcname> sets up the operation, configuring the Autodoc to manipulate a CBM." ) );
    if( needs_anesthesia ) {
        add_msg_player_or_npc( m_info,
                               _( "You settle into position, sliding your right wrist into the couch's strap." ),
                               _( "<npcname> settles into position, sliding their wrist into the couch's strap." ) );

        //post-threshold medical mutants do not fear operations.
        if( has_trait( trait_THRESH_MEDICAL ) ) {
            add_msg_if_player( m_mixed,
                               _( "You feel excited as the operation starts." ) );
        }

        add_msg_if_player( m_mixed,
                           _( "You feel a tiny pricking sensation in your right arm, and lose all sensation before abruptly blacking out." ) );

        //Pain junkies feel sorry about missed pain from operation.
        if( has_trait( trait_MASOCHIST ) || has_trait( trait_MASOCHIST_MED ) ||
            has_trait( trait_CENOBITE ) ) {
            add_msg_if_player( m_mixed,
                               _( "As your consciousness slips away, you feel regret that you won't be able to enjoy the operation." ) );
        }

        //post-threshold medical mutants with Deadened don't need anesthesia due to their inability to feel pain
    } else {
        //post-threshold medical mutants do not fear operations.
        if( has_trait( trait_THRESH_MEDICAL ) ) {
            add_msg_if_player( m_mixed,
                               _( "You feel excited as the Autodoc slices painlessly into you.  You enjoy the sight of scalpels slicing you apart." ) );
        } else {
            add_msg_if_player( m_mixed,
                               _( "You stay very, very still, intently staring off into space, as the Autodoc slices painlessly into you." ) );
        }
    }

    if( has_effect( effect_narcosis ) ) {
        const time_duration remaining_time = get_effect_dur( effect_narcosis );
        if( remaining_time <= duration ) {
            const time_duration top_off_time = duration - remaining_time;
            add_effect( effect_narcosis, top_off_time );
            fall_asleep( top_off_time );
        }
    } else {
        add_effect( effect_narcosis, duration );
        fall_asleep( duration );
    }
}
// NOTE: Not toggling in the sense of activation
// Instead toggling in the sense of having it
void Character::toggle_bionic( const bionic_id &bio )
{
    if( has_bionic( bio ) ) {
        remove_bionic( bio );
    } else {
        add_bionic( bio );
    }
}

