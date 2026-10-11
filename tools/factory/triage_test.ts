import { assertEquals, assertRejects } from "@std/assert"
import type { CommentInfo, Issue } from "./gh.ts"
import { needsHumanFeed, staleSweep, type TriageDeps, unblock, type UnblockDeps } from "./triage.ts"

const DAY = 86_400_000
const NOW = Date.parse("2026-10-11T12:00:00Z")
const iso = (ms: number) => new Date(ms).toISOString()

const issue = (number: number, labels: string[], updatedAt = iso(NOW)): Issue => ({
  number,
  title: `t${number}`,
  body: "",
  labels,
  state: "OPEN",
  createdAt: iso(NOW - 30 * DAY),
  updatedAt,
})

const comment = (
  body: string,
  publishedAt: string,
  association = "NONE",
  author = "someone",
): CommentInfo => ({ author, association, body, publishedAt })

const BLOCK = "factory: blocked (needs human).\n\nmissing sections"

Deno.test("needs-human feed holds only blocked issues that also carry needs-human", async () => {
  const all = [
    issue(1, ["factory:blocked", "factory:needs-human"]),
    issue(2, ["factory:blocked"]),
    issue(3, ["factory:needs-human"]),
  ]
  const deps: TriageDeps = {
    listIssues: () => Promise.resolve(all.filter((i) => i.labels.includes("factory:needs-human"))),
    listComments: () => Promise.resolve([comment(BLOCK, iso(NOW - 2 * 3_600_000))]),
  }
  const rows = await needsHumanFeed(deps, NOW)
  assertEquals(rows.map((r) => r.number), [1])
  assertEquals(rows[0].awaitingSince, 2)
  assertEquals(rows[0].humanReply, null)
})

Deno.test("a human reply after the block is the reply; outsider, bot and older replies are not", async () => {
  const blockedAt = iso(NOW - 5 * 3_600_000)
  const deps: TriageDeps = {
    listIssues: () => Promise.resolve([issue(1, ["factory:blocked", "factory:needs-human"])]),
    listComments: () =>
      Promise.resolve([
        comment("earlier human words", iso(NOW - 9 * 3_600_000), "OWNER", "nigel"),
        comment(BLOCK, blockedAt),
        comment("drive-by", iso(NOW - 4 * 3_600_000), "NONE", "rando"),
        comment("bot note", iso(NOW - 3 * 3_600_000), "BOT", "github-actions"),
        comment("use the other API", iso(NOW - 1 * 3_600_000), "MEMBER", "nigel"),
      ]),
  }
  const [row] = await needsHumanFeed(deps, NOW)
  assertEquals(row.blockedAt, blockedAt)
  assertEquals(row.humanReply?.author, "nigel")
  assertEquals(row.humanReply?.body, "use the other API")
})

Deno.test("the newest stop or block marker is the block time", async () => {
  const deps: TriageDeps = {
    listIssues: () => Promise.resolve([issue(1, ["factory:blocked", "factory:needs-human"])]),
    listComments: () =>
      Promise.resolve([
        comment(BLOCK, iso(NOW - 10 * 3_600_000)),
        comment("factory: stopped by a human.", iso(NOW - 2 * 3_600_000)),
      ]),
  }
  const [row] = await needsHumanFeed(deps, NOW)
  assertEquals(row.awaitingSince, 2)
})

Deno.test("feed sorts replied tickets first, then longest-waiting", async () => {
  const labels = ["factory:blocked", "factory:needs-human"]
  const byNumber: Record<number, CommentInfo[]> = {
    1: [comment(BLOCK, iso(NOW - 1 * 3_600_000))],
    2: [comment(BLOCK, iso(NOW - 8 * 3_600_000))],
    3: [
      comment(BLOCK, iso(NOW - 6 * 3_600_000)),
      comment("ok", iso(NOW - 1 * 3_600_000), "MEMBER", "nigel"),
    ],
  }
  const deps: TriageDeps = {
    listIssues: () => Promise.resolve([1, 2, 3].map((n) => issue(n, labels))),
    listComments: (n) => Promise.resolve(byNumber[n]),
  }
  const rows = await needsHumanFeed(deps, NOW)
  assertEquals(rows.map((r) => r.number), [3, 2, 1])
})

Deno.test("stale sweep: exactly N days idle is not stale; older is; longest-idle first", async () => {
  const deps = {
    listIssues: (labels: string[]) =>
      Promise.resolve(
        labels[0] === "factory:ready"
          ? [
            issue(1, ["factory:ready"], iso(NOW - 7 * DAY)),
            issue(2, ["factory:ready"], iso(NOW - 9 * DAY)),
            issue(3, ["factory:ready"], iso(NOW - 7 * DAY - 1)),
          ]
          : [],
      ),
  }
  const rows = await staleSweep(deps, NOW, 7)
  assertEquals(rows.map((r) => r.number), [2, 3])
  assertEquals(rows[0].idleDays, 9)
})

Deno.test("stale sweep de-duplicates an issue listed under several labels", async () => {
  const deps = {
    listIssues: () =>
      Promise.resolve([issue(4, ["factory:draft", "factory:review"], iso(NOW - 10 * DAY))]),
  }
  const rows = await staleSweep(deps, NOW, 7)
  assertEquals(rows.map((r) => r.number), [4])
})

Deno.test("unblock swaps labels to ready by default, draft on request, and comments who", async () => {
  const calls: { edit?: unknown; comment?: unknown } = {}
  const deps = (labels: string[]): UnblockDeps => ({
    getIssue: (n) => Promise.resolve(issue(n, labels)),
    editLabels: (n, change) => {
      calls.edit = { n, change }
      return Promise.resolve()
    },
    comment: (n, body) => {
      calls.comment = { n, body }
      return Promise.resolve()
    },
    login: () => Promise.resolve("nigel"),
  })
  await unblock(deps(["factory:blocked", "factory:needs-human"]), 7, "ready")
  assertEquals(calls.edit, {
    n: 7,
    change: { add: ["factory:ready"], remove: ["factory:blocked", "factory:needs-human"] },
  })
  assertEquals(calls.comment, { n: 7, body: "factory: unblocked by nigel (ready)." })

  await unblock(deps(["factory:blocked"]), 8, "draft")
  assertEquals(calls.edit, {
    n: 8,
    change: { add: ["factory:draft"], remove: ["factory:blocked", "factory:needs-human"] },
  })
})

Deno.test("unblock refuses an issue that is not blocked and changes nothing", async () => {
  let touched = false
  await assertRejects(
    () =>
      unblock(
        {
          getIssue: (n) => Promise.resolve(issue(n, ["factory:ready"])),
          editLabels: () => {
            touched = true
            return Promise.resolve()
          },
          comment: () => {
            touched = true
            return Promise.resolve()
          },
          login: () => Promise.resolve("nigel"),
        },
        9,
        "ready",
      ),
    Error,
    "not blocked",
  )
  assertEquals(touched, false)
})
