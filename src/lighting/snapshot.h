#pragma once
#include "lighting/event_queue.h"
#include "lighting/gpu_emitter.h"

#include <cstdint>
#include <vector>

namespace lighting {

// Build the per-frame emitter snapshot on the main thread.
//
// Walks the loaded reality bubble (same sources lightmap.cpp enumerates)
// and converts each light source to a gpu_emitter.  Also drains live
// flash_events from the provided event_queue.
//
// Call once per frame from the main game loop after all sim logic has
// run but before submitting the snapshot to the emitter_collector.
//
// Preconditions: g != nullptr, map is loaded.
// frame_ms: elapsed milliseconds since last frame (for event aging).
std::vector<gpu_emitter> build_emitter_snapshot(event_queue& eq, float frame_ms);

} // namespace lighting
