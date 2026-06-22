#pragma once
#ifndef CATA_SRC_RML_TOGGLE_REGISTRY_H
#define CATA_SRC_RML_TOGGLE_REGISTRY_H

// Tier 10 rip-out prep — the single source of truth for the per-screen RmlUi
// enable toggles (`*_rmlui_enabled()`). Each migrated screen owns a file-static
// `bool` exposed by reference; this registry is the one place that enumerates all
// of them, so the F4 dev panel can bind them in a loop and a single "flip all"
// control can drive the build-blind eyeball A/B pass (flip every screen ON, play,
// compare to OFF) instead of clicking ~47 checkboxes one at a time.
//
// Adding a screen = one row here (and its `bool &foo_rmlui_enabled()` decl in
// rml_screen.h / ui.h). The whole layer is deleted at the curses rip-out.

#include <vector>

// One toggle: the dev-panel/bind name, the accessor returning its live `bool&`,
// and the value it initialises to (so "reset defaults" is exact).
struct rml_toggle {
    const char *name;
    bool &( *accessor )();
    bool default_on;
};

// All per-screen RmlUi toggles, in dev-panel order. Stable for the process.
const std::vector<rml_toggle> &rml_toggle_registry();

// Set every screen toggle to `on` (the eyeball-pass "all ON / all OFF" control).
void rml_toggles_set_all( bool on );

// Restore every screen toggle to its compiled-in default (sidebar_hud ON, rest OFF).
void rml_toggles_reset_defaults();

#endif // CATA_SRC_RML_TOGGLE_REGISTRY_H
