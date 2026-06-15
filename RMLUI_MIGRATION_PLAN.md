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

### Tier 2 status (2026-06-11): COMPLETE except faction (deferred)

8 of 9 Tier-2 screens DONE: mutations / bionics / safemode / auto_pickup /
computer terminal / construction / crafting (eyeballed) / armor_layers. **faction
DEFERRED** — its detail panes ride the Tier-3-era creature/npc-info F.2 component
AND it needs an interface refactor of `npc::faction_display` (split draw from
state); do it after that component lands, not before.

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

**Discipline:** the cell/preset model is load-bearing and this file is 2640 lines
under the stale-read hook — every model field MUST be verified against fetched
source (the armor near-miss is the warning). Per-subclass gate means each slice
is independently bisectable + revertible.

## Tier 4 — big bespoke menus (worldfactory / main_menu / options / newcharacter)

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
