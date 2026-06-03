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

// (Re)read the icon registry from gfx/widgets/icons.json: a map of logical icon
// id -> SVG filename (+ animation specs, consumed by the sidebar animation
// system). Optional file — if absent or an id is unlisted, get() falls back to
// "<name>.svg", so every icon keeps working without a registry entry. Called
// lazily on the first get(); safe to call again to pick up edits.
void load_config();

// Drop cached rasters and re-read icons.json so an edited SVG / remapped id shows
// without a restart. Mirrors the tileset reload path.
void reload();
} // namespace widget_icon

// Draw a cached widget icon into a sidebar panel cell, tinted by `color`. The
// icon is square and one cell tall, placed at cell (col,row) within `win`. No-op
// if the window is invalid, the asset is missing, or no GPU device is live.
// Defined in the SDL backend (sdltiles.cpp), where the font cell size and the
// render queue live.
namespace sidebar_anim
{
struct icon_transform;
} // namespace sidebar_anim

// Animated variant: applies a per-frame transform (scale about the cell centre,
// alpha, vertical offset, tint colour-blend; rotation lands with the spin stage)
// resolved from the sidebar animation registry. The plain overload below is the
// identity case.
void draw_widget_icon( const catacurses::window &win, const point &cell,
                       const std::string &icon, const nc_color &color,
                       const sidebar_anim::icon_transform &tr );

void draw_widget_icon( const catacurses::window &win, const point &cell,
                       const std::string &icon, const nc_color &color );
