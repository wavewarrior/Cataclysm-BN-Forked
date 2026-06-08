# Plan: Migrate in-game menus to Dear ImGui (pilot = `uilist`)

## Context

In-game menus render to a fixed **catacurses character grid** (SDL_GPU glyph/rect queues),
keyboard-first — no real mouse interaction, hover highlight, click-to-select, or wheel scroll.
Goal: gain those ImGui benefits while keeping the game's color palette/feel.

The fork **already has Dear ImGui on SDL_GPU** (`src/lighting/imgui_layer.{h,cpp}`) but it is
explicitly **dev-only** (the F4 lighting panel; header says "Player-facing UI stays catacurses").
Upstream **Cataclysm: DDA already migrated player menus to ImGui** via `cata_imgui` /
`cataimgui::window` — strong prior art, but coupled to DDA's SDL2/SDL_Renderer backend, their
imtui TUI path, and a DDA-only `ui_adaptor::is_imgui` flag → **reference, not drop-in.**

**Outcome:** a small `cataimgui::window` wrapper on our SDL_GPU `imgui_layer`, with `uilist`
reimplemented on top. Because `uilist` is the shared list widget, the *majority* of call sites
(addentry + query + hotkeys + desc) gain mouse + highlight for free, code untouched.

## Decisions (user-confirmed, post-grill)

- **Look:** ImGui-native layout (native widgets, proportional font OK) tinted with the game's
  `nc_color` palette + dark theme — not pixel-faithful grid.
- **Pilot scope:** the `uilist` core in `src/ui.cpp` (public API preserved for the common path)
  **plus** porting the ~7 `refresh()` callback sites (see §5) — single widget, no legacy fork.
- **Library:** add **no** new dependency. Lift only DDA's portable `draw_colored_text()`
  `<color_x>`-tag parser as reference. **Do NOT port the `cata_key_to_imgui` input bridge.**

## Research / library result

- DDA `cata_imgui` (~1000+ lines, dual-backend imtui + SDL_Renderer) is not portable as-is here.
- We already feed SDL3 events to ImGui natively (`imgui_layer::process_event` →
  `ImGui_ImplSDL3_ProcessEvent`, `sdl_input.cpp:414`), so DDA's `AddKeyEvent` bridge is
  **unnecessary and would double-feed**. Lift only the color-tag text helper.
- Conclusion: no extra lib; minimal wrapper on the existing backend.

## Architecture

### 1. Frame driving — make a blocking loop host an immediate-mode widget
`uilist::query` (ui.cpp:902) blocks in `handle_input` and redraws once per actionable input —
retained-mode. ImGui needs a frame on **mouse-move** and on a **clock tick**.
- **Register `MOUSE_MOVE`** in `create_main_input_context()` so motion wakes the loop → redraw →
  hover tracks the cursor.
- **Internal frame-tick:** while an ImGui menu is open, drive `handle_input` with `timeout≈16ms`.
  The existing `TIMEOUT` branch (ui.cpp:967) currently *returns* `UILIST_TIMEOUT`; it must
  **distinguish caller-requested timeout from internal tick** — internal tick → redraw + keep
  looping; caller timeout → return as before.
- **Force `needupdate = true`** each tick / on MOUSE_MOVE so `refresh_display` (gated on
  `needupdate`, `sdl_render_frame.cpp:569`) actually repaints ImGui animations/hover.
- Cost (named): suspends the no-input-frame queue-retain optimization while a menu is open.

### 2. Input ownership — hybrid (game keeps the keyboard)
- **ImGui owns:** mouse (hover/click/double-click/wheel) + the filter `InputText` **only while
  focused**.
- **Game `input_context` owns:** all navigation/confirm/quit, per-entry **hotkeys**
  (`keymap`, ui.cpp:931), and **custom `callback->key`** — unchanged → API parity for the
  common path.
- **Mechanism:** disable ImGui keyboard-nav on menu windows (clear `NavEnableKeyboard` while open
  / `ImGuiWindowFlags_NoNav`) so `io.WantCaptureKeyboard` is true **only** during filter typing;
  otherwise the `continue` at `sdl_input.cpp:417` won't swallow game keys. Each frame, read ImGui's
  clicked/hovered `Selectable` **back into** `uilist.selected`/`ret` — one-way ImGui→game sync.

### 3. Rendering — registry-only on `imgui_layer`
The whole ImGui frame is bracketed in one place (`sdl_render_frame.cpp:527-562`):
`new_frame()` (NewFrame + runs callbacks' Begin/End) → `prepare()` (Render) → `render_in_pass()`
into the shared swapchain Pass B. So Begin/End **must** be called by `new_frame()`.
- `imgui_layer`: replace the single dev-UI slot with a **push/pop registry** of draw callbacks;
  `new_frame()` runs all. **Widen the gate** from `g_visible` to `g_visible || registry non-empty`
  in **both** `new_frame()` and `process_event()`. Update the "dev-only" header comment (conscious
  reversal).
- `cataimgui::window` = a **registry entry** (its draw fn) + the blocking query loop. **No
  `ui_adaptor`, no `is_imgui` flag, no empty-slice trick** — a pure-ImGui menu doesn't touch the
  catacurses slice system. World/sidebar behind stay visible via their own retained queues; ImGui
  overlays on top via `render_in_pass`; menu-close just drops the registry entry.

### 4. `uilist` reimplemented on `cataimgui::window` (src/ui.h, src/ui.cpp)
- Replace `uilist::show` (catacurses draw) with `draw_controls()` using ImGui
  `BeginTable`/`Selectable`/`ImGuiListClipper`; native scrollbar; `<color_x>` entries via the
  ported `draw_colored_text`.
- **Public API identical** (`addentry*`, `query`, `ret`, `selected`, `callback`) for the common
  path → call sites compile unchanged.
- Filter: ImGui `InputText` (imgui_stdlib `std::string`); **keep filter-history** — load from
  `uistate` by `identifier`, write back on accept.
- Sizing: ImGui **auto-size** to content, honoring `uilist.w` as a **min-width hint**; ignore
  catacurses pixel-pad knobs.

### 5. Port the ~7 `refresh()` callback sites
`uilist_callback::refresh(uilist*)` (ui.h:180) draws catacurses into `menu->window`; an ImGui
`uilist` has no such window. Add a **virtual ImGui-extension hook** on `cataimgui::window` and port
each body to ImGui calls. Sites: `src/advanced_inv.cpp` (compass squares — the non-trivial one),
`src/magic.cpp`/`magic.h`, `src/magic_teleporter_list.cpp`, `src/wish.cpp` (×3), `src/wisheffect.cpp`.

## Critical files
- `src/lighting/imgui_layer.{h,cpp}` — registry + widened gate + comment
- `src/cata_imgui.{h,cpp}` — **NEW** minimal wrapper (`cataimgui::window`, `draw_colored_text`)
- `src/ui.h`, `src/ui.cpp` — `uilist` reimpl (API preserved) + frame-tick/MOUSE_MOVE in `query`
- `src/sdl_input.cpp` — `MOUSE_MOVE` registration; `needupdate` forcing; capture gate sanity
- `src/sdl_render_frame.cpp` — gate `imgui_active` on registry too (line 527)
- The ~7 `refresh()` sites in §5
- Reference only: `CleverRaven/Cataclysm-DDA` `src/cata_imgui.cpp` (`draw_colored_text`)

## Risks
- **Immediate-mode vs retained** — continuous frames while a menu is open (expected; every ImGui
  app does this).
- **Selection-sync contract** — the one-way ImGui→game mapping (which `Selectable` state →
  `selected`/`ret`, hover vs. keyboard-selection independence) is the fiddly bit; mirror DDA's
  fix (mouse-hover treated independently of keyboard selection).
- **`refresh()` ports** — `advanced_inv` compass layout is real work in ImGui.
- **D3D12** — backend confirmed via F4 on Win11; menus reuse the same path → low risk. Dev-verify
  on Metal.

## Verification
- Build Metal: `cmake --build out/build/osx-arm-slim --target cataclysm-bn-tiles`.
- Open a plain `uilist` (e.g. crafting alt-components, zone-action menu): hover highlights a row,
  click selects, double-click confirms, wheel scrolls; keyboard UP/DOWN/CONFIRM + per-entry
  hotkeys still work; **filter text input works** (focus the box, type, history recalls);
  `<color_x>` entries render colored.
- Open a ported `refresh()` menu (advanced-inventory move = compass squares; a `wish` menu):
  the side panel renders correctly in ImGui.
- Confirm catacurses sidebar/map render underneath; menu-close repaints cleanly.
- Confirm the F4 dev panel still works (registry change didn't regress it).
- Spot-check on D3D12/Win11.

## Rollout sketch (beyond the pilot)
After `uilist`: `query_popup` → `string_input_popup` → bespoke screens (`inventory_ui`,
`advanced_inv` fully, `main_menu`) one at a time, each a `cataimgui::window` subclass. Commit to
DDA's full `input_context`↔ImGui bridge **only** if a future menu needs ImGui to own the keyboard
(none in the pilot do).
