#pragma once

#include "coordinates.h"

namespace sfx
{

/**
 * Emit a sound pulse for visualization at the given source position.
 * Computes occlusion-limited reachable tiles via Dijkstra flood-fill
 * and appends a sound_pulse to dev_test_lights::sound_pulses.
 */
auto emit_sound_pulse( const tripoint_bub_ms& source, float volume ) -> void;

} // namespace sfx