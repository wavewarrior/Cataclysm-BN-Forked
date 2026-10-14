import { assertEquals } from "@std/assert"
import { invocations, pickMacosPlan, pickWindowsPlan, stalenessProblem, sweepPlan } from "./plan.ts"

const BOX_PRESETS = JSON.stringify({
  configurePresets: [{
    name: "win",
    inherits: "windows-tiles-sounds-x64-msvc",
    binaryDir: "${sourceDir}/out/msvc",
  }],
  buildPresets: [{ name: "win-rel", configurePreset: "win", configuration: "RelWithDebInfo" }],
})

Deno.test("the box's win-rel user preset wins over the manifest one", () => {
  const plan = pickWindowsPlan(BOX_PRESETS, "C:/repo")
  assertEquals(plan.configure, ["cmake", "--preset", "win"])
  assertEquals(plan.build[3], "win-rel")
  assertEquals(plan.binary, "C:/repo/out/msvc/tests/RelWithDebInfo/cata_test-tiles.exe")
})

Deno.test("without a user preset file the manifest presets are used", () => {
  const plan = pickWindowsPlan(null, "C:/repo")
  assertEquals(plan.configure, ["cmake", "--preset", "windows-tiles-sounds-x64-msvc"])
  assertEquals(plan.build[3], "windows-msvc-relwithdebinfo")
  assertEquals(
    plan.binary,
    "C:/repo/out/build/windows-tiles-sounds-x64-msvc/tests/RelWithDebInfo/cata_test-tiles.exe",
  )
})

Deno.test("a malformed user preset file falls back instead of guessing", () => {
  assertEquals(pickWindowsPlan("{ not json", "R").configure[2], "windows-tiles-sounds-x64-msvc")
})

Deno.test("macOS sweeps the osx-arm-slim preset", () => {
  const plan = pickMacosPlan("/repo")
  assertEquals(plan.configure, ["cmake", "--preset", "osx-arm-slim"])
  assertEquals(plan.binary, "/repo/out/build/osx-arm-slim/tests/cata_test-tiles")
})

Deno.test("the suite runs everywhere; the render tier only on Windows", () => {
  const win = sweepPlan("windows", "C:/r", "C:/r/.sweep/logs", BOX_PRESETS)
  assertEquals(win.tests.map((t) => t.label), ["suite", "render"])
  assertEquals(win.tests[1].args.includes("[render]"), true)
  const mac = sweepPlan("macos", "/r", "/r/.sweep/logs", null)
  assertEquals(mac.tests.map((t) => t.label), ["suite"])
  for (const test of [...win.tests, ...mac.tests]) {
    assertEquals(test.args.slice(1, 3), ["--rng-seed", "1"])
    assertEquals(test.args.some((a) => a.startsWith("--user-dir=")), true)
    assertEquals(test.args.some((a) => a.startsWith("XML::out=")), true)
    assertEquals(test.report.endsWith(`${test.label}.xml`), true)
  }
})

Deno.test("invocations carry the binary first", () => {
  const tests = invocations("macos", "/bin/cata_test-tiles", "/logs", "/logs/userdir")
  assertEquals(tests[0].args[0], "/bin/cata_test-tiles")
})

Deno.test("a binary older than the last C++ commit is refused", () => {
  const problem = stalenessProblem(1000, 900, false, 2000)
  assertEquals(problem !== null, true)
  assertEquals(problem!.includes("predates the last C++ commit"), true)
})

Deno.test("a build that claimed to link the test target must have touched the binary", () => {
  assertEquals(stalenessProblem(1000, 5000, true, 0) !== null, true)
  assertEquals(stalenessProblem(6000, 5000, true, 0), null)
})

Deno.test("a fresh binary with an untouched-but-newer-than-C++ mtime passes", () => {
  assertEquals(stalenessProblem(3000, 5000, false, 2000), null)
})
