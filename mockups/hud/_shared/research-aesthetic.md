# AESTHETIC REGISTERS FOR HUD DESIGN

## Patterns (12 Registers)

**Terminal Phosphor — DEC VT220 (1979–1982)**
Hexes: #33FF66 (P1 green), #FFB000 (P3 amber), #F0F0F0 (P4 white), #0A0A0A (ground), #00FF99 (grid accent)
What it is: Monochromatic CRT emulation with warm amber or cool green glow; 80-column character cell grid with box-drawing frames.
Why it works: Warm phosphor on dark ground is readable for hours without fatigue; grid enforces strict information hierarchy and legibility under stress.
Applicability: Excellent for core vitals, threat status, and navigation displays; per-body-part health bars map naturally to grid layout; low GPU cost with CSS gradients and text-shadow.
Type: Monospace, 80-column fixed | Frame: Box-drawing character borders | Signature: Scanline glow behind text.

**IBM CGA/EGA — Norton Commander aesthetic (1981–1990)**
Hexes: #00AAAA (dark cyan), #55FFFF (light cyan), #555555 (shadow), #AAAAAA (text), #FFFFFF (bright), #FFFF55 (accent)
What it is: 16-colour DOS-era restriction; cyan-on-blue panel frames; blocky sans-serif stencil typography; double-line borders.
Why it works: Cognitive anchoring from millions of old file managers; cyan jumps from blue, forcing visual focus; the 16-colour limit prevents hierarchy collapse.
Applicability: Excellent for itemized lists (inventory, threats, status effects); high cognition transfer for players with PC nostalgia; frame treatment is pure CSS borders.
Type: Blocky sans-serif stencil | Frame: Double-line borders, cyan jump on blue | Signature: Cyan highlight forcing visual focus.

**Teletext / Ceefax — BBC broadcast graphics (1974–present)**
Hexes: #000000, #FF0000, #00FF00, #FFFF00, #00FFFF, #FFFFFF
What it is: 40-column × 24-row chunky monospace; six-colour palette on black; 2×3 block-mosaic graphics (sixels); high saturation primaries.
Why it works: Optimized for 1970s broadcast CRTs; every stroke is deliberate; saturation-on-black reads instantly; 40-column width mirrors retro handhelds.
Applicability: Strong for warning overlays and zone indicators; sixels can replace some icon glyphs; not suitable for fine typography; CSS-able via monospace + thick borders and block backgrounds.
Type: Chunky blocky monospace | Frame: Thick borders + block-mosaic sixels | Signature: 40-column × 24-row grid enforced.

**Swiss Typographic / Brutalist Web — modernism (1950s–2020s)**
Hexes: #0A0A0A (text), #FFFFFF (surface), #F5D00D (accent), #E5E7EB (border alternative)
What it is: Grid-based left alignment; single accent colour (bright against neutral); hard hairline rules; aggressive whitespace; DIN or Helvetica; no ornament or shadow.
Why it works: Zero visual noise forces attention to content; the accent colour becomes a navigation beacon; grid alignment creates kinetic balance and predictability.
Applicability: Excellent for "clean" HUD states (safe, normal play); high-information density without clutter; type treatment is monospace (Source Code Pro, already bundled) + line-height pinning.
Type: DIN sans-serif, left-aligned to grid | Frame: Hard hairline rules, aggressive whitespace | Signature: Single bright accent colour on neutral.

**Blueprint / Technical Drawing — engineering schematic (1842–present)**
Hexes: #0041BA (blueprint navy), #050C1E (dark navy), #00FFFF (bright cyan line), #35637C (grid/dimension), #00FFCC (callout accent)
What it is: Cyan lines on navy or black ground; thin hairline rules; dimension ticks and callouts; lettering in DIN/technical serif; centred compass rose or measurement grid.
Why it works: Conveys precision, authority, plan-state thinking; cyan on black has infinite contrast; the vocabulary (callout leaders, tick marks) is instantly recognized by engineers.
Applicability: Ideal for gear examination, crafting recipes, vehicle schematic view; the hairline + dimension-tick vocabulary is portable to a HUD; frame treatment uses thin horizontal/vertical rules.
Type: DIN technical serif | Frame: Thin rules, dimension ticks, centred zero-point | Signature: Callout leaders and dimension marks.

**Military Stencil / Mil-Spec — field equipment (MIL-STD-130, 1970–present)**
Hexes: #5D5D31 (olive drab FS 34087), #F5D99C (army sand FS 30277), #1A1A0F (shadow), #FFCC00 (hazard yellow), #1A1A1A (hazard black)
What it is: Stencil font with open letters (Mil Spec 1472D derivative); olive-drab base on sand accents; yellow-black hazard stripes; stencilled rivets and panel lines; industrial materiality.
Why it works: Weathered, hardened aesthetic; olive + sand reads as "equipment tested in the field"; yellow-black stripes arrest attention for danger/caution zones.
Applicability: Perfect for status degradation states (poisoned, infected, bleeding) and warning zones; signature element is yellow-black stripe as a border or background wipe; stencil font available in bundled set or CSS outline.
Type: Stencil font (Mil Spec 1472D) | Frame: Riveted plates, yellow-black stripes | Signature: Yellow-black hazard stripe border.

**Medical / Clinical Instrument — vitals monitor (1980–present)**
Hexes: #FF2D2D (immediate red), #FFD700 (warning yellow), #00C853 (stable green), #F0F0F0 (minor white), #757575 (neutral grey)
What it is: High-contrast monospace traces (ECG waveform, SpO₂ waveform); triage colour coding (red=immediate, yellow=warning, green=stable, white=minor); anatomical body-region callouts; numeric readout in DIN.
Why it works: Every colour has unambiguous meaning; the waveform conveys continuous state; anatomical callouts map the game's per-body-part health system directly to visual memory.
Applicability: Perfect for vitals panel redesign; triage colours can replace current condition icons; waveform animation is trivial via SVG or CSS animation; signature is animated ECG-like trace at top.
Type: DIN monospace, grid-aligned | Frame: Grid overlay background | Signature: Animated ECG-like waveform trace.

**Aviation Six-Pack — cockpit instrument cluster (1937–present)**
Hexes: #00C853 (normal green arc), #FFFFFF (flap white arc), #FFD700 (caution yellow), #FF0000 (never-exceed red line), #000000 (dial face)
What it is: Round gauge faces with needle pointers; colour-coded operational arcs (green=normal, white=flap-safe, yellow=caution, red=maximum); printed scale markings and numerals in DIN; 2×3 grid arrangement.
Why it works: A century of flight-safety standardization; colour coding is subconscious; the needle + scale metaphor survived digital displays because pilots trust it.
Applicability: Excellent for stamina/morale/hunger gauges arranged as a mini "instrument panel" at the bottom; each gauge is an SVG circle with a rotated line (needle); the six-pack layout enforces clean hierarchy.
Type: DIN numerals on gauge face | Frame: Round gauge perimeters, 2×3 grid layout | Signature: Needle pointer sweeping coloured operational arcs.

**Nixie Tube / Seven-Segment LED — retro digital counter (1955–1980s)**
Hexes: #FF5A36 (Portland orange), #FFA213 (vintage amber), #FFB933 (warm white), #1A1A1A (dark ground), #333333 (off-segment)
What it is: Warm orange-red glow; soft blur around numerals; seven-segment or wire-filament geometry; stacked display with vertical alignment; vacuum-tube-like visual decay and flicker.
Why it works: Analogue warmth in a digital display; orange against dark reads as "vintage laboratory"; numeric stacking creates vertical rhythm; blur mimics phosphor bloom without shader cost.
Applicability: Excellent for time-of-day clock, turn counter, and resource counts (ammo, food); CSS text-shadow + orange + monospace creates the effect; animation applies subtle flicker on tick via opacity steps.
Type: Monospace stacked vertically | Frame: Numeric cells spaced apart | Signature: Warm orange glow + subtle flicker.

**Weathered / Apocalyptic Found-Media — post-collapse (contemporary DIY/zine)**
Hexes: #F5EDD9 (aged paper tint), #E8DCC8 (water-damage tan), #8B4513 (mimeograph brown), #D2B48C (decay tan)
What it is: Mimeograph smudge (slightly-offset half-tone), hand-written annotations (irregular stroke weight, variable baseline), water-damage fading and bleeding, ink stamps with irregular registration, aged paper tint, torn edges.
Why it works: Conveys authenticity, abandonment, and time-passage; hand-made elements feel personal; the decay is readable because it's intentional.
Applicability: Strong for lore documents, crafting recipes, and injury/scar logs; signature is a hand-drawn box or underline around critical numbers; frame treatment uses uneven borders, slight rotation, optional water-damage circles.
Type: Irregular hand-drawn stroke | Frame: Uneven borders, slight rotation, water-damage circles | Signature: Hand-drawn underline/box around critical numbers.

**Minitel / Prestel — 1970s videotext (40-column, 8-colour)**
Hexes: #000000 (black), #FF0000 (red), #00FF00 (green), #FFFF00 (yellow), #00FFFF (cyan), #FFFFFF (white)
What it is: 40-column fixed display, 24 rows; limited palette on black (no half-tones); character cell 2:1 aspect ratio; blocky sans-serif; simple box-draw set; flash (blink) and conceal modes.
Why it works: Videotext was designed for low-bandwidth transmission and CRT phosphor flicker stability; the 40-column width forces brevity; the colour limit encodes meaning clearly.
Applicability: Ideal for tutorial overlays and mode indicators; character-cell rendering means every element snaps to grid; frame treatment uses double-line borders from the box-draw set; signature is blink animation (on/off every 500ms).
Type: Blocky monospace character-cell | Frame: Box-draw double-line borders | Signature: Blink animation on/off (500ms).

**CRT Artefacts — display effects (1950s–present)**
Hexes: #000000 (ground), #FFFFFF (bright), #CCCCCC (mid-tone), #333333 (shadow line)
What it is: Scanlines, bloom halation, barrel distortion, phosphor persistence trails, aperture grille shadow mask effects. CSS-fakeable: scanlines (repeating-linear-gradient), glow (text-shadow stacking). Shader-required: barrel distortion, bloom, persistent trails, aperture-grille blooming simulation.
Why it works: CRT artefacts are instantly evocative of retro displays; aperture grilles expand vertically under heat (no blooming); shadow masks bloom outward in all directions; scanlines are human-perceptible at 1–2px height.
Applicability: Scanlines via CSS repeating-linear-gradient(to bottom, transparent 0%, rgba(0,0,0,0.15) 1px, transparent 1px, transparent 2px); glow via text-shadow stacking; barrel/bloom require shader pass. Use selectively on phosphor and Nixie registers for authenticity.
Type: N/A (effects render pass) | Frame: CSS gradients (scanlines) + text-shadow (glow) OR shader (bloom/distortion) | Signature: Vertical scanline pattern or soft halo glow.

## Sources

1. https://vt100.net/ — VT100/VT220 terminal documentation and character processing
2. https://github.com/Swordfish90/cool-retro-term — Terminal emulator with configurable phosphor palettes
3. https://int10h.org/blog/ — CRT simulation documentation and artefact analysis
4. https://github.com/viler-int10h/FFmpeg-CRT-transform — Configurable CRT effect reference (bloom, aperture grille, scanlines)
5. https://en.wikipedia.org/wiki/Aperture_grille — CRT technology (blooming vs. grille expansion)
6. https://en.wikipedia.org/wiki/Blueprint — Technical drawing aesthetics and cyanotype process
7. https://en.wikipedia.org/wiki/DIN_1451 — Typography standardization for technical use
8. https://www.who.int/tools/triage — Medical triage colour coding (red/yellow/green/white)
9. https://www.behance.net/gallery/98359575/UI-Designs-for-Caves-of-Qud — Reference roguelike HUD aesthetic
10. https://ilyabirman.net/meanwhile/all/ui-museum-norton-commander-5-0/ — DOS-era file manager UI and colour scheme

Word count: ~1050 | Registers: 12 | Hex values: 4–6 per register | Sources: 10 primary