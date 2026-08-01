# 03 — Instrument Cluster

## Thesis

The survivor is operating a machine, and machines have instrument clusters. This
design argues that a survival HUD should be a **physical panel of dedicated,
single-purpose instruments in permanently fixed positions** — a cockpit
binnacle bolted along the bottom of the screen, with a slim overhead
annunciator strip for identity and environment. Every value gets its own
instrument, and the instrument never moves: the stamina dial is always the round
gauge in bay 2, `BLEED L·ARM` is always the left lamp of the annunciator's second
row, the noise VU is always at x=174 on the rail. The bet is that a player who has
flown this panel for ten hours reads it the way a pilot reads a cluster — by
*shape and position*, peripherally, without parsing text — and that this is
worth more than any amount of adaptive layout. What it trades away is
**flexibility, in three specific ways**. The panel cannot reflow, so it cannot
grow: the annunciator is 15 lamps and will never be 16, and an effect system with
hundreds of entries has to fit in three general-purpose slots (Weakness 3). The
panel cannot shrink, so it does not survive 1280x720 as anything but a letterbox
(Weakness 1). And the analog register that makes the glance-level read fast makes
the exact-number read slower, which is why almost every instrument here carries a
digital repeater beside it — an admission, priced into the design (Weakness 2).
In exchange it buys back the whole centre of the screen: the play area is one
unbroken 1920x754 rectangle with the player at its middle and nothing floating
over it, which is 5.4 points less occlusion than the shipping HUD.

## Register

Machined olive-drab sheet metal, sand-coloured engraving, orange nixie readouts,
caged filament annunciator lamps, and yellow-black hazard tape used as a severity
marker rather than as decoration. Everything is gradients and box-shadows; there
are no images.

### Chassis / plate

| token | hex | role |
|---|---|---|
| plate top | `#4b4e36` | `.plate` body ramp stop 0% — the lit upper face of the deck |
| plate 22% | `#40432e` | body ramp |
| plate 56% | `#383b28` | body ramp |
| plate 88% | `#2b2e1e` | body ramp |
| plate base | `#232619` | body ramp stop 100% |
| rail top | `#52553c` | `.rail-plate` stop 0% — the sub-row is one step lighter so it reads as a separate stamping |
| rail 30/74/100% | `#464932` `#3a3d29` `#2c2f20` | rail body ramp |
| recess wall | `#232519` → `#292c1e` → `#1c1e14` | `.recess` — the milled instrument pocket each bay sits in |
| socket | `#1e2015` → `#262819` | `.sw-sock`, the hole a switch cap sits in |
| cap live | `#5b5e42` `#4a4d34` `#3a3d29` `#2d3021` | `.sw-cap` — a proud, pressable switch |
| cap dead | `#31341f` `#292b1b` `#1f2115` | `.sw-cap-off` — slot 01, sunk instead of proud |
| rivet | `#9a9c78` → `#6d7050` → `#43462f` → `#1e2015` | `.rv`, a 7px radial-gradient dome; 26 of them |
| bezel (dial) | `#7f8261` `#56593d` `#353824` `#23261a` `#43462f` `#6e7152` `#4a4d34` | `.bez` conic stops from 210deg |
| bezel (scope) | `#7c7f5f` `#52553a` `#333623` `#212419` `#41442e` `#6b6e50` `#484b33` | `.sc-bez` conic stops from 205deg |

### Engraving

| token | hex | role |
|---|---|---|
| `.eng` | `#cdc39c` | primary cut legend — the name, switch keycaps, environment values |
| `.eng-d` | `#9d9878` | secondary — bay headers, body-part labels, dial numerals |
| `.eng-x` | `#6f6d55` | micro-caption tier — units, scale ranges, `PRESS-TO-TEST`, `DETENT 2 OF 3` |
| tick | `#e6e0c4` → `#a7a288` | dial and VU graduations |

### Nixie / LED

| token | hex | role |
|---|---|---|
| window bed | `#150e07` → `#1c1409` → `#0d0905` | `.win`, the tube cavity behind every digital readout |
| `.led` | `#ffa14a` | lit digits — time, HP values, counters, stamina, bearing |
| `.led-r` | `#ff6a52` | fault digits — `8/30`, `BLD·BTN`, morale `-3` |
| `.led-g` | `#7bf09a` | **declared and never applied** — a dead token in the stylesheet |
| `.led-x` | `#4a3320` | unlit-segment colour, used once, for the axe's `CUT 00` |

### Status / instrument

| token | hex | role |
|---|---|---|
| healthy | `#3fc763` (hi `#8ff0ad`) | body-part fill at 100%; dial green arc (35–100%) |
| worn | `#9ac72c` (hi `#d6f06a`) | body-part fill 26–99% |
| critical | `#d4271a` (hi `#ff7057`) | L·ARM fill; alarm ring `rgba(255,58,42,.75)` |
| dial redline | `#cf2415` | stamina 0–15% |
| dial caution | `#e79d0c` | stamina 15–35% |
| needle | `#e33a22` tip / `#f4f1e4` body / `#9b9784` counterweight | `.ndl`, the moving-coil pointer |
| pointer | `#fff3d0` → `#e8b028` → `#8a6410` | `.ew-ptr`, the edgewise meters' index |
| meter redline | `#8f1c0c` → `#4a0f06` | `.ew-red`, pain ≥ 40 |

### Annunciator lamp tiers

| tier | LED | legend | bed | used by |
|---|---|---|---|---|
| nominal | `#4ef07a` | `#a9f2b9` | `#2c5f33`→`#163a1b` | `SAFE MODE ON` (the only green lamp on the panel) |
| caution | `#ffb020` | `#ffd478` | `#5e4711`→`#332608` | contact, pain, needs, lamp charge, mission, target behaviour |
| alarm | `#ff3a2a` | `#ff9d88` | `#6d1b10`→`#3a0d07` | `TRACKING ×1`, `BLEED L·ARM`, `BITE L·ARM` |
| environment | `#46d0e0` | `#98e6f0` | `#154850`→`#0a2a2e` | `SOAKED`, `CHILLY` |
| dark | `#2b2d1f` | `#54563f` | `#23251a`→`#1a1c12` | the three unlit reserve slots |

### Scope, tape, chart

| token | hex | role |
|---|---|---|
| scope phosphor | `#12240d` → `#0a1707` → `#040802` | `.sc-face` |
| graticule | `rgba(150,220,140,.22)` | range rings and crosshair |
| bearing needle | `rgba(255,196,60,0)` → `#ffde8c` | `.sc-ndl`, faded at the hub, bright at the rim |
| return | `#c8503a` → `#7a2414` | undesignated hostile blip |
| designated | `#ffd0a0` → `#ff4a24` → `#8e1a08` | the brute, inside an `rgba(255,196,60,.85)` lock ring |
| own ship | `#ffffff` → `#bfe8b8` → `#4a7a44` | player at scope centre |
| paper | `#c9bb9a` `#d5c8a6` `#d9cdae` `#cabb98` | `.tape` stock |
| tape ink | `#2b2820` neutral · `#8e1a10` bad · `#a2564a` dim bad · `#8a6410` caution · `#7d6c48` timestamp | log row semantics |
| punch | `#a8200f` → `#6d1408` | the red edge-punch beside a damage row |
| chart | `#1b2a12` field · `#2f4d20` forest · `#57583f` house · `#8a7020` POI · `#e8b028` active mission · `#f2eee2` player | 11x11 overmap cells |
| hazard | `#d8ac16` on `#191b10` | `.hz` — 7px/7px stripes at 135deg |
| nameplate lit | `#d3c69c` `#bdae83` `#a4966c`, ink `#171910` | wielded weapon (engraved-in-hand) |
| nameplate dim | `#4e5138` `#3d4029` `#2f3221` | stowed sidearm |
| nameplate hostile | `#5e2418` `#4a1a10` `#360f08`, ink `#ffb2a0` | designated contact |
| magazine | lit `#ffc55a`→`#e8901a` · dark `#2a2c1c`→`#1c1e12` | 17 rounds, 12 charged |
| cage placard | `#8e1a10` → `#5a0f08`, ink `#ffcfc4` | `NO GUN` on switch 01 |

### Type

One face, declared once on `#screen div, #screen span`:
`Consolas, "Cascadia Mono", "Lucida Console", monospace`. Consolas stands in for
Source Code Pro per the contract; see Feasibility for what changes when the real
face is used. Nine sizes, in a strict hierarchy of instrument role rather than of
document structure:

| px | treatment | used by |
|---|---|---|
| 23 | `.led`, `letter-spacing: 2.4` | the master chronometer `21:47` — the largest thing on screen |
| 15 | `.eng` bold, `letter-spacing: 2.6` | `NIGEL ASHBY` |
| 14 | `.eng` bold | switch keycaps |
| 13 | `.led` | counter values (`32`, `12/17`, `045`, `10`) |
| 12 | tape ink, `.led` dial repeater | log rows, the dial's `59%` |
| 11.5 | bold, `letter-spacing: 2.2` | nameplate titles (`FIRE AXE`, `GLOCK 19`) |
| 9.5–10 | `.led` / `.eng` | body-part values, edgewise digits, environment values |
| 8–9 | `.eng-d`, `letter-spacing: 0.9–1.4` | bay headers, lamp legends, body-part labels, dial numerals |
| 6.5–7.5 | `.eng-x`, `letter-spacing: 0.6–1.1` | units, scale ranges, tags, cross-references |

### Border / frame treatment

There is no border property anywhere in this design; every edge is a shadow or a
gradient stop, which is what makes it read as pressed metal rather than as boxes.

- **Plate.** Three stacked background layers: a 5px rolled top bevel
  (`rgba(255,255,255,.085)` → `.028` at 2px → `0` at 5px), a brushed vertical
  grain on a 3px pitch (`repeating-linear-gradient(90deg, …)`, 1px light / 2px
  dark), and the five-stop olive body ramp. The rail repeats the recipe one step
  lighter so the sub-row reads as a second stamping bolted below the deck.
- **Recess.** A cavity shadow gradient over a darker ramp, plus a four-part
  box-shadow that does all the work: `inset 0 2px 3px rgba(0,0,0,.72)` (the
  pocket), `inset 0 0 0 1px rgba(0,0,0,.45)` (the milled edge),
  `inset 0 -1px 0 rgba(255,255,255,.075)` (light on the bottom lip), and
  `0 1px 0 rgba(255,255,255,.085)` (the plate surface below). Four shadows are
  what turn a 2px-radius rectangle into a machined pocket.
- **Bezel.** One `conic-gradient` through seven olive stops produces a turned
  metal ring with a specular arc at upper-left and a second at lower-right, no
  image required. The dial starts `from 210deg` and the scope `from 205deg` with
  different stop sets, so the two instruments do not look stamped from one die.
- **Engraving.** `text-shadow: 0 1px 0 rgba(0,0,0,.9)` on all three tiers, plus a
  `0 -1px 0 rgba(255,255,255,.055)` top highlight on the brightest tier only.
  That pair is what makes a letterform look cut into the plate rather than
  printed on it.
- **Nixie window.** Near-black bed, `inset 0 1px 3px rgba(0,0,0,.92)` to sink it,
  a warm `inset 0 -1px 0 rgba(255,150,60,.12)` bounce off the bottom, and a
  `.slit` overlay of `repeating-linear-gradient(180deg, rgba(0,0,0,.30) 1px /
  transparent 3px)` — a 1-on-2-off scanline reading as the wire mesh in front of
  a tube.
- **Hazard tape.** One class, `repeating-linear-gradient(135deg, #d8ac16 0 7px,
  #191b10 7px 14px)`, applied at four opacities by severity: `.22` on the two
  12px end strips, `.5` on the binnacle's upper lip, `.62` on the top plate's
  lower lip, and **full opacity** on the three places that mark a real fault —
  the `L·ARM CRIT` tab, the L·ARM label cap, and the two bars caging switch 01.
  Same physical tape, applied harder where it matters.
- **Seams and fasteners.** Five 3px vertical seam gradients at x=351, 527, 859,
  1135, 1419 divide the deck into bays; one 3px horizontal seam at y=1010
  separates deck from rail; two more verticals at x=527 and 1135 divide the rail.
  26 rivets: 14 on the deck at the seam ends, 8 on the rail, 4 on the top plate.

## Layout

Two anchored regions and nothing else — there are exactly four children of
`#screen`. All figures below are `getBoundingClientRect()` read off the rendered
file at a confirmed 1920x1080 viewport.

| region | x | y | w | h |
|---|---|---|---|---|
| `#oh` overhead plate | 520 | 0 | 880 | 56 |
| `#oh-sh` its cast shadow | 512 | 56 | 896 | 14 |
| `#bn` bottom binnacle | 0 | 840 | 1920 | 240 |
| `#bn-sh` its cast shadow | 0 | 824 | 1920 | 16 |

The binnacle decomposes into `#bn-lip` (0,840,1920,3 — the hazard lip),
`#deck` (0,843,1920,169) and `#rail` (0,1012,1920,68). The overhead plate is
centred: `(1920 − 880) / 2 = 520`. Both cast shadows are gradients onto live
terrain and are the only translucent elements in the design.

**Nothing occupies the band y = 70 → 824.** Measured directly: the lowest edge of
anything in the top group is 70, the highest edge of anything in the bottom group
is 824. That is a single unbroken 1920x754 rectangle of play area with the
player's tile (928,512)–(960,544) at its centre — 280px above the nearest HUD ink
and 442px below it.

### Overhead plate — internal geometry

| element | x | y | w | h |
|---|---|---|---|---|
| identity `NIGEL ASHBY` | 530 | 5 | 300 | 19 |
| `SURVIVOR · CHESWICK — RESIDENTIAL` | 530 | 26 | 300 | 12 |
| seam | 835 | 2 | 3 | 49 |
| chronometer window `21:47` | 922 | 2 | 86 | 30 |
| `DAY 014 · AUTUMN` | 840 | 33 | 250 | 11 |
| seam | 1095 | 2 | 3 | 49 |
| `TEMP +08 °C` / `WIND NE 12 KM/H` | 1100 / 1248 | 3 | 143 each | 13 |
| `SKY LIGHT DRIZZLE` / `LUX 12% MOON` | 1100 / 1248 | 25 | 143 each | 13 |
| light-level pip bar (5 x 8px at 11px pitch) | 1333 | 37 | 63 | 5 |
| `#oh-lip` hazard strip | 520 | 53 | 880 | 3 |

The plate's two bottom corners carry `border-radius: 5px`; the top corners do not,
because the plate is understood to be bolted to the top of the frame.

### Deck — six instrument bays

Bays are flush-packed, summing to exactly 1920. Each is `y=845, h=165`, with a
13px header row at the top and a 148px `.recess` beneath it at `y=860` (ending at
1008, 4px clear of the deck's lower edge at 1012).

| bay | x | w | recess x | recess w | contents |
|---|---|---|---|---|---|
| 1 `CONDITION` | 0 | 352 | 12 | 328 | six body-part VU columns |
| 2 `STAMINA` | 352 | 176 | 364 | 152 | 140px round dial |
| 3 `ANNUNCIATOR` | 528 | 332 | 540 | 308 | 3x5 caged lamp matrix |
| 4 `CONTACT` | 860 | 276 | 872 | 252 | 138px scope + readout column |
| 5 `ARMAMENT` | 1136 | 284 | 1148 | 260 | two nameplates + counters |
| 6 `TELEPRINTER` | 1420 | 500 | 1432 | 476 | tape + chart repeater |

Bay 1 is the widest instrument bay and sits at the left edge, where the eye lands
first, because per-limb condition is the one thing a Cataclysm HUD must get right.
Bay 6 is wider still but is a *log*, deliberately parked at the far end.

**Bay 1** — six `.bp` columns, each 50px wide on a 55px pitch, at x = 14, 69,
124, 179, 234, 289. Per column: a 13px label cap at y=860; a 96px well at
y=876→972 with the fill rising from the bottom; a 16px nixie value window at
y=975; an 11px tag at y=994. The well carries three overlay layers — a 9.6px
graduation ladder (10 divisions), a 6px segment mask (16 segments, so the fill
reads as a stacked LED bargraph and not a smooth bar), and a diagonal glass
highlight. A 1px dashed red **crit line at `bottom: 24px`** marks the 25%
threshold across all six wells; L·ARM's 26px fill sits 2px under it, which is the
whole story of that limb told geometrically. The `L·ARM CRIT` hazard tab occupies
the bay's header row at x=236→340.

**Bay 2** — `.bez` at (370,864) 140x140, `.face` inset 7px to 126x126. The scale
sweeps 270°: major ticks at −135/−67.5/0/+67.5/+135° (numerals 0/25/50/75/100),
minor ticks every 13.5° (5%), 21 in total. The redline arc is a `conic-gradient`
on `.arc`, masked back to a ring by `.arc-m`: red 225°→265.5° (scale 0–15%),
amber 265.5°→319.5° (15–35%), green elsewhere. A red index bug sits at −94.5°,
exactly on the 15% redline entry. The `5100/8700` nixie is in the bay header at
(452,845) 64x13; the `59%` repeater is inside the face at (407,958) 40x18.

**Bay 3** — 15 lamps of 100x26 on a 104x30 pitch: columns at x = 540, 644, 748;
rows at y = 861, 891, 921, 951, 981. Twelve lit, three dark reserve. Each lamp is
an LED slug at left, an inset bloom on the bed, and a `.cage` overlay of 8px-pitch
vertical bars — the wire guard that stops a boot from breaking the lens. A
`PRESS-TO-TEST` button sits in the bay header at (764,845) 84x13.

**Bay 4** — `.sc-bez` at (872,864) 138x138, face 118x118. Two range rings at
r=19.7 and r=39.3 of a 59px radius: a 12-tile scale in 4-tile steps. The bearing
needle is rotated 45° (=045). The designated brute's pip is at 1/3 radius on that
bearing (measured: r=19.66px of 59 = 4.00 tiles) inside an 18px lock ring; two
undesignated returns sit at 7.0 tiles/072° and 9.5 tiles/018°, giving three
hostiles on the scope. The readout column runs x=1018→1124: hostile nameplate
(y=876), an edgewise HP meter (y=900, pointer at 65/106 = 61.3%), `BRG 045` and
`RNG 04` counters (y=929), a `MOVING · CLOSING` lamp (y=958), and
`3 VISIBLE · 1 TRACKING` (y=985).

**Bay 5** — `WIELDED` caption, then a *lit* nameplate at y=871 (260x20) reading
`FIRE AXE  MELEE · 2H`, then three 84x26 nixie counters at y=894
(`BASH 32` / `CUT 00` / `TO-HIT +1`). A hairline at y=925 separates the stowed
half: `SIDEARM · STOWED (NOT IN HAND)`, a *dim* nameplate `GLOCK 19  9×19 JHP` at
y=940, a `ROUNDS 12/17` counter at y=963, and a 17-segment magazine strip at
y=994 (13px segments on a 15px pitch, 12 amber and 5 dark). Lit versus dim
nameplate is the in-hand/stowed distinction, carried by material rather than by a
word.

**Bay 6** — `.tape` at (1432,862) 372x140 holding six 21px rows, each with a
sprocket hole at the left margin and — on the three damage rows — a red edge
punch. `.chart-w` at (1812,862) 96x100 holds the 11x11 overmap as 8px cells on an
8px pitch, with `11×11 OVERMAP` beneath it and a `MISSION 045` lamp at y=982.

### Rail — the sub-row

| element | x | y | w | h |
|---|---|---|---|---|
| `MOVE MODE` rotary (CROUCH / **WALK** / RUN + knob) | 12 | 1014 | 150 | 60 |
| noise VU meter | 174 | 1018 | 200 | 56 |
| `ENCUMBRANCE` counters (TORSO 12 / ARMS 04) | 388 | 1014 | 116 | 60 |
| nine switch sockets, 60x60 on a 65px pitch | 542 | 1016 | 580 | 60 |
| `ATTRIBUTES` counters (STR/DEX/INT/PER, 72px each on a 76px pitch) | 1148 | 1014 | 300 | 60 |
| edgewise `SPEED` / `FOCUS` | 1468 / 1694 | 1016 | 214 | 25 |
| edgewise `PAIN` / `MORALE` | 1468 / 1694 | 1047 | 214 | 25 |

Switch sockets are at x = 542, 607, 672, 737, 802, 867, 932, 997, 1062. Socket 01
is the only one whose cap is `.sw-cap-off` (sunk, not proud); it carries a red
`NO GUN` placard and two full-opacity hazard bars across the cap at y=1034 and
y=1047.

## Data mapping

| fixture field | where it appears | encoding |
|---|---|---|
| Name `Nigel Ashby` | overhead plate, identity block | text, 15px `.eng` bold, the largest engraving on the panel |
| Profession `Survivor` | overhead plate, sub-line | text, `.eng-x` |
| Day `14` | overhead plate, `DAY 014` | numeric, zero-padded to instrument convention |
| Time `21:47` | overhead plate, chronometer window | numeric, 23px `.led` in a nixie window — the panel's master clock |
| Season `Autumn` | overhead plate, `DAY 014 · AUTUMN` | text |
| Place `Cheswick — residential` | overhead plate, sub-line | text, uppercased |
| Weather `Light drizzle` | overhead plate, `SKY LIGHT DRIZZLE` | text |
| Temperature `8 °C` | overhead plate, `TEMP +08 °C` | numeric, signed and zero-padded |
| Wind `NE 12 km/h` | overhead plate, `WIND NE 12 KM/H` | text + numeric |
| Light level `12%` | overhead plate, `LUX 12% MOON` **and** a 5-pip bar | numeric + discrete bar (1 of 5 pips lit — 12% falls in the lowest quintile) |
| Flashlight `34%` charge | bay 3, annunciator lamp `LAMP 34%` | lamp (caution tier) + numeric in the legend |
| STR `10` | rail, `ATTRIBUTES` counter | numeric, nixie, zero-padded |
| DEX `12` | rail, `ATTRIBUTES` counter | numeric, nixie |
| INT `9` | rail, `ATTRIBUTES` counter | numeric, nixie `09` |
| PER `11` | rail, `ATTRIBUTES` counter | numeric, nixie |
| Speed `96` | rail, edgewise `SPEED 0-150` | pointer position (136/214 = 63.6% ≡ 96/150) + numeric |
| Focus `84` | rail, edgewise `FOCUS 0-100` | pointer (179/214 = 83.6%) + numeric |
| Pain `12` | rail, edgewise `PAIN 0-80` **and** bay 3 lamp `PAIN 12` | pointer (31/214 = 14.5% ≡ 12/80) against a redline printed from 40 up + numeric + lamp |
| Morale `-3` | rail, edgewise `MORALE -100/+100` | pointer (103/214) against a printed zero index at 107, plus a `.led-r` numeric — the only bipolar scale on the panel |
| Encumbrance torso `12` | rail, `ENCUMBRANCE TORSO` | numeric, nixie |
| Encumbrance arms `4` | rail, `ENCUMBRANCE ARMS` | numeric, nixie `04` |
| Move mode `walking` | rail, rotary selector | position — the knob sits at detent 2 of 3, `WALK` lit green, `CROUCH`/`RUN` dark |
| Noise made `8` | rail, noise VU meter | needle angle (−19.6° of a ±42° sweep ≡ 8/30) + `08` nixie + a printed redline from 24 |
| Head `20/24` | bay 1 col 1 | fill height 80/96 = 83.3% + green hue + `20/24` nixie + `OK` tag |
| Torso `45/60` | bay 1 col 2 | fill 72/96 = 75.0% + green hue + nixie + `OK` |
| L arm `8/30` | bay 1 col 3 | fill 26/96 = 27.1% + red hue + `.led-r` nixie |
| L arm **critical** | bay 1 col 3, four redundant channels | hazard-striped label cap; an amber `L·ARM CRIT` tab in the bay header; a red `rgba(255,58,42,.75)` alarm ring plus outer bloom on the well; and the fill sitting visibly *below* the 25% crit line printed across all six wells |
| L arm **bleeding, bitten** | bay 1 col 3 tag **and** bay 3 lamps | `BLD·BTN` in `.led-r` directly under the limb's own value window (16px away, versus 891px in the shipping HUD), plus two dedicated alarm lamps `BLEED L·ARM` / `BITE L·ARM` naming the part |
| R arm `30/30` | bay 1 col 4 | fill 96/96 = 100% + `#3fc763` full-health hue + nixie + `OK` |
| L leg `22/28` | bay 1 col 5 | fill 75/96 = 78.1% + green hue + nixie + `OK` |
| R leg `28/28` | bay 1 col 6 | fill 100% + `#3fc763` + nixie + `OK` |
| Stamina `5100/8700` | bay 2 | needle angle (23.3° of a −135…+135° sweep ≡ 58.62%, computed from the true ratio, not the rounded percent) + `59%` digital repeater + the raw `5100/8700` nixie in the bay header + a printed 15% redline and 35% caution arc |
| Hunger `Peckish` | bay 3 lamp | lamp (caution tier) |
| Thirst `Thirsty` | bay 3 lamp | lamp (caution tier) |
| Fatigue `Tired` | bay 3 lamp | lamp (caution tier) |
| Warmth `Chilly` | bay 3 lamp | lamp (environment tier, cyan) |
| Effect `Bleeding (L arm)` | bay 3 lamp `BLEED L·ARM` + bay 1 tag | lamp (alarm tier) + glyph tag on the limb |
| Effect `Bitten (L arm)` | bay 3 lamp `BITE L·ARM` + bay 1 tag | lamp (alarm tier) + glyph tag on the limb |
| Effect `Soaked` | bay 3 lamp | lamp (environment tier) |
| Effect `Tired` | bay 3 lamp | lamp (caution tier) |
| Effect `Pain 12` | bay 3 lamp + rail meter | lamp + pointer |
| Effects, as a list | rail, `EFFECTS: BLEEDING / BITTEN / SOAKED / TIRED / PAIN — SEE ANNUNCIATOR` | text roll-up, 6.5px, deliberately demoted to a cross-reference |
| Effects, **priority order** | — | **DROPPED — the annunciator's value is that a lamp never moves, so the grid is ordered by category (threat / injury / environment / needs), not by the fixture's priority. `PAIN 12` reads third and `SOAKED` fourth, transposing the fixture's 3rd and 5th. Position is the encoding; it cannot also carry rank.** |
| Wielded `fire axe` | bay 5, lit nameplate | text on a light, proud brass plate — "in hand" is carried by material |
| `32 bash` | bay 5, `BASH` counter | numeric, nixie |
| `0 cut` | bay 5, `CUT` counter | numeric `00` in `.led-x`, the unlit-segment colour — the counter is present and reads zero, rather than being absent |
| `to-hit +1` | bay 5, `TO-HIT` counter | signed numeric, nixie |
| Sidearm `Glock 19` | bay 5, dim nameplate | text on a dark, sunk plate — "stowed", reinforced by the `SIDEARM · STOWED (NOT IN HAND)` caption |
| Sidearm `12/17` | bay 5, `ROUNDS` counter **and** magazine strip | numeric + discrete bar (17 segments, 12 amber, 5 dark) |
| Sidearm `9mm JHP` | bay 5, nameplate sub-field | text, rendered `9×19 JHP` — the same cartridge in headstamp notation, to match the register |
| Target `zombie brute` | bay 4, hostile nameplate | text on the only red nameplate on the panel |
| Target HP `62%` | bay 4, edgewise HP strip | pointer (65/106 = 61.3%) + `62%` numeric |
| Target `4 tiles NE` | bay 4, scope + counters | **position** — pip at 1/3 of a 12-tile radius on a 45° bearing, inside a lock ring — plus `BRG 045` and `RNG 04` nixies |
| Target status `moving` | bay 4, `MOVING · CLOSING` lamp | lamp (caution tier) |
| `3 hostiles visible` | bay 3 lamp `CONTACT ×3`, bay 4 scope, bay 4 caption | numeric in the lamp legend + three returns actually plotted on the scope + `3 VISIBLE · 1 TRACKING` |
| `1 tracking` | bay 3 lamp `TRACKING ×1` + the scope lock ring | lamp (alarm tier) + ring glyph on the designated pip |
| `SAFE MODE ON` | bay 3 lamp | lamp — the **only** green lamp on the panel, so "safe mode is off" is legible as an absence of green |
| Log, 6 rows | bay 6 tape | text on paper, 21px pitch, per-row ink colour (`#2b2820` / `#8e1a10` / `#a2564a` / `#8a6410`) **and** a per-row opacity ladder 0.70 → 0.80 → 0.88 → 0.92 → 0.96 → 1.00, newest at the bottom in bold, with a red edge punch beside each of the three damage rows |
| Minimap 11x11 | bay 6 chart repeater | 121 coloured cells, 8px on an 8px pitch, no glyphs |
| Player position | chart centre cell (row 6, col 6) | the single `#f2eee2` cell with a bloom — the brightest thing in the chart |
| Mission marker NE | chart row 2, col 10 + `MISSION 045` lamp | bright `#e8b028` cell with a bloom (the two other POI markers are dim `#8a7020`, so "the mission" is distinguished from "a POI") + a caution lamp |
| Hotbar `f` Fire | rail, switch socket 01 | keycap + engraved action |
| Hotbar slot 1 **unavailable** | rail, switch socket 01 | **three redundant channels** — the cap is sunk (`.sw-cap-off`) instead of proud, the key and label are dimmed to `#4f5140`/`#4a4c3b`, and two full-opacity hazard bars physically cage the switch above a red `NO GUN` placard |
| Hotbar `r` Reload | socket 02 | keycap + action |
| Hotbar `'` Run | socket 03 | keycap + action |
| Hotbar `e` Examine | socket 04 | keycap + action |
| Hotbar `p` Pick up | socket 05 | keycap + action |
| Hotbar `c` Craft | socket 06 | keycap + action |
| Hotbar `i` Inventory | socket 07 | keycap + action |
| Hotbar `t` Throw | socket 08 | keycap + action |
| Hotbar `5` Wait | socket 09 | keycap + action |

Every fixture value is present. The single `DROPPED` row is the *ordering* of the
effects list, not any effect.

## Occlusion budget

**25.6% of the 1920x1080 play area**, against the measured baseline of **31.0%** —
5.4 points less, a 17.4% reduction in painted chrome.

Play area: `1920 x 1080 = 2,073,600 px²`. Method as ratified by the baseline: union
(not sum) of each region's intersection with the frame, opaque at full weight,
translucent scrims at half weight.

| region | rect | area | weight | counted |
|---|---|---|---|---|
| `#oh` overhead plate | 880 x 56 | 49,280.00 | opaque, 1.0 | 49,280.00 |
| `#bn` bottom binnacle | 1920 x 240 | 460,800.00 | opaque, 1.0 | 460,800.00 |
| `#oh-sh` cast shadow | 896 x 14 | 12,544.00 | scrim, 0.5 | 6,272.00 |
| `#bn-sh` cast shadow | 1920 x 16 | 30,720.00 | scrim, 0.5 | 15,360.00 |
| **union** | | **553,344.00** | | **531,712.00** |

    531,712 / 2,073,600 = 0.25642  ->  25.64%

No overlaps to subtract: the four regions occupy y-bands 0–56, 56–70, 824–840 and
840–1080, which are disjoint by construction, and `#oh`'s x-range 520–1400 does
not meet anything.

Per region, as a share of the screen: binnacle **22.22%** (of which the deck is
15.65%, the rail 6.30% and the hazard lip 0.28%), overhead plate **2.38%**, the
two cast shadows together **1.04%**.

**Two alternative figures, both stated so the comparison rule is explicit.**
*Opaque only*, discarding the shadows entirely: `510,080 / 2,073,600 = 24.60%`.
*True alpha-weighted*, integrating each shadow's actual gradient rather than
taking half — `#bn-sh` ramps `rgba(0,0,0,.66)` → `.24` at 46% → `0`, mean alpha
0.2718; `#oh-sh` ramps `.62` → `.26` at 42% → `0`, mean 0.2602 — gives
`510,080 + 8,349 + 3,264 = 521,693`, i.e. **25.16%**. The plates themselves are
fully opaque (every gradient stop in `.plate` and `.rail-plate` is a solid hex),
so unlike the baseline there is no alpha discount available on the bulk of the
surface. Quote **25.6%** against the baseline's 31.0%; both are computed under the
same rule.

**What the 25.6% buys, which is the number that actually matters here.** The
shipping HUD's 31.0% is distributed as a top strip, a full-height right dock and
a floating panel over live terrain, so its largest contiguous unobstructed
rectangle is `1568 x 947 = 1,484,896 px²` minus the 55,040 px² vitals overlay
punched into it = **68.96%** of the screen, in an L-shape. This design's
unobstructed area is a single rectangle, `1920 x 754 = 1,447,680 px² = 69.81%`,
with no floating element in it and the player at its geometric centre. So for
5.4 points less paint it delivers *slightly more* usable play area and, more
importantly, a play area with no holes in it — the trade is that the paint is
concentrated into one thick band at the bottom instead of being spread over four
edges. That is the whole geometric argument of the design, and it is also
Weakness 1 in disguise: a thick band is fine at 1080 rows and is not fine at 720.

## Motion

**Nothing in the file animates.** There is no `@keyframes`, no `animation`
property and no `transition` property anywhere in the stylesheet — verified by
search. The mockup is entirely at rest, which for this design is the correct
resting state to show: a cockpit at rest is a cockpit reading nominal, and every
instrument here is drawn at its true fixture value. Everything below is
**proposed** for the port, not present in the file.

**The turn-based hazard, which governs all of it.** In a real-time cockpit a
needle in motion means something is changing. In Cataclysm every keypress is a
turn, and several of these values change on *every single turn* — noise, stamina,
focus, the target's range and bearing. An unthresholded 220ms sweep on the stamina
dial means the needle is in motion essentially permanently, which inverts the
entire premise: a precision instrument becomes a shimmer, "at rest = nominal"
stops being readable, and the player's peripheral vision is trained to ignore the
one channel the design spent 19,600 px² on. **Every animated instrument below is
therefore dead-band gated**: below the gate the value snaps with no transition,
above it the value sweeps. The gate is not a polish detail, it is the load-bearing
decision.

| what | trigger | duration | easing | gate |
|---|---|---|---|---|
| stamina needle | stamina delta at turn resolution | 220ms | `ease-out` with a 6% overshoot (0 → 106% → 100%) so it settles like a moving coil | `\|Δ\| ≥ 2%` of full scale (174 stamina) |
| noise VU needle | noise delta | 160ms | `ease-out`, no overshoot — a VU is a damped, not a ballistic, movement | `\|Δ\| ≥ 3` of 30, the harshest gate on the panel, because noise changes every turn |
| edgewise pointers (SPEED / FOCUS / PAIN / MORALE / target HP) | value change | 180ms | `linear` translate of `left` | `\|Δ\| ≥ 2px` of pointer travel |
| scope bearing needle | designated target's bearing change | 200ms | `ease-in-out` | `\|Δ\| ≥ 5°` |
| scope returns (blips) | any monster move | — | **jump-cut, never interpolated** | — |
| lamp latch on | effect gained | 90ms up, then a 40ms dip to 92% and back | `linear` up, `ease-out` on the dip — filament inrush | none |
| lamp latch off | effect lost | 320ms | `ease-in` decay — thermal cooldown, which doubles as a "this just cleared" cue | none |
| new alarm-tier lamp | alarm-tier effect gained | 1.1s, 50% duty, **3 cycles then solid** | `ease-in-out` on opacity | none; the repeat count matches the existing `repeats: 3` convention on `hud_anim`'s `status_bleed` |
| L·ARM alarm ring | continuous while any limb is critical | 1.4s loop | `ease-in-out` on the ring's outer bloom alpha only, 0.50 → 0.85 | none |
| teleprinter advance | new message | 260ms | `ease-out` translate of the whole stack up 21px, new row entering from below | none; but coalesce to one advance per turn, not per message |
| nixie digit change | value change | 70ms | cross-dim: outgoing to 25%, then incoming to 100%. **No rolling odometer** — nixies switch cathodes, they do not roll | none |
| `PRESS-TO-TEST` | held | momentary | none | all lamps to full while held |

**Explicitly inert, and this is a decision:** the plate, the rivets, the seams, the
hazard striping, the engraving, the bezels, the switch caps, the chart repeater
and the log's opacity ladder never move. The value of a cluster is that the
chassis is dead still and only the readouts change; a moving hazard stripe or a
breathing plate would be the single most distracting thing on the screen. The
opacity ladder on the tape is a static ordering cue, not an animation — it does
not re-run when a row ages.

## Feasibility in RCSS

Audited against the contract's verified RmlUi 6.2 lists, class by class.

### Already correct, no change needed

- **`border-radius` is a length everywhere.** `.bez` 70px, `.face`/`.arc` 63px,
  `.arc-m` 52px, `.sc-bez` 69px, `.sc-face`/`.sc-glass` 59px, `.hub` 9px,
  `.vu-hub`/`.ms-knob` 6px, `.rv` 4px, and 1–3px on every plate and window. **Zero
  percentages** — no `border-radius: 50%` circles. This is the single most common
  way a design like this fails to port and it is already avoided; the round
  instruments will survive intact.
- **`transform: rotate()` with `transform-origin`** — 40 uses (21 dial ticks, the
  dial needle, the index bug, 11 VU ticks, the VU needle, the scope needle).
  Both properties are in the verified list. The dial and the scope work.
- **`conic-gradient`** — 2 uses, and they are load-bearing: they are the only
  reason the two bezels look like turned metal without an image asset. Verified
  available.
- **`box-shadow`, including `inset` and spread** — ~70 uses. Every bevel, pocket,
  lamp bloom, rivet dome and LED glow. Verified available. Remove it and the
  design collapses to flat rectangles; there is no fallback and none is needed.
- **`opacity`** on the hazard lips, the end strips and the six tape rows.
  Verified available.
- **No `::before` / `::after`, no `content`, no CSS grid, no `clip-path`, no
  `mix-blend-mode`, no `calc()`, no `writing-mode`, no SVG.** Searched; none
  present. Every tick, sprocket hole, punch, rivet, pip and seam is already a real
  child element, which is the RCSS-honest construction. It costs 613 divs — see
  Weakness 4.
- **`display` defaults.** RmlUi registers `display: inline` by default and CBN
  does not link the sample `rml.rcss`. Every element here is `position: absolute`,
  which blockifies regardless, so the design does not silently depend on Chrome's
  UA sheet the way the shipping HUD's `.tbar-fill` and `.dock-header` do.
  `box-sizing: border-box` is already declared explicitly rather than assumed.
- **`overflow: hidden`** on `.win`, `.bp-well`, `.lamp`, `.sc-face`, `.tape`,
  `.chart-w`, `.ew`, `.vu` — available. Note that RmlUi does not clip to
  `border-radius`; nothing here depends on rounded clipping. `.sc-face` is the only
  risk (a 59px-radius circle clipping a rotated 3x59px needle) and the needle is
  exactly inscribed, so it never crosses the boundary even under rectangular
  clipping.

### Must be changed

- **`background-image:` → `decorator:`.** Every plate, recess, bezel, well, lamp
  bed, tape, chart cell, nameplate, magazine segment and hazard stripe. The
  function names are identical (`linear-gradient`,
  `repeating-linear-gradient`, `radial-gradient`, `conic-gradient` are all in the
  verified set) and decorators stack in layers, which is exactly what `.plate`'s
  three-layer bevel/grain/body construction needs. This is a property rename, not
  a redesign — but **every `px` stop position must become `dp`** (`.plate`'s
  `0px, 2px, 5px` bevel, the grain's 3px pitch, `.bp-well`'s 9.6px graduation
  ladder, `.bp-seg`'s 6px segments, `.hz`'s 7px/14px stripes, `.slit`'s 1px/3px
  scanline), or the whole texture layer scales wrong on HiDPI.
- **`text-shadow` → `font-effect`.** Used ~60 times: all three engraving tiers,
  all four LED classes, every lamp legend, and the scope's compass letters. Not
  available in RCSS 6.2. Replacements:
  - LED glow (`.led`, `.led-r`, `.led-g`) → `font-effect: glow(2dp 5dp #ff7414)`,
    which is a direct match for the two-radius glow the mockup fakes with two
    `text-shadow` stops.
  - Engraving (`.eng`, `.eng-d`, `.eng-x`) → `font-effect: shadow(1dp 1dp #000)`.
    **This loses something real:** `font-effect` is one effect per element, so
    `.eng`'s `0 -1px 0 rgba(255,255,255,.055)` top highlight — the half of the
    pair that makes the letterform look *cut into* rather than *printed on* the
    plate — cannot be composed with the drop shadow. Options are to accept the
    single dark shadow (engraving reads as embossed-down only), or to duplicate
    the text element with a 1dp offset and the highlight colour, which doubles the
    element count on every legend. Recommend accepting the single shadow; the
    bevel is carried by the plate's own gradients regardless.
- **`<br/>` in the encumbrance caption** — RmlUi supports it. No change, noted
  only because it is the one non-`div` structural element in the file.

### Font

Consolas stands in for Source Code Pro per the contract, advance `0.5498em` vs
`0.6em`. Two measured consequences, both real defects rather than cosmetic drift:

1. **The teleprinter's longest line already wraps at Consolas.**
   `21:46   You hit the zombie brute with your fire axe.` is 52 characters, which
   at 12px Consolas plus the class's `letter-spacing: .1px` is
   `52 × (6.5977 + 0.1) = 348.3px` inside a **344px** `.tp-line` box. Verified in
   the render: that row's `scrollHeight` is 42 against a `clientHeight` of 21, and
   the orphaned word `axe.` lands at y=890 — on top of the next row's `21:46`
   timestamp, because `.tp-line` rows are absolutely positioned on a fixed 21px
   pitch with `overflow: visible`. At Source Code Pro's advance the same line
   needs `52 × 7.3 = 379.6px` and the wrap gets worse. **Fix at the container, not
   the font**: bay 6 has 476px of recess for a 372px tape, so widening `.tape` to
   420px and `.tp-line` to 392px absorbs Source Code Pro with 12px of margin.
2. **The rail's effects roll-up will wrap into the meters below it.**
   `EFFECTS: BLEEDING / BITTEN / SOAKED / TIRED / PAIN — SEE ANNUNCIATOR` is 68
   characters at 6.5px in a 300px box: `68 × 3.87 = 263px` in Consolas (fits),
   `68 × 4.68 = 318px` in Source Code Pro (does not). Same fix — widen, do not
   shrink the type.

Separately: **the 6.5px and 7px micro-caption tier is below the size at which
RmlUi's rasterised text stays legible**, whatever the face. The real port should
floor `.eng-x` at 8dp and pay for it in width; at 1080p in a browser those
captions are already marginal, and they are the tier carrying every unit and scale
range on the panel.

No new asset, no bundled font, no image file. The entire design is gradients,
shadows and one monospace face.

### C++ producer work in `src/panels.cpp`

Substantial. Two existing producers have the data and throw it away; six new ones
are needed; and the geometry function has to change shape entirely.

**Data that already exists and is discarded:**

- **`hud_hotbar(avatar &)` ignores its `avatar&` argument entirely.** There is no
  per-slot availability concept, so switch 01's `NO GUN` cage — the sunk cap, the
  dimmed legend, the hazard bars, the red placard, all four channels — is **not
  currently producible at all**. Needs: each hotbar entry gains an `enabled` flag
  plus a short disabled-reason string, computed from the action's own
  precondition (for Fire, `u.weapon.is_gun()`). While that argument is being
  wired up, the fixed `std::array` of 9 actions also has to become a configurable
  list: the fixture's `e` Examine and `t` Throw are not in the shipped array, and
  three of the nine shipped slots render `[Unbound globally!]` on a default keymap.
- **`hud_vitals` computes per-part effect state into `limb_color` / `label_hex`
  (`panels.cpp:872`) and then never uses it.** Bay 1's `BLD·BTN` tag, the L·ARM
  alarm ring, and the `BLEED L·ARM` / `BITE L·ARM` lamps all need that state as
  *data*, not folded into a colour. Needs: a per-body-part
  `{bleeding, bitten, splinted, bandaged, infected}` bitfield emitted alongside
  cur/max.
- **The crit gate is arithmetically dead.** `o.cur * 100 / o.max < 25` truncates
  `8*100/30` to `26`, so it never fires at the fixture — which is exactly the
  defect the baseline documents. This design hangs its entire bay-1 alarm layer
  off that gate, so it must be fixed to `cur * 4 < max`. It matters more here than
  in the shipping HUD: the 25% crit line is *drawn on the instrument* at
  `bottom: 24px` of a 96px well, so if the code's threshold and the printed
  threshold disagree the panel visibly lies about its own scale.

**New producers, none of which exist:**

- `hud_dial_stamina` — stamina as a ratio plus the redline and caution
  thresholds, so the `conic-gradient` arc is data-driven rather than hard-coded at
  15%/35% the way the mockup has it.
- `hud_annunciator` — an ordered list of `{legend, tier, lit}` for a **fixed-size**
  grid. The producer, not the RCSS, must own slot assignment, must emit dark
  lamps for unlit slots so the grid never reflows, and must keep a given concept
  in a given slot across turns. See Weakness 3 for why this is the hard one.
- `hud_scope` — per-visible-monster `{bearing_deg, range_tiles, designated,
  tracking}`. Nothing spatial about monsters is currently emitted anywhere;
  `hud_botbar` reads only the target's name and HP percentage. This is squarely
  the "needs a new producer that emits polar coordinates" case.
- `hud_armament` — `hud_botbar` emits `tname()` and nothing else. Bash/cut/to-hit
  come off `item::damage_melee` and the itype's `m_to_hit`; the sidearm row needs
  an inventory scan for the best-scoring stowed gun plus its magazine count, which
  no HUD producer does today. That scan must be cached per turn, not recomputed
  per frame — it is an inventory walk, and the HUD rebuilds its RML string on
  every sync.
- `hud_env` — wind, light level and the held light source's charge. All three are
  `DROPPED` in the shipping HUD; `light_level()` and the wind helpers exist in the
  codebase and no HUD producer calls them.
- `hud_encumbrance`, `hud_noise`, `hud_morale` — `morale_stat()` exists and is
  uncalled; encumbrance and noise have no HUD path whatsoever.

**Geometry:**

`sidebar_hud_apply_rect()` has to be replaced, not adjusted. This design has no
dock and no percentage-of-viewport rects; it wants two fixed dp bands (a 56dp
plate centred horizontally, a 240dp binnacle pinned to the bottom) and — this is
the important part — **the terrain carve-out must be derived from those dp heights
rather than from whole text cells**. If it is not, the design inherits the
baseline's `TERMY × 16 = 1072 ≠ 1080` defect verbatim: a black sliver along one
edge and chrome painted over live terrain along the other. Getting this right is
also what makes the 25.6% figure above real rather than nominal.

## Weaknesses

1. **1280x720 does not degrade, it fails.** The binnacle is authored in absolute
   px, with six bays summing to exactly 1920 (352 + 176 + 332 + 276 + 284 + 500),
   and nothing in it is fluid. The true minima: bay 1 needs 6 × 55 = 330px for the
   body-part columns; bay 3 is a hard 3 × 104 = 312px lamp grid; bays 2 and 4 are
   140px and 138px *circles*, and a dial below about 110px loses its numerals
   entirely; bay 6 needs 344px of tape for a 52-character line that already wraps
   at 1920. The rail is worse than the deck: MOVE MODE 150 + noise VU 200 +
   encumbrance 116 + nine sockets at 65px pitch 585 + attributes 300 + two 214px
   meters 428 = **1779px of hard minimum in a 1280px rail**, 500px over budget
   before any padding. And the vertical cost scales against you: 240 + 56 = 296px
   of chrome is 27.4% of 1080 rows but **41.1% of 720**, which is not a HUD, it is
   a letterbox. There is no scale factor that fixes this — the honest 720p answer
   is a different arrangement (bays 5 and 6 demoted to on-demand overlays, the
   annunciator halved to two columns, the round instruments replaced by edgewise
   strips), which means shipping two layouts.

2. **The analog register is fast for "is it bad" and slow for "what is it", and
   the design keeps conceding the second half.** Reading `5100/8700` off a needle
   at 23.3° means interpolating between the `0` and `25` numerals — perfectly good
   for "low, above the redline", useless when you need to know whether you can
   afford one more sprint. The design admits this by printing `59%` in a nixie
   *inside the dial face* and `5100/8700` in the bay header, so one value carries
   three encodings and the 140x140px instrument (19,600 px², 0.95% of the screen)
   is spent on a glance-level cue that the 40x18px digital window already
   provides. The same charge applies to all four edgewise meters and the noise VU,
   every one of which carries its own digits. If you deleted every needle on this
   panel you would lose the peripheral layer and lose almost no information. The
   only defence is that the peripheral layer is precisely what the shipping HUD
   has none of — but it is a defence, not a refutation, and it costs roughly a
   quarter of the deck's area. (Colour-blindness, by contrast, is largely fine
   here: needle *angle*, fill *height*, cap *depth*, hazard *striping* and the
   digits themselves are all non-chromatic and all redundant with the hue. The one
   genuine loss is the annunciator's four-tier lamp colour, which encodes severity
   with nothing else carrying it — `SOAKED` and `BLEED L·ARM` differ only by hue.
   That wants a tier glyph in the legend.)

3. **Twelve effects overflow a fixed-position annunciator, and fixed positions are
   the entire point.** The panel is a hard 3 × 5 grid: 15 slots, 12 lit, 3 dark
   reserve (`RADIATION`, `INFECTED`, `BROKEN LIMB`). Its value proposition is that
   a lamp is *always in the same place*, so the player learns the shape of "bad"
   peripherally rather than reading twelve words — which is exactly why it cannot
   grow, scroll, paginate or reflow. But nine of the twelve lit slots are already
   spoken for by fixed concepts (safe mode, contact count, tracking count, pain,
   hunger, thirst, fatigue, warmth, light charge), leaving **three
   general-purpose slots** for an effect system with hundreds of entries. At twelve
   simultaneous effects the producer must either drop nine of them or start
   reassigning legends per turn — and a lamp whose meaning changes between turns
   is strictly worse than no lamp at all, because the player is reading *position*
   and position now lies. There is no clean fix inside this register. The
   least-bad one is a 16th `+N MORE` lamp feeding an on-demand overlay, which
   concedes that the annunciator is a summary and not a display.

4. **613 elements, and RmlUi has no generated content.** The rendered file
   contains 613 `div`s inside `#screen`, of which roughly 330 are pure ornament:
   121 minimap cells, 32 dial and VU ticks, 26 rivets, 17 magazine segments, 12
   sprocket holes, 8 seams, plus every glass and scanline overlay. Because RCSS
   has no `::before` / `::after`, every one of them must be a real element in the
   RML string that `panels.cpp` concatenates. The shipping HUD's entire dock is a
   few dozen elements; this is an order of magnitude more, rebuilt per turn, with
   the 121-cell chart alone carrying an inline `decorator` declaration each. That
   is a per-turn allocation and a style-matching cost the current HUD does not
   pay. The mitigation is real but partial: the ~74 elements that are genuinely
   static across the whole game (ticks, rivets, seams, sprocket holes, bay
   headers, scale legends) belong in the `.rml` document authored once, leaving
   the producers to emit only the ~250 that carry data. That still leaves the
   minimap as the single largest cost centre in the HUD, for the least
   information-dense instrument on the panel.
