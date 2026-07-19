// Extracted from game.cpp — B5 decomposition (activity cluster).
// All game:: methods remain declared in game.h (unchanged).
#include "game.h"

#include <chrono>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "activity_monmove_cache.h"
#include "activity_time_cadence.h"
#include "avatar.h"
#include "batch_turns.h"
#include "calendar.h"
#include "cached_options.h"
#include "catalua_hooks.h"
#include "catalua.h"
#include "catalua_sol.h"
#include "character_functions.h"
#include "character_turn.h"
#include "creature_tracker.h"
#include "debug.h"
#include "distraction_manager.h"
#include "explosion.h"
#include "explosion_queue.h"
#include "fluid_grid.h"
#include "game_constants.h"
#include "map.h"
#include "init.h"
#include "gamemode.h"
#include "mapbuffer_registry.h"
#include "mapbuffer.h"
#include "messages.h"
#include "npc.h"
#include "mission.h"
#include "popup.h"
#include "monster_action.h"
#include "options.h"
#include "ui_manager.h"
#include "output.h"
#include "overmapbuffer.h"
#include "vehicle.h"
#include "player_activity.h"
#include "rng.h"
#include "sounds.h"
#include "string_formatter.h"
#include "submap.h"
#include "submap_load_manager.h"
#include "timed_event.h"
#include "translations.h"
#include "weather.h"

#include "coop_proto.h"
#include "coop_server.h"

#include "profile.h"

#define dbg(x) DebugLogFL((x),DC::Game)

// File-local string-IDs — mirror the declarations in game.cpp
static const activity_id ACT_AUTODRIVE( "ACT_AUTODRIVE" );

static const efftype_id effect_sleep( "sleep" );

static const trait_id trait_HAS_NEMESIS( "HAS_NEMESIS" );

void game::process_voluntary_act_interrupt()
{
    if( u.has_effect( effect_sleep ) ) {
        // Can't interrupt
        return;
    }

    bool has_activity = u.activity && !u.activity->complete();
    bool is_travelling = u.has_destination() && !u.omt_path.empty();

    if( !has_activity && !is_travelling ) {
        // Nohing to interrupt
        return;
    }

    // Key poll may be quite expensive, so limit it to 10 times per second.
    static auto last_poll = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    int64_t difference = std::chrono::duration_cast<std::chrono::milliseconds>
                         ( now - last_poll ).count();

    if( difference > 100 ) {
        handle_key_blocking_activity();
        last_poll = now;
    }

    // If player is performing a task and a monster is dangerously close, warn them
    // regardless of previous safemode warnings.
    // Distraction Manager can change this.
    if( ( has_activity || is_travelling ) && !u.has_activity( activity_id( "ACT_AIM" ) ) &&
        !u.activity->is_distraction_ignored( distraction_type::hostile_spotted_near ) ) {
        Creature *hostile_critter = is_hostile_very_close();
        if( hostile_critter != nullptr ) {
            cancel_activity_or_ignore_query( distraction_type::hostile_spotted_near,
                                             string_format( _( "The %s is dangerously close!" ),
                                                     hostile_critter->get_name() ) );
        }
    }
}

void game::process_activity()
{
    ZoneScoped;
    if( !u.activity ) {
        return;
    }

    while( u.moves > 0 && *u.activity ) {
        u.activity->do_turn( u );
    }
}

auto game::activity_fixed_window_duration() -> time_duration
{
    auto duration = activity_time_cadence::fixed_window();
    const weather_manager &weather = get_weather();
    if( weather.weather_id && weather.nextweather > calendar::turn ) {
        duration = std::min( duration, weather.nextweather - calendar::turn );
    }
    return duration;
}

auto game::has_activity_skip_blocking_npc_state() -> bool
{
    return npcs_dirty;
}

auto game::has_activity_skip_relevant_vehicle() -> bool
{
    return std::ranges::any_of( m.get_vehicles(), []( const wrapped_vehicle & wrapped ) {
        const vehicle *veh = wrapped.v;
        return veh != nullptr &&
               ( veh->is_moving() || veh->vertical_velocity != 0 || veh->skidding ||
                 veh->is_falling || veh->engine_on || veh->is_autodriving ||
                 veh->is_following || veh->is_patrolling || veh->autopilot_on ||
                 veh->is_alarm_on || veh->check_environmental_effects ||
                 veh->total_accessory_epower_w() < 0 );
    } );
}

auto game::has_activity_skip_active_fire() -> bool
{
    const auto submap_has_active_fire = []( submap & sm ) {
        if( sm.field_count == 0 ) {
            return false;
        }
        for( const auto &local : sm.field_cache ) {
            auto &curfield = sm.get_field( local );
            for( auto &field_pair : curfield ) {
                auto &cur = field_pair.second;
                if( cur.is_field_alive() && cur.get_field_type().obj().has_fire ) {
                    return true;
                }
            }
        }
        return false;
    };

    const auto zmin = m.has_zlevels() ? -OVERMAP_DEPTH : m.get_abs_sub().z();
    const auto zmax = m.has_zlevels() ? OVERMAP_HEIGHT : m.get_abs_sub().z();
    const auto axis = std::views::iota( 0, m.getmapsize() );
    for( const auto x : axis ) {
        for( const auto y : axis ) {
            const auto p = point_bub_sm( x, y );
            for( const auto z : std::views::iota( zmin, zmax + 1 ) ) {
                auto *sm = m.get_submap_at_grid( tripoint_bub_sm( p, z ) );
                if( sm != nullptr && submap_has_active_fire( *sm ) ) {
                    return true;
                }
            }
        }
    }

    auto has_fire = false;
    const auto current_dim = m.get_bound_dimension();
    MAPBUFFER_REGISTRY.for_each( [&]( const std::string & dim, mapbuffer & mb ) {
        if( has_fire || ( pocket_simulation_level == pocket_sim_level::off && !dim.empty() ) ) {
            return;
        }
        mb.for_each_submap( [&]( std::pair<const tripoint_abs_sm, std::unique_ptr<submap>> &entry ) {
            if( has_fire ) {
                return;
            }
            auto &[pos_sm, sm_ptr] = entry;
            if( ( dim == current_dim && m.contains_abs_sm( pos_sm ) ) ||
                !sm_ptr ||
                !submap_loader.is_in_simulated_set( dim, pos_sm ) ) {
                return;
            }
            if( submap_has_active_fire( *sm_ptr ) ) {
                has_fire = true;
                return;
            }
        } );
    } );
    return has_fire;
}

auto game::can_activity_fixed_window_skip( const time_duration &duration ) -> bool
{
    if( new_game || queue_screenshot || uquit == QUIT_WATCH ) {
        return false;
    }
    if( duration <= 0_turns || !get_weather().weather_id ||
        get_weather().nextweather <= calendar::turn ) {
        return false;
    }
    if( !u.activity || !*u.activity || u.activity->complete() || u.has_destination() ||
        u.is_mounted() ) {
        return false;
    }
    if( u.activity->id() == ACT_AUTODRIVE || !u.activity->rooted() ||
        !u.activity->has_idle_bubble_effect() || u.activity->has_special_turns() ||
        !u.activity->assistants().empty() ) {
        return false;
    }
    if( u.in_vehicle && u.controlling_vehicle ) {
        return false;
    }
    if( m.field_at( u.bub_pos() ).field_count() > 0 ) {
        return false;
    }
    if( has_activity_skip_active_fire() ) {
        return false;
    }
    if( has_activity_skip_relevant_vehicle() ) {
        return false;
    }
    if( const std::optional<time_point> event_time = timed_events.next_event_time();
        event_time && *event_time <= calendar::turn + duration ) {
        return false;
    }
    if( has_activity_skip_blocking_npc_state() ) {
        return false;
    }
    return true;
}
auto game::execute_activity_fixed_window_skip( const time_duration &duration ) -> int
{
    ZoneScopedN( "activity_fixed_window_execute" );
    auto skipped_turns = 0;
    weather_manager &weather = get_weather();
    const auto starting_activity = u.activity->id();
    auto activity_monsters = activity_monmove_cache {};
    npcs_dirty = false;
    const int dur_turns = to_turns<int>( duration );
    /*
     * Activity setup (calc_moves) may have set u.moves to -all_moves.
     * Reset to 0 so the first process_turn() yields positive moves for
     * process_activity() to consume, avoiding a wasted first iteration.
     */
    u.moves = 0;
for( const auto turn_index : std::views::iota( 0, dur_turns ) ) {
        if( is_game_over() || !u.activity || !*u.activity ) {
            break;
        }

        if( !gamemode ) {
            gamemode = std::make_unique<special_game>();
        }
        gamemode->per_turn();
        calendar::turn += 1_turns;
        ++skipped_turns;
        swapping_dimensions = false;
        weather.clear_temp_cache();
        reset_light_level();

        const auto monster_count = critter_tracker->size();
        timed_events.process();
        mission::process_all();
        if( calendar::once_every( 1_days ) ) {
            get_overmapbuffer( current_dimension_id_ ).process_mongroups();
        }
        if( calendar::once_every( time_duration::from_minutes( 2.5 ) ) ) {
            get_overmapbuffer( current_dimension_id_ ).move_hordes();
            if( u.has_trait( trait_HAS_NEMESIS ) ) {
                get_overmapbuffer( current_dimension_id_ ).move_nemesis();
            }
            m.spawn_monsters( false );
        }
        if( get_option<bool>( "AUTOSAVE" ) &&
            calendar::once_every( 1_turns * get_option<int>( "AUTOSAVE_TURNS" ) ) &&
            !u.is_dead_state() ) {
            autosave();
        }
        perhaps_add_random_npc();
        npcs_dirty = false;
        if( npcs_dirty || critter_tracker->size() != monster_count ) {
            activity_fixed_window_force_normal_turn_ = true;
            break;
        }

        debug_hour_timer.print_time();
        u.update_body();
        process_voluntary_act_interrupt();
        {
            ZoneScopedN( "do_turn_player_process_turn" );
            u.process_turn();
        }

        process_activity();

        if( !u.activity || !*u.activity || u.activity->complete() ) {
            break;
        }
        if( is_game_over() ) {
            break;
        }
        if( npcs_dirty || critter_tracker->size() != monster_count ) {
            activity_fixed_window_force_normal_turn_ = true;
            break;
        }
        const auto activity_continues = u.activity && *u.activity &&
                                        u.activity->id() == starting_activity;

        if( m.has_field_at( u.bub_pos() ) ) {
            m.creature_in_field( u );
        }
        for( auto &[dim_id, tracker_ptr] : grid_trackers_ ) {
            if( tracker_ptr ) {
                tracker_ptr->update( calendar::turn );
            }
        }
        tick_portal_links();
        tick_temporary_pocket_dimensions();
        tick_vehicle_portal_taps();
        fluid_grid::update( calendar::turn );

        const auto has_active_npcs = std::ranges::any_of( active_npc,
        []( const shared_ptr_fast<npc> &guy ) {
            return guy && !guy->is_dead();
        } );
        if( critter_tracker->size() > 0 || has_active_npcs ) {
            sounds::process_sounds();
            m.build_map_cache( get_levz(), true );
            if( critter_tracker->size() > 0 ) {
                monmove( monster_activity_ai_mode::activity_skip, &activity_monsters );
                if( critter_tracker->size() != monster_count ) {
                    activity_fixed_window_force_normal_turn_ = true;
                    break;
                }
            }
            if( has_active_npcs ) {
                npcmove();
                npcs_dirty = false;
                if( npcs_dirty || critter_tracker->size() != monster_count ) {
                    activity_fixed_window_force_normal_turn_ = true;
                    break;
                }
            }
        }

        {
            ZoneScopedN( "do_turn_lua_every_x" );
            cata::run_on_every_x_hooks( *DynamicDataLoader::get_instance().lua );
        }
        explosion_handler::get_explosion_queue().execute();
        cleanup_dead();

        if( get_levz() >= 0 && !u.is_underwater() ) {
            handle_weather_effects( weather.weather_id );
        }
        u.update_bodytemp( m, weather );
        character_funcs::update_body_wetness( u, get_weather().get_precise() );
        u.apply_wetness_morale( weather.temperature );
        u.volume = 0;
        npcs_dirty = false;

        if( !activity_continues || u.activity->complete() ) {
            break;
        }
        // A5.2: activity yield cap — send a sync every COOP_ACTIVITY_YIELD_INTERVAL turns so
        // the client can catch up during long activities (sleep, craft, read).
        // Without this the entire 480-turn sleep resolves in one burst and the client
        // sees no world updates until the host wakes up.
        // COOP_ACTIVITY_YIELD_INTERVAL is defined in coop_proto.h (shared with client side).
        if( coop_server_ && coop_server_->is_running() &&
            skipped_turns % COOP_ACTIVITY_YIELD_INTERVAL == 0 ) {
            coop_server_->build_and_send_sync();
        }
    }
    /*
     * After the skip loop, if the activity completed but the type wasn't
     * nullified (actor-based activities call actor->finish() but don't
     * set type to ACT_NULL), clean it up so the activity_ptr appears empty.
     */
    if( u.activity && u.activity->complete() ) {
        u.activity->set_to_null();
    }
    run_activity_skip_batch_turns( skipped_turns );
    return skipped_turns;
}

auto game::run_activity_skip_batch_turns( const int skipped_turns ) -> void
{
    if( skipped_turns <= 0 ) {
        return;
    }

    {
        ZoneScopedN( "activity_fixed_window_batch_submaps" );
        const auto zmin = m.has_zlevels() ? -OVERMAP_DEPTH : m.get_abs_sub().z();
        const auto zmax = m.has_zlevels() ? OVERMAP_HEIGHT : m.get_abs_sub().z();
        const auto axis = std::views::iota( 0, m.getmapsize() );
        for( const auto x : axis ) {
            for( const auto y : axis ) {
                const auto p = point_bub_sm( x, y );
                for( const auto z : std::views::iota( zmin, zmax + 1 ) ) {
                    auto *sm = m.get_submap_at_grid( tripoint_bub_sm( p, z ) );
                    if( sm == nullptr ) {
                        continue;
                    }
                    run_submap_batch_turns( *sm, skipped_turns );
                    sm->last_touched = calendar::turn;
                }
            }
        }

        const auto current_dim = m.get_bound_dimension();
        MAPBUFFER_REGISTRY.for_each( [&]( const std::string & dim, mapbuffer & mb ) {
            if( pocket_simulation_level == pocket_sim_level::off && !dim.empty() ) {
                return;
            }
            mb.for_each_submap( [&]( std::pair<const tripoint_abs_sm, std::unique_ptr<submap>> &entry ) {
                auto &[pos_sm, sm_ptr] = entry;
                if( ( dim == current_dim && m.contains_abs_sm( pos_sm ) ) ||
                    !sm_ptr || !submap_loader.is_in_simulated_set( dim, pos_sm ) ) {
                    return;
                }
                run_submap_batch_turns( *sm_ptr, skipped_turns );
                sm_ptr->last_touched = calendar::turn;
            } );
        } );
    }

    {
        ZoneScopedN( "activity_fixed_window_flush_items" );
        m.process_items();
    }
    explosion_handler::get_explosion_queue().execute();
    cleanup_dead();
    Pathfinding::clear_d_maps();
}

auto game::run_activity_cadence_boundary() -> void
{
    ZoneScopedN( "activity_cadence_boundary" );
    weather_manager &weather = get_weather();
    weather.clear_temp_cache();
    weather.update_weather();
    reset_light_level();
    m.invalidate_lightmap_caches();
    m.invalidate_visibility_caches();
    if( calendar::once_every( activity_time_cadence::fixed_window() ) ) {
        overmap_npc_move();
    }
    Pathfinding::clear_d_maps();
    handle_wait_activity_redraw( true );
}

auto game::try_activity_fixed_window_skip() -> bool
{
    ZoneScopedN( "activity_fixed_window_try" );
    if( activity_fixed_window_force_normal_turn_ ) {
    activity_fixed_window_force_normal_turn_ = false;
    return false;
}
if( !u.activity || !*u.activity || calendar::turn < next_activity_fixed_window_check_ ) {
    return false;
}
const auto duration = activity_fixed_window_duration();
if( !can_activity_fixed_window_skip( duration ) ) {
    next_activity_fixed_window_check_ = calendar::turn + 1_minutes;
    return false;
}
const auto skipped_turns = execute_activity_fixed_window_skip( duration );
if( skipped_turns <= 0 ) {
    next_activity_fixed_window_check_ = calendar::turn + 1_minutes;
    return false;
}
TracyPlot( "Activity Fixed Window Skipped Turns", int64_t{ skipped_turns } );
next_activity_fixed_window_check_ = calendar::turn;
const auto full_window_turns = to_turns<int>( activity_time_cadence::fixed_window() );
if( skipped_turns >= full_window_turns || get_weather().nextweather <= calendar::turn ) {
    run_activity_cadence_boundary();
    }
    return true;
}

auto game::handle_wait_activity_redraw( const bool force ) -> void
{
    const auto player_is_sleeping = u.has_effect( effect_sleep );
    auto wait_redraw = false;
    auto wait_message = std::string {};
    auto wait_refresh_rate = 0_turns;
    if( player_is_sleeping ) {
        wait_redraw = true;
        wait_message = _( "Wait till you wake up…" );
        wait_refresh_rate = 30_minutes;
        if( calendar::once_every( 1_hours ) ) {
            add_artifact_dreams();
        }
    } else if( u.has_destination() ) {
        wait_redraw = true;
        wait_message = _( "Travelling…" );
        wait_refresh_rate = 15_turns;
    } else if( u.activity ) {
        if( const std::optional<std::string> progress = u.activity->get_progress_message( u ) ) {
            wait_redraw = true;
            wait_message = *progress;
            wait_refresh_rate = u.activity->id() == ACT_AUTODRIVE ? 1_turns :
                                activity_time_cadence::activity_render_refresh();
        }
    }
    if( wait_redraw ) {
        ZoneScopedN( "wait_redraw" );
        if( force || first_redraw_since_waiting_started ||
            calendar::once_every( std::min( 1_minutes, wait_refresh_rate ) ) ) {
            if( force || first_redraw_since_waiting_started || calendar::once_every( wait_refresh_rate ) ) {
                ui_manager::redraw();
            }

            ui_adaptor dummy( ui_adaptor::disable_uis_below {} );
            wait_popup = std::make_unique<static_popup>();
            wait_popup->on_top( true ).wait_message( "%s", wait_message );
            ui_manager::redraw();
            refresh_display();
            first_redraw_since_waiting_started = false;
        }
    } else {
        wait_popup.reset();
        first_redraw_since_waiting_started = true;
    }
}
