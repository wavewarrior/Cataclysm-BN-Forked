import { assertEquals, assertThrows } from "@std/assert"
import {
  agentCommand,
  BUILD_DIR,
  configureArguments,
  laneBranch,
  laneEnv,
  lanePath,
  lanePrompt,
  parseIssue,
  parseLaneArgs,
  seedCopyCommand,
  slugify,
  tmuxSessionName,
  tmuxStartArgs,
} from "./spec.ts"

Deno.test("a lane is lane-<n> under the root, on branch lane/<n>-<slug>", () => {
  assertEquals(lanePath("/home/u/cbn/lanes", 172), "/home/u/cbn/lanes/lane-172")
  assertEquals(laneBranch(172, "Cut the test init cost"), "lane/172-cut-the-test-init-cost")
  assertEquals(tmuxSessionName(172), "lane-172")
})

Deno.test("a slug is lowercase kebab-case, bounded, and never empty", () => {
  assertEquals(slugify("Fix the  fork runner!!"), "fix-the-fork-runner")
  assertEquals(
    slugify("Co-op: host-side turn authority for vehicle sync payloads"),
    "co-op-host-side-turn-authority-for",
  )
  assertEquals(slugify("###"), "lane")
})

Deno.test("the seed is copied with cp -a into the lane's own build directory", () => {
  assertEquals(seedCopyCommand("/seed/_deps", `/lane/${BUILD_DIR}/_deps`), [
    "cp",
    "-a",
    "/seed/_deps",
    `/lane/${BUILD_DIR}/_deps`,
  ])
})

Deno.test("configure uses the fast preset, points FetchContent at the lane's copy and forbids the network", () => {
  assertEquals(configureArguments("/lane/out/build/linux-fast/_deps", ["box2d-src", "download"]), [
    "cmake",
    "--preset",
    "linux-fast",
    "-DFETCHCONTENT_SOURCE_DIR_BOX2D=/lane/out/build/linux-fast/_deps/box2d-src",
    "-DFETCHCONTENT_FULLY_DISCONNECTED=ON",
  ])
})

Deno.test("every lane shares the compiler cache through a ccache base above all the worktrees", () => {
  assertEquals(laneEnv("/home/u/cbn/lanes"), { CCACHE_BASEDIR: "/home/u/cbn/lanes" })
})

Deno.test("the tmux command runs the agent with the prompt file's contents and logs the transcript", () => {
  const command = agentCommand("omp", "/lane/.check/prompt.md", "/lane/.check/lane.log")
  assertEquals(command, `omp "$(cat '/lane/.check/prompt.md')" >>'/lane/.check/lane.log' 2>&1`)
  assertEquals(tmuxStartArgs("lane-7", "/lane", command), [
    "tmux",
    "new-session",
    "-d",
    "-s",
    "lane-7",
    "-c",
    "/lane",
    "sh",
    "-c",
    command,
  ])
})

Deno.test("the issue is read from gh's JSON and the prompt is the title over the body", () => {
  const issue = parseIssue(`{"number":172,"title":"Do the thing","body":"Because reasons"}`)
  assertEquals(issue, { number: 172, title: "Do the thing", body: "Because reasons" })
  assertEquals(lanePrompt(issue), "# Issue #172: Do the thing\n\nBecause reasons\n")
})

Deno.test("gh output without the three fields is refused, not guessed at", () => {
  assertThrows(
    () => parseIssue(`{"title":"only a title"}`),
    Error,
    "unexpected gh issue view output",
  )
  assertThrows(() => parseIssue(`{"number":"172","title":"t","body":"b"}`), Error, "unexpected")
})

Deno.test("argv dispatch: a bare number starts, the three verbs take their issue", () => {
  assertEquals(parseLaneArgs(["42"]), { kind: "start", issue: 42 })
  assertEquals(parseLaneArgs(["status"]), { kind: "status" })
  assertEquals(parseLaneArgs(["attach", "7"]), { kind: "attach", issue: 7, writable: false })
  assertEquals(parseLaneArgs(["attach", "7", "--write"]), {
    kind: "attach",
    issue: 7,
    writable: true,
  })
  assertEquals(parseLaneArgs(["stop", "7"]), { kind: "stop", issue: 7 })
  assertEquals(parseLaneArgs([]), { kind: "help" })
  assertEquals(parseLaneArgs(["--help"]), { kind: "help" })
})

Deno.test("argv dispatch refuses nonsense instead of guessing an issue", () => {
  for (
    const bad of [["bogus"], ["42", "extra"], ["attach"], ["attach", "x"], ["stop", "1", "2"], [
      "status",
      "1",
    ]]
  ) {
    assertThrows(() => parseLaneArgs(bad), Error)
  }
})
