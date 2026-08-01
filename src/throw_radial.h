#pragma once
#ifndef CATA_SRC_THROW_RADIAL_H
#define CATA_SRC_THROW_RADIAL_H

#include <optional>

class avatar;

/// The throw quick-slot radial (ACTION_THROW_QUICKSLOT).
///
/// Six slot cards on a ring around the play area, picked by pointing rather than
/// by reading a list: the pointer's ANGLE from the ring centre selects, so the
/// gesture is a flick in a direction, not a cursor trip to a target. Number keys
/// 1-6 pick directly, ENTER commits the highlight, ESC cancels.
///
/// Two interaction modes, chosen at open time and governed by the
/// `THROW_RADIAL_HOLD` option:
///
///  - HOLD  - the player is still holding the key that opened it, so the ring
///            lives only for the length of that hold and commits whatever is
///            highlighted on release. This is the fast path.
///  - STICKY- the key was already up by the time the ring opened (a tap), or the
///            option is off. The ring stays up until the player commits or
///            cancels.
///
/// Falling back to STICKY on a tap is deliberate rather than a safety net: at a
/// 16 ms tick a quick tap is usually released before the first poll, so a strict
/// hold-mode would flash the ring open and shut with no chance to aim. Tap-to-
/// stick / hold-to-flick is also what players expect from a radial.
///
/// `shown` is false only when the document could not open (RmlUi not ready) — the
/// wheel has no curses twin, so the caller must fall back to a list menu rather
/// than treat that as a cancel. When `shown` is true, an empty `slot` means the
/// player cancelled or committed on an empty wedge.
///
/// The caller owns applying the result; this does not mutate the avatar.
struct throw_radial_result {
    bool shown = false;
    std::optional<int> slot;
};

auto show_throw_quickslot_radial( avatar &u ) -> throw_radial_result;

#endif // CATA_SRC_THROW_RADIAL_H
