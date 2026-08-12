# PROFESSION step — grouped card tree

Applies the layout philosophy settled on the SCENARIO tab
(`plans/charcreation-scenario-tree.md`) to the next step in the wizard. Read that file first:
the click-intent rule, the notch geometry and the centring safety argument all carry over
unchanged, and re-deriving them here would only invite divergence.

## Why this tab needs it more than SCENARIO did

258 professions against 38 scenarios. The current screen is one flat scrolling list beside a
single large text buffer — the exact shape the SCENARIO work replaced, at seven times the
length.

## Taxonomy comes from real data

Professions carry no category field, and cost bands would produce three groups of ~85 — no
better than the flat list. But every skill declares a `display_category`
(`skills.json`, 28/28), and `skill_display_type` gives it a human name:

| id | display_string |
|---|---|
| `display_melee` | Melee skills |
| `display_ranged` | Ranged skills |
| `display_crafting` | Crafting skills |
| `display_interaction` | Interaction skills |
| `display_social` | Social skills |

So a profession is grouped by the `display_category` of its **highest-level** skill. Measured
over `data/json/professions.json` (258 professions):

| group | count | carousel pages |
|---|---|---|
| Interaction | 86 | 15 |
| Crafting | 52 | 9 |
| Unskilled (no skills) | 35 | 6 |
| Ranged | 33 | 6 |
| Melee | 30 | 5 |
| Social | 21 | 4 |

Derived, not hand-mapped: a mod's new skill declares its own `display_category`, and a mod's
new profession lands in the right group without touching any of this. Group names come from
`SkillDisplayType::get_skill_type( id ).display_string()`, so they are translated already.

`skill.cpp:177` provides the lookup; `skill.h:76` the accessor.

## Sigil vocabulary from the JSON fields that matter

The scenario strip encodes flags; the profession strip encodes **what a profession hands you**,
which is what the player is actually comparing. All from measured field frequency:

| field | count | reads as |
|---|---|---|
| `traits` | 154 | locked traits |
| `CBMs` | 58 | starts bionic |
| `vehicle` | 58 | starts with a vehicle |
| `starting_cash` | 38 | starts with money |
| `pets` | 34 | starts with an animal |
| `npcs` | 14 | starts with a companion |
| `addictions` | 12 | starts addicted |
| `missions` | 8 | starts on a mission |

Colour carries valence exactly as on the SCENARIO tab: a liability (addiction) is red, a gift
(vehicle, cash, companion) is green/blue, a neutral fact (traits) is grey. One table feeds the
card strip, the selected profession's chips and the legend — the SCENARIO tab proved that
splitting those three across two sources silently drops a sigil.

## The art slot is already there

SCENARIO needed new machinery to show start-location art. PROFESSION does not: the character
portrait already renders through RmlUi as a decorator (`?avatar:<gen>`, from the wizard-flow
work) **and it already wears the selected profession's starting clothes**. It moves from its
absolute top-right corner into the info panel's art slot, so this tab reaches the same
`art | facts | prose` panel as SCENARIO with no new render path.

## Facts, not a buffer

The current right-hand pane is one pre-wrapped string carrying addictions, traits, skills,
items (wielded/worn/inventory), bionics, pets, vehicle, spells, cash and companions. That
cannot fit a fixed-height panel and cannot be given hierarchy.

Split: the decision-relevant facts become label/value fields (top skills, locked traits,
gear count, cash, vehicle), and the exhaustive detail stays as prose in the scrollable
column. The chips already answer "what does this hand me" at a glance.

## Carried over verbatim from SCENARIO

- **Click callbacks record intent; the loop applies it once.** `data-event-*` accumulates one
  listener per `data-for` regeneration, so a handler that mutates directly self-cancels.
- **Notch row mirrors the rail's flex geometry** (gutter, six cells, gutter) so it aligns with
  no pixel arithmetic.
- **`justify-content: center` is safe** — RmlUi guards main-axis alignment behind
  `remaining_free_space > 0`, degrading to flex-start on overflow.
- **Transitions only, inside each rule.** No entry keyframes: `data-for` regeneration would
  restart them every frame.
- **Open the group holding the already-selected profession on first build**, then never again.

## Shared vocabulary was promoted, not copied

The tree, its floaty panel and the legend now live in `newchar_common.rcss` rather than in
either tab's sheet, and the scenario-specific class names were renamed onto the shared set
(`nc-scen-card` → `nc-card`, `nc-scen-tree` → `nc-tree`, and so on). Two copies of one visual
language diverge; these two screens are deliberately the same shape. Each tab's own sheet keeps
only its frame and status rows.

Verified behaviour-neutral: the SCENARIO tab was re-captured after the move and is visually
identical.

## One pick path, not two

A card click now commits the profession by calling the same lambda `CONFIRM` uses, rather than
only moving the cursor. Before, clicking a card on SCENARIO selected the scenario while clicking
a card on PROFESSION merely previewed it — two adjacent steps disagreeing about what a click
means. The lambda early-returns when the clicked profession is already in force, because
toggling its locked traits off and back on is not a no-op.

## What landed

- Six bands from `display_category` of the top skill, plus Unskilled; empty bands are dropped,
  so the set follows the loaded mods.
- Collapse state is preserved across a re-sort **by category, not by index**: FILTER can remove
  a whole band and shift every later one.
- Band headers use `display_string()` verbatim ("Melee skills"). Slightly long, but stripping a
  " skills" suffix would be prefix-matching a localised string — the exact mistake the scenario
  grouping avoids by deriving from the `CHALLENGE` flag rather than the "Challenge - " name.
- The prose buffer no longer repeats Traits and Skills: the fact fields own them, and stating
  them twice in one panel is noise. It keeps the itemised detail no summary can carry.
- `starting_cash` of zero no longer earns a "Starts with money" sigil.

## Verified

- Entry opens the band holding the already-selected profession, cursor on it, centred.
- Header click toggles; card click commits (chosen border moves, points recomputed, portrait
  re-kitted); pager pages (`1-6 / 18` → `7-12 / 18`).
- Portrait renders in the art slot and changes with the selection.
- Chips agree with the card strip; legend covers all nine sigils.
- `[newchar]` passes; SCENARIO tab unaffected by the stylesheet move.

## Expand/collapse all

A bordered control sits above the first group, right-aligned over the carousel's own edge:
`- COLLAPSE ALL [E]` when any group is open, `+ EXPAND ALL [E]` when none is. The label states
what a click WILL do rather than the current state — a control named after its own condition
makes the reader work out the consequence.

It is one action, `TOGGLE_ALL_GROUPS`, shared by the control and the key, so the shortcut the
label prints comes from `input_context::get_desc` and follows a rebind. Bound to `e`, verified
free across GLOBAL and both creator categories before taking it.

Collapsing parks the cursor on the focused group's header, because the card it stood on is gone;
expanding steps back onto a card so the notch and panel have something to describe. The control
lives in the shared vocabulary, so both steps got it from one definition.

First attempt was too quiet to be a button — dim, unbordered, indistinguishable from the
captions around it. It now carries a border that goes gold on hover.

## Not done

- `SORT` / `FILTER` / `RANDOMIZE` / `CHANGE_GENDER` and keyboard tree navigation are unverified:
  synthetic keyboard input does not reach this SDL build and all are keyboard-only. They share
  `recalc_profs` and `sync_cur_from_focus` with the verified mouse paths.
- The nine profession sigils are procedural placeholders awaiting real art, as on SCENARIO.

## Acceptance

- Six groups from `display_category` of the top skill, plus Unskilled; only the group holding
  the current profession open on entry.
- Carousel pages within a group; cursor never lands on a hidden card.
- Sigil strip on every card, chips for the selected one, legend at the bottom — all three from
  one table, verified to agree on a profession carrying several.
- Portrait renders inside the panel's art slot and tracks the selected profession's kit.
- Header, card and pager clicks verified with REAL mouse events.
- `SORT` / `FILTER` / `RANDOMIZE` / `CHANGE_GENDER` still work.
