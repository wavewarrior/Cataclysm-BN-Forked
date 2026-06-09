#pragma once
#ifndef CATA_SRC_CATA_IMGUI_H
#define CATA_SRC_CATA_IMGUI_H

#include <string>

#include "color.h"

/// Minimal Dear ImGui helpers for player-facing menus.
namespace cataimgui
{

/// Render `<color_x>`-tagged text into the current ImGui window.
/// Color tags like `<color_red>`, `</color>` are parsed; non-color tags
/// are left as-is.  The nc_color parameter is unused (pilot uses a fixed
/// ANSI table); it is kept for API compatibility.
void draw_colored_text( const std::string &text, nc_color base_fallback = c_light_gray );

/// Render plain text colored by nc_color (no tag parsing).
void text_colored( nc_color c, const std::string &text );

} // namespace cataimgui

#endif // CATA_SRC_CATA_IMGUI_H
