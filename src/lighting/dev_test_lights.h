#pragma once

// Dev test lights — click-to-place point lights for confirming lighting/GI.
//
// The cursor light (cursor_light_emitter) follows the mouse, which is poor for
// testing OCCLUSION (you can't pin a light beside a wall and study the shadowed
// side). These are STATIC: while the F4 lighting panel is open and "place lights
// on click" is on, a left-click in the world drops a light at the hovered tile,
// using the cursor brush's radius/intensity/colour. They are appended to the
// emitter snapshot like any other light (so they drive BOTH direct lighting and
// the GI gather), and are cleared the moment the dev UI closes — purely a
// debugging aid, never persisted.
//
// hover_* is written each frame by sdl_render_frame (which owns the
// screen→world-tile math + tilecontext) and consumed by the F4 panel on click
// (which owns the ImGui mouse state). This split keeps ImGui out of the render
// frame and game coords out of the UI layer.

#include <chrono>
#include <vector>

#include "coordinates.h"

namespace dev_test_lights {

struct light {
    float wx, wy, wz; // world-tile position
    float radius;     // tiles
    float intensity;  // brightness multiplier
    float r, g, b;    // colour tint (0..1)
};

extern bool place_mode;                    // F4 checkbox: click places a light
extern float hover_wx, hover_wy, hover_wz; // last world-tile under the cursor
extern std::vector<light> lights;          // placed lights; cleared on dev-UI close

// Debug sound pulse — an animated expanding sound wave for the sound spawner.
// source + volume + spawn_s is all the render loop needs: radius = elapsed * speed.
struct sound_pulse {
    int z = 0;                // z-level the pulse lives on
    float volume = 0.f;       // drives the maximum radius (clamped to [1, 24] tiles)
    double spawn_s = 0.0;     // steady-clock seconds at spawn
    tripoint_bub_ms source;   // world position of the sound source
};

/// Seconds since a steady epoch; shared spawn/draw clock for sound pulses.
inline double pulse_now_s()
{
    return std::chrono::duration<double>(
               std::chrono::steady_clock::now().time_since_epoch() ).count();
}

extern std::vector<sound_pulse> sound_pulses; // active debug sound pulses


} // namespace dev_test_lights
