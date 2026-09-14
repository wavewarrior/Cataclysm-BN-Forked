# HUD Readability — Structural Separation & Interactivity Plan

## Context

The RmlUi HUD has spacing improvements but still lacks readability: (1) passive document gate blocks all mouse events including scroll wheel and F11 debugger, (2) log is not scrollable, (3) minimap is cramped with no breathing room, (4) bottom row still too dense, (5) topbar segments need visual symbols. This plan fixes all 5 issues.

## Approach

### Step 1 — Fix passive event gate (CRITICAL — unblocks everything else)

`src/lighting/rmlui_layer.cpp:584` returns `false` when only passive docs are open, blocking ALL input including scroll wheel and F11 debugger toggle.

**Fix:** Move debug key handling (F9-F12) and mouse wheel processing BEFORE the passive gate at line 584. Keep the click passthrough gated so world mouse still works.

Restructure `process_event()`:
1. Early return on `!g_ready || !any_open()` (line 580) — keep as-is
2. **Debug keys (F9-F12)** — move BEFORE the passive check
3. **Mouse wheel** — process BEFORE the passive check (wheel isn't used for world interaction)
4. Passive gate — keep for everything else (clicks, motion, keyboard)

### Step 2 — Fix log scroll CSS

`#hud-log-body` needs explicit flex sizing to fill the dock's remaining space:
```css
#hud-log-body {
    flex: 1 1 auto;
    overflow-y: auto;
    min-height: 0;
}
```

The minimap dock-section needs `flex: 0 0 auto` to not be squeezed.

### Step 3 — Minimap breathing room

Give the minimap section a minimum height and padding. Reduce the log's dominance by giving the minimap `flex: 0 0 auto` with adequate padding/margin.

### Step 4 — Bottom row spacing

Increase bottom row count from 3→4 to give more room for effects + hotbar.

### Step 5 — Topbar segment symbols

Add Unicode symbols as segment prefixes:
- Identity: `⚔` (character)
- Conditions: `♥` (health/status)
- Stats: `◆` (attributes)
- Time: `☀` (time/place)

## Verification

- F11 opens RmlUi debugger even with only HUD open
- Log scrolls with mouse wheel
- Minimap has padding and doesn't clip
- Bottom bars have room for both lines
- Topbar segments have visual symbol prefixes
