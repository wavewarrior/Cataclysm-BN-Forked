#pragma once

#include <string>

struct SDL_GPUTexture;
struct point;
class nc_color;
namespace catacurses
{
class window;
} // namespace catacurses

// Two-tone SVG icon system for the sidebar widget engine.
//
// Icons live in gfx/widgets/<name>.svg, authored as white strokes (#FFFFFF) +
// mid-gray fills (~#707070). They are rasterized once per (name, pixel-size) at
// the current font cell size, uploaded to the GPU, cached, and drawn as unlit UI
// sprites tinted by a curses color — so one tint yields an outline in the color
// plus a darker-shade fill, and the art stays crisp at any zoom / HiDPI
// (re-rasterized when the cell size changes).
namespace widget_icon
{
// Return a cached GPU texture for gfx/widgets/<name>.svg rasterized at px*px,
// rasterizing+uploading once on first request for a given (name, px). Returns
// nullptr (logged once) if the asset is missing, rasterization fails, or no GPU
// device is available (e.g. headless tests). The handle is owned by the cache;
// callers must not free it.
SDL_GPUTexture *get( const std::string &name, int px );

// Free all cached icon textures. Call when the font cell size changes (resize /
// zoom) so stale-size rasters don't accumulate, and on data reload / shutdown.
void clear();
} // namespace widget_icon

// Draw a cached widget icon into a sidebar panel cell, tinted by `color`. The
// icon is square and one cell tall, placed at cell (col,row) within `win`. No-op
// if the window is invalid, the asset is missing, or no GPU device is live.
// Defined in the SDL backend (sdltiles.cpp), where the font cell size and the
// render queue live.
void draw_widget_icon( const catacurses::window &win, const point &cell,
                       const std::string &icon, const nc_color &color );
