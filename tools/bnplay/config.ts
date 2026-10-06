/**
 * Supervisor configuration, read from the environment of the process that starts the daemon.
 *
 *   BNPLAY_HOME             daemon state: socket, log and one directory per Episode
 *                           (default out/bnplay in the repo)
 *   BNPLAY_FIXTURES         fixture library, one directory per fixture holding a world save
 *                           (default tools/bnplay/fixtures, gitignored)
 *   BNPLAY_BINARY           game binary (default out/build/osx-arm-slim/src/cataclysm-bn-tiles)
 *   BNPLAY_BASEPATH         `--basepath` for the game (default the repo root)
 *   BNPLAY_SCENES           directory of the Lua Scenes `run_scene` and a Trial's `scene` run
 *                           (default tools/visual_verify/scenes)
 *   BNPLAY_BOOT_TIMEOUT_MS  how long a game may take to answer its first ping (default 60000)
 *   BNPLAY_STEP_TIMEOUT_MS  how long a request that spends no game time may take before the game
 *                           counts as hung (default 30000)
 *   BNPLAY_TURN_TIMEOUT_MS  extra time a request gets for each game turn it may spend, up to the
 *                           driver's cap of 1000 turns a request (default 100; an empty world runs
 *                           a turn in about 2 ms). A request has BNPLAY_STEP_TIMEOUT_MS plus this
 *                           times its turns (a `wait`'s `turns`, an activity's `max_turns`, or the
 *                           whole cap when it names no limit), see step_timeout.ts
 *   BNPLAY_MAX_SESSIONS     how many Episodes may run at once; a `start` beyond it is refused
 *                           (default 2)
 *   BNPLAY_ENDED_SESSIONS_KEPT  how many ended sessions the daemon remembers, so `report` still
 *                           works on them; older ones are forgotten when a new session starts, and
 *                           their transcripts stay on disk (default 20)
 *   BNPLAY_IDLE_TIMEOUT_MS  an Episode that gets no request for this long is killed by process group
 *                           and ends as a harness error (default 600000, ten minutes)
 *   BNPLAY_BASELINE_IDLE_MS how long `fixture baseline` lets the game idle after it reports ready,
 *                           logging whatever it logs at idle (default 3000)
 *   BNPLAY_MIN_FREE_MEMORY_MB  `doctor` fails below this much available memory (free, inactive
 *                           and speculative pages; default 1024, a game holds about 1 GB)
 *   BNPLAY_MIN_FREE_SWAP_MB `doctor` fails below this much free swap (default 1024; swap has been
 *                           exhausted on this machine before)
 *   BNPLAY_SELFCHECK_IDLE_MS how long each Episode of `doctor --self-check` idles after it is
 *                           seeded, before its first world step (default 3000)
 *   BNPLAY_LAUNCHCTL        the `launchctl` that `doctor` asks for the session type a windowed
 *                           Trial needs (default `launchctl`; `Aqua` is a graphical login)
 */
import { dirname, fromFileUrl, join } from "@std/path"

const here = dirname(fromFileUrl(import.meta.url))
export const REPO_ROOT = dirname(dirname(here))

export type Config = {
  home: string
  fixtures: string
  binary: string
  basepath: string
  scenesDir: string
  bootTimeoutMs: number
  stepTimeoutMs: number
  turnTimeoutMs: number
  maxSessions: number
  endedSessionsKept: number
  idleTimeoutMs: number
  baselineIdleMs: number
  minFreeMemoryMb: number
  minFreeSwapMb: number
  selfCheckIdleMs: number
  launchctl: string
}

function positiveInteger(name: string, fallback: number): number {
  const raw = Deno.env.get(name)
  if (raw === undefined) return fallback
  const n = Number(raw)
  if (!Number.isInteger(n) || n <= 0) throw new Error(`${name} must be a positive integer`)
  return n
}

function nonNegativeInteger(name: string, fallback: number): number {
  const raw = Deno.env.get(name)
  if (raw === undefined) return fallback
  const n = Number(raw)
  if (!Number.isInteger(n) || n < 0) throw new Error(`${name} must be a non-negative integer`)
  return n
}

export function loadConfig(): Config {
  return {
    home: Deno.env.get("BNPLAY_HOME") ?? join(REPO_ROOT, "out", "bnplay"),
    fixtures: Deno.env.get("BNPLAY_FIXTURES") ?? join(here, "fixtures"),
    binary: Deno.env.get("BNPLAY_BINARY") ??
      join(REPO_ROOT, "out", "build", "osx-arm-slim", "src", "cataclysm-bn-tiles"),
    basepath: Deno.env.get("BNPLAY_BASEPATH") ?? REPO_ROOT,
    scenesDir: Deno.env.get("BNPLAY_SCENES") ?? join(REPO_ROOT, "tools", "visual_verify", "scenes"),
    bootTimeoutMs: positiveInteger("BNPLAY_BOOT_TIMEOUT_MS", 60_000),
    stepTimeoutMs: positiveInteger("BNPLAY_STEP_TIMEOUT_MS", 30_000),
    turnTimeoutMs: nonNegativeInteger("BNPLAY_TURN_TIMEOUT_MS", 100),
    maxSessions: positiveInteger("BNPLAY_MAX_SESSIONS", 2),
    endedSessionsKept: nonNegativeInteger("BNPLAY_ENDED_SESSIONS_KEPT", 20),
    idleTimeoutMs: positiveInteger("BNPLAY_IDLE_TIMEOUT_MS", 600_000),
    baselineIdleMs: positiveInteger("BNPLAY_BASELINE_IDLE_MS", 3_000),
    minFreeMemoryMb: nonNegativeInteger("BNPLAY_MIN_FREE_MEMORY_MB", 1_024),
    minFreeSwapMb: nonNegativeInteger("BNPLAY_MIN_FREE_SWAP_MB", 1_024),
    selfCheckIdleMs: nonNegativeInteger("BNPLAY_SELFCHECK_IDLE_MS", 3_000),
    launchctl: Deno.env.get("BNPLAY_LAUNCHCTL") ?? "launchctl",
  }
}

/** Unix socket the daemon listens on. */
export function socketPath(home: string): string {
  const path = join(home, "daemon.sock")
  // sockaddr_un.sun_path is 104 bytes on macOS.
  if (new TextEncoder().encode(path).length > 100) {
    throw new Error(`BNPLAY_HOME is too long for a unix socket path: ${path}`)
  }
  return path
}
