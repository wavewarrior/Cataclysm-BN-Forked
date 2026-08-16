#pragma once
#ifndef CATA_SRC_HUD_RUNIC_PANELS_H
#define CATA_SRC_HUD_RUNIC_PANELS_H

#include <string>

#include "hud_runic.h"

class avatar;

/// The two column producers of the sidebar HUD.
///
/// SOMA is the body column — parts, pools, effects. DOCK is the tactical
/// column — current target and arms. Between them they carry every field the
/// old `hud_vitals`, `hud_map` and the target/arms halves of `hud_botbar`
/// carried.
///
/// **Why these live outside `panels.cpp`.** That file owns the HUD *chassis*:
/// the data model, the document lifecycle, sync and geometry. The two densest
/// producers in the HUD would bury it. Both read the avatar and the layout,
/// return RML, and touch no document, no global UI state and no element, which
/// makes them trivially testable and lets the chassis stay a chassis.
///
/// **What they emit.** Flex boxes in the character creator's vocabulary, built
/// only from the `hud_runic` markup primitives: `.hud-row` list rows, the
/// creator's `.nc-fact` / `.nc-pip` / `.nc-chip` / `.nc-tally` devices, and
/// `.nc-rule` separators. No cell grid, no padding to a column width, and no
/// literal alignment space anywhere — under flex layout every gap comes from
/// `margin`/`padding`, and a run consisting of a space is trimmed at parse
/// time regardless of what the element's `white-space` says.
///
/// **No row budget.** Each panel's `.hud-body` is `overflow-y: auto`, so a
/// producer never counts its rows against the region height; content taller
/// than its region scrolls rather than painting across the region below. The
/// sole bound is SOMA's effect roster, capped in the producer, because there
/// scrolling is the wrong answer: an unbounded roster pushes the pools the
/// player reads every turn behind a scrollbar they have no way to operate.
///
/// **Colour.** `hud_runic::ink` and nothing else. No `nc_color`, no inline
/// `style="color:…"`, no second hue. Every distinction is a luminance step, so
/// the stylesheet keeps the palette, the F4 Theme tab keeps editing it live,
/// and desaturating the document stays a no-op on the information. Gold is the
/// stylesheet's alone — the crit row's left edge, a lit pip, a panel head.
///
/// **Case.** Labels are authored in caps at source (`STAM`, `WIELD`, `HP`);
/// data values keep the case they arrive in. An item `tname`, an effect
/// `disp_name`, a monster `disp_name` and an attitude word are all translated
/// prose belonging to somebody else, and the creator's register leaves such
/// prose alone — headings are upper-cased by RCSS `text-transform`, which no
/// locale has to ship a second copy of a string for.

/// Left column: body parts, pools, effects.
///
/// A body part that `hud_runic::is_critical` flags takes the creator's cursor
/// treatment — a gold left edge over a dark fill — and gains a second row of
/// chips naming the reason, so bleeding and bitten land against the limb
/// rather than in a bar at the other end of the screen.
auto hud_soma( avatar &u, const hud_runic::layout &l ) -> std::string;

/// Right column under the radar: mission bearing, current target, arms.
auto hud_dock( avatar &u, const hud_runic::layout &l ) -> std::string;

#endif // CATA_SRC_HUD_RUNIC_PANELS_H
