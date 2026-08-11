# SCENARIO step — grouped card tree

Reference: `UI_designs/13_charcreation_start_location.png`.

## Brief (as given)

- Organise scenarios **by type** — challenges, basic, advanced.
- State intent **at a glance**, using visual storytelling rather than text.
- Fix the list styling: the background colour destroys contrast, and there is no padding
  between the gold highlight at the head of a row and its text.
- Use imagery like the reference — e.g. the building the player starts life in.

## Decisions taken with the user

- **Taxonomy: Basic / Advanced / Challenge.**
- **Visuals: per-card flag icons, plus start-location art for the highlighted scenario.**
- **Layout: cards grouped by type, in a TREE with collapsible headers. Challenges
  collapsed by default, to gate them. Each group is a CAROUSEL showing ~8 at a time.**

## Grouping comes from real data, not the name prefix

Measured over `data/json/scenarios.json` (38 scenarios):

| group | rule | count |
|---|---|---|
| Basic | no `CHALLENGE` flag, `points >= 0` | 16 |
| Advanced | no `CHALLENGE` flag, `points < 0` (grants points, so harder) | 8 |
| Challenge | has the `CHALLENGE` flag | 14 |

The `CHALLENGE` flag is real and carried by 28 definitions across all data — **not** the
`"Challenge - "` name prefix. Deriving from the flag rather than the name matters twice
over: the prefix is translated (so prefix-matching breaks in every non-English locale),
and mods get grouped correctly for free without touching their JSON.

No JSON edits and no loader changes are needed.

## Visual vocabulary already exists

`set_scenario` already maps flags to meanings for its info pane, so the icon set is just
that vocabulary rendered instead of written:

| flag | meaning | icon treatment |
|---|---|---|
| `FIRE_START` | fire nearby | red |
| `INFECTED` | infected player | green |
| `BAD_DAY` | drunk and sick | green |
| `SUR_START` | zombies nearby | red |
| `HELI_CRASH` | limb wounds | red |
| `CITY_START` | urban start | grey |
| `LONE_START` | no starting NPC | blue |
| `BORDERED` | walled-in start | grey |
| `SPR/SUM/AUT/WIN/SUM_ADV_START` | season | yellow |

Icons are `?proc:runic-icon:<size>:<seed>:<hex>` with a stable per-flag seed, so each flag
keeps one recognisable glyph with zero art needed. Colour carries the valence (danger vs
circumstance) so the strip reads before any glyph is learned.

## Tree navigation without new keybindings

The group header is a **focusable row in the vertical order**, which is what a tree does
anyway:

```
[Basic ▾]          ← header focus; CONFIRM toggles collapse
  ‹ card card card … ›   ← card focus; LEFT/RIGHT move, and page the carousel at the edge
[Advanced ▾]
  ‹ card card … ›
[Challenge ▸]      ← collapsed by default
```

- `UP`/`DOWN` walk header → cards → next header, skipping the cards of a collapsed group.
- `LEFT`/`RIGHT` move within the focused group's cards; at either end they advance the
  carousel page rather than wrapping, so paging needs no extra key.
- `CONFIRM` on a header toggles collapse; on a card it selects the scenario, exactly as
  today.
- Clicks: card → focus + select, header → toggle. Requires `SELECT` registered (already
  done for the whole creator; see `plans/charcreation-wizard-flow.md` for why `ANY_INPUT`
  cannot substitute).

`cur_id` stays an index into the flat `sorted_scens`, so `SORT`, `FILTER`, `RANDOMIZE`
and `reset_scenario` keep working unchanged; the grouping is a VIEW over that list.

## Styling fixes

- The list's background block is what flattens contrast — rows sit on a lighter fill that
  removes the separation every semantic colour was designed against. Cards get a dark
  ground instead, and the container loses its fill.
- The gold cursor bar currently butts against the row text. The bar keeps its width and
  the content gains left padding, so the accent reads as an edge rather than as a
  prefix character.

## Layout revision (asked for mid-implementation)

The right-hand info pane is **gone**. Its content moved into a floaty panel beneath the
carousel, inside the open category:

```
[- BASIC  1-6 / 16]
 ‹ card card card card card card ›
        ▁                              ← notch, under the selected card
 ┌ art │ facts │ description ─────────┐
 └───────────────────────────────────┘
[+ ADVANCED 8]
[+ CHALLENGE 14]
 sigil :: meaning   × 13              ← legend, spread across the panel
```

- Only the **notch** moves as the selection changes; the panel is fixed height (164dp) and
  fixed position, so reading it while stepping through scenarios does not shift the layout.
- The notch row mirrors `.nc-band-rail`'s flex geometry exactly (gutter, six cells, gutter),
  so it lands under its card at any stage width with no pixel arithmetic anywhere.
- Dropping the pane freed the full width: six cards now get ~190dp each instead of ~60dp,
  which is what stopped names being clipped.
- Facts are per-field bindings (`loc_rml`, `prof_rml`, `veh_rml`, chips) with small dim
  labels over bright values. Previously one pre-wrapped string with embedded headers and
  blank-line separators — no stylesheet could give it hierarchy, and a third of its height
  was whitespace.
- All bands start collapsed: the first decision is *what kind of run*. Opening a band steps
  the cursor onto its first card, so the notch and panel always have a scenario to describe.

## Flag vocabulary is one table

`nc_scen_flag_icons()` carries `{flag, seed, colour, label, desc}` and feeds all three
surfaces: the card strip (glyph), the selected scenario's chips (glyph + `desc`) and the
legend (glyph + `label`). This was a real defect during implementation — the strip drew from
this table while the chip text came from a separate hand-written if-chain that omitted
`CITY_START` and `BORDERED`, so a card could show a sigil nothing on screen explained.

## Start-location art

Landed, and **not** via a second borrowed render target as phase B originally guessed.

`tileset` now records where each sprite index came from on disk (`sheet_spans`, the inverse
of the index formula in `copy_surface_to_dynamic_atlas`), and the render interface gained a
`?sprite:<x>:<y>:<w>:<h>:<path>` source that decodes the sheet, crops one sprite and uploads
it. Cheaper and safer than a render pass: no route flag, no target allocation, and no copy
pass inside RmlUi's render — the D3D12 hazard `rmlui_render_interface.cpp` already avoids.
Resolution (`scenario` → `start_location::first_target()` → sprite index → file + rect) stays
on the game side, so the render interface still knows nothing about tilesets.

`first_target()` is deliberate rather than `random_target()`: rerolling per frame would make
the art flicker.

## The click bug worth remembering

`data-event-*` installs a `DataControllerEvent` listener per generated element
(`DataControllerDefault.cpp:95`) and a `data-for` regeneration adds another **without
removing the old one**. This document dirties `bands` every redraw, so one click invoked the
callback an unbounded number of times — measured at 15. A toggle run an even number of times
cancels itself out, which is exactly why headers would not open.

Neither the event phase nor the target element can filter this: a click on a header's label
span legitimately arrives bubbled with a valid target. So **click callbacks here record
intent and mutate nothing**; the loop applies it once per input cycle, mirroring the existing
`nc_nav` idiom. Any future `data-event` callback on this screen must follow that shape.

## Verified

- Three groups from the `CHALLENGE` flag and point cost; all collapsed on entry.
- Header click toggles (clicked the word "BASIC", i.e. the bubbled span case).
- Card click focuses + selects: notch moves, facts and description update, panel stays put.
- Pager click pages the carousel (`1-6 / 16` → `7-12 / 16`).
- Art slot shows real location art (Evac Shelter, Radio Tower) cropped from the tileset.
- Chips match the card strip exactly (Armored Apocalypse: 3 sigils, 3 chips).
- `[newchar]` passes (133 assertions, 6 cases).

## Sprite decode cost

`?sprite:` decodes a multi-megabyte tileset sheet, so the interface keeps the most recently
decoded sheet (one entry — consecutive crops almost always share a sheet, and holding more
would pin tens of megabytes). Measured while browsing: 3 distinct sprites → 3 crops → **2**
sheet decodes, and revisiting an already-seen scenario costs nothing because RmlUi caches
textures by source string. So decodes are bounded by distinct *sheets touched*, not by
selections made.

The cached sheet is released 240 idle frames after the last crop, so visiting character
creation once does not pin the surface for the rest of the session.

## Not done

- `SORT` / `FILTER` / `RANDOMIZE` and keyboard tree navigation are unverified: synthetic
  keyboard input does not reach this SDL build, and all three are keyboard-only. They share
  `recalc_scens` and `sync_cur_from_focus` with the mouse paths that were verified.
- The 13 runic sigils are still procedural placeholders awaiting real art.
- **The all-collapsed entry state leaves most of the panel empty.** Captured and confirmed:
  three headers at the top, legend at the bottom, void between. That is the direct
  consequence of "collapse the categories by default" and is left as the user specified;
  the obvious fix (centre the cluster while everything is collapsed) trades the void for a
  layout jump the moment a band opens, so it is a design call rather than a defect to
  quietly patch.
