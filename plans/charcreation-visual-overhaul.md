# Character creation — visual overhaul

Status: in progress. Palette decision: **keep CBN's warm gold/amber**; borrow only
structure/restraint from `UI_designs/`, not the references' teal (`#002020`).

## Why

Measured against the three `UI_designs/*charcreation*` references with a
background-independent glyph-edge metric:

| screen | ink density | vertical occupancy |
|---|---|---|
| REF `10_charcreation_genotype` | 1.36% | 34% |
| REF `12_charcreation_calling` | 2.55% | 31% |
| REF `13_charcreation_start_location` | 2.08% | 29% |
| ours — TRAITS | **8.72%** | **93%** |
| ours — OVERVIEW | **6.10%** | 58% |
| ours — SCENARIO | **5.71%** | 83% |
| ours — POINTS | 2.67% | 63% |

2.5–4x the reference ink density. Line *leading* is not the cause (1.41x here vs
1.44–1.52x in the references). The causes are:

1. **No typographic hierarchy** — every line is the same 18dp weight, so `Stats:`
   competes with `Strength: 9`. The references get hierarchy from size and weight.
2. **Colour overloaded** — gold means both "section header" and "skill category";
   red means both `None!` and "random location". Yet on TRAITS colour is
   *load-bearing semantics* (taken / forbidden / available). One channel, two jobs.
3. **No grouping geometry** — OVERVIEW's six columns are separated by x-position
   alone.
4. **No character to look at** — the whole point of a creator is seeing your
   character; the preview was dropped in the RmlUi migration (below).

Airiness itself is NOT the defect — the references sit at 29–34% occupancy. The
empty space should be spent on hierarchy, the preview and the balance scale.

## Workstreams

### 1. Restore the character preview  (functional regression)

`newcharacter_ui.cpp:962` — *"The tile character_preview overlay is NOT drawn in rml
mode this slice (out of scope like the AIM minimap; flagged deferred)."*

The widget is **already fully RmlUi-aware**: `character_preview_window::display()`
opens `character_preview.rml` via `cp_rml_open()`, positions it with
`cp_rml_position()`, and draws the avatar as a GPU sprite. Its zoom / clothes-toggle
actions are still registered and still handled. The only thing missing is the call:
every tab's `ui.on_redraw` does `if( rml ) { sync_rml(); return; }` and returns
before the curses block that used to call `display()`.

Four tabs construct a preview: TRAITS (`:906`), BIONICS (`:1387`),
PROFESSION (`:1842`), OVERVIEW (`:3017`).

- Call `character_preview.display()` inside the rml branch when
  `use_character_preview`.
- Reserve layout space in each of those four documents so the sprite does not land
  on top of text. This doubles as density relief on TRAITS (93% occupancy).
- `clear()` on tab exit already happens via the existing `PREV_TAB`/`NEXT_TAB`
  handlers.

### 2. Trait / bionic balance scale  (new)

TRAITS and BIONICS currently express the good/bad budget as
`budget_rml` = `"%2d/%-2d  %3d/-%-2d"` → renders as `7/12 -1/-12`. Unreadable at a
glance.

Replace with a **tipping scale** that reads instantly, keeping the numbers beside
it:

- Pure RCSS geometry + `transform: rotate()`, driven from C++ through
  `data-style-transform` (same mechanism as the main menu's `data-style-top` bob),
  so it animates through the existing `transition` support and needs no new texture
  generator.
- Beam rotates about its centre; the pans are children of the beam so they follow.
- Angle = `clamp( (good - bad) / max_trait_points, -1, +1 ) * 12deg`. The heavier
  side sinks (positive good → good pan down).
- Freeform point pool → hide the scale (there is no budget to balance).

### 3. Iconography  (placeholders now, art later)

Use the existing procedural texture system — `?proc:runic-icon:<size>:<seed>:<hex>`,
resolved in `rmlui_render_interface.cpp:881` → `gen_runic_frame()`, already proven
for the main-menu nav glyphs. Each concept gets a **stable seed**, so every
placeholder is deterministic and recognisable, and swapping in real art later is a
one-line decorator change per row of the table below.

## Art asset intent  (for hand-off)

Every entry below is currently a `?proc:runic-icon` placeholder. Replace the
`decorator` with `image( <path> ... )` when art arrives. Seeds are arbitrary but
must stay stable so placeholders don't reshuffle between builds.

| # | Concept | Seed | Size | Where | Intent for the artist |
|---|---|---|---|---|---|
| 1 | POINTS tab | `0x5031` | 24dp | tab strip | Coin / token stack — a budget to spend |
| 2 | SCENARIO tab | `0x5343` | 24dp | tab strip | Torn map fragment — where the story starts |
| 3 | PROFESSION tab | `0x5052` | 24dp | tab strip | Crossed tools — what you did before |
| 4 | STATS tab | `0x5354` | 24dp | tab strip | Flexed arm / body outline — raw capability |
| 5 | TRAITS tab | `0x5452` | 24dp | tab strip | Double helix — innate qualities |
| 6 | BIONICS tab | `0x4249` | 24dp | tab strip | Circuit-etched limb — installed hardware |
| 7 | SKILLS tab | `0x534b` | 24dp | tab strip | Open book with a bookmark — learned ability |
| 8 | OVERVIEW tab | `0x4f56` | 24dp | tab strip | Clipboard / dossier — the final summary |
| 9 | Good-trait pan | `0x4744` | 20dp | balance scale | Upward chevron in a dish; reads positive |
| 10 | Bad-trait pan | `0x4244` | 20dp | balance scale | Downward chevron in a dish; reads negative |
| 11 | Scale fulcrum | `0x464c` | 16dp | balance scale | Narrow triangular pivot |
| 12 | Stats section | `0x0101` | 20dp | OVERVIEW header | Matches #4, small variant |
| 13 | Skills section | `0x0102` | 20dp | OVERVIEW header | Matches #7, small variant |
| 14 | Traits section | `0x0103` | 20dp | OVERVIEW header | Matches #5, small variant |
| 15 | Bionics section | `0x0104` | 20dp | OVERVIEW header | Matches #6, small variant |
| 16 | Vehicle section | `0x0105` | 20dp | OVERVIEW header | Wheel / chassis |
| 17 | Items section | `0x0106` | 20dp | OVERVIEW header | Bindle / pack |

Palette for all icons: ink `#c4a832` when active/selected, `#a1885f` when inactive
— the same two golds the main-menu nav rail uses.

## Already landed (this pass)

- **Cursor row was unreadable.** Every creator row arrives via `data-rml`, so its
  colour is baked into an inline `<span>` — which beats a class rule, *including
  with `!important`* (measured: an RCSS `color` on the cursor row changed zero
  pixels, while `background-color` applied immediately). theme.rcss's
  `.item.selected { background-color: {{yellow}}; color: {{bg-hard}} }` therefore
  never recoloured the text, leaving a semantic mid-tone colour on gold. The row
  rendered as a **blank gold bar** on POINTS, SCENARIO and TRAITS — the text could
  not be read in any capture. Fixed in `data/gui/newchar_common.rcss` with a dark
  fill + gold accent bar, keeping the semantic colour intact (it is the only
  channel encoding taken/forbidden/available on TRAITS, and affordability on
  PROFESSION/SCENARIO). The cursor row is now legible on POINTS, SCENARIO, TRAITS
  and BIONICS.
  - POINTS' "chosen pool" row additionally moved `c_green` → `c_light_green`; both
    contrast metrics below agree that is an improvement.
  - **Do not quote a contrast ratio for this.** Two defensible definitions
    disagree in DIRECTION: by mean-ink the gold state was 1.65:1 and the fix
    2.86:1, but by modal-ink (glyph interior) the gold state was 2.49:1 and the fix
    2.18:1. At 18dp through the CRT/bloom post-process these statistics are not
    WCAG-comparable. The defensible claim is qualitative: unreadable → readable,
    verified visually per tab.
  - `border-left` *shorthand* silently did not paint; the `border-left-width` /
    `border-left-color` longhand does.
- **OVERVIEW label welding.** `colorize( "Name:" ) + " " + colorize( value )` puts
  U+0020 between two spans, where RmlUi trims it at parse time — rendering
  `Name:Lon 'Electric' Woody`, `Height:188 cm`, `Age:43`, `Gender:MaleFemale`.
  Fixed with an `nc_label()` helper that keeps the space *inside* the colour run,
  matching `Scenario: ` / `Profession: ` which never had the bug, plus a `Male /
  Female` separator in its own colour run. No PO churn.
- **OVERVIEW help clipped.** `.nc-desc-guide { max-height: 110dp }` fitted 5 of 7
  lines, clipping the last mid-glyph. Raised to 200dp.
- **Character preview restored on all four tabs that own one.** Two bugs: the RmlUi
  redraw path never called `display()`, and `character_preview.rcss` never sized
  `body`, so the frame shrink-wrapped to (0,0) (the "missions gotcha" already
  documented at `newcharpoints.rcss:2`). PROFESSION and OVERVIEW additionally needed
  the panel narrowed — see below.
- **Panel narrowing.** `newchardescription.rcss` was **92%** wide, not the 80% every
  other creator sheet uses. A centred panel of width `W` has its right edge at
  `(100+W)/2`, so 80% clears the preview's 90.2% left edge by 0.2% while 92%
  overlapped it outright — PROFESSION passed a different `preview_ncols` and
  overlapped too. The avatar is a GPU sprite drawn *underneath* the RmlUi document
  and a panel background is a single rect, so no child can unpaint it: the fix must
  keep the preview rect clear of opaque geometry rather than reserve a slot inside
  it. All eight panels are now 72% with one shared `nc_prepare_preview()` geometry
  helper, replacing four hand-tuned per-tab computations.
- **Labelled points budget.** `0+0+0=0` replaced with
  `stats N traits N skills N total N` via `nc_points_line()`, routed through all
  eight tabs plus the scenario/profession cost lines.
- **Tipping balance scale** replaces TRAITS/BIONICS' `7/12 0/-12`. Beam rotates,
  pans stay upright.
  - RmlUi resolved `position: absolute` against `.nc-panel`, **not** against the
    `position: relative` parent — the beam landed ~39dp high, across the top bar.
    Rebuilt in pure flow (arm row + fulcrum beneath); no absolute positioning.
  - `rotate()` is clockwise and the good pan is on the **left**, so the angle must be
    **negative** to sink the same side `good_top` sinks. Getting this wrong moves beam
    and pans equal magnitudes in opposite directions.
  - Beam half-length 140dp at 8deg rises 19.5dp, so the pans are nudged 20dp; the two
    numbers must stay in step or the beam visibly detaches from its own pans.
  - Empty `Rml::String` defaults for `data-style-transform` / `data-style-top` emitted
    `Syntax error parsing inline property declaration 'top: ;'` on the first frame,
    before `sync_rml()` populates the model. Valid neutral defaults
    (`"rotate(0deg)"`, `"0dp"`) fix it.
- **SCENARIO empty sections.** `Scenario Vehicle:` / `Scenario Flags:` printed a
  header then nothing; they now name the empty state (`None`).
- **Type hierarchy.** Six OVERVIEW panes were one `data-rml` blob each, so RCSS had
  no element boundary to style. Split into header + label/value rows
  (`nc_desc_pane` / `nc_desc_row`), giving bold letter-spaced headers, hairline
  rules, real gutters, dimmed labels against full-strength values, and a
  de-emphasised help block.
- **Tab strip icons** on all eight tabs, and per-pane icons on OVERVIEW, as
  `?proc:runic-icon` placeholders (see "Art asset intent").

## Verification harness

Synthetic input does not reach this SDL build on macOS (`vv.py` is Windows-only;
Accessibility trust absent). Tabs were reached with a temporary scaffold —
`CBN_MENU_SEL1` in `main_menu.cpp` and `CBN_NEWCHAR_TAB` in `newcharacter.cpp`,
paired with `character_type::NOW` so every tab has randomised real content.

**Both scaffolds have been removed.** `grep CBN_NEWCHAR_TAB|CBN_MENU_SEL1` over
`src` and `data` returns nothing; `main_menu.cpp` and `newcharacter.cpp` are
byte-identical to HEAD (`git diff --stat HEAD` empty for both). The default boot
path was then re-verified: `opened document data/gui/mainmenu.rml (1 open)`, with
no creator document auto-opening, and the menu renders correctly.

### Verified this pass

All eight tabs rendered and inspected: POINTS, SCENARIO, PROFESSION, STATS,
TRAITS, BIONICS, SKILLS, OVERVIEW. Semantic colours preserved; preview visible on
all four tabs that own one; balance scale tips in the correct direction on TRAITS
(`8/12` low vs `-4/-12` high) and BIONICS (`4/12` low vs `0/-12` high).

Cursor-row legibility, stated precisely rather than as "all eight":

- **Measured** (pixel sampling of the cursor row vs normal rows): POINTS, TRAITS.
- **Seen highlighted in a capture**: SCENARIO (`Evacuee`), BIONICS
  (`Anti-Glare Compensators`).
- **Judged by eye, not measured**: STATS, SKILLS, OVERVIEW.
- **Not observable**: PROFESSION — the list sat at the top while `Tourist` was the
  selection, so the cursor row was off-screen in every capture. Its legibility is
  *inferred* from the shared `newchar_common.rcss` rule, not verified.

Build is clean (exit 0, no errors). The one remaining project warning
(`newcharacter_ui.cpp` `selected` set-but-not-used) and all 42 `-Wunused-macros`
warnings are **pre-existing**, proven rather than assumed: `newcharacter.cpp` has
zero net diff vs HEAD yet emits an identical 21-macro list, and every macro's use
count in `newcharacter_ui.cpp` is unchanged from HEAD (`COL_HEADER` gained one).
Left untouched per the no-churn rule. Four dead `int_page_width` declarations that
*were* mine — orphaned when the per-tab preview computations were replaced — were
removed.

### Still open

- PROFESSION's affordability colours were never exercised: every profession in the
  captured states cost 0 or 1 point, so the red/green affordability channel is
  inferred from TRAITS, not verified on that tab.
- PROFESSION's list did not scroll to its selected row (`Tourist` was the selection
  while the list sat at the top). Pre-existing scroll-follow behaviour, not
  introduced here.
- **The balance scale was only ever observed near one-third tilt** (`bal ≈ +0.33` on
  both TRAITS and BIONICS). Untested: `bal = 0` (level), `bal < 0` (disadvantages
  outweighing — pans swap, symmetric in code but unobserved), and `bal = ±1` (full
  tilt). At full tilt a pan is nudged 20dp inside a row whose padding is `2dp 0 8dp`,
  so a pan overflowing into the neighbouring row is plausible and has NOT been ruled
  out. Freeform hides the scale (`show = !freeform`) and was also not rendered.
- **One window size only.** Every capture was 3600x2260 physical. The occlusion fix
  rests on a geometric argument (a centred 72% panel's right edge at 86% clears the
  preview's 90.2% left edge), but the preview box is positioned from tile pixels via
  `calc_character_pos()`, not percentages, and `hide_below_ncols` drops it entirely on
  narrow terminals. Intermediate sizes are unverified — this is the single-point
  validation of the argument the whole fix depends on.
- **`nc_points_line()` only rewrites MULTI_POOL**; ONE_POOL, TRANSFER and FREEFORM
  fall through to `points_left::to_string()` unchanged (verified by reading, not by
  rendering those modes).
- Mouse navigation remains unverified anywhere.
- The 17 icon placeholders still need real art (see "Art asset intent").

## Tests

`cata_test-tiles` builds clean (exit 0) and `[traits]` passes (7 assertions, 1 test
case). Only `tests/new_character_test.cpp` touches this area, and it exercises
`newcharacter::add_traits` / `has_conflicting_trait` /
`add_default_mutation_type_traits` — none of which changed here. No test references
`points_left`, `nc_points_line`, `character_preview`, or the RmlUi layer, so there is
no existing contract covering this change; the verification is the in-game render.

**The test binary nevertheless exits 1 on this branch, and it is not this change.**
`report_unvisited()` logs an ERROR for terrain JSON that declares fields no loader
reads — `"acoustics"` in `terrain-{walls,windows,floors-indoor}.json` and `"decals"`
in `terrain-flora.json` — and the runner turns any error logged during
initialization into a failure even when every assertion passes. Proven pre-existing:
these two commits touch zero `data/json` files (`git diff --name-only
1d545a5355..HEAD`), and both fields are already present at `1d545a5355` ("terrain
decals"). Whoever owns that work should either register the fields or drop them; until
then the suite's exit code is not a usable signal on this branch.
