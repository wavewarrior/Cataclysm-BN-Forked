# 05 — Field Manual

## Thesis

Every other HUD in this comparison asks the player to believe that a man with a
fire axe and a dying flashlight is also wearing a heads-up display. This one
doesn't. The interface is a page torn out of a salvaged army field manual,
overprinted as a status form, taped to the inside of the screen and filled in by
hand — typed where the form was typed, pencilled where Nigel pencilled it, and
stamped in red where something has gone badly wrong. Nothing glows, nothing is a
bar, nothing is a technology the apocalypse still has. The argument is that
**the diegetic frame is the only unifying device that can cover an entire HUD
without being invented for the purpose**: `plans/hud-coherence.md` diagnoses the
current HUD as one ornate runic frame, two hairlines and three bare regions, and
no amount of token-tidying makes those four treatments read as one object. A
sheet of paper is one object by definition. What it trades away is chromatic
bandwidth — a printed form has two inks and a pencil, so there is no green for
"healthy" and no amber for "caution", and severity has to be carried by *where a
tick falls in a printed scale* rather than by hue. It also trades away the
comfortable dark-on-dark register: this is the only design here that inverts to
dark-on-light, which is a real cost at 3 a.m. and a real gain for legibility over
bright grass.

## Register

Weathered / found-media (`research-aesthetic.md`) crossed with its mil-spec
field-manual notes: DIN-ish stencil headings, preprinted form rules, ink stamps,
mimeograph-era typewriter body, water damage, hand annotation. Three marking
media, no more — and that constraint is the register.

| token | hex / value | role |
|---|---|---|
| `paper-hi` | `#efe3c4` | sheet highlight, top-left, where light falls across it |
| `paper` | `#e3d7b8` | the substrate — everything is printed *on* this |
| `paper-mid` | `#d8c9a4` | shade along the free (right) edge, sells the curl |
| `stain` | `rgba(185,154,104,.16–.30)` | water-damage bloom body |
| `tide` | `rgba(150,116,68,.20–.36)` | the darker ring at a stain's edge — the actual tell |
| `foxing` | `rgba(120,88,48,.20–.30)` | four small age spots |
| `ink` | `#221a11` | typewriter ink: typed values, part names, log body |
| `ink-80` | `rgba(34,26,17,.80)` | preprinted section headings, hard checkbox rules |
| `ink-62` | `rgba(34,26,17,.62)` | preprinted field labels, column heads |
| `ink-44` | `rgba(34,26,17,.44)` | soft checkbox rules, band footnote |
| `ink-rule` | `rgba(34,26,17,.30)` | preprinted horizontal rules, dotted leaders |
| `ink-faint` | `rgba(34,26,17,.13)` | form row hairlines, the log's ruled lines |
| `red` | `#9c2a1c` | correction / rubber-stamp ink: critical, harm, void |
| `red-70` | `rgba(156,42,28,.70)` | stamp borders, hand-drawn rings, weak-pen "dimmer" tier |
| `red-50` | `rgba(156,42,28,.50)` | the second ring pass, index-card header rule |
| `pencil` | `#4e5450` | graphite: hand ticks, tallies, annotations, timestamps |
| `pencil-70` | `rgba(78,84,80,.70)` | hand underlines, leader lines, margin bars |
| `pencil-45` | `rgba(78,84,80,.45)` | empty map cells, graph-paper grid |
| `tape` | `rgba(236,232,214,.34)` + `backdrop-filter: blur(1.2px)` | translucent tape over the world |
| `card` | `#ded2b2` → `#d4c6a2` | the index card's cooler, harder stock |

Measured contrast on `paper` (`#e3d7b8`, relative luminance 0.684): ink **12.0:1**,
red **5.3:1**, pencil **5.4:1**. All three clear WCAG AA at body size. Because the
paper is fully opaque, none of this depends on what tile is underneath — which is
the whole point of choosing an opaque plate over a scrim on a backdrop this noisy.

**Type stack** — three faces, one per medium, which is what makes the media
legible as media:

- *Preprinted form furniture* — `Franklin Gothic Medium` (stand-in for a DIN /
  stencil grotesque): 7.5 / 8 / 8.5 / 10 / 11.5 / 15.5 px, tracked 1.3–3.2 px, all caps.
- *Typed entries* — `Courier New` (a real typewriter face, and on the contract's
  safe list): 9 / 9.5 / 10.5 / 11 / 11.5 / 12 / 12.5 / 13 px, tabular figures.
- *Hand annotation* — `Segoe Script`, falling back `Ink Free` → `Gabriola` →
  `Comic Sans MS`: 11.5 / 12 / 12.5 / 13 / 14 px.

Fifteen sizes is more than an information-design purist allows, and the defence is
specific rather than aesthetic: a printed form genuinely mixes 6–8 pt press type
with 10–12 pt typed entries and a 14 pt hand, and every size here resolves to one
of exactly **three media** at one of exactly **five roles** (masthead, heading,
label, value, footnote). The reader never has to learn a size; they read the medium.

**Frame treatment.** There is no frame. The paper's own edge is the frame, and it
is made to read as an edge by five stacked cues rather than a border: a 1 px
`rgba(52,38,22,.55)` outline, a 34 px soft drop shadow offset 16 px right, a
2 px hard contact shadow, a 20 px inset warm shade along the free edge (page
curl), and three pieces of translucent tape crossing from paper onto world. Add a
−0.28° rotation, two horizontal pocket creases, three punched binder holes with
the world visible through them, a thumb smudge, and four water blooms with tide
rings. The index card is the same grammar in a cooler, harder stock at a −0.5°
rotation with its own two tape tabs. This is the one frame device that covers
100% of the HUD — which is exactly what the current HUD's 1-ornate / 2-hairline /
3-bare split does not.

## Layout

All numbers measured from the rendered document at 1920×1080, not intended.

**Object 1 — the field-manual page.** Unrotated box `left −16, top 8, 416 × 1064`,
rotated −0.28°; on-screen extent **x 0–403, y 7–1073**. Its left edge runs off the
viewport, which is what "pinned along one edge" means — the taped free edge is the
right one. Padding `18 / 14 / 14 / 42`; the fat 42 px left pad is the binder margin
the punch holes sit in. Content column **x 26–386 (360 wide), y 25–1059 (1034 tall)**,
of which 1024 is used.

| block | y | notes |
|---|---|---|
| masthead | 25–65 | `DEPT. OF THE ARMY · FM 21-76` / `SHEET 1 OF 1`, `FIELD RECORD` at 15.5 px tracked 3 px, closed by a 2 px + 1 px double rule |
| identity | 70–140 | 4 rows × 17, printed labels + dotted leaders + handwritten fills, width capped at 270 to leave the stamp its margin |
| round date stamp | x 306–382, y 22–110 | 76 px ⌀, double ring, rotated −11°, red at .78 with striped ink dropout |
| conditions | 145–181 | 2 rows, weather and light |
| **S1 triage** | 189–411 | heading 189–205; form 203–411 = col-head 18 + six rows × 25 + inserted annotation 19 + band footnote 13 |
| — hand rings | x 26–291, y 267–306 | two passes round the L ARM row, −1.1° and +1.3° |
| — `PRIORITY` stamp | x 293–399, y 260–310 | rotated −8°, 15 px clear of `8/30`, 4 px shy of the paper edge |
| **S2 effects** | 419–469 | two lines, five numbered entries in priority order |
| **S3 capacity** | 477–544 | three lines |
| **S4 reserves** | 552–604 | stamina strike-and-rewrite, four-box needs checklist |
| **S5 armament** | 612–679 | in-hand, stowed, pencil five-bar ammo tally |
| **S6 contact** | 687–754 | hostile, condition + `SAFE MODE ON` stamp box, threat tally |
| **S7 sketch** | 762–901 | 11×11 graph-paper map at x 27–149; legend + hand note at x 162–388 |
| **S8 running log** | 909–1049 | six ruled entries at 20 px pitch, newest at the pencil tip |

Column geometry of the triage form, relative to the content column: `PART` 0–62,
the five-box ordinal scale 62–193 (5 cells × 25, 14 px box centred), `CUR/MAX`
193–253 right-aligned, `FLAG` 253–289. That leaves 289–360 for the stamp to land
in, which is why it never covers a number.

**Object 2 — the index card.** `left 686, top 998, 584 × 58`, rotated −0.5°,
bottom edge 24 px clear of the screen floor. Header rule in red at 13 px, then
nine 62 px cells at y 1019–1056. Centred on x = 978, i.e. 34 px right of the
player tile at x 944 — close enough to be in the same glance as the avatar,
low enough to be out of the way. Two tape tabs at `(670,983)` and `(1239,1041)`.

Everything else — the whole right and centre of the screen, **x 403–1920 unbroken
top to bottom** — is world. There is no top strip and no bottom strip, so unlike
the shipping HUD the play area is never sliced horizontally.

## Data mapping

Every fixture field. `numeric` = the exact figure is printed. `ordinal` = position
of a mark in a printed scale. `hand` = written in pencil or red by the character.

| fixture field | where | encoding |
|---|---|---|
| Name `Nigel Ashby` | S0 identity, `NAME` row | hand (pencil), on a dotted leader |
| Profession `Survivor` | S0 identity, `ROLE` row | hand |
| Day `14` | round rubber stamp, top right | glyph — `DAY 014`, red stamp ink |
| Time `21:47` | S0 identity, `TIME` row | numeric, typed in ink inside a handwritten line |
| Season `Autumn` | S0 identity, `TIME` row | hand — `autumn` beside the time |
| Place `Cheswick — residential` | S0 identity, `POSN` row | hand — abbreviated `Cheswick — resid'l`, because a hand abbreviates |
| Weather `Light drizzle` | S0 conditions, `WX` | typed |
| Temperature `8 °C` | S0 conditions, `WX` | numeric, typed |
| Wind `NE 12 km/h` | S0 conditions, `WX` | typed |
| Light level `12%` moonlight | S0 conditions, `LT` | numeric, typed |
| Flashlight lit at `34%` | S0 conditions, `LT` | `torch` typed + `34% chg` in hand. *Lit* is ambient: an unlit torch is not a light source and would not be on the light line at all |
| STR `10`, DEX `12`, INT `9`, PER `11` | S3 line 1 | numeric, typed, tabular |
| Speed `96`, Focus `84` | S3 line 2 (`SPD`, `FOC`) | numeric |
| Pain `12` | S3 line 2, and again as effect 5 | numeric. A form repeats; both are the same value |
| Morale `-3` | S3 line 2 | numeric, in red — the only red in S3, because it is the only negative |
| Encumbrance torso `12`, arms `4` | S3 line 3 | numeric |
| Move mode `walking` | S3 line 3 (`GAIT`) | typed word |
| Noise made `8` | S3 line 3 | numeric |
| Head `20 / 24` | S1 row 1 | numeric (`20/24`) **+** ordinal (tick in `MIN`) |
| Torso `45 / 60` | S1 row 2 | numeric **+** ordinal (`MOD`) |
| **L arm `8 / 30`, critical, bleeding, bitten** | S1 row 3 | numeric (`8/30`) **+** ordinal (red tick in `CRT`) **+** two hand-drawn red rings round the row **+** red `!` in the `FLAG` column **+** a `PRIORITY` rubber stamp **+** an inserted red annotation line `↳ bleeding · bitten — tourniquet, then bandage`. Six redundant cues, only one of them chromatic |
| R arm `30 / 30` | S1 row 4 | numeric **+** ordinal (`FIT`) |
| L leg `22 / 28` | S1 row 5 | numeric **+** ordinal (`MOD`) |
| R leg `28 / 28` | S1 row 6 | numeric **+** ordinal (`FIT`) |
| band thresholds | S1 footnote | printed, so the ordinal scale is self-documenting: FIT 95+ · MIN 80–94 · MOD 60–79 · SEV 30–59 · CRT <30 (% of max) |
| Stamina `5100 / 8700` | S4 row 1 | the printed `8700` struck out in red, `5100` written beside it in pencil, `OF 8700` typed small. A depleted resource as a correction, not a fill |
| Hunger `Peckish` | S4 row 2 | printed checkbox + hand tick |
| Thirst `Thirsty` | S4 row 2 | printed checkbox + hand tick |
| Fatigue `Tired` | S4 row 2 | printed checkbox + hand tick |
| Warmth `Chilly` | S4 row 2 | printed checkbox + hand tick |
| Effects 1 `Bleeding (L arm)`, 2 `Bitten (L arm)` | S2 line 1 | typed, numbered, in red — these are the L-arm wounds |
| Effects 3 `Soaked`, 4 `Tired`, 5 `Pain 12` | S2 line 2 | typed, numbered, in ink. Numbering preserves the fixture's priority order |
| Wielded `fire axe` | S5 `IN HAND` | typed |
| `32 bash`, `0 cut`, `to-hit +1` | S5 `IN HAND` | numeric, typed small |
| Sidearm `Glock 19`, `9mm JHP` | S5 `STOWED` | typed |
| Ammo `12 / 17` | S5 `ROUNDS` | pencil five-bar gate tally — two full gates + two singles — then `= 12 of 17`. Hand count and total, which is what a real tally sheet carries |
| Target `zombie brute` | S6 `HOSTILE` | typed, with a hand underline |
| Target HP `62%` | S6 `CONDITION` | hand — `abt. 62%`, in pencil, because a survivor estimates a monster's condition, he does not read it |
| Target `4 tiles NE`, `moving` | S6 `HOSTILE` | hand |
| Threats `3 hostiles visible`, `1 tracking` | S6 `TALLY` | numeric, typed |
| `SAFE MODE ON` | S6, right of the condition row | glyph — small double-ruled ink stamp box, rotated −1.4° |
| Log 21:46 hit the brute (neutral) | S8 line 1 | ink, oldest, faded to .56 |
| Log 21:46 claws your left arm (bad) | S8 line 2 | red ink, .66 |
| Log 21:47 left arm bleeding (bad) | S8 line 3 | red ink, .74 |
| Log 21:47 sharp pain (bad, dimmer) | S8 line 4 | red ink at 70% strength — the same pen, applied weakly. Kept independent of the age ladder so "dimmer" survives being recent |
| Log 21:47 shambler groans (caution) | S8 line 5 | ink + a pencil **margin bar** in the gutter — caution has no ink of its own, so it is positional |
| Log 21:47 getting thirsty (caution, **newest**) | S8 line 6 | ink at full strength and bold, margin bar, a pencil caret at the tip, and a hand underline. Newest is the darkest and the only underlined line |
| Log timestamps | S8, 44 px gutter | hand, `21.46` / `21.47` in the character's own writing |
| Log recency | S8 | ambient — six opacity rungs .56 / .66 / .74 / .80 / .90 / 1.0. Ink ages |
| Minimap 11×11 | S7, 121 × 121 | pencil graph-paper grid; `.` at 45% pencil, `F` pencil bold, `H` ink, `@` red |
| Mission marker NE | S7, row 1 col 9 | a **hand-drawn red cross** (two rotated strokes, not a glyph) with a pencil leader line out to a script note `4 blocks NE — brute in the way` |
| Other two POI markers | S7 | pencil `x`, legended `POI, UNSEEN` — demoted below the objective, which is the only thing crossed |
| Hotbar slots 2–9 | index card | `key → action`: `r` RELOAD, `'` RUN, `e` EXAMINE, `p` PICK UP, `c` CRAFT, `i` INVENTORY, `t` THROW, `5` WAIT, each in a printed key box under its slot number |
| Hotbar slot 1 `f` Fire, **unavailable** | index card, cell 1 | red-bordered key box, **double red diagonal strike**, label in red, plus an explicit red header note `SLOT 1 VOID — NO FIREARM WIELDED`. Struck through, exactly as you would strike a line on a key card |
| Colour semantics: red = harm | throughout | red is correction/stamp ink: critical part, bad log lines, negative morale, void slot, struck figures |
| Colour semantics: yellow/amber = caution | S8 | **DROPPED as a hue** — a printed form has no amber. Caution is a pencil margin bar (position), and needs are ticked checkboxes (position) |
| Colour semantics: green = healthy | S1 | **DROPPED as a hue** — there is no green ink. "Healthy" is a tick in the `FIT` column, i.e. encoded by position in an ordinal scale. This is the design's biggest deliberate trade, and it is why the exact `cur/max` is printed on every row rather than only on the wounded ones |

Two fields are deliberately dropped, both of them *colours* rather than data: no
value in the fixture is unrecoverable from the mockup.

## Occlusion budget

Play area: 1920 × 1080 = **2,073,600 px²**.

**Opaque**

| element | arithmetic | px² |
|---|---|---|
| manual page, unrotated box | 416 × 1064 | 442,624 |
| less the strip left of x = 0 | −16 × 1064 | −17,024 |
| less three punched holes (true windows — the world is visible through them) | −3 × π × 8.5² | −681 |
| **page, visible** | | **424,919** |
| index card | 584 × 58 | 33,872 |
| **opaque subtotal** | | **458,791** |

The −0.28° rotation shears the page into a parallelogram of identical area, so the
off-screen strip is 16 px wide on average and the subtraction is exact to first
order.

**Translucent, counted at half weight** — only the part of each tape strip that is
*not* over paper occludes anything the paper wasn't already covering.

| element | over-world portion | px² |
|---|---|---|
| tape s1, 74 × 29 at x 366 | (440−400) → 34 × 29 | 986 |
| tape s2, 74 × 29 at x 362 | 36 × 29 | 1,044 |
| tape s3, 74 × 29 at x 366 | 34 × 29 | 986 |
| tape k1, 46 × 26 at (672,986) | 1,196 − 448 over card | 748 |
| tape k2, 46 × 26 at (1240,1044) | 1,196 − 360 over card | 836 |
| sum | | 4,600 |
| **half weight** | 4,600 / 2 | **2,300** |

**Total = 458,791 + 2,300 = 461,091 px² → 461,091 / 2,073,600 = 22.2%.**

Inside the 18–24% band the thesis targets. Against the measured shipping baseline
of **31.0%** (TERMX 240 / TERMY 67, a 352 px dock plus two 1568 px strips) this
gives back **8.8 percentage points**, a 28% relative reduction — roughly 183,000 px²
of tile map. The shape of what is given back matters as much as the amount: the
baseline spends its budget on two full-width horizontal strips that cut the world
into a letterboxed band, whereas this design spends all of it in one vertical
column, so the remaining play area is a single unbroken **1517 × 1080** rectangle
(less the 584 × 58 card at the bottom). In terminal cells the sidebar goes from 44
to 52 — eight cells of map width surrendered, the full 67 rows of map height
recovered. Net visible world area increases.

## Motion

Three triggers. Nothing loops, nothing breathes, nothing glows, and the substrate
never moves — a sheet of paper on a table is the one thing on this screen that is
*definitionally* still, and if the paper drifted the object would stop being an
object. All motion is a **mark arriving**, never a plate sliding.

1. **A value is rewritten** — 120 ms `opacity 0 → 1`, `quad_out`, on the new mark
   only (a pencil arriving). The red strike over a superseded figure draws in as
   `width: 0 → 34px`, 140 ms `quad_out`: a stroke being pulled across a number,
   not a fade.
2. **A part crosses into `CRT`** — the only impact moment in the design, and it
   fires at most once per limb per wound state. Ring 1 `opacity 0 → .70` over
   160 ms; ring 2 delayed 140 ms then the same, so it reads as two passes of the
   same pen. The `PRIORITY` stamp then lands in 90 ms: `scale(1.06) → scale(1)`
   with `opacity 0 → .74`, `quad_out`. Ninety milliseconds is a rubber stamp
   hitting paper; anything slower is a decal fading in.
3. **A new log entry** — the pencil caret and the hand underline translate down
   one 20 px line in 110 ms `quad_out`, and each demoted line steps one opacity
   rung over 400 ms `sine_out`. The entry *text* does not slide; the page is not
   scrolling, the pen is moving down it.

Never animated: the paper, the stains, the creases, the tape, the graph grid, the
preprinted rules, the map, the stamps once landed. At most two of the three
triggers can overlap, total budget ≤ 160 ms of feedback plus one 400 ms ambient
fade per turn — inside `research-infodesign.md` §7 (≤150 ms feedback, ambient
slow, 2–3 concurrent max). At the screenshot moment every animation is at its
terminal state, which is what the mockup shows.

## Feasibility in RCSS

**Not directly buildable as written, with the replacement:**

1. **`var()` custom properties.** RCSS 6.2 has none. The project's mechanism is
   textual `{{token}}` substitution (`data/gui/sidebar_hud.rcss:17`, `:51`, `:106`).
   The blocker is specific and documented in that very file at **`:361-362`** —
   *"token substitution is textual, so a gradient stop cannot reference the
   token"*. This design's entire substrate is ~24 gradient stops. So either the
   paper hexes become literals duplicated across decorators — which is exactly the
   drift `plans/hud-coherence.md` complains about — or the substitution pass is
   extended to run inside `decorator:` values. The second is the right fix, is a
   small change to one pass, and every future decorator-heavy panel needs it.
2. **`background-image` → `decorator:` layers.** RCSS has no `background-image`.
   Mechanical translation: `.grain`'s four `repeating-linear-gradient`s and
   `.stains`' eight `radial-gradient`s become stacked `decorator` layers, which is
   the documented way to build ornament without pseudo-elements. Note
   `background-color` **replaces** rather than composites
   (`sidebar_hud.rcss:503-504`), so the base paper colour must be the bottom
   decorator layer or an opaque `background-color` with every gradient above it —
   which is how the mockup is already built.
3. **`radial-gradient` with explicit ellipse radii** (`150px 108px at 82% 4%`).
   Supported in the 6.x decorator, but if a build only accepts `circle` each water
   bloom becomes two stacked circles. Low risk, named for honesty.
4. **`border-radius: 50%/58%`** — the two-value elliptical form, used for the
   hand-drawn rings. RCSS takes 1–4 lengths only. Replacement: drop the border
   entirely and paint an annulus,
   `decorator: radial-gradient(ellipse closest-side at 50% 50%, transparent 0 46%, {{ink-red-70}} 48% 52%, transparent 54%)`,
   on a plain rotated div. This is *better* than the mockup: a gradient annulus has
   a soft outer edge, which looks more like ink soaking into paper than a hard
   1-px border does.
5. **The punched holes are a cheat in the mockup.** They use
   `background-image: url(world.png)` at a negative offset, which only works
   because the backdrop here is a static PNG. The real construction is
   `mask-image` (confirmed available in 6.2) on the sheet: three
   `radial-gradient(circle closest-side …)` punches plus an opaque remainder.
   **Cost:** masking clips the element's own shadow, so the page's `box-shadow`
   must move to a sibling div rendered behind it at the same rect — one extra
   element. If that is judged not worth it, deleting the holes costs the design
   almost nothing (681 px² and one small storytelling beat).
6. **`:nth-child()` on `.gate b`** for the tally-stroke jitter. RmlUi supports it;
   if a build did not, four explicit classes do the same job.
7. **Named HTML entities** (`&mdash;`, `&middot;`, `&times;`, `&deg;`, `&nbsp;`).
   RML is XML-ish and only guarantees the XML five. The producer emits literal
   UTF-8 instead. Mechanical.

**Deliberately avoided so the port is honest:** no `::before` / `::after`, no
`content`, no `clip-path`, no `text-shadow`, no CSS grid, no SVG, no `calc()`, no
`text-decoration`. Every mark — every hand tick, tally stroke, strike-through,
ring, underline, caret, margin bar and map cross — is a real child element with
`transform: rotate()`. That is the single reason the markup is verbose, and it is
the reason this ports to RCSS without a rewrite. `box-shadow`, `backdrop-filter`
and `opacity` are all on the 6.2 available list; this would be the HUD's first use
of the first two, which currently uses none of them.

**Bundled fonts — a real asset requirement, not a cosmetic one:**

- **A script face is load-bearing.** `Segoe Script` is Windows-local, and RmlUi
  loads only bundled TTFs. If the hand annotations render in the typewriter face,
  the distinction between *what the form printed*, *what was typed into it* and
  *what Nigel scrawled on it* collapses and the design degrades into an ordinary
  table — the thesis dies. The game must bundle an OFL/Apache script face:
  **Caveat**, **Shadows Into Light** or **Reenie Beanie**, ~50–150 KB. Non-negotiable.
- **A typewriter face is strongly wanted.** `Courier New` → **Cousine** (Apache,
  Courier-metric) or **Nimbus Mono PS** (OFL). Falling back to the already-bundled
  **Source Code Pro** works and keeps the tabular figures, but it reads "code",
  not "typewriter", and costs real register.
- **The stencil face needs nothing new.** The preprinted furniture is all-caps and
  heavily tracked, so Source Code Pro Bold at the same letter-spacing is an
  acceptable substitute for `Franklin Gothic Medium`.

**New C++ producer work in `src/panels.cpp`:**

- **`hud_triage` replaces `hud_vitals`.** Per part it must emit name, `cur`, `max`,
  a **band index 0–4**, and per-part effect flags. The band is
  `pct = 100 * cur / max` → FIT ≥95 / MIN 80–94 / MOD 60–79 / SEV 30–59 / CRT <30.
  This deliberately differs from the shipping crit rule: integer `8*100/30 == 26`
  against a `pct < 25` test means the shipping HUD never flags this fixture's L arm
  at all, and my bands put 26 in `CRT`, which is the correct answer.
  **Per-part `bleeding` / `bitten` flags are not surfaced by the current vitals
  panel at all** and must be added — the character already carries them, nothing
  reads them.
- **`hud_log` age rungs.** One index 0–5 per line so RCSS can select
  `.a1`…`.a5` / `.now`, plus the existing severity so `.bad` / `.dim` / `.mark`
  can be applied. It already orders the lines, so this is an index, not a rewrite.
- **Ammo tally expansion.** `12` must become `⌊12/5⌋ = 2` full gates plus
  `12 mod 5 = 2` singles, emitted as child elements. Integer division in a producer
  loop; RCSS cannot do arithmetic.
- **Stamina as a pair.** `cur` and `max` must be separate values so the max can be
  printed-and-struck and the cur written beside it. Currently it is emitted as a
  bar fill fraction, which throws the pair away.
- **Target line as four fields** — name, condition %, `4 tiles NE`, `moving`. The
  distance-and-bearing string may need composing.
- **Hotbar availability flag.** Slot 1 needs an `unavailable` bit so `.cell.void`
  applies; the fixture requires that state be visible.
- **`hud_map` mission bit.** It already emits an 11×11 glyph grid; it needs one
  extra bit per cell, `is_mission_marker`, so the NE `^` becomes the red hand cross
  while the other two stay pencil `x`.

**Rect layout (`sidebar_hud_apply_rect`, `src/panels.cpp:1509`):** this design is
the *existing* left-sidebar case, which is the cheapest part of the whole port.
`SIDEBAR_POSITION == "left"` already exists (`:1518`) and
`panel_manager::get_width_left()` (`:1519`) already carves the map viewport with
the same width (`:1516-1517`), so widening the column cannot desynchronise the
world view. `#hud-dock` becomes the sheet; `#hud-topbar`, `#hud-botbar`,
`#hud-vitals` and `#hud-vehicle` are **deleted from the document** and their
content becomes Sections 2–6 — which removes the two `runic-edge-*` hairlines and
the one `runic-region` frame that `plans/hud-coherence.md` identifies as the
incoherence, and replaces all four treatments with one. `#hud-hotbar` stops being
`width: bar_width_pct` (`:1550`) and becomes a fixed 584 dp card at
`left: 686 dp`: three `SetProperty` calls. Finally, `runic_cfg()` is a global
singleton whose warm-brown `#a1885f` ink currently fights the teal chrome and
cannot be recoloured for the HUD alone — this design sets `runic-no-*` everywhere
and uses no runic frame at all, sidestepping the shared-singleton problem instead
of arguing with it.

## Weaknesses

1. **It does not reflow, and 1280×720 is where it breaks.** The page is a fixed
   416 px column: at 1280 wide that is 32.5% of the screen and the occlusion budget
   goes to ~31%, i.e. no better than the HUD it replaces. Worse, the content column
   needs 1024 px of height and 720 gives 674 — it overflows by roughly 350 px, and
   paper cannot scroll without stopping being paper. The honest fixes both cost
   something real: a second sheet (a two-page spread, doubling occlusion) or
   demoting Sections 3–7 to a "turn the page" state on a keypress, which
   contradicts the always-visible premise. This is the design's worst failure mode
   and it is structural, not a tuning problem.
2. **Red and pencil are the same colour to a colourblind player, and the ordinal
   scale is coarse.** Measured relative luminance: red `#9c2a1c` = 0.0880, pencil
   `#4e5450` = 0.0854 — a 1.03:1 ratio, so in greyscale or under protanopia they
   are *indistinguishable*. Ink separates cleanly from both (2.2:1), so
   printed-vs-marked survives, but red-annotation-vs-pencil-annotation does not.
   The design is built to absorb this — severity is carried by tick *position* in a
   five-column scale, and the critical row is additionally ringed, flagged,
   stamped and annotated — but a protanope reading the log will lose the
   bad-vs-caution distinction and has to fall back on the pencil margin bar. And
   the five-band scale is genuinely coarse: 45/60 and 22/28 both land in `MOD`, so
   the band alone cannot rank them. That is why the exact `cur/max` is printed on
   every row, not just the wounded ones — the ordinal scale is for the glance, the
   numbers are for the decision.
3. **Twelve effects overflow the form.** Section 2 is a fixed two-line block sized
   for five entries; twelve need four or five lines, and everything below shifts
   ~50 px into the 10 px of slack the page has left. A paper form running out of
   room is diegetically perfect and functionally bad. The realistic mitigation is
   capping the block at six with a typed `+6 MORE` and a keybind — which is
   progressive disclosure, and therefore a partial retreat from the design's own
   claim that everything is on one page.
4. **It is the brightest object on screen, and it inverts.** The paper sits at
   relative luminance 0.68 against a mid-dark tile map, and the renderer has an HDR
   bloom pass: a 416 × 1064 near-white plate will halate along its entire free edge
   and lift the world tiles next to it, exactly the failure `research-infodesign.md`
   §10 warns about (cap UI brightness at ~#e0e0e0; this stock is brighter). The
   mitigation is to drop the stock about two stops to ~`#d2c4a0` — ink contrast is
   still 9.6:1 — or exclude the RmlUi layer from the bloom source. Separately, and
   not fixable: this is the only dark-on-light HUD in the comparison set, so it is
   jarring to switch to, and a bright 22% plate at 3 a.m. in a dark room is a real
   comfort cost that every other design here simply does not pay.
