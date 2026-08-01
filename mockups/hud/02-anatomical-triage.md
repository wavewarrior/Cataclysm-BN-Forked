# 02 — Anatomical Triage / Clinical Instrument

## Thesis

Cataclysm's one irreducible signature is that it **models a body** — six
independent hit-point pools, each with its own effects, encumbrance, splints and
bleed timers — and every HUD the project has shipped has flattened that body into
a stack of horizontal bars whose only spatial claim is reading order. This design
argues the opposite: if the game's subject is a body, **the HUD should be a body**,
and everything else on screen should be arranged around it the way a trauma bay
arranges itself around a patient. So the centre of the display is an anterior
anatomical plate with the six tracked regions drawn as shapes, each filled from
the feet up to its own waterline, colour-coded on a three-tier triage ladder
(STABLE / MONITOR / IMMEDIATE) that a trauma surveyor reads pre-attentively: not
"which bar is short" but "which part of him is wrong". The critical limb then
carries a complete diagnostic chain within one saccade — the shape is red, hatched
and white-keylined; a ring is drawn on the wound site; a red leader runs from that
ring to the numbers `8/30 27%`; and directly beneath the plate an IMMEDIATE
callout names the site, the two effects and the treatment order. Measured on this
file, the wound ring, its numeric tag and its treatment line all sit inside a
**212px** radius. The shipping HUD puts the same two facts — the `L ARM` bar and
the words `Bleeding (L arm)` — **891px** apart (`00-current-baseline.md`, Data
mapping), which is the defect this design exists to answer.

What it trades away is two things, both real. First, **the whole HUD moves off the
play area and onto one opaque plate on the right edge**, so nothing is ever read
over tile noise — but the trauma plate's centre then sits **815px** from the
player's tile at (944,528), and the player's eye must leave the world to read his
own body. The shipping HUD's vitals panel is 5px from the left edge and floats
*over* terrain; this one is legible and far away instead of contaminated and near.
Second, it trades **density for anatomy**: a body-shaped readout spends 318px of
vertical space on six numbers that six 18px bars could carry in 108px, and the
design pays for that with 11px type throughout — well under the 18dp the project
settled on, and under the 15dp `sidebar_hud.rcss:14-16` already rejected as
"unreadably tiny on a 1080p screen".

## Register

A **clinical instrument**: a machined dark plate, silkscreened small-caps labels
in a hairline-ruled grid, radiographic conventions on the body plate (R/L lead
markers, `ANTERIOR · 6 REGIONS` orientation note, annotation leaders with diamond
terminators), an oscilloscope for the one time-varying quantity, and a triage
ladder borrowed from START field triage for the body itself. The chrome is cold
and desaturated so that the three triage inks are the only saturated colour on
screen, and the one thing that is allowed to be loud — the red alarm bezel across
the top of the plate — is loud enough to be caught in peripheral vision without
being looked at.

### Palette

The stylesheet declares **67 distinct hex literals** (156 uses) plus **31 distinct
`rgba()` forms**. That is a finding, not a boast — see Weaknesses. The load-bearing
tokens, all read out of the file:

**Substrate and chrome**

| token | hex | role |
|---|---|---|
| plate base | `rgba(5,8,10,0.965)` | `#mon` background — 96.5% opaque, the whole column |
| plate ground | `#070c0f` | trauma plate field |
| scope ground | `#060d0f` | stamina oscilloscope field |
| scan ground | `#070d10` | 11x11 position scanner field |
| well ground | `#0a1216` | observation-chart wells |
| hairline | `#1e3038` | every panel border and `.k` section rule |
| hairline dim | `#152227` / `rgba(30,48,56,0.55)` | well border; row dividers in the wound and armament charts |
| mid rule | `#2e4a54` | chart baselines, scope graticule, neck border |
| machined edge | `#4a7a84` | left rail highlight, section index ticks, scanner corner crops, leader diamonds |
| slot edge | `#22383f` / `#16222a` | order slot border, disabled order slot border |
| depleted ground | `#151f24` | the unfilled part of a MONITOR-tier body region |

**Text ladder** — nine steps, from brightest to dimmest

| token | hex | role |
|---|---|---|
| patient name | `#f0f6f8` | `ASHBY, NIGEL` only |
| primary | `#e4eef0` | order keys, wielded weapon name, newest-note gutter |
| body | `#dce8ec` | `#mon` inherited text, body-region numeric tags |
| value | `#c7d8dd` / `#bcd0d6` | attribute values; emphasised env/act values |
| readout | `#9fb6bc` | `.k` right-hand readouts, identity rows, sidearm line |
| label | `#7e969e` | silkscreened section labels, ward line, order slot labels |
| note | `#6d868d` | wound-chart consequence column, armament sub-lines |
| micro-label | `#5d7880` | region tag keys, chart column labels, armament keys |
| separator | `#4e656d` / `#3f545c` / `#31454c` | `·` separators, lead markers, note timestamps, disabled key |
| caption | `#2f4a50` / `#2f4249` / `#2f5a58` | engraved captions (`SWEEP 25 mm/s`, `11×11 · MISSION NE`) |

**Triage ladder** — the only saturated inks

| token | hex | greyscale Y | role |
|---|---|---|---|
| IMMEDIATE | `#ff3b30` | 0.247 | tier-3 fill, alarm bezel, callout bar, severity pip, wound ring dot |
| IMMEDIATE edge | `#ff6b60` | — | tier-3 region border, wound ring, `bad` note rows |
| sterile keyline | `#ffe8e4` | — | 1.5px white ring around every IMMEDIATE shape; chip and callout numerals |
| IMMEDIATE text | `#ff5045` / `#ff9a90` / `#ff8078` | — | callout label / management line and target name / severity name |
| MONITOR | `#c8a02a` | 0.376 | tier-2 fill, observation bars, caution notes, lamp gauge, POI markers |
| MONITOR edge | `#8a6f1e` | — | tier-2 region border |
| STABLE | `#1f6e4a` | 0.119 | tier-1 fill (100% only) |
| STABLE edge | `#35946a` | — | tier-1 region border |
| STABLE text | `#8fc0a6` / `#4e7361` | — | 100% region tags and their percent suffix |
| instrument cyan | `#38e0c0` | — | stamina trace, live values in `.k` readouts |
| trace head | `#c8fff2` | — | the sweep head dot |
| cold | `#7ab6c8` | — | TEMP observation only — warmth is the one variable that is not on the harm axis |
| good | `#6fc79a` | — | favourable note rows |
| low severity | `#6d7a3a` / `#96a06a` | — | wound-chart tier 3 (`PAIN 12`) |
| mission | `#ffdc72` on `#241d0c` | — | mission marker cell on the scanner |
| ink on amber | `#3a2c05` / `#241a02` / `#4c3a06` | — | the `TORSO 45/60 75%` legend printed *inside* the amber fill |

Greyscale Y is relative luminance, computed for the three tier fills because the
tiers are the design's primary channel; see Weakness 3.

### Type

Two faces, thirteen sizes, three weights.

| face | used for | note |
|---|---|---|
| `Consolas` (→ `Cascadia Mono` → `Lucida Console`) | every number, every sentence, the patient name, order keys | metric stand-in for the game's bundled `Source Code Pro`, per the contract |
| `Franklin Gothic Medium` (→ `Segoe UI` → `Tahoma`) | every silkscreened small-caps label: `.k .t`, `.tagk`, `.oc .ol`, `.ak`, `.sl .lb`, `.erow .lb`, `.chip`, `.cimm` | **not bundled by the game** — see Feasibility |

Sizes actually declared: **7, 7.5, 8, 9, 9.5, 10, 10.5, 11, 12, 13, 15, 16, 18** px.
The base is `11px` on `#mon` with `letter-spacing: 0.2px`. The tiers that carry
weight:

| role | declaration |
|---|---|
| patient name | `700 18px Consolas`, `letter-spacing: 0.6px` |
| callout numeral | `700 16px Consolas` |
| order key / torso legend numeral | `700 15px Consolas`, `line-height: 20px` / `16px` |
| region numeric tag | `700 13px Consolas`, `line-height: 15px`; percent suffix `400 9px` |
| wielded weapon | `700 13px Consolas`, `line-height: 16px` |
| target name | `400 12px Consolas`, `line-height: 15px` |
| note text | `11px`, `nowrap`; note timestamp `9px` |
| section label | `600 9px` sans, `letter-spacing: 1.7px`, `text-transform: uppercase` |
| micro-label | `600 8px` sans, `letter-spacing: 1.2–1.4px` |
| engraved caption | `7.5–8px`, `letter-spacing: 0.9–1.2px` |

Letter-spacing does the work a second face usually would: the sans labels run
`1.2px`–`1.7px` open at 8–9px, the mono runs `0.2px`–`0.6px`.

### Border and frame

One device, applied consistently, and it is a **hairline box, never an ornament**.
Four rules, in order of weight:

1. **The plate edge.** A 2px machined rail at `x=1536` running the full 1080px,
   painted with a vertical gradient `#4a7a84 → #2e4a54 (18%…82%) → #4a7a84` so it
   reads as a milled aluminium extrusion catching light at both ends. Eight 5x1px
   `#4a7a84` index ticks hang off it at exactly the eight section boundaries
   (`y = 92, 132, 518, 606, 682, 786, 926, 1024`), so the rail is also the
   section index.
2. **The alarm bezel.** A 384x6px strip at `y=0`, `#ffe8e4` 1px over
   `#ff3b30 → #d42a22`, with `box-shadow: 0 3px 16px rgba(255,59,48,0.45)`. It is
   the patient's triage category rendered as hardware, and it is the only element
   designed to be read without being looked at.
3. **Instrument boxes.** `1px solid #1e3038` on the four things that are
   instruments rather than text — the trauma plate, the oscilloscope, the scanner,
   and (at `#152227`) each observation well. Nothing else gets a box.
4. **Section separation is a rule, not a box.** `.k` is a flex row of
   [small-caps label] [1px `#1e3038` hairline, `flex: 1 1 auto`, `min-width: 8px`]
   [right-hand readout], so the rule length is a function of the label and the
   readout and every section header self-fits. Row separation inside the wound and
   armament charts is a single `rgba(30,48,56,0.55)` `border-bottom`, suppressed on
   the last row.

No rounding above 2px anywhere except the order slots (`2px`) and the shapes on
the body plate, whose `border-radius` **is** their anatomy. No drop shadows except
the four glows that carry triage state (alarm bezel, chip, IMMEDIATE regions,
wound ring) and the trace head. The scanner's frame is deliberately *incomplete* —
four 6x6px `#4a7a84` corner crops sitting outside the box edge, a reticle
convention rather than a border.

## Layout

One region. `#mon` is `position: absolute; left: 1536px; top: 0; width: 384px;
height: 1080px` — measured `getBoundingClientRect()`: `x=1536 y=0 w=384 h=1080`.
That is **exactly 20.0% of 1920** and, at the shipped `FONT_WIDTH` of 8, exactly
**48 text cells** — so unlike the current dock's 44 cells the width is expressible
with no fractional-cell residue, and `100*48/240 = 20%` is exact in the percentage
form `sidebar_hud_apply_rect()` writes.

The only other painted element is `.bezel-shade`: `x=1518 y=0 w=18 h=1080`, a
`rgba(0,0,0,0) → rgba(0,0,0,0.55)` left-to-right gradient that reads as the plate
casting a shadow into the room. **No HUD pixel exists left of x=1518** — verified
by inspection of every rule; the alarm bezel's 16px shadow blur is the furthest
spill and it lands at x≈1520, inside the band already counted.

Inside the plate, `.sec` is `left: 14px; width: 356px`, so the content column is
**x 1550 → 1906** with a 14px margin on both sides. Every section is
`position: absolute` at a literal `top`. Measured:

| section | y | height | x | width | contents |
|---|---|---|---|---|---|
| `#patient` | 6 | 86 | 1550 | 356 | name + IMMEDIATE chip, identity row, ward row, attribute row |
| `#env` | 92 | 40 | 1550 | 356 | two 20px rows: `ENV`, `ACT` |
| `#trauma` | 132 | 386 | 1550 | 356 | `.k` header 14 + plate 318 + 6 gap + callout 44 |
| `#stam` | 518 | 88 | 1550 | 356 | `.k` header 14 + oscilloscope 72 |
| `#obs` | 606 | 76 | 1550 | 356 | `.k` header 14 + six 56px chart columns, 62 tall |
| `#wound` | 682 | 104 | 1550 | 356 | `.k` header 14 + five 18px rows |
| `#pos` | 786 | 140 | 1550 | 356 | `.k` header 14 + 126px body: 123px scanner + armament column |
| `#notes` | 926 | 98 | 1550 | 356 | `.k` header 14 + six 14px note rows |
| `#orders` | 1024 | 56 | 1550 | 356 | `.k` header 14 + nine 36x42px slots |

`6 + 86 + 40 + 386 + 88 + 76 + 104 + 140 + 98 + 56 = 1080` — the column fills the
screen exactly, with **zero slack**. That is a deliberate fit at this resolution
and a hard failure at any other; see Weakness 2.

**The trauma plate** — measured `x=1550 y=146 w=356 h=318`. Local coordinates
below are relative to the plate's border box. Nineteen `.bp` divs make the figure;
six body regions are built from two or three segments each, plus one structural
neck that is not a tracked region:

| region | segments (local `left,top,w,h`) | region span (local y) | fill |
|---|---|---|---|
| Head | skull `158,4,40,38`; jaw `165,34,26,16` | 4 → 50 (46px) | 83.3% |
| Torso | chest `124,56,108,42`; abdomen `130,96,96,46`; pelvis `140,140,76,44` | 56 → 184 (128px) | 75.0% |
| R arm | upper `111,64,22,62`; fore `103,123,19,60`; hand `97,180,18,24` | 64 → 204 | 100% |
| L arm | upper `223,64,22,62`; fore `234,123,19,60`; hand `241,180,18,24` | 64 → 204 (140px) | 26.7% |
| R leg | thigh `143,178,34,74`; shin `148,246,27,62`; foot `140,302,30,14` | 178 → 316 | 100% |
| L leg | thigh `179,178,34,74`; shin `181,246,27,62`; foot `186,302,30,14` | 178 → 316 (138px) | 78.6% |
| neck | `170,45,16,12` | — | structural, `#1e3138` |

**The waterline mechanic, which is the geometric heart of the design.** A region's
fill percentage is applied to the **union height of all its segments**, and the
resulting absolute waterline is then re-expressed as a *local* percentage of
whichever single segment it crosses; segments fully below the line are solid tier
colour, segments fully above are `#151f24`. Every number in the file checks out:

- Head: `4 + 46 × (1 − 0.8333) = 11.67`. Inside the skull (y 4→42, h 38) that is
  `(11.67 − 4)/38 = 20.2%` — the declared stop is `20.2%`.
- Torso: `56 + 128 × 0.25 = 88`. Inside the chest (y 56→98, h 42):
  `(88 − 56)/42 = 76.19%` — declared `76.2%`.
- L arm: `64 + 140 × (1 − 0.2667) = 166.67`. Inside the forearm (y 123→183,
  h 60): `(166.67 − 123)/60 = 72.8%` — declared `72.8%`.
- L leg: `178 + 138 × (1 − 0.7857) = 207.57`. Inside the thigh (y 178→252,
  h 74): `(207.57 − 178)/74 = 39.96%` — declared `40%`.
- R arm and R leg are 100%, so every segment is solid `.st` with no gradient at
  all.

The six limb segments are the only rotated elements: `transform-origin: 50% 0`
(the joint) and `rotate(±9deg)` on the uppers, `rotate(±6deg)` on the forearms and
hands, so the arms hang away from the torso from a shoulder pivot. Their measured
AABBs are therefore wider than their declared boxes — the L arm upper is declared
22x62 and measures **31.43 x 64.68** at `x=1774.14 y=209.28`. Declared geometry is
authoritative; the AABBs are noted so nobody reads the difference as an error.

Annotation, all local: R/L radiographic lead markers at `6,4` and `338,4`;
`ANTERIOR · 6 REGIONS` at `6,298`; four leader rules with 5x5px rotated diamond
terminators at `82,23`, `80,192`, `94,275`, `208,275`. The five region tags:

| tag | local box | screen box | content |
|---|---|---|---|
| HEAD | `2,10,78` right-aligned | `1553,157,78x25` | `20/24 83%` |
| R ARM | `0,179,78` right-aligned | `1551,326,78x25` | `30/30 100%` in `#8fc0a6` |
| R LEG | `14,262,78` right-aligned | `1565,409,78x25` | `28/28 100%` in `#8fc0a6` |
| L LEG | `262,262,80` | `1813,409,80x25` | `22/28 79%` |
| L ARM | `272,130,82` | `1823,277,82x26` | `8/30 27%` in `#ffe8e4`, key in `#ff8078` |

The torso is the exception: it is large enough to print its own legend *inside* the
amber fill (`TORSO` / `45/60` / `75%` in `#3a2c05`/`#241a02`/`#4c3a06`), which is
why there is no external torso tag.

**The critical-limb chain, measured.** Wound ring at `x=1788 y=283 w=17 h=17`
(centre `1796.5, 291.5`), an 18x1px `#ff6b60` leader running right from it, the
`L ARM 8/30 27%` tag centred at `(1864, 290)` — **67.5px** from the ring — and the
IMMEDIATE callout at `x=1550 y=470 w=356 h=44`, centre `(1728, 492)`, **211.9px**
from the ring. Limb, numbers, effect names and treatment order inside a 212px
radius, against 891px in the shipping HUD.

**Oscilloscope** `x=1550 y=532 w=356 h=72`: a four-layer graticule
(32px/24px major, 8px/8px minor), a mean line at local `y=29`, **43** rotated 2px
`#38e0c0` segments forming the trace with `opacity` ramping `0.28 → 0.96` left to
right (age), a 7x7px `#c8fff2` head dot with a 12px glow at local `349,26`,
`SWEEP 25 mm/s` engraved at `6,4`, and `59%` at `298,35`.

**Observation chart** `x=1550 y=620 w=356 h=62`: six 56px columns with a 4px
`column-gap` — `6 × 56 + 5 × 4 = 356`, an exact fit. Each column is a 10px
small-caps label, a 36px well, and a 16px value line. Inside the well: a
`rgba(56,224,192,0.10)` normal-range band at `top:9 h:6`, a 1px `#2e4a54`
**baseline at `top:12`**, and a 14px-wide bar at `left:20`. Deviation is signed by
direction from that baseline:

| column | bar | direction |
|---|---|---|
| NUTR | `top:13 h:8` | 8px below — depleted |
| HYDR | `top:13 h:12` | 12px below |
| REST | `top:13 h:12` | 12px below |
| TEMP | `top:13 h:8`, `#7ab6c8` | 8px below, cold |
| PAIN | `top:3 h:9` | **9px above** — the only variable where up is the harm direction |
| MOOD | `top:13 h:5` | 5px below |

**Position and armament** `y=800`, 126px tall. Scanner `x=1550 y=800 w=123 h=123`:
an 11x11 grid of 11px cells (`121 x 121` inner), rendered by 121 floated `<s>`
blocks. Cell content is the contract's suggested ASCII **verbatim** — verified cell
by cell against all eleven rows — with the player at row 5 col 4 as a `#1a1114`
cell carrying a 7px `#ff3b30` dot inside a 1.5px `#ffe8e4` ring and a 9px glow,
i.e. the player is rendered in the same IMMEDIATE grammar as his own arm; the
mission marker at row 1 col 9 as a gold-ringed `#241d0c` cell with a glowing
`#ffdc72` diamond; the two other POIs at row 3 col 1 and row 8 col 4 as plain
`#c8a02a` diamonds; forest `#26402c`, structure `#333a42`, field `#101a17`. A 1px
`rgba(56,224,192,0.30)` sweep line parks at local `y=74`, one cell below the
player's row. Armament column `x=1685 y=800 w=221 h=126`, three
hairline-separated blocks: 44px wielded, 30px sidearm, 52px subject — the last
carrying a 5px target-HP trough (`#1a1114` on `#3a1f1d`) with a **62%-wide
`#ff3b30` fill** and a 20px-pitch tick overlay.

**Chart notes** `y=940`, six 14px rows of [2px gutter][30px timestamp][319px text].
Widest note ink measured **274.91px** in the 319px text cell, single line, no wrap.

**Orders** `x=1550 y=1038 w=356 h=42`: nine 36px slots with a 4px `column-gap` —
`9 × 36 + 8 × 4 = 356`, again an exact fit. Slot 1 is `.off`: dead border
`#16222a`, flat `#0a1013` ground, key greyed to `#31454c`, a 42px `#4a2622` bar
rotated `-49deg` across it, and the reason (`NO GUN`) printed at 7px where the
label would be — so the slot states *that* it is unavailable and *why*, in place.

## Data mapping

Every field in `CONTRACT.md` has a row and **nothing is DROPPED**.

| fixture field | where it appears | encoding |
|---|---|---|
| Name `Nigel Ashby` | `#patient`, `.pname .nm` as `ASHBY, NIGEL` | numeric/text, `#f0f6f8` 18px — surname-first, the clinical convention |
| Profession `Survivor` | `#patient`, `.prow` | text |
| Day `14` | `#patient`, `.prow` | text |
| Time `21:47` | `#patient` `.prow`; again as `#obs` readout `rec 21:47` | text (twice: identity, and the observation timestamp) |
| Season `Autumn` | `#patient`, `.prow` | text |
| Place `Cheswick — residential` | `#patient`, `.prow.ward` | text, `#7e969e` — demoted one step below the identity row |
| Weather `Light drizzle` | `#env` `ENV` row | text |
| Temperature `8 °C` | `#env` `ENV` row | numeric, `#bcd0d6` emphasis |
| Wind `NE 12 km/h` | `#env` `ENV` row | text |
| Light level `12%` | `#env` `ENV` row as `LUX 12%` | numeric |
| Flashlight `34%` | `#env` `ENV` row as `LAMP 34%` + `.lampbar` | numeric **+ bar** — a 24x3px gauge whose `#c8a02a` stop is at exactly 34% |
| STR/DEX/INT/PER `10/12/9/11` | `#patient`, `.attr` | numeric, values `#c7d8dd` against `#4e656d` keys |
| Speed `96` | `#env` `ACT` row | numeric |
| Focus `84` | `#env` `ACT` row | numeric |
| Pain `12` | `#obs` PAIN column **above** the baseline; `#wound` row 5 as `PAIN 12` | bar (signed, the only upward deviation) + glyph `↑` + numeric + severity pip |
| Morale `-3` | `#obs` MOOD column as `−3` | bar (5px below baseline) + glyph `↓` + numeric |
| Encumbrance torso `12`, arms `4` | `#env` `ACT` row as `ENC T12/A4`; **again** as `ENC A4` in the WIELDED key row | numeric, twice — the arm figure is repeated where it changes your swing |
| Move mode `walking` | `#env` `ACT` row as `WALKING` | text |
| Noise made `8` | `#env` `ACT` row as `NOISE 8` | numeric |
| Head 20/24 | plate: skull+jaw, waterline at local `y=11.67`; tag `20/24 83%` | **position + area + colour + numeric** — MONITOR amber, 135° hatch overlay |
| Torso 45/60 | plate: chest+abdomen+pelvis, waterline `y=88`; legend printed inside the fill | position + area + colour + numeric (`45/60` at 15px, the largest number on the plate after the callout) |
| L arm 8/30 | plate: three IMMEDIATE segments, waterline `y=166.67`; tag `8/30 27%` | position + area + colour + numeric |
| L arm **critical** | plate region border `#ff6b60` + 1.5px `#ffe8e4` keyline + 18px red glow + 3px/8px 34%-alpha hatch; wound ring; red leader; `IMMEDIATE` callout; `IMMEDIATE` chip in the patient header; the alarm bezel across the top of the whole plate; `triage IMMEDIATE` in the `#trauma` readout | **six redundant channels** — colour, texture, keyline, glow, position and text. The encoding the shipping HUD computes and then never fires (`8*100/30 == 26`, crit needs `< 25`) is the single fact this design is built to make unmissable. |
| L arm **bleeding, bitten** | `#trauma` callout: `BLEEDING + BITTEN · CONTROL HAEMORRHAGE, THEN DISINFECT`, 212px from the wound ring; `#wound` rows 1–2 with site `L ARM` and onsets `21:47` / `21:44` | text + colour + severity pip + **proximity** — against 891px centre-to-centre in the shipping HUD |
| R arm 30/30 | plate: three solid STABLE segments; tag `30/30 100%` in `#8fc0a6` | position + area + colour + numeric |
| L leg 22/28 | plate: thigh waterline `y=207.57`; tag `22/28 79%` | position + area + colour + numeric |
| R leg 28/28 | plate: three solid STABLE segments; tag `28/28 100%` | position + area + colour + numeric |
| Stamina `5100 / 8700` | `#stam` readout `5100 / 8700 · 59%`; oscilloscope trace + `59%` at the head | numeric + **trace** (43-segment history polyline with an age opacity ramp) + mean line + glowing head dot |
| Hunger `Peckish` | `#obs` NUTR | bar (signed) + glyph `↓` + text |
| Thirst `Thirsty` | `#obs` HYDR | bar + glyph + text |
| Fatigue `Tired` | `#obs` REST; also `#wound` row 4 with onset `20:12` | bar + glyph + text + severity pip |
| Warmth `Chilly` | `#obs` TEMP, in `#7ab6c8` | bar + glyph + text + **colour axis change** — the one variable pulled off the harm ladder onto a cold axis |
| Effect `Bleeding (L arm)` | `#wound` row 1, `lv1` | severity pip (`#ff3b30`, glowing) + name + site + onset `21:47` + consequence `ARTERIAL — UNTREATED` |
| Effect `Bitten (L arm)` | `#wound` row 2, `lv1` | pip + name + site + onset `21:44` + `INFECTION RISK` |
| Effect `Soaked` | `#wound` row 3, `lv2` | pip (`#c8a02a`) + site `WHOLE BODY` + onset `21:38` + `WARMTH ↓` |
| Effect `Tired` | `#wound` row 4, `lv2` | pip + site `SYSTEMIC` + onset `20:12` + `STAMINA REGEN ↓` |
| Effect `Pain 12` | `#wound` row 5, `lv3` | pip (`#6d7a3a`) + site `SYSTEMIC` + onset `21:47` + `SPEED ↓ · ACCURACY ↓` |
| Effect count | `#wound` readout `active 5` | numeric |
| Wielded `fire axe` | `#pos`, `.arm .a1` as `FIRE AXE` at 700 13px | text — the largest type in the section |
| `32 bash / 0 cut / to-hit +1` | `#pos`, `.a1` sub-line | numeric |
| Sidearm `Glock 19 12/17 9mm JHP` | `#pos`, `.a2`, keyed `SIDEARM / STOWED` | text + numeric; the `STOWED` key is why it is quieter than the wielded block |
| Target `zombie brute` | `#pos`, `.a3` as `ZOMBIE BRUTE` in `#ff9a90` | text + colour |
| Target HP `62%` | `#pos`, `.thp` fill at width 62% + `62% INTACT` | **bar** (a real one — the shipping HUD's equivalent trough is permanently empty because `.tbar-fill` is inline) + numeric |
| Target `4 tiles NE` | `#pos`, `.a3` key row right side | text |
| Target status `moving` | `#pos`, `62% INTACT · MOVING` | text |
| `3 hostiles visible` | `#pos` readout `3 HOSTILE` | numeric |
| `1 tracking` | `#pos` readout `1 TRK` | numeric |
| `SAFE MODE ON` | `#pos` readout `SAFE` in `#38e0c0` | colour + text |
| Log row 1 (21:46, hit) | `#notes` row 1, `.good` `#6fc79a`, `opacity 0.50` | text + colour + **opacity age ramp** |
| Log row 2 (21:46, claws) | row 2, `.bad` `#ff6b60`, `opacity 0.62` | text + colour + opacity |
| Log row 3 (21:47, bleeding) | row 3, `.bad`, `opacity 0.74` | text + colour + opacity |
| Log row 4 (21:47, sharp pain) | row 4, `.baddim` `#c25a52`, `opacity 0.84` | text + colour + opacity — dimmer hue *and* higher opacity, so age and severity stay separable |
| Log row 5 (21:47, shambler) | row 5, `.caut` `#c8a02a`, `opacity 0.92` | text + colour + opacity |
| Log row 6 (21:47, thirsty) | row 6, `.cautlit` `#d4ae3c`, `opacity 1.0`, **white `#e4eef0` gutter bar** | text + colour + glyph — the newest row is marked by position *and* a gutter rule, not by being the only bright one |
| Minimap 11x11 | `#pos` scanner, 121 11px cells | position + colour, contract content verbatim |
| Player position | row 5 col 4, red dot + white ring + glow | position + colour — the patient marker uses the IMMEDIATE grammar |
| Mission marker NE | row 1 col 9, `#241d0c` cell, gold inset ring, glowing `#ffdc72` diamond | position + colour + glyph |
| Hotbar `f` Fire, unavailable | `#orders` slot 1: dead border, flat ground, greyed key, `-49deg` strike bar, reason `NO GUN`; plus the readout `slot 1 unavailable` | **colour + glyph + text** — state *and* cause, in place |
| Hotbar `r` `'` `e` `p` `c` `i` `t` `5` | `#orders` slots 2–9 as `RELD RUN EXAM PICK CRFT INVN THRW WAIT` | glyph (key at 700 15px) + 8px label |

Two notes so nothing reads as an error. The wound chart's **onset times**
(`21:47 / 21:44 / 21:38 / 20:12 / 21:47`) are not in the fixture; they are derived
values consistent with it — bleeding and pain begin on the claw at 21:46–47, the
bite precedes it, soaked and tired are older — and they exist because a triage
chart without onset times is not a triage chart. `effect::get_start_time()`
(`effect.h:273`) makes them producible. The **consequence column** (`WARMTH ↓`,
`STAMINA REGEN ↓`, `SPEED ↓ · ACCURACY ↓`) is likewise derived, and is the design's
answer to the shipping HUD's flat `::`-joined effect run: an effect the player
cannot act on is noise, so every row states what it is doing to him.

## Occlusion budget

**20.47% of the 1920x1080 play area**, against the ratified baseline of **31.0%** —
a 10.55-point reduction, or **34.0% less screen** than the current HUD.

Method identical to `00-current-baseline.md`: union of each painted rect's
intersection with the frame, near-opaque at full weight, translucent scrims at
half weight, divided by `1920 x 1080 = 2,073,600 px²`.

| element | rect | raw area | weight | counted |
|---|---|---|---|---|
| `#mon` | 384 x 1080 @ (1536,0) | 414,720.00 | 1.0 (α 0.965) | 414,720.00 |
| `.bezel-shade` | 18 x 1080 @ (1518,0) | 19,440.00 | 0.5 (scrim, α ramps 0 → 0.55) | 9,720.00 |
| overlap | — | — | — | 0.00 (1518–1536 and 1536–1920 are disjoint) |
| **union** | | | | **424,440.00** |

    414,720 / 2,073,600 = 0.20000  ->  20.00%   (the plate alone, exactly 20%)
      9,720 / 2,073,600 = 0.00469  ->   0.47%   (bezel scrim at half weight)
    424,440 / 2,073,600 = 0.20469  ->  20.47%

    31.02% - 20.47% = 10.55 points ;  1 - 20.47/31.02 = 34.0% relative reduction

Nothing else is excluded, because nothing else exists: there is no vignette, no
floating vitals panel, no top bar, no bottom bar, no separate hotbar strip. The
four glows (alarm bezel, chip, IMMEDIATE regions, wound ring) and the alarm
bezel's 16px `box-shadow` blur all land at x ≥ 1520, inside the bezel band already
counted, so no glow adds occluding area.

**Alpha-weighted alternative: 19.56%**, against the baseline's 23.16%. `#mon`'s
substrate is `rgba(5,8,10,0.965)`; the three gradient layers over it are
low-alpha (0.022–0.60) and the top-fade layer is bounded to the first 240px, so
0.965 is the honest plate figure. The bezel's mean alpha across its ramp is 0.275.

    414,720 x 0.965 = 400,204.80
     19,440 x 0.275 =   5,346.00
            total   = 405,550.80  /  2,073,600 = 19.56%

Quote **20.47% vs 31.0%**; the alpha-weighted pair only against another
alpha-weighted design.

**The terrain reconciliation, which is where this design's real gain is.** The
engine carves the terrain viewport in whole text cells. A 48-cell sidebar with
`sidebar_hud_top_rows() == 0` and `bottom_rows() == 0` carves
`(240 − 48) x 67 = 192 x 67` cells = `1536 x 1072 px`, spanning x 0→1536,
y 0→1072. The plate occupies **exactly** the region the engine reserved:

| | area | % | cause |
|---|---|---|---|
| HUD painted over live terrain | 19,440 (at half weight 9,720) | 0.94% / 0.47% | the bezel scrim only — and it is a shadow, deliberately translucent. Baseline: **86,698 px² / 4.18%** of hard chrome over unreserved terrain. |
| carved to black, painted by nobody | 12,288 | 0.59% | `67 x 16 = 1072 != 1080`, so an 8 x 1536 sliver along the map's lower edge is reserved and unpainted. Baseline: 11,791 px² / 0.57% — the *same* defect, inherited from the cell/pixel mismatch, neither caused nor fixed by this design. |

The three geometry bugs the baseline attributes to percentage-vs-cell drift — the
top bar painting 20.19px over live terrain, the hotbar running 6.34px off the
bottom of the screen, and the hotbar overlapping the bottom bar by 6.36px — **cannot
occur here**, because there are no horizontally-anchored strips at all. The only
vertical percentage in the layout is the plate's `height: 100%`, and the only
horizontal one is `left: 80% / width: 20%`, both exact at any resolution.

## Motion

**The mockup declares no motion at all** — zero `transition` declarations, zero
`@keyframes`, zero `animation` properties (verified by search). Every value in the
file is a resting state, which is what the contract asks for and also what this
design wants: a clinical monitor that twitches is a broken monitor. Everything
below is therefore **proposed**, not present in the file, and each item names the
existing engine mechanism it would ride.

- **Triage escalation — the only thing that pulses.** When a region crosses into
  IMMEDIATE, the alarm bezel, the `IMMEDIATE` chip and the wound ring pulse
  together: `opacity 1.0 → 0.55 → 1.0`, **1000ms**, `sine-in-out`. Resting state is
  full opacity, which is exactly what the screenshot shows. It must be **capped in
  repeat count**, not infinite — `hud_anim`'s `status_bleed` spec was already
  changed to `repeats: 3` for precisely this reason (`00-current-baseline.md`,
  Motion), and an uncapped red pulse in the corner of the eye for the rest of a
  playthrough is the failure mode. Trigger is the *transition* into the tier, not
  the tier itself.
- **Waterlines settle, they do not jump.** A region's fill animates to its new
  level over **280ms**, `sine-in-out` — long enough to be seen as a change, short
  enough not to lag the turn. **This has an architectural cost that must be paid up
  front:** RCSS cannot transition a colour stop inside a `decorator` gradient. So
  the waterline cannot ship as the gradient the mockup uses; each segment needs a
  child fill div whose `height`/`top` is transitioned, with the parent's
  `border-radius` and `overflow: hidden` clipping it to the anatomy. That is a
  markup change, not a stylesheet change, and it is the price of wanting this
  particular motion.
- **The stamina trace scrolls; it does not tween.** One sample enters at the right
  per turn and the age opacity ramp shifts one step. That is a data update, and
  animating it would be a lie about the sampling rate. The head dot gets the only
  motion in the section: `box-shadow` spread breathing over **600ms**
  `sine-in-out`, gated on stamina below 30% so it is silent at the fixture's 59%.
- **Chart notes arrive from below.** Reuse the existing `hud_log_entry` spec — a
  slide-and-fade from beneath, already implemented in `hud_anim` — at **240ms**,
  `quadratic-out`. The five-step opacity ramp (`0.50 → 1.0`) is a *static* age
  encoding and must not be animated, or every new message would reanimate all six
  rows.
- **The scanner sweep is a state indicator, not decoration.** It sweeps local
  `top: 0 → 121px` over **2000ms** `linear`, looping, **only while safe mode is
  off**. With `SAFE` on it parks where the file shows it (local `y=74`), so a
  moving sweep line means "you are not being watched for you". Motion earns its
  place by carrying a bit.
- **Nothing else animates.** Not the observation bars (they are a chart; a chart
  that slides is unreadable), not the scanner cells, not the order slots, not the
  section rules.
- **One trap to avoid, inherited.** The shipping sheet declares
  `transition: background-color 0.8s, opacity 0.8s` on six region roots
  (`sidebar_hud.rcss:530-532`). If that rule is carried over onto `#mon` it will
  swallow the 280ms waterline and the 1000ms alarm easing wholesale — the exact
  property-scope collision `plans/hud-coherence.md` §1.6 documented. Any
  transition declared in this design must be scoped to the specific properties
  and elements named above, and must never be declared on an element that
  `hud_anim` writes inline.

## Feasibility in RCSS

Audited rule by rule against `CONTRACT.md`'s verified RmlUi 6.2 lists.

**Uses nothing from the forbidden list.** No CSS grid, no `::before`/`::after`, no
`content`, no `clip-path`, no `mix-blend-mode`, no `text-shadow`, no SVG, no
`writing-mode`, no `calc()`, no web fonts — verified by search. The layout is
absolute positioning plus flexbox with `column-gap`, all available. `box-shadow`
(including `inset` and spread), `border-radius` with four values, `opacity`,
`letter-spacing`, `transform: rotate()`, `transform-origin`, `float`,
`white-space: nowrap`, `min-width`, `box-sizing`, `overflow: hidden`,
`text-align`, `:last-child`, the `*` selector and inline `style` attributes are
all supported. `text-transform: uppercase` is not on either contract list but is
already in use in-tree at `mainmenu_theme.rcss:98`, and `transform-origin` is
written by `hud_anim.cpp:176`, so both are proven.

**Four mechanical translations, all one-for-one:**

1. **`background:` with gradients → `decorator:` layers.** Every gradient in the
   file is a CSS `background` shorthand and must become an RCSS `decorator` list
   plus a separate `background-color` for the solid final layer — RCSS's
   `background` only aliases a solid colour (as used at `aim_examine.rcss:13`,
   `string_editor.rcss:17`). The mapping is direct and the layering order is
   preserved, because RCSS decorators stack in declaration order:
   `linear-gradient` → `decorator: linear-gradient(...)`,
   `repeating-linear-gradient` → `decorator: repeating-linear-gradient(...)`,
   `radial-gradient(circle at 50% 42%, ...)` → `decorator: radial-gradient(circle
   farthest-side at 50% 42%, ...)`. This covers `#mon` (4 layers), the trauma
   plate (3), the oscilloscope graticule (5), the observation wells (2), the body
   fills and hatches (2 each), the callout wash, the order slots, the lamp gauge,
   the target-HP tick overlay and the bezel shade. **The `135deg` hatch on the
   MONITOR and IMMEDIATE regions is the one to check first** — it is
   `repeating-linear-gradient` at an angle over a rounded, rotated element, and it
   is the design's colourblind fallback, so if the angle or the clipping is wrong
   the fallback silently disappears.
2. **`font:` shorthand → longhands.** RCSS registers no `font` shorthand (there is
   not one instance in `data/gui/`). All 22 `font:` declarations expand to
   `font-weight` / `font-size` / `font-family`.
3. **`border: 1px solid #x` → `border: 1dp #x`.** RCSS has no line-style keyword,
   per the baseline's finding; the repo's convention is `border: 1dp {{token}}`
   (`sidebar_hud.rcss:107`, `bionics.rcss:45`, and 18 other files). `1dp == 1px` at
   1080p non-HiDPI.
4. **`text-decoration: none` on `<s>` is inert and should be deleted.** The file
   sets it in three places only to defeat Chrome's UA sheet striking `<s>`; RmlUi
   has no UA sheet. Likewise `font-style: normal` on `<i>`/`<em>`. Removing them
   changes nothing in the game and removes six lines of confusion. Note that `<s>`,
   `<u>`, `<b>`, `<i>`, `<em>` are used purely as anonymous inline containers with
   no semantics — RmlUi treats them as unknown elements defaulting to
   `display: inline`, which is exactly what is wanted, but `.scan s` must keep its
   explicit `display: block` because RmlUi's default is `inline`, not `block`
   (`StyleSheetSpecification.cpp:306`).

**One real font problem.** `Franklin Gothic Medium` carries every silkscreened
label in the design and the game bundles exactly three faces —
`Terminus.ttf`, `SourceCodePro-Regular.ttf` and `SourceCodePro-Semibold.ttf`
(`rmlui_layer.cpp:467-479`). The label face must therefore become
**SourceCodePro-Semibold**, which is monospace, so every small-caps label gets
wider. Consequences, measured, using Source Code Pro's 0.6em advance against
Consolas's 0.5498em (ratio 1.0913):

- **The section headers survive.** `POSITION & ARMAMENT` is the longest label; its
  `.k` hairline currently measures 63.56px and would shrink to ≈55px, still far
  above the 8px `min-width`. The flex hairline absorbs the growth by construction —
  this is the payoff for making the rule length elastic.
- **The `#env` `ENV` row does not.** Its monospace run measures **315.3px** in a
  330px flex line; at Source Code Pro that is **344.1px**, overflowing by
  **14.1px**. Nothing in the column sets `overflow: hidden`, so the run wraps to a
  second line and collides with the `ACT` row 20px below it. Fix: drop `LUX` and
  `LAMP` to a third 20px row and grow `#env` to 60px, which pushes every section
  below it by 20px — and the column has zero slack, so that budget has to come out
  of `#trauma`.
- **The callout's management line is marginal.** `CONTROL HAEMORRHAGE, THEN
  DISINFECT` measures **309.28px** in a 336px content box; at Source Code Pro,
  **337.5px** — 1.5px over, so it wraps and takes the callout from 44px to 52px in
  a 44px box. Fix: shorten the management string or widen the callout by reclaiming
  the 6px gap above it.
- **Chart notes are safe.** The widest note ink is 274.91px in a 319px cell → 300px
  at Source Code Pro, with 19px spare. This is the one place the design has
  headroom, and it is where the shipping HUD wraps three of six rows.
- **Weight 700 has no face.** `.pname .nm`, `.tagn`, `.aw`, `.ky` and the torso
  legend all ask for 700; only 400 and 600 are loaded, so RmlUi resolves them to
  Semibold. The design should declare 600 and mean it, rather than asking for a
  weight that silently degrades.
- **Glyph coverage needs checking**, not assuming: `▲` (U+25B2) in the callout
  label, `↑`/`↓` (U+2191/2193) in the observation chart, `−` (U+2212) for the
  morale sign, and the `&thinsp;` (U+2009) inside `8 / 30`. If Source Code Pro
  lacks any of them the fallback is Terminus, and if Terminus lacks them the
  replacement is already demonstrated inside this file: the scanner's POI markers
  are 5x5px divs with `transform: rotate(45deg)`, and the same trick builds the
  triangle and the arrows as pure geometry.

**C++ producer work in `src/panels.cpp`.** This is the expensive half, and two of
the gaps are defects rather than missing features.

- **`hud_vitals` already computes the per-part status colour and throws it away.**
  `panels.cpp:872` builds `label_hex` from `u.limb_color( bp.id(), true, true, true )`
  — the call that folds bleeding, bitten and splint state into the limb — and then
  never uses it; the only thing emitted per part is `vbar_rml` with an HP fill and
  a text label. So **per-part effect state is not currently emitted at all**, which
  is why the shipping HUD cannot say the left arm is open. The fix is cheaper than
  it looks, because a second function already does exactly the right thing:
  `bodygraph_bp_color( u, bp, widget_var::body_graph_status )` at
  `panels.cpp:2011-2053` returns `c_red` for `effect_bleed`, `c_yellow` for
  `effect_bite`, `c_green` for `effect_infected` and `c_blue` for a `SPLINT`-flagged
  worn item (`:2037-2047`) — the whole ladder this design needs, already written,
  already reachable from an `avatar &`, and never consulted by the HUD path.
  What must change is the *shape* of what `hud_vitals` emits: this design needs a
  **class token per part** (`st` / `mo` / `im`) plus a **status flag set**, not a
  hex string and not a bar.
- **The crit threshold must not be reused.** `hud_vitals` feeds
  `is_critical = allow_crit && norm < 0.25` (`:880`), and the fixture's own arm
  proves that gate is wrong: `8/30 = 0.2667`, so the most critical limb in the
  fixture is not critical. This design's IMMEDIATE tier must be an explicit rule —
  `pct < 34% OR the part carries bleed/bite/infection` — and it must be stated in
  one place, not derived twice. Honest caveat: **the file alone cannot distinguish
  those two clauses**, because L arm is the only IMMEDIATE region and it satisfies
  both. The file *does* pin the STABLE boundary: head at 83%, torso at 75% and
  L leg at 79% are all MONITOR while R arm and R leg at 100% are STABLE, so
  STABLE means exactly 100%. That is a strong rule with a real cost — the figure
  goes amber the first time anything scratches you — and if it over-alarms in play
  the boundary is the first thing to move.
- **A new `hud_body_plate()` producer, with a static geometry table.** The
  waterline mechanic needs, per part: the union height of its segments, which
  segment the waterline crosses, and the local percentage within it. The nineteen
  segment rects are constants, so the RML skeleton is static and only three things
  change per turn — one class token, one gradient stop (or one child `height`), and
  the tag text. That keeps the per-turn string build at roughly the cost of
  today's `hud_vitals`. **`get_all_body_parts( true )` already yields exactly the
  six main parts** in the order `hud_vitals` iterates, so the part→segment mapping
  is a fixed lookup, not a search.
- **`hud_hotbar` ignores its argument, so slot availability is not emitted.**
  The signature is literally `auto hud_hotbar( avatar & ) -> std::string`
  (`panels.cpp:1282`) — the parameter is unnamed and unused, and the body renders
  nine actions from a `constexpr std::array` with no reference to game state, so
  there is no disabled state for any slot. Three changes: name and use the
  parameter to test each action's availability (for slot 1, "is a gun wielded");
  emit a reason string alongside the disabled flag, because this design shows *why*
  (`NO GUN`), not just *that*; and replace the `key.empty() → continue` skip at
  `:1297-1299` with a rendered-but-disabled slot, since silently skipping unbound
  actions shifts every slot left and destroys the positional muscle memory the
  hotbar exists for. Separately, the fixed array at `:1284-1288` contains
  `ACTION_TOGGLE_CROUCH` and `ACTION_MAP` but not `ACTION_EXAMINE` or
  `ACTION_THROW`, so matching the fixture's nine is a two-line edit to that array
  and its `labels` twin.
- **A stamina history ring buffer, which does not exist today.** The oscilloscope
  needs the last 43 samples; the HUD has no concept of history for any quantity.
  A fixed `std::array<uint16_t, N>` plus a write cursor, sampled once per turn in
  `sidebar_hud_sync`, is the whole data structure. The *rendering* is the part to
  be careful about: 43 absolutely-positioned rotated elements rebuilt per turn is
  43 element allocations and 43 transform decompositions per frame, which is a real
  cost for a decorative trace. Two better options, in order: emit the trace as a
  single `shader` decorator, or pre-rasterise it and emit one `image` decorator.
  The 43-div form in the mockup is an honest illustration of the geometry, not a
  recommendation for how to ship it.
- **New producers with no current equivalent**, all of which the baseline lists as
  DROPPED: wind, light level, held-light charge, encumbrance, noise, morale,
  warmth, the sidearm, the wielded weapon's damage numbers, and the target's range
  and behaviour. Several of the underlying helpers already exist and are simply
  uncalled by any HUD producer (`morale_stat()`, the light-level helper). Adding
  them back is restoring parity with the curses sidebar, not new feature work.
- **The wound chart needs two derived columns.** Onset is producible —
  `effect::get_start_time()` exists (`effect.h:273`) — and the consequence column
  is a static string table keyed by `efftype_id`, which is new data but trivially
  authorable in JSON rather than C++. The chart must also be **ranked by severity**
  so that truncation (see Weakness 4) drops the least important row, which today's
  flat first-8-then-`(+N)` behaviour in `hud_botbar` does not do.
- **Geometry.** `sidebar_hud_apply_rect()` writes `dock_width_pct` from the widget
  layout width; `48` cells yields exactly `20%` / `left: 80%`, so the only change
  is the width value in `data/json/ui/sidebar.json` plus setting
  `sidebar_hud_top_rows()` and `sidebar_hud_bottom_rows()` to 0. **No new geometry
  code**, and dropping the strips deletes the three percentage-vs-cell bugs the
  baseline documents rather than porting them.

## Weaknesses

1. **The type is smaller than a size this project has already rejected, in a
   quantity it has already refused.** The base is **11px** where the shipping HUD
   is **18dp**, and `sidebar_hud.rcss:14-16` carries a comment explaining that
   15dp "read as unreadably tiny on a 1080p screen" — this design is 27% smaller
   again than the size that was thrown out, and its smallest text is 7px, 39% of
   the shipping body size. It uses **13 distinct font sizes** (7, 7.5, 8, 9, 9.5,
   10, 10.5, 11, 12, 13, 15, 16, 18) against the three-tier system
   `plans/hud-coherence.md` §1.4 landed, and **67 distinct hex literals** against
   theme.json's ~23 tokens — so it is not themeable in the project's existing token
   system without a substantial rewrite of both the palette and the type scale.
   Fixing the sizes is not a global scale-up either: the column is exactly full at
   1080px, so every point of type growth has to be paid for by deleting content.
   The design's information density is only achievable at a size the project has
   twice decided is too small, and that is the honest form of the criticism.
2. **The geometry is authored for exactly one resolution, and the column's height
   is what it costs.** Nine sections at literal `top` values summing to exactly
   1080px means **zero slack**: there is no reflow, no `flex-shrink`, no scroll and
   no priority ordering anywhere in the layout. At **1280x720** the content
   overflows by **360px** — the trauma plate (318px, which is 29% of a 1080px
   screen but **44%** of a 720px one) plus the stamina trace would fall off the
   bottom, and the plate is the entire point of the design. Simultaneously the
   column's fixed 384px goes from **20.0%** of a 1920px width to **30.0%** of a
   1280px one, so the design gets denser and more occluding exactly as the screen
   gets smaller. The fix is not a tweak: the sections must become a flex column
   with per-section shrink priorities, the plate needs a small mode (six fills and
   the callout; drop the leaders, the lead markers and the external tags), and the
   scanner and oscilloscope need to be droppable. Separately, and permanently: a
   full-height right-edge column puts the trauma plate's centre **815px** from the
   player's tile at (944,528). The design buys perfect intra-HUD cohesion — 212px
   from wound ring to treatment order, against the shipping HUD's 891px — by
   spending it on eye-travel from the world. For a player watching a zombie brute
   close from 4 tiles NE, that is a real cost and it is paid every single turn.
3. **Colour is the primary channel, and the tier ordering inverts in greyscale.**
   Relative luminance of the three tier fills: MONITOR `#c8a02a` = **0.376**,
   IMMEDIATE `#ff3b30` = **0.247**, STABLE `#1f6e4a` = **0.119**. The three are
   separable without hue — which is better than the usual red/green trap — but the
   ladder is **non-monotonic**: the *middle* tier is the brightest thing on the
   body, so a protanope or deutanope scanning the figure is pulled to the amber
   torso and legs, not to the dark-red arm. The worst limb becomes the *second*
   most salient region on the plate. Three of the six redundant channels on the L
   arm survive achromatically (the white keyline, the numeric tag, the callout
   text) and three do not (the fill hue, the border hue, the glow hue), and the
   135° hatch — which would carry it — is currently applied to the MONITOR tier
   *as well*, at a different alpha but the same angle, so it does not discriminate.
   The fix is cheap and should ship: give each tier a distinct hatch angle and
   pitch (STABLE none, MONITOR 45° coarse, IMMEDIATE 135° fine, as now), and
   darken MONITOR so the luminance ladder runs monotonically with severity.
4. **Twelve effects paint straight through the section below.** `#wound` is
   `height: 104px` at `top: 682` — a 14px header plus exactly five 18px rows,
   which is exactly the fixture. Twelve rows is `14 + 216 = 230px`, and because
   `#pos` is absolutely positioned at a literal `top: 786` (= 682 + 104), the seven
   extra rows paint **through** the position and armament block: over the scanner,
   over the wielded weapon, over the target's HP bar. There is no scroll, no
   `overflow: hidden`, no `(+N)` counter and no truncation anywhere in the column —
   silent overlap, which is the same failure class the baseline documents in the
   bottom bar, rotated 90°. `#notes` has the identical shape (six rows at exactly
   98px) and fails the same way if the log ever emits more, and the ENV-row font
   overflow in Feasibility is a third instance. The design has no mechanism for
   *more content than expected*, anywhere, and adding one means the flex-column
   rewrite from Weakness 2 — the two defects have one fix.
