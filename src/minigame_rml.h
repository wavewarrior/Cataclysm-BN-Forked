#pragma once
#ifndef CATA_SRC_MINIGAME_RML_H
#define CATA_SRC_MINIGAME_RML_H

// Tier 9 — the shared char-grid RmlUi widget for the 5 grid minigames
// (lightson / snake / sokoban / minesweeper / robot-finds-kitten). They are
// literally grid games, so ONE narrow reusable doc (data/gui/minigame.rml) serves
// all five — title + a monospace coloured-cell grid + a footer (legend/keys).
// This is NOT a general char-grid backend (per the migration strategy); it is
// scoped to the minigames.
//
// Render-behind-frozen-API like every other screen: each game keeps its
// ui_adaptor + input loop and, when minigames_rmlui_enabled() is on, its
// on_redraw feeds this widget cata-colour-tagged strings + sync()s instead of
// drawing curses. Single active minigame at a time (file-static model state +
// the rml_doc single-instance guard keyed on "minigame").

#include <string>
#include <vector>

class input_context;

namespace minigame_rml
{

// Open the shared doc (no-op returning false unless `enabled` and RmlUi is ready).
// `ctxt` is the game's input_context (used for the harness 16ms tick).
bool open( bool enabled, input_context &ctxt );

// True while the shared doc is open (the game gates its curses draw on this).
bool active();

// Tear down the doc + model. Idempotent; the rml_doc dtor is the safety net.
void close();

// This frame's content, as Cataclysm colour-tagged strings ("<color_x>…</color>").
// set_grid: one entry per grid row (rendered monospace, white-space:pre). Call
// set_* then sync() inside on_redraw.
void set_title( const std::string &title );
void set_footer( const std::string &footer );
void set_grid( const std::vector<std::string> &rows );
void sync();

}  // namespace minigame_rml

#endif  // CATA_SRC_MINIGAME_RML_H
