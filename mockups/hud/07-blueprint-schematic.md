# 07 — Blueprint Schematic

## Thesis

Every other HUD asks *where do I put the panels*. This one refuses the question:
a survival HUD should be a **technical drawing laid over the world**, and a
technical drawing has no panels — it has line work, dimension strings, extension
and leader lines, schedules, drawing notes and a title block. The consequence is
that information is *annotated onto the scene* rather than boxed beside it. The
avatar is not "the player sprite with a bar near it"; it is **datum A**, the
origin of a coordinate system, and the hostile four tiles north is dimensioned
*off that datum* with a real ordinate string, so the spatial relationship the
player actually cares about is drawn in the space it happens in. The body's
health is not six bars; it is **six independent dimension strings struck off an
elevation of the subject**, where the extension line springs from the limb it
measures and the solid portion of the dimension line *is* the current value
against a full-length line that *is* the maximum. A drawing cannot draw an
aggregate — you cannot dimension "the body" — so the format structurally forbids
the single-HP-bar failure the contract warns about. What it trades away is
**immediacy**. A drawing rewards a reader who has learned
its conventions and punishes one who has not: nothing here is a big red number.
The critical left arm is signalled by four redundant channels (a 2px accent edge
instead of 1px, a 4px red hatch instead of a 7px amber one, the word `CRITICAL`
in the dimension name, and a leadered `!` hazard flag) rather than by one
screaming glyph, and a player who has not learned that heavier line weight means
"this part of the drawing is a revision" will read it slower than they would read
a flashing bar. It also trades away **compactness of language**: the drafting
register needs a title block, a key, and captions that explain the encoding
(`DIM VALUE = CUR/MAX · SOLID = CUR`), and those are pixels spent on the
grammar rather than on the data. The bet is that the grammar pays for itself,
because once you know it, *every* new field has an obvious home — the sidearm
goes in an armament schedule, the weather goes in the title block's drawing
conditions, the season is a revision — and the layout never has to be
renegotiated to add a number.

## Register

Cyanotype-inverted: ink is light, ground is dark navy, and the ground is always
translucent. **Nothing in this sheet is opaque.** The heaviest ground on it is
the title block's `.72`; the schedules sit at `.55`; the body plates at `.42`.
That is a design commitment, not an accident — a drawing that blacks out the
thing it annotates is a worse drawing.

### Palette

| token | hex | role |
|---|---|---|
| `navy` | `#050C1E` | the only ground colour, used at α .12–.80, never 1.0 |
| `cyan` | `#00FFFF` | primary line: section rules, table head/foot rules, intact dimensions, reticle datum lines, leaders |
| `accent` | `#00FFCC` | callout / critical annotation: the L arm's 2px edge, the L arm dimension string, note flags, the current revision letter, the datum marker, the key-plan `@` |
| `dim` | `#35637C` | secondary rule: interior row rules, column rules (α .75), field grid (α .30), key-plan box |
| `amber` | `#FFB000` | caution value: head/torso/L leg dimensions, all four need states, pain, morale, stamina, magazine, flashlight, caution log rows, mission marker |
| `red` | `#FF4B4B` | harm value: hazard flag, L-arm hatch, hostile counts, target HP fill, bad log rows, the strike through the unavailable command |
| `paper` | `#DCE9F2` | primary lettering: section titles, dimension values, callout heads, title-block values |
| `pale` | `#BFD6E4` | log body text, command labels |
| `slate` | `#8FB2C6` | schedule labels, callout sublines |
| `tick` | `#7FA8BF` | ruler numerals, dimension names, extension lines, trim marks |
| `faint` | `#5C87A0` | captions, log timestamps, title-block field labels, key indices |
| `off` | `#4A6274` | the one unavailable command (slot 1, no gun wielded) |
| `plan/field` | `#2F4E5E` | key-plan `.` cells |
| `plan/forest` | `#2E6B4A` | key-plan `F` cells |
| `plan/house` | `#3E7E96` | key-plan `H` cells |

Green is deliberately absent. "Intact" is cyan, which is the same colour as the
line work — an undamaged part is *just drawn*, and only deviation gets a hue.

### Type

One face. `Consolas` stands in for `Source Code Pro`, which itself stands in for
a DIN-1451-style drafting stencil; see Feasibility for what the substitution
costs. Seven sizes, each tied to a role, never to a region:

| role | class | size / letter-spacing / line-height | colour |
|---|---|---|---|
| title-block subject | `.tbbig` | 20px bold, ls 3.4, lh 24 | `paper` |
| revision stamp letter | `.stampt2` | 19px bold, ls 4.6, lh 26 | `accent` |
| dimension value | `.dimval` | 15px bold, ls 1.2 | per severity |
| section title | `.vtitle` | 14px bold, ls 3.2 | `paper` |
| callout head | `.callab` | 13px bold, ls 2.4 | `paper` |
| key-plan cell | `.mrow` | 13px, ls 5, lh 16 | plan tints |
| schedule value | `.celval` | 12px bold, ls 1.1 | per severity |
| title-block value | `.tbv` / `.tbcyn` | 12px, ls 1.5 | `paper` / `cyan` |
| body default | `.t` | 12px, ls 1.1, lh 14 | `paper` |
| labels / log rows / numerals | `.cellab` `.revtxt` `.calsub` `.num` `.revid` | 11px, ls 0.6–1.6 | `slate` / `pale` / `tick` |
| schedule header | `.thead` | 10px, ls 2.6 | `cyan` |
| dimension name | `.dimname` | 10px, ls 2.2 | `tick`, `accent` when critical |
| caption / note / legend / timestamp | `.caption` `.note` `.maplbl` `.klab` `.revtime` | 10px, ls 1.2–1.6 | `faint` / `pale` |
| field label / key index / stamp label | `.tbl` `.kn` `.stampt1` `.stampt3` | 9px, ls 1.0–3.4 | `faint` / `accent` |

Field *labels* are stencil caps (`LOCATION`, `SIDEARM · STOWED`). Field *values*
are set exactly as the fixture supplies them — `Nigel Ashby`, `Cheswick —
residential`, `Glock 19`, `9mm JHP`, `Light drizzle`. That is deliberate and it
is the real drafting convention: a schedule quotes a part designation verbatim
and never restyles it. It also means the strings are recoverable from the
rendered text exactly as specified.

### Border / frame treatment

There are no panel borders, because there are no panels. Instead:

- **One sheet border**, 1px `dim` at (20,20)–(1896,1056), with four 24px trim
  marks outside it at the corners.
- **Every rule is 1px.** `cyan` for a structural rule (a section underline, a
  schedule's head and foot rule); `dim` at α .75 for interior row rules and
  column rules. A schedule is a *ruled* block, not a bordered one — it has no
  left or right edge at all, so the wash bleeds into the drawing.
- **Every hairline and plate carries a 1px black casing**
  (`box-shadow: 0 0 0 1px rgba(2,6,14,.62–.85)`) and **every text element
  carries a 4-way 1px black outline** (`filter: drop-shadow` ×4). Without this a
  1px cyan rule vanishes wherever it crosses `#a8b878` grass. This is the single
  most important detail in the register: it is what makes a translucent HUD
  legible over a noisy tile map at all.
- **No rounding** except where a drawing would round: the joint markers (elbow,
  knee, hand, foot) and the round note-flag bubbles.
- **Line weight is meaning.** The only 2px edges on the sheet are `.fig.crit`
  (the left arm) and the revision stamp. Heavier line = "this is the revision
  under discussion".

## Layout

All geometry is absolute at 1920x1080. The organising module is the game's own
tile: **1 tile = 32px**, and the major grid pitch is **5 tiles = 160px**, which
is why the rulers, the field grid, and every dimension string's full length are
all 160px multiples.

### Sheet furniture

| element | rect | note |
|---|---|---|
| sheet border | 20,20 1876x1036 | 1px `dim`, α-cased |
| trim marks | (12,12) (1907,12) (12,1067) (1907,1044) | 24px arms |
| top ruler axis | x 60→1896 at y=52 | minor ticks 32px pitch (6px tall) from x=144, major 160px pitch (13px tall) |
| top ruler numerals | −25…+20 at 160px pitch | zero centred on x=944, the avatar column |
| left ruler axis | y 60→1052 at x=52 | same tick grammar, majors from y=208 |
| field grid | vertical 160px pitch, x 144→1425, y 72→1000; horizontal 160px pitch, y 208→849, x 72→1544 | `dim` at α .30 — the sheet's setting-out grid |
| first-angle note | 72,12 266x14 | `TILES ± FROM DATUM A · FIRST ANGLE` |
| scale note | 1598,12 298x14 | `SCALE 1T = 32 PX · GRID 5T = 160 PX` |
| sight bracket | 816,62 254x8 + caption 832,70 | light level drawn as *reach*: `SIGHT R 4 T · LIGHT 12% MOON`, struck symmetrically off the datum column |

### ELEVATION 1 — the subject (x 60–520, y 74–490)

Title band at 70,74; scale `1:1` at 466,76; section rule at y=98 spanning
x 72–520; the encoding caption at 70,103.

The figure is an anthropometric elevation on centreline **x=288**, crown
**y=124**, ground line **y=458**, drawn as 24 outlined segments meeting at joint
markers. It is a **rear view**, so screen-left is the subject's left:

| part | x extent | y extent | segments |
|---|---|---|---|
| head | 274–302 | 124–160 | skull + jaw rule |
| neck | 283–293 | 160–172 | |
| torso | 262–314 | 172–296 | shoulder yoke 172–181, chest 181–238, waist 238–268, pelvis 268–296, hip rule at 296 |
| L arm | **246–262** | 181–321 | upper 181–243, elbow 239–249, forearm 249–303, hand 303–321 |
| R arm | 314–330 | 181–321 | same four segments |
| L leg | 261–284 | 296–456 | thigh 296–374, knee 370–379, shin 379–444, foot 444–456 |
| R leg | 292–315 | 296–456 | same four segments |

The arms hang off the **shoulder yoke** rather than floating beside the chest,
and the legs off the **pelvis** — that is what makes the assembly read as a body
instead of a stack of rectangles. Each limb's segments are separated by their own
1px outlines, which is correct for a line drawing: adjacent parts share an edge,
they do not merge.

Six dimension strings, all 160px long (= 5 tiles, the grid module), each with two
witness lines, two 45°-rotated arrowheads, a dashed extension line springing from
the correct part, the value above the line and the part name below it:

| part | string | side | extension springs from | fill |
|---|---|---|---|---|
| Head 20/24 | x 352–512 at y=140 | right | head right edge x=302 | 133/160 = 83% |
| Torso 45/60 | x 352–512 at y=196 | right | chest right edge x=314 | 120/160 = 75% |
| R arm 30/30 | x 352–512 at y=252 | right | R forearm right edge x=327 | 160/160 = 100% |
| R leg 28/28 | x 352–512 at y=400 | right | R shin right edge x=308 | 160/160 = 100% |
| **L arm 8/30** | x 72–232 at y=224 | **left** | L upper arm left edge x=246 | 43/160 = 27%, filled from the *right* so it grows toward the limb |
| L leg 22/28 | x 72–232 at y=372 | left | L knee left edge x=265 | 126/160 = 79% |

The left arm is the only part dimensioned on the left side of the figure, which
puts its string on the same side of the drawing as the wound and makes it the
first thing scanned. Its treatment: `accent` instead of `amber`/`cyan`, a name
that reads `L ARM — CRITICAL`, a 2px edge on all four limb segments, and a 4px
red hatch (against the 7px amber hatch used for merely-damaged parts). A
leadered flag group sits at 72,146 — a red `!` hazard flag plus round note flags
`1` and `2`, captioned `BLEEDING · BITTEN` — with a horizontal leader running to
x=213 then a 45° dogleg landing an arrowhead on the left upper arm at (245,188).
Notes 1 and 2 are the same numbers used in the DRAWING NOTES schedule.

Caption at 70,474: `DIMENSIONED INDEPENDENTLY — NO AGGREGATE FIGURE IS DRAWN`.

### Left column schedules

| block | rect | structure |
|---|---|---|
| TABLE 1 SPECIFICATION | 72,520 448x220 | two label/value column pairs split by a vrule at x=296; 9 interior rules at 22px pitch; stamina gets a 200x8 trough at 154,682 with a 117px amber fill (58%) |
| REVISION HISTORY | 72,796 474x154 | the message log as a title-block revision table: revision letter A–F (vrule at x=114), timestamp (vrule at x=170), text; 6 rows at 22px pitch; a 3x22 accent bar at 72,928 marks rev F as current |
| KEY — COMMANDS | 72,1000 1324x36 | nine cells at 131px pitch, vrules at 212, 343, 474, 605, 736, 867, 998, 1129, 1260 — a drawing legend, not a hotbar |

### Field annotation (the play area itself)

| element | geometry | carries |
|---|---|---|
| datum A | circle 931,515 24x24 (centre 943,527, the avatar tile centre) + four cross arms + a witness line down to y=590, a horizontal run to x=880, a filled triangle at 873,590 and a boxed `A` at 846,582 | the avatar as coordinate origin; label `DATUM A · 0,0` at 950,582 |
| target reticle | circle 955,379 40x40 (centre 975,399) + four red ticks | the zombie brute's tile |
| ordinate N | vertical dimension at x=913, y 400→528, extension lines at both ends, label `4 T N` at 836,456 | the target's 4-tile northing off the datum |
| ordinate E | horizontal dimension at y=568, x 944→976, label `1 T E` at 994,561 | the target's 1-tile easting; N+E is the fixture's `4 tiles NE` |
| leader 1 | arrowhead at 986,380 → 386px oblique at −45° → shoulder (1264,112) → horizontal to x=1556, landing tick at x=1543 | contact identity, out to callout 1 |
| leader 2 | arrowhead at 948,512 → 440px oblique at −45° → shoulder (1264,206) → horizontal to x=1556, landing tick at x=1543 | subject identity, out to callout 2 |

Both leaders are parallel and share a shoulder column at x=1264, which is what
makes them read as drafted rather than as arbitrary lines. **The whole centre of
the screen — roughly x 340–1550, y 90–1000 minus the annotation itself — is
untouched.** That is the point of the format.

### Right column (x 1556–1896, 340 wide)

| block | rect | structure |
|---|---|---|
| CALLOUT 1 (contact) | 1556,99 → 1818,152 | 3x52 accent bar; head `CONTACT 1 — zombie brute`; subline `HP 62% · moving · 4 tiles NE`; a 180x9 trough at 1566,137 with a 110px red fill = 62%; numeral `62%` at 1756 |
| CALLOUT 2 (subject) | 1556,193 → 1818,243 | 3x52 accent bar; head `SUBJECT — Nigel Ashby`; sublines `Survivor · day 14 · 21:47` and `Cheswick — residential` |
| TABLE 2 HAZARD SCHEDULE | 1556,268 340x132 | vrule at 1716; 5 rows at 22px pitch |
| TABLE 3 ARMAMENT SCHEDULE | 1556,412 340x154 | vrule at 1716; 6 rows at 22px pitch; flashlight charge gets a 56x8 trough at 1724,552 with an 18px fill = 34% |
| KEY PLAN | 1556,578 340x222 | 152x190 map box at 1564,606 holding the 11x11 overmap chunk (11 rows at 16px pitch from y=610, glyphs from x=1572 at a 12.15px cell pitch); a five-line glyph legend at x=1732 |
| DRAWING NOTES | 1556,812 340x108 | the five effects as numbered drawing notes, 16px pitch from y=839 |
| REVISION STAMP | 1424,826 120x94 | double-ruled accent box: `REVISION / F / Autumn / day 14 · 21:47` |
| TITLE BLOCK | 1424,932 472x104 | bottom-right, per drafting convention. Band A (933–972, vrule at 1700): `SUBJECT OF SHEET / Nigel Ashby` and `ROLE / Survivor`. Band B (973–1000): `LOCATION / Cheswick — residential` and `DATE / day 14 · 21:47`. Band C (1001–1018): `CONDITIONS / Light drizzle · 8 °C · wind NE 12 km/h`. Band D (1019–1036): `SCALE / 1:1 · 1 TILE = 32 PX` and `REV / Autumn · sheet 1 of 1`. |

The two callouts sit exactly where the leaders land, which is the reason the
right column starts at y=99 rather than at the top of the sheet: **the label's
position is dictated by the geometry of the thing it labels**, not by a region
allocation.

## Data mapping

Sixty fixture fields. **Nothing is dropped.**

| fixture field | where it appears | encoding |
|---|---|---|
| Name `Nigel Ashby` | callout 2 head (1566,195) **and** title block band A (1432,947, 20px) | text, `paper`; position — it is the subject the leader points at |
| Profession `Survivor` | callout 2 subline **and** title block band A `ROLE` | text, `slate` / `paper` |
| Day `14` | callout 2 subline, title block band B `DATE`, revision stamp | text |
| Time `21:47` | same three places, plus revision rows C–F | text |
| Season `Autumn` | revision stamp **and** title block band D `REV` | text, `accent` — the season *is* the drawing's revision |
| Place `Cheswick — residential` | callout 2 subline **and** title block band B `LOCATION` | text, `paper` |
| Weather `Light drizzle` | title block band C `CONDITIONS` | text — weather is a drawing condition |
| Temperature `8 °C` | title block band C | text |
| Wind `NE 12 km/h` | title block band C | text |
| Light level `12%` (moonlight) | top-of-sheet sight bracket, `LIGHT 12% MOON` | text + **position/extent**: the bracket's 254px width is the 4-tile sight radius struck off the datum column, so light level is drawn as reach |
| Flashlight lit at `34%` | TABLE 3 row 6, `FLASHLIGHT · LIT / 34%` | numeric + 56x8 bar, 18px amber fill |
| STR `10` | TABLE 1 r1c1 | numeric, `cyan` |
| DEX `12` | TABLE 1 r1c2 | numeric, `cyan` |
| INT `9` | TABLE 1 r2c1 | numeric, `cyan` |
| PER `11` | TABLE 1 r2c2 | numeric, `cyan` |
| Speed `96` | TABLE 1 r3c1 | numeric, `cyan` |
| Focus `84` | TABLE 1 r3c2 | numeric, `cyan` |
| Pain `12` | TABLE 1 r4c1 **and** drawing note 5 | numeric, `amber` |
| Morale `−3` | TABLE 1 r4c2 | numeric, `amber` |
| Encumbrance torso `12` | TABLE 1 r5c1 | numeric |
| Encumbrance arms `4` | TABLE 1 r5c2 | numeric |
| Move mode `walking` | TABLE 1 r6c1, `WALKING` | text |
| Noise made `8` | TABLE 1 r6c2 | numeric |
| Head `20/24` | ELEVATION 1 dimension string at y=140 | dimension: 160px line = max, 133px solid = cur, numeral `20/24`, amber; extension line from the head |
| Torso `45/60` | dimension string at y=196 | dimension, 120/160 solid, amber; extension from the chest |
| **L arm `8/30`** | dimension string at y=224, **left side** | dimension, 43/160 solid filled toward the limb, `accent` |
| L arm **critical** | same string | **four redundant channels**: `accent` hue instead of amber, the literal word `CRITICAL` in the dimension name, a **2px** edge on all four L-arm segments where every other part is 1px, and a red `!` hazard flag leadered to the limb. Not gated on any `< 25%` threshold — the producer flags the part, the drawing draws the flag |
| L arm **bleeding** | note flag `1` **on the limb**, caption `BLEEDING · BITTEN` at 72,171, drawing note 1, revision row C | glyph + colour + **position**: the flag's leader lands on the arm at (245,188), 132px from the limb it describes — versus 891px in the shipping HUD |
| L arm **bitten** | note flag `2` on the limb, same caption, drawing note 2 | glyph + position |
| R arm `30/30` | dimension string at y=252 | dimension, 160/160, cyan |
| L leg `22/28` | dimension string at y=372, left side | dimension, 126/160, amber |
| R leg `28/28` | dimension string at y=400 | dimension, 160/160, cyan |
| Stamina `5100 / 8700` | TABLE 1 row 8 | numeric + 200x8 bar, 117px amber fill (58%) |
| Hunger `Peckish` | TABLE 1 r9c1 | text, `amber` |
| Thirst `Thirsty` | TABLE 1 r9c2 | text, `amber` |
| Fatigue `Tired` | TABLE 1 r10c1 **and** drawing note 4 | text, `amber` |
| Warmth `Chilly` | TABLE 1 r10c2 | text, `amber` |
| Effect `Bleeding (L arm)` | drawing note 1 + flag `1` on the figure | numbered note, red, cross-referenced `see ELEV 1` |
| Effect `Bitten (L arm)` | drawing note 2 + flag `2` on the figure | numbered note, red |
| Effect `Soaked` | drawing note 3, `Soaked — drizzle, exposed` | numbered note, amber |
| Effect `Tired` | drawing note 4, `see TABLE 1 FATIGUE` | numbered note, amber |
| Effect `Pain 12` | drawing note 5, `see TABLE 1 PAIN` | numbered note, amber |
| Wielded `fire axe` | TABLE 3 row 1 | text, `accent` — the one item in the schedule that is in hand |
| `32 bash`, `0 cut` | TABLE 3 row 2, `32 bash / 0 cut` | numeric |
| `to-hit +1` | TABLE 3 row 3 | numeric |
| Sidearm `Glock 19` | TABLE 3 row 4, `SIDEARM · STOWED` | text — stowed state carried by the label, not by dimming |
| `12/17`, `9mm JHP` | TABLE 3 row 5, `MAGAZINE / 12/17 · 9mm JHP` | numeric + text, amber (partial magazine) |
| Target `zombie brute` | callout 1 head **and** TABLE 2 row 4 | text; the callout is the annotation, the schedule is the tabulation |
| Target HP `62%` | callout 1: 180x9 trough with a 110px red fill, plus the numeral `62%` | bar + numeric (both — the shipping HUD's target bar is a permanently empty trough) |
| Target `4 tiles NE` | **drawn in the field**: ordinate dimensions `4 T N` (x=913, 128px) and `1 T E` (y=568, 32px) off datum A, **and** callout 1 subline, **and** TABLE 2 row 5 | position + numeric. This is the field's whole argument: range and bearing are dimensioned in the space they occur in |
| Target status `moving` | callout 1 subline | text |
| `3 hostiles visible` | TABLE 2 row 1 | numeric, red |
| `1 tracking` | TABLE 2 row 2, `TRACKING SUBJECT` | numeric, red |
| `SAFE MODE ON` | TABLE 2 row 3, `SAFETY INTERLOCK / SAFE MODE ON` | text, `accent` — an interlock is a schedule entry on a drawing, not a status light |
| Log row 21:46 `You hit the zombie brute…` | REVISION HISTORY rev A | text, `pale`, α .60 |
| Log row 21:46 `The zombie brute claws…` | rev B | text, red, α .72 |
| Log row 21:47 `Your left arm is bleeding.` | rev C | text, red, α .82 |
| Log row 21:47 `You feel a sharp pain.` | rev D | text, dimmed red `#C4645F`, α .88 |
| Log row 21:47 `The zombie shambler groans…` | rev E | text, amber, α .94 |
| Log row 21:47 `You are getting thirsty.` | rev F | text, amber, α 1.0, accent revision letter, 3x22 accent bar at the sheet edge |
| Log ordering (newest last) | newest at the **foot** | position — and it is also the drafting convention for a revision table, so the format and the requirement agree |
| Minimap 11x11 | KEY PLAN, 1564,606 | ASCII glyph grid, one span per glyph run, 12.15px cell pitch |
| Player position | `@` at row 6 col 5 | glyph in `accent` bold — the only accent-coloured cell in the plan |
| Mission marker NE | `^` at row 2 col 10 (plus row 4 col 2, row 9 col 5) in `amber`, legend `^  MISSION NE` | glyph + colour |
| Hotbar 9 slots | KEY — COMMANDS: `1 [f] FIRE` … `9 [5] WAIT` | index + boxed key cap + label, i.e. a drawing legend entry |
| Hotbar slot 1 unavailable | slot 1 only: `off`-coloured cap and label, a red strike rotated −42° through the cap, and the literal `N/A` | colour + **strike-through glyph** + text; three channels, none of them colour-only |
| Colour semantics | red = harm, amber = caution, cyan = intact/nominal, accent = the thing under revision | see palette table; green is deliberately unused |

Two substitutions, stated so neither reads as an error:

- **Overmap glyphs** use the contract's suggested `H`/`F`/`^`/`.` verbatim, one
  character per cell with the gap made by `letter-spacing: 5px` rather than by
  literal spaces (the contract's inter-glyph spaces are markdown formatting).
- **Hotbar keys** are treated as this player's bindings, so `f r ' e p c i t 5`
  are used as given rather than the shipped defaults.

## Occlusion budget

**16.24% of the 1920x1080 play area**, against the ratified baseline of
**31.0%** — 52% of the current HUD's footprint. Play area = `1920 x 1080 =
2,073,600 px²`.

Method, per the contract: count anything opaque or near-opaque (effective α ≥
0.85) at full weight, count translucent washes at half weight, and take the
**union** rather than the sum so overlapping blocks are not double-counted. All
figures below were measured by rasterising every element's painted footprint at
1px resolution in Chromium. Two refinements the baseline did not need, because
this design is line work rather than region rects:

- **Gradient hairlines are counted as ink, not as bounding boxes.** The field
  grid is a `repeating-linear-gradient` element 1281x928 px in size that paints a
  1px line every 160px — 0.625% of its own box. Counting its box would
  attribute 1.19M px² to eight ruled lines. Ink is therefore
  `box area x duty cycle`: 1/160 for the grid and major ticks, 1/32 for minor
  ticks, 1/2 for the 4-on-8 dashed extension lines, 0.6 for the 6-on-10 figure
  centreline, 1/7 for the amber hatch and 1/4 for the red hatch.
- **Rotated line work is counted on its untransformed geometry.** A 386px 1px
  leader rotated −45° has a 273x273 = 74,529 px² axis-aligned bounding box and
  paints 386 px². The three obliques and their arrowheads are measured at 2,473 px².

### Grounds (translucent, half weight)

| block | rect | area |
|---|---|---|
| TABLE 1 SPECIFICATION (α .55) | 448 x 220 | 98,560 |
| KEY PLAN (α .55) | 340 x 222 | 75,480 |
| REVISION HISTORY (α .55) | 474 x 154 | 72,996 |
| TABLE 3 ARMAMENT (α .55) | 340 x 154 | 52,360 |
| TITLE BLOCK (α .72) | 472 x 104 | 49,088 |
| KEY — COMMANDS (α .72) | 1324 x 36 | 47,664 |
| TABLE 2 HAZARD (α .55) | 340 x 132 | 44,880 |
| DRAWING NOTES (α .55) | 340 x 108 | 36,720 |
| **8 washes, subtotal** | | **477,748** |
| 46 per-line text scrims (α .72 / .76) | | 81,750 |
| 43 plates: 24 figure segments (α .42), 3 bar troughs (α .70), 3 note flags + 9 key caps + datum box (α .80), revision stamp (α .45), datum and reticle discs (α .12/.18) | | 37,245 |
| **raw ground total** | | **596,743** |
| overlaps removed (shared block boundaries; pixels also carrying ink) | | −27,482 |
| **union of grounds** | | **569,261** |
| **at half weight** | | **284,630** |

### Ink (opaque, full weight)

| | area |
|---|---|
| union of all axis-aligned solid rules, dimension lines, dimension fills, witness ticks, arrowheads, column rules, trim marks, accent bars, bar fills, key strike, and all 1px/2px element borders | 41,697 |
| three rotated leader obliques + their arrowheads, measured untransformed (2,473) plus the dashed extension lines and figure centreline at their gradient duty (349) | 2,821 |
| **ink subtotal** | **44,518** |

### Texture (translucent hatch and grid, half weight)

| | area |
|---|---|
| field grid, 160px pitch, two elements | 13,327 |
| ruler ticks, 32px and 160px pitch, four elements | 542 |
| body hatch: amber at 1/7 duty (972) + L-arm red at 1/4 duty (266) | 1,238 |
| **subtotal 15,107, at half weight** | **7,554** |

### Total

    284,630 + 44,518 + 7,554 = 336,702
    336,702 / 2,073,600 = 0.16238  ->  16.24%

Per region, as a share of the screen (this split double-counts ~330 px of shared
boundary pixels, so it sums to 16.25 rather than 16.24):

| region | weighted px | % of screen |
|---|---|---|
| right column (2 callouts + TABLE 2 + TABLE 3 + KEY PLAN) | 103,458 | **4.99%** |
| TABLE 1 SPECIFICATION | 52,399 | 2.53% |
| REVISION HISTORY | 41,492 | 2.00% |
| ELEVATION 1 (figure + six dimension strings + flags) | 30,145 | 1.45% |
| DRAWING NOTES + revision stamp | 25,234 | 1.22% |
| TITLE BLOCK | 26,027 | 1.26% |
| KEY — COMMANDS | 24,959 | 1.20% |
| field annotation (datum, reticle, ordinates, leaders) | 18,665 | **0.90%** |
| sheet border, trim, rulers, field grid | 14,653 | 0.71% |

The two numbers worth reading together are the last two rows against the first.
Annotating the world *in* the world — datum, reticle, two ordinate dimensions and
two leaders reaching 600px across the screen — costs **0.90%**. The drawing's
setting-out furniture, which is 100% grammar and 0% data, costs **0.71%**.
Everything expensive is a schedule, i.e. a table, i.e. the one part of this design
that is not doing anything a conventional panel could not do.

**Full-weight alternative: 30.33%.** If every wash were counted as opaque this
design would be indistinguishable from the baseline's 31.0% — the entire saving
is that nothing on the sheet is opaque, and the α buys real terrain — measurably,
not impressionistically.
Sampling `_shared/world.png` and compositing at the design's own alphas
(`result = α·#050C1E + (1−α)·terrain`):

| probe | terrain | on screen | under |
|---|---|---|---|
| (200,560) building floor | `106,114,62` | `50,58,44` | TABLE 1, α .55 |
| (200,700) grass | `69,77,48` | `34,41,38` | TABLE 1, α .55 |
| (460,700) dirt | `90,99,53` | `43,51,40` | TABLE 1, α .55 |
| (400,1018) grass | `49,61,26` | `17,26,29` | KEY strip, α .72 |
| (760,1018) road | `28,26,26` | `11,16,29` | KEY strip, α .72 |

Through the schedules' `.55` the floor and the grass still differ by ΔRGB **24**
and the grass and dirt tiles by **14** — the checkerboard is still readable as
terrain. Even through the **heaviest** ground on the sheet, the `.72` KEY strip,
grass and road remain **12** apart, so the player can still see where the road is
under the command legend. Quote **16.24%** against the baseline's
31.0%; quote 30.33% only against a design that is also being counted at full
weight, so both sides use one rule.

## Motion

Nothing in the mockup animates, and the static frame is the design's resting
state. What *should* animate, all of it expressible as RCSS transitions and
`@keyframes` on properties this design already uses:

- **Dimension fills: `transition: width 220ms ease-out`.** When a part takes
  damage the solid portion of its dimension line retracts. The **numeral does not
  tween** — a drafting value is exact or it is wrong, so `20/24` snaps while the
  line slides. This is the design's one strong motion opinion.
- **The critical flag, not the critical part.** `.hazflag`'s red edge runs an
  `opacity: 1 → .55 → 1` cycle at **1400ms sine-in-out, repeat 3**, matching the
  shipping `status_bleed` `ambient` spec (`gfx/widgets/icons.json`). The *limb*
  never pulses: a drawing's line weight is not allowed to breathe, or the reader
  stops trusting line weight to mean anything.
- **Leaders draw themselves in.** When a contact becomes tracked, its oblique and
  horizontal leader animate `width: 0 → n` over **180ms linear**, oblique first,
  then the horizontal, then the callout's wash fades in over 120ms. A contact
  leaving reverses it. The effect is that the drawing is *being annotated* as the
  situation develops, which is the whole thesis expressed as motion.
- **Revision rows: 200ms slide-and-fade from below**, matching `hud_log_entry`.
  The accent bar marking the current revision is **deliberately static** — the
  same decision the shipping HUD's fresh-row treatment got right, for the same
  reason: it must not replay on an unrelated redraw.
- **Target HP fill: `transition: width 160ms linear`**, no easing. It is a
  measurement of something outside the player's body; it should feel mechanical.
- **Nothing else moves.** Not the rulers, not the field grid, not the sheet
  border, not the title block, not the schedules, not the key plan (which
  jump-cuts on an overmap tile change, as it must). A drawing that redraws itself
  is not a drawing.

## Feasibility in RCSS

The sheet is 503 elements: 199 text elements, 113 line elements, 24 figure
segments, 8 washes, 46 scrims, and the rest ticks, flags and caps. Everything is
absolutely positioned; there is no flexbox anywhere, no CSS grid, no
`::before`/`::after`, no `clip-path`, no `mix-blend-mode`, no SVG, no
`text-shadow`, no `calc()`. Every tick mark, witness line and arrowhead is a real
child div, which is the honest construction — none of this leans on generated
content the engine does not have.

Property-level deltas from the mockup to shipping RCSS:

- **`background: repeating-linear-gradient(...)` → `decorator: repeating-linear-gradient(...)`.**
  Used for the field grid, both rulers' tick strips, the dashed extension lines,
  the figure centreline, and both hatch patterns. Same painting, different
  property name; `repeating-linear-gradient` is in the verified 6.2 decorator set.
- **`rgba()` alpha is 0–255 in RCSS, not 0–1.** Every ground in this file is
  written `rgba(5,12,30,.55)` and must ship as `rgba(5,12,30,140)`
  (.55·255 = 140, .72 → 184, .76 → 194, .80 → 204, .42 → 107, .45 → 115,
  .70 → 179, .18 → 46, .12 → 31). This is a mechanical rewrite but it is the
  single easiest thing to get silently wrong — an alpha of `0.55` parses as
  effectively transparent.
- **`filter: drop-shadow()` ×4 per text element → `font-effect: outline(1dp #000000)`.**
  The mockup stacks four 1px drop-shadows on `.t` to cage every glyph in black.
  `font-effect: outline` is in the verified 6.2 list, does the same job in one
  declaration, and is far cheaper: four stacked filters on 199 elements is 796
  offscreen passes per frame, and `font-effect` bakes into the font texture.
  **Ship the `font-effect`; the mockup's filter stack is a browser stand-in.**
- **`box-shadow: 0 0 0 1px` casings are real.** `box-shadow` is available in 6.2
  and this is the correct use of it: a 1px spread with no blur and no offset is a
  second border outside the first, which is exactly the black casing a light rule
  needs over bright terrain.
- **4-value `border-radius` shorthand** is used on the head
  (`14px 14px 12px 12px`) and the hands/feet. If 6.2's shorthand parser does not
  accept four values, the four longhands (`border-top-left-radius` etc.) do the
  same thing. [INFERENCE — not verified against the pinned source.]
- **`border: 1px solid #hex` → `border: 1dp #hex`** (RCSS has no line-style
  keyword). `box-sizing: border-box` is set on `.fig` and is available in 6.2 —
  it matters here, because the figure's segment coordinates are outer edges and
  content-box arithmetic would grow the 2px-edged critical arm 2px wider than its
  intact counterpart.
- **`transform: rotate()`** on the two leader obliques, the note-flag dogleg, the
  arrowheads and the key strike is available in 6.2 and needs no substitute.

New C++ producer work in `src/panels.cpp`, honestly enumerated. Most of it is
small; one item is not:

1. **`hud_vitals` needs to emit the wound flag set, not just cur/max.** It
   already computes the colour that carries bleeding/bitten/splint state into
   `label_hex` (`panels.cpp:872`) and then discards it. The dimension string needs
   `cur`, `max`, a fill fraction, and a flag enum on the same element. Small.
2. **Do not gate crit on a percentage.** The shipping encoding fires on
   `cur * 100 / max < 25` and `8*100/30 == 26`, so the fixture's critical arm
   never renders as critical. This design's `CRITICAL` comes from the wound
   flags, not from a ratio.
3. **Weather, wind and light level** — `weather_manager` has the weather string
   and the wind vector; `hud_topbar` currently reads only the temperature. The
   light-level helper already exists in `panels.cpp` and no producer calls it.
   Three trivial additions.
4. **An inventory read for the armament schedule.** Nothing in the HUD reads
   inventory today. TABLE 3 needs the wielded weapon's bash/cut/to-hit (the
   accessors exist), the best stowed firearm, its magazine count and its ammo
   type. New, but bounded.
5. **A held/worn light-source scan** for `FLASHLIGHT · LIT / 34%`. New.
6. **Target range, bearing and behaviour.** `4 tiles NE` and `moving` need the
   tracked monster's position delta from the avatar and its current activity.
   New, small.
7. **`1 tracking`** needs a pass over visible monsters asking which have the
   avatar as their target. New, small.
8. **The leader solver — this is the real cost.** The two obliques are anchored to
   *world* pixel positions (the avatar tile centre and the target tile centre) and
   dogleg to a shared shoulder column. A producer must emit screen-space pixel
   coordinates for the avatar and for every annotated contact, and then solve the
   geometry: for a −45° oblique the run length is `|Δy| · √2` and the shoulder x
   is **determined** by the contact's y, not chosen — so with more than one
   contact the solver must sort landings by y, guarantee they do not collide with
   the callout stack's 94px pitch, and reject or re-route any contact whose
   geometry would put its shoulder past the sheet's right margin. This is the
   same class of work as "a producer that emits per-part polar coordinates", and
   it is the one thing in this design that cannot be faked in RCSS.
9. **No new asset or font.** `Source Code Pro` is already bundled. `Consolas`
   here is the contract's stand-in and its advance width is **0.5498em against
   Source Code Pro's 0.6em**, so every string in this mockup is ~9% narrower than
   it will be in the game. Checked against the tightest cells: the callout head
   `CONTACT 1 — zombie brute` is 229px of Consolas and 245px of Source Code Pro
   in a 256px scrim; the title block's conditions row is 308px → 331px in a 376px
   cell; the longest revision row is 293px → 317px in a 368px cell; the key-plan
   row is 134px → 141px in a 150px box. All survive, with the key-plan grid the
   tightest at 9px of slack — if the plan ever needs a 13x13 chunk the
   `letter-spacing` must drop from 5px to 4px.

## Weaknesses

1. **The sheet is authored in absolute pixels and does not survive 1280x720.**
   Every one of the 503 elements carries literal `left`/`top`, so there is no
   reflow — only a re-layout or a uniform scale, and a uniform scale of 0.667
   takes the 10px caption tier to 6.7px, which is unreadable. Re-laying out is
   worse: the left column's ink runs y 74→968 = **894px** of content against
   **700px** of usable height at 720p, and the right column's 340px goes from
   17.7% of the screen width to **26.6%**. Something must be cut, and the honest
   cut is TABLE 1 — ten rows of stats that are never the reason you die — which
   means the design's density is a 1080p-and-up proposition. A drawing scales by
   changing sheet size, and this design has exactly one sheet size.
2. **The critical encoding's strongest channel is line weight, and line weight is
   1px.** `.fig.crit` is 2px where everything else is 1px. On a 1080p screen at
   normal viewing distance that is a genuine, readable difference; at 150% UI
   scale it becomes 3px vs 1.5px and reads *better*; but on a 4K display at
   `ui_scale 1.0` it is two hairlines and the difference collapses. The
   redundancies save it — the word `CRITICAL`, the 4px-vs-7px hatch pitch, the
   leadered `!`, and the fill length — but the channel the register is proudest
   of is the one that degrades first.
3. **For a protanope the amber/red pair is the weak axis, and two encodings are
   colour-only.** The palette is already green-free, and the important encodings
   are multiply-redundant: dimension **fill length** is unambiguous, hatch
   **pitch** (7px vs 4px) is a genuine non-colour channel, and the critical part
   is named. But the REVISION HISTORY's `lg-bad` / `lg-caut` / `lg-bad2` rows are
   distinguished by hue alone — six rows of 11px text where "the brute clawed
   you" and "you are getting thirsty" differ only in colour — and the key plan's
   amber `^` mission marker against the dim-blue `H` house glyph is likewise
   hue-only. Both are fixable (a leading glyph per row; a boxed mission cell) and
   both are unfixed here.
4. **DRAWING NOTES is a fixed 5-row block and twelve effects overflow it.** It is
   108px at 812–920, hemmed in by the stamp at y=826 and the title block at
   y=932, so it can grow to at most 120px = 6 rows. Twelve effects need 240px.
   The graceful behaviour is a drafting one — `+7 FURTHER NOTES ON SHEET 2` — but
   there is no sheet 2 and no paging key, so notes 7–12 would be genuinely
   unreachable. The figure's flags are worse: they are hand-placed at 72/98/124,
   there are two, and a third and fourth wound on the same limb would need a
   flag-stacking solver, because two note flags cannot leader to the same 16px
   arm segment without their arrowheads landing on top of each other.
5. **Three hostiles are visible and only one is annotated.** TABLE 2 says `3`;
   the drawing reticles and dimensions exactly one. That is a deliberate limit —
   annotate the tracked contact, tabulate the rest — but it means the field
   annotation, which is the design's best idea, is also the part that scales
   worst. Three simultaneous −45° leaders whose shoulder x is *determined* by
   each contact's y will cross whenever two contacts are within 94px of each
   other vertically, and the drawing has no convention for a crossed leader
   except to be wrong.
