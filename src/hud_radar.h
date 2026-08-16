#pragma once
#ifndef CATA_SRC_HUD_RADAR_H
#define CATA_SRC_HUD_RADAR_H

#include "hud_runic.h"

class avatar;

/// The dot-matrix tactical minimap that occupies the HUD's RADAR region.
///
/// The dots are GPU quads on the unlit UI layer (`render_state::queue_ui_rect`),
/// not RmlUi elements: this project registers no custom `Rml::ElementInstancer`,
/// so a document cannot host C++-drawn geometry, and a few thousand real elements
/// per frame would not be affordable if it could. The RmlUi side of the region is
/// therefore nothing but the panel's border and its `.nc-colhead`, and the quads
/// are drawn UNDER it — which is also why `#hud-radar` carries `.hud-panel-clear`
/// and this layer paints its own opaque ground.
namespace hud_radar
{

/// Queue the ground, the world dots and the creature blips for this frame.
/// No-op when the radar is toggled off, when `l.radar` is empty, or when the
/// render state is not ready. MUST be called from inside a `ui_adaptor` redraw
/// callback so the quads land in that adaptor's retained slice.
auto draw( const avatar &u, const hud_runic::layout &l ) -> void;

/// True when the last `draw` emitted a blinking hostile blip, i.e. the main loop
/// must keep redrawing for the blink to be visible. Read by
/// `minimap_requires_animation()` in `animation.cpp`.
auto requires_animation() -> bool;

} // namespace hud_radar

#endif // CATA_SRC_HUD_RADAR_H
