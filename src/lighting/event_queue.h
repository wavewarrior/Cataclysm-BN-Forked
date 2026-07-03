#pragma once
#include "coordinates.h"

#include <mutex>
#include <vector>

namespace lighting {

// A transient lighting flash pushed by game events (explosions, muzzle
// flashes, lightning, sparks).  The emitter_collector drains the queue
// each frame and appends live events to the SSBO alongside static emitters.
struct flash_event {
    tripoint_abs_ms pos; // World-absolute position of the flash.
    float r, g, b;       // RGB color (0–1 linear).
    float intensity;     // Peak luminance in tiles (analogous to radius).
    float duration_ms;   // Total lifespan in milliseconds.
    float elapsed_ms;    // How much has already elapsed (managed by drain).
};

// Thread-safe queue.  Game-logic threads call push(); the emitter
// collector calls drain() once per frame on its own thread.
class event_queue {
public:
    // Push a new flash.  Safe to call from any thread including the sim thread.
    void push(const flash_event& ev);

    // Advance time by delta_ms, drop expired events, copy survivors into out.
    // Called by the emitter_collector on the collector thread.
    void drain(float delta_ms, std::vector<flash_event>& out);

private:
    std::mutex mu_;
    std::vector<flash_event> events_;
};

} // namespace lighting
