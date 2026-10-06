/**
 * The compact report of an Episode: verdict, exit code, one line per oracle with the first failing
 * request, boot and latency numbers and where the transcript and the game log are. It stays well
 * under about 500 tokens for a typical run; the transcript is the repro.
 *
 * Exit codes: 0 pass, 1 an oracle failed, 2 harness error (the game failed to boot, hung or died,
 * or the supervisor ended the Episode), 3 inconclusive (the wall-clock limit ended the Episode
 * before any oracle reached a verdict that decides it).
 */
import type { Config } from "./config.ts"
import type { EndReason } from "./episode.ts"
import { fixtureStatus, readBaseline } from "./fixtures.ts"
import { linesInWindow, stampTime, withoutStamp } from "./gamelog.ts"
import type { FirstFail, OracleResult, OracleRun, Progress } from "./oracles.ts"
import type { Trial } from "./trial.ts"

/**
 * When a request was sent (wall clock, ms since the epoch) and answered, and how long the game took
 * to answer it (both unset while unanswered).
 */
export type RequestTiming = { index: number; sentAt: number; answeredAt?: number; ms?: number }

/** What an Episode knows about itself that the report is made from. */
export type ReportInput = {
  session: string
  trial: Trial
  ended?: EndReason
  bootMs: number
  /** Wall-clock ms since the epoch at which the game first answered ready. */
  readyAt: number
  /** When the quit request was sent, or the Episode was killed: the end of the log window. */
  endedAt?: number
  transcript: string
  log: string
  oracles: OracleRun
  requests: RequestTiming[]
  /** The request that got no answer, when the game hung or died. */
  failure?: FirstFail
}

export type ReportOracle = Omit<OracleResult, "decisive">

export type Report = {
  session: string
  verdict: "pass" | "fail" | "harness_error" | "inconclusive"
  exit_code: 0 | 1 | 2 | 3
  /** `running` while the Episode is still going. */
  ended: EndReason | "running"
  oracles: ReportOracle[]
  boot_ms: number
  requests: number
  latency_ms: { median: number; max: number }
  /** The game's turn counter at the Episode's first and last observation. */
  turns?: { first: number; last: number }
  transcript: string
  log: string
  notes?: string[]
}

/** Endings that are the harness failing, not the game under test. */
const HARNESS_ENDINGS: EndReason[] = [
  "hang",
  "driver_exit",
  "boot_failure",
  "daemon_shutdown",
  "idle_timeout",
]

/** The longest `why` kept in a report line. */
const WHY_LIMIT = 160

const ERROR_LINE = /^\d{2}:\d{2}:\d{2}\.\d+ ERROR\b/

function progressOf(ended: EndReason | undefined): Progress {
  if (ended === undefined) return "running"
  return ended === "stop" || ended === "turn_limit" || ended === "died" ? "complete" : "wall_clock"
}

function trimmed(result: ReportOracle): ReportOracle {
  if (!result.first_fail || result.first_fail.why.length <= WHY_LIMIT) return result
  const why = result.first_fail.why.slice(0, WHY_LIMIT - 3) + "..."
  return { ...result, first_fail: { ...result.first_fail, why } }
}

/**
 * The game stopped answering: it hung or died, or the watchdog killed it while a request was
 * still unanswered, which is a hang the watchdog had to kill, not a quiet game at its limit.
 */
function stoppedAnswering({ ended, failure }: ReportInput): boolean {
  return ended === "hang" || ended === "driver_exit" ||
    (ended === "wall_clock" && failure !== undefined)
}

function aliveCheck(input: ReportInput): OracleResult {
  if (!stoppedAnswering(input)) return { name: "alive", result: "pass", decisive: false }
  const first_fail = input.failure ?? { index: 0, why: `the game ended: ${input.ended}` }
  return { name: "alive", result: "fail", first_fail, decisive: true }
}

/** The first request that was answered at or after `at`: the one the line was logged during. */
function requestAt(requests: RequestTiming[], at: number): number {
  const during = requests.find((r) => r.answeredAt !== undefined && r.answeredAt >= at)
  return (during ?? requests.at(-1))?.index ?? 0
}

/**
 * No new error lines in the game log after readiness, compared against the fixture's baseline.
 * Only lines of level ERROR count, and a line the baseline already holds is ignored whatever its
 * stamp says; perf and other chatter is not looked at.
 */
async function logCheck(config: Config, input: ReportInput): Promise<OracleResult> {
  const skipped = (note: string): OracleResult => ({
    name: "game_log",
    result: "skipped",
    note,
    decisive: false,
  })
  if (input.ended === undefined) {
    return skipped("the game writes its log as it exits; read the report after the Episode ends")
  }
  const fixture = input.trial.fixture
  let baseline
  try {
    baseline = await readBaseline(config.fixtures, fixture)
  } catch (e) {
    return skipped((e as Error).message)
  }
  if (!baseline) {
    return skipped(`no baseline for fixture ${fixture}; run \`bnplay fixture baseline ${fixture}\``)
  }
  let log: string
  try {
    log = await Deno.readTextFile(input.log)
  } catch {
    return skipped(`the game wrote no debug.log at ${input.log}`)
  }
  const known = new Set(baseline.lines.filter((l) => ERROR_LINE.test(l)).map(withoutStamp))
  const toMs = input.endedAt ?? Date.now()
  const fresh = linesInWindow(log, { fromMs: input.readyAt, toMs })
    .filter((line) => ERROR_LINE.test(line) && !known.has(withoutStamp(line)))
  if (fresh.length === 0) return { name: "game_log", result: "pass", decisive: false }

  const at = stampTime(fresh[0], input.readyAt) ?? input.readyAt
  const first = withoutStamp(fresh[0])
  const more = fresh.length > 1 ? ` (and ${fresh.length - 1} more new error lines)` : ""
  const status = await fixtureStatus(config.fixtures, fixture)
  return {
    name: "game_log",
    result: "fail",
    first_fail: {
      index: requestAt(input.requests, at),
      why: `new ${first}${more}`,
    },
    ...(status.baseline === "stale"
      ? { note: `the baseline is stale (${status.message}); old noise may read as new` }
      : {}),
    decisive: true,
  }
}

function median(sorted: number[]): number {
  if (sorted.length === 0) return 0
  const mid = sorted.length >> 1
  return sorted.length % 2 ? sorted[mid] : (sorted[mid - 1] + sorted[mid]) / 2
}

const round = (n: number) => Math.round(n * 10) / 10

export async function buildReport(config: Config, input: ReportInput): Promise<Report> {
  const progress = progressOf(input.ended)
  const results = [
    aliveCheck(input),
    await logCheck(config, input),
    ...input.oracles.results(progress),
  ]

  let verdict: Report["verdict"]
  if (stoppedAnswering(input) || (input.ended && HARNESS_ENDINGS.includes(input.ended))) {
    verdict = "harness_error"
  } else if (results.some((r) => r.result === "fail")) verdict = "fail"
  else if (progress !== "complete" && !results.some((r) => r.decisive)) verdict = "inconclusive"
  else verdict = "pass"
  const exit_code = ({ pass: 0, fail: 1, harness_error: 2, inconclusive: 3 } as const)[verdict]

  // The first request is the boot ping, whose answer is the boot time, not a latency.
  const latencies = input.requests.slice(1).flatMap((r) => r.ms === undefined ? [] : [r.ms])
    .sort((a, b) => a - b)
  const first = input.oracles.firstTurn
  const last = input.oracles.lastTurn
  const notes = results.flatMap((r) => r.note ? [`${r.name}: ${r.note}`] : [])
  return {
    session: input.session,
    verdict,
    exit_code,
    ended: input.ended ?? "running",
    oracles: results.map(({ decisive: _, note: __, ...rest }) => trimmed(rest)),
    boot_ms: input.bootMs,
    requests: input.requests.length,
    latency_ms: { median: round(median(latencies)), max: round(latencies.at(-1) ?? 0) },
    ...(first === undefined || last === undefined ? {} : { turns: { first, last } }),
    transcript: input.transcript,
    log: input.log,
    ...(notes.length > 0 ? { notes } : {}),
  }
}
