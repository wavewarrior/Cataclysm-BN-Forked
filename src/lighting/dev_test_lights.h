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

#include <vector>

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

} // namespace dev_test_lights
