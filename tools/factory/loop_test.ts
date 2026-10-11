import { assertEquals, assertRejects } from "@std/assert"
import type { TicketResult } from "./driver.ts"
import { pickNext } from "./driver.ts"
import type { Issue } from "./gh.ts"
import type { Lane } from "./lanes.ts"
import { type LoopDeps, runLoopWith } from "./loop.ts"
import { CLAIM_LAG_MS, LanesBusyError } from "./watch.ts"

/// A ready ticket whose `## Depends on` lists `deps`.
const ticket = (number: number, deps: number[] = []): Issue => ({
  number,
  title: `t${number}`,
  body: deps.length === 0 ? "" : `## Depends on\n${deps.map((d) => `- #${d}`).join("\n")}\n`,
  labels: ["factory:ready"],
  state: "OPEN",
  createdAt: "2026-01-01T00:00:00Z",
  updatedAt: "2026-01-01T00:00:00Z",
})

type Script = (n: number, tracker: Map<number, Issue>) => TicketResult

/// A merged ticket: its issue closes, which is what unblocks a dependent.
const merged: Script = (n, tracker) => {
  tracker.set(n, { ...tracker.get(n)!, state: "CLOSED", labels: [] })
  return { outcome: "pr", url: `https://example.test/pr/${n}` }
}

function harness(issues: Issue[], opts: {
  lanes?: number
  script?: Script
  lagClaims?: boolean
} = {}) {
  const tracker = new Map(issues.map((i) => [i.number, i]))
  const ran: number[] = []
  const logs: string[] = []
  let free = opts.lanes ?? 8
  let clock = 0
  const deps: LoopDeps = {
    listReady: () =>
      Promise.resolve(
        [...tracker.values()].filter((i) =>
          i.state === "OPEN" && i.labels.includes("factory:ready")
        ),
      ),
    pick: (ready) => pickNext(ready, (n) => Promise.resolve(tracker.get(n)?.state === "CLOSED")),
    explainHolds: () => Promise.resolve(""),
    acquireLane: (n) => {
      if (free === 0) return Promise.resolve(undefined)
      free--
      const lane: Lane = {
        n,
        path: `lane-${n}`,
        state: { lane: n, issue: n, pid: 0, startedAt: "", panes: [] },
      }
      return Promise.resolve(lane)
    },
    releaseLane: () => {
      free++
      return Promise.resolve()
    },
    // A real claim moves the ticket out of `factory:ready`; `lagClaims` models the listing lag.
    claim: (issue) => {
      if (!opts.lagClaims) {
        tracker.set(issue.number, { ...issue, labels: ["factory:in-progress"] })
      }
      return Promise.resolve()
    },
    // A ticket runs for a macrotask, so the loop reaches its "nothing pickable" check while it is
    // still in flight, which is the situation the rescan exists for.
    runTicket: async (issue) => {
      ran.push(issue.number)
      const { promise, resolve } = Promise.withResolvers<void>()
      setTimeout(resolve, 0)
      await promise
      return (opts.script ?? merged)(issue.number, tracker)
    },
    now: () => clock,
    log: (line) => logs.push(line),
  }
  return { deps, ran, logs, advance: (ms: number) => clock += ms }
}

Deno.test("independent ready tickets run lowest-numbered first until none remain", async () => {
  const h = harness([ticket(3), ticket(1), ticket(2)], { lanes: 1 })
  await runLoopWith(h.deps, { max: 1000 })
  assertEquals(h.ran, [1, 2, 3])
})

Deno.test("a dependent starts in the same pass once its dependency is merged", async () => {
  const h = harness([ticket(1), ticket(2, [1])], { lanes: 2 })
  await runLoopWith(h.deps, { max: 1000 })
  assertEquals(h.ran, [1, 2])
})

Deno.test("a dependent whose dependency ends in review never starts", async () => {
  const h = harness([ticket(1), ticket(2, [1])], {
    script: (n, tracker) => {
      tracker.set(n, { ...tracker.get(n)!, labels: ["factory:review"] })
      return { outcome: "pr", url: "u" }
    },
  })
  await runLoopWith(h.deps, { max: 1000 })
  assertEquals(h.ran, [1])
})

Deno.test("max stops the pass after that many tickets start", async () => {
  const h = harness([ticket(1), ticket(2), ticket(3)], { lanes: 1 })
  await runLoopWith(h.deps, { max: 1 })
  assertEquals(h.ran, [1])
})

Deno.test("no lane free and nothing running is an error, not a silent stop", async () => {
  const h = harness([ticket(1)], { lanes: 0 })
  await assertRejects(() => runLoopWith(h.deps, { max: 1000 }), LanesBusyError)
  assertEquals(h.ran, [])
})

Deno.test("no lane free while a ticket runs waits for the lane, then starts the next ticket", async () => {
  const h = harness([ticket(1), ticket(2)], { lanes: 1 })
  await runLoopWith(h.deps, { max: 1000 })
  assertEquals(h.ran, [1, 2])
})

Deno.test("a just-claimed ticket still listed as ready is not started twice until the claim lag passes", async () => {
  const h = harness([ticket(1)], { lagClaims: true })
  const claims = new Map([[1, 0]])
  await runLoopWith(h.deps, { max: 1000, claims })
  assertEquals(h.ran, [])
  h.advance(CLAIM_LAG_MS)
  await runLoopWith(h.deps, { max: 1000, claims })
  assertEquals(h.ran, [1])
})

Deno.test("a blocked ticket does not stop an independent ready ticket from running", async () => {
  const h = harness([ticket(1), ticket(2)], {
    lanes: 2,
    script: (n, tracker) => {
      if (n === 1) {
        tracker.set(n, { ...tracker.get(n)!, labels: ["factory:blocked"] })
        return { outcome: "blocked", reason: "gate failed" }
      }
      return merged(n, tracker)
    },
  })
  await runLoopWith(h.deps, { max: 1000 })
  assertEquals(h.ran, [1, 2])
})
