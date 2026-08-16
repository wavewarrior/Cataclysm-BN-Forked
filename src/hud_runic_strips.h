#pragma once
#ifndef CATA_SRC_HUD_RUNIC_STRIPS_H
#define CATA_SRC_HUD_RUNIC_STRIPS_H

#include <string>
#include <vector>

#include "hud_runic.h"

class avatar;

namespace Messages
{
struct rich_message;
} // namespace Messages

/// Sidebar HUD in the character creator's register: the strip producers.
///
/// Five regions — the two status rows, the message log, the function-key strip
/// and the driving panel — as pure `state -> markup` mappings with no lifetime,
/// no ownership and no document handle. `panels.cpp` keeps the HUD *chassis*
/// (the data model, the document lifecycle, the per-frame sync and the geometry)
/// and calls into producers it cannot accidentally entangle with.
///
/// Every producer builds its markup exclusively out of `hud_runic`'s primitives
/// and colours it exclusively through the `hud_runic::ink` ladder, so no
/// producer here emits an inline colour and none of them can invent a hue the
/// palette does not have.
///
/// **Each takes the whole layout and none of them reads it.** That is not an
/// oversight and the parameter is not dead weight. In the register these
/// replaced, a producer had to know where a *neighbouring* region's border
/// landed, because it drew that border itself as a box-drawing glyph at a
/// counted column. Frames are now CSS borders on the document's own elements
/// and overflow is `.hud-body`'s and `.hud-meta-mid`'s to absorb, so the one
/// thing a producer used the layout for no longer exists — while the layout
/// stays in the signature because it is the chassis' half of the contract, and
/// because a producer that ever again needs a region's real size must get it
/// from the same object every other region was placed from rather than from a
/// second expression that agrees only by coincidence.

/// Status row 1: identity, world clock, place, weather, light and safe mode.
/// The inner markup of one `.hud-meta` row — exactly three `.hud-meta-group`
/// divs, left, middle and right.
auto hud_status_row1( avatar &u, const hud_runic::layout &l ) -> std::string;

/// Status row 2: stats, movement, load, needs and the hostile summary. Same
/// three-group shape as row 1.
auto hud_status_row2( avatar &u, const hud_runic::layout &l ) -> std::string;

/// The message log: one `.hud-row.hud-log-entry` per message, oldest first. The
/// luminance ramp down the rows is the recency curve; the glyph in each row's
/// gutter is its severity. `msgs` is already trimmed to the region's row budget
/// by the caller, which is also what feeds the entry animation, so the two
/// cannot describe different sets.
auto hud_log_rows( const std::vector<Messages::rich_message> &msgs,
                   const hud_runic::layout &l ) -> std::string;

/// The function-key strip: the inner markup of its `.nc-legend` container, one
/// `KEY :: ACTION` item per slot, each saying whether its action is bound and
/// whether it is usable in the current state. Every slot is emitted; a narrow
/// viewport is `.nc-legend`'s `flex-wrap` to handle.
auto hud_keys( avatar &u, const hud_runic::layout &l ) -> std::string;

/// The driving panel's body. Empty unless the avatar is controlling a vehicle.
auto hud_veh_panel( avatar &u, const hud_runic::layout &l ) -> std::string;

#endif // CATA_SRC_HUD_RUNIC_STRIPS_H
