import { assertEquals, assertThrows } from "@std/assert"
import { DEFAULT_WINDOW_SIZE, parseTrial, TrialError } from "./trial.ts"

Deno.test("a Trial naming only a fixture parses with the default wall-clock limit", () => {
  const trial = parseTrial(`fixture = "bairdford"`)
  assertEquals(trial.fixture, "bairdford")
  assertEquals(trial.wallClockLimitS, 300)
  assertEquals(trial.seed, undefined)
  assertEquals(trial.turnLimit, undefined)
  assertEquals(trial.attachView, undefined)
  assertEquals(trial.scene, undefined)
})

Deno.test("a full Trial parses every field", () => {
  const trial = parseTrial(`
    fixture = "bairdford"
    seed = 12345
    start_date = "0002-03-10"
    time_of_day = "08:30"
    wall_clock_limit_s = 90
    turn_limit = 200
    attach_view = 3
    scene = "lightone"
  `)
  assertEquals(trial, {
    fixture: "bairdford",
    seed: 12345,
    startDate: "0002-03-10",
    timeOfDay: "08:30",
    wallClockLimitS: 90,
    turnLimit: 200,
    attachView: 3,
    scene: "lightone",
    expectedCommands: [],
    oracles: [],
    rendererOracles: [],
  })
})

Deno.test("attach_view names the radius of the view attached to every response", () => {
  assertEquals(parseTrial(`fixture = "a"\nattach_view = 1`).attachView, 1)
  assertEquals(parseTrial(`fixture = "a"\nattach_view = 10`).attachView, 10)
  for (const text of ["0", "-2", "1.5", '"3"', "true"]) {
    const toml = `fixture = "a"\nattach_view = ${text}`
    const err = assertThrows(() => parseTrial(toml), TrialError, undefined, toml)
    assertEquals(err.message.includes("attach_view"), true, `${toml} -> ${err.message}`)
  }
})

Deno.test("scene names the Scene run when the Episode starts", () => {
  assertEquals(parseTrial(`fixture = "a"\nscene = "lightone"`).scene, "lightone")
  assertEquals(parseTrial(`fixture = "a"\nscene = "shadow-test_2"`).scene, "shadow-test_2")
  for (const text of ['""', '"../x"', '"a/b"', '"x.lua"', "3", "true"]) {
    const toml = `fixture = "a"\nscene = ${text}`
    const err = assertThrows(() => parseTrial(toml), TrialError, undefined, toml)
    assertEquals(err.message.includes("scene"), true, `${toml} -> ${err.message}`)
  }
})

Deno.test("a Trial is windowless unless it selects the windowed mode", () => {
  assertEquals(parseTrial(`fixture = "a"`).window, undefined)
  assertEquals(parseTrial(`fixture = "a"\nmode = "windowless"`).window, undefined)
})

Deno.test("mode windowed selects a window of the stated default size", () => {
  assertEquals(parseTrial(`fixture = "a"\nmode = "windowed"`).window, DEFAULT_WINDOW_SIZE)
  assertEquals(DEFAULT_WINDOW_SIZE, { width: 1280, height: 720 })
})

Deno.test("window_size fixes the size of the window of a windowed Trial", () => {
  const trial = parseTrial(`fixture = "a"\nmode = "windowed"\nwindow_size = [1024, 768]`)
  assertEquals(trial.window, { width: 1024, height: 768 })
})

Deno.test("a bad mode or window size is rejected with the field name", () => {
  for (
    const [field, text] of [
      ["mode", `fixture = "a"\nmode = "headless"`],
      ["mode", `fixture = "a"\nmode = 1`],
      ["window_size", `fixture = "a"\nmode = "windowed"\nwindow_size = [800]`],
      ["window_size", `fixture = "a"\nmode = "windowed"\nwindow_size = [800, 600, 3]`],
      ["window_size", `fixture = "a"\nmode = "windowed"\nwindow_size = [800.5, 600]`],
      ["window_size", `fixture = "a"\nmode = "windowed"\nwindow_size = ["800", "600"]`],
      ["window_size", `fixture = "a"\nmode = "windowed"\nwindow_size = "800x600"`],
      // Below the game's 80x24 cell minimum the game would resize the window itself.
      ["window_size", `fixture = "a"\nmode = "windowed"\nwindow_size = [320, 200]`],
      // A size means nothing without a window.
      ["window_size", `fixture = "a"\nwindow_size = [1024, 768]`],
    ] as const
  ) {
    const err = assertThrows(() => parseTrial(text), TrialError, undefined, text)
    assertEquals(err.message.includes(field), true, `${text} -> ${err.message}`)
  }
})

Deno.test("oracles and expected commands parse with their defaults", () => {
  const trial = parseTrial(`
    fixture = "bairdford"
    expected_commands = ["wait", "action:pause"]

    [[oracle]]
    field = "hp"
    operator = "gt"
    value = 0

    [[oracle]]
    name = "found the exit"
    field = "prompt"
    operator = "eq"
    value = "map"
    mode = "by-turn-20"
    severity = "warn"

    [[oracle]]
    field = "new_messages"
    operator = "contains"
    value = "You die"
    mode = "never"
  `)
  assertEquals(trial.expectedCommands, ["wait", "action:pause"])
  assertEquals(trial.oracles, [
    {
      name: "always: hp gt 0",
      field: "hp",
      operator: "gt",
      value: 0,
      mode: "always",
      severity: "fail",
    },
    {
      name: "found the exit",
      field: "prompt",
      operator: "eq",
      value: "map",
      mode: { byTurn: 20 },
      severity: "warn",
    },
    {
      name: "never: new_messages contains You die",
      field: "new_messages",
      operator: "contains",
      value: "You die",
      mode: "never",
      severity: "fail",
    },
  ])
})

Deno.test("malformed oracles are rejected and the error says which part is wrong", () => {
  const base = `fixture = "a"\n[[oracle]]\n`
  for (
    const [expected, body] of [
      ["operator", `field = "hp"\nvalue = 1`],
      ["operator", `field = "hp"\noperator = "approx"\nvalue = 1`],
      ["field", `operator = "eq"\nvalue = 1`],
      ["value", `field = "hp"\noperator = "eq"`],
      ["value", `field = "hp"\noperator = "gt"\nvalue = "high"`],
      ["mode", `field = "hp"\noperator = "eq"\nvalue = 1\nmode = "sometimes"`],
      ["mode", `field = "hp"\noperator = "eq"\nvalue = 1\nmode = "by-turn-0"`],
      ["severity", `field = "hp"\noperator = "eq"\nvalue = 1\nseverity = "error"`],
      ["colour", `field = "hp"\noperator = "eq"\nvalue = 1\ncolour = "red"`],
      ["name", `field = "hp"\noperator = "eq"\nvalue = 1\nname = "alive"`],
    ] as const
  ) {
    const err = assertThrows(() => parseTrial(base + body), TrialError, undefined, body)
    assertEquals(err.message.includes(expected), true, `${body} -> ${err.message}`)
  }
  const twice = `${base}field = "hp"\noperator = "eq"\nvalue = 1\nname = "x"\n` +
    `[[oracle]]\nfield = "hp"\noperator = "eq"\nvalue = 2\nname = "x"`
  assertThrows(() => parseTrial(twice), TrialError, "already taken")
  assertThrows(
    () => parseTrial(`fixture = "a"\nexpected_commands = "wait"`),
    TrialError,
    "expected_commands",
  )
})

Deno.test("a Trial without a fixture is rejected and the error names the field", () => {
  const err = assertThrows(() => parseTrial(`seed = 1`), TrialError)
  assertEquals(err.message.includes("fixture"), true)
})

Deno.test("an unknown field is rejected and the error names it", () => {
  const err = assertThrows(
    () => parseTrial(`fixture = "bairdford"\nsedd = 1`),
    TrialError,
  )
  assertEquals(err.message.includes("sedd"), true)
})

Deno.test("fields of the wrong type or range are rejected with the field name", () => {
  for (
    const [field, text] of [
      ["seed", `fixture = "a"\nseed = "one"`],
      ["seed", `fixture = "a"\nseed = -3`],
      ["wall_clock_limit_s", `fixture = "a"\nwall_clock_limit_s = 0`],
      ["turn_limit", `fixture = "a"\nturn_limit = 1.5`],
      ["start_date", `fixture = "a"\nstart_date = "yesterday"`],
      ["start_date", `fixture = "a"\nstart_date = "2026-10-06"`],
      ["start_date", `fixture = "a"\nstart_date = "0000-01-01"`],
      ["start_date", `fixture = "a"\nstart_date = "0001-00-01"`],
      ["start_date", `fixture = "a"\nstart_date = "0001-05-01"`],
      ["start_date", `fixture = "a"\nstart_date = "0001-01-00"`],
      ["time_of_day", `fixture = "a"\ntime_of_day = "24:00"`],
      ["time_of_day", `fixture = "a"\ntime_of_day = "12:60"`],
      ["time_of_day", `fixture = "a"\ntime_of_day = 1200`],
      ["time_of_day", `fixture = "a"\ntime_of_day = "25:00"`],
      ["fixture", `fixture = 7`],
    ] as const
  ) {
    const err = assertThrows(() => parseTrial(text), TrialError, undefined, text)
    assertEquals(err.message.includes(field), true, `${text} -> ${err.message}`)
  }
})

Deno.test("a fixture name cannot escape the fixture library", () => {
  for (const name of ["../secret", "a/b", "", ".hidden", "a b"]) {
    assertThrows(() => parseTrial(`fixture = ${JSON.stringify(name)}`), TrialError)
  }
})

Deno.test("text that is not TOML is rejected with a Trial error", () => {
  assertThrows(() => parseTrial(`fixture = = oops`), TrialError)
})

const WINDOWED = `fixture = "a"\nmode = "windowed"\n`

Deno.test("capture oracles parse with the defaults a renderer Trial wants", () => {
  const trial = parseTrial(
    `${WINDOWED}
     [[oracle]]
     kind = "triplet"
     original = "on"
     toggled = "off"
     restored = "on-again"
     [[oracle]]
     kind = "diff_vs_null"
     name = "glow"
     original = "on"
     toggled = "off"
     factor = 3
     region = [0, 0.5, 0.5, 0.25]
     ready = "lighting settled"
     severity = "warn"
     [[oracle]]
     kind = "paired_null"
     original = "on"
     max_noise = 0.01
     [[oracle]]
     field = "hp"
     operator = "gt"
     value = 0`,
  )
  assertEquals(trial.oracles.length, 1)
  assertEquals(trial.rendererOracles, [
    {
      name: "triplet: on",
      kind: "triplet",
      original: "on",
      toggled: "off",
      restored: "on-again",
      factor: 2,
      maxNoise: 0.02,
      severity: "fail",
    },
    {
      name: "glow",
      kind: "diff_vs_null",
      original: "on",
      toggled: "off",
      factor: 3,
      maxNoise: 0.02,
      region: [0, 0.5, 0.5, 0.25],
      ready: "lighting settled",
      severity: "warn",
    },
    {
      name: "paired_null: on",
      kind: "paired_null",
      original: "on",
      factor: 2,
      maxNoise: 0.01,
      severity: "fail",
    },
  ])
})

Deno.test("a capture oracle needs a windowed Trial", () => {
  const err = assertThrows(
    () => parseTrial(`fixture = "a"\n[[oracle]]\nkind = "paired_null"\noriginal = "on"`),
    TrialError,
  )
  assertEquals(err.message.includes("windowed"), true, err.message)
})

Deno.test("malformed capture oracles are rejected and the error says which part is wrong", () => {
  const base = `${WINDOWED}[[oracle]]\n`
  for (
    const [expected, body] of [
      ["kind", `kind = "sparkle"\noriginal = "on"`],
      ["original", `kind = "paired_null"`],
      ["original", `kind = "paired_null"\noriginal = "has space"`],
      ["toggled", `kind = "diff_vs_null"\noriginal = "on"`],
      ["toggled", `kind = "triplet"\noriginal = "on"\nrestored = "on2"`],
      ["restored", `kind = "triplet"\noriginal = "on"\ntoggled = "off"`],
      ["toggled", `kind = "paired_null"\noriginal = "on"\ntoggled = "off"`],
      ["restored", `kind = "diff_vs_null"\noriginal = "on"\ntoggled = "off"\nrestored = "on"`],
      ["factor", `kind = "paired_null"\noriginal = "on"\nfactor = 0.5`],
      ["max_noise", `kind = "paired_null"\noriginal = "on"\nmax_noise = 2`],
      ["region", `kind = "paired_null"\noriginal = "on"\nregion = [0, 0, 1]`],
      ["region", `kind = "paired_null"\noriginal = "on"\nregion = [0.5, 0, 0.75, 1]`],
      ["region", `kind = "paired_null"\noriginal = "on"\nregion = [0, 0, 0, 1]`],
      ["ready", `kind = "paired_null"\noriginal = "on"\nready = ""`],
      ["field", `kind = "paired_null"\noriginal = "on"\nfield = "hp"`],
      ["severity", `kind = "paired_null"\noriginal = "on"\nseverity = "error"`],
      ["name", `kind = "paired_null"\noriginal = "on"\nname = "window_size"`],
    ] as const
  ) {
    const err = assertThrows(() => parseTrial(base + body), TrialError, undefined, body)
    assertEquals(err.message.includes(expected), true, `${body} -> ${err.message}`)
  }
  const same = `${base}kind = "paired_null"\noriginal = "on"\nname = "x"\n` +
    `[[oracle]]\nkind = "paired_null"\noriginal = "off"\nname = "x"`
  assertThrows(() => parseTrial(same), TrialError, "already taken")
  // A predicate and a capture oracle share one namespace of report names.
  const mixed = `${base}kind = "paired_null"\noriginal = "on"\nname = "x"\n` +
    `[[oracle]]\nfield = "hp"\noperator = "eq"\nvalue = 1\nname = "x"`
  assertThrows(() => parseTrial(mixed), TrialError, "already taken")
})
