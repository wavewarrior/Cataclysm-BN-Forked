/// The driver pass: pick ready tickets, give each a lane, run them concurrently. Every effect is
/// injected, so the pick/lane/claim logic is testable without GitHub, herdr or a compiler.
import type { TicketResult } from "./driver.ts"
import type { Issue } from "./gh.ts"
import type { Lane } from "./lanes.ts"
import { isRecentlyClaimed, LanesBusyError } from "./watch.ts"

export type LoopDeps = {
  /// Open `factory:ready` issues (or the single named issue when it is open and ready).
  listReady: () => Promise<Issue[]>
  pick: (ready: Issue[]) => Promise<Issue | undefined>
  /// Why `ready` tickets are held, or "".
  explainHolds: (ready: Issue[]) => Promise<string>
  acquireLane: (issue: number) => Promise<Lane | undefined>
  releaseLane: (lane: Lane) => Promise<void>
  claim: (issue: Issue) => Promise<void>
  runTicket: (issue: Issue, lane: Lane) => Promise<TicketResult>
  now: () => number
  log: (line: string) => void
}

export async function runLoopWith(
  deps: LoopDeps,
  opts: { max: number; claims?: Map<number, number> },
): Promise<void> {
  const running = new Set<Promise<void>>()
  let started = 0
  // `gh issue list` reads a search index that lags a label edit by seconds, so a ticket we just
  // claimed can still be listed as ready. Remember when this process claimed each one.
  const claims = opts.claims ?? new Map<number, number>()
  while (started < opts.max) {
    const listed = await deps.listReady()
    const ready = listed.filter((i) => !isRecentlyClaimed(claims, i.number, deps.now()))
    const next = await deps.pick(ready)
    if (!next) {
      // A running ticket may hand off a follow-up that unblocks a dependent: wait, then rescan.
      if (running.size > 0) {
        await Promise.race(running)
        continue
      }
      if (started === 0) {
        if (ready.length === 0) {
          deps.log("no factory:ready ticket to run")
        } else {
          const why = await deps.explainHolds(ready)
          deps.log(`nothing pickable: ${why || "its dependencies are still open"}`)
        }
      }
      break
    }
    const lane = await deps.acquireLane(next.number)
    if (!lane) {
      if (running.size === 0) {
        throw new LanesBusyError("no lane free and none running: see `status`, `stop <issue>`")
      }
      await Promise.race(running)
      continue
    }
    await deps.claim(next)
    claims.set(next.number, deps.now())
    started++
    deps.log(`lane ${lane.n}: #${next.number} ${next.title}`)
    const job: Promise<void> = deps.runTicket(next, lane)
      .then((r) => deps.log(`lane ${lane.n}: #${next.number} ${r.outcome}`))
      .finally(() => deps.releaseLane(lane))
      .finally(() => running.delete(job))
    running.add(job)
  }
  await Promise.all(running)
}
