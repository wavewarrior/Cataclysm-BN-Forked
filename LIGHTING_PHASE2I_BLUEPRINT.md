# Phase 2i-B Cutover Blueprint

Status as of this commit:
- Branch `feat/lighting-phase2i-cutover` is on top of `feat/lighting-phase2-sdl_gpu`.
- Phase 2i-A landed: `lighting::render_state` warms up on a hidden window, SDL_Renderer still drives the visible window.
- Build green on macOS arm64 (`out/build/osx-arm-slim`). No render-path regressions.

2i-B is the *visible* renderer swap. It cannot be done piecewise inside one commit because SDL3 makes both `SDL_ClaimWindowForGPUDevice` and `SDL_CreateRenderer` exclusive-claim a window — once you flip the visible window over, every legacy SDL_Renderer call site in the codebase becomes a no-op (best case) or a segfault (worst case) until it's been migrated to the GPU stack.

The plan below splits the work into seven commits that each end at a buildable, runnable point (even if the game looks broken in the middle). The order is the order in which call sites stop working as the renderer dies, so each step removes one source of fall-back churn rather than adding compatibility shims.

---

## Pre-flight (do once, before commit 2i-B-1)

Read these end-to-end so the bigger files don't surprise you mid-edit:

- `src/sdltiles.cpp` — focus on `WinCreate` (renderer init, lines ~190-345), `WinDestroy`, `refresh_display`, `set_displaybuffer_rendertarget`, `try_sdl_update`, `handle_resize`, `resize_term`, `SetupRenderTarget`.
- `src/cata_tiles.cpp` — focus on `cata_tiles::init` (atlas setup), `draw_tile_at`, `draw_sprite_at` (around line 4297), `apply_color_filter`, `render_to_screen` / similar entrypoint. There are ~30 `RenderCopy` / `SetTextureColorMod` / `SetRenderTarget` call sites scattered through the file — grep first, group later.
- `src/sdl_font.cpp` — `OutlinedTriangleFont::OutputChar`, `BitmapFont::OutputChar`, the glyph cache that uses `SDL_Texture` and `CreateTextureFromSurface`. The GPU path will route through `lighting::font_engine`, which uses `TTF_CreateText` against a `TTF_Font *` — note where the font handles currently live.
- `src/sdl_geometry.cpp` — `DefaultGeometryRenderer::rect`, `ColorModulatedGeometryRenderer::rect`. Trivially replaceable with `gpu_geometry`.
- `src/sdl_utils.cpp` — has CreateSurface / blit / palette helpers. Most stay (surfaces are still CPU-side staging); audit whether any SDL_Renderer-using helpers leak in.
- `src/dynamic_atlas.cpp` — `allocate_sprite` does render-target-into-atlas. The GPU equivalent is `gpu_atlas::upload(cb, surface)` which expects a CPU surface, so the caller's *upstream* surface preparation can stay; only the upload step changes.
- `src/pixel_minimap.cpp` — has its own render-target chain. Treat as an isolated sub-system; can land in its own commit (2i-B-7).
- `src/cata_tiles_color.cpp` — color modulation helpers. Most become uniform pushes; minor.

Also re-read the SDL3 GPU lifecycle: a frame is one `SDL_AcquireGPUCommandBuffer` → arbitrary number of copy/render passes → one `SDL_SubmitGPUCommandBuffer`. Nesting render passes is forbidden. `lighting::sprite_batcher` accumulates and flushes a whole pass per `begin_pass / end_pass` pair; for one-shot UI tweens you'll want multiple passes per frame.

---

## Commit 2i-B-1 — flip the visible window

Goal: `WinCreate` claims the visible window for the GPU device. SDL_Renderer init is removed. The game compiles. It will probably crash on first draw call — that's fine, the next commit takes the first draw call.

Edits:

1. `src/sdltiles.cpp::WinCreate`
   - Drop the `software_renderer` / `SDL_CreateRenderer` block (lines ~254-300).
   - Drop the `SetupRenderTarget` call and the `display_buffer` allocation.
   - Drop the `geometry = make_unique<…GeometryRenderer>` block.
   - Replace `lighting::try_init_render_state()` with a `lighting::init_render_state_for( ::window.get() )` call that runs against the visible window (add this helper to `render_state.cpp`; reuse the existing one, just rename the hidden-window holder away).
2. `src/sdltiles.cpp::WinDestroy`
   - Keep `shutdown_render_state()` at the top; remove the `renderer.reset()` and `display_buffer.reset()` lines.
3. `src/sdltiles.cpp` globals
   - Delete `static SDL_Renderer_Ptr renderer;`, `static SDL_Texture_Ptr display_buffer;`, `static GeometryRenderer_Ptr geometry;`. Every translation unit that references them (and they `extern` somewhere) will fail to compile next — that's the gate for commit 2i-B-2.
4. `src/sdltiles.cpp::refresh_display`
   - Rewrite to:
     ```cpp
     auto &rs = lighting::get_render_state();
     auto ctx = rs.device().begin_frame();
     if( !ctx.valid() || !ctx.swapchain_tex ) {
         if( ctx.cmd_buffer ) {
             rs.device().cancel_frame( ctx );
         }
         return;
     }
     // TODO 2i-B-N: dispatch accumulated tile/ui/geometry draws here.
     rs.tile_batcher().begin_pass( ctx.cmd_buffer, ctx.swapchain_tex,
                                   ctx.swapchain_w, ctx.swapchain_h,
                                   /*clear=*/ &k_black_rgba.x );
     rs.tile_batcher().end_pass();
     rs.device().submit_frame( ctx );
     ```
5. Strip the `set_displaybuffer_rendertarget` function (and its forward decls in headers).
6. `sdl_wrappers.h` — mark `SDL_Renderer_Ptr` / `SDL_Texture_Ptr` deprecated *but keep the typedefs*. Removing them now would cascade compile breaks; the next commits move call sites over and the typedefs disappear in 2i-B-7.

Expected outcome: project builds, game launches, shows a black window, exits cleanly on close. The SDL log shows the GPU device driver line.

---

## Commit 2i-B-2 — geometry renderer

Goal: every UI-frame rectangle and line goes through `lighting::gpu_geometry` via `lighting::render_state::ui_batcher`.

Edits:

1. `src/sdl_geometry.{h,cpp}` — delete the file pair. Forward-declare a new `class GeometryRenderer` in `sdl_geometry.h` that adapts to `gpu_geometry` + `ui_batcher`, OR (simpler) delete the abstraction entirely and have every caller use `render_state.geometry().rect( render_state.ui_batcher(), … )` directly. Grep `geometry->` to enumerate call sites; ~30-40 places.
2. `src/sdltiles.cpp` — remove the `geometry` global and its include; refresh_display picks up a `ui_batcher().begin_pass(...)` call that flushes the geometry queue at the end of the frame, after tiles but before the final swapchain submit.
3. Confirm `gpu_geometry` is fed via `set_texture( render_state.geometry().white_texture(), nullptr )` once per frame inside ui_batcher's first segment.

Game now draws solid-colour frames for UI rectangles. Text + sprites still missing.

---

## Commit 2i-B-3 — sprite batcher for tiles

Goal: `cata_tiles::draw_sprite_at` queues a `sprite_instance` instead of `RenderCopy`-ing.

Edits:

1. `src/cata_tiles.cpp::draw_sprite_at` (around line 4297):
   - Replace the `RenderCopy` block with a `render_state().tile_batcher().draw( inst )` where `inst.dst_*` come from the existing `destination` rect, `inst.src_uvw/h` come from the existing src rect normalized against the atlas page, and `inst.tint_rgba` is the legacy color-mod values pre-divided.
   - Tile batcher is opened/closed per layer via `begin_pass( … LOAD_OP_LOAD … )` so multiple layers compose. The layer-flush sequencing belongs in `cata_tiles::draw` rather than `draw_sprite_at`.
2. `cata_tiles::init` — replace `dynamic_atlas` with `lighting::gpu_atlas`. Reuse the existing `IMG_Load`→`SDL_Surface` upstream; route to `gpu_atlas::upload(cb, surface)` against the *one-shot atlas init command buffer* (acquire fresh `cb`, do all uploads, submit, release). Don't reuse the per-frame cb — atlas loads are amortised over thousands of frames.
3. `cata_tiles::apply_color_filter` — fold into the per-instance `tint` pre-multiply. Delete.
4. `active_tile_data` — animated overlays that previously rendered into the atlas now have two options:
   a. Re-upload from CPU surface each frame (simple, slower).
   b. Bind the atlas page as a render target via `SDL_GPUColorTargetInfo`. (Requires the atlas texture to be created with `USAGE_COLOR_TARGET | USAGE_SAMPLER`; change `gpu_atlas` page creation accordingly.)
   Pick (a) first for correctness, swap to (b) in a follow-up if perf matters.

After this commit terrain, items, vehicles, monsters render. Text still missing.

---

## Commit 2i-B-4 — fonts

Goal: every glyph drawn via `OutputChar` ends up as a `TTF_Text` driven by `lighting::font_engine`.

Edits:

1. `src/sdl_font.cpp` — replace `TTF_RenderText_Blended` → `SDL_Texture` cache with `TTF_CreateText( engine, font, str )` cache. The cache key (font, string, color, attr) stays; the value becomes `TTF_Text *`.
2. Per-frame, walk the visible glyphs, invoke `render_state().fonts().draw_text( cb, swapchain_tex, w, h, text, x, y, rgba )`. Currently the call site is `OutlinedTriangleFont::OutputChar` and `BitmapFont::OutputChar`; route through whichever exists.
3. Strip the SDL_Texture caches and the surface-to-texture helpers.

Game UI is now legible.

---

## Commit 2i-B-5 — pixel_minimap

Self-contained. Has its own atlas and renderer-target chain. Port in isolation; mirrors 2i-B-3's sprite_batcher swap but with a tiny dedicated batcher (or reuse `tile_batcher` with a different scale push-constant).

---

## Commit 2i-B-6 — atlas + dynamic_atlas cleanup

Goal: delete `src/dynamic_atlas.{h,cpp}` entirely.

Should be mechanical at this point — only place still using it is whatever path 2i-B-3 didn't catch. Grep `dynamic_atlas`, replace, delete the file. Update CMakeLists.txt globs auto-discover so the file just disappearing is enough.

---

## Commit 2i-B-7 — wrapper cleanup

Goal: delete dead types.

Edits:

1. `src/sdl_wrappers.h` — drop `SDL_Renderer_Ptr`, `SDL_Texture_Ptr`, `SDL_Texture_SharedPtr`, the renderer-using `RenderCopy` / `CreateTexture` / `SetRenderDrawColor` / `SetRenderDrawBlendMode` / `GetRenderDrawBlendMode` / `SetRenderTarget` / `RenderClear` / `RenderFillRect` / `RenderDrawPoint` declarations and definitions in `sdl_wrappers.cpp`.
2. Keep `SDL_Surface_Ptr` (still needed for IMG_Load staging) and `SDL_Window_Ptr`.
3. Rename `cata_tiles_color.cpp` content if it still has color-mod helpers tied to SDL_Texture; otherwise leave.
4. Update the secondary-window holder in `render_state.cpp`: remove it. The visible window is now the only one.

Final state: zero references to SDL_CreateRenderer / SDL_Renderer / SDL_RenderCopy / SDL_RenderPresent in src/.

---

## Verification per commit

After each commit:

1. `cmake --build out/build/osx-arm-slim -j 8` (or the Win11 preset). Must finish without errors.
2. `out/build/.../src/cataclysm-bn-tiles` launches. Crash logs go to `~/.cataclysm-bn/debug.log`; new "lighting:" lines go to the SDL log channel (stdout in dev builds).
3. After 2i-B-1: black window, exits cleanly via close button.
4. After 2i-B-2: black window with white menu rectangles.
5. After 2i-B-3: terrain visible, glyphs missing.
6. After 2i-B-4: full UI legible, minimap absent or stale.
7. After 2i-B-5: minimap correct.
8. After 2i-B-7: game indistinguishable from the SDL_Renderer build, but the SDL log shows a GPU device line at startup.

Reference scene for end-of-2i visual check: load `tests/save/test_save` (or whatever the smoke save is), pan camera, open inventory, open map, exit. Compare against a screenshot from the pre-phase-2 build.

---

## Risks specific to blind execution

- **Atlas page format**: legacy uses RGBA8 with pre-blended alpha because SDL_Renderer's blend handles that. `gpu_atlas` uses RGBA8_UNORM. If a tileset was authored against non-premultiplied alpha you'll see haloes around sprites — the fix is changing `tile_desc.src_color_blend` from `SRC_ALPHA` to `ONE` (already premultiplied) but only after confirming with a screenshot.
- **Coordinate orientation**: HLSL clip space + the Y-flip in the sprite shader matches the legacy renderer's top-left origin. If sprites render upside-down, the flip is the first place to look (`sprite.vert.hlsl`, the `* -2.0 + 1.0` term).
- **Color modulation parity**: the legacy `SDL_SetTextureColorMod` applies in linear space with the renderer's blend; my shader does `texel * tint` in whatever space the atlas was uploaded. If colors look washed-out, the fix is to gamma-decode in the fragment shader before the multiply.
- **MSAA / present**: SDL_GPU doesn't auto-MSAA; pipeline_desc currently sets `SDL_GPU_SAMPLECOUNT_1`. Old SDL_Renderer didn't either, so this should be parity.
- **Resize**: `gpu_device::on_window_resized()` is a no-op stub. SDL_GPU's swapchain rebuild on next acquire usually suffices, but I haven't tested. If a resize crashes, add explicit `SDL_SetGPUSwapchainParameters` re-call in there.

---

## When to bail and ask for a Win11 verification

Stop after each numbered commit and have the user run the game on Win11. The blind work is cheap to undo *between* commits (`git reset --hard HEAD~1`) but very expensive *after* commit 2i-B-7 when the legacy wrappers are gone. The point of the seven-commit split is exactly that bail-out granularity.
