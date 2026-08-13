# OVERVIEW step — an editable identity column, a record digest, and the seal

Seventh and last creator step reworked, after SCENARIO, PROFESSION, STATS, TRAITS, BIONICS and
SKILLS. Read `plans/charcreation-traits-tree.md` for the row anatomy and the click-intent rule,
`plans/charcreation-bionics-chassis.md` for the shared-vocabulary promotion rule and the
`SELECT`/`COORDINATE`/`ANY_INPUT` input trap, and `plans/charcreation-skills-matrix.md` for the
dot-lattice animation technique this borrows. None of that is re-derived here.

## What was there before

The OVERVIEW tab was the only step still wearing its pre-rework clothes:

- an edit row of five `data-rml` spans (name / gender / height / age / location) where the
  selected field was marked with a literal `"> "` inside the string;
- **three of the five fields were unreachable by the cursor.** `LEFT`/`RIGHT` cycled NAME →
  HEIGHT → AGE only; gender needed `CHANGE_GENDER` and location needed `CHOOSE_LOCATION`, and
  neither was in any list. Nothing on this screen was clickable except the two navigators;
- six wrapping `.nc-desc-pane` columns with their own private stylesheet;
- a **seven-line** keybinding guide taking 200dp at the bottom of the panel;
- the character portrait absolutely positioned at the panel's top-right, which is where it sat
  when it was still a GPU sprite that panels had to dodge;
- no meta bar, no dotted rules, no `:: NAME` detail panel, no hint line — and no animated element,
  the only step without one.

## Layout

```
·····························································································
 Points left: stats 0  traits 0  skills 0  total 0                          READY TO BEGIN
·····························································································
 IDENTITY          5 ┊ ATTRIBUTES     18 ┊ BODY         6 ┊ EQUIPMENT   14 ┊  RECORD
>  NAME    Lon Woody ┊ STATS             ┊ TRAITS         ┊ WIELDED        ┊    ·▲·
   GENDER  Male  - + ┊  Strength       8 ┊  Fleet-Footed  ┊  crowbar       ┊  ·     ·
   HEIGHT  175cm - + ┊  Dexterity      8 ┊ BIONICS        ┊ WORN           ┊ ◆   ✶   ◆
   AGE     30    - + ┊ SKILLS            ┊  None!         ┊  jeans         ┊  ·     ·
   LOCATION Random…  ┊ MELEE SKILLS      ┊ SPELLS         ┊ INVENTORY      ┊    ·◆·
 BACKGROUND          ┊  Bashing        2 ┊  None!         ┊  lighter       ┊
  Scenario  Evacuee  ┊ …                 ┊ ADDICTIONS     ┊ VEHICLE        ┊  [legend]
  Profession Unemp.  ┊                   ┊  None!         ┊  None!         ┊
·····························································································
 :: NAME
 [portrait]  NAME Lon Woody · POINTS 0 left · STATUS Ready to begin      description…
 up/down field · left/right adjust · [Enter] edit · [S] template · [R] re-roll · [tab] finish
```

Everything except the identity rows and the seal is the shared vocabulary from
`newchar_common.rcss` (`.nc-meta`, `.nc-rule`, `.nc-vrule`, `.nc-cols`, `.nc-colwrap`,
`.nc-colhead`, `.nc-list`, `.nc-row`, `.nc-cell-*`, `.nc-selname`, `.nc-info-*`, `.nc-hint`),
unchanged. There is no balance scale: this step spends no points.

## The five identity fields become a row list, and all five become reachable

The measurement that decides the presentation is trivial here — **five** fields, each with a
label, a value and a way to change it. That is a checklist, so it is rows, the same conclusion
TRAITS reached at 188 and SKILLS at 28. What matters is the axis mapping.

| | before | now |
|---|---|---|
| `UP`/`DOWN` | changed height/age by 1, nothing on NAME | move the cursor through the five fields |
| `LEFT`/`RIGHT` | cycled NAME → HEIGHT → AGE | change the cursored field's value |
| `CONFIRM` | edit name/height/age | edit the cursored field (all five) |

This is the SKILLS mapping (`LEFT`/`RIGHT` adjust, `UP`/`DOWN` walk the list), and it is what makes
gender and location first-class: they now sit in the same column as the other three, carry the same
cursor, and answer the same keys. `CHANGE_GENDER` and `CHOOSE_LOCATION` stay registered as
shortcuts, so no muscle memory breaks.

`LEFT`/`RIGHT` per field: height ±1 cm, age ±1 year, gender toggles, location steps through the
scenario's allowed locations (the same list `CHOOSE_LOCATION` opens, `* Random *` first), name does
nothing — a name has no ordering to step along, and its editor is `CONFIRM`.

Row anatomy, fixed-width cells so the five values form a column:

```
cursor 10dp | label 92dp | value flex | step- 14dp | step+ 14dp | edit 20dp
     >      | NAME       | Lon Woody  |            |            |    …
            | HEIGHT     | 175 cm     |     -      |     +      |    …
```

Steppers are absent on NAME (`data-if`), the edit cell is present on all five. A row click moves
the cursor; the steppers and the edit cell are what change the character — reading a field is not
the same gesture as editing it, and NAME's editor is a modal. Every row is the same 22dp, the
invariant the shared `.nc-row` rule exists for.

`BACKGROUND` (scenario and profession) is a second group in the same column, not a row of spans
across the top: it is part of the record, it is not editable HERE, and the navigators are what go
back to those steps. It is not cursorable, so the cursor list stays exactly the five fields.

## The six panes become three record columns

Six wrapping panes plus an identity column plus the seal would be eight columns. The panes carry
~45 rows in total, so they fold into three columns of the shared row vocabulary at roughly the
density SKILLS uses (17 rows a column):

| column | groups |
|---|---|
| `ATTRIBUTES` | STATS (4 rows), SKILLS (category sub-headings, levels > 0 only) |
| `BODY` | TRAITS, BIONICS, SPELLS, ADDICTIONS |
| `EQUIPMENT` | WIELDED, WORN, INVENTORY, VEHICLE, COMPANIONS, PETS, CASH |

Group headings occupy a row of the same height as the items under them (`.nc-row.header`), which is
the same rule the interactive lists follow — here it costs nothing and keeps one stylesheet
governing every list in the creator. Addictions sit under BODY rather than with the kit because an
addiction is a bodily state; cash and the vehicle sit under EQUIPMENT because they are things you
start with. Column heads carry a count of their DATA rows (headings excluded), so `ATTRIBUTES 18`
is a promise about how much there is to read.

Every value the six panes printed is still printed, in the same order within its group, from the
same accessors. `.nc-desc-*` and `.nc-pane-*` go with the rework — the latter out of
`newchar_common.rcss`, where it had been the only shared rule with exactly one consumer.

## The record seal

The animated element, and the one that had to tie the whole wizard together rather than illustrate
one step: **a ring of the seven earlier steps, welded shut once per cycle.**

- Seven nodes on a ring, one per creator step from POINTS to SKILLS, at the same bearings a
  seven-slice pie would use — node 0 at 12 o'clock, running clockwise. Each node draws **the same
  runic glyph its step draws in the rail across the top of the screen** (`NC_TAB_ICON_SEEDS`), so
  the ring is recognisably the rail closed into a loop. The hub is the OVERVIEW glyph: the record
  the other seven feed.
- A weld bead travels the ring, one segment per node. Thread ahead of it is dark, thread behind it
  stays lit, and the bead itself is brightest — the `nc_bio_scan` rule (nothing ahead, decay
  behind) with a floor under the decay, because a weld leaves a bead rather than a fading wake.
  When the bead closes the ring the hub **flares** and the whole ring holds bright for a beat, then
  the cycle restarts from dark. That flare is the single deliberate discontinuity in the cycle.
- A node's GLYPH COLOUR is its step's state and never animates: gold when that step has content,
  dim gray when it is empty (no traits taken, no implants, no skills bought), **red when that
  step's budget is overspent**. The socket glow behind it is what the weld animates. So the seal
  doubles as a validity dashboard — the same information the meta bar's readiness word carries, and
  both read the same gate.
- Under the ring, a legend: one row per step with its glyph, its name (`nc_step_captions()`, the
  same translated strings as the rail) and its one-line summary — the pool in force, the scenario,
  the profession, the stat total, the trait count, the implant count, the skill levels. An
  unlabelled emblem is decoration; with the legend the seal IS the digest of the build.

Geometry is pure arithmetic in `src/newchar_seal.h`, on the **same 13×13 lattice the aptitude radar
uses**, and it calls `nc_apt`'s lattice and angle helpers rather than copying them:
`newchar_aptitude.h` says in as many words that a second copy of `wrap_turn` is how the
discontinuity at the 2π wrap gets reintroduced, and that warning applies to the next widget on a
lattice, not only to the radar.

Nothing is positioned or transformed: 13 flex rows of 13 flex cells, each cell either empty, a
thread dot (a background colour whose ALPHA the weld drives), a node (background glow + glyph
decorator) or the hub. Overlaying rotated geometry at one centre would need `position: absolute`,
which these documents resolve against `.nc-panel` — the trap the balance scale recorded.

### `struck` is a flag, not `arc == 2π`

`nc_apt::wrap_turn` is half-open, so a full turn wraps to 0. If the hold phase were expressed as
`arc = 2π` and fed through the angle arithmetic, every bearing would report itself as un-welded and
the closed ring would render dark — the ring would appear to vanish at the exact moment it
completes. The phase therefore carries an explicit `struck` flag and the glow functions
short-circuit on it.

## One gate for readiness, reason and refusal

`src/newchar_finish_gate.h`, the shape `nc_trait_gate` and `nc_bionic_gate` established: plain-data
inputs in, a state out, precedence in the header where it is testable without an avatar. Three
surfaces read it — the meta bar's readiness word, the STATUS fact in the detail panel, and the
`NEXT_TAB` popup chain.

Precedence, preserved case for case and word for word from the old `NEXT_TAB` block: invalid
(skills → traits → stats → generic) → spare points → no name → ready. The popup strings are
unchanged; only the plumbing changes, so no refusal behaves differently. What the player gains is
that the refusal is now on screen BEFORE they press the key — the old screen said nothing about
readiness until it rejected the keypress.

## Portrait

Into the detail panel's art slot, where every other preview-bearing step puts it. The absolutely
positioned top-right box is gone: it dates from the era when the avatar was a GPU sprite drawn
under the document, and this screen is the last consumer of that placement. `zoom_in`/`zoom_out`
and `TOGGLE_CHARACTER_PREVIEW_CLOTHES` stay registered, because there is still an avatar for them
to act on.

## Detail panel and hint line

`art | facts | prose`:

- **facts** are the cursored field (label = its name, value = its value, sub = its range, its count or
  the key that edits it), `BODY` (the weight height feeds, sub = the height itself — the one derived
  figure creation never showed, and both honour the imperial/metric option) and `STATUS` (the finish
  gate's word, sub = its reason). The first tracks the cursor; the other two are the finish line,
  which is what this screen is for.
- **prose** is what the field means — that an empty name is generated for you, that gender moves
  the profession and scenario name variants and the starting clothes, that height feeds body weight,
  that age is on the character sheet and advances with the calendar, that a random location rolls
  from the scenario's allowed set.

The seven-line guide collapses into one `.nc-hint` line, because five of its seven lines described
keys the cursor now makes obvious. What stays named there is what nothing else on screen teaches:
save template, re-roll, re-roll with a random scenario, and finish.

## Shared vocabulary promoted on its second adopter

| was | now |
|---|---|
| `.nc-cell-step` (`newcharskills.rcss`) | `newchar_common.rcss` — IDENTITY is its second adopter |
| `.nc-apt-legend` `.nc-apt-leg` `.nc-apt-leg-icon` `.nc-apt-leg-name` `.nc-apt-leg-val` | `.nc-tally` `.nc-tally-row` `.nc-tally-icon` `.nc-tally-name` `.nc-tally-val` in `newchar_common.rcss` |
| `.nc-pane-*` (`newchar_common.rcss`, one consumer) | deleted with the panes |

`.nc-tally` rather than `.nc-legend*`: that name is already taken by the horizontal sigil strip on
SCENARIO/PROFESSION, and a vertical icon | name | value list is a different thing. The SKILLS radar
legend moves to the new names with it — two copies of a visual language diverge.

## Verified

Built and driven in the installed macOS build (`osx-arm-slim`) through a temporary `CBN_NEWCHAR_TAB`
scaffold in `main_menu.cpp` — **removed**, the file byte-identical to HEAD (`git checkout --`), and
both binaries rebuilt from the scaffold-free source afterwards. The scaffold forced
`sel1 = NEWCHAR, sel2 = 2` (Random Character), which is the one entry point that already starts on
`NEWCHAR_TAB_MAX`, so no change to `newcharacter.cpp` was needed this time.

- Screen renders end to end: step rail, meta bar with the pool line and `Ready to begin`, dotted
  rules, IDENTITY column with all five fields plus the BACKGROUND group, the three record columns
  (`ATTRIBUTES 7` / `BODY 16` / `EQUIPMENT 12` on the character measured), the seal with its legend,
  `:: NAME`, the `art | facts | prose` panel and the hint line.
- **The weld is measured, not eyeballed.** Eight frames at ~250 ms, gold pixels inside the ring's
  bounding box: the lit count climbs 364 → 616 → 779 → 1002 → 1234 → 1698 within one pass while the
  brightness centroid advances 338° → 38° → 74° → 122° → 179° → 241°, then the count drops back to
  ~360 as the next pass starts from dark. That is a bead travelling clockwise and leaving lit thread
  behind it, not the whole ring pulsing.
- Keyboard: `DOWN` `DOWN` moved the cursor NAME → GENDER → HEIGHT; `LEFT` ×3 took height 200 → 197 cm
  and the panel's HEIGHT fact and derived `BODY 97 kg / 197 cm` followed it; `RIGHT` on GENDER
  toggled to Male and the panel header switched to `:: GENDER`. `RIGHT` on a height already at 200
  did nothing and that row's `+` was drawn `.off` — the clamp and the affordance agree.
- Mouse: clicking the AGE row's `+` moved the cursor to AGE and took age 17 → 18 → 19, one increment
  per click on the FIRST click. The `SELECT`/`COORDINATE`/`ANY_INPUT` registration the BIONICS pass
  established is what makes that land.
- `[seal]` + `[finish_gate]` + `[newchar]` + `[new_character]` + `[aptitude]` + `[dna]` +
  `[bio_scan]` + `[trait_gate]` + `[bionic_gate]` + `[stat_meter]` + `[balance]`: **56,437 assertions
  in 70 test cases, all passing.**
- The session log is clean: **one** WARNING in 22,818 lines (the pre-existing "Failed to detect
  system UI language" at startup) and **zero** ERROR lines — no unbound `data-for`, no `decorator: ;`
  or `width: ;` from an empty style value, no unknown RCSS token.
- SKILLS still renders after the legend rename — radar, legend rows and row steppers all styled by
  the promoted `.nc-tally*` / `.nc-cell-step` rules.
- Warnings for `newcharacter_ui.cpp` went from **10 at HEAD to 0**. The `-Wunused-macros` noise was
  never really about unused colours: the `COL_*` block existed TWICE, verbatim, forty lines apart, so
  every use resolved against the second copy and the first warned as never expanded. The duplicate is
  gone, and `COL_TR_NEUT`/`COL_HEADER` went with it — the panes were their last consumers. Zero
  warnings from either new header.
- The formatter converges: `cmake --build … --target format` reports `Unchanged` for every file
  touched here on a second consecutive run. Three lambdas are shaped around astyle 3.6.16's
  trailing-return-type mis-indent (an accumulator and one `return` instead of a leading if-guard or
  switch), which is recorded in the code where it will be read.

### Not verified by this session

The `…` edit cells and `CONFIRM` on NAME / HEIGHT / AGE / LOCATION open modal popups; those were not
driven, nor were `SAVE_TEMPLATE`, the two re-rolls or `RANDOMIZE_CHAR_DESCRIPTION`. Their plumbing is
the same `pending_*` intent path the steppers proved, and the popups themselves are the pre-existing
code paths moved into `edit_field` unchanged. `NEXT_TAB` was deliberately not pressed — it starts the
game and writes a save.
