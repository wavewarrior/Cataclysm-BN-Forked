# HUD mockup harness — contract

Every design in `mockups/hud/` is judged side-by-side, so every design obeys this
contract exactly. Deviating means your design cannot be compared and will be
discarded.

## Deliverables (exactly two files, both yours alone)

1. `mockups/hud/<NN>-<slug>.html` — one self-contained HTML file, no JS, no
   external requests, all CSS in a single `<style>` block.
2. `mockups/hud/<NN>-<slug>.md` — the design spec (format at the bottom).

`<NN>` is your zero-padded design number, `<slug>` is your kebab-case concept
name. Touch **no other file**. Do not create directories.

## The frame

The HTML must render at exactly 1920x1080 with the game world behind it:

```html
<!DOCTYPE html>
<html><head><meta charset="utf-8"><style>
  html,body { margin:0; padding:0; background:#000; }
  #screen {
    position:relative; width:1920px; height:1080px; overflow:hidden;
    background-image:url("_shared/world.png");
    background-size:1920px 1080px;
  }
</style></head>
<body><div id="screen">
  <!-- your HUD here -->
</div></body></html>
```

`_shared/world.png` is a real Cataclysm-BN tile-map composite (32px tiles: grass,
dirt, pavement, a building interior, trees, five zombies, the player near the
centre at roughly x=944 y=528). It is deliberately noisy and high-contrast —
if your HUD is unreadable over it, that is a finding about your design, not the
backdrop. The player's tile is at pixel (928,512)-(960,544); designs that anchor
to the avatar should use that point.

## Hard rules

- **No JS.** No `<script>`. No animations that depend on scripting.
- **No external assets** except `_shared/world.png`. No web fonts, no SVG files,
  no images you would have to create. Inline `data:` URIs are permitted but keep
  them small; prefer CSS gradients.
- **Fonts**: pick from what Windows Chromium has locally. The game's UI font is
  *Source Code Pro*; use `Consolas` as its metric stand-in for monospace work.
  Other safe faces: `Cascadia Mono`, `Lucida Console`, `Courier New`,
  `Segoe UI`, `Tahoma`, `Verdana`, `Georgia`, `Trebuchet MS`, `Impact`,
  `Franklin Gothic Medium`. If your concept needs a face the game does not
  bundle, use the nearest local stand-in and say so in the spec's Feasibility
  section.
- **CSS animation is allowed** but the screenshot is taken at a static moment, so
  the design must read correctly with all animations at their resting state.
  Put motion intent in the spec instead of relying on it visually.
- **RmlUi feasibility matters.** This must be buildable in RCSS. The engine is
  RmlUi **6.2** (pinned in `CMakeLists.txt:781`); the lists below were verified
  directly against the fetched source, so trust them over your CSS instincts.

  **Available in RCSS 6.2** — use these freely:
  flexbox (incl. `row-gap` / `column-gap` / `flex-wrap`), `box-sizing`,
  absolute positioning, `border-radius`, `opacity`, `letter-spacing`,
  `transform` + `perspective`, transitions, `@keyframes` animations,
  `box-shadow`, `backdrop-filter`, `mask-image`,
  `filter:` `blur()` `drop-shadow()` `brightness()` `contrast()` `grayscale()`
  `invert()` `opacity()` `saturate()` `sepia()` `hue-rotate()`,
  `font-effect:` glow / outline / shadow / blur,
  and the full `decorator` set: `linear-gradient`, `radial-gradient`,
  **`conic-gradient`**, all three `repeating-*` variants, `horizontal-gradient`,
  `vertical-gradient`, `image`, `ninepatch`, `tiled-box`, `tiled-horizontal`,
  `tiled-vertical`, `text`, `shader`. Decorators stack in layers, which is how
  you build ornament without pseudo-elements.

  **NOT available in RCSS 6.2** — do not depend on these:
  CSS grid (`grid-template-*`), `::before` / `::after` generated content and the
  `content` property, `clip-path`, `mix-blend-mode`, `text-shadow`
  (use `font-effect` or `filter: drop-shadow`), SVG, `writing-mode`,
  `calc()` beyond trivial cases, web fonts (only bundled TTFs).
  You MAY use a forbidden property in the mockup **only** if you name it in the
  spec's Feasibility section with the RCSS technique that replaces it (e.g.
  "CSS grid here → nested flexbox in RCSS"; "`::after` tick mark → an extra
  child div, or a `repeating-linear-gradient` decorator layer").
  Prefer honest constructions: a mockup that cheats with unbuildable CSS is worth
  less than one that shows what actually ships.

## The fixture — render this data, all of it, verbatim

Every design must present the same game state. You choose the *form*, the
*hierarchy*, and what to demote to ambient encoding — but every field below must
be recoverable from your mockup, or the spec must state explicitly that you
deliberately dropped it and why. Values are exact; do not invent different ones.

**Identity / world**
- Name `Nigel Ashby`, profession `Survivor`
- Day `14`, time `21:47`, season `Autumn`
- Place `Cheswick — residential`
- Weather `Light drizzle`, temperature `8 °C`, wind `NE 12 km/h`
- Light level `12%` (moonlight), carrying a lit flashlight at `34%` charge

**Character**
- STR `10`, DEX `12`, INT `9`, PER `11`
- Speed `96`, Focus `84`, Pain `12`, Morale `-3`
- Encumbrance: torso `12`, arms `4`
- Move mode `walking`, noise made `8`

**Body-part health** (this is the signature Cataclysm requirement — a single
aggregate HP bar is a design failure)
| part | cur | max | note |
|---|---|---|---|
| Head | 20 | 24 | |
| Torso | 45 | 60 | |
| L arm | 8 | 30 | **critical**, bleeding, bitten |
| R arm | 30 | 30 | |
| L leg | 22 | 28 | |
| R leg | 28 | 28 | |

**Resources**
- Stamina `5100 / 8700`
- Hunger `Peckish`, Thirst `Thirsty`, Fatigue `Tired`, Warmth `Chilly`

**Effects** (5, in priority order)
`Bleeding (L arm)`, `Bitten (L arm)`, `Soaked`, `Tired`, `Pain 12`

**Combat**
- Wielded: `fire axe` — `32 bash`, `0 cut`, `to-hit +1`
- Sidearm in inventory: `Glock 19` — `12/17`, `9mm JHP`
- Target: `zombie brute`, HP `62%`, `4 tiles NE`, status `moving`
- Threats: `3 hostiles visible`, `1 tracking`, `SAFE MODE ON`

**Message log** (oldest first; newest is the last line, and must read as newest)
| time | text | colour semantics |
|---|---|---|
| 21:46 | You hit the zombie brute with your fire axe. | neutral/good |
| 21:46 | The zombie brute claws your left arm! | bad |
| 21:47 | Your left arm is bleeding. | bad |
| 21:47 | You feel a sharp pain. | bad (dimmer) |
| 21:47 | The zombie shambler groans nearby. | caution |
| 21:47 | You are getting thirsty. | caution |

**Minimap** — an 11x11 overmap chunk. Render it however your concept wants
(ASCII glyph grid, coloured cells, abstracted blocks, or omit it and justify).
There is a mission marker to the NE. Suggested ASCII content if you want one:

```
. . . F F F . . . . .
. . F F F F F . . ^ .
. . F F . . F F . . .
. ^ . . H H . . F . .
. . . H H H H . . . .
. . . H @ H H . . . .
. . . H H H . . F F .
. . F . . . . . F F .
. F F . ^ . . . . F .
. F F . . . . . . . .
. . . . . . . . . . .
```
(`@` player, `H` house, `F` forest, `^` mission/POI marker, `.` field)

**Hotbar** — 9 slots, `key → action`:
`f` Fire · `r` Reload · `'` Run · `e` Examine · `p` Pick up · `c` Craft ·
`i` Inventory · `t` Throw · `5` Wait.
Slot 1 (Fire) is unavailable (no gun wielded) — show that state.

**Colour semantics** — red = harm, green = healthy, yellow/amber = caution,
plus whatever your register dictates. Your palette does not have to be the
current teal/gruvbox one; if you replace it, list the hexes in the spec.

## Spec format — `mockups/hud/<NN>-<slug>.md`

```markdown
# <NN> — <Concept Name>

## Thesis
One paragraph. What is this design's single argument about how a survival
roguelike HUD should work? What does it trade away to win that argument?

## Register
The visual language, with the palette as a hex table (token name, hex, role).
Type stack and sizes. Border/frame treatment.

## Layout
Where each region sits and why. Give real numbers (x/y/width/height in px at
1920x1080).

## Data mapping
A table: fixture field → where it appears → encoding (numeric / bar / glyph /
colour / position / ambient / on-demand). Every fixture field must have a row,
including any you deliberately dropped (mark them `DROPPED — reason`).

## Occlusion budget
The fraction of the 1920x1080 play area your HUD covers, and how you computed
it. Count anything opaque or near-opaque; count translucent scrims at half
weight. Current HUD is roughly 26%.

## Motion
What animates, on what trigger, with what duration and easing — or "nothing
animates, and here is why".

## Feasibility in RCSS
Anything in the mockup that is not directly buildable in RCSS, with the
replacement technique. Anything needing new C++ producer work in
`src/panels.cpp`. Anything needing a bundled font or new asset. Be honest;
"this needs a new producer that emits per-part polar coordinates" is a fine
answer.

## Weaknesses
Two to four honest ones. What breaks at 1280x720? What breaks for a
colourblind player? What breaks when there are 12 effects instead of 5?
```

## Reference material

- Current HUD implementation: `data/gui/sidebar_hud.rml`, `data/gui/sidebar_hud.rcss`
- Current palette tokens: `data/gui/theme.json` (`rcss` and `hud_colors` blocks)
- Producers that generate the HUD content: `src/panels.cpp`
  (`hud_topbar`, `hud_topbar_row2`, `hud_vitals`, `hud_botbar`, `hud_hotbar`,
  `hud_log`, `hud_map`, `hud_vehicle`, `sidebar_hud_apply_rect`)
- Known problems with the current HUD: `plans/hud-coherence.md`
- Research digests: `mockups/hud/_shared/research-*.md`

## Do not

- Do not run builds, linters, formatters, or tests.
- Do not touch `src/`, `data/`, or any other design's files.
- Do not edit this contract.
