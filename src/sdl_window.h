#pragma once

/// Window/terminal lifecycle — extracted from sdltiles.cpp Step C.
///
/// Owns: SDL window creation/destruction, resize, fullscreen toggle,
/// initial terminal sizing, and the catacurses init/endwin lifecycle.
/// Public entry points below are declared from their original headers
/// (sdltiles.h, cursesport.h, cursesdef.h).
