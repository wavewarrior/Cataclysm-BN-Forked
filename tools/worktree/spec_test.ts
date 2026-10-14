import { assertEquals, assertThrows } from "@std/assert"
import {
  BRANCH_TYPES,
  copyPlan,
  formatAge,
  formatGib,
  formatSize,
  formatTable,
  gitDirsEqual,
  herdrCreateArgs,
  listRow,
  mslNames,
  parseNewArgs,
  parseSpec,
  PRESET,
  removePlan,
  type RowInput,
} from "./spec.ts"

Deno.test("a spec is <type>/<issue-id>/<slug> with one of the six types", () => {
  for (const type of BRANCH_TYPES) {
    assertEquals(parseSpec(`${type}/198/add-worktree-command`), {
      type,
      issueId: 198,
      slug: "add-worktree-command",
    })
  }
  assertEquals(parseSpec("FEAT/7/Demo".toLowerCase()), { type: "feat", issueId: 7, slug: "demo" })
})

Deno.test("a spec that is not <type>/<issue-id>/<slug> is refused", () => {
  for (const bad of ["docs/1/x", "feat/x", "feat/a/b", "feat/1", "feat/1/", "feat/1/A_b"]) {
    assertEquals(parseSpec(bad), null, bad)
  }
})

Deno.test("new defaults to feature/improvements over the base and <root>/<slug> for the path", () => {
  const cmd = parseNewArgs(["feat/198/smoke"], "/repo/../Cataclysm-BN-Forked-worktrees")
  assertEquals(cmd.base, "feature/improvements")
  assertEquals(cmd.path, "/repo/../Cataclysm-BN-Forked-worktrees/smoke")
  assertEquals(cmd.spec, { type: "feat", issueId: 198, slug: "smoke" })
})

Deno.test("new takes --base and --path in either order", () => {
  const cmd = parseNewArgs(
    ["--path", "/w/thing", "fix/9/thing", "--base", "origin/feature/improvements"],
    "/unused/root",
  )
  assertEquals(cmd.path, "/w/thing")
  assertEquals(cmd.base, "origin/feature/improvements")
  assertEquals(cmd.spec.type, "fix")
})

Deno.test("new refuses a missing spec, an unknown flag and a flag without a value", () => {
  assertThrows(() => parseNewArgs([], "/r"), Error, "missing")
  assertThrows(() => parseNewArgs(["docs/1/x"], "/r"), Error, "not <type>")
  assertThrows(() => parseNewArgs(["feat/1/x", "--force"], "/r"), Error, "unknown flag")
  assertThrows(() => parseNewArgs(["feat/1/x", "--base"], "/r"), Error, "--base needs a value")
  assertThrows(
    () => parseNewArgs(["feat/1/x", "--base", "--path"], "/r"),
    Error,
    "--base needs a value",
  )
})

Deno.test("the herdr call creates the branch from the base at the path, trusted", () => {
  const cmd = parseNewArgs(["feat/198/smoke", "--base", "origin/feature/improvements"], "/r")
  assertEquals(herdrCreateArgs(cmd), [
    "worktree",
    "create",
    "--branch",
    "feat/198/smoke",
    "--base",
    "origin/feature/improvements",
    "--path",
    "/r/smoke",
    "--trust-repository",
  ])
})

Deno.test("the copy step takes exactly the .msl files, sorted, paired primary to worktree", () => {
  const names = ["b.msl", "a.msl", "notes.txt", "lighting/x.hlsl", "c.msl"]
  assertEquals(mslNames(names), ["a.msl", "b.msl", "c.msl"])
  assertEquals(copyPlan("/prim", "/wt", names), [
    { from: "/prim/data/shaders/a.msl", to: "/wt/data/shaders/a.msl" },
    { from: "/prim/data/shaders/b.msl", to: "/wt/data/shaders/b.msl" },
    { from: "/prim/data/shaders/c.msl", to: "/wt/data/shaders/c.msl" },
  ])
  assertEquals(copyPlan("/prim", "/wt", ["readme.md"]), [])
})

const row = (over: Partial<RowInput>): RowInput => ({
  path: "/w/feat-1",
  branch: "feat/1/x",
  present: true,
  primary: false,
  sizeKiB: 2726297,
  ageSeconds: 3 * 86400 + 4 * 3600,
  mergedIntoBase: false,
  ...over,
})

Deno.test("a list row shows branch, GiB size, human age and merged state", () => {
  assertEquals(listRow(row({})), {
    name: "feat-1",
    branch: "feat/1/x",
    size: "2.6 GiB",
    age: "3 d",
    merged: "no",
  })
  assertEquals(listRow(row({ mergedIntoBase: true })).merged, "yes")
  assertEquals(listRow(row({ primary: true })).merged, "primary")
  assertEquals(listRow(row({ branch: "" })).branch, "(detached)")
  assertEquals(listRow(row({ sizeKiB: null, ageSeconds: null })).size, "-")
  assertEquals(listRow(row({ sizeKiB: null, ageSeconds: null })).age, "-")
  assertEquals(listRow(row({ mergedIntoBase: null })).merged, "?")
})

Deno.test("a prunable worktree is reported with dashes, not invented numbers", () => {
  assertEquals(listRow(row({ present: false })), {
    name: "feat-1",
    branch: "feat/1/x",
    size: "-",
    age: "-",
    merged: "-",
  })
})

Deno.test("sizes, ages and free space print in the units the disk talk uses", () => {
  assertEquals(formatSize(1048576), "1.0 GiB")
  assertEquals(formatSize(2 * 1048576 + 524288), "2.5 GiB")
  assertEquals(formatGib(68 * 1073741824), "68.0 GiB")
  assertEquals(formatAge(30), "1 m")
  assertEquals(formatAge(37 * 60), "37 m")
  assertEquals(formatAge(5 * 3600 + 1800), "5 h")
  assertEquals(formatAge(12 * 86400), "12 d")
})

Deno.test("the list table aligns one column per header", () => {
  const table = formatTable([
    listRow(row({})),
    listRow(row({ path: "/w/long-name-here", branch: "", sizeKiB: null, mergedIntoBase: true })),
  ])
  const lines = table.split("\n")
  assertEquals(lines[0], "worktree        branch      size     age  merged")
  assertEquals(lines[1], "feat-1          feat/1/x    2.6 GiB  3 d  no")
  assertEquals(lines[2], "long-name-here  (detached)  -        3 d  yes")
})

Deno.test("remove announces the checkout and every build dir it will delete", () => {
  assertEquals(removePlan("/w/smoke", ["/w/smoke/out/build/osx-arm-slim"]), [
    "worktree /w/smoke",
    "build dir /w/smoke/out/build/osx-arm-slim",
  ])
  assertEquals(removePlan("/w/smoke", []), ["worktree /w/smoke"])
})

Deno.test("the primary checkout is where git-dir and common-dir agree", () => {
  assertEquals(gitDirsEqual("/r/.git", "/r/.git"), true)
  assertEquals(gitDirsEqual("/r/.git/", "/r/.git"), true)
  assertEquals(
    gitDirsEqual("/r/wt/.git", "/r/.git"),
    false,
    "a linked worktree must not pass for the primary",
  )
})

Deno.test("the preset is the single constant #149 will replace", () => {
  assertEquals(PRESET, "osx-arm-slim")
})
