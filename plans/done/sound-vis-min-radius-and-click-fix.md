# Sound-visualization: wavefront min radius + F4 sound-click priority fix

## Context

Two independent bugs in the debug sound-pulse wavefront VFX:

1. `sfx::emit_sound_pulse` for player footsteps (`src/game_movement.cpp:488`) always
   passes a fixed volume of `3.0f`. That volume drives the pulse's maximum radius via
   `std::clamp(volume, 1.f, 24.f)` in `src/sdl_render_frame.cpp` (two sites), so
   footstep pulses only ever reach a 3-tile radius before expiring — visibly tiny
   compared to melee (8–12), gunfire (20), and ballistics (10) pulses. Since these
   pulses are only rendered while the player is in `CMM_STEALTH` (or the F4 panel is
   open), footsteps are the *only* pulse most players will ever see in normal stealth
   play, so the smallness is the whole visible effect. Fix: raise the radius floor so
   footstep pulses are visibly readable, without changing louder pulses.

2. On the F4 dev panel, the "spawn sounds on click" test tool
   (`sdl_lighting_devui::place_test_sound()`) never fires. Confirmed live via
   `~/Library/Application Support/Cataclysm-BN/config/debug.log` (10:25–10:26 today):
   every left click logs `[light_vis] LEFT_CLICK: rmlui_capture=0 calling
   place_test_light()` but **zero** `[sound_vis] LEFT_CLICK` lines ever appear. In
   `src/sdl_input.cpp`, the light-placement block and the sound-placement block are
   two separate `if` statements gated on the identical condition
   (`!rmlui_capture && MOUSE_BUTTON_DOWN && SDL_BUTTON_LEFT`); the light block runs
   first and, whenever `place_test_light()` returns `true` (i.e. whenever
   `dev_test_lights::place_mode` — the "place test lights on click" checkbox — is
   also checked), it hits `continue` and the sound block below it never executes for
   that click. Because `place_mode` is currently `true` in the running session, every
   click is fully consumed by the light placer and the sound placer is starved. Fix:
   merge the two click handlers into one block that attempts both placers
   independently and consumes the click if either handled it, so the two debug tools
   no longer compete for the same click.

End state: footstep pulses read as a visible ring in stealth movement, and the F4
panel's sound-click debug tool fires regardless of whether the light-click tool is
also enabled.

## Approach

### Step 1 — Raise the sound-pulse minimum radius (independent of Step 2)

File: `src/sdl_render_frame.cpp` (anchor tag `FEA4`).

1. After `using namespace std::literals;` (currently line 41), add a file-scope
   constant:
   ```cpp
   // Sound-pulse wavefront: minimum reachable radius in tiles. Footstep pulses
   // (volume=3, see game_movement.cpp) would otherwise expire at a barely-visible
   // 3-tile radius; louder pulses (melee 8-12, gunfire 20, ballistics 10) already
   // exceed this floor and are unaffected.
   constexpr float k_min_sound_pulse_radius = 6.0f;
   ```
2. In `draw_lighting_overlays`, the pulse-expiry lambda currently reads:
   ```cpp
   const float max_r = std::clamp( p.volume, 1.f, 24.f );
   ```
   (inside the `std::erase_if` at the line matching `constexpr float speed = 9.0f;`
   → `auto &pulses = dev_test_lights::sound_pulses;` → `std::erase_if(...)`).
   Replace the `1.f` floor with `k_min_sound_pulse_radius`.
3. In `render_world_pass_w`, the per-pulse render loop (`for( const auto &p :
   dev_test_lights::sound_pulses )`) has the identical line:
   ```cpp
   const float max_r = std::clamp( p.volume, 1.f, 24.f );
   ```
   Replace the `1.f` floor with `k_min_sound_pulse_radius` here too. Both sites must
   use the same floor — if one is missed, expiry timing (site 2) and render radius
   (site 3) desync, so a pulse would render at radius 6 but expire as if its cap were
   3.

File: `src/lighting/dev_test_lights.h` (anchor tag `E387`), line 41: update the
`sound_pulse::volume` doc comment from
`// drives the maximum radius (clamped to [1, 24] tiles)` to
`// drives the maximum radius (clamped to [6, 24] tiles)` to keep the comment
truthful.

No other emitter needs a change: footsteps (3.0) is the only call site below the new
6.0 floor (melee 8/10/12, ranged 20, ballistics 10, F4-panel test spawn default 30 —
all `>= 6` already).

### Step 2 — Stop light-click from starving sound-click (independent of Step 1)

File: `src/sdl_input.cpp` (anchor tag `B938`), lines 438–460. Current code is two
back-to-back `if` blocks with identical guard conditions:

```cpp
        // Dev test-light placement: a world click (not over the RmlUi panel) while the
        // dev panel is open with place-mode on drops a static light. Was the ImGui mouse
        // path; consume the click so it doesn't also trigger a game action.
        if( !rmlui_capture && ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            ev.button.button == SDL_BUTTON_LEFT ) {
            dbg( DL::Info ) << "[light_vis] LEFT_CLICK: rmlui_capture=" << rmlui_capture
                            << " calling place_test_light()";
            if( sdl_lighting_devui::place_test_light() ) {
                d.needupdate = true;
                continue;
            }
        }
        // Dev test-sound placement: a world click while sound place-mode is on drops a
        // test sound at that location. Consume the click so it doesn't trigger a game action.
        if( !rmlui_capture && ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            ev.button.button == SDL_BUTTON_LEFT ) {
            dbg( DL::Info ) << "[sound_vis] LEFT_CLICK: rmlui_capture=" << rmlui_capture
                            << " calling place_test_sound()";
            if( sdl_lighting_devui::place_test_sound() ) {
                d.needupdate = true;
                continue;
            }
        }
```

`place_test_light()` returning `true` short-circuits via `continue` before the second
`if` is ever reached, so whenever "place test lights on click" is checked, "spawn
sounds on click" silently never fires — this is the exact bug reproduced in the log.

Replace both blocks with a single merged block that calls both placers
unconditionally (each already internally no-ops via its own checkbox/panel-visible
guard — `place_test_light()` returns `false` immediately unless `place_mode` is on;
`place_test_sound()` returns `false` immediately unless `sound_place_mode` is on) and
consumes the click if either placed something:

```cpp
        // Dev test-light/test-sound placement: a world click (not over the RmlUi
        // panel) while the dev panel is open drops a static light and/or a test
        // sound at that tile, independently, per their own checkboxes — was the
        // ImGui mouse path. Consume the click so it doesn't also trigger a game
        // action whenever either placer handled it.
        if( !rmlui_capture && ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            ev.button.button == SDL_BUTTON_LEFT ) {
            dbg( DL::Info ) << "[light_vis] LEFT_CLICK: rmlui_capture=" << rmlui_capture
                            << " calling place_test_light()";
            const bool placed_light = sdl_lighting_devui::place_test_light();
            dbg( DL::Info ) << "[sound_vis] LEFT_CLICK: rmlui_capture=" << rmlui_capture
                            << " calling place_test_sound()";
            const bool placed_sound = sdl_lighting_devui::place_test_sound();
            if( placed_light || placed_sound ) {
                d.needupdate = true;
                continue;
            }
        }
```

This is a straight `SWAP` of lines 438–460 in `src/sdl_input.cpp`. No other callers
of `place_test_light`/`place_test_sound` exist outside this file (both are declared
only in `src/sdl_lighting_devui.h` and defined in `src/sdl_lighting_devui.cpp`); no
signature changes, so no other callsite updates are needed.

## Critical files & anchors

- `src/sdl_render_frame.cpp#FEA4` — two `std::clamp( p.volume, 1.f, 24.f )` sites
  (`draw_lighting_overlays`'s `erase_if` lambda, and `render_world_pass_w`'s pulse
  render loop); both must move to the new `k_min_sound_pulse_radius` constant.
- `src/lighting/dev_test_lights.h#E387` line 41 — `sound_pulse::volume` doc comment,
  update the stated clamp range.
- `src/sdl_input.cpp#B938` lines 438–460 — the two competing click-consumer `if`
  blocks to merge.
- `src/game_movement.cpp:488` — `sfx::emit_sound_pulse( u.bub_pos(), 3.0f )`, the
  footstep call whose visible radius the Step 1 floor fixes; not itself edited.
- `src/sdl_lighting_devui.cpp` lines 823–850 — `place_test_light()` /
  `place_test_sound()` bodies, confirms both are safe to call unconditionally (each
  gates on its own checkbox/panel-visible state and returns `false` as a no-op).

## Verification

1. Build: `cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles
   cata_test-tiles` as a background job with a 1200s+ timeout per the repo's build
   rules (never a synchronous/short-timeout build); poll to completion, fix any
   compile errors surfaced (this touches only two `.cpp`/`.h` files with no signature
   changes, so a clean compile is expected).
2. Format before considering the change final: `cmake --build build --target format`
   (or the equivalent build dir for whichever preset is configured) — confirms no
   astyle diff on the touched lines.
3. Manual smoke test (both bugs share one play session):
   - Launch `cataclysm-bn-tiles`, load a save, open the F4 dev panel, go to the
     "Overlays" tab and enable the sound overlay so pulses render even outside
     stealth (`ACTION_DISPLAY_SOUND`/devui-open path already renders regardless per
     `sound_pulses_visible`).
   - On the Effects tab, check **both** "place test lights on click" and "spawn
     sounds on click", then left-click a world tile. Confirm in
     `~/Library/Application Support/Cataclysm-BN/config/debug.log` (tail the file
     after the click) that a `[sound_vis] pulse pushed, total_pulses=N` line now
     appears alongside the light placement — this is the exact log line that was
     previously never reached; its appearance proves Step 2's fix.
   - With "place test lights on click" left checked and only stealth movement
     (`CMM_STEALTH`) active outside the dev panel, take a few steps and confirm the
     cyan wavefront ring is now clearly visible at spawn-to-expiry (radius reaching
     6 tiles) rather than the previous barely-visible 3-tile pulse — visually
     confirms Step 1.
4. `cata_test-tiles` full run is not required for this change (no testable game logic
   changed — pure rendering constant and an SDL input dispatch merge); skip running
   the suite, per the task's "skip project-wide test suites" guidance, beyond the
   build-time compile check in step 1.

## Assumptions & contingencies

- **Min radius value = 6.0 tiles.** Doubles the current 3-tile footstep radius while
  staying below every other emitter's volume (melee 8+, ranged 20, ballistics 10),
  so only footsteps are affected and the existing volume-to-size hierarchy among
  louder sounds is preserved. If 6 still reads as too small once built and tested in
  Step 3's manual smoke test, bump `k_min_sound_pulse_radius` alone (single
  constant, both clamp sites read it) — no other code changes needed.
- **Both debug placers fire on the same click when both checkboxes are enabled.**
  Chosen over making the two checkboxes mutually exclusive, since they are
  independent debugging aids (test light vs. test sound) with no reason a single
  click can't exercise both; this is also the minimal change against current
  behavior (light placement keeps working exactly as before, sound placement is no
  longer starved).
- **`place_mode`'s current `true` state** (why the bug is visible in today's live
  session) is a pre-existing runtime toggle left on from earlier light-spawner
  testing, not itself a bug to fix — the code fix is the merge in Step 2, not
  resetting the checkbox default.
