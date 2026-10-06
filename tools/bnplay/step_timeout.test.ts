import { assertEquals } from "@std/assert"
import { stepTimeout, TURN_CAP, turnBudget } from "./step_timeout.ts"

const config = { stepTimeoutMs: 30_000, turnTimeoutMs: 100 }

Deno.test("a request that never spends time gets the plain step timeout", () => {
  for (const cmd of ["ping", "state", "seed", "set_time", "attach_view", "move", "capture"]) {
    assertEquals(stepTimeout(config, { cmd }), 30_000, cmd)
  }
})

Deno.test("a wait or an activity gets time for the turns it may spend, up to the cap", () => {
  assertEquals(stepTimeout(config, { cmd: "wait", turns: 10 }), 31_000)
  assertEquals(stepTimeout(config, { cmd: "craft", max_turns: 50 }), 35_000)
  // The cap is the longest a request runs; asking for more earns no more time.
  assertEquals(stepTimeout(config, { cmd: "wait", turns: 1001 }), 30_000 + TURN_CAP * 100)
  assertEquals(stepTimeout(config, { cmd: "wait", turns: 1e9 }), 30_000 + TURN_CAP * 100)
})

Deno.test("a command that can run the world without naming a limit may spend the whole cap", () => {
  for (const cmd of ["action", "sleep", "craft", "key", "melee"]) {
    assertEquals(turnBudget({ cmd }), TURN_CAP, cmd)
  }
})

Deno.test("a turn count that is not a positive whole number earns nothing extra", () => {
  assertEquals(turnBudget({ cmd: "state", turns: -3 }), 0)
  assertEquals(turnBudget({ cmd: "state", turns: 1.5 }), 0)
  assertEquals(turnBudget({ cmd: "state", max_turns: "9" }), 0)
})
