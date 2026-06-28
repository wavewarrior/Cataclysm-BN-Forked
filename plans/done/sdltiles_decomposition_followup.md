# ✅ ARCHIVED 2026-06-28 — fully executed, moved to plans/done/

## STATUS (reviewed 2026-06-27)
**~100% DONE — ARCHIVE.** sdltiles.cpp now **289 lines** (plan started at 2063). All four extractions shipped as files: Step A `sdl_lighting_devui.{h,cpp}`, Step B `sdl_widget_draw.{h,cpp}`, Step C `sdl_window.{h,cpp}`, Step D2 `sdl_render_frame.{h,cpp}` (refresh_display moved out entirely — not in sdltiles.cpp anymore). Final residual landed well below the "300–500 lines" target. Plan matches reality, fully executed — move to `plans/done/`. Out-of-scope items (A2 callback-hook, fontwidth dual-source) remain open but were explicitly deferred.

# sdltiles.cpp — further cleanup (follow-up to sdltiles_decomposition.md)

Sidebar redesign + lighting both declared **settled**, so the earlier "active WIP" deferrals
lift. sdltiles.cpp is 2063 lines after the first decomposition; this sequences the remaining
extractions by dependency + risk. Each step: build-green gate (`cmake --build build -j`, EXIT 0)
+ its own commit. "Settled" removes the merge-churn risk, not the behavior risk — refresh_display
is still the frame heart, so it goes last and carries an in-game eyeball gate.

---

## Step 0 — commit the prior simplify work first (blocker)

Tree currently holds 9 uncommitted `/simplify` files (sdl_display.h extraction, dead-header
deletes, orphan-include removal — build-green). Commit before any new extraction so the
histories don't tangle.

```
git add -A && git commit -m "refactor(sdl): extract sdl_display.h; drop dead headers/includes"
```

✅ DONE — ## Step A — `sdl_lighting_devui.{h,cpp}`   (do first — also a prereq for D)

Move out of sdltiles.cpp: the lighting/debug globals (~535–630: `g_dbg_*`, `g_current_dbg_mode`,
`g_dbg_params`, `g_rc_readback`, `g_vol_*` + `g_vol_params`, `g_emitter_scale`/`g_sun_scale`/
`g_sky_scale`, `menu_emitter_tuning`) + `draw_lighting_dev_ui()` (635–856).

- Clean seam: the panel is already a registered callback (`imgui_layer::set_dev_ui(...)` at :879)
  → register `sdl_lighting_devui::draw` instead.
- Header **declares** the shared state; sdltiles.cpp (refresh_display) and sdl_input.cpp both
  include it. sdl_input.cpp deletes its hand-written `extern` block (~365–390).
- **Why first:** refresh_display reads `g_vol_params`/`g_dbg_*`, so giving them a header home is
  a prerequisite for moving refresh_display out in D.
- **Honest scope:** this is de-extern + state-centralization (~320 lines shed, declared home),
  **not** a full fix of the input→lighting coupling — input still writes these globals, and
  refresh_display still writes `g_vol_params`/`g_rc_readback`. The module owns the *declarations*,
  the dependency stays bidirectional. The callback-hook that would sever it is out of scope.

✅ DONE — ## Step B — `sdl_widget_draw.{h,cpp}`   (independent, low risk)

Move `draw_widget_icon` ×2 + `draw_widget_row_highlight` (441–635, ~194 lines) to a definition
site. Declarations stay in `widget_icon.h` (public API); callers in panels.cpp unchanged — same
pattern sdl_window_dims already uses. Zero lighting coupling; orderable anytime.

✅ DONE — ## Step C — `sdl_window.{h,cpp}`   (window/term lifecycle, medium risk)

Move InitSDL / WinCreate / WinDestroy / handle_resize / resize_term / toggle_fullscreen_window /
init_term_size_and_scaling_factor (+ `catacurses::init_interface` / `endwin`) — ~660 lines.

- Talks to lighting through the **public** render_state API (`lighting::init_render_state_on`,
  `rs.world_target()->resize()` …), not internals — so the cut is at the interface, feasible.
- Retires the window/renderer/format/geometry/WindowWidth reference-aliases (121–156) into this
  module, exactly as the alias-block comment intends ("each later extraction batch-migrates its
  domain's refs and drops the alias").
- Watch: GPU-device init ordering (WinCreate) and the per-target list in handle_resize (it
  enumerates ui/world/shadow/world_ldr/bloom — a mild leak; keep as-is, don't redesign here).

✅ DONE — ## Step D — decompose `refresh_display` (711 lines, the heart — last, highest risk)
> D2 executed: `refresh_display` lives in `sdl_render_frame.{h,cpp}` (45KB), no longer in sdltiles.cpp.

The function is one 711-line block already sectioned by phase comments, threading `(rs, ctx)` +
globals. Confirmed splittable; the fat "assemble frame_light_inputs" phase (~1017–1170, sun/
weather/camera/vol-params) is the cleanest single extract.

- **D1 (do this): split into named static phase-helpers in place** — frame-begin, dirty-gate +
  build/submit, upload drain, RC gather, RC readback, assemble-light-inputs, Pass-W draw,
  post chain (tonemap/bloom/volumetric/blit), UI composite + fonts, dev-UI + present. Each takes
  `(rs, ctx, …)`. Pure readability/altitude win, no behavior change, no new file. Called once/
  frame so helper-call cost is nil.
- **D2 (optional, after A + D1): move the orchestration to `sdl_render_frame.{h,cpp}`** — the big
  line-count reduction. Needs Step A done (globals reachable via header) and D1's clean helpers.
- **Gate:** build green **and** one in-game eyeball (a phase-ordering slip = visual breakage; the
  one risk "settled" doesn't remove). Commit D1 and D2 separately.

---

## Recommended order

`0 → A → B → C → D1 → (D2 optional)`. A before D (header prereq); B is independent (slot anywhere);
C and D are the big reductions. After A–C, sdltiles.cpp is ~900 lines; after D it's roughly the
residual (globals/aliases + tileset glue: load_tileset, rescale_tileset, repoint_overmap_tilecontext,
is_draw_tiles_mode, save_screenshot) — on the order of 300–500 lines.

## Still out of scope

- **A2 callback-hook** (route F-key lighting edits through a registered handler so input stops
  mutating lighting globals) — the real fix for the input↔lighting coupling, larger than
  extraction. Revisit after A if it still grates.
- The pre-existing `fontwidth` vs `font->width` two-sources-of-truth — unrelated.
