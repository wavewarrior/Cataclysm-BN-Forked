import { assertEquals } from "@std/assert"
import type { CheckOptions } from "./args.ts"
import {
  classifyFiles,
  exitCode,
  formatStep,
  planSteps,
  type StepEvent,
  testFilesFor,
} from "./steps.ts"

const options = (over: Partial<CheckOptions> = {}): CheckOptions => ({
  noFix: false,
  dryRun: false,
  macos: false,
  help: false,
  ...over,
})

Deno.test("the macOS gate plans guard, tools only for tooling changes, then the macos build", () => {
  const mac = options({ macos: true })
  assertEquals(planSteps(mac, classifyFiles(["src/map.h"])), ["guard", "macos"])
  assertEquals(planSteps(mac, classifyFiles(["src/map.h", "tools/check/main.ts"])), [
    "guard",
    "tools",
    "macos",
  ])
  assertEquals(planSteps({ ...mac, dryRun: true }, classifyFiles(["src/map.h"])), ["guard"])
})

Deno.test("classification takes project C++ under src and tests, not vendored C++, JSON or the rest", () => {
  const diff = classifyFiles([
    "src/lighting/render_state.cpp",
    "src/map.cpp",
    "src/lua/lua.h",
    "tests/map_test.cpp",
    "tests/catch/catch_test.cpp",
    "data/json/items/tool.json",
    "deno.jsonc",
    "docs/en/dev/guides/ci.md",
  ])
  assertEquals(diff.cpp, [
    "src/lighting/render_state.cpp",
    "src/map.cpp",
    "tests/map_test.cpp",
  ])
  assertEquals(diff.json, ["data/json/items/tool.json"])
  assertEquals(diff.all.length, 8)
})

Deno.test("tooling tests are the changed tests plus each changed module's own, never the module", () => {
  const present = new Set(["tools/check/steps_test.ts", "scripts/utils_test.ts"])
  assertEquals(
    testFilesFor(
      ["tools/check/steps.ts", "scripts/gen_cli_docs.ts", "scripts/utils_test.ts"],
      present,
    ),
    ["scripts/utils_test.ts", "tools/check/steps_test.ts"],
  )
  assertEquals(testFilesFor(["scripts/gen_cli_docs.ts"], present), [])
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
