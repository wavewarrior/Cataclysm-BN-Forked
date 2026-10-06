/**
 * The game's `debug.log`, read by the supervisor.
 *
 * Every line starts `HH:MM:SS.mmm`, the game's local wall clock to the millisecond, with no date;
 * a line without that stamp (a backtrace, a folded repeat) continues the stamped line above it.
 *
 * The game writes the log through a buffered stream and flushes it only as the buffer fills or the
 * process exits, so how many bytes the file holds at some moment says nothing about what had been
 * logged by then. The stamps are the only reliable clock, which is why a stretch of the log is
 * picked by time, not by file offset.
 */

const DAY_MS = 86_400_000

/**
 * The game stamps lines from its own clock, rounded to the millisecond, and the supervisor learns
 * of a boundary (readiness, the quit request) only after a pipe round trip. A line this close to a
 * boundary may be on either side of it, so it is counted as outside.
 */
const CLOCK_SLACK_MS = 3

const STAMP = /^(\d{2}):(\d{2}):(\d{2})\.(\d+) /

/** Milliseconds since the epoch. */
export type LogWindow = { fromMs: number; toMs: number }

function msOfDay(epochMs: number): number {
  const d = new Date(epochMs)
  return ((d.getHours() * 60 + d.getMinutes()) * 60 + d.getSeconds()) * 1000 + d.getMilliseconds()
}

/**
 * The non-blank lines of `log` stamped inside `window`, verbatim, in order. The stamp carries no
 * date, so a line's age is its distance past the window's start on the clock face, modulo a day:
 * a window may span midnight, and anything logged before it or after it falls far outside.
 */
export function linesInWindow(log: string, window: LogWindow): string[] {
  const from = window.fromMs + CLOCK_SLACK_MS
  const span = window.toMs - CLOCK_SLACK_MS - from
  if (span < 0) return []
  const start = msOfDay(from)
  const picked: string[] = []
  let inside = false
  for (const line of log.split("\n")) {
    const stamp = STAMP.exec(line)
    if (stamp) {
      const t = ((Number(stamp[1]) * 60 + Number(stamp[2])) * 60 + Number(stamp[3])) * 1000 +
        Number(stamp[4])
      inside = (t - start + DAY_MS) % DAY_MS <= span
    }
    if (inside && line.trim() !== "") picked.push(line.trimEnd())
  }
  return picked
}

/**
 * The wall-clock time, in ms since the epoch, a stamped `line` was logged at: the first moment at
 * or after `notBefore` with that time of day (the stamp carries no date). Undefined for a line
 * without a stamp.
 */
export function stampTime(line: string, notBefore: number): number | undefined {
  const stamp = STAMP.exec(line)
  if (!stamp) return undefined
  const t = ((Number(stamp[1]) * 60 + Number(stamp[2])) * 60 + Number(stamp[3])) * 1000 +
    Number(stamp[4])
  return notBefore + (t - msOfDay(notBefore) + DAY_MS) % DAY_MS
}

/** A line without its stamp, so the same message logged at another time compares equal. */
export function withoutStamp(line: string): string {
  return line.replace(STAMP, "").trimEnd()
}
