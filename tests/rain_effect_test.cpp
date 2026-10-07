#include "catch/catch_amalgamated.hpp"
#include "lighting/rain_effect.h"
#include "rng.h"

// How often the window redraws must never change the simulation, so the rain draws its spawn
// jitter from its own generator, never from the game's RNG. Before it did, every drawn frame of
// rain advanced the game's RNG and a windowed run diverged from the same seed.
TEST_CASE( "rain_spawns_and_steps_without_drawing_from_the_game_rng", "[lighting][rain]" )
{
    rng_set_engine_seed( 7 );
    const int next_draw = rng( 0, 1000000 );
    rng_set_engine_seed( 7 );
    lighting::rain_effect rain;
    // A drop lands within 13 steps and becomes a splash ring: both spawns run many times.
    for( int step = 0; step < 40; ++step ) {
        rain.add_drop( 3.5f, 4.5f, 0.5f );
        rain.step();
    }
    CHECK( rng( 0, 1000000 ) == next_draw );
}
