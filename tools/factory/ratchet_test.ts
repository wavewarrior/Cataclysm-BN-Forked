import { assertEquals } from "@std/assert"
import { describe, evaluateRun, grown } from "./ratchet.ts"

const TOTALS = "Catch2: test cases: 3 | 1 passed | 2 failed\nexit 1; log out/x.log"

Deno.test("failures inside the baseline pass; one outside it fails and is named", () => {
  const out = `FAIL known case\nFAIL brand new case\n${TOTALS}`
  const r = evaluateRun(out, ["known case"])
  assertEquals(r.unexpected, ["brand new case"])
  assertEquals(r.ok, false)
})

Deno.test("an all-baseline run is ok and reports baseline cases that now pass", () => {
  const r = evaluateRun(`FAIL a\n${TOTALS}`, ["a", "b"])
  assertEquals(r.ok, true)
  assertEquals(r.fixed, ["b"])
})

Deno.test("an empty baseline demands a green suite", () => {
  assertEquals(evaluateRun(`no unexpected failures in 3 cases\n${TOTALS}`, []).ok, true)
  assertEquals(evaluateRun(`FAIL a\n${TOTALS}`, []).ok, false)
})

Deno.test("a crash or a log without totals is never a pass, even with no FAIL lines", () => {
  assertEquals(evaluateRun(`stopped early: 5 cases never finished\n${TOTALS}`, []).ok, false)
  assertEquals(evaluateRun("", []).ok, false)
})

Deno.test("ANSI colour around FAIL does not hide a failure and names are matched exactly", () => {
  const r = evaluateRun(`\x1b[31mFAIL\x1b[0m case one\n${TOTALS}`, ["case"])
  assertEquals(r.unexpected, ["case one"])
})

Deno.test("grown reports names added relative to the base list", () => {
  assertEquals(grown(["a"], ["a", "b"]), ["b"])
  assertEquals(grown(["a", "b"], ["a"]), [])
  assertEquals(describe(evaluateRun(`FAIL z\n${TOTALS}`, [])).includes("z"), true)
})
