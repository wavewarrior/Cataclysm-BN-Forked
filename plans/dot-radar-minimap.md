# Dot-based tactical minimap ("RADAR") for the Terminal Phosphor HUD

## Context

Add a dot-matrix minimap that renders the **loaded local map** as one coloured dot
per tile — walls and building outlines, doors/windows, furniture, water, vegetation,
stairs, vehicles — plus creature blips split into hostile / neutral / friendly, plus
the player. It occupies the top of the HUD's right-hand (DOCK) column; the text
overmap minichunk that lives there today is **deleted and replaced by it**.

Two settled register decisions, both load-bearing below:

- **Hybrid palette.** The world layer stays inside the HUD's single-hue luminance
  ladder (`hud_phosphor::ink`). Creature blips are the only hue, and they reuse the
  existing in-world creature-outline colours so a blip matches the outline on the
  same creature. Every blip also carries a redundant shape cue, so nothing is
  encoded by hue alone.
- **The radar paints its own opaque ground.** A luminance-only encoding needs a
  background whose luminance is controlled (`src/hud_phosphor.h:43-55`), and the
  dots are drawn *under* the RmlUi document, so a translucent `.ph-veil` over them
  would eat the two dimmest rungs. The radar region is therefore **unveiled** in
  RCSS and the GPU layer fills its interior with `ink::ground` before drawing dots.

Toggle: the already-registered `toggle_pixel_minimap` action / `pixel_minimap_option`.

---

## Research findings (verified this session — do not re-derive)

### The existing minimap plumbing is dead code

- `src/pixel_minimap.{h,cpp}` **do not exist** (`glob src/pixel_minimap*` → nothing).
  `src/lighting/CLAUDE.md:213` still cites `pixel_minimap.cpp:324`; that line is stale.
- The only surviving draw is `cata_tiles::draw_minimap( point dest, const
  tripoint_bub_ms &center, int width, int height )` — `src/cata_tiles.cpp:1657-1722`.
  It draws **overmap (OMT) atlas sprites**, not local tiles.
- Its sole call site is `src/sdl_curses_draw.cpp:449-461`, gated on
  `w == g->w_pixel_minimap && pixel_minimap_option`.
- `g->w_pixel_minimap` is created as a **1×1 dummy** (`src/game_misc.cpp:428`) and is
  only re-assigned by `game::draw_pixel_minimap()` (`src/game_misc.cpp:537-540`),
  which has **zero callers**.
- `game::draw_panels()` (`src/game_misc.cpp:517-535`) is now only RmlUi HUD
  open/sync; `src/panels.cpp:1023` confirms an open HUD doc "suppresses the WHOLE
  curses sidebar".
- ⇒ Nothing renders a minimap in the shipping build today.

### How to draw dots (the live path)

- `lighting::render_state::queue_ui_rect( float x, float y, float w, float h,
  float r, float g, float b, float a )` — decl `src/lighting/render_state.h:81`,
  def `src/lighting/render_state.cpp:261-287`. One unlit solid quad on the shared
  white texture. This is the primitive.
- **Coordinate space is logical window pixels.** `composite_ui_pass_a` and the
  swapchain `blit_layer` both project at `proj_w/proj_h` from `SDL_GetWindowSize`
  (`src/sdl_render_frame.cpp:1337`, `1204-1219`).
- Composite order (`src/sdl_render_frame.cpp:1204-1243`): world → **ui_target** →
  HUD particles → RmlUi. So these dots sit above the world and **below** the HUD doc.
- Routing: `queue_ui_rect` appends to `current_slices_` while a `ui_adaptor` redraw
  callback runs (`render_state.cpp:280`; set/cleared at `src/ui_manager.cpp:383-387`).
  `game::draw()` → `game::draw_panels()` → `sidebar_hud_sync()` all run inside that
  callback, so calls from `sidebar_hud_sync` land in the main slice and are
  re-flushed on non-redraw frames (`render_state.cpp:347-351`).
- `SDL_Render*` is dead in this tree (hidden mirror window, `sdltiles.cpp:100-106`).
  Never use it.

### dp → pixels

`hud_dp_ratio() = rmlui_layer::density_ratio() * rmlui_layer::ui_scale()`
(`src/panels.cpp:697-701`) converts dp → **physical** px, because
`g_density_ratio = physical_w / logical_w` (`src/lighting/rmlui_layer.cpp:1206-1208`).
`queue_ui_rect` wants **logical** px, so the density factor cancels:

> **logical_px = dp × `rmlui_layer::ui_scale()`**

### HUD layout

- `hud_phosphor::layout` (`src/hud_phosphor.h:106-114`), built by `layout_for`
  (`src/hud_phosphor.cpp:421-516`), mirrored in one op by `mirror()` (370-375).
- Constants in `src/hud_phosphor.cpp`: `status_row_count=3` (213),
  `keys_row_count=2` (216), `soma_max_rows=23` (235), `dock_min_cols=35` (240),
  `dock_max_rows=22` (253), `veh_max_rows=8` (260); grid target 192 cols (302);
  cell aspect exactly 1:2 (`grid_of`, 342).
- At 1920×1080 / ui_scale 1: 192 cols × 54 rows, cell 10×20 dp; body rows 3..51.
- `sidebar_hud_apply_rect` (`src/panels.cpp:831-860`) writes each region's
  `left/top/width/height` via `hud_phosphor::to_dp` + `rml::dp`.
- RmlUi cannot draw custom C++ geometry inside a document — no `ElementInstancer`
  registrations exist in `src/`. `filter`/`backdrop-filter`/`mask-image` are
  non-functional in `src/lighting/rmlui_render_interface.h`.

### World data

- `map::ter( const tripoint_bub_ms & ) -> ter_id` (`src/map.h:996`),
  `map::furn(...) -> furn_id` (971), `map::has_furn(...) -> bool` (967).
  `ter_id::obj()` → `const ter_t &`; `map_data_common_t::has_flag( ter_bitflags )`
  (`src/mapdata.h:526`), `::transparent` bool (516), `ter_t::open`/`close`
  (`ter_str_id`, 569-570), `ter_t::is_null()` (597).
- Flags (`src/mapdata.h:273-334`): `TFLAG_WALL`(296), `TFLAG_LIQUID`(285),
  `TFLAG_DEEP_WATER`(297), `TFLAG_SWIMMABLE` (exists; used at
  `src/character_movement.cpp:1045`), `TFLAG_TREE`(325), `TFLAG_SHRUB`(324),
  `TFLAG_NO_FLOOR`(307), `TFLAG_GOES_UP`(306), `TFLAG_GOES_DOWN`(305), `TFLAG_RAMP`(311).
- `level_cache` (`src/map.h:310-439`) via `map::get_cache_ref(z)` (2406):
  `cache_x`/`cache_y` (321-322), `idx(x,y) = x*cache_y + y` (328),
  `inbounds(point_bub_ms)` (334), `outside_cache` (`std::vector<char>`, 383,
  `idx` stride), `visibility_cache` (`std::vector<lit_level>`, 428, `idx` stride),
  `map_memory_seen_cache` (`cata_dynamic_bitset`, 431, **`x + y*cache_x`** — a
  different stride, documented at 430), `vehicle_list` (`std::set<vehicle*>`, 436).
- `map::get_visibility( lit_level, const visibility_variables & ) -> visibility_type`
  (`src/map.h:661`); `map::get_visibility_variables_cache()` (2415);
  `visibility_type::{VIS_HIDDEN,VIS_CLEAR,VIS_LIT,VIS_BOOMER,VIS_DARK,VIS_BOOMER_DARK}`
  (`src/enums.h:91-98`).
- `avatar::should_show_map_memory()` (`src/avatar.h:120`).
- `map::abs_to_bub( const tripoint_abs_ms & ) -> tripoint_bub_ms` (`src/map.h:1891`).
- `vehicle::get_points( bool force_refresh = false ) -> std::set<tripoint_abs_ms> &`
  (`src/vehicle.h:1460`).
- `g->all_monsters()` / `g->all_npcs()` (`src/game.h:545,547`);
  `Creature::attitude_to( const Creature & ) -> Attitude` (`src/creature.h:349`);
  `Attitude::{A_HOSTILE,A_NEUTRAL,A_FRIENDLY,A_ANY}` (`src/enums.h:20-31`);
  `Character::sees( const Creature & )` (`src/character.h:2263-2266`).
  **Renderer convention is `critter.attitude_to( u )`**, not the reverse — see
  `src/cata_tiles_draw_layers.cpp:773,779,788,796`.
- Blip colours already exist and are F4-tunable:
  `g_outline_col_hostile/neutral/friendly/self` (`float[4]`, declared
  `src/sdl_lighting_devui.h:146-149`, defined `src/sdl_lighting_devui.cpp:108-111`
  as red `1.00,0.24,0.24` / amber `1.00,0.86,0.24` / green `0.31,0.90,0.31` /
  cyan `0.31,0.86,1.00`). `outline_color_for( Attitude, bool is_self )` at
  `src/cata_tiles_internal.h:154-175` is the existing consumer.

---

## Approach

Steps are ordered so the tree builds and the existing suite passes after each one.
Steps 1 and 2 are independent of each other; 3 depends on 2; 4-7 depend on 3.

### Step 1 — Delete the dead OMT-minimap plumbing (independent)

Clean cutover: none of this is reachable, and leaving a second, contradictory
minimap implementation behind would be dead code beside the new one.

Delete:

| What | Where |
|---|---|
| `cata_tiles::draw_minimap` body | `src/cata_tiles.cpp:1657-1722` |
| `cata_tiles::minimap_requires_animation` body | `src/cata_tiles.cpp:1724-1729` |
| `cata_tiles::reset_minimap` body | `src/cata_tiles.cpp:1731-1734` |
| those three declarations | `src/cata_tiles.h:833-835` |
| the `w_pixel_minimap` branch | `src/sdl_curses_draw.cpp:449-462` (drop the whole `else if`, keeping the final `else`) |
| `game::draw_pixel_minimap` body | `src/game_misc.cpp:537-540` |
| its declaration + comment | `src/game.h:1082-1083` |
| `catacurses::window w_pixel_minimap;` | `src/game.h:1192` |
| the `newwin(1,1)` init + its comment | `src/game_misc.cpp:427-428` |
| `clear_window_area( w_pixel_minimap );` and its `if` | `src/game_misc.cpp:346-348` (leave `pixel_minimap_option = !pixel_minimap_option;` and `mark_main_ui_adaptor_resize();`) |
| the `tilecontext->reset_minimap()` block | `src/game.cpp:2727-2729` |
| the explosion minimap toggle | `src/explosion.cpp:1113-1119` and `1132-1135` — the "texture pool" hazard was the deleted RTT; the radar is solid quads, and toggling would blink it off during every explosion |

Rewire (not delete): `src/animation.cpp:843-846`

```cpp
bool minimap_requires_animation()
{
    return hud_radar::requires_animation();
}
```

with `#include "hud_radar.h"` added to `src/animation.cpp`. Its caller
(`src/handle_action.cpp:314`) is unchanged.

Leave alone (still reachable / out of scope): `pixel_minimap_option`,
`game::toggle_pixel_minimap`, the `toggle_pixel_minimap` action and its keybinding,
every `PIXEL_MINIMAP_*` option, `window_panel::get_height` (`src/panels.cpp:426-439`),
and the look-around sizing at `src/game_ui_extra.cpp:1145-1149`.

### Step 2 — `hud_phosphor`: a `radar` region + ladder RGBA

**2a. `src/hud_phosphor.h`** — add to `struct layout` (after `dock`, line 110):

```cpp
    cell_rect radar;    ///< right column, above the dock: the dot minimap viewport
```

and declare beside `hex` (after line 52):

```cpp
/// The same rung as `hex`, as straight RGBA in 0..1 — for the GPU dot layer,
/// which cannot consume a CSS string. Falls back to the built-in ladder exactly
/// as `hex` does.
auto rgba( ink i ) -> std::array<float, 4>;
```

(`#include <array>` at the top of the header.)

**2b. `src/hud_phosphor.cpp`** — in the anonymous namespace, next to `to_byte`
(line 57), add the inverse:

```cpp
/// `#rrggbbaa` → straight RGBA 0..1. Only ever fed the `ink_fallback` literals,
/// which are always 9 bytes; anything else yields opaque black rather than
/// throwing, so a malformed literal degrades instead of crashing the HUD.
auto parse_hex8( std::string_view hex ) -> std::array<float, 4>
{
    auto out = std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f };
    if( hex.size() != 9 || hex[0] != '#' ) {
        return out;
    }
    const auto nib = []( char c ) -> int {
        if( c >= '0' && c <= '9' ) { return c - '0'; }
        if( c >= 'a' && c <= 'f' ) { return c - 'a' + 10; }
        if( c >= 'A' && c <= 'F' ) { return c - 'A' + 10; }
        return 0;
    };
    for( auto k = 0; k < 4; ++k ) {
        out[k] = static_cast<float>( nib( hex[1 + 2 * k] ) * 16 + nib( hex[2 + 2 * k] ) ) / 255.0f;
    }
    return out;
}
```

and define, beside `hex` (after line 390):

```cpp
auto hud_phosphor::rgba( ink i ) -> std::array<float, 4>
{
    const auto r = rung( i );
    auto out = std::array<float, 4>{};
    if( ui_theme::get_rcss_rgba( std::string( ink_tokens[r] ), out.data() ) ) {
        return out;
    }
    return parse_hex8( ink_fallback[r] );
}
```

**2c. Row constants** (`src/hud_phosphor.cpp`) — replace the `dock_max_rows`
block at lines 242-253 with:

```cpp
/// The radar viewport's height. 34 interior cells wide at a 1:2 cell aspect makes
/// 17 rows the square, which is what keeps the dot pitch equal on both axes at the
/// authored 192-column grid.
constexpr int radar_max_rows = 17;

/// DOCK's height, now that the 11-row text overmap chunk it used to carry has been
/// replaced by the radar region above it:
///    1 mission-marker caption row
///   + 1 `┤ TARGET ├` rule row
///   + 3 target rows                 (name and range, HP bar, status)
///   + 1 `┤ ARMS ├` rule row
///   + 4 arms rows                   (wielded, damage, sidearm, ammo)
///   + 1 closing rule row
///   = 11
constexpr int dock_max_rows = 11;
```

**2d. `layout_for`** (`src/hud_phosphor.cpp:449-464`) — replace the `l.dock`
assignment and the mirror block:

```cpp
    l.soma = { .col = 0, .row = body_top, .cols = soma_cols,
               .rows = std::min( soma_max_rows, body_rows ) };
    // Right column, top to bottom: RADAR, DOCK, VEHICLE. The radar is first
    // because it is what the shared status rule above titles.
    l.radar = { .col = cols - dock_cols, .row = body_top, .cols = dock_cols,
                .rows = std::min( radar_max_rows, body_rows ) };
    const auto dock_top = bottom( l.radar );
    l.dock = { .col = cols - dock_cols, .row = dock_top, .cols = dock_cols,
               .rows = std::clamp( body_bot - dock_top, 0, dock_max_rows ) };
    if( o.show_vehicle ) {
        const auto veh_top = bottom( l.dock );
        l.vehicle = { .col = l.dock.col, .row = veh_top, .cols = dock_cols,
                      .rows = std::clamp( body_bot - veh_top, 0, veh_max_rows ) };
    }
    if( !o.sidebar_right ) {
        mirror( l.soma, cols );
        mirror( l.radar, cols );
        mirror( l.dock, cols );
        mirror( l.vehicle, cols );
    }
```

Then add `&l.radar` to the three region lists that follow:

- stage-1 yield loop (line 484): `{ &l.soma, &l.radar, &l.dock, &l.vehicle }`
- stage-2 yield loop (line 499): `{ &l.vehicle, &l.dock, &l.radar, &l.soma }`
  — the radar yields before SOMA and after DOCK: it is supplementary, the body
  panel is not.
- the zero-clamp loop (line 510): `{ &l.status, &l.soma, &l.radar, &l.dock, &l.log, &l.keys, &l.vehicle }`

### Step 3 — New module `src/hud_radar.{h,cpp}` (depends on 2)

New files; `src/CMakeLists.txt:5` globs `src/*.cpp` with `CONFIGURE_DEPENDS`, so no
build-file edit is needed. No existing helper does per-tile minimap classification
or screen-space dot batching, so this is new code.

**`src/hud_radar.h`** — verbatim:

```cpp
#pragma once
#ifndef CATA_SRC_HUD_RADAR_H
#define CATA_SRC_HUD_RADAR_H

#include "hud_phosphor.h"

class avatar;

/// The dot-matrix tactical minimap that occupies the HUD's RADAR region.
///
/// The dots are GPU quads on the unlit UI layer (`render_state::queue_ui_rect`),
/// not RmlUi elements: this project registers no custom `Rml::ElementInstancer`,
/// so a document cannot host C++-drawn geometry, and a few thousand real elements
/// per frame would not be affordable if it could. The RmlUi side of the region is
/// therefore nothing but the box-glyph border emitted by `hud_radar_frame`, and
/// the quads are drawn UNDER it — which is also why this layer paints its own
/// opaque ground instead of relying on `.ph-veil`.
namespace hud_radar
{

/// Queue the ground, the world dots and the creature blips for this frame.
/// No-op when the radar is toggled off, when `l.radar` is empty, or when the
/// render state is not ready. MUST be called from inside a `ui_adaptor` redraw
/// callback so the quads land in that adaptor's retained slice.
auto draw( const avatar &u, const hud_phosphor::layout &l ) -> void;

/// True when the last `draw` emitted a blinking hostile blip, i.e. the main loop
/// must keep redrawing for the blink to be visible. Read by
/// `minimap_requires_animation()` in `animation.cpp`.
auto requires_animation() -> bool;

} // namespace hud_radar

#endif // CATA_SRC_HUD_RADAR_H
```

**`src/hud_radar.cpp`** — implement exactly this behaviour.

*Geometry.* All in logical window pixels.

```
scale   = rmlui_layer::ui_scale()   ; if <= 0, use 1.0f
r       = hud_phosphor::to_dp( l.m, l.radar )
// The region's leading cell is the box-drawing vertical the producer emits;
// the interior is everything to the right of it (mirrored when the sidebar is
// on the left, i.e. when l.radar.col == 0 the vertical is the LAST cell).
border_on_left = ( l.radar.col > 0 )
ix = ( r.x + ( border_on_left ? l.m.cell_w : 0.0f ) ) * scale
iy =   r.y * scale
iw = ( r.w - l.m.cell_w ) * scale
ih =   r.h * scale
if ( iw < 8 || ih < 8 ) return;                      // degenerate: draw nothing

pitch   = max( 2, (int)floor( min( iw, ih ) / 65.0f ) )   // 65 = the design's tile span
tiles_x = clamp( odd_at_most( (int)floor( iw / pitch ) ), 1, 129 )
tiles_y = clamp( odd_at_most( (int)floor( ih / pitch ) ), 1, 129 )
ox = ix + ( iw - tiles_x * pitch ) * 0.5f            // player tile lands dead centre
oy = iy + ( ih - tiles_y * pitch ) * 0.5f
```

`odd_at_most(n) = max( n % 2 == 0 ? n - 1 : n, 1 )` (a local copy; the one in
`hud_phosphor_panels.cpp` is being deleted in Step 4).

*Emission order is z-order* — queue order is paint order on this layer. Exactly
four passes, in this sequence, all inside one `draw()` call:

1. the interior ground quad,
2. the world dots, one pass over the whole `tiles_x × tiles_y` grid,
3. the creature blips (monsters, then NPCs),
4. the player cross.

Ground first, so every later quad lands on a controlled luminance:

```
ground = hud_phosphor::rgba( ink::ground )
rs.queue_ui_rect( ix, iy, iw, ih, ground[0], ground[1], ground[2], 0.92f );
```

*Shape and gutter.* Two shape families, plus a third for water/vegetation:

| shape | pixel rect inside the `pitch × pitch` cell |
|---|---|
| `full` | the whole cell — `pitch × pitch`, so walls butt together into continuous building outlines |
| `dot`  | `d × d` centred, `d = max( 1, pitch - max( 1, gutter ) )` |
| `half` | `pitch × max( 1, pitch / 2 )`, bottom-aligned |

`gutter` from `PIXEL_MINIMAP_MODE`: `"solid"` → 0, `"squares"` → 1, `"dots"` → 2
(the shipping default). The `max(1, gutter)` floor is deliberate: in `solid` mode a
`dot` would otherwise equal a `full` and the shape channel would collapse, making
wall/furniture and opening/stairs indistinguishable.

*Classification*, first match wins, evaluated per tile:

```cpp
const ter_t &t = m.ter( p ).obj();
if( t.is_null() )                                          -> none
if( t.has_flag( TFLAG_GOES_UP ) || t.has_flag( TFLAG_GOES_DOWN )
    || t.has_flag( TFLAG_RAMP ) )                          -> stairs
if( !t.open.is_null() || !t.close.is_null() )              -> opening   // doors, windows, gates
if( t.has_flag( TFLAG_WALL ) )
    -> t.transparent ? opening : wall                                   // a transparent wall IS a window
if( m.has_furn( p ) )                                      -> furniture
if( t.has_flag( TFLAG_SWIMMABLE ) || t.has_flag( TFLAG_LIQUID )
    || t.has_flag( TFLAG_DEEP_WATER ) )                    -> water
if( t.has_flag( TFLAG_TREE ) || t.has_flag( TFLAG_SHRUB ) )-> vegetation
if( t.has_flag( TFLAG_NO_FLOOR ) )                         -> none      // open air / shaft
-> indoors ? floor_in : ground
```

`indoors = cache.outside_cache[ cache.idx( x, y ) ] == 0`.

Vehicles are stamped **after** the terrain sweep, from `cache.vehicle_list`
(O(parts), not O(map)): for each `vehicle *v`, for each `p : v->get_points()` with
`p.z() == z`, convert with `m.abs_to_bub(p)` and overwrite that grid cell with
`vehicle` — but only if the cell passed the visibility gate below.

*World-layer table* (rung, shape) — every pair is unique in both channels:

| category | rung | shape |
|---|---|---|
| `opening` (door/window) | `ink::peak` | full |
| `wall` | `ink::datum` | full |
| `vehicle` | `ink::label` | full |
| `stairs` | `ink::peak` | dot |
| `furniture` | `ink::datum` | dot |
| `floor_in` (roofed floor) | `ink::rule` | dot |
| `ground` (open ground) | `ink::dead` | dot |
| `vegetation` | `ink::rule` | half |
| `water` | `ink::dead` | half |
| `none` | — | nothing drawn |

*Visibility gate*, per tile, before anything is drawn:

```
if( !cache.inbounds( point_bub_ms( x, y ) ) )                -> skip
vis = m.get_visibility( cache.visibility_cache[ cache.idx( x, y ) ], vis_vars )
seen_now = ( vis == VIS_CLEAR || vis == VIS_LIT || vis == VIS_BOOMER )
remembered = u.should_show_map_memory()
             && cache.map_memory_seen_cache[ x + y * cache.cache_x ]   // NOTE: different stride
if( !seen_now && !remembered )                               -> skip
alpha = seen_now ? 1.0f : 0.45f
```

Two tiers only. The 0.45 dim is applied to alpha, not to the rung, so the ladder's
ordering survives inside each tier.

*Brightness.* Every emitted colour's RGB is multiplied by
`get_option<int>( "PIXEL_MINIMAP_BRIGHTNESS" ) / 100.0f` and clamped to `[0,1]`.
Alpha is untouched.

*Creature blips.* Iterate `g->all_monsters()` then `g->all_npcs()`; skip any critter
where `!u.sees( critter )`, where `critter.bub_pos().z() != z`, or whose tile falls
outside the grid. Attitude is `critter.attitude_to( u )` (the renderer's convention).

```
beacon = clamp( get_option<int>( "PIXEL_MINIMAP_BEACON_SIZE" ), 1, 4 ) * pitch
thin   = max( 1, beacon / 2 )
```

| who | colour source | shape (centred on the tile cell) | motion |
|---|---|---|---|
| `A_HOSTILE` | `g_outline_col_hostile` | filled square `beacon × beacon` | blinks |
| `A_NEUTRAL` (and `A_ANY`) | `g_outline_col_neutral` | horizontal bar `beacon × thin` | steady |
| `A_FRIENDLY` | `g_outline_col_friendly` | vertical bar `thin × beacon` | steady |
| the avatar | `g_outline_col_self` | cross: `(beacon+2) × thin` plus `thin × (beacon+2)` | steady, drawn last |

Include `"sdl_lighting_devui.h"` for the four `float[4]` globals. Do **not** invent
new colour constants — these are the same values the in-world creature outlines use
and they stay F4-tunable.

Blink: `blink = get_option<int>( "PIXEL_MINIMAP_BLINK" )`; when `blink > 0` the
hostile square is emitted only while `( SDL_GetTicks() / ( blink * 200u ) ) % 2 == 0`.
When `blink == 0` it is always emitted.

*Animation flag.* A file-scope `bool g_wants_anim` is assigned at the very end of
every `draw()` — `true` iff the radar drew AND `blink > 0` AND at least one hostile
blip was in range this frame — and set to `false` on every early return.
`requires_animation()` returns it.

*Entry guard* at the top of `draw()`, in order:
`if( !pixel_minimap_option || l.radar.rows <= 0 || l.radar.cols <= 1 || g == nullptr )`
→ clear `g_wants_anim`, return. Then
`auto &rs = lighting::get_render_state(); if( !rs.ready() ) { ... return; }`.

Includes needed: `hud_radar.h`, `<SDL3/SDL_timer.h>`, `avatar.h`, `cached_options.h`,
`character.h`, `creature.h`, `game.h`, `game_constants.h`, `map.h`, `mapdata.h`,
`monster.h`, `npc.h`, `options.h`, `sdl_lighting_devui.h`, `vehicle.h`,
`lighting/render_state.h`, `lighting/rmlui_layer.h`.

### Step 4 — Replace the dock's text chunk with the radar frame (depends on 3)

**4a. `src/hud_phosphor_panels.cpp` — `hud_dock`** (line 814): delete the whole
overmap-chunk section, lines **822-877**, i.e. `dock_fixed_rows`, `map_w`, `map_h`,
the `overmap_chunk_rows` call, `lead`, `marker` and the `for` loop. Keep `here`,
`custom`, `active`, `target_omt`, `has_mission` — the mission-caption row at 879-893
still uses them. The function's first emitted row becomes that caption.

**4b. Same file — new producer**, placed just above `hud_dock`:

```cpp
/// The RADAR region's RmlUi content: nothing but the play-area-facing vertical,
/// one row per viewport row.
///
/// The dots themselves are GPU quads on the UI layer, which composites BELOW the
/// RmlUi document — so this element must stay transparent (no `.ph-veil` in the
/// stylesheet) and must emit no ground of its own. `hud_radar::draw` paints the
/// interior's ground itself, from the same `ink::ground` rung, for the reason
/// given in `hud_phosphor.h`: a luminance-only encoding needs a controlled
/// background, and an uncontrolled one would eat the two dimmest rungs.
auto hud_radar_frame( const hud_phosphor::layout &l ) -> std::string
{
    const auto cols = l.radar.cols;
    if( cols <= 0 || l.radar.rows <= 0 ) {
        return {};
    }
    auto rows = std::vector<std::string>();
    rows.reserve( l.radar.rows );
    for( auto k = 0; k < l.radar.rows; ++k ) {
        rows.push_back( compose( { .segs = {}, .cols = cols, .border = glyph_vert,
                                   .border_leads = true } ) );
    }
    return wrap_rows( rows );
}
```

`compose` pads the remainder with `ink::rule` spaces, which `render_runs` converts
to U+00A0 — the only fill that survives `data-rml`'s parse-time whitespace trim.
`border_leads = true` matches DOCK; the whole right stack is mirrored by
`layout_for`, so this file stays side-agnostic.

`src/hud_phosphor_panels.h`: declare it above `hud_dock` (line 61) —

```cpp
/// The RADAR region's frame. Rows are `l.radar.cols` cells wide and carry no
/// data: the dot field itself is drawn by `hud_radar::draw` on the GPU UI layer.
auto hud_radar_frame( const hud_phosphor::layout &l ) -> std::string;
```

and fix the file's header comment, which still says the HUD has "two translucent
content panels" and that DOCK carries an "overmap chunk" (lines 11-16): DOCK's
first row is now the mission caption, and RADAR is a third, deliberately
un-translucent region.

**4c. Same file — delete the helpers the chunk was the only user of**:
`overmap_ink` (line 384 and its doc comment), `odd_at_most` (line 665 and its doc
comment), `max_map_dim` (line 65). Keep `strip_rml` — `fit()` still calls it at
line 170. Keep `utf8_display_split` (it lives in `catacharset` and `mapgen.cpp`
uses it). Drop the now-unused `#include "overmap.h"` only if nothing else in the
file needs it (`ACTIVE_OVERMAP_BUFFER` / `overmap::invalid_tripoint` are still used
by the mission caption, so it stays).

**4d. Delete `overmap_ui::overmap_chunk_rows`** — definition
`src/panels.cpp:451-510` (including the long comment block at 451-459) and
declaration `src/panels.h:46-48`. `hud_dock` was its only caller.

### Step 5 — Retitle the shared status rule (depends on 4)

`src/hud_phosphor_strips.cpp`:

- line **1001**: `{ .col = dock_title, .text = _( "OVERMAP" ) }` →
  `{ .col = dock_title, .text = _( "RADAR" ) }`.
- The right-hand column stop must survive `l.dock` being clamped out on a short
  grid while `l.radar` lives. Add above `frame_of` (line 656):

  ```cpp
  /// The right column's stop. RADAR is the topmost right-hand region and DOCK sits
  /// under it in the same columns, so either answers — but on a short grid the one
  /// that gets clamped to an empty rect reports col 0, which would drag the status
  /// strip's vertical to the screen edge. Prefer whichever is actually present.
  auto right_column( const hud_phosphor::layout &l ) -> hud_phosphor::cell_rect
  {
      return l.radar.cols > 0 ? l.radar : l.dock;
  }
  ```

  and use it in `frame_of` (line 660) and for `dock_title` (line 996), replacing
  both `l.dock` reads.

### Step 6 — Wire the region into the document (depends on 4)

**6a. `src/panels.cpp`**

- `struct hud_rml_model` (line 648): add `Rml::String radar_rml;` after `dock_rml`.
- `sidebar_hud_open` (line 796): add `c.Bind( "radar_rml", &g_hud_data->radar_rml );`
  after the `dock_rml` bind.
- `sidebar_hud_apply_rect` (line 849): add `place( "hud-radar", l.radar );` before
  `place( "hud-dock", l.dock );`.
- `sidebar_hud_sync`, next to the dock producer (line 962):

  ```cpp
  g_hud_data->radar_rml = hud_radar_frame( l );
  g_hud_data->handle.DirtyVariable( "radar_rml" );
  ```

- at the very end of `sidebar_hud_sync`, **after** `sidebar_hud_apply_rect( l );`
  (line 993):

  ```cpp
  // The dot layer, queued into the main adaptor's UI slice. It must run after the
  // rect writer so the region it draws into is the one the document just moved to,
  // and it must run from here rather than from game::draw so that the layout it
  // reads is the same object every producer above read.
  hud_radar::draw( u, l );
  ```

  with `#include "hud_radar.h"` at the top of `panels.cpp`.

**6b. `data/gui/sidebar_hud.rml`** — insert before the `<div id="hud-dock" …>`
block (line 136):

```html
    <!-- RADAR — right column under the status rule, and the one region that is
         NOT veiled. Its content is the dot minimap, drawn as GPU quads on the UI
         compositing layer, which sits UNDER this document; a translucent veil over
         it would multiply the two dimmest rungs of the ladder into the ground and
         delete every open-terrain dot. `hud_radar::draw` therefore paints its own
         `ink::ground` fill first, and all this element contributes is the vertical
         that closes the panel's play-area-facing edge. Titled by the status rule
         above, as SOMA and DOCK are. -->
    <div id="hud-radar">
        <div data-rml="radar_rml"></div>
    </div>

```

**6c. `data/gui/sidebar_hud.rcss`**

- line **89**: add `#hud-radar` to the positioning selector list.
- line **119-120**: add `#hud-radar > div` to the wrapper selector list.
- Do **not** add `.ph-veil` to it and do not give it a `decorator` — transparency is
  the requirement, not an omission. Add a short comment saying so at the end of the
  "FOUR TRANSLUCENT PANELS" block (~line 388).

### Step 7 — Extend the layout test (depends on 2)

`tests/hud_phosphor_test.cpp:321-323`: add `l.radar` to the `regions[]` array so the
existing in-grid and pairwise-disjointness checks cover it across the whole
width/height/sidebar/vehicle sweep.

---

## Critical files & anchors

| File | Anchor | Why |
|---|---|---|
| `src/hud_phosphor.cpp` | `layout_for` 421-516, row constants 242-260 | the radar's rect, and the three region lists that must learn about it or it will overlap the log |
| `src/panels.cpp` | `sidebar_hud_sync` 864-994, `sidebar_hud_apply_rect` 831-860, `hud_dp_ratio` 697-701 | the only place the frame's layout exists; the GPU draw must hang off the same object, after the rect writer |
| `src/lighting/render_state.h` | `queue_ui_rect` 81, `set_current_slices` 240 | the draw primitive, and the reason it must be called inside a redraw callback |
| `src/hud_phosphor_panels.cpp` | `hud_dock` 814-877, `compose` 246-256 | what is being deleted, and the row builder the new frame producer reuses |
| `src/map.h` | `level_cache` 310-439 | `outside_cache`/`visibility_cache` use `idx(x,y)=x*cache_y+y`, but `map_memory_seen_cache` uses `x+y*cache_x` — mixing them silently transposes the fog of war |

---

## Verification

Build (never synchronously, never with a short cap — a killed ninja corrupts
`.ninja_deps`). Write a `.bat` that calls `vcvars64.bat` first, then:

```
cmake --build C:/WORK/GIT_REPOS/Cataclysm-BN-Forked/out/build/win-rel-deb ^
      --target cataclysm-bn-tiles cata_test-tiles
```

Run it as a background job with a 1200 s+ timeout and poll to completion.

**1 — Layout invariants (automated).**

```
out/build/win-rel-deb/tests/cata_test-tiles.exe "[hud_phosphor]"
```

First confirm the binary actually contains them (`--list-tests` must report a
non-zero match count; the runner floods stdout, so filter). Expected: all pass,
including the extended `regions[]` sweep — the radar never leaves the grid and never
overlaps SOMA, DOCK, LOG, KEYS or VEHICLE at any of the swept sizes, in either
sidebar orientation, driving or not.

**2 — The radar exists and is live (manual, pixel-measured).**
Launch the installed build straight into a save (`--world`, per the
`cbn-launch-into-save-harness` recipe) and take screenshot A. Let
`R` = the radar's on-screen rect (top of the right column, under the status rule;
its title now reads `RADAR`, not `OVERMAP`).

- **Static proof:** count non-background pixels inside `R` in A. A dead draw gives
  ~0; a working one gives thousands. Also confirm by eye that walls form continuous
  bright lines (buildings read as outlines) while open ground reads as a faint dot
  lattice.
- **Live proof:** walk the avatar several tiles (`l`/`j` etc.), screenshot B, diff
  `R` between A and B. **Broken → max diff 0. Working → hundreds of differing
  pixels**, because the dot field scrolls under a fixed centre. Diff the full frame
  too, to prove frames are live at all.

**3 — Blips (manual, the new behaviour end to end).**
Debug-spawn a hostile monster within ~20 tiles (`~` debug menu → spawn monster).
Measure inside `R` the count of pixels with `r > 0.6 && g < 0.4 && b < 0.4`:
**0 before the spawn, > 0 after.** Take two screenshots ~1 s apart with
`PIXEL_MINIMAP_BLINK = 10` and confirm that count alternates between >0 and 0 —
that proves both the blip and the `requires_animation()` redraw wiring. Then spawn
an NPC ally and confirm a green (`g > 0.6 && r < 0.5`) vertical bar appears.

**4 — The toggle still works.** Press the `toggle_pixel_minimap` key: the dot field
must vanish while the region's box-glyph border and the `RADAR` title remain (the
RmlUi frame is not gated on the option). Press again to restore. With it off,
confirm the input timeout is no longer pinned (no continuous full redraws).

**5 — Regression.** Confirm the DOCK panel below still shows the mission caption,
TARGET and ARMS rows, correctly framed, and that the status rule's two `┼`
crossings still land exactly on the SOMA and RADAR verticals in the rows beneath.
Then flip `SIDEBAR_POSITION` to `left` and repeat check 2's static proof — the
radar must mirror with the column, and its interior must shift by one cell because
the border glyph moves to the far side.

---

## Assumptions & contingencies

- **`ter_t::open`/`close` is the door/window test.** No single flag marks them, and
  a door is exactly a terrain with an open/close transform. If in play some
  ordinary terrain (a curtain, a hatch) lights up as `opening`, that is acceptable —
  it *is* an opening. If instead windows come out as plain `wall`, the
  `t.transparent` branch is the fallback already in the classifier; widen it before
  adding a new flag.
- **17 radar rows.** Chosen so the interior is square at the authored 192-column
  grid (34 cells × 10 dp = 340 dp wide; 17 rows × 20 dp = 340 dp tall). If a narrow
  window makes the right stack overflow — radar 17 + dock 11 + vehicle 8 + status 3
  + keys 2 = 41 rows, versus `grid_min_rows` 30 — `layout_for`'s existing clamping
  shortens the vehicle panel first and then the dock, which is correct; do not
  lower `radar_max_rows` to chase it.
- **A 65-tile design span, not an option.** `pitch` is derived from it and the
  actual tile counts fall out of the interior size, so the radar always fills its
  region. At 1920×1080 this yields pitch 5 and a 65×65 tile window (±32 tiles),
  comfortably inside the default reality bubble. No new option is added; if a range
  control is wanted later it belongs beside the other `PIXEL_MINIMAP_*` options.
- **No caching of the static layer.** ~4 200 `ter`/`furn` lookups per *redraw* (not
  per frame — `sidebar_hud_sync` runs from the `ui_adaptor` redraw callback) is far
  below what the tile renderer already does, and caching would introduce staleness
  on every terrain change. If profiling later shows this in the top of a
  `[render][perf][phase avg/max ms]` line, cache keyed on
  `level_cache::transparency_generation` plus the player's tile — but measure first.
- **Blip attitude uses `critter.attitude_to( u )`**, matching the creature-outline
  renderer. If an allied NPC ever reads as neutral, the alternative convention
  (`u.attitude_to( critter )`, used by the hostile counters at
  `src/game_misc.cpp:897`) is the fallback — switch both monsters and NPCs
  together, never one of them.
- **`astyle`/`clang-format` are not installed on this machine**, so the cmake
  `format` target does not exist and `CMakeModules/FormatSource.cmake:41` excludes
  `src/<subdir>/` anyway. Match the surrounding style by hand and say so.
