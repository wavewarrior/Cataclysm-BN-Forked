# Lighting module — architecture reference

**Phase**: 2i-B (single-pass GPU rendering, partial cutover)  
**Backend**: SDL_GPU / SPIRV-Cross on D3D12 (Win11), Vulkan, Metal

---

## File map

| File | Role |
|---|---|
| `gpu_device.cpp/h` | SDL_GPU device lifecycle; swapchain format query |
| `sprite_batcher.cpp/h` | Instance-batched draw engine; one pass per batcher |
| `render_state.cpp/h` | **Singleton** (`get_render_state()`). Owns device, batchers, queues |
| `gpu_atlas.cpp/h` | GPU mirror of SDL_Texture atlas pages (2048×2048, 32×32 tiles) |
| `font_engine.cpp/h` | SDL_Surface→GPU glyph upload; per-glyph texture cache |
| `gpu_geometry.cpp/h` | 1×1 white texture (shared by UI rects and solid fills) |
| `shader_compiler.cpp/h` | HLSL→SPIRV-Cross compile; embedded `SPRITE_VERT_HLSL` in `sprite_batcher.cpp` |

---

## Per-frame render pass (single pass)

`refresh_display()` in `sdltiles.cpp` drives everything:

```
tile_batcher.begin_pass(cb, swapchain_tex, clear=black)
  1. bridge blit       ← legacy SDL_Renderer output (sidebar, menus, any non-GPU window)
  2. flush_tile_sprites ← terrain/mob/item sprites, per-atlas-page segments
  3. set_texture(white) + flush_ui_rects  ← colour-block overlays (lighting, debug)
  4. flush_font_glyphs  ← one set_texture+draw per glyph
tile_batcher.end_pass()
```

**All five kinds of draw share one render pass on one command buffer.**  
The old `ui_batcher` pass was merged here because D3D12 silently drops prior-pass draws when a second `BeginGPURenderPass` opens the same swapchain texture.

---

## Queue lifecycle

Queues persist across frames. Only cleared at the START of a redraw cycle:

```
UIManager::redraw_invalidated() → render_state::clear_frame_queues()
```

On **no-input frames**, `refresh_display()` re-drains the same queues from the previous cycle. This prevents a black flash when curses hasn't re-run the per-window draws.

Do NOT clear queues inside a flush method.

---

## sprite_instance struct (64 bytes, wire-stable)

```
float dst_x, dst_y, dst_w, dst_h   // pixel-space destination quad
float src_u, src_v, src_uw, src_vh  // normalised UV rect (0..1)
float tint_r, tint_g, tint_b, tint_a  // RGBA multiplier (1.0 = passthrough)
float rotation                      // radians, around quad centre
float pad0, pad1, pad2
```

Changing this struct requires updating the embedded `SPRITE_VERT_HLSL` in `sprite_batcher.cpp`. The static_assert on `sizeof(sprite_instance)==64` enforces the contract.

---

## sprite_batcher internals

- `MAX_INSTANCES = 65536`, `RING_SLOTS = 3` (storage-buffer ring)
- Storage buffer upload uses **`cycle=true`** (fixes D3D12 frames-in-flight race where GPU was still reading slot N when CPU started overwriting it under heavy load)
- Blend: `SrcAlpha · src + (1−SrcAlpha) · dst`
- `set_texture()` closes the current segment and starts a new one; same-texture consecutive calls are no-ops and extend the current segment

---

## Bridge

`bridge_tex` / `bridge_sampler` / `bridge_xfer` in `render_state` hold the legacy SDL_Renderer output:

1. Per-frame, `refresh_display()` calls `SDL_RenderPresent` on the hidden mirror renderer, reads back `display_buffer`, then `bridge_upload()` copies it to `bridge_tex`.
2. The first segment in the pass (`set_texture(bridge_tex, bridge_sampler)`) blits it over the whole swapchain.

Since 2i-B-5, `cata_tiles::draw` no longer writes terrain to `display_buffer`, so the bridge is mostly black in-game. Non-terrain windows (sidebar, overmap, menus) still write via the legacy path.

---

## Known invariants & gotchas

**1. No opaque fills before tile sprites.**  
`cata_tiles::draw` used to call `geometry->rect(..., {0,0,0,255})` to clear the terrain area. After 2i-B-5, `geometry->rect` routes to `ui_rect_queue` which flushes **after** tile sprites → opaque black painted over terrain. This fill was removed (swapchain LOADOP_CLEAR does the job). Compare: `draw_om` used `SDL_Color()` (alpha=0 no-op) and so never exhibited the bug.

**2. overmap vs in-game discrepancy.**  
Any regression where overmap works but the in-game frame is black is almost always a UI rect with `alpha=255` queuing before or overlapping tile sprites. Check `ui_rect_queue_` content first.

**3. cata_tiles enqueue path.**  
`cata_tiles::enqueue_tile_sprite()` (in `cata_tiles.h`) calls `render_state::queue_tile_sprite()`. Always sets `tint=(1,1,1,alpha)`. UV comes from `gpu_atlas::find_gpu_texture_full()` returning the GPU-mirror texture + pixel-space coords divided by atlas dimensions.

**4. Rotation field.**  
`rotation` is stored in sprite_instance but the embedded HLSL shader currently does non-rotated math (the rotation shader path is disabled). Rotated sprites fall through to `SDL_RenderTextureRotated` on the hidden mirror renderer → appear only via the bridge. Restoring rotation shader math is a separate task.

---

## File sizes (2026-05-22)

`cata_tiles.cpp` 6971 lines — the dominant cost. The draw path (`draw_sprite_at`, `draw`, `draw_om`) and the atlas loader (`load_tilejson`) are the two large zones; a future split would be `cata_tiles_draw.cpp` + `cata_tiles_atlas.cpp`.

---

## Token-cost tips for future sessions

- **Don't paste full game logs.** Tail ~200 lines; filter to `ERROR|WARN` + specific patterns.
- **Read files once then edit in sequence.** Re-reads from "stale" files cost full file tokens.
- **Use `smart_outline` → `smart_unfold`** instead of reading large files whole.
- **This CLAUDE.md** exists so you don't have to re-discover the pipeline architecture every session.
