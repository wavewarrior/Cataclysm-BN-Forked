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

void iexamine::kiln_empty( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    furn_id cur_kiln_type = here.furn( examp );
    furn_id next_kiln_type = f_null;
    if( cur_kiln_type == f_kiln_empty ) {
        next_kiln_type = f_kiln_full;
    } else if( cur_kiln_type == f_kiln_metal_empty ) {
        next_kiln_type = f_kiln_metal_full;
    } else {
        debugmsg( "Examined furniture has action kiln_empty, but is of type %s",
                  here.furn( examp ).id().c_str() );
        return;
    }

    static const std::set<material_id> kilnable{ material_id( "wood" ), material_id( "bone" ), material_id( "bone_heavy" ) };
    bool fuel_present = false;
    auto items = here.i_at( examp );
    for( const item * const &i : items ) {
        if( i->typeId() == itype_charcoal ) {
            add_msg( _( "This kiln already contains charcoal." ) );
            add_msg( _( "Remove it before firing the kiln again." ) );
            return;
        } else if( i->made_of_any( kilnable ) ) {
            fuel_present = true;
        } else {
            add_msg( m_bad, _( "This kiln contains %s, which can't be made into charcoal!" ), i->tname( 1,
                     false ) );
            return;
        }
    }

    if( !fuel_present ) {
        add_msg( _( "This kiln is empty.  Fill it with wood or bone and try again." ) );
        return;
    }

    ///\EFFECT_FABRICATION decreases loss when firing a kiln
    const int skill = p.get_skill_level( skill_fabrication );
    int loss = 60 - 2 *
               skill; // We can afford to be inefficient - logs and skeletons are cheap, charcoal isn't

    // Burn stuff that should get charred, leave out the rest
    units::volume total_volume = 0_ml;
    for( const item * const &i : items ) {
        total_volume += i->volume();
    }

    units::volume char_volume = ( 100 - loss ) * total_volume / 100;
    int char_charges = itype_unfinished_charcoal->charges_per_volume( char_volume );
    if( char_charges < 1 ) {
        add_msg( _( "The batch in this kiln is too small to yield any charcoal." ) );
        return;
    }

    if( !p.has_charges( itype_fire, 1 ) ) {
        add_msg( _( "This kiln is ready to be fired, but you have no fire source." ) );
        return;
    } else {
        add_msg( _( "This kiln contains %s %s of material, and is ready to be fired." ),
                 format_volume( total_volume ), volume_units_abbr() );
        if( !query_yn( _( "Fire the kiln?" ) ) ) {
            return;
        }
    }

    p.use_charges( itype_fire, 1 );
    here.i_clear( examp );
    here.furn_set( examp, next_kiln_type );
    detached_ptr<item> result = item::spawn( itype_unfinished_charcoal, calendar::turn );
    result->charges = char_charges;
    here.add_item( examp, std::move( result ) );
    add_msg( _( "You fire the charcoal kiln." ) );
}

void iexamine::kiln_full( player &, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    furn_id cur_kiln_type = here.furn( examp );
    furn_id next_kiln_type = f_null;
    if( cur_kiln_type == f_kiln_full ) {
        next_kiln_type = f_kiln_empty;
    } else if( cur_kiln_type == f_kiln_metal_full ) {
        next_kiln_type = f_kiln_metal_empty;
    } else {
        debugmsg( "Examined furniture has action kiln_full, but is of type %s",
                  here.furn( examp ).id().c_str() );
        return;
    }

    map_stack items = here.i_at( examp );
    if( items.empty() ) {
        add_msg( _( "This kiln is empty…" ) );
        here.furn_set( examp, next_kiln_type );
        return;
    }
    add_msg( _( "There's a charcoal kiln there." ) );
    const time_duration firing_time = 6_hours; // 5 days in real life
    const time_duration time_left = firing_time - items.only_item().age();
    if( time_left > 0_turns ) {
        int hours = to_hours<int>( time_left );
        int minutes = to_minutes<int>( time_left ) + 1;
        if( minutes > 60 ) {
            add_msg( vgettext( "It will finish burning in about %d hour.",
                               "It will finish burning in about %d hours.",
                               hours ), hours );
        } else if( minutes > 30 ) {
            add_msg( _( "It will finish burning in less than an hour." ) );
        } else {
            add_msg( _( "It should take about %d minutes to finish burning." ), minutes );
        }
        return;
    }

    units::volume total_volume = 0_ml;
    // Burn stuff that should get charred, leave out the rest
    for( auto item_it = items.begin(); item_it != items.end(); ) {
        if( ( *item_it )->typeId() == itype_unfinished_charcoal ||
            ( *item_it )->typeId() == itype_charcoal ) {
            total_volume += ( *item_it )->volume();
            item_it = items.erase( item_it );
        } else {
            item_it++;
        }
    }
    detached_ptr<item> result = item::spawn( itype_charcoal, calendar::turn );
    result->charges = itype_charcoal->charges_per_volume( total_volume );
    add_msg( _( "It has finished burning, yielding %d charcoal." ), result->charges );
    here.add_item( examp, std::move( result ) );
    here.furn_set( examp, next_kiln_type );
}
//arc furnance start
void iexamine::arcfurnace_empty( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    furn_id cur_arcfurnace_type = here.furn( examp );
    furn_id next_arcfurnace_type = f_null;
    if( cur_arcfurnace_type == f_arcfurnace_empty ) {
        next_arcfurnace_type = f_arcfurnace_full;
    } else {
        debugmsg( "Examined furniture has action arcfurnace_empty, but is of type %s",
                  here.furn( examp ).id().c_str() );
        return;
    }

    static const std::set<material_id> arcfurnaceable{ material_id( "cac2powder" ) };
    bool fuel_present = false;
    auto items = here.i_at( examp );
    for( const item * const &i : items ) {
        if( i->typeId() == itype_chem_carbide ) {
            add_msg( _( "This furnace already contains calcium carbide." ) );
            add_msg( _( "Remove it before activating the arc furnace again." ) );
            return;
        } else if( i->made_of_any( arcfurnaceable ) ) {
            fuel_present = true;
        } else {
            add_msg( m_bad, _( "This furnace contains %s, which can't be made into calcium carbide!" ),
                     i->tname( 1, false ) );
            return;
        }
    }

    if( !fuel_present ) {
        add_msg( _( "This furance is empty.  Fill it with powdered coke and lime mix, and try again." ) );
        return;
    }

    ///\EFFECT_FABRICATION decreases loss when firing a furnace
    const int skill = p.get_skill_level( skill_fabrication );
    int loss = 60 - 2 *
               skill; // Inefficency is still fine, coal and limestone is abundant

    // Burn stuff that should get charred, leave out the rest
    units::volume total_volume = 0_ml;
    for( const item * const &i : items ) {
        total_volume += i->volume();
    }

    units::volume char_volume = ( 100 - loss ) * total_volume / 100;
    int char_charges = itype_unfinished_cac2->charges_per_volume( char_volume );
    if( char_charges < 1 ) {
        add_msg( _( "The batch in this furance is too small to yield usable calcium carbide." ) );
        return;
    }
    //arc furnaces require a huge amount of current, so 1 full storage battery would work as a stand in
    if( !p.has_charges( itype_UPS, 1250 ) ) {
        add_msg( _( "This furnace is ready to be turned on, but you lack a UPS with sufficient power." ) );
        return;
    } else {
        add_msg( _( "This furnace contains %s %s of material, and is ready to be turned on." ),
                 format_volume( total_volume ), volume_units_abbr() );
        if( !query_yn( _( "Turn on the furnace?" ) ) ) {
            return;
        }
    }

    p.use_charges( itype_UPS, 1250 );
    here.i_clear( examp );
    here.furn_set( examp, next_arcfurnace_type );
    detached_ptr<item> result = item::spawn( itype_unfinished_cac2, calendar::turn );
    result->charges = char_charges;
    here.add_item( examp, std::move( result ) );
    add_msg( _( "You turn on the furnace." ) );
}

void iexamine::arcfurnace_full( player &, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    furn_id cur_arcfurnace_type = here.furn( examp );
    furn_id next_arcfurnace_type = f_null;
    if( cur_arcfurnace_type == f_arcfurnace_full ) {
        next_arcfurnace_type = f_arcfurnace_empty;
    } else {
        debugmsg( "Examined furniture has action arcfurnace_full, but is of type %s",
                  here.furn( examp ).id().c_str() );
        return;
    }

    map_stack items = here.i_at( examp );
    if( items.empty() ) {
        add_msg( _( "This furnace is empty…" ) );
        here.furn_set( examp, next_arcfurnace_type );
        return;
    }
    add_msg( _( "There's an arc furnace there." ) );
    const time_duration firing_time = 2_hours; // Arc furnaces work really fast in reality
    const time_duration time_left = firing_time - items.only_item().age();
    if( time_left > 0_turns ) {
        int hours = to_hours<int>( time_left );
        int minutes = to_minutes<int>( time_left ) + 1;
        if( minutes > 60 ) {
            add_msg( vgettext( "It will finish burning in about %d hour.",
                               "It will finish burning in about %d hours.",
                               hours ), hours );
        } else if( minutes > 30 ) {
            add_msg( _( "It will finish burning in less than an hour." ) );
        } else {
            add_msg( _( "It should take about %d minutes to finish burning." ), minutes );
        }
        return;
    }

    units::volume total_volume = 0_ml;
    // Burn stuff that should get charred, leave out the rest
    for( auto item_it = items.begin(); item_it != items.end(); ) {
        if( ( *item_it )->typeId() == itype_unfinished_cac2 ||
            ( *item_it )->typeId() == itype_chem_carbide ) {
            total_volume += ( *item_it )->volume();
            item_it = items.erase( item_it );
        } else {
            item_it++;
        }
    }

    detached_ptr<item> result = item::spawn( itype_chem_carbide, calendar::turn );
    result->charges = itype_chem_carbide->charges_per_volume( total_volume );

    add_msg( _( "It has finished burning, yielding %d calcium carbide." ), result->charges );
    here.add_item( examp, std::move( result ) );
    here.furn_set( examp, next_arcfurnace_type );
}
//arc furnace end

void iexamine::autoclave_empty( player &p, const tripoint_bub_ms & )
{
    item *bionic = game_menus::inv::sterilize_cbm( p );
    if( bionic ) {
        avatar_funcs::mend_item( *p.as_avatar(), *bionic );
    } else {
        add_msg( _( "Never mind." ) );
    }
}

void iexamine::autoclave_full( player &, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    furn_id cur_autoclave_type = here.furn( examp );
    furn_id next_autoclave_type = f_null;
    if( cur_autoclave_type == furn_id( "f_autoclave_full" ) ) {
        next_autoclave_type = furn_id( "f_autoclave" );
    } else {
        debugmsg( "Examined furniture has action autoclave_full, but is of type %s",
                  here.furn( examp ).id().c_str() );
        return;
    }

    map_stack items = here.i_at( examp );
    bool cbms = std::ranges::all_of( items, []( const item * const & i ) {
        return i->is_bionic();
    } );

    bool cbms_not_packed = std::ranges::all_of( items, []( const item * const & i ) {
        return i->is_bionic() && i->has_flag( flag_NO_PACKED );
    } );

    if( items.empty() ) {
        add_msg( _( "This autoclave is empty…" ) );
        here.furn_set( examp, next_autoclave_type );
        return;
    }
    if( !cbms ) {
        add_msg( m_bad,
                 _( "ERROR Autoclave can't process non CBM items." ) );
        return;
    }
    add_msg( _( "The autoclave is running." ) );

    const item &clock = **items.begin();
    const time_duration Cycle_time = 90_minutes;
    const time_duration time_left = Cycle_time - clock.age();

    if( time_left > 0_turns ) {
        add_msg( _( "The cycle will be complete in %s." ), to_string( time_left ) );
        return;
    }

    here.furn_set( examp, next_autoclave_type );
    for( item * const &it : items ) {
        if( !it->has_flag( flag_NO_PACKED ) ) {
            it->unset_flag( flag_NO_STERILE );
        }
    }
    add_msg( m_good, _( "The cycle is complete, the CBMs are now sterile." ) );

    if( cbms_not_packed ) {
        add_msg( m_info,
                 _( "CBMs in direct contact with the environment will almost immediately become contaminated." ) );
    }
    here.furn_set( examp, next_autoclave_type );
}

void iexamine::fireplace( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    const bool already_on_fire = here.has_nearby_fire( examp, 0 );
    const bool furn_is_deployed = !here.furn( examp ).obj().deployed_item.is_empty();

    std::multimap<int, item *> firestarters;
    for( item *it : p.items_with( []( const item & it ) {
    return it.has_flag( flag_FIRESTARTER ) || it.has_flag( flag_FIRE );
    } ) ) {
        const auto usef = it->type->get_use( "firestarter" );
        if( usef != nullptr && usef->get_actor_ptr() != nullptr ) {
            const auto actor = dynamic_cast<const firestarter_actor *>( usef->get_actor_ptr() );
            if( actor->can_use( p, *it, false, examp ).success() ) {
                firestarters.insert( std::pair<int, item *>( actor->moves_cost_fast, it ) );
            }
        }
    }

    const bool has_firestarter = !firestarters.empty();
    const bool has_bionic_firestarter = p.has_bionic( bio_lighter ) &&
                                        p.enough_power_for( bio_lighter );

    uilist selection_menu;
    selection_menu.text = _( "Select an action" );
    selection_menu.addentry( 0, true, 'g', _( "Get items" ) );
    if( !already_on_fire ) {
        selection_menu.addentry( 1, has_firestarter, 'f',
                                 has_firestarter ? _( "Start a fire" ) : _( "Start a fire… you'll need a fire source." ) );
        if( has_bionic_firestarter ) {
            selection_menu.addentry( 2, true, 'b', _( "Use a CBM to start a fire" ) );
        }
    } else {
        selection_menu.addentry( 4, true, 'e', _( "Extinguish fire" ) );
    }
    if( furn_is_deployed ) {
        selection_menu.addentry( 3, true, 't', _( "Take down the %s" ), here.furnname( examp ) );
    }
    selection_menu.query();

    switch( selection_menu.ret ) {
        case 0:
            none( p, examp );
            pickup::pick_up( examp, 0 );
            return;
        case 1: {
            for( auto &firestarter : firestarters ) {
                item *it = firestarter.second;
                const auto usef = it->type->get_use( "firestarter" );
                const auto actor = dynamic_cast<const firestarter_actor *>( usef->get_actor_ptr() );
                p.add_msg_if_player( _( "You attempt to start a fire with your %s…" ), it->tname() );
                const ret_val<bool> can_use = actor->can_use( p, *it, false, examp );
                if( can_use.success() ) {
                    const int charges = actor->use( p, *it, false, examp );
                    p.use_charges( it->typeId(), charges );
                    return;
                } else {
                    p.add_msg_if_player( m_bad, can_use.str() );
                }
            }
            p.add_msg_if_player( _( "You weren't able to start a fire." ) );
            return;
        }
        case 2: {
            if( !here.get_field( examp, fd_fire ) && here.add_field( examp, fd_fire, 1 ) ) {
                p.mod_power_level( -bio_lighter->power_activate );
                p.mod_moves( -to_moves<int>( 1_seconds ) );
            } else {
                p.add_msg_if_player( m_info, _( "You can't light a fire there." ) );
            }
            return;
        }
        case 3: {
            if( already_on_fire ) {
                if( !query_yn( _( "Really take down the %s while it's on fire?" ), here.furnname( examp ) ) ) {
                    return;
                }
            }
            p.add_msg_if_player( m_info, _( "You take down the %s." ),
                                 here.furnname( examp ) );
            take_down_deployed_furniture( examp, examp );
            return;
        }
        case 4: {
            here.remove_field( examp, fd_fire );
            p.mod_moves( -200 );
            p.add_msg_if_player( m_info, _( "With a few determined moves you put out the fire in the %s." ),
                                 here.furnname( examp ) );
            return;
        }
        default:
            none( p, examp );
            return;
    }
}

void iexamine::fvat_empty( player &p, const tripoint_bub_ms &examp )
{
    itype_id brew_type;
    std::string brew_nname;
    bool to_deposit = false;
    static const auto vat_volume = units::from_liter( 50 );
    bool vat_full = false;
    bool ferment = false;
    bool brew_present = false;
    int charges_on_ground = 0;
    map &here = get_map();
    auto items = here.i_at( examp );
    for( auto item_it = items.begin(); item_it != items.end(); ) {
        //TODO!: check
        if( !( *item_it )->is_brewable() || brew_present ) {
            // This isn't a brew or there was already another kind of brew inside,
            // so this has to be moved.
            // This will add items to a space near the vat, because it's flagged as NOITEM.
            detached_ptr<item> det;
            item_it = items.erase( item_it, &det );
            items.insert( std::move( det ) );
        } else {
            item_it++;
            brew_present = true;
        }
    }
    if( !brew_present ) {
        add_msg( _( "This keg is empty." ) );
        // TODO: Allow using brews from crafting inventory
        const auto b_inv = p.items_with( []( const item & it ) {
            return it.is_brewable();
        } );
        if( b_inv.empty() ) {
            add_msg( m_info, _( "You have no brew to ferment." ) );
            return;
        }
        // Make lists of unique typeids and names for the menu
        // Code shamelessly stolen from the crop planting function!
        std::vector<itype_id> b_types;
        std::vector<std::string> b_names;
        for( auto &b : b_inv ) {
            if( !std::ranges::contains( b_types, b->typeId() ) ) {
                b_types.push_back( b->typeId() );
                b_names.push_back( item::nname( b->typeId() ) );
            }
        }
        // Choose brew from list
        int b_index = 0;
        if( b_types.size() > 1 ) {
            b_index = uilist( _( "Use which brew?" ), b_names );
        } else { //Only one brew type was in inventory, so it's automatically used
            if( !query_yn( _( "Set %s in the vat?" ), b_names[0] ) ) {
                b_index = -1;
            }
        }
        if( b_index < 0 ) {
            return;
        }
        to_deposit = true;
        brew_type = b_types[b_index];
        brew_nname = item::nname( brew_type );
    } else {
        item &brew = here.i_at( examp ).only_item();
        brew_type = brew.typeId();
        brew_nname = item::nname( brew_type );
        charges_on_ground = brew.charges;
        add_msg( _( "This keg contains %s (%d), %0.f%% full." ),
                 brew.tname(), brew.charges, brew.volume() * 100.0 / vat_volume );
        enum options { ADD_BREW, REMOVE_BREW, START_FERMENT };
        uilist selectmenu;
        selectmenu.text = _( "Select an action" );
        selectmenu.addentry( ADD_BREW, ( p.charges_of( brew_type ) > 0 ), MENU_AUTOASSIGN,
                             _( "Add more %s to the vat" ), brew_nname );
        selectmenu.addentry( REMOVE_BREW, brew.made_of( LIQUID ), MENU_AUTOASSIGN,
                             _( "Remove %s from the vat" ), brew.tname() );
        selectmenu.addentry( START_FERMENT, true, MENU_AUTOASSIGN, _( "Start fermenting cycle" ) );
        selectmenu.query();
        switch( selectmenu.ret ) {
            case ADD_BREW: {
                to_deposit = true;
                break;
            }
            case REMOVE_BREW: {
                liquid_handler::handle_liquid( **here.i_at( examp ).begin() );
                return;
            }
            case START_FERMENT: {
                ferment = true;
                break;
            }
            default:
                add_msg( _( "Never mind." ) );
                return;
        }
    }
    if( to_deposit ) {
        detached_ptr<item> brew = item::spawn( brew_type, calendar::start_of_cataclysm );
        int charges_held = p.charges_of( brew_type );
        brew->charges = charges_on_ground;
        for( int i = 0; i < charges_held && !vat_full; i++ ) {
            p.use_charges( brew_type, 1 );
            brew->charges++;
            if( brew->volume() >= vat_volume ) {
                vat_full = true;
            }
        }
        add_msg( _( "Set %s in the vat." ), brew_nname );
        add_msg( _( "The keg now contains %s (%d), %0.f%% full." ),
                 brew->tname(), brew->charges, brew->volume() * 100.0 / vat_volume );
        here.i_clear( examp );
        //This is needed to bypass NOITEM
        here.add_item( examp, std::move( brew ) );
        p.moves -= to_moves<int>( 20_seconds );
        if( !vat_full ) {
            ferment = query_yn( _( "Start fermenting cycle?" ) );
        }
    }
    if( vat_full || ferment ) {
        here.i_at( examp ).only_item().set_age( 0_turns );
        here.furn_set( examp, f_fvat_full );
        if( vat_full ) {
            add_msg( _( "The vat is full, so you close the lid and start the fermenting cycle." ) );
        } else {
            add_msg( _( "You close the lid and start the fermenting cycle." ) );
        }
    }
}

void iexamine::fvat_full( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    map_stack items_here = here.i_at( examp );
    if( items_here.empty() ) {
        debugmsg( "fvat_full was empty!" );
        here.furn_set( examp, f_fvat_empty );
        return;
    }

    for( auto it = items_here.begin(); it != items_here.end(); ) {
        if( !( *it )->made_of( LIQUID ) ) {
            add_msg( _( "You remove %s from the vat." ), ( *it )->tname() );
            detached_ptr<item> det;
            it = here.i_rem( examp, it, &det );
            here.add_item_or_charges( p.bub_pos(), std::move( det ) );
        } else {
            it++;
        }
    }

    if( items_here.empty() ) {
        here.furn_set( examp, f_fvat_empty );
        return;
    }

    item &brew_i = **items_here.begin();
    // Does the vat contain unfermented brew, or already fermented booze?
    // TODO: Allow "recursive brewing" to continue without player having to check on it
    if( brew_i.is_brewable() ) {
        add_msg( _( "There's a vat of %s set to ferment there." ), brew_i.tname() );

        // TODO: change brew_time to return time_duration
        const time_duration brew_time = brew_i.brewing_time();
        const time_duration progress = brew_i.age();
        if( progress < brew_time ) {
            int hours = to_hours<int>( brew_time - progress );
            if( hours < 1 ) {
                add_msg( _( "It will finish brewing in less than an hour." ) );
            } else {
                add_msg( vgettext( "It will finish brewing in about %d hour.",
                                   "It will finish brewing in about %d hours.",
                                   hours ), hours );
            }
            return;
        }

        if( query_yn( _( "Finish brewing?" ) ) ) {
            const auto results = brew_i.brewing_results();

            here.i_clear( examp );
            for( const auto &result : results ) {
                // TODO: Different age based on settings
                detached_ptr<item> booze = item::spawn( result, brew_i.birthday(), brew_i.charges );
                if( booze->made_of( LIQUID ) ) {
                    add_msg( _( "The %s is now ready for bottling." ), booze->tname() );
                }
                here.add_item( examp, std::move( booze ) );
            }

            p.moves -= to_moves<int>( 5_seconds );
            p.practice( skill_cooking, std::min( to_minutes<int>( brew_time ) / 10, 100 ) );
        }

        return;
    } else {
        add_msg( _( "There's a vat of fermented %s there." ), brew_i.tname() );
    }

    const std::string booze_name = brew_i.tname();
    liquid_handler::handle_liquid( **items_here.begin() );
    if( items_here.empty() ) {
        add_msg( _( "You squeeze the last drops of %s from the vat." ), booze_name );
        here.furn_set( examp, f_fvat_empty );
        return;
    }
}

static auto fluid_grid_tank_capacity( const furn_t &furn ) -> std::optional<units::volume>
{
    if( !furn.fluid_grid ) {
    return std::nullopt;
}
const auto &fluid_grid = *furn.fluid_grid;
if( fluid_grid.role != fluid_grid_role::tank ) {
    return std::nullopt;
}
if( fluid_grid.capacity ) {
    return fluid_grid.capacity;
}
if( fluid_grid.use_keg_capacity ) {
    return furn.keg_capacity;
}
return std::nullopt;
}

static auto is_fluid_grid_tank( const furn_t &furn ) -> bool
{
    return fluid_grid_tank_capacity( furn ).has_value();
}

static auto confirm_fluid_grid_contamination( const tripoint_abs_omt &pos_abs_omt,
        const itype_id &liquid_type ) -> bool
{
    if( !fluid_grid::would_contaminate( pos_abs_omt, liquid_type ) ) {
    return true;
}
const auto clean_available =
    fluid_grid::liquid_charges_at( pos_abs_omt, itype_water_clean ) > 0;
const auto dirty_available =
    fluid_grid::liquid_charges_at( pos_abs_omt, itype_water ) > 0;
if( liquid_type == itype_water_clean && dirty_available ) {
    return query_yn(
               _( "Adding clean water to this grid containing tainted water will contaminate your clean water.  Continue?" ) );
    }
    if( liquid_type == itype_water && clean_available ) {
    return query_yn(
               _( "Adding tainted water to this grid containing clean water will contaminate your clean water.  Continue?" ) );
    }
    return query_yn( string_format(
                         _( "Adding %s will contaminate the fluid grid's water supply.  Continue?" ),
                         item::nname( liquid_type ) ) );
}

static auto confirm_fluid_grid_contamination_for_items( const tripoint_abs_omt &pos_abs_omt,
        const map_stack &items ) -> bool
{
    const auto contaminating = std::ranges::find_if( items, [&]( const item * it ) {
        return it != nullptr && it->made_of( LIQUID ) &&
               fluid_grid::would_contaminate( pos_abs_omt, it->typeId() );
    } );
    if( contaminating == items.end() ) {
        return true;
    }
    return confirm_fluid_grid_contamination( pos_abs_omt, ( *contaminating )->typeId() );
}

static auto get_keg_capacity( const tripoint_bub_ms &pos ) -> units::volume
{
    const furn_t &furn = get_map().furn( pos ).obj();
    const auto capacity = fluid_grid_tank_capacity( furn );
    if( capacity ) {
        return *capacity;
    }
    return furn.keg_capacity;
}

/**
 * Check whether there is a keg on the map that can be filled via @ref pour_into_keg.
 */
bool iexamine::has_keg( const tripoint_bub_ms &pos )
{
    return get_keg_capacity( pos ) > 0_ml;
}

static void displace_items_except_one_liquid( const tripoint_bub_ms &examp )
{
    map &here = get_map();
    // Temporarily replace the real furniture with a fake furniture with NOITEM
    const furn_id previous_furn = here.furn( examp );
    here.furn_set( examp, furn_id( "f_no_item" ) );

    bool liquid_present = false;
    map_stack items = here.i_at( examp );
    for( map_stack::iterator it = items.begin(); it != items.end(); ) {
        //TODO!:check
        if( !( *it )->made_of( LIQUID ) || liquid_present ) {
            // This isn't a liquid or there was already another kind of liquid inside,
            // so this has to be moved.
            // This will add items to a space near the vat, because it's flagged as NOITEM.
            detached_ptr<item> det;
            it = items.erase( it, &det );
            items.insert( std::move( det ) );
        } else {
            it++;
            liquid_present = true;
        }
    }

    // Replace the real furniture
    here.furn_set( examp, previous_furn );
}

void iexamine::keg( player &p, const tripoint_bub_ms &examp )
{
    none( p, examp );
    map &here = get_map();
    const auto keg_name = here.name( examp );
    units::volume keg_cap = get_keg_capacity( examp );
    const auto furn_id = here.furn( examp );
    const auto &furn = furn_id.obj();
    const auto is_plumbed_tank = is_fluid_grid_tank( furn );
    const auto connected_variant = fluid_grid_connected_variant( furn_id );
    const auto disconnected_variant = fluid_grid_disconnected_variant( furn_id );
    const auto can_plumb_tank = connected_variant && p.has_amount( itype_plumber_toolkit, 1 );
    const auto can_disconnect_tank = disconnected_variant && p.has_amount( itype_plumber_toolkit, 1 );
    const auto notify_contents_changed = [&]( const tripoint_bub_ms & where ) {
        if( is_fluid_grid_tank( here.furn( where ).obj() ) ) {
            fluid_grid::on_contents_changed( here.bub_to_abs( where ) );
        }
    };
    const auto tank_contains_only_water = [&]( const tripoint_bub_ms & where ) -> bool {
        auto items = here.i_at( where );
        const auto has_non_water = std::ranges::any_of( items, [&]( const item * it )
        {
            return it != nullptr && it->made_of( LIQUID ) &&
            it->typeId() != itype_water && it->typeId() != itype_water_clean;
        } );
        if( has_non_water )
        {
            add_msg( m_info, _( "The %s contains non-water liquids and cannot be connected." ), keg_name );
            return false;
        }
        return true;
    };
    const auto transfer_tank_liquid_to_grid = [&]( const tripoint_bub_ms & where ) {
        const auto pos_abs_omt = project_to<coords::omt>( here.bub_to_abs( where ) );
        auto items = here.i_at( where );
        std::ranges::for_each( items, [&]( item * it ) {
            if( it != nullptr && it->made_of( LIQUID ) ) {
                fluid_grid::add_liquid_charges( pos_abs_omt, it->typeId(), it->charges );
            }
        } );
        here.i_clear( where );
    };

    if( is_plumbed_tank ) {
        const auto pos_abs_ms = here.bub_to_abs( examp );
        const auto pos_abs_omt = project_to<coords::omt>( pos_abs_ms );
        const auto clean_available = fluid_grid::liquid_charges_at( pos_abs_omt, itype_water_clean );
        const auto dirty_available = fluid_grid::liquid_charges_at( pos_abs_omt, itype_water );
        const auto available = clean_available > 0 ? clean_available : dirty_available;
        const auto &liquid_type = clean_available > 0 ? itype_water_clean : itype_water;

        if( available <= 0 ) {
            add_msg( m_info, _( "It is empty." ) );
        }

        enum options {
            DISPENSE,
            HAVE_A_DRINK,
            FILL,
            EXAMINE,
            DISCONNECT_FROM_FLUID_GRID,
        };
        uilist selectmenu;
        selectmenu.addentry( DISPENSE, available > 0, MENU_AUTOASSIGN,
                             _( "Dispense or dump %s" ), item::nname( liquid_type ) );
        selectmenu.addentry( HAVE_A_DRINK, available > 0, MENU_AUTOASSIGN,
                             _( "Have a drink" ) );
        selectmenu.addentry( FILL, true, MENU_AUTOASSIGN, _( "Fill" ) );
        selectmenu.addentry( EXAMINE, true, MENU_AUTOASSIGN, _( "Examine" ) );
        if( can_disconnect_tank ) {
            selectmenu.addentry( DISCONNECT_FROM_FLUID_GRID, true, MENU_AUTOASSIGN,
                                 _( "Disconnect from fluid grid" ) );
        }
        selectmenu.text = _( "Select an action" );
        selectmenu.query();

        const auto use_grid_liquid = [&]( const auto & fn ) -> int {
            auto target_sm = tripoint_abs_sm{};
            auto target_pos = point_sm_ms{};
            std::tie( target_sm, target_pos ) = project_remain<coords::sm>( pos_abs_ms );
            auto *target_submap = MAPBUFFER_REGISTRY.get(
                                      get_map().get_bound_dimension() ).lookup_submap( target_sm );
            if( target_submap == nullptr )
            {
                return 0;
            }

            auto &items = target_submap->get_items( target_pos );
            auto liquid_item = item::spawn( liquid_type, calendar::turn, available );
            auto iter = items.insert( items.end(), std::move( liquid_item ) );
            item *water_item = *iter;
            const auto before = water_item->charges;
            fn( *water_item );
            const auto &item_ptrs = items.as_vector();
            const auto still_here = std::ranges::find( item_ptrs, water_item ) != item_ptrs.end();
            if( still_here )
            {
                const auto used = before - water_item->charges;
                items.remove( water_item );
                return used;
            }
            return before;
        };

        switch( selectmenu.ret ) {
            case DISPENSE: {
                const auto used = use_grid_liquid( [&]( item & water_item ) {
                    liquid_handler::handle_liquid( water_item );
                } );
                if( used > 0 ) {
                    fluid_grid::drain_liquid_charges( pos_abs_omt, liquid_type, used );
                }
                return;
            }

            case HAVE_A_DRINK: {
                auto target_sm = tripoint_abs_sm{};
                auto target_pos = point_sm_ms{};
                std::tie( target_sm, target_pos ) = project_remain<coords::sm>( pos_abs_ms );
                auto *target_submap = MAPBUFFER_REGISTRY.get(
                                          get_map().get_bound_dimension() ).lookup_submap( target_sm );
                if( target_submap == nullptr ) {
                    return;
                }

                auto &items = target_submap->get_items( target_pos );
                auto liquid_item = item::spawn( liquid_type, calendar::turn, available );
                auto iter = items.insert( items.end(), std::move( liquid_item ) );
                item *water_item = *iter;
                const auto before = water_item->charges;
                if( !p.eat( *water_item ) ) {
                    const auto &item_ptrs = items.as_vector();
                    const auto still_here = std::ranges::find( item_ptrs, water_item ) != item_ptrs.end();
                    if( still_here ) {
                        items.remove( water_item );
                    }
                    return;
                }
                auto used = 0;
                const auto &item_ptrs = items.as_vector();
                const auto still_here = std::ranges::find( item_ptrs, water_item ) != item_ptrs.end();
                if( still_here ) {
                    used = before - water_item->charges;
                    items.remove( water_item );
                } else {
                    used = before;
                }
                if( used > 0 ) {
                    fluid_grid::drain_liquid_charges( pos_abs_omt, liquid_type, used );
                    p.moves -= to_moves<int>( 5_seconds );
                }
                return;
            }

            case FILL: {
                auto drinks_inv = p.items_with( []( const item & it ) {
                    return it.typeId() == itype_water || it.typeId() == itype_water_clean;
                } );
                if( drinks_inv.empty() ) {
                    add_msg( m_info, _( "You don't have any water to fill the %s with." ), keg_name );
                    return;
                }
                auto drink_types = std::vector<itype_id> {};
                auto drink_names = std::vector<std::string> {};
                std::ranges::for_each( drinks_inv, [&]( const auto & drink ) {
                    if( std::ranges::find( drink_types, drink->typeId() ) == drink_types.end() ) {
                        drink_types.push_back( drink->typeId() );
                        drink_names.push_back( item::nname( drink->typeId() ) );
                    }
                } );

                auto drink_index = 0;
                if( drink_types.size() > 1 ) {
                    drink_index = uilist( _( "Store which drink?" ), drink_names );
                    if( drink_index < 0 || static_cast<size_t>( drink_index ) >= drink_types.size() ) {
                        drink_index = -1;
                    }
                } else {
                    if( !query_yn( _( "Fill the %1$s with %2$s?" ),
                                   keg_name, drink_names[0].c_str() ) ) {
                        drink_index = -1;
                    }
                }
                if( drink_index < 0 ) {
                    return;
                }

                const auto drink_type = drink_types[ drink_index ];
                const auto charges_held = p.charges_of( drink_type );
                if( !confirm_fluid_grid_contamination( pos_abs_omt, drink_type ) ) {
                    return;
                }
                const auto added = fluid_grid::add_liquid_charges( pos_abs_omt, drink_type, charges_held );
                if( added <= 0 ) {
                    add_msg( m_info, _( "The %s cannot hold any more water." ), keg_name );
                    return;
                }
                p.use_charges( drink_type, added );
                add_msg( _( "You fill the %1$s with %2$s." ), keg_name, item::nname( drink_type ) );
                p.moves -= to_moves<int>( 10_seconds );
                return;
            }

            case EXAMINE: {
                const auto fluid_stats = fluid_grid::storage_stats_at( pos_abs_omt );
                add_msg( m_info, _( "Fluid stored: %1$s/%2$s %3$s." ),
                         format_volume( fluid_stats.stored ),
                         format_volume( fluid_stats.capacity ),
                         volume_units_abbr() );
                const auto stored_count = std::ranges::count_if( fluid_stats.stored_by_type,
                []( const auto & entry ) {
                    return entry.second > 0_ml;
                } );
                auto fluid_type = std::string{};
                if( stored_count == 0 ) {
                    fluid_type = _( "empty" );
                } else if( stored_count == 1 ) {
                    const auto iter = std::ranges::find_if( fluid_stats.stored_by_type,
                    []( const auto & entry ) {
                        return entry.second > 0_ml;
                    } );
                    if( iter != fluid_stats.stored_by_type.end() ) {
                        fluid_type = item::nname( iter->first );
                    } else {
                        fluid_type = _( "empty" );
                    }
                } else {
                    fluid_type = _( "mixed fluids" );
                }
                add_msg( m_info, _( "Fluid type: %s." ), fluid_type );
                return;
            }

            case DISCONNECT_FROM_FLUID_GRID:
                fluid_grid::disconnect_tank( pos_abs_ms );
                if( !disconnected_variant ) {
                    return;
                }
                here.furn_set( examp, *disconnected_variant );
                fluid_grid::on_structure_changed( pos_abs_ms );
                add_msg( m_info, _( "You disconnect the %s from the fluid grid." ), keg_name );
                return;

            default:
                return;
        }
    }

    const bool has_container_with_liquid = map_cursor( examp ).has_item_with( []( const item & it ) {
        return !it.is_container_empty() && it.can_unload_liquid();
    } );
    const bool liquid_present = map_cursor( examp ).has_item_with( []( const item & it ) {
        return it.made_of( LIQUID );
    } );

    if( !liquid_present || has_container_with_liquid ) {
        add_msg( m_info, _( "It is empty." ) );
        if( can_plumb_tank || can_disconnect_tank ) {
            enum options {
                ADD_TO_FLUID_GRID,
                DISCONNECT_FROM_FLUID_GRID,
                FILL,
            };
            uilist selectmenu;
            if( can_plumb_tank ) {
                selectmenu.addentry( ADD_TO_FLUID_GRID, true, MENU_AUTOASSIGN,
                                     _( "Add to fluid grid" ) );
            }
            if( can_disconnect_tank ) {
                selectmenu.addentry( DISCONNECT_FROM_FLUID_GRID, true, MENU_AUTOASSIGN,
                                     _( "Disconnect from fluid grid" ) );
            }
            selectmenu.addentry( FILL, true, MENU_AUTOASSIGN, _( "Fill" ) );
            selectmenu.text = _( "Select an action" );
            selectmenu.query();
            if( selectmenu.ret == ADD_TO_FLUID_GRID ) {
                if( !tank_contains_only_water( examp ) ) {
                    return;
                }
                displace_items_except_one_liquid( examp );
                if( !connected_variant ) {
                    return;
                }
                const auto pos_abs_omt = project_to<coords::omt>( here.bub_to_abs( examp ) );
                if( !confirm_fluid_grid_contamination_for_items( pos_abs_omt, here.i_at( examp ) ) ) {
                    return;
                }
                here.furn_set( examp, *connected_variant );
                fluid_grid::on_structure_changed( here.bub_to_abs( examp ) );
                transfer_tank_liquid_to_grid( examp );
                add_msg( m_info, _( "You connect the %s to the fluid grid." ), keg_name );
                return;
            } else if( selectmenu.ret == DISCONNECT_FROM_FLUID_GRID ) {
                fluid_grid::disconnect_tank( here.bub_to_abs( examp ) );
                if( !disconnected_variant ) {
                    return;
                }
                here.furn_set( examp, *disconnected_variant );
                fluid_grid::on_structure_changed( here.bub_to_abs( examp ) );
                add_msg( m_info, _( "You disconnect the %s from the fluid grid." ), keg_name );
                return;
            } else if( selectmenu.ret < 0 ) {
                return;
            }
        }
        // Get list of all drinks
        auto drinks_inv = p.items_with( []( const item & it ) {
            return it.made_of( LIQUID );
        } );
        if( drinks_inv.empty() ) {
            add_msg( m_info, _( "You don't have any drinks to fill the %s with." ), keg_name );
            return;
        }
        // Make lists of unique drinks... about third time we do this, maybe we ought to make a function next time
        std::vector<itype_id> drink_types;
        std::vector<std::string> drink_names;
        std::vector<double> drink_rot;
        for( auto &drink : drinks_inv ) {
            auto found_drink = std::ranges::find( drink_types, drink->typeId() );
            if( found_drink == drink_types.end() ) {
                drink_types.push_back( drink->typeId() );
                drink_names.push_back( item::nname( drink->typeId() ) );
                drink_rot.push_back( drink->get_relative_rot() );
            } else {
                auto rot_iter = std::next( drink_rot.begin(), std::distance( drink_types.begin(), found_drink ) );
                // Yep, worst rot wins.
                *rot_iter = std::max( *rot_iter, drink->get_relative_rot() );
            }
        }
        // Choose drink to store in keg from list
        int drink_index = 0;
        if( drink_types.size() > 1 ) {
            drink_index = uilist( _( "Store which drink?" ), drink_names );
            if( drink_index < 0 || static_cast<size_t>( drink_index ) >= drink_types.size() ) {
                drink_index = -1;
            }
        } else { //Only one drink type was in inventory, so it's automatically used
            if( !query_yn( _( "Fill the %1$s with %2$s?" ),
                           keg_name, drink_names[0].c_str() ) ) {
                drink_index = -1;
            }
        }
        if( drink_index < 0 ) {
            return;
        }

        // First empty the keg of foreign objects
        displace_items_except_one_liquid( examp );

        //Store liquid chosen in the keg
        itype_id drink_type = drink_types[ drink_index ];
        int charges_held = p.charges_of( drink_type );
        detached_ptr<item> drink = item::spawn( drink_type, calendar::start_of_cataclysm );
        drink->set_relative_rot( drink_rot[ drink_index ] );
        drink->charges = 0;
        bool keg_full = false;
        for( int i = 0; i < charges_held && !keg_full; i++ ) {
            g->u.use_charges( drink->typeId(), 1 );
            drink->charges++;
            keg_full = drink->volume() >= keg_cap;
        }
        if( keg_full ) {
            add_msg( _( "You completely fill the %1$s with %2$s." ),
                     keg_name, item::nname( drink_type ) );
        } else {
            add_msg( _( "You fill the %1$s with %2$s." ),
                     keg_name, item::nname( drink_type ) );
        }
        p.moves -= to_moves<int>( 10_seconds );
        here.i_clear( examp );
        here.add_item( examp, std::move( drink ) );
        notify_contents_changed( examp );
        return;
    } else {
        // First empty the keg of foreign objects
        displace_items_except_one_liquid( examp );

        map_stack items = here.i_at( examp );
        item &drink = items.only_item();
        const std::string drink_tname = drink.tname();
        const std::string drink_nname = item::nname( drink.typeId() );
        enum options {
            DISPENSE,
            HAVE_A_DRINK,
            REFILL,
            EXAMINE,
            ADD_TO_FLUID_GRID,
            DISCONNECT_FROM_FLUID_GRID,
        };
        uilist selectmenu;
        selectmenu.addentry( DISPENSE, drink.made_of( LIQUID ), MENU_AUTOASSIGN,
                             _( "Dispense or dump %s" ), drink_tname );
        selectmenu.addentry( HAVE_A_DRINK, drink.is_food() && drink.made_of( LIQUID ),
                             MENU_AUTOASSIGN, _( "Have a drink" ) );
        selectmenu.addentry( REFILL, true, MENU_AUTOASSIGN, _( "Refill" ) );
        selectmenu.addentry( EXAMINE, true, MENU_AUTOASSIGN, _( "Examine" ) );
        if( can_plumb_tank ) {
            selectmenu.addentry( ADD_TO_FLUID_GRID, true, MENU_AUTOASSIGN,
                                 _( "Add to fluid grid" ) );
        }
        if( can_disconnect_tank ) {
            selectmenu.addentry( DISCONNECT_FROM_FLUID_GRID, true, MENU_AUTOASSIGN,
                                 _( "Disconnect from fluid grid" ) );
        }

        selectmenu.text = _( "Select an action" );
        selectmenu.query();

        const auto pos_abs_omt = project_to<coords::omt>( here.bub_to_abs( examp ) );
        switch( selectmenu.ret ) {
            case DISPENSE:
                if( liquid_handler::handle_liquid( **items.begin() ) ) {
                    add_msg( _( "You squeeze the last drops of %1$s from the %2$s." ),
                             drink_tname, keg_name );
                }
                notify_contents_changed( examp );
                return;

            case HAVE_A_DRINK:
                if( !p.eat( drink ) ) {
                    return; // They didn't actually drink
                }

                if( drink.charges == 0 ) {
                    add_msg( _( "You squeeze the last drops of %1$s from the %2$s." ),
                             drink_tname, keg_name );
                    here.i_clear( examp );
                }
                notify_contents_changed( examp );
                p.moves -= to_moves<int>( 5_seconds );
                return;

            case REFILL: {
                if( drink.volume() >= keg_cap ) {
                    add_msg( _( "The %s is completely full." ), keg_name );
                    return;
                }
                int charges_held = p.charges_of( drink.typeId() );
                if( charges_held < 1 ) {
                    add_msg( m_info, _( "You don't have any %1$s to fill the %2$s with." ),
                             drink_nname, keg_name );
                    return;
                }
                detached_ptr<item> tmp = item::spawn( drink.typeId(), calendar::turn, charges_held );
                tmp = pour_into_keg( examp, std::move( tmp ) );
                p.use_charges( drink.typeId(), charges_held - tmp->charges );
                add_msg( _( "You fill the %1$s with %2$s." ), keg_name, drink_nname );
                notify_contents_changed( examp );
                p.moves -= to_moves<int>( 10_seconds );
                return;
            }

            case EXAMINE: {
                add_msg( m_info, _( "It contains %s (%d), %0.f%% full." ),
                         drink_tname, drink.charges, drink.volume() * 100.0 / keg_cap );
                return;
            }

            case ADD_TO_FLUID_GRID: {
                if( !tank_contains_only_water( examp ) ) {
                    return;
                }
                displace_items_except_one_liquid( examp );
                if( !connected_variant ) {
                    return;
                }
                if( !confirm_fluid_grid_contamination_for_items( pos_abs_omt, here.i_at( examp ) ) ) {
                    return;
                }
                here.furn_set( examp, *connected_variant );
                fluid_grid::on_structure_changed( here.bub_to_abs( examp ) );
                transfer_tank_liquid_to_grid( examp );
                add_msg( m_info, _( "You connect the %s to the fluid grid." ), keg_name );
                return;
            }

            case DISCONNECT_FROM_FLUID_GRID:
                fluid_grid::disconnect_tank( here.bub_to_abs( examp ) );
                if( !disconnected_variant ) {
                    return;
                }
                here.furn_set( examp, *disconnected_variant );
                fluid_grid::on_structure_changed( here.bub_to_abs( examp ) );
                add_msg( m_info, _( "You disconnect the %s from the fluid grid." ), keg_name );
                return;

            default:
                return;
        }
    }
}

/**
 * Pour liquid into a keg (furniture) on the map. The transferred charges (if any)
 * will be removed from the liquid item.
 * @return Any remaining liquid.
 */
detached_ptr<item> iexamine::pour_into_keg( const tripoint_bub_ms &pos,
        detached_ptr<item> &&liquid )
{
    const units::volume keg_cap = get_keg_capacity( pos );
    if( keg_cap <= 0_ml ) {
        return std::move( liquid );
    }
    map &here = get_map();
    const auto is_plumbed = is_fluid_grid_tank( here.furn( pos ).obj() );
    const auto notify_contents_changed = [&]( const tripoint_bub_ms & where ) {
        if( is_fluid_grid_tank( here.furn( where ).obj() ) ) {
            fluid_grid::on_contents_changed( here.bub_to_abs( where ) );
        }
    };
    const auto keg_name = here.name( pos );
    item &obj = *liquid;

    if( is_plumbed ) {
        if( liquid->typeId() != itype_water && liquid->typeId() != itype_water_clean ) {
            add_msg( _( "The %s only accepts water." ), keg_name );
            return std::move( liquid );
        }
        const auto pos_abs_omt = project_to<coords::omt>( here.bub_to_abs( pos ) );
        if( !confirm_fluid_grid_contamination( pos_abs_omt, liquid->typeId() ) ) {
            return std::move( liquid );
        }
        const auto added = fluid_grid::add_liquid_charges( pos_abs_omt, liquid->typeId(),
                           liquid->charges );
        if( added > 0 ) {
            add_msg( _( "You pour %1$s into the %2$s." ), obj.tname(), keg_name );
            liquid->charges -= added;
        }
        if( liquid->charges == 0 ) {
            return detached_ptr<item>();
        }
        return std::move( liquid );
    }

    auto stack = here.i_at( pos );
    if( stack.empty() ) {
        int charges = liquid->charges;
        here.add_item( pos, std::move( liquid ) );
        obj.charges = 0;
        while( charges > 0 && obj.volume() < keg_cap ) {
            obj.charges++;
            charges--;
        }
        add_msg( _( "You pour %1$s into the %2$s." ), obj.tname(), keg_name );
        if( charges > 0 ) {
            detached_ptr<item> ret = item::spawn( obj );
            ret->charges = charges;
            notify_contents_changed( pos );
            return ret;
        }
        notify_contents_changed( pos );
        return detached_ptr<item>();
    } else if( stack.only_item().typeId() != liquid->typeId() ) {
        add_msg( _( "The %s already contains some %s, you can't add a different liquid to it." ),
                 keg_name, item::nname( stack.only_item().typeId() ) );
        return std::move( liquid );
    } else {
        item &drink = stack.only_item();
        if( drink.volume() >= keg_cap ) {
            add_msg( _( "The %s is full." ), keg_name );
            return std::move( liquid );
        }
        while( liquid->charges > 0 && drink.volume() < keg_cap ) {
            drink.charges++;
            liquid->charges--;
        }
        add_msg( _( "You pour %1$s into the %2$s." ), obj.tname(), keg_name );
        if( liquid->charges == 0 ) {
            notify_contents_changed( pos );
            return detached_ptr<item>();
        }
    }

    notify_contents_changed( pos );
    return std::move( liquid );
}
