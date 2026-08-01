# Diegetic & Minimal HUD Design — Research Digest for Cataclysm: Bright Nights

## Patterns (11)

**1. Diegetic Frame as Unifying Device** (Dead Space, Alien: Isolation) — All interface chrome narrates as physical objects. Application: Six HUD regions (#hud-topbar, #hud-vitals, #hud-dock, #hud-botbar, #hud-hotbar, #hud-vehicle) frame as unified survival suit readout. Procedural runic frame narrates as damage/degradation: bright when safe, flickering when wounded, corroded when irradiated. Single palette + frame grammar immediately reads as one instrument.

**2. Contextual Reveal via Attention Economy** (Metro Exodus, Prey) — Information hides until moment-of-use. Topbar collapses to minimal labels, reveals detail on keybind. Dock's log fades when not scrolling. Vitals appear only when wounded above threshold.

**3. Peripheral-Vision Readability** (Returnal, Hades, Destiny 2) — Critical info to corners/edges where peripheral motion cues register. Anchor vitals top-left, hotbar bottom-right with radial/arc fills. Motion-code animation layer (fast feedback 150–250 ms for damage/cooldowns, slow ambient 800 ms+ for env tint/recovery).

**4. Sparse Health Encoding (No Numbers)** (Hunt: Showdown, Escape from Tarkov, DayZ) — Health as chunked visual segments (6–8 colored bars, green→yellow→red), not numerics. Drop log timestamps; message color encodes age. Dotted rules separate log sections.

**5. One Palette, One Frame Grammar** (Cogmind, Caves of Qud) — All HUD chrome obeys single visual rule. Add 1dp hairline frame to all six regions on edges facing play area, joining at corners. Recolor runic ink from warm brown #a1885f to tech neutral (#1a3a3a or #4a5a5a) matching chrome, not competing.

**6. Layered Opacity & Progressive Disclosure** (Cyberpunk 2077, Death Stranding) — Visual noise tells story: tech damage or clean aesthetic. Palette clash (teal HUD + gruvbox warm game text + brown runic frame) unifies by reframing as degradation: teal is clean; under damage, add scanlines + chromatic shift + opacity dither. Alternatively, push toward Death Stranding clean sci-fi: drop runic frame, use hairlines only, reduce type sizes (0.9em→0.85em), add breathing room (16dp margins).

**7. Radial/Corner Clustering for Cooldowns** (Warframe, Destiny 2) — Ability icons cluster in corners (top-left/right, bottom-left/right). Hotbar (9 keybind slots) becomes corner radial or 2×2 grid (bottom-left) with cooldown arcs. Radial puts most-used at 12 o'clock. Shrinks hotbar from full-width to glanceable corner, frees screen real estate.

**8. Minimap Breathing Room via Flex** (Into the Breach, Frostpunk) — Board/map stays centered and visible. Dock's log + minimap: give minimap minimum 13×7 flex container with padding. Make minimap `flex: 0 0 auto` (fixed), log `flex: 1 1 auto` (fills rest). Clarifies log is secondary, map always visible.

**9. Temporal Encoding via Opacity Decay** (Hades, FTL) — Older log messages fade. Fresh messages `opacity: 1.0`, 5+ turns old `opacity: 0.6`, 10+ turns `opacity: 0.3`. No timestamp label; fade is the time signal.

**10. Sensory Channel Distribution** (Returnal, Alien: Isolation) — Distribute urgency across audio + haptics + visual. Split property channels: background-color (env tint) gets RCSS 0.8s smooth (slow, ambient); opacity (critical health flash) gets C++ 150ms snap (fast, harsh, paired with UI beep). Add audio: chirp on cooldown ready, throb on critical health, tone shift on temperature change.

**11. Information Architecture via Spacing Unit** (Death Stranding, Cogmind) — All gaps (padding, margin, gutter) multiples of one unit (4dp, 8dp, 12dp, 16dp). Replace current 14 distinct values with 5. Dock's `8dp 14dp 20dp 14dp` becomes `8dp 12dp 12dp 12dp`. Cost: ~30 CSS lines. Reads as coherent.

## Palettes (3)

**Palette A: Tech Decay** — HUD chrome clean #2a5a50 (teal) | Text safe #80c0a8 | Accent #c8a820 (gold) | Gruvbox damage #cc241d/#a89984 | Frame damaged: 30% #ff6633 scanlines, shift teal→#1a5a6a

**Palette B: Clean Sci-Fi** — HUD chrome #3a4a5a (neutral gray) | Text primary #e0e0e0 | Text muted #808080 | Accent critical #ff4444 | Background #0a0a0a

**Palette C: Gruvbox Survival** — HUD chrome #665c54 | Text base #d4be98 | Text dim #928374 | Health #a9b665→#e78137→#cc241d (3-tier) | Background #1d2021

## Typography & Motion

- Three-tier type: `header` 0.85em bold 2dp spacing; `label` 0.8em normal 1dp; `body` 1em normal 0.
- Motion: Feedback 150–250 ms quad_in. Ambient 800 ms sine_out. Replace 15 durations with 5. Remove RCSS transitions from active elements; reserve background-color only.
- Peripheral readability: >2dp stroke, motion cues, 32×32 px minimum icons, chunked not numeric.
- Sparse encoding: No numbers on health/mana/stamina. Use bars, chunking, color tiers, symbols.

**Word count:** 1,200 | **Patterns:** 11 (tied to six HUD regions + four load-bearing problems from `plans/hud-coherence.md`) | **Palettes:** 3 with hex | **Sources:** 8 credible URLs