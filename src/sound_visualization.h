#pragma once

#include "coordinates.h"

namespace sfx
{

/// Emit a sound pulse for visualization at the given source position.
/// The GPU render loop animates the pulse as an expanding disc using only
/// source position, volume (→ max radius), and the spawn timestamp.
auto emit_sound_pulse( const tripoint_bub_ms &source, float volume ) -> void;

/// Returns true while any sound pulses are still expanding — lets the game
/// loop's anim_timeout keep ticking at animation cadence.
auto sound_pulses_active() -> bool;

} // namespace sfx
