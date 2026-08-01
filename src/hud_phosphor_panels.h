#pragma once
#ifndef CATA_SRC_HUD_PHOSPHOR_PANELS_H
#define CATA_SRC_HUD_PHOSPHOR_PANELS_H

#include <string>

#include "hud_phosphor.h"

class avatar;

/// The content producers of the terminal-phosphor sidebar HUD.
///
/// SOMA is the left column — body parts, pools, effects. DOCK is the right
/// column — current target and arms. RADAR sits above DOCK in the same columns
/// and is only a frame here, because its content is drawn on the GPU UI layer.
/// Between them they carry every field the old `hud_vitals`, `hud_map` and the
/// target/arms halves of `hud_botbar` carried, on one cell grid instead of four
/// ad-hoc widgets.
///
/// **Why these live outside `panels.cpp`.** That file is 2711 lines and, after
/// this pass, owns only the HUD *chassis*: the data model, the document
/// lifecycle, sync and geometry. The two densest producers in the HUD would
/// bury it. `AGENTS.md` also asks for a new header of pure functions over
/// growing an existing one, and these qualify — both read the avatar and the
/// layout, return RML, and touch no document, no global UI state and no
/// element. That makes them trivially testable and lets the chassis stay a
/// chassis.
///
/// **What they emit.** A run of `<div class="ph-row">` blocks, one per row,
/// each row exactly as many display cells wide as its region — the bound
/// element in `sidebar_hud.rml` is a plain class-less container, so the
/// producer owns the row structure because only the producer knows the row
/// count. It has to be a block per row: `data-rml` parses the markup against
/// the bound element's *current* computed style, so a single `white-space:
/// pre` box with the rows joined by `\n` loses every newline at parse time and
/// renders as one wrapped paragraph. Width is enforced through
/// `hud_phosphor::pad`, never by counting spaces: a hand-counted row is how
/// `[Unbound globally!]` came to overrun its box by 34 dp.
///
/// **Colour.** `hud_phosphor::tint` and `hud_phosphor::invert`, and nothing
/// else. No `hud_color_to_hex`, no inline `style="color:…"`, no `colorize`, no
/// second hue. The stylesheet owns the palette, so the F4 Theme tab keeps
/// editing it live and desaturating the document stays a no-op on the
/// information. Severity that would reach for red reaches for reverse video or
/// a higher rung instead.
///
/// **Case.** Data values are ALL CAPS; labels are written that way at source.
/// A value that arrives in somebody else's case — an item `tname`, an effect
/// `disp_name`, a monster `disp_name`, an attitude word — is re-cased here with
/// `to_upper_case`, at the point of render and never in the catalogue, so no
/// locale has to ship a second upper-case copy of a string it already has.
/// `to_upper_case` and not a byte loop over `std::toupper`: these are
/// translated strings, and a byte loop shreds every multi-byte glyph in them.

/// Left panel: body parts, pools, effects. Rows are `l.soma.cols` cells wide.
///
/// A body part that `hud_phosphor::is_critical` flags has its whole row
/// inverted and gains a second inverted row naming the reason, so bleeding and
/// bitten land against the limb rather than 891 dp away in the bottom bar.
auto hud_soma( avatar &u, const hud_phosphor::layout &l ) -> std::string;

/// Right panel: current target and arms. Rows are `l.dock.cols` cells wide.
auto hud_dock( avatar &u, const hud_phosphor::layout &l ) -> std::string;

/// The RADAR region's frame. Rows are `l.radar.cols` cells wide and carry no
/// data: the dot field itself is drawn by `hud_radar::draw` on the GPU UI layer.
auto hud_radar_frame( const hud_phosphor::layout &l ) -> std::string;

#endif // CATA_SRC_HUD_PHOSPHOR_PANELS_H
