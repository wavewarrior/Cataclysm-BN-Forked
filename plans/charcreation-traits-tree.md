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

They cannot drift, because a single struct decides — and that struct's precedence lives in
`src/newchar_trait_gate.h` as pure data-in/data-out, so the branchiness is reachable from tests
without an avatar. Two of its rules are NEW behaviour rather than a port, which is why they are
pinned there rather than trusted:

- `over_budget` keys off the **point sign**, where the old code keyed off the cursor's column
  index. The regression it guards is specific: keyed off a column, a 0-point trait fell into the
  disadvantage branch because it was "not an advantage".
- `can_swap` did not exist. A conflicting trait whose type declares `swap_on_conflict` is a
  *replacement*, not a refusal — but only when the avatar actually holds one of that type to give
  up. `swaps` alone is not permission.

## The DNA strand

A double helix in a narrow full-height column beside the lists, turning slowly, with one dot pair
lit per trait taken — so the genome visibly fills in as the character is built. Geometry is pure
trigonometry in `src/newchar_dna.h`.

Nothing rotates. A double helix seen side-on is two sine waves half a turn apart, so spinning it
about its vertical axis is just advancing a shared phase — no `transform`, and in particular no
`position: absolute`, which RmlUi resolves against `.nc-panel` in this document (the trap that put
the balance scale's beam through the top bar). Each rung is laid out in pure flow as
`[gap][dot][bond][dot]`, and C++ writes only the gap and bond widths.

Both backbones come from **one** sine (`a = 0.5 + 0.5·sin t`, `b = 0.5 − 0.5·sin t`) so they cannot
drift out of antiphase; `cos` of the same angle decides which dot is nearer the viewer and
therefore which is bright. Tests pin the antiphase invariant, that the bond width is never negative
(the backbones cross twice per turn, and a negative width would be a parse error every frame), and
that `span_of` reports the depth of the dot it actually put on the left.

Marked pairs take the valence colours the cost tags and the balance scale already use — green
advantage, red disadvantage, gold appearance — and beat `.front` on specificity so a lit pair keeps
its colour through the whole turn instead of flickering with depth.

Rungs are `flex: 1` inside a `flex: 1` column, so 22 of them divide whatever height exists and the
strand rescales with the window with no pixel arithmetic.

### Animating a screen whose input loop blocks

`rml_doc::open()` already sets a 16 ms poll on the step's input context (`rml_screen.cpp:78`) so
RmlUi hover and wheel stay live between keys — so the strand animates for free at ~60fps. An
earlier draft added `ctxt.set_timeout( 33 )` after the main menu's plexus idiom, which on this
screen only *downgraded* that tick; it was removed.

Phase comes from `steady_clock`, not a frame counter, so the spin neither speeds up while a key is
held nor stalls when the player stops typing.

The model is rebuilt only when something changed; the strand every frame. Without that split a
quiet animation tick would re-colour 188 rows and force RmlUi to regenerate 188 `data-for` elements
thirty times a second to move some dots.

**The dirty test must include the click intents, not just the action.** cata maps `MOUSE_LEFT` to
`SELECT` on mouse *down*, while RmlUi fires its `click` — and therefore the `data-event` callbacks
— on mouse *up*, so the loop iteration that carries a click's intent is usually a `"TIMEOUT"` one.
Gating on `action != "TIMEOUT"` alone applied every mouse toggle and rendered none of it: the
checkbox, the row highlight, the points total, the balance scale and the detail panel would all
keep showing the pre-click state. This is the kind of defect that only exists because the screen
is gated at all — the other seven steps call `sync_rml()` unconditionally.

### It rendered nothing at first, silently

The type was registered, the array filled and `DirtyVariable` called every frame — but
`c.Bind( "dna", … )` was never written. A `data-for` over an unbound name renders **nothing**, with
no warning at all: the `GENOME` heading appeared with an empty count over blank space while the
producer ran happily. "Dirty every new binding" has a sibling — *bind* it — and a heading that
renders while its list does not is the signature.

## Appearance column

### Prefixes stripped

The sub-heading already says `EYE COLOR`, so `Eye color: amber` repeated it. Not done by
prefix-matching the translated name — that is the mistake the SCENARIO grouping exists to avoid. In
order: the authored `apperance_description` (present for all 8 eye colours, 8 hair colours, 13 hair
styles and 8 of 11 skin tones, and *better* — it says "afro" where the name says "'fro", "very light
peach" where the name says "lighter"); else a split at the first `": "`, which is punctuation rather
than a word, for the 28 facial-hair traits that declare none; else the full name, which is what a
locale writing no colon keeps. A worse label, never a wrong one.

### Hovering — or cursoring — an option previews it on the portrait

So a hair style can be judged before a click is spent. The option is worn for the duration of ONE
portrait draw and removed again.

Safe against the point budget for a reason **independent of the trait's cost**:
`Character::toggle_trait` does not know the creator's budget exists. `points.trait_points`,
`num_good` and `num_bad` are written only by the local `toggle_trait_at` lambda, which this bypasses.
An earlier comment justified it with "appearance traits are all `points == 0`" — a premise this very
file rejects, since `nc_classify_trait` deliberately puts a modded hair style *with* a point cost in
this column. The wrong reason invites the wrong fix: route the swap through `toggle_trait_at` on
meeting such a trait and it charges the budget per frame, which is real corruption.

Scoped to the draw rather than held between frames. Holding it is cheaper, and was tried, but it
needs three separate rules to stay correct — each of which turned up as a live bug:

| holding the preview | what broke |
|---|---|
| a click landing while applied | `gate_of` sees `taken == true` for the option under the pointer, takes the DROP branch, and removes it while charging the budget for a drop that never happened — the *primary* interaction on this column |
| `sync_rml` running while applied | rows are built from `u.has_trait`, so the previewed row renders `[x]` in gold with STATUS "Taken" for an option nobody owns, on every arrow-key step |
| a reroll while applied | `u.randomize` replaces the character wholesale, and the pending un-swap would then toggle traits on a character that never had them |

Draw-scoping deletes all three by construction. It costs four cosmetic `toggle_trait` calls per
drawn frame while a preview is active, which is the trade taken deliberately.

### Two input traps behind "the hover feels slow"

**Mouse motion did not wake the loop at all.** Motion resolves to `CATA_ERROR`, and
`input.cpp:894-897` `continue`s on an unrecognised MOUSE event *without returning*; because each
pass restarts the wait, a moving pointer starves the timeout indefinitely, so `handle_input` only
returned once the pointer stopped — and that is when the preview caught up. Fixing it needs BOTH
`register_action( "COORDINATE" )`, which sets `handling_coordinate_input` and skips that early
`continue`, and `register_action( "ANY_INPUT" )`, which makes the fall-through at `:912` return.
Either alone still parks the loop. `ANY_INPUT` then has to be excluded from the model-dirty test, or
every pointer sample rebuilds 188 rows.

**`mouseout` bubbles** (`EventSpecification.cpp:16` declares `bubbles=true`), so stepping from one
row to the next fires it on the row being left and it arrives at the LIST handler too. Honouring
that made `hover_flat` oscillate `row → -1 → row`, and since -1 falls back to the cursor row the
portrait visibly alternated between the option worn and the option under the pointer. The handler
now reacts only when `GetTargetElement() == GetCurrentElement()`. `mouseover` is deliberately NOT
filtered the same way: entering one of the row's child spans arrives bubbled with the row as current
element, which is correct — the spans are the row's whole area — and it reports the same index.

**Keyboard navigation has to reclaim the preview.** `hover_flat` persists until the pointer leaves
the list and hover takes priority, so with the pointer resting over APPEARANCE the cursor moved while
the portrait stayed put. `UP`/`DOWN`/`LEFT`/`RIGHT`/`RANDOMIZE` clear it, after the hover intent is
applied so a key wins even when motion arrived in the same poll.

### Portrait box 108dp → 168dp

Panel 132 → 196dp with it. The portrait is a `scale-none` window into a 512px render target, so this
box does not resize the character — it decides how much of the target is visible, and at 216 physical
px the avatar reached the edges and read as cropped. To shrink the CHARACTER instead, zoom is the
control: `character_preview` defaults to `DEFAULT_ZOOM == MAX_ZOOM` (128), and one `zoom_out()` in
`nc_prepare_preview` would halve it — deliberately not done, because that function is shared by all
four preview-bearing tabs.

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
  (all pre-existing `-Wunused-macros`), `mutation_type.cpp` 0, and 0 from any new header.
  `main_menu.cpp` is byte-identical to HEAD.
- `[trait_gate]` + `[dna]` + `[newchar]` + `[mutations]` + `[character]`: 9723 assertions in 31
  cases, all passing.
- The row layout, the checkbox/cost gutters, the gold sub-headings, `[-]` plus dimming on
  unavailable rows and the `:: NAME` detail panel were confirmed on screen by the user.

## NOT verified

The author never rendered this screen. Synthetic input does not reach `gui/mainmenu.rml` at all —
neither clicks nor keys, foreground delivery included — so the creator cannot be reached without a
temporary scaffold, and macOS window identity for this app is ambiguous enough (`desktop.window`
matched two windows for one process) that any frame captured that way has uncertain provenance.
`tools/visual_verify/vv.py` drives the installed *Windows* build.

So the user is the observer, and it shows in where the bugs came from: **six defects in this work
were found by review or by the user looking, none by a test.** The missing `dna` bind, the
click-drops-instead-of-takes ordering bug, `sync_rml` rendering `[x]` for unowned options, the
bubbling `mouseout`, the keyboard-vs-hover priority, and the `set_timeout( 33 )` that downgraded an
existing 16ms tick. Every one is invisible to a compiler and to the pure-function tests, which is the
honest measure of what this screen's test coverage does and does not buy.

Still unconfirmed at time of writing: that the strand actually turns and its lit pairs match the
taken traits, that hover tracks without alternating, that arrow keys move the preview with the
pointer at rest over the list, and that clicking a hovered option takes rather than drops it.
