# HUD Polish — Completion Plan (Phases 4B, 5, 6, 7, 8)

## Context

The HUD polish plan implemented 10 phases of Qud-style effects. An audit found 5 gaps across phases 4–8: the damage vignette has no animation spec or trigger (4B), floating combat text is never called from damage events (5), environmental tinting is missing fire and storm detection (6), status icon animations have no DOM targets (7), and the HUD particle system is a CPU-only stub with no GPU rendering (8). This plan completes every gap so all verification criteria from the original plan pass.

## Approach

Phases are ordered by dependency: Phase 4B and 6 are trivial (JSON/data + 2–3 lines each). Phase 7 requires understanding the RML escaping pipeline. Phase 5 requires locating the damage event integration point. Phase 8 is the largest — a full GPU pipeline implementation. All 5 are independent of each other.

### Phase 4B — Damage Vignette (2 changes)

The `#hud-vignette` div and CSS (radial red gradient, `opacity: 0`, `z-index: 20`) already exist in `sidebar_hud.rml:44` and `sidebar_hud.rcss:216-225`. The `hud_anim` alpha→opacity pipeline is fully wired. Two things are missing: the animation spec and the trigger.

**Step 1 — Add `hud_vignette` spec to `gfx/widgets/icons.json`.**

Insert after the existing `hud_topbar` entry (around line 96). The spec uses `on_change` to flash to the damage intensity, and `on_decrease` to fade out:

```json
{
  "id": "hud_vignette",
  "animations": [
    {
      "trigger": "on_change",
      "property": "alpha",
      "from": 0.0,
      "to": 1.0,
      "duration": 150,
      "ease": "quad_in"
    },
    {
      "trigger": "on_decrease",
      "property": "alpha",
      "from": 1.0,
      "to": 0.0,
      "duration": 800,
      "ease": "sine_out"
    }
  ]
}
```

The `on_change` trigger fires when the fed value changes (HP decreased); `on_decrease` fires the fade-out. `hud_anim::apply_channel` for `alpha` already maps to `el->SetProperty("opacity", value)`.

**Step 2 — Trigger vignette from HP decrease in `src/panels.cpp`.**

In `sidebar_hud_sync()`, the HP decrease block is at ~line 1277, right after `hud_shake::trigger( intensity )`. Add one `hud_anim::feed()` call immediately after:

```cpp
hud_shake::trigger( intensity );
hud_anim::feed( { .element_id = "hud-vignette", .spec_icon = "hud_vignette",
                   .value = intensity, .is_critical = false } );
```

The `intensity` value is `std::clamp( static_cast<float>( dmg ) / max_hp, 0.0f, 1.0f )`, already computed on the preceding lines.

### Phase 5 — Wire Combat Text to Damage Events (1 file)

The `combat_text_add()` API, physics engine (`combat_text_tick`), geometry compilation with font_scale and alpha fade, and render-loop integration are all complete. The single gap: no caller bridges damage events to `combat_text_add()`.

**Step 1 — Add `combat_text_add()` call in `src/combat_feedback.cpp`.**

`spawn_combat_feedback()` at line 64 is the master dispatcher for all SCT events. It already has `const Creature &target`, damage amount, `is_critical`, `is_graze`, and damage type. This is the single integration point covering melee, ranged, explosions, and environmental damage.

At the end of `spawn_combat_feedback()`, after the existing SCT logic, add:

```cpp
#include "lighting/rmlui_layer.h"  // combat_text_add, combat_text_options

// Convert creature position to screen coordinates.
// player_to_screen() returns logical px — same space combat_text_add expects.
const auto [sx, sy] = tilecontext->player_to_screen( target.pos().xy() );

// Color by damage type.
std::uint32_t rgba = 0xFFFFFFFF; // white = physical
if( opts.damage_type == damage_type::HEAT ) {
    rgba = 0xFF8040FF; // orange = fire
} else if( opts.damage_type == damage_type::ELECTRIC ) {
    rgba = 0x40D0FFFF; // cyan = electric
} else if( opts.damage_type == damage_type::BIOLOGICAL ) {
    rgba = 0x40FF40FF; // green = bio/healing
}

// Scale by severity.
auto font_scale = 1.0f;
auto vy = -30.f;
auto ay = 5.f;
if( opts.is_critical ) {
    font_scale = 1.5f;
    vy = -60.f;
    ay = 15.f;
} else if( opts.is_graze ) {
    font_scale = 0.75f;
}

// Random horizontal scatter to prevent stacking.
const auto vx = static_cast<float>( rng( -10, 10 ) );

combat_text_add( {
    .x = static_cast<float>( sx ),
    .y = static_cast<float>( sy ),
    .text = std::to_string( opts.total_damage ),
    .rgba = rgba,
    .font_scale = font_scale,
    .lifetime_ms = 1200.f,
    .vx = vx,
    .vy = vy,
    .ay = ay,
} );
```

If `tilecontext` is null (curses mode), guard with `if( tilecontext )` — combat text is tiles-only.

Needed includes: `#include "lighting/rmlui_layer.h"`, `#include "sdltiles.h"` (for `tilecontext`), `#include "rng.h"` (for `rng()`). Check which are already present before adding.

**Edge case:** If `player_to_screen()` returns off-screen coordinates, `combat_text_tick()` already culls items with `age_ms > lifetime_ms`, so no special handling needed.

**Step 2 — Verify `combat_text_options` struct fields.**

Read `src/lighting/rmlui_layer.h` to confirm the `combat_text_options` struct has all fields used above (`x`, `y`, `text`, `rgba`, `font_scale`, `lifetime_ms`, `vx`, `vy`, `ay`). If any field name differs, adjust the call site. The struct was designed for this exact use case.

**Step 3 — Verify `spawn_combat_feedback` has damage_type access.**

Read `src/combat_feedback.cpp` `spawn_combat_feedback()` signature to confirm it receives damage type. If `damage_type` is not directly available, use the `game_message_type` parameter to infer color: `m_bad` → white, `m_warning` → yellow. The exact mapping depends on what's available — implementer reads the function and maps accordingly.

### Phase 6 — Complete Environmental Tinting (2 additions)

The env tinting lambda in `src/panels.cpp:1282-1307` already applies `env-night`, `env-rad`, and `env-cold` via `el->SetClass()`. Two conditions are missing: fire proximity and storm weather.

**Step 1 — Add fire proximity detection.**

Inside the `apply_env_classes` lambda (around line 1300), after the `env-cold` block, add:

```cpp
const bool near_fire = get_map().has_nearby_fire( u.bub_pos(), 3 );
```

`has_nearby_fire()` is declared at `src/map.h:1054` and scans `points_in_radius` for `fd_fire` fields and `USABLE_FIRE` terrain/furniture within the given radius. Radius 3 matches the warmth radius used elsewhere.

Then add the SetClass call alongside the existing ones:

```cpp
el->SetClass( "env-fire", near_fire );
```

This goes in the same loop that applies to all 4 container elements (`hud-topbar`, `hud-botbar`, `hud-dock`, `hud-vitals`).

**Step 2 — Add storm weather detection.**

Above the `apply_env_classes` lambda, add:

```cpp
const auto &weather_id = get_weather().weather_id;
const bool is_storm = weather_id == weather_type_id( "thunder" )
                   || weather_id == weather_type_id( "lightning" );
```

`weather_type_id()` returns a `weather_type_id` from the string name. The JSON definitions are at `data/json/weather_type.json:174` ("thunder") and `:200` ("lightning").

The `.env-storm` CSS class already exists in `sidebar_hud.rcss` — it's handled via screen shake + vignette intensity per the original plan comment. If the `.env-storm` class does NOT exist in the RCSS, add it:

```css
.env-storm { /* Handled by screen shake + vignette intensity modulation */ }
```

Since `env-storm` has no visual CSS effect (it modulates shake/vignette, not CSS colors), the SetClass call is optional. **Decision: add it anyway** for future extensibility and to match the tinting pattern. The SetClass call:

```cpp
el->SetClass( "env-storm", is_storm );
```

Needed includes: check if `weather.h` and `weather_type.h` are already included in `panels.cpp`. They likely are (panels.cpp already reads `calendar::turn`), but verify.

### Phase 7 — Status Icon DOM Wrapping (1 file, careful escaping)

The `feed_status_anim` lambda in `src/panels.cpp:1034-1037` feeds `hud_anim` with element IDs like `"status-poison"`, but the botbar renders effects as plain text with no corresponding `<span>` elements. `hud_anim::tick()` at `hud_anim.cpp:107` calls `doc->GetElementById(id)` and gets `nullptr`.

The critical constraint: `cata_text_to_rml()` (`src/rml_util.cpp:72-100`) escapes `<` → `&lt;`, so raw `<span>` tags cannot be embedded through the normal text pipeline.

**Step 1 — Emit `<span>` wrappers in the effects section of `hud_botbar()`.**

In `src/panels.cpp`, the effects loop at ~lines 1025-1033 builds a `joined` string of plain effect names. Replace the plain text join with span-wrapped RML. Instead of:

```cpp
joined += effect.first;
```

Emit:

```cpp
// Map effect name to a spec key for the span id.
// The feed_status_anim lambda below uses "status-{spec}" as the element_id.
std::string spec_key;
if( effect.first.find( "Poison" ) != std::string::npos ) {
    spec_key = "poison";
} else if( effect.first.find( "On Fire" ) != std::string::npos
        || effect.first.find( "Burning" ) != std::string::npos ) {
    spec_key = "fire";
} else if( effect.first.find( "Bleeding" ) != std::string::npos ) {
    spec_key = "bleed";
} else if( effect.first.find( "Irradiated" ) != std::string::npos
        || effect.first.find( "Radiation" ) != std::string::npos ) {
    spec_key = "rad";
}

if( !spec_key.empty() ) {
    joined += "<span id=\"status-" + spec_key + "\">"
           + rml_escape( effect.first ) + "</span>";
} else {
    joined += rml_escape( effect.first );
}
```

The effect text is escaped with `rml_escape()` so only the wrapper tags are raw RML. The `joined` string is then concatenated into the botbar RML output **without** passing through `cata_text_to_rml()` — it must be emitted as raw RML since it contains intentional `<span>` tags. The existing botbar already uses `colorize()` which emits raw `<span style="color:...">` tags, so this pattern is established.

**Step 2 — Verify `colorize()` emits raw RML.**

Read `src/panels.cpp` to confirm that `colorize()` returns raw RML with `<span>` tags (not escaped). If so, the `joined` output can be concatenated with other `colorize()` output directly. If `colorize()` is itself escaped downstream, find the exact point where the botbar RML is assembled and ensure the span-wrapped effects bypass escaping.

**Step 3 — Add minimal CSS for status spans** (optional but recommended).

In `data/gui/sidebar_hud.rcss`, add:

```css
#status-poison, #status-fire, #status-bleed, #status-rad {
    display: inline;
}
```

These spans inherit text styling from their parent. The `display: inline` ensures they don't break the text flow. No other CSS is needed — the animations modify `opacity`, `transform`, and `background-color` via `hud_anim::apply_channel`.

### Phase 8 — HUD Particle GPU Pipeline (largest change)

The particle system has a complete CPU lifecycle (spawn, update, cull) with 4 emitter types (ember, dust, pollen, snow), but the GPU rendering pipeline is entirely stubbed. The shaders exist but have 3 bugs. This phase implements the full GPU pipeline following the `rain_effect` pattern.

**Step 1 — Fix `hud_particle.vert.hlsl` (3 bugs).**

File: `data/shaders/lighting/src/hud_particle.vert.hlsl`.

Bug 1: `cbuffer ParticleInstance : register(t0)` → every instance sees the same data. Fix: use `StructuredBuffer<ParticleInstance> : register(t0, space0)` indexed by `SV_InstanceID`.

Bug 2: Hardcoded `float2(1920.0, 1080.0)` viewport. Fix: add `cbuffer FrameParams : register(b0, space1) { float2 target_size; uint instance_base; uint fp_pad; }` matching rain's pattern.

Bug 3: 4-vertex quad. Fix: rewrite to use 6 vertices (triangle list) with `SV_VertexID` matching rain's `quad_uv[6]` pattern.

Rewrite the entire vertex shader to match rain_droplet.vert.hlsl structure:

```hlsl
// HUD particle vertex shader — procedural quad from instance data.
// Each instance encodes (x, y, size, alpha, r, g, b, rotation) as a 64-byte struct.
// Generates a rotated square centered at (x, y) with the given size.

struct ParticleInstance {
    float4 pos_size;   // xy = position, z = size, w = alpha
    float4 color;      // rgb = color, a = unused
    float rotation;
    float3 padding;
}; // 64 bytes

StructuredBuffer<ParticleInstance> Instances : register(t0, space0);

cbuffer FrameParams : register(b0, space1) {
    float2 target_size;
    uint instance_base;
    uint fp_pad;
};

struct VS_OUT {
    float4 pos : SV_POSITION;
    float4 colour : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

static const float2 quad_uv[6] = {
    float2(0, 0), float2(1, 0), float2(0, 1),
    float2(1, 0), float2(1, 1), float2(0, 1)
};

VS_OUT main(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    const ParticleInstance p = Instances[iid + instance_base];
    const float2 uv = quad_uv[vid];

    // Rotated quad corners.
    const float2 local = (uv - 0.5) * p.pos_size.z; // center and scale
    const float c = cos(p.rotation);
    const float s = sin(p.rotation);
    const float2 rotated = float2(
        local.x * c - local.y * s,
        local.x * s + local.y * c
    );
    const float2 screen_pos = p.pos_size.xy + rotated;

    // Convert to NDC.
    const float2 ndc = float2(
        screen_pos.x / target_size.x * 2.0 - 1.0,
        -(screen_pos.y / target_size.y * 2.0 - 1.0)
    );

    VS_OUT o;
    o.pos = float4(ndc, 0.0, 1.0);
    o.colour = float4(p.color.rgb, p.pos_size.w); // alpha from pos_size.w
    o.uv = uv;
    return o;
}
```

**Step 2 — Verify `hud_particle.frag.hlsl` is correct.**

The fragment shader is functionally complete (soft circle via `smoothstep`). Confirm it compiles. No changes expected.

**Step 3 — Implement `init()` in `hud_particle_effect.cpp`.**

Follow `rain_effect::init()` pattern (lines 55-130 of `rain_effect.cpp`). After `dev_ = &dev; ui_format_ = ui_format;`:

```cpp
// 1. Init shader compiler.
init_shader_compiler();

// 2. Load shader sources.
const auto vert_src = load_lighting_shader_source( "hud_particle.vert.hlsl" );
const auto frag_src = load_lighting_shader_source( "hud_particle.frag.hlsl" );

// 3. Compile shaders.
// Vertex: 0 inputs (procedural), 1 storage buffer (instances), 1 uniform buffer (FrameParams)
auto v = compile_graphics_shader(
    *dev_, vert_src, "main", SDL_SHADERCROSS_SHADERSTAGE_VERTEX,
    "hud_particle.vert" );
// Fragment: 0 samplers, 0 storage, 0 uniforms (color passed via interpolants)
auto f = compile_graphics_shader(
    *dev_, frag_src, "main", SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT,
    "hud_particle.frag" );
if( !v || !f ) {
    if( v ) { SDL_ReleaseGPUShader( dev_->raw(), v.shader ); }
    if( f ) { SDL_ReleaseGPUShader( dev_->raw(), f.shader ); }
    dbg( DL::Error ) << "hud_particle_effect: shader compile failed";
    return false;
}
particle_vert_ = v.shader;
particle_frag_ = f.shader;

// 4. Create graphics pipeline — premultiplied alpha blend.
SDL_GPUColorTargetBlendState blend{};
blend.enable_blend = true;
blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
blend.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G
                       | SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;

SDL_GPUColorTargetDescription ctd{};
ctd.format = ui_format_;
ctd.blend_state = blend;

SDL_GPUGraphicsPipelineCreateInfo pci{};
pci.vertex_shader = particle_vert_;
pci.fragment_shader = particle_frag_;
pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
pci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
pci.target_info.num_color_targets = 1;
pci.target_info.color_target_descriptions = &ctd;

particle_pipeline_ = SDL_CreateGPUGraphicsPipeline( dev_->raw(), &pci );
if( !particle_pipeline_ ) {
    dbg( DL::Error ) << "hud_particle_effect: pipeline creation failed";
    return false;
}

// 5. Create GPU buffers — transfer (upload) + storage (draw).
constexpr auto instance_size = static_cast<Uint32>( sizeof( particle_gpu_instance ) );
constexpr auto buf_size = MAX_PARTICLES * instance_size;

SDL_GPUTransferBufferCreateInfo xfer_ci{};
xfer_ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
xfer_ci.size = buf_size;
particle_xfer_ = SDL_CreateGPUTransferBuffer( dev_->raw(), &xfer_ci );

SDL_GPUBufferCreateInfo stor_ci{};
stor_ci.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
stor_ci.size = buf_size;
particle_storage_ = SDL_CreateGPUBuffer( dev_->raw(), &stor_ci );

if( !particle_xfer_ || !particle_storage_ ) {
    dbg( DL::Error ) << "hud_particle_effect: buffer creation failed";
    return false;
}
```

**Step 4 — Define `particle_gpu_instance` struct.**

Add to `hud_particle_effect.cpp` in an anonymous namespace:

```cpp
// GPU-side instance layout matching hud_particle.vert.hlsl ParticleInstance.
struct particle_gpu_instance {
    float pos_size[4];  // x, y, size, alpha
    float color[4];     // r, g, b, unused
    float rotation;
    float pad[3];
}; // 64 bytes
static_assert( sizeof( particle_gpu_instance ) == 64 );
```

**Step 5 — Implement `shutdown()`.**

Release all GPU resources:

```cpp
auto hud_particle_effect::shutdown() noexcept -> void
{
    if( dev_ ) {
        if( particle_pipeline_ ) { SDL_ReleaseGPUGraphicsPipeline( dev_->raw(), particle_pipeline_ ); }
        if( particle_vert_ ) { SDL_ReleaseGPUShader( dev_->raw(), particle_vert_ ); }
        if( particle_frag_ ) { SDL_ReleaseGPUShader( dev_->raw(), particle_frag_ ); }
        if( particle_storage_ ) { SDL_ReleaseGPUBuffer( dev_->raw(), particle_storage_ ); }
        if( particle_xfer_ ) { SDL_ReleaseGPUTransferBuffer( dev_->raw(), particle_xfer_ ); }
    }
    particle_pipeline_ = nullptr;
    particle_vert_ = nullptr;
    particle_frag_ = nullptr;
    particle_storage_ = nullptr;
    particle_xfer_ = nullptr;
    particles_.clear();
    dev_ = nullptr;
}
```

**Step 6 — Fix `ready()`.**

Change from `return dev_ != nullptr;` to:

```cpp
auto ready() const noexcept -> bool {
    return dev_ != nullptr && particle_pipeline_ != nullptr
        && particle_xfer_ != nullptr && particle_storage_ != nullptr;
}
```

**Step 7 — Implement `upload_instances()`.**

Follow rain_effect's template pattern:

```cpp
auto hud_particle_effect::upload_instances(
    SDL_GPUCommandBuffer *cb,
    const std::vector<hud_particle> &particles ) -> bool
{
    if( particles.empty() ) { return false; }

    const auto count = static_cast<Uint32>(
        std::min( particles.size(), static_cast<size_t>( MAX_PARTICLES ) ) );
    const auto bytes = count * static_cast<Uint32>( sizeof( particle_gpu_instance ) );

    // Map transfer buffer, copy instances.
    void *mapped = SDL_MapGPUTransferBuffer( dev_->raw(), particle_xfer_, true );
    if( !mapped ) { return false; }
    auto *dst = static_cast<particle_gpu_instance *>( mapped );
    for( Uint32 i = 0; i < count; ++i ) {
        const auto &p = particles[i];
        dst[i] = {
            .pos_size = { p.x, p.y, p.size, p.alpha },
            .color = { p.color.r, p.color.g, p.color.b, 0.f },
            .rotation = p.rotation,
            .pad = { 0.f, 0.f, 0.f },
        };
    }
    SDL_UnmapGPUTransferBuffer( dev_->raw(), particle_xfer_ );

    // Copy pass: transfer → storage.
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass( cb );
    SDL_GPUTransferBufferLocation src{};
    src.transfer_buffer = particle_xfer_;
    src.offset = 0;
    SDL_GPUBufferRegion dst_region{};
    dst_region.buffer = particle_storage_;
    dst_region.offset = 0;
    dst_region.size = bytes;
    SDL_UploadToGPUBuffer( cp, &src, &dst_region, true );
    SDL_EndGPUCopyPass( cp );
    return true;
}
```

The `hud_particle` struct has `color` as an `nc_color` or similar — the implementer must check the actual field type and convert to float r/g/b. If `color` is a `uint32_t` RGBA, extract channels: `(color >> 24) / 255.f`, etc.

**Step 8 — Complete `record()` — add draw call after existing lifecycle code.**

The current `record()` already calls `spawn_particle()` and `update_particles()`. After that code, add:

```cpp
if( particles_.empty() ) { return; }

// Upload instances to GPU.
if( !upload_instances( cb, particles_ ) ) { return; }

const auto count = static_cast<Uint32>(
    std::min( particles_.size(), static_cast<size_t>( MAX_PARTICLES ) ) );

// Begin render pass on UI target — LOAD to preserve existing UI, STORE to keep.
SDL_GPUColorTargetInfo ct{};
ct.texture = ui_tex;
ct.load_op = SDL_GPU_LOADOP_LOAD;
ct.store_op = SDL_GPU_STOREOP_STORE;
SDL_GPURenderPass *rp = SDL_BeginGPURenderPass( cb, &ct, 1, nullptr );
if( !rp ) { return; }

// Push frame params uniform (vertex slot 0).
struct FrameParams {
    float target_w;
    float target_h;
    std::uint32_t instance_base;
    std::uint32_t pad;
};
const FrameParams fp {
    .target_w = static_cast<float>( ui_w ),
    .target_h = static_cast<float>( ui_h ),
    .instance_base = 0,
    .pad = 0,
};
SDL_PushGPUVertexUniformData( cb, 0, &fp, sizeof( fp ) );

SDL_BindGPUGraphicsPipeline( rp, particle_pipeline_ );

// Bind instance storage buffer.
SDL_GPUBufferBinding bb{};
bb.buffer = particle_storage_;
bb.offset = 0;
SDL_BindGPUVertexStorageBuffers( rp, 0, &bb.buffer, 1 );

// Draw: 6 vertices per instance (triangle list), N instances.
SDL_DrawGPUPrimitives( rp, 6, count, 0, 0 );
SDL_EndGPURenderPass( rp );
```

**Step 9 — Verify `hud_particle` struct field types.**

Read `src/lighting/hud_particle_effect.h` to confirm the `hud_particle` struct field names and types used in `upload_instances()`. The `color` field might be stored as separate floats, an `nc_color`, or a `uint32_t`. Adjust the `particle_gpu_instance` conversion in Step 7 accordingly.

## Critical Files & Anchors

| File | Symbol/Region | Reason |
|------|--------------|--------|
| `src/combat_feedback.cpp` | `spawn_combat_feedback()` ~line 64 | Phase 5 integration point — single dispatcher for all damage SCT |
| `src/panels.cpp` | `apply_env_classes` lambda ~line 1282 | Phase 6 fire/storm — insertion point for new SetClass calls |
| `src/panels.cpp` | effects loop ~line 1025 | Phase 7 — where plain text must become span-wrapped RML |
| `src/lighting/rain_effect.cpp` | `init()` lines 55-130, `upload_instances()` 195-215, `record()` 220-421 | Phase 8 — the reference GPU pipeline to clone |
| `data/shaders/lighting/src/hud_particle.vert.hlsl` | entire file | Phase 8 — must be rewritten (3 bugs) |

## Verification

- **Phase 4B**: Take damage in-game → confirm red vignette flashes at screen edges and fades over ~800ms. At low HP, consecutive hits should show proportionally stronger flashes.
- **Phase 5**: Attack a monster → floating damage numbers arc upward with white text. Land a crit → larger text (1.5x) with punchier arc. Fire damage → orange text.
- **Phase 6**: Stand near a fire source → `env-fire` class appears on HUD containers (inspect via debug). Set weather to thunderstorm → `env-storm` class appears.
- **Phase 7**: Apply poison via debug → the "Poisoned" text in the botbar pulses (alpha pingpong). Apply fire → "On Fire" oscillates (scale pingpong). Verify with DOM inspector that `<span id="status-poison">` exists.
- **Phase 8**: Stand near a fire source → ember particles drift upward across the HUD. Go outside in winter → snowflake particles. Verify non-zero GPU draw calls via debug overlay or frame debugger.
- **Build**: `cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles` succeeds. `./out/build/osx-arm-slim/tests/cata_test-tiles "[sidebar_anim]"` passes.

## Assumptions & Contingencies

- **`spawn_combat_feedback` signature**: The plan assumes it receives damage type and crit/graze flags. If the actual signature differs, map from whatever context is available (e.g. `game_message_type` → color, raw damage amount → font_scale threshold).
- **`hud_particle.color` field type**: If the `hud_particle` struct stores color as `nc_color` instead of floats, use the existing `nc_color_to_rgb()` or equivalent to extract float r/g/b. If stored as `uint32_t` RGBA, shift-and-mask.
- **`player_to_screen` availability in `combat_feedback.cpp`**: If `tilecontext` is not accessible from `combat_feedback.cpp`, move the `combat_text_add()` call to `Creature::deal_damage()` in `creature.cpp` (line ~1420, after `spawn_damage_number()`) where both the creature position and the tile context are available.
- **RML escaping in botbar**: If the `joined` effects string goes through an additional escaping step downstream that breaks the `<span>` tags, extract the effects section into a separate RML variable that bypasses `cata_text_to_rml()` — the existing `colorize()` function proves raw RML is supported in the botbar output.
- **Shader resource slot counts**: `compile_graphics_shader` infers slot counts from SPIR-V reflection. If the hud_particle vertex shader's storage buffer or uniform buffer slots are not detected correctly, pass explicit `SDL_GPUShaderCreateInfo` overrides matching rain_effect's pattern.
