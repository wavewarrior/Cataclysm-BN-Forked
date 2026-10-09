# Cold full-build overhaul (deps in best state / better alternatives)

Destination: cold `cmake --preset osx-arm-slim && cmake --build` (game + tests) drops dramatically on macOS arm64, by putting every dependency in its best state or replacing it with a better alternative. Windows/Linux presets untouched.

## Measured baseline (out/build/osx-arm-slim/.ninja_log, last-entry-per-output)

| zone                                    | steps | CPU s | share |
| --------------------------------------- | ----- | ----- | ----- |
| src                                     | 679   | 12895 | 64.8% |
| sdl3_shadercross-build (DXC/LLVM/SPIRV) | 1654  | 4589  | 23.1% |
| tests                                   | 239   | 1498  | 7.5%  |
| rmlui                                   | 216   | 609   | 3.1%  |
| box2d+sdl3_net+rest                     | ~70   | 300   | 1.5%  |

## Findings

1. **ccache rejects every PCH TU** — `ccache -sv`: 195,287 / 200,218 uncacheable calls = "Could not use precompiled header" (97.5%). src+tests = 72% of CPU never cached. Cause: no `CCACHE_SLOPPINESS=pch_defines,time_macros`. Proven fixable: same TU, second run = direct hit with sloppiness set.
2. **Launcher hoisted**: `CATA_CCACHE_CMD` (BASEDIR/NOHASHDIR) was set at old line ~991, _after_ SDL3/shadercross/DXC FetchContent → all ~2,200 vendored compile edges got bare `ccache`, absolute `-I` paths, zero cross-worktree sharing. Moved above first FetchContent (now CMakeLists.txt:381-402); verified 2426/2426 compile edges carry launcher in fresh probe configure, incl. 966 DXC edges. Link launchers removed (uncacheable noise, 4,426 recorded calls).
3. **DXC/LLVM vendor build (23% CPU)**: root CMakeLists ~799-800 force `SDLSHADERCROSS_VENDORED/DXC ON`; game links the shadercross _library_ (runtime HLSL→SPIRV→MSL via `SDL_ShaderCross_CompileHLSLToSPIRV`, src/lighting/shader_compiler.cpp). Non-vendored path exists: `find_package(DirectXShaderCompiler)` needs `DirectXShaderCompiler_{INCLUDE_PATH,dxcompiler_LIBRARY,dxil_LIBRARY}` (unix branch of FindDirectXShaderCompiler.cmake). Prebuilt macOS arm64 candidates: MethanePowered/DirectXShaderCompilerBinary (DXC v1.9.2602 = pinned 2026-02 release, MacOS.zip + headers), LunarG Vulkan SDK libdxcompiler.dylib (older, 1.4.321). Must verify ENABLE_SPIRV_CODEGEN + reflection correctness (fork pins DXC precisely because older DXC mis-reflected StructuredBuffers).
4. **Minor deps already lean** (scout audit): SDL3/ttf/image/mixer/SQLite/zlib/FreeType all Homebrew/system, zero build steps; no duplicate src compilation (shared OBJECT lib); waste left: USE_UNITY_BUILD switch inert-ish, IPO still ON for box2d/SDL3_net/liblua, catalua bindings -O0/no-PCH, tests PCH disabled when EXPORT_COMPILE_COMMANDS=ON (preset sets it ON → 1498s tests).
5. `time_macros` sloppiness caveat: src/version.cpp:20 embeds `__DATE__`; cache hits may carry a stale compile date (cosmetic).

## Tickets / next steps

- [x] Baseline + inventory
- [x] ccache fix (implemented, in-situ hit proven; cross-root test in flight via /tmp/cbn-rootB twin root)
- [ ] Cross-root PCH hit test; if miss, evaluate `-Xclang -fno-pch-timestamp` + `include_file_mtime,include_file_ctime`
- [ ] DXC prebuilt feasibility spike (private CCACHE_DIR, CCACHE_LOGFILE; never race feat-49 build)
- [ ] Trim: IPO off for box2d/sdl3_net/liblua; tests PCH; unity-build trial for src cold path
- [ ] Full cold build proof in isolated dir; cata_test-tiles run; game launch (shaders load)

## Constraints

- Foreign feat-49 build owns machine until done (load ~95/8 cores): no competing builds/benchmarks; use private CCACHE_DIR + log files for experiments.
- Never kill ninja mid-run; builds only as long-deadline background services.
- Do not commit user's dirty GLOSSARY.md / plans/level-cache-freshness-facade.md.
