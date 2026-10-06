/**
 * Line-JSON client for the game's `--driver-fd` mode.
 *
 * One request line in, one response line out. Every request carries a timeout: a hang is a
 * failure, and a timed-out driver is killed by process group so no game process lingers.
 */
import { dirname, fromFileUrl, join } from "@std/path"

export type DriverRequest = { cmd: string; [key: string]: unknown }

/** Handle accepted by `clearTimeout`. */
type TimerId = Parameters<typeof clearTimeout>[0]

export type DriverResponse = {
  id: number | null
  status: "ok" | "error"
  [key: string]: unknown
}

export class DriverTimeout extends Error {
  constructor(message: string) {
    super(message)
    this.name = "DriverTimeout"
  }
}

export type SpawnOptions = {
  /** Game binary (the tiles binary). */
  binary: string
  /** Private user directory holding `save/<world>`. */
  userdir: string
  world: string
  /** `--basepath` for the game (data and gfx live there). */
  basepath: string
  /** Timeout for the first request (the game boots a world before it answers). */
  firstTimeoutMs?: number
  /** Timeout for every later request. */
  requestTimeoutMs?: number
  /** Where the game's own log output goes; "inherit" shows it. */
  stderr?: "null" | "inherit"
  /**
   * Called with every request sent through `send`, and then either the response or the failure
   * (timeout, kill, dead game) that ended it, so every request is paired.
   */
  trace?: (
    entry:
      | { request: DriverRequest & { id: number } }
      | { response: DriverResponse }
      | { failure: { id: number; message: string } },
  ) => void
  /** Deny-list data file for the driver (`--driver-deny-list`); default is the repo's file. */
  denyList?: string
}

export type Driver = {
  /** Sends one request and resolves with its response. Rejects with DriverTimeout. */
  send(req: DriverRequest, timeoutMs?: number): Promise<DriverResponse>
  /**
   * Writes a raw line as-is, for malformed-request tests. `expectedId` is the id the response
   * must echo: null when the line carries no readable id.
   */
  sendRaw(line: string, expectedId: number | null, timeoutMs?: number): Promise<DriverResponse>
  /** Non-protocol lines that appeared on the protocol channel. Must stay empty. */
  readonly noise: string[]
  /** Process group id of the shim and everything it started. */
  readonly pgid: number
  /** Resolves with the exit code of the game once it ends. */
  readonly exited: Promise<number>
  /** Kills the whole process group now. Idempotent. */
  kill(): void
  /** Closes stdin, waits briefly, then always kills leftovers. */
  close(): Promise<void>
}

const SHIM = join(dirname(fromFileUrl(import.meta.url)), "fd_shim.py")

export function spawnDriver(opts: SpawnOptions): Driver {
  const firstTimeout = opts.firstTimeoutMs ?? 60_000
  const requestTimeout = opts.requestTimeoutMs ?? 10_000
  const child = new Deno.Command("/usr/bin/python3", {
    args: [
      SHIM,
      "--",
      opts.binary,
      "--userdir",
      opts.userdir + "/",
      "--world",
      opts.world,
      "--dont-debugmsg",
      "--basepath",
      opts.basepath,
      ...(opts.denyList ? ["--driver-deny-list", opts.denyList] : []),
    ],
    stdin: "piped",
    stdout: "piped",
    stderr: opts.stderr ?? "null",
    env: { SDL_VIDEODRIVER: "dummy", SDL_AUDIODRIVER: "dummy" },
  }).spawn()
  const pgid = child.pid

  const noise: string[] = []
  const pending = new Map<
    number | null,
    { resolve: (r: DriverResponse) => void; reject: (e: Error) => void; timer: TimerId }
  >()
  let closed = false
  let sentFirst = false

  const kill = () => {
    try {
      Deno.kill(-pgid, "SIGKILL")
    } catch { /* group already gone */ }
    try {
      child.kill("SIGKILL")
    } catch { /* already reaped */ }
  }

  const rejectAll = (reason: string) => {
    for (const p of pending.values()) {
      clearTimeout(p.timer)
      p.reject(new Error(reason))
    }
    pending.clear()
  }

  const reader = (async () => {
    const dec = new TextDecoder()
    let buf = ""
    for await (const chunk of child.stdout) {
      buf += dec.decode(chunk, { stream: true })
      let nl: number
      while ((nl = buf.indexOf("\n")) >= 0) {
        const line = buf.slice(0, nl).trim()
        buf = buf.slice(nl + 1)
        if (!line) continue
        let rec: DriverResponse | undefined
        try {
          const obj = JSON.parse(line)
          if (obj && typeof obj === "object" && "status" in obj && "id" in obj) rec = obj
        } catch { /* not JSON */ }
        const p = rec ? pending.get(rec.id) : undefined
        if (!rec || !p) {
          noise.push(line)
          continue
        }
        clearTimeout(p.timer)
        pending.delete(rec.id)
        p.resolve(rec)
      }
    }
    rejectAll("driver stream ended")
  })()

  const writer = child.stdin.getWriter()
  let nextId = 1

  const transmit = (
    key: number | null,
    line: string,
    timeoutMs: number | undefined,
    what: string,
  ): Promise<DriverResponse> => {
    if (closed) return Promise.reject(new Error("driver closed"))
    const limit = timeoutMs ?? (sentFirst ? requestTimeout : firstTimeout)
    sentFirst = true
    return new Promise<DriverResponse>((resolve, reject) => {
      const timer = setTimeout(() => {
        pending.delete(key)
        closed = true
        kill()
        reject(new DriverTimeout(`timeout after ${limit}ms waiting for ${what}`))
      }, limit)
      pending.set(key, { resolve, reject, timer })
      writer.write(new TextEncoder().encode(line + "\n")).catch((e) => {
        clearTimeout(timer)
        pending.delete(key)
        reject(e)
      })
    })
  }

  const exited = child.status.then((s) => s.code)

  return {
    async send(req, timeoutMs) {
      const id = nextId++
      const request = { ...req, id }
      opts.trace?.({ request })
      try {
        const response = await transmit(
          id,
          JSON.stringify(request),
          timeoutMs,
          `cmd=${req.cmd} id=${id}`,
        )
        opts.trace?.({ response })
        return response
      } catch (e) {
        opts.trace?.({ failure: { id, message: (e as Error).message } })
        throw e
      }
    },
    sendRaw(line, expectedId, timeoutMs) {
      return transmit(expectedId, line, timeoutMs, `raw line ${JSON.stringify(line)}`)
    },
    noise,
    pgid,
    exited,
    kill,
    async close() {
      if (!closed) {
        closed = true
        try {
          await writer.close()
        } catch { /* pipe may already be gone */ }
      }
      const killTimer = setTimeout(kill, 5_000)
      await exited
      clearTimeout(killTimer)
      kill() // reap any descendant that outlived the shim
      await reader
    },
  }
}
