# High-Fidelity Rain Effect — Implementation Plan

## Goal
Replace the current static single-tile rain overlay with a GPU-accelerated particle system featuring falling droplets, impact splashes, and persistent wet-spot accumulation via a reusable ping-pong splat map texture.

## Architecture Overview

### Three-Layer Rendering Pipeline
1. **Droplet layer** — Alpha-blended procedural streaks drawn onto the HDR world target (world_target)
2. **Splash layer** — Short-lived particles that contribute to wet-spot accumulation on a persistent splat map
3. **Splat map layer** — Ping-pong RGBA8 texture that accumulates wetness over time and fades gradually

### Render Order (in refresh_display / render_world_pass_w)
```
render_world_pass_w() {
    // ... terrain + lighting passes ...
    volumetric().record();      // sun shafts (additive into world_target)
    rain().record();            // droplets (alpha-blended) + splat fade (fullscreen)
    bloom().record();           // HDR bloom (reads from world_target)
}
```

## Files Created/Modified

### New Files
| File | Purpose |
|------|---------|
| `src/lighting/rain_effect.h` | Header: particle structs, rain_params, rain_effect class |
| `src/lighting/rain_effect.cpp` | Implementation: init/shutdown/particle management/render recording |
| `data/shaders/lighting/src/rain_droplet.vert.hlsl` | Droplet vertex shader (procedural quad renderer) |
| `data/shaders/lighting/src/rain_droplet.frag.hlsl` | Droplet fragment shader (procedural vertical streak, no texture sampling) |
| `data/shaders/lighting/src/rain_splat.frag.hlsl` | Splat fade + splash accumulation fullscreen shader |

### Modified Files
| File | Change |
|------|--------|
| `src/lighting/render_state.h` | Added `#include "rain_effect.h"`, `rain()` accessor, `rain_` member |
| `src/lighting/render_state.cpp` | Added `rain_.init()` in init(), `rain_.shutdown()` in shutdown() |
| `src/sdl_render_frame.cpp` | Added rain effect call between volumetric and bloom passes |
| `src/sdl_lighting_devui.h` | Added `g_rain_enable`, `g_rain_intensity` extern declarations |
| `src/sdl_lighting_devui.cpp` | Defined globals + ImGui controls (checkbox + intensity slider) |

## Implementation Details

### Droplet System
- **Max particles**: 2048 concurrent droplets
- **Spawn rate**: proportional to intensity (~60/frame at intensity=1.0)
- **Visual**: Procedural vertical streaks with bright-top/transparent-bottom gradient
- **Wind drift**: Configurable angle (degrees from north, clockwise), applied as rotation tilt
- **Rendering**: Alpha-blended onto world_target using instance-batched quad draws

### Splash System
- **Max particles**: 512 concurrent splashes
- **Trigger**: Droplets crossing the ground plane (bottom of screen)
- **Lifetime**: 10-15 frames with quadratic intensity falloff
- **Contribution**: Each splash writes a radial bright spot to the splat map

### Splat Map (Wet Spots)
- **Resolution**: 512x512 RGBA8 ping-pong textures
- **Fade rate**: Configurable per-frame decay multiplier (default: 0.98 = ~50% in 35 frames)
- **Accumulation**: Splash contributions add bright spots that gradually darken the wetness texture
- **Filtering**: Linear sampling for soft, painterly wet spot edges

### GPU Pipeline Structure
```
Droplet Pipeline:
  Vertex: rain_droplet.vert.hlsl (procedural quad with rotation)
  Fragment: rain_droplet.frag.hlsl (procedural streak gradient)
  Blend: SRC_ALPHA / ONE_MINUS_SRC_ALPHA (alpha blend onto world_target)

Splat Pipeline:
  Vertex: tonemap.vert.hlsl (fullscreen triangle, reused)
  Fragment: rain_splat.frag.hlsl (fade + splash accumulation)
  LoadOp: DONT_CARE (fully covered by fullscreen tri)
```

## Status: COMPLETE

All files written and integrated. Feature is ready for compilation testing on a system with a C++ compiler (MSVC/Clang/GCC).

### Remaining Work (Post-Compilation)
1. **Compile & fix any shader cross-compilation errors** — HLSL to SPIR-V/DXBC conversion may need adjustment
2. **Tune particle parameters** — spawn rates, velocities, visual sizes based on actual gameplay feel
3. **Weather integration** — currently uses hardcoded rain params; should read from actual weather system
4. **Performance profiling** — verify droplet draw call overhead at 60fps with max particles
5. **Wind direction from game state** — connect to actual wind data from the weather system

### Known Limitations
- Wind angle is hardcoded (270 degrees = west wind) until weather integration
- Splat map resolution (512x512) may show pixelation on very high-res displays
- No per-splash size variation (all splashes use 8px radius)
