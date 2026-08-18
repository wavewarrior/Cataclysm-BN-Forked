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

void iexamine::cvdmachine( player &p, const tripoint_bub_ms & )
{
    // Select an item to which it is possible to apply a diamond coating
    auto loc = g->inv_map_splice( []( const item & e ) {
        return ( e.is_melee( DT_CUT ) || e.is_melee( DT_STAB ) ) && e.made_of( material_id( "steel" ) ) &&
               !e.has_flag( flag_DIAMOND ) && !e.has_flag( flag_NO_CVD );
    }, _( "Apply diamond coating" ), 1, _( "You don't have a suitable item to coat with diamond" ) );

    if( !loc ) {
        return;
    }

    // Require materials proportional to selected item volume
    auto qty = loc->volume() / units::legacy_volume_factor;
    qty = std::max( 1, qty );
    auto reqs = *requirement_id( "cvd_diamond" ) * qty;

    if( !reqs.can_make_with_inventory( p.crafting_inventory(), is_crafting_component ) ) {
        popup( "%s", reqs.list_missing() );
        return;
    }

    // Consume materials
    for( const auto &e : reqs.get_components() ) {
        p.consume_items( e, 1, is_crafting_component );
    }
    for( const auto &e : reqs.get_tools() ) {
        p.consume_tools( e );
    }
    p.invalidate_crafting_inventory();

    // Apply flag to item
    loc->set_flag( flag_DIAMOND );
    add_msg( m_good, _( "You apply a diamond coating to your %s" ), loc->type_name() );
    p.mod_moves( -to_turns<int>( 10_seconds ) );
}

void iexamine::nanofab( player &p, const tripoint_bub_ms &examp )
{
    bool table_exists = false;
    tripoint_bub_ms spawn_point;
    map &here = get_map();
    for( const auto &valid_location : here.points_in_radius( examp, 1 ) ) {
        if( here.ter( valid_location ) == ter_str_id( "t_nanofab_body" ) ) {
            spawn_point = valid_location;
            table_exists = true;
            break;
        }
    }
    if( !table_exists ) {
        return;
    }

    auto nanofab_template = g->inv_map_splice( []( const item & e ) {
        return e.has_var( "NANOFAB_GROUP_ID" ) || e.has_var( "NANOFAB_ITEM_ID" );
    }, _( "Introduce nanofabricator template:" ), PICKUP_RANGE,
    _( "You don't have any usable templates." ) );

    if( !nanofab_template ) {
        return;
    }

    std::vector<std::string> recipe_ids;

    if( nanofab_template->has_var( "NANOFAB_GROUP_ID" ) ) {
        // Preferred behavior: build from group
        item_group_id group_id( nanofab_template->get_var( "NANOFAB_GROUP_ID" ) );
        std::set<const itype *> all_items = item_group::every_possible_item_from( group_id );
        for( const itype *it : all_items ) {
            recipe_ids.push_back( it->get_id().str() );
        }
    } else if( nanofab_template->has_var( "NANOFAB_ITEM_ID" ) ) {
        // Fallback for old templates: use single stored recipe
        recipe_ids.push_back( nanofab_template->get_var( "NANOFAB_ITEM_ID" ) );
    }

    if( recipe_ids.empty() ) {
        return;
    }

    std::string chosen_recipe;
    if( recipe_ids.size() > 1 ) {
        uilist menu;
        menu.text = _( "Choose a recipe:" );
        for( size_t i = 0; i < recipe_ids.size(); ++i ) {
            itype_id item = itype_id( recipe_ids[i] );
            auto button_text = string_format( "%s [%d]", item->nname( 1 ),
                                              std::max( 1, item->volume / 250_ml ) * 5 );
            menu.addentry( i, true, -1, button_text );
        }
        menu.query();

        if( menu.ret >= 0 && static_cast<size_t>( menu.ret ) < recipe_ids.size() ) {
            chosen_recipe = recipe_ids[ menu.ret ];
        }
    } else {
        chosen_recipe = recipe_ids.front();
    }

    if( chosen_recipe.empty() ) {
        return;
    }

    int item_count = 1;

    detached_ptr<item> new_item = item::spawn( itype_id( chosen_recipe ), calendar::turn );

    if( new_item->made_of( LIQUID ) ) {
        const int amount = string_input_popup()
                           .title( _( "Dispense how many units?" ) )
                           .width( 5 )
                           .text( std::to_string( 1 ) )
                           .only_digits( true )
                           .query_int();
        item_count = amount;

        new_item = item::spawn( itype_id( chosen_recipe ), calendar::turn, item_count );
    }

    auto qty = std::max( 1, new_item->volume() / 250_ml );
    auto reqs = *requirement_id( "nanofabricator" ) * qty;

    if( !reqs.can_make_with_inventory( p.crafting_inventory(), is_crafting_component ) ) {
        popup( "%s", reqs.list_missing() );
        return;
    }

    for( const auto &e : reqs.get_components() ) {
        p.consume_items( e, 1, is_crafting_component );
    }
    for( const auto &e : reqs.get_tools() ) {
        p.consume_tools( e );
    }
    p.invalidate_crafting_inventory();

    if( new_item->is_armor() && new_item->has_flag( flag_VARSIZE ) ) {
        new_item->set_flag( flag_FIT );
    }

    // we're sticking an item from our inventory under the nanofabrication dispenser
    if( new_item->made_of( LIQUID ) ) {
        liquid_handler::handle_all_liquid( std::move( new_item ), PICKUP_RANGE );  // let it own the pointer
        return;
    }

    here.add_item_or_charges( spawn_point, std::move( new_item ) );
}

void iexamine::nanoforge( player &p, const tripoint_bub_ms &examp )
{
    if( !query_yn(
            _( "Use the superalloy forge? Requires 1 sheet metal and 5 nanomaterial canisters." ) ) ) {
        none( p, examp );
        return;
    }

    bool table_exists = false;
    tripoint_bub_ms spawn_point;
    map &here = get_map();
    for( const auto &valid_location : here.points_in_radius( examp, 1 ) ) {
        if( here.ter( valid_location ) == ter_str_id( "t_nanoforge_body" ) ) {
            spawn_point = valid_location;
            table_exists = true;
            break;
        }
    }
    if( !table_exists ) {
        return;
    }

    std::vector<std::string> recipe_ids;
    recipe_ids.push_back( "alloy_sheet" );

    if( recipe_ids.empty() ) {
        return;
    }

    std::string chosen_recipe = recipe_ids.front();;

    if( chosen_recipe.empty() ) {
        return;
    }

    detached_ptr<item> new_item = item::spawn( itype_id( chosen_recipe ), calendar::turn );

    auto qty = 1;
    auto reqs = *requirement_id( "superalloy_forge" ) * qty;

    if( !reqs.can_make_with_inventory( p.crafting_inventory(), is_crafting_component ) ) {
        popup( "%s", reqs.list_missing() );
        return;
    }

    for( const auto &e : reqs.get_components() ) {
        p.consume_items( e, 1, is_crafting_component );
    }
    for( const auto &e : reqs.get_tools() ) {
        p.consume_tools( e );
    }
    p.invalidate_crafting_inventory();

    here.add_item_or_charges( spawn_point, std::move( new_item ) );
}

void iexamine::gaspump( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    if( !query_yn( _( "Use the %s?" ), here.tername( examp ) ) ) {
        none( p, examp );
        return;
    }

    auto items = here.i_at( examp );
    for( auto item_it = items.begin(); item_it != items.end(); ++item_it ) {
        item *content = *item_it;
        if( content->made_of( LIQUID ) ) {
            if( content->charges <= 0 ) {
                add_msg( m_info, _( "Out of order." ) );
                return;
            }
            item_it = location_vector<item>::iterator();
            ///\EFFECT_DEX decreases chance of spilling gas from a pump
            if( one_in( 10 + p.get_dex() ) ) {
                add_msg( m_bad, _( "You accidentally spill the %s." ), content->type_name() );
                static const auto max_spill_volume = units::from_liter( 1 );
                const int max_spill_charges = std::max( 1, content->charges_per_volume( max_spill_volume ) );
                ///\EFFECT_DEX decreases amount of gas spilled from a pump
                const int qty = rng( 1, max_spill_charges * 8.0 / std::max( 1, p.get_dex() ) );

                //This could invalidate the iterator, but it's not used again as we return right after
                detached_ptr<item> spill = content->split( qty );
                here.add_item_or_charges( p.bub_pos(), std::move( spill ) );
                return;
            } else {
                liquid_handler::handle_liquid( *content, 1 );
            }
            return;
        }
    }
    add_msg( m_info, _( "Out of order." ) );
}

void iexamine::translocator( player &, const tripoint_bub_ms &examp )
{
    // TODO: fix point types
    const tripoint_abs_omt omt_loc( project_to<coords::omt>( get_map().bub_to_abs( examp ) ) );
    avatar &player_character = get_avatar();
    const bool activated = player_character.translocators->knows_translocator( omt_loc );
    if( !activated ) {
        g->u.translocators->activate_teleporter( omt_loc, examp );
        add_msg( m_info, _( "Translocator gate active." ) );
    } else {
        const int choice = uilist( _( "Do what with Translocator?" ), {
            _( "Visit another gate." ),
            _( "Deactivate." )
        } );
        if( choice == 0 ) {
            item *vtm = item::spawn_temporary( "translocation_caster", calendar::start_of_cataclysm );
            player_character.invoke_item( vtm );
        } else if( choice == 1 ) {
            if( query_yn( _( "Deactivate this Translocator?" ) ) ) {
                g->u.translocators->deactivate_teleporter( omt_loc, examp );
            }
        } else {
            add_msg( _( "Never mind." ) );
        }
    }
}

class atm_menu
{
    public:
        // menu choices
        enum options : int {
            cancel, purchase_card, deposit_money, withdraw_money, deposit_cash, withdraw_cash, transfer_all_money
        };

        atm_menu()                           = delete;
        atm_menu( atm_menu const & )            = delete;
        atm_menu( atm_menu && )                 = delete;
        atm_menu &operator=( atm_menu const & ) = delete;
        atm_menu &operator=( atm_menu && )      = delete;

        explicit atm_menu( player &p ) : u( p ) {
            reset( false );
        }

        void start() {
            for( bool result = false; !result; ) {
                switch( choose_option() ) {
                    case purchase_card:
                        result = do_purchase_card();
                        break;
                    case deposit_money:
                        result = do_deposit_money();
                        break;
                    case withdraw_money:
                        result = do_withdraw_money();
                        break;
                    case deposit_cash:
                        result = do_deposit_cash();
                        break;
                    case withdraw_cash:
                        result = do_withdraw_cash();
                        break;
                    case transfer_all_money:
                        result = do_transfer_all_money();
                        break;
                    default:
                        return;
                }
                if( !u.activity->is_null() ) {
                    break;
                }
            }
        }
    private:
        int value_of_money_bundle = item::spawn_temporary( itype_money_bundle,
                                    calendar::start_of_cataclysm )->price( false );

        void add_choice( const int i, const char *const title ) {
            amenu.addentry( i, true, -1, title );
        }
        void add_info( const int i, const char *const title ) {
            amenu.addentry( i, false, -1, title );
        }

        options choose_option() {
            if( u.activity->id() == ACT_ATM ) {
                return static_cast<options>( u.activity->index );
            }
            amenu.query();
            uistate.iexamine_atm_selected = amenu.selected;
            return amenu.ret < 0 ? cancel : static_cast<options>( amenu.ret );
        }

        //! Reset and repopulate the menu; with a fair bit of work this could be more efficient.
        void reset( const bool clear = true ) {
            const int cash_amount   = u.amount_of( itype_money_bundle );
            const int card_count   = u.amount_of( itype_cash_card );
            const int charge_count = card_count ? u.charges_of( itype_cash_card ) : 0;

            if( clear ) {
                amenu.reset();
            }

            amenu.selected = uistate.iexamine_atm_selected;
            amenu.text = string_format( _( "Welcome to the C.C.B.o.t.T. ATM.  What would you like to do?\n"
                                           "Your current balance is: %s" ),
                                        format_money( u.cash ) );

            if( u.cash >= 1000 ) {
                add_choice( purchase_card, _( "Purchase cash card" ) );
            } else {
                add_info( purchase_card, _( "You need $10.00 in your account to purchase a card." ) );
            }

            if( u.cash > value_of_money_bundle ) {
                add_choice( withdraw_cash, _( "Withdraw cash" ) );
            } else if( u.cash < 0 ) {
                add_info( withdraw_cash,
                          _( "You need to pay down your debt before withdrawing cash!" ) );
            } else {
                add_info( withdraw_cash,
                          _( "You don't have enough to withdraw a money bundle!" ) );
            }

            if( cash_amount > 0 ) {
                add_choice( deposit_cash, _( "Deposit cash" ) );
            } else {
                add_info( deposit_cash,
                          _( "You need cash to deposit!" ) );
            }

            if( card_count && u.cash > 0 ) {
                add_choice( withdraw_money, _( "Withdraw onto cash card" ) );
            } else if( u.cash > 0 ) {
                add_info( withdraw_money, _( "You need a cash card before you can withdraw money!" ) );
            } else if( u.cash < 0 ) {
                add_info( withdraw_money,
                          _( "You need to pay down your debt before withdrawing money onto a card!" ) );
            } else {
                add_info( withdraw_money,
                          _( "You need money in your account before you can withdraw money!" ) );
            }

            if( charge_count ) {
                add_choice( deposit_money, _( "Deposit from cash card" ) );
            } else {
                add_info( deposit_money,
                          _( "You need a charged cash card before you can deposit money!" ) );
            }

            if( card_count >= 2 && charge_count ) {
                add_choice( transfer_all_money, _( "Combine cash cards" ) );
            }
        }

        //! print a bank statement for @p print = true;
        void finish_interaction( const bool print = true ) {
            if( print ) {
                if( u.cash < 0 ) {
                    add_msg( m_info, _( "Your debt is now %s." ), format_money( u.cash ) );
                } else {
                    add_msg( m_info, _( "Your account now holds %s." ), format_money( u.cash ) );
                }
            }

            u.moves -= to_turns<int>( 5_seconds );
        }

        //! Prompt for an integral value clamped to [0, max].
        static int prompt_for_amount( const char *const msg, const int max ) {
            const std::string formatted = string_format( msg, max );
            const int amount = string_input_popup()
                               .title( formatted )
                               .width( 20 )
                               .text( std::to_string( max ) )
                               .only_digits( true )
                               .query_int();

            return clamp( amount, 0, max );
        }

        //!Get a new cash card. $10.00 fine.
        bool do_purchase_card() {
            const char *prompt =
                _( "This will automatically deduct $10.00 from your bank account.  Continue?" );

            if( !query_yn( prompt ) ) {
                return false;
            }

            detached_ptr<item> card = item::spawn( "cash_card", calendar::turn );
            card->charges = 0;
            u.i_add( std::move( card ) );
            u.cash -= 1000;
            u.moves -= to_turns<int>( 5_seconds );
            finish_interaction();

            return true;
        }

        //!Deposit money from cash card into bank account.
        bool do_deposit_cash() {
            int money = u.charges_of( itype_money_bundle );

            if( !money ) {
                popup( _( "You can only deposit money from charged cash cards!" ) );
                return false;
            }

            const int amount = prompt_for_amount( vgettext(
                    "Deposit how many bundles?  Max: %d bundles.  (0 to cancel) ",
                    "Deposit how many bundles?  Max: %d bundles.  (0 to cancel) ", money ),
                                                  money );

            if( !amount ) {
                return false;
            }

            u.use_charges( itype_money_bundle, amount );
            u.cash += amount * value_of_money_bundle;
            u.moves -= to_turns<int>( 10_seconds );
            finish_interaction();

            return true;
        }

        //!Deposit money from cash card into bank account.
        bool do_deposit_money() {
            int money = u.charges_of( itype_cash_card );

            if( !money ) {
                popup( _( "You can only deposit money from charged cash cards!" ) );
                return false;
            }

            const int amount = prompt_for_amount( vgettext(
                    "Deposit how much?  Max: %d cent.  (0 to cancel) ",
                    "Deposit how much?  Max: %d cents.  (0 to cancel) ", money ), money );

            if( !amount ) {
                return false;
            }

            add_msg( m_info, "amount: %d", amount );
            u.use_charges( itype_cash_card, amount );
            u.cash += amount;
            u.moves -= to_turns<int>( 10_seconds );
            finish_interaction();

            return true;
        }

        //!Move money from bank account onto cash card.
        bool do_withdraw_money() {
            //We may want to use visit_items here but that's fairly heavy.
            //For now, just check weapon if we didn't find it in the inventory.
            int pos = u.inv_position_by_type( itype_cash_card );
            item *dst;
            if( pos == INT_MIN ) {
                dst = &u.primary_weapon();
            } else {
                dst = &u.i_at( pos );
            }

            if( dst == nullptr || dst->is_null() ) {
                //Just in case we run into an edge case
                popup( _( "You do not have a cash card to withdraw money!" ) );
                return false;
            }

            const int amount = prompt_for_amount( vgettext(
                    "Withdraw how much?  Max: %d cent.  (0 to cancel) ",
                    "Withdraw how much?  Max: %d cents.  (0 to cancel) ", u.cash ), u.cash );

            if( !amount ) {
                return false;
            }

            dst->charges += amount;
            u.cash -= amount;
            u.moves -= to_turns<int>( 10_seconds );
            finish_interaction();

            return true;
        }

        //!Move money from bank account onto cash card.
        bool do_withdraw_cash() {
            const int amount = prompt_for_amount( vgettext(
                    "Withdraw how much?  Max: %d bundles.  (0 to cancel) ",
                    "Withdraw how much?  Max: %d bundles.  (0 to cancel) ", u.cash / value_of_money_bundle ),
                                                  u.cash / value_of_money_bundle );

            if( !amount ) {
                return false;
            }

            for( int i = 0; i < amount; i++ ) {
                detached_ptr<item> card = item::spawn( "money_bundle", calendar::turn );

                u.i_add( std::move( card ) );
                u.cash -= value_of_money_bundle;
            }

            u.moves -= to_turns<int>( 5_seconds );

            finish_interaction();

            return true;
        }

        //!Move the money from all the cash cards in inventory to a single card.
        bool do_transfer_all_money() {
            item *dst;
            if( u.activity->id() == ACT_ATM ) {
                u.activity->set_to_null(); // stop for now, if required, it will be created again.
                //TODO!: safety check?
                dst = &*u.activity->targets.front();
                if( dst == nullptr || dst->is_null() || dst->typeId() != itype_cash_card ) {
                    return false;
                }
            } else {
                const int pos = u.inv_position_by_type( itype_cash_card );

                if( pos == INT_MIN ) {
                    return false;
                }
                dst = &u.i_at( pos );
            }

            for( auto &i : u.inv_dump() ) {
                if( i == dst || i->charges <= 0 || i->typeId() != itype_cash_card ) {
                    continue;
                }
                if( u.moves < 0 ) {
                    // Money from `*i` could be transferred, but we're out of moves, schedule it for
                    // the next turn. Putting this here makes sure there will be something to be
                    // done next turn.
                    u.assign_activity( std::make_unique<player_activity>( std::make_unique<atm_activity_actor>() ) );
                    break;
                }

                dst->charges += i->charges;
                i->charges = 0;
                u.moves -= 10;
            }

            return true;
        }

        player &u;
        uilist amenu;
};

void iexamine::atm( player &p, const tripoint_bub_ms & )
{
    atm_menu {p} .start();
}

struct vend_row {
    Rml::String text_rml;
    bool selected = false;
};

struct vend_session {
    Rml::String money_rml;
    Rml::Vector<vend_row> rows;
    Rml::String info_header_rml;
    Rml::String info_rml;
    Rml::DataModelHandle handle;
};

static bool g_vend_types_registered = false;

void register_vend_rml_types( Rml::DataModelConstructor &c )
{
    if( g_vend_types_registered ) {
        return;
    }
    Rml::StructHandle<vend_row> rh = c.RegisterStruct<vend_row>();
    rh.RegisterMember( "text_rml", &vend_row::text_rml );
    rh.RegisterMember( "selected", &vend_row::selected );
    c.RegisterArray<Rml::Vector<vend_row>>();
    g_vend_types_registered = true;
}

void iexamine::vending( player &p, const tripoint_bub_ms &examp )
{
    constexpr int moves_cost = to_turns<int>( 5_seconds );
    int money = p.charges_of( itype_cash_card );
    auto vend_items = get_map().i_at( examp );

    if( vend_items.empty() ) {
        add_msg( m_info, _( "The vending machine is empty." ) );
        return;
    }

    if( !money ) {
        popup( _( "You need some money on a cash card to buy things." ) );
    }

    int w_items_w = 0;
    int w_info_w = 0;
    int list_lines = 0;
    int lines_above = 0;
    int lines_below = 0;

    catacurses::window w;
    catacurses::window w_item_info;

    ui_adaptor ui;
    ui.on_screen_resize( [&]( ui_adaptor & ui ) {
        const point padding( std::max( 0, TERMX - FULL_SCREEN_WIDTH ) / 4, std::max( 0,
                             TERMY - FULL_SCREEN_HEIGHT ) / 6 );
        const int window_h   = FULL_SCREEN_HEIGHT + std::max( 0, TERMY - FULL_SCREEN_HEIGHT ) * 2 / 3;
        const int window_w   = FULL_SCREEN_WIDTH + std::max( 0, TERMX - FULL_SCREEN_WIDTH ) / 2;
        w_items_w  = window_w / 2;
        w_info_w   = window_w - w_items_w;
        list_lines = window_h - 4; // minus for header and footer

        lines_above = list_lines / 2;                  // lines above the selector
        lines_below = list_lines / 2 + list_lines % 2; // lines below the selector

        w = catacurses::newwin( window_h, w_items_w,
                                padding );
        w_item_info = catacurses::newwin( window_h, w_info_w,
                                          padding + point( w_items_w, 0 ) );

        ui.position( padding, point( window_w, window_h ) );
    } );
    ui.mark_resize();

    bool used_machine = false;
    input_context ctxt( "VENDING_MACHINE" );
    ctxt.register_updown();
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );

    std::vector<std::vector<item *>> item_map;
    for( item * &it : vend_items ) {
        // |# {name}|
        // 123      4
        std::string name = it->tname();
        bool found = false;
        for( auto item_list : item_map ) {
            if( item_list.front()->tname() == name ) {
                item_list.push_back( it );
                found = true;
                break;
            }
        }
        if( !found ) {
            item_map.push_back( {it} );
        }
    }

    int cur_pos = 0;

    // RmlUi render path (render-only; keyboard owns nav/confirm below).
    auto rml_data = std::make_unique<vend_session>();
    rml_doc rml;
    const auto sync_rml = [&]() {
        if( !rml_data->handle ) {
            return;
        }
        rml_data->money_rml = cata_text_to_rml( string_format( _( "Money left: %s" ),
                                                format_money( money ) ) );
        rml_data->rows.clear();
        const int num_items = item_map.size();
        for( int i = 0; i < num_items; i++ ) {
            const auto &elem = item_map[i];
            const auto count = elem.size();
            const char cc = count < 10 ? static_cast<char>( '0' + count ) : '*';
            vend_row r;
            r.text_rml = cata_text_to_rml( colorize(
                                               string_format( "%c %s", cc, elem.front()->tname() ),
                                               elem.front()->color_in_inventory( p ) ) );
            r.selected = ( i == cur_pos );
            rml_data->rows.push_back( r );
        }
        if( num_items > 0 && cur_pos >= 0 && cur_pos < num_items ) {
            const item *cur_item = item_map[cur_pos].back();
            rml_data->info_header_rml = cata_text_to_rml( string_format( "<%s> %s",
                                        colorize( cur_item->display_name(), cur_item->color_in_inventory( p ) ),
                                        format_money( cur_item->price( false ) ) ) );
            rml_data->info_rml = cata_text_to_rml( cur_item->info_string() );
        } else {
            rml_data->info_header_rml.clear();
            rml_data->info_rml.clear();
        }
        rml_data->handle.DirtyAllVariables();
    };

    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( rml ) {
            sync_rml();
            return;
        }
    } );

    rml.open( vending_rmlui_enabled(), "vending", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
        register_vend_rml_types( c );
        c.Bind( "money_rml", &rml_data->money_rml );
        c.Bind( "rows", &rml_data->rows );
        c.Bind( "info_header_rml", &rml_data->info_header_rml );
        c.Bind( "info_rml", &rml_data->info_rml );
        rml_data->handle = c.GetModelHandle();
    } );

    for( ;; ) {
        ui_manager::redraw();

        const int num_items = item_map.size();

        // Item info
        auto &cur_items = item_map[static_cast<size_t>( cur_pos )];
        auto &cur_item  = cur_items.back();

        const std::string &action = ctxt.handle_input();
        if( action == "DOWN" ) {
            cur_pos = ( cur_pos + 1 ) % num_items;
        } else if( action == "UP" ) {
            cur_pos = ( cur_pos + num_items - 1 ) % num_items;
        } else if( action == "CONFIRM" ) {
            const int iprice = cur_item->price( false );

            if( iprice > money ) {
                popup( _( "You can't afford that item." ) );
                continue;
            }

            if( !used_machine ) {
                used_machine = true;
                p.moves -= moves_cost;
            }

            money -= iprice;
            p.use_charges( itype_cash_card, iprice );
            p.i_add_or_drop( vend_items.remove( cur_item ) );

            cur_items.pop_back();
            if( !cur_items.empty() ) {
                continue;
            }

            item_map.erase( std::begin( item_map ) + cur_pos );
            if( item_map.empty() ) {
                add_msg( _( "With a beep, the empty vending machine shuts down." ) );
                return;
            } else if( cur_pos == num_items - 1 ) {
                cur_pos--;
            }
        } else if( action == "QUIT" ) {
            break;
        }
    }
}

void iexamine::toilet( player &p, const tripoint_bub_ms &examp )
{
    auto items = get_map().i_at( examp );
    auto water = items.begin();
    for( ; water != items.end(); ++water ) {
        if( ( *water )->typeId() == itype_water ) {
            break;
        }
    }

    if( water == items.end() ) {
        add_msg( m_info, _( "This toilet is empty." ) );
    } else if( !( *water )->made_of( LIQUID ) ) {
        add_msg( m_info, _( "The toilet water is frozen solid!" ) );
    } else {
        // Use a different poison value each time water is drawn from the toilet.
        ( *water )->poison = one_in( 3 ) ? 0 : rng( 1, 3 );

        // TODO: use me
        ( void ) p;
        liquid_handler::handle_liquid( **water );
    }
}

void iexamine::toggle_lights( player &/*p*/, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    const auto flag = here.has_flag_furn( "L_OFF", examp ) ? "L_OFF" : "L_ON";

    add_msg( _( here.furn( examp ).obj().message ) );

    for( const auto &light_loc : here.find_furnitures_with_flag_in_omt( examp, flag ) ) {
        here.furn_set( light_loc, here.get_furn_transforms_into( light_loc ) );
    };
}

void iexamine::controls_gate( player &p, const tripoint_bub_ms &examp )
{
    if( !query_yn( _( "Use the %s?" ), get_map().tername( examp ) ) ) {
        none( p, examp );
        return;
    }
    g->toggle_gate( examp );
}

static bool try_start_hacking( player &p, const tripoint_bub_ms &examp )
{
    if( p.has_trait( trait_ILLITERATE ) ) {
        add_msg( _( "You cannot read!" ) );
        return false;
    }
    const bool has_item = p.has_charges( itype_electrohack, 25 );
    const bool has_bionic = p.has_bionic( bio_fingerhack ) && p.get_power_level() >= 25_kJ;
    if( !has_item && !has_bionic ) {
        add_msg( _( "You don't have a hacking tool with enough charges!" ) );
        return false;
    }
    bool use_bionic = has_bionic;
    if( has_item && has_bionic ) {
        uilist menu;
        menu.settext( _( "Use which hacking tool?" ) );
        menu.addentry( 0, true, MENU_AUTOASSIGN, "%s", itype_electrohack->nname( 1 ) );
        menu.addentry( 1, true, MENU_AUTOASSIGN, "%s", bio_fingerhack->name );
        menu.query();
        switch( menu.ret ) {
            case 0:
                use_bionic = false;
                break;
            case 1:
                use_bionic = true;
                break;
            default:
                return false;
        }
    }
    if( use_bionic ) {
        p.mod_power_level( -25_kJ );
        p.assign_activity( std::make_unique<player_activity>( std::make_unique<hacking_activity_actor>(
                               hacking_activity_actor::use_bionic {} ) ) );
    } else {
        p.use_charges( itype_electrohack, 25 );
        p.assign_activity( std::make_unique<player_activity>
                           ( std::make_unique<hacking_activity_actor>() ) );
    }
    p.activity->placement = bub_to_abs( examp );
    return true;
}

void iexamine::cardreader( player &p, const tripoint_bub_ms &examp )
{
    bool open = false;
    map &here = get_map();
    itype_id card_type = ( here.ter( examp ) == t_card_science ? itype_id_science :
                           here.ter( examp ) == t_card_military ? itype_id_military :
                           itype_id_industrial );
    if( p.has_amount( card_type, 1 ) && query_yn( _( "Swipe your ID card?" ) ) ) {
        p.mod_moves( -to_turns<int>( 1_seconds ) );
        for( const tripoint_bub_ms &tmp : here.points_in_radius( examp, 3 ) ) {
            if( here.ter( tmp ) == t_door_metal_locked ) {
                const auto is_door = [&here]( const tripoint_bub_ms & pos ) -> bool { return here.ter( pos ) == t_door_metal_locked; };

                std::unordered_set<tripoint_bub_ms> visited;
                for( const tripoint_bub_ms &tmp2 : ff::point_flood_fill_4_connected( tmp, visited, is_door ) ) {
                    here.ter_set( tmp2, t_door_metal_c );
                    open = true;
                }
            }
        }
        for( monster &critter : g->all_monsters() ) {
            // Check 1) same overmap coords, 2) turret, 3) hostile
            if( project_to<coords::omt>( here.bub_to_abs( critter.bub_pos() ) ) == project_to<coords::omt>
                ( here.bub_to_abs(
                      examp ) ) &&
                critter.has_flag( MF_ID_CARD_DESPAWN ) &&
                critter.attitude_to( p ) == Attitude::A_HOSTILE ) {
                g->remove_zombie( critter );
            }
        }
        if( open ) {
            add_msg( _( "You insert your ID card." ) );
            add_msg( m_good, _( "The nearby doors unlock." ) );
            p.use_amount( card_type, 1 );
        } else {
            add_msg( _( "The nearby doors are already opened." ) );
        }
    } else if( query_yn( _( "Attempt to hack this card-reader?" ) ) ) {
        try_start_hacking( p, examp );
    }
}

void iexamine::cardreader_robofac( player &p, const tripoint_bub_ms &examp )
{
    itype_id card_type = itype_id_science;
    if( p.has_amount( card_type, 1 ) && query_yn( _( "Swipe your ID card?" ) ) ) {
        p.mod_moves( -100 );
        p.use_amount( card_type, 1 );
        add_msg( m_bad, _( "The card reader short circuits!" ) );
        get_map().ter_set( examp, t_card_reader_broken );
        intercom( p, examp );
    } else {
        add_msg( _( "You have never seen this card reader model before.  Hacking it seems impossible." ) );
    }
}

void iexamine::cardreader_foodplace( player &p, const tripoint_bub_ms &examp )
{
    bool open = false;
    if( ( p.is_wearing( itype_id( "foodperson_mask" ) ) ||
          p.is_wearing( itype_id( "foodperson_mask_on" ) ) ) &&
        query_yn( _( "Press mask on the reader?" ) ) ) {
        p.mod_moves( -100 );
        map &here = get_map();
        for( const tripoint_bub_ms &tmp : here.points_in_radius( examp, 3 ) ) {
            if( here.ter( tmp ) == t_door_metal_locked ) {
                here.ter_set( tmp, t_door_metal_c );
                open = true;
            }
        }
        if( open ) {
            add_msg( _( "You press your face on the reader." ) );
            add_msg( m_good, _( "The nearby doors are unlocked." ) );
            sound_event se;
            se.origin = examp;
            se.volume = 50;
            se.category = sounds::sound_t::electronic_speech;
            se.description = _( "\"Hello Foodperson.  Welcome home.\"" );
            se.id = "speech";
            se.variant = "welcome";
            sounds::sound( se );
        } else {
            add_msg( _( "The nearby doors are already unlocked." ) );
            if( query_yn( _( "Lock doors?" ) ) ) {
                for( const tripoint_bub_ms &tmp : here.points_in_radius( examp, 3 ) ) {
                    if( here.ter( tmp ) == t_door_metal_o || here.ter( tmp ) == t_door_metal_c ) {
                        if( p.bub_pos() == tmp ) {
                            p.add_msg_if_player( m_bad, _( "You are in the way of the door, move before trying again." ) );
                        } else {
                            here.ter_set( tmp, t_door_metal_locked );
                        }
                    }
                }
            }
        }
    } else if( p.has_amount( itype_id( "foodperson_mask" ), 1 ) ||
               p.has_amount( itype_id( "foodperson_mask_on" ), 1 ) ) {
        sound_event se;
        se.origin = examp;
        se.volume = 50;
        se.category = sounds::sound_t::electronic_speech;
        se.description = _( "\"FOODPERSON DETECTED.  Please make yourself presentable.\"" );
        se.id = "speech";
        se.variant = "welcome";
        sounds::sound( se );
    } else {
        sound_event se;
        se.origin = examp;
        se.volume = 50;
        se.category = sounds::sound_t::electronic_speech;
        se.description = _( "\"Your face is inadequate.  Please go away.\"" );
        se.id = "speech";
        se.variant = "welcome";
        sounds::sound( se );
        if( query_yn( _( "Attempt to hack this card-reader?" ) ) ) {
            try_start_hacking( p, examp );
        }
    }
}

void iexamine::intercom( player &p, const tripoint_bub_ms &examp )
{
    const std::vector<npc *> intercom_npcs = g->get_npcs_if( [examp]( const npc & guy ) {
        return guy.myclass == npc_class_id( "NC_ROBOFAC_INTERCOM" ) && rl_dist( guy.bub_pos(), examp ) < 10;
    } );
    if( intercom_npcs.empty() ) {
        p.add_msg_if_player( m_info, _( "No one responds." ) );
    } else {
        intercom_npcs.front()->talk_to_u( false );
    }
}

void iexamine::rubble( player &p, const tripoint_bub_ms &examp )
{
    int moves;
    if( p.has_quality( qual_DIG, 3 ) || p.has_trait( trait_BURROW ) ) {
        moves = to_moves<int>( 1_minutes );
    } else if( p.has_quality( qual_DIG, 2 ) ) {
        moves = to_moves<int>( 2_minutes );
    } else {
        add_msg( m_info, _( "If only you had a shovel…" ) );
        return;
    }
    map &here = get_map();
    if( ( here.veh_at( examp ) || !here.tr_at( examp ).is_null() ||
          g->critter_at( examp ) != nullptr ) &&
        !query_yn( _( "Clear up that %s?" ), here.furnname( examp ) ) ) {
        return;
    }
    p.assign_activity( std::make_unique<player_activity>(
                           std::make_unique<clear_rubble_activity_actor>( bub_to_abs( examp ) ) ) );
    return;
}

void iexamine::chainfence( player &p, const tripoint_bub_ms &examp )
{
    // We're not going to do anything if we're already on that point.
    // Also prompt the player before taking an action.
    if( p.bub_pos() == examp || !query_yn( _( "Climb obstacle?" ) ) ) {
        none( p, examp );
        return;
    }

    map &here = get_map();
    if( here.has_flag( flag_CLIMB_SIMPLE, examp ) &&
        p.mutation_value( "movecost_obstacle_modifier" ) <= 0.5f ) {
        add_msg( _( "You vault over the obstacle with ease." ) );
        p.moves -= 100; // Not tall enough to warrant spider-climbing, so only relevant trait.
    } else if( here.has_flag( flag_CLIMB_SIMPLE, examp ) ) {
        add_msg( _( "You vault over the obstacle." ) );
        p.moves -= 300; // Most common move cost for barricades pre-change.
    } else if( p.has_trait( trait_ARACHNID_ARMS_OK ) &&
               !p.wearing_something_on( bodypart_id( "torso" ) ) ) {
        add_msg( _( "Climbing this obstacle is trivial for one such as you." ) );
        p.moves -= 75; // Yes, faster than walking.  6-8 limbs are impressive.
    } else if( p.has_trait( trait_INSECT_ARMS_OK ) &&
               !p.wearing_something_on( bodypart_id( "torso" ) ) ) {
        add_msg( _( "You quickly scale the fence." ) );
        p.moves -= 90;
    } else if( p.mutation_value( "movecost_obstacle_modifier" ) <= 0.5f ) {
        add_msg( _( "This obstacle is no match for your freerunning abilities." ) );
        p.moves -= 100;
    } else {
        p.moves -= 400;
        ///\EFFECT_DEX decreases chances of slipping while climbing
        int climb = p.dex_cur;
        if( p.has_trait( trait_BADKNEES ) ) {
            climb = climb / 2;
        }
        if( g->slip_down() ) {
            return;
        }
        p.moves += climb * 10;
        sfx::play_variant_sound( "plmove", "clear_obstacle", sfx::get_heard_volume( g->u.bub_pos(),
                                 60 ) );
    }
    if( p.in_vehicle ) {
        here.unboard_vehicle( p.bub_pos() );
    }
    p.setpos( examp );
    if( examp.x() < g_half_mapsize_x || examp.y() < g_half_mapsize_y ||
        examp.x() >= g_half_mapsize_x + SEEX || examp.y() >= g_half_mapsize_y + SEEY ) {
        if( p.is_player() ) {
            g->update_map( p );
        }
    }
}

void iexamine::bars( player &p, const tripoint_bub_ms &examp )
{
    if( !( p.has_trait( trait_AMORPHOUS ) ) ) {
        none( p, examp );
        return;
    }
    map &here = get_map();
    if( ( ( p.encumb( body_part_torso ) ) >= 10 ) && ( ( p.encumb( body_part_head ) ) >= 10 ) &&
        ( p.encumb( body_part_foot_l ) >= 10 ||
          p.encumb( body_part_foot_r ) >=
          10 ) ) { // Most likely places for rigid gear that would catch on the bars.
        add_msg( m_info,
                 _( "Your amorphous body could slip though the %s, but your cumbersome gear can't." ),
                 here.tername( examp ) );
        return;
    }
    if( !query_yn( _( "Slip through the %s?" ), here.tername( examp ) ) ) {
        none( p, examp );
        return;
    }
    p.moves -= to_turns<int>( 2_seconds );
    add_msg( _( "You slide right between the bars." ) );
    p.setpos( examp );
}

void iexamine::deployed_furniture( player &p, const tripoint_bub_ms &pos )
{
    map &here = get_map();
    if( !query_yn( _( "Take down the %s?" ), here.furn( pos ).obj().name() ) ) {
        return;
    }
    p.add_msg_if_player( m_info, _( "You take down the %s." ),
                         here.furn( pos ).obj().name() );
    take_down_deployed_furniture( pos, pos );
}

static std::pair<itype_id, const deploy_tent_actor *> find_tent_itype( const furn_str_id &id )
{
    const itype_id &iid = id->deployed_item;
    if( iid.is_valid() ) {
        const itype &type = *iid;
        for( const auto &pair : type.use_methods ) {
            const auto actor = dynamic_cast<const deploy_tent_actor *>( pair.second.get_actor_ptr() );
            if( !actor ) {
                continue;
            }
            if( ( actor->floor_center && *actor->floor_center == id ) || ( !actor->floor_center &&
                    actor->floor == id ) ) {
                return std::make_pair( iid, actor );
            }
        }
    }
    return std::make_pair( iid, nullptr );
}

void iexamine::portable_structure( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    const furn_str_id fid = here.furn( examp ).id();
    const std::pair<itype_id, const deploy_tent_actor *> tent_item_type = find_tent_itype( fid );
    if( tent_item_type.first.is_null() ) {
        debugmsg( "unknown furniture %s: don't know how to transform it into an item", fid.str() );
        return;
    }

    itype_id dropped = tent_item_type.first;
    std::string name = item::nname( dropped );
    int radius;

    if( tent_item_type.second ) {
        const deploy_tent_actor &actor = *tent_item_type.second;
        if( !actor.check_intact( examp ) ) {
            if( !actor.broken_type ) {
                add_msg( _( "The %s is broken and can not be picked up." ), name );
                none( p, examp );
                return;
            }
            dropped = *actor.broken_type;
            name = string_format( _( "damaged %s" ), name );
        }
        radius = actor.radius;

    } else {
        radius = std::max( 1, fid->bash.collapse_radius );
    }

    if( !query_yn( _( "Take down the %s?" ), name ) ) {
        none( p, examp );
        return;
    }

    p.moves -= to_turns<int>( 2_seconds );
    for( const tripoint_bub_ms &pt : here.points_in_radius( examp, radius ) ) {
        here.furn_set( pt, f_null );
    }

    here.add_item_or_charges( examp, item::spawn( dropped, calendar::turn ) );
}

void iexamine::pit( player &p, const tripoint_bub_ms &examp )
{
    const inventory &crafting_inv = p.crafting_inventory();
    if( !crafting_inv.has_amount( itype_2x4, 1 ) ) {
        none( p, examp );
        return;
    }
    std::vector<item_comp> planks;
    planks.emplace_back( itype_2x4, 1 );

    map &here = get_map();
    if( query_yn( _( "Place a plank over the pit?" ) ) ) {
        p.consume_items( planks, 1, is_crafting_component );
        if( here.ter( examp ) == t_pit ) {
            here.ter_set( examp, t_pit_covered );
        } else if( here.ter( examp ) == t_pit_spiked ) {
            here.ter_set( examp, t_pit_spiked_covered );
        } else if( here.ter( examp ) == t_pit_glass ) {
            here.ter_set( examp, t_pit_glass_covered );
        }
        add_msg( _( "You place a plank of wood over the pit." ) );
        p.mod_moves( -to_turns<int>( 1_seconds ) );
    }
}

void iexamine::pit_covered( player &p, const tripoint_bub_ms &examp )
{
    if( !query_yn( _( "Remove cover?" ) ) ) {
        none( p, examp );
        return;
    }

    map &here = get_map();
    add_msg( _( "You remove the plank." ) );
    here.add_item_or_charges( p.bub_pos(), item::spawn( "2x4", calendar::turn ) );

    if( here.ter( examp ) == t_pit_covered ) {
        here.ter_set( examp, t_pit );
    } else if( here.ter( examp ) == t_pit_spiked_covered ) {
        here.ter_set( examp, t_pit_spiked );
    } else if( here.ter( examp ) == t_pit_glass_covered ) {
        here.ter_set( examp, t_pit_glass );
    }
    p.mod_moves( -to_turns<int>( 1_seconds ) );
}

void iexamine::slot_machine( player &p, const tripoint_bub_ms & )
{
    const int price = 10;
    auto cents = []( int x ) {
        return x * 100;
    };
    bool played = false;
    while( true ) {
        if( p.cash < cents( price ) ) {
            add_msg( m_info, _( "You need $%d to play." ), price );
            break;
        }
        if( !query_yn( played ? _( "Play again for $%d?" ) : _( "Insert $%d?" ), price ) ) {
            break;
        }
        p.cash -= cents( price );
        played = true;
        int won;
        if( one_in( 5 ) ) {
            won = price;
            popup( _( "Three cherries… you get your money back!" ) );
        } else if( one_in( 20 ) ) {
            won = 50;
            popup( _( "Three bells… you win $%d!" ), won );
        } else if( one_in( 50 ) ) {
            won = 200;
            popup( _( "Three stars… you win $%d!" ), won );
        } else if( one_in( 1000 ) ) {
            won = 3000;
            popup( _( "JACKPOT!  You win $%d!" ), won );
        } else {
            won = 0;
            popup( _( "No win." ) );
        }
        p.cash += cents( won );
    }
}

static auto find_best_prying_tool( player &p ) -> item *
{
    auto prying_items = p.items_with( []( const item & it ) {
        // we want to get worn items (eg crowbar in toolbelt), so no check on item position
        return it.type->get_use( "CROWBAR" ) != nullptr;
    } );

    if( p.has_active_bionic( bio_tools ) ) {
        auto toolset = item::spawn_temporary( itype_toolset, calendar::turn );
        prying_items.push_back( toolset );
    }

    if( prying_items.empty() ) {
        return nullptr;
    }

    const auto best_tool = std::ranges::max_element( prying_items, []( const item * a,
    const item * b ) -> bool {
        return a->get_quality( quality_id( "PRY" ) ) < b->get_quality( quality_id( "PRY" ) );
    } );

    return *best_tool;
}

static auto apply_prying_tool( player &p, item *it, const tripoint_bub_ms &examp )
{
    map &here = get_map();

    //~ %1$s: terrain/furniture name, %2$s: prying tool name
    p.add_msg_if_player( _( "You attempt to pry open the %1$s using your %2$s…" ),
                         here.has_furn( examp ) ? here.furnname( examp ) : here.tername( examp ), it->tname() );

    p.invoke_item( it, "CROWBAR", examp );
}

static time_duration safecracking_time( const player &p )
{
    time_duration time = 120_minutes;
    time -= 10_minutes * p.get_skill_level( skill_mechanics );
    if( p.get_per() > 10 ) {
        time -= 5_minutes * ( p.get_per() - 10 );
    }
    // Count Safecracking tools (stethoscopes) as 2 perception
    if( p.has_item_with_flag( flag_SAFECRACK ) ) {
        time -= 10_minutes;
    }
    // Should take longer with poor hearing, hence the negative possibility
    time -= ( 5_minutes * ( p.hearing_ability() - 1 ) );
    return std::max( time, 5_minutes );
}

void iexamine::safe( player &p, const tripoint_bub_ms &examp )
{

    map &here = get_map();
    auto *prying_tool = find_best_prying_tool( p );
    const int target_diff = here.has_furn( examp ) ? here.furn( examp )->pry.pry_quality : here.ter(
                                examp )->pry.pry_quality;
    if( target_diff > 0 && prying_tool && !p.is_crouching() ) {
        // keep going in case we have a prying tool that can't be used against the target, so we can try lockpicking
        if( prying_tool->get_quality( quality_id( "PRY" ) ) >= target_diff ) {
            apply_prying_tool( p, prying_tool, examp );
            return;
        }
    }

    // Requires mutant hearing, Enhanced Hearing CBM, or a stethoscope.
    bool can_safecrack = p.hearing_ability() > 1.5f || p.has_item_with_flag( flag_SAFECRACK );
    bool can_decode = p.get_skill_level( skill_mechanics ) >= 5;

    // We can skip worrying about a stethoscope if we're skilled enough.
    if( !can_decode ) {
        // Lack both the tools and the skills so fiddle with the dial a bit.
        if( !can_safecrack ) {
            p.mod_moves( -to_moves<int>( 10_seconds ) );
            // one_in(30^3) chance of guessing
            if( one_in( 27000 ) ) {
                p.add_msg_if_player( m_good,
                                     _( "Lacking the skill to crack this without tools, you mess with the dial for a little bit… and it opens!" ) );
                get_map().furn_set( examp, f_safe_o );
                return;
            } else {
                p.add_msg_if_player( m_info,
                                     _( "Lacking the skill to crack this without tools, you mess with the dial for a little bit." ) );
                return;
            }
        }
        // We both need and have hearing enhancement, so here we rule out states that prevent us from using it.
        if( p.is_deaf() ) {
            add_msg( m_info, _( "You can't crack a safe while deaf!" ) );
            return;
        } else if( p.has_effect( effect_earphones ) ) {
            add_msg( m_info, _( "You can't crack a safe while listening to music!" ) );
            return;
        }
    }

    if( query_yn(
            _( "Attempt to crack the safe?\n\nUses a stethoscope, augmented hearing, or mechanics skill of 5 or higher." ) ) ) {
        std::string safecracking_message = can_decode ?
                                           _( "You begin to expertly decode the safe." ) :
                                           _( "You start cracking the safe." );
        add_msg( m_info, safecracking_message );
        // 120 minutes - 10 minutes per mechanics point, - 5 per perception point above 10;
        // capped at 5 minutes minimum.
        const time_duration time = safecracking_time( p );

        auto act = std::make_unique<player_activity>( std::make_unique<cracking_activity_actor>( bub_to_abs(
                       examp ) ) );
        act->moves_left = to_moves<int>( time );
        p.assign_activity( std::move( act ) );
    }
}

void iexamine::gunsafe_el( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    auto *prying_tool = find_best_prying_tool( p );
    const int target_diff = here.has_furn( examp ) ? here.furn( examp )->pry.pry_quality : here.ter(
                                examp )->pry.pry_quality;
    if( target_diff > 0 && prying_tool && !p.is_crouching() ) {
        // keep going in case we have a prying tool that can't be used against the target, so we can try lockpicking
        if( prying_tool->get_quality( quality_id( "PRY" ) ) >= target_diff ) {
            apply_prying_tool( p, prying_tool, examp );
            return;
        }
    }
    if( query_yn( _( "Attempt to hack this safe?" ) ) ) {
        try_start_hacking( p, examp );
    }
}

static item *find_best_lock_picking_tool( player &p )
{
    std::vector<item *> picklocks = p.items_with( []( const item & it ) {
        // we want to get worn items (eg hairpin), so no check on item position
        return it.type->get_use( "PICK_LOCK" ) != nullptr;
    } );

    // Sort by their picklock level.
    std::ranges::sort( picklocks, [&]( const item * a, const item * b ) {
        return a->get_quality( qual_LOCKPICK ) > b->get_quality( qual_LOCKPICK );
    } );

    if( picklocks.empty() ) {
        return nullptr;
    }

    return picklocks[0];
}

static void apply_lock_picking_tool( player &p, item *it, const tripoint_bub_ms &examp )
{
    map &here = get_map();

    const use_function *iuse_fn = it->type->get_use( "PICK_LOCK" );
    std::string target;
    if( here.has_furn( examp ) ) {
        target = here.furnname( examp );
    } else if( here.veh_at( examp ) ) {
        target = here.veh_at( examp )->vehicle().name;
    } else {
        target = here.tername( examp );
    }
    p.add_msg_if_player(
        _( "You attempt to pick lock of %1$s using your %2$s…" ), target, it->tname() );
    const ret_val<bool> can_use = iuse_fn->can_call( p, *it, false, examp );
    if( can_use.success() ) {
        p.invoke_item( it, "PICK_LOCK", examp );
        return;
    } else {
        p.add_msg_if_player( m_bad, can_use.str() );
    }
}

static bool pick_lock( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();

    if( p.has_bionic( bio_lockpick ) ) {
        if( p.get_power_level() >= bio_lockpick->power_activate ) {
            p.mod_power_level( -bio_lockpick->power_activate );
            p.add_msg_if_player( m_info, _( "You activate your %s." ), bio_lockpick->name );
            p.assign_activity( std::make_unique<player_activity>( lockpick_activity_actor::use_bionic(
                                   item::spawn( bio_lockpick->fake_item ), here.bub_to_abs( examp ) ) ) );
            return true;
        } else {
            p.add_msg_if_player( m_info, _( "You don't have enough power to activate your %s." ),
                                 bio_lockpick->name );
            return false;
        }
    }

    safe_reference<item> lock_picking_tool = find_best_lock_picking_tool( p );
    if( lock_picking_tool ) {
        apply_lock_picking_tool( p, lock_picking_tool.get(), examp );
        return true;
    }

    return false;
}

void iexamine::locked_object( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();

    // if the furniture/terrain is also lockpickable
    // try lockpicking first if we're crouched
    if( lockpick_activity_actor::is_pickable( examp ) && p.is_crouching() ) {
        if( pick_lock( p, examp ) ) {
            return;
        }
    }

    auto *prying_tool = find_best_prying_tool( p );
    if( prying_tool ) {
        const int target_diff = here.has_furn( examp ) ? here.furn( examp )->pry.pry_quality : here.ter(
                                    examp )->pry.pry_quality;
        // keep going in case we have a prying tool that can't be used against the target, so we can try lockpicking
        if( prying_tool->get_quality( quality_id( "PRY" ) ) >= target_diff ) {
            apply_prying_tool( p, prying_tool, examp );
            return;
        }
    }

    std::string target;
    if( here.has_furn( examp ) ) {
        target = here.furnname( examp );
    } else if( here.veh_at( examp ) ) {
        target = here.veh_at( examp )->vehicle().name;
    } else {
        target = here.tername( examp );
    }

    if( lockpick_activity_actor::is_pickable( examp ) ) {
        if( !pick_lock( p, examp ) ) {
            if( prying_tool ) {
                add_msg( m_info,
                         _( "The %s is locked.  If only you had something to pick its lock, or a stronger prying tool…" ),
                         target );
            } else {
                add_msg( m_info, _( "The %s is locked.  If only you had something to pry it or pick its lock…" ),
                         target );
            }
        }
        return;
    }

    if( prying_tool ) {
        add_msg( m_info, _( "The %s is locked, and your tools aren't strong enough to pry it open…" ),
                 target );
    } else {
        add_msg( m_info, _( "The %s is locked.  If only you had something to pry it…" ),
                 target );
    }
}

void iexamine::locked_object_pickable( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();

    std::string target;
    if( here.has_furn( examp ) ) {
        target = here.furnname( examp );
    } else if( here.veh_at( examp ) ) {
        target = here.veh_at( examp )->vehicle().name;
    } else {
        target = here.tername( examp );
    }

    if( !pick_lock( p, examp ) ) {
        add_msg( m_info, _( "The %s is locked.  If only you had something to pick its lock…" ),
                 target );
    }
}

void iexamine::fault( player &, const tripoint_bub_ms & )
{
    popup( _( "This wall is perfectly vertical.  Odd, twisted holes are set in it, leading\n"
              "as far back into the solid rock as you can see.  The holes are humanoid in\n"
              "shape, but with long, twisted, distended limbs." ) );
}

void iexamine::notify( player &, const tripoint_bub_ms &pos )
{
    std::string message = g->m.has_furn( pos ) ?
                          g->m.furn( pos ).obj().message :
                          g->m.ter( pos ).obj().message;
    if( !message.empty() ) {
        popup( _( message ) );
    }
}

void iexamine::transform( player &p, const tripoint_bub_ms &pos )
{
    std::string message;
    std::string prompt;
    const bool has_lootable_items = !g->m.i_at( pos ).empty();
    const bool furn_is_deployed = !g->m.furn( pos ).obj().deployed_item.is_empty();
    const bool can_climb = g->m.has_flag( flag_CLIMBABLE, pos ) ||
                           g->m.has_flag( flag_CLIMB_SIMPLE, pos );

    if( g->m.has_furn( pos ) ) {
        message = g->m.furn( pos ).obj().message;
        prompt = g->m.furn( pos ).obj().prompt;
    } else {
        message = g->m.ter( pos ).obj().message;
        prompt = g->m.ter( pos ).obj().prompt;
    }

    if( has_lootable_items || furn_is_deployed || can_climb ) {

        uilist selection_menu;
        selection_menu.text = _( "Select an action" );
        if( has_lootable_items ) {
            selection_menu.addentry( 0, true, 'g', _( "Get items" ) );
        }
        selection_menu.addentry( 1, true, 't', !prompt.empty() ? _( prompt ) : _( "Transform furniture" ) );
        if( furn_is_deployed ) {
            selection_menu.addentry( 2, true, 'T', _( "Take down the %s" ), g->m.furnname( pos ) );
        }
        if( can_climb ) {
            selection_menu.addentry( 3, true, 'c', _( "Climb %s" ), g->m.furnname( pos ) );
        }
        selection_menu.query();

        switch( selection_menu.ret ) {
            case 0:
                none( p, pos );
                pickup::pick_up( pos, 0 );
                return;
            case 1: {
                if( !message.empty() ) {
                    add_msg( _( message ) );
                }
                if( g->m.has_furn( pos ) ) {
                    g->m.furn_set( pos, g->m.get_furn_transforms_into( pos ) );
                } else {
                    g->m.ter_set( pos, g->m.get_ter_transforms_into( pos ) );
                }
                p.moves -= to_moves<int>( 2_seconds );
                return;
            }
            case 2: {
                add_msg( m_info, _( "You take down the %s." ),
                         g->m.furnname( pos ) );
                take_down_deployed_furniture( pos, pos );
                return;
            }
            case 3: {
                iexamine::chainfence( p, pos );
                return;
            }
            default:
                none( p, pos );
                return;
        }
    } else {
        if( !message.empty() ) {
            add_msg( _( message ) );
        }
        if( g->m.has_furn( pos ) ) {
            g->m.furn_set( pos, g->m.get_furn_transforms_into( pos ) );
        } else {
            g->m.ter_set( pos, g->m.get_ter_transforms_into( pos ) );
        }
        p.moves -= to_moves<int>( 2_seconds );
    }
}

void iexamine::pedestal_wyrm( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    map_stack items = here.i_at( examp );
    if( !items.empty() ) {
        if( items.only_item().typeId() == itype_petrified_eye &&
            query_yn( _( "Remove the petrified eye from the pedestal?" ) ) ) {
            here.i_clear( examp );

            p.i_add_or_drop( item::spawn( itype_petrified_eye ) );

            // Send in a few wyrms to start things off.
            get_event_bus().send<event_type::awakes_dark_wyrms>();
            for( const tripoint_bub_ms &p : here.points_on_zlevel() ) {
                if( here.ter( p ) == ter_id( "t_orifice" ) ) {
                    g->place_critter_around( mon_dark_wyrm, p, 1 );
                }
            }

            sound_event se;
            se.origin = examp;
            se.volume = 100;
            se.category = sounds::sound_t::combat;
            se.description = _( "an ominous grinding noise…" );
            se.id = "misc";
            se.variant = "stones_grinding";
            sounds::sound( se );
            add_msg( _( "The pedestal sinks into the ground…" ) );
            here.ter_set( examp, t_rock_floor );
            g->timed_events.add( TIMED_EVENT_SPAWN_WYRMS, calendar::turn + rng( 30_seconds, 60_seconds ) );
        } else {
            none( p, examp );
            add_msg( _( "You decided to leave the petrified eye on the pedestal…" ) );
            return;
        }
    }
}

void iexamine::pedestal_temple( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    map_stack items = here.i_at( examp );
    if( !items.empty() && items.only_item().typeId() == itype_petrified_eye ) {
        add_msg( _( "The pedestal sinks into the ground…" ) );
        here.ter_set( examp, t_dirt );
        here.i_clear( examp );
        g->timed_events.add( TIMED_EVENT_TEMPLE_OPEN, calendar::turn + 10_seconds );
    } else if( p.has_amount( itype_petrified_eye, 1 ) &&
               query_yn( _( "Place your petrified eye on the pedestal?" ) ) ) {
        p.use_amount( itype_petrified_eye, 1 );
        add_msg( _( "The pedestal sinks into the ground…" ) );
        here.ter_set( examp, t_dirt );
        g->timed_events.add( TIMED_EVENT_TEMPLE_OPEN, calendar::turn + 10_seconds );
    } else {
        add_msg( _( "This pedestal is engraved in eye-shaped diagrams, and has a "
                    "large semi-spherical indentation at the top." ) );
    }
}

void iexamine::door_peephole( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    if( here.is_outside( p.bub_pos() ) ) {
        // if door is a locked type attempt to open
        if( here.has_flag( flag_OPENCLOSE_INSIDE, examp ) ) {
            locked_object( p, examp );
        } else {
            p.add_msg_if_player( _( "You cannot look through the peephole from the outside." ) );
        }

        return;
    }

    if( here.can_open_door( &p, examp, true ) ) {
        g->peek( examp );
        p.add_msg_if_player( _( "You peek through the peephole." ) );
    } else {
        // Peek through the peephole, or open the door.
        const int choice = uilist( _( "Do what with the door?" ), {
            _( "Peek through peephole." ),
            _( "Open door." )
        } );
        if( choice == 0 ) {
            // Peek
            g->peek( examp );
            p.add_msg_if_player( _( "You peek through the peephole." ) );
        } else if( choice == 1 ) {
            here.open_door( &p, examp, true );
            p.add_msg_if_player( _( "You open the door." ) );
        } else {
            p.add_msg_if_player( _( "Never mind." ) );
        }
    }
}

void iexamine::fswitch( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    if( !query_yn( _( "Flip the %s?" ), here.tername( examp ) ) ) {
        none( p, examp );
        return;
    }
    ter_id terid = here.ter( examp );
    p.moves -= to_moves<int>( 1_seconds );
    tripoint_bub_ms tmp;
    tmp.z() = examp.z();
    for( tmp.y() = examp.y(); tmp.y() <= examp.y() + 5; tmp.y()++ ) {
        for( tmp.x() = 0; tmp.x() < g_mapsize_x; tmp.x()++ ) {
            if( terid == t_switch_rg ) {
                if( here.ter( tmp ) == t_rock_red ) {
                    here.ter_set( tmp, t_floor_red );
                } else if( here.ter( tmp ) == t_floor_red ) {
                    here.ter_set( tmp, t_rock_red );
                } else if( here.ter( tmp ) == t_rock_green ) {
                    here.ter_set( tmp, t_floor_green );
                } else if( here.ter( tmp ) == t_floor_green ) {
                    here.ter_set( tmp, t_rock_green );
                }
            } else if( terid == t_switch_gb ) {
                if( here.ter( tmp ) == t_rock_blue ) {
                    here.ter_set( tmp, t_floor_blue );
                } else if( here.ter( tmp ) == t_floor_blue ) {
                    here.ter_set( tmp, t_rock_blue );
                } else if( here.ter( tmp ) == t_rock_green ) {
                    here.ter_set( tmp, t_floor_green );
                } else if( here.ter( tmp ) == t_floor_green ) {
                    here.ter_set( tmp, t_rock_green );
                }
            } else if( terid == t_switch_rb ) {
                if( here.ter( tmp ) == t_rock_blue ) {
                    here.ter_set( tmp, t_floor_blue );
                } else if( here.ter( tmp ) == t_floor_blue ) {
                    here.ter_set( tmp, t_rock_blue );
                } else if( here.ter( tmp ) == t_rock_red ) {
                    here.ter_set( tmp, t_floor_red );
                } else if( here.ter( tmp ) == t_floor_red ) {
                    here.ter_set( tmp, t_rock_red );
                }
            } else if( terid == t_switch_even ) {
                if( ( tmp.y() - examp.y() ) % 2 == 1 ) {
                    if( here.ter( tmp ) == t_rock_red ) {
                        here.ter_set( tmp, t_floor_red );
                    } else if( here.ter( tmp ) == t_floor_red ) {
                        here.ter_set( tmp, t_rock_red );
                    } else if( here.ter( tmp ) == t_rock_green ) {
                        here.ter_set( tmp, t_floor_green );
                    } else if( here.ter( tmp ) == t_floor_green ) {
                        here.ter_set( tmp, t_rock_green );
                    } else if( here.ter( tmp ) == t_rock_blue ) {
                        here.ter_set( tmp, t_floor_blue );
                    } else if( here.ter( tmp ) == t_floor_blue ) {
                        here.ter_set( tmp, t_rock_blue );
                    }
                }
            }
        }
    }
    add_msg( m_warning, _( "You hear the rumble of rock shifting." ) );
    g->timed_events.add( TIMED_EVENT_TEMPLE_SPAWN, calendar::turn + 3_turns );
}

auto iexamine::fluid_grid_fixture( player &p, const tripoint_bub_ms &examp ) -> void
{
    map &here = get_map();
    const auto has_lootable_items = !here.i_at( examp ).empty();
    if( has_lootable_items ) {
        uilist selection_menu;
        selection_menu.text = _( "Select an action" );
        selection_menu.addentry( 0, true, 'g', _( "Get items" ) );
        selection_menu.addentry( 1, true, 'w', _( "Use fixture" ) );
        selection_menu.query();
        if( selection_menu.ret == 0 ) {
            none( p, examp );
            pickup::pick_up( examp, 0 );
            return;
        }
        if( selection_menu.ret != 1 ) {
            return;
        }
    }

    const auto &furn = here.furn( examp ).obj();
    if( !furn.fluid_grid || furn.fluid_grid->role != fluid_grid_role::fixture ) {
        add_msg( m_info, _( "It is not connected to a fluid grid fixture." ) );
        return;
    }
    const auto &fluid_grid = *furn.fluid_grid;
    const auto fixture_name = here.name( examp );
    if( !fluid_grid.allow_output ) {
        add_msg( m_info, _( "The %s has no usable output." ), fixture_name );
        return;
    }

    const auto pos_abs_ms = here.bub_to_abs( examp );
    const auto pos_abs_omt = project_to<coords::omt>( pos_abs_ms );

    const auto available_liquid = std::ranges::find_if( fluid_grid.allowed_liquids,
    [&]( const itype_id & liquid ) {
        return fluid_grid::liquid_charges_at( pos_abs_omt, liquid ) > 0;
    } );
    if( available_liquid == fluid_grid.allowed_liquids.end() ) {
        add_msg( m_info, _( "The %s is dry." ), fixture_name );
        return;
    }

    const auto &liquid_type = *available_liquid;
    const auto available = fluid_grid::liquid_charges_at( pos_abs_omt, liquid_type );
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
    liquid_handler::handle_liquid( *water_item );
    auto used = 0;
    const auto &item_ptrs = items.as_vector();
    const auto still_here = std::ranges::find( item_ptrs, water_item ) != item_ptrs.end();
    if( still_here ) {
        used = before - water_item->charges;
        items.remove( water_item );
    } else {
        used = before;
    }
    if( used <= 0 ) {
        return;
    }

    fluid_grid::drain_liquid_charges( pos_abs_omt, liquid_type, used );
}

static int count_charges_in_list( const itype *type, const map_stack &items )
{
    for( const auto &candidate : items ) {
        if( candidate->type == type ) {
            return candidate->charges;
        }
    }
    return 0;
}

void iexamine::reload_furniture( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    const furn_t &f = here.furn( examp ).obj();
    const std::vector<itype> ammo_types = f.crafting_ammo_item_types();

    // No fake item, no ammo, something is wrong here.
    if( ammo_types.empty() ) {
        debugmsg( ( "The %s has no crafting_pseudo_items or no defined ammo!" ), f.name() );
        return;
    }

    map_stack items_here = here.i_at( examp );
    std::vector<std::string> ammo_names;
    std::vector<itype> ammo_filtered;
    int ammo_index = 0;

    for( const itype &at : ammo_types ) {
        if( at.get_id() != itype_battery ) {
            ammo_names.emplace_back( at.nname( 1 ) );
            ammo_filtered.emplace_back( at );
        }
    }

    if( ammo_filtered.empty() ) {
        debugmsg( "Furniture %s has no valid ammo to reload!", f.name() );
        return;
    } else if( ammo_names.size() > 1 ) {
        ammo_index = uilist( _( "What would you like to change?" ), ammo_names );
        if( ammo_index < 0 || static_cast<size_t>( ammo_index ) >= ammo_names.size() ) {
            ammo_index = -1;
        }
    }
    if( ammo_index < 0 ) {
        return;
    }

    // HACK: Yes I know I'm converting from itype to itype_id and back again into a pointer.
    // It seems to be needed to make the appropriate connections?
    // Otherwise the part where it queries for the amount doesn't seem to work right.
    const itype *cur_ammo = &*ammo_types.at( ammo_index ).get_id();
    const int amount_in_furn = count_charges_in_list( cur_ammo, items_here );
    const int amount_in_inv = p.charges_of( cur_ammo->get_id() );
    if( amount_in_furn > 0 ) {
        if( p.query_yn( _( "The %1$s contains %2$d %3$s.  Unload?" ), f.name(), amount_in_furn,
                        cur_ammo->nname( amount_in_furn ) ) ) {
            auto items = here.i_at( examp );
            for( auto &itm : items ) {
                if( itm->type == cur_ammo ) {
                    g->u.assign_activity( std::make_unique<player_activity>( std::make_unique<pickup_activity_actor>(
                    std::vector<pickup::pick_drop_selection> { { itm, std::nullopt, {} } }, g->u.bub_pos() ) ) );
                    return;
                }
            }
        }
    }

    // Note that this can lead to hammerspace as each ammo type doesn't take other items on the tile into account.
    const int max_amount_in_furn = cur_ammo->charges_per_volume( f.max_volume );
    const int max_reload_amount = max_amount_in_furn - amount_in_furn;
    if( max_reload_amount <= 0 ) {
        return;
    }

    // Check for charcoal to load around the rack as if crafting
    int amount_nearby = 0;
    if( cur_ammo->get_id() == itype_charcoal ) {
        for( const tripoint_bub_ms &pt : here.points_in_radius( examp, PICKUP_RANGE ) ) {
            if( pt == examp ) {
                continue;
            }
            for( const item *it : here.i_at( pt ) ) {
                if( it->typeId() == itype_charcoal ) {
                    amount_nearby += it->charges;
                }
            }
        }
    }

    const int total_available = amount_in_inv + amount_nearby;
    if( total_available == 0 ) {
        //~ Reloading or restocking a piece of furniture, for example a forge.
        add_msg( m_info, _( "You need some %1$s to reload this %2$s." ),
                 cur_ammo->nname( 2 ),
                 f.name() );
        return;
    }
    const int max_amount = std::min( total_available, max_reload_amount );

    // For smoking racks, use total available as default (no artificial limits)
    int default_amount = max_amount;
    int actual_max = max_amount;

    //~ Loading fuel or other items into a piece of furniture.
    std::string source_desc = "";
    if( amount_in_inv > 0 && amount_nearby > 0 ) {
        source_desc = string_format( _( " (%d in inventory, %d nearby)" ), amount_in_inv, amount_nearby );
    } else if( amount_nearby > 0 ) {
        source_desc = string_format( _( " (%d nearby)" ), amount_nearby );
    }

    const std::string popupmsg = string_format( _( "Put how much %1$s into the %2$s?%3$s" ),
                                 cur_ammo->nname( actual_max ), f.name(), source_desc );
    int amount = string_input_popup()
                 .title( popupmsg )
                 .width( 20 )
                 .text( std::to_string( default_amount ) )
                 .only_digits( true )
                 .query_int();
    if( amount <= 0 ) {
        return;
    }

    // Prevent putting in too much coal
    if( amount > actual_max ) {
        amount = actual_max;
    }

    // First use from inventory,
    int remaining = amount;
    if( amount_in_inv > 0 ) {
        const int from_inv = std::min( remaining, amount_in_inv );
        p.use_charges( cur_ammo->get_id(), from_inv );
        remaining -= from_inv;
    }

    // Then use from nearby ground
    if( remaining > 0 && amount_nearby > 0 ) {
        for( const tripoint_bub_ms &pt : here.points_in_radius( examp, PICKUP_RANGE ) ) {
            if( pt == examp || remaining <= 0 ) {
                continue;
            }
            auto ground_items = here.i_at( pt );
            for( auto iter = ground_items.begin(); iter != ground_items.end() && remaining > 0; ) {
                item *it = *iter;
                if( it->typeId() == itype_charcoal ) {
                    const int to_take = std::min( remaining, it->charges );
                    if( to_take >= it->charges ) {
                        detached_ptr<item> det;
                        iter = ground_items.erase( iter, &det );
                        remaining -= to_take;
                    } else {
                        it->charges -= to_take;
                        remaining -= to_take;
                        ++iter;
                    }
                } else {
                    ++iter;
                }
            }
        }
    }

    auto items = here.i_at( examp );
    for( auto &itm : items ) {
        if( itm->type == cur_ammo ) {
            itm->charges += amount;
            amount = 0;
            break;
        }
    }
    if( amount != 0 ) {
        here.add_item( examp, item::spawn( cur_ammo->get_id(), calendar::turn, amount ) );
    }

    const int amount_in_furn_after_placing = count_charges_in_list( &ammo_types.at( ammo_index ),
        items );
    //~ %1$s - furniture, %2$d - number, %3$s items.
    add_msg( _( "The %1$s contains %2$d %3$s." ), f.name(), amount_in_furn_after_placing,
             cur_ammo->nname( amount_in_furn_after_placing ) );

    add_msg( _( "You reload the %s." ), here.furnname( examp ) );
    p.moves -= to_moves<int>( 5_seconds );
}

void iexamine::use_furn_fake_item( player &p, const tripoint_bub_ms &examp )
{
    map &m = get_map();
    const tripoint_abs_ms abspos( m.bub_to_abs( examp ) );

    if( !m.has_furn( examp ) ) {
        debugmsg( "lost furniture at %s", examp.to_string() );
        return;
    }
    const furn_t &furniture = m.furn( examp ).obj();
    const std::vector<itype> item_type_list = furniture.crafting_pseudo_item_types();
    std::vector<itype> usable_item_types;
    std::vector<std::string> usable_item_names;
    if( item_type_list.empty() ) {
        debugmsg(
            "Furniture ( %s ) with use_furn_fake_item does not have fake item to use.",
            furniture.id.c_str()
        );
        return;
    }
    for( const itype &itt : item_type_list ) {
        if( !itt.has_use() ) {
            continue;
        }
        usable_item_types.push_back( itt );
        usable_item_names.push_back( itt.nname( 1 ) );
    }
    if( usable_item_types.empty() ) {
        debugmsg(
            "Furniture ( %s ) with use_furn_fake_item has no fake item with uses",
            furniture.id.c_str() );
        return;
    }

    int tool_index = 0;
    if( usable_item_types.size() > 1 ) {
        tool_index = uilist( _( "Which tool do you want to use?" ), usable_item_names );
        if( tool_index < 0 || static_cast<size_t>( tool_index ) >= usable_item_types.size() ) {
            tool_index = -1;
        }
    }
    if( tool_index < 0 ) {
        return;
    }

    const itype &cur_tool = usable_item_types.at( tool_index );
    item &fake_item = *item::spawn_temporary( cur_tool.get_id(), calendar::turn, 0 );
    const itype_id ammo = fake_item.ammo_default();
    fake_item.set_flag( flag_PSEUDO );

    enum class charges_type {
        grid, ammo_from_map, none
    };

    charges_type charge_type = charges_type::none;
    fake_item.charges = 0;

    if( fake_item.has_flag( flag_USES_GRID_POWER ) ) {
        const distribution_grid &grid = get_distribution_grid_tracker().grid_at( abspos );
        fake_item.charges = grid.get_resource();
        charge_type = charges_type::grid;
    } else if( ammo != itype_id::NULL_ID() ) {
        fake_item.charges = count_charges_in_list( &*ammo, m.i_at( examp ) );
        charge_type = charges_type::ammo_from_map;
    }

    const int original_charges = fake_item.charges;
    p.invoke_item( &fake_item, examp );

    if( auto *actor = g->u.activity->get_actor<repair_item_activity_actor>() ) {
        actor->set_hack_furniture( abspos, cur_tool.get_id() );
    } else if( auto *ts_actor = g->u.activity->get_actor<train_skill_activity_actor>() ) {
        ts_actor->set_hack_furniture( abspos, cur_tool.get_id() );
    }

    const int discharged_ammo = original_charges - fake_item.charges;

    if( discharged_ammo == 0 ) {
        return;
    }

    switch( charge_type ) {
        case charges_type::grid: {
            distribution_grid &grid = get_distribution_grid_tracker().grid_at( abspos );
            const int remainder = grid.mod_resource( -discharged_ammo );
            if( remainder != 0 ) {
                debugmsg( "Fake item %s discharged more charges than have in grid at %s.",
                          cur_tool.get_id().c_str(), abspos.to_string() );
            }
            return;
        }
        case charges_type::ammo_from_map: {
            int by_ref = discharged_ammo;
            m.use_charges( examp, 0, ammo->get_id(), by_ref );
            if( by_ref != 0 ) {
                debugmsg( "Discharged fake item %s more ammo than %s has at %s.",
                          cur_tool.get_id().c_str(),
                          furniture.id.c_str(),
                          examp.to_string()
                        );
            }
            return;
        }
        case charges_type::none:
            debugmsg( "Somehow changed charges of fake item %s without ammo type to %d.",
                      cur_tool.get_id().c_str(),
                      fake_item.charges
                    );
            return;
    }
}

void iexamine::curtains( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    const bool closed_window_with_curtains = here.has_flag( flag_BARRICADABLE_WINDOW_CURTAINS, examp );
    if( here.is_outside( p.bub_pos() ) && ( here.has_flag( flag_WALL, examp ) ||
                                            closed_window_with_curtains ) ) {
        locked_object( p, examp );
        return;
    }

    const ter_id ter = here.ter( examp );

    // Peek through the curtains, or tear them down.
    uilist window_menu;
    window_menu.text = _( "Do what with the curtains?" );
    window_menu.addentry( 0, ( !ter.obj().close &&
                               closed_window_with_curtains ), 'p', _( "Peek through the closed curtains." ) );
    window_menu.addentry( 1, true, 't', _( "Tear down the curtains." ) );
    window_menu.query();
    const int choice = window_menu.ret;
    if( choice == 0 ) {
        // Peek
        g->peek( examp );
        p.add_msg_if_player( _( "You carefully peek through the curtains." ) );
    } else if( choice == 1 ) {
        // Mr. Gorbachev, tear down those curtains!
        if( here.ter( examp )->has_curtains() ) {
            here.ter_set( examp, here.ter( examp )->curtain_transform );
        }

        here.spawn_item( p.bub_pos(), itype_nail, 1, 4, calendar::turn );
        here.spawn_item( p.bub_pos(), itype_sheet, 2, 0, calendar::turn );
        here.spawn_item( p.bub_pos(), itype_stick, 1, 0, calendar::turn );
        here.spawn_item( p.bub_pos(), itype_string_36, 1, 0, calendar::turn );
        p.moves -= to_moves<int>( 10_seconds );
        p.add_msg_if_player( _( "You tear the curtains and curtain rod off the windowframe." ) );
    } else {
        p.add_msg_if_player( _( "Never mind." ) );
    }
}

void iexamine::sign( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    std::string existing_signage = here.get_signage( examp );
    bool previous_signage_exists = !existing_signage.empty();

    // Display existing message, or lack thereof.
    if( p.has_trait( trait_ILLITERATE ) ) {
        popup( _( "You're illiterate, and can't read the message on the sign." ) );
    } else if( previous_signage_exists ) {
        popup( existing_signage.c_str() );
    } else {
        p.add_msg_if_player( m_neutral, _( "Nothing legible on the sign." ) );
    }

    // Allow chance to modify message.
    std::vector<tool_comp> tools;
    std::vector<item *> filter = p.crafting_inventory().items_with( []( const item & it ) {
        return it.has_flag( flag_WRITE_MESSAGE ) && it.charges > 0;
    } );
    tools.reserve( filter.size() );
    for( const item *writing_item : filter ) {
        tools.emplace_back( writing_item->typeId(), 1 );
    }

    if( !tools.empty() ) {
        // Different messages if the sign already has writing associated with it.
        std::string query_message = previous_signage_exists ?
                                    _( "Overwrite the existing message on the sign?" ) :
                                    _( "Add a message to the sign?" );
        std::string ignore_message = _( "You leave the sign alone." );
        if( query_yn( query_message ) ) {
            std::string signage = string_input_popup()
                                  .title( _( "Write what?" ) )
                                  .identifier( "signage" )
                                  .query_string();
            if( signage.empty() ) {
                p.add_msg_if_player( m_neutral, ignore_message );
            } else {
                std::string spray_painted_message = previous_signage_exists ?
                                                    _( "You overwrite the previous message on the sign with your graffiti." ) :
                                                    _( "You graffiti a message onto the sign." );
                here.set_signage( examp, signage );
                p.add_msg_if_player( m_info, spray_painted_message );
                p.mod_moves( - 20 * signage.length() );
                p.consume_tools( tools, 1 );
            }
        } else {
            p.add_msg_if_player( m_neutral, ignore_message );
        }
    }
}

static int getNearPumpCount( const tripoint_bub_ms &p )
{
    int result = 0;
    map &here = get_map();
    for( const tripoint_bub_ms &tmp : here.points_in_radius( p, 12 ) ) {
        const auto t = here.ter( tmp );
        if( t == ter_str_id( "t_gas_pump" ) || t == ter_str_id( "t_gas_pump_a" ) ) {
            result++;
        }
    }
    return result;
}

std::optional<tripoint_bub_ms> iexamine::getNearFilledGasTank( const tripoint_bub_ms &center,
        int &gas_units )
{
    map &here = get_map();
    std::optional<tripoint_bub_ms> tank_loc;
    int distance = INT_MAX;
    gas_units = 0;

    for( const tripoint_bub_ms &tmp : here.points_in_radius( center, SEEX * 2 ) ) {
        if( here.ter( tmp ) != ter_str_id( "t_gas_tank" ) ) {
            continue;
        }

        const int new_distance = rl_dist( center, tmp );

        if( new_distance >= distance ) {
            continue;
        }
        if( !tank_loc ) {
            // Return a potentially empty tank, but only if we don't find a closer full one.
            tank_loc.emplace( tmp );
        }
        for( auto &k : here.i_at( tmp ) ) {
            if( k->made_of( LIQUID ) ) {
                distance = new_distance;
                tank_loc.emplace( tmp );
                gas_units = k->charges;
                break;
            }
        }
    }
    return tank_loc;
}

static int getGasDiscountCardQuality( const item &it )
{
    for( const flag_id &tag : it.type->get_flags() ) {
        int discount_value;
        if( sscanf( tag->id.c_str(), "DISCOUNT_VALUE_%i", &discount_value ) == 1 ) {
            return discount_value;
        }
    }
    return 0;
}

static int findBestGasDiscount( player &p )
{
    int discount = 0;

    for( size_t i = 0; i < p.inv_size(); i++ ) {
        item &it = p.inv_find_item( i );

        if( it.has_flag( flag_GAS_DISCOUNT ) ) {

            int q = getGasDiscountCardQuality( it );
            if( q > discount ) {
                discount = q;
            }
        }
    }

    return discount;
}

static std::string str_to_illiterate_str( std::string s )
{
    if( !g->u.has_trait( trait_ILLITERATE ) ) {
        return s;
    } else {
        for( auto &i : s ) {
            i = i + rng( 0, 5 ) - rng( 0, 5 );
            if( i < ' ' ) {
                // some control character, most likely not handled correctly be the print functions
                i = ' ';
            } else if( i == '%' ) {
                // avoid characters that trigger formatting in the various print functions
                i++;
            }
        }
        return s;
    }
}

static std::string getGasDiscountName( int discount )
{
    if( discount == 3 ) {
        return str_to_illiterate_str( _( "Platinum member" ) );
    } else if( discount == 2 ) {
        return str_to_illiterate_str( _( "Gold member" ) );
    } else if( discount == 1 ) {
        return str_to_illiterate_str( _( "Silver member" ) );
    } else {
        return str_to_illiterate_str( _( "Beloved customer" ) );
    }
}

static int getGasPricePerLiter( int discount )
{
    // Those prices are in cents
    static const int prices[4] = { 1400, 1320, 1200, 1000 };
    if( discount < 0 || discount > 3 ) {
        return prices[0];
    } else {
        return prices[discount];
    }
}

std::optional<tripoint_bub_ms> iexamine::getGasPumpByNumber( const tripoint_bub_ms &p, int number )
{
    map &here = get_map();
    int k = 0;
    for( const auto &tmp : here.points_in_radius( p, 12 ) ) {
        const auto t = here.ter( tmp );
        if( ( t == ter_str_id( "t_gas_pump" ) || t == ter_str_id( "t_gas_pump_a" ) ) && number == k++ ) {
            return tmp;
        }
    }
    return std::nullopt;
}

bool iexamine::toPumpFuel( const tripoint_bub_ms &src, const tripoint_bub_ms &dst, int units )
{
    map &here = get_map();
    auto items = here.i_at( src );
    for( auto item_it = items.begin(); item_it != items.end(); ++item_it ) {
        item *content = *item_it;
        if( content->made_of( LIQUID ) ) {
            if( content->charges < units ) {
                return false;
            }

            content->charges -= units;

            const auto backup_pump = here.ter( dst );
            here.ter_set( dst, ter_str_id::NULL_ID() );
            here.add_item_or_charges( dst, item::spawn( content->type, calendar::turn, units ) );
            here.ter_set( dst, backup_pump );

            if( content->charges < 1 ) {
                items.erase( item_it );
            }

            return true;
        }
    }

    return false;
}

static int fromPumpFuel( const tripoint_bub_ms &dst, const tripoint_bub_ms &src )
{
    map &here = get_map();
    auto items = here.i_at( src );
    for( auto item_it = items.begin(); item_it != items.end(); ++item_it ) {
        item *content = *item_it;
        if( content->made_of( LIQUID ) ) {
            // how much do we have in the pump?

            // add the charges to the destination
            const auto backup_tank = here.ter( dst );
            here.ter_set( dst, ter_str_id::NULL_ID() );
            here.add_item_or_charges( dst, item::spawn( content->type, calendar::turn, content->charges ) );
            here.ter_set( dst, backup_tank );

            // remove the liquid from the pump
            int amount = content->charges;
            items.erase( item_it );
            return amount;
        }
    }
    return -1;
}

static void turnOnSelectedPump( const tripoint_bub_ms &p, int number )
{
    map &here = get_map();
    int k = 0;
    for( const tripoint_bub_ms &tmp : here.points_in_radius( p, 12 ) ) {
        const auto t = here.ter( tmp );
        if( t == ter_str_id( "t_gas_pump" ) || t == ter_str_id( "t_gas_pump_a" ) ) {
            if( number == k++ ) {
                here.ter_set( tmp, ter_str_id( "t_gas_pump_a" ) );
            } else {
                here.ter_set( tmp, ter_str_id( "t_gas_pump" ) );
            }
        }
    }
}

void iexamine::pay_gas( player &p, const tripoint_bub_ms &examp )
{

    int choice = -1;
    const int buy_gas = 1;
    const int choose_pump = 2;
    const int hack = 3;
    const int refund = 4;

    if( p.has_trait( trait_ILLITERATE ) ) {
        popup( _( "You're illiterate, and can't read the screen." ) );
    }

    int pumpCount = getNearPumpCount( examp );
    if( pumpCount == 0 ) {
        popup( str_to_illiterate_str( _( "Failure!  No gas pumps found!" ) ) );
        return;
    }

    int tankGasUnits;
    const std::optional<tripoint_bub_ms> pTank_ = getNearFilledGasTank( examp, tankGasUnits );
    if( !pTank_ ) {
        popup( str_to_illiterate_str( _( "Failure!  No gas tank found!" ) ) );
        return;
    }
    const auto pTank = *pTank_;

    if( tankGasUnits == 0 ) {
        popup( str_to_illiterate_str(
                   _( "This station is out of fuel.  We apologize for the inconvenience." ) ) );
        return;
    }

    if( uistate.ags_pay_gas_selected_pump + 1 > pumpCount ) {
        uistate.ags_pay_gas_selected_pump = 0;
    }

    int discount = findBestGasDiscount( p );
    std::string discountName = getGasDiscountName( discount );

    int pricePerUnit = getGasPricePerLiter( discount );

    bool can_hack = ( !p.has_trait( trait_ILLITERATE ) &&
                      ( ( p.has_charges( itype_electrohack, 25 ) ) ||
                        ( p.has_bionic( bio_fingerhack ) && p.get_power_level() > 24_kJ ) ) );

    uilist amenu;
    amenu.selected = 1;
    amenu.text = str_to_illiterate_str( _( "Welcome to AutoGas!" ) );
    amenu.addentry( 0, false, -1, str_to_illiterate_str( _( "What would you like to do?" ) ) );

    amenu.addentry( buy_gas, true, 'b', str_to_illiterate_str( _( "Buy gas." ) ) );
    amenu.addentry( refund, true, 'r', str_to_illiterate_str( _( "Refund cash." ) ) );

    std::string gaspumpselected = str_to_illiterate_str( _( "Current gas pump: " ) ) +
                                  std::to_string( uistate.ags_pay_gas_selected_pump + 1 );
    amenu.addentry( 0, false, -1, gaspumpselected );
    amenu.addentry( choose_pump, true, 'p', str_to_illiterate_str( _( "Choose a gas pump." ) ) );

    amenu.addentry( 0, false, -1, str_to_illiterate_str( _( "Your discount: " ) ) + discountName );
    amenu.addentry( 0, false, -1, str_to_illiterate_str( _( "Your price per gasoline unit: " ) ) +
                    format_money( pricePerUnit ) );

    if( can_hack ) {
        amenu.addentry( hack, true, 'h', _( "Hack console." ) );
    }

    amenu.query();
    choice = amenu.ret;

    if( choose_pump == choice ) {
        uilist amenu;
        amenu.selected = uistate.ags_pay_gas_selected_pump;
        amenu.text = str_to_illiterate_str( _( "Please choose gas pump:" ) );

        for( int i = 0; i < pumpCount; i++ ) {
            amenu.addentry( i, true, -1,
                            str_to_illiterate_str( _( "Pump " ) ) + std::to_string( i + 1 ) );
        }
        amenu.query();
        choice = amenu.ret;

        if( choice < 0 ) {
            return;
        }

        uistate.ags_pay_gas_selected_pump = choice;

        turnOnSelectedPump( examp, uistate.ags_pay_gas_selected_pump );

        return;

    }

    if( buy_gas == choice ) {
        int money = p.charges_of( itype_cash_card );

        if( money < pricePerUnit ) {
            popup( str_to_illiterate_str(
                       _( "Not enough money, please refill your cash card." ) ) ); //or ride on a solar car, ha ha ha
            return;
        }

        int maximum_liters = std::min( money / pricePerUnit, tankGasUnits / 1000 );

        std::string popupmsg = string_format(
                                   _( "How many liters of gasoline to buy?  Max: %d L.  (0 to cancel)" ), maximum_liters );
        int liters = string_input_popup()
                     .title( popupmsg )
                     .width( 20 )
                     .text( std::to_string( maximum_liters ) )
                     .only_digits( true )
                     .query_int();
        if( liters <= 0 ) {
            return;
        }
        if( liters > maximum_liters ) {
            liters = maximum_liters;
        }

        const std::optional<tripoint_bub_ms> pGasPump = getGasPumpByNumber( examp,
            uistate.ags_pay_gas_selected_pump );
        if( !pGasPump || !toPumpFuel( pTank, *pGasPump, liters * 1000 ) ) {
            return;
        }

        sound_event se;
        se.origin = p.bub_pos();
        se.volume = 50;
        se.category = sounds::sound_t::activity;
        se.description = _( "Glug Glug Glug" );
        se.id = "tool";
        se.variant = "gaspump";
        sounds::sound( se );

        int cost = liters * pricePerUnit;
        money -= cost;
        p.use_charges( itype_cash_card, cost );

        add_msg( m_info, _( "Your cash cards now hold %s." ), format_money( money ) );
        p.moves -= to_moves<int>( 5_seconds );
        return;
    }

    if( hack == choice ) {
        try_start_hacking( p, examp );
    }

    if( refund == choice ) {
        const int pos = p.inv_position_by_type( itype_id( "cash_card" ) );

        if( pos == INT_MIN ) {
            add_msg( _( "Never mind." ) );
            return;
        }

        item *cashcard = &( p.i_at( pos ) );
        // Okay, we have a cash card. Now we need to know what's left in the pump.
        const std::optional<tripoint_bub_ms> pGasPump = getGasPumpByNumber( examp,
            uistate.ags_pay_gas_selected_pump );
        int amount = pGasPump ? fromPumpFuel( pTank, *pGasPump ) : 0;
        if( amount >= 0 ) {
            sound_event se;
            se.origin = p.bub_pos();
            se.volume = 50;
            se.category = sounds::sound_t::activity;
            se.description = _( "Glug Glug Glug" );
            se.id = "tool";
            se.variant = "gaspump";
            sounds::sound( se );
            cashcard->charges += amount * pricePerUnit / 1000.0f;
            add_msg( m_info, _( "Your cash cards now hold %s." ),
                     format_money( p.charges_of( itype_cash_card ) ) );
            p.moves -= to_moves<int>( 5_seconds );
            return;
        } else {
            popup( _( "Unable to refund, no fuel in pump." ) );
            return;
        }
    }
}

void iexamine::ledge( player &p, const tripoint_bub_ms &examp )
{
    enum ledge_action : int { jump_over, climb_down, pull_up_rope, spin_web_bridge };
    if( p.in_vehicle ) {
        if( !character_funcs::can_fly( p ) &&
            !query_yn( _( "Do you really want to jump off the vehicle?" ) ) ) {
            return;
        }
        get_map().unboard_vehicle( p.bub_pos() );
    }
    if( get_map().ter( p.bub_pos() ).id().str() == "t_open_air" && !character_funcs::can_fly( p ) ) {
        auto where = p.bub_pos();
        auto below = where;
        below.z()--;

        // Keep going down until we find a tile that is NOT open air
        while( get_map().ter( below ).id().str() == "t_open_air" &&
               get_map().valid_move( where, below, false, true ) ) {
            where.z()--;
            below.z()--;
        }
        // where now represents the first NON-open-air tile or the last valid move before hitting one
        const int height = p.bub_pos().z() - below.z();

        if( height > 0 ) {
            g->vertical_move( -height, true );  // fall onto the solid tile
            return;
        }
    }

    uilist cmenu;
    cmenu.text = _( "There is a ledge here.  What do you want to do?" );
    cmenu.addentry( ledge_action::jump_over, true, 'j', _( "Jump over." ) );
    cmenu.addentry( ledge_action::climb_down, true, 'c', _( "Climb down." ) );
    //if the tile below has a grappling hook, you can pull it up
    auto below_rope = examp;
    below_rope.z()--;
    if( get_map().has_flag_furn( "REMOVE_FROM_ABOVE", below_rope ) ) {
        cmenu.addentry( ledge_action::pull_up_rope, true, 'r', _( "Pull up the %s." ),
                        get_map().furn( below_rope ).obj().name() );
    }
    if( p.has_trait( trait_WEB_BRIDGE ) ) {
        cmenu.addentry( ledge_action::spin_web_bridge, true, 'w', _( "Spin Web Bridge." ) );
    }

    cmenu.query();

    map &here = get_map();
    switch( cmenu.ret ) {
        case ledge_action::jump_over: {
            tripoint_bub_ms dest( p.bub_pos().x() + 2 * sgn( examp.x() - p.bub_pos().x() ),
                                  p.bub_pos().y() + 2 * sgn( examp.y() - p.bub_pos().y() ),
                                  p.bub_pos().z() );
            if( p.get_str() < 4 ) {
                add_msg( m_warning, _( "You are too weak to jump over an obstacle." ) );
            } else if( 100 * p.weight_carried() / p.weight_capacity() > 25 ) {
                add_msg( m_warning, _( "You are too burdened to jump over an obstacle." ) );
            } else if( !here.valid_move( examp, dest, false, true ) ) {
                add_msg( m_warning, _( "You cannot jump over an obstacle - something is blocking the way." ) );
            } else if( g->critter_at( dest ) ) {
                add_msg( m_warning, _( "You cannot jump over an obstacle - there is %s blocking the way." ),
                         g->critter_at( dest )->disp_name() );
            } else if( here.ter( dest ).obj().trap == tr_ledge ) {
                add_msg( m_warning, _( "You are not going to jump over an obstacle only to fall down." ) );
            } else {
                add_msg( m_info, _( "You jump over an obstacle." ) );
                p.setpos( dest );
            }
            break;
        }
        case ledge_action::climb_down: {
            auto where = examp;
            auto below = examp;
            below.z()--;
            while( here.valid_move( where, below, false, true ) ) {
                where.z()--;
                below.z()--;
            }

            const int height = examp.z() - where.z();
            if( height == 0 ) {
                p.add_msg_if_player( _( "You can't climb down there." ) );
                return;
            }

            const bool has_grapnel = p.has_amount( itype_grapnel, 1 );
            const auto climb_cost = map_funcs::climbing_cost( here, where, examp );
            const auto fall_mod = p.fall_damage_mod();
            const std::string query_str = vgettext( "Looks like %d story.  Jump down?",
                                                    "Looks like %d stories.  Jump down?",
                                                    height );

            if( height > 1 && !query_yn( query_str.c_str(), height ) ) {
                return;
            } else if( height == 1 ) {
                enum class climb_result {
                    one_way_dangerous, one_way_unclimbable,
                    both_way_safe, both_way_grapnel, both_way_hard_to_climb,
                };
                const auto get_climb_result = [&]() {
                    if( has_grapnel ) {
                        return climb_result::both_way_grapnel;
                    }
                    if( climb_cost.has_value() ) {
                        return climb_cost.value() < 200
                               ? climb_result::both_way_safe
                               : climb_result::both_way_hard_to_climb;
                    } else {
                        return fall_mod > 0.8
                               ? climb_result::one_way_dangerous
                               : climb_result::one_way_unclimbable;
                    }
                };
                const auto get_message = []( climb_result res ) {
                    switch( res ) {
                        case climb_result::both_way_safe:
                            return _( "You climb down." );
                        case climb_result::both_way_grapnel:
                            return _( "You tie the rope around your waist and begin to climb down." );
                        case climb_result::both_way_hard_to_climb:
                            return _( "You climb down but feel that it won't be easy to climb back up." );
                        case climb_result::one_way_dangerous:
                            return _( "You probably won't be able to get up and jumping down may hurt.  Jump?" );
                        case climb_result::one_way_unclimbable:
                            return _( "You probably won't be able to get back up.  Climb down?" );
                    }
                    cata::unreachable();
                };
                add_msg( m_debug, "climb_cost: %d", climb_cost.value_or( -1 ) );
                const auto result = get_climb_result();
                const auto message = get_message( result );
                switch( result ) {
                    case climb_result::both_way_safe:
                    case climb_result::both_way_grapnel:
                    case climb_result::both_way_hard_to_climb:
                        p.add_msg_if_player( message );
                        break;
                    case climb_result::one_way_dangerous:
                    case climb_result::one_way_unclimbable:
                    default:
                        if( !query_yn( message ) ) {
                            return;
                        }
                }
            }

            p.moves -= to_moves<int>( 1_seconds + 1_seconds * fall_mod );
            p.setpos( examp );

            if( climb_cost > 0 || rng_float( 0.8, 1.0 ) > fall_mod ) {
                // One tile of falling less (possibly zero)
                g->vertical_move( -1, true );
            }
            here.creature_on_trap( p );
            break;
        }
        case ledge_action::pull_up_rope: {
            map &here = get_map();
            p.add_msg_if_player( m_info, _( "You pull up the %s." ),
                                 here.furn( below_rope ).obj().name() );
            take_down_deployed_furniture( below_rope, p.bub_pos() );
            break;
        }
        case ledge_action::spin_web_bridge: {

            if( !can_use_mutation_warn( trait_WEB_BRIDGE, p ) ) {
                break;
            }
            const int range = 6; //this means we could web across a gap of 5.
            int success_range = 0;
            bool success = false;
            for( int i = 2; i <= range; i++ ) {
                //break at the first non empty space encountered
                if( g->m.ter( tripoint_bub_ms( p.bub_pos().x() + i * sgn( examp.x() - p.bub_pos().x() ),
                                               p.bub_pos().y() + i * sgn( examp.y() - p.bub_pos().y() ), p.bub_pos().z() ) ) != t_open_air ) {
                    success_range = i;
                    success = true;
                    break;
                }
            }
            if( !success ) {
                p.add_msg_if_player( _( "There is nothing for your to attach your web to!" ) );
            } else {
                for( int i = 1; i < success_range; i++ ) {
                    tripoint_bub_ms dest( p.bub_pos().x() + i * sgn( examp.x() - p.bub_pos().x() ),
                                          p.bub_pos().y() + i * sgn( examp.y() - p.bub_pos().y() ),
                                          p.bub_pos().z() );

                    g->m.ter_set( dest, t_web_bridge );
                }
                p.mutation_spend_resources( trait_WEB_BRIDGE );
            }
            break;
        }
        default:
            p.add_msg_if_player( _( "You decided to step back from the ledge." ) );
            break;
    }
}

void iexamine::open_safe( player &, const tripoint_bub_ms &examp )
{
    add_msg( m_info, _( "You open the unlocked safe." ) );
    get_map().furn_set( examp, f_safe_o );
}

void iexamine::workbench( player &, const tripoint_bub_ms & )
{
    // Dummied out and only used for function equality check
}

void iexamine::dimensional_portal( player &p, const tripoint_bub_ms &examp )
{
    uilist menu;
    menu.text = _( "What to do with the portal:" );
    menu.desc_enabled = true;

    std::vector<item *> nukes = p.all_items_with_flag( flag_CLOSES_PORTAL );
    menu.addentry_desc( 0, !nukes.empty(), 'e', _( "Close from here" ),
                        _( "Requires a nuclear explosive" ) );
    menu.addentry_desc( 1, true, 'Q', _( "Sacrifice yourself" ),
                        _( "This will kill you, but close the portal" ) );
    menu.query();

    switch( menu.ret ) {
        case 0: {
            item *the_nuke = game_menus::inv::titled_filter_menu( []( const item & it ) {
                return it.has_flag( flag_CLOSES_PORTAL );
            }, static_cast<avatar &>( p ), _( "What to use to close the portal?" ) );
            if( !the_nuke ) {
                add_msg( m_info, _( "Never mind." ) );
                break;
            }

            add_msg( m_good, _( "You throw the armed %s into the portal!" ), the_nuke->tname() );
            the_nuke->detach();
            g->m.translate_radius( t_dimensional_portal, t_thconc_floor, 5, examp, true );
            g->win();
            break;
        }

        case 1:
            p.set_all_parts_hp_cur( 0 );
            g->m.translate_radius( t_dimensional_portal, t_thconc_floor, 5, examp, true );
            g->win();
            break;
        default:
            add_msg( m_info, _( "Never mind." ) );
    }
}

void iexamine::check_power( player &, const tripoint_bub_ms &examp )
{
    tripoint_abs_ms abspos( g->m.bub_to_abs( examp ) );
    battery_tile *battery = active_tiles::furn_at<battery_tile>( abspos );
    if( battery != nullptr ) {
        add_msg( m_info, _( "This battery stores %d kJ of electric power." ), battery->get_resource() );
    }
    int amt = get_distribution_grid_tracker().grid_at( abspos ).get_resource();
    add_msg( m_info, _( "This electric grid stores %d kJ of electric power." ), amt );
}

void iexamine::power_portal( player &p, const tripoint_bub_ms &examp )
{
    const tripoint_abs_ms abs_pos( g->m.bub_to_abs( examp ) );
    const std::string local_dim = g->m.get_bound_dimension();

    // Look up the grid_link_tile for this portal.  Access through the correct
    // mapbuffer so this works regardless of which dimension the player is in.
    tripoint_abs_sm sm_abs;
    point_sm_ms sm_pt;
    std::tie( sm_abs, sm_pt ) = project_remain<coords::sm>( abs_pos );
    submap *sm = MAPBUFFER_REGISTRY.get( local_dim ).lookup_submap( sm_abs );
    if( sm == nullptr ) {
        add_msg( m_bad, _( "The portal is in an unloaded submap." ) );
        return;
    }
    const auto it = sm->active_furniture.find( sm_pt );
    if( it == sm->active_furniture.end() ) {
        add_msg( m_bad, _( "This portal has no active tile data." ) );
        return;
    }
    grid_link_tile *glt = dynamic_cast<grid_link_tile *>( it->second.get() );
    if( glt == nullptr ) {
        add_msg( m_bad, _( "This doesn't seem to be a functioning power portal." ) );
        return;
    }

    // Build status line for the menu.
    std::string status;
    if( !glt->linked ) {
        status = _( "Status: Unlinked" );
    } else if( glt->paused ) {
        status = string_format(
                     _( "Status: PAUSED — insufficient power\nTarget: [%s] (%d,%d,%d)" ),
                     glt->target_dim_id.empty() ? _( "primary" ) : glt->target_dim_id,
                     glt->target_pos.raw().x, glt->target_pos.raw().y, glt->target_pos.raw().z );
    } else {
        status = string_format(
                     _( "Status: Active\nTarget: [%s] (%d,%d,%d)" ),
                     glt->target_dim_id.empty() ? _( "primary" ) : glt->target_dim_id,
                     glt->target_pos.raw().x, glt->target_pos.raw().y, glt->target_pos.raw().z );
    }



    // Find power-portal keycards in the player's inventory.
    static const itype_id itype_portal_key( "power_portal_key" );
    item *keycard = nullptr;
    p.visit_items( [&]( item * candidate ) {
        if( keycard == nullptr && candidate->typeId() == itype_portal_key ) {
            keycard = candidate;
        }
        return VisitResponse::NEXT;
    } );
    const bool has_keycard = keycard != nullptr;
    const bool key_has_attunement = has_keycard && keycard->has_var( "portal_target_dim" );

    uilist menu;
    menu.text = status;
    menu.desc_enabled = true;
    menu.addentry_desc( 0, has_keycard, 'a',
                        _( "Attune keycard to this portal" ),
                        _( "Stores this portal's location in the keycard.  Overwrites any existing attunement." ) );
    menu.addentry_desc( 1, has_keycard && key_has_attunement && !glt->linked, 'l',
                        _( "Link portal using keycard" ),
                        _( "Establishes a power bridge to the portal stored in the keycard." ) );
    menu.addentry_desc( 2, glt->linked && glt->paused, 'r',
                        _( "Resume link" ),
                        _( "Restarts power transfer.  Both sides need enough power to pay upkeep." ) );
    menu.addentry_desc( 3, glt->linked && !glt->paused, 'p',
                        _( "Pause link" ),
                        _( "Suspends power transfer without severing the connection." ) );
    menu.addentry_desc( 4, glt->linked, 'u',
                        _( "Unlink portal" ),
                        _( "Severs the connection permanently.  Both portals revert to unlinked." ) );
    menu.query();

    distribution_grid_tracker &local_tracker = get_distribution_grid_tracker();

    switch( menu.ret ) {
        case 0: { // Attune keycard
            keycard->set_var( "portal_target_dim", local_dim );
            keycard->set_var( "portal_target_pos", abs_pos );
            add_msg( m_info, _( "You attune the keycard to this power portal." ) );
            break;
        }
        case 1: { // Link using keycard
            const std::string target_dim = keycard->get_var( "portal_target_dim", std::string{} );
            const auto target_pos = keycard->get_var( "portal_target_pos", tripoint_abs_ms::zero() );
            if( target_pos == abs_pos && target_dim == local_dim ) {
                add_msg( m_bad, _( "You can't link a portal to itself." ) );
                break;
            }
            // Update local tile.
            glt->linked        = true;
            glt->paused        = false;
            glt->target_dim_id = target_dim;
            glt->target_pos    = target_pos;
            // Register the local export node (also requests load for far end).
            cross_dimension_export_node node;
            node.source_pos    = abs_pos;
            node.target_dim_id = target_dim;
            node.target_pos    = target_pos;
            local_tracker.add_export_node( std::move( node ) );
            // add_export_node() now auto-registers the reverse node on the
            // remote tracker (creating the tracker if needed).  We still need
            // to update the remote grid_link_tile so that it serialises
            // correctly and on_submap_loaded picks it up on future loads.
            {
                tripoint_abs_sm rem_sm_abs;
                point_sm_ms rem_sm_pt;
                std::tie( rem_sm_abs, rem_sm_pt ) = project_remain<coords::sm>( target_pos );
                auto &remote_mb = MAPBUFFER_REGISTRY.get( target_dim );
                submap *rem_sm = remote_mb.lookup_submap( rem_sm_abs );
                if( rem_sm != nullptr ) {
                    const auto rem_it = rem_sm->active_furniture.find( rem_sm_pt );
                    if( rem_it != rem_sm->active_furniture.end() ) {
                        grid_link_tile *rglt = dynamic_cast<grid_link_tile *>( rem_it->second.get() );
                        if( rglt != nullptr ) {
                            rglt->linked        = true;
                            rglt->paused        = false;
                            rglt->target_dim_id = local_dim;
                            rglt->target_pos    = abs_pos;
                        }
                    }
                }
            }
            add_msg( m_info, _( "Power link established." ) );
            break;
        }
        case 2: { // Resume link
            glt->paused = false;
            local_tracker.resume_export_node( abs_pos );
            distribution_grid_tracker *remote_tracker = get_distribution_grid_tracker_for( glt->target_dim_id );
            if( remote_tracker != nullptr ) {
                remote_tracker->resume_export_node( glt->target_pos );
            }
            add_msg( m_info, _( "Power link resumed." ) );
            break;
        }
        case 3: { // Pause link
            glt->paused = true;
            local_tracker.pause_export_node( abs_pos );
            distribution_grid_tracker *remote_tracker = get_distribution_grid_tracker_for( glt->target_dim_id );
            if( remote_tracker != nullptr ) {
                remote_tracker->pause_export_node( glt->target_pos );
            }
            add_msg( m_info, _( "Power link paused." ) );
            break;
        }
        case 4: { // Unlink
            const tripoint_abs_ms old_target_pos    = glt->target_pos;
            const std::string     old_target_dim_id = glt->target_dim_id;
            // Sever local side first.
            local_tracker.remove_export_node( abs_pos );
            glt->linked = false;
            glt->paused = false;
            glt->target_dim_id.clear();
            // Always update the remote grid_link_tile (submap may be resident
            // via load handles even if the remote tracker was destroyed).
            {
                tripoint_abs_sm rem_sm_abs;
                point_sm_ms rem_sm_pt;
                std::tie( rem_sm_abs, rem_sm_pt ) = project_remain<coords::sm>( old_target_pos );
                submap *rem_sm = MAPBUFFER_REGISTRY.get( old_target_dim_id ).lookup_submap( rem_sm_abs );
                if( rem_sm != nullptr ) {
                    const auto rem_it = rem_sm->active_furniture.find( rem_sm_pt );
                    if( rem_it != rem_sm->active_furniture.end() ) {
                        grid_link_tile *rglt = dynamic_cast<grid_link_tile *>( rem_it->second.get() );
                        if( rglt != nullptr ) {
                            rglt->linked = false;
                            rglt->paused = false;
                            rglt->target_dim_id.clear();
                        }
                    }
                }
            }
            // Remove remote export node if the tracker exists.
            distribution_grid_tracker *remote_tracker = get_distribution_grid_tracker_for( old_target_dim_id );
            if( remote_tracker != nullptr ) {
                remote_tracker->remove_export_node( old_target_pos );
            }
            add_msg( m_info, _( "Power link severed." ) );
            break;
        }
        default:
            break;
    }
}

void iexamine::portal( player &p, const tripoint_bub_ms &examp )
{
    const tripoint_abs_ms abs_pos( get_map().bub_to_abs( examp ) );

    portal_tile *pt = active_tiles::furn_at<portal_tile>( abs_pos );
    if( pt == nullptr ) {
        add_msg( m_info, _( "This portal doesn't appear to be active." ) );
        return;
    }

    // Dynamic generation: first use generates the destination special.
    if( !pt->linked && !pt->dynamic_special.is_null() && pt->dynamic_special.is_valid() ) {
        if( !query_yn( _( "The portal shimmers.  Step through to an unknown destination?" ) ) ) {
            return;
        }
        // Generate the dynamic special in the target dimension at a random overmap location.
        const std::string &tdim = pt->target_dim_id;
        auto &omb = get_overmapbuffer( tdim );
        // Pick an origin far enough from the player so the generated area doesn't overlap.
        const tripoint_abs_omt gen_origin( rng( 50, 100 ), rng( 50, 100 ), 0 );
        overmap &om = *omb.get_om_global( gen_origin ).om;
        const tripoint_om_omt local_pos = omb.get_om_global( gen_origin ).local;
        om.place_special_forced( pt->dynamic_special, local_pos, om_direction::type::north );
        // The portal in the generated special sets back-link to this portal's abs_pos.
        pt->target_pos = project_to<coords::ms>( gen_origin );
        pt->linked = true;
        add_msg( m_info, _( "The portal stabilizes." ) );
    }

    if( !pt->linked ) {
        add_msg( m_info, _( "This portal has no configured destination." ) );
        return;
    }

    // Bionic tap linking opportunity.
    if( pt->allow_bionic_tap && p.has_bionic( bionic_id( "bio_portal_tap" ) ) ) {
        if( !p.bio_portal_tap_linked &&
            query_yn( _( "Link your Dimensional Portal Tap bionic to this portal?" ) ) ) {
            p.bio_portal_tap_dim_id = pt->target_dim_id;
            p.bio_portal_tap_pos = pt->target_pos;
            p.bio_portal_tap_linked = true;
            add_msg( m_good, _( "Bionic linked." ) );
            return;
        }
    }

    // Vehicle portal tap linking opportunity.
    if( pt->allow_bionic_tap ) {
        static const std::string flag_portal_tap( "POWER_DRAW_LINKED_PORTAL" );
        const optional_vpart_position vp = get_map().veh_at( examp );
        if( !vp ) {
            // Also check if player is inside a nearby vehicle.
            for( const tripoint_bub_ms &adj : get_map().points_in_radius( p.bub_pos(), 1 ) ) {
                const optional_vpart_position vp2 = get_map().veh_at( adj );
                if( vp2 ) {
                    vehicle &veh = vp2->vehicle();
                    for( int i = 0; i < veh.part_count(); ++i ) {
                        vehicle_part &vpart = veh.part( i );
                        if( !vpart.portal_tap_linked &&
                            vpart.info().has_flag( flag_portal_tap ) &&
                            query_yn( _( "Link vehicle's Dimensional Portal Tap to this portal?" ) ) ) {
                            vpart.portal_tap_linked = true;
                            vpart.portal_tap_dim_id = pt->target_dim_id;
                            vpart.portal_tap_pos = pt->target_pos;
                            add_msg( m_good, _( "Vehicle part linked." ) );
                            return;
                        }
                    }
                }
            }
        }
    }

    if( pt->one_way ) {
        add_msg( m_info, _( "The portal shimmers — it appears to be one-way." ) );
    }

    if( !query_yn( _( "Step through the portal?" ) ) ) {
        return;
    }

    p.add_msg_if_player( m_good, _( "You step through the portal." ) );

    // Resolve destination world_type.
    auto wt_id = world_type_id( pt->target_dim_id );
    if( pt->target_dim_id.empty() ) {
        wt_id = world_types::get_default();
    }

    const auto dest_sm = project_to<coords::sm>( pt->target_pos ) -
                         tripoint_rel_sm( g_half_mapsize, g_half_mapsize, 0 );

    g->travel_to_dimension( pt->target_dim_id, wt_id, std::nullopt, dest_sm );

    auto entry_local = get_map().abs_to_bub( pt->target_pos );
    p.setpos( entry_local );
    g->update_map( p );
}

void iexamine::migo_nerve_cluster( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    if( query_yn( _( "This looks important.  Tear open nerve cluster?" ) ) ) {
        p.mod_moves( -200 );
        add_msg( _( "You grab hold of a sinewy tendril and wrench it loose!" ) );
        map_funcs::migo_nerve_cage_removal( here, examp, false );
        here.furn_set( examp, furn_id( "f_alien_scar" ) );
    }
}

void iexamine::cardreader_plutgen( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    itype_id card_type = itype_id_military;
    if( p.has_amount( card_type, 1 ) && query_yn( _( "Swipe your ID card?" ) ) ) {
        // The duration taken may need modification.
        p.mod_moves( -100 );
        p.use_amount( card_type, 1 );
        add_msg( _( "You insert your ID card." ) );
        add_msg( m_good,
                 _( "The plutonium generator beeps twice, then disengages from the surrounding conduits with a series of mechanical clunks." ) );
        here.ter_set( examp, t_concrete );
        here.add_item_or_charges( examp, item::spawn( itype_plut_generator_item, calendar::turn ) );
    } else {
        add_msg( _( "The plutonium generator has significant security measures in place. Without the necessary ID, you'll have to remove it by hand." ) );
    }
}

void iexamine::multicooker( player &p, const tripoint_bub_ms &pos )
{
    map &here = get_map();
    const furn_id furniture = here.furn( pos );
    data_vars::data_set *vars = here.furn_vars( pos );
    const tripoint_abs_ms abspos( here.bub_to_abs( pos ) );
    auto grid = get_distribution_grid_tracker().grid_at( abspos );
    int battery = grid.get_resource();
    enum {
        mc_start, mc_stop, mc_take, mc_upgrade
    };

    uilist menu;
    menu.text = _( "Choose option:" );

    if( vars->get( "ACTIVE", false ) ) {
        if( vars->contains( "STARTTIME" ) &&
            vars->get( "STARTTIME", 0 ) + vars->get( "COOKTIME", 0 ) > to_turn<int>( calendar::turn ) ) {
            menu.addentry( mc_stop, true, 's', _( "Stop crafting" ) );
        } else {
            menu.addentry( mc_take, true, 't', _( "Remove Product" ) );
        }
    } else {
        if( battery < vars->get( "CHARGE_START", 0 ) ) {
            p.add_msg_if_player( _( "Batteries are low." ) );
            return;
        }
        menu.addentry( mc_start, true, 's', _( "Start crafting " ) );
    }

    menu.query();
    int choice = menu.ret;

    if( choice < 0 ) {
        return;
    }

    if( mc_stop == choice ) {
        if( query_yn( _( "Really stop?" ) ) ) {
            vars->erase( "RESULT" );
            vars->erase( "ACTIVE" );
            vars->erase( "STARTTIME" );
            vars->erase( "COOKTIME" );
            vars->erase( "BATCHCOUNT" );
            vars->erase( "RECIPE" );
        }
        return;
    }

    if( mc_take == choice ) {

        detached_ptr<item> dish = item::spawn( vars->get( "RESULT" ), calendar::turn,
                                               vars->get( "BATCHCOUNT", 1 ) );

        const std::string dish_name = dish->tname( dish->charges, false );
        if( dish->made_of( LIQUID ) ) {
            if( !p.check_eligible_containers_for_crafting( *recipe_id( vars->get( "RECIPE" ) ), 1 ) ) {
                p.add_msg_if_player( m_info, _( "You don't have a suitable container to store your %s." ),
                                     dish_name );
                return;
            }
            liquid_handler::handle_all_liquid( std::move( dish ), PICKUP_RANGE );
        } else {
            p.i_add( std::move( dish ) );
        }

        grid.mod_resource( vars->get( "COOKTIME", 0 ) * vars->get( "CRAFTSPEEDMULT",
                           1.0 ) / 6000 * vars->get( "CHARGE_PER_MIN", 0.0 ) + vars->get( "CHARGE_START", 0.0 ) );
        vars->erase( "RESULT" );
        vars->erase( "ACTIVE" );
        vars->erase( "STARTTIME" );
        vars->erase( "COOKTIME" );
        vars->erase( "BATCHCOUNT" );
        vars->erase( "RECIPE" );
        p.add_msg_if_player( m_good, _( "You got the %s from the %s." ),
                             dish_name, furniture->name() );

        return;
    }

    if( mc_start == choice ) {
        uilist dmenu;
        dmenu.text = _( "Choose desired recipe:" );

        std::vector<const recipe *> dishes;

        inventory crafting_inv = g->u.crafting_inventory();

        for( itype item : furniture->crafting_pseudo_item_types() ) {
            crafting_inv.add_item( *item::spawn_temporary( item.get_id(), calendar::start_of_cataclysm ),
                                   false );
        }
        crafting_inv.update_quality_cache();

        int counter = 0;

        for( const auto &r : g->u.get_learned_recipes() ) {
            if( vars->get( "CATEGORYIDS", std::set<std::string>() ).contains( r->subcategory ) ||
                vars->get( "RECIPEIDS", std::set<std::string>() ).contains( r->result().str() ) ) {
                dishes.push_back( r );
                const bool can_make = r->deduped_requirements().can_make_with_inventory(
                                          crafting_inv, r->get_component_filter() );
                dmenu.addentry( counter++, can_make, -1, string_format( _( "%s (%1.f charges)" ), r->result_name(),
                                r->time * vars->get( "CRAFTSPEEDMULT", 1.0 ) / 6000 * vars->get( "CHARGE_PER_MIN",
                                        0.0 ) + vars->get( "CHARGE_START", 0.0 ) ) );
            }
        }

        dmenu.query();

        int choice = dmenu.ret;

        if( choice < 0 ) {

            if( choice == -1024 ) {
                p.add_msg_if_player( m_warning,
                                     _( "You don't know of anything you could craft with this." ) );
            }

            return;
        } else {
            const recipe *meal = dishes[choice];

            uilist batchmenu;
            batchmenu.text = _( "Choose batch count:" );
            int counter = 0;

            for( int i = 1; i < 51; i++ ) {
                const bool can_make = meal->deduped_requirements().can_make_with_inventory(
                                          crafting_inv, meal->get_component_filter(), i );
                batchmenu.addentry( counter++, can_make, -1, string_format( _( "%s batches (%1.f charges)" ), i,
                                    meal->batch_time( i, 1, 0 ) * vars->get( "CRAFTSPEEDMULT",
                                            1.0 ) / 6000 * vars->get( "CHARGE_PER_MIN", 0.0 ) + vars->get( "CHARGE_START", 0.0 ) ) );
            }

            batchmenu.query();

            int batchcount = batchmenu.ret;

            if( batchcount < 0 ) {
                return;
            }
            batchcount++;

            // Seems to be divided by 100;
            // See the CHARGE_PER_MIN calc being 6000 instead of 60
            int mealtime = meal->batch_time( batchcount, 1, 0 ) * vars->get( "CRAFTSPEEDMULT", 1.0 ) / 100;
            int all_charges = mealtime / 6000 * vars->get( "CHARGE_PER_MIN", 0.0 ) + vars->get( "CHARGE_START",
                              0.0 );

            if( battery < all_charges ) {

                p.add_msg_if_player( m_warning,
                                     _( "The %s needs %d charges to create this." ),
                                     furniture->name(), all_charges );
                return;
            }

            const auto filter = is_crafting_component;
            const requirement_data *reqs =
                meal->deduped_requirements().select_alternative( p, crafting_inv, filter, batchcount );
            if( !reqs ) {
                return;
            }

            for( const auto &component : reqs->get_components() ) {
                p.consume_items( component, batchcount, filter );
            }

            vars->set( "ACTIVE", true );
            vars->set( "RECIPE", meal->ident().str() );
            vars->set( "RESULT", meal->result().str() );
            vars->set( "STARTTIME", to_turn<int>( calendar::turn ) );
            vars->set( "COOKTIME", mealtime );
            vars->set( "BATCHCOUNT", meal->makes_amount() * batchcount );

            p.add_msg_if_player( m_good, _( "The %s begins to hum." ), furniture->name() );

            return;
        }
    }
}

