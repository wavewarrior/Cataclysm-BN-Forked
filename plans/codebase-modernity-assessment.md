# Codebase Modernity Assessment — Cataclysm-BN-Forked (2026)

## Context

Critical assessment of how modern/efficient this fork is for a 2026 project that must support agentic coding, grounded in the actual code plus upstream release research, followed by an approved modernization execution plan (Waves 1–3; god-file decomposition explicitly stays in its own existing plans). Repo state: fork of CataclysmBN/Cataclysm-BN at commit 05a403d, build 2026-07-16. First execution step: copy this file to `plans/codebase-modernity-assessment.md` per repo convention.

## Upstream version research (verified via web, July 2026)

| Package | In repo | Latest upstream | Gap |
|---|---|---|---|
| SDL3 | 3.4.10 (FetchContent tag, CMakeLists.txt ~line 412) | 3.4.12 (Jul 1 2026) | patch drift only |
| SDL3_ttf / image / mixer / net | 3.2.2 / 3.4.4 / 3.2.0 / 3.2.0 | current-ish | fine |
| RmlUi | 6.2 (CMakeLists.txt ~797) | 6.2 (Jan 2026) | **current** |
| Box2D | v3.0.0 (CMakeLists.txt ~825) | v3.1.1 (Jun 2025) | 1 minor: bug fixes, new sensor system, character mover |
| Lua | **5.3.6 vendored** (src/lua/lua.h:18-21, © 2020) | 5.5.0 (Dec 2025) | **2 majors**; AGENTS.md wrongly says 5.4 |
| sol2 | 3.2.3 vendored (src/sol/forward.hpp:39-42, Feb 2021) | repo stagnant since 2022; no Lua 5.5 support | stale upstream, but **has full Lua 5.4 support built in** (verified: `SOL_LUA_VERSION 504` paths in src/sol/sol.hpp:3020,4200,10961,27866,28300) |
| fmt | **7.1.3 vendored** (src/fmtlib_core.h:21, Nov 2020) | 12.2.0 (Jun 2026) | **5 majors** |
| Catch2 | v3.7.1 amalgamated (tests/catch/catch_amalgamated.hpp:7331) | v3.15.2 (Jul 2026) | ~2 years |
| CMake | min 3.24, presets schema v2 | 4.4.0; presets schema v8+ | works; schema v2 blocks includes/`$penv`/conditions |
| Compilers | CI: gcc-14, clang-20 (tidy job) | GCC 16.1, Clang 21/22 | acceptable |
| C++ std | C++23 | C++26 shipped Mar 2026, compiler support partial | staying on 23 is correct for now |
| libbacktrace / Tracy | `master` (unpinned) | — | non-reproducible |

## Findings — codebase state

### Build system (verified: CMakeLists.txt, CMakePresets.json)
- `cmake_minimum_required(VERSION 3.24)`, `CMAKE_CXX_STANDARD 23`, Extensions OFF.
- Deps via FetchContent with pinned tags (reproducible) except libbacktrace/Tracy on `master`.
- Link-perf engineering is genuinely modern: ThinLTO + on-disk LTO cache for Apple ld/mold/lld, mold on Linux presets, `-no_deduplicate` on macOS dev builds, IPO disabled for vendored subtrees, PCH (`pch/main-pch.hpp`), worktree-safe ccache (`CCACHE_BASEDIR`/`CCACHE_NOHASHDIR`).
- `CMAKE_EXPORT_COMPILE_COMMANDS: ON` in all dev presets.
- **Defects**: Windows preset hardcodes a personal sccache path (`C:/Users/NIGEL-BEAST/...`, CMakePresets.json:27-28); presets schema v2 forces copy-pasted cache vars across 10+ presets; ≥3 parallel Windows build paths beside presets (msvc-full-features/*.vcxproj+.sln, cmake-build.ps1 61KB, CMakeSettings.json).

### CI (.github/workflows, 14 workflows)
Modern shape: matrix builds w/ skip-duplicate detection + path filters, ccache-action on all OSes, gcc-14 (Linux), clang-20 clang-tidy job with custom cata-* tidy plugin, co-op tests plus dedicated TSan and ASan+UBSan jobs, emmylua type-check, autofix.ci PR formatting, PR artifact publish/stale, semantic PR titles, release changelog automation, i18n printf/extraction checks, `ubuntu-slim` for cheap jobs. Verdict: above typical 2026 OSS C++ quality; no major gaps found. [INFERENCE: wall-clock unmeasured.]

### Code health (measured over src/, 1044 files, 512.7k lines, vendored excluded)
Largest TUs: character.cpp 11,713 · map.cpp 9,817 · iuse.cpp 8,919 · vehicle.cpp 8,430 · iexamine.cpp 8,186 · iuse_actor.cpp 7,879 · activity_actor.cpp 7,426 · game.cpp 7,196 · overmap.cpp 6,700 · mapgen.cpp 6,555. Flat `src/` (~1000 files; only lighting/, lua/, sol/, physics/ subdirs).

Modern-C++ adoption vs AGENTS.md mandates (occurrence counts):
| Mandate | Actual usage | Verdict |
|---|---|---|
| `std::ranges`/`views` everywhere | 935 + 220 | real but minority (165 `++it` remain) |
| trailing return types | 1,877 | minority |
| `std::expected` for fallible fns | **3 uses** (2 files) | aspirational, not practice |
| `std::optional` | 1,036 | genuinely adopted |
| `constexpr` | 1,415 | healthy |
`std::format`: 3 uses vs 2,088 printf-style `string_format()` — printf placeholders are translator-facing in PO files, so mass migration is off the table. Legacy residue small: 680 raw `new` (factory/clone), 137 `NULL`, 23 `typedef`, 41 printf-family. No C++ coroutines — stackful fibers via vendored minicoro (deliberate). Custom `shared_ptr_fast` perf shim (69+ files). Two logging worlds: 1,657 `debugmsg` vs stream-based `DebugLog` (87+ files). Global state: `extern std::unique_ptr<game> g` + 100+ extern globals (lineage debt). ncurses paths dormant. IWYU tooling exists (tools/run_iwyu.sh).

### Agentic-coding affordances
Strong: operational AGENTS.md + CLAUDE.md + llm_review_guide; 8 project skills in `.agents/skills/`; `plans/` convention with 55 tracked plans; typed Deno task inventory; dprint/deno fmt; tagged Catch2 suite with helpers and shard scripts; compile_commands exported; `.clangd` with inlay hints.
Weak:
- **clangd disabled for the agent harness** (`.omp/lsp.json`) — agents navigate 512k lines by grep; and no root `compile_commands.json` exists for clangd to find anyway.
- **AGENTS.md diverges from code reality** (Lua "5.4" vs 5.3.6; `std::expected` "MUST" vs 3 uses; Important-Files table missing iexamine/iuse_actor/activity_actor).
- Fragmented agent-config surfaces: `.claude/`, `.opencode/`, `.hermes/`, `.omp/`, `.agents/`, `opencode.json`, `.headroom/`, CLAUDE.md, AGENTS.md.
- Root clutter: `test_shard_*` dirs not gitignored; loose experiment reports at root (BUILD_REPAIR_NOTES.md, RAIN_EFFECT_LEARNINGS.md, PROFILING_PROTOCOL.md, SPRITE_ANIMATION_TUNING.md, sound-stealth-roadmap.md).
- God files exceed agent context windows; proven fix exists (`cpp-godfile-decompose` skill, item.cpp 11,688→1,262 precedent) — **out of scope here, tracked in existing plans**.

## Assessment — verdict

**Overall: strong B+ for 2026.** The engineering shell (build, CI, link-perf, test infra, agent docs/skills/plans) is well above the 2026 OSS C++ median. Three real liabilities: (1) 2020-vintage vendored scripting/format layer (Lua 5.3.6 + sol2 3.2.3 + fmt 7.1.3) under the entire modding story; (2) docs-vs-code drift — for agents, a lying spec is worse than none; (3) god files + flat src/ (separate plans). Everything else is drift, not decay.

## Approach — execution (user-approved scope: Waves 1–3)

Steps within a wave are independent unless noted. Wave 1 step 0 first; Waves 2, 3a, 3b may then proceed in parallel. Each wave lands as its own conventional-commit series (`chore:`/`build:`/`docs:` as fitting); a wave is done only when build+tests are green.

### Wave 1 — hygiene quick wins
0. Copy this assessment to `plans/codebase-modernity-assessment.md` (repo plan convention).
1. Pin libbacktrace: CMakeLists.txt `set(LIBBACKTRACE_GIT_TAG master)` (~line 672) → the SHA returned by `git ls-remote https://github.com/ianlancetaylor/libbacktrace master` at execution time.
2. Pin Tracy: CMakePresets.json linux-full `"TRACY_VERSION": "master"` → latest release tag (resolve via `git ls-remote --tags https://github.com/wolfpld/tracy.git`, pick highest `v0.x` stable).
3. CMakePresets.json:27-28 — replace both hardcoded sccache paths with `"sccache"` (PATH lookup; machine-independent).
4. SDL3 bump: FetchContent URL `release-3.4.10` → `release-3.4.12` (~line 412).
5. Box2D bump: `GIT_TAG v3.0.0` → `v3.1.1` (~line 825). Gate: `[vehicle]` and vehicle_box2d_test pass. If the 3.1 sensor rework breaks `tests/vehicle_box2d_test.cpp`, revert the pin to v3.0.0 and file the migration as its own plan — do not block the wave.
6. Catch2 refresh: replace `tests/catch/catch_amalgamated.hpp` + `.cpp` with the v3.15.2 amalgamated pair from the upstream release (https://github.com/catchorg/Catch2, `extras/` of the v3.15.2 tag). No other files change. Gate: full default test suite passes.
7. .gitignore: add `test_shard_*/` (test_user_dir* already covered, .gitignore:19). Move loose root reports into `plans/done/`: `git mv` for tracked (BUILD_REPAIR_NOTES.md, RAIN_EFFECT_LEARNINGS.md, PROFILING_PROTOCOL.md, SPRITE_ANIMATION_TUNING.md), plain `mv` for untracked (sound-stealth-roadmap.md).

### Wave 2 — agentic-coding enablement
8. Re-enable clangd for the harness: `.omp/lsp.json` → `{ "servers": { "clangd": {} } }`. Add to `.gitignore`: `/compile_commands.json`; create a root symlink `compile_commands.json -> out/build/osx-arm-slim/compile_commands.json` on this machine (clangd discovers the DB at root; per-machine, hence gitignored). Extend committed `.clangd` with `Index: { Background: Build }` (keep existing Style/InlayHints blocks). Fallback: if first-index latency on 1044 TUs makes the harness unusable, scope with `If: { PathMatch: "src/.*" }`.
9. AGENTS.md re-baseline (docs must stop lying):
   - Reframe the HARD CONSTRAINTS + Coding Convention sections as policy for **new/modified code**, adding one adoption-reality line per mandate (e.g. "`std::expected`: 3 uses today — required for new fallible APIs, do not churn existing signatures"). Keep astyle `*NOPAD*` caveats verbatim (still true).
   - Fix Important Files table: add iexamine.cpp (8,186), iuse_actor.cpp (7,879), activity_actor.cpp (7,426); refresh other counts from `wc -l`.
   - The "Lua 5.4" claim is fixed by Wave 3a (do not edit twice; if 3a is deferred, correct it to 5.3.6 here instead).
10. Agent-config consolidation. Keep: AGENTS.md, CLAUDE.md, `.agents/`, `.omp/`, `.claude/`. For each of `.opencode/`, `opencode.json`, `.hermes/`, `.headroom/`: delete only if BOTH (a) `git log -1 --format=%cr -- <path>` shows no touch in 30 days AND (b) `grep -r <name> AGENTS.md CLAUDE.md .github deno.jsonc` finds no reference; otherwise leave and note why in the commit message. (`.headroom/` is referenced by AGENTS.md Token Optimization section — expect to keep it.)

### Wave 3a — Lua 5.3.6 → 5.4.8 (drop-in vendor swap; verified no sol2 changes needed)
11. Replace vendored sources: delete all `src/lua/*.c` + `*.h`, copy in the 5.4.8 source set (lua.org release tarball, `src/` contents minus `lua.c`/`luac.c` standalone-interpreter mains — mirror the current vendored file set, which has no interpreter mains). Note: 5.4 drops `lbitlib.c` (already absent in the vendored 5.3 set — verify, else delete). `src/lua/CMakeLists.txt` needs no change (GLOBs, compiled as C++ with `-w`).
12. Keep stock `luaconf.h` from 5.4.8. Do NOT define `LUA_COMPAT_5_3` — sol2 3.2.3 uses native 5.4 paths (`lua_resume` w/ nresults, `lua_newuserdatauv`, generational GC); verified no direct usage of changed C-API symbols in `src/catalua*.cpp`.
13. Fallout policy: compile errors in game code referencing removed 5.3 APIs get fixed forward to the 5.4 equivalent (none found by grep; contingency only). Lua-side breakage in `data/mods/` scripts (integer for-loop semantics, `math.tointeger` behavior) is caught by `--check-mods` + `[lua]` tests; fix scripts forward.
14. Update AGENTS.md/docs: "Lua 5.4" is now true; `grep -ri "5\.3" docs/en/mod/lua AGENTS.md` and fix stale version mentions; `deno task docs:gen` to regenerate `lua_annotations.lua` + reference md.

### Wave 3b — fmt 7.1.3 → 12.2.0 re-vendor (surface = 1 consumer: src/string_formatter.cpp)
15. Re-vendor with the existing flat naming (verified include graph: string_formatter.cpp → fmtlib_printf.h → fmtlib_ostream.h → fmtlib_format.h → fmtlib_core.h; fmtlib_format.cpp → fmtlib_format-inl.h):
    - fmt 12.2 `include/fmt/base.h` → `src/fmtlib_core.h` (fmt 11+ renamed core→base; keep our flat filename)
    - `format.h` → `src/fmtlib_format.h`; `format-inl.h` → `fmtlib_format-inl.h`; `printf.h` → `fmtlib_printf.h`; `src/format.cc` → `fmtlib_format.cpp`
    - Rewrite internal `#include "fmt/*.h"` directives to the flat `fmtlib_*.h` names.
    - fmt 12's printf.h no longer routes through ostream.h → delete `src/fmtlib_ostream.h` (verified sole includer is fmtlib_printf.h today).
16. `cata::string_formatter::do_formating` overloads call `fmt::sprintf(current_format, value)` (string_formatter.cpp:145-170) — API retained in fmt 12 printf.h; expect recompile-only. If