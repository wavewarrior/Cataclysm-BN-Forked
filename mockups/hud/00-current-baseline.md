# 00 — Current HUD (control sample)

This is not a proposal. It is a translation of what ships today, rendered through
the same harness as every design that follows, so the comparison is
apples-to-apples. Where the shipping HUD is ugly, mis-cascaded or dead, the
mockup is ugly, mis-cascaded and dead in the same way and in the same pixels.

## Thesis

The current HUD argues that a survival roguelike HUD should be a **chassis of
fixed regions carved out of the terminal grid**: two chrome strips top and
bottom, a full-height sidebar dock on one edge, and one floating panel over the
play area — each region a pre-rendered RML string produced by a C++ function, so
the layout never iterates and never reflows. That buys three real things: the
regions are stable enough that a player builds muscle memory for *where* to look;
the body-part bars are per-limb rather than one aggregate HP number, which is the
one thing a Cataclysm HUD must get right; and the whole surface costs one string
build per turn. What it trades away is **hierarchy**. Every region is the same
weight, every strip is a single `white-space: pre` line of `label value  label
value`, and nothing on screen tells you that `L ARM 8/30` matters more right now
than `FOC:84` — the arm and the focus pool are the same size, the same distance
from the eye, in the same register. It is a *readout*, not a *display*.

The known failures fall into two groups. The ones `plans/hud-coherence.md`
diagnosed and then fixed in its §8 execution pass are genuinely gone: the
half-covering runic frame is replaced by one 1dp `hud-border` edge grammar
(§1.1), the spacing zoo is one 4dp unit and the five type sizes are three tiers
(§1.3, §1.4), the dock's proximity is inverted correctly (16dp between sections
vs 8dp inner padding, §1.7), the environmental tints are re-authored at ID
specificity so three of five stop being dead (§1.8), and the 15 animation
durations are five slots (§1.6). What that pass did **not** touch, and what this
control sample makes visible, is a second group of defects that are all
*structural*, not stylistic:

1. **Three regions are laid out by percentage of the pixel viewport while the
   space they are supposed to occupy is carved in whole text cells**, and
   `TERMY x fontheight = 67 x 16 = 1072 != 1080`. The consequences are measurable
   below: the top bar paints 20.19px over live terrain, the hotbar runs 6.34px
   off the bottom of the screen and overlaps the bottom bar by 6.36px, and a
   7.52px strip of already-cleared terrain along the bottom of the map is painted
   by nobody.
2. **Two elements have no `display` declaration and RmlUi's default is
   `inline`** (`StyleSheetSpecification.cpp:306`; CBN does not link the sample
   `rml.rcss`, documented at `theme.rcss:99-105`). `.tbar-fill` therefore has no
   width and the target's HP bar is a permanently empty trough. `.dock-header`
   and `.dock-body.hud-map-body` therefore share one line box inside the MINIMAP
   section, so the word `MINIMAP` and the top row of the overmap render on the
   same line with the header's `border-bottom` struck through the middle of it —
   while the LOG header, three lines further down, is correct, because
   `.dock-flex` happens to blockify it. Same class, two different renderings, in
   one panel.
3. **The left arm is bleeding, bitten and at 27% HP, and the vitals panel says
   none of that.** `hud_vitals` computes the limb colour that carries
   bleeding/bitten/splint state into a local called `label_hex`
   (`panels.cpp:872`) and then never uses it. The crit encoding that *would* have
   fired is gated on `o.cur * 100 / o.max < 25`, and integer truncation makes
   `8*100/30 == 26`, so it does not fire either. The player's only evidence that
   an arm is open is the word `Bleeding (L arm)` at the head of a 64-character,
   722px run of `::`-separated grey text at the bottom of the screen.
4. **The message log destroys its own colour semantics after five turns.**
   `msg.get_color()` returns the bright type colour only while
   `turn >= now`, the dim variant for five more turns, and `c_dark_gray`
   thereafter (`messages.cpp:81-101`). Two of the six fixture rows — including
   `The zombie brute claws your left arm!` — are already undifferentiated grey.

## Register

Nothing here is chosen; every value is read out of `data/gui/theme.json` and
`data/gui/sidebar_hud.rcss`.

### Chrome tokens (`theme.json` → `rcss`)

| token | hex | role |
|---|---|---|
| `hud-bg` | `#0a1a1ae0` | every region background (alpha 224/255 = 88%) |
| `hud-border` | `#2a5a50ff` | the entire edge grammar — 1dp hairlines and boxes |
| `hud-fg` | `#80c0a8ff` | body text, uncolorized separators |
| `hud-fg-muted` | `#6e8c84ff` | label tier: `ID`/`COND`/`STAT`/`TIME`, log timestamps |
| `hud-fg-bright` | `#d0f0e0ff` | `.vbar-fill` default (never used — every bar sets its fill inline) |
| `hud-accent` | `#c8a820ff` | dock headers, fresh-row left rule |
| `hud-accent-wash` | `#c8a8201c` | fresh-row background wash (alpha 28/255 = 11%) |
| `hud-bar-bg` | `#123030ff` | bar troughs, log-well fill, top fade stop |
| `hud-ink` | `#0d0d0dff` | `.vbar-text` and `.vbar-tick` — near-black **on** the bars |
| `hud-crit-bg` | `#401010ff` | crit trough — **dead at this fixture**, see below |
| `hud-red-bright` | `#ff7a5aff` | crit fill — **dead at this fixture** |
| `hud-red` | `#e05050ff` | crit text — **dead at this fixture** |
| `hud-scroll-thumb` | `#2a5a50cc` | log scrollbar thumb — not instantiated (no overflow) |

### Game-text palette (`theme.json` → `hud_colors`, via `hud_color_to_hex`)

| nc_color | hex | appears as |
|---|---|---|
| `c_white` | `#d0f0e0` | player name, stat values, place name |
| `c_light_gray` | `#a8c8bc` | stat labels, effects run, weapon, move mode, FOC |
| `c_dark_gray` | `#5a7a70` | log rows older than 5 turns |
| `c_light_green` | `#7ad090` | full HP fill, target HP fill |
| `c_green` | `#5a9c6a` | `SPD:96`, `SAFE` |
| `c_yellow` | `#d4b84a` | COND run, STA fill, hotbar keys, freshest warning |
| `c_brown` | `#b08850` | overmap field glyphs, aged warning row |
| `c_light_red` | `#e05050` | L ARM fill (the gradient's `lo` stop) |
| `c_red` | `#b83c3c` | target name + hostility, hostile count, aged bad rows, mission-marker cell |
| `c_cyan` | `#5aa89c` | `T:8°C` (temp_color: 46.4°F is in the 32–50 band) |

### Type

One face, `Source Code Pro` at `18dp` (`sidebar_hud.rcss:13,16`), stood in for by
`Consolas` per the contract. RmlUi's default `line-height` is `1.2`
(`StyleSheetSpecification.cpp:330`), which is what a plain HUD line box is; the
log overrides to `1.35` and `.dock-body` to `1.4`. Three tiers, as landed by
`hud-coherence.md` §3.2:

| tier | size | treatment | used by |
|---|---|---|---|
| header | `0.85em` = 15.3px | bold, `letter-spacing: 2dp`, `hud-accent` | `.dock-header` |
| label | `0.8em` = 14.4px | `letter-spacing: 1dp`, `hud-fg-muted` | `.seg-label`, `.hud-log-time` |
| body | `1em` = 18px | `hud-fg` or a data colour | everything else |
| (bar) | `0.85em` / `0.8em` thin | `line-height` pinned to the bar height | `.vbar-text` |

### Border / frame

One device, applied inconsistently by design intent: a **1dp `hud-border`
hairline on the play-area-facing edge** of each anchored strip (`border-bottom`
on the top bar, `border-top` on both bottom strips, `border-left` on the dock),
and a **full 1dp box** on the two floating panels (vitals, vehicle). No corners
meet: the top bar's bottom rule stops at x=1568 and the dock's left rule starts
at x=1568, y=0, so they form a T-junction rather than a joined corner. No
rounding, no shadow, no ornament — the `runic-*` procedural frame was removed
from this document (`sidebar_hud.rml:14-20`).

## Layout

TERMX/TERMY are the only inputs. At 1920x1080 with the shipped defaults —
`FONT_WIDTH` 8 (`options_registration.cpp:1074-1076`), `FONT_HEIGHT` 16
(`:1089-1090`), `scaling_factor` 1 — `TERMX = 1920/8 = 240` and
`TERMY = 1080/16 = 67` (`sdl_window.cpp:126-127`). `SIDEBAR_POSITION` defaults to
`"right"` (`:655-658`) and the default widget layout is `custom` at width **44**
cells (`data/json/ui/sidebar.json:611-612`, selected at `panels.cpp:2125`, fed to
`update_offsets` at `:2182`). `sidebar_hud_top_rows()` is 3 and
`sidebar_hud_bottom_rows()` is 4 (`panels.cpp:1802-1810`). `1dp == 1px` because
`density_ratio` and `ui_scale` are both 1.0 at 1080p non-HiDPI
(`rmlui_layer.cpp:64,69,1186`).

`sidebar_hud_apply_rect()` (`panels.cpp:1509-1595`) then writes:

| property | expression | value written | px at 1080p |
|---|---|---|---|
| `dock_width_pct` | `100*44/240` | `18.3333%` | 352 |
| `bar_width_pct` | `100 - 18.3333` | `81.6667%` | 1568 |
| `bar_left` | sidebar right → `"0%"` | `0%` | 0 |
| botbar `top` | `100 - 100*4/67` | `94.0299%` | 1015.52 |
| hotbar `top` | `100 - (100*4/67)/2` | `97.0149%` | 1047.75 |
| overlay `top` | `100*(topbar_border_h + 8)/1080` | `7.0556%` | 76.19 |
| vitals `left` | `width_left_pct`, and `width_left == 0` | `0%` | 0 |
| dock `left` | `100 - 18.3333` | `81.6667%` | 1568 |

Heights are `auto` for all three strips — the 3/4 row counts only set the top
offsets. The measured boxes (Chromium, this file, 1920x1080):

| region | x | y | w | h | notes |
|---|---|---|---|---|---|
| `#hud-topbar` | 0 | 0 | 1568 | **68.19** | 4+4 own padding + 2 x (4+21.6+4) rows + 1 border |
| `#hud-vitals` | 0 | 76.19 | 256 | 215 | shrink-to-fit: 230 bar + 24 padding + 2 border |
| `#hud-dock` | 1568 | 0 | 352 | 1080 | full height, `overflow: hidden` |
| `#hud-botbar` | 0 | 1015.52 | 1568 | **38.59** | ends at y=1054.11 |
| `#hud-hotbar` | 0 | 1047.75 | 1568 | **38.59** | ends at y=**1086.34** — off screen |
| `#hud-vehicle` | — | — | — | — | `display: none` (not driving) |
| `#hud-vignette` | 0 | 0 | 1920 | 1080 | `opacity: 0` at rest |

Inside the dock (content box 327 x 1052 after the `8/12/20/12` padding and the
1dp left border):

| child | h | how |
|---|---|---|
| MINIMAP `.dock-section` | 237.53 | 11 inline line boxes at 21.6px — `min-height: 140dp` is inert on an inline element |
| `.vitals-divider` | 1 (+8/+8 margin) | flex item of `#hud-dock`, so it *is* blockified and does render |
| LOG `.dock-header` | 23 (+4 margin) | flex item of `.dock-flex` → blockified, unlike the MINIMAP header |
| `.log-well` | 754.13 | `flex: 1 1 0` — takes exactly what is left |
| six log rows | 267 | three of six wrap to two lines at 319px of usable width |

That last pair is the single most visible fact about the current dock: the log
well is **752px of client height holding 266.97px of message**, so 485px — 45%
of the dock's full height, 7.6% of the entire screen — is empty recessed
`#123030` trough with a 30px gradient fade over the top of it. `margin-top: auto`
on the first row
bottom-anchors the content, which is correct behaviour for a log and completely
wrong as a use of a quarter-million pixels.

The log's other documented device, the per-row age ramp, is also barely present at
this message count. `hud_log` computes
`fade = 1 - min(rows_back/14, 1) * (1 - 0.42)` (`panels.cpp:934-959`), so the
**0.42 floor is only reached 14 rows back** and six rows span
`0.79 / 0.83 / 0.88 / 0.92 / 0.96 / 1.00` — a 21-point opacity spread across the
entire visible log, which is below the threshold at which a reader can order two
adjacent rows by brightness. The ramp is real, correctly implemented, and
invisible until the log is more than twice as full as it is here. What actually
differentiates these six rows is the hue ladder in `msg.get_color()`, and that one
is *too* aggressive: it flattens both 21:46 rows to the same `c_dark_gray` after
five turns, so the ramp and the ladder pull in opposite directions — the ramp says
"these two are only 17% older", the ladder says "these two have no type at all".

`.log-fade-top` is a third no-op here. Its stops are `#123030ff -> #12303000`
(`rcss:369-373`) — the trough colour fading to the same colour at zero alpha — so
wherever the well is empty it paints `hud-bar-bg` over `hud-bar-bg` and is
invisible by construction. It only does work when a message is scrolled up under
it, which at six rows in a 752px well never happens. Three devices (ramp, fade,
scrollbar) are all latent, waiting on a message volume the well is far too tall to
ever reach at this resolution.

## Data mapping

| fixture field | where it appears | encoding |
|---|---|---|
| Name `Nigel Ashby` | top bar row 1, `ID` segment | numeric/text, `c_white` |
| Profession `Survivor` | — | **DROPPED — no producer emits it** |
| Day `14` | top bar row 2, `TIME` segment | text (`, day 14 of Autumn`, uncolorized → `hud-fg`) |
| Time `21:47` | top bar row 2, `TIME` | text, `c_light_gray` |
| Season `Autumn` | top bar row 2, `TIME` | text |
| Place `Cheswick — residential` | top bar row 2, after ` :: ` | text, `c_white` |
| Weather `Light drizzle` | — | **DROPPED — `hud_topbar` reads only the temperature** |
| Temperature `8 °C` | top bar row 1, `T:8°C` | text + colour band (`temp_color` → `c_cyan`) |
| Wind `NE 12 km/h` | — | **DROPPED** |
| Light level `12%` | — | **DROPPED** |
| Flashlight `34%` | — | **DROPPED — no held-item charge readout exists** |
| STR/DEX/INT/PER `10/12/9/11` | top bar row 2, `STAT` | numeric + colour (`color_compare_base` → `c_white`, unmodified) |
| Speed `96` | `SPD:96` | numeric + colour (`value_color`, ≥75 → `c_green`) |
| Focus `84` | `FOC:84` | numeric + colour (`focus_color`, <100 → `c_light_gray`) |
| Pain `12` | `COND` as `Mild pain`; also `Pain 12` in EFFECTS | text (descriptive ladder, `HEALTH_STYLE` is `bar` not `number`) + numeric |
| Morale `-3` | — | **DROPPED — `morale_stat()` exists but no HUD producer calls it** |
| Encumbrance torso `12`, arms `4` | — | **DROPPED** |
| Move mode `walking` | bottom bar right, `W` | single glyph + colour (`c_light_gray`) |
| Noise made `8` | — | **DROPPED** |
| Head 20/24 | vitals bar 1 | bar 83% + numeric + 3-stop gradient `#a7c46d` + 3 quarter ticks |
| Torso 45/60 | vitals bar 2 | bar 75% + numeric + `#bcbe5c` |
| L arm 8/30 | vitals bar 3 | bar 26% + numeric + `#e05050` (gradient `lo`) |
| L arm **critical** | — | **DROPPED BY THE CURRENT HUD — the fixture field stands; `8*100/30 == 26` and `crit` needs `< 25`, so the encoding that exists never fires. Showing this is a goal for every other design, not an optional extra.** |
| L arm **bleeding, bitten** | recoverable only from the EFFECTS run, whose `Bleeding (L arm)` token sits at (113, 1024) 158x22 — **891px** centre-to-centre from the L ARM bar at (13, 137) 230x18 | **DROPPED FROM THE BARS — `label_hex` computed at `panels.cpp:872`, never used. Nothing on or near the limb says the limb is open.** |
| R arm 30/30 | vitals bar 4 | bar 100% + `#7ad090` |
| L leg 22/28 | vitals bar 5 | bar 78% + `#b4c062` |
| R leg 28/28 | vitals bar 6 | bar 100% + `#7ad090` |
| Stamina `5100/8700` | vitals thin bar | bar 58% + numeric + flat `get_hp_bar` colour `c_yellow` |
| Hunger `Peckish` | `COND` | text + colour (caution tier → `c_yellow`) |
| Thirst `Thirsty` | `COND` | text + `c_yellow` |
| Fatigue `Tired` | `COND` | text + `c_yellow` |
| Warmth `Chilly` | — | **DROPPED — warmth has no `COND` slot** |
| Effects, all 5 | bottom bar left, ` :: `-joined | text, **one flat `c_light_gray` for all five** — no per-effect colour, no severity |
| `Bleeding (L arm)` | as above, wrapped in `<span id="status-bleed">` | + an ambient pulse animation hook |
| Wielded `fire axe` | bottom bar right | text (`fmt_wielded_weapon` → bare `tname()` for a non-gun) |
| `32 bash / 0 cut / +1 to-hit` | — | **DROPPED — the strip shows the name only** |
| Sidearm `Glock 19 12/17 9mm JHP` | — | **DROPPED — nothing in the HUD reads inventory** |
| Target `zombie brute` | bottom bar middle | text, `c_red` (the monster's `basic_symbol_color`) |
| Target HP `62%` | `.tbar` width + trailing `75` | **bar is INVISIBLE** (`.tbar-fill` is an inline span, so width/height are ignored) — only the raw HP number survives |
| Target `4 tiles NE` | — | **DROPPED** |
| Target status `moving` | — | **DROPPED** |
| `3 hostiles visible` | `SAFE: 3` | numeric + `c_red` |
| `1 tracking` | — | **DROPPED** |
| `SAFE MODE ON` | the word `SAFE` in `c_green` | colour only (`safe_color()`) |
| Log, 6 rows | dock LOG well | glyph (`+`/`!`/`^`) + timestamp + text, per-row colour **and** per-row opacity |
| Minimap 11x11 | dock MINIMAP | ASCII glyph grid, one coloured `<span>` per cell |
| Player position | centre cell | inverted cell: terrain colour as **background**, glyph in `#ffffff` — **there is no `@`** |
| Mission marker NE | row 2, col 10 | `background-color: #b83c3c` behind the glyph |
| Hotbar `f` Fire | slot 1 | `[f]  Fire ` |
| Hotbar slot 1 **unavailable** | — | **DROPPED — `hud_hotbar(avatar &)` ignores its argument; there is no disabled state** |
| Hotbar `r` Reload, `'` Run, `5` Wait, `p` Pick up, `c` Craft, `i` Inventory | slots 2,3,5,6,7,8 | `[key] Label` |
| Hotbar `e` Examine, `t` Throw | — | **DROPPED — the action list is a fixed `std::array` of 9 and neither is in it** |
| (producer-only) Crouch, Map | slots 4, 9 | Crouch has no default binding, so it renders the literal string `[Unbound globally!]` |

Two deliberate substitutions, both stated so no one reads them as errors:

- **Overmap glyphs.** The contract calls its grid "suggested ASCII content", so
  its `H`/`F`/`^`/`.` are used verbatim, coloured through the real oter palette
  (house `light_green`, `overmap_terrain_residential.json:12-20`; forest `green`
  and field `brown`, `overmap_terrain_hardcoded.json:39-55`). The real
  `oter_id::get_symbol()` for a residential house is rotation-dependent. Cells
  are one character wide with no separator, which is what
  `overmap_chunk_rows()` emits — the contract's inter-glyph spaces are markdown
  formatting.
- **Hotbar keys.** The fixture's key list is treated as this player's bindings,
  so `r`/`'`/`5`/`p`/`c` are used rather than the shipped defaults. On a
  *default* keymap three of the nine slots are unbound
  (`keybindings.json:2038-2039, 2611-2613, 2617-2619`) and the strip becomes
  `[Unbound globally!]  Reload  [Unbound globally!]  Run  [Unbound globally!]
  Crouch` — 157 characters at 9.897px = **1554px inside a 1520px content box**,
  which overflows the strip past the dock's left border. One instance (Crouch) is
  rendered here so the failure mode is visible without contradicting the fixture.

## Occlusion budget

**31.0% of the 1920x1080 play area.** `CONTRACT.md` carries "roughly 26%"; that was
an estimate made before anyone measured the real rects, and it is **superseded** —
31.0% is the ratified baseline every other design states its density against. Here
is the method in full, and it is reproducible: take the
union (not the sum) of each region's intersection with the 1920x1080 frame, count
anything whose effective alpha is at or near opaque at full weight, and divide by
2,073,600.

Play area: `1920 x 1080 = 2,073,600 px²`.

| region | rect | area | note |
|---|---|---|---|
| `#hud-topbar` | 1568 x 68.19 | 106,921.92 | |
| `#hud-vitals` | 256 x 215 | 55,040.00 | floats over live terrain |
| `#hud-dock` | 352 x 1080 | 380,160.00 | |
| `#hud-botbar` | 1568 x 38.59 | 60,509.12 | y 1015.52 → 1054.11 |
| `#hud-hotbar` | 1568 x 32.25 | 50,568.00 | 38.59 tall, but clipped: 1080 − 1047.75 = 32.25 |
| overlap botbar ∩ hotbar | 1568 x 6.36 | −9,972.48 | 1054.11 − 1047.75 = 6.36 |
| **union** | | **643,226.56** | |

    643,226.56 / 2,073,600 = 0.31020  ->  31.02%

Per region, as a share of the screen: dock **18.33%**, top bar **5.16%**, the two
bottom strips together **4.87%**, vitals **2.65%**.

Excluded, with reasons: `#hud-vignette` is full-screen but `opacity: 0` at rest;
`#hud-vehicle` is `display: none`; the 6.34 x 1568 = 9,940px² of hotbar below
y=1080 is off screen and cannot occlude anything.

**Alpha-weighted alternative: 23.2%.** All five painted regions carry
`background-color: hud-bg` = `#0a1a1ae0` (alpha 224/255 = 0.8784), and
`sidebar_hud_sync` sets `.env-night` for `hour >= 21` (`panels.cpp:1638-1640`) —
the fixture clock is 21:47 — which multiplies each region's opacity by 0.85.
Effective alpha is therefore `0.8784 x 0.85 = 0.7466`, and
`643,226.56 x 0.7466 / 2,073,600 = 23.16%`. Full weight is the headline because
the screenshot shows what that alpha actually buys: the building wall behind the
vitals panel survives only as a faint brightening across the right half of the HP
bars, contaminating the bar colours without making any terrain readable. Quote
31.0% when comparing designs; 23.2% only if a design's own regions are also
alpha-weighted, so the two sides of the comparison use one rule.

**The reconciliation, which is where the real defect is.** The game carves the
terrain viewport in whole text cells: `TERMX − 44 = 196` columns by
`TERMY − 3 − 4 = 60` rows, i.e. `1568 x 960 = 1,505,280 px²`, spanning
x 0→1568, y 48→1008. So the engine has reserved `2,073,600 − 1,505,280 =
568,320 px² = 27.41%` for chrome. The HUD paints **31.02%**. The 3.61-point
difference decomposes exactly:

| | area | % | cause |
|---|---|---|---|
| HUD painted over live terrain | 86,698 | 4.18% | top bar is 68.19px tall but only 48px was carved (20.19 x 1568 = 31,658); the vitals panel is 100% unreserved overlay (55,040) |
| carved to black, painted by nobody | 11,791 | 0.57% | bottom strips start at `(1 − 4/67) x 1080 = 1015.52` but the carve-out starts at `63 x 16 = 1008` — a 7.52px sliver, because `67 x 16 = 1072 != 1080` |
| | | | `568,320 − (643,227 − 86,698) = 11,791` ✔ |

## Motion

Everything below is at its resting state in the mockup, as the contract requires.

- **`transition: background-color 0.8s, opacity 0.8s`** on all six regions
  (`rcss:530-532`), reproduced verbatim. This is the environmental-context
  channel, and it is deliberately the slowest thing on screen. `.env-night` is
  *live* at 21:47, so the resting state of this HUD is 85% opacity on every
  region — that is not an animation frame, it is the steady state.
- **`hud_anim` writes inline `opacity` / `top` / `transform` to CHILD elements
  only** — `vbar_*` (spec `hud_vbar`), `status-*` (`status_bleed` and friends,
  `ambient`, now `repeats: 3`), `log-*` (`hud_log_entry`, a slide-and-fade from
  below), and `hud-vignette` (`hud_vignette`). Because the RCSS transition is
  scoped to the six parents and `hud_anim` never targets those, the two systems
  do not contend (`rcss:507-510`) — the property-scope collision that
  `hud-coherence.md` §1.6 found was fixed in its §8 pass.
- **`hud_shake::trigger(intensity)`** on any net HP decrease, paired with a
  vignette feed (`panels.cpp:1620-1627`). Full-screen, not reproducible without
  JS, and not visible at rest.
- **Nothing animates in the hotbar, the minimap, or the target bar.** Listed as a
  coverage gap in §1.6 and still open. The minimap jump-cuts on every overmap
  tile change; the target HP bar would snap if it were visible at all.
- **The fresh-row treatment is deliberately static** — an accent left rule plus
  an 11%-alpha wash rather than a pulse, so it does not replay on unrelated
  redraws (`rcss:277-282`). That is the one motion decision in the HUD that is
  clearly right, and it is a decision *not* to animate.

## Feasibility in RCSS

This design is by definition 100% feasible: it is the shipping RCSS. The mockup's
deviations are all in the *other* direction — things I had to add to stop a
browser from rendering it better than the game does:

- **`:where(#screen) div, :where(#screen) span { display: inline; }`** — not in
  the source. RmlUi registers `display` with the default value `inline`
  (`StyleSheetSpecification.cpp:306`) and CBN does not link the sample
  `rml.rcss` (`theme.rcss:99-105`), whereas Chrome's UA sheet makes every `div` a
  block. Without this reset the MINIMAP header would correctly sit above the map
  and `.tbar-fill` would correctly show a 62% bar — i.e. the mockup would show
  two bugs fixed. The type-selector specificity (0,0,1) means every ported class
  rule still wins, so the cascade order is identical to RmlUi's.
- **`background-image: linear-gradient(...)`** stands in for RCSS's
  `decorator: linear-gradient(...)` on `.log-fade-top`, and
  `radial-gradient(farthest-corner at 50% 50%, ...)` for the vignette's
  `decorator: radial-gradient(farthest-corner, ...)`. Same painting, different
  property name.
- **`border: 1px solid`** for RCSS's `border: 1dp` (RCSS has no line-style
  keyword), and `::-webkit-scrollbar` for the `scrollbarvertical` /
  `sliderbar` element selectors. Neither is instantiated here — the six fixture
  rows do not overflow the well.
- **`Consolas` for `Source Code Pro`**, per the contract. Advance width 0.5498em
  vs Source Code Pro's 0.6em, so every `white-space: pre` strip in this mockup is
  ~8% narrower than in the game. Measured: the bottom bar's ink is 1108.89px
  (`EFFECTS` run + target readout) plus a 197.97px floated right block, 1306.86px
  of ink inside a 1520px content box with a 213px gap in the middle; at Source
  Code Pro's advance that becomes ~1426px, still inside the box. The
  default-keymap hotbar overflow computed above gets *worse* in the real font
  (1554px of Consolas becomes ~1696px against the same 1520px box).
- **The `.vbar.crit` rules are ported but unselected.** No element carries
  `.crit`, because no fixture bar is under 25%. Kept in the stylesheet so the
  port is complete and so the next design can see what the encoding it is
  replacing actually was: `background-color: #401010` trough,
  `#ff7a5a` fill, `#e05050` text, plus a literal ` !!` appended to the label by
  `vbar_rml` (`panels.cpp:830`).
- **No new C++ producer work** is needed for anything in this file, obviously.
  Everything a *replacement* design would need is therefore a delta against these
  ten producers: `hud_topbar`, `hud_topbar_row2`, `hud_vitals`, `hud_botbar`,
  `hud_hotbar`, `hud_log`, `hud_map`, `hud_vehicle`, `vbar_rml`, plus
  `sidebar_hud_apply_rect` for geometry.

## Weaknesses

1. **The geometry is wrong at every resolution, not just small ones.** The rects
   are percentages of the pixel viewport; the space they are meant to fill is
   carved in whole cells. The two agree only when `TERMY x fontheight` divides
   the window height exactly. At 1080p that costs 6.34px of hotbar off the bottom
   of the screen, a 6.36px strip where the hotbar paints over the bottom bar, and
   a 7.52px black sliver along the map's lower edge. At **1280x720** the same
   arithmetic gives `TERMY = 45` and a bottom band of `4/45 x 720 = 64px` for two
   strips that still measure 38.59px each — the half-band is `2/45 x 720 = 32px`,
   so the hotbar hangs 6.6px off the bottom and overlaps the bottom bar by 6.6px,
   13.2px of excess in a 64px band. Meanwhile the top bar's fixed 68.19px eats
   9.5% of a 720p screen instead of 6.3%, and the dock stays 352px so it goes
   from 18.3% to **27.5%** of the width. The log's 319px of usable text width does
   not change at all, even though the map viewport it is carved from shrinks by a
   third.
2. **A colourblind player loses the body-part bars entirely.** The bars' only
   severity encoding is the 3-stop hue ramp green→yellow→red; the fill *width*
   duplicates it, but the three quarter ticks are `#0d0d0d` on a mid-tone fill
   and are nearly invisible even to a trichromat. Worse, the numeric label is
   `hud-ink` `#0d0d0d` painted across the whole bar, so on any bar under ~50% the
   digits fall on the `#123030` trough — near-black on very dark teal, contrast
   ratio ≈1.5:1. `L ARM 8/30` in this mockup is legible for the first seven
   characters and then vanishes. That is the single most important number on the
   screen.
3. **Twelve effects would break the bottom bar silently.** `hud_botbar` takes the
   first 8 and appends ` (+4)` — but the strip is one `white-space: pre` line with
   no wrapping and no ellipsis, and `.strip-right` floats the weapon/move/SAFE
   block to the far edge. At five effects the left run is 73 characters; at twelve
   it is ~150, and the left run collides with and paints under the floated right
   block, hiding the hostile count. The row does not grow, scroll, or truncate —
   it just overlaps. The same class of failure is already live in the hotbar on a
   default keymap.
4. **Half the fixture is unrepresentable.** Sixteen fields in the mapping table
   above are marked DROPPED, including morale, encumbrance, noise, warmth, wind,
   light level, the sidearm, the wielded weapon's damage numbers, and the target's
   range and behaviour. Several of those (`morale_stat`, the light-level helper)
   still exist in `panels.cpp` as functions no HUD producer calls — leftovers of
   the curses sidebar this replaced. Any design that adds them back is not adding
   features; it is restoring parity.
