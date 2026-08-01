# 10 — Sparkline Console

<!-- D10-SPEC-SENTINEL: derived from 10-sparkline-console.html @ sha256 11c6c74c… -->

Every number below was measured out of `10-sparkline-console.html` — from the CSS
as authored, or from the rendered DOM at exactly 1920x1080 in Chromium. Nothing
here is estimated. Where the file contradicts its own stated intent, the file
wins and the contradiction is written down.

## Thesis

This design argues that the current HUD should be beaten **on its own terms**:
not by being prettier or more atmospheric, but by carrying strictly more
information in strictly less covered screen, and then adding the one dimension
the shipping HUD lacks entirely — **history**. A survival roguelike is a game of
slopes, not levels. `STAMINA 59%` is not a decision; `STAMINA 59%, −300/turn,
below its band for eleven turns` is. So every metric that varies gets a
12-sample chart on a shared 49x22px geometry, the newest sample is the only bar
at full strength, and a faint band behind each chart marks where that metric is
*supposed* to sit — so "out of range" is a shape you see without reading a
number. The whole surface is one table in one strip plus one hairline at the top;
there is no panel, no frame, no box, and nothing floats over the play area. What
it trades away is **approachability and elasticity**. This is a dashboard: seven
clusters, fifteen charts, four numeric channels per row, a permanent on-screen
legend, and an 11px type tier that is exactly at the legibility floor and fails
outright at couch distance. It is authored in absolute pixels for a 1920-wide
monitor and it does not survive 1280x720 in any form. It is the most information
per covered pixel of any design here, and the least forgiving.

## Register

### Palette

Measured L\* and HSL for every colour literal in the file (grep of
`#[0-9a-fA-F]{3,8}|rgba?\(…\)` returns exactly these and nothing else).

**Cool neutral ramp** — carries all structure and all values:

| token | hex / rgba | L\* | H° | S% | role |
|---|---|---|---|---|---|
| `ink-bright` | `#eef4f6` | 95.8 | 195 | 31 | the datum: `.now` bars, `.big` values, `.name`, `.who`, `.clock` |
| `ink-body` | `#cfdae0` | 86.4 | 201 | 22 | `.val` / `.num` / `.msg` — every printed value, all log text |
| `ink-label` | `#9dadb4` | 69.7 | 198 | 13 | `.ct` column titles, `.lab` row labels, `H` house glyphs |
| `ink-muted` | `#6d7c83` | 51.0 | 199 | 9 | `.sub` / `.dim` — units, rates, notes, log timestamps, `F` forest |
| `ink-faint` | `#5d6b71` | 44.3 | 198 | 10 | `.chd` sub-headers (`12T` / `NOW` / `%` / `Δ12T` / `RATE`) |
| `ink-off` | `#46545a` | 34.8 | 198 | 12 | disabled hotbar slot cap + label |
| `ink-terrain` | `#3d4a4f` | 30.5 | 197 | 13 | map field glyphs `.` |
| `ink-sep` | `#343f44` | 25.9 | 199 | 13 | the `·` separators in the ledger |
| `hair` | `rgba(157,173,180,α)` | — | 198 | 13 | every rule, every history bar, every cap. α = `.13` `.155` `.17` `.18` `.22` `.26` `.34` `.36` `.50` |
| `scrim` | `rgba(5,8,10,.60–.66)` | 2.5 | 197 | 41 | the only two backgrounds in the design |
| `plate` | `rgba(11,17,20,.80)` | 4.7 | 200 | 29 | sparkline casing (15 instances) |
| `plate-map` | `rgba(6,10,12,.80)` | — | — | — | map casing, 143x143 |
| `casing` | `rgba(0,0,0,.55–.92)` | 0 | — | — | the four-layer `text-shadow` welded to every glyph |

**Three chromatic hues** — nothing else on screen carries chroma:

| token | hex | L\* | H° | S% | role |
|---|---|---|---|---|---|
| `red` | `#f65a45` | 59.3 | 7 | 91 | critical / harm you must act on now: `CRIT`, `8/30`, `27%`, `bleeding · bitten · −6/t`, `3 HOSTILE`, bad log rows, `1 BLEEDING` / `2 BITTEN` |
| `red-dim` | `#c2554a` | 50.1 | 5 | 50 | one aged bad log row (`You feel a sharp pain.`), `no gun` |
| `amber` | `#f2b134` | 76.4 | 39 | 88 | outside its desired band: stamina, pain, noise, thirst, fatigue, warmth, `1 TRACKING`, caution log rows, mission marker |
| `cyan` | `#74e2f0` | 84.2 | 187 | 81 | **`NOW` and `self`, never a value**: the 3x2 reading head under each chart, the newest-log rail, `@`, the safe-mode dot |

**Hue cap: three chromatic hues.** The file declares it in its own comment
(lines 15–20) and **honours it** — the colour-literal grep finds red, amber,
cyan and neutrals, and no green anywhere. That is the deliberate consequence:
*healthy is bright neutral*, not green, which removes every red/green pair from
the design and makes the health encoding luminance-first (research-infodesign
§8). One honest caveat: the "neutral" ramp is not achromatic. It is a fourth,
low-chroma hue at 195–201° — S 9–13% through the working mid-range, rising to
22% at `ink-body` and 31% at `ink-bright`. A strict reading is therefore *three
chromatic hues over one near-neutral cool tint*, not three hues total.

**Monochrome check.** Collapse to L\* and the ladder mostly survives:
amber 76.4 vs red 59.3 is 17 points apart; red 59.3 vs `ink-bright` 95.8 is 36.
One pair collapses: **`red-dim` #c2554a (L\*50.1) and `ink-muted` #6d7c83
(L\*51.0) are 0.9 L\* apart** — in greyscale the aged bad log row is
indistinguishable from a unit label. Fix is a one-token change: drop `.crit2`
to L\*≈42 or lift it to L\*≈62.

### Type

One face: `Consolas, "Cascadia Mono", "Lucida Console", monospace` — Consolas as
the contract's metric stand-in for Source Code Pro. Measured advance width
0.5498em: **6.048 / 6.598 / 7.148 / 8.247px** at 11 / 12 / 13 / 15px.

Four sizes, with the measured count of text-bearing elements at each:

| size | elements | weight / spacing | used by |
|---|---|---|---|
| 11px | **93** | `600` on `.ct`, else normal; letter-spacing 0.5–2px | column titles, sub-headers, all row labels, all rates, all notes, `.flag`, `.eff`, log timestamps, the whole legend, `SURVIVOR`, `AUTUMN` |
| 12px | 171 (121 of them map glyph cells) | normal | `.val` / `.num` values, `·` separators, hotbar key caps, map glyphs |
| 13px | 22 | `600` on `.big` / `.who` | `.big` percentages, item names, log messages, `NIGEL ASHBY`, `3 HOSTILE` |
| 15px | 1 | `600` | the clock `21:47` — the single largest glyph on screen |

**Smallest type size: 11px.** The cited floor is
`research-infodesign.md:89` — *"Minimum readable size: 0.8em monospace = ~11px
at 96dpi; ensure line-height 1.2× font-size to prevent glyph clipping."*
The design therefore sits **exactly at the floor, with zero margin**, on 93
elements. Two qualifications, both against the file:

1. **The digest's own arithmetic is internally inconsistent**, and reading it the
   other way is far less kind. `research-infodesign.md:33` states the game's
   scale as 0.8em / 0.85em / 1em; against the shipping 18dp body
   (`sidebar_hud.rcss:13,16`) that makes 0.8em **14.4px**, not 11px. Measured
   against the *game's* smallest tier, this design's floor is 3.4px — **24%** —
   below anything the shipping HUD prints.
2. **The line box misses the 1.2× requirement by 0.2px.** Measured glyph box for
   11px Consolas at `line-height: normal` is **13px = 1.182×**; the digest asks
   1.2× = 13.2px. Nothing clips, because every row box is 14–25px tall and
   `align-items: center` centres the glyph box inside it — but the declared line
   height is under the floor. Ported to RCSS this self-corrects: RmlUi's default
   `line-height` is `1.2` (`StyleSheetSpecification.cpp:330`), giving 13.2px.

### Border / frame treatment

**There is none.** No `border` property appears anywhere in the file. The only
`border-radius` is 2px on the nine hotbar key caps and a 3px pill on the
safe-mode dot. Enclosure is replaced by nine hairlines, every one 1px and every
one fading to zero alpha at both ends so no line reads as an edge:

| rule | rect | alpha | fade |
|---|---|---|---|
| `.r-ledger` | 0,30 1920x1 | `.26` | 0→.26 at 2%, .26→0 at 98% |
| `.r-console` | 0,828 1920x1 | `.34` | 0→.34 at 2%, .34→0 at 98% |
| `.r-head` | 28,858 1864x1 | `.18` | flat |
| `.r-foot` | 28,1036 1864x1 | `.13` | flat |
| 5x `.vrule` | y836 1x198 | `.17` | 0→.17 at 9%, .17→0 at 91% |

The two scrims are the same idea in the other axis: both fade to nothing at the
edge that faces the play area (the ledger fades downward from 62% of its height,
the console fades upward over its first 32px), so the HUD has **no hard boundary
against the world** anywhere on screen. Contrast is not bought with plates but
with a four-layer black casing welded to every glyph
(`0 0 3px rgba(0,0,0,.92)`, `0 1px 0 .85`, `±1px 0 0 .55`).

## Layout

Two regions. Everything is absolutely positioned in px; all rects below are
`getBoundingClientRect()` from the rendered document.

### The ledger — one thin rule at the horizon

| element | x | y | w | h |
|---|---|---|---|---|
| `.ledger` scrim | 0 | 0 | 1920 | 42 |
| `.r-ledger` hairline | 0 | 30 | 1920 | 1 |
| `.ledger-in` | 28 | 4 | 1864 | 22 |
| `.lg-l` identity run | 28 | 4 | **521.94** | 18 |
| `.lg-r` threat run | **1611.41** | 6 | 280.59 | 15 |

The identity run ends at x=549.94, the threat run starts at x=1611.41:
**1061.47px of the top strip is bare terrain under a 0.60-alpha wash.** The
text box (y 4→26) stops 0.04px short of y=26.04, which is exactly where the
scrim's gradient begins to fade — the ink sits entirely inside the strongest
part of the scrim, by construction.

Reading order left→right is *who · when · where*, then a 1061px gap, then *how
dangerous*. The clock is the largest glyph in the design (15px) because it is
the one field a player checks on a fixed cadence rather than on an event.

### The console — one table, six columns, fifteen charts

| element | x | y | w | h |
|---|---|---|---|---|
| `.console` scrim | 0 | 796 | 1920 | 284 |
| `.r-console` | 0 | 828 | 1920 | 1 |
| `.r-head` | 28 | 858 | 1864 | 1 |
| `.r-foot` | 28 | 1036 | 1864 | 1 |
| `.foot` hotbar + legend | 28 | 1044 | 1864 | 22 |
| 5x `.vrule` | 302 / 635 / 930 / 1271 / 1712 | 836 | 1 | 198 |

All six columns are `top: 836; height: 198`:

| column | x | w | head | `.cbody` h | ink right edge | slack to column edge |
|---|---|---|---|---|---|---|
| BODY | 28 | 254 | `BODY 12T HP %` | 164 | 267 | 15 |
| LOAD | 323 | 292 | `LOAD 12T NOW % Δ12T RATE` | 168 | 612 | **3** |
| SENSE | 656 | 254 | `SENSE 12T NOW Δ12T NOTE` | 136 | 901 | 9 |
| CONTACT | 951 | 300 | `CONTACT · ARMS · TARGET · STATE` | 154 | 1251 | 0 |
| LOG | 1292 | 400 | `LOG · NEWEST LAST` | 144 | 1673.30 | 18.70 |
| MAP | 1733 | 159 | `MAP · 11x11` | 165 | 1892 | 0 |

The grid closes to the pixel:
`28 + 254 + 41 + 292 + 41 + 254 + 41 + 300 + 41 + 400 + 41 + 159 + 28 = 1920`.
Every gutter is **41px = 20 + 1px rule + 20**, five times identically, and the
outer margin is 28px on both sides. Column ink bottoms are 1031.5 / 1034 / 1002
/ 1020 / 1010 / 1031 against a column box closing at 1034 — LOAD is flush
against it with zero vertical slack, and 3px of horizontal slack.

Only three of the six column heads carry the `12T` history marker (BODY, LOAD,
SENSE). CONTACT holds the fifteenth sparkline (TARGET) but its head reads
`ARMS · TARGET · STATE`, so that one chart's window is undeclared.

### The sparkline — one geometry, fifteen instances

`.spark` is **49x22px** `box-sizing: border-box` with `padding: 1px`, so the
content box is 47x20. Twelve 3px bars with a 1px `column-gap`:
`12x3 + 11x1 = 47` exactly. Measured: 15 instances, 12 bars each. Bar height is
`round(pct x 20)` — HEAD 83% → 17px, TORSO 75% → 15px, L·ARM 27% → 5px,
R·ARM 100% → 20px, L·LEG 79% → 16px.

Three layers, in paint order:

1. **The plate and the band.** A single `linear-gradient` decorator per metric:
   `rgba(11,17,20,.80)` casing with a `rgba(157,173,180,.155)` window punched
   out of it at the metric's desired range. Ten distinct band definitions
   (`.sp-body .sp-sta .sp-pain .sp-mor .sp-focus .sp-light .sp-temp .sp-noise
   .sp-lamp .sp-tgt`). `.sp-mor` additionally carries a brighter `.36` stop at
   border-box y 10–11px — the **zero line** for the only diverging chart.
2. **Eleven history bars** at `rgba(157,173,180,.50)`, i.e. half strength. They
   are texture, not data you read individually.
3. **The newest sample** (`.now`) at full strength, and its hue *is* the datum:
   `#eef4f6` inside band, `#f2b134` out of band, `#f65a45` critical — written
   inline, because the producer decides it.

The **reading head** is a 3x2px cyan block at `right:1px; bottom:-3px` —
measured at x 115→118 under the twelfth bar, sitting 3px *below* the plate. It
is the only cyan in the chart and it never moves.

`.sp-mor` is the one chart that is not a column chart: bars carry
`margin-bottom` as well as `height`, so `height:5px; margin-bottom:10px` spans
bar-y 10→15 (positive) and `height:4px; margin-bottom:6px` spans 6→10 (negative,
hanging from the zero line at bar-y 9–10). Morale's history therefore reads as a
sign change, which a bar-from-zero chart could not show.

### Log and map

Six `.logline` rows at exactly 24px each, y 866 / 890 / 914 / 938 / 962 / 986,
newest last. Each row is `[2px rail][6px][40px stamp, right-aligned][10px][msg]`,
so `.msg` starts at x=1350 with 342px of usable width. Widest fixture line
measures **323.30px** — 18.70px of slack, no wrap. The timestamp is printed
**only when the minute changes** (rows 1 and 3 carry `21:46` / `21:47`; rows 2,
4, 5, 6 leave the 40px cell blank but reserved), so the column stays aligned
without repeating four identical stamps.

The map is a 143x143 plate: 11 rows of 13px, 11 cells of 13x13, one 12px glyph
per cell centred with `line-height: 13px` pinned to the cell height.

### The untouched centre

The band `y 42 → 796` — **754px full width, 1,447,680 px² = 69.81% of the
screen** — contains zero HUD pixels. There is no floating panel, no vitals
overlay, no dock. The player's tile at (928,512)–(960,544) is 254px clear of the
nearest HUD pixel in every direction.

## Data mapping

Every one of the 61 atomic fixture fields, and whether the shipping HUD carries
it (`base?` column, taken from `00-current-baseline.md`'s own mapping table).

| # | fixture field | where it appears | encoding | base? |
|---|---|---|---|---|
| 1 | Name `Nigel Ashby` | ledger `.who` | text 13px/600, `ink-bright`, +1.8px tracking | ✓ |
| 2 | Profession `Survivor` | ledger `.lab` | text 11px, `ink-label` | ✗ |
| 3 | Day `14` | ledger `.val` | text 12px, `ink-body` | ✓ |
| 4 | Time `21:47` | ledger `.clock` | text 15px/600 — largest glyph on screen | ✓ |
| 5 | Season `Autumn` | ledger `.lab` | text 11px, `ink-label` | ✓ |
| 6 | Place `Cheswick — residential` | ledger `.val` | text 12px, `ink-body` | ✓ |
| 7 | Weather `Light drizzle` | SENSE `SKY`, and `drizzle` as the TEMP note | text 12px + 11px note | ✗ |
| 8 | Temperature `8 °C` | SENSE `TEMP` | numeric 13px/600 + 12-sample chart + Δ12T `−4` | ✓ |
| 9 | Wind `NE 12 km/h` | SENSE `WIND` | text 12px, `ink-body` | ✗ |
| 10 | Light level `12%` (moonlight) | SENSE `LIGHT` | numeric 13px/600 + chart + Δ12T `−14` + note `moonlit` | ✗ |
| 11 | Flashlight `34%`, lit | SENSE `FLASHLT` | numeric + chart + Δ12T `−11` + note `lit ≈37t` | ✗ |
| 12 | STR `10` | LOAD row 8 | numeric 12px | ✓ |
| 13 | DEX `12` | LOAD row 8 | numeric 12px | ✓ |
| 14 | INT `9` | LOAD row 8 | numeric 12px | ✓ |
| 15 | PER `11` | LOAD row 8 | numeric 12px | ✓ |
| 16 | Speed `96` | LOAD row 7 `SPEED` | numeric 12px | ✓ |
| 17 | Focus `84` | LOAD row 4 | numeric 12px + chart + Δ12T `−16` + rate `−1/t` | ✓ |
| 18 | Pain `12` | LOAD row 2 | numeric 12px **amber** + chart + Δ12T `+12` + rate `+1/t` | ✓ |
| 19 | Morale `−3` | LOAD row 3 | numeric 12px + **diverging** chart w/ zero line + Δ12T `−7` + rate `−1/t` | ✗ |
| 20 | Encumbrance torso `12` | LOAD row 7 `ENCUM` | text `torso 12 · arms 4` | ✗ |
| 21 | Encumbrance arms `4` | LOAD row 7 `ENCUM` | same cell | ✗ |
| 22 | Move mode `walking` | LOAD row 6 `MOVE` | text 12px | ✓ |
| 23 | Noise made `8` | SENSE `NOISE` | numeric 13px/600 **amber** + chart + Δ12T `+6` + attribution `you` | ✗ |
| 24 | Head `20/24` | BODY row 1 | 12-sample chart + `20/24` + `83%` 13px/600, all neutral (inside band) | ✓ |
| 25 | Torso `45/60` | BODY row 2 | chart + `45/60` + `75%`, neutral | ✓ |
| 26 | L arm `8/30` | BODY row 3 | chart, newest bar 5px in `red`; `8/30` and `27%` both `red` | ✓ |
| 27 | L arm **critical** | BODY row 3, `.flag` | the literal word **`CRIT`** in `red`, 11px, +1.4px tracking, right-aligned in a reserved 44px cell | ✗ |
| 28 | L arm **bleeding** | BODY row 3b, directly beneath the bar | the literal word `bleeding` in `red`, 11px, indented 50px to sit under the chart | ✗ |
| 29 | L arm **bitten** | BODY row 3b, same line | the literal word `bitten` in `red` | ✗ |
| 30 | R arm `30/30` | BODY row 4 | chart flat at 20px + `30/30` + `100%` | ✓ |
| 31 | L leg `22/28` | BODY row 5 | chart + `22/28` + `79%` | ✓ |
| 32 | R leg `28/28` | BODY row 6 | chart flat + `28/28` + `100%` | ✓ |
| 33 | Stamina `5100/8700` | LOAD row 1 | chart + `5100/8700` + `59%` both **amber** + Δ12T `−3600` + rate `−300/t` | ✓ |
| 34 | Hunger `Peckish` | LOAD row 5 | text `peckish`, neutral (not yet actionable) | ✓ |
| 35 | Thirst `Thirsty` | LOAD row 5 | text `thirsty` **amber** + projection `→DEHYD ≈38t` | ✓ |
| 36 | Fatigue `Tired` | LOAD row 6 | text `tired` **amber** | ✓ |
| 37 | Warmth `Chilly` | LOAD row 6 | text `chilly` **amber** | ✗ |
| 38 | Effect `Bleeding (L arm)` | CONTACT `STATE` | `1 BLEEDING l·arm` — **priority ordinal** + `red` | ✓ |
| 39 | Effect `Bitten (L arm)` | CONTACT `STATE` | `2 BITTEN l·arm`, `red` | ✓ |
| 40 | Effect `Soaked` | CONTACT `STATE` row 2 | `3 SOAKED`, **amber** | ✓ |
| 41 | Effect `Tired` | CONTACT `STATE` row 2 | `4 TIRED`, amber | ✓ |
| 42 | Effect `Pain 12` | CONTACT `STATE` row 2 | `5 PAIN 12`, amber | ✓ |
| 43 | Wielded `fire axe` | CONTACT `WIELD` | text 13px `ink-bright` | ✓ |
| 44 | `32 bash` | CONTACT sub-row | text 11px `ink-muted`, indented 46px | ✗ |
| 45 | `0 cut` | CONTACT sub-row | same cell | ✗ |
| 46 | `to-hit +1` | CONTACT `WIELD`, right-pushed | text 11px `+1 hit` | ✗ |
| 47 | Sidearm `Glock 19` | CONTACT `STOW` | text 13px `ink-bright` | ✗ |
| 48 | `12/17` | CONTACT `STOW`, right-pushed | numeric 12px | ✗ |
| 49 | `9mm JHP` | CONTACT sub-row | text 11px `ink-muted` | ✗ |
| 50 | Target `zombie brute` | CONTACT sub-row under TARGET | text 11px `ink-muted` | ✓ |
| 51 | Target HP `62%` | CONTACT `TARGET` | **12-sample chart** + `62%` 13px/600 + Δ12T `−38` | ✓ (number only — the shipping bar is a permanently empty trough) |
| 52 | Target `4 tiles NE` | CONTACT `TARGET`, note cell | text 11px `ink-muted` | ✗ |
| 53 | Target status `moving` | CONTACT sub-row | text 11px `ink-muted` | ✗ |
| 54 | `3 hostiles visible` | ledger `.lg-r` | `3 HOSTILE` 13px/600 **red** | ✓ |
| 55 | `1 tracking` | ledger `.lg-r` | `1 TRACKING` 11px **amber** | ✗ |
| 56 | `SAFE MODE ON` | ledger `.lg-r` | text 12px + a 5px **cyan** dot (self-state, not a value) | ✓ (colour only) |
| 57 | Log, 6 rows | LOG column | 2px rail (cyan on newest only) + conditional stamp + text; **hue = type, opacity = age**, ramp 0.52 → 1.00 | ✓ |
| 58 | Minimap 11x11 | MAP column | ASCII grid; terrain is a pure **luminance ramp** L\* 30.5 / 51.0 / 69.7, no hue | ✓ |
| 59 | Mission marker NE | MAP row 2 col 10 | `^` at full `amber`; the two non-mission POIs are the same glyph at 42% alpha | ✓ |
| 60 | Hotbar, 9 actions `f r ' e p c i t 5` | `.foot`, 96px slots | 18px rounded cap + 11px label, all nine present | ✗ (shipping drops Examine and Throw; the fixed array holds Crouch and Map instead) |
| 61 | Hotbar slot 1 **unavailable** | `.foot` slot 1, widened to 120px | cap alpha `.15`→`.05`, ink `ink-label`→`ink-off` (L\* 69.7→34.8), plus the literal reason **`no gun`** in `red-dim` | ✗ |

**Zero fields are dropped.** 61/61 recoverable; the shipping HUD carries 37/61.

Three notes where the file is worth reporting precisely rather than flattering:

- **Four of the fifteen charts sit outside their painted band but are coloured
  neutral.** Decoding the band gradients into bar-space (border-box y minus the
  1px padding) and comparing to the measured `.now` height: STAMINA 12 vs band
  13–20 → amber ✓; PAIN 15 vs 0–6 → amber ✓; NOISE 13 vs 0–7 → amber ✓;
  L·ARM 5 vs 13–20 → red ✓; MORALE 6–10 vs 4–16 → inside, neutral ✓;
  FLASHLT 11 vs 7–20 → inside, neutral ✓. But **FOCUS** (8 vs 10–20),
  **LIGHT** (5 vs 16–20), **TEMP** (8 vs 15–20) and **TARGET** (12 vs 0–6) are
  all outside their bands and all rendered neutral. So the CSS comment's stated
  rule — "amber = outside its desired band" — is *not* what the file does. The
  defensible rule the file actually implements is "amber = outside band **and**
  actionable this turn": a dark night, a cold night and a slightly low focus pool
  are not decisions. That rule is better; it just isn't the one written down, and
  the comment should be corrected rather than the pixels.
- **The `%` column in LOAD is populated only for STAMINA.** PAIN, MORALE and
  FOCUS reserve the 28px cell and leave it blank, because a percentage of an
  unbounded pool is meaningless. Reserving rather than collapsing is what keeps
  the four rows' `Δ12T` and `RATE` columns aligned.
- **Log colour semantics are carried on two independent channels**, which is the
  direct fix for `00-current-baseline.md`'s defect 4. Hue carries type
  (neutral / red / red / red-dim / amber / amber, matching the fixture's
  good / bad / bad / bad-dimmer / caution / caution), opacity carries age
  (0.52 / 0.64 / 0.78 / 0.86 / 0.93 / 1.00). The shipping HUD flattens both
  21:46 rows to `c_dark_gray` after five turns while its opacity ramp says they
  are only 17% older — the two channels contradict. Here the spread is
  **48 points across six rows** versus the shipping **21**, and no row loses its
  type.

### Density claim

**Fields the current HUD does not carry at all.** Twenty-four of the 61 fixture
fields (the `✗` rows above) plus 33 derived values that do not exist anywhere in
the game today:

| kind | count | items |
|---|---|---|
| 12-turn history series | **15** | HEAD, TORSO, L·ARM, R·ARM, L·LEG, R·LEG, STAMINA, PAIN, MORALE, FOCUS, LIGHT, TEMP, NOISE, FLASHLT, TARGET |
| Δ12T change-over-window | **9** | STAMINA `−3600`, PAIN `+12`, MORALE `−7`, FOCUS `−16`, LIGHT `−14`, TEMP `−4`, NOISE `+6`, FLASHLT `−11`, TARGET `−38` |
| instantaneous rate | **5** | STAMINA `−300/t`, PAIN `+1/t`, MORALE `−1/t`, FOCUS `−1/t`, **L·ARM bleed `−6/t`** |
| forward projection | **2** | `→DEHYD ≈38t`, FLASHLT burn-out `≈37t` |
| noise source attribution | **1** | `you` — is that 8 noise *mine* or something else's |
| effect priority made explicit | **1** | the ordinals `1`…`5` on the STATE block |
| **derived total** | **33** | |

(A stricter count would add the 10 desired-band definitions and the morale zero
line — 11 more thresholds the engine must supply — for 44 derived. The headline
below uses the conservative 33 so the claim cannot be accused of padding; the
44-item variant is shown in the ladder.)

**Both occlusion figures.** This design: **15.98%** (arithmetic in the next
section). Shipping HUD: **31.02%** (`00-current-baseline.md:308`, union
643,226.56 / 2,073,600).

**The ratio.**

    design 10   61 fixture + 33 derived = 94 recoverable fields
                94 / 15.98 = 5.884 fields per percent of screen occluded

    baseline    37 fixture + 0 derived  = 37 recoverable fields
                37 / 31.02 = 1.193 fields per percent of screen occluded

    5.884 / 1.193 = 4.93x

**≈4.9x the information per unit of covered screen.** Because a headline that
depends on one accounting convention is not a headline, here is the same claim
under every convention, including the ones that hurt:

| accounting | d10 occ. | d10 fields/pt | base occ. | base fields/pt | ratio |
|---|---|---|---|---|---|
| contract rule (translucent at half weight) — **headline** | 15.98% | 94/15.98 = 5.884 | 31.02% | 37/31.02 = 1.193 | **4.93x** |
| same, counting all 44 derived incl. bands | 15.98% | 105/15.98 = 6.573 | 31.02% | 1.193 | 5.51x |
| alpha-integrated, both sides | 17.27% | 94/17.27 = 5.443 | 23.16% | 37/23.16 = 1.598 | 3.41x |
| raw rect at full weight, both sides, no discount anywhere | 30.19% | 94/30.19 = 3.114 | 31.02% | 1.193 | **2.61x** |

The claim's floor is **2.61x** and its ceiling is 5.51x. The bottom row is the
important one to read honestly: measured as bare rectangle area, this design
covers **30.19%** against the baseline's 31.02% — essentially the same
rectangle. The occlusion win is not that the HUD is smaller. It is that **no
pixel of this HUD is opaque**: peak alpha is 0.66 in a 26px band and 0.60
everywhere else, against the shipping HUD's 0.878 (0.747 with `.env-night`
live at 21:47), and that 69.81% of the screen — the entire centre — has nothing
over it at all, where the shipping HUD floats a 256x215 vitals panel directly
onto live terrain.

## Occlusion budget

Play area: `1920 x 1080 = 2,073,600 px²`.

Two painted regions. No third region, no vignette, no floating panel, no
`display:none` element to exclude.

| region | rect | area | alpha profile |
|---|---|---|---|
| `.ledger` | 1920 x 42 | 80,640 | gradient `.66` at y0 → `.60` at y26.04 → `0` at y42, **and** a `mask-image` that is opaque to 62% then ramps to transparent — so the last 15.96px is attenuated twice |
| `.console` | 1920 x 284 | 545,280 | flat `.60`, with a `mask-image` ramping `0 → 1` over the first 32px (y 796→828), then unattenuated to y1080 |
| **union rect** | | **625,920** | |

Both are unambiguously translucent, not near-opaque, so the contract's rule
applies at face value:

    half weight:  0.5 x 625,920 = 312,960  ->  15.093%

**Near-opaque correction.** Sub-rects *inside* the console composite their own
`0.80` plate over the `0.60` scrim, giving `1 − (1−0.80)(1−0.60) = 0.92` — that
is near-opaque and the contract says count it at full weight, not half:

| element | rect | count | area |
|---|---|---|---|
| `.map` plate | 143 x 143 | 1 | 20,449 |
| `.spark` plates | 49 x 22 | 15 | 16,170 |
| | | | **36,619** |

    correction = (1.0 − 0.5) x 36,619 = 18,309.5
    312,960 + 18,309.5 = 331,269.5

    331,269.5 / 2,073,600 = 0.15976  ->  15.98%   <- headline

**Two cross-checks, so the figure cannot be gamed by choosing a rule.**

*Alpha-integrated* — every pixel weighted by the alpha it actually composites at,
which is the harshest defensible reading of "count translucent scrims at half
weight":

    ledger, y 0→26.04   : 26.04 x mean(.66,.60)      = 26.04 x .63  = 16.4052
    ledger, y 26.04→42  : gradient .60(1−t) x mask (1−t) = .60(1−t)^2
                          integral over the band       = .60/3 = .20
                          15.96 x .20                                =  3.1920
    ledger effective height                                          = 19.5972
    console, y 796→828  : 32 x mean(0,.60) = 32 x .30                =  9.6000
    console, y 828→1080 : 252 x .60                                  = 151.2000
    console effective height                                         = 160.8000

    (19.5972 + 160.8000) x 1920                          = 346,362.6
    plate excess: (0.92 − 0.60) x 36,619                 =  11,718.1
                                                           358,080.7

    358,080.7 / 2,073,600 = 0.17269  ->  17.27%

*Raw rect, no discount at all* — the number to quote if someone rejects
translucency discounts entirely:

    625,920 / 2,073,600 = 0.30185  ->  30.19%

**Against the ratified baseline of 31.02%:**

| rule | d10 | baseline | d10 / baseline |
|---|---|---|---|
| contract (translucent half weight) | **15.98%** | 31.02% | **0.515x** — a 48.5% reduction |
| alpha-integrated both sides | 17.27% | 23.16% | 0.746x |
| raw rect both sides | 30.19% | 31.02% | 0.973x |

**Excluded: nothing.** Every element in the file lies inside one of the two
scrim rects — `.ledger-in` (y 4→26) inside the ledger; all six columns
(y 836→1034), both full-width rules, all five verticals and the hotbar row
(y 1044→1066) inside the console. There is no ink anywhere else on screen, so
the union of scrims *is* the union of the HUD.

**Two costs the percentage does not capture, stated so the figure is not read as
better than it is.**

1. **`backdrop-filter` degrades the covered band beyond its alpha.** The ledger
   applies `blur(2px) saturate(.72)` and the console `blur(2.5px) saturate(.72)
   brightness(.94)`. Terrain silhouettes and large shapes survive at 40%
   transmission; 32px-tile detail does not. This is the trade that lets the
   design run at 0.60 alpha instead of 0.88 — legibility bought with blur rather
   than with opacity — but a player cannot read individual tiles under the
   console band, only see that something is there.
2. **As a *carve* rather than an overlay, this design is slightly worse than the
   sidebar.** The shipping layout carves its terrain viewport in whole text
   cells: `(TERMX − 44) x (TERMY − 3 − 4) = 1568 x 960 = 1,505,280 px²`. This
   design needs no sidebar, but its bands are full width: 42px of ledger is 3
   rows and 284px of console is 18 rows, so a cell-carved equivalent gives
   `1920 x (1080 − 48 − 288) = 1920 x 744 = 1,428,480 px²` — **76,800 px²
   (5.1%) less terrain viewport** than the sidebar layout. The design's answer
   is that it should not be carved at all: it is a translucent overlay over a
   full-screen terrain viewport, which is what makes the 15.98% figure the right
   one and what removes the baseline's entire `TERMY x 16 != 1080` class of
   defect (off-screen hotbar, 7.52px black sliver, 4.18% unreserved paint). But
   if the project keeps the carve-based viewport, the honest number to compare
   is the one above and it is not a win.

## Motion

**Nothing in the file animates.** Grep for `@keyframes|transition|animation|
:hover|:active` returns zero matches. The mockup is not "at rest" — it is
static by construction, and that is the design position, not an omission. What
follows is the proposal, marked for what is in the file versus what is not.

**The hazard, named first.** A 12-column chart in a turn-based game shifts one
column **every single turn**. Fifteen of them shift together. That is precisely
the failure `research-infodesign.md` warns about twice — line 38: *"Do not
animate log scroll every turn or stamina decay every tick. Animate only discrete
state transitions"*; line 91: *"Never animate every-turn decay"* — and at 2000
turns it is 30,000 slide animations. A sparkline HUD is the single easiest way to
build the most fatiguing HUD in the project. Three damping measures, in the order
I would apply them:

1. **Do not animate the shift at all.** The chart jump-cuts; the reading head is
   pinned and the history simply redraws beneath it. This is what the file does
   and it is the default. The information in a sparkline is its *shape*, and
   shape is fully legible in a static frame — sliding it adds nothing a player
   reads.
2. **Widen the sample interval.** Sample once per *N* turns rather than every
   turn. At N=5 with a 12-column window the horizon becomes 60 turns and the
   chart changes 400 times per 2000 turns instead of 2000. The file's headers say
   `12T`, so N=1 as authored; the header string is the only thing that changes.
   This is a tuning decision belonging in JSON, not a redesign.
3. **Spend the entire motion budget on band crossings, never on the shift.**

Proposed animations, all of which are **not in the file**:

| what | trigger | property | duration | easing | note |
|---|---|---|---|---|---|
| band crossing | the newest sample moves from inside its desired band to outside, or into crit | `.now` bar `background-color` + the row's value `color`, neutral→amber or amber→red | 150ms | `cubic-bezier(0.25,0.46,0.45,0.94)` (research-infodesign:91) | **colour only** — no size, no position, so it cannot reflow a 6-column table. RCSS 6.2 transitions `background-color` and `color` natively. |
| return to band | value re-enters its band | — | **0** | — | deliberately silent. A two-way pulse doubles the motion for no added information; recovering is not a decision. |
| crit annotation appears | `bleeding` / `bitten` applies to a part | `opacity` 0→1 on the 14px sub-row | 120ms | ease-out | the shipping `status_bleed` ambient pulse (`repeats: 3`) is **dropped**: red at L\*59 is already preattentive (research-infodesign:13), and a pulse on a 14px row that will persist for hundreds of turns is peripheral motion with no event behind it. |
| log arrival | a new message | reuse the existing `hud_log_entry` slide-and-fade from below (`00-current-baseline.md:351-354`) verbatim, one row only | ≤150ms | ease-out | the only per-turn motion the design keeps, because a new log line genuinely *is* the discrete event of the turn. |
| the reading head | never | — | — | — | the 3x2 cyan foot is pinned `right:1px` and does not move. What moves conceptually is the data under it. A travelling marker is continuous motion; a fixed marker with changing data is a discrete redraw. |

Budget, per `research-infodesign.md:38`: at most 2 concurrent animations,
≤150ms each, ≤300ms total per turn cycle. On a quiet turn the count is **zero**.
The steady state of this HUD is still, and the mockup shows exactly that state
because it is the only state it has.

## Feasibility in RCSS

### Properties used that RCSS 6.2 does not have

| # | used in the file | contract status | replacement |
|---|---|---|---|
| 1 | `text-shadow`, four layers, on `.hud` | **not available** (contract line 78) | `font-effect: outline(1dp, #000000eb)`. See below — this is the one that matters. |
| 2 | `background: linear-gradient(...)` — both scrims, 5 rules, 10 band definitions, 2 legend swatches | property name only | `decorator: linear-gradient(...)`. Direct rename; the multi-stop bands (`.sp-mor` has 10 stops) need `linear-gradient` proper rather than the two-stop `vertical-gradient` shorthand, and that is supported (contract line 70). |
| 3 | `-webkit-mask-image` prefix duplication | n/a | drop the prefixed copy; `mask-image` is available (line 66) and takes a decorator: `mask-image: vertical-gradient(#000, transparent)`. |

Everything else is clean. `backdrop-filter` (line 66), `mask-image` (66),
`border-radius` (63), flexbox with `column-gap` (63), `opacity` (63),
`letter-spacing` (63), absolute positioning (63) are all on the available list.
And verified **absent** from the file: no `::before` / `::after`, no `content`,
no CSS grid, no `clip-path`, no `mix-blend-mode`, no `writing-mode`, no `calc()`,
no SVG, no web font. Every sparkline bar, every hairline, every legend swatch is
a real child div — 15 x 13 = 195 sparkline children plus 121 map cells — which is
the honest construction the contract asks for rather than a pseudo-element cheat.

**Why item 1 is the serious one.** The four-layer casing
(`0 0 3px rgba(0,0,0,.92)` + `0 1px 0 .85` + `±1px 0 0 .55`) is the *only*
contrast device in the design — there are no plates behind text. `font-effect`
accepts one shadow, not four, so the stack must collapse. Three of the four
layers are a 1px hard ring, so `outline` is the closer match than `shadow`. This
is load-bearing because of what the contrast math says: composite the 0.60 scrim
over worst-case bright terrain (`#90ee90`, the case `research-infodesign.md:48`
names) and the weakest inks fall to **`.chd` #5d6b71 at 1.23:1** and
**`.sub`/`.dim` #6d7c83 at 1.57:1**, against the 3:1 floor that line asks for.
Over dark terrain they are 3.31:1 and 4.22:1 and fine. The casing is what saves
them over grass, so losing three of its four layers is a real regression and must
be checked in-engine before this design ships, not assumed.

### The font

Consolas stands in for Source Code Pro per the contract, and here it is **not
cosmetic**, because this design puts values in fixed-px cells. Measured advance
ratio: `0.60 / 0.5498 = 1.0913`, **+9.13%**.

| string | cell | Consolas | Source Code Pro |
|---|---|---|---|
| `5100/8700` (LOAD `NOW`, 12px) | 62px | 59.38 (+2.62) | **64.80 (−2.80 — overflows)** |
| `You hit the zombie brute with your fire axe.` (LOG, 13px) | 342px | 323.29 (+18.71) | **352.00 (−10.00 — wraps)** |
| `lit ≈37t` (SENSE note, 11px) | 58px | 48.38 (+9.62) | 52.80 (+5.20) |
| `−300/t` (LOAD `RATE`, 11px) | 44px | 36.29 (+7.71) | 39.60 (+4.40) |
| `20/24` (BODY `HP`, 12px) | 46px | 32.99 (+13.01) | 36.00 (+10.00) |

Two cells break in the real font. The log one is worse than it looks: `.logline`
is a fixed `height: 24px`, so a wrapped message becomes a 30px line box inside a
24px row and collides with the next entry. Fix at port time: LOAD's `NOW` cell
62→68dp and the LOG column 400→412dp, both absorbable out of the 41px gutters.
Dropping to a 10px tier would fit, and is forbidden by the legibility floor.

### The real cost: history does not exist in the game today

Grep of `src/character.h`, `src/avatar.h` and `src/panels.cpp` for
`history|ring_buffer|circular_buffer` returns exactly one hit —
`consumption_history_t` (`character.h:73,1737`), a food log for morale,
unrelated. Every one of the 15 charts, 9 deltas, 5 rates and 2 projections in
this design is data the engine does not retain for a single turn. The bill:

**1. A ring buffer per tracked metric — owned by `Character`, not the HUD.**
15 series x 12 samples. Every fixture value fits `int16_t` (largest is stamina
8700 < 32767), so `std::array<int16_t, 12>` = 24 bytes per series, **360 bytes**
for all fifteen, plus a `uint8_t` head index each. It must **not** live in the
HUD: `game::draw_panels` calls `sidebar_hud_close()` whenever the HUD is toggled
off or the game is quitting (`game_misc.cpp:529-531`), so HUD-owned history would
silently reset and every chart would come back empty. Put a small
`struct metric_history` in `character.h` beside `consumption_history`.

**2. Where it ticks — and the trap that will be hit first.** *Not* in
`sidebar_hud_sync`. `sidebar_hud_sync( avatar & )` (`panels.cpp:1597`) is called
from `game::draw_panels` (`game_misc.cpp:534`), which is called from `game::draw`
(`:508`) — that is **per redraw, not per turn**. Sampling there advances all
fifteen charts on every `ui_adaptor` refresh: open the inventory, and stamina
history fills with twelve identical samples and every chart flatlines into a
straight line that says "nothing has changed for twelve turns". The sample site
must be `Character::process_turn()` (`src/character_turn.cpp:179`), guarded on
`calendar::turn` so it fires exactly once per turn. `sidebar_hud_sync` then only
*reads* the buffers.

**3. Save state.** CBN saves are JSON, so fifteen arrays of twelve small integers
is roughly 15 keys plus 180 decimal literals — **≈1 KB per character**, and only
the avatar needs it since no NPC HUD exists. Negligible in bytes. The real cost
is that it is new serialised state: `store`/`load` entries in
`savegame_json.cpp`, plus a **missing-key default that is not zeros**. Twelve
zeros on the PAIN chart reads as "pain has been zero for twelve turns", which is
a lie the first time anyone loads a pre-migration save. The buffer needs an
occupancy count and the producer must emit fewer than twelve bars —
left-padded with nothing — until the history is genuinely full.

**4. Producers in `src/panels.cpp` that must change.**

- **`hud_vitals` (`:846`)** — emit 12 HP samples plus the bleed rate per part,
  and **stop discarding what it already computes**: `label_hex` at **`:872`**
  holds `u.limb_color( bp.id(), true, true, true )`, which is exactly the
  bleeding / bitten / splint state this design prints as the words
  `bleeding · bitten`, and the `vbar_rml` call at `:874` never passes it. The
  per-part effect state is *already computed and thrown away every turn today*.
  This design needs it as text rather than as a hue, which is a strictly easier
  consumer than the one that was intended.
- **`hud_hotbar` (`:1282`)** — the signature is `auto hud_hotbar( avatar & )`,
  an **anonymous parameter**: the avatar is not read at all. The loop
  (`:1295-1305`) builds `[key] Label` from a `constexpr std::array<action_id,9>`
  and `continue`s past unbound actions; there is no disabled state and no way to
  express one. The `FIRE / no gun` slot this design shows requires naming the
  parameter, testing the wielded item for a gun, and emitting a `.slot.off`
  class plus a reason string. Same gap the baseline records at
  `00-current-baseline.md:262`.
- **`hud_topbar` (`:993`) + `hud_topbar_row2` (`:1112`)** — collapse into one
  `hud_ledger`. Add profession. Move `SPD` / `FOC` / `COND` out to LOAD.
- **`hud_botbar` (`:1152`)** — dissolved. Its EFFECTS run becomes the CONTACT
  `STATE` block with priority ordinals; its target readout becomes the TARGET
  chart row. `.tbar-fill`'s permanently-empty trough (baseline defect 2) is
  deleted rather than fixed.
- **`hud_log` (`:932`)** — keep the opacity ramp, re-tune it. Shipping uses
  `ramp_floor = 0.42` over a 14-row horizon (`:934-959`), which spreads six rows
  across only 21 opacity points; this design wants 0.52→1.00, **48 points over
  six rows**. Add minute-change stamp suppression. And **stop flattening rows
  older than five turns to `c_dark_gray`** (`messages.cpp:81-101`) — hue must
  carry type while opacity carries age, or the two channels contradict each other
  as they do today.
- **`hud_map` (`:986`)** — content unchanged, but it needs an `@` at the player
  cell (the shipping map has none, `00-current-baseline.md:259`) and a
  luminance-only terrain palette instead of the oter hues.
- **`sidebar_hud_apply_rect` (`:1509`)** — replaced. This design has two
  full-width rects instead of six mixed ones, so the whole
  `dock_width_pct` / `bar_width_pct` / `TERMY x fontheight != 1080` arithmetic
  goes away along with its off-screen hotbar and 7.52px black sliver. What it
  gains is six column widths, which is the one genuinely new piece of geometry.
- **New producers**: `hud_ledger`, `hud_console_body`, `hud_console_load`,
  `hud_console_sense`, `hud_console_contact`, plus a **`spark_rml()`** helper
  analogous to the existing `vbar_rml`, emitting the band class, twelve
  `<span style="height:Npx">` children and the `.now` colour. `spark_rml` is
  ~30 lines and runs 15 times per turn: 180 spans per turn against `vbar_rml`'s
  current ~6 bars. A real but small string-build increase on a surface that
  already rebuilds every region every turn.

**5. Derived values are cheap once history exists — and must be computed in C++,
not RML.** `Δ12T` is `buf[newest] − buf[oldest]`; the rate is that over the
window. The two projections (`→DEHYD ≈38t`, `FLASHLT ≈37t`) are linear
extrapolations of the same slope against a threshold, which means they are
**wrong whenever the slope changes** — which is why the file prints `≈` and why
the producer must keep it.

**6. The desired bands are content, not code.** Ten `(lo, hi)` pairs plus the
morale zero line. They belong in JSON beside the widget definitions
(`data/json/ui/sidebar.json`), because "what stamina counts as acceptable" is a
balance question and will be argued about.

**7. No new asset.** No bundled font beyond the Source Code Pro already shipped,
no texture, no image. The entire design is type, 1px hairlines and gradient
decorators.

## Weaknesses

**1. It assumes fluency, and the file admits it by shipping a key.** Six columns,
fifteen charts, four numeric channels per row (`NOW` / `%` / `Δ12T` / `RATE`) and
a shared-scale convention are a *dashboard*. **463.03px of the 1864px hotbar row
is a permanent legend** explaining `12-TURN HISTORY · DESIRED BAND · NOW ·
Δ12T = CHANGE OVER 12 TURNS`. A HUD that needs a key on screen at all times has
lost an argument about itself. `L·ARM 8/30 27% CRIT` with `bleeding · bitten`
underneath is legible to anyone; `MORALE −3 −7 −1/t` against a diverging chart
whose zero line is a 1px `.36`-alpha stop at bar-y 9–10 is not. And the cluster
count is already at the ceiling: 6 columns + 1 ledger = **7 visual clusters**
against the 4±1 that `research-infodesign.md:18` cites. The mitigation is real
but partial — every chart is *redundant*, the number is always printed beside it,
so a `hud_history` option could hide all fifteen without moving a single column.

**2. The 11px tier is a desk-monitor decision that fails outright on a TV, and
its contrast depends on the one property RCSS cannot reproduce.** 11px is exactly
the digest's floor with zero margin, on **93 elements** — every label, unit,
rate, note, timestamp and the whole legend. On a 55" 1080p panel (measured pixel
pitch **0.6342 mm**) an 11px Consolas x-height is 3.41mm; at 2.5m that subtends
**4.68 arcmin**, against the ~16–20 arcmin ISO 9241-303 asks for sustained
reading — roughly **four times too small**. At a 0.6m desk it is 19.52 arcmin and
perfectly fine. So this design is correct for a monitor and unusable from a sofa,
and raising the floor is not a rescale: LOAD's five-cell row already has **3px**
of horizontal slack at 1920, so a 27% type increase (11→14px) forces the table
from six columns to five or the console to two rows. Compounding it, the design
already fails its own shipping font (log line overflows by 10.00px, `5100/8700`
by 2.80px), and its weakest inks measure **1.23:1** and **1.57:1** over
worst-case bright terrain under the 0.60 scrim — saved only by a four-layer text
casing that must collapse to a single `font-effect` in RCSS.

**3. The layout has no give in either axis, so both a smaller screen and a
busier game state break it — silently.** *Vertically and at 1280x720:* every
column carries a hard `left`/`width` and the console a hard `top: 796`. At 720p
the console's top edge is **76px below the bottom of the screen** — all six
columns, the hotbar and the legend are gone and 42px of ledger is the entire HUD.
Convert to percentages and it still breaks: the table needs **1864px** of ink
where 1224px is available (a **34.3% shortfall**), `LOG`'s `left:1292` and
`MAP`'s `left:1733` both start past x=1280, and the console's content height is
an incompressible 284px (BODY alone is 6x25 + 14 = 164px of rows under a 22px
head) — **39.4% of a 720p screen** for the strip that was 26.3% at 1080p. This
is a ≥1600px design; below that it needs a genuine two-row reflow or a
column-drop order (MAP → LOG → SENSE). *Horizontally and at 12 effects:* the
`STATE` block is two 15px flex rows with **no `flex-wrap`**. The five fixture
effects measure 111.17 / 97.28 / 55.59 / 48.64 / 62.54px of ink — mean 75.04 plus
a 13px `margin-right` = 88.04px each. Twelve need ≈1056px against 254px of
usable row width = **5 rows**, i.e. 75px where 30px exists; CONTACT's ink already
ends at y=1020 with the column box closing at 1034, so 14px absorbs one extra row
and the remaining **31px overruns the `.r-foot` rule at y=1036 and the hotbar at
y=1044**. Because the row does not wrap, the *first* failure is horizontal: the
effects run past x=1251, through the 41px gutter, under the vrule at 1271 and
into the LOG column. This is the same class of silent overlap the shipping
`hud_botbar` already has (`00-current-baseline.md:437-444`), reproduced in a new
place, and it needs the fix the shipping code also lacks — a hard `4 + (+8)` cap,
or `flex-wrap: wrap` in a column allowed to grow.

**4. History is a permanent tax on every future metric, paid by people who are
not working on the HUD.** The 360 bytes and 1 KB of save are nothing. The cost is
structural: from the day this ships, **every new tracked stat has a second
question attached** — does it get a buffer, at what sample interval, with what
desired band, and does its band live in JSON? A ring buffer sampled in
`Character::process_turn()` is state that must be serialised, versioned,
defaulted for old saves, and kept correct across load, death, `debug`
teleport-and-time-skip, sleep fast-forward and any future turn-batching (the
adaptive sim-ticking work would need an explicit decision about whether a skipped
turn writes a sample or a gap — and a chart that silently treats a 500-turn sleep
as one turn is worse than no chart). Ten desired bands become a balance surface
someone must own. And the two projections are linear extrapolations that will be
confidently wrong at every inflection point; the `≈` is doing a lot of work and
players will still report them as bugs. None of that is visible in a mockup,
which is exactly why it belongs here: this is the most expensive design in the
set to *keep* correct, not to build.
