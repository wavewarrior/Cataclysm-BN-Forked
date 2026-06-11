#pragma once
#ifndef CATA_SRC_RML_SCREEN_H
#define CATA_SRC_RML_SCREEN_H

// F.3 — per-screen RmlUi migration support (full UI→RmlUi migration plan).
//
// THIN + PROVISIONAL: this currently holds only the per-screen enable toggles
// (mirroring the uilist/query_popup/string_input toggles in ui.h). A shared
// open/sync/close/tick harness is deliberately NOT extracted yet — one screen
// (missions) is N=1 and the common shape across differently-structured screens
// (list+detail vs tab-page forms vs the continuous sidebar) is not yet visible.
// Extract the harness here once 2-3 dissimilar screens agree on the pattern.
//
// Each migrated screen gets an OFF-by-default toggle so it can be A/B'd in
// isolation from the F4 dev panel; flip the default ON once eyeballed, and the
// whole toggle layer is deleted at the final curses rip-out.

// game::list_missions() RmlUi render path.
bool &missions_rmlui_enabled();

// show_scores_ui() RmlUi render path (achievements/scores/kills tabs).
bool &scores_rmlui_enabled();

// help::display_help() RmlUi render path (topic menu + scrolling article).
bool &help_rmlui_enabled();

#endif // CATA_SRC_RML_SCREEN_H
