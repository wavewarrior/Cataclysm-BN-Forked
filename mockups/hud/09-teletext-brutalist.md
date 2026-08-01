# 09 — Teletext Blocks / Brutalist Grid

<!-- TELETEXT-BRUTALIST-SENTINEL-D09-RAILWAY-DEPARTURE-BOARD -->

## Thesis

**Order beats subtlety.** This HUD argues that the fastest survival readout is not
the prettiest one or the most diegetic one, but the one whose *geometry* the
player memorises inside an hour — after which position alone is the label and
nothing needs reading at all. So it commits to a single hard modular grid
(32px columns, 24px rows, whole pixels, no exceptions), flush-left everything,
pure `#000000` opaque ground, five saturated primaries each with exactly one
meaning, two type sizes, and one hairline weight used only where a rule does
structural work. Health is not a thin bar but a **run of chunky 16px blocks**,
big enough to count peripherally: the left arm at 8/30 is two red blocks against
six dead ones, and you see it from the corner of your eye without a fixation.
What it trades away is charm, atmosphere, and every gram of world-fiction. There
is no glow, no gradient, no rounded corner, no drop shadow, no animation, and no
attempt to look like a wrist device or a field manual. It looks like a railway
departure board bolted over the game, and it is unapologetic about that: the
board is legible in a smoke-filled station at forty metres, which is the exact
problem a HUD has.

## Register

Teletext/Ceefax (`research-aesthetic.md`) supplies the ground and the chroma:
`#000000` field, saturated primaries only, chunky block mosaics instead of
glyphs. Swiss/brutalist supplies the discipline: modular grid, flush-left,
hairline rules, one weight of ornament (none), aggressive black gutters instead
of boxes. `research-infodesign.md` supplies two constraints that override taste:
data-ink (a pixel that does not change when the data changes is deleted) and
position as a preattentive channel (§2/§3 — colour and position resolve in
<250 ms, shape does not, so the encoding is *run length in a fixed column*, with
hue as reinforcement).

### Chroma — five hues, one exclusive meaning each

| token | hex | exclusive meaning | where it appears |
|---|---|---|---|
| `WHITE` | `#FFFFFF` | **fact** — every label, every value, the player's own map cell | all text, `@` cell |
| `GREEN` | `#00FF00` | **nominal** — a metric at or above its safe threshold (≥67%) | health runs, `SAFE ON`, good log line |
| `YELLOW` | `#FFFF00` | **caution** — below safe, above critical (34–66%), or an off-nominal need | stamina run, needs, `PAIN 12`, `MRL -3`, `LAMP 34%`, target HP, caution log lines |
| `RED` | `#FF0000` | **harm / denial** — critical metric (≤33%), injury flag, hostile, blocked action | L-arm run + flags, bleed/bite effects, hostile counts, `NO GUN`, bad log lines |
| `CYAN` | `#00FFFF` | **actionable** — a thing the player can press or go to | hotbar key cells, mission marker |

No hue appears for any other reason. There is no decorative chroma anywhere in
the sheet — grep for `#00FF00` and every hit is a threshold test.

### Neutral ramp — structure and absence, never state

Chroma is spent; structure needs a separate channel, so it gets a luminance
ramp. These are **values, not hues**, and the five-hue rule governs chroma only.

| token | hex | role |
|---|---|---|
| `N0` | `#000000` | panel ground, map field cell, black gutters |
| `N1` | `#1E1E1E` | **absence** — empty block in a run, dead hotbar key cell |
| `N2` | `#3A3A3A` | low structure — the hairline, forest cells |
| `N3` | `#6E6E6E` | high structure — building cells, unavailable-action text |
| `N4` | `#FFFFFF` | = `WHITE` above |

### Type — exactly two sizes

| size | line-height | used for |
|---|---|---|
| **20px** | 20px | everything: labels, values, hotbar, map header, contact panel |
| **14px** | 20px | the message log only — the sole free-prose surface |

Face: `Consolas`, the contract's metric stand-in for the bundled *Source Code
Pro* (both 0.5498 em advance, so the character cell is 11.0px at 20px and 7.70px
at 14px). Monospace gives tabular figures for free — `20/24` and `45/60` occupy
identical width without `font-variant-numeric`.

**Label and value are the same size and the same colour.** They are separated by
**tracking alone**: labels are `letter-spacing: 2px` ALL CAPS (13px cell),
values are `letter-spacing: 0` (11px cell). This is the whole typographic system.

### Frame treatment

There is no frame. Panels are un-bordered opaque black rectangles; grouping is
done by the grid and by black gutters. The single ornament budget is one
hairline, `1px solid #3A3A3A`, used **four times in the entire HUD** — above
`RESERVE` and above `EFFECTS` in the vitals column, and above the threat group
and the armament group in the contact panel. Never as a box outline, never as an
underline for decoration.

## Layout

### The modular grid (this is the design)

| axis | body | gutter | pitch | count across 1920/1080 |
|---|---|---|---|---|
| **column** | 24px | 8px | **32px** | 60 columns, exactly |
| **row** | 20px (= line box) | 4px (leading) | **24px** | 45 rows, exactly |

Every panel `x`, `y`, `width` and `height` is a whole multiple of 32 (x/width)
or 24 (y/height). Verified in the browser: all six panels report
`scrollWidth == clientWidth` and `scrollHeight == clientHeight`, i.e. nothing
overflows its module.

Derived modules, all whole pixels:

- **Half-module = 8px.** Every sub-column stop inside a panel is a multiple of 8:
  `0, 32, 64, 72, 96, 112, 128, 208, 240, 264, 288, 312, 320, 368, 392`.
- **Panel padding = 16px** left/right (half a column pitch), **0** vertical —
  the 4px row leading is the vertical padding.
- **Block run pitch = 24px** (16px block + 8px kerf) — deliberately equal to the
  row pitch, so a run of 8 blocks is 184px = exactly as wide as 8 rows are tall.
- **Mosaic cell = 16 x 16** — the half-module squared.
- **Hotbar slot pitch = 160px** = 5 columns.

Nothing is centred. There is not one `text-align: center` in the sheet; every
absolutely-positioned child is placed by `left`.

### Regions

| # | region | x | y | w | h | cols x rows |
|---|---|---|---|---|---|---|
| A | `#top` TOPBOARD | 0 | 0 | 1920 | 48 | 60 x 2 |
| B | `#vit` VITALS | 0 | 72 | 448 | 408 | 14 x 17 |
| C | `#map` OVERMAP | 1696 | 72 | 224 | 216 | 7 x 9 |
| D | `#log` LOG | 0 | 840 | 608 | 144 | 19 x 6 |
| E | `#con` CONTACT | 704 | 840 | 416 | 144 | 13 x 6 |
| F | `#bar` HOTBAR | 0 | 1032 | 1472 | 24 | 46 x 1 |

No two regions overlap, so the union is the sum. The player's tile
(928,512)–(960,544) is clear of every panel by 480px horizontally.

**A — TOPBOARD**, two 24px rows, one flex line each, `gap: 20px`, nowrap.
Row 1 is identity + place + clock + weather (9 tokens, 1544px of the 1888px
content width). Row 2 is light + character numbers (13 tokens, 1333px). Full
bleed because the board's top edge is the one place a strip costs nothing: the
world above y=48 is sky and roof.

**B — VITALS**, 17 rows at 24px = 408, zero slack. Row 1 header + the two flag
column captions `B` `T`; rows 2–7 the six body parts; row 8 hairline +
`RESERVE`; row 9 stamina; rows 10–11 the four needs; row 12 hairline +
`EFFECTS`; rows 13–17 the five effects. Column stops inside the 416px content
box: `LABEL 0`, `RUN 96` (184 wide), `VALUE 288`, `FLAG-BLEED 368`,
`FLAG-BITE 392`.

**C — OVERMAP**, header row + an 11 x 11 mosaic of 16px cells (176 x 176) flush
to the left padding, 16px bottom pad.

**D — LOG**, six rows, no header, three rigid columns: `TIME 0`,
`SEVERITY 64` (a 16 x 16 block), `TEXT 96` (480px = 62 characters at 14px;
longest fixture line measures 339px). Oldest at the top; the newest line sits
flush against the panel's bottom edge, which is the "now" edge.

**E — CONTACT**, six rows: target identity, target HP run, range/bearing/
behaviour, hairline + threat line, hairline + wielded, sidearm.

**F — HOTBAR**, one 24px row, nine 160px slots. Each slot is
`[12px inverse key cell] [8px] ACTION [8px] STATE`. State text is emitted only
when the state is *not* nominal — eight identical `READY` labels would be eight
slots of pure non-data ink.

## Data mapping

| fixture field | where | encoding |
|---|---|---|
| Name `Nigel Ashby` | A row 1 | numeric/text, `NAME` label |
| Profession `Survivor` | A row 1 | text |
| Day `14` | A row 1 | numeric |
| Time `21:47` | A row 1 | numeric |
| Season `Autumn` | A row 1 | text |
| Place `Cheswick — residential` | A row 1 | text |
| Weather `Light drizzle` | A row 1 | text |
| Temperature `8 °C` | A row 1 | numeric |
| Wind `NE 12 km/h` | A row 1 | numeric |
| Light level `12%` | A row 2 | numeric |
| Flashlight `34%` | A row 2, `LAMP` | numeric + **yellow** (caution: below half) |
| STR `10` / DEX `12` / INT `9` / PER `11` | A row 2 | numeric |
| Speed `96` (`SPD`) | A row 2 | numeric |
| Focus `84` (`FOC`) | A row 2 | numeric |
| Pain `12` | A row 2 | numeric + **yellow** |
| Morale `-3` (`MRL`) | A row 2 | numeric + **yellow** (negative) |
| Encumbrance torso `12`, arms `4` | A row 2, `ENC T12 A4` | numeric |
| Move mode `walking` | A row 2, `MOVE WALK` | text |
| Noise `8` | A row 2, `NOISE 8` | numeric |
| Head `20/24` | B row 2 | **6 of 8 green blocks** + numeric |
| Torso `45/60` | B row 3 | **6 of 8 green blocks** + numeric |
| L arm `8/30` **critical** | B row 4 | **2 of 8 red blocks** + numeric |
| L arm **bleeding** | B row 4, flag column 1 | red 16px block in a fixed position |
| L arm **bitten** | B row 4, flag column 2 | red 16px block in a fixed position |
| R arm `30/30` | B row 5 | 8 of 8 green blocks + numeric |
| L leg `22/28` | B row 6 | 6 of 8 green blocks + numeric |
| R leg `28/28` | B row 7 | 8 of 8 green blocks + numeric |
| Stamina `5100 / 8700` | B row 9 | **4 of 8 yellow blocks** + numeric |
| Hunger `Peckish` | B row 10 | text + yellow |
| Thirst `Thirsty` | B row 10 | text + yellow |
| Fatigue `Tired` | B row 11 | text + yellow |
| Warmth `Chilly` | B row 11 | text + yellow |
| Effect `Bleeding (L arm)` | B row 13 | red severity block + text + part |
| Effect `Bitten (L arm)` | B row 14 | red severity block + text + part |
| Effect `Soaked` | B row 15 | yellow severity block + text |
| Effect `Tired` | B row 16 | yellow severity block + text |
| Effect `Pain 12` | B row 17 | yellow severity block + text + numeric |
| Wielded `fire axe 32 bash 0 cut +1` | E row 5, `WIELD FIRE AXE 32B 0C +1` | text/numeric |
| Sidearm `Glock 19 12/17 9mm JHP` | E row 6 | text/numeric |
| Target `zombie brute` | E row 1 | text |
| Target HP `62%` | E row 2 | **4 of 8 yellow blocks** + numeric |
| Target range `4 tiles NE` | E row 3, `RANGE 4 NE` | numeric + bearing |
| Target status `moving` | E row 3 | text + yellow |
| Threats `3 hostiles visible` | E row 4, `HOSTILE 3` | numeric + red |
| Threats `1 tracking` | E row 4, `TRACKING 1` | numeric + red |
| `SAFE MODE ON` | E row 4, `SAFE ON` | text + green |
| Log 21:46 hit brute (neutral/good) | D row 1 | **green** full severity block, white text |
| Log 21:46 claws left arm (bad) | D row 2 | **red** full severity block |
| Log 21:47 arm bleeding (bad) | D row 3 | **red** full severity block |
| Log 21:47 sharp pain (bad, dimmer) | D row 4 | **red half-width** severity block — intensity is block *width*, never opacity |
| Log 21:47 shambler groans (caution) | D row 5 | **yellow** full severity block |
| Log 21:47 getting thirsty (caution) | D row 6 | **yellow** full severity block |
| Log ordering (newest last) | D | position: oldest top, newest flush to the bottom edge |
| Minimap 11 x 11 chunk | C | 121 coloured 16px cells: field `N0`, forest `N2`, house `N3` |
| Minimap player `@` | C row 6 col 5 | **white** cell (fact) |
| Minimap mission marker NE | C row 2 col 10 | **cyan** cell (actionable) — the only chroma on the map |
| Minimap non-mission POI `^` x2 | C rows 4 and 9 | **DROPPED (distinction only)** — rendered as generic `N3` structure. The neutral ramp is spent on terrain and only the mission earns a hue; two POI pips are not worth a sixth value step. |
| Hotbar `f` Fire, unavailable | F slot 1 | dead `N1` key cell with `N3` glyph, `N3` action text, red `NO GUN` |
| Hotbar `r` `'` `e` `p` `c` `i` `t` `5` | F slots 2–9 | cyan inverse key cell (black glyph on cyan), white action, no state text |
| Hotbar keys' letter case | F | keys rendered ALL CAPS (`F R ' E P C I T 5`) with the rest of the HUD; the bound key is unchanged |

Every other fixture field is present. Nothing else is dropped.

### Two shipping bugs this encoding fixes

- **L arm reads critical.** The shipping HUD computes `8*100/30 == 26` in integer
  maths and tests `< 25`, so the fixture's critical limb never turns red. This
  design's threshold is `pct <= 33` (one third), so 8/30 is red — which is what
  the fixture says it is.
- **Bleeding and bitten appear in the vitals panel.** They are two fixed 16px
  columns present on every part row (`N1` when absent), not text crammed into a
  message. Today they render nowhere in `#hud-vitals` at all.
- **The target HP bar carries a value.** The shipping trough is permanently
  empty; here it is four yellow blocks and `62%`.

### Block quantisation

`filled = floor(cur / max * 8)`, clamped to a minimum of 1 while `cur > 0`.
Head 6.67→6, torso 6.0→6, L arm 2.13→**2**, R arm 8, L leg 6.29→6, R leg 8,
stamina 4.69→4, target 4.96→4. The run is coarse on purpose — it is the
peripheral channel; the exact figure is one column to its right for when you
actually look.

Hue by integer percent: `>= 67` green, `34–66` yellow, `<= 33` red.

## Occlusion budget

Play area: `1920 x 1080 = 2,073,600 px²`. Every panel is `#000000` at alpha
**1.0** — there is no scrim, no translucency, and no `opacity` anywhere in the
sheet, so nothing is counted at half weight. No two panels overlap, so the union
is the sum.

| region | rect | area |
|---|---|---|
| A `#top` | 1920 x 48 | 92,160 |
| B `#vit` | 448 x 408 | 182,784 |
| C `#map` | 224 x 216 | 48,384 |
| D `#log` | 608 x 144 | 87,552 |
| E `#con` | 416 x 144 | 59,904 |
| F `#bar` | 1472 x 24 | 35,328 |
| **union** | | **506,112** |

    506,112 / 2,073,600 = 0.244074 -> 24.41%

Per region as a share of screen: vitals **8.82%**, topboard **4.44%**, log
**4.22%**, contact **2.89%**, overmap **2.33%**, hotbar **1.70%**.

**Against the ratified baseline of 31.02%** (643,226.56 px²) this is 137,114 px²
less ink — **6.61 percentage points lower, a 21.3% relative reduction** — while
carrying every field the baseline drops, including morale, encumbrance, noise,
wind, light level, warmth, the sidearm, the weapon's damage numbers, the
target's range and behaviour, and the two limb-injury flags.

**I own the black.** Alpha-weighted the comparison inverts: the baseline's five
regions are `#0a1a1ae0` (alpha 0.8784) and weight down to **23.16%**, while this
design is opaque and stays at **24.41%** — 1.25 points *worse*. That is a
deliberate trade. A scrim over noisy 32px tiles is a contrast gamble that the
tileset wins about a third of the time; pure black is the only ground on which
`#FF0000` and `#00FF00` hold their full saturation, and full saturation is the
entire preattentive argument. A translucent teletext is just a dim teletext.

## Motion

**Nothing animates.** No transitions, no `@keyframes`, no `opacity` ramps, no
pulses on the critical limb. Three reasons, in order of weight:

1. **It is a turn-based game.** `research-infodesign.md` §7: 2000 turns is 2000
   animations, and per-turn motion accumulates fatigue with no information
   payoff. The block run changes length on the turn tick, instantly, the way a
   split-flap board changes: the *new state* is the event, not the transit.
2. **Motion is a preattentive channel and it is already spent.** §2 reserves
   motion for discrete state changes only. If nothing moves during ordinary
   play, then any movement on screen is the *world*, and the player's peripheral
   motion detector stays pointed at the thing that can kill them.
3. **It costs nothing to be honest.** The contract screenshots a static moment;
   a design whose resting state is its only state cannot cheat that.

The one thing I would consider adding later is a single-frame inversion (swap
ground and ink for one turn) on a block run that just crossed a threshold — a
teletext-legal "flash" with no easing curve and no duration to tune. It is not
in the mockup because it is not visible at rest.

## Feasibility in RCSS

Buildable essentially as-is on RmlUi 6.2. What was consciously avoided:

- **No CSS grid.** The modular grid is enforced by absolute `left`/`top` on
  whole-pixel stops and by flexbox for the two topboard strips and the mosaic —
  `display:flex`, `flex-wrap`, and `gap` are all on the verified-available list.
- **No `::before`/`::after`.** Every block, every flag, every severity marker and
  every mosaic cell is a real `<div>`. That is 121 divs for the map and 8 per
  health run; `hud_map`/`hud_vitals` already emit their regions as one built
  string per turn, so this is string length, not layout cost.
- **No `text-shadow`, no `border-radius`, no gradient decorators, no
  `box-shadow`, no `filter`, no `opacity`.** The sheet is `background-color`,
  `color`, `letter-spacing`, `border-top` and position. That is it.
- **No `var()`.** RCSS 6.2 has no custom properties, so every hex is literal in
  the sheet, exactly as it would ship.

One mockup-only artifact to strip on port:

- `-webkit-font-smoothing: none` on `#screen` is a Chromium hint that makes
  Consolas render hard-edged; RmlUi rasterises the bundled TTF itself and has no
  equivalent. Drop the line — nothing else depends on it.

**Font.** `Consolas` stands in for the bundled *Source Code Pro*; both are
0.5498 em advance, so the 11px/7.7px character cells and every column stop in
the Layout section transfer unchanged. No new asset.

**C++ producer work in `src/panels.cpp`** — this is the honest cost, and it is
not small:

- `hud_vitals` must emit, per part, a floor-quantised eighth count *and* two
  boolean injury flags. It emits neither today; `bleeding`/`bitten` never reach
  the vitals region at all.
- The critical threshold must move from `< 25` to `<= 33` (or be computed in
  floating point), or the fixture's own signature limb stays yellow.
- `hud_topbar_row2` must emit morale, encumbrance (torso and arms), noise, wind,
  light level and held-light charge. `morale_stat()` and the light-level helper
  already exist in `panels.cpp` with no caller — this is restoring parity with
  the curses sidebar, not new features.
- A new `hud_contact` producer for target name / HP percent / range / bearing /
  behaviour plus the threat triple. The current target HP bar is a permanently
  empty trough because nothing fills it.
- `hud_log` must emit a severity **enum** per line rather than a colour, so the
  severity column can be a block and the text can stay white. It must also
  distinguish "bad" from "bad, dimmer" as two enum steps, since this design maps
  intensity to block width and has no opacity channel to fall back on.
- `hud_map` must emit a terrain class per cell instead of a glyph.
- `hud_hotbar` must emit a per-slot availability reason string (`NO GUN`), not
  just a boolean.

## Weaknesses

1. **A quarter of the screen is dead black, and it looks it.** Opaque `#000000`
   is the design's greatest strength for legibility and its worst property as a
   window onto a game about atmosphere. Alpha-weighted it occludes *more* than
   the HUD it replaces (24.41% vs 23.16%), and on a night-lit tileset the panels
   read less like an overlay and more like a broken framebuffer. Anyone who
   wants the world to breathe should pick a different design; the argument here
   is that the world is not what kills you, the unread number is.
2. **The log's severity column fails deuteranopia.** Everywhere else, colour is
   redundant: the health runs encode state as *length in a fixed column* and
   would still be readable in greyscale (L arm = 2 blocks, R arm = 8). The log
   is the exception — its severity blocks are identical 16px squares that differ
   only in hue, and green "you hit the brute" against red "it claws your arm"
   collapses to one grey for 8% of male players. Full-vs-half width encodes
   intensity, not valence, so it does not rescue it. The fix is a shape ramp
   (full square / half-height bar / quarter pip) rather than a hue ramp, and I
   did not take it because it would have added a third block geometry to a
   design whose whole claim is that there is exactly one.
3. **The fixed grid does not survive 1280x720 or a twelfth effect.** Every number
   in the Layout section is absolute. At 720p the log/contact band starts at
   y=840, which is 120px off the bottom of the screen — the design needs a real
   second breakpoint that folds CONTACT under LOG, halves the block run to four,
   and drops the topboard to one row. Worse, the EFFECTS group is a fixed 5-row
   tail of a fixed 408px panel: seven more effects means seven more rows, 576px
   of panel, and 28.5% occlusion. There is no scroll affordance and no overflow
   glyph, so the design must either grow (breaking the memorised geometry the
   thesis rests on) or silently truncate by priority (losing data). Neither is
   good, and a HUD that promises "position is the label" cannot honestly do the
   first.
4. **Position-as-label has a cold start.** The two injury flag columns are
   captioned once, with a single `B` and a single `T`, and never again. Until
   that is learned they are two anonymous squares. Every diegetic and every
   iconographic design in this set is more legible to a player on turn one; this
   one is only faster from about turn two hundred onward. That is a real bet
   about who the HUD is for.
