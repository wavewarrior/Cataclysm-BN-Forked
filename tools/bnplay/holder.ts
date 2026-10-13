/**
 * The session holder: the detached process that keeps one game alive between CLI calls.
 *
 * `bnplay start` spawns this detached (`session.ts spawnHolder`) and waits on the state file; the
 * CLI then talks to it over the session's socket. It serves one request at a time:
 *
 *   {"op":"step","request":{...}}   forward the driver request, answer {"ok":true,"result":<response>}
 *   {"op":"stop"}                   ask the game to quit, answer {"ok":true,"result":{...summary}}
 *
 * A step that times out kills the game's process group — a hang is a failure, and no game process
 * may linger — records the session as ended and shuts down, so the next call cannot trust a wedged
 * world. An idle session is reaped the same way after `BNPLAY_IDLE_TIMEOUT_MS`.
 */
import { join } from "@std/path"
import { IS_WINDOWS, loadConfig, portPath, socketPath } from "./config.ts"
import type { Config } from "./config.ts"
import { DriverTimeout, spawnDriver } from "./client.ts"
import type { Driver, DriverRequest } from "./client.ts"
import { stepTimeout } from "./step_timeout.ts"
import { cloneTree, logPath, readLines, statePath, writeState } from "./session.ts"
import type { HolderReply, SessionState } from "./session.ts"

/** How long a graceful `quit` gets before the holder kills the game instead. */
const QUIT_TIMEOUT_MS = 10_000

/** Appends to the session log and echoes to the holder's own stdout (its redirect). */
class Log {
  #path: string
  constructor(path: string) {
    this.#path = path
  }
  async line(message: string): Promise<void> {
    console.log(message)
    await Deno.writeTextFile(this.#path, `${new Date().toISOString()} ${message}\n`, {
      create: true,
      append: true,
    }).catch(() => {})
  }
}

async function readStateOrNull(dir: string): Promise<SessionState | undefined> {
  const text = await Deno.readTextFile(statePath(dir)).catch(() => undefined)
  return text === undefined ? undefined : JSON.parse(text) as SessionState
}

export async function hold(session: string): Promise<number> {
  const config = loadConfig()
  const dir = join(config.home, "sessions", session)
  const log = new Log(logPath(dir))
  const opened = await readStateOrNull(dir)
  if (!opened || opened.status !== "booting") {
    await log.line(`hold: ${session} is not a session waiting to boot`)
    return 2
  }
  // Claim the session: our pid, so `stop` can reap us if we ever stop answering.
  let state: SessionState = { ...opened, pid: Deno.pid }
  await writeState(dir, state)
  const save = state.save!
  const userdir = join(dir, "userdir")
  const end = async (ended: string, extra: Partial<SessionState> = {}) => {
    state = { ...state, status: "ended", ended, ...extra }
    await writeState(dir, state)
  }

  try {
    await Deno.mkdir(join(userdir, "save"), { recursive: true })
    await cloneTree(save, join(userdir, "save", "bnplay"))
  } catch (e) {
    await end("clone_failed", { error: msg(e) })
    await log.line(`hold: cloning ${save} failed: ${msg(e)}`)
    return 1
  }

  const driver = spawnDriver({
    binary: config.binary,
    userdir,
    world: "bnplay",
    basepath: config.basepath,
    firstTimeoutMs: config.bootTimeoutMs,
    requestTimeoutMs: config.stepTimeoutMs,
    stderr: IS_WINDOWS ? "null" : "inherit",
    scenesDir: config.scenesDir,
  })
  await log.line(`hold: spawned ${config.binary} for session ${session}`)

  let server: Deno.Listener
  try {
    server = await listen(dir)
  } catch (e) {
    driver.kill()
    await end("listen_failed", { error: msg(e) })
    await log.line(`hold: cannot listen: ${msg(e)}`)
    return 1
  }

  const bootedAt = Date.now()
  try {
    const ping = await driver.send({ cmd: "ping" }, config.bootTimeoutMs)
    if (ping.status !== "ok") throw new Error(`the game refused ping: ${JSON.stringify(ping)}`)
    await pin(driver, state)
  } catch (e) {
    driver.kill()
    await end("boot_failure", { error: msg(e) })
    await log.line(`hold: boot failed: ${msg(e)}`)
    return 1
  }
  const boot_ms = Date.now() - bootedAt
  state = { ...state, status: "ready", userdir, boot_ms, log: logPath(dir) }
  await writeState(dir, state)
  await log.line(`hold: ready in ${boot_ms}ms`)

  let idleTimer: ReturnType<typeof setTimeout> | undefined
  const clearIdle = () => {
    if (idleTimer !== undefined) {
      clearTimeout(idleTimer)
      idleTimer = undefined
    }
  }
  const armIdle = () => {
    clearIdle()
    idleTimer = setTimeout(() => {
      void (async () => {
        await log.line(`hold: idle for ${config.idleTimeoutMs}ms, reaping the session`)
        driver.kill()
        await end("idle_timeout")
        shutdown(server, dir)
        Deno.exit(0)
      })()
    }, config.idleTimeoutMs)
    idleTimer.unref?.()
  }
  armIdle()

  for await (const conn of server) {
    // A request under way is activity, not idleness: the reaper waits for the reply.
    clearIdle()
    let stopRequested = false
    try {
      const served = await serve(conn, driver, config, log, end)
      stopRequested = served.stop
      await conn.writable.getWriter().write(
        new TextEncoder().encode(JSON.stringify(served.reply) + "\n"),
      )
    } catch (e) {
      await log.line(`hold: connection failed: ${msg(e)}`)
    } finally {
      try {
        conn.close()
      } catch { /* the stream already took the socket with it */ }
    }
    // A fatal step (timeout, dead game) ends the session; so does a stop. Either way the holder
    // follows the game out, leaving no listener behind a session the state file calls ended.
    if (stopRequested || state.status === "ended") break
    armIdle()
  }
  clearIdle()
  shutdown(server, dir)
  return 0
}

function shutdown(server: Deno.Listener, dir: string): void {
  server.close()
  const stale = IS_WINDOWS ? portPath(dir) : socketPath(dir)
  Deno.remove(stale).catch(() => {})
}
/** The pins `start` was given, applied before anyone can observe the world. */
async function pin(driver: Driver, state: SessionState): Promise<void> {
  if (state.seed !== undefined) {
    const seeded = await driver.send({ cmd: "seed", seed: state.seed })
    if (seeded.status !== "ok") {
      throw new Error(`the game refused the seed: ${JSON.stringify(seeded)}`)
    }
  }
  if (state.date !== undefined || state.time !== undefined) {
    const pinned = await driver.send({
      cmd: "set_time",
      ...(state.date === undefined ? {} : { date: state.date }),
      ...(state.time === undefined ? {} : { time: state.time }),
    })
    if (pinned.status !== "ok") {
      throw new Error(`the game refused the clock pin: ${JSON.stringify(pinned)}`)
    }
  }
}

/** One connection, one request, one reply; `stop` asks the holder to shut down afterwards. */
async function serve(
  conn: Deno.Conn,
  driver: Driver,
  config: Config,
  log: Log,
  end: (ended: string, extra?: Partial<SessionState>) => Promise<void>,
): Promise<{ reply: HolderReply; stop: boolean }> {
  const line = await firstLine(conn.readable)
  if (line === undefined) {
    return { reply: { ok: false, error: "the connection closed without a request" }, stop: false }
  }
  let request: { op?: unknown; request?: DriverRequest }
  try {
    request = JSON.parse(line)
  } catch {
    return { reply: { ok: false, error: "the request line is not JSON" }, stop: false }
  }
  if (request.op === "stop") return { reply: await stop(driver, log, end), stop: true }
  if (request.op === "step" && request.request !== undefined) {
    return { reply: await step(request.request, driver, config, log, end), stop: false }
  }
  return { reply: { ok: false, error: `unknown op ${JSON.stringify(request.op)}` }, stop: false }
}

async function step(
  request: DriverRequest,
  driver: Driver,
  config: Config,
  log: Log,
  end: (ended: string, extra?: Partial<SessionState>) => Promise<void>,
): Promise<HolderReply> {
  const budget = stepTimeout(config, request)
  try {
    return { ok: true, result: await driver.send(request, budget) }
  } catch (e) {
    const timedOut = e instanceof DriverTimeout
    await log.line(`hold: step cmd=${request.cmd} failed: ${msg(e)}`)
    driver.kill()
    const exit_code = await withTimeout(driver.exited, 5_000)
    await end(timedOut ? "timeout" : "gone", exit_code === undefined ? {} : { exit_code })
    return {
      ok: false,
      error: timedOut
        ? `the game did not answer cmd=${request.cmd} within ${budget}ms; its process group was killed`
        : `the game died during cmd=${request.cmd}: ${msg(e)}`,
    }
  }
}

async function stop(
  driver: Driver,
  log: Log,
  end: (ended: string, extra?: Partial<SessionState>) => Promise<void>,
): Promise<HolderReply> {
  let ended = "stopped"
  let exit_code: number | undefined
  try {
    const quit = await withTimeout(driver.send({ cmd: "quit" }, QUIT_TIMEOUT_MS), QUIT_TIMEOUT_MS)
    if (quit === undefined) throw new Error("the game did not answer quit")
    exit_code = await withTimeout(driver.exited, QUIT_TIMEOUT_MS)
  } catch (e) {
    ended = "quit_refused"
    await log.line(`hold: graceful quit failed: ${msg(e)}`)
    driver.kill()
    exit_code = await withTimeout(driver.exited, 5_000)
  }
  await end(ended, exit_code === undefined ? {} : { exit_code })
  const result: Record<string, unknown> = { session_ended: true, ended }
  if (exit_code !== undefined) result.exit_code = exit_code
  return { ok: true, result }
}

async function firstLine(stream: ReadableStream<Uint8Array>): Promise<string | undefined> {
  for await (const line of readLines(stream)) {
    if (line.trim()) return line
  }
  return undefined
}

/** The promise's value, or undefined when `ms` pass first; rejects with the promise's error. */
async function withTimeout<T>(promise: Promise<T>, ms: number): Promise<T | undefined> {
  const { promise: settled, resolve, reject } = Promise.withResolvers<T | undefined>()
  const timer = setTimeout(() => resolve(undefined), ms)
  promise.then(resolve, reject)
  try {
    return await settled
  } finally {
    clearTimeout(timer)
  }
}

async function listen(dir: string): Promise<Deno.Listener> {
  if (!IS_WINDOWS) {
    await Deno.remove(socketPath(dir)).catch(() => {})
    return Deno.listen({ transport: "unix", path: socketPath(dir) })
  }
  const listener = Deno.listen({ transport: "tcp", hostname: "127.0.0.1", port: 0 })
  const { port } = listener.addr as Deno.NetAddr
  await Deno.writeTextFile(portPath(dir), String(port))
  return listener
}

function msg(e: unknown): string {
  return e instanceof Error ? e.message : String(e)
}
