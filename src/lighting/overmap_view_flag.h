#pragma once

namespace lighting {

/// True while the overmap UI (overmap_ui.cpp display()) is open, i.e. the world
/// target shows the overmap rather than the local map. Scoped to the UI itself,
/// not to draw_om: a redraw cycle clears the tile queue on every pass but only
/// re-runs draw_om when the overmap adaptor was invalidated. World-only overlays
/// (emitter glow, godray shafts, dust motes) check this and stay off.
inline auto overmap_view_open = false;

} // namespace lighting
