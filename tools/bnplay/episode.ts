/**
 * One Episode: a game started from a private clone of a fixture, owned by the supervisor.
 *
 * The Episode is the isolation boundary. It has its own user directory and world (a copy-on-write
 * clone, so the source fixture is never touched), its own process group (killed whole on any hang)
 * and a wall-clock watchdog that runs outside the game.
 */
import { join } from "@std/path"
import {
  type Driver,
  type DriverRequest,
  type DriverResponse,
  DriverTimeout,
  spawnDriver,
} from "./client.ts"
import type { Config } from "./config.ts"
import { OracleRun } from "./oracles.ts"
import { RendererRun } from "./renderer.ts"
import type { ReportInput, RequestTiming } from "./report.ts"
import { Transcript } from "./transcript.ts"
import { CAPTURE_TAG, type Trial } from "./trial.ts"

/** Why an Episode ended. Everything except `stop`, `turn_limit` and `died` is a harness failure. */
export type EndReason =
  | "stop"
  | "turn_limit"
  | "died"
  | "wall_clock"
  | "hang"
  | "driver_exit"
  | "boot_failure"
  | "daemon_shutdown"
  | "idle_timeout"

/** A failure the agent should read, not a bug in the supervisor. */
export class HarnessError extends Error {
  constructor(message: string) {
    super(message)
    this.name = "HarnessError"
  }
}

export type EpisodeSummary = {
  session: string
  ended: EndReason
  exit_code: number
  transcript: string
}

/** The game-side exit code of an Episode that ended any way but `stop`, `turn_limit` or `died`. */
const HARNESS_ERROR_EXIT_CODE = 2

/** Time a graceful `quit` gets before the Episode is killed instead. */
const QUIT_TIMEOUT_MS = 10_000

export async function run(cmd: string, args: string[]): Promise<void> {
  const out = await new Deno.Command(cmd, { args, stdout: "null", stderr: "piped" }).output()
  if (!out.success) {
    throw new Error(`${cmd} failed: ${new TextDecoder().decode(out.stderr).trim()}`)
  }
}

export class Episode {
  readonly id: string
  readonly transcriptPath: string
  /** World name in the Episode's own user directory: unique, so no two Episodes share a world. */
  readonly world: string
  /** Set once, synchronously, when the Episode ends; also the signal that it must not be used. */
  ended?: EndReason
  bootMs = 0
  /** Wall-clock time, in ms since the epoch, at which the game answered its first ping ready. */
  readyAt = 0
  /** The game's debug.log; the game flushes it as it exits, so read it after the Episode ends. */
  readonly debugLogPath: string
  /** Where this Episode's captures go: `capture` writes under it, whatever the agent asked for. */
  readonly capturesPath: string

  readonly #config: Config
  readonly #trial: Trial
  readonly #dir: string
  readonly #userdir: string
  readonly #transcript: Transcript
  /** Undefined only while the fixture is still being cloned. */
  #driver?: Driver
  #watchdog?: Parameters<typeof clearTimeout>[0]
  #idleTimer?: Parameters<typeof clearTimeout>[0]
  /** Requests accepted and not yet answered; an Episode is only idle while this is zero. */
  #inFlight = 0
  #finishing?: Promise<void>
  #exitCode = 0
  #queue: Promise<unknown> = Promise.resolve()
  readonly #oracles: OracleRun
  readonly #renderer: RendererRun
  readonly #requests: RequestTiming[] = []
  /** The request being answered: requests of one Episode never overlap. */
  #current?: { request: DriverRequest; began: number; tag?: string }
  /** The tag of the capture being sent: the supervisor's, never the game's. */
  #nextTag?: string
  #failure?: ReportInput["failure"]
  /** True once a capture wrote a frame, so the report names the directory. */
  #captured = false
  /** When the quit request was sent, or the Episode was killed: the end of the log window. */
  #endedAt?: number

  private constructor(config: Config, trial: Trial, id: string) {
    this.#config = config
    this.#trial = trial
    this.id = id
    this.#oracles = new OracleRun(trial)
    this.#renderer = new RendererRun(trial)
    this.world = `${trial.fixture}-${id}`
    this.#dir = join(config.home, "episodes", id)
    this.#userdir = join(this.#dir, "userdir")
    this.transcriptPath = join(this.#dir, "transcript.jsonl")
    this.debugLogPath = join(this.#userdir, "config", "debug.log")
    this.capturesPath = join(this.#dir, "captures")
    this.#transcript = new Transcript(this.transcriptPath)
  }

  /**
   * Clones the fixture, starts the game and waits until it answers a ping.
   * Throws HarnessError when any of that fails; no process outlives a failed start.
   */
  static async start(config: Config, trial: Trial, id: string): Promise<Episode> {
    await Deno.mkdir(join(config.home, "episodes", id, "userdir", "save"), { recursive: true })
    const episode = new Episode(config, trial, id)
    episode.#transcript.add({
      event: "start",
      detail: { session: id, world: episode.world, trial, userdir: episode.#userdir },
    })
    try {
      await run("cp", [
        "-cR",
        join(config.fixtures, trial.fixture),
        join(episode.#userdir, "save", episode.world),
      ])
    } catch (e) {
      await episode.#finish("boot_failure")
      throw new HarnessError(`cloning fixture \`${trial.fixture}\` failed: ${(e as Error).message}`)
    }
    await episode.#boot()
    return episode
  }

  async #boot(): Promise<void> {
    const began = performance.now()
    this.#driver = spawnDriver({
      binary: this.#config.binary,
      userdir: this.#userdir,
      world: this.world,
      basepath: this.#config.basepath,
      scenesDir: this.#config.scenesDir,
      window: this.#trial.window,
      firstTimeoutMs: this.#config.bootTimeoutMs,
      requestTimeoutMs: this.#config.stepTimeoutMs,
      trace: (entry) => {
        this.#transcript.add(entry)
        this.#observe(entry)
      },
    })
    this.#watchdog = setTimeout(
      () => void this.#finish("wall_clock"),
      this.#trial.wallClockLimitS * 1000,
    )
    try {
      const res = await this.#driver!.send({ cmd: "ping" })
      this.readyAt = Date.now()
      if (res.status !== "ok" || res.ready !== true) {
        throw new Error(`the first ping was not answered ready: ${JSON.stringify(res)}`)
      }
      this.bootMs = Math.round(performance.now() - began)
      // The Trial's seed applies before turn 0, and the first state is the turn the Episode counts from.
      if (this.#trial.seed !== undefined) {
        const seeded = await this.#driver!.send({ cmd: "seed", seed: this.#trial.seed })
        if (seeded.status !== "ok") {
          throw new Error(`the game refused the Trial's seed: ${JSON.stringify(seeded)}`)
        }
      }
      if (this.#trial.attachView !== undefined) {
        const attached = await this.#driver!.send({
          cmd: "attach_view",
          radius: this.#trial.attachView,
        })
        if (attached.status !== "ok") {
          throw new Error(`the game refused the Trial's attach_view: ${JSON.stringify(attached)}`)
        }
      }
      // A Scene that fails is the Trial's result to judge (an oracle can read `scene.status`); only
      // a game that cannot run the request at all, such as an unknown Scene, ends the boot.
      if (this.#trial.scene !== undefined) {
        const ran = await this.#driver!.send({ cmd: "run_scene", name: this.#trial.scene })
        if (ran.status !== "ok") {
          throw new Error(`the game refused the Trial's scene: ${JSON.stringify(ran)}`)
        }
      }
      const state = await this.#driver!.send({ cmd: "state" })
      if (state.status !== "ok") {
        throw new Error(`the game refused the first state request: ${JSON.stringify(state)}`)
      }
    } catch (e) {
      const expired = this.ended === "wall_clock"
      await this.#finish("boot_failure")
      throw new HarnessError(
        expired
          ? `boot did not finish within the wall_clock limit of ${this.#trial.wallClockLimitS}s`
          : `boot failed: ${(e as Error).message}`,
      )
    }
    this.#armIdleTimer()
    this.#transcript.add({ event: "ready", detail: { boot_ms: this.bootMs } })
  }

  /** Sends one command and returns the driver's response, serialised with other requests. */
  step(request: DriverRequest): Promise<DriverResponse & { episode_ended?: EndReason }> {
    return this.#request(async () => {
      this.#assertLive()
      // A capture's `tag` names its state for the renderer oracles; the game never sees it.
      const { tag, ...sent } = request
      if (request.cmd === "capture" && tag !== undefined) {
        if (typeof tag !== "string" || !CAPTURE_TAG.test(tag)) {
          return {
            id: null,
            status: "error",
            error: "`tag` must be a capture tag: letters, digits, `_` and `-`",
          }
        }
        this.#nextTag = tag
      }
      let response: DriverResponse
      try {
        // A `capture` always writes into the Episode's own directory, never where the agent points.
        response = await this.#driver!.send(
          request.cmd === "capture" ? { ...sent, dir: this.capturesPath } : request,
        )
      } catch (e) {
        if (this.ended) this.#assertLive()
        if (e instanceof DriverTimeout) {
          await this.#finish("hang")
          throw new HarnessError(
            `the game hung: ${e.message}; the Episode was killed (ended: hang)`,
          )
        }
        await this.#finish("driver_exit")
        throw new HarnessError(
          `the game stopped answering: ${(e as Error).message} (ended: driver_exit)`,
        )
      }
      if (request.cmd === "quit" && response.status === "ok") await this.#finish("stop", true)
      else if (response.outcome === "died") {
        // The avatar is dead: nothing more can happen in this world, so the Episode ends here.
        await this.#quit("died")
        return { ...response, episode_ended: "died" }
      } else if (this.#turnLimitReached()) {
        await this.#quit("turn_limit")
        return { ...response, episode_ended: "turn_limit" }
      }
      return response
    })
  }

  /** True once the game turn counter has moved as far as the Trial's turn limit allows. */
  #turnLimitReached(): boolean {
    const { turnLimit } = this.#trial
    const first = this.#oracles.firstTurn
    const last = this.#oracles.lastTurn
    return turnLimit !== undefined && first !== undefined && last !== undefined &&
      last - first >= turnLimit
  }

  /** Asks the game to quit and reaps it; a game that will not quit is ended as a hang or a death. */
  async #quit(reason: "stop" | "turn_limit" | "died"): Promise<void> {
    try {
      await this.#driver!.send({ cmd: "quit" }, QUIT_TIMEOUT_MS)
      await this.#finish(reason, true)
    } catch (e) {
      await this.#finish(e instanceof DriverTimeout ? "hang" : "driver_exit")
    }
  }

  /** Asks the game to quit, then reaps the process group. Idempotent. */
  stop(): Promise<EpisodeSummary> {
    return this.#request(async () => {
      if (!this.ended) await this.#quit("stop")
      await this.#finishing
      return this.summary()
    })
  }

  /** Resolves once the Episode has ended and its game is reaped; at once if it is still running. */
  closed(): Promise<void> {
    return this.#finishing ?? Promise.resolve()
  }

  /** True for an Episode with a real game window (a renderer Trial). */
  get windowed(): boolean {
    return this.#trial.window !== undefined
  }

  /** Kills the Episode now, whatever it is doing. */
  kill(reason: EndReason): Promise<void> {
    return this.#finish(reason)
  }

  summary(): EpisodeSummary {
    return {
      session: this.id,
      ended: this.ended ?? "stop",
      exit_code: this.#exitCode,
      transcript: this.transcriptPath,
    }
  }

  /** Everything the report is made from. */
  reportInput(): ReportInput {
    return {
      session: this.id,
      trial: this.#trial,
      ended: this.ended,
      bootMs: this.bootMs,
      readyAt: this.readyAt,
      endedAt: this.#endedAt,
      transcript: this.transcriptPath,
      log: this.debugLogPath,
      oracles: this.#oracles,
      renderer: this.#renderer,
      requests: this.#requests,
      failure: this.#failure,
      captures: this.#captured ? this.capturesPath : undefined,
    }
  }

  /** Records every request and answer: when it was sent, how long it took, what the oracles see. */
  #observe(
    entry:
      | { request: DriverRequest & { id: number } }
      | { response: DriverResponse }
      | { failure: { id: number; message: string } },
  ): void {
    if ("request" in entry) {
      this.#requests.push({ index: entry.request.id, sentAt: Date.now() })
      this.#current = { request: entry.request, began: performance.now(), tag: this.#nextTag }
      this.#nextTag = undefined
      if (entry.request.cmd === "quit") this.#endedAt = Date.now()
    } else if ("response" in entry) {
      const { request, began, tag } = this.#current!
      const timing = this.#requests.at(-1)!
      timing.ms = performance.now() - began
      timing.answeredAt = Date.now()
      const index = timing.index
      if (typeof entry.response.capture === "object" && entry.response.capture !== null) {
        this.#captured = true
      }
      this.#oracles.observe({ index, request, response: entry.response })
      this.#renderer.observe({ index, tag, response: entry.response })
    } else {
      this.#failure = { index: entry.failure.id, why: entry.failure.message }
    }
  }

  #assertLive(): void {
    if (this.ended === "idle_timeout") {
      throw new HarnessError(
        `session ${this.id} was killed after ${this.#config.idleTimeoutMs / 1000}s without a ` +
          `request (ended: idle_timeout); its transcript is kept at ${this.transcriptPath}`,
      )
    }
    if (this.ended) {
      throw new HarnessError(`session ${this.id} ended: ${this.ended}`)
    }
  }

  /** Runs one client request in order with the others; the Episode is never idle while it runs. */
  #request<T>(body: () => Promise<T>): Promise<T> {
    this.#inFlight++
    clearTimeout(this.#idleTimer)
    const next = this.#queue.then(body, body)
    this.#queue = next.catch(() => undefined)
    return next.finally(() => {
      this.#inFlight--
      this.#armIdleTimer()
    })
  }

  /** The reaper: the game dies by process group if no request arrives within the idle timeout. */
  #armIdleTimer(): void {
    clearTimeout(this.#idleTimer)
    if (this.ended || this.#inFlight > 0) return
    this.#idleTimer = setTimeout(
      () => void this.#finish("idle_timeout"),
      this.#config.idleTimeoutMs,
    )
  }

  /**
   * Ends the Episode: the first caller decides the reason, later callers wait for the same end.
   * `graceful` waits for the game to exit on its own after a `quit` instead of killing it.
   */
  #finish(reason: EndReason, graceful = false): Promise<void> {
    if (!this.#finishing) {
      this.ended = reason
      this.#endedAt ??= Date.now()
      clearTimeout(this.#watchdog)
      clearTimeout(this.#idleTimer)
      this.#finishing = this.#reap(reason, graceful)
    }
    return this.#finishing
  }

  async #reap(reason: EndReason, graceful: boolean): Promise<void> {
    let driverExit = 0
    if (this.#driver) {
      if (!graceful) this.#driver.kill()
      await this.#driver.close()
      driverExit = await this.#driver.exited
    }
    const normal = reason === "stop" || reason === "turn_limit" || reason === "died"
    this.#exitCode = normal ? driverExit : HARNESS_ERROR_EXIT_CODE
    this.#transcript.add({ event: "end", detail: { reason, exit_code: this.#exitCode } })
    this.#transcript.close()
    // The clone is the heavy part; the transcript and logs stay.
    await Deno.remove(join(this.#userdir, "save"), { recursive: true }).catch(() => undefined)
  }
}
