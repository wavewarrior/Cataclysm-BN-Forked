#pragma once
#ifndef CATA_SRC_UI_THEME_H
#define CATA_SRC_UI_THEME_H

#include <string>
#include <vector>

class nc_color;

// Central, config-driven UI theme (data/gui/theme.json). Two roles:
//   - RCSS token substitution: the RmlUi FileInterface replaces {{token}}
//     placeholders in data/gui/*.rcss with hex values from the "rcss" block
//     (RmlUi 6.2 has no native CSS variables).
//   - Game-colour overrides: nc_color_to_hex (RmlUi text) consults game_color_hex
//     so menu text uses themed colours instead of the raw curses RGB.
// Scoped to RmlUi; the tile/world render path is untouched.
namespace ui_theme
{
// Load (or reload) data/gui/theme.json. Safe to call before colour load.
void load();

// Replace every {{token}} in `rcss` (in place) with the matching "rcss" hex.
// Unknown tokens become #ff00ffff and log a warning.
void substitute_tokens( std::string &rcss );

// If `c` has a "game_colors" override, write its hex to `out_hex` and return
// true; otherwise return false (caller falls back to the curses RGB).
bool game_color_hex( const nc_color &c, std::string &out_hex );

// ── Live editor support (F4 Theme tab) ────────────────────────────────────
// Ordered token / game-colour names (JSON order) for building the editor UI.
const std::vector<std::string> &rcss_names();
const std::vector<std::string> &game_color_names();
// Get/set a colour as RGBA floats (0..1). get returns false if the name is
// unknown. set updates the in-memory theme; call rmlui_layer::reload_theme()
// after rcss edits to re-apply, and colour edits take effect on next screen
// open (the setters clear the resolved cache).
bool get_rcss_rgba( const std::string &name, float out[4] );
void set_rcss_rgba( const std::string &name, const float in[4] );
bool get_game_rgba( const std::string &name, float out[4] );
void set_game_rgba( const std::string &name, const float in[4] );
// Persist the current theme back to data/gui/theme.json.
void save();
} // namespace ui_theme

#endif // CATA_SRC_UI_THEME_H
