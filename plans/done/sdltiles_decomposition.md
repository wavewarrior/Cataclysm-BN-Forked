# ✅ ARCHIVED 2026-06-28 — fully executed, moved to plans/done/

## STATUS (reviewed 2026-06-27)
**~100% DONE — ARCHIVE.** sdltiles.cpp is now **289 lines** (from 4,293). All 6 proposed modules exist: `sdl_input`, `sdl_window_dims`, `sdl_fonts`, `sdl_overmap_draw`, `sdl_framebuffer`, `sdl_curses_draw` (+ `sdl_display.h` holding `display_context`/`g_display`). Phase 0 net landed: `render_state::capture_texture_to_rgba` + `tests/render_regression_test.cpp` both present. `refresh_display`/`InitSDL`/`WinCreate` gone from sdltiles.cpp. Plan matches reality and is fully executed — move to `plans/done/`. Continued by `sdltiles_decomposition_followup.md`.

# Decompose `sdltiles.cpp` — God-TU → Owned Modules

> Supersedes the original "Overmap Context Gating" draft. That plan attacked one
> symptom (shared draw queues); this plan removes the structural root cause — a
> 4,293-line god translation unit with no ownership boundaries.

## Context

`src/sdltiles.cpp` is a 4,293-line god translation unit with **~76 mutable file-scope statics** spanning ~10 unrelated domains (window lifecycle, input, fonts, framebuffer cache, curses→SDL dispatch, GPU driver, tileset/overmap, dimensions). The lack of ownership boundaries is the root cause of a recurring class of rendering bugs — most recently the overmap-black + vision-edge artifacts fixed in `ea0fb20e0a` / `1037b51840`. Those were symptom fixes; this plan removes the **structural cause**.

The acute bug is already fixed, so this is *hardening*, not a fire. It is justified because the fragility costs real velocity (the eyeball-debug loop documented across the lighting work) and because the codebase goal is "clean and solid, no wobbly foundations."

**Hard constraint:** render verification is currently eyeball-only — there is no automated render test. Refactoring a render pipeline you can only verify by eye trades known-fragile-but-working for unknown-broken. **Therefore Phase 0 builds a verification net first; every later step is gated by it.**

**Goal / endpoint:** `sdltiles.cpp` ends as a thin **composition-root** (Domain-A lifecycle + frozen cross-TU entry points only). All other domains move into owned modules that receive a `display_context&` by reference. Number of files is an *output* of fixing ownership — not the goal.

**Starting point:** clean working tree, all committed, branch `feature/improvements`.

---

## Frozen public contract (must NOT change signatures)

These cross-TU seams insulate the rest of the game; every extraction is internal to them:

- `catacurses::init_interface()`, `catacurses::endwin()` — `cursesport.h`, called from `init.cpp` + `~game`
- `cata_cursesport::curses_drawwindow(const catacurses::window&)` — **called directly (not a fn-pointer) from `cursesport.cpp:192`**
- `refresh_display()` — called from `catacurses::doupdate()`
- `input_manager::get_input_event() / pump_events() / set_timeout()` — `input.h`
- `get_window_dimensions()`, `get_sdl_window_size/font_size()`, `get_scaling_factor()`, `get_sdl_renderer/window()`
- **Tier-2 exported globals — permanent facade, never internalized:** `tilecontext`, `overmap_tilecontext` (`sdltiles.h:20-21`, used via extern in `cata_tiles.cpp` + game), `fontwidth`, `fontheight` (`extern int` in `cata_tiles.cpp:123-124`, pervasive layout), `windowsPalette` (`sdltiles.h:22`). These keep their current definitions in `sdltiles.cpp`.

---

## The ownership spine: `display_context`

A single owned struct holding the **Tier-1** shared SDL core currently scattered as statics:
`window`, `legacy_window`, `renderer`, `geometry`, `format`, `WindowWidth/Height`, `TERMINAL_WIDTH/HEIGHT`, `scaling_factor`, `fullscreen`, plus pure-internal statics (timing/dirty flags, dpad state, `inputdelay`, `last_input`, `joystick`).

- **One owned file-scope instance** `static display_context g_display;` — **NOT** a `get_display()`-reached-everywhere singleton (that is a global with a function in front of it = fake boundary). Singleton access is confined to the ~6 frozen spine entry points, which deref `g_display` once at the top and thread `display_context&` downward. Every extracted module takes `display_context&` as a parameter — that is where the boundary is real.
- **Default-constructible, staged init** — members filled across `InitSDL` → `WinCreate` → font-load (no all-args ctor).
- **Step-0 zero-touch bridge:** keep the 76 in-file refs compiling via reference-aliases to members, e.g. `static SDL_Renderer_Ptr &renderer = g_display.renderer;`. The member *address* is stable at static-init even though its *value* is filled at `WinCreate`. (A value-copy alias or accessor would bind to a not-yet-constructed instance — crash at first render. The reference-to-member is the load-bearing trick.) Each later extraction batch-migrates that domain's refs into its by-ref module and drops the aliases. End state: aliases gone, only Tier-2 facades remain global.

### Lifetime trap (verified, must mitigate)
`cata_tiles` holds `const SDL_Renderer_Ptr &renderer;` **by reference** (`cata_tiles.h:589`, and again at `:1213-1214` with `geometry`) — it aliases `g_display.renderer`, not an owning copy. The renderer/geometry must outlive `tilecontext.reset()`. **Mitigation:** explicitly null `g_display` members inside `WinDestroy`; preserve today's `endwin` order (contexts → fonts → `WinDestroy`); never rely on static-destruction order. This null-in-`WinDestroy` must land in the same commit that internalizes `renderer`.

---

## Extracted modules (new files)

| New files | Domain | Moves in | Interface back |
|---|---|---|---|
| `src/sdl_input.{h,cpp}` | Input | `CheckMessages`, `get_input_event`, `pump_events`, `set_timeout`, `HandleDPad`, `sdl_keysym_to_curses`, alt-code/arrow-combo; owns `joystick`/`last_input`/dpad/`inputdelay` | Helpers behind frozen `input_manager::*`; takes `display_context&` for window/mouse. Touches no renderer/geometry/fb/tiles — **cleanest seam** |
| `src/sdl_window_dims.{h,cpp}` | Dims + mouse-map | `get_window_dimensions` (×3), `get_sdl_window_size/font_size`, `projected_window_*`, `get_scaling_factor`, `input_context::get_coordinates` | Pure queries over `display_context` + `fontwidth/height` |
| `src/sdl_fonts.{h,cpp}` | Fonts | `draw_string`, `draw_sdl_text_outlined`, `map_font_*`, `to/from_map_font_dimension`; owns the three `Font_Ptr`s | Frozen `draw_sdl_text_outlined`; reads `windowsPalette`/`fontwidth/height` |
| `src/sdl_overmap_draw.{h,cpp}` | Overmap render | `cata_tiles::draw_om`, `get_omt_id_rotation`, `get_mission_arrow`, `rescale_tileset` | Uses `overmap_tilecontext` facade + `display_context` |
| `src/sdl_framebuffer.{h,cpp}` | **Bug seam** | dirty-cell cache (`terminal_/oversized_framebuffer`, `winBuffer`, `fontScaleBuffer`), `reinitialize_framebuffer`, `invalidate_framebuffer(_proportion)` | **Window-family-keyed self-validating cache** |
| `src/sdl_curses_draw.{h,cpp}` | **Bug seam** | `curses_drawwindow`, `draw_window`, window-path `draw_string`, `clear_window_area` | Frozen `cata_cursesport::curses_drawwindow` |

- **Domain F (GPU)** already partially extracted (`lighting/frame_build.cpp`). Leave `refresh_display` in `sdltiles.cpp` as a thin spine fn calling `lighting::` + the fb module.
- **Domain A (lifecycle) + the `get_sdl_*` accessors STAY** in `sdltiles.cpp` (composition root).
- Naming: use `sdl_framebuffer` (cache) and `sdl_curses_draw` (dispatch) — avoid ambiguous "window_cache".

---

## Phase 0 — Verification net (numeric-oracle, no golden PNGs)

**Decision: numeric/structural oracle only** — no committed golden images, no eyeball-approval, no per-platform golden dirs. Portable across Metal (dev) and D3D12 (target).

Reuse, don't build from scratch:
- `tests/lighting_gpu_test.cpp` — headless hidden-window `gpu_device` init + `[.gpu]` manual Catch2 tag.
- `save_screenshot()` (`sdltiles.cpp:4170-4267`) — already does offscreen render → `SDL_DownloadFromGPUTexture` → transfer buffer → readback. **Factor the readback half out** as a reusable `capture_texture_to_rgba(render_state&, SDL_GPUTexture*, w, h, std::vector<uint8_t>&)` in `render_state.{h,cpp}`; re-point `save_screenshot` at it (DRY, behaviour identical, verify byte-identical screenshot after).
- `radiance_cascade_pass::debug_log_stats()` (`radiance_cascade_pass.cpp:320-387`) — the region-stats (sum/max/nonzero/centroid) oracle pattern.
- `tests/CMakeLists.txt:2-3` GLOBs `tests/*.cpp` with `CONFIGURE_DEPENDS` → **a new `tests/render_regression_test.cpp` auto-compiles into `cata_test-tiles`; no CMake edit.**

Harness shape (`tests/render_regression_test.cpp`, tag `[.gpu][render]`, run `cata_test-tiles "[render]"`):
- **Single `TEST_CASE` with `SECTION`s** — the harness uses the process-wide `get_render_state()` singleton; one window + one init/shutdown for the file (never multiple TEST_CASEs sharing the singleton).
- **Synthetic deterministic scene through the real pipeline** — hand-built `sprite_instance`s queued via `queue_tile_sprite(white_texture()/fixture, inst)` (no loaded game/tileset needed — `cata_tiles` is only the in-game producer). Capture `world_ldr_target()` (deterministic, no ImGui), **never the swapchain**.
- **Determinism pins** (or it flickers): fixed `debug_params` with `anim_time=0`, `sway_amp=0` (do NOT copy `g_dbg_params`); hardcoded tonemap exposure/EV (do NOT read the `g_tonemap_*` F4 globals); ordering `begin_lighting_frame(in)` **before** `tile_batcher().begin_pass(...)`.
- **Oracles** (`compute_region_stats(rgba, rect)` mirroring `debug_log_stats`): region mean ≈ expected; "lit half brighter than shadowed half"; "retained frame-2 == frame-1" (driver-independent equality — covers the partial-redraw retention class); "UI region differs from bare world".

Fixtures (the scenarios that actually cover the decomposition's risk):
- **A — compositor:** UI rects + glyphs → offscreen → stats. Covers `flush_tile_sprites`+`flush_ui`.
- **B — world-lit:** checker tile grid + fixed sun/ambient (no-SDF path) → `world_ldr_target`. Covers Pass W + Pass T + uniform-push ordering.
- **D — ui-over-world:** B into world_ldr + UI into `ui_target()`; replicate Pass B `blit_layer` (world then UI) into a harness offscreen. Covers straight-alpha composite + slice z-order.
- **E — retention:** frame 1 full tiles → capture; frame 2 `clear_tile_queue()` only, no enqueue, run Pass-W skip gate + Pass T → assert world **retained** (== frame 1), not black.
- **C — curses-cell→glyph (the real gate):** drives the cell→glyph translation in `draw_window` (the riskiest decomposition target, bypassed by direct `queue_*` calls). **Chicken/egg:** if `draw_window` is too coupled to `Font`/`WINDOW` globals to drive pre-decomposition, the first dispatch-extraction commit must expose a testable `emit_window_cells(WINDOW&, font, render_state&)` seam, and C goldens *that* seam. Investigate coupling at Phase 6 before fixing C's shape.

---

## Extraction order (each step independently build-green + shippable)

✅ ALL STEPS DONE (0–7b). Verified 2026-06-27 by file presence; see STATUS banner.

| Step | What | Why this order | Gate |
|---|---|---|---|
| 0 | Verification net (Phase 0) + DRY `capture_texture_to_rgba` | Foundation under everything; refactor is gated on it | `[render]` green; live screenshot byte-identical |
| 1 | `display_context` + Tier-1 reference-alias bridge | Storage-move, zero behavior change; proves the spine | Build green + smoke run |
| 2 | **Input → `sdl_input`** | Cheapest clean seam, ~0 render risk; proves the `display_context&`-by-ref module pattern | `[render]` green; keyboard/mouse/dpad work |
| 3 | **Dims+mouse → `sdl_window_dims`** | Pure queries, low risk | Window sizing + mouse-coord + fullscreen dims correct |
| 4 | **Fonts → `sdl_fonts`** | Self-contained font ownership before dispatch consumes it | All three fonts render; outlined text intact |
| 5 | **Overmap-draw → `sdl_overmap_draw`** | Isolates overmap render before touching shared fb | Overmap renders; arrows + rotation correct |
| 6a | **Framebuffer extract** (`sdl_framebuffer`, behavior-preserving, shared buffers) | Move with no logic change | Build green; no visual change main↔overmap |
| 6b | **Framebuffer ownership fix** (THE FIX) | Behavior change isolated for bisect | **Visual smoke** (below) + `[render]` retention oracle |
| 7a | **Dispatch extract** (`sdl_curses_draw`, pass `window_family` enum to decouple from `g`) | Move dispatch behavior-preserving | Build green; all windows draw |
| 7b | **Dispatch wire** self-validating cache signature | Completes the fix | **Visual smoke** + `[render]` |

`build-green ≠ render-correct` for dirty-cell logic — that is why 6/7 split into a move-commit then a fix-commit (bisectable) and carry a **manual visual smoke** gate, not just compile + oracle.

---

## The bug-seam fix (Steps 6–7): fix, not relocate

**Defect (verified, lines):**
- `sdltiles.cpp:2506` reads `fontScale = tilecontext->get_tile_width()` **unconditionally** — always the *main* context, even when drawing an overmap window. The original author's comment at `:2507-2509` documents exactly this: *"This creates a problem when map_font is different from the regular font / Specifically when showing the overmap."*
- `:2516` classifies the window via `g && (w == g->w_terrain || w == g->w_overmap)` — couples the cache to `game.h`.
- `:2594-2595` skip-gate `cell == oldcell && fontScale == fontScaleBuffer` — with a single `fontScaleBuffer` tracking only the main context, the scale guard never blocks the overmap's stale-skip → stale cells survive a main↔overmap flip.

**Why per-`cata_tiles`-context keying is insufficient:** (1) when `tilesName == omTilesName` the contexts are the *same instance* (`overmap_tilecontext = tilecontext`, `:3776`/`:3843`) → a pointer key collapses both families; (2) the divergence axis is the **FONT** (`map_font` vs `overmap_font` are always separate `Font_Ptr`s, even with identical tilesets), not the context.

**The fix:** the cache lives in `sdl_framebuffer` (it is a curses concept — `curseline`/`cursecell`). **Key by window-family** (`terminal` / `terrain-map` / `overmap`), passed in as an enum by the caller — this both removes the fb module's dependency on `g` and supplies the right cache key. Each per-family cache is **self-validating**: it stores the font-metric `(w,h)` + scale signature it was built under and **refuses the dirty-skip on signature mismatch**, sourcing scale from the *matching* context (`overmap_tilecontext` for the overmap family). The stale-skip becomes structurally impossible.

**Empirical gate at implementation time:** confirm the repro triggers on `map_font != overmap_font` *alone* (not requiring `tiles != omtiles`). That proves family-keying over context-keying is the correct axis.

---

## Critical files

- `src/sdltiles.cpp` — statics `110-167`; framebuffer `1623-1709`; **bug-seam dispatch `2492-2621`**; lifecycle `3725-3870`; `save_screenshot` `4170-4267`; `refresh_display` Pass A/W/T/B `900-1606`
- `src/sdltiles.h` — Tier-2 extern contract + frozen query signatures
- `src/cursesport.cpp:192` — direct `curses_drawwindow` call; `handle_additional_window_clear`
- `src/cata_tiles.h:589`, `:1213-1214` — `const SDL_Renderer_Ptr &renderer;` (the lifetime trap)
- `src/lighting/render_state.{h,cpp}` — sole GPU interface; add `capture_texture_to_rgba`; `begin_lighting_frame` `274-318`
- `src/lighting/frame_build.{h,cpp}` — Domain-F extraction precedent (the self-contained-module pattern to mirror)
- `tests/lighting_gpu_test.cpp` — headless device + `[.gpu]` pattern to reuse
- `tests/render_regression_test.cpp` — **new**, the harness

---

## Risks

| # | Risk | Mitigation |
|---|---|---|
| 1 | Tier-2 externs (`fontwidth`/`tilecontext`/`windowsPalette`) break linkage if moved | Keep as permanent global facades in `sdltiles.cpp`; `display_context` never owns them |
| 2 | Static-init/destruction order: `cata_tiles` aliases `g_display.renderer` by reference → dangle/double-free at exit | Null `g_display` members in `WinDestroy` (same commit as renderer internalization); preserve `endwin` order; never rely on static-dtor order |
| 3 | `cursesport.cpp:192` calls `curses_drawwindow` directly — signature frozen | Keep exact free-fn signature in `sdl_curses_draw`; deref `g_display` internally |
| 4 | Bug "fix" merely relocates the stale-skip | Key by window-family + self-validating font/scale signature; verify repro on `map_font != overmap_font` alone |
| 5 | `display_context` degrades into `get_display()`-everywhere (fake boundary) | Singleton access confined to ~6 spine entry points; every module takes `display_context&` |
| 6 | `build-green ≠ render-correct` for dirty-cell logic | Steps 6/7 split move-commit then fix-commit; gated by visual smoke + retention oracle |
| 7 | Fixture C depends on a dispatch seam that doesn't exist yet (chicken/egg) | Make extracting `emit_window_cells` the first dispatch commit; C goldens that seam |

---

## Verification

- **Per step:** `cmake --build build --target cata_test-tiles && ./cata_test-tiles "[render]"` (numeric oracle, green) + build-green of the full game.
- **Steps 6b / 7b (bug-seam) — manual visual smoke (the human gate the oracle can't fully replace):** open game with `map_font` ≠ `overmap_font`; flip main↔overmap repeatedly; toggle fullscreen; confirm **no stale cells / no black overmap**. Repro must trigger on font divergence alone.
- **End state:** `sdltiles.cpp` reduced to lifecycle + frozen entry points; `grep -c "^static" src/sdltiles.cpp` collapses from ~76 toward the handful of lifecycle/`g_display` items; each module takes `display_context&` (no module reaches an ambient global for `renderer`/`geometry`).
