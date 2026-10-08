import { assertEquals, assertStringIncludes } from "@std/assert"
import { checkPush, parsePushLines, type PushEvidence } from "./check_push.ts"
import type { Stamp } from "./gate.ts"

const SHA = "a".repeat(40)
const OTHER = "b".repeat(40)
const ref = (remote: string, sha = SHA) => ({
  localRef: "refs/heads/factory/7-x",
  localSha: sha,
  remoteRef: remote,
  remoteSha: "0".repeat(40),
})
const stamp = (patch: Partial<Stamp> = {}): Stamp => ({
  headSha: SHA,
  baseSha: OTHER,
  tier: "full",
  ok: true,
  finishedAt: "2026-01-01T00:00:00Z",
  failedSteps: [],
  infraSteps: [],
  ticket: 7,
  ...patch,
})
const evidence = (
  s: Stamp | undefined,
  verdict: string | null = "VERDICT: PASS",
): PushEvidence => ({
  stamp: s,
  reviewFirstLine: () => verdict ?? undefined,
})
const FACTORY = "refs/heads/factory/7-x"

Deno.test("a full passing stamp for the pushed sha plus a PASS verdict is allowed", () => {
  assertEquals(checkPush([ref(FACTORY)], evidence(stamp())), [])
})

Deno.test("only factory/* branches may be pushed", () => {
  const problems = checkPush([ref("refs/heads/feature/improvements")], evidence(stamp()))
  assertStringIncludes(problems.join("\n"), "may only push")
})

Deno.test("a missing, failing, fast or stale stamp each refuses the push", () => {
  assertStringIncludes(checkPush([ref(FACTORY)], evidence(undefined)).join(), "no gate stamp")
  assertStringIncludes(
    checkPush([ref(FACTORY)], evidence(stamp({ ok: false, failedSteps: ["build"] }))).join(),
    "build",
  )
  assertStringIncludes(
    checkPush([ref(FACTORY)], evidence(stamp({ tier: "fast" }))).join(),
    "need full",
  )
  assertStringIncludes(
    checkPush([ref(FACTORY, OTHER)], evidence(stamp())).join(),
    "stamp sha mismatch",
  )
})

Deno.test("a missing or failing review verdict refuses the push", () => {
  assertStringIncludes(
    checkPush([ref(FACTORY)], evidence(stamp(), null)).join(),
    "no review verdict",
  )
  assertStringIncludes(
    checkPush([ref(FACTORY)], evidence(stamp(), "VERDICT: FAIL")).join(),
    "VERDICT: FAIL",
  )
})

Deno.test("deleting a remote branch is refused even with perfect evidence", () => {
  assertStringIncludes(
    checkPush([ref(FACTORY, "0".repeat(40))], evidence(stamp())).join(),
    "deleting",
  )
})

Deno.test("git's pre-push lines parse and every pushed ref is judged", () => {
  const text = `refs/heads/factory/7-x ${SHA} ${FACTORY} ${
    "0".repeat(40)
  }\nrefs/heads/x ${SHA} refs/heads/main ${OTHER}\n`
  const refs = parsePushLines(text)
  assertEquals(refs.length, 2)
  assertEquals(checkPush(refs, evidence(stamp())).length, 1)
})
