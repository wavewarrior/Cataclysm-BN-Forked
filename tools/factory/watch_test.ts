import { assertEquals } from "@std/assert"
import {
  CLAIM_LAG_MS,
  isRecentlyClaimed,
  LanesBusyError,
  MAX_BACKOFF_SEC,
  nextDelaySec,
  watch,
  type WatchOptions,
} from "./watch.ts"

/// Runs `watch` with instant, recorded sleeps and logs so its timing is observable.
async function run(
  opts: Pick<WatchOptions, "hasWork" | "runPass"> & Partial<Pick<WatchOptions, "once" | "signal">>,
) {
  const sleeps: number[] = []
  const logs: string[] = []
  const passes = await watch({
    intervalSec: 60,
    ...opts,
    sleep: (ms) => {
      sleeps.push(ms / 1000)
      return Promise.resolve()
    },
    log: (line) => logs.push(line),
  })
  return { passes, sleeps, logs }
}

Deno.test("nextDelaySec doubles per failure and caps at the backoff maximum", () => {
  assertEquals(nextDelaySec(60, 1), 120)
  assertEquals(nextDelaySec(60, 2), 240)
  assertEquals(nextDelaySec(60, 10), MAX_BACKOFF_SEC)
  // An interval longer than the cap is never shortened.
  assertEquals(nextDelaySec(900, 1), 900)
})

Deno.test("watch never starts a pass while nothing is pickable", async () => {
  let started = 0
  const result = await run({
    once: true,
    hasWork: () => Promise.resolve(false),
    runPass: () => {
      started++
      return Promise.resolve()
    },
  })
  assertEquals(result.passes, 0)
  assertEquals(started, 0)
  assertEquals(result.sleeps, [])
})

Deno.test("watch starts exactly one pass for one pickable ticket", async () => {
  let started = 0
  const result = await run({
    once: true,
    hasWork: () => Promise.resolve(true),
    runPass: () => {
      started++
      return Promise.resolve()
    },
  })
  assertEquals(result.passes, 1)
  assertEquals(started, 1)
})

Deno.test("watch backs off after failing passes and keeps polling", async () => {
  let calls = 0
  const controller = new AbortController()
  const result = await run({
    signal: controller.signal,
    hasWork: () => Promise.resolve(true),
    runPass: () => {
      calls++
      if (calls === 3) controller.abort()
      if (calls <= 2) return Promise.reject(new Error("gh unreachable"))
      return Promise.resolve()
    },
  })
  // Two failures double the wait; the successful third pass resets the failure count.
  assertEquals(result.sleeps, [120, 240])
  assertEquals(result.logs.filter((l) => l.startsWith("error")).length, 2)
  assertEquals(calls, 3)
})

Deno.test("watch backs off when the ticket check itself fails", async () => {
  let checks = 0
  const controller = new AbortController()
  const result = await run({
    signal: controller.signal,
    hasWork: () => {
      checks++
      if (checks === 1) return Promise.reject(new Error("HTTP 401"))
      controller.abort()
      return Promise.resolve(false)
    },
    runPass: () => Promise.resolve(),
  })
  assertEquals(result.sleeps, [120])
  assertEquals(result.logs.some((l) => l.includes("HTTP 401")), true)
})

Deno.test("watch does not sleep after a single tick", async () => {
  const result = await run({
    once: true,
    hasWork: () => Promise.resolve(false),
    runPass: () => Promise.resolve(),
  })
  assertEquals(result.sleeps, [])
})

Deno.test("watch does not start another pass once aborted during a pass", async () => {
  const controller = new AbortController()
  let started = 0
  const result = await run({
    signal: controller.signal,
    hasWork: () => Promise.resolve(true),
    runPass: () => {
      started++
      controller.abort()
      return Promise.resolve()
    },
  })
  assertEquals(result.passes, 1)
  assertEquals(started, 1)
})

Deno.test("isRecentlyClaimed ignores a ticket only inside the lag window", () => {
  const claims = new Map([[7, 1_000]])
  assertEquals(isRecentlyClaimed(claims, 7, 1_000 + CLAIM_LAG_MS - 1), true)
  assertEquals(isRecentlyClaimed(claims, 7, 1_000 + CLAIM_LAG_MS), false)
  assertEquals(isRecentlyClaimed(claims, 8, 1_000), false)
})

Deno.test("watch treats busy lanes as idle, not as a failure", async () => {
  let calls = 0
  const controller = new AbortController()
  const result = await run({
    signal: controller.signal,
    hasWork: () => Promise.resolve(true),
    runPass: () => {
      calls++
      if (calls === 4) controller.abort()
      if (calls <= 3) {
        return Promise.reject(new LanesBusyError("implementer lanes are held by another driver"))
      }
      return Promise.resolve()
    },
  })
  // Busy lanes never double the wait, and the busy state is logged once, not on every tick.
  assertEquals(result.sleeps, [60, 60, 60])
  assertEquals(result.logs.filter((l) => l.includes("retrying every 60s")).length, 1)
  assertEquals(result.logs.filter((l) => l.startsWith("error")).length, 0)
  assertEquals(calls, 4)
})
