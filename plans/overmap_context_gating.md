# Overmap Context Gating — Architectural Plan

**Status**: Draft  
**Goal**: Eliminate all shared mutable global state between overmap rendering and main game rendering by introducing value-type `render_context` objects as the sole pipeline for draw commands.

---

## 1. Problem

The overmap and main game share several global mutable resources, causing rendering artifacts and requiring fragile RAII hacks:

| Shared resource | File(s) | Failure mode |
|---|---|---|
| `lighting::render_state::tile_sprite_queue_` | `src/lighting/render_state.h` | One drawer clears another's enqueued sprites |
| `terminal_framebuffer` / `oversized_framebuffer` | `src/sdltiles.cpp` | Stale cell skip when fonts differ between overmap and main game |
| `winBuffer` | `src/sdltiles.cpp` | Dirty-cell optimization groups incompatible windows together |
| `overmap_tilecontext == tilecontext` alias | `src/sdltiles.cpp` | Shared `o`/`op` tile origin members when no separate overmap tileset |
| `stdscr.transparent_backdrop` | `src/cursesport.h`, `src/overmap_ui.cpp` | Exception-leaked alpha state |
| `game::w_overmap`, `game::w_omlegend` | `src/game.h`, `src/overmap_ui.cpp` | Window aliasing, recreated each open |
| Zoom level on `class game` | `src/game.h`, `src/overmap_ui.cpp` | RAII guard bypass leaves wrong zoom |

---

## 2. Core concept

Replace every `lighting::get_render_state().queue_*(...)` side-effect call with a push into a **local `render_context` value**. Draw functions receive a `render_context&` and populate it. The compositor merges all contexts after all draw callbacks complete.

```
BEFORE:   draw(void) → global_queue.push()  [side effect on global singleton]
AFTER:    draw(render_context& ctx) → ctx.tile_sprites.push_back(...)  [side effect on local value]
```

### `render_context`

```cpp
// New file: src/lighting/render_context.h

struct tile_sprite_draw { /* as today */ };
struct ui_rect_draw { /* as today */ };
struct font_glyph_draw { /* as today */ };

/// Captures all draw commands emitted during a single frame's render pass.
/// Created fresh each frame by ui_manager, passed by mutable reference to
/// every redraw callback. Composited and flushed to GPU once at frame end.
struct render_context {
    std::vector<tile_sprite_draw> tile_sprites;
    std::vector<ui_rect_draw> ui_rects;
    std::vector<font_glyph_draw> font_glyphs;

    point pixel_offset = point_zero;  // screen-space offset for this layer
    int zoom = 0;                     // per-context zoom; no more global zoom save/restore

    // Window snapshot support: instead of blitting directly to a global
    // framebuffer, curses-to-SDL conversions write into these per-context
    // collections. The compositor re-assembles them in z-order.
    struct window_snapshot {
        catacurses::window win;       // identifies the window (for dirty-cell tracking)
        point pos;                    // screen-space position
        int width, height;
        std::vector<std::vector<cursecell>> cells;  // captured cell content
    };
    std::vector<window_snapshot> windows;

    // Compositing
    auto merge(render_context&& other) -> void;  // merge other's queues into this
    auto clear() -> void;                         // reset for next frame
};
```

Key design decisions:
- **Value type** — created per frame, moved not shared. No singletons.
- **No GPU handles** — `render_context` holds logical draw commands only. GPU upload happens in the flush step.
- **Owns its windows** — `window_snapshot` captures rendered cell grids. The compositor transforms these to glyph/rect commands during flush.

---

## 3. Pipeline change

### Today

```
SDL event loop
  → ui_manager::redraw_invalidated()
    → clear_frame_queues()            // global singleton clear (clears everyone's work)
    → main_ui_adaptor redraw_cb → game::draw()
      → cata_tiles::draw()
        → lighting::get_render_state().queue_tile_sprite(...)  [GLOBAL MUTATION]
      → curses rendering (wnoutrefresh etc.) → sdltiles.cpp::draw_window()
        → lighting::get_render_state().queue_ui_rect(...)      [GLOBAL MUTATION]
    → overmap ui_adaptor redraw_cb  (if overmap open)
      → cata_tiles::draw_om()       [ALSO USES GLOBAL QUEUE — conflict!]
    → composite per-adaptor slices
  → refresh_display()
    → begin_lighting_frame()
    → flush render_state queues → GPU
    → submit_frame()
```

### Tomorrow

```
SDL event loop
  → ui_manager::redraw_invalidated()
    → auto frame_ctx = render_context{};           // fresh context each frame
    → main_ui_adaptor redraw_cb(frame_ctx)
      → game::draw(frame_ctx)
        → cata_tiles::draw(frame_ctx, ...)
          → frame_ctx.tile_sprites.push_back(...)  [LOCAL MUTATION]
        → curses rendering → draw_window(win, frame_ctx)
          → frame_ctx.windows.push_back(snapshot)  [LOCAL MUTATION]
    → overmap ui_adaptor redraw_cb(frame_ctx)      [also uses frame_ctx — safe, ordered by z]
      → overmap_tilecontext->draw_om(frame_ctx, ...)
        → frame_ctx.tile_sprites.push_back(...)
    → compositor merges per-adaptor sub-contexts   [ordered by z, no aliasing]
    → refresh_display(frame_ctx)
      → resolve window snapshots → frame_ctx glyphs/rects
      → flush tile_sprites → GPU
      → flush font_glyphs + ui_rects → GPU
      → submit_frame()
```

**No global queue. No `clear_frame_queues()` that destroys another subsystem's work.**

---

## 4. Changes by component

### 4.1. New: `src/lighting/render_context.h`

Contents: `render_context` struct, `window_snapshot`, merge/clear operations, type aliases.

### 4.2. Refactor: `src/lighting/render_state.h/.cpp`

**Current role**: GPU-side singleton owning tile/font/rect queues + flush logic.  
**Future role**: Pure GPU flush engine. Removes queue ownership.

| Method | Change |
|---|---|
| `queue_tile_sprite(...)` | Remove. Callers push to `render_context.tile_sprites` instead. |
| `queue_font_glyph(...)` | Remove. |
| `queue_ui_rect(...)` | Remove. |
| `clear_tile_queue()` / `clear_font_queue()` / `clear_ui_queue()` | Remove. Fresh `render_context` per frame replaces clear. |
| `flush_tile_sprites()` | Now takes `render_context&` and iterates `ctx.tile_sprites` |
| `flush_ui()` | Now takes `render_context&` and iterates `ctx.font_glyphs` + `ctx.ui_rects` |
| `set_tile_scissor(...)` | Keep (GPU state; unaffected by context change). |
| `begin_lighting_frame()` / `end_pass()` / `submit_frame()` | Keep (GPU pipeline lifecycle). |

Removed data members:
- `tile_sprite_queue_` — owned by `render_context` now
- `ui_rect_queue_` — owned by `render_context` now
- `font_glyph_queue_` — owned by `render_context` now

Kept:
- `tile_batcher_` — GPU-side instanced batch builder; takes per-context input during flush
- `ui_batcher_` — same
- `font_engine_` — font atlas; unchanged
- `geometry` — GPU geometry helpers; unchanged

### 4.3. Refactor: `src/cata_tiles.h/.cpp`

| Signature change |
|---|
| `auto draw( render_context &ctx, const tripoint &center, ... ) -> void` |
| `auto draw_om( render_context &ctx, const tripoint &center, ... ) -> void` |

Internal change: all `lighting::get_render_state().queue_tile_sprite(...)` calls become `ctx.tile_sprites.emplace_back(...)`.

Remove:
- `clear_tile_queue()` calls — no longer owns a queue
- `o` / `op` members reliance in overmap path — offset comes from `ctx.pixel_offset`

### 4.4. Refactor: `src/sdltiles.cpp` (heaviest change)

**Goal**: Curses window rendering → `render_context.windows` snapshots instead of immediate GPU blits.

#### `curses_drawwindow(WINDOW*, render_context& ctx)`
Instead of calling `geometry->rect()` and `font->OutputChar()` directly (GPU side effects), this function:
1. Captures the window's cell grid into a `render_context::window_snapshot`
2. Pushes the snapshot to `ctx.windows`

The resolution of snapshots → glyphs/rects happens later in `refresh_display()`, where the compositor can process windows in z-order.

#### `draw_window()` removal
This function was the main curses-to-SDL bridge. Its logic moves into:
- Snapshot capture: inline in `curses_drawwindow()` (trivial: copy `win->line[]` cells)
- Snapshot rendering: a new `resolve_window_snapshots(render_context&, render_state&)` function called by `refresh_display()`

#### Removed global state
| Global | Disposition |
|---|---|
| `terminal_framebuffer` | Moves into `render_context::window_snapshot` resolution. Each context tracks its own last-drawn cells per window. |
| `oversized_framebuffer` | Same — per-context tracking replaces global cache. |
| `winBuffer` | Per-context tracking. Each `window_snapshot` knows its snapshot history. |
| `fontScaleBuffer` | Removed — stale-skip logic becomes deterministic (windows are identified by handle in the snapshot, no font-size guessing). |
| `overmap_tilecontext` special-case in `draw_window()` | Removed — overmap renders through its own path, not through the main game's window dispatch. |

The `draw_window` dispatch table:
```cpp
// CURRENT: one giant if/else chain checking g->w_terrain, g->w_overmap, etc.
if( w == g->w_terrain ) { ... }
else if( w == g->w_overmap ) { overmap_tilecontext->draw_om(...); }
else { ... }
```

**Becomes**: `curses_drawwindow()` only captures snapshots. Neither `cata_tiles::draw()` nor `draw_om()` fire from window dispatch — they fire from redraw callbacks that explicitly receive their own context. The `tiles_redraw_info` bridge is no longer needed.

### 4.5. Refactor: `src/ui_manager.h/.cpp`

**Goal**: ui_manager orchestrates context creation and compositing.

```
ui_manager::redraw_invalidated() {
    auto frame_ctx = render_context{};
    for each active ui_adaptor in z-order:
        if adaptor is visible:
            auto layer_ctx = render_context{};
            adaptor.redraw_cb(layer_ctx);
            frame_ctx.merge(std::move(layer_ctx));
    refresh_display(frame_ctx);
}
```

Alternatively, to avoid N+1 render_context creations, pass `frame_ctx` directly and use `pixel_offset` to layer adaptors:

```cpp
ui_manager::redraw_invalidated() {
    auto frame_ctx = render_context{};
    for each active ui_adaptor in z-order:
        adaptor.redraw_cb(frame_ctx);
        // adaptor draws relative to its pixel_offset; compositor flushes
        // in adaptor stack order, so overmap (on top) draws last.
    refresh_display(frame_ctx);
}
```

- `background_pane` + `disable_uis_below` still prevents lower adaptors from drawing — that's correct, it's a semantic gate not a rendering gate.
- `clear_frame_queues()` is removed. A fresh `render_context` is zero-initialized.
- `composite()` step goes away (or becomes trivial — just iterate adaptors in order).

### 4.6. New: `src/overmap/overmap_renderer.h/.cpp`

**Goal**: Encapsulate all overmap-specific rendering state in a single class. No writes to global `game` members.

```cpp
class overmap_renderer {
public:
    overmap_renderer();

    auto draw( render_context &ctx ) -> void;

    auto handle_mouse_move( const point &screen_pos ) -> void;
    auto handle_click( const point &screen_pos ) -> void;
    auto handle_key( const input_event &evt ) -> bool;

    auto center() const -> point_abs_omt;

private:
    // Windows — private, not game:: members
    catacurses::window w_overmap;
    catacurses::window w_omlegend;
    catacurses::window w_border;

    // Tile rendering
    std::shared_ptr<cata_tiles> tilecontext;  // separate instance, always

    // State
    point_abs_omt center_;
    bool blink_;
    int zoom_;
};
```

The `draw()` method:
1. Renders legend text into `w_omlegend` via `mvwprintw` (standard curses)
2. Calls `curses_drawwindow()` on both windows → their contents go into `ctx.windows`
3. Calls `tilecontext->draw_om(ctx, center_, blink_)` → tiles go into `ctx.tile_sprites`
4. Cursor, crosshair, selection box drawn directly into `ctx.tile_sprites` or `ctx.ui_rects`

No `tiles_redraw_info` bridge, no `transparent_backdrop` toggle, no zoom save/restore on `game`.

### 4.7. Refactor: `src/overmap_ui.h/.cpp`

- Remove `tiles_redraw_info` struct + global
- Remove RAII zoom guard
- Remove RAII `transparent_backdrop` guard
- Remove `game::w_overmap` / `game::w_omlegend` writes
- Replace raw window handles with `overmap_renderer` instance
- `display()` creates an `overmap_renderer`, creates a `ui_adaptor` whose redraw callback calls `overmap_renderer::draw()`

### 4.8. Refactor: `src/game.h/.cpp`

- Remove `w_overmap`, `w_omlegend` members
- Remove overmap window creation from game startup / resize paths
- Remove overmap-related includes (or reduce to forward declarations)

### 4.9. Remove: `src/cursesport.h/.cpp` `transparent_backdrop` flag

- `set_window_transparent_backdrop()` → delete
- `WINDOW::transparent_backdrop` → delete
- `suppress_cell_bg()` → delete
- The compositor renders windows in z-order with proper alpha; no per-cell suppression needed

---

## 5. Migration strategy

Each step is independently buildable and testable.

### Step 1: `render_context` type (2-3 days)

- Create `src/lighting/render_context.h` with the struct
- Add parallel push methods to `render_state` that also accept a `render_context&`
- `render_state::flush_*(render_context&)` methods alongside existing `flush_*()` (which still flush the global queues)
- No behavior change. Builds, existing tests pass.

### Step 2: Migrate overmap tile path (2-3 days)

- `cata_tiles::draw_om()` takes a `render_context&` parameter (default to `lighting::get_render_state()` for backward compat)
- Overmap's draw path passes its own `render_context` instead of using the global
- Overmap tile sprites now live in a separate queue
- `redraw_invalidated()` merges overmap context into frame context before flush
- Verify: overmap still renders. Main game still renders. No regression in artifact reproduction tests.

### Step 3: Migrate curses window rendering (2-3 days)

- `curses_drawwindow(WINDOW*, render_context&)` — new path that captures snapshots
- `draw_window()` → `resolve_window_snapshots()` — moved logic, takes context
- `refresh_display()` resolves snapshots before flushing
- Overmap and main game each get their windows captured into the shared frame context
- `transparent_backdrop` toggle still exists but is now redundant (can be no-op)
- Verify: fonts work correctly, cell rendering matches baseline pixel-for-pixel

### Step 4: Overmap renderer class (2-3 days)

- Create `overmap_renderer` class
- Overmap `display()` uses it
- Remove `game::w_overmap` / `game::w_omlegend`
- Remove `tiles_redraw_info`
- Remove overmap special-casing from `game::draw()` and `draw_window()` dispatch
- Verify: overmap opens, renders, closes cleanly. Main game unaffected.

### Step 5: Delete dead code (1 day)

- Remove `transparent_backdrop` from `cursesport`
- Remove `winBuffer` / `fontScaleBuffer` / `terminal_framebuffer` / `oversized_framebuffer` globals
- Remove RAII guards in overmap_ui.cpp
- Remove `clear_frame_queues()` / `clear_*_queue()` methods
- Convert remaining `render_state` methods to exclusively take `render_context&`
- Verify: builds with zero warnings. Run full test suite.

---

## 6. Deleted code summary

| Code | File(s) | Lines |
|---|---|---|
| `transparent_backdrop` flag + getter/setter | `src/cursesport.h`, `src/cursesport.cpp` | ~15 |
| `set_window_transparent_backdrop()` | `src/sdltiles.cpp` | ~5 |
| `suppress_cell_bg()` | `src/sdltiles.cpp` | ~10 |
| `tiles_redraw_info` struct + global | `src/overmap_ui.h`, `src/overmap_ui.cpp` | ~15 |
| RAII zoom guard in `display()` | `src/overmap_ui.cpp` | ~10 |
| RAII backdrop guard in `display()` | `src/overmap_ui.cpp` | ~15 |
| `winBuffer` compatibility hacks | `src/sdltiles.cpp` | ~30 |
| `fontScaleBuffer` comparison workarounds | `src/sdltiles.cpp` | ~15 |
| `terminal_framebuffer` / `oversized_framebuffer` globals | `src/sdltiles.cpp` | ~20 |
| `game::w_overmap`, `game::w_omlegend` | `src/game.h`, `src/game.cpp` | ~10 |
| overmap window dispatch in `draw_window()` | `src/sdltiles.cpp` | ~15 |
| `clear_frame_queues()` | `src/ui_manager.cpp` | ~10 |
| `clear_tile_queue()` / `clear_font_queue()` / `clear_ui_queue()` | `src/lighting/render_state.h/.cpp` | ~20 |
| Global `queue_*()` methods | `src/lighting/render_state.h/.cpp` | ~40 |
| **Total** | | **~230 lines deleted** |

---

## 7. Risk table

| Risk | Likelihood | Mitigation |
|---|---|---|
| Pixel mismatch after snapshot→glyph conversion | Medium | Step 3: compare pixel output before/after with automated screenshot tests (or manual diff on a known scene) |
| Performance regression from extra snapshot copies | Low | `window_snapshot` uses move semantics; cells are shallow-copied once per frame; GPU upload unchanged |
| Overmap windows not properly sized after removing `game::` members | Low | Overmap creates its own windows in the `overmap_renderer` constructor; resize_cb on the overmap's ui_adaptor recreates them |
| Compositor z-ordering wrong | Medium | Compositor iterates ui_adaptor stack in order; topmost adaptor draws last → correct overlap. Verify with overmap open (should cover main game fully) |
| Breakage during migration (partial state) | Low | Each step is independently buildable. Deprecation path: old `queue_*()` methods forward to a deprecated global context; new callers use explicit contexts. Remove old path in step 5. |

---

## 8. Test plan

| Test | When | How |
|---|---|---|
| Overmap renders correctly (ASCII) | Step 2 | Manually open overmap in ASCII mode, verify terrain chars, legend, cursor |
| Overmap renders correctly (tiles) | Step 2 | Same with tiles mode |
| Main game renders correctly after overmap close | Step 2 | Close overmap, verify main view has no artifacts |
| Cell rendering pixel match | Step 3 | Screenshot comparison of a known scene before/after migration |
| Zoom restored correctly after overmap | Step 4 | Verify game zoom unaffected after overmap open/close |
| No window handle leaks | Step 4 | Open/close overmap 100 times, check for window count growth |
| Full test suite | Step 5 | `cmake --build build --target cata_test-tiles && cata_test-tiles` |
| No regressions in sidebar minimap | Step 5 | Minimap uses `draw_overmap_chunk()`; verify it still renders correctly after global queue removal |

---

## 9. Future work (out of scope)

- **Thread safety**: `render_context` is single-threaded. Future: one context per thread, composite at frame end.
- **Partial redraw**: Context-gating enables per-region invalidation (each window_snapshot knows its dirty rect). Not tackled here.
- **GPU particles**: Would be a separate `particle_context` composed the same way.
