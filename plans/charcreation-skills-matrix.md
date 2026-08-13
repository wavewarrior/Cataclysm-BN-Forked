# SKILLS step — row lists with level meters plus an aptitude matrix

Sixth creator step reworked, after SCENARIO, PROFESSION, STATS, TRAITS and BIONICS. Read
`plans/charcreation-traits-tree.md` for the row anatomy and the click-intent rule,
`plans/charcreation-bionics-chassis.md` for the shared-vocabulary promotion and the
`COORDINATE`/`ANY_INPUT` input trap, and `plans/charcreation-stats-cards.md` for the pip meter
this borrows. None of that is re-derived here.

## What was there before

The pre-rework screen: a 320dp `scroll-pane` of one `data-rml` line per skill (`Marksmanship (2)
(+1)`), yellow category headers, and a full-width text pane carrying the description and every
recipe the skill unlocks. No cursor glyph, no cost column, no per-skill facts, no meter — the one
thing a skill has that a trait does not is **a magnitude**, and the screen expressed it as a
number in parentheses inside a coloured string.

## Rows with a meter, and the measurement that decides it

Counted over `data/json/skills.json` — **28** skills in **5** display categories plus one
uncategorised (`weapon`, `display_category: none`):

| category | display_string | skills |
|---|---|---|
| `display_ranged` | Ranged skills | 8 |
| `display_interaction` | Interaction skills | 6 |
| `display_melee` | Melee skills | 6 |
| `display_crafting` | Crafting skills | 5 |
| `display_social` | Social skills | 2 |
| `none` | (empty → `Other skills`) | 1 |

28 items of name / level / price is a checklist, so it is a row — the conclusion TRAITS reached at
188 and BIONICS at 34. But each row carries a 0..`MAX_SKILL` magnitude, which is what STATS
answers with a pip meter, so the row's second cell is that meter rather than a checkbox. A column
of meters can be run down as a competence profile; `(2)` inside a sentence cannot.

28 skills + 6 headings = **34 rows**, which is why this step uses **two** columns rather than
three: 17 rows a column fits without scrolling at 1080p, and each row needs width for steppers,
meter, level, price and name.

## Layout

```
·····························································································
 Points left: …                                                        Skill points left: 6
·····························································································
 MELEE SKILLS        6         ┊ INTERACTION SKILLS   6      ┊  APTITUDE                14
> - ▮▮▮▮▁▁▁▁▁▁ + 4   [ 2] Melee┊  - ▁▁▁▁▁▁▁▁▁▁ + 0  [ 1] …  ┊       · · ·
  - ▮▮▁▁▁▁▁▁▁▁ + 2   [ 1] Dodge┊  …                          ┊    ·  ▪ ▪ ▪  ·   13x13 dot
 RANGED SKILLS       8         ┊ CRAFTING SKILLS      5      ┊   · ▪ ▪ ✱ ▪ ▪ ·   radar with
  …                            ┊  …                          ┊    ·  ▪ ▪ ▪  ·   a sweeping
                               ┊ SOCIAL SKILLS        2      ┊       · · ·       beam
·····························································································
 :: MARKSMANSHIP
 [sigil]  LEVEL 4 · NEXT LEVEL 2 points · CATEGORY Ranged skills · UNLOCKS 12   description…
                                                                                 recipes (scroll)
·····························································································
```

Everything except the meter cells, the steppers and the matrix is the shared row-list vocabulary
from `newchar_common.rcss` (`.nc-meta`, `.nc-rule`, `.nc-vrule`, `.nc-cols`, `.nc-colwrap`,
`.nc-colhead`, `.nc-list`, `.nc-row`, `.nc-cell-*`, `.nc-selname`, `.nc-info-*`, `.nc-hint`),
unchanged.

There is no balance scale: skills have no advantage/disadvantage budget, and the scale is already
`data-if="balance.show"`, so it is simply absent rather than special-cased.

## Columns are a wrap of ONE list, so LEFT/RIGHT stay on the level

TRAITS and BIONICS spend LEFT/RIGHT on changing column. This step cannot: LEFT/RIGHT have always
been *lower/raise the skill*, which is the primary action of the whole screen.

So the two columns are a **wrap of a single ordered list**, not two independent lists. The cursor
walks the flat visual order — top of column 0 down, then top of column 1 down, wrapping — which is
the order the eye reads them in anyway. Nothing needs a new binding, and `UP`/`DOWN` crossing the
column boundary is one modulo over the concatenated row sequence.

Categories are packed into the columns by a **linear partition preserving sorted order**: fill
column 0 until it holds at least half the rows, then column 1. Derived from the row counts, so a
mod adding skills or categories redistributes without touching this code, and the split is stable
across runs. Never a hardcoded category-id list.

## Row anatomy

```
cursor 10dp | step 14dp | meter 84dp | step 14dp | level 34dp | cost 44dp | name flex
     >      |     -     | ▮▮▮▮▁▁▁▁▁▁ |     +     |   4 +1     |   [ 2]    | Melee
```

- **Meter**: `MAX_SKILL` pips. `on` = bought, `bonus` = granted by the profession *after*
  creation, `steep` = a level whose price is 2 points or more (`skill_increment_cost` is
  `max(1,(level+1)/2)`, so pips 4 and up). Filled pips are taller as well as brighter, the
  greyscale channel STATS established.
- **Level** shows the number the meter draws, plus `+N` when the profession grants more — colour
  alone must not be the only carrier of a quantity.
- **Cost** is the price of the NEXT level, red when it is unaffordable, `[max]` at the cap. It
  replaces the old free-floating "Upgrading %s by %s costs %d points" line, which said the same
  thing about one skill in a place the eye had to leave the list to read.
- **Steppers** flank the meter, `-` on its left and `+` on its right, so the mouse gesture is
  spatially the same as the LEFT/RIGHT keys. `.off` when at the floor or the cap.
- Headings occupy a row of the same height (the invariant the cursor-scroll arithmetic depends
  on), with the meter, steppers, level and cost cells absent via `data-if="!r.header"`.

## The aptitude matrix

The animated element. A 13×13 **dot-matrix radar** — the shape `plans/dot-radar-minimap.md`
established for the in-game minimap, reused here as the character's competence profile:

- One filled **sector** per display category — a wedge, not a radial spoke. Sector 0 begins at 12
  o'clock and they run clockwise, so the legend under the disc reads in sector order. Spokes were
  built first and rejected: on a 13-cell lattice a spoke is at most one dot wide per ring, which is
  too little ink to compare two categories at a glance, while a wedge compares as an area.
- A sector's **depth** is that category's share of the strongest category, floored so that half of
  one skill's range must be invested before a wedge reaches the rim. Measured: a finished creation
  buys a handful of levels, so against the theoretical cap (every skill at `MAX_SKILL`) each wedge
  was under a cell deep and the disc was a dot with a legend. The comparison is what the player
  wants from it anyway; the absolute count sits in the legend beside each name.
- Three dim **rings** plus a lit hub give the disc its scale.
- A **beam** sweeps one revolution per 5.5 s. Every dot it passes brightens and then fades over
  `tail` radians behind it — trailing, never leading, so the direction of travel is unambiguous.
  That is the same rule `nc_bio_scan` pins, in angle rather than in rows.
- The cursor's own category is drawn at full strength and the others dimmed, so the radar tracks
  the list.
- A legend under the disc names each sector with its sigil and its level count, because an
  unlabelled radar is decoration.

Geometry is pure arithmetic in `src/newchar_aptitude.h`, exercised without an RmlUi document.

**Composition is by ALPHA, not by blending.** Each dot has one base colour and base alpha by
layer (field / ring / hub / sector); the sweep raises that alpha toward 255. On a dark ground that
is what the compositor is for, and it means no colour arithmetic exists to get wrong — the lesson
`nc_bio_scan` recorded.

**The sectors must TILE the turn.** A gap leaves dots no category can claim; an overlap makes a
dot's colour depend on which category was asked first. `sector_of` is therefore the single owner
test, clamped rather than wrapped at the top end so a bearing one ULP under 2π lands in the last
sector instead of a phantom `n`-th one.

Nothing is positioned or transformed. The matrix is 13 flex rows of 13 flex dots, so it stays
square and aligned at any stage size — the rule the balance scale's beam was written for, and the
reason a circular radar is drawn as a dot field instead of as rotated bars: overlaying elements at
one centre would need `position: absolute`, which these documents resolve against `.nc-panel`.

## One table for sigil, colour and legend

`nc_skill_cat_art_of( id, order )` maps a display category to `{ seed, colour }`. Three surfaces
read it: the column heading's sigil, the sector colour on the radar, and the detail panel's art
slot. They cannot disagree.

The two halves are derived differently, on purpose:

- the **seed** is an FNV-1a hash of the category id (`nc_skill_cat_seed`), not `std::hash`, which is
  not required to agree between platforms or runs — the same category must draw the same glyph
  everywhere, including for a modded category nobody listed here;
- the **colour** comes from the category's POSITION in the sorted list, indexing a six-entry
  palette. A hash would happily hand two neighbouring wedges the same colour, and telling the
  wedges apart is the entire job of the palette.

Category display names come from `SkillDisplayType::get_skill_type(id).display_string()`, already
translated; the uncategorised `weapon` skill falls back to `_( "Other skills" )` rather than
rendering an empty heading.

## Detail panel

`art | facts | prose`, the shared panel:

- **art** is the category's sigil, not the character portrait: a skill does not change how the
  avatar looks, and the sigil ties the panel to its sector on the radar and to the column heading
  it sits under. The
  preview's zoom/clothes actions stay unregistered for the same reason BIONICS drops them.
- **facts** are LEVEL (sub: what the profession adds after creation), NEXT LEVEL (sub: the
  free-extra-level rule at 0, or that the cap is reached), CATEGORY (sub: `N levels across M
  skills`, the same count the radar draws that sector from and the same one its legend prints) and
  UNLOCKS (how many recipes this skill unlocks).
- **prose** is the skill's own description in `.nc-info-blurb`, with the recipe list beneath it in
  a scrolling `.nc-info-detail` — which is what `SCROLL_UP`/`SCROLL_DOWN` now scroll. A skill
  unlocking 40 recipes cannot push the description out of the panel.

`nc_skill_recipes_desc` splits into `nc_skill_recipe_list`, returning the text AND the count, so
the UNLOCKS fact and the prose pane cannot disagree. The recipe scan walks all of `recipe_dict`,
so it runs behind the `model_dirty` gate — never on an animation tick.

## Interaction

- `UP`/`DOWN` move through the flat visual order, skipping headings, crossing the column boundary.
- `LEFT`/`RIGHT` lower/raise the cursor skill, with the existing balance rules intact (0 → 2
  levels for one point; 2 → 0 forfeits the free level).
- `CONFIRM` raises, the same path `+` takes.
- Row click moves the cursor; the steppers change the level. Reading a skill is not the same
  gesture as buying one.
- `RANDOMIZE` jumps the cursor, `SCROLL_UP`/`DOWN` scroll the recipe pane, `NEXT`/`PREV_TAB`,
  `QUIT`: unchanged.
- `COORDINATE` and `ANY_INPUT` are registered as well as `SELECT` — all three are required for a
  click to reach the loop at all. See the BIONICS plan for why either alone parks it.

## Shared vocabulary promoted on its second adopter

`.nc-pip` (+ `.on`, `.base`, `.steep`) moves from `newcharstats.rcss` to `newchar_common.rcss`,
because SKILLS is its second adopter. `.nc-stat-meter`, `.nc-stat-step` and the
`.nc-legend-item .nc-pip` override stay with STATS; `.nc-cell-meter`, `.nc-cell-lvl` and
`.nc-cell-step` stay with SKILLS until something else wants them.

The dead `.nc-skill-row` selectors go with the rework: the old class disappears from the cursor-row
selector list at the top of `newchar_common.rcss` and its `.header` rule is deleted, rather than
being left to accumulate.

## Verified

Built and driven in the installed macOS build (`osx-arm-slim`) through a temporary
`CBN_NEWCHAR_TAB` scaffold in `main_menu.cpp` + `newcharacter.cpp` — **removed**, both files
byte-identical to HEAD (`git checkout --`), and the shipped binary rebuilt from the scaffold-free
source afterwards. See the bottom of `plans/charcreation-bionics-chassis.md` for what synthetic
input on this build is and is not evidence of.

- `opened document data/gui/newcharskills.rml` in `/tmp/cbn-nc/config/debug.log`, and **zero**
  WARNING or ERROR lines in the ~9,000 log lines after it — no missing binding, no unknown RCSS
  token, no `decorator: ;` / `width: ;` from an empty style value.
- Rows read exactly as designed, transcribed from a zoomed frame: `> − [▮▮ dim×8] + 2 [ 1] Bashing
  Weapons`, i.e. cursor, minus, meter with two tall gold pips against eight short dim ones, plus,
  level, price, name. A level-0 row shows ten dim pips and `0`. `Bartering` shows `0 +1`, the
  profession's grant as a distinct blue count with matching blue pips.
- Column captions `SKILLS 14` and `CONTINUED 15` with the category sub-headings MELEE SKILLS /
  RANGED SKILLS / CRAFTING SKILLS / INTERACTION SKILLS / SOCIAL SKILLS in place, and the meta bar
  carrying both the pool line and `Skill points left: 0`.
- Detail panel: `:: BASHING WEAPONS`, LEVEL `2`, NEXT LEVEL `1 point`, CATEGORY `Melee skills`,
  UNLOCKS `0 recipes`, the category sigil in the art slot, the skill's description as prose, and
  the hint line `left/right raise or lower · NPAGE scroll recipes`.
- The disc renders as a circle on the 13×13 lattice (measured bounding box 137×133 px for a
  182dp square), with `APTITUDE 8` — the level count — in its heading.
- Sectors are measured, not eyeballed: saturated in-disc pixels per 60° wedge came out
  26 / 189 / 28 / 110 / 166 / 227, i.e. six wedges of visibly different depth rather than one
  uniform field. A spoke layout could not produce those counts.
- The sweep is real and moves: the per-frame deviation from the six-frame average has its centroid
  at bearings 208°, 168°, 32°, 303°, 187°, 44° — a highlight travelling round the disc rather than
  the whole ring pulsing. **The user confirmed the direction is clockwise**; the ~1.4 s capture
  interval against a 5.5 s revolution aliases too badly for a screenshot sequence to settle that.
- `[aptitude]` + `[newchar]` + `[dna]` + `[bio_scan]` + `[trait_gate]` + `[bionic_gate]` +
  `[mutations]`: all passing, 39,310 assertions in 41 cases for the first four alone.
- Warnings for `newcharacter_ui.cpp` went from 11 at HEAD to **10** — all pre-existing
  `-Wunused-macros` on the `COL_*` defines. The eleventh was the `selected` set-but-unused local in
  the old `set_skills`, which this rework deletes. Zero warnings from `newchar_aptitude.h`.

### Not verified by this session

Mouse row-click, the two steppers, and `RANDOMIZE` were not exercised. Synthetic clicks on this
build land a frame or a keypress late, which the BIONICS pass already recorded as an unreliable
oracle; the input registration they depend on (`SELECT` + `COORDINATE` + `ANY_INPUT`) is the same
set that pass fixed and verified. Leave the interaction sweep to a human.
