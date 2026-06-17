# Lighting perf follow-up — decouple vis from structure rebuilds

Date: 2026-06-17
Status: planned (research done, code not started)
Research doc: LIGHTING_PERF_RESEARCH.md

## Background

The generation-counter gate (`transparency_generation`) is already implemented and working. It replaced the old `now != last_turn` + camera-origin terms in `sdl_render_frame.cpp`. The SDF/sun_sdf/sky_vis buffers now only rebuild when terrain/furniture/fields actually change, not every turn or scroll.

**Remaining problem:** `rebuild_pertile` is still one boolean that gates ALL four buffers together (SDF, sun_sdf, sky_vis, vis). When a door opens (`transparency_generation++`), it rebuilds everything including vis even though vis depends on `seen_cache`, not the occluder set. Conversely, when the player moves but nothing structural changed, vis still rebuilds alongside SDF/sun_sdf/sky_vis — but those are byte-identical so the generation counter correctly skips them. The asymmetry is: structure changes force an unnecessary vis rebuild.

## Goal

Split `rebuild_pertile` into two independent gates so each buffer only rebuilds when its actual dependency changed:

| Buffer | Dependency | Gate |
|--------|-----------|------|
| SDF, sun_sdf | `transparency_generation` + z + bubble origin | `rebuild_structure` |
| sky_vis | `outside_cache_dirty` (or outside generation) + z + origin | `rebuild_structure` (same as above for now) |
| vis | `seen_cache_dirty` + turn/position change | `rebuild_vis` |

## Files to touch

- `src/sdl_render_frame.cpp` — split the gate logic, pass two booleans instead of one
- `src/lighting/frame_build.h` — update function signature: `bool rebuild_structure, bool rebuild_vis` (or a struct)
- `src/lighting/frame_build.cpp` — use separate gates for SDF/sun_sdf/sky_vis vs vis sections

## Implementation steps

### Step 1: Verify vehicle movement invalidates correctly

Before changing anything, confirm that vehicles call `set_transparency_cache_dirty()` when they move. Search for vehicle movement code paths and trace whether the generation counter increments. If not, the gate is wrong for driving scenes and needs fixing first.

**Search targets:**
- Vehicle movement/update functions in `src/vehicle.cpp` or similar
- Any path that changes vehicle position without calling `set_transparency_cache_dirty`

### Step 2: Split the gate in sdl_render_frame.cpp

Current code (lines ~132-150):
```cpp
static std::uint64_t last_gen = 0;
static int           last_z = INT_MIN;
static point         last_origin{ INT_MIN, INT_MIN };
bool rebuild_pertile = true;
if( g && world_generator && world_generator->active_world ) {
    const auto &cache = g->m.get_cache_ref( z );
    const std::uint64_t gen = cache.transparency_generation;
    rebuild_pertile = imgui_layer::visible()
                      || gen != last_gen || z != last_z
                      || origin != last_origin;
```

Replace with two gates:
```cpp
bool rebuild_structure = true;  // SDF, sun_sdf, sky_vis
bool rebuild_vis = true;        // FOV visibility
if( g && world_generator && world_generator->active_world ) {
    const auto &cache = g->m.get_cache_ref( z );
    const std::uint64_t gen = cache.transparency_generation;
    
    rebuild_structure = imgui_layer::visible()
                        || gen != last_gen || z != last_z
                        || origin != last_origin;
    
    // vis depends on seen_cache_dirty + player movement/turn
    rebuild_vis = imgui_layer::visible()
                  || cache.seen_cache_dirty  // or however we track this
                  || now != last_turn;       // turn advanced → FOV may change
    
    if( rebuild_structure ) {
        last_gen = gen;
        last_z = z;
        last_origin = origin;
    }
}
```

**Question to resolve:** how does `seen_cache_dirty` propagate? Currently it's a bool on the level_cache. Check whether it gets set every turn or only when FOV actually changes. If it's always true during gameplay, we need a different vis gate (e.g., player position change + light source changes).

### Step 3: Update frame_build.h signature

Change from:
```cpp
frame_lighting_result build_and_submit_lighting( render_state &rs,
        bool rebuild_pertile, bool want_hud_snapshot, float skylight_bleed = 0.0f,
        float vision_blur = 0.0f );
```

To either two booleans or a struct:
```cpp
struct lighting_rebuild_flags {
    bool structure = true;  // SDF, sun_sdf, sky_vis
    bool vis = true;        // FOV visibility
};

frame_lighting_result build_and_submit_lighting( render_state &rs,
        lighting_rebuild_flags rebuild, bool want_hud_snapshot, float skylight_bleed = 0.0f,
        float vision_blur = 0.0f );
```

### Step 4: Update frame_build.cpp to use separate gates

The SDF/sun_sdf/sky_vis block (lines ~103-272) is gated on `rebuild_pertile`. Change to `rebuild.structure`.

The vis block (lines ~287-322) is also inside the same gate. Extract it so it runs when `rebuild.vis` regardless of structure state.

**Key detail:** the current code has the vis build INSIDE the `if( rebuild_pertile && ... )` block at line 103. The vis section needs to be able to run independently — move it outside or create a parallel gate path.

### Step 5: Verify upload handles partial rebuilds

`sdf_pass::upload()` already gates each buffer on vector size (e.g., `if( xfer_sdf_ && static_cast<Uint32>( sdf.size() ) >= sdf_subcells )`). Empty vectors → skip upload → GPU retains previous frame's data. This path is already correct; just verify with a test build that partial rebuilds don't cause visual artifacts.

## Additional easy wins (lower priority)

### Bleed flood-fill gate on outside_cache_dirty

Lines 207-254 in frame_build.cpp run the skylight bleed flood-fill every structure rebuild, even when `outside_cache` hasn't changed. Gate it:
```cpp
if( skylight_bleed > 0.001f && mc.outside_cache_dirty.any() ) {
    // ... flood fill ...
}
```

### Gaussian blur early-out

Lines 261-272 convert sky_vis uint8 → float, run blur, then convert back — even when `vision_blur` is low. The function guard at line 31 catches it but the conversion loop still runs. Move the check before the conversion:
```cpp
if( vision_blur > 0.05f && static_cast<int>( sky_vis.size() ) >= total ) {
    std::vector<float> sf( total );
    for( int i = 0; i < total; ++i ) {
        sf[i] = static_cast<float>( sky_vis[i] );
    }
    gaussian_blur_tilefield( sf, W, H, vision_blur );
    // ... convert back ...
}
```

## Risks

- **seen_cache_dirty always true:** if the sim marks seen_cache dirty every turn (e.g., because of light source aging), then `rebuild_vis` fires every turn anyway and the decoupling is pointless. Check this before investing in the split.
- **Partial rebuild visual artifacts:** if vis uploads without SDF, ensure the shader handles stale SDF + fresh vis correctly. The upload path retains GPU buffers on skip, so this should be fine — but verify.

## Verification

1. Build with Tracy zones enabled, profile a static interior scene (player standing still for 10 turns). Confirm `light_sdf_dt` zone is NOT firing turn-to-turn.
2. Profile walking in static interior. Confirm only `light_vis_build` fires, not SDF DT.
3. Open/close a door. Confirm structure rebuilds but vis does not (unless player also moved).
4. Drive a vehicle. Confirm both rebuild correctly (vehicle movement should increment transparency_generation).
