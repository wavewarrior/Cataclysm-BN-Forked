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
#include "action_time_scale.h"
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
void iexamine::none( player &/*p*/, const tripoint_bub_ms &examp )
{
    add_msg( _( "That is a %s." ), get_map().name( examp ) );
}

/**
 * Pick an appropriate item and apply diamond coating if possible.
 */
/**
 * UI FOR LAB_FINALE NANO FABRICATOR.
 */
/**
 * UI FOR LAB_FINALE SUPERALLOY FORGE.
 */
/**
 * Use "gas pump."  Will pump any liquids on tile.
 */
namespace
{
//--------------------------------------------------------------------------------------------------
//! Implements iexamine::atm(...)
//--------------------------------------------------------------------------------------------------
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
} //namespace

/**
 * launches the atm menu class which then handles all the atm interactions.
 */
// Tier 5: the vending-machine screen RmlUi render path (the one bespoke iexamine
// screen — the rest of the file is popups / uilists, Tier-0 covered). Render-only
// doc: money header + item list (count + name, coloured) + item-info pane.
bool &vending_rmlui_enabled()
{
    static bool enabled = true;
    return enabled;
}

namespace
{
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

bool g_vend_types_registered = false;

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
} // namespace

/**
 * Generates vending machine UI and allows players to purchase contained items with a cash card.
 */
/**
 * If there's water, allow its usage but add chance of poison.
 */
/** Toggle the lights in a overmap terrain*/
/**
 * Open or close gate.
 */
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

/**
 * Use id/hack reader. Using an id despawns turrets.
 */
/**
 * Prompt removal of rubble. Select best shovel and invoke "CLEAR_RUBBLE" on tile.
 */
/**
 * Prompt climbing over fence. Calculates move cost, applies it to player and, moves them.
 */
/**
 * If player has amorphous trait, slip through the bars.
 */
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

/**
 * Determine structure's type and prompts its removal.
 */
/**
 * If there is a 2x4 around, prompt placing it across pit.
 */
/**
 * Prompt removing the 2x4 placed across the pit
 */
/**
 * Loop prompt to bet $10.
 */
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

/**
 * Attempt to crack safe through audio-feedback manual lock manipulation.
 *
 * Try to unlock the safe by moving the dial and listening for the mechanism to "click into place."
 *
 * Time per attempt affected by perception and mechanics. 5 minutes per attempt minimum.
 * Small chance of just guessing the combo without listening device.
 */
/**
 * Attempt to "hack" the gunsafe's electronic lock and open it.
 * Also allow for trying to pry it open as an alternative.
 */
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

/**
 * Checks whether PC has a crowbar then calls iuse.crowbar.
 */
/**
* Checks whether PC has picklocks then calls pick_lock iuse function OR assigns ACT_LOCKPICK
*/
/**
 * Display popup with reference to "The Enigma of Amigara Fault."
 */
/**
 * Display popup message pulled from the object's message property
 */
/**
* Transform the examined object into the object specified by its transforms_into property. If the new object has a message property,
* it is displayed as if the notify examine_action was used.
*/
/**
 * Spawn 1d4 wyrms and sink pedestal into ground.
 */
/**
 * Put petrified eye on pedestal causing it to sink into ground and open temple.
 */
/**
 * Unlock/open door or attempt to peek through peephole.
 */
/**
 * If it's winter: show msg and return true. Otherwise return false
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

/**
 * Helper method to see if player has traits, hunger and mouthwear for drinking nectar.
 */
static bool can_drink_nectar( const player &p, const item &nectar )
{
    return ( p.has_active_mutation( trait_PROBOSCIS )  ||
             p.has_active_mutation( trait_BEAK_HUM ) ) &&
           ( ( p.max_stored_kcal() - p.get_stored_kcal() ) >
             nectar.get_comestible()->default_nutrition.kcal ) &&
           ( !( p.wearing_something_on( bodypart_id( "mouth" ) ) ) );
}

/**
 * Consume Nectar. -15 hunger.
 */
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

/**
 * Spawn an item after harvesting the plant
 */
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

/**
 * Prompt pick (or drink nectar if able) poppy bud. Not safe for player.
 *
 * Drinking causes: -25 hunger, +20 fatigue, pkill2-70 effect and, 1 in 20 pkiller-1 addiction.
 * Picking w/ env_resist < 5 causes 1 in 3  sleep for 12 min and 4 dmg to each leg
 */
/**
 * Prompt pick cactus pad. Not safe for player.
 */
/**
 * Dig up its roots or drink its nectar if you can.
 */
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

/**
 * Only harvest a plant once per season.  Display message and call iexamine::none.
 */
/**
 * Spawn spiders from a spider egg sack in radius 1 around the egg sack.
 * Transforms the egg sack furniture into a ruptured egg sack (f_egg_sacke).
 * Also spawns eggs.
 * @param p The player
 * @param examp Location of egg sack
 * @param montype The monster type of the created spiders.
 */
/**
 * Remove furniture. Add spore effect.
 */
/**
 *  Make lists of unique seed types and names for the menu(no multiple hemp seeds etc)
 */
/**
 *  Choose seed for planting
 */
/**
 *  Actual planting of selected seed
 */
/**
 * If it's warm enough, pick one of the player's seeds and plant it.
 */
/**
 * Items that appear when a generic plant is harvested. Seed @ref islot_seed.
 * @param type The seed type, must have a @ref itype::seed slot.
 * @param plant_count Number of fruits to generate. For charge-based items, this
 *     specifies multiples of the default charge.
 * @param seed_count Number of seeds to generate.
 * @param byproducts If true, byproducts (like straw, withered plants, see
 * @ref islot_seed::byproducts) are included.
 */
/**
 * Actual harvesting of selected plant
 */
// Highly modified fermenting vat functions

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

static item *maple_tree_sap_container()
{
    const item &maple_sap = *item::spawn_temporary( itype_maple_sap, calendar::start_of_cataclysm );
    return g->inv_map_splice( [&]( const item & it ) {
        return it.get_remaining_capacity_for_liquid( maple_sap, true ) > 0;
    }, _( "Which container?" ), PICKUP_RANGE );
}

void iexamine::recycle_compactor( player &, const tripoint_bub_ms &examp )
{
    // choose what metal to recycle
    auto metals = materials::get_compactable();
    uilist choose_metal;
    choose_metal.text = _( "Recycle what metal?" );
    for( auto &m : metals ) {
        choose_metal.addentry( m.name() );
    }
    choose_metal.query();
    int m_idx = choose_metal.ret;
    if( m_idx < 0 || m_idx >= static_cast<int>( metals.size() ) ) {
        add_msg( _( "Never mind." ) );
        return;
    }
    material_type m = metals.at( m_idx );

    map &here = get_map();
    // check inputs and tally total mass
    auto inputs = here.i_at( examp );
    units::mass sum_weight = 0_gram;
    auto ca = m.compact_accepts();
    std::set<material_id> accepts( ca.begin(), ca.end() );
    accepts.insert( m.id );
    for( auto &input : inputs ) {
        if( !input->only_made_of( accepts ) ) {
            //~ %1$s: an item in the compactor , %2$s: desired compactor output material
            add_msg( _( "You realize this isn't going to work because %1$s is not made purely of %2$s." ),
                     input->tname(), m.name() );
            return;
        }
        if( input->is_container() && !input->is_container_empty() ) {
            //~ %1$s: an item in the compactor
            add_msg( _( "You realize this isn't going to work because %1$s has not been emptied of its contents." ),
                     input->tname() );
            return;
        }
        sum_weight += input->weight();
    }
    if( sum_weight <= 0_gram ) {
        //~ %1$s: desired compactor output material
        add_msg( _( "There is no %1$s in the compactor.  Drop some metal items onto it and try again." ),
                 m.name() );
        return;
    }

    // See below for recover_factor (rng(6,9)/10), this
    // is the normal value of that recover factor.
    static const double norm_recover_factor = 8.0 / 10.0;
    const units::mass norm_recover_weight = sum_weight * norm_recover_factor;

    // choose output
    uilist choose_output;
    //~ %1$.3f: total mass of material in compactor, %2$s: weight units , %3$s: compactor output material
    choose_output.text = string_format( _( "Compact %1$.3f %2$s of %3$s into:" ),
                                        convert_weight( sum_weight ), weight_units(), m.name() );
    for( auto &ci : m.compacts_into() ) {
        //TODO!: check
        auto it = item::spawn_temporary( ci, calendar::start_of_cataclysm, item::solitary_tag{} );
        const int amount = norm_recover_weight / it->weight();
        //~ %1$d: number of, %2$s: output item
        choose_output.addentry( string_format( _( "about %1$d %2$s" ), amount,
                                               it->tname( amount ) ) );
    }
    choose_output.query();
    int o_idx = choose_output.ret;
    if( o_idx < 0 || o_idx >= static_cast<int>( m.compacts_into().size() ) ) {
        add_msg( _( "Never mind." ) );
        return;
    }

    // remove items
    for( auto it = inputs.begin(); it != inputs.end(); ) {
        it = inputs.erase( it );
    }

    // produce outputs
    double recover_factor = rng( 6, 9 ) / 10.0;
    sum_weight = sum_weight * recover_factor;
    sound_event se;
    se.origin = examp;
    se.volume = 80;
    se.category = sounds::sound_t::combat;
    se.description = _( "Ka-klunk!" );
    se.id = "tool";
    se.variant = "compactor";
    sounds::sound( se );
    bool out_desired = false;
    bool out_any = false;
    for( auto it = m.compacts_into().begin() + o_idx; it != m.compacts_into().end(); ++it ) {
        const units::mass ow = item::spawn_temporary( *it, calendar::start_of_cataclysm, item::solitary_tag{} )->weight();
        int count = sum_weight / ow;
        sum_weight -= count * ow;
        if( count > 0 ) {
            here.spawn_item( examp, *it, count, 1, calendar::turn );
            if( !out_any ) {
                out_any = true;
                if( it == m.compacts_into().begin() + o_idx ) {
                    out_desired = true;
                }
            }
        }
    }

    // feedback to user
    if( !out_any ) {
        add_msg( _( "The compactor chews up all the items in its hopper." ) );
        //~ %1$s: compactor output material
        add_msg( _( "The compactor beeps: \"No %1$s to process!\"" ), m.name() );
        return;
    }
    if( !out_desired ) {
        //~ %1$s: compactor output material
        add_msg( _( "The compactor beeps: \"Insufficient %1$s!\"" ), m.name() );
        add_msg( _( "It spits out an assortment of smaller pieces instead." ) );
    }
}

void iexamine::trap( player &p, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    const auto &tr = here.tr_at( examp );
    if( !p.is_player() || tr.is_null() ) {
        return;
    }
    const int possible = tr.get_difficulty();
    bool seen = tr.can_see( examp, p );
    if( tr.loadid == tr_unfinished_construction || here.partial_con_at( tripoint_bub_ms( examp ) ) ) {
        partial_con *pc = here.partial_con_at( tripoint_bub_ms( examp ) );
        if( pc ) {
            const construction &built = pc->id.obj();
            if( !character_funcs::can_see_fine_details( p ) && !built.dark_craftable &&
                !p.has_trait( trait_DEBUG_HS ) ) {
                add_msg( m_info, _( "It is too dark to construct right now." ) );
                return;
            }
            if( !query_yn( _( "Unfinished task: %s, %d%% complete here, continue construction?" ),
                           built.group->name(), pc->counter / 100000 ) ) {
                if( query_yn( _( "Cancel construction?" ) ) ) {
                    here.disarm_trap( examp );
                    for( detached_ptr<item> &it : pc->components.clear() ) {
                        here.add_item_or_charges( p.bub_pos(), std::move( it ) );
                    }
                    here.partial_con_remove( examp );
                    return;
                } else {
                    return;
                }
            } else {
                p.assign_activity( std::make_unique<player_activity>( std::make_unique<construction_activity_actor>
                                   ( here.bub_to_abs( examp ) ) ) );
                return;
            }
        } else {
            return;
        }
    }
    if( seen && possible >= 99 ) {
        add_msg( m_info, _( "That %s looks too dangerous to mess with.  Best leave it alone." ),
                 tr.name() );
        return;
    }
    // Some traps are not actual traps. Those should get a different query.
    if( seen && possible == 0 &&
        tr.get_avoidance() == 0 ) { // Separated so saying no doesn't trigger the other query.
        if( query_yn( _( "There is a %s there.  Take down?" ), tr.name() ) ) {
            here.disarm_trap( examp );
        }
    } else if( seen && query_yn( _( "There is a %s there.  Disarm?" ), tr.name() ) ) {
        here.disarm_trap( examp );
    }
}
//Note that these three functions are checked by pointer in map::water_from. Yes it's awful.
void iexamine::water_source( player &, const tripoint_bub_ms &examp )
{
    // Routed to map::water_from to handle poison
    liquid_handler::handle_liquid( examp );
}

//Note that these three functions are checked by pointer in map::water_from. Yes it's awful.
void iexamine::clean_water_source( player &, const tripoint_bub_ms & )
{
    liquid_handler::handle_liquid( item::spawn( "water_clean", calendar::start_of_cataclysm,
                                   item::INFINITE_CHARGES ) );
}

//Note that these three functions are checked by pointer in map::water_from. Yes it's awful.
void iexamine::liquid_source( player &, const tripoint_bub_ms &examp )
{
    liquid_handler::handle_liquid( item::spawn( get_map().furn( examp ).obj().provides_liquids,
                                   calendar::turn, item::INFINITE_CHARGES ) );
}

std::vector<itype> furn_t::crafting_pseudo_item_types() const
{
    std::vector<itype> conversion;
    conversion.reserve( crafting_pseudo_items.size() );
    for( const itype_id &itid : crafting_pseudo_items ) {
        conversion.push_back( *itid );
    }
    return conversion;
}

std::vector<itype> furn_t::crafting_ammo_item_types() const
{
    const std::vector<itype> pseudo_list = crafting_pseudo_item_types();

    std::vector<itype> pseudo_ammo_list;
    if( !pseudo_list.empty() ) {
        for( const itype &pseudo : pseudo_list ) {
            if( pseudo.tool && !pseudo.tool->ammo_id.empty() ) {
                const itype_id &iid = ammotype( *pseudo.tool->ammo_id.begin() )->default_ammotype();
                pseudo_ammo_list.push_back( *iid );
            }
        }
    }
    return pseudo_ammo_list;
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

namespace sm_rack
{
const int MIN_CHARCOAL = 100;
const int CHARCOAL_PER_LITER = 25;
const units::volume MAX_FOOD_VOLUME_MILLING = units::from_liter( 100 );
const units::volume MAX_FOOD_VOLUME = units::from_liter( 20 );
const units::volume MAX_FOOD_VOLUME_PORTABLE = units::from_liter( 15 );
} // namespace sm_rack

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

/**
 * Given then name of one of the above functions, returns the matching function
 * pointer. If no match is found, defaults to iexamine::none but prints out a
 * debug message as a warning.
 * @param function_name The name of the function to get.
 * @return A function pointer to the specified function.
 */
iexamine_function iexamine_function_from_string( const std::string &function_name )
{
    static const std::map<std::string, iexamine_function> function_map = {{
            { "none", &iexamine::none },
            { "deployed_furniture", &iexamine::deployed_furniture },
            { "cvdmachine", &iexamine::cvdmachine },
            { "nanofab", &iexamine::nanofab },
            { "nanoforge", &iexamine::nanoforge },
            { "gaspump", &iexamine::gaspump },
            { "atm", &iexamine::atm },
            { "vending", &iexamine::vending },
            { "toilet", &iexamine::toilet },
            { "elevator", &iexamine::elevator },
            { "toggle_lights", &iexamine::toggle_lights},
            { "controls_gate", &iexamine::controls_gate },
            { "cardreader", &iexamine::cardreader },
            { "cardreader_robofac", &iexamine::cardreader_robofac },
            { "cardreader_fp", &iexamine::cardreader_foodplace },
            { "intercom", &iexamine::intercom },
            { "rubble", &iexamine::rubble },
            { "chainfence", &iexamine::chainfence },
            { "bars", &iexamine::bars },
            { "portable_structure", &iexamine::portable_structure },
            { "pit", &iexamine::pit },
            { "pit_covered", &iexamine::pit_covered },
            { "slot_machine", &iexamine::slot_machine },
            { "safe", &iexamine::safe },
            { "fault", &iexamine::fault },
            { "notify", &iexamine::notify },
            { "transform", &iexamine::transform },
            { "pedestal_wyrm", &iexamine::pedestal_wyrm },
            { "pedestal_temple", &iexamine::pedestal_temple },
            { "door_peephole", &iexamine::door_peephole },
            { "fswitch", &iexamine::fswitch },
            { "flower_poppy", &iexamine::flower_poppy },
            { "flower_cactus", &iexamine::flower_cactus },
            { "fungus", &iexamine::fungus },
            { "flower_dahlia", &iexamine::flower_dahlia },
            { "flower_marloss", &iexamine::flower_marloss },
            { "egg_sackbw", &iexamine::egg_sackbw },
            { "egg_sackcs", &iexamine::egg_sackcs },
            { "egg_sackws", &iexamine::egg_sackws },
            { "dirtmound", &iexamine::dirtmound },
            { "aggie_plant", &iexamine::aggie_plant },
            { "fvat_empty", &iexamine::fvat_empty },
            { "fvat_full", &iexamine::fvat_full },
            { "keg", &iexamine::keg },
            { "harvest_furn_nectar", &iexamine::harvest_furn_nectar },
            { "harvest_furn", &iexamine::harvest_furn },
            { "harvest_ter_nectar", &iexamine::harvest_ter_nectar },
            { "harvest_ter", &iexamine::harvest_ter },
            { "harvested_plant", &iexamine::harvested_plant },
            { "shrub_marloss", &iexamine::shrub_marloss },
            { "translocator", &iexamine::translocator },
            { "tree_marloss", &iexamine::tree_marloss },
            { "tree_hickory", &iexamine::tree_hickory },
            { "tree_maple", &iexamine::tree_maple },
            { "tree_maple_tapped", &iexamine::tree_maple_tapped },
            { "shrub_wildveggies", &iexamine::shrub_wildveggies },
            { "recycle_compactor", &iexamine::recycle_compactor },
            { "trap", &iexamine::trap },
            { "water_source", &iexamine::water_source },
            { "clean_water_source", &iexamine::clean_water_source },
            { "liquid_source", &iexamine::liquid_source },
            { "fluid_grid_fixture", &iexamine::fluid_grid_fixture },
            { "reload_furniture", &iexamine::reload_furniture },
            { "use_furn_fake_item", &iexamine::use_furn_fake_item },
            { "curtains", &iexamine::curtains },
            { "sign", &iexamine::sign },
            { "pay_gas", &iexamine::pay_gas },
            { "gunsafe_el", &iexamine::gunsafe_el },
            { "locked_object", &iexamine::locked_object },
            { "locked_object_pickable", &iexamine::locked_object_pickable },
            { "kiln_empty", &iexamine::kiln_empty },
            { "kiln_full", &iexamine::kiln_full },
            { "arcfurnace_empty", &iexamine::arcfurnace_empty },
            { "arcfurnace_full", &iexamine::arcfurnace_full },
            { "autoclave_empty", &iexamine::autoclave_empty },
            { "autoclave_full", &iexamine::autoclave_full },
            { "fireplace", &iexamine::fireplace },
            { "ledge", &iexamine::ledge },
            { "autodoc", &iexamine::autodoc },
            { "quern_examine", &iexamine::quern_examine },
            { "cloning_vat_examine", &iexamine::cloning_vat_examine },
            { "smoker_options", &iexamine::smoker_options },
            { "open_safe", &iexamine::open_safe },
            { "workbench", &iexamine::workbench },
            { "dimensional_portal", &iexamine::dimensional_portal },
            { "check_power", &iexamine::check_power },
            { "power_portal", &iexamine::power_portal },
            { "portal", &iexamine::portal },
            { "migo_nerve_cluster", &iexamine::migo_nerve_cluster },
            { "cardreader_plutgen", &iexamine::cardreader_plutgen },
            { "multicooker", &iexamine::multicooker },
        }
    };

    auto iter = function_map.find( function_name );
    if( iter != function_map.end() ) {
        return iter->second;
    }

    //No match found
    debugmsg( "Could not find an iexamine function matching '%s'!", function_name );
    return &iexamine::none;
}

void iexamine::practice_survival_while_foraging( player *p )
{
    ///\EFFECT_INT Intelligence caps survival skill gains from foraging
    const int max_forage_skill = p->int_cur / 2 + 1;
    const int max_exp = 2 * max_forage_skill;
    // Award experience for foraging attempt regardless of success
    p->practice( skill_survival, rng( 1, max_exp ), max_forage_skill );
}
