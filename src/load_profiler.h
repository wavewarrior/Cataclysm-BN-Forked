#pragma once

/// @brief Structured logging for game loading phases (menu → in-game).
///
/// Usage: wrap each loading phase with a scoped timer that auto-logs on destruction.
///   {
///       load_profiler::phase_timer t( load_profiler::load_phase::world_setup );
///       // ... loading work ...
///   } // logs automatically
///
/// Phases are additive (multiple timers can run in parallel for nested phases).
/// Final summary is printed when the last timer for a session is destroyed.

#include <chrono>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "debug.h"

namespace load_profiler
{

enum class load_phase {
    world_setup,         ///< game::setup() — JSON mod loading
    save_master,         ///< load_master() — faction/dimension data
    save_deserialize,    ///< unserialize() — full game state
    save_lua_state,      ///< load_world_lua_state()
    update_map,          ///< game::update_map() — submap loading
    map_load_submaps,    ///< map::load() — loadn() per submap
    map_cache_build,     ///< map::build_map_cache()
    map_spawn_monsters,  ///< map::spawn_monsters()
    map_vehicle_cache,   ///< map::reset_vehicle_cache()
    first_turn,          ///< first game::do_turn() after loading
    total                ///< overall loading time (menu → in-game)
};

using accumulator_t = std::unordered_map<load_phase, double>;

auto phase_name( load_phase phase ) -> std::string_view;

/// @brief Scoped timer for a single loading phase.
/// Construct at the start of a phase; logs duration on destruction.
/// If this is the last timer destroyed (ref_count reaches 0), prints the full summary.
class phase_timer {
    const load_phase phase_;
    const std::chrono::steady_clock::time_point start_;
    std::chrono::steady_clock::duration duration_;

    static accumulator_t &get_accumulator();
    static int &get_ref_count();

public:
    explicit phase_timer( load_phase phase );
    ~phase_timer();

    /// Non-copyable, non-movable.
    phase_timer( const phase_timer & ) = delete;
    phase_timer &operator=( const phase_timer & ) = delete;
    phase_timer( phase_timer && ) = delete;
    phase_timer &operator=( phase_timer && ) = delete;
};

/// @brief Reset all accumulated timings. Call before starting a new loading session.
auto reset() -> void;

} // namespace load_profiler
