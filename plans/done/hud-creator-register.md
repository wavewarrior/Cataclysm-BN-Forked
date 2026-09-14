# Sidebar HUD → character-creator register

## Context

The in-game sidebar HUD (`data/gui/sidebar_hud.{rml,rcss}` + `src/hud_phosphor*.cpp`) is the
"terminal phosphor" design: a character-cell grid in Source Code Pro at one size and one weight,
a single-hue amber luminance ladder, and every frame stroke a real Unicode box-drawing glyph
emitted from C++. The character-creation wizard (`data/gui/newchar*.{rml,rcss}`,
`src/newcharacter_ui.cpp`) is a different register entirely: flex layout, gruvbox neutrals with a
gold accent, a 9→34 dp type scale, tracked uppercase headings, dotted repeating-gradient rules,
pip meters, runic sigils, and a gold left-edge cursor.

This plan rebuilds all six HUD regions in the creator's register — reusing the creator's own
device classes from `data/gui/newchar_common.rcss` rather than restating them — while keeping
Source Code Pro as the HUD's typeface. The cell grid, the box-drawing producers, the amber ladder
and reverse video are retired. The HUD keeps every field it carries today, keeps `hud_anim` /
`hud_shake` / the damage vignette / the GPU radar, and keeps its viewport carve exact.

Three decisions are already made and are not open:

1. **Full register replacement.** Flex documents in the creator's vocabulary; `hud_phosphor`'s
   cell grid, `pad`, `rule`, `bar` and the box-glyph producers are deleted.
2. **Palette: gruvbox neutral tiers + gold accent, no red.** Danger is carried by the creator's
   cursor treatment (gold left edge + dark fill + bold) and by a gold-filled alarm chip, never by
   a second data hue. Desaturating the HUD still preserves every reading except the accent.
3. **Typeface stays "Source Code Pro".** The creator's absolute dp type sizes (11 / 13 / 16 dp)
   are adopted verbatim, so the two surfaces share a type scale without sharing a face.

Before the first edit, copy this file to `plans/hud-creator-register.md` — `AGENTS.md` requires
every plan to exist in the repo as a permanent record, and that copy supersedes
`plans/phosphor-hud.md`, whose binding contract (cell grid, one hue, no CSS border, box-glyph
frames) this plan deliberately retires. Add a one-line pointer at the top of `phosphor-hud.md`
saying so, rather than deleting it: it is the record of why the HUD looked the way it did.

## Palette and typography contract

Everything below is the reference the Approach steps quote. Do not invent a value that is not here.

### Theme tokens (`data/gui/theme.json`, `"rcss"` block)

**Delete** all eleven `ph-*` tokens (`ph-0`…`ph-5`, `ph-k`, `ph-veil`, `ph-veil-0`, `ph-scan`,
`ph-scan-glass`, `ph-vignette`, `ph-glow`) and the whole `"hud_colors"` object.

**Add** (keys are inserted in alphabetical position, matching the file's existing ordering):

| token | value | role |
|---|---|---|
| `accent-rgb` | `c4a832` | bare hex (no `#`, no alpha) for `?proc:runic-icon` URLs — the same gold `nc_icon_dec` uses for an active creator sigil |
| `hud-veil` | `#1d2021d1` | translucent panel ground: `bg-hard` at alpha 0.82 |
| `hud-veil-0` | `#1d202100` | its zero-alpha twin; a gradient stop cannot derive an alpha from another token |
| `hud-scan` | `#0000004d` | per-panel scanline ink |
| `hud-scan-glass` | `#00000013` | full-screen scanline ink |
| `hud-vig` | `#00000075` | faceplate corner darkening |

**Keep unchanged**: the legacy teal `hud-accent` / `hud-bg` / `hud-border` / `hud-fg*` / `hud-red*` /
`hud-sel` / `hud-ink` / `hud-bar-bg` / `hud-crit-bg` / `hud-scroll-thumb` / `hud-bg-{cold,fire,rad}` /
`hud-accent-wash` / `hud-bg-scrim` tokens. `data/gui/throw_radial.rcss` is their only consumer and
is out of scope; do not repurpose or delete them.

### The ink ladder (`hud_runic::ink`)

| `ink` | theme token | hex | relative luma | role |
|---|---|---|---|---|
| `ground` | `bg-hard` | `#1d2021` | 31.5 | panel ground, radar ground |
| `dead` | `bg2` | `#504945` | 74.2 | present but unavailable (unbound key, empty ammo) |
| `rule` | `bg4` | `#7c6f64` | 113.0 | separators, `::`, oldest log rows |
| `label` | `fg4` | `#a89984` | 154.6 | chrome words, limbs at FULL health |
| `datum` | `fg` | `#ebdbb2` | 219.5 | live data, limbs that are DAMAGED |
| `peak` | `fg0` | `#fbf1c7` | 240.2 | newest log line, wielded weapon, the clock |

`ground`…`datum` are ≥38 luma apart, so that span survives greyscale and every form of colour
blindness. `peak` sits only 20.7 above `datum` — that is the creator's own `fg`/`fg0` relationship
and it is deliberately **not** load-bearing: nothing critical is encoded by `peak` alone.

**There is no gold rung, and adding one would be a mistake.** `{{yellow}}` appears in this HUD
only through the stylesheet — `.nc-colname`, `.nc-col-sigil`, `.nc-pip.on`, `.hud-row.crit`'s
left edge, `.hud-log-fresh`'s left edge, `.hud-alarm`'s fill — never as a colour a producer
chooses for a value. That is what keeps the accessibility property true: every distinction a
producer makes is a luminance step, and every gold mark on screen is redundant with a shape
(a left edge, a filled pip, a filled chip) that survives losing the hue.

`ink::inverse` is deleted along with reverse video.

### Device vocabulary — reuse, do not restate

`data/gui/sidebar_hud.rml` links `theme.rcss`, then `newchar_common.rcss`, then `sidebar_hud.rcss`,
in that order. Every rule in `newchar_common.rcss` is class-scoped (there is no bare-element rule
in it), so linking it costs nothing for the classes the HUD does not use. This is the repo's own
promote-on-second-adopter convention applied without renaming anything.

| creator class | comes from | HUD use |
|---|---|---|
| `.nc-rule` | `newchar_common.rcss:848` | every horizontal separator (dotted repeating-gradient, 2 dp ink / 3 dp gap) |
| `.nc-colhead` / `.nc-col-sigil` / `.nc-colname` | `:906`, `:919`, `:926` | every panel head: sigil + tracked-caps name over a 1 dp `bg2` rule |
| `.nc-fact` / `-label` / `-value` / `-sub` | `:612`–`:640` | DOCK fields, VEHICLE fields: 11 dp tracked-caps dim label / 16 dp bold bright value / 11 dp gray sub |
| `.nc-pip` (+ `.on`) | `:804`, `:815` | every meter: HP, stamina, focus, morale, target HP, fuel |
| `.nc-chip` / `.nc-chip-label` | `:651`, `:668` | limb notes (BLEEDING, BITTEN, …) and vehicle status flags |
| `.nc-legend` / `.nc-legend-item` / `-sep` / `-label` | `:711`–`:747` | the KEYS strip, and every compact `LABEL :: value` field in the STATUS strip |
| `.nc-tally` / `-row` / `-name` / `-val` | `:1067`–`:1108` | the SOMA effects roster |
| `.nc-hint` | `:1120` | reserved; not used by the initial HUD build |

Classes the HUD must **not** use: `.nc-card*`, `.nc-band*`, `.nc-tree*`, `.nc-arrow*`, `.nc-step*`,
`.nc-row` (its 22 dp height is the creator's, not the HUD's), `.nc-cell-*`, `.nc-pip.steep` (red).

The document must **not** carry `class="panel"`, `class="runic-region"` or `id="crt-overlay"`:
`lighting::apply_crt()` sweeps every open document for all three and drives their `decorator`
inline every frame with the menus' warm runic ring (`rmlui_layer.cpp:874-1070`).

### Type scale

| element | size | tracking | case | colour |
|---|---|---|---|---|
| HUD body | 16 dp | normal | as authored | `{{fg}}` |
| `.nc-colname` (panel head) | 12 dp | 3 dp (inherited) | uppercase (inherited) | `{{yellow}}` |
| `.hud-subhead` (in-panel section) | 11 dp | 2 dp | uppercase | `{{fg4}}` |
| `.nc-fact-label` | 11 dp | 2 dp | uppercase | `{{fg4}}` |
| `.nc-fact-value` | 16 dp | normal | as authored | `{{fg0}}` |
| `.nc-fact-sub` / `.nc-chip-label` | 11 / 13 dp | normal | as authored | `{{gray}}` |
| list rows (`.hud-row`) | 16 dp | normal | as authored | per rung class |

**Drop the ALL-CAPS data policy.** The creator uppercases headings via RCSS `text-transform` and
leaves data — item names, effect names, monster names, place names — in its natural case. Every
`to_upper_case()` call that exists only to shout a *data value* is removed with its producer.
Labels authored in caps at source (`STR`, `WIELD`, `SPD`) stay as written.

## Approach

Steps 1–2 add new translation units nothing calls yet; the tree builds and every existing test
passes after each. Step 3 is the cutover — after it the HUD renders in the new register. Steps 4–6
are cleanup and coverage and depend on step 3. Step 7 is independent of all of them.

### Step 1 — `src/hud_runic.{h,cpp}`: ink, geometry, markup primitives

New module, namespace `hud_runic`. Nothing includes it yet.

**Palette.** Copy `hud_phosphor.cpp`'s `hex` / `rgba` implementation verbatim, including the
`ui_theme::get_rcss_rgba` lookup and the baked-fallback behaviour (a truncated `theme.json` must
degrade to the correct colours, never to `substitute_tokens`'s magenta). Retable:

```cpp
enum class ink : int { ground, dead, rule, label, datum, peak };

constexpr std::array<std::string_view, 6> ink_tokens = {
    "bg-hard", "bg2", "bg4", "fg4", "fg", "fg0" };
constexpr std::array<std::string_view, 6> ink_fallback = {
    "#1d2021ff", "#504945ff", "#7c6f64ff", "#a89984ff", "#ebdbb2ff", "#fbf1c7ff" };
constexpr std::array<std::string_view, 6> ink_classes = {
    "hud-i0", "hud-i1", "hud-i2", "hud-i3", "hud-i4", "hud-i5" };

auto hex( ink i ) -> std::string;
auto rgba( ink i ) -> std::array<float, 4>;
auto tint( ink i, std::string_view content ) -> std::string;  // <span class="hud-iN">…</span>
```

`tint` is `hud_phosphor::tint` unchanged apart from the class table. There is no `invert`.

**Severity.** Move `is_critical` and `crit_options` across byte-for-byte from `hud_phosphor.h`
(float ratio, threshold at a third, bleeding/bitten critical at any health, `broken` suppresses).
Its behaviour is covered by an existing test that must keep passing.

**Geometry.** No cell grid and no `metrics` ladder — a fixed dp scale, as the creator has:

```cpp
inline constexpr float row_h    = 20.0f;  ///< dp, one HUD list row
inline constexpr float head_h   = 22.0f;  ///< dp, one .nc-colhead (3+3 padding, 12dp caps, 1dp rule)
inline constexpr float chrome_h = 8.0f;   ///< dp, a panel's 1dp border pair + .hud-body padding

struct rect { float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f; };

struct layout {
    float ctx_w = 0.0f, ctx_h = 0.0f;
    rect status, soma, radar, dock, log, keys, vehicle;
};

struct layout_options {
    float ctx_w_dp = 0.0f, ctx_h_dp = 0.0f;
    bool sidebar_right = true;
    int log_lines = 6;
    bool show_vehicle = false;
};

auto layout_for( const layout_options &o ) -> layout;

/// dp height of a panel holding exactly `rows` list rows under a head.
constexpr auto panel_h( int rows ) -> float
{ return head_h + static_cast<float>( rows ) * row_h + chrome_h; }
```

`layout_for` reproduces `hud_phosphor::layout_for`'s algorithm exactly, with cells replaced by dp
and rows replaced by whole multiples of `row_h`. Constants:

```
status_h   = 60.0f               // 4 pad + 20 meta + 11 .nc-rule block + 20 meta + 4 pad + 1 slack
keys_h     = 28.0f               // 4 pad + 20 legend row + 4 pad
col_w      = clamp( ctx_w * 0.20f, 300.0f, 460.0f )
log_w      = clamp( ctx_w * 0.48f, 380.0f, 900.0f )
soma_max   = panel_h( 26 )       // 550
radar_max  = head_h + 300.0f + chrome_h   // 330; a fixed 300dp dot field, letterboxed
                                          // horizontally on a wide column rather than square,
                                          // because a square field at col_w 460 would push DOCK
                                          // off a 720p-class viewport
dock_max   = panel_h( 12 )       // 270
veh_max    = panel_h( 8 )        // 190
log_min    = panel_h( 1 )        // 50
```

`status_h` and `keys_h` are literals, not formulas: they are the measured sum of the RCSS box
the strip actually contains, and deriving them from `row_h` would silently disagree with the
stylesheet the first time a padding value moves. If either strip's RCSS padding changes, change
the literal here in the same commit — `sidebar_hud_top_rows()` carves the terrain viewport out of
exactly this number, so a stale value leaves a strip painting over live map.

Order of construction, unchanged from today:

1. `status = { 0, 0, ctx_w, min( status_h, ctx_h ) }`;
   `keys = { 0, ctx_h - keys_h, ctx_w, keys_h }` clamped so it cannot overlap `status`.
   Anchoring `keys` to `ctx_h` exactly (rather than to a row count) is what removes the residual
   sliver the cell grid had to round away.
2. `body_top = status.h`, `body_bot = keys.y`.
3. Built right-sidebar first, then mirrored as one operation:
   `soma` at `x = 0`; `radar` at `x = ctx_w - col_w`, `y = body_top`; `dock` under `radar`;
   `vehicle` under `dock` when `show_vehicle`. Each height is `min( its max, body_bot - its y )`,
   then floored to `head_h + n * row_h + chrome_h` for the largest whole `n ≥ 0`.
4. Mirror (`x = ctx_w - x - w`) for `soma`, `radar`, `dock`, `vehicle` when `!sidebar_right`.
5. Log, bottom-left: wants `panel_h( log_lines )`, `log_lines == 0` wants nothing.
   **Stage 1** — the log yields: for each of `soma`, `radar`, `dock`, `vehicle` whose x-span
   overlaps the log's, push the log's top down to that region's bottom.
   **Stage 2** — if the log still cannot reach `log_min` and the body can, the log takes `log_min`
   and the columns yield in the fixed order `vehicle`, `dock`, `radar`, `soma`, each re-floored to
   whole rows.
6. Any rect with `w <= 0` or `h <= 0` is zeroed whole, so a caller tests one field.

**Markup primitives.** These replace `pad` / `rule` / `bar` and are the only way producers build
markup. Every one returns a complete, escaped RML fragment.

```cpp
/// <div class="{classes}"[ id="{id}"]>{inner}</div>. `id` empty → no attribute.
auto row( std::string_view classes, std::string_view id, std::string_view inner ) -> std::string;

/// <div class="nc-rule"></div>
auto rule_div() -> std::string;

/// <div class="hud-row hud-subhead"><span>{title}</span></div>, preceded by rule_div().
auto subhead( std::string_view title ) -> std::string;

struct pip_options { int cur = 0; int max = 0; int count = 12; };
/// `count` <div class="nc-pip"> elements, the first round(count*cur/max) carrying " on".
/// max <= 0 → every pip off. Clamped to [0, count].
auto pips( const pip_options &o ) -> std::string;

struct fact_options {
    std::string label;              ///< emitted as-is; RCSS upper-cases it
    std::string value;
    std::string sub;                ///< omitted entirely when empty
    ink value_ink = ink::datum;
};
/// <div class="nc-fact"><div class="nc-fact-label">…</div>
///   <div class="nc-fact-value hud-iN">…</div>[<div class="nc-fact-sub">…</div>]</div>
auto fact( const fact_options &o ) -> std::string;

struct legend_options {
    std::string label;
    std::string value;
    ink label_ink = ink::label;
    ink value_ink = ink::datum;
    bool alarm = false;                   ///< value takes "hud-alarm" instead of a rung class
    std::string label_class = "hud-lab";  ///< extra class on the label span; "" for none
    std::string value_class;              ///< extra class on the value span; "" for none
};
/// <div class="nc-legend-item">
///   <span class="nc-legend-label[ {label_class}] hud-iN">label</span>
///   <span class="nc-legend-sep">::</span>
///   <span class="[{value_class} ]hud-iN|hud-alarm">value</span></div>
/// An empty `label` omits both the label span and the separator, which is how a bare value
/// (the season, the clock, the place name) joins a STATUS group without inventing a caption.
/// The default `label_class` is the STATUS strip's: `.hud-lab` shrinks a caption to the
/// `.nc-fact-label` recipe (11dp, 2dp tracking, caps). The KEYS strip passes
/// `.label_class = "hud-key"` and `.value_class = "nc-legend-label"` instead, because there the
/// label position holds a key glyph and the value position holds the action name — content, not
/// a caption, and it must not shrink.
auto legend_item( const legend_options &o ) -> std::string;

/// <span class="nc-chip"><span class="nc-chip-label hud-iN">text</span></span>
auto chip( std::string_view text, ink i ) -> std::string;

/// <div class="nc-tally-row"><span class="nc-tally-name hud-iN">…</span>
///   <span class="nc-tally-val hud-iN">…</span></div>. `val` empty → span omitted.
auto tally_row( std::string_view name, std::string_view val, ink name_ink, ink val_ink )
    -> std::string;
```

Every caller-supplied string passes through `rml_escape` (`src/rml_util.h`) inside these helpers.
There is no `no_break` / U+00A0 conversion and there must not be: under flex layout, gaps come
from `margin`/`padding`, never from padding spaces, so the parse-time whitespace trim that forced
`hud_phosphor::pad` to use NO-BREAK SPACE can no longer corrupt a row. A producer that reaches for
a literal alignment space is a bug — `newcharprofession.rcss:201` records the same rule for the
creator.

### Step 2 — the producers: `src/hud_runic_panels.{h,cpp}`, `src/hud_runic_strips.{h,cpp}`

New TUs, nothing calls them yet. Signatures mirror today's but take the new layout:

```cpp
// hud_runic_panels.h
auto hud_soma( avatar &u, const hud_runic::layout &l ) -> std::string;
auto hud_dock( avatar &u, const hud_runic::layout &l ) -> std::string;

// hud_runic_strips.h
auto hud_status_row1( avatar &u, const hud_runic::layout &l ) -> std::string;
auto hud_status_row2( avatar &u, const hud_runic::layout &l ) -> std::string;
auto hud_log_rows( const std::vector<Messages::rich_message> &msgs,
                   const hud_runic::layout &l ) -> std::string;
auto hud_keys( avatar &u, const hud_runic::layout &l ) -> std::string;
auto hud_veh_panel( avatar &u, const hud_runic::layout &l ) -> std::string;
```

`hud_status_rule`, `hud_keys_rule` and `hud_radar_frame` have no successor: those three rows were
frame strokes, and frames are now CSS borders and `.nc-rule` divs in the document.

**Carry every field across.** The data each producer reads is unchanged; only its markup and its
casing change. Port the threshold helpers (`stat_rung`, `speed_rung`, `temp_rung`, `reserve_rung`,
`pool_ink`, `morale_ink`, `limb_notes`, `effect_tiers`, `split_site`, `stowed_sidearm`,
`brightest_lamp`, `light_text`, `warmth_text`, `move_mode_word`, `need_field`, `coop_text`,
`throw_text`, `wind_text`, `heading_word`, `profession_text`) across unchanged. `capped` goes
with the grid — there is no cell budget left to cap against.
Delete the cell-grid scaffolding they sat on: `row`, `seg`, `fit`, `justify`, `render_runs`,
`compose`, `no_break`, `repeat`, `strip_rml`, `wrap_rows`, `clamp_rows`, `soma_grid`,
`soma_grid_for`, `roster_grid`, `roster_grid_for`, `gauge_row`, `note_row`, `roster_row`,
`field_row`, `morale_scale`, `soma_sub_rule`, `dock_sub_rule`, `frame_of`, `put_segments`,
`segment`, `stat_field`, and every `glyph_*` constant.

**Overflow.** There is no row-budget arithmetic in a producer, and adding any is a mistake: every
panel's `.hud-body` is `overflow-y: auto`, so content taller than its region scrolls instead of
painting across the region below. Two exceptions, both because the region is *sized to* its
content rather than the other way round:

- **LOG** — `layout_for` sizes `l.log` from `log_lines`, and `panels.cpp` already trims `msgs`
  before the call. Keep that trim; change its bound to
  `floor( ( l.log.h - head_h - chrome_h ) / row_h )`.
- **SOMA's effect roster** — capped at the eight highest-tier entries, a literal in the producer.
  An unbounded roster is the one case where scrolling is the wrong answer: it pushes the pools the
  player reads every turn out of view behind a scrollbar they have no way to operate.

#### SOMA (`hud_soma`) — `l.soma`

```
per body part, one row; class "hud-row" plus " crit" when hud_runic::is_critical:
  <span class="hud-cell-name hud-i3|hud-i4">Torso</span>            (label rung when cur==max)
  <div class="hud-cell-meter">{ pips( cur, max, 12 ) }</div>
  <span class="hud-cell-val hud-iN">124/130</span>
    broken     → value "SPLINT" at datum when splinted, "BROKE" at dead otherwise
    otherwise  → "{cur}/{max}" at label when cur==max, datum otherwise
if limb_notes() is non-empty, one further row, class "hud-row hud-chiprow", holding one
  chip( note, ink::datum ) per note — chip( note, ink::peak ) when the limb is critical
subhead( _( "POOLS" ) )
  STAM   pips( stamina, stamina_max, 12 )  value "{cur}/{max}"  rung pool_ink
  FOCUS  pips( focus, 100, 12 )            value "{focus}"      rung pool_ink
  MORALE pips( |level|, 100, 12 )          value "{:+}"         rung morale_ink
subhead( _( "EFFECTS" ) )
  <div class="nc-tally">  one tally_row( name, site, ink::datum, ink::label ) per entry,
                          name rung raised to peak at tier 3  </div>
  empty roster → tally_row( _( "None" ), "", ink::dead, ink::dead )
```

The morale meter loses its bidirectional scale (`morale_scale` is deleted); the sign is carried by
the `{:+}` value and the rung, which is what `morale_ink` already encodes.

#### DOCK (`hud_dock`) — `l.dock`

```
fact{ .label = _( "MISSION" ),
      .value = has_mission ? "{dist} {NE}" : _( "None" ),
      .value_ink = has_mission ? ink::datum : ink::dead }
rule_div()
subhead( _( "TARGET" ) )
  target present:
    fact{ .label = {}, .value = t->disp_name(),
          .sub = string_format( _( "%d tiles %s" ), range, heading ),
          .value_ink = ink::peak }
    row "hud-row": <span class="hud-cell-name hud-i3">HP</span>
                   <div class="hud-cell-meter">{ pips( hp, hp_max, 12 ) }</div>
                   <span class="hud-cell-val hud-i4">{pct}%</span>
    fact{ .label = _( "STATUS" ), .value = attitude word }
  no target:
    fact{ .label = {}, .value = _( "No target" ), .value_ink = ink::dead }
rule_div()
subhead( _( "ARMS" ) )
  fact{ .label = _( "WIELD" ),
        .value = armed ? primary_weapon().tname() : _( "fists" ),
        .sub   = armed ? std::format( "{} bash · {} cut · {:+} hit", … ) : "",
        .value_ink = armed ? ink::peak : ink::dead }
  stowed sidearm present:
    fact{ .label = _( "ALT" ), .value = sidearm->tname(),
          .sub = std::format( "{}/{} {}", loaded, capacity, ammo name or _( "empty" ) ),
          .value_ink = loaded > 0 ? ink::datum : ink::dead }
```

#### STATUS rows (`hud_status_row1`, `hud_status_row2`) — `l.status`

Each returns the inner markup of one `.hud-meta` row: exactly three group divs.

```
<div class="hud-meta-group">…left…</div>
<div class="hud-meta-group hud-meta-mid">…middle…</div>
<div class="hud-meta-group">…right…</div>
```

Every field inside a group is a `legend_item`. Row 1:

- left: `legend_item{ .label = {}, .value = u.get_name(), .value_ink = ink::peak }` then
  `legend_item{ .label = {}, .value = profession_text( u ), .value_ink = ink::label }`
- middle, in this order, each omitted when its source is absent exactly as today. Only CO-OP,
  DAY, WX, LUX and LAMP carry a `.label`; season, clock, place, depth and temperature are bare
  values with an empty `.label`, which is how they read today and why `legend_item` drops the
  separator on an empty label:
  CO-OP (peak), DAY, season, clock (peak), place, depth `-NZ` (label rung), WX, temperature
  (`temp_rung`), wind, LUX (`light_text` rung), LAMP (`reserve_rung`)
- right: `legend_item{ .label = _( "SAFE MODE" ), .value = _( "ON" ), .value_ink = ink::peak }`
  when on; when off, the same with `.value = _( "OFF" ), .alarm = true`

Row 2:

- left: STR / DEX / INT / PER, each a `legend_item` with `.value_ink = stat_rung( base, value )`
- middle: MOVE, SPD (`speed_rung`), NOISE, ENC TORSO, ARMS, HUNGER, THIRST, FATIGUE, WARMTH,
  plus `legend_item{ .value = _( "OVERBURDENED" ), .alarm = true }` when over capacity and
  THROW when `throw_text` is non-empty
- right: `legend_item{ .label = _( "HOSTILE" ),
  .value = visible > 0 ? std::format( "{} vis · {} track", … ) : _( "None" ),
  .value_ink = visible > 0 ? ink::peak : ink::label }`

The middle group is `flex: 1; min-width: 0; flex-wrap: wrap; overflow: hidden`, so a narrow
viewport drops the trailing fields by clipping rather than by the old width arithmetic. All of
`capped()`, `free_cells`, `need_cells`, `stat_cells` and the `labelled` fallback are deleted with
the grid — there is no cell budget left to compute.

#### LOG (`hud_log_rows`) — `l.log`

```
recency ramp, newest first: { ink::peak, ink::datum, ink::label, ink::rule }
per message, oldest first, trimmed by the caller to the region's row budget:
  row( newest ? "hud-row hud-log-entry hud-log-fresh" : "hud-row hud-log-entry",
       std::format( "log-{}", m.seq ),
       <span class="hud-log-time hud-iN">{m.time}</span>
       <span class="hud-log-glyph hud-iN">{+ ! ^ ·}</span>
       <span class="hud-log-text hud-iN">{remove_color_tags( m.text )}[ █]</span> )
```

Glyph tier is severity (`m_good` → `+`, `m_bad` → `!`, `m_warning` → `^`, else `·`); rung is
recency; the two never contend. The `█` cursor block is appended to the newest row's text only.
The `>` marker is deleted — `.hud-log-fresh`'s gold left edge replaces it, which is the creator's
cursor device. `hud-log-entry` and the `log-<seq>` id are load-bearing: `hud_anim` feeds and
forgets rows by that exact id.

#### KEYS (`hud_keys`) — `l.keys`

Returns the inner markup of the strip's `.nc-legend` container: one `.nc-legend-item` per slot,
built from `key_slots( u )` and `bound_key( ctxt, slot.act )` exactly as today.

```
<div class="nc-legend-item">
  <span class="nc-legend-label hud-key hud-i5|hud-i1">{key or "-" or ""}</span>
  <span class="nc-legend-sep">::</span>
  <span class="nc-legend-label hud-i4|hud-i1">{slot.label}</span>
  [<span class="hud-keyreason hud-i1">{slot.reason}</span>]
</div>
```

That is exactly `legend_item( { .label = key glyph, .value = slot.label, .label_ink = …,
.value_ink = …, .label_class = "hud-key", .value_class = "nc-legend-label" } )`; the producer
appends the reason span itself when `slot.reason` is non-empty, because the helper has no third
slot and inventing one for a single caller would be worse than the concatenation.

A slot is *live* when it has a binding and `slot.available`; live spends `hud-i5` on the key and
`hud-i4` on the label, dead spends `hud-i1` on both. The key glyph is the key itself when live,
`-` when bound but unusable right now, and empty when the action has no binding at all — nothing
else can reach that cell, because `bound_key` already catches `get_desc`'s three sentence-shaped
sentinels. The `[n]` index brackets are deleted: the `::` separator is the creator's own
key/meaning device and the index carried no information the key glyph did not. All slots are
emitted; `.nc-legend`'s `flex-wrap: wrap` handles a narrow viewport, so `slot_cells`,
`label_cells`, `keys_lead_cells`, `keys_tail_cells`, `keys_fixed_cells` and the early `break` are
all deleted.

#### VEHICLE (`hud_veh_panel`) — `l.vehicle`

Returns empty unless `u.controlling_vehicle` and a vehicle is under the avatar.

```
fact{ .label = {}, .value = veh->name, .sub = heading_word( veh->face.dir8() ),
      .value_ink = ink::datum }
fact{ .label = _( "SPD" ),
      .value = std::format( "{}/{} {}", disp( velocity ), disp( max_velocity ), units ),
      .sub   = cruise_on && cruise_velocity != 0 ? std::format( "cruise {}", … ) : "",
      .value_ink = speed_rung_veh }
row "hud-row hud-chiprow": chip( ENGINE ON|OFF, datum|rule ) plus one chip per live flag —
      UNSAFE (peak), ALARM (peak), CRUISE, AUTO, CAM (all datum)
rule_div()
per fuel with capacity > 0, one "hud-row":
  <span class="hud-cell-name hud-i3">{item::nname}</span>
  <div class="hud-cell-meter">{ pips( amount, capacity, 12 ) }</div>
  <span class="hud-cell-val hud-iN">{pct}%</span>   rung reserve_rung( pct )
```

### Step 3 — the cutover: document, stylesheet, theme, chassis, radar

This is one step because the tree is only *correct* when all five land together. Do them in the
order below and build once at the end.

#### 3a. `data/gui/theme.json`

Apply the token table from **Palette and typography contract** above.

#### 3b. `data/gui/sidebar_hud.rml` — full rewrite

```xml
<rml>
<head>
    <link type="text/rcss" href="theme.rcss"/>
    <link type="text/rcss" href="newchar_common.rcss"/>
    <link type="text/rcss" href="sidebar_hud.rcss"/>
</head>
<body data-model="sidebar_hud">
    <div id="hud-status" class="hud-strip">
        <div class="hud-meta" data-rml="status_row1_rml"></div>
        <div class="nc-rule"></div>
        <div class="hud-meta" data-rml="status_row2_rml"></div>
    </div>

    <div id="hud-soma" class="hud-panel">
        <div class="nc-colhead">
            <div class="nc-col-sigil hud-sigil-soma"></div>
            <span class="nc-colname">Soma</span>
        </div>
        <div class="hud-body" data-rml="soma_rml"></div>
    </div>

    <div id="hud-radar" class="hud-panel hud-panel-clear">
        <div class="nc-colhead">
            <div class="nc-col-sigil hud-sigil-radar"></div>
            <span class="nc-colname">Overmap</span>
        </div>
    </div>

    <div id="hud-dock" class="hud-panel">
        <div class="nc-colhead">
            <div class="nc-col-sigil hud-sigil-dock"></div>
            <span class="nc-colname">Dock</span>
        </div>
        <div class="hud-body" data-rml="dock_rml"></div>
    </div>

    <div id="hud-log" class="hud-panel">
        <div class="nc-colhead">
            <div class="nc-col-sigil hud-sigil-log"></div>
            <span class="nc-colname">Message log</span>
        </div>
        <div class="hud-body" data-rml="log_rml"></div>
    </div>

    <div id="hud-keys" class="hud-strip">
        <div class="nc-legend" data-rml="keys_rml"></div>
    </div>

    <div id="hud-vehicle" class="hud-panel">
        <div class="nc-colhead">
            <div class="nc-col-sigil hud-sigil-veh"></div>
            <span class="nc-colname">Vehicle</span>
        </div>
        <div class="hud-body" data-rml="veh_rml"></div>
    </div>

    <div id="hud-crt-scan"></div>
    <div id="hud-crt-curve"></div>
    <div id="hud-vignette"></div>
</body>
</rml>
```

Panel titles are authored in sentence case and upper-cased by `.nc-colname`'s `text-transform`,
so no locale ships a second copy. Tree order is paint order: the three overlays come last.

#### 3c. `data/gui/sidebar_hud.rcss` — full rewrite

Structure, in this order (later rules in this sheet beat `newchar_common.rcss` at equal
specificity, which is how the pip-size and colhead-colour overrides work):

```
body                      Source Code Pro, font-size 16dp, letter-spacing normal,
                          font-weight normal, color {{fg}}, background transparent,
                          width/height 100%
#hud-status,#hud-soma,#hud-radar,#hud-dock,#hud-log,#hud-keys,#hud-vehicle
                          position absolute; display flex; flex-direction column;
                          box-sizing border-box; overflow hidden
.hud-strip                background-color {{bg-hard}}; padding 4dp 8dp;
                          decorator repeating-linear-gradient(180deg,
                            {{hud-veil-0}} 0dp, {{hud-veil-0}} 2dp,
                            {{hud-scan}} 2dp, {{hud-scan}} 3dp)
.hud-panel                border 1dp {{bg4}}; padding 0;
                          backdrop-filter blur(2.5dp) saturate(0.72) brightness(0.94);
                          decorator: the same scanline pass listed FIRST, then
                            linear-gradient(180deg, {{hud-veil}} 0%, {{hud-veil}} 78%,
                                            {{hud-veil-0}} 100%)
#hud-log                  same two decorators, the veil gradient at 0deg (dissolves upward:
                          the newest line is on its last row)
.hud-panel-clear          background-color transparent; decorator none
                          (#hud-radar only — a veil over the GPU dot field would multiply the
                          two dimmest categories into the ground)
.hud-panel-clear .nc-colhead
                          background-color {{hud-veil}}
                          (the radar panel has no ground of its own, so its head would otherwise
                          sit unreadable directly on live terrain)
.nc-colhead               (inherited) + padding 3dp 6dp 3dp 6dp; margin-bottom 0
.nc-colhead .nc-colname   font-size 12dp; color {{yellow}}
.nc-col-sigil.hud-sigil-* decorator image( ?proc:runic-icon:14:<seed>:{{accent-rgb}}
                                           none contain ) border-box
                          seeds: soma 7101, radar 7102, dock 7103, log 7104, veh 7106
.hud-body                 flex 1; min-height 0; display flex; flex-direction column;
                          overflow-y auto; padding 2dp 6dp 4dp 6dp
.hud-i0 … .hud-i5         display inline; color, in order: {{bg-hard}} {{bg2}} {{bg4}}
                                                            {{fg4}} {{fg}} {{fg0}}
                          (`display: inline` is stated on every one and is not redundant: the
                          shipping target-HP bar was a permanently empty trough because a span
                          with a percentage height had no `display` and RmlUi's inline default
                          collapsed the box it was a percentage of)
.hud-row                  flex 0 0 auto; display flex; flex-direction row;
                          align-items center; height 20dp; line-height 20dp;
                          overflow hidden; background-color transparent;
                          border-left-width 3dp; border-left-color transparent
.hud-row.crit             background-color {{bg-hard}}; border-left-color {{yellow}};
                          font-weight bold
.hud-subhead              font-size 11dp; letter-spacing 2dp; text-transform uppercase;
                          color {{fg4}}
.hud-chiprow              height 18dp; line-height 18dp; flex-wrap nowrap
.hud-cell-name            flex 0 0 auto; width 74dp; white-space nowrap; overflow hidden
.hud-cell-meter           flex 1; min-width 0; display flex; flex-direction row;
                          align-items flex-end; height 9dp; margin 0 8dp
.hud-cell-meter .nc-pip     height 4dp; margin-right 1dp
.hud-cell-meter .nc-pip.on  height 9dp
.hud-chiprow .nc-chip     margin-bottom 0
                          (.nc-chip's 4dp bottom margin would overflow an 18dp fixed-height row)
.hud-body .nc-tally       flex 0 0 auto; overflow-y visible
                          (.nc-tally scrolls on its own in the creator; inside .hud-body, which
                          already scrolls, a nested scroller traps the roster behind a scrollbar
                          the HUD gives the player no way to drive)
.hud-lab                  font-size 11dp; letter-spacing 2dp; text-transform uppercase
                          (the .nc-fact-label recipe without its colour, so the rung class the
                          producer puts on the same element still wins)
.hud-cell-val             flex 0 0 auto; width 74dp; text-align right; white-space nowrap;
                          overflow hidden
.hud-meta                 flex 0 0 auto; display flex; flex-direction row;
                          align-items center; justify-content space-between; height 20dp
.hud-meta-group           flex 0 0 auto; display flex; flex-direction row; align-items center
.hud-meta-mid             flex 1; min-width 0; justify-content center; flex-wrap wrap;
                          overflow hidden
.hud-alarm                display inline; background-color {{yellow}}; color {{bg-hard}};
                          font-weight bold; padding 0 4dp
.hud-log-entry            position relative
.hud-log-fresh            border-left-color {{yellow}}
.hud-log-time             flex 0 0 auto; width 54dp
.hud-log-glyph            flex 0 0 auto; width 16dp; text-align center
.hud-log-text             flex 1; min-width 0; white-space nowrap; overflow hidden
.hud-key                  flex 0 0 auto; min-width 22dp; font-weight bold
.hud-keyreason            flex 0 0 auto; margin-left 6dp; font-size 11dp
#hud-keys .nc-legend      flex 1; min-height 0; padding 0; align-items center
                          (.nc-legend brings 6dp/8dp/2dp padding and a 1dp {{bg4}} top border
                          from the creator; the border stays — it is the strip's edge against the
                          map — and the padding goes, because .hud-strip already pads)
#hud-keys .nc-legend-item margin-bottom 0
#hud-vehicle              display none            (must come AFTER the region block)
#hud-crt-scan,#hud-crt-curve  position absolute; display block; left/top 0;
                          width/height 100%; pointer-events none
#hud-crt-scan             z-index 10; decorator repeating-linear-gradient(180deg,
                            {{hud-veil-0}} 0dp, {{hud-veil-0}} 2dp,
                            {{hud-scan-glass}} 2dp, {{hud-scan-glass}} 3dp)
#hud-crt-curve            z-index 11; decorator radial-gradient(farthest-corner,
                            {{hud-veil-0}} 42%, {{hud-vig}} 100%)
#hud-vignette             position absolute; display block; left/top 0; width/height 100%;
                          pointer-events none; z-index 20; opacity 0;
                          decorator radial-gradient(farthest-corner,
                            {{hud-veil-0}}, {{yellow}})
.nc-colhead .nc-colname   font-effect glow( 2dp {{yellow-glow}} )
                          (the ONLY glow in the HUD, and only on the gold panel heads. It is
                          `theme.rcss`'s `.title` policy verbatim, including its reason: a glow
                          under dense body text muddies it, and `{{yellow-glow}}` is bright
                          gruvbox yellow — putting it on every row would tint the whole neutral
                          ladder gold and undo the palette this change exists to adopt)
```

Facts to carry into the new file's comments, all verified against this backend and still true:

- `font-effect: glow()` is the only text glow that exists — `text-shadow` is not a property in
  RCSS 6.2. It is applied in the font engine, so it works where `backdrop-filter` does not.
- `backdrop-filter` is declared and **inert**: `lighting::rmlui_render_interface` does not override
  `PushLayer` / `PopLayer` / `CompositeLayers` / `CompileFilter`. The alpha translucency is real;
  the blur and desaturation are not. Kept because it becomes live for free when the layer API lands.
- `mask-image` is not inert but **destructive** on this backend — its gradient quads land on the
  swapchain over the panel's own text. The panel-edge dissolve is a `linear-gradient` on the
  ground for that reason, and because fading the ground leaves every glyph at full luminance.
- Decorators paint over `background-color` and under the element's content, and
  `ElementEffects.cpp:195-200` walks the decorator list in reverse — so the scanline pass is
  listed **first** to sit on top of the veil.
- The panel-edge fades are vertical only: `soma` / `dock` / `vehicle` mirror with
  `SIDEBAR_POSITION`, so a fade hardcoded to a left or right edge points into the bezel the moment
  the player flips the sidebar.
- RmlUi elements default to `display: inline`; this game does not link RmlUi's sample `rml.rcss`.
  Every block or flex box states its own `display`.
- There is no `transition` anywhere in this file and there must not be. An RCSS transition wins on
  the property it names, and `hud_anim` writes `opacity` and `top` inline through Penner curves —
  the shipping HUD's `transition: opacity 0.8s` silently replaced every one of them.
  `.nc-pip`'s own `transition` on `background-color`/`height` comes from `newchar_common.rcss`,
  touches no channel `hud_anim` writes, and is wanted.

#### 3d. `src/panels.cpp` — chassis

- Swap includes: `hud_phosphor{,_panels,_strips}.h` → `hud_runic{,_panels,_strips}.h`.
- `hud_rml_model`: delete `status_rule_rml`, `radar_rml`, `keys_rule_rml`; keep the other seven.
  Delete their three `c.Bind(...)` calls in `sidebar_hud_open` and their three assignments in
  `sidebar_hud_sync`.
- Delete `hud_metrics_now()`. Replace `hud_layout_now( log_lines, show_vehicle )` with a call to
  `hud_runic::layout_for( { .ctx_w_dp = dims.x / ratio, .ctx_h_dp = dims.y / ratio,
  .sidebar_right = …, .log_lines = …, .show_vehicle = … } )`, keeping the existing
  `rmlui_layer::ready()` / context / `hud_dp_ratio()` guards.
- `hud_layout_same`: compare `ctx_w`, `ctx_h` and the seven rects field-by-field for exact
  equality. Keep the write-only-on-change caching in `sidebar_hud_apply_rect` — it exists because
  `Element::SetProperty` dirties unconditionally and this runs every frame.
- `sidebar_hud_apply_rect`: `place()` now takes a `hud_runic::rect` directly (no `to_dp`).
  **Delete the `font-size` and `line-height` writes and the `g_hud_font_size` /
  `g_hud_line_height` / `g_hud_type_valid` statics** — the type scale is fixed in RCSS now.
- `terminal_rows_for`: change the signature to `( float strip_h_dp ) -> int`, returning
  `ceil( strip_h_dp * hud_dp_ratio() / fontheight )` with the existing `fontheight <= 0` guard.
  Rounding **up** is load-bearing: the carve must cover the strip, never merely approach it.
  `sidebar_hud_top_rows()` passes the status rect's height, `sidebar_hud_bottom_rows()` the keys'.
- The log trim before `hud_log_rows`, the `hud_anim::feed`/`forget` seq bookkeeping, the vehicle
  `display` toggle, `hud_shake`, and `sidebar_hud_anim_tick` are unchanged except for the layout
  type. The log's row budget is now
  `floor( ( l.log.h - hud_runic::head_h - hud_runic::chrome_h ) / hud_runic::row_h )`.

#### 3e. `src/hud_radar.{h,cpp}`

- Include and use `hud_runic` instead of `hud_phosphor`; `dot_style::rung` becomes
  `hud_runic::ink` and the `world_table` entries keep their existing rungs verbatim.
- Replace the guard `l.radar.rows <= 0 || l.radar.cols <= 1` with `l.radar.w <= 0 || l.radar.h <= 0`.
- Replace the `to_dp` + one-cell-border inset with the panel's real chrome: the dot field is
  `l.radar` inset by `1 dp` on every side (the CSS border) and by a further
  `hud_runic::head_h` at the top (the `.nc-colhead`). Delete `border_on_left` and the
  `l.m.cell_w` arithmetic — the inset no longer depends on which side the sidebar is on.
- The ground fill stays `hud_runic::rgba( hud_runic::ink::ground )` at alpha 0.92. It must stay:
  `#hud-radar` carries `.hud-panel-clear`, so the dots have no other controlled background.

#### 3f. Verify the cutover builds and runs

Build target list and commands are in **Verification**.

### Step 4 — delete the phosphor module

Depends on step 3. Delete outright, with no shim and no alias:

- `src/hud_phosphor.h`, `src/hud_phosphor.cpp`
- `src/hud_phosphor_panels.h`, `src/hud_phosphor_panels.cpp`
- `src/hud_phosphor_strips.h`, `src/hud_phosphor_strips.cpp`
- `tests/hud_phosphor_test.cpp`

`grep -rn 'hud_phosphor' src tests data` must come back empty afterwards. `src/CMakeLists.txt`
needs no edit: the `.cpp` glob carries `CONFIGURE_DEPENDS` and picks up the new files; the header
glob is plain but headers are tracked through the compiler's `-MMD` output.

### Step 5 — `tests/hud_runic_test.cpp`

Depends on step 1 (it can be written then, but only compiles into a green suite once step 4 has
removed the duplicate). Replaces `tests/hud_phosphor_test.cpp`, tag `[hud_runic]`.

Port unchanged in substance:

- **`runic_crit_predicate_fires_on_the_shipped_bug`** — the 8/30 bleeding+bitten case, the broken
  suppression, and broken+bleeding still critical. Byte-for-byte port of the existing case.
- **`runic_layout_regions_never_overlap`** — sweep a spread of context sizes
  (`{640,480}`, `{1280,720}`, `{1920,1080}`, `{2560,1440}`, `{800,600}`) × `sidebar_right`
  ∈ {true,false} × `log_lines` ∈ {0,3,6,12} × `show_vehicle` ∈ {true,false}. For every
  combination assert (a) no two non-empty rects intersect, (b) every rect lies inside
  `[0,ctx_w] × [0,ctx_h]`, (c) `status.y == 0` and `keys.y + keys.h == ctx_h` exactly — the
  property the dp anchor buys that the cell grid could only round toward.
- **`runic_ink_ladder_never_collapses`** — replaces the one-hue test. Assert `hex()` over
  `ground` → `peak` is strictly increasing in relative luma
  (`0.2126R + 0.7152G + 0.0722B` on the 8-bit channels), that every adjacent gap from `ground`
  through `datum` is ≥ 35, and that `peak > datum`. The `peak`/`datum` gap is deliberately
  **not** asserted at 35 — it is 20.7 and that is the creator's own `fg`/`fg0` step; the test
  carries a comment saying so, so nobody "fixes" the ladder to satisfy a threshold the design
  does not claim. There is no gold rung to exclude: `ink` has exactly six members.
- **`runic_tint_emits_theme_classes_not_inline_colour`** — `tint()` output contains
  `class="hud-i4"` and contains no `style=` and no `#`.

New, covering the primitives step 2's producers are built from:

- **`runic_pips_quantise_and_never_exceed_count`** — `pips({.cur,.max,.count=12})` always emits
  exactly 12 `<div class="nc-pip` occurrences; the ` on` count is `round(12*cur/max)` clamped to
  `[0,12]`; `max <= 0` yields zero on; `cur > max` yields 12 on.
- **`runic_fact_omits_an_empty_sub`** — `fact()` with an empty `.sub` emits no `nc-fact-sub`
  element at all (an empty one would take vertical space in a fixed-height panel).
- **`runic_helpers_escape_caller_text`** — a value of `a<b&c"` comes back with `&lt;`, `&amp;`,
  `&quot;` and no raw `<` outside a tag, through `fact`, `legend_item`, `chip` and `tally_row`.
- **`runic_panel_h_is_whole_rows`** — `panel_h(n) - head_h - chrome_h == n * row_h` for
  `n` in 0..30, which is the invariant the log's message trim in `panels.cpp` divides by.

### Step 6 — remove the dead teal HUD colour register

Independent of steps 1–5; may land before or after. The `"hud_colors"` layer exists to pull game
colours into "the HUD's cool teal register", which after this change does not exist. It is already
dead apart from one call site — `hud_color_names`, `get_hud_rgba`, `set_hud_rgba` and
`cata_hud_text_to_rml` have zero callers, and the F4 Theme tab
(`sdl_lighting_devui.cpp:907-915`) enumerates only `rcss_names()` and `game_color_names()`.

- `src/hud_anim.cpp:196` — `hud_color_to_hex( transform.blend_color )` → `nc_color_to_hex(...)`.
- `src/rml_util.{h,cpp}` — delete `hud_color_to_hex`, `cata_hud_text_to_rml` and the
  `g_hud_hex_cache` static; drop the cache from whatever `reset_hex_caches` clears.
- `src/ui_theme.{h,cpp}` — delete `hud_color_hex`, `hud_color_names`, `get_hud_rgba`,
  `set_hud_rgba`, the `g_hud_by_name` / `g_hud_by_int` / `g_hud_built` / `g_hud_order` statics,
  the `"hud_colors"` read block in `load()` (`:139-146`), their clears in the reset block
  (`:113-117`), and the `"hud_colors"` write block in `save()` (`:292-297`). Update the header
  comment `save()` writes so it no longer describes a layer that is gone.
- `data/gui/theme.json` — the `"hud_colors"` object goes (already listed in step 3a).

`grep -rn 'hud_colors\|hud_color_to_hex\|cata_hud_text_to_rml\|hud_color_hex\|get_hud_rgba\|set_hud_rgba\|hud_color_names' src data` must come back empty.

## Critical files & anchors

| file | anchor | why |
|---|---|---|
| `data/gui/newchar_common.rcss` | `.nc-rule` `:848`, `.nc-pip` `:804`, `.nc-fact-*` `:612-640`, `.nc-legend-*` `:711-747`, `.nc-tally-*` `:1067-1108`, `.nc-colhead` `:906-938` | the device rules the HUD inherits verbatim; read before writing any HUD markup so the class names and their required child structure match |
| `src/hud_phosphor_strips.cpp` | `hud_status_row1` `:777`, `hud_status_row2` `:882`, `hud_log_rows` `:1015`, `hud_keys` `:1123`, `hud_veh_panel` `:1195` | the field inventory being ported; every threshold helper and every conditional field lives above these in the same anonymous namespace |
| `src/hud_phosphor_panels.cpp` | `hud_soma` `:664`, `hud_dock` `:814` | same, for the two panels; also the source of `limb_notes`, `effect_tiers`, `split_site`, `stowed_sidearm` |
| `src/panels.cpp` | `sidebar_hud_open` `:810`, `sidebar_hud_apply_rect` `:871`, `sidebar_hud_sync` `:947`, `terminal_rows_for` `:694`, `sidebar_hud_top_rows` `:1126` | the chassis; the write-only-on-change property caching and the `hud_anim` seq bookkeeping must survive the layout-type change intact |
| `src/lighting/rmlui_proc_texture.cpp` | `runic-icon` `:783` | the `?proc:runic-icon:<size>:<seed>:<rrggbb>` contract the six panel sigils use; confirms the URL takes a bare six-digit hex with no `#` |

## Verification

This machine is **darwin/arm64**. `tools/visual_verify/vv.py` is Windows-only (`ctypes/user32`,
`vv.py:20`) and cannot be used here; the visual check below drives the macOS build with the
`computer` tool instead.

**Build.** Never synchronously and never with a short cap — a killed ninja run corrupts
`.ninja_deps` and forces a near-full rebuild:

```sh
cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles
```

Start it as a background job with a ≥1200 s timeout and poll to completion.

**Unit.** From the repo root. The `osx-arm-slim` preset links BOTH build products to the repo
root; `out/build/osx-arm-slim/tests/cata_test-tiles` and `out/build/osx-arm-slim/src/
cataclysm-bn-tiles` are months-stale leftovers. Running the stale test binary reports
`No test cases matched '[hud_runic]'` even with a freshly compiled object, which reads exactly
like a filter typo:

```sh
./cata_test-tiles "[hud_runic]"
```

All nine cases pass. `runic_layout_regions_never_overlap` is the check tied to the riskiest step
(1): it is what proves the dp layout kept the disjointness and viewport-coverage guarantees the
cell grid provided, including the exact `keys.y + keys.h == ctx_h` bottom anchor.

**Full suite,** to catch anything the deleted `hud_color_to_hex` / `ui_theme` symbols reached:

```sh
./cata_test-tiles
```

**End-to-end visual, exercising the new behaviour.** Launch the repo-root binary, for the same
reason as above — the one under `out/build/osx-arm-slim/src` is stale and the change will appear
to have no effect:

1. Launch `./cataclysm-bn-tiles` and load any existing save (`~/Library/Application Support/Cataclysm-BN/save/`).
2. With the `computer` tool, screenshot the game window and confirm, in one frame:
   - all six regions are gruvbox-dark cards with gold tracked-caps heads and a 14 dp runic sigil —
     no amber anywhere, no box-drawing glyph (`─ │ ┌ ┐ └ ┘ ├ ┤ ┬ ┴ ┼`) anywhere;
   - SOMA's body-part rows show pip meters, not `▄`/`░` runs;
   - the KEYS strip reads `KEY :: ACTION` with a dim `::`, wrapping rather than truncating;
   - the newest log line carries a gold left edge and a `█` cursor block.
3. Reach a critical limb through the debug menu (`~` → Debug → damage the avatar) rather than by
   waiting on combat. With a limb below a third of its max HP, or bleeding at any HP, `hud_soma`
   emits `class="hud-row crit"` on exactly that row and on no other, and the row renders as a
   gold left edge over a `bg-hard` fill — **not** reverse video, and **not** red.
4. Toggle safe mode off (`!`): the STATUS strip's right field switches to a gold-filled `OFF` chip.
5. Take vehicle controls: `#hud-vehicle` appears under DOCK with `.nc-fact` speed/heading and pip
   fuel gauges, and the DOCK panel above it is not overlapped.
6. Flip `SIDEBAR_POSITION` to `left` in Options and confirm SOMA/RADAR/DOCK/VEHICLE mirror as a
   block, the log stays bottom-left, and no panel edge fade points into the screen bezel.
7. Resize the window small enough that the log has to yield rows, then small enough that the
   columns yield: nothing overlaps and the keys strip stays flush with the bottom edge.
8. `~/Library/Application Support/Cataclysm-BN/config/debug.log` contains no
   `unknown rcss token` warning and no `decorator: ;` / RCSS parse error. An unknown token would
   also light RmlUi's yellow beacon in a screen corner — the screenshot in (2) covers that.

## Assumptions & contingencies

- **Assumption: linking `newchar_common.rcss` into the HUD document is safe.** Every rule in that
  file is class-scoped, so the creator's layout rules cannot reach a HUD element that does not opt
  in by class. *If reality is otherwise* — a bare-element or `body` rule turns up and disturbs the
  HUD — copy only the eight device blocks named in the vocabulary table into `sidebar_hud.rcss`
  under their existing `nc-` names and stop linking the creator sheet. Do not rename the classes:
  the markup contracts above are written against them.
- **Assumption: 16 dp / 20 dp rows keep today's information density.** The current grid resolves to
  font-size 16.667 dp and a 20 dp cell at 1920×1080, so this is within a hair of the shipping
  density. *If the HUD reads too sparse or too dense in step 3f*, change `row_h` and the `body`
  `font-size` together in a 4:5 ratio (18/14.4, 22/17.6) and re-run the layout test — everything
  else is derived and nothing else needs touching.
- **Assumption: `?proc:runic-icon` renders acceptably at 14 px.** The creator requests exactly
  `14` for `.nc-col-sigil` (`newcharacter_ui.cpp:6034`, `:7379`, `:7765`). *If the 14 px rune is
  mush*, request `28` in the URL and leave the element at 14 dp — `contain` downsamples it.
- **Assumption: `font-weight: bold` resolves to a real face.** Only `SourceCodePro-Regular.ttf`
  and `SourceCodePro-Semibold.ttf` are registered (`rmlui_layer.cpp:500`), so `bold` (700) has to
  be matched to Semibold (600) by RmlUi's weight matcher. It is used in exactly three places —
  `.hud-row.crit`, `.hud-alarm`, `.hud-key`. *If the weight step is not visible in step 3f*,
  delete the three `font-weight: bold` declarations and instead raise the crit row's text to the
  `hud-i5` rung from the producer; the fill and the gold edge already carry the state, and a
  synthesised or absent bold is worse than no weight channel at all.
- **Decision, not an assumption: reverse video is gone.** `ink::inverse`, `.ph-inv`, `.ph-ik` and
  `hud_phosphor::invert` are deleted rather than recoloured. Critical state is the creator's cursor
  treatment (gold edge + `bg-hard` fill + bold) and the gold-filled `.hud-alarm`; both keep two
  channels that survive greyscale (fill and weight) so nothing is encoded by hue alone.
- **Scope edge:** `data/gui/throw_radial.rcss` and the legacy teal `hud-*` rcss tokens it consumes
  are deliberately untouched. It is a radial menu, not the sidebar HUD. Do not "finish the job" by
  restyling it here.
