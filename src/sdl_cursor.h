#pragma once
#ifndef CATA_SRC_SDL_CURSOR_H
#define CATA_SRC_SDL_CURSOR_H

/// Contextual SDL mouse cursor management (mouse interactivity plan, Step 5).
/// Lazily creates and caches the small set of SDL system cursors the game
/// world uses to hint what's under the pointer (crosshair over a visible
/// monster, a hand over an examine target, the plain arrow otherwise), and
/// skips redundant SDL_SetCursor calls when the requested kind is already
/// active. Deliberately kept out of the larger sdl_window/sdl_input TUs so
/// this small, single-purpose cache doesn't bloat their include graph.

enum class cursor_kind {
    arrow,
    hand,
    crosshair,
    forbidden
};

/// Make `kind` the active OS mouse cursor. Lazily creates the underlying
/// SDL_Cursor on first use (cached thereafter); a no-op when `kind` is
/// already the active cursor. Main-thread only (matches the rest of the SDL
/// input/window code, which is never called off the main thread).
auto set_game_cursor( cursor_kind kind ) -> void;

/// Destroy every cursor cached by set_game_cursor() and forget the current
/// kind. Call once during SDL window teardown (see WinDestroy in
/// sdl_window.cpp); idempotent, and a no-op if none were ever created.
auto destroy_game_cursors() -> void;

#endif // CATA_SRC_SDL_CURSOR_H
