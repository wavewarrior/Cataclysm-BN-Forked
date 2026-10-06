/**
 * Supervisor configuration, read from the environment of the process that starts the daemon.
 *
 *   BNPLAY_HOME             daemon state: socket, log and one directory per Episode
 *                           (default out/bnplay in the repo)
 *   BNPLAY_FIXTURES         fixture library, one directory per fixture holding a world save
 *                           (default tools/bnplay/fixtures, gitignored)
 *   BNPLAY_BINARY           game binary (default out/build/osx-arm-slim/src/cataclysm-bn-tiles)
 *   BNPLAY_BASEPATH         `--basepath` for the game (default the repo root)
 *   BNPLAY_BOOT_TIMEOUT_MS  how long a game may take to answer its first ping (default 60000)
 *   BNPLAY_STEP_TIMEOUT_MS  how long one request may take before the game counts as hung
 *                           (default 30000)
 *   BNPLAY_MAX_SESSIONS     how many Episodes may run at once; a `start` beyond it is refused
 *                           (default 2)
 *   BNPLAY_IDLE_TIMEOUT_MS  an Episode that gets no request for this long is killed by process group
 *                           and ends as a harness error (default 600000, ten minutes)
 *   BNPLAY_BASELINE_IDLE_MS how long `fixture baseline` lets the game idle after it reports ready,
 *                           logging whatever it logs at idle (default 3000)
 */
import { dirname, fromFileUrl, join } from "@std/path"

const here = dirname(fromFileUrl(import.meta.url))
export const REPO_ROOT = dirname(dirname(here))

export type Config = {
  home: string
  fixtures: string
  binary: string
  basepath: string
  bootTimeoutMs: number
  stepTimeoutMs: number
  maxSessions: number
  idleTimeoutMs: number
  baselineIdleMs: number
}

function positiveInteger(name: string, fallback: number): number {
  const raw = Deno.env.get(name)
  if (raw === undefined) return fallback
  const n = Number(raw)
  if (!Number.isInteger(n) || n <= 0) throw new Error(`${name} must be a positive integer`)
  return n
}

export function loadConfig(): Config {
  return {
    home: Deno.env.get("BNPLAY_HOME") ?? join(REPO_ROOT, "out", "bnplay"),
    fixtures: Deno.env.get("BNPLAY_FIXTURES") ?? join(here, "fixtures"),
    binary: Deno.env.get("BNPLAY_BINARY") ??
      join(REPO_ROOT, "out", "build", "osx-arm-slim", "src", "cataclysm-bn-tiles"),
    basepath: Deno.env.get("BNPLAY_BASEPATH") ?? REPO_ROOT,
    bootTimeoutMs: positiveInteger("BNPLAY_BOOT_TIMEOUT_MS", 60_000),
    stepTimeoutMs: positiveInteger("BNPLAY_STEP_TIMEOUT_MS", 30_000),
    maxSessions: positiveInteger("BNPLAY_MAX_SESSIONS", 2),
    idleTimeoutMs: positiveInteger("BNPLAY_IDLE_TIMEOUT_MS", 600_000),
    baselineIdleMs: positiveInteger("BNPLAY_BASELINE_IDLE_MS", 3_000),
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
