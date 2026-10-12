#include "turn_plan.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

#include "avatar.h"
#include "calendar.h"
#include "catalua.h"
#include "character.h"
#include "character_turn.h"
#include "distribution_grid.h"
#include "explosion_queue.h"
#include "fluid_grid.h"
#include "game.h"
#include "game_constants.h"
#include "gamemode.h"
#include "init.h"
#include "input.h"
#include "level_cache_freshness.h"
#include "line.h"
#include "map.h"
#include "mission.h"
#include "npc.h"
#include "output.h"
#include "overmapbuffer.h"
#include "pathfinding.h"
#include "profile.h"
#include "scent_map.h"
#include "sounds.h"
#include "submap_load_manager.h"
#include "timed_event.h"
#include "ui_manager.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "weather.h"

namespace
{

const trait_id trait_HAS_NEMESIS( "HAS_NEMESIS" );

/// A gate decides whether a step runs. `runs == nullptr` means the step always runs.
/// `skip_reason` is what a skipped step reports.
struct turn_gate {
    auto( *runs )( const turn_inputs & ) -> bool;
    std::string_view skip_reason;
};

/// One row of the step table: everything the plan knows about a step, before the mode and
/// the gate are applied.
struct turn_spec {
    turn_step_kind kind;
    std::string_view name;
    turn_scope scope;
    turn_perf_group group;
    bool reads_turn;
    turn_gate gate;
};

auto gate_new_game( const turn_inputs &in ) -> bool
{
    return in.new_game;
}

auto gate_not_new_game( const turn_inputs &in ) -> bool
{
    return !in.new_game;
}

auto gate_npcs_dirty( const turn_inputs &in ) -> bool
{
    return in.npcs_dirty;
}

auto gate_vehicle_owned( const turn_inputs &in ) -> bool
{
    return in.controlling_owned_vehicle;
}

auto gate_mounted( const turn_inputs &in ) -> bool
{
    return in.mounted;
}

auto gate_daily( const turn_inputs &in ) -> bool
{
    return in.tick_daily;
}

auto gate_two_and_half_minutes( const turn_inputs &in ) -> bool
{
    return in.tick_two_and_half_minutes;
}

auto gate_autosave( const turn_inputs &in ) -> bool
{
    return in.autosave_option && in.tick_autosave && !in.avatar_dead;
}

auto gate_sound_awake( const turn_inputs &in ) -> bool
{
    return !( in.asleep && in.sleep_skip_sound );
}

auto gate_client_coop( const turn_inputs &in ) -> bool
{
    return in.coop == coop_mode::client;
}

auto gate_driving_offset( const turn_inputs &in ) -> bool
{
    return in.driving_offset;
}

auto gate_scent_writable( const turn_inputs &in ) -> bool
{
    return in.scent_writable;
}

auto gate_vehicles_awake( const turn_inputs &in ) -> bool
{
    return !( in.asleep && !in.driving && in.sleep_skip_veh );
}

auto gate_monsters_awake( const turn_inputs &in ) -> bool
{
    return !( in.asleep && in.sleep_skip_mon );
}

auto gate_npcs_awake( const turn_inputs &in ) -> bool
{
    return !( in.asleep && in.sleep_skip_npc );
}

auto gate_npcs_sleep_skip( const turn_inputs &in ) -> bool
{
    return in.asleep && in.sleep_skip_npc;
}

auto gate_five_minutes( const turn_inputs &in ) -> bool
{
    return in.tick_five_minutes;
}

auto gate_input_idle( const turn_inputs &in ) -> bool
{
    return !in.has_activity && !in.has_destination;
}

auto gate_force_redraw( const turn_inputs &in ) -> bool
{
    return in.force_redraw;
}

auto gate_weather_effects( const turn_inputs &in ) -> bool
{
    return in.weather_effects;
}

auto gate_not_deaf( const turn_inputs &in ) -> bool
{
    return !in.deaf;
}

constexpr auto kGateNewGame = turn_gate{ .runs = gate_new_game, .skip_reason = "not a new game" };
constexpr auto kGateNotNewGame = turn_gate{ .runs = gate_not_new_game,
                                                .skip_reason = "new game holds the calendar" };
constexpr auto kGateNpcsDirty = turn_gate{ .runs = gate_npcs_dirty, .skip_reason = "npcs clean" };
constexpr auto kGateVehicleOwned = turn_gate{ .runs = gate_vehicle_owned,
                                                  .skip_reason = "not controlling an owned vehicle" };
constexpr auto kGateMounted = turn_gate{ .runs = gate_mounted, .skip_reason = "not mounted" };
constexpr auto kGateDaily = turn_gate{ .runs = gate_daily, .skip_reason = "not a daily tick" };
constexpr auto kGateTwoAndHalfMinutes = turn_gate{ .runs = gate_two_and_half_minutes,
                                                       .skip_reason = "not a 2.5 minute tick" };
constexpr auto kGateAutosave = turn_gate{ .runs = gate_autosave,
                                              .skip_reason = "autosave off, not due, or dead" };
constexpr auto kGateSoundAwake = turn_gate{ .runs = gate_sound_awake,
                                                .skip_reason = "sleep-skip sound" };
constexpr auto kGateClientCoop = turn_gate{ .runs = gate_client_coop,
                                                .skip_reason = "host owns sounds" };
constexpr auto kGateDrivingOffset = turn_gate{ .runs = gate_driving_offset,
                                                   .skip_reason = "no driving offset" };
constexpr auto kGateScentWritable = turn_gate{ .runs = gate_scent_writable,
                                                   .skip_reason = "scent writes disabled" };
constexpr auto kGateVehiclesAwake = turn_gate{ .runs = gate_vehicles_awake,
                                                   .skip_reason = "sleep-skip vehicles" };
constexpr auto kGateMonstersAwake = turn_gate{ .runs = gate_monsters_awake,
                                                   .skip_reason = "sleep-skip monsters" };
constexpr auto kGateNpcsAwake = turn_gate{ .runs = gate_npcs_awake, .skip_reason = "sleep-skip npcs" };
constexpr auto kGateNpcsSleepSkip = turn_gate{ .runs = gate_npcs_sleep_skip,
                                                   .skip_reason = "npcs run full move" };
constexpr auto kGateFiveMinutes = turn_gate{ .runs = gate_five_minutes,
                                                 .skip_reason = "not a 5 minute tick" };
constexpr auto kGateInputIdle = turn_gate{ .runs = gate_input_idle,
                                               .skip_reason = "activity or destination pending" };
constexpr auto kGateForceRedraw = turn_gate{ .runs = gate_force_redraw,
                                                 .skip_reason = "no forced redraw" };
constexpr auto kGateWeatherEffects = turn_gate{ .runs = gate_weather_effects,
                                                    .skip_reason = "no weather effects" };
constexpr auto kGateNotDeaf = turn_gate{ .runs = gate_not_deaf, .skip_reason = "deaf" };

/// The step table, in canonical order. Frame steps come last, so the client frame plan is
/// the tail of the table.
constexpr auto kTurnSpecs = std::array<turn_spec, 69> {
    turn_spec{
        .kind = turn_step_kind::new_game_start, .name = "new_game_start",
        .scope = turn_scope::world, .group = turn_perf_group::none,
        .reads_turn = false, .gate = kGateNewGame },
    turn_spec{
        .kind = turn_step_kind::calendar_advance, .name = "calendar_advance",
        .scope = turn_scope::world, .group = turn_perf_group::none,
        .reads_turn = false, .gate = kGateNotNewGame },
    turn_spec{
        .kind = turn_step_kind::swap_reset, .name = "swap_reset",
        .scope = turn_scope::world, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::light_dirty, .name = "light_dirty",
        .scope = turn_scope::world, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::temp_cache_clear, .name = "temp_cache_clear",
        .scope = turn_scope::avatar, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::npc_reload, .name = "npc_reload",
        .scope = turn_scope::world, .group = turn_perf_group::none,
        .reads_turn = false, .gate = kGateNpcsDirty },
    turn_spec{
        .kind = turn_step_kind::timed_events, .name = "timed_events",
        .scope = turn_scope::avatar, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::missions, .name = "missions",
        .scope = turn_scope::avatar, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::vehicle_theft, .name = "vehicle_theft",
        .scope = turn_scope::avatar, .group = turn_perf_group::none,
        .reads_turn = false, .gate = kGateVehicleOwned },
    turn_spec{
        .kind = turn_step_kind::mount_spook, .name = "mount_spook",
        .scope = turn_scope::avatar, .group = turn_perf_group::none,
        .reads_turn = false, .gate = kGateMounted },
    turn_spec{
        .kind = turn_step_kind::daily_mongroups, .name = "daily_mongroups",
        .scope = turn_scope::world, .group = turn_perf_group::none,
        .reads_turn = true, .gate = kGateDaily },
    turn_spec{
        .kind = turn_step_kind::hordes_move, .name = "hordes_move",
        .scope = turn_scope::world, .group = turn_perf_group::none,
        .reads_turn = true, .gate = kGateTwoAndHalfMinutes },
    turn_spec{
        .kind = turn_step_kind::debug_hour_timer, .name = "debug_hour_timer",
        .scope = turn_scope::world, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::update_body, .name = "update_body",
        .scope = turn_scope::avatar, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::autosave, .name = "autosave",
        .scope = turn_scope::avatar, .group = turn_perf_group::none,
        .reads_turn = true, .gate = kGateAutosave },
    turn_spec{
        .kind = turn_step_kind::weather_update, .name = "weather_update",
        .scope = turn_scope::avatar, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::random_npc, .name = "random_npc",
        .scope = turn_scope::world, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::voluntary_interrupt, .name = "voluntary_interrupt",
        .scope = turn_scope::avatar, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::process_activity, .name = "process_activity",
        .scope = turn_scope::avatar, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::performance_bubble, .name = "performance_bubble",
        .scope = turn_scope::world, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::sound_reset, .name = "sound_reset",
        .scope = turn_scope::avatar, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::npc_sound_markers, .name = "npc_sound_markers",
        .scope = turn_scope::world, .group = turn_perf_group::none,
        .reads_turn = false, .gate = kGateSoundAwake },
    turn_spec{
        .kind = turn_step_kind::avatar_sound_markers, .name = "avatar_sound_markers",
        .scope = turn_scope::avatar, .group = turn_perf_group::none,
        .reads_turn = false, .gate = kGateSoundAwake },
    turn_spec{
        .kind = turn_step_kind::clear_recent_sounds, .name = "clear_recent_sounds",
        .scope = turn_scope::avatar, .group = turn_perf_group::none,
        .reads_turn = false, .gate = kGateClientCoop },
    turn_spec{
        .kind = turn_step_kind::driving_offset, .name = "driving_offset",
        .scope = turn_scope::world, .group = turn_perf_group::none,
        .reads_turn = false, .gate = kGateDrivingOffset },
    turn_spec{
        .kind = turn_step_kind::scent_write, .name = "scent_write",
        .scope = turn_scope::avatar, .group = turn_perf_group::world_tick,
        .reads_turn = false, .gate = kGateScentWritable },
    turn_spec{
        .kind = turn_step_kind::scent_update, .name = "scent_update",
        .scope = turn_scope::avatar, .group = turn_perf_group::world_tick,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::build_floor_caches, .name = "build_floor_caches",
        .scope = turn_scope::world, .group = turn_perf_group::world_tick,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::process_falling, .name = "process_falling",
        .scope = turn_scope::world, .group = turn_perf_group::world_tick,
        .reads_turn = false, .gate = kGateVehiclesAwake },
    turn_spec{
        .kind = turn_step_kind::autopilot_vehicles, .name = "autopilot_vehicles",
        .scope = turn_scope::world, .group = turn_perf_group::world_tick,
        .reads_turn = false, .gate = kGateVehiclesAwake },
    turn_spec{
        .kind = turn_step_kind::vehmove, .name = "vehmove",
        .scope = turn_scope::world, .group = turn_perf_group::world_tick,
        .reads_turn = false, .gate = kGateVehiclesAwake },
    turn_spec{
        .kind = turn_step_kind::process_items, .name = "process_items",
        .scope = turn_scope::world, .group = turn_perf_group::world_tick,
        .reads_turn = true, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::creature_in_field, .name = "creature_in_field",
        .scope = turn_scope::avatar, .group = turn_perf_group::world_tick,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::grid_tracker_update, .name = "grid_tracker_update",
        .scope = turn_scope::world, .group = turn_perf_group::world_tick,
        .reads_turn = true, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::portal_links, .name = "portal_links",
        .scope = turn_scope::world, .group = turn_perf_group::world_tick,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::fluid_grid, .name = "fluid_grid",
        .scope = turn_scope::world, .group = turn_perf_group::world_tick,
        .reads_turn = true, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::process_sounds, .name = "process_sounds",
        .scope = turn_scope::world, .group = turn_perf_group::world_tick,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::build_map_cache, .name = "build_map_cache",
        .scope = turn_scope::world, .group = turn_perf_group::build_map_cache,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::monmove, .name = "monmove",
        .scope = turn_scope::world, .group = turn_perf_group::monmove,
        .reads_turn = false, .gate = kGateMonstersAwake },
    turn_spec{
        .kind = turn_step_kind::npcmove, .name = "npcmove",
        .scope = turn_scope::world, .group = turn_perf_group::none,
        .reads_turn = false, .gate = kGateNpcsAwake },
    turn_spec{
        .kind = turn_step_kind::sleep_skip_npc_process, .name = "sleep_skip_npc_process",
        .scope = turn_scope::world, .group = turn_perf_group::none,
        .reads_turn = false, .gate = kGateNpcsSleepSkip },
    turn_spec{
        .kind = turn_step_kind::overmap_npc_move, .name = "overmap_npc_move",
        .scope = turn_scope::world, .group = turn_perf_group::none,
        .reads_turn = true, .gate = kGateFiveMinutes },
    turn_spec{
        .kind = turn_step_kind::update_stair_monsters, .name = "update_stair_monsters",
        .scope = turn_scope::world, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::mon_info_update, .name = "mon_info_update",
        .scope = turn_scope::world, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::player_process_turn, .name = "player_process_turn",
        .scope = turn_scope::avatar, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::lua_every_x, .name = "lua_every_x",
        .scope = turn_scope::avatar, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::explosion_queue, .name = "explosion_queue",
        .scope = turn_scope::avatar, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::cleanup_dead, .name = "cleanup_dead",
        .scope = turn_scope::avatar, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::force_redraw, .name = "force_redraw",
        .scope = turn_scope::world, .group = turn_perf_group::none,
        .reads_turn = false, .gate = kGateForceRedraw },
    turn_spec{
        .kind = turn_step_kind::weather_effects, .name = "weather_effects",
        .scope = turn_scope::avatar, .group = turn_perf_group::none,
        .reads_turn = false, .gate = kGateWeatherEffects },
    turn_spec{
        .kind = turn_step_kind::wait_activity_redraw, .name = "wait_activity_redraw",
        .scope = turn_scope::world, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::bodytemp_wetness_morale, .name = "bodytemp_wetness_morale",
        .scope = turn_scope::avatar, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::remove_hearing_loss, .name = "remove_hearing_loss",
        .scope = turn_scope::world, .group = turn_perf_group::none,
        .reads_turn = false, .gate = kGateNotDeaf },
    turn_spec{
        .kind = turn_step_kind::sfx_world, .name = "sfx_world",
        .scope = turn_scope::world, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::reset_noise, .name = "reset_noise",
        .scope = turn_scope::avatar, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::world_tick, .name = "world_tick",
        .scope = turn_scope::world, .group = turn_perf_group::world_tick,
        .reads_turn = true, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::submap_load, .name = "submap_load",
        .scope = turn_scope::world, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::prune_grid_trackers, .name = "prune_grid_trackers",
        .scope = turn_scope::world, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::clear_d_maps, .name = "clear_d_maps",
        .scope = turn_scope::world, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::input_drain, .name = "input_drain",
        .scope = turn_scope::world, .group = turn_perf_group::none,
        .reads_turn = false, .gate = kGateInputIdle },
    turn_spec{
        .kind = turn_step_kind::frame_light_dirty, .name = "frame_light_dirty",
        .scope = turn_scope::frame, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::frame_performance_bubble, .name = "frame_performance_bubble",
        .scope = turn_scope::frame, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::frame_driving_offset, .name = "frame_driving_offset",
        .scope = turn_scope::frame, .group = turn_perf_group::none,
        .reads_turn = false, .gate = kGateDrivingOffset },
    turn_spec{
        .kind = turn_step_kind::frame_build_floor_caches, .name = "frame_build_floor_caches",
        .scope = turn_scope::frame, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::frame_build_map_cache, .name = "frame_build_map_cache",
        .scope = turn_scope::frame, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::frame_mon_info_update, .name = "frame_mon_info_update",
        .scope = turn_scope::frame, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::frame_wait_activity_redraw, .name = "frame_wait_activity_redraw",
        .scope = turn_scope::frame, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
    turn_spec{
        .kind = turn_step_kind::frame_remove_hearing_loss, .name = "frame_remove_hearing_loss",
        .scope = turn_scope::frame, .group = turn_perf_group::none,
        .reads_turn = false, .gate = kGateNotDeaf },
    turn_spec{
        .kind = turn_step_kind::frame_sfx, .name = "frame_sfx",
        .scope = turn_scope::frame, .group = turn_perf_group::none,
        .reads_turn = false, .gate = {} },
};

static_assert( kTurnSpecs.size() <= kMaxTurnSteps );
static_assert( kTurnSpecs.back().kind == turn_step_kind::frame_sfx );

/// Whether a step of `scope` belongs in the plan for `mode`.
auto in_mode( turn_scope scope, turn_mode mode ) -> bool
{
    switch( mode ) {
        case turn_mode::solo:
        case turn_mode::host:
            return scope != turn_scope::frame;
        case turn_mode::client_turn:
            return scope == turn_scope::avatar;
        case turn_mode::client_frame:
            return scope == turn_scope::frame;
    }
    return false;
}

} // namespace

/// Holds the body of every step kind. A friend of `game`, so the bodies reach its members.
/// The switch has no default, so adding a kind without a body is a compile error.
struct turn_step_bodies {
    static auto run( game &gm, turn_step_kind kind ) -> void;
};

auto turn_step_bodies::run( game &gm, turn_step_kind kind ) -> void
{
    switch( kind ) {
        case turn_step_kind::new_game_start:
            gm.new_game = false;
            break;
        case turn_step_kind::calendar_advance:
            if( !gm.gamemode ) {
                gm.gamemode = std::make_unique<special_game>();
            }
            gm.gamemode->per_turn();
            calendar::turn += 1_turns;
            break;
        case turn_step_kind::swap_reset:
            gm.swapping_dimensions = false;
            break;
        case turn_step_kind::light_dirty:
        case turn_step_kind::frame_light_dirty:
            level_cache_freshness::report( get_map(),
            level_cache_freshness::light_changed{
                .scope = level_cache_freshness::light_changed::lightmap_scope::none,
                .visibility = true,
            } );
            break;
        case turn_step_kind::temp_cache_clear:
            get_weather().clear_temp_cache();
            break;
        case turn_step_kind::npc_reload:
            gm.load_npcs();
            break;
        case turn_step_kind::timed_events:
            gm.timed_events.process();
            break;
        case turn_step_kind::missions:
            mission::process_all();
            break;
        case turn_step_kind::vehicle_theft: {
            auto &m = get_map();
            vehicle *veh = veh_pointer_or_null( m.veh_at( gm.u.bub_pos() ) );
            if( veh && !veh->handle_potential_theft( gm.u, true ) ) {
                veh->handle_potential_theft( gm.u, false, false );
            }
            break;
        }
        case turn_step_kind::mount_spook:
            gm.u.check_mount_is_spooked();
            break;
        case turn_step_kind::daily_mongroups:
            get_overmapbuffer( gm.current_dimension_id_ ).process_mongroups();
            break;
        case turn_step_kind::hordes_move: {
            auto &m = get_map();
            auto &om = get_overmapbuffer( gm.current_dimension_id_ );
            om.move_hordes();
            if( gm.u.has_trait( trait_HAS_NEMESIS ) ) {
                om.move_nemesis();
            }
            m.spawn_monsters( false );
            break;
        }
        case turn_step_kind::debug_hour_timer:
            gm.debug_hour_timer.print_time();
            break;
        case turn_step_kind::update_body:
            gm.u.update_body();
            break;
        case turn_step_kind::autosave:
            gm.autosave();
            break;
        case turn_step_kind::weather_update:
            get_weather().update_weather();
            gm.reset_light_level();
            break;
        case turn_step_kind::random_npc:
            gm.perhaps_add_random_npc();
            break;
        case turn_step_kind::voluntary_interrupt:
            gm.process_voluntary_act_interrupt();
            break;
        case turn_step_kind::process_activity:
            gm.process_activity();
            break;
        case turn_step_kind::performance_bubble:
        case turn_step_kind::frame_performance_bubble:
            gm.update_performance_bubble();
            break;
        case turn_step_kind::sound_reset:
            sounds::reset_markers();
            break;
        case turn_step_kind::npc_sound_markers:
            for( npc &guy : gm.all_npcs() ) {
                if( rl_dist( guy.bub_pos(), gm.u.bub_pos() ) < g_max_view_distance ) {
                    sounds::process_sound_markers( &guy );
                }
            }
            break;
        case turn_step_kind::avatar_sound_markers:
            sounds::process_sound_markers( &gm.u );
            if( gm.u.is_deaf() ) {
                sfx::do_hearing_loss();
            }
            break;
        case turn_step_kind::clear_recent_sounds:
            sounds::clear_recent_sounds();
            break;
        case turn_step_kind::driving_offset:
        case turn_step_kind::frame_driving_offset: {
            auto &m = get_map();
            vehicle *veh = veh_pointer_or_null( m.veh_at( gm.u.bub_pos() ) );
            gm.calc_driving_offset( veh );
            break;
        }
        case turn_step_kind::scent_write:
            gm.scent.set( gm.u.bub_pos(), gm.u.scent, gm.u.get_type_of_scent() );
            get_overmapbuffer( gm.current_dimension_id_ ).set_scent( gm.u.abs_omt_pos(), gm.u.scent );
            break;
        case turn_step_kind::scent_update:
            gm.scent.update( gm.u.bub_pos(), get_map() );
            break;
        case turn_step_kind::build_floor_caches:
        case turn_step_kind::frame_build_floor_caches:
            get_map().build_floor_caches();
            break;
        case turn_step_kind::process_falling:
            get_map().process_falling();
            break;
        case turn_step_kind::autopilot_vehicles:
            gm.autopilot_vehicles();
            break;
        case turn_step_kind::vehmove:
            get_map().vehmove();
            break;
        case turn_step_kind::process_items: {
            ZoneScopedN( "do_turn_process_items" );
            get_map().process_items();
            break;
        }
        case turn_step_kind::creature_in_field:
            get_map().creature_in_field( gm.u );
            break;
        case turn_step_kind::grid_tracker_update:
            for( auto &entry : gm.grid_trackers_ ) {
                if( entry.second ) {
                    entry.second->update( calendar::turn );
                }
            }
            break;
        case turn_step_kind::portal_links:
            gm.tick_portal_links();
            gm.tick_temporary_pocket_dimensions();
            gm.tick_vehicle_portal_taps();
            break;
        case turn_step_kind::fluid_grid:
            fluid_grid::update( calendar::turn );
            break;
        case turn_step_kind::process_sounds:
            sounds::process_sounds();
            break;
        case turn_step_kind::build_map_cache:
        case turn_step_kind::frame_build_map_cache: {
            auto &m = get_map();
            m.build_map_cache( level_cache_freshness::plan_for( m,
                               level_cache_freshness::pose_of_viewer( gm.u, gm.get_levz() ),
                               level_cache_freshness::lightmap_policy::skip ) );
            break;
        }
        case turn_step_kind::monmove:
            gm.monmove();
            break;
        case turn_step_kind::npcmove:
            gm.npcmove();
            break;
        case turn_step_kind::sleep_skip_npc_process:
            gm.sleep_skip_npc_process();
            break;
        case turn_step_kind::overmap_npc_move:
            gm.overmap_npc_move();
            break;
        case turn_step_kind::update_stair_monsters:
            gm.update_stair_monsters();
            break;
        case turn_step_kind::mon_info_update:
        case turn_step_kind::frame_mon_info_update:
            gm.mon_info_update();
            break;
        case turn_step_kind::player_process_turn: {
            ZoneScopedN( "do_turn_player_process_turn" );
            gm.u.process_turn();
            break;
        }
        case turn_step_kind::lua_every_x: {
            ZoneScopedN( "do_turn_lua_every_x" );
            cata::run_on_every_x_hooks( *DynamicDataLoader::get_instance().lua );
            break;
        }
        case turn_step_kind::explosion_queue:
            explosion_handler::get_explosion_queue().execute();
            break;
        case turn_step_kind::cleanup_dead:
            gm.cleanup_dead();
            break;
        case turn_step_kind::force_redraw:
            ui_manager::redraw();
            refresh_display();
            break;
        case turn_step_kind::weather_effects:
            handle_weather_effects( get_weather().weather_id );
            break;
        case turn_step_kind::wait_activity_redraw:
        case turn_step_kind::frame_wait_activity_redraw:
            gm.handle_wait_activity_redraw();
            break;
        case turn_step_kind::bodytemp_wetness_morale: {
            auto &m = get_map();
            weather_manager &weather = get_weather();
            gm.u.update_bodytemp( m, weather );
            character_funcs::update_body_wetness( gm.u, weather.get_precise() );
            gm.u.apply_wetness_morale( weather.temperature );
            break;
        }
        case turn_step_kind::remove_hearing_loss:
        case turn_step_kind::frame_remove_hearing_loss:
            sfx::remove_hearing_loss();
            break;
        case turn_step_kind::sfx_world:
        case turn_step_kind::frame_sfx:
            sfx::do_danger_music();
            sfx::do_vehicle_engine_sfx();
            sfx::do_vehicle_exterior_engine_sfx();
            sfx::do_fatigue();
            break;
        case turn_step_kind::reset_noise:
            gm.u.volume = 0;
            break;
        case turn_step_kind::world_tick:
            gm.world_tick();
            break;
        case turn_step_kind::submap_load:
            for( const auto &dim_id : submap_loader.active_dimensions() ) {
                ensure_distribution_grid_tracker_for( dim_id );
            }
            submap_loader.update_lazy_border_focus( gm.current_dimension_id_, gm.u.abs_pos() );
            submap_loader.update();
            break;
        case turn_step_kind::prune_grid_trackers:
            for( auto it = gm.grid_trackers_.begin(); it != gm.grid_trackers_.end(); ) {
                if( !it->first.is_empty() && !it->second->has_tracked_submaps() ) {
                    submap_loader.remove_listener( it->second.get() );
                    it = gm.grid_trackers_.erase( it );
                } else {
                    ++it;
                }
            }
            break;
        case turn_step_kind::clear_d_maps:
            Pathfinding::clear_d_maps();
            break;
        case turn_step_kind::input_drain:
            inp_mngr.pump_events();
            break;
    }
}

auto make_turn_plan( const turn_inputs &inputs, turn_mode mode ) -> turn_plan
{
    auto plan = turn_plan{};
    for( const turn_spec &spec : kTurnSpecs ) {
        if( !in_mode( spec.scope, mode ) ) {
            continue;
        }
        const bool runs = spec.gate.runs == nullptr || spec.gate.runs( inputs );
        plan.push_back( turn_step{
            .name = spec.name,
            .kind = spec.kind,
            .scope = spec.scope,
            .group = spec.group,
            .reads_turn = spec.reads_turn,
            .status = runs ? turn_step_status::run : turn_step_status::skipped,
.reason = runs ? std::string_view{} : spec.gate.skip_reason,
        } );
    }
    return plan;
}

auto run_turn_plan( game &gm, const turn_plan &plan ) -> turn_report
{
    auto report = turn_report{};
    for( const turn_step &step : plan.items() ) {
        if( step.status == turn_step_status::skipped ) {
            report.push_back( turn_step_result{
                .name = step.name,
                .status = step.status,
                .reason = step.reason,
                .group = step.group,
                .duration_ms = 0.0,
            } );
            continue;
        }
        ZoneTransientN( step_zone, step.name.data(), true );
        const auto start = std::chrono::steady_clock::now();
        turn_step_bodies::run( gm, step.kind );
        const auto elapsed = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - start );
        report.push_back( turn_step_result{
            .name = step.name,
            .status = step.status,
            .reason = step.reason,
            .group = step.group,
            .duration_ms = elapsed.count(),
        } );
    }
    return report;
}

auto add_turn_to_perf_window( sim_perf_window &window, const turn_report &report ) -> bool
{
    for( const turn_step_result &result : report.items() ) {
        if( result.status != turn_step_status::run ) {
            continue;
        }
        window.sim_total_ms += result.duration_ms;
        switch( result.group ) {
            case turn_perf_group::none:
                break;
            case turn_perf_group::build_map_cache:
                window.build_map_cache_ms += result.duration_ms;
                break;
            case turn_perf_group::monmove:
                window.monmove_ms += result.duration_ms;
                break;
            case turn_perf_group::world_tick:
                window.world_tick_ms += result.duration_ms;
                break;
        }
    }
    ++window.turns;
    return window.turns >= kSimPerfWindowTurns;
}

auto format_sim_perf_line( const sim_perf_window &window ) -> std::string
{
    const auto turns = static_cast<double>( window.turns );
    auto out = std::ostringstream{};
    out << "[sim][perf] " << window.turns << " turns avg: sim_total=" << window.sim_total_ms / turns
        << "ms (build_map_cache=" << window.build_map_cache_ms / turns
        << " monmove=" << window.monmove_ms / turns
        << " world_tick=" << window.world_tick_ms / turns << ")";
    return out.str();
}
