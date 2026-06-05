# Sidebar Widget-Engine Migration — Stage 2 Audit (divergence & go/no-go)

Tracking doc for porting CDDA's data-driven sidebar widget engine to BN
(plan: `piped-hatching-reef`). Stage 1 (responsiveness) shipped in `c0acc17665`.
This file is the **Stage 2 divergence audit** + the seed conversion inventory.

**Status:** Stage 1 shipped · Stage 2 GO · **Stage 3 done** (engine core +
native-wrapper bridge, green build + tests). Next: Stage 4 (value wiring).

### Stage 3 — what landed (2026-06-02)

- `src/widget.{h,cpp}` — trimmed `widget` class: `generic_factory` + JSON load
  of structural fields (`style`, `label`, `width`, `height`, `native`, `flags`,
  `widgets`). Deferred (BN lacks the deps): `widget_var`/`get_var_value`,
  `widget_custom_var` (no `dbl_or_var`), `widget_clause` conditions, `display::`
  text helpers. `widget_id` added to `type_id.h`.
- `init.cpp` — registered `add("widget", &widget::load_widget)` + reset +
  `finalize_all` in the finalize map.
- `src/panels.cpp` — `make_native_widget_panel(const widget&, int)`: the parity
  bridge. Delegates a `style:"native"` widget's draw to the existing **static**
  `draw_*` fn named by `widget::native()`, via a name→fn registry that lives in
  panels.cpp (the `draw_*` are TU-static). Registry currently
  `{draw_stats, draw_messages}`; unknown target → log-once + no-op draw, never
  crash. Built on BN's **existing** `window_panel` ctor — the CDDA `wgt`-backing
  (`set_widget`/`get_widget`) is NOT added yet (deferred to Stage 5/6).
- `data/json/ui/sidebar.json` — seed: two `native` widgets + one `sidebar`
  container. (Provisional proof data; the real default layout is built Stage 4+.)
- `tests/widget_test.cpp` — 2 cases / 16 assertions, all pass: factory loads the
  JSON, native style + target parse, dispatch resolves `draw_stats` / rejects
  unknown, panel binds metadata + non-null draw, container lists children.
- Build: `cata_test-tiles` + `cataclysm-bn-tiles` both link green (0 errors).
  Note: adding a new `tests/*.cpp` needed an explicit `cmake` reconfigure — the
  `file(GLOB)` CONFIGURE_DEPENDS did not re-glob on file-add.

**Review fixes applied (post Stage 3):**
- `widget::height()` returns `_height` **unclamped** — BN encodes flex panels as
  negative heights (Log `-2`, Map `-1`); clamping to ≥1 would make a native
  Log/Map impossible.
- The native panel's name is keyed on the **widget id** (`getId().str()`), not
  the translated label — BN `window_panel` names are untranslated English keys
  used for save/load matching and re-localized via `_()` at display.
  `translation::untranslated()` is `=delete`, so the id is the only stable key;
  `widget::label()` holds the display label for Stage 6.
- `make_native_widget_panel` clamps `panel_width` to ≥1 (a 0-width first panel
  collapses the sidebar via `update_offsets`).
- `widget::check()` validates `_widgets` child ids resolve; registered as
  `widget::check_consistency` in `DynamicDataLoader::check_consistency`.
- Dropped the once-only `warned` static (builder runs once per panel at
  layout-build, not per frame) → plain `debugmsg`; `load()` takes
  `const std::string&` to match the codebase convention.

CDDA source referenced: `CleverRaven/Cataclysm-DDA@master` `src/widget.h`,
`src/panels.h`, `data/json/ui/sidebar.json` (fetched 2026-06-02).

> **Evidence grade.** BN-side facts (Part A2/A3/A4, the `draw_*` surface) were
> read directly from BN source and are firm — the GO rests on these. The
> **CDDA-side** structural claims (Part A1 `window_panel`/`panel_layout` shapes)
> and the **var table (Part B)** are derived from a WebFetch *summary* of CDDA
> headers + the plan, **not verbatim source**. Treat them as a first pass:
> re-fetch and confirm the few load-bearing CDDA signatures verbatim before
> Stage 3/5 writes any port code.

---

## Part A — Skeleton drift check (Stage 2.1)

**Verdict: skeleton holds.** The `panel_layout` wrapper + additive `window_panel`
plan is sound. One blast-radius site more than the plan enumerated; all
mechanical. One naming collision to avoid.

### A1. `window_panel` — BN vs CDDA

| Aspect | BN (`panels.h:38`) | CDDA | Migration |
|---|---|---|---|
| `draw` signature | `void(avatar&, const catacurses::window&)` | `int(const draw_args&)` (returns height diff) | **Keep BN's.** Do NOT adopt `draw_args`. CDDA `get_window_panel` builds an `int(draw_args)` lambda → must be re-wrapped to BN's `void(avatar&,window&)` when ported (Stage 5). Known port friction, not a skeleton break. |
| `render` | `std::function<bool()>` | same | none |
| `dynamic_height` | `std::function<int()>` (Stage 1) | — (CDDA uses `_height`/`_height_max` on widget) | BN-ahead; coexists |
| widget backing | none | `widget_id wgt` + `set_widget`/`get_widget`/`get_id` | **Additive** — add `widget_id wgt`, `set_widget`, `get_widget`, `get_id` |
| private | `height,width,default_toggle,name` | `height,width,wgt,id,name` | add `wgt`,`id` |
| bools | `toggle`,`always_draw` | `toggle`,`always_draw` | none |

`window_panel` changes are **purely additive** — no existing signature breaks.
Stage 1 already proved additive growth works (`dynamic_height` added cleanly).

### A2. `panel_manager` — storage migration

| Aspect | BN (`panels.h:64`) | CDDA | Migration |
|---|---|---|---|
| layout storage | `map<string, vector<window_panel>>` | `map<string, panel_layout>` | wrap vector in `panel_layout` |
| `get_current_layout()` | returns `vector<window_panel>&` | returns `panel_layout&` | change return type; body is a trivial map lookup (`panels.cpp:2555`, `:2563` is defensive reset-recursion) |
| layout name | none (key only) | `panel_layout{ translation _name; vector _panels; }` | adds display name + `panels()` accessor |
| lua bridge | `sync_lua_panels` + `lua_panel_names` | **absent** (CDDA has no lua sidebar) | BN-only; preserve through migration (Stage 6 rework) |
| `get_current_sidebar`/`get_sidebar` | absent | `widget*` getters | add in Stage 6 |

### A3. `get_current_layout()` blast radius (verified by grep)

Plan claimed: *draw_panels (2 loops + width), update_offsets, show_adm*.
**Actual: 7 call sites, all mechanical** (`L` → `L.panels()`):

| Site | Use | Adapt |
|---|---|---|
| `game.cpp:4326` | draw_panels sum loop (range-for) | `.panels()` |
| `game.cpp:4340` | draw_panels draw loop (range-for) | `.panels()` |
| `game.cpp:4393` | `.begin()->get_width()` | `.panels().begin()->...` |
| `game.cpp:9369` | `.begin()->get_width()` — **NOT in plan; one extra** | `.panels().begin()->...` |
| `panels.cpp:2563` | defensive return (recursion) | return-type only |
| `panels.cpp:2590` | `update_offsets(...begin()->get_width())` | `.panels().begin()->...` |
| `panels.cpp:2958` | `.begin()->get_width()` | `.panels().begin()->...` |

No consumer inspects panel internals beyond iteration + `get_width()`. Migration
is a return-type change + 6 `.panels()` insertions. **Tiny, as the plan claimed.**

`get_current_layout_id()` (string) consumers — `ranged.cpp:1985/3066`,
`catalua_bindings_ui.cpp:209`, `panels.cpp:301` — use only the id **string** and
are **untouched** by the wrapper migration. ✓ Confirms plan.

### A4. Naming collision to avoid

BN **already** has a `panel_layout_entry` type + `saved_panel_layouts()` for
save/load (`panels.cpp:2606+`), distinct from CDDA's runtime `panel_layout`
wrapper. Importing CDDA's `panel_layout` alongside BN's `panel_layout_entry` is
fine (different names) but easy to confuse. Document both; do not merge them —
`panel_layout` = runtime collection, `panel_layout_entry` = serialized record.

---

## Part B — `widget_var` → BN value-source table (Stage 2.2)

CDDA `widget_var` has **~75 enumerators** (plan estimated ~50). Classification:
**direct** (BN getter exists), **adapt** (rename / wrap existing `draw_*` helper),
**unsupported** (BN lacks the system → placeholder + log-once, never crash).

**First-pass classification — confirm each row in Stage 4.** ~14 API symbols
were probed directly (`src/character.h`, `src/creature.h`, `src/avatar.h`); the
rest are inferred from the plan + the `draw_*` surface in `panels.cpp`. A
`direct` tag means "BN getter expected to exist", not "value path verified" —
Stage 4 does the exact wiring and is where these get confirmed.

### B1. Numeric vars

| widget_var | BN source | class | note |
|---|---|---|---|
| focus | `Character::focus_pool` | direct | |
| move | move counter | direct | |
| move_remainder | move counter | adapt | |
| move_cost | `run_cost` | adapt | |
| pain | `get_perceived_pain()` ✓ | direct | |
| sound | volume source (`draw_stealth`/`draw_sound_*`) | adapt | |
| speed | `get_speed()` | direct | |
| stamina | `get_stamina()` ✓ | direct | |
| sleepiness | `get_fatigue()` ✓ | **adapt** | CDDA renamed fatigue→sleepiness |
| health | `get_healthy()` (`draw_health_classic`) | adapt | |
| daily_health | — no getter | **unsupported** | revisit if `get_healthy_mod` fits |
| mana | `magic.available_mana()` (`draw_mana_*`) | direct | |
| max_mana | `magic.max_mana()` | direct | |
| power_percentage | `power_level`/`max_power_level` ✓ | adapt | |
| log_power_balance | — | unsupported | |
| morale_level | `get_morale_level()` ✓ | direct | |
| weariness_level | — no weariness system | **unsupported** | |
| weary_transition_level | — | **unsupported** | |
| stat_str | `str_string`/`get_str_base` ✓ | direct | |
| stat_dex | `dex_string` ✓ | direct | |
| stat_int | `int_string` ✓ | direct | |
| stat_per | `per_string` ✓ | direct | |
| bp_hp | `get_part_hp_cur()` (`draw_limb_health`) | direct | per-bp |
| bp_encumb | `get_part_encumbrance()` | direct | per-bp |
| bp_warmth | `get_part_temp_*()` | adapt | per-bp |
| bp_wetness | `get_part_wetness()` | direct | per-bp |
| mood | CDDA TODO | unsupported | |
| cardio_fit | — no cardio system | **unsupported** | |
| cardio_acc | — | **unsupported** | |
| carry_weight | `weight_carried`/`weight_capacity` (`draw_weightvolume`) | direct | |
| oxygen | `Character::oxygen` ✓ | direct | |
| custom | CDDA math-lang / variable_object | **unsupported** | **biggest gap** — depends on CDDA dialogue/math eval BN lacks. Skip; revisit only if a widget needs it. |

### B2. Text vars

| widget_var | BN source (`draw_*` bridge) | class |
|---|---|---|
| activity_text | `draw_hint` / activity | adapt |
| body_graph (+ _temp/_encumb/_status/_wet) | — (BN `draw_limb_*`, no body-graph) | **PORT** (×5) — scope add, see Part D |
| bp_armor_outer_text | `draw_armor*` | adapt |
| carry_weight_text / _value | `draw_weightvolume*` | adapt |
| compass_text | `draw_compass*` | adapt |
| compass_legend_text | compass legend | adapt |
| date_text | `draw_time*` | adapt |
| env_temp_text | `draw_env_compact` | adapt |
| faction_territory | — | unsupported |
| mood_text | — (no mood emote) | unsupported |
| move_count_mode_text | `draw_char_*` | adapt |
| overmap_loc_text | `draw_loc_*` | adapt |
| overmap_text | `draw_loc_wide_map` | adapt |
| pain_text | pain desc | adapt |
| place_text | `draw_location_classic` | adapt |
| power_text | power_stat | adapt |
| power_balance_text | — | unsupported |
| safe_mode_text / _classic_text | safe-mode | adapt |
| style_text | martial style | adapt |
| sundial_text / sundial_time_text | — (CDDA sundial) | unsupported |
| time_text | `draw_time*` | direct/adapt |
| veh_azimuth/_battery/_cruise/_fuel_text | `draw_veh_*` | adapt |
| weariness_text / weary_malus_text | — | **unsupported** |
| snow_depth_text | — | unsupported |
| weather_text | `draw_weather_classic` | adapt |
| wielding_text / _simple / _mode / _ammo | `draw_weapon_*` | adapt |
| wind_text | `draw_wind*` | adapt |

### B3. Summary counts

- **direct** ~15 · **adapt** ~30 · **port** 5 (body_graph family — scope add) ·
  **unsupported** ~15 (cardio×2, weariness×4, daily_health, mood×2, sundial×2,
  custom, faction_territory, power_balance, snow_depth, log_power_balance).
- The unsupported set is concentrated in **systems BN never forked in** (cardio,
  weariness, sundial) + CDDA's **custom math var**. None block the default
  sidebar: every value the current BN layouts display has a direct/adapt path via
  existing `draw_*` helpers (the native-wrapper bridge).

### Scope decision (2026-06-02, user)

- **Build out: body-graph only** (the 5 `body_graph*` vars) — see Part D.
- **Stay placeholder + log-once:** weariness, cardio, sundial, `custom` math var,
  daily_health, mood, faction_territory, power_balance, snow_depth,
  log_power_balance. No BN gameplay systems built to fill these.

---

## Part D — body-graph port (in-scope add)

BN has no body-graph; it shows limb data via `draw_limb_health/_narrow/_wide`
(per-limb HP rows). CDDA's body-graph is a color-coded whole-body diagram driven
by `body_graph` (+ `_temp`/`_encumb`/`_status`/`_wet`). To support these 5 vars:

- Port CDDA's body-graph **renderer** (`display::*body_graph*` / the graph layout
  in CDDA) adapted to BN's `bodypart`/`Character` API.
- Wire per-bp data sources: HP (`get_part_hp_cur`), temp (`get_part_temp_*`),
  encumbrance (`get_part_encumbrance`), status (bite/bleed effects), wetness
  (`get_part_wetness`) — most already probed as present in B1.
- Sequencing: this is **Stage 4+ work** (value wiring), gated behind the engine
  landing (Stage 3) and the native-wrapper parity bridge. Not on the Stage 3
  critical path. Confirm CDDA renderer source verbatim before porting.

---

## Part C — CDDA `sidebar.json` shape (Stage 3 input)

CDDA `data/json/ui/sidebar.json` is a **thin container**: one `type:"widget",
style:"sidebar"` object referencing **26 sub-widgets by id**. It defines **no
`var` fields inline** — the var-bearing widget definitions live in *separate*
JSON files. Implications:

- `var:"custom"` appears in **0** widgets of the reference layout → **"skip
  `custom`" costs nothing for default parity.** ✓ (confirms the recommendation)
- Stage 3's "minimal `sidebar.json` to prove parsing" can use this container
  shape, but reaching parity needs the **26 referenced widget definitions** too
  (separate CDDA JSON, to be located/ported in Stage 3/4).

---

## Part E — grounded conversion inventory (the porting checklist)

Authoritative list, read from BN's 4 default layouts in `panels.cpp`
(`initialize_default_{classic,compact,label_narrow,label}_panels`, default =
`labels`). ~53 distinct `draw_*` fns collapse into **23 logical widgets**. Each
is **native-wrapped first** (parity bridge, `make_native_widget_panel`), then
**converted to data-driven vars** by tier. Width variants collapse (vars are
width-agnostic; layout handles width).

Legend: ☐ native-wrapped · ☐ var-converted · **sig** = draw fn signature
(`a`=`avatar&`, `cp`=`const player&`, `ca`=`const avatar&`) — non-`avatar&`
needs an adapter in the native registry. **SVG** = strong candidate for a vector
icon (see Part F).

### Tier 1 — leaf value widgets
| ☐wrap ☐var | Logical | draw_* variants | var source | sig | SVG |
|---|---|---|---|---|---|
| ☐ ☐ | Stats | draw_stats · draw_stat_narrow · draw_stat_wide | stat_str/dex/int/per | a | – |
| ☐ ☐ | Movement | draw_char_narrow · draw_char_wide | move/speed/stamina | ? | – |
| ☐ ☐ | Sound | draw_stealth · draw_sound_narrow · draw_sound_labels | sound | ? | 🔊 icon |
| ☐ ☐ | Mana | draw_mana_classic/_compact/_narrow/_wide | mana/max_mana | **cp** | ✦ icon |
| ☐ ☐ | Needs | draw_needs_compact/_narrow/_labels | hunger/thirst/**fatigue**/pain | ? | 🍖💧 icons |
| ☐ ☐ | Wgt/Vol | draw_weightvolume_classic/_compact/_narrow/_labels | carry_weight/volume | ? | – |
| ☐ ☐ | Time | draw_time · draw_time_classic | time_text | ? | – |
| ☐ ☐ | Weather | draw_weather_classic | weather_text | ? | ☀☁ icon |
| ☐ ☐ | Lighting | draw_lighting_classic | light level | ? | – |
| ☐ ☐ | Wind | draw_wind · draw_wind_padding | wind/weather | ? | ➤ dir-arrow |
| ☐ ☐ | Health | draw_health_classic | morale/power/health | ? | – |

### Tier 2 — composite / multi-row
| ☐wrap ☐var | Logical | draw_* variants | var source | sig | SVG |
|---|---|---|---|---|---|
| ☐ ☐ | Limbs/HP | draw_limb2 · draw_limb_narrow · draw_limb_wide | bp_hp + **body-graph** | ? | **body silhouette** |
| ☐ ☐ | Armor | draw_armor · draw_armor_padding · draw_armor_comp | per-bp get_armor → bp_* | ? | – |
| ☐ ☐ | Vehicle | draw_veh_classic · draw_veh_compact · draw_veh_padding | veh vars | ? | – |
| ☐ ☐ | Location | draw_location_classic · draw_loc_narrow · draw_loc_wide_map · draw_loc_wide | place + overmap | ? | – |
| ☐ ☐ | Weapon | draw_weapon_classic · draw_weapon_classic_alt · draw_weapon_labels | wielding text | ? | – |
| ☐ ☐ | Env | draw_env_compact | temperature/weather | ? | 🌡 icon |
| ☐ ☐ | Moon | draw_moon_narrow · draw_moon_wide | moon phase | ? | **moon-phase disc** |
| ☐ ☐ | Hint | draw_hint | activity/hint | ? | – |
| ☐ ☐ | AI goal | draw_ai_goal | custom/text | ? | – (name hardcoded "AI Needs", no translate_marker) |

### Tier 3 — dynamic height (validates flex end-to-end)
| ☐wrap ☐var | Logical | draw_* variants | notes | SVG |
|---|---|---|---|---|
| ☐ ☐ | Compass | draw_compass · draw_compass_padding · draw_simple_compass | canonical dyn-height; 3 variants | **compass rose** |

### Native permanent — wrapped, NEVER converted to vars
| ☐wrap | Logical | draw_* | height | notes |
|---|---|---|---|---|
| ☐ | Log | draw_messages · draw_messages_classic | **-2** flex-fill | height-sentinel fix (#1) enables native-wrap |
| ☐ | Map | draw_mminimap | **-1** minimap | GPU pixel-minimap, genuinely special |

**Render-gated** (only drawn when a condition holds): Mana (`spell_panel`),
Vehicle (`veh_panel`). The widget needs an equivalent show/hide condition
(CDDA: `widget_clause`/flags; BN-trim: a render predicate, mirror Stage-1 lua
`render_func`).

**Signature audit (`sig` column) is a Stage-4 task** — most `draw_*` were not
individually checked beyond Stats/Messages (`a`) and Mana (`cp`). The native
registry needs per-fn adapters where the signature isn't `void(avatar&,
const catacurses::window&)`.

---

## Part F — SVG icon aesthetic (proposed "spice")

Goal: vector line-art icons that match the ASCII/bitmap-font look but stay crisp
at any `tile_px` / HiDPI. **Feasible with zero new deps** — SDL3_image (already a
dependency) bundles `nanosvg`/`nanosvgrast`/`IMG_svg.c`. Pipeline:
`.svg` → rasterize at target px (`IMG_LoadSVG_IO` / nanosvgrast scale) →
`SDL_Surface` (RGBA) → `copy_surface_to_dynamic_atlas` → `sprite_batcher` draw
with `tint` = the curses color. Monochrome white line-art + tint = recolorable
icons; re-raster per `tile_px` = crisp at HiDPI (the pipeline already does this
for tiles).

Prime candidates (semantically iconic, marked SVG above): **body-graph
silhouette** (ties to the in-scope body-graph port), **moon-phase disc**,
**compass rose**, **weather** (sun/cloud/rain), **wind direction arrow**, plus
small inline glyph-icons for needs/mana/sound/env.

Design + integration details: plan `dapper-wandering-popcorn`.

### Phase 2 status — SVG icon infra landed (2026-06-02)

- `src/widget_icon.{h,cpp}`: `widget_icon::get(name, px)` resolves
  `gfx/widgets/<name>.svg`, rasterizes at `px²` via `IMG_LoadSizedSVG_IO`,
  uploads **once** through `render_state::upload_surface_to_gpu_texture`, caches
  by `(name, px)`. `widget_icon::clear()` (called from the window-resize hook in
  sdltiles.cpp) drops stale-size rasters.
- `draw_widget_icon(win, cell, icon, nc_color)` in `sdltiles.cpp`: maps a panel
  cell to absolute pixels (`win->pos * fontwidth/height`), tints via
  `curses_color_to_SDL`, draws an **unlit** sprite through
  `render_state::queue_font_glyph` (composites with sidebar text, partial-redraw
  safe). `sdltiles.cpp` is in the common lib, so `panels.cpp` links it in both
  game + test.
- Assets: `gfx/widgets/{moon,heart,droplet,sound,wind,compass}.svg` (repo-root
  `gfx/`, beside the tilesets — `gfxdir()` is `gfx/`, NOT `data/gfx/`) +
  `README.md` (white-stroke / gray-fill two-tone tint contract).
- Proof: `draw_moon_narrow`/`draw_moon_wide` draw the `moon` icon in their free
  leading column (Phase 4 makes it phase-accurate).
- Verified: game + test build green; `[widget]` 26/26; 6 SVGs well-formed; game
  boots + loads the 22-widget sidebar.json clean, no `widget_icon`/SVG/assert
  errors. **On-screen icon render is reachable only in-game with the Moon panel
  enabled** — user-side visual confirmation pending.

---

## Phase 3 status — value renderer + custom layout selectable (2026-06-02)

- `widget_var` enum + `widget::get_var_value(avatar&)` (widget.{h,cpp}): 12 vars
  wired to BN getters — stat_str/dex/int/per, pain, stamina, mana, max_mana,
  morale, thirst, fatigue, speed. (`hunger` dropped — BN has no numeric getter,
  only `get_hunger_description`.) Unknown/`last` → 0.
- `make_value_widget_panel` (panels.cpp): draws `[icon] label: value` itself for
  `style:"number"`/`"value"`, with an optional leading two-tone SVG icon tinted
  by the value color. `make_widget_panel` dispatches native vs value by style.
- **`inject_widget_layouts` → `panel_manager::init()`**: builds a selectable
  layout from every `style:"sidebar"` widget (keyed by id, e.g. `custom`) and
  merges it into the layouts map post-load. This is the Stage-6 slice that makes
  BOTH the Phase-1 native widgets AND the value widgets actually **render in-game**
  (select the `custom` sidebar via `}`). Custom layouts are opt-in — never
  auto-selected, never replace a built-in. (Full Stage 6 — `sync_lua_panels`
  rework + save migration — still deferred.)
- `sidebar.json`: 4 value widgets (`val_pain`→heart, `val_thirst`→droplet,
  `val_stamina` no-icon, `val_mana`→spark, gated `spell_panel`) inserted into the
  `custom` container (now 26 children). Icons: 8 total (added `food`, `spark`).
- Verified: game + test build green; `[widget]` 35/5; **full suite 716 cases**;
  game boots + loads the 26-child container clean. In-game visual (select
  `custom` sidebar) is user-side.
- Known-minor / incremental: value color is currently flat (`c_white` value,
  `c_light_gray` label) — per-var coloring (stat compare, hp-bar) is a follow-up;
  left-aligned layout (right-align/bars need the Stage-5 arrange engine).

## Phase 3b status — value-widget polish (2026-06-02)

Polish pass on the data-driven value widgets (options 1+2), all in `panels.cpp`
`make_value_widget_panel` + new TU-static helpers:

- **Per-var value color** (`value_widget_color`): the number is now colored to
  match how the native `draw_*` panels color the same stat — reuses the existing
  static helpers (`str/dex/int/per_string`, `value_color` for speed,
  `get_hp_bar().second` for stamina, `morale_stat`, `mana_stat`,
  `get_thirst_description().second`, `get_fatigue_description().second`). Pain has
  no native helper → reddens with perceived pain (gray→yellow→light_red→red).
  The leading SVG icon is tinted to the **same** value color (two-tone contract),
  so a reddening stat reddens its glyph.
- **Clean gutter name** (`value_widget_name`): the value panel's `window_panel`
  name is now the id with the `val_` prefix stripped + capitalized
  (`val_pain` → `Pain`). These widgets are new + opt-in, so no save ever
  persisted the raw id — this clean English string becomes the stable save/load
  key AND the gutter label (`show_adm` re-localizes via `_()` at `panels.cpp:3079`,
  verified). No `translation::untranslated()` needed; mirrors the native panels'
  clean-English-key pattern.
- **In-row layout**: dropped the redundant colon — each value row now reads
  `[icon] Label value` (e.g. `♥ Pain  5`). The translated `_label` is the in-row
  string; the gutter rename removes the felt duplication.
- **Native casing dedup** (`sidebar.json`): `comp.Armor`→`Compact Armor`,
  `Comp.Compass`→`Compact Compass`, `Sim.Compass`→`Simple Compass`. NOTE: this is
  forward-looking hygiene only — `make_native_widget_panel` keys the gutter on
  `getId().str()` and never reads `_label`, so the native gutters still display
  the raw id (`armor_comp`, …). Pretty native gutters require renaming the
  window_panel **name**, which is the existing save/load key → a Stage-6
  save-migration task, NOT a freebie like the new value widgets.
- Verified: `cata_test-tiles` + `cataclysm-bn-tiles` link green; `[widget]`
  37 assertions / 6 cases pass (name assertion updated `val_pain`→`Pain`).

Deferred to the Stage-5 arrange engine: right-align / value bars (still left-aligned).

## Phase 4a status — phase-accurate moon (2026-06-02)

The moon-panel icon is no longer a static disc; it tracks the lunar phase.

- **8 per-phase SVGs** in `gfx/widgets/`: `moon_new`, `moon_waxing_crescent`,
  `moon_first_quarter`, `moon_waxing_gibbous`, `moon_full`, `moon_waning_gibbous`,
  `moon_last_quarter`, `moon_waning_crescent`. Each follows the two-tone contract:
  a gray `#707070` base disc, a white `#FFFFFF` lit path, and a white rim stroke
  always on top (so the disc outline reads at any phase). Northern-hemisphere
  convention — **waxing lights from the right, waning from the left**.
- The lit path is two SVG arcs (outer rim + terminator ellipse). Terminator
  `rx` controls crescent vs gibbous; the arc sweep flag controls bulge side.
  Geometry verified by reasoning (SVG y-down → sweep=1 is clockwise); no
  rasterizer was available locally, so confirm the on-screen shapes in-game.
- `panels.cpp::moon_phase_icon()` maps the 8 `moon_phase` enumerators
  (`calendar.h`) to those names via `get_moon_phase( calendar::turn )`; any
  out-of-range phase falls back to the generic `moon` disc. Both
  `draw_moon_narrow`/`draw_moon_wide` now draw `moon_phase_icon()` (was the flat
  `"moon"`).
- Verified: `cataclysm-bn-tiles` links green; all 8 SVGs well-formed (`xmllint`).
  On-screen render reachable with the Moon panel enabled (W_DISABLED_BY_DEFAULT)
  — user-side visual confirmation pending.

## Phase 4b status — directional wind arrow (2026-06-02)

The wind panel now shows a vector arrow that points the way the wind blows.

- **8 pre-rotated arrow SVGs** in `gfx/widgets/` (`wind_n`/`wind_ne`/…/`wind_nw`):
  one up-arrow path wrapped in `<g transform="rotate(R 12 12)">`, rotation about
  the cell center so the glyph never clips the 24×24 box. Chose pre-rotated SVGs
  over adding a rotation param to the GPU font-glyph draw path — zero change to
  `draw_widget_icon`/`queue_font_glyph`. Two-tone contract (gray fill + white
  outline). These are the **first** repo icons to use a group transform.
  **nanosvg support verified in the vendored source** (`sdl3_image-src/src/
  nanosvg.h`): `<g>` → `nsvg__pushAttr` + `nsvg__parseAttribs` (which handles the
  `transform` attr), the child `<path>` inherits the composed xform, and
  `nsvg__parseRotate` implements `rotate(angle cx cy)` (translate −c · rotate ·
  translate +c). So the rotation is honored, not assumed.
- `panels.cpp::wind_arrow_icon( int dirangle )` bins the angle to the **same 8
  45° sectors as `get_wind_arrow()`** (`weather.cpp`), keyed by origin cardinal
  (origin N → arrow points down/south). Out-of-range (calm) → the generic `wind`
  gust icon.
- `render_wind` rewired: the directional arrow is drawn in the **leading column**
  tinted by `get_wind_color(windpower)`; the "Wind" label shifts to col 3 and the
  description to col 11; the trailing unicode arrow the text used to append is
  **dropped** (the icon replaces it). Both `draw_wind` and `draw_wind_padding`
  feed through `render_wind`, so both get the arrow. The `wind` widget exists in
  `sidebar.json` (W_DISABLED_BY_DEFAULT), so this is reachable in-game.
- **Blast radius**: `render_wind` backs the *built-in* layouts (classic/compact/…)
  too, not just the opt-in `custom` container — so the col-shift (text now starts
  at col 3) and the dropped unicode arrow ship to those wind panels as well. The
  column math fits width-44; narrower built-in wind panels also start text at
  col 3. (This is a behavior change to shipped panels, unlike the value widgets.)
- Verified: `cataclysm-bn-tiles` links green; all 8 arrow SVGs well-formed
  (`xmllint`); `[widget]` 37/6 still pass. On-screen arrow orientation + nanosvg
  rasterization of the rotate transform are user-side (enable the Wind panel).

Skipped this pass (no layout consumer / low value): a **weather** sun/cloud/rain
icon — `draw_weather_classic` is in the `classic` built-in but NOT in the custom
container, so a weather icon wouldn't render there yet; and a **compass-rose**
icon — `compass.svg` already exists and the compass panel is a dense 8-row grid
where a corner icon adds clutter. Revisit if a weather widget joins the container.

## Phase 4c status — body-graph widget (minimal native grid, 2026-06-02)

The body-graph landed as a **BN-native colored limb grid**, NOT a port of CDDA's
data-driven ASCII silhouette. Research (this session) found CDDA's
`get_bodygraph_lines()` compiles against `sub_bodypart_id` (which BN lacks),
though the sidebar's own bodygraph JSON never populates sub-parts — so the dep is
a no-op for sidebar data. User chose the minimal grid over porting the full
`bodygraph` JSON type system: smaller, no new type, no `init.cpp` registration.

- **`widget_var` enum** (`widget.h`/`widget.cpp`): added `body_graph` (HP),
  `body_graph_temp`, `body_graph_encumb`, `body_graph_status`, `body_graph_wet`.
  Not scalar → excluded from `get_var_value`/`value_widget_color` (grouped with
  `last` → 0 / `c_white`). The body-graph renderer reads `w.var()` directly.
- **`bodygraph_bp_color( var, avatar&, bodypart_id& )`** (`panels.cpp`): the
  renderer-agnostic core — per-bp → `nc_color` for each dimension. HP via
  `get_hp_bar(get_part_hp_cur,max).second` (matches Limbs panel); temp bands
  `get_part_temp_cur` against `weather.h` `BODYTEMP_*`; encumb bands
  `encumb(bp.id())`; status = `limb_color(bp.id(),…)`. `body_graph_wet` →
  `c_light_gray` + once-at-build `debugmsg` (**BN has no per-bp wetness** — only
  global; this dimension is intentionally degraded).
- **`make_bodygraph_widget_panel`** (`panels.cpp`, declared in `panels.h`): lays
  `get_all_body_parts(true)` in the `draw_limb2` 2-column grid, each limb label
  (`body_part_hp_bar_ui_text`) tinted by `bodygraph_bp_color(w.var(),…)`. The **HP
  dimension** also draws the HP bar (`draw_limb_health`, label x0/x11 + bar x5/x16)
  to match the Limbs-panel look the approved preview showed; the temp/encumb/status/
  wet dimensions have no bar representation, so they render **color-only** (the tint
  is the data) at wider x0/x16 spacing. Dispatched from `make_widget_panel` on
  `style=="body_graph"`.
- **`value_widget_name` generalized**: now title-cases underscore-separated
  segments (`bodygraph_temp`→`"Bodygraph Temp"`); `val_`-stripped value ids are
  single-segment so their names are unchanged (`val_pain`→`"Pain"`).
- **`sidebar.json`**: 4 body_graph widgets (`bodygraph` HP default + temp/encumb/
  status `W_DISABLED_BY_DEFAULT`) added + inserted into the `custom` container
  (now 30 children).
- Verified: `cata_test-tiles` + `cataclysm-bn-tiles` link green; `[widget]`
  46 assertions / 7 cases pass (added a body_graph parse+build case; container
  count 26→30). In-game render (select `custom` sidebar, enable Body widget;
  switch dimension via the temp/encumb/status variants) is user-side.

**Part B B2 update**: the `body_graph` family is now **supported via the minimal
grid** (hp/temp/encumb/status); `body_graph_wet` is **degraded** (no BN per-bp
wetness). The CDDA ASCII silhouette + the SVG silhouette (Part F) remain the
optional follow-up upgrade if the colored grid reads as redundant with Limbs.

Phase 4 complete (4a moon · 4b wind · 4c body-graph).

## Stage 5 status — value readout polish (right-align + bars, 2026-06-02)

**Scope decision (user):** Stage 5 as written ("port `get_window_panel`/`layout`/
`show`/`arrange`") assumed CDDA's everything-is-a-widget-tree model. BN took the
**flat-panel native-renderer path** (native bridge + `make_value_widget_panel` +
`make_bodygraph_widget_panel`), so porting CDDA's engine would orphan those and
drag in `dbl_or_var`/dialogue eval. We did the **consumer-driven subset** instead:
- **Built:** right-aligned values + fill bars (the value widgets looked
  unfinished — clear consumer).
- **Deferred:** the generic `style:"layout"`/`arrange:"columns"` composition tree
  + content-driven `get_wgt_height`. **No current consumer** — body-graph
  hand-draws its own grid; the only eventual user is Stage-7 labels-layout-as-value
  widgets, which is gated and already covered by the native wrappers. Revisit when
  a widget actually composes children.

**Shipped** (all in `make_value_widget_panel`, `panels.cpp`):
- Value/readout is **right-aligned to the panel edge** (`getmaxx(win) -
  utf8_width(rhs)`, clamped so it never overlaps the label).
- **Bounded vars get a fill bar + percent**: `value_var_max(var,u)` returns the
  ceiling for `stamina` (`get_stamina_max`) and `mana` (`max_mana`); the rhs is
  then `get_hp_bar(val,max).first + " NN%"` (reuses the native 5-cell bar string,
  so it reads like the HP/stamina panels). Unbounded vars (pain/speed/morale/
  thirst/fatigue — no clean ceiling) stay a right-aligned number. The whole rhs is
  tinted by the existing `value_widget_color`.
- Verified: `cata_test-tiles` + `cataclysm-bn-tiles` green; `[widget]` 46/7 pass
  (build path only — the right-align/bar draw is in-game, user-side).

Remaining: Stage 6 (`sync_lua_panels` rework + save migration; promote storage to
the `panel_layout` wrapper) and the Stage-7 default-flip gate. The deferred arrange
tree folds into whichever later stage first needs widget composition.

## Stage 7 status — layout parity (default-capable, 2026-06-03)

Stage 7 **gate #2 (layout parity)** done: BN's four hardcoded layouts are now
reproduced as JSON `style:"sidebar"` widgets, so the widget engine is
**default-capable**. They ship as ADDITIONAL selectable layouts (parallel ids,
`we_` prefix) beside the built-ins — the actual default-flip (gate #3: delete the
`draw_*` builders) stays a separate release step.

- **`data/json/ui/sidebar.json`**: 4 new containers `we_classic` (19), `we_compact`
  (17), `we_labels_narrow` (21), `we_labels` (22) — each a verbatim transcription of
  `initialize_default_{classic,compact,label_narrow,label}_panels` (`panels.cpp:2486-
  2617`): same panels, order, toggle defaults (`W_DISABLED_BY_DEFAULT`), `show_if`
  gates (`spell_panel`/`veh_panel`). The pre-existing 22 wide native defs already
  WERE the `labels` set, so `we_labels` reuses them wholesale; the other three added
  ~33 native defs for the classic/compact/narrow `draw_*` variants. The container
  width drives all child widths (the `make_native_widget_panel` rule), so a few
  builder quirks where a disabled panel hardcoded width 32 inside a width-44 layout
  (`comp.Armor`, `Sim.Compass`) now take the container width — accepted, same as the
  shipped `custom` container, all on `W_DISABLED_BY_DEFAULT` panels.
- **Gate #1 (var parity)** was already satisfiable (Part B3) — the default layouts
  show no unsupported var; this port uses native wrappers, so no var work was needed.
- **One C++ change** (`make_native_widget_panel`, `panels.cpp`): honor a
  `W_ALWAYS_DRAW` widget flag → the `window_panel` `force_draw` ctor arg. The Map
  (`draw_mminimap`) panel needs it (the hardcoded builders pass `always_draw=true`
  so the GPU minimap redraws every frame; `game.cpp:4372` `panel.always_draw ||
  draw_this_turn`). Added the flag to the `map` def. `widget::has_flag(string)` takes
  arbitrary flag strings (no enum/registration), same path as `W_DISABLED_BY_DEFAULT`.
  Side effect: the shipped `custom` container reuses the same `map` def, so its
  minimap now also redraws smoothly — a strict improvement, but a behavior change to
  already-shipped data, noted for the record.
- **Tests** (`tests/widget_test.cpp`): `W_ALWAYS_DRAW`→`always_draw` (map true, stats
  false); each `we_*` builds the expected panel count + first/last id in builder order
  + registers via `reload_widget_layouts` without replacing the built-ins.
- Verified: `cataclysm-bn-tiles` + `cata_test-tiles` link green; `[widget]`+`[ui_tween]`
  220 assertions / 22 cases; **full suite 7,765,782 assertions / 733 cases** (the
  data-integrity pass over all loaded widgets covers the new defs). **In-game A/B
  (user-side):** `}` selector now lists `we_classic`/`we_compact`/`we_labels_narrow`/
  `we_labels` beside the built-ins; select each, compare to its hardcoded twin (panel
  set/order/toggles/heights, gating, and the Map redrawing smoothly while moving).

## Go / No-Go recommendation

**GO.** The recommendation rests on **directly-verified BN-side facts**, not the
provisional CDDA/var material:

1. **`window_panel` grows additively** — Stage 1 already added `dynamic_height`
   with no signature break; adding `widget_id wgt` + accessors is the same shape.
2. **Native-wrapper bridge is viable** — every `draw_*` target the bridge needs
   exists in `panels.cpp` (verified by grep), so the default JSON sidebar can
   reach visual parity immediately, *before* any var coverage.
3. **Storage migration is tiny** — `get_current_layout()` → `panel_layout&` is a
   return-type change + 6 mechanical `.panels()` insertions; id-string consumers
   untouched.

Scope notes (carried into the questions below):
- **Do not port** CDDA's `custom` math var (0 uses in the reference layout) or
  the unsupported systems (cardio, weariness, body-graph, sundial, …) — not
  needed for parity, would drag in eval machinery BN lacks.
- **Default flip stays gated** (Stage 7): var parity + layout parity + a
  dual-maintenance window before any `draw_*` deletion.

**Provisional, verify before port code:** CDDA `window_panel`/`panel_layout`/
`get_window_panel` signatures (re-fetch verbatim, Stage 3); the ~75-row var
table (confirm per-row, Stage 4).

---

# Icon config + tween animation arc (plan `as-a-continuation-of-deep-ullman`, 2026-06-03)

Continuation of the sidebar work: (1) a config registry so SVG icons can be
swapped without recompiling, (2) a state-based tween animation system so the
sidebar "feels alive". 5 stages; foundation (1–3) landed, 4–5 pending.

**Stage 1 — icon config registry + reload (DONE).** `gfx/widgets/icons.json`
maps a logical icon id → SVG file (+ an optional per-icon `animations` array,
consumed in Stage 4). `widget_icon` parses it (`load_config`, lazy on first
`get()`); `get()` resolves the file via the registry with **fallback
`<id>.svg`** so unlisted icons keep working. `reload()` = `clear()` cached
rasters + re-read JSON, wired into the existing `DEBUG_RELOAD_TILES` debug action
→ edit an SVG, "Reload tileset", art swaps live. Files: `widget_icon.{h,cpp}`,
`debug_menu.cpp`, `gfx/widgets/icons.json` (24 icons).

**Stage 2 — tween core + anim registry (DONE, unit-tested).**
- `src/ui_tween.{h,cpp}` — **pure, dependency-free**: full Penner `ease_curve`
  set (normalized `[0,1]→[0,1]`; back/elastic overshoot intentionally), a POD
  `tween{from,to,start_ms,duration_ms,ease,loop,repeats}` with `value_at(now)` /
  `settled(now)`. Time-based on `SDL_GetTicks`, so cadence (8–30 fps) only
  changes sample smoothness, never timing. Implementation is custom (the interp
  primitive `lerp` already exists; a library would replace only ~60 lines of
  trivial math and impose its own update loop) — grill outcome.
- `src/sidebar_anim.{h,cpp}` — the **state registry** (process singleton, keyed by
  widget id). `update(key,value,is_critical,now)` primes WITHOUT animating on
  first sight (no open-flash), pops on value change (retarget-from-current),
  starts a pingpong blink on critical-band entry and an ease-out-to-identity on
  exit. `sample(key,now)→icon_transform{scale,alpha,offset_y,rotation,blend}`.
  `any_active(now)` + the free fn `sidebar_requires_animation()` drive the idle
  redraw. `now_ms()` centralizes the SDL clock (keeps SDL out of panels.cpp).
- `tests/ui_tween_test.cpp` — 10 cases / 141 assertions: easing endpoints+clamp,
  known midpoints, `value_at` once/loop/pingpong/finite-repeat, registry
  prime-no-flash / pop-then-settle / critical blink+ease-back. **Caveat: these
  test the math + state machine, NOT the on-screen feature** (the draw is
  GPU/in-game). New test file ⇒ explicit `cmake` reconfigure (file(GLOB)).

**Stage 3 — draw integration + idle hook + 30fps + one effect (DONE, in-game
unverified).**
- `draw_widget_icon` gains a transform overload (scale about cell centre, alpha,
  vertical offset, tint colour-blend; rotation deferred to Stage 5). Old 4-arg
  overload delegates with identity → existing callers unchanged.
- `make_value_widget_panel` feeds the live value to the registry and passes the
  sampled transform to the icon draw → a value change pops the icon.
- `handle_action.cpp`: `sidebar_requires_animation()` added to the
  `minimap/terrain_requires_animation()` invalidation `||`; input cadence is
  re-evaluated each loop iteration — **125 ms while weather/SCT animate**
  (their step speed unchanged), **~30 fps only while a sidebar tween is live**,
  else 125 ms (idle stays cheap; no perpetual 30fps wake). Chose dynamic
  re-eval over a weather/SCT step-accumulator (simpler, also covers SCT).
- **Post-grill fixes (advisor):** clear the anim singleton in `game::setup`
  (else loading a save pops every icon whose value differs from the prior
  session — the rejected "flash on load"); cadence gated on
  `sidebar_requires_animation()` inside the loop (else idle ran 30fps for all
  players). Latent: every value-icon currently pops (fine for slow stats); Stage
  4's config-driven binding makes pop opt-in per icon, resolving it.

Build: `cataclysm-bn-tiles` + `cata_test-tiles` link green; `[ui_tween]` +
`[widget]` = 187 assertions / 17 cases pass.

**In-game verification (user-side, NOT covered by the tests):** boots + loads the
custom sidebar with no `widget_icon`/SVG warning; spike pain → heart pops, eases
(`back_out`), settles; reload-tileset swaps an edited SVG live; **load a save →
no pop-storm**; idle in clear weather → redraw quiets (loop relaxes to 125 ms).

**Stage 4 — full vocabulary + config-driven binding (DONE, in-game unverified).**
`sidebar_anim` is now **fully data-driven**: each icon's `animations` array in
`icons.json` is parsed into `anim_spec`s (`trigger` on_change|critical|ambient ×
`property` scale|alpha|offset_y|rotation|color_blend × from/to/duration/ease/loop/
repeats/color) and bound via `load_specs()`/`bind_specs()`. `update(key, icon, …)`
fires specs by trigger — config says WHAT/HOW, C++ says WHEN. **No specs → no
animation (opt-in)**, which also retires the "every value-icon pops" latent risk.
`load_specs()` is called from `game::setup` (after `clear()`) and the debug
reload. Value widgets derive `is_critical` from the value colour (red band).
Native **moon/wind** panels wired (`draw_moon_phase_icon` shared helper; wind in
`render_wind`) → pop on phase/direction change. **Default specs shipped (modest):**
pop on heart/droplet/food/spark/moon/wind; critical alpha-blink on heart; one
gentle ambient alpha-pulse on spark (mana panel, opt-in → low blast radius). New
test: "no specs → never animates". Spin is config-only (no default spinning icon).

**Stage 5 — spin (DONE).** `queue_font_glyph` gained a trailing `rotation`
(radians, default 0) on both overloads → `sprite_instance.rotation`; the shared
`SPRITE_VERT_HLSL` already rotates by it (no shader change). `draw_widget_icon`
converts the transform's degrees→radians. Enable on any icon with an ambient
`rotation` spec, e.g. `{"trigger":"ambient","property":"rotation","from":0,
"to":360,"duration":4000,"ease":"linear","loop":"loop"}`.

**Status:** all 5 stages build green (`cataclysm-bn-tiles` + `cata_test-tiles`);
`[ui_tween]` + `[widget]` = 189 assertions / 18 cases. The in-game checklist
above is the real verification and remains user-side.

## Polish + save/load fixes (2026-06-03)

User confirmed the base animations work in-game, then asked for polish.

- **Directional change effect** (replaces the uniform pop + the colour row-flash):
  `on_increase`/`on_decrease` triggers fire by the sign of the change, driving a
  `scale_y` tween anchored at `pivot_y` (top for up, bottom for down, `back_out`
  spring) → the icon recoils as if hit from below/above. No colour coding (user
  preference). `icon_transform` gained `scale_y`+`pivot_y`; `draw_widget_icon`
  applies an anchored vertical scale (uniform case unchanged). heart/droplet/food
  use it.
- **Ambient spin**: spark (6 s) + compass slow drift (12 s) via `rotation` loop
  specs. Rotation was plumbed through `queue_font_glyph` (Stage 5).
- **Row highlight** (`draw_widget_row_highlight`, `queue_ui_rect`, draws under the
  row text): shipped then demoted to **opt-in** — add a `_row` entry with a
  `color_blend` spec to re-enable. The directional icon squash is the default
  change cue instead.

**Save/load fixes (the deferred Stage 6 path, surfaced by loading a saved
"custom" layout — NOT caused by the animation work):**
- `panel_manager::deserialize`: always read + preserve a layout's `panels`, even
  when the layout isn't built yet (the runtime "custom" layout loads after world
  load) — previously early-returned, leaving `panels` unvisited → `report_unvisited`.
- `get_current_layout()`: fall back to a built-in **without discarding**
  `current_layout_id`, so a saved "custom" selection resolves once built instead
  of being reset to classic on every launch.
- `reload_widget_layouts()`: re-apply saved toggle/order state to the layouts it
  builds (so the custom sidebar's toggles restore).
- `icons.json` readers (`widget_icon::load_config`, `sidebar_anim::load_specs`):
  `allow_omitted_members()` — strict JSON, two readers each see the other's fields
  + the `//` comments.

Shipped as 3 commits on `feature/improvements`. Tests `[ui_tween]` 147 / `[widget]`
green; the real `icons.json` parse is covered by a headless test. On-screen feel
remains user-verified.
