---
name: bnplay
description: Use when asked to playtest, verify gameplay, drive or run the game, reproduce an in-game bug, or check that a code change behaves in a running Cataclysm-BN. Teaches the bnplay observe, act, decide loop: start a session from a save, send driver commands, read the observations, stop.
---

# bnplay: start, step, stop

`bnplay` (`tools/bnplay/`, Deno) boots the game windowless on a private copy of a world save and
forwards your JSON commands to its `--driver-fd` driver. It observes; it judges nothing. You read
the observations and decide whether the change works.

- `bnplay <cmd>` below means `deno task bnplay <cmd>` from the repo root.
- The game stays resident between your calls; you pay the 7 to 10 s boot once per session.
- Success prints one JSON line on stdout. A refusal prints one `bnplay: <reason>` line on stderr
  and exits 2; the message names the live sessions, so a lost session id is recoverable.
- Isolation is real: `start` clones the save (copy-on-write on macOS) into a private user
  directory under `BNPLAY_HOME` (default `out/bnplay`), so the game can never touch your own save,
  and two sessions can never share one world.

## The loop

1. **Start.** `deno task bnplay start ~/Library/Application\ Support/Cataclysm-BN/save/MyWorld`
   prints `{"session":"ab12cd34","userdir":"...","log":"...","boot_ms":7110}`. The session id is
   the handle for every later call. Options: `--seed N` pins the RNG, `--date YYYY-MM-DD` and
   `--time HH:MM` pin the clock, all applied before the session reports ready. A start on a
   directory that is not a world save (no `worldoptions.json` / `master.gsav`) is refused.
2. **Step** until your question is answered:
   `deno task bnplay step ab12cd34 '{"cmd":"state"}'` prints the driver's response line.
   Exit 0 means the game answered `ok`; exit 1 means it answered an error (read the line — a
   refused command is an observation, not a crash). Never send the next command before reading the
   previous response.
3. **Stop.** `deno task bnplay stop ab12cd34` asks the game to quit and tears the session down;
   it prints `{"session":"...","ended":"stopped","exit_code":0}`. Stop even after something goes
   wrong; a session whose game died or hung is already ended and stop reports why.

A session nobody uses for 10 minutes (`BNPLAY_IDLE_TIMEOUT_MS`) is reaped by its holder, which
kills the game's whole process group. A request that times out (`BNPLAY_STEP_TIMEOUT_MS`, plus
`BNPLAY_TURN_TIMEOUT_MS` per game turn the request may spend) is a failure: the game is killed and
the session ends. There is no restart — start a new session.

## Commands (`step`)

One JSON object per call. The driver answers `{"id":N,"status":"ok",...}` or
`{"id":N,"status":"error","error":"..."}`.

| `cmd`                | fields                                | notes                                                            |
| -------------------- | ------------------------------------- | ---------------------------------------------------------------- |
| `ping`               |                                       | alive check                                                      |
| `state`              |                                       | turn, vitals, position, weather                                  |
| `view`               | `radius`                              | tiles around the avatar; `attach_view` attaches it to every reply |
| `attach_view`        | `radius`                              | `{"cmd":"attach_view","radius":3}`; `radius` 0 detaches          |
| `query`              | `topic` `inventory` \| `effects`      | what the avatar carries, what is wrong with it                   |
| `move`               | `dir` n/ne/e/se/s/sw/w/nw or `center` | one step; `center` waits in place                                |
| `wait`               | `turns` (1..1000)                     | time passes                                                      |
| `sleep`              | `max_turns`                           | sleep until dawn or an interruption                              |
| `action`             | `name` (e.g. `EXAMINE`)               | any player action by name                                        |
| `key`                | `key`                                 | answer an open menu; a modal blocks every other command          |
| item commands        | `item`, plus `anyway`, `method`       | `pickup` `drop` `wield` `wear` `take_off` `eat` `drink` `use` `read` `reload` |
| combat commands      | `dir` or `pos` `[dx,dy]`              | `melee` `fire` `smash`; one of the two, never both               |
| `craft`              | `recipe`, `max_turns`                 | refuse reasons name what is missing                              |
| `run_scene`          | `name`                                | a Lua Scene from `BNPLAY_SCENES` (default `tools/visual_verify/scenes`) |
| `seed` / `set_time`  | as `start`                            | re-pin mid-session; prefer the `start` options                   |
| `quit`               |                                       | prefer `bnplay stop`                                             |

Multi-turn commands (`craft`, `sleep`, `read`, `reload`, some `use`) take `max_turns`; the default
runs until the activity ends or is interrupted, and one request is capped at 1000 turns
(`interrupted`, reason `turn_cap`). Responses carry `outcome` (`success` / `refused` /
`interrupted` / `no_effect`), `time_passed`, `turn`, and new `messages`.

What is NOT here any more: oracles, reports, Trials, fixture baselines, `doctor`, windowed
capture. Pixel verification moved to the Catch2 Render tier (ADR-0006); gameplay judgement is
yours.

## Traps

- **Binary freshness.** The authoritative binary is `out/build/osx-arm-slim/src/cataclysm-bn-tiles`
  (Windows: `out/msvc/src/RelWithDebInfo/cataclysm-bn-tiles.exe`), not the repo-root copy. Point
  `BNPLAY_BINARY` wherever you built something else. A stale binary proves nothing about your diff.
- **One game per user directory.** Always go through `start`; never launch the driver yourself
  against a save another session owns. Two writers corrupt `map.sqlite3` silently.
- **Seed determinism is not guaranteed**: same-seed sessions can diverge at the first world step.
  Assert invariants (outcome, `time_passed`, monotonic turn), never exact world state or RNG values.
- **A menu left open blocks everything**: every gameplay command is refused with the prompt until
  you answer it with `key`.
- **`set_time` mid-session jumps the clock** without `time_passed`; pin at `start` instead.
- **The log.** `start` reports `log` (the game's `debug.log` under the session userdir); the
  holder's own events are in `holder.log` beside it. Read them before guessing at a boot failure.
- **Socket paths.** `BNPLAY_HOME` must keep `<home>/sessions/<id>/s` under 100 bytes (macOS
  `sockaddr_un`). The default is fine.
- **Windows**: `python` must be on PATH (the fd shim is Python; the driver serves stdio there
  instead). There is no default save location; pass any world save directory to `start`.

## Tests

`deno task test:bnplay` runs the CLI end to end against a fake driver in the test file (no game
needed): boot, pins, forwarding, clean stop, hang kill, death, idle reaping, refusals.
