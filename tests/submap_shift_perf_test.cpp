#include "catch/catch_amalgamated.hpp"

#include <algorithm>
#include <chrono>

#include "avatar.h"
#include "game.h"
#include "game_constants.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "state_helpers.h"
#include "veh_type.h"

// Populate the map with content that exercises the shift hot path:
// vehicles (loaded_vehicles registration), items (active_items tracking),
// furniture with traps (funnel registration), and mixed terrain.
static void populate_map_with_content( map &here )
{
    // Scatter vehicles across several submaps.
    const std::array<tripoint_bub_ms, 4> vehicle_spots = { {
            { 30, 30, 0 }, { 60, 30, 0 }, { 90, 30, 0 }, { 120, 60, 0 }
        }
    };
    for( const auto &pos : vehicle_spots ) {
        here.add_vehicle( vproto_id( "car" ), pos, 0_degrees, 0, 0 );
    }

    // Scatter items across many tiles to populate active_items and submap content.
    for( int x = 10; x < 160; x += 8 ) {
        for( int y = 10; y < 160; y += 8 ) {
            here.add_item_or_charges( tripoint_bub_ms( x, y, 0 ),
                                      item::spawn( "flashlight_on" ) );
            here.add_item_or_charges( tripoint_bub_ms( x + 1, y, 0 ),
                                      item::spawn( "rock" ) );
        }
    }

    // Place furniture on many tiles.
    for( int x = 12; x < 160; x += 12 ) {
        for( int y = 12; y < 160; y += 12 ) {
            here.furn_set( tripoint_bub_ms( x, y, 0 ), furn_id( "f_table" ) );
        }
    }
}

static auto run_shift_benchmark( int iterations ) -> std::pair<double, double>
{
    const int start_y = g_half_mapsize_y + SEEY / 2;

    double total_ms = 0;
    double max_ms = 0;

    for( int i = 0; i < iterations; ++i ) {
        const int edge_x = ( i % 2 == 0 )
                           ? g_half_mapsize_x + SEEX - 1
                           : g_half_mapsize_x;
        g->u.setpos( tripoint_bub_ms( edge_x, start_y, 0 ) );

        const int step_x = ( i % 2 == 0 ) ? edge_x + 1 : edge_x - 1;
        int x = step_x;
        int y = start_y;

        const auto t0 = std::chrono::steady_clock::now();
        g->update_map( x, y );
        const auto t1 = std::chrono::steady_clock::now();

        const double ms = std::chrono::duration<double, std::milli>( t1 - t0 ).count();
        total_ms += ms;
        max_ms = std::max( max_ms, ms );
    }
    return { total_ms / iterations, max_ms };
}

// Benchmark on an empty map — baseline.
TEST_CASE( "submap_shift_perf_empty", "[.][perf]" )
{
    clear_all_state();
    build_test_map( ter_id( "t_pavement" ) );

    // Warm up: two shifts to populate caches.
    const int start_x = g_half_mapsize_x + SEEX - 1;
    const int start_y = g_half_mapsize_y + SEEY / 2;
    g->u.setpos( tripoint_bub_ms( start_x, start_y, 0 ) );
    { int x = start_x + 1, y = start_y; g->update_map( x, y ); }
    g->u.setpos( tripoint_bub_ms( start_x, start_y, 0 ) );
    { int x = start_x - 1, y = start_y; g->update_map( x, y ); }

    const auto [avg, mx] = run_shift_benchmark( 4 );
    CAPTURE( avg, mx );
    CHECK( avg < 200.0 );
    CHECK( mx < 500.0 );
    WARN( "submap_shift_perf_empty: avg=" << avg << "ms max=" << mx << "ms" );
}

// Benchmark on a populated map — realistic.
TEST_CASE( "submap_shift_perf_populated", "[.][perf]" )
{
    clear_all_state();
    build_test_map( ter_id( "t_pavement" ) );
    populate_map_with_content( get_map() );

    // Warm up.
    const int start_x = g_half_mapsize_x + SEEX - 1;
    const int start_y = g_half_mapsize_y + SEEY / 2;
    g->u.setpos( tripoint_bub_ms( start_x, start_y, 0 ) );
    { int x = start_x + 1, y = start_y; g->update_map( x, y ); }
    g->u.setpos( tripoint_bub_ms( start_x, start_y, 0 ) );
    { int x = start_x - 1, y = start_y; g->update_map( x, y ); }

    const auto [avg, mx] = run_shift_benchmark( 4 );
    CAPTURE( avg, mx );
    CHECK( avg < 200.0 );
    CHECK( mx < 500.0 );
    WARN( "submap_shift_perf_populated: avg=" << avg << "ms max=" << mx << "ms" );
}
// Benchmark with real procedural mapgen enabled — exercises the actual
// loadn() → lookup_submap() → generate_omt() code path that fires when
// walking into never-visited terrain.  This is the most realistic test
// of the shift optimization since disable_mapgen=true (set by clear_all_state)
// normally makes loadn() generate empty grass, skipping real terrain gen.
TEST_CASE( "submap_shift_perf_real_mapgen", "[.][perf]" )
{
    clear_all_state();

    // Enable real mapgen so loadn() generates procedural terrain for new
    // edge submaps — the exact code path that was slow in the real game.
    disable_mapgen = false;

    const int start_x = g_half_mapsize_x + SEEX - 1;
    const int start_y = g_half_mapsize_y + SEEY / 2;

    // Warm up: two shifts to populate caches and trigger initial mapgen.
    g->u.setpos( tripoint_bub_ms( start_x, start_y, 0 ) );
    { int x = start_x + 1, y = start_y; g->update_map( x, y ); }
    g->u.setpos( tripoint_bub_ms( start_x, start_y, 0 ) );
    { int x = start_x - 1, y = start_y; g->update_map( x, y ); }

    const auto [avg, mx] = run_shift_benchmark( 4 );

    // Restore for other tests.
    disable_mapgen = true;

    CAPTURE( avg, mx );
    // Real mapgen is slower; allow up to 500ms avg, 1500ms max.
    CHECK( avg < 500.0 );
    CHECK( mx < 1500.0 );
    WARN( "submap_shift_perf_real_mapgen: avg=" << avg << "ms max=" << mx << "ms" );
}
