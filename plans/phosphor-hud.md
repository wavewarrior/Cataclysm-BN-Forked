# Terminal Phosphor HUD — implementation contract

We are replacing the shipping sidebar HUD with the **Terminal Phosphor** design
(`mockups/hud/04-terminal-phosphor.html` + `.md`), modified per the user's
instruction: **the floating panels take the translucent treatment from the
Sparkline Console design (`mockups/hud/10-sparkline-console.html`), but the top
status strip and the bottom function-key strip stay opaque.**

Read both mockups and `mockups/hud/04-terminal-phosphor.md` before starting. The
mockup is the visual target; this document is the binding interface.

## Building this on Windows — four traps, all pre-existing

`out/build/win-rel-deb` could not simply be rebuilt after four new translation
units were added. None of these are caused by the HUD change, and all four cost
real time, so they are recorded here rather than in a throwaway script.

1. **The build needs a Visual Studio developer environment.** The DXC/LLVM
   subproject under `_deps` resolves the C++ standard library through the `INCLUDE`
   environment variable rather than through CMake include dirs. From a plain shell
   every `LLVMSupport` TU dies with `Cannot open include file: 'utility'` before any
   game source is reached. Run the build from a shell that has sourced
   `VC\Auxiliary\Build\vcvars64.bat`.
2. **That build dir predated the SDL3_net dependency.** Its generated ninja files
   never resolved the `SDL3_net` FetchContent block (`CMakeLists.txt:828-849`, "Co-op
   is always compiled"), so adding source files forced a reconfigure and the
   reconfigure was the first time SDL3_net was configured there. Its cache had no
   `CMAKE_TOOLCHAIN_FILE` despite a populated manifest-mode vcpkg tree inside it, so
   `CMAKE_PREFIX_PATH` pointed at `vcpkg_installed/x64-windows-static` is the lookup
   that works; the classic vcpkg toolchain does NOT, because it redirects to
   `C:/vcpkg/installed` and SDL3 then goes missing entirely.
3. **No shared SDL3 exists on this machine,** so `DYNAMIC_LINKING` must be `OFF`.
   With it on, `CMakeLists.txt:831-834` forces `BUILD_SHARED_LIBS` for SDL3_net,
   which then links `SDL3::SDL3-shared` — a target that does not exist against a
   static SDL3, and CMake additionally rejects the `INTERFACE_SDL3_SHARED`
   disagreement on `SDL3::SDL3-static`.
4. **Never pass `CMAKE_FIND_PACKAGE_PREFER_CONFIG=ON` to force CONFIG-mode lookups.**
   `CMakeLists.txt` gates its entire SDL3 / SDL3_ttf search on
   `if (NOT CMAKE_FIND_PACKAGE_PREFER_CONFIG)` (`:415`), so turning it on skips the
   search and the build dies with "This project requires SDL3 to be installed" while
   SDL3 is sitting right there.

`CMAKE_CXX_FLAGS` for this dir must include `/utf-8` and `/bigobj`. Without
`/utf-8`, `fmtlib_core.h:452` hard-fails with *"Unicode support requires compiling
with /utf-8"* — and this HUD is built entirely from U+2500 box-drawing and U+2580
block-element glyphs, so it is doubly required. Without `/bigobj`,
`catalua_bindings_map.cpp` exceeds the object section limit (C1128).

One companion fix went into the repo: `CMakeModules/Find/FindSDL3.cmake` searched
`find_library` for a library named `SDL3` only, so it could not see vcpkg's
`SDL3-static.lib` even though the module's own `SDL3::SDL3-static` branch exists for
exactly that case. The project's own lookup prefers `SDL3Config.cmake` and never
noticed; `SDL3_net` calls `find_package(SDL3)` itself and lands in the module, so it
did.

**There is no formatter on this machine.** `--target format` does not exist (CMake
reports clang-format and astyle missing at configure time, so the targets are never
created) and neither binary is on `PATH`. Match surrounding style by hand and never
claim a format pass ran.

## Known unfixed: the live_view RmlUi hover tooltip

`live_view` (the mouse-hover tile-info box, `src/live_view.cpp`) draws its
`< Mouse View >` title and its inner scroll pane's unstyled grey scrollbar
furniture at the **top-left screen corner**, over the phosphor HUD's identity row,
whenever the pointer is inside the window. Two changes were made and it is still
not fully understood:

- `lv_rml_apply_rect()` was extracted and is now called from `lv_rml_open` as well
  as `lv_rml_sync`, so the document can no longer be rendered before it is
  positioned. Suspected but unconfirmed: `lv_rml_open` runs while `TERMX == 0`, the
  helper's guard bails, and nothing positions the box until the first hover sync.
- The column width is floored (`update_offsets` zeroes the column opposite the
  configured sidebar, and the layout the other comes from can be empty), because a
  0% width is a box its own scroll furniture spills out of.

**What is still unexplained:** with `SIDEBAR_POSITION=right` the arithmetic
(`left_pct = 100 - width_pct`) cannot place the box at the left edge, yet that is
where it was measured (x 0..20 at 2560 wide, x≈0 at 1920). Either `wd` is reaching
~`TERMX`, or the option is not reading `right` in this translation unit while it
does in `panels.cpp` (the HUD's own mirroring is demonstrably correct — SOMA left,
DOCK right), or something outside these three properties places the element.
A world-specific override was the leading suspect and is RULED OUT:
`save/Eldora/worldoptions.json` carries 93 options and none of them is
`SIDEBAR_POSITION`, so the world cannot be flipping the side.

`live_view_rmlui_enabled()` returned `true` while its own comment said *"Default
OFF — opt in via the F4 panel"*. It is now `false`, matching the documented intent,
which removes the overlap for everyone and restores the curses tooltip path in
`live_view::show` (unaffected by any of this). **The placement bug therefore still
affects anyone who opts in via F4** and should be settled before that default is
flipped back.

## The design in one paragraph

A character-cell terminal. One hue (DEC P3 amber `#ffb000`) as a seven-rung
luminance ladder; hierarchy is luminance, position and reverse video, never a
second hue. Every frame stroke is a real Unicode box-drawing glyph inside a text
run — **there is not one CSS `border` in this design**. Section titles interrupt
their rule DOS-style (`──┤ SOMA ├──`) and therefore cost zero rows. Bars are
block-element glyphs. A critical body part is not given a hotter colour; its
whole row is inverted.

## Regions

Five gameplay regions plus the vehicle panel. `status` and `keys` are **opaque**
(`{{ph-0}}`); `soma`, `dock`, `log` and `vehicle` are **translucent**
(`{{ph-veil}}` + a `backdrop-filter` blur, per design 10).

| id | region | position | ground |
|---|---|---|---|
| `hud-status` | world + character state, 2 text rows + 1 rule row | full width, top edge | **opaque** |
| `hud-soma` | body parts, pools, effects | left column under status | translucent |
| `hud-dock` | overmap, target, arms | right column under status (mirrors with `SIDEBAR_POSITION`) | translucent |
| `hud-log` | message log, sized to exactly its line count | bottom-left, above `keys` | translucent |
| `hud-keys` | 1 rule row + the function-key row | full width, bottom edge | **opaque** |
| `hud-vehicle` | driving panel, `display:none` unless driving | right column under dock | translucent |
| `hud-crt-scan` | full-screen scanline glass | overlay | — |
| `hud-crt-curve` | full-screen faceplate vignette | overlay | — |
| `hud-vignette` | existing damage flash, keep as-is | overlay | — |

## The shared primitive header — already written, do not change it

`src/hud_phosphor.h` exists and is the contract. Read it. It declares:

- `enum class ink { ground, dead, rule, label, datum, peak, inverse }` — the ladder
- `hex( ink )`, resolved through the `ph-*` theme tokens
- `metrics` / `metrics_for( ctx_w_dp, ctx_h_dp )` — the cell grid
- `cell_rect`, `rect`, `layout`, `layout_options`, `layout_for`, `to_dp`
- `display_width`, `pad`, `pad_left` — cell-exact UTF-8 text fitting
- `rule_title`, `rule_options`, `rule` — rules with interrupting titles and crossings
- `bar_options`, `bar` — eighth-block-precise bars, `intact_recedes` severity rule
- `tint( ink, content )`, `invert( content )` — emphasis
- `crit_options`, `is_critical` — the fixed severity predicate

If you need a primitive it does not declare, message `Main` — do not add a
private duplicate in `panels.cpp`.

## Theme tokens — already added to `data/gui/theme.json`, do not change them

`ph-0` `#0c0800ff` ground · `ph-1` `#3a2800ff` dead · `ph-2` `#7a5400ff` rule ·
`ph-3` `#b87f00ff` label · `ph-4` `#ffb000ff` datum · `ph-5` `#ffebbfff` peak ·
`ph-k` `#120c00ff` inverted-cell ink · `ph-veil` `#0c0800a3` translucent ground ·
`ph-veil-0` `#0c080000` its zero-alpha twin for gradient stops ·
`ph-scan` `#0000004d` · `ph-scan-glass` `#00000013` · `ph-vignette` `#00000075` ·
`ph-glow` `#ffb00059` halation.

## RCSS class vocabulary — every slice must agree on exactly these

Producers emit **classes, never inline colour**, so the theme keeps ownership of
the palette and the F4 Theme tab keeps working.

| class | meaning |
|---|---|
| `.ph-row` | one text row. `white-space: pre`, height and line-height both one cell. **Every producer emits one `.ph-row` per row.** Padding inside it is U+00A0, not U+0020 |
| `.ph-i0` … `.ph-i5` | a span at ladder rung 0–5 |
| `.ph-ik` | a span at `ph-k` (only ever used inside `.ph-inv`) |
| `.ph-inv` | reverse video: `background-color: {{ph-5}}`, `color: {{ph-k}}` |
| `.ph-veil` | translucent panel ground + `backdrop-filter` |

### The whitespace trap, learned in-game

Two defects only showed up once the build was driven, and both come from the same
engine behaviour. `data-rml` sets INNER RML, and RmlUi decides whitespace handling
from the **computed style of the element it parses into, at parse time** — so a
`white-space: pre` declared on the very element being created cannot save its own
content.

1. **A newline-joined block does not break lines.** SOMA, DOCK and the vehicle
   panel each emitted one element containing rows joined by `\n`. In-game the rows
   ran together on one line and wrapped at the panel edge. Fixed by emitting one
   `.ph-row` element per row — the shape the log and keys row always used, and the
   only one that works. `.ph-block` is deleted; do not reintroduce it.
2. **Ordinary spaces are trimmed at span boundaries.** Every field becomes its own
   `<span>`, so padding at a span edge is trimmed off and a gap segment that is
   nothing but a space disappears entirely. In-game this welded every label to its
   value: `DAY16Spring12:59:46mansionWXRain11°C`, `MOVEWALKINGSPD136FOC132`,
   `STR13DEX22INT9PER20`, `MISSION MARKERNONE`. Fixed by making all alignment
   whitespace U+00A0 NO-BREAK SPACE, which is not whitespace for that purpose.
   Three single points own the conversion: `hud_phosphor::pad` / `pad_left` fill
   with it, and each producer TU converts U+0020 to U+00A0 in the one function that
   turns a field into a span (`render_runs` in `hud_phosphor_panels.cpp`,
   `row::emit` in `hud_phosphor_strips.cpp`). Already-rendered markup passes
   through untouched, because rewriting bytes inside tags would corrupt them.

In a fixed-cell grid every space is alignment, never collapsible word spacing, so
the conversion is unconditionally safe. `tests/hud_phosphor_test.cpp` pins the
U+00A0 fill so nobody "cleans it up" back to a plain space.

`hud_phosphor::tint( ink::datum, s )` must emit `<span class="ph-i4">s</span>`.
`invert( s )` must emit `<span class="ph-inv">s</span>`.

## Data model — `hud_rml_model` field names

Replacing the old field set entirely. New fields, all `Rml::String`:

`status_row1_rml`, `status_row2_rml`, `status_rule_rml`, `soma_rml`, `dock_rml`,
`log_rml`, `keys_rule_rml`, `keys_rml`, `veh_rml`.

**Deleted**: `topbar_rml`, `topbar_row2_rml`, `vitals_rml`, `minimap_rml`,
`minimap_title`, `log_rml`'s old shape, `log_title`, `botbar_rml`, `hotbar_rml`.
The two static titles are gone because titles are now baked into their rules by
the producers.

## Font, halation, and the two things NOT to do

- The document's `font-size` and `line-height` are set **from C++** by
  `sidebar_hud_apply_rect` out of `metrics`, because RCSS cannot know the
  computed cell size. Do not hardcode a font size in the stylesheet.
- Source Code Pro's advance is exactly `0.6em`, which is why `cell_w == 0.6 *
  font_size` and the grid has zero horizontal drift. Its `cmap` carries all 32 of
  U+2580–U+259F (verified), so eighth-block bars work in the shipping font.
- **Halation is `font-effect: glow`, not a duplicated blurred layer.** The mockup
  duplicates the whole glyph layer and blurs it because a browser had no better
  option; RmlUi has `font-effect` natively and it is both cheaper and honest.
- **Do not use `text-shadow`** — RmlUi 6.2 does not support it (verified against
  the fetched source). `font-effect` is the replacement.
- **Do not use CSS grid, `::before`/`::after`, `clip-path` or `mix-blend-mode`** —
  none exist in RCSS 6.2. `backdrop-filter`, `box-shadow`, `mask-image`,
  `filter`, and every gradient decorator including `conic-gradient` DO exist.

## Bugs this redesign must fix, not reproduce

All measured in the shipping HUD, with evidence in
`mockups/hud/00-current-baseline.md`:

1. `vbar_rml`'s crit gate is integer `o.cur * 100 / o.max < 25`
   (`panels.cpp:795-796`). 8/30 truncates to 26, so a limb at 26.7% that is both
   bleeding and bitten never rendered critical. Use
   `hud_phosphor::is_critical` instead.
2. `hud_vitals` computes `limb_color` into `label_hex` (`panels.cpp:872`) and
   never uses it, so bleeding/bitten never reach the body panel at all. The
   phosphor SOMA panel must show per-part effect state next to the part.
3. `.tbar-fill` is an inline `<span>` with no `display`, so the target HP bar is a
   permanently empty trough. Phosphor bars are glyphs, so this cannot recur — but
   the target bar must actually show 62%.
4. Percentage-vs-cell geometry put the hotbar 6.34 dp off the bottom of the
   screen. `layout_for` snaps to cells; use it and never write a percentage.
5. `hud_hotbar( avatar & )` ignores its argument (`panels.cpp:1282`), so no slot
   can show as unavailable, and unbound actions emit the literal
   `[Unbound globally!]`. The keys row must render an unavailable slot in
   `ink::dead` and must never emit that string.
6. The log well was 752 dp holding 267 dp of message. Size the log region to its
   line count — that is where this design's occlusion saving comes from.

## Ownership — one file per agent, no shared files

The HUD producers are being **extracted out of `src/panels.cpp` into their own
translation units**. That is partly to let six agents work concurrently without
clobbering each other, and partly because it is the right structure anyway:
`panels.cpp` is 2711 lines, and `AGENTS.md` says to prefer a new header of pure
functions over growing an existing one. After this pass `panels.cpp` keeps only
the HUD *chassis* — the data model, document lifecycle, sync and geometry — and
calls into the producer TUs.

| slice | owns, exclusively |
|---|---|
| **P1** | `src/hud_phosphor.cpp` |
| **P2** | `data/gui/sidebar_hud.rcss` |
| **P3** | `data/gui/sidebar_hud.rml` |
| **P4** | `src/hud_phosphor_panels.h` + `src/hud_phosphor_panels.cpp` — the SOMA and DOCK producers |
| **P5** | `src/hud_phosphor_strips.h` + `src/hud_phosphor_strips.cpp` — the status, log, keys and vehicle producers |
| **P6** | `src/panels.cpp` (and `src/panels.h` if a declaration must change) — chassis only |

No two slices touch the same file. Nobody but P6 edits `panels.cpp`; nobody but
P1 edits `hud_phosphor.cpp`; the frozen `src/hud_phosphor.h` is read-only to
everyone.

**The producer signatures are therefore part of this contract**, because P6 has
to call functions P4 and P5 are still writing. They are, exactly:

```cpp
// src/hud_phosphor_panels.h   (P4)
auto hud_soma( avatar &u, const hud_phosphor::layout &l ) -> std::string;
auto hud_dock( avatar &u, const hud_phosphor::layout &l ) -> std::string;

// src/hud_phosphor_strips.h   (P5)
auto hud_status_row1( avatar &u, const hud_phosphor::layout &l ) -> std::string;
auto hud_status_row2( avatar &u, const hud_phosphor::layout &l ) -> std::string;
auto hud_status_rule( const hud_phosphor::layout &l ) -> std::string;
auto hud_log_rows( const std::vector<Messages::rich_message> &msgs,
                   const hud_phosphor::layout &l ) -> std::string;
auto hud_keys_rule( const hud_phosphor::layout &l ) -> std::string;
auto hud_keys( avatar &u, const hud_phosphor::layout &l ) -> std::string;
auto hud_veh_panel( avatar &u, const hud_phosphor::layout &l ) -> std::string;
```

**Every producer receives the whole `layout`, not a width.** The layout is the
single source of truth for every column in the HUD, and each producer reads its
own region's width off it.

The first draft of this contract passed `int cols`, and P5 caught why that fails:
the mockup draws a `│` in both status text rows at the same columns where SOMA's
right border and DOCK's left border land, and `hud_status_rule` places its
crossings there. With only `cols`, the text rows would have to re-derive those
columns from the 34/192 and 35/192 ratios — and if `layout_for` rounded
differently by one cell, the verticals would sit one cell off the crossings
directly beneath them. Two expressions agreeing by convention is exactly the
half-character shift this register exists to prevent; passing the layout makes
them agree by construction.

A corollary for P6: compute the layout **once per sync**, before filling the
model, and hand the same object to every producer and to `apply_rect`.
Recomputing it twice in a frame reintroduces the disagreement by another route.

Rows must still be exactly their region's width, which is what makes
`hud_phosphor::pad` mandatory rather than advisory. Do not change a signature
unilaterally; message `Main`.

A producer that needs a threshold helper currently file-static in `panels.cpp`
(`temp_color`, the `str_string`-family stat colours) should implement its own in
its own TU. In this register you want their *bands*, not their hues — a normal
value is `ink::label`, a notable one `ink::datum`, an alarming one `ink::peak` —
so copying the thresholds and dropping the `nc_color` is the correct move, not a
duplication smell. P6 leaves the originals alone; other screens still use them.

## Do not

- Do not run builds, formatters, linters or tests. `Main` builds once at the end.
- Do not use the `eval` kernel to generate file content; its globals are shared
  across all of you and it has already silently cross-contaminated one file in
  this project. Use `write`/`edit` with literal content.
- Do not delete the vehicle panel, the damage vignette, or `hud_anim` integration.
