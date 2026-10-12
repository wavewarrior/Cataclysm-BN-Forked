#pragma once
#ifndef CATA_SRC_TURN_PLAN_H
#define CATA_SRC_TURN_PLAN_H

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// The turn plan: the order and gates of one game turn as a pure value. Deriving a plan
// needs only `turn_inputs`, so every plan law can be asserted without a game, a world or
// a GPU. `turn_executor` (turn_plan.cpp) dispatches from the same plan.

/// Which part of the world a step touches.
enum class turn_scope {
    /// Host-owned turn state: calendar, monsters, NPCs, weather, world tick.
    world,
    /// The avatar's own state, which the host and the coop client both run.
    avatar,
    /// Once-per-sync caches, UI and audio.
    frame,
};

/// Coop role of this process.
enum class coop_role {
    solo,
    host,
    client,
};

/// Which view of the plan an executor runs.
enum class turn_mode {
    solo,
    host,
    client_turn,
    client_frame,
};

/// Which sim timing bucket a step's duration is accumulated into.
enum class perf_group {
    none,
    sim,
    build_map_cache,
    monmove,
    world_tick,
};

/// Whether a step runs or is skipped by its gate.
enum class step_status {
    run,
    skip,
};

/// Every step of one game turn, in plan order.
enum class turn_step_kind {
    cleanup_arenas,
    advance_calendar,
    mark_visibility_dirty,
    clear_weather_temp_cache,
    load_npcs,
    timed_events_and_missions,
    vehicle_theft,
    mount_spook,
    process_mongroups,
    move_hordes,
    debug_hour_timer,
    update_body,
    autosave,
    update_weather,
    perhaps_add_random_npc,
    voluntary_interrupt,
    process_activity,
    performance_bubble,
    reset_sound_markers,
    npc_sound_markers,
    avatar_sound_markers,
    hearing_loss,
    clear_recent_sounds,
    driving_offset,
    scent_set,
    scent_update,
    creature_in_field,
    build_floor_caches,
    vehicle_moves,
    process_items,
    grid_trackers,
    tick_portals,
    fluid_grid,
    process_sounds,
    build_map_cache,
    monmove,
    npc_move,
    sleep_skip_npc,
    overmap_npc_move,
    update_stair_monsters,
    mon_info_update,
    process_turn,
    lua_every_x,
    explosions,
    cleanup_dead,
    forced_redraw,
    weather_effects,
    wait_redraw,
    update_bodytemp,
    reset_volume,
    world_tick,
    submap_loader,
    clear_d_maps,
    pump_events,
    frame_caches,
    frame_driving_offset,
    frame_map_caches,
    frame_mon_info,
    frame_wait_redraw,
    frame_hearing_loss,
    frame_audio,
};

/// Sleep-skip gates. Each flag is true when the matching sleep-skip option applies this turn.
/// Coop clients pass all-false, because the original client path never gated on sleep.
struct sleep_inputs {
    bool vehicles = false;
    bool sound = false;
    bool monsters = false;
    bool npcs = false;
};

/// Calendar boundary gates. Each flag is the full gate, with its option already applied (e.g.
/// autosave), evaluated against the turn before advance_calendar runs, as the original did.
struct calendar_inputs {
    bool daily = false;
    bool horde_move = false;
    bool npc_overmap_move = false;
    bool autosave = false;
};

/// Everything a plan depends on. Built by the caller from game state; a plan is pure over it.
struct turn_inputs {
    coop_role coop = coop_role::solo;
    sleep_inputs sleep;
    calendar_inputs calendar;
    bool npcs_dirty = false;
    /// True when the avatar is idle, so input can be pumped.
    bool avatar_idle = false;
    /// True when the driving view offset is non-zero.
    bool driving_view_offset_set = false;
    /// True when the avatar's scent is masked (bionic or debug), so scent is not set.
    bool scent_masked = false;
    /// True when the avatar is on dry, above-ground terrain that gets weather effects.
    bool surface_dry = false;
    /// True when the forced-redraw option applies (the avatar has negative moves).
    bool forced_redraw = false;
};

/// One step of the plan. `reason` is non-empty exactly when `status` is skip.
struct turn_step {
    turn_step_kind kind;
    turn_scope scope;
    step_status status = step_status::run;
    std::string reason;
    perf_group group = perf_group::none;
    bool reads_turn = false;
    bool advances_calendar = false;
};

/// A full plan in turn order. `view` selects the steps one executor mode runs.
struct turn_plan {
    std::vector<turn_step> steps;

    /// The steps the given mode admits, in plan order, including skipped steps.
    auto view( turn_mode mode ) const -> std::vector<turn_step>;
};

/// Build the plan for one turn from its inputs.
auto derive_turn_plan( const turn_inputs &in ) -> turn_plan;

/// Check the plan laws. Returns one message per violation; empty means the plan is lawful.
/// Laws: calendar (advance_calendar precedes every turn reader), fork (the client turn view
/// holds only avatar steps), frame (each frame step runs once per client frame and never in a
/// client turn), and skip consistency (a skipped step has a reason and a run step has none).
auto check_plan_laws( const turn_plan &plan ) -> std::vector<std::string>;

/// Position of the first step of this kind, if present.
auto index_of( const std::vector<turn_step> &steps,
               turn_step_kind kind ) -> std::optional<std::size_t>;

/// Turns per aggregated sim perf line.
inline constexpr int kTurnsPerLine = 20;

/// Milliseconds spent in one turn's sim timing buckets.
struct perf_sample {
    double sim_total_ms = 0.0;
    double build_map_cache_ms = 0.0;
    double monmove_ms = 0.0;
    double world_tick_ms = 0.0;
};

/// Averages perf samples and emits the legacy `[sim][perf]` line every `kTurnsPerLine` turns.
class sim_perf_aggregator
{
    public:
        /// Add one turn. Returns the formatted line when a window completes, then resets.
        auto add( const perf_sample &sample ) -> std::optional<std::string>;

    private:
        int turns_ = 0;
        perf_sample total_;
};

class game;

/// The timing and outcome of one executed step.
struct turn_step_report {
    turn_step_kind kind;
    step_status status;
    std::string reason;
    double duration_ms = 0.0;
};

/// The outcome of one executed view of the plan.
struct turn_report {
    std::vector<turn_step_report> steps;
    perf_sample perf;
};

/// Runs the steps a mode admits, in plan order, against the game.
class turn_executor
{
    public:
        /// Run every step of `plan` that `mode` admits. Skipped steps are reported, not run.
        static auto run( game &g, const turn_plan &plan, turn_mode mode ) -> turn_report;

    private:
        /// Run one step body. Private so the friend grant in game.h covers the private members it uses.
        static auto run_step( game &g, turn_step_kind kind ) -> void;
};

#endif // CATA_SRC_TURN_PLAN_H
