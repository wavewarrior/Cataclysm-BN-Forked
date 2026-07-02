#pragma once
#ifndef CATA_SRC_RML_UTIL_H
#define CATA_SRC_RML_UTIL_H

#include <string>
#include <vector>

class nc_color;
struct item_info_data;

// Shared RmlUi text helpers, promoted out of ui.cpp so every migrated screen,
// reusable component, and the world-space text layer use ONE colour/escape path
// (no dependency on ui.cpp). Signatures are std::string so this header pulls in
// neither RmlUi nor SDL — only color.h's forward-declared nc_color.

// Escape &, < and > (and turn '\n' into <br/>) so raw game text is safe inside a
// data-rml binding, which parses markup. {{ }} interpolation auto-escapes and
// does NOT need this.
std::string rml_escape( const std::string& s );

// Map an nc_color to an "#rrggbbaa" hex string (alpha 255), cached. Honors
// data/gui/theme.json "game_colors" overrides, else uses the curses SDL palette.
std::string nc_color_to_hex( const nc_color& color );

// Drop the nc_color_to_hex cache (call after a live theme edit so reopened menus
// pick up new game-colour overrides).
void clear_nc_color_cache();

// Convert a Cataclysm color-tagged string (e.g. "<color_red>foo</color>") into
// RML markup with <span style="color:…"> spans; plain segments are rml_escape'd.
// Shared by the per-menu draw_rml() overrides and every migrated screen.
std::string cata_text_to_rml( const std::string& s );

// F.2 item-info component core: render an item_info_data's body
// (format_item_info → colour-delta tags) as RmlUi-ready markup, ONE string per
// text line. Bind the returned vector into a per-line list (`.item-info` rcss +
// a scroll-pane). This is the shared item-info pane behind draw_item_info's
// callsites; the host screen migrates render-behind and feeds its item_info_data
// here. NOTE: +/- compare-delta colouring only fires when item_compare is
// non-empty — unproven until a comparing consumer migrates.
std::vector<std::string> item_info_rml_lines( item_info_data& data );

/// RmlUi replacement for draw_item_info — opens aim_examine.rml as a blocking
/// modal overlay (PAGE_UP/PAGE_DOWN/QUIT), feeds item_info_rml_lines, handles
/// scroll. Falls back silently (no-op) when RmlUi is not ready.
void rml_examine_item( item_info_data& data );

#endif // CATA_SRC_RML_UTIL_H
