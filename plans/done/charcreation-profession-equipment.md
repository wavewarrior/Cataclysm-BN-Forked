# PROFESSION step — starting-equipment sheet

Adds a Caves-of-Qud-style equipment display to the PROFESSION step of the character creator.
Reference: `UI_designs/04_character_equipment.png` (Qud's INVENTORY & EQUIPMENT screen).

Builds on `plans/charcreation-profession-tree.md`; the click-intent rule, the flex-mirroring
rule and the "scope every patch to one loop" rule all carry over unchanged.

## What is wrong today

The starting kit — the single most concrete thing a profession hands you — is a flat
name-per-line dump inside a scrolling prose buffer:

```
Items:
Wielded:
None
Worn:
pair of jeans
long-sleeved shirt
...
Inventory:
...
```

No weight, no volume, no value, no totals, and no sense of *where on the body* any of it
goes. Beside it, the facts column says `GEAR / 12 items` and nothing more. A player choosing
between two professions cannot answer "which one leaves my hands free", "which one starts me
over my carry weight", or "does this one give me boots".

## What Qud does that is worth copying

| Qud element | Why it earns its place |
|---|---|
| Paper doll of body slots, each a bordered box with the item's sprite and a slot caption, joined by lines | answers "where does it go" spatially; empty slots are as informative as filled ones |
| Collapsible category tree, `[-]WEAPONS ─── [67 lbs.]` | rollups per group; a long kit stays scannable |
| Per-row stat + weight columns | lets rows be compared, not just read |
| `421/430 lbs.` capacity readout | turns weight from trivia into a constraint |

All four are meaningful for a *profession preview*, so all four are implemented.

## Where it lives: a sheet, not another column

The info panel is a fixed 188dp, three columns (`art | facts | prose`) — deliberately fixed so
a wordy profession cannot shove the tree around. A paper doll plus a two-level tree does not
fit in a third of 188dp, and growing the panel would undo that guarantee.

So the sheet takes the whole stage: while open it **replaces `.nc-tree`** inside
`.nc-prof-body`, which is already `flex: 1`. No new tab (equipment is a property of the
profession, not a step of the wizard), no floating window, no new document.

Opened by `VIEW_EQUIPMENT` (`g`, verified free across the global category and both creator
categories) or by an `EQUIPMENT [g]` control in the **status row**, beside the sort hint.
Closed by the same key, or `QUIT` — which is intercepted before the "Return to main menu?"
confirm while the sheet is open.

The control sits in the status row because a screen-level toggle belongs beside the other
screen-level status text, next to the sort hint — the same reasoning that already put that hint
there rather than in the per-profession panel. It started on the GEAR fact and did not respond to
clicks, but the cause was not the panel: see "The real bug behind the dead control" below.

## Layout

```
Starting equipment · Scoundrel              14 items   3.1 kg   8.16 L   $878.52
┌ doll ───────────────────┐ ┌ tree ──────────────────────────────────────────┐
│        ┌──────┐         │ │ - WORN ─────────────────────────────── 2.1 kg  │
│        │  ▓▓  │  HEAD   │ │   - CLOTHING ───────────────────────── 2.1 kg  │
│        └───┬──┘         │ │       cargo pants   ARM 2/2 2.25 L $25   0.7 kg│
│  ┌────┐ ───┼─── ┌────┐  │ │       hoodie        ARM 3/3 3.00 L $30   0.4 kg│
│  │EYES│    │    │MOUTH│ │ │ - CARRIED ──────────────────────────── 0.9 kg  │
│  └────┘ ┌──┴───┐└────┘  │ │   - WORKSHOP TOOLS ─────────────────── 0.1 kg  │
│  ┌────┐ │ ▓▓▓▓ │ ┌────┐ │ │       switchblade   DMG 16 0.05 L $20   0.1 kg │
│  │armL│─│ TORSO│─│armR│ │ │   + ELECTRONICS ────────────────────── 0.2 kg  │
└─────────────────────────┘ └────────────────────────────────────────────────┘
3.1 / 49.0 kg  ███░░░░░░░░░░░░░░░░░                          CLOSE [g]
```

### Paper doll

Six rows × three columns, every cell present (blank cells are spacers), so the grid needs no
pixel arithmetic — the same reason the info-panel notch row mirrors the carousel's flex
geometry rather than measuring it.

| row | left | centre | right |
|---|---|---|---|
| 0 | — | HEAD | — |
| 1 | EYES | junction | MOUTH |
| 2 | ARM L | TORSO | ARM R |
| 3 | HAND L | WIELDED | HAND R |
| 4 | LEG L | junction | LEG R |
| 5 | FOOT L | junction | FOOT R |

A *junction* cell draws Qud's connecting lines: a 1dp vertical spine down the cell centre and
a 1dp horizontal rung across its full width, giving a `+` that joins the box above to the box
below and the left cell to the right one. Both are centred by flex, not offsets, so they stay
aligned at any stage width.

`WIELDED` sits on the spine between the hands because CBN wields with both hands — there is no
left/right weapon slot to mirror Qud's. `CARRIED` is deliberately **not** a doll cell: it has
no anatomy, and the tree already itemises it.

Slot contents: of the items covering that body part, the one with the greatest
`get_coverage( bp )` (ties broken by weight) is shown as sprite + name; the rest become a
`+N` badge. Empty slots keep their box and caption, dimmed — "no gloves" is information.

Sprites come from the tileset through the existing `?sprite:<x>:<y>:<w>:<h>:<path>` RmlUi
source, resolved via `cata_tiles::find_tile_looks_like( id, C_ITEM )` so an item with no
sprite of its own follows its `looks_like` chain exactly as the world render does.

### Tree

Two levels, both collapsible:

1. **WIELDED / WORN / CARRIED** — the same classification the profession itself applies
   (`json_flag_auto_wield`, `json_flag_no_auto_equip`, `is_armor()`), so the grouping is the
   real equip outcome rather than a taxonomy invented for the display.
2. **`item::get_category()`** — ordered by `sort_rank()`, then name.

Identical items stack into one row with `xN` and summed weight/volume/value: professions
routinely hand out four of a thing, and four identical lines are four times the noise for no
extra information. The stack key is `tname( 1, false )`, not `display_name()` — with
`ITEM_HEALTH_BAR` on, `display_name()` prefixes every armor item with a durability bar
(`|| hoodie`), which is pure noise on a kit that is pristine by construction.
`with_prefix = false` gates only that prefix and the burnt text
(`item_display.cpp:343,372`), so charges and container suffixes survive and
`lighter (25/25)` still reads as such.

Rows are a **flat** vector carrying `group`/`sub` flags rather than a nested `data-for`.
Collapsing a level then means filtering one list, and — because every row is the same height —
scrolling the keyboard cursor into view is exact arithmetic instead of DOM child indexing.

Collapse state is keyed by `mode` / `mode|category_id` **strings**, not indices: switching
profession changes which categories exist, and index-keyed state would silently reassign.

### Per-row stat column

One conditional column, chosen so it is never blank for an item where it would matter:

| item | shown |
|---|---|
| melee-capable (best `damage_melee` ≥ 4) or gun | `DMG n` |
| armor | `ARM bash/cut` |
| `count_by_charges()` | charge count, via the name's own `(25/25)` suffix |

### Capacity readout

`weight total / u.weight_capacity()` plus a proportional bar, red past 100%. `weight_capacity()`
is STR-derived and the creator avatar's STR is live, so the readout tracks the STATS step — a
kit that fits a strong character and not a weak one says so.

## Totals are always visible

The GEAR fact keeps summarising without the sheet open — `14 items`, and a sub-line with total
weight and cash — so two kits can be compared without opening anything. The prose buffer's flat
`Items:` block is **removed**: it is now a strictly worse copy of the sheet, and two disagreeing
item lists on one screen is the failure mode the profession tree already fixed for skills and
traits.

## Incidental improvements

- `profession::items()` was built **twice** per redraw (prose buffer + gear count). Now built
  once per sync and shared by the fact, the doll and the tree, so adding the sheet costs no
  extra item construction.
- `nc_scen_art_dec` and the new item lookup share one `nc_tile_sprite_dec( id, category )`
  helper. As a side effect the scenario's start-location art now follows `looks_like`
  fallbacks, where before a missing sprite rendered nothing.

## The real bug behind the "dead control"

The toggle refused to respond to clicks for three attempts — on the GEAR fact, then in the
status row, with and without a `data-for` scope argument — while its neighbours (`.nc-tree-all`,
cards, band headers, the sheet's own rows) all worked. The cause was neither the element nor the
callback:

`main_menu::opening_screen` opens `gui/mainmenu.rml` and calls `new_character_tab()` from
**inside its own input loop** (`main_menu.cpp:1046`), with no close in between — unlike the
loadchar path, which does close its document. So the menu document stayed live underneath every
character-creation screen and kept hit-testing: a creator element that happened to sit over a
menu element never received its click, while its neighbours worked. The same defect painted the
menu's typography through the creator's panels as ghost text, visible on all eight tabs.

Fixed in the product with `rmlui_layer::scoped_documents_hidden` at the top of
`new_character_tab()` — the mechanism that already exists for exactly this ("a curses-rendered
modal opened while a document is live is invisible"). Fixing it only in the verification
scaffold would have made the control work in testing and stay dead in shipped builds.

## RmlUi traps this cost a cycle each

All of these presented as "the C++ is obviously right, so why is the screen wrong".

1. **A dead-looking control is usually another document, not your element.** See above — and
   note that hover state cannot diagnose it, because synthetic mouse-move does not drive
   `:hover` in this build either. `opened document … (N open)` in the log with the SDL debug
   class on is the signal worth reading first.
2. **`overflow: hidden` on a flex item resolves its intrinsic width to zero.** Adding it to
   `.nc-eqp-name` made every label in the tree vanish while the numeric columns kept
   rendering. Isolated: removing *only* that property, with `flex` and `white-space` held
   constant, brought the labels back. Clip on the ROW instead; `white-space: nowrap` on the item
   is enough to stop the wrap.
   *Not isolated:* `flex: 0 0 auto` is what shipped, but `0 1 auto` was only ever tried while
   `overflow: hidden` was also present, so "the `flex: 1` leader shrinks a `0 1 auto` name to
   nothing" is untested.
3. **RmlUi elements default to `display: inline`.** Every bare `div` in this sheet is
   blockified by a flex parent except `.nc-eqp-bar-fill`, whose parent is a plain block — so
   its width and height were ignored and the capacity bar drew nothing.
   *Not isolated:* the fix changed four things at once — `display: block`, a fixed-width track
   instead of `flex: 1`, a `dp` length instead of a percentage, and a `width: 0` base for the
   transition. `display: inline` alone accounts for a fill that draws nothing, at any unit, so
   **the percentage may be innocent**; treat "percentages against a `flex: 1` track resolve to
   nothing" as untested. Settle it by restoring the percentage with `display: block` in place.
4. **A lone space between two colour runs is trimmed at parse time.** `cata_text_to_rml` emits
   one `<span>` per run, so `"%s  %s  %s  %s"` of four colorized parts rendered as
   `16 items9.1 kg9.32 L$2795.26`. Separation has to be the stylesheet's job: four bindings,
   four elements, margins between them.

## Verified in the installed macOS build

Driven with the `computer` tool against `osx-arm-slim`, using a temporary `CBN_NEWCHAR_TAB=2`
scaffold in `main_menu.cpp` + `newcharacter.cpp` plus a `RELOAD_RCSS` key — **all removed**, and
the binary rebuilt from the scaffold-free source afterwards. Two environment facts worth keeping:

- `FULLSCREEN=maximized` is mandatory. Under the default `windowedbl` the window takes its own
  macOS Space and `desktop.windows()` returns nothing while the game runs happily.
- **Input must use `delivery: "foreground"`.** Background delivery is silently dropped by this
  SDL build, which looks exactly like a dead control — it cost one wrong diagnosis here.
- `reload_theme()` (bound to a temporary key) re-parses RCSS in place, turning a 40-minute
  restart into a keypress — verified repeatedly: stylesheet edits took effect mid-session.
  Whether it also picks up `.rml` is **untested**. One probe suggested not (a markup edit was
  still absent after leaving and re-entering the step), but that probe is not sound: its only
  observable was a label whose text also arrives through a per-frame `data-rml` binding, and
  re-entry into `set_profession` was inferred from band-collapse state rather than measured.
  RmlUi's `Context::LoadDocument` has no document cache, which contradicts the observation — so
  one of the two is wrong and this work did not determine which. To settle it: change something
  no binding can overwrite (a literal-text element with no `data-rml`, or a class name the
  stylesheet keys on) and confirm re-entry from `opened document` in the log rather than by
  inference. Until then, budget a relaunch for markup changes but do not assume one is required.

- Sheet opens and closes with `g` AND with a click on the status-row `EQUIPMENT [G]` control;
  `QUIT` closes the sheet and returns to the card tree without prompting for the main menu.
- Doll fills from the kit — hard hat/HEAD, safety glasses/EYES, t-shirt/ARM+TORSO, work
  gloves/HANDS, jeans/LEGS, socks/FEET, `empty hands`/WIELDED — with `+N` where several items
  cover a part, and dimmed boxes with captions for bare slots. Sprites resolve through
  `?sprite:` and change with the profession. Connectors align at both window sizes tried.
- Tree renders two collapsible levels with rollups: `WORN 2.5 kg > CLOTHING 2.5 kg > 8 items`,
  `CARRIED 0.8 kg > WORKSHOP TOOLS / COOKING TOOLS / ELECTRONICS / FOOD`. Stat column shows
  `ARM 2/2` on clothing and `DMG 7` on a pocket knife — armor takes precedence, so a hard hat
  no longer reads `DMG 6`.
- `UP`/`DOWN` move the cursor; `CONFIRM` on an item row correctly does nothing; a click on a
  group header collapses it and keeps its rollup; `e` collapses all groups and expands them
  again.
- Header totals read `12 items  3.3 kg  6.89 L  $724.98`; footer reads `3.3 / 45.0 kg` with a
  proportional yellow fill.
- Menu ghost text is gone from the creator, and the RmlUi warning count for a full session fell
  from 97,021 to 6 — all 6 pre-existing (`mainmenu` bob bindings, `loading.rcss` escapes).
  Zero `ERROR` lines.
- `[newchar]` and `[item]` pass (140,769 assertions, 53 cases) on the scaffold-free binary.
  `build-scripts/lint-json.sh` passes. No new compiler warnings: every one remaining in the
  three touched files maps to a construct present verbatim at `HEAD` (unused `COL_*`/`#define`
  macros, `selected` set-but-unused, `prev_submenu_*` unused).
- `[json]` contains `translation_text_style_check*`, which is `[.]`-tagged (excluded by default)
  and fails in this preset. Attributed as pre-existing by reachability, not by rebuild: it
  exercises `test_mode && check_style` in the JSON/translation reader, and this diff touches no
  JSON parsing, translation, or debugmsg code.

## Not done

- The doll shows one item per slot plus a count. Layered clothing is not drawn as layers; Qud
  does not either.
- `SORT` / `FILTER` / `RANDOMIZE` interaction with the sheet open is untested; the sheet
  swallows every key it handles and falls through on the rest, so those reach their existing
  handlers only once it is closed. `CHANGE_GENDER` IS handled with the sheet open, because
  gender changes the kit.

## Acceptance

- Doll: 18 cells, connectors aligned, sprite + name + `+N` for filled slots, dimmed for empty.
  Only items the equip pass actually WEARS reach a slot — a packed spare (`no_auto_equip`) does
  not claim a body part.
- Tree: two collapsible levels with weight rollups, `xN` stacking, per-row stat/volume/value/
  weight columns, and no durability-bar noise in the names.
- Header totals and a capacity bar that reddens past `weight_capacity()`.
- `g` and the status-row `EQUIPMENT` control both toggle the sheet; `QUIT` closes it without
  prompting.
- `profession::items()` called once per redraw, not twice.
- No new compiler warnings; `[newchar]` and `[item]` pass; scaffold removed and the committed
  source rebuilt clean.
