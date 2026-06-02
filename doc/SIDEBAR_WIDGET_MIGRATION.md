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
