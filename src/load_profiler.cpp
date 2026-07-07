#include "load_profiler.h"

#include <algorithm>
#include <ranges>
#include <sstream>
#include <string>

#include "debug.h"

#define dbg( x ) DebugLogFL( ( x ), DC::Main )
namespace load_profiler
{

auto phase_name( load_phase phase ) -> std::string_view
{
    switch( phase ) {
        case load_phase::world_setup:
            return "world_setup";
        case load_phase::save_master:
            return "save_master";
        case load_phase::save_deserialize:
            return "save_deserialize";
        case load_phase::save_lua_state:
            return "save_lua_state";
        case load_phase::update_map:
            return "update_map";
        case load_phase::map_load_submaps:
            return "map_load_submaps";
        case load_phase::map_cache_build:
            return "map_cache_build";
        case load_phase::map_spawn_monsters:
            return "map_spawn_monsters";
        case load_phase::map_vehicle_cache:
            return "map_vehicle_cache";
        case load_phase::first_turn:
            return "first_turn";
        case load_phase::total:
            return "total";
    }
    return "unknown";
}

// Namespace-level thread_local storage
thread_local accumulator_t accumulator;
thread_local int ref_count = 0;

accumulator_t &phase_timer::get_accumulator()
{
    return accumulator;
}

int &phase_timer::get_ref_count()
{
    return ref_count;
}

phase_timer::phase_timer( load_phase phase )
    : phase_( phase ), start_( std::chrono::steady_clock::now() )
{
    ++get_ref_count();
}

phase_timer::~phase_timer()
{
    duration_ = std::chrono::steady_clock::now() - start_;
    auto &acc = get_accumulator();
    acc[phase_] += std::chrono::duration_cast<std::chrono::microseconds>( duration_ ).count() / 1000.0;

    --get_ref_count();
    if( get_ref_count() <= 0 ) {
        // Print summary
        std::ostringstream buf;
        buf << "[LOAD-PROF] === Loading Phase Summary ===\n";

        // Sort by duration (descending)
        using entry_t = std::pair<load_phase, double>;
        std::vector<entry_t> entries( acc.begin(), acc.end() );
        std::ranges::sort( entries, {}, &entry_t::second );
        std::ranges::reverse( entries );

        double total_ms = 0.0;
        for( const auto &[phase, ms] : entries ) {
            buf << string_format( "[LOAD-PROF]   %15s: %8.1f ms\n", phase_name( phase ).data(), ms );
            total_ms += ms;
        }
        buf << "[LOAD-PROF] ----------------------------\n";
        dbg( DL::Info ) << buf.str();
    }
}

auto reset() -> void
{
    accumulator.clear();
    ref_count = 0;
}

} // namespace load_profiler
