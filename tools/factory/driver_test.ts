import { assertEquals, assertStringIncludes } from "@std/assert"
import {
  blockComment,
  blockLabels,
  type DepState,
  depStateOf,
  describeHolds,
  followUpBody,
  followUpOf,
  gateFailurePrompt,
  holdsOf,
  keepsBranch,
  ownedBranchAt,
  parseVerdict,
  pickNext,
  prTitle,
  renderPrBody,
  restartCommands,
  restartConflict,
  reviewFailurePrompt,
  rewireDependsOn,
  shouldCreateFollowUp,
  workspaceLabel,
  workspacePrefix,
} from "./driver.ts"
import { makeTicket, missingSections } from "./ticket.ts"
import type { Issue } from "./gh.ts"
import type { Stamp } from "./gate.ts"

const issue = (number: number, body = "", title = `t${number}`): Issue => ({
  number,
  title,
  body,
  labels: ["factory:ready"],
  state: "OPEN",
  createdAt: "2026-01-01T00:00:00Z",
  updatedAt: "2026-01-01T00:00:00Z",
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

Deno.test("a parent with an open follow-up gets no second one; any other blocked ticket gets one", () => {
  assertEquals(shouldCreateFollowUp({ issue: 170, existingFollowUps: [170] }), false)
  assertEquals(shouldCreateFollowUp({ issue: 170, existingFollowUps: [171] }), true)
  assertEquals(shouldCreateFollowUp({ issue: 166, existingFollowUps: [] }), true)
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
  createdAt: "2026-01-01T00:00:00Z",
  updatedAt: "2026-01-01T00:00:00Z",
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

Deno.test("a needs-human block adds factory:needs-human and says so in its comment; others do not", () => {
  assertEquals(blockLabels(false), ["factory:blocked"])
  assertEquals(blockLabels(true), ["factory:blocked", "factory:needs-human"])
  assertEquals(blockComment("gate broke", false), "factory: blocked.\n\ngate broke")
  assertEquals(
    blockComment("gate broke", true),
    "factory: blocked (needs human).\n\ngate broke",
  )
})

Deno.test("workspaceLabel carries the ticket prefix that re-pick matches on", () => {
  const label = workspaceLabel(7, "Fix the thing")
  assertEquals(label, "#7 Fix the thing")
  assertEquals(label.startsWith(workspacePrefix(7)), true)
  assertEquals(label.startsWith(workspacePrefix(70)), false)
  assertEquals(workspaceLabel(7, "x".repeat(200)).length, 60)
})

Deno.test("keepsBranch keeps a branch only when it has commits past base", () => {
  assertEquals(keepsBranch(0), false)
  assertEquals(keepsBranch(1), true)
})

Deno.test("restartCommands always removes the worktree; the branch goes only when not kept", () => {
  const remove = ["git", "-C", "repo", "worktree", "remove", "--force", "wt"]
  assertEquals(restartCommands("repo", "wt", "factory/1-x", true), [remove])
  assertEquals(restartCommands("repo", "wt", "factory/1-x", false), [
    remove,
    ["git", "-C", "repo", "branch", "-D", "factory/1-x"],
  ])
})

Deno.test("restartConflict stops a re-pick on a checkout it does not own", () => {
  const path = "C:/wt/wt-factory-7"
  assertEquals(restartConflict([{ path, branch: "factory/7-x" }], path, "factory/7-x"), undefined)
  assertEquals(restartConflict([], path, "factory/7-x"), undefined)
  assertEquals(
    restartConflict(
      [{ path: "C:\\wt\\wt-factory-7\\", branch: "factory/7-x" }],
      path,
      "factory/7-x",
    ),
    undefined,
  )
  assertEquals(
    restartConflict([{ path, branch: "feature/human" }], path, "factory/7-x"),
    `${path} holds feature/human, not factory/7-x`,
  )
  assertEquals(
    restartConflict([{ path, branch: undefined }], path, "factory/7-x"),
    `${path} holds a detached checkout, not factory/7-x`,
  )
  assertEquals(
    restartConflict([{ path: "C:/wt/other", branch: "factory/7-x" }], path, "factory/7-x"),
    "factory/7-x is checked out at C:/wt/other",
  )
})

Deno.test("ownedBranchAt keeps a retitled ticket on the branch its own checkout holds", () => {
  const path = "C:/wt/wt-factory-7"
  const held = [{ path, branch: "factory/7-old-title" }]
  assertEquals(ownedBranchAt(held, path, "factory/7-"), "factory/7-old-title")
  assertEquals(ownedBranchAt([{ path, branch: "feature/human" }], path, "factory/7-"), undefined)
  assertEquals(ownedBranchAt([{ path, branch: "factory/77-x" }], path, "factory/7-"), undefined)
  assertEquals(
    ownedBranchAt([{ path: "C:/wt/other", branch: "factory/7-x" }], path, "factory/7-"),
    undefined,
  )
  assertEquals(
    restartConflict(held, path, ownedBranchAt(held, path, "factory/7-") ?? "factory/7-new"),
    undefined,
  )
})
