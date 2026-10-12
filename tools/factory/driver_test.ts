import { assertEquals, assertStringIncludes } from "@std/assert"
import {
  type DepState,
  depStateOf,
  describeHolds,
  followUpBody,
  followUpOf,
  gateFailurePrompt,
  holdsOf,
  parseVerdict,
  pickNext,
  planWorktree,
  prTitle,
  renderPrBody,
  reviewFailurePrompt,
  rewireDependsOn,
} from "./driver.ts"
import { makeTicket, missingSections } from "./ticket.ts"
import type { Issue } from "./gh.ts"
import type { WorktreeEntry } from "./herdr.ts"
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
    infraSteps: [],
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

const BLOCKED =
  "## Goal\n\nBuild it.\n\n## Acceptance\n\nIt works.\n\n## Touches\n\n- src/vehicle.cpp\n\n## Test tags\n\n- [vehicle]\n"

Deno.test("rewireDependsOn moves only the Depends on item for the blocked ticket", () => {
  const body =
    "## Goal\n\nSee #166 in prose.\n\n## Depends on\n\n- #166\n- #1660\n1. #166\n\n## Touches\n\n- #166\n"
  assertEquals(
    rewireDependsOn(body, 166, 170),
    "## Goal\n\nSee #166 in prose.\n\n## Depends on\n\n- #170\n- #1660\n1. #170\n\n## Touches\n\n- #166\n",
  )
})

Deno.test("a follow-up names the blocked ticket it unblocks; an ordinary ticket names none", () => {
  const blocked = issue(166, BLOCKED)
  assertEquals(followUpOf(makeTicket(blocked).sections), undefined)
  const body = followUpBody({ issue: blocked, ticket: makeTicket(blocked), reason: "gate failed" })
  assertEquals(followUpOf(makeTicket(issue(170, body)).sections), 166)
})

Deno.test("a follow-up is a runnable ticket that inherits the blocked ticket's touches and tags", () => {
  const blocked = issue(166, BLOCKED)
  const body = followUpBody({
    issue: blocked,
    ticket: makeTicket(blocked),
    reason: "FAIL vehicle_x",
  })
  const ticket = makeTicket(issue(170, body))
  assertEquals(missingSections(ticket.sections), [])
  assertEquals(ticket.sections.Touches, "- src/vehicle.cpp")
  assertEquals(ticket.sections["Test tags"], "- [vehicle]")
  assertStringIncludes(body, "FAIL vehicle_x")
})

Deno.test("a dependent moved to a follow-up waits for the follow-up, not the blocked ticket", async () => {
  const before = issue(7, "## Depends on\n\n- #166\n")
  const after = issue(7, rewireDependsOn(before.body, 166, 170))
  const closed = new Set([170])
  const isClosed = (n: number) => Promise.resolve(closed.has(n))
  assertEquals(await pickNext([before], isClosed), undefined)
  assertEquals((await pickNext([after], isClosed))?.number, 7)
})

const wtBranch = "factory/170-characterize-the-three-turn-cycle-copies"
const wtPath = "C:/WORK/GIT_REPOS/wt-factory-170"
const held = (over: Partial<WorktreeEntry>): WorktreeEntry => ({
  path: wtPath,
  branch: wtBranch,
  openWorkspaceId: undefined,
  ...over,
})

Deno.test("planWorktree reuses the existing checkout and closes the workspace holding it", () => {
  assertEquals(
    planWorktree([held({ openWorkspaceId: "wG" })], { path: wtPath, branch: wtBranch }),
    { action: "reuse", closeWorkspace: "wG" },
  )
})

Deno.test("planWorktree reuses a checkout that has no open workspace", () => {
  assertEquals(
    planWorktree([held({})], { path: wtPath, branch: wtBranch }),
    { action: "reuse", closeWorkspace: undefined },
  )
})

Deno.test("planWorktree creates a worktree when nothing holds the path or the branch", () => {
  assertEquals(
    planWorktree([held({ path: "C:/WORK/GIT_REPOS/wt-factory-166", branch: "factory/166-x" })], {
      path: wtPath,
      branch: wtBranch,
    }),
    { action: "create" },
  )
})

Deno.test("planWorktree refuses a path that is checked out on another branch", () => {
  const plan = planWorktree([held({ branch: "factory/other" })], { path: wtPath, branch: wtBranch })
  assertEquals(plan.action, "conflict")
  assertStringIncludes(plan.action === "conflict" ? plan.reason : "", "factory/other")
})

Deno.test("planWorktree refuses a branch that is checked out at another path", () => {
  const plan = planWorktree([held({ path: "C:/WORK/GIT_REPOS/wt-elsewhere" })], {
    path: wtPath,
    branch: wtBranch,
  })
  assertEquals(plan.action, "conflict")
  assertStringIncludes(plan.action === "conflict" ? plan.reason : "", "wt-elsewhere")
})

Deno.test("planWorktree matches a path across slash style, case, trailing slash and the \\\\?\\ prefix", () => {
  assertEquals(
    planWorktree([
      held({ path: "\\\\?\\C:\\WORK\\GIT_REPOS\\WT-factory-170\\", openWorkspaceId: "wH" }),
    ], {
      path: "c:/work/git_repos/wt-factory-170",
      branch: wtBranch,
    }),
    { action: "reuse", closeWorkspace: "wH" },
  )
})

const issueOf = (
  number: number,
  labels: string[] = [],
  state = "OPEN",
  deps: number[] = [],
): Issue => ({
  number,
  title: `t${number}`,
  body: deps.length > 0 ? `## Depends on\n${deps.map((d) => `- #${d}`).join("\n")}\n` : "",
  labels,
  state,
})

Deno.test("depStateOf reads closed first, then the factory label, else open", () => {
  assertEquals(depStateOf(undefined), "missing")
  assertEquals(depStateOf(issueOf(1, ["factory:blocked"], "CLOSED")), "closed")
  assertEquals(depStateOf(issueOf(1, ["factory:ready"])), "ready")
  assertEquals(depStateOf(issueOf(1, ["factory:blocked"])), "blocked")
  assertEquals(depStateOf(issueOf(1, ["factory:review"])), "review")
  assertEquals(depStateOf(issueOf(1, ["factory:in-progress"])), "in-progress")
  assertEquals(depStateOf(issueOf(1)), "open")
})

Deno.test("holdsOf lists only tickets with an unclosed dependency and asks for each dependency once", async () => {
  const states = new Map<number, DepState>([[170, "blocked"], [171, "closed"], [172, "ready"]])
  const asked: number[] = []
  const holds = await holdsOf(
    [
      issueOf(175, [], "OPEN", [170]),
      issueOf(172, [], "OPEN", [170, 171]),
      issueOf(173, [], "OPEN", [172]),
      issueOf(174),
      issueOf(176, [], "OPEN", [999]),
    ],
    (n) => {
      asked.push(n)
      return Promise.resolve(states.get(n) ?? "missing")
    },
  )
  assertEquals(holds, [
    { issue: 172, waitingOn: [{ number: 170, state: "blocked" }] },
    { issue: 173, waitingOn: [{ number: 172, state: "ready" }] },
    { issue: 175, waitingOn: [{ number: 170, state: "blocked" }] },
    { issue: 176, waitingOn: [{ number: 999, state: "missing" }] },
  ])
  assertEquals(asked, [170, 171, 172, 999])
})

Deno.test("describeHolds names each held ticket's blockers with their states", () => {
  assertEquals(
    describeHolds([
      {
        issue: 172,
        waitingOn: [{ number: 170, state: "blocked" }, { number: 171, state: "blocked" }],
      },
      { issue: 173, waitingOn: [{ number: 172, state: "ready" }] },
    ]),
    "#172 waits on #170 (blocked), #171 (blocked); #173 waits on #172 (queued)",
  )
  assertEquals(describeHolds([]), "")
})

Deno.test("gate failure prompt sends the implementer to format with the base", () => {
  const prompt = gateFailurePrompt(
    [{ name: "wsl-lane", tail: "src/game.h" }],
    "origin/feature/improvements",
  )
  assertStringIncludes(prompt, "tools/factory/format.ts --base origin/feature/improvements")
  assertStringIncludes(prompt, "### wsl-lane")
})

Deno.test("review failure prompt sends the implementer to format with the base", () => {
  const prompt = reviewFailurePrompt("- handle the empty list", "origin/feature/improvements")
  assertStringIncludes(prompt, "tools/factory/format.ts --base origin/feature/improvements")
  assertStringIncludes(prompt, "- handle the empty list")
})
