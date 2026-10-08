import { assertEquals, assertStringIncludes } from "@std/assert"
import { parseVerdict, pickNext, prTitle, renderPrBody } from "./driver.ts"
import type { Issue } from "./gh.ts"
import type { Stamp } from "./gate.ts"

const issue = (number: number, body = "", title = `t${number}`): Issue => ({
  number,
  title,
  body,
  labels: ["factory:ready"],
  state: "OPEN",
})

Deno.test("only an exact first-line verdict counts; a verdict buried later is ignored", () => {
  assertEquals(parseVerdict("VERDICT: PASS\n1. fine"), "PASS")
  assertEquals(parseVerdict("VERDICT: FAIL\r\n1. x.cpp:3"), "FAIL")
  assertEquals(parseVerdict("  VERDICT: PASS  \n"), "PASS")
  assertEquals(parseVerdict("Looks good\nVERDICT: PASS"), undefined)
  assertEquals(parseVerdict("VERDICT: MAYBE"), undefined)
  assertEquals(parseVerdict(""), undefined)
})

Deno.test("pickNext takes the lowest-numbered ticket whose dependencies are all closed", async () => {
  const closed = new Set([5])
  const isClosed = (n: number) => Promise.resolve(closed.has(n))
  const ready = [
    issue(9),
    issue(7, "## Depends on\n- #8\n"),
    issue(8, "## Depends on\n- #5\n- #6\n"),
    issue(10, "## Depends on\n- #5\n"),
  ]
  // 7 waits on open 8; 8 waits on open 6; 9 has none.
  assertEquals((await pickNext(ready, isClosed))?.number, 9)
  closed.add(6)
  assertEquals((await pickNext(ready, isClosed))?.number, 8)
  assertEquals(await pickNext([issue(7, "## Depends on\n- #99\n")], isClosed), undefined)
  assertEquals(await pickNext([], isClosed), undefined)
})

Deno.test("an issue title without a conventional type gets chore:, one with a type is kept", () => {
  assertEquals(prTitle("fix(rot): corpses never rot"), "fix(rot): corpses never rot")
  assertEquals(prTitle("feat!: drop the old API"), "feat!: drop the old API")
  assertEquals(
    prTitle("Make the rot test boundary exact"),
    "chore: Make the rot test boundary exact",
  )
  assertEquals(prTitle("fix: "), "chore: fix: ")
})

Deno.test("the PR body links the issue, reports the gate and reviewer, and keeps the template checklist", () => {
  const stamp: Stamp = {
    headSha: "a".repeat(40),
    baseSha: "b".repeat(40),
    tier: "full",
    ok: true,
    finishedAt: "2026-01-01T00:00:00Z",
    failedSteps: [],
    ticket: 12,
  }
  const template =
    "## Purpose of change (The Why)\n\n<!-- c -->\n\n## Testing\n\n<!-- t -->\n\n- [ ] This PR used AI assistance.\n"
  const body = renderPrBody({
    template,
    issue: issue(12, "", "fix: x"),
    stamp,
    verdict: "PASS",
    findings: "VERDICT: PASS\n1. nit",
  })
  assertStringIncludes(body, "Closes #12")
  assertStringIncludes(body, "aaaaaaaaaa")
  assertStringIncludes(body, "Reviewer: PASS")
  assertStringIncludes(body, "- [x] This PR used AI assistance.")
  // The Why section comes before Testing, as in the template.
  assertEquals(body.indexOf("Closes #12") < body.indexOf("Reviewer: PASS"), true)
})
