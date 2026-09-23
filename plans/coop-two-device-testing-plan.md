# Co-op two-device testing plan

## Context

Single-machine co-op testing (two processes on this dev box) surfaced and fixed two real
bugs (below), but same-device testing itself is not a reliable substitute for the real
scenario — two players on separate machines over a LAN/WAN. This plan hands off actual
co-op verification to the user across two real devices, with everything needed to reproduce
cleanly and know what "working" looks like.

## What was fixed this session (code-level, already verified)

Both fixes are narrow, root-caused, and covered by re-running `./cata_test-tiles "[coop]"
--rng-seed 1` after each (exit 0, "All tests passed (552 assertions in 159 test cases)"
both times) plus a fresh full rebuild.

1. **False test failure — GPU init severity mismatch** (`src/compute/gpu_platform.cpp`,
   `src/preload_config.{h,cpp}`, `tests/test_main.cpp`). The test binary's implicit default
   GPU-compute-backend guess (`gpu_software`) logged at `DL::Error` when unavailable in a
   headless/no-GPU environment, even though the caller already has a working fallback to
   CPU compute right after. Catch2 treats any `DL::Error` during a run as an overall
   failure regardless of assertion results, so `[coop]` (and the full suite) reported
   `exit 1` despite every actual test passing. Fixed by adding
   `preload_config::set_require_gpu_device(bool)`: `true` (log `DL::Error`, unchanged
   behavior) for an *explicit* `CATA_TEST_COMPUTE_ACCELERATION` env var or `COMPUTE_ACCELERATION`
   option; `false` (log `DL::Warn` instead) for the implicit default guess. Verified: the
   implicit path now logs `WARNING` and exits 0; an explicit
   `CATA_TEST_COMPUTE_ACCELERATION=gpu_software` request still logs `ERROR` and exits 1,
   preserving the documented "reproduces the stuck-GPU-path behaviour on purpose" regression-
   attribution use case.
2. **Real crash — client join SIGSEGV** (`src/coop_client.cpp`). `coop_menu::start_join()`
   calls `g->setup()` unconditionally right after `receive_world_seed()`. When the client
   doesn't have a local world by the host's exact name (the expected case for a genuinely
   fresh client — different machine, never played this world before), the prior code just
   logged and left `world_generator->active_world` unset. `game::setup()` →
   `init::load_world_modfiles(ui, get_active_world(), …)` has no null check on that pointer
   and segfaults immediately (`crash.log` confirmed: SIGSEGV in `load_world_modfiles` ←
   `game::setup` ← `coop_menu::run`). Fixed: when the named world isn't found locally,
   `receive_world_seed()` now calls `world_generator->make_new_world(false, "")` to
   bootstrap a throwaway local world (WORLDINFO's constructor seeds it with
   `mod_manager::get_default_mods()`, matching an unconfigured client) and sets it active,
   giving `load_world_modfiles` something real to load. The guest never saves through this
   world (Track A design — map state arrives entirely via network sync), so its exact
   name/identity doesn't matter.

**Practical implication for testing:** a client joining for the first time, with no local
world of the same name as the host's, previously crashed on every attempt. This is now the
common case you'll hit in real two-device testing (different machines almost never share a
world by name) — verify it no longer crashes as step 1 of the plan below.

## Known non-blocking noise (do not mistake for a new bug)

Both are dismissible with the space bar, pre-existing, and unrelated to co-op or the fixes
above — confirmed reproducible on a completely fresh `--userdir` with **no** co-op involved
at all (hit during plain single-player `Play Now!` testing this session too):

- **Once per fresh `--userdir`:** a debugmsg `invalid weather_type id "null"` from
  `generic_factory.h:463`, on first launch after language selection, before the main menu
  ever renders. One `space` press clears it permanently for that userdir.
- **On every fresh world's first few turns:** a burst (20–40) of debugmsg popups
  `map active bounds view incomplete after map::shift; missing submap (X,Y,-10) in
  dimension ''` from `map.cpp:636`, one per missing z=-10 column during initial map-cache
  population. Bounded — stops on its own once the initial cache settles. Hold/spam `space`
  through them; gameplay proceeds normally afterward.

If either debugmsg burst appears to *not* terminate (keeps going indefinitely, past ~50
presses), that would be new and worth reporting — but in every reproduction this session it
was bounded.

## Setup (two physical/virtual machines on the same LAN)

1. **Build** on each machine from this branch (`feature/fast-track-to-co-op`, current HEAD
   with the two fixes above) using the `osx-arm-slim` preset (or the platform-appropriate
   preset if not both macOS):
   ```sh
   cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles
   ```
   Never run this synchronously with a short timeout — see repo `AGENTS.md` build rules. If
   you hit `find_library ... ncursesw` or `BUILD_SHADERCROSS disabled and precompiled
   platform shaders are missing: ...spv` on a from-scratch configure, the cache needs
   `-DSHADER_TARGETS=msl` explicitly (this preset is Metal/MSL-only on macOS; the
   CMakeLists.txt default of `spirv;msl` requires `.spv` files this repo never generates
   here):
   ```sh
   cmake --preset osx-arm-slim -DSHADER_TARGETS=msl
   cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles
   ```
2. **Port.** Co-op's default port is `8080` (`COOP_PORT` option, `Settings` in-game or
   `config/options.json`), which collides with extremely common local dev tooling
   (`http-alt` — confirmed a real collision on this dev box, from an unrelated launcher
   process already listening there). Before hosting, either confirm 8080 is free on the
   host machine (`lsof -i :8080`) or set `COOP_PORT` to something else (e.g. `18080`) in
   `Settings → Co-op` (or `config/options.json`) **on the host**; the client then supplies
   the same port via the `IP:port` override in the join dialog (no client-side option
   change needed).
3. **Host character.** On the host machine: `New Game → Play Now! (Default Scenario)` spawns
   a safe Evacuee in an evac shelter (no immediate threats). **Persist it** with
   `ESC → Save and Quit (Shift+S, then Y to confirm)` — a killed process loses the
   character and leaves the world save-less, and `coop_menu::start_host()` refuses a
   world with no saves.
4. **Host, from a freshly launched process** (loading a world, returning to menu, and
   reloading in the *same* process deadlocks at `Verifying N/59 Materials` — always relaunch
   the binary before hosting): `Co-op → Host a game`. Note the menu's cursor may default to
   the last-highlighted item (`Back`), not always `Host a game` — read the highlighted row
   before pressing Enter. Wait for `[coop] listening on port <port>` in
   `config/debug.log`, or the in-game message `Hosting on <LAN-IP>:<port> — waiting for
   partner...`.
5. **Client, from a freshly launched process** on the second machine: `Co-op → Join a game`,
   enter `<host-LAN-IP>[:<port>]` (omit `:port` only if using the 8080 default and it's
   free on the host). This is the path that previously crashed — confirm it does **not**
   now (see step 1 of Verification below).

## Verification — the plan's original "done" bar

Both conditions required; either alone is not sufficient:

**A. `[coop]` test suite, on each machine (or at minimum the one you build on):**
```sh
./cata_test-tiles "[coop]" --rng-seed 1
echo "exit=$?"
```
Expect a normal Catch2 summary ending `All tests passed (552 assertions in 159 test
cases)` and `exit=0`. (If you see `exit=1` with `Treating result as failure due to error
logged during initialization` and an `ERROR: SDL_GPU: device creation failed; selected GPU
compute backend is unavailable` — that's the bug from fix #1 above; means your build predates
it or the fix didn't apply. Re-pull/rebuild.)

**B. Live two-device session**, a multi-minute gameplay loop with no desync or crash:
1. **Join succeeds without crashing.** This is the headline regression test for fix #2.
   Client-side debug.log should show, in order: `connected to …` → `client handshake
   complete` → `received world_seed` → `applied world_seed to avatar` — and the client
   should land in gameplay, not on the main menu with a crash dialog. Check
   `config/crash.log.dmp` does *not* appear.
2. **Host-side confirmation**, same session, `config/debug.log`: `client TCP connected` →
   `handshake complete` → `join_info: client start (…)` → `proxy NPC spawned: <name>` →
   `client join finalized — session active`.
3. **Client avatar has a real body.** Check the client's own sidebar shows actual limb bars
   (HEAD/TORSO/arms/legs), not just STA/MANA. A body-less proxy (a known distinct failure
   mode, unrelated to this session's fixes) divides by zero in
   `Character::hp_percentage()` inside the per-tick status packet.
4. **Simulation actually runs on both sides.** Drive movement (all four directions), a
   pickup/drop, and a melee/unarmed attack from both the host and the client. Confirm the
   host's and client's ambient temperature and `COND` string (e.g. `Sated`/`Hydrated`)
   both advance and agree with each other over a few minutes of play.
5. **No desync.** No repeated `[coop] checksum mismatch` messages in either log; no
   "partner disconnected" alerts during active play (a *host-initiated* graceful
   disconnect/reconnect test — quit and rejoin the client mid-session — should show
   `[ALERT] Partner disconnected! Waiting for reconnection...` on the host followed by a
   clean re-join, not a stuck 300s-reconnect-window rejection; if a *fresh* join attempt is
   rejected, the host is mid-way through its own reconnection window from a *prior* attempt
   — restart the host process, don't fight the state machine).
6. **Cleanup.** Kill both processes; if you used throwaway `--userdir` sandboxes (recommended
   — do not point either machine at a real save you care about while testing), delete them.

## If step B.1 (client join) still crashes

That would mean fix #2 didn't fully close the gap. Capture and report:
- The client's `config/crash.log` (full stack trace) and `config/crash.log.dmp` presence.
- The last ~20 lines of client `config/debug.log` before the crash.
- Whether the client had *any* pre-existing local world (named anything) before joining —
  the fix's bootstrap path only triggers when `has_world(host_world_name)` is false; if the
  client happens to have a world of that exact name already, it takes the original
  `set_active_world()` path instead, which is unaffected by this fix and was never the
  crashing path.
