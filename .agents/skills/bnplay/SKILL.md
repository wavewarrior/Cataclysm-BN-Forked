---
name: bnplay
description: Use when asked to playtest, verify gameplay, drive or run the game, reproduce an in-game bug, or check that a code change behaves in a running Cataclysm-BN. Teaches the bnplay observe, act, verify loop: start an Episode, send typed commands, read the report and its exit code.
---

# bnplay: observe, act, verify

`bnplay` (`tools/bnplay/`, Deno) boots the game windowless from a save fixture, serves it over a one-line-in, one-line-out JSON driver, and judges the run with oracles.

- The game stays resident between your calls; you pay the 7 to 10 s boot once per Episode.
- **Trial** (TOML): defines the test. **Episode**: one run of it. **Scene**: a Lua setup script (see GLOSSARY.md).

**Windows** (verified 2026-10-07): windowless Episodes work. The game serves the protocol on its stdin/stdout (`--driver-fd 0`; Windows cannot hand a child fd 3) and the daemon listens on loopback TCP, its port in `<BNPLAY_HOME>/daemon.port`. The default binary is `out/msvc/src/RelWithDebInfo/cataclysm-bn-tiles.exe` (see `cbn-windows-build-test-plumbing`). Clones are plain copies (no copy-on-write); `doctor` reads processes and memory through CIM, so "swap" is free commit; there is no load average. A hung, idle or stopped-by-force game is killed with its whole process tree (`taskkill /T`); with no process groups, a game that exits on its own leaves any process it started running. Make a fixture with Play Now in an EMPTY private user dir (`--userdir out/genfix/`), so the world gets only the in-repo default mods: Play Now otherwise reuses any existing empty world with its third-party mods, and a mod that fails to load drops you silently back on the main menu. Save with `S`, then a capital `Y` (`FORCE_CAPITAL_YN` is on by default), and judge success by a fresh `#*.sav` and `master.gsav` in the new world, not by the exit code: one session exited `0xC0000005` after a clean `Log shutdown.` with the save intact (not reproduced on a menu quit, a save-and-quit, or the driver's `quit`). `{"cmd":"action","name":"map"}` crashes the windowless game (`0xC0000005`, no crash log; `inventory` and `look` are fine), so the Episode ends as `driver_exit`. Untested on Windows with the real game: windowed mode and `capture`.

**Tests on Windows** (`deno task test:bnplay`, verified 2026-10-07): `python` must be on PATH; the mock driver (`mock_driver.py`) runs under it, as does any `.py` `BNPLAY_BINARY`. The real-binary tests need `BNPLAY_SAVE=<save dir>` (there is no default save location). Their contracts assume Bairdford (avatar walled in by a vehicle, carrying a smartphone, room in its inventory, no monster in view): another save, such as the `out/genfix` Barnum, fails those steps. A full run takes about 11 minutes.

The same operations exist as a CLI and as MCP tools:

- **CLI**: below, `bnplay <op>` means `deno task bnplay <op>` from the repo root. The result is JSON on stdout; a failure is one `bnplay: <reason>` line on stderr with exit 2 (deno adds its own banner on stderr).
- **MCP**: `deno task bnplay mcp`. Tools: `start`, `step`, `stop`, `report`, `fixture_add`, `fixture_baseline`, `fixture_list`, `doctor`, `shutdown`. Arguments are named as below; `step` takes `session` and `command` (an object).
- **MCP results**: the same JSON; a refusal is a tool error; a verdict or `healthy:false` is a result.

## Run an Episode

Each step is done when its stated output appears.

1. **Preflight.** `deno task bnplay doctor [--fixture <name>]` prints `"healthy":true` and exits 0. It checks the `--driver-fd` flag in the binary, binary newer than `src/`, fixture and baseline, stray driver processes, free memory (default 1024 MB) and swap (default 1024 MB). Unhealthy: read each failing check's `message` and fix it; a stale binary means rebuild (AGENTS.md), not retry. The game binary defaults to `out/build/osx-arm-slim/src/cataclysm-bn-tiles`; point at another with `BNPLAY_BINARY=<path>` and the checkout whose `data/` and `src/` it was built from with `BNPLAY_BASEPATH=<dir>` (default: this repo).
2. **Fixture** (once per save). `bnplay fixture add "<save-dir>" [name]` copy-on-write clones a world save (the folder holding `map.sqlite3`, e.g. `~/Library/Application Support/Cataclysm-BN/save/<World>`) into the gitignored `tools/bnplay/fixtures/`. Then `bnplay fixture baseline <name>` boots it, idles, and records the post-ready game log (about 15 s). Output `"baseline":"fresh"`. `bnplay fixture list` shows freshness. Refresh the baseline whenever the fixture or the mod set changes.
3. **Trial.** Write a TOML file:
   ```toml
   fixture = "Bairdford"      # required
   seed = 7                   # reseeds the engine just before turn 0
   start_date = "0001-03-10"  # optional: pins the game date, YYYY-SS-DD (see below)
   time_of_day = "08:30"      # optional: pins the game time of day, HH:MM
   turn_limit = 20            # Episode ends after this many game turns; checked after each request completes
   wall_clock_limit_s = 300   # hard cap enforced outside the game; default 300 counts from boot
   expected_commands = ["wait", "action:pause"]   # commands that must not be unsupported/no_effect
   attach_view = 3            # optional: a radius-3 view on every response
   scene = "lightone"         # optional: Lua Scene run before the first state

   [[oracle]]                 # predicate over every observation; flat observation keys
   name = "stays alive"
   field = "hp"
   operator = "gt"            # eq ne lt le gt ge contains
   value = 0
   mode = "always"            # always (default) | never | by-turn-N (N game turns after the first state)
   severity = "fail"          # fail (default) | warn: warn never changes the exit code
   ```
   - Unknown fields are errors. Choose `wall_clock_limit_s` for the whole session including your thinking time.
   - `start_date` is `YYYY-SS-DD`: year from 0001, season 01 (spring) to 04 (winter), day of the season from 01 to the world's season length (91 by default). The game has no months.
   - `start_date` and `time_of_day` set the game clock after the seed and before the first state (the driver's `set_time`). The turn counter jumps; nothing is simulated across the gap. One alone keeps the other (the day, or the time of day). A day past the season length fails the boot.
   - `turn_limit` counts from the first state and is checked only after a request completes: a Trial that sends no request never reaches it (the wall clock still ends it). The request that crosses it is answered in full with `episode_ended: turn_limit`.
4. **Start.** `bnplay start trial.toml` prints `{"session":"814f5a63","transcript":"...","boot_ms":7110}`. The session id is the handle for every later call. A start beyond the cap (2 sessions, `BNPLAY_MAX_SESSIONS`) is refused.
5. **Step** until your question is answered: `bnplay step 814f5a63 '{"cmd":"wait","turns":3}'` prints one lean observation. Observe, act, check the `outcome`, repeat. Never send the next command before reading the previous outcome.
6. **Stop.** `bnplay stop 814f5a63` ends the Episode (also reaps the game) and prints the report; its exit code is the verdict. `bnplay report <session>` re-reads it (also while running). The Episode may have ended on its own (`"episode_ended":"turn_limit"` or `"died"` in a response); `stop` still returns the report. The daemon remembers the last 20 ended sessions (`BNPLAY_ENDED_SESSIONS_KEPT`), so `report` fails with `no session` on an older one; its transcript stays on disk.
7. **Clean up.** `bnplay shutdown` ends every Episode and the daemon when you are finished.

Passing run (real output, trimmed): `{"session":"814f5a63","verdict":"pass","exit_code":0,"ended":"turn_limit","oracles":[{"name":"alive","result":"pass"},{"name":"game_log","result":"pass"},{"name":"turn_counter","result":"pass"},{"name":"commands","result":"pass"},{"name":"stays alive","result":"pass"}],"boot_ms":7110,"requests":20,"latency_ms":{"median":0.4,"max":98.6},"turns":{"first":1344365,"last":1344393},"transcript":"<dir>/transcript.jsonl","log":"<dir>/userdir/config/debug.log"}`

## Read the report

- **Exit code** (of `stop` and `report`):
  - `0` pass.
  - `1` an oracle failed.
  - `2` harness error: boot failure, game hung or died, idle reaper, daemon shutdown. Any bnplay refusal prints `bnplay: <reason>` and exits 2.
  - `3` inconclusive: no oracle has decided yet, or the wall clock ended the Episode with no oracle failed.
  - A wall-clock ending is never `0`: `1` if any oracle failed, else `3`, however many oracles were already satisfied.
- `oracles[]`: built-ins `alive` (answers in time), `game_log` (no new ERROR lines after ready versus the fixture baseline), `turn_counter` (never backwards, agrees with `time_passed`), `commands` (no `unsupported`/`no_effect` on `expected_commands`), plus yours. `result` is `pass`, `fail`, `warn`, `inconclusive` or `skipped`.
- A failure carries `first_fail`: `{"index":3,"turn":1344365,"elapsed":0,"why":"stamina=10000, not stamina ge 10001"}`. `index` is the `id` of the request in the transcript: open `transcript` (JSONL, one `request` or `response` per line) and search `"id":3`. The transcript is the repro. The debug log at `log` holds the game's own errors.
- Seed determinism is not guaranteed: same-seed Episodes sometimes diverge at the first world step. Assert invariants (outcome, `time_passed`, monotonic turn), never exact world state or RNG values. `bnplay doctor --self-check` runs an A/A pair and reports divergence next to load and swap.

## Commands (`step`)

Every request is `{"cmd":"<name>", ...}`; a malformed or unknown one answers `{"status":"error","error":"..."}` and costs nothing (the game survives).

| Command | Arguments | Notes |
|---|---|---|
| `state` | | observation, no game time |
| `move` | `dir`: `n ne e se s sw w nw up down` | |
| `wait` | `turns` >= 1 | |
| `sleep` | `max_turns` | |
| `pickup drop wield wear take_off eat drink use read reload` | `item` (id from `query inventory`); `eat`/`drink`: `anyway`; `use`: `method` when the item has several | an invented or stale id is a protocol error; a valid id the game rejects is `refused` with its message |
| `craft` | `recipe` id (e.g. `pointy_stick`), `max_turns` | |
| `melee fire smash` | `dir` or `pos`: `[dx,dy]` offset from the avatar | |
| `action` | `name`: any game action (`pause`, `inventory`, `look`, `map`, `messages`) | raw passthrough; see deny list |
| `key` | `key` (e.g. `ESC`) | answers an open menu only |
| `view` | `radius` 1 to 10 | ASCII `grid`, `legend`, `creatures` and `items` with `dx`,`dy`,`id` |
| `query` | `topic`: `inventory` or `effects` | item ids are stable only within the Episode; names carry colour markup and are truncated |
| `run_scene` | `name` (a `.lua` in `tools/visual_verify/scenes`, or `BNPLAY_SCENES`) | response has `scene: {status, lines}` |
| `attach_view` | `radius` 0 to 10 (0 detaches) | |
| `seed` | `seed` | |
| `set_time` | `date` `YYYY-SS-DD` and/or `time` `HH:MM` | pins the game clock (a Trial's `start_date`, `time_of_day`); answers `turn`, `date`, `time`; refuses an impossible value |
| `capture` | `tag`, `mode` `final` or `state` | windowed Episodes only |
| `quit` | | prefer `bnplay stop` |

Multi-turn commands (`craft`, `sleep`, `read`, `reload`, some `use`) take `max_turns`; the default runs until the activity ends or is interrupted, and one request is capped at 1000 turns (`interrupted`, reason `turn_cap`).

**Raw `action` deny list** (`data/driver_deny_list.json`): `craft drop eat apply wear read` answer `unsupported`, reason `deny_list`, naming the typed command to use. Any other blocking input read is caught by the no-fiber guard and answers `unsupported`, reason `blocking_read`. Neither hangs the game. An opened menu (`action inventory`) answers `awaiting_input` with `prompt`: only `key`, `view`, `state` and `query` work until you send `{"cmd":"key","key":"ESC"}` (no game time passes).

## The observation

Every response: `id`, `status` (`ok` or `error`), `boundary` (`turn_complete` or `needs_input`), `turn`, `time_passed`, `moved`, `new_messages` (only what this action logged; content-based, includes repeats), `prompt` (open menu name or null), vitals `hp pain stamina hunger thirst` (flat keys), `outcome`. Optional: `reason`, `detail`, `turns`, `progress`, `truncated`, `view`, `episode_ended`. Ceiling about 1.5K tokens.

| `outcome` | Meaning |
|---|---|
| `completed` | it happened (check `time_passed`) |
| `blocked` | the move spent no time and did not change position: a wall, or a game message refusal such as "You can't walk through that" (`detail` or `new_messages` holds it) |
| `refused` | the game rejected it; `detail` holds the game's message |
| `no_effect` | accepted, nothing observable changed |
| `awaiting_input` | a menu is open; see `prompt` |
| `unsupported` | deny list or no-fiber guard; `reason` says which |
| `interrupted` | `reason`: `turn_cap`, `monster_in_view`, `pain`, `noise`, `other` |
| `died` | terminal; the Episode ends |

Time passes only when moves were spent: a cancelled menu or blocked move changes nothing. The first action after load may complete a partial turn (a `wait` of 2 can advance 1). The Bairdford avatar starts enclosed by vehicle walls, so every compass `move` is `blocked`; use `wait` for time-based checks or another fixture.

## Escalation ladder

Spend tokens only when the cheaper rung cannot answer: the lean `step` response; then `view` and `query`; then the debug log and transcript; then, in a windowed Episode only, `capture`. `view` is read-only and free of game time. `fingerprint` (an integrity hash) is specified but the driver has no such command: it answers `unknown cmd`.

## Traps

- **The game has no instance lock.** Exit code 25 is the ordinary quit path, not a conflict signal, and two instances sharing a save corrupt it silently. Isolation is enforced by the **supervisor**, never by the game: every Episode gets its own `--userdir`, a copy-on-write clone of the fixture and a unique world name. Never launch the game by hand against a user directory or world that an Episode or an interactive game uses, and never point two sessions at the same userdir or world.
- **External watchdog.**
  - A request unanswered in time means a hung game: the supervisor kills it by process group and ends the Episode as a harness error (exit 2).
  - The time allowed is `BNPLAY_STEP_TIMEOUT_MS` (30 s) plus `BNPLAY_TURN_TIMEOUT_MS` (100 ms) for every turn the request may spend, at most the 1000-turn cap: `state`, `seed`, `view`, `move` get 30 s; `wait` with `turns: 1000` gets 130 s. The turns it may spend are its `turns` or `max_turns`, else the whole cap for a command that can run the world (`action`, `sleep`, `craft`, `key`, item and combat commands). A `wait` of 1001 turns therefore returns `interrupted`, `turn_cap`; it is not a hang.
  - The Trial's `wall_clock_limit_s` ends the Episode the same way from outside the game: exit 1 if an oracle had failed, else 3 (inconclusive), never 0.
  - A session with no request for 10 minutes (`BNPLAY_IDLE_TIMEOUT_MS`) is reaped by process group. Its transcript is kept, later `step` calls fail with `ended: idle_timeout`, and `stop` still prints the report (exit 2).
  - Always `stop` when done: a game holds about 1 GB and swap has run out on this machine before.
- **Modal debug prompts block forever.** A mod-heavy world raises thousands of JSON debug prompts; the driver is always started with `--dont-debugmsg`. Never start the game any other way unattended.
- **ESC on the main menu quits.** In an interactive launch, `ESC` opens "Really quit?" and a later Enter accepts: the game exits cleanly and looks exactly like a crash. The driver has no main menu (`--world` loads straight in; `key` with no menu open is an error), so this bites only the fallback workflows below.
- **A stale binary produces phantom diagnoses.** Run `doctor` before trusting any Episode; rebuild when it says the binary predates `src/`. A copied binary gets a new mtime and passes the check: copy only fresh builds.
- **Memory.** Default cap is 2 concurrent sessions; `doctor` fails below 1 GB free swap. Do not raise `BNPLAY_MAX_SESSIONS` on a machine that is swapping.
- **Daemon socket.** `BNPLAY_HOME` (default `out/bnplay`, one directory per Episode holding `transcript.jsonl` and `userdir/config/debug.log`) must keep the socket path under 100 bytes.

## Windowed mode (renderer changes only)

Add `mode = "windowed"` (and optionally `window_size = [1280, 720]`) to the Trial. A real, visible window opens in a screen corner on the user's desktop without taking focus: leave it alone, and run one windowed session at a time (a second is refused). It needs a graphical login session, so not over ssh.

Windowed init fails without two shader sources, `data/shaders/lighting/src/emitter_glow.vert.hlsl` and `emitter_glow.frag.hlsl`. They were never committed (`/data/shaders/` is gitignored, `.gitignore` line 29; the 45 sibling shader files were force-added; no build step generates them). A fresh clone or worktree must copy them from the main checkout:
`cp <main-checkout>/data/shaders/lighting/src/emitter_glow.*.hlsl <basepath>/data/shaders/lighting/src/`. `bnplay doctor --trial <windowed.toml>` checks them, a display session and stray game windows.
On Windows (D3D12, verified 2026-10-07) the game boots windowed without them: `emitter_glow_pass` logs `failed to load shader source` and the glow pass stays off. `doctor --trial` still fails the check, so a capture there runs without the glow effect.

`bnplay step <s> '{"cmd":"capture","tag":"original"}'` writes the final frame and a paired map snapshot under the Episode's `captures/` directory and reports `capture.frame`, `capture.map`, `width`, `height`. `mode:"state"` skips lighting and interface passes. A hidden, minimised or locked window answers `outcome: refused`, `reason: no_drawable`: never compare against a missing or stale frame. Capture oracles in the Trial compare tagged frames, all judged against a paired same-state null:
```toml
[[oracle]]
name = "glow toggles"
kind = "triplet"       # paired_null | diff_vs_null | triplet (1 -> 0 -> 1 must restore)
original = "original"  # tags you give your captures: the first two tagged original are the reference and its paired null
toggled = "toggled"
restored = "restored"
factor = 2             # effect must exceed factor x the null's noise
```

Toggles that work: the `probe_light_on` / `probe_light_off` Scenes (`run_scene`) add and remove one `radiant_core` two tiles east of the avatar. A `set_time` jump mid-Episode does not: the built-in `turn_counter` oracle fails it (the turn moves without `time_passed`), and a jump past `turn_limit` ends the Episode.

**Open limitation (Windows, verified 2026-10-07): same-state captures are not deterministic.** The avatar's tile changes shading on successive rendered frames (not with wall-clock time: a 20 s idle changes nothing; the next capture does), cycling through a few states while the rest of the frame stays byte-identical. Neither tile idle animations (`ANIMATIONS`) nor shader `anim_time` is the cause. The paired null is a single pair, so it reads 0 or about 2e-5 depending on where in the cycle the two `original` captures land. The toggled check is reliable for an effect far above that (the light toggle is about 4e-4); the restored check is not: with a 0 null, a restore that lands on another phase of the cycle fails (seen: 1.5e-5 against 0), and with a nonzero null it passes. Until this is fixed, read a triplet's restore verdict from a crop that excludes the avatar's tile.

## Driver unavailable

`doctor` reports `driver_available:false` and lists the fallbacks; bnplay never emulates the driver. Use: file triggers (`touch /tmp/cata_dump_trigger` for a frame and map dump; `echo <0-17> > /tmp/cata_dbg_mode`; `/tmp/cata_knob`; skill `cbn-headless-file-trigger-verification`; global filenames, one game at a time), the Windows harness (`tools/visual_verify/README.md`), and the Catch2 suite (`./out/build/osx-arm-slim/tests/cata_test-tiles "[tag]"`, AGENTS.md). Remember the ESC trap there.

## Configuration

Environment of the process that starts the daemon. Set it before the first `bnplay` call; run `bnplay shutdown` first to change it. Full list: header of `tools/bnplay/config.ts`. Tests: `deno task test:bnplay`.

| Variable | Default | Sets |
|---|---|---|
| `BNPLAY_BINARY` | `out/build/osx-arm-slim/src/cataclysm-bn-tiles` | game binary |
| `BNPLAY_BASEPATH` | this repo | checkout whose `data/` and `src/` the binary was built from |
| `BNPLAY_HOME` | `out/bnplay` | daemon state: socket, one directory per Episode |
| `BNPLAY_FIXTURES` | `tools/bnplay/fixtures` | fixture library |
| `BNPLAY_SCENES` | `tools/visual_verify/scenes` | where `run_scene` and a Trial's `scene` find Scenes |
| `BNPLAY_BOOT_TIMEOUT_MS` | 60000 | first ping |
| `BNPLAY_STEP_TIMEOUT_MS` | 30000 | a request that spends no game time |
| `BNPLAY_TURN_TIMEOUT_MS` | 100 | extra time per turn a request may spend |
| `BNPLAY_MAX_SESSIONS` | 2 | concurrent Episodes |
| `BNPLAY_ENDED_SESSIONS_KEPT` | 20 | ended sessions `report` still knows |
| `BNPLAY_IDLE_TIMEOUT_MS` | 600000 | idle reaper |
| `BNPLAY_MIN_FREE_MEMORY_MB` | 1024 | `doctor` memory floor |
| `BNPLAY_MIN_FREE_SWAP_MB` | 1024 | `doctor` swap floor |
