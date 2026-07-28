#pragma once
#ifndef CATA_SRC_CONTEXT_MENU_H
#define CATA_SRC_CONTEXT_MENU_H

#include <optional>
#include <string>
#include <vector>

#include "action.h"
#include "point.h"

/// One selectable row of a floating context menu (see show_context_menu).
struct context_action {
    std::string label;
    std::string hotkey_hint;
    action_id act = ACTION_NULL;
    bool enabled = true;
};

/// Show a floating, modal context menu anchored at `screen_pos` (window-
/// relative pixels, e.g. the mouse position at the moment the menu was
/// requested — see rmlui_layer's mouse pipeline for the coordinate space).
/// Supports mouse (click/hover) and keyboard (UP/DOWN/CONFIRM) selection;
/// dismissed via Escape or a click outside the menu. Rows with
/// `enabled == false` render greyed out and cannot be picked by either input
/// method.
///
/// Returns the chosen action, or std::nullopt if dismissed, if `actions` is
/// empty, or if RmlUi isn't ready to render it (this component is RmlUi-only
/// and has no curses fallback).
auto show_context_menu( const point &screen_pos,
                        const std::vector<context_action> &actions ) -> std::optional<action_id>;

#endif // CATA_SRC_CONTEXT_MENU_H
