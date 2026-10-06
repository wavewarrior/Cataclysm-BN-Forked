import { assertEquals, assertThrows } from "@std/assert"
import { parseTrial, TrialError } from "./trial.ts"

Deno.test("a Trial naming only a fixture parses with the default wall-clock limit", () => {
  const trial = parseTrial(`fixture = "bairdford"`)
  assertEquals(trial.fixture, "bairdford")
  assertEquals(trial.wallClockLimitS, 300)
  assertEquals(trial.seed, undefined)
  assertEquals(trial.turnLimit, undefined)
})

Deno.test("a full Trial parses every field", () => {
  const trial = parseTrial(`
    fixture = "bairdford"
    seed = 12345
    start_date = "2026-10-06"
    time_of_day = "08:30"
    wall_clock_limit_s = 90
    turn_limit = 200
  `)
  assertEquals(trial, {
    fixture: "bairdford",
    seed: 12345,
    startDate: "2026-10-06",
    timeOfDay: "08:30",
    wallClockLimitS: 90,
    turnLimit: 200,
  })
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
