#include "fire_cmd.h"

#include "game.h" // for get_map()
#include "map.h"

auto resolve_aim_line( const tripoint_bub_ms& source, const tripoint_bub_ms& target )
-> fire_trajectory
{
    // map::find_clear_path() is fully deterministic: pure Bresenham geometry +
    // LOS obstacle checks.  No rng(), one_in(), roll_remainder(), or
    // dispersion.roll() calls anywhere in its call chain.
    return get_map().find_clear_path( source, target );
}
