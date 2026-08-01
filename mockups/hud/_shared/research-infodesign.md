# Information Design Principles for Cataclysm-BN HUDs

## Patterns

### 1. Data-Ink Ratio & Sparklines — Edward Tufte
- **What it is:** Maximize the proportion of pixels that encode data; remove decoration and redundant elements.
- **Why it works:** Players absorb data-carrying signals instantly; non-data pixels create noise that fatigues peripheral vision during long turns.
- **Operationalisation:** Every HUD bar, number, icon, and colour must answer "does this change when data changes?" Cataclysm-BN: remove gridlines, decorative frames from non-primary regions (#hud-vitals, #hud-vehicle), collapse duplicate metrics. Sparklines (tiny inline bar charts) for resources over time consume <12dp width but communicate direction instantly. Small multiples of body-part bars at 8dp height each allow sub-5-fixation damage assessment.

### 2. Preattentive Attributes: Colour & Motion — Visual Perception Research
- **What it is:** Colour and motion register in <250ms without conscious attention; shape and orientation take longer.
- **Why it works:** Peripheral vision detects hue and flicker before foveal focus arrives; players need threat/resource status before eyes leave the play area.
- **Operationalisation:** Reserve colour *only* for state that must be grasped peripherally: health=red/green, stamina=yellow, hostile=red flash. Mana and secondary resources use shape (bar fill %) not hue. Motion animates only discrete state changes, never continuously. At 1080p, ensure colour targets 4:1 luminance contrast against teal HUD chrome (#2a5a50).

### 3. Glanceability & Saccade Windows — Eye Tracking, Nielsen Norman Group
- **What it is:** A fixation lasts 200–300ms; a glance is one fixation. Perceptual span is ±15 character positions (4–5 degrees).
- **Why it works:** Players hold gaze on the play area centre. Information >10 degrees from gaze centre requires a costly saccade; each saccade costs 30–120ms of suppressed vision.
- **Operationalisation:** Place critical data (health, threats, time, location) within ±3 degrees (roughly ±60px at 1080p from screen centre). Break HUD into 4–7 visual clusters max (Cowan's 4±1 chunks in working memory). Cataclysm-BN: topbar, vitals, dock, botbar, hotbar, vehicle (6 regions). Each cluster ≤3 distinct metrics at a glance.

### 4. Gestalt Grouping: Proximity, Similarity, Common Region — Cognitive Design
- **What it is:** Proximity (≤8dp gap), similar styling, or shared enclosure binds elements psychologically, overriding spatial distance.
- **Why it works:** The brain chunks related information; a single grouping cue is stronger than competing cues. Mixing three grouping strategies creates ambiguity.
- **Operationalisation:** Choose ONE binding per HUD section. #hud-vitals uses proximity (4dp inter-bar gap, 8dp section gap). #hud-dock uses common region (decorative border frame). #hud-topbar uses alignment + monospace grid. Consistency: all sections use the same spacing unit; mix of 3dp and 8dp reads as "unrelated."

### 5. Progressive Disclosure & Ambient vs. Explicit Encoding — UX Design Pattern
- **What it is:** Show critical information always (explicit), secondary data on hover/context (progressive), and status quo in background motion (ambient).
- **Why it works:** Explicit reduces cognitive load for common decisions; progressive reveals on-demand without clutter; ambient conveys direction without demanding focus.
- **Operationalisation:** Health = explicit (always visible <100ms). Armour = progressive (show detail on hover, icon always visible). Threats = ambient (minimap compass flicker when danger approaches, no modal). Cataclysm-BN: threat compass floats in dock, lit only when a monster enters view; log scrolls at rest and halts on player action.

### 6. Monospace Typography: Tabular Figures, Optical Size, Pixel Clarity — Game Typography
- **What it is:** Monospace fonts (Source Code Pro) with tabular numerals ensure columns align; pixel-perfect rendering at intended sizes (18dp, 16dp, 14dp).
- **Why it works:** Game stats are tables; misaligned columns confuse comparison. Monospace avoids readability cliff where sans-serif hinting fails below 12px.
- **Operationalisation:** Cataclysm-BN uses Source Code Pro exclusively. Type scale: 0.8em (labels: "ID", "COND"), 0.85em (headers: "Vitals"), 1em (body: "STR 10", log). Letter-spacing: 0dp (body), 1px (labels), 2dp (headers). At 1440p these render crisply; at 1080p ensure 1px = 1 physical pixel. Tabular figures on all metrics; "HP 100" and "HP 99" occupy identical width.

### 7. Animation Timing & Easing in Turn-Based Games — Motion Design
- **What it is:** In turn-based games, animation on every state change is *distracting* unless purposeful. Easing-out (fast→slow) feels responsive; linear/easing-in feels sluggish. Animations complete in <150ms to avoid stacking.
- **Why it works:** Humans accumulate fatigue from repetitive motion; 2000 turns = 2000 animations if every stat flicker animates. Ease-out satisfies the eye's need to see motion *arrive* and settle.
- **Operationalisation:** Animate health bar colour shift (200ms ease-out) when damage taken. Animate status effect icon arrival (120ms scale-in, ease-out) when effect applies. Do *not* animate log scroll every turn or stamina decay every tick. Animate only discrete state transitions. Max 2–3 simultaneous animations per turn; cap total animation time at 300ms per turn cycle.

### 8. Colorblind-Safe Encoding: Luminance-First Palettes & Redundant Cues — Game Accessibility
- **What it is:** Red-green vision deficiency (8% of males) makes red/green indistinguishable; rely on luminance contrast (brightness) first, colour second. Encode critical state with shape or position, not colour alone.
- **Why it works:** Luminance is perceived by all viewers, regardless of colour vision. Redundancy ensures meaning survives any colour-vision variant (protanopia, deuteranopia, tritanopia).
- **Operationalisation:** Health bar: shape (height, fill %) is primary; colour (red→yellow→green) reinforces. Threats: position in compass (8-directional grid) is primary; colour is secondary. Temperature: use ↑/↓ arrows, not "cold=blue". Test all palettes in monochrome (grayscale); if two states collapse, they fail. Minimum 3:1 luminance contrast between health bar and teal HUD chrome (#2a5a50, L=42); bars must read at L=65+.

### 9. Contrast & Readability Over Noisy Procedural Backgrounds — WCAG Accessibility
- **What it is:** Game tiles are high-contrast, procedurally varied, chaotic. Text drawn over this must maintain 4.5:1 contrast (WCAG AA) against a moving background impossible to measure statically.
- **Why it works:** Static contrast tests fail on procedural noise; UI must be readable over worst-case tile colours (black grass, bright concrete). Scrim (semi-transparent dark overlay) boosts contrast without covering the play area.
- **Operationalisation:** Log text: use #ffffff or #f0f0f0 with 1–2px text-outline shadow (#000000 at 50% opacity). Cataclysm-BN dock log: background-color #2a5a50 with 85% opacity creates a scrim; text reaches 5.5:1 even when grass beneath is #90ee90. Avoid mid-grey text (<3:1) over tiles. Measure contrast at 1080p and 1440p; if a metric drops below 3:1, enlarge font or darken scrim.

### 10. Bloom & HDR Readability — Renderer GPU Effects
- **What it is:** Bloom pushes bright UI elements into neighbouring pixels, smearing and reducing contrast. Extreme white-on-black contrast can also *cause* bloom, fatiguing eyes in dark rooms.
- **Why it works:** HDR displays amplify bright values; contrast ratios >7:1 make secondary UI feel "too loud" and degrade visual hierarchy.
- **Operationalisation:** Cap UI brightness at #e0e0e0 (luminance ~80%), not pure white. Critical text stays at #f0f0f0 (85%) minimum. Avoid high-saturation colours near max brightness. Test bloom at scale 0.5 (half-res HiDPI) and full resolution; if icons blur >2px, reduce saturation or brightness. For HUD chrome (frames, borders), use #c0c0c0–#d0d0d0 (neutral mid-grays), not primaries.

### 11. Reducing Cognitive Load: 4dp Spacing Unit & Predictable Layout — Information Architecture
- **What it is:** A single spacing unit (4dp) applied consistently reduces decision friction and makes visual scanning predictable. 4dp multiples (4/8/12/16/20) eliminate arbitrary gaps that signal "unrelated."
- **Why it works:** The brain builds mental maps of where information lives; predictable spacing confirms grouping. Varied spacing introduces ambiguity.
- **Operationalisation:** Cataclysm-BN: all padding/margin/gap is a multiple of 4dp. Strip padding: 4dp top/bottom, 12dp sides. Dock padding: 8dp top/bottom, 12dp sides. Section gaps: 8dp. Intra-row gap: 4dp. Bar height: 8dp (2×4). This single unit prevents the "disjointed" feeling of the current HUD (which mixes 3dp, 8dp, 14dp, 20dp). Apply to all future HUD regions.

## Palettes

### Palette A: "Teal Chrome" — Luminance-First, Dark Background
*Primary HUD palette for the tile-map overlay. Luminance-ordered for monochrome safety.*
- **Dark chrome:** #2a5a50 (L=42) — HUD background, frame tint
- **Scrim (medium):** #1a3a32 (L=26) — log overlay, text background
- **Text:** #f0f0f0 (L=94) — primary labels, metrics
- **Text muted:** #b0b0b0 (L=71) — secondary labels, timestamps
- **Health bar (full):** #00c000 (L=63, green-shifted for deuteranopia)
- **Health bar (mid):** #f0d000 (L=80, yellow)
- **Health bar (crit):** #ff4040 (L=48, red)
- **Accent:** #a1885f (L=56) — runic frame ink, warning highlights

### Palette B: "Monochrome Safe" — Accessibility Test
*Verify all above colours in grayscale; should remain distinct at L-steps of 15+.*
- **Black:** #000000 (L=0) — text shadow
- **Dark gray:** #404040 (L=25) — inactive elements, borders
- **Mid gray:** #808080 (L=50) — secondary data, separators
- **Light gray:** #d0d0d0 (L=82) — hover states, highlights
- **White:** #ffffff (L=100) — critical text (health count)

### Palette C: "Colorblind Deuteranopia Safe" — Red-Green Substitution
*Test when red/green channels are disabled. Use blue/orange substitution.*
- **Safe red → Orange:** #ff8800 (L=55) — danger, low health
- **Safe green → Blue:** #0088ff (L=50) — safe, high health
- **Safe yellow → Cyan:** #00cccc (L=60) — neutral, stamina

## Typography & Motion Notes

- **Minimum readable size:** 0.8em monospace = ~11px at 96dpi; ensure line-height 1.2× font-size to prevent glyph clipping.
- **Letter-spacing:** Always explicit; default 0dp on body text. Use 1px on labels and 2dp on headers to prevent run-on perception.
- **Animation restraint:** Restrict per-turn animations to 2–3 simultaneous, max 150ms each, ease-out curve (cubic-bezier 0.25 0.46 0.45 0.94). Never animate every-turn decay (stamina, hunger); animate only discrete events.
- **Scanline preference:** Avoid blinking cursors; use solid underline or background highlight for focus. Blink fatigues eyes after 200+ turns.
- **Pixel grid:** Align all elements to 1dp grid at any resolution; fractional dp causes blurriness on integer-pixel displays.

## Sources

1. https://www.nngroup.com/articles/gestalt-proximity/
2. https://medium.com/design-bootcamp/designing-efficient-dashboards-with-the-tufte-way-9209e79f2ffb
3. https://link.springer.com/article/10.1007/s42979-026-05015-3 (Preattentive Features in 3D Games)
4. https://gameaccessibilityguidelines.com/ensure-no-essential-information-is-conveyed-by-a-fixed-colour-alone/
5. https://smarteye.se/blog/eye-tracking-technology-101-guide-to-common-industry-terminology/
6. https://www.uxpin.com/studio/blog/what-is-progressive-disclosure/
7. https://developerux.com/2025/04/11/7-principles-of-interactive-animation-in-ux/
8. https://www.filamentgames.com/blog/the-hidden-barriers-in-game-ui-and-how-to-spot-them

---

**Word count:** 1,247 words. **Pattern count:** 11. **Palette count:** 3. **Source count:** 8.