# 04 — Terminal Phosphor

## Thesis

Stop apologising for being a roguelike. This HUD is a **single-hue amber
phosphor terminal**: a 192 × 54 grid of 10 × 20 px character cells, framed in
real Unicode box-drawing glyphs, with scanlines, halation and faceplate
darkening — and **exactly one hue**, so that every distinction on screen is
carried by luminance, weight, glyph and position. Its argument is that a
monochrome, luminance-first interface is *simultaneously* the most readable
surface under stress and the only one that is colourblind-proof by
construction rather than by audit: there is no red/green pair to confuse
because there is no second hue to confuse it with, and `filter: grayscale(1)`
is a provable no-op on the information (§Register). The grid is not decoration,
it is the discipline — nothing straddles a cell, so the frame closes, columns
align across regions that were authored independently, and the producer can
never emit a string that silently shifts everything to its right by half a
character. What it trades away is **hue as a spare channel**. Every other
design in this set can reach for red when something is wrong; this one has
already spent that budget, so severity has to be paid for in luminance steps,
bar length, glyph tier and cell inversion — four redundant encodings where
others use one. It also trades away translucency: the ground is opaque
(§Layout), because a luminance-only encoding cannot survive an uncontrolled
luminance underneath it.

## Register

### Palette — the whole thing, six values on one hue

Every colour in the file is one of these. There is no seventh.

| token | hex | hue | rel. luminance Y | contrast vs ground | greyscale | role |
|---|---|---|---|---|---|---|
| `ph-0` ground | `#0c0800` | 40.0° | 0.0025 | — | `#080808` | panel ground, opaque |
| `ph-1` dead | `#3a2800` | 41.4° | 0.0242 | 1.41 : 1 | `#2b2b2b` | the unavailable hotbar slot's number |
| `ph-2` rule | `#7a5400` | 41.3° | 0.1048 | 2.95 : 1 | `#5b5b5b` | box frames, bar troughs, field tiles, oldest log rows |
| `ph-3` label | `#b87f00` | 41.4° | 0.2537 | 5.78 : 1 | `#8a8a8a` | chrome words, forest tiles, **intact** limbs |
| `ph-4` datum | `#ffb000` | 41.4° | 0.5231 | 10.91 : 1 | `#bfbfbf` | P3 amber — live data, damaged limbs, house tiles |
| `ph-5` peak | `#ffebbf` | 41.2° | 0.8444 | 17.03 : 1 | `#ededed` | critical, newest, wielded, player, mission marker |
| `ph-k` ink | `#120c00` | 40.0° | 0.0039 | — | `#0d0d0d` | glyphs **inside** inverted cells (16.59 : 1 on `ph-5`) |

`ph-1`…`ph-5` are pure shades and tints of `#ffb000` (DEC P3 amber, taken from
`research-aesthetic.md`), which is why the hue column spans **1.4°**. `ph-0`
and `ph-k` are the same hue crushed to near-black; the register's ground is
`#0A0A0A` in the research digest and `#0c0800` here so that even the unlit
cells sit on the amber axis.

**Why this is fully recoverable in greyscale, and why that matters.** The
encoding never consults hue. Desaturating the document maps the six tokens to
`#080808 / #2b2b2b / #5b5b5b / #8a8a8a / #bfbfbf / #ededed` — 8-bit steps of
35, 48, 47, 53 and 46 between neighbours, every one of them three to five times
the ~10-level just-noticeable difference for adjacent greys. No two tokens
share a luminance, so nothing can collapse: not for protanopia, deuteranopia or
tritanopia (which change hue, not luminance), and not for a monochrome CRT, a
sun-bleached laptop panel, or a player who has turned the saturation down.
`research-infodesign.md` §8 prescribes "test all palettes in monochrome; if two
states collapse, they fail" — this palette cannot fail that test, because
monochrome is what it already is. And the redundancy goes further than the
ladder: severity is *additionally* carried by the glyph tier
(`!!` › `!` › `^` › `·` › `+`), by bar length, by cell inversion and by
position, so any single channel can be destroyed and the state still reads.

### Type

**One face, one size, one weight, one line-height, for the entire HUD.**

| property | value | note |
|---|---|---|
| family | `Consolas` (stand-in) | game ships `Source Code Pro`, `data/font/SourceCodePro-Regular.ttf` |
| size | `18.19px` | Consolas advance is 0.549809em → **10.001 px/cell**, +0.19px drift across a full 192-cell row (0.01%) |
| line-height | `20px` | pinned to the cell so `│` and `┼` stems abut between rows and corners close |
| letter-spacing | `0` | any tracking would break the box rules into dashes |
| weight / style | normal only | no bold, no italic, anywhere |

There is no type scale. Hierarchy is 100% luminance, position and inversion —
which is the point: a terminal has one font, and everything a terminal can say
about importance it says with brightness and reverse video. In the shipping
font the same cell is `font-size: 16.667dp` (Source Code Pro's advance is
0.6em exactly) with **zero** drift.

### Frame

Real box-drawing characters inside the text runs. **There is not one CSS
border in the file.** The glyph set in use is
`─ │ ┌ ┐ └ ┘ ├ ┤ ┬ ┴ ┼`, and every junction is the correct member:

| junction | glyph | where |
|---|---|---|
| top rule crosses a panel's vertical | `┼` | row 2 at col 33 (SOMA) and col 157 (DOCK) |
| panel's internal rule meets its right border | `┤` | SOMA rows 10, 14 |
| panel's internal rule meets its left border | `├` | DOCK rows 15, 19 |
| bottom-right corner | `┘` | SOMA row 20 col 33 |
| bottom-left corner | `└` | DOCK row 24 col 157 |
| top-right corner | `┐` | LOG row 45 col 91 |
| a column starts | `┬` | LOG row 45 col 7 (the timestamp gutter) |
| a column ends | `┴` | KEYS row 52 at col 7 **and** col 91 |

Panel titles interrupt the rule the way a DOS TUI does — `──┤ SOMA ├──`,
`──┤ OVERMAP ├──`, `┤ POOLS ├`, `┤ EFFECTS ├`, `┤ TARGET ├`, `┤ ARMS ├`,
`┤ MESSAGE LOG ├`, `┤ FUNCTION KEYS ├` — so a section header costs **zero
rows**.

**The screen edge is the fourth wall.** Panels that touch the viewport carry a
rule only on their play-area-facing sides; drawing a second rule one cell
inside the bezel would be a redundant frame. The cost is that right-aligned
text would sit flush against x=1920, so every such field is padded one cell in
(`ON`, `1 TRACK`, `NE`, `4 TILES NE`, `62`).

### Bars

15 cells wide. Fill `▄` U+2584 (lower half block, 50% cell ink, 10.5px tall so
adjacent bars never merge vertically), trough `░` U+2591 (light shade, 25% ink)
at `ph-2`. Fill luminance is severity: **`ph-3` when the part is at full HP**
(intact recedes) and **`ph-4` when it is damaged** (damage advances). A
critical part does not get a colour — it gets the whole row inverted.

Quantisation is ±½ cell = ±3.33 pp; the exact figure is printed next to every
bar, so the bar carries the gestalt and the number carries the truth. Consolas
ships only `▀ ▄ █ ▌ ▐ ░ ▒ ▓` from U+2580–259F (verified in-browser), which is
why the mockup rounds to whole cells; **Source Code Pro carries all 32 block
elements including the seven eighth-blocks** (verified by parsing its `cmap`),
so the shipping build gets 8× sub-cell precision for free.

### CRT artefacts

| artefact | technique | strength |
|---|---|---|
| scanlines, phosphor | `repeating-linear-gradient` 3px pitch, 2px lit / 1px `rgba(0,0,0,.30)` | mean 10% darkening, over HUD regions only |
| scanlines, glass | same at `rgba(0,0,0,.075)` full-screen | mean 2.5% — the tilemap stays crisp |
| halation | duplicated glyph layer, `filter: blur(2.6px)`, `opacity .62`, painted **under** the crisp layer | bleed is proportional to emitted luminance for free, because the copy keeps its own ladder steps — the two inverted CRITICAL rows bloom hardest, which is the intent |
| faceplate curve | full-screen `radial-gradient(farthest-corner at 50% 50%, transparent 42%, rgba(0,0,0,.18) 72%, rgba(0,0,0,.46) 100%)` | mean 6.3% over the play area |
| phosphor persistence | **not** a decorative trail — the log's luminance ramp *is* the persistence curve (§Motion) | — |

`research-aesthetic.md` lists barrel distortion, true bloom and persistence
trails as shader-only. None of them are faked here; the three that CSS can do
honestly are done, and the fourth is spent on meaning instead of on a filter.

## Layout

Grid: **192 columns × 54 rows of 10 × 20 px**. Cell (c, r) is at
x = 10c, y = 20r. 1920 = 192 × 10 and 1080 = 54 × 20, both exact, so the grid
tiles the viewport with no remainder — unlike the shipping HUD, where
`TERMY × FONT_HEIGHT = 67 × 16 = 1072 ≠ 1080` and the hotbar runs 6.34px off
the bottom of the screen.

| region | cells | x | y | w | h | why here |
|---|---|---|---|---|---|---|
| **A** STATUS RULE | cols 0–191, rows 0–2 | 0 | 0 | 1920 | 60 | two 192-cell lines of world/character state plus the rule that carries the frame across the whole screen. Split at cols 33 and 157 — the *same* columns as the two panels below it, so the top bar is a header row for them, not a separate object |
| **B** SOMA | cols 0–33, rows 3–20 | 0 | 60 | 340 | 360 | body parts, pools and effects, hung off the top-left corner so it and the status rule read as one L. Left-hand because the crit row must be the first thing a left-to-right reader hits |
| **C** DOCK | cols 157–191, rows 3–24 | 1570 | 60 | 350 | 440 | overmap, target, arms. Right-hand, matching 30 years of muscle memory for where a roguelike sidebar lives |
| **D** MESSAGE LOG | cols 0–91, rows 45–51 | 0 | 900 | 920 | 140 | six rows and **exactly** six rows. The well is sized to its content, not the other way round |
| **E** FUNCTION KEYS | cols 0–191, rows 52–53 | 0 | 1040 | 1920 | 40 | the DOS F-key line, at the bottom edge where DOS put it. Its rule also closes the log panel — one row doing two jobs |

The five regions are pairwise disjoint, so their union is their sum. The player
tile at px (928, 512)–(960, 544) is cells (92.8–96, 25.6–27.2) — dead centre of
the clear zone, with the nearest HUD edge 570px away.

**Internal column grids**, because this is the part that makes the alignment
survive contact with a producer:

```
SOMA row        1 pad │ 6 label │ 1 gap │ 15 bar │ 1 gap │ 9 value │ 1 border  = 34
EFFECTS row     1 pad │ 2 glyph │ 1 gap │ 20 name        │ 9 site  │ 1 border  = 34
DOCK map row    1 border │ 6 pad │ 11 tiles × 2 cells │ 6 pad                  = 35
LOG row         1 mark │ 5 time │ 1 gap │ 1 gutter │ 1 gap │ 2 glyph │ 1 gap
                │ 79 text │ 1 border                                          = 92
KEYS row        1 pad │ 9 slots × (3 bracket + 1 key + 1 gap + 16 label) │ 2   = 192
```

The log's text field is **79 cells** — a VT220 line, minus one for the border.
The longest fixture message is 44 characters, so no message wraps and no
message is truncated, in a well that costs 6.2% of the screen rather than the
shipping dock's 18.3%.

## Data mapping

Every fixture field. Nothing is dropped.

| fixture field | where | encoding |
|---|---|---|
| Name `Nigel Ashby` | A r0 c1 | text, `ph-5` |
| Profession `Survivor` | A r0 c14 | text, `ph-3` |
| Day `14` | A r0 c39 | label `ph-3` + numeric `ph-4` |
| Time `21:47` | A r0 c56 | numeric, `ph-5` (only the clock is peak in the status rule) |
| Season `Autumn` | A r0 c49 | text, `ph-4` |
| Place `Cheswick — residential` | A r0 c64 | text, `ph-4` |
| Weather `Light drizzle` | A r0 c89 `WX LIGHT DRIZZLE` | label + text |
| Temperature `8 °C` | A r0 c108 | numeric |
| Wind `NE 12 km/h` | A r0 c113 `NE 12KM/H` | text |
| Light level `12%` | A r0 c124 `LUX 12%` | label + numeric |
| Flashlight `34%` | A r0 c132 `LAMP 34%` | label + numeric |
| STR / DEX / INT / PER `10 12 9 11` | A r1 c1–c30 | label `ph-3` + numeric `ph-4`, four fixed 6-cell fields |
| Speed `96` | A r1 c50 `SPD 96` | label + numeric |
| Focus `84` | B r12 POOLS | 15-cell bar (13 cells) + numeric |
| Pain `12` | B r19 EFFECTS | glyph `·` + roster row + numeric |
| Morale `-3` | B r13 POOLS | 15-cell **deviation** scale, zero axis at the cell-7/8 boundary, `▌` = the negative half-cell (so \|morale\| < 6.7 points), + signed numeric |
| Encumbrance torso `12`, arms `4` | A r1 c78 `ENC TORSO 12  ARMS 4` | label + numeric |
| Move mode `walking` | A r1 c35 `MOVE WALKING` | label + text |
| Noise made `8` | A r1 c60 `NOISE 8` | label + numeric |
| Head `20/24` | B r3 | bar 12/15 + `20/24`, fill `ph-4` |
| Torso `45/60` | B r4 | bar 11/15 + `45/60`, fill `ph-4` |
| L arm `8/30` | B r5 | bar 4/15 + `8/30`, **whole row inverted** |
| L arm **critical** | B r5 + r6 | cell inversion (`ph-k` on `ph-5`), plus the literal word `CRITICAL` on r6, also inverted. No hue is involved and none is needed: the inverted band is the single highest-luminance object on the screen and the only one with a filled background |
| L arm **bleeding, bitten** | B r6, **directly under the bar** | inverted continuation row `!! CRITICAL · BLEEDING · BITTEN`, 0 px from the limb it describes — versus 891px in the shipping HUD. Also rostered at B r15/r16 with `L ARM` in the site column |
| R arm `30/30` | B r7 | bar 15/15 + `30/30`, fill and value drop to `ph-3` — intact recedes |
| L leg `22/28` | B r8 | bar 12/15 + `22/28`, fill `ph-4` |
| R leg `28/28` | B r9 | bar 15/15 + `28/28`, `ph-3` |
| Stamina `5100/8700` | B r11 POOLS | bar 9/15 + `5100/8700` |
| Hunger `Peckish` | A r1 c95 | label + text |
| Thirst `Thirsty` | A r1 c111 | label + text |
| Fatigue `Tired` | A r1 c126 | label + text |
| Warmth `Chilly` | A r1 c141 | label + text |
| Effect `Bleeding (L arm)` | B r15 | `!!` `ph-5` + name + site column |
| Effect `Bitten (L arm)` | B r16 | `!` `ph-4` + name + site column |
| Effect `Soaked` | B r17 | `!` `ph-4` + name |
| Effect `Tired` | B r18 | `·` `ph-3` + name |
| Effect `Pain 12` | B r19 | `·` `ph-3` + name + magnitude in the site column |
| Wielded `fire axe` | C r20 `WIELD FIRE AXE` | text, `ph-5` (the thing in your hands is peak) |
| `32 bash / 0 cut / to-hit +1` | C r21 | numerics `ph-4`, units `ph-3` |
| Sidearm `Glock 19 12/17 9mm JHP` | C r22–r23 | `ALT` row + ammo row |
| Target `zombie brute` | C r16 | text, `ph-5` |
| Target HP `62%` | C r17 | 15-cell bar (9 cells) + numeric |
| Target `4 tiles NE` | C r16 right | text |
| Target status `moving` | C r18 `STATUS MOVING` | label + text |
| `3 hostiles visible` | A r1 right `3 VIS` | numeric |
| `1 tracking` | A r1 right `1 TRACK` | numeric |
| `SAFE MODE ON` | A r0 right | `ON` at `ph-5`; when it is off the word becomes `OFF`, inverted |
| Log × 6 | D r46–r51 | glyph tier (`+` `!` `·` `^`) = severity; luminance ramp `ph-2 → ph-2 → ph-3 → ph-3 → ph-4 → ph-5` = recency; `>` marker + a solid cursor block on the newest row. **The two encodings never contend because they are on different channels** |
| Minimap 11 × 11 | C r3–r13 | 2 cells per tile (20 × 20 px, effectively square). `.` field `ph-2`, `F` forest `ph-3`, `H` house `ph-4` |
| Player | C r8, map (5,4) | `@` on an inverted `ph-5` cell — the brightest object in the dock |
| Mission marker NE | C r4, map (1,9) | `^` on an inverted `ph-4` cell, one ladder step below the player so the two never confuse; captioned `^ MISSION MARKER … NE` at C r14. The two non-mission `^` POIs stay non-inverted at `ph-5` |
| Hotbar slot 1 `f` Fire | E r53 `[1]- FIRE  NO GUN` | **unavailable**: bracket `ph-1`, key replaced by `-`, label and reason `ph-2` — three redundant markers, all luminance |
| Hotbar slots 2–9 | E r53 | `[N]` `ph-2/ph-3`, key `ph-5`, label `ph-4`, 21 cells each |

Two deliberate presentation choices, stated so no one reads them as errors:

- **Chrome is upper-case, game data is verbatim.** Labels, section titles and
  state words are capitalised because that is the register; the six log
  messages keep their original sentence case, because they are prose and
  shouting prose is unreadable.
- **The overmap uses the contract's suggested glyph set** (`. F H ^ @`)
  unchanged, so it is directly comparable with design 00, but each glyph is
  placed on a 2-cell tile and assigned a ladder step instead of an oter colour.

## Occlusion budget

**28.80% of the 1920 × 1080 play area**, computed exactly as
`00-current-baseline.md` computes its ratified 31.02%: the union of each
region's intersection with the frame, anything opaque at full weight, divided
by 2,073,600.

The five regions are pairwise disjoint (A is rows 0–2; B is rows 3–20 in cols
0–33; C is rows 3–24 in cols 157–191; D is rows 45–51 in cols 0–91; E is rows
52–53), so the union is the sum:

| region | cells | rect | area |
|---|---|---|---|
| A STATUS RULE | 192 × 3 | 1920 × 60 | 115,200 |
| B SOMA | 34 × 18 | 340 × 360 | 122,400 |
| C DOCK | 35 × 22 | 350 × 440 | 154,000 |
| D MESSAGE LOG | 92 × 7 | 920 × 140 | 128,800 |
| E FUNCTION KEYS | 192 × 2 | 1920 × 40 | 76,800 |
| **union** | **2,986 cells** | | **597,200** |

    597,200 / 2,073,600 = 0.28800  ->  28.80%

Per region, as a share of the screen: dock **7.43%**, log **6.21%**, status
rule **5.56%**, soma **5.90%**, keys **3.70%**. Against the baseline's
dock **18.33%**, top bar **5.16%**, bottom strips **4.87%**, vitals **2.65%**.

**Against the 31.0% baseline: −2.22 points**, and the two numbers are directly
comparable because both are the opaque-region union under the same rule. There
is no alpha-weighted alternative to quote, because **every ground here is
alpha 1.0** — the alpha-weighted figure is also 28.80%. (The baseline's own
alpha-weighted figure is 23.16%; if you prefer that column, compare 23.16% to
28.80% and this design is the denser one. Both statements are true; the
opaque-union comparison is the one the baseline ratified.)

**Disclosure — the CRT glass.** Two full-screen translucent layers cover the
play area as well as the HUD: the weak scanline (mean α 0.025) and the
faceplate vignette (mean α 0.0627 over the play area, integrated numerically
over the actual gradient stops). Composited, they darken the 1,476,400 px² of
clear play area by a mean of 8.61%, i.e. 127,163 px² of effective coverage =
6.13% at full weight, **3.07% at the contract's half weight for translucent
scrims**. Charging them gives **31.87%**, still inside the 24–32% band and
still under the baseline's 31.02% + its own uncounted overlays. They are
reported separately rather than folded in because they obscure nothing: at
mean 8.6% darkening the terrain, the zombies and the player sprite remain fully
legible under them (visible in the render), which is not what "occlusion"
means. Excluded with reason, exactly as the baseline excludes its own
`#hud-vignette`.

**Where the density went.** This design is 2.22 points *leaner* than the
shipping HUD while carrying **every field in the fixture** — 53 rows in the
mapping table above, not one of them `DROPPED` — against the sixteen the
baseline's own Weakness 4 counts. The whole of that difference is one
structural fact: the
shipping dock spends 485px — 45% of its own height, 7.6% of the entire screen —
on empty log trough. Sizing the log well to six rows instead of to the
sidebar's height recovers more area than everything this design adds.

## Motion

**Nothing that carries information animates, and the resting state in the
render is the design.**

- **`#sl-all` rolls 3px over 9s, linear, infinite** — one scanline pitch, so the
  loop is seamless and the layer is *phase-invariant*: every frame of the
  animation is the identical picture. It is the only thing running in the
  mockup, and it cannot be caught in a bad state.
- **The log cursor is solid here, and blinks at 1.06s step-end in the game.**
  The contract screenshots one static frame; a blink phase that landed "off"
  would delete the newest-message marker, so the animation is documented rather
  than shipped into the mockup. 1.06s is the VT220's rate.
- **Crit inversion is a step, not a transition.** When a limb crosses into
  critical the row flips in one frame. A terminal cannot cross-fade a cell, and
  more importantly a 200ms fade would spend 200ms being neither state. This is
  the one place where the register's limitation is also the correct interaction
  design.
- **Phosphor persistence is the log's age ramp, and it steps per turn, not per
  frame.** A new line arrives at `ph-5` and steps down `ph-4 → ph-3 → ph-2`
  over the following turns. There is no tween: the decay is quantised to the
  turn, which is the unit the player actually reasons in, and it means the
  ramp never redraws on an unrelated frame.
- **One event animation is specified and deliberately not built:** on a net HP
  decrease, `#halo` opacity goes 0.62 → 0.85 and back over 120ms ease-out. A
  bloom flash is the physically correct CRT response to a sudden bright cell
  (the crit inversion) and it costs one property on one element. `hud_shake` is
  explicitly *not* wanted here — shaking a character grid tears the frame joins.
- Nothing pulses on the crit row. It is already the loudest object the register
  can produce; a pulse could only make it periodically *quieter*.

## Feasibility in RCSS

Everything in this file is RCSS 6.2. There is no forbidden property in it — no
CSS grid, no `::before`/`::after`, no `clip-path`, no `mix-blend-mode`, no
`text-shadow`, no SVG, no `calc()`, no web font.

| used here | RCSS 6.2 equivalent |
|---|---|
| absolute positioning, `white-space: pre`, `line-height` | identical |
| `background-color` on a `<span>` (cell inversion) | identical |
| `background-image: repeating-linear-gradient(...)` | `decorator: repeating-linear-gradient(...)` — same painting, different property name |
| `background-image: radial-gradient(farthest-corner ...)` | `decorator: radial-gradient(farthest-corner, ...)` |
| `filter: blur(2.6px)` on the halo layer | `filter: blur()` — verified available |
| `@keyframes` + `transform: translateY()` | verified available |
| fractional `font-size: 18.19px` | fine as `dp`, **but** the producer must emit it via `rml::dp()` (`src/rml_length.h`) — `std::to_string` honours `LC_NUMERIC` and emits `18,19dp` on comma-decimal locales, which RmlUi silently rejects |

**Fonts: zero new assets.** `data/font/SourceCodePro-Regular.ttf` is already
bundled and already the sidebar's face (`sidebar_hud.rcss:13`). Parsing its
`cmap` gives **128/128** of U+2500–257F (box drawing) and **32/32** of
U+2580–259F (block elements, including all seven eighth-blocks). `Terminus.ttf`
(120/128, 30/32) and `unifont.ttf` (128/128, 32/32) are also bundled as
fallbacks. Consolas — the contract's stand-in, and the only reason the mockup's
bars round to whole cells — carries only `▀ ▄ █ ▌ ▐ ░ ▒ ▓` from that block.

**The halo layer is the one thing worth arguing about.** Duplicating the whole
HUD subtree doubles the element count and the producer output. RmlUi 6.2 has
`font-effect: glow`, which gives per-glyph bloom on a single tree for a
fraction of the cost; the duplicate is only strictly needed for the background
bleed of inverted cells (a `font-effect` cannot bloom a `background-color`). A
shipping build should use `font-effect: glow` on `.r` and keep a duplicate for
`.inv4`/`.inv5` alone — three elements, not fifty-two.

**New C++ producer work, honestly.** This is not a restyle; it is a different
contract with `src/panels.cpp`.

1. **A cell-buffer producer is mandatory.** Every current producer builds its
   row by string concatenation. That is exactly the failure mode this design
   cannot tolerate: one character of overrun anywhere shifts the rest of the
   row off the grid and breaks the frame join at the far end. The producers
   need a `write(col, row, text, step)` buffer that pads and truncates to the
   field width, and one new `hud_frame` producer that emits the box-drawing
   rows so the eleven junction glyphs are computed **once** rather than
   duplicated across five producers.
2. **`sidebar_hud_apply_rect()` (`panels.cpp:1509`) must stop writing
   percentages.** All five rects here are whole-cell: `cell_w = TERMW / 192`,
   `cell_h = TERMH / 54`. This is the fix for the baseline's third structural
   defect, not a new feature.
3. **`hud_vitals` already computes the flag it needs and throws it away.**
   `label_hex` at `panels.cpp:872` carries bleeding/bitten/splint state and is
   never used; here it becomes the boolean that inverts the row. Separately,
   `vbar_rml`'s crit test at `panels.cpp:795-796` is
   `pct = o.cur * 100 / o.max` in *integers* and `crit = pct < 25`, so
   `8*100/30 == 26` and the fixture's critical arm never fires. That comparison
   has to become a real ratio or this design's signature encoding is dead on
   arrival for the same reason the baseline's is.
4. **`hud_hotbar( avatar & )` (`panels.cpp:1282`) ignores its argument** and has
   no disabled state. Slot 1 needs one: a per-slot `available` bool plus a
   short reason string (`NO GUN`).
5. **`hud_map` must emit 2-cell tiles** and flag which `^` is the active
   mission's marker rather than colouring every POI the same.
6. **`hud_topbar` and `hud_topbar_row2` merge** into one two-row, 192-cell
   composer, because the three column fields must line up with the panels below
   them and that alignment cannot be maintained by two independent producers.

## Weaknesses

1. **The grid is a tyrant, and the current producers would break it on the
   first long monster name.** `zombie brute` is 12 characters and the target
   name field is 12 cells. `shocker brute zombie` is 20. With string
   concatenation that pushes `4 TILES NE` off the right edge *and* moves the
   `│` at col 157 out of line with the `┼` above it, so the failure is not a
   clipped word — it is a visibly broken frame two panels away. The cell-buffer
   producer in §Feasibility is not a nicety; without it this design is fragile
   in a way the shipping HUD is not.
2. **1280 × 720 forces a choice and both options hurt.** Keep the 10 × 20 cell
   and the grid becomes 128 × 36: the 192-cell status rule no longer exists, and
   the five regions (which do not shrink) rise from 28.80% to **57.9%** of the
   screen. Scale the cell to the viewport instead (6.67 × 13.33) and the layout
   survives exactly, but the glyph drops to 12.1px — right on
   `research-roguelike.md`'s readability cliff, and `░` at `ph-2` in a 6.67px
   cell is one or two lit subpixels. The design is genuinely 1080p-and-up; at
   720p it wants a 160 × 45 re-author, not a scale factor.
3. **A luminance-only ladder has no slack at the bottom.** `ph-1` is 1.41 : 1
   and `ph-2` is 2.95 : 1 against the ground — both below WCAG AA for text, and
   `ph-2` carries the frame, the bar troughs, the overmap's field tiles *and*
   the two oldest log rows. On a glare-hit panel, a washed-out TN display, or
   for a low-vision player, the bottom two rungs collapse into the ground and
   the frame goes with them. The colourblind axis is won outright; the
   low-contrast axis is the price, and it is paid in exactly the places a
   colour design would have used a second hue to escape.
4. **Twelve effects reflow the panel instead of overflowing it — which is
   better, and still wrong.** The EFFECTS section is one row per effect with
   nothing beneath it, so SOMA simply grows from row 20 to row 27 and stays
   clear of the log at row 45; it degrades gracefully to about 19 effects. But
   the panel's bottom rule *moves*, so the one thing a character-grid HUD is
   supposed to buy — a frame you can find without looking — stops being true
   the moment the player gets wet, cold and poisoned at once. A fixed 8-row
   section with a `+N MORE` row would keep the frame still at the cost of
   hiding effects; this mockup has not made that call.
5. **28.80% of the screen is hard black.** The opaque ground is a deliberate
   consequence of the thesis — if luminance is the only channel carrying
   meaning, an uncontrolled luminance behind it is not a stylistic risk, it is
   data corruption — but it means five rectangles of the world are simply gone,
   where the shipping HUD's 88% alpha at least hints at what is underneath.
   Over a bright daytime tileset the panels read as holes punched in the map.
   This is the trade the thesis makes, taken with both eyes open, and it is the
   first thing a reviewer should push back on.
