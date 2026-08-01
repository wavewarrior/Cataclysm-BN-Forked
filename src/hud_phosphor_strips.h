#pragma once
#ifndef CATA_SRC_HUD_PHOSPHOR_STRIPS_H
#define CATA_SRC_HUD_PHOSPHOR_STRIPS_H

#include <string>
#include <vector>

#include "hud_phosphor.h"

class avatar;

namespace Messages
{
struct rich_message;
} // namespace Messages

/// Terminal-phosphor sidebar HUD: the strip producers.
///
/// These five regions — the two status rows and the rule that carries them
/// across the screen, the message log, the function-key strip, and the driving
/// panel — used to be file-static lambda soup inside `src/panels.cpp`. They live
/// here instead for three reasons, in ascending order of importance:
///
///  1. `panels.cpp` was 2711 lines. `AGENTS.md` asks for a new header of pure
///     functions rather than another handful bolted onto an existing one.
///  2. Every function below is a pure `state -> markup` mapping with no lifetime,
///     no ownership and no document handle. `panels.cpp` keeps the HUD *chassis*
///     — the data model, the document lifecycle, the per-frame sync and the
///     geometry — and calls into producers it cannot accidentally entangle with.
///  3. The producers needed threshold helpers that `panels.cpp` had as file
///     statics (`temp_color`, the `str_string` family). In this register we want
///     those *bands* but explicitly not their hues, so the copies live beside
///     their only caller instead of forcing a hue-shaped helper to grow a second
///     personality. The originals stay put; the curses screens still need them.
///
/// Every producer takes the whole `hud_phosphor::layout` rather than a width,
/// because several of them place glyphs at columns owned by a *different*
/// region: the status rows and the status rule put verticals and crossings where
/// SOMA's right border and DOCK's left border land, and the function-key rule
/// also closes the message log from below. Passing the layout makes those agree
/// by construction; passing widths would make them agree only for as long as two
/// separately-maintained expressions happened to round the same way.
///
/// Each producer emits rows of exactly its own region's width, in cells, via
/// `hud_phosphor::pad`, and colours them exclusively through
/// `hud_phosphor::tint` / `invert`. No producer here emits an inline colour.

/// Status row 1: identity, world clock, place, weather and safe mode.
/// Verticals at SOMA's right border and DOCK's left border.
auto hud_status_row1( avatar &u, const hud_phosphor::layout &l ) -> std::string;

/// Status row 2: stats, movement, load, needs and the hostile summary.
/// Verticals at the same two columns as row 1.
auto hud_status_row2( avatar &u, const hud_phosphor::layout &l ) -> std::string;

/// The rule under the status rows. Carries BOTH panel titles — `SOMA` and
/// `OVERMAP` — and the two `┼` crossings, so the status strip reads as a header
/// row for the two panels below it rather than as a separate object. The panels
/// themselves draw no top rule.
auto hud_status_rule( const hud_phosphor::layout &l ) -> std::string;

/// The message log: its own titled top rule, then one row per message, newest
/// last. The luminance ramp down the rows IS the phosphor-persistence curve.
auto hud_log_rows( const std::vector<Messages::rich_message> &msgs,
                   const hud_phosphor::layout &l ) -> std::string;

/// The rule above the function keys. One row doing two jobs: it titles the key
/// strip AND closes the message log's box from below.
auto hud_keys_rule( const hud_phosphor::layout &l ) -> std::string;

/// The function-key strip: nine slots, each showing whether its action is bound
/// and whether it is usable in the current state.
auto hud_keys( avatar &u, const hud_phosphor::layout &l ) -> std::string;

/// The driving panel. Empty unless the avatar is controlling a vehicle.
auto hud_veh_panel( avatar &u, const hud_phosphor::layout &l ) -> std::string;

#endif // CATA_SRC_HUD_PHOSPHOR_STRIPS_H
