# Lighting module — architecture reference

**Phase**: 2i-B (bridge removed, GPU-only rendering)
**Backend**: SDL_GPU / SPIRV-Cross on D3D12 (Win11), Vulkan, Metal

---

## File map

| File | Role |
|---|---|
| `gpu_device.cpp/h` | SDL_GPU device lifecycle; swapchain format query |
| `sprite_batcher.cpp/h` | Instance-batched draw engine; one pass per batcher |
| `render_state.cpp/h` | **Singleton** (`get_render_state()`). Owns device, batchers, queues |
| `gpu_atlas.cpp/h` | GPU mirror of SDL_Texture atlas pages |
| `font_engine.cpp/h` | SDL_Surface→GPU glyph upload; per-glyph texture cache |
| `gpu_geometry.cpp/h` | 1×1 white texture (shared by UI rects and solid fills) |
| `shader_compiler.cpp/h` | HLSL→SPIRV-Cross compile; embedded `SPRITE_VERT_HLSL` in `sprite_batcher.cpp` |

---

## Per-frame render pass (bridge-free single pass)

`refresh_display()` in `sdltiles.cpp`:

```
tile_batcher.begin_pass(cb, swapchain_tex, LOADOP_CLEAR=black)
  1. flush_tile_sprites(gpu_sampler)   ← terrain/mob/item/vehicle sprites
  2. set_texture(white) + flush_ui_rects(gpu_sampler)  ← colour-block overlays
  3. flush_font_glyphs(gpu_sampler)    ← one set_texture+draw per unique glyph
tile_batcher.end_pass()
submit_frame()
```

Bridge removed 2026-05-22. `gpu_sampler_` created eagerly in `render_state::init()`.

---

## Queue lifecycle

```
ui_adaptor::redraw_invalidated()        ← called on every UI redraw cycle
  → clear_frame_queues()                ← ALL 3 queues cleared at start
  → window callbacks run                ← populate ui_rect + font_glyph queues

cata_tiles::draw() (in-game map)        ← called as a window callback WITHIN redraw_invalidated()
  → clear_tile_queue()                  ← ONLY tile sprites; NOT ui/font (already cleared by outer cycle)
  → enqueue all terrain/mob/vehicle sprites

draw_om() (sdltiles.cpp, overmap)       ← also a window callback within redraw_invalidated()
  → clear_tile_queue()                  ← same: tile-only clear
  → enqueue overmap tile sprites
```

On **no-input frames** (between redraw cycles), `refresh_display()` re-drains the **same queues** from the previous cycle. This is intentional — prevents black flash when no redraw has run yet.

Do NOT clear queues inside a flush method.

**Known limitation**: Partial redraws (e.g. tooltip on mouse-move) trigger `redraw_invalidated()` → `clear_frame_queues()` → only tooltip repopulates → one-frame black sidebar. Root fix = GPU accumulation texture (future phase 2i-B-7g or similar).

---

## sprite_instance struct (64 bytes, wire-stable)

```
float dst_x, dst_y, dst_w, dst_h   // pixel-space destination quad
float src_u, src_v, src_uw, src_vh  // normalised UV (negative = flipped axis)
float tint_r, tint_g, tint_b, tint_a  // RGBA multiplier (1.0 = passthrough)
float rotation                      // radians, clockwise, around quad centre
float pad0, pad1, pad2
```

Changing this struct requires updating `SPRITE_VERT_HLSL` in `sprite_batcher.cpp`. `static_assert(sizeof(sprite_instance)==64)` enforces the contract.

---

## Rotation in vertex shader

Clockwise screen-space rotation (Y-down) around quad centre. `rotation` field is in radians (converted from degrees by `enqueue_tile_sprite` in `cata_tiles.h`). Flip is UV-encoded (negative `src_uw`/`src_vh`), independent of rotation.

Formula: `x' = x*cos - y*sin`, `y' = x*sin + y*cos`

---

## sprite_batcher internals

- `MAX_INSTANCES = 65536`, `RING_SLOTS = 3` (storage-buffer ring)
- `cycle=true` on storage buffer upload (D3D12 frames-in-flight race fix)
- Blend: `SrcAlpha · src + (1−SrcAlpha) · dst`
- `set_texture()` closes current segment and starts a new one; same-texture calls extend current segment
- Null texture in a segment → D3D12 command list corruption → crash at next GPU call. Never pass null.

---

## Known invariants & gotchas

**1. No opaque fills before tile sprites.**
`geometry->rect` routes to `ui_rect_queue` which flushes **after** tile sprites. An opaque fill in `ui_rect_queue` will paint over terrain. Use LOADOP_CLEAR for full-screen black, not a queued rect.

**2. GPU atlas lookup (`find_gpu_texture_full`).** 
Returns `{nullptr,0,0}` if the SDL_Texture is not tracked as an atlas sheet. GPU miss → sprite invisible + D_WARNING log. Causes: sprite not packing through `copy_surface_to_dynamic_atlas`, or texture handle mismatch.

**3. `draw()` and `draw_om()` are window callbacks.**
Both run INSIDE a `redraw_invalidated()` cycle. `clear_frame_queues()` already ran before them. They call `clear_tile_queue()` only to reset tile sprites before re-enqueuing. Calling `clear_frame_queues()` here would wipe UI content the other window callbacks just populated.

**4. Loading image GPU path disabled.**
`loading_ui.cpp:409` has `if (false && cache->gpu_texture)`. The GPU path caused a D3D12 crash: `upload_surface_to_gpu_texture` submits on a separate CB; D3D12 may not complete the resource barrier to PIXEL_SHADER_RESOURCE before the render pass samples → command buffer corruption. Fix: upload on the render CB, or fence the separate CB before sampling.

**5. SDL_Renderer still alive.**
`copy_surface_to_dynamic_atlas` still creates SDL_Textures (used as lookup keys for `find_gpu_texture_full`). Cannot delete SDL_Renderer until atlas switches to a pure GPU key. Target: phase 2i-B-7f.

---

## What remains for 2i-B completion (phase 2i-B-7)

| Sub | State | Notes |
|---|---|---|
| 7b pixel_minimap | ⏳ stubbed as no-op | Migrate to GPU or drop entirely |
| 7d scissor/clip | ⏳ | `vehicle_preview` and UI clip-rect wrappers need scissor pass in sprite_batcher |
| 7e screenshot | ⏳ | Switch from `SDL_RenderReadPixels(display_buffer)` to SDL_GPU copy pass on swapchain |
| 7f mechanical delete | ⏳ | Remove `SDL_Renderer_Ptr`, `legacy_window`, `display_buffer`, `bridge_*`, `set_displaybuffer_rendertarget()` |
| accumulation texture | future | GPU-side "previous frame" buffer to fix partial-redraw flicker |

---

## Recurring build friction & fixes (2026-05-22/23 session)

Every item below has bitten us and costs 1+ build cycles. Check here first.

### C++ API patterns that differ from what you'd expect

| Wrong | Right | Why |
|-------|-------|-----|
| `my_MAPSIZE * SEEX` (global) | `get_map().getmapsize() * SEEX` | `my_MAPSIZE` is a **member** of the `map` class, not a global. `SEEX`/`SEEY` are in `game_constants.h`. |
| `get_map()` in `render_state::init()` | hardcode `11 * SEEX` or use `get_option` | `g == nullptr` at WinCreate time — `get_map()` dereferences `g`. Use a safe default; lazy-init on first game load if exact size matters. |
| `ter_id.obj_ptr()` | `ter_id->light_emitted` | `int_id<ter_t>` has `operator->` returning `const ter_t*`. There is no `obj_ptr()` method. |
| `m.i_at(tripoint_bub_ms)` | `m.i_at(p.raw())` | `map::i_at` takes `point` or `tripoint`, not strong-type wrappers. Call `.raw()`. |
| `map::get_cache(z)` from outside map | `map::access_cache(z)` | `get_cache` is private; `access_cache` is the public const accessor. |
| `effect_onfire` (extern) | `static const efftype_id my_effect_onfire("onfire")` | It's a translation-unit static in `lightmap.cpp`. Define a local copy. |
| `std::ranges::for_each(field, lambda)` | plain range-based `for` | MSVC rejects `std::ranges::for_each` on `field` objects; use `for(const auto& [ftype, fentry] : field)`. |
| `get_option<int>("MAPSIZE")` | doesn't exist | The option is named differently (or doesn't exist). Use `get_map().getmapsize()`. |
| `display_buffer` (removed) | `get_sdl_window_size()` | `display_buffer` was deleted in 2i-B-7f Part A. Callers of `get_sdl_display_buffer_size()` → use `get_sdl_window_size()`. |

### `dbg` macro — lighting/ files must define it themselves

The `dbg(x)` macro (`#define dbg(x) DebugLogFL((x),DC::SDL)`) is NOT globally available. Every new `.cpp` in `src/lighting/` that wants to log must add:
```cpp
#define dbg(x) DebugLogFL((x),DC::SDL)
```
after its includes. Using `DebugLog(DL::Error)` directly fails because `DebugLog` takes TWO arguments (level + class). Always use `dbg(DL::Error) << "..."` inside lighting/ files.

### SDL_GPU HLSL register spaces (D3D12 / SPIRV-Cross)

| SDL_GPU API | HLSL register |
|---|---|
| `SDL_BindGPUVertexStorageBuffers(rp, N, ...)` | `register(tN, space0)` |
| `SDL_PushGPUVertexUniformData(cb, N, ...)` | `register(bN, space1)` |
| `SDL_BindGPUFragmentSamplers(rp, N, ...)` | `register(tN, space2)` + `register(sN, space2)` |
| `SDL_BindGPUFragmentStorageBuffers(rp, N, ...)` | `register(tN, space4)` |
| `SDL_PushGPUFragmentUniformData(cb, N, ...)` | `register(bN, space3)` |

### cata_tiles.h nested class trap

`cata_tiles` contains a nested struct/class with getters like `get_tile_width()`. Code added inside THAT nested class cannot access `cata_tiles` members like `point o` (declared in the outer class at line ~1243).

**Rule**: any new method that touches `cata_tiles` private members (`o`, `tile_width`, `screentile_width`, etc.) must go in the **outer** `cata_tiles` class body, using explicit `public:`/`private:` guards if the surrounding region is private:
```cpp
    public:
        point get_tile_map_origin() const { return o; }
    private:
```

### Forward declarations missing in lighting/ headers

Always forward-declare ALL SDL GPU types used in a lighting/ header. Common omissions:
- `struct SDL_GPUBuffer;` — needed when any field or return type is `SDL_GPUBuffer*`
- `struct SDL_GPUTransferBuffer;`
- `struct SDL_GPUCopyPass;`

`sdl_wrappers.h` is NOT included by lighting/ headers (by design — they're self-contained).

### Phase 5 CPU lightmap tint: guards required

The `gpu_light_r/g/b = lm[idx].max()` path in `draw_from_id_string` MUST have:
1. `g != nullptr` — `get_map()` is only safe when `g` exists  
2. `lum > 0.001f` — before `generate_lightmap` runs, `lm` is all zeros. Tiles reaching this code are guaranteed LIT by the draw loop, so `lum == 0` means lightmap not generated yet (main menu, loading). Fall back to white tint (1.0f).

Without guard 2, the main menu renders completely black.

### Phase 7 black screen — SPIRV-Cross resource threading + ddx/ddy

**Symptom**: Main menu (and all rendering) fully black after adding fragment-stage
StructuredBuffers combined with ddx/ddy in the same HLSL shader.

**Mechanism**: `init_render_state_on` (render_state.cpp:527) has a try-catch
that silently swallows shader compile/pipeline failures. `ready()` only checks
`device_.ready()`, not the batcher. If `tile_batcher_.init()` throws, `gpu_sampler_`
is never set → all `refresh_display` flush guards (`rs.gpu_sampler()`) fail →
only LOADOP_CLEAR black runs. `WinCreate` ignores the return value of
`init_render_state_on`, so the game runs with black screen instead of crashing.

**Root causes**:
1. **SPIRV-Cross resource threading**: a helper function that accesses a global
   `StructuredBuffer` (e.g. `sdf_shadow(float2, float2, uint)` accessing `SdfBuf`)
   requires SPIRV-Cross to thread the buffer as an implicit MSL argument. Older
   SPIRV-Cross pins (SDL_shadercross @ 6b06e55c) may fail this silently.
   **Fix**: inline the helper — no separate function accessing global buffers.

2. **ddx/ddy + fragment StructuredBuffer combination**: `ddx(texel.rgb)` combined
   with `StructuredBuffer` in the same fragment shader can fail HLSL→SPIR-V→MSL
   on macOS Metal. **Fix**: replace `ddx/ddy` normal with flat `float3(0,0,1)`;
   proper normals come from normal atlas texture in Phase 7b.

**Rule**: In SPRITE_FRAG_HLSL —
- Never put StructuredBuffer access inside a helper function. Inline all SDF/emitter
  buffer accesses directly in `main()`.
- Avoid `ddx/ddy` on sampled texture values (screen-space derivatives + storage
  buffers in the same shader = Metal MSL translation failure on this SDL_shadercross pin).

### MAX_INSTANCES must be large for 4K + minimap

At 4K with a large terminal sidebar + pixel_minimap (17K rects) + tile sprites, the old 65536 cap is hit constantly. Current value: **262144** (262144 × 64 bytes = 16 MB/ring slot). Do not reduce.

### Variable scope in upload functions

When adding a storage buffer upload alongside a texture upload, do NOT reference `byte_size` from the texture block — declare a fresh variable:
```cpp
const Uint32 sbuf_size = pixel_count * static_cast<Uint32>(sizeof(float));
```

---

## Build workflow

**Do NOT run builds on this Mac.** Builds run on the Win11 PC (MSVC/D3D12).
If a build fails, the user pastes errors into the chat. Fix errors from the paste.

## Token-cost tips

- **Don't paste full game logs.** Tail ~200 lines; filter to `ERROR|WARN`.
- **Read files once then edit in sequence.**
- **Use `smart_outline` → `smart_unfold`** instead of reading large files whole.
- **This CLAUDE.md** exists so you don't have to re-discover the pipeline architecture every session.
- **CRITICAL**: read `project_rendering_pipeline.md` from the auto-memory directory at session start (path in main CLAUDE.md).
