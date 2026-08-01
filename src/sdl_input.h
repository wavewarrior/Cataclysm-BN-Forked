#pragma once

#include "sdl_display.h"

/// Input processing helpers extracted from sdltiles.cpp.
///
/// Every function takes display_context& for the input state it needs
/// (joystick, dpad timing, window for mouse capture).  No function touches
/// renderer/geometry/fonts/tiles — cleanest decomposition seam.
namespace sdl_input
{

/// Poll the gamepad d-pad (joystick hat) and synthesise directional input
/// events.  Handles diagonal delay so quick orthogonal presses produce a
/// diagonal.
/// @return 1 if a d-pad event was synthesised into d.last_input, 0 otherwise.
auto handle_dpad( display_context &d ) -> int;

/// Translate an SDL key press to an ncurses key identifier.
/// @return curses constant (>0), 0 for unknown, or -1 when ALT+number
///         sequence has been started (caller should check add_alt_code).
auto keysym_to_curses( SDL_Keycode sym, SDL_Keymod mod ) -> int;

/// Begin accumulating an ALT+nnnn decimal code (Alt-key down event).
void begin_alt_code();
/// Accumulate one decimal digit into the ALT+nnnn buffer.
/// @return true if the buffer was non-empty and the digit was accumulated.
auto add_alt_code( char c ) -> bool;
/// Finish ALT+nnnn and return the accumulated code (0 if none).
auto end_alt_code() -> int;

/// Arrow-combo diagonal movement helpers (mode1: Ctrl+two-arrow -> numpad).
auto handle_arrow_combo( SDL_Keycode key ) -> int;
void end_arrow_combo();

/// @return true if a joystick/gamepad is available.
auto gamepad_available( const display_context &d ) -> bool;

/// Main event loop — polls SDL events and updates display_context input state.
/// Currently defined in sdltiles.cpp (too coupled to ImGui / debug F-keys /
/// resize to extract fully); this declaration exposes it to the dispatch
/// pipeline below.  Callers always pass g_display.
void CheckMessages( display_context &d );

/// Last cursor position seen in an SDL mouse-motion event, in window pixels.
/// Unlike display_context::last_input.mouse_pos this is NOT reset per event, so it
/// still answers "where is the cursor" inside a keyboard-driven modal loop.
auto last_mouse_px() -> point;

/// Physical right-mouse-button state, tracked from the SDL event stream. Use this
/// instead of SDL_GetMouseState's button mask, which reports nothing unless a
/// window currently holds mouse focus — inside the ranged-targeting modal loop it
/// always came back empty, so hold-to-aim never saw the release.
auto rmb_down() -> bool;

/// How many NON-MODIFIER keys are physically held right now, tracked from the SDL
/// event stream for the same reason as rmb_down(): SDL_GetKeyboardState() only
/// answers while a window holds keyboard focus, so a modal loop cannot use it to
/// notice a release.
///
/// Modifiers are excluded so a hold-to-open UI bound to a combo (CTRL+T) sees the
/// count drop to zero when the *letter* comes up, without having to know which
/// key opened it. Zero means "nothing is held" — i.e. the player tapped rather
/// than held, and a hold-to-close UI should stay open.
auto non_modifier_keys_held() -> int;

} // namespace sdl_input
