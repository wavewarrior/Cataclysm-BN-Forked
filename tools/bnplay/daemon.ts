/**
 * The resident supervisor daemon. It keeps games running between shell calls (a boot takes 7 to 10
 * seconds) and serves the operations the CLI and MCP front ends share: start, step, stop, the
 * fixture library operations, the doctor preflight and shutdown.
 */
import { join } from "@std/path"
import { captureBaseline } from "./baseline.ts"
import { type Config, loadConfig, socketPath } from "./config.ts"
import { runDoctor } from "./doctor.ts"
import { Episode, HarnessError } from "./episode.ts"
import { addFixture, fixtureStatus, listFixtures } from "./fixtures.ts"
import { type DaemonReply, type DaemonRequest, daemonState, readLines } from "./ipc.ts"
import { buildReport } from "./report.ts"
import { parseTrial, type Trial, TrialError } from "./trial.ts"

class Daemon {
  readonly #config: Config
  readonly #sessions = new Map<string, Episode>()
  /** Starts that passed the cap check and have not yet become sessions. */
  #starting = 0
  /** Of those, the starts of a windowed Trial: only one window may be open at a time. */
  #startingWindowed = 0
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
      if (
        e instanceof Deno.errors.BrokenPipe || e instanceof Deno.errors.ConnectionReset ||
        e instanceof Deno.errors.NotConnected
      ) {
        this.#log(`client hung up: ${e.message}`) // a CLI that timed out and left; expected
      } else {
        this.#log(`connection failed: ${(e as Error).message}`)
      }
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
        case "stop": {
          const episode = this.#session(request.session)
          await episode.stop()
          return { ok: true, result: await buildReport(this.#config, episode.reportInput()) }
        }
        case "report": {
          const episode = this.#session(request.session)
          await episode.closed() // an Episode the watchdog killed may still be reaping its game
          return { ok: true, result: await buildReport(this.#config, episode.reportInput()) }
        }
        case "fixture_add": {
          const added = await addFixture(this.#config.fixtures, request.source, request.name)
          return {
            ok: true,
            result: { ...added, ...(await fixtureStatus(this.#config.fixtures, added.fixture)) },
          }
        }
        case "fixture_list":
          return {
            ok: true,
            result: {
              dir: this.#config.fixtures,
              fixtures: await listFixtures(this.#config.fixtures),
            },
          }
        case "fixture_baseline":
          return { ok: true, result: await this.#baseline(request.name) }
        case "doctor":
          return {
            ok: true,
            result: await this.#doctor(request.fixture, request.self_check, request.trial),
          }
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
    this.#refuseBeyondCap()
    // The slot is taken before the first await: concurrent starts must not all see a free slot.
    this.#starting++
    try {
      return await this.#startEpisode(trialPath)
    } finally {
      this.#starting--
    }
  }

  #refuseBeyondCap(): void {
    const live = [...this.#sessions.values()].filter((e) => !e.ended).length + this.#starting
    if (live >= this.#config.maxSessions) {
      throw new HarnessError(
        `session limit reached: ${live} of ${this.#config.maxSessions} sessions are running; ` +
          `stop one (bnplay stop <session>) or raise BNPLAY_MAX_SESSIONS before starting another`,
      )
    }
  }

  async #startEpisode(trialPath: string): Promise<object> {
    let text: string
    try {
      text = await Deno.readTextFile(trialPath)
    } catch {
      throw new HarnessError(`cannot read the Trial file ${trialPath}`)
    }
    const trial = parseTrial(text)
    // Checked and taken before the next await, as the cap is: concurrent starts cannot both pass.
    if (trial.window) this.#refuseSecondWindow()
    if (trial.window) this.#startingWindowed++
    try {
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
    } finally {
      if (trial.window) this.#startingWindowed--
    }
  }

  /**
   * Two game windows at once would occlude each other and silently break the captures, so a
   * windowed Trial starts only while no other windowed session is live or booting. Windowless
   * sessions are not counted.
   */
  #refuseSecondWindow(): void {
    const live = [...this.#sessions.values()].find((e) => e.windowed && !e.ended)
    if (!live && this.#startingWindowed === 0) return
    const which = live ? `session ${live.id} is` : "another windowed session is still booting and"
    throw new HarnessError(
      `a windowed session is already running: ${which} using the one game window ` +
        `(only one windowed session runs at a time); stop it (bnplay stop ${
          live?.id ?? "<session>"
        }) ` +
        `or start a windowless Trial, which is not limited this way`,
    )
  }

  /**
   * Captures a fixture's baseline on an Episode of its own. The game it boots counts against the
   * session cap like any other, and a daemon shutdown ends it with the rest.
   */
  async #baseline(fixture: string): Promise<object> {
    let episode = undefined as Episode | undefined
    try {
      return await captureBaseline(this.#config, fixture, async (trial) => {
        this.#refuseBeyondCap()
        this.#starting++ // taken before the first await, as in #start
        try {
          episode = await Episode.start(this.#config, trial, crypto.randomUUID().slice(0, 8))
          this.#sessions.set(episode.id, episode)
          return episode
        } finally {
          this.#starting--
        }
      })
    } finally {
      // The capture ended its Episode; nobody can step it, so it is not kept as a session.
      if (episode) this.#sessions.delete(episode.id)
    }
  }

  /**
   * The doctor preflight. The Episodes of the optional self-check are booted one at a time, count
   * against the session cap like any other, and are ended before this returns.
   */
  async #doctor(
    fixture: string | undefined,
    selfCheck: boolean,
    trialPath: string | undefined,
  ): Promise<object> {
    let trial: Trial | undefined
    if (trialPath !== undefined) {
      try {
        trial = parseTrial(await Deno.readTextFile(trialPath))
      } catch (e) {
        if (e instanceof TrialError) throw e
        throw new HarnessError(`cannot read the Trial file ${trialPath}`)
      }
    }
    const booted: Episode[] = []
    try {
      return await runDoctor(this.#config, {
        fixture,
        trial,
        selfCheck,
        liveUserdirs: [...this.#sessions.values()]
          .filter((e) => !e.ended)
          .map((e) => join(this.#config.home, "episodes", e.id, "userdir")),
        boot: async (selfCheckTrial) => {
          this.#refuseBeyondCap()
          this.#starting++ // taken before the first await, as in #start
          try {
            const episode = await Episode.start(
              this.#config,
              selfCheckTrial,
              crypto.randomUUID().slice(0, 8),
            )
            booted.push(episode)
            this.#sessions.set(episode.id, episode)
            return episode
          } finally {
            this.#starting--
          }
        },
      })
    } finally {
      // The self-check ended its Episodes; nobody can step them, so they are not kept as sessions.
      for (const episode of booted) this.#sessions.delete(episode.id)
    }
  }

  async #endAll(): Promise<void> {
    await Promise.all([...this.#sessions.values()].map((e) => e.kill("daemon_shutdown")))
  }
}

/** Runs the daemon in this process until it is told to shut down. */
export function runDaemon(): Promise<void> {
  return new Daemon(loadConfig()).serve()
}
