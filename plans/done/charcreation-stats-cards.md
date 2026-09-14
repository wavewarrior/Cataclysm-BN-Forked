# STATS step — stat cards, pip meters, tracking info panel

Applies the creator chooser philosophy settled on SCENARIO
(`plans/charcreation-scenario-tree.md`) and PROFESSION
(`plans/charcreation-profession-tree.md`) to the fourth step. Read those first: the
click-intent rule, the notch geometry and the centring safety argument all carry over
unchanged.

## Why this step needs a different shape than the other two

Four items, not 258. A collapsible band tree with carousel pagers over four stats would be
pure ceremony — the taxonomy work that makes SCENARIO and PROFESSION legible has nothing to
group here.

What *does* carry over is everything else, and this step is arguably the worst offender
against it:

| pattern rule | STATS before |
|---|---|
| cards, not a flat list | four `label: value` rows in a 320dp gutter |
| fixed `art \| facts \| prose` panel | one right-hand pane holding a pre-wrapped colour-tagged string |
| facts are per-field bindings | `nc_stat_desc` joins five lines with `\n` and a blank-line separator |
| state at a glance, visually | the value as a two-digit number; nothing shows where it sits in the range |
| one pick path incl. mouse | keyboard-only: `LEFT`/`RIGHT` were the ONLY way to change a stat |
| centred, subtle motion | top-aligned, static |

So: same panel, same notch, same legend, same motion budget — a **single row of four cards**
in place of the band tree.

## The visual channel is magnitude, not sigils

SCENARIO encodes flags and PROFESSION encodes gifts, both as a sigil strip. A stat has no
flags; what the player is actually comparing is **how far along the range each stat sits, and
whether the next point is cheap or expensive**. That is a meter, so each card carries one:

| pip range | meaning | colour |
|---|---|---|
| 1–4 | granted, cannot be sold back (`set_stats` refuses to decrement at 4) | `{{bg4}}` |
| 5–`HIGH_STAT` (14) | one point each | `{{yellow}}` |
| 15–`MAX_STAT` (20) | two points each | `{{red}}` |
| unfilled | — | `{{bg1}}` |

Red for the double-cost band is not invention: it is the colour the step already used for
`"Increasing Str further costs 2 points."` (`c_light_red`), now carried by the thing it
describes instead of by a warning line that only appeared once you were already past the
threshold.

Filled pips are 12dp tall and unfilled 6dp, both animated, so the meter reads in greyscale
and at a glance — colour is the second channel, not the only one.

## Boundaries live in a pure header, because they are off-by-one bait

`src/newchar_stat_meter.h`, mirroring `newchar_balance.h`: `pip_tier`, `next_cost`, `refund`.

`HIGH_STAT` is the value **at** which the next increment starts costing two, so the first
pip that costs two is `HIGH_STAT + 1` while the pip that costs one goes up to `HIGH_STAT`
inclusive. Getting that wrong shifts the whole red band by one and nothing on screen would
look obviously broken. Tests pin the three boundaries (4/5, 14/15, 20/cap).

Extracting `next_cost`/`refund` also collapses the loop's eight near-identical arms (four
stats × two directions, each restating the `HIGH_STAT` double-cost rule) into one `stat_ref`
accessor plus two lambdas. Same arithmetic, verified against the original branch by branch.

## Facts, not a buffer

`nc_stat_desc` built one string: three colour-tagged effect lines, a blank line, then the
stat's blurb. Split as the pattern requires — effects become label/value fact fields, the
blurb becomes the prose column:

| stat | facts |
|---|---|
| Strength | Base HP, Carry weight, Melee damage |
| Dexterity | Melee to-hit, Throw penalty, Ranged penalty (when non-zero) |
| Intelligence | Read times, Skill rust, Crafting bonus |
| Perception | Aiming penalty (when non-zero), Night vision |

The HIGH_STAT warning becomes the **first** fact — `NEXT POINT · 1 point / 2 points /
Maxed`, with the threshold as its sub-line. It is a fact about the decision in front of you,
so it belongs where the other facts are and it is present at every value rather than
appearing only after you cross the line.

Fact labels are translated and differ per stat, so they are bound from C++ rather than
written into the markup. (SCENARIO and PROFESSION hardcode their labels in the RML, which is
a translation hole; this step does not copy it.)

Values keep their existing `COL_STAT_BONUS`/`COL_STAT_PENALTY` colouring — the baked inline
colour beats `.nc-fact-value`, which is the documented behaviour and here the intended one:
a penalty should read red.

## Mouse parity

Each card carries `−` and `+` controls, dimmed at the bounds. This is the first time the step
can be driven with a mouse at all. They record intent and the loop applies it through the same
`dec_stat`/`inc_stat` lambdas `LEFT`/`RIGHT` use — one path, per the pattern's rule 9.

A click on `+` bubbles to the card and therefore also moves the cursor to that stat, which is
what you want: adjusting a stat should select it.

## Geometry reuse

- The card rail flanks its cards with two `.nc-info-gutter` divs (18dp) instead of the
  pagers the other tabs use there. Same width, so the notch row's gutter/cells/gutter mirror
  still lands each notch under its card with no arithmetic — and no dead pager glyphs
  implying a second page that does not exist.
- `.nc-stat-card` overrides `.nc-card`'s 96dp to 108dp (name row, a value row flanked by its
  steppers, the meter). Per-tab sheets are linked after `newchar_common.rcss`, so a
  same-specificity override wins. The first pass at 138dp with the steppers in a row of their
  own left a 40dp void between the number and the meter — a block's text sits at the TOP of
  whatever height `flex: 1` hands it — and put the two controls at the card's outer edges,
  where they read as unrelated to the number between them.
- `.nc-stat` is removed from the shared cursor-row selector list in `newchar_common.rcss`:
  the stats tab was its only user and it no longer has rows.

## Legend

Three entries, all derived from the constants (`Granted (first 4)`, `1 point each`,
`2 points each (above 14)`), each with a real pip as its swatch rather than a runic sigil —
the legend explains a meter, so it should show the meter's own vocabulary.

## Keybinding hints

The seven-line hints block in the left gutter becomes one status line beside the points total.
The step rail, navigators and exit line already state the rest.

Every key comes from `input_context::get_desc` so it survives a rebind, but ONE key each and a
named one where there is one. The cardinals carry five bindings apiece (`h`, LEFT, `4`,
NUMPAD_4, JOY_LEFT), and unfiltered that rendered `h, LEFT, 4 or NUMPAD_4 / l, RIGHT, 6 or
NUMPAD_6 select · …` right across the top bar. `disallow_lower_case` alone was not enough —
UP's second binding is the digit `8`, so the pair read `DOWN / 8`. Filtering to keycodes above
0x7F leaves the arrow, with a fallback to the unfiltered first key so a cardinal bound only to
a letter is still printed rather than reported "Disabled".

## Two things found by running it

**The prose column rendered nothing.** `.nc-info-desc` is `display: flex` (PROFESSION made it
one so it could stack a blurb over a detail buffer), and the flex path has no inline handling:
`FlexFormattingContext.cpp:249-271` turns EVERY child into a blockified flex item
(`BuildBoxMode::UnalignedBlock`), skipping only `display: none` and out-of-flow ones, whereas
`BlockFormattingContext.cpp:71-74,254` dispatches on `display` and routes inline-level children
into an `InlineContainer`. So a bare text node under a flex container becomes a block-level item
with no inline context to lay out its glyphs, and renders nothing at all — silently, with no log
warning. Wrapping the text in a `.nc-info-blurb` child, which is what PROFESSION already does,
fixes it.

The discriminator is block-vs-inline, **not** element-vs-text: a child `<span>` is also
blockified, but being a real element it runs its own block context inside, which lays its text
out normally. That is why SCENARIO — whose `desc_rml` is `colorize( …, c_green )` in both
branches of `set_scenario`, so always an inline `<span>` — was rendering fine and was left
alone. STATS' blurb is uncolored plain text, which is exactly the case that breaks.

An earlier draft of this plan claimed SCENARIO had been silently broken too and "fixed" it. It
had not; the claim came from generalising one measurement to a shape that differs in the way
that actually matters.

**The empty half of the meter vanished on the focused card.** Unfilled pips were `{{bg1}}`,
which is exactly `.nc-card.selected`'s own fill, so the selected stat appeared to have a meter
one third the width of its neighbours'. Unfilled pips are `{{bg2}}`, which reads on both the
card's normal `{{bg-hard}}` ground and the selected `{{bg1}}` one.

## Verified

- Build: `cataclysm-bn-tiles` + `cata_test-tiles`. Warning set byte-identical to HEAD's
  (22 in this TU: 21 pre-existing `-Wunused-macros`, one pre-existing `-Wunused-but-set`),
  attributed by recompiling HEAD's version of the TU with its own recorded flags.
- `[newchar]` passes: 163 assertions in 11 cases, up from 133 in 6.
- In the installed macOS build: four cards, meter filled to each value with the red band past
  14, notch under the selected card, the panel fixed while stepping across stats, facts and
  prose tracking the selection, legend derived from the constants. `−`/`+` and card clicks
  confirmed to select and to adjust the value and the points total.

## Not done

- `RANDOMIZE` and keyboard `UP`/`DOWN`/`LEFT`/`RIGHT` are unverified in-game: synthetic
  keyboard input does not reach this SDL build. They share `dec_stat`/`inc_stat` and `sel`
  with the verified mouse paths.
- The four stat sigils are procedural placeholders awaiting real art, as on the other tabs.
