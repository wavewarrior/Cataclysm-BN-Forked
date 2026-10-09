/// Poll the tracker and run a driver pass whenever a `factory:ready` ticket can be picked up.
/// Meant to sit in a herdr pane (`deno task factory watch`). Every effect is injected, so the loop
/// is testable without GitHub, herdr or a compiler.

export type WatchOptions = {
  intervalSec: number
  /// Run a single tick, then return.
  once?: boolean
  /// Whether a `factory:ready` ticket is pickable right now (its dependencies are closed).
  hasWork: () => Promise<boolean>
  /// One driver pass; resolves when every ticket it started has finished.
  runPass: () => Promise<void>
  sleep: (ms: number) => Promise<void>
  log: (line: string) => void
  /// Aborting stops the loop before the next pass. A pass already running is not interrupted.
  signal?: AbortSignal
}

/// Longest wait after repeated failures, in seconds.
export const MAX_BACKOFF_SEC = 600

/// Seconds to wait after `failures` consecutive failures: the interval, doubled per failure, capped.
/// An interval above the cap is kept as is.
export function nextDelaySec(intervalSec: number, failures: number): number {
  return Math.min(intervalSec * 2 ** failures, Math.max(MAX_BACKOFF_SEC, intervalSec))
}

/// How long a claimed ticket stays ignored: `gh issue list` lags a label edit by seconds. Ten
/// minutes is generous, and still lets a human re-release a blocked ticket without a restart.
export const CLAIM_LAG_MS = 10 * 60_000

/// Whether `issue` was claimed recently enough that a listing may still show it as ready.
export function isRecentlyClaimed(
  claims: Map<number, number>,
  issue: number,
  now: number,
): boolean {
  const at = claims.get(issue)
  return at !== undefined && now - at < CLAIM_LAG_MS
}

/// Thrown when every implementer lane is held by another driver. The watcher waits instead of backing off.
export class LanesBusyError extends Error {}

/// Returns the number of passes started.
export async function watch(opts: WatchOptions): Promise<number> {
  let passes = 0
  let failures = 0
  let lastState = ""
  /// Idle lines repeat every tick, so print only when the state changes.
  const report = (state: string, line: string): void => {
    if (state !== lastState) opts.log(line)
    lastState = state
  }
  while (!opts.signal?.aborted) {
    let wait = opts.intervalSec
    try {
      if (await opts.hasWork()) {
        passes++
        await opts.runPass()
        failures = 0
        report("done", "driver pass finished")
      } else {
        failures = 0
        report("idle", `idle: no pickable factory:ready ticket, polling every ${opts.intervalSec}s`)
      }
    } catch (e) {
      if (e instanceof LanesBusyError) {
        failures = 0
        report("busy", `${e.message}; retrying every ${opts.intervalSec}s`)
      } else {
        failures++
        wait = nextDelaySec(opts.intervalSec, failures)
        opts.log(`error (${failures} in a row): ${(e as Error).message}; retry in ${wait}s`)
      }
    }
    if (opts.once || opts.signal?.aborted) break
    await opts.sleep(wait * 1000)
  }
  return passes
}
