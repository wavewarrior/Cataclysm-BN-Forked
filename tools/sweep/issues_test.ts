import { assertEquals } from "@std/assert"
import {
  caseBody,
  caseTitle,
  execute,
  labelFor,
  planActions,
  planSummary,
  summaryBody,
  summaryTitle,
} from "./issues.ts"
import type { Gh, IssueAction, IssueRef } from "./types.ts"

const recordingGh = () => {
  const calls: string[] = []
  const gh: Gh = {
    openIssues: () => Promise.resolve([] as IssueRef[]),
    createIssue: (title, _body, label) => {
      calls.push(`create ${label} ${title}`)
      return Promise.resolve(42)
    },
    commentIssue: (number, _body) => {
      calls.push(`comment ${number}`)
      return Promise.resolve()
    },
  }
  return { gh, calls }
}

Deno.test("titles and labels follow the contract exactly", () => {
  assertEquals(
    caseTitle("vehicle ramp test", "windows"),
    "[platform-sweep] vehicle ramp test (windows)",
  )
  assertEquals(labelFor("macos"), "platform:macos")
  assertEquals(summaryTitle("windows"), "[platform-sweep] summary (windows)")
})

Deno.test("a failure with no open issue of the exact title is created", () => {
  const actions = planActions(
    [{ caseName: "metal only", body: "b" }],
    "macos",
    [{ number: 5, title: "[platform-sweep] something else (macos)" }],
  )
  assertEquals(actions, [{
    kind: "create",
    title: "[platform-sweep] metal only (macos)",
    body: "b",
  }])
})

Deno.test("a failure with an open issue of the exact title is commented on, never reopened", () => {
  const actions = planActions(
    [{ caseName: "metal only", body: "b" }],
    "macos",
    [{ number: 77, title: "[platform-sweep] metal only (macos)" }],
  )
  assertEquals(actions, [{
    kind: "comment",
    title: "[platform-sweep] metal only (macos)",
    number: 77,
    body: "b",
  }])
})

Deno.test("the same case on the other platform is a different issue", () => {
  const actions = planActions([{ caseName: "shared name", body: "b" }], "windows", [
    { number: 9, title: "[platform-sweep] shared name (macos)" },
  ])
  assertEquals(actions[0].kind, "create")
})

Deno.test("the summary comments when the summary issue is open, creates when not", () => {
  assertEquals(planSummary("body", "windows", []).kind, "create")
  assertEquals(
    planSummary("body", "windows", [{ number: 3, title: "[platform-sweep] summary (windows)" }]),
    { kind: "comment", title: "[platform-sweep] summary (windows)", number: 3, body: "body" },
  )
})

Deno.test("executing actions goes through the injected gh only", async () => {
  const { gh, calls } = recordingGh()
  const actions: IssueAction[] = [
    { kind: "create", title: "t1", body: "b1" },
    { kind: "comment", title: "t2", number: 12, body: "b2" },
  ]
  const lines = await execute(gh, actions, "windows")
  assertEquals(calls, ["create platform:windows t1", "comment 12"])
  assertEquals(lines, ["opened #42: t1", "commented on #12: t2"])
})

Deno.test("a case body carries platform, range, case name and excerpt", () => {
  const body = caseBody({
    platform: "windows",
    caseName: "vehicle ramp test 60",
    rangeLabel: "aaaabbbb..ccccdddd (3 commits)",
    rangeLines: ["ccccddd1 fix lighting", "ccccddd2 tune vehicles", "cccddddd init tests"],
    invocation: "suite",
    excerpt: "REQUIRE FAILED: dist == 60",
  })
  assertEquals(body.includes("**Platform:** windows"), true)
  assertEquals(body.includes("`vehicle ramp test 60`"), true)
  assertEquals(body.includes("aaaabbbb..ccccdddd (3 commits)"), true)
  assertEquals(body.includes("fix lighting"), true)
  assertEquals(body.includes("REQUIRE FAILED: dist == 60"), true)
})

Deno.test("a fenced excerpt with backticks inside still fences", () => {
  const body = caseBody({
    platform: "macos",
    caseName: "tricky",
    rangeLabel: "x (1 commit)",
    rangeLines: [],
    invocation: "suite",
    excerpt: "value `with` backticks",
  })
  assertEquals(body.includes("``\nvalue `with` backticks\n``"), true)
})

Deno.test("the summary body separates the buckets", () => {
  const body = summaryBody({
    platform: "macos",
    rangeLabel: "r",
    exitCodes: { suite: 1 },
    platformOnly: ["solo"],
    quarantined: [{ name: "flaky", expires: "2026-10-27" }],
    expiredStillFailing: [{ name: "old", expires: "2026-10-01" }],
    failsEverywhere: ["both"],
  })
  assertEquals(body.includes("- `solo`"), true)
  assertEquals(body.includes("- `flaky` until 2026-10-27"), true)
  assertEquals(body.includes("- `old` (expired 2026-10-01)"), true)
  assertEquals(body.includes("- `both`"), true)
  assertEquals(body.includes("`suite` exit 1"), true)
})
