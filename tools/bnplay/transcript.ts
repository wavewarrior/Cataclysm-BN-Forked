/**
 * Episode transcript: a JSONL file with one record per line, the repro of an Episode.
 *
 * Every record carries `ms`, the milliseconds since the Episode started. A record is either a
 * `request` the supervisor sent, the matching `response` (or `failure` when none came), or a
 * lifecycle `event`.
 */
import type { DriverRequest, DriverResponse } from "./client.ts"

export type TranscriptRecord = {
  ms: number
  request?: DriverRequest & { id: number }
  response?: DriverResponse
  /** A request that got no response: it timed out, or the game died or was killed first. */
  failure?: { id: number; message: string }
  /** Lifecycle event: `start`, `ready` or `end`. */
  event?: string
  /** Event details (the Trial, the end reason). */
  detail?: Record<string, unknown>
}

export class Transcript {
  readonly #file: Deno.FsFile
  readonly #started = performance.now()

  constructor(readonly path: string) {
    this.#file = Deno.openSync(path, { create: true, append: true })
  }

  add(record: Omit<TranscriptRecord, "ms">): void {
    const line = JSON.stringify({ ms: Math.round(performance.now() - this.#started), ...record })
    this.#file.writeSync(new TextEncoder().encode(line + "\n"))
  }

  close(): void {
    this.#file.close()
  }
}
