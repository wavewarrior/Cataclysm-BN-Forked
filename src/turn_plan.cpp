#include "turn_plan.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_arena.h"
#include "catalua.h"
#include "character_functions.h"
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
#include "mutation.h"
#include "npc.h"
#include "output.h"
#include "overmapbuffer_registry.h"
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

/// Static description of one step: identity, scope, timing bucket and turn/calendar flags.
struct registry_entry {
    turn_step_kind kind;
    std::string_view name;
    turn_scope scope;
    perf_group group = perf_group::none;
    bool reads_turn = false;
    bool advances_calendar = false;
};

/// Every step in plan order. The index of an entry equals its `kind`, asserted below.
constexpr registry_entry kRegistry[] = {
    { .kind = turn_step_kind::cleanup_arenas, .name = "cleanup_arenas", .scope = turn_scope::avatar },
    { .kind = turn_step_kind::advance_calendar, .name = "advance_calendar", .scope = turn_scope::world, .advances_calendar = true },
    { .kind = turn_step_kind::mark_visibility_dirty, .name = "mark_visibility_dirty", .scope = turn_scope::world },
    { .kind = turn_step_kind::clear_weather_temp_cache, .name = "clear_weather_temp_cache", .scope = turn_scope::avatar },
    { .kind = turn_step_kind::load_npcs, .name = "load_npcs", .scope = turn_scope::world },
    { .kind = turn_step_kind::timed_events_and_missions, .name = "timed_events_and_missions", .scope = turn_scope::avatar, .reads_turn = true },
    { .kind = turn_step_kind::vehicle_theft, .name = "vehicle_theft", .scope = turn_scope::avatar },
    { .kind = turn_step_kind::mount_spook, .name = "mount_spook", .scope = turn_scope::avatar },
    { .kind = turn_step_kind::process_mongroups, .name = "process_mongroups", .scope = turn_scope::world, .reads_turn = true },
    { .kind = turn_step_kind::move_hordes, .name = "move_hordes", .scope = turn_scope::world, .reads_turn = true },
    { .kind = turn_step_kind::debug_hour_timer, .name = "debug_hour_timer", .scope = turn_scope::world },
    { .kind = turn_step_kind::update_body, .name = "update_body", .scope = turn_scope::avatar },
    { .kind = turn_step_kind::autosave, .name = "autosave", .scope = turn_scope::avatar, .reads_turn = true },
    { .kind = turn_step_kind::update_weather, .name = "update_weather", .scope = turn_scope::avatar, .reads_turn = true },
    { .kind = turn_step_kind::perhaps_add_random_npc, .name = "perhaps_add_random_npc", .scope = turn_scope::world },
    { .kind = turn_step_kind::voluntary_interrupt, .name = "voluntary_interrupt", .scope = turn_scope::avatar },
    { .kind = turn_step_kind::process_activity, .name = "process_activity", .scope = turn_scope::avatar },
    { .kind = turn_step_kind::performance_bubble, .name = "performance_bubble", .scope = turn_scope::world },
    { .kind = turn_step_kind::reset_sound_markers, .name = "reset_sound_markers", .scope = turn_scope::avatar },
    { .kind = turn_step_kind::npc_sound_markers, .name = "npc_sound_markers", .scope = turn_scope::world },
    { .kind = turn_step_kind::avatar_sound_markers, .name = "avatar_sound_markers", .scope = turn_scope::avatar },
    { .kind = turn_step_kind::hearing_loss, .name = "hearing_loss", .scope = turn_scope::avatar },
    { .kind = turn_step_kind::clear_recent_sounds, .name = "clear_recent_sounds", .scope = turn_scope::avatar },
    { .kind = turn_step_kind::driving_offset, .name = "driving_offset", .scope = turn_scope::world },
    { .kind = turn_step_kind::scent_set, .name = "scent_set", .scope = turn_scope::avatar, .group = perf_group::world_tick },
    { .kind = turn_step_kind::scent_update, .name = "scent_update", .scope = turn_scope::avatar, .group = perf_group::world_tick },
    { .kind = turn_step_kind::creature_in_field, .name = "creature_in_field", .scope = turn_scope::avatar, .group = perf_group::world_tick },
    { .kind = turn_step_kind::build_floor_caches, .name = "build_floor_caches", .scope = turn_scope::world, .group = perf_group::world_tick },
    { .kind = turn_step_kind::vehicle_moves, .name = "vehicle_moves", .scope = turn_scope::world, .group = perf_group::world_tick },
    { .kind = turn_step_kind::process_items, .name = "process_items", .scope = turn_scope::world, .group = perf_group::world_tick },
    { .kind = turn_step_kind::grid_trackers, .name = "grid_trackers", .scope = turn_scope::world, .group = perf_group::world_tick, .reads_turn = true },
    { .kind = turn_step_kind::tick_portals, .name = "tick_portals", .scope = turn_scope::world, .group = perf_group::world_tick },
    { .kind = turn_step_kind::fluid_grid, .name = "fluid_grid", .scope = turn_scope::world, .group = perf_group::world_tick, .reads_turn = true },
    { .kind = turn_step_kind::process_sounds, .name = "process_sounds", .scope = turn_scope::world, .group = perf_group::world_tick },
    { .kind = turn_step_kind::build_map_cache, .name = "build_map_cache", .scope = turn_scope::world, .group = perf_group::build_map_cache },
    { .kind = turn_step_kind::monmove, .name = "monmove", .scope = turn_scope::world, .group = perf_group::monmove },
    { .kind = turn_step_kind::npc_move, .name = "npc_move", .scope = turn_scope::world, .group = perf_group::sim },
    { .kind = turn_step_kind::sleep_skip_npc, .name = "sleep_skip_npc", .scope = turn_scope::world, .group = perf_group::sim },
    { .kind = turn_step_kind::overmap_npc_move, .name = "overmap_npc_move", .scope = turn_scope::world, .group = perf_group::sim, .reads_turn = true },
    { .kind = turn_step_kind::update_stair_monsters, .name = "update_stair_monsters", .scope = turn_scope::world, .group = perf_group::sim },
    { .kind = turn_step_kind::mon_info_update, .name = "mon_info_update", .scope = turn_scope::world, .group = perf_group::sim },
    { .kind = turn_step_kind::process_turn, .name = "process_turn", .scope = turn_scope::avatar, .group = perf_group::sim },
    { .kind = turn_step_kind::lua_every_x, .name = "lua_every_x", .scope = turn_scope::avatar, .group = perf_group::sim },
    { .kind = turn_step_kind::explosions, .name = "explosions", .scope = turn_scope::avatar, .group = perf_group::sim },
    { .kind = turn_step_kind::cleanup_dead, .name = "cleanup_dead", .scope = turn_scope::avatar, .group = perf_group::sim },
    { .kind = turn_step_kind::forced_redraw, .name = "forced_redraw", .scope = turn_scope::world, .group = perf_group::sim },
    { .kind = turn_step_kind::weather_effects, .name = "weather_effects", .scope = turn_scope::avatar, .group = perf_group::sim },
    { .kind = turn_step_kind::wait_redraw, .name = "wait_redraw", .scope = turn_scope::world, .group = perf_group::sim },
    { .kind = turn_step_kind::update_bodytemp, .name = "update_bodytemp", .scope = turn_scope::avatar, .group = perf_group::sim },
    { .kind = turn_step_kind::reset_volume, .name = "reset_volume", .scope = turn_scope::avatar, .group = perf_group::sim },
    { .kind = turn_step_kind::world_tick, .name = "world_tick", .scope = turn_scope::world, .group = perf_group::world_tick },
    { .kind = turn_step_kind::submap_loader, .name = "submap_loader", .scope = turn_scope::world, .group = perf_group::sim },
    { .kind = turn_step_kind::clear_d_maps, .name = "clear_d_maps", .scope = turn_scope::world, .group = perf_group::sim },
    { .kind = turn_step_kind::pump_events, .name = "pump_events", .scope = turn_scope::world, .group = perf_group::sim },
    { .kind = turn_step_kind::frame_caches, .name = "frame_caches", .scope = turn_scope::frame },
    { .kind = turn_step_kind::frame_driving_offset, .name = "frame_driving_offset", .scope = turn_scope::frame },
    { .kind = turn_step_kind::frame_map_caches, .name = "frame_map_caches", .scope = turn_scope::frame },
    { .kind = turn_step_kind::frame_mon_info, .name = "frame_mon_info", .scope = turn_scope::frame },
    { .kind = turn_step_kind::frame_wait_redraw, .name = "frame_wait_redraw", .scope = turn_scope::frame },
    { .kind = turn_step_kind::frame_hearing_loss, .name = "frame_hearing_loss", .scope = turn_scope::frame },
    { .kind = turn_step_kind::frame_audio, .name = "frame_audio", .scope = turn_scope::frame },
};

static_assert( std::size( kRegistry ) == static_cast<std::size_t>( turn_step_kind::frame_audio ) +
               1 );
static_assert( std::ranges::all_of( std::views::iota( std::size_t{ 0 }, std::size( kRegistry ) ),
[]( const std::size_t i ) { return static_cast<std::size_t>( kRegistry[i].kind ) == i; } ) );

/// The reason a step is skipped, or empty when its gate is open.
auto gate( const bool open, const std::string_view reason ) -> std::string_view
{
    return open ? std::string_view{} :
           reason;
}

/// The reason this step is skipped this turn, or empty when it runs.
auto skip_reason( const registry_entry &entry, const turn_inputs &in ) -> std::string_view
{
    if( in.coop == coop_role::client && entry.scope == turn_scope::world ) {
        return "client: world state arrives via sync";
    }
    switch( entry.kind ) {
        case turn_step_kind::clear_recent_sounds:
            return gate( in.coop == coop_role::client, "host: process_sounds owns recent sounds" );
        case turn_step_kind::load_npcs:
            return gate( in.npcs_dirty, "npcs unchanged" );
        case turn_step_kind::process_mongroups:
            return gate( in.calendar.daily, "daily boundary not due" );
        case turn_step_kind::move_hordes:
            return gate( in.calendar.horde_move, "horde boundary not due" );
        case turn_step_kind::overmap_npc_move:
            return gate( in.calendar.npc_overmap_move, "npc overmap boundary not due" );
        case turn_step_kind::autosave:
            return gate( in.calendar.autosave, "autosave not due" );
        case turn_step_kind::vehicle_moves:
            return gate( !in.sleep.vehicles, "asleep: vehicle moves skipped" );
        case turn_step_kind::monmove:
            return gate( !in.sleep.monsters, "asleep: monster moves skipped" );
        case turn_step_kind::npc_move:
            return gate( !in.sleep.npcs, "asleep: npc moves skipped" );
        case turn_step_kind::sleep_skip_npc:
            return gate( in.sleep.npcs, "awake: npcs run full moves" );
        case turn_step_kind::npc_sound_markers:
        case turn_step_kind::avatar_sound_markers:
        case turn_step_kind::hearing_loss:
            return gate( !in.sleep.sound, "asleep: sound skipped" );
        case turn_step_kind::driving_offset:
        case turn_step_kind::frame_driving_offset:
            return gate( in.driving_view_offset_set, "no driving view offset" );
        case turn_step_kind::scent_set:
            return gate( !in.scent_masked, "scent is masked" );
        case turn_step_kind::weather_effects:
            return gate( in.surface_dry, "not on dry surface" );
        case turn_step_kind::forced_redraw:
            return gate( in.forced_redraw, "no forced redraw" );
        case turn_step_kind::pump_events:
            return gate( in.avatar_idle, "avatar busy" );
        default:
            return {};
    }
}

/// Whether a turn view of this mode admits a step of this scope.
auto admits( const turn_mode mode, const turn_scope scope ) -> bool
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

auto name_of( const turn_step_kind kind ) -> std::string_view
{
    return kRegistry[static_cast<std::size_t>( kind )].name;
}

/// Add one step's duration to its sim bucket. `sim_total` sums every bucket that is not none.
auto accumulate( perf_sample &perf, const perf_group group, const double ms ) -> void
{
    if( group == perf_group::none ) {
        return;
    }
    perf.sim_total_ms += ms;
    switch( group ) {
        case perf_group::build_map_cache:
            perf.build_map_cache_ms += ms;
            return;
        case perf_group::monmove:
            perf.monmove_ms += ms;
            return;
        case perf_group::world_tick:
            perf.world_tick_ms += ms;
            return;
        case perf_group::none:
        case perf_group::sim:
            return;
    }
}

} // namespace

auto derive_turn_plan( const turn_inputs &in ) -> turn_plan
{
    auto steps = kRegistry
    | std::views::transform( [&in]( const registry_entry & entry ) -> turn_step {
        const auto reason = skip_reason( entry, in );
        return turn_step{
            .kind = entry.kind,
            .scope = entry.scope,
            .status = reason.empty() ? step_status::run : step_status::skip,
            .reason = std::string( reason ),
            .group = entry.group,
            .reads_turn = entry.reads_turn,
            .advances_calendar = entry.advances_calendar,
        };
    } )
    | std::ranges::to<std::vector>();
    return turn_plan{ .steps = std::move( steps ) };
}

auto turn_plan::view( const turn_mode mode ) const -> std::vector<turn_step>
{
    return steps
    | std::views::filter( [mode]( const turn_step & step ) { return admits( mode, step.scope ); } )
    | std::ranges::to<std::vector>();
}

auto check_plan_laws( const turn_plan &plan ) -> std::vector<std::string>
{
    auto violations = std::vector<std::string> {};

    const auto calendar = index_of( plan.steps, turn_step_kind::advance_calendar );
    if( !calendar ) {
        violations.push_back( "calendar law: advance_calendar is missing" );
    }
    for( const auto i : std::views::iota( std::size_t{ 0 }, plan.steps.size() ) ) {
        const auto &step = plan.steps[i];
        if( step.reads_turn && calendar && *calendar >= i ) {
            violations.push_back( std::string( name_of( step.kind ) ) +
                                  " reads the turn before advance_calendar" );
        }
        if( ( step.status == step_status::skip ) != !step.reason.empty() ) {
            violations.push_back( std::string( name_of( step.kind ) ) +
                                  ": skip status and reason disagree" );
        }
    }

    const auto client_turn = plan.view( turn_mode::client_turn );
    if( client_turn.empty() ) {
        violations.push_back( "fork law: the client turn view is empty" );
    }
    for( const auto &step : client_turn ) {
        if( step.scope != turn_scope::avatar ) {
            violations.push_back( "fork law: client turn admits " +
                                  std::string( name_of( step.kind ) ) + ", which is not avatar scope" );
        }
    }

    const auto client_frame = plan.view( turn_mode::client_frame );
    for( const auto &step : plan.steps ) {
        if( step.scope != turn_scope::frame ) {
            continue;
        }
        const auto in_frame = std::ranges::count( client_frame, step.kind, &turn_step::kind );
        const auto in_turn = std::ranges::count( client_turn, step.kind, &turn_step::kind );
        if( in_frame != 1 ) {
            violations.push_back( "frame law: " + std::string( name_of( step.kind ) ) + " runs " +
                                  std::to_string( in_frame ) + " times in the client frame view" );
        }
        if( in_turn != 0 ) {
            violations.push_back( "frame law: " + std::string( name_of( step.kind ) ) +
                                  " runs in the client turn view" );
        }
    }

    return violations;
}

auto index_of( const std::vector<turn_step> &steps,
               const turn_step_kind kind ) -> std::optional<std::size_t>
{
    const auto found = std::ranges::find( steps, kind, &turn_step::kind );
    if( found == steps.end() ) {
        return std::nullopt;
    }
    return static_cast<std::size_t>( found - steps.begin() );
}

auto sim_perf_aggregator::add( const perf_sample &sample ) -> std::optional<std::string>
{
    ++turns_;
    total_.sim_total_ms += sample.sim_total_ms;
    total_.build_map_cache_ms += sample.build_map_cache_ms;
    total_.monmove_ms += sample.monmove_ms;
    total_.world_tick_ms += sample.world_tick_ms;
    if( turns_ < kTurnsPerLine ) {
        return std::nullopt;
    }
    const auto turns = static_cast<double>( turns_ );
    auto line = std::ostringstream{};
    line << "[sim][perf] " << turns_ << " turns avg: sim_total=" << total_.sim_total_ms / turns
         << "ms (build_map_cache=" << total_.build_map_cache_ms / turns
         << " monmove=" << total_.monmove_ms / turns
         << " world_tick=" << total_.world_tick_ms / turns << ")";
    turns_ = 0;
    total_ = perf_sample{};
    return line.str();
}

auto turn_executor::run( game &g, const turn_plan &plan, const turn_mode mode ) -> turn_report
{
    auto report = turn_report{};
    for( const turn_step &step : plan.view( mode ) ) {
        auto entry = turn_step_report{ .kind = step.kind, .status = step.status, .reason = step.reason };
        if( step.status == step_status::run ) {
            const auto start = std::chrono::steady_clock::now();
            run_step( g, step.kind );
            entry.duration_ms =
                std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start ).count();
            accumulate( report.perf, step.group, entry.duration_ms );
        }
        report.steps.push_back( std::move( entry ) );
    }
    return report;
}

auto turn_executor::run_step( game &g, const turn_step_kind kind ) -> void
{
    avatar &u = g.u;
    map &m = g.m;
    switch( kind ) {
        case turn_step_kind::cleanup_arenas:
            cleanup_arenas();
            break;
        case turn_step_kind::advance_calendar:
            if( g.new_game ) {
                g.new_game = false;
            } else {
                if( !g.gamemode ) {
                    g.gamemode = std::make_unique<special_game>();
                }
                g.gamemode->per_turn();
                calendar::turn += 1_turns;
            }
            g.swapping_dimensions = false;
            break;
        case turn_step_kind::mark_visibility_dirty:
            level_cache_freshness::report( m, level_cache_freshness::light_changed{
                .scope = level_cache_freshness::light_changed::lightmap_scope::none,
                .visibility = true,
            } );
            break;
        case turn_step_kind::frame_caches:
            level_cache_freshness::report( m, level_cache_freshness::light_changed{
                .scope = level_cache_freshness::light_changed::lightmap_scope::none,
                .visibility = true,
            } );
            g.update_performance_bubble();
            break;
        case turn_step_kind::clear_weather_temp_cache:
            get_weather().clear_temp_cache();
            break;
        case turn_step_kind::load_npcs:
            g.load_npcs();
            break;
        case turn_step_kind::timed_events_and_missions:
            g.timed_events.process();
            mission::process_all();
            break;
        case turn_step_kind::vehicle_theft:
            if( u.in_vehicle && u.controlling_vehicle ) {
                vehicle *veh = veh_pointer_or_null( m.veh_at( u.bub_pos() ) );
                if( veh && !veh->handle_potential_theft( u, true ) ) {
                    veh->handle_potential_theft( u, false, false );
                }
            }
            break;
        case turn_step_kind::mount_spook:
            if( u.is_mounted() ) {
                u.check_mount_is_spooked();
            }
            break;
        case turn_step_kind::process_mongroups:
            get_overmapbuffer( g.current_dimension_id_ ).process_mongroups();
            break;
        case turn_step_kind::move_hordes:
            get_overmapbuffer( g.current_dimension_id_ ).move_hordes();
            if( u.has_trait( trait_HAS_NEMESIS ) ) {
                get_overmapbuffer( g.current_dimension_id_ ).move_nemesis();
            }
            m.spawn_monsters( false );
            break;
        case turn_step_kind::debug_hour_timer:
            g.debug_hour_timer.print_time();
            break;
        case turn_step_kind::update_body:
            u.update_body();
            break;
        case turn_step_kind::autosave:
            g.autosave();
            break;
        case turn_step_kind::update_weather:
            get_weather().update_weather();
            g.reset_light_level();
            break;
        case turn_step_kind::perhaps_add_random_npc:
            g.perhaps_add_random_npc();
            break;
        case turn_step_kind::voluntary_interrupt:
            g.process_voluntary_act_interrupt();
            break;
        case turn_step_kind::process_activity:
            g.process_activity();
            break;
        case turn_step_kind::performance_bubble:
            g.update_performance_bubble();
            break;
        case turn_step_kind::reset_sound_markers:
            sounds::reset_markers();
            break;
        case turn_step_kind::npc_sound_markers:
            for( npc &guy : g.all_npcs() ) {
                if( rl_dist( guy.bub_pos(), u.bub_pos() ) < g_max_view_distance ) {
                    sounds::process_sound_markers( &guy );
                }
            }
            break;
        case turn_step_kind::avatar_sound_markers:
            sounds::process_sound_markers( &u );
            break;
        case turn_step_kind::hearing_loss:
            if( u.is_deaf() ) {
                sfx::do_hearing_loss();
            }
            break;
        case turn_step_kind::clear_recent_sounds:
            sounds::clear_recent_sounds();
            break;
        case turn_step_kind::driving_offset:
        case turn_step_kind::frame_driving_offset: {
            vehicle *veh = veh_pointer_or_null( m.veh_at( u.bub_pos() ) );
            g.calc_driving_offset( veh );
            break;
        }
        case turn_step_kind::scent_set:
            g.scent.set( u.bub_pos(), u.scent, u.get_type_of_scent() );
            get_overmapbuffer( g.current_dimension_id_ ).set_scent( u.abs_omt_pos(), u.scent );
            break;
        case turn_step_kind::scent_update:
            g.scent.update( u.bub_pos(), m );
            break;
        case turn_step_kind::creature_in_field:
            m.creature_in_field( u );
            break;
        case turn_step_kind::build_floor_caches:
            m.build_floor_caches();
            break;
        case turn_step_kind::vehicle_moves:
            m.process_falling();
            g.autopilot_vehicles();
            m.vehmove();
            break;
        case turn_step_kind::process_items: {
            ZoneScopedN( "do_turn_process_items" );
            m.process_items();
            break;
        }
        case turn_step_kind::grid_trackers:
            for( auto &[dim_id, tracker_ptr] : g.grid_trackers_ ) {
                if( tracker_ptr ) {
                    tracker_ptr->update( calendar::turn );
                }
            }
            break;
        case turn_step_kind::tick_portals:
            g.tick_portal_links();
            g.tick_temporary_pocket_dimensions();
            g.tick_vehicle_portal_taps();
            break;
        case turn_step_kind::fluid_grid:
            fluid_grid::update( calendar::turn );
            break;
        case turn_step_kind::process_sounds:
            sounds::process_sounds();
            break;
        case turn_step_kind::build_map_cache:
            m.build_map_cache( level_cache_freshness::plan_for( m,
                               level_cache_freshness::pose_of_viewer( u, g.get_levz() ),
                               level_cache_freshness::lightmap_policy::skip ) );
            break;
        case turn_step_kind::frame_map_caches:
            m.build_floor_caches();
            m.build_map_cache( level_cache_freshness::plan_for( m,
                               level_cache_freshness::pose_of_viewer( u, g.get_levz() ),
                               level_cache_freshness::lightmap_policy::skip ) );
            break;
        case turn_step_kind::monmove:
            g.monmove();
            break;
        case turn_step_kind::npc_move:
            g.npcmove();
            break;
        case turn_step_kind::sleep_skip_npc:
            g.sleep_skip_npc_process();
            break;
        case turn_step_kind::overmap_npc_move:
            g.overmap_npc_move();
            break;
        case turn_step_kind::update_stair_monsters:
            g.update_stair_monsters();
            break;
        case turn_step_kind::mon_info_update:
        case turn_step_kind::frame_mon_info:
            g.mon_info_update();
            break;
        case turn_step_kind::process_turn: {
            ZoneScopedN( "do_turn_player_process_turn" );
            u.process_turn();
            break;
        }
        case turn_step_kind::lua_every_x: {
            ZoneScopedN( "do_turn_lua_every_x" );
            cata::run_on_every_x_hooks( *DynamicDataLoader::get_instance().lua );
            break;
        }
        case turn_step_kind::explosions:
            explosion_handler::get_explosion_queue().execute();
            break;
        case turn_step_kind::cleanup_dead:
            g.cleanup_dead();
            break;
        case turn_step_kind::forced_redraw:
            ui_manager::redraw();
            refresh_display();
            break;
        case turn_step_kind::weather_effects: {
            weather_manager &weather = get_weather();
            handle_weather_effects( weather.weather_id );
            break;
        }
        case turn_step_kind::wait_redraw:
        case turn_step_kind::frame_wait_redraw:
            g.handle_wait_activity_redraw();
            break;
        case turn_step_kind::update_bodytemp: {
            weather_manager &weather = get_weather();
            u.update_bodytemp( m, weather );
            character_funcs::update_body_wetness( u, get_weather().get_precise() );
            u.apply_wetness_morale( weather.temperature );
            break;
        }
        case turn_step_kind::reset_volume:
            u.volume = 0;
            break;
        case turn_step_kind::world_tick:
            g.world_tick();
            break;
        case turn_step_kind::submap_loader: {
            for( const auto &dim_id : submap_loader.active_dimensions() ) {
                ensure_distribution_grid_tracker_for( dim_id );
            }
            submap_loader.update_lazy_border_focus( g.current_dimension_id_, u.abs_pos() );
            submap_loader.update();
            for( auto it = g.grid_trackers_.begin(); it != g.grid_trackers_.end(); ) {
                if( !it->first.is_empty() && !it->second->has_tracked_submaps() ) {
                    submap_loader.remove_listener( it->second.get() );
                    it = g.grid_trackers_.erase( it );
                } else {
                    ++it;
                }
            }
            break;
        }
        case turn_step_kind::clear_d_maps:
            Pathfinding::clear_d_maps();
            break;
        case turn_step_kind::pump_events:
            inp_mngr.pump_events();
            break;
        case turn_step_kind::frame_hearing_loss:
            if( !u.is_deaf() ) {
                sfx::remove_hearing_loss();
            }
            break;
        case turn_step_kind::frame_audio:
            sfx::do_danger_music();
            sfx::do_vehicle_engine_sfx();
            sfx::do_vehicle_exterior_engine_sfx();
            sfx::do_fatigue();
            break;
    }
}
