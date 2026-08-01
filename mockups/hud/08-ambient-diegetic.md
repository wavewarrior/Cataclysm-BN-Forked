# 08 — Proprioception

## Thesis

This design argues that **the most readable HUD is the one you never read**: that a
survival roguelike's continuous state — how hurt you are and roughly where, how
loud you just were, how far your light reaches, where the things that want to
kill you are standing — belongs in the *presentation of the world itself*, not in
a panel beside it, and that explicit numbers should exist only on demand. So the
resting state has no chrome at all. There is no dock, no strip, no bar, no box.
An injury is a red bleed on the screen edge nearest the wound; noise is a ring
whose radius *is* the volume in tiles; the torch is a warm pool with a charge
sweep at its own reach; contacts are diamonds sitting on the sprites, and the one
tracking you is promoted from a diamond to a reticle; the newest one or two log
lines float in the world beside the avatar and fade on a timer. Everything else —
the six exact HP values, the stat block, the effect durations, the armament, the
overmap, the full log, the hotbar — lives behind a held key in a single right-edge
panel. What it trades away is **cardinality**. Measured, the resting layer is a
purely ordinal instrument: it tells you *worse* and *where*, never *how much*. It
answers "am I in trouble" in one saccade and cannot answer "will this next hit
break my arm" at all, and the state that can answer that costs 24.0% of the screen
while it is held. It also spends its budget in luminance rather than area, which
at 21:47 under 12% moonlight is the most expensive currency available.

## Register

Nothing here is a panel treatment; the register *is* the tint stack. Every hex
below is read out of the file.

### Ambient channels — injury (left/top/bottom edges)

| token | hex / rgba | role |
|---|---|---|
| `injury-core` | `rgba(133,16,20,.66)` `#851014` | `#w-larm` stop 0 — the wound edge at x=0 |
| `injury-mid` | `rgba(124,20,25,.44)` `#7c1419` | stop at 34px |
| `injury-fall` | `rgba(115,20,24,.21)` `#731418` | stop at 96px |
| `injury-tail` | `rgba(108,20,24,.07)` `#6c1418` | stop at 152px, to `0` at 206px |
| `tear-deep` | `rgba(48,4,7,.52)` `#300407` | `#w-larm-tear` crack, 4px band |
| `tear-thin` | `rgba(28,2,4,.36)` `#1c0204` | crack, 3px band |
| `shard` | `rgba(24,2,4,.66)` `#180204` | 8 hard chips off the wound edge |
| `rim-glow` | `rgba(216,78,78,.60)` `#d84e4e` | 5 × 2px vertical rim lights, `#c2323a` at 0 alpha |
| `drip-top` / `drip-tail` | `rgba(142,26,30,.88)` `#8e1a1e` → `rgba(198,54,60,.92)` `#c6363c` | 3 running drips |
| `bead` | `rgba(216,90,90,.95)` → `rgba(126,20,23,.92)` `#d85a5a`/`#7e1417` | 3 gathered beads at drip ends |
| `wash-part` | `rgba(128,26,28,.19)` `#801a1c` | `#w-head` (top) and `#w-lleg` (bottom-left) shoulder wash |

### Ambient channels — world, light, weather

| token | hex / rgba | role |
|---|---|---|
| `night-1` | `rgba(9,12,24,.06)` `#090c18` | `#night` at r=520 — cool inner falloff |
| `night-2` | `rgba(19,10,15,.15)` `#130a0f` | r=700 |
| `night-3` | `rgba(23,9,13,.25)` `#17090d` | r=900 |
| `night-4` | `rgba(25,8,11,.34)` `#19080b` | r=1140 — the darkest wash, and it is *warm* dark, not blue |
| `torch-hot` | `rgba(255,216,154,.22)` `#ffd89a` | `#pool` at r=0 |
| `torch-mid` | `rgba(255,206,140,.16)` `#ffce8c` | r=96 |
| `torch-fall` | `rgba(248,182,104,.095)` `#f8b668` | r=208 |
| `torch-edge` | `rgba(228,150,74,.038)` `#e4964a` | r=300, to `#d2823c` at 0 alpha, r=352 |
| `rain-lit` | `rgba(234,244,252,.22)` `#eaf4fc` | `#poolrain` — drizzle **only inside the torch pool** |
| `rain-amb` | `rgba(199,214,224,.075/.045)` `#c7d6e0` | `#rain` — global drizzle, two densities |
| `chill-edge` | `rgba(133,188,222,.34/.30/.24)` `#85bcde` | `#chill` inset hairlines, top/right/bottom |
| `chill-glow` | `rgba(96,152,196,.12)` `#6098c4` | 38px inner bloom |
| `frost` | `rgba(156,204,232,.32/.17)` `#9ccce8` | `#frost` — 22px dotted frieze, top edge |

### Ambient channels — reach, noise, threat

| token | hex / rgba | role |
|---|---|---|
| `noise-ring` | `rgba(204,220,234,.15)` `#ccdcea` | `#noise` 1px stroke; radius **is** noise-made in tiles |
| `noise-bloom` | `rgba(176,200,220,.04)` `#b0c8dc` | 46px inner bloom on the ring |
| `charge-on` | `rgba(255,210,144,.88)` `#ffd290` | `#batt` lit arc = torch charge; also `#leader`, `#notch` |
| `charge-off` | `rgba(120,110,96,.28)` `#786e60` | `#batt` spent arc |
| `collar` | `rgba(232,228,216,.10)` `#e8e4d8` | `#collar` — the faint bearing ring around the avatar |
| `threat` | `rgba(255,112,67,.94)` `#ff7043` | `#bearing` wedge, `#tether`, `#trk-hp` fill, reticle ticks |
| `threat-trough` | `rgba(80,36,28,.60)` `#50241c` | `#trk-hp` spent arc |
| `safe-bracket` | `rgba(210,228,236,.46)` `#d2e4ec` | outer reticle brackets = SAFE MODE ON |
| `contact` | `rgba(255,196,80,.88)` `#ffc450` + glow `#ffb43c` | `.glint` diamonds on untracked hostiles |
| `msg-now` | `#eebd4c` | freshest floating log line, opacity .97; also thirst glyph |
| `msg-prev` | `#c8a23e` | previous floating line, opacity .44 |
| `need-hun`/`fat`/`cld` | `#e8e4d8` / `#e0a341` / `#95c9ea` | the four need glyphs; **opacity carries severity** |

### Revealed panel

| token | hex | role |
|---|---|---|
| `pan-bg-a` / `pan-bg-b` | `rgba(9,12,17,.85)` / `rgba(7,10,15,.78)` | top/mid stops of the panel wash, `.87` at the foot |
| `pan-edge-in` / `pan-edge-out` | `rgba(232,228,216,.12)` / `.05` | 1px inset left / right rules — the only frame |
| `ink-hi` | `#f2eee2` | `NIGEL ASHBY`, tracked contact, player cell |
| `ink` | `#e8e4d8` | body values, `.bv`, `.gn` |
| `ink-2` … `ink-6` | `#d2cdbf` `#bcb8ab` `#9a9689` `#8a8780` `#7d7a72` | descending text tiers |
| `ink-7` / `ink-8` / `ink-9` | `#6e6b64` / `#5a5750` / `#4c4a45` / `#3f3d38` | labels, log times, dead chips, ghost glyphs |
| `ok` | `#8fbf6a` | full-health segments, `safe mode on` |
| `warn` / `warn-2` | `#e0b341` / `#d9b25e` | 75–85% segments, stamina, caution log rows |
| `hurt` | `#d9663a` | `.sg.on.hurt` (declared, unselected at this fixture) |
| `crit` | `#c8323c` | L-arm segments, `bleeding`/`bitten`, bad log rows |
| `crit-lab` / `crit-val` / `crit-edge` | `#e38186` / `#f0a0a4` / `#e0454f` | the crit row's label, value and edge glyph |
| `cold` / `cold-2` | `#7fb6d9` / `#95c9ea` | `soaked` segments, the weather line |
| `edge-lit` | `#cd8f5c` | `.bedge.lit` — this row's ambient channel is firing |
| `bad` | `#dd8060` | negative morale |
| `map-field`/`forest`/`house` | `#23271d` / `#33482c` / `#4e4c46` | overmap cells |
| `map-mission` / `map-player` | `rgba(224,179,65,.22)`+`#e0b341` / `rgba(242,238,226,.18)`+`#f2eee2` | mission ▲, player ■ |
| `key-cap` | `#bcb8ab` on `#0b0f14` | hotbar key caps (inverted) |

### Type

One face: `Consolas, "Cascadia Mono", "Lucida Console", monospace` — the contract's
metric stand-in for the game's Source Code Pro. **Ten distinct sizes**, which is
the register's weakest point and is stated plainly here rather than hidden:

| px | letter-spacing | used by |
|---|---|---|
| 19 | 5.5 | `.hdr-name` — `NIGEL ASHBY`, the only display-scale text |
| 16 | — | `#needs` glyphs (lh 22) |
| 14 | .2 | `#float` world-space log lines (lh 20) |
| 11.5 | .4 / .5 / .7 | `.brow`, `.srow`, `.grow`, `.hdr-line` — the panel's body tier |
| 11 | .25 / 1.4 / .3 | `.erow`, `.crow`, `.lrow`, `.chip`, `.hdr-sub`, `.bedge` |
| 10.5 | — | `.bnote`, `.el`, `.ed`, `.gd` — annotation tier |
| 10 | .8 | `.lt` log times, `.cs`, `.cmeta`, `.k` key caps (lh 15) |
| 9.5 | 3.4 / 2.2 / .6 | `.lab` section labels, `.gk`, `.deadnote` |
| 9 | 1.6 | `.cm` tracked marker, `.mcap` |
| 8 | — | `.mc` overmap cell glyphs (lh 10) |

The three widely-tracked sizes (`.lab` at 3.4px, `.gk` at 2.2px, `.hdr-name` at
5.5px) are the only ornament in the panel. Non-Latin glyphs used as data:
`↑ ↓ ← → □ ► ▲ ■ ○ ≈ ▼ *` (U+2191/2193/2190/2192/25A1/25BA/25B2/25A0/25CB/2248/25BC).

### Border / frame

There is **no border anywhere in the resting state** — not one stroke that is
chrome rather than data. Every visible line is a measurement: the noise ring is
the noise value, the collar is the bearing rose, the reticle brackets are safe
mode. The revealed panel's entire frame is two 1px inset box-shadow rules
(`rgba(232,228,216,.12)` left, `.05` right) plus a `-22px 0 54px rgba(0,0,0,.52)`
outer shadow that feathers it into the world, and eight 1px `.rule` dividers whose
gradient decays left-to-right (`.22 → .05 at 72% → 0`) so they never read as a
closed box. The `#notch` — a 2×38 warm sliver on the panel's left edge at y=512 —
is where the world's `#leader` line lands, and it is the only join between the two
states.

## Layout

### Resting ambient layer (measured in Chromium at 1920x1080)

Six full-screen `.lyr` planes at (0,0,1920,1080) carry the world channels; the
rest are discrete marks. The avatar point is (944,528) per the contract, and three
of the four rings are centred on it exactly.

| element | x | y | w | h | construction |
|---|---|---|---|---|---|
| `#grade` | 0 | 0 | 1920 | 1080 | `backdrop-filter: saturate(.86) brightness(.94) contrast(1.03)` — no paint |
| `#night` | 0 | 0 | 1920 | 1080 | radial `circle 1140px at 944px 528px`; transparent to r=400, `.34` at r=1140 |
| `#pool` | 0 | 0 | 1920 | 1080 | radial `circle 352px at 944px 528px`; `.22` at centre → 0 at r=352 |
| `#poolrain` | 0 | 0 | 1920 | 1080 | 101° streaks, 16px period, 1px at `.22`; masked to r=330 (`.85`→`.45` at 200→0) |
| `#rain` | 0 | 0 | 1920 | 1080 | 101° streaks, 48px period, 1px at `.075` + 1px at `.045` |
| `#periph` | 0 | 0 | 1920 | 1080 | `backdrop-filter: saturate(.40) brightness(.85)`; masked 0 to r=470, `.30`@690, `.72`@900, `1`@1130; `opacity:.93` |
| `#w-larm` | 0 | 0 | **206** | 1080 | 90° red gradient, `.66`@0 → 0@206 |
| `#w-larm-tear` | 0 | 0 | **152** | 1080 | 7° crack pattern (34px period), masked `1`@0 → 0@152 |
| `.shard` ×8 | 0 | 96 / 214 / 352 / 468 / 588 / 726 / 858 / 972 | 68–146 | 4–7 | `rotate(±2…6deg)` about `0% 50%` |
| `.glint-rim` ×5 | 0 | 60 / 300 / 540 / 760 / 930 | 2 | 120/180/140/110/120 | vertical rim lights |
| `.drip` ×3 | 4 / 20 / 34 | 359 / 596 / 222 | 3 | 152 / 96 / 112 | running blood |
| `.bead` ×3 | 2 / 18 / 32 | 508 / 689 / 331 | 6 | 7 | at each drip's foot |
| `#w-head` | 0 | 0 | 1920 | **60** | 180° red wash, `.19`@0 → 0@60 |
| `#w-lleg` | 0 | **1026** | **912** | **54** | 0° red wash, `.21`@bottom → 0@54 |
| `#chill` | 0 | 0 | 1920 | 1080 | inset 1px hairlines top `.34` / bottom `.24` / right `.30` + 38px `.12` bloom; masked out for x<210, full from x=380 |
| `#frost` | 0 | 0 | 1920 | **22** | 90° dotted blue frieze (16px period), masked `1`→0 downward |
| `#noise` | 688 | 272 | **512** | **512** | centre **(944,528)** exactly; 1px stroke at r=255.5 |
| `#batt` | 640 | 224 | 606 | 606 | centre **(943,527)** — 1px off the avatar; conic `from -61.2deg`, lit 0→122.4°, masked to a 4px ring at r≈301 |
| `#collar` | 824 | 408 | 240 | 240 | centre (944,528); 1px stroke at r=119 |
| `#bearing` | 824 | 408 | 240 | 240 | centre (944,528); conic `from -8.7deg`, 13° wedge spanning absolute −6.7°→+6.3°, masked to a ring r 111–120 |
| `#tether` | 953 | 412 | 1 | 92 | `rotate(4deg)` about `50% 100%` — runs from the collar's crown up to the reticle |
| `#trk-hp` | 928 | 352 | 64 | 64 | centre **(960,384)** = the brute's sprite; conic 223.2° lit, masked to a 4px ring at r=22 |
| `.tick` ×8 | 928–991 | 352–415 | 12×2 / 2×12 | | orange corner brackets, 42px box |
| `.tick.safe` ×8 | 928–991 | 352–415 | 10×1 / 1×10 | | pale outer brackets, 64px box |
| `.tail` ×2 | 994 / 990 | 366 / 355 | 26 / 17 | 2 | `rotate(-30deg)` — motion trail |
| `.glint` ×2 | 923 / 1211 | 635 / 507 | 9 | 9 | `rotate(45deg)` diamonds + 8px/2px glow |
| `#float` | 1004 | 552 | 340 | 40 | two 20px lines; measured ink 268.52px and 189.55px; double `drop-shadow` |
| `#needs` | 866 | 994 | 156 | 22 | flex `space-between`; glyphs at x 866 / 915 / 964 / 1013, 8.8px each |

**Why the geometry is where it is.** Three rings share the avatar point, so the
whole reach system reads as concentric: `#collar` r=119 (3.7 tiles, the bearing
rose), `#noise` r=255.5 (**7.98 tiles — the fixture's `noise 8`, to within a
rounding**), `#batt` r≈301 (9.4 tiles, the torch's own claimed reach). `#pool`
tops out at r=352 and `#poolrain` is masked to r=330, so the lit rain sits just
inside the light that lights it. `#periph` starts desaturating at r=470 — outside
every data ring — so no measurement is ever taken in degraded pixels.
`#needs` is centred on x=944, the avatar's column, 466px below it; `#float` sits
60px right and 24px below the avatar, reading as annotation attached to the body.
The injury channel owns the left 206px and `#chill` is explicitly masked out below
x=210, so the two never composite on the same pixels.

### Revealed overlay (measured)

| element | x | y | w | h |
|---|---|---|---|---|
| `#leader-dot` | 1291 | 525 | 5 | 5 |
| `#leader` | 1294 | 527 | 106 | 1 |
| `#reveal` | **1400** | **40** | **496** | **1004** |
| `#notch` | 1400 | 512 | 2 | 38 |
| content box | 1424 | 62 | **448** | **962** |

(The panel's *declared* left edge is 1400, not the ~1140 a screenshot suggests:
`backdrop-filter: blur(11px)` plus a `-22px 0 54px` outer shadow feathers the
boundary leftwards to about x=1324, and the `#leader-dot` at x=1291 extends the
visual complex further still. 1400→1896 leaves a 24px right gutter; 40→1044 leaves
a 36px foot.)

Panel interior, top to bottom — all measured, all left-aligned at x=1424 across
448px unless noted:

| block | y | h | detail |
|---|---|---|---|
| `.hdr-name` | 62 | 22 | `NIGEL ASHBY` at 19px/5.5px tracking |
| `.hdr-sub` | 89 | 13 | `survivor · cheswick — residential` |
| `.hdr-line` ×3 | 105 / 122 / 139 | 14 | day/season/time · weather+temp+wind · moonlight+torch |
| `.rule` | 166 | 1 | |
| `.lab` BODY | 178 | 11 | |
| `.brow` ×6 | 196 / 214 / 232 / 250 / 268 / 286 | 18 | `.bp` 60 · `.bv` 64 right · `.seg` 104 at x=1558 · `.bedge` 16 · `.bnote` flex |
| `.rule` | 317 | 1 | |
| `.brow.stam` | 329 | 20 | |
| `.rule` | 362 | 1 | |
| `.lab` STATE | 374 | 11 | |
| `.srow` ×3 | 392 / 409 / 426 | 17 | four 104px columns; row 3 is `.wide` with a flex spacer |
| `.rule` | 456 | 1 | |
| `.lab` EFFECTS | 468 | 11 | |
| `.erow` ×5 | 486 / 503 / 520 / 537 / 554 | 17 | `.en` 80 · `.el` 58 · `.seg` 44 (7 × 5px) · `.ed` right |
| `.rule` | 584 | 1 | |
| `.grow` ×2 | 596 / 614 | 18 | `.gk` 52 · `.gn` 104 · `.gd` flex |
| `.rule` | 645 | 1 | |
| `.split` | 657 | 138 | `.colL` 244 @1424 · `.colR` 204 @1668 (20px pad → 184) |
| `.lab` CONTACTS / `.cmeta` / `.crow` ×3 | 657 / 675 / 693,710,727 | 11/12/17 | inside `.colL` |
| overmap `.mr` ×11 | 657 → 767, step 11 | 11 | 121 `.mc` at 10×10 + 1px gap → an exact **121×121 inset at (1688,657)** |
| `.mcap` | 785 | 10 | `OVERMAP · MISSION NE` |
| `.rule` | 808 | 1 | |
| `.lab` LOG | 820 | 11 | |
| `.lrow` ×6 | 838 / 854 / 870 / 886 / 902 / 918 | 16 | `.lt` 42 · `.lx` flex |
| `.rule` | 947 | 1 | |
| `#hot` | 959 | 63 | 9 `.chip` 144×21, flex-wrapped 3×3 at y 959/980/1001 (432 ink in 448) |
| `.deadnote` | 1028 | 11 | bottom edge at **y=1039** |

**The panel is full to five pixels.** Measured `scrollHeight` 1019 against
`clientHeight` 1004: the content overruns its 962px content box (which ends at
y=1024) by 15px, spills through the 20px bottom padding, and stops at y=1039 —
five pixels short of the padding box at y=1044 where `overflow: hidden` clips.
Nothing is lost at this fixture and there is no headroom whatsoever.

## Data mapping

Two states, so every row names which one it lives in: **A** = resting ambient,
**R** = revealed overlay, **A+R** = both.

| fixture field | state | where | encoding |
|---|---|---|---|
| Name `Nigel Ashby` | **R** | `.hdr-name` (1424,62) | text, 19px/5.5px tracking, `#f2eee2` |
| Profession `Survivor` | **R** | `.hdr-sub` | text, `#7d7a72` |
| Day `14` | **R** | `.hdr-line` 1 | text |
| Time `21:47` | **R** | `.hdr-line` 1 | text |
| Season `Autumn` | **R** | `.hdr-line` 1 | text |
| Place `Cheswick — residential` | **R** | `.hdr-sub` | text |
| Weather `Light drizzle` | **A+R** | A: `#rain` 101° streaks over the whole frame, denser inside `#poolrain`. R: `.hdr-line` 2 | ambient texture + text |
| Temperature `8 °C` | **A+R** | A: `#chill` blue hairlines + `#frost` frieze. R: `.hdr-line` 2, `#95c9ea` | ambient edge + colour + text |
| Wind `NE 12 km/h` | **R** | `.hdr-line` 2 | text. Partly ambient: the 101° rain angle is the wind's, but no ambient channel carries its *speed* |
| Light level `12%` (moonlight) | **A+R** | A: `#night`, transparent to r=400 and `.34` warm-dark at r=1140, plus `#periph` desaturating from r=470. R: `.hdr-line` 3 | ambient falloff + text |
| Flashlight `34%` charge | **A+R** | A: `#batt` conic ring at r≈301 — `122.4° / 360° = 34.0%` lit, arc centred on 12 o'clock. R: `torch 34%` | **arc angle** + text |
| STR/DEX/INT/PER `10/12/9/11` | **R** | `.srow` 1 (y=392) | numeric, 4 × 104px columns |
| Speed `96` | **R** | `.srow` 2 | numeric |
| Focus `84` | **R** | `.srow` 2 | numeric |
| Pain `12` | **R** | `.srow` 2 **and** `.erow` 5 `pain 12` with a 2/7 duration bar | numeric ×2 + segment bar |
| Morale `-3` | **R** | `.srow` 2, `.bad` `#dd8060` | numeric + colour (the shipping HUD drops this entirely) |
| Encumbrance torso `12` | **R** | `.brow` torso `.bnote` `enc 12`, and `.srow` 3 | numeric, twice |
| Encumbrance arms `4` | **R** | `.brow` r arm `.bnote` `enc 4`, and `.srow` 3 | numeric, twice |
| Move mode `walking` | **R** | `.srow` 3 `gait walking` | text |
| Noise made `8` | **A+R** | A: `#noise` ring, r=255.5px = **7.98 tiles at 32px/tile — the radius *is* the value**. R: `.srow` 3 `noise 8` | **radius** + numeric |
| Head 20/24 | **A+R** | A: `#w-head`, a 1920×60 red wash on the top edge (`.19` peak). R: `.brow` y=196, `20/24`, 17/20 warn segments, `.bedge ↑ lit` | ambient edge + numeric + segment bar |
| Torso 45/60 | **A+R** (weakly) | A: **no dedicated edge** — `.bedge □ lit` claims a channel that is not painted. R: `.brow` y=214, `45/60`, 15/20 segments | numeric + segment bar; ambient encoding is absent, see Weaknesses 3 |
| L arm 8/30 | **A+R** | A: `#w-larm` 206×1080 left-edge bleed, `.66`→0, plus `#w-larm-tear`, 8 shards, 5 rim glows, 3 drips + beads. R: `.brow` y=232, `8/30`, 5/20 crit segments, `.bedge ← lit` `#e0454f` | ambient edge (ordinal only) + numeric + segment bar |
| L arm **critical** | **A+R** | A: the bleed is the loudest ambient channel on screen — 206px wide, `.66` peak, torn, dripping. R: `.brow.crit` recolours the whole row (`.bp #e38186`, `.bv #f0a0a4`, `.bedge #e0454f`, `.bnote #c8323c`) | **This design fixes the shipping bug.** The crit class is applied by state, not by `cur*100/max < 25`, so `8/30` (26.67%) renders critical instead of silently falling through |
| L arm **bleeding, bitten** | **A+R** | A: `.drip`/`.bead` are the bleeding channel and sit *on* the wound edge, not 891px away. R: `.bnote` `bleeding · bitten` **on the L-arm row itself**, plus `.erow` 1–2 with `l arm` locus and durations | drip glyphs + per-part annotation. Fixes the baseline's discarded `label_hex` |
| R arm 30/30 | **R** | `.brow` y=250, `30/30`, 20/20 ok segments, `.bedge → ghost` | numeric + segment bar; **no ambient channel** — the ghost glyph says so |
| L leg 22/28 | **A+R** | A: `#w-lleg`, 912×54 on the bottom-**left** (`.21` peak). R: `.brow` y=268, `22/28`, 16/20 segments, `.bedge ↓ lit` | ambient edge + numeric + segment bar |
| R leg 28/28 | **R** | `.brow` y=286, `28/28`, 20/20 segments, `.bedge ↓ ghost` | numeric + segment bar; the bottom-right 1008px is deliberately unpainted |
| Stamina `5100/8700` | **R** | `.brow.stam` y=329, `5100/8700`, 12/20 segments, `.bnote 59% · winded below 25%` | numeric + segment bar + threshold text |
| Hunger `Peckish` | **A+R** | A: `#needs` `○` at (866,994), `#e8e4d8` at **opacity .38** — the dimmest of four, i.e. the mildest. R: — | **glyph + opacity as severity** |
| Thirst `Thirsty` | **A+R** | A: `#needs` `≈`, `#eebd4c` at .88. R: also the freshest floating line `You are getting thirsty.` | glyph + colour + opacity |
| Fatigue `Tired` | **A+R** | A: `#needs` `▼`, `#e0a341` at .80. R: `.erow` 4 `tired` 5/7 | glyph + colour + segment bar |
| Warmth `Chilly` | **A+R** | A: `#needs` `*`, `#95c9ea` at .88, **plus** `#chill` + `#frost`. R: `.hdr-line` 2 in `#95c9ea` | glyph + ambient edge (the shipping HUD drops warmth entirely) |
| Effect `Bleeding (L arm)` | **A+R** | A: drips/beads on the wound edge. R: `.erow` 1, `#c8323c`, locus `l arm`, 5/7 bar, `~14 t` | glyph + text + duration bar |
| Effect `Bitten (L arm)` | **A+R** | A: the `#w-larm-tear` crack pattern is the bite, distinct from the bleed's smooth falloff. R: `.erow` 2, 3/7, `~38 t` | texture + text + duration bar |
| Effect `Soaked` | **A+R** | A: `#chill` + `#frost` + `#rain`. R: `.erow` 3, `#95c9ea`, `body`, 4/7 | ambient + text + duration bar |
| Effect `Tired` | **A+R** | A: `#needs ▼`. R: `.erow` 4, 5/7 | glyph + duration bar |
| Effect `Pain 12` | **R** | `.erow` 5, 2/7, `~6 t` | text + duration bar. **No ambient channel** — pain is the one effect with no world encoding |
| Wielded `fire axe` | **R** | `.grow` HANDS y=596 | text |
| `32 bash / 0 cut / +1 to-hit` | **R** | `.grow` HANDS `.gd` | text (the shipping HUD drops all three) |
| Sidearm `Glock 19 12/17 9mm JHP` | **R** | `.grow.idle` PACK y=614, dimmed to `#9a9689` | text + dim state = not in hand |
| Target `zombie brute` | **A+R** | A: promoted from a `.glint` diamond to the full reticle at (928,352) — ring + 8 orange ticks + 2 motion tails + `#bearing` wedge + `#tether`. R: `.crow.trk` y=693, `#f2eee2` | **promotion** + text |
| Target HP `62%` | **A+R** | A: `#trk-hp` conic — `223.2° / 360° = 62.0%` lit, on a 4px ring at r=22 around the sprite. R: `.crow.trk .ch` `62%` in `#ff7043` | **arc angle** + numeric. Fixes the baseline's permanently-empty trough |
| Target `4 tiles NE` | **A+R** | A: `#bearing`, a 13° wedge on the r≈115.5 collar whose east edge lands on the sprite's true bearing (+6.3° from north), plus `#tether` pointing at it. R: `.crow.trk .cd` `4 NE` | **wedge angle** + text. Note the ambient bearing is computed from the sprite's screen position (4.53 tiles, +6.3°) while the text repeats the fixture's `4 NE` — see Feasibility |
| Target status `moving` | **A+R** | A: two `.tail` streaks at `rotate(-30deg)` trailing the reticle. R: `.crow.trk .cs` `moving` | motion glyph + text |
| `3 hostiles visible` | **A+R** | A: three marks on screen — one reticle at (960,384) + `.glint` at (923,635) (≈3.4 tiles S) and (1211,507) (≈8.4 tiles E), matching the fixture's `3 S` and `9 E`. R: `.cmeta` `3 visible` + three `.crow`s | **count by presence, positioned in world space** + numeric |
| `1 tracking` | **A+R** | A: exactly one contact is a reticle, the other two are bare diamonds. R: `.cmeta` `1 tracked` + `.crow.trk ►` | **promotion** + numeric + glyph |
| `SAFE MODE ON` | **A+R** | A: the 8 pale `.tick.safe` outer brackets at a 64px box — safe mode is what draws the confirm frame. R: `.cmeta` `safe mode on` in `#8fbf6a` | bracket glyphs + text + colour |
| Log row 6 `You are getting thirsty.` | **A+R** | A: `#float .now` (1004,572), `#eebd4c` at opacity .97. R: `.lrow.caut.a0` | floating world text + dock row |
| Log row 5 `The zombie shambler groans nearby.` | **A+R** | A: `#float .prev` (1004,552), `#c8a23e` at opacity .44. R: `.lrow.caut.a1` | floating world text + dock row |
| Log rows 1–4 | **R** | `.lrow` y=838/854/870/886 | text + type colour (`good #bcb8ab`, `bad #c8323c`, `dim #9c544e`) + a 6-step opacity ramp `.42/.54/.68/.80/.90/1` |
| Log colour semantics | **A+R** | R: the ramp spans 58 points (`.42`→`1`), against the baseline's 21 — a spread a reader can actually order. A: only two rows exist, at `.44` and `.97` | opacity + hue |
| Minimap 11x11 | **R** | `.colR`, an exact 121×121 inset at (1688,657) | 121 coloured 10×10 cells, no glyphs except mission/player |
| Player position | **R** | centre cell (1738,707) | `rgba(242,238,226,.18)` cell + `■` in `#f2eee2` |
| Mission marker NE | **R** | row 2 col 10, plus two more `▲` POIs at (4,2) and (9,5) | `rgba(224,179,65,.22)` cell + `▲` in `#e0b341`; `.mcap` states `MISSION NE` |
| Hotbar 9 slots | **R** | `#hot` (1424,959), 3×3 of 144×21 chips | inverted key cap + label; **all nine present**, including `e` Examine and `t` Throw which the shipping producer's fixed array cannot emit |
| Hotbar slot 1 **unavailable** | **R** | `.chip.dead` — opacity .6, cap knocked back to `rgba(232,228,216,.14)` on `#5a5750`, label `#4c4a45`, plus `.deadnote` `fire unavailable — no firearm wielded` at y=1028 | **opacity + inversion loss + explicit reason string.** Fixes the baseline's dropped state |
| — | **R** | `.bedge` column, 16px at x≈1768 | **Not a fixture field; the design's own legend.** `↑ ↓ ← → □` name which screen edge carries this row at rest, `.lit #cd8f5c` when that edge is currently painted, `.ghost #3f3d38` when it is not. This is how the revealed state teaches the resting state |

**Nothing in the fixture is dropped.** All 40 fields are recoverable, which is 16
more than the shipping HUD carries. The cost is that 24 of them are recoverable
*only* while a key is held.

## Occlusion budget

Play area `1920 x 1080 = 2,073,600 px²`. Baseline to beat: **31.0%**
(`00-current-baseline.md`, union of the six real rects, 643,227 px²).

**Method, stated because this design forces the question.** The contract says
count opaque or near-opaque at full weight and translucent scrims at half weight.
Almost nothing in this design's resting state *is* a scrim, so a bare geometric
union would report six full-screen planes and score over 300%, which is nonsense.
The rule used here, applied identically to both states:

1. For every painted layer, **equivalent occluded area = painted area × mean
   alpha**, integrating the gradient's own stop list (piecewise-linear, and for
   the radial layers by 2px-grid numerical integration over the actual 1920x1080
   frame, so off-screen gradient tail is never counted).
2. **Translucent world tints then take the contract's ×0.5.** Discrete ink —
   strokes, glyphs, text — takes full weight, because it replaces pixels rather
   than tinting them.
3. `#grade` is scored **zero** and the reason is stated: it is
   `backdrop-filter: saturate(.86) brightness(.94) contrast(1.03)` with no paint
   at all. A uniform 6% luminance trim with a 3% contrast lift on top removes no
   terrain detail; it is a colour grade, not coverage. It is nevertheless present
   in every *measured* pixel figure quoted below, so the two methods are not being
   played off each other.

### Resting state — tint layers

| layer | painted area | mean α | equiv px² |
|---|---|---|---|
| `#grade` `brightness(.94)` | 2,073,600 | — | **0** (grade, not coverage) |
| `#night` radial r=1140 | 2,073,600 | 0.10906 | 226,147 |
| `#pool` radial r=352 | 389,256 | 0.07682 | 29,904 |
| `#poolrain` (16px period × mask r=330) | 2,073,600 | 0.00078 | 1,623 |
| `#rain` (48px period, `.075`+`.045`) | 2,073,600 | 0.00250 | 5,184 |
| `#periph` `brightness(.85)` × mask × `opacity .93` | 2,073,600 | 0.03672 | 76,147 |
| `#w-larm` 206×1080 | 222,480 | 0.23583 | 52,466 |
| `#w-larm-tear` 152×1080 | 164,160 | 0.03962 | 6,504 |
| `#w-head` 1920×60 | 115,200 | 0.08117 | 9,350 |
| `#w-lleg` 912×54 | 49,248 | 0.08167 | 4,022 |
| `#chill` hairlines + 38px bloom | 168,870 | 0.04840 | 8,167 |
| `#frost` 1920×22 | 42,240 | 0.02531 | 1,069 |
| **tint subtotal** | | | **420,584** |

    420,584 x 0.5 (translucent rule)                  = 210,292 px²

### Resting state — discrete ink

`.shard`×8 2,804 · `.glint-rim`×5 402 · `.drip`×3 918 · `.bead`×3 117 ·
`#noise` stroke 240 + bloom 939 · `#batt` ring 3,661 · `#collar` 74 ·
`#bearing` 160 · `#tether` 30 · `#trk-hp` 441 · `.tick`×8 180 ·
`.tick.safe`×8 37 · `.tail`×2 17 · `.glint`×2 143 + glow 152 ·
`#float` two measured line boxes (268.52+189.55)×20 = 9,161 ·
`#needs` 156×22 = 3,432.

    ink subtotal                                      =  22,909 px²  (1.105%)

### Resting total

    210,292 + 22,909 = 233,201 px²
    233,201 / 2,073,600 = 0.11246           ->  11.2%

**11.2% against the baseline's 31.0% — 0.363x, a 2.76x improvement.** It **misses
the sub-8% ambition** and the spec will not pretend otherwise. Two layers own the
overage: `#night` contributes 5.45 points and `#periph` 1.84 points of the 11.25.
Cutting `#night`'s outermost stop from `.34` to `.20` and pulling `#periph`'s mask
in from r=470 to r=620 would land it at roughly 8.0% while leaving every data
channel untouched, because no measurement in this design is taken outside r=352.
That is the honest fix, and it is a two-number change.

**The stronger sub-figure, and the one that carries the thesis: 1.105%.** Of the
whole screen, only 22,909 px² has terrain *replaced* rather than tinted — the
rings, the reticle, the drips, the two floating log lines and the four need
glyphs. Independently measured on the real render: taking the bare-world frame
and the resting frame from the same browser and comparing local luminance
variance in 15px blocks, **0.81% of blocks lose more than 85% of their local
contrast**. Two unrelated methods agreeing at 1.1% vs 0.8% is the claim this
design is actually making.

**What it pays instead, measured on the render — this is the real cost.**
Mean luminance loss over the whole frame at rest is **18.1%**, and it is
distributed radially, deliberately:

| band from (944,528) | share of screen | bare Y | resting Y | change |
|---|---|---|---|---|
| r 0–200 | 6.06% | 70.10 | 85.58 | **+22.1% brighter** |
| r 200–400 | 18.18% | 64.70 | 64.94 | −0.4% |
| r 400–600 | 28.24% | 69.48 | 60.87 | −12.4% |
| r 600–800 | 23.99% | 71.54 | 55.24 | −22.8% |
| r 800–1000 | 20.85% | 74.88 | 48.66 | −35.0% |
| r 1000–1200 | 2.67% | 73.12 | 45.02 | −38.4% |

The design *brightens* the 6% of the screen it wants you reading, holds the next
18% neutral, and pays the whole bill at the rim. Whether that is a feature or a
defect is the argument; it is not an accident, and it is Weakness 2.

### Revealed state

Same rule. The panel's `blur(11px) brightness(.40) saturate(.42)` under a
`.78`–`.87` wash destroys all terrain information, so it counts at **full weight,
full area** — exactly as the baseline's dock does. The full-screen tints are then
integrated over the frame *minus* the panel rect (1400,40,496,1004), since the
panel is opaque over them.

| component | equiv px² |
|---|---|
| `#reveal` 496 × 1004 | **497,984** |
| `#leader` 106×1 + `#leader-dot` 5×5 | 39 |
| tints outside the panel rect (subtotal 296,290 × 0.5) | 148,145 |
| discrete ink (all of it is left of x=1400) | 22,909 |
| **total** | **669,077** |

    669,077 / 2,073,600 = 0.32266           ->  32.3%

**32.3% against 31.0% — 1.041x. The revealed state is worse than the HUD it
replaces.** The panel alone is 497,984 px² = **24.0%**, against the shipping
dock's 380,160 px² = 18.3%: it is 31% larger than the thing it is meant to be an
improvement on. There is no reading of the arithmetic that rescues this on area.
The only defence is **duty cycle**, and it has to be stated as the claim it is:
the 31.0% baseline is paid on every frame of the game, while 32.3% is paid only
while a key is held. At a 5% hold duty the design averages
`0.95 x 11.25 + 0.05 x 32.27 = 12.3%`; at 10%, 13.3%. Both beat 31.0% by better
than 2x. But that defence is only as good as the duty cycle, and Weakness 1
argues the duty cycle will be higher in exactly the situations that matter.

## Motion

An ambient design leans on motion harder than any other in this set, and this is
the honest ledger: **the file contains exactly one animation.** Everything else
below is proposed, and the difference is marked.

### In the file

- **`#periph` peripheral breathe** — `animation: d08breathe 5.2s ease-in-out
  infinite`, `@keyframes` swinging `opacity` between `.86` and `1.00` (the
  element's static `opacity: .93` is the value the mockup renders at, since the
  screenshot is taken at rest). The dark, desaturated rim tightens and loosens on
  a 5.2s cycle. It is deliberately the slowest and least legible thing on screen —
  slow enough to sit below conscious notice, which is the point: it makes the
  frame feel alive without ever becoming a signal. This is the only animation, so
  the mockup reads correctly frozen, as the contract requires.

### Proposed

- **Noise ring expansion.** Trigger: any turn in which the avatar makes noise.
  The ring is drawn at `r = 0` and expands to `r = volume x 32px` over **280ms,
  `ease-out`**, then holds for the rest of the turn and fades over 400ms. Ease-out
  matters: a linear expansion reads as a sweep, a decelerating one reads as an
  impulse propagating, which is what noise is. This is the single most important
  motion in the design, because a static ring is ambiguous between "you just made
  this noise" and "this is your permanent hearing radius".
- **Edge bleed heartbeat.** Trigger: a bleeding effect on any part. The
  `#w-larm` gradient's peak alpha oscillates `.66 → .74 → .66`.
  **This is where the design has to choose a clock, and the choice is not
  cosmetic.** Tied to real time (say 900ms, `ease-in-out`, sine-like) it reads as
  a *pulse* — a body, a heartbeat, something happening to you now; it also keeps
  animating while the player sits in a menu, which is either immersive or
  maddening. Tied to the turn clock (one beat per turn, retriggered by
  `hud_anim`) it reads as a *metronome* — pressure, a countdown, "that is one more
  turn of blood" — and it stops dead whenever the game does, which correctly
  communicates that a turn-based game is not losing HP while you think.
  Recommendation: **turn-clocked**, at 280ms per beat, because this HUD's whole
  claim is that the world tells you the truth, and a real-time heartbeat would be
  telling you time is passing when it is not. Cost: at high speed the beat
  becomes a flicker, so clamp to one beat per 200ms of wall time.
- **Floating log lines.** Trigger: a new message. The line appears at
  `opacity 0`, `top +6px`, rises to its slot and `opacity .97` over **180ms
  `ease-out`**; the previous line demotes to `.44` over the same 180ms; both fade
  to 0 over **1400ms `linear`** starting 4s after arrival. Real-time, not
  turn-clocked — a message is an event in the player's time, and the whole point
  of floating it in the world is that it is transient. `hud_anim`'s existing
  `hud_log_entry` spec is already a slide-and-fade from below, so the entrance
  half of this exists.
- **Reveal on key-hold.** Trigger: key down on a new binding. The panel
  translates in from `left: 1424` to `1400` and `opacity 0 → 1` over **140ms
  `ease-out`**; `#leader` draws left-to-right over the same 140ms; release
  reverses in **90ms `ease-in`**. Asymmetric on purpose — a held overlay should
  appear deliberately and get out of the way fast. 140ms is under the ~200ms
  threshold at which a player reads a UI as sluggish, and long enough that the
  panel does not appear to teleport.
- **Threat glint shimmer.** Trigger: a hostile in view. A 2.4s `ease-in-out`
  opacity swing between `.62` and `.88` on `.glint`, **desynchronised per contact**
  by seeding the delay from the monster id — synchronised blinking reads as UI,
  desynchronised reads as eyes.
- **Reticle acquisition.** Trigger: tracking begins. The 8 `.tick` brackets
  converge from a 96px box to their 42px box over **160ms `ease-out`**. Nothing
  animates on the HP arc: it should **snap**, because a smoothly-lerping enemy
  health arc lies about the timing of your own hit.
- **Not animated, on purpose.** The `#needs` glyphs, the `#batt` charge arc, the
  `#collar`/`#bearing` rose, `#frost`/`#chill`, and every element of the revealed
  panel. These are all slow-moving state; animating them would spend the player's
  attention on things that did not just change. The one HUD motion decision the
  baseline gets unambiguously right is its refusal to pulse the fresh log row, and
  the same restraint applies here.

## Feasibility in RCSS

**Audited against the contract's verified RmlUi 6.2 lists. The file uses zero
forbidden properties** — grepped: no `mix-blend-mode`, no `clip-path`, no CSS
grid, no `::before`/`::after`, no `content:`, no `text-shadow`, no SVG, no
`calc()`, no `@font-face`, no `<script>`. The design's entire visual language is
built from the four features the contract confirms and the baseline never used:
`backdrop-filter`, `mask-image`, `conic-gradient`, and `filter: drop-shadow`.

- **The ambient tints do not rely on blending, and that is a deliberate
  constraint.** `mix-blend-mode` is unavailable, so every world layer composites
  with plain source-over alpha and the *stop colours* were chosen to survive it:
  `#night`'s outer stop is `rgba(25,8,11,.34)` — a warm near-black rather than a
  blue one — because source-over `multiply`-style darkening is unavailable and a
  blue wash over the tile set's browns at 34% would grey them instead of dimming
  them. The two `backdrop-filter` planes do the work `mix-blend-mode` would
  otherwise be asked for: `#grade` (`saturate .86 brightness .94 contrast 1.03`)
  is the global grade and `#periph` (`saturate .40 brightness .85`) is the vignette
  — filters on a masked plane are strictly more capable than a blend mode here,
  because they can desaturate without tinting. **Nothing needs replacing.**
- **`decorator:` instead of `background:`.** Every `linear-gradient`,
  `radial-gradient`, `repeating-linear-gradient` and `conic-gradient` becomes
  `decorator: <same>` with an unchanged argument grammar. `mask-image` in RCSS
  already takes decorator values, so the eight masked layers port verbatim. All
  `-webkit-` duplicates are Chromium scaffolding and are dropped.
- **`border-radius: 50%` must become a length.** RCSS border-radius takes lengths,
  not percentages, so `#noise` needs `border-radius: 256dp`, `#batt` `303dp`,
  `#collar`/`#bearing` `120dp`, `#trk-hp` `32dp`, `.bead`'s
  `50% 50% 60% 60%` → `3dp 3dp 4dp 4dp`. `.shard`'s `0 3px 3px 0` and `.drip`'s
  `0 0 3px 3px` are already lengths.
- **`transform-origin` is not on the contract's verified list.** Used on `.shard`
  (`0% 50%`), `.tail` (`0% 50%`) and `#tether` (`50% 100%`). RmlUi 6.2 does ship
  it, but if it proves unavailable the replacement is arithmetic, not markup: bake
  the rotation's origin offset into the element's `left`/`top` and keep the plain
  `transform: rotate()`. All three are static rotations, so this is a one-time
  precomputation.
- **`box-shadow: inset` carries three whole channels** — `#chill`'s edge
  hairlines, `#noise`'s 46px bloom, `#reveal`'s 1px frame. `box-shadow` is
  verified; inset specifically is not called out. Fallback if inset is missing:
  each inset hairline becomes a 1px child div, and the blooms become
  `radial-gradient` decorator layers, which stack.
- **`<i>` and `font-style: normal`.** The mockup wraps inline data in `<i>` and
  resets the style because Chrome's UA sheet italicises it; RmlUi has no such UA
  sheet, so in RCSS these are plain `<span>`/`<div>` and the reset is deleted.
  Conversely — and the baseline documents this at length — **RmlUi's default
  `display` is `inline`**, so every `.brow`, `.erow`, `.srow`, `.grow`, `.crow`,
  `.lrow`, `.mr`, `.chip`, `.rule`, `.lab` and `.sg` needs an explicit
  `display: block`/`flex`. This design has 380+ block-level elements in the panel;
  omitting that reset would collapse the entire panel into one line box.
- **Fractional lengths and the locale trap.** The panel uses `11.5px`, `9.5px`,
  `5.5px` tracking and `.25px` tracking. `dp` accepts fractions, but any length
  this design's *producer* generates in C++ — the noise ring radius, the bleed
  width, the conic arc angles — must not go through `std::to_string` or
  `string_format`, which honour `LC_NUMERIC` and emit `255,5dp` on a
  comma-decimal locale, which RmlUi silently rejects. `rml_length.h`'s `pct`/`dp`/
  `px` helpers exist for exactly this and must be used.
- **Fixed pixel geometry must become a written rect.** `#reveal`'s
  `left:1400px; top:40px; width:496px; height:1004px` and every full-screen
  `.lyr`'s `1920x1080` are 1080p literals. `sidebar_hud_apply_rect()` already
  writes percentage rects per region and would write this one; the `.lyr` planes
  become `width: 100%; height: 100%`.
- **Glyph coverage needs verifying against the bundled TTF.** The design uses
  `↑ ↓ ← → □ ► ▲ ■ ○ ≈ ▼` as data. Source Code Pro's arrow coverage is good; its
  geometric-shape coverage (U+25A0/25A1/25B2/25BA/25BC/25CB) is **not assumed
  here**. Each needs checking against the shipped font, with an ASCII fallback per
  glyph (`^ v < > .` for the edge legend, `>` for the tracked marker, `#` for the
  player cell, `+` for mission). No new font is requested.
- **Four `backdrop-filter` passes per frame is the real performance question.**
  `#grade` and `#periph` are full-screen and `#reveal` is 24% of it; each forces a
  framebuffer read-back into a render target. Worse, `#periph`'s 5.2s breathe
  animation means the full-screen copy happens **every frame, forever**, even when
  nothing in the game has changed. Mitigations, in order of preference: fold
  `#grade` into the existing GPU lighting/tonemap pass where the frame is already
  in a render target and the grade is free; make `#periph`'s breathe a property
  the lighting pass consumes rather than an RmlUi layer; keep `#reveal`'s blur,
  since it only runs while held.

### New C++ work

This is the largest producer delta of any design in the set, and the honest
summary is that **the resting layer needs a producer class that does not exist**:
one that emits *world-space* geometry, where every current producer emits a
region-space RML string.

1. **A hold-to-reveal input path — and the blocker is real.** CBN's input layer is
   a discrete-action model: `input_context::handle_input()` returns action names,
   and `input_to_action` returns the *first* registered action matching an event.
   There is no key-release event for an action, so "while held" cannot be
   expressed in the existing vocabulary. Three options, in order of honesty:
   (a) sample the raw key state once per frame the way the existing throw-radial
   overlay samples the pointer, and drive the document's class from that;
   (b) ship it as a **toggle** first, which needs no new input machinery at all;
   (c) add a genuine key-up action channel, which is the correct fix and the
   largest. Two adjacent hazards, both already burned into this codebase: a new
   `DEFAULTMODE` binding must be checked for a same-key collision across *all*
   categories, because the earlier-registered action silently wins and the later
   one becomes unreachable with no warning; and if the reveal is driven by a
   timeout loop, the 16ms RmlUi frame tick must never be read as a release — the
   tick always beats a human, so that bug would be 100% reproducible.
   The document itself should open **passive** (`rml_doc::open(..., passive=true)`),
   so it paints every frame but leaves `any_interactive_open()` false and input
   falls through to the game — which is exactly right for an overlay the player
   never clicks.
2. **`hud_ambient` — a new producer for the resting channels.** Four feeds:
   - *Injury side + severity per part.* **This one is nearly free.** `hud_vitals`
     already computes the per-part colour carrying bleeding/bitten/splint state
     into `label_hex` at `panels.cpp:872` and then **never uses it**. The producer
     does not need new game queries; it needs to stop discarding a value it
     already has, and to map `bodypart_id` → screen edge.
   - *Noise radius.* Not emitted anywhere today (the baseline marks noise `8` as
     DROPPED), so this is a new read of the volume the avatar made this turn,
     converted to a pixel radius by the tile size.
   - *Torch charge.* Also new — no held-item charge readout exists in the HUD at
     all (`Flashlight 34%` is DROPPED in the baseline). Needs the wielded/worn
     light source's `ammo_remaining` over its capacity, emitted as a conic arc
     degree count (`0.34 x 360 = 122.4deg`).
   - *Threat glints in world space.* **This is the genuinely hard one and it has a
     known trap.** The glints and the reticle must be positioned by the *same*
     tile→screen transform the tile renderer uses, obtained from `cata_tiles`, not
     re-derived in `panels.cpp`. An overlay that computes its own mapping ends up
     offset by half a tile — the mistake is invisible at one zoom level and
     glaring at another, and it is not fixable by nudging constants. Same
     requirement for `#bearing`'s wedge angle, which must be computed from the
     monster's position through that transform, or the ambient bearing and the
     panel's `4 NE` text will disagree exactly as they do in this mockup (measured:
     the reticle sits at +6.3° from north at 4.53 tiles, while the text says NE).
3. **Floating world-space log lines.** `hud_log` emits a fixed bottom-anchored
   dock list with a turn-based age ramp. The floating pair needs the newest 1–2
   messages placed near the avatar in screen space (same transform dependency),
   plus a **real-time** fade timer independent of the turn clock, which no HUD
   producer currently owns.
4. **`hud_hotbar(avatar &)` ignores its argument.** The baseline records this:
   there is no disabled slot state, which is why the fixture's "slot 1
   unavailable" is a DROPPED field today. This design's `.chip.dead` and its
   `fire unavailable — no firearm wielded` note both need the producer to actually
   read the avatar — a wielded-gun check per slot, and a reason string.
5. **Parity reads the shipping HUD simply does not do:** per-part encumbrance
   (`enc 12` / `enc 4`), morale, wind, light level, the sidearm, the wielded
   weapon's bash/cut/to-hit, the target's range and behaviour, warmth, and
   effect durations. All are marked DROPPED in `00-current-baseline.md`, and
   several (`morale_stat`, the light-level helper) already exist in `panels.cpp`
   as functions no producer calls. Adding them back is restoring parity, not
   adding features.
6. **`sidebar_hud_apply_rect()` inverts.** Today it carves 44 of 240 cells for the
   dock and writes six percentage rects. This design needs it to write **one**
   rect, and in the resting state **none** — the terrain viewport becomes the full
   240x67 grid. That is a change to the widget layout width (44 → 0) and therefore
   to `update_offsets`. It also makes the baseline's `TERMY x 16 = 1072 != 1080`
   defect irrelevant rather than fixed: with nothing anchored to the bottom band,
   the 7.52px unpainted sliver and the 6.34px off-screen hotbar cannot occur.

## Weaknesses

1. **A player who cannot see `8/30` without holding a key will make worse combat
   decisions, and the design as rendered does not fully answer this.** This is the
   hardest question and it deserves a straight answer rather than a defence. The
   resting layer is *ordinal*: `#w-larm` is a 206px gradient falling `.66 → .07`,
   and there is no reading of it that distinguishes 8/30 from 11/30. But 8/30 is
   one unlucky claw from a broken limb, and the difference between "hurt" and
   "one hit from disabled" is the difference between swinging again and
   disengaging. Three real aggravators: the reveal costs 24.0% of the screen at
   exactly the moment the player wants to see the field; holding a key occupies a
   hand that is otherwise issuing movement; and 5 of the 6 exact HP values are
   *only* in the panel, so a player checking one arm is forced to load the whole
   dock. Mitigations that keep the thesis mostly intact, in order of cost:
   (a) **promote the critical part's number into the resting layer** — one
   4-glyph `8/30` pinned to the injured edge, ~120px² of ink, 0.006% of the
   screen, which removes the worst case entirely and breaks the design's purity
   for exactly one field; (b) make the reveal a **toggle as well as a hold**, so
   the player can pin it while planning and drop it while acting; (c) **auto-reveal
   for N turns on any HP decrease** — the `hud_shake::trigger` hook already fires
   on exactly that event, so the plumbing exists. Bottom line: **for the single
   most important number on the screen, this design is a worse instrument than the
   baseline until (a) is implemented**, and (a) should be considered mandatory
   rather than optional.
2. **The ambient tints land on a scene that was already the darkest the game
   produces, and the measured cost is large.** At 21:47 under 12% moonlight the
   terrain this design tints has no headroom. Measured on the real render against
   a bare-world frame from the same browser: mean luminance loss over the whole
   frame is **18.1%**, distributed as −12.4% at r 400–600, −22.8% at 600–800,
   −35.0% at 800–1000 and −38.4% beyond. **47.5% of the screen loses at least a
   fifth of its light**, and `#periph` also drops saturation to `.40` out there —
   saturation being precisely the channel the tile set uses to separate grass from
   dirt from pavement. So the "near-zero HUD" claim is true only in the currency
   of *painted coverage* (1.105% by area, 0.81% by measured contrast destruction);
   in the currency of *legibility* the design removes a fifth of the frame's light
   and removes it from the half of the screen that was hardest to read to begin
   with. It would be nearly free at noon. It is expensive at 21:47, which is when
   people play. The two-number fix in the Occlusion section (`#night`'s outer stop
   `.34 → .20`, `#periph`'s mask start `470 → 620`) recovers most of this and costs
   no data channel, and it should be applied before this design is shown to anyone.
3. **The injury channel is red-only and edge-only, so it fails twice: once for
   colour vision, and once structurally for six parts on four edges.** For colour:
   the bleed's only carrier is hue. Measured at y=540, `R−B` runs `+68` at x=8 to
   `+16` at x=240 (ambient terrain), while luminance runs `Y=39.7` to `Y=56.3` —
   so a protan or deutan player sees a dark neutral gradient that is 30% *darker*
   than its surroundings and reads as shadow or a wall, not as a wound, especially
   against the tile set's browns and `#night`'s warm-dark wash. A luminance or
   texture co-encoding is required: the `#w-larm-tear` crack pattern is already
   achromatic and should be strengthened and driven by *severity* rather than by
   the bitten effect alone. For structure, the problem is worse and is not fixable
   by recolouring: **there are four screen edges and six body parts.** The file
   resolves the collision by simply not encoding three of them — head takes the
   top (1920×60), l arm the left (206×1080), l leg the bottom-left (912×54), and
   **torso, r arm and r leg have no resting encoding at all**. The `.bedge` legend
   is honest about it (`→ ghost`, `↓ ghost`), but the consequence is that "nothing
   is glowing on the right" is ambiguous between *the right arm is fine* and *the
   right arm has no channel* — and a player cannot resolve that ambiguity without
   holding the key, which is the thing the design exists to avoid. Worse, the
   `.bedge □ lit` on the torso row claims a firing channel that is painted nowhere,
   which is a straightforward inconsistency in the legend. Two ways out, both real
   work: segment each edge (top-left head / top-right torso, which is not
   anatomical and must be *learned*, undermining the "no reading required" claim),
   or abandon screen edges for an **avatar-anchored rosette** — six short arcs on
   the existing `#collar` ring at r=119, which is anatomically unambiguous, already
   has a construction in the file, and costs about 900px² of ink. The rosette is
   the better design and this mockup does not implement it.
4. **The revealed panel is a dock by another name, it is larger than the dock it
   replaces, and it is already full to five pixels.** Measured: 496×1004 =
   497,984 px² = **24.0%** of the screen against the shipping dock's 18.3%, and
   the revealed-state total of 32.3% is *above* the 31.0% baseline. The design's
   only defence is duty cycle, and Weakness 1 argues the duty cycle rises in
   precisely the situations where occlusion hurts most — so the defence is weakest
   exactly where it is needed. It is also at capacity: measured `scrollHeight`
   1019 against `clientHeight` 1004, with `.deadnote` ending at y=1039 and
   `overflow: hidden` clipping at y=1044. **Twelve effects instead of five** adds
   seven `.erow`s at 17px = 119px, which pushes the last four log rows, the entire
   hotbar and the reason note off the bottom — silently, with no scrollbar and no
   `(+N)` indicator, because the panel has neither. And **at 1280x720 the panel
   does not fit at all**: its 1004px height exceeds the 720px viewport, so it must
   become percentage-based, at which point its 496px width is **38.8%** of a
   1280px screen and the `.split` block's 244+204px columns can no longer hold a
   121px overmap beside three contact rows. The resting layer, by contrast, scales
   cleanly — every ring is anchored to the avatar and sized in tiles, so it is
   resolution-independent by construction. The panel is the only part of this
   design that has the baseline's problems, and it has them because it *is* the
   baseline, hidden behind a key.
