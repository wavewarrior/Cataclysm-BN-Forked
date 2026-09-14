# Sound Debug Visualization Overlay Plan

**Status:** Complete  
**Completed:** 2026-07-13
**Scope:** Real-time debug overlay visualizing sound propagation, attenuation, occlusion, and acoustic properties on the game map.

---

## Context

The user wants a debug visual overlay that displays sound propagation and attenuation as heatmap-like visuals on the game map. This enables seeing sound waves, bounces, occlusion effects, and how different implemented audio features change sound behavior in the game environment. The overlay should be togglable like existing debug overlays (visibility, lighting, temperature, radiation).

CBN already has a mature debug overlay infrastructure: `ACTION_DISPLAY_*` actions toggle overlays, `game::display_overlay_state()` checks if active, and `cata_tiles.cpp` draws color blocks (`color_blocks`) and text labels (`overlay_strings`) per tile during map rendering. **Critically, `DEBUG_SHOW_SOUND` already exists as a TODO stub** (`src/debug_menu.cpp:160`, handler at `:1914`) — the original intent was a monster-sound overlay ('?' markers) that was broken under tiles mode. We repurpose and complete this stub with a comprehensive sound visualization system.

---

## Approach

### Step 1: Wire `DEBUG_SHOW_SOUND` to `ACTION_DISPLAY_SOUND`

**Files:** `src/action.h`, `src/action.cpp`, `src/handle_action.cpp`, `src/debug_menu.cpp`, `src/game.h`, `src/game.cpp`

**Background:** `DEBUG_SHOW_SOUND` exists in the debug menu enum (`src/debug_menu.cpp:160`) but the handler at line 1914 is a TODO stub that just redraws and waits for a key. We need to wire it to a proper overlay action.

**Concrete edits:**

1. **`src/action.h`** — Add `ACTION_DISPLAY_SOUND` to the `action_id` enum near other `ACTION_DISPLAY_*` entries (around line 347, after `ACTION_DISPLAY_OUTSIDE`).

2. **`src/action.cpp`** — Add `"debug_sound"` string mapping in the action name switch (around line 311, after `ACTION_DISPLAY_OUTSIDE`).

3. **`src/handle_action.cpp`** — Add handler case for `ACTION_DISPLAY_SOUND` in both the competitive-map-sharing guard section (around line 2877) and the execution section (around line 4073). Copy the pattern from `ACTION_DISPLAY_OUTSIDE`:
   ```cpp
   case ACTION_DISPLAY_SOUND:
       if( MAP_SHARING::isCompetitive() && !MAP_SHARING::isDebugger() ) { break; }
       g->display_sound();
       break;
   ```

4. **`src/debug_menu.cpp`** — Replace the TODO stub at line 1914 with proper overlay toggle:
   ```cpp
   case DEBUG_SHOW_SOUND:
       g->display_toggle_overlay( ACTION_DISPLAY_SOUND );
       break;
   ```

5. **`src/game.h`** — Add `void display_sound();` declaration near other `display_*` methods (around line 1195).

6. **`src/game.cpp`** — Add implementation:
   ```cpp
   void game::display_sound()
   {
       display_toggle_overlay( ACTION_DISPLAY_SOUND );
   }
   ```

**Pattern to copy:** `ACTION_DISPLAY_OUTSIDE` is the simplest, cleanest pattern — no submenu, no creature selection. Copy it exactly.

---

### Step 2: Sound Visualization Data Layer

**Files:** `src/sounds.h`, `src/sounds.cpp`

**Purpose:** Expose per-tile sound data for visualization. The overlay needs to know, for each visible tile:
- Sound intensity (volume reaching that tile from active sources)
- Occlusion loss (dB reduction from terrain/furniture between source and tile)
- Frequency filter cutoff (muffling effect)
- Sound category breakdown (for color-coding by type)

**Verified infrastructure:**
- `sounds_since_last_turn` (`src/sounds.cpp:169`) — `std::vector<std::pair<tripoint_bub_ms, sound_event>>` of active sound events.
- `sound_markers` (`src/sounds.cpp:171`) — `std::unordered_map<tripoint_bub_ms, sound_event>` of current sound markers.
- `sound_distance()` (`src/sounds.cpp:179-195`) — Computes modified Chebyshev distance with vertical penalties.
- `process_sound_markers()` (`src/sounds.cpp:439-622`) — Volume calculation: `heard_volume = (raw_volume - weather_vol) * volume_multiplier - distance_to_sound`.
- `draw_footsteps_frame()` (`src/cata_tiles_anim.cpp`) — Already renders footstep/above/below tile overlays from `sound_markers` positions.

**Concrete edits:**

1. **`src/sounds.h`** — Declare the visualization data structure and accessor:
   ```cpp
   /// Per-tile sound visualization data (for debug overlay).
   struct sound_vis_tile {
       float intensity = 0.0f;          ///< Normalized volume [0.0, 1.0] reaching this tile
       float occlusion_db = 0.0f;       ///< Transmission loss in dB
       float freq_cutoff_hz = 8000.0f;  ///< Low-pass filter cutoff (200-8000 Hz range)
       sounds::sound_t dominant_category = sounds::sound_t::background; ///< Loudest sound category at this tile
   };

   /// Compute sound visualization data for all tiles in the visible map area.
   /// Called once per frame when the sound overlay is active.
   /// @param viewer_pos  Position of the player/listener
   /// @return Map of tile position -> visualization data
   std::unordered_map<tripoint_bub_ms, sound_vis_tile>
   compute_sound_visualization( const tripoint_bub_ms &viewer_pos );
   ```

2. **`src/sounds.cpp`** — Implement `compute_sound_visualization()`:
   - Iterate `sounds_since_last_turn` (the active sound events).
   - For each sound event, compute contribution to each visible tile.
   - Reuse `compute_acoustic_path()` from the occlusion system (Phase 2.1 of the network-audio plan) if it exists; otherwise, use `sound_distance()` as a placeholder for intensity and zero occlusion.
   - Aggregate overlapping sounds at each tile (sum intensities, weighted average of occlusion).
   - Normalize intensity to [0.0, 1.0] range for color mapping.
   - **Performance:** Only compute for tiles in the current map view (the visible area). Skip invisible tiles. Cache results for the frame.

   ```cpp
   std::unordered_map<tripoint_bub_ms, sound_vis_tile>
   compute_sound_visualization( const tripoint_bub_ms &viewer_pos )
   {
       std::unordered_map<tripoint_bub_ms, sound_vis_tile> result;
       const float max_volume = 128.0f; // Reference volume for normalization

       for( const auto &[source_pos, sound_event] : sounds_since_last_turn ) {
           // Skip if source is not on current Z level
           if( source_pos.z() != viewer_pos.z() ) {
               continue;
           }

           // Compute contribution to nearby tiles (limited radius for performance)
           const int radius = std::min( sound_event.volume, 20 ); // Max 20 tile radius
           for( int dx = -radius; dx <= radius; dx++ ) {
               for( int dy = -radius; dy <= radius; dy++ ) {
                   const tripoint_bub_ms tile( source_pos.x() + dx, source_pos.y() + dy, source_pos.z() );
                   const int dist = sound_distance( source_pos, tile );
                   const int heard_vol = sound_event.volume - dist;
                   if( heard_vol <= 0 ) {
                       continue;
                   }

                   auto &[data] = result[tile]; // Default-constructs if not present
                   const float contrib = std::clamp( static_cast<float>( heard_vol ) / max_volume, 0.0f, 1.0f );
                   data.intensity = std::min( data.intensity + contrib, 1.0f );
                   data.dominant_category = sound_event.category;

                   // TODO: When compute_acoustic_path() exists, call it here
                   // const auto acoustic = compute_acoustic_path( source_pos, tile );
                   // data.occlusion_db = acoustic.transmission_loss_db;
                   // data.freq_cutoff_hz = acoustic.frequency_filter_cutoff;
               }
           }
       }
       return result;
   }
   ```

**Edge cases:**
- No active sounds: return empty map (overlay shows nothing).
- `compute_acoustic_path()` not yet implemented: fall back to `sound_distance()` for intensity, zero occlusion. The overlay still works.
- Multiple sounds at same tile: sum intensities (clamped to 1.0), keep highest-volume category.

---

### Step 3: Overlay Rendering in `cata_tiles.cpp`

**File:** `src/cata_tiles.cpp`

**Purpose:** Draw the sound heatmap overlay on the map, following the exact pattern of existing overlays (lighting, transparency, visibility).

**Verified infrastructure:**
- `color_block_overlay_container` is `std::pair<SDL_BlendMode, std::multimap<point, SDL_Color>>` (`src/cata_tiles.h:741`).
- `overlay_strings` is `std::multimap<point, formatted_text>` (`src/cata_tiles.cpp:123`).
- `player_to_screen()` converts bubble-ms coords to screen pixels.
- `draw_debug_tile` lambda (`src/cata_tiles.cpp:757`) is the template: color overlay + text label.
- Color blocks are rendered in `sdl_curses_draw.cpp:curses_drawwindow()` after `tilecontext->draw()` returns, via `geometry->rect()` for each entry.

**Concrete edits:**

Add a new overlay block in `cata_tiles.cpp` after the existing overlay blocks (after `ACTION_DISPLAY_OUTSIDE`, around line 825). The pattern mirrors `draw_debug_tile` (line 757):

```cpp
if( g->display_overlay_state( ACTION_DISPLAY_SOUND ) ) {
    const auto sound_data = compute_sound_visualization( g->u.bub_pos() );

    if( sound_data.empty() ) {
        continue; // No sounds to visualize
    }

    const auto it = sound_data.find( {temp_x, temp_y, center.z()} );
    if( it == sound_data.end() ) {
        continue; // No sound data for this tile
    }

    const auto &sv = it->second;

    // Color mapping: intensity drives hue (blue=cold/quiet → red=hot/loud)
    // Occlusion modulates saturation (desaturated = heavily occluded/muffled)
    SDL_Color block_color;
    const float intensity_clamped = std::clamp( sv.intensity, 0.0f, 1.0f );
    const float occlusion_factor = 1.0f - std::clamp( sv.occlusion_db / 60.0f, 0.0f, 1.0f );

    // Heatmap gradient: blue (quiet) → cyan → green → yellow → red (loud)
    if( intensity_clamped < 0.25f ) {
        block_color = { 0, static_cast<uint8_t>( intensity_clamped * 4.0f * 255 ), 255, 128 };
    } else if( intensity_clamped < 0.5f ) {
        block_color = { 0, 255, static_cast<uint8_t>( ( 1.0f - ( intensity_clamped - 0.25f ) * 4.0f ) * 255 ), 128 };
    } else if( intensity_clamped < 0.75f ) {
        block_color = { static_cast<uint8_t>( ( intensity_clamped - 0.5f ) * 4.0f * 255 ), 255, 0, 128 };
    } else {
        block_color = { 255, static_cast<uint8_t>( ( 1.0f - ( intensity_clamped - 0.75f ) * 4.0f ) * 255 ), 0, 128 };
    }

    // Desaturate based on occlusion (muffled sounds appear washed out)
    const float gray = block_color.r * 0.299f + block_color.g * 0.587f + block_color.b * 0.114f;
    block_color.r = static_cast<uint8_t>( gray + ( block_color.r - gray ) * occlusion_factor );
    block_color.g = static_cast<uint8_t>( gray + ( block_color.g - gray ) * occlusion_factor );
    block_color.b = static_cast<uint8_t>( gray + ( block_color.b - gray ) * occlusion_factor );

    color_blocks.first = SDL_BLENDMODE_BLEND;
    color_blocks.second.emplace( player_to_screen( point_bub_ms( temp_x, temp_y ) ), block_color );

    // Text overlay: show volume percentage and occlusion dB
    std::string label = string_format( "%d%%/%.0fdb",
        static_cast<int>( intensity_clamped * 100 ), sv.occlusion_db );
    overlay_strings.emplace(
        player_to_screen( point_bub_ms( temp_x, temp_y ) ) + quarter_tile,
        formatted_text( label, catacurses::black, direction::NORTH ) );
}
```

**Color scheme rationale (industry standard for audio heatmaps):**
- Blue → Cyan → Green → Yellow → Red maps to increasing intensity (borrowed from thermal imaging and Unreal Engine's attenuation visualization).
- Occlusion desaturates toward grayscale (muffled = washed out), following the visual metaphor used in acoustic simulation tools.
- Alpha 128 (50% transparent) so the map remains readable underneath.

---

### Step 4: Sound Wave Propagation Rays

**Files:** `src/sounds.cpp` (data), `src/cata_tiles.cpp` (rendering)

**Purpose:** Draw the acoustic raycast paths from sound sources to the listener, showing how sound propagates through the environment. This visualizes the "sound waves and bounces" the user requested.

**Concrete edits:**

1. **`src/sounds.cpp`** — Add ray data accessor:
   ```cpp
   /// Acoustic ray data for visualization.
   struct sound_vis_ray {
       tripoint_bub_ms source;
       tripoint_bub_ms target;
       std::vector<tripoint_bub_ms> path;     ///< Tiles along the ray
       float occlusion_db;
       sounds::sound_t category;
   };

   /// Compute acoustic rays from active sound sources to the listener.
   std::vector<sound_vis_ray> compute_sound_rays( const tripoint_bub_ms &listener_pos );
   ```

2. **`src/sounds.cpp`** — Implement `compute_sound_rays()`:
   - For each active sound source, cast rays toward the listener using `ray_cast_angle()` (`src/map.cpp:6702-6750`).
   - Collect tiles along each ray path.
   - Compute occlusion for each ray.
   - Return the ray data for rendering.

3. **`src/cata_tiles.cpp`** — After the tile overlay loop, draw rays as colored tile overlays along the ray path. Use the same `color_blocks` mechanism (not SDL_Renderer lines, since the renderer context may not be available at the overlay drawing point):
   ```cpp
   if( g->display_overlay_state( ACTION_DISPLAY_SOUND ) ) {
       const auto rays = compute_sound_rays( g->u.bub_pos() );
       // Rays are drawn as colored tile overlays along the path
       // This is handled in the main tile loop by checking if current tile is on a ray
   }
   ```

**Decision:** Use tile-based overlay (consistent with other overlays) rather than SDL_Renderer lines. Color tiles along the ray path with a thin line color. The ray tiles use a distinct color (cyan with high alpha) to differentiate from the heatmap.

---

### Step 5: Sound Source Markers

**File:** `src/cata_tiles.cpp`

**Purpose:** Mark active sound sources on the map with pulsing indicators. Extends the existing `draw_footsteps_frame()` pattern from `src/cata_tiles_anim.cpp`.

**Concrete edits:**

After the sound heatmap overlay, add source markers in the tile loop:

```cpp
if( g->display_overlay_state( ACTION_DISPLAY_SOUND ) ) {
    // Mark sound sources
    for( const auto &[pos, sound] : sounds_since_last_turn ) {
        if( pos.xy() == point_bub_ms( temp_x, temp_y ) && pos.z() == center.z() ) {
            // White marker with high alpha for source identification
            SDL_Color src_color = { 255, 255, 255, 200 };
            color_blocks.first = SDL_BLENDMODE_BLEND;
            color_blocks.second.emplace( player_to_screen( point_bub_ms( temp_x, temp_y ) ), src_color );

            // Label with sound description
            overlay_strings.emplace(
                player_to_screen( point_bub_ms( temp_x, temp_y ) ) + point( tile_width / 2, 0 ),
                formatted_text( sound.description, catacurses::white, direction::NORTH ) );
        }
    }
}
```

---

## Critical Files & Anchors

| File | Symbol/Region | Why It Matters |
|------|---------------|----------------|
| `src/action.h:303-351` | `action_id` enum | Add `ACTION_DISPLAY_SOUND` near `ACTION_DISPLAY_*` entries. |
| `src/action.cpp` | action name switch | String mapping for `"debug_sound"`. |
| `src/handle_action.cpp` | `ACTION_DISPLAY_*

---

## Implementation Notes (Actual)

### Commits

| Commit | Description |
|--------|-------------|
| `37079c13bc` | `feat(debug): add sound propagation visualization overlay` — 10 files, +496/-9 |
| `93da39127d` | `fix(sound): cache sound vis data before sounds_since_last_turn is cleared` — 1 file, +34/-25 |
| `7506a7c50d` | `feat(debug): add spawn sound at cursor debug action` — 1 file, +12/-0 |

### Key Deviations from Plan

1. **Timing fix (critical):** `sounds_since_last_turn` is cleared at end of `process_sound_markers()` every turn, *before* `cata_tiles::draw()` runs. Solution: snapshot visualization data into static cache (`_cached_sound_vis`, `_cached_sound_rays`) inside `process_sound_markers()` before the clear. `compute_sound_visualization()` and `compute_sound_rays()` now return the cached data. Cache cleared in `reset_sounds()`.

2. **Source markers simplified:** Instead of a separate `get_sound_events()` accessor (which would leak the file-local `sound_event` type), added `is_source` bool to `sound_vis_tile` and set it during snapshot when `dx==0 && dy==0`.

3. **Namespace qualification:** `sound_vis_tile` and `sound_vis_ray` live in `namespace sounds` — static cache variables and local uses in file-scope functions need `sounds::` prefix.

4. **Spawn sound action:** Added `DEBUG_SPAWN_SOUND` to the spawning submenu. Uses `g->look_around(LA_MODE_2D)` for tile selection (NOT `input_context` directly — `w_terrain` is private and `input_context` lacks the assumed registration methods). Emits volume-64 `activity` sound with `fire_gun` SFX.

### Testing Checklist

- [x] Build: clean compile on `osx-arm-slim`, zero warnings from our code
- [ ] In-game: toggle overlay via F8 → Show sound
- [ ] In-game: spawn sound via F8 → Spawning → Spawn sound at cursor
- [ ] In-game: verify heatmap colors propagate from source outward
- [ ] In-game: verify source tile shows white marker
- [ ] Performance: no measurable framerate drop with multiple active sounds

---

## F4 Spawner Rework — Animated Occlusion-Aware Wave (2026-07-13)

The per-turn heatmap snapshot (above) stays, but the F4 dev-panel "sound
spawner" was reworked from a one-frame static blob into a real-time animated
wave, fixing four user-reported bugs.

### Bugs fixed

1. **Click "did nothing":** the sound *was* spawned, but its only feedback was
   the offset one-frame blob — so it looked inert. The animated wave now gives
   immediate, persistent, correctly-placed feedback.
2. **One-tile offset:** the old blob drew via `cata_tiles::player_to_screen`,
   which differs by a tile from the GPU overlay mapping the lights use. The wave
   now draws in `draw_lighting_overlays` with the *same* `s_emo` mapping as the
   dev lights, so it lands exactly where the lights do.
3. **No real-time expansion:** replaced the static snapshot with a flood-filled
   reachable field animated by a wavefront radius that grows over wall-clock time
   (`speed = 9 tiles/s`), leading band bright, interior a dim trail, fading with
   age.
4. **Overlay text froze on toggle-off:** `rmlui_layer::prepare` early-returned
   without clearing compiled world-text geometry (`g_world_geom`), so
   `render_in_pass` kept drawing the last frame. Now `g_world_geom.clear()` runs
   before the empty early-return.

### Implementation

- `src/lighting/dev_test_lights.h` — `sound_pulse` / `sound_pulse_tile` structs,
  `pulse_now_s()` steady clock, `sound_pulses` vector.
- `src/sdl_lighting_devui.cpp` `place_test_sound()` — Dijkstra flood from the
  clicked tile, 8-connected, blocked by `light_transparency <=
  LIGHT_TRANSPARENCY_SOLID` (walls recorded but not propagated → shadows).
  Still queues a real `sounds::sound` for AI; dropped the immediate
  `process_sound_markers`/`process_sounds` that caused the confusing static blob.
- `src/sdl_render_frame.cpp` `draw_lighting_overlays()` — reveals field tiles up
  to the current wavefront radius each frame; prunes expired pulses. Gated inside
  the existing `g_dbg_lighting` block (on by default) for a valid `s_emo` mapping.
- `src/lighting/rmlui_layer.cpp` `prepare()` — the `g_world_geom.clear()` fix.

### Commits

| Commit | Description |
|--------|-------------|
| (this branch) | `fix(devui): clear stale world-text geometry when overlays toggle off` |
| (this branch) | `feat(devui): animate debug sound spawner as occlusion-aware expanding wave` |

### Verification

- [x] Build: `cataclysm-bn-tiles` + `cata_test-tiles` link clean on `osx-arm-slim` (no warnings from our code).
- [ ] In-game (user): F4 → Overlays → "spawn sounds on click", click near a wall → wave expands outward, shadowed behind the wall, centred on the clicked tile.
- [ ] In-game (user): toggle a text overlay on then off → it disappears immediately (no frozen frame).