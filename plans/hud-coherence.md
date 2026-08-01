# HUD Coherence — Research & Execution Plan

## Context

The sidebar HUD "feels somewhat disconnected". This document is the research
deliverable: what actually causes it, what the reference games do instead, and a
phased plan to make the HUD read as one instrument panel.

Scope is the RmlUi HUD document `data/gui/sidebar_hud.rml` + `sidebar_hud.rcss`,
its producers in `src/panels.cpp`, the motion layer in `src/hud_anim.cpp` /
`src/sidebar_anim.cpp` / `gfx/widgets/icons.json`, and the procedural frame in
`src/lighting/rmlui_layer.cpp` / `rmlui_proc_texture.cpp`.

Prior art: `plans/qud-hud.md` (the original build plan — Caves-of-Qud language,
teal HUD palette, inset ALL-CAPS headers) and `plans/RMLUI_HUD_PANEL_REFERENCE.md`
(per-panel content catalog). Two decisions locked there are load-bearing here and
are revisited in §3.4 and §5: *"Qud teal palette scoped to the HUD only …
producer text colors remain the game palette"* and *"No icon pop animations in
this pass"*.

---

## 1. Diagnosis — why it reads as disconnected

Six findings, ordered by how much each contributes. Every claim carries a
file:line citation.

### 1.1 The shared frame device covers only half the HUD  ← biggest single cause

There is already a procedural "runic frame" system: `apply_crt()` sweeps every
open document for `.panel`, `.runic-region` and `.runic-edge-top/bottom`, and
composes an 8-decorator ornate frame per element, honouring `runic-no-{top,
bottom,left,right}` edge suppression (`rmlui_layer.cpp:874-1093`).

Coverage in the HUD today (`sidebar_hud.rml:13,19,24,29,48,53`):

| Region | Frame class | Result |
|---|---|---|
| `#hud-topbar` | `runic-edge-bottom runic-no-top` | thin rune rule, bottom edge |
| `#hud-vitals` | *(none)* | no shared chrome at all |
| `#hud-vehicle` | *(none)* | plain 1dp box border |
| `#hud-dock` | `runic-region runic-no-right` | full ornate frame, 3 sides |
| `#hud-botbar` | `runic-edge-top runic-no-bottom` | thin rune rule, top edge |
| `#hud-hotbar` | *(none)* | nothing |

One region gets an ornate frame, two get a single hairline, three get nothing.
That is the disconnection, almost by itself.

**And the unifying device is in its own palette.** The rune ink defaults to
`#a1885f` warm brown (`rmlui_proc_texture.h:16`) while the chrome it frames is
teal `{{hud-border}} #2a5a50`. The thing meant to tie the HUD together is
visually foreign to it.

### 1.2 Three palettes on screen at once

| Family | Where | Values |
|---|---|---|
| HUD teal chrome | region backgrounds, borders, bars | `hud-bg #0a1a1ae0`, `hud-border #2a5a50`, `hud-fg #80c0a8`, `hud-bar-bg #123030` |
| Gruvbox warm | *all* game text — log messages, needs, threats | `game_colors` in `theme.json:30-47` |
| Runic brown | the frame | `#a1885f` |
| Gold accent | headers, fresh-row rule | `hud-accent #c8a820` |

Gruvbox `c_light_cyan #8ec07c` and `c_red #cc241d` render inside teal panels
under a gold header framed in brown. This was a deliberate locked decision in
`plans/qud-hud.md:9`, so it needs a *decision*, not a silent fix — see §5.

Compounding it, seven literal hexes shadow or duplicate tokens
(`sidebar_hud.rcss:59,114,122,136,139,142,241,250,340,432`):
`#e05050ff` = `{{hud-red}}`, `#2a5a50cc` = `{{hud-border}}` + alpha,
`#c8a8201c` = `{{hud-accent}}` + alpha, and `#888888ff` appears twice for two
unrelated roles.

### 1.3 No spacing unit

Distinct values in use: **0, 1, 2, 3, 4, 5, 6, 8, 12, 13, 14, 16, 18, 20 dp.**
The same conceptual gap differs by region — strip padding is `4dp 10dp`
(`rcss:25,40,69`) but the hotbar directly below it is `3dp 10dp` (`rcss:77`).
Dock padding is `8dp 14dp 20dp 14dp` (`rcss:171`) — a 2.5× vertical imbalance.

### 1.4 Five type sizes with near-duplicates

`0.75em` / `0.8em` / `0.85em` / `0.9em` / `1em`, and letter-spacing is `2dp` on
`.dock-header` but `1dp` on `.veh-name` (`rcss:203,408`). `0.8em` serves both
"label" and "timestamp"; `0.85em` serves both "bar text" and "section header".
There is a hierarchy trying to exist here; it just has not been named.

### 1.5 Structural asymmetry between comparable regions

Only the dock has section headers (`.dock-header`, `rcss:201-208`). The strips
are bare. Only `#hud-vehicle` has a full box border (`rcss:384`); every other
region has a single edge. `#hud-vitals` has no background at all, so it floats
ambiguously between HUD and world. And the vitals and vehicle panels are both
positioned with a magic `top_rows_pct + 1.0f` (`panels.cpp:1506,1516`) — a
hardcoded 1% that has no relationship to the topbar's actual height.

### 1.6 The motion layer has no shared language — and one live perf bug

Two independent animation systems write the same properties:

- **C++** (`sidebar_anim` + `hud_anim`), data-driven from `icons.json`, Penner
  easing, value-triggered — 23 animations across **15 distinct durations**
  (150/200/250/280/300/320/350/400/450/600/800/1000/1200/6000/12000 ms).
- **RCSS** `transition: background-color 0.8s, opacity 0.8s` on `#hud-topbar,
  #hud-botbar, #hud-dock, #hud-vitals, #hud-vehicle` (`rcss:459-461`).

They collide on `opacity` and `background-color` for five of six regions. The
RCSS rule wins, so **every C++ easing curve on those elements is silently
discarded**: a 280 ms `sine_in_out` critical flash becomes an 800 ms linear ramp
down, then `RemoveProperty` triggers another 800 ms linear ramp back — 1600 ms
of mush where a 280 ms pulse was specified.

Three more defects found in the same pass:

- **`repeats: 0` never settles** (`ui_tween.cpp` `settled()`), and six ambient
  specs omit `repeats` — `status_poison/fire/bleed/rad`, `spark`, `compass`.
  `sidebar_requires_animation()` therefore stays true forever, pinning the input
  timeout to 33 ms and **forcing a full-screen redraw every frame whenever the
  player has any status effect** (`handle_action.cpp:255-264,313-317`, which
  already carries a TODO about the cost).
- **`pivot_y` is dead config.** Parsed and stored (`sidebar_anim.cpp:197-199`)
  but never emitted as `transform-origin`, so the heart/droplet/food
  squash-recoil specs all squash from centre instead of from the struck edge.
  Six specs, no effect.
- **Stale `transform` string.** `scale_set`/`scale_y_set`/`rotation_set` are only
  set inside their non-identity branches (`hud_anim.cpp:154,158,162`), so a
  channel returning to identity while another is still animating leaves its term
  missing from the composed string.

Plus coverage gaps: the hotbar has no motion at all, the minimap updates
instantly, the target bar snaps, and the vehicle panel pops via `display`
(`panels.cpp:1698`) which is not transitionable.

### 1.7 No proximity contrast inside the dock

Proximity is the strongest grouping cue, and the dock spends it on nothing: the
gap *between* sections (`.dock-section { margin-bottom: 8dp }`, `rcss:180`) is
exactly the dock's own inner padding (`padding: 8dp 14dp 20dp 14dp`, `rcss:171`).
A section is therefore no more tightly bound to its own contents than it is to
the panel edge, so the minimap and the log read as two unrelated things sharing a
box rather than two sections of one panel.

### 1.8 Three of the five environmental tints are probably dead  [UNVERIFIED]

Not caught by any audit. `#hud-topbar/botbar/hotbar/dock/vehicle` set
`background-color` via **ID** selectors (`rcss:38,67,75,166,383`, specificity
1,0,0). `.env-fire`, `.env-rad` and `.env-cold` try to override it via **class**
selectors (`rcss:453-455`, specificity 0,1,0).

If RmlUi applies CSS-standard specificity — which its RCSS documentation states,
but which I have **not** confirmed against source (RmlUi is fetched, not vendored;
there is no `StyleSheetNode.cpp` in-tree) — the ID wins and those three tints have
never rendered. The counter-argument is source order: `.env-*` at `rcss:452-455`
comes *after* `#hud-dock` at 166, so under equal specificity the class would win
instead. The entire finding hinges on which rule decides.

**Settle it empirically before relying on it**: debug-apply a fire or radiation
effect and pixel-diff the dock background. One in-game check, ~2 minutes.

What *is* certain either way: `.env-night` and `.env-storm` work, they work via
`opacity`, and `opacity` is the channel hud_anim also writes — so the half of the
env system that definitely functions is the half that collides.

---

## 2. What the references do

Full teardown in the research artifacts; the transferable devices:

**Cogmind** (gridsagegames.com/blog — the closest analogue, ASCII roguelike with
an exceptionally coherent full-screen HUD):
- Every panel sits on one cell grid; borders are drawn as *continuous* rules that
  join at corners, so separate windows read as one chassis.
- ~8 core HUD colours total, with global brightness/saturation/hue filters applied
  *uniformly* — data colours are never allowed to escape the system.
- Colour is never the only channel; state is also carried by symbol and position.
- Animation is state-triggered and always paired with sound; env transitions are
  slow and smooth (~0.8 s) but *event* feedback is fast.

**Caves of Qud** (the stated inspiration): an 18-colour fixed palette, three
colours per tile. Restriction is the whole technique.

**Prey (2017)** is the instructive failure: its original HUD was widely criticised
as disconnected from the game's aesthetic, and the well-known redesign fixes it
almost entirely with *chrome* — consistent geometric borders, CRT field, one
palette — without moving a single element.

The pattern across all of them: **coherence comes from a shared chassis and a
restricted palette, not from re-laying-out the regions.** That is good news here,
because the region rects are fixed by `sidebar_hud_apply_rect()` and we do not
want to move them.

---

## 3. The design system to adopt

### 3.1 Spacing — one 4dp unit

Every padding, margin and gap becomes a multiple of 4: **4 / 8 / 12 / 16 / 20**.

| Role | Value |
|---|---|
| strip padding | `4dp 12dp` |
| dock padding | `8dp 12dp` (bottom inset handled by the frame reserve, not ad-hoc 20dp) |
| intra-row gap | `4dp` |
| inter-section gap | `8dp` |
| section rule margin | `8dp 0dp` |

### 3.2 Type — three named tiers, not five sizes

| Tier | Size | Weight | Letter-spacing | Colour | Used for |
|---|---|---|---|---|---|
| `header` | 0.85em | bold | 2dp | `{{hud-accent}}` | section titles |
| `label` | 0.8em | normal | 1dp | `{{hud-fg-muted}}` | `ID`/`COND`, timestamps, fuel labels |
| `body` | 1em | normal | 0 | `{{hud-fg}}` / data colour | values, messages |

Collapses `0.75/0.9em` away and unifies the two letter-spacings.

### 3.3 Colour — two new tokens, zero literals

Add to `theme.json`: `hud-fg-muted #888888ff`, `hud-ink #0d0d0dff`,
`hud-crit-bg #401010ff`. Replace all seven shadowing literals with tokens.

### 3.4 One edge grammar — the key move

Pick **one** and apply it to all six regions:

- **(A) Hairline chassis** *(recommended first)* — a single 1dp `{{hud-border}}`
  rule on exactly the edges that face the play area, on every region, with the
  corners actually joining where the strips meet the dock. Cheap, legible,
  immediately coherent.
- **(B) Runic everywhere** — give every region `runic-region` with correct
  `runic-no-*` suppression so the procedural frame traces the whole screen
  boundary. More distinctive, more expensive, and it must be recoloured first.

**Rune ink is NOT HUD-local — see §5b before touching it.** `runic_cfg()` is a
single global singleton (`rmlui_proc_texture.h:57`, `rmlui_proc_texture.cpp:522`)
persisted to `config/runic_frame.json`, and `apply_crt()` reads it once for
*every* open document (`rmlui_layer.cpp:861`). Changing `col_r/g/b` repaints the
frame on the main menu, inventory, crafting and every other `.panel` too. Same
shared-resource trap as `game_colors`.

The strong version is A + runic **corners only** at the four junctions — that is
Cogmind's corner-joinery device, and it costs nothing along the edges.

### 3.5 Motion — five durations, two easing families

| Slot | Duration | Easing | Use |
|---|---|---|---|
| `snap` | 150 ms | `quad_in` | damage flash onset |
| `entry` | 200 ms | `back_out` + `quad_out` | log rows, things arriving |
| `recoil` | 250 ms | `back_out` | value changes, icon pops |
| `emphasis` | 400 ms | `sine_in_out` | critical pulses |
| `ambient` | 800 ms | `sine_out` / `linear` | recovery fades, env tint, continuous spin |

**The governing split** (Disney/IBM Carbon/Material all draw it): *feedback*
answers "did my action register?" and must be fast (150-250 ms); *context shift*
reports a change in the world the player is not waiting on, and should be slow
(800 ms+). The current RCSS rule is a sledgehammer that catches both classes,
which is why event feedback feels laggy. 800 ms is right for env tint — it is
wrong that the same rule also governs critical flashes.

Remaps: heart/droplet/food 320→250, spark 300→250, wind 350→250, moon 450→400,
heart-critical 280→400, vbar-increase 200→250. Everything else already lands on a
slot.

---

## 4. Phases

Each phase is independently shippable and independently verifiable.

### Phase 1 — token hygiene *(mechanical, no visual change intended)*
Add the three tokens; replace the seven literals. Ship separately so any visual
delta in later phases is unambiguous.

### Phase 2 — spacing + type scale
Apply §3.1 and §3.2 across `sidebar_hud.rcss`. Fix the hotbar's 3dp, the dock's
asymmetric 20dp, and the two letter-spacings.

### Phase 3 — one edge grammar
§3.4 option A. Recolour the rune ink. Add the label tier to the strips so they
stop being bare relative to the dock. Give `#hud-vitals` a background so it reads
as HUD rather than world, and reduce `#hud-vehicle` from a full box to the same
grammar as everything else.

### Phase 4 — connective tissue
Make the topbar-bottom / dock-left / botbar-top rules meet at their corners
(today they are three independent declarations that nearly touch). Replace the
magic `+ 1.0f` on vitals and vehicle (`panels.cpp:1506,1516`) with an offset
derived from the topbar's real height, so the overlays align to the inner
boundary.

### Phase 5 — motion unification
1. **Fix the transition's property scope** (`rcss:459-461`). This is a
   property-sharing bug, not a duration choice — the two channels behave
   differently:
   - `background-color` is **uncontested**: only `.env-fire/.env-rad/.env-cold`
     write it (`rcss:453-455`) and no `color_blend` spec exists. The 0.8 s
     transition on it is correct — keep it.
   - `opacity` is **contested**: `.env-night` and `.env-storm` use it
     (`rcss:452,456`) *and* hud_anim drives it for every flash, pulse and log
     fade. Drop `opacity` from the transition; that single deletion restores
     every C++ easing curve on all five regions.
   - **The fix must escape the ID cascade** (§1.8) — a class rule cannot repaint
     an ID-set background. Options: a dedicated tint **underlay** element per
     region; compound `#hud-dock.env-fire` selectors; or driving the tint from
     C++ inline (inline beats ID). **Underlay recommended**: it is the only one
     that also frees region `opacity` completely for hud_anim, and it revives the
     three dead tints in the same move.
   - **`opacity`'s second job is a design call, not a bug fix.** Region opacity
     does not merely dim — it makes the whole HUD *translucent to the terrain
     behind it* at night and in storms. Moving the tint to an underlay wash
     removes that see-through effect. Keep it only if it was intentional; if so,
     the underlay carries the colour tints and region `opacity` stays as a
     separate, deliberate night treatment applied where hud_anim never writes.
   - Either way this kills a live flicker: hud_anim's inline
     `SetProperty("opacity")` outranks the `.env-night` class rule, so night
     dimming is silently defeated on any region mid-animation and snaps back when
     the tween settles.
2. Remap all `icons.json` durations onto the five slots.
3. Fix `pivot_y` → emit `transform-origin`.
4. Fix the stale-`transform` bug.
5. Give the status pulses finite `repeats` so they settle — this also fixes the
   30 fps / full-redraw pin. Introduce `repeats: -1` as *explicit* infinite for
   spark/compass.
6. Animate the vehicle panel with opacity+offset instead of `display`.

### Phase 6 — coverage
Hotbar feedback, target-bar easing, minimap update softening.

---

## 5. Decision required — the gruvbox-in-teal collision

`game_colors` is shared between the HUD and every menu, so this cannot be fixed
unilaterally. Three options:

1. **Keep as-is.** Game text stays "the game's colours". Zero work, but this *is*
   the colour half of the disconnection.
2. **Re-tune `game_colors` toward the teal family.** Coherent HUD, but it
   recolours text in every menu too.
3. **Add a HUD-scoped `nc_color`→hex map** (a `hud_colors` block in `theme.json`)
   consulted only by the HUD producers. Menus untouched, HUD coherent; costs a
   small indirection in `nc_color_to_hex`. **Recommended.**

## 5b. Decision required — the rune ink is global too

Same shape of problem. `runic_cfg().col_r/g/b` (default `#a1885f`) is one
singleton read by `apply_crt()` for every open document, so the frame colour
cannot be changed for the HUD alone. Options:

1. **Recolour globally.** One line; the whole UI's frame goes teal-ward. Loses
   the warm gruvbox frame the menus were designed around.
2. **Leave it warm and drop the runic frame from the HUD** (§3.4 option A only,
   no runic corners). Zero engine change; the HUD simply stops using a device
   that does not match it.  **Cheapest coherent answer.**
3. **Per-element ink.** Add a second colour to `runic_params` and key the
   procedural textures on it (`?proc:runic-hedge` → `?proc:runic-hedge-hud`), so
   HUD regions render teal ink while menus keep warm. Contained but real work in
   `rmlui_proc_texture.cpp` + the texture cache key. Only worth it if we want
   §3.4 option B or the corner accents.

---

## 6. Verification

Per phase: build `cataclysm-bn-tiles`, install, drive the installed build, and
capture the HUD at 1440p. Specifically:

- Phases 1-2: pixel-diff before/after — Phase 1 should be ~0% changed; Phase 2
  should change only spacing.
- Phase 3-4: visual — every region shares one edge grammar; the play-area
  boundary reads as one continuous rule; no corner gaps.
- Phase 5: the perf fix is measurable — apply a status effect (debug menu) and
  confirm the input timeout is no longer pinned at 33 ms once the pulse settles
  (`sidebar_requires_animation()` returns false). Confirm a critical HP flash
  actually lasts ~400 ms rather than ~1600 ms.
- No formatter available on this machine (neither `astyle` nor `clang-format` is
  on PATH, so `--target format` does not exist); match surrounding style by hand.

---

## 7. Ranked by coherence-gain per unit of effort

| # | Lever | Effort | Why |
|---|---|---|---|
| 1 | One edge grammar on all six regions (§3.4 A) | S | The single biggest cause; RCSS + a few classes |
| 2 | Delete the RCSS/C++ transition conflict | S | Restores every animation's intended timing on 5 of 6 regions |
| 3 | 4dp spacing unit + 3-tier type | S | Mechanical, high yield |
| 4 | Token hygiene | XS | Enables everything else to be themed at once |
| 5 | Finite `repeats` on ambient pulses | XS | Fixes a real 30 fps + full-redraw bug |
| 6 | HUD-scoped `game_colors` (§5 option 3) | M | Biggest *colour* win; needs a decision |
| 7 | Rune-ink resolution (§5b) | XS–M | Removes a whole foreign palette; cost depends on the option chosen |
| 8 | Corner joinery + kill the magic `+1.0f` | M | Turns adjacency into connection |
| 9 | Duration remap onto five slots | M | Makes the HUD feel choreographed |
| 10 | `pivot_y` + stale-transform fixes | S | Six specs currently do nothing |

---

*Research: 2026-07-31. Sources — Cogmind dev blog (7 posts, gridsagegames.com),
Caves of Qud wiki Visual Style, Prey 2017 UI redesign critique, UnderRail wiki,
Intravenous 2 dev news, plus Gestalt/Material/IBM Carbon motion literature.
Internal audits cover `sidebar_hud.rcss`, `sidebar_hud.rml`, `theme.json`,
`panels.cpp:700-1780`, `hud_anim.*`, `sidebar_anim.*`, `ui_tween.*`,
`icons.json`, `rmlui_layer.cpp`.*

---

## 8. Execution record — 2026-07-31

All six phases implemented and installed. Verified by driving the installed
Windows build against the Kendale Lakes save (launched with `--world "Kendale
Lakes"`, which bypasses menu navigation entirely — do this instead of SendKeys
menu driving).

### Landed

| Phase | Change |
|---|---|
| Tokens | `hud_colors` override map in `theme.json` + `ui_theme` load/lookup/save path; `hud_color_to_hex()` / `cata_text_to_rml_hud()` in `rml_util`; all 29 HUD producer call sites in `panels.cpp` routed to it, the 4 sidebar-options *menu* sites deliberately left on `game_colors`. New chrome tokens: `hud-accent-wash`, `hud-crit-bg`, `hud-red-bright`, `hud-scroll-thumb`, `hud-bg-fire/rad/cold`. |
| System | One 4dp spacing unit across all six regions; three type tiers (label 0.8em / body 1em / header 0.85em+bold+2dp tracking). |
| Chassis | All `runic-*` classes dropped from the HUD document — one hairline `{{hud-border}}` edge grammar instead (play-area-facing edge on anchored strips, full box on the two floating panels). Vitals gained its box. HP gradient derived from the palette instead of `#40c040`/`#c0c040`/`#e05050` literals; crit fill moved into the RCSS. The `+1.0f` overlay offsets replaced with a measurement of the topbar's real border box + one 8dp gutter. |
| Motion | 15 durations collapsed to 5 slots (150/200/250/400/800) plus two intentional continuous rotations; ambient status pulses given `repeats: 3`; `pivot_y` finally emitted as `transform-origin`; the stale-transform bug fixed (per-channel `*_set` flags no longer gate individual terms). |

### Corrections made during execution

- **`#hud-hotbar` was never fed env classes at all** (`apply_env_classes` covered
  five of six regions). Added, so the new `#hud-hotbar.env-*` rules aren't dead.
- **Finite `repeats` needed a matching prune.** `ambient` specs start once per
  key, so a settled `status-*` key would never pulse again on a second bout.
  `panels.cpp` now forgets status keys that drop out, mirroring the log's prune.
- **`.vbar.thin` clipped its own text.** 11dp bar vs 0.8em-of-18dp text: STA and
  MANA were sliced mid-glyph. Bars are 16dp and `.vbar-text` now pins
  `line-height` to the bar height. *(This one was a live defect the audits
  missed, not a regression from this pass — visible in the first capture.)*

### Verified in-game

- Every token resolves; zero parse errors from `sidebar_hud.rcss`, `theme.json`
  or `icons.json` (the four remaining RCSS errors are pre-existing, in
  `mainmenu_theme.rcss`, `string_input.rcss` and `loading.rcss`).
- HP at three states in one frame: 93/93 green, 40/93 palette gradient, 15/93
  crit — hot fill (`hud-red-bright`), dark-red trough, red text and ` !!`, all
  from the theme.
- STA/MANA render in full inside the vitals box, bottom border intact.
- Newest log row fully visible at the well floor with its accent rule; older
  rows age-ramped and top-dissolved.
- `transform-origin` confirmed live: RmlUi registers it
  (`StyleSheetSpecification.cpp:394-397`) and `SetProperty` expands shorthands.

### Not observed — reasoned only

- **Env tint colours.** The fix is a cascade correction (`#hud-x.env-fire`
  (1,1,0) beats `#hud-x` (1,0,0); the old bare `.env-fire` (0,1,0) lost). The
  selectors compile, but no fire/radiation/cold state was driven in-game.
- **Vehicle panel at the new 1em.** `.veh-row` keeps `flex-wrap: wrap`, and the
  widest fuel row (60dp label + 60dp bar + 30dp pct + 16dp gaps = 166dp) sits
  well inside the 300dp cap, so overflow is implausible — but unobserved.

### Pre-existing, out of scope

Curses popups (`Mouse View`, control-vehicle menu) render *underneath* the
RmlUi topbar. That is the known RmlUi-over-curses z-order issue, not a
consequence of the vitals chrome — the popup sits at y≈12, inside the topbar
band, above the vitals panel's top border at y≈83.
