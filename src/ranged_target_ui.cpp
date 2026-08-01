#include "ranged.h"

#include "activity_actor_definitions.h"
#include "animation.h"
#include "avatar.h"
#include "ballistics.h"
#include "bodypart.h"
#include "cached_options.h"
#include "calendar.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "catalua_coord.h"
#include "catalua_hooks.h"
#include "catalua_icallback_actor.h"
#include "catalua_sol.h"
#include "character.h"
#include "character_functions.h"
#include "color.h"
#include "coordinates.h"
#include "coop_session.h"
#include "creature.h"
#include "cursesdef.h"
#include "damage.h"
#include "debug.h"
#include "sound_visualization.h"
#include "dispersion.h"
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "flag.h"
#include "game.h"
#include "game_constants.h"
#include "gun_mode.h"
#include "input.h"
#include "item.h"
#include "item_functions.h"
#include "item_reload_option.h"
#include "itype.h"
#include "line.h"
#include "magic.h"
#include "magic_enchantment.h"
#include "map.h"
#include "material.h"
#include "math_defines.h"
#include "messages.h"
#include "monster.h"
#include "morale_types.h"
#include "mtype.h"
#include "npc.h"
#include "options.h"
#include "output.h"
#include "panels.h"
#include "player.h"
#include "player_activity.h"
#include "point.h"
#include "projectile.h"
#include "rml_screen.h"
#include "rml_util.h"
#include "rng.h"
#include "shape_impl.h"
#include "skill.h"
#include "sounds.h"
#include "string_formatter.h"
#include "string_id.h"
#include "translations.h"
#include "trap.h"
#include "type_id.h"
#include "ui_manager.h"
#include "units.h"
#include "units_angle.h"
#include "units_utility.h"
#include "value_ptr.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vpart_position.h"
#include "sdl_window_dims.h"

#include <RmlUi/Core.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>


using ammo_effect_str_id = string_id<ammo_effect>;

static const weapon_category_id weapon_cat_GRENADE_LAUNCHERS( "GRENADE_LAUNCHERS" );
static const weapon_category_id weapon_cat_MAGNETIC( "MAGNETIC" );
static const weapon_category_id weapon_cat_PNEUMATIC( "PNEUMATIC" );
static const weapon_category_id weapon_cat_FLAMETHROWERS( "FLAMETHROWERS" );
static const weapon_category_id weapon_cat_SPRAY_GUNS( "SPRAY_GUNS" );
static const weapon_category_id weapon_cat_WATER_CANNONS( "WATER_CANNONS" );
static const weapon_category_id weapon_cat_ROCKET_LAUNCHERS( "ROCKET_LAUNCHERS" );
static const weapon_category_id weapon_cat_ELASTIC( "ELASTIC" );
static const weapon_category_id weapon_cat_S_XBOWS( "S_XBOWS" );
static const weapon_category_id weapon_cat_M_XBOWS( "M_XBOWS" );
static const weapon_category_id weapon_cat_ENERGY_WEAPONS( "ENERGY_WEAPONS" );

static const ammo_effect_str_id ammo_effect_ACT_ON_RANGED_HIT( "ACT_ON_RANGED_HIT" );
static const ammo_effect_str_id ammo_effect_BLACKPOWDER( "BLACKPOWDER" );
static const ammo_effect_str_id ammo_effect_BOUNCE( "BOUNCE" );
static const ammo_effect_str_id ammo_effect_BLINDS_EYES( "BLINDS_EYES" );
static const ammo_effect_str_id ammo_effect_BURST( "BURST" );
static const ammo_effect_str_id ammo_effect_CUSTOM_EXPLOSION( "CUSTOM_EXPLOSION" );
static const ammo_effect_str_id ammo_effect_EMP( "EMP" );
static const ammo_effect_str_id ammo_effect_EXPLOSIVE( "EXPLOSIVE" );
static const ammo_effect_str_id ammo_effect_HEAVY_HIT( "HEAVY_HIT" );
static const ammo_effect_str_id ammo_effect_IGNITE( "IGNITE" );
static const ammo_effect_str_id ammo_effect_LASER( "LASER" );
static const ammo_effect_str_id ammo_effect_LIGHTNING( "LIGHTNING" );
static const ammo_effect_str_id ammo_effect_JET( "JET" );
static const ammo_effect_str_id ammo_effect_NO_CRIT( "NO_CRIT" );
static const ammo_effect_str_id ammo_effect_NO_DAMAGE( "NO_DAMAGE" );
static const ammo_effect_str_id ammo_effect_NO_OVERSHOOT( "NO_OVERSHOOT" );
static const ammo_effect_str_id ammo_effect_NO_EMBED( "NO_EMBED" );
static const ammo_effect_str_id ammo_effect_NO_ITEM_DAMAGE( "NO_ITEM_DAMAGE" );
static const ammo_effect_str_id ammo_effect_NON_FOULING( "NON_FOULING" );
static const ammo_effect_str_id ammo_effect_PLASMA( "PLASMA" );
static const ammo_effect_str_id ammo_effect_RECYCLED( "RECYCLED" );
static const ammo_effect_str_id ammo_effect_DRAW_AS_LINE( "DRAW_AS_LINE" );
static const ammo_effect_str_id ammo_effect_SHATTER_SELF( "SHATTER_SELF" );
static const ammo_effect_str_id ammo_effect_SHOT( "SHOT" );
static const ammo_effect_str_id ammo_effect_STREAM( "STREAM" );
static const ammo_effect_str_id ammo_effect_STREAM_BIG( "STREAM_BIG" );
static const ammo_effect_str_id ammo_effect_TANGLE( "TANGLE" );
static const ammo_effect_str_id ammo_effect_NET_TANGLE( "NET_TANGLE" );
static const ammo_effect_str_id ammo_effect_WIDE( "WIDE" );
static const ammo_effect_str_id ammo_effect_THROWN( "THROWN" );

static const efftype_id effect_downed( "downed" );
static const efftype_id effect_hit_by_player( "hit_by_player" );
static const efftype_id effect_on_roof( "on_roof" );

static const itype_id itype_UPS( "UPS" );

static const trap_str_id tr_practice_target( "tr_practice_target" );

static const fault_id fault_gun_blackpowder( "fault_gun_blackpowder" );
static const fault_id fault_gun_chamber_spent( "fault_gun_chamber_spent" );
static const fault_id fault_gun_dirt( "fault_gun_dirt" );
static const fault_id fault_gun_unlubricated( "fault_gun_unlubricated" );

static const skill_id skill_driving( "driving" );
static const skill_id skill_gun( "gun" );
static const skill_id skill_launcher( "launcher" );
static const skill_id skill_pistol( "pistol" );
static const skill_id skill_rifle( "rifle" );
static const skill_id skill_throw( "throw" );

static const bionic_id bio_railgun( "bio_railgun" );
static const bionic_id bio_targeting( "bio_targeting" );
static const bionic_id bio_ups( "bio_ups" );

static const trait_id trait_LASER_GUIDED( "LASER_GUIDED" );
static const trait_id trait_PYROMANIA( "PYROMANIA" );
static const trait_id trait_NORANGEDCRIT( "NO_RANGED_CRIT" );

// not to confuse with item flags (json_flag)
static const std::string flag_SHOOT_ME( "SHOOT_ME" );

// Maximum duration of aim-and-fire loop, in turns
static constexpr int AIF_DURATION_LIMIT = 10;



#include "ranged_internal.h"
#include "target_ui.h"

namespace
{
struct target_rml_session {
    std::string body_rml;
    Rml::DataModelHandle handle;
};

/// Free-aim jitter buffer, in screen pixels. Wide enough to swallow sensor noise
/// and hand tremor, narrow enough that a deliberate correction still lands where
/// the player pointed. See target_ui::aim_jitter_px for why it exists.
constexpr auto free_aim_jitter_px = 6;

} // namespace

target_handler::trajectory target_ui::run()
{
    if( mode == TargetMode::Spell && !no_mana && !casting->can_cast( *you ) ) {
        you->add_msg_if_player(
            m_bad, _( "You don't have enough %s to cast this spell" ), casting->energy_string() );
    }
    if( mode == TargetMode::Fire || mode == TargetMode::TurretManual || mode == TargetMode::Shape ) {
        ensure_ranged_gun_mode();
        update_ammo_range_from_gun_mode();
        if( mode == TargetMode::Fire ) {
            sight_dispersion = ranged::effective_dispersion( *you, relevant->sight_dispersion() );
        }
    }

    map& here = get_map();
    // Load settings
    allow_zlevel_shift = here.has_zlevels() && get_option<bool>( "FOV_3D" );
    snap_to_target = get_option<bool>( "SNAP_TO_TARGET" );
    if( mode == TargetMode::Turrets ) {
        // Due to how cluttered the display would become, disable it by default
        // unless aiming a single turret.
        draw_turret_lines = vturrets->size() == 1;
    }

    avatar& player_character = *you;
    on_out_of_scope cleanup( [&here, &player_character]() {
        here.invalidate_map_cache(
            player_character.bub_pos().z() + player_character.view_offset.z() );
    } );

    shared_ptr_fast<game::draw_callback_t> target_ui_cb = make_shared_fast<game::draw_callback_t>(
    [&]() { draw_terrain_overlay(); } );
    g->add_draw_callback( target_ui_cb );

    ui_adaptor ui;
    ui.on_screen_resize( [&]( ui_adaptor & ui ) {
        init_window_and_input();
        ui.position_from_window( w_target );
    } );
    ui.mark_resize();

    // RmlUi render path (slice 2a). ctxt is built by init_window_and_input (via
    // mark_resize above), so it is ready to pass to open() for the tick.
    target_rml_session rml_data;
    rml_doc rml;
    const auto sync_rml = [&]() {
        if( !rml ) { return; }
        rml_data.body_rml = cata_text_to_rml( panel_text() );
        rml_data.handle.DirtyVariable( "body_rml" );
    };
    rml.open( ranged_rmlui_enabled(), "ranged", ctxt, [&]( Rml::DataModelConstructor & c ) {
        c.Bind( "body_rml", &rml_data.body_rml );
        rml_data.handle = c.GetModelHandle();
    }, true );

    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( rml ) {
            sync_rml();
            return;
        }
    } );

    // Handle multi-turn aiming
    std::string action;
    bool attack_was_confirmed = false;
    bool reentered = false;
    bool resume_critter = false;
    if( mode == TargetMode::Fire && !activity->action.empty() ) {
        // We were in this UI during previous turn...
        reentered = true;
        std::string act_data = activity->action;
        if( act_data == "AIM" ) {
            // ...and ran out of moves while aiming.
        } else {
            // ...and selected 'aim and shoot', but ran out of moves.
            // So, skip retrieving input and go straight to the action.
            action = act_data;
            attack_was_confirmed = true;
        }
        // Load state to keep the ui consistent across turns
        snap_to_target = activity->snap_to_target;
        shifting_view = activity->shifting_view;
        resume_critter = activity->aiming_at_critter;
    }

    // Initialize cursor position
    src = you->bub_pos();
    update_target_list();

    if( activity && activity->abort_if_no_targets && targets.empty() ) {
        // this branch is taken when already shot once and re-entered
        // aiming, if no targets are available we want to abort so
        // players don't arrive at aiming ui with nothing to shoot at.
        activity->aborted = true;
        traj.clear();
        return traj;
    }
    tripoint_bub_ms initial_dst = src;
    if( reentered ) {
        if( !try_reacquire_target( resume_critter, initial_dst ) ) {
            // Target lost
            action.clear();
            attack_was_confirmed = false;
        }
    } else {
        initial_dst = choose_initial_target();
    }
    set_cursor_pos( initial_dst );
    sync_aim_angle_from_dst();
    opened_by_rmb = is_rmb_held();
    aim_commit_px = point_zero;
    // Only free aim needs the buffer — keyboard aiming moves in deliberate 1°
    // steps. And only outside co-op: there the world runs on a real-time
    // accumulator, so holding still does not stop the clock and the buffer would
    // buy nothing while making the two players' recoil rules disagree.
    aim_jitter_px = ( opened_by_rmb && !coop_session::get().is_coop() )
                    ? free_aim_jitter_px : 0;
    if( mode == TargetMode::Throw ) {
        max_throw_range = range;
        throw_charge    = 0.0;
        throw_charge_start_ms = get_sdl_ticks();
    }
    if( dst != initial_dst ) {
        // Our target moved out of range
        action.clear();
        attack_was_confirmed = false;
    }
    if( mode == TargetMode::Fire ) {
        if( activity->aif_duration > AIF_DURATION_LIMIT ) {
            // Break long (potentially infinite) aim-and-fire loop.
            // May happen if e.g. avatar tries to get 'precise' shot while being
            // attacked by multiple zombies, which triggers dodges and corresponding aim loss.
            action.clear();
            attack_was_confirmed = false;
        }
        if( !action.empty() && !prompt_friendlies_in_lof() ) {
            // A friendly creature moved into line of fire during aim-and-shoot,
            // and player decided to stop aiming
            action.clear();
            attack_was_confirmed = false;
        }
        activity->acceptable_losses.clear();
        if( action.empty() ) {
            activity->aif_duration = 0;
        } else {
            activity->aif_duration += 1;
        }
    }

    // Event loop!
    ExitCode loop_exit_code;
    std::string timed_out_action;
    bool skip_redraw = false;
    for( ;; action.clear() ) {
        if( !skip_redraw ) {
            g->invalidate_main_ui_adaptor();
            ui_manager::redraw();
        }
        skip_redraw = false;

        // Wait for user input (or use value retrieved from activity)
        if( action.empty() ) {
            // The loop needs a steady tick even when the player disabled edge
            // scrolling (EDGE_SCROLL == -1), because handle_input only arms a
            // timeout for non-negative values. Without one there is no TIMEOUT
            // action, which starves the throw-charge meter below AND the
            // hold-to-aim release backstop — the latter is precisely what made
            // right-click aiming behave like a toggle.
            constexpr int aim_tick_ms = 50;
            const int edge_scroll = get_option<int>( "EDGE_SCROLL" );
            const int timeout = edge_scroll >= 0 ? std::min( edge_scroll, aim_tick_ms )
                                : aim_tick_ms;
            action = ctxt.handle_input( timeout );
        }
        // After a fiber yield the world may have ticked; dst_critter and targets
        // hold raw Creature* that could have been freed.
        // Re-derive from the stable dst position and rebuild the visible target list.
        if( coop_fiber::active() ) {
            Creature* cr = g->critter_at( dst, true );
            dst_critter = ( cr && pl_sees( *cr ) ) ? cr : nullptr;
            update_target_list();
        }

        // If an aiming mode is selected, use "*_SHOT" instead of "FIRE"
        if( mode == TargetMode::Fire && action == "FIRE" && aim_mode->has_threshold ) {
            action = aim_mode->action;
        }

        // In hold-to-aim mode, LMB (SELECT) fires — redirect to the FIRE path
        if( opened_by_rmb && action == "SELECT" ) {
            action = "FIRE";
        }

        // Update throw charge and effective range each TIMEOUT tick
        if( action == "TIMEOUT" && mode == TargetMode::Throw && max_throw_range > 0 ) {
            const auto elapsed = static_cast<double>( get_sdl_ticks() - throw_charge_start_ms );
            throw_charge      = std::min( 1.0, elapsed / throw_charge_full_ms );
            const auto eff_range = std::max( 1, static_cast<int>(
                                                 throw_charge * static_cast<double>( max_throw_range ) ) );
            if( eff_range != range ) {
                range = eff_range;
                set_aim_angle( aim_angle ); // re-cast DDA at same angle with new range
            }
        }

        // Hold-to-aim: SEC_SELECT is the RMB *release* (keybindings.json binds it to
        // MOUSE_RIGHT, which sdl_input emits on SDL_EVENT_MOUSE_BUTTON_UP), so it ends
        // the hold the instant the button comes up instead of waiting for the next
        // poll. The TIMEOUT poll below stays as a backstop for the release that lands
        // between the AIM_HOLD press and this loop taking over input.
        if( opened_by_rmb && action == "SEC_SELECT" ) {
            loop_exit_code = ExitCode::Abort;
            break;
        }
        if( opened_by_rmb && action == "TIMEOUT" ) {
            if( !is_rmb_held() ) {
                loop_exit_code = ExitCode::Abort;
                break;
            }
            action_aim(); // shrinks spread cone as recoil decays
        }

        // Handle received input
        if( handle_cursor_movement( action, skip_redraw ) ) {
            continue;
        } else if( action == "TOGGLE_SNAP_TO_TARGET" ) {
            toggle_snap_to_target();
        } else if( action == "TOGGLE_TURRET_LINES" ) {
            draw_turret_lines = !draw_turret_lines;
        } else if( action == "TOGGLE_MOVE_CURSOR_VIEW" ) {
            if( snap_to_target ) { toggle_snap_to_target(); }
            shifting_view = !shifting_view;
        } else if( action == "zoom_in" ) {
            g->zoom_in();
            g->mark_main_ui_adaptor_resize();
        } else if( action == "zoom_out" ) {
            g->zoom_out();
            g->mark_main_ui_adaptor_resize();
        } else if( action == "QUIT" ) {
            loop_exit_code = ExitCode::Abort;
            break;
        } else if( action == "SWITCH_MODE" ) {
            action_switch_mode();
        } else if( action == "SWITCH_AMMO" ) {
            if( !action_switch_ammo() ) {
                loop_exit_code = ExitCode::Reload;
                break;
            }
        } else if( action == "FIRE" ) {
            if( status != Status::Good ) { continue; }
            bool can_skip_confirm = ( mode == TargetMode::Spell && casting->damage() <= 0 );
            if( !can_skip_confirm && !confirm_non_enemy_target() ) { continue; }
            set_last_target();
            loop_exit_code = ExitCode::Fire;
            break;
        } else if( action == "AIM" ) {
            if( status != Status::Good ) { continue; }

            // No confirm_non_enemy_target here because we have not initiated the firing.
            // Aiming can be stopped / aborted at any time.

            if( !action_aim() ) {
                timed_out_action = "AIM";
                loop_exit_code = ExitCode::Timeout;
                break;
            }
        } else if( action == "AIMED_SHOT" || action == "CAREFUL_SHOT" || action == "PRECISE_SHOT" ) {
            if( status != Status::Good ) { continue; }

            // This action basically means "Fire" as well; the actual firing may be delayed
            // through aiming, but there is usually no means to abort it. Therefore we query now
            if( !attack_was_confirmed && !confirm_non_enemy_target() ) { continue; }

            if( action_aim_and_shoot( action ) ) {
                loop_exit_code = ExitCode::Fire;
            } else {
                timed_out_action = action;
                loop_exit_code = ExitCode::Timeout;
            }
            break;
        }
    } // for(;;)

    switch( loop_exit_code ) {
        case ExitCode::Abort: {
            traj.clear();
            if( mode == TargetMode::Fire || mode == TargetMode::Shape ) { activity->aborted = true; }
            if( mode == TargetMode::Throw || mode == TargetMode::ThrowBlind ) { g->void_throw_impact(); }
            break;
        }
        case ExitCode::Fire: {
            bool harmful = !( mode == TargetMode::Spell && casting->damage() <= 0 );
            on_target_accepted( harmful );
            if( mode == TargetMode::Throw || mode == TargetMode::ThrowBlind ) { g->void_throw_impact(); }
            break;
        }
        case ExitCode::Timeout: {
            // We've ran out of moves, save UI state
            activity->acceptable_losses = list_friendlies_in_lof();
            traj.clear();
            activity->action = timed_out_action;
            activity->snap_to_target = snap_to_target;
            activity->shifting_view = shifting_view;
            activity->aiming_at_critter = !!dst_critter;
            break;
        }
        case ExitCode::Reload: {
            traj.clear();
            activity->aborted = true;
            activity->reload_requested = true;
            if( mode == TargetMode::Throw || mode == TargetMode::ThrowBlind ) { g->void_throw_impact(); }
            break;
        }
    }

    return traj;
}

void target_ui::init_window_and_input()
{
    std::string display_type = get_option<std::string>( "ACCURACY_DISPLAY" );
    std::string panel_type = panel_manager::get_manager().get_current_layout_id();
    narrow = ( panel_type == "compact" || panel_type == "labels-narrow" );

    int top = 0;
    int width;
    int height;
    if( narrow ) {
        // Narrow layout removes the list of controls. This allows us
        // to have small window size and not suffer from it.
        width = 34;
        height = 24;
        compact = true;
    } else {
        width = 55;
        compact = TERMY < 41;
        tiny = TERMY < 28;
        bool use_whole_sidebar = TERMY < 32;
        if( use_whole_sidebar ) {
            // If we're extremely short on space, use the whole sidebar.
            height = TERMY;
        } else if( compact ) {
            // Cover up more low-value ui elements if we're tight on space.
            height = 28;
        } else {
            // Go all out
            height = 32;
        }
    }

    w_target = catacurses::newwin( height, width, point( TERMX - width, top ) );

    ctxt = input_context( "TARGET" );
    ctxt.set_iso( true );
    ctxt.register_directions();
    ctxt.register_action( "COORDINATE" );
    ctxt.register_action( "SELECT" );
    // RMB release — hold-to-aim ends on this; see the loop's SEC_SELECT branch.
    ctxt.register_action( "SEC_SELECT" );
    ctxt.register_action( "FIRE" );
    ctxt.register_action( "NEXT_TARGET" );
    ctxt.register_action( "PREV_TARGET" );
    ctxt.register_action( "CENTER" );
    ctxt.register_action( "TOGGLE_SNAP_TO_TARGET" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "MOUSE_MOVE" );
    ctxt.register_action( "zoom_out" );
    ctxt.register_action( "zoom_in" );
    ctxt.register_action( "TOGGLE_MOVE_CURSOR_VIEW" );
    if( allow_zlevel_shift ) {
        ctxt.register_action( "LEVEL_UP" );
        ctxt.register_action( "LEVEL_DOWN" );
    }
    if( mode == TargetMode::Fire || mode == TargetMode::TurretManual
        || ( mode == TargetMode::Shape && relevant->is_gun() ) ) {
        ctxt.register_action( "SWITCH_MODE" );
        if( mode == TargetMode::TurretManual || relevant->has_flag( flag_RELOAD_AND_SHOOT ) ) {
            // Turrets may support multiple ammo types.
            // RELOAD_AND_SHOOT weapons use whatever ammo is favorite.
            ctxt.register_action( "SWITCH_AMMO" );
        }
    }
    if( mode == TargetMode::Fire ) {
        ctxt.register_action( "AIM" );

        aim_types = ranged::get_aim_types( *you, *relevant );
        for( ranged::aim_type& type : aim_types ) {
            if( type.has_threshold ) { ctxt.register_action( type.action ); }
        }
        aim_mode = aim_types.begin();
        for( auto it = aim_types.begin(); it != aim_types.end(); ++it ) {
            if( you->preferred_aiming_mode == it->action ) {
                aim_mode = it; // default to persisted mode if possible
            }
        }
    }
    if( mode == TargetMode::Turrets ) { ctxt.register_action( "TOGGLE_TURRET_LINES" ); }
}

bool target_ui::handle_cursor_movement( const std::string& action, bool& skip_redraw )
{
    std::optional<tripoint_bub_ms> mouse_pos;
    const auto shift_view_or_cursor = [this]( const tripoint_rel_ms & delta ) {
        if( this->shifting_view ) {
            this->set_view_offset( this->you->view_offset + delta );
        } else {
            this->set_cursor_pos( dst + delta );
        }
    };

    if( action == "MOUSE_MOVE" || action == "TIMEOUT" ) {
        // Free aim: while the player holds RMB the reticle follows the pointer
        // continuously, no click needed. Polled on TIMEOUT as well as MOUSE_MOVE
        // because sdl_input suppresses MOUSE_MOVE actions while the cursor is
        // hidden, and hold-to-aim is exactly the mode that hides it — without the
        // poll, holding RMB gave you an aim overlay you could only nudge one
        // degree at a time with the arrow keys.
        //
        // Deliberately NOT extended to the keyboard targeting UI: there a stray
        // pointer bump would silently drop the target you cycled to with TAB.
        const bool aim_moved = opened_by_rmb && !shifting_view && track_mouse_aim();
        // Shift pos and/or view via edge scrolling
        auto edge_scroll = g->mouse_edge_scrolling_terrain( ctxt );
        if( edge_scroll == tripoint_rel_ms::zero() ) {
            skip_redraw = !aim_moved;
        } else {
            if( action == "MOUSE_MOVE" ) { edge_scroll += edge_scroll; }
            if( snap_to_target ) {
                set_cursor_pos( dst + edge_scroll );
            } else {
                set_view_offset( you->view_offset + edge_scroll );
            }
        }
    } else if( const std::optional<tripoint_rel_ms> delta = ctxt.get_direction( action ) ) {
        // Keyboard aim: rotate aim angle 1° per key press (Hotline Miami feel)
        if( shifting_view ) {
            set_view_offset( you->view_offset + *delta );
        } else {
            const auto aim_dx = units::cos( aim_angle );
            const auto aim_dy = units::sin( aim_angle );
            const auto key_dx = static_cast<double>( delta->x() );
            const auto key_dy = static_cast<double>( delta->y() );
            // z-component of cross product: positive = CCW, negative = CW
            const auto cross = aim_dx * key_dy - aim_dy * key_dx;
            const auto dot   = aim_dx * key_dx + aim_dy * key_dy;
            if( key_dx == 0.0 && key_dy == 0.0 ) {
                // z-only delta (LEVEL_UP/DOWN handled separately)
            } else if( cross > 0.0 ) {
                set_aim_angle( aim_angle - 1_degrees );   // CCW
            } else if( cross < 0.0 ) {
                set_aim_angle( aim_angle + 1_degrees );   // CW
            } else if( dot > 0.0 ) {
                set_aim_angle( aim_angle + 1_degrees );   // same direction: advance CW
            } else {
                set_aim_angle( aim_angle + 180_degrees ); // opposite direction: reverse
            }
        }
    } else if( action == "SELECT" ) {
        // Mouse aim: float pixel → exact angle → DDA ray
        if( const auto angle = ctxt.get_aim_angle_to_src( src ) ) {
            set_aim_angle( *angle );
        } else if( ( mouse_pos = ctxt.get_coordinates( g->w_terrain ) ) ) {
            // Curses fallback: tile-snap
            mouse_pos->z() = you->bub_pos().z() + you->view_offset.z();
            set_cursor_pos( *mouse_pos );
            sync_aim_angle_from_dst();
        }
    } else if( action == "LEVEL_UP" || action == "LEVEL_DOWN" ) {
        // Shift view/cursor up/down one z level
        auto delta = tripoint_rel_ms( 0, 0, action == "LEVEL_UP" ? 1 : -1 );
        shift_view_or_cursor( delta );
    } else if( action == "NEXT_TARGET" ) {
        cycle_targets( 1 );
        sync_aim_angle_from_dst();
    } else if( action == "PREV_TARGET" ) {
        cycle_targets( -1 );
        sync_aim_angle_from_dst();
    } else if( action == "CENTER" ) {
        if( shifting_view ) {
            set_view_offset( tripoint_rel_ms::zero() );
        } else {
            set_cursor_pos( src );
            aim_angle = 0_radians;
        }
    } else {
        return false;
    }

    return true;
}

bool target_ui::set_cursor_pos( const tripoint_bub_ms& new_pos )
{
    if( dst == new_pos ) { return false; }
    if( status == Status::OutOfAmmo && new_pos != src ) {
        // range == 0, no sense in moving cursor
        return false;
    }

    // Make sure new position is valid or find a closest valid position
    std::vector<tripoint_bub_ms> new_traj;
    tripoint_bub_ms valid_pos = new_pos;
    map& here = get_map();
    if( new_pos != src ) {
        // On Z axis, make sure we do not exceed map boundaries
        valid_pos.z() = clamp( valid_pos.z(), -OVERMAP_DEPTH, OVERMAP_HEIGHT );
        // Or current view range
        valid_pos.z() = clamp( valid_pos.z() - src.z(), -fov_3d_z_range, fov_3d_z_range ) + src.z();

        new_traj = here.find_clear_path( src, valid_pos );
        if( range == 1 ) {
            // We should always be able to hit adjacent squares
            if( square_dist( src, valid_pos ) > 1 ) { valid_pos = new_traj[0]; }
        } else if( trigdist ) {
            if( dist_fn( valid_pos ) > range ) {
                // Find the farthest point that is still in range
                for( size_t i = new_traj.size(); i > 0; i-- ) {
                    if( dist_fn( new_traj[i - 1] ) <= range ) {
                        valid_pos = new_traj[i - 1];
                        break;
                    }
                }
                // Sanity check
                if( dist_fn( valid_pos ) > range ) {
                    debugmsg( "Calculated trajectory exceeds allowed range!" );
                    valid_pos = src;
                }
            }
        } else {
            auto delta = valid_pos - src;
            valid_pos =
                src
                + tripoint_rel_ms( clamp( delta.x(), -range, range ), clamp( delta.y(), -range, range ),
                                   clamp( delta.z(), -range, range ) );
        }
    } else {
        new_traj.push_back( src );
    }

    if( valid_pos == dst ) {
        // We don't need to move the cursor after all
        return false;
    } else if( new_pos == valid_pos ) {
        // We can reuse new_traj
        dst = valid_pos;
        traj = new_traj;
    } else {
        dst = valid_pos;
        traj = here.find_clear_path( src, dst );
    }

    if( snap_to_target ) { set_view_offset( dst - src ); }

    // Make player's sprite flip to face the current target
    auto d = dst.xy() - src.xy();
    if( !tile_iso ) {

        if( d.x() > 0 ) {
            you->facing = FacingDirection::FD_RIGHT;
        } else if( d.x() < 0 ) {
            you->facing = FacingDirection::FD_LEFT;
        }
    } else {
        if( d.x() >= 0 && d.y() >= 0 ) { you->facing = FacingDirection::FD_RIGHT; }
        if( d.y() <= 0 && d.x() <= 0 ) { you->facing = FacingDirection::FD_LEFT; }
    }

    // Cache creature under cursor
    if( src != dst ) {
        Creature* cr = g->critter_at( dst, true );
        if( cr && pl_sees( *cr ) ) {
            dst_critter = cr;
        } else {
            dst_critter = nullptr;
        }
    } else {
        dst_critter = nullptr;
    }

    // Update mode-specific stuff
    if( mode == TargetMode::Fire ) {
        recalc_aim_turning_penalty();
    } else if( mode == TargetMode::Spell ) {
        const std::string fx = casting->effect();
        if( fx == "target_attack" || fx == "projectile_attack" || fx == "ter_transform" ) {
            spell_aoe = spell_effect::spell_effect_blast( *casting, src, dst, casting->aoe(), true );
        } else if( fx == "cone_attack" ) {
            spell_aoe = spell_effect::spell_effect_cone( *casting, src, dst, casting->aoe(), true );
        } else if( fx == "line_attack" ) {
            spell_aoe = spell_effect::spell_effect_line( *casting, src, dst, casting->aoe(), true );
        } else {
            spell_aoe.clear();
        }
    } else if( mode == TargetMode::Turrets ) {
        update_turrets_in_range();
    }

    if( mode == TargetMode::Shape || ( mode == TargetMode::Fire && shape_gen ) ) {
        std::shared_ptr<shape> sh = shape_gen->create( src.raw(), dst.raw() );
        projectile proj = make_gun_projectile( *relevant );
        // Same as in map::shoot (should probably be a function!)
        int expected_bash_force = std::accumulate(
                                      proj.impact.begin(), proj.impact.end(), 0.0,
        []( double acc, const damage_unit & du ) { return acc + du.amount + du.res_pen; } );
        shape_coverage = ranged::expected_coverage( *sh, here, expected_bash_force );
    }

    // Update UI controls & colors
    update_status();

    return true;
}

auto target_ui::set_aim_angle( units::angle angle ) -> void
{
    aim_angle = angle;
    const auto &here = get_map();
    const auto ray = here.ray_cast_angle( src, units::to_radians( angle ), range );
    if( ray.empty() ) { return; }
    traj = ray;
    dst  = ray.back();

    if( snap_to_target ) { set_view_offset( dst - src ); }

    // Update facing direction (mirrors set_cursor_pos)
    const auto d = dst.xy() - src.xy();
    if( !tile_iso ) {
        if( d.x() > 0 )      { you->facing = FacingDirection::FD_RIGHT; }
        else if( d.x() < 0 ) { you->facing = FacingDirection::FD_LEFT; }
    } else {
        if( d.x() >= 0 && d.y() >= 0 ) { you->facing = FacingDirection::FD_RIGHT; }
        if( d.y() <= 0 && d.x() <= 0 ) { you->facing = FacingDirection::FD_LEFT; }
    }

    // Critter under cursor (mirrors set_cursor_pos)
    if( src != dst ) {
        auto *const cr = g->critter_at( dst, true );
        dst_critter = ( cr && pl_sees( *cr ) ) ? cr : nullptr;
    } else {
        dst_critter = nullptr;
    }

    if( mode == TargetMode::Fire ) { recalc_aim_turning_penalty(); }
    update_status();
}

auto target_ui::sync_aim_angle_from_dst() -> void
{
    const auto dx = static_cast<double>( dst.x() - src.x() );
    const auto dy = static_cast<double>( dst.y() - src.y() );
    if( std::hypot( dx, dy ) > 0.01 ) {
        aim_angle = units::atan2( dy, dx );
    }
}

auto target_ui::track_mouse_aim() -> bool
{
    const auto px = get_aim_mouse_pos();
    // (0, 0) means neither SDL nor the tracked record has a real position yet;
    // honouring it would slam the aim into the map's top-left corner.
    if( px == point_zero || px == aim_commit_px ) { return false; }
    const auto drift = px - aim_commit_px;
    if( drift.x * drift.x + drift.y * drift.y < aim_jitter_px * aim_jitter_px ) {
        // Inside the buffer: leave the weapon pointed where it is. Returning
        // false also leaves skip_redraw set, so the whole overlay — cone, sight
        // line and reticle — holds still rather than twitching, which is the
        // point. The reticle can therefore trail the OS pointer by up to the
        // buffer radius; at 6 px that is not perceptible, and the system cursor
        // is still there for anyone who wants the exact pixel.
        return false;
    }
    const auto angle = aim_angle_from_pixel( px, src );
    if( !angle || *angle == aim_angle ) { return false; }
    aim_commit_px = px;
    set_aim_angle( *angle );
    return true;
}

auto target_ui::calc_spread_half_angle() const -> units::angle
{
    if( mode != TargetMode::Fire || !relevant || range < 1 ) { return 0_radians; }
    const auto &here = get_map();
    const auto disp = calculate_dispersion( here, *you, *relevant,
                                            static_cast<int>( predicted_recoil ), false );
    const auto miss_tiles = iso_tangent( static_cast<double>( range ),
                                         units::from_arcmin( disp.avg() ) );
    return units::from_radians( std::atan( miss_tiles / static_cast<double>( range ) ) );
}

void target_ui::on_range_ammo_changed()
{
    update_status();
    update_target_list();
}

void target_ui::update_target_list()
{
    if( range == 0 ) {
        targets.clear();
        return;
    }

    if( mode == TargetMode::TurretManual ) {
        targets = ranged::targetable_creatures( *you, range, *turret );
    } else {
        targets = ranged::targetable_creatures( *you, range );
    }

    map& here = get_map();
    const auto player_pos = you->bub_pos();

    std::ranges::sort( targets, {}, [&]( const Creature * c ) -> std::tuple<bool, bool, bool, int> {
        const auto target_pos = c->bub_pos();
        const auto z_diff = std::abs( player_pos.z() - target_pos.z() );
        const auto is_hostile = c->attitude_to( *you ) == Attitude::A_HOSTILE;
        const auto has_los = here.sees( player_pos, target_pos, range );
        return {!has_los, !is_hostile, z_diff, rl_dist_exact( target_pos, player_pos )};
    } );
}

tripoint_bub_ms target_ui::choose_initial_target()
{
    // If we're casting a spell, don't lock onto enemies if the spell is meant for using on
    // friendlies.
    if( mode == TargetMode::Spell && !casting->is_valid_target( valid_target::target_hostile ) ) {
        return src;
    }

    // Try previously targeted creature
    shared_ptr_fast<Creature> cr = you->last_target.lock();
    if( cr && pl_sees( *cr ) && dist_fn( cr->bub_pos() ) <= range ) { return cr->bub_pos(); }

    // Try closest creature
    if( !targets.empty() ) { return targets[0]->bub_pos(); }

    // Try closest practice target
    map& here = get_map();
    const std::vector<tripoint_bub_ms> nearby = closest_points_first( src, range );
    const auto target_spot =
    std::find_if( nearby.begin(), nearby.end(), [this, &here]( const tripoint_bub_ms & pt ) {
        return ( here.has_flag_ter_or_furn( flag_SHOOT_ME, pt )
                 || here.tr_at( pt ).id == tr_practice_target )
               && this->you->sees( pt );
    } );
    if( target_spot != nearby.end() ) { return *target_spot; }

    // We've got nothing.
    return src;
}

bool target_ui::try_reacquire_target( bool critter, tripoint_bub_ms& new_dst )
{
    if( critter ) {
        // Try to re-acquire the creature
        shared_ptr_fast<Creature> cr = you->last_target.lock();
        if( cr && pl_sees( *cr ) && dist_fn( cr->bub_pos() ) <= range ) {
            new_dst = cr->bub_pos();
            return true;
        }
    }

    if( !you->last_target_pos.has_value() ) {
        // This shouldn't happen
        return false;
    }

    // Try to re-acquire target tile or tile where the target creature used to be
    auto local_lt = get_map().abs_to_bub( *you->last_target_pos );
    if( !outside_visible_z_range( src, local_lt ) && dist_fn( local_lt ) <= range ) {
        new_dst = local_lt;
        // Abort aiming if a creature moved in
        return !critter && !g->critter_at( local_lt, true );
    }

    // We moved out of range
    return false;
}

void target_ui::update_status()
{
    std::vector<std::string> msgbuf;
    if( mode == TargetMode::Turrets && turrets_in_range.empty() ) {
        // None of the turrets are in range
        status = Status::OutOfRange;
    } else if(
        mode == TargetMode::Fire
        && ( !ranged::gunmode_checks_common( *you, get_map(), msgbuf, relevant->gun_current_mode() )
             || !ranged::
             gunmode_checks_weapon( *you, get_map(), msgbuf, relevant->gun_current_mode() ) ) ) {
        // Selected gun mode is empty
        // TODO: it might be some other error, but that's highly unlikely to happen, so a catch-all
        // 'Out of ammo' is fine
        status = Status::OutOfAmmo;
    } else if(
        mode == TargetMode::TurretManual
        && ( turret->query() != turret_data::status::ready
             || !ranged::
             gunmode_checks_common( *you, get_map(), msgbuf, relevant->gun_current_mode() ) ) ) {
        status = Status::OutOfAmmo;
    } else if(
        ( src == dst )
        && !( mode == TargetMode::Spell && casting->is_valid_target( valid_target::target_self ) ) ) {
        // TODO: consider allowing targeting yourself with turrets
        status = Status::BadTarget;
    } else if( dist_fn( dst ) > range ) {
        // We're out of range. This can happen if we switch from long-ranged
        // gun mode to short-ranged. We can, of course, move the cursor into range automatically,
        // but that would be rude. Instead, wait for directional keys/etc. and *then* move the
        // cursor.
        status = Status::OutOfRange;
    } else {
        status = Status::Good;
    }
}

int target_ui::dist_fn( const tripoint_bub_ms& p )
{
    return static_cast<int>( std::round( rl_dist_exact( src, p ) ) );
}

void target_ui::set_last_target()
{
    you->last_target_pos = get_map().bub_to_abs( dst );
    if( dst_critter ) {
        you->last_target = g->shared_from( *dst_critter );
    } else {
        you->last_target.reset();
    }
}

bool target_ui::confirm_non_enemy_target()
{
    npc* const who = dynamic_cast<npc *>( dst_critter );
    if( who && !who->guaranteed_hostile() ) {
        return query_yn( _( "Really attack %s?" ), who->name.c_str() );
    }
    return true;
}

bool target_ui::prompt_friendlies_in_lof()
{
    if( mode != TargetMode::Fire ) {
        debugmsg( "Not implemented" );
        return true;
    }

    std::vector<weak_ptr_fast<Creature>> in_lof = list_friendlies_in_lof();
    std::vector<Creature *> new_in_lof;
    for( const weak_ptr_fast<Creature> &cr_ptr : in_lof ) {
        bool found = false;
        shared_ptr_fast<Creature> ptr_lock = cr_ptr.lock();
        Creature* cr = ptr_lock.get();
        for( const weak_ptr_fast<Creature> &cr2_ptr : activity->acceptable_losses ) {
            shared_ptr_fast<Creature> ptr2_lock = cr2_ptr.lock();
            Creature* cr2 = ptr2_lock.get();
            if( cr == cr2 ) {
                found = true;
                break;
            }
        }
        if( !found ) { new_in_lof.push_back( cr ); }
    }

    if( new_in_lof.empty() ) { return true; }

    std::string msg = _( "There are friendly creatures in line of fire:\n" );
    for( Creature * cr : new_in_lof ) { msg += "  " + cr->disp_name() + "\n"; }
    msg += _( "Proceed with the attack?" );
    return query_yn( msg );
}

std::vector<weak_ptr_fast<Creature>> target_ui::list_friendlies_in_lof()
{
    std::vector<weak_ptr_fast<Creature>> ret;
    if( mode == TargetMode::Turrets || mode == TargetMode::Spell ) {
        debugmsg( "Not implemented" );
        return ret;
    }
    for( const tripoint_bub_ms& p : traj ) {
        if( p != dst && p != src ) {
            Creature* cr = g->critter_at( p, true );
            if( cr && you->sees( *cr ) ) {
                Attitude a = cr->attitude_to( *this->you );
                if( ( cr->is_npc() && a != Attitude::A_HOSTILE )
                    || ( !cr->is_npc() && a == Attitude::A_FRIENDLY ) ) {
                    ret.emplace_back( g->shared_from( *cr ) );
                }
            }
        }
    }
    return ret;
}

void target_ui::toggle_snap_to_target()
{
    shifting_view = false;
    if( snap_to_target ) {
        // Keep current view offset
    } else {
        set_view_offset( dst - src );
    }
    snap_to_target = !snap_to_target;
}

void target_ui::cycle_targets( int direction )
{
    if( targets.empty() ) {
        // Nothing to cycle
        return;
    }

    if( dst_critter ) {
        auto t = std::find( targets.begin(), targets.end(), dst_critter );
        size_t new_target = 0;
        if( t != targets.end() ) {
            size_t idx = std::distance( targets.begin(), t );
            new_target = ( idx + targets.size() + direction ) % targets.size();
            set_cursor_pos( targets[new_target]->bub_pos() );
            return;
        }
    }

    // There is either no creature under the cursor or the player can't see it.
    // Use the closest/farthest target in this case
    if( direction == 1 ) {
        set_cursor_pos( targets.front()->bub_pos() );
    } else {
        set_cursor_pos( targets.back()->bub_pos() );
    }
}

void target_ui::set_view_offset( const tripoint_rel_ms& new_offset )
{
    tripoint_rel_ms new_( new_offset.xy(), clamp( new_offset.z(), -fov_3d_z_range, fov_3d_z_range ) );
    new_.z() = clamp( new_.z() + src.z(), -OVERMAP_DEPTH, OVERMAP_HEIGHT ) - src.z();

    bool changed_z = you->view_offset.z() != new_.z();
    you->view_offset = new_;
    if( changed_z ) {
        // We need to do a bunch of cache updates since we're
        // looking at a different z-level.
        get_map().invalidate_map_cache( new_.z() );
    }
}

void target_ui::update_turrets_in_range()
{
    turrets_in_range.clear();
    for( vehicle_part * t : *vturrets ) {
        turret_data td = veh->turret_query( *t );
        if( td.in_range( bub_to_abs( dst ) ) ) {
            tripoint_bub_ms src = veh->bub_part_location( *t );
            turrets_in_range.push_back( {t, line_to( src, dst )} );
        }
    }
}

void target_ui::recalc_aim_turning_penalty()
{
    if( status != Status::Good ) {
        // We don't care about invalid situations
        predicted_recoil = MAX_RECOIL;
        return;
    }

    double curr_recoil = you->recoil;
    tripoint_bub_ms curr_recoil_pos;
    shared_ptr_fast<Creature> ptr_lock = you->last_target.lock();
    const Creature* lt_ptr = ptr_lock.get();
    if( lt_ptr ) {
        curr_recoil_pos = lt_ptr->bub_pos();
    } else if( you->last_target_pos ) {
        curr_recoil_pos = get_map().abs_to_bub( *you->last_target_pos );
    } else {
        curr_recoil_pos = src;
    }

    if( curr_recoil_pos == dst ) {
        // We're aiming at that point right now, no penalty
        predicted_recoil = curr_recoil;
    } else if( curr_recoil_pos == src ) {
        // The player wasn't aiming anywhere, max it out
        predicted_recoil = MAX_RECOIL;
    } else {
        // Raise it proportionally to how much
        // the player has to turn from previous aiming point
        const double recoil_per_degree = MAX_RECOIL / 180.0;
        const units::angle angle_curr = coord_to_angle( src, curr_recoil_pos );
        const units::angle angle_desired = coord_to_angle( src, dst );
        const units::angle phi = normalize( angle_curr - angle_desired );
        const units::angle angle = std::min( phi, 360.0_degrees - phi );
        predicted_recoil =
            std::min( MAX_RECOIL, curr_recoil + to_degrees( angle ) * recoil_per_degree );
    }
}

void target_ui::apply_aim_turning_penalty() { you->recoil = predicted_recoil; }

void target_ui::action_switch_mode()
{
    uilist menu;
    menu.settext( _( "Select preferences" ) );

    std::vector<std::function<void()>> on_select;

    if( !aim_types.empty() ) {
        menu.addentry( -1, false, 0, "  " + std::string( _( "Default aiming mode" ) ) );
        menu.entries.back().force_color = true;
        menu.entries.back().text_color = c_cyan;

        for( auto it = aim_types.begin(); it != aim_types.end(); ++it ) {
            const bool is_active_aim_mode = aim_mode == it;
            const std::string text =
                ( it->name.empty() ? _( "Immediate" ) : it->name )
                + ( is_active_aim_mode ? _( " (active)" ) : "" );

            menu.addentry( on_select.size(), true, MENU_AUTOASSIGN, text );
            on_select.emplace_back( [it, this]() {
                aim_mode = it;
                you->preferred_aiming_mode = it->action;
            } );
            if( is_active_aim_mode ) { menu.entries.back().text_color = c_light_green; }
        }
    }

    const std::map<gun_mode_id, gun_mode> &all_gun_modes = relevant->gun_all_modes();
    if( !all_gun_modes.empty() ) {
        menu.addentry( -1, false, 0, "  " + std::string( _( "Firing mode" ) ) );
        menu.entries.back().force_color = true;
        menu.entries.back().text_color = c_cyan;

        for( const auto& mode : all_gun_modes ) {
            if( mode.second.melee() ) { continue; }
            const bool active_gun_mode = relevant->gun_get_mode_id() == mode.first;

            // If gun mode is from a gunmod use gunmod's name, pay attention to the "->" on tname
            std::string text =
                ( mode.second.target == relevant )
                ? mode.second.tname()
                : mode.second->tname() + " (" + std::to_string( mode.second.qty ) + ")";

            text += ( active_gun_mode ? _( " (active)" ) : "" );

            menu.entries
            .emplace_back( static_cast<int>( on_select.size() ), true, MENU_AUTOASSIGN, text );
            on_select.emplace_back( [mode, this]() { relevant->gun_set_mode( mode.first ); } );
            if( active_gun_mode ) {
                menu.entries.back().text_color = c_light_green;
                if( menu.selected == 0 ) { menu.selected = menu.entries.size() - 1; }
            }
        }
    }

    menu.query();
    if( menu.ret >= 0 && menu.ret < static_cast<int>( on_select.size() ) ) {
        size_t i = static_cast<size_t>( menu.ret );
        on_select[i]();
    } // else - just refresh

    ensure_ranged_gun_mode();
    update_ammo_range_from_gun_mode();
    on_range_ammo_changed();
}

void target_ui::ensure_ranged_gun_mode()
{
    while( relevant->gun_current_mode().melee() ) { relevant->gun_cycle_mode(); }
}

void target_ui::update_ammo_range_from_gun_mode()
{
    if( mode == TargetMode::TurretManual ) {
        itype_id ammo_current = turret->ammo_current();
        // Test no-ammo and not a UPS weapon
        if( !ammo_current && ( relevant->get_gun_ups_drain() == 0 ) ) {
            ammo = nullptr;
            range = 0;
        } else {
            ammo = &*ammo_current;
            range = turret->range();
        }
    } else {
        ammo = activity->reload_loc
               ? activity->reload_loc->type
               : relevant->gun_current_mode().target->ammo_data();
        if( activity->reload_loc ) {
            item* temp_weapon = item::spawn_temporary( *relevant );
            temp_weapon->ammo_set( ammo->get_id() );
            range = temp_weapon->gun_current_mode().target->gun_range( you );
        } else {
            range = relevant->gun_current_mode().target->gun_range( you );
        }
    }
}

bool target_ui::action_switch_ammo()
{
    if( mode == TargetMode::TurretManual ) {
        // For turrets that use vehicle tanks & can fire multiple liquids
        if( turret->ammo_options().size() > 1 ) {
            const auto opts = turret->ammo_options();
            auto iter = opts.find( turret->ammo_current() );
            turret->ammo_select( ++iter != opts.end() ? *iter : *opts.begin() );
            ammo = &*turret->ammo_current();
            range = turret->range();
        }
    } else {
        // Leave aiming UI and open reloading UI since
        // reloading annihilates our aim anyway
        return false;
    }
    on_range_ammo_changed();
    return true;
}

bool target_ui::action_aim()
{
    set_last_target();
    apply_aim_turning_penalty();
    const double min_recoil = calculate_aim_cap( *you, dst );
    for( int i = 0; i < 10; ++i ) { do_aim( *you, *relevant, min_recoil ); }

    // We've changed pc.recoil, update penalty
    recalc_aim_turning_penalty();

    return you->moves > 0;
}

bool target_ui::action_aim_and_shoot( const std::string& action )
{
    std::vector<ranged::aim_type>::iterator it;
    for( it = aim_types.begin(); it != aim_types.end(); it++ ) {
        if( action == it->action ) { break; }
    }
    if( it == aim_types.end() ) {
        debugmsg( "Could not find a valid aim_type for %s", action.c_str() );
        aim_mode = aim_types.begin();
    }
    int aim_threshold = it->threshold;
    set_last_target();
    apply_aim_turning_penalty();
    const double min_recoil = calculate_aim_cap( *you, dst );
    do {
        do_aim( *you, relevant ? *relevant : null_item_reference(), min_recoil );
    } while(
        you->moves > 0 && you->recoil > aim_threshold
        && you->recoil - sight_dispersion > min_recoil );

    // If we made it under the aim threshold, go ahead and fire.
    // Also fire if we're at our best aim level already.
    // If no critter is at dst then sight dispersion does not apply,
    // so it would lock into an infinite loop.
    bool done_aiming =
        you->recoil <= aim_threshold || you->recoil - sight_dispersion == min_recoil
        || ( !g->critter_at( dst ) && you->recoil == min_recoil );
    return done_aiming;
}

void target_ui::draw_terrain_overlay()
{
    auto center = you->bub_pos() + you->view_offset;

    // Removes parts that don't belong to currently visible Z level
    const auto filter_this_z = [&center]( const std::vector<tripoint_bub_ms> &traj ) {
        std::vector<tripoint_bub_ms> this_z = traj;
        this_z.erase(
            std::remove_if( this_z.begin(), this_z.end(),
        [&center]( const tripoint_bub_ms & p ) { return p.z() != center.z(); } ),
        this_z.end() );
        return this_z;
    };

    // FIXME: TILES version of g->draw_line helpfully draws a cursor at last point.
    //        This creates a fake cursor if 'dst' is on a z-level we cannot see.

    // Draw approximate line of fire for each turret in range
    if( mode == TargetMode::Turrets && draw_turret_lines ) {
        // TODO: TILES version doesn't know how to draw more than 1 line at a time.
        //       We merge all lines together and draw them as a big malformed one
        std::set<tripoint_bub_ms> points;
        for( const turret_with_lof& it : turrets_in_range ) {
            std::vector<tripoint_bub_ms> this_z = filter_this_z( it.line );
            for( const tripoint_bub_ms& p : this_z ) { points.insert( p ); }
        }
        // Since "trajectory" for each turret is just a straight line,
        // we can draw it even if the player can't see some parts
        points.erase( dst ); // Workaround for fake cursor on TILES
        std::vector<tripoint_bub_ms> l( points.begin(), points.end() );
        if( dst.z() == center.z() ) {
            // Workaround for fake cursor bug on TILES
            l.push_back( dst );
        }
        g->draw_line( src, center, l, true );
    }

    // Draw trajectory as a tile path. Fire and Throw are excluded: they each have a
    // pixel-accurate, tile-independent overlay below (the spread cone + laser line,
    // and the ballistic arc + impact ring). Drawing both put a discrete tile ladder
    // underneath a continuous cone, which reads as two disagreeing aim indicators.
    // Spell / Shape / TurretManual have no such replacement, so they keep the path.
    const bool has_pixel_aim_overlay = mode == TargetMode::Fire
                                       || mode == TargetMode::Throw
                                       || mode == TargetMode::ThrowBlind;
    if( mode != TargetMode::Turrets && !has_pixel_aim_overlay && dst != src ) {
        std::vector<tripoint_bub_ms> this_z = filter_this_z( traj );

        // Draw a highlighted trajectory only if we can see the endpoint.
        // Provides feedback to the player, but avoids leaking information
        // about tiles they can't see.
        g->draw_line( dst, center, this_z );
    }

    // Since draw_line does nothing if destination is not visible,
    // cursor also disappears. Draw it explicitly.
    if( dst.z() == center.z() ) { g->draw_cursor( dst ); }

    // Draw spell AOE
    if( mode == TargetMode::Spell ) {
        for( const tripoint_bub_ms& tile : spell_aoe ) {
            if( tile.z() != center.z() ) { continue; }
            g->draw_highlight( tile );
        }
    } else if( mode == TargetMode::Shape || ( mode == TargetMode::Fire && shape_gen ) ) {
        for( const std::pair<const tripoint_bub_ms, double> &pr : shape_coverage ) {
            const tripoint_bub_ms& tile = pr.first;
            g->draw_highlight( tile );
        }
    }

    // Pixel-accurate spread cone + sight line + reticle
    if( mode == TargetMode::Fire && dst != src ) {
        const auto half = calc_spread_half_angle();
        g->draw_aim_cone( src.xy(), static_cast<float>( units::to_radians( aim_angle ) ),
                          static_cast<float>( units::to_radians( half ) ), range, src.z() );
        // The reticle sits at the pointer, not at the snapped destination tile:
        // free aim is continuous and the tile the DDA ray lands on is only ever an
        // approximation of where the player is actually pointing.
        g->draw_aim_crosshair( get_aim_mouse_pos() );
    }

    // Throw arc and impact indicator
    if( ( mode == TargetMode::Throw || mode == TargetMode::ThrowBlind ) && dst != src ) {
        g->draw_throw_arc( src, dst, static_cast<float>( throw_charge ) );
        const auto max_r_tiles = ( relevant && static_cast<bool>( relevant->type->explosion ) )
                                 ? relevant->type->explosion.radius
                                 : 0.5f;
        g->draw_throw_impact( dst, max_r_tiles );
    }
}

std::string target_ui::panel_text()
{
    // Reproduces the old draw_ui_window's section order as colour-tagged text,
    // including the aim/hit-chance readout (slice 2b, appended below).
    std::vector<std::string> L;

    L.push_back( colorize( uitext_title(), c_red ) );

    {
        std::string label_range =
            ( src == dst )
            ? string_format( "Range: %d", range )
            : string_format( "Range: %d/%d", dist_fn( dst ), range );
        if( status == Status::OutOfRange && mode != TargetMode::Turrets ) {
            label_range = colorize( label_range, c_red );
        }
        std::string row = label_range;
        if( allow_zlevel_shift ) {
            row += "  " + string_format( _( "Elevation: %d" ), dst.z() - src.z() );
        }
        row += "  " + string_format( _( "Targets: %d" ), targets.size() );
        L.push_back( row );
    }

    if( mode == TargetMode::Fire || mode == TargetMode::TurretManual
        || ( mode == TargetMode::Shape && relevant->is_gun() ) ) {
        gun_mode m = relevant->gun_current_mode();
        const std::string gunmod_name = ( m.target != relevant ) ? m->tname() + " " : std::string();
        L.push_back( string_format(
                         _( "Firing mode: <color_cyan>%s%s (%d)</color>" ), gunmod_name, m.tname(), m.qty ) );
        if( status == Status::OutOfAmmo ) {
            L.push_back( colorize( _( "OUT OF AMMO" ), c_red ) );
        } else if( ammo ) {
            L.push_back( string_format(
                             m->ammo_remaining() ? _( "Ammo: %s (%d/%d)" ) : _( "Ammo: %s" ),
                             colorize( ammo->nname( std::max( m->ammo_remaining(), 1 ) ), ammo->color ),
                             m->ammo_remaining(), m->ammo_capacity() ) );
        }
        const int val = ranged::recoil_total( *you );
        const int min_recoil = ranged::effective_dispersion( *you, relevant->sight_dispersion() );
        const int recoil_range = MAX_RECOIL - min_recoil;
        std::string rc;
        if( val >= min_recoil + ( recoil_range * 2 / 3 ) ) {
            rc = pgettext( "amount of backward momentum", "<color_red>High</color>" );
        } else if( val >= min_recoil + ( recoil_range / 2 ) ) {
            rc = pgettext( "amount of backward momentum", "<color_yellow>Medium</color>" );
        } else if( val >= min_recoil + ( recoil_range / 4 ) ) {
            rc = pgettext( "amount of backward momentum", "<color_light_green>Low</color>" );
        } else {
            rc = pgettext( "amount of backward momentum", "<color_cyan>None</color>" );
        }
        L.push_back( string_format( _( "Recoil: %s" ), rc ) );
    } else if( mode == TargetMode::Spell ) {
        L.push_back( colorize(
                         string_format( _( "Casting: %s (Level %u)" ), casting->name(), casting->get_level() ),
                         c_light_green ) );
        if( !no_mana || casting->energy_source() == energy_type::none_energy ) {
            if( casting->energy_source() == energy_type::hp_energy ) {
                L.push_back( string_format(
                                 _( "Cost: %s %s" ), casting->energy_cost_string( *you ), casting->energy_string() ) );
            } else {
                L.push_back( string_format(
                                 _( "Cost: %s %s (Current: %s)" ), casting->energy_cost_string( *you ),
                                 casting->energy_string(), casting->energy_cur_string( *you ) ) );
            }
        }
        if( no_fail ) {
            L.push_back( colorize( _( "0.0 % Failure Chance" ), c_light_green ) );
        } else {
            L.push_back( casting->colorized_fail_percent( *you ) );
        }
        if( casting->aoe() > 0 ) {
            const std::string fx = casting->effect();
            const std::string aoes = casting->aoe_string();
            if( fx == "projectile_attack" || fx == "target_attack" || fx == "area_pull"
                || fx == "area_push" || fx == "ter_transform" ) {
                L.push_back( string_format(
                                 _( "Effective Spell Radius: %s%s" ), aoes,
                                 casting->in_aoe( src, dst )
                                 ? colorize( _( " WARNING!  IN RANGE" ), c_red )
                                 : std::string() ) );
            } else if( fx == "cone_attack" ) {
                L.push_back( string_format( _( "Cone Arc: %s degrees" ), aoes ) );
            } else if( fx == "line_attack" ) {
                L.push_back( string_format( _( "Line width: %s" ), aoes ) );
            }
        }
        L.push_back(
            colorize( string_format( _( "Damage: %s" ), casting->damage_string( *you ) ), c_light_red ) );
        L.push_back( casting->description() );
    }

    if( dst_critter ) {
        if( you->sees( *dst_critter ) ) {
            const std::string info = dst_critter->print_info_text();
            if( !info.empty() ) { L.push_back( info ); }
        } else {
            std::vector<std::string> buf;
            if( you->sees_with_infrared( *dst_critter ) ) {
                dst_critter->describe_infrared( buf );
            } else if( you->sees_with_specials( *dst_critter ) ) {
                dst_critter->describe_specials( buf );
            }
            for( const std::string& b : buf ) { L.push_back( b ); }
        }
    }

    if( mode == TargetMode::Turrets ) {
        L.push_back(
            string_format( _( "Turrets in range: %d/%d" ), turrets_in_range.size(), vturrets->size() ) );
        for( const turret_with_lof& it : turrets_in_range ) {
            L.push_back( string_format( "* %s", it.turret->name() ) );
        }
    } else if( status == Status::Good
               && ( mode == TargetMode::Fire || mode == TargetMode::Throw
                    || mode == TargetMode::ThrowBlind ) ) {
        // Slice 2b: the aim / hit-chance readout. Bars use the same char width as the
        // curses w_target panel (34/55 minus borders) so they read identically.
        const int bar_width = getmaxx( w_target ) - 2;
        std::vector<std::string> aim;
        if( mode == TargetMode::Fire ) {
            // Mirrors panel_fire_mode_aim: simulate predicted recoil + aim delay.
            const double saved_pc_recoil = you->recoil;
            you->recoil = predicted_recoil;
            double pred_recoil = you->recoil;
            int predicted_delay = 0;
            if( aim_mode->has_threshold && aim_mode->threshold < you->recoil ) {
                do {
                    const double aim_amount = ranged::aim_per_move( *you, *relevant, pred_recoil );
                    if( aim_amount > 0 ) {
                        predicted_delay++;
                        pred_recoil = std::max( pred_recoil - aim_amount, 0.0 );
                    }
                } while( pred_recoil > aim_mode->threshold && pred_recoil - sight_dispersion > 0 );
            } else {
                pred_recoil = you->recoil;
            }
            const double target_size =
                dst_critter ? dst_critter->ranged_target_size()
                : occupied_tile_fraction( creature_size::medium );
            item* load_loc = activity->reload_loc ? &*activity->reload_loc : nullptr;
            aim = aim_lines( *you, bar_width, ctxt, *relevant->gun_current_mode(), target_size, dst,
                             pred_recoil, load_loc );
            if( aim_mode->has_threshold ) {
                aim.push_back( string_format( _( "%s Delay: %i" ), aim_mode->name, predicted_delay ) );
            }
            you->recoil = saved_pc_recoil;
        } else {
            const bool blind = ( mode == TargetMode::ThrowBlind );
            aim = throw_aim_lines( *you, bar_width, ctxt, *relevant, dst, blind );
        }
        L.insert( L.end(), aim.begin(), aim.end() );
    }

    {
        const auto bound_key = [this]( const std::string & s ) -> char {
            const std::vector<char> keys = ctxt.keys_bound_to( s );
            return keys.empty() ? ' ' : keys.front();
        };
        L.push_back(
            shifting_view
            ? _( "Shift view with directional keys" )
            : _( "Move cursor with directional keys" ) );
        if( is_mouse_enabled() ) {
            L.push_back( std::string( _( "Mouse: LMB: Target, Wheel: Cycle," ) ) + " " + _( "RMB: Fire" ) );
        }
        L.push_back(
            string_format( _( "[%s] Cycle targets;" ), ctxt.get_desc( "NEXT_TARGET", 1 ) ) + " "
            + string_format( _( "[%c] %s." ), bound_key( "FIRE" ), uitext_fire() ) );
        L.push_back( string_format(
                         _( "[%c] target self; [%c] toggle snap-to-target" ), bound_key( "CENTER" ),
                         bound_key( "TOGGLE_SNAP_TO_TARGET" ) ) );
        if( mode == TargetMode::Fire ) {
            std::string aim_and_fire;
            for( const ranged::aim_type& e : aim_types ) {
                if( e.has_threshold ) {
                    aim_and_fire += string_format( "[%c] ", bound_key( e.action ) );
                }
            }
            aim_and_fire += _( "to aim and fire." );
            L.push_back( string_format( _( "[%c] to steady your aim.  (10 moves)" ), bound_key( "AIM" ) ) );
            L.push_back( aim_and_fire );
        }
        if( mode == TargetMode::Fire || mode == TargetMode::TurretManual
            || ( mode == TargetMode::Shape && relevant->is_gun() ) ) {
            L.push_back( string_format( _( "[%c] to switch firing modes." ), bound_key( "SWITCH_MODE" ) ) );
            L.push_back( string_format( _( "[%c] to switch ammo." ), bound_key( "SWITCH_AMMO" ) ) );
        }
        if( mode == TargetMode::Turrets ) {
            const std::string label =
                draw_turret_lines ? _( "[%c] Hide lines of fire" ) : _( "[%c] Show lines of fire" );
            L.push_back( string_format( label, bound_key( "TOGGLE_TURRET_LINES" ) ) );
        }
    }

    std::string out;
    for( size_t i = 0; i < L.size(); i++ ) {
        if( i > 0 ) { out += '\n'; }
        out += L[i];
    }
    return out;
}

std::string target_ui::uitext_title()
{
    switch( mode ) {
        case TargetMode::Fire:
        case TargetMode::TurretManual:
            return string_format( _( "Firing %s" ), relevant->tname() );
        case TargetMode::Throw:
            return string_format( _( "Throwing %s" ), relevant->tname() );
        case TargetMode::ThrowBlind:
            return string_format( _( "Blind throwing %s" ), relevant->tname() );
        default:
            return _( "Set target" );
    }
}

std::string target_ui::uitext_fire()
{
    if( mode == TargetMode::Throw || mode == TargetMode::ThrowBlind ) {
        return to_translation( "[Hotkey] to throw", "to throw" ).translated();
    } else if( mode == TargetMode::Reach ) {
        return to_translation( "[Hotkey] to attack", "to attack" ).translated();
    } else if( mode == TargetMode::Spell ) {
        return to_translation( "[Hotkey] to cast the spell", "to cast" ).translated();
    } else {
        return to_translation( "[Hotkey] to fire", "to fire" ).translated();
    }
}

void target_ui::on_target_accepted( bool harmful )
{
    // TODO: all of this should be moved into on-hit code
    const auto lt_ptr = you->last_target.lock();
    if( npc * const guy = dynamic_cast<npc * >( lt_ptr.get() ) ) {
        if( harmful ) {
            if( !guy->guaranteed_hostile() ) {
                // TODO: get rid of this. Or combine it with effect_hit_by_player
                guy->hit_by_player = true; // used for morale penalty
            }
            guy->make_angry();
        }
    } else if( monster * const mon = dynamic_cast<monster * >( lt_ptr.get() ) ) {
        mon->add_effect( effect_hit_by_player, 10_minutes );
    }
}

