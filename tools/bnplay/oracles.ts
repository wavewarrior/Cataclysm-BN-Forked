/**
 * Oracles: predicates over the observation stream, evaluated supervisor-side as each response
 * arrives, plus the two built-in checks that read only the stream (the turn counter and the
 * commands the Trial expects to work). The other two built-ins (the process stayed alive, no new
 * game-log errors) need the Episode's end and the log, and live in report.ts.
 *
 * An oracle's verdict is its first failing observation: `index` is the request id in the
 * transcript, so a failure can be found there directly.
 */
import type { DriverRequest, DriverResponse } from "./client.ts"
import type { OracleOperator, OracleSpec, Trial } from "./trial.ts"

/** One answered request: its id in the transcript, what was asked and what came back. */
export type Observation = { index: number; request: DriverRequest; response: DriverResponse }

export type FirstFail = {
  /** The request id in the transcript. */
  index: number
  /** The game's turn counter at the failing observation, when it had one. */
  turn?: number
  /** Game turns since the Episode's first state. */
  elapsed?: number
  why: string
}

export type OracleResult = {
  name: string
  /** `pass`; `fail` and `warn` (a failed `warn` oracle); `inconclusive`: never got decided. */
  result: "pass" | "fail" | "warn" | "inconclusive" | "skipped"
  first_fail?: FirstFail
  /** Why a check was skipped or what else the reader should know. */
  note?: string
  /** True when the oracle reached a verdict that can decide the Episode: a fail, or a by-turn pass. */
  decisive: boolean
}

/** How far the Episode got when the results are read. */
export type Progress = "complete" | "wall_clock" | "running"

const OUTCOMES_THAT_BREAK_EXPECTED_COMMANDS = ["unsupported", "no_effect"]

/** Whether `actual` satisfies `operator` against `expected`. A mismatch of types does not hold. */
function holds(
  operator: OracleOperator,
  actual: unknown,
  expected: string | number | boolean,
): boolean {
  switch (operator) {
    case "eq":
      return actual === expected
    case "ne":
      return actual !== expected
    case "lt":
      return typeof actual === "number" && actual < (expected as number)
    case "le":
      return typeof actual === "number" && actual <= (expected as number)
    case "gt":
      return typeof actual === "number" && actual > (expected as number)
    case "ge":
      return typeof actual === "number" && actual >= (expected as number)
    case "contains": {
      if (typeof actual === "string") return actual.includes(String(expected))
      if (!Array.isArray(actual)) return false
      return actual.some((item) =>
        typeof item === "string" && typeof expected === "string"
          ? item.includes(expected)
          : item === expected
      )
    }
  }
}

function describe(value: unknown): string {
  const text = JSON.stringify(value)
  return text.length > 60 ? text.slice(0, 57) + "..." : text
}

/**
 * The value a Trial oracle's `field` names in a response: a top-level key, or a path into an
 * object member with dots (`scene.status`). `found` is false when the response has no such field.
 */
function lookup(response: DriverResponse, field: string): { found: boolean; value?: unknown } {
  if (field in response) return { found: true, value: response[field] }
  let value: unknown = response
  for (const part of field.split(".")) {
    if (typeof value !== "object" || value === null || Array.isArray(value) || !(part in value)) {
      return { found: false }
    }
    value = (value as Record<string, unknown>)[part]
  }
  return { found: true, value }
}

type Tracked = { spec: OracleSpec; failed?: FirstFail; satisfied: boolean }

export class OracleRun {
  readonly #commands: string[]
  readonly #tracked: Tracked[]
  #firstTurn?: number
  #lastTurn?: number
  /** The first time-passing observation may complete a partial turn without moving the counter. */
  #timePassedSeen = false
  #turnCounter?: FirstFail
  #commandsBroken?: FirstFail
  #lastIndex = 0

  constructor(trial: Pick<Trial, "oracles" | "expectedCommands">) {
    this.#commands = trial.expectedCommands
    this.#tracked = trial.oracles.map((spec) => ({ spec, satisfied: false }))
  }

  /** The game turn of the Episode's first state; undefined until one was observed. */
  get firstTurn(): number | undefined {
    return this.#firstTurn
  }

  /** The latest turn counter seen. */
  get lastTurn(): number | undefined {
    return this.#lastTurn
  }

  observe({ index, request, response }: Observation): void {
    this.#lastIndex = index
    if (response.status !== "ok") return
    const turn = typeof response.turn === "number" ? response.turn : undefined
    if (turn !== undefined) this.#firstTurn ??= turn
    const at = {
      index,
      ...(turn === undefined ? {} : { turn, elapsed: turn - (this.#firstTurn ?? turn) }),
    }
    if (turn !== undefined) this.#checkTurnCounter(at, request, response, turn)
    this.#checkCommands(at, request, response)
    for (const tracked of this.#tracked) this.#checkOracle(tracked, at, response)
  }

  #checkTurnCounter(
    at: Omit<FirstFail, "why">,
    request: DriverRequest,
    response: DriverResponse,
    turn: number,
  ): void {
    const previous = this.#lastTurn
    this.#lastTurn = turn
    const timePassed = response.time_passed === true
    const after = this.#timePassedSeen
    if (timePassed) this.#timePassedSeen = true
    if (previous === undefined || this.#turnCounter) return
    let why: string | undefined
    if (turn < previous) {
      why = `the turn counter went backwards, ${previous} to ${turn}`
    } else if (turn > previous && !timePassed) {
      why = `the turn counter moved ${previous} to ${turn} but time_passed is false`
    } else if (
      after && timePassed && turn === previous && request.cmd === "wait" &&
      response.outcome === "completed"
    ) {
      why = `a completed wait reported time_passed but the turn counter stayed at ${turn}`
    }
    if (why) this.#turnCounter = { ...at, why }
  }

  #checkCommands(at: Omit<FirstFail, "why">, request: DriverRequest, response: DriverResponse) {
    if (this.#commandsBroken) return
    const names = [request.cmd, `${request.cmd}:${request.name}`]
    if (!names.some((name) => this.#commands.includes(name))) return
    const outcome = String(response.outcome)
    if (!OUTCOMES_THAT_BREAK_EXPECTED_COMMANDS.includes(outcome)) return
    const reason = typeof response.reason === "string" ? ` (${response.reason})` : ""
    const what = request.cmd === "action" ? `action ${request.name}` : request.cmd
    this.#commandsBroken = {
      ...at,
      why: `${what} is expected to work but its outcome is ${outcome}${reason}`,
    }
  }

  #checkOracle(tracked: Tracked, at: Omit<FirstFail, "why">, response: DriverResponse): void {
    const { spec } = tracked
    if (tracked.failed || tracked.satisfied) return
    const { found, value: actual } = lookup(response, spec.field)
    if (!found) return
    const holding = holds(spec.operator, actual, spec.value)
    const expectation = `${spec.field} ${spec.operator} ${describe(spec.value)}`
    if (spec.mode === "always") {
      if (!holding) {
        tracked.failed = { ...at, why: `${spec.field}=${describe(actual)}, not ${expectation}` }
      }
    } else if (spec.mode === "never") {
      if (holding) {
        tracked.failed = { ...at, why: `${spec.field}=${describe(actual)} (${expectation})` }
      }
    } else {
      const elapsed = at.elapsed
      if (holding && (elapsed === undefined || elapsed <= spec.mode.byTurn)) {
        tracked.satisfied = true
      } else if (elapsed !== undefined && elapsed >= spec.mode.byTurn) {
        tracked.failed = {
          ...at,
          why: `${expectation} did not hold by turn ${spec.mode.byTurn}; ` +
            `${spec.field}=${describe(actual)} at turn ${elapsed}`,
        }
      }
    }
  }

  /** The verdict of every check this class owns, in report order. */
  results(progress: Progress): OracleResult[] {
    const builtIn = (name: string, failed: FirstFail | undefined): OracleResult =>
      failed
        ? { name, result: "fail", first_fail: failed, decisive: true }
        : { name, result: "pass", decisive: false }
    return [
      builtIn("turn_counter", this.#turnCounter),
      builtIn("commands", this.#commandsBroken),
      ...this.#tracked.map((tracked) => this.#result(tracked, progress)),
    ]
  }

  #result({ spec, failed, satisfied }: Tracked, progress: Progress): OracleResult {
    const failure = spec.severity === "fail" ? "fail" : "warn"
    if (failed) {
      return { name: spec.name, result: failure, first_fail: failed, decisive: failure === "fail" }
    }
    if (typeof spec.mode === "string") return { name: spec.name, result: "pass", decisive: false }
    if (satisfied) {
      return { name: spec.name, result: "pass", decisive: spec.severity === "fail" }
    }
    if (progress === "complete") {
      // The Episode is over and the condition never held by its turn: that is a failure.
      const first: FirstFail = {
        index: this.#lastIndex,
        ...(this.#lastTurn === undefined || this.#firstTurn === undefined
          ? {}
          : { turn: this.#lastTurn, elapsed: this.#lastTurn - this.#firstTurn }),
        why: `${spec.field} ${spec.operator} ${describe(spec.value)} never held; ` +
          `the Episode ended before turn ${spec.mode.byTurn} was decided`,
      }
      return { name: spec.name, result: failure, first_fail: first, decisive: failure === "fail" }
    }
    return { name: spec.name, result: "inconclusive", decisive: false }
  }
}
