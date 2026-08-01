# 06 — Radial Orbit

## Thesis

Every HUD in this set except this one puts information at the edges of the screen,
and every one of them therefore charges the player a saccade to read it. This
design argues that **the screen edges are the wrong place for anything you need
while you are looking at your own character**, and that the correct anchor for a
survival HUD is not the viewport but the avatar: health becomes arcs drawn around
the player's own tile, threats become bearings on a compass ring centred on that
tile, the action list becomes a wheel that appears around the player's feet when
you hold a key. Read as one claim: *the player's eye is already at (944, 528) —
put the state there and the saccade cost goes to zero.* Positional encoding does
work that no label can: an arc at 274° **is** the left arm, a diamond at 45° **is**
the thing behind you to the north-east, and neither needs to be read, only
noticed. What it trades away is the one thing the edges were good for. Edges are
dead space; the centre is not. This design spends 67 tile-equivalents of paint on
the 178 map tiles closest to the avatar — the tiles the player most needs to see
— and it keeps only a 9-tile clear core in the middle of them. It costs less
than half the screen area of the shipping HUD and buys that back in the worst
possible currency: the pixels immediately around the character. The whole design
is a bet that a 68px hole is enough, and the Occlusion budget below states the
price of that bet in tiles instead of hiding it in a percentage.

## Register

The register is **instrument reticle**: nothing is a panel, everything is either
an arc, a mark on an arc, or a caption that a leader line ties back to an arc.
There are exactly two rectangles in the whole design, and they are the two
things that must be read cold rather than noticed.

### Palette

Declared as custom properties on `#screen`. All 24 tokens, plus the literals that
appear inline.

| token | hex | role |
|---|---|---|
| `--d06-void` | `#04080a` | scrim base, glyph outline ink, all wash gradients |
| `--d06-casing` | `#010405` | the casing shells — an opaque black keyline on both radial edges of every ring |
| `--d06-hair` | `#63797f` | **declared and never referenced — dead token, delete on port** |
| `--d06-chrome` | `#8ea3a9` | cardinal compass ticks, all hairlines and borders (used as `rgba(142,163,169,α)`), light-level bar fill |
| `--d06-txt` | `#e4ecee` | primary values: limb numerics, keycap glyphs, player name, stat values |
| `--d06-txt2` | `#a6b8be` | secondary body: place name, weather line, target range line |
| `--d06-txt3` | `#6d8087` | labels and separators: `SYSTEMIC`, `MOBILITY`, `HANDS`, log timestamps, all `·` |
| `--d06-ok` | `#35d6a0` | body-part arc fill, healthy |
| `--d06-okf` | `#4ae8b0` | body-part arc fill at exactly 100% (R arm, R leg) — a brighter step, so "untouched" is distinguishable from "nearly full" without reading the number |
| `--d06-warn` | `#f0c33c` | pain, morale, flashlight charge |
| `--d06-crit` | `#ff5a4e` | L-arm arc fill, `CRITICAL` badge, the two breach marks, the plate's big `8` |
| `--d06-stam` | `#4fc3f7` | stamina arc |
| `--d06-need` | `#e9973f` | hunger / thirst / fatigue arcs |
| `--d06-cold` | `#78b6e8` | warmth arc, `CHILLY` label, `8 °C` |
| `--d06-hostile` | `#ff3b30` | untracked hostile pips |
| `--d06-track` | `#ffb300` | tracked-contact pip, target plate border and name, the target's leader line |
| `--d06-mission` | `#b98cff` | mission marker on the overmap band |
| `--d06-good` | `#9bd46a` | log row: neutral/good |
| `--d06-bad` | `#ff6b5e` | log row: bad |
| `--d06-baddim` | `#c9564d` | log row: bad, dimmer tier |
| `--d06-caution` | `#f2c14e` | log row: caution, and the fresh-row left rule |
| `--d06-empty` | `#182226` | arc trough (unfilled remainder of any arc), bar troughs |
| `--d06-emptycrit` | `#41120f` | arc trough on a critical limb — a *warm* trough, so the L-arm track reads as wounded across its whole 60° even where it is empty |

Inline literals, none of them tokenised:

| hex | role |
|---|---|
| `#5b6166` | overmap band: built-up / residential sector |
| `#2f4a34` | overmap band: forest sector |
| `#3c4033` | overmap band: field sector |
| `#d9f2ff` | stamina arc end cap |
| `#fff6dd` | white core inside the tracked pip |
| `#bfe6fa` | stamina value text |
| `#ff8c82` | `L ARM` plate heading and the `8/30` ring tag |
| `#ffc0b8` / `rgba(60,14,11,.55)` | effect chip text / chip fill |
| `#d3b8ff` | `MISSION` label |
| `#8fdcc0` | `SAFE MODE ON` badge |
| `#3a464a` / `#33403f` | disabled-slot keycap / action label |

### Type

One face: `Consolas, "Cascadia Mono", "Lucida Console", monospace`, set once on
`#screen`, standing in for the bundled `Source Code Pro` per the contract.

Twelve discrete sizes are in the file — measured, not estimated:
`7, 8, 9, 10, 11, 12, 13, 14, 15, 17, 20, 26` px. They resolve into four
registers, and the fact that it takes twelve values to express four registers is
a real cost this design pays for putting type at four different radii:

| register | sizes | used for |
|---|---|---|
| micro | 7 / 8 / 9 | slot ordinals, slot action labels, need labels, compass letters, section labels, effect chips, badges, the two key hints |
| value | 10 / 11 / 12 | limb numerics (`.d06-tag`, 10px), stamina value, log timestamps, `L ARM` header, target range line, all identity and top-right stat lines |
| body | 13 / 14 | place name, wielded weapon, the six log rows, target name (`ZOMBIE BRUTE`, 14px) |
| display | 15 / 17 / 20 / 26 | the `/30` denominator, the keycap glyph, the player name, and the critical limb's `8` — the largest glyph on screen, which is the correct thing for it to be |

Line-heights are pinned in px on every absolutely-positioned label
(`7, 8, 11, 12, 13, 14, 16, 17, 22, 26`) rather than left to a ratio, because a
13px label box at the default 1.2 ratio would clip inside an 11px ring tag.
Letter-spacing runs `0.2 → 2.2px`, widest on the smallest type — `SURVIVOR` at
9px carries 2.2px, `NIGEL ASHBY` at 20px carries 1.4px.

### Casing and halo — the whole legibility strategy

The backdrop is a noisy 32px tile map, so **nothing in this design ever touches
terrain directly**. Three devices, applied without exception:

1. **Paired ring shells.** Every ring is two masked elements, not one: an opaque
   `--d06-casing` `#010405` shell, and an ink shell inset radially inside it. The
   casing is therefore a black keyline on *both* radial edges of every arc.

   | ring | casing r | ink r | keyline |
   |---|---|---|---|
   | band A (core limbs) | 70–88 | 72–86 | 2px each side |
   | band B (extremities) | 88–106 | 90–104 | 2px each side |
   | needs quartet | 93–103 | 95–101 | 2px each side |
   | stamina | 124–137 | 126–135 | 2px each side |
   | threat bearing | 145–151 | 146.5–149.5 | 1.5px each side |
   | overmap band | 164–180 | 166–178 | 2px each side |
   | hotbar wheel guide | 218.5–225.5 | 220–224 | 1.5px each side |

2. **Keylined marks.** Every free-standing mark — the eight compass ticks, the
   three threat pips, the two breach marks, the stamina end cap, the four
   leaders, every bar — carries `box-shadow: 0 0 0 1px rgba(1,4,5,.9–.95)`, the
   same keyline expressed as a zero-blur spread shadow. Fourteen uses.
3. **Outlined glyphs.** `.d06-o` is an 8-way 1px `#04080a` outline plus a 4px
   `rgba(4,8,10,.95)` blur, applied to essentially every text node. Body text on
   its own casing is `16.79:1`.

Above that sits one **annular scrim** — a 470×470 radial gradient centred on the
avatar, transparent at the core, peaking at only `0.40` alpha at mid-radius and
returning to transparent at its rim. It has no visible edge in either direction:
it does not announce itself at r=235 and it does not dim the tile the player is
standing on.

### Frames

Two, and only two: `.d06-plate` — `rgba(10,19,23,.92)`, a 1px border, and
`backdrop-filter: blur(3px)` — on the **L ARM** card and the **target** card.
Every other peripheral block is a `.d06-wash`: a gradient with **no border at
all**, anchored opaque against its own edge of the screen and fading to alpha 0
toward the play area. So the design has no rectangles except where a rectangle
means *read this now*. Corner radii are `2px` (chips, badges, compass letters)
and `4px` (wheel slots).

## Layout

**Ring centre is (944, 528)** — the exact centre of the player's tile,
`(928,512)-(960,544)`. Every one of the 14 masked shells resolves to that point:
each is a box of side `2·r_out` positioned at `(944 − r_out, 528 − r_out)`, and
the measured centre error across all 14 is `(±0.00, ±0.00)`.

Each shell's inner radius is a mask percentage of its own half-width, and all 14
land on integers:

| class | box | r_out | mask stop | r_in | content |
|---|---|---|---|---|---|
| `.d06-Ac` / `.d06-Ai` | 176 / 172 | 88 / 86 | 79.545% / 83.721% | 70 / 72 | band A: torso, L leg, R leg |
| `.d06-Bc` / `.d06-Bi` | 212 / 208 | 106 / 104 | 83.019% / 86.538% | 88 / 90 | band B: head, L arm, R arm |
| `.d06-Nc` / `.d06-Ni` | 206 / 202 | 103 / 101 | 90.291% / 94.059% | 93 / 95 | needs quartet |
| `.d06-Sc` / `.d06-Si` | 274 / 270 | 137 / 135 | 90.511% / 93.333% | 124 / 126 | stamina |
| `.d06-Tc` / `.d06-Ti` | 302 / 299 | 151 / 149.5 | 96.026% / 97.993% | 145 / 146.5 | threat bearing ring |
| `.d06-Mc` / `.d06-Mi` | 360 / 356 | 180 / 178 | 91.111% / 93.258% | 164 / 166 | overmap directional band |
| `.d06-Wc` / `.d06-Wi` | 451 / 448 | 225.5 / 224 | 96.896% / 98.214% | 218.5 / 220 | hotbar wheel guide arc |

**The inner clear radius is 68px** (`#d06-clear`, a 138×138 box at
`(875, 459)` giving a guide ring at r 68–69). Nothing is drawn inside it. That
number is not arbitrary: the 3×3 block of 32px tiles centred on the player has a
maximum corner radius of `√(48² + 48²) = 67.882`, so the player's tile **and all
eight adjacent tiles** fit inside the clear core — by 0.118px. Exactly nine tiles
are wholly clear, and the design is tuned to that and nothing more.

### Body-part angular assignment

Bearings are measured clockwise from screen-up, so 0° = north. Anatomy is laid
out **as the player experiences their own body, not as a diagram seen from the
front** — head up, legs down, right arm to the screen's right, **left arm to the
screen's left** (span centred on 274°, i.e. due west). That is the mirror of the
convention an anatomical chart would use, and it is the right choice here because
the ring is drawn around the player's own avatar: your left arm is on your left.

| part | value | band | span | span° | fill° | fill fraction |
|---|---|---|---|---|---|---|
| Head | 20/24 | B (90–104) | 344° → 16° | 32 | 26.67, centred (2.67 → 29.33) | 83.33% |
| Torso | 45/60 | A (72–86) | 300° → 60° | 120 | 90, centred (15 → 105) | 75.0% |
| L arm | 8/30 | B (90–104) | 244° → 304° | 60 | 16, centred (22 → 38) | 26.67% |
| R arm | 30/30 | B (90–104) | 55° → 115° | 60 | 60, full | 100% |
| L leg | 22/28 | A (72–86) | 190° → 235° | 45 | 35.36, centred (4.82 → 40.18) | 78.57% |
| R leg | 28/28 | A (72–86) | 125° → 170° | 45 | 45, full | 100% |
| Stamina | 5100/8700 | 126–135 | 0° → 360° | 360 | 211.03 from north | 58.62% |

Fills are **centre-anchored**, not start-anchored: an arc shortens symmetrically
from both ends as the part is damaged, so the empty trough appears on both sides
and a half-full arc cannot be mistaken for a full one that starts late. The head
and torso share the same bearing (0°) at different radii — the core limb is
inboard, the extremity outboard — which is what makes six independent values fit
without any of them being off-axis.

The four **needs** arcs sit at r 95–101, deliberately inside band B's angular
gaps (band B occupies 344–16, 55–115, 244–304; the needs occupy 20–52, 120–152,
208–240, 308–340). Verified zero angular overlap. Each is a 32° track:
hunger `Peckish` → 9.6° (30%), thirst `Thirsty` → 17.6° (55%), fatigue `Tired`
→ 17.6° (55%), warmth `Chilly` → 12.8° (40%) in the cold hue.

### Annotation radii

| element | radius | bearings |
|---|---|---|
| numeric tag annulus (`.d06-tag`, 34×13) | **117.0** (measured 116.96–117.05) | 0° head, 20° torso, 85° R arm, 165° R leg, 195° L leg, 275° L arm |
| need labels (`.d06-tagn`, 46×11) | 117.75–118.41 | 35.85°, 135.86°, 224.14°, 323.76° — each at its own arc's angular centre |
| `STAMINA 5100/8700` block (78 wide) | 142.72 | 253.72° |
| threat pips | 148 (marks span r 138–158) | 91° and 180.5° hostile, 45° tracked |
| compass ticks | long r 140–158 at 0/90/180/270; short r 144–154 at 45/135/225/315 |
| compass letters (`.d06-card8`, 14×14) | **172.0** | 0° N, 90° E, 180° S, 270° W |
| overmap POI marks | 189.99 / 188.88 / 191.0 | 51.41° mission, 303.77°, 179.7° |
| hotbar slots (46×46) | **222.0** (boxes span r 199–245) | 0° to 240° in exact 30° steps |
| L-arm breach marks | 88–108 | 244° and 304° — the two ends of the wounded track |

### Leaders

Four, each a radial stub rotated about the centre plus a 2px horizontal jog that
lands exactly on the edge of its card.

| bearing | radial extent | outer end | jog | lands on |
|---|---|---|---|---|
| 45° | 156 → 258 | (1126.4, 345.6) | `(1126, 345) 70×2` | target plate, left edge x=1196 — amber, same hue as the pip it starts from |
| 275° | 106 → 150 | (794.6, 514.9) | `(700, 540) 95×2` | L ARM plate, right edge x=700 — red |
| 103° | 106 → 150 | (1090.2, 561.7) | `(1090, 561) 106×2` | HANDS wash, left edge x=1196 — leaves the *right arm* arc, because the hands are on the arms |
| 310° | 139 → 190 | (798.5, 405.9) | `(682, 405) 117×2` | MOBILITY wash, right edge x=682 — leaves just outside the stamina ring |

The `SYSTEMIC` block has deliberately **no leader**: those three effects are not
localised to any limb, and giving them a leader would imply they were.

### Peripheral blocks

Every card clears the ring assembly entirely — verified: zero pixels of overlap
between any wash/plate and the scrim disc. Closest approach is the L ARM plate's
right edge, 9px from the scrim's bounding box and 244px from the nearest painted
scrim pixel.

| block | x | y | w | h | kind |
|---|---|---|---|---|---|
| identity / world | 28 | 26 | 480 | 158 | wash (radial, anchored top-left) |
| top-right "between-turn" stats | 1532 | 26 | 360 | 186 | wash (radial, anchored top-right) |
| `SYSTEMIC` effects | 452 | 288 | 280 | 40 | wash (linear → right) |
| `MOBILITY WALK · NOISE 8` | 452 | 386 | 230 | 40 | wash (linear → right) |
| **`L ARM` critical card** | 452 | 474 | 248 | 96 | **plate** (0.92 alpha, 1px `rgba(255,90,78,.62)` border, blur 3px) |
| `CONTACTS` | 1196 | 268 | 320 | 38 | wash (linear → right) |
| **target card** | 1196 | 316 | 260 | 66 | **plate** (0.92 alpha, 1px `rgba(255,179,0,.55)` border) |
| `HANDS` | 1196 | 512 | 320 | 98 | wash (linear → right) |
| message log | 28 | 876 | 520 | 166 | wash (linear → right, plus a linear → top) |
| 9 hotbar slots | on the r=222 circle | | 46 | 46 | 0.86 alpha, 1px border, blur 2px |

Inside the L ARM plate, at plate-relative offsets: heading `L ARM` at (11, 9);
`CRITICAL` badge right-aligned at (·, 8); the `8` at 26px at (10, 26) with `/30`
at 15px; `27% · ENC 4` at (96, 31); a 140×5 bar at (96, 46) with a 37px fill
(`37/140 = 26.4%`); a rule at (10, 64); `BLEEDING` and `BITTEN` chips at (10, 72).
The log's six rows start at y=900 on a 22px line-height, so the newest row
occupies y 1010–1032 and the amber fresh-row rule at `(29, 1010) 2×20` marks it.

## Data mapping

| fixture field | where it appears | encoding |
|---|---|---|
| Name `Nigel Ashby` | identity block (36, 34) | text, 20px `--d06-txt` |
| Profession `Survivor` | identity block (36, 60) | text, 9px `--d06-txt3` |
| Day `14` | identity block (36, 108) | numeric, `--d06-txt` inside a `--d06-txt2` run |
| Time `21:47` | same line | numeric |
| Season `Autumn` | same line | text |
| Place `Cheswick — residential` | identity block (36, 78) | text, 13px |
| Weather `Light drizzle` | identity block (36, 127) | text |
| Temperature `8 °C` | same line | numeric + colour (`--d06-cold`, cold band) |
| Wind `NE 12 km/h` | same line | text |
| Light level `12%` (moonlight) | identity block (36, 146) + a 60×4 bar at (212, 151) | numeric + bar (7px fill = 11.7%) |
| Flashlight lit `34%` | `HANDS` block (1200, 566) + a 52×4 bar at (1296, 569) | numeric + bar (18px fill = 34.6%) + colour `--d06-warn` |
| STR/DEX/INT/PER `10/12/9/11` | top-right (1584, 64) | numeric, right-aligned |
| Speed `96`, Focus `84` | top-right (1584, 92) | numeric |
| Pain `12` | top-right (1584, 113) + `PAIN 12` chip in `SYSTEMIC` | numeric + colour `--d06-warn` + chip |
| Morale `-3` | top-right (1584, 113) | numeric + colour `--d06-warn` |
| Encumbrance torso `12`, arms `4` | top-right (1584, 134); arms `4` **also** on the L ARM plate as `ENC 4` | numeric, duplicated where it is load-bearing |
| Move mode `walking` | `MOBILITY` block as `WALK` | text, leadered to the stamina ring |
| Noise made `8` | `MOBILITY` block as `NOISE 8` | numeric |
| Head 20/24 | band B arc 344°→16° + tag at r117 bearing 0° | arc length (83.3% of 32°) + `--d06-ok` + numeric |
| Torso 45/60 | band A arc 300°→60° + tag at bearing 20° | arc length (75% of 120°) + `--d06-ok` + numeric |
| L arm 8/30 | band B arc 244°→304° + tag at bearing 275° | arc length (26.7% of 60°) + `--d06-crit` fill on a **warm** `--d06-emptycrit` trough + numeric in `#ff8c82` bold |
| R arm 30/30 | band B arc 55°→115° | arc full + `--d06-okf` (the brighter 100% step) + numeric |
| L leg 22/28 | band A arc 190°→235° | arc length (78.6% of 45°) + `--d06-ok` + numeric |
| R leg 28/28 | band A arc 125°→170° | arc full + `--d06-okf` + numeric |
| L arm **critical** | four independent channels: the `CRITICAL` badge on the plate; the 26px `8`; two `--d06-crit` breach marks at 244° and 304° pinning the wounded track's ends; and `#d06-critglow`, a 9px-blurred duplicate of the arc bleeding r 81–113 under band B | badge + display numeric + position + ambient bloom |
| L arm **bleeding** | `BLEEDING` chip on the plate, 26px from the limb's own numeric | chip |
| L arm **bitten** | `BITTEN` chip on the plate | chip |
| Stamina `5100/8700` | full-circle arc r126–135, 211.03° from north, `#d9f2ff` end cap, label at r142.7 | arc length + numeric + colour `--d06-stam` |
| Hunger `Peckish` | needs arc 20°→52° (30% fill) + `PECKISH` label at 35.85° | arc length + position + colour `--d06-need` + text |
| Thirst `Thirsty` | needs arc 120°→152° (55%) + `THIRSTY` at 135.86° | as above |
| Fatigue `Tired` | needs arc 208°→240° (55%) + `TIRED` at 224.14° | as above |
| Warmth `Chilly` | needs arc 308°→340° (40%) + `CHILLY` at 323.76° | arc length + **cold** hue `--d06-cold`, the one need whose direction of badness is downward |
| Effect `Bleeding (L arm)` | L ARM plate chip | chip, localised to the limb |
| Effect `Bitten (L arm)` | L ARM plate chip | chip, localised to the limb |
| Effect `Soaked` | `SYSTEMIC` chip | chip |
| Effect `Tired` | `SYSTEMIC` chip (and the fatigue arc) | chip + arc |
| Effect `Pain 12` | `SYSTEMIC` chip (and the top-right numeric) | chip + numeric |
| Effects **priority order** | — | **DROPPED — the five effects are regrouped by *locality* (limb-local on the limb's own card, systemic in one row) rather than kept in one priority-ordered run. Locality is the more actionable sort for a body-part game; the fixture's 1–5 ranking is not recoverable.** |
| Wielded `fire axe` | `HANDS` (1200, 531) | text, 13px bold |
| `32 bash / 0 cut / +1 to-hit` | `HANDS` (1200, 547) as `32 BASH · 0 CUT · HIT +1` | numeric |
| Sidearm `Glock 19 12/17 9mm JHP` | `HANDS` (1200, 584) as `GLOCK 19 STOWED 12/17 9MM JHP` | text + numeric, dimmed to `--d06-txt3` because it is stowed |
| Target `zombie brute` | target plate (1206, 324) | text, 14px `--d06-track` |
| Target HP `62%` | 160×5 bar with a 99px fill (61.9%) + `62%` numeric at (1374, 342) | bar + numeric |
| Target `4 tiles NE` | target plate `4 TILES NE`, **and** the tracked pip at bearing 45° on the r=148 ring, **and** the amber leader from that pip to the plate | numeric + position + leader. The ring carries bearing only; range is left to the card. |
| Target status `moving` | target plate `MOVING` | text |
| `3 hostiles visible` | `CONTACTS` numeric `3`, plus three pips at 91°, 180.5°, 45° | numeric + position |
| `1 tracking` | `CONTACTS` numeric `1`, plus the pip at 45° being the larger `.d06-pipT` with a `#fff6dd` core and a `TRACKED` label on the plate | numeric + glyph size + colour + core |
| `SAFE MODE ON` | `CONTACTS` badge, outlined green | badge |
| Log, 6 rows | bottom-left wash, oldest first, newest last with an amber left rule | glyph-free text + 10px timestamp + per-row hue (`good`/`bad`/`baddim`/`caution`) + per-row opacity ramp `0.45 → 0.58 → 0.72 → 0.82 → 0.91 → 1.00` |
| Minimap `11×11` overmap | **FOLDED + ON-DEMAND (`M`)** — the grid is not drawn. It is reduced to the 16-sector directional band at r 166–178: each 22.5° sector takes the dominant terrain of the overmap cells inside it (`#5b6166` built-up, `#2f4a34` forest, `#3c4033` field), with the four cardinals taking the immediately adjacent cell — all four of which are `H`, the residential block the player is standing in. That reproduces the fixture grid exactly in 16 of 16 sectors. The full grid is deferred to `M`, advertised on screen as `M — FULL OVERMAP`. | position + colour, lossy by construction |
| Mission marker NE | `--d06-mission` diamond at r=190, bearing **51.41°**, labelled `MISSION` — the fixture cell is `(+5, −4)` from the player, true bearing 51.34° at 6.40 cells | position + colour + label |
| Other overmap POIs (2) | hollow purple diamonds at r≈189/191, bearings 303.77° and 179.7° — the fixture's `^` at `(−3, −2)` and `(0, +3)` | position, dimmer for "not your mission" |
| Hotbar, 9 slots `f r ' e p c i t 5` | all nine on the r=222 wheel at exact 30° steps, `f` Fire at 0° (N) through `5` Wait at 240°, each slot a 46×46 box carrying its ordinal `1`–`9`, its keycap glyph at 17px and its action label at 8px | keycap glyph + position + ordinal. All nine actions are present, including `e` Examine and `t` Throw, which the shipping producer's fixed 9-entry array does not contain. |
| Hotbar slot 1 **unavailable** | slot 1 at bearing 0° carries `.d06-off`: dimmed fill, 0.18-alpha border, `#3a464a` keycap, a −45° strike through the box, and the literal reason `NO GUN` below it | four channels — the shipping HUD has none |
| Hotbar reveal | `HOLD TAB — ACTION WHEEL` in the top-right hint block; the wheel is drawn revealed | on-demand |
| Colour semantics red/green/amber | `--d06-crit`/`--d06-ok`/`--d06-warn` throughout, plus `--d06-hostile` vs `--d06-track` for contact state | colour |

Nothing else is dropped. The two entries above (`Effects priority order`, and the
literal 11×11 grid) are the only fixture content this design does not carry, and
sixteen fields it *does* carry are ones the shipping HUD drops entirely.

## Occlusion budget

**14.63% of the 1920×1080 play area**, against the ratified baseline of
**31.0%** — under half. The method is the baseline's: union (not sum) of each
element's intersection with the frame, anything at or near opaque at full weight,
translucent scrims at half weight, divided by `1920 × 1080 = 2,073,600`. Measured
by rasterising the declared geometry at 1px and taking, per pixel, the maximum
weight of any layer covering it — so overlaps are counted once.

**Full weight** (alpha ≥ 0.86):

| element | area px² |
|---|---|
| band A casing, r 70–88 over 210° | 5,234 |
| band B casing, r 88–106 over 152° | 4,641 |
| needs casing, r 93–103 over 128° | 2,180 |
| stamina casing, r 124–137 full circle | 10,652 |
| threat casing, r 145–151 full circle | 5,560 |
| overmap casing, r 164–180 full circle | 17,276 |
| wheel guide arc, r 218.5–225.5 over 240° | 6,523 |
| 8 compass ticks, 3 threat pips, 2 breach marks, stamina end cap | 1,126 |
| 4 leaders (radial stubs + horizontal jogs) | 1,226 |
| 8 live hotbar slots, 46×46 | 16,928 |
| 4 compass letter chips + 3 overmap POI marks | 1,513 |
| L ARM plate 248×96 + target plate 260×66 | 40,968 |
| sum | 113,827 |
| less double-counted overlap (slots ∩ wheel arc, pips ∩ threat casing, letters ∩ overmap casing, leaders ∩ rings) | −4,193 |
| **full-weight union** | **109,634** |

`109,634 / 2,073,600 = 5.29%`. Sanity-checked against closed form: the stamina
casing is `π(137² − 124²) = 10,659.7` vs 10,652 rasterised; the overmap casing
`π(180² − 164²) = 17,293.1` vs 17,276.

**Half weight** (translucent, ×0.5):

| element | area px² |
|---|---|
| annular scrim, r 67.9–235 | 159,024 total, of which 64,948 lies under full-weight ink → **94,076** counted |
| 7 gradient washes (`480×158 + 360×186 + 280×40 + 230×40 + 320×38 + 320×98 + 520×166`) | 293,040 — verified zero overlap with the ring assembly and with each other |
| disabled slot 1's corners beyond r=235, plus the critical-limb bloom's blur bleed | 476 |
| **half-only subtotal** | **387,592** → ×0.5 = **193,796** |

    109,634 + 193,796 = 303,430
    303,430 / 2,073,600 = 0.14633  ->  14.63%

Two cross-checks, so the comparison cannot be gamed by choosing a rule:

- **Raw footprint**, every painted pixel at full weight including the whole scrim
  disc and every wash rect: `497,226 / 2,073,600 = 23.98%` — still below the
  baseline's 31.0%.
- **Alpha-weighted**, integrating each layer's real alpha (the scrim's mean alpha
  over its own annulus is only **0.257**; the washes ramp `0.88 → 0.61 → 0`):
  `297,972 / 2,073,600 = 14.37%`, against the baseline's alpha-weighted 23.16%.

The design wins on all three rules. **And the percentage is the wrong metric for
this design, which is the point of the next paragraph.**

### The thesis's own admitted risk, in tiles

The world is a 32px tile grid aligned to the player's tile
(`928 = 29×32`, `512 = 16×32`), so tile `(col, row) = (x//32, y//32)` and the
player is tile `(29, 16)`. A 1920×1080 view holds `60 × 33.75 = 2,025` tiles.

- **Clear core: exactly 9 tiles.** All nine — the player's own and all eight
  adjacent — are wholly inside r=68, with 0.118px to spare. Nothing is drawn on
  them. That is the entire guarantee this design makes.
- **Opaque ring and wheel ink covers 68,759 px² = 67.1 tile-equivalents and
  touches 178 distinct tiles** (8.8% of the visible grid), excluding the two
  peripheral plates. Mean coverage of a touched tile is **37.7%**; **112 tiles
  are more than a quarter covered, 64 more than half covered**, and the worst are
  100% covered.
- **Obscuration is worst exactly where proximity matters most.** Of the 25 tiles
  in the 5×5 block around the player, **14 carry opaque ring ink** — i.e. 14 of
  the 16 tiles at ranges 2. Of the 49 tiles in the 7×7, **38**. Of the 81 in the
  9×9, **70**. The clear core ends at range 1, and from range 2 outward the arcs
  start striking tiles. A zombie closing from 3 tiles to 2 tiles walks *under*
  band B and then band A.
- Including the translucent scrim, **259 tiles** carry some HUD paint, and the
  scrim disc alone spans `π·235² = 173,494 px² = 169.4 tile-equivalents`.

So: this HUD occupies 14.63% of the screen where the shipping one occupies 31.0%,
but it spends that budget on the 178 tiles nearest the avatar instead of on the
2,025-tile view's dead edges. **That is the trade, stated in the units the player
actually loses.** Whether a 9-tile clear core is enough is the single question
this design should be judged on, and it is not a question a percentage can
answer.

## Motion

Two animations are in the file. Both are continuous ambient loops, both are
opacity-only, and both are attached to conditions that are *persistent and bad*
rather than to the turn clock:

| what | element | property | values | duration | easing | resting value |
|---|---|---|---|---|---|---|
| critical-limb bloom | `#d06-critglow` (the 9px-blurred duplicate of the L-arm arc) | `opacity` | `.62 ↔ .94` | 1400ms | `ease-in-out`, infinite | `.62` (the 0% keyframe — what the screenshot shows) |
| tracked-contact pip | `.d06-pipT` (the amber diamond at 45°) | `opacity` | `1 ↔ .55` | 900ms | `ease-out`, infinite | `1` |

Nothing else in the file moves. Every arc, tag, pip, slot and card is a static
resting state, as the contract requires.

**Proposed, not in the file:**

- **Damage flash, not arc tweening.** RCSS cannot transition a decorator's
  gradient stops, so an arc's fill cannot animate — the producer rewrites the
  stop list and it jump-cuts. The buildable substitute is a second arc element
  covering only the segment just lost, painted `--d06-crit` and faded
  `opacity: 1 → 0` over **320ms ease-out** on the turn the damage lands. This is
  the one motion I would actually ship, because it reports an *event* and then
  gets out of the way.
- **Wheel reveal on hold.** `opacity: 0 → 1` with `transform: scale(.94) → scale(1)`
  over **120ms ease-out** on the wheel guide arc and all nine slots when TAB goes
  down, reversed at **90ms** on release. Fully buildable: `transform` and
  `opacity` are both transitionable in RmlUi 6.2, and the C++ side is one class
  toggle on keydown/keyup.
- **Pip bearing: buildable and deliberately rejected.** `transition: transform
  180ms cubic-bezier(.2,.7,.3,1)` on `.d06-spin` would work exactly as written.
  It is rejected below.
- **Breach-mark jitter while bleeding: rejected**, same reason.

### The turn-based hazard, directly

In a turn-based game every state change fires on the same event — the turn
boundary — and this design has put every animated element within 246px of the
exact pixel the player is staring at. If pips re-seek their bearings, arcs
re-fill and breach marks jitter once per turn, the centre of the screen twitches
once per keypress. That is the worst possible place for involuntary motion,
because it competes with the thing it is annotating: the avatar, and the tile the
player is about to step onto. A hostile that moves one tile can change bearing by
up to 45°, and sweeping a 10px diamond through 45° over 180ms drags the eye off
the avatar to follow it — the animation actively destroys the saccade saving the
whole design exists to buy.

So the rule is: **around the avatar, motion is permitted only for state that is
persistent, bad, and positionally static.** Everything discrete — arc fill, pip
bearing, need level, stamina, the overmap band — **jump-cuts on purpose**, because
a jump-cut at the centre of the screen is cheaper than a slide at the centre of
the screen. The two loops that survive that rule are the two in the file: they
change no element's position, they are slow (900/1400ms) and low-amplitude
(Δopacity 0.45 and 0.32), and they read as breathing rather than as an event.
They mean "you are still bleeding" and "it is still hunting you" — facts that
remain true between turns, which is exactly why a continuous loop is the honest
encoding for them and a per-turn transition is not.

## Feasibility in RCSS

Audited against the contract's verified RmlUi 6.2 lists. The document is 190
`div`s and 45 `span`s.

**Already on the available list, used as-is:** absolute positioning;
`transform: rotate()` (25 uses, all of them the `.d06-spin` wrapper, which is how
every polar mark is placed without trigonometry); `opacity`; `letter-spacing`;
`box-shadow` (14); `backdrop-filter: blur()` (2); `filter: blur()` (3, including
the critical bloom); `@keyframes` + `animation` (2); `box-sizing: border-box`;
`line-height`; `text-align`; `display: inline-block`; and the decorator set —
**`conic-gradient` (29 uses)** and **`mask-image` (28)**, both confirmed
available, which together are what make this design buildable at all.

**Needs changing on port:**

1. **`background-image:` → `decorator:`** on all 29 conic, 32 radial and 9 linear
   gradients. Property rename, identical painting.
2. **CSS custom properties (`--d06-*` and `var()`) do not exist in RCSS 6.2.**
   All 24 tokens must be flattened to literal hexes in the sheet, or emitted by
   the producer. This is the single largest mechanical cost of the port and it is
   completely invisible in the mockup. While flattening: `--d06-hair: #63797f` is
   declared and **never referenced anywhere** — a dead token; delete it.
3. **`text-shadow` → `font-effect`.** One rule (`.d06-o`) applied to roughly 60
   elements. Replacement is `font-effect: outline(1dp #04080a)`. RmlUi's
   `font-effect` accepts a comma-separated list, so the soft 4px halo can ride
   along as a second `shadow`/`blur` effect [INFERENCE — the list form is not on
   the contract's verified list, only the individual effects are]; if only one
   effect survives, **keep the outline**, because the outline is what makes 9px
   labels survive grass. Measured: body text on its own casing is 16.79:1.
4. **The `closest-side` mask trap — carry this verbatim.** `mask-image:
   radial-gradient(closest-side, …)` sizes the gradient to the circle inscribed in
   the element's box, and everything *outside* that circle — the four box corners —
   takes the gradient's **last** stop. If the last stop is opaque, every "ring"
   paints a square with a hole punched in it. All 14 shells therefore terminate
   the stop list explicitly: `transparent 0 X%, #000 X% 100%, transparent 100%`.
   Drop that trailing `transparent 100%` and the design becomes 14 black squares.
5. **`border-radius: 50%` is not portable — RCSS takes a length only.** The clear
   guide circle (`#d06-clear`, a 138×138dp box) becomes `border-radius: 69dp`.
   The 2px and 4px radii elsewhere are already lengths and port unchanged.
6. **`border: 1px dotted` has no RCSS equivalent** (no line-style keyword). Either
   a solid 1dp hairline at lower alpha, or drop the guide circle — it marks the
   clear radius for the reader of this mockup and carries no game data.
7. **RmlUi's default `display` is `inline`** (`StyleSheetSpecification.cpp:306`;
   CBN does not link the sample `rml.rcss`). Every ring shell, every mark, and
   every `<i>` bar fill needs an explicit `display: block`, or the bars get no
   width and the shells collapse — this is precisely the bug that leaves the
   shipping HUD's target-HP bar a permanently empty trough.
8. **Font.** `Consolas` stands in for the bundled `Source Code Pro` per the
   contract; advance width 0.5498em vs 0.6em, so every fixed-width label box is
   ~8% tight in the real font. Checked against the actual boxes: `.d06-tag` holds
   `20/24` (30.0dp of Source Code Pro in a 34dp box, +4.0 slack); `.d06-tagn`
   holds `PECKISH` (37.8dp in 46dp, +8.2); the sidearm line is 191.4dp in a 320dp
   wash. One box is genuinely tight: **`.d06-a` holding `INVENTORY` is 43.2dp in a
   44dp box — 0.8dp of slack.** Any longer action label clips, so slot labels are
   capped at 9 characters or the box grows.

**C++ producer work in `src/panels.cpp`.** This is where the real cost of this
design sits, and it is substantial:

- **A polar vitals producer, which does not exist.** Nothing today emits an
  angle. `hud_vitals` builds linear bars through `vbar_rml` (`panels.cpp:830`),
  and it already computes the per-part effect colour that would carry
  bleeding/bitten/splint state into a local at `panels.cpp:872` — then discards
  it, which is why the shipping HUD cannot say "this limb is open". This design
  needs, per body part: band assignment, span start, span length, fill length in
  degrees, fill and trough hexes, and the crit/bleeding/bitten flags. RCSS has no
  trigonometry and no usable `calc()`, so all of it arrives as a **pre-built
  `conic-gradient` stop-list string** plus a `transform: rotate(Xdeg)` per breach
  mark. The crit gate must also be fixed: `o.cur * 100 / o.max < 25` truncates
  `8*100/30` to 26 and never fires, and this design's entire left-hand column —
  plate, badge, bloom, warm trough, breach marks — hangs off that flag.
- **A threat-bearing producer, which does not exist.** New work: walk the visible
  creature list, compute `atan2` from `u.pos()` to each creature, emit one
  `.d06-spin` wrapper per contact carrying `transform: rotate(bearing)` plus a
  hostile/tracked class, and emit the range and behaviour strings for the target
  card (`4 TILES NE`, `MOVING`) — both of which the shipping HUD drops.
- **`hud_hotbar(avatar &)` ignores its argument**, so per-slot availability is
  never emitted and there is no disabled state at all. The `NO GUN` slot needs
  the producer to test each action against the avatar and emit `.d06-off` plus a
  reason string. Slot *positions* can stay hard-coded in RCSS as they are here —
  nine fixed `rotate(30k deg)` values are cheaper than emitting them.
- **A 16-sector overmap fold.** `hud_map` emits an 11×11 glyph grid; the band
  needs a dominant-terrain reduction per 22.5° sector plus per-POI bearing and
  distance. This is lossy by construction, which is why `M` stays the real answer
  and the HUD says so on screen.
- **Parity work for everything the shipping HUD drops** and this design shows:
  profession, weather, wind, light level, flashlight charge, morale, encumbrance,
  noise, warmth, the sidearm, the weapon's damage numbers, and the target's range
  and behaviour. Several helpers already exist and are called by nobody
  (`morale_stat`, the light-level helper) — restoring them is parity with the
  curses sidebar this replaced, not new features.
- **Geometry is a new input.** `sidebar_hud_apply_rect()` positions regions as
  percentages of the viewport. This design has no regions: it has one full-screen
  transparent layer whose ring centre must be written from the avatar's *screen
  pixel* position every frame — which the HUD does not currently know. That new
  input is also exactly what weakness 3 turns on.

No new assets and no new font are needed.

## Weaknesses

1. **It occludes the tiles the player most needs to see, and the numbers are not
   small.** 178 distinct 32px tiles carry opaque ring ink; 112 of them are more
   than a quarter covered, 64 more than half, and the worst are entirely covered.
   The clear core is exactly 9 tiles — the player's own and its eight neighbours
   — and it ends there: 14 of the 16 tiles at range 2 are struck, 38 of 49 in the
   7×7, 70 of 81 in the 9×9. A hostile closing from 3 tiles to 2 passes *under*
   band B and then band A, so the design is least legible about terrain in exactly
   the ring where a melee threat resolves. Every other design in this set occludes
   more total area and none of them occludes anything the player is looking at.
   This is the trade, and it may simply be the wrong one.
2. **At 1280×720 it stops being a HUD and becomes a mandala.** Nothing here scales
   with the viewport — the radii are absolute pixels tied to a 32px tile size. The
   assembly disc alone goes from 8.4% of the frame to **18.8%**, the wheel's outer
   diameter goes from 45.6% to **68.3% of the screen height**, and the weighted
   occlusion goes from 14.63% to **32.9% — worse than the 31.0% baseline it beats
   at 1080p** (raw footprint 54.0%). Worse, the peripheral blocks are positioned
   from fixed corner offsets, so the log wash at `y 876–1042` falls entirely off a
   720-tall screen. A real port needs the ring radii expressed in tile widths and
   the peripheral blocks anchored per-corner with a reflow — neither of which this
   mockup demonstrates.
3. **The rings clip the moment the avatar is not centred.** The full assembly needs
   246px of clearance in every direction, so the avatar-safe box is
   `1428 × 588 = 40.5% of the frame` at 1080p and `788 × 228 = 19.5%` at 720p.
   CBN does not always centre the avatar — the look-around cursor, vehicle views,
   `centered` off, and the edges of the loaded map all move it — and the instant it
   moves within 246px of an edge the wheel clips, the compass ring becomes a
   partial arc, and the bearing read breaks. Bearing is the one encoding here with
   no fallback: a pip whose sector is off-screen conveys nothing, and there is no
   number anywhere that says "91°". Every other design degrades gracefully when
   the view pans; this one loses a data channel.
4. **Colourblind legibility fails where the design has no second channel.** The
   limb arcs are fine — `--d06-ok #35d6a0` vs `--d06-crit #ff5a4e` is only 1.65:1
   in luminance, but arc *length* duplicates the same value, and fill vs trough is
   8.70:1, so a deuteranope still reads "this arc is short". Three places have no
   such redundancy. **The 6px needs ring is the worst:** `--d06-need #e9973f` and
   `--d06-cold #78b6e8` differ by 0.035 in relative luminance — **1.08:1**, an
   effectively invisible luminance step — so on a 6px stroke the *direction* of
   badness (too cold vs too hungry) is carried by hue alone, and degrades to
   reading four 9px words. **The threat pips** are red `#ff3b30` vs amber `#ffb300`,
   precisely the pair protanopes and deuteranopes confuse; the 10px→14px size step
   and the white core are the only surviving cues at that size over noisy terrain.
   **The log's three hues** (`good`/`bad`/`caution`, 1.60:1 and 1.66:1) have no
   second channel at all — the opacity ramp encodes age, not severity — so six
   rows collapse into near-identical olive text. That last one is the shipping
   HUD's failure, reproduced unchanged.
