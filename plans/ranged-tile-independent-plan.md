# Ranged Combat Tile-Independent Rework

## Status: ✅ Complete (2026-07-12)

All 13 steps implemented, built, and verified. Post-implementation fixes applied:

| Item | Change | File |
|------|--------|------|
| Live mouse position | `get_sdl_mouse_pos()` via `SDL_GetMouseState` replaces stale `get_raw_input().mouse_pos` | `sdl_window_dims.h/cpp`, `ranged.cpp` |
| Uniform spread | `rng_float(-deflect_angle, deflect_angle)` replaces `(one_in(2) ? 1 : -1) * deflect_angle` | `ballistics.cpp:422` |
| Private field access | `ctxt.get_raw_input().mouse_pos` replaces `ctxt.coordinate` | `ranged.cpp:3874` |
| Free function call | `clear_map()` replaces `map_helpers::clear_map()` | `tests/ranged_aiming_test.cpp:307` |

**Tests**: `[ranged]` suite — 805 assertions in 18 test cases, all pass.

---

## Context

The current ranged system snaps aiming to integer tile centres (Bresenham) and walks projectiles tile-by-tile, destroying both angular precision and sub-tile hit fidelity. This rework makes the full chain continuous: mouse movement produces a `units::angle`, a DDA ray walks that exact angle for the aim preview, and the **actual projectile** also travels along a float DDA ray derived from the rolled dispersion angle — enabling sub-tile hit accuracy and eliminating grid-snap artefacts. Spread is visualised as SDL pixel-line wedges (Intravenous 2 style), and keyboard aiming rotates the aim angle in 1° steps (Hotline Miami feel). The public signatures of `fire_gun` and `projectile_attack` are unchanged; only their internals change.

---

## Approach

Steps 1–7 are strictly sequential. Steps 8–9 depend on 1–7 and are independent of each other. Steps 10–13 depend on 1–9: step 10 adds RMB press-to-aim infrastructure; step 11 is a verification no-op that confirms the existing recoil system drives the shrinking cone automatically; steps 12 and 13 share the `set_aim_angle` hook from step 3 and can be implemented in parallel with each other once step 10 is done.

---

### Step 1 — DDA ray utility (`src/map.h`, `src/map.cpp`) ✅

Declare in `src/map.h` (public section, near `find_clear_path`):
```cpp
/// Amanatides-Woo DDA grid traversal from src along angle for up to
/// max_range Euclidean tiles. Returns tiles visited (src excluded).
auto ray_cast_angle( const tripoint_bub_ms &src, double angle_rad,
                     int max_range ) const -> std::vector<tripoint_bub_ms>;
```

Implement in `src/map.cpp` immediately after `find_clear_path` (after line 6476):
```cpp
auto map::ray_cast_angle( const tripoint_bub_ms &src, double angle_rad,
                          int max_range ) const -> std::vector<tripoint_bub_ms>
{
    const auto dx = std::cos( angle_rad );
    const auto dy = std::sin( angle_rad );
    const auto step_x = dx >= 0.0 ? 1 : -1;
    const auto step_y = dy >= 0.0 ? 1 : -1;
    // Start at tile centre
    const auto fx0 = src.x() + 0.5;
    const auto fy0 = src.y() + 0.5;
    const auto inf = std::numeric_limits<double>::infinity();
    const auto t_delta_x = dx != 0.0 ? std::abs( 1.0 / dx ) : inf;
    const auto t_delta_y = dy != 0.0 ? std::abs( 1.0 / dy ) : inf;
    auto t_max_x = dx != 0.0
        ? std::abs( ( step_x > 0 ? std::ceil( fx0 ) - fx0
                                 : fx0 - std::floor( fx0 ) ) / dx )
        : inf;
    auto t_max_y = dy != 0.0
        ? std::abs( ( step_y > 0 ? std::ceil( fy0 ) - fy0
                                 : fy0 - std::floor( fy0 ) ) / dy )
        : inf;
    auto tx = src.x();
    auto ty = src.y();
    auto result = std::vector<tripoint_bub_ms>{};
    result.reserve( static_cast<size_t>( max_range ) + 2 );
    while( true ) {
        if( t_max_x < t_max_y ) {
            t_max_x += t_delta_x;
            tx += step_x;
        } else {
            t_max_y += t_delta_y;
            ty += step_y;
        }
        const auto dist = std::hypot( static_cast<double>( tx - src.x() ),
                                      static_cast<double>( ty - src.y() ) );
        if( dist > static_cast<double>( max_range ) ) { break; }
        const auto tile = tripoint_bub_ms{ tx, ty, src.z() };
        if( !inbounds( tile ) ) { break; }
        result.push_back( tile );
    }
    return result;
}
```

Edge cases: `max_range == 0` → empty. Axis-aligned angle (dx or dy == 0) → infinity in the opposite t_delta prevents premature stepping. Out-of-bounds → break immediately. Does NOT stop at impassable terrain; the trajectory walk in ballistics handles that (same as `find_clear_path`).

---

### Step 2 — Float aim angle field + helper declarations (`src/ranged.cpp`, `src/input.h`) ✅

**A. Field** — in the `target_ui` private section, after `dst` (line 439):
```cpp
// Continuous aim direction. Derived from mouse pixel (exact) or keyboard
// tile-move (atan2 sync). Drives DDA ray for traj and dst instead of
// Bresenham-to-clicked-tile.
units::angle aim_angle = 0_radians;
```

**B. Private method declarations** — add after `set_cursor_pos` declaration:
```cpp
auto set_aim_angle( units::angle angle ) -> void;
auto sync_aim_angle_from_dst() -> void;
```

**C. `input_context` method** — declare in `src/input.h` (public section, near `get_coordinates`):
```cpp
// Returns aim angle from src tile centre to current mouse pixel.
// Returns nullopt in curses builds (tilecontext is null) or when cursor
// is on src (distance < 0.01 tiles).
auto get_aim_angle_to_src( const tripoint_bub_ms &src ) const
    -> std::optional<units::angle>;
```

---

### Step 3 — Implement the three helpers ✅

**`set_aim_angle`** — add to `src/ranged.cpp` (near `set_cursor_pos` implementation):
```cpp
auto target_ui::set_aim_angle( units::angle angle ) -> void
{
    aim_angle = angle;
    const auto &here = get_map();
    const auto ray = here.ray_cast_angle( src, units::to_radians( angle ), range );
    if( ray.empty() ) { return; }
    traj = ray;
    dst  = ray.back();

    if( snap_to_target ) { set_view_offset( dst - src ); }

    // Update facing direction (mirrors set_cursor_pos lines 3120–3132)
    const auto d = dst.xy() - src.xy();
    if( !tile_iso ) {
        if( d.x() > 0 )      { you->facing = FacingDirection::FD_RIGHT; }
        else if( d.x() < 0 ) { you->facing = FacingDirection::FD_LEFT; }
    } else {
        if( d.x() >= 0 && d.y() >= 0 ) { you->facing = FacingDirection::FD_RIGHT; }
        if( d.y() <= 0 && d.x() <= 0 ) { you->facing = FacingDirection::FD_LEFT; }
    }

    // Critter under cursor (mirrors set_cursor_pos lines 3134–3144)
    if( src != dst ) {
        auto *const cr = g->critter_at( dst, true );
        dst_critter = ( cr && pl_sees( *cr ) ) ? cr : nullptr;
    } else {
        dst_critter = nullptr;
    }

    if( mode == TargetMode::Fire ) { recalc_aim_turning_penalty(); }
    update_status();
}
```

**`sync_aim_angle_from_dst`** — add to `src/ranged.cpp`:
```cpp
auto target_ui::sync_aim_angle_from_dst() -> void
{
    const auto dx = static_cast<double>( dst.x() - src.x() );
    const auto dy = static_cast<double>( dst.y() - src.y() );
    if( std::hypot( dx, dy ) > 0.01 ) {
        aim_angle = units::atan2( dy, dx );
    }
}
```

**`get_aim_angle_to_src`** — implement in `src/sdl_window_dims.cpp` (which already includes `cata_tiles.h` giving access to `tilecontext`):
```cpp
auto input_context::get_aim_angle_to_src( const tripoint_bub_ms &src ) const
    -> std::optional<units::angle>
{
    if( !coordinate_input_received || !tilecontext ) { return std::nullopt; }
    const auto o  = tilecontext->get_tile_map_origin().raw();
    const auto op = tilecontext->get_drawing_pixel_offset();
    const auto tw = std::max( 1, tilecontext->get_tile_width() );
    const auto th = std::max( 1, tilecontext->get_tile_height() );
    // Same formula as sdl_render_frame.cpp:219-221 (cursor_light_emitter)
    const auto wx = ( coordinate.x - static_cast<double>( op.x ) ) / tw + o.x;
    const auto wy = ( coordinate.y - static_cast<double>( op.y ) ) / th + o.y;
    const auto dx = wx - ( src.x() + 0.5 );
    const auto dy = wy - ( src.y() + 0.5 );
    if( std::hypot( dx, dy ) < 0.01 ) { return std::nullopt; }
    return units::atan2( dy, dx );
}
```

`tilecontext` is `nullptr` on curses builds — the `!tilecontext` guard returns `nullopt` automatically. No separate stub or `#ifdef` needed.

---

### Step 4 — Wire angle path into `handle_cursor_movement` (`src/ranged.cpp`) ✅

**SELECT branch** — replace lines 3033–3036 with:
```cpp
} else if( action == "SELECT" ) {
    if( const auto angle = ctxt.get_aim_angle_to_src( src ) ) {
        // Mouse path: float pixel → exact angle → DDA ray
        set_aim_angle( *angle );
    } else if( const auto mouse_pos = ctxt.get_coordinates( g->w_terrain ) ) {
        // Curses fallback: tile-snap
        auto p = *mouse_pos;
        p.z() = you->bub_pos().z() + you->view_offset.z();
        set_cursor_pos( p );
        sync_aim_angle_from_dst();
    }
}
```

**Keyboard directional branch** — replace the current `if( delta ) { shift_view_or_cursor(*delta); }` block with angle rotation. The current `shift_view_or_cursor` lambda calls `set_cursor_pos` when `!shifting_view`. Replace it so when `!shifting_view`, we rotate `aim_angle` instead:

```cpp
if( delta ) {
    if( shifting_view ) {
        // View shift unchanged
        set_view_offset( you->view_offset + *delta );
    } else {
        // Rotate aim angle using cross-product sign of (aim_dir × key_dir)
        const auto aim_dx = units::cos( aim_angle );
        const auto aim_dy = units::sin( aim_angle );
        const auto key_dx = static_cast<double>( delta->x() );
        const auto key_dy = static_cast<double>( delta->y() );
        // z-component of cross product: positive = CCW, negative = CW (screen space)
        const auto cross = aim_dx * key_dy - aim_dy * key_dx;
        // dot product: negative = roughly opposite, positive = roughly same direction
        const auto dot = aim_dx * key_dx + aim_dy * key_dy;
        if( key_dx == 0.0 && key_dy == 0.0 ) {
            // z-only delta (LEVEL_UP/DOWN): handle separately below
        } else if( cross > 0.0 ) {
            set_aim_angle( aim_angle - 1_degrees );   // CCW
        } else if( cross < 0.0 ) {
            set_aim_angle( aim_angle + 1_degrees );   // CW
        } else if( dot > 0.0 ) {
            // Same direction as aim: advance 1° CW
            set_aim_angle( aim_angle + 1_degrees );
        } else {
            // Opposite direction: reverse 180°
            set_aim_angle( aim_angle + 180_degrees );
        }
    }
}
```

For LEVEL_UP/LEVEL_DOWN (z-axis): these currently call `shift_view_or_cursor` with a z delta. Preserve that behaviour: keep the existing z-shift code that adjusts `you->view_offset.z()` and calls `set_cursor_pos` with a z delta, then add `sync_aim_angle_from_dst()` after.

**NEXT_TARGET / PREV_TARGET** — after each `cycle_targets(n)` call, add `sync_aim_angle_from_dst()`. Do not add a new action or keybinding.

**CENTER** — after `set_cursor_pos( src )`, set `aim_angle = 0_radians;`.

---

### Step 5 — Initialize `aim_angle` on UI open (`src/ranged.cpp:2773`) ✅

After `set_cursor_pos( initial_dst )` at the end of the `run()` setup phase:
```cpp
sync_aim_angle_from_dst();
```

---

### Step 6 — Replace tile-walk with float DDA in `projectile_attack` (`src/ballistics.cpp`) ✅

This is the core tile-independence change. The projectile travels along the dispersion-deflected float angle instead of a Bresenham walk.

**A. Precise deletions and replacements in lines 415–462**

The block to touch (re-read before editing):
```
415: auto target = target_arg;
416: std::vector<tripoint_bub_ms> trajectory;
417: std::vector<std::pair<monster, const dealt_projectile_attack>> hit_monsters;
419–457: if/else trajectory block (miss deflect + find_clear_path)
459–462: add_msg( m_debug, ... target.x() ... )   ← uses deleted `target`
677: here.shoot( source, tp, proj, !no_item_damage && tp == target );   ← uses deleted `target`
```

Concrete edits, in this exact order:

1. **Delete line 415** (`auto target = target_arg;`). `hit_monsters` at line 417 and the new `trajectory` declaration both survive.

2. **Replace line 416** (`std::vector<tripoint_bub_ms> trajectory;`) with the full DDA block:
```cpp
    // Float DDA trajectory: dispersion deflects the aim angle; bullet travels
    // along the exact deflected angle rather than a Bresenham walk.
    const auto base_angle = units::atan2(
        static_cast<double>( target_arg.y() - source.y() ),
        static_cast<double>( target_arg.x() - source.x() ) );
    const auto deflect_angle =
        std::min( units::from_arcmin( aim.dispersion ), 30_degrees );
    const auto actual_angle = base_angle +
        ( one_in( 2 ) ? 1 : -1 ) * deflect_angle;

    // Play miss-sound when the shot deviates >= 1 tile laterally (unchanged threshold)
    if( aim.missed_by_tiles >= 1.0 ) {
        sfx::play_variant_sound( "bullet_hit", "hit_wall",
                                 sfx::get_heard_volume( target_arg ),
                                 sfx::get_heard_angle( target_arg ) );
    }

    const auto dda_max_range =
        static_cast<int>( no_overshoot ? range : extend_to_range );
    auto trajectory = here.ray_cast_angle(
        source, units::to_radians( actual_angle ), dda_max_range );
```

3. **Keep line 417** (`hit_monsters` declaration) — it is used at lines 653 and 751–755 and must not be removed.

4. **Delete lines 419–462**: the entire if/else trajectory block plus the `add_msg` that references the now-deleted `target` variable (lines 459–462 contain `target.x(), target.y(), target.z()`).

5. **Line 469** (`trajectory.insert( trajectory.begin(), source );`) stays — the DDA excludes src by design, and this insert matches the existing loop expectation.

6. **Delete lines 476–483** (trajectory extension block):
```cpp
// DELETE:
if( !no_overshoot && range < extend_to_range ) {
    std::vector<tripoint_bub_ms> trajectory_extension = continue_line( trajectory,
        extend_to_range - range );
    trajectory.reserve( trajectory.size() + trajectory_extension.size() );
    trajectory.insert( trajectory.end(), trajectory_extension.begin(), trajectory_extension.end() );
}
```
The DDA already goes to `dda_max_range` (= `extend_to_range` when `!no_overshoot`), making the extension redundant.

7. **Line 677** — replace `tp == target` with `tp == target_arg`:
```cpp
// OLD:
here.shoot( source, tp, proj, !no_item_damage && tp == target );
// NEW:
here.shoot( source, tp, proj, !no_item_damage && tp == target_arg );
```
`target` was initialised as `target_arg` and only changed on the now-deleted miss-deflect branch; `target_arg` is the correct aimed tile.

**B. Replace the `cur_missed_by` heuristic with geometric sub-tile calculation** — replace lines 594–603:

```cpp
// OLD (delete):
double cur_missed_by = aim.missed_by;
if( critter != nullptr && tp != target_arg ) {
    cur_missed_by = std::max( rng_float( 0.1, 1.5 - aim.missed_by ) /
                              critter->ranged_target_size(), 0.4 );
}

// NEW: geometric perpendicular distance from DDA ray to creature centre,
// normalised by target size. Identical to aim.missed_by for the aimed tile;
// physically correct (no random re-roll) for bystanders in the bullet path.
const auto cur_missed_by = [&]() -> double {
    if( critter == nullptr ) { return aim.missed_by; }
    const auto range_to_critter = static_cast<double>( rl_dist( source, tp ) );
    const auto angle_to_critter = units::atan2(
        static_cast<double>( tp.y() - source.y() ),
        static_cast<double>( tp.x() - source.x() ) );
    const auto delta     = normalize( actual_angle - angle_to_critter );
    const auto half_turn = 180_degrees;
    const auto abs_delta = delta > half_turn ? 360_degrees - delta : delta;
    const auto perp_dist = iso_tangent( range_to_critter, abs_delta );
    const auto sz        = critter->ranged_target_size();
    return sz > 0.0 ? std::min( 1.0, perp_dist / sz ) : 1.0;
}();
```

**C. Keep all remaining loop logic unchanged**: terrain `here.shoot()`, momentum, penetration, vehicle, z-floor, animation. The loop iterates over DDA tiles; every other decision is identical to before.
### Step 7 — Spread cone overlay (`src/ranged.cpp`, `src/cata_tiles.h`, `src/cata_tiles_anim.cpp`, `src/game.h`, `src/animation.cpp`) ✅

Renders a thin SDL pixel-line wedge showing weapon spread — visible in tiles mode only.

**A. Add field to `cata_tiles`** — in `src/cata_tiles.h`, private section after `cursors` (line 1297):
```cpp
bool do_draw_aim_crosshair = false;
```

**B. Add `init`/`void`/`draw` implementations in `src/cata_tiles_anim.cpp`** (after `void_cursor`, line 325):
```cpp
auto cata_tiles::init_draw_aim_crosshair( point pixel ) -> void
{
    aim_crosshair_pixel_ = pixel;
    do_draw_aim_crosshair = true;
}

auto cata_tiles::void_aim_crosshair() -> void
{
    do_draw_aim_crosshair = false;
    aim_crosshair_pixel_ = std::nullopt;
}
```

Add private field in `src/cata_tiles.h` alongside `do_draw_aim_crosshair`:
```cpp
std::optional<point> aim_crosshair_pixel_;
```

Add the spread cone + crosshair draw **inside `cata_tiles::draw_cursor()`** (after the tile-sprite cursor loop, after line 519):
```cpp
    if( do_draw_aim_crosshair && aim_crosshair_pixel_.has_value() ) {
        const auto c = *aim_crosshair_pixel_;
        constexpr auto arm = 6;
        // SDL3 API: SDL_SetRenderDrawColor + SDL_RenderLines (same as physics_debug_draw.cpp)
        SDL_SetRenderDrawColor( renderer.get(), 255, 80, 0, 220 );  // orange
        const std::array<SDL_FPoint, 2> h = { SDL_FPoint{ static_cast<float>( c.x - arm ),
                                                           static_cast<float>( c.y ) },
                                              SDL_FPoint{ static_cast<float>( c.x + arm ),
                                                           static_cast<float>( c.y ) } };
        const std::array<SDL_FPoint, 2> v = { SDL_FPoint{ static_cast<float>( c.x ),
                                                           static_cast<float>( c.y - arm ) },
                                              SDL_FPoint{ static_cast<float>( c.x ),
                                                           static_cast<float>( c.y + arm ) } };
        SDL_RenderLines( renderer.get(), h.data(), 2 );
        SDL_RenderLines( renderer.get(), v.data(), 2 );
        void_aim_crosshair();
    }
```

Declare both `init_draw_aim_crosshair` and `void_aim_crosshair` in `src/cata_tiles.h` public section alongside `init_draw_cursor`.

**C. Bridge on `game`** — declare in `src/game.h`:
```cpp
auto draw_aim_crosshair( point pixel ) -> void;
```

Implement in `src/animation.cpp` (after `draw_cursor`, line 541):
```cpp
auto game::draw_aim_crosshair( point pixel ) -> void
{
    if( !tilecontext ) { return; }  // curses no-op; tilecontext is null on non-SDL builds
    tilecontext->init_draw_aim_crosshair( pixel );
}
```

**D. Spread cone method** — add private declaration to `target_ui`:
```cpp
auto calc_spread_half_angle() const -> units::angle;
```

Implement in `src/ranged.cpp`:
```cpp
auto target_ui::calc_spread_half_angle() const -> units::angle
{
    if( mode != TargetMode::Fire || !relevant || range < 1 ) { return 0_radians; }
    const auto &here = get_map();
    const auto disp = calculate_dispersion( here, *you, *relevant,
                                            static_cast<int>( predicted_recoil ), false );
    // iso_tangent gives lateral miss in tiles at range for avg dispersion
    const auto miss_tiles = iso_tangent( static_cast<double>( range ),
                                         units::from_arcmin( disp.avg() ) );
    return units::from_radians( std::atan( miss_tiles / static_cast<double>( range ) ) );
}
```

**E. Call from `draw_terrain_overlay`** — at the end of `draw_terrain_overlay` (after the existing `g->draw_cursor(dst)` call, around line 3698):
```cpp
    // Spread cone: SDL pixel lines at ±half_angle from aim direction
    if( mode == TargetMode::Fire && dst != src ) {
        const auto half = calc_spread_half_angle();
        if( half > 0.01_radians ) {
            namespace views = std::views;
            const auto &here = get_map();
            const auto center = you->bub_pos() + you->view_offset;
            for( const auto edge : { aim_angle - half, aim_angle + half } ) {
                const auto ray = here.ray_cast_angle( src, units::to_radians( edge ), range );
                if( ray.empty() ) { continue; }
                const auto this_z = ray
                    | views::filter( [&center]( const tripoint_bub_ms &p ) {
                          return p.z() == center.z();
                      } )
                    | std::ranges::to<std::vector>();
                if( !this_z.empty() ) {
                    // 2-arg overload uses 'line_trail' (dimmer), no endpoint visibility check
                    g->draw_line( this_z.back(), this_z );
                }
            }
        }

        // Pixel-precise crosshair at actual mouse position
        g->draw_aim_crosshair( ctxt.coordinate );
    }
```

`ctxt.coordinate` is the raw pixel point (public member of `input_context`, `input.h:602`) set from the last mouse event. No SDL include needed in `ranged.cpp`.

If `map& here` is not already in scope at the top of `draw_terrain_overlay`, add `const auto &here = get_map();` at the function start.

---

### Step 8 — Spread cone for shotguns (fire loop, `src/ranged.cpp`) ✅

`fire_gun` already calls `projectile_attack` per pellet. Each pellet's `projectile_attack` now uses its own DDA at its own deflected angle. No change needed: the spread cone visualization shows the statistical spread, and each pellet travels its own actual path. This step is a no-op — confirming the existing pellet loop is already correct.

---

### Step 9 — Unit test for `ray_cast_angle` (`tests/ranged_test.cpp`) ✅

Find existing file `tests/ranged_test.cpp` (or `tests/ballistics_test.cpp`) and add a `TEST_CASE( "ray_cast_angle DDA accuracy", "[ranged]" )`:

```cpp
TEST_CASE( "ray_cast_angle DDA accuracy", "[ranged]" )
{
    // Setup: build a flat map large enough for the test
    map_helpers::clear_map();
    auto &here = get_map();
    const auto src = tripoint_bub_ms{ 60, 60, 0 };

    SECTION( "due east" ) {
        const auto ray = here.ray_cast_angle( src, 0.0, 10 );
        REQUIRE( !ray.empty() );
        for( const auto &t : ray ) {
            CHECK( t.y() == src.y() );
            CHECK( t.x() > src.x() );
        }
        CHECK( ray.back().x() <= src.x() + 10 );
    }

    SECTION( "45 degrees diagonal within one tile of y=x" ) {
        const auto ray = here.ray_cast_angle( src, M_PI / 4, 10 );
        for( const auto &t : ray ) {
            CHECK( std::abs( t.x() - t.y() - ( src.x() - src.y() ) ) <= 1 );
        }
    }

    SECTION( "max_range 0 returns empty" ) {
        const auto ray = here.ray_cast_angle( src, 0.0, 0 );
        CHECK( ray.empty() );
    }
}
```

---


### Step 10 — RMB press-to-aim; LMB fires; RMB release exits (`src/input.h`, `src/input.cpp`, `src/sdl_input.cpp`, `src/action.h`, `src/handle_action.cpp`, `src/sdl_window_dims.h`, `src/sdl_window_dims.cpp`, `src/ranged.cpp`, `data/raw/keybindings/keybindings.json`) ✅

**Design**: Three roles — hold RMB to open and remain in aim mode, click LMB to fire/throw, release RMB to cancel and exit without firing. Non-combat RMB actions (examine, close door, pickup) are unchanged: they fire on `MOUSE_BUTTON_RIGHT` UP via `SEC_SELECT`, which only runs when the targeting UI is not blocking the input context. The advisory double-trigger concern is eliminated because RMB UP never fires — exactly one mechanism (the TIMEOUT poll) handles exit, and LMB is the sole fire trigger.

**10A — Extend `mouse_buttons` enum** (`src/input.h` line 89):
```cpp
enum mouse_buttons {
    MOUSE_BUTTON_LEFT = 1, MOUSE_BUTTON_RIGHT, SCROLLWHEEL_UP,
    SCROLLWHEEL_DOWN, MOUSE_MOVE, MOUSE_BUTTON_RIGHT_DOWN
};
```

**10B — Register key name** (`src/input.cpp`):
After line 428 (`keyname_to_keycode["MOUSE_MOVE"] = MOUSE_MOVE;`):
```cpp
keyname_to_keycode["MOUSE_RIGHT_DOWN"] = MOUSE_BUTTON_RIGHT_DOWN;
```
After line 476 (`} else if( ch == MOUSE_MOVE ) {`):
```cpp
} else if( ch == MOUSE_BUTTON_RIGHT_DOWN ) {
    raw = translate_marker_context( "key name", "MOUSE_RIGHT_DOWN" );
```

**10C — Generate `MOUSE_BUTTON_RIGHT_DOWN` on SDL press** (`src/sdl_input.cpp`):
Between `SDL_EVENT_MOUSE_BUTTON_UP` (line 649) and `SDL_EVENT_MOUSE_WHEEL` (line 660), insert:
```cpp
case SDL_EVENT_MOUSE_BUTTON_DOWN:
    if( ev.button.button == SDL_BUTTON_RIGHT && !rmlui_layer::active() ) {
        d.last_input = input_event( MOUSE_BUTTON_RIGHT_DOWN, input_event_t::mouse );
    }
    break;
```

**10D — `ACTION_AIM_HOLD` enum entry** (`src/action.h`, after `ACTION_FIRE` at line 172):
```cpp
/** RMB press: opens aim UI if wielding a gun. LMB fires; RMB release cancels without firing. */
ACTION_AIM_HOLD,
```
Also add to `src/action.cpp`'s `action_id → string` table (grep the file for `ACTION_FIRE` to find the table): `ACTION_AIM_HOLD → "AIM_HOLD"`.

**10E — `ACTION_AIM_HOLD` handler** (`src/handle_action.cpp`):
After `case ACTION_FIRE: fire(); break;` (main switch ~line 2387):
```cpp
case ACTION_AIM_HOLD: {
    // Silently ignore if not wielding a gun; non-combat SEC_SELECT still fires on RMB UP.
    auto &weapon = u.primary_weapon();
    if( weapon.is_gun() && !weapon.gun_current_mode().melee() ) {
        avatar_action::fire_wielded_weapon( u );
    }
    break;
}
```
The cooperative-fiber path (~line 3547) has its own `case ACTION_FIRE:` — add a mirror `case ACTION_AIM_HOLD:` that calls `fire()` inside the same fiber lambda.

**10F — Keybinding** (`data/raw/keybindings/keybindings.json`):
After the `SEC_SELECT` entry (~line 1149), add:
```json
{
  "type": "keybinding",
  "id": "AIM_HOLD",
  "name": "Hold to aim (RMB); LMB fires; release RMB cancels",
  "bindings": [ { "input_method": "mouse", "key": "MOUSE_RIGHT_DOWN" } ]
}
```
**Do NOT** add any `MOUSE_RIGHT` binding in the TARGET context. RMB release is handled entirely by the TIMEOUT poll in 10H — no keybinding mechanism, no double-trigger risk.

**10G — `is_rmb_held` helper** (`src/sdl_window_dims.h`, `src/sdl_window_dims.cpp`):

Declaration in `src/sdl_window_dims.h` (near `get_coordinates`):
```cpp
/// True if the right mouse button is physically depressed right now.
auto is_rmb_held() -> bool; // *NOPAD*
```
Definition in `src/sdl_window_dims.cpp` (after `get_coordinates`):
```cpp
auto is_rmb_held() -> bool
{
    return ( SDL_GetMouseState( nullptr, nullptr ) & SDL_BUTTON_RMASK ) != 0;
}
```
In curses/no-SDL builds `SDL_GetMouseState` returns 0, so this correctly returns `false`.

**10H — Hold detection, auto-aim, fire, and cancel in `target_ui::run()`** (`src/ranged.cpp`):

Add to `target_ui` private section:
```cpp
bool opened_by_rmb = false; ///< aim UI was opened via RMB press; LMB fires, RMB release cancels
```

After `ctxt` registration, before `while(true)`:
```cpp
opened_by_rmb = is_rmb_held();
```

**TIMEOUT handler** — after edge-scroll, replace the unconditional `continue` with:
```cpp
if( opened_by_rmb ) {
    if( !is_rmb_held() ) {
        // RMB released → cancel aim mode; exit same as ESC/QUIT
        // Find the QUIT/ESCAPE return path in target_ui::run() and replicate it here.
        // Typically: break out of while(true) and return the cancel sentinel.
        break; // then return cancel sentinel after the loop
    }
    action_aim(); // RMB held: reduce recoil each tick, shrinks cone automatically
}
continue;
```
Confirm the cancel-return value by grepping `action == "QUIT"` or `action == "ESCAPE"` in `target_ui::run()` to find how ESC exits and returning the identical value. The `break` exits the `while(true)` loop; add the return statement after the loop with the same cancel sentinel.

**SELECT handler (LMB fires in hold-to-aim mode)** — in `handle_cursor_movement` (Step 4), modify the `"SELECT"` branch to fire instead of moving when `opened_by_rmb`:
```cpp
} else if( action == "SELECT" ) {
    if( opened_by_rmb ) {
        // LMB fires the weapon / confirms the throw
        action_fire(); // call the same function the FIRE action branch calls
        return;        // action_fire either fires (exits run()) or stays open; let it decide
    }
    // Normal targeting UI: move cursor to mouse position
    if( const auto angle = ctxt.get_aim_angle_to_src( src ) ) {
        set_aim_angle( *angle );
    } else if( const auto mouse_pos = ctxt.get_coordinates( g->w_terrain ) ) {
        auto p = *mouse_pos;
        p.z() = you->bub_pos().z() + you->view_offset.z();
        set_cursor_pos( p );
        sync_aim_angle_from_dst();
    }
}
```
`action_fire()` is a private member of `target_ui` (or the inline function called from the FIRE branch of the dispatch — grep `"FIRE"` in `target_ui::run()` to find it). Call it directly without re-dispatch. If it is not a named method but is inline code, extract it into a private helper `do_fire()` and call from both the FIRE branch and this SELECT branch.

---

### Step 11 — Dynamic shrinking cone while holding aim (verification only) ✅

**No new code required.** The auto-aim calls added in Step 10H (`action_aim()` inside the TIMEOUT handler) already reduce `predicted_recoil` each tick via the existing `do_aim()` mechanism. `calc_spread_half_angle()` from Step 7D reads `predicted_recoil` on every redraw, and `draw_terrain_overlay` is called each TIMEOUT cycle (it is: `skip_redraw` remains false after `action_aim()`).

**Verification only**: confirm manually that holding RMB while wielding a gun shows the spread cone visibly narrowing over ~1.5 seconds as recoil decays.

---

### Step 12 — Throw arc: lengthening arc while holding aim (`src/ranged.cpp`, `src/cata_tiles.h`, `src/cata_tiles_anim.cpp`, `src/game.h`, `src/animation.cpp`) ✅

Adds hold-duration-based throw distance and a visible quadratic Bezier arc in screen space. The arc lengthens from 0 to `max_throw_range` over 1.5 seconds of holding aim.

**12A — Charge fields in `target_ui`** (`src/ranged.cpp`, private section):
```cpp
double throw_charge = 0.0;          ///< 0..1; grows over throw_charge_full_ms in Throw mode
Uint64 throw_charge_start_ms = 0;   ///< SDL_GetTicks64() at Throw UI open
static constexpr double throw_charge_full_ms = 1500.0; ///< ms for full charge
int max_throw_range = 0;            ///< `range` captured at Throw UI open
```
`Uint64` and `SDL_GetTicks()` are SDL3 types; available via `sdl_window_dims.h` (added in Step 10G). Add `#include "sdl_window_dims.h"` to `ranged.cpp` if not already transitively present.

**12B — Initialise on Throw UI open** (`src/ranged.cpp`, `run()` setup block after `sync_aim_angle_from_dst()`):
```cpp
if( mode == TargetMode::Throw ) {
    max_throw_range = range; // throw_range() already computed at UI entry
    throw_charge    = 0.0;
    throw_charge_start_ms = SDL_GetTicks();
}
```

**12C — Update charge + effective range in TIMEOUT** (`src/ranged.cpp`):
At the top of the TIMEOUT handler (before the RMB check from Step 10H):
```cpp
if( mode == TargetMode::Throw && max_throw_range > 0 ) {
    const auto elapsed_ms = static_cast<double>( SDL_GetTicks() - throw_charge_start_ms );
    throw_charge          = std::min( 1.0, elapsed_ms / throw_charge_full_ms );
    const auto eff_range  = std::max( 1, static_cast<int>(
        throw_charge * static_cast<double>( max_throw_range ) ) );
    if( eff_range != range ) {
        range = eff_range;
        set_aim_angle( aim_angle ); // re-run DDA at same angle but new range to update dst/traj
    }
}
```

**12D — Parabolic arc in `cata_tiles`** (`src/cata_tiles.h`, `src/cata_tiles_anim.cpp`):

Add private fields alongside `do_draw_aim_crosshair`:
```cpp
bool do_draw_throw_arc = false;
tripoint_bub_ms throw_arc_src;
tripoint_bub_ms throw_arc_dst;
float throw_arc_charge = 0.0f;
```

Declare in `src/cata_tiles.h` public section (alongside `init_draw_cursor`):
```cpp
auto init_draw_throw_arc( const tripoint_bub_ms &src,
                          const tripoint_bub_ms &dst,
                          float charge ) -> void;
auto void_throw_arc() -> void;
auto draw_throw_arc() -> void;
```

Implement in `src/cata_tiles_anim.cpp` after `draw_cursor()`:
```cpp
auto cata_tiles::init_draw_throw_arc( const tripoint_bub_ms &src,
                                       const tripoint_bub_ms &dst,
                                       float charge ) -> void
{
    throw_arc_src    = src;
    throw_arc_dst    = dst;
    throw_arc_charge = charge;
    do_draw_throw_arc = true;
}
auto cata_tiles::void_throw_arc() -> void { do_draw_throw_arc = false; }
auto cata_tiles::draw_throw_arc() -> void
{
    if( !do_draw_throw_arc ) { return; }
    do_draw_throw_arc = false;
    const auto p1   = player_to_screen( throw_arc_src.xy() );
    const auto p2   = player_to_screen( throw_arc_dst.xy() );
    const auto dist = std::hypot( float( p2.x - p1.x ), float( p2.y - p1.y ) );
    const auto arc_h = std::max( 8.0f, dist / 3.0f );
    // Bezier control point: midpoint lifted upward in screen space (smaller y = up)
    const SDL_FPoint mid{
        0.5f * ( float( p1.x ) + float( p2.x ) ),
        0.5f * ( float( p1.y ) + float( p2.y ) ) - arc_h
    };
    // Sample quadratic Bezier B(t) = (1-t)^2 P1 + 2(1-t)t mid + t^2 P2
    constexpr auto N = 24;
    std::array<SDL_FPoint, N> pts;
    for( auto i = 0; i < N; ++i ) {
        const auto t = float( i ) / float( N - 1 );
        const auto u = 1.0f - t;
        pts[i] = SDL_FPoint{
            u * u * float( p1.x ) + 2.0f * u * t * mid.x + t * t * float( p2.x ),
            u * u * float( p1.y ) + 2.0f * u * t * mid.y + t * t * float( p2.y )
        };
    }
    // Orange tinted by charge: dim when uncharged, bright at full charge
    const auto alpha = static_cast<Uint8>( 120 + static_cast<int>( 135.0f * throw_arc_charge ) );
    SDL_SetRenderDrawColor( renderer.get(), 255, 140, 0, alpha );
    SDL_RenderLines( renderer.get(), pts.data(), N );
}
```

**12E — `game` bridge** (`src/game.h` declaration, `src/animation.cpp` definition after `draw_aim_crosshair`):
```cpp
// game.h
auto draw_throw_arc( const tripoint_bub_ms &src,
                     const tripoint_bub_ms &dst,
                     float charge ) -> void;

// animation.cpp
auto game::draw_throw_arc( const tripoint_bub_ms &src,
                            const tripoint_bub_ms &dst,
                            float charge ) -> void
{
    if( !tilecontext ) { return; }
    tilecontext->init_draw_throw_arc( src, dst, charge );
}
```

**12F — Call from `draw_terrain_overlay`** (`src/ranged.cpp`, after the cone block from Step 7E):
```cpp
if( mode == TargetMode::Throw && dst != src ) {
    g->draw_throw_arc( src, dst, static_cast<float>( throw_charge ) );
}
```

---

### Step 13 — Pulsing ripple impact indicator for thrown items (`src/cata_tiles.h`, `src/cata_tiles_anim.cpp`, `src/game.h`, `src/animation.cpp`, `src/ranged.cpp`) ✅

Mark of the Ninja-style: a persistent solid center dot at the impact tile with 3 concentric rings expanding outward and fading — looping continuously while aim is active. For explosive items the rings expand to `explosion_radius` tiles, communicating blast area. For non-explosive items they expand to 0.5 tiles (just a landing pulse). Everything renders at FRAME RATE (not TIMEOUT rate) using `SDL_GetTicks()` inside the draw function; `do_draw_throw_impact` stays set until explicitly voided.

**13A — Private state in `cata_tiles`** (`src/cata_tiles.h`, private section alongside `do_draw_aim_crosshair`):
```cpp
bool do_draw_throw_impact = false;
tripoint_bub_ms throw_impact_dst;
float throw_impact_max_r_tiles = 0.5f; ///< blast radius in tiles; 0.5 for non-explosive
```

**13B — Declare in `cata_tiles.h` public section** (alongside `init_draw_cursor`):
```cpp
auto init_draw_throw_impact( const tripoint_bub_ms &dst,
                              float max_radius_tiles ) -> void;
auto void_throw_impact() -> void;
auto draw_throw_impact() -> void;
```

**13C — Implement in `src/cata_tiles_anim.cpp`** (after `draw_throw_arc()` from Step 12):
```cpp
auto cata_tiles::init_draw_throw_impact( const tripoint_bub_ms &dst,
                                          float max_radius_tiles ) -> void
{
    throw_impact_dst         = dst;
    throw_impact_max_r_tiles = max_radius_tiles;
    do_draw_throw_impact     = true;
}
auto cata_tiles::void_throw_impact() -> void { do_draw_throw_impact = false; }
auto cata_tiles::draw_throw_impact() -> void
{
    if( !do_draw_throw_impact ) { return; }
    const auto c      = player_to_screen( throw_impact_dst.xy() );
    const auto tile_w = static_cast<float>( tile_width );
    const auto max_r  = throw_impact_max_r_tiles * tile_w;
    const bool explosive = ( throw_impact_max_r_tiles > 0.6f ); // non-explosive = 0.5
    // Color: warm orange for throws, red-orange for explosives
    const auto r_ch = static_cast<Uint8>( explosive ? 60 : 130 );

    // Approximates a circle with N+1 SDL_FPoints (closed polygon).
    // lambda is defined inline to keep SDL calls inside cata_tiles.
    const auto draw_ring = [&]( float radius, Uint8 alpha ) {
        if( radius < 1.0f ) { return; }
        constexpr auto N = 28;
        std::array<SDL_FPoint, N + 1> pts;
        for( auto i = 0; i <= N; ++i ) {
            const auto ang = float( i ) * 2.0f * float( M_PI ) / float( N );
            pts[i] = { float( c.x ) + radius * std::cos( ang ),
                        float( c.y ) + radius * std::sin( ang ) };
        }
        SDL_SetRenderDrawColor( renderer.get(), 255, r_ch, 0, alpha );
        SDL_RenderLines( renderer.get(), pts.data(), N + 1 );
    };

    // Center dot: always-on, 4 px radius, full alpha
    draw_ring( 4.0f, 230 );

    // 3 staggered rings, period 1200 ms each, phase-shifted by 1/3
    constexpr auto period_ms = 1200.0f;
    constexpr auto n_rings   = 3;
    const auto t_now = static_cast<float>( SDL_GetTicks() );
    for( auto i = 0; i < n_rings; ++i ) {
        const auto offset = float( i ) / float( n_rings );
        const auto t = std::fmod( t_now / period_ms + offset, 1.0f );
        draw_ring( t * max_r,
                   static_cast<Uint8>( ( 1.0f - t ) * 200.0f ) );
    }
}
```
`SDL_GetTicks()` (SDL3) returns `Uint64`; casting to `float` is intentional (wraps every ~49 days, irrelevant for animation).

**13D — `game` bridge** (`src/game.h` declaration; `src/animation.cpp` definition after `draw_throw_arc`):
```cpp
// game.h
auto draw_throw_impact( const tripoint_bub_ms &dst, float max_radius_tiles ) -> void;
auto void_throw_impact() -> void;

// animation.cpp
auto game::draw_throw_impact( const tripoint_bub_ms &dst, float max_radius_tiles ) -> void
{
    if( !tilecontext ) { return; }
    tilecontext->init_draw_throw_impact( dst, max_radius_tiles );
}
auto game::void_throw_impact() -> void
{
    if( !tilecontext ) { return; }
    tilecontext->void_throw_impact();
}
```

**13E — Call from `draw_terrain_overlay`** (after throw-arc call from Step 12F; replaces old `draw_highlight` loop):
```cpp
if( mode == TargetMode::Throw && dst != src ) {
    const auto max_r_tiles = ( relevant && relevant->type->explosion.power > 0 )
        ? float( relevant->type->explosion.radius )
        : 0.5f;
    g->draw_throw_impact( dst, max_r_tiles );
}
```
`draw_terrain_overlay` is called each TIMEOUT tick and on cursor movement, so the impact position updates immediately as the player aims.

**13F — Void on targeting UI exit**: call `g->void_throw_impact()` at every exit point of `target_ui::run()` when `mode == TargetMode::Throw`. Do not call it in other modes.

**13G — Call `draw_throw_impact()` from the render path**: in `src/cata_tiles_anim.cpp` or wherever `draw_cursor()` is called as part of the frame render, add `draw_throw_impact()` immediately after (before or after `draw_throw_arc()` is fine). Unlike `draw_cursor`-style self-clearing calls, `draw_throw_impact()` does NOT clear `do_draw_throw_impact` — it persists until `void_throw_impact()`. Confirm the render-path call site by searching `draw_cursor()` invocations outside of `cata_tiles_anim.cpp` (likely in `sdl_render_frame.cpp` or `cata_tiles.cpp`).

## Critical files & anchors

| File | Symbol / region | Why it matters |
|---|---|---|
| `src/ballistics.cpp:415–457` | `target`/`trajectory` if-else block | Entire block deleted and replaced by float DDA in Step 6 |
| `src/ballistics.cpp:594–603` | `cur_missed_by` heuristic | Replaced by geometric sub-tile calculation in Step 6C |
| `src/ranged.cpp:3033` | `SELECT` branch in `handle_cursor_movement` | Exact replacement point for angle-based mouse input |
| `src/ranged.cpp:3649` | `draw_terrain_overlay` body | Insertion point for spread cone + crosshair in Step 7E |
| `src/sdl_window_dims.cpp:133` | After `get_coordinates` | Insertion point for `get_aim_angle_to_src`; has `tilecontext` access via `cata_tiles.h` include |
| `src/input.h:89` | `mouse_buttons` enum | Add `MOUSE_BUTTON_RIGHT_DOWN = 6` (Step 10A) |
| `src/input.cpp:424–428` | `keyname_to_keycode` map | Add `"MOUSE_RIGHT_DOWN"` mapping (Step 10B) |
| `src/input.cpp:468–478` | display-name chain | Add `MOUSE_BUTTON_RIGHT_DOWN` display name (Step 10B) |
| `src/sdl_input.cpp:649–660` | `SDL_EVENT_MOUSE_BUTTON_UP` block | Insert `SDL_EVENT_MOUSE_BUTTON_DOWN` case for RMB (Step 10C) |
| `src/action.h:172` | `ACTION_FIRE` | Insert `ACTION_AIM_HOLD` after (Step 10D) |
| `src/handle_action.cpp:2387` | `case ACTION_FIRE:` | Insert `case ACTION_AIM_HOLD:` handler (Step 10E) |
| `src/sdl_window_dims.h` / `.cpp` | `is_rmb_held()` | New free function (Step 10G) |
| `src/ranged.cpp` (target_ui) | `opened_by_rmb` field + TIMEOUT handler | RMB hold detection + auto-aim; LMB fires, RMB release cancels (Step 10H) |
| `data/raw/keybindings/keybindings.json:1149` | `SEC_SELECT` entry | Add `AIM_HOLD` entry after (Step 10F); no TARGET FIRE modification |
| `src/ranged.cpp` (target_ui) | `throw_charge`, `throw_charge_start_ms`, `max_throw_range` | Throw charge state (Step 12A–C) |
| `src/cata_tiles.h` / `src/cata_tiles_anim.cpp` | `init_draw_throw_arc` / `draw_throw_arc` | Quadratic Bezier arc rendering (Step 12D) |
| `src/game.h` / `src/animation.cpp` | `game::draw_throw_arc` | Bridge into cata_tiles for throw arc (Step 12E) |
| `src/cata_tiles.h` / `src/cata_tiles_anim.cpp` | `do_draw_throw_impact` + `draw_throw_impact()` | Pulsing ripple center dot + expanding rings animation (Step 13A–C) |
| `src/game.h` / `src/animation.cpp` | `game::draw_throw_impact` / `void_throw_impact` | Bridge to cata_tiles for impact indicator (Step 13D) |
| `src/ranged.cpp` `draw_terrain_overlay` | After throw arc call | `g->draw_throw_impact(dst, max_r_tiles)` + `void` on exit (Step 13E–F) |

---

## Verification

### Build
```sh
cmake --build --preset osx-arm-slim \
    --target cataclysm-bn-tiles cata_test-tiles &
# poll to completion; never kill mid-run
```

### Unit test
```sh
./out/build/osx-arm-slim/tests/cata_test-tiles "[ranged]"
```
Must pass `ray_cast_angle DDA accuracy` with all three sections green.

### Manual smoke test

Load a save, wield a firearm, press `f` to enter targeting:

1. **Angle tracking**: Move mouse smoothly 360° — the aim line (tile trail sprites) sweeps continuously; no tile-jump artefacts at any range.
2. **Spread cone**: Aim at range ≥ 10 with a shotgun or after firing several shots (high recoil): two dimmer edge rays flank the main line, widening as recoil increases.
3. **Pixel crosshair**: A small orange `+` crosshair tracks the raw mouse pixel ahead of the tile-snapped cursor sprite. Disappears on leaving Fire mode.
4. **Keyboard 1° rotation**: Numpad 6 (east/right) rotates the aim line clockwise by 1°; numpad 4 (west/left) counterclockwise. After 360 presses of one direction, the line returns to its original heading.
5. **NEXT_TARGET**: Tab cycles to next visible enemy and the aim angle smoothly snaps to their position (no tile snap pop).
6. **Fire**: Shoot and confirm bullet lands in aimed direction. Check `debug.log` for no assertion failures or `debugmsg` spam.
7. **Curses build** (if available): targeting still works; the angle falls back to tile-snap (no crosshair, no cone); no compile error.
8. **RMB hold-to-aim**: Press and hold RMB while wielding a gun — the targeting UI opens immediately on press. The spread cone is visible and narrows while holding. Click LMB — the shot fires. Alternatively, release RMB — the aim mode cancels without firing and the UI closes. Repeat with no gun equipped: RMB press does nothing, RMB release still examines/closes doors normally.
9. **Non-combat RMB unchanged**: While unarmed, right-click an adjacent door — it closes (or opens) on release as before. Right-click self with items to pick up — pickup still works. No regressions.
10. **Throw arc**: Enter throw mode with any item. The aim cursor shows a curved orange arc from the player's tile toward the aimed tile; the arc lengthens as you hold aim, reaching the item's maximum throw range after ~1.5 seconds. Arc is absent in curses mode (no crash).
11. **Ripple impact indicator**: Enter throw mode with a grenade or molotov. While aiming, a pulsing orange dot appears at the aim tile with 3 concentric rings expanding outward and fading continuously (period ~1.2 s). Ring max radius matches the item's blast radius in tiles. For a non-explosive thrown item, rings expand to ~0.5 tiles (small landing pulse). Moving the aim cursor instantly moves the indicator. No crash in curses mode.

---

## Assumptions & contingencies

- **`calculate_dispersion` visibility**: It is `static` in `ranged.cpp`; `calc_spread_half_angle` is also in `ranged.cpp`, so no linkage issue.
- **`map& here` in `draw_terrain_overlay`**: If not already bound, add `const auto &here = get_map();` at the function top.
- **`hit_monsters` variable** (ballistics.cpp line 417): DO NOT remove. Declared at line 417, accumulated at line 653 (`push_back`), consumed at lines 751–755 to call `sp_defense` on each hit monster. The earlier draft incorrectly said to remove it; Step 6's rewrite already keeps the declaration intact.
- **`continue_line` removal**: After deleting lines 476–483, verify `continue_line` is not called elsewhere in `projectile_attack`. If it is (e.g. for a stream-type projectile branch), keep those uses.
- **`rmlui_layer::active()` availability** (Step 10C): This call is used in the existing `SDL_EVENT_MOUSE_BUTTON_DOWN` handling in `sdl_input.cpp` (~line 429) for RmlUI clicks. It is already in scope — no new include required.
- **`SDL_GetMouseState` in `sdl_window_dims.cpp`** (Step 10G): SDL3's `SDL_GetMouseState(float*, float*)` is available. The SDL3 return type is `SDL_MouseButtonFlags`; `SDL_BUTTON_RMASK` is the bit for RMB. In curses-mode SDL builds with no mouse input, `SDL_GetMouseState` returns 0, so `is_rmb_held()` correctly returns `false`.
- **`AIM_HOLD` missing from action string tables**: After adding `ACTION_AIM_HOLD` to `action.h`, search `src/action.cpp` for the array or switch that maps `action_id` → string name (used in keybinding display). Add `ACTION_AIM_HOLD → "AIM_HOLD"` there.
- **TIMEOUT cancel path** (Step 10H): Use `break` to exit the `while(true)` loop, then return the same cancel sentinel that ESC/QUIT returns in `target_ui::run()`. Grep `action == "QUIT"` or `action == "ESCAPE"` in `target_ui::run()` to find the cancel-return statement and replicate it. Do NOT set `action = "FIRE"` on RMB release.
- **`Uint64` type** (Step 12A): SDL3 uses `Uint64` (typedef for `uint64_t`) for `SDL_GetTicks()`. This type is in `<SDL3/SDL.h>`. If `sdl_window_dims.h` is included in `ranged.cpp` (which it must be for `is_rmb_held()`), SDL3 headers are transitively available. If not, add `#include <SDL3/SDL.h>` at the top of `ranged.cpp` guarded by `#ifdef TILES` or inside a `#if !defined(TILES)` stub block.
- **`relevant` in Throw mode** (Step 13E): Confirm that `target_ui::relevant` (the `item *` being aimed) is non-null and points to the thrown item when `mode == TargetMode::Throw`. If null, retrieve the thrown item from `you->get_wielded_item()` or the throw-activity actor instead.
- **`explosion.radius` / `explosion.power` fields** (Step 13E): Verify field names in CBN's `explosion_data` struct. Grep `explosion_handler.cpp` for `.power` and `.radius` access patterns if in doubt.
- **`player_to_screen` axis convention** (Step 12D): `player_to_screen` returns screen pixels where y increases downward (standard 2D convention). The arc midpoint lifts the control point upward by subtracting `arc_h` from y. Confirm this visually: the arc should bow away from the ground toward the top of the screen.
- **Keybinding file**: `data/raw/keybindings/keybindings.json` (not `data/raw/keybindings.json`). No new action key is added (Step 4 uses existing `NEXT_TARGET`/`PREV_TARGET` actions).
- **Isometric mode**: `get_aim_angle_to_src` uses the cartesian formula and returns `nullopt` in iso mode because `!tilecontext` is false but the formula is the same non-iso formula from `sdl_render_frame.cpp`. For iso builds, if angle tracking is visually off, the fallback to `get_coordinates` + `sync_aim_angle_from_dst` (curses path) is the contingency.
- **SDL include in `cata_tiles_anim.cpp`**: The file already links against SDL3 (used in zone overlay drawing); `SDL_RenderLines`/`SDL_SetRenderDrawColor` follow the same pattern as `physics_debug_draw.cpp`. If SDL headers are not directly included, add `#include <SDL3/SDL.h>` alongside the existing SDL includes in that file.
- **`predicted_recoil` cast**: `predicted_recoil` is `double`; `calculate_dispersion` takes `int at_recoil`. The cast `static_cast<int>(predicted_recoil)` matches existing usage in the file.
