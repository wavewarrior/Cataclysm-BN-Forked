# Agent Playtest Pipeline — Wayfinder Map

Mirrors GitHub issue "Agent playtest pipeline" (wayfinder:map) on wavewarrior/Cataclysm-BN-Forked.

## Destination

A decision-complete design for an **agent playtest pipeline**: a standard way for AI agents (omp sessions, CI bots) to *drive* Cataclysm-BN, *observe* resulting world state, and *verify* behavior — cheap in tokens, deterministic, parallelizable — handed off via `/to-spec`. No harness implementation happens on the map; the map ends when nothing is left to decide.

## Ground truth from the charting audit (2026-10-05, four scouts)

- **vv.py is Windows-only** (`tools/visual_verify/vv.py:20,34,42-44`: ctypes/user32 SendInput, ImageGrab, Windows install path). Its *methodology* transfers: log-based readiness, averaged captures, paired same-state nulls, 1→0→1 triplets, `--dont-debugmsg` mandatory, numeric reports (~932 tokens vs ~14,700 for frames).
- **macOS file-trigger/env hooks already exist**: `/tmp/cata_dbg_mode`, `/tmp/cata_knob`, `/tmp/cata_build_gi_scene`, F13 dump (`src/sdl_input.cpp:492-577,846`), `CATA_FRAME_DUMP`/`/tmp/cata_dump_trigger` (`src/lighting/gpu_device.cpp:164-173`), `CATA_MAP_DUMP` JSON (`src/sdl_lighting_devui.cpp:1245-1260`), `CBN_FORCE_SUN_HOUR`, `CATA_JSON_PERF` (`src/init.cpp:188`). Observation-only; no input channel.
- **Synthetic input does not reach the macOS SDL build** (plans/charcreation-visual-overhaul.md:203-206); no `SDL_PushEvent` in the input path; no macro/replay facility.
- **The real seam already exists**: `game::handle_action_from()` + `game::post_action_world_step()` are public (`src/game.h:234,262`), the coop accumulator loop (`src/main.cpp:786-890`) already drives turns from a queued action list, `player_cmd_t` is a typed command layer (`src/player_cmd.h:38-120`), `coop_fiber` resumes modal UI with injected events (`src/coop_fiber.h:13-48`), and `tests/coop_inproc_test.cpp:55-140` proves a deterministic in-process server+client rig over `coop_sim_transport` with zero GPU.
- **Windowless sim works**: `tests/test_main.cpp:196-276` boots world/avatar/map with no window; real `g->do_turn()` runs in tests (`tests/activity_fixed_window_test.cpp:52-70`). Startup ≈14.5 s (`plans/done/parallel-loading-plan.md:17`) → process-per-episode is expensive; a resident driver is preferred.
- **Lua is observe/mutate, not act**: rich map/item/creature APIs and 53 hooks, but no calendar advance, no generic action submission, no `io`/`os` in scripts (`src/catalua_bindings.cpp:411`).
- **Machine-checkable oracles exist**: `coop_world_checksum()` (`src/coop_checksum.h`), per-tick mutation-log FNV hash (`src/coop_mutation_log.h`), sync packet tiles/monsters/vehicles (`src/coop_server.cpp:1487-1670`).
- **Coop as transport blockers**: no non-interactive coop bootstrap (menu-only, `src/coop_menu.cpp:50-104`); client needs SDL video; 2-player hardcode; queue cap 2 with staleness eviction (`src/coop_proto.h:80`).
- **Determinism levers**: `--seed`/`--rng-seed`, `--userdir`, `--world`, `--dont-debugmsg` (`src/main.cpp:185-465`). `--gpu-backend software` is NOT a macOS hatch (#46). Exit 25 is NOT an instance lock: it is `exit_handler(-999) & 0xFF` (`src/runtime_handlers.cpp:8-16`), the ordinary quit path, so instances sharing a save collide silently (#47). The `/tmp/cata_*` triggers are global and consumed on read, so concurrent instances steal each other's triggers; use the `CATA_FRAME_DUMP`/`CATA_MAP_DUMP` env channels. Shard-3 test hang still open (`plans/test-hang-investigation-handoff.md`).
- **No prior art** for bot/autoplay/scenario-runner anywhere in docs, CI, or plans.

## Tickets (children of the map)

Tracker: map [#44](https://github.com/wavewarrior/Cataclysm-BN-Forked/issues/44); tickets below carry the same order.

|#|Ticket|Type|Blocked by|
|---|---|---|---|
|#45|Seam census: cost of a headless driver binary|research|—|
|#46|Rendering without a window on macOS|research|—|
|#47|Parallel episodes: isolation, ports, save hygiene|research|—|
|#48|Control transport: which channel becomes the agent's hands|grilling|#45|
|#49|Prototype: stdio action/state REPL over the coop loop|prototype|#48 (closed; graduated, see map)|
|#50|Observation contract: what the agent sees per turn|grilling|#49 (closed; resolved)|
|#51|Episode format: trial, seed, fixture|grilling|#48|
|#52|Oracle and report schema: machine-readable pass/fail|task|#50 (closed); resolved|
|#53|Agent-facing surface: CLI + skill packaging|grilling|resolved (#50, #51, #52, #79 closed)|
|#54|Retire or absorb the existing harnesses|task|resolved (#48, #53 closed)|
|#79|Typed commands: the agent driver's command set and deny list|grilling|resolved (added after #50 closed)|
|#80|Windowed driver mode: booting the driver with a real window for renderer trials|grilling|resolved|
|#81|RNG divergence: why same-seed Episodes diverge at the first pause|task|resolved|

## Blocking edges

- Seam census → Control transport → {Prototype, Episode format}
- Prototype → Observation contract → {Oracle schema, Agent surface ← also Episode format}
- Oracle schema → Agent surface
- Transport + Agent surface → Harness retirement
- Rendering-macOS and Parallel-episodes research: frontier, inform Prototype/Agent surface from the fog.

## Not yet specified (fog)

- Whether the driver exposes coop TCP (multi-machine agents) or stays local stdio; graduates from the transport decision.
- Vision-language fallback: when pixel capture beats structured state; after observation contract.
- Long-horizon playtests (survive-N-days benchmarks, scoring rubric); after episode format.
- Fuzz/smoke extension of the same pipeline (packet fuzzer was planned, never built); after oracle schema.
- Windows/Linux parity of the pipeline; after macOS design locks.

## Out of scope

- Multiplayer gameplay changes themselves; the pipeline consumes coop plumbing, does not alter replication semantics.
- Retiring vv.py's Windows scenarios wholesale — they keep serving the Windows contributor; only the agent-facing surface is unified.
