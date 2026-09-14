# Main Menu Rework — Execution Plan

## Context

The current main menu is a 1215-line god file with a flat single-panel RMLUI layout: ASCII logo → version → horizontal tab row → inline submenu/MOTD → tips. It uses the gruvbox-dark palette inherited from the shared theme. The menu is functional but visually bland — no background atmosphere, no decorative framing, no visual hierarchy. We're reworking it into a full CoQ-inspired redesign: dark runic gold palette, animated plexus particle background, split-panel layout with top navigation bar, styled ASCII art logo with subtitle, and decorative runic framing.

**Branch:** `MainMenu`

## Approach

### Phase 1: Noise Utility Extraction

**Goal:** Extract `corr_hash`/`corr_vnoise` from `rmlui_proc_texture.cpp` into a shared header so both the runic frame and the plexus can use the noise flow field.

**Step 1.1: Create `src/lighting/noise_utils.h`**

Pure-function header extracting the noise helpers from `rmlui_proc_texture.cpp:50-77`. Exact copy, no modification:

```cpp
#pragma once
#ifndef CATA_SRC_LIGHTING_NOISE_UTILS_H
#define CATA_SRC_LIGHTING_NOISE_UTILS_H

#include <cstdint>

// Integer hash (3 ints -> uint32) and bilinearly-interpolated value noise.
// Seeded so a given position is deterministic across launches.
// Extracted from rmlui_proc_texture.cpp for shared use (runic frame + plexus).
inline std::uint32_t corr_hash(int x, int y, unsigned seed) {
    std::uint32_t h = seed * 2166136261u;
    h = (h ^ static_cast<std::uint32_t>(x)) * 16777619u;
    h = (h ^ static_cast<std::uint32_t>(y)) * 16777619u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

inline double corr_hash01(int gx, int gy, unsigned seed) {
    return (corr_hash(gx, gy, seed) & 0xffffffu) / static_cast<double>(0x1000000);
}

inline double corr_vnoise(int x, int y, int grid, unsigned seed) {
    const int G = grid < 1 ? 1 : grid;
    const int gx = (x >= 0 ? x : x - G + 1) / G;
    const int gy = (y >= 0 ? y : y - G + 1) / G;
    double fx = (x - gx * G) / static_cast<double>(G);
    double fy = (y - gy * G) / static_cast<double>(G);
    fx = fx * fx * (3.0 - 2.0 * fx);
    fy = fy * fy * (3.0 - 2.0 * fy);
    const double a = corr_hash01(gx, gy, seed);
    const double b = corr_hash01(gx + 1, gy, seed);
    const double c = corr_hash01(gx, gy + 1, seed);
    const double d = corr_hash01(gx + 1, gy + 1, seed);
    const double top = a + (b - a) * fx;
    const double bot = c + (d - c) * fx;
    return top + (bot - top) * fy;
}

#endif // CATA_SRC_LIGHTING_NOISE_UTILS_H
```

**Step 1.2: Update `src/lighting/rmlui_proc_texture.cpp`**

- Add `#include "noise_utils.h"` after existing includes.
- Delete the local `corr_hash`, `corr_hash01`, `corr_vnoise` definitions (currently lines 50-77).
- All other code (including `corrode_keep` which calls `corr_vnoise`) remains unchanged — it now uses the header version.

**Step 1.3: Update `src/lighting/CMakeLists.txt`**

Add `noise_utils.h` to the header glob or explicit header list. Verify it compiles by building the lighting target.

**Dependencies:** None. Can run in parallel with Phase 2.

---

### Phase 2: Plexus Background System

**Goal:** Animated plexus particle network rendered at ~10fps behind the RMLUI menu panel. The plexus is a **CPU simulation + CPU rasterizer** producing an RGBA pixel buffer; the RMLUI layer uploads that buffer to a `Rml::Texture` and draws it as a fullscreen quad. This fits the codebase's two render paths without inventing GPU calls: SDL3's GPU API has NO immediate-mode line/point primitives (`SDL_GPURenderLines`/`SDL_GPURenderPoints` do NOT exist — only `SDL_RenderLines` on the legacy `SDL_Renderer`, which the lighting layer does not use), and every lighting effect (`rain_effect`, `sound_wave_pass`, `hud_particle_effect`) is a full graphics-pipeline + shader. A CPU rasterizer at 120 particles / ~10fps is trivially cheap and avoids all of that.

**Files:** `src/lighting/menu_plexus.h` (new), `src/lighting/menu_plexus.cpp` (new), `src/lighting/CMakeLists.txt` (modify), `src/main_menu.cpp` (modify), `src/main_menu.h` (modify), `src/lighting/rmlui_layer.cpp` (modify)

**Step 2.1: Create `src/lighting/menu_plexus.h`**

Pure CPU module — no SDL/GPU types in the header:

```cpp
#pragma once
#ifndef CATA_SRC_LIGHTING_MENU_PLEXUS_H
#define CATA_SRC_LIGHTING_MENU_PLEXUS_H
#include <cstdint>
#include <vector>

namespace lighting {

struct plexus_particle {
    float x, y;       // position in pixel coords
    float vx, vy;     // velocity
    float life;       // 0.0-1.0, pulsing alpha
    std::uint8_t glyph; // 0=none, 1-4=Elder Futhark runes
};

struct plexus_config {
    int particle_count = 120;
    int connection_dist = 140;
    float speed_scale = 0.4f;
    int node_radius = 2;
    // Colors (RGB; alpha computed per-pixel from life/distance)
    std::uint8_t bg_r = 10, bg_g = 10, bg_b = 15, bg_a = 255;
    std::uint8_t line_r = 161, line_g = 136, line_b = 95, line_a = 60;
    std::uint8_t node_r = 196, node_g = 168, node_b = 50, node_a = 180;
    std::uint8_t glyph_r = 90, glyph_g = 176, glyph_b = 160, glyph_a = 200;
    int advance_every_n_frames = 6;  // ~10fps at 60hz
    bool enabled = true;
};

void plexus_init();
void plexus_finish();
// (Re)allocate the pixel buffer + respawn particles for a new size.
void plexus_resize(int width, int height);
// Advance simulation one step and rasterize into the pixel buffer.
void plexus_step();
// The RGBA8888 pixel buffer (width*height*4 bytes), or empty if unsized.
const std::vector<std::uint8_t>& plexus_pixels();
int plexus_width();
int plexus_height();
// Incremented each plexus_step(); the RMLUI layer re-uploads when it changes.
unsigned plexus_generation();
plexus_config& plexus_get_config();

// Visibility gate — set true when main menu is active, false otherwise.
extern bool g_plexus_visible;

} // namespace lighting
#endif // CATA_SRC_LIGHTING_MENU_PLEXUS_H
```

**Step 2.2: Create `src/lighting/menu_plexus.cpp`**

Anonymous-namespace state: `std::vector<plexus_particle> g_particles;`, `std::vector<std::uint8_t> g_pixels;`, `int g_w, g_h;`, `unsigned g_gen;`, `int g_frame;`, `plexus_config g_cfg;`. Include `noise_utils.h`, `<cmath>`, `<random>`.

Local rasterizer helpers (do NOT reuse `draw_stroke`/`put` — they are in `rmlui_proc_texture.cpp`'s anonymous namespace, TU-private):
- `blend_px(int x, int y, uint8_t r, g, b, a)` — alpha-over composite into `g_pixels` at (x,y), bounds-checked. `out = src*a + dst*(1-a)`.
- `draw_line(x0,y0,x1,y1, r,g,b,a)` — Bresenham/DDA line, calls `blend_px` per step.
- `fill_circle(cx,cy,radius, r,g,b,a)` — filled disc via bounding-box scan + radius test.
- `draw_glyph(cx,cy, glyph_id, r,g,b,a)` — draws one of 4 hardcoded Elder Futhark rune stroke sets (each rune = 2-4 line segments on a small local grid, e.g. Isa=1 vertical line, Algiz=vertical + 2 upper diagonals, Gebo=an X, Thurisaz=vertical + a right-side triangle). Segments defined as static coordinate arrays scaled to ~16×24px, drawn via `draw_line`.

`plexus_resize(w,h)`: set `g_w/g_h`, resize `g_pixels` to `w*h*4`, respawn `g_particles` (size `particle_count`) with `std::mt19937` seeded constant 0xB17E; random x∈[0,w), y∈[0,h), zero velocity, `life=rand[0,1]`, `glyph = (i % 10 == 0) ? (i % 4) + 1 : 0`.

`plexus_step()`:
1. `g_frame++`.
2. Advance each particle: `float n = corr_vnoise((int)p.x, (int)p.y, 64, g_frame / 30); float ang = n * PI * 4.0f; p.vx = (p.vx + cosf(ang)*speed_scale) * 0.98f; p.vy = (p.vy + sinf(ang)*speed_scale) * 0.98f; p.x += p.vx; p.y += p.vy;` — wrap x/y modulo (g_w,g_h). `p.life = sinf(g_frame*0.015f + i*0.7f)*0.5f + 0.5f`.
3. Clear buffer: fill every pixel with (bg_r,bg_g,bg_b,bg_a).
4. Connection lines: O(n²) over particle pairs; for `dist < connection_dist`, `float a = (1.0f - dist/connection_dist) * fminf(p_i.life,p_j.life) * (line_a/255.0f); draw_line(...)` with alpha `a*255`.
5. Nodes: for each particle, `fill_circle(p.x, p.y, node_radius, node_r,node_g,node_b, p.life*node_a)`.
6. Glyphs: for each particle with `glyph != 0`, `draw_glyph(p.x, p.y, p.glyph, glyph_r,glyph_g,glyph_b, p.life*glyph_a)`.
7. `g_gen++`.

`plexus_pixels/width/height/generation/get_config`: trivial accessors. `plexus_init`: zero state. `plexus_finish`: clear vectors.

**Step 2.3: Wire the simulation into the main menu loop**

**Modify `src/main_menu.h`:** add member `int plexus_frame_counter = 0;`

**Modify `src/main_menu.cpp`:**
- Add `#include "menu_plexus.h"`.
- In `opening_screen()` before the `while(!start)` loop: `lighting::g_plexus_visible = true;` and `lighting::plexus_resize(projected_window_width(), projected_window_height());` — use `projected_window_width()`/`projected_window_height()` from `sdltiles.h` which return pixel dims.
- In the `ui.on_screen_resize` callback (line 630): add the same `plexus_resize(...)` call.
- In the main loop after `ui_manager::redraw()` (line 638): `plexus_frame_counter++; if (plexus_frame_counter % lighting::plexus_get_config().advance_every_n_frames == 0) { lighting::plexus_step(); }`.
- On both exit paths (the `return false` at line 686 for quit, and after the loop before `return true` at line 808): `lighting::g_plexus_visible = false;`. Simplest: set it right before every `return` in `opening_screen`, or wrap with an `on_out_of_scope` guard that resets it.

**Step 2.4: Composite the plexus as a fullscreen quad in the RMLUI render pass**

The plexus pixel buffer is uploaded to a `Rml::Texture` and drawn as a fullscreen quad in `rmlui_layer.cpp::render_in_pass()` (line 1090) — the one site holding the live `SDL_GPURenderPass*`/`SDL_GPUCommandBuffer*`. It draws BEFORE world text (line 1103) and documents (line 1105); the menu panel's semi-transparent background (`rgba(10,10,15,0.88)`, Phase 3) lets it bleed through. This mirrors the existing `world_text_geom` pattern exactly (`Rml::Geometry` + `Rml::Texture` + `.Render(pos, texture)`).

**Modify `src/lighting/rmlui_layer.cpp`:**
- Add `#include "menu_plexus.h"`.
- Add file-scope state near `g_world_geom` (line 131): `Rml::Geometry g_plexus_geom; Rml::Texture g_plexus_tex; unsigned g_plexus_uploaded_gen = 0; int g_plexus_tex_w = 0, g_plexus_tex_h = 0;`
- Add a helper `rebuild_plexus_geom()` that, when `plexus_generation() != g_plexus_uploaded_gen` or the size changed: calls `render_manager.GenerateTexture(span_of(plexus_pixels()), {plexus_width(), plexus_height()})` to make a fresh `Rml::Texture` (release the prior via the render manager), builds a 2-triangle quad mesh spanning (0,0)-(w,h) with UVs (0,0)-(1,1) via `rm.MakeGeometry(mesh)`, and records the generation. Use the render manager already used by `build_world_text` (line 181, `rm.MakeGeometry`); `GenerateTexture` is on the render interface (`rmlui_render_interface.cpp:785`).
- In `render_in_pass()`, change the early-out (line 1093) to also stay alive when the plexus is visible: `const bool px = lighting::g_plexus_visible && lighting::plexus_width() > 0;` and include `px` in the `(!doc && !wt && !px)` return test.
- After `begin_render_pass` (line 1100), before the world-text loop (line 1103): `if (px) { rebuild_plexus_geom(); g_plexus_geom.Render(Rml::Vector2f(0,0), g_plexus_tex); }`.
- In `prepare()` (line 1077): the plexus texture upload (`GenerateTexture`) issues its own copy pass and must happen OUTSIDE the render pass — call `rebuild_plexus_geom()` from `prepare()` too (guarded by `px`), so the upload lands before `render_in_pass`. Keep the `.Render()` call in `render_in_pass`. Match how `build_world_text()` is split across `prepare`/`render_in_pass` (lines 1086/1103).

**Edge cases:** Before the first `plexus_step()`, `plexus_generation()==0` and the buffer is the cleared background — the quad still renders a solid dark field (correct). If `plexus_width()==0` (never resized), `px` is false and nothing draws. Texture release: `rebuild_plexus_geom` releases the previous `Rml::Texture` before generating a new one each generation (~10/sec) to avoid a GPU texture leak.

**Dependencies:** Phase 1 (needs `noise_utils.h`).

---

### Phase 3: Dark Runic Gold Theme

**Goal:** Shift the main menu from gruvbox-dark to dark runic gold palette. Isolated to the main menu — shared `theme.rcss` is untouched.

**Files:** `data/gui/mainmenu_theme.rcss` (new), `data/gui/mainmenu.rml` (modify), `data/gui/mainmenu.rcss` (modify)

**Step 3.1: Create `data/gui/mainmenu_theme.rcss`**

Main-menu-specific palette overrides. Imported by `mainmenu.rcss` after `theme.rcss`.

```css
/* Dark Runic Gold palette for the title screen.
   Deep charcoal background, warm gold accents, cyan interactive highlights.