#pragma once

#include "coordinates.h"

namespace sfx
{

/**
 * Emit a sound pulse for visualization at the given source position.
 * Seeds a lazy Dijkstra BFS — the flood-fill is advanced incrementally
 * each frame in the render loop via advance_all_pulses().
 */
auto emit_sound_pulse( const tripoint_bub_ms& source, float volume ) -> void;

/**
 * Advance the lazy BFS for all active pulses so the reachable field
 * stays ahead of the animated wavefront. Call once per frame before
 * rendering sound pulses.
 */
auto advance_all_pulses( double now ) -> void;

/// Returns true while any sound pulses are still expanding — lets the game
/// loop's anim_timeout keep ticking at animation cadence without the render
/// function fighting over g_display.inputdelay.
auto sound_pulses_active() -> bool;

} // namespace sfx