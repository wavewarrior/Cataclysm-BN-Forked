import { assertEquals, assertRejects } from "@std/assert"
import type { CommentInfo, Issue } from "./gh.ts"
import {
  applyActions,
  type ApplyDeps,
  isHumanReply,
  needsHumanFeed,
  staleSweep,
  type TriageDeps,
  unblock,
  type UnblockDeps,
} from "./triage.ts"

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
    listComments: () => Promise.resolve([]),
    editIssue: () => Promise.resolve(),
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
          listComments: () => Promise.resolve([]),
          editIssue: () => {
            touched = true
            return Promise.resolve()
          },
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

Deno.test("unblock with a note appends it to the body under Human guidance", async () => {
  let body: string | undefined
  await unblock(
    {
      getIssue: (n) =>
        Promise.resolve({ ...issue(n, ["factory:blocked"]), body: "## Purpose\n\nx" }),
      listComments: () => Promise.resolve([]),
      editIssue: (_n, text) => {
        body = text
        return Promise.resolve()
      },
      editLabels: () => Promise.resolve(),
      comment: () => Promise.resolve(),
      login: () => Promise.resolve("nigel"),
    },
    3,
    "ready",
    { note: "Use the existing helper." },
  )
  assertEquals(body, "## Purpose\n\nx\n\n## Human guidance\n\nUse the existing helper.\n")
})

Deno.test("unblock to ready without a note takes the newest human reply, never a factory comment", async () => {
  let body: string | undefined
  await unblock(
    {
      getIssue: (n) => Promise.resolve({ ...issue(n, ["factory:blocked"]), body: "b" }),
      listComments: () =>
        Promise.resolve([
          comment("older", iso(NOW - DAY), "MEMBER"),
          comment("newer guidance", iso(NOW), "OWNER", "nigel"),
          comment("factory: unblocked by nigel (ready).", iso(NOW + 1), "OWNER"),
        ]),
      editIssue: (_n, text) => {
        body = text
        return Promise.resolve()
      },
      editLabels: () => Promise.resolve(),
      comment: () => Promise.resolve(),
      login: () => Promise.resolve("nigel"),
    },
    4,
    "ready",
  )
  assertEquals(body, "b\n\n## Human guidance\n\nnewer guidance\n")
})

Deno.test("unblock without a note or a human reply leaves the body untouched", async () => {
  let edited = false
  const deps = (): UnblockDeps => ({
    getIssue: (n) => Promise.resolve(issue(n, ["factory:blocked"])),
    listComments: () =>
      Promise.resolve([
        comment("factory: unblocked by nigel (ready).", iso(NOW), "OWNER"),
        comment("looks fine", iso(NOW), "CONTRIBUTOR"),
      ]),
    editIssue: () => {
      edited = true
      return Promise.resolve()
    },
    editLabels: () => Promise.resolve(),
    comment: () => Promise.resolve(),
    login: () => Promise.resolve("nigel"),
  })
  await unblock(deps(), 5, "ready")
  await unblock(deps(), 5, "draft")
  assertEquals(edited, false)
})

Deno.test("factory's own comments and non-member comments are not human replies", () => {
  assertEquals(
    isHumanReply(comment("factory: unblocked by nigel (ready).", iso(NOW), "OWNER")),
    false,
  )
  assertEquals(isHumanReply(comment("looks good", iso(NOW), "CONTRIBUTOR")), false)
  assertEquals(isHumanReply(comment("looks good", iso(NOW), "MEMBER")), true)
})

const applyDeps = (issues: Issue[], calls: string[]): ApplyDeps => ({
  getIssue: (n) => {
    const found = issues.find((i) => i.number === n)
    return found ? Promise.resolve(found) : Promise.reject(new Error("not found"))
  },
  listComments: () => Promise.resolve([]),
  editIssue: (n) => {
    calls.push(`edit ${n}`)
    return Promise.resolve()
  },
  editLabels: (n, change) => {
    calls.push(`labels ${n} ${JSON.stringify(change)}`)
    return Promise.resolve()
  },
  comment: (n, body) => {
    calls.push(`comment ${n} ${body}`)
    return Promise.resolve()
  },
  closeIssue: (n, note) => {
    calls.push(`close ${n} ${note}`)
    return Promise.resolve()
  },
})

Deno.test("apply runs each valid action, in order, and reports one row per action", async () => {
  const calls: string[] = []
  const deps = applyDeps([
    issue(1, ["factory:blocked", "factory:needs-human"]),
    issue(2, ["factory:ready"]),
    issue(3, ["factory:draft"]),
    issue(4, ["factory:draft"]),
  ], calls)
  const rows = await applyActions(deps, [
    { number: 1, action: "unblock-draft", note: "use the helper" },
    { number: 2, action: "close", note: "superseded" },
    { number: 3, action: "comment", note: "see spec" },
    { number: 4, action: "needs-human", note: "decide" },
  ], "nigel")
  assertEquals(rows.map((r) => r.ok), [true, true, true, true])
  assertEquals(calls, [
    "edit 1",
    `labels 1 {"add":["factory:draft"],"remove":["factory:blocked","factory:needs-human"]}`,
    "comment 1 factory: unblocked by nigel (draft).",
    "close 2 superseded",
    "comment 3 factory: triage — see spec",
    `labels 4 {"add":["factory:blocked","factory:needs-human"],"remove":["factory:ready","factory:in-progress"]}`,
    "comment 4 factory: triage — decide",
  ])
})

Deno.test("apply rejects closed, non-factory, missing-issue, unnoted and unknown actions", async () => {
  const calls: string[] = []
  const deps = applyDeps([
    { ...issue(6, ["factory:ready"]), state: "CLOSED" },
    issue(7, ["bug"]),
    issue(8, ["factory:ready"]),
  ], calls)
  const rows = await applyActions(deps, [
    { number: 6, action: "comment", note: "hi" },
    { number: 7, action: "comment", note: "hi" },
    { number: 8, action: "close" },
    { number: 9, action: "comment", note: "hi" },
    { number: 8, action: "bogus" },
  ], "nigel")
  assertEquals(rows.map((r) => [r.number, r.ok, r.problem]), [
    [6, false, "issue is not open"],
    [7, false, "issue carries no factory: label"],
    [8, false, "close needs a non-empty note"],
    [9, false, "issue does not exist"],
    [8, false, `unknown action "bogus"`],
  ])
  assertEquals(calls, [])
})

Deno.test("apply on a malformed file exits 1", async () => {
  const file = await Deno.makeTempFile({ suffix: ".json" })
  await Deno.writeTextFile(file, "{not json")
  const out = await new Deno.Command(Deno.execPath(), {
    args: ["run", "-A", new URL("./main.ts", import.meta.url).pathname, "triage", "apply", file],
    stdout: "null",
    stderr: "null",
  }).output()
  await Deno.remove(file)
  assertEquals(out.code, 1)
})
