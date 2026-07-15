# RmlUi HUD — Panel Widget Reference

## STATUS (reviewed 2026-07-15)

**Superseded by the Qud HUD build (`plans/qud-hud.md`) — coverage below reflects the
finished state, not a forward worklist anymore.** All 30 `custom`-layout widgets now have
RML producers (`g_hud_producers` in `src/panels.cpp`); no panel falls through to the
`[name]` placeholder. Two panels ship at reduced fidelity vs the deleted curses code,
documented in §2 below (`map`: colored-text overmap chunk, not the pixel-minimap RTT;
`vehicle`: text fuel-indicator lines via `vehicle::fuel_indicator_lines()`, not the
graphical `print_fuel_indicators` gauges). This doc is kept as the historical per-panel
content catalog (§3) for anyone auditing a producer against the original curses body.

---

**Purpose.** The Tier-10 curses sidebar rip-out (commits `1322cce459` → `bc8980a098`,
2026-06-23) deleted all 58 curses `draw_*` sidebar-panel renderers. The RmlUi HUD now
renders the sidebar by panel **name**, and any panel without an RML producer shows a
`[name]` placeholder. This document is the spec for **rebuilding the missing panels as
RmlUi producers** (the separate "new HUD" plan). It records what each deleted panel
displayed, its data sources, and which panels still need building.

---

## 1. How the HUD renders now (the contract)

- `game::draw_panels()` (game.cpp) drives ONE persistent RmlUi document via
  `sidebar_hud_open/sync/close` (panels.cpp). The curses panel loop is gone.
- `sidebar_hud_sync()` walks `panel_manager::get_current_layout()` (the widget layout from
  `data/json/ui/sidebar.json`, e.g. the `"custom"` layout). For each panel it calls:
  ```cpp
  auto produce = hud_producer( panel.get_name() );   // exact, case-insensitive name match
  row.rml = produce ? cata_text_to_rml( produce( u ) )
                    : cata_text_to_rml( colorize( "[" + panel.get_name() + "]", c_dark_gray ) );
  ```
- **Producers** live in `g_hud_producers` (panels.cpp, `std::array<hud_producer_entry,N>`):
  `{ panel_name, hud_* fn }`. A producer is `std::string (*)( avatar & )` returning a
  colour-tagged string (consumed by `cata_text_to_rml`). To migrate a panel: write a
  `hud_<x>( avatar & )` producer and add a `{ "<panel_name>", hud_<x> }` row.
- **Panel name** = the widget id (`w.getId().str()`), set in `make_native_widget_panel` /
  `make_bodygraph_widget_panel` (now name-only, null `.draw`). The name is the lookup key.
- **No generic producer exists** — value widgets (`val_*`) and body-graph widgets currently
  fall through to the `[name]` placeholder. There is no `_style="number"` fallback yet.

### Source recovery
The deleted curses bodies are the visual spec. Recover any of them with:
```
git show d40d1cd49e:src/panels.cpp
```
(`d40d1cd49e` = commit P2a, the last revision before the `draw_*` were deleted in P2b.)
The `native_draw_registry()` map in that revision lists every `draw_*` target name.

---

## 2. Coverage matrix — the user's `custom` layout (30 widgets)

Layout (sidebar.json `custom`, in order): hint, limbs, movement, mana, stats, val_pain,
val_thirst, val_stamina, val_mana, bodygraph, bodygraph_temp, bodygraph_encumb,
bodygraph_status, vehicle, location, wind, location_alt, weapon, weightvolume, needs,
sound, log, moon, armor, armor_comp, compass, compass_comp, compass_simple, map, ai_goal.

| Widget | Status | Producer / Note |
|---|---|---|
| hint | ✅ done | `hud_hint` |
| limbs | ✅ done | `hud_limbs` |
| movement | ✅ done | `hud_movement` |
| mana | ✅ done | `hud_mana` |
| stats | ✅ done | `hud_stats_wide` |
| location | ✅ done | `hud_location` |
| location_alt | ✅ done | `hud_location` |
| weapon | ✅ done | `hud_weapon` |
| weightvolume | ✅ done | `hud_wgtvol` |
| needs | ✅ done | `hud_needs_labels` |
| sound | ✅ done | `hud_sound_labels` |
| log | ✅ done | `hud_log` |
| armor | ✅ done | `hud_armor` |
| compass / compass_comp / compass_simple | ✅ done | `hud_compass` |
| **val_pain / val_thirst / val_stamina / val_mana** | ✅ done | `make_value_widget_panel` (per-widget `hud_produce` lambda, not `g_hud_producers` — reads `widget::get_var_value`/`value_widget_color`, fill-bar+percent for bounded vars else raw number). |
| **bodygraph (+ _temp / _encumb / _status)** | ✅ done | `make_bodygraph_widget_panel` (per-widget `hud_produce` lambda). One row per main body part; label colored by `bodygraph_bp_color()` per dimension (limb/temp/encumb/status). Text, not the curses body-graph diagram — color encodes the data instead. |
| **vehicle** | ✅ done | `hud_vehicle`. Fuel gauges are `vehicle::fuel_indicator_lines()` (new text accessor) instead of the graphical `print_fuel_indicators`. |
| **wind** | ✅ done | `hud_wind` + `wind_arrow_icon()` helper (8-sector directional SVG id). |
| **moon** | ✅ done | `hud_moon` + `moon_phase_display()` helper (phase SVG id + name + temp). |
| **armor_comp** | ✅ done | `hud_armor_comp` (1-char colored symbol per region). |
| **map** | ✅ done (reduced fidelity) | `hud_map` → `overmap_ui::overmap_chunk_rows()` — 13×7 colored-text overmap minichunk (successor to the removed `draw_overmap_chunk`), not the pixel-minimap GPU RTT. Player cell + mission-target cell use inline `background-color` spans (curses reverse-video has no `cata_text_to_rml` equivalent). Border horde markers and the past-the-grid-edge mission arrow are dropped (graphical bonuses on the physical minimap, not portable to an arbitrary text row count). |
| **ai_goal** | ✅ done | `hud_ai_goal` (ticks the `"npc_needs"` behaviour tree via `character_oracle_t` each call). |

**30 of 30 covered.** Two ship at reduced fidelity vs the original curses/graphics
version: `map` (text minichunk, no pixel-minimap RTT) and `vehicle` (text fuel lines,
no graphical gauges) — both locked MVP decisions from the `qud-hud` planning session,
not omissions.

---

## 3. Per-panel content catalog (recovered from `d40d1cd49e:src/panels.cpp`)

> All panels `werase(w)` first / `wnoutrefresh(w)` last (boilerplate). `point(x,y)` = cell
> offsets. GAP panels (no RML producer yet) are documented most thoroughly.

### Shared helpers (compute-only)
- **`get_temp(u)`** → temperature string IF `u.has_item_with_flag(json_flag_THERMOMETER)`
  or `u.has_bionic(bio_infolink)`, via `print_temperature(get_weather().get_temperature(u.abs_pos()))`; else `"-"`.
- **`temp_delta_string(u)` / `temp_delta_arrows(u)`** → rising/falling suffix or unicode
  arrows. Computed from `temp_delta(u)` (scans `u.get_body()` for max deviation from
  `BODYTEMP_NORM`, bins via `define_temp_level()` into 1–7, takes conv−cur zone).
- **`get_int_digits(n)`** → column width of an int (used for curses x-packing; RML uses flex).
- **`get_weight_color(u)`** red > capacity / yellow > 0.75 / else gray. **`get_volume_color(u)`** red > 0.85 / yellow > 0.65.
- **`get_armor_comp(u, bp)`** → topmost worn item's colored 1-char symbol (truncate to first `</color>`), or `"-"`.
- Widely-used elsewhere (still present): `str_string/dex_string/int_string/per_string(u)`,
  `value_color(stat)`, `morale_stat(u)`, `morale_emotion(level, faceType, horizontal)`,
  `power_stat/mana_stat/temp_stat(u)`, `get_hp_bar()`, `body_part_hp_bar_ui_text(bp)`,
  `move_mode_color/string(u)`, `get_light_level()`, `get_wind_color/desc/arrow()`,
  `get_local_windpower()`.

### 3.1 stats — `draw_stats` / `draw_stat_narrow` / `draw_stat_wide` / `draw_char_narrow` / `draw_char_wide`  ✅ (hud_stats_wide)
- `draw_stats`: one row `STR n  DEX n  INT n  PER n`; value x shifts −1 when ≥10; `"99+"` if ≥100.
- `draw_stat_narrow/wide`: 2×2 grid (Str/Int | Dex/Per) + `Power:` + `Safe: On/Off`.
- `draw_char_*`: combo — Sound, Stam (bar), Focus(+trend arrow ↥/↧ vs `calc_focus_equilibrium`), Mood (smiley), Speed, Move.
- Data: `u.get_str/dex/int/per()`, `power_stat`, `g->safe_mode`, `u.get_stamina[_max]`, `u.focus_pool`, `morale_stat`+`morale_emotion`, `u.get_speed`, `move_mode_*`, `u.movecounter`.

### 3.2 needs — `draw_needs_compact` / `_narrow` / `_labels`  ✅ (hud_needs_labels)
- Colored words for Hunger/Thirst/Fatigue(Rest)/Pain/Temp(Heat)(+delta arrow)/Focus, laid out by variant (compact 3×2, narrow 5 rows, labels 3×2).
- Data: `u.get_hunger/thirst/fatigue/pain_description()` (each returns `<string,color>` — pair order varies by accessor), `temp_stat(u)`, `temp_delta_arrows(u)`, `u.focus_pool`.

### 3.3 sound/stealth — `draw_stealth` / `draw_sound_narrow` / `draw_sound_labels`  ✅ (hud_sound_labels)
- stealth: `Speed n` + move string (right-aligned) + `DEAF`(red) or `Sound: vol`(yellow if ≠0).
- sound_*: `Sound: vol` (yellow) or `Deaf!` (red).
- Data: `u.get_speed`, `value_color`, `u.movecounter`, `move_mode_*`, `u.is_deaf`, `u.volume`.

### 3.4 weightvolume — `_classic` / `_compact` / `_narrow` / `_labels`  ✅ (hud_wgtvol)
- `Weight: w/cap` + `Volume: v/cap`, colored by `get_weight_color`/`get_volume_color`.
- Data: `carry_weight_string(u)` (`%.1f/%.1f` convert_weight), `carry_volume_string(u)` (`%.2f/%.2f` convert_volume). Units follow user options (kg/lbs, L/cups).

### 3.5 mana — `draw_mana_classic/_compact/_narrow/_wide` (→ `print_mana`)  ✅ (hud_mana)
- `Mana: cur   Max Mana: max`. Variants differ only by format string + `utf8_justify` widths.
- Data: `mana_stat(u)` (`available_mana`, color blue≥max/2 / yellow≥max/3 / red), `u.magic->max_mana(u)`. Gated by `spell_panel` show-predicate (only if avatar has magic).

### 3.6 limbs/health — `draw_limb2` / `_narrow` / `_wide` / `draw_health_classic` / `draw_limb_health`  ✅ limbs (hud_limbs); ✅ body-graph (make_bodygraph_widget_panel)
- **`draw_limb_health(u,w,bp)`** (core HP renderer): broken limb → mend% as `#`×(mend/20) + `=`×rest, color blue if splinted (`worn_with_flag(json_flag_SPLINT,bp)` / `mutation_value("mending_modifier")>=1.0`) else dark_gray; else `get_hp_bar(hp_cur,hp_max)` bar + `.` padding to width 5. `HEALTH_STYLE=="number"` or `effect_got_checked` → raw `%3d`. Reads `u.get_part_hp_cur/max(bp)`.
- `draw_limb2`: all `u.get_all_body_parts(true)` in 2 cols + STM bar + PWR + SAFE + mood smiley. Label = `body_part_hp_bar_ui_text(bp)` colored by `u.limb_color(bp,true,true,true)`.
- `draw_limb_narrow`: 2 cols, `Label: bar`. `draw_limb_wide`: 3-per-row.
- `draw_health_classic`: kitchen-sink — embeds minimap (§3.16) + 6 fixed-limb HP bars + needs column + mood + STR/DEX/INT/PER + SAFE + Stamina + Speed + temp + POWER.
- **Body-graph link:** the `bodygraph*` widgets are done via `make_bodygraph_widget_panel`'s `hud_produce` lambda (§2) — one row per body part, label colored by `bodygraph_bp_color()` per dimension. Text encoding, not the curses graphic; `draw_limb_health`'s per-limb HP/color logic stays the reference model.

### 3.7 armor — `draw_armor` / `draw_armor_padding` / `draw_armor_comp`  ✅ armor (hud_armor); ✅ armor_comp (hud_armor_comp)
- armor/padding (5 rows): Head/Torso/Arms/Legs/Feet → full item `tname(1,true,truncate)` or `"-"` (topmost worn covering head/torso/arm_r/leg_r/foot_r).
- **armor_comp** ✅ done — `hud_armor_comp`: `H: T: A: L: F:` + 1-char colored symbol per region via `get_armor_comp(u,bp)`.

### 3.8 vehicle ✅ done (hud_vehicle) — `draw_veh_classic` / `_compact` / `_padding`
- Only when in/controlling a vehicle (2 rows):
  - Row 0: azimuth heading (`veh->face.to_string_azimuth_from_north()`); `<unit> :` label (`USE_METRIC_SPEEDS` option string); current speed (color by `veh->strain()`: ≤0 blue / ≤0.2 yellow / ≤0.4 light_red / else red); if `veh->cruise_on`, `>` + cruise speed (light_green); if `veh->has_part("WING")`, `flight: <get_takeoff_speed()>` (green if faster than needed).
  - Row 1 ✅ done: `veh->fuel_indicator_lines()` (new text accessor) replaces the graphical `print_fuel_indicators` — one text line per fuel type instead of a bar gauge.
- Data: `g->remoteveh()` / `veh_pointer_or_null(get_map().veh_at(u.bub_pos()))`; `veh->velocity`/`cruise_velocity` → `convert_velocity(...,VU_VEHICLE)`; `veh->strain()`, `veh->cruise_on`, `veh->has_part("WING")`, `veh->get_takeoff_speed()`. Gated by `veh_panel` show-predicate.
- **RML notes:** ✅ resolved via `vehicle::fuel_indicator_lines()` — a new text accessor returning one formatted line per fuel type, appended verbatim to `hud_vehicle`'s output. Drops the digit-width x-packing (flex handles it). Strain→color + flight readout are simple thresholds, ported as-is.

### 3.9 location — `draw_location_classic` / `draw_loc_narrow` / `_wide` / `_wide_map` / `draw_loc_labels`  ✅ (hud_location)
- classic: `Location: <terrain>`. labels (6 rows): Place / X,Y,Z / Sky(weather|Underground) / Light / Date(season, day N) / Time(clock|approx|???).
- `_wide_map` additionally embeds a 5×5 overmap chunk (§3.16).
- Data: `ACTIVE_OVERMAP_BUFFER.ter(u.abs_omt_pos())->get_name()`, `u.abs_omt_pos()` (format per `OVERMAP_COORDINATE_FORMAT`), `g->get_levz()`, `get_weather().weather_id`, `get_light_level(...)`, `calendar::*`, `u.has_watch()` → `to_string_time_of_day` / `time_approx` / `"???"`.

### 3.10 weapon — `draw_weapon_classic` / `_classic_alt` / `draw_weapon_labels`  ✅ (hud_weapon)
- Wielded weapon (`character_funcs::fmt_wielded_weapon(u)`, `trim_and_print`) + martial style (`u.martial_arts_data->selected_style_name(u)`; classic colors red/blue by `u.is_armed()`, labels plain gray).

### 3.11 time — `draw_time` / `draw_time_classic` (+ `draw_time_graphic`)  ⚠ (no dedicated producer; folded into location's Date/Time)
- season + day + time (clock if `u.has_watch()`, else `draw_time_graphic`, else `???`) + Moon glyph (`get_moon_graphic()`); classic adds Temp if thermometer.
- `draw_time_graphic`: 14-cell `[ … ]` sun/moon ASCII clock bar, glyph+color per column from `hour_of_day<int>(calendar::turn)`.

### 3.12 weather/lighting/env/wind ✅ wind done (hud_wind) — `draw_weather_classic` / `draw_lighting_classic` / `draw_env_compact` / `draw_wind` / `_padding`
- weather_classic: `Weather: <name>` or `Underground` + Moon glyph.
- lighting_classic: `Lighting: <level>` + `Sound: vol`/`Deaf!`.
- env_compact (6 rows + minimap): weapon, style, location, weather, lighting, wind (`get_wind_desc`+`get_wind_arrow`), temp.
- **`draw_wind` / `_padding`** ✅ done → `hud_wind`: directional SVG icon (`<img>` at native `?px=32` sizing, via the new sized-SVG `LoadTexture` path) selected by `wind_arrow_icon(winddirection)`, plus `Wind : <get_wind_desc>` (colored) + `get_wind_arrow` text glyph.
  - **`wind_arrow_icon(dirangle)`**: 8 sectors → icon ids `wind_n/ne/e/se/s/sw/w/nw` (N≤23 or >338; NE≤68; E≤113; SE≤158; S≤203; SW≤248; W≤293; else NW; `<0`/`≥360` → `"wind"`). Arrow points where wind blows *toward*.
  - `windpower = get_local_windpower(weather.windspeed, cur_om_ter, u.abs_pos(), weather.winddirection, g->is_sheltered(u.bub_pos()))` (factors terrain + shelter — NOT raw windspeed); `wind_color = get_wind_color(windpower)`.
  - **RML notes:** ✅ resolved. No pop-on-change animation (the curses `sidebar_anim::registry` tween engine isn't ported) — icon swaps instantly on direction change; acceptable MVP gap, not tracked further here.

### 3.13 moon ✅ done (hud_moon) — `draw_moon_narrow` / `_wide` (+ `get_moon` / `get_moon_graphic` / `moon_phase_icon`)
- Graphical moon-phase disc icon at x=0 + `Moon : <phase name>` + `Temp : <temp>` (narrow row1 / wide row0 x=23).
- `hud_moon` uses `moon_phase_display(get_moon_phase(calendar::turn))` — a new helper pairing the SVG icon id with the translated phase name — instead of the separate `get_moon()`/`moon_phase_icon()` pair.
- **RML notes:** ✅ resolved. Phase-accurate SVG disc (`<img>` at `?px=32`) + `Moon : name` + `Temp : temp`, tinted via the SVG asset itself (icon ids `moon_*` in icons.json). No pop-on-change animation ported (same MVP gap as wind).

### 3.14 compass — `draw_compass` / `_padding` / `draw_simple_compass`  ✅ (hud_compass)
- compass/padding: delegate to `g->mon_info(w[,1])` — 3×3 directional monster grid (graphical in tiles mode).
- simple_compass: one-line `N(2) SE(1) …` from `u.get_mon_visible().visible_count_by_dir` (9 counts: N/NE/E/SE/S/SW/W/NW/local).

### 3.15 messages/log — `draw_messages` / `draw_messages_classic`  ✅ (hud_log)
- Both call `Messages::display_messages(w, leftmargin, topline=0, maxlength, line=getmaxy(w)-2)`. The Messages subsystem renders rows with per-message age coloring.

### 3.16 minimap ✅ done, reduced fidelity (hud_map) — `draw_minimap` / `draw_mminimap`
- `draw_minimap(u,w)` / `draw_mminimap(u,w)` (`g->draw_pixel_minimap(w)`, the true GPU RTT pixel minimap) are both **removed** — `overmap_ui::draw_overmap_chunk` had zero remaining callers after the Tier-10 curses rip-out and was replaced outright rather than kept dead.
- **Successor:** `overmap_ui::overmap_chunk_rows(you, global_omt, width, height)` — same terrain/note/vehicle cell lookups as the deleted `draw_overmap_chunk`, emitting `colorize()`-tagged single-glyph cells as text rows instead of `mvwputch`. `hud_map` calls it at 13×7 (odd dims keep the avatar's tile dead-centre) and joins the rows into a `white-space:pre` div (`.hud-map` in sidebar_hud.rcss).
- **RML notes:** ✅ resolved as a **text minichunk, not the pixel-minimap RTT** — a locked MVP decision (`plans/qud-hud.md`), not a partial implementation. Also dropped: the past-the-grid-edge mission arrow and border horde markers (graphical bonuses layered on the *pixel* minimap's fixed 7-cell border offset — not portable to an arbitrary text row count). The player-position and mission-target cells substitute inline `background-color` spans for curses reverse-video (no `cata_text_to_rml` equivalent exists).

### 3.17 hint — `draw_hint`  ✅ (hud_hint)
- `<keybind> to open sidebar options`; key = `press_x(ACTION_TOGGLE_PANEL_ADM)` (light_green).

### 3.18 ai_goal ✅ done (hud_ai_goal) — `draw_ai_goal`
- `Goal: <current need>` — top behaviour-tree need run against the avatar:
  ```cpp
  behavior::tree needs;
  needs.add( &string_id<behavior::node_t>( "npc_needs" ).obj() );
  behavior::character_oracle_t player_oracle( &u );
  std::string current_need = needs.tick( &player_oracle );
  ```
- **RML notes:** ✅ resolved — `hud_ai_goal` ticks the `"npc_needs"` behaviour tree via a
  fresh `character_oracle_t(&u)` on every call (no caching; matches the debug-grade original).
  `current_need == "idle"` (or empty) normalizes to `_("none")` before display; single text
  field, `colorize(string_format(_("Goal: %s"), goal), c_light_gray)`.

---

## 4. Cross-cutting rebuild notes
- **`morale_emotion`** emoticon string: "horizontal" style branches by face type
  (`get_face_type(u)`: feline/ursine/bird/human) across 9 morale bands; non-horizontal style
  `8D/:D/:3/:)/:|/):/D:/D8`. Used by stats/limb/health.
- **Icon + animation panels** (wind, moon): ✅ SVG done — both render a sized `<img>` (`?px=32`,
  via the new `rmlui_render_interface::LoadTexture` sized-SVG path) picked by `wind_arrow_icon`/
  `moon_phase_display`. ⚠ Pop-on-change animation is NOT ported (the curses
  `sidebar_anim::registry` tween engine is curses-coupled and out of scope) — icons swap
  instantly on state change. Not tracked as a gap; acceptable MVP fidelity loss.
- **`HEALTH_STYLE`** option ("number" vs bar) + **`effect_got_checked`** switch HP/stamina
  rendering between bar-with-dots and raw numbers.
- **Right-alignment / digit-width x-math** (`get_int_digits`, `utf8_width` offsets) are curses
  artifacts — use flex / right-align in RML.
- **Graphical / subsystem-delegated panels** (compass `mon_info`, vehicle
  `print_fuel_indicators`, Messages log, minimap): ✅ all resolved with new text accessors —
  `hud_compass` (raw threat grid), `vehicle::fuel_indicator_lines()`, `Messages::recent_messages_colored`,
  `overmap_ui::overmap_chunk_rows()`. The one genuinely unresolved graphics piece is the pixel-minimap
  GPU RTT itself (§3.16) — dropped by locked MVP decision, not deferred as a TODO.

---

*Generated 2026-06-23 after the panels curses rip-out (P1/P2a/P2b). Exact deleted bodies:
`git show d40d1cd49e:src/panels.cpp`. Related: `RMLUI_MIGRATION_PLAN.md` §8.2; the Tier-7 HUD
strategy section; memory `project_rmlui_gate_backlog`.*
