# Rain Shader Performance Analysis + FPS Counter Plan

> **Revised 2026-06-24** after a source review. Two bugs in the original Part 1/Part 2
> were corrected, a redundant new font path was dropped in favour of the existing RmlUi
> world-text overlay, and the ordering was flipped: **build the FPS counter first as a
> measurement tool, then decide whether the rain CPU path is worth optimizing.**

## Rain Shader Analysis

The rain shaders themselves are **not** the performance problem. All three are trivial
procedural shaders with zero texture lookups, zero loops, and ~6--15 ALU ops each:

| Shader                         | Lines | ALU  | Tex | Loops | Branches  |
|--------------------------------|-------|------|-----|-------|-----------|
| `rain_droplet.vert.hlsl`       | 57    | ~15  | 0   | 0     | 0         |
| `rain_droplet.frag.hlsl`       | 31    | ~6   | 0   | 0     | 1 (early-Z) |
| `rain_splash.frag.hlsl`        | 55    | ~12  | 0   | 0     | 1 (early-Z) |

For comparison, the main `sprite.frag.hlsl` (799 lines) has 5 texture samples,
up to 8192 emitters processed in two-pass loops (×16 shadow steps each), making
the rain shaders negligible by comparison.

The CPU side of `rain_effect::record()` (`src/lighting/rain_effect.cpp`) has four costs:

1. Per-drop world→screen projection, wind lean, off-screen cull — O(n)
2. **`droplets_.erase(it)` in a while-loop** (lines 322--328) — O(n²) worst case
   because `std::vector::erase` shifts subsequent elements
3. Two GPU copy passes (transfer→storage buffer) per frame
4. One render pass on `world_target` (LOADOP_LOAD)

**Caveat — none of this is measured yet.** `MAX_DROPLETS = 4096` (`rain_effect.h:133`),
and only a fraction expire per frame (`DESCENT = 0.13` tiles/frame), so the realistic erase
cost is well under a millisecond — the O(n²) is real but not obviously the FPS mover. If rain
is genuinely expensive, costs **#3/#4 (GPU copies + extra pass)** are the more likely culprits,
and the erase fix touches neither. The `[render][perf]` log already distinguishes render-bound
from sim-bound; the live debug.log currently holds almost no samples (game barely ran with rain
on). **Measure before optimizing** — which is exactly what Part 1 (the counter) gives us.

## Plan

### Part 1: FPS counter (build first — it's the measurement tool)

#### Frame timing data — reuse existing infra

`refresh_display()` already has a `frame_perf` RAII struct
(`src/sdl_render_frame.cpp:792-819`) that rolls render_body avg/max + frame_period avg +
derived fps every 120 frames into `DC::Main`. Lift those rolling values into globals:

- `src/sdl_render_frame.h` (new, ~8 lines): declare `extern float g_fps_avg`,
  `extern float g_body_ms_avg`, `extern bool g_show_fps`.
- `src/sdl_render_frame.cpp`: define the globals; in `frame_perf::~frame_perf()` write
  `g_fps_avg`/`g_body_ms_avg` from the rolling sums (keep the 120-frame log; just also
  publish to the globals, optionally on a shorter ~per-second cadence for a livelier readout).

#### Rendering — reuse the RmlUi world-text overlay (no new font path)

Do **not** add a new `glyph_gpu_data`/`get_glyph_gpu`/`render_string_gpu` path to `sdl_font`.
That duplicates `CachedTTFFont::OutputChar` (`sdl_font.cpp:430-464`), and — critically — its
glyphs go through the **font glyph queue**, which `composite_swapchain_pass_b` does **not**
flush (it only blits the world/UI layer quads then `end_pass()` with the RmlUi overlay fn), so
queuing there renders nothing.

Instead use the existing always-on text overlay that already survives the curses rip-out and
does **not** gate input (`src/lighting/rmlui_layer.h:127-150`):

```cpp
void world_text_begin();                                   // clears this frame's items
void world_text_add( float sx, float sy, const std::string &utf8, unsigned int rgba );
bool world_text_active();                                  // render-gate
```

SCT and overmap labels already feed this (`sdl_overmap_draw.cpp:241`, `sdl_curses_draw.cpp:351`).
World-text items are compiled to geometry in `rmlui_layer::prepare()` and drawn in
`render_in_pass()`, both invoked at the **top of `composite_swapchain_pass_b`**
(`sdl_render_frame.cpp:744-748`, gated on `active() || world_text_active()`).

So the FPS submit goes at the top of `composite_swapchain_pass_b`, **before**
`rmlui_layer::new_frame()/prepare()`:

```cpp
if( g_show_fps ) {
    const std::string text = string_format( "FPS: %.0f  body: %.1fms", g_fps_avg, g_body_ms_avg );
    rmlui_layer::world_text_add( 12.f, 12.f, text, 0xFFFFFFFFu );   // top-left, opaque white
}
```

This sets `world_text_active()` → the overlay pass runs even with no menu/SCT, and the text is
drawn through RmlUi's own font engine. No new font code, no pass-flush bug.

> **One lifecycle edge to settle during impl:** `world_text_begin()` clears the frame's queue
> and is called per draw-context (overmap/curses, mutually exclusive per frame). The FPS item
> must be appended **after** the active context's begin so it coexists with SCT, and must not
> leak on no-redraw frames (where `refresh_display` re-drains stale queues). Resolve by either
> owning a single per-frame `world_text_begin()` site, or re-submitting the FPS item each
> `composite_swapchain_pass_b` call (which runs every frame) — verify against a frame with SCT
> active so the two don't clobber each other.

#### Toggle mechanism

Follow `ACTION_TOGGLE_HOUR_TIMER` exactly (verified: `action.cpp:314`, `game.cpp` register
×2 at 3251/9785 + dispatch 10031 + method 15985, `handle_action.cpp:2836`):

| File | Change |
|------|--------|
| `src/action.h` | Add `ACTION_TOGGLE_FPS` enum after `ACTION_TOGGLE_HOUR_TIMER` |
| `src/action.cpp` | Map `ACTION_TOGGLE_FPS` → `"debug_fps"` |
| `src/game.h` | Add `void toggle_debug_fps()` (the `bool` lives in `g_show_fps`) |
| `src/game.cpp` | Implement toggle (flip `g_show_fps`), register action in both keybinding contexts, dispatch handler |
| `src/handle_action.cpp` | Case → `toggle_debug_fps()` |
| `src/debug_menu.cpp` | Add "FPS counter" entry (toggle) |
| `src/input_default.cpp` | Bind `F3` → `"debug_fps"` (confirm F3 is unbound first) |

> **Cheaper alternative:** the F4 RmlUi dev panel already exists; adding an FPS line/checkbox
> there would skip the entire 7-file action plumbing. Use the dedicated `ACTION_TOGGLE_FPS`
> only if a standalone hotkey is wanted.

### Part 2: Rain CPU fix (opportunistic — gate on the numbers from Part 1)

Only pursue this once the counter shows rain actually moves render_body. When it does, fix
the O(n²) expiry. The current loop does **three** things per droplet — build the streak
instance, advance `d.fall -= DESCENT`, and erase-on-impact — so it can't be replaced wholesale
by a partition/erase. Split into two passes:

```cpp
// Pass A: advance + build instances (no structural mutation).
for( rain_droplet &d : droplets_ ) {
    // ... existing projection + on-screen streak push into droplet_inst ...
    d.fall -= DESCENT;
}

// Pass B: single O(n) expiry pass; predicate spawns the splash for dead drops.
std::erase_if( droplets_, [&]( rain_droplet &d ) {
    if( d.fall <= 0.f ) {
        add_splash( d.world_x, d.world_y, d.opacity );   // ring at the landing tile
        return true;
    }
    return false;
} );
```

`std::erase_if` calls the predicate exactly once per element, so the `add_splash` side effect
is safe, and it is a single O(n) pass. Droplet order is purely visual, so `stable_partition`
(and even `partition`) would only add cost — `erase_if` is the simplest correct form.

## Files changed

| File | Δ lines | Purpose |
|------|---------|---------|
| `src/sdl_render_frame.h` | new (~8) | extern `g_fps_avg`/`g_body_ms_avg`/`g_show_fps` |
| `src/sdl_render_frame.cpp` | ~20 | publish `frame_perf` averages to globals + `world_text_add` submit |
| `src/action.h` | +1 | `ACTION_TOGGLE_FPS` |
| `src/action.cpp` | +2 | action→string map |
| `src/game.h` | +1 | `toggle_debug_fps()` decl |
| `src/game.cpp` | ~12 | toggle + register (×2) + dispatch |
| `src/handle_action.cpp` | +3 | case handler |
| `src/input_default.cpp` | +1 | default key (F3) |
| `src/debug_menu.cpp` | +2 | menu entry |
| `src/lighting/rain_effect.cpp` | ~10 | (opportunistic) two-pass O(n) expiry |
| **Total** | **~60** | across 10 files |

## Verification

1. Build: `cmake --build out/build/osx-arm-slim --target cataclysm-bn-tiles`
   (check binary mtime relinked — a stale binary can read as a passing build).
2. Toggle the counter (F3 / debug menu), confirm it renders top-left with no menu open
   **and** still shows correctly while SCT is active (no clobber).
3. Enable rain, play ~30s, then compare `render_body avg` vs `frame_period avg` in
   `grep '\[render\]\[perf\]' "~/Library/Application Support/Cataclysm-BN/config/debug.log"`
   to classify render- vs sim-bound — this decides whether Part 2 is worth doing.
4. If Part 2 lands: confirm rain still falls + splashes (visual) and droplet count holds near
   the cap without stutter.
