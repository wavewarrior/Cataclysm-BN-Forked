/**
 * bnplay configuration, read from the environment of whichever command is run.
 *
 *   BNPLAY_HOME             one directory per session (its private user directory, the game's
 *                           log, the holder's socket and state file); default `out/bnplay` in the
 *                           repo. Keep it short: a unix socket path must stay under 100 bytes.
 *   BNPLAY_BINARY           game binary (default out/build/osx-arm-slim/src/cataclysm-bn-tiles;
 *                           on Windows out/msvc/src/RelWithDebInfo/cataclysm-bn-tiles.exe)
 *   BNPLAY_BASEPATH         `--basepath` for the game (default the repo root)
 *   BNPLAY_SCENES           directory of the Lua Scenes `run_scene` runs
 *                           (default tools/visual_verify/scenes)
 *   BNPLAY_BOOT_TIMEOUT_MS  how long a game may take to answer its first ping (default 60000)
 *   BNPLAY_STEP_TIMEOUT_MS  how long a request that spends no game time may take before the game
 *                           counts as hung (default 30000)
 *   BNPLAY_TURN_TIMEOUT_MS  extra time a request gets for each game turn it may spend, up to the
 *                           driver's cap of 1000 turns a request (default 100; an empty world runs
 *                           a turn in about 2 ms); see step_timeout.ts
 *   BNPLAY_IDLE_TIMEOUT_MS  a session that gets no request for this long is reaped by its holder,
 *                           which kills the game by process group and exits (default 600000)
 */
import { dirname, fromFileUrl, join } from "@std/path"

const here = dirname(fromFileUrl(import.meta.url))
export const REPO_ROOT = dirname(dirname(here))

/** Windows has no unix sockets in Deno, no process groups and no inherited fd 3. */
export const IS_WINDOWS = Deno.build.os === "windows"

export type Config = {
  home: string
  binary: string
  basepath: string
  scenesDir: string
  bootTimeoutMs: number
  stepTimeoutMs: number
  turnTimeoutMs: number
  idleTimeoutMs: number
}

function integer(name: string, fallback: number, min: number): number {
  const raw = Deno.env.get(name)
  if (raw === undefined) return fallback
  const n = Number(raw)
  if (!Number.isInteger(n) || n < min) {
    throw new Error(`${name} must be an integer of at least ${min}, got ${raw}`)
  }
  return n
}

export function loadConfig(): Config {
  return {
    home: Deno.env.get("BNPLAY_HOME") ?? join(REPO_ROOT, "out", "bnplay"),
    binary: Deno.env.get("BNPLAY_BINARY") ??
      (IS_WINDOWS
        ? join(REPO_ROOT, "out", "msvc", "src", "RelWithDebInfo", "cataclysm-bn-tiles.exe")
        : join(REPO_ROOT, "out", "build", "osx-arm-slim", "src", "cataclysm-bn-tiles")),
    basepath: Deno.env.get("BNPLAY_BASEPATH") ?? REPO_ROOT,
    scenesDir: Deno.env.get("BNPLAY_SCENES") ?? join(REPO_ROOT, "tools", "visual_verify", "scenes"),
    bootTimeoutMs: integer("BNPLAY_BOOT_TIMEOUT_MS", 60_000, 1),
    stepTimeoutMs: integer("BNPLAY_STEP_TIMEOUT_MS", 30_000, 1),
    turnTimeoutMs: integer("BNPLAY_TURN_TIMEOUT_MS", 100, 0),
    idleTimeoutMs: integer("BNPLAY_IDLE_TIMEOUT_MS", 600_000, 1),
  }
}

/**
 * The socket a session's holder listens on (POSIX). Windows has no unix sockets in Deno: there the
 * holder listens on loopback TCP and records the port in `portPath`.
 */
export function socketPath(dir: string): string {
  const path = join(dir, "s")
  // sockaddr_un.sun_path is 104 bytes on macOS.
  if (new TextEncoder().encode(path).length > 100) {
    throw new Error(`BNPLAY_HOME is too long for a unix socket path: ${path}`)
  }
  return path
}

/** Windows: the file holding the TCP port the holder listens on. */
export function portPath(dir: string): string {
  return join(dir, "port")
}
