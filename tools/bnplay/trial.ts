/**
 * Trial: the declarative definition of a playtest, a small TOML file.
 *
 * The supervisor parses it (the engine has no TOML dependency). Unknown fields are errors so a typo
 * never silently becomes a default.
 */
import { parse } from "@std/toml"

export class TrialError extends Error {
  constructor(message: string) {
    super(message)
    this.name = "TrialError"
  }
}

export type Trial = {
  /** Name of a fixture in the fixture library. */
  fixture: string
  /** RNG seed pinned for the Episode. */
  seed?: number
  /** In-game start date, `YYYY-MM-DD`. */
  startDate?: string
  /** In-game time of day, `HH:MM`. */
  timeOfDay?: string
  /** Wall-clock limit, enforced outside the game; the Episode is killed when it expires. */
  wallClockLimitS: number
  /** End the Episode after this many game turns. */
  turnLimit?: number
  /**
   * Attach a view of this radius (in tiles) to every response, so a dense Trial needs no extra
   * round trip a turn. Absent: no view. The game refuses a radius above its own limit, 10.
   */
  attachView?: number
  /**
   * Name of a Scene (a Lua script that builds a situation in the world) to run once the Episode
   * has booted and been seeded, before its first state. The response to `run_scene` is in the
   * transcript and in every oracle's reach as the `scene` field. Absent: none runs.
   */
  scene?: string
  /**
   * Commands the Trial expects to work: a response to one of them with outcome `unsupported` or
   * `no_effect` fails the Episode. An entry is a command (`move`) or an `action` by name
   * (`action:pause`).
   */
  expectedCommands: string[]
  /** Predicates evaluated over the observation stream, besides the built-in checks. */
  oracles: OracleSpec[]
}

export const ORACLE_OPERATORS = ["eq", "ne", "lt", "le", "gt", "ge", "contains"] as const
export type OracleOperator = typeof ORACLE_OPERATORS[number]

/**
 * `always`: the predicate holds in every observation that has the field. `never`: it holds in none.
 * `by-turn-N` (`{ byTurn: N }`): it holds in an observation seen no later than N game turns after
 * the Episode's first state.
 */
export type OracleMode = "always" | "never" | { byTurn: number }

export type OracleSpec = {
  name: string
  /** A flat key of the observation (`hp`, `turn`, `outcome`, `new_messages`, ...). */
  field: string
  operator: OracleOperator
  value: string | number | boolean
  mode: OracleMode
  /** A `warn` oracle is reported but never changes the exit code. */
  severity: "fail" | "warn"
}

/** Names the built-in checks report under; a Trial oracle may not reuse them. */
export const BUILT_IN_ORACLES = ["alive", "game_log", "turn_counter", "commands"]

/** Wall-clock limit applied when a Trial does not set one: an Episode never runs unbounded. */
export const DEFAULT_WALL_CLOCK_LIMIT_S = 300

const FIELDS = [
  "fixture",
  "seed",
  "start_date",
  "time_of_day",
  "wall_clock_limit_s",
  "turn_limit",
  "attach_view",
  "scene",
  "expected_commands",
  "oracle",
]

const ORACLE_FIELDS = ["name", "field", "operator", "value", "mode", "severity"]

/** What a fixture name may look like: it names a directory in the fixture library. */
export const FIXTURE_NAME = /^[A-Za-z0-9][A-Za-z0-9._-]*$/

/** What a Scene name may look like: it names a file in the Scenes directory. */
export const SCENE_NAME = /^[A-Za-z0-9_-]+$/

function fail(field: string, expectation: string): never {
  throw new TrialError(`Trial field \`${field}\` ${expectation}`)
}

function integer(table: Record<string, unknown>, field: string, min: number): number | undefined {
  const value = table[field]
  if (value === undefined) return undefined
  if (typeof value !== "number" && typeof value !== "bigint") {
    fail(field, "must be an integer")
  }
  const n = Number(value)
  if (!Number.isInteger(n) || n < min) fail(field, `must be an integer of at least ${min}`)
  return n
}

function text(
  table: Record<string, unknown>,
  field: string,
  shape: RegExp,
  expectation: string,
): string | undefined {
  const value = table[field]
  if (value === undefined) return undefined
  if (typeof value !== "string" || !shape.test(value)) fail(field, expectation)
  return value
}

function oracleSpecs(raw: unknown): OracleSpec[] {
  if (raw === undefined) return []
  if (!Array.isArray(raw)) fail("oracle", "must be a list of `[[oracle]]` tables")
  const specs: OracleSpec[] = []
  for (const [i, entry] of raw.entries()) {
    const where = `oracle ${i + 1}`
    if (typeof entry !== "object" || entry === null || Array.isArray(entry)) {
      fail("oracle", "must be a list of `[[oracle]]` tables")
    }
    const table = entry as Record<string, unknown>
    for (const key of Object.keys(table)) {
      if (!ORACLE_FIELDS.includes(key)) {
        fail(`${where} field \`${key}\``, `is unknown (known: ${ORACLE_FIELDS.join(", ")})`)
      }
    }
    if (typeof table.field !== "string" || table.field === "") {
      fail(`${where} field`, "is required and must name an observation key")
    }
    if (!ORACLE_OPERATORS.includes(table.operator as OracleOperator)) {
      fail(`${where} operator`, `is required and must be one of ${ORACLE_OPERATORS.join(", ")}`)
    }
    const operator = table.operator as OracleOperator
    const value = table.value
    if (typeof value !== "string" && typeof value !== "number" && typeof value !== "boolean") {
      fail(`${where} value`, "is required and must be a string, number or boolean")
    }
    if (["lt", "le", "gt", "ge"].includes(operator) && typeof value !== "number") {
      fail(`${where} value`, `must be a number for operator ${operator}`)
    }
    let mode: OracleMode = "always"
    if (table.mode !== undefined) {
      const byTurn = typeof table.mode === "string" ? /^by-turn-(\d+)$/.exec(table.mode) : null
      if (table.mode === "always" || table.mode === "never") mode = table.mode
      else if (byTurn && Number(byTurn[1]) >= 1) mode = { byTurn: Number(byTurn[1]) }
      else fail(`${where} mode`, 'must be "always", "never" or "by-turn-N" (N of at least 1)')
    }
    if (table.severity !== undefined && table.severity !== "fail" && table.severity !== "warn") {
      fail(`${where} severity`, 'must be "fail" or "warn"')
    }
    const modeText = typeof mode === "string" ? mode : `by-turn-${mode.byTurn}`
    const name = table.name === undefined
      ? `${modeText}: ${table.field} ${operator} ${value}`
      : table.name
    if (typeof name !== "string" || name === "") fail(`${where} name`, "must be a non-empty string")
    if (BUILT_IN_ORACLES.includes(name) || specs.some((s) => s.name === name)) {
      fail(`${where} name`, `\`${name}\` is already taken by another oracle`)
    }
    specs.push({
      name,
      field: table.field,
      operator,
      value,
      mode,
      severity: table.severity === "warn" ? "warn" : "fail",
    })
  }
  return specs
}

export function parseTrial(source: string): Trial {
  let table: Record<string, unknown>
  try {
    table = parse(source)
  } catch (e) {
    throw new TrialError(`Trial is not valid TOML: ${(e as Error).message}`)
  }
  for (const key of Object.keys(table)) {
    if (!FIELDS.includes(key)) {
      throw new TrialError(`unknown Trial field \`${key}\` (known fields: ${FIELDS.join(", ")})`)
    }
  }
  if (table.fixture === undefined) fail("fixture", "is required")
  const fixture = text(
    table,
    "fixture",
    FIXTURE_NAME,
    "must be a plain fixture name (letters, digits, `.`, `_`, `-`)",
  )!

  const wallClock = table.wall_clock_limit_s
  if (
    wallClock !== undefined &&
    (typeof wallClock !== "number" || !Number.isFinite(wallClock) || wallClock <= 0)
  ) {
    fail("wall_clock_limit_s", "must be a positive number of seconds")
  }

  const expected = table.expected_commands
  if (
    expected !== undefined &&
    (!Array.isArray(expected) || expected.some((c) => typeof c !== "string" || c === ""))
  ) {
    fail("expected_commands", 'must be a list of command names such as ["move", "action:pause"]')
  }

  return {
    fixture,
    seed: integer(table, "seed", 0),
    startDate: text(
      table,
      "start_date",
      /^\d{4}-\d{2}-\d{2}$/,
      'must be a quoted "YYYY-MM-DD" string',
    ),
    timeOfDay: text(
      table,
      "time_of_day",
      /^([01]\d|2[0-3]):[0-5]\d$/,
      'must be a quoted "HH:MM" string',
    ),
    wallClockLimitS: (wallClock as number | undefined) ?? DEFAULT_WALL_CLOCK_LIMIT_S,
    turnLimit: integer(table, "turn_limit", 1),
    attachView: integer(table, "attach_view", 1),
    scene: text(
      table,
      "scene",
      SCENE_NAME,
      "must be a plain Scene name (letters, digits, `_` and `-`)",
    ),
    expectedCommands: (expected as string[] | undefined) ?? [],
    oracles: oracleSpecs(table.oracle),
  }
}
