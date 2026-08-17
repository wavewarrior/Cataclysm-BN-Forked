# Merge `origin/main` into `feature/merge-dev-into-improvements`

## Context

Merge `origin/main` into the current branch `feature/merge-dev-into-improvements`, resolving every
conflict so the current branch's architectural improvements and their philosophy survive while
absorbing main's work (559 of its 579 commits are merged upstream CataclysmBN PRs). End state: one
merged tree that builds `cataclysm-bn-tiles` + `cata_test-tiles` and passes the test suite at parity
with the pre-merge baseline, with every HEAD-only subsystem still functional.

Repo: `/Users/nigel.fierens/dev-projects/Cataclysm-BN-Forked`. Working tree is clean at HEAD.

## Decisions (settled — do not revisit)

| # | Decision |
|---|---|
| **D1** | **Staged merges at landmark checkpoints.** Eleven successive `git merge <checkpoint>` stages, each buildable, `git rerere` carrying resolutions forward. Final tree equals a single `git merge origin/main`. |
| **D2** | **Adopt main's GPU compute lightmap; share one SDL_GPU device.** `cata_gpu::get_device()` returns HEAD's `lighting::gpu_device::raw()` instead of creating a second device. Default `compute_accel` stays as main ships it. |
| **D3** | **Adopt the upstream coordinate refactors, each isolated in its own stage.** "Last" is DAG-impossible (#9174 sits at ancestry depth 96 of 579), so isolation replaces ordering: stages S3, S5, S9 contain the coordinate refactors and almost nothing else. |
| **D4** | **Adopt main's strong `dimension_id` type; keep HEAD's `map`-level bounds accessors.** |
| **D5** | **Adopt main's decibel sound units, and audit every HEAD volume consumer.** |
| **D6** | **Install clang-format 22 and pre-normalize HEAD's `tests/` + `tools/` before merging.** |

## Measured ground truth

Measured this session with `git merge-tree --write-tree` (read-only in-memory merge). Nothing below
is an estimate unless labelled.

| Fact | Value |
|---|---|
| Merge base | `70e905452fcd36634b56c187bb460bf68843f9a3` (2026-06-01) |
| HEAD ahead / `origin/main` ahead | 1285 / 579 commits |
| `origin/main` composition | 559 commits tagged `(#NNNN)` (upstream PRs) + 20 fork-local |
| Files both sides changed | 462 |
| **Conflicted files / hunks** | **336 / 1324** |

By directory: `tests` 172, `src` 135, `android` 6, `tools` 5, `scripts` 4, `data` 3,
`msvc-full-features` 3, plus `.github/workflows/matrix.yml`, `AGENTS.md`, `CMakeLists.txt`,
`CMakePresets.json`, `build-scripts/`, `deno.jsonc`, `docs/`, `gfx/`.

Token-level classification (comments + whitespace stripped, each side vs base):
286 files semantic on both sides; 23 formatting-only on our side (take theirs — `docs` 1,
`scripts` 3, `src` 15, `tools` 4); 10 formatting-only on their side (take ours — all `tests`);
1 `tools` file formatting-only on both. `-X ignore-space-change` removes only 97/1324 hunks (7%).

Highest-conflict files: `src/mapgen.cpp` 115, `src/map.cpp` 83, `src/character.cpp` 46,
`src/activity_item_handling.cpp` 44, `src/cata_tiles.cpp` 37, `src/monster.cpp` 34,
`src/lightmap.cpp` 31, `src/game.cpp` 28, `src/ranged.cpp` 27, `src/npcmove.cpp` 25,
`src/vehicle_use.cpp` 21, `src/iuse.cpp` 20, `src/activity_actor.cpp` 19, `src/item.cpp` 19,
`src/game.h` 18, `src/iexamine.cpp` 16, `tests/json_test.cpp` 15, `src/editmap.cpp` 14,
`src/handle_action.cpp` 13, `src/item.h` 13, `tests/enchantment_test.cpp` 12, `CMakeLists.txt` 11.

### Subsystem asymmetry

HEAD-only: `src/physics/` (Box2D), `src/lighting/` (72 files / 17,987 lines — SDL_GPU sprite
renderer: SDF shadows, GI, bloom, volumetric fog, tonemap, RmlUI render interface),
`src/rml_screen.h`, `src/coop_net.h`. HEAD deleted `android/` and `src/pixel_minimap.*`.

main-only: `src/compute/` (`gpu_lm`, `gpu_platform`, `gpu_transparency`, `compute_backend.h`) plus
18 `src/shaders/lm_*_compute.hlsl` — a GPU compute port of the **CPU lightmap** (8,636 lines) plus
+1,690 lines into `src/lightmap.cpp`/`src/shadowcasting.*`; also `src/platform/sdl_video.*`,
`src/preload_config.*`, `src/units/sound.h`, `src/enchantments/*`, `src/magic/*`, `src/map/utils/*`,
`src/utils/algo.*`.

Both sides are SDL3 + SDL_GPU + SDL_shadercross. **Two GPU device owners:** HEAD's class
`lighting::gpu_device` (`src/lighting/gpu_device.h`) claims the window swapchain; main's
`cata_gpu::init()/get_device()` (`src/compute/gpu_platform.h`) is compute-only with no window claim.

`src/lighting/CLAUDE.md` documents that HEAD's sprite shader computes `combined = max(tint, gpu_total)`
where `tint` is the **CPU lightmap** colour lane, and `lit_level` still drives the greyscale ramp;
only the per-tile brightness read (`gpu_light_* = lm[idx].max()`) was deleted, with readiness latched
by `lightmap_ever_generated()` (`src/lightmap_ready.h`). So `src/lightmap.cpp` is still live on HEAD
and main's `gpu_lm` accelerates a grid HEAD still consumes — complementary, not competing.

### main-side renames (follow these; never re-create the old path)

| Old (exists on HEAD) | New (on main) |
|---|---|
| `src/cata_algo.h` | `src/utils/algo.h` (+ new `src/utils/algo.cpp`) |
| `src/magic.{cpp,h}` | `src/magic/magic.{cpp,h}` |
| `src/magic_spell_effect.cpp` | `src/magic/magic_spell_effect.cpp` |
| `src/magic_spell_effect_helpers.h` | `src/magic/magic_spell_effect_helpers.h` |
| `src/magic_teleporter_list.{cpp,h}` | `src/magic/magic_teleporter_list.{cpp,h}` |
| `src/magic_ter_fur_transform.cpp` | `src/magic/magic_ter_fur_transform.cpp` |
| `src/magic_ter_furn_transform.h` | `src/magic/magic_ter_furn_transform.h` |
| `src/magic_enchantment.{cpp,h}` | split → `src/enchantments/{enchanter,enchantment,enchantment_condition,enchantment_flag,enchantment_value}.{cpp,h}` |
| `src/map_functions.{cpp,h}` | `src/map/utils/map_functions.{cpp,h}` |
| `src/map_utils.{cpp,h}` | `src/map/utils/map_utils.{cpp,h}` |

### modify/delete conflicts (16)

**Keep deleted** (`git rm` the path to resolve): `android/app/build.gradle.kts`,
`android/app/deps.zip`, `android/app/jni/CMakeLists.txt`,
`android/app/src/main/AndroidManifest.xml`,
`android/app/src/main/java/org/libsdl/app/SDLActivity.java`, `android/gradle.properties`,
`src/pixel_minimap.cpp`, `src/pixel_minimap.h`, `tests/loading_ui_test.cpp`.

**Follow the rename** (port HEAD's edits to the new path, then `git rm` the old one):
`src/magic.cpp`, `src/magic.h`, `src/magic_enchantment.cpp`, `src/magic_teleporter_list.cpp`,
`src/magic_ter_fur_transform.cpp`, `src/magic_ter_furn_transform.h`, `src/map_functions.cpp`.

### Formatter state

`.astylerc` is byte-identical on both sides. Local `astyle` is 3.6.16. Both branches carry a
near-identical `.clang-format` (main adds `IndentPPDirectives: AfterHash`,
`BreakStringLiterals: false`).

| Path | HEAD (`CMakeModules/FormatSource.cmake`) | main (`build-scripts/format-cpp.sh`) |
|---|---|---|
| flat `src/*.{cpp,h}` | astyle | astyle |
| `src/*/*` (excl. `lua`, `sol`, `third-party`) | clang-format | clang-format |
| `tests/*` (excl. `tests/catch/*`) | **not formatted** | **clang-format** |
| `tools/format/*`, `tools/clang-tidy-plugin/*` (excl. its `test/*`) | **not formatted** | **clang-format** |

Scopes differ **only** in `tests/` and those two `tools/` subtrees. main's `462b918320`
("style: format tests and tools with clang-format") is 238 files, +28,835/−32,355 — purely
mechanical, and the direct cause of the 172 `tests/` conflicts. main pins **clang-format 22**
(`d3e74ba29a`, `.github/workflows/autofix.yml`, `LLVM_VERSION: "22"`, passed as
`-DCLANG_FORMAT_EXECUTABLE=`). clang-format is **not installed** on this machine.

Directional measurement (proxy: re-emit both sides one token per line, then 3-way merge): for
`tests/`, 302 hunks → 160 and **49 of 171 files auto-merge completely**. `src/` has no scope
divergence, so reformatting `src/` buys nothing and is NOT a merge tactic.

### Build-system collision: curses optionality

| | HEAD | origin/main |
|---|---|---|
| `CURSES` refs in root `CMakeLists.txt` | **0** | 20 (`option(CURSES "Build curses version." "ON")`) |
| `TILES` conditionals in root `CMakeLists.txt` | **0** (unconditional) | 13 |
| Executable targets in `src/CMakeLists.txt` | `cataclysm-bn-tiles` only | `cataclysm-bn-tiles` **and** `cataclysm-bn` |

### Dimension systems are the same design

`src/dimension_info.h` exists on **both** sides and **merges cleanly** (not in the conflict list).
`git diff HEAD origin/main -- src/dimension_info.h` is 44 lines and contains exactly three changes:
`pocket_dimension_data::return_dimension_id` `std::string`→`dimension_id`; two new additive inline
helpers `is_outside_pocket_dimension_bounds(...)`; and `dimension_info::dimension_id` (`std::string`)
renamed+retyped to `dimension_info::id` (`dimension_id`). `dimension_bounds` (with
`boundary_terrain`, `boundary_overmap_terrain`, `contains()`) is identical on both sides.

So D4 is a **mechanical type substitution**, not an architectural reconciliation.
main defines `using dimension_id = string_id<dimension>;` at **`src/type_id.h:49`** — the project's
standard type-safe identifier pattern.

`map`-level API split (counts are `src/map.h` occurrences):

| symbol | HEAD | main |
|---|---|---|
| `set_pocket_info`, `get_pocket_info`, `clear_pocket_info`, `has_dimension_bounds`, `is_out_of_bounds`, `get_boundary_terrain`, `pocket_info_` | present | **absent — HEAD-only, must survive** |
| `refresh_active_submap_view` | absent | present (2) — **take it** |
| `bind_dimension`, `get_bound_dimension`, `current_bounds_` | present | present |

HEAD dimension-API callsite volume (`git grep -c` over `src/ tests/` on HEAD), i.e. the D4 sweep
surface: `get_dimension()` 127 hits/39 files, `current_dimension` 115/25, `get_bound_dimension` 69/27,
`bound_dimension_` 35/7, `pocket_info_` 26/5, `is_out_of_bounds` 23/7, `pocket_dimension_data` 21/10,
`travel_to_dimension` 18/6, `bind_dimension` 16/8, `has_dimension_bounds` 5/3,
`get_boundary_terrain` 5/3.

### Sound units

main replaced legacy "volume = tile radius" with real acoustics in **mdB SPL**:
`src/units/sound.h` defines `using sound = quantity<int, sound_in_decibel_tag>` with `from_decibel`,
`to_decibel` and a `_dB` literal. `src/sounds.h` on main gains `short volume`,
`get_heard_volume(source, const short &origin_volume, ...)`, a distance-loss table
(`get_distance_for_volume_loss`), a 20 dB SPL propagation floor, and — critically — an explicit
legacy bridge `approximate_dB_volume_from_legacy_tile_distance_vol(const int &legacy_dist, ...)`.

**The concrete silent-rescale bug this would cause on HEAD:** `src/sounds.cpp:838` sets
`const float max_volume = 128.0f;` inside `_snapshot_sound_visualization`, used at line 857 as
`contrib = clamp(heard_vol / max_volume, 0, 1)`, feeding `data.intensity` (857–858) and weighting
`data.occlusion_db` (868). With `heard_vol` in mdB SPL (thousands), `contrib` saturates to 1.0 for
every sound and the sound-wave visualisation renders uniformly opaque. Also
`src/sounds.cpp:633-634` computes
`heard_volume = (raw_volume - weather_vol) * volume_multiplier - distance_to_sound - occlusion_db`,
mixing a legacy tile-distance volume with a dB occlusion term; under main's model the distance term
must come from `get_distance_for_volume_loss` instead.

### Upstream cross-cutting refactors on main

| Commit | Size | What |
|---|---|---|
| `462b918320` | 238 files, +28835/−32355 | clang-format `tests/`+`tools/` — mechanical |
| `33b3c8d7b8` (#9016) | 105 files, +13335/−1597 | perf: Shader Compute & Lighting Refactor (GPU lightmap) |
| `c3090ca8f0` (#9174) | 96 files, +1934/−1344 | refactor: redefine bubble space truth |
| `d0115ae247` (#9566) | 36 files, +2421/−1622 | refactor: Absolute Backing API Usage |
| `b0382913dc` (#9559) | 36 files, +495/−167 | refactor: Absolute Backing API |
| `a295b902a3` (#9761) | 28 files, +815/−519 | refactor: map-physical-path |
| `b5568240a0` | 24 files, +533/−505 | move utilities under subdirectories |

## Approach

Stages are strictly sequential — each merges into the previous stage's result. `+files`/`+hunks` are
the measured **incremental** conflict deltas (each stage's cumulative `merge-tree` against HEAD minus
the previous stage's). Later-stage actuals will be at or below these numbers because earlier stages
already absorbed shared changes.

### S0 — Prerequisites (no merge)

Independent of everything else; must complete first.

1. Install clang-format 22 and verify: `clang-format --version` reports 22.x. On this macOS/arm64
   box: `brew install llvm@22` then use `/opt/homebrew/opt/llvm@22/bin/clang-format`. If `llvm@22`
   is unavailable, fall back to `brew install llvm` and accept the newest available major, recording
   the actual version in the commit message — matching main's *scope* matters more than the exact
   patch level, and a version mismatch shows up only as extra churn, never as a semantic change.
2. Enable conflict-resolution reuse across the eleven stages: `git config rerere.enabled true` and
   `git config rerere.autoupdate true`.
3. Record the pre-merge baseline that every later verification compares against — see
   **Verification § Baseline**. Do this before touching anything.
4. Write this plan to `plans/merge-main-into-improvements.md` in the repo. `AGENTS.md` requires every
   plan to exist both as a session artifact and as a committed permanent record that survives a
   session reset; plan mode could not create the repo copy. Commit alone as
   `docs: add main-into-improvements merge plan`.
5. Replace `CMakeModules/FormatSource.cmake` with main's version and add main's
   `build-scripts/format-cpp.sh`, taking both verbatim from `origin/main`:
   `git checkout origin/main -- CMakeModules/FormatSource.cmake build-scripts/format-cpp.sh`.
   This adopts main's format scope wholesale; the two files are equivalent for `src/` and differ only
   by also covering `tests/` and the two `tools/` subtrees. Commit alone as
   `build: adopt main's format-cpp.sh scope`.
6. Run the formatter over the newly-covered paths only, and commit the result as a single
   formatting-only commit `style: format tests and tools with clang-format`:
   `cmake --preset lint -DCLANG_FORMAT_EXECUTABLE=<clang-format-22>` then
   `cmake --build build --target format`.
   Verify it is genuinely behaviour-free before committing: `git diff -w --stat` on `src/` must be
   empty (only `tests/` and `tools/` may change), and the token-level check must show no semantic
   delta — for every changed file, stripping comments and all whitespace must yield an identical
   string before and after.

Acceptance for S0: clang-format 22 on PATH, `rerere` on, baseline recorded,
`plans/merge-main-into-improvements.md` committed, three commits landed, and `src/` untouched by the
formatting commit.

### S1 — `604eeffad5` (`33b3c8d7b8^`, depth 70) — pre-GPU content tail — +49 files / +152 hunks

`git merge 604eeffad5`. 70 upstream content commits, no landmark refactor. This stage establishes the
default resolution rules that every later stage reuses:

- **R1 include blocks** — both sides reorganized `#include` lists (205 hunks repo-wide). Resolve as
  the **union** of both sides, then let the formatter's `IncludeBlocks: Regroup` + `SortIncludes`
  order them. Never drop an include from either side.
- **R2 signature modernization** — where both sides modernized the same function differently and the
  semantics match, keep **HEAD's** form, because HEAD's is the convention `AGENTS.md` mandates
  (trailing return types, `auto`, ranges, options structs). Then apply main's *behavioural* delta
  inside that signature.
- **R3 HEAD-only subsystem hooks** — when main edited a function that also contains a HEAD-only hook
  (`box2d_position_authority`, `lighting::`/`render_state` calls, RmlUI dispatch, co-op guards,
  `npc_lod_tier`, `is_coop_remote`, submap-load-manager calls), keep **every** HEAD hook and layer
  main's change around it. Losing one of these is the primary silent-breakage risk in this merge —
  see the guard list under **Verification § Invariant guards**.
- **R4 HEAD-deleted facilities** — if main's side references curses, `pixel_minimap`, android, or
  `loading_ui`, drop main's side of that hunk. HEAD has no `CURSES`/`TILES` conditionals and only the
  `cataclysm-bn-tiles` target; keep it that way.
- **R5 formatting-only files** — for the 23 files classified formatting-only on our side, take
  theirs wholesale (`git checkout --theirs`); for the 10 `tests/` files formatting-only on their
  side, take ours (`git checkout --ours`). S0 step 5 may already have collapsed some of these.

Also in this stage, resolve the build/config conflicts, since they gate every later build:

- `CMakeLists.txt` (11 hunks) — take **HEAD** for hunks 1, 3, 5, 6, 9, 10, 11 (each is main
  re-introducing an `if (CURSES)` / `if (TILES)` / `if (CATA_SDL)` guard around something HEAD made
  unconditional). Take **main** for hunk 2 (`find_package(Tracy CONFIG QUIET)` + installed-Tracy
  support), hunk 7 (use an installed libbacktrace when
  `CATA_LIBBACKTRACE_INCLUDE_DIR`/`_LIBRARY` are set), and hunk 8 (rpath assignment), simplifying
  hunk 8 to the `cataclysm-bn-tiles` branch only and dropping main's `if (TARGET cataclysm-bn)`
  branch, which is dead on HEAD.
- `src/CMakeLists.txt` — keep HEAD's single-target structure. Add main's new sources
  (`src/compute/*`, `src/platform/*`, `src/preload_config.*`, `src/enchantments/*`, `src/magic/*`,
  `src/map/utils/*`, `src/utils/algo.*`) to the existing `cataclysm-bn-tiles-common` target. Do not
  create `cataclysm-bn-common`.
- `CMakePresets.json` — keep HEAD's presets (`osx-arm-slim`, `linux-slim`, `linux-full`) and add
  main's `lint` preset if absent; drop any curses-only preset.
- `.github/workflows/matrix.yml` — drop main's curses and android matrix entries; keep HEAD's.
- `AGENTS.md` — keep HEAD's wholesale (it is this fork's authoritative workflow), then append only
  main's additions that are still true after this merge: the `format-cpp.sh` invocation and the
  clang-format-22 pin.
- `deno.jsonc`, `gfx/*`, `data/*` (3), `tools/*` (5), `scripts/*` (4), `msvc-full-features/*` (3) —
  apply R5; where both sides are semantic, prefer main for pure content/data and HEAD for anything
  referencing a HEAD-only subsystem.

Acceptance: tree builds; baseline test parity (**Verification**).

### S2 — `b8b26d6b40` (`c3090ca8f0^`, depth 95) — GPU compute lightmap (#9016, #9348) — +39 files / +183 hunks

`git merge b8b26d6b40`. Brings `src/compute/`, the 18 `src/shaders/lm_*_compute.hlsl`, and +1,690
lines into `src/lightmap.cpp`/`src/shadowcasting.*`. Implements **D2**.

1. Accept main's `src/compute/` and `src/shaders/lm_*_compute.hlsl` as new files unchanged.
2. **Single-device adapter.** Change `cata_gpu::get_device()` in `src/compute/gpu_platform.cpp` to
   return HEAD's existing device rather than creating a second one, and make `cata_gpu::init()` /
   `cata_gpu::shutdown()` no-ops with respect to device lifetime (HEAD's `lighting::gpu_device` owns
   it). Keep the exact declared signature from `src/compute/gpu_platform.h` so main's call sites
   compile untouched:
   `auto get_device() -> SDL_GPUDevice*` — returns `lighting::get_render_state()`'s
   `gpu_device::raw()`, or `nullptr` when `!ready()`.
   `nullptr` is already main's documented "device creation failed" contract
   (`src/compute/gpu_platform.h`), and `compute_backend.h::selected_backend()` already falls back to
   `backend::cpu_compute` on `nullptr` — so a not-yet-ready renderer degrades to the CPU lightmap
   instead of crashing. That fallback is the required behaviour for the window between process start
   and `gpu_device` becoming ready.
3. Keep main's `compute_shader_entrypoint(SDL_GPUShaderFormat)` as-is (it handles the Metal `main0`
   entry-point rename, which matters on this macOS box).
4. Do not route main's compute shaders through `src/lighting/shader_compiler.cpp`; main's compute
   path compiles its own kernels and the two use different entry-point conventions.
5. In `src/lightmap.cpp`, resolve toward **main** for the lightmap computation itself (that is the
   work being adopted) but keep every HEAD hook per R3 — specifically the `lightmap_ever_generated()`
   latch in `src/lightmap_ready.h` must still be set on the same code path.
6. Confirm HEAD's renderer still reads the grid: `combined = max(tint, gpu_total)` and the
   `lit_level` greyscale ramp must remain intact in `src/lighting/` (see `src/lighting/CLAUDE.md`).

Acceptance: builds; `[.]`-excluded suite at baseline parity; the GPU-lighting check in
**Verification** shows a lit scene, not black.

### S3 — `c3090ca8f0` (depth 96) — COORD #9174 "redefine bubble space truth" — +11 files / +131 hunks

`git merge c3090ca8f0`. **One commit, 131 conflict hunks — the single most expensive commit in
main's history relative to HEAD.** It is isolated precisely so a coordinate regression is
attributable to this stage alone. Implements **D3**.

Adopt upstream's coordinate semantics as base truth and port HEAD's code onto it. The three HEAD
subsystems that encode coordinate meaning and must be re-checked here:

- `src/submap_load_manager.cpp` — submap streaming and the desired-set logic.
- `src/physics/` — Box2D position authority; `vehicle::box2d_position_authority` decides whether
  Box2D or the legacy tile-stepped path owns a vehicle's position. A coordinate-space error here
  silently teleports vehicles.
- dimension binding — `map::bind_dimension` / `map::current_bounds_`.

Take main's `map::refresh_active_submap_view()` (new on main, called from `bind_dimension`) and keep
HEAD's `set_pocket_info`/`get_pocket_info`/`clear_pocket_info`/`has_dimension_bounds`/
`is_out_of_bounds`/`get_boundary_terrain`/`pocket_info_`.

Acceptance: builds; `"[map]" "[vehicle]" "[physics]"` and the coordinate-sensitive tests pass at
baseline parity. If this stage's conflicts prove intractable in `src/mapgen.cpp`, fall back per
**Assumptions & contingencies**.

### S4 — `d5855ad224` (`b0382913dc^`, depth 219) — GPU follow-ups + content — +37 files / +356 hunks

`git merge d5855ad224`. Upstream #9424 (Metal entry point), #9445 (manual CPU fallback for compute),
#9468 (block vision through floors), #9479 (visibility & position fixes), #9487 (coloured lights on
the CPU path), plus content. Largest hunk count of any stage but no new architecture — apply R1–R5.
#9487 touches the CPU-path colour lane that HEAD's `tint` reads, so re-verify the GPU-lighting check
after this stage.

### S5 — `d0115ae247` (depth 226) — COORD #9559 + #9566 Absolute Backing API — +3 files / +31 hunks

`git merge d0115ae247`. Cheap despite the upstream diff size (72 files, +2,916/−1,789) — HEAD barely
touches the same lines. Adopt main's API. Implements **D3**.

### S6 — `6774f7da2b` (`96efbbc240^`, depth 308) — content incl. #9689 — +12 files / +54 hunks

`git merge 6774f7da2b`. Apply R1–R5. #9689 ("furniture blocks line of sight") touches the visibility
path shared with `src/lighting/` occluder capture — keep HEAD's occluder hooks per R3.

### S7 — `462b918320` (depth 310) — clang-format `tests/`+`tools/` — +160 files / +288 hunks

`git merge 462b918320`. Purely mechanical upstream. S0 step 5 already put HEAD's `tests/` and
`tools/` into clang-format-22 form, so most of these 160 files should now auto-merge; the measured
directional expectation is ~49 files fully auto-resolving with the remainder roughly halved.

For every file still conflicting here: the resolution is **HEAD's semantic content in main's line
structure**. Take HEAD's content, then re-run `cmake --build build --target format` and confirm the
result is token-identical to HEAD's content (comments + whitespace stripped). Never accept main's
side of a `tests/` hunk that removes a HEAD-only test.

`tests/loading_ui_test.cpp` is a modify/delete — keep deleted.

### S8 — `b5568240a0` (depth 354) — move-utilities renames — +6 files / +19 hunks

`git merge b5568240a0`. Isolated so rename-following is unambiguous. For each entry in the rename
table above: port HEAD's edits to main's new path, then `git rm` the old path. Update every `#include`
of a moved header — `src/cata_algo.h` → `src/utils/algo.h` is the widest. Verify no file still
includes a moved path and that no old path survives on disk.

`src/magic_enchantment.{cpp,h}` is the one non-trivial case: main split it into five
`src/enchantments/*` files, so HEAD's edits must be distributed to whichever of
`enchanter`/`enchantment`/`enchantment_condition`/`enchantment_flag`/`enchantment_value` owns the
touched symbol.

### S9 — `a295b902a3` (depth 358) — COORD #9761 map-physical-path — +0 files / +1 hunk

`git merge a295b902a3`. Effectively free (28 upstream files, 1 conflict hunk). Adopt main's.
Implements **D3**.

### S10 — `db0aeeea75` (depth 369) — sound decibels — +1 file / +4 hunks

`git merge db0aeeea75`. Trivially few conflicts, but the *silent* risk is the whole point.
Implements **D5**. Adopt main's `src/units/sound.h` and `src/sounds.{h,cpp}`, including the converted
JSON content, then sweep every HEAD consumer of sound volume:

1. `src/sounds.cpp:838` — `const float max_volume = 128.0f;`. This is a legacy-volume divisor. Under
   mdB SPL it saturates `contrib` to 1.0 everywhere (line 857), flattening `data.intensity` (858) and
   corrupting the contribution-weighted `data.occlusion_db` (868). Replace the normalisation with a
   dB-domain mapping: normalise `heard_vol` across the propagation range whose floor is main's 20 dB
   SPL minimum and whose ceiling is the loudest `sounds_since_last_turn` entry for the frame, so
   `intensity` stays a true [0,1] ratio. `sound_vis_tile::occlusion_db` and
   `sound_vis_ray::occlusion_db` are already dB and need no rescale.
2. `src/sounds.cpp:633-634` — replace the `- distance_to_sound` legacy term with main's
   `get_distance_for_volume_loss`, keeping the existing `occlusion_db` subtraction (already dB).
3. Convert remaining HEAD `int volume` parameters that carry *game* volume to `units::sound`. Leave
   the `sfx::` mixer functions (`play_variant_sound`, `play_ambient_variant_sound`,
   `play_activity_sound`, `set_channel_volume`, `get_channel_volume`) on `int` — main kept those on
   `int` because they are SDL mixer channel volumes, not acoustic levels.
4. For any HEAD call site that passes a legacy tile-distance volume, convert it with main's
   `approximate_dB_volume_from_legacy_tile_distance_vol` rather than inventing a factor.

Acceptance: builds; `"[sound]"` tests pass; the sound-visualisation check in **Verification** shows
graded intensity rather than a uniform field.

### S11 — `origin/main` (depth 579) — content tail — +18 files / +105 hunks

`git merge origin/main`. 210 upstream content commits including #10059 (enchantment values with
multiple children categories, which lands in main's `src/enchantments/*` from S8). Apply R1–R5.
After this stage `git merge-tree --write-tree HEAD origin/main` must exit 0 and
`git diff HEAD origin/main --stat` must show only intended divergence.

### S12 — Adopt main's strong `dimension_id` (D4)

Not a merge. Runs after S11 because it is a mechanical sweep that would otherwise be re-conflicted by
every intervening stage. Per **D4**:

1. `src/type_id.h` — add `using dimension_id = string_id<dimension>;` (main's line 49) if S11 did not
   already bring it, and the forward declaration of `dimension` it needs.
2. `src/dimension_info.h` — the file merges cleanly, so after S11 it should already be main's form.
   Confirm all three deltas are present: `pocket_dimension_data::return_dimension_id` is
   `dimension_id`; both `is_outside_pocket_dimension_bounds` overloads exist;
   `dimension_info::id` (not `dimension_info::dimension_id`) is a `dimension_id`.
3. Convert HEAD's `std::string`-typed dimension variables to `dimension_id` across the measured
   sweep surface, largest first: `get_dimension()` (127 hits / 39 files), `current_dimension`
   (115/25), `get_bound_dimension` (69/27), `bound_dimension_` (35/7), `travel_to_dimension` (18/6),
   `bind_dimension` (16/8). At string boundaries — savegame JSON, Lua bindings, JSON loading — use
   `.str()` to serialize and `dimension_id(...)` to construct; main's `src/savegame.cpp` already
   reconstructs legacy ids from `save_prefix + pocket_id`, so keep that path rather than writing a
   new one.
4. Keep HEAD's `map` bounds accessors (`set_pocket_info`, `get_pocket_info`, `clear_pocket_info`,
   `has_dimension_bounds`, `is_out_of_bounds`, `get_boundary_terrain`, `pocket_info_`) — main has no
   equivalent. Where main uses the free `is_outside_pocket_dimension_bounds` helpers, both may
   coexist; the free helpers are additive.
5. The empty dimension id means the overworld on both sides. `string_id`'s default-constructed value
   is the empty string, so `dimension_id()` and `""` remain equivalent — do not add a sentinel.

Acceptance: builds; save round-trip check in **Verification** loads a pre-merge save.

## Critical files & anchors

| Path | Anchor | Why |
|---|---|---|
| `src/compute/gpu_platform.cpp` / `.h` | `cata_gpu::get_device`, `init`, `shutdown` | The single edit implementing D2's shared device; `nullptr` must keep meaning "fall back to CPU compute". |
| `src/lighting/gpu_device.h` | `gpu_device::raw()`, `ready()` | The device D2 adapts to; `ready()` gates the fallback window. |
| `src/sounds.cpp` | line 838 `max_volume = 128.0f`; 857–858; 868; 633–634 | The exact silent-rescale sites for D5. |
| `src/dimension_info.h` + `src/type_id.h:49` | `pocket_dimension_data`, `dimension_info::id`, `using dimension_id` | D4's whole surface; the header merges cleanly, so these three deltas are the entire type change. |
| `src/lighting/CLAUDE.md` | `combined = max(tint, gpu_total)`, `lightmap_ever_generated()` | Documents why `src/lightmap.cpp` is still live on HEAD, which is what makes D2 safe. |

## Verification

### Baseline (record during S0, before any merge)

Every later "parity" claim compares against this. From the repo root:

```sh
cmake --preset osx-arm-slim
cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles &   # background, 1200s+, never kill
./out/build/osx-arm-slim/tests/cata_test-tiles --rng-seed 1 --order decl > /tmp/baseline_tests.txt 2>&1
```

Record: the build succeeds, and the exact pass/fail/assertion counts plus the list of failing test
names. The suite is known to be order-dependent and to contain pre-existing failures, so the
**named-failure set** — not "zero failures" — is the parity criterion. A pinned `--rng-seed` and
fixed `--order` are required or successive runs compare different subsets.

**Build rule (from `AGENTS.md`, non-negotiable):** never run a build synchronously or with a short
timeout, and never bundle it into a `&&` chain with a short cap. A killed build corrupts
`.ninja_deps`/`.ninja_log` and forces a near-full rebuild every subsequent run. Start builds as
background jobs with a 1200 s+ budget and poll to completion. If the dep log is already corrupt,
recover by letting exactly one complete build run uninterrupted.

### Per-stage gate (S1–S11, and after S12)

1. `cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles` succeeds.
2. `git grep -n '<<<<<<<\|>>>>>>>\|=======' -- src/ tests/` returns nothing outside `tests/catch/`.
3. `./out/build/osx-arm-slim/tests/cata_test-tiles --rng-seed 1 --order decl` — failing-test set is a
   subset of the baseline's. Any **new** named failure blocks the stage; fix it before merging the
   next checkpoint rather than carrying it forward, because a later stage makes attribution
   impossible.
4. Stage-targeted filters: S3/S5/S9 → `"[map]" "[vehicle]" "[physics]"`; S2/S4/S6 → `"[lighting]"`
   if present plus the GPU check below; S10 → `"[sound]"`; S12 → `"[json]"` plus the save round-trip.

### New-behaviour checks (not covered by the existing suite)

These exercise the specific things this merge introduces; a green suite does not prove any of them.

- **D2 shared device — the GPU lightmap actually runs and lighting is not black.** Launch the built
  game, load a save, and confirm a lit night scene renders with graded lighting rather than a black
  or uniformly-white world. `~/Library/Application Support/Cataclysm-BN/config/debug.log` must show
  main's backend selection resolving to `sdl_gpu_hardware` (from
  `cata_compute::selected_backend_name()`) and must contain no SDL_GPU device-creation error. Then
  force `compute_accel=cpu` and confirm the scene still renders — that proves the `nullptr` fallback
  path. Two devices being created is the failure mode to rule out: exactly one SDL_GPU device
  creation may appear in the log.
- **D5 sound visualisation is graded, not saturated.** With the sound overlay active, generate a loud
  and a quiet sound and confirm `sound_vis_tile::intensity` differentiates them. Saturation to a
  uniform field is the exact bug the `max_volume = 128.0f` divisor causes, and it is invisible to the
  test suite.
- **D4 save compatibility.** Load a save created by the pre-merge HEAD build and confirm the world,
  the current dimension, and any pocket dimension load without error, then enter and exit a pocket
  dimension. This is the one check that catches a broken `dimension_id` serialization round-trip.
- **Merge completeness.** `git merge-tree --write-tree HEAD origin/main` exits 0.

## Assumptions & contingencies

- **"Coordinate refactors last" was DAG-impossible.** #9174 sits at ancestry depth 96 of 579, so no
  checkpoint reaches later work without it. The decision's intent — attributability — is honoured by
  isolating the coordinate refactors in S3, S5 and S9 instead. If you would rather they truly came
  last, the only mechanism is cherry-picking, which forfeits the real-merge history that D1 chose.
- **If S3 (#9174, 131 hunks) proves intractable**, do not abandon the staged topology. Split S3
  itself: `git merge` with `-X ours` to get a compiling tree, then port #9174's coordinate changes
  file-by-file in follow-up commits on top, starting with `src/map.cpp` and `src/mapgen.cpp`. Do not
  fall back to rejecting the refactor — D3 settled that.
- **The `tests/` normalization win is a directional measurement**, not an exact one: it came from a
  one-token-per-line proxy, because clang-format is not installed yet. Expect ~49 test files to
  auto-resolve and the rest to roughly halve. If S7 still presents close to 160 conflicted files,
  the S0 formatting commit did not match main's output — check the clang-format major version first,
  then confirm `build-scripts/format-cpp.sh` was taken verbatim from `origin/main`.
- **clang-format major version may not be exactly 22** on this box. Scope parity matters more than
  version parity; a mismatch produces extra formatting churn, never a semantic change. Record the
  version actually used in the S0 commit message.
- **`src/dimension_info.h` merges cleanly today.** If a later stage makes it conflict, the
  authoritative resolution is main's form plus HEAD's `map`-level accessors — the file's entire
  divergence is the three deltas listed in **S12 step 2**.
- **Per-stage hunk counts are `merge-tree` measurements of HEAD against each checkpoint.** S1's
  numbers are exact; later stages should come in at or below the stated deltas, since earlier stages
  absorb shared changes. A stage coming in materially *higher* means an earlier resolution went
  wrong — stop and re-check rather than pressing on.
- **`rerere` is load-bearing across eleven stages.** If resolutions stop replaying, confirm
  `rerere.enabled` survived (it is repo-local config, not carried by a worktree switch).

## Execution addenda (measured during S0, 2026-08-17)

Corrections and discoveries from actually running S0. Each supersedes the corresponding claim above.

1. **clang-format 22 was already installed** — `/opt/homebrew/opt/llvm/bin/clang-format` reports
   `Homebrew clang-format version 22.1.8` (the `llvm` formula is at 22). No `brew install` was
   needed, and the version pin matches main's `LLVM_VERSION: "22"` exactly. It is **not** on `PATH`
   under the bare name `clang-format`.
2. **`-DCLANG_FORMAT_EXECUTABLE=` does nothing under main's formatter.** main's
   `build-scripts/format-cpp.sh` resolves the binary with `command -v clang-format` and hard-fails
   otherwise; it never reads a CMake variable. S0 step 6's `-DCLANG_FORMAT_EXECUTABLE=` is inert.
   The formatter must instead be invoked with `/opt/homebrew/opt/llvm/bin` prepended to `PATH`.
3. **`.clang-format` must also be taken from `origin/main` in S0.** main adds
   `IndentPPDirectives: AfterHash` and `BreakStringLiterals: false`. HEAD never touched the file
   (`git diff base HEAD -- .clang-format` is empty), so main's version wins the merge automatically
   and is absent from the conflict set — taking it early is merge-neutral, and *required*, because
   without it S0's output cannot match main's and S7's conflict collapse silently fails.
4. **The formatter-scope table was wrong about `tests/`.** HEAD's `CMakeModules/FormatSource.cmake`
   *does* format `tests/`, `tools/format/` and `tools/clang-tidy-plugin/` — with **astyle**, not
   clang-format (excluding `tests/iteminfo_test.cpp` and `tests/json_test.cpp`, which crashed astyle
   3.6.13). The divergence is astyle-vs-clang-format, not formatted-vs-unformatted. The action is
   unchanged: adopt main's script and re-format that scope with clang-format.
5. **"Token-identical after stripping comments and whitespace" is unachievable as literally
   written**, because `SortIncludes` + `IncludeBlocks: Regroup` reorder include lines and drop exact
   duplicates, and `SortUsingDeclarations` reorders adjacent `using` declarations. The correct
   behaviour-free criterion, and the one actually applied, is: **the multiset of `#include` lines is
   preserved up to order and de-duplication, and the token stream of everything else is identical.**
   Measured over all 263 in-scope files: **263/263 behaviour-free**. The three files that first
   looked semantic were a `using`-declaration sort (`tests/vehicle_box2d_test.cpp`), duplicate
   include removal of three guarded headers (`tests/player_test.cpp`), and a tokenizer artifact.
   A naive tokenizer must handle digit separators (`90'000`), raw strings (`R"(...)"`), prefixed
   character literals (`U'x'`) and backslash line-continuations, or it reports false positives.
6. **The `tests/` normalization gain is now exact, not directional.** After formatting HEAD's
   in-scope files with clang-format 22.1.8 and main's `.clang-format`, **37 of the 220 files present
   on both sides become byte-identical to main's version** and therefore cannot conflict at S7.
7. **astyle 3.6.16 is a clean no-op on HEAD's flat `src/`** — 0 of 1098 files change. So running the
   full `format` target after a merge cannot churn `src/`, and the `cbn-astyle-indent-bug` trap is
   not currently armed. Re-check this after any stage that adds a trailing-return-type function with
   a negated first-statement `if` guard.
8. **`CATA_SDL` is the S2 landmine — a dead compile guard.** main wraps *all* of `src/compute/`
   (including `cata_gpu::get_device`, the entire subject of **D2**) in `#if defined(CATA_SDL)`, and
   defines it via `link_sdl_core()` → `target_compile_definitions(<target> PUBLIC CATA_SDL)` plus
   `add_definitions(-DCATA_SDL)`. The token **does not exist anywhere on HEAD**, and the current
   build defines it in **0 of 2368** TUs. If `CMakeLists.txt`/`src/CMakeLists.txt` are resolved by
   simply "keeping HEAD" per R4, main's GPU compute lightmap compiles to *nothing*, `get_device()`
   never exists, and D2 becomes a no-op that no test can detect.
   **Required resolution:** define `CATA_SDL` **unconditionally** on `cataclysm-bn-tiles-common`.
   This is R4 applied correctly — drop main's *conditionality* (HEAD links SDL3 unconditionally: 0
   `#ifdef TILES` in `src/`, 13 files including `SDL3/SDL_gpu.h` directly), while keeping main's
   *definition*. Verify after S2 with:
   `python3 -c "import json;cc=json.load(open('out/build/osx-arm-slim/compile_commands.json'));print(sum('CATA_SDL' in e['command'] for e in cc))"`
   — it must be non-zero, and `src/compute/gpu_lm.cpp.o` must appear in the build log.
9. **`CMakeLists.txt` has only 1 conflict hunk at S1**, not 11 — the 11 is the cumulative count
   against full `origin/main`. The S1 hunk is the SDL3_image `FATAL_ERROR` message: resolve as
   HEAD's wording (no `-DTILES=1` text, per R4) with main's corrected doc path
   `docs/en/dev/guides/building/cmake.md`.
10. **S1's measured conflict surface confirms the plan's numbers**: 49 conflicted files, 173 hunks.
    Only **9 of 173 hunks (5%) are pure include-block unions**, so R1 automation buys little and the
    bulk needs semantic judgement. **26 of the 49 files carry HEAD-only subsystem hooks** (R3) —
    highest risk: `src/handle_action.cpp` (55 `coop_` refs), `src/game.h` (23), `src/cata_tiles.cpp`
    (21 `occluder`, 16 `lighting::`, 11 `render_state`), `src/activity_actor.cpp` (17),
    `src/map.cpp` (18 `pocket_info_`), `src/sounds.cpp` (14 `sound_vis`).
11. **The verification commands pointed at a stale binary.** On the `osx-arm-slim` preset the
    freshly-linked binaries land at the **repo root** — `./cata_test-tiles` and
    `./cataclysm-bn-tiles` — while `out/build/osx-arm-slim/tests/cata_test-tiles` is a month-stale
    leftover (2026-07-11 vs. a 2026-08-17 14:30 link). Every `./out/build/osx-arm-slim/…` path in
    **Verification** must be read as `./<binary>` from the repo root, or the whole merge gets
    validated against pre-merge objects.
12. **The plan's single unfiltered baseline run would have been invalid.** Unfiltered, the suite
    aborts partway on a co-op SIGSEGV and never reaches over half its cases, so the run silently
    compares different subsets between stages. The baseline is therefore **two** deterministic runs
    that together cover all 1057 cases with no abort. `--order decl` plus a pinned `--rng-seed`
    are both required; `--order decl` alone reduced the failure set from the many families named in
    the `cbn-test-regression-attribution-by-seed` skill to just two, confirming the rest were
    ordering artifacts.
13. **The co-op SIGSEGV is fixed on this branch.** `coop_inproc_test.cpp:100` no longer crashes:
    `[coop]` runs 159/159 green. So `[coop]` **is** usable as a parity gate, and it must be used —
    co-op is a HEAD-only subsystem and R3's highest-volume hook (`src/handle_action.cpp` alone has
    55 `coop_` references).

### Recorded baseline — supersedes **Verification § Baseline**

Tree: `f20cc53c59` (HEAD after the three S0 commits). Build: `cataclysm-bn-tiles` +
`cata_test-tiles` via `cmake --build --preset osx-arm-slim`, **exit 0**, 1865 targets, 46m35s cold
(ccache had been emptied).

```sh
./cata_test-tiles "~[coop]" --order decl --rng-seed 1   # 898 cases
./cata_test-tiles "[coop]"  --order decl --rng-seed 1   # 159 cases
```

| Run | Cases | Pass | Fail | Expected-fail | Assertions pass | Assertions fail |
|---|---|---|---|---|---|---|
| `~[coop]` | 898 | 895 | **2** | 1 | 7,806,945 | 97 |
| `[coop]` | 159 | 159 | **0** | 0 | 554 | 0 |

**The parity criterion is this named failure set — not "zero failures".** Any stage that adds a
name here has regressed and blocks the next merge:

| Failing case | Assertion sites |
|---|---|
| `vehicle_efficiency` | `vehicle_efficiency_test.cpp:289` |
| `vehicle_ramp_test_60` | `vehicle_ramp_test.cpp:196, 198, 211, 214, 222, 227` |

Both are pre-existing vehicle-physics failures. Capture each stage's run with
`-r xml -o /tmp/stageN.xml` and diff the failing-case *name set* against this table; the raw
assertion counts move with content changes and are not the criterion. The suite leaves the worktree
clean — no `hit_range.json`/`*.pgm` artifacts appeared.
