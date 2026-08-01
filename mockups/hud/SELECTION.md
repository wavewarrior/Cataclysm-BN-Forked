# HUD redesign — 10 concepts, scored, top 3 selected

Ten alternative HUD designs for Cataclysm: Bright Nights, plus a faithful control
sample of the shipping HUD, all rendered through one harness at 1920x1080 over the
same tile-map backdrop (`_shared/world.png`, composited from real MSX++UnDeadPeople
sprites) with the same canonical game state (`_shared/CONTRACT.md`).

- Mockups: `mockups/hud/NN-slug.html` — self-contained, no JS, no external assets
- Specs: `mockups/hud/NN-slug.md` — thesis, register, layout, data mapping,
  occlusion arithmetic, motion, RCSS feasibility, weaknesses
- Renders: `mockups/hud/_shots/NN-slug.png`, contact sheet `_shots/_contact-sheet.png`
- Research digests: `mockups/hud/_shared/research-{roguelike,diegetic,infodesign,aesthetic}.md`

## Harness gate — all 11 pass

Every file: `#screen` exactly 1920x1080, exactly one `<style>` block, zero
`<script>`, zero classes used in markup but undefined in that file's own
stylesheet (this last check exists because the design agents shared a Python
kernel and one design was briefly written with a sibling's stylesheet).

## Measured occlusion

Contract rule: opaque and near-opaque at full weight, translucent scrims at half
weight, over 1920x1080 = 2,073,600 px². Every figure below was measured from the
rendered DOM by the design's own spec, with arithmetic shown there.

| # | Design | Occlusion | vs baseline |
|---|---|---|---|
| 00 | **Current HUD (control)** | **31.02%** | — |
| 01 | Corner Triad | 9.07% | −21.95 pp |
| 02 | Anatomical Triage | 20.47% | −10.55 pp |
| 03 | Instrument Cluster | 25.64% | −5.38 pp |
| 04 | Terminal Phosphor | 28.80% | −2.22 pp |
| 05 | Field Manual | 22.20% | −8.82 pp |
| 06 | Radial Orbit | 14.63% | −16.39 pp |
| 07 | Blueprint Schematic | 16.24% | −14.78 pp |
| 08 | Ambient / Proprioception | 11.2% resting / 32.3% revealed | −19.8 / **+1.3** |
| 09 | Teletext Brutalist | 24.41% | −6.61 pp |
| 10 | Sparkline Console | 15.98% | −15.04 pp |

The baseline's own 31.02% was itself a finding: the contract had estimated 26%.

## Scoring

Six axes, 0–5. Glanceability and coherence are doubled because they are what a HUD
is *for* and what the current one demonstrably fails at
(`plans/hud-coherence.md`); robustness and feasibility carry 1.5 because
accessibility and cost are not negotiable; and **character carries 1.5 because the
brief asked for creative and diverse designs** — a HUD that is correct and
forgettable does not satisfy that brief.

> **Correction.** The first version of this table weighted character at ×1 — the
> lowest tier — on a brief that explicitly asked for creativity and diversity. That
> was wrong, and it produced a top 3 containing two designs (04 and 09) in the same
> austere text-grid register, wasting a slot. Both the weight and the selection rule
> below are corrected.

| # | Design | Register | Glance ×2 | Coherence ×2 | Robust ×1.5 | Complete ×1 | Feasible ×1.5 | Character ×1.5 | **Total** |
|---|---|---|---|---|---|---|---|---|---|
| 04 | Terminal Phosphor | monochrome phosphor terminal | 5 | 5 | 5 | 5 | 5 | 4 | **46.0** |
| 02 | Anatomical Triage | clinical instrument | 5 | 5 | 3 | 5 | 3 | 5 | **41.5** |
| 09 | Teletext Brutalist | austere text grid | 5 | 5 | 4 | 5 | 5 | 2 | **41.5** |
| 03 | Instrument Cluster | analog machined hardware | 4 | 5 | 4 | 4 | 2 | 5 | 38.5 |
| 05 | Field Manual | diegetic paper | 3 | 5 | 3 | 4 | 3 | 5 | 36.5 |
| 10 | Sparkline Console | data-visualisation console | 4 | 5 | 3 | 5 | 2 | 4 | 36.5 |
| 01 | Corner Triad | minimal perimeter typography | 3 | 4 | 3 | 5 | 5 | 3 | 35.5 |
| 07 | Blueprint Schematic | technical drawing | 3 | 4 | 3 | 5 | 3 | 4 | 34.0 |
| 06 | Radial Orbit | player-centred radial | 4 | 4 | 2 | 4 | 2 | 5 | 33.5 |
| 08 | Ambient / Proprioception | near-zero diegetic | 2 | 3 | 2 | 4 | 2 | 4 | 26.0 |

### The selection rule is set-selection, not top-N

Picking three designs is choosing a *portfolio*, so two entries in the same register
waste a slot. Two consequences:

1. **09 is strictly dominated by 04.** Compare them axis by axis: glance 5=5,
   coherence 5=5, robust 5>4, complete 5=5, feasible 5=5, character 4>2. 04 is equal
   or better on every one. They also share a register — austere limited-palette text
   on opaque dark ground. 09 therefore cannot earn a slot beside 04 at any weighting;
   it is the accessibility runner-up, not a third option.
2. **The third slot goes to the highest scorer in an unrepresented register.** With
   04 (phosphor terminal) and 02 (clinical instrument) taken, that is 10 Sparkline
   Console at 36.5 — data-visualisation, and the only design that adds *history*, a
   dimension the game has never had. 03 Instrument Cluster scores 2 points higher at
   38.5 but sits in the adjacent instrument-panel family as 02, and carries the
   joint-lowest feasibility plus a confirmed log-row wrap collision.

Axis definitions, and the measured evidence behind the low scores:

- **Glance** — does "the left arm is dying" hit you before you read anything?
  04 inverts the whole L-arm row and puts `!! CRITICAL · BLEEDING · BITTEN` 0 px
  under the bar, against 891 px separation in the shipping HUD. 08 scores 2
  because the six exact values only exist while a key is held.
- **Coherence** — does it read as one object? The shipping HUD's central failure is
  that it reads as six unrelated boxes. 08 scores 3 because its revealed overlay is
  a right-edge dock by another name.
- **Robust** — colourblind safety, contrast over lit terrain, and behaviour at
  1280x720 and at 12 effects instead of 5. 06 scores 2 on measurement: its needs
  ring is 1.08:1 luminance (hue-only) at 6 px stroke, and at 720p it computes to
  32.9% occlusion — worse than the HUD it replaces. 08 scores 2 because its injury
  edge-bleed is 30% *darker* than the world, so it reads as shadow to a protan or
  deutan player, and its revealed panel has 5 px of vertical headroom.
- **Feasible** — RCSS-buildable, and what it costs in `src/panels.cpp`. 10 scores 2
  because history does not exist in the game: it needs a ring buffer per tracked
  metric, owned somewhere, ticked somewhere, and saved. 06 needs a per-part polar
  producer that does not exist. 03 needs six new producers and renders 613 divs.

## Top 3 — three distinct registers

### 04 — Terminal Phosphor (46.0) · monochrome phosphor terminal
One amber hue, one 192x54 character grid, box-drawing frames, luminance-only
encoding. Wins outright on the two axes nothing else does: it is **fully
colourblind-proof by construction** (the six-token ladder converts to greyscale in
8-bit steps of 35/48/47/53/46 — no pair can collapse), and it is **the cheapest to
ship** — the frames are real box glyphs, and the bundled `SourceCodePro-Regular.ttf`
was verified to carry 128/128 of U+2500–257F and all seven eighth-blocks, so zero
new assets. It is also the most coherent thing in the set: one grid and one hue
*is* the shared frame grammar `hud-coherence.md` says the HUD lacks. Cost: the
highest occlusion at 28.80%, opaque panels, and a committed retro register.

### 02 — Anatomical Triage (41.5) · clinical instrument
The HUD is a body. Cataclysm's signature mechanic becomes its signature visual: a
CSS-drawn anatomical plate with six independently-filled parts, triage colour
coding, an ECG stamina trace, a clinical observation chart and a wound/medication
list. Best glanceability in the set by the strongest test — a viewer with no legend
points at the injured limb and is right. Measured cohesion: 67.5 px from the wound
ring to its numeric, 211.9 px to the callout, against 891 px in the shipping HUD.
One 384 px right column (exactly 20% of width, exactly 48 text cells), 20.47%.
Cost: triage colour is hue-led, the panel has 5 px of vertical headroom before 12
effects eat the log, and the body plate needs a new producer.

### 10 — Sparkline Console (36.5) · data-visualisation console
The Tufte answer, and the only design that beats the current HUD on the current
HUD's own terms: **94 fields against the baseline's 37, in 15.98% against 31.02%** —
4.93x the fields per percent of screen, with 69.81% of the frame left as one
contiguous untouched block. It adds the dimension the game has never had: every
changing value carries a 12-turn sparkline with a reference band, so the player sees
"stamina 59% *and falling 300/turn*" and "the left arm didn't just get low, it
collapsed in two turns at -6/turn". Six small multiples on a shared scale for the
body. Cost: it assumes fluency, its 11 px type sits exactly at the legibility floor
with zero margin, and history is a new subsystem — a ring buffer per tracked metric,
ticked and saved.

## What would change this ranking

**Weighting rigour over art direction.** 09 Teletext Brutalist ties 02 on points at
41.5 and is the **accessibility and cost runner-up** — maximally unambiguous, and
grep-verified to use no gradient, glow, `border-radius`, shadow, `opacity` or
animation at all, making it the simplest possible port. It is excluded from the top
3 on dominance, not on taste: 04 equals or beats it on all six axes and occupies the
same register. If you want the austere option, 04 *is* that option.

**Weighting a shipped-object feel over portfolio spread.** 03 Instrument Cluster
scores 38.5 — above 10 — and is the most convincingly *designed object* here, with a
real conic-gradient dial, annunciator lamps and a paper-tape log, exploiting the
game's existing vehicle-HUD fiction. It was set aside only because it shares the
instrument-panel family with 02. Swap it in for 10 if a bottom binnacle suits the
game better than a data strip; it costs six new producers, renders 613 divs, and has
a confirmed log-row wrap collision to fix.

**Wanting maximum character.** 05 Field Manual (36.5) is the most charming thing in
the set and has the strongest single-object coherence anywhere here — one weathered
paper sheet, ink stamps, hand tallies for ammo, a pencil-sketch overmap. It trades
glanceability (you *read* it) and inverts contrast against a bright world.

## Defects found in the shipping HUD

Building the control sample measured the current HUD rather than describing it, and
turned up real bugs, all with file:line evidence in `00-current-baseline.md`:

1. **The critical-limb state is dead at this fixture.** `vbar_rml` computes integer
   `o.cur * 100 / o.max` and gates crit on `pct < 25` (`panels.cpp:795-796`).
   8/30 = 26, so a left arm at 27% health with bleeding and a bite never turns red.
   `hud_anim`'s `is_critical` misses identically (0.267 vs 0.25).
2. **Bleeding and bitten never reach the vitals panel.** `hud_vitals` computes
   `limb_color` into `label_hex` at `panels.cpp:872` and never uses it. The words
   only appear 891 px away in the bottom bar's grey effects text.
   `bodygraph_bp_color` (`panels.cpp:2011-2053`) already returns exactly the colours
   needed and is never consulted by the HUD path.
3. **The target HP bar is a permanently empty trough.** `.tbar-fill` is a `<span>`
   with `height:100%` and no `display`, so RmlUi's inline default collapses it.
   `.veh-fuel-fill` is the identical construction *with* `display:block`.
4. **The hotbar renders off-screen.** It runs 6.34 px past the bottom of the screen
   and overlaps the bottom bar by 6.36 px, from mixing percentage and cell geometry.
5. **The hotbar cannot show availability.** `hud_hotbar( avatar & )` ignores its
   argument (`panels.cpp:1282`), and unbound actions render the literal string
   `[Unbound globally!]` — three of nine on a default keymap, computing to 1554 px
   of text inside a 1520 px box.
6. **45% of the dock is empty.** The log well is 752 px of client height holding
   267 px of message. Its age-fade ramp needs 14 rows to reach its floor and only
   ever gets 6; `.log-fade-top` paints `hud-bar-bg` over `hud-bar-bg` and is
   invisible by construction; the scrollbar never instantiates.
7. **The MINIMAP header collides with the map.** Both are inline, so the title
   shares a line box with the map's top row and its `border-bottom` strikes through
   it. The LOG header escapes only because `.dock-flex` blockifies it.

These are independent of which redesign is chosen.
