# Rain Shader Performance Analysis + FPS Counter Plan

> **Revised 2026-06-26** — status: Part 1 mostly done; Part 2 not started; weather
> integration (outside scope of this plan) completed.

## Implementation Status

| Section | Status | Notes |
|---------|--------|-------|
| Part 1: FPS globals (`g_fps_avg`, `g_show_fps`) | ✅ Done | `sdl_render_frame.h:7`, `sdl_render_frame.cpp:44-47` |
| Part 1: FPS overlay in `composite_swapchain_pass_b` | ✅ Done | via `rmlui_layer::set_hud_text()` at `sdl_render_frame.cpp:818-830` |
| Part 1: `ACTION_TOGGLE_FPS` enum + string map | ✅ Done | `action.h:355`, `action.cpp:316-317` |
| Part 1: `toggle_debug_fps()` in game | ✅ Done | `game.h:1177`, `game.cpp:16034-16038` |
| Part 1: case handler in `handle_action.cpp` | ✅ Done | `handle_action.cpp:2841-2843` |
| Part 1: debug menu entry | ✅ Done | `debug_menu.cpp:1974-1976` |
| Part 1: default keybinding | ❌ Not done | `"debug_fps"` registered at `game.cpp:3252` but no key bound |
| Part 2: two-pass O(n) expiry | ❌ Not done | `rain_effect.cpp` still uses manual iterator erase |
| Weather hookup (new, post-plan) | ✅ Done | `weather_rain_intensity()` in `sdl_render_frame.cpp`; old rain suppressed via `g_rain_enable` in `handle_action.cpp` |

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

### Part 1: FPS counter — ✅ Done

#### Frame timing data — ✅ Done

`refresh_display()` already has a `frame_perf` RAII struct
(`src/sdl_render_frame.cpp:881-926`) that rolls render_body avg/max + frame_period avg +
derived fps every 120 frames into `DC::Main`. Those rolling values are lifted into globals:

- `src/sdl_render_frame.h`: declares `extern float g_fps_avg`,
  `extern float g_body_ms_avg`, `extern bool g_show_fps`. ✅
- `src/sdl_render_frame.cpp`: defines the globals; in `frame_perf::~frame_perf()` writes
  `g_fps_avg`/`g_body_ms_avg` from the rolling sums (keeps the 120-frame log; publishes
  the averages every frame so the overlay isn't pinned to 0 during the first window). ✅

#### Rendering — ✅ Done

Uses `rmlui_layer::set_hud_text()` instead of `world_text_add()` — a persistent,
non-accumulating path that sidesteps the `world_text_begin` lifecycle edge. The HUD text
is set in `composite_swapchain_pass_b` (`sdl_render_frame.cpp:818-830`), before
`rmlui_layer::new_frame()/prepare()`. The overlay pass runs when `world_text_active()`
is true (set_hud_text sets this). No new font code.

#### Toggle mechanism — ✅ Done (minus default keybinding)

| File | Status | Location |
|------|--------|----------|
| `src/action.h` | ✅ `ACTION_TOGGLE_FPS` enum | `action.h:355` |
| `src/action.cpp` | ✅ `"debug_fps"` mapping + category reg | `action.cpp:316-317, 957` |
| `src/game.h` | ✅ `toggle_debug_fps()` decl | `game.h:1177` |
| `src/game.cpp` | ✅ toggle impl + register (×2) + dispatch | `game.cpp:16034-16038, 3251-3252, 9785, 10035-10036` |
| `src/handle_action.cpp` | ✅ case handler | `handle_action.cpp:2841-2843` |
| `src/debug_menu.cpp` | ✅ menu entry | `debug_menu.cpp:1974-1976` |
| `src/input_default.cpp` | ❌ **Not done** — `"debug_fps"` registered at `game.cpp:3252` but no default key bound | |

### Part 2: Rain CPU fix — ❌ Not started

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

### Weather hookup (post-plan, completed 2026-06-26)

Outside the original plan scope, the GPU rain was using the dev slider `g_rain_intensity`
(default 0.5) instead of actual weather data, so rain never activated during gameplay.
Additionally, the old tile-based rain animation was still showing independently.

**Changes:**

| File | Change |
|------|--------|
| `src/sdl_render_frame.cpp` | Added `weather_rain_intensity()` helper that reads `weather_type_id->precip` and maps `very_light→0.1 / light→0.3 / medium→0.6 / heavy→1.0`. Falls back to dev slider when no game session. Used in both the specular wetness folding and the GPU rain spawn intensity. |
| `src/handle_action.cpp` | Old tile-based rain suppressed when `g_rain_enable` is true (GPU rain handles it). Falls back automatically if GPU rain is disabled. |

**Verification:** Drops only spawn on sky-exposed tiles (`mc.outside_cache` gate at
`sdl_render_frame.cpp:775`), so indoors/caves/roofed areas get no rain. Builds and
links cleanly.

## Files changed

| File | Δ lines | Purpose |
|------|---------|---------|
| `src/sdl_render_frame.h` | new (~8) ✅ | extern `g_fps_avg`/`g_body_ms_avg`/`g_show_fps` |
| `src/sdl_render_frame.cpp` | ~20 ✅ | publish `frame_perf` averages to globals + `set_hud_text` submit + `weather_rain_intensity()` helper |
| `src/action.h` | +1 ✅ | `ACTION_TOGGLE_FPS` |
| `src/action.cpp` | +2 ✅ | action→string map |
| `src/game.h` | +1 ✅ | `toggle_debug_fps()` decl |
| `src/game.cpp` | ~12 ✅ | toggle + register (×2) + dispatch |
| `src/handle_action.cpp` | +3 ✅ | case handler + old rain suppression via `g_rain_enable` |
| `src/input_default.cpp` | +1 ❌ | default key (F3) — action registered but no key bound |
| `src/debug_menu.cpp` | +2 ✅ | menu entry |
| `src/lighting/rain_effect.cpp` | ~10 ❌ | (opportunistic) two-pass O(n) expiry — still manual iterator erase |
| **Total** | **~70** | across 11 files (weather hookup added 2 files) |

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
