# Full UI Migration to RmlUi — Master Plan

> **▶ NEXT SESSION: see `plans/RMLUI_P6_BACKEND_CULL_PLAN.md` — P6 is the active plan.**
> (2026-06-30) P5-I complete (prerequisites + 5a/5b backend sweep). P6 covers: fixing
> 5 unguarded on_redraw callers (P6-A/B/C), sweeping ~40 on_redraw fallback bodies
> (P6-D), migrating catalua_console (P6-E), deleting curses utility functions (P6-F),
> then output.cpp primitives + full backend cull (P6-G/H). Start with P6-A-1:
> `src/game.cpp` vehicle list on_redraw — add `if(rml){sync_rml();return;}` guard.

## STATUS (reviewed 2026-06-27)

**≈95% done. NEARLY A HISTORICAL RECORD — only the backend-deletion tail is live, and it is
BLOCKED by design (not by missing work).** Verified against the clean tree this date.

**What is DONE (confirmed in code/commits):**
- All screen migration (Tiers 0–9) + the §8.1 hidden backlog (char sheet, vehicles, defense
  mode, message log, morale, martialarts, pickup, creature-info hosts, keybindings, colors,
  blood-test, scrollable_text). 49 `*_rmlui_enabled()` toggles in `rml_screen.h`; 74 `data/gui/*.rml`.
- **Toggles flipped ON + eyeball pass COMPLETE** (memory-confirmed 2026-06-23; toggle accessors
  now `static bool enabled = true` despite stale "Default OFF" comments). Do NOT re-open the
  per-screen "eyeball owed / toggle OFF" tags in the prose below — they are HISTORICAL.
- **Sub-series A (ImGui rip-out) COMPLETE** — verified: 0 real `ImGui::` calls (only 2 in
  comments), no `imgui.h` include, `imgui_layer.{cpp,h}` deleted, `imgui` target dropped from
  CMake. Residual `imgui_scroll_to_selected` in ui.cpp/ui.h is a field NAME only.
- **Sub-series B de-curse campaign COMPLETE** (batches 1–13 + 8 clean batches + popup migration,
  through 2026-06-24, commits `7b27..16d4`). The migrated screens' curses *render* bodies are
  deleted; the panels.cpp curses sidebar is ripped out (P1/P2a/P2b). The 52 remaining `if(!rml)`
  arms are now **benign `return;` guards in sync lambdas**, NOT curses-render fallbacks.

**What GENUINELY REMAINS (Tier-10 §8.2 deletion sequence, steps 2–6) — verified NOT done:**
`output.cpp` primitive bodies, `sdl_curses_draw.cpp`, `sdl_font.{cpp,h}`, `sdl_fonts.{cpp,h}`,
`cursesport.cpp`, `rml_screen.cpp`, `rml_toggle_registry.*` ALL still present on disk.
**BLOCKED, not pending:** §8.2 (scope-corrected 2026-06-23) found the curses backend mostly
SURVIVES — the text primitives + glyph path still have LIVE callers in the map/dev path
(`live_view`, `animation`, `scent_map`, `editmap`, `character_preview`, ASCII overmap `draw_ascii`,
`panel_manager::show_adm` sidebar editor, uilist early-init fallback). The realistic endgame is
NOT "delete the curses renderer entirely"; it's deleting only the now-dead UI-screen draw code
(done) and leaving the shared backend. Steps 2–6 wait on those live consumers migrating.

**Active work has moved ON** (camera/minimap/FPS commits sit on top) — this plan is at a stable
rest point. **USEFULNESS: keep as historical record + the §8.2 worklist for the eventual
backend cull.** The only still-actionable items: the D3D12/Win11 no-imgui link build (owed), and
the per-site migrations that unblock §8.2 step 2 (creature-info `print_info`, `show_adm` editor,
uilist custom-draw callbacks). The split-out **NEW-HUD feature plan** (minimap/bodygraph RTT,
panel parity) lives in `RMLUI_HUD_PANEL_REFERENCE.md`, not here.

---

> Goal: migrate **all** player-facing UI off the legacy curses-SDL renderer onto
> RmlUi (RML/RCSS + C++ data-binding), per-screen and semantic, then **delete the
> curses-SDL text renderer and ImGui entirely**. The GPU map/world tile view
> (`w_terrain`/`w_overmap` sprite path) is NOT UI and stays.
>
> This supersedes-in-scope the in-flight player-menu plan
> (`rmlui-migration-remaining-phases.md`), which becomes **Tier 0** here: the
> three generic widgets (uilist / query_popup / string_input) are the reference
> pattern everything else copies.

## Locked decisions (from user)

| Decision | Choice |
|---|---|
| **Strategy** | **Per-screen semantic rewrite.** Each screen gets its own `.rml` + `.rcss` + data-model/binding. No generic char-grid backend for general UI. |
| **Scope** | Modal menus & dialogs (~45) **+** sidebar HUD/panels **+** F4 dev panel (→ RmlUi, retiring ImGui). |
| **Endgame** | **Rip out** the curses-SDL cell/font renderer entirely. No per-screen fallback survives. Every screen proven first. |
| **Minigames** | One **narrow reusable char-grid RML widget** used only by the 5 minigames (they are literally grid games) — contained, not a general backend. |
| **On-map text** | Migrate to an RmlUi-backed overlay. Architected as a **world-space text layer** (see §7) so it stays clean AND is the foundation for future floating damage numbers / world text. |

## ★ ACTUAL FRONTIER — full code audit (2026-06-17)

**The per-tier blocks below LAG the code badly — trust this banner + the ground
truth, not the prose.** GROUND TRUTH = the `*_rmlui_enabled()` toggles in
`src/rml_screen.h` (one per migrated screen) + `data/gui/*.{rml,rcss}` assets + git.
Audit method: cross-referenced all 29 toggles + ~44 doc assets against a CLEAN working
tree (everything committed). Repeatedly the prose said "in flight / deferred /
uncommitted" for screens that are actually DONE + committed (worldfactory was the
tell; the whole Tier-4 giant tier is the same).

**DONE + COMMITTED (29 screens, through Tier 6):**
- Tier 0 generics: uilist / query_popup / string_input (always-on, no toggle).
- Tier 1: missions / scores / help / distraction / auto_note / diary.
- Tier 2 (all 9): mutations / bionics / safemode / auto_pickup / computer /
  construction / crafting / armor_layers / **faction**.
- Tier 3: examine_item / inventory / advanced_inv / compare_items / description_view
  (examine-tile).
- **Tier 4 (ALL FOUR giants): options / worldfactory / main_menu / newcharacter.**
- Tier 5: npctalk (`dialogue`) / trade / vending.
- Tier 6: overmap (+ search) / `world_text` §7 on-map text layer.

**VERIFICATION EVIDENCE (reproduce before doubting any "done" claim):**
`grep -oE '[a-z_]+_rmlui_enabled' src/rml_screen.h | sort -u` → 29 toggles;
`ls data/gui/*.rml` → ~44 docs; `git status --short` → clean (all committed). The
four giants are WIRED, not stub toggles — `grep -c 'rml\.open\|rml_doc ' src/<f>.cpp`:
options=2, **newcharacter=16 (8 slices)**, main_menu=2, worldfactory=4 sessions;
**ranged.cpp=0** (the proof it is genuinely unstarted). Worldfactory commits:
`07d6d6aa53` / `6a7f93a194` / `d689e9acb4` / `2eedac603e`.

**GENUINELY REMAINING (updated 2026-06-18):**
1. ~~**ranged**~~ **DONE + DE-CURSED** — slices 2a (`3109aa26b4`) + 2b/2c (`5a3c772d2d`)
   committed; `ranged_rmlui_enabled()` toggle live. Batch 13 dropped the curses panel_*/
   draw_ui_window; **batch 14b (2026-06-22) deleted the last 4 dead curses readout fns
   (print_steadiness/print_ranged_chance/print_aim/draw_throw_aim, −268 lines), build+link
   green.** The top-banner "ranged.cpp=0" proof is STALE (it predates the slices). Only the
   keystone number-for-number aim A/B eyeball is owed.
2. **Tier 7 — sidebar HUD** — **STRATEGY CHANGED 2026-06-20 (grilled).** Single user, one
   bespoke UI → **faithful curses-repro DROPPED.** Don't reproduce the cell grid; **design the
   target HUD now, AS the migration.** Phase 1 = MVP (the data you can't play without, cheapest
   available source) → suppress curses sidebar → rip out. Phase 2 = design to perfection on a
   curses-free base. Slices 1–2d (9 panels, lifecycle `03bf258feb`..2d) stay as a working
   render path but the **per-panel absolute-positioning + variant machinery is superseded** by a
   **hand-authored single flex-column document** (document flow — kills the height/overlap class).
   Widget engine kept as **data layer + icon registry only** (multi-layout/variant/custom-save
   machinery dropped). See the rewritten Tier 7 strategy section below.
3. ~~**Tier 8 — F4 dev panel**~~ **DONE (eyeball owed)** — full ImGui parity (slices 1–8) +
   **§8 flip committed `8c6a66d7e6` 2026-06-20**: F4 opens the RmlUi `devui.rml` doc; ImGui retired
   from the live composite (`devui_visible()` flag; `imgui_layer::visible()` never set → `active()`
   false). **★ FULL IMGUI RIP-OUT DONE 2026-06-23 (§8.2 sub-series A, `1e5a316369`+`a277c32e68`):**
   devui.rml eyeball-confirmed → stripped all ImGui uses + deleted the `imgui_layer` module + unlinked
   the library; build+link green Metal. ImGui is gone from the build (D3D12 build still owed).
4. ~~**Tier 9 — minigames** char-grid widget.~~ **DONE (eyeball owed)** — all 5 grid games
   (lightson/snake/sokoban/minesweeper/kitten) render through ONE shared char-grid doc
   (`minigame.rml` + `src/minigame_rml.*`), one toggle. Slice 1 `c21e4bb6ae`; slice 2 (other 4) committed.
5. **Tier 10 — RIP OUT** curses-SDL + ImGui — gated on 100% coverage (i.e. 2-4 above).
6. **★ §8.1 GATE-BLOCKER BACKLOG (found 2026-06-20 primitive sweep) — the banner above
   UNDERCOUNTS.** The enumerated tiers 0–9 are code-done-pending-eyeball, BUT the sweep found
   **~12 interactive curses screens never listed in any tier** that still block the rip-out.
   **★ RESUME HERE (2026-06-21 eve): the bespoke-screen backlog is all but cleared.**
   - **DONE + EYEBALL-CONFIRMED CLEAN (user, 2026-06-21 — "all clean"):** `character_display` @ sheet,
     full `messages` log, `morale`, `martialarts`, `pickup`, **`veh_interact`/`vehicle_display` (all 6
     slices: name/mode, stats+fuel, overview, parts, descs, ASCII diagram, install/repair)**, and
     **`gamemode_defense` (both slices: setup form + caravan shop)**. All committed, toggles still OFF
     (the A/B control; the mass flip-ON is the §8 rip-out step). The whole §8.1 bespoke-screen backlog
     is now eyeball-clean on Metal. (D3D12/Win11 glance still nice-to-have but the dynamic-doc path is
     long since proven cross-backend.)
   - **creature-info (track A) — SCOPE COLLAPSED + slice 1 DONE (2026-06-21).** The plan over-framed
     this as a big architectural blob; the audit found the hard part already exists:
     **`Creature::print_info_text()` IS the shared producer** (virtual base empty; `monster`+`npc`
     override — built earlier for ranged), and **mtype static info is already migrated** (it's
     `game::extended_description` = Tier-3 `description_view`). So track A is just wiring the ready
     producer into its 2 player-facing **hosts** (the dev hosts editmap/wish stay):
       - **`list_monsters` (the `m` list) — DONE (build-green Metal, toggle OFF, eyeball owed),
         COMMITTED `95a59a1c24`.** Right-docked panel: header+counter, scrolling list (magenta
         attitude-category headers + creature rows = coloured name + HP-bar/attitude/distance), the
         selected creature's `print_info_text()` info pane, footer (tab+safemode hints). Render-only;
         keyboard owns nav/safemode/look/fire; native scroll; doc hidden during nested look_around.
         `list_monsters_rmlui_enabled()` + F4 "monster list" + devui bind. `data/gui/list_monsters.{rml,rcss}`.
         **WATCH:** long list keyboard cursor has no scroll-into-view yet (native scroll; stage the
         ScrollIntoView one-liner if lost — deferred to avoid the empty-row child DOM-index subtlety).
       - **`look_around` info pane — DONE (build-green Metal, toggle OFF, eyeball owed), COMMITTED
         `845e6498d5`.** Migrated the WHOLE `w_info` tile readout (not just creature): new parallel
         producer `game::print_all_tile_info_text()` mirrors print_all_tile_info's 8 sub-producers
         (visibility/terrain/fields/trap/creature/vehicle/items/graffiti + infrared/specials + sound),
         creature section reusing `print_info_text()` + vehicle reusing `part_list_text()`. Render-only
         doc: centered title + cursor coords + tile-info pane + footer hints (4 scalar binds, no struct).
         Function-scope `rml_doc` (dtor covers look_around's many returns); opened on toggle && show_window;
         map cursor + zone overlay stay on the map path. `look_around_rmlui_enabled()` + F4 "look around"
         + devui bind. `data/gui/look_around.{rml,rcss}`. **WATCH (eyeball, "diff the numbers"):** the
         tile readout is a from-scratch reconstruction across 8 producers — verify terrain (move-cost/
         light/cover/block/features/sign/below), fields, traps, the creature block, vehicle part list,
         item stacks, graffiti, and sounds all match curses; + reduced-visibility (blur/darkness +
         infrared/specials) + D3D12. Curses column alignment dropped (semantic, native scroll).
     **→ TRACK A COMPLETE** (both player-facing hosts migrated; dev hosts editmap/wish stay).
     PLUS the **3 font-layer stragglers** (gate step 4, NOT step 2 — invisible to the primitive grep):
     `loading_ui` splash author text (`draw_sdl_text_outlined`→`draw_string`), `sdl_overmap_draw`
     city/note labels, `sdl_render_frame` dev tile-coord overlay — all §7-class (route to the §7 RmlUi
     text layer, or delete the dev one with the dev tools).
   - (RE-CLASSIFIED as covered-by-toggle, NOT separate screens: `dialogue_win` = Tier-5 `dialogue`
     fallback; `magic` + `magic_teleporter_list` = `uilist`+`draw_rml`, covered by the uilist toggle.)
   See §8.1 for the full table + the corrected 3-part Tier-10 readiness gate. The honest picture now:
   **all enumerated tiers + the unlisted game screens are code-done-pending-eyeball; the rip-out is
   gated on (a) the mass toggle-flip + eyeball pass, (b) the deferred creature-info F.2 component, and
   (c) the 3 font-layer stragglers.**

**EYEBALL DEBT (committed, toggle OFF, user A/B owed):** most of the above DONE set
shipped build-blind. The newest unverified: world_text 4.1/4.2a, description_view,
faction, and the Tier-4 giants (options / newcharacter / main_menu / worldfactory).

So the migration is ~90% by screen count; the remaining effort is **ranged + the
three hard architectural tiers (7/8/9) + the rip-out** — NOT the modal/giant screens
the prose still frames as open.

## Status / progress (2026-06-10)

- **Tier 0 (uilist/query_popup/string_input) — DONE + eyeballed clean.** Toggles
  split into 3 independent F4 checkboxes. The reference pattern HOLDS at runtime.
- **F.0 census — DONE** (no orphan screens; see below).
- **F.1 shared utils — DONE.** `src/rml_util.{h,cpp}` (rml_escape / nc_color_to_hex
  / cata_text_to_rml) promoted out of ui.cpp.
- **F.3 — HARNESS EXTRACTED (`rml_doc`) + first consumer, DONE + EYEBALLED CLEAN,
  COMMITTED `7ae4f90fc5` (with #5).** After 4 screens confirmed
  the lifecycle is byte-for-byte identical, extracted `rml_doc` (new
  `src/rml_screen.cpp`): owns open(enabled,name,ctx,bind)/close()/operator bool/
  document(). open() = single-instance guard (name-keyed `active_models()` set,
  replacing per-screen `g_*_model_active`) → CreateDataModel → run `bind` lambda
  (screen registers structs+Binds+callbacks+captures GetModelHandle) → open
  gui/<name>.rml → 16ms tick; **guard inserted ONLY on full success so every
  failure path stays clean** (null-doc rolls back RemoveDataModel). close() =
  idempotent teardown (RAII dtor = safety net). Header is RmlUi-free
  (forward-decls); screens no longer touch `rmlui_layer::` directly. NO rml_sync
  (sync is irreducibly per-screen). Committed screens NOT retrofitted (plan
  forbids regressing eyeballed screens). N=4→~40 justifies it (centralizes the
  16ms-tick + guard + teardown-ordering correctness bugs).
- **F.2 component library — NOT STARTED** (deliberately: building components
  demand-driven from screens, not speculatively).
- **Tier 1 screen #1: missions (game::list_missions) — DONE + eyeballed clean,
  committed.** `data/gui/missions.{rml,rcss}` + mission_ui.cpp RmlUi path. Proved
  the per-screen pattern on a real list+detail+tabs modal. Two RmlUi-6.2 gotchas
  found & fixed (both documented in uilist.rcss, both bit the first new screen):
  (1) a flex child with overflow needs an explicit `vh` max-height or it collapses;
  (2) `body` needs a definite `height:100%` or `height:%` panels shrink-wrap.
- **Tier 1 screen #2: scores (show_scores_ui) — DONE + eyeballed, committed
  `f1ec226267`.** tabs + single scrolling text pane (vs missions' list+detail).
  Seeded the F.2 scrolling-text-view. show_kills (same file) deferred (near-free
  2nd consumer once pane proven).
- **Tier 1 screen #3: help (help::display_help) — DONE + EYEBALLED CLEAN, COMMITTED
  `79f6bc02f5`.** 2-col grid + Movement ASCII alignment both confirmed in-game.
  `data/gui/help.{rml,rcss}` + help.cpp RmlUi path. One doc, two
  regions toggled by `data-if` (topic menu ↔ scrolling article) mirroring the
  legacy two-phase flow. **Triggered the F.2 scroll-pane extraction** (now N=3):
  shared `.scroll-pane` lifted into theme.rcss; help uses it; missions/scores NOT
  retrofitted (keep their copies, adopt on next touch). Notes: `scrollable_text`
  is a SHARED helper (4 callers iuse/help/game×2) — NOT migrated globally, help got
  its own pane; **§8 sweep must inventory `scrollable_text` too**. hotkey highlight
  via `cata_text_to_rml(shortcut_text(...))`; article pane `white-space:pre-wrap`
  for the Movement ASCII diagram. F4 "help via RmlUi" default OFF.
- **Tier 1 screen #4: distraction_manager (distraction_manager_gui::show) — DONE +
  EYEBALLED CLEAN, COMMITTED `5e39c3f1f0`.** First TOGGLE-LIST
  shape (4th distinct: vs missions list+detail, scores tabs+pane, help menu+article):
  a moving-cursor list whose rows mutate in place (CONFIRM flips Enabled↔Disabled)
  + a description header that re-renders as the cursor moves.
  `data/gui/distraction.{rml,rcss}` + distraction_manager.cpp RmlUi path. KEY
  STRUCTURAL DIFFERENCE vs help: this is **LIVE-SYNC, not sync-once** — copied
  the **missions** `sync_rml()`-inside-`on_redraw` pattern (every
  `ui_manager::redraw()` at loop top rebuilds rows+header from selection → no
  per-branch DirtyVariable; cursor highlight via per-row `selected` bool +
  `.item.selected`; status colour baked into `status_rml` via
  `cata_text_to_rml(colorize())`). Reuses theme `.scroll-pane`/`.item`. F4 toggle
  "distraction mgr via RmlUi" default OFF. New scaffolding: `distraction_rmlui_enabled()`
  + `g_distraction_model_active` guard. **EYEBALL CHECK (user, A/B via F4):** cursor
  moves on UP/DOWN with highlight; CONFIRM flips the row's Enabled(green)↔Disabled(red)
  in place; header description tracks selection; mouse hover/click selects; ESC saves+exits.
- **Tier 1 screen #5: auto_note (auto_note_manager_gui::show) — DONE + EYEBALLED
  CLEAN, COMMITTED `7ae4f90fc5` (with F.3). FIRST `rml_doc` consumer (= the F.3
  harness proof; harness reopen/guard-release verified in-game).** Toggle-list family (cf. distraction) but 3 columns
  (name / coloured map-symbol / yes-no status) + a header showing the global
  AUTO_NOTES option state + an emptyMode hint ("discover more…"). `data/gui/
  auto_note.{rml,rcss}` + auto_note.cpp RmlUi path via `rml_doc rml; rml.open(...)`
  + live `sync_rml()` in on_redraw. **Harness payoff shown:** the screen has an
  early `return` before its old teardown spot — a single safe-anywhere `rml.close()`
  after the loop covers all exit paths (inline teardown would've leaked + stuck the
  guard there). F4 toggle "auto notes via RmlUi" (OFF). **EYEBALL CHECK (user, A/B):**
  cursor moves w/ highlight; E/D/Enter toggle row yes(green)↔no(red) in place; symbol
  column shows the coloured map char; header "Auto notes enabled: True/False" tracks
  the <S>witch option; mouse selects; empty-save case ("discover more" hint when no
  extras discovered) — AND the harness behaves (open once, scroll/tick live, clean
  close, reopen works = guard released).
- **Tier 1 screen #6: diary (diary::show_diary_ui) — CODE-COMPLETE + BUILD-GREEN
  (diary_ui.cpp + devui TUs compile clean), TOGGLE OFF, EYEBALL OWED, UNCOMMITTED.**
  FIRST MULTI-PANE shape (5th distinct family: vs missions list+detail, scores
  tabs+pane, help menu+article, distraction/auto_note toggle-list): THREE
  simultaneous scroll lists (pages / changes / page-text) + title bar + keybinding
  bar + bottom info pane, with a single moving FOCUS — only the active pane shows
  its selected-row highlight (mirrors legacy `active` flag on print_list_scrollable
  that gated the hilite). `data/gui/diary.{rml,rcss}` + diary_ui.cpp RmlUi path via
  `rml_doc rml; rml.open(...)` + 2nd `rml_doc` consumer. KEY STRUCTURAL POINTS: (1)
  4 separate `ui_adaptor`s — `if(rml)return;` early-out added to all 4 on_redraws;
  sync_rml() lives in ONLY ui_diary's (invalidated every loop iter) so the doc
  syncs once/frame, other 3 cheap-return. (2) Rows use a custom `.diary-line` (NOT
  theme `.item`, whose `.selected` is always accent) so the selected accent is
  scoped under `.diary-pane.active` → inactive panes show no cursor, matching curses.
  (3) Decorative ASCII book border (draw_diary_border) DROPPED — semantic rewrite
  keeps meaning not art. (4) page text split into lines via `foldstring(...,1e6)`
  (split on \n, no hard wrap) + CSS `white-space:pre-wrap` for visual wrap. Mouse
  on_pages/on_changes/on_text focus the pane + move its cursor; 16ms tick rebuilds.
  F4 toggle "diary via RmlUi" (OFF). **EYEBALL CHECK (user, A/B via F4):** LEFT/RIGHT
  moves focus (active pane border brightens to accent); UP/DOWN moves cursor in the
  active pane (highlight ONLY there); page list ↔ change list ↔ text track selection;
  bottom info shows the selected change's description when changes/text focused;
  mouse hover/click focuses a pane + selects; New page / Edit (CONFIRM→nested curses
  editor, see caveat) / Delete / Export still work; ESC exits. **TWO EDGE CHECKS
  (advisor — not obvious):** (a) EMPTY DIARY (brand-new character, zero pages) —
  same getters the curses path survived, but the likeliest crash/garbage case; open
  it on a fresh save. (b) MOUSE-vs-KEYBOARD FOCUS FIGHT (new to multi-pane, mouseover
  steals `currwin`) — keyboard LEFT/RIGHT focus while the mouse sits PARKED over a
  different pane; if focus snaps back to the parked pane, switch the panes from
  mouseover→click-only. **KNOWN INTERACTION
  TO JUDGE:** CONFIRM opens the legacy `string_editor_window` (curses, NOT migrated)
  over the still-rendered RmlUi diary doc — editor composites on top; usable but the
  diary text shows behind it. Acceptable per per-screen migration (nested editor is
  its own screen); flag if it reads badly.
  - **REGRESSION FOUND + FIXED + RE-EYEBALLED CLEAN, COMMITTED `21c2664314`:** the
    prior "eyeballed clean" was editor-focused and MISSED that the pages/changes/
    text lists were flowing HORIZONTALLY — same `display: inline` default bug as
    mutations (`.diary-line` had no `display`, `.diary-pane` is a `.scroll-pane`
    block container). Fixed `.diary-line { display: block }`; gotcha documented in
    theme.rcss.
- **Tier 1 screen: loading_ui — BESPOKE DOC BUILT 2026-06-22 (was done-by-coverage).**
  `loading_ui::menu` IS a `uilist` (loading_ui.cpp:476), so it already rode the Tier-0
  uilist toggle (generic list-box look). Now upgraded to a dedicated `gui/loading.rml`:
  context title + a progress bar (done/total) + the step list with per-row
  done(✓/green)/current(▶/glow)/pending(•/dim) state. **Non-modal** like the sidebar
  HUD (no input_context / loop) — `loading_doc_open/sync/close` (file-local in
  loading_ui.cpp, NOT `rml_doc`, which bundles a modal input tick); lazy open, sync each
  `loading_ui::show()`, close on dtor. The `uilist menu` STAYS the state holder
  (entries / selected / green-done); `loading_doc_sync` reads it → clean toggle fallback.
  `passive=true` (render-only). Toggle `loading_rmlui_enabled()` (default OFF, F4 "loading
  screen"). Falls back to `menu->show()` when OFF **or** RmlUi not yet ready (early data
  load, before the first refresh_display inits the context) — so the earliest loads are
  curses, later loads are the bespoke doc. Build-green osx-arm-slim 2026-06-22; eyeball +
  D3D12 owed. The other half of the file (`loading_image_splash`) is the SDL splash IMAGE
  + author-text overlay (`background_pane`/RenderCopy) — still out of scope (the §8.1
  font straggler, resolved at rip-out); the bespoke list renders on top of it.
- **Tier 1 "screen": mod_manager_ui — NOT A SCREEN (mislabel). NO MIGRATION.**
  Read in full: `mod_manager_ui.cpp` is the `mod_ui` LOGIC class — try_add /
  try_rem / try_shift / can_shift_up/down (dependency + conflict resolution) plus
  `get_information()`, a colour-tagged info-STRING builder. Zero draw code (no
  mvwprintz / on_redraw / curses window — that's why the grep found none). The
  actual interactive mod-selection SCREEN (`draw_mod_list`, the available/active
  panes, the info popup that consumes `get_information`) lives entirely in
  **worldfactory.cpp** (worldfactory.cpp:551/715/1035/1080/1084/1213) → it migrates
  as part of **Tier 4 (worldfactory)**. `get_information()`'s colour string flows
  through `cata_text_to_rml` when that host screen migrates; the logic file is
  untouched. **Consequence: there is NO cheap non-conforming proxy in Tier 1 — the
  first non-conforming-screen cost is only paid on a Tier-4/5/7 giant, which is
  exactly what the gate decides.**
- **★ RE-SCOPE GATE — REACHED (2026-06-11). Tier 1 COMPLETE.** Real screens done +
  eyeballed: missions / scores / help / distraction / auto_note / diary (6).
  loading = covered by Tier-0 uilist (no bespoke work). mod_manager_ui = logic, not
  a screen (surfaces at Tier 4). **Measured per-screen cost (harness-amortized,
  post-Foundation), all CONFORMING modal screens:** scores 223 ins/5 files · help
  341/6 · distraction 228/5 · auto_note(+F.3 harness) 405/6 · diary(multi-pane)
  409/6 → steady-state ≈ **220–410 insertions, 5–6 files** each (1 cpp path-edit +
  .rml + .rcss + header toggle + F4 checkbox), plus build-blind + one user A/B
  eyeball cycle. **GATE CAVEAT (carry into the decision):** every datapoint is a
  harness-conforming open→sync→close modal; the remaining ~39 screens include the
  Tiers 4/5/7 giants (worldfactory, options ~172K, newcharacter ~177K, npctalk
  ~170K, sidebar HUD) which are tab-forms / live-overlay / continuous-HUD — a
  DIFFERENT shape the conforming sample cannot price. Any effort estimate for the
  giants is optimism-biased until one is actually cut. **DECISION IS THE USER'S:**
  go at current scope / re-scope (lighter treatment or defer the giants) / adjust
  cadence. **DECIDED 2026-06-11: GO AT FULL SCOPE — rip-out is the goal.** User
  committed to all ~45 screens incl. the giants (multi-month), because only 100%
  coverage unlocks the curses+ImGui deletion. Sequence stays conforming-first
  (Tier 2 → giants → Tier 10 rip-out): clear the priced Tier-2 screens to mature
  the F.2 component library, THEN cut the giants with a battle-tested library, at
  the established one-screen-at-a-time eyeball cadence. **ACTIVE PHASE: Tier 2.**

### Tier 2 progress

- **Tier 2 screen #1: mutations (show_mutations_ui) — DONE + EYEBALLED CLEAN,
  COMMITTED `c93c05b550`** (layout fix `21c2664314`-class included; diary inline
  fix `21c2664314`). TOGGLE OFF.
  First Tier-2 screen; 3rd `rml_doc` consumer. TWO-COLUMN GRID shape (passive | active
  mutations), each row "<key> <name>" with legacy per-state colour baked into the
  bound string via cata_text_to_rml (passive base-trait cyan/light-cyan; active
  powered green/red, base/light variants) + active rows append cost/cooldown/Active.
  Single shared cursor moves within the FOCUSED column (tab_mode); selected-row
  highlight scoped under `.mut-pane.active` so the unfocused column shows no cursor
  (reuses diary's active-pane pattern; the legacy `c_*_yellow` highlight variants
  are dropped in favour of the CSS .selected accent). `data/gui/mutations.{rml,rcss}`
  + mutation_ui.cpp RmlUi path via `rml_doc rml; rml.open(...)` + sync in on_redraw.
  STRUCTURAL POINTS: (1) extracted `mutation_titlebar_desc(menu_mode,ctxt)` so the
  RmlUi titlebar + the curses `show_mutations_titlebar` share ONE text builder (no
  drift). (2) menu modes (examine/reassign/hide) + activation stay on input_context
  (keyboard owns) — the model only reflects the titlebar text + examine pane; the
  reassign/activate popups are already Tier-0 query_popup. (3) RmlUi renders ALL
  rows + scrolls natively → legacy scroll_position windowing is curses-only, not
  ported. (4) column heads "Passive"/"Active" are BOUND+translated+coloured (better
  than auto_note's hardcoded-English heads). F4 toggle "mutations via RmlUi" (OFF).
  **EYEBALL CHECK (user, A/B via F4):** two columns populated, colours match curses
  (cyan passive, green=powered/red=off active); NEXT_TAB switches focused column
  (border→accent), cursor highlight only in focused column; UP/DOWN moves cursor;
  TOGGLE_EXAMINE shows the examine pane with the mutation desc + titlebar says
  "Examining"; REASSIGN→letter popup works; activate (CONFIRM / invlet) still
  activates+exits; mouse hover/click focuses a column + selects + sets examine.
  **WATCH (advisor-class):** (a) LONG mutation list — keyboard UP/DOWN past the
  viewport does NOT auto-scroll the selected row into view (RmlUi native scroll
  only; matches auto_note, but mutations can be long → flag if cursor gets lost).
  **REMEDY PRE-STAGED (advisor — auto_note precedent is weak here: a mutated
  character runs 20-40 traits vs auto_note's short list, and legacy ALWAYS kept the
  cursor in view, so this is a more real regression):** if the user reports
  cursor-lost on a long list, add `doc->GetElementById(<selected row>)->
  ScrollIntoView()` in sync_rml (or scroll the focused `.mut-pane` to its selected
  child). Don't add preemptively; it's the known one-liner fix.
  (b) empty column shows translated "None". (c) invlet keys still work while the
  RmlUi doc is up (keyboard fall-through).
  - **LAYOUT FIX after 1st eyeball (2026-06-11):** rows were flowing HORIZONTALLY
    (one wrapped paragraph) — root cause: RmlUi elements default to `display:
    inline`; the `div{display:block}` default lives in RmlUi's SAMPLE rml.rcss
    which we don't link, and `.mut-pane` is a `.scroll-pane` block container (not a
    flex-column), so the rows need their own `display`. Fixed `.mut-line {
    display: block }`. ALSO per user request the rows are now an unordered list:
    "• " prefix on every row, "> " on the current item of the FOCUSED column
    (baked into the bound text in sync_rml — RCSS has no ::before/content). Gotcha
    documented in theme.rcss so it can't recur. **RE-EYEBALL after this fix.**
  - **D3D12 (Win11) FIRST-OF-TIER CHECK — CONFIRMED CLEAN (2026-06-11, user).**
    Tier-2 RmlUi docs render perfectly on Win11/D3D12 (the primary target), not
    just Metal. The "Metal-green ≠ D3D12-green for new dynamic docs" risk is
    RETIRED for the Tier-2 screen shape (dynamic per-screen data-model docs over
    the shared Rml context). Tier 2 is now structurally proven on the primary
    target; later tiers' first screens still warrant a glance but the core dynamic-
    doc path is validated cross-backend.

- **Tier 2 screen #2: bionics (show_bionics_ui) — CODE-COMPLETE + BUILD-GREEN
  (bionics_ui.cpp + devui compile clean), TOGGLE OFF, EYEBALL OWED, UNCOMMITTED.**
  4th `rml_doc` consumer; **first user of the shared `.tabs`/`.tab` component**
  (LIFTED into theme.rcss at this 3rd tab use, after missions + scores shipped
  their own — they keep their copies, adopt on next touch). Shape = TAB BAR
  (ACTIVE/PASSIVE) over a SINGLE list of the current tab's bionics + an examine
  pane (right, examine mode only) + a titlebar (power + fuel + mode hints). Single
  visible list → selected accent + "> "/"• " marker just track the cursor (no
  focused-column gating). `data/gui/bionics.{rml,rcss}` + bionics_ui.cpp RmlUi path.
  STRUCTURAL POINTS: (1) extracted 3 titlebar TEXT builders (`bionics_fuel_text` /
  `bionics_power_markup` / `bionics_hints_text`) shared by the RmlUi title + the
  curses `draw_bionics_titlebar` (which keeps its own border-glyph drawing — text
  only, per advisor: the curses fn interleaves glyphs with text so only the text
  was lifted). (2) Row text reuses `build_bionic_powerdesc_string` +
  `get_bionic_text_color(bio,false)` (base colour; CSS does highlight). (3) the
  `hide` flag (transient during a bionic that spends moves / targets) is honoured
  by toggling the doc's `visibility` property (curses just early-returns; RmlUi
  would leave a stale overlay otherwise). (4) early `return` (moves spent) is
  covered by the rml_doc dtor (rml declared after data). (5) SORT only sets
  `uistate.bionic_sort_mode` and the active/passive vectors are built once at
  entry → neither curses nor RmlUi reorders live (behaviour-identical, verified).
  (6) CBM-slot connector graphic (CBM_SLOTS_ENABLED, default off) DROPPED — semantic
  rewrite (like diary's ASCII border). F4 toggle "bionics via RmlUi" (OFF).
  **EYEBALL CHECK (user, A/B via F4):** ACTIVE/PASSIVE tabs (counts correct), click
  or NEXT_TAB switches the visible list + bold-marks the tab; rows show "<key>
  <name>, <power props>" with colours matching curses (ON/OFF, fuel-saving, hidden,
  etc.); "> " on the cursor row + "• " elsewhere; UP/DOWN moves cursor; titlebar
  shows Bionic Power + Available Fuel + mode hints; TOGGLE_EXAMINE → examine pane
  with name/power-usage/description; REASSIGN letter popup; activate (Enter/invlet)
  works (incl. a targeting bionic → screen hides cleanly, no stale overlay); mouse
  hover/click selects; empty tab shows the right "No … installed" message.
  **WATCH:** long bionic list → no auto-scroll-into-view (same as mutations; remedy
  = the staged ScrollIntoView one-liner if cursor-lost).

- **Tier 2 screen #3: safemode (safemode::show) — CODE-COMPLETE + BUILD-GREEN
  (safemode_ui.cpp + devui compile clean), TOGGLE OFF, EYEBALL OWED, UNCOMMITTED.**
  5th `rml_doc` consumer; 2nd `.tabs`/`.tab` user. HARDEST Tier-2 so far: a
  5-column rules table (#/Rule/Attitude/Dist/B-W/Cat) with Global/Character tabs
  and a **2D cursor (line + column)** — the active CELL is highlighted, not the
  whole row. `data/gui/safemode.{rml,rcss}` + safemode_ui.cpp RmlUi path. KEY
  POINT: **all editing stays keyboard + the migrated string_input_popup** — the
  RmlUi doc is RENDER-ONLY (table + cell highlight + status); add/remove/copy/
  move/enable/disable/edit/test/switch-option are untouched in the loop. Per-cell
  colour baked via cata_text_to_rml (rule.active → white/grey); the active cell is
  marked by `row.sel_col` (== column on the cursor line, else -1). Hotkey hints
  reuse `shortcut_text` (same as help). Column headers are literal English (same
  i18n gap as auto_note — §8/F.1 sweep). F4 toggle "safemode via RmlUi" (OFF).
  **EYEBALL CHECK (user, A/B via F4):** Global/Character tabs switch the table
  (bold-marks active tab); rows show #/rule/attitude/dist/B-W/category with
  inactive rules greyed; **the single active CELL highlights and moves with
  LEFT/RIGHT/UP/DOWN** (this is the make-or-break check); Add/Remove/Copy/Move/
  Enable/Disable/Test + Enter-edit (string_input_popup) all still work and the
  table updates; "Safe Mode enabled: True/False" + <S>witch reflect the option;
  empty/char-no-name states show the right message; mouse hover/click selects a row.
  **RISK (advisor-class, the one novel bit): the per-cell highlight uses an RmlUi
  data-binding comparison `row.sel_col == N` in `data-class`.** If RmlUi 6.2
  doesn't evaluate `== <int>` in a data-class expression, NO cell will highlight
  (rows still render fine). FALLBACK if so: give each row 5 explicit bools
  (cell0_sel..cell4_sel) instead of the int compare. Flag on eyeball if the cell
  highlight is missing/stuck.
  **(Tier-2 #4 auto_pickup is the near-free twin once this is confirmed — same
  tabbed rules-table shape.)**
  - **SAFEMODE EYEBALLED CLEAN, COMMITTED `06b16872aa`** (the `row.sel_col == N`
    cell-highlight data-expression WORKS in RmlUi 6.2 — proven here, de-risks
    auto_pickup's identical approach; no bool fallback needed).

- **Tier 2 screen #4: auto_pickup (auto_pickup user_interface::show) —
  CODE-COMPLETE + BUILD-GREEN (auto_pickup.cpp + devui compile clean), TOGGLE OFF,
  EYEBALL OWED, UNCOMMITTED.** 6th `rml_doc` consumer; near-free twin of safemode.
  DYNAMIC tabs (Global/Character) rendered as a BOUND VECTOR `data-for` over the
  shared theme `.tabs/.tab` (first dynamic-count tab bar — vs bionics/safemode's
  fixed 2). 2-column rules table (Rule / I/E) with the same 2D-cursor cell
  highlight (`row.sel_col == 1|2`, proven by safemode). `data/gui/autopickup.{rml,
  rcss}` + auto_pickup.cpp RmlUi path. STRUCTURAL NOTE: on_redraw is registered
  ABOVE the input_context in this file, so the rml decls + sync_rml lambda sit
  before on_redraw and `rml.open()` runs AFTER ctxt is built (open needs ctxt for
  the 16ms tick; sync_rml needs no ctxt). All editing stays keyboard +
  string_input_popup; render-only doc; `bActive`→grey; hints via `shortcut_text`;
  column heads literal English (i18n gap, §8/F.1). F4 toggle "auto pickup via
  RmlUi" (OFF). **EYEBALL CHECK (user, A/B via F4):** tabs (Global/Character if a
  character is loaded) switch the table + bold-mark active; rows #/rule/I-E with
  inactive rules greyed; active CELL highlights + moves with LEFT/RIGHT (Rule↔I/E)
  + UP/DOWN; Add/Remove/Copy/Move/Enable/Disable/Test + Enter-edit popup work;
  "Auto pickup enabled: T/F" + <S>witch; mouse hover/click selects a row.

- **Tier 2 screen #5: computer terminal (computer_session::use) — CODE-COMPLETE +
  BUILD-GREEN (computer_session.cpp + devui compile clean), TOGGLE OFF, EYEBALL
  OWED, UNCOMMITTED.** 7th `rml_doc` consumer; the genuinely-clean one (advisor-
  picked over faction). The terminal is a scrolling text PANE backed by the
  existing `lines` buffer (`vector<pair<int,string>>` = indent + colour-tagged
  text that print_* append and refresh() draws). RmlUi path renders that buffer:
  one `.comp-line` per entry, indent→left-pad, colour via cata_text_to_rml
  (untagged → terminal-green default matching the legacy c_green base). `data/gui/
  computer.{rml,rcss}`. STRUCTURAL NOTES: (1) the root menu (uilist) + prompts
  (query_any/ynq/bool) are ALREADY Tier-0 → the terminal doc is a persistent
  BACKDROP they render over; no bespoke menu work. (2) use() has NO input_context
  of its own (input is on the menu/prompts) → a throwaway `term_ctxt` is passed to
  open() only for the harness tick; the RmlUi context is ticked each frame by the
  menu/prompt loops. (3) ~10 early returns (login failures) → teardown rides the
  rml_doc DESTRUCTOR (data declared before rml); no explicit close(). Render-only.
  F4 toggle "computer terminal via RmlUi" (OFF). **EYEBALL CHECK (user, A/B via
  F4):** use a computer (console) in-game — terminal text shows green, errors red,
  gibberish yellow; print_text body lines indented; the root menu (uilist) + the
  "Press any key" prompts appear over the terminal; multi-step actions (hack,
  open/lock, sample, etc.) show their output accumulating; scrolls when long.
  **WATCH:** the default green is CSS `#5fdd5f` (approximation of c_green) — if it
  reads off vs the curses terminal, tweak `.comp-screen color` in computer.rcss.

- **Tier 2 screen #6: construction (construction_menu) — CODE-COMPLETE +
  BUILD-GREEN (construction.cpp + devui compile clean), TOGGLE OFF, EYEBALL OWED,
  UNCOMMITTED.** 8th `rml_doc` consumer; LARGEST screen so far (2497-line file).
  Category tabs (dynamic Vector, shared theme `.tabs/.tab`) + construct list (left)
  + detail pane (right) rendering the existing `full_construct_buffer` (folded
  colour-tagged lines built by `recalc_buffer` from `requirement_data::get_folded_*`
  — confirming the "requirement pane" is just the lines-pane, no new widget).
  `data/gui/construction.{rml,rcss}`. STRUCTURAL POINTS: (1) curses tab-overflow
  windowing + detail breakpoint-paging DROPPED for native scroll — but PAGE_UP/DOWN
  are repurposed (rml mode) to `SetScrollTop` on `#construct-detail` so they aren't
  dead keys (advisor catch; cf. scores). UP/DOWN still move the list cursor. (2)
  list colour via `construction_color(group,false)` + CSS `.selected`; favorites
  keep the `* ` prefix. (3) separator lines in the buffer → a thin `.cm-line.sep`
  rule. (4) speed%/status hints + notes reproduced as bound strings. (5) sync
  guards `select` bounds (sync may read `constructs[select]` before the loop's
  own reset — advisor). (6) loop is exit-flag-only → `close()` before `return ret`.
  All build/editing + the FILTER popup (string_input_popup, Tier-0) stay in the
  loop; render-only doc. F4 toggle "construction via RmlUi" (OFF). **EYEBALL CHECK
  (user, A/B via F4) — hit the edge states, not just the happy path (advisor):**
  (a) normal: tabs switch category, list coloured by buildability + `*` favorites,
  detail shows stages/components/tools/skills/time matching curses, UP/DOWN move
  cursor, **PAGE_UP/DOWN scroll the detail** (the make-or-break key check); (b)
  EMPTY category (a tab with 0 constructs — no crash, blank list/detail); (c)
  FILTER mode (press filter → tab becomes "Searched", single tab); (d) FAVORITES
  (toggle ★ → `*` prefix + the FAVORITE tab lists it); (e) a LONG multi-stage
  construction (detail overflows → scrolls). CONFIRM builds as before; mouse
  hover/click selects a row, click a tab switches category. **WATCH:** pre-folded
  buffer is folded to the old curses width (cosmetic; lines may not fill the RmlUi
  pane width — left as-is per advisor).

- **Tier 2 screen #7: crafting (select_crafting_recipe) — DONE + EYEBALLED CLEAN
  ("all works", user 2026-06-11), COMMITTED `8f7ca0d0e6`. TOGGLE OFF** (matches all
  prior Tier-2 — mass flip-ON deferred to §8; toggle is the A/B control).** 9th
  `rml_doc` consumer; PEER of construction (the cadence
  note's "tabs+list+lines", budgeted as its own ~2000-line screen). HEAVIEST Tier-2
  yet — diverges from the construction template in 4 ways (all advisor-flagged
  pre-write): (1) **TWO tab rows** (category + subcategory), both string
  `list_circularizer`s NOT int-indexed → `selected` is string-equality vs
  `tab.cur()`/`subtab.cur()`; clicks map index→string via `set_index()` then (cat
  click) rebuild the subtab circularizer + `recalc=true`, mirroring PREV/NEXT_TAB.
  (2) **Dual lockstep scroll** — PAGE_UP/DOWN + SCROLL_RECIPE_INFO_UP/DOWN scroll
  BOTH `#recipe-info` and `#item-info` (a `rml_scroll_info(dir)` lambda doing
  SetScrollTop on both, alongside the curses recipe_info_scroll/item_info_scroll).
  (3) **Three TAB_MODEs** (NORMAL/FILTERED/BATCH) — cat bar shows categories OR a
  single "Searched"/"Batch" tab; subcat row hidden (data-if `show_subcats`) +
  tab-clicks disabled outside NORMAL; batch rows are "Nx recipe". (4) **Heavier
  sync** — no prebuilt buffer; sync_rml computes the middle pane each frame via
  `cached_recipe_info(...fold_width=46...)` (non-nested only) and the right pane
  (wide mode) via either a nested-category text block OR `format_item_info` of the
  result item split through `foldstring`. `data/gui/crafting.{rml,rcss}` +
  crafting_gui.cpp RmlUi path. STRUCTURAL POINTS: (a) the RIGHT item-info pane is
  **AD-HOC TEXT (format_item_info → cata_text_to_rml lines), PENDING the Tier-3
  item-info F.2 component** — gets crafting off curses now (what §8 rip-out needs);
  the reusable-component adoption is a later refactor, NOT a migration blocker (§8
  sweep must NOT assume this pane already uses the component). (b) can-craft
  indicator + hidden-recipe amount reproduced as bound colour strings (mirrors
  draw_can_craft_indicator / draw_hidden_amount; curses keeps its own right_print
  positioning). (c) keybinding tips → bound footer string. (d) `on_select` sets
  `line` only (no update flag — inherits construction's proven click→redraw→sync).
  (e) single loop exit → `rml.close()` before `return chosen`. F4 toggle "crafting
  via RmlUi" (OFF). **EYEBALL CHECK (user, A/B via F4) — exercise the NOVEL surface,
  not the happy path (this screen exceeds anything prior):** (1) **★ LONG-LIST
  KEYBOARD CURSOR** (crafting has the longest lists, 30-80 recipes — cursor leaves
  viewport on UP/DOWN with NO auto-scroll; advisor: near-CERTAIN regression here).
  REMEDY (staged, NOT shipped blind — has RmlUi-6.2 data-for-child-index + DOM-
  update-timing subtlety I can't verify build-blind): set a `scroll_to_sel` flag in
  the **keyboard UP/DOWN handlers ONLY** (never on_select — mouse select must not
  force-scroll), and in sync_rml ScrollIntoView the selected list child + clear the
  flag. Do NOT ship the unconditional one-liner (it fights native scroll/wheel).
  (2) click a CATEGORY tab → subtab resets + list rebuilds; (3) BATCH mode (single
  "Batch" tab, "Nx recipe" rows, no subtabs, cat-click disabled); (4) FILTER mode
  ("Searched"); (5) enter a NESTED category + back — **A/B the nested right pane
  specifically: it's a RECONSTRUCTION (the original ≈1016-1059 block kept coming
  back stale; identifiers grep-confirmed but exact rendering unseen) MISSING the
  "Origin:" + subcategory lines** (shows name/Category/description/Known-recipes
  only); (6) PAGE_UP/DOWN scrolls BOTH info panes; (7) click-to-select moves
  highlight + info (the one place diverged from the template without a flag).
  **KNOWN FIDELITY GAPS (flag for §8, harmless):** subcategory unread "⁺" markers
  added to cats only (not subcats); `empty` bound+dirtied but unreferenced in RML
  (empty category renders blank, no message). **WATCH:** middle pane pre-folded to
  curses width 46 (cosmetic, may not fill the RmlUi pane — left as-is per the
  construction precedent).

- **Tier 2 screen #8: armor_layers (show_armor_layers_ui) — CODE-COMPLETE +
  BUILD-GREEN (armor_layers.cpp + character_display.cpp + devui compile + LINK
  clean), TOGGLE OFF, EYEBALL OWED, UNCOMMITTED.** 10th `rml_doc` consumer;
  BIGGEST Tier-2 yet — a **4-pane render-only doc** (cat header / left worn-list +
  Total-Protection / mid item-detail + encumbrance-warmth table / right
  per-bodypart layering). First screen with a "drag/move mode" — but that's
  KEYBOARD (MOVE_ARMOR picks an item up, UP/DOWN swap it); RmlUi just renders the
  picked-item marker, so it's still render-behind-frozen-API, NOT a new mouse-drag
  interaction. `data/gui/sortarmor.{rml,rcss}` + armor_layers.cpp RmlUi path.
  STRUCTURAL POINTS: (1) **TWO contained reconstructions reuse helpers WITHOUT
  touching the curses paths** (kept pristine for the A/B, advisor-chosen over
  editing shared/curses code build-blind): `mid_pane_lines()` (new, this file,
  parallel to `draw_mid_pane`, reuses the in-TU clothing_*/penalty helpers) and
  **`character_display::encumbrance_lines()`** (NEW non-invasive fn in
  character_display.cpp — reuses that TU's file-local statics list_and_combine_bps/
  encumb_color/temperature_print_rescaling/get_temp_conv; `print_encumbrance`
  UNTOUCHED so the '@' char sheet stays byte-identical; **converge when the char
  sheet migrates, Tier 7**). (2) all actions (move/equip/remove/change-side/hide/
  sort/assign-invlets/help) stay on input_context + popups; render-only doc.
  (3) **mouse `on_left` is GATED to `selected < 0`** (NOT move-mode) — else a click
  would jump the cursor without the keyboard swap, desyncing the cursor vs the
  picked-item marker (advisor catch; the crafting on_select pattern is wrong here).
  (4) `<< / >>` cat arrows are clickable (on_prev_bp/on_next_bp mirror LEFT/RIGHT:
  cycle tabindex + reset leftListIndex/offset/selected); bodypart cycler stays a
  header, NOT a 14-tab bar (faithful to curses). (5) curses windowing
  (leftListOffset / right-list offset / breakpoint) DROPPED for native scroll.
  (6) multiple early `return`s (npc / activity) → rml_doc DESTRUCTOR tears down (no
  explicit close). (7) static labels (Innermost/Outermost/Storage/Encumbrance and
  Warmth) literal English (i18n gap, §8/F.1 sweep). F4 toggle "armor layers via
  RmlUi" (OFF). **EYEBALL CHECK (user, A/B via F4) — DIFF THE NUMBERS, not just the
  layout (advisor: silent wrong-number bugs live in the reconstructions, none
  crash):** (1) **★ ENCUMBRANCE/WARMTH pane** — every bodypart row's "enc+penalty
  (warmth)" matches the curses table EXACTLY (this is the #1 from-scratch
  reconstruction); covered-by-selected-item bps turn green. (2) right-pane per-bp
  layering: bodypart MERGING reads right ("Arms" combined vs "L. Arm"/"R. Arm"
  split) + per-item encumber numbers match. (3) left list: stacking-penalty
  colours, "H" hidden badge, storage volume, cursor highlight. (4) move-mode:
  MOVE_ARMOR picks an item (pink marker), UP/DOWN reorder it, and a **mouse click
  during move-mode does NOT move the cursor** (the gated on_left); ESC/MOVE_ARMOR
  drops it. (5) Total Protection block shows on a specific bodypart (Bash/Cut/Stab/
  Ballistic), hidden on "All". (6) `<< / >>` clicks + LEFT/RIGHT both cycle
  bodypart. (7) mid pane shows item detail / "Nothing to see here!" when empty.
  **WATCH:** mid-pane + right-pane pre-folded to fixed widths (cosmetic, native
  pre-wrap re-wraps); `items_cover_bp` recomputed a few times per frame (matches
  curses, not a perf concern at this list size).

- **★ TIER-2 CADENCE NOTE (advisor, 2026-06-11):** the 5 Tier-2 screens shipped so
  far (mutations/bionics/safemode/auto_pickup/computer) were CLEAN COMPOSITIONS of
  proven primitives (lists/tabs/colored-rows/text-pane). The REMAINING Tier-2 are
  a heavier class — **component-builds / interface-refactors**, not quick
  compositions. **CORRECTION (post-construction):** the "construction + crafting
  share a requirement-data pane, build once serves both" framing was WEAKER than it
  sounded — the sharing is at the C++ `requirement_data::get_folded_*` level (which
  already exists), and the RmlUi render is just the trivial lines-pane. So
  **crafting_gui is a PEER ~2000-line full tabs+list+lines migration, not a cheap
  follow-on** — construction is its template, but budget it as its own screen.
  **faction** is
  DEFERRED: its detail panes are the Tier-3-era `Creature::print_info`/npc-info
  F.2 component (should ride that, not precede it) AND its detail render returns
  interactability flags the input loop consumes → needs an interface refactor of
  `npc::faction_display` (split draw from state). **armor_layers** has drag-reorder
  (new interaction). So: the quick-win streak is over; the next unit is real F.2
  component work. Recommended order: requirement-pane (construction+crafting) →
  armor_layers → faction (after creature/npc-info F.2 lands).
  - **THE DECIDING FACT (rip-out reality, was missing):** the payoff is §8 Tier-10
    rip-out (delete curses + ImGui), which is ALL-OR-NOTHING — it needs EVERY
    screen migrated. So deferring/skipping the giants means the rip-out never
    fires and BOTH renderers stay compiled + live: that is MORE maintenance surface
    than today (an RmlUi path was ADDED alongside curses, not replacing it). The
    giants are not optional polish — they are load-bearing for the only payoff. The
    measured 220–410 LOC/screen is the cheap, de-risked part; the real question is
    binary: **is the user in for finishing the giants (multi-month), or not?**
  - **Remainder bucketed by risk (~39 screens):**
    - **PRICED** (Tier-1 data extrapolates — same open→sync→close modal shape):
      Tier 2 (bionics / mutation / safemode / auto_pickup / construction /
      armor_layers / computer_session / faction / crafting), most of Tier 3,
      Tier 6 static labels, Tier 9 minigames → ~25–30 screens at known cadence.
    - **UNPRICED structural risk** (different shape, no sample): Tiers 4/5/7 giants
      (worldfactory / options / newcharacter / npctalk / ranged) + advanced_inv +
      sidebar HUD. Risk is concentrated HERE, not across the whole remainder.
  - **RECOMMENDATION (if committed to rip-out):** conforming-first — clear Tier 2
    (also matures the F.2 component library the giants will need), THEN re-gate on
    the giants with a battle-tested library. Tier 2 is the "keep moving while you
    decide the giants" path: priced, builds the library, commits to nothing. Do
    NOT solo-charge worldfactory. If multi-month giant work is in doubt, decide
    THAT now — every conforming screen added while the giants stay on curses is
    effort toward an endgame that won't arrive.

### Tier 2 status (2026-06-17): COMPLETE (all 9, faction included)

9 of 9 Tier-2 screens DONE: mutations / bionics / safemode / auto_pickup /
computer terminal / construction / crafting (eyeballed) / armor_layers / **faction**.

- **Tier 2 screen #9: faction manager (faction_manager::display) — CODE-COMPLETE +
  BUILD-GREEN (faction.cpp.o 08:33 / mtype.cpp.o 08:36 / npc.cpp.o 08:37 all newer
  than source; binary relinked 08:39:47, fresh mtime), TOGGLE OFF, EYEBALL OWED,
  COMMITTED `a37612b35a`.** The long-deferred Tier-2 screen, unblocked by doing the refactor +
  the npc-info text inline (the "creature/npc-info F.2 component" the deferral waited
  on turned out to split: the `extended_description()` half shipped as Tier-3 #2's
  examine screen; this screen needs the COMPACT `*_faction_display` text, produced
  here as parallel `faction_info_text()` methods). 4-tab (Followers / Other factions
  / Lore / Creatures) list+detail screen — bionics shape. `data/gui/faction.{rml,rcss}`.
  STRUCTURAL POINTS: (1) **the flagged refactor, done:** `npc::faction_display`
  computed `retval` (radio/interaction-range flags the input loop consumes) DURING the
  curses draw → extracted `npc::follower_interaction_flag()` and now set the flags in
  the input LOOP (after `guy = followers[selection]`), so CONFIRM/SWAPTONPC keep
  working when the RmlUi path skips the draw. The curses draw still draws (its retval
  capture dropped, flags now loop-set) — behaviour-preserving. (2) **3 parallel text
  producers** (`npc::faction_info_text` / `faction::faction_info_text` /
  `mtype::faction_info_text`) reproduce the per-tab detail as colour-tagged strings;
  the curses `*_faction_display` draw fns are LEFT PRISTINE (armor_layers/
  character_display precedent — duplicated content, byte-faithful A/B). (3) lore tab =
  the snippet translated string directly. (4) render-only doc; keyboard owns nav +
  CONFIRM/SWAPTONPC; rml_data before rml for teardown. (5) **latent curses bug found,
  NOT fixed (surgical):** `npc::faction_display` indexes `skill_strs[1]/[2]`
  unconditionally → would crash a follower with <3 non-combat skills; the new
  `faction_info_text` GUARDS it (emits only present entries). F4 toggle "faction
  manager" (OFF). **EYEBALL CHECK (user, A/B via F4) — DIFF THE TEXT per tab (the
  producers are from-scratch reconstructions):** (1) **★ FOLLOWERS** — the detail pane
  matches curses (Attitude/Status/Condition/Hunger/Thirst/Fatigue/Wielding/skills + the
  can-see line: "Within interaction range" / radio variants) AND **CONFIRM (talk) +
  SWAPTONPC still work** (the refactor's make-or-break: the flag is loop-set now).
  (2) OTHER FACTIONS — attitude/strength/desc. (3) LORE — snippet text. (4) CREATURES
  — symbol+name list, detail = difficulty/origin/size/species/senses/abilities/desc.
  (5) tabs switch (NEXT_TAB / click), list cursor moves, empty tabs show the right
  "You have no…" message. **WATCH:** long lists → no auto-scroll-into-view (same as
  prior screens); creature/faction detail pre-folded width is gone (native wrap).

### Tier 3 progress (item-info family)

- **F.2 item-info component — LANDED (additive), COMMITTED.** `rml_util::
  item_info_rml_lines(item_info_data&)` → `format_item_info` → `foldstring(s,1e5)`
  → `cata_text_to_rml` per line (the exact shape crafting's ad-hoc right pane
  already runs). Additive: touches no existing code; the shared `draw_item_info`
  curses core (output.cpp:954, one flag-driven loop behind ~10 callsites) is
  UNTOUCHED. Host screens migrate render-behind and feed their `item_info_data`
  here. **Structural finding:** there is NO separable "modal overload" — all 3
  `draw_item_info` overloads funnel into the one core loop, so full-screen modal
  callers (no own ui_adaptor) can't be migrated without touching the shared core.
  The clean consumers are screens that own a ui_adaptor + embed
  `draw_item_info(without_getch)`.
- **Tier 3 screen #1: examine_item_menu (examine_item_menu::run) — CODE-COMPLETE +
  BUILD-GREEN (examine_item_menu.cpp + rml_util + devui compile + LINK clean),
  TOGGLE OFF, EYEBALL OWED, UNCOMMITTED.** 11th `rml_doc` consumer; FIRST consumer
  of the F.2 item-info component AND the **FIRST RmlUi modal opened NESTED over a
  still-active curses parent** (the inventory/pickup it launches from). One
  full-screen doc: title (item name) + left action list (rows from
  `action_list.entries` — hotkey + txt + text_color, cursor) + right item-info pane
  (#examine-info, the F.2 lines). `data/gui/examineitem.{rml,rcss}`. STRUCTURAL
  POINTS: (1) the legacy is a contextual SIDE PANEL beside the parent; this is a
  full-screen doc that COVERS the parent (CONSCIOUS simplification — item already
  chosen; flagged for eyeball). (2) shared `draw_item_info` core + the action
  uilist UNTOUCHED (curses path unchanged). (3) keyboard owns action select/run
  (existing loop: UP/DOWN + CONFIRM/hotkey); mouse `on_action` SELECTS only (run
  via Enter — click-to-run not supported, per the mouse-selects/keyboard-acts
  invariant). (4) PAGE_UP/DOWN scroll #examine-info (SetScrollTop). (5) single
  exit → explicit `rml.close()`. F4 toggle "examine item via RmlUi" (OFF).
  **EYEBALL CHECK (user, A/B via F4):** (1) **★ NESTED-MODAL (the novel surface):**
  open examine-item FROM the inventory (examine an item) — the RmlUi doc renders
  cleanly over the parent, and the inventory is INTACT after close (this is the
  first nested RmlUi-over-curses modal). (2) item-info content matches curses
  (props/protection/flags/description, colours). (3) action list shows hotkey +
  name with hint colours; UP/DOWN move cursor, Enter/hotkey runs the action; mouse
  click selects (then Enter runs). (4) PAGE_UP/DOWN scroll the info pane.
  **UNPROVEN:** `item_compare` is empty here → `format_item_info`'s +/- compare-
  delta colouring is NOT exercised (the first COMPARING consumer earns that later).

- **Tier 3 screen #2: examine-tile description view (game::extended_description) —
  CODE-COMPLETE + BUILD-GREEN (descriptions.cpp.o 07:58:48 newer than source 07:57:06;
  binary relinked 07:59:47, fresh mtime), TOGGLE OFF, EYEBALL OWED, COMMITTED
  `a42d96a8e9`.** The "examine surroundings → describe this tile" screen ([c] creatures / [f]
  furniture / [t] terrain / close). **SCOPE CLARIFICATION (research finding — the
  cadence note's "creature/npc-info F.2 component" splits in two):** this screen uses
  `Creature/furn/ter::extended_description()` — a finished `colorize()` STRING — so it
  migrates trivially via the EXISTING primitives (`.scroll-pane` + `cata_text_to_rml`),
  NO new rml_util component. The COMPACT look-around side panel + faction use the
  harder positional `print_info(catacurses::window&, vStart, vLines, column)` (HP bar +
  right-aligned attitude + paginated) — a DIFFERENT shape that needs a parallel
  `_lines()` extraction and rides its host screens (look-around giant / faction), NOT
  this unit. So this lands the EXAMINE half cleanly; the compact-pane component is
  still owed for faction. `data/gui/descriptionview.{rml,rcss}` + descriptions.cpp
  RmlUi path. STRUCTURAL POINTS: (1) extracted `build_hint()` + `build_desc()` lambdas
  so the curses path and the RmlUi sync render ONE source (no drift); on_redraw
  branches `if(rml){sync_rml();return;}` else curses. (2) the WHOLE `desc` runs through
  `cata_text_to_rml` in one pass → correct colour-tag matching across newlines + `\n`→
  `<br/>`, bound as a single `body_rml` string (no per-line split → no mid-tag cut
  risk; simpler than computer's line vector — plain `Bind`, no RegisterStruct/Array).
  (3) render-only doc; keyboard owns CREATURE/FURNITURE/TERRAIN/QUIT; the screen's own
  `ctxt` is passed to open() for the tick (standard live-screen sharing, cf. mutations
  /distraction). (4) `rml_data` declared before `rml` so the doc tears down while the
  bound buffer is alive; single loop → rml_doc dtor handles teardown. F4 toggle
  "examine description" (OFF). **EYEBALL CHECK (user, A/B via F4):** (1) examine a tile
  (the look-around "describe" key) — creature/furniture/terrain text matches curses
  (colours, layout, multi-line), [c]/[f]/[t] switch the target, the hint bar shows the
  keybinds, QUIT/close exits; long descriptions scroll. (2) signage tiles show the
  "Sign: …" line (and "Sign: ???" when illiterate). (3) debug `display_mod_source`/
  `display_object_ids` Origin/[id] lines render if those debug flags are on. **WATCH:**
  the doc covers the parent look-around view (full-screen, like examine_item_menu's
  conscious simplification) — confirm the parent is intact after close.

### Tier 3: inventory framework (inventory_ui) — MULTI-SESSION SUB-PROJECT (user GO, 2026-06-12)

**Why it's different from every prior unit:** `inventory_ui.cpp` (2640) is NOT a
screen — it's the `inventory_selector` FRAMEWORK. One shared
`refresh_window()` (inventory_ui.cpp:1636 = frame + `draw_header` + `draw_columns`
+ `draw_footer`) is the single render for ALL SIX selector subclasses
(`inventory_pick_selector` / `inventory_multiselector` / `inventory_compare_selector`
/ `inventory_iuse_selector` / `inventory_drop_selector` / `inventory_pickup_selector`),
each with its own `execute()` loop (`ui_manager::redraw()` → base `on_redraw`
[1471] → `refresh_window`). It **gates the whole inventory family**: `game_inventory`
has NO independent render (it just builds a selector + calls `execute()`), and
advanced_inv is the multi-pane variant — neither moves until this framework does.
Data model = the most complex in the migration: multi-COLUMN cell-grid
(`inventory_column::draw` [924] + the preset's `get_cells_count`/`get_cell_text`
interface + category headers + invlets + multiselect marks), NOT a list/tab/pane.

**Cadence-preserving gate (advisor's key move):** do NOT migrate all 6 at once.
Gate the RmlUi path in `refresh_window` per SELECTOR SUBCLASS — `inventory_rmlui_enabled()`
(global toggle) **AND** a per-subclass flag (a virtual `use_rml()` or a member each
ctor sets). Light ONE subclass at a time; the other 5 stay curses even with the
toggle on. This restores the exact toggle→eyeball-ONE→commit→light-next rhythm.

**Slice decomposition (each its own commit + eyeball; increasing complexity):**
1. **Slice 1 — `inventory_pick_selector` only** (the simplest, single-select). Regular
   columns + simplest preset + header (title/hint) + footer. The F.2-style RmlUi doc
   (`inventory.{rml,rcss}`, "inventory" model): header text, a column of entry rows
   (invlet + cell text, category headers, selected highlight), footer (mode hint).
   The shared cell/preset model is built + proven HERE. DEFER everything below.
2. **Slice 2** — stats header (`display_stats` / `get_stats()` right-aligned block).
3. **Slice 3** — `inventory_multiselector` mechanism (the `selection_column` +
   multiselect marks + `query_count`). NOTE: `inventory_multiselector` is never
   instantiated directly (always a subclass), and "marks + query_count" needs a
   count-bearing selector — so the vehicle is **`inventory_drop_selector`** (moved
   up from slice 5; exercises all three named items + is easy to open).
4. **Slice 4** — `inventory_compare_selector` (two-selection state).
5. **Slice 5** — `inventory_iuse_selector` / `inventory_pickup_selector`
   (count/stats state; `drop` already lit in slice 3).
6. **Slice 6** — multi-column layout fidelity + filter popup coexistence
   (string_input already Tier-0) + nav modes.
Only after all slices ON + eyeballed does the family (game_inventory, then
advanced_inv) follow.

**SLICE 1 — DONE + EYEBALLED CLEAN (user 2026-06-15; foundation confirmed —
reopen-test + rows + colours all good), TOGGLE OFF, COMMITTED (`31d524a9db` +
`bd913226e0`).** Mechanism shipped:
(1) **per-subclass gate** — `virtual inventory_selector::uses_rml()` (default false;
private virtual, override-able), overridden by `inventory_pick_selector::uses_rml()`
→ `inventory_rmlui_enabled()`. The other 5 selectors stay curses even with the
toggle on. (2) **pImpl** `inventory_rml_state` (rml_doc + Rml model) defined in the
.cpp, forward-declared in the header → RmlUi stays out of inventory_ui.h. (3)
`refresh_window()` (the shared render) gains `if(uses_rml()&&rml_state_){rml_sync();
return;}` at the top — one guard covers all 6 selectors, lit one at a time. (4)
doc opened once in `create_or_get_ui_adaptor` (toggle read there); torn down by the
pImpl's rml_doc dtor in `~inventory_selector`. (5) `inventory_column::rml_rows()`
(new public method) mirrors `draw()` + REUSES the per-entry cell cache
(`get_entry_cell_cache`) → category rows (cache.color header) + item rows (invlet +
joined cell text, denial→grey+red). (6) `rml_sync()` builds title/hint/footer +
FLATTENS visible columns into one row list. `data/gui/inventory.{rml,rcss}` model
"inventory". F4 toggle "inventory (pick) via RmlUi" (OFF). **DEFERRED to later
slices (NOT bugs):** stats header (slice 2), true multi-column side-by-side
(flattened for now), MOUSE-select (keyboard-only this slice — with curses not drawn
the legacy screen-coord mouse mapping is dead in RmlUi mode; a row-click callback
comes in a later slice), multiselect/compare/count/drop. **EYEBALL CHECK (user, A/B via F4):**
(0) **★ REOPEN (advisor's #1 — structural, recurs in every later slice):** open a
pick inventory, close it (QUIT), open it AGAIN in the same session — the 2nd open
must render rows, NOT empty. The model-name "inventory" is shared + the rml_doc
single-instance guard is the known "type-register reuse / per-instance name" risk
in this plan; if open #2 is blank, the guard is biting. (1) single-select inventory
(an `i`nspect/activate picker using inventory_pick_selector): items list with invlet
+ item SYMBOL glyph + colours matching curses, category headers, cursor row
highlighted, UP/DOWN/PAGE move cursor, title/hint/footer present, CONFIRM picks +
QUIT cancels. (2) the OTHER inventory screens (drop/multidrop/compare/etc.) MUST
still be curses (gate proof). **POST-COMMIT FIXES (advisor, applied before handoff):**
item symbol glyph now included (identifying content, not cosmetic); denied items
show only cell[0] + red reason (matches curses `count=1`, avoids layout collision).
**WATCH:** flattened columns (character vs map items stack instead of side-by-side —
expected this slice); keyboard-only (mouse won't select — expected).

**SLICE 2 — stats header — DONE + EYEBALLED CLEAN (user 2026-06-15, "looks great"),
COMMITTED `739ca9b6e8`. TOGGLE OFF.** Adds the weight/volume stats block
to the slice-1 doc. Additive + small: (1) model gains a 2nd `Rml::Vector<inv_rml_row_model>
stats` (REUSES the slice-1 row struct for its `text_rml` — no new registered type),
bound in `rml_open`. (2) `rml_sync` fills it from `get_stats()` under the same
`display_stats` guard the curses `draw_header` uses; each line already carries
per-segment colour tags so it's wrapped in the `c_dark_gray` base via
`cata_text_to_rml(colorize(elem,c_dark_gray))` — the exact title/hint/footer idiom
+ DirtyVariable("stats"). Empty vector when `display_stats` off → right block
collapses. (3) `inventory.rml`: header split into `.inv-header` (flex row) =
`.inv-header-text` (title+hint, left) + `.inv-stats` (`data-for s : stats`
`data-rml="s.text_rml"`, right). (4) `inventory.rcss`: `.inv-header` is the
space-between flex row and now OWNS the full-width separator border (moved off
`.inv-hint`, so it spans like the curses hline); `.inv-stats` is a right-aligned
(`align-items:flex-end`) flex column; `.inv-stat` is `display:block white-space:pre
text-align:right`. `display_stats=true` by default + pick selector doesn't override
`get_raw_stats` → stats ALWAYS show on the pick path (slice 2 is testable). **EYEBALL
CHECK (user, A/B via F4):** open a pick inventory — weight + volume stats appear
TOP-RIGHT of the header (right-aligned), values coloured (red when over capacity,
light-gray normal), matching the curses right_print block; title/hint still top-left;
the separator line spans the full header width; list/footer unchanged from slice 1.
**WATCH:** stats are space-padded internally for cell alignment (`get_stats()` pads to
max cell width) → `white-space:pre` preserves it; if the two lines don't column-align
vs curses, the monospace assumption broke.

**SLICE 2b — side-by-side columns (multi-column layout, pulled forward from slice 6)
— CODE-COMPLETE + BUILD-GREEN (inventory_ui.cpp compile + LINK clean, fresh obj+bin
mtime), TOGGLE OFF, EYEBALL OWED, UNCOMMITTED.** The slice-2 eyeball surfaced that
flattened columns read WRONG: "ITEMS WORN" (a separate `inventory_column`) stacked
BELOW the main list instead of beside it (the slice-1 flatten). Fix = render each
visible `inventory_column` as its own side-by-side column. **KEY DECISION — no nested
data-for:** RmlUi 6.2's truly-nested `data-for="row : col.rows"` (member array on a
loop alias) is UNPROVEN in this codebase + unverifiable build-blind (every migrated
screen iterates flat vectors; the DataBinding unit test only proves single-struct
`data-for="arrays.a"`, not array-of-struct-with-array). So each column's rows are
BAKED into one finished-markup string (`<div class="inv-row [category|selected]">…
</div>` per row) and the RML is a FLAT `data-for="col : columns" data-rml="col.html"`
— the proven single-level primitive (crafting/sortarmor info-lines). `data-rml`→
`DataViewRml::SetInnerRML` parses the string through the normal RML parser, so the
`.inv-row`/`.category`/`.selected` CLASS rules still apply (verified in vendored
DataViewDefault.cpp). New `inv_col_model{Rml::String html}` + registered array
(`inv_rml_row_model` kept for stats). `.inv-list` becomes a flex ROW
(`align-items:flex-start`); `.inv-col` is `flex:0 0 auto` content-width + 24dp gap;
scroll-pane `overflow-y:auto` still scrolls the row vertically as a whole. **TRADE-OFF
(acceptable, documented):** per-sync each column does a full `SetInnerRML` re-parse
(vs slice-1's data-for array diff) — heavier per cursor-move but fine at inventory
sizes; the selected-highlight now spans the COLUMN content width (was full pane),
which matches curses better. **EYEBALL CHECK (user, A/B via F4):** worn items now sit
in a SEPARATE column to the RIGHT of the main inventory list (not below); category
headers + cursor highlight + colours intact within each column; both columns
top-align; the list scrolls vertically when tall. **WATCH:** (a) >2 columns (map/multi
selectors come in later slices, but if any pick path has 3 columns they should all
sit side-by-side, sizing to content — flag if they overflow/clip horizontally, since
scroll-pane only sets overflow-y). (b) a column whose rows are wider than its share
→ horizontal clip (no overflow-x set; left as-is, cosmetic).

**SLICE 3 — multiselect mechanism (via `inventory_drop_selector`) — DONE + EYEBALLED
CLEAN (user 2026-06-15, "looks good"), COMMITTED `948cfde8cd`. TOGGLE OFF.** Lights
the multiselect render path. Tiny,
because slice-2b already does the heavy lifting: (1) the `selection_column` is just
another visible `inventory_column` (appended in the multiselector ctor) → slice-2b's
side-by-side render shows it as a column FOR FREE; its "N of M" / count caption is
cell text (`selection_column_preset::get_caption`) already captured by the cell
cache. (2) the only thing `rml_rows()` missed = the **multiselect mark glyph** drawn
SEPARATELY by `draw()` via `mvwputch` (not in cell text): `-` none (dark_gray) / `+`
all (light_green) / `#` partial (light_green). Added it to `rml_rows()` after the
symbol, gated by the SAME `allows_selecting() && activatable() && multiselect` as
draw() — so it renders ONLY for multiselect columns and the selection_column
(multiselect=false, appended after the set_multiselect loop) shows no mark, matching
curses. (3) gate: `inventory_drop_selector::uses_rml()` override → the shared
`inventory_rmlui_enabled()` (the per-subclass-override mechanism from slice 1; compare/
iuse/pickup stay curses). (4) `query_count` is a `string_input_popup` (Tier-0,
migrated) over the still-rendered inventory doc — no bespoke work, same as `set_filter`.
F4 label renamed "inventory (pick+drop) via RmlUi" (one global toggle now lights both
proven selectors). **EYEBALL CHECK (user, A/B via F4):** open the DROP screen (drop
items) — items list shows the multiselect mark (`-`/`+`/`#`) after the symbol, marks
flip as you RIGHT/select, a SEPARATE selection column on the right lists chosen items
with "N of M" counts, query_count popup (enter a number) sets the count + the mark
goes `#`/`+`; weight/volume stats track; the actual drop happens on CONFIRM. The PICK
inventory still renders correctly (shared toggle, slice 1/2 unaffected). **WATCH:**
(a) `query_count`'s string_input_popup must render + tick over the inventory doc (16ms
tick; same path as set_filter, untested in slices 1-2 — flag if the popup is frozen or
the doc vanishes behind it). (b) selection column appears/hides on narrow widths
(`rearrange_columns` sets its visibility by overflow) — at small terminal sizes it may
not show (matches curses). (c) the mark color (`#`/`+` light_green) should match curses.

**SLICE 4 — two-selection compare (`inventory_compare_selector`) — DONE + EYEBALLED
CLEAN (user 2026-06-15, "looks good"), COMMITTED `b1108ec7dc`. TOGGLE OFF.** GATE ONLY. compare
inherits the multiselector ctor (selection column "ITEMS TO COMPARE" + marks) and
overrides ONLY `toggle_entry` (input-side: sets `chosen_count` 0/1, tracks the
`compared` pair) — grep-confirmed it has NO render override (no refresh_window/draw/
rml_/get_raw_stats), so its render IS the multiselect path slice 3 already proved.
So slice 4 = `inventory_compare_selector::uses_rml()` override → shared
`inventory_rmlui_enabled()`; the "two-selection state" is execute()-loop logic
(keyboard, untouched). F4 label → "inventory (pick+drop+compare) via RmlUi". The
comparison RESULT screen (after execute returns the item pair) is a SEPARATE
examine/compare display, not the selector — out of scope here. **EYEBALL CHECK (user,
A/B via F4):** open compare (the inventory "compare items" action) — marks appear,
selecting an item marks it + adds to the "ITEMS TO COMPARE" selection column, picking
a SECOND item triggers the comparison (returns the pair); selecting/deselecting works,
the 2-item cap holds (can't mark a 3rd). Marks/colours match curses (chosen_count is
0/1 → `+` if available==1 else `#`). Pick+drop still render (shared toggle).

**SLICE 5 — iuse + pickup selectors — CODE-COMPLETE + BUILD-GREEN (inventory_ui.{h,cpp}
+ devui compile + LINK clean, fresh obj+bin mtime), TOGGLE OFF, EYEBALL OWED,
UNCOMMITTED.** GATE ONLY (like slice 4). Both inherit the multiselector render;
grep-confirmed NO render override. Their differences are `get_raw_stats` (custom stats
— iuse builds them from its `GetStats` functor; both return the 2-elem `stats` type by
signature, so they flow through slice-2's GENERIC stats header via virtual dispatch,
no render change) and `set_chosen_count` (input-side). Added
`inventory_iuse_selector::uses_rml()` + `inventory_pickup_selector::uses_rml()` → the
shared `inventory_rmlui_enabled()`. **With this, ALL concrete selectors are lit**
(pick s1, drop s3, compare s4, iuse+pickup s5; the `inventory_multiselector` base is
never instantiated) → F4 label simplified to "inventory via RmlUi (all selectors)".
**EYEBALL CHECK (user, A/B via F4):** (1) iuse = an "apply/use which items" action
(e.g. the multi-item use flows) — marks + selection column + query_count work AND the
custom stats header shows the iuse-specific stats (whatever the GetStats functor
computes, not just weight/volume). (2) pickup = the pickup screen (walk onto a tile
with items / `,`) — marks, selection column, counts, and its weight/volume(+capacity)
stats. Both: CONFIRM applies the selection. Pick/drop/compare still render (shared
toggle). **WATCH:** iuse's stats header is the first NON-weight/volume stats rendered
via the RmlUi path (slices 1-4 all used the default weight/volume) — confirm the
custom captions/values + right-alignment read correctly (the generic loop should
handle it, but it's the one untested-shape bit of slice 5).

**SLICE 6 — framework completion (filter indicator + nav modes + multi-column
fidelity) — CODE-COMPLETE + BUILD-GREEN (inventory_ui.cpp compile + LINK clean, fresh
obj+bin mtime), TOGGLE OFF, EYEBALL OWED, UNCOMMITTED.** Investigated all three named
items; only ONE needed code (avoided speculative fidelity work per the simplicity
discipline):
- **Filter coexistence (the one real gap) — DONE.** The filter APPLIES on confirm via
  the existing `set_filter` (string_input_popup, Tier-0) → `col->set_filter()` →
  `rml_sync` re-renders the filtered columns on the next redraw; popup-over-RmlUi-doc
  is already proven (crafting/construction filter). BUT `draw_footer` also draws a
  **filter INDICATOR** (`[F] Filter` / `[F] Filter: <text>`, draw_footer:2012-2021)
  that the RmlUi footer (which only carried `get_footer(mode)`) omitted. Added a bound
  `filter_rml` (built in rml_sync under the same `has_available_choices()||!filter`
  guard, key via `ctxt.get_desc("INVENTORY_FILTER")`, label c_light_gray + filter text
  c_white) and split the footer RML into a flex row: `.inv-filter` (left, content
  width) + `.inv-mode` (`get_footer`, flex:1 centered) — mirroring draw_footer's
  left-filter / centered-mode. ASCII `< >`/`─ ─` decoration dropped (semantic rewrite).
- **Nav modes — NO CODE (rides existing).** The mode name/colour is `get_footer(mode)`
  → `footer_rml` (now `.inv-mode`), dirtied every sync; switching `mode` updates it.
  The active column's cursor highlight is `active && is_selected` in `rml_rows` → only
  the focused column highlights (matches curses). Both already worked from slices 1-5.
- **Multi-column fidelity — NO CODE (rides slice 2b).** Side-by-side columns shipped
  in 2b (flex content-width + gap). Curses-exact centering/occupancy-ratio/variable
  gaps deliberately NOT replicated (left-aligned flex is clean + readable; pixel-
  faithful gap math is not worth the complexity).
F4 toggle unchanged ("inventory via RmlUi (all selectors)"). **EYEBALL CHECK (user,
A/B via F4):** (1) **filter** — press the filter key, type a query in the popup
(renders over the doc, ticks), confirm → list narrows to matches AND the footer-left
shows "[F] Filter: <query>"; clear it → indicator shows "[F] Filter" (or gone if no
choices). (2) **nav modes** — cycle the nav mode (the mode key) → the centered footer
hint changes; in a multi-column selector, the cursor highlight stays in the focused
column only. (3) **multi-column** — open a selector with 2+ columns (e.g. a drop with
worn + the selection column) → columns sit side-by-side, the active one's cursor
highlights. **WATCH:** when the filter popup is OPEN, the inventory's own footer (filter
indicator + mode) still renders behind the Tier-0 popup (curses replaced it with the
input box inline) — harmless overlay, flag only if it reads badly.

**INVENTORY_SELECTOR FRAMEWORK = DONE + FULLY EYEBALLED (slices 1-6 + 2b; slices 5+6
confirmed clean by user 2026-06-15).** All 5 concrete selectors lit;
header/stats/multi-column/marks/selection-column/filter/nav all on the RmlUi path. Only after that do the FOLLOWERS migrate: `game_inventory` (builds a
selector + calls execute — no independent render, so it rides the framework
automatically once the toggle's on; mostly a verification pass) → then **advanced_inv**
(the multi-pane dual-list variant — its own sub-project, scope its panes individually).

### Tier 3 followers — game_inventory

- **game_inventory selectors = DONE BY COVERAGE (no code).** All of `game_inventory.cpp`'s
  screens build an `inventory_*_selector` + call `execute()` with NO independent render
  → they ride the now-complete framework automatically when the toggle is on. Verified:
  `common_inventory_selector` (the only bespoke subclass, used by the main `i`nventory)
  derives `inventory_pick_selector` and overrides ONLY `handle_action` (the `unload_all`
  key) → inherits pick's `uses_rml()`, lit for free. So game_inventory's many entry
  points (activate/consume/wield/wear/drop/pickup/etc.) are a verification pass, not a
  migration.
- **EXCEPTION — `game_menus::inv::compare(l, r)` — MIGRATED (CODE-COMPLETE + BUILD-GREEN,
  game_inventory.cpp + devui compile + LINK clean, fresh obj+bin mtime), TOGGLE OFF,
  EYEBALLED CLEAN (user 2026-06-15; compare-delta colouring + lockstep scroll
  confirmed), COMMITTED `c376c1c305`, TOGGLE OFF.** This is the ONE game_inventory
  function with its own render: the comparison RESULT display (after `inventory_compare_selector` picks two
  items) — a `ui_adaptor` with two side-by-side `draw_item_info` panes + synced scroll.
  Migrated via the F.2 item-info component (`item_info_rml_lines`). **It is the FIRST
  compare-delta consumer** (the gap flagged at examine_item_menu): each pane's
  `item_info_data` carries the OTHER item as its compare set, so `format_item_info`
  renders the +/- stat deltas — now exercised through the RmlUi path. New `compare.{rml,
  rcss}` model "compare": two `.cmp-pane` (title + `#cmp-lhs`/`#cmp-rhs` scroll-pane of
  the folded item-info lines). Lines/titles are STATIC for the screen's lifetime → built
  once in `open()` (no per-frame sync); the on_redraw just `if(rml) return;` else the
  curses two-pane draw. Scroll is native: UP/DOWN (±0.15 page) + PAGE_UP/DOWN (±0.85)
  `SetScrollTop` BOTH panes in lockstep (the curses `lhs/rhs_scroll_pos` are kept for the
  curses path). New toggle `compare_items_rmlui_enabled()` (rml_screen.h) + F4 "compare
  items via RmlUi". rml_doc dtor tears down at the single function exit. **EYEBALL CHECK
  (user, A/B via F4):** compare two items (inventory → compare action, pick two) → two
  panes side-by-side, each item's info, **and the +/- compare deltas coloured** (the
  novel bit — e.g. one item's higher stat shows green/+, lower red/-); UP/DOWN + PAGE
  scroll BOTH panes together; QUIT returns to the compare selector (pick another pair) /
  exits. **WATCH:** (a) the compare-delta colouring is the first real test of
  `format_item_info`'s compare path through `item_info_rml_lines` — confirm deltas show
  and are coloured, not flat. (b) both panes scroll in lockstep (not independently).
### Tier 3 — advanced_inv (AIM) — MULTI-SESSION SUB-PROJECT

**Why it's the hardest inventory unit:** `advanced_inv.cpp` (1992) is the dual-pane
item-management screen (`/`). Unlike `inventory_selector` (one shared `refresh_window`
+ a cell cache + an `rml_rows()` helper), AIM draws each pane with **positioned
`mvwprintz` at absolute columns** (`print_items` :216 — name_startpos/amt_startpos/
weight_startpos/vol_startpos), no row-builder helper. It is ONE screen rendered as ONE
RmlUi doc (both panes + the middle sidebar), so the gate is a single
`advanced_inv_rmlui_enabled()` toggle lighting the whole doc — the WORK is sliced, but
runtime is all-or-nothing (flip ON for eyeball only when a slice is presentable).
Row data = `advanced_inv_listitem` (name/stacks/weight/volume/cat/area/from_vehicle/
autopickup; category-header vs item-entry). Columns: name (+ITEM_SYMBOLS glyph + stolen
`!` + money), src (AIM_ALL only), amt (>1), weight, vol. Active pane's cursor = `>>`+
hilite; inactive pane no cursor. `compact` when TERMX<=100.

**Slices (each its own commit + eyeball):**
1. **Slice 1 — dual-pane item lists + pane head.** Both panes side-by-side; each =
   area-name title + weight/volume capacity head (top-right) + a column-header row +
   the item list (category `[Name]` rows + item rows with name/amt/weight/vol cells,
   per-item colour, `.selected` highlight on the active pane's cursor). New
   `pane_rml_html(pane,active)` helper mirrors print_items' per-item logic but emits a
   baked markup string (flat data-rml per pane, like inventory slice 2b — cells are
   flex columns via CSS, not absolute positions). DEFER: AIM_ALL `src` column, compact
   mode, autopickup marker, the middle sidebar, the footer.
2. **Slice 2 — sidebar + per-pane area header.** `redraw_sidebar` (:1053, the middle
   minimap + info) + `print_header` (:529, the in-pane area-selection grid line).
3. **Slice 3 — footer + interactions.** Sort/filter/move hints footer; filter popup
   (string_input Tier-0) coexistence; examine (item-info component); move-item feedback;
   AIM_ALL src column; compact mode; autopickup marker. All editing/move stays keyboard.

**SLICE 1 — dual-pane item lists + pane head — DONE + EYEBALLED CLEAN (user
2026-06-15; after the recalc fix both lists populate, "perfect"), COMMITTED
`1f8fbba979` + fix `0f42b65261`. TOGGLE OFF.** One RmlUi doc ("advinv") for the whole screen; all-scalar
model (six baked strings — per pane: title / weight-vol head / rows_html), NO struct/
array registration. Two free builders in an anon namespace mirror print_items:
`aim_pane_rows_html(pane,active)` (column-header row + category `[Name]` rows + item
rows, each a `.aim-row` of name/amt/weight/vol `<span>` cells — CSS flex columns, not
absolute x; per-item colour via `it.color_in_inventory()`, inactive pane greyed; name
reproduces stolen `!`/money/ITEM_SYMBOLS; amt/weight/vol reproduce the precision +
red-overflow rules; `.selected` highlight on the active (`src`) pane's cursor row) and
`aim_pane_head_html(pane,active,squares)` (the INVENTORY/WORN carried-vs-capacity head
OR the square weight/vol head incl. AIM_ALL + container/vehicle/map maxvolume). Opened
in `display()` under `if(!is_processing())`; `sync_rml()` rebuilds all six strings each
redraw (active pane = `src`); on_redraw `if(rml){<prep>;sync_rml();return;}` else curses.
**FIX (`0f42b65261`, first eyeball: both lists rendered EMPTY):** the rml branch returned
before `redraw_pane`, so its `recalc_pane` + `fix_index` prep never ran and `pane.items`
stayed empty. The rml branch now runs that same per-pane prep (gated `recalc||pane.recalc`
+ `fix_index`) before `sync_rml`. Re-eyeball.
`advinv.{rml,rcss}`: `.panel` root → flex row of two `.aim-pane` (divider between) →
phead (title left / capacity right) + `.aim-list` scroll-pane (data-rml the baked rows).
New toggle `advanced_inv_rmlui_enabled()` + F4 "advanced inventory via RmlUi". rml_doc
dtor tears down at display() exit. **DEFERRED (later slices, NOT bugs):** AIM_ALL `src`
column, compact mode (TERMX<=100), autopickup magenta marker, the middle SIDEBAR
(minimap + area grid), per-pane area-selection header, the FOOTER (sort/filter/move
hints), filter popup, examine, move-item — all still curses-less in RmlUi mode this
slice (the doc shows only the two lists + heads). Keyboard still drives everything
(move/sort/filter/examine work; only their on-screen FEEDBACK is partial until later
slices). **EYEBALL CHECK (user, A/B via F4 "advanced inventory via RmlUi"):** open AIM
(`/`) → two panes side-by-side, each with its area name (active pane's brighter) +
weight/volume head top-right, a "Name (charges) … amt weight vol" column header, then
the items with category `[headers]`, per-item colours matching curses, amt/weight/vol
right-aligned in their columns; the ACTIVE pane's cursor row highlights (inactive pane
no cursor); TAB swaps active pane (highlight + title brightness move); UP/DOWN move the
cursor; moving items between panes still works (keyboard) and the lists update.
**WATCH:** (a) the lists scroll natively but the keyboard cursor past the viewport has
no scroll-into-view yet (same class as inventory; a later slice). (b) NO sidebar/footer
yet — screen looks sparser than curses (expected this slice). (c) long item names
truncate (`.aim-c-name` overflow:hidden) — confirm they don't overflow into the numeric
columns.

**SLICE 2 — top bar + per-pane area-selection grid — CODE-COMPLETE + BUILD-GREEN
(advanced_inv.{h,cpp} compile + LINK clean, fresh obj+bin mtime), TOGGLE OFF, EYEBALL
OWED, UNCOMMITTED.** Adds the two chrome pieces that read as "missing" after slice 1:
(1) **Top bar** — clock (if the avatar has a watch, `to_string_time_of_day`) on the
left + the keybinding hint (`< [key] keybindings >`, yellow key) and reset-filter
indicator (`Reset Filter On Close [ON|OFF]`, AIM_AUTORESET_FILTER) on the right; built
inline in sync_rml as two bound strings (`clock_rml`/`hints_rml`). (2) **Area-selection
grid** — new member `aim_area_grid_html(pane,sel)` mirrors `print_header`'s per-location
colour state (canputitems → current=white / selectable=gray / can't=red; vehicle=blue;
`<>` vs `[]` brackets; `V` when in-vehicle) but lays the 9 directional locations out as
a SEMANTIC compass 3x3 (NW/N/NE · W/C/E · SW/S/SE) + a specials row (Inventory/Worn/All/
Dragged/Container), instead of the absolute `hscreen` positions (semantic rewrite). Per
pane (`left_grid_rml`/`right_grid_rml`), sel = current item's area else pane area (mirrors
the print_header call site). `advinv.{rml,rcss}` restructured: `.panel` → `.aim-outer`
column = `.aim-topbar` + `.aim-root` (the two panes); each pane gains `.aim-areagrid`
(compass rows right-aligned in the header). **DEFERRED to slice 3:** the message log
(`Messages::display_messages`), the graphical MINIMAP (`draw_minimap` — overmap tiles,
hard in RmlUi; its own effort), the footer, filter popup, examine, move feedback,
AIM_ALL src column, compact mode, autopickup marker. **EYEBALL CHECK (user, A/B via F4):**
open AIM → top bar shows the clock (if watch) + keybinding + reset-filter hints; each
pane shows the area-selection grid (compass of `[key]` cells, the pane's CURRENT area
white, selectable gray, unreachable red, vehicle-capable `<>`); switching a pane's area
(move/`[`/`]` etc.) updates which cell is highlighted; TAB still swaps the active pane.
**WATCH:** (a) the grid is a SEMANTIC compass, NOT the curses `hscreen` pixel layout —
confirm the directions read right (NW top-left … SE bottom-right) and the current area
highlights. (b) rotation/iso (`screen_relative_location`) is honoured via the same call
print_header uses, but only matters on iso tilesets. (c) no minimap/messages yet
(slice 3) — top-right where the minimap sat is empty.

**SLICE 3 — sort indicator + filter footer + AIM_ALL src column — CODE-COMPLETE +
BUILD-GREEN (advanced_inv.cpp compile + LINK clean, fresh obj+bin mtime), TOGGLE OFF,
EYEBALL OWED, UNCOMMITTED.** The functional finisher. Adds the three real remaining
gaps from `redraw_pane`/`print_items`: (1) **Sort indicator** — per pane `< [SORT]
Sort: <name> >` (via `get_sortname`) + item count `< n/max >` when the square has a
max, top of pane (`*_sort_rml`). (2) **Filter footer** — per pane `< [F] Filter >` /
`< [F] Filter: <text> >  [R] Reset`, bottom of pane (`*_filter_rml`); while editing,
the ACTIVE pane shows the live in-progress query from `spopup->text()` (the
filter-popup coexistence — the rml branch skips the curses spopup draw, so the query
is surfaced in the footer instead). (3) **AIM_ALL src column** — `aim_pane_rows_html`
now takes `squares` and, when the pane area is AIM_ALL, emits a `src` cell
(`squares[sitem.area].shortname`) + a `src` column header (matches print_items). Six
new bound strings → 4 (sort/filter ×2) this slice. `advinv.{rml,rcss}`: each pane gains
`.aim-sort` (top) + `.aim-filter` (bottom); new `.aim-c-src` cell. **AIM RmlUi path is
now FUNCTIONALLY COMPLETE** (both panes: items + colours + columns + selection + area
grid + sort + filter + heads + top bar). **DOCUMENTED REMAINING GAPS (minor / deferred,
NOT blocking the slice — flag if any matters):** (a) the graphical **MINIMAP** (overmap
tiles — genuinely hard in RmlUi; the top-right minimap area is empty in rml mode); (b)
the **message log** (`Messages::display_messages` in the head bar — needs a text getter;
not surfaced in rml mode); (c) **autopickup `>` marker** on auto-pickup rows; (d) area
**desc/flags** sublines under the title (title shows the area name only); (e) **examine**
(`action_examine`) renders via its own ui_adaptor over the doc — should composite fine
(verify); (f) **compact mode** (TERMX<=100) — the flex layout adapts, the curses
compact branch isn't reproduced. **EYEBALL CHECK (user, A/B via F4):** open AIM → each
pane shows the sort indicator (`Sort: <mode>` + count) at top and the filter line at
bottom; press the sort key → the indicator updates; press filter, TYPE a query → the
active pane's filter footer shows it live, confirm → list narrows + footer shows
`Filter: <q>  [R] Reset`; switch a pane to **All** (surrounding) → items gain a `src`
column showing each item's source square (NW/S/etc.); move/sort/filter all still work.
**WATCH:** (a) the live filter query in the footer (`spopup->text()`) updates as you
type (the popup-draw is replaced by this). (b) src column only on AIM_ALL; the numeric
columns stay aligned with/without it. (c) minimap + message log absent (documented gap).

**GAP-CLOSING (2026-06-22, build-green, toggle already ON via `ad617da615`, eyeball
owed) — AIM rml path is now COMPLETE enough to de-curse:** the two head-bar gaps (b)
above closed/decided: (1) **message log DONE** — new `msglog_rml` scalar bound in the
topbar; sync_rml builds it from `Messages::recent_messages(4)` (the text getter the
plan wanted) → per-line `<div class="aim-msg">` of `colorize(time,dark_gray)+colorize(
text,light_gray)` via the proven `cata_text_to_rml(colorize+colorize)` idiom (same as
filter_str). `advinv.{rml,rcss}` gain `.aim-msglog`/`.aim-msg` between topbar + panes.
(2) **minimap DROPPED in rml mode (decision, not deferral)** — it's `get_map().draw()`
(a LOCAL-map cell snippet, NOT overmap tiles — the plan mis-described it) + L/R/^/v
pane markers; the **slice-2 area-selection compass grid already conveys pane→direction**
and terrain context is marginal on an item screen, so reproducing the cell render in
RmlUi is high-effort/low-value. Aligns with the 2026-06-20 "drop faithful curses-repro"
call. Remaining accepted gaps unchanged: (c) autopickup marker, (d) area desc/flags
sublines, (e) examine composites over the doc, (f) compact mode.

**DE-CURSED (2026-06-22, build-green, eyeball owed) — `advanced_inv` is curses-draw-free.**
Removed the on_redraw `else` curses body + the now-dead curses renderers it exclusively
fed: `print_items`, `print_header`, `redraw_pane`, `redraw_sidebar`, `draw_minimap`,
`refresh_minimap`, `get_minimap_sym` (~441 lines, +header decls). KEPT (char-sheet
convention — dies with the curses backend at Tier 10): the `head`/`minimap`/`mm_border`/
pane-window scaffolding in the resize callback (`minimap_width` still feeds the head
layout math; the minimap windows are now created-but-undrawn). The `if(rml){…return;}`
on_redraw guard stays (vestigial — toggle-OFF now renders nothing; toggle deleted at
rip-out). `recalc_pane` kept (the rml prep loop calls it). **EYEBALL (user, in-game —
toggle ON by default):** AIM renders fully via rml with NO curses fallback; everything
slices 1-3 + the new message log show; the minimap area is intentionally empty.

**Discipline:** the cell/preset model is load-bearing and this file is 2640 lines
under the stale-read hook — every model field MUST be verified against fetched
source (the armor near-miss is the warning). Per-subclass gate means each slice
is independently bisectable + revertible.

## Tier 4 — big bespoke menus (worldfactory / main_menu / options / newcharacter)

> **STATUS (2026-06-17 audit): ALL FOUR GIANTS DONE + COMMITTED, eyeball owed.**
> options / worldfactory / main_menu / newcharacter each have a committed
> `*_rmlui_enabled()` toggle + assets (clean tree). The "ACTIVE PHASE" framing below
> and any "in flight / uncommitted" per-screen notes are STALE — see the top frontier
> banner. The tier is functionally complete; only the user A/B eyeball remains.

ACTIVE PHASE (2026-06-15). The giant tier. LOC: main_menu 1219 < worldfactory 1641 <
options 4196 < newcharacter 4337 (the plan's "~172K/177K" were file BYTE sizes). Plan
mandate: *"build a form/tab-page sub-pattern HERE"* + *"Do NOT solo-charge worldfactory"*
+ budget as multi-screen sub-projects.

**Dependency-aware order: options → worldfactory → main_menu → newcharacter.** Options
first because (a) it's the canonical tabbed FORM → matures the reusable form/tab-page
pattern the others need; (b) zero coupled game state → testable anytime;
(c) **worldfactory depends on it** — `worldfactory.cpp:536` calls
`get_options().show(false, true, on_quit)` (world-options-only mode). newcharacter
(biggest + most coupled: points/scenario/profession interplay) goes LAST on the matured
pattern.

### Tier 4 screen #1: options (`options_manager::show`)

Anatomy (`options.cpp:3457`): ONE `ui_adaptor` + ONE `on_redraw` + ONE input loop; 4
curses windows. Shape = **page tabs + grouped two-column (name/value) list + tooltip**.
Rows = `pages_[iCurrentPage].items_` (`PageItem{type,group,data}`,
`ItemType{GroupHeader,BlankLine,Option}`); collapsible groups via `groups_state[id]`.
3 callers: `main_menu.cpp:844` `show(false)`, `handle_action.cpp:2639` `show(true)`,
`worldfactory.cpp:536` `show(false,true,on_quit)`. Editing stays keyboard
(setPrev/setNext; int/float → `string_input_popup` Tier-0; CONFIRM on a header toggles
collapse).

**Slices:**
1. **Slice 1 — standalone mode (`world_options_only==false`):** covers BOTH `show(false)`
   (main menu) and `show(true)` (in-game, incl. the "Current world" tab relabel +
   world-options note + `ACTIVE_WORLD_OPTIONS`). Page tabs + grouped list + tooltip,
   render-only doc.
2. **Slice 2 — `world_options_only==true`:** worldgen-tab chrome
   (`worldfactory::draw_worldgen_tabs`) + `on_quit`; the piece worldfactory reuses.

**SLICE 1 — DONE + EYEBALLED CLEAN (user 2026-06-15, "great"), COMMITTED. TOGGLE OFF.**
12th `rml_doc`-style consumer; first Tier-4 screen. Post-eyeball fixes folded into the
commit: (a) **missing `body` rule** (the omission caused all 3 first-eyeball symptoms —
no body width → panel shrink-wrapped to tab content (width shifted per tab); no body
height → `.opt-list flex:1` collapsed to 0 → lists looked unpopulated; no centering)
→ added `body{display:flex;justify-content:center;align-items:center;width/height:100%}`
mirroring construction.rcss, panel `width:80%`+`min-width:720dp`; (b) `.opt-tabs .tab
{flex:0 0 auto}` so page tabs size to content (theme `.tab` flex:1 truncated "World
Defaults" etc.); (c) **keyboard scroll-follow** — UP/DOWN/tab set `rml_scroll_pending`
→ sync_rml `ScrollIntoView(ScrollAlignment::Nearest)` on the selected `#opt-list` child
(keyboard only — mouse/wheel not fought). ALSO shipped a **global F4 "RmlUi UI scale"
slider** (`rmlui_layer::ui_scale`, 0.5–1.5, default 1.0) multiplying the context
dp-ratio → scales font + dp spacing across ALL RmlUi panels live; `g_density_ratio`
kept as the true physical/logical ratio so input/hit-testing is unaffected (the font
was reported slightly too large). Adapts the
`construction` template (tabs+list+detail). `data/gui/options.{rml,rcss}` model
"options". STRUCTURAL POINTS: (1) gate = `options_rmlui_enabled() && !world_options_only`
→ slice 1 lights ONLY standalone; world-options-only stays curses (slice 2). (2)
**`fmt_name_value` HOISTED** out of on_redraw to show() scope + parameterized with
`cOPTIONS` → the curses on_redraw AND `sync_rml` share ONE formatter (no colour-rule
drift; the plan's "share one formatter" instruction). (3) model = `opt_rml_tab{name_rml,
selected}` (page bar) + `opt_rml_row{num_rml,name_rml,value_rml,is_header,selected}` +
`tooltip_rml`; `sync_rml` rebuilds all three each redraw, flattening collapsed-group
visibility into the flat row list and keeping `rml_visible[row]→page_items` for the
click callback. (4) cursor `>> ` baked into the selected row's name (RCSS has no ::before
content — the AIM/mutations idiom); value passed `is_selected=false` for BASE colour
(CSS `.selected` does the highlight). (5) MOUSE = click-only select (no mouseover — a
parked mouse can't steal the keyboard cursor); `on_tab(idx)` sets page+resets line/scroll
(mirrors NEXT_TAB), `on_select(idx)` → `iCurrentLine=rml_visible[idx]`. (6) doc opened
before the loop; `rml_doc` dtor tears down at the single function exit (the post-loop
save/apply block runs `query_yn` over the still-rendered doc — behaviour-identical to
curses, which also redraws options behind that prompt). F4 toggle "options via RmlUi"
(OFF). **DEFERRED (NOT bugs):** native scroll has no keyboard scroll-into-view yet (long
pages — the staged ScrollIntoView one-liner if eyeball shows cursor-lost);
world_options_only chrome (slice 2). **EYEBALL CHECK (user, A/B via F4):** open Options
from **main menu** AND **in-game** (Esc→Options) — page tabs across top (current
hilited), rows show number + name + value with colours matching curses (green=on,
red=off/disabled, grey=prereq-unmet), `>>` on the cursor row; group headers show
`+`/`-` and CONFIRM collapses/expands (members hide/show); tooltip tracks selection;
UP/DOWN + NEXT_TAB/PREV_TAB navigate; LEFT/RIGHT/CONFIRM change values; int/float opens
the string_input popup over the doc; mouse click selects a row / a tab; QUIT saves +
applies (terminal-size / tiles / language change paths still fire). **WATCH:** (a) value
text keeps its baked colour over the yellow `.selected` bg — confirm readable (cursor
`>>` is the backup selection cue). (b) **D3D12 (Win11) first-of-Tier-4 glance** — first
Tier-4 dynamic doc on the primary target. (c) the world page (in-game `show(true)`)
relabels to "Current world" + shows the red note.

**SLICE 2 — world_options_only mode (worldfactory wizard step) — CODE-COMPLETE +
BUILD-GREEN (options.cpp compile + LINK clean; options.cpp.o + binary fresh 15:03),
TOGGLE OFF, EYEBALL OWED, UNCOMMITTED.** Lights the `world_options_only==true` path
(`worldfactory::show_worldgen_tab_options` → `get_options().show(false,true,on_quit)`).
SMALL — the option list + tooltip already render the `world_default` page (sync_rml's
`cOPTIONS` already keys `(ingame||world_options_only)&&iCurrentPage==iWorldOptPage` →
`ACTIVE_WORLD_OPTIONS`). Three changes: (1) gate dropped the `&& !world_options_only`
→ `rml.open(options_rmlui_enabled())` lights both modes (same toggle). (2) sync_rml tab
strip branches: world mode renders the 3 worldgen wizard steps ("World Mods" / **"World
Options"** current / "Finalize World"), mirroring `draw_worldgen_tabs(w,1)` — RENDER-ONLY
(wizard nav is keyboard PREV_TAB/NEXT_TAB, which `show` returns to worldfactory at
options.cpp's `world_options_only` early-return → `show_worldgen_tab_options` maps to
-1/+1/-999). (3) `on_tab` no-ops in world mode (clicks must not switch options pages —
there's one). No new toggle (the wizard step rides the options toggle). The OTHER wizard
steps (mods / finalize / world-name) stay curses → they migrate with **worldfactory
(Tier 4 #2)**, which will own the real worldgen-tab component; this slice just gets the
options step onto RmlUi. **EYEBALL CHECK (user, A/B via F4 "options via RmlUi"):** New
World → advance to the **World Options** wizard step — the option list + tooltip render
in RmlUi (same two-column rows as standalone), the tab strip shows the 3 worldgen steps
with "World Options" highlighted; LEFT/RIGHT/CONFIRM edit world options; PREV_TAB →
back to Mods (curses), NEXT_TAB → Finalize (curses), QUIT aborts; the edited world
options take effect on the new world. **WATCH:** (a) the worldgen tab strip is
render-only — clicking a step does nothing (wizard is keyboard); (b) stepping
RmlUi-options → curses-mods and back is clean (doc opens/closes per `show` call).

### Tier 4 screen #2: worldfactory (`worldfactory.cpp`, 1641) — MULTI-SCREEN SUB-PROJECT

> **STATUS (2026-06-17): ALL 4 SLICES CODE-COMPLETE + COMMITTED, EYEBALL OWED.**
> The per-slice blocks below were written PRE-commit (they say "UNCOMMITTED") — that
> is stale; git history confirms all four landed (Jun 15):
> - slice 1 Finalize — `07d6d6aa53`
> - slice 2 pick_world — `6a7f93a194`
> - slices 3+4 active-mods + mod-selector — `d689e9acb4`
> - wizard-renders-fully-under-one-toggle fix — `2eedac603e`
>
> All 4 RmlUi screens (`pickworld` / `worldmods` / `modselect` / `worldfinalize`)
> + their `data/gui/*.{rml,rcss}` exist, gated by the single `worldfactory_rmlui_enabled()`
> toggle (F4 "worldfactory via RmlUi", OFF). The wizard Mods step + `edit_active_world_mods`
> ride slice 4 (delegates); the Options step rides options slice 2; the driver tab-strip is
> occluded (no migration, dies at rip-out). **So worldfactory itself is functionally
> complete — the only remaining work is the user A/B eyeball of the 4 screens.** Detail
> blocks below retained as the per-slice record.

**Why it's a sub-project (not one screen):** worldfactory is a world-creation WIZARD
plus several standalone screens, each with its own `ui_adaptor` + `on_redraw` +
`input_context` (so each fits the harness independently). Plan mandate: *"Do NOT
solo-charge worldfactory; build a form/tab-page sub-pattern; budget as a multi-screen
sub-project."*

**Architecture (verified 2026-06-15 via explore):**
- **Wizard driver = `make_new_world(bool,string)` (77-120).** Owns ONE `ui_adaptor`
  whose on_redraw draws ONLY the worldgen tab strip (`draw_worldgen_tabs(wf_win,curtab)`,
  102-105). Loop (108-113) calls `curtab += tabs[curtab](wf_win, world, on_quit)` over
  the 3 tab fns built at 55-57; return codes +1 next / -1 prev / -999 quit / 0 stay;
  `curtab<0` exits (abort). Each TAB FN owns its OWN `ui_adaptor` + nested input loop and
  draws its own full-screen window OVER wf_win (so each tab re-draws its own worldgen tab
  strip — that's why options slice 2 renders the strip itself; the driver's strip is
  occluded and dies at rip-out). **Consequence: NO separate driver migration — each tab's
  RmlUi doc renders its own strip (the options-slice-2 precedent); the worldgen strip is
  3 render-only bound tabs per doc, not a shared component worth extracting yet.**
- **The 3 wizard tabs:** Mods (`show_worldgen_tab_modselection`→`show_modselection_window`),
  Options (`show_worldgen_tab_options`→`get_options().show(false,true,…)` — **DONE**,
  options slice 2), Finalize (`show_worldgen_tab_confirm`).
- **Standalone screens:** `pick_world` (world picker), `show_active_world_mods`
  (read-only mod list), `edit_active_world_mods` (delegates to `show_modselection_window`
  with `standalone=true`).

**Gate:** ONE `worldfactory_rmlui_enabled()` toggle, lit PER-SCREEN (each on_redraw
guards `if(rml){sync_rml();return;}`), exactly like the inventory framework. Light one
screen per slice; the rest stay curses even with the toggle on.

**Slices (conforming-first → giant last; each its own commit + eyeball):**

1. **Slice 1 — Finalize (`show_worldgen_tab_confirm`, 1251-1394).** Smallest wizard
   screen; proves the worldfactory toggle + the worldgen-tab-strip-in-doc consistency
   with options slice 2. Shape = a small FORM: worldgen tab strip (3 steps, "Finalize
   World" current = index 2) + "World Name:" + the live name text + save-format line
   (V1 Legacy / V2 Current) + keybinding hints + a "NO NAME ENTERED!" error state.
   **Name editing stays keyboard** — the inline `string_input_popup spopup` keeps
   handling TEXT.* keys; the doc DISPLAYS `spopup.text()` live (the AIM/safemode
   live-query idiom) with a caret cue. Model: `tabs` (3 worldgen steps) + bound strings
   `name_rml` (live) / `format_rml` / `hints_rml` / `error_rml`. Reads/writes
   `world->world_name` + `world->world_save_format` via the existing loop
   (PICK_RANDOM_WORLDNAME / TOGGLE_V2_SAVE_FORMAT / NEXT_TAB-finalize / PREV_TAB / QUIT).
   `data/gui/worldcreate.{rml,rcss}` (or `worldfinalize.*`). **EYEBALL:** New World →
   Finalize step: tab strip shows Finalize current; type a name (shows live, caret);
   random-name key fills it; V1/V2 toggle line updates; empty-name + NEXT_TAB shows the
   error; NEXT_TAB with a valid name creates the world; PREV_TAB → Options.

2. **Slice 2 — `pick_world` (263-476, PICK_WORLD_DIALOG).** Near-CLONE of the options
   screen shape (4-window bordered: border/tooltip/header/content → tabs + list +
   tooltip), so it reuses the options.rml pattern almost verbatim. Page tabs (`[Page N]`),
   per-world row = number + `>>` cursor + `"name (charcount)"`, tooltip "Pick a world to
   enter game". Pagination by content height → native scroll (drop `world_pages` paging,
   one flat list, the options scroll-into-view remedy applies). Model: `tabs` (or drop
   pages for one scrolling list — decide at build) + `rows{name_rml,selected}`. Input
   unchanged (UP/DOWN, CONFIRM→return world, QUIT). `data/gui/pickworld.{rml,rcss}`.
   **EYEBALL:** main menu → World (pick existing) → worlds list with char counts, cursor
   highlight, CONFIRM enters, QUIT cancels; many worlds scroll.

3. **Slice 3 — `show_active_world_mods` (681-743).** Small read-only mod list — proves
   the **mod-row rendering** (the `draw_mod_list` shape: magenta category headers + mod
   rows `"[id] name"`, obsolete→dark_gray+`*`, invalid→red `N/A`, cursor `>>`) in
   isolation BEFORE the giant. A new `mod_rows_html`/builder mirrors `draw_mod_list`'s
   per-row logic (reuse `MOD_INFORMATION` name/category/obsolete). Model: `rows` (category
   header vs mod entry, colour baked). DEFAULT ctxt UP/DOWN/QUIT unchanged. Reuses a
   `worldmods.{rml,rcss}` the giant will share. **EYEBALL:** in-game world-mods viewer →
   active mods listed with category headers + colours + cursor; UP/DOWN scroll; QUIT.

4. **Slice 4 — `show_modselection_window` (784-1249, MODMANAGER_DIALOG) — THE GIANT.**
   Dual-pane: available (left, category-tabbed) | active load-order (right) + a `w_shift`
   reorder-indicator strip (`+`/`-` can_shift_up/down) + a `w_description` info pane
   (bottom) + category tabs (left) + filter. Reuses slice-3's mod-row builder for BOTH
   panes; the shift `+`/`-` glyphs render per active row (gated like draw_mod_list); the
   description pane renders `mod_ui::get_information()` (colour-tagged → `cata_text_to_rml`,
   the item-info idiom) with the VIEW_MOD_DESCRIPTION full popup unchanged (Tier-0). Model:
   `cat_tabs` + `avail_rows` + `active_rows` (with shift flags) + `desc_rml` + focused-pane
   + filter footer. **ALL editing stays keyboard** — add/remove/reorder go through
   `mod_ui::try_add/try_rem/try_shift` (dependency+conflict resolution) untouched; LEFT/
   RIGHT switch pane, CONFIRM add/remove, ADD_MOD/REMOVE_MOD reorder, category-tab keys,
   FILTER (string_input Tier-0), TOGGLE_SHOW_OBSOLETE, SAVE_DEFAULT_MODS, NEXT/PREV_TAB
   (wizard, unless standalone). `data/gui/modselect.{rml,rcss}`. **Lighting this ALSO
   lights `edit_active_world_mods` (delegates) + the wizard Mods step
   (`show_worldgen_tab_modselection` delegates) for free.** Hardest screen in Tier 4 so
   far — do it LAST, on the proven mod-row + tab + list + info-pane patterns. **EYEBALL
   (hit edges):** New World → Mods step: two panes, available category-tabbed, CONFIRM
   adds (deps pulled in), the active pane shows load order with `+`/`-` shift cues,
   ADD_MOD/REMOVE_MOD reorders (respecting can_shift), description pane shows the selected
   mod's info, FILTER narrows, obsolete toggle, NEXT_TAB→Finalize; ALSO the standalone
   edit-world-mods + the conflict/dependency error popups.

**Sequencing notes:** options (screen #1) done first matured the form/tab pattern; the
worldgen tab strip is per-doc (no shared component yet — revisit if a 3rd consumer
appears). After all 4 worldfactory slices + eyeball, the world-creation wizard is fully
on RmlUi (Mods+Options+Finalize) and Tier 4 #2 is complete → next Tier-4 screens:
main_menu (#3), then newcharacter (#4, the biggest/most-coupled, last).

**SLICE 1 (Finalize) — CODE-COMPLETE + BUILD-GREEN (worldfactory.cpp + rml_screen.h +
devui compile + LINK clean; worldfactory.cpp.o + binary fresh-relinked 15:27), TOGGLE
OFF, EYEBALL OWED, UNCOMMITTED.** New `worldfactory_rmlui_enabled()` toggle (one for the
whole sub-project, gated per-screen) + `data/gui/worldfinalize.{rml,rcss}` model
"worldfinalize". `show_worldgen_tab_confirm` gets `if(rml){sync_rml();return;}` in
on_redraw; rml_doc dtor tears down at the single return. sync_rml builds: worldgen tab
strip (3 steps, "Finalize World"=idx2, render-only) + `name_rml` ("World Name: " + live
`worldname` + `_` caret, or the red "NO NAME ENTERED!" when `noname`) + `format_rml`
(V2 Current white / V1 Legacy gray) + `hints_rml` (random-name / format-toggle +
explanation / nav, via `ctxt.get_desc`). **KEY POINT — editing stays keyboard:** the
inline `string_input_popup spopup` still processes TEXT.* in the loop; sync_rml
re-seeds `spopup.text(worldname)` each frame (mirrors the curses on_redraw) so
PICK_RANDOM_WORLDNAME + an initial/copied name reach the editor and don't revert. F4
toggle "worldfactory via RmlUi" (OFF). **EYEBALL CHECK (user, A/B via F4):** New World →
Finalize step: tab strip shows the 3 worldgen steps with "Finalize World" current; type
a name (shows live with trailing `_`); PICK_RANDOM key fills a random name (and it
STICKS, not reverts — the seed check); V1/V2 toggle flips the format line; empty name +
NEXT_TAB shows the red "NO NAME ENTERED!"; NEXT_TAB with a name creates the world;
PREV_TAB → Options (RmlUi if its toggle on). **WATCH:** the name caret is a static `_`
(no blink — editing is keyboard-only, render-behind); the worldgen tab strip is
render-only (consistent with options slice 2).

**SLICE 2 (pick_world) — CODE-COMPLETE + BUILD-GREEN (worldfactory.cpp compile + LINK
clean; worldfactory.cpp.o + binary fresh 15:36), TOGGLE OFF, EYEBALL OWED, UNCOMMITTED.**
2nd worldfactory consumer (shared `worldfactory_rmlui_enabled()` toggle). Same shape as
the options screen — page tabs + one-column world list + tooltip — so `pickworld.{rml,
rcss}` (model "pickworld") closely mirrors options. Render-only: the PICK_WORLD_DIALOG
loop owns `selpage`/`sel`; on_redraw gets `if(rml){sync_rml();return;}`; rml.open after
the ctxt actions (auto_pickup ordering — on_redraw is above the input_context).
STRUCTURAL: distinct struct types `wf_pick_tab`/`wf_pick_row` (NOT finalize's
`wf_rml_tab` — RegisterStruct is context-global, a 2nd model must not re-register the
same C++ type) under their own guard. sync_rml: page tabs (skip empty pages, `rml_pages`
maps tab→real page index) + rows ("n  >> name (saves)", cursor on `sel`) + the "Pick a
world to enter game" tooltip. Mouse: `on_tab`→selpage (+sel=0), `on_select`→sel (click
only). Pages cap each list to a screen → no scroll-into-view needed. F4 toggle shared
("worldfactory via RmlUi"). **EYEBALL CHECK (user, A/B via F4):** main menu → choose a
world to play → world list with `name (saves)` per row, cursor highlight, UP/DOWN move,
NEXT_TAB/PREV_TAB switch pages (only if >1 page of worlds), CONFIRM enters the world,
QUIT cancels; mouse click selects a row / a page tab. **WATCH:** (a) the auto-return
single-world case (line ~359, `pick_world` returns before opening any UI) is unchanged;
(b) >1 page needs many worlds (>content height) — page tabs appear then.

**SLICE 3 (show_active_world_mods) — CODE-COMPLETE + BUILD-GREEN (binary 15:45), TOGGLE
OFF, EYEBALL OWED, UNCOMMITTED.** Read-only mod list. Introduces the **shared mod-row
builder** `build_wf_mod_rows()` (anon ns): mirrors `draw_mod_list` — flat list of
category headers (magenta) + mod entries ("name [id]", obsolete→dark_gray+`*`,
invalid→red `N/A`, `>> ` cursor) baked to rml markup, returned as a neutral POD
`plain_mod_row` so each model copies into its OWN Rml struct (distinct per-model types
avoid re-registering one C++ type on two models). Model "worldmods" = `wf_amod_row`
rows + title; render-only, keyboard-only (DEFAULT UP/DOWN/QUIT); scroll-into-view on
UP/DOWN (`ScrollAlignment::Nearest`, `#wm-list` child). `worldmods.{rml,rcss}`.
**EYEBALL:** in-game world-mods viewer → active mods with category headers + colours +
cursor; UP/DOWN scroll-follow; QUIT.

**SLICE 4 (show_modselection_window — THE GIANT) — CODE-COMPLETE + BUILD-GREEN
(worldfactory.cpp.o 15:52:30 + binary 15:52:32, 0 warnings), TOGGLE OFF, EYEBALL OWED,
UNCOMMITTED.** The dual-pane mod selector. Model "modselect": worldgen steps (`wtabs`,
`data-if="show_wtabs"` — hidden when standalone) + category tabs (`cats`) + available
list (`avail`) + active load-order (`active`, with `shift_rml` `+`/`-` from
`mman_ui->can_shift_up/down`) + 2 headers (focused list marked `< >`) + description pane
(`mman_ui->get_information(selected)` → cata_text_to_rml, scrollable preview; full text
still via the VIEW_MOD_DESCRIPTION popup) + filter line (live `fpopup->text()` while
editing). Both lists reuse `build_wf_mod_rows` (own struct `wf_mod_row` with shift).
**Render-only, KEYBOARD-ONLY this slice** — all add/remove/reorder (dependency+conflict
resolution via `mman_ui->try_add/try_rem/try_shift`), category-tab cycling, FILTER
(string_input Tier-0), TOGGLE_SHOW_OBSOLETE, SAVE_DEFAULT_MODS, VIEW_MOD_DESCRIPTION,
NEXT/PREV_TAB stay in the loop untouched (no mouse this slice — the index↔mod mapping
through category headers is deferred). Scroll-into-view on UP/DOWN for the FOCUSED list
(`ms-avail`/`ms-active`). `modselect.{rml,rcss}`. **Lighting this ALSO lights
`edit_active_world_mods` (delegates, standalone=true → wtabs hidden) + the wizard Mods
step (`show_worldgen_tab_modselection` delegates).** **EYEBALL (hit edges):** New World →
Mods step: two panes (available category-tabbed | active load order), CONFIRM adds (deps
pulled in), `+`/`-` shift cues on active rows, ADD_MOD/REMOVE_MOD reorders (respecting
can_shift), description pane tracks selection, FILTER narrows + live query, obsolete
toggle, category tabs switch, NEXT_TAB→Finalize; ALSO standalone edit-world-mods (no
worldgen strip) + the conflict/dependency error popups (Tier-0). **WATCH:** keyboard-only
(mouse won't select — expected); the worldgen strip + headers + filter are render-only.

**WORLDFACTORY (Tier 4 #2) = CODE-COMPLETE (all 4 slices, build-green, EYEBALL OWED, one
shared `worldfactory_rmlui_enabled()` toggle).** Wizard fully on RmlUi when the toggle is
on (Mods=slice4, Options=screen#1 slice2, Finalize=slice1) + pick_world (slice2) +
active-mods view (slice3) + edit-mods (slice4 delegate). Next Tier-4: main_menu (#3),
then newcharacter (#4).

**WIZARD-BLEED FIX (eyeball #1 — user saw World Options as old curses + Finalize
overlapping it):** two causes, both fixed. (1) The World Options step
(`get_options().show(false,true,…)`) was gated by the OPTIONS toggle, so with only
"worldfactory via RmlUi" on it drew curses → stale curses bled onto Finalize. Since
`world_options_only==true` happens ONLY in the wizard, that step now rides
`worldfactory_rmlui_enabled()` (options.cpp gate: `world_options_only ?
worldfactory_rmlui_enabled() : options_rmlui_enabled()`) — one toggle lights the whole
wizard. (2) The wizard driver (`make_new_world`) drew the curses worldgen-tab strip into
`wf_win` every frame, bleeding through each step doc's transparent margins (theme `body`
has no background; `.panel` is 80–92%); when `worldfactory_rmlui_enabled()` it now
`werase`s wf_win instead (each step doc draws its own strip). Build-green 16:18.

### Tier 4 screen #3: main_menu (`main_menu.cpp`, 1219) — the title screen

**Anatomy (verified 2026-06-15):** `opening_screen()` (612) owns ONE `ui_adaptor` +
`on_redraw`→`print_menu` (294) + the input loop (714). Two-level selection: `sel1` (top
item: MOTD/NEWCHAR/LOADCHAR/WORLD/SETTINGS/HELP/CREDITS/QUIT via `main_menu_opts`), `sel2`
(sub-option), `sel_line` (MOTD/CREDITS scroll). `print_menu` draws: holiday art + the
`mmenu_title` ASCII logo + version + the horizontal menu row (`print_menu_items` (133):
`[hotkey]label`, selected hilited, builds `main_menu_button_map` mouse hitboxes) +
`display_sub_menu` (185: floating vertical sub-options for NEWCHAR/SETTINGS/WORLD/LOADCHAR,
OR `display_text` (572) renders the scrolled MOTD/CREDITS text) + bottom tip/hints/bugs
line. CONFIRM dispatches: HELP→display_help, QUIT→`query_yn`, SETTINGS→options/keybind/
autopickup/safemode/distraction/colors (all migrated screens), WORLD→`world_tab`,
LOADCHAR→`load_character_tab` (uilist, Tier-0), NEWCHAR→`new_character_tab` (→newcharacter,
Tier 4 #4).

**It's essentially ONE screen** — `display_text`/`display_sub_menu` are part of the title
render; the New/Load/World actions DELEGATE (uilist Tier-0 / worldfactory / newcharacter),
so they're not main_menu's render. → **one slice**, toggle `main_menu_rmlui_enabled()`.

**Slice 1 — title screen (render-behind).** Model "mainmenu": `logo_rml` (mmenu_title
ASCII, white, `white-space:pre`), `version_rml`, `items` (vMenuItems `[hotkey]label`,
`selected`=sel1, horizontal flex), `submenu` (sel2 sub-options for NEWCHAR/SETTINGS/WORLD/
LOADCHAR, vertical; empty for HELP/QUIT), `motd_rml` (scrolled MOTD/CREDITS text when those
are selected, in a scroll-pane), `tips_rml` (bottom hints), `show_motd`/`show_submenu`
flags. Keyboard owns the 2-level nav (LEFT/RIGHT→sel1, UP/DOWN→sel2 or sel_line); mouse
click selects (on_item→sel1, on_sub→sel2). Submenu rendered inline below the menu row
(semantic; NOT the curses floating box at the item's x). Holiday ASCII art + the per-char
mouse hitbox maps dropped (semantic rewrite). `mainmenu.{rml,rcss}`. **VISUAL-FIDELITY
RISK (eyeball-critical, flagged):** the ASCII logo centering/scale + menu spacing +
submenu placement are the iconic title look — build-blind will get structure, expect
layout iteration. **WATCH:** the in-progress theme.rcss CRT rework (uncommitted) will
restyle this most of all.

**SLICE 1 — CODE-COMPLETE + BUILD-GREEN (main_menu.cpp.o 16:53 + binary relinked,
0 warnings), TOGGLE OFF, EYEBALL OWED, UNCOMMITTED.** `mainmenu.{rml,rcss}` model
"mainmenu" + `main_menu_rmlui_enabled()` toggle + F4 "main menu via RmlUi".
`opening_screen` on_redraw gets `if(rml){sync_rml();return;}`; rml_doc dtor tears down.
sync_rml builds logo (mmenu_title pre joined) / version / `items` (vMenuItems
shortcut_text, sel1) / `submenu` (replicates display_sub_menu for SETTINGS/NEWCHAR/
LOADCHAR/WORLD incl. "Create World" + world saves count) / `motd_lines` (foldstring of
mmenu_motd|credits) / `tips_rml` (NEWCHAR hint else bugs+tip-of-day). Keyboard owns the
2-level nav; mouse `on_item`/`on_sub` select (click only). MOTD/Credits keyboard scroll
via `rml_scroll_pending` → ScrollIntoView the sel_line child of `#mm-motd`. Submenu
rendered INLINE below the menu row (semantic, not the curses floating box). Holiday art +
per-char mouse hitboxes dropped. Includes the `<div class="crt">` overlay (user's CRT
theme). **EYEBALL (visual-fidelity critical):** title screen → logo centered, version,
horizontal menu (selected hilited), arrow into New Game/Settings/World/Load shows the
inline submenu (sel2 highlight), MOTD/Credits show scrolling text (UP/DOWN scrolls),
CONFIRM acts (New/Load/World/Settings/Help/Quit all still work), mouse click selects;
QUIT→"Really quit?" (needs query_popup toggle, per invariant 6). **WATCH:** logo
centering/scale + menu spacing + inline-submenu placement vs the curses floating box —
expect layout iteration.

### Tier 4 screen #4: newcharacter (`newcharacter.cpp`, 4337) — MULTI-SCREEN SUB-PROJECT

The biggest + most-coupled Tier-4 screen, plan's explicit LAST. **Architecture (verified
2026-06-16):** the creator is a WIZARD of 8 tabs driven by `avatar::create` (486) — a
`do/while` over `tab` (0-7) that calls each tab fn and steps on its returned
`tab_direction` (FORWARD/BACKWARD/QUIT/NONE). Tab order (driver switch):
0 set_points / 1 set_scenario / 2 set_profession / 3 set_stats / 4 set_traits /
5 set_bionics / 6 set_skills / 7 set_description. **Each tab fn owns its OWN
`ui_adaptor` + `on_redraw` + `input_context` + nested input loop** and draws the shared
`draw_character_tabs(w, sTab)` strip (735) + the points line (`draw_points`, 757) + its
body. So each tab fits the harness INDEPENDENTLY — exactly the worldfactory wizard
shape: each tab's RmlUi doc renders its OWN character-tab strip (the worldfactory
per-doc-strip precedent; no shared strip component yet — revisit if a cleaner 3rd
consumer appears), the driver's own strip is occluded and dies at rip-out.

**Gate:** ONE `newcharacter_rmlui_enabled()` toggle, lit PER-TAB (each on_redraw guards
`if(rml){sync_rml();return;}`), like worldfactory/inventory. Light one tab per slice;
the rest stay curses even with the toggle on. **Invariant 6 applies hard here** — every
tab's PREV_TAB/QUIT fires a `query_yn("Return to main menu?")`, so A/B requires the
query_popup toggle ON too, else the confirm is invisible.

**Slices (simplest→hardest; each its own commit + eyeball):**
1. **set_points** (POINTS tab) — char-tab strip + points line + 1-3 pool options
   (Multiple/Single/Freeform; cursor + chosen-pool green) + description pane. Smallest;
   proves the toggle + char-tab-strip-in-doc + points-line.
2. **set_stats** — 4 stats with +/- and a live description/effects pane.
3. **set_skills** — skills list (level +/-) + description.
4. **set_traits** — good/bad trait columns + description + points interplay.
5. **set_bionics** — CBM list + description.
6. **set_scenario** — scenario list + description + flags/professions/start-location.
7. **set_profession** — profession list + description + items/skills/addictions sub-panes
   (heaviest list tab).
8. **set_description** — the OVERVIEW form: name/gender/height/age/blood + scenario/
   profession summary + reroll/save (most fields; last).

**SLICE 1 (set_points) — DONE + EYEBALLED CLEAN (user 2026-06-16, "looks great"),
COMMITTED. TOGGLE OFF.** New `newcharacter_rmlui_enabled()` toggle (one for the whole creator,
gated per-tab) + `data/gui/newcharpoints.{rml,rcss}` model "newcharpoints" + F4 "new
character" checkbox (System menus group). `set_points` gets a render-only RmlUi path:
anon-ns `nc_points_session` (8-tab strip `tabs` + `points_rml` + `opts` + `desc_rml`) +
`build_nc_char_tabs(active)` helper (the 8 translated captions, POINTS=0 selected,
shared by later tabs) + `register_nc_points_rml_types` + `rml_doc rml` + `sync_rml`
(rebuilds all four each redraw: points via `points.to_string()`, each opt name baked
green when chosen via `colorize(...,COL_SKILL_USED)` else light_gray, `selected`=cursor,
desc = highlighted opt's text). on_redraw `if(rml){sync_rml();return;}` else curses;
`rml.open` after the ctxt/on_redraw, before the loop; rml_doc dtor tears down at the
single return. STRUCTURAL POINTS: (1) clones the worldfinalize template (sized+centered
body, `.tab{flex:0 0 auto}` so 8 captions size to content, `.nc-opt{display:block}` for
the inline-row gotcha). (2) cursor `.selected` (CSS accent) + chosen-pool colour baked
into the text — both cues, mirroring options/mutations. (3) keyboard still owns
UP/DOWN/CONFIRM/NEXT_TAB/PREV_TAB/QUIT untouched; render-only doc. F4 toggle "new
character" (OFF). **EYEBALL CHECK (user, A/B via F4 — needs query_popup toggle ON too,
invariant 6):** New Game → Custom Character → POINTS tab: char-tab strip shows the 8
steps with POINTS current; the points line shows the pool totals; the pool options
(Multiple pools / Single pool / Freeform, per CHARACTER_POINT_POOLS) list with the
CHOSEN pool green and the cursor row highlighted; UP/DOWN move the cursor; CONFIRM sets
the pool (green moves); the description pane tracks the highlighted option; NEXT_TAB →
Scenario (curses, until slice 6); PREV_TAB/QUIT → "Return to main menu?" confirm (RmlUi
popup if query_popup ON). **WATCH:** (a) only ONE option exists when point_pool ==
"multi_pool" (list shows a single row — expected). (b) the other 7 tabs stay curses
(gate proof). (c) first newcharacter dynamic doc — D3D12 (Win11) glance warranted.

**SLICE 2 (set_stats) — DONE + EYEBALLED CLEAN (user 2026-06-16, "looks great"),
COMMITTED. TOGGLE OFF.** The STATS
tab on the shared `newcharacter_rmlui_enabled()` toggle (2nd tab lit; the other 6 stay
curses). `data/gui/newcharstats.{rml,rcss}` model "newcharstats". REFACTOR: lifted
`build_nc_char_tabs` to a TEMPLATE on the tab struct so each tab's model uses its OWN
registered C++ type (`nc_stats_tab` distinct from slice-1's `nc_rml_tab`) — distinct
types avoid re-registering one struct on two models (worldfactory precedent); the
caption logic is shared. Render-only doc: char-tab strip (STATS=idx3) + points line +
the 4 stats (Str/Dex/Int/Per as `nc_stat_row{name,val,selected}`, active stat coloured
COL_STAT_ACT + `.selected`, others light_gray) + per-stat effects/description
(`nc_stat_desc(u,sel)` — a free builder mirroring the curses per-stat block verbatim,
incl. the `u.recalc_hp()` side-effect for Str; joined colour-tagged lines via `join`) +
the HIGH_STAT cost warning (exact curses strings, red, selected stat only) + keybinding
hints. on_redraw `if(rml){sync_rml();return;}` else curses; `rml.open` after on_redraw;
rml_doc dtor tears down at each `return` (the loop returns directly). Keyboard owns
UP/DOWN select, LEFT/RIGHT dec/inc (incl. the >HIGH_STAT 2-point cost), RANDOMIZE,
NEXT/PREV_TAB, QUIT — all untouched. F4 toggle "new character" (shared). **EYEBALL CHECK
(user, A/B via F4 — query_popup toggle ON too, invariant 6):** New Game → Custom →
STATS tab: char-tab strip (STATS current), points line, the 4 stats with the SELECTED
one highlighted (COL_STAT_ACT) + cursor accent; UP/DOWN move selection; LEFT/RIGHT
decrease/increase the selected stat (value + points update); RANDOMIZE jumps selection;
the description pane shows the selected stat's effects (HP/carry/melee for Str,
to-hit/throw/ranged for Dex, read/rust/craft for Int, aim/nightvis for Per) matching
curses; raising a stat past HIGH_STAT shows the red "Increasing X further costs 2
points." line + costs 2; NEXT_TAB → Profession (curses), PREV_TAB → Points (RmlUi).
**WATCH:** (a) the effects NUMBERS must match curses exactly (the desc is a from-scratch
reconstruction — diff a few values). (b) value column right-alignment. (c) the cost
warning only shows for the selected stat at/above HIGH_STAT.

**SLICE 3 (set_skills) — DONE + EYEBALLED CLEAN (user 2026-06-16, "looks great"),
COMMITTED. TOGGLE OFF.** The SKILLS
tab on the shared `newcharacter_rmlui_enabled()` toggle (3rd tab lit; the other 5 stay
curses). HEAVIEST newcharacter tab so far. `data/gui/newcharskills.{rml,rcss}` model
"newcharskills". Render-only doc: char-tab strip (SKILLS=idx6) + points line +
upgrade-cost hint (`Upgrading X by N level(s) costs M point(s)`, green if affordable
else red) + a CATEGORY-GROUPED skill list (`nc_skill_row{text,is_header,selected}`:
yellow category headers + skill rows showing name + `(level)` if >0 + `(+prof)` bonus,
COL_SKILL_USED when level>0 else light_gray, cursor = `.selected`) + a scrollable
description pane (the selected skill's description + the recipes it unlocks). STRUCTURAL
POINTS: (1) the recipe-desc is a parallel free builder `nc_skill_recipes_desc(u,skill,
prof_skills)` mirroring the curses recipe-gathering block VERBATIM (brown for the skill's
own recipes, gray for cross-skill) — curses path left intact for the A/B (armor_layers
precedent). (2) **list scroll-follow** reuses the PROVEN options idiom: UP/DOWN/RANDOMIZE
set `rml_scroll_pending`, sync_rml `ScrollIntoView(Nearest)` the cursor skill's flattened
row (`rml_sel_child`) in `#nc-skill-list` — needed because the skill list is long and
curses always kept the cursor in view. (3) **desc-pane scroll**: SCROLL_UP/DOWN branch
to `SetScrollTop` on `#nc-skill-desc` (±0.15 page) in rml mode vs the curses fold offset.
(4) skill rows reuse theme `.item`/`.item.selected` (yellow accent); headers add a
`.header` class that neutralizes the `.item` background (plain coloured label, matching
curses). on_redraw `if(rml){sync_rml();return;}` else curses; `rml.open` after on_redraw;
rml_doc dtor tears down at each `return`. Keyboard owns all nav/inc/dec/randomize/scroll.
F4 toggle "new character" (shared). **EYEBALL CHECK (user, A/B via F4 — query_popup
toggle ON too):** New Game → Custom → SKILLS tab: char-tab strip (SKILLS current), points
line, upgrade-cost hint (green/red by affordability), the skill list grouped under yellow
category headers, each skill with its level + profession `(+N)` bonus; UP/DOWN move the
cursor (**list scrolls to follow — the make-or-break check on this long list**); LEFT/
RIGHT raise/lower the selected skill (level + cost + points update; remember level-0→1
gives 2 free levels); RANDOMIZE jumps; the desc pane shows the skill's description +
unlockable recipes (brown for its own, gray for cross-skill); SCROLL_UP/DOWN scroll the
desc pane; NEXT_TAB → Traits (curses), PREV_TAB → Stats (RmlUi). **WATCH:** (a) the
recipe list is a from-scratch reconstruction — spot-check a skill's unlocked recipes vs
curses. (b) RANDOMIZE in curses does NOT refresh `currentSkill` until the next UP/DOWN
(latent curses quirk preserved for A/B — the desc/cost may lag one keypress after
randomize in BOTH paths). (c) cursor-follow on the long list (if it doesn't track, the
staged ScrollIntoView is already wired — flag if it over/under-scrolls).

**SLICE 4 (set_traits) — CODE-COMPLETE + BUILD-GREEN (newcharacter.cpp.o 12:00:57 +
binary relinked 12:00:59, 0 errors), TOGGLE OFF, EYEBALL OWED, UNCOMMITTED.** The TRAITS
tab (4th tab lit). `data/gui/newchartraits.{rml,rcss}` model "newchartraits". 3-COLUMN
multi-pane (good/bad/neutral; neutral hides when empty via `data-if="show_col2"`) — each
column's rows BAKED as a markup string (the advinv-2b "no nested data-for" primitive,
because the row struct `trait_entry` is function-local) and emitted via flat `data-rml`
per column. Only the WORKING column's cursor row gets `.selected`. Render-only doc:
char-tab strip (TRAITS=idx4) + top bar (points + good/bad budget `G/max B/-max` when
non-freeform + the working trait's `costs/earns N points` line in the trait colour) +
the 3 columns + a description footer (working trait desc). Per-trait colour replicates
the curses on/off × active/passive × conflict/forbidden matrix (COL_TR_* consts) inline
in sync (the colour state is function-local). num_good/num_bad update live on CONFIRM.
on_redraw `if(rml){sync_rml();return;}` else curses; `rml.open` before on_redraw (anchor
uniqueness); rml_doc dtor tears down at each `return`. Keyboard owns LEFT/RIGHT (switch
column), UP/DOWN, CONFIRM (toggle, with all the dependency/conflict/budget `popup()`
errors — curses popups, invariant 6), RANDOMIZE, REROLL_*, NEXT/PREV_TAB, QUIT.
**DEFERRED (NOT bugs):** (1) the tile `character_preview` overlay is NOT drawn in rml
mode (out of scope like the AIM minimap; zoom/clothes keys still no-op safely). (2) NO
keyboard scroll-into-view on long columns (the mutations multi-column precedent — baked
`data-rml` columns make per-row ScrollIntoView timing unproven; flag if cursor gets lost
on a long good/bad list). F4 toggle "new character" (shared). **EYEBALL CHECK (user, A/B
via F4 — query_popup toggle ON too):** New Game → Custom → TRAITS: char-tab strip
(TRAITS current), points + good/bad budget, 3 columns (good/bad/neutral) with per-trait
colours matching curses (taken traits brighter, conflicting/forbidden greyed); LEFT/RIGHT
switch the working column (only its cursor highlights), UP/DOWN move, CONFIRM toggles a
trait (budget + colours update; conflict/forbidden/over-budget show the curses popup),
the cost line + description track the working trait; NEXT_TAB → Bionics (curses),
PREV_TAB → Skills (RmlUi). **WATCH:** (a) no character preview in rml mode (expected).
(b) long column cursor-follow (deferred — flag if lost). (c) the neutral column appears
only when traits with 0 points exist.

**SLICE 5 (set_bionics) — CODE-COMPLETE + BUILD-GREEN (binary relinked 12:04:41, 0
errors), TOGGLE OFF, EYEBALL OWED, UNCOMMITTED.** The BIONICS tab (5th lit). STRUCTURAL
CLONE of slice 4 (set_bionics is line-for-line the TRAITS tab with bionic data) —
`data/gui/newcharbionics.{rml,rcss}` model "newcharbionics" (distinct `nc_bionics_tab`
type; rml/rcss reuse the `nc-trait-*` classes). Same 3-column baked-markup render +
budget + cost line + desc footer; per-bionic colour via the same COL_TR_* matrix; row
text `bio.name.translated()`, desc `bio.description.translated()`. Keyboard owns all
toggle/nav/reroll; tile preview not drawn in rml. F4 toggle "new character" (shared).
**EYEBALL CHECK (user, A/B via F4 — query_popup ON):** New Game → Custom → BIONICS: same
3-column shape as Traits but bionics (good/bad/neutral), colours/budget/cost/desc match
curses; LEFT/RIGHT switch column, UP/DOWN move, CONFIRM toggles a CBM (conflict/forbidden
popups), NEXT_TAB → Skills (RmlUi), PREV_TAB → Traits (RmlUi). Same deferred gaps as
Traits (no tile preview, no long-list scroll-follow).

**SLICE 6 (set_scenario) — CODE-COMPLETE + BUILD-GREEN (binary relinked 12:09:44, 0
errors), TOGGLE OFF, EYEBALL OWED, UNCOMMITTED.** The SCENARIO tab (6th lit).
`data/gui/newcharscenario.{rml,rcss}` model "newcharscenario". Single scenario list
(left, `data-for` rows + scroll-follow via the proven options ScrollIntoView idiom) + a
right info pane combining sort indicator + professions (count + default w/ point cost) +
location + vehicle + flags (all rebuilt as one colour-tagged `info_rml` string mirroring
the curses w_profession/w_location/w_vehicle/w_flags blocks verbatim) + a top bar (points
w/ netPointCost +/- + the scenario cost line, green if can_pick else red) + a description
footer (green desc, red CITY_START-unavailable note) + a filter indicator footer.
Per-row colour: current scenario COL_SKILL_USED, CITY_START-unavailable c_dark_gray, else
c_light_gray; cursor = `.selected`. on_redraw `if(rml){sync_rml();return;}` else curses;
`rml.open` after on_redraw; rml_doc dtor at the single return. Keyboard owns UP/DOWN,
CONFIRM (pick scenario → resets profession), SORT (points/name), FILTER (string_input
Tier-0 popup → recalc), RANDOMIZE, NEXT/PREV_TAB, QUIT. F4 toggle "new character"
(shared). **EYEBALL CHECK (user, A/B via F4 — query_popup ON):** New Game → Custom →
SCENARIO: char-tab strip (SCENARIO current), scenario list (current scenario green,
city-locked greyed), cursor scroll-follows UP/DOWN; the right pane shows sort + default
profession (+cost) + location (N locations, M variants) + vehicle + flag lines matching
curses; the cost line is green/red by affordability; the description shows below (red
note for city-locked); SORT toggles points/name + re-sorts; FILTER popup narrows the list
+ the `<filter>` indicator updates; CONFIRM picks; NEXT_TAB → Profession (curses),
PREV_TAB → Points (RmlUi). **WATCH:** (a) info pane is a from-scratch reconstruction —
spot-check professions/location/vehicle/flags vs curses. (b) FILTER popup renders over
the doc + the empty-result "Nothing found." popup (invariant 6). (c) cursor scroll-follow
on a long scenario list.

**SLICE 7 (set_profession) — CODE-COMPLETE + BUILD-GREEN (binary relinked 12:14:42, 0
errors), TOGGLE OFF, EYEBALL OWED, UNCOMMITTED.** The PROFESSION tab (7th lit; heaviest
list tab). `data/gui/newcharprofession.{rml,rcss}` model "newcharprofession". Same shape
as SCENARIO (list left + scroll-follow, top bar points/cost, desc footer, filter, sort)
PLUS: (1) a big scrollable INFO BUFFER (`info_rml`, `#nc-prof-info`) rebuilt verbatim
from the curses `w_items` buffer — addictions/traits/skills(category-grouped)/items
(wielded/worn/inventory)/bionics/pets/vehicle/spells/money/companions; LEFT/RIGHT scroll
it via `SetScrollTop` (vs the curses `desc_offset`/print_scrollable). (2) a gender-swap
line (`gender_rml`, CHANGE_GENDER). Per-row colour: current profession COL_SKILL_USED
else c_light_gray; cursor `.selected`. Keyboard owns UP/DOWN (list, resets scroll +
scroll-follow), LEFT/RIGHT (buffer scroll), CONFIRM (pick → swaps traits/age), SORT,
FILTER (Tier-0 popup), CHANGE_GENDER (re-sorts), RANDOMIZE, NEXT/PREV_TAB, QUIT. Tile
preview not drawn in rml mode. F4 toggle "new character" (shared). **EYEBALL CHECK (user,
A/B via F4 — query_popup ON):** New Game → Custom → PROFESSION: char-tab strip
(PROFESSION current), profession list (current green, cursor scroll-follows), cost line
green/red; the right buffer shows items/skills/traits/bionics/etc matching curses,
LEFT/RIGHT scroll it; sort + gender-swap lines present; CONFIRM picks (description +
points update), CHANGE_GENDER flips gender + re-sorts + relabels, SORT toggles, FILTER
narrows; NEXT_TAB → Stats (curses), PREV_TAB → Scenario (RmlUi). **WATCH:** (a) the info
buffer is a big from-scratch reconstruction — spot-check items/skills/bionics vs curses.
(b) LEFT/RIGHT scroll the buffer (not move the list). (c) cursor scroll-follow on a long
profession list. (d) FILTER + "Nothing found" popups over the doc (invariant 6).

**SLICE 8 (set_description) — DONE + EYEBALLED CLEAN (user 2026-06-16), COMMITTED.
TOGGLE OFF. The LAST newcharacter tab → newcharacter (Tier 4 #4, the last giant) is now
COMPLETE across all 8 slices.** (Build note: fresh mtime verified — newcharacter.cpp.o
12:31:49 / bin 12:31:52; the first `cmake --build | rtk err` falsely reported success
without recompiling, caught via the build-verify-mtime gotcha.) The OVERVIEW tab (8th
lit) — the final summary form. `data/gui/newchardescription.{rml,rcss}` model
"newchardescription". RENDER-ONLY doc: char-tab strip (OVERVIEW=idx7) + points line + an
edit row (name / gender / height / age / location) + scenario/profession lines + SIX
read-only summary panes (stats / skills / traits / bionics+spells / misc / gear, flex-wrap
row, each its own `.scroll-pane`) + a keybinding guide footer. STRUCTURAL POINTS: (1) every
pane is ONE colour-tagged string built by mirroring the curses block verbatim then
`cata_text_to_rml` (the profession `info_rml` approach, replicated per-pane — no row
structs; the only registered type is the shared `nc_desc_tab` for the strip). (2) the
three EDITABLE fields (name/height/age) bake their selector highlight into the string: a
`> ` marker + bright `c_white` label when `current_selector` points at them, `  `/
`c_light_gray` otherwise (no extra bound bools/CSS — mirrors the curses `h_light_gray`
cue). (3) bionics pane built ONCE (curses draws it twice — a latent curses quirk; the
semantic rewrite drops the redundant second draw). (4) `sync_rml` uses
`DirtyAllVariables()` (many panes; cheaper to write than 16 per-var dirties). (5) all
editing stays keyboard + the migrated string_input_popup (name/age/height edit) +
select_location uilist (Tier-0) + the curses popups; render-only doc, `rml.open` after
on_redraw, rml_doc dtor tears down at each `return` (the loop returns directly).
(6) character_preview tile overlay NOT drawn in rml mode (out of scope, like prior tabs;
zoom/clothes keys no-op safely). F4 toggle "new character" (shared). **EYEBALL CHECK
(user, A/B via F4 — query_popup toggle ON too, invariant 6):** New Game → Custom →
OVERVIEW (the last tab, reached via NEXT_TAB from Skills or by completing the wizard):
char-tab strip (OVERVIEW current), points line; the edit row shows Name / Gender (Male
cyan / Female pink per sex) / Height / Age / Starting location; LEFT/RIGHT cycle the
active field (the `> ` marker + bright label move between Name → Height → Age), UP/DOWN
change height/age, CONFIRM opens the edit popup (name string / age+height int), CHANGE_GENDER
flips Male/Female, CHOOSE_LOCATION opens the uilist; the six panes show
stats/skills(category-grouped)/traits(display colours)/bionics+spells/misc(vehicle/
companions/cash/pets/addictions)/gear(wielded/worn/inventory) matching curses;
RANDOMIZE_CHAR_DESCRIPTION rerolls name/age/height/sex; SAVE_TEMPLATE + REROLL_* work;
NEXT_TAB → "Are you SURE you're finished?" confirm (RmlUi popup if query_popup ON) →
finishes creation, PREV_TAB → Skills (RmlUi). **WATCH:** (a) the six panes are from-scratch
reconstructions — spot-check stats numbers, skill levels (+prof bonus), trait/bionic lists,
gear groups vs curses. (b) the editable-field highlight (`> ` marker) tracks LEFT/RIGHT.
(c) empty cases (no traits/bionics/skills/items → "None!"). (d) the finish/abort popups
render over the doc (invariant 6). (e) first time the OVERVIEW dynamic doc renders —
D3D12 (Win11) glance warranted (last-tier-first cross-check).

### Tier 4 #4 (newcharacter) status (2026-06-16): CODE-COMPLETE, all 8 slices

Slices 1-3 (points/stats/skills) eyeballed-clean + committed. Slices 4-7
(traits/bionics/scenario/profession) committed build-blind (git
`20dc0dd2c2`..`996bbafa6c`); slice 8 (description) committed this session.
**EYEBALL: cleared 2026-06-16** — the user reached the OVERVIEW tab (idx7, last in the
wizard) with the shared toggle ON, walking the full creator (scenario→profession→stats→
traits→bionics→skills→overview all rendering RmlUi en route) and confirmed clean. With
newcharacter done, **all four Tier-4 giants
(worldfactory / main_menu / options / newcharacter) are CODE-COMPLETE** — the largest
unpriced-risk bucket from the re-scope gate is cleared. Remaining toward the §8 rip-out:
Tier 5 (npctalk/ranged/iexamine/trade), Tier 6 (overmap + on-map text §7), Tier 7
(sidebar HUD), Tier 8 (F4→RmlUi), Tier 9 (minigames), plus the deferred faction (Tier 2)
and the per-slice eyeball debt.

### Tier 5 progress (interaction dialogs)

- **Tier 5 screen #1: trade (trading_window::perform_trade) — CODE-COMPLETE +
  BUILD-GREEN (trade_win.cpp.o 13:07:51 + binary relinked 13:08:28, 0 errors, fresh
  mtime verified), TOGGLE OFF, EYEBALL OWED, UNCOMMITTED.** First Tier-5 screen; the
  NPC trade dialog. `data/gui/trade.{rml,rcss}` model "trade" + `trade_rmlui_enabled()`
  toggle (rml_screen.h decl + def in trade_win.cpp) + F4 "trade" checkbox (World
  interaction group). Render-only doc mirroring `update_win`: head bar (title /
  credit-debt cost coloured green=accept|red / category ON|OFF toggle / keybind hints)
  + TWO item panes (theirs left | yours right) each with an inventory header +
  weight/volume used/max stats line + column header + a row list + filter/page footer;
  an item-info pane shows below when TOGGLE_ITEM_INFO is on; the focused pane border
  brightens to accent (`data-class-active` on `them_focus`/`you_focus`). STRUCTURAL
  POINTS: (1) **renders the SAME visible page as curses, NOT a native-scroll-all list**
  — trade uses page-relative single-char hotkeys (a-z A-Z per page) that the input loop
  reads, so showing all rows would desync the displayed letters; the doc renders
  `[offset, offset+entries_per_page)` and the cursor is always on-page (also sidesteps
  the scroll-follow problem). (2) each row is ONE monospace-aligned colour-tagged string
  (Terminus font → space-padding aligns columns): `keychar selmark name | amt weight vol
  price`, name/qty/weight/vol in the item colour, price in its own ratio colour
  (green/red by buy/sell delta); category headers are `header`-flagged rows (magenta, no
  accent). (3) cursor + category-mode hilite → `.trade-row.selected` (CSS accent), NOT
  baked `hilite()`. (4) `sync_rml` recomputes both panes each redraw mirroring
  update_win's column-width / category / stats logic (curses `update_win` left intact for
  the A/B — armor_layers precedent). (5) ALL interaction stays keyboard + the migrated
  string_input_popup (filter) — render-only doc; on_redraw guards `if(rml){sync_rml();
  return;}`, `rml.open` after on_redraw before the first redraw, rml_doc dtor tears down.
  F4 toggle "trade" (OFF). **EYEBALL CHECK (user, A/B via F4 — string_input toggle ON for
  the filter, invariant 6):** trade with an NPC (or shopkeeper): two panes (their inv /
  yours), each row shows hotkey + selection mark (-/#/+) + name + amt/weight/vol/price
  with prices coloured by deal quality; the head shows "Trading with <npc>" + the
  Credit/Debt/Exchange cost (green if they'll accept, red if not) + category toggle;
  SWITCH_LISTS moves focus (border accent flips), UP/DOWN move the cursor (page-relative
  letters stay correct), letter hotkeys + LEFT/RIGHT adjust quantities, CONFIRM trades,
  AUTOBALANCE, FILTER (popup narrows the pane + the footer shows the filter + Page x/y),
  CATEGORY_SELECTION highlights a whole category, TOGGLE_ITEM_INFO shows the description
  pane. **WATCH:** (a) column alignment depends on the monospace font — if a pane's
  numbers don't line up, the baked padding is off. (b) per-pane stats (weight/vol
  used/max) match curses; shopkeeper's own pane hides them. (c) the EXAMINE popup
  (`show_item_data`) is a separate nested curses screen — still its own path this slice
  (flag if it reads badly over the doc). (d) first Tier-5 dynamic doc → D3D12 (Win11)
  glance warranted.

- **Tier 5 screen #2: vending machine (iexamine::vending) — CODE-COMPLETE +
  BUILD-GREEN (iexamine.cpp.o 13:37:30 + binary relinked 13:46:40, user-confirmed
  clean), TOGGLE OFF, EYEBALL OWED, COMMITTED.** The ONE bespoke iexamine screen (the
  rest of iexamine.cpp is popups/uilists, Tier-0 covered). `data/gui/vending.{rml,rcss}`
  model "vending" + `vending_rmlui_enabled()` toggle + F4 "vending" checkbox.
  Render-only doc: left pane = "Money left: $X" header + item list (count digit + name,
  coloured via color_in_inventory, cursor `.selected`); right pane = item-info
  (name+price header + `info_string()` folded description). Curses path (paged,
  cursor-centered, scrollbar) left intact; the RmlUi list shows all items + native
  scroll (the list is short — a vending machine — so no page/scroll-follow needed).
  Keyboard owns UP/DOWN (wrap), CONFIRM (buy, afford-check popup), QUIT. **EYEBALL
  CHECK (user, A/B via F4):** examine a vending machine: money header, item list with
  count+name coloured + cursor highlight, UP/DOWN wrap the cursor, the info pane shows
  the selected item's name/price + description, CONFIRM buys (money drops, item leaves;
  "can't afford" popup when broke), empty machine shuts down.

- **Tier 5 screen #3: npc dialogue (dialogue::opt / dialogue_window) — CODE-COMPLETE +
  BUILD-GREEN (npctalk.cpp.o 14:01:05 + dialogue_win.cpp.o 13:58:33 + binary relinked
  14:01:10, 0 errors, fresh mtime), TOGGLE OFF, EYEBALL OWED, COMMITTED.** The Tier-5
  modal giant (the talk tree). `data/gui/dialogue.{rml,rcss}` model "dialogue" +
  `dialogue_rmlui_enabled()` toggle + F4 "npc dialogue" checkbox. Render-only doc:
  header (Dialogue: <name>) + left history pane (the exchanged words, last two messages
  white / rest grey) + right response pane ("Your response:" + lettered option list,
  selected option `.selected`) + keybind hints ([L]ook/[S]ize/[Y]ell/[O]pinion).
  STRUCTURAL POINTS: (1) **dialogue::opt drives input via the RAW `inp_mngr`, NOT an
  input_context** — so a THROWAWAY `input_context dlg_ctxt` is passed to `rml.open()`
  only for the harness 16ms tick (computer_session precedent); the real keys stay on the
  raw loop, the doc is render-only so the tick not driving input is harmless. (2) the doc
  is local to `opt()` → opened + torn down PER dialogue line (each topic is a fresh
  opt() call); the single-instance guard releases on the rml_doc dtor each return, so
  reopen is clean. (3) history is private to `dialogue_window` → added a public
  `history_markup()` getter (mirrors print_history's last-two-white highlight) that the
  sync feeds through cata_text_to_rml; the curses path (print_header/display_responses)
  is left intact for the A/B. (4) curses paging of responses → native scroll (all
  responses shown, selected highlighted; UP/DOWN still wrap via the raw loop, PAGE keys
  still work). F4 toggle "npc dialogue" (OFF). **EYEBALL CHECK (user, A/B via F4 —
  query_popup ON for the hostile/helpless confirms, invariant 6):** talk to an NPC: the
  header shows "Dialogue: <name>", the left pane shows the conversation history (newest
  exchange brighter), the right pane lists lettered responses with the selected one
  highlighted; UP/DOWN move the selection (wrap), letter keys + Enter pick, PAGE_UP/DOWN
  scroll long response sets, [L]/[S]/[Y]/[O] special actions still fire; a response with
  a hostile/helpless consequence shows the curses confirm (needs query_popup ON).
  **WATCH:** (a) doc reopens each dialogue line — confirm no flicker / stuck guard across
  a multi-line conversation (the reopen-per-topic path). (b) history grows correctly as
  the conversation proceeds (newest at the bottom; scroll if long). (c) first dialogue
  dynamic doc → D3D12 (Win11) glance.

### Tier 5 status: COMPLETE (modal screens); ranged DEFERRED to Tier 6

trade + vending + npc dialogue all CODE-COMPLETE + build-green + committed (eyeball owed,
batched). **ranged/targeting is the ONLY remaining Tier-5 screen and is intentionally
DEFERRED to Tier 6** — it is a live overlay tied to the map (world→screen projection),
which the plan sequences AFTER the §7 world-text / Tier-6 overlay layer exists. So the
modal Tier-5 work is done; ranged rides Tier 6.
**UPDATE (2026-06-17): the §7 world-text layer now EXISTS (slices 4.1/4.2a committed),
so ranged is UNBLOCKED.** It is the last interactive modal still on curses (no
`*_rmlui_enabled` toggle, `ranged.cpp` has zero rml wiring) — see the top frontier
banner. It is the natural next migration unit before the Tier 7/8/9 architectural tiers.

**RANGED SCOPING (2026-06-17, pre-build):** bigger than a clean modal — it is
giant-class. `target_ui` (ranged.cpp:387) has TWO render parts: (a) `draw_terrain_overlay()`
via a `game::draw_callback_t` — the aim line / cursor / trajectory drawn ON the map
(world→screen); this is SPRITE-PATH rendering, OUT OF SCOPE (stays like §7, dies/stays
with the map path, not curses-text UI). (b) **`draw_ui_window()` (3905) → the `w_target`
side info panel — the RmlUi target.** It composes ~10 `panel_*` helpers (draw_window_title /
draw_help_notice / draw_controls_list / panel_cursor_info / panel_gun_info / panel_recoil /
panel_spell_info / target-info…) with per-TargetMode variation (Fire / Throw / Reach /
TurretManual / Turrets / Spell / Shape). The target-info section calls the GENERIC
`Creature::print_info(w_target,…)` (ranged.cpp:4254) → so ranged needs the deferred
**generic `Creature::print_info` → lines component** (3 overrides: Character / monster /
npc), the same one faction wanted (faction used the faction-specific `*_faction_display`
text instead; ranged uses the generic compact print_info). The panel is ONE window
rendered per frame → NOT section-sliceable by toggle (all-or-nothing, like faction).
**Unit = the whole `draw_ui_window` panel + the print_info lines component**, done as one
careful build-blind pass (faction precedent: parallel `_lines()` producers, curses
`print_info`/`panel_*` left pristine for A/B). Sized ≈ faction-plus.

**RANGED PROGRESS (2026-06-17):**
- **PIECE 1 — generic `Creature::print_info_text()` lines component — DONE + COMMITTED
  `228d962992`, build-green.** Virtual on Creature (empty default) + monster/npc overrides
  (Character not a target → inherits empty). Faithful to each curses `print_info` (verified
  against source incl. npc's visibility_cap formula); curses paths pristine. No consumer
  yet — foundation for piece 2 + later look-around.
- **PIECE 2 — the `w_target` panel (`draw_ui_window`) — NOT STARTED (next ranged unit).**
  Bigger than faction: `draw_ui_window` (ranged.cpp:3905) = border + `draw_window_title` +
  `draw_help_notice` + ~7 CONDITIONAL positional sections tracked by `text_y`:
  `panel_cursor_info`; then per-mode `panel_gun_info`+`panel_recoil` (Fire/TurretManual/
  Shape-gun) OR `panel_spell_info` (Spell); `panel_target_info` (→ now `print_info_text`);
  then `panel_turret_list` (Turrets) OR `panel_fire_mode_aim` (Fire) OR `draw_throw_aim`
  (Throw); `draw_controls_list` (unless narrow). Three layout variants (`compact`/`narrow`/
  `tiny`) + 7 TargetModes. The sub-panels (`panel_fire_mode_aim`, `draw_throw_aim`,
  `panel_turret_list`) carry their own intricate logic → each needs a faithful text
  producer. This is a dedicated session-sized reproduction; piece 1 deliberately landed
  first so piece 2 starts from a proven, green foundation. Approach when resumed: parallel
  `*_text()`/lines producers per panel section, curses `panel_*` pristine, one RmlUi doc
  (`ranged.{rml,rcss}`) + `ranged_rmlui_enabled()` toggle + F4, render-only (keyboard owns
  aim/fire loop; the map overlay `draw_terrain_overlay` stays curses/sprite).

  **DEEP DECOMPOSITION (mapped 2026-06-17 — piece 2 is itself a multi-commit sub-project,
  NOT one pass):** `draw_ui_window` orchestrates section producers, of which the AIM READOUT
  is a 4-deep drawing-function tree and the rest are shallow:
  - **Shallow (direct `*_text()` producers, low risk):** `uitext_title` (already a string
    fn), `draw_help_notice`, `panel_cursor_info` (range/elev/targets), `panel_gun_info`
    (firing mode + ammo), `panel_recoil`, `panel_spell_info` (cost/fail/aoe/damage/desc),
    `panel_target_info` (→ `print_info_text` ✓ done; + the infrared/specials `describe_*`
    buf branches), `panel_turret_list`, `draw_controls_list` (keybinds).
  - **DEEP — the aim readout (numerically critical, MUST be eyeballed):**
    `panel_fire_mode_aim` → `print_aim` (ranged.cpp:2157) → **`print_steadiness`** (bar) +
    **`print_ranged_chance`** (the `*`/`+`/`|` confidence + hit-chance table); plus
    `draw_throw_aim` (ranged.cpp:2205, the parallel Throw tree). Shared static drawing fns
    with intricate bar/table layout + confidence math — each needs its own lines-extraction,
    and the hit-chance/confidence numbers are the whole point.
  **SLICE 2a — DONE + BUILD-GREEN (ranged.cpp.o 10:41:18 newer than source; binary relinked
  10:41:21), TOGGLE OFF, EYEBALL OWED (partial — aim readout stubbed), COMMITTED `3109aa26b4`.**
  `target_ui::panel_text()` builds draw_ui_window's shallow sections (title / cursor /
  gun+recoil OR spell / target-info via `print_info_text` / turret list / controls) as one
  colour-tagged `body_rml` string (→ cata_text_to_rml in sync). New `data/gui/ranged.{rml,rcss}`,
  `ranged_rmlui_enabled()` toggle + F4 "ranged targeting (2a)". Render-only: branch in run()'s
  on_redraw (`if(rml){sync;return;}`); curses `draw_ui_window`/`panel_*` pristine; map overlay
  untouched. The aim/hit-chance readout is a WIP stub line (slice 2b). **NOT yet eyeball-complete
  — 2b must land the aim readout before a real A/B; 2a only de-risks the scaffold + shallow text.**
  **RECOMMENDED SLICES:** (2a ✓) shallow sections + panel scaffold + doc/toggle/F4, aim section
  stubbed to a clearly-marked WIP line (toggle OFF → safe) = first buildable commit;
  (2b) extract `print_steadiness`+`print_ranged_chance`→lines + wire the Fire aim readout =
  the keystone eyeball (hit-chance numbers vs curses, number-for-number); (2c) `draw_throw_aim`
  readout for Throw/ThrowBlind. **Most eyeball-sensitive screen in the migration — do NOT
  land the aim readout build-blind without a number-for-number A/B.**
  **SLICE 2b — DONE + BUILD+LINK-GREEN on a clean tree (Metal; binary relinked 2026-06-22),
  curses draw fns DELETED, EYEBALL OWED (the keystone A/B), COMMITTED batch 14b.** Earlier the
  producers were wired but the curses `print_steadiness`/`print_ranged_chance`/`print_aim`/
  `draw_throw_aim` were left in place pending parity confirmation + a full link (the tree then
  carried in-flight lighting changes). This session: clean tree, full link verified, and those
  4 now-dead curses fns DELETED (−268 lines; shared helpers `confidence_estimate`/`get_colored_bar`/
  `calculate_aim_cap`/`get_default_aim_type`/`outside_visible_z_range`/`pl_sees` kept — they feed
  the live producers). The 4 parallel `*_lines()` producers are now the sole readout path:
  `steadiness_line(bar_width,steadiness)`,
  `ranged_chance_lines(ctxt,bar_width,aim_types,dispersion_fun,cost_fun,confidence_config,
  range,target_size)`, `aim_lines(...)` (mirrors print_aim's steadiness+dispersion+cost math),
  `throw_aim_lines(...)` (mirrors draw_throw_aim incl. the critter-vs-object confidence_config
  fork). They reuse the EXACT math + `get_labeled_bar`/`get_colored_bar`/`confidence_estimate`/
  `confidence_config` so the displayed numbers are identical; only the layout is semantic.
  `panel_text()`'s WIP stub replaced with a Fire/Throw dispatch: Fire inlines
  `panel_fire_mode_aim`'s predicted-recoil + aim-delay simulation (saving/restoring
  `you->recoil`) then `aim_lines(...)` + the "<mode> Delay: N" line; Throw/ThrowBlind →
  `throw_aim_lines(...)`. `bar_width = getmaxx(w_target)-2` (34/55−2, identical to curses).
  **SEMANTIC SIMPLIFICATION (flag for §8):** the compact/labels-narrow `insert_table`
  positional layout + the `bars_pad=3` narrowing are DROPPED — always the readable wide form;
  ACCURACY_DISPLAY (numbers vs bars) IS honoured. Numbers match number-for-number; only the
  compact-table arrangement differs. **EYEBALL CHECK (user, A/B via F4 "ranged targeting"):**
  aim a GUN at a critter — Steadiness bar/% + per-aim-type "[hotkey] … Moves to fire: N" +
  the `*`/`+`/`|` confidence bar (or Great/Normal/Graze %) match curses **number-for-number**
  across ACCURACY_DISPLAY=bars AND =numbers; aim-threshold modes show the "Delay" line;
  THROW an item (and a blind throw) shows the throw confidence (critter → 3-tier, object →
  single "Hit"). **(2c folds into 2b — both Fire and Throw readouts landed together.)**

### Tier 6 progress (overmap_ui + on-map text + §7 world-text layer)

**Tier 6 decomposition (multi-slice sub-project; architecturally the hardest tier):**
1. **overmap legend sidebar** (`draw_om_sidebar`) — the text panel beside the overmap
   tile grid. Conforming side-panel; the priced first slice. **← DONE (slice 1).**
2. **overmap sub-screens** — search popup, note editor (already has a `draw_rml` path),
   place-terrain/special editor. Mostly small / partly Tier-0.
3. **on-map static labels** — city labels (`draw_city_labels`) + zone labels + static
   `overlay_strings` → an RmlUi DOM overlay positioned via world→screen projection
   (reuse sprite.vert constants: `SV_Position/tile_px − camera_off`). NOVEL.
4. **§7 world-text layer** — transient/animated text (SCT now, floating damage later):
   an imperative pooled text layer reusing the RmlUi glyph atlas/render-interface (not
   DOM, to avoid layout thrash). FOUNDATIONAL + hardest. **Unblocks Tier-5 ranged.**

NOTE: the ASCII overmap grid (`draw_ascii`, used when `!use_tiles_overmap`) is curses
cell-text but is the MAP VIEW (like `w_terrain`) — its disposition (GPU-only vs migrate)
is a §8 rip-out question, not slice work here.

- **Tier 6 slice 1: overmap legend sidebar (overmap_ui::display / draw_om_sidebar) —
  CODE-COMPLETE + BUILD-GREEN (overmap_ui.cpp.o 14:17:47 + binary relinked 14:18:10, 0
  errors, fresh mtime), TOGGLE OFF, EYEBALL OWED, COMMITTED.** `data/gui/overmap.{rml,
  rcss}` model "overmap" + `overmap_rmlui_enabled()` toggle + F4 "overmap legend"
  checkbox. The legend is a RIGHT-SIDE panel only — the doc body is **transparent +
  right-anchored** so the overmap tile grid (drawn underneath by the GPU/ASCII map path,
  UNCHANGED) shows through on the left. Render-only doc, 3 flat bound strings (no row
  vectors → nothing to register): `info_rml` (tile symbol+description / weather / debug
  oter info / mission distance), `hints_rml` (pan hints + the ~24 keybinding hints,
  each coloured pink when its toggle is active else magenta — scroll-pane), `footer_rml`
  (dimension name + LEVEL/coordinates). STRUCTURAL POINTS: (1) the session ptr is
  THREADED through `draw()` → `draw_om_sidebar` (both file-static, only called here) —
  draw_om_sidebar early-returns into `build_om_sidebar_rml` when the ptr+handle are set,
  else draws curses (intact for A/B); cleaner than a file-global. (2) `build_om_sidebar_rml`
  mirrors the curses content verbatim into the 3 strings. (3) doc owned in display()
  (local unique_ptr + rml_doc), opened on the screen's own `ictxt` (real input_context,
  unlike dialogue), torn down by the rml_doc dtor at the single `return`. (4) the on_redraw
  passes `rml ? sidebar.get() : nullptr` so curses runs when the toggle is off. F4 toggle
  "overmap legend" (OFF). **EYEBALL CHECK (user, A/B via F4):** open the overmap ('m'):
  the right legend panel shows the tile description at the cursor (symbol + name), weather
  if visible, mission distance, the full keybinding list (toggled actions pink), and the
  dimension + "LEVEL z, coords" footer; moving the cursor updates the description; the
  toggle hints flip pink/magenta as you press them; the map tiles still render on the left
  (panel doesn't cover them). **WATCH:** (a) the panel is transparent-bg right-anchored —
  confirm the map shows through (no full-screen cover). (b) the hint list is long → it
  scrolls in its pane. (c) first Tier-6 doc over the live map → D3D12 (Win11) glance. (d)
  the ASCII (non-tiles) overmap still renders underneath when use_tiles_overmap is off.

- **Tier 6 slice 2: overmap search box (overmap_ui::search) — CODE-COMPLETE +
  BUILD-GREEN (overmap_ui.cpp.o 14:41:52 + binary relinked 14:41:54, 0 errors, fresh
  mtime), TOGGLE OFF (shares overmap_rmlui_enabled()), EYEBALL OWED, COMMITTED.**
  `data/gui/overmapsearch.{rml,rcss}` model "overmapsearch" — **reuses the overmap
  toggle** (one overmap family gated per sub-screen; worldfactory precedent), no new
  toggle/checkbox. Small top-right box (transparent right-anchored body) stacked OVER
  the legend doc: `body_rml` (Search term / Result i/N / Direction dist+name, label
  light_blue + value light_red) + `hints_rml` (cycle/confirm/quit, yellow keys). The
  search TERM is entered via the Tier-0 string_input popup (unchanged); this migrates
  the result box (the on_redraw). Inline rml_doc + sync_rml in search() (its own
  ui_adaptor/ctxt — not threaded like the legend). NEXT/PREV cycle, CONFIRM, QUIT stay
  on the loop. **EYEBALL CHECK (user, A/B via the overmap toggle, string_input ON):**
  open overmap → SEARCH → type a term → the result box (top-right, over the legend)
  shows the term, "Result 1/N", direction+distance, and cycle/confirm/quit hints;
  NEXT/PREV cycle results (the map cursor jumps), CONFIRM jumps there, QUIT cancels.
  **WATCH:** two RmlUi docs stacked (legend "overmap" + "overmapsearch") — confirm the
  box sits cleanly over the legend, both render.

**Tier 6 slice 2 scope note:** the remaining overmap sub-screens — the note-create
TERRAIN PREVIEW (a 3×3 oter-symbol mini-grid, borderline map-view not text-UI) and the
debug PLACE_TERRAIN/SPECIAL editor (debug-only) — are LOW-VALUE / map-adjacent and
DEFERRED (the note's color menu + text entry are already Tier-0 uilist/string_input).
Revisit at the §8 rip-out sweep if they keep curses alive.

- **Tier 6 slice 4.1: §7 world-text layer (FIRST vertical slice — SCT through RmlUi's
  own FontEngine) — DONE + EYEBALLED WORKING (user 2026-06-16: "SCT shows as rmlui
  text"), TOGGLE OFF, COMMITTED.** Position/size left at defaults (px=24, dx=dy=0) —
  user will tune the live F4 sliders later. The
  foundational/hardest tier-6 unit, started at its cheapest real consumer. Renders the
  tile-path scrolling combat text (SCT) through RmlUi's **own** font engine (the glyph
  path that survives the §8 curses rip-out) instead of the curses `overlay_strings` →
  `map_font->OutputChar` path. **KEY DESIGN FINDING (refines §7):** §7's wording "reuse
  RmlUi render interface for raw glyphs" was imprecise — RmlUi 6.x routes font geometry
  through `RenderManager` (managed layer), NOT the bare `RenderInterface` the project
  wraps. The reachable native path (all public API, confirmed): `ctx->GetRenderManager()`
  → `GetFontEngineInterface()->GetFontFaceHandle("Terminus (TTF)",…)` +
  `GenerateString(rm, face, 0, str, {0,0}, ColourbPremultiplied, 1, shaping, meshes)` →
  per `TexturedMesh`: `rm.MakeGeometry(mesh)` → `Geometry::Render(translation, texture)`.
  STRUCTURAL POINTS: (1) new `rmlui_layer::world_text_begin/add/active` (rmlui_layer.{h,
  cpp}): items submitted per frame (physical-px top-left + utf8 + 0xRRGGBBAA), COMPILED
  in `prepare()` (Render with no open pass = compile-only, uploaded by `upload_pending`),
  DRAWN in `render_in_pass()` UNDER the menu docs (so SCT sits on the map, under any open
  UI). Mirrors the doc two-phase render exactly. (2) `world_text_active()` is kept OUT of
  `active()` (which gates input) so SCT showing with no menu open does NOT steal mouse;
  `sdl_render_frame.cpp` gates the overlay pass on `active() || world_text_active()`.
  (3) `cata_tiles::draw_sct_frame`: toggle ON → `world_text_begin()` + `world_text_add()`
  per SCT entry (color = `windowsPalette[FG]`, the SAME index the curses overlay path
  feeds `windowsPalette[]` → exact A/B colour fidelity; pos = `player_to_screen`), and
  SKIPS the `overlay_strings.emplace` (no double-draw). (4) context is sized in PHYSICAL
  px so `player_to_screen`'s physical-px output is the translation directly (no density
  division). F4 toggle "world text (SCT)" (OFF). **EYEBALL CHECK (user, A/B via F4):**
  enter combat (melee a monster) so damage SCT rises off it — with the toggle ON the
  numbers render as crisp RmlUi-font text at the right map position, colours matching the
  curses path (toggle OFF), animate/rise + fade as before, and clear when combat ends;
  with a menu open over combat, SCT draws UNDER the menu. **WATCH / known flags (build-
  blind on the RmlUi runtime):** (a) **font size hardcoded `g_world_text_px=16`** — may
  not match the curses SCT size; tunable (TODO: promote to F4 slider). (b) **HiDPI:** the
  font size is NOT scaled by `density_ratio` (the translation is physical-px and matches,
  but the glyph size may read small on a HiDPI display) — flag if text is too small. (c)
  **baseline offset:** GenerateString lays on the baseline at y=0; I nudge +font_px so
  screen_y reads as the top edge — vertical position may be slightly off, flag. (d)
  alignment: left/center/right per-direction offset of the curses path is approximated by
  the start position only — minor horizontal drift possible. (e) the two-phase
  compile→draw on ad-hoc `Geometry` (vs documents) is the core unproven mechanism — if
  SCT renders NOTHING with the toggle on (but the overlay pass fires), the deferred-
  upload reuse on ad-hoc geometry is the suspect. (f) D3D12 (Win11) glance — first
  non-document RmlUi geometry path.
  **★ KEY LEARNING (cost a long debug session — carry to any future imperative
  RmlUi-font work): `FontEngineInterface::GetFontFaceHandle(family,…)` does a DIRECT
  family lookup with NO fallback resolution.** The shared context's font is loaded via
  `Rml::LoadFontFace(path, /*fallback=*/true)`, which registers under the TTF's embedded
  family AND as the fallback — document text resolves ANY family via that fallback, so
  RCSS "just works" and HIDES whether a direct lookup would. A direct
  `GetFontFaceHandle("Terminus (TTF)", …)` returned **0** (font not found) → no geometry
  → nothing rendered, SILENTLY. Fix: register the font from memory under an explicit
  family we own (`Rml::LoadFontFace(bytes, "cata-world-text", …)`, bytes retained until
  Shutdown) and look THAT up, with `FontWeight::Auto` (bundled Terminus is weight
  "Medium"/500, so `FontWeight::Normal`/400 also misses). SECONDARY time-sink: the
  project's debug.log filters by DEBUG CLASS — `DC::SDL` is filtered OFF, `DC::Main`
  shows; all diagnostics MUST use `DC::Main` or they're invisible even while the code
  runs. The render mechanism (two-phase compile-in-prepare / draw-in-render_in_pass on
  ad-hoc `RenderManager::MakeGeometry`+`Geometry::Render`) is now PROVEN.

- **Tier 6 slice 4.2a: generalize the world-text ROUTING (all `overlay_strings` →
  RmlUi font) — CODE-COMPLETE + BUILD-GREEN (cata_tiles.cpp.o 06:24:44 +
  sdl_curses_draw.cpp.o 06:24:34 both newer than source; binary relinked 06:24:47,
  fresh mtime), TOGGLE OFF (shares `world_text_rmlui_enabled()`), EYEBALL OWED,
  COMMITTED `9167f978b7`.** Moves the RmlUi world-text routing from the SCT-specific
  `cata_tiles::draw_sct_frame` (slice 4.1) UP to the GENERIC `overlay_strings`
  render loop in `cata_cursesport::curses_drawwindow` (sdl_curses_draw.cpp:343/380,
  inside the `w == g->w_terrain && use_tiles` block ONLY → clears + refills once per
  terrain draw, not per window). NET EFFECT: the world-text layer now catches ALL
  on-map text that reaches `overlay_strings` — SCT (font mode) + zone labels + debug
  overlays (scent/rad/temp/vis) — in ONE place, replacing the curses `OutputChar`
  cell loop for the whole overlay, not just SCT. STRUCTURAL POINTS: (1) the slice-4.1
  SCT-special-case in cata_tiles.cpp is REMOVED (-29 lines: the `world_text_begin()`
  in `draw()`, the `world_text_add` branch + skip-emplace in `draw_sct_frame`, the
  `rmlui_layer.h` include); `draw_sct_frame` now ALWAYS emplaces SCT into
  `overlay_strings` (font mode) so the generic path picks it up — no double-draw. (2)
  start pixel mirrors the curses path exactly: `win->pos*font + (x_offset −
  alignment_offset)*map_font->width + coord` (coord is already physical-px from
  `player_to_screen`; world-text context is physical-px → translation is direct). (3)
  colour via `windowsPalette[ft.color]` (same index the curses overlay fed) → exact
  A/B fidelity. **BEHAVIOR DELTA vs 4.1 (intentional, arguably more correct):** with
  `ANIMATION_SCT_USE_FONT=off`, SCT draws as TILES (never enters `overlay_strings`) so
  it no longer force-routes to RmlUi — the world-text layer replaces the FONT path,
  not the tile path. **NOT in this slice (deferred):** the pooled animated layer with
  its own lifetime/motion (floating damage numbers) — animation still rides the
  existing per-frame `overlay_strings` rebuild. F4 toggle "world text (SCT)" (OFF).
  **EYEBALL CHECK (user, A/B via F4):** (1) combat SCT renders as RmlUi-font text,
  colours/positions matching curses (toggle OFF), under any open menu. (2) **NEW
  coverage — ZONE LABELS** (zone-manager labels on the map) render as RmlUi text with
  the toggle ON. (3) **NEW coverage — DEBUG OVERLAYS** (enable scent/temp/vis debug
  display) route through RmlUi too. (4) toggle OFF → all three fall back to the curses
  `OutputChar` path identically. **WATCH:** (a) any `overlay_strings` source with a
  per-entry alignment/multi-segment layout the simplified start-pixel approximates
  (left/center/right offset is start-position only, per 4.1 flag (d)) — check zone
  labels aren't horizontally drifted. (b) SCT in tile-mode no longer appears in the
  RmlUi layer (the behavior delta above) — confirm that's acceptable.

**§7 ON-MAP TEXT — FUNCTIONALLY COMPLETE as of slice 4.2a (pending eyeball)
(2026-06-17).** AUDIT FINDING that collapses the remaining §7 work: ALL on-map
overlay text flows through ONE indivisible path — 14 producer emit-sites in
cata_tiles (zone labels, zone-dim, vehicle-AI, scent, scent-type, rad/temp, SCT) →
the SINGLE drain loop at `sdl_curses_draw.cpp:343-412`. `formatted_text` carries no
source tag, so the loop is indivisible — "the whole render moves as one slice." Slice
4.2a gated that ENTIRE loop, so **every** on-map overlay-text source already routes
through the RmlUi FontEngine layer when the toggle is ON. Grep-confirmed: no other
overlay_strings consumer, and no direct text-draw in cata_tiles that bypasses the
drain (only sprite/texture blits). Consequences:
- **Slice 3 (zone/city labels) is SUBSUMED — no separate work.** Those labels were
  always in this same drain loop; 4.2a migrated them. There are no main-map static
  labels outside overlay_strings (overmap city labels are Tier-6 slices 1-2, done).
- **Slice 4.2b (pooled animated layer / floating damage numbers) DEFERRED as
  speculative polish.** SCT already IS the floating-number system (own lifetime/motion
  via `vSCT` steps) and already animates THROUGH the layer post-4.2a. 4.2b would only
  reimplement that for smoother sub-pixel motion — it does NOT advance the §8 rip-out
  (the curses `OutputChar` overlay consumer is already fully bypassable). Revisit only
  if/when the turn-stepped SCT motion is judged too coarse. One-line revert to pick up.
- **Rip-out banked:** the `OutputChar` overlay-text drain (a curses font consumer §8
  must delete) is now end-to-end bypassable via `world_text_rmlui_enabled()`. At §8
  it's force-ON + deleted.

§7
world-text unblocks Tier-5 ranged. The deferred faction (Tier 2) also remains (needs the
creature/npc-info F.2 component).

### Tier 7 progress (sidebar HUD — the continuous every-turn HUD)

Plan/slice design: `~/.claude/plans/rippling-spinning-starfish.md`. Strategy (decided,
GO at full scope): **hybrid, render-only, top-down contiguous incremental** — value/
body-graph widgets bind structured data directly; the 53 native `draw_*` get parallel
`*_text()` producers (faction/ranged precedent, curses pristine for A/B). One master
toggle replaces a contiguous TOP segment; panels migrate in column order so the RmlUi
block stays a single flex column; final slice → whole column → default-flip → §8 gate #1.

Ground truth: `game::draw_panels` (game.cpp) loops `panel_manager::get_current_layout()`,
drawing each `window_panel` into a fresh `newwin` via its `draw` fn. RmlUi context
renders only while `any_open()` (rmlui_layer `g_open_docs`), so a persistent HUD doc
opened once renders every frame with modals stacking on top by open order. The modal
`rml_doc` harness is the WRONG lifecycle (bundles a 16ms input tick for a blocking loop);
the HUD uses its own non-modal open path.

- **Slice 1 — lifecycle scaffold + Stats panel — DONE + BUILD-GREEN (Metal, fresh relink
  22:00), TOGGLE OFF, EYEBALL OWED (Metal + D3D12), COMMITTED `03bf258feb`.** Persistent-
  HUD owner in **panels.cpp** (`sidebar_hud_open/sync/close/owns_panel`; lives there to
  reuse the TU-static `str_string`/etc.) mirrors `rml_doc::open` MINUS the input tick.
  `data/gui/sidebar_hud.{rml,rcss}` (model `sidebar_hud`, transparent edge-anchored flex
  column) binds one `cata_text_to_rml` stats row built from the same getters `draw_stats`
  uses. Wired into `draw_panels`: lazy open + per-turn sync when enabled, skip the curses
  draw for the owned "Stats" panel; `close()` on toggle-off + in `game::cleanup_at_end`
  (so the HUD never lingers over the main menu). F4 toggle "sidebar HUD (stats)" (OFF).
  All 7 touched files were CLEAN (no entanglement with the uncommitted runic/lighting
  working set). **EYEBALL CHECK (user, A/B via F4 — use a BUILT-IN layout, the default;
  the `we_*` widget layouts name the panel by widget id, not "Stats", so slice 1 won't
  own them yet):** toggle ON → STR/DEX/INT/PER render via RmlUi at the sidebar top,
  values + colours matching curses number-for-number, updating as stats change; rest of
  sidebar still curses, no overlap/gap; toggle OFF → identical to today; new-game / load /
  quit-to-menu / re-enter → no crash, no doc lingering over the menu. **★ KNOWN LIMITATION
  (slice 2):** while ON, the always-open doc makes `rmlui_layer::process_event` capture
  in-game WORLD MOUSE (look/examine) — `process_event` returns true for any mouse event
  whenever `any_open()`, and sdl_input.cpp:427 withholds it from the game. Fix = a
  passive/render-only-doc distinction (`any_interactive_open()`) in `rmlui_layer.cpp`,
  DEFERRED because that file is in the uncommitted runic-border set (would entangle the
  commit). Keyboard + the F4 ImGui panel are unaffected (ImGui captures first).
- **Slice 2a — passive-doc mouse gate — DONE + BUILD-GREEN (Metal, fresh relink 21:50),
  COMMITTED `5c4b2c69de`, EYEBALL OWED.** Resolves slice 1's known limitation. `open_document`
  gained a `passive` flag (default false = interactive, unchanged for all modal screens);
  passive docs are tracked in a parallel `g_passive_docs`. `rmlui_layer::process_event` now
  returns false (falls through to the game) when only passive docs are open — new
  `any_interactive_open()` — so the always-open HUD no longer swallows world mouse
  (look/examine); capture resumes the instant an interactive modal stacks on top. The HUD
  opens passive. Rendering unchanged (still gates on `any_open()`). Tree clean (runic
  `rmlui_layer.cpp` was committed, so this no longer entangles). **EYEBALL:** HUD ON →
  world mouse (look/examine) works; open a modal over it → modal mouse still captured.
- **Slice 2b — multi-panel HUD (Sound + Needs) — DONE + BUILD-GREEN (Metal, fresh relink
  22:00), COMMITTED `f0ee61902f`, TOGGLE OFF, EYEBALL OWED.** Generalized the HUD from one
  positioned fragment to N: each owned panel is its own absolutely-positioned element,
  driven by a name→element-id table (`g_hud_owned`); `owns_panel` + `sidebar_hud_position(
  name, rect)` look up per panel, `draw_panels` passes `panel.get_name()`. (The single
  `#hud-col` couldn't host non-contiguous panels — Mana can sit between Stats and Needs.)
  Added Sound (`draw_stealth`) + Needs (`draw_needs_compact`) alongside Stats, each with a
  `hud_*_text()` producer + bound model var + positioned `<div>`. Producers reproduce
  content in reading order with simple spacing, not curses cell-exact columns (slice-1
  precedent). **Time deferred** — its no-watch branch is a graphical sun/moon bar
  (`draw_time_graphic`), not text; needs a non-text treatment. **EYEBALL:** toggle ON →
  Sound/Stats/Needs render via RmlUi at their sidebar slots, values+colours matching
  curses, rest of sidebar still curses, no overlap/gap; OFF → identical to today.
- **Slice 2c — variant-aware HUD + Wgt/Vol + Mana — DONE + BUILD-GREEN (Metal, fresh
  relink 12:55), COMMITTED, TOGGLE OFF, EYEBALL OWED.** Also fixes the slice-2b
  positioning bug surfaced by the first eyeball (`fix` commit `feature` earlier this
  session): the user's live sidebar is the **data-driven widget layout** (`current_layout_id:
  "custom"`, `data/json/ui/sidebar.json`), whose panels are named by lowercase widget id
  (`stats`/`sound`/`needs`/`weightvolume`/`mana`) — NOT the built-in `translate_marker`
  labels (`Stats`/…). slice 2b's `g_hud_owned` keyed only the capitalised labels → `owns_panel`
  false on the widget layout → curses kept drawing AND the never-positioned RmlUi fragments
  stacked at the window top-left. **Root insight:** each logical panel has up to 4 *variants*
  (classic/compact, labels/wide, narrow) under different names, and they render *different
  content* (`stats`=`draw_stat_wide` adds Power+Safe; `stats_compact`=`draw_stats` doesn't).
  - **`g_hud_owned` is now variant-aware:** `(name → elem_id, model var, producer)`; several
    names share an elem_id+var but point at different producers, so the runtime panel name
    selects the content variant. `hud_lookup` matches case-insensitively. `sidebar_hud_sync`
    is layout-driven — walks the current layout, runs each present (toggled + `render()`)
    owned panel's producer into its var; absent panels clear. Own only (name,variant) pairs
    a producer faithfully reproduces; unlisted names fall back to the curses draw.
  - **New producers:** `hud_stats_wide`, `hud_sound_labels`, `hud_needs_labels`, `hud_wgtvol`,
    `hud_mana`. Added Wgt/Vol + Mana `<div>`s. **`Mana` (the label) intentionally NOT owned**
    — collides with the `val_mana` value-widget; only native `mana*` ids map to the readout.
  - **EYEBALL (Metal + D3D12):** toggle ON over the widget/custom layout → stats/sound/needs/
    wgt-vol/mana render via RmlUi at their slots, content matching the *labels/wide* curses
    draws (the user's variant), rest of sidebar curses, no overlap/gap; OFF → identical.
- **Env/weather DEFERRED:** not in the target (custom) layout, and `draw_env_compact` (6 rows)
  vs `draw_weather_classic` are *different content*, not one panel — needs its own scoping.
- **Slice 2d — pure-text panels — DONE + BUILD-GREEN (Metal, relink 13:07), COMMITTED, TOGGLE
  OFF, EYEBALL OWED, 2026-06-20.** The panels that are pure text in the labels/wide layout:
  `hud_hint` (draw_hint), `hud_movement` (draw_char_wide), `hud_weapon` (draw_weapon_labels),
  `hud_armor` (draw_armor[_padding]). New elements hud-Hint/Movement/Weapon/Armor + vars;
  owns the user's widget ids (+ armor_classic/"Armor"/"Hint" aliases). 10 of the column's
  logical panels now have RmlUi producers (stats/sound/needs/wgtvol/mana/hint/movement/weapon/
  armor — plus limbs is still curses).
- **EYEBALL CONFIRMED 2026-06-20** — all 9 migrated panels (stats/sound/needs/wgtvol/mana/
  hint/movement/weapon/armor) render correctly in-game on the user's custom widget layout.
- **Slice 3 — STRUCTURAL PIVOT (flex-column + whole-sidebar suppression) — DONE + EYEBALL
  CONFIRMED (user, 2026-06-20), COMMITTED, TOGGLE OFF.** Executes the locked new strategy (§"HUD
  STRATEGY" below). The slice 1–2d per-panel `position:absolute` + `sidebar_hud_position` +
  per-panel `owns_panel` suppression machinery is REPLACED by:
  - **`sidebar_hud.rml`** — ONE container `#hud-sidebar` with a single `data-for="row : rows"`
    div (`data-class-flex="row.flex" data-rml="row.rml"`). No per-panel elements.
  - **`sidebar_hud.rcss`** — `#hud-sidebar` = `position:absolute` (C++ injects left/top/width/
    height = sidebar rect %) + `display:flex; flex-direction:column`. `.hud-row` block +
    pre-wrap, `flex:0 0 auto`; `.hud-row.flex` (sentinel -1 minimap / -2 log) = `flex:1 1 auto`.
  - **panels.cpp** — model is now `Rml::Vector<hud_row_model{rml,flex}>` bound as `rows`
    (RegisterStruct/RegisterArray, autopickup idiom). `g_hud_owned`(27, elem_id+var) → simpler
    `g_hud_producers`(27, name→producer) + `hud_producer()`. `sidebar_hud_sync` walks the layout
    in order: each present panel → producer RML, or a `[name]` dark-gray placeholder (no curses
    fallback after suppression); `flex = get_height()<0`. New `sidebar_hud_apply_rect()` places
    the container at the sidebar rect each sync (first-panel width / TERMX, edge per
    SIDEBAR_POSITION, full height). `sidebar_hud_position` DELETED; `owns_panel` → `sidebar_hud_active()`.
  - **game.cpp `draw_panels`** — when `sidebar_hud_active()`, advance `previous_turn` + early-return
    BEFORE the curses panel loop (whole-sidebar suppression). The per-panel owns_panel/position
    branch inside the loop is deleted (dead after early-return); curses path is now unconditional draw.
  - **panels.h** — decls updated (drop owns_panel/position, add `sidebar_hud_active`).
  - **EYEBALL CHECK (user, A/B via F4 "sidebar HUD", Metal + D3D12):** toggle ON over the custom
    layout → the 9 migrated panels render as a single flex column at the sidebar (top→bottom in
    layout order), values+colours as before; unmigrated present panels (limbs/location/log/compass/
    map/bodygraph/val_*) show a dark-gray `[name]` placeholder in their slot; the log/map rows
    grow (flex); NO curses sidebar underneath (whole-sidebar suppressed); world mouse (look/examine)
    still works (passive doc); resize tracks the rect; toggle OFF → identical to today's curses.
    **WATCH:** (a) column overflow — if total content > TERMY, flex sentinels shrink but fixed rows
    could clip (no overflow marker now; flag if content lost). (b) the `[name]` placeholders are
    intentional this slice — limbs/log/etc data accessors land NEXT slice.
- **Slice 4 — MVP accessors: limbs (HP) + message log — CODE-COMPLETE + BUILD-GREEN (Metal,
  fresh relink 14:44, panels.o fresh), TOGGLE OFF, EYEBALL OWED (Metal + D3D12), UNCOMMITTED,
  2026-06-20.** The slice-3 pivot pays off: adding a panel = ONE producer entry, ZERO RML/model/
  RCSS churn (the `[name]` placeholder auto-replaces). Two new producers in panels.cpp + 7 table
  rows (limbs/limbs_compact/limbs_narrow/Limbs + log/log_classic/Log):
  - **`hud_limbs`** (+ helper `hud_limb_health`) — HP per body part, one row each, FULL colour
    fidelity (reproduces draw_limb_wide name + draw_limb_health's text: broken-limb #/= mend bar,
    splint colour, number vs 5-cell hp-bar style, trailing dots). One producer serves all limb
    variants (same data, layout-only differences).
  - **`hud_log`** — `Messages::recent_messages(20)` joined chronologically. **MVP FIDELITY GAP
    (flagged):** recent_messages drops the per-type colour + age fade the curses display applies,
    and the flex row top-aligns (curses bottom-aligns near the play area). Phase-2 fix = a coloured
    message accessor. Acceptable for MVP (text is legible, cheapest source).
  - Remaining `[name]` placeholders after this slice: location, compass, **map (minimap — phase 2
    RTT, decoupled from gate)**, val_pain/thirst/stamina/mana (number widgets — easy structured via
    get_var_value, candidate next), bodygraph (graphical — phase 2).
  - **EYEBALL CHECK (user, A/B via F4, Metal + D3D12):** toggle ON → Limbs shows each body part's
    name + hp bar with the right colours (matches curses limbs panel number-for-number; try a
    damaged/broken/splinted limb + HEALTH_STYLE number vs bar); Log shows recent messages in order
    (plain colour — known gap); both grow/sit in their column slots; other placeholders unchanged.
  - **COMMITTED `e269094038` (build-green, eyeball owed).**
- **Slice 5 — MVP accessor: location — CODE-COMPLETE + BUILD-GREEN (Metal, fresh relink 15:14,
  panels.o fresh), TOGGLE OFF, EYEBALL OWED (Metal + D3D12), UNCOMMITTED, 2026-06-20.** One
  producer + 4 table rows (location/location_alt/location_narrow/Location). **`hud_location`** —
  reproduces draw_loc_labels' text: Place / X,Y,Z (honours OVERMAP_COORDINATE_FORMAT subdivided) /
  Sky+weather / Light / Date / Time (watch vs approx vs underground), one row each, full colour.
  **DROPPED for MVP:** draw_loc_wide_map's inline overmap minichunk (`draw_overmap_chunk`) — it is
  GRAPHICAL → phase 2, like the pixel minimap. `draw_location_classic` (different compact layout)
  is NOT served (classic layout, not the user's; would show placeholder).
  - **EYEBALL CHECK (user, A/B via F4):** Location row shows place name / coords / sky+weather (right
    colour) / light level / date / time matching curses; underground + no-watch cases read right.
- **Slice 6 — MVP accessor: compass (directional enemy counts) — CODE-COMPLETE + BUILD-GREEN
  (Metal, fresh relink 15:17, panels.o fresh), TOGGLE OFF, EYEBALL OWED, UNCOMMITTED, 2026-06-20.**
  One producer + 8 table rows (compass/compass_comp/compass_simple/compass_compact/
  compass_comp_compact/Compass/Compact Compass/Simple Compass). **`hud_compass`** — enemy COUNTS
  per direction (`get_mon_visible().visible_count_by_dir`, octants + local), "No enemies in sight"
  when clear. **★ MVP DESIGN SIMPLIFICATION (flag for user):** the native full compass
  (draw_compass_padding → `g->mon_info`) is a GRAPHICAL 3×3 directional symbol grid + creature
  list → phase 2; this ships the simple-compass datum (counts) instead. User's design call at
  eyeball: counts vs symbol grid.
  - **EYEBALL CHECK:** compass row shows "N(2) SE(1)…" for visible enemies by direction, "No
    enemies in sight" when clear; updates as monsters move.

- **★ TIER 7 PHASE-1 MVP — TEXT-COMPLETE (2026-06-20).** Every text-able sidebar panel in the
  user's custom layout now has an RmlUi producer on the slice-3 flex-column base: stats / sound /
  needs / wgt-vol / mana / hint / movement / weapon / armor / **limbs / log / location / compass**
  (12 logical panels). Remaining placeholders are NON-text / deferred, all phase-2 or out-of-MVP:
  - **map (pixel minimap)** — graphical RTT, DECOUPLED from the gate (curses never drew it anyway);
    ships as an empty/placeholder slot, RTT lands later as independent GPU work.
  - **bodygraph** — graphical body silhouette → phase 2.
  - **compass (full symbol grid)** — phase-2 upgrade from the MVP counts.
  - **val_pain/thirst/stamina/mana** — DEFERRED (redundant + name collision, see below).
  So the HUD is **playable on** (all essential text data present). **DECISION POINT (user):**
  (a) flip the toggle default ON for daily use + iterate phase-2 polish on a curses-free base; or
  (b) keep OFF and proceed to phase-2 graphical (minimap RTT / bodygraph) first; or (c) move to
  Tier 8 (F4→RmlUi) / Tier 9 (minigames) toward the §8 all-tiers rip-out. Tier 7 completion alone
  does NOT trigger the rip-out (§8 needs Tiers 0–9); it does unlock running the HUD full-time.
- **★ val_* number widgets (val_pain/thirst/stamina/mana) — DEFERRED (not next), 2026-06-20.**
  Two reasons: (1) **largely REDUNDANT** in the custom layout — pain+thirst already render in the
  migrated Needs panel, stamina in Movement; only mana/stamina-as-bar add anything. (2) **name
  collision:** the value-widget panel name is the *stripped* title-case form (`value_widget_name`:
  "val_pain"→"Pain", "val_mana"→"Mana"), and `hud_producer` matches case-insensitively, so "Mana"
  would wrongly hit the native `mana` producer (the exact clash slice 2c flagged). A generic value
  producer (strategy's "one producer for all scalars" via `get_var_value`+`value_widget_color`) is
  the right phase-2 home — but needs a name→widget resolver + collision handling. Defer to phase 2
  with icons. The structured path (`get_var_value`, `value_widget_color`, `value_var_max`) already
  exists, so it's cheap THEN; just not worth the collision wrinkle for redundant data now.

### Tier 7 — HUD STRATEGY (rewritten 2026-06-20, grilled)

**Context change that drives everything: single user, ONE bespoke UI.** No multi-layout, no
variants, no per-user customization. This collapses the old "faithful-repro of 53 `draw_*`
panels × 4 variants → flip → rip out" roadmap. The decisions below supersede the old slice
3–10 roadmap (preserved in git history if needed).

**Core decision — DON'T reproduce curses. DESIGN the target HUD now, as the migration.**
Rationale: the data-extraction work (getting weapon/armor/weather/compass/log/hp-by-limb out
of `draw_*` as data) is identical whether you reproduce or redesign; only the curses-cell
*layout* differs, and a faithful layout is throwaway for a user who will design their own UI.
So faithful-repro is strictly dominated. Build the HUD you want; extract each panel's data as
you add it; suppress curses; rip out; then polish.

**Two-phase, crisp gate kept:**
- **Phase 1 = MVP** — a *designed* HUD covering the essential data you can't play without,
  using the **cheapest available data source** per datum. When it's enough to play on and the
  curses sidebar is redundant → suppress curses sidebar → **Tier 10 rip-out**.
- **Phase 2 = design to perfection** on a curses-free base (no fallback to fight, no pressure).

**Architecture decisions (locked):**
1. **Layout = ONE hand-authored flex-column document** (`sidebar_hud.rml`), document flow —
   NOT per-panel `position:absolute`. This deletes the unmanaged-height overlap/overrun class
   entirely; widget flex-height sentinels (`-1` log / `-2` minimap) map to CSS `flex-grow`.
   One absolutely-positioned *container* = the sidebar rect (width, full TERMY, left/right
   edge), computed once per resize; children flow inside. The slice 1–2d per-panel
   `sidebar_hud_position` + variant-aware `g_hud_owned` machinery is superseded.
2. **Curses suppression = whole-sidebar**, not per-panel. Toggle ON → RmlUi container owns the
   entire sidebar region, curses draws none of it. (Per-panel coexistence/alignment is gone
   with the absolute positioning.) Un-built panels show a **visible placeholder**
   (`[unmigrated: name]`), never a silent blank — post-rip-out `draw_*` cannot run (it only
   writes to a curses window), so there is NO runtime curses fallback; producers must fully
   replace it.
3. **Data binding = the house idiom** — `RegisterStruct`/`RegisterArray` + `data-for` +
   per-row `data-rml` (proven in auto_note/compare/bionics/…). Repeating sub-lists (message
   log, limb rows) use `data-for`; the top-level structure is authored.
4. **Widget engine = DATA LAYER + ICON REGISTRY only.** Its multi-layout / variant /
   custom-save machinery (`panel_manager` layouts, multiple `sidebar.json` layouts, custom
   save/load) is dropped. Data accessors built to **serve the later full redesign** (structured
   widget-engine vars — value+color+fraction — via `widget_var` / ported `display::` providers,
   NOT throwaway draw_* text-scraping). For MVP, only add the **easy** accessors; defer hard
   data extraction to phase 2.
5. **Animation = native RCSS** (transitions / `@keyframes`), NOT the bespoke curses-era tween
   engine (which is curses-coupled and dies at rip-out). Verify coupling before deleting.

**Coverage universe is finite + CLOSED:** a panel reaches screen via `_style="number"` (one
generic value producer covers all scalars, incl. future/modded) or `_style="native"` →
`native_draw_registry()` (panels.cpp:2634, ~57 entries / ~15–18 logical, **fixed C++** — mods
cannot add native targets). The gate for the flip is just "every panel in MY one UI is built";
`native_draw_target_exists()` + a `sidebar_hud_has_producer()` audit makes it mechanical.

**The one true-graphics piece — pixel minimap:** render-to-texture into an offscreen
`COLOR_TARGET|SAMPLER` GPU texture, **on the frame command buffer** (the GI compute-pass
barrier recipe — SDL_GPU auto-inserts the write→read barrier), bound as `<img>`. NEVER a
separate CB (that is the loading-image D3D12 crash: barrier to PIXEL_SHADER_RESOURCE not
complete before sample). Real labor = porting `pixel_minimap::render` from SDL_Renderer
`RenderCopy` (currently invisible, pixel_minimap.cpp:324) to SDL_GPU geometry. **DECOUPLED
from the rip-out gate** — curses never drew the minimap, so the flip ships with an empty
minimap slot; the RTT lands as independent GPU work after.

**Icon capability is cheap + already exists** (`rmlui_proc_texture.cpp`): static SVG textures,
upload-once → no D3D12 hazard (unlike the per-turn minimap RTT). Add icons in phase 2 polish.

**Non-risk (was flagged KEY RISK, now retired):** monospace alignment. `Terminus.ttf` is
`LoadFontFace`'d at init (`rmlui_layer.cpp:352`, registered as fallback face) and proven
cell-aligned in-game (help Movement grid under `white-space:pre`). Colored glyph grids work;
only test is N colored `<span>`s on one `pre` line for advance drift IF the design uses one.

**Next concrete steps:** (a) define the MVP panel set (the can't-play-without data); (b) author
the single flex-column container + whole-sidebar suppression gate; (c) wire the easy data
accessors; (d) placeholder everything else; (e) when playable, rip out curses; (f) phase 2
design + icons + minimap RTT + RCSS animation.

### Tier 8 progress (F4 dev panel → RmlUi)

**Approach (user GO 2026-06-20, "start now / parallel preview"):** build an RmlUi dev-panel
in PARALLEL behind a preview toggle inside the existing ImGui F4 — ImGui stays primary + fully
live (zero tuning loss) until the RmlUi version reaches full parity, THEN F4 opens the RmlUi
doc and `imgui_layer` is retired from the composite pass (§8). Migrate lowest-risk first (the
screen-toggle registry — pure checkboxes), then the lighting tuning controls section by section.
**Scope reality:** the F4 panel is ONE window, ~7 tabs, 51 checkboxes + 3 float / 2 int sliders
+ 3 combos + 3 colour pickers + 9 buttons (`sdl_lighting_devui.cpp`, 745 LOC). Foundation needed:
RmlUi bound FORM CONTROLS (`<input type="checkbox" data-checked>` + `<input type="range"
data-value>`) — proven in RmlUi's own samples, compiled into rmlui_core, but UNUSED in this
codebase until now.

- **Slice 1 — form-control foundation + preview lifecycle — DONE + EYEBALL CONFIRMED (user,
  "looks great", 2026-06-20), COMMITTED `e9e883a65a`.** Proved RmlUi checkbox two-way data-binding
  end-to-end (`data-checked` fires both ways — no event fallback needed; de-risks the whole tier).
  - `data/gui/devui.{rml,rcss}` — model `devui`, interactive doc (top-left box). 4 checkboxes
    (uilist / query_popup / string_input / sidebar HUD) bound `data-checked` to their
    `*_rmlui_enabled()` bools. RCSS styles `input.checkbox` + `:checked` (green fill) — form
    controls are invisible without styling (invader/data_binding sample idiom, no sprite sheet).
  - `sdl_lighting_devui.cpp` — `#include <RmlUi/Core.h>` + path_info/panels; anon-namespace
    `g_devui_doc`/`g_devui_model`/`g_devui_preview` + `devui_rml_open/close` (bind bools BY
    POINTER → two-way) + `rml_tick(bool imgui_visible)` (open when F4 visible && preview on, else
    close; DirtyAllVariables each frame so ImGui-side toggles reflect). ImGui checkbox "RmlUi dev
    panel (preview)" added to the RmlUi tab. ImGui path otherwise UNTOUCHED.
  - `sdl_render_frame.cpp` — calls `rml_tick(imgui_layer::visible())` each frame after rmlui init.
  - **EYEBALL CHECK (user, A/B, Metal + D3D12):** open F4 → RmlUi tab → check "RmlUi dev panel
    (preview)" → a top-left RmlUi box appears with 4 checkboxes whose state matches the ImGui
    checkboxes; clicking an RmlUi box toggles the real screen live AND the ImGui checkbox updates
    (and vice versa — two-way); uncheck preview (or close F4) → box disappears; world mouse returns
    when closed. **The make-or-break:** does `data-checked` two-way binding actually fire (click
    writes the bool, external change reflects)? If a box renders but won't toggle, the binding
    needs an `onchange`/event fallback — flag it.
  - **WATCH:** the preview box + ImGui window are both on screen (intended for A/B); the RmlUi box
    captures mouse only when hovered (interactive doc), ImGui captures when over its window.
- **Slice 2 — full toggle registry + first slider — DONE + EYEBALL CONFIRMED (user, "slider works",
  2026-06-20), COMMITTED `870c0c6f27`. Plus usability fix `80bb1842ab`.** Expands the preview to ALL
  35 `*_rmlui_enabled()` checkboxes (6 sections mirroring the
  ImGui registry) + proves `<input type="range" data-value>` slider binding on `rmlui_layer::
  ui_scale()` (float, two-way) with a `{{ui_scale}}` live readout. RCSS: scrollable panel
  (`max-height:92vh; overflow-y:auto` — 35 rows exceed the screen) + range sub-element styling
  (`slidertrack`/`sliderbar`, plain colours, no sprite sheet).
  - **EYEBALL CHECK (user, A/B, Metal + D3D12):** preview shows all 6 sections of checkboxes
    (state mirrors ImGui both ways); the UI-scale slider drags 0.5–1.5, the `{{ui_scale}}` number
    tracks it, and dragging it live-rescales the RmlUi UI (same effect as the ImGui slider); the
    panel scrolls when taller than the screen. **Make-or-break:** does `data-value` two-way binding
    fire on the range (drag writes the float, ImGui-side change reflects)? If the slider renders
    but won't drag-write, add an `onchange` event fallback.
  - **★ USABILITY FIX `80bb1842ab`:** `ui_scale()` multiplies the context-wide
    `SetDensityIndependentPixelRatio` (rmlui_layer.cpp:904), so all `dp`/`em` scaled — including the
    dev panel hosting the slider, which reflowed under the cursor (unusable). Re-authored devui.rcss
    in **px** (raw pixels immune to the dp ratio) → panel is fixed-size; the slider rescales the rest
    of the UI while staying put. (Data files read from source `data/` — no rebuild, relaunch only.)
- **Slice 3 — Effects tab tuning (sliders + toggles) — CODE-COMPLETE + BUILD-GREEN (Metal, fresh
  relink 19:34, devui.o fresh — all 21 effect globals + dbg_params fields resolved), EYEBALL OWED,
  COMMITTED pending, 2026-06-20.** Ports the Effects tab's proven-mechanism controls to RmlUi: 4
  checkboxes (vol/rain/shadow-debug/outline) + 17 sliders (normals nrm_amount/relief/elev, vol
  density/intensity/reach/shadow, rain intensity + wet spec, shadow-mask str, mem dim/radius, sway
  amp/freq, outline thickness/alpha/mask-cut) each with a `{{var}}` live readout, bound by pointer
  to the same globals (`g_dbg_params.*`, `g_vol_*`, `g_rain_*`, `g_outline_*`) the ImGui sliders
  drive. **DEFERRED (own slices):** colour pickers (no native RmlUi control → RGB-slider or hex
  pattern), the cursor/place-light interaction (ImGui mouse logic), buttons (event-click callback),
  debug-mode combos (`<select>`).
  - **EYEBALL CHECK (user, A/B):** open the preview → Effects sections show; each slider drags its
    lighting param live (game render changes, `{{var}}` tracks), matching the ImGui slider; the 4
    checkboxes toggle their effect. Long panel scrolls.
  - **Remaining for Tier 8 (later slices):** colour pickers (theme/text/outline/cursor colours),
    lighting debug-mode combos (`<select>`), the 9 buttons (event-click), cursor/place-light, the
    Lighting/Animation/Runic/Diagnostics tabs, CRT sliders, world-text px/offset. Likely a TAB
    structure (reuse the `.tabs`/`.tab` component) once the flat panel gets unwieldy. Then F4 opens
    the RmlUi doc + retire `imgui_layer` (§8 gate).
- **Slice 4 — composite COLOUR PICKER — DONE + EYEBALL CONFIRMED (user, "looks perfect",
  2026-06-20), COMMITTED `6a2b89dcb5`.** A real SV-square + hue-strip picker (user asked for a
  proper one, not RGB sliders). Gradients render, drag maps, colour applies — all confirmed. Enabled by the discovery that the
  codebase's RmlUi render interface IMPLEMENTS gradient shaders (`linear/horizontal/vertical/
  radial/conic-gradient` → `rmlui_gradient.frag.hlsl`, rmlui_render_interface.cpp:622-638).
  - **SV square:** `#pk-sv` = `horizontal-gradient(#fff <pure-hue>)` (saturation; hue tint set
    from C++) with a child `.pk-sv-val` = `vertical-gradient(#00000000 #000000ff)` overlay (value);
    `#pk-hue` = rainbow `linear-gradient(180deg, 7 stops)`. Thumbs are absolutely-positioned divs.
  - **Interaction:** `drag:drag` on the surfaces + children; `data-event-mousedown`/`data-event-drag`
    → C++ handlers (BindEventCallback) map mouse pos (event mouse_x/y minus element
    `GetAbsoluteOffset` / `GetClientWidth/Height`) → S,V or H. `picker_apply()` converts HSV→RGB
    (local float helpers, NOT the lossy uint8 hsv_color.h ones), writes the target colour, and
    repaints the saturation gradient + thumb positions + swatch via `SetProperty`; `{{pk_hex}}`
    readout bound. `picker_init()` seeds HSV from the target via RGB→HSV on open.
  - **Target:** cursor-light colour (`cursor_light_emitter::color[3]`) — proves the mechanism;
    generalising to the outline (`g_outline_col_*[4]`) / theme / text colours is the next slice
    (likely a colour-target selector or one picker per colour).
  - **EYEBALL CHECK (user, A/B, Metal + D3D12) — the make-or-break runtime unknowns:** (1) do the
    GRADIENTS render (SV square shows white→hue left-right, dark at bottom; hue strip shows the
    rainbow)? (2) does DRAG work (drag in the square moves the thumb + recolours; drag the hue
    strip rotates the SV square's hue)? (3) does the swatch + `{{pk_hex}}` track, and does the
    actual cursor light in-game change colour (enable "omni light follows cursor")? If gradients
    don't paint, the RCSS gradient-decorator syntax needs adjusting; if drag doesn't fire, switch
    the `drag` event for `mousemove`-while-pressed.
- **Slice 5 — generalise the picker to the outline colours — CODE-COMPLETE + BUILD-GREEN (Metal,
  fresh relink 20:00, devui.o fresh), EYEBALL OWED, COMMITTED pending, 2026-06-20.** One picker,
  selectable target. A target table `g_pk_targets[5]` ({elem_id, float* rgb}) covers cursor light +
  the 4 hover-outline colours (`g_outline_col_hostile/neutral/friendly/self[4]` — RGB edited, alpha
  left). A swatch row (`pk_target(idx)` → BindEventCallback) selects the active colour: reseeds HSV
  (`picker_init`) + repaints. `picker_apply` writes the active target and repaints ALL target
  swatches from their live colour, outlining the active one white. `picker_init`/`apply` are now
  index-driven (was cursor-hardcoded).
  - **EYEBALL CHECK (user, A/B):** 5 swatches (cur/hos/neu/fri/slf) show their live colours; click
    one → its border goes white + the SV/hue picker jumps to that colour; edit → that swatch + the
    in-game effect update (hover-outline colours visible when you mouse over a creature with the
    outline effect on). Switching targets preserves each colour independently.
  - **Slice 5b — new↔orig swatches + revert (user request):** `picker_init` snapshots the target's
    colour into `g_pk_orig` on select; the row now shows **new** (current edit) beside **orig** (the
    snapshot). Clicking the orig swatch (`pk_revert` → BindEventCallback) restores it (reseeds HSV
    without re-snapshotting) → instant revert. `picker_apply` paints both. COMMITTED `be05da2b94`.
- **Slice 6 — theme/text colours via `<select>` combo — CODE-COMPLETE + BUILD-GREEN (Metal, fresh
  relink 20:12, devui.o fresh), EYEBALL OWED, COMMITTED pending, 2026-06-20.** Proves the LAST
  unproven control mechanism (`<select>`) AND routes the picker to the ui_theme named colours.
  - **3-kind target abstraction:** `pk_read_target`/`pk_write_target` dispatch on whether a fixed
    swatch (direct `float*`) or a named theme colour is active. Named writes go through
    `ui_theme::get/set_rcss_rgba` (+ `rmlui_layer::reload_theme()` — applies instantly) or
    `get/set_game_rgba` (+ `clear_nc_color_cache()` — applies on screen reopen), RGB edited, ALPHA
    preserved. `picker_init`/`apply`/`revert` now all go through the helpers.
  - **`<select>` combo:** options generated via `<option data-for="n : pk_names" data-value="it_index">`
    from a bound `Rml::Vector<Rml::String>` (rcss_names + game_color_names, built on open);
    `data-value="pk_combo_idx"` two-way + `data-event-change="pk_combo"` → activates the named target.
    RCSS styles `select`/`selectvalue`/`selectarrow`/`selectbox`/`option` with plain colours
    (scrollable dropdown).
  - **EYEBALL CHECK (user, A/B):** the theme/text dropdown lists all theme + game colour names;
    picking one routes the picker to it; editing an RCSS colour restyles open RmlUi panels instantly
    (e.g. a panel background/border colour), a game colour applies on screen-reopen; alpha is kept;
    the 5 fixed swatches still work + switching between a swatch and a combo entry is clean.
  - **★ ALL TIER-8 CONTROL MECHANISMS NOW PROVEN:** checkbox, slider, button (event-click+arg),
    `<select>` combo, gradient decorators, 2D drag, colour picker. Everything remaining is mechanical
    replication: the Lighting/Animation/Runic tab sliders + CRT sliders + world-text offsets (slider
    pattern), the debug-mode selector (`<select>`), the 9 buttons (event-click), the diagnostics
    read-out (bound text). Then a tab structure for navigability → F4 opens the RmlUi doc → retire
    `imgui_layer` (§8 gate).
- **Slice 7 — real TAB structure + drag-resize — CODE-COMPLETE + BUILD-GREEN (Metal, fresh relink
  20:24, devui.o + binary fresh), EYEBALL OWED, COMMITTED pending, 2026-06-20.** The flat 60-row
  panel is now 7 tabs (Screens / Effects / Colour / Lighting / Anim / Runic / Diag) so it's
  navigable before the remaining ImGui tabs are ported. Mechanisms all previously proven — this is
  pure layout.
  - **devui.rml:** a `.devui-tabs` bar of `.devui-tab` buttons, each `data-class-active="tab==N"`
    (highlight) + `data-event-click="devui_tab(N)"` (switch); each page wrapped in `data-if="tab==N"`.
    Screens/Effects/Colour pages hold the slice 1-6 content (moved verbatim, not rewritten); the 4
    not-yet-ported tabs (Lighting/Anim/Runic/Diag) are stubbed `(slice 8)`. A `#devui-resize` handle
    (bottom-right, `data-event-drag="devui_resize"`).
  - **devui.rcss:** `.devui-tab`/`.devui-tab.active` styling; scroll moved off the panel onto a new
    `.devui-body` (height 560px, `overflow-y:auto`) so the tab bar stays pinned while rows scroll;
    `.devui-resize` (needs `drag: drag` to emit drag events, like the picker surfaces).
  - **sdl_lighting_devui.cpp:** `int g_devui_tab` global + `c.Bind("tab", &g_devui_tab)`;
    `devui_tab` event callback (arg→g_devui_tab + `DirtyVariable("tab")` for instant switch);
    `devui_resize` callback (drag mouse_x/y → panel width + body height via `SetProperty` px strings,
    clamped 240-900 / 120-1200). `data-class-<name>` confirmed valid RmlUi data-binding (proven path).
  - **EYEBALL CHECK (user, A/B):** open the preview → 7 tab buttons; clicking one shows that page +
    bold-highlights the tab (active class); Screens/Effects/Colour all still work as before; the
    bottom-right handle drags to resize the panel (width + scrollable body height); stub tabs read
    "(slice 8)". **Make-or-break:** does `data-if="tab==N"` swap pages on click (the int-equality
    data-expr in data-if, cf. safemode's `row.sel_col==N` which works)?
  - **Remaining (slice 8):** fill the 4 stub tabs — Lighting/Anim/Runic sliders + CRT sliders +
    world-text offsets (slider pattern), debug-mode `<select>`, the 9 buttons (event-click),
    diagnostics bound-text read-out. Then F4 opens the RmlUi doc + retire `imgui_layer` (§8 gate).
- **Slice 8 — fill all stub tabs (full ImGui parity) + bigger/readable panel — CODE-COMPLETE +
  BUILD-GREEN (Metal, fresh relink 20:40, devui.o + binary fresh), EYEBALL OWED, COMMITTED pending,
  2026-06-20.** Every remaining ImGui control replicated in RmlUi; the dev panel is now a complete
  parallel of the ImGui F4 (mechanisms all proven in 1-7, this is bulk replication). ~90 new binds.
  - **Lighting tab:** debug-mode `<select>` (15 modes, int proxy `g_devui_dbg_mode` + `dbg_mode`
    event → `g_current_dbg_mode`/`g_dbg_params.debug_mode`) + 27 sliders (light scales, AgX tonemap +
    bloom, dither/GI/shadow incl. the `shadow_steps` uint reconciled to an int proxy each frame,
    vision, tone grade) + RC-readback button.
  - **Effects tab:** added the cursor-light controls (enable/radius/intensity + place-mode + clear
    button + `{{placed_count}}`); colour stays on the Colour tab; click-to-place still rides ImGui
    while it's primary (noted in-panel).
  - **Animation tab:** live-override + 4 effect checkboxes + 22 sliders bound straight to the
    `debug_anim_tuning()` singleton fields.
  - **Runic tab:** template `<select>` (event → `force_template`) + fixed-seed checkbox + Randomize +
    26 int sliders (incl. rune-ink R/G/B 0-255) + auto-regen checkbox + Generate/Save/Reload/Reset
    buttons. Auto-regen bumps `regen` in `rml_tick` via a cheap field-sum change check (ImGui used a
    per-slider `changed` flag; data-binding has none).
  - **Diagnostics tab:** the read-only HUD as one bound multi-line string (`build_diag_text()`,
    rebuilt each frame — mirrors `draw_diagnostics_tab`; shows the "enable F5" hint when the HUD's off).
  - **CRT + world-text** added to the Screens tab (CRT enable + 6 sliders bound to `rmlui_layer::crt()`;
    wt px/dx/dy bound to the `world_text_*()` accessors).
  - **Readability (user ask):** panel font 15→18px, title 17→22, tabs 13→15; default panel 400×660.
    `.devui-btn` (event-click span), `.devui-note`, `.devui-diag` (pre-wrap) styles added.
  - **EYEBALL CHECK (user, A/B):** each tab's controls drive their ImGui-equivalent param live —
    Lighting mode `<select>` switches debug view + all sliders; Animation knobs change sprite motion
    (with live-override on); Runic edits regenerate the border live (auto-regen) + Save/Reload/Reset/
    Randomize work; Diagnostics shows the live HUD text; CRT + world-text sliders match ImGui. Font is
    readable. **Make-or-break:** the two new `<select>`s (dbg_mode, ru_template) fire their change
    events (proven by pk_combo); the shadow_steps + runic auto-regen per-frame reconciles behave.
  - **★ TIER 8 NOW AT FULL PARITY — the RmlUi dev panel mirrors the entire ImGui F4.** Next: flip F4
    to open the RmlUi doc as primary + retire `imgui_layer` from the composite pass (§8 gate), after
    eyeball. (Cursor click-to-place + the ImGui-only mouse-placement logic must move at that flip.)
- **§8 FLIP — F4 opens the RmlUi doc; ImGui dev panel retired from the composite — CODE-COMPLETE +
  BUILD-GREEN (Metal, fresh relink 20:49, devui/render_frame/input + binary fresh), EYEBALL OWED,
  COMMITTED pending, 2026-06-20.** The dev panel is now RmlUi-primary. Minimal + surgical — no
  composite-pass edits needed (the elegant part):
  - **The mechanism:** F4 now toggles a NEW `sdl_lighting_devui::devui_visible()` flag instead of
    `imgui_layer::visible()`. `rml_tick()` (no longer arg-gated on imgui+preview) opens/closes the
    devui doc to match it. Since nothing flips `imgui_layer::visible()` any more, `imgui_layer::active()`
    stays false → the composite's `if(imgui_active)` branch never runs → **ImGui retires from the
    composite automatically, no edit to `composite_swapchain_pass_b`.** The doc registers in
    `rmlui_layer::active()` (any_open()), so it composites + captures mouse through the existing RmlUi
    path.
  - **Files:** `sdl_lighting_devui.{h,cpp}` — `devui_visible()` accessor + `g_devui_visible`;
    `rml_tick()` opens on that flag; legacy `g_devui_preview`/`draw()` left dead (compiled, removed at
    Tier 10). `sdl_render_frame.cpp` — `rml_tick()` call + the three `imgui_layer::visible()`
    cache-rebuild/despawn gates (structure/vis live-rebuild while tuning + test-light despawn) now read
    `devui_visible()`. `sdl_input.cpp` — F4 toggles `devui_visible()`; **cursor click-to-place moved
    here** (`place_test_light()` on a left-click not over the panel, `!rmlui_capture`, consumes the
    click) since the ImGui Effects-tab mouse path is dead.
  - **NOT deleted (Tier 10, gated on Tiers 7+9):** `imgui_layer` the module stays compiled — still
    inits + can composite for the dormant uilist ImGui pilot (`ui.cpp`, default off) and as a safety.
    "Retire" here = the dev panel is off ImGui + ImGui isn't in the live composite. Full ImGui deletion
    is the Tier 10 rip-out.
  - **EYEBALL CHECK (user, A/B, Metal + D3D12):** F4 opens the RmlUi dev panel (no ImGui window); all
    tabs/controls work; closing F4 hides it + returns world mouse; lighting sliders live-update (the
    rebuild gate now keys off the RmlUi panel); F5/F6/F7 debug keys still work (they were never ImGui-
    gated); place-test-lights: enable place-mode → click the world → a light drops (click consumed),
    closing the panel despawns them. **Make-or-break:** does the rebuild-gate flip keep lighting tuning
    live (SDF rebuilds each frame while the panel's open)? And does the world click reach
    `place_test_light` (not eaten by the panel) only when off-panel?

### Tier 9 progress (minigames — shared char-grid widget)

The 5 grid games — `lightson` / `snake` / `sokoban` / `minesweeper` / `robot_finds_kitten`
(`iuse_software*.cpp`, dispatched by `play_videogame` in `iuse_software.cpp`) — all draw a char
grid via `mvwprintz` into a `ui_adaptor` curses window in a blocking input loop. Per the locked
decision they share **ONE narrow reusable char-grid RmlUi doc** (NOT a general backend). Functionally
the widget is title + a monospace coloured-cell grid + a footer — the sidebar-hud slice-3 idiom
(`Vector<row{rml}>` + `data-for`/`data-rml`), and since the games are blocking loops the `rml_doc`
modal harness fits. One toggle (`minigames_rmlui_enabled()`) lights all five.

- **Slice 1 — shared widget + lightson — CODE-COMPLETE + BUILD-GREEN (Metal, fresh relink 21:12,
  minigame_rml.o + binary fresh), TOGGLE OFF, EYEBALL OWED (Metal + D3D12), COMMITTED pending,
  2026-06-20.** The reusable piece + the smallest game (lightson = a literal on/off grid).
  - **`src/minigame_rml.{h,cpp}`** — free-function facade (`open/active/close/set_title/set_footer/
    set_grid/sync`) over file-static model state (single active minigame; `rml_doc` guard keyed
    "minigame"). The bind lambda registers a `mg_row{rml}` struct + array and binds `title`/`footer`/
    `rows`; `set_*` take cata-colour-tagged strings → `cata_text_to_rml`. All RmlUi stays in the cpp;
    the header is SDL/RmlUi-free so each game includes it cheaply.
  - **`data/gui/minigame.{rml,rcss}`** — model `minigame`: `{{title}}` + `data-for row : rows`
    monospace grid (`white-space:pre`, Terminus → cell-aligned) + `{{footer}}`. Centred flex modal.
  - **`iuse_software_lightson.cpp`** — `minigame_rml::open(minigames_rmlui_enabled(), ctxt)` after the
    input_context; on_redraw early-branches to build title/grid/footer + `sync()` (skips curses) when
    active; `close()` before return. Cursor cell = bright yellow (RML markup is foreground-only, so the
    curses inverse-video `hilite` becomes a distinct colour — fidelity note).
  - **`rml_screen.h`** — `minigames_rmlui_enabled()` toggle (defined in minigame_rml.cpp). F4 devui
    checkbox + bind added (Screens tab).
  - **EYEBALL CHECK (user, A/B via F4 "minigames", Metal + D3D12):** find a video game (e.g. a handheld/
    console item) that runs "Lights on!" → with the toggle ON it renders as a centred RmlUi panel:
    title, the `#`(on, white)/`-`(off, grey) grid with the cursor cell yellow, legend+keys footer;
    arrows move the cursor, space/5 toggles + neighbours, win → "Congratulations" popup, r resets, q
    quits; toggle OFF → identical curses game. **Make-or-break:** monospace cell alignment under
    `white-space:pre` (proven by help's Movement grid) + the grid rebuilds each frame from state.
- **Slice 2 — the other 4 games (snake / sokoban / minesweeper / kitten) — CODE-COMPLETE +
  BUILD-GREEN (Metal, fresh relink 21:19, all 4 TUs + binary fresh), TOGGLE OFF, EYEBALL OWED
  (Metal + D3D12), COMMITTED pending, 2026-06-20.** The slice-1 widget served all four unchanged;
  each is just a game-state→grid producer + open/close wiring (render-behind-frozen-API).
  - **snake** — full-screen interior grid (rows 1..H-2): snake head `#` white / body grey, fruit
    `*` red; live + a GAME-OVER screen (title "GAME OVER" + score; drops the curses dead-snake ASCII
    art — semantic rewrite). close on QUIT + end.
  - **sokoban** — `mLevel[y][x]` glyphs (walls `#`, package `$` brown, on-goal `*` green, goal `.`
    red, player `@`); **walls render as `#`** (the curses box-drawing connection glyphs / red-bg goal
    tiles don't port to the RmlUi font — foreground-only, fidelity note). Title carries level/score/
    moves; footer the keys.
  - **minesweeper** — `level.x×level.y` grid by reveal state (unknown `#` / flag `!` yellow / seen
    number coloured by `aColors` / bomb `*` red); **cursor cell forced bright green** (no curses
    inverse). uilist difficulty + string_input bomb-count are already Tier-0 (stack over). close at end.
  - **kitten** — state-machined (instructions/main/bogus_message/end_animation); new `show_rml()`
    mirrors `show()`: field rows 2..rfkLINES (separator `_` + robot/kitten/bogus glyphs from
    `rfkscreen`) + per-state title/footer (bogus message → footer; end → "<3<3<3"). The game loop is
    in the ctor → a throwaway `input_context` drives the tick; close after the loop.
  - **EYEBALL CHECK (user, A/B via F4 "minigames", Metal + D3D12):** each game with the toggle ON
    renders as the centred RmlUi panel — snake moves/eats/dies, sokoban pushes packages + level
    switch, minesweeper reveals/flags/cursor-moves + win/boom, kitten instructions→move→bump
    message→find-kitten; toggle OFF → identical curses games. Watch the noted fidelity simplifications
    (sokoban walls, minesweeper/kitten cursor colour, snake game-over art).
- **★ TIER 9 COMPLETE (2026-06-20, eyeball owed).** All 5 grid minigames render through the one shared
  char-grid widget (`minigame.rml` + `minigame_rml`), one toggle. With the rip-out, curses can't draw
  them — this is the path that survives. Gate-blocker for Tier 10 cleared (pending eyeball).
- **★ TIER-9 DE-CURSE COMPLETE (2026-06-29, build+link green Metal).** Minigame curses fallback
  stripped — the RmlUi char-grid is the only live renderer:
  - snake `5bd03588c6` (prior session).
  - kitten/lightson/minesweeper/sokoban `e12b47fdc3` — removed orphaned `draw_level`/`print_score`/
    member draw windows + gutted `on_redraw` curses blocks (−278).
  - kitten orphan `show()` `c5831194dc` — the fallback-arm strip left `show()` callerless; deleted it
    + its `draw_robot`/`draw_kitten` helpers + inner field window `w` (−106).
  - **KEPT (load-bearing, NOT orphaned):** each game's *border* window (`w_border`/`w_minesweeper_border`/
    `bkatwin`/…) + its `newwin` — still drives `ui.position_from_window()` layout sizing under RmlUi.
    `grep mvwputch/wputch/werase\( w` over the 5 files = 0.

## Load-bearing architecture facts (verified this session)

- **Single curses chokepoint:** every non-map window renders through
  `cata_cursesport::curses_drawwindow()` (`sdl_curses_draw.cpp:306`), iterating a
  regular cell grid `WINDOW::std::vector<curseline> line` (`cursesport.h:34-50`,
  per-cell `ch` + `FG`/`BG`). We are NOT exploiting this for a generic backend
  (per strategy decision) — but it's where the legacy path gets deleted at rip-out.
- **Map/overmap bypass it:** `w_terrain`/`w_overmap` call the GPU tile path
  (`tilecontext->draw()` / `overmap_tilecontext->draw_om()`) directly. Untouched.
- **RmlUi already composites as an overlay pass** over the curses framebuffer in
  `sdl_render_frame.cpp` `composite_swapchain_pass_b()` (rmlui under, imgui on top),
  gated `imgui_active||rmlui_active`. Curses + RmlUi coexist on screen today.
- **Sidebar HUD** (`panels.cpp`): hybrid — **53 hardcoded `draw_*` functions**
  (`native_draw_registry()` `panels.cpp:2626-2688`) bridged to JSON widget layouts
  via `panel_manager`. Core struct `window_panel` (`panels.h:40-64`) holds a
  `std::function<void(avatar&, const catacurses::window&)> draw`. Redrawn **every
  turn** (`game::draw_panels()` `game.cpp:4322`, turn-gated, `always_draw` skips gate).
- **On-map text** accumulates in `std::multimap<point, formatted_text> overlay_strings`
  (populated `cata_tiles.cpp:3086-4164`), rendered after geometry. Sources: SCT
  (`animation.cpp:911/934`), zone labels (`cata_tiles.cpp:4130`), creature markers
  (`:3790`), debug overlays (scent/rad/temp/vis).
- **Popup completeness (PROVISIONAL — confirm in §8 sweep):** a flash-model
  explore reports the `popup()` family
  (`popup`/`popup_getkey`/`full_screen_popup`/`query_yn`) routes through
  `query_popup` (migrated) and `query_int`→`string_input` (migrated), with no
  separate `popup_window`/`draw_popup` path. BUT the prior player-menu plan
  explicitly suspected `popup_window`/`draw_popup` as independent — one grep is
  weak evidence for a rip-out *completeness* claim. **Re-verify in the §8 primitive
  sweep; if any popup helper draws its own curses window it's an orphaned path
  that blocks the gate.**
- **Embeddable widgets needing component treatment:** `draw_item_info`
  (`output.cpp:798/947/954`, **10 callsites** — inventory/craft/examine) and
  `scrolling_text_view` (3 callsites). Low-level primitives (`draw_border`,
  `draw_tabs`, `scrollbar`, `fold_and_print`, `print_colored_text`) are building
  blocks *inside* screens — replaced naturally by per-screen RML/RCSS.

## Settled invariants (carry forward, do not re-litigate)

1. **`input_context` owns the keyboard** (rebindable keys, hotkeys, IME,
   accessibility). RmlUi renders + handles **mouse only**. Per-event mouse-only
   `rmlui_layer::process_event` (gated `sdl_input.cpp:427`); keys fall through.
2. **Render-behind-frozen-API:** no call-site changes. Each screen's existing
   `ui_adaptor` `on_redraw` callback is rewritten to **sync an RmlUi data-model**
   instead of calling `mvwprintz`. The input loop stays. This generalizes the
   uilist pattern to **screens that use the `ui_adaptor`/`on_redraw` callback** —
   48 of 60 drawing files do. The other ~12 draw without that callback and may
   NOT fit the harness; **F.0 enumerates them before scaling** (see Foundation).
3. **Retained-mode:** per-screen document + data-model on the shared `Rml::Context
   "main"`; documents stack for nesting/modality (z = open order).
4. **Per-screen lifecycle:** rewrite → gate behind an OFF-by-default toggle →
   in-game eyeball (A/B vs curses) → flip default ON → commit. **No phase starts
   until the prior screen's eyeball passes** (build-blind on RmlUi runtime).
5. The map/world GPU tile render is out of scope and unchanged.
6. **RmlUi screens REQUIRE RmlUi popups (verified 2026-06-15, worldfactory eyeball).**
   RmlUi composites OVER the curses framebuffer, so a curses popup (`query_yn`/`popup`/
   `query_int` family) opened by an RmlUi screen renders UNDERNEATH the screen's doc →
   invisible → the loop blocks on an unseeable prompt ("can't exit" symptom). Therefore
   any screen whose toggle is ON must also have the Tier-0 popup toggles ON
   (`query_popup`/`string_input`) so popups stack as RmlUi docs on top. At the §8 mass
   flip this is automatic; during per-screen A/B, enabling a screen toggle without the
   popup toggles breaks its nested prompts (abort-confirm, filter, dependency errors).

---

## Cross-cutting Foundation (Tier F) — build before scaling

The leverage that makes ~45 bespoke screens tractable. Without this, every screen
reinvents borders/lists/scroll/colour and the rip-out completeness surface explodes.

- **F.0 Harness-coverage census — DONE (2026-06-10). Verdict: NO new orphan
  screens; the harness assumption holds.** 15 drawing files lack an in-file
  `on_redraw`; all resolve to a covered bucket: (1) uilist-callback driven
  (debug_menu, magic, magic_teleporter_list, wish, wisheffect) → Tier 0; (2)
  `dialogue_win` uses `ui_adaptor` via its caller (npctalk registers the
  `on_redraw`) → fits harness, Tier 5; (3) **embedded info-pane helpers**
  `print_info(window&)` (creature/monster/mtype/npc) + vehicle `print_*(window&)`
  (vehicle_display) → become **F.2 components** (same shape as `draw_item_info`);
  (4) on-map/overlay/debug text (animation/scent_map/map) → §7 world-text layer;
  (5) `cursesport` = curses backend infra → dies at rip-out.
- **F.1 Shared utils (promote from uilist):** move `nc_color → #rrggbbaa` palette
  map and `cata_text_to_rml()` (colour-tag→span + escape) out of `ui.cpp` into a
  shared header so every screen + the world-text layer use one colour/escape path.
- **F.2 Reusable RML component library** (`data/gui/components/` + `theme.rcss`
  classes + thin C++ bind helpers). The recurring primitives, authored once:
  - bordered panel + titled frame (replaces `draw_border`/`draw_custom_border`)
  - tab bar (replaces `draw_tabs`/`draw_tab`)
  - scrollable list / virtualized list (uilist already proves windowed binding)
  - native RmlUi scrollbar styling (replaces `scrollbar`/`draw_scrollbar`)
  - **item-info pane component** (`draw_item_info` × 10 sites) — bound from
    `item_info_data`; colour-delta lines via F.1
  - **creature/monster/npc info-pane component** (`Creature::print_info(window&)`
    + overrides) — embedded by look-around/examine; same pattern as item-info
  - **vehicle info-pane component** (`vehicle::print_vparts_descs`/`print_fuel_*`)
  - **scrolling-text-view component** (rich folded text + scroll; 3 sites)
  - colour/stat bar (HP/stamina/mana), key-value stat row, compass widget
- **F.3 Per-screen migration harness:** a small documented helper set mirroring
  `rml_open/rml_sync/rml_close` so a screen migration is mechanical: create model,
  bind, open doc, sync-on-redraw, close-on-exit, gate on a toggle.
- **F.4 Toggle policy:** one OFF-by-default toggle per screen (or per screen
  family) in the F4 panel, so each can be A/B'd in isolation; flipped ON when
  proven. (Independent uilist/query_popup/string_input toggles already split.)

**Exit:** components render in a scratch harness; item-info + scrolling-text-view
match curses output side-by-side. These are the only shared widgets; everything
else composes them.

---

## Per-screen tiers (easy→hard / value-first)

Each screen: rewrite using F.2 components → toggle → eyeball → flip ON → commit.
Counts/sizes from the surface survey.

- **Tier 0 — generic widgets (in-flight, finish first):** uilist, query_popup,
  string_input. Complete `rmlui-migration-remaining-phases.md` (2b colour spans
  ✓, callback ports ✓, virtual scroll ✓ — eyeballs owed). These ARE the pattern.

- **Tier 1 — simple text screens (warm-up, proves the harness):** help, scores_ui,
  mission_ui, diary_ui, loading_ui, mod_manager_ui, distraction_manager, auto_note.
  Mostly static text + a list → near-pure component composition.

- **★ RE-SCOPE GATE (hard checkpoint, after Foundation + Tier 1):** before touching
  the 130–180K-LOC monsters (Tiers 4/5/7), STOP and measure. Record real
  per-screen cost (author + bind + eyeball) from Tiers 0–1. Then explicitly decide
  with the user: **go** at current scope / **re-scope** (e.g. lighter treatment or
  defer the giants) / **adjust cadence**. A multi-month solo bet must not commit to
  the giants on optimism — this gate is the plan's most important control point.

- **Tier 2 — medium dedicated modules:** bionics_ui, mutation_ui, safemode_ui,
  auto_pickup, construction, armor_layers, computer_session, faction, crafting_gui.
  Lists + side panels + tabs; all covered by F.2.

- **Tier 3 — item-info + inventory family (high traffic, hard):** lands the
  item-info component first, then inventory_ui, game_inventory, then **advanced_inv**
  (multi-pane, dual-list, drag/move — hardest of the three; scope its panes
  individually).

- **Tier 4 — big bespoke menus:** worldfactory, main_menu, options (~172K),
  newcharacter (~177K). Options + newcharacter are tab-heavy forms — build a
  **form/tab-page sub-pattern** here; budget these as multi-screen sub-projects.

- **Tier 5 — interaction dialogs:** npctalk (~170K, dialogue tree + trade),
  ranged/targeting (~138K, live overlay tied to the map), iexamine, trade_win.
  Ranged overlaps the map → coordinate with Tier 6's overlay layer.

- **Tier 6 — overmap_ui + on-map static labels:** overmap_ui screen, plus zone
  labels / static `overlay_strings` → RmlUi DOM overlay positioned via world→screen
  projection (reuse sprite.vert projection constants: `SV_Position/tile_px −
  camera_off`). Transient/animated text goes to the §7 layer, not here.

- **Tier 7 — sidebar HUD / panels (architecturally hardest):** the 53 `draw_*`
  functions + messages. Continuous, every-turn, data-driven. Approach: a persistent
  HUD document whose data-model is synced in `draw_panels()` each turn (gate
  unchanged); each `draw_*` becomes a model-population fn + an RML fragment;
  preserve the JSON widget-layout system (map layout → RML fragment composition).
  Messages = a bound, coalesced list. Do this AFTER modal screens so the
  component library + colour/util path are battle-tested.

- **Tier 8 — F4 dev panel → RmlUi:** re-author the ImGui debug/lighting panel as
  RML/RCSS (live sliders → bound numeric inputs/events). Retire `imgui_layer` from
  the composite pass. Most `debug.cpp` menus already route through uilist (Tier 0).
  **Cost to call out:** this rebuilds your OWN primary tuning tool (the F4 lighting
  dev panel with its live sliders) mid-migration — temporary loss of tuning
  ergonomics until the RML version reaches parity. Sequence late, after the
  component library + bound-input controls are mature.

- **Tier 9 — minigames char-grid widget:** one reusable `<char-grid>` RML element
  (rows×cols of styled cells, bound from a cell buffer) used by kitten/sokoban/
  minesweeper/snake/lightson. Contained; ~1 widget + 5 small bindings.

- **Tier 10 — RIP OUT (gated on 100% coverage):** delete the curses-SDL cell/glyph
  renderer (`sdl_curses_draw.cpp` `draw_window` glyph/rect loop, font rasterization,
  `OutputChar` path), the catacurses SDL backend, ImGui from the build, and all
  per-screen fallback toggles. See §8 gate.

---

## §7 — World-space text layer (map overlays + future floating text)

The answer to "will it look clean + support future floating damage numbers."

- **Two classes of on-map text, two mechanisms, one renderer:**
  - **Static labels** (zone names): few, stable → RmlUi **DOM** overlay (Tier 6),
    repositioned per frame from world→screen projection.
  - **Transient/animated text** (SCT now; **floating damage numbers later**):
    high-churn, bursty, animated (rise/fade/scale/ease). DOM churn would thrash
    layout → instead an **imperative pooled text layer** that *reuses RmlUi's glyph
    atlas + render interface + font* (so it's the same crisp text and no separate
    font path survives rip-out), driven like a particle list.
- **Why this is clean + future-proof:** identical font/quality to all UI; supports
  fade/scale/easing/outline/shadow; world-anchored via the same projection
  constants as sprites; a single text renderer for the whole game. The future
  floating-damage feature drops onto this layer (emit text + lifetime + motion).
- **Build:** in Tier 6, alongside migrating `overlay_strings`. Reuses the existing
  `rmlui_render_interface` GPU path; no new font system. Debug overlays
  (scent/rad/temp/vis) route through the same layer (dev-only emitters).

- **MECHANISM PROVEN + RECONCILED (2026-06-16/17, supersedes a mid-course detour):**
  the imperative layer is REAL, not the raw-atlas access the §7 wording first implied.
  RmlUi 6.x exposes glyph layout through `FontEngineInterface::GenerateString` +
  `RenderManager::MakeGeometry`/`Geometry::Render` (managed layer) — slice 4.1 proved
  this end-to-end (SCT through `rmlui_layer::world_text_*`, eyeballed working), 4.2a
  generalized it to ALL `overlay_strings`. **TOMBSTONE:** a mid-course plan
  (`~/.claude/plans/continue-rmlui-migration-plan-md-drifting-river.md`) declared the
  imperative path "infeasible — no imperative glyph access" and prescribed a DOM
  overlay (`map_text_overlay.{h,cpp}` + `mapoverlay.rml`) re-synced per frame. That
  infeasibility claim was WRONG (it predates the FontEngine discovery) — the DOM
  module was NEVER built; on-map text is the imperative world-text layer. The DOM
  approach stays only as a deferred fallback IF per-frame churn ever bites (it has
  not). Do not resurrect the DOM plan.

---

## §8 — Rip-out completeness gate (Tier 10 entry condition)

Curses deletion is all-or-nothing. Before deleting anything, prove the floor:

1. **Every text-drawing path migrated:** all tiers 0–9 flipped ON and eyeballed.
2. **Primitive sweep:** grep confirms no live caller still depends on curses-only
   `draw_border`/`draw_tabs`/`scrollbar`/`fold_and_print`/`print_colored_text`/
   `mvwprintz`/`draw_item_info`/`scrolling_text_view` for *rendering* (callers now
   feed RML components). Any stragglers block the gate.
3. **No `wnoutrefresh`/`curses_drawwindow` reachable** for any non-map window.
4. **Font path:** the only remaining glyph consumer is the §7 world-text layer
   (RmlUi-atlas based), NOT the curses `OutputChar` cell loop.
5. **Bisect safety:** delete in order — toggles → ImGui → curses cell/glyph loop →
   catacurses SDL backend — each its own commit, build-green between.

### §8.1 — SWEEP RESULTS (2026-06-20) — the gate is NOT just "flip 32 toggles"

Ran the gate's step-2 primitive sweep. **Headline finding: the "~90% done, only Tier 10
left" framing in the frontier banner is WRONG.** Tiers 0–9 cover the *enumerated* screens,
but the sweep surfaced a **hidden backlog of ~12 interactive curses screens that no tier
(0–9) ever listed** — each has a live curses redraw loop and ZERO RmlUi path. These block
the rip-out exactly like an un-flipped toggle would. Method: cross-referenced every `.cpp`
calling a curses text primitive (`mvwprintz`/`fold_and_print`/`print_colored_text`/
`trim_and_print`/`draw_border`/`draw_scrollbar`) against the 33 `*_rmlui_enabled()` files;
the set difference is below. Reproduce: see the per-file `mvwprintz` density grep.

**Raw primitive caller totals (whole `src/`, all paths still wired — 32/33 toggles OFF):**
`mvwprintz`=913 · `wnoutrefresh`=321 · `fold_and_print`=144 · `trim_and_print`=119 ·
`print_colored_text`=97 · `draw_border`=76 · `right_print`=44 · `draw_scrollbar`=40 ·
`OutputChar`=21 · `draw_item_info`=15 · `scrolling_text_view`=12 · `draw_tabs`=11.
(Totals are uninformative until toggles flip + dead curses paths delete — they only confirm
"everything still live by design." The file-level cross-ref below is the real signal.)

**§8.1 BACKLOG PROGRESS:**
- **`character_display` (@ sheet) — DONE (build-green Metal, toggle OFF, eyeball owed), COMMITTED.**
  3-col grid of 6 tabs + speed + info + tip; 13 parallel producers (curses pristine), diary
  multi-pane focus + faction data-model, sync-in-all-9-guards. `character_display` toggle + F4
  checkbox. encumbrance reuses `encumbrance_lines`. Eyeball A/B: DIFF THE NUMBERS per tab
  (stats/skills/speed/encumbrance reconstructions) + D3D12. Mouse click-focus deferred; titles
  hardcoded English (i18n gap). The biggest blocker is cleared.
- **`messages` (full message LOG, the ESC log) — DONE (build-green Metal, toggle OFF, eyeball owed),
  UNCOMMITTED.** A scrolling text pane: one row per folded message line — a right-aligned time
  column (shown only when the time string changes; the curses ASCII time-range bracket glyphs
  LINE_XOXO/XXOO/OXXO are DROPPED, semantic rewrite à la diary's book border) + msgtype-coloured
  text via `cata_text_to_rml(colorize(...))`. Native RmlUi scroll replaces the curses offset
  windowing: UP/DOWN/PAGE_UP/PAGE_DOWN repurposed in the rml path to `SetScrollTop` on `#msg-screen`
  (offset math still runs, harmless), + a 3-frame initial scroll-to-newest-end for `!log_from_top`
  (history(0)=newest → rows run oldest→newest top-to-bottom). The transient FILTER help + input
  overlay is LEFT as the legacy curses `string_input_popup` (Tier-0) compositing on top — like
  diary's nested editor; only the log pane itself moved off curses. `rml_doc` opened at the tail of
  `dialog::init()` (must follow `init_first_time()` so open's `set_timeout(16)` lands on the member
  `ctxt`; idempotent across resize re-inits). `data/gui/messages.{rml,rcss}` + `messages` toggle +
  F4 checkbox + devui bind. **EYEBALL A/B (F4 "message log"):** ESC log shows time column + coloured
  lines matching curses; FILTER (`type:text` syntax) narrows the list + the curses filter overlay
  composites on top; RESET clears; UP/DOWN/PAGE scroll; COPY writes debug.log+clipboard; ERASE
  clears; wide/full-height toggles re-fold. **WATCH:** lines pre-folded to the curses window width
  (cosmetic, may not fill the RmlUi pane — construction/crafting precedent); time-range bracket art
  intentionally gone. **+ D3D12 glance** (first §8.1-backlog text-pane on the primary target).
  - **EYEBALL CONFIRMED CLEAN (user, 2026-06-21) — both `character_display` and `messages`.** "all
    screens up till now look good." Toggles stay OFF (the A/B control; mass flip-ON is the §8 step).
- **`morale` (player_morale::display) — DONE (build-green Metal, toggle OFF, eyeball owed),
  UNCOMMITTED.** Smallest §8.1 screen. Fixed title + Source/Value header (or a "Nothing affects
  your morale" line when empty), a scrolling middle list of morale sources (name + percent
  contribution, coloured green/red by sign, with "Total positive/negative morale" caption rows),
  and a fixed bottom block (Total morale / Pain level / Fatigue cap / Focus trend). Each line is a
  flex pair of a left label + right-aligned value, both pre-coloured via
  `cata_text_to_rml(colorize(...))` — REUSES the curses `morale_line` colour logic by adding public
  getters (`is_separator`/`get_left`/`get_right`/`get_color`) to that local class (curses draw
  pristine). Content is STATIC for the view → model built ONCE after open (not synced per frame),
  `on_redraw` just skips the curses draw, UP/DOWN scroll `#morale-mid` natively. ASCII separator
  glyphs + scrollbar + sticky-caption pinning DROPPED (semantic); blank spacer rows skipped.
  `data/gui/morale.{rml,rcss}` + `morale` toggle + F4 checkbox + devui bind. Same gotcha as
  messages: `rml_doc` opened AFTER the `ctxt("MORALE")` is built (open's `set_timeout(16)` lands on
  it). **EYEBALL A/B (F4 "morale"):** title + Source/Value cols; positive sources green / negative
  red with percent values matching curses; Total positive/negative caption rows; bottom Total
  morale + Pain + Fatigue cap (when present) + Focus trend match the curses numbers; UP/DOWN scroll
  a long list; empty case shows "Nothing affects your morale".
- **`martialarts` (ma_style_callback::key SHOW_DESCRIPTION) — DONE (build-green Metal, toggle OFF,
  eyeball owed), UNCOMMITTED.** Cleanest §8.1 screen yet. The style PICKER is a uilist (Tier-0
  covered); this migrates only the scrolling style-DESCRIPTION popup that opens over it. A single
  static text pane: the colour-tagged writeup via `cata_text_to_rml(replace_colors(buffer))`
  (`replace_colors` expands the `<info>`/`<good>`/`<bold>`/`<stat>` shorthand tags to `<color_x>`
  that cata_text_to_rml understands; `\n` survives → `white-space:pre-wrap` line breaks). TWO scalar
  string binds (title + body) — no struct/array registration needed. Model built ONCE at open;
  `on_redraw` skips curses; UP/DOWN/PAGE scroll `#ma-body` natively (curses line-offset windowing +
  border title + scrollbar dropped). Composites OVER the uilist's own RmlUi doc (distinct model
  names → both open fine). `rml_doc` opened after the `ict` context (open's `set_timeout(16)`).
  `data/gui/martialarts.{rml,rcss}` + `martialarts` toggle + F4 checkbox + devui bind. **EYEBALL A/B
  (F4 "martial arts desc"):** open the MA style picker, SHOW_DESCRIPTION (usually `?`/a key) on a
  style → description shows over the picker with colours (info cyan, good green, bad red, bold
  white, "[have]" mutations light-cyan); UP/DOWN/PAGE scroll a long writeup; title "Style: <name>";
  ESC/QUIT closes back to the picker.
- **`pickup` (pick_up_from_items) — DONE (build-green Metal, toggle OFF, eyeball owed),
  UNCOMMITTED.** Biggest §8.1 screen so far — the item PICKUP menu: a multi-select list (hotkey
  char + parent/pick mark + item name, selected row highlighted) over a scrolling item-info pane,
  plus a weight/volume header + footer hint line. RENDER-ONLY: the doc is rebuilt each frame
  (`sync_rml` in on_redraw) from `getitem[]`/`matches`/`selected`; ALL marking / count entry /
  filter / paging stay on the keyboard (+ the Tier-0 `string_input` filter popup). The curses
  `start`/`maxitems` paging is DROPPED for native scroll — the selected row is `ScrollIntoView`'d on
  change (via `#pu-list`'s child[selected], guarded; refine if it snaps oddly). The row name logic
  (money formatting / stack count / ITEM_SYMBOLS / stolen-`!` marker) is reproduced verbatim from
  the curses draw (curses path pristine); item-info pane = `format_item_info(sel.info(temp), {})` →
  cata_text_to_rml (ad-hoc text, same as crafting — the Tier-3 item-info F.2 component is still
  pending). `data/gui/pickup.{rml,rcss}` + `pickup` toggle + F4 checkbox + devui bind. **EYEBALL A/B
  (F4 "pickup menu") — exercise the multi-select state, not just the layout:** mark/unmark
  (RIGHT/LEFT) flips the +/#/- mark + recomputes the Wgt/Vol header; the selected row highlights and
  stays in view on UP/DOWN through a LONG list (the scroll-into-view check); count entry (digits)
  shows `#`; SELECT_ALL marks all; FILTER narrows the list (curses popup composites on top); the
  info pane matches the curses item info; parent/child marks (containers) colour right; CONFIRM
  picks up, QUIT cancels. **WATCH:** selected-row text keeps its item colour over the accent bg
  (curses used hilite-white); info/name pre-folded widths cosmetic (crafting precedent).
- Remaining backlog (real bespoke screens): ~~**veh_interact**~~ **DONE (all 6 slices, eyeball owed)** —
  the giant fully migrated (name/mode, stats+fuel, overview, parts, descs, ASCII diagram,
  install/repair). **gamemode_defense DONE** (both slices: setup form + caravan shop). Left: the
  creature-info trio (monster·mtype·npc = host-drawn helper, architectural — deferred) + the 3
  font-layer stragglers. RE-CLASSIFIED as covered-by-toggle (NOT separate, the sweep over-counted
  uilist-callback screens): `dialogue_win` (Tier-5 `dialogue` fallback), `magic` +
  `magic_teleporter_list` (`uilist`+`draw_rml`, covered by the uilist toggle).

**GATE-BLOCKER BACKLOG — unmigrated interactive screens (no toggle, curses redraw, rml_refs=0):**

| File | curses-text density | redraw markers | nature |
|---|---|---|---|
| `character_display` (+`character`) | 67 (+6) | 38 | the `@` character sheet. Tier 7 flagged "converge when char sheet migrates" — NEVER done. Largest blocker. |
| `gamemode_defense` **DONE (both slices, eyeball owed)** | 51 | 6 | Defense game-mode menus. Two interactive curses screens: `setup()` (settings form) + `caravan()` (between-wave shop). **Slice 1 DONE+committed:** the SETUP form (`refresh_setup`). Render-only field list — 9 settings (label + value + description) + 6 enemy toggles + 5 needs toggles, section headers, the selected field highlighted; SELCOL/TOGCOL colours reproduced, the cursor + all cycling/toggling/start stay on the keyboard, doc rebuilt each frame (live form). Method-local `rml_doc` + file-static `defense_setup_*` model (distraction-style sync-in-on_redraw). `gamemode_defense_rmlui_enabled()` toggle + F4 "defense mode setup" + devui bind. i18n gap: hardcoded "Defense Mode" title. **Slice 2 DONE (build-green Metal, toggle OFF, eyeball owed), UNCOMMITTED→committed:** the CARAVAN shop (`caravan()` + the 3 `draw_caravan_*` helpers). Render-only 3-pane doc (model `gamemode_defense_caravan`, reuses `defense_setup_row`): cash header ("Your Cash: $X -> $Y", balance red/green) + category list (left) + the selected item's `info_string` (left-bottom) + the item list ("name xN" + price red/green, right). The focused pane takes an accent border via `cat_active`/`items_active` (curses yellow border). All selection/buy/sell/confirm stays on the keyboard; the curses offset windowing dropped for native scroll. The same `gamemode_defense_rmlui_enabled()` toggle drives both screens. `draw_caravan_*` pristine. **WATCH:** the item list has no scroll-into-view yet (stage the one-liner if the cursor is lost on a long category). The mode is now fully migrated — one A/B pass owed (setup form + caravan, incl. D3D12). |
| `veh_interact` (+`vehicle_display`) **DONE (all 6 slices, eyeball owed)** | 39 (+13) | 11 | THE GIANT — migrated in slices. **Slice 1 DONE (build-green Metal, toggle OFF), UNCOMMITTED→committed:** lifecycle harness (`rml_doc` on the class, opened in `do_main_loop`, on_redraw branch at create_or_get_ui_adaptor) + the two zero-dependency panes (vehicle NAME + action MODE bar, parallel to display_name/display_mode, curses pristine). 2 scalar binds, no struct/array. `veh_interact_rmlui_enabled()` + F4 "vehicle interact". **Slice 2 DONE (build-green Metal, toggle OFF, eyeball owed), UNCOMMITTED→committed:** the STATS pane — new `stats_lines()` producer (parallel to `display_stats`, curses pristine) returns the speed/accel/mass/lift/buoyancy/cargo/status/wheels/most-damaged/drag/offroad/draft lines as colour-tagged strings; the curses 3-column slot layout + empty-slot padding DROPPED (semantic, one-column pre-wrap pane); 3rd scalar bind `stats_rml`. The fuel-indicator gauges (`print_fuel_indicators`) deferred to slice 4. **Slice 3 DONE (build-green Metal, toggle OFF, eyeball owed), UNCOMMITTED→committed:** the OVERVIEW pane (engines/tanks/batteries/power-gen/reactors/turrets/seats grouped list). First ARRAY bind for this screen (`veh_overview_row` struct + `overview_rows` vector, registered via guarded `register_veh_rml_types`). Reuses the `overview_opts` that `calc_overview()` builds at the loop top for grouping+hotkeys; reproduces only the uncapturable TEXT — free-static `veh_overview_header_label`/`veh_overview_caption` (the per-group header label + right caption, incl. the battery/generator/reactor epower loops) and a MEMBER `overview_detail` (per-part right column; a member so it keeps veh_interact's friend access to private `vehicle_part::base` in the tank branch). Highlight faithful to display_overview (`overview_pos`); curses offset windowing + `{`/`}` scroll hints dropped for native scroll. **WATCH:** during the interactive `overview()` selection sub-mode (refuel/etc., reuses the same ui adaptor) the selected row highlights but is NOT scroll-into-view'd yet (overview_pos is -1 in the passive main view, so harmless there) — stage the ScrollIntoView one-liner if the cursor gets lost on a long list. install sub-mode (slice 6) leaves the overview pane empty for now. **Slice 4 DONE (build-green Metal, toggle OFF, eyeball owed), UNCOMMITTED→committed:** the parts-at-tile list pane + the descriptions/message pane (`w_parts`/`w_msg`). Two `vehicle.cpp` (vehicle_display.cpp) text producers: `vehicle::part_list_text(p,hl)` (NEW, parallel to `print_part_list` detail=true; inlines the curses x-positioned symbols + right-aligned Interior/Exterior marker, drops scroll windowing, hl row → "> " prefix) and `vehicle::parts_descs_text(...)` (EXTRACTED from `print_vparts_descs`, which now delegates to it → curses output byte-identical; RmlUi calls it with `max_y=INT_MAX` to disable the scroll windowing). 2 scalar binds (`parts_rml`/`msg_rml`); msg pane shows the transient `msg` (red) or the folded descriptions. Body re-laid into two stacked columns (stats+parts | overview+msg). The fuel-indicator gauges (`print_fuel_indicators`) are split out to slice 4b. **Slice 4b DONE (build-green Metal, toggle OFF, eyeball owed), UNCOMMITTED→committed:** the fuel gauges appended to the stats pane. New `vehicle::fuel_indicator_lines()` (vehicle_display.cpp, parallel to `print_fuel_indicators` fullsize/verbose path + `print_fuel_indicator`): one colour-tagged line per printable fuel ("<fuel>  NN%" + the verbose rate / time-to-full/empty suffix, debug-mode variant included); the E…F ASCII gauge bar + the gauge windowing dropped (semantic). sync_rml appends them to the `stats_rml` string. **Slice 5 DONE (build-green Metal, toggle OFF, eyeball owed), UNCOMMITTED→committed:** the 2D vehicle DIAGRAM (`display_veh`'s `w_disp` glyph grid). New member `diagram_lines()` builds a monospace colour-tagged glyph canvas reusing `w_disp`'s dims (still allocated regardless of toggle): dark-gray guideline cross, one glyph per `all_standalone_parts()` (rotated `dir_symbol`/`part_color`, cursor tile hilited), centre marker (cursor part, red when the tile is obstructed). Box-drawing glyphs handled by a `veh_sym_to_utf8` helper mapping `special_symbol`'s `LINE_*` sentinels → their `_UNICODE` codepoints (the sentinels aren't codepoints). Rendered as per-row strings (`white-space:pre`, theme monospace — minigame-grid pattern) via a 2nd `Rml::Vector<veh_overview_row>` (`diagram_rows`, reusing the registered struct's `.text`). Debug CoM/pivot overlay + red-bg art dropped (semantic). Body re-laid into 3 columns (diagram+parts | stats | overview+msg). `cpart` is owned by `move_cursor` (set every cursor move), so the rml path skipping `display_veh` doesn't stale it. display_veh stays pristine. **WATCH:** glyph alignment under `white-space:pre` + theme monospace is the make-or-break eyeball (proven by the minigames/help diagram, but veh glyphs incl. rotated box-drawing); tile-graphics mode (`display_veh_tiles`, VEHICLE_EDIT_TILES) is NOT handled — the rml path always draws the ASCII diagram. **Slice 6 DONE (build-green Metal, toggle OFF, eyeball owed), UNCOMMITTED→committed → veh_interact MIGRATION COMPLETE (all 6 slices):** the install/repair sub-mode. When `install_info` is active the right column swaps (RML `data-if` on `install_active`/`show_overview`) from overview+msg to the installable-vpart list + the selected part's detail. `install_tabs_text()` (parallels the `draw_subtab` loop — selected tab bracketed, glyph art dropped); `install_rows` (3rd reuse of `veh_overview_row`: symbol in `info.color` + name in white/dark-gray by `can_potentially_install`, selected via CSS — list built in sync_rml since `can_potentially_install` is non-const); `install_details_text(sel_vpart_info)` (parallels `display_details` — bordered 2-column block flattened to one colour-tagged line per stat, small_mode abbrevs dropped). display_list/display_details/draw_subtab stay pristine. **WATCH:** install-list keyboard cursor has no scroll-into-view yet (native scroll; stage the one-liner if it's lost on a long category). **NOTE:** the whole screen is now migrated but still TOGGLE-OFF + eyeball-owed across all 6 slices — needs one A/B pass (incl. D3D12) covering: name/mode bar, stats+fuel, overview (+ the refuel `overview()` sub-mode), parts list, descriptions, the ASCII diagram glyph alignment, and the install/repair sub-mode. |
| ~~`magic`~~ (+~~`magic_teleporter_list`~~) **NOT A BLOCKER** | 33 (+1) | helper | RE-CLASSIFIED 2026-06-21: both are `uilist` + `uilist_callback` screens whose `draw_rml` override is ALREADY IMPLEMENTED (`spellcasting_callback::draw_rml` magic.cpp:2001, `teleporter_callback::draw_rml` magic_teleporter_list.cpp:210 — the full spell-info / teleporter side pane as RML). `select_spell` is a pure `uilist spell_menu` with no own window; ui.cpp:1030 calls `callback->draw_rml` in the uilist RmlUi path. So both render via the **uilist toggle** (the `refresh()`/`draw_imgui` curses+ImGui paths are the toggle-OFF fallback). Covered (gate part 1, not part 2). The §8.1 density flag was the fallback code. |
| ~~`messages`~~ **DONE** | 9 | 7 | The full message-LOG screen (ESC log). Tier-7 sidebar log MVP ≠ this. Migrated (toggle OFF, eyeball owed): scrolling time+text pane, native scroll, filter overlay stays Tier-0. |
| `monster` / `mtype` / `npc` | 13 / 14 / 11 | helper (NOT screens) | **RE-SCOPED 2026-06-21 (track A).** These are the producer files, not screens: `Creature::print_info_text()` (virtual; `monster`+`npc` override) IS the shared component — already built. mtype static info = `extended_description` = Tier-3 `description_view` (done). The host screens are where it surfaced — **BOTH DONE (toggle OFF, eyeball owed): `list_monsters` (`95a59a1c24`) + `look_around` info pane (`845e6498d5`, via new `print_all_tile_info_text()` mirroring all 8 sub-producers)**; editmap/wish hosts dev-only (stay). **Track A complete.** |
| ~~`morale`~~ **DONE** / ~~`martialarts`~~ **DONE** / ~~`pickup`~~ **DONE** | 1 / 0 / 4 | 3 / 3 / 3 | Small unlisted screens — all three migrated (toggle OFF, eyeball owed). morale screen, MA-style description, item pickup menu. |
| ~~`dialogue_win`~~ **NOT A BLOCKER** | 12 | 1 | RE-CLASSIFIED 2026-06-21: this is the curses FALLBACK of the already-migrated Tier-5 `dialogue` screen, not a separate screen. `dialogue_window`'s draw methods (`print_header`/`display_responses`/`print_history`) are reached ONLY in npctalk's `on_redraw` when `rml` is false; its sole caller is the toggle-gated `dialogue::opt` path. Covered by the `dialogue` toggle (gate part 1, not part 2) — dies with the toggle flip + rip-out. The §8.1 table mislabeled it. |

**★ ZERO-RML-COVERAGE CODE SWEEP (2026-06-22) — screens the §8.1 density table never listed.**
Scanned all 50 files with `catacurses::newwin` + `input_context`, filtered to those with NO rml
coverage at all (`rml_doc`/`_rmlui_enabled`/`draw_rml`/`open_document`). Only 6 hit; the table had
missed three real screens:
- ~~**keybindings editor** (`input_context::display_menu`, input.cpp)~~ **DONE (batch 17,
  2026-06-22, build+link green Metal, toggle OFF, eyeball owed)** — `keybindings_rmlui_enabled()` +
  F4 checkbox; list+legend → `"keybindings"` doc (windowed rows preserve the a-z hotkey mapping);
  filter stays Tier-0. The highest-traffic miss (reachable from every screen via HELP_KEYBINDINGS).
- ~~**Colors editor** (`color_manager::show_gui`, color.cpp)~~ **DONE (batch 18, 2026-06-22,
  build+link green Metal, toggle OFF, eyeball owed)** — `color_manager_rmlui_enabled()` + F4
  checkbox; header + name/Normal/Invert table → `"colors"` doc (native-scroll, sel_col highlights
  the active column); colour-pick uilists stay Tier-0.
- ~~**Blood-test results** (`character.cpp`, `BLOOD_TEST_RESULTS`)~~ **DONE (batch 20, 2026-06-23,
  build+link green Metal, toggle OFF, eyeball owed)** — `blood_test_rmlui_enabled()` + F4 checkbox;
  red-bordered title + per-effect coloured line list (green=good/red=bad, or "No effects.") →
  `"blood_test"` doc. Static dismiss-only popup; strings built once, re-DirtyVariable each redraw.
  This empties the zero-RML-coverage set (only dev-only `catalua_console`/`editmap`/`wish` remain,
  which stay by design).
- ~~**`scrollable_text`** (output.cpp:357)~~ **DONE (batch 19, 2026-06-22, build+link green Metal,
  toggle OFF, eyeball owed)** — `scrollable_text_rmlui_enabled()` + F4 checkbox; renders the visible
  window (`beg_line..+text_h`) to a `"scrollable_text"` doc so keyboard scroll still works. De-curses
  every caller of the primitive at once.
- Dev-only, stay: `catalua_console`, `editmap` (+`wish`).

**CAVEAT:** this filter only catches files with ZERO rml coverage; a partly-migrated file
(e.g. `game.cpp`, `iexamine.cpp`) could still hide a leftover curses screen behind no toggle.
A per-screen pass on the big multi-screen files is still owed for an exhaustive gate.

**★ FONT-LAYER STRAGGLER the primitive sweep MISSED — `loading_ui` splash author text
(gate step 4, not step 2).** The §8.1 cross-ref above swept the curses *primitive* layer
(`mvwprintz`/`fold_and_print`/…) and found `loading_ui` clean (0 primitive calls). But the
loading splash draws the "by <author>" overlay via `draw_sdl_text_outlined` (sdl_fonts.cpp)
→ `draw_string(*g_display.font, …)` — i.e. directly through the **curses SDL `Font` / glyph
path**, the exact renderer Tier 10 deletes. So it is a genuine gate-blocker against **step 4**
("the only remaining glyph consumer is the §7 world-text layer"), invisible to the step-2
primitive grep. `loading_ui::menu` (the uilist) is still Tier-0-covered as the plan said; the
SPLASH author text is the straggler. Fix options: (a) route the splash text through the §7
RmlUi text layer (`rmlui_layer::world_text_*` / managed `FontEngine::GenerateString`), or
(b) accept the splash as a documented exception that keeps a single non-curses SDL-font draw
alive (then gate step 4 must be reworded — currently it forbids exactly this). **Full font-layer sweep (2026-06-20, after loading_ui surfaced the dimension) — TWO MORE
stragglers found.** Method: grepped every caller of the glyph-draw entry points
(`Font::OutputChar`, `Font::draw_ascii_lines`, free `draw_string`, `draw_sdl_text_outlined`)
minus the backend impl (`sdl_font`/`sdl_fonts`/`sdl_curses_draw`/`sdltiles`/`cata_cursesport`/
`wincurse`/`ncurses_def`) and the §7 layer. Result:

| Font-layer caller | Class | Verdict |
|---|---|---|
| `sdl_overmap_draw` city names + note labels (×3, `draw_string`) | player-facing WORLD text | **DONE `0706194b93` (toggle OFF, eyeball owed).** Routed to §7 `world_text_add` via new `overmap_text_rmlui_enabled()` + F4 "overmap labels (text)". draw_om runs in the redraw cycle (pre-prepare) so the begin+add timing matches the proven SCT feed; bg rects stay (GPU). The clean one of the three. |
| `loading_ui` splash author (`draw_sdl_text_outlined`) | player-facing | **DOCUMENTED EXCEPTION (rip-out-time).** Not §7-routable as a slice: the splash draws on a SEPARATE render path (loading_image_splash), pre/around RmlUi-context init — not the in-game refresh_display frame that runs world_text prepare/render. Resolve at Tier 10 (accept one non-curses SDL-font draw, or special-case it then). |
| `sdl_render_frame` `tile_labels` (`draw_string`) | **dev/debug** coord overlay (lighting dev-UI, draws "mx,my" per tile in radius) | **NOT a clean §7 route — DEFERRED to rip-out.** Lives in `draw_lighting_overlays`, which runs AFTER `prepare()` (refresh_display order: prepare[776] → draw_lighting_overlays[889] → render_world_pass_w[893]) → a `world_text_add` here lands one frame late / after geometry is built. Dev-only → per the plan, delete with the dev tools at Tier 10, or restructure its submission to pre-prepare if kept. Not worth a build-blind frame-pipeline change now. |
| `sdl_window` (font construct/reset) | backend lifecycle | NOT a straggler — deletes wholesale |
| `sdl_framebuffer` (`font->width/height`) | backend METRICS only (curses cell rects) | NOT a straggler — deletes wholesale |
| `Font::OutputChar` / `draw_ascii_lines` | the curses cell loop | **fully contained** — ZERO non-backend callers (clean) |

So the font layer had **3 real stragglers** — **STATUS 2026-06-22: 1 routed, 2 deferred to rip-out.**
- **overmap labels — DONE** (`0706194b93`, toggle OFF, eyeball owed): the clean one (pre-prepare
  redraw-cycle timing → §7 `world_text_add`, like SCT).
- **dev tile-coord overlay — DEFERRED**: post-`prepare()` in `draw_lighting_overlays` → not §7-routable
  without restructuring; dev-only → delete-with-dev-tools or restructure at Tier 10.
- **loading splash — DOCUMENTED EXCEPTION**: separate render path (loading_image_splash),
  pre/around context init → resolve at Tier 10.
**Lesson: the gate has TWO sweep dimensions — the primitive layer (step 2) AND the font layer
(step 4); §8.1's first pass only did step 2.** Good news: `OutputChar` (the actual cell/glyph
loop) has no external consumers; with the screen backlog done + overmap labels routed, the only
glyph-path draw_string callers left are the 2 deferred dev/splash cases — both resolvable at the
rip-out (delete or special-case), so deleting the curses glyph path stays clean.

**NON-blockers (correctly handled by the existing plan / out of scope):**
- **Curses backend + primitive defs — DELETE WHOLESALE at Tier 10, do not migrate:**
  `output` (the primitive bodies live here), `color`, `input`, `popup`, `string_input_popup`,
  `string_editor_window` (the last is diary's known nested editor; it dies with the widget set).
- **Map / world-tile path — STAYS (not UI):** `animation`, `scent_map`, `live_view`, `editmap`
  (dev map editor), `character_preview`.
- **Dev-only — fold into the rip-out sweep, not a player screen:** `catalua_console` (already a
  §8 loose end), `wish` (debug spawn), `debug` (mostly uilist already).
- **False positive:** `rml_util` (a mention, not a draw).

**CORRECTED Tier-10 readiness (supersedes the frontier banner's item 5):**
The rip-out is gated on THREE things, not one:
1. **Flip + eyeball the 32 OFF toggles** (the enumerated tiers 0–9). User-task; build-blind debt.
2. **Migrate the §8.1 gate-blocker backlog** (~12 screens above) — these were never coded at all.
   Biggest: `character_display`, `veh_interact`, `gamemode_defense`, `magic`, full `messages` log.
   Creature-info (track A) — **COMPLETE**: `Creature::print_info_text()` IS the shared producer (done);
   mtype = `description_view` (done); both hosts migrated (`list_monsters` `95a59a1c24`, `look_around`
   `845e6498d5` via `print_all_tile_info_text()`). Only the 3 font-layer stragglers + the toggle flips remain.
   **PLUS 3 font-layer stragglers** (gate step 4, missed by the step-2 grep): `loading_ui` splash
   author text, `sdl_overmap_draw` city/note labels, `sdl_render_frame` dev tile-coord overlay —
   all §7-class (route to the §7 RmlUi text layer, or delete the dev one with the dev tools).
3. **Then** the deletion sequence (§8 step 5).
So the honest remaining-work picture is: enumerated migration ≈ done-pending-eyeball; **a whole
unlisted "Tier 7.5 / 9.5" of game screens (char sheet, vehicles, magic, defense mode, message
log, creature info) is still on curses and must be migrated before the curses renderer can die.**

### §8.2 — Deletion sequence (reachability-verified 2026-06-22)

The screen migration is code-complete; this is the ordered, build-green deletion plan for the
rip-out. **Correction to §8 step 5's stated chain** (`toggles → ImGui → curses glyph → backend`):
it is really **two near-independent sub-series**. ImGui is deletable *now* (already retired from
the live composite). The curses primitives are *caller-gated*, not toggle-flip-gated — `output.cpp`
(mvwprintz / fold_and_print / draw_border / draw_scrollbar / draw_tabs / draw_item_info) and
`cata_cursesport::WINDOW` have 50–100+ live callers and die only after every screen's curses
*fallback* branch (`if(!rml){ …curses… }`) is removed — which is what the toggle-flip eyeball
pass gates.

**Prep landed (this session):** the flip-all control (`rml_toggle_registry` +
`rml_toggles_set_all`/`reset_defaults`, F4 "All ON/OFF/Reset") makes the ~47-toggle eyeball pass
one action; the coverage audit (`sidebar_hud_has_producer` + `sidebar_hud_coverage_report`, F4
readout + one-shot DebugLog) makes the HUD flip gate mechanical. `native_draw_target_exists`
(panels.cpp) already existed.

**Sub-series A — ImGui. ★ RE-AUDITED 2026-06-23 — the old "deletable now, self-contained, ~5
commits, only ui.cpp+sdl_render_frame" claim was STALE on every point.** Ground-truth reachability
(grep on a clean tree):
- `ui.cpp` has **zero** imgui refs — the uilist ImGui fallback is ALREADY gone; uilist is
  permanently RmlUi. So is `cata_imgui.{cpp,h}` (0 refs — already deleted). Old steps 1–2 are no-ops.
- `imgui_layer` is referenced by **9** files, not 2: `sdl_render_frame.cpp` (init + Pass-B
  composite: `active/new_frame/prepare/render_in_pass/set_dev_ui`), `sdl_input.cpp`
  (`process_event` + `active()` capture gate), `sdl_window.cpp` (`shutdown()`), `sdltiles.cpp`
  (include only), `sdl_lighting_devui.cpp` (the `ImGui::Begin` panel), + comment-only mentions in
  `rmlui_layer.h` / `rmlui_render_interface.h` / two `.md`s.
- **`push_draw_callback`/`remove_draw_callback`: 0 callers** (the player-facing ImGui-menu pilot is
  gone). The ONLY `set_dev_ui` caller wires `sdl_lighting_devui::draw`.
- **`imgui_layer::visible()`: the only caller is the dead `ImGui::Begin` inside
  `sdl_lighting_devui.cpp:1361` itself.** Nobody sets it true; the live F4 lighting panel runs on the
  **RML** path (`devui_visible()`/`rml_tick()`, toggled at sdl_input.cpp:423). So `active()` is always
  false — ImGui is genuinely dormant (composites nothing, captures nothing).

**Two findings that resize the work:**
1. **Scope ≈ 10×.** `sdl_lighting_devui.cpp` is NOT a small dead `draw()` — the **whole 1551-line
   file** is the ImGui dev panel (189 `ImGui::` calls; 7 tabs: theme/lighting/effects/animation/
   rmlui/diagnostics/runic). Only 3 funcs are the live RML path (`devui_visible`/`rml_tick`/
   `place_test_light`) to keep. And because every `#include "imgui.h"` resolves through the `imgui`
   target's include dir, **dropping the link line breaks all 4 includers at once** → the use-removal
   must land in ONE commit, then the library unlink in the next. Not 5 tidy independent commits.
2. **It is NOT "deletable now" — it's the A/B reference for an UN-eyeballed replacement.** The plan's
   own discipline keeps the old path as the toggle-OFF A/B fallback until the RML replacement is
   *eyeball-confirmed*. The RML dev panel `devui.rml` (Tier-8 flip `8c6a66d7e6`) has full 7-tab
   parity (verified: 347-line doc, all knobs) **but is still eyeball-owed.** The dormant ImGui
   lighting panel is exactly the reference to diff `devui.rml` against. Deleting it first destroys the
   A/B net for an unverified panel — the same trap the curses sub-series B explicitly avoids.

**CORRECTED gate + order (build-green, each its own commit):**
- **GATE: eyeball-confirm `devui.rml`** (F4 dev panel) has parity with the ImGui panel on Metal +
  D3D12 — same as every other rip-out fallback. **DONE — user-confirmed green 2026-06-23.**
- **Commit A1 — DONE `1e5a316369` (build+link green Metal, binary relinked):** use-removal, library
  stays linked. Stripped `imgui_active` + the `if(imgui_active)` Pass-B composite blocks + `init`/
  `set_dev_ui` from `sdl_render_frame.cpp`; dropped `process_event` + the `imgui_layer::active()`
  capture arm + the repaint-gate imgui half in `sdl_input.cpp` (kept `rmlui_capture`); dropped
  `shutdown()` in `sdl_window.cpp`; dropped the imgui includes in `sdltiles.cpp`; **deleted the dead
  ImGui panel from `sdl_lighting_devui.cpp` — 3 slider/color helpers + all 7 tab fns + `draw()`
  (~1000 lines, the whole ImGui surface), KEEPING the RML path (`devui_visible`/`rml_tick`/
  `place_test_light` + the `devui.rml` data model, which is in a nested anon namespace INTERLEAVED
  between the tabs — excised per-function, not as one range)**; dropped `draw()` from the header.
  Also dropped the leftover `imgui.h` macro-guard include from the 6 ex-`draw_imgui` files
  (magic / advanced_inv / overmap_ui / wisheffect / wish / magic_teleporter_list). −776 lines.
- **Commit A2 — DONE `a277c32e68` (reconfigure + build+link green Metal):** deleted
  `src/lighting/imgui_layer.{cpp,h}`; dropped `imgui` from `target_link_libraries`
  (`src/CMakeLists.txt`); removed the `FetchContent(imgui)` + `add_library(imgui …)` block from root
  `CMakeLists.txt`; `cmake -S . -B out/build/osx-arm-slim` reconfigure re-globbed the removed file.
  −344 lines. **ImGui is fully gone from the build.** (Leftover `libimgui.a` on disk is a stale
  artifact, no longer a target — vanishes on a clean build.)

**★ SUB-SERIES A COMPLETE 2026-06-23.** ImGui ripped out end-to-end, build+link green on Metal,
binary verified relinked (mtime). Residual `imgui_scroll_to_selected` in `ui.cpp`/`ui.h` is a uilist
field NAME only (no library dependency) — left as-is. **Audit corrections proven by execution:** the
old "~5 tidy commits, only ui.cpp+sdl_render_frame" plan was wrong; reality was 9 files + the whole
1551-line `sdl_lighting_devui.cpp` ImGui body, done in 2 commits as predicted by the re-audit.
**Owed: a D3D12/Win11 build (the no-imgui link line is new there too) — Metal-green ≠ D3D12-green.**

**Sub-series B — curses glyph/backend (gated on the toggle-flip eyeball pass).**
**★ STARTED 2026-06-23 (user eyeball-confirmed sub-series B). ★ SCOPE CORRECTION from execution:
the curses BACKEND mostly SURVIVES — "delete the curses renderer entirely" (old steps 2–5) is NOT
achievable.** Ground truth: `curses_drawwindow` is called from `cursesport.cpp` inside
`wrefresh`/`wnoutrefresh`, and the **surviving** map/dev path still refreshes curses windows —
`live_view` (`wnoutrefresh`), `animation`, `scent_map`, `editmap`, `character_preview`, + dev
`wish`/`debug`/`catalua_console`. `OutputChar` is backend-internal (only `sdl_font.cpp`). So the
text-primitive bodies (`mvwprintz` etc.), `curses_drawwindow`, `sdl_font`/`sdl_fonts`, and
`cursesport` **stay** for those consumers. The realistic rip-out = **remove the migrated UI screens'
now-dead curses draw code** (the `if(!rml)` arms — mostly done in batches 1–13 — PLUS the orphaned
`draw_*` FUNCTION BODIES those arms used to call, which were left behind and still inflate the
primitive counts) + the `panels.cpp` HUD curses sidebar. The shared backend is NOT deleted.
- **Step 0 DONE `9cc0231444`:** all 48 screen toggles default ON (flipped the last 4 —
  keybindings/colors/scrollable_text/blood_test). Curses arms now dead at runtime.
- **Batch 1 DONE `ceebdbcfba` (build+link green, binary relinked):** deleted the orphaned
  `faction::`/`npc::`/`mtype::faction_display(window&)` curses draws (state→`follower_interaction_flag`,
  text→`faction_info_text` kept). First orphaned-draw-fn cleanup; the on_redraw arms were already gone.
- **Batches 2–5 DONE (build+link green each, binary relinked):**
  - B2 `0631dcc356` — bionics_ui (draw_bionics_titlebar/_tabs, draw_description, draw_connectors) +
    crafting_gui (draw_can_craft_indicator/_recipe_tabs/_recipe_subtabs/_hidden_amount).
  - B3 `e6a50d41a6` — gamemode_defense FULLY de-cursed: removed the caravan on_redraw curses arm +
    draw_caravan_* fns, refresh_setup, 2 stray setup draws → 0 prims.
  - B4 `442b0f5ff5` — mutation_ui (draw_exam_window, show_mutations_titlebar) + computer_session
    (computer_session::refresh + .h decl) → both 0 prims.
  - B5 `94b5c1353f` — help (help::draw_menu + decl) + armor_layers (draw_mid_pane, draw_grid) → both 0.
  - B6 `e1230ed030` — main_menu orphaned curses draws.
  - B7 `2f0003d954` — character_display orphaned curses draws (9-pane cascade).
  - B8 `d4343e6c3d` — worldfactory: deleted the modselection + confirm on_redraw curses ELSE-blocks
    (both behind `if(rml){sync_rml();return;}`, dead since toggle ON) + orphaned `draw_mod_list`/
    `draw_modselection_borders`/`draw_worldgen_tabs` (−346). **CORRECTION:** worldfactory was NOT cleanly
    "arms gutted" — batch-8-fallback-sweep only dropped the worldgen-tab else; the modselection + confirm
    else-blocks were RESIDUAL. KEY: `draw_empty_worldgen_tabs` STAYS — it has a LIVE unguarded caller in
    `edit_active_world_mods` (the rml backdrop-erase at ~1035). 3 on_redraw lambdas total: 2 dead/deleted,
    1 live backdrop.
  - B9 `4a90d1dc4c` — veh_interact: dead on_redraw else + 9 orphaned member draws (display_grid/veh/
    veh_tiles/stats/name/mode/overview/list/details), −740. B9b `8994c80e8d` — orphaned vehicle::print_vparts_descs.
    KEPT: print_part_list (editmap+game look), print_fuel_indicators (panels HUD), parts_descs_text (RML).
    calc_overview's 7 trim_and_print stay (LIVE fn — builds overview_opts the RML path needs).
  - B10 `9f97e3cfdd` — overmap_ui: gutted draw_om_sidebar's 206-line curses legend tail (after the
    if(rml&&handle)->build_om_sidebar_rml guard); fn stays for the RML sync, wbar param commented.
    KEPT (not orphans): draw_ascii + draw_map_labels/draw_city_labels (map-grid), place_ter_or_special
    (dev terrain mode), update_note_preview (LIVE — notes-list preview on_redraw @641 is unguarded).
  - B11 `de6952265f` — inventory_ui de-cursed (−234). NOT actually blocked: inventory.cpp:1122's base
    inventory_selector is a non-drawing keybind hack (all_bound_keys only, never executed). All executed
    selectors have uses_rml()=true → refresh_window's curses body was dead. Deleted it + the 5 orphaned
    member draws (inventory_column::draw + draw_frame/header/columns/footer). LESSON: a direct base-class
    instantiation isn't automatically a live draw path — check whether it's EXECUTED/shown.
  - **★ DE-CURSE CAMPAIGN COMPLETE after B11 (2026-06-23).** All bespoke screens + the panels HUD are
    de-cursed. The ONLY remaining rip-out-plan work is popup-migration (NEW RML authoring:
    ~~trade_win~~/~~safemode_ui~~/~~messages-filter~~/~~scores_ui-show_kills~~ — ALL DONE 2026-06-24) — a
    feature task, not a deletion batch. **★ POPUP-MIGRATION COMPLETE; eyeball pass owed on all four.**
  - **panels.cpp HUD curses sidebar — RIP OUT STAYS IN THIS PLAN (2026-06-23 user directive: rip out the
    curses panels EVEN IF the RmlUi HUD lacks features the old panels had).** Whole-sidebar curses
    suppression + delete the curses `draw_*` panel builders; un-built panels show a placeholder / are
    simply absent — accepted. SPLIT OUT to a separate future plan = only the NEW-HUD FEATURE WORK
    (minimap + bodygraph RTT, polish, full panel parity). So: delete curses panels now; perfect the RML
    HUD later.
  - Method that works: grep refs of each `draw_*`/`mvwprintz`-bearing fn; if refs = def + fwd-decl +
    comments only (no call site), it's orphaned → delete via the ripfn.py helper (delete_fn to the
    col-0 `}`; remove_decl up to `;`; remove decls BEFORE defs when they share a first-line prefix).
    Build (`.cpp`-only edits are fast; header edits recompile game.cpp ~5min) — unused-function warnings
    reveal cascade orphans; delete those too. The shared TEXT producers (e.g. `mutation_titlebar_desc`,
    `faction_info_text`, `encumbrance_lines`) STAY — the RML path uses them.
- **★ SCREEN CLASSIFICATION (triage 2026-06-23) — what's left after B1–B5:**
  - **Already clean (0 prims, nothing to do):** descriptions, options, newcharacter, ranged,
    game_inventory, examine_item_menu, pickup, distraction_manager, npctalk. (Their `if(!rml)` are
    benign null-guards in sync, not curses arms.)
  - **BIG de-curse jobs (migrated, arms gutted, but MANY orphaned draw fns / multi-pane):**
    `character_display` (multi-pane @ sheet; print_encumbrance orphaned + per-pane draws — caution: its
    helpers encumb_color/get_temp_conv/etc. cascade), `veh_interact` (60 prims) + `vehicle_display` (13;
    the `vehicle::print_*` methods are cross-file — verify no non-veh_interact caller before deleting),
    `overmap_ui` (80 — BUT most is `draw_ascii`, the ASCII map-grid render that STAYS with the backend;
    only non-map orphans are deletable), `worldfactory` (39, 4 arms), `inventory_ui` (17), `main_menu` (10).
  - **NEED POPUP MIGRATION first (live un-migrated curses sub-screens — NOT deletable, new RML work like
    auto_pickup's batch-14):** ~~`trade_win` (scrollable item-info popup)~~ **DONE 2026-06-24** (new
    `trade_iteminfo` doc + .rcss; show_item_data's curses w_popup now an rml_doc stacked over "trade",
    overlaying the examined pane via data-class-right; PAGE_UP/DOWN → SetScrollTop like help.cpp, UP/DOWN
    still exit-to-adjacent; curses kept as toggle-OFF fallback; build+link green Metal, EYEBALL OWED),
    ~~`safemode_ui` (wildcard-help + test-rule popups)~~ **DONE** (commit 89d5299ab1, mirrors auto_pickup's
    `autopickup_help`/`autopickup_test`), ~~`messages` (filter-help overlay)~~ **DONE 2026-06-24** (new
    data-bound `messages_filter_help` backdrop — the syntax help is DYNAMIC (lists registered msg-type
    names+colours) so unlike the static autopickup/safemode helps it binds a `help_rml` string;
    `filter_help_text(10000)` joined → cata_text_to_rml; opened lazily while `filtering`, closed on exit;
    the Tier-0 curses string_input field + `< >` markers composite on top of the backdrop's blank bottom
    row; curses help box kept as toggle-OFF fallback; build+link green Metal, EYEBALL OWED — esp. the
    backdrop-vs-input-row alignment, the known-fragile partial-migration seam), ~~`scores_ui` (the whole
    `show_kills` screen)~~ **DONE 2026-06-24** (new tab-less `scores_kills` doc — the show_kills twin of
    show_scores_ui's body, on the rml_doc harness; one bound `body_rml` via cata_text_to_rml, scrolled by
    SetScrollTop like the scores body; shares the scores_rmlui_enabled() toggle; curses scrolling_text_view
    kept as fallback; build+link green Metal, EYEBALL OWED). **★ ALL POPUP MIGRATION DONE.** `auto_pickup`
    already did its two (batch 14) — its residual 6 prims need a recheck.
  - **LEAVE (not orphans):** uilist-callback `refresh()` curses draws (advanced_inv `draw_squares`,
    magic/magic_teleporter_list, wish) — uilist KEEPS a curses fallback (`uilist::show` calls
    `callback->refresh()` at ui.cpp:922 for early-init before RmlUi is ready); shared text producers;
    and the whole map/dev backend.
  - **`panels.cpp` HUD sidebar — RIP-OUT DONE 2026-06-23** (P1 `1322cce459` + P2a `d40d1cd49e` + P2b
    `bc8980a098`, ~1900 lines, build+link green Metal). Curses sidebar fully gone; RmlUi HUD is the only
    sidebar. P1: draw_panels→HUD-only + make_native/make_bodygraph name-only. P2a: dropped 4 built-in layouts,
    ctor default→"custom", empty-layout guards, reload_widget_layouts→update_offsets (width_right now tracks
    custom layout — behavior change). P2b: deleted 58 draw_* + native_draw_registry/target_exists/fn + orphaned
    helpers + widget_test native cases. Unmigrated panels = [name] placeholder; body graph = placeholder.
    **EYEBALL+STARTUP OWED.** NEW-HUD feature work (minimap/bodygraph RTT, parity, polish) = separate future plan.
- **BUILD NOTE (this session):** the first `game.cpp` recompile after touching `npc.h` failed once with
  a phantom `butchery_activity_actor` "no matching constructor" — a STALE intermediate against the
  uncommitted SIM_PERFORMANCE `activity_actor*` edits, NOT a source bug (the 2-arg calls match). A clean
  rebuild fixed it; no SIM code was changed. If it recurs, it's PCH/intermediate staleness, not source.

**★ TILES-ONLY PRECURSOR (use_tiles neutralization) — STAGE 1+2 DONE, build-green.**
This fork is tiles-only, so the legacy ASCII-mode axis (`use_tiles` false) is dead. Stage 1
(`96129a4859`) + stage 2 (committed 2026-06-24, `+79 −195`) strip the always-true `if(use_tiles)`
guards and delete the dead `!use_tiles` ASCII else-branches across the map/UI render paths
(tile_iso, memorized symbols, spell/shape highlight drawsq, zone-overlay offset, editmap preview,
scent overlay, font dims, character preview, gamepad). `use_tiles`/`use_tiles_overmap` globals
stay **forced `true`** in `cached_options.cpp` as the anchor (not ripped — many readers remain).
**DEFERRED to steps 2–3 below (intentionally left dead-but-correct, global=true):** the large
ASCII-overmap bodies (`overmap_ui.cpp` `draw_ascii` + the `use_tiles_overmap` guards at 1678/2247)
and all of `sdl_curses_draw.cpp` (whole file dies in step 3). These are curses-body deletions, so
they ride the curses rip-out sequence below, not the guard-simplification pass.

Order, each build-green:
1. After all toggles flipped & eyeballed: delete each screen's curses *draw/redraw fallback*
   branch (the `if(!rml)` arms) → primitive callers fall toward zero.
   **PROGRESS (track-B (a) batches 1–13):** curses draw arm dropped from morale/scores/help,
   martialarts/computer/descriptions, the 5 live-sync screens, armor/pickup/construction/
   crafting/diary/faction, missions/item-examine/defense-setup, pickup/trade, character sheet,
   options/iexamine/worldfactory/main-menu, message log, advanced_inv, newcharacter
   (batch 11 — all 8 sub-screens, last Tier-4 giant; 207→0), overmap **search** (batch 12), and
   the **ranged target panel** (batch 13 — `draw_ui_window` + 10 exclusive panel_* helpers removed;
   kept uitext_title/uitext_fire, shared with rml).
   **★ CENSUS CORRECTION (2026-06-24): "de-curse complete" was optimistic.** A grep census of
   curses text-primitive callers (mvwprintz/mvwputch/fold_and_print/etc., excluding `output.cpp`
   = the primitive library, and tests) shows many migrated screens still carry their curses draw
   as **orphaned helpers or toggle-OFF fallback arms** that were never deleted. The eyeball gate
   passed 2026-06-23, so these are now deletable (step 1 tail). Caller counts at census time
   (non-test, excluding output.cpp): game 123 (mixed: map overlays STAY + dev + fallback),
   overmap_ui 45 (incl. `draw_ascii` map-grid which dies at backend rip-out), ~~trade_win 51~~,
   veh_interact 22, ui 22, panels 18, color 17, editmap 10 (STAYS — map path), string_input_popup 8,
   input 8, safemode_ui 6, diary_ui 6, character 6, messages 5, auto_pickup 5, morale 2,
   advanced_inv 1; dev minigames (snake/kitten/sokoban/lightson/minesweeper ~41) fold into the sweep.
   **BATCH (2026-06-24): trade_win DONE** — `trading_window::update_win` (the ~484-line curses
   draw fn, ~42 mvwprintz) was already orphaned (the `on_redraw` arm is RML-only; zero callers);
   deleted it + the `.h` decl. Build+link green Metal, 0 mvwprintz left in the file. The 4 curses
   windows (`w_them/w_you/w_head/w_whose`) are now vestigial (still created in `setup_win`) — clean
   those + the remaining `show_item_data` curses fallback in a follow-up. Eyeball not required
   (pure dead-code deletion, RML path untouched).
   **BATCH (2026-06-24): morale DONE** — `morale_line::draw()` (the per-row curses draw, the only
   mvwprintz/mvwhline/trim_and_print/right_print in the file) was orphaned after commit `187561`
   gutted the on_redraw; deleted it + the now-unused `middle_padding_min` local. The struct's
   accessors (`get_left/get_right/get_color/is_separator/max_width`) stay — the RML sync + window
   sizing use them. Build+link green, 0 primitives left.
   **BATCH (2026-06-24): diary_ui DONE** — gutted the 3 curses fallback arms (`ui_pages`/`ui_desc`/
   `ui_info` on_redraw, all `if(rml) return;` + curses below) and deleted the now-orphaned curses
   helpers in the anon namespace: 4 `print_list_scrollable` overloads + `draw_diary_border`
   (the ASCII-art border, ~120 lines, only `mvwprintw` user). Build+link green, 0 primitives left.
   Curses windows (`w_pages/w_desc/w_info`) vestigial (resize still creates them) — follow-up.
   **BATCH (2026-06-24): safemode_ui DONE** — gutted both curses fallback arms: the help popup
   (`help_ui.on_redraw`, `if(help_doc) return;` + fold_and_print/draw_border) and the test-rule
   popup (`ui.on_redraw`, `if(test_rml){sync;return;}` + the creature-list curses draw). Removed
   the dead curses scaffolding: `w_help` + `init_help_window`, and `w_test_rule_content` +
   `start_pos`. KEPT `w_test_rule_border` — load-bearing (`ui.position_from_window` drives the rml
   layout too). Build+link green, 0 primitives left. (Pre-existing unused `start_pos`@~199 in a
   different fn left as-is.)
   **BATCH (2026-06-24): character (blood-test) DONE** — `Character::conduct_blood_analysis` had a
   clean fallback arm (`if(bt_rml){DirtyVars}` + draw_border/trim_and_print into `w`); gutted the
   curses. `colors`/`w` stay (rml body-build uses `colors`; `position_from_window(w)` sizes the rml
   popup). **`Character::print_info` (11648, body bars) is LIVE — kept** (polymorphic with
   monster/npc::print_info; called by look_around/monster-info/editmap/wish via curses windows;
   that creature-info subsystem isn't migrated). `advanced_inv::query_destination_callback::
   draw_squares` (uilist `grid` callback) DEFERRED — unclear if RML still invokes it for the 3×3
   grid decoration; needs a uilist-callback trace before deleting.
   **TRIAGE (2026-06-24): panels (18) — NOT a clean delete, LIVE curses.** `draw_overmap_chunk`
   (ASCII minimap, ~488-536) is called by `magic_teleporter_list.cpp:182` (live). `decorate_panel`
   + the 2223-2264 block live inside `panel_manager::show_adm()` — the sidebar-options / panel-
   layout editor, a whole **unmigrated curses screen** (migration target, not a delete). The 319 +
   1784/1802 widget label/value drawers (~3 calls) would need individual tracing. None are
   fallback arms.

   **BATCH (2026-06-24): color + input (keybindings) DONE** — both had clean fallback arms
   (`color_manager::show_gui` on_redraw `if(cm_rml){sync;return}` + the full color-table curses;
   `input_context::display_menu` redraw `if(kb_rml){sync;return}` + the keybinding-list curses).
   Gutted both; deleted orphaned `color::draw_header` + the `w_colors`/`w_colors_header` windows +
   `iStartPos`/`iContentHeight`. KEPT `w_colors_border` (color) and `w_help` (input) — load-bearing
   (`position_from_window`; input's `w_help` also backs the filter `spopup.window`). The keybindings
   filter spopup is Tier-0, renders on its own path. Build+link green, 0 primitives in both.
   (Correction: the earlier inflection note guessed "color test menu = live" — it was a clean arm.)

   **BATCH (2026-06-24): auto_pickup DONE** — same dual-popup shape as safemode. Gutted the help
   (`help_ui.on_redraw`, `if(help_doc) return;` + fold_and_print) and test-pattern (`ui.on_redraw`,
   `if(test_rml){sync;return}` + match-list curses) arms; removed `w_help`+`init_help_window` and
   `w_test_rule_content`+`iStartPos`. Kept `w_test_rule_border` (load-bearing position_from_window).
   Build+link green, 0 primitives. (Pre-existing unused `iStartPos`@173 + `check_special_rule` left.)
   **ui.cpp (22, `uilist::show`) DEFERRED — DO NOT mechanically delete.** Those primitives are the
   shared curses fallback for EVERY uilist (gated by `rml_session`). Gutting commits all uilist
   consumers — incl. custom-draw callbacks like `advanced_inv::draw_squares` — to RML-only; that's a
   big eyeball-required decision across many screens, not a free arm-drop.

   **★ INFLECTION (2026-06-24): cheap single-screen deletes exhausted.** Session landed 8 clean
   de-curse batches (trade_win, morale, diary_ui, safemode_ui, character, color, input,
   auto_pickup) — orphaned helpers + toggle-OFF fallback arms, build+link green throughout. What
   REMAINS is live/shared curses that
   needs **per-screen migration** (heavier, eyeball-required), not deletion:
   `panel_manager::show_adm` (sidebar-options editor), the polymorphic `print_info` creature-info
   subsystem (look_around / monster sidebar), `veh_interact` part-picker (`overview()`), uilist
   custom-draw callbacks (`draw_squares`), `messages` in-game log, `color` test menu, the ASCII
   minimap, and the `game`/`overmap_ui` map overlays. Step 2 (delete `output.cpp` primitive bodies)
   stays blocked until these consumers migrate. Next mode = pick ONE screen, migrate, eyeball.
   **RECLASSIFIED — NOT clean orphan/fallback deletes (need per-site migration first):**
   `veh_interact` (22): the `overview()`/`calc_overview()` part-picker is a **live** curses path
   called by do_repair/refill/siphon/change_shape (NOT a dead arm). `messages` (1037/1070): the
   `ipk_target` print_colored_text is the **live** in-game message-log render. `auto_pickup` help +
   test-rule popups: verify before deleting. These are not free dead-code wins.
   **REMAINING curses-draw still live (each its own unit, NOT a simple arm-drop):**
   - `panels` (~303) = Tier-7 sidebar HUD — own strategy.
   - overmap **main display**: curses vs rml splits *inside* `draw()`/`draw_om_sidebar`, not a
     deletable arm; plus place-terrain/special editor (still curses). **Notes flow DONE:**
       - notes-manager `map_notes_callback` preview was ALREADY RML (`draw_rml` → uilist
         "callback" element); this session (batch 15) it was refactored to share a new
         `note_preview_rml(note, map_around)` producer.
       - `create_note` editor preview (batch 15): the curses w_preview/title/map panes now
         render via a passive backdrop doc `gui/overmap_note.rml` (`#preview` element filled
         each keystroke by the shared producer), stacked under the string_input "Note:" box,
         gated `overmap_rmlui_enabled()`, curses kept as toggle-OFF fallback. Build+link green
         Metal; eyeball owed (backdrop position vs the input box, minimap glyph parity).
     **place-terrain/special editor DONE (batch 16):** `place_ter_or_special`'s `w_editor`
     box now renders via passive backdrop doc `gui/overmap_editor.rml` (`#editor`), synced each
     redraw, gated `overmap_rmlui_enabled()`, curses fallback kept; the 5 narrow warning lines
     merged into one wrapping `_()` paragraph (dev wizard tool). Build+link green Metal, eyeball owed.
     **O3-B RESOLVED — overmap UI migration is COMPLETE (2026-06-22).** Audit of `draw()`/
     `draw_om_sidebar`/`build_om_sidebar_rml` shows EVERY overmap UI panel is already RML: the
     sidebar info, the **full `print_hint` legend**, and the footer are all mirrored into the
     `om_sidebar_session` model (`build_om_sidebar_rml`, overmap_ui.cpp ~1481-1653) and bound on the
     `"overmap"` doc; search/notes-manager/create_note/place-terrain editor are RML (batches 12/15/16).
     The only remaining curses is `draw_ascii` — the **ASCII map-grid render** (mvwputch glyphs into
     `w_overmap`), which is NOT a menu: it's the world view. There are two render paths — the tiles
     (sprite) overmap that survives rip-out, and `draw_ascii` (the `!use_tiles_overmap` ASCII path)
     which dies with the curses-cell backend at Tier-10, exactly like the in-game ASCII map (see the
     NOTE ~line 2055: "a §8 rip-out question, not slice work"). So nothing here is left to *migrate*;
     the curses sidebar/create_note/editor bodies remain only as toggle-OFF A/B fallbacks (delete at
     the eyeball-gated rip-out, not now).
   - ~~`ranged` aim readout (`print_ranged_chance`/`print_aim`) — slice-2b deferred.~~ **DONE
     (batch 14b, 2026-06-22): producers were already wired into `panel_text()`; this session
     deleted the 4 now-dead curses draw fns (−268 lines) on a clean tree, build+link green.
     Eyeball still owed (the keystone number-for-number A/B).**
   - ~~nested sub-popups in faction/armor_layers/auto_pickup~~ **BATCH 14 DONE (2026-06-22,
     build+link green Metal, toggle gates via `autopickup_rmlui_enabled()`, eyeball owed).**
     Ground-truth on a clean tree corrected the stale attribution: **faction & armor_layers have
     NO live nested popup** — their main draw-arms were already gutted (`if(rml)return;` at
     faction.cpp:945 / armor_layers.cpp:866) and the faction "member detail" is already the
     migrated `detail_rml` pane. The only genuine nested popups were BOTH in `auto_pickup.cpp`:
       - **test-rule popup** (`user_interface::test_pattern`) → new modal doc `autopickup_test`
         (centered match-list box stacked over the open "autopickup" doc; keyboard owns nav, mouse
         click/hover moves cursor). `data/gui/autopickup_test.{rml,rcss}`.
       - **wildcard help backdrop** (ADD/EDIT-rule flow) → passive static doc `autopickup_help`
         via `rmlui_layer::open_document(..., passive=true)` (no data model; literal English text,
         white-space:pre column alignment), stacked UNDER the string_input "Pickup Rule:" popup,
         closed after `query_string()`. `data/gui/autopickup_help.{rml,rcss}`.
     Both keep the curses draw as a toggle-OFF fallback (fresh build-blind migration) — the
     fallback-delete is a later de-curse step. **WATCH (eyeball):** test-list cursor scroll-into-view
     on long lists (native scroll, no ScrollIntoView yet); help backdrop position vs the string_input
     box (upper-centre, should stay clear); + D3D12.
2. Delete the now-unreachable curses text-primitive bodies in `output.cpp` once grep confirms
   zero non-test callers.
3. Delete `sdl_curses_draw.cpp`; delete `Font::OutputChar` / `draw_ascii_lines` (`sdl_font.cpp`) —
   verified zero non-backend callers.
4. Delete `sdl_font.{cpp,h}`, `sdl_fonts.{cpp,h}`.
5. Delete `curses_drawwindow()` + `set_window_transparent_backdrop()` then the `cursesport.cpp`
   impl. `cursesport.h` data structures (`WINDOW` / `cursecell` / `colorpairs`) stay until last —
   they back `ui_adaptor` + the map path (~19 includers).
6. Delete the toggle layer itself: `rml_screen.{h,cpp}` toggle accessors, `rml_toggle_registry.*`,
   the F4 dev-panel checkboxes/flip-all, and `devui.rml`'s screen-toggle section.

**STAYS (not UI — do not delete):** the map/world-tile path — `animation`, `scent_map`,
`live_view`, `editmap`, `character_preview`, `cata_tiles` — keeps `WINDOW` + the GPU tile path.
Dev-only (`catalua_console`, `wish`, `debug`) folds into the sweep.

**Deferred font stragglers resolved AT rip-out** (per §8.1 font sweep): the dev tile-coord overlay
(`draw_lighting_overlays`, post-`prepare()` → not §7-routable; delete with the dev tools) and the
loading splash (`loading_image_splash`, separate render path → accept/special-case).

**Mechanics:** CMake `GLOB_RECURSE CONFIGURE_DEPENDS` → deleting a `.cpp` needs no manifest edit,
**but a brand-new/removed file requires an explicit `cmake -S . -B <dir>` reconfigure** to re-glob
(`cmake <dir>` alone does NOT re-glob; verified this session). Always check the binary mtime after
a build — `| rtk err` can report success on a no-op/failed link.

---

## Risks / watch-fors

- **Scale (estimate is SOFT):** ~45 bespoke screens, several 130–180K LOC
  (options/newcharacter/npctalk/ranged/panels — those are file byte sizes, not a
  vetted effort model; treat any week-count as a guess until the Tier-1 re-scope
  gate produces a real per-screen cost). Tiers 4/5/7 are multi-screen sub-projects,
  not single passes. The F.2 component library is the only thing that keeps this
  finite.
- **Type-register reuse across reopen** (`RegisterStruct`/`RegisterArray` are
  context-global, persist past `RemoveDataModel`) — uilist proved safe; re-verify
  per new model. Failure mode: 2nd-open renders empty rows.
- **Per-instance model names (Phase-5 carryover):** fixed model-name + global
  guards force nested same-type screens to fall back. Resolve generally in Tier F
  (per-instance names) so nesting works everywhere, not per screen.
- **16ms tick:** every blocking loop must tick at 16ms when RmlUi active or
  hover/caret/animation freeze (uilist pattern).
- **Sidebar continuous render (Tier 7):** highest architectural risk — turn-gated
  every-frame model sync, not open/close. De-risk by doing it late.
- **Ranged/targeting (Tier 5) overlaps the live map** — couples to §7; sequence
  after the overlay layer exists.
- **D3D12 (Win11) cross-check** each tier's first screen (in-pass upload counters
  sane) — Metal-green ≠ D3D12-green for new dynamic docs.

## Open loose ends (flag, not blocking)

- `catalua_console.cpp` (Lua dev console) — dev-only; fold into Tier 8 or leave
  for the rip-out sweep (confirm it doesn't keep curses alive).
- Exact toggle granularity for Tier 7 (one HUD toggle vs per-widget) — decide when
  the HUD model shape is known.
- Font consolidation (custom `FontEngineInterface`) — only if cross-UI text drift
  appears; the §7 layer sharing the RmlUi atlas largely pre-empts this.

---

## Tier-10 §C — CORE WORLD-RENDER rip-out / ASCII-mode DROP (2026-06-29)

**Decision (user, 2026-06-29): DROP runtime ASCII mode (`use_tiles=false`).**
This unblocks eventual deletion of the curses backend. Irreversible for no-tiles play.

### Survey findings (the ~278 core-render curses calls are NOT one world render)
- `game.cpp` 183, `overmap_ui.cpp` 57, `panels.cpp` 33, `map.cpp` 5.
- The sidebar HUD is **already 100% RmlUi** — `game::draw_panels` says
  *"The legacy curses panel render path has been removed."*
- `use_tiles` is a **runtime** `extern bool` (cached_options.h, default `true`), NOT
  compile-time. Only **14 read sites**. The single curses *executable* is already gone
  (`cataclysm-bn-tiles` only). So ASCII mode = the runtime Options toggle, nothing else.
- **Keystone**: `sdl_curses_draw.cpp:316` `if(w==w_terrain && use_tiles)` → tile render,
  else → ASCII glyph render of the curses buffer. `:472` same for `w_overmap`. Under
  `use_tiles` the map `mvwputch` glyphs are already dead pixels.

### Four kinds of remaining core-render curses
- **A — bespoke interactive screens stuck in game.cpp → MIGRATE to RmlUi** (same as prior
  tiers; render identically in tile/ASCII so deletion is NOT an option, must migrate):
  `list_items` family (~59), `list_monsters` 25 + `mon_info` 4, `zones_manager` 15 +
  `is_zone_submap_grid_overlay_enabled` 18, `look_around` 8 + `print_*_info` look pane
  (~28), `panels.cpp::show_adm` 14 (the `}` panel-manager menu, live from handle_action).
- **B — overmap screen → MIGRATE** (rml path partly exists: `overmap_text_rmlui_enabled`,
  `sdl_overmap_draw`): `overmap_ui::draw_ascii` 21, `update_note_preview` 14,
  `place_ter_or_special` 18 (overmap editor, dev).
- **C — ASCII-only map glyph render → DELETE** (the actual "world render", now unblocked):
  `game::draw_minimap` (**DONE — orphaned, deleted `ecc47c6cf6`**), `draw_critter_internal`
  glyphs on w_terrain, `draw_ter` ASCII arm, vehicle dir indicator, scattered
  creature/monster/npc glyph draws, `sdl_curses_draw` w_terrain/w_overmap else-branches.
- **D — panels.cpp helpers** (`decorate_panel`, `make_value_widget_panel`,
  `draw_lua_widget_panel`) — called only inside panels.cpp; likely orphaned by the HUD
  rip-out → reachability pass → probably DELETE.

### Forced phase order (dependency-driven)
1. **P1 — Orphan sweep (low-risk, NOW).** Delete already-zero-caller curses fns in the
   core-render files. `draw_minimap` done. Continue: audit D helpers + any other orphans.
2. **P2 — ASCII map-glyph removal (category C).** Force `use_tiles=true`, delete the map
   glyph writers, collapse `sdl_curses_draw:316/472` to the tile path, drop
   `overmap_ui:1678` `draw_ascii` fallback. Remove the `use_tiles`/`use_tiles_overmap`
   Options entries.
3. **P3 — Migrate category-A screens** (one per session): list_items, list_monsters,
   zones_manager, look_around pane, show_adm.
4. **P4 — Migrate/confirm B overmap** (`draw_ascii` → rml, or confirm tile-overmap covers).
5. **P5 — Backend deletion.** Once only RmlUi windows remain, delete `sdl_curses_draw`,
   `cursesport`, and the `output.cpp` curses primitives.

### Gotchas
- `w_minimap`/`w_terrain` buffering in `sdl_curses_draw` (130-131) is the fragile
  persistent-backbuffer path (see `bug_persistent_backbuffer_loss`). Touch carefully in P2/P5.
- `editmap` is NOT mechanical (per memory). Treat as bespoke in P3/P4.
- Category-A windows (e.g. `w_missions`) are often kept load-bearing for
  `ui.position_from_window` sizing even under RmlUi — don't delete the window when you
  delete its draw block.

### P2 progress (2026-06-29) — all build+link green, tiles-only already in force

**Key proof (no eyeball needed for w_terrain writers):** the *only* renderer of `w_terrain`
is `sdl_curses_draw` branch 315 → `::tilecontext->draw(...)`, which renders from map data
and emits only the *output* `overlay_strings`/`color_blocks`. It never reads w_terrain's
curses cell buffer, and w_terrain never reaches the final `draw_window` else. **Therefore
every `mvwputch`/`wputch` into `w_terrain` is dead** (cata_tiles draws the real thing).

DONE this session:
- `ecc47c6cf6` — orphan `game::draw_minimap` (text 5×5 minimap; GPU pixel-minimap replaced it).
- `248071aa5a` — **keystone**: deleted the `!use_tiles` ASCII arms for w_terrain (map_font) +
  w_overmap (overmap_font) in sdl_curses_draw; collapsed the const `use_tiles`/`use_tiles_overmap`
  guards; w_terrain/w_overmap now unconditionally tile-rendered; dropped orphan `overmap_font` alias.
- `72fae4a792` — dead dup `game::draw_veh_dir_indicator` (cata_tiles.cpp:4069 draws it via
  `get_veh_dir_indicator_location`, which STAYS).
- `756ca00706` — orphan `game::draw_critter_highlighted` (zero callers).

**NEXT P2 sub-project — "creature-glyph sweep" (own session, eyeball at end):**
All write dead w_terrain cells; cata_tiles covers them — but high blast radius:
- `game::draw_critter` (1 caller: draw_ter loop) → `draw_critter_internal` → `Creature::draw`
  (creature.cpp ×3) → virtual `monster::draw` (monster.cpp ×11) / `npc::draw` (npc.cpp ×8) /
  Character glyphs (character.cpp ×3).
- `map::draw( w_terrain, … )` (map.cpp ×4) — the terrain glyph render. **HIGHEST RISK**: verify
  cata_tiles does NOT depend on side effects of `m.draw` (cache/memorized-tile/lighting) before
  deleting. Likely the call site in draw_ter is removable but `map::draw` the method may have
  other (non-w_terrain) callers — check.
- draw_ter leftovers: `destination_preview` auto-move trail (verify cata_tiles draws it),
  `draw_footsteps`, visibility indicator (game.cpp ~8403), trajectory/target glyphs (~10035).
- `debug_menu.cpp:1916/1919` sound `?` overlay; `editmap.cpp:260` cursor (**NOT mechanical** — bespoke).
Recommend: code-confirm cata_tiles coverage per item (as done for veh-dir/critter), delete writers,
then ONE in-game eyeball that critters/below-indicators/auto-move-trail/SCT still render.

### P2 creature-glyph sweep progress (2026-06-29 cont.) — all build+link green

DONE (provably-dead w_terrain writers, cata_tiles covers each — code-confirmed):
- `5b7b11ba51` — `draw_critter` wrapper chain (draw_ter loop + game::draw_critter +
  static draw_critter_internal). cata_tiles `draw_critter_at` covers critters.
- `3a7d879284` — static `draw_footsteps` + its draw_ter call site (cata_tiles
  `draw_footsteps_frame` covers it). `draw_sounds` param commented out, kept for overloads.

**KEPT — Creature::draw is NOT mine to delete:** it has LIVE callers in
`editmap.cpp:493,532` and the look-around pane `game.cpp:~8311` (`creature->draw(...)`).
It already no-ops under `is_draw_tiles_mode()`. Delete it + those call sites when editmap /
look-around migrate (category A + editmap-is-bespoke).
> **Caller-census lesson:** grep BOTH `\.draw\(` and `->draw\(` (pointer syntax) — the first
> pass missed the editmap/look-around pointer callers and a deletion broke the build. Always
> census every call syntax before deleting a shared method.

draw_ter is now down to TWO curses writers, both needing more than a code-grep:
- `m.draw( w_terrain, center )` (map.cpp:7068) — **HIGHEST RISK**, deferred. Verify cata_tiles
  has no dependency on its side effects (memorized tiles / cache / lighting) before removing.
- `destination_preview` auto-move trail → `game::draw_line(...w_terrain)` (game.cpp:4402).
  cata_tiles has a `draw_line()` for the firing/targeting trajectory, but it is UNCONFIRMED
  whether it renders the mouse auto-move preview trail. Needs an in-game eyeball (click-to-move
  and check the trail shows) before deleting — otherwise silent regression.

Other-file category-C still to census (per-file, same `->draw`/`.draw` caution):
`monster.cpp` ×11, `npc.cpp` ×8, `character.cpp` ×3, `map.cpp` ×4 curses calls;
`debug_menu.cpp:1916/1919` sound `?` overlay; `editmap.cpp` (NOT mechanical).

### P2 — CORE WORLD-RENDER curses ELIMINATED (2026-06-29, eyeball-confirmed all green)

draw_ter is curses-free and `map::draw` is gone. The actual terrain/creature/minimap
glyph render no longer touches curses. game.cpp curses: **183 → 104**.

- `86a2d236ad` — draw_ter: dropped `m.draw(w_terrain)` (no-op under tiles — map::draw
  early-returns) + the `destination_preview` auto-move trail. draw_ter now only sets the
  screen-reader cursor.
- `69c49a2ee8` — deleted orphaned `map::draw` (terrain glyph render + map-memory memorize
  loop). `draw_maptile`/`draw_from_above` kept (drawsq still uses them).

**REMAINING w_terrain writers = interactive-mode highlights/cursors only (the tail, not bulk):**
all write the unrendered w_terrain buffer; cata_tiles draws cursors/highlights via its own
overlay — confirm per-call + eyeball, and most ride along with the category-A screen migration:
- `game.cpp:8188/8298/8300` — look-around highlight + `creature->draw` (LOOK-AROUND = category A).
- `game.cpp:9961/9963` — targeting/trajectory cursor glyphs (target_ui path).
- `action.cpp:1165` — examine/peek highlight.  `construction.cpp:1654` — placement highlight.
- `editmap.cpp:260/534` — editmap (**NOT mechanical**).  `debug_menu.cpp:1916/1919` — sound `?` debug overlay.
- Shared methods kept until their callers migrate: `Creature::draw`, `map::drawsq`.

Next: fold these into P3 category-A migration (look_around first — owns 8188/8298/8300 + Creature::draw),
or a focused "w_terrain highlight overlay" sweep with one click-to-examine/target eyeball.

### P2 — w_terrain SWEEP COMPLETE (2026-06-29, user decision: delete vestigial, track reimpl)

The w_terrain curses cell buffer is no longer written by anything except editmap. All
terrain/creature/cursor/highlight/marker rendering goes through the tile path.

- `df4710c43d` — collapsed `draw_look_around_cursor` to the tiles `draw_cursor(lp)` path
  (dead !tiles ASCII branch removed).
- `4548df6553` — removed vestigial highlight/marker callbacks with NO tiles equivalent:
  pickup target highlight, `draw_trail` end marker (X/^/v), examine/peek (action.cpp),
  construction placement, DEBUG_SHOW_SOUND markers. Each left a `TODO(tiles-rip-out)`.

**FOLLOW-UP (tracked, not yet done) — re-add these indicators via the tiles cursor/highlight
overlay (`init_draw_cursor` / `init_draw_highlight`), then they show in tiles again:**
pickup target · examine/peek adjacent-tile · construction valid-placement · trajectory
end + z-direction marker · monster-sound debug overlay. Search `TODO(tiles-rip-out)`.

**ONLY remaining w_terrain writers: `editmap.cpp:260` (cursor mvwputch) + `:534` (drawsq
highlight)** — editmap is bespoke/non-mechanical; handle during an editmap-specific pass.
`map::drawsq` and `Creature::draw` are now reached ONLY from editmap → both become deletable
once editmap is migrated/de-cursed.

**game.cpp curses 183 → 102.** The remaining 102 are NOT w_terrain — they are category-A
interactive screens that render into their own (displayed) curses windows and need real
RmlUi migration (P3), not deletion: `list_items`/`reset_item_list_state`, `list_monsters`
(+ `monster::print_info`), `zones_manager`, and the look-around INFO pane
(`print_all_tile_info` → `print_terrain_info`/`print_*_info`, which also drives the
`monster.cpp`/`npc.cpp`/`character.cpp` `print_info` panes). Plus `panels.cpp::show_adm`.

NEXT (P3): migrate one category-A screen — recommend `list_items`/`list_monsters` (self-
contained, high-traffic) or the look-around info pane (unlocks monster/npc/character panes).

---

## ★★★ RESUME HERE — Tier-10 §C next session (clean handoff, 2026-06-29) ★★★

### Where we are
Core world-render (w_terrain) is **fully de-cursed** and tiles-only is already in force
(`use_tiles`/`use_tiles_overmap` hardcoded `true`, Options entry gone). The map viewport,
terrain, creatures, cursor, highlights, and trajectory all render via the tile path
(`cata_tiles` / `sdl_curses_draw` branch 315). `game.cpp` curses calls: 183 → 102.
Build + tests green, in-game eyeball confirmed.

### The invariant that drove §C (don't re-derive)
`sdl_curses_draw` renders `w_terrain`/`w_overmap` ONLY via `tilecontext->draw` (from map
data) and **never reads their curses cell buffer**; those windows never hit the final
`draw_window` else. So **any `mvwputch`/`drawsq`/`->draw` into `w_terrain` is dead** — delete
it once you confirm `cata_tiles` covers that visual (terrain, `draw_critter_at`,
`draw_footsteps_frame`, `draw_cursor`, `init_draw_line`, `init_draw_highlight`).

### Loose ends in §C (small, optional)
1. **editmap** (`editmap.cpp:260` mvwputch, `:534` drawsq) — the ONLY remaining w_terrain
   writers. Bespoke/non-mechanical. Once handled, `map::drawsq` AND `Creature::draw` become
   orphaned (editmap is their last caller) → delete both.
2. **`grep TODO(tiles-rip-out)`** — 5 indicators removed but not yet re-added in tiles
   (pickup/examine/construction highlight, trajectory end-marker, debug sound). Parity-neutral
   (they rendered nothing under tiles already). Re-add via `init_draw_cursor`/`init_draw_highlight`
   when convenient — feature work, not blocking.

### NEXT MAJOR PHASE — P3: migrate category-A screens to RmlUi (NOT deletion)
The remaining 102 game.cpp curses calls are interactive screens rendering into their own
*displayed* curses windows. These need real RmlUi documents, one screen per session. Targets
(curses-call weight in game.cpp):
- ~~`list_items` family~~ **DONE 2026-06-29 (`7c30b7e2f1`)** — RmlUi render path added
  (twin of list_monsters): `data/gui/list_items.{rml,rcss}` + `list_items_rmlui_enabled`
  toggle (default on); curses `on_redraw` body retained as gated A/B fallback. Info pane
  via `item_info_rml_lines`. Build + test build green (Metal). **EYEBALL-CONFIRMED 2026-06-29.** Note
  `print_items_info` (the look-around tile-item pane, game.cpp:~8546) is a SEPARATE
  render path shared with look-around — migrate it under the look-around INFO pane target,
  NOT here. `find_nearby_items` is pure data (no curses), already fine.
- `list_monsters` — already migrated (in-file twin used as the reference). `monster::print_info`
  in monster.cpp still curses (look-around path).
- look-around INFO pane: `print_all_tile_info` → `print_terrain_info` / `print_fields_info` /
  `print_trap_info` / `print_vehicle_info` / `print_graffiti_info` / `print_visibility_info`,
  plus the creature panes `monster.cpp`/`npc.cpp`/`character.cpp` `print_info`. Migrating this
  pane unlocks all three creature-info files at once.
- ~~`zones_manager`~~ **DONE 2026-06-29 (`4aba15d3aa`)** — RmlUi render path (sibling of
  list_items): `data/gui/zones_manager.{rml,rcss}` + `zones_manager_rmlui_enabled` toggle;
  3-pane panel (zone list / options block / shortcut footer), hidden during nested
  point-selection look_around. Curses body kept as gated A/B fallback. **EYEBALL-CONFIRMED 2026-06-29.**
  `is_zone_submap_grid_overlay_enabled` is the map-overlay path (separate, untouched).
- ~~`panels.cpp::show_adm`~~ **DONE 2026-06-29 (`b945e6a589`)** — RmlUi render path: centered
  3-column modal (`data/gui/panel_adm.{rml,rcss}` + `panel_adm_rmlui_enabled` toggle), panel
  list / help / layout list with a 2D cursor; swap-drag reproduced via display-order emit.
  Curses body kept as gated A/B fallback. **EYEBALL-CONFIRMED 2026-06-29.**

**All five originally-listed P3 targets are now migrated.** Re-survey (2026-06-29) found the
straggler set:
- ~~`live_view`~~ **DONE 2026-06-29 (`89f2bd36b3`)** — the SDL mouse-hover tile tooltip, a NON-modal
  passive overlay (rmlui_layer doc lifecycle, not rml_doc). Fed by the already-migrated
  `print_all_tile_info_text`. `data/gui/live_view.{rml,rcss}` + `live_view_rmlui_enabled`. **Eyeball owed.**
- ~~`scent_map::draw`~~ **DELETED 2026-06-29 (`085d0649ee`)** — was DEAD (zero callers; debug scent goes
  through `display_toggle_overlay`, a GPU overlay). Removed def + decl + orphaned `sev()` + color/cursesdef includes.
- ~~`character_preview`~~ **DONE 2026-06-29 (`439b8e1ee3`)** — NOT a text screen (the avatar is a GPU
  sprite via `display_avatar_preview_with_overlays`); the only curses was a single `draw_border` +
  "CHARACTER PREVIEW" title. Replaced that chrome with a non-modal passive RmlUi backdrop (rmlui_layer
  doc lifecycle, like live_view): bordered box, transparent centre so the sprite shows through, `#cp-box`
  left/top/width/height set each redraw from the preview rect (cells→TERM%), hidden when too narrow.
  `data/gui/character_preview.{rml,rcss}` + `character_preview_rmlui_enabled` toggle + registry row;
  curses `draw_border` kept as gated A/B fallback. Build + test build green. **Eyeball owed.**
  → removes `character_preview` from the P5 backend-deletion blocker list below.
- `editmap` (dev map editor) — explicitly NON-mechanical / deferred (has an `editmap_info` backdrop
  toggle already; the cursor/menus are map-path + uilists). Not a clean mechanical migration.
- Creature-info `print_info` (monster/npc/character.cpp) — curses, but only as the A/B fallback for
  the migrated `look_around` info pane; deletion is P5, not a new migration.

Player-facing P3 is effectively complete (live_view was the last interactive straggler).

**P4 — ASCII map-render confirm: DONE 2026-06-29.**
- Overmap `draw_ascii` — was DEAD (dispatch always takes the tile-redraw else; `use_tiles*` forced true)
  → DELETED `05c4ccfeed` (−664: draw_ascii + draw_city/map_labels + get_scent_glyph/has_player_label/
  get_map_label_text cascade). Graphical overmap renders via sdltiles; labels via §7 world-text.
- In-game `w_terrain` — NO dead ASCII branch exists: `sdl_curses_draw.cpp:315` renders w_terrain ONLY via
  `tilecontext->draw` (the §C work already made it tiles-only). Nothing to delete.
- `draw_ascii_lines` (sdl_font.cpp) is the backend box-drawing glyph routine for the generic cell
  renderer — P5, not a map path.

**editmap NSA de-cursed: DONE 2026-06-29 (`1a926da72c`, −464).** `uber_draw_ter` (dead) +
`editmap_hilight::draw` (dead mvwputch) deleted; `draw_main_ui_overlay`'s dead w_terrain writes removed
(drawsq's one live effect — `draw_highlight` when no critter — preserved as a direct GPU call). This
orphaned **`map::drawsq` + `Creature::draw`** (editmap was their last caller) → both deleted, plus the
chain they alone fed (`map::draw_maptile`/`draw_from_above`, file-local `has_memory_at`/`get_memory_at`).
A big curses-cell-renderer chunk is now gone.

**Remaining for P5 (backend deletion), gated on eyeball sign-off + dev consumers:** the generic curses
cell/glyph backend (`output.cpp` primitives, `cursesport`, `sdl_curses_draw` non-terrain `draw_window`,
`sdl_font`/`Font::OutputChar`/`draw_ascii_lines`), neutralizing the `use_tiles` option, and removing the
per-screen `*_rmlui_enabled()` toggle layer + retained curses A/B fallback bodies after the daily-drive
bake. (`character_preview` border DONE `439b8e1ee3` — no longer a bespoke blocker; its `draw_border`
fallback dies with the generic backend cut.)

**★ FULL-RIP CAMPAIGN (user authorized "full rip" 2026-06-29).** Census reframed P5: the backend cull
is **mechanically reachable — no feature-completion blocker** (the feared `vehicle_display` primary-curses
turned out to be live TEXT producers + dead fallback; the 2D veh diagram was already deleted). Done this
session: `use_tiles`/`use_tiles_overmap` globals removed (`06b7efb0c6`); orphaned `print_fuel_indicators`/
`print_fuel_indicator` cluster deleted. Remaining deletable chain (all dead fallback, priority order):
(1) creature-info `print_info(window)` + its 4 callers [look_around/list_monsters/editmap/wish];
(2) `print_part_list` (orphans with #1's look_around+editmap blocks); (3) magic.cpp uilist-callback curses
fallback; (4) dialogue_win curses impl; (5) game.cpp ~135-prim remnants; (6) generics popup/string_input_popup/
uilist (high blast radius); (7) dev wish/catalua_console; (8) veh_interact/advanced_inv remnants; (9) backend
cull + toggle layer (LAST). Full batch detail + caller census in the `project_rmlui_gate_backlog` memory.

**How to migrate (follow existing Tier 2–9 pattern):** add an `xxx.rml` + `xxx.rcss` under
`data/`, a `xxx_rmlui_enabled()` toggle (default true) like the others, build the model in a
`*_rml` helper, gate the existing `ui_adaptor` redraw with the RmlUi path, and keep any
load-bearing border window for `ui.position_from_window()` sizing. Reference a recently-migrated
screen (e.g. `scores_ui.cpp`, the minigames) for the exact scaffolding.

### P4 / P5 (after all category-A migrate)
P4: confirm overmap screen (`overmap_ui::draw_ascii`) — tile-overmap likely covers it.
P5: backend deletion — once no window but RmlUi remains, delete `sdl_curses_draw`,
`cursesport`, and `output.cpp` curses primitives.

### Build / verify (per session)
- Main: `cmake --build out/build/osx-arm-slim --target cataclysm-bn-tiles -j8` — pipe to RAW
  output (NOT `rtk err` — it masks no-op/relink failures). Confirm 0 `error:` AND check the
  binary mtime is fresh (`stat -f '%Sm' out/build/osx-arm-slim/src/cataclysm-bn-tiles`).
- **Header-touching commits MUST also build `cata_test-tiles`** — test fakes (`tests/fake_*.cpp`)
  mirror real signatures; a removed overload breaks them (see `4219a150cc`).
- **Before deleting any shared method, census BOTH `\.draw\(` and `->draw\(`** (pointer syntax) —
  see `ref_decurse_caller_census` memory; a missed pointer caller broke the build mid-sweep.
- LSP/clangd diagnostics LAG direct file writes — trust the build, not inline `No member`/`does
  not match` diagnostics at stale line numbers.

### Session commit log (2026-06-29, all green)
minigames `e12b47fdc3` `c5831194dc`; test fake `4219a150cc`; §C keystone `248071aa5a`;
deletions `ecc47c6cf6` (draw_minimap) `72fae4a792` (veh-dir) `756ca00706` (critter_highlighted)
`5b7b11ba51` (draw_critter chain) `3a7d879284` (footsteps) `86a2d236ad` (draw_ter m.draw+trail)
`69c49a2ee8` (map::draw) `df4710c43d` (look-around cursor) `4548df6553` (vestigial highlights);
plus docs commits.

### P5-A progress (CURRENT SESSION — commit TBD)

**State entering this session (HEAD `b56e9ee3df`):**
- P3 category-A migration COMPLETE: list_items / list_monsters / zones_manager / show_adm /
  live_view / character_preview all migrated. The curses on_redraw arms exist but are dead
  behind default-ON toggles.
- P4 done: overmap `draw_ascii` deleted (`05c4ccfeed`).
- look_around curses producers deleted (`b56e9ee3df`): `print_all_tile_info` + its 8
  `print_*_info` sub-producers gone (−387 lines). `Creature::print_info` callers: 3
  (game.cpp list_monsters arm, editmap, wish).

**P5-A batch — game.cpp dead arm deletion:**
- Deleted `zones_manager` on_redraw curses arm + orphaned static helpers
  `zones_manager_draw_borders` / `zones_manager_shortcuts` / local lambda `zones_manager_options`.
- Deleted `list_items` on_redraw curses arm + orphaned `reset_item_list_state`
  (`draw_item_filter_rules` kept — live caller in clzones.cpp:502).
- Deleted `list_monsters` on_redraw curses arm — **this removes `cCurMon->print_info(w_monster_info,
  ...)` (the last game.cpp caller of `Creature::print_info`)**.
- game.cpp curses calls: ~102 → 28. `Creature::print_info` callers: 2 (editmap + wish).
- All build-green. No eyeball required (dead-code deletion, no runtime path change).

**Remaining `Creature::print_info(window&)` callers (NOT deleted here — need their own pass):**
- `editmap.cpp:804` — live cursor pane (editmap is deferred / non-mechanical).
- `wish.cpp:654` — dev wish screen (dev tools batch, P5-G).

**`vehicle::print_part_list` remaining callers:** editmap.cpp:808 only (same editmap pass).

**Next after P5-A (priority order from the full-rip chain):**
1. **P5-B** — magic.cpp uilist-callback curses fallback: `spellcasting_callback::refresh()` +
   `teleporter_callback::refresh()` (the toggle-OFF curses fallback for the uilist draw_rml path;
   delete the bodies, keep the `draw_rml` override; these are the ~33 curses-text calls in magic.cpp).
2. **P5-C** — dialogue_win curses impl: `dialogue_window::print_header` / `print_history` /
   `display_responses` / `clear_window_texts` / `cache_msg` / `refresh_response_display` (the
   curses fallback for the migrated Tier-5 `dialogue` screen; callers are guarded by
   `dialogue_rmlui_enabled()`; delete the curses method bodies, keep `history_markup()` and the
   data members the RmlUi path uses).
3. **P5-D** — game.cpp death screen (`draw_rip_screen`, ~1505–1580): the post-death curses UI
   (kills/name/last-words); decide: migrate to RmlUi or remove (it is not player-interactive).
4. **P5-E** — remaining game.cpp isolated curses: NPC debugger (~4075), compass helper (~4614–4725);
   census callers per symbol.
5. **P5-F** — generics (uilist `show` curses fallback, string_input_popup, popup): high blast radius;
   gated on all the above.
6. **P5-G** — dev tools: wish, catalua_console.
7. **P5-H** — veh_interact / advanced_inv curses remnants.
8. **P5-I** — backend cull + toggle layer (LAST).

### P5-B progress (CURRENT SESSION)

**Deleted curses fallback bodies in magic.cpp + magic_teleporter_list.cpp:**
- `spellcasting_callback::refresh()` (magic.cpp ~1871–1889) — border + shortcut drawing removed.
  Base class `uilist_callback::refresh() {}` no-op now inherited.
- `spellcasting_callback::draw_spell_info()` (magic.cpp ~2150–2315) — orphaned curses info pane
  renderer; only caller was `refresh()`. The four static helpers
  (`enumerate_spell_data`, `enumerate_traits`, `casting_time_encumbered`,
  `energy_cost_encumbered`) were NOT deleted — they are also called from `draw_rml`.
- `draw_spell_info` private declaration removed from `spellcasting_callback` (line 1835).
- `teleporter_callback::refresh()` (magic_teleporter_list.cpp ~172–191) — overmap chunk draw
  + distance label removed. Base class no-op inherited. `draw_rml` kept intact.
- Both `cataclysm-bn-tiles` + `cata_test-tiles` link green (37s incremental).

**Next: P5-C** — dialogue_win curses impl: delete `dialogue_window::print_header` /
`print_history` / `display_responses` / `clear_window_texts` / `cache_msg` /
`refresh_response_display` bodies; keep `history_markup()` and data members the RmlUi path uses.

### P5-C progress (CURRENT SESSION)

**Emptied dialogue_window curses method bodies in dialogue_win.cpp:**
- `print_header(const std::string &)` → `{}`
- `clear_window_texts()` → `{}`
- `print_history()` → `{}`
- `cache_msg(const std::string &, size_t)` → `{}`
- `display_responses(const std::vector<talk_data> &, size_t)` → `{}`
- Deleted file-static helpers: `page_entry`, `page` structs; `split_to_pages`,
  `print_responses`, `print_keybindings` functions; anonymous namespace
  (`header_height`, `dialogue_divider_x`).
- `refresh_response_display()` and `handle_scrolling()` kept (called from live
  main loop in npctalk.cpp; scroll state is always at default in RmlUi path).
- `add_to_history()`, `history_markup()`, `resize_dialogue()` untouched.
- Build green `+28/-258`. No warnings.

**Next: P5-D** — game.cpp death screen (`draw_rip_screen`, ~1505–1580):
post-death curses UI (kills/name/last-words); decide migrate vs remove.

### P5-D progress (CURRENT SESSION)

**Migrated death/RIP screen to RmlUi (`game::cleanup_at_end`):**
- Created `data/gui/death_rip.rml` + `data/gui/death_rip.rcss`.
- Data model `"death_rip"` with 4 bindings:
  - `art_rml` — pre-coloured ASCII gravestone art (`&nbsp;` for spaces,
    `<br/>` between lines, `cata_text_to_rml(colorize(...))` for coloured chars).
  - `survived_rml` — "Survived: <duration>" (coloured).
  - `kills_rml` — "Kills: <N>" (coloured).
  - `name_rml` — "In memory of:<br/><player name>" (coloured).
- Toggle `death_rip_rmlui_enabled()` added (default ON); registered in
  `rml_toggle_registry.cpp`.
- `DirtyAllVariables()` called after `rml.open()` to prime the eager-populated
  model (matches the zones_manager/dialogue proven pattern).
- Curses fallback arm kept intact in `ui.on_redraw()`.
- Last-words `string_input_popup()` uses standalone path in RmlUi arm
  (no `.window()` → uses `string_input.rml` doc); curses arm passes `.window(w_rip, ...)`.
- Build green `+234/-63` across 5 files. Render correctness needs an in-game death
  to verify — cannot be exercised by the test suite.

**⚠ Working-tree WIP note:** `src/rml_screen.h` and `src/rml_toggle_registry.cpp`
have pre-existing reformatting as uncommitted diffs (same pattern as game.h / magic.cpp).
Committed versions are clean originals + our additions only.

**Next: P5-E** — remaining game.cpp isolated curses: NPC debugger (~4075),
compass helper (~4614–4725); census callers per symbol before deleting.

### P5-E progress (CURRENT SESSION)

**Census result:**
- `game::mon_info(const catacurses::window &, int hor_padding)` — ZERO callers. The
  sidebar compass was fully replaced by `hud_compass()` in `panels.cpp` (uses
  `visible_count_by_dir`; symbol-grid deferred to "phase 2"). Safe to delete.
- `game::disp_NPCs()` — live caller in `debug_menu.cpp:1619`. Dev tool; deferred to P5-G.

**Deleted:**
- `game::mon_info` definition (game.cpp, 154 lines) + 2-line declaration (game.h).
- `mon_info_update()` kept (live, called from `do_turn`).
- Build green `+0/-156`. Both targets link.

**⚠ Working-tree WIP note:** `src/game.h` has pre-existing reformatting as uncommitted
diff (~2377 lines churn). Committed version is clean HEAD − the 2-line declaration.

**Next: P5-F** — generics: uilist `show` curses fallback, string_input_popup, popup.
High blast radius; gated on all the above being complete.

### P5-F progress (CURRENT SESSION)

**Only one safe change landed; the other two are explicitly deferred.**

**Done — `string_input_popup::draw()` RmlUi gate:**
- Added `if( rml_session ) { return; }` at the top of `draw()` (string_input_popup.cpp).
- `rml_session` is the instance's own RmlUi state pointer: non-null only when
  `rml_open()` succeeded for THIS call.  When RmlUi is active, curses `draw()` is
  now a no-op; all three fall-through cases (toggle OFF / `!ready()` / open failure)
  leave `rml_session` null and curses renders normally.  `custom_window` and
  `draw_only=true` paths are unaffected (both leave `rml_session` null).
- Build green (trivial — no new TUs touched).

**Deferred — `uilist::show()` and `query_popup::show()`:**
The plan to empty these bodies was rejected after analysis:
- `uilist::rml_open()` returns `false` (→ `rml_session` null → `show()` called) in
  THREE live cases that a nested-uilist fix does NOT eliminate:
  1. `!uilist_rmlui_enabled()` — F4 toggle OFF (the A/B fallback kept until P5-I).
  2. `!rmlui_layer::ready()` — early-init uilists before the first `refresh_display`.
  3. `CreateDataModel` / `open_document` failure path.
- `query_popup::show()` has an even deeper blocker: **`static_popup` and
  `throbber_popup` never call `rml_open()` at all** — their ctors call only
  `create_or_get_adaptor()`, so `rml_session` is ALWAYS null and `on_redraw`
  ALWAYS hits `else { show(); }`.  This is the entire loading/wait-screen family:
  worldgen "Please wait…", save/load, saving, mapbuffer, autodrive.  Emptying
  `show()` would blank EVERY loading screen unconditionally, not just nested-popup
  edge cases.  The toggle-exposure analysis for interactive `query_popup` applies
  here too (toggle OFF / early-init / failure).
- The test suite cannot catch blank-screen regressions (`query()` bails in
  `test_mode`); silent breakage would only surface in manual play.
- **`uilist::show()`**: deleted in **P5-I** together with the toggle layer —
  the three fall-through cases no longer exist at that point.
- **`query_popup::show()`**: **CANNOT be deleted at P5-I** without FIRST giving
  `static_popup` and `throbber_popup` their own RmlUi render path.  That work
  is a prerequisite of P5-I, not a free side-effect of dropping the toggle.
  Record this as a P5-I pre-requisite now so a future session doesn't silently
  blank every loading screen.
**Next: P5-G** — dev tools: `wish.cpp`, `catalua_console`, `game::disp_NPCs`.

### P5-G progress (CURRENT SESSION)

**`wish.cpp` — three `refresh()` curses bodies deleted:**
- `wish_mutate_callback::refresh()` (−146 lines) — mutation detail side pane.
- `wish_monster_callback::refresh()` (−36 lines) — monster info pane +
  `monster::print_info(w_info, ...)` (**removes the last `Creature::print_info`
  caller outside dev-deferred editmap/wish**; now only editmap remains).
- `wish_item_callback::refresh()` (−53 lines) — item info side pane.
- Each had an existing `draw_rml()` counterpart that owns all state + rendering.
- Net: `−235` lines, 3 insertions (empty stubs). Build-clean.

**`game::disp_NPCs()` — converted from curses window to `uilist`:**
- Removed `catacurses::window`, `ui_adaptor`, `on_redraw`, `on_screen_resize`,
  and the full `mvwprintz`/`wnoutrefresh` body.
- Replaced with a `uilist` populated with player-pos title + NPC entries +
  monster entries. `uilist` is RmlUi-backed; no new curses calls.
- Net: `−38 +14` lines. Build-clean.

**`catalua_console` — DEFERRED to P5-H:**
- Census (grep tool): 3 catacurses::window members, `catacurses::newwin` ×3,
  `werase` ×3, `draw_border`, ~15 `mvwprintz`, `wnoutrefresh` ×3 — full
  curses on_redraw body with log/console/prompt layout.  **NO RmlUi/`draw_rml`
  path exists**.  This is a genuine migration (not a body-empty), requiring
  new RML/RCSS + data model for a multi-pane interactive console.
- **Cannot be treated as a no-op or simple deletion at P5-I** — it needs a
  real RmlUi screen before the backend is culled.
- Promoted to **P5-H** so it gets its own session.

**Next: P5-H** — `catalua_console` RmlUi migration (P5-H primary).
`veh_interact` / `advanced_inv` — **status uncertain, deferred:**
The `overview_headers[]` / per-entry `details` lambdas in `calc_overview` are
only ever ASSIGNED, never called (no `[key](w,y)` / `.details(pt,w,y)` /
map-iteration invocation found in grep census). The curses `on_redraw` body is
empty (just `if(rml){ return; }`). Parallel RmlUi text producers exist at
veh_interact.cpp:2592+. **Conclusion likely: lambdas are dead code, not live**
— the original curses renderers that would have called them appear already
deleted. But the `calc_overview()` data population feeds the live RmlUi sync,
so structure changes need care. **Before next session:** confirm with a
`.details(` / `overview_headers` call-site trace + check the one remaining
display_/draw_ method; if dead, the removal is mechanical (like P5-A).
Deferred to a post-P5-H pass.

### P5-H progress (CURRENT SESSION)

**`catalua_console` — MIGRATED to RmlUi:**
- New `data/gui/lua_console.{rml,rcss}` — full-screen panel with:
  - Scrollable log area (`data-for="entry : log"`) — entries rendered
    oldest-first (top-to-bottom), coloured via `cata_text_to_rml`.
  - Hints bar (`hints_rml`) — switches between normal / editing text.
  - Prompt preview (`data-for="line : prompt"`) — numbered lines from
    `build_numbered_prompt_lines`.
  - Footer (`footer_rml`).
- `lua_console_rmlui_enabled()` toggle (default ON) added to
  `catalua_console.cpp` (global scope, outside `namespace cata`).
- Toggle registered in `rml_screen.h` + `rml_toggle_registry.cpp`.
- `rml_doc rml` + `lua_console_rml_session` injected into
  `show_lua_console_impl()`:
  - `on_redraw` gated: `if( rml ) { return; }` skips curses draw.
  - `sync_rml_data()` lambda: rebuilds log entries + prompt lines +
    hints string from current state; called on every log invalidation
    and after each input action via `if(rml) sync_rml_data()` inline.
  - `rml.open(...)` called before main loop; RAII close on exit.
- Curses fallback (`ui.on_screen_resize` + `ui.on_redraw` curses body)
  kept intact — active when toggle OFF / `!rmlui_layer::ready()` /
  open failure. The `string_editor_window` (EDIT action) is still
  curses; it composites over the RmlUi console doc transparently.
- Build green (`+140/-0` in catalua_console.cpp; `+3/-0` rml_screen.h;
  `+1/-0` rml_toggle_registry.cpp; +2 new data files).
- **Eyeball owed** (dev tool; needs Lua-enabled build to verify log
  renders, scroll works, prompt preview updates, EDIT mode composes).

**`veh_interact` / `advanced_inv`** — see deferral note above.

**Next: P5-I** (backend cull prereqs):
1. Confirm veh_interact `overview_headers`/`details` lambda call-site trace
   (likely dead — remove if confirmed).
2. Give `static_popup` + `throbber_popup` an RmlUi render path (required
   before `query_popup::show()` can be deleted).
3. Give `string_editor_window` an RmlUi path — curses-only (`_win`,
   `mvwprintz print_editor`, own `on_redraw`; no `draw_rml`/toggle).
   Live callers include the Lua console EDIT action and likely others
   (census `string_editor_window` callers before migrating). Backend cull
   breaks it with no fallback.
4. Then: remove toggle layer (`*_rmlui_enabled()`), delete curses backend.
