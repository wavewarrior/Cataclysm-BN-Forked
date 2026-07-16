#pragma once
#ifndef CATA_SRC_HUD_SHAKE_H
#define CATA_SRC_HUD_SHAKE_H

// Screen shake + damage vignette feedback for HUD elements.
// Triggered by damage events; decays exponentially each frame.
// Applied as margin offsets on HUD containers via CSS.

#include <cstdint>

namespace hud_shake
{

// Per-sample shake offset (dx, dy in pixels).
struct shake_offset {
    float dx = 0.0f;
    float dy = 0.0f;
};

// Trigger a shake event. `intensity` in [0, 1] where 1.0 = max 6px offset.
auto trigger( float intensity ) -> void;

// Sample current shake offset for this frame. Returns (0, 0) when decayed.
auto sample() -> shake_offset;

// Decay shake by `dt_seconds`. Call each render frame.
auto tick( float dt_seconds ) -> void;

// Current shake intensity (0 = none, 1 = max). Exposed for post-process CA tie-in.
auto intensity() -> float;

} // namespace hud_shake

#endif // CATA_SRC_HUD_SHAKE_H