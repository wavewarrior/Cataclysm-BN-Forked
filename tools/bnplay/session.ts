/**
 * One session: its directory on disk, the state file the front end and the holder exchange through
 * it, and the line-JSON transport between them.
 *
 * Layout under `BNPLAY_HOME`:
 *
 *   sessions/<id>/state.json   the handshake: `booting` until the game answers its first ping,
 *                              then `ready`, then `ended` with the reason and the game's exit code
 *   sessions/<id>/userdir/     the session's private user directory; the holder clones the save
 *                              `start` named into `save/bnplay` inside it
 *   sessions/<id>/holder.log   the holder's own events and, on POSIX, the game's stderr
 *   sessions/<id>/s            POSIX: the unix socket the holder listens on
 *   sessions/<id>/port         Windows: the TCP port the holder listens on
 *
 * The holder is a detached process (`main.ts hold`): it is what keeps the game resident between
 * CLI calls, one process per session, gone when the session ends.
 */
import { copy } from "@std/fs"
import { join } from "@std/path"
import { IS_WINDOWS, portPath, REPO_ROOT, socketPath } from "./config.ts"
import type { DriverRequest } from "./client.ts"

/** A refusal the front end prints as its own error (exit 2), not as an observation. */
export class SessionError extends Error {
  constructor(message: string) {
    super(message)
    this.name = "SessionError"
  }
}

export type SessionState = {
  session: string
  /** Pid of the holder, so a wedged one can be killed by hand or by `stop`. */
  pid?: number
  status: "booting" | "ready" | "ended"
  /** Absolute path of the world save `start` was given, for the record. */
  save?: string
  userdir?: string
  boot_ms?: number
  /** Applied by the holder before the session is ready: a pinned RNG seed, a pinned clock. */
  seed?: number
  date?: string
  time?: string
  /** Set once the session ends: why, and the game's exit code when it was asked to quit. */
  ended?: string
  exit_code?: number
  /** Why a boot failed, when the session ended before it was ever ready. */
  error?: string
  log?: string
}

export function sessionDir(home: string, id: string): string {
  return join(home, "sessions", id)
}

export function statePath(dir: string): string {
  return join(dir, "state.json")
}

export function logPath(dir: string): string {
  return join(dir, "holder.log")
}

export async function readState(dir: string): Promise<SessionState | undefined> {
  const text = await Deno.readTextFile(statePath(dir)).catch(() => undefined)
  return text === undefined ? undefined : JSON.parse(text) as SessionState
}

/** Publishes state atomically: a reader never sees a half-written file. */
export async function writeState(dir: string, state: SessionState): Promise<void> {
  const tmp = `${statePath(dir)}.${crypto.randomUUID().slice(0, 8)}.tmp`
  await Deno.writeTextFile(tmp, JSON.stringify(state) + "\n")
  await Deno.rename(tmp, statePath(dir))
}

/**
 * Refuses a directory that is not a world save, so a typo fails here instead of booting a game
 * that quietly creates a new world.
 */
export async function validateSave(path: string): Promise<string> {
  const real = await Deno.realPath(path).catch(() => undefined)
  if (real === undefined) throw new SessionError(`no such save directory: ${path}`)
  const markers = ["worldoptions.json", "master.gsav"]
  const found = await Promise.all(
    markers.map((m) => Deno.lstat(join(real, m)).then(() => true, () => false)),
  )
  if (!found.some(Boolean)) {
    throw new SessionError(`${path} is not a world save: it has no ${markers.join(" or ")}`)
  }
  return real
}

/**
 * Clones a world directory so a session can never write to the save it started from. Where the
 * filesystem supports it the clone is copy-on-write and costs no disk: `cp -c` on macOS,
 * `--reflink=auto` on Linux. Windows has no such clone; a plain recursive copy keeps the same
 * isolation.
 */
export async function cloneTree(source: string, target: string): Promise<void> {
  if (IS_WINDOWS) await copy(source, target)
  else if (Deno.build.os === "darwin") await runCloned("cp", ["-cR", source, target])
  else await runCloned("cp", ["--reflink=auto", "-R", source, target])
}

async function runCloned(cmd: string, args: string[]): Promise<void> {
  const out = await new Deno.Command(cmd, { args, stdout: "null", stderr: "piped" }).output()
  if (!out.success) {
    throw new Error(`${cmd} failed: ${new TextDecoder().decode(out.stderr).trim()}`)
  }
}

/** One front-end request to the holder of a session. */
export type HolderRequest = { op: "step"; request: DriverRequest } | { op: "stop" }

/** The holder's answer: its own verdict, wrapping the driver's response for a step. */
export type HolderReply = { ok: true; result: object } | { ok: false; error: string }

/** Opens a connection to the holder of `dir`; throws when none is listening. */
export async function connectHolder(dir: string): Promise<Deno.Conn> {
  if (IS_WINDOWS) {
    const port = Number.parseInt(
      await Deno.readTextFile(portPath(dir)).catch(() => ""),
      10,
    )
    if (!Number.isInteger(port) || port <= 0) {
      throw new SessionError(`no usable port recorded under ${dir}`)
    }
    return await Deno.connect({ transport: "tcp", hostname: "127.0.0.1", port })
  }
  return await Deno.connect({ transport: "unix", path: socketPath(dir) })
}

/**
 * Yields complete lines from a byte stream. `preventCancel` matters: an early `break` out of a
 * consumer must not cancel the stream, because cancelling a Conn's readable closes the whole
 * socket, and a reader that got its line still has to write its reply.
 */
export async function* readLines(stream: ReadableStream<Uint8Array>): AsyncGenerator<string> {
  const decoder = new TextDecoder()
  let buffer = ""
  for await (const chunk of stream.values({ preventCancel: true })) {
    buffer += decoder.decode(chunk, { stream: true })
    let nl: number
    while ((nl = buffer.indexOf("\n")) >= 0) {
      yield buffer.slice(0, nl)
      buffer = buffer.slice(nl + 1)
    }
  }
  if (buffer.length > 0) yield buffer
}

/**
 * Sends one request to the holder and returns its reply. The timeout bounds the whole exchange:
 * a holder wedged inside a hung game answers nothing and must not hang the front end too. A
 * session that has ended answers with its recorded reason, not a socket error.
 */
export async function askHolder(
  dir: string,
  request: HolderRequest,
  timeoutMs: number,
): Promise<HolderReply> {
  const conn = await connectHolder(dir).catch(async (e: unknown) => {
    const state = await readState(dir)
    if (state?.status === "ended") {
      throw new SessionError(
        `session ${state.session} ended (${state.ended}${state.error ? `: ${state.error}` : ""})`,
      )
    }
    throw new SessionError(`cannot reach the holder of this session: ${msg(e)}`)
  })
  try {
    await conn.writable.getWriter().write(new TextEncoder().encode(JSON.stringify(request) + "\n"))
    const first = firstLine(conn.readable).catch(() => undefined)
    const { promise, reject } = Promise.withResolvers<never>()
    const timer = setTimeout(
      () => reject(new SessionError("the holder did not answer in time")),
      timeoutMs,
    )
    try {
      const line = await Promise.race([first, promise])
      if (line === undefined) {
        throw new SessionError("the holder closed the connection without answering")
      }
      return JSON.parse(line) as HolderReply
    } finally {
      clearTimeout(timer)
    }
  } finally {
    try {
      conn.close()
    } catch { /* the stream already took the socket with it */ }
  }
}

function msg(e: unknown): string {
  return e instanceof Error ? e.message : String(e)
}

/** The first non-empty line of `stream`, or undefined when it ends without one. */
async function firstLine(stream: ReadableStream<Uint8Array>): Promise<string | undefined> {
  for await (const line of readLines(stream)) {
    if (line.trim()) return line
  }
  return undefined
}
/**
 * Waits for the holder of `dir` to leave `booting`: ready, or ended with the reason. Polls the
 * state file because a boot that fails before the socket exists has nothing to connect to.
 */
export async function waitForBoot(dir: string, timeoutMs: number): Promise<SessionState> {
  const deadline = Date.now() + timeoutMs
  for (;;) {
    const state = await readState(dir)
    if (state && state.status !== "booting") return state
    if (Date.now() > deadline) {
      throw new SessionError(
        `the game did not report ready within ${timeoutMs}ms; see ${logPath(dir)}`,
      )
    }
    await new Promise((resolve) => setTimeout(resolve, 100))
  }
}

const MAIN = join(REPO_ROOT, "tools", "bnplay", "main.ts")

/**
 * Starts the session's holder detached, in its own session so closing this shell does not take the
 * game down with it. POSIX: a `setsid` plus two `dup2` through `python3` puts its output (and the
 * game's stderr, which the fd shim forwards) in the session's log. Windows: detached with null
 * stdio; the holder writes its own events to the log itself.
 */
export function spawnHolder(session: string, log: string): void {
  const deno = [
    "run",
    "--allow-run",
    "--allow-read",
    "--allow-write",
    "--allow-net",
    "--allow-env",
    "--config",
    join(REPO_ROOT, "deno.jsonc"),
    MAIN,
    "hold",
    session,
  ]
  const command = IS_WINDOWS
    ? { command: Deno.execPath(), args: deno, stdout: "null" as const, stderr: "null" as const }
    : {
      command: "/usr/bin/python3",
      args: [
        "-c",
        "import os, sys\n" +
        "try:\n os.setsid()\nexcept OSError:\n pass\n" +
        "fd = os.open(sys.argv[1], os.O_WRONLY | os.O_CREAT | os.O_APPEND)\n" +
        "os.dup2(fd, 1)\n" +
        "os.dup2(fd, 2)\n" +
        "os.execv(sys.argv[2], sys.argv[2:])",
        log,
        Deno.execPath(),
        ...deno,
      ],
      stdout: "null" as const,
      stderr: "null" as const,
    }
  const child = new Deno.Command(command.command, {
    args: command.args,
    cwd: REPO_ROOT,
    stdin: "null",
    stdout: command.stdout,
    stderr: command.stderr,
    detached: true,
  }).spawn()
  child.unref()
}

/** Ends a session whose holder will not answer: kill it, and the game it leads dies with it. */
export function killHolder(pid: number): void {
  try {
    Deno.kill(pid, "SIGKILL")
  } catch { /* already gone */ }
}
