# P6: Screen Migration Completion + Backend Cull

> **▶ NEXT SESSION: jump to the "★★★ RESUME HERE" section at the bottom.** (2026-06-30)
> P5-I complete; P6 plan written. P6-A (unguarded on_redraw callers) is the first blocker.

## Context and ground truth

All 56 RmlUi toggle accessors initialize `static bool enabled = true` at runtime.
The `rml_toggle_registry`'s `false` defaults are stale documentation — they are
reset-to targets for `rml_toggles_reset_defaults()`, not the live initializer values.
Do NOT use the registry column as a migration worklist.

Every screen already renders via RmlUi. What remains is:
1. **Removing dead curses fallback code** — the `if(rml){return;}` guards are in place
   but ~40 files still carry curses draw bodies below them.
2. **Fixing 5 screens** that call curses unconditionally (not behind a guard).
3. **Migrating `catalua_console`** — the one remaining screen with no RmlUi path.
4. **Deleting curses utility functions** with RmlUi replacements already present.
5. **Deleting output.cpp primitives** once callers are gone.
6. **Full backend cull** (draw_window → cursesport → toggle layer).

---

## Census findings (2026-06-30)

### Group A — Blocking: curses called unconditionally (not behind guard)

| File | Location | Issue |
|---|---|---|
| `src/game.cpp` | vehicle list on_redraw ~line 9757 | No `if(rml)` guard; `trim_and_print` calls unconditional |
| `src/editmap.cpp` | on_redraw ~line 299 | Calls `update_view_with_help()` with no guard |
| `src/wisheffect.cpp` | `effect_select_callback::refresh()` ~line 220 | `fold_and_print_from(menu->window)` in uilist callback |
| `src/magic.cpp` | `draw_spellbook_info()` ~line 1990 | `fold_and_print(menu->window)` in uilist callback |
| `src/inventory_ui.cpp` | outside on_redraw ~line 812 | `draw_item_info()` call needs guard check |

### Group B — On_redraw curses body present (guard in place, body not yet deleted)

~40 files confirmed to have curses bodies behind the `if(rml){sync_rml();return;}` guard.
These are mechanical deletions (no RmlUi work needed). See P6-D for full list.

Notable subgroup: `debug.cpp` has `fold_and_print()` inside on_redraw (~line 326).

### Group C — Full migration needed

| File | Status |
|---|---|
| `src/catalua_console.cpp` | 3-pane curses screen (w_console/w_log/w_prompt, ~21 mvwprintz); no RmlUi path |

### Parallel RmlUi utility functions already exist

- `npc::print_info_text()` replaces `npc::print_info(window)`
- `monster::print_info_text()` replaces `monster::print_info(window)`
- `vehicle::part_list_text()` replaces `vehicle::print_part_list(window)`
- `character::print_info_text()` — verify existence before P6-F

---

## Phase P6-A: Fix unguarded on_redraw callers

**Prerequisite for everything else** — these cause curses to run unconditionally.

### A-1: game.cpp vehicle list on_redraw

**File**: `src/game.cpp`

Locate the vehicle list on_redraw (grep for `w_vehicles`). Add a guard at the top:
```cpp
ui.on_redraw([&](const ui_adaptor&) {
    if( rml ) { sync_rml(); return; }
    // ... trim_and_print calls ...
});
```
The existing `sync_rml()` lambda for this screen already populates the data model;
the guard just needs inserting. Re-read before editing to get fresh line numbers.

The `draw_item_info()` call at ~line 10437 (EXAMINE action in look-around main loop,
outside on_redraw) stays for now — handled in P6-C.

**Verify**: build green; open nearby vehicle list — renders correctly.

### A-2: editmap.cpp on_redraw

**File**: `src/editmap.cpp`

The `ui.on_redraw` lambda at ~line 299 calls `update_view_with_help()` unconditionally.
Add a guard:
```cpp
ui.on_redraw([&](const ui_adaptor&) {
    if( editmap_rmlui_enabled() && rml ) { return; }
    update_view_with_help();
});
```
`update_view_with_help()` calls `npc::print_info(w)`, `monster::print_info(w)`,
`character::print_info(w)` with curses windows — those are gated by this on_redraw
guard. The editmap RmlUi path has its own data model via `sync_rml()` / `*_text()`.

**Verify**: build green; open editmap — info panel still shows content for NPCs/monsters.

Build both targets after A-1 and A-2:
```sh
cmake --build out/build/osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles -j8
```

---

## Phase P6-B: Migrate uilist callbacks off curses

These are `uilist_callback` subclasses that write to `menu->window` via `refresh()`.
Since uilist renders via RmlUi, `menu->window` is never displayed — but the curses
write still happens. Fix: gate `refresh()` with `if(menu->rml_session) return;`.

**Invariant before gating any uilist_callback's curses body**: confirm a `draw_rml()`
override exists that renders the same content. If `draw_rml()` is missing, **add it
first** rather than gate (per AGENTS.md "preserve visible content"). Do NOT apply
the gate pattern blindly during the P6-D sweep — a callback without `draw_rml()` would
lose its side-panel content under RmlUi.

B-1 and B-2 are safe because both callbacks already have complete `draw_rml()` overrides:
- `wisheffect.cpp:245` — `draw_rml()` renders the full effect stats panel
- `magic.cpp:2100-2188` — `draw_rml()` renders the full spell stats panel

### B-1: wisheffect.cpp effect_select_callback

**File**: `src/wisheffect.cpp`  
**Location**: `effect_select_callback::refresh(uilist* menu)` (~line 220)  
**draw_rml() present at**: `wisheffect.cpp:~245` — renders effect stats (safe to gate)

```cpp
void effect_select_callback::refresh(uilist* menu) override {
    if( menu->rml_session ) { return; }  // draw_rml() handles this under RmlUi
    fold_and_print_from( menu->window, ... );
    wnoutrefresh( menu->window );
}
```

**Verify**: open wish → apply effect — selector works, description shows in side panel.

### B-2: magic.cpp draw_spellbook_info

**File**: `src/magic.cpp`  
**Location**: `spellcasting_callback::refresh()` calls `draw_spellbook_info(menu)` (~line 2095)  
**draw_rml() present at**: `magic.cpp:~2100-2188` — renders full spell stats (safe to gate)

Gate in `refresh()`:
```cpp
void spellcasting_callback::refresh(uilist* menu) override {
    if( menu->rml_session ) { return; }  // draw_rml() handles this under RmlUi
    draw_spellbook_info( menu );
}
```

**Verify**: open magic casting menu — spells list correctly, spell description appears
in the side panel (rendered by draw_rml(), not refresh()).

Build after B-1 and B-2.

---

## Phase P6-C: Gate remaining draw_item_info calls

### C-1: inventory_ui.cpp

**File**: `src/inventory_ui.cpp`  
**Location**: `draw_item_info()` at ~line 812.

Re-read the surrounding code first — if this is already inside an `else { }` block
after `if(uses_rml() && rml_state_)`, it is already gated (no change needed). If
unconditional, add: `if( !uses_rml() || !rml_state_ ) { draw_item_info(...); }`.

### C-2: game.cpp examine in look-around

**File**: `src/game.cpp`  
**Location**: `draw_item_info()` at ~line 10437 in the look-around EXAMINE action.

Gate: `if( !look_around_rmlui_enabled() || !rml ) { draw_item_info(...); }`

This defers the full RmlUi examine overlay to a follow-up. The gate prevents the
curses window from painting while RmlUi is active.

Build after P6-C.

---

## Phase P6-D: Sweep on_redraw curses fallback bodies (~40 files)

The major mechanical sweep. Run after P6-A and P6-B are green.

For each file: grep for `sync_rml` to find the guard, read from its closing `}` to the
lambda's `} );`, delete any curses code (werase/mvwprintz/fold_and_print/wnoutrefresh).

**Batch D-1** (Tier 1-2 — confirmed partially done, verify):
`help.cpp`, `scores_ui.cpp`, `debug.cpp` (~line 326 fold_and_print in on_redraw),
`mission_ui.cpp`, `options.cpp`, `main_menu.cpp`

**Batch D-2** (Tier 3 — mostly clean, verify):
`inventory_ui.cpp` (on_redraw body, distinct from C-1), `examine_item_menu.cpp`,
`crafting_gui.cpp`, `construction.cpp`

**Batch D-3** (Tier 4-5 — mostly clean, verify):
`newcharacter.cpp`, `trade_win.cpp`, `pickup.cpp`, `ranged.cpp`

**Batch D-4** (Tier 6-7 — mostly clean, verify):
`overmap_ui.cpp`, `messages.cpp`, `morale.cpp`, `martialarts.cpp`, `character_display.cpp`

**Batch D-5** (game.cpp remaining screens + any missed):
After P6-A-1 guard is in place, re-sweep any remaining on_redraw fallbacks in `game.cpp`.

Build after each batch. After D-5:
```sh
cmake --build out/build/osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles -j8
```
**Eyeball**: main menu → options → crafting → inventory → AIM → overmap — no blank panels.

---

## Phase P6-E: catalua_console RmlUi migration

The only remaining screen requiring new RML/RCSS files. The Lua console has a 3-pane
curses screen (output/log/prompt, ~21 mvwprintz) with no RmlUi path. The
`if(rml){return;}` guard at ~line 266 already exists; the data model just needs to be
populated.

### E-1: New RML/RCSS files

Create `data/gui/lua_console.rml` + `data/gui/lua_console.rcss` following the
`data/gui/string_editor.rml` pattern. Structure:
- **Top**: scrollable log pane (past output lines, bound `data-for="row : log_rows"`)
- **Middle**: current output/result pane (bound `data-rml="output_rml"`)
- **Bottom**: prompt line (bound `data-rml="prompt_rml"`)

### E-2: Data model

In `catalua_console.cpp`, add a data model:
```cpp
struct lc_row_model { Rml::String html; };
struct lc_data_t {
    Rml::Vector<lc_row_model> log_rows;
    Rml::String output_rml;
    Rml::String prompt_rml;
    Rml::DataModelHandle handle;
} lc_data;
rml_doc lc_rml;
```

Open after `ui.mark_resize()`:
```cpp
lc_rml.open( lua_console_rmlui_enabled(), "lua_console", *ctxt,
    [&lc_data]( Rml::DataModelConstructor &c ) {
        // register lc_row_model struct + vector
        c.Bind( "log_rows", &lc_data.log_rows );
        c.Bind( "output_rml", &lc_data.output_rml );
        c.Bind( "prompt_rml", &lc_data.prompt_rml );
        lc_data.handle = c.GetModelHandle();
    } );
```

### E-3: Populate in sync_rml_data() call sites (~lines 464, 474)

Fold log buffer lines → `lc_data.log_rows`, format current output → `output_rml`,
build prompt from input state → `prompt_rml`. Call `lc_data.handle.DirtyAllVariables()`.

### E-4: Delete curses body

Once the RmlUi path renders correctly:
- Delete the ~21-line curses body from the on_redraw lambda

**Acceptance**: Lua console opens as RmlUi doc, past commands scroll, input line accepts
text, ESC closes. All existing behavior preserved.

---

## Phase P6-F: Delete curses utility functions

After P6-A gates the editmap calls and P6-D sweeps the on_redraw bodies, the curses
utility functions have zero callers. Verify with `lsp references` before deleting:

1. **`npc::print_info(const catacurses::window&, ...)`** — `src/npc.cpp` ~line 2434
   - Replacement: `npc::print_info_text()` (~line 2531)
2. **`monster::print_info(const catacurses::window&, ...)`** — `src/monster.cpp` ~line 826
   - Replacement: `monster::print_info_text()` (~line 893)
3. **`vehicle::print_part_list(const catacurses::window&, ...)`** — `src/vehicle_display.cpp` ~line 182
   - Replacement: `vehicle::part_list_text()` (~line 277)
4. **`character::print_info(const catacurses::window&, ...)`** — `src/character.cpp` ~line 11630
   - Confirm `print_info_text()` exists before deleting

Delete: declaration from `.h`, definition from `.cpp`. Build after each — linker errors
catch any missed callers.

---

## Phase P6-G: Delete output.cpp text primitives (P5-I-5c)

**Prerequisite**: P6-A through P6-F complete; census confirms zero non-test callers.

Verify:
```sh
grep -rn 'fold_and_print\|trim_and_print\|right_print\|left_print\|center_print\|print_colored_text\|draw_item_info\|scrollable_text' src/ --include='*.cpp' | grep -v 'output\.cpp\|tests/'
```

Zero results → delete each function (declaration in `output.h`, definition in `output.cpp`).
Functions to delete:
- `fold_and_print` (two overloads)
- `fold_and_print_from` (two overloads)
- `trim_and_print` (three overloads)
- `center_print`, `right_print`, `print_colored_text`
- `draw_item_info` (three overloads)
- `scrollable_text` (check callers separately)

Build and verify: fresh binary, all screens render, no blank panels.

---

## Phase P6-H: Backend cull (P5-I-5d–5g)

Only after P6-G confirms output.cpp is clean.

### H-1: draw_window stub (5d)

`draw_window` (static, `src/sdl_curses_draw.cpp` ~lines 88–270) is the general cell
renderer. Has two callers inside `curses_drawwindow()`: minimap path (~453) and general
non-terrain path (~465). Once all non-terrain windows are RmlUi-only:
1. Stub: `static bool draw_window(...) { return false; }`
2. Keep: the w_terrain branch at line ~315 (`tilecontext->draw`) — tile renderer, NOT
   part of the cell renderer
3. Verify tiles render; HUD visible

### H-2: Font::OutputChar + draw_ascii_lines (5e)

Grep for callers (zero once draw_window is a stub). Delete declaration + definition.

### H-3: cursesport.cpp implementations (5f)

For each function: grep for callers outside `cursesport.cpp` and `tests/`. Zero → delete.
Non-zero test-only callers → minimal stub returning an appropriate default.
Keep: `cursesport.h` data structures (`WINDOW`, `cursecell`, `colorpairs`).

### H-4: Toggle layer removal (5g)

1. Delete all `bool& xxx_rmlui_enabled();` declarations from `src/rml_screen.h`
2. Fix each compiler error (remove guard, keep RmlUi path)
3. Delete per-file accessor definitions
4. Delete `src/rml_toggle_registry.cpp` + remove its `#include` references
5. In `data/gui/devui.rml` + `src/devui.cpp`: delete the screen-toggle section

---

## Verification matrix

| Phase | Build check | Eyeball |
|-------|------------|---------|
| P6-A | green | vehicle list renders; editmap info panel shows |
| P6-B | green | wish effect list works; magic casting shows desc |
| P6-C | green | look-around examine works; inventory compare works |
| P6-D | green | all migrated screens render; no blank panels |
| P6-E | green | Lua console opens, scrolls, accepts input |
| P6-F | linker clean | editmap info still shows npc/monster/vehicle info |
| P6-G | linker clean | full game session, no blank screens |
| P6-H | green | tiles render; HUD visible; all menus work |

---

## Reference patterns

- **Guard pattern**: any clean on_redraw lambda — `if(rml){sync_rml();return;}`
- **string_editor RML pattern**: `data/gui/string_editor.rml` + `src/string_editor_window.cpp`
- **uilist callback gate**: `src/advanced_inv.cpp` draw_squares deletion (P5-I-4b)
- **Curses utility delete pattern**: `npc::print_info` → `npc::print_info_text()`
- **Build command**: `cmake --build out/build/osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles -j8`

---

## ★★★ COMPLETED (2026-07-04) ★★★

All phases verified against current tree. Code state matches plan claims.

### Phase completion summary

| Phase | Status | Evidence |
|---|---|---|
| P6-A | ✅ Complete | list_vehicles: `if(rml){sync_rml();return;}` guard at game.cpp:9928; editmap: `update_view_with_help` self-gates via `info_doc_` at editmap.cpp:668 |
| P6-B | ✅ Complete | `effect_select_callback::refresh` and `spellcasting_callback::refresh` bodies deleted (grep: zero matches for `fold_and_print_from.*menu->window` or `draw_spellbook_info`) |
| P6-C | ✅ Complete | `rml_examine_item` at game.cpp:10464 replaces all `draw_item_info` callers; examine_item_menu.cpp:44 confirms migration |
| P6-D | ✅ Complete | on_redraw fallbacks swept across ~40 files. Remaining `scrollable_text` body in output.cpp:362 is expected (function not deleted per P6-G) |
| P6-E | ✅ Complete | `lua_console_rml_session` struct at catalua_console.cpp:100; `rml.open()` at line 300; data model with `log_rows`, `hints_rml`, `prompt` |
| P6-F | ✅ Complete | `Creature::print_info_text()` virtual at creature.h:773; `npc::print_info_text()` at npc.h:713; `monster::print_info_text()` at monster.h:152; `vehicle::part_list_text()` at vehicle.h:510 |
| P6-G | ⚠️ Partial (exhausted) | Deletable set exhausted. Permanent exceptions verified via grep: |
| P6-H | ⛔ Blocked | Achievable scope: zero. Blocked by permanent exceptions. |

### Permanent exceptions (verified via grep)

| Function | Live callers | Reason |
|---|---|---|
| `fold_and_print` | `draw_item_filter_rules` (output.cpp:747) → `clzones.cpp:502` | Item filter rules dialog; live caller chain |
| `trim_and_print` | `center_print` (output.cpp:436) → `draw_border` (output.cpp:543) | Border/title rendering; cascading dependency |
| `center_print` | `draw_border` (output.cpp:543) | Title centering within borders |
| `print_colored_text` | `debug.cpp:293` (crash), `popup.cpp:224` (pre-init), `panels.cpp:319` (HUD), `overmap_ui.cpp:324` | Three permanent exceptions + overmap preview |
| `draw_border` | `character_preview.cpp:448`, `overmap_ui.cpp:317`, `panels.cpp:547`, `popup.cpp:220`, `worldfactory.cpp:1637` | Window chrome; used by permanent exceptions |

### Unblocking P6-H requires (in order)

1. **Startup popups**: wire `popup.cpp::show()` to non-curses renderer (SDL text) OR defer until after `rmlui_layer::init()`
2. **Crash handler**: replace `debug.cpp` on_redraw with SDL_RenderDrawText or similar (works without RmlUi context)
3. **HUD**: port `panels.cpp` sidebar panel system to RmlUi (major project)

### End state

P6-A through P6-F complete. P6-G partial (deletable set exhausted). P6-H blocked.
The curses infrastructure (draw_window, cursesport, toggle layer) remains for the
three pre-init/crash/HUD exceptions.
