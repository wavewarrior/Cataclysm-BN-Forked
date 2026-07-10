# Speed up the final link on macOS (osx-arm-slim)

## Context

The last build step — linking `src/cataclysm-bn-tiles` and `tests/cata_test-tiles` — takes **216 s and 220 s** respectively on the `osx-arm-slim` preset (measured from `out/build/osx-arm-slim/.ninja_log`), so every incremental change costs ~7 min of linking. Cause: `CMAKE_INTERPROCEDURAL_OPTIMIZATION: ON` makes every TU compile to ThinLTO bitcode (`-flto=thin`), and the whole optimizer backend re-runs inside each link with **no LTO cache configured**. Decision (user-confirmed): keep ThinLTO in dev builds and make relinks incremental via the linker's LTO cache, plus three supporting reductions. End state: warm relinks of both binaries drop to an expected 30–60 s each, cold links shrink too, dev builds stay perf-representative.

All linker flags below were verified accepted by the local toolchain (Apple ld-1267 on macOS 26.5): a probe link with `-Wl,-cache_path_lto`, `-Wl,-prune_interval_lto`, `-Wl,-prune_after_lto`, and `-Wl,-no_deduplicate` succeeded and populated `llvmcache-*` entries.

## Approach

All edits are in the root `CMakeLists.txt` unless noted. Steps 1–4 are independent of each other; do them in order anyway so verification failures are attributable. Nothing changes for MSVC/Windows (every edit is inside `NOT MSVC` / `APPLE` / Clang guards).

### Step 1 — ThinLTO incremental cache + skip dedup pass

Insert a new block in `CMakeLists.txt` immediately after the `CMAKE_BUILD_TYPE` default block (currently lines 86–88, ends `endif ()`), i.e. before `add_definitions(-DCMAKE)`. `LINKER` (defined line 68) and `CMAKE_CXX_COMPILER_ID` are both available at this point:

```cmake
# --- Link performance ---
# ThinLTO (CMAKE_INTERPROCEDURAL_OPTIMIZATION) runs the whole optimizer
# backend inside the link step. Point the linker at an on-disk cache so
# relinks only re-optimize modules whose inputs changed. The cache lives in
# the build dir but is not a build output, so it survives `ninja clean`.
if (CMAKE_INTERPROCEDURAL_OPTIMIZATION AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if (APPLE)
        add_link_options(
            "-Wl,-cache_path_lto,${CMAKE_BINARY_DIR}/lto.cache"
            "-Wl,-prune_interval_lto,3600"
            "-Wl,-prune_after_lto,604800"
        )
    elseif (LINKER MATCHES "mold|lld")
        add_link_options("-Wl,--thinlto-cache-dir=${CMAKE_BINARY_DIR}/lto.cache")
    endif ()
endif ()
# Apple ld's content-deduplication pass is pure link-time cost;
# skip it outside distribution builds.
if (APPLE AND NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    add_link_options("-Wl,-no_deduplicate")
endif ()
```

No existing equivalent exists in the tree (grep for `cache_path_lto|thinlto-cache` returns nothing). The `elseif` branch covers the Linux presets (`linux-slim`, `linux-full` set `LINKER: mold` + IPO ON); `ci-tiles` is excluded by the IPO gate (it doesn't set `CMAKE_INTERPROCEDURAL_OPTIMIZATION`) and by the Clang gate (gcc-14).

### Step 2 — Take vendored deps out of ThinLTO

RmlUi (155 MB bitcode `librmlui.a`) and the SDL_shadercross tree (DXC/SPIRV-Tools/SPIRV-Cross; `libdxcompiler.dylib` alone links for 268 s cold) inherit IPO from the global switch and inflate every final link for zero gameplay benefit. `src/lua` stays in LTO deliberately (in-repo, tiny, interpreter perf matters); Tracy and the BUILD_SDL3 path stay untouched (not exercised on this machine, negligible size).

Define this helper inside the new "Link performance" block from Step 1 (it mirrors the existing `_cata_strip_werror` recursion at `CMakeLists.txt:569–591` — copy that shape, not a new pattern):

```cmake
# Vendored subtrees inherit CMAKE_INTERPROCEDURAL_OPTIMIZATION and bloat the
# final ThinLTO link; cross-lib LTO with the game buys nothing there.
function(_cata_disable_ipo _dir)
    get_directory_property(_targets DIRECTORY "${_dir}" BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _targets)
        get_target_property(_type ${_t} TYPE)
        if(_type STREQUAL "INTERFACE_LIBRARY" OR _type STREQUAL "UTILITY")
            continue()
        endif()
        set_target_properties(${_t} PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION OFF
            INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF
            INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO OFF)
    endforeach()
    get_directory_property(_subs DIRECTORY "${_dir}" SUBDIRECTORIES)
    foreach(_s IN LISTS _subs)
        _cata_disable_ipo("${_s}")
    endforeach()
endfunction()
```

(The per-config properties matter: the Windows preset sets `CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE/_RELWITHDEBINFO`, which initialize the per-config target properties.)

Add two call sites:

1. Inside the existing `if(DEFINED sdl3_shadercross_SOURCE_DIR)` guard at `CMakeLists.txt:592–594`, after `_cata_strip_werror(...)`:
   ```cmake
   _cata_disable_ipo("${sdl3_shadercross_SOURCE_DIR}")
   ```
2. After `FetchContent_MakeAvailable(RmlUi)` (`CMakeLists.txt:741`, before the `BUILD_SHARED_LIBS` restore):
   ```cmake
   _cata_disable_ipo("${rmlui_SOURCE_DIR}")
   ```

Mixing the resulting native archives with the game's bitcode objects in one link is normal and supported.

### Step 3 — Honor the intended `-g1` in RelWithDebInfo

`CATA_OTHER_FLAGS` adds `-g1` (`CMakeLists.txt:278–279`, gated `BACKTRACE OR NOT RELEASE`; `BACKTRACE` defaults ON), but CMake appends `CMAKE_CXX_FLAGS_RELWITHDEBINFO` = `-O2 -g -DNDEBUG` **after** user flags, so the trailing `-g` wins — confirmed in the generated `build.ninja` FLAGS: `... -g1 ... -O2 -g -DNDEBUG -flto=thin`. Full DWARF in every bitcode module is wasted ThinLTO backend work.

Inside the `if (NOT MSVC)` block, directly after `set(CMAKE_CXX_FLAGS_DEBUG "-Og -g2")` (`CMakeLists.txt:288`), add:

```cmake
# RelWithDebInfo defaults to `-O2 -g -DNDEBUG`; per-config flags come after
# CMAKE_CXX_FLAGS, so the trailing -g silently overrides the -g1 intended
# above. Pin -g1 (line tables — enough for backtraces); use the Debug
# config for full lldb variable info.
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g1 -DNDEBUG")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g1 -DNDEBUG")
```

The `Debug` config (`-Og -g2`) and `Release`/dist presets are unaffected. This applies to Linux clang/gcc RelWithDebInfo presets too — that matches the original line-279 intent.

### Step 4 — none

Steps 1–3 are the whole change. Do not add a ninja link job pool, do not touch the presets file, and do not change the ccache `LINKER_LAUNCHER` wiring (ccache passes links through; it's inert but harmless).

## Critical files & anchors

- `CMakeLists.txt:79–88` — `if (LINKER)` block + `CMAKE_BUILD_TYPE` default; the new link-performance block goes right after line 88.
- `CMakeLists.txt:569–594` — `_cata_strip_werror` recursive helper + its shadercross call site; pattern to copy and first `_cata_disable_ipo` call site.
- `CMakeLists.txt:734–742` — RmlUi `FetchContent_MakeAvailable`; second `_cata_disable_ipo` call site.
- `CMakeLists.txt:275–289` — `CATA_OTHER_FLAGS` / `-g1` / `CMAKE_CXX_FLAGS_DEBUG`; Step 3 insertion point.

Line numbers are hints — re-read before editing.

## Verification

Baseline (already measured, pre-change): game link 216.2 s, test link 220.4 s in `out/build/osx-arm-slim/.ninja_log`.

Working directory: repo root. Expect the **first** build after this change to recompile everything (the `-g`→`-g1` flag change invalidates ninja/ccache) and to link cold — the win shows from the second link onward.

1. Reconfigure: `cmake --preset osx-arm-slim`. Must succeed with no new warnings about unknown properties.
2. Generated-flag audit (proves each step took effect) — grep `out/build/osx-arm-slim/build.ninja`:
   - the `cataclysm-bn-tiles` and `cata_test-tiles` link edges' `LINK_FLAGS` contain `-Wl,-cache_path_lto,` and `-Wl,-no_deduplicate`;
   - any `rmlui_core` or `DirectXShaderCompiler` compile edge's `FLAGS` no longer contains `-flto=thin`;
   - a `cataclysm-bn-tiles-common` compile edge's `FLAGS` contains `-O2 -g1 -DNDEBUG -flto=thin` and no bare `-g` after it.
3. Full build (populates the cache): `cmake --build out/build/osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles`. Then confirm `ls out/build/osx-arm-slim/lto.cache | head` shows `llvmcache-*` entries.
4. **The new behavior, end to end**: `touch src/character.cpp && cmake --build out/build/osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles`, then re-read the two link durations from the tail of `.ninja_log` (start/end are ms columns 1–2). Acceptance: each link **< 90 s** (expected 30–60 s) vs the 216/220 s baseline.
5. Behavioral smoke (LTO-cache + mixed native/bitcode link produced a correct binary): `./out/build/osx-arm-slim/cataclysm-bn-tiles --check-mods` exits 0, and one focused test passes, e.g. `./out/build/osx-arm-slim/tests/cata_test-tiles "[item]" | tail -5` reports success (any small existing tag is fine).

## Assumptions & contingencies

- **mold `--thinlto-cache-dir` branch is documented but not runnable on this Mac.** If a Linux configure/link errors on the flag (ancient mold), delete the `elseif (LINKER MATCHES "mold|lld")` branch — Linux merely keeps today's behavior; the macOS win is independent.
- **`-g1` fidelity**: user explicitly accepted losing local-variable inspection in RelWithDebInfo lldb sessions (file/line backtraces remain; `Debug` config keeps `-g2`). If this proves too lossy in practice, revert Step 3 alone — Steps 1–2 carry most of the win.
- **Estimate risk**: 30–60 s warm relink is an estimate; the hard acceptance gate is < 90 s. If a warm relink still exceeds 90 s, check `ls out/build/osx-arm-slim/lto.cache | wc -l` grows between builds; a non-growing cache means the flag isn't reaching the link edge — re-check step 2's grep before touching anything else.
- **Dist presets** (`osx-dist-base`, Release) also inherit the cache flag: harmless one-shot population on CI; `-no_deduplicate` is correctly excluded by the `NOT Release` gate, and Step 3 doesn't touch Release flags.

## Result

**DONE** — 2026-07-10. Warm relinks: 216 s → 6.2 s (game), 220 s → 6.9 s (tests). 35× speedup. All verification steps passed.
