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

static bool dead_plant( bool flower, player &p, const tripoint_bub_ms &examp )
{
    if( season_of_year( calendar::turn ) == WINTER ) {
        if( flower ) {
            add_msg( m_info, _( "This flower is dead.  You can't get it." ) );
        } else {
            add_msg( m_info, _( "This plant is dead.  You can't get it." ) );
        }

        iexamine::none( p, examp );
        return true;
    }

    return false;
}

static bool can_drink_nectar( const player &p, const item &nectar )
{
    return ( p.has_active_mutation( trait_PROBOSCIS )  ||
             p.has_active_mutation( trait_BEAK_HUM ) ) &&
           ( ( p.max_stored_kcal() - p.get_stored_kcal() ) >
             nectar.get_comestible()->default_nutrition.kcal ) &&
           ( !( p.wearing_something_on( bodypart_id( "mouth" ) ) ) );
}

static bool drink_nectar( player &p )
{
    item &nectar = *item::spawn_temporary( "nectar", calendar::turn, 1 );
    if( can_drink_nectar( p, nectar ) ) {
        p.moves -= to_moves<int>( 30_seconds );
        add_msg( _( "You drink some nectar." ) );
        p.eat( nectar );
        return true;
    }

    return false;
}

static void handle_harvest( player &p, const std::string &itemid, bool force_drop )
{
    detached_ptr<item> harvest = item::spawn( itemid );
    if( harvest->has_flag( flag_FORAGE_POISON ) && one_in( 10 ) ) {
        harvest->set_flag( flag_HIDDEN_POISON );
        harvest->poison = rng( 2, 7 );
    }
    if( harvest->has_flag( flag_FORAGE_HALLU ) && !harvest->has_flag( flag_HIDDEN_POISON ) &&
        one_in( 10 ) ) {
        harvest->set_flag( flag_HIDDEN_HALLU );
    }
    // Drop items that're exceed available space and things that aren't comestibles
    if( !force_drop && p.can_pick_volume( *harvest ) &&
        p.can_pick_weight( *harvest, !get_option<bool>( "DANGEROUS_PICKUPS" ) ) ) {

        p.add_msg_if_player( _( "You harvest: %s." ), harvest->tname() );
        p.i_add( std::move( harvest ) );
    } else {
        p.add_msg_if_player( _( "You harvest and drop: %s." ), harvest->tname() );
        get_map().add_item_or_charges( p.bub_pos(), std::move( harvest ) );
    }
}

void iexamine::flower_poppy( player &p, const tripoint_bub_ms &examp )
{
    if( dead_plant( true, p, examp ) ) {
        return;
    }
    map &here = get_map();
    // TODO: Get rid of this section and move it to eating
    // Two y/n prompts is just too much
    item &poppy = *item::spawn_temporary( "nectar", calendar::turn, 1 );
    if( can_drink_nectar( p, poppy ) ) {
        if( !query_yn( _( "You feel woozy as you explore the %s. Drink?" ),
                       here.furnname( examp ) ) ) {
            return;
        }
        p.moves -= to_moves<int>( 30_seconds ); // You take your time...
        add_msg( _( "You slowly suck up the nectar." ) );
        p.eat( poppy );
        p.mod_fatigue( 20 );
        p.add_effect( effect_pkill2, 7_minutes );
        // Please drink poppy nectar responsibly.
        if( one_in( 20 ) ) {
            p.add_addiction( add_type::PKILLER, 1 );
        }
    }
    if( !query_yn( _( "Pick %s?" ), here.furnname( examp ) ) ) {
        none( p, examp );
        return;
    }

    int resist = p.get_env_resist( bodypart_id( "mouth" ) );

    if( resist < 10 ) {
        // Can't smell the flowers with a gas mask on!
        add_msg( m_warning, _( "This flower has a heady aroma." ) );
    }

    auto recentWeather = sum_conditions( calendar::turn - 10_minutes, calendar::turn, p.abs_pos() );

    // If it has been raining recently, then this event is twice less likely.
    if( ( ( recentWeather.rain_amount > 1 ) ? one_in( 6 ) : one_in( 3 ) ) && resist < 5 ) {
        // Should user player::infect, but can't!
        // player::infect needs to be restructured to return a bool indicating success.
        add_msg( m_bad, _( "You fall asleep…" ) );
        p.fall_asleep( 2_hours );
        add_msg( m_bad, _( "Your legs are covered in the poppy's roots!" ) );
        p.apply_damage( nullptr, bodypart_id( "leg_l" ), 4 );
        p.apply_damage( nullptr, bodypart_id( "leg_r" ), 4 );
        p.moves -= 50;
    }

    here.furn_set( examp, f_null );

    handle_harvest( p, "poppy_bud", false );
    handle_harvest( p, "withered", false );
}

void iexamine::flower_cactus( player &p, const tripoint_bub_ms &examp )
{
    if( dead_plant( true, p, examp ) ) {
        return;
    }

    map &here = get_map();
    if( !query_yn( _( "Pick %s?" ), here.furnname( examp ) ) ) {
        none( p, examp );
        return;
    }

    if( one_in( 6 ) ) {
        add_msg( m_bad, _( "The cactus' nettles sting you!" ) );
        p.apply_damage( nullptr, bodypart_id( "arm_l" ), 4 );
        p.apply_damage( nullptr, bodypart_id( "arm_r" ), 4 );
    }

    here.furn_set( examp, f_null );

    handle_harvest( p, "cactus_pad", false );
    handle_harvest( p, "seed_cactus", false );
}

void iexamine::flower_dahlia( player &p, const tripoint_bub_ms &examp )
{
    if( dead_plant( true, p, examp ) ) {
        return;
    }

    if( drink_nectar( p ) ) {
        return;
    }

    map &here = get_map();
    bool can_get_root = p.has_quality( qual_DIG ) || p.has_trait( trait_BURROW );
    if( can_get_root ) {
        if( !query_yn( _( "Pick %s?" ), here.furnname( examp ) ) ) {
            none( p, examp );
            return;
        }
    } else {
        if( !query_yn( _( "You don't have a digging tool to dig up roots.  Pick %s anyway?" ),
                       here.furnname( examp ) ) ) {
            none( p, examp );
            return;
        }
    }

    here.furn_set( examp, f_null );

    if( can_get_root ) {
        handle_harvest( p, "dahlia_root", false );
    }
    handle_harvest( p, "seed_dahlia", false );
    handle_harvest( p, "withered", false );
    // There was a bud and flower spawn here
    // But those were useless, don't re-add until they get useful
}

static bool harvest_common( player &p, const tripoint_bub_ms &examp, bool furn, bool nectar,
                            bool auto_forage = false )
{
    map &here = get_map();
    const auto hid = here.get_harvest( examp );
    if( hid.is_null() || hid->empty() ) {
        if( !auto_forage ) {
            p.add_msg_if_player( m_info, _( "Nothing can be harvested from this currently." ) );
        }
        if( p.manual_examine ) {
            iexamine::none( p, examp );
        }
        return false;
    }

    const auto &harvest = hid.obj();

    // If nothing can be harvested, neither can nectar
    // Incredibly low priority TODO: Allow separating nectar seasons
    if( nectar && drink_nectar( p ) ) {
        return false;
    }

    if( p.is_player() && !auto_forage &&
        !query_yn( _( "Pick %s?" ), furn ? here.furnname( examp ) : here.tername(
                       examp ) ) ) {
        iexamine::none( p, examp );
        return false;
    }

    int lev = p.get_skill_level( skill_survival );
    bool got_anything = false;
    for( const auto &entry : harvest ) {
        float min_num = entry.base_num.first + lev * entry.scale_num.first;
        float max_num = entry.base_num.second + lev * entry.scale_num.second;
        int roll = std::min<int>( entry.max, std::round( rng_float( min_num, max_num ) ) );
        if( roll >= 1 ) {
            got_anything = true;
            for( int i = 0; i < roll; i++ ) {
                handle_harvest( p, entry.drop, entry.no_auto_pickup );
            }
        }
    }

    if( !got_anything ) {
        p.add_msg_if_player( m_bad, _( "You couldn't harvest anything." ) );
    }

    iexamine::practice_survival_while_foraging( &p );

    p.mod_moves( -to_moves<int>( rng( 5_seconds, 15_seconds ) ) );
    return true;
}

void iexamine::harvest_furn_nectar( player &p, const tripoint_bub_ms &examp )
{
    bool auto_forage = get_option<bool>( "AUTO_FEATURES" ) &&
                       ( get_option<std::string>( "AUTO_FORAGING" ) == "flowers" ||
                         get_option<std::string>( "AUTO_FORAGING" ) == "both" );
    if( harvest_common( p, examp, true, true, auto_forage ) ) {
        map &here = get_map();
        get_map().furn_set( examp, here.get_furn_transforms_into( examp ) );
    }
}

void iexamine::harvest_furn( player &p, const tripoint_bub_ms &examp )
{
    bool auto_forage = get_option<bool>( "AUTO_FEATURES" ) &&
                       ( get_option<std::string>( "AUTO_FORAGING" ) == "flowers" ||
                         get_option<std::string>( "AUTO_FORAGING" ) == "both" );
    if( harvest_common( p, examp, true, false, auto_forage ) ) {
        map &here = get_map();
        get_map().furn_set( examp, here.get_furn_transforms_into( examp ) );
    }
}

void iexamine::harvest_ter_nectar( player &p, const tripoint_bub_ms &examp )
{
    bool auto_forage = get_option<bool>( "AUTO_FEATURES" ) &&
                       ( get_option<std::string>( "AUTO_FORAGING" ) == "both" ||
                         get_option<std::string>( "AUTO_FORAGING" ) == "bushes" ||
                         get_option<std::string>( "AUTO_FORAGING" ) == "flowers" ||
                         get_option<std::string>( "AUTO_FORAGING" ) == "trees" );
    if( harvest_common( p, examp, false, true, auto_forage ) ) {
        map &here = get_map();
        here.ter_set( examp, here.get_ter_transforms_into( examp ) );
    }
}

void iexamine::harvest_ter( player &p, const tripoint_bub_ms &examp )
{
    bool auto_forage = get_option<bool>( "AUTO_FEATURES" ) &&
                       ( get_option<std::string>( "AUTO_FORAGING" ) == "both" ||
                         get_option<std::string>( "AUTO_FORAGING" ) == "flowers" ||
                         get_option<std::string>( "AUTO_FORAGING" ) == "trees" );
    if( harvest_common( p, examp, false, false, auto_forage ) ) {
        map &here = get_map();
        here.ter_set( examp, here.get_ter_transforms_into( examp ) );
    }
}

void iexamine::harvested_plant( player &p, const tripoint_bub_ms &examp )
{
    p.add_msg_if_player( m_info, _( "Nothing can be harvested from this currently." ) );
    iexamine::none( p, examp );
}

void iexamine::flower_marloss( player &p, const tripoint_bub_ms &examp )
{
    if( season_of_year( calendar::turn ) == WINTER ) {
        add_msg( m_info, _( "This flower is still alive, despite the harsh conditions…" ) );
    }
    map &here = get_map();
    item &nectar = *item::spawn_temporary( "nectar" );
    if( can_drink_nectar( p, nectar ) ) {
        if( !query_yn( _( "You feel out of place as you explore the %s. Drink?" ),
                       here.furnname( examp ) ) ) {
            return;
        }
        p.moves -= to_moves<int>( 30_seconds ); // Takes 30 seconds
        add_msg( m_bad, _( "This flower tastes very wrong…" ) );
        // If you can drink flowers, you're post-thresh and the Mycus does not want you.
        p.add_effect( effect_teleglow, 10_minutes );
    }
    if( !query_yn( _( "Pick %s?" ), here.furnname( examp ) ) ) {
        none( p, examp );
        return;
    }
    here.furn_set( examp, f_null );
    here.spawn_item( p.bub_pos(), itype_marloss_seed, 1, 3, calendar::turn );
    handle_harvest( p, "withered", false );
}

void iexamine::egg_sack_generic( player &p, const tripoint_bub_ms &examp,
                                 const mtype_id &montype )
{
    map &here = get_map();
    const std::string old_furn_name = here.furnname( examp );
    if( !query_yn( _( "Harvest the %s?" ), old_furn_name ) ) {
        none( p, examp );
        return;
    }
    here.furn_set( examp, f_egg_sacke );
    int monster_count = 0;
    if( one_in( 2 ) ) {
        for( const tripoint_bub_ms &nearby_pos : closest_points_first( examp, 1 ) ) {
            if( !one_in( 3 ) ) {
                continue;
            } else if( g->place_critter_at( montype, nearby_pos ) ) {
                monster_count++;
            }
        }
    }
    int roll = rng( 4, 20 );
    bool drop_eggs = monster_count >= 1;
    for( int i = 0; i < roll; i++ ) {
        handle_harvest( p, "spider_egg", drop_eggs );
    }
    if( monster_count == 1 ) {
        add_msg( m_warning, _( "A spiderling bursts from the %s!" ), old_furn_name );
    } else if( monster_count >= 1 ) {
        add_msg( m_warning, _( "Spiderlings burst from the %s!" ), old_furn_name );
    }
}

void iexamine::egg_sackbw( player &p, const tripoint_bub_ms &examp )
{
    egg_sack_generic( p, examp, mon_spider_widow_giant_s );
}

void iexamine::egg_sackcs( player &p, const tripoint_bub_ms &examp )
{
    egg_sack_generic( p, examp, mon_spider_cellar_giant_s );
}

void iexamine::egg_sackws( player &p, const tripoint_bub_ms &examp )
{
    egg_sack_generic( p, examp, mon_spider_web_s );
}

void iexamine::fungus( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    add_msg( _( "The %s crumbles into spores!" ), here.furnname( examp ) );
    fungal_effects( *g, here ).create_spores( examp, &p );
    here.furn_set( examp, f_null );
    p.moves -= 50;
}

std::vector<seed_tuple> iexamine::get_seed_entries( const std::vector<item *> &seed_inv )
{
    std::map<itype_id, int> seed_map;
    for( auto &seed : seed_inv ) {
        seed_map[seed->typeId()] += ( seed->charges > 0 ? seed->charges : 1 );
    }

    std::vector<seed_tuple> seed_entries;
    seed_entries.reserve( seed_map.size() );
    for( const auto &pr : seed_map ) {
        seed_entries.emplace_back(
            pr.first, item::nname( pr.first, pr.second ), pr.second );
    }

    // Sort by name
    std::ranges::sort( seed_entries,
    []( const seed_tuple & l, const seed_tuple & r ) {
        return std::get<1>( l ).compare( std::get<1>( r ) ) < 0;
    } );

    return seed_entries;
}

int iexamine::query_seed( const std::vector<seed_tuple> &seed_entries, int min_req )
{
    uilist smenu;

    smenu.text = _( "Use which seed?" );
    int count = 0;
    for( const auto &entry : seed_entries ) {
        const std::string &seed_name = std::get<1>( entry );
        int seed_count = std::get<2>( entry );

        std::string format = seed_count > 0 ? "%s (%d)" : "%s";

        smenu.addentry( count++, seed_count >= min_req, MENU_AUTOASSIGN, format.c_str(),
                        seed_name, seed_count );
    }

    smenu.query();

    if( smenu.ret >= 0 ) {
        return smenu.ret;
    } else {
        return seed_entries.size();
    }
}

void iexamine::plant_seed( player &p, const tripoint_bub_ms &examp, const itype_id &seed_id )
{
    p.assign_activity( std::make_unique<player_activity>(
                           std::make_unique<plant_seed_activity_actor>( get_map().bub_to_abs( examp ),
                                   seed_id ) ) );
}

void iexamine::dirtmound( player &p, const tripoint_bub_ms &examp )
{

    if( !warm_enough_to_plant( g->u.abs_pos() ) ) {
        add_msg( m_info, _( "It is too cold to plant anything now." ) );
        return;
    }
    map &here = get_map();
    /* ambient_light_at() not working?
    if (here.ambient_light_at(examp) < LIGHT_AMBIENT_LOW) {
        add_msg(m_info, _("It is too dark to plant anything now."));
        return;
    }*/
    std::vector<item *> seed_inv = p.items_with( []( const item & itm ) {
        return itm.is_seed();
    } );
    if( seed_inv.empty() ) {
        add_msg( m_info, _( "You have no seeds to plant." ) );
        return;
    }
    if( !here.i_at( examp ).empty() ) {
        add_msg( _( "Something's lying there…" ) );
        return;
    }

    auto seed_entries = get_seed_entries( seed_inv );

    int seed_index = query_seed( seed_entries );

    // Did we cancel?
    if( seed_index < 0 || seed_index >= static_cast<int>( seed_entries.size() ) ) {
        add_msg( _( "You saved your seeds for later." ) );
        return;
    }
    const auto &seed_id = std::get<0>( seed_entries[seed_index] );

    // Separate temp check because for now we permit growing regular plants underground with artificial heating
    if( !seed_id.obj().has_flag( flag_CAN_PLANT_UNDERGROUND ) && examp.z() < 0 &&
        get_weather().get_temperature( bub_to_abs( examp ) ) < 10_c ) {
        add_msg( _( "It's too cold down here to plant this type of seed underground." ) );
        return;
    }

    if( !here.has_flag_ter_or_furn( seed_id->seed->required_terrain_flag, examp ) ) {
        add_msg( _( "This type of seed can not be planted in this location." ) );
        return;
    }

    plant_seed( p, examp, seed_id );
}

std::vector<detached_ptr<item>> iexamine::get_harvest_items( const itype &type,
        const int plant_count,
        const int seed_count, const bool byproducts )
{
    std::vector<detached_ptr<item>> result;
    if( !type.seed ) {
        return result;
    }
    const islot_seed &seed_data = *type.seed;
    // This is a temporary measure, itype should instead provide appropriate accessors
    // to expose data about the seed item to allow harvesting to function.
    const itype_id &seed_type = type.get_id();

    const auto add = [&]( const itype_id & id, int count ) {

        //TODO!:check
        detached_ptr<item> new_item = item::spawn( id, calendar::turn );
        if( new_item->count_by_charges() && count > 0 ) {
            new_item->charges *= count;
            new_item->charges /= seed_data.fruit_div;
            if( new_item->charges <= 0 ) {
                new_item->charges = 1;
            }
            result.push_back( std::move( new_item ) );
        } else {
            while( count > 0 ) {
                result.push_back( item::spawn( id, calendar::turn ) );
                count--;
            }
        }
    };

    if( seed_data.spawn_seeds ) {
        add( seed_type, seed_count );
    }

    add( seed_data.fruit_id, plant_count );

    if( byproducts ) {
        for( auto &b : seed_data.byproducts ) {
            add( b, 1 );
        }
    }

    return result;
}

void iexamine::harvest_plant( player &p, const tripoint_bub_ms &examp, bool from_activity )
{
    map &here = get_map();
    // Can't use item_stack::only_item() since there might be fertilizer
    map_stack items = here.i_at( examp );
    map_stack::iterator seed_it = std::ranges::find_if( items,
    []( const item * const & it ) {
        return it->is_seed();
    } );

    if( seed_it == items.end() ) {
        debugmsg( "Missing seed for plant at (%d, %d, %d)", examp.x(), examp.y(), examp.z() );
        here.i_clear( examp );
        here.furn_set( examp, f_null );
        return;
    }
    item *&seed = *seed_it;
    seed_it = map_stack::iterator();
    const itype_id &seedType = seed->typeId();
    if( seedType == itype_fungal_seeds ) {
        fungus( p, examp );
        here.i_clear( examp );
    } else if( seedType == itype_marloss_seed ) {
        fungus( p, examp );
        here.i_clear( examp );
        if( p.has_trait( trait_M_DEPENDENT ) && ( p.get_kcal_percent() < 0.8f ||
                p.get_thirst() > thirst_levels::very_thirsty ) ) {
            here.ter_set( examp, t_marloss );
            add_msg( m_info,
                     _( "We have altered this unit's configuration to extract and provide local nutriment.  The Mycus provides." ) );
        } else if( ( p.has_trait( trait_M_DEFENDER ) ) || ( ( p.has_trait( trait_M_SPORES ) ||
                   p.has_trait( trait_M_FERTILE ) ) &&
                   one_in( 2 ) ) ) {
            g->place_critter_at( mon_fungal_blossom, examp );
            add_msg( m_info, _( "The seed blooms forth!  We have brought true beauty to this world." ) );
        } else if( ( p.has_trait( trait_THRESH_MYCUS ) ) || one_in( 4 ) ) {
            here.furn_set( examp, f_flower_marloss );
            add_msg( m_info, _( "The seed blossoms rather rapidly…" ) );
        } else {
            here.furn_set( examp, f_flower_fungal );
            add_msg( m_info, _( "The seed blossoms into a flower-looking fungus." ) );
        }
    } else { // Generic seed, use the seed item data
        const itype &type = *seed->type;
        here.i_clear( examp );

        int skillLevel = p.get_skill_level( skill_survival );
        ///\EFFECT_SURVIVAL increases number of plants harvested from a seed
        int plant_count = rng( skillLevel / 2, skillLevel );
        plant_count *= here.furn( examp )->plant->harvest_multiplier;
        const int max_harvest_count = get_option<int>( "MAX_HARVEST_COUNT" );
        if( plant_count >= max_harvest_count ) {
            plant_count = max_harvest_count;
        } else if( plant_count <= 0 ) {
            plant_count = 1;
        }
        const int seedCount = std::max( 1, rng( plant_count / 4, plant_count / 2 ) );
        for( auto &i : get_harvest_items( type, plant_count, seedCount, true ) ) {
            if( from_activity ) {
                i->set_var( "activity_var", p.name );
            }
            here.add_item_or_charges( examp, std::move( i ) );
        }
        here.furn_set( examp, furn_str_id( here.furn( examp )->plant->transform ) );
        p.moves -= to_moves<int>( 10_seconds );
    }
}

ret_val<bool> iexamine::can_fertilize( player &p, const tripoint_bub_ms &tile,
                                       const itype_id &fertilizer )
{
    map &here = get_map();
    if( !here.has_flag_furn( "PLANT", tile ) ) {
        return ret_val<bool>::make_failure( _( "Tile isn't a plant" ) );
    }
    if( here.i_at( tile ).size() > 1 ) {
        return ret_val<bool>::make_failure( _( "Tile is already fertilized" ) );
    }
    if( !p.has_charges( fertilizer, 1 ) ) {
        return ret_val<bool>::make_failure(
                   _( "Tried to fertilize with %s, but player doesn't have any." ),
                   fertilizer.c_str() );
    }

    return ret_val<bool>::make_success();
}

void iexamine::fertilize_plant( player &p, const tripoint_bub_ms &tile, const itype_id &fertilizer )
{
    ret_val<bool> can_fert = can_fertilize( p, tile, fertilizer );
    if( !can_fert.success() ) {
        debugmsg( can_fert.str() );
        return;
    }

    map &here = get_map();
    // The plant furniture has the NOITEM token which prevents adding items on that square,
    // spawned items are moved to an adjacent field instead, but the fertilizer token
    // must be on the square of the plant, therefore this hack:
    const auto old_furn = here.furn( tile );
    here.furn_set( tile, f_null );
    here.spawn_item( tile, itype_fertilizer, 1, 1, calendar::turn );
    here.furn_set( tile, old_furn );
    std::vector<detached_ptr<item>> planted = p.use_charges( fertilizer, 1 );

    // Can't use item_stack::only_item() since there might be fertilizer
    map_stack items = here.i_at( tile );
    map_stack::iterator seed_it = std::ranges::find_if( items,
    []( const item * const & it ) {
        return it->is_seed();
    } );
    if( seed_it == items.end() ) {
        debugmsg( "Missing seed for plant at (%d, %d, %d)", tile.x(), tile.y(), tile.z() );
        here.i_clear( tile );
        here.furn_set( tile, f_null );
        return;
    }

    item *&seed = *seed_it;
    seed_it = map_stack::iterator();
    // Reduce the amount of time it takes until the next stage of growth
    // by 60% of the seed's stage duration, or 20% of its overall growth time
    const time_duration fertilized_boost = seed->get_plant_epoch() * 0.6;
    seed->set_birthday( seed->birthday() - fertilized_boost );
    p.mod_moves( -to_moves<int>( 10_seconds ) );
    //~ %1$s: plant name, %2$s: fertilizer name
    add_msg( m_info, _( "You fertilize the %1$s with the %2$s." ), seed->get_plant_name(),
             planted.front()->tname() );
}

itype_id iexamine::choose_fertilizer( player &p, const std::string &pname, bool ask_player )
{
    std::vector<item *> f_inv = p.all_items_with_flag( flag_FERTILIZER );
    if( f_inv.empty() ) {
        add_msg( m_info, _( "You have no fertilizer for the %s." ), pname );
        return itype_id();
    }

    std::vector<itype_id> f_types;
    std::vector<std::string> f_names;
    for( auto &f : f_inv ) {
        if( !std::ranges::contains( f_types, f->typeId() ) ) {
            f_types.push_back( f->typeId() );
            f_names.push_back( f->tname() );
        }
    }

    if( ask_player && !query_yn( _( "Fertilize the %s?" ), pname ) ) {
        return itype_id();
    }

    // Choose fertilizer from list
    int f_index = 0;
    if( f_types.size() > 1 ) {
        f_index = uilist( _( "Use which fertilizer?" ), f_names );
    }
    if( f_index < 0 ) {
        return itype_id();
    }

    return f_types[f_index];

}

void iexamine::aggie_plant( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    // Can't use item_stack::only_item() since there might be fertilizer
    map_stack items = here.i_at( examp );
    map_stack::iterator seed_it = std::ranges::find_if( items,
    []( const item * const & it ) {
        return it->is_seed();
    } );

    if( seed_it == items.end() ) {
        debugmsg( "Missing seed for plant at (%d, %d, %d)", examp.x(), examp.y(), examp.z() );
        here.i_clear( examp );
        here.furn_set( examp, f_null );
        return;
    }

    const item *const &seed = *seed_it;
    seed_it = map_stack::iterator();

    const std::string pname = seed->get_plant_name();

    if( here.has_flag_furn( flag_GROWTH_HARVEST, examp ) &&
        query_yn( _( "Harvest the %s?" ), pname ) ) {
        harvest_plant( p, examp );
    } else if( !here.has_flag_furn( flag_GROWTH_HARVEST, examp ) ) {
        if( here.i_at( examp ).size() > 1 ) {
            add_msg( m_info, _( "This %s has already been fertilized." ), pname );
            return;
        }
        itype_id fertilizer = choose_fertilizer( p, pname, true /*ask player for confirmation */ );

        if( !fertilizer.is_empty() ) {
            fertilize_plant( p, examp, fertilizer );
        }
    }
}

static void pick_plant( player &p, const tripoint_bub_ms &examp,
                        const itype_id &itemType, ter_id new_ter, bool seeds = false )
{
    map &here = get_map();
    bool auto_forage = get_option<bool>( "AUTO_FEATURES" ) &&
                       get_option<std::string>( "AUTO_FORAGING" ) != "off";
    if( p.is_player() && !auto_forage &&
        !query_yn( _( "Harvest the %s?" ), here.tername( examp ) ) ) {
        iexamine::none( p, examp );
        return;
    }

    const int survival = p.get_skill_level( skill_survival );
    p.practice( skill_survival, 6 );

    int plantBase = rng( 2, 5 );
    ///\EFFECT_SURVIVAL increases number of plants harvested
    int plantCount = rng( plantBase, plantBase + survival / 2 );
    plantCount = std::min( plantCount, 12 );

    here.spawn_item( p.bub_pos(), itemType, plantCount, 0, calendar::turn );

    if( seeds ) {
        // FIXME: shouldn't derive seed type by string manipulation
        here.spawn_item( p.bub_pos(), itype_id( "seed_" + itemType.str() ), 1,
                         rng( plantCount / 4, plantCount / 2 ), calendar::turn );
    }

    here.ter_set( examp, new_ter );
}

void iexamine::tree_hickory( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    if( harvest_common( p, examp, false, false ) ) {
        here.ter_set( examp, here.get_ter_transforms_into( examp ) );
    }
    if( !p.has_quality( qual_DIG ) ) {
        p.add_msg_if_player( m_info, _( "You have no tool to dig with…" ) );
        return;
    }
    if( p.is_player() &&
        !query_yn( _( "Dig up %s?  This kills the tree!" ), here.tername( examp ) ) ) {
        return;
    }

    ///\EFFECT_SURVIVAL increases hickory root number per tree
    here.spawn_item( p.bub_pos(), itype_hickory_root, rng( 1, 3 + p.get_skill_level( skill_survival ) ),
                     0,
                     calendar::turn );
    here.ter_set( examp, t_tree_hickory_dead );
    ///\EFFECT_SURVIVAL speeds up hickory root digging
    p.moves -= to_moves<int>( 20_seconds ) / ( p.get_skill_level( skill_survival ) + 1 ) + 100;
}

static item *maple_tree_sap_container()
{
    const item &maple_sap = *item::spawn_temporary( itype_maple_sap, calendar::start_of_cataclysm );
    return g->inv_map_splice( [&]( const item & it ) {
        return it.get_remaining_capacity_for_liquid( maple_sap, true ) > 0;
    }, _( "Which container?" ), PICKUP_RANGE );
}

void iexamine::tree_maple( player &p, const tripoint_bub_ms &examp )
{
    if( !p.has_quality( quality_id( "DRILL" ) ) ) {
        add_msg( m_info, _( "You need a tool to drill the crust to tap this maple tree." ) );
        return;
    }

    if( !p.has_quality( quality_id( "HAMMER" ) ) ) {
        add_msg( m_info,
                 _( "You need a tool to hammer the spile into the crust to tap this maple tree." ) );
        return;
    }

    const inventory &crafting_inv = p.crafting_inventory();

    if( !crafting_inv.has_amount( itype_tree_spile, 1 ) ) {
        add_msg( m_info, _( "You need a %s to tap this maple tree." ),
                 item::nname( itype_tree_spile ) );
        return;
    }

    std::vector<item_comp> comps;
    comps.emplace_back( itype_tree_spile, 1 );
    p.consume_items( comps, 1, is_crafting_component );

    p.mod_moves( -to_moves<int>( 20_seconds ) );
    map &here = get_map();
    here.ter_set( examp, t_tree_maple_tapped );

    item *container = maple_tree_sap_container();
    if( container ) {
        here.add_item_or_charges( examp, container->detach(), false );
    } else {
        add_msg( m_info, _( "No container added.  The sap will just spill on the ground." ) );
    }
}

void iexamine::tree_maple_tapped( player &p, const tripoint_bub_ms &examp )
{
    bool has_sap = false;
    item *container = nullptr;
    int charges = 0;

    const std::string maple_sap_name = item::nname( itype_maple_sap );

    map &here = get_map();
    map_stack items = here.i_at( examp );
    if( !items.empty() ) {
        item &it = items.only_item();
        if( it.is_bucket() || it.is_watertight_container() ) {
            container = &it;

            if( !it.is_container_empty() && it.contents.front().typeId() == itype_maple_sap ) {
                has_sap = true;
                charges = it.contents.front().charges;
            }
        }
    }

    enum options {
        REMOVE_TAP,
        ADD_CONTAINER,
        HARVEST_SAP,
        REMOVE_CONTAINER,
    };
    uilist selectmenu;
    selectmenu.addentry( REMOVE_TAP, true, MENU_AUTOASSIGN, _( "Remove tap" ) );
    selectmenu.addentry( ADD_CONTAINER, !container, MENU_AUTOASSIGN,
                         _( "Add a container to receive the %s" ), maple_sap_name );
    selectmenu.addentry( HARVEST_SAP, has_sap, MENU_AUTOASSIGN, _( "Harvest current %s (%d)" ),
                         maple_sap_name, charges );
    selectmenu.addentry( REMOVE_CONTAINER, container, MENU_AUTOASSIGN, _( "Remove container" ) );

    selectmenu.text = _( "Select an action" );
    selectmenu.query();

    switch( selectmenu.ret ) {
        case REMOVE_TAP: {
            if( !p.has_quality( quality_id( "HAMMER" ) ) ) {
                add_msg( m_info, _( "You need a hammering tool to remove the spile from the crust." ) );
                return;
            }

            detached_ptr<item> tree_spile = item::spawn( "tree_spile" );
            add_msg( _( "You remove the %s." ), tree_spile->tname( 1 ) );
            here.add_item_or_charges( p.bub_pos(), std::move( tree_spile ) );

            if( container ) {
                here.add_item_or_charges( p.bub_pos(), container->detach() );
            }
            here.i_clear( examp );

            p.mod_moves( -to_moves<int>( 20_seconds ) );
            here.ter_set( examp, t_tree_maple );

            return;
        }

        case ADD_CONTAINER: {
            container = maple_tree_sap_container();
            if( container ) {
                here.add_item_or_charges( examp, container->detach(), false );

            } else {
                add_msg( m_info, _( "No container added.  The sap will just spill on the ground." ) );
            }

            return;
        }

        case HARVEST_SAP: {
            liquid_handler::handle_liquid( container->contents.front(), PICKUP_RANGE );
            return;
        }

        case REMOVE_CONTAINER: {
            g->u.assign_activity( std::make_unique<player_activity>( std::make_unique<pickup_activity_actor>(
            std::vector<pickup::pick_drop_selection> { { container, std::nullopt, {} } }, g->u.bub_pos() ) ) );
            return;
        }

        default:
            return;
    }
}

void iexamine::shrub_marloss( player &p, const tripoint_bub_ms &examp )
{
    if( p.has_trait( trait_THRESH_MYCUS ) ) {
        pick_plant( p, examp, itype_mycus_fruit, t_shrub_fungal );
    } else if( p.has_trait( trait_THRESH_MARLOSS ) ) {
        map &here = get_map();
        here.spawn_item( p.bub_pos(), itype_mycus_fruit, 1, 0, calendar::turn );
        here.ter_set( examp, t_fungus );
        add_msg( m_info, _( "The shrub offers up a fruit, then crumbles into a fungal bed." ) );
    } else {
        pick_plant( p, examp, itype_marloss_berry, t_shrub_fungal );
    }
}

void iexamine::tree_marloss( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    if( p.has_trait( trait_THRESH_MYCUS ) ) {
        pick_plant( p, examp, itype_mycus_fruit, t_tree_fungal );
        if( p.has_trait( trait_M_DEPENDENT ) && one_in( 3 ) ) {
            // Folks have a better shot at keeping fed.
            add_msg( m_info,
                     _( "We have located a particularly vital nutrient deposit underneath this location." ) );
            add_msg( m_good, _( "Additional nourishment is available." ) );
            here.ter_set( examp, t_marloss_tree );
        }
    } else if( p.has_trait( trait_THRESH_MARLOSS ) ) {
        here.spawn_item( p.bub_pos(), itype_mycus_fruit, 1, 0, calendar::turn );
        here.ter_set( examp, t_tree_fungal );
        add_msg( m_info, _( "The tree offers up a fruit, then shrivels into a fungal tree." ) );
    } else {
        pick_plant( p, examp, itype_marloss_berry, t_tree_fungal );
    }
}

void iexamine::shrub_wildveggies( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    // Ask if there's something possibly more interesting than this shrub here
    if( ( !here.i_at( examp ).empty() ||
          here.veh_at( examp ) ||
          !here.tr_at( examp ).is_null() ||
          g->critter_at( examp ) != nullptr ) &&
        !query_yn( _( "Forage through %s?" ), here.tername( examp ) ) ) {
        none( p, examp );
        return;
    }

    add_msg( _( "You forage through the %s." ), here.tername( examp ) );
    ///\EFFECT_SURVIVAL speeds up foraging
    int move_cost = 100000 / ( 2 * p.get_skill_level( skill_survival ) + 5 );
    ///\EFFECT_PER randomly speeds up foraging
    move_cost /= rng( std::max( 4, p.per_cur ), 4 + p.per_cur * 2 );
    p.assign_activity( std::make_unique<player_activity>(
                           std::make_unique<forage_activity_actor>( here.bub_to_abs( examp ),
                                   true ) ) );
    return;
}

