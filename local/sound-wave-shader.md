# GPU Shader Sound Wave Visualization

## Goal
Replace the blocky tile-based sound pulse rendering (`queue_ui_rect` per tile) with smooth, GPU-rendered expanding wavefront circles that respect occlusion from the existing flood-fill data.

## Approach: Instanced Quad Pass (Rain Effect Pattern)

Follow the proven `rain_effect` pattern: instance buffer + procedural vertex shader + custom fragment shader, rendering onto the world target with alpha blending.

### Why this approach
- **Proven**: `rain_effect` already does instanced world-positioned rendering with procedural vertices
- **Smooth**: Fragment shader computes distance from circle center → smooth radial gradient
- **Occlusion-aware**: Flood-fill data already encodes reachable tiles; render only reachable tiles as circles
- **Multi-pulse**: Natural alpha blending handles overlapping pulses
- **Minimal integration**: One `record()` call in `draw_lighting_overlays()`

## Architecture

### New Files

#### `src/lighting/sound_wave_pass.h`

```cpp
#pragma once
#include <SDL3/SDL_gpu.h>
#include <cstdint>
#include <vector>

namespace lighting {
class gpu_device;

// One sound wave instance (wire-stable with vertex shader).
// 32 bytes, 16-byte aligned.
struct sound_wave_instance {
    float world_x;    // source tile X
    float world_y;    // source tile Y
    float radius;     // current wavefront radius in tiles
    float max_radius; // pulse expires at this radius
    float intensity;  // 0..1 brightness (life curve)
    float pad0;
    float pad1;
    float pad2;
};
static_assert(sizeof(sound_wave_instance) == 32);

class sound_wave_pass {
public:
    sound_wave_pass() = default;
    ~sound_wave_pass();

    bool init(gpu_device& dev, SDL_GPUTextureFormat target_format);
    void shutdown() noexcept;
    bool ready() const noexcept;

    // Record all pulses onto `world_tex`.
    // `params` carries camera projection + tile geometry.
    void record(
        SDL_GPUCommandBuffer* cb,
        SDL_GPUTexture* world_tex,
        std::uint32_t world_w, std::uint32_t world_h,
        const std::vector<sound_wave_instance>& instances,
        float camera_off_x, float camera_off_y,
        float tile_pixel_size);

private:
    gpu_device* dev_ = nullptr;
    SDL_GPUShader* vert_ = nullptr;
    SDL_GPUShader* frag_ = nullptr;
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
    SDL_GPUTransferBuffer* xfer_ = nullptr;
    SDL_GPUBuffer* storage_ = nullptr;
    static constexpr int MAX_INSTANCES = 512;
};
}
```

#### `data/shaders/lighting/src/sound_wave.vert.hlsl`

Procedural quad vertex shader (6 vertices = 2 triangles forming a unit quad centered at origin). Expands/scales to each instance's screen position.

```hlsl
// Vertex shader for sound wave visualization.
// Procedural quad (6 verts = unit square centered at origin).
// Instance buffer provides world position, radius, intensity.
// Uniform provides camera projection.

struct SoundWaveInstance {
    float2 world_pos;   // xy: source tile
    float radius;       // current wavefront radius (tiles)
    float max_radius;   // expiration radius
    float intensity;    // 0..1 life
    float3 pad;
};

cbuffer SoundWaveParams : register(b0, space1) {
    float camera_off_x;
    float camera_off_y;
    float tile_pixel_size;
    float proj_w;
    float proj_h;
    uint instance_count;
    float3 pad;
};

StructuredBuffer<SoundWaveInstance> Instances : register(t0, space0);

struct VS_OUT {
    float4 pos : SV_Position;
    float2 center : TEXCOORD0;  // circle center in pixel coords
    float radius_px : TEXCOORD1; // radius in pixels
    float intensity : TEXCOORD2;
};

static const float2 quad_verts[6] = {
    {-1, -1}, {1, -1}, {1, 1},
    {-1, -1}, {1,  1}, {-1, 1}
};

VS_OUT main(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    if (iid >= instance_count) {
        // Kill unused instances: project off-screen.
        return (VS_OUT)(float4(0,0,0,1), 0, 0, 0);
    }

    const SoundWaveInstance inst = Instances[iid];
    // World tile → screen pixel (same formula as sprite.vert)
    float2 center = (inst.world_pos + float2(camera_off_x, camera_off_y)) * tile_pixel_size;
    float r_px = inst.radius * tile_pixel_size;

    // Scale quad to circle diameter + margin for anti-aliasing
    float2 scale = r_px * 2.1;
    float2 ndc_pos = (center + quad_verts[vid] * scale) / float2(proj_w, proj_h);
    // Flip Y for NDC (SVG convention: +Y down → NDC +Y up)
    ndc_pos.y = 1.0 - ndc_pos.y;

    VS_OUT out;
    out.pos = float4(ndc_pos * 2.0 - 1.0, 0, 1);
    out.center = center;
    out.radius_px = r_px;
    out.intensity = inst.intensity;
    return out;
}
```

#### `data/shaders/lighting/src/sound_wave.frag.hlsl`

Distance-based smooth wavefront with interior gradient.

```hlsl
// Fragment shader for sound wave visualization.
// Smooth expanding ring with interior fade.
// Colors: cyan wavefront fading to translucent blue interior.

struct VS_OUT {
    float4 pos : SV_Position;
    float2 center : TEXCOORD0;
    float radius_px : TEXCOORD1;
    float intensity : TEXCOORD2;
};

float4 main(VS_OUT inp) : SV_Target {
    float dist = distance(inp.center, inp.pos.xy);
    float r = inp.radius_px;

    if (r <= 0.0 || dist > r * 1.05) {
        discard;
    }

    // Normalized distance from center (0=center, 1=edge)
    float norm = dist / r;

    // Wavefront ring: bright at edge, fading inward
    // Band width ~1.5 tiles worth of pixels
    float band_width = 1.5 * (r / 24.0); // scales with radius
    float band = 1.0 - smoothstep(1.0 - band_width, 1.0, norm);

    // Interior: low-opacity fill
    float interior = (1.0 - norm) * 0.15;

    float alpha = max(band * 0.8, interior) * inp.intensity;

    if (alpha < 0.01) discard;

    // Color: bright cyan wavefront → blue interior
    float3 wavefront = float3(0.3, 0.8, 1.0); // cyan
    float3 interior_c = float3(0.15, 0.4, 0.7); // blue
    float3 col = lerp(interior_c, wavefront, band);

    return float4(col * alpha, alpha);
}
```

### Integration Points

#### `src/sdl_render_frame.cpp` — `draw_lighting_overlays()`

Replace the current tile-based rendering block (lines 674-700) with:

```cpp
// ── Animated debug sound pulses (shader-rendered) ─────────────────
{
    auto& pulses = dev_test_lights::sound_pulses;
    if (pulses.empty() || !rs.sound_waves().ready()) { return; }

    const float tp = s_emo.tile_px > 0.f ? s_emo.tile_px : 32.f;
    const double now = dev_test_lights::pulse_now_s();
    constexpr float speed = 9.0f; // tiles/sec

    std::vector<lighting::sound_wave_instance> instances;
    instances.reserve(pulses.size());

    for (const auto& p : pulses) {
        if (p.z != s_emo.player_z) continue;
        const float max_r = std::clamp(p.volume, 1.f, 24.f);
        const float radius = static_cast<float>(now - p.spawn_s) * speed;
        const float life = std::clamp(1.f - radius / max_r, 0.f, 1.f);

        // Source position: use centroid of field tiles
        if (!p.field.empty()) {
            float cx = 0, cy = 0;
            for (const auto& t : p.field) { cx += t.tx; cy += t.ty; }
            cx /= p.field.size(); cy /= p.field.size();

            instances.push_back({cx, cy, radius, max_r, life, 0, 0, 0});
        }
    }

    auto& sw = rs.sound_waves();
    const auto win = get_sdl_window_size();
    sw.record(ctx->cmd_buffer, /* world_tex or swapchain */,
              win.x, win.y, instances,
              s_emo.cam_off_x, s_emo.cam_off_y, tp);

    // Purge expired pulses
    std::erase_if(pulses, [now](const dev_test_lights::sound_pulse& p) {
        const float max_r = std::clamp(p.volume, 1.f, 24.f);
        return static_cast<float>(now - p.spawn_s) * speed > max_r;
    });
}
```

#### `src/lighting/render_state.h`

Add `sound_wave_pass_` member and accessor:
```cpp
sound_wave_pass& sound_waves() noexcept { return sound_waves_; }
// ... private:
sound_wave_pass sound_waves_;
```

#### `src/lighting/render_state.cpp`

In `init()`:
```cpp
if (!sound_waves_.init(device_, swap_format)) {
    dbg(DL::Warning) << "sound_wave_pass init failed (non-fatal)";
}
```

In `shutdown()`:
```cpp
sound_waves_.shutdown();
```

#### `src/lighting/sound_wave_pass.cpp`

Implementation following `rain_effect.cpp` pattern:
1. `init()` — compile shaders via `load_lighting_shader_source()` + `compile_graphics_shader()`, create pipeline with alpha blend, allocate transfer/storage buffers
2. `record()` — upload instances via copy pass, open render pass on `world_tex`, push uniform, draw instanced primitives
3. `shutdown()` — release GPU resources

## Implementation Order

1. **Create shader files** — `sound_wave.vert.hlsl`, `sound_wave.frag.hlsl`
2. **Create pass class** — `sound_wave_pass.h`, `sound_wave_pass.cpp`
3. **Wire into render_state** — add member, init/shutdown, accessor
4. **Integrate in draw_lighting_overlays** — replace tile-based rendering
5. **Build & test** — `cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles`
6. **Tune** — adjust colors, band width, interior opacity based on visual result

## Visual Reference
- Mark of the Ninja stealth detection waves
- Dark Souls detection indicator
- Expanding cyan ring with bright edge, translucent blue interior
- Alpha fades with distance from wavefront

## Risks & Mitigations

| Risk | Mitigation |
|------|-----------|
| Shader compile failure | Non-fatal init; falls back gracefully |
| Instance count exceeded | `MAX_INSTANCES = 512` — plenty for debug pulses |
| Wrong projection | Use same world→screen formula as `sprite.vert` |
| Z-fighting with UI | Render in overlays pass (after tiles, before UI composite) |

## Future Enhancements (Not in Scope)

- Per-tile occlusion rendering (render each reachable tile as a small circle, weighted by distance — preserves wall shadows)
- SDF-aware wavefront deformation (sample SDF buffer in shader to bend wavefront around walls)
- Color-coded by sound category (combat, environmental, creature)
- Persistence: keep sound visualization running (not just debug pulses)