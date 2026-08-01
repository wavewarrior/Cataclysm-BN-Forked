# 01 — Corner Triad

## Thesis

This design argues that a survival roguelike HUD should be **four typographic
clusters pinned to the four screen corners and nothing else** — no panels, no
plates, no boxes, no frame around any group, and above all no rectangle
between the player and the thing that is about to kill them. The play area is
the instrument; the HUD is marginalia. Every grouping cue is Gestalt proximity
and alignment rather than enclosure (research-infodesign.md §4: *choose ONE
binding per section*), which is why the design can carry the full fixture at
**9.07% occlusion against the shipping HUD's 31.0%** and still leave a
**1111 x 1080 px band down the middle of the screen with literally zero HUD
pixels in it**. What it trades away is the thing enclosure buys you for free:
**contrast**. A panel guarantees its own background; a corner cluster is text
lying directly on 32px terrain tiles that range from `#1a1a1a` night pavement
to `#8fa860` lit grass in the space of one glyph. The first version of this
file paid that bill in the wrong currency — it used a 9px label tier at
luminance 104 and trusted a 1px outline — and the result was a HUD that read
as half-empty: the log, the minimap and every label were invisible. The
version in this file pays it with two devices that are both **edgeless**, so
the thesis survives intact:

1. a **per-glyph dark casing** (1px hard ring + two soft black glows), which
   is glyph-shaped and therefore costs almost no area; and
2. six **soft pools** — radial gradients that reach alpha 0 exactly on their
   own ellipse, so they have no boundary anywhere and are not panels.

The second trade is real and permanent: this design has **no reserved space**.
Nothing here reflows, wraps or scrolls, so every field is laid out for the
fixture's exact string lengths and a longer string overhangs into terrain
rather than into chrome.

## Register

Neutral-luminance ladder plus **one** accent hue (20°). Colour is never the
only carrier of anything: every state that matters is also encoded by
luminance step, position, glyph width or a 2px mark, so the whole HUD survives
grayscale (research-infodesign.md §8).

### Palette

| token | hex | L | role |
|---|---|---|---|
| `d01n0` | `#f4f8f6` | 246 | primary value — the number you came to read |
| `d01n1` | `#dfe6e2` | 228 | secondary value (name, place) |
| `d01n2` | `#b8c3be` | 193 | label tier, cluster headers, neutral log row |
| `d01n3` | `#8e9c96` | 154 | ambient: timestamps, disabled slot, rosette |
| `d01a1` | `#f2bf9a` | 195 | caution — needs, aged bar fill, POI, newest log |
| `d01a2` | `#e08a5c` | 154 | harm, one step back (dimmer bad rows) |
| `d01a0` | `#ff8347` | 154 | harm, fresh — critical limb, target, tracking |
| pool ink | `rgba(3,6,5,α)` | 5 | the six scrims; α 0.80 → 0 across the ellipse |
| casing | `#000000` | 0 | the 1px ring + two glows under every glyph |

The whole ladder sits **~40 L above** the version this replaces
(`#5e6a66`/`#93a09b`/`#c6cfcb` → `#8e9c96`/`#b8c3be`/`#dfe6e2`). The old
label tier at L104–L157 was at or *below* the luminance of the lit grass in
`_shared/world.png`, which is what made the HUD read as half-empty. Steps are
≥30 L apart so the ladder is still four distinct greys in monochrome. `a1`,
`a2` and `a0` are one hue at three saturations, so they hold their luminance
order (195 / 154 / 154) rather than their hue when desaturated — `a2` and `a0`
are separated by saturation and by *position* (a2 never appears next to a0 in
the same row), not by brightness.

### Type

One face: `Consolas` as the contract's metric stand-in for Source Code Pro.
Five tiers, and the smallest is **11px** — the floor named in
research-infodesign.md ("0.8em monospace = ~11px; line-height 1.2×"). There is
no 9px text in this file; the previous 9px label tier was below the floor and
is the single largest cause of the defect this revision fixes.

| tier | class | size / line | tracking | weight | used by |
|---|---|---|---|---|---|
| label | `.d01u` | 11 / 13 | 1.8px | 700 | cluster headers, part labels, needs, hotbar labels |
| dense | `.d01u2` | 11 / 13 | 0.4px | 700 | `/max` denominators, STR/DEX/INT/PER, SPD/FOC/MRL, timestamps |
| body | `.d01l` | 12 / 15 | 0.6px | 400 | weather, light, target lines, weapon lines |
| log | `.d01b` | 13 / 17 | 0.15px | 400 | log rows, hotbar keys, target HP % |
| head | `.d01m` | 17 / 20 | 1.2px | 400 | name, target name, weapon name |
| value | `.d01v` | 23 / 26 | 0.5px | 400 | the six body-part current values |
| clock | `.d01xl` | 38 / 42 | 2px | 400 | `21:47` |

The `/max` denominators are **bold 11px, not 9px light**, and they sit on the
same baseline as the 23px current value with a 3px gap, so `8 /30` reads as one
number pair in one fixation instead of two disconnected marks.

### Border / frame treatment

**There is none, and that is the design.** No element in this file has a
`border`, a `background-color`, or a `border-radius` box. Every rectangle in
the document is ≤2px in its minor dimension and is carrying data:

| mark | size | job |
|---|---|---|
| edge rail | 1px wide, 5 segments, gradient-faded at both ends | an index of *where* the clusters are; the gaps are the map |
| registration tick | 1px, 14px long, 8 of them | corner datum, 24px inset |
| sparkline track/fill | 2px tall | capacity vs value |
| critical flag | 2px wide, 64px tall, `a0` | beside the failing limb only |
| log recency rule | 2px wide, 15px tall, ×6 | age by luminance, newest at 100% |
| hotbar strike | 1px | the one unavailable slot |
| playhead notch | 1px × 16px at 85% | rail brightens beside the newest message |
| rosette dots | 2px, plus one 4px `a0` circle | the eight compass bearings; the only mark in the file wider than 2px, and it is round, not a rectangle |

The contrast substrate is the two edgeless devices described in the Thesis. The
pools are `radial-gradient(ellipse 50% 50% at 50% 50%, …)` — the gradient's
last stop is alpha 0 and it lands **on** the ellipse, half the box on each
axis, so the box corners are never painted and there is no boundary to see at
any alpha. Six of them, only under the blocks that the casing alone could not
carry.

## Layout

Measured in Chromium at 1920x1080, from the rendered file.

| region | x | y | w | h | anchor |
|---|---|---|---|---|---|
| cluster 0 — SITUATION | 56 | 40 | 302 | 327 | top-left, 56px inset |
| cluster 1 — CONTACT | 1627 | 40 | 239 | 143 | top-right, optical right edge on x=1864 |
| cluster 2 — CONDITION + LOG | 44 | 574 | 375 | 499 | bottom-left, baseline of last log row on y=1071 |
| cluster 3 — LOADOUT | 1570 | 893 | 296 | 166 | bottom-right |
| rails | 40 / 1880 | 5 segments | 1 | 136–369 | screen edge |
| ticks | 24 / 1882 / 1895 | 24 / 1042 / 1055 | 1–14 | 1–14 | 24px corner inset |

Cluster 2 starts at x=44 rather than 56 because the critical flag and the six
log recency rules hang 12px into the margin — they are the only things in the
design that are allowed to break the 56px inset, which is exactly why they read
as alarms rather than as content.

Inside cluster 2, the body-part grid is laid out **anatomically**, not as a
list: `HEAD` alone on the top row centred over the torso column, then
`L·ARM | TORSO | R·ARM`, then `L·LEG | ___ | R·LEG`. Column pitch 88px, row
pitch 70px, 66px sparkline per cell. The left arm is on the left. That costs
one empty grid cell and buys a body map you can read positionally, without
reading a single label.

### The six pools

| # | block it serves | x | y | w | h | ellipse area |
|---|---|---|---|---|---|---|
| 1 | overmap + `MISSION NE` | 28 | 178 | 199 | 215 | 33,603 |
| 2 | CONTACT | 1625 | 22 | 255 | 181 | 36,250 |
| 3 | body-part grid | 38 | 578 | 278 | 223 | 48,690 |
| 4 | effects / needs / stamina / stats | 38 | 785 | 276 | 150 | 32,516 |
| 5 | LOG | 38 | 918 | 399 | 158 | 49,513 |
| 6 | LOADOUT + hotbar | 1548 | 874 | 336 | 200 | 52,779 |

Ellipse area is `π/4 · w · h`. Pools 3/4 and 4/5 abut in a 20px band where both
are already near alpha 0, so there is no seam.

**Cluster 0 deliberately has no pool.** Its type is the largest and brightest
in the design (38px clock at L246, 17px name at L228, nothing below 11px bold at
L193) and the per-glyph casing carries it unaided over both lit grass and the
building's pale concrete — verified by render. That is the control that proves
the casing is doing real work and the pools are not a panel system in disguise.

### The protected centre

Left-most right-side paint is pool 6 at x=1548; right-most left-side paint is
pool 5 at x=437. The band **x = 437 → 1548, y = 0 → 1080** — 1,199,880 px²,
**57.9% of the screen** — contains no HUD pixel of any alpha. The contract's
player tile at (928,512)-(960,544) is inside it, and so is every tile within 15
of the avatar in either axis.

## Data mapping

| fixture field | where it appears | encoding |
|---|---|---|
| Name `Nigel Ashby` | cluster 0 row 2 | text 17px `n1` |
| Profession `Survivor` | cluster 0 row 2, right of name | text 11px `n2` |
| Day `14` | cluster 0, right of clock | numeric `n0` in an `n2` label |
| Time `21:47` | cluster 0, 38px | numeric — the largest glyphs on screen |
| Season `Autumn` | cluster 0, beside DAY | text `n2` |
| Place `Cheswick — residential` | cluster 0, under DAY/AUTUMN | text 12px `n1` |
| Weather `Light drizzle` | cluster 0 env row | text `n2` |
| Temperature `8 °C` | cluster 0 env row | numeric `n0` |
| Wind `NE 12 km/h` | cluster 0 env row | numeric + bearing text `n0` |
| Light level `12%` (moonlight) | cluster 0 light row, `MOONLIGHT 12%` | numeric `n0` |
| Flashlight `34%` charge | cluster 0 light row | numeric `a1` + 44px sparkline at 34% |
| STR/DEX/INT/PER `10/12/9/11` | cluster 0, own row | numeric `n0` on `n2` labels — **demoted off the identity line** (it was competing with the name for the same row and forcing the cluster 302px wide) |
| Speed `96` | cluster 2 stat run | numeric `n0` |
| Focus `84` | cluster 2 stat run | numeric `n0` |
| Pain `12` | cluster 2 effects row 2 | numeric `a1` |
| Morale `-3` | cluster 2 stat run, `MRL -3` | numeric `n0` |
| Encumbrance torso `12`, arms `4` | cluster 2 stat run, `ENC 12 T · 4 A` | numeric — **demoted out of the limb cells**, where it was fighting the `/max` denominators for the same 11px tier inside an 88px column |
| Move mode `walking` | cluster 2 stat run, `WALK` | text `n2` |
| Noise made `8` | cluster 2 stat run, `NOISE 8` | numeric `n0` |
| Head `20 / 24` | grid cell (88,0) | 23px numeric + bold 11px `/24` + 66px bar at 83% `a1` |
| Torso `45 / 60` | grid cell (88,70) | 23px + `/60` + bar 75% `a1` |
| L arm `8 / 30` | grid cell (0,70) | 23px in `a0` + `/30` in `a1` + bar 27% `a0` |
| L arm **critical** | same cell | **four redundant cues**: label reads `L·ARM !!`, value in `a0`, bar fill in `a0`, and a 2px `a0` flag hanging 12px outside the cluster inset, pulsing 1.9s. Encodes at 27% because it is *bleeding and bitten*, not on a `< 25%` integer threshold |
| L arm **bleeding, bitten** | same cell, line under the bar: `BLEED · BITE` | text `a0`, **6px from the number it describes** (the shipping HUD puts this 891px away) — and again in full in the effects run |
| R arm `30 / 30` | grid cell (176,70) | 23px + `/30` + bar 100% `n1` |
| L leg `22 / 28` | grid cell (0,140) | 23px + `/28` + bar 79% `a1` |
| R leg `28 / 28` | grid cell (176,140) | 23px + `/28` + bar 100% `n1` |
| Stamina `5100 / 8700` | cluster 2, `STAM` row | 120px bar at 59% + full numeric pair `n0` |
| Hunger `Peckish` | cluster 2 needs row | text `a1` |
| Thirst `Thirsty` | cluster 2 needs row | text `a1` |
| Fatigue `Tired` | cluster 2 effects row 2 | text `n2` |
| Warmth `Chilly` | cluster 2 needs row | text `a1` |
| Effect `Bleeding (L arm)` | cluster 2 effects row 1 | text `a0` (+ at the limb) |
| Effect `Bitten (L arm)` | cluster 2 effects row 1 | text `a0` (+ at the limb) |
| Effect `Soaked` | cluster 2 effects row 2 | text `n2` |
| Effect `Tired` | cluster 2 effects row 2 | text `n2` |
| Effect `Pain 12` | cluster 2 effects row 2 | text + numeric `a1` |
| Wielded `fire axe` | cluster 3, 17px | text `n0` |
| `32 bash / 0 cut / +1 to-hit` | cluster 3 row 3 | numerics `n0` on `n2` labels |
| Sidearm `Glock 19 12/17 9mm JHP` | cluster 3 row 4 | text `n2`, magazine `12/17` in `n0` |
| Target `zombie brute` | cluster 1, 17px `a0` | text + hue |
| Target HP `62%` | cluster 1 | 96px bar at 62% `a0` + numeric `n0` |
| Target `4 tiles NE` | cluster 1 + rosette | numeric **and** position: the 8-dot rosette lights its NE dot at 4px `a0` while the other seven stay 2px `n3` |
| Target status `moving` | cluster 1 | text `n2` |
| `3 hostiles visible` | cluster 1 | numeric `n0` |
| `1 tracking` | cluster 1 | text `a0` — the only `a0` in that row, so it separates from the count |
| `SAFE MODE ON` | cluster 1, 11px bold `n0` | text at the ladder's top step |
| Log, 6 rows | cluster 2, `LOG` | 13px text + 11px `n3` timestamp + a 2px recency rule per row. **Age is the rule's luminance (0.20 → 1.00), never the text's opacity.** The previous opacity ramp (0.28 → 1.00 on the text itself) is what made five of six rows unreadable and it is gone |
| log colour semantics | per row | `n2` neutral, `a2` bad, `a0` bad-fresh, `a1` caution; newest row also goes bold |
| Minimap 11x11 | cluster 0, 13px cells, 143x143 | ASCII glyph matrix, five luminance steps: field `·` L184, forest `F` L205, built `H` L235, POI `^` `a1`, self `@` pure white bold |
| Mission marker NE | overmap row 2 col 10, `^` in `a0` bold | hue + position |
| Mission bearing | `MISSION NE` under the map | text, `NE` in `a0` |
| Hotbar 9 slots | cluster 3, 3x3 at 105px pitch | `key` 13px `n0` + label 11px bold `n2` |
| Hotbar slot 1 **unavailable** | slot `f` FIRE | **both** channels dimmed to `n3` **and** a 1px strike through the whole slot |

**Nothing in the fixture is dropped.** Two fields were *demoted* (STR/DEX/INT/PER
off the identity line, encumbrance out of the limb cells) purely to thin the
number of things competing in the 11px tier, and both are marked above.

## Occlusion budget

**9.07% of the 1920x1080 play area, against the ratified baseline of 31.0% —
a 3.42× reduction.**

Counting rule, stated up front because this design has no rectangles to
measure: **ink and hairlines at full weight, soft scrims at half weight.**
Play area = `1920 × 1080 = 2,073,600 px²`.

### Scrims — half weight

Geometric footprint of the six pools, `π/4 · w · h`:

| pool | w x h | ellipse area |
|---|---|---|
| 1 overmap | 199 x 215 | 33,603 |
| 2 contact | 255 x 181 | 36,250 |
| 3 body-part grid | 278 x 223 | 48,690 |
| 4 effects/needs/stats | 276 x 150 | 32,516 |
| 5 log | 399 x 158 | 49,513 |
| 6 loadout/hotbar | 336 x 200 | 52,779 |
| sum | | 253,351 |
| − pool 3∩4 and 4∩5 overlap | | −3,004 |
| **union** | | **250,347** |

The union is measured, not estimated: the six pools were rendered alone onto a
transparent 1920x1080 surface and every pixel with non-zero alpha counted —
250,347 px², which agrees with the analytic `253,351 − overlap` to 1.2%.

    250,347 x 0.5 = 125,174 px²  ->  6.037%

### Ink — full weight

The clusters, rails, ticks, bars and per-glyph casing were rendered alone onto
the same transparent surface and integrated over alpha:

    sum(alpha) / 255 = 62,906 px²  ->  3.034%

Alpha-integration rather than a bounding box, because there is no box: the
paint is glyph-shaped. The four cluster *bounding* rectangles total 369,192 px²
(17.81%) and the union of all non-zero-alpha ink pixels is 197,375 px²
(9.52%), but the great majority of that second figure is casing halo at 1–5%
alpha, which removes 1–5% of the light beneath it and would be dishonest to
charge as occlusion.

### Total

    62,906 + 125,174 = 188,080 px²
    188,080 / 2,073,600 = 0.09070  ->  9.07%

    31.0% / 9.07% = 3.42x less of the screen covered

**Sensitivity.** Swap the ink rule and the answer barely moves, which is the
point of quoting it: ink counted only where alpha ≥ 50% gives **8.54%**; ink
counted where alpha ≥ 25% gives **9.47%**. Every variant is under 10%.

**Half weight is the conservative choice here, not a favour.** The pools'
actual alpha integral is 117,530 px² (5.67%), i.e. their mean alpha over the
ellipse is **0.469** — below the 0.5 the half-weight rule charges them. Quoting
the alpha-weighted figure on both sides would give `62,906 + 117,530 =
180,436 = 8.70%`. The headline stays at 9.07% so it can be compared directly
against the baseline's full-weight 31.0% under one rule.

**Excluded, with reasons:** nothing. There is no `display:none` region, no
`opacity: 0` vignette and no off-screen overflow in this design — every pixel
it can ever paint is in the number above. (The shipping HUD's 31.02% excludes
9,940 px² of hotbar that falls off the bottom of the screen; this design has no
such off-screen paint because nothing here is positioned by percentage.)

## Motion

Three things animate, all of them discrete-state, all of them inside
research-infodesign.md §7's budget of 2–3 per turn.

- **The critical flag** — the only animation in the resting screenshot. 2px
  `a0` bar beside the failing limb, `opacity 1 → 0.45 → 1` over 1.9s
  `ease-in-out`, looping while the limb is critical. It is deliberately slow
  and low-amplitude: it must be detectable in peripheral vision without
  demanding a saccade, and it must survive 2,000 turns without fatiguing
  (§7). It resolves to full opacity, so a screenshot at any moment shows a
  solid mark. It is the *only* looping animation in the design.
- **Log arrival** — a new row slides up 6px and fades 0 → 1 over 140ms
  `ease-out` (cubic-bezier 0.25 0.46 0.45 0.94), and the five rows below it
  translate up 20px over the same 140ms. The 2px recency rules re-ramp their
  luminance in the same frame. At rest this is invisible, which is why the
  ramp is baked into the static luminances rather than left to the animation.
- **Body-part value change** — the 23px numeral crossfades over 120ms
  `ease-out` and its sparkline fill animates its width over 200ms `ease-out`.
  Colour does **not** animate; a tier change (`n1` → `a1` → `a0`) is a hard
  cut, because an interpolated health colour spends most of its duration
  showing a value that is not true.

Deliberately **not** animated: the pools (they must never draw attention to
their own boundary — a fading scrim is exactly how a scrim becomes visible as
a rectangle), the overmap, the hotbar, the clock, and the rails. The playhead
notch does not blink (§ *avoid blinking cursors*); it jumps to the new row.

## Feasibility in RCSS

Buildable as-is except where noted.

- **`text-shadow` is not in RCSS 6.2.** It is the mockup's stand-in for the
  per-glyph casing, which ships as
  `font-effect: outline(1dp #000), blur(3dp #000e);` — RmlUi's `font-effect`
  takes a comma-separated list and `outline` + `blur` compose exactly the hard
  ring + soft halo this file paints with ten shadow terms. This is the single
  most important line in the design and it is native.
- **The pools are native.** `background-image: radial-gradient(ellipse 50% 50%
  at 50% 50%, …)` becomes
  `decorator: radial-gradient(ellipse closest-side at 50% 50%, #030605cc 0%,
  #030605bd 42%, #0306058a 70%, #03060542 86%, #03060500 100%);`.
  `closest-side` on a rectangle resolves to exactly `50% 50%`, which is what
  guarantees the last stop lands on the ellipse and the box corners stay
  unpainted. Better still, in RCSS a decorator paints on the element's own
  padding box, so the six pool `<div>`s do not need to exist at all — each
  block carries its own decorator with padding to give the gradient room. The
  mockup keeps them as separate divs only so their geometry is legible in the
  source.
- **`filter: drop-shadow()` on `.d01c`** is native (contract's verified list).
- **Flexbox baseline alignment** (`align-items: baseline` for the `8` `/30`
  pair) is native; `align-self: center` for the sparklines is native.
- **`display:inline-block` on the overmap cells** — RmlUi 6.2 has
  `inline-block`, and it is the right primitive here. The 11 rows are plain
  blocks.
- **Everything is absolutely positioned against a `position: relative`
  cluster**, which is native and is also what makes this design cheap: no
  region needs `sidebar_hud_apply_rect()` percentages, so the
  `TERMY × fontheight != 1080` class of bug that costs the shipping HUD 6.34px
  of off-screen hotbar cannot occur. Corners are corners at every resolution.
- **`Consolas` for `Source Code Pro`**, per the contract. Source Code Pro's
  advance is 0.6em against Consolas' 0.5498em, so **every row in this file is
  ~9% wider in the game than in the mockup**. Worst case is the log: the
  longest row is 363px here and ~396px shipped, and pool 5 is 399px wide — it
  would need widening to ~435px, which costs 0.26 points of occlusion (9.07% →
  9.33%). Cluster 0's widest row goes 302 → ~330px and cluster 3's hotbar grid
  294 → ~321px; neither reaches the protected centre band, which has 1111px of
  slack.

### New C++ producer work

This design needs more from `src/panels.cpp` than the shipping HUD emits. Being
specific, because "restore parity" is most of it:

- `hud_topbar` must emit **weather description, wind, light level and the held
  light source's charge**, none of which it reads today.
- `hud_vitals` must emit **`max` per part** (it currently formats
  `cur`/`max` into the bar text but does not expose the pair as separate
  fields), and must emit the **bleeding / bitten / splinted flags per part** —
  the value is already computed into `label_hex` at `panels.cpp:872` and
  thrown away. This design puts `BLEED · BITE` inside the limb's own cell, so
  the producer must key it to the part, not to a global effects string.
- The **critical predicate must move off `cur * 100 / max < 25`**. Integer
  truncation makes `8*100/30 == 26`, so the shipping crit encoding never fires
  on the fixture. This design fires on *bleeding or bitten or ratio < 0.30*,
  which needs a float ratio and the per-part effect flags above.
- `hud_botbar` must emit **morale, noise, per-group encumbrance and the
  wielded weapon's bash/cut/to-hit**, and a new producer must read the
  **first sidearm in inventory** (nothing in the HUD reads inventory today).
- `hud_target` (new, or an extension of the botbar producer) must emit the
  target's **range and bearing** so the rosette can light one of eight dots,
  and the **HP fraction as a number** — the shipping `.tbar-fill` is an inline
  span with no `display: block`, so its width is ignored and the bar is a
  permanently empty trough.
- `hud_hotbar` must gain a **disabled state**; it currently ignores its
  `avatar &` argument entirely.
- `hud_log` must emit **per-row age as an index**, not as a pre-multiplied
  opacity, so the recency rule can carry it instead of the text.

No new asset and no new font: the design is one bundled face and CSS
gradients.

## Weaknesses

1. **It has no reserved space, so it degrades by overhang rather than by
   truncation.** Every string is positioned for the fixture's exact length.
   Twelve effects instead of five turns the two effects rows into four or five,
   which pushes the needs / stamina / stat run down by ~60px into the `LOG`
   header; the log has no room to give, because its last row is already
   baselined 9px off the bottom of the screen. There is no wrapping, no
   scrollbar and no `(+7)` overflow marker anywhere in this design — the rows
   would simply collide. Pool 4 would not grow with them either, so the
   overflowing effects would land on unscrimmed terrain. A shipping version
   needs the bottom-left cluster to be a bottom-anchored flex column with the
   log capped at six rows, and pool 4 sized from the measured content height.

2. **1280x720 does not just shrink it, it changes what the design is.** The
   clusters are absolutely positioned in pixels with no scaling, so at 720p the
   same 302 x 327 top-left cluster occupies 23.6% of the width instead of
   15.7%, and the four clusters' bounding boxes go from 17.8% of the screen to
   **40.1%** — the protected centre band collapses from 1111px (57.9% of
   width) to 471px (36.8%), which is barely wider than the eight tiles around
   the avatar. The thesis ("the centre is sacred") is quantitatively false at
   720p. Scaling the type down instead is not available: the label tier is
   already at the 11px legibility floor, so the only honest 720p answer is to
   drop the overmap and the hotbar labels (keys only), which is a different
   design and should be specced as one.

3. **The casing is a contrast device, not a contrast guarantee, and cluster 0
   is where that shows.** Everything under a pool has a floor: the scrim is
   opaque enough at its centre that even `n3` clears 4.5:1. Cluster 0 has no
   pool, so its contrast is whatever the terrain gives it plus the casing —
   fine over grass and over the building's concrete in `world.png`, and fine
   over night pavement, but a snow tile or a fire tile at L230+ would put 11px
   `n2` (L193) at roughly 1.2:1 with only the 1px black ring separating them.
   The design has no adaptive response to this; the shipping HUD's
   `.env-fire` / `.env-night` tint hooks have no equivalent here because there
   is no region background to tint. The correct fix is an ambient-luminance
   feed that raises the pool alphas (and adds a seventh pool under cluster 0)
   rather than changing any colour — but that needs a per-frame terrain
   luminance sample that no producer computes today.

4. **A deuteranope loses one distinction, and it is a real one.** Harm-fresh
   `a0` (#ff8347, L154) and ambient `n3` (#8e9c96, L154) are the *same
   luminance* by construction — the ladder was built so the accents sit at
   known L-steps, and 154 was reused. In grayscale, `1 TRACKING` collapses into
   the same tone as a timestamp, and `L·ARM` collapses into the same tone as
   `R·LEG`'s label. Every one of those cases is redundantly encoded (the `!!`
   suffix, the 2px flag, the bar width, the rosette dot's *size*), so nothing
   becomes unrecoverable — but the *preattentive* channel, the one that is
   supposed to work in <250ms without a fixation, is gone for those players and
   they fall back to reading. Fixing it properly means splitting the ladder so
   no accent shares an L-step with a neutral, which costs one step of the
   four-step neutral ramp.
