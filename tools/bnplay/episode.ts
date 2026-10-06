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
import { Transcript } from "./transcript.ts"
import type { Trial } from "./trial.ts"

/** Why an Episode ended. Everything except `stop` is a harness failure. */
export type EndReason =
  | "stop"
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

/** The exit code of an Episode that ended any way but `stop`: a harness error, not a game result. */
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
  /** Set once, synchronously, when the Episode ends; also the signal that it must not be used. */
  ended?: EndReason
  bootMs = 0

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

  private constructor(config: Config, trial: Trial, id: string) {
    this.#config = config
    this.#trial = trial
    this.id = id
    this.#dir = join(config.home, "episodes", id)
    this.#userdir = join(this.#dir, "userdir")
    this.transcriptPath = join(this.#dir, "transcript.jsonl")
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
      detail: { session: id, trial, userdir: episode.#userdir },
    })
    try {
      await run("cp", [
        "-cR",
        join(config.fixtures, trial.fixture),
        join(episode.#userdir, "save", trial.fixture),
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
      world: this.#trial.fixture,
      basepath: this.#config.basepath,
      firstTimeoutMs: this.#config.bootTimeoutMs,
      requestTimeoutMs: this.#config.stepTimeoutMs,
      trace: (entry) => this.#transcript.add(entry),
    })
    this.#watchdog = setTimeout(
      () => void this.#finish("wall_clock"),
      this.#trial.wallClockLimitS * 1000,
    )
    try {
      const res = await this.#driver!.send({ cmd: "ping" })
      if (res.status !== "ok" || res.ready !== true) {
        throw new Error(`the first ping was not answered ready: ${JSON.stringify(res)}`)
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
    this.bootMs = Math.round(performance.now() - began)
    this.#armIdleTimer()
    this.#transcript.add({ event: "ready", detail: { boot_ms: this.bootMs } })
  }

  /** Sends one command and returns the driver's response, serialised with other requests. */
  step(request: DriverRequest): Promise<DriverResponse> {
    return this.#request(async () => {
      this.#assertLive()
      let response: DriverResponse
      try {
        response = await this.#driver!.send(request)
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
      return response
    })
  }

  /** Asks the game to quit, then reaps the process group. Idempotent. */
  stop(): Promise<EpisodeSummary> {
    return this.#request(async () => {
      if (!this.ended) {
        try {
          await this.#driver!.send({ cmd: "quit" }, QUIT_TIMEOUT_MS)
          await this.#finish("stop", true)
        } catch (e) {
          await this.#finish(e instanceof DriverTimeout ? "hang" : "driver_exit")
        }
      }
      await this.#finishing
      return this.summary()
    })
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
    this.#exitCode = reason === "stop" ? driverExit : HARNESS_ERROR_EXIT_CODE
    this.#transcript.add({ event: "end", detail: { reason, exit_code: this.#exitCode } })
    this.#transcript.close()
    // The clone is the heavy part; the transcript and logs stay.
    await Deno.remove(join(this.#userdir, "save"), { recursive: true }).catch(() => undefined)
  }
}
