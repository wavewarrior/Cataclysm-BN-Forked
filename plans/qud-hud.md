# Qud-Style RmlUi HUD — Execution Plan

## Context

Flesh out the RmlUi sidebar HUD (`src/panels.cpp` producers + `data/gui/sidebar_hud.rml`) into a complete, Caves-of-Qud-inspired HUD. Reference worklist: `plans/RMLUI_HUD_PANEL_REFERENCE.md` (16 of 30 `custom`-layout widgets have producers; 14 GAP: val_* ×4, bodygraph ×4, vehicle, wind, moon, armor_comp, map, ai_goal). End state: every gap panel renders real content, the HUD carries Qud's visual language (dark-teal panels, inset all-caps headers, div fill bars, per-word semantic color), and the screen gains Qud-style top/bottom chrome strips carved out of the viewport.

User decisions (locked, from planning session):
- **Layout**: Full Qud screen chrome — top status strip (2 rows) + bottom effects strip (1 row) + the existing right sidebar column. Strips span the viewport width only; sidebar stays full-height.
- **Palette**: Qud teal palette scoped to the HUD only (new `hud-*` theme tokens; global gruvbox menus untouched. `nc_color`→hex mapping via `game_colors` stays shared — producer text colors remain the game palette).
- **Map widget**: colored-text overmap chunk (refactor `overmap_ui::draw_overmap_chunk` into a text producer). NOT the GPU RTT pixel minimap.
- **Compass**: full threat grid — 3×3 directional grid of colored monster symbols + compact name list. Replaces counts-only `hud_compass` content.
- No icon pop animations in this pass (RCSS keyframes wiring deferred; static SVG icons only).

## Current-state facts (verified this session)

- `src/panels.cpp` anonymous namespace: `hud_row_model { Rml::String rml; bool flex; }` (~line 1039); producer registry `g_hud_producers` = `std::vector<hud_producer_entry>` of `{ const char *panel_name; std::string(*produce)(avatar&); }` (~1443–1507); `hud_producer(name)` CI-matches by name (~1509); `sidebar_hud_open/sync/close/active/has_producer/coverage_report` (~1528–1683); `sidebar_hud_apply_rect()` sets `#hud-sidebar` left/top/width/height as % (~1575–1595). Sync wraps every producer through `cata_text_to_rml(produce(u))` — producers can NOT emit raw RML today (~1622).
- `data/gui/sidebar_hud.rml`: one `data-model="sidebar_hud"` body; `#hud-sidebar` flex column; rows via `data-for="row : rows"` with `data-class-flex="row.flex"` `data-rml="row.rml"`.
- `data/gui/sidebar_hud.rcss`: `#hud-sidebar` absolute (rect injected by C++), `.hud-row` block + `white-space: pre-wrap`, `.hud-row.flex { flex: 1 1 auto; }`. Body transparent.
- `data/gui/theme.json`: `"rcss"` map of `{{token}}` → hex substituted literally into every menu `.rcss` at load (RmlUi 6.2 has no CSS vars); `"game_colors"` overrides `nc_color`→hex. Live-editable in F4 Theme tab.
- Viewport carve point: `src/game.cpp` `create_or_get_main_ui_adaptor()` resize lambda (~3718–3767): `TERRAIN_WINDOW_HEIGHT = TERMY;`, `w_terrain = newwin(H, W, point(sidebar_left, 0))`. `game::draw_panels` (~3873–3891) only opens/syncs the HUD; guarded by `uquit != QUIT_NO || !sidebar_hud_rmlui_enabled()`.
- `sidebar_hud_rmlui_enabled()` — `bool&` registered in `src/rml_toggle_registry.cpp:62` (F4 toggle, default true). Open() additionally gates on `rmlui_layer::ready()`.
- `window_panel` (src/panels.h:40–64): public `draw`, `render`, `dynamic_height`, `toggle`, `always_draw`; ctor `(draw_func, nm, ht, wd, default_toggle_, render_func = default_render, force_draw = false)`.
- val_* widgets: `make_value_widget_panel` (panels.cpp ~1805–1870) still builds a **curses** draw closure (dead — whole-sidebar suppression means it never runs); HUD side shows `[Pain]` placeholders because `hud_producer("Pain")` misses. Structured providers exist in the same TU: value getter switch (~1690–1720 region), color switch ending ~1757, `value_var_max(...)` (~1759+), `value_widget_name(id)` ("val_pain"→"Pain").
- `make_bodygraph_widget_panel` (panels.cpp:1882–1890): name-only, no draw, placeholder.
- `overmap_ui::draw_overmap_chunk` (panels.cpp:437–~533, decl panels.h:29–34): **zero callers** anywhere — free to change signature.
- Monster data: `monster_visible_info` (src/avatar.h:37–63): `unique_mons[9]` = `vector<pair<const mtype*, int>>`, `unique_types[9]` = `vector<npc*>`, `dangerous[8]`, `visible_count_by_dir[9]`; index layout `7 0 1 / 6 8 2 / 5 4 3` (0=N clockwise, 8=local). `mtype::sym` (std::string, 1 cell) + `mtype::color` (src/mtype.h:301, 306). Accessor `u.get_mon_visible()`.
- Weather/moon: `get_wind_arrow(int)`, `get_wind_desc(double)`, `get_wind_color(double)`, `get_local_windpower(...)` (src/weather.h:126–150); `get_weather().get_temperature(tripoint_abs_ms) -> units::temperature` + `print_temperature(units::temperature)` (weather.h:117–217); `get_moon_phase(time_point)` → `moon_phase` enum MOON_NEW..MOON_WANING_CRESCENT (src/calendar.h:29–48). Position accessor: `u.abs_pos()` (creature.h:538).
- Vehicle: `vehicle::fuel_indicator_lines()` (vehicle.h:843, impl vehicle_display.cpp:321 — colour-tagged text, one line per fuel); `veh->face.to_string_azimuth_from_north()` (tileray.h:52); `veh->cruise_on` (vehicle.h:1836); `veh->get_takeoff_speed()` (vehicle.h:1147); `veh->strain()`.
- Effects source: `character_display.cpp` `disp_info` builds `effect_name_and_text` (lines ~1000–1081: `ch.get_all_effects()` + `disp_name()` + pain + starvation/BMI + sunlight traits + addictions). Header `src/character_display.h` namespace `character_display` (precedent: `encumbrance_lines`).
- Log color: `Messages::recent_messages(count)` returns plain pairs (hud_log drops color, flagged FIDELITY GAP at panels.cpp:1341–1344). Colored pattern exists in messages.cpp ~866–870 (`messages_rml_row` uses `colorize(folded, col)` with per-type + age color).
- SVG: `IMG_LoadSizedSVG_IO` available (widget_icon.cpp:13, 64 — rasterize at exact px). `rmlui_render_interface::LoadTexture` (src/lighting/rmlui_render_interface.cpp:666–717) handles `?proc:` prefix else `IMG_Load(source)` — SVGs rasterize at intrinsic size (24×24 viewBox) and blur when scaled.
- Icons: `gfx/widgets/icons.json` maps ids→SVGs: `wind, wind_n, wind_ne, wind_e, wind_se, wind_s, wind_sw, wind_w, wind_nw`, `moon_new, moon_waxing_crescent, moon_first_quarter, moon_waxing_gibbous, moon_full, moon_waning_gibbous, moon_last_quarter, moon_waning_crescent`, `heart`, etc.
- ai_goal tick recipe (plans/RMLUI_HUD_PANEL_REFERENCE.md §3.18): `behavior::tree needs; needs.add(&string_id<behavior::node_t>("npc_needs").obj()); behavior::character_oracle_t oracle(&u); std::string current_need = needs.tick(&oracle);` — `"npc_needs"` exists (data/json/npcs/npc_behavior.json:4); panels.cpp already includes behavior.h + character_oracle.h.
- Tests: `tests/widget_test.cpp` — pure widget/panel-shape tests, no curses context needed. Line 84 asserts value panels carry a curses draw (`CHECK(static_cast<bool>(wp.draw))`) — must flip when the closure is removed.
- Helpers alive in panels.cpp: `morale_emotion` (771), `power_stat` (853), `mana_stat` (880), `safe_color` (898), `carry_weight_string` (947), `focus_color` (panels_utility.h:12), `get_hp_bar`, `hud_limb_health` (~1306), `get_armor(u,bp,truncate)` (~746). `get_temp`/`get_moon`/`wind_arrow_icon`/`get_armor_comp` were deleted — re-specced below.

## Approach

Phases build in order; the tree compiles and `[widget][sidebar]` tests pass after each. Phase 2 is independent of 3–5 (only needs Phase 1's data-model strings) — 2 and 3 can be done in parallel after 1.

### Phase 0 — persist the plan

Copy this file verbatim to `plans/qud-hud.md` (repo convention: plans live in `plans/`; this file is the session copy).

### Phase 1 — producer plumbing: raw-RML producers, per-row titles, per-panel producers

1. **Extend `hud_producer_entry` and `hud_row_model`** in panels.cpp:
   ```cpp
   struct hud_producer_entry {
       const char *panel_name;
       std::string( *produce )( avatar & );
       const char *title = nullptr; // untranslated; Qud inset header when set
       bool raw = false;            // produce() returns ready RML (skip cata_text_to_rml)
   };
   struct hud_row_model {
       Rml::String rml;
       Rml::String title; // empty → no header chrome
       bool flex = false;
   };
   ```
   Register `title` in the data-model struct handle next to `rml`/`flex` (same `RegisterStruct` block, ~panels.cpp:1546).
   Change `hud_producer(name)` to return `const hud_producer_entry *` (nullptr when missing) so sync sees title+raw. Update its two callers: `sidebar_hud_sync` and `sidebar_hud_has_producer` (grep `hud_producer(` in panels.cpp — exactly those two).

2. **Per-panel instance producers** — add to `window_panel` (src/panels.h, public members next to `draw`):
   ```cpp
   // RmlUi HUD content producer bound to this panel instance (value/bodygraph
   // widgets know their own widget id; the name-keyed g_hud_producers table can't).
   // Checked FIRST by sidebar_hud_sync. Output is colorize()-tagged text unless
   // hud_raw, then it is ready RML.
   std::function<std::string( avatar & )> hud_produce;
   bool hud_raw = false;
   ```
   (No Rml types in panels.h — plain `std::function<std::string(avatar&)>` + `bool`.)

3. **Rewrite the `sidebar_hud_sync` row loop** (panels.cpp:1614–1628):
   ```cpp
   for( const window_panel &panel : ...get_current_layout() ) {
       if( !panel.toggle || !panel.render() ) { continue; }
       hud_row_model row;
       if( panel.hud_produce ) {
           const std::string s = panel.hud_produce( u );
           row.rml = panel.hud_raw ? Rml::String( s ) : cata_text_to_rml( s );
           row.title = "";                     // instance widgets: no header
       } else if( const hud_producer_entry *p = hud_producer( panel.get_name() ) ) {
           const std::string s = p->produce( u );
           if( s.empty() ) { continue; }       // producer opted out (e.g. vehicle w/o vehicle)
           row.rml = p->raw ? Rml::String( s ) : cata_text_to_rml( s );
           row.title = p->title ? Rml::String( to_upper_case( _( p->title ) ) ) : "";
       } else {
           row.rml = cata_text_to_rml( colorize( "[" + panel.get_name() + "]", c_dark_gray ) );
       }
       row.flex = panel.get_height() < 0;
       g_hud_data->rows.push_back( std::move( row ) );
   }
   ```
   `sidebar_hud_has_producer(name)` becomes `hud_producer(name) != nullptr` (unchanged semantics). Coverage report: also count panels with `hud_produce` set as covered — add that check in `sidebar_hud_coverage_report`'s loop (it iterates `window_panel`s already).

4. **Row template** in `data/gui/sidebar_hud.rml` — replace the single row div with:
   ```rml
   <div class="hud-row" data-for="row : rows" data-class-flex="row.flex">
       <div class="hud-row-header" data-if="row.title != ''" data-rml="row.title"></div>
       <div class="hud-row-body" data-rml="row.rml"></div>
   </div>
   ```
   (`data-rml` moves from the row to `.hud-row-body`; flex rows need `.hud-row-body` to inherit fill: add `.hud-row.flex .hud-row-body { flex: 1 1 auto; }` and make `.hud-row.flex { display: flex; flex-direction: column; }` in the rcss.)

5. **Qud theme tokens** — add to `data/gui/theme.json` `"rcss"` map (hyphenated keys match existing `bg-hard` style):
   ```json
   "hud-bg":        "#0d2020f0",
   "hud-bg-panel":  "#102828e8",
   "hud-fg":        "#80c0a8ff",
   "hud-fg-bright": "#d0f0e0ff",
   "hud-border":    "#2a5a50ff",
   "hud-sel":       "#305040ff",
   "hud-accent":    "#c8a820ff",
   "hud-red":       "#e05050ff",
   "hud-bar-bg":    "#123030ff"
   ```

6. **Qud chrome RCSS** — rewrite `data/gui/sidebar_hud.rcss` additions (keep the existing `#hud-sidebar` rect/flex machinery and `.hud-row` pre-wrap rules):
   ```rcss
   #hud-sidebar { background-color: {{hud-bg}}; border-left: 1dp {{hud-border}}; padding: 2dp; }
   .hud-row { margin-bottom: 3dp; }
   .hud-row-header {
       color: {{hud-fg-bright}}; background-color: {{hud-bg-panel}};
       border-bottom: 1dp {{hud-border}};
       font-size: 0.9em; letter-spacing: 1px; padding: 0dp 4dp;
   }
   .hud-row-body { color: {{hud-fg}}; }
   /* Qud fill bar: producer emits <div class="qbar"><div class="qbar-fill" style="width:N%; background-color:#rgb"/></div> */
   .qbar { display: inline-block; width: 60dp; height: 8dp; background-color: {{hud-bar-bg}};
           border: 1dp {{hud-border}}; vertical-align: middle; }
   .qbar-fill { height: 100%; }
   /* Threat compass grid */
   .tc-row { display: block; white-space: pre; }
   .tc-cell { display: inline-block; width: 3em; text-align: center; }
   .tc-cell.danger { background-color: {{hud-sel}}; }
   /* Map chunk + log */
   .hud-map { white-space: pre; text-align: center; }
   .hud-icon { vertical-align: middle; }
   ```
   Exact dp values are starting points; the implementer tunes visually in-game (F4 theme tab reloads colors live).

7. **Title assignments** in `g_hud_producers` (untranslated strings, `_()` at sync): limbs* → "Vitals"; armor*/Armor → "Armor"; log* → "Log"; location*/Location → "Location"; compass entries → "Threats"; needs/needs_narrow → "Needs"; Stats/stats* → "Stats"; weapon → "Weapon"; new map → "Map"; new vehicle → "Vehicle". All other entries (hint, sound, wgt/vol, mana, movement, moon, wind, armor_comp, val_*, bodygraph) keep `title = nullptr` (single-line rows, no header).

### Phase 2 — Qud screen chrome: top strip (2 rows) + bottom strip (1 row) + viewport carve

Depends only on Phase 1 step 1 (data model). Parallel-safe with Phases 3–5.

1. **Carve predicate** — add to panels.h (next to `sidebar_hud_active`) and panels.cpp:
   ```cpp
   // Rows of standard-font cells reserved above/below the viewport for the Qud
   // HUD chrome strips. 0 whenever the RmlUi HUD can't render (curses fallback
   // keeps the full viewport).
   int sidebar_hud_top_rows();    // = 2 when sidebar_hud_rmlui_enabled() && rmlui_layer::ready(), else 0
   int sidebar_hud_bottom_rows(); // = 1 when ..., else 0
   ```
   panels.cpp includes `lighting/rmlui_layer.h` already.

2. **Viewport carve** — game.cpp resize lambda (3718–3758):
   ```cpp
   const int top = sidebar_hud_top_rows();
   const int bottom = sidebar_hud_bottom_rows();
   TERRAIN_WINDOW_HEIGHT = TERMY - top - bottom;
   TERRAIN_WINDOW_WIDTH  = TERMX - ( sidebar_left + sidebar_right );
   TERRAIN_WINDOW_TERM_WIDTH  = TERRAIN_WINDOW_WIDTH;
   TERRAIN_WINDOW_TERM_HEIGHT = TERRAIN_WINDOW_HEIGHT;
   ...
   w_terrain = w_terrain_ptr = catacurses::newwin( TERRAIN_WINDOW_HEIGHT, TERRAIN_WINDOW_WIDTH,
                               point( sidebar_left, top ) );
   ```
   `to_map_font_dimension` already converts the carved height. POSX/POSY math unchanged (window-relative).

3. **Resize on HUD lifecycle transitions** — in `sidebar_hud_open()` after the doc successfully opens, and in `sidebar_hud_close()` after closing an open doc, call `g->mark_main_ui_adaptor_resize()`. This applies/releases the carve when the F4 toggle flips or the layer becomes ready. (`mark_resize` during a redraw defers to the next frame — safe, same pattern as `toggle_pixel_minimap` game.cpp:503–510.)

4. **Strip containers** — sidebar_hud.rml body gains, before `#hud-sidebar`:
   ```rml
   <div id="hud-topbar">
       <div class="hud-strip-row" data-rml="topbar_rml"></div>
       <div class="hud-strip-row" data-rml="topbar2_rml"></div>
   </div>
   <div id="hud-botbar"><div class="hud-strip-row" data-rml="botbar_rml"></div></div>
   ```
   Data model: add `Rml::String topbar_rml, topbar2_rml, botbar_rml;` to the model struct, register, fill in sync, `DirtyVariable` each. RCSS: `#hud-topbar, #hud-botbar { position: absolute; background-color: {{hud-bg}}; border-bottom/top: 1dp {{hud-border}}; overflow: hidden; }`, `.hud-strip-row { white-space: pre; color: {{hud-fg}}; }`.

5. **Strip rects** — extend `sidebar_hud_apply_rect()` (panels.cpp:1575–1595): compute `sidebar_left/right` cells from `panel_manager::get_width_left/right()`, then set as percentages of TERMX/TERMY:
   - `#hud-topbar`: left = `width_left/TERMX`, top = 0, width = `(TERMX - width_left - width_right)/TERMX`, height = `sidebar_hud_top_rows()/TERMY`.
   - `#hud-botbar`: same left/width, top = `(TERMY - sidebar_hud_bottom_rows())/TERMY`, height = `sidebar_hud_bottom_rows()/TERMY`.
   - `#hud-sidebar`: unchanged (full height).
   Reuse the existing `GetElementById` + `SetProperty("%.4f%%")` pattern in that function.

6. **Top row 1 producer** — `static std::string hud_topbar( avatar &u )` (colorize-tagged text; joined with two-space gaps):
   - `u.get_name()` in `c_white`.
   - `T:` + `print_temperature( get_weather().get_temperature( u.abs_pos() ) )` — always shown (no thermometer gate; deleted with the curses HUD, Qud shows temp always).
   - Status words, only when non-empty: `u.get_hunger_description()`, `u.get_thirst_description()`, `u.get_fatigue_description()` — each returns `pair<string, nc_color>`; skip pairs whose text is empty. Add `Pain` via the same expression `hud_needs_text` uses (anchor: panels.cpp `desc(pain)` ~1121).
   - `Wgt` + `carry_weight_string(u)` colored like `hud_wgtvol` (panels.cpp:1162 overload logic).
   - Right side (pad with spaces to strip width — width unknown in text; instead emit two spans and let RCSS place them: make row 1 raw RML with `<span class="strip-left">…</span><span class="strip-right">…</span>`, `.strip-right { float: right; }`): date + time exactly as `hud_location` rows Date/Time (panels.cpp:1392–1402).
7. **Top row 2 producer** — `static std::string hud_topbar2( avatar &u )` (**raw RML**): HP/stamina/mana bars Qud-style:
   - `HP` = torso `u.get_part_hp_cur/max( bodypart_id("torso") )`: `qbar` fill div, width% = cur*100/max clamped 0–100, fill color = hex of `get_hp_bar(cur,max).second` — convert `nc_color` to hex with the same helper `cata_text_to_rml` uses (grep `curses_color_to_RGB\|nc_color_to_hex` in src/rml_util.cpp; reuse it, export from rml_util.h if static). Text `cur/max` beside the bar.
   - `STA` stamina bar: `u.get_stamina()/u.get_stamina_max()`, color via `get_hp_bar(...).second`.
   - `MANA` bar only when `u.magic->max_mana(u) > 0`: `mana_stat(u)` color (panels.cpp:880).
   - `FOCUS` number colored `focus_color(u.focus_pool)`; `SPD` `u.get_speed()`; move mode string+color as `hud_sound_text` does (panels.cpp:1241–1256).
8. **Bottom row producer** — `static std::string hud_botbar( avatar &u )` (raw RML, left/right spans):
   - Left: `EFFECTS:` + effect names joined `" :: "`, `c_light_gray`, from the new shared helper (next step). Cap at 8 names then `(+n)`.
   - Right: `SAFE` colored `safe_color()` (panels.cpp:898) + `HOSTILES: n` where n = `u.get_mon_visible().nearby_hostile_count`, `c_red` when >0 else `c_dark_gray`.
9. **Extract the effects list** — new `character_display::effect_name_and_text( const Character &ch ) -> std::vector<std::pair<std::string, std::string>>` in src/character_display.h/.cpp: move lines ~1000–1081 of character_display.cpp (the `effect_name_and_text` assembly incl. pain, BMI/starvation, TROGLO sunlight, addictions) verbatim into it; `disp_info` calls it. hud_botbar uses `.first` of each pair. Pure move — no behavior change.

### Phase 3 — gap producers, text tier (each independent; all follow Phase 1)

Register every new producer in `g_hud_producers` with the names listed; all take `avatar &u`.

1. **ai_goal** — `hud_ai_goal`: reference §3.18 recipe (verbatim snippet above in facts). Output `Goal: <need>` `c_light_gray`; when tick returns "idle"/empty → `Goal: none`. Register `{ "ai_goal", hud_ai_goal }`.
2. **armor_comp** — `hud_armor_comp`: one row `H:x T:x A:x L:x F:x`. For each of bodypart_id `head/torso/arm_r/leg_r/foot_r`: topmost worn item covering it (same iteration as `get_armor`, panels.cpp:746–752); x = `item->symbol()` colored `item->color()`; no item → `-` in `c_dark_gray`. Register `{ "armor_comp", hud_armor_comp }`.
3. **moon** — `hud_moon` (**raw RML**): `<img class="hud-icon" src="../gfx/widgets/<icon>.svg?px=32"/>` + `Moon : <phase name>` + `  Temp : <print_temperature(...)>`. Phase→name+icon switch over `get_moon_phase( calendar::turn )`:
   MOON_NEW→(_("New moon"), moon_new), WAXING_CRESCENT→(_("Waxing crescent"), moon_waxing_crescent), HALF_MOON_WAXING→(_("First quarter"), moon_first_quarter), WAXING_GIBBOUS→(_("Waxing gibbous"), moon_waxing_gibbous), FULL→(_("Full moon"), moon_full), WANING_GIBBOUS→(_("Waning gibbous"), moon_waning_gibbous), HALF_MOON_WANING→(_("Last quarter"), moon_last_quarter), WANING_CRESCENT→(_("Waning crescent"), moon_waning_crescent).
   Write the mapping as a standalone pure function `moon_phase_display( moon_phase ) -> struct { const char *name; const char *icon; }` (testable). Register `{ "moon", hud_moon }, { "moon_narrow", hud_moon }`. Img src path: relative to the document (`data/gui/`) — verify one existing `<img>` usage in data/gui/*.rml for the correct base (`grep -l "<img" data/gui`); if none, use the path form `widget_icon.cpp` derives (`PATH_INFO::gfxdir() + "widgets/"`) by emitting an absolute path from the producer.
4. **wind** — `hud_wind` (**raw RML**): sector icon + `Wind : <get_wind_desc(windpower)>` colored `get_wind_color(windpower)`.
   - `windpower = get_local_windpower( get_weather().windspeed, ACTIVE_OVERMAP_BUFFER.ter( u.abs_omt_pos() ), u.abs_pos(), get_weather().winddirection, g->is_sheltered( u.bub_pos() ) )` (signature weather.h:126; overmap-ter pattern from `hud_location` panels.cpp:1366).
   - Pure fn `wind_arrow_icon( int dirangle ) -> const char *`: `<0||>=360`→"wind"; `≤23||>338`→"wind_n"; `≤68`→"wind_ne"; `≤113`→"wind_e"; `≤158`→"wind_se"; `≤203`→"wind_s"; `≤248`→"wind_sw"; `≤293`→"wind_w"; else "wind_nw" (reference §3.12).
   - Text fallback appended: `get_wind_arrow( winddirection )`.
   Register `{ "wind", hud_wind }`.
5. **vehicle** — `hud_vehicle`: guard `const optional_vpart_position vp = g->m.veh_at( u.bub_pos() ); if( !vp ) return "";` (empty → row skipped per Phase-1 sync rule; the widget's `show_if: veh_panel` already gates on `u.in_vehicle`).
   - Row 1: `Head: <veh->face.to_string_azimuth_from_north()>`, speed `convert_velocity( veh->velocity, VU_VEHICLE )` + unit from `get_option<std::string>("USE_METRIC_SPEEDS")`, colored by `veh->strain()`: ≤0 `c_light_blue`, ≤0.2 `c_yellow`, ≤0.4 `c_light_red`, else `c_red` (reference §3.8). If `veh->cruise_on`: append `> <cruise>` in `c_light_green`. If `veh->has_part("WING")`: append `flight: <get_takeoff_speed()>` green when current speed ≥ takeoff.
   - Rows 2+: `veh->fuel_indicator_lines()` joined by `\n` (already colour-tagged).
   Register `{ "vehicle", hud_vehicle, "Vehicle" }`.
6. **val_*** — replace `make_value_widget_panel`'s curses closure (panels.cpp:1805–1870) with a `hud_produce` lambda on the returned panel (delete the closure and its `sidebar_anim`/`draw_widget_icon` calls; pass `{}` as draw_func):
   - Capture widget id; body: `val = <value getter switch>`, `color = <color switch ~1720–1757>`, `max = value_var_max(...)`; output `"<label>  <get_hp_bar(val,max).first + " N%">"` when bounded else `"<label>  N"`, label `c_light_gray`, rhs colored. Reuse the exact getter/color/max helpers already in the TU (re-read panels.cpp 1690–1770 for their names before editing).
   - Do NOT delete `src/sidebar_anim.*` / `src/widget_icon.*` — widget_icon feeds the icon registry consumed elsewhere; only the value-widget closure goes.
   - Update tests/widget_test.cpp:84: `CHECK_FALSE( static_cast<bool>( wp.draw ) );` + `CHECK( static_cast<bool>( wp.hud_produce ) );`.
7. **bodygraph** — give `make_bodygraph_widget_panel` (panels.cpp:1882–1890) a `hud_produce` lambda capturing `widget_var dim = w.var()`: 2-column grid (the `hud_limbs` iteration pattern, panels.cpp:1329–1338) of `body_part_hp_bar_ui_text(bp)` labels, each colored by new pure helper `bodygraph_bp_color( const avatar &u, const bodypart_id &bp, widget_var dim ) -> nc_color`:
   - `body_graph`: `u.limb_color( bp, true, true, true )`.
   - `body_graph_temp`: ladder on `u.get_part_temp_cur(bp)` copied from character_display.cpp:~725–745 (BODYTEMP_* thresholds → c_blue…c_red).
   - `body_graph_encumb`: `u.encumb(bp)` <10 `c_light_gray`, <40 `c_yellow`, else `c_light_red`.
   - `body_graph_status`: priority: bleed effect on bp → `c_red`; bite → `c_yellow`; infected → `c_green`; splinted (`u.worn_with_flag(json_flag_SPLINT, bp)`) → `c_blue`; else `c_dark_gray`. (Effect ids: `effect_bleed`, `effect_bite`, `effect_infected` — grep panels.cpp/character.cpp for the declared `efftype_id` constants and reuse.)
   - `body_graph_wet`: `c_light_blue` when the bp wetness is >0 (grep `body_wetness\|get_part_wetness` in character.h for the getter; if BN truly lacks per-bp wetness, fall back to `c_dark_gray` always — the widget doc already flags wet as degraded), else `c_dark_gray`.
   Update tests/widget_test.cpp:109 (`CHECK_FALSE(wp.draw)` stays; add `CHECK(static_cast<bool>(wp.hud_produce))`).

### Phase 4 — rich content (follows Phase 1; items independent of each other)

1. **Threat compass grid** — rewrite `hud_compass` (panels.cpp:1411–…) as **raw RML** producer (update its `g_hud_producers` entries to `raw = true, title = "Threats"`):
   - Grid index map `{7,0,1, 6,8,2, 5,4,3}`; three `.tc-row` divs of three `.tc-cell` spans. Cell content: up to 3 monsters from `unique_mons[i]` as `mtype->sym` colored `mtype->color` (convert via the shared nc_color→hex helper), plus `@` per npc in `unique_types[i]` (colored `c_light_red`); center cell (8) shows `⌂` `c_white`. Empty cell: `·` `c_dark_gray`. Cell class `danger` when `i<8 && dangerous[i]`.
   - Below the grid: one line per direction with monsters (max 4 lines, then `+n dirs`): `<DIR> <name×count, name>` — direction labels N/NE/E/SE/S/SW/W/NW colored `c_red` when dangerous else `c_light_gray`; monster names colored `mtype->color`, `×count` when count>1. HTML-escape names through `cata_text_to_rml` per text fragment.
2. **Qud limb bars** — rewrite `hud_limbs` (panels.cpp:1325–1339) as raw RML: per limb one row: 5-char label colored `u.limb_color(...)` + `qbar` div (width% = `get_part_hp_cur*100/get_part_hp_max`, fill = hex of `get_hp_bar(...).second`) + `cur/max` text. Broken-limb branch: reuse `hud_limb_health`'s splint/mend logic (panels.cpp:1306 region) rendering mend% as bar fill in `c_blue`/`c_dark_gray`. Keep the `HEALTH_STYLE=="number"` option branch as plain numbers.
3. **Colored log** — add `static std::vector<std::string> Messages::recent_messages_colored( size_t count );` to src/messages.h/messages.cpp returning colorize()-tagged lines with per-type color + age fade — reuse the exact color logic of the RML messages window (messages.cpp ~860–880: `msgtype_to_color( type, /*bold=*/false )` + the folded/age pattern in `messages_rml_row`). Point `hud_log` (panels.cpp:1345–1357) at it; delete the FIDELITY GAP comment.
4. **Map = colored-text overmap chunk** — change `overmap_ui::draw_overmap_chunk` (panels.h:31, panels.cpp:437) into
   ```cpp
   // Renders a width×height overmap chunk centred on the avatar as colour-tagged
   // text rows (one string per row). start=(-1,-1) centres. Text port of the old
   // curses minimap chunk; zero curses callers remained.
   std::vector<std::string> overmap_chunk_rows( const avatar &you, const tripoint_abs_omt &global_omt,
                                                point start, int width, int height );
   ```
   Same body, `mvwprintz(w, p, color, sym)` → append `colorize(sym, color)` into the row string (keep the cursor/horde/note logic intact). Delete the old signature (no callers — verified). New producer `hud_map`: `overmap_chunk_rows( u, u.abs_omt_pos(), point(-1,-1), 13, 7 )` joined `\n`, wrapped `<div class="hud-map">…</div>` (raw), registered `{ "map", hud_map, "Map", true }`. 13×7: odd dims keep the cursor centred; height 7 matches the old minimap. The `map` widget is the `-1` flex sentinel — the chunk top-aligns in the flex row.
5. **Sized SVG loading** — extend `rmlui_render_interface::LoadTexture` (src/lighting/rmlui_render_interface.cpp:666–717): before the `IMG_Load` fallback (line 691), detect `\.svg\?px=(\d+)$` (plain `rfind("?px=")` parse — no regex): strip the query, open with `SDL_IOFromFile`, rasterize `IMG_LoadSizedSVG_IO( io, px, px )` (copy widget_icon.cpp:55–70 incl. error logging), continue into the existing RGBA32-convert + upload path (lines 696–706). No query → unchanged behavior. Malformed px → log via existing `dbg(DL::Warn)` pattern and fall through to plain `IMG_Load` on the stripped path.

### Phase 5 — cleanup & verification (last; gated on the HUD demonstrably working)

1. Update `plans/RMLUI_HUD_PANEL_REFERENCE.md` header: coverage now 30/30 (val_*/bodygraph via `hud_produce`); note map = text chunk, pixel-minimap RTT still deferred.
2. Tests (tests/widget_test.cpp additions + the two flipped asserts from Phase 3):
   - `moon_phase_display` maps all 8 phases to non-empty name+icon; icon ids ∈ icons.json set (hardcode expected ids).
   - `wind_arrow_icon` sector boundaries: 0→wind_n, 23→wind_n, 24→wind_ne, 338→wind_nw…wait 339→wind_n, 400→"wind", -1→"wind".
   - Coverage gate: after `reload_widget_layouts`, every widget in layout `custom` satisfies `sidebar_hud_has_producer(name) || panel.hud_produce` — the 30/30 assertion.
3. Format: `cmake --build build --target format` (C++), `cmake --build build --target style-json-parallel` (theme.json). Fix diffs.
4. Build (background job, ≥1200 s timeout, NEVER killed mid-run — corrupts .ninja_deps):
   `cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles`
5. Run `./out/build/osx-arm-slim/tests/cata_test-tiles "[widget][sidebar]"`.
6. Commit atomically per phase (Conventional Commits, no body): e.g. `feat(hud): raw-RML producers + Qud panel chrome`, `feat(hud): Qud top/bottom screen strips`, `feat(hud): fill all 14 sidebar producer gaps`, `feat(hud): threat grid, limb bars, colored log, overmap chunk`, `feat(rmlui): sized SVG texture loading (?px=)`.

## Critical files & anchors

- `src/panels.cpp` — `g_hud_producers` (1443), `sidebar_hud_sync` row loop (1614–1628), `sidebar_hud_apply_rect` (1575), `make_value_widget_panel` closure to delete (1805–1870), `draw_overmap_chunk` body to convert (437). THE file; re-read each region before editing.
- `src/game.cpp:3718–3758` — resize lambda; the only viewport-carve point (`TERRAIN_WINDOW_HEIGHT`, `w_terrain` origin).
- `data/gui/sidebar_hud.rml` + `.rcss` — row template, strip containers, all Qud chrome classes.
- `src/lighting/rmlui_render_interface.cpp:666–717` — LoadTexture; `?px=` SVG branch goes before the IMG_Load fallback (691).
- `src/character_display.cpp:1000–1081` — effects-list block to extract for the bottom strip.

## Verification

All from repo root; binary+tests preset `osx-arm-slim`. **Launch the binary the preset links at repo root, NOT `out/build/.../src`** (stale-binary trap — see skill cbn-osx-slim-binary-launch-path).

1. Build (background, uninterrupted): `cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles`.
2. Unit: `./out/build/osx-arm-slim/tests/cata_test-tiles "[widget][sidebar]"` — includes the new 30/30 coverage gate, moon/wind mapping tests.
3. JSON: `./build-scripts/lint-json.sh` (theme.json, icons untouched but gate anyway).
4. Smoke (the real check): launch the game, load/start a world, then verify in order:
   - Sidebar shows Qud chrome: teal panels, inset ALL-CAPS headers on Vitals/Threats/Location/Log/Map; **zero `[name]` placeholders** — cross-check `debug.log` line `sidebar HUD coverage: 30/30 panels` (logged on first sync).
   - Top strip (2 rows) and bottom strip (1 row) render above/below the map; map viewport is shifted down `2` rows and shortened by `3` — walk around and confirm no tile clipping under the strips and mouse-look selects the tile under the cursor (carve correctness).
   - Limbs panel shows fill bars; take damage (debug menu) → bar shrinks and reddens.
   - Spawn a monster N of the player (debug spawn) → Threats grid N cell shows its colored symbol + name line; bottom-strip HOSTILES increments.
   - Board a vehicle → Vehicle panel shows heading/speed/fuel lines; leave vehicle → row disappears (empty-producer skip).
   - Moon + wind rows show crisp SVG icons (not 24px-blurry — proves `?px=` path).
   - F4 → toggle `sidebar_hud` OFF → strips vanish AND viewport reclaims full height (carve predicate + mark_resize path).
5. `./out/build/osx-arm-slim/cataclysm-bn-tiles --check-mods` if any JSON changed beyond theme.json.

## Assumptions & contingencies

- **Temp always visible** in the top strip (no thermometer-item gate; the old gate died with the curses HUD). Trivial to re-add if the user objects.
- **Strip heights** top=2/bottom=1 standard-font rows, constants in `sidebar_hud_top_rows/_bottom_rows`. If 2 rows can't fit the bar row legibly at small font sizes, bump to 3 — single constant.
- **RmlUi `data-if` + nested `data-rml` inside `data-for`** is assumed to work (standard RmlUi 6.2 data bindings; devui/character_preview use nested bindings). If `data-if` misbehaves inside the loop, fall back to always-emitting the header div and hiding empty ones via RCSS `.hud-row-header:empty { display: none; }` — RmlUi supports `:empty`? If not, set `display:none` via a `data-style-display` binding on the title string.
- **`<img>` relative src** from data/gui documents: if RmlUi resolves relative to the document path, `../../gfx/widgets/x.svg?px=32` works; otherwise emit absolute paths (`PATH_INFO::gfxdir()`) from producers. Decide by testing the moon row first.
- **Per-bp wetness** may not exist in BN (`body_graph_wet` degraded per widget docs) — fallback `c_dark_gray` is pre-decided.
- **`convert_velocity`/vehicle speed formatting**: if `convert_velocity` isn't the BN name, copy the speed formatting from `veh_interact.cpp` `display_stats` (grep `velocity` there) — behavior contract is "same numbers the vehicle interaction screen shows".
- If the carve breaks any overlay that assumed `w_terrain` starts at y=0 (candidates: look_around targeting, pixel-minimap placement), fix the overlay to use `w_terrain`'s window origin (`catacurses::getbegy/x`) rather than reverting the carve.
