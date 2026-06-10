#pragma once
#ifndef CATA_SRC_RML_UTIL_H
#define CATA_SRC_RML_UTIL_H

#include <string>

class nc_color;

// Shared RmlUi text helpers, promoted out of ui.cpp so every migrated screen,
// reusable component, and the world-space text layer use ONE colour/escape path
// (no dependency on ui.cpp). Signatures are std::string so this header pulls in
// neither RmlUi nor SDL — only color.h's forward-declared nc_color.

// Escape &, < and > (and turn '\n' into <br/>) so raw game text is safe inside a
// data-rml binding, which parses markup. {{ }} interpolation auto-escapes and
// does NOT need this.
std::string rml_escape( const std::string &s );

// Map an nc_color to an "#rrggbbaa" hex string (alpha 255), cached. Uses the same
// SDL palette the tile renderer uses, so RmlUi colours match in-game text exactly.
std::string nc_color_to_hex( const nc_color &color );

// Convert a Cataclysm color-tagged string (e.g. "<color_red>foo</color>") into
// RML markup with <span style="color:…"> spans; plain segments are rml_escape'd.
// Shared by the per-menu draw_rml() overrides and every migrated screen.
std::string cata_text_to_rml( const std::string &s );

#endif // CATA_SRC_RML_UTIL_H
