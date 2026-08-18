#include "action.h"
#include "activity_actor_definitions.h"
#include "advanced_inv.h"
#include "animation.h"
#include "armor_layers.h"
#include "auto_note.h"
#include "auto_pickup.h"
#include "avatar.h"
#include "avatar_action.h"
#include "avatar_functions.h"
#include "bionics.h"
#include "bionics_ui.h"
#include "calendar.h"
#include "catacharset.h"
#include "catalua.h"
#include "coop_client.h"
#include "coop_menu.h"
#include "coop_server.h"
#include "coop_session.h"
#include "json.h"
#include "character.h"
#include "character_display.h"
#include "character_martial_arts.h"
#include "character_turn.h"
#include "clzones.h"
#include "color.h"
#include "construction.h"
#include "crafting.h"
#include "cursesdef.h"
#include "damage.h"
#include "debug.h"
#include "debug_menu.h"
#include "diary.h"
#include "distraction_manager.h"
#include "faction.h"
#include "field.h"
#include "field_type.h"
#include "flag.h"
#include "fstream_utils.h"
#include "game.h" // IWYU pragma: associated
#include "game_constants.h"
#include "game_inventory.h"
#include "gamemode.h"
#include "gates.h"
#include "gun_mode.h"
#include "help.h"
#include "input.h"
#include "int_id.h"
#include "item.h"
#include "item_contents.h"
#include "item_group.h"
#include "item_hauling.h"
#include "itype.h"
#include "iuse.h"
#include "lightmap.h"
#include "line.h"
#include "magic.h"
#include "make_static.h"
#include "map.h"
#include "map_selector.h"
#include "mapdata.h"
#include "mapsharing.h"
#include "messages.h"
#include "monster.h"
#include "mtype.h"
#include "mutation.h"
#include "mutation_ui.h"
#include "options.h"
#include "output.h"
#include "overmap_ui.h"
#include "panels.h"
#include "player.h"
#include "player_activity.h"
#include "player_cmd.h"
#include "popup.h"
#include "ranged.h"
#include "rng.h"
#include "safemode_ui.h"
#include "salvage.h"
#include "scores_ui.h"
#include "sdl_lighting_devui.h"
#include "sidebar_anim.h"
#include "sounds.h"
#include "sound_visualization.h"
#include "string_formatter.h"
#include "string_id.h"
#include "string_input_popup.h"
#include "string_utils.h"
#include "throw_radial.h"
#include "translations.h"
#include "ui.h"
#include "ui_manager.h"
#include "units.h"
#include "utils/url.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vehicle_grab.h"
#include "vehicle_part.h"
#include "vehicle_wait.h"
#include "vpart_position.h"
#include "vpart_range.h"
#include "weather.h"
#include "worldfactory.h"

#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <set>
#include <unordered_set>
#include <sstream>
#include <utility>

namespace
{
/// Emit a TERRAIN_CHANGE action to the co-op host with the new ter/furn at an abs position.
/// Only called when g->coop_client_ is non-null (client-side only).
inline void coop_emit_terrain_change(
    const tripoint_abs_ms &abs, const ter_id &ter, const furn_id &furn )
{
    g->coop_client_->queue_terrain_change( abs, ter.id().str(), furn.id().str() );
}
} // namespace

#include "handle_action_helpers.h"
using namespace action_handlers;

static const activity_id ACT_MOVE_LOOT( "ACT_MOVE_LOOT" );
static const activity_id ACT_MULTIPLE_BUTCHER( "ACT_MULTIPLE_BUTCHER" );
static const activity_id ACT_MULTIPLE_CHOP_PLANKS( "ACT_MULTIPLE_CHOP_PLANKS" );
static const activity_id ACT_MULTIPLE_CHOP_TREES( "ACT_MULTIPLE_CHOP_TREES" );
static const activity_id ACT_MULTIPLE_CONSTRUCTION( "ACT_MULTIPLE_CONSTRUCTION" );
static const activity_id ACT_MULTIPLE_FARM( "ACT_MULTIPLE_FARM" );
static const activity_id ACT_MULTIPLE_MINE( "ACT_MULTIPLE_MINE" );
static const activity_id ACT_PULP( "ACT_PULP" );
static const activity_id ACT_SPELLCASTING( "ACT_SPELLCASTING" );
static const activity_id ACT_VEHICLE_DECONSTRUCTION( "ACT_VEHICLE_DECONSTRUCTION" );
static const activity_id ACT_VEHICLE_REPAIR( "ACT_VEHICLE_REPAIR" );
static const activity_id ACT_WAIT( "ACT_WAIT" );
static const activity_id ACT_WAIT_STAMINA( "ACT_WAIT_STAMINA" );
static const activity_id ACT_WAIT_WEATHER( "ACT_WAIT_WEATHER" );

static const efftype_id effect_alarm_clock( "alarm_clock" );
static const efftype_id effect_laserlocked( "laserlocked" );
static const efftype_id effect_relax_gas( "relax_gas" );

static const itype_id itype_shoulder_strap( "shoulder_strap" );
static const itype_id itype_pistol_lanyard( "pistol_lanyard" );

static const skill_id skill_melee( "melee" );

static const quality_id qual_CUT( "CUT" );

static const bionic_id bio_remote( "bio_remote" );

static const trait_id trait_HIBERNATE( "HIBERNATE" );
static const trait_id trait_PROF_CHURL( "PROF_CHURL" );
static const trait_id trait_SHELL2( "SHELL2" );
static const trait_id trait_BRAWLER( "BRAWLER" );

static const std::string flag_LOCKED( "LOCKED" );

#define dbg(x) DebugLogFL((x), DC::Game)

class user_turn
{

    private:
        std::chrono::time_point<std::chrono::steady_clock> user_turn_start;

    public:
        user_turn() { user_turn_start = std::chrono::steady_clock::now(); }

        bool has_timeout_elapsed() { return moves_elapsed() > 100; }

        int moves_elapsed() {
            const float turn_duration = get_option<float>( "TURN_DURATION" );
            // Magic number 0.005 chosen due to option menu's 2 digit precision and
            // the option menu UI rounding <= 0.005 down to "0.00" in the display.
            // This conditional will catch values (e.g. 0.003) that the options menu
            // would round down to "0.00" in the options menu display. This prevents
            // the user from being surprised by floating point rounding near zero.
            if( turn_duration <= 0.005 ) { return 0; }
            auto now = std::chrono::steady_clock::now();
            std::chrono::milliseconds elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>( now - user_turn_start );
            return elapsed_ms.count() / ( 10.0 * turn_duration );
        }
};

input_context game::get_player_input( std::string& action )
{
    input_context ctxt;
    if( uquit == QUIT_WATCH ) {
        ctxt = input_context( "DEFAULTMODE" );
        ctxt.set_iso( true );
        // The list of allowed actions in death-cam mode in game::handle_action
        // *INDENT-OFF*
        for (const action_id id : {
                 ACTION_TOGGLE_MAP_MEMORY,
                 ACTION_CENTER,
                 ACTION_SHIFT_N,
                 ACTION_SHIFT_NE,
                 ACTION_SHIFT_E,
                 ACTION_SHIFT_SE,
                 ACTION_SHIFT_S,
                 ACTION_SHIFT_SW,
                 ACTION_SHIFT_W,
                 ACTION_SHIFT_NW,
                 ACTION_LOOK,
                 ACTION_KEYBINDINGS,
             }) {
            ctxt.register_action(action_ident(id));
        }
        // *INDENT-ON*
        ctxt.register_action( "QUIT", to_translation( "Accept your fate" ) );
    } else {
        ctxt = get_default_mode_input_context();
    }

    user_turn current_turn;


    // Checking early if we will need to handle animations
    // If we do not need to handle animations that will not change as long as the user has not
    // selected an action and we can handle it like we are not animating.
    weather_printable wPrint;
    bool animate_weather = false;
    bool animate_sct = false;
    bool do_animations = [&]() {
        if( get_option<bool>( "ANIMATIONS" ) ) {
            const bool weather_has_anim = init_weather_anim( get_weather().weather_id, wPrint );

            animate_weather =
                weather_has_anim && get_option<bool>( "ANIMATION_RAIN" ) && !g_rain_enable;
            animate_sct =
                !SCT.vSCT.empty() && uquit != QUIT_WATCH && get_option<bool>( "ANIMATION_SCT" );

            // Always animate, minimap and terrain may have animations to run
            return true;
        }
        return false;
    }
    ();

    if( do_animations ) {
        // Input/redraw cadence. Weather/SCT frame-stepping is tuned to a 125ms
        // tick, so keep that while either is active (rain/combat-text speed
        // unchanged). When ONLY the sidebar is animating, run ~30fps so tween
        // motion is smooth. Otherwise (idle, nothing animating) stay at 125ms so
        // we don't wake 30x/sec for nothing — this is re-evaluated each iteration
        // below, since a tween only starts once the first redraw runs.
        constexpr int ANIM_FRAME_MS = 33;
        constexpr int SPRITE_ANIM_FRAME_MS = 25; // ~40fps while sprite animations are live
        const auto anim_timeout = []( bool weather, bool sct ) {
            // Weather/SCT frame-stepping is tuned to 125ms ticks; keep that while active
            // (sprite anims degrade to 8fps during rain rather than speeding the rain 5x).
            if( weather || sct ) { return 125; }
            if( creatures_require_animation() || sfx::sound_pulses_active() ) { return SPRITE_ANIM_FRAME_MS; }
            return sidebar_requires_animation() ? ANIM_FRAME_MS : 125;
        };
        ctxt.set_timeout( anim_timeout( animate_weather, animate_sct ) );

        shared_ptr_fast<game::draw_callback_t> animation_cb = make_shared_fast <
        game::draw_callback_t > ( [&]() {
            if( animate_weather ) { draw_weather( wPrint ); }
            if( animate_sct ) { draw_sct(); }
        } );
        add_draw_callback( animation_cb );
        invalidate_main_ui_adaptor(); // We want to redraw at least once.

        do {
            if( animate_weather ) {
                invalidate_main_ui_adaptor();
                generate_weather_anim_frame( get_weather().weather_id, wPrint );
            }
            // don't bother calculating SCT if we won't show it
            if( animate_sct ) {
                invalidate_main_ui_adaptor();

                SCT.advanceAllSteps();

                // Check for creatures on all drawing positions and offset if necessary
                for( auto iter = SCT.vSCT.rbegin(); iter != SCT.vSCT.rend(); ++iter ) {
                    const direction oCurDir = iter->getDirecton();
                    const int width = utf8_width( iter->getText() );
                    for( int i = 0; i < width; ++i ) {
                        tripoint_bub_ms tmp( iter->getPosX() + i, iter->getPosY(), get_levz() );
                        const Creature* critter = critter_at( tmp, true );

                        if( critter != nullptr && u.sees( *critter ) ) {
                            i = -1;
                            int iPos = iter->getStep() + iter->getStepOffset();
                            for( auto iter2 = iter; iter2 != SCT.vSCT.rend(); ++iter2 ) {
                                if( iter2->getDirecton() == oCurDir
                                    && iter2->getStep() + iter2->getStepOffset() <= iPos ) {
                                    if( iter2->getType() == "hp" ) { iter2->advanceStepOffset(); }

                                    iter2->advanceStepOffset();
                                    iPos = iter2->getStep() + iter2->getStepOffset();
                                }
                            }
                        }
                    }
                }

                // Stop animation when done
                animate_sct = !SCT.vSCT.empty();
            }
            // We don't cache these checks as their result may change after 1st redraw
            if( minimap_requires_animation() || terrain_requires_animation()
                || sidebar_requires_animation() || creatures_require_animation()
                || sfx::sound_pulses_active() ) {
                // TODO: we redraw *everything* just to animate a couple blinking dots
                //       on the minimap or a few tiles.
                //       This is far from ideal, and can probably be done much cheaper
                //       (update only part of the screen? draw static parts into a texture?)
                invalidate_main_ui_adaptor();
            }

            std::unique_ptr<static_popup> deathcam_msg_popup;
            if( uquit == QUIT_WATCH ) {
                deathcam_msg_popup = std::make_unique<static_popup>();
                deathcam_msg_popup
                ->wait_message( c_red, _( "Press %s to accept your fate…" ), ctxt.get_desc( "QUIT" ) )
                .on_top( true );
            }

            ui_manager::redraw_invalidated();
            // This redraw may have started a sidebar tween (a value-change pop):
            // tighten to ~30fps while one is live, relax back to 125ms once it
            // settles, so an idle sidebar doesn't hold the loop at 30fps.
            ctxt.set_timeout( anim_timeout( animate_weather, animate_sct ) );
        } while(
            handle_mouseview( ctxt, action ) && uquit != QUIT_WATCH
            && ( action != "TIMEOUT" || !current_turn.has_timeout_elapsed() ) );
        ctxt.reset_timeout();
    } else {
        invalidate_main_ui_adaptor();
        ui_manager::redraw_invalidated();
        SCT.vSCT.clear();

        ctxt.set_timeout( sfx::sound_pulses_active() ? 25 : 125 );
        while( handle_mouseview( ctxt, action ) ) {
            if( action == "TIMEOUT" ) {
                ctxt.set_timeout( sfx::sound_pulses_active() ? 25 : 125 );
                if( current_turn.has_timeout_elapsed() ) { break; }
            }
        }
        ctxt.reset_timeout();
    }

    return ctxt;
}

// Establish or release a grab on a vehicle
// Perform a reach attach using wielded weapon
namespace
{
/// ACTION_THROW_QUICKSLOT, shared by game::handle_action() and
/// game::handle_action_from(). Both dispatchers used to carry a byte-identical
/// copy of the list menu below, so the radial would otherwise have had to be
/// added — and kept in sync — twice, with only one of the two ever exercised.
void do_throw_quickslot( avatar &u )
{
    const throw_radial_result res = show_throw_quickslot_radial( u );
    std::optional<int> chosen = res.slot;

    if( !res.shown ) {
        // RmlUi unavailable: the wheel cannot degrade to "point at nothing", so
        // fall back to the flat list this action shipped with.
        uilist menu;
        menu.title = _( "Throw Quick-Slots" );
        for( int i = 0; i < avatar::MAX_THROW_SLOTS; ++i ) {
            if( u.is_throw_slot_empty( i ) ) {
                menu.addentry( i, false, '1' + i, _( "[%d] ---" ), i + 1 );
            } else {
                const auto &type = u.get_throw_slot( i );
                const int count = u.count_throwable( i );
                const bool active = ( i == u.get_active_throw_slot() );
                menu.addentry( i, true, '1' + i, "%s[%d] %s \u00d7%d",
                               active ? "> " : "  ", i + 1, type->nname( 1 ), count );
            }
        }
        menu.query();
        if( menu.ret >= 0 && menu.ret < avatar::MAX_THROW_SLOTS ) { chosen = menu.ret; }
    }

    if( !chosen.has_value() ) { return; }
    u.set_active_throw_slot( *chosen );
    add_msg( _( "Throw slot set to %s." ), u.get_throw_slot( *chosen )->nname( 1 ) );
}
} // namespace

void game::open_consume_item_menu()
{
    uilist as_m;

    as_m.text = _( "What do you want to consume?" );

    as_m.entries.emplace_back( 0, true, 'f', _( "Food" ) );
    as_m.entries.emplace_back( 1, true, 'd', _( "Drink" ) );
    as_m.entries.emplace_back( 2, true, 'm', _( "Medication" ) );
    as_m.query();

    switch( as_m.ret ) {
        case 0:
            avatar_action::eat( u, game_menus::inv::consume_food( u ) );
            break;
        case 1:
            avatar_action::eat( u, game_menus::inv::consume_drink( u ) );
            break;
        case 2:
            avatar_action::eat( u, game_menus::inv::consume_meds( u ) );
            break;
        default:
            break;
    }
}

bool game::handle_action()
{
    std::string action;
    input_context ctxt;
    action_id act = ACTION_NULL;
    user_turn current_turn;
    // Check if we have an auto-move destination
    if( u.has_destination() ) {
        act = u.get_next_auto_move_direction();
        if( act == ACTION_NULL ) {
            add_msg( m_info, _( "Auto-move canceled" ) );
            u.clear_destination();
            return false;
        }
    } else if( u.has_destination_activity() ) {
        // starts destination activity after the player successfully reached his destination
        u.start_destination_activity();
        return false;
    } else {
        // No auto-move, ask player for input
        ctxt = get_player_input( action );
    }

    const optional_vpart_position vp = m.veh_at( u.bub_pos() );
    const auto player_vehicle = vp ? &vp->vehicle() : nullptr;
    const bool local_vehicle_in_control = vp && vp->vehicle().player_in_control( u );
    const auto remote_vehicle = remoteveh();
    const auto controlled_vehicle =
        remote_vehicle != nullptr ? remote_vehicle
        : local_vehicle_in_control
        ? player_vehicle
        : nullptr;
    const auto veh_ctrl =
        !u.is_dead_state() && ( local_vehicle_in_control || remote_vehicle != nullptr );

    // If performing an action with right mouse button, co-ordinates
    // of location clicked.
    std::optional<tripoint_bub_ms> mouse_target;

    if( uquit == QUIT_WATCH && action == "QUIT" ) {
        uquit = QUIT_DIED;
        return false;
    }

    if( act == ACTION_NULL ) {
        act = look_up_action( action );

        if( act == ACTION_KEYBINDINGS ) {
            // already handled by input context
            return false;
        }

        if( act == ACTION_MAIN_MENU ) {
            if( uquit == QUIT_WATCH ) { return false; }
            // No auto-move actions have or can be set at this point.
            u.clear_destination();
            destination_preview.clear();
            act = handle_main_menu();
            if( act == ACTION_NULL ) { return false; }
        }

        if( act == ACTION_ACTIONMENU ) {
            if( uquit == QUIT_WATCH ) { return false; }
            // No auto-move actions have or can be set at this point.
            u.clear_destination();
            destination_preview.clear();
            act = handle_action_menu();
            if( act == ACTION_NULL ) { return false; }
        }

        if( act == ACTION_KEYBINDINGS ) {
            u.clear_destination();
            destination_preview.clear();
            act = ctxt.display_menu( true );
            if( act == ACTION_NULL ) { return false; }
        }

        if( can_action_change_worldstate( act ) ) { user_action_counter += 1; }

        if( act == ACTION_SELECT || act == ACTION_SEC_SELECT ) {
            // Mouse button click
            if( veh_ctrl ) {
                // No mouse use in vehicle
                return false;
            }

            if( u.is_dead_state() ) {
                // do not allow mouse actions while dead
                return false;
            }

            const std::optional<tripoint_bub_ms> mouse_pos = ctxt.get_coordinates( w_terrain );
            if( !mouse_pos ) {
                return false;
            } else if( !u.sees( *mouse_pos ) ) {
                // Not clicked in visible terrain
                return false;
            }
            mouse_target = mouse_pos;

            if( act == ACTION_SELECT ) {
                // Note: The following has the potential side effect of
                // setting auto-move destination state in addition to setting
                // act.
                if( !try_get_left_click_action( act, *mouse_target ) ) { return false; }
            } else if( act == ACTION_SEC_SELECT ) {
                if( !try_get_right_click_action( act, *mouse_target ) ) { return false; }
            }
        } else if( act != ACTION_TIMEOUT ) {
            // act has not been set for an auto-move, so clearing possible
            // auto-move destinations. Since initializing an auto-move with
            // the mouse may span across multiple actions, we do not clear the
            // auto-move destination if the action is only a timeout, as this
            // would require the user to double click quicker than the
            // timeout delay.
            u.clear_destination();
            destination_preview.clear();
        }
    }

    if( act == ACTION_NULL ) {
        const input_event&& evt = ctxt.get_raw_input();
        if( !evt.sequence.empty() ) {
            const int ch = evt.get_first_input();
            const std::string&& name = inp_mngr.get_keyname( ch, evt.type, true );
            if( !get_option<bool>( "NO_UNKNOWN_COMMAND_MSG" ) ) {
                add_msg( m_info, _( "Unknown command: \"%s\" (%ld)" ), name, ch );
                if( const std::optional<std::string> hint = press_x_if_bound( ACTION_KEYBINDINGS ) ) {
                    add_msg( m_info,
                             _( "%s at any time to see and edit keybindings relevant to "
                                "the current context." ),
                             *hint );
                }
            }
        }
        return false;
    }

    // This has no action unless we're in a special game mode.
    gamemode->pre_action( act );

    int soffset = get_option<int>( "MOVE_VIEW_OFFSET" );

    int before_action_moves = u.moves;

    // These actions are allowed while deathcam is active. Registered in game::get_player_input
    if( uquit == QUIT_WATCH || !u.is_dead_state() ) {
        switch( act ) {
            case ACTION_TOGGLE_MAP_MEMORY:
                u.toggle_map_memory();
                break;

            case ACTION_CENTER:
                u.view_offset.x() = driving_view_offset.x;
                u.view_offset.y() = driving_view_offset.y;
                break;

            case ACTION_SHIFT_N:
            case ACTION_SHIFT_NE:
            case ACTION_SHIFT_E:
            case ACTION_SHIFT_SE:
            case ACTION_SHIFT_S:
            case ACTION_SHIFT_SW:
            case ACTION_SHIFT_W:
            case ACTION_SHIFT_NW: {
                static const std::map<action_id, std::pair<point, point>> shift_delta = {
                    {ACTION_SHIFT_N, {point_north, point_north_east}},
                    {ACTION_SHIFT_NE, {point_north_east, point_east}},
                    {ACTION_SHIFT_E, {point_east, point_south_east}},
                    {ACTION_SHIFT_SE, {point_south_east, point_south}},
                    {ACTION_SHIFT_S, {point_south, point_south_west}},
                    {ACTION_SHIFT_SW, {point_south_west, point_west}},
                    {ACTION_SHIFT_W, {point_west, point_north_west}},
                    {ACTION_SHIFT_NW, {point_north_west, point_north}},
                };
                u.view_offset +=
                    tile_iso ? shift_delta.at( act ).second * soffset
                    : shift_delta.at( act ).first * soffset;
            }
            break;

            case ACTION_LOOK:
                // Middle-click (or any future mouse binding of "look") leaves
                // ctxt's coordinate state populated from the triggering click
                // even though act was resolved directly rather than through
                // look_up_action(); reuse it so the cursor starts on the
                // clicked tile instead of the player. nullopt (keyboard-
                // triggered look) falls back to the player's own tile.
                look_around( LA_MODE_DEFAULT, ctxt.get_coordinates( w_terrain ) );
                break;

            case ACTION_KEYBINDINGS:
                // already handled by input context
                break;

            default:
                break;
        }
    }

    // actions allowed only while alive
    if( !u.is_dead_state() ) {
        switch( act ) {
            case ACTION_NULL:
            case NUM_ACTIONS:
                break; // dummy entries
            case ACTION_ACTIONMENU:
            case ACTION_MAIN_MENU:
            case ACTION_KEYBINDINGS:
                break; // handled above

            case ACTION_TIMEOUT:
                if( check_safe_mode_allowed( false ) ) { character_funcs::do_pause( u ); }
                break;

            case ACTION_PAUSE:
                if( check_safe_mode_allowed() ) { character_funcs::do_pause( u ); }
                break;

            case ACTION_CYCLE_MOVE:
                u.cycle_move_mode();
                break;

            case ACTION_RESET_MOVE:
                u.reset_move_mode();
                break;

            case ACTION_TOGGLE_RUN:
                u.toggle_run_mode();
                break;

            case ACTION_TOGGLE_CROUCH:
                u.toggle_crouch_mode();
                break;

            case ACTION_OPEN_MOVEMENT:
                open_movement_mode_menu();
                break;

            case ACTION_MOVE_FORTH:
            case ACTION_MOVE_FORTH_RIGHT:
            case ACTION_MOVE_RIGHT:
            case ACTION_MOVE_BACK_RIGHT:
            case ACTION_MOVE_BACK:
            case ACTION_MOVE_BACK_LEFT:
            case ACTION_MOVE_LEFT:
            case ACTION_MOVE_FORTH_LEFT:
                if( !u.get_value( "remote_controlling" ).empty()
                    && ( u.has_active_item_with_action( "RADIOCONTROL" )
                         || u.has_active_bionic( bio_remote ) ) ) {
                    rcdrive( get_delta_from_movement_action( act, iso_rotate::yes ) );
                } else if( veh_ctrl ) {
                    // vehicle control uses x for steering and y for ac/deceleration,
                    // so no rotation needed
                    pldrive( get_delta_from_movement_action( act, iso_rotate::no )
                             .reinterpret_as<point_rel_veh>() );
                } else {
                    auto dest_delta = get_delta_from_movement_action( act, iso_rotate::yes );
                    if( auto_travel_mode && !u.is_auto_moving() ) {
                        for( int i = 0; i < SEEX; i++ ) {
                            tripoint_bub_ms auto_travel_destination(
                                u.bub_pos().x() + dest_delta.x() * ( SEEX - i ),
                                u.bub_pos().y() + dest_delta.y() * ( SEEX - i ), u.bub_pos().z() );
                            destination_preview = m.route(
                                                      u.bub_pos(), auto_travel_destination,
                                                      u.get_legacy_pathfinding_settings(), u.get_legacy_path_avoid() );
                            if( !destination_preview.empty() ) {
                                destination_preview.erase(
                                    destination_preview.begin() + 1, destination_preview.end() );
                                u.set_destination( destination_preview );
                                break;
                            }
                        }
                        act = u.get_next_auto_move_direction();
                        const auto dest_next = get_delta_from_movement_action( act, iso_rotate::yes );
                        if( dest_next == point_rel_ms::zero() ) { u.clear_destination(); }
                        dest_delta = dest_next;
                    }
                    if( !avatar_action::move( u, m, dest_delta ) ) {
                        // auto-move should be canceled due to a failed move or obstacle
                        u.clear_destination();
                    }
                }
                break;
            case ACTION_MOVE_DOWN:
                if( u.is_mounted() ) {
                    const monster* mon = u.mounted_creature.get();
                    bool ladder = m.has_flag( "DIFFICULT_Z", u.bub_pos() );

                    if( ladder ) {
                        const bool can_use_ladder =
                            mon->has_flag( MF_MOUNTABLE_LADDER ) || mon->has_flag( MF_FLIES );

                        if( !can_use_ladder ) {
                            add_msg( m_info, _( "Your mount can't go downstairs while riding." ) );
                            break;
                        }
                    } else {
                        const bool can_use_stairs =
                            mon->has_flag( MF_MOUNTABLE_STAIRS ) || mon->has_flag( MF_FLIES );

                        if( !can_use_stairs ) {
                            add_msg( m_info, _( "Your mount can't go downstairs while riding." ) );
                            break;
                        }
                    }
                }
                if( controlled_vehicle != nullptr && controlled_vehicle->is_aircraft() ) {
                    pldrive( tripoint_rel_veh::below() );
                } else if( !u.in_vehicle ) {
                    vertical_move( -1, false );
                } else if( get_map().has_rope_at( u.bub_pos() ) ) {
                    map& here = get_map();
                    const optional_vpart_position vp = here.veh_at( u.bub_pos() );
                    const int idx =
                        vp->vehicle().part_with_feature( vp->part_index(), VPFLAG_LADDER, true );
                    if( idx != -1 ) {
                        const vpart_info info = vp->vehicle().part_info( idx );
                        auto where = u.bub_pos();
                        auto below = where;
                        if( get_map().ter( where ).id().str() != "t_open_air" ) { break; }
                        below.z()--;
                        // Keep going down until we find a tile that is NOT open air
                        while( get_map().ter( below ).id().str() == "t_open_air" ) {
                            where.z()--;
                            below.z()--;
                        }
                        const int dist = u.bub_pos().z() - below.z();
                        if( info.ladder_length() >= dist ) {
                            get_map().unboard_vehicle( u.bub_pos() );
                            vertical_move( -dist, true );
                        }
                    }
                }
                break;

            case ACTION_MOVE_UP:
                if( u.is_mounted() ) {
                    const monster* mon = u.mounted_creature.get();
                    bool ladder = m.has_flag( "DIFFICULT_Z", u.bub_pos() );

                    if( ladder ) {
                        const bool can_use_ladder =
                            mon->has_flag( MF_MOUNTABLE_LADDER ) || mon->has_flag( MF_FLIES );

                        if( !can_use_ladder ) {
                            add_msg( m_info, _( "Your mount can't go upstairs or climb while "
                                                "riding." ) );
                            break;
                        }
                    } else {
                        const bool can_use_stairs =
                            mon->has_flag( MF_MOUNTABLE_STAIRS ) || mon->has_flag( MF_FLIES );

                        if( !can_use_stairs ) {
                            add_msg( m_info, _( "Your mount can't go upstairs or climb while "
                                                "riding." ) );
                            break;
                        }
                    }
                }
                if( controlled_vehicle != nullptr ) {
                    if( controlled_vehicle->is_aircraft() ) {
                        pldrive( tripoint_rel_veh::above() );
                    } else if(
                        ( controlled_vehicle->has_part( "ROTOR" )
                          || controlled_vehicle->has_part( "BALLOON" )
                          || controlled_vehicle->has_part( "WING" ) )
                        && !controlled_vehicle->has_sufficient_lift() ) {
                        add_msg( m_bad, _( "The craft struggles to generate enough lift!" ) );
                    } else {
                        u.add_msg_if_player( _( "You need a propeller to take off!" ) );
                    }
                } else if( !u.in_vehicle ) {
                    if( get_map().has_rope_at( u.bub_pos() ) ) {
                        auto xy = u.bub_pos().xy();
                        map& here = get_map();
                        auto where = u.bub_pos();
                        auto above = where;
                        above.z()++;
                        if( get_map().ter( above ).id().str() != "t_open_air" ) {
                            vertical_move( 1, false );
                            break;
                        }
                        // Keep going down until we find a tile that is NOT open air
                        while( get_map().ter( above ).id().str() == "t_open_air"
                               && !here.veh_at( tripoint_bub_ms( xy, above.z() ) ) ) {
                            above.z()++;
                        }
                        const optional_vpart_position vp = here.veh_at(
                                                               tripoint_bub_ms( xy, above.z() ) );
                        const int dist = above.z() - u.bub_pos().z();
                        if( vp ) {
                            const int idx = vp->vehicle().part_with_feature(
                                                vp->part_index(), VPFLAG_LADDER, true );
                            if( idx != -1 ) {
                                const vpart_info info = vp->vehicle().part_info( idx );
                                if( info.ladder_length() >= dist ) {
                                    vertical_move( dist, true );
                                    here.board_vehicle( u.bub_pos(), u.as_character() );
                                    break;
                                }
                            }
                        } else {
                            vertical_move( 1, false );
                        }
                    } else {
                        vertical_move( 1, false );
                    }
                } else {
                    u.add_msg_if_player( _( "You need a propeller to take off!" ) );
                }
                break;

            case ACTION_OPEN:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't open things while you're in your shell." ) );
                } else if( u.is_mounted() ) {
                    auto mon = u.mounted_creature.get();
                    if( !mon->has_flag( MF_MOUNTABLE_DOORS ) ) {
                        add_msg( m_info, _( "You can't open things while you're riding." ) );
                        break;
                    } else {
                        open();
                    }
                } else {
                    open();
                }
                break;

            case ACTION_CLOSE:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't close things while you're in your shell." ) );
                } else if( u.is_mounted() ) {
                    auto mon = u.mounted_creature.get();
                    if( !mon->has_flag( MF_MOUNTABLE_DOORS ) ) {
                        add_msg( m_info, _( "You can't close things while you're riding." ) );
                        break;
                    } else {
                        close();
                    }
                } else if( mouse_target ) {
                    doors::close_door( m, u, *mouse_target );
                } else {
                    close();
                }
                break;

            case ACTION_SMASH:
                if( veh_ctrl ) {
                    handbrake();
                } else if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't smash things while you're in your shell." ) );
                } else {
                    smash();
                }
                break;

            case ACTION_EXAMINE:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't examine your surroundings while you're in your "
                                        "shell." ) );
                } else if( mouse_target ) {
                    examine( *mouse_target );
                } else {
                    examine();
                }
                break;

            case ACTION_ADVANCEDINV:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't move mass quantities while you're in your "
                                        "shell." ) );
                } else if( u.is_mounted() ) {
                    add_msg( m_info, _( "You can't move mass quantities while you're riding." ) );
                } else {
                    create_advanced_inv();
                }
                break;

            case ACTION_PICKUP:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't pick anything up while you're in your shell." ) );
                } else if( u.is_mounted() ) {
                    add_msg( m_info, _( "You can't pick anything up while you're riding." ) );
                } else if( mouse_target ) {
                    pickup( *mouse_target );
                } else {
                    pickup();
                }
                break;

            case ACTION_PICKUP_ALL:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't pick anything up while you're in your shell." ) );
                } else if( u.is_mounted() ) {
                    add_msg( m_info, _( "You can't pick anything up while you're riding." ) );
                } else {
                    pickup_all();
                }
                break;

            case ACTION_PICKUP_FEET:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't pick anything up while you're in your shell." ) );
                } else {
                    pickup_feet();
                }
                break;

            case ACTION_GRAB:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't grab things while you're in your shell." ) );
                } else if( u.is_mounted() ) {
                    auto mon = u.mounted_creature.get();
                    if( !mon->has_flag( MF_RIDEABLE_MECH ) ) {
                        add_msg( m_info, _( "You can't grab things while you're riding." ) );
                        break;
                    } else if( !mon->type->mech_weapon.is_empty() ) {
                        add_msg( m_info, _( "Your mech doesn't have hands to grab with." ) );
                        break;
                    } else {
                        grab();
                    }
                } else {
                    grab();
                }
                break;

            case ACTION_HAUL:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't haul things while you're in your shell." ) );
                } else if( u.is_mounted() ) {
                    add_msg( m_info, _( "You can't haul things while you're riding." ) );
                } else {
                    haul();
                }
                break;

            case ACTION_BUTCHER:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't butcher while you're in your shell." ) );
                } else if( u.is_mounted() ) {
                    add_msg( m_info, _( "You can't butcher while you're riding." ) );
                } else {
                    butcher();
                }
                break;

            case ACTION_CHAT:
                chat();
                break;

            case ACTION_PEEK:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't peek around corners while you're in your shell." ) );
                } else if( u.is_mounted() ) {
                    add_msg( m_info, _( "You can't peek around corners while you're riding." ) );
                } else {
                    peek();
                }
                break;

            case ACTION_LIST_ITEMS:
                list_items_monsters();
                break;

            case ACTION_ZONES:
                zones_manager();
                break;

            case ACTION_LOOT:
                loot();
                break;

            case ACTION_INVENTORY:
                game_menus::inv::common( u );
                break;

            case ACTION_COMPARE:
                game_menus::inv::compare( u, std::nullopt );
                break;

            case ACTION_ORGANIZE:
                game_menus::inv::swap_letters( u );
                break;

            case ACTION_USE:
                // Shell-users are presumed to be able to mess with their inventories, etc
                // while in the shell.  Eating, gear-changing, and item use are OK.
                avatar_action::use_item( u );
                break;

            case ACTION_USE_WIELDED:
                avatar_funcs::use_item( u, u.primary_weapon() );
                break;

            case ACTION_WEAR:
                wear();
                break;

            case ACTION_TAKE_OFF:
                takeoff();
                break;

            case ACTION_EAT:
                if( !avatar_action::eat_here( u ) ) { avatar_action::eat( u ); }
                break;

            case ACTION_OPEN_CONSUME:
                if( !avatar_action::eat_here( u ) ) { open_consume_item_menu(); }
                break;

            case ACTION_READ:
                // Shell-users are presumed to have the book just at an opening and read it that way
                read();
                break;

            case ACTION_WIELD:
                avatar_action::wield();
                break;

            case ACTION_PICK_STYLE:
                u.martial_arts_data->pick_style( u );
                break;

            case ACTION_RELOAD_ITEM:
                avatar_action::reload_item();
                break;

            case ACTION_RELOAD_WEAPON:
                avatar_action::reload_weapon();
                break;

            case ACTION_RELOAD_WIELDED:
                avatar_action::reload_wielded();
                break;

            case ACTION_UNLOAD:
                avatar_action::unload( u );
                break;

            case ACTION_UNLOAD_ALL:
                avatar_action::unload_all( u );
                break;

            case ACTION_MEND:
                avatar_action::mend( g->u, nullptr );
                break;

            case ACTION_THROW: {
                // Quick-throw: use active throw slot if available
                if( auto *throwable = g->u.get_active_throwable() ) {
                    avatar_action::plthrow( g->u, throwable );
                    break;
                }
                avatar_action::plthrow( g->u, nullptr );
                break;
            }

            case ACTION_THROW_QUICKSLOT:
                do_throw_quickslot( u );
                break;

            case ACTION_FIRE:
                fire();
                break;

            case ACTION_AIM_HOLD:
                if( u.is_armed() && u.primary_weapon().is_gun() ) {
                    avatar_action::fire_wielded_weapon( u );
                }
                break;

            case ACTION_CAST_SPELL:
                cast_spell();
                break;
            case ACTION_CAST_LAST_SPELL:
                cast_last_spell();
                break;

            case ACTION_FIRE_BURST: {
                if( u.primary_weapon().gun_set_mode( gun_mode_id( "AUTO" ) ) ) {
                    avatar_action::fire_wielded_weapon( u );
                }
                break;
            }

            case ACTION_SELECT_FIRE_MODE:
                if( u.is_armed() && u.primary_weapon().is_gun()
                    && !u.primary_weapon().is_gunmod() ) {
                    if( u.primary_weapon().gun_all_modes().size() > 1 ) {
                        u.primary_weapon().gun_cycle_mode();
                    } else {
                        add_msg( m_info, _( "Your %s has only one firing mode." ),
                                 u.primary_weapon().display_name() );
                    }
                }
                break;

            case ACTION_SELECT_DEFAULT_AMMO:
                if( u.is_armed() && u.primary_weapon().is_gun()
                    && !u.primary_weapon().is_gunmod() ) {
                    ranged::prompt_select_default_ammo_for( u, u.primary_weapon() );
                }
                break;

            case ACTION_DROP:
                // You CAN drop things to your own tile while in the shell.
                drop();
                break;

            case ACTION_DIR_DROP:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't drop things to another tile while you're in your "
                                        "shell." ) );
                } else {
                    drop_in_direction();
                }
                break;
            case ACTION_BIONICS:
                show_bionics_ui( u );
                break;
            case ACTION_MUTATIONS:
                show_mutations_ui( u );
                break;

            case ACTION_SORT_ARMOR:
                show_armor_layers_ui( u );
                break;

            case ACTION_WAIT:
                wait();
                break;

            case ACTION_CRAFT:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't craft while you're in your shell." ) );
                } else if( u.is_mounted() ) {
                    add_msg( m_info, _( "You can't craft while you're riding." ) );
                } else {
                    u.craft();
                }
                break;

            case ACTION_RECRAFT:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't craft while you're in your shell." ) );
                } else if( u.is_mounted() ) {
                    add_msg( m_info, _( "You can't craft while you're riding." ) );
                } else {
                    u.recraft();
                }
                break;

            case ACTION_LONGCRAFT:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't craft while you're in your shell." ) );
                } else if( u.is_mounted() ) {
                    add_msg( m_info, _( "You can't craft while you're riding." ) );
                } else {
                    u.long_craft();
                }
                break;

            case ACTION_DISASSEMBLE:
                if( u.controlling_vehicle ) {
                    add_msg( m_info, _( "You can't disassemble items while driving." ) );
                } else if( u.is_mounted() ) {
                    add_msg( m_info, _( "You can't disassemble items while you're riding." ) );
                } else {
                    crafting::disassemble( u );
                }
                break;

            case ACTION_SALVAGE:
                if( u.controlling_vehicle ) {
                    add_msg( m_info, _( "You can't salvage items while driving." ) );
                } else if( u.is_mounted() ) {
                    add_msg( m_info, _( "You can't salvage items while you're riding." ) );
                } else {
                    salvage::menu_salvage_single( u );
                }
                break;

            case ACTION_CONSTRUCT:
                if( u.in_vehicle ) {
                    add_msg( m_info, _( "You can't construct while in a vehicle." ) );
                } else if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't construct while you're in your shell." ) );
                } else if( u.is_mounted() ) {
                    add_msg( m_info, _( "You can't construct while you're riding." ) );
                } else {
                    construction_menu( false );
                }
                break;

            case ACTION_SLEEP:
                if( veh_ctrl ) {
                    add_msg( m_info, _( "Vehicle control has moved, %s" ),
                             press_x( ACTION_CONTROL_VEHICLE, _( "new binding is " ),
                                      _( "new default binding is '^'." ) ) );
                } else {
                    sleep();
                }
                break;

            case ACTION_CONTROL_VEHICLE:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't operate a vehicle while you're in your shell." ) );
                } else if( u.is_mounted() ) {
                    u.dismount();
                } else {
                    control_vehicle();
                }
                break;

            case ACTION_TOGGLE_AUTO_TRAVEL_MODE:
                auto_travel_mode = !auto_travel_mode;
                add_msg( m_info,
                         auto_travel_mode ? _( "Auto travel mode ON!" ) : _( "Auto travel mode OFF!" ) );
                break;

            case ACTION_TOGGLE_SAFEMODE:
                if( safe_mode == SAFE_MODE_OFF ) {
                    set_safe_mode( SAFE_MODE_ON );
                    mostseen = 0;
                    add_msg( m_info, _( "Safe mode ON!" ) );
                } else {
                    turnssincelastmon = 0;
                    set_safe_mode( SAFE_MODE_OFF );
                    add_msg( m_info,
                             get_option<bool>( "AUTOSAFEMODE" )
                             ? _( "Safe mode OFF!  (Auto safe mode still enabled!)" )
                             : _( "Safe mode OFF!" ) );
                }
                if( u.has_effect( effect_laserlocked ) ) {
                    u.remove_effect( effect_laserlocked );
                    safe_mode_warning_logged = false;
                }
                break;

            case ACTION_TOGGLE_AUTOSAFE: {
                auto& autosafemode_option = get_options().get_option( "AUTOSAFEMODE" );
                add_msg( m_info,
                         autosafemode_option.value_as<bool>()
                         ? _( "Auto safe mode OFF!" )
                         : _( "Auto safe mode ON!" ) );
                autosafemode_option.setNext();
                break;
            }

            case ACTION_IGNORE_ENEMY:
                if( safe_mode == SAFE_MODE_STOP ) {
                    add_msg( m_info, _( "Ignoring enemy!" ) );
                    for( auto& elem : u.get_mon_visible().new_seen_mon ) {
                        monster& critter = *elem;
                        critter.ignoring = rl_dist( u.bub_pos(), critter.bub_pos() );
                    }
                    set_safe_mode( SAFE_MODE_ON );
                } else if( u.has_effect( effect_laserlocked ) ) {
                    if( u.has_trait( trait_PROF_CHURL ) ) {
                        add_msg( m_warning, _( "You make the sign of the cross." ) );
                    } else {
                        add_msg( m_info, _( "Ignoring laser targeting!" ) );
                    }
                    u.remove_effect( effect_laserlocked );
                    safe_mode_warning_logged = false;
                }
                break;

            case ACTION_WHITELIST_ENEMY:
                if( safe_mode == SAFE_MODE_STOP && !get_safemode().empty() ) {
                    get_safemode().add_rule(
                        get_safemode().lastmon_whitelist, Attitude::A_ANY, 0, RULE_WHITELISTED );
                    add_msg( m_info, _( "Creature whitelisted: %s" ),
                             get_safemode().lastmon_whitelist );
                    set_safe_mode( SAFE_MODE_ON );
                    mostseen = 0;
                } else {
                    get_safemode().show();
                }
                break;

            case ACTION_SUICIDE:
                if( query_yn( _( "Commit suicide?" ) ) ) {
                    if( query_yn( _( "REALLY commit suicide?" ) ) ) {
                        u.apply_damage( &u, body_part_head, 99999 );
                        u.moves = 0;
                        u.place_corpse();
                        uquit = QUIT_SUICIDE;
                    }
                }
                break;

            case ACTION_SAVE:
                if( query_yn( _( "Save and quit?" ) ) ) {
                    if( save( true ) ) {
                        u.moves = 0;
                        uquit = QUIT_SAVED;
                    }
                }
                break;

            case ACTION_QUICKSAVE:
                quicksave();
                return false;

            case ACTION_QUICKLOAD:
                quickload();
                return false;

            case ACTION_PL_INFO:
                character_display::disp_info( u );
                break;

            case ACTION_MAP:
                ui::omap::display();
                break;

            case ACTION_SKY:
                if( m.is_outside( u.bub_pos() ) ) {
                    ui::omap::display_visible_weather();
                } else {
                    add_msg( m_info, _( "You can't see the sky from here." ) );
                }
                break;

            case ACTION_MISSIONS:
                list_missions();
                break;

            case ACTION_SCORES:
                show_scores_ui( *achievements_tracker_ptr, stats(), get_kill_tracker() );
                break;

            case ACTION_DIARY:
                diary::show_diary_ui( u.get_avatar_diary() );
                break;

            case ACTION_FACTIONS:
                faction_manager_ptr->display();
                break;

            case ACTION_MORALE:
                u.disp_morale();
                break;

            case ACTION_MESSAGES:
                Messages::display_messages();
                break;

            case ACTION_OPEN_WIKI:
                if( !get_option<std::string>( "WIKI_DOC_URL" ).empty() ) {
                    open_url( get_option<std::string>( "WIKI_DOC_URL" ) );
                } else {
                    add_msg( m_bad, _( "Invalid Wiki URL specified!" ) );
                }

                break;

            case ACTION_OPEN_HHG:
                if( !get_option<std::string>( "HHG_URL" ).empty() ) {
                    open_url( get_option<std::string>( "HHG_URL" ) + std::string( "/?t=UNDEAD_PEOPLE" ) );
                } else {
                    add_msg( m_bad, _( "Invalid Hitchhiker's Guide URL specified!" ) );
                }
                break;

            case ACTION_HELP:
                get_help().display_help();
                break;

            case ACTION_OPTIONS:
                get_options().show( true );
                break;

            case ACTION_AUTOPICKUP:
                get_auto_pickup().show();
                break;

            case ACTION_AUTONOTES:
                get_auto_notes_settings().show_gui();
                break;

            case ACTION_SAFEMODE:
                get_safemode().show();
                break;

            case ACTION_DISTRACTION_MANAGER:
                get_distraction_manager().show();
                break;

            case ACTION_COLOR:
                all_colors.show_gui();
                break;

            case ACTION_WORLD_MODS:
                world_generator->show_active_world_mods(
                    world_generator->active_world->info->active_mod_order );
                break;

            case ACTION_DEBUG:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) {
                    break; // don't do anything when sharing and not debugger
                }
                debug_menu::debug();
                break;

            case ACTION_LUA_CONSOLE:
                cata::show_lua_console();
                break;

            case ACTION_LUA_RELOAD:
                cata::reload_lua_code();
                break;

            case ACTION_TOGGLE_FULLSCREEN:
                toggle_fullscreen();
                break;

            case ACTION_TOGGLE_PIXEL_MINIMAP:
                toggle_pixel_minimap();
                break;

            case ACTION_TOGGLE_PANEL_ADM:
                panel_manager::get_manager().show_adm();
                break;

            case ACTION_TOGGLE_SOMA_DETAIL:
                sidebar_hud_toggle_soma_detail();
                break;

            case ACTION_RELOAD_TILESET:
                reload_tileset( []( const std::string & str ) { DebugLog( DL::Info, DC::Main ) << str; } );
                break;

            case ACTION_TOGGLE_AUTO_FEATURES:
                get_options().get_option( "AUTO_FEATURES" ).setNext();
                get_options().save();
                //~ Auto Features are now ON/OFF
                add_msg( _( "%s are now %s." ),
                         get_options().get_option( "AUTO_FEATURES" ).getMenuText(),
                         get_option<bool>( "AUTO_FEATURES" ) ? _( "ON" ) : _( "OFF" ) );
                break;

            case ACTION_TOGGLE_AUTO_PULP_BUTCHER:
                get_options().get_option( "AUTO_PULP_BUTCHER" ).setNext();
                get_options().save();
                //~ Auto Pulp/Pulp Adjacent/Butcher is now set to x
                add_msg( _( "%s is now set to %s." ),
                         get_options().get_option( "AUTO_PULP_BUTCHER" ).getMenuText(),
                         get_options().get_option( "AUTO_PULP_BUTCHER" ).getValueName() );
                break;

            case ACTION_TOGGLE_AUTO_MINING:
                get_options().get_option( "AUTO_MINING" ).setNext();
                get_options().save();
                //~ Auto Mining is now ON/OFF
                add_msg( _( "%s is now %s." ), get_options().get_option( "AUTO_MINING" ).getMenuText(),
                         get_option<bool>( "AUTO_MINING" ) ? _( "ON" ) : _( "OFF" ) );
                break;

            case ACTION_TOGGLE_THIEF_MODE:
                if( g->u.get_value( "THIEF_MODE" ) == "THIEF_ASK" ) {
                    u.set_value( "THIEF_MODE", "THIEF_HONEST" );
                    u.set_value( "THIEF_MODE_KEEP", "YES" );
                    //~ Thief mode cycled between THIEF_ASK/THIEF_HONEST/THIEF_STEAL
                    add_msg( _( "You will not pick up other peoples belongings." ) );
                } else if( g->u.get_value( "THIEF_MODE" ) == "THIEF_HONEST" ) {
                    u.set_value( "THIEF_MODE", "THIEF_STEAL" );
                    u.set_value( "THIEF_MODE_KEEP", "YES" );
                    //~ Thief mode cycled between THIEF_ASK/THIEF_HONEST/THIEF_STEAL
                    add_msg( _( "You will pick up also those things that belong to others!" ) );
                } else if( g->u.get_value( "THIEF_MODE" ) == "THIEF_STEAL" ) {
                    u.set_value( "THIEF_MODE", "THIEF_ASK" );
                    u.set_value( "THIEF_MODE_KEEP", "NO" );
                    //~ Thief mode cycled between THIEF_ASK/THIEF_HONEST/THIEF_STEAL
                    add_msg( _( "You will be reminded not to steal." ) );
                } else {
                    // ERROR
                    add_msg( _( "THIEF_MODE CONTAINED BAD VALUE [ %s ]!" ), g->u.get_value( "THIEF_"
                                "MODE" ) );
                }
                break;

            case ACTION_TOGGLE_AUTO_FORAGING:
                get_options().get_option( "AUTO_FORAGING" ).setNext();
                get_options().save();
                //~ Auto Foraging is now set to x
                add_msg( _( "%s is now set to %s." ),
                         get_options().get_option( "AUTO_FORAGING" ).getMenuText(),
                         get_options().get_option( "AUTO_FORAGING" ).getValueName() );
                break;

            case ACTION_TOGGLE_AUTO_PICKUP:
                get_options().get_option( "AUTO_PICKUP" ).setNext();
                get_options().save();
                //~ Auto pickup is now set to x
                add_msg( _( "%s is now set to %s." ),
                         get_options().get_option( "AUTO_PICKUP" ).getMenuText(),
                         get_options().get_option( "AUTO_PICKUP" ).getValueName() );
                break;

            case ACTION_DISPLAY_SCENT:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) {
                    break; // don't do anything when sharing and not debugger
                }
                display_scent();
                break;

            case ACTION_DISPLAY_SCENT_TYPE:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) {
                    break; // don't do anything when sharing and not debugger
                }
                display_scent();
                break;

            case ACTION_DISPLAY_TEMPERATURE:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) {
                    break; // don't do anything when sharing and not debugger
                }
                display_temperature();
                break;
            case ACTION_DISPLAY_VEHICLE_AI:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) {
                    break; // don't do anything when sharing and not debugger
                }
                display_vehicle_ai();
                break;
            case ACTION_DISPLAY_VISIBILITY:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) {
                    break; // don't do anything when sharing and not debugger
                }
                display_visibility();
                break;

            case ACTION_DISPLAY_LIGHTING:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) {
                    break; // don't do anything when sharing and not debugger
                }
                display_lighting();
                break;

            case ACTION_DISPLAY_RADIATION:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) {
                    break; // don't do anything when sharing and not debugger
                }
                display_radiation();
                break;

            case ACTION_DISPLAY_TRANSPARENCY:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) {
                    break; // don't do anything when sharing and not debugger
                }
                display_transparency();
                break;

            case ACTION_DISPLAY_OUTSIDE:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) {
                    break; // don't do anything when sharing and not debugger
                }
                display_outside();
                break;

            case ACTION_DISPLAY_SOUND:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) {
                    break; // don't do anything when sharing and not debugger
                }
                display_sound();
                break;

            case ACTION_DISPLAY_SOUND_ABSORPTION:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) {
                    break; // don't do anything when sharing and not debugger
                }
                display_sound_absorption();
                break;

            case ACTION_DISPLAY_SOUND_WALLS:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) {
                    break; // don't do anything when sharing and not debugger
                }
                display_sound_walls();
                break;

            case ACTION_DISPLAY_SUBMAP_GRID:
                g->debug_submap_grid_overlay = !g->debug_submap_grid_overlay;
                break;

            case ACTION_TOGGLE_ZONE_OVERLAY:
                g->show_zone_overlay = !g->show_zone_overlay;
                break;

            case ACTION_TOGGLE_HOUR_TIMER:
                toggle_debug_hour_timer();
                break;

            case ACTION_TOGGLE_FPS:
                toggle_debug_fps();
                break;

            case ACTION_TOGGLE_DEBUG_MODE:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) {
                    break; // don't do anything when sharing and not debugger
                }
                debug_mode = !debug_mode;
                if( debug_mode ) {
                    add_msg( m_info, _( "Debug mode ON!" ) );
                } else {
                    add_msg( m_info, _( "Debug mode OFF!" ) );
                }
                break;

            case ACTION_ZOOM_IN:
                zoom_in();
                mark_main_ui_adaptor_resize();
                break;

            case ACTION_ZOOM_OUT:
                zoom_out();
                mark_main_ui_adaptor_resize();
                break;

            case ACTION_ITEMACTION:
                item_action_menu();
                break;

            case ACTION_AUTOATTACK:
                avatar_action::autoattack( u, m );
                break;

            default:
                break;
        }
    }
    if( act != ACTION_TIMEOUT ) { u.mod_moves( -current_turn.moves_elapsed() ); }
    gamemode->post_action( act );

    u.movecounter = ( !u.is_dead_state() ? ( before_action_moves - u.moves ) : 0 );
    dbg( DL::Info ) << string_format(
                        "%s: [%d] %d - %d = %d", action_ident( act ), to_turn<int>( calendar::turn ),
                        before_action_moves, u.movecounter, u.moves );
    return ( !u.is_dead_state() );
}

/// Variant of handle_action() that takes a pre-resolved action string from the
/// non-blocking main loop, skipping the blocking get_player_input() call.
/// Modal-opening cases push to game::modal_fiber_ instead of calling directly;
/// all other cases execute inline identically to handle_action().
auto game::handle_action_from( const std::string& pre_action ) -> bool
{
    auto action = pre_action;
    input_context ctxt = get_default_mode_input_context();
    action_id act = ACTION_NULL;
    user_turn current_turn;
    // Capture position before any action executes.  Used at the end of the
    // function to detect whether a movement action was blocked (pos unchanged)
    // and skip queuing it — blocked moves in the ring buffer cause wall-flicker.
    const tripoint_bub_ms coop_pos_before_ = u.bub_pos();

    // Auto-move path (identical to handle_action)
    if( u.has_destination() ) {
        act = u.get_next_auto_move_direction();
        if( act == ACTION_NULL ) {
            add_msg( m_info, _( "Auto-move canceled" ) );
            u.clear_destination();
            return false;
        }
    } else if( u.has_destination_activity() ) {
        u.start_destination_activity();
        return false;
    }
    // (no blocking input call — action string comes from pre_action)

    const optional_vpart_position vp = m.veh_at( u.bub_pos() );
    const auto player_vehicle = vp ? &vp->vehicle() : nullptr;
    const bool local_vehicle_in_control = vp && vp->vehicle().player_in_control( u );
    const auto remote_vehicle = remoteveh();
    const auto controlled_vehicle =
        remote_vehicle != nullptr ? remote_vehicle
        : local_vehicle_in_control
        ? player_vehicle
        : nullptr;
    const auto veh_ctrl =
        !u.is_dead_state() && ( local_vehicle_in_control || remote_vehicle != nullptr );

    std::optional<tripoint_bub_ms> mouse_target;

    if( uquit == QUIT_WATCH && action == "QUIT" ) {
        uquit = QUIT_DIED;
        return false;
    }

    if( act == ACTION_NULL ) {
        act = look_up_action( action );

        if( act == ACTION_KEYBINDINGS ) { return false; }

        if( act == ACTION_MAIN_MENU ) {
            if( uquit == QUIT_WATCH ) { return false; }
            u.clear_destination();
            destination_preview.clear();
            act = handle_main_menu();
            if( act == ACTION_NULL ) { return false; }
        }

        if( act == ACTION_ACTIONMENU ) {
            if( uquit == QUIT_WATCH ) { return false; }
            u.clear_destination();
            destination_preview.clear();
            act = handle_action_menu();
            if( act == ACTION_NULL ) { return false; }
        }

        if( act == ACTION_KEYBINDINGS ) {
            u.clear_destination();
            destination_preview.clear();
            act = ctxt.display_menu( true );
            if( act == ACTION_NULL ) { return false; }
        }

        if( can_action_change_worldstate( act ) ) { user_action_counter += 1; }

        if( act != ACTION_TIMEOUT ) {
            u.clear_destination();
            destination_preview.clear();
        }
    }

    if( act == ACTION_NULL ) { return false; }
    {
        const auto& sess = coop_session::get();
        if( sess.is_client() && sess.is_downed ) {
            static const std::array<action_id, 5> allowed = {
                ACTION_PAUSE, ACTION_SAVE,
                ACTION_MESSAGES, ACTION_HELP, ACTION_OPTIONS
            };
            if( std::ranges::find( allowed, act ) == allowed.end() ) {
                add_msg( m_bad, _( "You can't act while critically wounded!" ) );
                return false;
            }
        }
    }

    gamemode->pre_action( act );

    int soffset = get_option<int>( "MOVE_VIEW_OFFSET" );

    int before_action_moves = u.moves;


    // These actions are allowed while deathcam is active
    if( uquit == QUIT_WATCH || !u.is_dead_state() ) {
        switch( act ) {
            case ACTION_TOGGLE_MAP_MEMORY:
                u.toggle_map_memory();
                break;

            case ACTION_CENTER:
                u.view_offset.x() = driving_view_offset.x;
                u.view_offset.y() = driving_view_offset.y;
                break;

            case ACTION_SHIFT_N:
            case ACTION_SHIFT_NE:
            case ACTION_SHIFT_E:
            case ACTION_SHIFT_SE:
            case ACTION_SHIFT_S:
            case ACTION_SHIFT_SW:
            case ACTION_SHIFT_W:
            case ACTION_SHIFT_NW: {
                static const std::map<action_id, std::pair<point, point>> shift_delta = {
                    {ACTION_SHIFT_N, {point_north, point_north_east}},
                    {ACTION_SHIFT_NE, {point_north_east, point_east}},
                    {ACTION_SHIFT_E, {point_east, point_south_east}},
                    {ACTION_SHIFT_SE, {point_south_east, point_south}},
                    {ACTION_SHIFT_S, {point_south, point_south_west}},
                    {ACTION_SHIFT_SW, {point_south_west, point_west}},
                    {ACTION_SHIFT_W, {point_west, point_north_west}},
                    {ACTION_SHIFT_NW, {point_north_west, point_north}},
                };
                u.view_offset +=
                    tile_iso ? shift_delta.at( act ).second * soffset
                    : shift_delta.at( act ).first * soffset;
            }
            break;

            case ACTION_LOOK:
                // Fiber-wrapped: world ticks while looking around
                modal_fiber_.emplace( [this]() { look_around(); } );
                break;

            case ACTION_KEYBINDINGS:
                break;

            default:
                break;
        }
    }

    // Actions allowed only while alive

    if( !u.is_dead_state() ) {
        switch( act ) {
            case ACTION_NULL:
            case NUM_ACTIONS:
                break;
            case ACTION_ACTIONMENU:
            case ACTION_MAIN_MENU:
            case ACTION_KEYBINDINGS:
                break;

            case ACTION_TIMEOUT:
                if( check_safe_mode_allowed( false ) ) { character_funcs::do_pause( u ); }
                break;

            case ACTION_PAUSE:
                if( check_safe_mode_allowed() ) { character_funcs::do_pause( u ); }
                break;

            case ACTION_CYCLE_MOVE:
                u.cycle_move_mode();
                break;

            case ACTION_RESET_MOVE:
                u.reset_move_mode();
                break;

            case ACTION_TOGGLE_RUN:
                u.toggle_run_mode();
                break;

            case ACTION_TOGGLE_CROUCH:
                u.toggle_crouch_mode();
                break;

            case ACTION_OPEN_MOVEMENT:
                modal_fiber_.emplace( [this]() { open_movement_mode_menu(); } );
                break;

            case ACTION_MOVE_FORTH:
            case ACTION_MOVE_FORTH_RIGHT:
            case ACTION_MOVE_RIGHT:
            case ACTION_MOVE_BACK_RIGHT:
            case ACTION_MOVE_BACK:
            case ACTION_MOVE_BACK_LEFT:
            case ACTION_MOVE_LEFT:
            case ACTION_MOVE_FORTH_LEFT:
                if( !u.get_value( "remote_controlling" ).empty()
                    && ( u.has_active_item_with_action( "RADIOCONTROL" )
                         || u.has_active_bionic( bio_remote ) ) ) {
                    rcdrive( get_delta_from_movement_action( act, iso_rotate::yes ) );
                } else if( veh_ctrl ) {
                    pldrive( get_delta_from_movement_action( act, iso_rotate::no )
                             .reinterpret_as<point_rel_veh>() );
                } else {
                    auto dest_delta = make_player_move_cmd( act, iso_rotate::yes ).delta.xy();
                    if( auto_travel_mode && !u.is_auto_moving() ) {
                        for( int i = 0; i < SEEX; i++ ) {
                            tripoint_bub_ms auto_travel_destination(
                                u.bub_pos().x() + dest_delta.x() * ( SEEX - i ),
                                u.bub_pos().y() + dest_delta.y() * ( SEEX - i ), u.bub_pos().z() );
                            destination_preview = m.route(
                                                      u.bub_pos(), auto_travel_destination,
                                                      u.get_legacy_pathfinding_settings(), u.get_legacy_path_avoid() );
                            if( !destination_preview.empty() ) {
                                destination_preview.erase(
                                    destination_preview.begin() + 1, destination_preview.end() );
                                u.set_destination( destination_preview );
                                break;
                            }
                        }
                        act = u.get_next_auto_move_direction();
                        const auto dest_next =
                            make_player_move_cmd( act, iso_rotate::yes ).delta.xy();
                        if( dest_next == point_rel_ms::zero() ) { u.clear_destination(); }
                        dest_delta = dest_next;
                    }
                    if( !avatar_action::move( u, m, dest_delta ) ) { u.clear_destination(); }
                }
                break;

            case ACTION_MOVE_DOWN:
                if( u.is_mounted() ) {
                    const monster* mon = u.mounted_creature.get();
                    bool ladder = m.has_flag( "DIFFICULT_Z", u.bub_pos() );

                    if( ladder ) {
                        const bool can_use_ladder =
                            mon->has_flag( MF_MOUNTABLE_LADDER ) || mon->has_flag( MF_FLIES );

                        if( !can_use_ladder ) {
                            add_msg( m_info, _( "Your mount can't go downstairs while riding." ) );
                            break;
                        }
                    } else {
                        const bool can_use_stairs =
                            mon->has_flag( MF_MOUNTABLE_STAIRS ) || mon->has_flag( MF_FLIES );

                        if( !can_use_stairs ) {
                            add_msg( m_info, _( "Your mount can't go downstairs while riding." ) );
                            break;
                        }
                    }
                }
                if( controlled_vehicle != nullptr && controlled_vehicle->is_aircraft() ) {
                    pldrive( tripoint_rel_veh::below() );
                } else if( !u.in_vehicle ) {
                    vertical_move( -1, false );
                } else if( get_map().has_rope_at( u.bub_pos() ) ) {
                    map& here = get_map();
                    const optional_vpart_position vp_down = here.veh_at( u.bub_pos() );
                    const int idx = vp_down->vehicle().part_with_feature(
                                        vp_down->part_index(), VPFLAG_LADDER, true );
                    if( idx != -1 ) {
                        const vpart_info info = vp_down->vehicle().part_info( idx );
                        auto where = u.bub_pos();
                        auto below = where;
                        if( get_map().ter( where ).id().str() != "t_open_air" ) { break; }
                        below.z()--;
                        while( get_map().ter( below ).id().str() == "t_open_air" ) {
                            where.z()--;
                            below.z()--;
                        }
                        const int dist = u.bub_pos().z() - below.z();
                        if( info.ladder_length() >= dist ) {
                            get_map().unboard_vehicle( u.bub_pos() );
                            vertical_move( -dist, true );
                        }
                    }
                }
                break;

            case ACTION_MOVE_UP:
                if( u.is_mounted() ) {
                    const monster* mon = u.mounted_creature.get();
                    bool ladder = m.has_flag( "DIFFICULT_Z", u.bub_pos() );

                    if( ladder ) {
                        const bool can_use_ladder =
                            mon->has_flag( MF_MOUNTABLE_LADDER ) || mon->has_flag( MF_FLIES );

                        if( !can_use_ladder ) {
                            add_msg( m_info, _( "Your mount can't go upstairs or climb while "
                                                "riding." ) );
                            break;
                        }
                    } else {
                        const bool can_use_stairs =
                            mon->has_flag( MF_MOUNTABLE_STAIRS ) || mon->has_flag( MF_FLIES );

                        if( !can_use_stairs ) {
                            add_msg( m_info, _( "Your mount can't go upstairs or climb while "
                                                "riding." ) );
                            break;
                        }
                    }
                }
                if( controlled_vehicle != nullptr ) {
                    if( controlled_vehicle->is_aircraft() ) {
                        pldrive( tripoint_rel_veh::above() );
                    } else if(
                        ( controlled_vehicle->has_part( "ROTOR" )
                          || controlled_vehicle->has_part( "BALLOON" )
                          || controlled_vehicle->has_part( "WING" ) )
                        && !controlled_vehicle->has_sufficient_lift() ) {
                        add_msg( m_bad, _( "The craft struggles to generate enough lift!" ) );
                    } else {
                        u.add_msg_if_player( _( "You need a propeller to take off!" ) );
                    }
                } else if( !u.in_vehicle ) {
                    if( get_map().has_rope_at( u.bub_pos() ) ) {
                        auto xy = u.bub_pos().xy();
                        map& here = get_map();
                        auto where = u.bub_pos();
                        auto above = where;
                        above.z()++;
                        if( get_map().ter( above ).id().str() != "t_open_air" ) {
                            vertical_move( 1, false );
                            break;
                        }
                        while( get_map().ter( above ).id().str() == "t_open_air"
                               && !here.veh_at( tripoint_bub_ms( xy, above.z() ) ) ) {
                            above.z()++;
                        }
                        const optional_vpart_position vp_up = here.veh_at(
                                tripoint_bub_ms( xy, above.z() ) );
                        const int dist = above.z() - u.bub_pos().z();
                        if( vp_up ) {
                            const int idx = vp_up->vehicle().part_with_feature(
                                                vp_up->part_index(), VPFLAG_LADDER, true );
                            if( idx != -1 ) {
                                const vpart_info info = vp_up->vehicle().part_info( idx );
                                if( info.ladder_length() >= dist ) {
                                    vertical_move( dist, true );
                                    here.board_vehicle( u.bub_pos(), u.as_character() );
                                    break;
                                }
                            }
                        } else {
                            vertical_move( 1, false );
                        }
                    } else {
                        vertical_move( 1, false );
                    }
                } else {
                    u.add_msg_if_player( _( "You need a propeller to take off!" ) );
                }
                break;

            case ACTION_OPEN:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't open things while you're in your shell." ) );
                } else if( u.is_mounted() ) {
                    auto mon = u.mounted_creature.get();
                    if( !mon->has_flag( MF_MOUNTABLE_DOORS ) ) {
                        add_msg( m_info, _( "You can't open things while you're riding." ) );
                        break;
                    } else {
                        open();
                    }
                } else {
                    open();
                }
                break;

            case ACTION_CLOSE:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't close things while you're in your shell." ) );
                } else if( u.is_mounted() ) {
                    auto mon = u.mounted_creature.get();
                    if( !mon->has_flag( MF_MOUNTABLE_DOORS ) ) {
                        add_msg( m_info, _( "You can't close things while you're riding." ) );
                        break;
                    } else {
                        close();
                    }
                } else if( mouse_target ) {
                    doors::close_door( m, u, *mouse_target );
                } else {
                    close();
                }
                break;

            case ACTION_SMASH:
                if( veh_ctrl ) {
                    handbrake();
                } else if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't smash things while you're in your shell." ) );
                } else {
                    smash();
                }
                break;

            case ACTION_EXAMINE:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't examine your surroundings while you're in your "
                                        "shell." ) );
                } else if( mouse_target ) {
                    examine( *mouse_target );
                } else {
                    examine();
                }
                break;

            case ACTION_ADVANCEDINV:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't move mass quantities while you're in your "
                                        "shell." ) );
                } else if( u.is_mounted() ) {
                    add_msg( m_info, _( "You can't move mass quantities while you're riding." ) );
                } else {
                    modal_fiber_.emplace( [this]() { create_advanced_inv(); } );
                }
                break;

            case ACTION_PICKUP:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't pick anything up while you're in your shell." ) );
                } else if( u.is_mounted() ) {
                    add_msg( m_info, _( "You can't pick anything up while you're riding." ) );
                } else if( mouse_target ) {
                    pickup( *mouse_target );
                } else {
                    pickup();
                }
                break;

            case ACTION_PICKUP_ALL:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't pick anything up while you're in your shell." ) );
                } else if( u.is_mounted() ) {
                    add_msg( m_info, _( "You can't pick anything up while you're riding." ) );
                } else {
                    pickup_all();
                }
                break;

            case ACTION_PICKUP_FEET:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't pick anything up while you're in your shell." ) );
                } else {
                    pickup_feet();
                }
                break;

            case ACTION_GRAB:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't grab things while you're in your shell." ) );
                } else if( u.is_mounted() ) {
                    auto mon = u.mounted_creature.get();
                    if( !mon->has_flag( MF_RIDEABLE_MECH ) ) {
                        add_msg( m_info, _( "You can't grab things while you're riding." ) );
                        break;
                    } else if( !mon->type->mech_weapon.is_empty() ) {
                        add_msg( m_info, _( "Your mech doesn't have hands to grab with." ) );
                        break;
                    } else {
                        grab();
                    }
                } else {
                    grab();
                }
                break;

            case ACTION_HAUL:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't haul things while you're in your shell." ) );
                } else if( u.is_mounted() ) {
                    add_msg( m_info, _( "You can't haul things while you're riding." ) );
                } else {
                    haul();
                }
                break;

            case ACTION_BUTCHER:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't butcher while you're in your shell." ) );
                } else if( u.is_mounted() ) {
                    add_msg( m_info, _( "You can't butcher while you're riding." ) );
                } else {
                    butcher();
                }
                break;

            case ACTION_CHAT:
                modal_fiber_.emplace( [this]() { chat(); } );
                break;

            case ACTION_PEEK:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't peek around corners while you're in your shell." ) );
                } else if( u.is_mounted() ) {
                    add_msg( m_info, _( "You can't peek around corners while you're riding." ) );
                } else {
                    modal_fiber_.emplace( [this]() { peek(); } );
                }
                break;

            case ACTION_LIST_ITEMS:
                modal_fiber_.emplace( [this]() { list_items_monsters(); } );
                break;

            case ACTION_ZONES:
                modal_fiber_.emplace( [this]() { zones_manager(); } );
                break;

            case ACTION_LOOT:
                modal_fiber_.emplace( [this]() { loot(); } );
                break;

            case ACTION_INVENTORY:
                modal_fiber_.emplace( [this]() { game_menus::inv::common( u ); } );
                break;

            case ACTION_COMPARE:
                modal_fiber_.emplace( [this]() { game_menus::inv::compare( u, std::nullopt ); } );
                break;

            case ACTION_ORGANIZE:
                modal_fiber_.emplace( [this]() { game_menus::inv::swap_letters( u ); } );
                break;

            case ACTION_USE:
                avatar_action::use_item( u );
                break;

            case ACTION_USE_WIELDED:
                avatar_funcs::use_item( u, u.primary_weapon() );
                break;

            case ACTION_WEAR:
                wear();
                break;

            case ACTION_TAKE_OFF:
                takeoff();
                break;

            case ACTION_EAT:
                if( !avatar_action::eat_here( u ) ) { avatar_action::eat( u ); }
                break;

            case ACTION_OPEN_CONSUME:
                if( !avatar_action::eat_here( u ) ) { open_consume_item_menu(); }
                break;

            case ACTION_READ:
                read();
                break;

            case ACTION_WIELD:
                avatar_action::wield();
                break;

            case ACTION_PICK_STYLE:
                u.martial_arts_data->pick_style( u );
                break;

            case ACTION_RELOAD_ITEM:
                avatar_action::reload_item();
                break;

            case ACTION_RELOAD_WEAPON:
                avatar_action::reload_weapon();
                break;

            case ACTION_RELOAD_WIELDED:
                avatar_action::reload_wielded();
                break;

            case ACTION_UNLOAD:
                avatar_action::unload( u );
                break;

            case ACTION_UNLOAD_ALL:
                avatar_action::unload_all( u );
                break;

            case ACTION_MEND:
                avatar_action::mend( g->u, nullptr );
                break;

            case ACTION_THROW: {
                // C2e (deferred): throw relay must hook in throw_activity_actor::do_turn()
                // (activity_actor.cpp) right after throw_item() — plthrow() here only assigns
                // the activity.  Requires live COOP session to verify.  Molotovs/grenades
                // also need separate field-relay; deferred to Phase 9.
                if( auto *throwable = g->u.get_active_throwable() ) {
                    avatar_action::plthrow( g->u, throwable );
                    break;
                }
                avatar_action::plthrow( g->u, nullptr );
                break;
            }

            case ACTION_THROW_QUICKSLOT:
                do_throw_quickslot( u );
                break;

            case ACTION_FIRE:
                modal_fiber_.emplace( [this]() {
                    // COOP: avatar_action::fire_wielded_weapon() (called by fire()) assigns
                    // ACT_AIM to u.activity then returns immediately.  If the fiber ends
                    // there, target_ui::run() runs later via aim_activity_actor::do_turn
                    // inside process_activity() — outside any fiber — where
                    // ctxt.handle_input() blocks on get_input_event() directly.
                    //
                    // Fix: after fire() assigns ACT_AIM, move it out of u.activity with
                    // release() so the world-tick's process_activity() sees the null-type
                    // activity_ptr and returns at game.cpp:2294, then drive the actor here
                    // inside the fiber where yield_event() suspends instead of blocking.
                    //
                    // Exception safety: if fiber_cancelled unwinds from yield_event() during
                    // fiber teardown, u.activity already holds the null-type placeholder from
                    // release() — process_activity()'s early-return guard holds with no flag
                    // to reset.  The loop also handles AIM_AFTER_FIRING: finish() may
                    // re-assign ACT_AIM to u.activity; we pick it up in the next iteration.
                    fire();
                    while( u.activity && u.activity->id() == activity_id( "ACT_AIM" ) ) {
                        auto local_act = u.activity.release(); // nulls u.activity; blocks
                        // process_activity()
                        local_act->do_turn( u ); // runs target_ui::run() inside this fiber
                    }
                    // A5.3: queue FIRE to host proxy AFTER aim completes so that
                    // u.last_target_pos (tripoint_abs_ms, set at ranged.cpp:3303) is valid.
                    // Absolute coords are globally consistent — bub coords differ per machine.
                    if( coop_client_ && u.last_target_pos.has_value() ) {
                        const auto& tap = *u.last_target_pos;
                        // CL-RANGED: embed weapon+ammo at fire time so the server arms the
                        // proxy with exactly the weapon the client fired, not a stale heartbeat.
                        std::ostringstream ctx_oss;
                        JsonOut ctx_jout( ctx_oss );
                        ctx_jout.start_object();
                        ctx_jout.member( "tx", tap.x() );
                        ctx_jout.member( "ty", tap.y() );
                        ctx_jout.member( "tz", tap.z() );
                        if( u.is_armed() && u.primary_weapon().is_gun() ) {
                            ctx_jout.member( "weapon_id", u.primary_weapon().typeId().str() );
                            const itype_id cur_ammo = u.primary_weapon().ammo_current();
                            if( !cur_ammo.is_null() ) {
                                ctx_jout.member( "ammo_id", cur_ammo.str() );
                            }
                        }
                        ctx_jout.end_object();
                        coop_client_->queue_action( "FIRE", ctx_oss.str() );
                    }
                } );
                break;

            case ACTION_AIM_HOLD:
                if( u.is_armed() && u.primary_weapon().is_gun() ) {
                    avatar_action::fire_wielded_weapon( u );
                }
                break;

            case ACTION_CAST_SPELL:
                modal_fiber_.emplace( [this]() { cast_spell(); } );
                break;

            case ACTION_CAST_LAST_SPELL:
                cast_last_spell();
                break;

            case ACTION_FIRE_BURST: {
                if( u.primary_weapon().gun_set_mode( gun_mode_id( "AUTO" ) ) ) {
                    avatar_action::fire_wielded_weapon( u );
                }
                break;
            }

            case ACTION_SELECT_FIRE_MODE:
                if( u.is_armed() && u.primary_weapon().is_gun()
                    && !u.primary_weapon().is_gunmod() ) {
                    if( u.primary_weapon().gun_all_modes().size() > 1 ) {
                        u.primary_weapon().gun_cycle_mode();
                    } else {
                        add_msg( m_info, _( "Your %s has only one firing mode." ),
                                 u.primary_weapon().display_name() );
                    }
                }
                break;

            case ACTION_SELECT_DEFAULT_AMMO:
                if( u.is_armed() && u.primary_weapon().is_gun()
                    && !u.primary_weapon().is_gunmod() ) {
                    ranged::prompt_select_default_ammo_for( u, u.primary_weapon() );
                }
                break;

            case ACTION_DROP:
                drop();
                break;

            case ACTION_DIR_DROP:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't drop things to another tile while you're in your "
                                        "shell." ) );
                } else {
                    drop_in_direction();
                }
                break;

            case ACTION_BIONICS:
                modal_fiber_.emplace( [this]() { show_bionics_ui( u ); } );
                break;

            case ACTION_MUTATIONS:
                modal_fiber_.emplace( [this]() { show_mutations_ui( u ); } );
                break;

            case ACTION_SORT_ARMOR:
                modal_fiber_.emplace( [this]() { show_armor_layers_ui( u ); } );
                break;

            case ACTION_WAIT:
                wait();
                break;

            case ACTION_CRAFT:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't craft while you're in your shell." ) );
                } else if( u.is_mounted() ) {
                    add_msg( m_info, _( "You can't craft while you're riding." ) );
                } else {
                    u.craft();
                }
                break;

            case ACTION_RECRAFT:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't craft while you're in your shell." ) );
                } else if( u.is_mounted() ) {
                    add_msg( m_info, _( "You can't craft while you're riding." ) );
                } else {
                    u.recraft();
                }
                break;

            case ACTION_LONGCRAFT:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't craft while you're in your shell." ) );
                } else if( u.is_mounted() ) {
                    add_msg( m_info, _( "You can't craft while you're riding." ) );
                } else {
                    u.long_craft();
                }
                break;

            case ACTION_DISASSEMBLE:
                if( u.controlling_vehicle ) {
                    add_msg( m_info, _( "You can't disassemble items while driving." ) );
                } else if( u.is_mounted() ) {
                    add_msg( m_info, _( "You can't disassemble items while you're riding." ) );
                } else {
                    crafting::disassemble( u );
                }
                break;

            case ACTION_SALVAGE:
                if( u.controlling_vehicle ) {
                    add_msg( m_info, _( "You can't salvage items while driving." ) );
                } else if( u.is_mounted() ) {
                    add_msg( m_info, _( "You can't salvage items while you're riding." ) );
                } else {
                    salvage::menu_salvage_single( u );
                }
                break;

            case ACTION_CONSTRUCT:
                if( u.in_vehicle ) {
                    add_msg( m_info, _( "You can't construct while in a vehicle." ) );
                } else if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't construct while you're in your shell." ) );
                } else if( u.is_mounted() ) {
                    add_msg( m_info, _( "You can't construct while you're riding." ) );
                } else {
                    modal_fiber_.emplace( [this]() { construction_menu( false ); } );
                }
                break;

            case ACTION_SLEEP:
                if( veh_ctrl ) {
                    add_msg( m_info, _( "Vehicle control has moved, %s" ),
                             press_x( ACTION_CONTROL_VEHICLE, _( "new binding is " ),
                                      _( "new default binding is '^'." ) ) );
                } else {
                    sleep();
                }
                break;

            case ACTION_CONTROL_VEHICLE:
                if( u.has_active_mutation( trait_SHELL2 ) ) {
                    add_msg( m_info, _( "You can't operate a vehicle while you're in your shell." ) );
                } else if( u.is_mounted() ) {
                    u.dismount();
                } else {
                    modal_fiber_.emplace( [this]() { control_vehicle(); } );
                }
                break;

            case ACTION_TOGGLE_AUTO_TRAVEL_MODE:
                auto_travel_mode = !auto_travel_mode;
                add_msg( m_info,
                         auto_travel_mode ? _( "Auto travel mode ON!" ) : _( "Auto travel mode OFF!" ) );
                break;

            case ACTION_TOGGLE_SAFEMODE:
                if( safe_mode == SAFE_MODE_OFF ) {
                    set_safe_mode( SAFE_MODE_ON );
                    mostseen = 0;
                    add_msg( m_info, _( "Safe mode ON!" ) );
                } else {
                    turnssincelastmon = 0;
                    set_safe_mode( SAFE_MODE_OFF );
                    add_msg( m_info,
                             get_option<bool>( "AUTOSAFEMODE" )
                             ? _( "Safe mode OFF!  (Auto safe mode still enabled!)" )
                             : _( "Safe mode OFF!" ) );
                }
                if( u.has_effect( effect_laserlocked ) ) {
                    u.remove_effect( effect_laserlocked );
                    safe_mode_warning_logged = false;
                }
                break;

            case ACTION_TOGGLE_AUTOSAFE: {
                auto& autosafemode_option = get_options().get_option( "AUTOSAFEMODE" );
                add_msg( m_info,
                         autosafemode_option.value_as<bool>()
                         ? _( "Auto safe mode OFF!" )
                         : _( "Auto safe mode ON!" ) );
                autosafemode_option.setNext();
                break;
            }

            case ACTION_IGNORE_ENEMY:
                if( safe_mode == SAFE_MODE_STOP ) {
                    add_msg( m_info, _( "Ignoring enemy!" ) );
                    for( auto& elem : u.get_mon_visible().new_seen_mon ) {
                        monster& critter = *elem;
                        critter.ignoring = rl_dist( u.bub_pos(), critter.bub_pos() );
                    }
                    set_safe_mode( SAFE_MODE_ON );
                } else if( u.has_effect( effect_laserlocked ) ) {
                    if( u.has_trait( trait_PROF_CHURL ) ) {
                        add_msg( m_warning, _( "You make the sign of the cross." ) );
                    } else {
                        add_msg( m_info, _( "Ignoring laser targeting!" ) );
                    }
                    u.remove_effect( effect_laserlocked );
                    safe_mode_warning_logged = false;
                }
                break;

            case ACTION_WHITELIST_ENEMY:
                if( safe_mode == SAFE_MODE_STOP && !get_safemode().empty() ) {
                    get_safemode().add_rule(
                        get_safemode().lastmon_whitelist, Attitude::A_ANY, 0, RULE_WHITELISTED );
                    add_msg( m_info, _( "Creature whitelisted: %s" ),
                             get_safemode().lastmon_whitelist );
                    set_safe_mode( SAFE_MODE_ON );
                    mostseen = 0;
                } else {
                    get_safemode().show();
                }
                break;

            case ACTION_SUICIDE:
                if( query_yn( _( "Commit suicide?" ) ) ) {
                    if( query_yn( _( "REALLY commit suicide?" ) ) ) {
                        u.apply_damage( &u, body_part_head, 99999 );
                        u.moves = 0;
                        u.place_corpse();
                        uquit = QUIT_SUICIDE;
                    }
                }
                break;

            case ACTION_SAVE:
                if( query_yn( _( "Save and quit?" ) ) ) {
                    if( save( true ) ) {
                        u.moves = 0;
                        uquit = QUIT_SAVED;
                    }
                }
                break;

            case ACTION_QUICKSAVE:
                quicksave();
                return false;

            case ACTION_QUICKLOAD:
                quickload();
                return false;

            case ACTION_PL_INFO:
                modal_fiber_.emplace( [this]() { character_display::disp_info( u ); } );
                break;

            case ACTION_MAP:
                modal_fiber_.emplace( [this]() { ui::omap::display(); } );
                break;

            case ACTION_SKY:
                if( m.is_outside( u.bub_pos() ) ) {
                    ui::omap::display_visible_weather();
                } else {
                    add_msg( m_info, _( "You can't see the sky from here." ) );
                }
                break;

            case ACTION_MISSIONS:
                modal_fiber_.emplace( [this]() { list_missions(); } );
                break;

            case ACTION_SCORES:
                modal_fiber_.emplace( [this]() {
                    show_scores_ui( *achievements_tracker_ptr, stats(), get_kill_tracker() );
                } );
                break;

            case ACTION_DIARY:
                modal_fiber_.emplace( [this]() { diary::show_diary_ui( u.get_avatar_diary() ); } );
                break;

            case ACTION_FACTIONS:
                modal_fiber_.emplace( [this]() { faction_manager_ptr->display(); } );
                break;

            case ACTION_MORALE:
                modal_fiber_.emplace( [this]() { u.disp_morale(); } );
                break;

            case ACTION_MESSAGES:
                modal_fiber_.emplace( [this]() { Messages::display_messages(); } );
                break;

            case ACTION_OPEN_WIKI:
                if( !get_option<std::string>( "WIKI_DOC_URL" ).empty() ) {
                    open_url( get_option<std::string>( "WIKI_DOC_URL" ) );
                } else {
                    add_msg( m_bad, _( "Invalid Wiki URL specified!" ) );
                }
                break;

            case ACTION_OPEN_HHG:
                if( !get_option<std::string>( "HHG_URL" ).empty() ) {
                    open_url( get_option<std::string>( "HHG_URL" ) + std::string( "/?t=UNDEAD_PEOPLE" ) );
                } else {
                    add_msg( m_bad, _( "Invalid Hitchhiker's Guide URL specified!" ) );
                }
                break;

            case ACTION_HELP:
                modal_fiber_.emplace( [this]() { get_help().display_help(); } );
                break;

            case ACTION_OPTIONS:
                modal_fiber_.emplace( [this]() { get_options().show( true ); } );
                break;

            case ACTION_AUTOPICKUP:
                modal_fiber_.emplace( [this]() { get_auto_pickup().show(); } );
                break;

            case ACTION_AUTONOTES:
                modal_fiber_.emplace( [this]() { get_auto_notes_settings().show_gui(); } );
                break;

            case ACTION_SAFEMODE:
                modal_fiber_.emplace( [this]() { get_safemode().show(); } );
                break;

            case ACTION_DISTRACTION_MANAGER:
                modal_fiber_.emplace( [this]() { get_distraction_manager().show(); } );
                break;

            case ACTION_COLOR:
                modal_fiber_.emplace( [this]() { all_colors.show_gui(); } );
                break;

            case ACTION_WORLD_MODS:
                modal_fiber_.emplace( [this]() {
                    world_generator->show_active_world_mods(
                        world_generator->active_world->info->active_mod_order );
                } );
                break;

            case ACTION_DEBUG:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) { break; }
                debug_menu::debug();
                break;

            case ACTION_LUA_CONSOLE:
                modal_fiber_.emplace( [this]() { cata::show_lua_console(); } );
                break;

            case ACTION_LUA_RELOAD:
                cata::reload_lua_code();
                break;

            case ACTION_TOGGLE_FULLSCREEN:
                toggle_fullscreen();
                break;

            case ACTION_TOGGLE_PIXEL_MINIMAP:
                toggle_pixel_minimap();
                break;

            case ACTION_TOGGLE_PANEL_ADM:
                panel_manager::get_manager().show_adm();
                break;

            case ACTION_TOGGLE_SOMA_DETAIL:
                sidebar_hud_toggle_soma_detail();
                break;

            case ACTION_RELOAD_TILESET:
                reload_tileset( []( const std::string & str ) { DebugLog( DL::Info, DC::Main ) << str; } );
                break;

            case ACTION_TOGGLE_AUTO_FEATURES:
                get_options().get_option( "AUTO_FEATURES" ).setNext();
                get_options().save();
                add_msg( _( "%s are now %s." ),
                         get_options().get_option( "AUTO_FEATURES" ).getMenuText(),
                         get_option<bool>( "AUTO_FEATURES" ) ? _( "ON" ) : _( "OFF" ) );
                break;

            case ACTION_TOGGLE_AUTO_PULP_BUTCHER:
                get_options().get_option( "AUTO_PULP_BUTCHER" ).setNext();
                get_options().save();
                add_msg( _( "%s is now set to %s." ),
                         get_options().get_option( "AUTO_PULP_BUTCHER" ).getMenuText(),
                         get_options().get_option( "AUTO_PULP_BUTCHER" ).getValueName() );
                break;

            case ACTION_TOGGLE_AUTO_MINING:
                get_options().get_option( "AUTO_MINING" ).setNext();
                get_options().save();
                add_msg( _( "%s is now %s." ), get_options().get_option( "AUTO_MINING" ).getMenuText(),
                         get_option<bool>( "AUTO_MINING" ) ? _( "ON" ) : _( "OFF" ) );
                break;

            case ACTION_TOGGLE_THIEF_MODE:
                if( g->u.get_value( "THIEF_MODE" ) == "THIEF_ASK" ) {
                    u.set_value( "THIEF_MODE", "THIEF_HONEST" );
                    u.set_value( "THIEF_MODE_KEEP", "YES" );
                    add_msg( _( "You will not pick up other peoples belongings." ) );
                } else if( g->u.get_value( "THIEF_MODE" ) == "THIEF_HONEST" ) {
                    u.set_value( "THIEF_MODE", "THIEF_STEAL" );
                    u.set_value( "THIEF_MODE_KEEP", "YES" );
                    add_msg( _( "You will pick up also those things that belong to others!" ) );
                } else if( g->u.get_value( "THIEF_MODE" ) == "THIEF_STEAL" ) {
                    u.set_value( "THIEF_MODE", "THIEF_ASK" );
                    u.set_value( "THIEF_MODE_KEEP", "NO" );
                    add_msg( _( "You will be reminded not to steal." ) );
                } else {
                    add_msg( _( "THIEF_MODE CONTAINED BAD VALUE [ %s ]!" ),
                             g->u.get_value( "THIEF_"
                                             "MODE" ) );
                }
                break;

            case ACTION_TOGGLE_AUTO_FORAGING:
                get_options().get_option( "AUTO_FORAGING" ).setNext();
                get_options().save();
                add_msg( _( "%s is now set to %s." ),
                         get_options().get_option( "AUTO_FORAGING" ).getMenuText(),
                         get_options().get_option( "AUTO_FORAGING" ).getValueName() );
                break;

            case ACTION_TOGGLE_AUTO_PICKUP:
                get_options().get_option( "AUTO_PICKUP" ).setNext();
                get_options().save();
                add_msg( _( "%s is now set to %s." ),
                         get_options().get_option( "AUTO_PICKUP" ).getMenuText(),
                         get_options().get_option( "AUTO_PICKUP" ).getValueName() );
                break;

            case ACTION_DISPLAY_SCENT:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) { break; }
                display_scent();
                break;

            case ACTION_DISPLAY_SCENT_TYPE:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) { break; }
                display_scent();
                break;

            case ACTION_DISPLAY_TEMPERATURE:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) { break; }
                display_temperature();
                break;

            case ACTION_DISPLAY_VEHICLE_AI:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) { break; }
                display_vehicle_ai();
                break;

            case ACTION_DISPLAY_VISIBILITY:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) { break; }
                display_visibility();
                break;

            case ACTION_DISPLAY_LIGHTING:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) { break; }
                display_lighting();
                break;

            case ACTION_DISPLAY_RADIATION:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) { break; }
                display_radiation();
                break;

            case ACTION_DISPLAY_TRANSPARENCY:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) { break; }
                display_transparency();
                break;

            case ACTION_DISPLAY_OUTSIDE:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) { break; }
                display_outside();
                break;

            case ACTION_DISPLAY_SOUND:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) { break; }
                display_sound();
                break;

            case ACTION_DISPLAY_SOUND_ABSORPTION:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) { break; }
                display_sound_absorption();
                break;

            case ACTION_DISPLAY_SOUND_WALLS:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) { break; }
                display_sound_walls();
                break;

            case ACTION_DISPLAY_SUBMAP_GRID:
                g->debug_submap_grid_overlay = !g->debug_submap_grid_overlay;
                break;

            case ACTION_TOGGLE_ZONE_OVERLAY:
                g->show_zone_overlay = !g->show_zone_overlay;
                break;

            case ACTION_TOGGLE_HOUR_TIMER:
                toggle_debug_hour_timer();
                break;

            case ACTION_TOGGLE_FPS:
                toggle_debug_fps();
                break;

            case ACTION_TOGGLE_DEBUG_MODE:
                if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) { break; }
                debug_mode = !debug_mode;
                if( debug_mode ) {
                    add_msg( m_info, _( "Debug mode ON!" ) );
                } else {
                    add_msg( m_info, _( "Debug mode OFF!" ) );
                }
                break;

            case ACTION_ZOOM_IN:
                zoom_in();
                mark_main_ui_adaptor_resize();
                break;

            case ACTION_ZOOM_OUT:
                zoom_out();
                mark_main_ui_adaptor_resize();
                break;

            case ACTION_ITEMACTION:
                modal_fiber_.emplace( [this]() { item_action_menu(); } );
                break;

            case ACTION_AUTOATTACK:
                avatar_action::autoattack( u, m );
                break;

            default:
                break;
        }
    }

    // Client co-op: forward world-affecting actions to the host proxy.
    // The local action already executed above for instant visual feedback
    // (local prediction).  The host mirrors via execute_client_action().
    if( coop_client_ ) {
        // Build a typed command and forward it to the host proxy.
        // Fire is queued from inside modal_fiber_ above; burst-fire below.
        const auto move_cmd = make_player_move_cmd( act, iso_rotate::yes );
        if( move_cmd.kind == player_cmd_kind::move ) {
            // Only queue if the move actually succeeded — blocked moves leave
            // g->u at coop_pos_before_ and cause wall-flicker on replay.
            const bool actually_moved = ( u.bub_pos().raw() != coop_pos_before_.raw() );
            const auto dir = move_cmd_to_dir_string( move_cmd );
            if( !dir.empty() && actually_moved ) { coop_client_->queue_action( std::string( dir ) ); }
            // D4: haul relay — items dragged to the new tile need DROP + ITEM_REMOVE_ALL on host.
            if( u.is_hauling() && actually_moved ) {
                const auto new_abs = m.bub_to_abs( u.bub_pos() );
                for( const auto& it : m.i_at( u.bub_pos() ) ) {
                    std::ostringstream drop_ctx;
                    JsonOut jd( drop_ctx );
                    jd.start_object();
                    jd.member( "ax", new_abs.x() );
                    jd.member( "ay", new_abs.y() );
                    jd.member( "az", new_abs.z() );
                    jd.member( "item" );
                    it->serialize( jd );
                    jd.end_object();
                    coop_client_->queue_action( "DROP", drop_ctx.str() );
                }
                const auto old_abs = m.bub_to_abs( coop_pos_before_ );
                std::ostringstream rm_ctx;
                JsonOut jr( rm_ctx );
                jr.start_object();
                jr.member( "ax", old_abs.x() );
                jr.member( "ay", old_abs.y() );
                jr.member( "az", old_abs.z() );
                jr.member( "type", "*" );
                jr.end_object();
                coop_client_->queue_action( "ITEM_REMOVE_ALL", rm_ctx.str() );
            }
        } else if( act == ACTION_PAUSE || act == ACTION_TIMEOUT || act == ACTION_WAIT ) {
            coop_client_->queue_action( "PAUSE" );
        } else if( act == ACTION_PICKUP || act == ACTION_PICKUP_ALL || act == ACTION_PICKUP_FEET ) {
            coop_client_->queue_action( "PICKUP" );
        } else if( act == ACTION_SLEEP ) {
            coop_client_->queue_action( "SLEEP" );
        } else if( act == ACTION_CRAFT || act == ACTION_LONGCRAFT || act == ACTION_RECRAFT ) {
            coop_client_->queue_action( "CRAFT" );
        } else if( act == ACTION_EAT ) {
            // B3 Phase 7: eat/drink — no-payload relay.
            coop_client_->queue_action( "EAT" );
        } else if( act == ACTION_RELOAD_ITEM || act == ACTION_RELOAD_WEAPON
                   || act == ACTION_RELOAD_WIELDED ) {
            // B3 Phase 7: reload — no-payload relay.
            coop_client_->queue_action( "RELOAD" );
        } else if( act == ACTION_USE ) {
            // B3 Phase 8: use/activate item — no-payload relay.
            coop_client_->queue_action( "USE" );
        } else if( act == ACTION_FIRE ) {
            // Queued from inside modal_fiber_ above — nothing here.
        } else if( act == ACTION_FIRE_BURST ) {
            // Burst runs inline; last_target_pos is valid immediately.
            if( u.last_target_pos.has_value() ) {
                const auto& tap = *u.last_target_pos;
                std::ostringstream ctx_oss;
                JsonOut ctx_jout( ctx_oss );
                ctx_jout.start_object();
                ctx_jout.member( "tx", tap.x() );
                ctx_jout.member( "ty", tap.y() );
                ctx_jout.member( "tz", tap.z() );
                if( u.is_armed() && u.primary_weapon().is_gun() ) {
                    ctx_jout.member( "weapon_id", u.primary_weapon().typeId().str() );
                    const itype_id cur_ammo = u.primary_weapon().ammo_current();
                    if( !cur_ammo.is_null() ) {
                        ctx_jout.member( "ammo_id", cur_ammo.str() );
                    }
                }
                ctx_jout.end_object();
                coop_client_->queue_action( "FIRE", ctx_oss.str() );
            }
        } else if( act == ACTION_MOVE_UP || act == ACTION_MOVE_DOWN ) {
            // Queue only if vertical_move() actually changed z — it can fail (no stairs,
            // blocked, etc.). coop_pos_before_ captures the pre-action bub_pos (existing local).
            if( u.bub_pos().z() != coop_pos_before_.z() ) {
                const auto ap = u.abs_pos();
                const auto ctx =
                    "{\"ax\":" + std::to_string( ap.x() ) +
                    ",\"ay\":" + std::to_string( ap.y() ) +
                    ",\"az\":" + std::to_string( ap.z() ) + "}";
                coop_client_->queue_action(
                    act == ACTION_MOVE_UP ? "MOVE_UP" : "MOVE_DOWN", ctx );
            }
        } else if( act == ACTION_AUTOATTACK ) {
            // B3 Phase 9: relay MELEE to host proxy.
            // autoattack() calls C++ move()/reach_attack() directly — no MOVE packet fires.
            // coop_session::last_autoattack_target is set ONLY on success; cleared at entry
            // so failed autoattacks (no hostiles) relay nothing.
            const auto& tgt = coop_session::get().last_autoattack_target;
            if( tgt.has_value() ) {
                // CL-MELEE-WEAPON: embed weapon at attack time (same pattern as FIRE ctx_json).
                std::ostringstream melee_ctx_oss;
                JsonOut melee_ctx_jout( melee_ctx_oss );
                melee_ctx_jout.start_object();
                melee_ctx_jout.member( "tx", tgt->x() );
                melee_ctx_jout.member( "ty", tgt->y() );
                melee_ctx_jout.member( "tz", tgt->z() );
                if( u.is_armed() ) {
                    melee_ctx_jout.member( "weapon_id", u.primary_weapon().typeId().str() );
                }
                melee_ctx_jout.end_object();
                coop_client_->queue_action( "MELEE", melee_ctx_oss.str() );
            }
        } else if( act == ACTION_CAST_SPELL || act == ACTION_CAST_LAST_SPELL ) {
            // D7: spell action token — terrain/field mutations relayed separately via magic.cpp.
            coop_client_->queue_action( "CAST_SPELL", "{}" );
        } else if( act == ACTION_WEAR ) {
            // G1: relay the last worn item to the proxy.
            if( !u.worn.empty() ) {
                std::ostringstream wear_ctx;
                JsonOut wear_j( wear_ctx );
                u.worn.back()->serialize( wear_j );
                coop_client_->queue_action( "WEAR", wear_ctx.str() );
            }
        } else if( act == ACTION_TAKE_OFF ) {
            // G1: full worn-list resync (can't identify which item was removed post-takeoff).
            std::ostringstream worn_ctx;
            JsonOut worn_j( worn_ctx );
            worn_j.start_array();
            for( const auto& w : u.worn ) { w->serialize( worn_j ); }
            worn_j.end_array();
            coop_client_->queue_action( "WORN_SYNC", worn_ctx.str() );
        }
    }
    // Co-op keybindings active for both host and client
    {
        auto& sess = coop_session::get();
        if( sess.is_coop() ) {
            if( act == ACTION_CO_OP_TAP_SHOULDER ) {
                if( sess.is_client() ) {
                    coop_client_->send_tap_shoulder();
                } else if( g->coop_server_ ) {
                    g->coop_server_->send_tap_shoulder();
                }
            } else if( act == ACTION_CO_OP_EMOTE ) {
                if( sess.is_client() ) {
                    coop_client_->send_emote( "high_five" );
                } else if( g->coop_server_ ) {
                    g->coop_server_->send_emote( "high_five" );
                }
            } else if( act == ACTION_CO_OP_STABILIZE && sess.is_host() && g->coop_server_ ) {
                if( !g->coop_server_->client_downed() ) {
                    add_msg( m_info, _( "Partner is not downed." ) );
                } else if( show_coop_popup( string_format(
                                                _( "Stabilize %s? (consumes bandage or first aid kit)" ),
                                                sess.partner_name ) ) ) {
                    g->coop_server_->stabilize_client();
                }
            } else if( act == ACTION_CO_OP_PASS_ITEM ) {
                // F2: item passing — both client and host can offer items to their partner
                const auto& partner_pos = sess.partner_abs_pos;
                const auto delta = partner_pos - u.abs_pos();
                const int dist2 = delta.x() * delta.x() + delta.y() * delta.y();
                if( dist2 > 4 ) {
                    add_msg( m_info, _( "You need to be closer to your partner to pass items." ) );
                } else {
                    item *it = game_menus::inv::titled_menu( u, _( "Pass which item?" ),
                               _( "You have nothing to pass." ) );
                    if( it && !it->is_null() ) {
                        // Serialize the item BEFORE removing from inventory
                        const std::string item_name = it->tname();
                        std::ostringstream item_oss;
                        JsonOut item_jout( item_oss );
                        it->serialize( item_jout );
                        std::ostringstream oss;
                        JsonOut jout( oss );
                        jout.start_object();
                        jout.member( "t", static_cast<int>( coop_pkt::trade_offer ) );
                        jout.member( "d" );
                        jout.start_object();
                        jout.member( "item_json", item_oss.str() );
                        jout.end_object();
                        jout.end_object();
                        // Send via the appropriate transport
                        if( sess.is_client() && coop_client_ ) {
                            coop_client_->send_raw( oss.str() );
                        } else if( sess.is_host() && g->coop_server_ ) {
                            g->coop_server_->send_raw( oss.str() );
                        }
                        // Remove item from giver's inventory
                        const int pos = u.inv_position_by_item( it );
                        u.i_rem( pos );
                        add_msg( m_info, _( "Offered %s to %s." ), item_name, sess.partner_name );
                    }
                }
            } else if( act == ACTION_CO_OP_CHAT ) {
                const std::string msg = string_input_popup()
                                        .title( _( "Chat:" ) )
                                        .width( 50 )
                                        .query_string();
                if( !msg.empty() ) {
                    if( sess.is_client() && coop_client_ ) {
                        coop_client_->send_chat( msg );
                    } else if( sess.is_host() && coop_server_ ) {
                        coop_server_->send_chat( msg );
                    }
                    add_msg( m_info, _( "[you]: %s" ), msg );
                }
            }
        }
    }

    if( act != ACTION_TIMEOUT ) { u.mod_moves( -current_turn.moves_elapsed() ); }
    gamemode->post_action( act );

    u.movecounter = ( !u.is_dead_state() ? ( before_action_moves - u.moves ) : 0 );
    dbg( DL::Info ) << string_format(
                        "%s: [%d] %d - %d = %d", action_ident( act ), to_turn<int>( calendar::turn ),
                        before_action_moves, u.movecounter, u.moves );
    return ( !u.is_dead_state() );
}
