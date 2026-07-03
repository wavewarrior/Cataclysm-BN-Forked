#include "activity_handlers.h"
#include "activity_actor_definitions.h"

#include "action.h"
#include "advanced_inv.h"
#include "armor_layers.h"
#include "avatar.h"
#include "avatar_action.h"
#include "bionics.h"
#include "bodypart.h"
#include "calendar.h"
#include "character.h"
#include "character_functions.h"
#include "character_martial_arts.h"
#include "clzones.h"
#include "color.h"
#include "construction.h"
#include "construction_partial.h"
#include "coordinates.h"
#include "craft_command.h"
#include "crafting.h"
#include "crafting_quality.h"
#include "creature.h"
#include "damage.h"
#include "debug.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <memory>
#include <optional>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#include <weighted_list.h>
// TODO (https://github.com/cataclysmbn/Cataclysm-BN/issues/1612):
// Remove that include after implementing repair_activity_actor.
#include "distribution_grid.h"
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "fault.h"
#include "field_type.h"
#include "flag.h"
#include "fstream_utils.h"
#include "game.h"
#include "game_constants.h"
#include "game_inventory.h"
#include "handle_liquid.h"
#include "harvest.h"
#include "iexamine.h"
#include "int_id.h"
#include "inventory.h"
#include "item.h"
#include "item_contents.h"
#include "item_group.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "line.h"
#include "magic.h"
#include "map.h"
#include "map_iterator.h"
#include "mapdata.h"
#include "martialarts.h"
#include "messages.h"
#include "mongroup.h"
#include "monster.h"
#include "morale_types.h"
#include "mtype.h"
#include "npc.h"
#include "omdata.h"
#include "output.h"
#include "overmapbuffer.h"
#include "player.h"
#include "player_activity.h"
#include "point.h"
#include "ranged.h"
#include "recipe.h"
#include "requirements.h"
#include "ret_val.h"
#include "rng.h"
#include "skill.h"
#include "sounds.h"
#include "string_formatter.h"
#include "string_id.h"
#include "text_snippets.h"
#include "translations.h"
#include "type_id.h"
#include "ui.h"
#include "veh_interact.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vpart_position.h"

enum creature_size : int;

static const activity_id ACT_ADV_INVENTORY( "ACT_ADV_INVENTORY" );
static const activity_id ACT_ANIMALS( "ACT_ANIMALS" );
static const activity_id ACT_ATM( "ACT_ATM" );
static const activity_id ACT_BLEED( "ACT_BLEED" );
static const activity_id ACT_BUTCHER( "ACT_BUTCHER" );
static const activity_id ACT_BUTCHER_FULL( "ACT_BUTCHER_FULL" );
static const activity_id ACT_CRACKING( "ACT_CRACKING" );
static const activity_id ACT_CRAFT( "ACT_CRAFT" );
static constexpr auto craft_is_long_idx = 0;
static constexpr auto craft_bench_type_idx = 1;
static constexpr auto craft_tools_mult_percent_idx = 2;
static constexpr auto craft_tools_mult_next_refresh_idx = 3;
static const activity_id ACT_DISMEMBER( "ACT_DISMEMBER" );
static const activity_id ACT_DISSECT( "ACT_DISSECT" );
static const activity_id ACT_FETCH_REQUIRED( "ACT_FETCH_REQUIRED" );
static const activity_id ACT_FIELD_DRESS( "ACT_FIELD_DRESS" );
static const activity_id ACT_MILK( "ACT_MILK" );
static const activity_id ACT_FIND_MOUNT( "ACT_FIND_MOUNT" );
static const activity_id ACT_FISH( "ACT_FISH" );
static const activity_id ACT_GAME( "ACT_GAME" );
static const activity_id ACT_GENERIC_GAME( "ACT_GENERIC_GAME" );
static const activity_id ACT_HAIRCUT( "ACT_HAIRCUT" );
static const activity_id ACT_HOTWIRE_CAR( "ACT_HOTWIRE_CAR" );
static const activity_id ACT_MAKE_ZLAVE( "ACT_MAKE_ZLAVE" );
static const activity_id ACT_MEDITATE( "ACT_MEDITATE" );
static const activity_id ACT_MIND_SPLICER( "ACT_MIND_SPLICER" );
static const activity_id ACT_MOVE_LOOT( "ACT_MOVE_LOOT" );
static const activity_id ACT_MULTIPLE_BUTCHER( "ACT_MULTIPLE_BUTCHER" );
static const activity_id ACT_MULTIPLE_CHOP_PLANKS( "ACT_MULTIPLE_CHOP_PLANKS" );
static const activity_id ACT_MULTIPLE_CHOP_TREES( "ACT_MULTIPLE_CHOP_TREES" );
static const activity_id ACT_MULTIPLE_CONSTRUCTION( "ACT_MULTIPLE_CONSTRUCTION" );
static const activity_id ACT_MULTIPLE_MINE( "ACT_MULTIPLE_MINE" );
static const activity_id ACT_MULTIPLE_FARM( "ACT_MULTIPLE_FARM" );
static const activity_id ACT_MULTIPLE_FISH( "ACT_MULTIPLE_FISH" );
static const activity_id ACT_PLAY_WITH_PET( "ACT_PLAY_WITH_PET" );
static const activity_id ACT_TRAIN_PET( "ACT_TRAIN_PET" );
static const activity_id ACT_PULP( "ACT_PULP" );
static const activity_id ACT_QUARTER( "ACT_QUARTER" );
static const activity_id ACT_READ( "ACT_READ" );
static const activity_id ACT_REPAIR_ITEM( "ACT_REPAIR_ITEM" );
static const activity_id ACT_ROBOT_CONTROL( "ACT_ROBOT_CONTROL" );
static const activity_id ACT_SHAVE( "ACT_SHAVE" );
static const activity_id ACT_SKIN( "ACT_SKIN" );
static const activity_id ACT_SOCIALIZE( "ACT_SOCIALIZE" );
static const activity_id ACT_SPELLCASTING( "ACT_SPELLCASTING" );
static const activity_id ACT_START_ENGINES( "ACT_START_ENGINES" );
static const activity_id ACT_START_FIRE( "ACT_START_FIRE" );
static const activity_id ACT_STUDY_SPELL( "ACT_STUDY_SPELL" );
static const activity_id ACT_TIDY_UP( "ACT_TIDY_UP" );
static const activity_id ACT_TRAIN( "ACT_TRAIN" );
static const activity_id ACT_TRAVELLING( "ACT_TRAVELLING" );
static const activity_id ACT_TREE_COMMUNION( "ACT_TREE_COMMUNION" );
static const activity_id ACT_TRY_SLEEP( "ACT_TRY_SLEEP" );
static const activity_id ACT_VEHICLE( "ACT_VEHICLE" );
static const activity_id ACT_VEHICLE_DECONSTRUCTION( "ACT_VEHICLE_DECONSTRUCTION" );
static const activity_id ACT_VEHICLE_REPAIR( "ACT_VEHICLE_REPAIR" );
static const activity_id ACT_VIBE( "ACT_VIBE" );
static const activity_id ACT_WAIT( "ACT_WAIT" );
static const activity_id ACT_WAIT_NPC( "ACT_WAIT_NPC" );
static const activity_id ACT_WAIT_STAMINA( "ACT_WAIT_STAMINA" );
static const activity_id ACT_WAIT_WEATHER( "ACT_WAIT_WEATHER" );

static const efftype_id effect_ai_waiting( "ai_waiting" );
static const efftype_id effect_bleed( "bleed" );
static const efftype_id effect_blind( "blind" );
static const efftype_id effect_narcosis( "narcosis" );
static const efftype_id effect_pet( "pet" );
static const efftype_id effect_sheared( "sheared" );
static const efftype_id effect_sleep( "sleep" );
static const efftype_id effect_tied( "tied" );
static const efftype_id effect_under_op( "under_operation" );
static const efftype_id effect_well_fed( "well_fed" );

static const fault_id fault_bionic_nonsterile( "fault_bionic_nonsterile" );

static const itype_id itype_2x4( "2x4" );
static const itype_id itype_animal( "animal" );
static const itype_id itype_battery( "battery" );
static const itype_id itype_burnt_out_bionic( "burnt_out_bionic" );
static const itype_id itype_grapnel( "grapnel" );
static const itype_id itype_hd_tow_cable( "hd_tow_cable" );
static const itype_id itype_log( "log" );
static const itype_id itype_mind_scan_robofac( "mind_scan_robofac" );
static const itype_id itype_muscle( "muscle" );
static const itype_id itype_nail( "nail" );
static const itype_id itype_rope_30( "rope_30" );
static const itype_id itype_rope_makeshift_30( "rope_makeshift_30" );
static const itype_id itype_splinter( "splinter" );
static const itype_id itype_stick_long( "stick_long" );
static const itype_id itype_vine_30( "vine_30" );
static const itype_id itype_wool_staple( "wool_staple" );

static const zone_type_id zone_type_FARM_PLOT( "FARM_PLOT" );

static const skill_id skill_computer( "computer" );
static const skill_id skill_electronics( "electronics" );
static const skill_id skill_fabrication( "fabrication" );
static const skill_id skill_firstaid( "firstaid" );
static const skill_id skill_mechanics( "mechanics" );
static const skill_id skill_survival( "survival" );

static const quality_id qual_BUTCHER( "BUTCHER" );
static const quality_id qual_CUT_FINE( "CUT_FINE" );

static const species_id HUMAN( "HUMAN" );
static const species_id ZOMBIE( "ZOMBIE" );

static const trait_flag_str_id trait_flag_CANNIBAL( "CANNIBAL" );
static const trait_flag_str_id trait_flag_PSYCHOPATH( "PSYCHOPATH" );
static const trait_flag_str_id trait_flag_SAPIOVORE( "SAPIOVORE" );

static const bionic_id bio_painkiller( "bio_painkiller" );

static const itype_id itype_UPS( "UPS" );

static const trait_id trait_NOPAIN( "NOPAIN" );
static const trait_id trait_SPIRITUAL( "SPIRITUAL" );
static const trait_id trait_STOCKY_TROGLO( "STOCKY_TROGLO" );

// not to confuse with item flags (json_flag)
static const std::string flag_AUTODOC( "AUTODOC" );
static const std::string flag_AUTODOC_COUCH( "AUTODOC_COUCH" );
static const std::string flag_BUTCHER_EQ( "BUTCHER_EQ" );
static const std::string flag_TREE( "TREE" );

using namespace activity_handlers;


bool activity_handlers::resume_for_multi_activities( player& p )
{
    if( !p.backlog.empty() ) {
        activity_ptr& back_act = p.backlog.front();
        if( back_act->is_multi_type() ) {
            p.assign_activity( std::make_unique<player_activity>(
                                   std::make_unique<generic_multi_activity_actor>( back_act->id() ) ) );
            p.backlog.clear();
            return true;
        }
    }
    return false;
}

static bool check_butcher_cbm( const int roll )
{
    // Success rate for dissection rolls, simple percentage roll
    // +10% per fine cutting quality, +10% per 2 levels of first aid and electronics
    // Additional, small randomized bonus/penalty if dexterity is above/below average
    // Roll is reduced by corpse damage level (up to -4), minimum of 10% success chance
    add_msg( m_debug, _( "Roll = %i" ), roll );
    add_msg( m_debug, _( "Success chance = %i%%" ), std::min( 100, ( roll * 10 ) ) );
    const bool success = x_in_y( roll, 10 );
    return success;
}

void extract_or_wreck_cbms( std::vector<detached_ptr<item>> &cbms, int roll, player& p )
{
    if( roll < 0 ) { return; }
    for( detached_ptr<item> &it : cbms ) {
        // For some stupid reason, zombie pheromones are dropped using bionic type
        // This complicates things
        if( it->is_bionic() ) {
            if( check_butcher_cbm( roll ) || it->typeId() == itype_burnt_out_bionic ) {
                if( it->has_flag( flag_BIONIC_FAULTY ) ) {
                    it->convert( itype_burnt_out_bionic );
                    // We don't need the non-sterile fault on a piece of burnt-out bionic
                    if( it->has_fault( fault_bionic_nonsterile ) ) {
                        it->faults.erase( fault_bionic_nonsterile );
                    }
                }
                add_msg( m_good, _( "You discover: %s!" ), it->tname() );
            } else {
                it->convert( itype_burnt_out_bionic );
                if( it->has_fault( fault_bionic_nonsterile ) ) {
                    it->faults.erase( fault_bionic_nonsterile );
                }
                add_msg( m_bad, _( "Your imprecise surgery damaged a bionic, producing a %s." ),
                         it->tname() );
            }
        } else {
            if( !check_butcher_cbm( roll ) ) {
                add_msg( m_bad, _( "Your imprecise surgery destroyed something." ) );
                continue;
            } else {
                // If we have non-bionic loot in a harvest's bionic_group it doesn't need to be
                // marked non-sterile either.
                if( it->has_fault( fault_bionic_nonsterile ) ) {
                    it->faults.erase( fault_bionic_nonsterile );
                }
                add_msg( m_good, _( "You discover: %s!" ), it->tname() );
            }
        }

        if( it->type->phase == LIQUID ) {
            // TODO: smarter NPC liquid handling
            if( p.is_npc() ) {
                drop_on_map( p, item_drop_reason::deliberate, {std::move( it )}, p.bub_pos() );
            } else {
                liquid_handler::handle_all_liquid( std::move( it ), PICKUP_RANGE );
            }
        } else {
            get_map().add_item( p.bub_pos(), std::move( it ) );
        }
    }
}

// TODO: Implement
enum class butcherable_rating : int {
    butcherable = 0,
    no_tool,
    no_tree_rope_rack,
    no_table,
    no_saw,
    too_damaged,
    already_dressed,
    already_skinned,
    already_bled,
    already_quartered,
    too_small,
    needs_dressing,
    warn_cannibalism,
    info_tools
};

butchery_setup consider_butchery( const item& corpse_item, player& u, butcher_type action )
{
    map& here = get_map();
    butchery_setup setup;
    setup.can_do = butchery_possibility::yes;
    setup.type = action;
    const auto wont_do = [&setup]( const std::string & msg, butcherable_rating ) {
        setup.problems.emplace_back( msg );
        setup.can_do = butchery_possibility::never;
    };
    const auto not_this_one = [&setup]( const std::string & msg, butcherable_rating ) {
        setup.problems.emplace_back( msg );
        if( setup.can_do != butchery_possibility::never ) {
            setup.can_do = butchery_possibility::not_this;
        }
    };
    const auto info = [&setup]( const std::string & msg, butcherable_rating ) {
        setup.info.emplace_back( msg );
    };
    const auto need_confirm = [&setup]( const std::string & msg, butcherable_rating ) {
        setup.problems.emplace_back( msg );
        if( setup.can_do == butchery_possibility::yes ) {
            setup.can_do = butchery_possibility::need_confirmation;
        }
    };

    const inventory& inv = u.crafting_inventory();
    const int factor = inv.max_quality( action == DISSECT ? qual_CUT_FINE : qual_BUTCHER );

    const mtype& corpse = *corpse_item.get_mtype();

    if( action != DISSECT && factor == INT_MIN ) {
        wont_do( _( "None of your cutting tools are suitable for butchering." ),
                 butcherable_rating::no_tool );
    }

    if( action == DISSECT ) {
        switch( factor ) {
            case INT_MIN:
                wont_do( _( "None of your tools are sharp and precise enough to do that." ),
                         butcherable_rating::no_tool );
                break;
            case 1:
                info( _( "You could use a better tool, but this will do." ),
                      butcherable_rating::info_tools );
                break;
            case 2:
                info( _( "This tool is great, but you still would like a scalpel." ),
                      butcherable_rating::info_tools );
                break;
            case 3:
                info( _( "You dissect the corpse with a trusty scalpel." ),
                      butcherable_rating::info_tools );
                break;
            case 5:
                info( _( "You dissect the corpse with a sophisticated system of surgical grade "
                         "scalpels." ),
                      butcherable_rating::info_tools );
                break;
        }
    }

    bool has_tree_nearby = false;
    for( const auto& pt : here.points_in_radius( u.bub_pos(), PICKUP_RANGE ) ) {
        if( here.has_flag( flag_TREE, pt ) ) {
            has_tree_nearby = true;
            break;
        }
    }
    bool b_rack_present = false;
    for( const auto& pt : here.points_in_radius( u.bub_pos(), PICKUP_RANGE ) ) {
        if( here.has_flag_furn( flag_BUTCHER_EQ, pt ) ) {
            b_rack_present = true;
            break;
        }
        // vehicle part
        const optional_vpart_position vp = here.veh_at( pt );
        if( !vp ) { continue; }
        vp->vehicle();
        if( vp.part_with_feature( "BUTCHER_EQ", true ) ) {
            b_rack_present = true;
            break;
        }
    }
    if( !b_rack_present ) {
        b_rack_present = inv.has_item_with( []( const item & it ) {
            return it.has_flag( flag_BUTCHER_RACK );
        } );
    }
    // workshop butchery (full) prequisites
    if( action == BUTCHER_FULL ) {
        const bool has_rope =
            inv.has_amount( itype_rope_30, 1 ) || inv.has_amount( itype_rope_makeshift_30, 1 )
            || inv.has_amount( itype_hd_tow_cable, 1 ) || inv.has_amount( itype_vine_30, 1 )
            || inv.has_amount( itype_grapnel, 1 );
        const bool big_corpse = corpse.size >= creature_size::medium;

        if( big_corpse ) {
            if( has_rope && !has_tree_nearby && !b_rack_present ) {
                not_this_one(
                    _( "You need to suspend this corpse to butcher it.  While you have a "
                       "rope to lift the corpse, there is no tree nearby to hang it from." ),
                    butcherable_rating::no_tree_rope_rack );
            }
            if( !has_rope && !b_rack_present ) {
                not_this_one(
                    _( "To perform a full butchery on a corpse this big, you need either a "
                       "butchering rack, a nearby hanging meathook, or both a long rope in "
                       "your inventory and a nearby tree to hang the corpse from." ),
                    butcherable_rating::no_tree_rope_rack );
            }
            if( !( here.has_nearby_table( u.bub_pos(), PICKUP_RANGE )
            || inv.has_item_with( []( const item & it ) {
            return it.has_flag( flag_FLAT_SURFACE );
            } ) ) ) {
                not_this_one(
                    _( "To perform a full butchery on a corpse this big, you need a table "
                       "nearby or something else with a flat surface.  A leather tarp "
                       "spread out on the ground could suffice." ),
                    butcherable_rating::no_table );
            }
        }
    }

    if( action == DISSECT
        && ( corpse_item.has_flag( flag_QUARTERED )
             || corpse_item.has_flag( flag_FIELD_DRESS_FAILED ) ) ) {
        not_this_one(
            _( "It would be futile to search for implants inside this badly damaged "
               "corpse." ),
            butcherable_rating::too_damaged );
    }

    if( action == F_DRESS
        && ( corpse_item.has_flag( flag_FIELD_DRESS )
             || corpse_item.has_flag( flag_FIELD_DRESS_FAILED ) ) ) {
        not_this_one( _( "This corpse is already field dressed." ),
                      butcherable_rating::already_dressed );
    }

    if( action == SKIN && corpse_item.has_flag( flag_SKINNED ) ) {
        not_this_one( _( "This corpse is already skinned." ), butcherable_rating::already_skinned );
    }

    if( action == QUARTER ) {
        if( corpse.size == creature_size::tiny ) {
            not_this_one( _( "This corpse is too small to quarter without damaging." ),
                          butcherable_rating::too_small );
        }
        if( corpse_item.has_flag( flag_QUARTERED ) ) {
            not_this_one( _( "This is already quartered." ), butcherable_rating::already_quartered );
        }
        if( !( corpse_item.has_flag( flag_FIELD_DRESS )
               || corpse_item.has_flag( flag_FIELD_DRESS_FAILED ) )
            && corpse_item.get_mtype()->harvest->has_entry_type( "offal" ) ) {
            not_this_one( _( "You need to perform field dressing before quartering." ),
                          butcherable_rating::needs_dressing );
        }
    }

    if( action == BLEED ) {
        if( corpse_item.has_flag( flag_BLED ) ) {
            not_this_one( _( "This has already been bled." ), butcherable_rating::already_bled );
        }
        if( ( corpse_item.has_flag( flag_FIELD_DRESS )
              || corpse_item.has_flag( flag_FIELD_DRESS_FAILED ) )
            && corpse_item.get_mtype()->harvest->has_entry_type( "offal" ) ) {
            not_this_one( _( "Field dressed corpses no longer have blood." ),
                          butcherable_rating::already_bled );
        }
        if( corpse_item.has_flag( flag_QUARTERED ) ) {
            not_this_one( _( "Quartered corpses no longer have blood." ),
                          butcherable_rating::already_bled );
        }
    }

    // applies to all butchery actions
    const bool is_human =
        corpse.id == mtype_id::NULL_ID()
        || ( corpse.in_species( HUMAN ) && !corpse.in_species( ZOMBIE ) );
    if( is_human
        && !( u.has_trait_flag( trait_flag_CANNIBAL ) || u.has_trait_flag( trait_flag_PSYCHOPATH )
              || u.has_trait_flag( trait_flag_SAPIOVORE ) ) ) {
        need_confirm( _( "Would you dare desecrate the mortal remains of a fellow human being?" ),
                      butcherable_rating::warn_cannibalism );
    }

    setup.move_cost = butcher_time_to_cut( corpse_item, action );

    return setup;
}

int size_factor_in_time_to_cut( creature_size size )
{
    switch( size ) {
        // Time (roughly) in turns to cut up the corpse
        case creature_size::tiny:
            return 15000;
        case creature_size::small:
            return 30000;
        case creature_size::medium:
            return 45000;
        case creature_size::large:
            return 60000;
        case creature_size::huge:
            return 180000;
        default:
            debugmsg( "Invalid creature_size value for butchering corpse: %d",
                      static_cast<int>( size ) );
            break;
    }
    return 0;
}

int butcher_time_to_cut( const item& corpse_item, const butcher_type action )
{
    const mtype& corpse = *corpse_item.get_mtype();
    int time_to_cut = size_factor_in_time_to_cut( corpse.size );

    switch( action ) {
        case BUTCHER:
        case BLEED:
            break;
        case BUTCHER_FULL:
            if( !corpse_item.has_flag( flag_FIELD_DRESS )
                || corpse_item.has_flag( flag_FIELD_DRESS_FAILED ) ) {
                time_to_cut *= 6;
            } else {
                time_to_cut *= 4;
            }
            break;
        case F_DRESS:
        case SKIN:
            break;
        case QUARTER:
            time_to_cut = std::max( 1000, time_to_cut / 4 );
            break;
        case DISMEMBER:
            time_to_cut = std::max( 400, time_to_cut / 10 );
            break;
        case DISSECT:
            time_to_cut *= 4;
            break;
    }

    if( corpse_item.has_flag( flag_QUARTERED ) ) { time_to_cut /= 4; }
    return time_to_cut;
}

// this function modifies the input weight by its damage level, depending on the bodypart
static int corpse_damage_effect( int weight, const std::string& entry_type, int damage_level )
{
    const float slight_damage = 0.9;
    const float damage = 0.75;
    const float high_damage = 0.5;
    const int destroyed = 0;

    switch( damage_level ) {
        case 2:
            // "damaged"
            if( entry_type == "offal" ) { return std::round( weight * damage ); }
            if( entry_type == "skin" ) { return std::round( weight * damage ); }
            if( entry_type == "flesh" ) { return std::round( weight * slight_damage ); }
            break;
        case 3:
            // "mangled"
            if( entry_type == "offal" ) { return destroyed; }
            if( entry_type == "skin" ) { return std::round( weight * high_damage ); }
            if( entry_type == "bone" ) { return std::round( weight * slight_damage ); }
            if( entry_type == "flesh" ) { return std::round( weight * damage ); }
            break;
        case 4:
            // "pulped"
            if( entry_type == "offal" ) { return destroyed; }
            if( entry_type == "skin" ) { return destroyed; }
            if( entry_type == "bone" ) { return std::round( weight * damage ); }
            if( entry_type == "flesh" ) { return std::round( weight * high_damage ); }
            break;
        default:
            // "bruised" modifier is almost impossible to avoid; also includes no modifier (zero
            // damage)
            break;
    }
    return weight;
}

void butchery_drops_harvest(
    item* corpse_item, const mtype& mt, player& p, const std::function<int()> &roll_butchery,
    butcher_type action, const std::function<double()> &roll_drops )
{
    p.add_msg_if_player( m_neutral, mt.harvest->message() );
    int monster_weight = to_gram( mt.weight );
    monster_weight += std::round( monster_weight * rng_float( -0.1, 0.1 ) );
    if( corpse_item->has_flag( flag_QUARTERED ) ) { monster_weight *= 0.95; }
    if( corpse_item->has_flag( flag_GIBBED ) ) {
        monster_weight = std::round( 0.85 * monster_weight );
        if( action != F_DRESS ) {
            p.add_msg_if_player(
                m_bad,
                _( "You salvage what you can from the corpse, but it is "
                   "badly damaged." ) );
        }
    }
    if( corpse_item->has_flag( flag_SKINNED ) ) { monster_weight = std::round( 0.85 * monster_weight ); }
    if( corpse_item->has_flag( flag_BLED ) ) { monster_weight = std::round( 0.90 * monster_weight ); }
    int practice = 4 + roll_butchery();

    if( mt.harvest.is_null() ) {
        debugmsg( "ERROR: %s has no harvest entry.", mt.id.c_str() );
        return;
    }

    map& here = get_map();
    for( const harvest_entry& entry : *mt.harvest ) {
        const int butchery = roll_butchery();
        const float min_num = entry.base_num.first + butchery * entry.scale_num.first;
        const float max_num = entry.base_num.second + butchery * entry.scale_num.second;
        int roll = 0;
        // mass_ratio will override the use of base_num, scale_num, and max
        if( entry.mass_ratio != 0.00f ) {
            roll = static_cast<int>( std::round( entry.mass_ratio * monster_weight ) );
            roll = corpse_damage_effect( roll, entry.type, corpse_item->damage_level( 4 ) );
        } else if( entry.type != "bionic" && entry.type != "bionic_group" ) {
            roll = std::min<int>( entry.max, std::round( rng_float( min_num, max_num ) ) );
            // will not give less than min_num defined in the JSON
            roll = std::max <
                   int > ( corpse_damage_effect( roll, entry.type, corpse_item->damage_level( 4 ) ),
                           entry.base_num.first );
        }
        const itype* drop = nullptr;
        if( entry.type != "bionic_group" ) { drop = &*itype_id( entry.drop ); }

        // BIONIC handling - no code for DISSECT to let the bionic drop fall through
        if( entry.type == "bionic" || entry.type == "bionic_group" ) {
            if( action == F_DRESS ) {
                if( drop != nullptr && !drop->bionic ) {
                    if( one_in( 3 ) ) {
                        p.add_msg_if_player(
                            m_bad,
                            _( "You notice something embedded in the corpse, "
                               "perhaps harvestable via careful "
                               "dissection." ) );
                    }
                    continue;
                }
                p.add_msg_if_player(
                    m_bad,
                    _( "You notice there are implants in this corpse, that "
                       "careful dissection might preserve." ) );
                continue;
            }
            if( action == BUTCHER || action == BUTCHER_FULL || action == DISMEMBER ) {
                if( drop != nullptr && !drop->bionic ) {
                    if( one_in( 3 ) ) {
                        p.add_msg_if_player(
                            m_bad,
                            _( "Your butchering tool destroys something.  "
                               "Perhaps a more surgical approach would allow "
                               "harvesting it." ) );
                    }
                    continue;
                }
                switch( rng( 1, 3 ) ) {
                    case 1:
                        p.add_msg_if_player(
                            m_bad,
                            _( "Your butchering tool encounters something "
                               "implanted in this corpse, but your rough "
                               "cuts destroy it." ) );
                        break;
                    case 2:
                        p.add_msg_if_player(
                            m_bad,
                            _( "You find traces of implants in the body, but "
                               "you care only for the flesh." ) );
                        break;
                    case 3:
                        p.add_msg_if_player(
                            m_bad,
                            _( "You found some implants in the body, but "
                               "harvesting them would require more surgical "
                               "approach." ) );
                        break;
                }
                continue;
            }
        }

        // Check if monster was gibbed, and handle accordingly
        if( corpse_item->has_flag( flag_GIBBED ) && ( entry.type == "flesh" || entry.type == "bone" ) ) {
            roll /= 2;
        }

        // Corpses that have been skinned, field dressed, or bleed do not yield that item anymore
        // Also ensure message does not mention blood if you're not bleeding the corpse
        const bool has_any_field_dressing =
            corpse_item->has_flag( flag_FIELD_DRESS )
            || corpse_item->has_flag( flag_FIELD_DRESS_FAILED )
            || corpse_item->has_flag( flag_QUARTERED );
        const bool already_harvested =
            ( corpse_item->has_flag( flag_SKINNED ) && entry.type == "skin" )
            || ( has_any_field_dressing && entry.type == "offal" )
            || ( ( has_any_field_dressing || corpse_item->has_flag( flag_BLED ) || action != BLEED )
                 && entry.type == "blood" );
        if( already_harvested ) { roll = 0; }

        // QUICK BUTCHERY
        if( action == BUTCHER ) {
            if( entry.type == "flesh" ) {
                roll = roll / 4;
            } else if( entry.type == "bone" ) {
                roll /= 2;
            } else if( corpse_item->get_mtype()->size >= creature_size::medium
                       && ( entry.type == "skin" ) ) {
                roll /= 2;
            } else if( entry.type == "offal" ) {
                roll /= 5;
            } else {
                continue;
            }
        }
        // RIP AND TEAR
        if( action == DISMEMBER ) {
            if( entry.type == "flesh" ) {
                roll /= 6;
            } else {
                continue;
            }
        }
        // field dressing ignores everything outside below list
        if( action == F_DRESS ) {
            if( entry.type == "bone" ) { roll = rng( 0, roll / 2 ); }
            if( entry.type == "flesh" ) { continue; }
            if( entry.type == "skin" ) { continue; }
        }

        // you only get the skin from skinning
        if( action == SKIN ) {
            if( entry.type != "skin" ) { continue; }
            if( corpse_item->has_flag( flag_FIELD_DRESS_FAILED ) ) { roll = rng( 0, roll ); }
        }

        // you only get the liquids from bleeding
        if( action == BLEED ) {
            if( entry.type != "blood" ) { continue; }
        }

        // field dressing removed innards and bones from meatless limbs
        if( ( action == BUTCHER_FULL || action == BUTCHER )
            && corpse_item->has_flag( flag_FIELD_DRESS ) ) {
            if( entry.type == "offal" ) { continue; }
            if( entry.type == "bone" ) { roll = ( roll / 2 ) + rng( roll / 2, roll ); }
        }
        // unskillfull field dressing may damage the skin, meat, and other parts
        if( ( action == BUTCHER_FULL || action == BUTCHER )
            && corpse_item->has_flag( flag_FIELD_DRESS_FAILED ) ) {
            if( entry.type == "offal" ) { continue; }
            if( entry.type == "bone" ) { roll = ( roll / 2 ) + rng( roll / 2, roll ); }
            if( entry.type == "flesh" || entry.type == "skin" ) { roll = rng( 0, roll ); }
        }
        // quartering ruins skin
        if( corpse_item->has_flag( flag_QUARTERED ) ) {
            if( entry.type == "skin" ) {
                // not continue to show fail effect
                roll = 0;
            } else {
                roll /= 4;
            }
        }

        if( entry.type != "bionic" && entry.type != "bionic_group" ) {
            // divide total dropped weight by drop's weight to get amount
            if( entry.mass_ratio != 0.00f ) {
                // apply skill before converting to items, but only if mass_ratio is defined
                roll *= roll_drops();
                // cap dropped weight at monster weight * mass ratio of drop
                roll = std::min<float>( roll, to_gram( mt.weight ) * entry.mass_ratio );
                roll = std::ceil( static_cast<double>( roll ) / to_gram( drop->weight ) );
            }

            if( roll <= 0 ) {
                if( !already_harvested ) {
                    p.add_msg_if_player( m_bad, _( "You fail to harvest: %s" ), drop->nname( 1 ) );
                }
                continue;
            }
            if( drop->phase == LIQUID ) {
                detached_ptr<item> it = item::spawn( drop, calendar::turn, roll );
                item& obj = *it;
                if( obj.goes_bad() ) { obj.set_rot( corpse_item->get_rot() ); }
                for( const flag_id& flg : entry.flags ) { obj.set_flag( flg ); }
                for( const fault_id& flt : entry.faults ) { obj.faults.emplace( flt ); }
                // TODO: smarter NPC liquid handling
                if( p.is_npc() || action != butcher_type::BLEED ) {
                    drop_on_map( p, item_drop_reason::deliberate, std::move( it ), p.bub_pos() );
                } else {
                    liquid_handler::handle_all_liquid( std::move( it ), PICKUP_RANGE );
                }
            } else if( drop->count_by_charges() ) {
                detached_ptr<item> it = item::spawn( drop, calendar::turn, roll );
                item& obj = *it;
                if( obj.goes_bad() ) { obj.set_rot( corpse_item->get_rot() ); }
                for( const flag_id& flg : entry.flags ) { obj.set_flag( flg ); }
                for( const fault_id& flt : entry.faults ) { obj.faults.emplace( flt ); }
                if( !p.backlog.empty() && p.backlog.front()->id() == ACT_MULTIPLE_BUTCHER ) {
                    obj.set_var( "activity_var", p.name );
                }
                here.add_item_or_charges( p.bub_pos(), std::move( it ) );
            } else {
                item& obj = *item::spawn_temporary( drop, calendar::turn );
                obj.set_mtype( &mt );
                if( obj.goes_bad() ) { obj.set_rot( corpse_item->get_rot() ); }
                for( const flag_id& flg : entry.flags ) { obj.set_flag( flg ); }
                for( const fault_id& flt : entry.faults ) { obj.faults.emplace( flt ); }
                if( !p.backlog.empty() && p.backlog.front()->id() == ACT_MULTIPLE_BUTCHER ) {
                    obj.set_var( "activity_var", p.name );
                }
                for( int i = 0; i != roll; ++i ) {
                    here.add_item_or_charges( p.bub_pos(), item::spawn( obj ) );
                }
            }
            p.add_msg_if_player( m_good, _( "You harvest: %s" ), drop->nname( roll ) );
        }
        practice++;
    }
    // 20% of the original corpse weight is not an item, but liquid gore

    if( action != DISSECT ) {
        p.practice( skill_survival, std::max( 0, practice ),
                    std::max( mt.size - creature_size::medium, 0 ) + 4 );
    }
}

void butchery_quarter( item* corpse_item, const player& p )
{
    corpse_item->set_flag( flag_QUARTERED );
    p.add_msg_if_player(
        m_good, _( "You roughly slice the corpse of %s into four parts and set them aside." ),
        corpse_item->get_mtype()->nname() );
    map& here = get_map();
    // 4 quarters (one exists, add 3, flag does the rest)
    for( int i = 1; i <= 3; i++ ) {
        here.add_item_or_charges( p.bub_pos(), item::spawn( *corpse_item ), true );
    }
}


static bool magic_train( player_activity* act, player* p )
{
    if( !p ) { return false; }
    const spell_id& sp_id = spell_id( act->name );
    if( sp_id.is_valid() ) {
        const bool knows = g->u.magic->knows_spell( sp_id );
        if( knows ) {
            spell& studying = p->magic->get_spell( sp_id );
            const int expert_multiplier = act->values.empty() ? 0 : act->values[0];
            const int xp = roll_remainder( studying.exp_modifier( *p ) * expert_multiplier );
            studying.gain_exp( xp );
            p->add_msg_if_player( m_good, _( "You learn a little about the spell: %s" ), sp_id->name );
        } else {
            p->magic->learn_spell( act->name, *p );
            // you can decline to learn this spell , as it may lock you out of other magic.
            if( p->magic->knows_spell( sp_id ) ) {
                add_msg( m_good, _( "You learn %s." ), sp_id->name.translated() );
            } else {
                act->set_to_null();
            }
        }
        return true;
    }
    return false;
}


// TODO (https://github.com/cataclysmbn/Cataclysm-BN/issues/1612):
// Remove that repair code after repair_activity_actor.

enum repeat_type : int {
    // REPEAT_INIT should be zero. In some scenarios (veh welder), activity value default to zero.
    REPEAT_INIT = 0, // Haven't found repeat value yet.
    REPEAT_ONCE,     // Repeat just once
    REPEAT_FOREVER,  // Repeat for as long as possible
    REPEAT_FULL,     // Repeat until damage==0
    REPEAT_EVENT,    // Repeat until something interesting happens
    REPEAT_CANCEL,   // Stop repeating
};

static repeat_type repeat_menu( const std::string& title, repeat_type last_selection )
{
    uilist rmenu;
    rmenu.text = title;

    rmenu.addentry( REPEAT_ONCE, true, '1', _( "Repeat once" ) );
    rmenu.addentry( REPEAT_FOREVER, true, '2', _( "Repeat until reinforced" ) );
    rmenu.addentry( REPEAT_FULL, true, '3', _( "Repeat until fully repaired, but don't reinforce" ) );
    rmenu.addentry( REPEAT_EVENT, true, '4', _( "Repeat until success/failure/level up" ) );
    rmenu.addentry( REPEAT_INIT, true, '5', _( "Back to item selection" ) );

    rmenu.selected = last_selection - REPEAT_ONCE;
    rmenu.query();

    if( rmenu.ret >= REPEAT_INIT && rmenu.ret <= REPEAT_EVENT ) {
        return static_cast<repeat_type>( rmenu.ret );
    }

    return REPEAT_CANCEL;
}



// fish-with-rod fish catching function.
static void rod_fish( player* p, const weighted_int_list<std::pair<std::string, int>> &fishables )
{
    map& here = get_map();
    const std::pair<std::string, int> *caught = fishables.pick();
    if( caught->first.contains( "fish" ) ) {
        const std::vector<mtype_id> fish_group = MonsterGroupManager::GetMonstersFromGroup(
                mongroup_id( "GROUP_FISH" ) );
        const mtype_id fish_mon = random_entry_ref( fish_group );
        here.add_item_or_charges(
            p->bub_pos(), item::make_corpse( fish_mon, calendar::turn + rng( 0_turns, 3_hours ) ) );

        p->add_msg_if_player( m_good, _( "You caught a %s." ), fish_mon.obj().nname() );
    } else {
        itype_id possible( caught->first );
        if( possible.is_valid() ) {
            here.add_item_or_charges(
                p->bub_pos(), item::spawn( caught->first, calendar::turn, caught->second ), true );
            p->add_msg_if_player( m_good, _( "You reeled in %s." ) );
        }
    }

    for( item * &elem : here.i_at( p->bub_pos() ) ) {
        if( elem->is_corpse() && !elem->has_var( "activity_var" ) ) {
            elem->set_var( "activity_var", p->name );
        }
    }
}


void activity_handlers::try_sleep_query( player_activity* act, player* p )
{
    if( p->get_value( "sleep_query" ) == "false" ) { return; }
    uilist sleep_query;
    sleep_query.text = _( "You have trouble sleeping, keep trying?" );
    sleep_query.addentry( 1, true, 'S', _( "Stop trying to fall asleep and get up." ) );
    sleep_query.addentry( 2, true, 'c', _( "Continue trying to fall asleep." ) );
    sleep_query.addentry( 3, true, 'C', _( "Continue trying to fall asleep and don't ask again." ) );
    sleep_query.query();
    switch( sleep_query.ret ) {
        case UILIST_CANCEL:
        case 1:
            act->set_to_null();
            break;
        case 3:
            p->set_value( "sleep_query", "false" );
            break;
        case 2:
        default:
            break;
    }
}



std::vector<tripoint_bub_ms> get_sorted_tiles_by_distance(
    const tripoint_bub_ms& coord, const std::unordered_set<tripoint_bub_ms> &tiles )
{
    const auto cmp = [coord]( tripoint_bub_ms a, tripoint_bub_ms b ) {
        const int da = rl_dist( coord, a );
        const int db = rl_dist( coord, b );

        return da < db;
    };

    std::vector<tripoint_bub_ms> sorted( tiles.begin(), tiles.end() );
    std::ranges::sort( sorted, cmp );

    return sorted;
}

std::vector<tripoint_abs_ms> get_sorted_tiles_by_distance(
    const tripoint_abs_ms& coord, const std::unordered_set<tripoint_abs_ms> &tiles )
{
    const auto cmp = [coord]( tripoint_abs_ms a, tripoint_abs_ms b ) {
        const int da = rl_dist( coord, a );
        const int db = rl_dist( coord, b );

        return da < db;
    };

    std::vector<tripoint_abs_ms> sorted( tiles.begin(), tiles.end() );
    std::ranges::sort( sorted, cmp );

    return sorted;
}

template <typename fn>
static void cleanup_tiles( std::unordered_set<tripoint_abs_ms> &tiles, fn& cleanup )
{
    auto it = tiles.begin();
    map& here = get_map();
    while( it != tiles.end() ) {
        auto current = it++;

        if( cleanup( here.abs_to_bub( *current ) ) ) { tiles.erase( current ); }
    }
}

void activity_handlers::perform_zone_activity_turn(
    player* p, const zone_type_id& ztype,
    const std::function<bool( const tripoint_bub_ms & )> &tile_filter,
    const std::function<void( player& p, const tripoint_bub_ms & )> &tile_action,
    const std::string& finished_msg )
{
    const zone_manager& mgr = zone_manager::get_manager();
    map& here = get_map();
    const auto abspos = p->abs_pos();
    std::unordered_set<tripoint_abs_ms> unsorted_tiles = mgr.get_near( ztype, abspos );

    cleanup_tiles( unsorted_tiles, tile_filter );

    // sort remaining tiles by distance
    const std::vector<tripoint_abs_ms> &tiles =
        get_sorted_tiles_by_distance( abspos, unsorted_tiles );

    for( const auto& tile : tiles ) {
        const tripoint_bub_ms& tile_loc = here.abs_to_bub( tile );

        auto route = here.route(
                         p->bub_pos(), tile_loc, p->get_legacy_pathfinding_settings(),
                         p->get_legacy_path_avoid() );
        if( route.size() > 1 ) {
            route.pop_back();

            p->set_destination( route, p->remove_activity() );
            p->activity = std::make_unique<player_activity>();
            return;
        } else {
            // we are at destination already
            /* Perform action */
            tile_action( *p, tile_loc );
            if( p->moves <= 0 ) { return; }
        }
    }
    add_msg( m_info, finished_msg );
    p->activity->set_to_null();
}


static void blood_magic( player* p, int cost )
{
    std::vector<uilist_entry> uile;
    std::vector<bodypart_id> parts;
    int i = 0;
    for( const bodypart_id& bp : p->get_all_body_parts( true ) ) {
        const int hp_cur = p->get_part_hp_cur( bp );
        uilist_entry entry( i, hp_cur > cost, i + 49, body_part_hp_bar_ui_text( bp ) );

        const std::pair<std::string, nc_color> &hp = get_hp_bar( hp_cur, p->get_part_hp_max( bp ) );
        entry.ctxt = colorize( hp.first, hp.second );
        uile.emplace_back( entry );
        parts.push_back( bp );
        i++;
    }
    int action = -1;
    while( action < 0 ) { action = uilist( _( "Choose part\nto draw blood from." ), uile ); }
    p->mod_part_hp_cur( parts[action], -cost );
    p->mod_pain( std::max( 1, cost / 3 ) );
}


// This is just used for robofac_intercom_mission_2
