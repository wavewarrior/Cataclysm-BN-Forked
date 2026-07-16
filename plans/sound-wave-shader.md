# GPU Shader Sound Wave Visualization

## Status: ✅ Built and Linked

Build: `cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles` — clean, no errors.

## Goal
Replace the blocky tile-based sound pulse rendering (`queue_ui_rect` per tile) with smooth, GPU-rendered expanding wavefront circles that respect occlusion from the existing flood-fill data.

## Approach: Per-Tile Instanced Circles (Rain Effect Pattern)

Follow the proven `rain_effect` pattern: instance buffer + procedural vertex shader + custom fragment shader, rendering onto the world target with alpha blending.

**Key design decision (corrected from initial plan):** Each reachable tile from the BFS flood-fill becomes one instanced circle (~0.55× tile size). The union of overlapping circles with additive blending naturally traces the occlusion boundary — smooth AND occlusion-aware. This preserves the most valuable property of the original rendering: wall shadows and corner wrapping.

### Why this approach
- **Occlusion-preserving**: BFS flood-fill data encoded in per-tile instances; circles only drawn where sound reaches
- **Smooth**: Fragment shader computes radial distance → soft circle edges, no blocky tiles
- **Proven pattern**: `rain_effect` already does instanced procedural quads with storage buffers
- **Multi-pulse**: Each pulse group rendered in its own `record()` call; alpha blending composites
- **Same instance count**: One instance per reachable tile (≤256 per pulse), same as current rect count

## Architecture

### Instance Struct (16 bytes)

```cpp
struct sound_wave_instance {
    float screen_x; // tile center X in screen pixels
    float screen_y; // tile center Y in screen pixels
    float dist;     // flood distance from source (tiles)
    float pad;
};
static_assert(sizeof(sound_wave_instance) == 16);
```

### Shader Uniform (32 bytes)

```cpp
cbuffer SoundWaveParams : register(b0, space1) {
    float radius;           // current wavefront radius (tiles)
    float life;             // 0..1 life curve
    float circle_radius_px; // per-tile circle render radius (pixels)
    float proj_w;
    float proj_h;
    uint  instance_count;
    float pad0;
    float pad1;
};
```

### Files Created

| File | Purpose |
|------|---------|
| `src/lighting/sound_wave_pass.h` | Pass class header; instance struct; `init()`/`shutdown()`/`record()` API |
| `src/lighting/sound_wave_pass.cpp` | Implementation: shader compile, pipeline, buffer upload, render pass |
| `data/shaders/lighting/src/sound_wave.vert.hlsl` | Procedural quad vertex shader; per-instance screen position → NDC |
| `data/shaders/lighting/src/sound_wave.frag.hlsl` | Smooth circle + wavefront ring; distance-based alpha; premultiplied output |

### Files Modified

| File | Change |
|------|--------|
| `src/lighting/render_state.h` | Added `#include "sound_wave_pass.h"`, `sound_waves()` accessor, `sound_waves_` member |
| `src/lighting/render_state.cpp` | `sound_waves_.init(device_, fmt)` in `init()`, `sound_waves_.shutdown()` in `shutdown()` |
| `src/sdl_render_frame.cpp` | Replaced `queue_ui_rect` tile loop with per-tile instance population + `rs.sound_waves().record()` |

### Vertex Shader

Procedural quad (6 vertices, unit square ±1). Each instance carries `screen_center` (already projected to pixels) and `dist` (flood distance). The vertex shader expands the quad to `circle_radius_px` and outputs NDC position, center, radius, and life to the fragment shader.

### Fragment Shader

- Computes radial distance from circle center (normalized 0..1)
- Soft circle edge via `smoothstep(0.85, 1.0, norm)`
- Wavefront intensity: bright ring where `band = radius - dist ≈ 0` (within 1.5 tile band), dim interior fill behind
- Color: cyan wavefront `float3(0.25, 0.85, 1.0)` → blue interior `float3(0.12, 0.35, 0.65)`
- Premultiplied alpha output; early discard below threshold

### Integration Flow

```
draw_lighting_overlays()
  ├─ for each pulse on player_z:
  │   ├─ compute radius, life from elapsed time
  │   └─ for each tile in pulse.field where dist <= radius:
  │       └─ convert (tx,ty) → screen pixels, push instance{sx, sy, dist}
  │
  └─ rs.sound_waves().record(cmd_buffer, world_target, win_size, instances, radius, life, circle_r)
      ├─ upload_instances() — memcpy → transfer buffer → GPU storage
      ├─ SDL_BeginGPURenderPass(world_target, LOAD_OP_LOAD)
      ├─ bind pipeline + storage buffer
      ├─ push fragment uniform (radius, life, circle_r, proj dimensions)
      ├─ SDL_DrawGPUPrimitives(6 verts, N instances)
      └─ SDL_EndGPURenderPass
```

### Buffer Layout

- **Vertex storage buffer** (`t0/space0`): `sound_wave_instance[MAX_INSTANCES]` — 16 bytes × 2048 = 32 KB
- **Fragment uniform** (`b0/space1`): `SoundWaveParams` — 32 bytes, pushed per-frame
- **Alpha blend**: premultiplied (`SRC=ONE, DST=ONE_MINUS_SRC_ALPHA`)

## Visual Reference
- Mark of the Ninja stealth detection waves
- Dark Souls detection indicator
- Expanding cyan ring with bright edge, translucent blue interior
- Occlusion boundary follows walls (preserved from BFS flood-fill)

## Tuning Parameters (in shader / integration)

| Parameter | Location | Current Value | Effect |
|-----------|----------|--------------|--------|
| `circle_radius_px` | `draw_lighting_overlays()` | `tp * 0.55f` | Per-tile circle size; controls overlap density |
| `band_width` | `sound_wave.frag.hlsl` | `1.5` tiles | Wavefront ring thickness |
| Ring peak alpha | `sound_wave.frag.hlsl` | `0.85` | Brightness at wavefront |
| Interior alpha | `sound_wave.frag.hlsl` | `0.12` | Fill behind wavefront |
| Circle edge feather | `sound_wave.frag.hlsl` | `smoothstep(0.85, 1.0)` | Anti-aliasing width |
| Wavefront color | `sound_wave.frag.hlsl` | `(0.25, 0.85, 1.0)` | Cyan |
| Interior color | `sound_wave.frag.hlsl` | `(0.12, 0.35, 0.65)` | Blue |

## Risks & Mitigations

| Risk | Mitigation |
|------|-----------|
| Shader compile failure | Non-fatal init; `ready()` guards the record call |
| Instance count exceeded | `MAX_INSTANCES = 2048` — supports 8+ simultaneous pulses |
| Wrong projection | Screen coords computed in CPU using same formula as existing code |
| Z-fighting with UI | Render in overlays pass (after tiles, before UI composite) |

## Future Enhancements (Not in Scope)

- SDF-aware wavefront deformation (sample SDF buffer in shader to bend wavefront around walls)
- Color-coded by sound category (combat, environmental, creature)
- Persistence: keep sound visualization running (not just debug pulses)
- Bloom/glow pass integration for wavefront emphasis