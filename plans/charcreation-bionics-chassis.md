# BIONICS step — row lists plus a chassis diagram

Fifth creator step reworked, after SCENARIO, PROFESSION, STATS and TRAITS. Read
`plans/charcreation-traits-tree.md` for the row anatomy and the click-intent rule, and
`plans/charcreation-profession-equipment.md` for the paper-doll grid this borrows; neither is
re-derived here.

Reference: `UI_designs/02_cybernetics_terminal.png` — an implant list on the left, a body
diagram on the right with a slot box wired to each region.

## What was there before

The BIONICS tab was still the pre-rework TRAITS screen with bionic data poured into it: three
`scroll-pane` columns of bare `data-rml` HTML, one coloured name per line, a 3-line description
footer, and nothing else. No cursor glyph, no cost column, no checkbox, no per-item facts, and
no hint of the one thing a bionic has that a trait does not — **a place in the body**.

## Rows, not cards, and the measurement that decides it

Counted over `data/json/bionics/` — 141 bionics, **34** with `starting_bionic`:

| column | rule | count |
|---|---|---|
| Advantages | `points > 0` | 15 |
| Disadvantages | `points < 0` | 19 |
| Neutral | `points == 0` | 0 (so vanilla shows two columns) |

34 items of name / price / do-I-have-it is a checklist, which is a row — the same conclusion
TRAITS reached at 188. Cards would also spend their area re-stating in text the fact this screen
can show spatially: 30 of the 34 declare `occupied_bodyparts`, so *where it goes* is the
distinguishing fact, and a diagram beats 34 copies of a sentence.

Column names reuse `_("Advantages")` / `_("Disadvantages")` / `_("Neutral")` verbatim — the same
strings TRAITS uses, because it is the same budget (`num_good` / `num_bad` / `MAX_TRAIT_POINTS`
are shared across the two steps) and because inventing "Enhancements/Impairments" would be PO
churn for a synonym.

## Layout

```
·····························································································
 Points left: …                                                       Bionic points left: 4
·····························································································
                                    [ balance scale ]
 ADVANTAGES 15        ┊ DISADVANTAGES 19    ┊  CHASSIS                       6 implants
>[x] [+3] Night Vision┊  [ ] [-2] Ugly      ┊   ┆  ┌ head ─┐
 [ ] [+1] Bio Ears    ┊  [-] [-4] …         ┊   ┆  │ ▓▓ +1 │
 …                    ┊  …                  ┊   ┆  └───────┘   … 6x3 body grid …
·····························································································
 :: NIGHT VISION CBM
 [cbm art]  COST 3 · SITE head, eyes · POWER Passive · STATUS Available     description…
```

The chassis is a fourth column of `.nc-cols`, exactly where TRAITS puts its DNA strand, so the
two steps have the same silhouette. Everything above and below the columns — meta bar, dotted
rules, balance scale, `:: NAME` header, `art | facts | prose` panel, hint line — is the shared
vocabulary unchanged.

## The lists group by chassis region

Flat columns of 15 and 19 rows were the first cut, and they looked poured rather than designed: a
solid slab of rows stopping two-fifths down a tall column, with nothing telling the eye where one
kind of implant ended. Two changes fixed it, both of which the reference art was already doing.

**Region sub-headings**, head to feet — HEAD AND FACE / TORSO / ARMS AND HANDS / LEGS AND FEET /
NO IMPLANT SITE — so a column reads top-to-bottom in the same order the diagram beside it does,
and a heading names something the player can point at on the body. Derived from
`occupied_bodyparts`: an implant belongs to the region of the part it claims the MOST space in,
which is the same rule the diagram's boxes use to pick the implant they show. A modded CBM files
itself; one occupying a modded body part lands in NO IMPLANT SITE rather than being mis-filed.
Ties break in head-to-feet order, so the grouping is stable across runs.

A heading OCCUPIES A ROW of the same 22dp height, which is the invariant the cursor-scroll
arithmetic depends on; `skip_headings` steps the cursor over them in the direction of travel and a
click on one is ignored rather than parking the cursor on a label.

**Rows sit on the dark ground.** The markup carries `theme.rcss`'s `.item`, whose `{{bg1}}` fill
turns a column into one pale slab and flattens the contrast of every semantic colour in it — the
exact defect already recorded for `.scroll-pane` at the top of `newchar_common.rcss`, and the same
fix: `.nc-row` sets `background-color: transparent`, and only the cursor row and hover paint one.
Applied in the SHARED sheet, so TRAITS gets it too.

## The chassis is anatomy first, capacity second

`CBM_SLOTS_ENABLED` is an EXTERNAL_OPTION defaulting to **false** (`data/json/game_balance.json`;
the bundled `cbm_slots` mod turns it on), and `Character::bionic_installation_issues` returns
early when it is off. So a `used / total slots` readout as the primary content would present a
rule that is not in force in a default game.

What is always true is `occupied_bodyparts`, which drives encumbrance, heating and coverage
regardless of the option. So the diagram shows **which implants live where**:

| always | when `CBM_SLOTS_ENABLED` |
|---|---|
| part caption, sprite of the implant occupying the most space there, `+N` for the rest | `used/total` plus a proportional capacity bar per part |
| gold outline on every part the cursored bionic occupies | red outline when it does not fit, and the shortfall in the SITE fact's sub-line |

Grid is the equipment doll's, cell for cell — six rows of three, every position emitted, a
junction cell drawing spine + rung and a blank holding its column open, so alignment comes from
equal flex with no pixel arithmetic:

| row | left | centre | right |
|---|---|---|---|
| 0 | — | HEAD | — |
| 1 | EYES | junction | MOUTH |
| 2 | ARM L | TORSO | ARM R |
| 3 | HAND L | junction | HAND R |
| 4 | LEG L | junction | LEG R |
| 5 | FOOT L | junction | FOOT R |

Those twelve are exactly the parts that declare `bionic_slots` (torso 80, head 18, arms 20, legs
30, feet 7, hands 5, eyes 4, mouth 4), so the diagram covers every site a bionic can claim.

Sprites come from the CBM item through `nc_tile_sprite_dec( id, C_ITEM )`. Every one of the 34
starting bionics has an item whose id equals the bionic id (measured: 0 misses), and
`find_tile_looks_like` handles a modded CBM with no art of its own.

### Occupancy counts profession CBMs; the install check does not

`u.prof->CBMs()` are installed by `avatar::add_profession_items` (`newcharacter.cpp:674`) — i.e.
AFTER the wizard — so during creation `u.has_bionic()` is false for them. The list has always
compensated (`avatar_has = has_bionic || in prof CBMs`), and the chassis counts them the same
way, because it describes the character being built rather than the half-built object in memory.

`bionic_installation_issues` is deliberately left alone and keeps asking the game's own
accounting, so no refusal changes behaviour. The two can disagree only for a profession-granted
CBM under a mod that enables slots, which is the pre-existing state of that mechanic, not
something this screen should paper over.

## Input: take the whole registration from TRAITS, not most of it

The first cut deliberately skipped `COORDINATE` and `ANY_INPUT` on the grounds that they exist on
TRAITS only to drive its appearance preview. That was wrong, and it presented as a dead checkbox.

`MOUSE_LEFT` arrives as `SELECT` on mouse DOWN, but RmlUi fires its `click` — and therefore the
`data-event-click` callbacks — on mouse UP. That UP event resolves to `CATA_ERROR`, and
`input.cpp:894-897` `continue`s on an unrecognised MOUSE event WITHOUT returning, so
`handle_input()` stays parked right through the callbacks and the intent they recorded is cleared
at the top of the next iteration — which only runs when some later input wakes the loop. The tell
was subtle: a row click appeared to work, because the FOLLOWING click's mouse-down flushed it.

`COORDINATE` sets `handling_coordinate_input`, which skips that early `continue`; `ANY_INPUT`
makes the fall-through at `:912` return. Either alone still parks the loop. `ANY_INPUT` is then
excluded from the model-dirty test, so a pointer sample does not re-gate every row.

## No character preview on this step

The other preview-bearing steps put the avatar in the detail panel's art slot. Here the art slot
carries the CBM's own sprite instead: the chassis IS this screen's body view, an implant does not
change how the avatar looks, and the sprite ties the panel to the boxes on the diagram. The
preview's zoom and clothes actions are not registered for the same reason — there would be nothing
on screen for them to act on.

## The scan sweep

The animated element. A diagnostic pulse runs head-to-feet down a bus rail at the chassis edge,
and each body row's slot boxes light a hairline scanline as the pulse passes — the terminal
reading the body out. Geometry is pure arithmetic in `src/newchar_bio_scan.h`.

- One float phase from `steady_clock`, as the DNA strand does, so the sweep neither accelerates
  while a key is held nor stalls when the player stops typing.
- Per row the producer emits ONE colour string, `#c4a832aa`, consumed by two elements (the rail
  segment and that row's scanlines). Intensity becomes ALPHA rather than a blend, so the fade is
  the compositor's job and no colour arithmetic is needed.
- A scan has a trailing glow and no leading one: `intensity` is 0 ahead of the head, 1 at it, and
  decays linearly over `tail` rows behind. Tests pin exactly that, plus wrap continuity — a
  discontinuity at the wrap is the one defect that would read as a flicker rather than a sweep.
- Nothing is positioned or transformed. The rail is one flex segment per doll row, so it stays
  aligned with the grid at any stage size — the rule the balance scale's beam was written for.

## One gate for state, reason and refusal

`src/newchar_bionic_gate.h`, the shape `nc_trait_gate` established: plain-data inputs in, a
`state` out, precedence in the header where it is testable without an avatar. Three surfaces read
it — the row's `[x]`/`[ ]`/`[-]`, the STATUS fact's reason line in words, and the CONFIRM popups.

The refusal ORDER is the existing `set_bionics` chain, preserved case for case and word for word:
dependants → scenario forbids all → profession forbids all → scenario forbids this → profession
forbids this → scenario locked → profession locked → conflicting traits → over budget → no space
→ missing prerequisites → downgrade held → upgrade held. Only the *plumbing* changes: each arm now
reads a gate field instead of re-asking the game mid-chain.

Two things the gate makes explicit that the old chain left implicit:

- **`granted`** — a profession CBM the avatar does not yet hold renders `[x]` (it always did) and
  is not toggleable. Previously that state fell through to the scenario/profession *locked* arms,
  which is why a take attempt could raise "prevents you from removing this bionic". The popup
  wording is unchanged; the gate just stops pretending the row was available.
- **`over_budget` keys off the point sign**, not `iCurWorkingPage`. Same defect TRAITS had: a
  zero-point bionic in a modded neutral column took the disadvantage branch because it was "not
  an advantage".

## Interaction

- `LEFT`/`RIGHT` change column, `UP`/`DOWN` move within it — the axis mapping this tab always had.
- A row click moves the cursor; the checkbox toggles. Toggling spends points and can raise a
  modal, so reading an implant is not the same gesture as installing it.
- `CONFIRM` toggles the cursor row through the same lambda the checkbox uses.
- `RANDOMIZE`, `REROLL_*`, `NEXT`/`PREV_TAB`, `QUIT`: unchanged. Zoom and clothes toggles are gone
  with the character preview.
- Headings are skipped by cursor movement in both directions, and a click on one is ignored.

## Shared vocabulary promoted on its second adopter

The row-list language landed on TRAITS with `nc-trait-*` names. BIONICS is the second adopter, so
it moves to `newchar_common.rcss` under names that do not lie about what they style — two copies
of a visual language diverge, and a bionics row called `.nc-trait-row` is a trap for the next
step that adopts it:

| was (newchartraits.rcss) | now (newchar_common.rcss) |
|---|---|
| `.nc-trait-top` `.nc-trait-points` `.nc-trait-budget` | `.nc-meta` `.nc-meta-pool` `.nc-meta-budget` |
| `.nc-trait-cols` `.nc-trait-colwrap` `.nc-trait-colhead` `.nc-trait-colname` `.nc-trait-colcount` `.nc-trait-list` | `.nc-cols` `.nc-colwrap` `.nc-colhead` `.nc-colname` `.nc-colcount` `.nc-list` |
| `.nc-trait-row` (+`.header`) | `.nc-row` |
| `.nc-tr-cursor` `.nc-tr-check` `.nc-tr-cost` `.nc-tr-name` | `.nc-cell-cursor` `.nc-cell-check` `.nc-cell-cost` `.nc-cell-name` |
| `.nc-rule` `.nc-vrule` `.nc-trait-selname` `.nc-trait-hint` | `.nc-rule` `.nc-vrule` `.nc-selname` `.nc-hint` |

`id="nc-trait-col0"` becomes `id="nc-col0"` with it, and the C++ `string_format( "nc-col%d", … )`
that scrolls a column's cursor into view moves with the markup. The cursor-row selector list at
the top of `newchar_common.rcss` loses `.nc-trait-row` and gains `.nc-row`.

Per-step sheets keep only what is theirs: TRAITS keeps the DNA strand, BIONICS keeps the chassis.

## Verified

Built and driven in the installed macOS build (`osx-arm-slim`), through a temporary
`CBN_NEWCHAR_TAB` scaffold in `main_menu.cpp` + `newcharacter.cpp` — **removed**, both files
byte-identical to HEAD, and the shipped binary rebuilt from the scaffold-free source afterwards.

- Screen renders end to end: step rail, meta bar with both budgets, balance scale, two grouped
  columns with region sub-headings, the chassis, `:: NAME` header, `art | facts | prose` panel and
  the hint line. The third column stays absent, which is correct — vanilla has no zero-point
  bionic.
- Facts read correctly per implant: `COST 1 point`, `SITE mouth 1 and head 2`, `POWER Passive`,
  `STATUS Available` — and `STATUS Unavailable / No points left on that side of the budget` on a
  character with a spent budget, which is the refusal the old screen only showed after CONFIRM.
- Selecting an implant outlines exactly the parts it occupies: eyes for Anti-Glare Compensators,
  head + mouth for Bionic Nostril, matching that implant's SITE fact.
- The scan sweep is measured, not eyeballed: 12 frames at ~260 ms, gold pixels (`#c4a832`) in the
  chassis region minus the static set, giving a moving highlight whose y-centroid visits 277, 348,
  420, 492 and 563 px — the five lower doll rows — and goes dark between passes.
- Row click, checkbox toggle and the navigators confirmed working by the user in the shipped-shape
  build. Synthetic clicks were NOT a reliable oracle here: several landed but their effect appeared
  only on a later frame, which is what sent the first diagnosis of the input bug down the wrong
  path. The `COORDINATE`/`ANY_INPUT` fix above is what made them responsive.
- RmlUi log is clean for this document: `width: ;` went from thousands per session (an empty
  `bar_w` on every hidden capacity bar, every frame) to **0**, and 0 `ERROR` lines. The 4 remaining
  `decorator: ;` fire once at document open, from the shared `nc_balance` icon defaults that every
  step's first frame emits.
- `[bionic_gate]` + `[bio_scan]` + `[newchar]` + `[mutations]` + `[trait_gate]` + `[dna]` +
  `[bionic]`: 10,060 assertions in 38 cases, all passing.
- Warnings for `newcharacter_ui.cpp` went from 22 at HEAD to **11**, all pre-existing
  `-Wunused-macros` on the `COL_*` defines plus one pre-existing `selected` set-but-unused. The 12
  `COL_TR_*_ON/OFF_*` macros this rework made dead were deleted rather than left warning. Zero
  warnings from either new header.

### The "crash on returning to main menu" was the first scaffold

Reported during review, and worth recording because it looks exactly like a product bug — and
because the first write-up of it here was reasoning from control flow rather than a run, which is
not verification.

The first scaffold had `opening_screen` **return** `new_character_tab()` instead of entering its own
menu loop, so QUIT from the creator ended `opening_screen` and the process exited. That is what the
user hit.

Re-tested with a loop-faithful scaffold that enters the creator from INSIDE the menu loop exactly as
CONFIRM on NEW CHARACTER does. From that run's log (`/tmp/cbn-nc/config/debug.log`):

- `opened document data/gui/newcharbionics.rml (2 open)` — the run was on the BIONICS step, not the
  main menu.
- ESC raised `query_popup.rml`, i.e. the step's `query_yn( "Return to main menu?" )`.
- The open-document count then fell from 3 to 2 with `mainmenu.rml` still open: the creator closed
  and the menu was live behind it.
- Shutdown was clean — `Log shutdown.`, no backtrace, no `ERROR`, and both `crash.log` copies are
  stale (VERSION 2f5d5a1fe1, stack in `load_soundset` at startup, unrelated to this path).

What was NOT observed by this session: the main menu redrawn and accepting input after the return.
The synthetic keypress that triggered the ESC ran with `delivery: "foreground"` and the harness
aborted that call, taking the child with it — which is what every exit code 25 in this session was.
**The user confirmed the return-to-menu path manually**; that is the observation this claim rests on.

Method note for the next step: synthetic input is not a trustworthy oracle on this build. Clicks
land but their effect can appear a frame or a keypress later, and a foreground keypress can be torn
down mid-sequence. Prefer the log's `opened document` lines and pixel measurements for state, and
leave the final interaction pass to a human.
