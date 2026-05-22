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

## Token-cost tips

- **Don't paste full game logs.** Tail ~200 lines; filter to `ERROR|WARN`.
- **Read files once then edit in sequence.**
- **Use `smart_outline` → `smart_unfold`** instead of reading large files whole.
- **This CLAUDE.md** exists so you don't have to re-discover the pipeline architecture every session.
- **CRITICAL**: read `project_rendering_pipeline.md` from the auto-memory directory at session start (path in main CLAUDE.md).
