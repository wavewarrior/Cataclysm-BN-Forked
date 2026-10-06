/**
 * Trial: the declarative definition of a playtest, a small TOML file.
 *
 * The supervisor parses it (the engine has no TOML dependency). Unknown fields are errors so a typo
 * never silently becomes a default.
 */
import { parse } from "@std/toml"
import type { Region } from "./frames.ts"

export class TrialError extends Error {
  constructor(message: string) {
    super(message)
    this.name = "TrialError"
  }
}

/** Size of a game window in logical pixels (the swapchain is larger on a HiDPI display). */
export type WindowSize = { width: number; height: number }

/** The window size of a windowed Trial that does not set `window_size`. */
export const DEFAULT_WINDOW_SIZE: WindowSize = { width: 1280, height: 720 }

/** The game keeps a window at least 80x24 cells of its default 8x16 font; a smaller size is refused. */
const MIN_WINDOW_SIZE: WindowSize = { width: 640, height: 384 }

export type Trial = {
  /** Name of a fixture in the fixture library. */
  fixture: string
  /** RNG seed pinned for the Episode. */
  seed?: number
  /**
   * In-game date the clock is pinned to, `YYYY-SS-DD`: year from 0001, season 01 (spring) to 04
   * (winter), day of the season from 01. The game has no months. The day must also fit the world's
   * season length (91 by default), which only the game knows; it refuses a later day.
   */
  startDate?: string
  /** In-game time of day the clock is pinned to, `HH:MM`. */
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
   * The window of a renderer Trial (`mode = "windowed"`): a real game window, visible, at this
   * fixed size in logical pixels, in a corner of the screen. Absent: the Episode is windowless
   * (no window, no display session needed), the default.
   */
  window?: WindowSize
  /**
   * Commands the Trial expects to work: a response to one of them with outcome `unsupported` or
   * `no_effect` fails the Episode. An entry is a command (`move`) or an `action` by name
   * (`action:pause`).
   */
  expectedCommands: string[]
  /** Predicates evaluated over the observation stream, besides the built-in checks. */
  oracles: OracleSpec[]
  /**
   * Oracles over the captured frames of a windowed Trial: a toggled state judged against a paired
   * same-state null.
   */
  rendererOracles: RendererOracleSpec[]
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

/**
 * `paired_null`: two captures of the same state, which must differ by no more than `max_noise`.
 * `diff_vs_null`: a toggled state must differ from the original by more than `factor` times that
 * noise. `triplet`: as `diff_vs_null`, and the restored state must match the original again
 * (1 -> 0 -> 1).
 */
export const RENDERER_KINDS = ["paired_null", "diff_vs_null", "triplet"] as const
export type RendererKind = typeof RENDERER_KINDS[number]

/**
 * A capture oracle. Frames are picked by the `tag` the agent gives each `capture`: the first two
 * captures tagged `original` are the reference and its paired null, the first tagged `toggled` and
 * the first tagged `restored` are the other states. Every number is a frame delta (see
 * frames.ts): the mean absolute colour difference, 0 identical to 1 black against white.
 */
export type RendererOracleSpec = {
  name: string
  kind: RendererKind
  original: string
  toggled?: string
  restored?: string
  /** An effect must exceed this many times the paired null's noise. */
  factor: number
  /** A `paired_null` oracle fails when its two captures differ by more than this. */
  maxNoise: number
  /** Compare only this part of the frame, as fractions of its size. */
  region?: Region
  /**
   * A game message that must be logged before a capture of a state other than the previous
   * capture's: the game says it has settled, so readiness never rests on the pixels.
   */
  ready?: string
  severity: "fail" | "warn"
}

/** Names the built-in checks report under; a Trial oracle may not reuse them. */
export const BUILT_IN_ORACLES = ["alive", "game_log", "turn_counter", "commands", "window_size"]

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
  "mode",
  "window_size",
  "expected_commands",
  "oracle",
]

const ORACLE_FIELDS = ["name", "field", "operator", "value", "mode", "severity"]

const RENDERER_FIELDS = [
  "name",
  "kind",
  "original",
  "toggled",
  "restored",
  "factor",
  "max_noise",
  "region",
  "ready",
  "severity",
]

/** What a capture tag may look like: the agent writes it in `capture` requests. */
export const CAPTURE_TAG = /^[A-Za-z0-9_-]+$/

/** Defaults of a capture oracle: twice the noise, and a null no noisier than this. */
const DEFAULT_FACTOR = 2
const DEFAULT_MAX_NOISE = 0.02

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

/**
 * `start_date`: `YYYY-SS-DD` with a year from 1, a season 01 to 04 and a day 01 to 99. Whether
 * the day fits the world's season length is the game's to say.
 */
function startDate(table: Record<string, unknown>): string | undefined {
  const expectation =
    'must be a quoted "YYYY-SS-DD" string: year from 0001, season 01 to 04 (spring to winter; the game has no months), day of the season from 01'
  const value = text(table, "start_date", /^\d{4}-\d{2}-\d{2}$/, expectation)
  if (value === undefined) return undefined
  const [year, season, day] = value.split("-").map(Number)
  if (year < 1 || season < 1 || season > 4 || day < 1) fail("start_date", expectation)
  return value
}

/** The window a Trial asks for: none when windowless, else its `window_size` or the default. */
function windowOf(table: Record<string, unknown>): WindowSize | undefined {
  const mode = table.mode
  if (mode !== undefined && mode !== "windowless" && mode !== "windowed") {
    fail("mode", 'must be "windowless" (the default) or "windowed"')
  }
  const size = table.window_size
  if (mode !== "windowed") {
    if (size !== undefined) fail("window_size", 'is only meaningful with mode = "windowed"')
    return undefined
  }
  if (size === undefined) return DEFAULT_WINDOW_SIZE
  const expectation =
    `must be [width, height], whole numbers of at least ${MIN_WINDOW_SIZE.width} by ${MIN_WINDOW_SIZE.height}`
  if (!Array.isArray(size) || size.length !== 2) fail("window_size", expectation)
  const [width, height] = size.map((n) =>
    (typeof n === "number" || typeof n === "bigint") ? Number(n) : NaN
  )
  if (
    !Number.isInteger(width) || !Number.isInteger(height) ||
    width < MIN_WINDOW_SIZE.width || height < MIN_WINDOW_SIZE.height
  ) {
    fail("window_size", expectation)
  }
  return { width, height }
}

function rendererSpec(table: Record<string, unknown>, where: string): RendererOracleSpec {
  for (const key of Object.keys(table)) {
    if (!RENDERER_FIELDS.includes(key)) {
      fail(`${where} field \`${key}\``, `is unknown (known: ${RENDERER_FIELDS.join(", ")})`)
    }
  }
  const kind = table.kind as RendererKind
  if (!RENDERER_KINDS.includes(kind)) {
    fail(`${where} kind`, `must be one of ${RENDERER_KINDS.join(", ")}`)
  }
  const tag = (key: string): string | undefined => {
    const value = table[key]
    if (value === undefined) return undefined
    if (typeof value !== "string" || !CAPTURE_TAG.test(value)) {
      fail(`${where} ${key}`, "must be a capture tag (letters, digits, `_` and `-`)")
    }
    return value
  }
  const original = tag("original")
  if (original === undefined) {
    fail(`${where} original`, "is required: the tag of the original state")
  }
  const toggled = tag("toggled")
  const restored = tag("restored")
  if (kind === "paired_null") {
    if (toggled !== undefined) fail(`${where} toggled`, "means nothing to a paired_null oracle")
  } else if (toggled === undefined) {
    fail(`${where} toggled`, `is required by a ${kind} oracle: the tag of the toggled state`)
  }
  if (kind === "triplet") {
    if (restored === undefined) {
      fail(`${where} restored`, "is required by a triplet oracle: the tag of the restored state")
    }
  } else if (restored !== undefined) {
    fail(`${where} restored`, `means nothing to a ${kind} oracle`)
  }

  const factor = table.factor ?? DEFAULT_FACTOR
  if (typeof factor !== "number" || !Number.isFinite(factor) || factor < 1) {
    fail(`${where} factor`, "must be a number of at least 1")
  }
  const maxNoise = table.max_noise ?? DEFAULT_MAX_NOISE
  if (typeof maxNoise !== "number" || !(maxNoise >= 0 && maxNoise <= 1)) {
    fail(`${where} max_noise`, "must be a number from 0 to 1 (a share of the colour range)")
  }

  let region: Region | undefined
  if (table.region !== undefined) {
    const r = table.region
    const ok = Array.isArray(r) && r.length === 4 && r.every((n) => typeof n === "number") &&
      r[0] >= 0 && r[1] >= 0 && r[2] > 0 && r[3] > 0 && r[0] + r[2] <= 1 && r[1] + r[3] <= 1
    if (!ok) {
      fail(`${where} region`, "must be [x, y, width, height], fractions of the frame inside 0..1")
    }
    region = r as Region
  }
  if (table.ready !== undefined && (typeof table.ready !== "string" || table.ready === "")) {
    fail(`${where} ready`, "must be a non-empty message text")
  }
  if (table.severity !== undefined && table.severity !== "fail" && table.severity !== "warn") {
    fail(`${where} severity`, 'must be "fail" or "warn"')
  }
  const name = table.name ?? `${kind}: ${original}`
  if (typeof name !== "string" || name === "") fail(`${where} name`, "must be a non-empty string")
  return {
    name,
    kind,
    original,
    ...(toggled === undefined ? {} : { toggled }),
    ...(restored === undefined ? {} : { restored }),
    factor,
    maxNoise,
    ...(region === undefined ? {} : { region }),
    ...(table.ready === undefined ? {} : { ready: table.ready as string }),
    severity: table.severity === "warn" ? "warn" : "fail",
  }
}

/** The two kinds of `[[oracle]]` table: predicates over the stream, and capture oracles. */
function oracleSpecs(
  raw: unknown,
  windowed: boolean,
): { oracles: OracleSpec[]; rendererOracles: RendererOracleSpec[] } {
  if (raw === undefined) return { oracles: [], rendererOracles: [] }
  if (!Array.isArray(raw)) fail("oracle", "must be a list of `[[oracle]]` tables")
  const specs: OracleSpec[] = []
  const renderer: RendererOracleSpec[] = []
  const taken = (name: string) =>
    BUILT_IN_ORACLES.includes(name) || specs.some((s) => s.name === name) ||
    renderer.some((s) => s.name === name)
  for (const [i, entry] of raw.entries()) {
    const where = `oracle ${i + 1}`
    if (typeof entry !== "object" || entry === null || Array.isArray(entry)) {
      fail("oracle", "must be a list of `[[oracle]]` tables")
    }
    const table = entry as Record<string, unknown>
    if ("kind" in table) {
      if (!windowed) {
        fail(
          `${where} kind`,
          'is a capture oracle, which needs a windowed Trial (mode = "windowed")',
        )
      }
      const spec = rendererSpec(table, where)
      if (taken(spec.name)) {
        fail(`${where} name`, `\`${spec.name}\` is already taken by another oracle`)
      }
      renderer.push(spec)
      continue
    }
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
    if (taken(name)) fail(`${where} name`, `\`${name}\` is already taken by another oracle`)
    specs.push({
      name,
      field: table.field,
      operator,
      value,
      mode,
      severity: table.severity === "warn" ? "warn" : "fail",
    })
  }
  return { oracles: specs, rendererOracles: renderer }
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

  const window = windowOf(table)
  const { oracles, rendererOracles } = oracleSpecs(table.oracle, window !== undefined)

  return {
    fixture,
    seed: integer(table, "seed", 0),
    startDate: startDate(table),
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
    ...(window ? { window } : {}),
    expectedCommands: (expected as string[] | undefined) ?? [],
    oracles,
    rendererOracles,
  }
}
