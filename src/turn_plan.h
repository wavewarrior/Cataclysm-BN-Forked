#pragma once
#ifndef CATA_SRC_TURN_PLAN_H
#define CATA_SRC_TURN_PLAN_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "coop_session.h"

class game;

// The turn plan: the ordered list of named steps one simulation turn runs, as a pure
// value. `make_turn_plan()` is a pure function of `turn_inputs` (plain facts copied out
// of the game) and a `turn_mode`. It keeps the steps of the mode in canonical order, and
// records a skip reason for each step whose gate is false. `run_turn_plan()` executes a
// plan against the game and fills a `turn_report`. `sim_perf_window` folds reports into
// the `[sim][perf]` line.
//
// Plan laws (tests/turn_plan_test.cpp, tag [turn_plan]):
// - the client turn plan holds only avatar-scope steps, so no world step runs on a client
// - calendar_advance precedes every running step that reads the turn counter
// - frame steps appear only in the client frame plan
// - every skipped step records a reason
//
// Caller-owned preconditions, outside the plan: `cleanup_arenas()` runs before the plan,
// and so does the fixed-window activity skip; when that skip applies, the caller returns
// before building a plan.
//
// Nothing in the game calls this yet. A later ticket cuts the three turn cycles over.

/// Kinds of step scope. World steps run once per turn on the host or solo, avatar steps
/// run on every process, and frame steps run once per render frame on a client.
enum class turn_scope : std::uint8_t {
    world,
    avatar,
    frame,
};

/// Which plan a caller wants. Solo and host share the full turn; the client runs its own
/// avatar-only turn and a frame plan.
enum class turn_mode : std::uint8_t {
    solo,
    host,
    client_turn,
    client_frame,
};

/// The `[sim][perf]` group a step's duration is added to. `none` steps count toward
/// sim_total only.
enum class turn_perf_group : std::uint8_t {
    none,
    build_map_cache,
    monmove,
    world_tick,
};

enum class turn_step_status : std::uint8_t {
    run,
    skipped,
};

/// Every step the turn plan knows about, in canonical order. Frame kinds come last.
enum class turn_step_kind : std::uint8_t {
    new_game_start,
    calendar_advance,
    swap_reset,
    light_dirty,
    temp_cache_clear,
    npc_reload,
    timed_events,
    missions,
    vehicle_theft,
    mount_spook,
    daily_mongroups,
    hordes_move,
    debug_hour_timer,
    update_body,
    autosave,
    weather_update,
    random_npc,
    voluntary_interrupt,
    process_activity,
    performance_bubble,
    sound_reset,
    npc_sound_markers,
    avatar_sound_markers,
    clear_recent_sounds,
    driving_offset,
    scent_write,
    scent_update,
    build_floor_caches,
    process_falling,
    autopilot_vehicles,
    vehmove,
    process_items,
    creature_in_field,
    grid_tracker_update,
    portal_links,
    fluid_grid,
    process_sounds,
    build_map_cache,
    monmove,
    npcmove,
    sleep_skip_npc_process,
    overmap_npc_move,
    update_stair_monsters,
    mon_info_update,
    player_process_turn,
    lua_every_x,
    explosion_queue,
    cleanup_dead,
    force_redraw,
    weather_effects,
    wait_activity_redraw,
    bodytemp_wetness_morale,
    remove_hearing_loss,
    sfx_world,
    reset_noise,
    world_tick,
    submap_load,
    prune_grid_trackers,
    clear_d_maps,
    input_drain,
    frame_light_dirty,
    frame_performance_bubble,
    frame_driving_offset,
    frame_build_floor_caches,
    frame_build_map_cache,
    frame_mon_info_update,
    frame_wait_activity_redraw,
    frame_remove_hearing_loss,
    frame_sfx,
};

/// Upper bound on steps in one plan or one report. The full table has 69 kinds.
inline constexpr std::size_t kMaxTurnSteps = 80;

/// Turns folded into one `[sim][perf]` line.
inline constexpr int kSimPerfWindowTurns = 20;

/// Plain facts the gates read, copied out of the game by the caller. The plan never reads
/// game state, so the laws can be tested without a game.
struct turn_inputs {
    coop_mode coop = coop_mode::none;
    bool asleep = false;
    bool driving = false;
    bool sleep_skip_veh = false;
    bool sleep_skip_sound = false;
    bool sleep_skip_mon = false;
    bool sleep_skip_npc = false;
    bool has_activity = false;
    bool has_destination = false;
    bool new_game = false;
    bool npcs_dirty = false;
    bool controlling_owned_vehicle = false;
    bool mounted = false;
    bool tick_daily = false;
    bool tick_two_and_half_minutes = false;
    bool tick_five_minutes = false;
    bool tick_autosave = false;
    bool autosave_option = false;
    bool avatar_dead = false;
    bool scent_writable = false;
    bool driving_offset = false;
    bool deaf = false;
    bool force_redraw = false;
    bool weather_effects = false;
};

/// One planned step. `name` views a static string, so steps are trivially copyable.
struct turn_step {
    std::string_view name;
    turn_step_kind kind;
    turn_scope scope;
    turn_perf_group group;
    /// True when the step reads the turn counter or a tick derived from it.
    bool reads_turn;
    turn_step_status status;
    /// Why the step is skipped. Empty when it runs.
    std::string_view reason;
};

/// The outcome of one executed step. A skipped step has `duration_ms` 0.
struct turn_step_result {
    std::string_view name;
    turn_step_status status;
    std::string_view reason;
    turn_perf_group group;
    double duration_ms;
};

/// A fixed-capacity list with no heap allocation, holding at most `kMaxTurnSteps` items.
template<typename T>
class turn_fixed_list
{
    public:
        auto push_back( const T &value ) -> void {
            items_[count_] = value;
            ++count_;
        }

        auto items() const -> std::span<const T> {
            return { items_.data(), count_ };
        }

    private:
        std::array<T, kMaxTurnSteps> items_{};
        std::size_t count_ = 0;
};

/// The ordered steps of one turn, as a pure value.
using turn_plan = turn_fixed_list<turn_step>;

/// The per-step outcome of one executed plan, in plan order.
using turn_report = turn_fixed_list<turn_step_result>;

/// Build the plan for `mode` from `inputs`. Pure: the same inputs always give the same plan.
auto make_turn_plan( const turn_inputs &inputs, turn_mode mode ) -> turn_plan;

/// Execute `plan` against `gm`. Each running step is timed and reported, and each skipped
/// step is reported with its reason and a zero duration.
auto run_turn_plan( game &gm, const turn_plan &plan ) -> turn_report;

/// Running totals for the `[sim][perf]` line, in milliseconds.
struct sim_perf_window {
    int turns = 0;
    double sim_total_ms = 0.0;
    double build_map_cache_ms = 0.0;
    double monmove_ms = 0.0;
    double world_tick_ms = 0.0;
};

/// Fold one turn's running steps into `window`. Returns true once the window holds
/// `kSimPerfWindowTurns` turns, which is when the caller must emit the line and reset.
auto add_turn_to_perf_window( sim_perf_window &window, const turn_report &report ) -> bool;

/// The `[sim][perf]` line for a full window, with every value averaged per turn.
auto format_sim_perf_line( const sim_perf_window &window ) -> std::string;

#endif // CATA_SRC_TURN_PLAN_H
