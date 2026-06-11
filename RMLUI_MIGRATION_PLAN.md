# Full UI Migration to RmlUi — Master Plan

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
- **Tier 1 screen: loading_ui — NO BESPOKE MIGRATION NEEDED (covered by Tier 0).**
  Investigated: `loading_ui::menu` IS a `uilist` (loading_ui.cpp:476); its sole
  on_redraw calls `menu->show(ui)` (loading_ui.cpp:508) → renders via RmlUi
  automatically whenever the Tier-0 uilist toggle is ON. It rides the uilist
  toggle, needs no own screen/toggle. The other half of the file
  (`loading_image_splash`) is an SDL splash IMAGE + author-text overlay drawn via
  `background_pane`/RenderCopy — not curses text UI, out of scope like the map
  tile path. So loading is done-by-coverage; nothing diary-style to do. (Confirms
  the F.0 census flag "loading_ui may be a no-loop progress display.")
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

- **Tier 2 screen #7: crafting (select_crafting_recipe) — CODE-COMPLETE +
  BUILD-GREEN (crafting_gui.cpp + devui compile + LINK clean), TOGGLE OFF, EYEBALL
  OWED, UNCOMMITTED.** 9th `rml_doc` consumer; PEER of construction (the cadence
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
