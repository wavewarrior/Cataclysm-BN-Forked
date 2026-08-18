#include "iexamine.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <map>
#include <memory>
#include <ranges>
#include <set>
#include <type_traits>
#include <utility>

#include "action.h"
#include "activity_actor.h"
#include "activity_actor_definitions.h"
#include "active_tile_data_def.h"
#include "ammo.h"
#include "avatar.h"
#include "avatar_action.h"
#include "avatar_functions.h"
#include "bionics.h"
#include "bodypart.h"
#include "cached_options.h"
#include "calendar.h"
#include "cata_unreachable.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "character.h"
#include "character_functions.h"
#include "data_vars.h"
#include "detached_ptr.h"
#include "flag.h"
#include "color.h"
#include "construction.h"
#include "construction_group.h"
#include "construction_partial.h"
#include "craft_command.h"
#include "cursesdef.h"
#include "damage.h"
#include "debug.h"
#include "distribution_grid.h"
#include "effect.h"
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "field_type.h"
#include "flat_set.h"
#include "flood_fill.h"
#include "fungal_effects.h"
#include "game.h"
#include "game_constants.h"
#include "game_inventory.h"
#include "handle_liquid.h"
#include "harvest.h"
#include "input.h"
#include "int_id.h"
#include "inventory.h"
#include "item.h"
#include "item_contents.h"
#include "item_stack.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "line.h"
#include "magic_teleporter_list.h"
#include "map.h"
#include "map_iterator.h"
#include "map_selector.h"
#include "map_functions.h"
#include "map_utils.h"
#include "mapdata.h"
#include "mapbuffer.h"
#include "mapbuffer_registry.h"
#include "material.h"
#include "messages.h"
#include "submap.h"
#include "monster.h"
#include "mongroup.h"
#include "mtype.h"
#include "mutation.h"
#include "npc.h"
#include "options.h"
#include "output.h"
#include "overmapbuffer.h"
#include "pickup.h"
#include "fluid_grid.h"
#include "pimpl.h"
#include "player.h"
#include "player_activity.h"
#include "pldata.h"
#include "point.h"
#include "recipe.h"
#include "relic.h"
#include "requirements.h"
#include "rng.h"
#include "sounds.h"
#include "cloning_utils.h"
#include "string_formatter.h"
#include "string_id.h"
#include "string_input_popup.h"
#include "timed_event.h"
#include "translations.h"
#include "trap.h"
#include "ui.h"
#include "ui_manager.h"
#include "uistate.h"
#include "units.h"
#include "units_utility.h"
#include "recipe_dictionary.h"
#include "value_ptr.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vpart_position.h"
#include "weather.h"
#include "world_type.h"
#include "dimension_info.h"
#include "overmap.h"
#include "veh_type.h"

#include <RmlUi/Core.h>
#include "rml_screen.h"
#include "rml_util.h"

static const activity_id ACT_ATM( "ACT_ATM" );
static const activity_id ACT_CRACKING( "ACT_CRACKING" );

static const efftype_id effect_antibiotic( "antibiotic" );
static const efftype_id effect_bite( "bite" );
static const efftype_id effect_bleed( "bleed" );
static const efftype_id effect_disinfected( "disinfected" );
static const efftype_id effect_earphones( "earphones" );
static const efftype_id effect_infected( "infected" );
static const efftype_id effect_pblue( "pblue" );
static const efftype_id effect_pkill2( "pkill2" );
static const efftype_id effect_sleep( "sleep" );
static const efftype_id effect_strong_antibiotic( "strong_antibiotic" );
static const efftype_id effect_teleglow( "teleglow" );
static const efftype_id effect_weak_antibiotic( "weak_antibiotic" );

static const itype_id itype_2x4( "2x4" );
static const itype_id itype_arm_splint( "arm_splint" );
static const itype_id itype_battery( "battery" );
static const itype_id itype_bot_broken_cyborg( "bot_broken_cyborg" );
static const itype_id itype_bot_prototype_cyborg( "bot_prototype_cyborg" );
static const itype_id itype_cash_card( "cash_card" );
static const itype_id itype_money_bundle( "money_bundle" );
static const itype_id itype_charcoal( "charcoal" );
static const itype_id itype_chem_carbide( "chem_carbide" );
static const itype_id itype_water( "water" );
static const itype_id itype_water_clean( "water_clean" );
static const itype_id itype_corpse( "corpse" );
static const itype_id itype_electrohack( "electrohack" );
static const auto itype_plumber_toolkit = itype_id( "plumber_toolkit" );
static const itype_id itype_fake_milling_item( "fake_milling_item" );
static const itype_id itype_fake_smoke_plume( "fake_smoke_plume" );
static const itype_id itype_fertilizer( "fertilizer" );
static const itype_id itype_fire( "fire" );
static const itype_id itype_fungal_seeds( "fungal_seeds" );
static const itype_id itype_grapnel( "grapnel" );
static const itype_id itype_hickory_root( "hickory_root" );
static const itype_id itype_id_industrial( "id_industrial" );
static const itype_id itype_id_military( "id_military" );
static const itype_id itype_id_science( "id_science" );
static const itype_id itype_leg_splint( "leg_splint" );
static const itype_id itype_maple_sap( "maple_sap" );
static const itype_id itype_marloss_berry( "marloss_berry" );
static const itype_id itype_marloss_seed( "marloss_seed" );
static const itype_id itype_mycus_fruit( "mycus_fruit" );
static const itype_id itype_nail( "nail" );
static const itype_id itype_petrified_eye( "petrified_eye" );
static const itype_id itype_plut_generator_item( "plut_generator_item" );
static const itype_id itype_sheet( "sheet" );
static const itype_id itype_stick( "stick" );
static const itype_id itype_string_36( "string_36" );
static const itype_id itype_tree_spile( "tree_spile" );
static const itype_id itype_unfinished_cac2( "unfinished_cac2" );
static const itype_id itype_unfinished_charcoal( "unfinished_charcoal" );
static const itype_id itype_UPS( "UPS" );
static const itype_id itype_dna( "dna" );
static const itype_id itype_embryo( "embryo" );
static const itype_id itype_embryo_empty( "embryo_empty" );

static const trap_str_id tr_unfinished_construction( "tr_unfinished_construction" );

static const skill_id skill_computer( "computer" );
static const skill_id skill_cooking( "cooking" );
static const skill_id skill_electronics( "electronics" );
static const skill_id skill_fabrication( "fabrication" );
static const skill_id skill_firstaid( "firstaid" );
static const skill_id skill_mechanics( "mechanics" );
static const skill_id skill_survival( "survival" );

static const ter_str_id t_dimensional_portal( "t_dimensional_portal" );
static const ter_str_id t_web_bridge( "t_web_bridge" );

static const trait_id trait_AMORPHOUS( "AMORPHOUS" );
static const trait_id trait_ARACHNID_ARMS_OK( "ARACHNID_ARMS_OK" );
static const trait_id trait_BADKNEES( "BADKNEES" );
static const trait_id trait_BEAK_HUM( "BEAK_HUM" );
static const trait_id trait_BURROW( "BURROW" );
static const trait_id trait_DEBUG_HS( "DEBUG_HS" );
static const trait_id trait_ILLITERATE( "ILLITERATE" );
static const trait_id trait_INSECT_ARMS_OK( "INSECT_ARMS_OK" );
static const trait_id trait_M_DEFENDER( "M_DEFENDER" );
static const trait_id trait_M_DEPENDENT( "M_DEPENDENT" );
static const trait_id trait_M_FERTILE( "M_FERTILE" );
static const trait_id trait_M_SPORES( "M_SPORES" );
static const trait_id trait_PROBOSCIS( "PROBOSCIS" );
static const trait_id trait_THRESH_MARLOSS( "THRESH_MARLOSS" );
static const trait_id trait_THRESH_MYCUS( "THRESH_MYCUS" );
static const trait_id trait_WEB_BRIDGE( "WEB_BRIDGE" );

static const quality_id qual_ANESTHESIA( "ANESTHESIA" );
static const quality_id qual_DIG( "DIG" );
static const quality_id qual_LOCKPICK( "LOCKPICK" );

static const mtype_id mon_broken_cyborg( "mon_broken_cyborg" );
static const mtype_id mon_dark_wyrm( "mon_dark_wyrm" );
static const mtype_id mon_fungal_blossom( "mon_fungal_blossom" );
static const mtype_id mon_prototype_cyborg( "mon_prototype_cyborg" );
static const mtype_id mon_spider_cellar_giant_s( "mon_spider_cellar_giant_s" );
static const mtype_id mon_spider_web_s( "mon_spider_web_s" );
static const mtype_id mon_spider_widow_giant_s( "mon_spider_widow_giant_s" );

static const bionic_id bio_fingerhack( "bio_fingerhack" );
static const bionic_id bio_lighter( "bio_lighter" );
static const bionic_id bio_lockpick( "bio_lockpick" );
static const bionic_id bio_tools( "bio_tools" );

static const itype_id itype_toolset( "toolset" );

static const std::string flag_AUTODOC( "AUTODOC" );
static const std::string flag_AUTODOC_COUCH( "AUTODOC_COUCH" );
static const std::string flag_BARRICADABLE_WINDOW_CURTAINS( "BARRICADABLE_WINDOW_CURTAINS" );
static const std::string flag_CLIMBABLE( "CLIMBABLE" );
static const std::string flag_CLIMB_SIMPLE( "CLIMB_SIMPLE" );
static const std::string flag_GROWTH_HARVEST( "GROWTH_HARVEST" );
static const std::string flag_OPENCLOSE_INSIDE( "OPENCLOSE_INSIDE" );
static const std::string flag_WALL( "WALL" );

// @TODO maybe make this a property of the item (depend on volume/type)
static const time_duration milling_time = 6_hours;

/**
 * Nothing player can interact with here.
 */

static int count_charges_in_list( const itype *type, const map_stack &items )
{
    for( const auto &candidate : items ) {
        if( candidate->type == type ) {
            return candidate->charges;
        }
    }
    return 0;
}

namespace sm_rack
{
const int MIN_CHARCOAL = 100;
const int CHARCOAL_PER_LITER = 25;
const units::volume MAX_FOOD_VOLUME_MILLING = units::from_liter( 100 );
const units::volume MAX_FOOD_VOLUME = units::from_liter( 20 );
const units::volume MAX_FOOD_VOLUME_PORTABLE = units::from_liter( 15 );
} // namespace sm_rack

static player &player_on_couch( player &p, const tripoint_bub_ms &autodoc_loc, player &null_patient,
                                bool &adjacent_couch, tripoint_bub_ms &couch_pos )
{
    map &here = get_map();
    for( const auto &couch_loc : here.find_furnitures_or_vparts_with_flag_in_radius( autodoc_loc, 1,
            flag_AUTODOC_COUCH ) ) {
        adjacent_couch = true;
        couch_pos = couch_loc;
        if( p.bub_pos() == couch_loc ) {
            return p;
        }
        for( const npc *e : g->allies() ) {
            if( e->bub_pos() == couch_loc ) {
                return  *g->critter_by_id<player>( e->getID() );
            }
        }
    }
    return null_patient;
}

static Character &operator_present( Character &p, const tripoint_bub_ms &autodoc_loc,
                                    Character &null_patient )
{
    map &here = get_map();
    for( const auto &loc : here.points_in_radius( autodoc_loc, 1 ) ) {
        if( !here.has_flag_furn_or_vpart( flag_AUTODOC_COUCH, loc ) ) {
            if( p.bub_pos() == loc ) {
                return p;
            }
            for( const npc *e : g->allies() ) {
                if( e->bub_pos() == loc ) {
                    return  *g->critter_by_id<player>( e->getID() );
                }
            }
        }
    }
    return null_patient;
}

static item *cyborg_on_couch( const tripoint_bub_ms &couch_pos )
{
    for( item * const &it : get_map().i_at( couch_pos ) ) {
        if( it->typeId() == itype_bot_broken_cyborg || it->typeId() == itype_bot_prototype_cyborg ) {
            return it;
        }
        if( it->typeId() == itype_corpse ) {
            if( it->get_mtype()->id == mon_broken_cyborg || it->get_mtype()->id == mon_prototype_cyborg ) {
                return it;
            }
        }
    }
    // if we're in a autodoc couch on a vehicle, go through the items in it, and return the item if's a cyborg
    if( const std::optional<vpart_reference> vp = get_map().veh_at( couch_pos ).part_with_feature(
            flag_AUTODOC_COUCH, false ) ) {
        auto dest_veh = &vp->vehicle();
        int dest_part = vp->part_index();
        for( item * const &it : dest_veh->get_items( dest_part ) ) {
            if( it->typeId() == itype_bot_broken_cyborg || it->typeId() == itype_bot_prototype_cyborg ) {
                return it;
            }
            if( it->typeId() == itype_corpse ) {
                if( it->get_mtype()->id == mon_broken_cyborg || it->get_mtype()->id == mon_prototype_cyborg ) {
                    return it;
                }
            }
        }
    }
    return nullptr;
}

static player &best_installer( player &p, player &null_player, int difficulty )
{
    float player_skill = p.bionics_adjusted_skill( skill_firstaid,
                         skill_computer,
                         skill_electronics );

    std::vector< std::pair<float, int>> ally_skills;
    ally_skills.reserve( g->allies().size() );
    for( size_t i = 0; i < g->allies().size() ; i ++ ) {
        std::pair<float, int> ally_skill;
        const npc *e = g->allies()[ i ];

        player &ally = *g->critter_by_id<player>( e->getID() );
        ally_skill.second = i;
        ally_skill.first = ally.bionics_adjusted_skill( skill_firstaid,
                           skill_computer,
                           skill_electronics );
        ally_skills.push_back( ally_skill );
    }
    std::ranges::sort( ally_skills, [&]( const std::pair<float, int> &lhs,
    const std::pair<float, int> &rhs ) {
        return rhs.first < lhs.first;
    } );
    int player_cos = bionic_manip_cos( player_skill, difficulty );
    for( size_t i = 0; i < g->allies().size() ; i ++ ) {
        if( ally_skills[ i ].first > player_skill ) {
            const npc *e = g->allies()[ ally_skills[ i ].second ];
            player &ally = *g->critter_by_id<player>( e->getID() );
            int ally_cos = bionic_manip_cos( ally_skills[ i ].first, difficulty );
            if( e->has_effect( effect_sleep ) ) {
                if( !g->u.query_yn(
                        //~ %1$s is the name of the ally
                        _( "<color_white>%1$s is asleep, but has a <color_green>%2$d<color_white> chance of success compared to your <color_red>%3$d<color_white> chance of success.  Continue with a higher risk of failure?</color>" ),
                        ally.disp_name(), ally_cos, player_cos ) ) {
                    return null_player;
                } else {
                    continue;
                }
            }
            //~ %1$s is the name of the ally
            add_msg( _( "%1$s will perform the operation with a %2$d chance of success." ), ally.disp_name(),
                     ally_cos );
            return ally;
        } else {
            break;
        }
    }

    return p;
}

template<typename ...Args>
inline void popup_player_or_npc( player &p, const char *player_mes, const char *npc_mes,
                                 Args &&... args )
{
    if( p.is_player() ) {
        popup( player_mes, std::forward<Args>( args )... );
    } else {
        popup( p.replace_with_npc_name( string_format( npc_mes, std::forward<Args>( args )... ) ) );
    }
}

void iexamine::autodoc( player &p, const tripoint_bub_ms &examp )
{
    enum options {
        INSTALL_CBM,
        UNINSTALL_CBM,
        BONESETTING,
        TREAT_WOUNDS,
        RAD_AWAY,
        BLOOD_ANALYSIS,
    };

    bool adjacent_couch = false;
    static avatar null_player;
    tripoint_bub_ms couch_pos;
    player &patient = player_on_couch( p, examp, null_player, adjacent_couch, couch_pos );
    Character &Operator = operator_present( p, examp, null_player );

    //static item null_cyborg;
    item *cyborg = cyborg_on_couch( couch_pos );

    if( !adjacent_couch ) {
        popup( _( "No connected couches found.  Operation impossible.  Exiting." ) );
        return;
    }
    if( &patient == &null_player ) {
        if( cyborg != nullptr ) {
            if( cyborg->typeId() == itype_corpse && !cyborg->is_active() ) {
                popup( _( "Patient is dead.  Please remove corpse to proceed.  Exiting." ) );
                return;
            } else if( cyborg->typeId() == itype_bot_broken_cyborg || cyborg->typeId() == itype_corpse ) {
                popup( _( "ERROR Bionic Level Assessement: FULL CYBORG.  Autodoc Mk. XI can't opperate.  Please move patient to appropriate facility.  Exiting." ) );
                return;
            }

            uilist cmenu;
            cmenu.text = _( "Autodoc Mk. XI.  Status: Online.  Please choose operation." );
            cmenu.addentry( 1, true, 'i', _( "Choose Compact Bionic Module to install." ) );
            cmenu.addentry( 2, true, 'u', _( "Choose installed bionic to uninstall." ) );
            cmenu.query();

            switch( cmenu.ret ) {
                case 1: {
                    popup( _( "ERROR NO SPACE AVAILABLE.  Operation impossible.  Exiting." ) );
                    break;
                }
                case 2: {
                    std::vector<std::string> choice_names;
                    choice_names.emplace_back( _( "Personality_Override" ) );
                    for( size_t i = 0; i < 6; i++ ) {
                        choice_names.emplace_back( _( "C0RR#PTED?D#TA" ) );
                    }
                    int choice_index = uilist( _( "Choose bionic to uninstall" ), choice_names );
                    if( choice_index == 0 ) {
                        g->save_cyborg( cyborg, couch_pos, p );
                    } else {
                        popup( _( "UNKNOWN COMMAND.  Autodoc Mk. XI. Crashed." ) );
                        return;
                    }
                    break;
                }
                default:
                    return;
            }
            return;
        } else {
            popup( _( "No patient found located on the connected couches.  Operation impossible.  Exiting." ) );
            return;
        }
    } else if( patient.activity->id() == activity_id( "ACT_OPERATION" ) ) {
        popup( _( "Operation underway.  Please wait until the end of the current procedure.  Estimated time remaining: %s." ),
               to_string( time_duration::from_turns( patient.activity->moves_left / 100 ) ) );
        p.add_msg_if_player( m_info, _( "The autodoc is working on %s." ), patient.disp_name() );
        return;
    }

    const bool unsafe_usage = &Operator == &null_player || ( &Operator == &p && &patient == &p );
    std::string autodoc_header = _( "Autodoc Mk. XI.  Status: Online.  Please choose operation" );
    if( unsafe_usage ) {
        const std::string &warning_sign = colorize( " /", c_yellow ) + colorize( "!",
                                          c_red ) + colorize( "\\", c_yellow );
        const std::string &warning = warning_sign + colorize( _( " WARNING: Operator missing" ),
                                     c_red ) + warning_sign;
        autodoc_header = warning +
                         _( "\n Using the Autodoc without an operator can lead to <color_light_cyan>serious injuries</color> and <color_light_cyan>various internal bionic malfunctions</color>.\n Manufacturer <color_light_green>guarantees automated bionic installation in functional condition</color>.\n Manufacturer <color_light_cyan>does not guarantee automated bionic uninstallation</color>.\n By continuing with the operation you accept the risks and acknowledge that you will not take any legal actions against this facility in case of an accident.\n\nThe following skills affect autodoc installation: Computers, First Aid, and Electronics." );
    }

    std::vector<item *> arm_splints;
    std::vector<item *> leg_splints;

    // find splints on the ground
    for( item * &supplies : get_map().i_at( examp ) ) {
        if( supplies->typeId() == itype_arm_splint ) {
            arm_splints.push_back( supplies );
        }
        if( supplies->typeId() == itype_leg_splint ) {
            leg_splints.push_back( supplies );
        }
    }
    // find splints in vehicle
    if( const std::optional<vpart_reference> vp = get_map().veh_at( examp ).part_with_feature(
            flag_AUTODOC, false ) ) {
        auto dest_veh = &vp->vehicle();
        int dest_part = vp->part_index();
        for( item * &it : dest_veh->get_items( dest_part ) ) {
            if( it->typeId() == itype_arm_splint ) {
                arm_splints.push_back( it );
            }
            if( it->typeId() == itype_leg_splint ) {
                leg_splints.push_back( it );
            }
        }
    }

    autodoc_header +=
        string_format(
            _( "\n\n<color_white>Internal supplies:</color>\n Arm splints: %d\n Leg splints: %d" ),
            arm_splints.size(), leg_splints.size() );

    uilist amenu;
    amenu.text = autodoc_header;
    amenu.addentry( INSTALL_CBM, true, 'i', _( "Choose Compact Bionic Module to install" ) );
    amenu.addentry( UNINSTALL_CBM, true, 'u', _( "Choose installed bionic to uninstall" ) );
    amenu.addentry( BONESETTING, true, 's', _( "Splint broken limbs" ) );
    amenu.addentry( TREAT_WOUNDS, true, 'w', _( "Treat wounds" ) );
    amenu.addentry( RAD_AWAY, true, 'r', _( "Check radiation level" ) );
    amenu.addentry( BLOOD_ANALYSIS, true, 'b', _( "Conduct blood analysis" ) );

    amenu.query();

    bool needs_anesthesia = cbm_needs_anesthesia( patient );
    std::vector<tool_comp> anesth_kit;

    if( needs_anesthesia && amenu.ret < 2 ) {
        const inventory &crafting_inv = p.crafting_inventory();
        std::vector<item *> a_filter = crafting_inv.items_with( []( const item & it ) {
            return it.has_quality( qual_ANESTHESIA );
        } );
        for( const item *anesthesia_item : a_filter ) {
            if( anesthesia_item->ammo_remaining() >= 1 ) {
                anesth_kit.emplace_back( anesthesia_item->typeId(), 1 );
            }
        }
    }

    switch( amenu.ret ) {
        case INSTALL_CBM: {
            item *bionic = game_menus::inv::install_bionic( p, patient );

            if( !bionic ) {
                return;
            }

            const itype *itemtype = bionic->type;

            player &installer = best_installer( p, null_player, itemtype->bionic->difficulty );
            if( &installer == &null_player ) {
                return;
            }

            std::vector<item_comp> progs;
            bool has_install_program = false;

            std::vector<item *> install_programs = p.crafting_inventory().items_with( [itemtype](
                    const item & it ) -> bool { return it.typeId() == itemtype->bionic->installation_data; } );

            if( !install_programs.empty() ) {
                has_install_program = true;
                progs.emplace_back( install_programs[0]->typeId(), 1 );
            }

            const int weight = 7;
            const int surgery_duration = itemtype->bionic->difficulty * 2;
            const requirement_data req_anesth = *requirement_id( "anesthetic" ) *
                                                surgery_duration * weight;

            if( patient.can_install_bionics( ( *itemtype ), installer, true, has_install_program ? 10 : -1 ) ) {
                const time_duration duration = itemtype->bionic->difficulty * 20_minutes;
                patient.introduce_into_anesthesia( duration, installer, needs_anesthesia );
                bionic->detach();
                if( needs_anesthesia ) {
                    for( const auto &e : req_anesth.get_components() ) {
                        p.consume_items( e, 1, is_crafting_component );
                    }
                    for( const auto &e : req_anesth.get_tools() ) {
                        p.consume_tools( e );
                    }
                    p.invalidate_crafting_inventory();
                }
                installer.mod_moves( -to_moves<int>( 1_minutes ) );
                patient.install_bionics( ( *itemtype ), installer, true, has_install_program ? 10 : -1 );

                if( has_install_program ) {
                    patient.consume_items( progs );
                }
            }
            break;
        }

        case UNINSTALL_CBM: {
            const bionic_collection &installed_bionics = *patient.my_bionics;
            if( installed_bionics.empty() ) {
                popup_player_or_npc( patient, _( "You don't have any bionics installed." ),
                                     _( "<npcname> doesn't have any bionics installed." ) );
                return;
            }

            for( const bionic &bio : installed_bionics ) {
                if( bio.info().itype().is_valid() ) {
                    // put cbm items in your inventory
                    detached_ptr<item> bionic_to_uninstall = item::spawn( bio.id.str(), calendar::turn );
                    bionic_to_uninstall->set_flag( flag_IN_CBM );
                    bionic_to_uninstall->set_flag( flag_NO_STERILE );
                    bionic_to_uninstall->set_flag( flag_NO_PACKED );
                    g->u.i_add( std::move( bionic_to_uninstall ) );
                }
            }

            const item *bionic = game_menus::inv::uninstall_bionic( p, patient );
            if( !bionic ) {
                g->u.remove_items_with( []( detached_ptr<item> &&it ) { // remove cbm items from inventory
                    if( it->has_flag( flag_IN_CBM ) ) {
                        detached_ptr<item> del = std::move( it ); //This acts as a delete
                    }
                    return VisitResponse::SKIP;
                } );
                return;
            }
            const item *it = bionic;
            const itype *itemtype = it->type;
            const bionic_id &bid = itemtype->bionic->id;

            g->u.remove_items_with( []( detached_ptr<item> &&it ) { // remove cbm items from inventory
                if( it->has_flag( flag_IN_CBM ) ) {
                    detached_ptr<item> del = std::move( it ); //This acts as a delete
                }
                return VisitResponse::SKIP;
            } );

            // Malfunctioning bionics that don't have associated items and get a difficulty of 12
            const int difficulty = itemtype->bionic ? itemtype->bionic->difficulty : 12;
            const float volume_anesth = difficulty * 20 * 2; // 2ml/min

            player &installer = best_installer( p, null_player, difficulty );
            if( &installer == &null_player ) {
                return;
            }

            if( patient.can_uninstall_bionic( bid, installer, true ) ) {
                const time_duration duration = difficulty * 20_minutes;
                patient.introduce_into_anesthesia( duration, installer, needs_anesthesia );
                if( needs_anesthesia ) {
                    p.consume_tools( anesth_kit, volume_anesth );
                }
                installer.mod_moves( -to_moves<int>( 1_minutes ) );
                patient.uninstall_bionic( bid, installer, true );
            }
            break;
        }

        case BONESETTING: {
            if( arm_splints.empty() && leg_splints.empty() ) {
                popup( _( "Internal supply of splints exhausted.  Operation impossible.  Exiting." ) );
                return;
            }

            int broken_limbs_count = 0;
            for( const bodypart_id &part : patient.get_all_body_parts( true ) ) {
                const bool broken = patient.is_limb_broken( part );
                if( !broken ) {
                    continue;
                }
                broken_limbs_count++;
                patient.moves -= 500;
                // TODO: fail here if unable to perform the action, i.e. can't wear more, trait mismatch.
                int quantity = 1;
                if( part == bodypart_id( "arm_l" ) || part == bodypart_id( "arm_r" ) ) {
                    if( !arm_splints.empty() ) {
                        for( detached_ptr<item> &it : get_map().use_amount( examp, 1, itype_arm_splint, quantity ) ) {
                            patient.wear_item( std::move( it ), false );
                        }
                    } else {
                        popup( _( "Internal supply of arm splints exhausted.  Splinting broken arms impossible.  Exiting." ) );
                        continue;
                    }
                } else if( part == bodypart_id( "leg_l" ) || part == bodypart_id( "leg_r" ) ) {
                    if( !leg_splints.empty() ) {
                        for( detached_ptr<item> &it : get_map().use_amount( examp, 1, itype_leg_splint, quantity ) ) {
                            patient.wear_item( std::move( it ), false );
                        }
                    } else {
                        popup( _( "Internal supply of leg splints exhausted.  Splinting broken legs impossible.  Exiting." ) );
                        continue;
                    }
                }

                if( patient.worn_with_flag( flag_SPLINT, part ) ) {
                    patient.add_msg_player_or_npc( m_good, _( "The machine rapidly sets and splints your broken %s." ),
                                                   _( "The machine rapidly sets and splints <npcname>'s broken %s." ),
                                                   body_part_name( part ) );
                    // TODO: Prevent exploits with hp draining stuff?
                    int heal_amt = patient.get_part_hp_max( part ) / 2 - patient.get_part_hp_cur( part );
                    if( heal_amt > 0 ) {
                        patient.heal( part, heal_amt );
                    }
                }
            }
            if( broken_limbs_count == 0 ) {
                popup_player_or_npc( patient, _( "You have no limbs that require splinting." ),
                                     _( "<npcname> doesn't have limbs that require splinting." ) );
            }
            break;
        }

        case TREAT_WOUNDS: {
            if( !patient.has_effect( effect_bleed ) && !patient.has_effect( effect_infected ) &&
                !patient.has_effect( effect_bite ) ) {
                p.add_msg_player_or_npc( m_info, _( "You don't have any wounds that need treatment." ),
                                         _( "<npcname> doesn't have any wounds that need treatment." ) );
                return;
            }

            if( patient.has_effect( effect_infected ) ) {
                if( patient.has_effect( effect_strong_antibiotic ) ||
                    patient.has_effect( effect_antibiotic ) ||
                    patient.has_effect( effect_weak_antibiotic ) ) {
                    patient.add_msg_player_or_npc( m_info,
                                                   _( "The autodoc detected a bacterial infection in your body, but as it also detected you've already taken antibiotics, it decided not to apply another dose right now." ),
                                                   _( "The autodoc detected a bacterial infection in <npcname>'s body, but as it also detected they've already taken antibiotics, it decided not to apply another dose right now." ) );
                } else {
                    patient.add_effect( effect_strong_antibiotic, 12_hours );
                    patient.mod_pain( 3 );
                    patient.add_msg_player_or_npc( m_good,
                                                   _( "The autodoc detected a bacterial infection in your body and injected antibiotics to treat it." ),
                                                   _( "The autodoc detected a bacterial infection in <npcname>'s body and injected antibiotics to treat it." ) );

                }
            }

            for( const bodypart_id &bp : patient.get_all_body_parts( true ) ) {
                const bodypart_str_id &bp_healed = bp.id();
                if( patient.has_effect( effect_bleed, bp_healed ) ) {
                    patient.remove_effect( effect_bleed, bp_healed );
                    patient.add_msg_player_or_npc( m_good,
                                                   _( "The autodoc detected a bleeding on your %s and applied a hemostatic drug to stop it." ),
                                                   _( "The autodoc detected a bleeding on <npcname>'s %s and applied a hemostatic drug to stop it." ),
                                                   body_part_name( bp_healed ) );
                }

                if( patient.has_effect( effect_bite, bp_healed ) ) {
                    patient.remove_effect( effect_bite, bp_healed );
                    patient.add_msg_player_or_npc( m_good,
                                                   _( "The autodoc detected an open wound on your %s and applied a disinfectant to clean it." ),
                                                   _( "The autodoc detected an open wound on <npcname>'s %s and applied a disinfectant to clean it." ),
                                                   body_part_name( bp_healed ) );

                    // Fixed disinfectant intensity of 4 disinfectant_power + 10 first aid skill level of autodoc.
                    const int disinfectant_intensity = 14;
                    patient.add_effect( effect_disinfected, 1_turns, bp_healed );
                    effect &e = patient.get_effect( effect_disinfected, bp_healed );
                    e.set_duration( e.get_int_dur_factor() * disinfectant_intensity );
                    bodypart_str_id target_part = player::bp_to_hp( bp_healed );
                    bodypart &part = patient.get_part( target_part );
                    part.set_damage_disinfected( part.get_hp_max() - part.get_hp_cur() );

                }
            }
            patient.moves -= 500;
            break;
        }

        case RAD_AWAY: {
            patient.moves -= 500;
            patient.add_msg_player_or_npc( m_info,
                                           _( "The autodoc scanned you and detected a radiation level of %d mSv." ),
                                           _( "The autodoc scanned <npcname> and detected a radiation level of %d mSv." ),
                                           patient.get_rad() );
            if( patient.get_rad() ) {
                if( patient.has_effect( effect_pblue ) ) {
                    patient.add_msg_player_or_npc( m_info,
                                                   _( "The autodoc detected an anti-radiation drug in your bloodstream, so it decided not to administer another dose right now." ),
                                                   _( "The autodoc detected an anti-radiation drug in <npcname>'s bloodstream, so it decided not to administer another dose right now." ) );
                } else {
                    add_msg( m_good,
                             _( "The autodoc administered an anti-radiation drug to treat radiation poisoning." ) );
                    patient.mod_pain( 3 );
                    patient.add_effect( effect_pblue, 1_hours );
                }
            }
            if( patient.leak_level( flag_RADIOACTIVE ) ) {
                popup( _( "Warning!  Autodoc detected a radiation leak of %d mSv from items in patient's possession.  Urgent decontamination procedures highly recommended." ),
                       patient.leak_level( flag_RADIOACTIVE ) );
            }
            break;
        }

        case BLOOD_ANALYSIS: {
            patient.moves -= 500;
            patient.conduct_blood_analysis();
            patient.add_msg_player_or_npc( m_info,
                                           _( "The autodoc analyzed your blood." ),
                                           _( "The autodoc analyzed <npcname>'s blood." ) );
            break;
        }

        default:
            return;
    }
}

static int get_charcoal_charges( units::volume food )
{
    const int charcoal = to_liter( food ) * sm_rack::CHARCOAL_PER_LITER;

    return  std::max( charcoal, sm_rack::MIN_CHARCOAL );
}

static bool is_non_rotten_crafting_component( const item &it )
{
    return is_crafting_component( it ) && !it.rotten();
}

static void mill_activate( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    const furn_id cur_mill_type = here.furn( examp );
    furn_id next_mill_type = f_null;
    if( cur_mill_type == f_wind_mill ) {
        next_mill_type = f_wind_mill_active;
    } else if( cur_mill_type == f_water_mill ) {
        next_mill_type = f_water_mill_active;
    } else {
        debugmsg( "Examined furniture has action mill_activate, but is of type %s",
                  here.furn( examp ).id().c_str() );
        return;
    }
    bool food_present = false;
    map_stack items = here.i_at( examp );
    units::volume food_volume = 0_ml;

    for( item * &it : items ) {
        if( it->type->milling_data ) {
            food_present = true;
            food_volume += it->volume();
            continue;
        } else {
            add_msg( m_bad, _( "This mill contains %s, which can't be milled!" ), it->tname( 1, false ) );
            add_msg( _( "You remove the %s from the mill." ), it->tname() );
            p.mod_moves( -p.item_handling_cost( *it ) );
            here.add_item_or_charges( p.bub_pos(), here.i_rem( examp, it ) );
            return;
        }
    }
    if( !food_present ) {
        add_msg( _( "This mill is empty.  Fill it with starchy products such as wheat, barley or oats and try again." ) );
        return;
    }
    if( food_volume > sm_rack::MAX_FOOD_VOLUME_MILLING ) {
        add_msg( _( "This mill is overloaded with products, and the millstone can't turn.  Remove some and try again." ) );
        add_msg( pgettext( "volume units", "You think that you can load about %s %s in it." ),
                 format_volume( sm_rack::MAX_FOOD_VOLUME_MILLING ), volume_units_long() );
        return;
    }

    for( auto &it : here.i_at( examp ) ) {
        if( it->type->milling_data ) {
            it->set_flag( flag_PROCESSING );
        }
    }
    here.furn_set( examp, next_mill_type );
    detached_ptr<item> result = item::spawn( "fake_milling_item", calendar::turn );
    result->activate();
    result->set_counter( to_turns<int>( milling_time ) );
    here.add_item( examp, std::move( result ) );
    add_msg( _( "You remove the brake on the millstone and it slowly starts to turn." ) );
}

static void cloning_vat_activate( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    // 86400 = 1 day, so this is 12 hrs per size increment
    const int turns_to_clone = 43200;

    // filter out faulty carriers
    auto carriers = p.wielded_items();
    for( size_t i = 0; i < carriers.size(); ) {
        item *const carrier = carriers[i];  // already a pointer
        if( carrier->has_fault( fault_id( "fault_bionic_nonsterile" ) ) ||
            carrier->typeId() != itype_embryo_empty ) {
            carriers.erase( carriers.begin() + i ); // erase by iterator
            // do NOT increment i here
        } else {
            i++; // increment only if we didn't erase
        }
    }
    if( carriers.empty() ) {
        popup( _( "You need a sterilized artificial womb and DNA to begin incubation." ) );
        return;
    }
    if( ( *carriers.begin() )->has_flag( flag_RADIO_MOD ) ) {
        popup( _( "You need to remove the radio mod first." ) );
        return;
    }

    // choose specimen sample
    auto syringes = p.all_items_with_id( itype_dna );
    if( syringes.size() == 0 ) {
        popup( _( "You have no valid specimen samples." ) );
        return;
    }
    uilist specimen_menu;
    specimen_menu.text = _( "Select specimen sample:" );
    for( size_t z = 0; z < syringes.size(); z++ ) {
        const auto specimen_id = mtype_id( syringes[z]->get_var( "specimen_sample" ) );
        const auto size = std::max( 1, cloning_utils::specimen_required_sample_size( specimen_id ) );
        specimen_menu.addentry( z, true, MENU_AUTOASSIGN, string_format( "%s [%s]",
                                syringes[z]->display_name(),
                                to_string( time_duration::from_turns( turns_to_clone * size ) ) ) );
    }
    specimen_menu.query();
    const int choice = specimen_menu.ret;
    if( choice < 0 ) {
        return;
    }

    // reference to the original detached_ptr
    auto &selected_syringe = syringes[choice];
    p.mod_moves( -250 );

    // Only try to remove if the pointer is valid
    if( !selected_syringe ) {
        return;
    }
    // remove the clean carrier
    detached_ptr<item> weapon = p.remove_primary_weapon();

    // search for DNA and begin process
    std::vector<item *> items = p.all_items_with_id( itype_dna );
    for( size_t x = 0; x < items.size(); x++ ) {
        if( selected_syringe->get_var( "specimen_sample" ) == items[x]->get_var( "specimen_sample" ) ) {
            if( items[x]->units_remaining( p ) <= 1 ) {
                // this consumes the container. need to figure that out
                detached_ptr<item> garbage = p.i_rem( p.inv_position_by_item( items[x] ) );
            } else {
                items[x]->mod_charges( -1 );
            }

            add_msg( m_info, _( "The cloning vat begins its rapid incubation process." ) );
            here.furn_set( examp, furn_str_id( "f_cloning_vat_active" ) );
            detached_ptr<item> result = item::spawn( "fake_cloning_vat_item", calendar::turn );

            // 100 turns = 1 second, so 180000 = 30 min per size increment
            result->set_var( "specimen_name", selected_syringe->get_var( "specimen_name" ) );
            result->set_var( "specimen_sample", selected_syringe->get_var( "specimen_sample" ) );

            // cloning vat random upgrade logic
            if( rng( 1, 100 ) < 90 ) {
                const mtype_id id( selected_syringe->get_var( "specimen_sample" ) );
                const mtype &type = id.obj();

                mongroup_id upgrade_group = mongroup_id::NULL_ID();
                upgrade_group = type.upgrade_group;
                const auto mons = upgrade_group.obj().monsters;

                if( !mons.empty() ) {
                    // calculate total weight (sum of frequencies), pick random, then iterate until we find it
                    int total_freq = 0;
                    for( const MonsterGroupEntry &entry : mons ) {
                        total_freq += entry.frequency;
                    }
                    int roll = rng( 1, total_freq );
                    const MonsterGroupEntry *chosen = nullptr;
                    for( const MonsterGroupEntry &entry : mons ) {
                        roll -= entry.frequency;
                        if( roll <= 0 ) {
                            chosen = &entry;
                            break;
                        }
                    }
                    const shared_ptr_fast<monster> newmon_ptr = make_shared_fast<monster>
                        ( mtype_id( chosen->name.str() ) );
                    monster &newmon = *newmon_ptr;

                    if( chosen ) {
                        result->set_var( "specimen_sample", chosen->name.str() );
                        result->set_var( "specimen_name", newmon.name() );
                    }
                }
            }

            const auto specimen_id = mtype_id( selected_syringe->get_var( "specimen_sample" ) );
            const auto size = std::max( 1, cloning_utils::specimen_required_sample_size( specimen_id ) );
            result->set_counter( turns_to_clone * size );
            result->activate();
            here.add_item( examp, std::move( result ) );

            return;
        }
    }

    return;
}

static void smoker_activate( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    furn_id cur_smoker_type = here.furn( examp );
    furn_id next_smoker_type = f_null;
    const bool portable = here.furn( examp ) == furn_str_id( "f_metal_smoking_rack" ) ||
                          here.furn( examp ) == furn_str_id( "f_metal_smoking_rack_active" );
    if( cur_smoker_type == f_smoking_rack ) {
        next_smoker_type = f_smoking_rack_active;
    } else if( cur_smoker_type == f_metal_smoking_rack ) {
        next_smoker_type = f_metal_smoking_rack_active;
    } else {
        debugmsg( "Examined furniture has action smoker_activate, but is of type %s",
                  here.furn( examp ).id().c_str() );
        return;
    }
    bool food_present = false;
    bool charcoal_present = false;
    map_stack items = here.i_at( examp );
    units::volume food_volume = 0_ml;
    item *charcoal = nullptr;

    for( item * const &it : items ) {
        // Check for finished items (either has SMOKED flag, or is not smokable and not charcoal)
        if( ( it->has_flag( flag_SMOKED ) && !it->has_flag( flag_SMOKABLE ) ) ||
            ( !it->has_flag( flag_SMOKABLE ) && it->typeId() != itype_charcoal ) ) {
            add_msg( _( "This rack already contains finished items." ) );
            add_msg( _( "Remove them before firing the smoking rack again." ) );
            return;
        }
        if( it->has_flag( flag_SMOKABLE ) ) {
            food_present = true;
            food_volume += it->volume();
            continue;
        }
        if( it->typeId() == itype_charcoal ) {
            charcoal_present = true;
            charcoal = it;
        }
        if( it->has_flag( flag_SMOKED ) && it->has_flag( flag_SMOKABLE ) ) {
            add_msg( _( "This rack has some smoked items that might be dehydrated by smoking them again." ) );
        }
    }
    if( !food_present ) {
        add_msg( _( "This rack is empty.  Fill it with raw meat, fish or sausages and try again." ) );
        return;
    }
    if( !charcoal_present ) {
        add_msg( _( "There is no charcoal in the rack." ) );
        return;
    }
    if( portable && food_volume > sm_rack::MAX_FOOD_VOLUME_PORTABLE ) {
        add_msg( _( "This rack is overloaded with food, and it blocks the flow of smoke.  Remove some and try again." ) );
        add_msg( _( "You think that you can load about %s %s in it." ),
                 format_volume( sm_rack::MAX_FOOD_VOLUME_PORTABLE ), volume_units_long() );
        return;
    } else if( food_volume > sm_rack::MAX_FOOD_VOLUME ) {
        add_msg( _( "This rack is overloaded with food, and it blocks the flow of smoke.  Remove some and try again." ) );
        add_msg( _( "You think that you can load about %s %s in it." ),
                 format_volume( sm_rack::MAX_FOOD_VOLUME ), volume_units_long() );
        return;
    }

    int char_charges = get_charcoal_charges( food_volume );

    if( count_charges_in_list( charcoal->type, here.i_at( examp ) ) < char_charges ) {
        add_msg( _( "There is not enough charcoal in the rack to smoke this much food." ) );
        add_msg( _( "You need at least %1$s pieces of charcoal, and the smoking rack has %2$s inside." ),
                 char_charges, count_charges_in_list( charcoal->type, here.i_at( examp ) ) );
        return;
    }

    if( !p.has_charges( itype_fire, 1 ) ) {
        add_msg( _( "This smoking rack is ready to be fired, but you have no fire source." ) );
        return;
    } else if( !query_yn( _( "Fire the smoking rack?" ) ) ) {
        return;
    }

    p.use_charges( itype_fire, 1 );
    for( auto &it : here.i_at( examp ) ) {
        if( it->has_flag( flag_SMOKABLE ) ) {
            it->set_flag( flag_PROCESSING );
        }
    }
    here.furn_set( examp, next_smoker_type );
    if( charcoal->charges == char_charges ) {
        //TODO!: check
        here.i_rem( examp, charcoal );
    } else {
        charcoal->charges -= char_charges;
    }
    detached_ptr<item> result = item::spawn( "fake_smoke_plume", calendar::turn );
    result->activate();
    result->set_counter( to_turns<int>( 6_hours ) );
    here.add_item( examp, std::move( result ) );
    add_msg( _( "You light a small fire under the rack and it starts to smoke." ) );
}

void iexamine::mill_finalize( player &, const tripoint_bub_ms &examp, const time_point &start_time )
{
    map &here = get_map();
    const furn_id cur_mill_type = here.furn( examp );
    furn_id next_mill_type = f_null;
    if( cur_mill_type == f_wind_mill_active ) {
        next_mill_type = f_wind_mill;
    } else if( cur_mill_type == f_water_mill_active ) {
        next_mill_type = f_water_mill;
    } else {
        debugmsg( "Furniture executed action mill_finalize, but is of type %s",
                  here.furn( examp ).id().c_str() );
        return;
    }

    map_stack items = here.i_at( examp );
    if( items.empty() ) {
        here.furn_set( examp, next_mill_type );
        return;
    }
    std::vector<detached_ptr<item>> results;
    for( auto iter = items.begin(); iter != items.end(); ) {
        item *it = *iter;
        if( it->type->milling_data ) {
            it->mod_last_rot_check( milling_time );
            const islot_milling &mdata = *it->type->milling_data;
            detached_ptr<item> result = item::spawn( mdata.into_, start_time + milling_time,
                                        it->count() * mdata.conversion_rate_ );
            detached_ptr<item> det;
            iter = items.erase( iter, &det );
            result->add_component( std::move( det ) );
            // copied from item::inherit_flags, which can not be called here because it requires a recipe.
            for( const flag_id &f : it->type->get_flags() ) {
                if( f->craft_inherit() ) {
                    result->set_flag( f );
                }
            }
            result->recipe_charges = result->charges;
            // Set flag to tell set_relative_rot() to calc from bday not now
            result->set_flag( flag_PROCESSING_RESULT );
            result->set_relative_rot( it->get_relative_rot() );
            result->unset_flag( flag_PROCESSING_RESULT );

            results.push_back( std::move( result ) );
        } else {
            iter++;
        }
    }

    for( detached_ptr<item> &it : results ) {
        here.add_item( examp, std::move( it ) );
    }
    here.furn_set( examp, next_mill_type );
}

void iexamine::cloning_vat_finalize( const tripoint_bub_ms &examp, const time_point & )
{
    // grab items in the vat
    map &here = get_map();
    map_stack items_here = here.i_at( examp );
    item developing_embryo;
    if( items_here.size() == 1 &&
        ( *items_here.begin() )->typeId() == itype_id( "fake_cloning_vat_item" ) ) {
        if( here.furn( examp ) == furn_str_id( "f_cloning_vat_active" ) ) {
            here.furn_set( examp, furn_str_id( "f_cloning_vat" ) );
        }
        developing_embryo = **items_here.begin();
    }

    here.furn_set( examp, furn_str_id( "f_cloning_vat" ) );

    // cloning vat failure: choose random garbage item and faulty womb
    if( rng( 1, 100 ) < 10 ) {
        const std::vector<itype_id> item_results{ itype_id( "arm" ), itype_id( "leg" ), itype_id( "fetus" ) };
        const itype_id &chosen_id = random_entry( item_results );
        detached_ptr<item> spawned_womb = item::spawn( itype_embryo_empty, calendar::turn );
        spawned_womb->faults.emplace( fault_id( "fault_bionic_nonsterile" ) );
        here.add_item( examp, std::move( spawned_womb ) );
        detached_ptr<item> spawned_item = item::spawn( chosen_id, calendar::turn );
        here.add_item( examp, std::move( spawned_item ) );

        sound_event se;
        se.origin = examp;
        se.volume = 50;
        se.category = sounds::sound_t::alarm;
        se.description = _( "beep!" );
        se.id = "misc";
        se.variant = "beep";
        sounds::sound( se );

        return;
    }

    // success: spawn the completed artificial womb
    sound_event se;
    se.origin = examp;
    se.volume = 50;
    se.category = sounds::sound_t::alarm;
    se.description = _( "ding!" );
    se.id = "misc";
    se.variant = "ding";
    sounds::sound( se );
    detached_ptr<item> spawned_embryo = item::spawn( itype_embryo, calendar::turn );
    spawned_embryo->set_var( "place_monster_override", developing_embryo.get_var( "specimen_sample" ) );
    spawned_embryo->set_var( "place_monster_override_name",
                             developing_embryo.get_var( "specimen_name" ) );
    here.add_item( examp, std::move( spawned_embryo ) );

    return;
}

static void smoker_finalize( player &, const tripoint_bub_ms &examp, const time_point &start_time )
{
    map &here = get_map();
    furn_id cur_smoker_type = here.furn( examp );
    furn_id next_smoker_type = f_null;
    if( cur_smoker_type == f_smoking_rack_active ) {
        next_smoker_type = f_smoking_rack;
    } else if( cur_smoker_type == f_metal_smoking_rack_active ) {
        next_smoker_type = f_metal_smoking_rack;
    } else {
        debugmsg( "Furniture executed action smoker_finalize, but is of type %s",
                  here.furn( examp ).id().c_str() );
        return;
    }

    map_stack items = here.i_at( examp );
    if( items.empty() ) {
        here.furn_set( examp, next_smoker_type );
        return;
    }

    std::vector<detached_ptr<item>> results;
    for( auto iter = items.begin(); iter != items.end(); ) {
        item *it = *iter;
        if( it->has_flag( flag_SMOKABLE ) && it->get_comestible() ) {
            if( it->get_comestible()->smoking_result.is_empty() ) {
                it->unset_flag( flag_PROCESSING );
                iter++;
            } else {
                it->mod_last_rot_check( 6_hours );

                detached_ptr<item> result = item::spawn( it->get_comestible()->smoking_result, start_time + 6_hours,
                                            it->charges );

                // Set flag to tell set_relative_rot() to calc from bday not now
                result->set_flag( flag_PROCESSING_RESULT );
                result->set_relative_rot( it->get_relative_rot() );
                result->unset_flag( flag_PROCESSING_RESULT );

                recipe rec;
                result->inherit_flags( *it, rec );

                detached_ptr<item> det;
                iter = items.erase( iter, &det );

                if( !result->has_flag( flag_NUTRIENT_OVERRIDE ) ) {
                    // If the item has "cooks_like" it will be replaced by that item as a component.
                    if( !it->get_comestible()->cooks_like.is_empty() ) {
                        // Set charges to 1 for stacking purposes.
                        det = item::spawn( it->get_comestible()->cooks_like, it->birthday(), 1 );
                    }
                    // Smoking is always 1:1, so these must be equal for correct kcal/vitamin calculation.
                    result->recipe_charges = det->charges;
                    result->add_component( std::move( det ) );
                    result->set_flag_recursive( flag_COOKED );
                }
                results.push_back( std::move( result ) );
            }
        } else {
            iter++;
        }
    }

    for( detached_ptr<item> &it : results ) {
        here.add_item( examp, std::move( it ) );
    }

    here.furn_set( examp, next_smoker_type );
}

static void smoker_load_food( player &p, const tripoint_bub_ms &examp,
                              const units::volume &remaining_capacity )
{
    std::vector<item_comp> comps;

    map &here = get_map();
    if( here.furn( examp ) == furn_str_id( "f_smoking_rack_active" ) ||
        here.furn( examp ) == furn_str_id( "f_metal_smoking_rack_active" ) ) {
        p.add_msg_if_player( _( "You can't place more food while it's smoking." ) );
        return;
    }

    // Already finished items have to be removed before adding more items for smoker to operate properly
    map_stack items = here.i_at( examp );
    for( item * const &it : items ) {
        // Check for finished items (either has SMOKED flag, or is not smokable and not charcoal)
        if( ( it->has_flag( flag_SMOKED ) && !it->has_flag( flag_SMOKABLE ) ) ||
            ( !it->has_flag( flag_SMOKABLE ) && it->typeId() != itype_charcoal ) ) {
            add_msg( _( "This rack already contains finished items." ) );
            add_msg( _( "Remove them before loading the smoking rack again." ) );
            return;
        }
    }

    // filter SMOKABLE food
    inventory inv = p.crafting_inventory();
    inv.remove_items_with( []( const item & it ) {
        return it.rotten();
    } );
    std::vector<item *> filtered = p.crafting_inventory().items_with( []( const item & it ) {
        return it.has_flag( flag_SMOKABLE );
    } );

    uilist smenu;
    smenu.text = _( "Load smoking rack with what kind of item?" );
    // count and ask for item to be placed ...
    std::list<std::string> names;
    std::vector<const item *> entries;
    for( const item *smokable_item : filtered ) {
        int count;
        if( smokable_item->count_by_charges() ) {
            count = inv.charges_of( smokable_item->typeId() );
        } else {
            count = inv.amount_of( smokable_item->typeId() );
        }
        if( count != 0 ) {
            auto on_list = std::ranges::find( names, item::nname( smokable_item->typeId(), 1 ) );
            if( on_list == names.end() ) {
                smenu.addentry( item::nname( smokable_item->typeId(), 1 ) );
                entries.push_back( smokable_item );
            }
            names.push_back( item::nname( smokable_item->typeId(), 1 ) );
            comps.emplace_back( smokable_item->typeId(), count );
        }
    }

    if( comps.empty() ) {
        p.add_msg_if_player( _( "You don't have any food that can be smoked." ) );
        return;
    }

    smenu.query();

    if( smenu.ret < 0 || static_cast<size_t>( smenu.ret ) >= entries.size() ) {
        add_msg( m_info, _( "Never mind." ) );
        return;
    }
    int count = 0;
    auto what = entries[smenu.ret];
    for( const auto &c : comps ) {
        if( c.type == what->typeId() ) {
            count = c.count;
        }
    }

    const int max_count_for_capacity = remaining_capacity / what->base_volume();
    const int max_count = std::min( count, max_count_for_capacity );

    // ... then ask how many to put it
    const std::string popupmsg = string_format( _( "Insert how many %s into the rack?" ),
                                 item::nname( what->typeId(), count ) );
    int amount = string_input_popup()
                 .title( popupmsg )
                 .width( 20 )
                 .text( std::to_string( max_count ) )
                 .only_digits( true )
                 .query_int();

    if( amount == 0 ) {
        add_msg( m_info, _( "Never mind." ) );
        return;
    } else if( amount > count ) {
        add_msg( m_info, _( "You don't have that many." ) );
        return;
    } else if( amount > max_count_for_capacity ) {
        add_msg( m_info, _( "You can't place that many." ) );
        return;
    }

    // reload comps with chosen items and quantity
    comps.clear();
    comps.emplace_back( what->typeId(), amount );

    // select from where to get the items from and place them
    inv.form_from_map( g->u.bub_pos(), PICKUP_RANGE, &g->u );
    inv.remove_items_with( []( const item & it ) {
        return it.rotten();
    } );

    comp_selection<item_comp> selected = p.select_item_component( comps, 1, inv, true,
                                         is_non_rotten_crafting_component );
    std::vector<detached_ptr<item>> moved = p.consume_items( selected, 1,
                                            is_non_rotten_crafting_component );

    for( detached_ptr<item> &m : moved ) {
        p.mod_moves( -p.item_handling_cost( *m ) );
        add_msg( m_info, _( "You carefully place %s %s in the rack." ), amount,
                 item::nname( m->typeId(), amount ) );
        here.add_item( examp, std::move( m ) );
    }
    p.invalidate_crafting_inventory();
}

static void mill_load_food( player &p, const tripoint_bub_ms &examp,
                            const units::volume &remaining_capacity )
{
    std::vector<item_comp> comps;
    map &here = get_map();
    if( here.furn( examp ) == furn_str_id( "f_wind_mill_active" ) ||
        here.furn( examp ) == furn_str_id( "f_water_mill_active" ) ) {
        p.add_msg_if_player( _( "You can't place more food while it's milling." ) );
        return;
    }
    // filter millable food
    inventory inv = p.crafting_inventory();
    inv.remove_items_with( []( const item & it ) {
        return it.rotten();
    } );
    std::vector<item *> filtered = p.crafting_inventory().items_with( []( const item & it ) {
        return static_cast<bool>( it.type->milling_data );
    } );

    uilist smenu;
    smenu.text = _( "Load mill with what kind of product?" );
    // count and ask for item to be placed ...
    std::list<std::string> names;
    std::vector<const item *> entries;
    for( const item *millable_item : filtered ) {
        int count;
        if( millable_item->count_by_charges() ) {
            count = inv.charges_of( millable_item->typeId() );
        } else {
            count = inv.amount_of( millable_item->typeId() );
        }
        if( count != 0 ) {
            auto on_list = std::ranges::find( names, item::nname( millable_item->typeId(), 1 ) );
            if( on_list == names.end() ) {
                smenu.addentry( item::nname( millable_item->typeId(), 1 ) );
                entries.push_back( millable_item );
            }
            names.push_back( item::nname( millable_item->typeId(), 1 ) );
            comps.emplace_back( millable_item->typeId(), count );
        }
    }

    if( comps.empty() ) {
        p.add_msg_if_player( _( "You don't have any materials that can be milled." ) );
        return;
    }

    smenu.query();

    if( smenu.ret < 0 || static_cast<size_t>( smenu.ret ) >= entries.size() ) {
        add_msg( m_info, _( "Never mind." ) );
        return;
    }
    int count = 0;
    auto what = entries[smenu.ret];
    for( const auto &c : comps ) {
        if( c.type == what->typeId() ) {
            count = c.count;
        }
    }

    const int max_count_for_capacity = remaining_capacity / what->base_volume();
    const int max_count = std::min( count, max_count_for_capacity );

    // ... then ask how many to put it
    const std::string popupmsg = string_format( _( "Insert how many %s into the mill?" ),
                                 item::nname( what->typeId(), count ) );
    int amount = string_input_popup()
                 .title( popupmsg )
                 .width( 20 )
                 .text( std::to_string( max_count ) )
                 .only_digits( true )
                 .query_int();

    if( amount == 0 ) {
        add_msg( m_info, _( "Never mind." ) );
        return;
    } else if( amount > count ) {
        add_msg( m_info, _( "You don't have that many." ) );
        return;
    } else if( amount > max_count_for_capacity ) {
        add_msg( m_info, _( "You can't place that many." ) );
        return;
    }

    // reload comps with chosen items and quantity
    comps.clear();
    comps.emplace_back( what->typeId(), amount );

    // select from where to get the items from and place them
    inv.form_from_map( g->u.bub_pos(), PICKUP_RANGE, &g->u );
    inv.remove_items_with( []( const item & it ) {
        return it.rotten();
    } );

    comp_selection<item_comp> selected = p.select_item_component( comps, 1, inv, true,
                                         is_non_rotten_crafting_component );
    std::vector<detached_ptr<item>> moved = p.consume_items( selected, 1,
                                            is_non_rotten_crafting_component );
    for( detached_ptr<item> &m : moved ) {
        p.mod_moves( -p.item_handling_cost( *m ) );
        add_msg( m_info, pgettext( "item amount and name", "You carefully place %s %s in the mill." ),
                 amount, item::nname( m->typeId(), amount ) );

        here.add_item( examp, std::move( m ) );
    }
    p.invalidate_crafting_inventory();
}

void iexamine::on_smoke_out( const tripoint_bub_ms &examp, const time_point &start_time )
{
    map &here = get_map();
    if( here.furn( examp ) == furn_str_id( "f_smoking_rack_active" ) ||
        here.furn( examp ) == furn_str_id( "f_metal_smoking_rack_active" ) ) {
        smoker_finalize( g->u, examp, start_time );
    }
}

void iexamine::cloning_vat_examine( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    const bool active = here.furn( examp ) == furn_str_id( "f_cloning_vat_active" );
    map_stack items_here = here.i_at( examp );

    if( !active ) {
        // handle inactive vat: load or unload
        uilist menu;
        menu.text = _( "What to do with the cloning vat?" );
        if( items_here.size() > 0 ) {
            menu.addentry( _( "Get contents" ) );
            menu.query();
            if( menu.ret != 0 ) {
                return;
            }

            // get pointer to first item, ask user if they want to wield
            item *it = *items_here.begin();
            if( !query_yn( string_format( _( "Take %s from the cloning vat?" ), it->tname().c_str() ) ) ) {
                return;
            }
            // remove from map, store in det
            detached_ptr<item> det;
            items_here.erase( items_here.begin(), &det );
            p.wield( std::move( det ) );

            return;
        }

        menu.addentry( _( "Begin incubation" ) );
        menu.query();

        if( menu.ret != 0 ) {
            return;
        }

        cloning_vat_activate( p, examp );
    } else {
        // handle active vat: cancel process
        if( items_here.size() == 0 ) {
            return;
        }

        const std::string prompt = string_format( _( "Cancel incubation (%s left)" ),
                                   to_string( time_duration::from_turns( ( *items_here.begin() )->get_counter() ) ) );

        uilist menu;
        menu.text = _( "What to do with the active cloning vat?" );
        menu.addentry( prompt );
        menu.query();
        if( menu.ret != 0 ) {
            return;
        }
        // Ask using the item's name
        if( !query_yn( _( "Cancel incubation process?  This will kill the specimen inside." ) ) ) {
            return;
        }

        sound_event se;
        se.origin = examp;
        se.volume = 50;
        se.category = sounds::sound_t::alarm;
        se.description = _( "beep!" );
        se.id = "misc";
        se.variant = "beep";
        sounds::sound( se );

        if( items_here.size() > 0 ) {
            items_here.erase( items_here.begin() );  // delete all items here
        }

        // cloning vat failure
        const std::vector<itype_id> item_results{ itype_id( "arm" ), itype_id( "leg" ), itype_id( "fetus" ) };
        const itype_id &chosen_id = random_entry( item_results );
        detached_ptr<item> spawned_remains = item::spawn( chosen_id, calendar::turn, 1 );
        detached_ptr<item> spawned_womb = item::spawn( itype_embryo_empty, calendar::turn, 1 );
        spawned_womb->faults.emplace( fault_id( "fault_bionic_nonsterile" ) );
        here.add_item( examp, std::move( spawned_womb ) );
        here.add_item( examp, std::move( spawned_remains ) );

        here.furn_set( examp, furn_str_id( "f_cloning_vat" ) );
    }
}

void iexamine::quern_examine( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    if( here.furn( examp ) == furn_str_id( "f_water_mill" ) ) {
        if( !here.is_water_shallow_current( examp ) ) {
            add_msg( _( "The water mill needs to be over shallow flowing water to work." ) );
            return;
        }
    }
    if( here.furn( examp ) == furn_str_id( "f_wind_mill" ) ) {
        if( g->is_sheltered( examp ) ) {
            add_msg( _( "The wind mill needs to be outside in the wind to work." ) );
            return;
        }
    }

    const bool active = here.furn( examp ) == furn_str_id( "f_water_mill_active" ) ||
                        here.furn( examp ) == furn_str_id( "f_wind_mill_active" );
    map_stack items_here = here.i_at( examp );

    if( items_here.empty() && active ) {
        debugmsg( "active mill was empty!" );
        if( here.furn( examp ) == furn_str_id( "f_water_mill_active" ) ) {
            here.furn_set( examp, f_water_mill );
        } else if( here.furn( examp ) == furn_str_id( "f_wind_mill_active" ) ) {
            here.furn_set( examp, f_wind_mill );
        }
        return;
    }

    if( items_here.size() == 1 && ( *items_here.begin() )->typeId() == itype_fake_milling_item ) {
        debugmsg( "f_mill_active was empty, and had fake_milling_item!" );
        if( here.furn( examp ) == furn_str_id( "f_water_mill_active" ) ) {
            here.furn_set( examp, f_water_mill );
        } else if( here.furn( examp ) == furn_str_id( "f_wind_mill_active" ) ) {
            here.furn_set( examp, f_wind_mill );
        }
        items_here.erase( items_here.begin() );
        return;
    }

    time_duration time_left = 0_turns;
    units::volume f_volume = 0_ml;
    bool f_check = false;

    for( const item * const &it : items_here ) {
        if( it->typeId() != itype_fake_milling_item ) {
            f_check = true;
            f_volume += it->volume();
        }
        if( active && it->typeId() == itype_fake_milling_item ) {
            time_left = time_duration::from_turns( it->get_counter() );
        }
    }

    const bool empty = f_volume == 0_ml;
    const bool full = f_volume >= sm_rack::MAX_FOOD_VOLUME_MILLING;
    const auto remaining_capacity = sm_rack::MAX_FOOD_VOLUME_MILLING - f_volume;

    uilist smenu;
    smenu.text = _( "What to do with the mill?" );
    smenu.desc_enabled = true;

    smenu.addentry( 0, true, 'i', _( "Inspect mill" ) );

    if( !active ) {
        smenu.addentry_desc( 1, !empty, 'r',
                             empty ?  _( "Remove brake and start milling… insert some products for milling first" ) :
                             _( "Remove brake and start milling" ),
                             string_format( _( "Remove brake and start milling, milling will take about %s." ),
                                            to_string( milling_time ) ) );

        smenu.addentry_desc( 2, !full, 'p',
                             full ? _( "Insert products for milling… mill is full" ) :
                             string_format( _( "Insert products for milling… remaining capacity is %s %s" ),
                                            format_volume( remaining_capacity ), volume_units_abbr() ),
                             _( "Fill the mill with starchy products such as wheat, barley or oats." ) );

        if( f_check ) {
            smenu.addentry( 3, f_check, 'e', _( "Remove products from mill" ) );
        }

    } else {
        smenu.addentry_desc( 4, true, 'x',
                             _( "Apply brake to mill" ),
                             _( "Applying the brake will stop milling process." ) );
    }

    smenu.query();

    switch( smenu.ret ) {
        case 0: { //inspect mill
            std::string pop;
            if( active ) {
                pop = colorize( _( "There's a mill here.  It is turning and milling." ), c_green ) + "\n";
                if( time_left > 0_turns ) {
                    pop += string_format( _( "It should take about %s to finish milling." ),
                                          to_string_clipped( time_left ) ) + "\n";
                }
            } else {
                pop += colorize( _( "There's a mill here." ), c_green ) + "\n";
            }
            pop += colorize( _( "You inspect its contents and find: " ), c_green ) + "\n\n";
            if( items_here.empty() ) {
                pop += _( "…that it is empty." );
            } else {
                std::map<const item *, int> mill_list;
                for( const item * const &it : items_here ) {
                    if( it->typeId() == itype_fake_milling_item ) {
                        pop += "\n" + colorize( _( "You see that the milling process is not yet complete." ),
                                                c_red ) + "\n";
                        continue;
                    }
                    mill_list[it] += it->count();
                }
                for( auto it_mill : mill_list ) {
                    pop += "-> " + item::nname( it_mill.first->typeId(),
                                                it_mill.first->count() ) + ( ( it_mill.second > 1 ) ? " (" + std::to_string(
                                                        it_mill.second ) + ")\n" : "\n" );
                }
            }
            popup( pop, PF_NONE );
            break;
        }
        case 1:
            //activate
            if( active ) {
                add_msg( _( "It is already milling." ) );
            } else {
                mill_activate( p, examp );
            }
            break;
        case 2:
            // load food
            mill_load_food( p, examp, remaining_capacity );
            break;
        case 3:
            // remove food
            for( map_stack::iterator it = items_here.begin(); it != items_here.end(); ) {
                if( ( *it )->typeId() != itype_fake_milling_item ) {
                    // get handling cost before the item reference is invalidated
                    const int handling_cost = -p.item_handling_cost( **it );

                    add_msg( _( "You remove %s from the mill." ), ( *it )->tname() );
                    detached_ptr<item> det;
                    it = items_here.erase( it, &det );
                    here.add_item_or_charges( p.bub_pos(), std::move( det ) );
                    p.mod_moves( handling_cost );
                } else {
                    ++it;
                }
            }
            if( active ) {
                if( here.furn( examp ) == furn_str_id( "f_water_mill_active" ) ) {
                    here.furn_set( examp, f_water_mill );
                } else if( here.furn( examp ) == furn_str_id( "f_wind_mill_active" ) ) {
                    here.furn_set( examp, f_wind_mill );
                }
                add_msg( m_info, _( "You stop the milling process." ) );
            }
            break;
        default:
            add_msg( m_info, _( "Never mind." ) );
            break;
        case 4:
            if( here.furn( examp ) == furn_str_id( "f_water_mill_active" ) ) {
                here.furn_set( examp, f_water_mill );
            } else if( here.furn( examp ) == furn_str_id( "f_wind_mill_active" ) ) {
                here.furn_set( examp, f_wind_mill );
            }
            add_msg( m_info, _( "You stop the milling process." ) );
            break;
    }
}

void iexamine::smoker_options( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    const bool active = here.furn( examp ) == furn_str_id( "f_smoking_rack_active" ) ||
                        here.furn( examp ) == furn_str_id( "f_metal_smoking_rack_active" );
    const bool portable = here.furn( examp ) == furn_str_id( "f_metal_smoking_rack" ) ||
                          here.furn( examp ) == furn_str_id( "f_metal_smoking_rack_active" );
    map_stack items_here = here.i_at( examp );

    if( portable && items_here.empty() && active ) {
        debugmsg( "f_metal_smoking_rack_active was empty!" );
        here.furn_set( examp, f_metal_smoking_rack );
        return;
    } else if( items_here.empty() && active ) {
        debugmsg( "f_smoking_rack_active was empty!" );
        here.furn_set( examp, f_smoking_rack );
        return;
    }
    if( portable && items_here.size() == 1 &&
        ( *items_here.begin() )->typeId() == itype_fake_smoke_plume ) {
        debugmsg( "f_metal_smoking_rack_active was empty, and had fake_smoke_plume!" );
        here.furn_set( examp, f_metal_smoking_rack );
        items_here.erase( items_here.begin() );
        return;
    } else if( items_here.size() == 1 && ( *items_here.begin() )->typeId() == itype_fake_smoke_plume ) {
        debugmsg( "f_smoking_rack_active was empty, and had fake_smoke_plume!" );
        here.furn_set( examp, f_smoking_rack );
        items_here.erase( items_here.begin() );
        return;
    }

    bool rem_f_opt = false;
    time_duration time_left = 0_turns;
    int hours_left = 0;
    int minutes_left = 0;
    units::volume f_volume = 0_ml;
    bool f_check = false;

    for( const item * const &it : items_here ) {
        const bool has_smokable_item = it->typeId() != itype_charcoal && it->has_flag( flag_SMOKABLE );
        const bool has_removable_item = it->typeId() != itype_charcoal &&
                                        it->typeId() != itype_fake_smoke_plume;
        if( has_removable_item ) {
            f_check = true;
        }
        if( has_smokable_item ) {
            f_volume += it->volume();
        }
        if( active && it->typeId() == itype_fake_smoke_plume ) {
            time_left = time_duration::from_turns( it->get_counter() );
            hours_left = to_hours<int>( time_left );
            minutes_left = to_minutes<int>( time_left ) + 1;
        }
    }

    const bool empty = f_volume == 0_ml;
    const bool full = f_volume >= sm_rack::MAX_FOOD_VOLUME;
    const bool full_portable = f_volume >= sm_rack::MAX_FOOD_VOLUME_PORTABLE;
    const auto remaining_capacity = sm_rack::MAX_FOOD_VOLUME - f_volume;
    const auto remaining_capacity_portable = sm_rack::MAX_FOOD_VOLUME_PORTABLE - f_volume;

    // Check for charcoal in inventory and nearby ground
    int charcoal_nearby = 0;
    for( const tripoint_bub_ms &pt : here.points_in_radius( examp, PICKUP_RANGE ) ) {
        if( pt == examp ) {
            continue;
        }
        for( const item *it : here.i_at( pt ) ) {
            if( it->typeId() == itype_charcoal ) {
                charcoal_nearby += it->charges;
                if( charcoal_nearby > 0 ) {
                    break;
                }
            }
        }
        if( charcoal_nearby > 0 ) {
            break;
        }
    }
    const auto has_coal_in_inventory = p.charges_of( itype_charcoal ) > 0 || charcoal_nearby > 0;

    const auto coal_charges = count_charges_in_list( &*itype_charcoal, items_here );
    const auto need_charges = get_charcoal_charges( f_volume );
    const bool has_coal = coal_charges > 0;
    const bool has_enough_coal = coal_charges >= need_charges;

    uilist smenu;
    smenu.text = _( "What to do with the smoking rack:" );
    smenu.desc_enabled = true;

    smenu.addentry( 0, true, 'i', _( "Inspect smoking rack" ) );

    if( !active ) {
        smenu.addentry_desc( 1, !empty && has_enough_coal, 'l',
                             empty ?  _( "Light up and start smoking… insert some items for smoking first" ) :
                             !has_enough_coal ? string_format(
                                 _( "Light up and start smoking… need extra %d charges of charcoal" ),
                                 need_charges - coal_charges ) :
                             _( "Light up and start smoking" ),
                             _( "Light up the smoking rack and start smoking.  Smoking will take about 6 hours." ) );
        if( portable ) {
            smenu.addentry_desc( 2, !full_portable, 'f',
                                 full_portable ? _( "Insert items for smoking… smoking rack is full" ) :
                                 string_format( _( "Insert items for smoking… remaining capacity is %s %s" ),
                                                format_volume( remaining_capacity_portable ), volume_units_abbr() ),
                                 _( "Fill the smoking rack with raw meat, fish, sausages, hides, or other smokable items." ) );

            smenu.addentry_desc( 8, !active, 'z',
                                 active ? _( "You cannot disassemble this smoking rack while it is active!" ) :
                                 _( "Disassemble the smoking rack" ), "" );

        } else {
            smenu.addentry_desc( 2, !full, 'f',
                                 full ? _( "Insert items for smoking… smoking rack is full" ) :
                                 string_format( _( "Insert items for smoking… remaining capacity is %s %s" ),
                                                format_volume( remaining_capacity ), volume_units_abbr() ),
                                 _( "Fill the smoking rack with raw meat, fish, sausages, hides, or other smokable items." ) );
        }

        if( f_check ) {
            smenu.addentry( 4, f_check, 'e', _( "Remove items from smoking rack" ) );
        }

        smenu.addentry_desc( 3, has_coal_in_inventory, 'r',
                             !has_coal_in_inventory ? _( "Reload with charcoal… you don't have any" ) :
                             _( "Reload with charcoal" ),
                             string_format(
                                 _( "You need %d charges of charcoal for %s %s of food.  Minimal amount of charcoal is %d charges." ),
                                 sm_rack::CHARCOAL_PER_LITER, format_volume( 1_liter ), volume_units_long(),
                                 sm_rack::MIN_CHARCOAL ) );

    } else {
        smenu.addentry_desc( 7, true, 'x',
                             _( "Quench burning charcoal" ),
                             _( "Quenching will stop smoking process, but also destroy all used charcoal." ) );
    }

    if( has_coal ) {
        smenu.addentry( 5, true, 'c',
                        active ? string_format( _( "Rake out %d excess charges of charcoal from smoking rack" ),
                                                coal_charges ) :
                        string_format( _( "Remove %d charges of charcoal from smoking rack" ), coal_charges ) );
    }

    smenu.query();

    switch( smenu.ret ) {
        case 0: { //inspect smoking rack
            std::string pop;
            if( active ) {
                pop += colorize( _( "There's a smoking rack here.  It is lit and smoking." ), c_green ) + "\n";
                if( time_left > 0_turns ) {
                    if( minutes_left > 60 ) {
                        pop += string_format( vgettext( "It will finish smoking in about %d hour.",
                                                        "It will finish smoking in about %d hours.",
                                                        hours_left ), hours_left ) + "\n\n";
                    } else if( minutes_left > 30 ) {
                        pop += _( "It will finish smoking in less than an hour." ) + std::string( "\n" );
                    } else {
                        pop += string_format( _( "It should take about %d minutes to finish smoking." ),
                                              minutes_left ) + "\n ";
                    }
                }
            } else {
                pop += colorize( _( "There's a smoking rack here." ), c_green ) + "\n";
            }
            pop += colorize( _( "You inspect its contents and find: " ), c_green ) + "\n";
            if( items_here.empty() ) {
                pop += _( "…that it is empty." );
            } else {
                for( const item * const &it : items_here ) {
                    if( it->typeId() == itype_fake_smoke_plume ) {
                        pop += "\n" + colorize( _( "You see some smoldering embers there." ), c_red ) + "\n";
                        continue;
                    }
                    pop += "-> " + item::nname( it->typeId(),
                                                it->charges ) + " (" + std::to_string( it->charges ) + ")\n";
                }
            }
            popup( pop, PF_NONE );
            break;
        }
        case 1:
            //activate
            if( active ) {
                add_msg( _( "It is already lit and smoking." ) );
            } else {
                smoker_activate( p, examp );
            }
            break;
        case 2:
            // load food
            if( portable ) {
                smoker_load_food( p, examp, remaining_capacity_portable );
            } else {
                smoker_load_food( p, examp, remaining_capacity );
            }
            break;
        case 3:
            // load charcoal
            reload_furniture( p, examp );
            break;
        case 4:
            // remove food
            rem_f_opt = true;
        /* fallthrough */
        case 5: {
            //remove charcoal
            for( map_stack::iterator it = items_here.begin(); it != items_here.end(); ) {
                // Remove everything except charcoal when removing food, or only charcoal when removing charcoal
                const bool should_remove = ( rem_f_opt && ( *it )->typeId() != itype_charcoal ) ||
                                           ( !rem_f_opt && ( *it )->typeId() == itype_charcoal );
                if( should_remove ) {
                    // get handling cost before the item reference is invalidated
                    const int handling_cost = -p.item_handling_cost( **it );

                    add_msg( _( "You remove %s from the rack." ), ( *it )->tname() );
                    detached_ptr<item> det;
                    it = items_here.erase( it, &det );
                    here.add_item_or_charges( p.bub_pos(), std::move( det ) );
                    p.mod_moves( handling_cost );
                } else {
                    ++it;
                }
            }
            if( portable && active && rem_f_opt ) {
                here.furn_set( examp, f_metal_smoking_rack );
                add_msg( m_info, _( "You stop the smoking process." ) );
            } else if( active && rem_f_opt ) {
                here.furn_set( examp, f_smoking_rack );
                add_msg( m_info, _( "You stop the smoking process." ) );
            }
        }
        break;
        default:
            add_msg( m_info, _( "Never mind." ) );
            break;
        case 7:
            if( portable ) {
                here.furn_set( examp, f_metal_smoking_rack );
                add_msg( m_info, _( "You stop the smoking process." ) );
            } else {
                here.furn_set( examp, f_smoking_rack );
                add_msg( m_info, _( "You stop the smoking process." ) );
            }
            break;
        case 8:
            here.furn_set( examp, f_metal_smoking_rack );
            deployed_furniture( p, examp );
            break;
    }
}
