/**
 * The resident supervisor daemon. It keeps games running between shell calls (a boot takes 7 to 10
 * seconds) and serves the operations the CLI and MCP front ends share: start, step, stop, shutdown.
 */
import { join } from "@std/path"
import { type Config, loadConfig, socketPath } from "./config.ts"
import { Episode, HarnessError } from "./episode.ts"
import { type DaemonReply, type DaemonRequest, daemonState, readLines } from "./ipc.ts"
import { parseTrial, TrialError } from "./trial.ts"

class Daemon {
  readonly #config: Config
  readonly #sessions = new Map<string, Episode>()
  readonly #log: (message: string) => void

  constructor(config: Config) {
    this.#config = config
    const logPath = join(config.home, "daemon.log")
    this.#log = (message) => {
      Deno.writeTextFileSync(logPath, `${new Date().toISOString()} ${message}\n`, { append: true })
    }
  }

  async serve(): Promise<void> {
    const path = socketPath(this.#config.home)
    await Deno.mkdir(this.#config.home, { recursive: true })
    // Another daemon already owns this home, even a slow one: never remove a live socket.
    if ((await daemonState(this.#config.home)) !== "absent") return
    await Deno.remove(path).catch(() => undefined) // a stale socket from a dead daemon
    const listener = Deno.listen({ transport: "unix", path })
    this.#log(`listening on ${path} pid ${Deno.pid}`)
    // A resident daemon holds games for other sessions; a stray error is logged, not fatal.
    globalThis.addEventListener("unhandledrejection", (event) => {
      event.preventDefault()
      this.#log(`unhandled rejection: ${event.reason?.stack ?? event.reason}`)
    })
    globalThis.addEventListener("error", (event) => {
      event.preventDefault()
      this.#log(`uncaught error: ${event.error?.stack ?? event.message}`)
    })

    const shutdown = async () => {
      await this.#endAll()
      listener.close()
      await Deno.remove(path).catch(() => undefined)
      this.#log("shut down")
      Deno.exit(0)
    }
    for (const signal of ["SIGTERM", "SIGINT", "SIGHUP"] as const) {
      Deno.addSignalListener(signal, () => void shutdown())
    }

    for (;;) {
      let conn: Deno.Conn
      try {
        conn = await listener.accept()
      } catch (e) {
        if (e instanceof Deno.errors.BadResource) return // the listener was closed
        // macOS fails accept() with EINVAL when a peer (a liveness probe) hung up before it was
        // accepted; that must not take the daemon, and the games it holds, down.
        this.#log(`accept failed: ${(e as Error).message}`)
        continue
      }
      void this.#serveConnection(conn, shutdown)
    }
  }

  async #serveConnection(conn: Deno.Conn, shutdown: () => Promise<void>): Promise<void> {
    const writer = conn.writable.getWriter()
    try {
      for await (const line of readLines(conn.readable)) {
        let request: DaemonRequest
        try {
          request = JSON.parse(line)
        } catch {
          await this.#reply(writer, { ok: false, error: "the daemon request is not JSON" })
          continue
        }
        const reply = await this.#handle(request)
        await this.#reply(writer, reply)
        if (request.op === "shutdown") await shutdown()
      }
    } catch (e) {
      this.#log(`connection failed: ${(e as Error).message}`)
    } finally {
      try {
        writer.releaseLock()
        conn.close()
      } catch { /* the peer is gone */ }
    }
  }

  async #reply(writer: WritableStreamDefaultWriter<Uint8Array>, reply: DaemonReply): Promise<void> {
    await writer.write(new TextEncoder().encode(JSON.stringify(reply) + "\n"))
  }

  async #handle(request: DaemonRequest): Promise<DaemonReply> {
    try {
      switch (request.op) {
        case "start":
          return { ok: true, result: await this.#start(request.trial) }
        case "step":
          return { ok: true, result: await this.#session(request.session).step(request.request) }
        case "stop":
          return { ok: true, result: await this.#session(request.session).stop() }
        case "ping":
          return { ok: true, result: {} }
        case "shutdown":
          await this.#endAll()
          return { ok: true, result: {} }
        default:
          return { ok: false, error: `unknown daemon operation ${JSON.stringify(request)}` }
      }
    } catch (e) {
      if (e instanceof HarnessError || e instanceof TrialError) {
        return { ok: false, error: e.message }
      }
      this.#log(`internal error: ${(e as Error).stack ?? e}`)
      return { ok: false, error: `internal error: ${(e as Error).message}` }
    }
  }

  #session(id: string): Episode {
    const episode = this.#sessions.get(id)
    if (!episode) throw new HarnessError(`no session ${id}`)
    return episode
  }

  async #start(trialPath: string): Promise<object> {
    let text: string
    try {
      text = await Deno.readTextFile(trialPath)
    } catch {
      throw new HarnessError(`cannot read the Trial file ${trialPath}`)
    }
    const trial = parseTrial(text)
    const fixture = join(this.#config.fixtures, trial.fixture)
    if (!(await Deno.stat(fixture).then((s) => s.isDirectory, () => false))) {
      throw new HarnessError(`fixture ${trial.fixture} not found in ${this.#config.fixtures}`)
    }
    if (!(await Deno.stat(this.#config.binary).then((s) => s.isFile, () => false))) {
      throw new HarnessError(`game binary not found: ${this.#config.binary}`)
    }
    const id = crypto.randomUUID().slice(0, 8)
    const episode = await Episode.start(this.#config, trial, id)
    this.#sessions.set(id, episode)
    return { session: id, transcript: episode.transcriptPath, boot_ms: episode.bootMs }
  }

  async #endAll(): Promise<void> {
    await Promise.all([...this.#sessions.values()].map((e) => e.kill("daemon_shutdown")))
  }
}

/** Runs the daemon in this process until it is told to shut down. */
export function runDaemon(): Promise<void> {
  return new Daemon(loadConfig()).serve()
}
