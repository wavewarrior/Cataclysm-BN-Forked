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
}

/** Wall-clock limit applied when a Trial does not set one: an Episode never runs unbounded. */
export const DEFAULT_WALL_CLOCK_LIMIT_S = 300

const FIELDS = [
  "fixture",
  "seed",
  "start_date",
  "time_of_day",
  "wall_clock_limit_s",
  "turn_limit",
]

const FIXTURE_NAME = /^[A-Za-z0-9][A-Za-z0-9._-]*$/

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
  }
}
