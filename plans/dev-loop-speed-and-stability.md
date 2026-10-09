# macOS dev-loop speed and stability

Wayfinder map: [Map: macOS dev-loop speed and stability](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/134). The map is the source of truth for decisions; this file keeps the measured facts the tickets cite.

## Destination (LOCKED by #135 on 2026-10-08; one recalibration allowed after the #137 baseline)

Measured on an idle machine, `osx-arm-slim` unchanged in character (-O2, ThinLTO), Windows/Linux presets and CI byte-identical:

| Loop                                                          | Target                                                          | Measured idle baseline (`osx-arm-slim`, #137)                                              |
| ------------------------------------------------------------- | --------------------------------------------------------------- | ------------------------------------------------------------------------------------------ |
| Leaf `.cpp` edit -> relinked game binary, new **fast preset** | <= 20 s                                                         | 20-24 s today on slim (TU ~7 s + ThinLTO relink 13-16 s)                                   |
| Hub-header edit -> usable binary                              | <= 10 min                                                       | `map.h` 10.3-10.8 min / 565 edges; `vehicle.h` 5.8-5.9 min / 232 edges                     |
| Fresh worktree, hot shared ccache -> first usable binary      | <= 10 min                                                       | 9.0-9.4 min first-ever (542 s build + 22 s configure); ~2 min once same-path entries exist |
| Stability                                                     | every in-scope instability guarded or fixed, shown before/after | see tickets                                                                                |

## Baseline (idle machine, 2026-10-08 20:48 - 2026-10-09 00:07, ticket #137)

Protocol: `tools/devloop-bench.ts` (content nonce + `CCACHE_RECACHE=1` on timed builds so ccache cannot serve hits; restore + untimed resync after every rep; idle gate load1 < 4, pageouts < 5/s, no foreign builders; `ccache -z` per rep; per-rep env recorded). Raw rows `/tmp/wf/baseline.jsonl`, service log `proc://devloop-baseline`. All on `osx-arm-slim` @ `53804c5b60`, ThinLTO cache warm unless stated.

| Scenario                         | Wall (reps)     | Composition                                                                          |
| -------------------------------- | --------------- | ------------------------------------------------------------------------------------ |
| Leaf `.cpp` -> game binary       | 24.1 / 19.9 s   | TU 7.0/6.4 s + relink 16.5/13.3 s                                                    |
| Leaf `.cpp` -> both binaries     | 22.2 / 22.4 s   | TU ~7 s + 2 relinks ~14.8 s each (parallel)                                          |
| Test `.cpp` -> test binary       | 21.2 / 20.8 s   | TU 5.7 s + relink 15 s                                                               |
| Relink only, ThinLTO warm        | 11.8 / 10.0 s   | both binaries                                                                        |
| Relink only, ThinLTO cold        | 309.5 / 323.1 s | cache eviction costs ~30x                                                            |
| Worktree configure               | 21.6 / 18.7 s   | with `.msl`/ignored shaders copied                                                   |
| Worktree -> first binary         | 541.7 / 129.2 s | rep1 hits rep0's cache entries: first-ever worktree ~9 min, same-path repeats ~2 min |
| `vehicle.h` hub -> both binaries | 350.2 / 352.7 s | 232 edges, max TU 22 s (`mapgen`), relinks ~178-181 s                                |
| `map.h` hub -> both binaries     | 645.9 / 618.4 s | 565 edges, max TU 94/73 s (`catalua_bindings_creature`), relinks ~217-224 s          |

Headlines:

- **Link dominates the leaf loop**: 14-16 s of the ~22 s is ThinLTO relink; the compile is ~6-7 s. The warm/cold link ratio (10 s vs 310 s) makes protecting `lto.cache` a first-class concern.
- **Hub cost is fan-out-bound**: `map.h` = 565 edges / ~10.4 min wall, exceeding the 10-min destination target; `vehicle.h` = 232 edges / ~5.9 min, inside it. Consistent with #152 (frontend-dominated TUs, fan-out dominates).
- Estimated cold core-seconds per hub touch (log-window reconstruction, approximate): `vehicle.h` ~1,700 s over 229 cold objects; `map.h` ~3,920 s over ~510. Exact per-rep `tu_count`/`tu_sum_s` recording was added to the harness after this campaign; later A/Bs carry it.
- Scout readings below are confirmed inflated for links (17-39 s claimed vs 10-12 s measured warm) and understated for hub wall (they predicted 15-20 min idle for a hub touch; measured ~10 min for `map.h`).
- Destination-target check (binds the fast preset, not slim): current slim leaf loop 20-24 s vs 20 s target; hub 10.4 min vs 10 min target; worktree 9.4 min vs 10 min target.

## Scout readings (UNVERIFIED: ccache-hit and load contaminated; superseded by the #137 Baseline)

- 630 src build edges: 598 use the shared PCH, 32 do not (`catalua_bindings*`, `main`, `messages`). Summed wall 13,017 s; median 6.3 s, p90 26.7 s, mean 20.2 s. `martialarts.cpp` 1,961 s is a contended outlier.
- Slowest src TUs (s): game.cpp 430, game_setup.cpp 417, game_misc.cpp 405, game_save.cpp 389, map_bash.cpp 259, map_cache.cpp 253, map_collapse.cpp 243, catalua_bindings_creature.cpp 212, catalua_bindings_item.cpp 203, map_items.cpp 202, map.cpp 200.
- Tests: 238 TUs, 1,228 s summed, median 6.6 s, **0 TUs with a PCH** (`tests/CMakeLists.txt:67-70` applies `tests-pch.hpp` only when `CMAKE_EXPORT_COMPILE_COMMANDS` is OFF; every macOS preset sets it ON, e.g. `CMakePresets.json:74`). Test binary shares the src OBJECT library, so src is not compiled twice.
- Links: game 254.9 s cold LTO cache (2026-10-05) then 38.6 s warm; tests 17.2 s then 36.1 s. Warm relink was 6.2 s in July (`plans/done/link-speed-macos-plan.md`). No post-link step (no dsymutil, codesign, copy).
- Per-TU command: Apple clang via ccache, `-O2 -g1 -DNDEBUG -std=c++23 -flto=thin`, PCH `pch/main-pch.hpp` (stdlib only, 65 lines, 26 MB `.pch`, 12.6 s once per configure). Unity build OFF.
- Header fan-out (TUs / summed wall s): enum_traits.h 551 / 9,040; debug.h 548; enums.h 517; translations.h 480; json.h 463; item.h 413 / 8,835; map.h 382 / 8,727; creature.h 381; character.h 338; game.h 266 / 7,913; options.h 246; monster.h 209; vehicle.h 189 / 6,966; npc.h 166. Of 1,227 src headers: 627 fan-out 1, 123 in 2-4, 138 in 5-19, 145 in 20-99, 194 >= 100, 86 >= 400.
- Stability inventory (scout `StabilityInventory`): 27 distinct issues; strict fix commits since 2026-07-01: build tooling 15, test instability 5, runtime crash 10, cross-platform 11. macOS CI builds `cata_test-tiles` (`build.yml:814`) but never runs it (`:826`); workflows trigger on main only (`matrix.yml:3-11`).

## Correction recorded at charting

The scout claimed the committed ccache PCH fix (`cab7aec3e8`) was absent from the live configure and ranked "reconfigure" as lever #1. Disproved: `ninja -C out/build/osx-arm-slim -t commands <src TU>` shows `CCACHE_SLOPPINESS=pch_defines,time_macros`, `CCACHE_BASEDIR`, `CCACHE_NOHASHDIR` and `-fno-pch-timestamp`; the build dir was regenerated on 2026-10-07. `ccache -s` uncacheable counts (23.9%) are cumulative history and do not describe the current configuration; the baseline ticket measures hit rates fresh.

## Decisions taken with the user (2026-10-08)

- Execution override: decided levers are carried out inside the map.
- macOS only; Windows/Linux presets and CI untouched or byte-identical by construction.
- Separate fast preset; `osx-arm-slim` stays perf-representative.
- Structural splits of hub headers allowed (`AGENTS.md` ">10 usages" waiver to be confirmed per header set).
- Contention policy: machine-wide build queue.
- Priority: hub-header edits first.
- Done = idle-box measurements plus stability demonstrations.
- Destination LOCKED; targets bind the fast preset, `osx-arm-slim` no-regression only; AGENTS.md waiver for split-program headers only (the edit itself is a #145 step); fence = `map.h` only until `feature/fast-track-to-co-op` lands (its own commits touch nothing else; per-branch verified); measurements run overnight/weekend through the wrapper's queue.

## Ticket graph

Frontier at charting: Confirm provisional destination and priorities; Rank hub headers by churn-weighted rebuild cost; Establish the idle-machine dev-loop baseline; Design the dev build wrapper; Test PCH gate and fast-preset constraints; Fresh-worktree options under clang's absolute-path PCH; Which formatter setup is idempotent on macOS; Root-cause the vehicle-cache SIGSEGV and shard-3 hang; Make headless GPU tests safe against the Metal swapchain assert; Fix boot-time debugmsg bursts at the source.

Blocked: Decide the hub-header split program (research + confirm); Implement the dev build wrapper (design); Restore the PCH for test TUs on macOS (test-PCH research + baseline); Measure fast-preset candidate flag sets (baseline + test-PCH research); Decide the fast preset definition (measure + confirm); Decide the fresh-worktree strategy (worktree research + baseline + confirm); Decide the formatter fix (formatter research).

Added at resume (2026-10-08): Measure frontend-vs-backend compile split with -ftime-trace (#152, frontier research; blocks the hub split program and the fast preset decision). Baseline ticket (#137) gained step-0 recovery build, `ccache -z` and idle-gate hygiene, pushed to the ticket body.

## Closed so far (details in the tickets, indexed on map 134)

- #139 test-PCH gate: restore on macOS via `APPLE AND NOT CATA_CLANG_TIDY_PLUGIN`; src lost the guard in 178178e1ab, latent. The researcher traced the upstream origin (`91175cb747e`) independently; my prepared lead body was never pushed because the ticket closed first.
- #136 hub ranking: game.h, map.h, vehicle.h are the split candidates; leaf-split percentages withdrawn pending classifier rerun.
- #141 formatter: pin astyle 3.6.14 (fixtures 9/9, token-neutral vs HEAD); depth-oracle fixed/broken counts withdrawn as unverified; 3.5.2 invalid (rc=1); pin location is #151's call.
- #153 unused-include pruning: not worth a ticket — 14 conservatively-removable edges, fan-out −4 of ~792; #136's N-class counts (61/76/49) do not survive a conservative rule (1/0/1); folds into #145 as post-split cleanup.
- #140 fresh worktree: cross-root PCH ccache sharing impossible (27.2 MB .pch embeds 2,133 absolute paths); fresh configure ~30 s once 19 gitignored `data/shaders/*.msl` are copied; levers = timestamp sloppiness, canonical symlinked build path, PCH-less fallback preset.
- #152 parse-vs-codegen (`-ftime-trace`, loads 3.5-5.5; numbers in its resolution comment, raw traces /tmp/wftt/ ephemeral): within a rebuilding TU the frontend is 69-75% of wall at current flags, 87% at the -O0/noLTO proxy; the proxy saves only ~16% per TU (game.cpp 8.76 -> 7.33 s), so hub-edit cost is dominated by rebuild fan-out, which a split attacks and a preset cannot; the levers stack. PCH saves 0.42 s/TU (leaf 2.66 vs 3.07 s); catalua TUs compile effectively -O0 anyway (last -O wins), 86.6% frontend. Ranking left to #145/#149.
- #151 formatter decision: pin astyle 3.6.14 everywhere incl. the `autofix.yml` one-liner (user lifted the CI fence for that line); `tools/` provisioning + `ASTYLE_BIN` in `format-cpp.sh`; `*NOPAD*` retired opportunistically. Graduated as execution ticket #154.
- New tickets since charting: #152 (-ftime-trace), #153 (unused-include quantification), #154 (formatter execution). #152/#153 blocked #145; #154 blocked by #151.
- Fact correction, twice over: the charting-era "30 TUs / 1,840 s / 61 s each" for `catalua_bindings*` read the wrong `.ninja_log` v7 column as duration; the correct-column recompute gave 43 TUs summing 76 s — but THAT figure is ccache-hit time. #152's real cold compiles give 18.7-45 s per TU (median ~21 s, load 3.5-5.5), so cold-summed the family is plausibly hundreds of seconds and the scout's ~61 s under load was not unreasonable. Treat all `.ninja_log`-derived per-TU numbers as hit-times; cold costs come from #137/#152-style forced compiles only.

## Constraints

- Never kill a build; background, no deadline; never pipe a build to `head`.
- Authoritative binaries: `out/build/osx-arm-slim/src/cataclysm-bn-tiles`, `out/build/osx-arm-slim/tests/cata_test-tiles`; compare mtimes.
- A Debug-typed preset redirects output to the repo root (`CMakeLists.txt:256-265`): the fast preset must not be Debug-typed unless that redirect is removed on Apple.
- PCH TUs share ccache entries per root only (clang records absolute producer paths; proven in `plans/cold-build-deps.md`).
- Execution tickets name an explicit platform/test-tag gate.
