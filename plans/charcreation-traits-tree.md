# TRAITS step — three columns of rows

Fourth creator step reworked, after SCENARIO, PROFESSION and STATS. Read
`plans/charcreation-scenario-tree.md` for the click-intent rule and
`plans/charcreation-stats-cards.md` for the flex/bare-text trap; neither is re-derived here.

Reference: `UI_designs/11_charcreation_mutations.png`.

## Cards were tried first and rejected

The first implementation applied the card tree verbatim: eight collapsible bands, each a 6-card
carousel with a sigil strip. Reviewed in-game as **not readable**, and the diagnosis holds up:

- A card is 96–116dp tall and carries one short name. 188 traits at six per page is 31 pages of
  paging through mostly-empty rectangles, where the flat list showed 60 names at once.
- The strip needed ten sigils to encode availability — and availability is the one thing traits
  have lots of. Ten procedural runic placeholders is a cipher, not storytelling; SCENARIO's
  flags and PROFESSION's gifts are each a handful of *facts*, which is what that pattern suits.
- Cards spend their area on chrome. Traits are a checklist: a name, a price, and whether you
  have it. That is a row.

So this step keeps the vocabulary (dark grounds, gold cursor, the fixed `art | facts | prose`
panel, the typographic scale) and drops the carousel.

## Layout

```
··············································································
 Points left: …                                        Trait points left: 4
··············································································
                        [ balance scale ]
 ADVANTAGES 59        ┊ DISADVANTAGES 61      ┊ APPEARANCE 69
 >[x] [+3] Fleet-Foot ┊  [ ] [-2] Bad Back    ┊  NEUTRAL
  [ ] [+1] Night Vis  ┊  [-] [-4] Hoarder     ┊  [ ]      Tail Stub
  …                   ┊  …                    ┊  HAIR STYLE
                      ┊                       ┊  [x]      Crewcut
··············································································
 :: FLEET-FOOTED
 [portrait] COST 3 points · GROUP Advantages · STATUS Available   description…
```

Three columns, each scrolling independently so a long Advantages list can never push the detail
panel off screen. Column 3 carries sub-headings — Neutral, then one per appearance type.

## Rows, and why alignment is the whole argument

Each row is four fixed-width cells: **cursor | checkbox | cost | name**. Fixed widths mean each
cell forms a column the eye can run down — you can scan prices without reading names, or find
what you already have by running down the `[x]`s. That is the trick the reference is using, and
it is why a 60-row column is more scannable than 60 cards.

| cell | width | content |
|---|---|---|
| cursor | 10dp | `>` on the cursor row |
| checkbox | 30dp | `[x]` held · `[ ]` free to take · `[-]` refused |
| cost | 44dp, right-aligned | `[+3]` / `[-2]`, empty when free |
| name | flex | trait name |

Cost colour is the valence the balance scale already uses — `COL_TR_GOOD` for an advantage,
`COL_TR_BAD` for a disadvantage — so the column and the scale cannot disagree.

Hierarchy is **brightness and tracking, never size**: every row, sub-headings included, is
exactly 22dp. That is load-bearing rather than cosmetic — C++ scrolls the cursor into view as
`scroll_height / row_count`, which is only exact while no row is taller. Same constraint the
profession equipment tree accepted, same reason.

`line-height: 22dp` against a smaller glyph is where the spacious feel comes from: a generous
rhythm without leading big enough to break the columns apart.

## Dotted rules

The reference separates every region with a dotted rule. RmlUi has **no `border-style`**, and
this repo already recorded that twice — "a flat 1dp dotted #3a3a4a border rendered as nothing
here" (`mainmenu_theme.rcss:103`, `mainmenu.rcss:59`). The working substitute is a repeating
gradient, which is what the sidebar HUD uses for its scanlines:

```
decorator: repeating-linear-gradient( 90deg, {{bg4}} 0dp, {{bg4}} 2dp, #00000000 2dp, #00000000 5dp );
```

2dp of ink, 3dp of gap, on a 1dp-high div; the same rule at `180deg` on a 1dp-wide div gives the
vertical rules between columns.

## Groups come from the mutation_type registry

Measured over `data/json/mutations/` — 571 mutations, **188** with `starting_trait`:

| column | rule | count |
|---|---|---|
| Advantages | `points > 0` | 59 |
| Disadvantages | `points < 0` | 61 |
| Appearance → Neutral | `points == 0`, not an appearance type | 3 |
| Appearance → Facial hair / Hair style / Skin colour / Eye colour / Hair colour | by type | 28 / 13 / 9 / 8 / 8 |

The appearance split is **derived, not a hand-written id list**. A `mutation_type` is an
appearance type when it declares `mandatory_one` or `swap_on_conflict`, and across all 23 type
definitions that predicate selects exactly those five and no gameplay type:

| type | mandatory_one | swap_on_conflict |
|---|---|---|
| `skin_tone` `eye_color` `hair_style` `hair_color` | true | false |
| `facial_hair` | false | true (`random_chance: 50`) |
| the other 18 (HEALING, RUNNING, TAIL, EARS, …) | false | false |

Both flags mean "you hold at most one of these", which is what makes a set a picker rather than
a list of independent choices — so the predicate is the concept, not a coincidence. A mod adding
`horn_color` with `mandatory_one` gets its own sub-heading for free.

This also fixes the old "neutral" column, which was 66 appearance pickers and 3 gameplay traits:
the one column that looked like "traits with no cost" was mostly a wardrobe.

Sub-heading names come from `mutation_type_display_name()`, already translated. It was missing a
`facial_hair` case and returned the raw id, so that string is added — the barber menu
(`npctalk_funcs.cpp:556`) already needed the same words.

`character_description.cpp:832`'s hardcoded `valid_apperance_categories` looks like the same list
minus `facial_hair` and is deliberately LEFT ALONE. It enumerates the categories that function
knows how to phrase in prose, which is a different concept from "is an appearance type"; folding
it onto this predicate would add a `facial_hair` entry nothing reads, and calling the difference
a bug would repeat the over-generalisation that produced a bogus SCENARIO "fix" on STATS.

## One gate feeds the box, the reason and the popups

The old screen encoded every refusal as the same `c_dark_gray` row, and told you *why* only as a
popup raised **after** you pressed CONFIRM on it. A `nc_trait_gate` now computes taken / locked /
mandatory / conflicts / can-swap / scenario-forbids / profession-forbids / blocking-bionics /
over-budget once, and three surfaces read it:

- the checkbox glyph (`[x]` / `[ ]` / `[-]`),
- the `STATUS` fact's sub-line, in words ("Conflicts with a trait you already have."),
- `CONFIRM`'s refusal popups, unchanged in wording and still firing for exactly their old cases.

They cannot drift, because a single struct decides.

## Point-sign, not column index

`iCurWorkingPage` was overloaded: `== 0` meant both "the good column" and "counts against the
advantages budget" (`:1803`, `:1828`). The budget check and the `num_good`/`num_bad` update now
key off `mdata.points > 0`. Same behaviour — a 0-point trait previously took the `num_bad` path
and added 0 — but correct by construction instead of by the neutral column happening to be
index 2.

## Interaction

- `LEFT`/`RIGHT` change column, `UP`/`DOWN` move within it: the mapping this tab always had, and
  the one three columns imply. No remap, unlike STATS.
- Clicking a **row** moves the cursor. Clicking the **checkbox** toggles. Toggling spends or
  refunds points and can raise a modal, so reading a trait must not be the same gesture as
  taking it — and the box is the element the metaphor already points at.
- `CONFIRM` toggles the cursor row, through the same lambda the checkbox uses.
- Headings are skipped by cursor movement in both directions.

## Kept

- The balance scale, unchanged, and `newchar_balance.h`'s six tests with it.
- `REROLL_*` returning `tab_direction::NONE` to re-enter the tab.
- The character preview, moved into the detail panel's art slot as on PROFESSION. It earns its
  place here most: taking a visible mutation changes it.

## Verified

- Builds clean. Warning counts identical to HEAD for every touched TU: `newcharacter_ui.cpp` 22
  (all pre-existing), `mutation_type.cpp` 0. `main_menu.cpp` is byte-identical to HEAD.
- `[newchar]` + `[mutations]`: 3381 assertions in 17 cases, all passing.

## NOT verified

**This screen has never been rendered.** Synthetic input does not reach `gui/mainmenu.rml` at
all — neither clicks nor keys, foreground delivery included — so the creator cannot be reached
without a temporary scaffold, and macOS window identity for this app is ambiguous enough
(`desktop.window` matched two windows for one process) that any frame captured this way has
uncertain provenance. `tools/visual_verify/vv.py` drives the installed *Windows* build.

So the observer is the user. Everything above about layout, spacing and colour is a design
argument, not a measurement.
