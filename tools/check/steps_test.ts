import { assertEquals } from "@std/assert"
import type { CheckOptions } from "./args.ts"
import { classifyFiles, exitCode, formatStep, planSteps, type StepEvent } from "./steps.ts"

const options = (over: Partial<CheckOptions> = {}): CheckOptions => ({
  noFix: false,
  dryRun: false,
  help: false,
  ...over,
})

Deno.test("classification splits C++ under a src subdirectory or tests from JSON from the rest", () => {
  const diff = classifyFiles([
    "src/lighting/render_state.cpp",
    "src/map.cpp",
    "tests/map_test.cpp",
    "tests/catch/catch_test.cpp",
    "data/json/items/tool.json",
    "deno.jsonc",
    "docs/en/dev/guides/ci.md",
  ])
  assertEquals(diff.cpp, [
    "src/lighting/render_state.cpp",
    "tests/map_test.cpp",
    "tests/catch/catch_test.cpp",
  ])
  assertEquals(diff.json, ["data/json/items/tool.json"])
  assertEquals(diff.all.length, 7)
})

Deno.test("a change to the Deno tooling plans a tools step before the build; other changes do not", () => {
  assertEquals(
    planSteps(options({ noFix: true }), classifyFiles(["tools/check/main.ts", "src/map.cpp"])),
    ["guard", "tools", "build", "tests", "tidy"],
  )
  assertEquals(
    planSteps(options({ noFix: true }), classifyFiles(["scripts/changelog/main.ts"])),
    ["guard", "tools", "build", "tests", "tidy"],
  )
  assertEquals(
    planSteps(options({ noFix: true }), classifyFiles(["src/map.cpp", "docs/a.md"])),
    ["guard", "build", "tests", "tidy"],
  )
})

Deno.test("the full plan is guard, autofix, build, tests, tidy, and coop only for a networking change", () => {
  assertEquals(planSteps(options(), classifyFiles(["src/lighting/snapshot.cpp"])), [
    "guard",
    "autofix",
    "build",
    "tests",
    "tidy",
  ])
  assertEquals(
    planSteps(options(), classifyFiles(["src/coop_server.cpp", "src/net_messages.h"])).at(-1),
    "coop",
  )
})

Deno.test("a docs-only change plans no auto-fix and no co-op run", () => {
  assertEquals(planSteps(options(), classifyFiles(["docs/en/dev/guides/ci.md"])), [
    "guard",
    "build",
    "tests",
    "tidy",
  ])
})

Deno.test("--no-fix and --dry-run both drop the auto-fix; --dry-run stops there", () => {
  const cpp = classifyFiles(["src/lighting/snapshot.cpp"])
  assertEquals(planSteps(options({ noFix: true }), cpp), ["guard", "build", "tests", "tidy"])
  assertEquals(planSteps(options({ dryRun: true }), cpp), ["guard"])
  assertEquals(planSteps(options({ dryRun: true, noFix: true }), classifyFiles(["a.json"])), [
    "guard",
  ])
})

Deno.test("a step line leads with the status and carries the step, the seconds and the detail", () => {
  const event: StepEvent = {
    at: "2026-10-13T00:00:00.000Z",
    step: "tests",
    status: "fail",
    durationMs: 12_340,
    detail: "1459 cases, 2 failed",
    base: "origin/feature/improvements",
  }
  assertEquals(formatStep(event), "FAIL    tests      12.3s  1459 cases, 2 failed")
  assertEquals(
    formatStep({ ...event, status: "skipped", durationMs: 0 }).startsWith("SKIPPED tests"),
    true,
  )
})

Deno.test("one failed step fails the run; skipped steps decide nothing", () => {
  assertEquals(exitCode([{ status: "pass" }, { status: "skipped" }]), 0)
  assertEquals(exitCode([{ status: "skipped" }, { status: "fail" }]), 1)
  assertEquals(exitCode([]), 0)
})
