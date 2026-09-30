# Driven-vehicle composite, continuous motion, pre-steer, seat lock, path overlay, co-op driving, Box2D overlay fix

## Context

Repo root: `/Users/nigel.fierens/dev-projects/Cataclysm-BN-Forked`. Step 0 records this vocabulary in `GLOSSARY.md`, and the plan uses it throughout:

- **Heading**: `face`.
- **Steer target**: `turn_dir`.
- **Pre-steer**: a steer target set while stopped.
- **Driven vehicle**: the vehicle whose controls the local avatar operates.
- **Partner-driven vehicle**: the vehicle the co-op partner drives.
- **Rolling**: `velocity != 0`.
- **Coasting**: rolling with no one driving.

Requested end state:

1. **Box2D debug overlay draws hitboxes again.** It is needed to evaluate everything below.
2. **Pre-steer.**
   - A stopped vehicle never rotates. Left/right queues the steer target in 15° steps, at most 3 steps (45°).
   - The steer target applies once the vehicle rolls, forward or reverse.
   - If the vehicle pulls away in reverse, the queued steer is mirrored the same way `vehicle::turn()` mirrors a press made while already reversing.
3. **Seat lock.**
   - The avatar aboard a rolling vehicle cannot walk to another tile of it.
   - Diving out via the existing prompt still works, at any nonzero speed.
   - NPCs are unchanged.
4. **Composite sprite with continuous motion.**
   - **Slots.** Up to two vehicles are drawn as one rotated composite each:
     - slot 0 is the driven vehicle, or else the rolling vehicle the avatar is aboard;
     - slot 1 is the partner-driven vehicle.
   - **Motion.** Each is drawn at a render-only pose that follows the committed Box2D pose:
     - single-player uses an exponential ease at the camera rate;
     - co-op uses tick-paced constant-speed segments, because co-op advances one world turn per tick: every 1000 ms when idle and every 16–250 ms while input is held.
   - **Camera.** While the avatar is aboard slot 0, the camera locks to the rendered seat.
   - **Riders** follow the composite.
   - **Vision and lighting** stay at the committed tiles.
   - Every other vehicle keeps per-tile drawing.
5. **Intended-path overlay** while driving:
   - drawn in all four aim-cone layers (fill, two vehicle-width edges bending toward the steer target, sight line, end pip);
   - clipped at the first impassable tile; the pip is red (`aim_hit_col`) when the path is clear and orange (`aim_blocked_col`) when clipped;
   - 2 turns long, clamped to 4–24 tiles;
   - replaces the `"cursor"` indicator behind `VEHICLE_DIR_INDICATOR`.
6. **Co-op.**
   - The host streams the pose of every vehicle in the sync area to the client each sync.
   - The client applies it to its copies and gets the same composite, motion, seat lock and path.
   - The client can drive: take/let go of control (starting engines on take), steer, and throttle are relayed to the host, where the client's proxy NPC operates the controls authoritatively.
   - The dead client→host `vehicle_state` (E1) path is removed.
7. **Tests travel with their change.** Every step ships its own test edits, so no step leaves test fixing for later.

Facts this rests on, confirmed in code:

- **Stopped-vehicle rotation today:** `map_vehicle.cpp:941` applies `turn_dir` every turn to every authority vehicle. `vehicle::thrust` (`vehicle_move.cpp:143-146`) resets `turn_dir = face.dir()` whenever it thrusts from a stop. `turn()` (:361-373) negates the delta when `velocity < 0 && !REVERSE_STEERING`.
- **In-vehicle walking:** `avatar_action::move` (`avatar_action.cpp:618-631`) treats a vehicle as moving only when `abs(velocity) > 100`. When it is, a step off the vehicle raises the dive prompt, and a step to another BOARDABLE tile of the same vehicle is allowed.
- **Aim cone:** it has no GPU pass. It is built from `lighting::overlay_*` quads (`src/lighting/solid_overlay.h`), with constants at `cata_tiles_anim.cpp:289-318`. The pip is drawn at :507-514.
- **Camera:** `camera_2d` eases at `camera_dbg::smooth_speed` (12/s) and snaps on jumps of more than 8 tiles. `game::draw_ter` (`game_misc.cpp:587-603`) updates it and calls `tilecontext->set_subtile_offset`, then `game::draw` refreshes `w_terrain`, which runs `cata_tiles::draw`. `cata_tiles` floors `center + subtile_off` (`cata_tiles.cpp:696-700`), so offsets of any size are safe.
- **Redraw pump:** a `draw()` that sets `creatures_anim_active_ = true` keeps frames coming. `refresh_anim_frame` resets that flag at the top of `draw()` (`cata_tiles_anim.cpp:105`).
- **Co-op loop** (`main.cpp:786-899`):
  - the host ticks after the input window (`COOP_INPUT_WINDOW_MIN_MS`/`MAX_MS` = 16/250, `coop_proto.h:75-77`), or every `IDLE_TICK_INTERVAL_MS = 1000` when idle;
  - it redraws every iteration (~4 ms);
  - `coop_server::coop_world_tick` runs `post_action_world_step()` (which includes `m.vehmove()`, `game_action.cpp:1998`), then executes client actions on the proxy (`coop_server.cpp:662`), then `build_and_send_sync()` (:780).
- **Co-op client:**
  - It is thin: no local sim (`game.cpp:3465-3470`).
  - Vehicles reach it only inside whole-submap snapshots in `sync.tiles`, sent when the host's submap origin changes (`coop_server.cpp:1427-1487`); `submap::swap` destroys and replaces the client's vehicle objects (`coop_client.cpp:773-785`).
  - Client movement is local prediction plus a `MOVE_*` relay (`handle_action.cpp:3025-3063`). The host applies it as a bare `proxy->setpos` (`coop_server.cpp:816-826`), so the proxy never boards vehicles.
  - There is no driving relay.
- **E1 is dead:**
  - the host fills `vehicle_id_map_` only through `register_vehicle_for_test` (`coop_server.h:127-132`), so the client's push (`coop_client.cpp:295-315`) never finds an id in real play;
  - the host ignores `face_x`/`face_y` (`coop_server.cpp:678-700`);
  - `coop_pkt::vehicle_sync = 21` has no users.
- **Host cruise for an NPC driver:** `vehicle::gain_moves` applies cruise thrust only when `player_in_control( g->u )` (`vehicle.cpp:3231,3254`, comment "cruise control TODO: enable for NPC?"). The random-skid check at `vehicle_move.cpp:1658-1675` already accepts any boarded character at CONTROLS.
- **Box2D overlay:**
  - The world target is PHYSICAL pixels (`render_state.cpp:165-202`, `SDL_GetWindowSizeInPixels`). `s_emo.tile_px` is the LOGICAL tile width (`sdl_render_frame.cpp:727-742`).
  - `debug_lines().record( …, wt->width(), wt->height(), … )` (`sdl_render_frame.cpp:1219-1221`) divides logical pixels by the physical size (`debug_line.vert.hlsl:35-38`). On a 2× display every line lands at half its position, crammed into the top-left quarter.
  - Every other world overlay passes `proj_w/proj_h` instead, e.g. `emitter_glow().record` at :1309-1315.
- **Physics:** bodies exist in normal play: terrain via `map::on_submap_loaded` → `phys_world->on_submap_loaded` (`map.cpp:684`), vehicles via `on_vehicle_added` (`map_vehicle.cpp:344`), creatures via `creature_tracker.cpp:101`.
- **Render precedents:**
  - the avatar-portrait route (`set_avatar_route` / `flush_avatar_sprites` / `composite_avatar_pass`, `sdl_render_frame.cpp:1046`);
  - per-part drawing in `vehicle_preview_window::display` (`vehicle_preview.cpp:225-288`);
  - `sprite.vert.hlsl:230` marks `dst_h > 1.5 tiles` as tall (lit from the base tile, and a shadow caster at `render_state.cpp:1005`);
  - `sprite_instance::cutout_pad2` is unread; `tests/sprite_instance_wire_test.cpp` pins only names and layout.
- **Part layouts:** part tiles are `bub_ms_location() + precalc[0]`.
  - (A) Authority vehicles after a physics step use `refresh_precalc(physics_angle)`, which rotates about mount (0,0) with `lround` (`vehicle_query.cpp:953-966`).
  - (B) Otherwise `rotate_to_world(pivot_rotation[0], pivot_anchor[0], m)`, which is exact at 0/90/180/270°.
- **Vehicle handles:** `vehicle_handle handle() const` (`vehicle.h:480`), `resolve_vehicle`, and `std::hash<vehicle_handle>` (`vehicle_handle.h:47-57`).

## Approach

**Test policy (every step):**
- A step is done only when its own test filter passes and a `"[vehicle]"` run (plus `"[coop]"` for Steps 5–6) shows exactly the Step 0 baseline failures.
- A grep of `tests/` found no assertion of any behaviour changed here except `tests/coop_vehicle_test.cpp`. That file tests E1, which Step 6 rewrites.
- If a step's run exposes another test asserting old behaviour, rewrite that assertion to the new rule in the same step's commit.
- Commit each step atomically (Conventional Commits, no body).

### Step 0 — Record keeping and baseline

- Copy this file verbatim to `plans/vehicle-drive-composite-path.md`.
- Under `## Stage E` in `plans/vehicle-continuous-program.md`, add: "Driven/partner-driven subset (composite, continuous pose, riders) is delivered by `plans/vehicle-drive-composite-path.md`; E for all vehicles remains open."
- Create the root `GLOSSARY.md`, which does not exist yet, using the format in `.agents/skills/domain-modeling/GLOSSARY-FORMAT.md`:
  ```md
  # Cataclysm: Bright Nights (Forked)

  Survival roguelike; this glossary pins player-facing game concepts whose names drift in code.

  ## Language

  ### Vehicles

  **Heading**:
  The direction a vehicle currently points.
  _Avoid_: facing, face, direction

  **Steer target**:
  The heading a vehicle will turn to once it moves.
  _Avoid_: turn_dir, intended heading, turn target

  **Pre-steer**:
  A steer target set while the vehicle is stopped; it changes nothing until the vehicle moves.
  _Avoid_: stationary turn

  **Driven vehicle**:
  The vehicle whose controls the avatar is operating, in person or by remote.
  _Avoid_: controlled vehicle, player vehicle

  **Partner-driven vehicle**:
  In co-op, the vehicle whose controls the other player is operating.
  _Avoid_: remote vehicle, proxy vehicle

  **Rolling**:
  A vehicle with any nonzero speed.
  _Avoid_: moving (ambiguous with grab/drag), in motion

  **Coasting**:
  A rolling vehicle that no one is driving.
  _Avoid_: freewheeling, drifting
  ```
- Baseline:
  1. Build (see Verification).
  2. Run `out/build/osx-arm-slim/tests/cata_test-tiles "[vehicle]" --order decl --rng-seed 1 --user-dir=/tmp/veh_base` and save the failing test names to `/tmp/veh_base_failures.txt`.
  3. Run `"[coop]" --order decl --rng-seed 1 --user-dir=/tmp/coop_base` and save its failing names to `/tmp/coop_base_failures.txt`.

  Known pre-existing failures include `vehicle_efficiency` and `vehicle_ramp_test_60`.

### Step 1 — Box2D debug overlay: logical projection (independent; do first)

`src/lighting/debug_line_pass.h`: replace the 8-parameter `record` with an options struct, per the repo rule for more than 3 parameters:
```cpp
struct debug_line_record_options {
    SDL_GPUCommandBuffer *cb = nullptr;
    SDL_GPUTexture *target = nullptr;
    std::uint32_t target_w = 0; ///< PHYSICAL texture px — viewport only
    std::uint32_t target_h = 0;
    std::uint32_t proj_w = 0;   ///< LOGICAL projection px — same space as tile_w and sprite dst
    std::uint32_t proj_h = 0;
    float cam_x = 0.0f;         ///< tile-space camera origin (pixel = (tile - cam) * tile_w)
    float cam_y = 0.0f;
    float tile_w = 32.0f;       ///< logical px per tile
    float tile_h = 32.0f;
};
auto record( const debug_line_record_options &opts ) -> void;
```
Update the doc comment above it to say NDC comes from `proj_*` and the viewport from `target_*`.

`src/lighting/debug_line_pass.cpp` `record` (:205-262):
- `FrameParams` becomes `{ cam_x, cam_y, tile_w, tile_h, proj_w, proj_h, pad0, pad1 }`, filled from `proj_w/proj_h`.
- The viewport stays `target_w/target_h`.
- Early-return also when `proj_w == 0 || proj_h == 0`.

`data/shaders/lighting/src/debug_line.vert.hlsl`: rename the cbuffer fields `target_w, target_h` to `proj_w, proj_h` (comment: "logical projection size (px), same space as tile_w") and use them in the NDC lines (:37-38).

`src/sdl_render_frame.cpp:1219-1221` (the only caller; `grep -rn "debug_lines().record" src` returns exactly it) becomes:
```cpp
rs.debug_lines().record( { .cb = ctx.cmd_buffer, .target = wt->texture(),
                           .target_w = wt->width(), .target_h = wt->height(),
                           .proj_w = static_cast<std::uint32_t>( proj_w ),
                           .proj_h = static_cast<std::uint32_t>( proj_h ),
                           .cam_x = cam_x, .cam_y = cam_y, .tile_w = tp, .tile_h = tp } );
```

`src/game_misc.cpp` `toggle_box2d_debug_draw` (:1805-1813): the enabled message becomes `string_format( "Box2D debug overlay enabled (%zu bodies, %zu terrain)", pw->world_body_count(), pw->terrain_body_count() )`. Both accessors already exist (`physics_world.h:116,121`). This tells "nothing to draw" apart from "not drawn".

No automated test is possible for a GPU projection; proof is Verification 4(a). Contingency:
- If lines are still absent after this step, add `dbg( DL::Info ) << "[box2d-debug] lines=" << rs.debug_lines().count();` inside the `if( rs.debug_lines().count() > 0 …)` guard and in `cata_tiles::draw` after `pw->draw_debug( dl )`, then relaunch.
  - lines = 0 means `b2World_Draw` emitted nothing; check `make_debug_draw` flags against the vendored Box2D's `b2DebugDraw` fields.
  - lines > 0 means the draw was dropped; check `debug_lines().ready()` and the `[lighting] DEGRADED:` log.
- Fix the cause, then remove the probe.

### Step 2 — Pre-steer (independent)

`src/vehicle_move.cpp`, anonymous namespace near the top (create it if absent):
```cpp
/// Most 15° steering steps a stopped vehicle can queue: the same 3 steps a moving
/// driver gets per turn ("At most 3 turns per turn" in pldrive).
constexpr auto max_presteer_steps = 3;
/// Heading change from `from` to `to` in whole 15° steps, signed, in (-12, 12].
auto presteer_steps( units::angle from, units::angle to ) -> int
{
    auto d = normalize( to - from );
    if( d > 180_degrees ) { d -= 360_degrees; }
    return static_cast<int>( std::lround( d / 15_degrees ) );
}
```

`vehicle::thrust` (:139):
- The first statement becomes `const bool started_stopped = !is_moving();`.
- In the stopped block (:143-146), delete `turn_dir = face.dir();` and keep `stop();`. The comment becomes `// Stopped: clear skid state, but keep any pre-steer (turn_dir) so it applies on the first move.`
- Directly after the velocity-update `if( … ) { stop(); } else { … }` block (:291-304), before the animal-harness loop, add:
  ```cpp
  // Pre-steer is a wheel position: pulling away in reverse turns the other way, exactly
  // as vehicle::turn() mirrors a press made while already reversing.
  if( started_stopped && velocity < 0 && !::get_option<bool>( "REVERSE_STEERING" ) ) {
      turn_dir = normalize( face.dir() - ( turn_dir - face.dir() ) );
  }
  ```
- Leave the pre-existing `REVERSE_STEERING` label/code mismatch untouched.

`vehicle::pldrive` (:1363):
- After `units::angle turn_delta = 15_degrees * p.x();`:
  ```cpp
  if( turn_delta != 0_degrees && !is_moving()
      && std::abs( presteer_steps( face.dir(), turn_dir + turn_delta ) ) > max_presteer_steps ) {
      turn_delta = 0_degrees; // wheel at lock: no steering, no move cost; throttle (p.y) still handled
  }
  ```
  Do not `return` here.
- After `turn( turn_delta );` (:1426):
  ```cpp
  if( !is_moving() ) {
      const auto steps = std::clamp( presteer_steps( face.dir(), turn_dir ), -max_presteer_steps, max_presteer_steps );
      turn_dir = normalize( face.dir() + steps * 15_degrees );
  }
  ```

`src/map_vehicle.cpp:941`: replace the line with the block below, and add one sentence to the comment above it: "A stopped vehicle keeps its heading and only holds its steer target."
```cpp
const auto steer_target = veh.turn_dir;
veh.set_facing_and_pivot(veh.is_moving() ? steer_target : veh.face.dir(), veh.pivot_point(), false);
veh.turn_dir = steer_target; // set_facing() overwrites turn_dir; keep the pre-steer
```

Tests in `tests/vehicle_test.cpp`, tagged `"[vehicle][steering]"`. Fixture:
- `clear_all_state(); build_test_map(ter_id("t_pavement"));`
- `const auto rev = override_option("REVERSE_STEERING", "false");`
- `car_test` via `add_vehicle(vproto_id("car_test"), tripoint_bub_ms(60,60,0), 0_degrees, 100, 0)`, then `REQUIRE(veh->box2d_position_authority)` and `veh->engine_on = true`.
- Avatar: `set_skill_level(skill_id("driving"), 10)`, and `set_moves(100)` before every `pldrive`.
- Compare angles as `lround(to_degrees(normalize(x)))`.

Cases:
1. `stationary_steering_queues_heading_without_rotating`: Right gives face 0, turn_dir 15. After 3× `vehmove()` they are still 0 and 15.
2. `stationary_presteer_stops_at_three_steps`: 5× Right gives turn_dir 45, face 0.
3. `pulling_away_forward_keeps_presteer`: Right, then `cruise_on = false; pldrive(you,{0,-1,0})`, gives `velocity > 0` and turn_dir 15.
4. `pulling_away_in_reverse_mirrors_presteer`: Right, then `cruise_on = false; pldrive(you,{0,1,0})`, gives `velocity < 0` and turn_dir 345.
5. `rolling_steering_turns_vehicle`: `tags.insert("IN_CONTROL_OVERRIDE"); velocity = cruise_velocity = 1000;`, then Right and `vehmove()`, gives face 15.

Cases 1–2 fail without the `map_vehicle.cpp` edit; 3–4 fail without the `thrust` edits.

### Step 3 — Seat lock while rolling (independent)

`src/avatar_action.cpp:618-631`:
- The condition becomes `src_veh != nullptr && src_veh->is_moving()`.
- The dive-prompt and other-vehicle branches keep their bodies.
- Replace the final `else if( !vp_dst.part_with_feature( "BOARDABLE", true ) ) { …unsafe… }` with:
  ```cpp
  } else {
      add_msg( m_info, _( "You can't move around inside a moving vehicle." ) );
      if( you.is_auto_moving() ) { you.clear_destination(); }
      return false;
  }
  ```
  This removes the "currently unsafe" string.

Tests in `tests/vehicle_test.cpp`, tagged `"[vehicle][seat_lock]"`:
- Fixture:
  - `clear_all_state(); build_test_map(t_pavement)`, then `car_test` at (60,60,0), `0_degrees`.
  - `seat = veh->bub_part_location( veh->get_avail_parts( "BOARDABLE" ).begin()->part_index() )`.
  - Find a neighbour `n` among the 8 around `seat` whose `veh_at(n)` is the same vehicle with `.part_with_feature("BOARDABLE", true)`; `REQUIRE` it exists.
  - `you.setpos(seat); here.board_vehicle(seat, &you);`
  - Re-read `seat = you.bub_pos()` and recompute `n` by the same offset, because boarding can shift the map.
- `avatar_cannot_walk_inside_rolling_vehicle`: `velocity = 50` (under the old 100 threshold, so this fails before the edit). Then `avatar_action::move(you, here, n - seat)` returns false and the avatar stays at `seat`, still `in_vehicle`.
- `avatar_can_walk_inside_stopped_vehicle`: `velocity = 0`, then the move returns true and the avatar is at `n`.

### Step 4 — Pure render geometry and motion (new files; prerequisite for Steps 7–9)

Create `src/vehicle_render_geometry.h`. It must not include `vehicle.h`. No equivalent exists.
```cpp
#pragma once
#include <array>
#include <cstdint>
class vehicle;

/// A continuous position in bubble-tile units; integer values are tile centres.
struct vehicle_render_point { float x = 0.0f; float y = 0.0f; };

/// Where a vehicle's mount grid sits this frame, continuously. Always anchored at mount (0,0).
struct vehicle_render_frame {
    float angle = 0.0f;    ///< radians, clockwise on screen (y down); 0 = mount +x points east. Same unit as sprite_instance::rotation.
    float origin_x = 0.0f; ///< continuous bubble-tile position of mount (0,0)'s tile centre
    float origin_y = 0.0f;
    int mount_min_x = 0;   ///< bounds over veh.all_standalone_parts()
    int mount_min_y = 0;
    int mount_max_x = 0;
    int mount_max_y = 0;
};
auto make_vehicle_render_frame( const vehicle &veh ) -> vehicle_render_frame;
auto vehicle_mount_to_bubble( const vehicle_render_frame &f, float mx, float my ) -> vehicle_render_point;

/// Render-only pose relative to the vehicle's committed tile anchor (tiles, radians).
struct vehicle_render_pose {
    float x = 0.0f;
    float y = 0.0f;
    float angle = 0.0f;
    auto operator==( const vehicle_render_pose & ) const -> bool = default; // *NOPAD*
};

enum class vehicle_motion_mode : std::uint8_t {
    ease,       ///< single-player: exponential follow at the camera rate
    tick_paced, ///< co-op: constant-speed segment per committed pose, lasting the last tick interval
};
struct vehicle_motion_state {
    vehicle_render_pose shown;    ///< pose drawn last update
    vehicle_render_pose seg_from; ///< tick_paced segment start
    vehicle_render_pose seg_to;   ///< last committed target seen
    double seg_start = 0.0;       ///< wall seconds
    double seg_len = 1.0;         ///< seconds
    double last_commit = 0.0;     ///< wall seconds of the previous target change
    double last_update = 0.0;     ///< wall seconds of the previous advance
};
struct vehicle_motion_options {
    vehicle_render_pose target;
    double now = 0.0;             ///< wall seconds
    vehicle_motion_mode mode = vehicle_motion_mode::ease;
    float rate = 12.0f;           ///< 1/s, ease mode (camera_dbg::smooth_speed)
    float snap_tiles = 64.0f;     ///< |target - shown| beyond this on either axis snaps (teleport)
};
auto reset_vehicle_motion( vehicle_motion_state &state, const vehicle_render_pose &pose, double now ) -> void;
/// Shift every stored pose by (dx, dy) tiles because the committed anchor moved by (-dx, -dy).
auto rebase_vehicle_motion( vehicle_motion_state &state, float dx, float dy ) -> void;
/// Advance to `opts.now`; returns the pose to draw (also stored in state.shown).
auto advance_vehicle_motion( vehicle_motion_state &state, const vehicle_motion_options &opts ) -> vehicle_render_pose;

struct vehicle_path_options {
    vehicle_render_frame frame;
    float steer_angle = 0.0f; ///< radians, frame.angle convention (units::to_radians( veh.turn_dir ))
    int velocity = 0;         ///< cm/s; < 0 = reversing: path leaves the rear edge, travelling backwards
};
struct vehicle_path_band {
    static constexpr int samples = 24;
    std::array<vehicle_render_point, samples> centre;
    std::array<vehicle_render_point, samples> left;  ///< centre - normal * width/2
    std::array<vehicle_render_point, samples> right; ///< centre + normal * width/2, normal = (-tangent.y, tangent.x)
    float width = 0.0f;                              ///< tiles = mount_max_y - mount_min_y + 1
};
auto make_vehicle_path_band( const vehicle_path_options &opts ) -> vehicle_path_band;
```

`src/vehicle_render_geometry.cpp` includes `vehicle.h`, `vpart_position.h` and `units_utility.h`.

`make_vehicle_render_frame`:
- Bounds come from `cpart(i).mount` over `all_standalone_parts()`; if there are none, return a default frame.
- **Branch A:** `box2d_position_authority`, and every standalone part's `precalc[0] == { lround(mx*c - my*s), lround(mx*s + my*c) }` with `c,s` from `physics_angle`. Set `angle = physics_angle` and `origin = bub_ms_location().xy() + (render_offset_x, render_offset_y)`.
- **Branch B:** otherwise. `a = to_radians(pivot_rotation[0])`; `origin = bub_ms_location().xy() + R(a)·(−pivot_anchor[0].xy)`.
- `R = [c −s; s c]`. `vehicle_mount_to_bubble` returns `origin + R(angle)·m`.

`reset_vehicle_motion` sets `shown = seg_from = seg_to = pose`, `seg_start = last_commit = last_update = now`, and `seg_len = 1.0`.

`rebase_vehicle_motion` adds `(dx, dy)` to `shown`, `seg_from` and `seg_to`.

`advance_vehicle_motion`:
- **Snap.** If `|target.x − shown.x| > snap_tiles || |target.y − shown.y| > snap_tiles`, call `reset_vehicle_motion(state, target, now)` and return.
- **`ease` mode.**
  - `dt = clamp(now − last_update, 0, 0.1)` and `t = 1 − exp(−rate·dt)`.
  - Ease x and y. For the angle, add `wrap(target.angle − shown.angle)·t`, where `wrap` maps to (−π, π].
  - Tail-snap each axis within 0.01 and the angle within 0.001.
- **`tick_paced` mode.**
  - A new commit is `|target.x − seg_to.x| > 1e-4 || |target.y − seg_to.y| > 1e-4 || |wrap(target.angle − seg_to.angle)| > 1e-5`. On a new commit:
    - `seg_from = shown`, `seg_to = target`;
    - `seg_len = clamp(now − last_commit, 0.016, 1.0)`;
    - `seg_start = now`, `last_commit = now`.
  - Then `f = clamp((now − seg_start)/seg_len, 0, 1)`; `shown = seg_from + (seg_to − seg_from)·f`, with the angle along the shortest arc.
- **Finally** set `last_update = now` and return `shown`.

`make_vehicle_path_band`:
- Setup:
  - `s = velocity < 0 ? −1 : 1`, `a = frame.angle`, `Δ = wrap(steer_angle − a)`.
  - `S = vehicle_mount_to_bubble(frame, s > 0 ? max_x + 0.5 : min_x − 0.5, (min_y + max_y)/2)`.
  - `d0 = s·(cos a, sin a)`, `d1 = s·(cos(a+Δ), sin(a+Δ))`.
- Length: `L = clamp(path_lookahead_turns·|velocity| / vehicles::cmps_per_tile, path_min_tiles, path_max_tiles)`, using anonymous-namespace constants `path_lookahead_turns = 2.0f`, `path_min_tiles = 4.0f`, `path_max_tiles = 24.0f`.
- Curve: quadratic Bézier `S → C = S + d0·L/2 → E = C + d1·L/2`, sampled at `t = i/(samples−1)`. The tangent is `2(1−t)(C−S) + 2t(E−C)`, normalised.

New `tests/vehicle_render_geometry_test.cpp`, tagged `"[vehicle][render]"`:
- `vehicle_render_frame_matches_part_tiles`:
  - Headings `0/90/180/270_degrees`: `clear_all_state(); build_test_map(t_pavement);` then `car_test` at (60,60,0).
  - (a) Right after spawn (branch B) and (b) after one `vehmove()` at velocity 0 (branch A): every standalone part has `lround(vehicle_mount_to_bubble(frame, mount)) == bub_part_location(part).xy()` exactly.
  - `frame.angle ≡ to_radians(face.dir())` mod 2π within 1e-3.
- `vehicle_motion_ease`, starting from `reset(state, {0,0,0}, 0.0)`:
  - `advance({ .target = {1,0,0}, .now = 0.05 })` gives `0 < x < 1`.
  - Target `{70,0,0}` gives exactly 70 (snap).
  - Reset with angle 6.109, then target angle 0.1745 at +0.05 s: the wrapped result moved forward through 0.
  - Target `{0.005,0,0}` gives exactly 0.005.
- `vehicle_motion_tick_paced`, starting from `reset(state, {0,0,0}, 0.0)`, with `mode = tick_paced` throughout:
  - target `{1,0,0}` at now 1.0 gives x 0; at 1.5, x 0.5 (±1e-4); at 2.0, x 1.0;
  - target `{2,0,0}` at 2.25, then at 2.75, gives x 1.5 (±1e-4). The segment length is 1.0 because `2.25 − 1.0` clamps to 1.0.
- `vehicle_path_band_geometry`: hand-built frame with angle 0, origin (10,10), x∈[−1,2], y∈[−1,1]:
  - velocity 0, steer 0: `centre[0] == (12.5,10)` and `centre.back() == (16.5,10)`; `|left[i]−right[i]| == 3` (±1e-4).
  - steer +15°: `centre.back().y > 10`.
  - velocity −500: `centre[0].x == 8.5` and `centre.back().x < 8.5`.
  - velocity 1788 straight: `centre.back().x ≈ 32.5` (±0.05).

### Step 5 — Co-op client driving: host-side relays and proxy boarding (independent of rendering)

New `src/vehicle_driver.h` and `.cpp`. This is a new pure-function header, because `vehicle.h` has far more than 10 includers:
```cpp
#pragma once
class Character;
class vehicle;
/// The character operating this vehicle's controls: the avatar (in person or by remote),
/// else any boarded character at CONTROLS with controlling_vehicle set (the co-op proxy).
/// nullptr when nobody drives.
auto vehicle_driver( const vehicle &veh ) -> const Character *; // *NOPAD*
```
The implementation:
- returns `&get_avatar()` if `veh.player_in_control( get_avatar() )`;
- otherwise returns the first `psg = veh.get_passenger( p )` over `veh.boarded_parts()` with `psg != &get_avatar()` and `veh.player_in_control( *psg )`;
- otherwise nullptr.

`src/vehicle.cpp` `gain_moves` (:3231):
- `const bool pl_control = player_in_control( g->u );` becomes `const bool pl_control = vehicle_driver( *this ) != nullptr;`.
- Include `vehicle_driver.h`.
- Replace the comment `// cruise control TODO: enable for NPC?` (:3253) with `// cruise control: applies to whichever character drives (avatar or co-op proxy)`.

`src/coop_server.cpp` `execute_player_cmd` `K::move` (:816-826). The proxy must really board and unboard, or it can never be at the controls:
```cpp
case K::move: {
    const tripoint_abs_ms dest_abs = proxy->abs_pos() + cmd.delta;
    const tripoint_bub_ms dest = abs_to_map_local( g->m, dest_abs );
    if( !g->m.inbounds( dest ) ) { break; }
    if( proxy->in_vehicle ) { g->m.unboard_vehicle( proxy->bub_pos() ); }
    proxy->setpos( dest_abs );
    if( g->m.veh_at( dest ).part_with_feature( VPFLAG_BOARDABLE, true ) ) { g->m.board_vehicle( dest, proxy ); }
    break;
}
```
Keep the existing comment above it about setpos vs `move_to`. Unboarding first is required: writing the position of a boarded creature outside `commit_occupants` trips `Creature::check_position_write_owner`.

`execute_client_action` (:1049): add two keys after `"USE"`. Parse `ctx_json` exactly like `"MELEE"` (:1101-1108): `std::istringstream`, `JsonIn`, `JsonObject`, `allow_omitted_members`. Wrap each parse in `try { … } catch( const JsonError & ) { DebugLog( DL::Error, DC::Main ) << "[coop] <KEY>: bad ctx"; }` (coop-audit-checklist).

**`"VEH_CONTROL"`**, ctx `{"on":bool,"engine":bool}`:
```cpp
const optional_vpart_position vp = g->m.veh_at( proxy->bub_pos() );
if( !vp || !proxy->in_vehicle ) { proxy->controlling_vehicle = false; return; }
vehicle &veh = vp->vehicle();
const bool at_controls = veh.avail_part_with_feature( vp->part_index(), "CONTROLS", true ) >= 0;
proxy->controlling_vehicle = on && at_controls;
if( proxy->controlling_vehicle && engine && !veh.engine_on ) {
    int started = 0;
    for( size_t e = 0; e < veh.engines.size(); ++e ) {
        if( veh.is_engine_on( e ) && veh.start_engine( static_cast<int>( e ) ) ) { ++started; }
    }
    veh.engine_on = started > 0;
} else if( !engine && veh.engine_on && proxy->controlling_vehicle ) {
    veh.engine_on = false;
}
```
The engine loop mirrors `start_engines_activity_actor::finish` (`activity_actor_combat.cpp:931-942`).

Unverified: where single-player sets `engine_on = true` after that loop. Run `grep -rn "engine_on" src/activity_actor*.cpp src/vehicle_use.cpp src/vehicle.cpp`. If single-player sets it through a different statement, use that exact statement instead of `veh.engine_on = started > 0`.

**`"VEH_DRIVE"`**, ctx `{"x":int,"y":int,"z":int}`: if `veh_pointer_or_null( g->m.veh_at( proxy->bub_pos() ) )` is non-null and `player_in_control( *proxy )`, call `veh->pldrive( *proxy, tripoint_rel_veh{ x, y, z } )`. Otherwise ignore it.

`src/handle_action.cpp`, client relays inside `if( coop_client_ )` (:3028). `veh_ctrl` (:1826) and `controlled_vehicle` (:1821) are in scope.
- New first branch before `if( move_cmd.kind == player_cmd_kind::move )`:
  ```cpp
  if( veh_ctrl && move_cmd.kind == player_cmd_kind::move ) {
      const auto d = get_delta_from_movement_action( act, iso_rotate::no );
      coop_client_->queue_action( "VEH_DRIVE", string_format( R"({"x":%d,"y":%d,"z":0})", d.x(), d.y() ) );
  } else if( move_cmd.kind == player_cmd_kind::move ) {
  ```
  The existing body is unchanged.
- In the `ACTION_MOVE_UP || ACTION_MOVE_DOWN` branch (:3104), first check `controlled_vehicle != nullptr && controlled_vehicle->is_aircraft()`. If true, queue `VEH_DRIVE` with `z = act == ACTION_MOVE_UP ? 1 : -1`, `x = y = 0`. Otherwise keep the existing body.

`src/coop_client.{h,cpp}`, control-state edge relay, replacing the E1 push:
- Add `struct coop_control_state { bool controlling = false; bool engine_on = false; auto operator==( const coop_control_state & ) const -> bool = default; // *NOPAD* };` to `coop_client.h`.
- Add private members `std::optional<coop_control_state> last_control_state_;` and `auto current_control_state() const -> coop_control_state;`.
  - `controlling = g->u.controlling_vehicle && veh_at( g->u.bub_pos() )`.
  - `engine_on` = that vehicle's `engine_on`, or false if there is none.
- In `coop_world_tick`, before the pending-action send loop (:281): `const auto cur = current_control_state(); if( last_control_state_ && cur != *last_control_state_ ) { queue_action( "VEH_CONTROL", string_format( R"({"on":%s,"engine":%s})", cur.controlling ? "true" : "false", cur.engine_on ? "true" : "false" ) ); } last_control_state_ = cur;`
- At the end of `apply_sync`, after parsing: `last_control_state_ = current_control_state();`. The next tick therefore compares against the host-applied state, and only a local change made since then is relayed. A single tick of local revert before the host's echo is accepted.
- Remote control (`g->remoteveh()`) is not relayed. Other `use_controls` menu entries (horn, lights, autopilot) stay local-only on a co-op client, like every other non-relayed action today.

Tests in `tests/coop_vehicle_test.cpp`, tagged `"[coop][vehicle]"`. Keep its `coop_mode_guard` and `inproc_harness`.
- Shared setup:
  - `veh = g->m.add_vehicle(vproto_id("car_test"), abs_to_map_local(g->m, h.proxy->abs_pos()) + tripoint_rel_ms(3,0,0), 0_degrees, 100, 0)`;
  - `ctrl = veh->bub_part_location( veh->get_avail_parts( "CONTROLS" ).begin()->part_index() )`;
  - `h.proxy->setpos( ctrl ); g->m.board_vehicle( ctrl, h.proxy );`.
- `client_takes_control_via_relay`: `h.cli.queue_action("VEH_CONTROL", R"({"on":true,"engine":true})"); h.tick();` gives `veh->player_in_control( *h.proxy )` and `veh->engine_on`.
- `client_presteer_via_relay`: control as above, then `VEH_DRIVE {"x":1,"y":0,"z":0}` and one tick gives `lround(to_degrees(turn_dir)) == 15` and face 0.
- `proxy_driver_gets_cruise_control`: control as above, then `VEH_DRIVE {"x":0,"y":-1,"z":0}` and 3 ticks give `veh->velocity > 0`. This fails before the `gain_moves` edit, because cruise thrust never applies without the avatar driving.
- `proxy_move_boards_vehicle`: an unboarded proxy next to a BOARDABLE tile, then `execute_player_cmd(h.proxy, move toward that tile, 1)`, gives `h.proxy->in_vehicle`.

### Step 6 — Co-op vehicle pose stream host → client; E1 removal (needs Step 5 for partner detection)

New `src/coop_vehicle_sync.h` and `.cpp`:
```cpp
#pragma once
#include "coordinates.h"
#include <cstdint>
#include <vector>
class JsonIn;
class JsonOut;
class map;
class vehicle;

/// One vehicle's host-authoritative pose, sent host → client in every sync (`"vehicles"`).
struct coop_vehicle_pose {
    std::uint32_t vid = 0;
    tripoint_abs_ms anchor;     ///< vehicle::abs_ms_location()
    float frac_x = 0.0f;        ///< physics_pos − bub anchor (tiles); 0 without authority
    float frac_y = 0.0f;
    float angle = 0.0f;         ///< physics_angle (rad)
    int face_deg = 0;           ///< lround(to_degrees(face.dir()))
    int steer_deg = 0;          ///< lround(to_degrees(turn_dir))
    int velocity = 0;
    int cruise_velocity = 0;
    bool engine_on = false;
    bool authority = false;     ///< box2d_position_authority
    bool host_driving = false;  ///< host avatar drives it (client: partner-driven)
    auto operator==( const coop_vehicle_pose & ) const -> bool = default; // *NOPAD*
};
auto make_coop_vehicle_pose( const vehicle &veh, std::uint32_t vid, bool host_driving ) -> coop_vehicle_pose;
/// Writes the array value only; caller writes the member name.
auto write_coop_vehicle_poses( JsonOut &jout, const std::vector<coop_vehicle_pose> &poses ) -> void;
auto read_coop_vehicle_poses( JsonIn &jin ) -> std::vector<coop_vehicle_pose>;
/// Apply `pose` to this side's copy of the vehicle. Returns true when its part layout
/// rotated (caller then calls map::vehicle_footprint_changed once for it).
auto apply_coop_vehicle_pose( map &here, vehicle &veh, const coop_vehicle_pose &pose ) -> bool;
/// Vehicle the co-op partner drives, as seen on this side; nullptr outside co-op or when none.
auto coop_partner_driven_vehicle() -> const vehicle *; // *NOPAD*
```

JSON keys per element: `vid, ax, ay, az, fx, fy, ang, face, steer, vel, cruise, eng, auth, hd`. `read` uses `JsonArray`/`JsonObject` with `allow_omitted_members` and defaults equal to the struct defaults.

`apply_coop_vehicle_pose`:
1. `delta = pose.anchor − veh.abs_ms_location()`; if non-zero, call `here.displace_vehicle( veh, tripoint_rel_ms( delta.raw() ) )`.
2. Record `rotated = veh.face.dir() != units::from_degrees( pose.face_deg ) || veh.physics_angle != pose.angle`.
3. `veh.box2d_position_authority = pose.authority`.
4. **If `authority`:**
   - `physics_pos = { bub.x + frac_x, bub.y + frac_y }`;
   - `physics_angle = angle`;
   - `render_offset_x/y = frac_x/frac_y`;
   - `veh.refresh_precalc( angle )`;
   - `veh.face.init( units::from_degrees( pose.face_deg ) )`.
5. **Else:** `render_offset_x = render_offset_y = 0`, then `veh.set_facing_and_pivot( units::from_degrees( pose.face_deg ), veh.pivot_point(), true )`.
6. Set `turn_dir`, `velocity`, `cruise_velocity` and `engine_on` from the pose.
7. `veh.commit_occupants()`: the client avatar rides along before proxy reconciliation.
8. Return `rotated`.

`coop_partner_driven_vehicle`:
- **Host:** `proxy = g->critter_by_id<npc>( coop_session::get().proxy_npc_id )`. Return its `veh_at` vehicle if `player_in_control( *proxy )`.
- **Client:** `g->coop_client_ ? g->coop_client_->host_driven_vehicle() : nullptr`.
- **Otherwise:** nullptr.

Host (`src/coop_server.{h,cpp}`):
- Replace `vehicle_id_map_` / `vehicle_id_map_rev_` (`coop_server.h:271-273`) with `std::unordered_map<vehicle_handle, std::uint32_t> vehicle_ids_;`. Keep `next_vehicle_id_`.
- Add a public `auto vehicle_id_for( const vehicle &veh ) -> std::uint32_t;` that assigns on first use.
- In `build_and_send_sync`, directly after the `"monsters"` array (:1519):
  1. `std::erase_if( vehicle_ids_, []( const auto &kv ) { return resolve_vehicle( kv.first ) == nullptr; } );`
  2. Collect poses for each `w.v` of `g->m.get_vehicles()` where either `project_to<coords::sm>( w.v->abs_ms_location() )` is within ±2 x/y of `abs_sub` on the same z (the same 5×5 area as `tiles`), or the vehicle is the one at the proxy's position. Each pose is `make_coop_vehicle_pose( *w.v, vehicle_id_for( *w.v ), w.v->player_in_control( g->u ) )`.
  3. `jout.member( "vehicles" ); write_coop_vehicle_poses( jout, poses );`
  4. The member comes after `"tiles"`, so the client applies snapshots first.
- Session reset (:1800-1804, :1819-1821): clear `vehicle_ids_`, set `next_vehicle_id_ = 1`, and delete the `pending_veh_*` and `client_known_vehicles_` lines.

Client (`src/coop_client.{h,cpp}`):
- Private members:
  - `std::unordered_map<std::uint32_t, vehicle_handle> coop_vehicle_handles_;`
  - `vehicle_handle host_driven_vehicle_;`
  - `auto resolve_coop_vehicle( const coop_vehicle_pose &pose ) -> vehicle *;`
- Public: `auto host_driven_vehicle() const -> const vehicle *; // *NOPAD*` which returns `resolve_vehicle( host_driven_vehicle_ )`.
- `resolve_coop_vehicle`:
  1. Use the mapped handle if it resolves.
  2. Otherwise, a full tile sync replaced the objects: find the vehicle in `get_map().get_vehicles()` whose `abs_ms_location() == pose.anchor` and map its `handle()`.
  3. Otherwise erase the mapping and return nullptr.
- `apply_sync`: new branch `else if( key == "vehicles" )`:
  - Read the poses; reset `host_driven_vehicle_ = {}`.
  - For each pose, resolve it, `apply_coop_vehicle_pose`, and call `here.vehicle_footprint_changed( veh )` when it returns true. If `pose.host_driving`, set `host_driven_vehicle_ = veh.handle()`.
  - Afterwards erase `coop_vehicle_handles_` entries whose vid was absent.

Delete E1 (clean cutover):
- `coop_client.cpp:295-315` and `coop_client.h:113-116`;
- `coop_server.cpp` receiver branch `t == coop_pkt::vehicle_state` (:371-383) and the apply block (:678-700);
- `coop_server.h` `register_vehicle_for_test` (:125-132), `client_known_vehicles_` (:232), and `pending_veh_state_t` / `pending_veh_mtx_` / `pending_veh_state_` (:275-285);
- `coop_proto.h` enum members `vehicle_state = 42` and the unused `vehicle_sync = 21`.

`grep -rn "vehicle_state\|vehicle_sync\|register_vehicle_for_test\|client_known_vehicles_\|coop_vehicle_map" src tests` must then be empty.

Tests: rewrite `tests/coop_vehicle_test.cpp`. Delete its three E1 cases and `make_vehicle_state_json`; keep the Step 5 cases. Add:
- `vehicle_pose_json_round_trip`: write 2 poses with distinct values in every field, read them back, and check `==`.
- `apply_pose_moves_and_rotates_copy`:
  - Spawn `car_test` at (60,60,0), then apply a pose with anchor +3 x, `authority = true`, `frac = {0.25, 0}`, `angle = to_radians(15°)`, `face_deg = 15`, `steer_deg = 30`, `velocity = 500`, `engine_on = true`.
  - Expect `abs_ms_location` +3 x and `physics_angle` within 1e-6.
  - Every standalone part's `precalc[0]` must equal the `refresh_precalc` formula.
  - Expect `turn_dir` 30°, velocity 500, `engine_on`.
- `host_sync_flags_host_driven_vehicle`:
  - With the harness, spawn `car_test` under `g->u`, board it at the controls, and set `g->u.controlling_vehicle = true`.
  - After `h.srv.coop_world_tick(); h.srv.flush_send_queue_for_test();` and one client tick under `coop_mode_guard( client )`, `h.cli.host_driven_vehicle() == veh`. In-process, the client resolves by anchor to the same object.

### Step 7 — Render-state composite slots, pass and shader flag (prerequisite for Step 8)

`src/lighting/render_state.h`: add next to the avatar route (:188-210), with members next to `avatar_sprite_queue_` (:519-522) and `avatar_target_` (:547):
```cpp
    // ── Vehicle composite route (slot 0 = own vehicle, slot 1 = co-op partner's) ──
    // While a slot is routed, queue_tile_sprite redirects into that slot's queue, flattened
    // to plain unlit albedo; the composite quad is lit once in the world pass. Checked FIRST
    // in queue_tile_sprite (never enabled together with the other routes).
    static constexpr std::size_t vehicle_composite_slots = 2;
    void set_vehicle_route(int slot) noexcept { vehicle_route_ = slot; } // -1 = off
    bool vehicle_sprites_empty(std::size_t slot) const noexcept { return vehicle_sprite_queues_[slot].empty(); }
    // Drains WITHOUT clearing (same contract as flush_tile_sprites); clear_tile_queue() resets.
    void flush_vehicle_sprites(std::size_t slot, sprite_batcher& dst, SDL_GPUSampler* sampler);
    // Lazily allocates (first call) or resizes the slot's target to w×h LOGICAL px.
    // nullptr when w/h <= 0, the device is not ready, or allocation failed.
    ui_composite_target* vehicle_target(std::size_t slot, int w, int h);
    ui_composite_target* current_vehicle_target(std::size_t slot) noexcept { return vehicle_targets_[slot].get(); }
  private:
    int vehicle_route_ = -1;
    std::array<std::vector<tile_sprite_draw>, vehicle_composite_slots> vehicle_sprite_queues_;
    std::array<std::unique_ptr<ui_composite_target>, vehicle_composite_slots> vehicle_targets_;
```

`src/lighting/render_state.cpp`:
- `queue_tile_sprite` (:845): make this the first check. If `vehicle_route_ >= 0`:
  - copy `inst`;
  - zero `light_mode, light_mul, pad1, extrude_px, extrude_dark, extrude_lean, face_amt, cutout, cutout_pad0, cutout_pad1`;
  - keep `tint_*`, `flash_*`, `rotation`, `pad2`;
  - push to `vehicle_sprite_queues_[vehicle_route_]` and return.
- `flush_vehicle_sprites`: copy `flush_tile_sprites`' set-texture-dedupe loop over the slot's queue, with no clear. Return early on a null sampler.
- `clear_tile_queue` (:519): clear both slot queues.
- `vehicle_target(slot, w, h)`:
  - nullptr if `w <= 0 || h <= 0 || !device_.ready()`;
  - with no target yet, `make_unique` and `init(device_, w, h)`; on failure `reset()` and return nullptr;
  - otherwise `resize(w, h)`;
  - return `texture() ? ptr : nullptr`.
- `shutdown` (:302): reset both targets.
- `flush_shadow_casters` (:1004-1008): `if (s.inst.cutout_pad2 > 0.5f) { continue; } // Ground-plane composites (vehicles) are floor-level art, never casters.`

Shader flag:
- `src/lighting/sprite_batcher.h` (:129-133): document `cutout_pad2` as "Ground-plane multi-tile flag: 1 = a flat composite (vehicle) larger than 1.5 tiles that must NOT take the tall-sprite base-tile lighting or cast a silhouette shadow. 0 for every other sprite."
- `data/shaders/lighting/src/sprite.vert.hlsl` :230 becomes `const bool is_tall = s.dst_h > tile_pixel_size * 1.5 && s.cutout_pad2 < 0.5;`, and update the struct comment at :20-21.
- `sprite.frag.hlsl` needs no edit.

`src/sdl_render_frame.cpp`:
- Declare `static auto composite_vehicle_pass( lighting::render_state &rs, lighting::frame_context &ctx ) -> void;` beside :126.
- Define it after `composite_avatar_pass`. For each slot:
  - skip if the target is null or texture-less, the slot queue is empty, or `!rs.gpu_sampler()`;
  - `begin_pass( ctx.cmd_buffer, vt->texture(), vt->width(), vt->height(), clear_transparent, vt->width(), vt->height() )`;
  - `set_scissor( nullptr )`;
  - `flush_vehicle_sprites( slot, … )`;
  - `end_pass()`.
- Call it between `composite_avatar_pass( rs, *ctx );` (:1992) and `render_world_pass_w`, with `dbg( DL::Debug ) << "[render] composite_vehicle_pass";`.

### Step 8 — Composite vehicles in `cata_tiles`, motion, riders, camera lock (needs Steps 4, 6, 7)

`src/cata_tiles.h`:
- Includes: `vehicle_render_geometry.h`, `vehicle_handle.h`, `<array>`, `<optional>`. Forward-declare `class vehicle; class vpart_position;` if absent.
- Public, next to `set_subtile_offset` (:1527):
  ```cpp
        /// Select and advance this frame's composite vehicles (slot 0 own, slot 1 co-op
        /// partner). Call once per frame before draw(). Returns the avatar's seat lag
        /// (rendered − committed, tiles) when the avatar rides slot 0, for the camera.
        auto prepare_vehicle_composites() -> std::optional<SDL_FPoint>;
  ```
- Private, beside `prefetch_valid_` (:1664):
  ```cpp
        struct composite_slot {
            const vehicle *veh = nullptr;  ///< drawn this frame; nullptr = unused. Valid only until draw() ends.
            vehicle_render_frame frame;    ///< eased frame this frame
            vehicle_handle tracked;        ///< vehicle `motion` belongs to
            tripoint_abs_ms ref;           ///< committed anchor at the last advance
            vehicle_motion_state motion;
            bool settled = true;
        };
        std::array<composite_slot, lighting::render_state::vehicle_composite_slots> composite_slots_;
  ```
- Declarations beside `draw_vpart` (:1218): `auto draw_vehicle_composite( std::size_t slot ) -> void;` and `auto vehicle_ride_offset( const vpart_position &vp, const tripoint_bub_ms &p ) const -> SDL_FPoint;`.
- Beside `draw_aim_cone` (:1292): `auto draw_vehicle_path( const vehicle &veh ) -> void;`.

`src/cata_tiles.cpp`, anonymous namespace (declarations end with `// *NOPAD*`):
- `driven_vehicle_for_render() -> const vehicle *`: `g->remoteveh()` if set; else the vehicle at `g->u.bub_pos()` if `g->u.controlling_vehicle` and `player_in_control( g->u )`; else nullptr. This mirrors `handle_action.cpp:1817-1825`.
- `own_composite_vehicle() -> const vehicle *`: `driven_vehicle_for_render()`, else the vehicle at the avatar's position if `g->u.in_vehicle` and `is_moving()`, else nullptr.

`prepare_vehicle_composites()`:
- `now = static_cast<double>( SDL_GetTicks() ) / 1000.0`.
- `mode = coop_session::get().is_coop() ? vehicle_motion_mode::tick_paced : vehicle_motion_mode::ease`; include `coop_session.h`, `coop_vehicle_sync.h` and `camera_debug.h`.
- Candidates:
  - slot 0 is `own_composite_vehicle()`;
  - slot 1 is `coop_partner_driven_vehicle()`, dropped if it equals slot 0;
  - both are null unless `!tile_iso && vpart_override.empty() && lighting::get_render_state().ready()`.
- For each slot `i` with candidate `v`:
  - `f = make_vehicle_render_frame( *v )`, `anchor = v->bub_ms_location()`, and `target = { f.origin_x − anchor.x(), f.origin_y − anchor.y(), f.angle }`.
  - If `slot.tracked != v->handle() || slot.ref.z() != v->abs_ms_location().z()`, call `reset_vehicle_motion( slot.motion, target, now )`. Otherwise:
    - `d = slot.ref − v->abs_ms_location()`;
    - `rebase_vehicle_motion( slot.motion, d.x(), d.y() )`.
  - `pose = advance_vehicle_motion( slot.motion, { .target = target, .now = now, .mode = mode, .rate = camera_dbg::smooth_speed, .snap_tiles = 64.0f } )`.
  - Store `tracked = handle()`, `ref = abs_ms_location()`, `settled = ( pose == target )`, comparing all three fields exactly.
  - `slot.frame = f` with `origin = anchor + (pose.x, pose.y)` and `angle = pose.angle`.
  - Target size: `w = (f.mount_max_x − f.mount_min_x + 3) * tile_width` and `h = (… y … + 3) * tile_height`. If `rs.vehicle_target( i, w, h )` is non-null, set `slot.veh = v`.
- For a slot with no candidate: `slot.veh = nullptr; slot.tracked = {};`.
- Return value: if `composite_slots_[0].veh` is non-null, `g->u.in_vehicle`, and `veh_at( g->u.bub_pos() )` is that vehicle, return `seat − g->u.bub_pos()` where `seat = vehicle_mount_to_bubble( slot0.frame, mount.x(), mount.y() )` and `mount = vp->mount()`. Otherwise return nullopt.

`draw()`:
- After `refresh_anim_frame()` (:571): `if( std::ranges::any_of( composite_slots_, []( const auto &s ) { return s.veh != nullptr && !s.settled; } ) ) { creatures_anim_active_ = true; }`.
- After the Pass 2 row loop (:1432): for each slot with `veh != nullptr && z == veh->bub_ms_location().z()`, call `draw_vehicle_composite( i )`.
- Just before `clear_tile_scissor()` (:1891): set `veh = nullptr` in every slot. This keeps the motion state but means a `draw()` without `prepare` draws per-tile.

`src/game_misc.cpp` `draw_ter` (:597-603), camera lock:
```cpp
main_camera_.set_follow_speed( camera_dbg::smooth_speed );
main_camera_.set_look_ahead( camera_dbg::look_ahead );
main_camera_.set_dead_zone( camera_dbg::dead_zone );
const auto seat_lag = tilecontext ? tilecontext->prepare_vehicle_composites() : std::nullopt;
if( seat_lag && !looking ) {
    // Riding a composite vehicle: the vehicle's own motion model drives the view. The
    // camera only eases the view offset (driving look-ahead) in the seat's frame.
    main_camera_.update( ( center.xy() - u.bub_pos().xy() ).raw(), false );
    tilecontext->set_subtile_offset( main_camera_.sub_x() + seat_lag->x, main_camera_.sub_y() + seat_lag->y );
} else {
    main_camera_.update( center.xy().raw(), looking );
    if( tilecontext ) { tilecontext->set_subtile_offset( main_camera_.sub_x(), main_camera_.sub_y() ); }
}
```
Switching between the two frames is a jump of more than 8 tiles, so `camera_2d` snaps once. At that moment the lag is 0 (entering a still vehicle, or leaving a stopped one), so nothing moves on screen. `camera_2d` itself is unchanged.

`src/cata_tiles_draw_layers.cpp`, `draw_vpart` (:623): add a private helper `auto is_composite_vehicle( const vehicle &v ) const -> bool` that returns true if any slot's `veh == &v`.
- Visible branch, after the memorize/clear block (:667-671): `if( !overridden && is_composite_vehicle( veh ) ) { return true; }`.
- `invisible[0]` live branch, after its memorize (:733-735): `if( is_composite_vehicle( veh ) ) { return true; }`.
- The rope branch is unchanged.
- Composite tiles no longer feed sprite-alpha occluder capture, so they fall back to the TransBuf seed. This is accepted per Stage E decision 2 of `plans/vehicle-continuous-program.md`.

New `draw_vehicle_composite( slot )`, after `draw_vpart`:
- Return if `current_vehicle_target( slot )` is null or texture-less.
- `active_anim_xform_ = {};` then `rs.set_vehicle_route( static_cast<int>( slot ) );`.
- For each `p` in `veh.all_standalone_parts()`, following `vehicle_preview.cpp:252-270`:
  - `part_id_string( p, false, part_mod )`; `subtile = part_mod == 1 ? open_ : part_mod == 2 ? broken : 0`;
  - `rotation = lround(to_degrees(normalize(part_display_direction(p) − face.dir())))`;
  - `[bg, fg] = get_vpart_color( here.veh_at( bub_part_location( p ) ), here, bub_part_location( p ) )`;
  - pixel `tripoint_bub_ms( (mount.x − min_x + 1) * tile_width, (mount.y − min_y + 1) * tile_height, 0 )`;
  - `draw_from_id_string( { "vp_" + vp_id.str(), C_VEHICLE_PART, empty_string, subtile, rotation }, px, bg, fg, lit_level::BRIGHT, false, 0, /*as_independent_entity=*/true, height_3d )`;
  - if `part_with_feature( p, "CARGO", true ) >= 0` has items, also draw `{ ITEM_HIGHLIGHT, C_NONE, empty_string, 0, 0 }` at `px` (LIT, independent).
- `rs.set_vehicle_route( -1 );`.
- Queue the world quad with `rs.queue_tile_sprite( vt->texture(), si )`:
  - `c = vehicle_mount_to_bubble( frame, 0.5f*(min_x+max_x), 0.5f*(min_y+max_y) )`;
  - `base = player_to_screen( anchor.xy() )`;
  - `cx = base.x + (c.x − anchor.x() + 0.5f) * tile_width`, and likewise `cy`;
  - zero-initialised `si` with `dst = {cx − W/2, cy − H/2, W, H}`, `src = {0,0,1,1}`, `tint = 1`, `rotation = frame.angle`, `light_mode = static_cast<float>( sprite_light_mode::gpu_lit )`, `cutout_pad2 = 1.0f`.

New `vehicle_ride_offset( vp, p )`:
- If a slot's `veh == &vp.vehicle()`: `c = vehicle_mount_to_bubble( slot.frame, mount.x(), mount.y() )`; return `{ (c.x − p.x()) * tile_width, (c.y − p.y()) * tile_height }`.
- Otherwise return `{ render_offset_x * tile_width, render_offset_y * tile_height }`, today's behaviour.
- Replace both rider sites with `const auto ro = vehicle_ride_offset( *vp_ride, p ); active_anim_xform_.off_x += ro.x; active_anim_xform_.off_y += ro.y;`: :909-911 and :1257-1259.

No automated test; the proof is Verification 4(c)–(e) and 5.

### Step 9 — Intended-path overlay replaces the facing cursor (needs Steps 4, 8)

`src/cata_tiles_anim.cpp`: new `draw_vehicle_path( const vehicle &veh )` after `draw_aim_cone`, using the aim constants directly.
- Frame and band:
  - `f` = the frame of the slot holding `&veh` if any, else `make_vehicle_render_frame( veh )`;
  - `band = make_vehicle_path_band( { .frame = f, .steer_angle = units::to_radians( veh.turn_dir ), .velocity = veh.velocity } )`.
- **Clip.** `clip` is the first index `i ≥ 1` where any of `left[i]`, `centre[i]`, `right[i]`, rounded at `z = bub_ms_location().z()`, is a tile `t` with `here.inbounds(t) && here.impassable(t) && veh_pointer_or_null(here.veh_at(t)) != &veh`. `blocked` means a clip index was found; otherwise `clip = samples − 1`.
- **Screen mapping:** `px(p) = { base.x + (p.x − anchor.x() + 0.5f)*tile_width, base.y + (p.y − anchor.y() + 0.5f)*tile_height }`, with `base = player_to_screen( anchor.xy() )`.
- **Draw over `[0, clip]`:**
  1. Fill: per consecutive centre pair, `overlay_quad({ .centre = midpoint, .w = segment px, .h = band.width * tile_width, .rotation = atan2(dy,dx), .color = aim_fill_col })`.
  2. `overlay_polyline` over the left and right points (`aim_edge_col`, thickness 2).
  3. `overlay_polyline` over the centre points (`aim_sight_col`, thickness 2).
  4. The pip at `centre_px[clip]` exactly as :510-514: shade plate `aim_shade_col`, then `blocked ? aim_blocked_col : aim_hit_col`, size `max(4.0f, tile_width * 0.20f)`.

`src/cata_tiles.cpp:1766-1775`: replace the `"cursor"` block with:
```cpp
if( !tile_iso && get_option<bool>( "VEHICLE_DIR_INDICATOR" ) ) {
    if( const vehicle *veh = driven_vehicle_for_render() ) { draw_vehicle_path( *veh ); }
}
```
A coasting or partner-driven vehicle shows no path.

Also:
- Delete `game::get_veh_dir_indicator_location` (`game.h:309-316`, `game.cpp:2343-2357`). `grep -rn get_veh_dir_indicator_location src tests` must then be empty.
- `options_registration.cpp:655-656`: keep the id and the `true` default.
  - Label: `"Draw vehicle path indicator"`.
  - Description: `"If true, when driving a vehicle, draws its intended path: two lines as wide as the vehicle that bend toward the steering direction and stop at the first obstacle."`

## Critical files & anchors

|File|Symbol / region|Why|
|---|---|---|
|`src/sdl_render_frame.cpp`|debug-line record :1219, composite pass insertion :1992|Box2D projection fix; composite pass order|
|`src/map_vehicle.cpp`|`:941` authority readback `set_facing_and_pivot`|The single place a stopped vehicle rotates|
|`src/coop_server.cpp`|`K::move` :816, `execute_client_action` :1049, `build_and_send_sync` :1413-1574, E1 :371/:678|Proxy boarding, relays, pose stream, E1 removal|
|`src/coop_client.cpp`|`coop_world_tick` :281-315, `apply_sync` `"tiles"`/`"monsters"` :741-857|Control-edge relay, pose apply, E1 removal|
|`src/cata_tiles.cpp` / `src/game_misc.cpp`|`draw()` :571/:1432/:1766/:1891; `draw_ter` :597-603|Slot scheduling, path call site, camera lock|

## Verification

Prerequisites:
- Work from the repo root.
- Read `skill://cbn-omp-long-build-service`. Build in the background with a timeout of at least 1800 s and never kill it: `cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles`.
- `ls -lT out/build/osx-arm-slim/tests/cata_test-tiles out/build/osx-arm-slim/src/cataclysm-bn-tiles` must show both newer than the build start. Use these paths, not the stale repo-root copies.

1. Per step, run `out/build/osx-arm-slim/tests/cata_test-tiles "<filter>" --rng-seed 1 --user-dir=/tmp/vN`:
   - `"[steering]"` (Step 2), `"[seat_lock]"` (Step 3), `"[vehicle][render]"` (Step 4), `"[coop][vehicle]"` (Steps 5–6).
   - All pass, and each case called out as failing-before fails when its edit is reverted.
2. After every step, `"[vehicle]"` plus `"*sprite_instance*"` must equal `/tmp/veh_base_failures.txt`. After Steps 5–6, `"[coop]"` must equal `/tmp/coop_base_failures.txt`. Use `skill://cbn-ab-regression-check` for attribution.
3. Greps: after Step 6 `grep -rn "vehicle_state\|vehicle_sync\|register_vehicle_for_test\|client_known_vehicles_\|coop_vehicle_map" src tests` is empty; after Step 9 `grep -rn get_veh_dir_indicator_location src tests` is empty.
4. Single-player in game. Read `skill://cbn-launch-into-save-harness` and `skill://cbn-input-harness-pixel-proof` first. Launch `out/build/osx-arm-slim/src/cataclysm-bn-tiles` into a daytime debug world, spawn a car on pavement next to a wall, and take the controls. Capture with the `computer` tool; measure with PIL, numbers only.
   - (a) **Box2D overlay (Step 1):** open the debug menu and choose Box2D shapes.
     - The message reads `Box2D debug overlay enabled (N bodies, M terrain)` with N > 0.
     - The pixels changed by toggling span more than 50% of the map viewport width and height, i.e. they are not confined to the top-left quarter.
     - A wall's outline edges coincide with its tile edges to within 2 px.
   - (b) **Pre-steer:** stopped, press Right ×2. The vehicle-bbox diff is ≈ 0 (excluding the path band), and the path end moves more than 1 tile toward screen-right.
   - (c) **Rotation:** accelerate; at heading 15°/30° the part edges are rotated. Once settled (≥ 0.5 s after a turn), the Box2D vehicle outline from (a) lies on the composite's silhouette within 0.5 tile, and the driver sprite sits within ±0.5 tile of its seat.
   - (d) **Continuity:** during cruise, 5 frames 50 ms apart right after a turn. The vehicle-bbox centroid stays within 1 tile of screen centre in every frame, while the terrain offset changes monotonically.
   - (e) **Clipping:** driving at a wall within the path length gives an orange pip (R≈255 G≈119 B≈34) before the wall; open road gives a red pip (R≈255 G≈51 B≈51).
   - (f) **Seat lock:** let go while rolling and press toward a seat. The log shows "You can't move around inside a moving vehicle.", the avatar has not moved, the composite remains, and the path is gone.
   - (g) Turning `VEHICLE_DIR_INDICATOR` off removes the path. Stopping and stepping out returns the vehicle to per-tile drawing with no gap or duplicate.
5. Co-op two-instance session. Read `skill://cbn-drive-two-instance-coop` first, then set up host and client sandboxes per that skill. Spawn a car beside the host.
   - (a) **Client drives.** The client walks onto the driver seat and takes control. The host `debug.log` shows `[coop][action] VEH_CONTROL`, and both screens show the engine running.
   - (b) **Client pre-steer.** The client presses Right ×2 while stopped. On both screens the vehicle does not rotate and the client's path bends right.
   - (c) **Idle cadence.** The client throttles up and then stops pressing keys, so ticks come every 1000 ms. On the client, 10 frames 100 ms apart show the car's screen-space distance to the terrain changing by nearly equal steps (max/min step ratio < 2), not one jump per second. On the host, the same measurement on the partner-driven car in slot 1 gives the same result.
   - (d) **Seat lock on the client.** The host rides as a passenger. With the car rolling, the host presses toward another seat and gets the seat-lock message.
   - (e) **Clean shutdown.** Kill both instances and delete the sandboxes, as the skill requires.

## Assumptions & contingencies

- **Autodrive.** If `"[vehicle][autodrive]"` newly fails after Step 2 because it leaves at the wrong heading, delete the pre-steer clamp (`max_presteer_steps` and both `pldrive` insertions) and keep the rest.
- **Tileset variations.** Part variation seeds in the composite come from pixel coordinates, as in `vehicle_preview`, so a random-variant part may differ from its per-tile look. This is accepted.
- **Iso mode** keeps per-tile vehicles, no path, and no camera lock: all screen math here is linear.
- **Non-authority vehicles (rails).** Branch B places them by continuous rotation, while their tiles use the sheared discrete layout at 15°/30°/45°. The resulting sub-tile visual offset is accepted.
- **Client resolution.** A client that has not yet received the submap snapshot containing a streamed vehicle skips that vehicle until the next full tile sync, because there is no object to resolve by anchor.
- **`vehicle_footprint_changed`.** If the name does not mean "rebuild this vehicle's cache footprint" (`map.h:1166`; confirm in `map_vehicle.cpp`), call `here.reset_vehicle_cache()` once after the pose loop instead.
