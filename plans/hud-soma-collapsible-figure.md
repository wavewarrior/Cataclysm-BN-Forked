# SOMA panel → collapsible card with an animated body figure

## Context

`hud_soma` (`src/hud_runic_panels.cpp`) currently emits six flat rows —
`name | pip meter | 124/130` — always expanded, always occupying `panel_h( 26 )` = 550 dp of the
left column. The user asked for three changes:

1. limbs shown by a **visual element**, not a row list;
2. that element **lightly animated**;
3. the whole thing in a **collapsible card**, default COLLAPSED showing a general health bar,
   expandable to the detailed per-limb reading.

This plan supersedes nothing; it extends `plans/hud-creator-register.md`, whose palette and device
contract it obeys without exception. Read that file's **Palette and typography contract** first —
in particular that producers may only colour with the six-rung `hud_runic::ink` ladder and that
every gold mark on screen must be redundant with a shape.

### Two decisions the user made, which are not open

**Animation is EVENT-DRIVEN, never ambient.** `handle_action.cpp:263` drops the input timeout to
33 ms and `:314-321` calls `invalidate_main_ui_adaptor()` — a full-screen redraw — on every loop
iteration while `sidebar_requires_animation()` is true; idle is otherwise 125 ms with no redraw.
An ambient spec therefore costs the whole game 30 fps of full redraws while the player stands
still. Every spec this plan adds has a FINITE `repeats` and settles. `gfx/widgets/icons.json`
already contains infinite specs (`spark`, `compass`, `repeats: -1`); do not copy them, and do not
"fix" this by adding one.

**The collapsed card shows bar + worst limb + active conditions.** Not a bare percentage: limb max
HP is per-part (`base_hp` + `str * 3`, and GLASSJAW cuts the head by 20% —
`character.cpp:1341-1343`), so a summed ratio hides one crippled limb inside five healthy ones. The
worst-limb name says *where*, and the condition chips carry the states that tick damage between
turns, which the player must never have to expand a card to see.

### Two facts that determine the shape of the solution

- **There is no body-diagram system to reuse.** `make_bodygraph_widget_panel`
  (`panels.cpp:1375`) draws a row of colour-coded 5-character abbreviations; `bodygraph_bp_color`
  (`:1327`) is its per-part colour function. Neither draws a figure, and the RmlUi HUD has never
  consulted either.
- **The HUD takes no mouse input.** `sidebar_hud_open` opens the document `passive=true`, so a
  click cannot toggle anything. The toggle is a gameplay keybinding.

## Approach

Steps 1–2 are additive and testable; step 3 is the cutover; step 4 is the toggle; step 5 the
animation; step 6 the tests. Build once at the end of 3, again at the end of 5.

### Step 1 — `src/hud_soma_figure.h`: the figure's geometry and the summary arithmetic

New pure header, namespace `hud_figure`, no game include, no RmlUi. This is the only part of the
feature verifiable without looking at the screen, and per
`plans/hud-creator-register.md`'s sibling convention (`newchar_aptitude.h`, `newchar_dna.h`) that
is where the invariants live.

```cpp
/// Canonical display order, matching body_parts.json `sort_order`
/// (head 0, torso 100, arm_l 200, arm_r 300, leg_l 500, leg_r 600).
enum class limb : int { head, torso, arm_l, arm_r, leg_l, leg_r, num };

struct limb_state {
    int cur = 0;
    int max = 0;
    bool bleeding = false, bitten = false, infected = false;
    bool broken = false, splinted = false;
};

struct body_state { std::array<limb_state, 6> limbs; };

/// Whole-body health, 0..1: sum of current over sum of maximum.
/// Weighted by max HP on purpose — a torso point is worth more than an arm point — and
/// clamped, because a `max` of zero is reachable on a mod-defined part.
auto overall_ratio( const body_state &b ) -> float;

/// The limb the player should look at. Lowest health ratio wins; a tie is broken by
/// SEVERITY (bleeding or bitten first, then broken), and only then by canonical order,
/// so two limbs at 50% do not report the arbitrary one.
auto worst_limb( const body_state &b ) -> limb;

/// Conditions present ANYWHERE on the body, for the collapsed chip row.
struct conditions { bool bleeding = false, bitten = false, infected = false, broken = false; };
auto conditions_of( const body_state &b ) -> conditions;

/// A limb's health ratio, 0..1. `max <= 0` reads as 0 rather than dividing.
auto ratio_of( const limb_state &l ) -> float;

/// Element id for a limb's box in the figure — `hud-soma-fig-<name>`. Stable and
/// load-bearing: `hud_anim` feeds and forgets by exactly this id.
auto element_id( limb l ) -> std::string_view;

/// RCSS fill class for a limb, from its ratio and state. Returns one of
/// `hud-fig-f1` .. `hud-fig-f5`, which map onto the SAME six-rung ink ladder every
/// other producer uses — intact recedes to `label`, damage advances to `datum`,
/// critical reaches `peak`. There is no red and no second hue: the figure encodes
/// health as LUMINANCE, so it survives greyscale like the rest of the HUD.
auto fill_class( const limb_state &l ) -> std::string_view;
```

Criticality reuses `hud_runic::is_critical` verbatim — the same predicate, the same shipped-bug
threshold. Do not restate it.

**Figure box sizes** are `constexpr float` here, each with a comment naming the RCSS rule that must
agree, because a dp constant shared between a producer and a stylesheet drifts the moment only one
is edited.

### Step 2 — the figure markup primitive

The figure is **six flex boxes in pure flow**, one per limb, not a raster:

```
            [ head ]
   [arm_l] [  torso  ] [arm_r]
            [leg_l][leg_r]
```

`position: absolute` resolves against the wrong ancestor in these documents, so nothing may be
overlaid; and six elements rather than a ~99-cell raster is what makes the animation possible at
all, because `hud_anim` addresses elements by id. A raster's cells cannot be grouped per limb.

**Orientation: the avatar seen from BEHIND**, so a limb's side on screen matches its name — `L ARM`
lights on the left. State this in the header comment. The alternative (an anatomical
chart, facing the viewer, patient's left on your right) puts `L ARM` on the right and reads as a
bug beside the text rows.

Emitted by a new helper in `hud_runic_panels.cpp`:

```
<div class="hud-fig">
  <div class="hud-fig-row"><div id="hud-soma-fig-head"  class="hud-fig-head hud-fig-fN"></div></div>
  <div class="hud-fig-row"><div id="hud-soma-fig-arml"  class="hud-fig-arm  hud-fig-fN"></div>
                           <div id="hud-soma-fig-torso" class="hud-fig-torso hud-fig-fN"></div>
                           <div id="hud-soma-fig-armr"  class="hud-fig-arm  hud-fig-fN"></div></div>
  <div class="hud-fig-row"><div id="hud-soma-fig-legl"  class="hud-fig-leg  hud-fig-fN"></div>
                           <div id="hud-soma-fig-legr"  class="hud-fig-leg  hud-fig-fN"></div></div>
</div>
```

**The figure sits BESIDE the six detail rows, not above them**, and that is a height decision, not
an aesthetic one — see the layout table in step 3. Six rows are 120 dp; the figure is ~78 dp; side
by side the pair costs `max( 120, 78 )` and the figure is free. Stacked it would cost their sum and
push POOLS and EFFECTS out of the region. It also reads better: the eye maps a box to the row
level with it.

```
<div class="hud-fig-split">
  <div class="hud-fig"> … the three figure rows above … </div>
  <div class="hud-fig-rows"> … six gauge_row()s and their note-chip rows … </div>
</div>
```

A critical limb additionally takes `hud-fig-crit`, whose gold outline is the same cursor device
`.hud-row.crit` and `.hud-log-fresh` already use — one mark, three places, all redundant with a
shape.

### Step 3 — `hud_soma` rewrite, layout, stylesheet

**The card wraps the LIMB SECTION ONLY.** `hud_soma` emits three sections in one string —
limbs (`hud_runic_panels.cpp:265-315`), `subhead( POOLS )` + STAM/FOCUS/MORALE (`:317-351`), and
`subhead( EFFECTS )` + the roster (`:353-369`). The user scoped the card to limbs; collapsing the
whole producer would ship a default HUD with stamina, focus, morale and the entire effects roster
gone from the screen, which nobody asked for. POOLS and EFFECTS render unconditionally, below the
card, in both states.

**Producer.** `hud_soma( avatar &u, const layout &l )` reads `uistate.hud_soma_expanded` (see
step 4); it takes no new parameter, because the chassis hands producers only the layout and a
second state channel invites the two to disagree.

The limb section becomes one wrapper the producer emits with its own id, so the expand/collapse
motion has an element to address and `data/gui/sidebar_hud.rml` needs no change at all:

```
<div class="hud-card" id="hud-soma-card"> … limb content … </div>
```

- **Collapsed card** — a summary row, a worst-limb row, and a chip row only when a condition is
  live:
  ```
  row "hud-row": <span class="hud-cell-name hud-i3">VITALS</span>
                 <div class="hud-cell-meter" id="hud-soma-bar">{ pips( cur_sum, max_sum, 12 ) }</div>
                 <span class="hud-cell-val hud-iN">{pct}%</span>
  row "hud-row": <span class="hud-cell-name hud-i3">WORST</span>
                 <span class="hud-fig-worst hud-iN">{L LEG}</span>
  row "hud-row hud-chiprow": one chip per live condition — BLEEDING/BITTEN at peak,
                 INFECTED/BROKEN at datum. Omitted entirely when there are none.
  ```
- **Expanded card** — the figure and the six detail rows, side by side, both unchanged in
  substance from today:
  ```
  <div class="hud-fig-split">{ figure }{ six gauge_row()s + their note-chip rows }</div>
  ```
- **Below the card, always**: `subhead( POOLS )` + three pool rows, `subhead( EFFECTS )` + the
  roster. Not inside the card, not conditional.

**Layout.** `hud_runic::layout_options` gains `bool soma_expanded = false` and `layout_for` picks
`soma_max = o.soma_expanded ? panel_h( 26 ) : panel_h( 16 )`. Both are worst-case sums of the RCSS
boxes the panel can actually contain, in dp:

| | expanded | collapsed |
|---|---|---|
| limb card | `max( 6 rows 120 + 6 chip rows 108, figure 78 )` = **228** | 3 rows = **60** |
| POOLS | rule 11 + subhead 20 + 3 rows 60 = **91** | **91** |
| EFFECTS | rule 11 + subhead 20 + 8 tally rows at 16 dp = **159** | **159** |
| total | 478 ≤ 520 → `panel_h( 26 )` = 550, **unchanged** | 310 ≤ 320 → `panel_h( 16 )` = 350 |

**The expanded height must not grow, and the figure is placed beside the rows so that it does
not.** A panel sized under its content does NOT degrade gracefully here: `.hud-body`'s scrollbar is
decorative, because with only passive documents open `rmlui_layer.cpp:702-705` feeds the wheel to
RmlUi and then returns `any_interactive_open()` — false — so the event falls through to the map
zoom. That is the same reasoning already recorded at `sidebar_hud.rcss:207-209` (the roster's
eight-entry cap) and `:337-339` (the disabled nested scroller). Overflowing SOMA does not scroll
POOLS and EFFECTS out of reach; it removes them, which is the content loss this step exists to
avoid. `panels.cpp` passes `uistate.hud_soma_expanded` when it builds the options.

**Document** (`data/gui/sidebar_hud.rml`): unchanged, as above.

**Stylesheet** (`data/gui/sidebar_hud.rcss`), added after the existing list-row block:

```
.hud-fig-split      flex 0 0 auto; display flex; flex-direction row; align-items flex-start
.hud-fig            flex 0 0 auto; display flex; flex-direction column; align-items center;
                    width 46dp; margin-right 8dp; padding-top 2dp
.hud-fig-rows       flex 1; min-width 0; display flex; flex-direction column
.hud-fig-row        flex 0 0 auto; display flex; flex-direction row;
                    align-items flex-start; justify-content center
.hud-fig-head       display block; width 14dp; height 12dp; margin-bottom 2dp
.hud-fig-torso      display block; width 22dp; height 32dp; margin 0 2dp
.hud-fig-arm        display block; width 8dp;  height 28dp
.hud-fig-leg        display block; width 10dp; height 26dp; margin 2dp 1dp 0 1dp
.hud-fig-f1 … -f5   background-color, in order: {{bg2}} {{bg4}} {{fg4}} {{fg}} {{fg0}}
.hud-fig-crit       border 1dp {{yellow}}
.hud-fig-worst      display block; flex 1; min-width 0; white-space nowrap; overflow hidden
```

`.hud-fig` is 46 dp wide — `8 + 2 + 22 + 2 + 8 + 4` of margins — and ~78 dp tall
(`12 + 2 + 32 + 2 + 26 + 4`). At the narrowest column (`col_w` 300, inner 286) that leaves
`286 - 46 - 8 = 232 dp` for the rows beside it, of which `.hud-cell-name` and `.hud-cell-val` take
148, leaving 84 dp of meter — enough for twelve pips. Keep those two figures in agreement with
`hud_soma_figure.h`'s constants; they are the reason the split fits.

Every box states `display` and a fixed `width`/`height`; RmlUi does not blockify flex children and
an inline one ignores both. The five fill classes are the ink ladder's upper five rungs by
another name — `ground` is not among them, because a limb filled with the panel's own ground is a
limb that has vanished.

### Step 4 — the toggle

Five edit sites, copying `toggle_panel_adm` exactly:

| file | change |
|---|---|
| `src/action.h` | `ACTION_TOGGLE_SOMA_DETAIL` beside `ACTION_TOGGLE_PANEL_ADM` (`:321`) |
| `src/action.cpp` | name `"toggle_soma_detail"` (`:334`); add to the always-available list at `:466` |
| `src/game.cpp` | `ctxt.register_action( "toggle_soma_detail" )` beside `:1668` |
| `src/handle_action.cpp` | dispatch at BOTH `:1449` and `:2677` — the action switch is duplicated |
| `data/raw/keybindings/keybindings.json` | default binding, DEFAULTMODE |

**Choosing the default key is not a free choice, and the JSON will lie to you about it.**
`sdl_input.cpp:521-525` and `:577-679` intercept **F4 through F12** inside the SDL event pump —
each handler ends in `break`, so those keys never reach `get_input_event`, the action pipeline, or
any keybinding at all. They drive the lighting dev panel, the shader debug view, the debug-mode
cycle and the emitter tuning. A scan of `keybindings.json` alone reports every one of them as
free, and binding one produces an action that silently never fires while quietly toggling a
lighting debug global instead — which then contaminates any pixel measurement taken afterwards.

Every printable key is already bound in DEFAULTMODE (126 of them), so the remaining space is CTRL
combos, a form this file already uses (`CTRL+M/S/T/U/V/W/Y`). This plan takes **`CTRL+B`**. Any
future free-key scan MUST union the JSON's DEFAULTMODE keys with the `sdl_input.cpp` intercept
list.

State lives in `uistatedata` (`src/uistate.h`), beside `msg_window_wide_display` (`:159`):
`bool hud_soma_expanded = false;` — default COLLAPSED, as asked — plus its line in
`uistatedata::serialize` / `deserialize` so it survives a reload.

The KEYS strip must learn the binding, or the feature is undiscoverable: add a slot to
`key_slots()` in `src/hud_runic_strips.cpp` whose label flips between `BODY+` and `BODY-` with the
state.

### Step 5 — the animation

Three motions, all finite, all settling to zero idle cost. New specs in `gfx/widgets/icons.json`,
modelled on `hud_vbar` (`:86`) and `hud_log_entry` (`:94`):

```jsonc
{ "id": "hud_limb", "animations": [
    // A hit on this limb: the box drops in and springs back. on_decrease only —
    // healing is not an event the player needs pulled to their eye.
    { "trigger": "on_decrease", "property": "scale",  "from": 1.35, "to": 1.0,
      "duration": 260, "ease": "back_out" },
    { "trigger": "on_decrease", "property": "alpha",  "from": 0.35, "to": 1.0,
      "duration": 260, "ease": "quad_out" },
    // Critical: a slow breath, SIX half-cycles and then silence. Finite on purpose —
    // see the note on handle_action.cpp above.
    { "trigger": "critical",   "property": "alpha",   "from": 1.0, "to": 0.35,
      "duration": 620, "ease": "sine_in_out", "loop": "pingpong", "repeats": 6 } ] },

{ "id": "hud_soma_bar", "animations": [
    { "trigger": "on_change", "property": "offset_y", "from": 3, "to": 0,
      "duration": 220, "ease": "back_out" } ] },

{ "id": "hud_soma_card", "animations": [
    { "trigger": "on_change", "property": "alpha",    "from": 0.0, "to": 1.0,
      "duration": 180, "ease": "quad_out" },
    { "trigger": "on_change", "property": "offset_y", "from": -6, "to": 0,
      "duration": 180, "ease": "back_out" } ] }
```

Fed from `hud_soma`, once per sync:

- per limb, when expanded: `hud_anim::feed( { .element_id = hud_figure::element_id( l ),
  .spec_icon = "hud_limb", .value = ratio_of( state ), .is_critical = is_critical( … ) } )`.
  `sidebar_anim` itself decides *when* — the first call primes without firing, a drop fires
  `on_decrease`, entering the critical band fires `critical` and leaving it eases back.
- the collapsed bar: `.element_id = "hud-soma-bar", .spec_icon = "hud_soma_bar",
  .value = overall_ratio(...)`.
- the card, on the frame the toggle flips: `.element_id = "hud-soma-body",
  .spec_icon = "hud_soma_card"`, with the toggle's own counter as `.value` so `on_change` fires.

`hud_anim::forget` MUST be called for every limb id when the card collapses and for
`hud-soma-bar` when it expands, exactly as `panels.cpp` already forgets log rows that leave the
window — an element fed but no longer in the DOM keeps its registry key alive and
`any_active()` true, which is the framerate pin arriving by the back door.

Two traps this must not walk into, both recorded in `cbn-hud-theming-and-motion`:

- **No RCSS `transition` on any property `hud_anim` writes** — `opacity`, `top`, `transform`,
  `transform-origin`, `background-color`. `sidebar_hud.rcss` has no `transition` at all today and
  must keep none. `.nc-pip`'s transition (from `newchar_common.rcss`, on `background-color` and
  `height`) touches the pips, which `hud_anim` never addresses, and is wanted.
- **`repeats` omitted or `<= 0` means infinite.** Every spec above states a finite one or is
  `once` by default.

### Step 6 — `tests/hud_soma_figure_test.cpp`, tag `[hud_figure]`

- `figure_overall_ratio_is_max_weighted_and_bounded` — a full body reads 1.0, an empty one 0.0,
  a zero `max` does not divide, and damaging the torso moves the figure further than damaging an
  arm by the same absolute HP.
- `figure_worst_limb_breaks_ties_by_severity` — two limbs at the same ratio report the bleeding
  one; with no conditions, the canonical-order one; an all-healthy body still reports a limb
  rather than a sentinel.
- `figure_conditions_are_body_wide` — a single bleeding arm sets `bleeding` and nothing else.
- `figure_fill_class_walks_the_ladder_and_never_hits_ground` — a healthy limb is dimmer than a
  damaged one (intact recedes), a critical one is the brightest, and no input yields `hud-fig-f0`.
- `figure_element_ids_are_unique_and_stable` — six distinct ids, all `hud-soma-fig-`-prefixed,
  because `hud_anim` keys on them.
- Extend `tests/hud_runic_test.cpp`: `runic_layout_soma_collapses` — with
  `.soma_expanded = false` the SOMA rect is `panel_h( 3 )` tall at every swept viewport, still
  whole-rowed, still disjoint from every other region.

## Verification

**Build** (background, ≥1200 s, never killed):
```sh
cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles
```

**Unit** — from the repo root; the `out/build/osx-arm-slim/` copies are stale leftovers:
```sh
./cata_test-tiles "[hud_figure],[hud_runic]"
```

**In-game**, driving the installed build and measuring pixels (the vision model on this machine
times out at 300 s; PIL is the oracle):

1. Collapsed default: the SOMA card is ~3 rows tall, shows one pip bar + a percentage + a worst-limb
   name, and the region's measured height matches `panel_h( 3 )` scaled by the dp ratio.
2. Press the toggle: the card grows to the figure + six gauge rows + POOLS + EFFECTS, and the
   measured rect matches `panel_h( 26 )`. Nothing overlaps the log below it.
3. The figure is six filled boxes in a head / arm-torso-arm / leg-leg arrangement, each on the
   neutral ladder — no red, no second hue.
4. Damage one limb via the debug menu (`ESC` → `b` → `p` → `h`): that limb's box changes rung and,
   in a frame captured within ~260 ms, is measurably offset/dimmed relative to a settled capture.
5. **The idle cost is unchanged.** With the card collapsed and nothing animating, confirm
   `sidebar_requires_animation()` is false — the honest check is that the game is not redrawing:
   capture two frames ~2 s apart with the avatar standing still and assert they are byte-identical
   outside the clock field. A permanently-animating HUD makes them differ.
6. `debug.log` contains no `unknown rcss token`, no RCSS parse error, and no `hud_anim` warning.

Back up `~/Library/Application Support/Cataclysm-BN/save` before step 4 — `AUTOSAVE` defaults on
(`options_registration.cpp:313-316`), so debug damage can be written to the user's character by a
timer rather than by anything you type.

## What the build actually found

Recorded because three of these cost real time and none is discoverable from the code alone.

1. **The figure's fill ladder must not start below `bg4`.** The first draft started at `bg2`
   (luma 74) so a healthy limb would "recede". Over the panel's own veil it receded out of
   existence: a near-full-health body — the common case — drew six boxes peaking at luma 69 and
   measured as an empty panel. `figure_fill_class_walks_the_ladder_and_stays_visible` pins the
   floor. The ladder is four levels, not five: the step between `fg` and `fg0` is 20 luma, under
   the just-noticeable difference on a 10 dp box.
2. **F4–F12 never reach the action pipeline** — see the note in step 4. Binding one produces an
   action that silently never fires *and* flips a lighting debug global, which then contaminates
   every pixel measurement taken afterwards.
3. **The CRT faceplate darkens the left column.** `#hud-crt-curve`'s radial vignette multiplies
   everything near the screen edge, so the figure's `bg4` boxes measure ~66 rather than ~113.
   That is uniform across the column — the limb rows' text is darkened identically — so it is not
   a bug, but any absolute-luma assertion about SOMA content has to be made against a neighbouring
   element in the same column, never against the token value.
4. **Autosave will write to the save while you verify.** `AUTOSAVE` defaults on, so a session
   spent pressing keys in someone's world commits those turns on a timer. Back up
   `~/Library/Application Support/Cataclysm-BN/save` before starting and restore it after; a
   post-hoc mtime check detects the write but cannot undo it.

### Verified in the running build

- Collapsed is the default; its bottom border measured at y=291 against a predicted 291.6 px.
- `CTRL+B` expands; bottom border y=433 against a predicted 433.8 px — the expanded height did
  **not** grow, which is what putting the figure beside the rows bought.
- The figure renders as head / arm-torso-arm / leg-leg, every box within a pixel of its RCSS size.
- **Idle cost is zero**: over 2.5 s with the avatar still, the figure changed 0 px and both opaque
  strips were byte-identical. Only the translucent panels changed, and only where the game's own
  map animation shows through them.
- The card tween interpolates: with its duration temporarily raised to 2400 ms, the torso box
  measured RGB (40, 39, 36) mid-flight against (67, 61, 56) settled — the `on_change` alpha ramp
  caught mid-interpolation on the element the toggle addresses. Duration restored to 180 ms.
- The per-limb `hud_limb` hit flash was NOT observed live: producing damage safely needs the debug
  menu, and reading that menu needs a vision model that times out on this machine. It rides the
  identical path as the verified card tween, differing only in trigger keyword and element id.

### The full suite only reaches 350 of 898 cases

`coop_inproc_test.cpp:100` SIGSEGVs and terminates the run. At `--rng-seed 3458997103` the
unfiltered suite reports 350 test cases; excluding the crashing family with `~[coop]` it reports
**898**. More than half the suite therefore never executes in a default run, and — worse for
attribution — adding test cases reshuffles which half, so a before/after comparison at a fixed
seed silently compares two different subsets.

Attribution runs should use `./cata_test-tiles "~[coop]" --rng-seed <SEED>` so the suite completes.
With that, this change's run is 898 cases / 5 failed, every failure in `vehicle_efficiency_test`,
`vehicle_test` or `vehicle_ramp_test` — the vehicle-physics families already attributed
pre-existing by a stash-and-rebuild baseline at the same seed. Nothing outside them fails, which
is the claim that matters: the ~548 cases the aborted run never reached do run here, and none of
them regressed.

The SIGSEGV itself is pre-existing and out of scope for this plan, but it is the single highest-
value thing anyone could fix in this test suite.

## Assumptions & contingencies

- **Assumption: six flex boxes read as a body.** They are blocky, and that is the register.
  *If it reads as bar charts rather than a figure*, add a second row to the torso and taper the
  leg boxes — still pure flow, still six ids. Do NOT reach for `position: absolute` or a rotated
  transform; neither works in these documents.
- **Assumption: `scale` on a fixed-size flex box animates without disturbing flow.** `hud_anim`
  writes `transform`, which does not reflow. *If the neighbouring boxes jump*, drop the `scale`
  channel and keep `alpha` alone — the flash still reads.
- **Assumption: `hud_anim`'s `color_blend` is live.** `hud_anim.h:22-24` says it is unsupported,
  but `hud_anim.cpp:196-208` implements it, guarded on `blend_color != c_white`. This plan does
  not use it; if a later revision wants a red flash it must first fix that stale header comment.
- **Scope edge:** the six `gauge_row` limb rows, the pools and the effects roster are carried
  across UNCHANGED. This change is about what wraps them.
